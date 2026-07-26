#!/usr/bin/env python3
"""Build Foundry Android runtime AARs from explicit, revision-matched inputs."""

from __future__ import annotations

import argparse
import json
import os
import shutil
import subprocess
import sys
import tarfile
import tempfile
from dataclasses import dataclass
from pathlib import Path, PurePosixPath
from typing import Any

import android_runtime_contract as contract

FOUNDRY_REPOSITORY = "https://github.com/cafecito-games/Foundry"
GRADLE_TASKS = (
    ":runtime:testDebugUnitTest",
    ":runtime:testDevUnitTest",
    ":runtime:testReleaseUnitTest",
    ":runtime:assembleDebugAndroidTest",
    ":runtime:verifyJniContract",
    ":runtime:lint",
    ":runtime:inspectArtifacts",
)


class BuildError(RuntimeError):
    """A fail-closed Android runtime preparation error."""


@dataclass(frozen=True)
class BuildResult:
    bundle: Path
    aars: dict[str, Path]
    compatibility: Path


def _run_text(
    arguments: list[str],
    *,
    cwd: Path,
    description: str,
) -> str:
    try:
        result = subprocess.run(
            arguments,
            cwd=cwd,
            check=True,
            capture_output=True,
            text=True,
        )
    except (OSError, subprocess.CalledProcessError) as error:
        stdout = getattr(error, "stdout", "") or ""
        stderr = getattr(error, "stderr", "") or ""
        detail = "\n".join(part.strip() for part in (stdout, stderr) if part.strip())
        if not detail:
            detail = str(error)
        raise BuildError(f"{description} failed: {detail}") from error
    return result.stdout.strip()


def _remove_owned_path(path: Path) -> None:
    if path.is_symlink() or path.is_file():
        path.unlink()
    elif path.is_dir():
        shutil.rmtree(path)


def _check_archive_member(member: tarfile.TarInfo) -> None:
    path = PurePosixPath(member.name)
    if path.is_absolute() or ".." in path.parts:
        raise BuildError(f"standalone source archive has unsafe path: {member.name!r}")
    if member.issym() or member.islnk() or member.isdev():
        raise BuildError(f"standalone source archive has unsupported entry: {member.name!r}")


def _export_git_object(repository: Path, revision: str, output: Path) -> None:
    output.parent.mkdir(parents=True, exist_ok=True)
    archive_path = output.parent / f".{output.name}-{revision}.tar"
    staging = Path(
        tempfile.mkdtemp(
            prefix=f".{output.name}-",
            dir=output.parent,
        )
    )
    try:
        with archive_path.open("wb") as archive:
            try:
                subprocess.run(
                    ["git", "archive", "--format=tar", revision],
                    cwd=repository,
                    check=True,
                    stdout=archive,
                    stderr=subprocess.PIPE,
                )
            except (OSError, subprocess.CalledProcessError) as error:
                stderr = getattr(error, "stderr", b"") or b""
                detail = stderr.decode(errors="replace").strip() or str(error)
                raise BuildError(f"unable to export pinned standalone source: {detail}") from error
        with tarfile.open(archive_path, "r:") as archive:
            members = archive.getmembers()
            for member in members:
                _check_archive_member(member)
            archive.extractall(staging, members=members, filter="data")
        if output.exists() or output.is_symlink():
            _remove_owned_path(output)
        os.replace(staging, output)
    finally:
        archive_path.unlink(missing_ok=True)
        if staging.exists():
            shutil.rmtree(staging)


def _read_canonical_json(path: Path, description: str) -> dict[str, Any]:
    if not path.is_file():
        raise BuildError(f"{description} does not exist: {path}")
    raw = path.read_bytes()
    try:
        value = json.loads(raw)
    except (UnicodeDecodeError, json.JSONDecodeError) as error:
        raise BuildError(f"unable to parse {description} {path}: {error}") from error
    if not isinstance(value, dict):
        raise BuildError(f"{description} must contain a JSON object: {path}")
    if raw != contract.canonical_json(value):
        raise BuildError(f"{description} is not canonical JSON: {path}")
    return value


