"""Contracts shared by Foundry's Android native producer and runtime consumer."""

from __future__ import annotations

import hashlib
import json
import re
import shutil
import subprocess
from dataclasses import dataclass
from pathlib import Path
from typing import Any, TypedDict

SCHEMA_VERSION = 1
ENGINE_COMPATIBILITY_POLICY = "exact-native-source-revision"
SHA_PATTERN = re.compile(r"^[0-9a-f]{40}$")
LIBRARY_NAMES = ("libc++_shared.so", "libfoundry_android.so")
REQUIRED_SOURCE_PATHS = (
    "compatibility/foundry-engine.json",
    "gradlew",
    "runtime/build.gradle",
    "tools/native_bundle.py",
    "tools/sync_engine_pin.py",
    "tools/verify_jni_contract.py",
)
OUTPUT_KEYS = ("debug", "dev", "release")


class ContractError(RuntimeError):
    """A malformed or incompatible Android runtime build input."""


@dataclass(frozen=True)
class RuntimePin:
    repository: str
    source_revision: str
    source_tree: str
    bindings_version: str
    jni_contract_version: int
    engine_compatibility_policy: str
    required_paths: tuple[str, ...]
    outputs: tuple[tuple[str, str], ...]

    def output_path(self, build_type: str) -> str:
        for name, path in self.outputs:
            if name == build_type:
                return path
        raise ContractError(f"runtime pin has no output for build type: {build_type}")


@dataclass(frozen=True)
class BuildSpec:
    build_type: str
    abi: str
    arch: str
    target: str
    production: bool
    dev_mode: bool
    dev_build: bool
    debug_symbols: bool
    tests: bool = False
    swappy: bool = True

    def build_json(self) -> dict[str, object]:
        return {
            "abi": self.abi,
            "arch": self.arch,
            "build_type": self.build_type,
            "debug_symbols": self.debug_symbols,
            "dev_build": self.dev_build,
            "dev_mode": self.dev_mode,
            "production": self.production,
            "swappy": self.swappy,
            "target": self.target,
            "tests": self.tests,
        }


@dataclass(frozen=True)
class NativeCell:
    build_type: str
    abi: str
    arch: str
    directory: Path
    libraries: tuple[Path, Path]


class BuildTypeOptions(TypedDict):
    target: str
    production: bool
    dev_mode: bool
    dev_build: bool
    debug_symbols: bool


BUILD_TYPES: dict[str, BuildTypeOptions] = {
    "debug": {
        "debug_symbols": False,
        "dev_build": False,
        "dev_mode": False,
        "production": False,
        "target": "template_debug",
    },
    "dev": {
        "debug_symbols": True,
        "dev_build": True,
        "dev_mode": True,
        "production": False,
        "target": "template_debug",
    },
    "release": {
        "debug_symbols": False,
        "dev_build": False,
        "dev_mode": False,
        "production": True,
        "target": "template_release",
    },
}
ABIS = {
    "arm64-v8a": "arm64",
    "armeabi-v7a": "arm32",
    "x86": "x86_32",
    "x86_64": "x86_64",
}
MATRIX = tuple(
    BuildSpec(build_type=build_type, abi=abi, arch=arch, **BUILD_TYPES[build_type])
    for build_type in sorted(BUILD_TYPES)
    for abi, arch in sorted(ABIS.items())
)


def canonical_json(value: object) -> bytes:
    """Return the one accepted JSON encoding for tracked/build contracts."""

    return (json.dumps(value, sort_keys=True, separators=(",", ":")) + "\n").encode()


def _exact_keys(value: object, expected: set[str], description: str) -> dict[str, Any]:
    if not isinstance(value, dict):
        raise ContractError(f"{description} must be an object")
    actual = set(value)
    missing = sorted(expected - actual)
    if missing:
        raise ContractError(f"{description} is missing required field: {missing[0]}")
    unexpected = sorted(actual - expected)
    if unexpected:
        raise ContractError(f"{description} has unexpected field: {unexpected[0]}")
    return value


def _sha(value: object, description: str) -> str:
    if not isinstance(value, str) or SHA_PATTERN.fullmatch(value) is None:
        raise ContractError(f"{description} must be a lowercase 40-character Git SHA")
    return value


def _safe_relative_path(value: object, description: str) -> str:
    if not isinstance(value, str) or not value:
        raise ContractError(f"{description} must be a non-empty relative path")
    path = Path(value)
    if path.is_absolute() or ".." in path.parts or path.as_posix() != value:
        raise ContractError(f"{description} must be a normalized relative path")
    return value


