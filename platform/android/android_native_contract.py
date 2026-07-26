"""Contracts for Foundry-owned Android native cells and JNI artifacts."""

from __future__ import annotations

import hashlib
import json
import re
import struct
import subprocess
from dataclasses import dataclass
from pathlib import Path
from typing import Any, TypedDict

SCHEMA_VERSION = 1
SHA_PATTERN = re.compile(r"^[0-9a-f]{40}$")
LIBRARY_NAMES = ("libc++_shared.so", "libfoundry_android.so")
JNI_PREFIX = "Java_games_cafecito_foundry_"
ELF_ABIS = {
    "arm64-v8a": {"elf_class": 64, "elf_machine": 183},
    "armeabi-v7a": {"elf_class": 32, "elf_machine": 40},
    "x86": {"elf_class": 32, "elf_machine": 3},
    "x86_64": {"elf_class": 64, "elf_machine": 62},
}
REQUIRED_EXTERNAL_JNI_SYMBOLS = (
    "Java_com_google_androidgamesdk_ChoreographerCallback_nOnChoreographer",
    "Java_com_google_androidgamesdk_SwappyDisplayManager_nSetSupportedRefreshPeriods",
    "Java_com_google_androidgamesdk_SwappyDisplayManager_nOnRefreshPeriodChanged",
)


class ContractError(RuntimeError):
    """A malformed or incompatible Foundry-owned Android native input."""


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


@dataclass(frozen=True)
class ElfInfo:
    elf_class: int
    machine: int
    exported_symbols: tuple[str, ...]


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
        raise ContractError(f"Foundry source repository does not exist: {repository}")
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


def _bounded_slice(contents: bytes, offset: int, size: int, description: str) -> bytes:
    if offset < 0 or size < 0 or offset + size > len(contents):
        raise ContractError(f"invalid ELF {description} bounds")
    return contents[offset : offset + size]


def _unpack_from(format_string: str, contents: bytes, offset: int, description: str) -> tuple[Any, ...]:
    size = struct.calcsize(format_string)
    return struct.unpack(format_string, _bounded_slice(contents, offset, size, description))


def _string_at(table: bytes, offset: int) -> str:
    if offset < 0 or offset >= len(table):
        raise ContractError("invalid ELF dynamic string offset")
    end = table.find(b"\0", offset)
    if end < 0:
        raise ContractError("unterminated ELF dynamic string")
    try:
        return table[offset:end].decode("utf-8")
    except UnicodeDecodeError as error:
        raise ContractError("ELF dynamic symbol name is not UTF-8") from error


def read_elf(contents: bytes, description: str) -> ElfInfo:
    """Read the ABI and exported dynamic symbols from one little-endian ELF."""

    if len(contents) < 16 or contents[:4] != b"\x7fELF":
        raise ContractError(f"{description} is not an ELF file")
    class_byte = contents[4]
    data_byte = contents[5]
    if class_byte not in (1, 2):
        raise ContractError(f"{description} has unsupported ELF class byte {class_byte}")
    if data_byte != 1:
        raise ContractError(f"{description} must be a little-endian ELF file")
    elf_class = 32 if class_byte == 1 else 64

    if elf_class == 64:
        header_format = "<HHIQQQIHHHHHH"
        section_format = "<IIQQQQIIQQ"
        symbol_format = "<IBBHQQ"
    else:
        header_format = "<HHIIIIIHHHHHH"
        section_format = "<IIIIIIIIII"
        symbol_format = "<IIIBBH"
    header = _unpack_from(header_format, contents, 16, f"header for {description}")
    machine = int(header[1])
    section_offset = int(header[5])
    section_entry_size = int(header[10])
    section_count = int(header[11])
    expected_section_entry_size = struct.calcsize(section_format)
    if section_offset == 0 or section_count == 0:
        raise ContractError(f"{description} has no ELF section table")
    if section_entry_size != expected_section_entry_size:
        raise ContractError(f"{description} has unexpected ELF section entry size {section_entry_size}")
    _bounded_slice(
        contents,
        section_offset,
        section_entry_size * section_count,
        f"section table for {description}",
    )

    sections = [
        _unpack_from(
            section_format,
            contents,
            section_offset + index * section_entry_size,
            f"section {index} for {description}",
        )
        for index in range(section_count)
    ]
    dynamic_symbol_sections = [section for section in sections if int(section[1]) == 11]
    if not dynamic_symbol_sections:
        raise ContractError(f"{description} has no ELF dynamic symbol table")

    exported: set[str] = set()
    expected_symbol_size = struct.calcsize(symbol_format)
    for section in dynamic_symbol_sections:
        symbol_offset = int(section[4])
        symbol_size = int(section[5])
        string_table_index = int(section[6])
        symbol_entry_size = int(section[9])
        if not 0 <= string_table_index < len(sections):
            raise ContractError(f"{description} has an invalid ELF dynamic string-table link")
        if symbol_entry_size != expected_symbol_size or symbol_size % symbol_entry_size:
            raise ContractError(f"{description} has an invalid ELF dynamic symbol entry size")
        string_section = sections[string_table_index]
        string_table = _bounded_slice(
            contents,
            int(string_section[4]),
            int(string_section[5]),
            f"dynamic string table for {description}",
        )
        symbol_table = _bounded_slice(
            contents,
            symbol_offset,
            symbol_size,
            f"dynamic symbol table for {description}",
        )
        for offset in range(0, len(symbol_table), symbol_entry_size):
            symbol = struct.unpack(symbol_format, symbol_table[offset : offset + symbol_entry_size])
            if elf_class == 64:
                name_offset, info, other, section_index = (
                    int(symbol[0]),
                    int(symbol[1]),
                    int(symbol[2]),
                    int(symbol[3]),
                )
            else:
                name_offset, info, other, section_index = (
                    int(symbol[0]),
                    int(symbol[3]),
                    int(symbol[4]),
                    int(symbol[5]),
                )
            if name_offset == 0 or section_index == 0:
                continue
            binding = info >> 4
            visibility = other & 0x03
            if binding not in (1, 2) or visibility not in (0, 3):
                continue
            name = _string_at(string_table, name_offset)
            if name:
                exported.add(name)
    return ElfInfo(elf_class=elf_class, machine=machine, exported_symbols=tuple(sorted(exported)))


