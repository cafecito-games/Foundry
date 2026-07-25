from __future__ import annotations

import hashlib
import importlib.util
import json
import os
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from types import ModuleType

REPO_ROOT = Path(__file__).resolve().parents[2]
MODULE_PATH = REPO_ROOT / "platform/android/android_runtime_contract.py"
PIN_PATH = REPO_ROOT / "platform/android/foundry_android_runtime.json"
REVISION = "1" * 40
TREE = "2" * 40
LIBRARIES = ("libfoundry_android.so", "libc++_shared.so")
BUILD_SPECS = {
    "debug": {
        "target": "template_debug",
        "production": False,
        "dev_mode": False,
        "dev_build": False,
        "debug_symbols": False,
        "tests": False,
    },
    "dev": {
        "target": "template_debug",
        "production": False,
        "dev_mode": True,
        "dev_build": True,
        "debug_symbols": True,
        "tests": False,
    },
    "release": {
        "target": "template_release",
        "production": True,
        "dev_mode": False,
        "dev_build": False,
        "debug_symbols": False,
        "tests": False,
    },
}
ABI_SPECS = {
    "arm64-v8a": "arm64",
    "armeabi-v7a": "arm32",
    "x86": "x86_32",
    "x86_64": "x86_64",
}


def load_contract() -> ModuleType:
    if not MODULE_PATH.is_file():
        raise AssertionError(f"missing Android runtime contract module: {MODULE_PATH}")
    spec = importlib.util.spec_from_file_location("android_runtime_contract", MODULE_PATH)
    if spec is None or spec.loader is None:
        raise AssertionError(f"unable to load Android runtime contract module: {MODULE_PATH}")
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


def canonical_json(value: object) -> bytes:
    return (json.dumps(value, sort_keys=True, separators=(",", ":")) + "\n").encode()


def library_record(path: Path) -> dict[str, object]:
    contents = path.read_bytes()
    return {
        "path": path.name,
        "sha256": hashlib.sha256(contents).hexdigest(),
        "size": len(contents),
    }


def provenance(build_type: str, abi: str, directory: Path) -> dict[str, object]:
    return {
        "build": {
            "abi": abi,
            "arch": ABI_SPECS[abi],
            "build_type": build_type,
            "debug_symbols": BUILD_SPECS[build_type]["debug_symbols"],
            "dev_build": BUILD_SPECS[build_type]["dev_build"],
            "dev_mode": BUILD_SPECS[build_type]["dev_mode"],
            "production": BUILD_SPECS[build_type]["production"],
            "swappy": True,
            "target": BUILD_SPECS[build_type]["target"],
            "tests": BUILD_SPECS[build_type]["tests"],
        },
        "engine": {
            "dirty": False,
            "revision": REVISION,
            "tree": TREE,
        },
        "libraries": [library_record(directory / name) for name in sorted(LIBRARIES)],
        "schema_version": 1,
    }


def create_matrix(root: Path) -> None:
    for build_type in sorted(BUILD_SPECS):
        for abi in sorted(ABI_SPECS):
            directory = root / build_type / abi
            directory.mkdir(parents=True)
            for library in LIBRARIES:
                (directory / library).write_bytes(f"{build_type}:{abi}:{library}\n".encode())
            (directory / "provenance.json").write_bytes(canonical_json(provenance(build_type, abi, directory)))


def read_provenance(root: Path, build_type: str, abi: str) -> dict[str, object]:
    return json.loads((root / build_type / abi / "provenance.json").read_bytes())


def write_provenance(root: Path, build_type: str, abi: str, value: dict[str, object]) -> None:
    (root / build_type / abi / "provenance.json").write_bytes(canonical_json(value))