def load_pin(path: Path) -> RuntimePin:
    """Load and strictly validate the tracked standalone-source pin."""

    if not path.is_file():
        raise ContractError(f"runtime pin does not exist: {path}")
    raw = path.read_bytes()
    try:
        value = json.loads(raw)
    except (UnicodeDecodeError, json.JSONDecodeError) as error:
        raise ContractError(f"unable to parse runtime pin {path}: {error}") from error
    if raw != canonical_json(value):
        raise ContractError(f"runtime pin is not canonical JSON: {path}")

    root = _exact_keys(
        value,
        {
            "bindings",
            "engine_compatibility",
            "jni_contract_version",
            "outputs",
            "required_paths",
            "schema_version",
            "source",
        },
        "runtime pin",
    )
    if root["schema_version"] != SCHEMA_VERSION:
        raise ContractError(f"unsupported runtime pin schema: {root['schema_version']!r}")

    source = _exact_keys(root["source"], {"repository", "revision", "tree"}, "runtime pin source")
    repository = source["repository"]
    if not isinstance(repository, str) or not repository.startswith("https://") or not repository.endswith(".git"):
        raise ContractError("runtime pin source.repository must be an HTTPS Git URL")
    revision = _sha(source["revision"], "runtime pin source.revision")
    tree = _sha(source["tree"], "runtime pin source.tree")

    bindings = _exact_keys(root["bindings"], {"version"}, "runtime pin bindings")
    bindings_version = bindings["version"]
    if not isinstance(bindings_version, str) or not bindings_version:
        raise ContractError("runtime pin bindings.version must be a non-empty string")

    contract_version = root["jni_contract_version"]
    if not isinstance(contract_version, int) or isinstance(contract_version, bool) or contract_version < 1:
        raise ContractError("runtime pin JNI contract must be a positive integer")

    compatibility = _exact_keys(
        root["engine_compatibility"],
        {"policy"},
        "runtime pin engine compatibility",
    )
    policy = compatibility["policy"]
    if policy != ENGINE_COMPATIBILITY_POLICY:
        raise ContractError(
            f"runtime pin engine compatibility policy must be {ENGINE_COMPATIBILITY_POLICY!r}, got {policy!r}"
        )

    required_paths_value = root["required_paths"]
    if not isinstance(required_paths_value, list):
        raise ContractError("runtime pin required_paths must be an array")
    required_paths = tuple(_safe_relative_path(value, "runtime pin required path") for value in required_paths_value)
    if required_paths != REQUIRED_SOURCE_PATHS:
        raise ContractError("runtime pin required_paths must be the sorted authoritative standalone tool paths")

    outputs_value = _exact_keys(root["outputs"], set(OUTPUT_KEYS), "runtime pin outputs")
    outputs = tuple(
        (build_type, _safe_relative_path(outputs_value[build_type], f"runtime pin output {build_type}"))
        for build_type in OUTPUT_KEYS
    )
    return RuntimePin(
        repository=repository,
        source_revision=revision,
        source_tree=tree,
        bindings_version=bindings_version,
        jni_contract_version=contract_version,
        engine_compatibility_policy=policy,
        required_paths=required_paths,
        outputs=outputs,
    )


def _git(repository: Path, *arguments: str) -> str:
    try:
        result = subprocess.run(
            ["git", *arguments],
            cwd=repository,
            check=True,
            capture_output=True,
            text=True,
        )
    except (OSError, subprocess.CalledProcessError) as error:
        detail = getattr(error, "stderr", "") or str(error)
        raise ContractError(f"unable to inspect Git repository {repository}: {detail.strip()}") from error
    return result.stdout.strip()


def source_identity(repository: Path, revision: str) -> tuple[str, str]:
    """Resolve an exact commit and its tree in a Git repository."""

    if not repository.is_dir():
        raise ContractError(f"standalone source repository does not exist: {repository}")
    commit = _git(repository, "rev-parse", "--verify", f"{revision}^{{commit}}")
    tree = _git(repository, "rev-parse", "--verify", f"{commit}^{{tree}}")
    return _sha(commit, "resolved source revision"), _sha(tree, "resolved source tree")


def foundry_identity(repository: Path) -> tuple[str, str, bool]:
    """Return Foundry HEAD/tree plus tracked-dirty state."""

    revision, tree = source_identity(repository, "HEAD")
    dirty = bool(_git(repository, "status", "--porcelain", "--untracked-files=no"))
    return revision, tree, dirty


