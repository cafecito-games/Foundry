from __future__ import annotations

import importlib.util
import json
import subprocess
import sys
import tempfile
import textwrap
import unittest
from pathlib import Path
from types import ModuleType

REPO_ROOT = Path(__file__).resolve().parents[2]
ANDROID_ROOT = REPO_ROOT / "platform/android"
CONTRACT_PATH = ANDROID_ROOT / "android_runtime_contract.py"
BUILD_PATH = ANDROID_ROOT / "android_runtime_build.py"
BASELINE_REVISION = "3f1054e65f942375cb7f42f299505220fca990fd"
REQUIRED_PATHS = (
    "compatibility/foundry-engine.json",
    "gradlew",
    "runtime/build.gradle",
    "tools/native_bundle.py",
    "tools/sync_engine_pin.py",
    "tools/verify_jni_contract.py",
)


def load_module(name: str, path: Path) -> ModuleType:
    if not path.is_file():
        raise AssertionError(f"missing module: {path}")
    spec = importlib.util.spec_from_file_location(name, path)
    if spec is None or spec.loader is None:
        raise AssertionError(f"unable to load module: {path}")
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


def run(*arguments: str, cwd: Path) -> str:
    return subprocess.run(
        list(arguments),
        cwd=cwd,
        check=True,
        capture_output=True,
        text=True,
    ).stdout.strip()