def _validate_exported_source(exported: Path, pin: contract.RuntimePin) -> None:
    for relative_path in pin.required_paths:
        path = exported / relative_path
        if path.is_symlink() or not path.is_file():
            raise BuildError(f"pinned standalone source is missing required file: {relative_path}")
    compatibility = _read_canonical_json(
        exported / "compatibility/foundry-engine.json",
        "pinned standalone compatibility",
    )
    try:
        bindings_version = compatibility["bindings"]["version"]
        jni_contract_version = compatibility["jni_contract_version"]
    except (KeyError, TypeError) as error:
        raise BuildError(f"pinned standalone compatibility is missing required field: {error}") from error
    if bindings_version != pin.bindings_version:
        raise BuildError(
            f"pinned standalone bindings version mismatch: expected {pin.bindings_version!r}, got {bindings_version!r}"
        )
    if jni_contract_version != pin.jni_contract_version:
        raise BuildError(
            "pinned standalone JNI contract mismatch: "
            f"expected {pin.jni_contract_version}, got {jni_contract_version!r}"
        )


def _fetch_repository(pin: contract.RuntimePin, scratch: Path) -> Path:
    repository = scratch / "standalone-repository.git"
    if repository.exists() and not (repository / "HEAD").is_file():
        raise BuildError(f"standalone fetch cache is not a bare Git repository: {repository}")
    if not repository.exists():
        repository.mkdir(parents=True)
        _run_text(
            ["git", "init", "--bare"],
            cwd=repository,
            description="initializing standalone fetch cache",
        )
    _run_text(
        [
            "git",
            "fetch",
            "--depth=1",
            "--no-tags",
            pin.repository,
            pin.source_revision,
        ],
        cwd=repository,
        description=f"fetching exact standalone revision {pin.source_revision}",
    )
    return repository


def export_pinned_source(
    *,
    pin: contract.RuntimePin,
    source_repository: Path | None,
    scratch: Path,
    allow_fetch: bool,
) -> Path:
    """Export and validate the exact standalone Git object into scratch."""

    scratch.mkdir(parents=True, exist_ok=True)
    if source_repository is not None and allow_fetch:
        raise BuildError("choose either --source-repository or --allow-fetch, not both")
    if source_repository is None:
        if not allow_fetch:
            raise BuildError(
                "missing prefetched standalone source; pass --source-repository <git-repository> "
                "for offline use or explicitly enable --allow-fetch"
            )
        repository = _fetch_repository(pin, scratch)
    else:
        repository = source_repository.resolve()
    try:
        revision, tree = contract.source_identity(repository, pin.source_revision)
    except contract.ContractError as error:
        raise BuildError(str(error)) from error
    if revision != pin.source_revision:
        raise BuildError(f"standalone source revision mismatch: expected {pin.source_revision}, got {revision}")
    if tree != pin.source_tree:
        raise BuildError(f"standalone source tree mismatch: expected {pin.source_tree}, got {tree}")

    exported = scratch / "source"
    _export_git_object(repository, revision, exported)
    _validate_exported_source(exported, pin)
    (exported / ".foundry-source.json").write_bytes(
        contract.canonical_json(
            {
                "repository": pin.repository,
                "source_revision": revision,
                "source_tree": tree,
            }
        )
    )
    return exported


def _validate_export_record(exported: Path, pin: contract.RuntimePin) -> None:
    value = _read_canonical_json(exported / ".foundry-source.json", "standalone source record")
    expected = {
        "repository": pin.repository,
        "source_revision": pin.source_revision,
        "source_tree": pin.source_tree,
    }
    if value != expected:
        raise BuildError(f"standalone source record mismatch: expected {expected!r}, got {value!r}")