def _inspect_native_artifacts(cells: tuple[NativeCell, ...]) -> None:
    foundry_surfaces: set[tuple[str, ...]] = set()
    required_external = set(REQUIRED_EXTERNAL_JNI_SYMBOLS)
    for cell in cells:
        abi_specification = ELF_ABIS[cell.abi]
        for library in cell.libraries:
            description = f"{cell.build_type}/{cell.abi}/{library.name}"
            elf = read_elf(library.read_bytes(), description)
            expected_class = abi_specification["elf_class"]
            expected_machine = abi_specification["elf_machine"]
            if elf.elf_class != expected_class:
                raise ContractError(
                    f"ELF class mismatch for {description}: expected {expected_class}, found {elf.elf_class}"
                )
            if elf.machine != expected_machine:
                raise ContractError(
                    f"ELF machine mismatch for {description}: expected {expected_machine}, found {elf.machine}"
                )

            jni_symbols = tuple(symbol for symbol in elf.exported_symbols if symbol.startswith("Java_"))
            if library.name == "libc++_shared.so":
                if jni_symbols:
                    raise ContractError(f"unexpected JNI symbol in {description}: {jni_symbols[0]}")
                continue

            foundry_symbols = tuple(symbol for symbol in jni_symbols if symbol.startswith(JNI_PREFIX))
            external_symbols = set(jni_symbols) - set(foundry_symbols)
            extra_external = sorted(external_symbols - required_external)
            if extra_external:
                if extra_external[0].startswith("Java_org_godotengine_"):
                    raise ContractError(f"stale JNI symbol in {description}: {extra_external[0]}")
                raise ContractError(f"unallowlisted external JNI symbol in {description}: {extra_external[0]}")
            missing_external = sorted(required_external - external_symbols)
            if missing_external:
                raise ContractError(f"missing required external JNI symbol in {description}: {missing_external[0]}")
            if not foundry_symbols:
                raise ContractError(f"{description} exports no Foundry JNI symbols")
            foundry_surfaces.add(foundry_symbols)

    if len(foundry_surfaces) != 1:
        raise ContractError("libfoundry_android.so JNI surface mismatch across the native matrix")


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
        raise ContractError(f"native matrix mismatch: missing cell {missing[0][0]}/{missing[0][1]}")
    unexpected = sorted(actual_pairs - expected_pairs)
    if unexpected:
        raise ContractError(f"native matrix mismatch: unexpected cell {unexpected[0][0]}/{unexpected[0][1]}")

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
    validated = tuple(cells)
    _inspect_native_artifacts(validated)
    return validated
