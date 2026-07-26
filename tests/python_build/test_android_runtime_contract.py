from __future__ import annotations

import importlib.util
import json
import os
import sys
import tempfile
import unittest
from pathlib import Path
from types import ModuleType
from typing import Any, cast

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
BUILDERS_PATH = REPO_ROOT / "platform/android/platform_android_builders.py"
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


def load_builders() -> ModuleType:
    sys.path.insert(0, str(BUILDERS_PATH.parent))
    spec = importlib.util.spec_from_file_location("platform_android_builders_for_test", BUILDERS_PATH)
    if spec is None or spec.loader is None:
        raise AssertionError(f"unable to load Android platform builders: {BUILDERS_PATH}")
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

    def validate_matrix(
        self,
        root: Path,
        *,
        revision: str = REVISION,
        tree: str = TREE,
        expected_foundry_jni: tuple[str, ...] = FOUNDRY_SYMBOLS,
    ) -> tuple[Any, ...]:
        return cast(
            tuple[Any, ...],
            self.contract.validate_native_matrix(
                root,
                revision,
                tree,
                expected_foundry_jni=expected_foundry_jni,
            ),
        )

    def test_exact_matrix_validates_provenance_elf_and_jni_surface(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            self.create_matrix(root)

            cells = self.validate_matrix(root)

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
                self.validate_matrix(root)

            self.create_matrix(root)
            provenance = root / "debug/arm64-v8a/provenance.json"
            provenance.write_bytes(b'{ "not": "canonical" }\n')
            with self.assertRaisesRegex(self.contract.ContractError, "not canonical JSON"):
                self.validate_matrix(root)

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
                self.validate_matrix(root)

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
                self.validate_matrix(root)

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
                self.validate_matrix(root)

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

            with self.assertRaisesRegex(self.contract.ContractError, "undeclared JNI symbol"):
                self.validate_matrix(root)

    def test_matrix_rejects_declared_jni_method_missing_consistently_from_all_cells(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            populate_native_matrix(
                root,
                revision=REVISION,
                tree=TREE,
                foundry_symbols=FOUNDRY_SYMBOLS[:-1],
            )

            with self.assertRaisesRegex(self.contract.ContractError, "missing declared JNI symbol"):
                self.validate_matrix(root)

    def test_matrix_rejects_undeclared_jni_method_added_consistently_to_all_cells(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            unexpected = "Java_games_cafecito_foundry_FoundryLib_undeclared"
            populate_native_matrix(
                root,
                revision=REVISION,
                tree=TREE,
                foundry_symbols=FOUNDRY_SYMBOLS + (unexpected,),
            )

            with self.assertRaisesRegex(self.contract.ContractError, "undeclared JNI symbol"):
                self.validate_matrix(root)

    def test_matrix_rejects_dirty_wrong_build_shape_extra_cells_and_symlinked_payloads(self) -> None:
        def rewrite_provenance(root: Path, build_type: str, abi: str, mutate) -> None:
            path = root / build_type / abi / "provenance.json"
            value = json.loads(path.read_bytes())
            mutate(value)
            path.write_bytes(canonical_json(value))

        mutations = (
            (
                "dirty",
                lambda root: rewrite_provenance(
                    root,
                    "debug",
                    "arm64-v8a",
                    lambda value: value["engine"].__setitem__("dirty", True),
                ),
                "dirty Foundry source",
            ),
            (
                "wrong-flags",
                lambda root: rewrite_provenance(
                    root,
                    "release",
                    "x86_64",
                    lambda value: value["build"].__setitem__("production", False),
                ),
                "build configuration mismatch",
            ),
            (
                "extra-file",
                lambda root: (root / "debug/arm64-v8a/extra.so").write_bytes(b"extra"),
                "unexpected native cell file",
            ),
            (
                "extra-cell",
                lambda root: (root / "profile/arm64-v8a").mkdir(parents=True),
                "unexpected cell",
            ),
        )
        for name, mutate, diagnostic in mutations:
            with self.subTest(name=name), tempfile.TemporaryDirectory() as temporary:
                root = Path(temporary)
                self.create_matrix(root)
                mutate(root)
                with self.assertRaisesRegex(self.contract.ContractError, diagnostic):
                    self.validate_matrix(root)

        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            self.create_matrix(root)
            payload = root / "debug/arm64-v8a/libfoundry_android.so"
            donor = root / "release/arm64-v8a/libfoundry_android.so"
            payload.unlink()
            try:
                os.symlink(donor, payload)
            except OSError as error:
                self.skipTest(f"symlinks unavailable: {error}")
            with self.assertRaisesRegex(self.contract.ContractError, "symbolic link"):
                self.validate_matrix(root)

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


class AndroidNativeProvenanceBuilderTests(unittest.TestCase):
    contract: ModuleType
    builders: ModuleType

    @classmethod
    def setUpClass(cls) -> None:
        cls.contract = load_contract()
        cls.builders = load_builders()

    @staticmethod
    def environment(build_type: str, abi: str) -> dict[str, object]:
        specification = AndroidNativeProvenanceBuilderTests.contract.build_spec(build_type, abi)
        return {
            "android_native_abi": abi,
            "android_native_build_type": build_type,
            "arch": specification.arch,
            "debug_symbols": specification.debug_symbols,
            "dev_build": specification.dev_build,
            "dev_mode": specification.dev_mode,
            "foundry_android_source_dirty": False,
            "foundry_android_source_revision": REVISION,
            "foundry_android_source_tree": TREE,
            "production": specification.production,
            "swappy": specification.swappy,
            "target": specification.target,
            "tests": specification.tests,
        }

    def test_scons_writer_emits_all_twelve_exact_cells_and_rejects_false_labels(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            for specification in self.contract.MATRIX:
                directory = root / specification.build_type / specification.abi
                directory.mkdir(parents=True)
                sources = []
                for library in LIBRARIES:
                    path = directory / library
                    path.write_bytes(f"{specification.build_type}:{specification.abi}:{library}".encode())
                    sources.append(path)
                target = directory / "provenance.json"
                environment = self.environment(specification.build_type, specification.abi)

                self.builders.write_android_native_provenance([target], sources, environment)

                value = json.loads(target.read_bytes())
                self.assertEqual(specification.build_json(), value["build"])
                self.assertEqual(REVISION, value["engine"]["revision"])
                self.assertEqual(TREE, value["engine"]["tree"])

            wrong = self.environment("release", "x86_64")
            wrong["production"] = False
            directory = root / "release/x86_64"
            with self.assertRaisesRegex(self.contract.ContractError, "build configuration mismatch"):
                self.builders.write_android_native_provenance(
                    [directory / "provenance.json"],
                    [directory / library for library in LIBRARIES],
                    wrong,
                )


if __name__ == "__main__":
    unittest.main()