def _hash_file(path: Path) -> tuple[int, str]:
    digest = hashlib.sha256()
    size = 0
    with path.open("rb") as file:
        while contents := file.read(1024 * 1024):
            size += len(contents)
            digest.update(contents)
    return size, digest.hexdigest()


def build_spec(build_type: str, abi: str) -> BuildSpec:
    for specification in MATRIX:
        if specification.build_type == build_type and specification.abi == abi:
            return specification
    raise ContractError(f"unsupported Android native cell: {build_type}/{abi}")


def create_native_provenance(
    *,
    revision: str,
    tree: str,
    dirty: bool,
    build_type: str,
    abi: str,
    library_directory: Path,
) -> dict[str, object]:
    """Create validated canonical provenance content for one native cell."""

    specification = build_spec(build_type, abi)
    records: list[dict[str, object]] = []
    for name in LIBRARY_NAMES:
        path = library_directory / name
        if path.is_symlink():
            raise ContractError(f"native library must not be a symbolic link: {path}")
        if not path.is_file():
            raise ContractError(f"native library does not exist: {path}")
        size, digest = _hash_file(path)
        records.append({"path": name, "sha256": digest, "size": size})
    value = {
        "build": specification.build_json(),
        "engine": {
            "dirty": bool(dirty),
            "revision": _sha(revision, "native provenance engine revision"),
            "tree": _sha(tree, "native provenance engine tree"),
        },
        "libraries": records,
        "schema_version": SCHEMA_VERSION,
    }
    _validate_provenance_value(
        value,
        specification=specification,
        revision=revision,
        tree=tree,
        directory=library_directory,
        allow_dirty=True,
    )
    return value


def write_native_provenance(
    path: Path,
    *,
    revision: str,
    tree: str,
    dirty: bool,
    build_type: str,
    abi: str,
    library_directory: Path,
) -> None:
    value = create_native_provenance(
        revision=revision,
        tree=tree,
        dirty=dirty,
        build_type=build_type,
        abi=abi,
        library_directory=library_directory,
    )
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(canonical_json(value))


def _validate_library_record(
    value: object,
    *,
    expected_name: str,
    directory: Path,
) -> None:
    record = _exact_keys(value, {"path", "sha256", "size"}, "native provenance library")
    if record["path"] != expected_name:
        raise ContractError(
            f"native provenance library path mismatch: expected {expected_name}, got {record['path']!r}"
        )
    path = directory / expected_name
    if path.is_symlink():
        raise ContractError(f"native cell contains symbolic link: {path}")
    if not path.is_file():
        raise ContractError(f"native cell is missing library: {path}")
    size, digest = _hash_file(path)
    if record["size"] != size:
        raise ContractError(f"native library size mismatch for {path}")
    if record["sha256"] != digest:
        raise ContractError(f"native library SHA-256 mismatch for {path}")


def _validate_provenance_value(
    value: object,
    *,
    specification: BuildSpec,
    revision: str,
    tree: str,
    directory: Path,
    allow_dirty: bool = False,
) -> None:
    root = _exact_keys(
        value,
        {"build", "engine", "libraries", "schema_version"},
        "native provenance",
    )
    if root["schema_version"] != SCHEMA_VERSION:
        raise ContractError(f"unsupported native provenance schema: {root['schema_version']!r}")
    engine = _exact_keys(root["engine"], {"dirty", "revision", "tree"}, "native provenance engine")
    if not isinstance(engine["dirty"], bool):
        raise ContractError(f"native provenance dirty state must be a boolean in {directory}")
    if engine["dirty"] and not allow_dirty:
        raise ContractError(f"native cell was built from dirty Foundry source: {directory}")
    if engine["revision"] != revision:
        raise ContractError(
            f"native cell engine revision mismatch in {directory}: expected {revision}, got {engine['revision']!r}"
        )
    if engine["tree"] != tree:
        raise ContractError(f"native cell engine tree mismatch in {directory}: expected {tree}, got {engine['tree']!r}")

    build = _exact_keys(
        root["build"],
        set(specification.build_json()),
        "native provenance build",
    )
    if build.get("arch") != specification.arch:
        raise ContractError(
            f"native cell ABI mapping mismatch in {directory}: {build.get('abi')!r}/{build.get('arch')!r}"
        )
    if build != specification.build_json():
        raise ContractError(f"native cell build configuration mismatch in {directory}")

    libraries = root["libraries"]
    if not isinstance(libraries, list) or len(libraries) != len(LIBRARY_NAMES):
        raise ContractError(f"native provenance libraries mismatch in {directory}")
    for record, name in zip(libraries, LIBRARY_NAMES):
        _validate_library_record(record, expected_name=name, directory=directory)


