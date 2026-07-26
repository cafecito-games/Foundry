from __future__ import annotations

import hashlib
import json
import struct
import subprocess
import sys
import zipfile
from pathlib import Path
from typing import Any

ROOT = Path(__file__).resolve().parents[2]

BUILD_TYPES = ("debug", "dev", "release")
ABI_SPECS = {
    "armeabi-v7a": {"elf_class": 32, "elf_machine": 40},
    "arm64-v8a": {"elf_class": 64, "elf_machine": 183},
    "x86": {"elf_class": 32, "elf_machine": 3},
    "x86_64": {"elf_class": 64, "elf_machine": 62},
}
LIBRARIES = ("libfoundry_android.so", "libc++_shared.so")
FOUNDRY_SYMBOLS = (
    "Java_games_cafecito_foundry_FoundryLib_step",
    "Java_games_cafecito_foundry_plugin_FoundryPlugin_nativeRegisterSingleton",
)
EXTERNAL_JNI_EXPORTS = (
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
)
EXTERNAL_JNI_SYMBOLS = tuple(entry["symbol"] for entry in EXTERNAL_JNI_EXPORTS)


def canonical_json(value: Any) -> bytes:
    return (json.dumps(value, sort_keys=True, separators=(",", ":"), ensure_ascii=True) + "\n").encode()


def sample_compatibility() -> dict[str, Any]:
    return {
        "bindings": {"version": "0.1.0-dev-SNAPSHOT"},
        "engine": {
            "repository": "https://github.com/cafecito-games/Foundry",
            "revision": "a" * 40,
            "version": "0.1.0-dev",
            "version_components": {
                "major": 0,
                "minor": 1,
                "module_config": "",
                "patch": 0,
                "status": "dev",
            },
        },
        "jni_contract_version": 1,
        "native": {
            "abis": ABI_SPECS,
            "build_types": list(BUILD_TYPES),
            "external_jni_allowlist": list(EXTERNAL_JNI_EXPORTS),
            "libraries": list(LIBRARIES),
        },
        "schema_version": 1,
    }


def write_compatibility(path: Path, value: dict[str, Any] | None = None) -> dict[str, Any]:
    compatibility = value or sample_compatibility()
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(canonical_json(compatibility))
    return compatibility


def _align(value: int, alignment: int) -> int:
    return (value + alignment - 1) & ~(alignment - 1)


def minimal_elf(elf_class: int, machine: int, symbols: tuple[str, ...] = ()) -> bytes:
    if elf_class not in (32, 64):
        raise ValueError(f"unsupported test ELF class: {elf_class}")

    ident = b"\x7fELF" + bytes((1 if elf_class == 32 else 2, 1, 1, 0)) + bytes(8)
    dynamic_strings = bytearray(b"\0")
    symbol_name_offsets: list[int] = []
    for symbol in symbols:
        symbol_name_offsets.append(len(dynamic_strings))
        dynamic_strings.extend(symbol.encode("ascii"))
        dynamic_strings.append(0)

    section_strings = b"\0.dynstr\0.dynsym\0.shstrtab\0"
    section_name_offsets = (0, 1, 9, 17)
    if elf_class == 64:
        header_size = 64
        section_header_size = 64
        symbol_entry_size = 24
        symbol_table = bytearray(symbol_entry_size)
        for name_offset in symbol_name_offsets:
            symbol_table.extend(struct.pack("<IBBHQQ", name_offset, 0x12, 0, 1, 0, 0))
    else:
        header_size = 52
        section_header_size = 40
        symbol_entry_size = 16
        symbol_table = bytearray(symbol_entry_size)
        for name_offset in symbol_name_offsets:
            symbol_table.extend(struct.pack("<IIIBBH", name_offset, 0, 0, 0x12, 0, 1))

    dynstr_offset = header_size
    dynsym_offset = _align(dynstr_offset + len(dynamic_strings), 8 if elf_class == 64 else 4)
    shstr_offset = dynsym_offset + len(symbol_table)
    section_headers_offset = _align(shstr_offset + len(section_strings), 8 if elf_class == 64 else 4)

    if elf_class == 64:
        header = ident + struct.pack(
            "<HHIQQQIHHHHHH",
            3,
            machine,
            1,
            0,
            0,
            section_headers_offset,
            0,
            header_size,
            0,
            0,
            section_header_size,
            4,
            3,
        )
        section_headers = [
            bytes(section_header_size),
            struct.pack(
                "<IIQQQQIIQQ",
                section_name_offsets[1],
                3,
                2,
                0,
                dynstr_offset,
                len(dynamic_strings),
                0,
                0,
                1,
                0,
            ),
            struct.pack(
                "<IIQQQQIIQQ",
                section_name_offsets[2],
                11,
                2,
                0,
                dynsym_offset,
                len(symbol_table),
                1,
                1,
                8,
                symbol_entry_size,
            ),
            struct.pack(
                "<IIQQQQIIQQ",
                section_name_offsets[3],
                3,
                0,
                0,
                shstr_offset,
                len(section_strings),
                0,
                0,
                1,
                0,
            ),
        ]
    else:
        header = ident + struct.pack(
            "<HHIIIIIHHHHHH",
            3,
            machine,
            1,
            0,
            0,
            section_headers_offset,
            0,
            header_size,
            0,
            0,
            section_header_size,
            4,
            3,
        )
        section_headers = [
            bytes(section_header_size),
            struct.pack(
                "<IIIIIIIIII",
                section_name_offsets[1],
                3,
                2,
                0,
                dynstr_offset,
                len(dynamic_strings),
                0,
                0,
                1,
                0,
            ),
            struct.pack(
                "<IIIIIIIIII",
                section_name_offsets[2],
                11,
                2,
                0,
                dynsym_offset,
                len(symbol_table),
                1,
                1,
                4,
                symbol_entry_size,
            ),
            struct.pack(
                "<IIIIIIIIII",
                section_name_offsets[3],
                3,
                0,
                0,
                shstr_offset,
                len(section_strings),
                0,
                0,
                1,
                0,
            ),
        ]

    data = bytearray(header)
    data.extend(dynamic_strings)
    data.extend(bytes(dynsym_offset - len(data)))
    data.extend(symbol_table)
    data.extend(section_strings)
    data.extend(bytes(section_headers_offset - len(data)))
    for section_header in section_headers:
        data.extend(section_header)
    return bytes(data)