def derive_compatibility(
    *,
    exported: Path,
    foundry: Path,
    pin: contract.RuntimePin,
    expected_revision: str,
    expected_tree: str,
) -> dict[str, Any]:
    """Derive truthful compatibility metadata with the pinned standalone tool."""

    _validate_export_record(exported, pin)
    try:
        revision, tree, dirty = contract.foundry_identity(foundry)
    except contract.ContractError as error:
        raise BuildError(str(error)) from error
    if dirty:
        raise BuildError("runtime compatibility requires a clean Foundry checkout with no tracked changes")
    if revision != expected_revision:
        raise BuildError(f"Foundry revision changed during runtime build: expected {expected_revision}, got {revision}")
    if tree != expected_tree:
        raise BuildError(f"Foundry tree changed during runtime build: expected {expected_tree}, got {tree}")

    compatibility_path = exported / "compatibility/foundry-engine.json"
    _run_text(
        [
            sys.executable,
            str(exported / "tools/sync_engine_pin.py"),
            "derive",
            "--source",
            str(foundry),
            "--repository",
            FOUNDRY_REPOSITORY,
            "--expected-revision",
            revision,
            "--bindings-version",
            pin.bindings_version,
            "--jni-contract-version",
            str(pin.jni_contract_version),
            "--output",
            str(compatibility_path),
        ],
        cwd=exported,
        description="deriving standalone compatibility metadata",
    )
    compatibility = _read_canonical_json(
        compatibility_path,
        "derived standalone compatibility",
    )
    try:
        actual = {
            "bindings_version": compatibility["bindings"]["version"],
            "engine_repository": compatibility["engine"]["repository"],
            "engine_revision": compatibility["engine"]["revision"],
            "jni_contract_version": compatibility["jni_contract_version"],
        }
    except (KeyError, TypeError) as error:
        raise BuildError(f"derived compatibility is missing required field: {error}") from error
    expected = {
        "bindings_version": pin.bindings_version,
        "engine_repository": FOUNDRY_REPOSITORY,
        "engine_revision": revision,
        "jni_contract_version": pin.jni_contract_version,
    }
    if actual != expected:
        raise BuildError(f"derived compatibility mismatch: expected {expected!r}, got {actual!r}")
    (exported / "build-input.json").write_bytes(
        contract.canonical_json(
            {
                "bindings_version": pin.bindings_version,
                "engine_revision": revision,
                "engine_tree": tree,
                "jni_contract_version": pin.jni_contract_version,
                "standalone_revision": pin.source_revision,
                "standalone_tree": pin.source_tree,
            }
        )
    )
    return compatibility


def _validate_output_location(path: Path, foundry: Path, description: str) -> None:
    candidate = path.resolve()
    repository = foundry.resolve()
    if candidate == repository:
        raise BuildError(f"{description} must not be the Foundry repository root")
    try:
        relative = candidate.relative_to(repository)
    except ValueError:
        return
    result = subprocess.run(
        ["git", "check-ignore", "--quiet", "--", relative.as_posix()],
        cwd=repository,
        check=False,
    )
    if result.returncode != 0:
        raise BuildError(f"{description} must be outside Foundry or Git-ignored: {candidate}")


def _invoke_native_tool(
    exported: Path,
    arguments: list[str],
    description: str,
) -> None:
    _run_text(
        [sys.executable, str(exported / "tools/native_bundle.py"), *arguments],
        cwd=exported,
        description=description,
    )


def _prepare_bundle(
    *,
    exported: Path,
    compatibility_path: Path,
    scratch: Path,
    native_cells: tuple[contract.NativeCell, ...] | None,
    native_bundle: Path | None,
) -> Path:
    if (native_cells is None) == (native_bundle is None):
        raise BuildError("provide exactly one of --native-root or --native-bundle")
    if native_bundle is not None:
        bundle = native_bundle.resolve()
        if bundle.is_symlink() or not bundle.is_file():
            raise BuildError(f"prebuilt native bundle does not exist: {bundle}")
    else:
        assert native_cells is not None
        try:
            staging = scratch / "native-payload"
            if staging.exists() or staging.is_symlink():
                _remove_owned_path(staging)
            contract.stage_native_payload(native_cells, staging)
        except contract.ContractError as error:
            raise BuildError(str(error)) from error
        bundle = scratch / "foundry-native.zip"
        bundle.unlink(missing_ok=True)
        _invoke_native_tool(
            exported,
            [
                "create",
                "--compatibility",
                str(compatibility_path),
                "--input-root",
                str(staging),
                "--output",
                str(bundle),
            ],
            "creating standalone native bundle",
        )
    _invoke_native_tool(
        exported,
        [
            "validate",
            "--compatibility",
            str(compatibility_path),
            "--bundle",
            str(bundle),
        ],
        "validating standalone native bundle",
    )
    return bundle


def _run_standalone_gradle(exported: Path, bundle: Path) -> None:
    wrapper = exported / "gradlew"
    if wrapper.is_symlink() or not wrapper.is_file():
        raise BuildError(f"pinned standalone Gradle wrapper is missing: {wrapper}")
    wrapper.chmod(wrapper.stat().st_mode | 0o100)
    _run_text(
        [
            str(wrapper),
            "--no-daemon",
            f"-PfoundryNativeBundle={bundle}",
            *GRADLE_TASKS,
        ],
        cwd=exported,
        description="building and verifying standalone Android runtime",
    )


