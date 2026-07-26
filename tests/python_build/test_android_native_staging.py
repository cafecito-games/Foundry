from __future__ import annotations

import importlib.util
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from types import ModuleType

from tests.python_build.android_native_test_support import ABI_SPECS, LIBRARIES, populate_native_matrix

REPO_ROOT = Path(__file__).resolve().parents[2]
STAGING_PATH = REPO_ROOT / "platform/android/android_native_staging.py"
CONTRACT_PATH = REPO_ROOT / "platform/android/android_native_contract.py"
REVISION = "1" * 40
TREE = "2" * 40
ALL_ABIS = tuple(sorted(ABI_SPECS))


def load_module(name: str, path: Path) -> ModuleType:
    if not path.is_file():
        raise AssertionError(f"missing internal Android native module: {path}")
    spec = importlib.util.spec_from_file_location(name, path)
    if spec is None or spec.loader is None:
        raise AssertionError(f"could not load internal Android native module: {path}")
    module = importlib.util.module_from_spec(spec)
    sys.modules[name] = module
    spec.loader.exec_module(module)
    return module


class AndroidNativeStagingTests(unittest.TestCase):
    staging: ModuleType

    @classmethod
    def setUpClass(cls) -> None:
        load_module("android_native_contract", CONTRACT_PATH)
        cls.staging = load_module("android_native_staging_for_test", STAGING_PATH)

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
            self.assertEqual(sorted(f"arm64-v8a/{library}" for library in LIBRARIES), files)
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
                input_identity="root:/native/two",
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

    def test_native_root_validates_all_cells_before_staging_a_subset(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            native_root = root / "native"
            populate_native_matrix(native_root, revision=REVISION, tree=TREE)
            output = root / "stage"

            self.staging.prepare(
                revision=REVISION,
                build_type="debug",
                selected_abis=("arm64-v8a",),
                output=output,
                local_root=None,
                native_root=native_root,
            )

            self.assertEqual(
                sorted(f"arm64-v8a/{library}" for library in LIBRARIES),
                sorted(path.relative_to(output).as_posix() for path in output.rglob("*.so")),
            )

            (native_root / "release/x86_64/provenance.json").unlink()
            with self.assertRaisesRegex(self.staging.StagingError, "missing file.*provenance.json"):
                self.staging.prepare(
                    revision=REVISION,
                    build_type="debug",
                    selected_abis=("arm64-v8a",),
                    output=output,
                    local_root=None,
                    native_root=native_root,
                )

    def test_prepare_requires_exactly_one_internal_input_mode(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            with self.assertRaisesRegex(self.staging.StagingError, "exactly one"):
                self.staging.prepare(
                    revision=REVISION,
                    build_type="debug",
                    selected_abis=("arm64-v8a",),
                    output=root / "stage",
                    local_root=None,
                    native_root=None,
                )

    def test_cli_exposes_only_local_and_native_root_inputs(self) -> None:
        result = subprocess.run(
            [sys.executable, STAGING_PATH, "--help"],
            cwd=REPO_ROOT,
            check=True,
            capture_output=True,
            text=True,
        )

        self.assertIn("--local-root", result.stdout)
        self.assertIn("--native-root", result.stdout)
        self.assertNotIn("--native-bundle", result.stdout)
        self.assertNotIn("--runtime-scratch", result.stdout)


if __name__ == "__main__":
    unittest.main()
