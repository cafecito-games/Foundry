#!/usr/bin/env python3
"""Validate and stage Foundry-owned Android native cells for the in-tree host."""

from __future__ import annotations

import argparse
import hashlib
import os
import shutil
import sys
import tempfile
from pathlib import Path
from typing import cast

import android_jni_contract as jni_contract
import android_native_contract as contract

SUPPORTED_ABIS = tuple(sorted(contract.ABIS))
ZERO_SHA = "0" * 40
STAGING_ROOT_NAME = "android-native-stage"
ROOT_MARKER_NAME = ".foundry-android-native-stage-root"
ROOT_MARKER_CONTENTS = b"foundry-android-native-stage-root-v1\n"
OUTPUT_MARKER_SUFFIX = ".foundry-android-native-stage"
OUTPUT_MARKER_CONTENTS = b"foundry-android-native-stage-output-v1\n"
REPOSITORY_ROOT = Path(__file__).absolute().parents[2]


class StagingError(RuntimeError):
    """A Foundry-owned native input could not be staged safely."""


def staging_key(
    *,
    revision: str,
    input_identity: str,
    selected_abis: tuple[str, ...],
) -> str:
    """Return a stable input/revision/selection key for a Gradle staging path."""

    _validate_abis(selected_abis)
    contents = "\0".join((revision, input_identity, *selected_abis)).encode()
    return hashlib.sha256(contents).hexdigest()[:20]


def _validate_abis(selected_abis: tuple[str, ...]) -> None:
    if not selected_abis:
        raise StagingError("at least one Android ABI must be selected")
    if len(set(selected_abis)) != len(selected_abis):
        raise StagingError("selected Android ABIs contain duplicates")
    unsupported = sorted(set(selected_abis) - set(SUPPORTED_ABIS))
    if unsupported:
        raise StagingError(f"unsupported Android ABI: {unsupported[0]}")


def trusted_tree(source_root: Path, revision: str) -> str:
    """Resolve the expected tree from the checked-out Foundry source."""

    if contract.SHA_PATTERN.fullmatch(revision) is None:
        raise StagingError("trusted Foundry revision must be a lowercase 40-character Git SHA")
    if revision == ZERO_SHA:
        return ZERO_SHA
    try:
        resolved_revision, tree = contract.source_identity(source_root, revision)
    except contract.ContractError as error:
        raise StagingError(f"unable to resolve trusted Foundry tree: {error}") from error
    if resolved_revision != revision:
        raise StagingError(f"trusted Foundry revision mismatch: expected {revision}, resolved {resolved_revision}")
    return cast(str, tree)


def _absolute_lexical(path: Path, description: str) -> Path:
    if any(part == ".." for part in path.parts):
        raise StagingError(f"{description} contains traversal: {path}")
    if not path.is_absolute():
        raise StagingError(f"{description} must be absolute: {path}")
    return Path(os.path.abspath(path))


def _reject_symlink_components(path: Path, description: str) -> None:
    candidates = (path, *path.parents)
    for candidate in candidates:
        if candidate.is_symlink():
            raise StagingError(f"{description} contains symbolic link: {candidate}")


def _root_marker(staging_root: Path) -> Path:
    return staging_root / ROOT_MARKER_NAME


def _output_marker(output: Path) -> Path:
    return output.parent / f".{output.name}{OUTPUT_MARKER_SUFFIX}"


def _validate_marker(path: Path, expected: bytes, description: str) -> None:
    if path.is_symlink() or not path.is_file():
        raise StagingError(f"{description} is not owned by Foundry: {path}")
    try:
        contents = path.read_bytes()
    except OSError as error:
        raise StagingError(f"unable to read {description} ownership marker {path}: {error}") from error
    if contents != expected:
        raise StagingError(f"{description} has an invalid ownership marker: {path}")


def _contains_user_data(path: Path) -> bool:
    for candidate in path.rglob("*"):
        if candidate.is_symlink() or not candidate.is_dir():
            return True
    return False


def _prepare_staging_root(staging_root: Path) -> Path:
    root = _absolute_lexical(staging_root, "native staging root")
    unsafe_roots = {
        Path(root.anchor),
        Path.home().absolute(),
        Path(tempfile.gettempdir()).absolute(),
        REPOSITORY_ROOT,
    }
    if root in unsafe_roots or root.name != STAGING_ROOT_NAME:
        raise StagingError(f"refusing unsafe native staging root: {root}")
    _reject_symlink_components(root, "native staging root")
    marker = _root_marker(root)
    if root.exists():
        if not root.is_dir():
            raise StagingError(f"native staging root is not a directory: {root}")
        if not marker.exists() and not marker.is_symlink() and not _contains_user_data(root):
            # Gradle may materialize an empty source-set directory before Exec.
            # Claiming an empty tree cannot discard user data; non-empty trees fail closed.
            marker.write_bytes(ROOT_MARKER_CONTENTS)
        _validate_marker(marker, ROOT_MARKER_CONTENTS, "native staging root")
        return root
    if not root.parent.is_dir():
        raise StagingError(f"native staging root parent does not exist: {root.parent}")
    root.mkdir()
    try:
        marker.write_bytes(ROOT_MARKER_CONTENTS)
    except OSError:
        root.rmdir()
        raise
    return root


