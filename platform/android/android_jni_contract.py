#!/usr/bin/env python3
"""Derive the exact JNI surface from compiled Foundry Java and Kotlin classes."""

from __future__ import annotations

import argparse
import re
import subprocess
import sys
import zipfile
from collections import Counter
from dataclasses import dataclass
from pathlib import Path

CLASS_DECLARATION = re.compile(r"\b(?:class|interface|enum)\s+([^\s<{]+)")
NATIVE_KEYWORD = re.compile(r"\bnative\b")
NATIVE_METHOD = re.compile(r"\bnative\b[^(]*\s([^\s(]+)\(")
DESCRIPTOR = re.compile(r"^\s*descriptor:\s*(\([^)]*\).+)$")
FOUNDRY_CLASS_PREFIX = "games.cafecito.foundry."


class JniContractError(RuntimeError):
    """Compiled declarations could not provide one exact Foundry JNI contract."""


@dataclass(frozen=True)
class NativeDeclaration:
    class_name: str
    method_name: str
    descriptor: str


def _jni_mangle(value: str) -> str:
    result: list[str] = []
    for character in value:
        if character.isascii() and character.isalnum():
            result.append(character)
        elif character in ("/", "."):
            result.append("_")
        elif character == "_":
            result.append("_1")
        elif character == ";":
            result.append("_2")
        elif character == "[":
            result.append("_3")
        else:
            encoded = character.encode("utf-16-be")
            for offset in range(0, len(encoded), 2):
                code_unit = int.from_bytes(encoded[offset : offset + 2], "big")
                result.append(f"_0{code_unit:04x}")
    return "".join(result)


def _compiled_class_names(classes_jar: Path) -> tuple[str, ...]:
    try:
        with zipfile.ZipFile(classes_jar) as archive:
            names = tuple(
                sorted(
                    name[:-6].replace("/", ".")
                    for name in archive.namelist()
                    if name.endswith(".class")
                    and name.startswith(FOUNDRY_CLASS_PREFIX.replace(".", "/"))
                    and not name.endswith("module-info.class")
                )
            )
    except (OSError, zipfile.BadZipFile) as error:
        raise JniContractError(f"unable to read compiled classes {classes_jar}: {error}") from error
    if not names:
        raise JniContractError(f"compiled classes archive contains no compiled classes: {classes_jar}")
    return names


def _javap(classes_jar: Path, class_names: tuple[str, ...], javap: str) -> str:
    try:
        result = subprocess.run(
            [javap, "-p", "-s", "-classpath", str(classes_jar), *class_names],
            check=True,
            capture_output=True,
            text=True,
        )
    except (OSError, subprocess.CalledProcessError) as error:
        detail = getattr(error, "stderr", "") or str(error)
        raise JniContractError(f"unable to inspect compiled native declarations: {detail.strip()}") from error
    return result.stdout


def _parse_declarations(output: str) -> tuple[NativeDeclaration, ...]:
    current_class: str | None = None
    pending_method: str | None = None
    declarations: list[NativeDeclaration] = []
    for line in output.splitlines():
        class_match = CLASS_DECLARATION.search(line)
        if class_match:
            if pending_method is not None:
                raise JniContractError("javap omitted a descriptor for a compiled native declaration")
            current_class = class_match.group(1)
            continue
        method_match = NATIVE_METHOD.search(line)
        if NATIVE_KEYWORD.search(line):
            if method_match is None:
                raise JniContractError(f"unable to parse native declaration from javap output: {line.strip()}")
            if current_class is None:
                raise JniContractError("javap reported a native method before its declaring class")
            if pending_method is not None:
                raise JniContractError("javap omitted a descriptor for a compiled native declaration")
            pending_method = method_match.group(1)
            continue
        descriptor_match = DESCRIPTOR.match(line)
        if descriptor_match and pending_method is not None:
            assert current_class is not None
            declarations.append(
                NativeDeclaration(
                    class_name=current_class,
                    method_name=pending_method,
                    descriptor=descriptor_match.group(1),
                )
            )
            pending_method = None
    if pending_method is not None:
        raise JniContractError("javap omitted a descriptor for a compiled native declaration")
    if not declarations:
        raise JniContractError("compiled Foundry classes declare no native methods")
    return tuple(declarations)


def _symbol(declaration: NativeDeclaration, overloaded: bool) -> str:
    symbol = f"Java_{_jni_mangle(declaration.class_name)}_{_jni_mangle(declaration.method_name)}"
    if overloaded:
        arguments = declaration.descriptor[1 : declaration.descriptor.index(")")]
        symbol += f"__{_jni_mangle(arguments)}"
    return symbol


def derive_declared_jni_symbols(classes_jar: Path, *, javap: str = "javap") -> tuple[str, ...]:
    """Return canonical JNI names derived from actual compiled native declarations."""

    class_names = _compiled_class_names(classes_jar)
    declarations = _parse_declarations(_javap(classes_jar, class_names, javap))
    counts = Counter((declaration.class_name, declaration.method_name) for declaration in declarations)
    symbols = tuple(
        sorted(
            _symbol(
                declaration,
                counts[(declaration.class_name, declaration.method_name)] > 1,
            )
            for declaration in declarations
        )
    )
    if len(set(symbols)) != len(symbols):
        raise JniContractError("compiled native declarations produce duplicate JNI symbols")
    return symbols


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--classes-jar", required=True, type=Path)
    parser.add_argument("--javap", default="javap")
    return parser


def main() -> int:
    arguments = build_parser().parse_args()
    try:
        for symbol in derive_declared_jni_symbols(arguments.classes_jar, javap=arguments.javap):
            print(symbol)
    except JniContractError as error:
        print(f"error: {error}", file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main())
