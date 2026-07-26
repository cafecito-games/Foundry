#!/usr/bin/env python3
"""Create, validate, and extract deterministic Foundry Android native bundles."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import shutil
import stat
import struct
import sys
import tempfile
import zipfile
from dataclasses import dataclass
from pathlib import Path, PurePosixPath
from typing import Any

BUNDLE_FORMAT = "foundry-android-native-bundle"
BUNDLE_SCHEMA_VERSION = 1
MANIFEST_PATH = "manifest.json"
CANONICAL_TIMESTAMP = (1980, 1, 1, 0, 0, 0)
CANONICAL_FILE_MODE = (stat.S_IFREG | 0o644) << 16
JNI_PREFIX = "Java_games_cafecito_foundry_"


class BundleError(Exception):
    """A native-bundle contract failure."""


@dataclass(frozen=True)
class ElfInfo:
    elf_class: int
    machine: int
    exported_symbols: tuple[str, ...]


@dataclass(frozen=True)
class ValidatedBundle:
    manifest: dict[str, Any]
    compatibility: dict[str, Any]


def canonical_json(value: Any) -> bytes:
    return (json.dumps(value, sort_keys=True, separators=(",", ":"), ensure_ascii=True) + "\n").encode()


def sha256(contents: bytes) -> str:
    return hashlib.sha256(contents).hexdigest()


def symbol_digest(symbols: tuple[str, ...] | list[str]) -> str:
    contents = ("\n".join(symbols) + ("\n" if symbols else "")).encode()
    return sha256(contents)


def read_canonical_json(path: Path, description: str) -> dict[str, Any]:
    if not path.is_file():
        raise BundleError(f"{description} does not exist: {path}")
    try:
        raw = path.read_bytes()
        value = json.loads(raw)
    except (OSError, json.JSONDecodeError) as error:
        raise BundleError(f"unable to read {description}: {error}") from error
    if not isinstance(value, dict):
        raise BundleError(f"{description} root must be an object")
    if raw != canonical_json(value):
        raise BundleError(f"{description} is not canonical JSON")
    return value


def validate_compatibility_shape(compatibility: dict[str, Any]) -> None:
    try:
        schema_version = compatibility["schema_version"]
        bindings_version = compatibility["bindings"]["version"]
        engine = compatibility["engine"]
        repository = engine["repository"]
        revision = engine["revision"]
        engine_version = engine["version"]
        jni_contract_version = compatibility["jni_contract_version"]
        native = compatibility["native"]
        build_types = native["build_types"]
        abis = native["abis"]
        external_jni_allowlist = native["external_jni_allowlist"]
        libraries = native["libraries"]
    except (KeyError, TypeError) as error:
        raise BundleError(f"compatibility document is missing required field: {error}") from error

    if schema_version != 1:
        raise BundleError(f"unsupported compatibility schema: {schema_version}")
    if not isinstance(bindings_version, str) or not bindings_version:
        raise BundleError("compatibility bindings.version must be a non-empty string")
    if not isinstance(repository, str) or not repository.startswith("https://"):
        raise BundleError("compatibility engine.repository must be an HTTPS URL")
    if (
        not isinstance(revision, str)
        or len(revision) != 40
        or any(character not in "0123456789abcdef" for character in revision)
    ):
        raise BundleError("compatibility engine.revision must be a lowercase 40-character Git SHA")
    if not isinstance(engine_version, str) or not engine_version:
        raise BundleError("compatibility engine.version must be a non-empty string")
    if not isinstance(jni_contract_version, int) or jni_contract_version < 1:
        raise BundleError("compatibility JNI contract must be a positive integer")
    if not isinstance(build_types, list) or not build_types or build_types != sorted(set(build_types)):
        raise BundleError("compatibility native.build_types must be a sorted unique list")
    if not isinstance(abis, dict) or not abis:
        raise BundleError("compatibility native.abis must be a non-empty object")
    if list(abis) != sorted(abis):
        raise BundleError("compatibility native.abis keys must be sorted")
    for abi, specification in abis.items():
        if (
            not isinstance(abi, str)
            or not isinstance(specification, dict)
            or set(specification) != {"elf_class", "elf_machine"}
            or specification["elf_class"] not in (32, 64)
            or not isinstance(specification["elf_machine"], int)
        ):
            raise BundleError(f"invalid ELF compatibility specification for ABI {abi!r}")
    if not isinstance(libraries, list) or libraries != [
        "libfoundry_android.so",
        "libc++_shared.so",
    ]:
        raise BundleError(
            "compatibility native.libraries must contain exactly "
            "libfoundry_android.so and libc++_shared.so in canonical contract order"
        )
    if not isinstance(external_jni_allowlist, list) or not external_jni_allowlist:
        raise BundleError("compatibility native.external_jni_allowlist must be a non-empty list")
    external_symbols: list[str] = []
    for entry in external_jni_allowlist:
        if not isinstance(entry, dict) or set(entry) != {
            "component",
            "owner",
            "symbol",
        }:
            raise BundleError(
                "compatibility native.external_jni_allowlist entries must contain exactly component, owner, and symbol"
            )
        component = entry["component"]
        owner = entry["owner"]
        symbol = entry["symbol"]
        if not isinstance(component, str) or not component:
            raise BundleError("external JNI allowlist component must be a non-empty string")
        if not isinstance(owner, str) or not owner:
            raise BundleError("external JNI allowlist owner must be a non-empty string")
        if not isinstance(symbol, str) or not symbol.startswith("Java_") or symbol.startswith(JNI_PREFIX):
            raise BundleError(f"invalid external JNI allowlist symbol: {symbol!r}")
        external_symbols.append(symbol)
    if external_symbols != sorted(set(external_symbols)):
        raise BundleError("compatibility native.external_jni_allowlist must be sorted by unique symbol")


def required_external_jni_symbols(compatibility: dict[str, Any]) -> tuple[str, ...]:
    return tuple(entry["symbol"] for entry in compatibility["native"]["external_jni_allowlist"])


def expected_payload_paths(compatibility: dict[str, Any]) -> tuple[str, ...]:
    native = compatibility["native"]
    return tuple(
        f"{build_type}/{abi}/{library}"
        for build_type in native["build_types"]
        for abi in sorted(native["abis"])
        for library in native["libraries"]
    )


def expected_matrix(compatibility: dict[str, Any]) -> list[dict[str, str]]:
    native = compatibility["native"]
    return [
        {"abi": abi, "build_type": build_type} for build_type in native["build_types"] for abi in sorted(native["abis"])
    ]


def _bounded_slice(contents: bytes, offset: int, size: int, description: str) -> bytes:
    if offset < 0 or size < 0 or offset + size > len(contents):
        raise BundleError(f"invalid ELF {description} bounds")
    return contents[offset : offset + size]


def _unpack_from(format_string: str, contents: bytes, offset: int, description: str) -> tuple[Any, ...]:
    size = struct.calcsize(format_string)
    chunk = _bounded_slice(contents, offset, size, description)
    return struct.unpack(format_string, chunk)


def _string_at(table: bytes, offset: int) -> str:
    if offset < 0 or offset >= len(table):
        raise BundleError("invalid ELF dynamic string offset")
    end = table.find(b"\0", offset)
    if end < 0:
        raise BundleError("unterminated ELF dynamic string")
    try:
        return table[offset:end].decode("utf-8")
    except UnicodeDecodeError as error:
        raise BundleError("ELF dynamic symbol name is not UTF-8") from error


def read_elf(contents: bytes, description: str) -> ElfInfo:
    if len(contents) < 16 or contents[:4] != b"\x7fELF":
        raise BundleError(f"{description} is not an ELF file")
    class_byte = contents[4]
    data_byte = contents[5]
    if class_byte not in (1, 2):
        raise BundleError(f"{description} has unsupported ELF class byte {class_byte}")
    if data_byte != 1:
        raise BundleError(f"{description} must be a little-endian ELF file")
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
        raise BundleError(f"{description} has no ELF section table")
    if section_entry_size != expected_section_entry_size:
        raise BundleError(f"{description} has unexpected ELF section entry size {section_entry_size}")
    _bounded_slice(
        contents,
        section_offset,
        section_entry_size * section_count,
        f"section table for {description}",
    )

    sections: list[tuple[Any, ...]] = []
    for index in range(section_count):
        sections.append(
            _unpack_from(
                section_format,
                contents,
                section_offset + index * section_entry_size,
                f"section {index} for {description}",
            )
        )

    dynamic_symbol_sections = [section for section in sections if int(section[1]) == 11]
    if not dynamic_symbol_sections:
        raise BundleError(f"{description} has no ELF dynamic symbol table")

    exported: set[str] = set()
    expected_symbol_size = struct.calcsize(symbol_format)
    for section in dynamic_symbol_sections:
        symbol_offset = int(section[4])
        symbol_size = int(section[5])
        string_table_index = int(section[6])
        symbol_entry_size = int(section[9])
        if not 0 <= string_table_index < len(sections):
            raise BundleError(f"{description} has an invalid ELF dynamic string-table link")
        if symbol_entry_size != expected_symbol_size or symbol_size % symbol_entry_size:
            raise BundleError(f"{description} has an invalid ELF dynamic symbol entry size")
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


def _abi_for_path(path: str) -> str:
    parts = path.split("/")
    if len(parts) != 3:
        raise BundleError(f"invalid native payload path: {path}")
    return parts[1]


def inspect_payload(
    *,
    path: str,
    contents: bytes,
    compatibility: dict[str, Any],
) -> dict[str, Any]:
    abi = _abi_for_path(path)
    abi_specification = compatibility["native"]["abis"][abi]
    elf = read_elf(contents, path)
    expected_class = abi_specification["elf_class"]
    expected_machine = abi_specification["elf_machine"]
    if elf.elf_class != expected_class:
        raise BundleError(f"ELF class mismatch for {path}: expected {expected_class}, found {elf.elf_class}")
    if elf.machine != expected_machine:
        raise BundleError(f"ELF machine mismatch for {path}: expected {expected_machine}, found {elf.machine}")

    jni_symbols = tuple(symbol for symbol in elf.exported_symbols if symbol.startswith("Java_"))
    library = path.rsplit("/", 1)[1]
    if library == "libfoundry_android.so":
        foundry_symbols = tuple(symbol for symbol in jni_symbols if symbol.startswith(JNI_PREFIX))
        external_symbols = tuple(symbol for symbol in jni_symbols if not symbol.startswith(JNI_PREFIX))
        required_external = required_external_jni_symbols(compatibility)
        extra_external = sorted(set(external_symbols) - set(required_external))
        if extra_external:
            if extra_external[0].startswith("Java_org_godotengine_"):
                raise BundleError(f"stale JNI symbol in {path}: {extra_external[0]}")
            raise BundleError(f"unallowlisted external JNI symbol in {path}: {extra_external[0]}")
        missing_external = sorted(set(required_external) - set(external_symbols))
        if missing_external:
            raise BundleError(f"missing required external JNI symbol in {path}: {missing_external[0]}")
        if not foundry_symbols:
            raise BundleError(f"{path} exports no Foundry JNI symbols")
    elif jni_symbols:
        raise BundleError(f"unexpected JNI symbol in {path}: {jni_symbols[0]}")

    return {
        "elf_class": elf.elf_class,
        "elf_machine": elf.machine,
        "jni_symbols": list(jni_symbols),
        "jni_symbols_sha256": symbol_digest(jni_symbols),
        "path": path,
        "sha256": sha256(contents),
        "size": len(contents),
    }


def verify_consistent_jni_surface(file_records: list[dict[str, Any]]) -> None:
    symbol_sets = {
        tuple(record["jni_symbols"]) for record in file_records if record["path"].endswith("/libfoundry_android.so")
    }
    if len(symbol_sets) != 1:
        raise BundleError("libfoundry_android.so JNI symbol surface differs across the native matrix")


def _canonical_zip_info(path: str) -> zipfile.ZipInfo:
    info = zipfile.ZipInfo(path, CANONICAL_TIMESTAMP)
    info.compress_type = zipfile.ZIP_DEFLATED
    info.create_system = 3
    info.external_attr = CANONICAL_FILE_MODE
    info.flag_bits = 0x800
    return info


def _write_bundle_zip(
    output: Path,
    manifest: dict[str, Any],
    payloads: dict[str, bytes],
) -> None:
    output.parent.mkdir(parents=True, exist_ok=True)
    descriptor, temporary_name = tempfile.mkstemp(prefix=f".{output.name}.", suffix=".tmp", dir=output.parent)
    os.close(descriptor)
    temporary_path = Path(temporary_name)
    try:
        with zipfile.ZipFile(
            temporary_path,
            mode="w",
            compression=zipfile.ZIP_DEFLATED,
            compresslevel=9,
            strict_timestamps=True,
        ) as archive:
            archive.comment = b""
            archive.writestr(_canonical_zip_info(MANIFEST_PATH), canonical_json(manifest))
            for path in sorted(payloads):
                archive.writestr(_canonical_zip_info(path), payloads[path])
        os.replace(temporary_path, output)
    finally:
        temporary_path.unlink(missing_ok=True)


def create_bundle(compatibility_path: Path, input_root: Path, output: Path) -> None:
    compatibility = read_canonical_json(compatibility_path, "compatibility document")
    validate_compatibility_shape(compatibility)
    if not input_root.is_dir():
        raise BundleError(f"native input root does not exist: {input_root}")

    expected_paths = set(expected_payload_paths(compatibility))
    actual_paths: set[str] = set()
    for input_path in input_root.rglob("*"):
        if input_path.is_symlink():
            raise BundleError(f"native input contains symbolic link: {input_path}")
        if input_path.is_file():
            actual_paths.add(input_path.relative_to(input_root).as_posix())
    missing = sorted(expected_paths - actual_paths)
    extra = sorted(actual_paths - expected_paths)
    if missing or extra:
        details = []
        if missing:
            details.append(f"missing {missing}")
        if extra:
            details.append(f"unexpected {extra}")
        raise BundleError(f"native input matrix mismatch: {'; '.join(details)}")

    payloads: dict[str, bytes] = {}
    file_records: list[dict[str, Any]] = []
    for payload_path in sorted(expected_paths):
        contents = (input_root / PurePosixPath(payload_path)).read_bytes()
        payloads[payload_path] = contents
        file_records.append(inspect_payload(path=payload_path, contents=contents, compatibility=compatibility))
    verify_consistent_jni_surface(file_records)

    manifest = {
        "bundle_version": compatibility["bindings"]["version"],
        "compatibility": compatibility,
        "files": file_records,
        "format": BUNDLE_FORMAT,
        "matrix": expected_matrix(compatibility),
        "schema_version": BUNDLE_SCHEMA_VERSION,
    }
    _write_bundle_zip(output, manifest, payloads)


def _validate_archive_path(path: str) -> None:
    if not path or "\0" in path or "\\" in path or path.startswith("/"):
        raise BundleError(f"unsafe archive path: {path!r}")
    pure_path = PurePosixPath(path)
    if any(part in ("", ".", "..") for part in pure_path.parts):
        raise BundleError(f"unsafe archive path: {path!r}")
    if pure_path.as_posix() != path:
        raise BundleError(f"unsafe archive path: {path!r}")


def _compare_compatibility(
    embedded: dict[str, Any],
    expected: dict[str, Any],
) -> None:
    try:
        if embedded["engine"]["revision"] != expected["engine"]["revision"]:
            raise BundleError(
                "revision mismatch: native bundle records "
                f"{embedded['engine']['revision']}, expected {expected['engine']['revision']}"
            )
        if embedded["engine"]["version"] != expected["engine"]["version"]:
            raise BundleError(
                "version mismatch: native bundle records "
                f"{embedded['engine']['version']}, expected {expected['engine']['version']}"
            )
        if embedded["jni_contract_version"] != expected["jni_contract_version"]:
            raise BundleError(
                "JNI contract mismatch: native bundle records "
                f"{embedded['jni_contract_version']}, expected {expected['jni_contract_version']}"
            )
    except (KeyError, TypeError) as error:
        raise BundleError(f"native bundle compatibility metadata is malformed: {error}") from error
    if embedded != expected:
        raise BundleError("native bundle compatibility metadata mismatch")


def _load_manifest(
    archive: zipfile.ZipFile,
) -> tuple[dict[str, Any], dict[str, zipfile.ZipInfo]]:
    infos = archive.infolist()
    names: dict[str, zipfile.ZipInfo] = {}
    for info in infos:
        _validate_archive_path(info.filename)
        if info.filename in names:
            raise BundleError(f"duplicate archive member: {info.filename}")
        file_type = (info.external_attr >> 16) & 0o170000
        if file_type == stat.S_IFLNK:
            raise BundleError(f"symbolic-link archive member: {info.filename}")
        if info.is_dir():
            raise BundleError(f"directory archive member is not allowed: {info.filename}")
        names[info.filename] = info
    if MANIFEST_PATH not in names:
        raise BundleError("native bundle is missing manifest.json")
    try:
        raw = archive.read(names[MANIFEST_PATH])
        manifest = json.loads(raw)
    except (OSError, KeyError, json.JSONDecodeError, UnicodeDecodeError) as error:
        raise BundleError(f"unable to read native bundle manifest: {error}") from error
    if not isinstance(manifest, dict):
        raise BundleError("native bundle manifest root must be an object")
    if raw != canonical_json(manifest):
        raise BundleError("native bundle manifest is not canonical JSON")
    return manifest, names


def validate_bundle(bundle: Path, compatibility_path: Path) -> ValidatedBundle:
    compatibility = read_canonical_json(compatibility_path, "compatibility document")
    validate_compatibility_shape(compatibility)
    if not bundle.is_file():
        raise BundleError(f"native bundle does not exist: {bundle}")
    try:
        archive = zipfile.ZipFile(bundle)
    except (OSError, zipfile.BadZipFile) as error:
        raise BundleError(f"unable to open native bundle: {error}") from error

    with archive:
        manifest, archive_infos = _load_manifest(archive)
        try:
            if manifest["format"] != BUNDLE_FORMAT:
                raise BundleError(f"unsupported native bundle format: {manifest['format']!r}")
            if manifest["schema_version"] != BUNDLE_SCHEMA_VERSION:
                raise BundleError(f"unsupported native bundle schema: {manifest['schema_version']}")
            embedded_compatibility = manifest["compatibility"]
            if not isinstance(embedded_compatibility, dict):
                raise BundleError("native bundle compatibility metadata must be an object")
            _compare_compatibility(embedded_compatibility, compatibility)
            if manifest["bundle_version"] != compatibility["bindings"]["version"]:
                raise BundleError("native bundle version does not match bindings version")
            if manifest["matrix"] != expected_matrix(compatibility):
                raise BundleError("native bundle declared matrix does not match compatibility matrix")
            file_records = manifest["files"]
        except (KeyError, TypeError) as error:
            raise BundleError(f"native bundle manifest is missing required field: {error}") from error
        if not isinstance(file_records, list):
            raise BundleError("native bundle files field must be an array")

        expected_paths = set(expected_payload_paths(compatibility))
        actual_paths = set(archive_infos) - {MANIFEST_PATH}
        for path in sorted(expected_paths - actual_paths):
            raise BundleError(f"missing archive member: {path}")
        extra_paths = sorted(actual_paths - expected_paths)
        if extra_paths:
            raise BundleError(f"unexpected archive member: {extra_paths[0]}")

        records_by_path: dict[str, dict[str, Any]] = {}
        for record in file_records:
            if not isinstance(record, dict) or not isinstance(record.get("path"), str):
                raise BundleError("native bundle contains a malformed file record")
            path = record["path"]
            if path in records_by_path:
                raise BundleError(f"duplicate manifest file record: {path}")
            records_by_path[path] = record
        missing_records = sorted(expected_paths - records_by_path.keys())
        extra_records = sorted(records_by_path.keys() - expected_paths)
        if missing_records:
            raise BundleError(f"missing manifest file record: {missing_records[0]}")
        if extra_records:
            raise BundleError(f"unexpected manifest file record: {extra_records[0]}")
        if [record["path"] for record in file_records] != sorted(expected_paths):
            raise BundleError("native bundle file records are not in canonical path order")

        verified_records: list[dict[str, Any]] = []
        for path in sorted(expected_paths):
            contents = archive.read(archive_infos[path])
            record = records_by_path[path]
            if record.get("size") != len(contents):
                raise BundleError(f"size mismatch for {path}")
            if record.get("sha256") != sha256(contents):
                raise BundleError(f"SHA-256 mismatch for {path}")
            inspected = inspect_payload(
                path=path,
                contents=contents,
                compatibility=compatibility,
            )
            for key in (
                "elf_class",
                "elf_machine",
                "jni_symbols",
                "jni_symbols_sha256",
            ):
                if record.get(key) != inspected[key]:
                    if key == "jni_symbols":
                        raise BundleError(f"JNI symbol list mismatch for {path}")
                    if key == "jni_symbols_sha256":
                        raise BundleError(f"JNI symbol digest mismatch for {path}")
                    raise BundleError(f"{key.replace('_', ' ')} mismatch for {path}")
            if set(record) != {
                "elf_class",
                "elf_machine",
                "jni_symbols",
                "jni_symbols_sha256",
                "path",
                "sha256",
                "size",
            }:
                raise BundleError(f"file record has unexpected fields: {path}")
            verified_records.append(inspected)
        verify_consistent_jni_surface(verified_records)
    return ValidatedBundle(manifest=manifest, compatibility=compatibility)


def extract_bundle(
    bundle: Path,
    compatibility_path: Path,
    build_type: str,
    output: Path,
) -> None:
    validated = validate_bundle(bundle, compatibility_path)
    build_types = validated.compatibility["native"]["build_types"]
    if build_type not in build_types:
        raise BundleError(f"unsupported build type {build_type!r}; expected one of {', '.join(build_types)}")
    output = output.resolve()
    if output == Path(output.anchor) or output == Path.home().resolve():
        raise BundleError(f"refusing unsafe extraction output: {output}")
    output.parent.mkdir(parents=True, exist_ok=True)
    temporary = Path(tempfile.mkdtemp(prefix=f".{output.name}.", dir=output.parent))
    try:
        with zipfile.ZipFile(bundle) as archive:
            prefix = f"{build_type}/"
            for record in validated.manifest["files"]:
                path = record["path"]
                if not path.startswith(prefix):
                    continue
                relative = PurePosixPath(path[len(prefix) :])
                destination = temporary.joinpath(*relative.parts)
                destination.parent.mkdir(parents=True, exist_ok=True)
                with destination.open("wb") as output_file:
                    output_file.write(archive.read(path))
                destination.chmod(0o644)
        if output.exists():
            if output.is_symlink() or not output.is_dir():
                raise BundleError(f"extraction output exists and is not a directory: {output}")
            shutil.rmtree(output)
        os.replace(temporary, output)
    finally:
        if temporary.exists():
            shutil.rmtree(temporary)


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    subparsers = parser.add_subparsers(dest="command", required=True)

    create = subparsers.add_parser("create", help="create a deterministic native bundle")
    create.add_argument("--compatibility", required=True, type=Path)
    create.add_argument("--input-root", required=True, type=Path)
    create.add_argument("--output", required=True, type=Path)

    validate = subparsers.add_parser("validate", help="validate a native bundle")
    validate.add_argument("--compatibility", required=True, type=Path)
    validate.add_argument("--bundle", required=True, type=Path)

    extract = subparsers.add_parser("extract", help="validate and extract one build type")
    extract.add_argument("--compatibility", required=True, type=Path)
    extract.add_argument("--bundle", required=True, type=Path)
    extract.add_argument("--build-type", required=True)
    extract.add_argument("--output", required=True, type=Path)
    return parser


def main() -> int:
    arguments = build_parser().parse_args()
    try:
        if arguments.command == "create":
            create_bundle(arguments.compatibility, arguments.input_root, arguments.output)
            print(f"created deterministic native bundle: {arguments.output}")
        elif arguments.command == "validate":
            validate_bundle(arguments.bundle, arguments.compatibility)
            print(f"validated native bundle: {arguments.bundle}")
        else:
            extract_bundle(
                arguments.bundle,
                arguments.compatibility,
                arguments.build_type,
                arguments.output,
            )
            print(f"validated and extracted {arguments.build_type} native payload: {arguments.output}")
    except (BundleError, OSError, zipfile.BadZipFile) as error:
        print(f"error: {error}", file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main())
