from __future__ import annotations

import importlib.util
import sys
import tempfile
import unittest
from pathlib import Path
from types import ModuleType

from tests.python_build.android_native_bundle_test_support import create_bundle

REPO_ROOT = Path(__file__).resolve().parents[2]
STAGING_PATH = REPO_ROOT / "platform/android/android_native_staging.py"
BUNDLE_PATH = REPO_ROOT / "platform/android/android_native_bundle.py"
CONTRACT_PATH = REPO_ROOT / "platform/android/android_runtime_contract.py"
REVISION = "1" * 40
TREE = "2" * 40
ALL_ABIS = ("arm64-v8a", "armeabi-v7a", "x86", "x86_64")
LIBRARIES = ("libc++_shared.so", "libfoundry_android.so")


def load_module(name: str, path: Path) -> ModuleType:
    if not path.is_file():
        raise AssertionError(f"missing Android native bridge module: {path}")
    spec = importlib.util.spec_from_file_location(name, path)
    if spec is None or spec.loader is None:
        raise AssertionError(f"could not load Android native bridge module: {path}")
    module = importlib.util.module_from_spec(spec)
    sys.modules[name] = module
    spec.loader.exec_module(module)
    return module


class AndroidNativeStagingTests(unittest.TestCase):
    bundle: ModuleType
    staging: ModuleType

    @classmethod
    def setUpClass(cls) -> None:
        cls.bundle = load_module("android_native_bundle", BUNDLE_PATH)
        load_module("android_runtime_contract", CONTRACT_PATH)
        cls.staging = load_module("android_native_staging_for_test", STAGING_PATH)

    def test_bridge_preserves_the_existing_public_bundle_format(self) -> None:
        self.assertEqual("foundry-android-native-bundle", self.bundle.BUNDLE_FORMAT)
        self.assertEqual(1, self.bundle.BUNDLE_SCHEMA_VERSION)
        self.assertEqual("WS2_REMOVE_ANDROID_RUNTIME_COMPAT_BRIDGE", self.staging.WS2_REMOVAL_MARKER)

    def test_compatibility_document_describes_exactly_twelve_cells(self) -> None:
        document = self.staging.create_compatibility(
            revision=REVISION,
            engine_version="0.1.0-dev",
            bindings_version="0.1.0-dev",
        )

        self.assertEqual(["debug", "dev", "release"], document["native"]["build_types"])
        self.assertEqual(list(ALL_ABIS), list(document["native"]["abis"]))
        self.assertEqual(["libfoundry_android.so", "libc++_shared.so"], document["native"]["libraries"])
        self.assertEqual(12, len(self.bundle.expected_matrix(document)))

    def test_subset_restage_removes_stale_abis_and_old_input_bytes(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            first = root / "first"
            second = root / "second"
            output = root / "stage"
            for source, label in ((first, "old"), (second, "new")):
                for abi in ALL_ABIS:
                    cell = source / "debug" / abi
                    cell.mkdir(parents=True)
                    for library in LIBRARIES:
                        (cell / library).write_text(f"{label}:{abi}:{library}\n", encoding="utf-8")

            self.staging.replace_staged_build_type(
                input_root=first,
                build_type="debug",
                selected_abis=ALL_ABIS,
                output=output,
            )
            self.staging.replace_staged_build_type(
                input_root=second,
                build_type="debug",
                selected_abis=("arm64-v8a",),
                output=output,
            )

            files = sorted(path.relative_to(output).as_posix() for path in output.rglob("*") if path.is_file())
            self.assertEqual(
                [f"arm64-v8a/{library}" for library in LIBRARIES],
                files,
            )
            for path in output.rglob("*.so"):
                self.assertTrue(path.read_text(encoding="utf-8").startswith("new:"))

    def test_staging_key_changes_with_revision_input_and_abi_selection(self) -> None:
        first = self.staging.staging_key(
            revision=REVISION,
            input_identity="root:/native/one",
            selected_abis=ALL_ABIS,
        )
        self.assertNotEqual(
            first,
            self.staging.staging_key(
                revision="3" * 40,
                input_identity="root:/native/one",
                selected_abis=ALL_ABIS,
            ),
        )
        self.assertNotEqual(
            first,
            self.staging.staging_key(
                revision=REVISION,
                input_identity="bundle:/native/two.zip",
                selected_abis=ALL_ABIS,
            ),
        )
        self.assertNotEqual(
            first,
            self.staging.staging_key(
                revision=REVISION,
                input_identity="root:/native/one",
                selected_abis=("arm64-v8a",),
            ),
        )

    def test_bundle_bridge_rejects_wrong_current_engine_version(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            _, _, native_bundle = create_bundle(root)
            with self.assertRaisesRegex(self.staging.StagingError, "engine version mismatch"):
                self.staging.prepare(
                    revision="a" * 40,
                    engine_version="9.9.9",
                    bindings_version="ignored",
                    build_type="debug",
                    selected_abis=("arm64-v8a",),
                    output=root / "stage",
                    local_root=None,
                    native_root=None,
                    native_bundle=native_bundle,
                    runtime_scratch=root / "scratch",
                )


if __name__ == "__main__":
    unittest.main()