def _actual_cell_pairs(root: Path) -> set[tuple[str, str]]:
    pairs: set[tuple[str, str]] = set()
    if not root.is_dir():
        return pairs
    for build_path in root.iterdir():
        if build_path.is_symlink() or not build_path.is_dir():
            raise ContractError(f"unexpected native cell path: {build_path}")
        for abi_path in build_path.iterdir():
            if abi_path.is_symlink() or not abi_path.is_dir():
                raise ContractError(f"unexpected native cell path: {abi_path}")
            pairs.add((build_path.name, abi_path.name))
    return pairs


def validate_native_matrix(root: Path, revision: str, tree: str) -> tuple[NativeCell, ...]:
    """Validate exactly one complete 3x4 native matrix and its provenance."""

    _sha(revision, "expected Foundry revision")
    _sha(tree, "expected Foundry tree")
    expected_pairs = {(specification.build_type, specification.abi) for specification in MATRIX}
    actual_pairs = _actual_cell_pairs(root)
    missing = sorted(expected_pairs - actual_pairs)
    if missing:
        raise ContractError(f"missing native cell: {missing[0][0]}/{missing[0][1]}")
    unexpected = sorted(actual_pairs - expected_pairs)
    if unexpected:
        raise ContractError(f"unexpected native cell: {unexpected[0][0]}/{unexpected[0][1]}")

    return validate_native_cells(
        root,
        revision=revision,
        tree=tree,
        pairs=tuple((specification.build_type, specification.abi) for specification in MATRIX),
    )


def validate_native_cells(
    root: Path,
    *,
    revision: str,
    tree: str,
    pairs: tuple[tuple[str, str], ...],
) -> tuple[NativeCell, ...]:
    """Validate a selected set of native cells after its producer has finished."""

    _sha(revision, "expected Foundry revision")
    _sha(tree, "expected Foundry tree")
    if not pairs:
        raise ContractError("at least one Android native cell is required")
    if len(set(pairs)) != len(pairs):
        raise ContractError("Android native cell selection contains duplicates")
    specifications = tuple(build_spec(build_type, abi) for build_type, abi in pairs)
    cells: list[NativeCell] = []
    expected_names = {*LIBRARY_NAMES, "provenance.json"}
    for specification in specifications:
        directory = root / specification.build_type / specification.abi
        if directory.is_symlink() or not directory.is_dir():
            raise ContractError(f"native cell does not exist: {directory}")
        actual_names = {path.name for path in directory.iterdir()}
        extra_names = sorted(actual_names - expected_names)
        if extra_names:
            raise ContractError(f"unexpected native cell file in {directory}: {extra_names[0]}")
        missing_names = sorted(expected_names - actual_names)
        if missing_names:
            raise ContractError(f"native cell is missing file in {directory}: {missing_names[0]}")
        provenance_path = directory / "provenance.json"
        if provenance_path.is_symlink():
            raise ContractError(f"native cell contains symbolic link: {provenance_path}")
        raw = provenance_path.read_bytes()
        try:
            value = json.loads(raw)
        except (UnicodeDecodeError, json.JSONDecodeError) as error:
            raise ContractError(f"unable to parse native provenance {provenance_path}: {error}") from error
        if raw != canonical_json(value):
            raise ContractError(f"native provenance is not canonical JSON: {provenance_path}")
        _validate_provenance_value(
            value,
            specification=specification,
            revision=revision,
            tree=tree,
            directory=directory,
        )
        libraries = (directory / LIBRARY_NAMES[0], directory / LIBRARY_NAMES[1])
        cells.append(
            NativeCell(
                build_type=specification.build_type,
                abi=specification.abi,
                arch=specification.arch,
                directory=directory,
                libraries=libraries,
            )
        )
    return tuple(cells)


def stage_native_payload(cells: tuple[NativeCell, ...], output: Path) -> None:
    """Copy validated payload files into the standalone native-bundle layout."""

    if output.exists():
        raise ContractError(f"native staging output already exists: {output}")
    output.mkdir(parents=True)
    for cell in cells:
        directory = output / cell.build_type / cell.abi
        directory.mkdir(parents=True)
        for library in cell.libraries:
            if library.is_symlink() or not library.is_file():
                raise ContractError(f"validated native library changed before staging: {library}")
            shutil.copyfile(library, directory / library.name)