def populate_native_inputs(
    root: Path,
    *,
    foundry_symbols: tuple[str, ...] = FOUNDRY_SYMBOLS,
    external_jni_symbols: tuple[str, ...] = EXTERNAL_JNI_SYMBOLS,
    wrong_abi_path: str | None = None,
) -> None:
    for build_type in BUILD_TYPES:
        for abi, abi_spec in ABI_SPECS.items():
            for library in LIBRARIES:
                path = root / build_type / abi / library
                path.parent.mkdir(parents=True, exist_ok=True)
                effective_spec = abi_spec
                if path.relative_to(root).as_posix() == wrong_abi_path:
                    effective_spec = ABI_SPECS["x86_64" if abi != "x86_64" else "arm64-v8a"]
                symbols = foundry_symbols + external_jni_symbols if library == "libfoundry_android.so" else ()
                path.write_bytes(
                    minimal_elf(
                        effective_spec["elf_class"],
                        effective_spec["elf_machine"],
                        symbols,
                    )
                )


def run_tool(tool: str, *arguments: str) -> subprocess.CompletedProcess[str]:
    if tool == "native_bundle.py":
        tool = "android_native_bundle.py"
    return subprocess.run(
        [sys.executable, str(ROOT / "platform" / "android" / tool), *arguments],
        cwd=ROOT,
        check=False,
        capture_output=True,
        text=True,
    )


def create_bundle(
    workspace: Path,
    *,
    foundry_symbols: tuple[str, ...] = FOUNDRY_SYMBOLS,
) -> tuple[Path, Path, Path]:
    compatibility = workspace / "compatibility.json"
    native_root = workspace / "native"
    bundle = workspace / "foundry-native.zip"
    write_compatibility(compatibility)
    populate_native_inputs(native_root, foundry_symbols=foundry_symbols)
    result = run_tool(
        "native_bundle.py",
        "create",
        "--compatibility",
        str(compatibility),
        "--input-root",
        str(native_root),
        "--output",
        str(bundle),
    )
    if result.returncode != 0:
        raise AssertionError(f"bundle creation failed:\nstdout:\n{result.stdout}\nstderr:\n{result.stderr}")
    return compatibility, native_root, bundle


def rewrite_zip(
    source: Path,
    destination: Path,
    *,
    replace: dict[str, bytes] | None = None,
    remove: set[str] | None = None,
    additions: list[tuple[zipfile.ZipInfo | str, bytes]] | None = None,
) -> None:
    replacements = replace or {}
    removals = remove or set()
    with zipfile.ZipFile(source) as source_zip:
        with zipfile.ZipFile(destination, "w") as destination_zip:
            for info in source_zip.infolist():
                if info.filename in removals:
                    continue
                contents = replacements.get(info.filename, source_zip.read(info))
                copied = zipfile.ZipInfo(info.filename, info.date_time)
                copied.compress_type = info.compress_type
                copied.create_system = info.create_system
                copied.external_attr = info.external_attr
                copied.flag_bits = info.flag_bits
                destination_zip.writestr(copied, contents)
            for name_or_info, contents in additions or []:
                destination_zip.writestr(name_or_info, contents)


def sha256(contents: bytes) -> str:
    return hashlib.sha256(contents).hexdigest()
