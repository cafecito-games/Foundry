from __future__ import annotations

import hashlib
import json
import stat
import tempfile
import unittest
import warnings
import zipfile
from pathlib import Path
from typing import Any, Callable

from tests.python_build.android_native_bundle_test_support import (
    ABI_SPECS,
    EXTERNAL_JNI_SYMBOLS,
    FOUNDRY_SYMBOLS,
    canonical_json,
    create_bundle,
    minimal_elf,
    populate_native_inputs,
    rewrite_zip,
    run_tool,
    sha256,
    write_compatibility,
)


class NativeBundleTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary_directory = tempfile.TemporaryDirectory()
        self.workspace = Path(self.temporary_directory.name)

    def tearDown(self) -> None:
        self.temporary_directory.cleanup()

    def validate(
        self,
        bundle: Path,
        compatibility: Path,
        *,
        expected_returncode: int = 0,
        expected_error: str | None = None,
    ) -> None:
        result = run_tool(
            "native_bundle.py",
            "validate",
            "--compatibility",
            str(compatibility),
            "--bundle",
            str(bundle),
        )
        self.assertEqual(
            expected_returncode,
            result.returncode,
            f"stdout:\n{result.stdout}\nstderr:\n{result.stderr}",
        )
        if expected_error is not None:
            self.assertIn(expected_error, result.stderr)

    def mutate_manifest(
        self,
        bundle: Path,
        name: str,
        mutate: Callable[[dict[str, Any]], None],
    ) -> Path:
        with zipfile.ZipFile(bundle) as archive:
            manifest = json.loads(archive.read("manifest.json"))
        mutate(manifest)
        output = self.workspace / name
        rewrite_zip(bundle, output, replace={"manifest.json": canonical_json(manifest)})
        return output

    def test_create_is_deterministic_and_validate_extracts_exact_matrix(self) -> None:
        compatibility = self.workspace / "compatibility.json"
        native_root = self.workspace / "native"
        first = self.workspace / "first.zip"
        second = self.workspace / "second.zip"
        extraction = self.workspace / "extracted"
        write_compatibility(compatibility)
        populate_native_inputs(native_root)

        for output in (first, second):
            result = run_tool(
                "native_bundle.py",
                "create",
                "--compatibility",
                str(compatibility),
                "--input-root",
                str(native_root),
                "--output",
                str(output),
            )
            self.assertEqual(0, result.returncode, result.stderr)
        self.assertEqual(first.read_bytes(), second.read_bytes())

        result = run_tool(
            "native_bundle.py",
            "extract",
            "--compatibility",
            str(compatibility),
            "--bundle",
            str(first),
            "--build-type",
            "debug",
            "--output",
            str(extraction),
        )
        self.assertEqual(0, result.returncode, result.stderr)
        extracted = sorted(path.relative_to(extraction).as_posix() for path in extraction.rglob("*.so"))
        self.assertEqual(
            sorted(
                f"{abi}/{library}" for abi in ABI_SPECS for library in ("libfoundry_android.so", "libc++_shared.so")
            ),
            extracted,
        )

    def test_missing_bundle_fails_clearly(self) -> None:
        compatibility = self.workspace / "compatibility.json"
        write_compatibility(compatibility)
        self.validate(
            self.workspace / "missing.zip",
            compatibility,
            expected_returncode=2,
            expected_error="native bundle does not exist",
        )

    def test_wrong_revision_version_and_contract_each_fail_clearly(self) -> None:
        compatibility, _, bundle = create_bundle(self.workspace)
        mutations = {
            "revision": lambda manifest: manifest["compatibility"]["engine"].__setitem__("revision", "b" * 40),
            "version": lambda manifest: manifest["compatibility"]["engine"].__setitem__("version", "9.9.9"),
            "JNI contract": lambda manifest: manifest["compatibility"].__setitem__("jni_contract_version", 99),
        }
        for index, (expected_error, mutation) in enumerate(mutations.items()):
            with self.subTest(expected_error=expected_error):
                changed = self.mutate_manifest(bundle, f"wrong-{index}.zip", mutation)
                self.validate(
                    changed,
                    compatibility,
                    expected_returncode=2,
                    expected_error=expected_error,
                )

    def test_incomplete_matrix_fails(self) -> None:
        compatibility, _, bundle = create_bundle(self.workspace)
        missing_path = "release/x86_64/libfoundry_android.so"
        incomplete = self.workspace / "incomplete.zip"
        rewrite_zip(bundle, incomplete, remove={missing_path})
        self.validate(
            incomplete,
            compatibility,
            expected_returncode=2,
            expected_error=f"missing archive member: {missing_path}",
        )

    def test_corrupted_payload_hash_fails(self) -> None:
        compatibility, _, bundle = create_bundle(self.workspace)
        path = "debug/arm64-v8a/libfoundry_android.so"
        corrupted = self.workspace / "corrupted.zip"
        with zipfile.ZipFile(bundle) as archive:
            changed = bytearray(archive.read(path))
            changed[-1] ^= 0x01
            contents = bytes(changed)
        rewrite_zip(bundle, corrupted, replace={path: contents})
        self.validate(
            corrupted,
            compatibility,
            expected_returncode=2,
            expected_error=f"SHA-256 mismatch for {path}",
        )

    def test_wrong_elf_abi_fails_even_with_updated_hash(self) -> None:
        compatibility, _, bundle = create_bundle(self.workspace)
        path = "debug/arm64-v8a/libfoundry_android.so"
        wrong_elf = minimal_elf(64, ABI_SPECS["x86_64"]["elf_machine"], FOUNDRY_SYMBOLS)

        with zipfile.ZipFile(bundle) as archive:
            manifest = json.loads(archive.read("manifest.json"))
        file_record = next(record for record in manifest["files"] if record["path"] == path)
        file_record["sha256"] = sha256(wrong_elf)
        file_record["size"] = len(wrong_elf)
        wrong = self.workspace / "wrong-abi.zip"
        rewrite_zip(
            bundle,
            wrong,
            replace={path: wrong_elf, "manifest.json": canonical_json(manifest)},
        )
        self.validate(
            wrong,
            compatibility,
            expected_returncode=2,
            expected_error=f"ELF machine mismatch for {path}",
        )

    def test_stale_jni_symbol_fails_even_when_manifest_matches_payload(self) -> None:
        compatibility, _, bundle = create_bundle(self.workspace)
        path = "dev/x86/libfoundry_android.so"
        stale_symbols = ("Java_org_godotengine_GodotLib_step",)
        stale_elf = minimal_elf(32, ABI_SPECS["x86"]["elf_machine"], stale_symbols)

        with zipfile.ZipFile(bundle) as archive:
            manifest = json.loads(archive.read("manifest.json"))
        file_record = next(record for record in manifest["files"] if record["path"] == path)
        file_record["sha256"] = sha256(stale_elf)
        file_record["size"] = len(stale_elf)
        file_record["jni_symbols"] = list(stale_symbols)
        file_record["jni_symbols_sha256"] = hashlib.sha256(("\n".join(stale_symbols) + "\n").encode()).hexdigest()
        stale = self.workspace / "stale-symbol.zip"
        rewrite_zip(
            bundle,
            stale,
            replace={path: stale_elf, "manifest.json": canonical_json(manifest)},
        )
        self.validate(
            stale,
            compatibility,
            expected_returncode=2,
            expected_error="stale JNI symbol",
        )

    def test_missing_and_unallowlisted_external_jni_symbols_fail(self) -> None:
        compatibility = self.workspace / "compatibility.json"
        write_compatibility(compatibility)

        missing_root = self.workspace / "missing-external"
        populate_native_inputs(
            missing_root,
            external_jni_symbols=EXTERNAL_JNI_SYMBOLS[:-1],
        )
        result = run_tool(
            "native_bundle.py",
            "create",
            "--compatibility",
            str(compatibility),
            "--input-root",
            str(missing_root),
            "--output",
            str(self.workspace / "missing-external.zip"),
        )
        self.assertEqual(2, result.returncode)
        self.assertIn("missing required external JNI symbol", result.stderr)
        self.assertIn(EXTERNAL_JNI_SYMBOLS[-1], result.stderr)

        extra_root = self.workspace / "extra-external"
        unknown = "Java_com_example_Unowned_nativeMethod"
        populate_native_inputs(
            extra_root,
            external_jni_symbols=EXTERNAL_JNI_SYMBOLS + (unknown,),
        )
        result = run_tool(
            "native_bundle.py",
            "create",
            "--compatibility",
            str(compatibility),
            "--input-root",
            str(extra_root),
            "--output",
            str(self.workspace / "extra-external.zip"),
        )
        self.assertEqual(2, result.returncode)
        self.assertIn("unallowlisted external JNI symbol", result.stderr)
        self.assertIn(unknown, result.stderr)

    def test_unsafe_traversal_and_symlink_members_fail(self) -> None:
        compatibility, _, bundle = create_bundle(self.workspace)
        traversal = self.workspace / "traversal.zip"
        rewrite_zip(bundle, traversal, additions=[("../escape.so", b"unsafe")])
        self.validate(
            traversal,
            compatibility,
            expected_returncode=2,
            expected_error="unsafe archive path",
        )

        symlink_info = zipfile.ZipInfo("debug/arm64-v8a/symlink.so")
        symlink_info.create_system = 3
        symlink_info.external_attr = (stat.S_IFLNK | 0o777) << 16
        symlink = self.workspace / "symlink.zip"
        rewrite_zip(bundle, symlink, additions=[(symlink_info, b"target")])
        self.validate(
            symlink,
            compatibility,
            expected_returncode=2,
            expected_error="symbolic-link archive member",
        )

    def test_duplicate_archive_member_fails(self) -> None:
        compatibility, _, bundle = create_bundle(self.workspace)
        duplicate = self.workspace / "duplicate.zip"
        with warnings.catch_warnings():
            warnings.simplefilter("ignore", UserWarning)
            rewrite_zip(bundle, duplicate, additions=[("manifest.json", b"{}")])
        self.validate(
            duplicate,
            compatibility,
            expected_returncode=2,
            expected_error="duplicate archive member: manifest.json",
        )

    def test_creator_rejects_incomplete_input_and_wrong_abi(self) -> None:
        compatibility = self.workspace / "compatibility.json"
        write_compatibility(compatibility)

        incomplete_root = self.workspace / "incomplete-input"
        populate_native_inputs(incomplete_root)
        (incomplete_root / "release/x86/libc++_shared.so").unlink()
        result = run_tool(
            "native_bundle.py",
            "create",
            "--compatibility",
            str(compatibility),
            "--input-root",
            str(incomplete_root),
            "--output",
            str(self.workspace / "incomplete-output.zip"),
        )
        self.assertEqual(2, result.returncode)
        self.assertIn("native input matrix mismatch", result.stderr)

        wrong_root = self.workspace / "wrong-input"
        populate_native_inputs(
            wrong_root,
            wrong_abi_path="release/armeabi-v7a/libfoundry_android.so",
        )
        result = run_tool(
            "native_bundle.py",
            "create",
            "--compatibility",
            str(compatibility),
            "--input-root",
            str(wrong_root),
            "--output",
            str(self.workspace / "wrong-output.zip"),
        )
        self.assertEqual(2, result.returncode)
        self.assertIn("ELF class mismatch", result.stderr)


if __name__ == "__main__":
    unittest.main()
