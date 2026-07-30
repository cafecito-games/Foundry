from __future__ import annotations

import importlib.util
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from types import ModuleType

REPO_ROOT = Path(__file__).resolve().parents[2]
HOST_CONTRACT_PATH = REPO_ROOT / "platform/android/android_host_contract.py"
KEEP_RULES_PATH = REPO_ROOT / "platform/android/java/lib/proguard-rules.pro"


def load_contract() -> ModuleType:
    if not HOST_CONTRACT_PATH.is_file():
        raise AssertionError(f"missing Android host JNI contract: {HOST_CONTRACT_PATH}")
    spec = importlib.util.spec_from_file_location("android_host_contract_for_test", HOST_CONTRACT_PATH)
    if spec is None or spec.loader is None:
        raise AssertionError(f"unable to load Android host JNI contract: {HOST_CONTRACT_PATH}")
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


class AndroidHostContractTests(unittest.TestCase):
    contract: ModuleType

    @classmethod
    def setUpClass(cls) -> None:
        cls.contract = load_contract()

    def test_checked_in_keep_rules_match_the_derived_contract(self) -> None:
        derived = self.contract.emit_keep_rules(self.contract.derive_host_members(REPO_ROOT))

        self.assertEqual(
            derived,
            KEEP_RULES_PATH.read_text(encoding="utf-8"),
            "platform/android/java/lib/proguard-rules.pro is stale; regenerate with "
            "'python3 platform/android/android_host_contract.py --emit-keep-rules'",
        )

    def test_every_native_lookup_site_is_scanned(self) -> None:
        scanned = set(self.contract.SCANNED_SOURCES)
        android_root = REPO_ROOT / "platform/android"
        performing_lookups = {
            f"platform/android/{path.name}"
            for path in sorted(android_root.glob("*.cpp"))
            if any(
                marker in path.read_text(encoding="utf-8")
                for marker in ("GetMethodID", "GetStaticMethodID", "GetFieldID", "GetStaticFieldID")
            )
        }

        self.assertEqual(
            performing_lookups,
            scanned,
            "a native source performs JNI member lookups without being covered by the host contract",
        )

    def test_contract_covers_the_members_the_release_abort_lost(self) -> None:
        members = {
            (member.class_name, member.member_name, member.descriptor)
            for member in self.contract.derive_host_members(REPO_ROOT)
        }

        for member_name, descriptor in (
            ("restart", "()V"),
            ("setKeepScreenOn", "(Z)V"),
            ("getActivity", "()Landroid/app/Activity;"),
            ("onFoundrySetupCompleted", "()V"),
        ):
            self.assertIn(("games.cafecito.foundry.Foundry", member_name, descriptor), members)

    def test_by_name_classes_keep_their_own_name(self) -> None:
        derived = self.contract.emit_keep_rules(self.contract.derive_host_members(REPO_ROOT))

        # jni_find_class/FindClass resolve these by string, so the class name itself
        # must survive; instance-derived classes only need their members pinned.
        self.assertIn("-keep,includedescriptorclasses class games.cafecito.foundry.Foundry {", derived)
        self.assertIn("-keep,includedescriptorclasses class games.cafecito.foundry.Dictionary {", derived)
        self.assertIn("-keep,includedescriptorclasses class games.cafecito.foundry.variant.Callable {", derived)
        self.assertIn("-keepclassmembers,includedescriptorclasses class games.cafecito.foundry.FoundryIO {", derived)

    def test_keep_rules_never_allow_optimization(self) -> None:
        derived = self.contract.emit_keep_rules(self.contract.derive_host_members(REPO_ROOT))
        directives = "\n".join(line for line in derived.splitlines() if not line.startswith("#"))

        # allowoptimization would let R8 inline or staticize a member, which breaks
        # GetMethodID exactly as a rename does.
        self.assertNotIn("allowoptimization", directives)
        self.assertNotIn("allowobfuscation", directives)
        self.assertNotIn("allowshrinking", directives)

    def test_check_mode_rejects_stale_rules(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            stale = root / "platform/android/java/lib"
            stale.mkdir(parents=True)
            (stale / "proguard-rules.pro").write_text("# stale\n", encoding="utf-8")
            for source in self.contract.SCANNED_SOURCES:
                destination = root / source
                destination.parent.mkdir(parents=True, exist_ok=True)
                destination.write_bytes((REPO_ROOT / source).read_bytes())

            result = subprocess.run(
                [sys.executable, str(HOST_CONTRACT_PATH), "--repo-root", str(root), "--check"],
                capture_output=True,
                text=True,
                check=False,
            )

            self.assertEqual(2, result.returncode)
            self.assertIn("is stale", result.stderr)

    def test_unmapped_instance_lookup_is_rejected(self) -> None:
        source = 'jclass c = env->GetObjectClass(thing);\n_probe = env->GetMethodID(c, "probe", "()V");\n'

        with self.assertRaisesRegex(self.contract.HostContractError, "no declared class"):
            self.contract._scan_source(source, "platform/android/unknown_source.cpp")

    def test_by_name_lookup_needs_no_declaration(self) -> None:
        source = (
            'jclass k = jni_find_class(env, "games/cafecito/foundry/Probe");\n'
            '_probe = env->GetMethodID(k, "probe", "(I)Ljava/lang/String;");\n'
        )

        members, dynamic = self.contract._scan_source(source, "platform/android/probe.cpp")

        self.assertEqual((), tuple(dynamic))
        self.assertEqual(1, len(members))
        self.assertEqual("games.cafecito.foundry.Probe", members[0].class_name)
        self.assertTrue(members[0].resolved_by_name)
        self.assertEqual(
            "java.lang.String probe(int);",
            self.contract.member_signature(members[0]),
        )

    def test_global_reference_alias_keeps_the_resolved_class(self) -> None:
        source = (
            'jclass local = jni_find_class(env, "games/cafecito/foundry/Probe");\n'
            "cached = (jclass)env->NewGlobalRef(local);\n"
            '_probe = env->GetMethodID(cached, "probe", "()V");\n'
        )

        members, _ = self.contract._scan_source(source, "platform/android/probe.cpp")

        self.assertEqual("games.cafecito.foundry.Probe", members[0].class_name)

    def test_reassigned_handle_loses_its_resolved_class(self) -> None:
        # A stale binding would silently attribute the member to the wrong class and
        # emit a keep rule that matches nothing.
        source = (
            'jclass k = jni_find_class(env, "games/cafecito/foundry/Probe");\n'
            "k = some_other_handle;\n"
            '_probe = env->GetMethodID(k, "probe", "()V");\n'
        )

        with self.assertRaisesRegex(self.contract.HostContractError, "no declared class"):
            self.contract._scan_source(source, "platform/android/probe.cpp")

    def test_nested_object_class_alias_stays_instance_derived(self) -> None:
        source = "_cls = (jclass)env->NewGlobalRef(env->GetObjectClass(view));\n"
        source += '_probe = env->GetMethodID(_cls, "probe", "()V");\n'

        with self.assertRaisesRegex(self.contract.HostContractError, "no declared class"):
            self.contract._scan_source(source, "platform/android/probe.cpp")

    def test_static_and_field_lookups_render_distinct_specifications(self) -> None:
        static_method = self.contract.HostMember(
            class_name="games.cafecito.foundry.Probe",
            member_name="probe",
            descriptor="([Ljava/lang/String;)J",
            static=True,
            field=False,
            resolved_by_name=True,
        )
        field = self.contract.HostMember(
            class_name="games.cafecito.foundry.Probe",
            member_name="handle",
            descriptor="Lgames/cafecito/foundry/Probe$Inner;",
            static=False,
            field=True,
            resolved_by_name=True,
        )

        self.assertEqual(
            "static long probe(java.lang.String[]);",
            self.contract.member_signature(static_method),
        )
        self.assertEqual(
            "games.cafecito.foundry.Probe$Inner handle;",
            self.contract.member_signature(field),
        )

    def test_descriptor_decoding_rejects_malformed_input(self) -> None:
        for descriptor in ("(", "(I", "(Ljava/lang/String)V", "()", "()VV", "(Q)V"):
            with self.subTest(descriptor=descriptor):
                with self.assertRaises(self.contract.HostContractError):
                    self.contract.decode_method_descriptor(descriptor)

    def test_new_runtime_named_lookup_is_rejected(self) -> None:
        source = "method = env->GetMethodID(bclass, name.utf8().get_data(), signature.utf8().get_data());\n"

        with self.assertRaisesRegex(self.contract.HostContractError, "runtime-named JNI lookups"):
            self.contract.derive_host_members_from_sources({"platform/android/new_bridge.cpp": source})


if __name__ == "__main__":
    unittest.main()