def _validate_output(staging_root: Path, output: Path) -> tuple[Path, Path]:
    target = _absolute_lexical(output, "native staging output")
    try:
        relative = target.relative_to(staging_root)
    except ValueError as error:
        raise StagingError(f"native staging output is not contained within its owned root: {target}") from error
    if not relative.parts:
        raise StagingError(f"refusing unsafe native staging output: {target}")
    _reject_symlink_components(target, "native staging output")
    marker = _output_marker(target)
    if target.exists():
        if not target.is_dir():
            raise StagingError(f"native staging output exists and is not an owned directory: {target}")
        if not marker.exists() and not marker.is_symlink() and not _contains_user_data(target):
            marker.write_bytes(OUTPUT_MARKER_CONTENTS)
        _validate_marker(marker, OUTPUT_MARKER_CONTENTS, "native staging output")
    elif marker.exists() or marker.is_symlink():
        raise StagingError(f"native staging output marker exists without its owned directory: {marker}")
    return target, marker


def replace_staged_build_type(
    *,
    input_root: Path,
    build_type: str,
    selected_abis: tuple[str, ...],
    staging_root: Path,
    output: Path,
) -> None:
    """Prepare a complete selected payload, then replace its owned stage as a whole."""

    _validate_abis(selected_abis)
    if build_type not in contract.BUILD_TYPES:
        raise StagingError(f"unsupported Android build type: {build_type}")
    owned_root = _prepare_staging_root(staging_root)
    output, output_marker = _validate_output(owned_root, output)
    output.parent.mkdir(parents=True, exist_ok=True)
    _reject_symlink_components(output.parent, "native staging output parent")
    temporary = Path(tempfile.mkdtemp(prefix=f".{output.name}.", dir=output.parent))
    backup: Path | None = None
    created_marker = False
    try:
        for abi in selected_abis:
            source = input_root / build_type / abi
            destination = temporary / abi
            destination.mkdir()
            for library in contract.LIBRARY_NAMES:
                path = source / library
                if path.is_symlink() or not path.is_file():
                    raise StagingError(f"native staging input is missing library: {path}")
                shutil.copyfile(path, destination / library)
        if output.exists():
            backup = Path(tempfile.mkdtemp(prefix=f".{output.name}.backup.", dir=output.parent))
            backup.rmdir()
            os.replace(output, backup)
        else:
            output_marker.write_bytes(OUTPUT_MARKER_CONTENTS)
            created_marker = True
        try:
            os.replace(temporary, output)
        except BaseException:
            if backup is not None:
                os.replace(backup, output)
                backup = None
            elif created_marker:
                output_marker.unlink(missing_ok=True)
            raise
        if backup is not None:
            shutil.rmtree(backup)
            backup = None
    finally:
        if temporary.exists():
            shutil.rmtree(temporary)


def prepare(
    *,
    revision: str,
    tree: str,
    expected_foundry_jni: tuple[str, ...],
    build_type: str,
    selected_abis: tuple[str, ...],
    staging_root: Path,
    output: Path,
    local_root: Path | None,
    native_root: Path | None,
) -> None:
    """Validate one internal input mode and replace its scoped JNI stage."""

    _validate_abis(selected_abis)
    if (local_root is None) == (native_root is None):
        raise StagingError("provide exactly one of --local-root or --native-root")

    root = native_root if native_root is not None else local_root
    assert root is not None
    try:
        if native_root is not None:
            contract.validate_native_matrix(
                root,
                revision,
                tree,
                expected_foundry_jni=expected_foundry_jni,
            )
        else:
            contract.validate_native_cells(
                root,
                revision=revision,
                tree=tree,
                pairs=tuple((build_type, abi) for abi in selected_abis),
                expected_foundry_jni=expected_foundry_jni,
            )
    except contract.ContractError as error:
        raise StagingError(str(error)) from error
    replace_staged_build_type(
        input_root=root,
        build_type=build_type,
        selected_abis=selected_abis,
        staging_root=staging_root,
        output=output,
    )


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--revision", required=True)
    parser.add_argument("--source-root", required=True, type=Path)
    parser.add_argument("--classes-jar", required=True, type=Path)
    parser.add_argument("--build-type", choices=sorted(contract.BUILD_TYPES), required=True)
    parser.add_argument("--selected-abis", required=True)
    parser.add_argument("--staging-root", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    modes = parser.add_mutually_exclusive_group(required=True)
    modes.add_argument("--local-root", type=Path)
    modes.add_argument("--native-root", type=Path)
    return parser


def main() -> int:
    arguments = build_parser().parse_args()
    try:
        tree = trusted_tree(arguments.source_root, arguments.revision)
        expected_foundry_jni = jni_contract.derive_declared_jni_symbols(arguments.classes_jar)
        prepare(
            revision=arguments.revision,
            tree=tree,
            expected_foundry_jni=expected_foundry_jni,
            build_type=arguments.build_type,
            selected_abis=tuple(value for value in arguments.selected_abis.split(",") if value),
            staging_root=arguments.staging_root,
            output=arguments.output,
            local_root=arguments.local_root,
            native_root=arguments.native_root,
        )
    except (StagingError, contract.ContractError, jni_contract.JniContractError, OSError) as error:
        print(f"error: {error}", file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main())