def _promote_aars(
    *,
    exported: Path,
    output: Path,
    pin: contract.RuntimePin,
) -> dict[str, Path]:
    output.parent.mkdir(parents=True, exist_ok=True)
    staging = Path(
        tempfile.mkdtemp(
            prefix=f".{output.name}-",
            dir=output.parent,
        )
    )
    try:
        for build_type in contract.OUTPUT_KEYS:
            source = exported / pin.output_path(build_type)
            if source.is_symlink() or not source.is_file():
                raise BuildError(f"standalone build did not produce {build_type} AAR: {source}")
            destination = staging / build_type / source.name
            destination.parent.mkdir(parents=True)
            shutil.copyfile(source, destination)
        if output.exists() or output.is_symlink():
            _remove_owned_path(output)
        os.replace(staging, output)
    finally:
        if staging.exists():
            shutil.rmtree(staging)
    return {
        build_type: output / build_type / Path(pin.output_path(build_type)).name for build_type in contract.OUTPUT_KEYS
    }


def prepare_runtime(
    *,
    pin_path: Path,
    engine_source: Path,
    scratch: Path,
    output_aars: Path,
    standalone_source: Path | None,
    allow_fetch: bool,
    native_root: Path | None,
    native_bundle: Path | None,
) -> BuildResult:
    """Run the complete fail-closed standalone runtime preparation."""

    engine_source = engine_source.resolve()
    scratch = scratch.resolve()
    output_aars = output_aars.resolve()
    _validate_output_location(scratch, engine_source, "runtime scratch")
    _validate_output_location(output_aars, engine_source, "runtime AAR output")
    if scratch == output_aars:
        raise BuildError("runtime scratch and AAR output must be different directories")
    pin = contract.load_pin(pin_path)
    try:
        revision, tree, dirty = contract.foundry_identity(engine_source)
    except contract.ContractError as error:
        raise BuildError(str(error)) from error
    if dirty:
        raise BuildError("runtime preparation requires a clean Foundry checkout with no tracked changes")

    scratch.mkdir(parents=True, exist_ok=True)
    exported = export_pinned_source(
        pin=pin,
        source_repository=standalone_source,
        scratch=scratch,
        allow_fetch=allow_fetch,
    )
    native_cells: tuple[contract.NativeCell, ...] | None = None
    if native_root is not None:
        try:
            native_cells = contract.validate_native_matrix(native_root.resolve(), revision, tree)
        except contract.ContractError as error:
            raise BuildError(str(error)) from error
    derive_compatibility(
        exported=exported,
        foundry=engine_source,
        pin=pin,
        expected_revision=revision,
        expected_tree=tree,
    )
    compatibility_path = exported / "compatibility/foundry-engine.json"
    bundle = _prepare_bundle(
        exported=exported,
        compatibility_path=compatibility_path,
        scratch=scratch,
        native_cells=native_cells,
        native_bundle=native_bundle,
    )
    _run_standalone_gradle(exported, bundle)
    aars = _promote_aars(exported=exported, output=output_aars, pin=pin)
    return BuildResult(bundle=bundle, aars=aars, compatibility=compatibility_path)


def _parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Build pinned Foundry Android runtime AARs from explicit inputs.")
    subparsers = parser.add_subparsers(dest="command", required=True)
    prepare = subparsers.add_parser("prepare", help="validate inputs and build all runtime AARs")
    prepare.add_argument("--pin", required=True, type=Path)
    prepare.add_argument("--engine-source", required=True, type=Path)
    prepare.add_argument("--scratch", required=True, type=Path)
    prepare.add_argument("--output-aars", required=True, type=Path)
    source = prepare.add_mutually_exclusive_group(required=True)
    source.add_argument(
        "--source-repository",
        type=Path,
        help="prefetched Git repository containing the exact pinned commit",
    )
    source.add_argument(
        "--allow-fetch",
        action="store_true",
        help="opt in to fetching only the exact standalone commit from the pin",
    )
    native = prepare.add_mutually_exclusive_group(required=True)
    native.add_argument("--native-root", type=Path)
    native.add_argument("--native-bundle", type=Path)
    return parser


def main() -> int:
    arguments = _parser().parse_args()
    try:
        result = prepare_runtime(
            pin_path=arguments.pin,
            engine_source=arguments.engine_source,
            scratch=arguments.scratch,
            output_aars=arguments.output_aars,
            standalone_source=arguments.source_repository,
            allow_fetch=arguments.allow_fetch,
            native_root=arguments.native_root,
            native_bundle=arguments.native_bundle,
        )
    except (BuildError, contract.ContractError) as error:
        print(f"Android runtime preparation failed: {error}", file=sys.stderr)
        return 2
    print(
        json.dumps(
            {
                "aars": {name: str(path) for name, path in result.aars.items()},
                "bundle": str(result.bundle),
                "compatibility": str(result.compatibility),
            },
            sort_keys=True,
        )
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
