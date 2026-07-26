from __future__ import annotations

import importlib.util
import json
import sys
import tempfile
import unittest
from pathlib import Path
from types import ModuleType

from tests.python_build.android_native_test_support import (
    ABI_SPECS,
    EXTERNAL_JNI_SYMBOLS,
    FOUNDRY_SYMBOLS,
    LIBRARIES,
    canonical_json,
    minimal_elf,
    populate_native_matrix,
    sha256,
)

REPO_ROOT = Path(__file__).resolve().parents[2]
CONTRACT_PATH = REPO_ROOT / "platform/android/android_native_contract.py"
REVISION = "1" * 40
TREE = "2" * 40


def load_contract() -> ModuleType:
    if not CONTRACT_PATH.is_file():
        raise AssertionError(f"missing internal Android native contract: {CONTRACT_PATH}")
    spec = importlib.util.spec_from_file_location("android_native_contract", CONTRACT_PATH)
    if spec is None or spec.loader is None:
        raise AssertionError(f"unable to load Android native contract: {CONTRACT_PATH}")
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


def rewrite_library(
    root: Path,
    *,
    build_type: str,
    abi: str,
    library: str,
    contents: bytes,
) -> None:
    directory = root / build_type / abi
    path = directory / library
    path.write_bytes(contents)
    provenance_path = directory / "provenance.json"
    provenance = json.loads(provenance_path.read_bytes())
    record = next(entry for entry in provenance["libraries"] if entry["path"] == library)
    record["sha256"] = sha256(contents)
    record["size"] = len(contents)
    provenance_path.write_bytes(canonical_json(provenance))


class AndroidNativeContractTests(unittest.TestCase):
    contract: ModuleType

    @classmethod
    def setUpClass(cls) -> None:
        cls.contract = load_contract()

    def create_matrix(self, root: Path) -> None:
        populate_native_matrix(root, revision=REVISION, tree=TREE)

    def test_exact_matrix_validates_provenance_elf_and_jni_surface(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            self.create_matrix(root)

            cells = self.contract.validate_native_matrix(root, REVISION, TREE)

        self.assertEqual(12, len(cells))
        self.assertEqual(
            {(build_type, abi) for build_type in ("debug", "dev", "release") for abi in ABI_SPECS},
            {(cell.build_type, cell.abi) for cell in cells},
        )

    def test_matrix_rejects_missing_cell_and_noncanonical_provenance(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            self.create_matrix(root)
            missing = root / "release/x86_64"
            for path in missing.iterdir():
                path.unlink()
            missing.rmdir()
            with self.assertRaisesRegex(self.contract.ContractError, "matrix mismatch"):
                self.contract.validate_native_matrix(root, REVISION, TREE)

            self.create_matrix(root)
            provenance = root / "debug/arm64-v8a/provenance.json"
            provenance.write_bytes(b'{ "not": "canonical" }\n')
            with self.assertRaisesRegex(self.contract.ContractError, "not canonical JSON"):
                self.contract.validate_native_matrix(root, REVISION, TREE)

    def test_matrix_rejects_wrong_elf_abi_after_hash_is_updated(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            self.create_matrix(root)
            contents = minimal_elf(
                ABI_SPECS["x86_64"]["elf_class"],
                ABI_SPECS["x86_64"]["elf_machine"],
                FOUNDRY_SYMBOLS + EXTERNAL_JNI_SYMBOLS,
            )
            rewrite_library(
                root,
                build_type="debug",
                abi="arm64-v8a",
                library="libfoundry_android.so",
                contents=contents,
            )

            with self.assertRaisesRegex(self.contract.ContractError, "ELF machine mismatch"):
                self.contract.validate_native_matrix(root, REVISION, TREE)

    def test_matrix_rejects_stale_or_unowned_jni_exports(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            self.create_matrix(root)
            abi = "arm64-v8a"
            contents = minimal_elf(
                ABI_SPECS[abi]["elf_class"],
                ABI_SPECS[abi]["elf_machine"],
                ("Java_org_godotengine_GodotLib_step",) + EXTERNAL_JNI_SYMBOLS,
            )
            rewrite_library(
                root,
                build_type="debug",
                abi=abi,
                library="libfoundry_android.so",
                contents=contents,
            )

            with self.assertRaisesRegex(self.contract.ContractError, "stale JNI symbol"):
                self.contract.validate_native_matrix(root, REVISION, TREE)

    def test_matrix_requires_allowlisted_external_jni_exports(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            self.create_matrix(root)
            abi = "x86"
            contents = minimal_elf(
                ABI_SPECS[abi]["elf_class"],
                ABI_SPECS[abi]["elf_machine"],
                FOUNDRY_SYMBOLS + EXTERNAL_JNI_SYMBOLS[:-1],
            )
            rewrite_library(
                root,
                build_type="dev",
                abi=abi,
                library="libfoundry_android.so",
                contents=contents,
            )

            with self.assertRaisesRegex(self.contract.ContractError, "missing required external JNI symbol"):
                self.contract.validate_native_matrix(root, REVISION, TREE)

    def test_matrix_requires_identical_foundry_jni_exports_in_every_cell(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            self.create_matrix(root)
            abi = "armeabi-v7a"
            contents = minimal_elf(
                ABI_SPECS[abi]["elf_class"],
                ABI_SPECS[abi]["elf_machine"],
                FOUNDRY_SYMBOLS + ("Java_games_cafecito_foundry_FoundryLib_unexpected",) + EXTERNAL_JNI_SYMBOLS,
            )
            rewrite_library(
                root,
                build_type="release",
                abi=abi,
                library="libfoundry_android.so",
                contents=contents,
            )

            with self.assertRaisesRegex(self.contract.ContractError, "JNI surface mismatch"):
                self.contract.validate_native_matrix(root, REVISION, TREE)

    def test_provenance_writer_records_exact_library_hashes(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            for library in LIBRARIES:
                (directory / library).write_bytes(library.encode())
            output = directory / "provenance.json"

            self.contract.write_native_provenance(
                output,
                revision=REVISION,
                tree=TREE,
                dirty=False,
                build_type="debug",
                abi="arm64-v8a",
                library_directory=directory,
            )

            value = json.loads(output.read_bytes())
            self.assertEqual(REVISION, value["engine"]["revision"])
            self.assertEqual(sorted(LIBRARIES), [record["path"] for record in value["libraries"]])
            self.assertEqual(output.read_bytes(), canonical_json(value))


if __name__ == "__main__":
    unittest.main()
