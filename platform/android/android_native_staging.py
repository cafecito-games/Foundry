#!/usr/bin/env python3
"""Validate and stage Foundry-owned Android native cells for the in-tree host."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import shutil
import sys
import tempfile
from pathlib import Path

import android_native_contract as contract

SUPPORTED_ABIS = tuple(sorted(contract.ABIS))


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


def _remove_owned_path(path: Path) -> None:
    if path.is_symlink() or path.is_file():
        path.unlink()
    elif path.is_dir():
        shutil.rmtree(path)


def replace_staged_build_type(
    *,
    input_root: Path,
    build_type: str,
    selected_abis: tuple[str, ...],
    output: Path,
) -> None:
    """Prepare a complete selected payload, then replace its owned stage as a whole."""

    _validate_abis(selected_abis)
    if build_type not in contract.BUILD_TYPES:
        raise StagingError(f"unsupported Android build type: {build_type}")
    output = output.resolve()
    if output == Path(output.anchor) or output == Path.home().resolve():
        raise StagingError(f"refusing unsafe native staging output: {output}")
    output.parent.mkdir(parents=True, exist_ok=True)
    temporary = Path(tempfile.mkdtemp(prefix=f".{output.name}.", dir=output.parent))
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
        if output.exists() or output.is_symlink():
            _remove_owned_path(output)
        os.replace(temporary, output)
    finally:
        if temporary.exists():
            shutil.rmtree(temporary)


def _tree_from_cell(root: Path, build_type: str, abi: str) -> str:
    provenance = root / build_type / abi / "provenance.json"
    try:
        value = json.loads(provenance.read_bytes())
        tree = value["engine"]["tree"]
    except (OSError, KeyError, TypeError, json.JSONDecodeError) as error:
        raise StagingError(f"unable to read native provenance tree from {provenance}: {error}") from error
    if not isinstance(tree, str) or contract.SHA_PATTERN.fullmatch(tree) is None:
        raise StagingError(f"native provenance contains an invalid tree: {provenance}")
    return tree


def prepare(
    *,
    revision: str,
    build_type: str,
    selected_abis: tuple[str, ...],
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
    tree = _tree_from_cell(root, build_type, selected_abis[0])
    try:
        if native_root is not None:
            contract.validate_native_matrix(root, revision, tree)
        else:
            contract.validate_native_cells(
                root,
                revision=revision,
                tree=tree,
                pairs=tuple((build_type, abi) for abi in selected_abis),
            )
    except contract.ContractError as error:
        raise StagingError(str(error)) from error
    replace_staged_build_type(
        input_root=root,
        build_type=build_type,
        selected_abis=selected_abis,
        output=output,
    )


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--revision", required=True)
    parser.add_argument("--build-type", choices=sorted(contract.BUILD_TYPES), required=True)
    parser.add_argument("--selected-abis", required=True)
    parser.add_argument("--output", required=True, type=Path)
    modes = parser.add_mutually_exclusive_group(required=True)
    modes.add_argument("--local-root", type=Path)
    modes.add_argument("--native-root", type=Path)
    return parser


def main() -> int:
    arguments = build_parser().parse_args()
    try:
        prepare(
            revision=arguments.revision,
            build_type=arguments.build_type,
            selected_abis=tuple(value for value in arguments.selected_abis.split(",") if value),
            output=arguments.output,
            local_root=arguments.local_root,
            native_root=arguments.native_root,
        )
    except (StagingError, contract.ContractError, OSError) as error:
        print(f"error: {error}", file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main())