def write_executable(path: Path, contents: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(textwrap.dedent(contents).lstrip(), encoding="utf-8")
    path.chmod(0o755)


def commit_all(repository: Path, message: str) -> str:
    run("git", "add", ".", cwd=repository)
    run("git", "commit", "-m", message, cwd=repository)
    return run("git", "rev-parse", "HEAD", cwd=repository)


def initialize_repository(path: Path) -> None:
    path.mkdir()
    run("git", "init", "-b", "develop", cwd=path)
    run("git", "config", "user.email", "tests@cafecito.games", cwd=path)
    run("git", "config", "user.name", "Android Runtime Tests", cwd=path)


def baseline_compatibility() -> dict[str, object]:
    return {
        "bindings": {"version": "0.1.0-dev-SNAPSHOT"},
        "engine": {
            "repository": "https://github.com/cafecito-games/Foundry",
            "revision": BASELINE_REVISION,
            "version": "0.1.0-dev",
            "version_components": {
                "major": 0,
                "minor": 1,
                "module_config": "",
                "patch": 0,
                "status": "dev",
            },
        },
        "jni_contract_version": 1,
        "native": {
            "abis": {
                "arm64-v8a": {"elf_class": 64, "elf_machine": 183},
                "armeabi-v7a": {"elf_class": 32, "elf_machine": 40},
                "x86": {"elf_class": 32, "elf_machine": 3},
                "x86_64": {"elf_class": 64, "elf_machine": 62},
            },
            "build_types": ["debug", "dev", "release"],
            "external_jni_allowlist": [],
            "libraries": ["libfoundry_android.so", "libc++_shared.so"],
        },
        "schema_version": 1,
    }


def create_fake_standalone(path: Path) -> tuple[str, str]:
    initialize_repository(path)
    (path / "compatibility").mkdir()
    (path / "compatibility/foundry-engine.json").write_bytes(
        (json.dumps(baseline_compatibility(), sort_keys=True, separators=(",", ":")) + "\n").encode()
    )
    (path / "runtime").mkdir()
    (path / "runtime/build.gradle").write_text("// fake runtime\n", encoding="utf-8")
    (path / "tools").mkdir()
    write_executable(
        path / "tools/sync_engine_pin.py",
        """
        #!/usr/bin/env python3
        import argparse
        import json
        import subprocess
        from pathlib import Path

        parser = argparse.ArgumentParser()
        parser.add_argument("command")
        parser.add_argument("--source", required=True, type=Path)
        parser.add_argument("--repository", required=True)
        parser.add_argument("--expected-revision", required=True)
        parser.add_argument("--bindings-version", required=True)
        parser.add_argument("--jni-contract-version", required=True, type=int)
        parser.add_argument("--output", required=True, type=Path)
        arguments = parser.parse_args()
        revision = subprocess.run(
            ["git", "rev-parse", "HEAD"],
            cwd=arguments.source,
            check=True,
            capture_output=True,
            text=True,
        ).stdout.strip()
        if revision != arguments.expected_revision:
            raise SystemExit("source revision does not match expected revision")
        value = {
            "bindings": {"version": arguments.bindings_version},
            "engine": {
                "repository": arguments.repository,
                "revision": revision,
                "version": "0.1.0-dev",
                "version_components": {
                    "major": 0,
                    "minor": 1,
                    "module_config": "",
                    "patch": 0,
                    "status": "dev",
                },
            },
            "jni_contract_version": arguments.jni_contract_version,
            "native": {
                "abis": {
                    "arm64-v8a": {"elf_class": 64, "elf_machine": 183},
                    "armeabi-v7a": {"elf_class": 32, "elf_machine": 40},
                    "x86": {"elf_class": 32, "elf_machine": 3},
                    "x86_64": {"elf_class": 64, "elf_machine": 62},
                },
                "build_types": ["debug", "dev", "release"],
                "external_jni_allowlist": [],
                "libraries": ["libfoundry_android.so", "libc++_shared.so"],
            },
            "schema_version": 1,
        }
        arguments.output.write_bytes(
            (json.dumps(value, sort_keys=True, separators=(",", ":")) + "\\n").encode()
        )
        """,
    )
    write_executable(
        path / "tools/native_bundle.py",
        """
        #!/usr/bin/env python3
        import argparse
        import json
        from pathlib import Path

        parser = argparse.ArgumentParser()
        subparsers = parser.add_subparsers(dest="command", required=True)
        create = subparsers.add_parser("create")
        create.add_argument("--compatibility", required=True, type=Path)
        create.add_argument("--input-root", required=True, type=Path)
        create.add_argument("--output", required=True, type=Path)
        validate = subparsers.add_parser("validate")
        validate.add_argument("--compatibility", required=True, type=Path)
        validate.add_argument("--bundle", required=True, type=Path)
        arguments = parser.parse_args()
        compatibility = json.loads(arguments.compatibility.read_bytes())
        if arguments.command == "create":
            arguments.output.write_bytes(
                (
                    json.dumps(
                        {"compatibility": compatibility},
                        sort_keys=True,
                        separators=(",", ":"),
                    )
                    + "\\n"
                ).encode()
            )
        else:
            bundle = json.loads(arguments.bundle.read_bytes())
            if bundle.get("compatibility") != compatibility:
                raise SystemExit("native bundle compatibility metadata mismatch")
        """,
    )
    write_executable(
        path / "tools/verify_jni_contract.py",
        """
        #!/usr/bin/env python3
        raise SystemExit(0)
        """,
    )
    write_executable(
        path / "gradlew",
        """
        #!/usr/bin/env python3
        from pathlib import Path

        root = Path(__file__).resolve().parent
        (root / "gradle-invoked").write_text("yes\\n", encoding="utf-8")
        output = root / "runtime/build/outputs/aar"
        output.mkdir(parents=True, exist_ok=True)
        for build_type in ("debug", "dev", "release"):
            (output / f"foundry-{build_type}.aar").write_bytes(
                f"fake-{build_type}\\n".encode()
            )
        """,
    )
    first = commit_all(path, "Pinned source")
    tree = run("git", "rev-parse", "HEAD^{tree}", cwd=path)
    (path / "newer.txt").write_text("not pinned\n", encoding="utf-8")
    commit_all(path, "Newer checkout")
    return first, tree


def create_foundry(path: Path) -> tuple[str, str]:
    initialize_repository(path)
    (path / "version.py").write_text(
        'short_name = "foundry"\n'
        'name = "Foundry"\n'
        "major = 0\n"
        "minor = 1\n"
        "patch = 0\n"
        'status = "dev"\n'
        'module_config = ""\n',
        encoding="utf-8",
    )
    revision = commit_all(path, "Foundry source")
    tree = run("git", "rev-parse", "HEAD^{tree}", cwd=path)
    return revision, tree


def write_pin(path: Path, revision: str, tree: str) -> Path:
    path.mkdir(parents=True, exist_ok=True)
    value = {
        "bindings": {"version": "0.1.0-dev-SNAPSHOT"},
        "engine_compatibility": {"policy": "exact-native-source-revision"},
        "jni_contract_version": 1,
        "outputs": {
            "debug": "runtime/build/outputs/aar/foundry-debug.aar",
            "dev": "runtime/build/outputs/aar/foundry-dev.aar",
            "release": "runtime/build/outputs/aar/foundry-release.aar",
        },
        "required_paths": list(REQUIRED_PATHS),
        "schema_version": 1,
        "source": {
            "repository": "https://github.com/cafecito-games/Foundry-Android.git",
            "revision": revision,
            "tree": tree,
        },
    }
    pin = path / "pin.json"
    pin.write_bytes((json.dumps(value, sort_keys=True, separators=(",", ":")) + "\n").encode())
    return pin


def create_native_matrix(
    contract: ModuleType,
    root: Path,
    revision: str,
    tree: str,
) -> None:
    for specification in contract.MATRIX:
        directory = root / specification.build_type / specification.abi
        directory.mkdir(parents=True)
        for name in contract.LIBRARY_NAMES:
            (directory / name).write_bytes(f"{specification.build_type}:{specification.abi}:{name}\n".encode())
        contract.write_native_provenance(
            directory / "provenance.json",
            revision=revision,
            tree=tree,
            dirty=False,
            build_type=specification.build_type,
            abi=specification.abi,
            library_directory=directory,
        )


class AndroidRuntimeBuildTests(unittest.TestCase):
    def setUp(self) -> None:
        self.contract = load_module("android_runtime_contract", CONTRACT_PATH)
        self.build = load_module("android_runtime_build", BUILD_PATH)
        self.temporary = tempfile.TemporaryDirectory()
        self.workspace = Path(self.temporary.name)
        self.standalone = self.workspace / "standalone"
        self.pinned_revision, self.pinned_tree = create_fake_standalone(self.standalone)
        self.pin_path = write_pin(self.workspace, self.pinned_revision, self.pinned_tree)
        self.pin = self.contract.load_pin(self.pin_path)
        self.foundry = self.workspace / "foundry"
        self.foundry_revision, self.foundry_tree = create_foundry(self.foundry)

    def tearDown(self) -> None:
        self.temporary.cleanup()

    def test_exports_exact_pinned_object_instead_of_checked_out_branch(self) -> None:
        exported = self.build.export_pinned_source(
            pin=self.pin,
            source_repository=self.standalone,
            scratch=self.workspace / "scratch",
            allow_fetch=False,
        )

        self.assertFalse((exported / ".git").exists())
        self.assertFalse((exported / "newer.txt").exists())
        self.assertEqual(
            self.pin.bindings_version,
            json.loads((exported / "compatibility/foundry-engine.json").read_bytes())["bindings"]["version"],
        )
        self.assertEqual(
            self.pinned_tree,
            json.loads((exported / ".foundry-source.json").read_bytes())["source_tree"],
        )

    def test_export_rejects_wrong_tree_contract_drift_and_missing_offline_source(self) -> None:
        wrong_tree_path = write_pin(self.workspace / "wrong-tree", self.pinned_revision, "f" * 40)
        wrong_tree = self.contract.load_pin(wrong_tree_path)
        with self.assertRaisesRegex(self.build.BuildError, "source tree mismatch"):
            self.build.export_pinned_source(
                pin=wrong_tree,
                source_repository=self.standalone,
                scratch=self.workspace / "wrong-tree-scratch",
                allow_fetch=False,
            )

        compatibility = self.standalone / "compatibility/foundry-engine.json"
        original = compatibility.read_bytes()
        value = json.loads(original)
        value["jni_contract_version"] = 99
        compatibility.write_bytes((json.dumps(value, sort_keys=True, separators=(",", ":")) + "\n").encode())
        drift_revision = commit_all(self.standalone, "Drifted contract")
        drift_tree = run("git", "rev-parse", "HEAD^{tree}", cwd=self.standalone)
        drift_pin = self.contract.load_pin(write_pin(self.workspace / "drift", drift_revision, drift_tree))
        with self.assertRaisesRegex(self.build.BuildError, "JNI contract mismatch"):
            self.build.export_pinned_source(
                pin=drift_pin,
                source_repository=self.standalone,
                scratch=self.workspace / "drift-scratch",
                allow_fetch=False,
            )

        compatibility.write_bytes(original)
        with self.assertRaisesRegex(self.build.BuildError, "prefetched standalone source"):
            self.build.export_pinned_source(
                pin=self.pin,
                source_repository=None,
                scratch=self.workspace / "offline-scratch",
                allow_fetch=False,
            )

    def test_derives_compatibility_from_exact_clean_current_foundry(self) -> None:
        exported = self.build.export_pinned_source(
            pin=self.pin,
            source_repository=self.standalone,
            scratch=self.workspace / "derive",
            allow_fetch=False,
        )

        compatibility = self.build.derive_compatibility(
            exported=exported,
            foundry=self.foundry,
            pin=self.pin,
            expected_revision=self.foundry_revision,
            expected_tree=self.foundry_tree,
        )

        self.assertEqual(self.foundry_revision, compatibility["engine"]["revision"])
        self.assertNotEqual(BASELINE_REVISION, compatibility["engine"]["revision"])
        build_input = json.loads((exported / "build-input.json").read_bytes())
        self.assertEqual(self.foundry_tree, build_input["engine_tree"])
        self.assertEqual(self.pinned_revision, build_input["standalone_revision"])

        (self.foundry / "version.py").write_text("# dirty\n", encoding="utf-8")
        with self.assertRaisesRegex(self.build.BuildError, "clean Foundry checkout"):
            self.build.derive_compatibility(
                exported=exported,
                foundry=self.foundry,
                pin=self.pin,
                expected_revision=self.foundry_revision,
                expected_tree=self.foundry_tree,
            )

    def test_prebuilt_bundle_mismatch_fails_before_gradle(self) -> None:
        bundle = self.workspace / "baseline-bundle.json"
        bundle.write_bytes(
            (
                json.dumps(
                    {"compatibility": baseline_compatibility()},
                    sort_keys=True,
                    separators=(",", ":"),
                )
                + "\n"
            ).encode()
        )
        output = self.workspace / "aars"

        with self.assertRaisesRegex(self.build.BuildError, "native bundle compatibility metadata mismatch"):
            self.build.prepare_runtime(
                pin_path=self.pin_path,
                engine_source=self.foundry,
                scratch=self.workspace / "prepare-mismatch",
                output_aars=output,
                standalone_source=self.standalone,
                allow_fetch=False,
                native_root=None,
                native_bundle=bundle,
            )

        self.assertFalse(output.exists())
        self.assertFalse((self.workspace / "prepare-mismatch/source/gradle-invoked").exists())

    def test_explicit_prefetched_source_and_native_root_build_all_aars(self) -> None:
        native_root = self.workspace / "native"
        create_native_matrix(
            self.contract,
            native_root,
            self.foundry_revision,
            self.foundry_tree,
        )
        output = self.workspace / "aars"

        result = self.build.prepare_runtime(
            pin_path=self.pin_path,
            engine_source=self.foundry,
            scratch=self.workspace / "prepare-success",
            output_aars=output,
            standalone_source=self.standalone,
            allow_fetch=False,
            native_root=native_root,
            native_bundle=None,
        )

        self.assertEqual(
            {
                "debug": output.resolve() / "debug/foundry-debug.aar",
                "dev": output.resolve() / "dev/foundry-dev.aar",
                "release": output.resolve() / "release/foundry-release.aar",
            },
            result.aars,
        )
        for path in result.aars.values():
            self.assertTrue(path.is_file(), path)
        self.assertTrue(result.bundle.is_file())
        self.assertTrue((self.workspace / "prepare-success/source/gradle-invoked").is_file())

    def test_rejects_repository_root_or_tracked_output_locations(self) -> None:
        with self.assertRaisesRegex(self.build.BuildError, "must not be the Foundry repository root"):
            self.build.prepare_runtime(
                pin_path=self.pin_path,
                engine_source=self.foundry,
                scratch=self.foundry,
                output_aars=self.workspace / "aars",
                standalone_source=self.standalone,
                allow_fetch=False,
                native_root=None,
                native_bundle=self.workspace / "unused.zip",
            )

    def test_prepare_help_exposes_explicit_source_and_native_input_choices(self) -> None:
        result = subprocess.run(
            [sys.executable, BUILD_PATH, "prepare", "--help"],
            cwd=REPO_ROOT,
            check=True,
            capture_output=True,
            text=True,
        )

        self.assertIn("--source-repository SOURCE_REPOSITORY", result.stdout)
        self.assertIn("--allow-fetch", result.stdout)
        self.assertIn("--native-root NATIVE_ROOT", result.stdout)
        self.assertIn("--native-bundle NATIVE_BUNDLE", result.stdout)
        self.assertNotIn("--standalone-source", result.stdout)

        tracked_output = self.foundry / "tracked-output"
        tracked_output.mkdir()
        (tracked_output / "keep.txt").write_text("tracked\n", encoding="utf-8")
        run("git", "add", "tracked-output/keep.txt", cwd=self.foundry)
        run("git", "commit", "-m", "Track output", cwd=self.foundry)
        with self.assertRaisesRegex(self.build.BuildError, "must be outside Foundry or Git-ignored"):
            self.build.prepare_runtime(
                pin_path=self.pin_path,
                engine_source=self.foundry,
                scratch=self.workspace / "scratch-output",
                output_aars=tracked_output,
                standalone_source=self.standalone,
                allow_fetch=False,
                native_root=None,
                native_bundle=self.workspace / "unused.zip",
            )


if __name__ == "__main__":
    unittest.main()
