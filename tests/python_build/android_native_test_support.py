from __future__ import annotations

import hashlib
import json
import struct
from pathlib import Path
from typing import Any

BUILD_TYPES = ("debug", "dev", "release")
BUILD_SPECS = {
    "debug": {
        "target": "template_debug",
        "production": False,
        "dev_mode": False,
        "dev_build": False,
        "debug_symbols": False,
        "tests": False,
    },
    "dev": {
        "target": "template_debug",
        "production": False,
        "dev_mode": True,
        "dev_build": True,
        "debug_symbols": True,
        "tests": False,
    },
    "release": {
        "target": "template_release",
        "production": True,
        "dev_mode": False,
        "dev_build": False,
        "debug_symbols": False,
        "tests": False,
    },
}
ABI_SPECS = {
    "armeabi-v7a": {"elf_class": 32, "elf_machine": 40},
    "arm64-v8a": {"elf_class": 64, "elf_machine": 183},
    "x86": {"elf_class": 32, "elf_machine": 3},
    "x86_64": {"elf_class": 64, "elf_machine": 62},
}
SCONS_ARCHES = {
    "armeabi-v7a": "arm32",
    "arm64-v8a": "arm64",
    "x86": "x86_32",
    "x86_64": "x86_64",
}
LIBRARIES = ("libfoundry_android.so", "libc++_shared.so")
FOUNDRY_SYMBOLS = (
    "Java_games_cafecito_foundry_FoundryLib_accelerometer",
    "Java_games_cafecito_foundry_FoundryLib_back",
    "Java_games_cafecito_foundry_FoundryLib_dispatchMouseEvent",
    "Java_games_cafecito_foundry_FoundryLib_dispatchTouchEvent",
    "Java_games_cafecito_foundry_FoundryLib_filePickerCallback",
    "Java_games_cafecito_foundry_FoundryLib_focusin",
    "Java_games_cafecito_foundry_FoundryLib_focusout",
    "Java_games_cafecito_foundry_FoundryLib_getGlobal",
    "Java_games_cafecito_foundry_FoundryLib_getProjectResourceDir",
    "Java_games_cafecito_foundry_FoundryLib_getRendererInfo",
    "Java_games_cafecito_foundry_FoundryLib_gravity",
    "Java_games_cafecito_foundry_FoundryLib_gyroscope",
    "Java_games_cafecito_foundry_FoundryLib_hardwareKeyboardConnected",
    "Java_games_cafecito_foundry_FoundryLib_hasFeature",
    "Java_games_cafecito_foundry_FoundryLib_initialize",
    "Java_games_cafecito_foundry_FoundryLib_joyaxis",
    "Java_games_cafecito_foundry_FoundryLib_joybutton",
    "Java_games_cafecito_foundry_FoundryLib_joyconnectionchanged",
    "Java_games_cafecito_foundry_FoundryLib_joyhat",
    "Java_games_cafecito_foundry_FoundryLib_key",
    "Java_games_cafecito_foundry_FoundryLib_magnetometer",
    "Java_games_cafecito_foundry_FoundryLib_magnify",
    "Java_games_cafecito_foundry_FoundryLib_newcontext",
    "Java_games_cafecito_foundry_FoundryLib_onNightModeChanged",
    "Java_games_cafecito_foundry_FoundryLib_onRendererPaused",
    "Java_games_cafecito_foundry_FoundryLib_onRendererResumed",
    "Java_games_cafecito_foundry_FoundryLib_onScreenRotationChange",
    "Java_games_cafecito_foundry_FoundryLib_ondestroy",
    "Java_games_cafecito_foundry_FoundryLib_pan",
    "Java_games_cafecito_foundry_FoundryLib_requestPermissionResult",
    "Java_games_cafecito_foundry_FoundryLib_resize",
    "Java_games_cafecito_foundry_FoundryLib_setVirtualKeyboardHeight",
    "Java_games_cafecito_foundry_FoundryLib_setup",
    "Java_games_cafecito_foundry_FoundryLib_shouldDispatchInputToRenderThread",
    "Java_games_cafecito_foundry_FoundryLib_step",
    "Java_games_cafecito_foundry_FoundryLib_ttsCallback",
    "Java_games_cafecito_foundry_utils_DialogUtils_dialogCallback",
    "Java_games_cafecito_foundry_utils_DialogUtils_inputDialogCallback",
    "Java_games_cafecito_foundry_variant_Callable_nativeCall",
    "Java_games_cafecito_foundry_variant_Callable_nativeCallObject",
    "Java_games_cafecito_foundry_variant_Callable_nativeCallObjectDeferred",
    "Java_games_cafecito_foundry_variant_Callable_releaseNativePointer",
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
    payload_suffix: bytes = b"",
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
                    + payload_suffix
                )


def populate_native_matrix(
    root: Path,
    *,
    revision: str,
    tree: str,
    foundry_symbols: tuple[str, ...] = FOUNDRY_SYMBOLS,
    payload_suffix: bytes = b"",
) -> None:
    """Create a deterministic valid 12-cell native root for Gradle behavior tests."""

    populate_native_inputs(root, foundry_symbols=foundry_symbols, payload_suffix=payload_suffix)
    for build_type in BUILD_TYPES:
        build_spec = BUILD_SPECS[build_type]
        for abi in ABI_SPECS:
            directory = root / build_type / abi
            libraries = []
            for library in sorted(LIBRARIES):
                contents = (directory / library).read_bytes()
                libraries.append(
                    {
                        "path": library,
                        "sha256": sha256(contents),
                        "size": len(contents),
                    }
                )
            provenance = {
                "build": {
                    "abi": abi,
                    "arch": SCONS_ARCHES[abi],
                    "build_type": build_type,
                    **build_spec,
                    "swappy": True,
                },
                "engine": {
                    "dirty": False,
                    "revision": revision,
                    "tree": tree,
                },
                "libraries": libraries,
                "schema_version": 1,
            }
            (directory / "provenance.json").write_bytes(canonical_json(provenance))


def sha256(contents: bytes) -> str:
    return hashlib.sha256(contents).hexdigest()
