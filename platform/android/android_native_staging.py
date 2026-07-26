#!/usr/bin/env python3
"""Validate and stage Foundry-owned Android native inputs for the in-tree host."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import shutil
import sys
import tempfile
import zipfile
from pathlib import Path
from typing import Any

import android_native_bundle as bundle
import android_runtime_contract as contract

WS2_REMOVAL_MARKER = "WS2_REMOVE_ANDROID_RUNTIME_COMPAT_BRIDGE"
FOUNDRY_REPOSITORY = "https://github.com/cafecito-games/Foundry"
SUPPORTED_ABIS = tuple(sorted(contract.ABIS))
ELF_ABIS = {
    "arm64-v8a": {"elf_class": 64, "elf_machine": 183},
    "armeabi-v7a": {"elf_class": 32, "elf_machine": 40},
    "x86": {"elf_class": 32, "elf_machine": 3},
    "x86_64": {"elf_class": 64, "elf_machine": 62},
}
EXTERNAL_JNI_ALLOWLIST = [
    {
        "component": "Swappy (Android Game SDK)",
        "owner": "Google LLC",
        "symbol": "Java_com_google_androidgamesdk_ChoreographerCallback_nOnChoreographer",
    },
    {
        "component": "Swappy (Android Game SDK)",
        "owner": "Google LLC",
        "symbol": "Java_com_google_androidgamesdk_SwappyDisplayManager_nOnRefreshPeriodChanged",
    },
    {
        "component": "Swappy (Android Game SDK)",
        "owner": "Google LLC",
        "symbol": "Java_com_google_androidgamesdk_SwappyDisplayManager_nSetSupportedRefreshPeriods",
    },
]


class StagingError(RuntimeError):
    """A native caller-bridge input failed closed."""


def create_compatibility(
    *,
    revision: str,
    engine_version: str,
    bindings_version: str,
) -> dict[str, Any]:
    """Create the compatibility document embedded by the existing public bundle."""

    if contract.SHA_PATTERN.fullmatch(revision) is None:
        raise StagingError("engine revision must be a lowercase 40-character Git SHA")
    if not engine_version or not bindings_version:
        raise StagingError("engine and bindings versions must be non-empty")
    return {
        "bindings": {"version": bindings_version},
        "engine": {
            "repository": FOUNDRY_REPOSITORY,
            "revision": revision,
            "version": engine_version,
            "version_components": _version_components(engine_version),
        },
        "jni_contract_version": 1,
        "native": {
            "abis": ELF_ABIS,
            "build_types": sorted(contract.BUILD_TYPES),
            "external_jni_allowlist": EXTERNAL_JNI_ALLOWLIST,
            "libraries": ["libfoundry_android.so", "libc++_shared.so"],
        },
        "schema_version": 1,
    }


def _version_components(version: str) -> dict[str, object]:
    pieces = version.replace("-", ".").split(".")
    numeric: list[int] = []
    while pieces and len(numeric) < 3:
        piece = pieces.pop(0)
        if not piece.isdigit():
            break
        numeric.append(int(piece))
    while len(numeric) < 3:
        numeric.append(0)
    return {
        "major": numeric[0],
        "minor": numeric[1],
        "module_config": "",
        "patch": numeric[2],
        "status": ".".join(pieces) or "custom",
    }


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
    """Prepare a complete selected build-type payload, then replace its owned stage as a whole."""

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


def _write_compatibility(path: Path, compatibility: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(bundle.canonical_json(compatibility))


def _native_root_fingerprint(root: Path, cells: tuple[contract.NativeCell, ...]) -> str:
    digest = hashlib.sha256()
    digest.update(str(root.resolve()).encode())
    for cell in cells:
        provenance = cell.directory / "provenance.json"
        digest.update(f"\0{cell.build_type}/{cell.abi}\0".encode())
        digest.update(hashlib.sha256(provenance.read_bytes()).digest())
    return digest.hexdigest()


def _create_bundle_from_cells(
    compatibility: dict[str, Any],
    cells: tuple[contract.NativeCell, ...],
    output: Path,
) -> None:
    payloads: dict[str, bytes] = {}
    records: list[dict[str, Any]] = []
    for cell in cells:
        for library in ("libfoundry_android.so", "libc++_shared.so"):
            path = f"{cell.build_type}/{cell.abi}/{library}"
            contents = (cell.directory / library).read_bytes()
            payloads[path] = contents
            records.append(bundle.inspect_payload(path=path, contents=contents, compatibility=compatibility))
    records.sort(key=lambda record: record["path"])
    bundle.verify_consistent_jni_surface(records)
    manifest = {
        "bundle_version": compatibility["bindings"]["version"],
        "compatibility": compatibility,
        "files": records,
        "format": bundle.BUNDLE_FORMAT,
        "matrix": bundle.expected_matrix(compatibility),
        "schema_version": bundle.BUNDLE_SCHEMA_VERSION,
    }
    bundle._write_bundle_zip(output, manifest, payloads)


def _embedded_bundle_compatibility(path: Path, revision: str, engine_version: str) -> dict[str, Any]:
    if path.is_symlink() or not path.is_file():
        raise StagingError(f"native bundle does not exist: {path}")
    try:
        with zipfile.ZipFile(path) as archive:
            manifest, _ = bundle._load_manifest(archive)
            compatibility = manifest["compatibility"]
    except (OSError, KeyError, TypeError, zipfile.BadZipFile, bundle.BundleError) as error:
        raise StagingError(f"unable to read native bundle compatibility: {error}") from error
    if not isinstance(compatibility, dict):
        raise StagingError("native bundle compatibility metadata must be an object")
    bundle.validate_compatibility_shape(compatibility)
    if compatibility["engine"]["version_components"] != _version_components(engine_version):
        raise StagingError(
            "native bundle engine version mismatch: "
            f"bundle records {compatibility['engine']['version']!r}, expected {engine_version!r}"
        )
    expected = create_compatibility(
        revision=revision,
        engine_version=compatibility["engine"]["version"],
        bindings_version=compatibility["bindings"]["version"],
    )
    if compatibility != expected:
        raise StagingError("native bundle compatibility does not match the in-tree Android contract")
    return compatibility


def prepare(
    *,
    revision: str,
    engine_version: str,
    bindings_version: str,
    build_type: str,
    selected_abis: tuple[str, ...],
    output: Path,
    local_root: Path | None,
    native_root: Path | None,
    native_bundle: Path | None,
    runtime_scratch: Path | None,
) -> None:
    """Validate one caller mode and replace its revision-scoped JNI stage as a whole."""

    _validate_abis(selected_abis)
    modes = (local_root is not None, native_root is not None, native_bundle is not None)
    if sum(modes) != 1:
        raise StagingError("provide exactly one of --local-root, --native-root, or --native-bundle")

    if native_bundle is not None:
        if runtime_scratch is None:
            raise StagingError("--runtime-scratch is required with --native-bundle")
        runtime_scratch.mkdir(parents=True, exist_ok=True)
        compatibility = _embedded_bundle_compatibility(native_bundle, revision, engine_version)
        compatibility_path = runtime_scratch / "foundry-engine.json"
        _write_compatibility(compatibility_path, compatibility)
        extracted_root = Path(tempfile.mkdtemp(prefix=".native-extracted.", dir=runtime_scratch))
        try:
            extracted_build_type = extracted_root / build_type
            bundle.extract_bundle(native_bundle, compatibility_path, build_type, extracted_build_type)
            replace_staged_build_type(
                input_root=extracted_root,
                build_type=build_type,
                selected_abis=selected_abis,
                output=output,
            )
        finally:
            if extracted_root.exists():
                shutil.rmtree(extracted_root)
        return

    root = native_root if native_root is not None else local_root
    assert root is not None
    tree = _tree_from_cell(root, build_type, selected_abis[0])
    if native_root is not None:
        if runtime_scratch is None:
            raise StagingError("--runtime-scratch is required with --native-root")
        runtime_scratch.mkdir(parents=True, exist_ok=True)
        cells = contract.validate_native_matrix(root, revision, tree)
        compatibility = create_compatibility(
            revision=revision,
            engine_version=engine_version,
            bindings_version=bindings_version,
        )
        compatibility_path = runtime_scratch / "foundry-engine.json"
        _write_compatibility(compatibility_path, compatibility)
        output_bundle = runtime_scratch / "foundry-native.zip"
        fingerprint = _native_root_fingerprint(root, cells)
        fingerprint_path = runtime_scratch / "native-root-input.json"
        expected_record = {"fingerprint": fingerprint, "revision": revision}
        cached_record: object = None
        if fingerprint_path.is_file():
            try:
                cached_record = json.loads(fingerprint_path.read_bytes())
            except (OSError, json.JSONDecodeError):
                cached_record = None
        if cached_record == expected_record and output_bundle.is_file():
            bundle.validate_bundle(output_bundle, compatibility_path)
        else:
            _create_bundle_from_cells(compatibility, cells, output_bundle)
            fingerprint_path.write_bytes(contract.canonical_json(expected_record))
    else:
        contract.validate_native_cells(
            root,
            revision=revision,
            tree=tree,
            pairs=tuple((build_type, abi) for abi in selected_abis),
        )
    replace_staged_build_type(
        input_root=root,
        build_type=build_type,
        selected_abis=selected_abis,
        output=output,
    )


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--revision", required=True)
    parser.add_argument("--engine-version", required=True)
    parser.add_argument("--bindings-version", required=True)
    parser.add_argument("--build-type", choices=sorted(contract.BUILD_TYPES), required=True)
    parser.add_argument("--selected-abis", required=True)
    parser.add_argument("--output", required=True, type=Path)
    modes = parser.add_mutually_exclusive_group(required=True)
    modes.add_argument("--local-root", type=Path)
    modes.add_argument("--native-root", type=Path)
    modes.add_argument("--native-bundle", type=Path)
    parser.add_argument("--runtime-scratch", type=Path)
    return parser


def main() -> int:
    arguments = build_parser().parse_args()
    try:
        prepare(
            revision=arguments.revision,
            engine_version=arguments.engine_version,
            bindings_version=arguments.bindings_version,
            build_type=arguments.build_type,
            selected_abis=tuple(value for value in arguments.selected_abis.split(",") if value),
            output=arguments.output,
            local_root=arguments.local_root,
            native_root=arguments.native_root,
            native_bundle=arguments.native_bundle,
            runtime_scratch=arguments.runtime_scratch,
        )
    except (StagingError, contract.ContractError, bundle.BundleError, OSError) as error:
        print(f"error: {error}", file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main())
