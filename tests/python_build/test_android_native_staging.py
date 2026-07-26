from __future__ import annotations

import importlib.util
import os
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from types import ModuleType
from unittest import mock

from tests.python_build.android_native_test_support import (
    ABI_SPECS,
    FOUNDRY_SYMBOLS,
    LIBRARIES,
    populate_native_matrix,
)

REPO_ROOT = Path(__file__).resolve().parents[2]
STAGING_PATH = REPO_ROOT / "platform/android/android_native_staging.py"
CONTRACT_PATH = REPO_ROOT / "platform/android/android_native_contract.py"
JNI_CONTRACT_PATH = REPO_ROOT / "platform/android/android_jni_contract.py"
REVISION = "1" * 40
TREE = "2" * 40
ALL_ABIS = tuple(sorted(ABI_SPECS))
TEST_SCRATCH = REPO_ROOT / ".test_scratch"


def temporary_directory() -> tempfile.TemporaryDirectory[str]:
    TEST_SCRATCH.mkdir(exist_ok=True)
    return tempfile.TemporaryDirectory(dir=TEST_SCRATCH)


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
        load_module("android_jni_contract", JNI_CONTRACT_PATH)
        cls.staging = load_module("android_native_staging_for_test", STAGING_PATH)

    @staticmethod
    def staging_paths(root: Path, name: str = "first") -> tuple[Path, Path]:
        staging_root = root / "build/android-native-stage"
        output = staging_root / REVISION / name / "arm64-v8a" / "debug"
        return staging_root, output

    def test_subset_restage_removes_stale_abis_and_old_input_bytes(self) -> None:
        with temporary_directory() as temporary:
            root = Path(temporary)
            first = root / "first"
            second = root / "second"
            staging_root, output = self.staging_paths(root)
            staging_root.parent.mkdir(parents=True)
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
                staging_root=staging_root,
                output=output,
            )
            self.staging.replace_staged_build_type(
                input_root=second,
                build_type="debug",
                selected_abis=("arm64-v8a",),
                staging_root=staging_root,
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
        with temporary_directory() as temporary:
            root = Path(temporary)
            native_root = root / "native"
            populate_native_matrix(native_root, revision=REVISION, tree=TREE)
            staging_root, output = self.staging_paths(root)
            staging_root.parent.mkdir(parents=True)

            self.staging.prepare(
                revision=REVISION,
                tree=TREE,
                expected_foundry_jni=FOUNDRY_SYMBOLS,
                build_type="debug",
                selected_abis=("arm64-v8a",),
                staging_root=staging_root,
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
                    tree=TREE,
                    expected_foundry_jni=FOUNDRY_SYMBOLS,
                    build_type="debug",
                    selected_abis=("arm64-v8a",),
                    staging_root=staging_root,
                    output=output,
                    local_root=None,
                    native_root=native_root,
                )

    def test_prepare_requires_exactly_one_internal_input_mode(self) -> None:
        with temporary_directory() as temporary:
            root = Path(temporary)
            staging_root, output = self.staging_paths(root)
            with self.assertRaisesRegex(self.staging.StagingError, "exactly one"):
                self.staging.prepare(
                    revision=REVISION,
                    tree=TREE,
                    expected_foundry_jni=FOUNDRY_SYMBOLS,
                    build_type="debug",
                    selected_abis=("arm64-v8a",),
                    staging_root=staging_root,
                    output=output,
                    local_root=None,
                    native_root=None,
                )

    def test_prepare_rejects_consistently_forged_tree_and_mismatched_revision_tree_pair(self) -> None:
        with temporary_directory() as temporary:
            root = Path(temporary)
            native_root = root / "native"
            forged_tree = "3" * 40
            populate_native_matrix(native_root, revision=REVISION, tree=forged_tree)
            staging_root, output = self.staging_paths(root)

            with self.assertRaisesRegex(self.staging.StagingError, "engine tree mismatch"):
                self.staging.prepare(
                    revision=REVISION,
                    tree=TREE,
                    expected_foundry_jni=FOUNDRY_SYMBOLS,
                    build_type="debug",
                    selected_abis=("arm64-v8a",),
                    staging_root=staging_root,
                    output=output,
                    local_root=None,
                    native_root=native_root,
                )

            populate_native_matrix(native_root, revision=REVISION, tree=TREE)
            with self.assertRaisesRegex(self.staging.StagingError, "engine tree mismatch"):
                self.staging.prepare(
                    revision=REVISION,
                    tree="4" * 40,
                    expected_foundry_jni=FOUNDRY_SYMBOLS,
                    build_type="debug",
                    selected_abis=("arm64-v8a",),
                    staging_root=staging_root,
                    output=output,
                    local_root=None,
                    native_root=native_root,
                )

    def test_trusted_tree_resolves_revision_pair_and_zero_identity_without_git(self) -> None:
        with temporary_directory() as temporary:
            repository = Path(temporary) / "repository"
            repository.mkdir()
            subprocess.run(["git", "init", "-b", "develop"], cwd=repository, check=True, capture_output=True)
            subprocess.run(["git", "config", "user.email", "tests@cafecito.games"], cwd=repository, check=True)
            subprocess.run(["git", "config", "user.name", "Tests"], cwd=repository, check=True)
            (repository / "tracked").write_text("one\n", encoding="utf-8")
            subprocess.run(["git", "add", "tracked"], cwd=repository, check=True)
            subprocess.run(["git", "commit", "-m", "one"], cwd=repository, check=True, capture_output=True)
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

            self.assertEqual(tree, self.staging.trusted_tree(repository, revision))
            self.assertEqual("0" * 40, self.staging.trusted_tree(Path(temporary) / "no-git", "0" * 40))
            with self.assertRaisesRegex(self.staging.StagingError, "unable to resolve trusted Foundry tree"):
                self.staging.trusted_tree(repository, "f" * 40)

    def test_staging_rejects_symlink_target_and_symlink_ancestor_without_touching_donor(self) -> None:
        with temporary_directory() as temporary:
            root = Path(temporary)
            input_root = root / "input"
            for abi in ALL_ABIS:
                directory = input_root / "debug" / abi
                directory.mkdir(parents=True)
                for library in LIBRARIES:
                    (directory / library).write_bytes(f"input:{abi}:{library}".encode())
            staging_root, output = self.staging_paths(root)
            staging_root.parent.mkdir(parents=True)
            donor = root / "donor"
            donor.mkdir()
            sentinel = donor / "sentinel"
            sentinel.write_text("keep", encoding="utf-8")

            self.staging.replace_staged_build_type(
                input_root=input_root,
                build_type="debug",
                selected_abis=("arm64-v8a",),
                staging_root=staging_root,
                output=output,
            )
            shutil.rmtree(output)
            try:
                os.symlink(donor, output)
            except OSError as error:
                self.skipTest(f"symlinks unavailable: {error}")
            with self.assertRaisesRegex(self.staging.StagingError, "symbolic link"):
                self.staging.replace_staged_build_type(
                    input_root=input_root,
                    build_type="debug",
                    selected_abis=("arm64-v8a",),
                    staging_root=staging_root,
                    output=output,
                )
            self.assertEqual("keep", sentinel.read_text(encoding="utf-8"))
            output.unlink()

            ancestor = staging_root / "ancestor"
            os.symlink(donor, ancestor)
            with self.assertRaisesRegex(self.staging.StagingError, "symbolic link"):
                self.staging.replace_staged_build_type(
                    input_root=input_root,
                    build_type="debug",
                    selected_abis=("arm64-v8a",),
                    staging_root=staging_root,
                    output=ancestor / "debug",
                )
            self.assertEqual("keep", sentinel.read_text(encoding="utf-8"))

    def test_staging_rejects_traversal_broad_paths_and_unowned_existing_directory(self) -> None:
        with temporary_directory() as temporary:
            root = Path(temporary)
            input_root = root / "input"
            directory = input_root / "debug/arm64-v8a"
            directory.mkdir(parents=True)
            for library in LIBRARIES:
                (directory / library).write_bytes(library.encode())
            staging_root, output = self.staging_paths(root)
            staging_root.parent.mkdir(parents=True)

            unsafe_cases = (
                (staging_root, staging_root / ".." / "escape"),
                (staging_root, staging_root),
                (Path("/"), Path("/danger")),
                (Path.home(), Path.home() / "danger"),
                (Path(tempfile.gettempdir()), Path(tempfile.gettempdir()) / "danger"),
                (REPO_ROOT, REPO_ROOT / "danger"),
            )
            for unsafe_root, unsafe_output in unsafe_cases:
                with self.subTest(root=unsafe_root, output=unsafe_output):
                    with self.assertRaisesRegex(self.staging.StagingError, "unsafe|traversal|contained"):
                        self.staging.replace_staged_build_type(
                            input_root=input_root,
                            build_type="debug",
                            selected_abis=("arm64-v8a",),
                            staging_root=unsafe_root,
                            output=unsafe_output,
                        )

            self.staging.replace_staged_build_type(
                input_root=input_root,
                build_type="debug",
                selected_abis=("arm64-v8a",),
                staging_root=staging_root,
                output=output,
            )
            unowned = staging_root / REVISION / "unowned" / "arm64-v8a/debug"
            unowned.mkdir(parents=True)
            sentinel = unowned / "sentinel"
            sentinel.write_text("keep", encoding="utf-8")
            with self.assertRaisesRegex(self.staging.StagingError, "not owned"):
                self.staging.replace_staged_build_type(
                    input_root=input_root,
                    build_type="debug",
                    selected_abis=("arm64-v8a",),
                    staging_root=staging_root,
                    output=unowned,
                )
            self.assertEqual("keep", sentinel.read_text(encoding="utf-8"))

    def test_replacement_failure_restores_owned_output_and_cleans_temporary_directories(self) -> None:
        with temporary_directory() as temporary:
            root = Path(temporary)
            first = root / "first"
            second = root / "second"
            for source, label in ((first, "old"), (second, "new")):
                directory = source / "debug/arm64-v8a"
                directory.mkdir(parents=True)
                for library in LIBRARIES:
                    (directory / library).write_text(f"{label}:{library}", encoding="utf-8")
            staging_root, output = self.staging_paths(root)
            staging_root.parent.mkdir(parents=True)
            self.staging.replace_staged_build_type(
                input_root=first,
                build_type="debug",
                selected_abis=("arm64-v8a",),
                staging_root=staging_root,
                output=output,
            )
            real_replace = self.staging.os.replace

            def fail_new_install(source, destination):
                source_name = Path(source).name
                if (
                    Path(destination) == output
                    and source_name.startswith(f".{output.name}.")
                    and ".backup." not in source_name
                ):
                    raise OSError("injected replacement failure")
                return real_replace(source, destination)

            with mock.patch.object(self.staging.os, "replace", side_effect=fail_new_install):
                with self.assertRaisesRegex(OSError, "injected replacement failure"):
                    self.staging.replace_staged_build_type(
                        input_root=second,
                        build_type="debug",
                        selected_abis=("arm64-v8a",),
                        staging_root=staging_root,
                        output=output,
                    )

            for library in LIBRARIES:
                self.assertEqual(
                    f"old:{library}",
                    (output / "arm64-v8a" / library).read_text(encoding="utf-8"),
                )
            leftovers = [
                path.name
                for path in output.parent.iterdir()
                if path.name.startswith(f".{output.name}.")
                and not path.name.endswith(self.staging.OUTPUT_MARKER_SUFFIX)
            ]
            self.assertEqual([], leftovers)

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
        self.assertIn("--source-root", result.stdout)
        self.assertIn("--classes-jar", result.stdout)
        self.assertIn("--staging-root", result.stdout)
        self.assertNotIn("--native-bundle", result.stdout)
        self.assertNotIn("--runtime-scratch", result.stdout)


if __name__ == "__main__":
    unittest.main()