class RuntimePinTests(unittest.TestCase):
    def setUp(self) -> None:
        self.contract = load_contract()

    def test_tracked_pin_is_canonical_and_exact(self) -> None:
        pin = self.contract.load_pin(PIN_PATH)

        self.assertEqual(
            "https://github.com/cafecito-games/Foundry-Android.git",
            pin.repository,
        )
        self.assertEqual(
            "b8c46c807d467fcd1667b7d4cb04d07a09a08860",
            pin.source_revision,
        )
        self.assertEqual("f22bec563cbb992e770c89688a2c761855102f82", pin.source_tree)
        self.assertEqual("0.1.0-dev-SNAPSHOT", pin.bindings_version)
        self.assertEqual(1, pin.jni_contract_version)
        self.assertEqual("exact-native-source-revision", pin.engine_compatibility_policy)
        self.assertEqual(
            (
                "compatibility/foundry-engine.json",
                "gradlew",
                "runtime/build.gradle",
                "tools/native_bundle.py",
                "tools/sync_engine_pin.py",
                "tools/verify_jni_contract.py",
            ),
            pin.required_paths,
        )
        self.assertEqual(self.contract.canonical_json(json.loads(PIN_PATH.read_bytes())), PIN_PATH.read_bytes())

    def test_pin_rejects_noncanonical_missing_unexpected_and_invalid_fields(self) -> None:
        valid = json.loads(PIN_PATH.read_bytes())
        mutations = {
            "noncanonical": (lambda value: value, "not canonical JSON"),
            "missing source": (lambda value: value.pop("source"), "missing required field"),
            "unexpected field": (
                lambda value: value.__setitem__("surprise", True),
                "unexpected field",
            ),
            "wrong revision": (
                lambda value: value["source"].__setitem__("revision", "not-a-sha"),
                "source.revision",
            ),
            "wrong tree": (
                lambda value: value["source"].__setitem__("tree", "F" * 40),
                "source.tree",
            ),
            "wrong policy": (
                lambda value: value["engine_compatibility"].__setitem__("policy", "ancestor"),
                "engine compatibility policy",
            ),
            "wrong JNI contract": (
                lambda value: value.__setitem__("jni_contract_version", 0),
                "JNI contract",
            ),
        }

        for name, (mutate, diagnostic) in mutations.items():
            with self.subTest(name=name), tempfile.TemporaryDirectory() as temporary:
                value = json.loads(json.dumps(valid))
                mutate(value)
                path = Path(temporary) / "pin.json"
                if name == "noncanonical":
                    path.write_text(json.dumps(value, indent=2), encoding="utf-8")
                else:
                    path.write_bytes(canonical_json(value))
                with self.assertRaisesRegex(self.contract.ContractError, diagnostic):
                    self.contract.load_pin(path)

    def test_git_identities_use_exact_objects_and_ignore_untracked_build_outputs(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            repository = Path(temporary)
            subprocess.run(["git", "init", "-b", "develop"], cwd=repository, check=True, capture_output=True)
            subprocess.run(["git", "config", "user.email", "tests@cafecito.games"], cwd=repository, check=True)
            subprocess.run(["git", "config", "user.name", "Runtime Tests"], cwd=repository, check=True)
            tracked = repository / "tracked.txt"
            tracked.write_text("one\n", encoding="utf-8")
            subprocess.run(["git", "add", "tracked.txt"], cwd=repository, check=True)
            subprocess.run(["git", "commit", "-m", "Initial"], cwd=repository, check=True, capture_output=True)

            revision = subprocess.run(
                ["git", "rev-parse", "HEAD"],
                cwd=repository,
                check=True,
                capture_output=True,
                text=True,
            ).stdout.strip()
            tree = subprocess.run(
                ["git", "rev-parse", "HEAD^{tree}"],
                cwd=repository,
                check=True,
                capture_output=True,
                text=True,
            ).stdout.strip()

            self.assertEqual((revision, tree), self.contract.source_identity(repository, revision))
            self.assertEqual((revision, tree, False), self.contract.foundry_identity(repository))

            (repository / "untracked.bin").write_bytes(b"build output")
            self.assertEqual((revision, tree, False), self.contract.foundry_identity(repository))

            tracked.write_text("dirty\n", encoding="utf-8")
            self.assertEqual((revision, tree, True), self.contract.foundry_identity(repository))


class NativeMatrixTests(unittest.TestCase):
    def setUp(self) -> None:
        self.contract = load_contract()
        self.temporary = tempfile.TemporaryDirectory()
        self.workspace = Path(self.temporary.name)

    def tearDown(self) -> None:
        self.temporary.cleanup()

    def test_complete_matrix_validates_and_stages_only_payload_libraries(self) -> None:
        root = self.workspace / "native"
        create_matrix(root)

        cells = self.contract.validate_native_matrix(root, REVISION, TREE)

        self.assertEqual(12, len(cells))
        self.assertEqual(
            sorted((build_type, abi) for build_type in BUILD_SPECS for abi in ABI_SPECS),
            [(cell.build_type, cell.abi) for cell in cells],
        )
        output = self.workspace / "staged"
        self.contract.stage_native_payload(cells, output)
        self.assertEqual(
            sorted(
                f"{build_type}/{abi}/{library}"
                for build_type in BUILD_SPECS
                for abi in ABI_SPECS
                for library in LIBRARIES
            ),
            sorted(path.relative_to(output).as_posix() for path in output.rglob("*") if path.is_file()),
        )

    def test_matrix_rejects_dirty_mixed_mislabeled_hash_and_shape_drift(self) -> None:
        def mutate_dirty(root: Path) -> None:
            value = read_provenance(root, "debug", "arm64-v8a")
            value["engine"]["dirty"] = True
            write_provenance(root, "debug", "arm64-v8a", value)

        def mutate_revision(root: Path) -> None:
            value = read_provenance(root, "debug", "arm64-v8a")
            value["engine"]["revision"] = "3" * 40
            write_provenance(root, "debug", "arm64-v8a", value)

        def mutate_tree(root: Path) -> None:
            value = read_provenance(root, "debug", "arm64-v8a")
            value["engine"]["tree"] = "4" * 40
            write_provenance(root, "debug", "arm64-v8a", value)

        def mutate_flags(root: Path) -> None:
            value = read_provenance(root, "release", "x86_64")
            value["build"]["production"] = False
            write_provenance(root, "release", "x86_64", value)

        def mutate_abi(root: Path) -> None:
            value = read_provenance(root, "dev", "x86")
            value["build"]["arch"] = "x86_64"
            write_provenance(root, "dev", "x86", value)

        def mutate_hash(root: Path) -> None:
            path = root / "debug/arm64-v8a/libfoundry_android.so"
            contents = bytearray(path.read_bytes())
            contents[-1] ^= 0x01
            path.write_bytes(contents)

        def mutate_missing(root: Path) -> None:
            for path in (root / "debug/arm64-v8a").iterdir():
                path.unlink()
            (root / "debug/arm64-v8a").rmdir()

        def mutate_extra_cell(root: Path) -> None:
            extra = root / "profile/arm64-v8a"
            extra.mkdir(parents=True)
            (extra / "unexpected").write_text("extra", encoding="utf-8")

        def mutate_extra_file(root: Path) -> None:
            (root / "debug/arm64-v8a/extra.so").write_bytes(b"extra")

        def mutate_noncanonical(root: Path) -> None:
            path = root / "debug/arm64-v8a/provenance.json"
            path.write_text(json.dumps(json.loads(path.read_bytes()), indent=2), encoding="utf-8")

        mutations = (
            (mutate_dirty, "dirty Foundry source"),
            (mutate_revision, "engine revision mismatch"),
            (mutate_tree, "engine tree mismatch"),
            (mutate_flags, "build configuration mismatch"),
            (mutate_abi, "ABI mapping mismatch"),
            (mutate_hash, "SHA-256 mismatch"),
            (mutate_missing, "missing native cell"),
            (mutate_extra_cell, "unexpected native cell"),
            (mutate_extra_file, "unexpected native cell file"),
            (mutate_noncanonical, "provenance is not canonical JSON"),
        )
        for mutate, diagnostic in mutations:
            with self.subTest(diagnostic=diagnostic):
                root = self.workspace / diagnostic.replace(" ", "-")
                create_matrix(root)
                mutate(root)
                with self.assertRaisesRegex(self.contract.ContractError, diagnostic):
                    self.contract.validate_native_matrix(root, REVISION, TREE)

    def test_matrix_rejects_symlinked_payload(self) -> None:
        root = self.workspace / "symlink"
        create_matrix(root)
        path = root / "debug/arm64-v8a/libfoundry_android.so"
        path.unlink()
        try:
            os.symlink(root / "release/arm64-v8a/libfoundry_android.so", path)
        except OSError as error:
            self.skipTest(f"symlinks unavailable: {error}")

        with self.assertRaisesRegex(self.contract.ContractError, "symbolic link"):
            self.contract.validate_native_matrix(root, REVISION, TREE)


if __name__ == "__main__":
    unittest.main()
