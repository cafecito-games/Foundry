#!/usr/bin/env python3
"""Derive the Java members the Android native host resolves through JNI at runtime.

The engine looks these members up reflectively with ``GetMethodID``/``GetFieldID``,
so nothing in the compiled Java references them and R8 is free to rename, inline or
remove them in minified release builds. This module parses the authoritative call
sites out of the native sources and emits the keep rules that pin them.
"""

from __future__ import annotations

import argparse
import re
import sys
from dataclasses import dataclass
from pathlib import Path

FOUNDRY_PACKAGE_PREFIX = "games.cafecito.foundry."

# Native sources scanned for runtime JNI member lookups. Every source under
# platform/android that performs a lookup must appear here so a new lookup site
# cannot silently escape the contract.
SCANNED_SOURCES = (
    "platform/android/dir_access_jandroid.cpp",
    "platform/android/file_access_filesystem_jandroid.cpp",
    "platform/android/java_class_wrapper.cpp",
    "platform/android/java_foundry_io_wrapper.cpp",
    "platform/android/java_foundry_view_wrapper.cpp",
    "platform/android/java_foundry_wrapper.cpp",
    "platform/android/jni_utils.cpp",
    "platform/android/net_socket_android.cpp",
    "platform/android/tts_android.cpp",
)

# jclass handles obtained with GetObjectClass carry no class name in the native
# source, so the declaring type is recorded here. Each entry is the static type
# the instance is handed over with across the FoundryLib JNI boundary.
INSTANCE_CLASS_BY_HANDLE = {
    (
        "platform/android/dir_access_jandroid.cpp",
        "cls",
    ): "games.cafecito.foundry.io.directory.DirectoryAccessHandler",
    (
        "platform/android/file_access_filesystem_jandroid.cpp",
        "cls",
    ): "games.cafecito.foundry.io.file.FileAccessHandler",
    ("platform/android/java_foundry_io_wrapper.cpp", "cls"): "games.cafecito.foundry.FoundryIO",
    ("platform/android/java_foundry_view_wrapper.cpp", "_cls"): "games.cafecito.foundry.FoundryRenderView",
    ("platform/android/net_socket_android.cpp", "cls"): "games.cafecito.foundry.utils.FoundryNetUtils",
    ("platform/android/tts_android.cpp", "cls"): "games.cafecito.foundry.tts.FoundryTTS",
}

# Members resolved on a Foundry type but declared by the Android framework. R8
# never renames library members, and they are absent from the application dex,
# so they carry no keep rule and are excluded from artifact inspection.
FRAMEWORK_INHERITED_MEMBERS = {
    ("games.cafecito.foundry.FoundryRenderView", "requestPointerCapture", "()V"),
    ("games.cafecito.foundry.FoundryRenderView", "releasePointerCapture", "()V"),
}

# jclass handles that hold the runtime class of a polymorphic jobject. The
# declaring type is a framework class per lookup, so every member resolved on one
# is declared here; an undeclared lookup fails rather than escaping the contract.
FRAMEWORK_DISPATCH_MEMBERS = {
    ("platform/android/jni_utils.cpp", "c", "booleanValue", "()Z"): "java.lang.Boolean",
    ("platform/android/jni_utils.cpp", "oclass", "entrySet", "()Ljava/util/Set;"): "java.util.Map",
    ("platform/android/jni_utils.cpp", "setClass", "iterator", "()Ljava/util/Iterator;"): "java.util.Set",
    ("platform/android/jni_utils.cpp", "iteratorClass", "hasNext", "()Z"): "java.util.Iterator",
    (
        "platform/android/jni_utils.cpp",
        "iteratorClass",
        "next",
        "()Ljava/lang/Object;",
    ): "java.util.Iterator",
}

# Lookups whose member name is only known at runtime. These drive the generic
# JavaClassWrapper bridge over arbitrary user classes, which no engine-owned keep
# rule can cover. Pinned so a new dynamic site is an explicit review decision.
DYNAMIC_LOOKUP_SITES = (
    ("platform/android/java_class_wrapper.cpp", "GetStaticMethodID"),
    ("platform/android/java_class_wrapper.cpp", "GetMethodID"),
)

_LOOKUP = re.compile(
    r"->\s*(?P<kind>GetStaticMethodID|GetMethodID|GetStaticFieldID|GetFieldID)\s*\(\s*"
    r"(?P<handle>[A-Za-z_][A-Za-z0-9_]*)\s*,\s*"
    r'"(?P<member>[^"]*)"\s*,\s*"(?P<descriptor>[^"]*)"\s*\)'
)
_DYNAMIC_LOOKUP = re.compile(
    r"->\s*(?P<kind>GetStaticMethodID|GetMethodID|GetStaticFieldID|GetFieldID)\s*\(\s*"
    r"(?P<handle>[A-Za-z_][A-Za-z0-9_]*)\s*,(?P<arguments>[^;]*)"
)
_BY_NAME_BINDING = re.compile(
    r"(?P<handle>[A-Za-z_][A-Za-z0-9_]*)\s*=\s*(?:\([^)]*\)\s*)?"
    r'(?:jni_find_class\s*\([^,]+,\s*|[A-Za-z_][A-Za-z0-9_]*->\s*FindClass\s*\(\s*)"(?P<class_name>[^"]+)"'
)
_ALIAS_BINDING = re.compile(
    r"(?P<handle>[A-Za-z_][A-Za-z0-9_]*)\s*=\s*(?:\([^)]*\)\s*)?"
    r"[A-Za-z_][A-Za-z0-9_]*->\s*NewGlobalRef\s*\(\s*(?P<source>[A-Za-z_][A-Za-z0-9_]*)\s*\)"
)
_INSTANCE_BINDING = re.compile(
    r"(?P<handle>[A-Za-z_][A-Za-z0-9_]*)\s*=\s*(?:\([^)]*\)\s*)?"
    r"[A-Za-z_][A-Za-z0-9_]*->\s*GetObjectClass\s*\("
)
_NESTED_INSTANCE_BINDING = re.compile(
    r"(?P<handle>[A-Za-z_][A-Za-z0-9_]*)\s*=\s*(?:\([^)]*\)\s*)?"
    r"[A-Za-z_][A-Za-z0-9_]*->\s*NewGlobalRef\s*\(\s*"
    r"[A-Za-z_][A-Za-z0-9_]*->\s*GetObjectClass\s*\("
)

_ANY_ASSIGNMENT = re.compile(r"(?P<handle>[A-Za-z_][A-Za-z0-9_]*)\s*=(?!=)")

_PRIMITIVE_TYPES = {
    "Z": "boolean",
    "B": "byte",
    "C": "char",
    "S": "short",
    "I": "int",
    "J": "long",
    "F": "float",
    "D": "double",
    "V": "void",
}

_INSTANCE_HANDLE = "<instance>"

GENERATED_HEADER = (
    "# Generated by platform/android/android_host_contract.py. Do not edit by hand.\n"
    "#\n"
    "# The native host resolves these members reflectively through JNI, so no compiled\n"
    "# Java reference keeps them alive. Rules are emitted without allowoptimization so\n"
    "# R8 cannot rename, remove, inline or staticize them in minified release builds.\n"
    "# Regenerate with:\n"
    "#\n"
    "#     python3 platform/android/android_host_contract.py --emit-keep-rules\n"
)


class HostContractError(RuntimeError):
    """The native sources do not yield one exact Java host member contract."""


@dataclass(frozen=True, order=True)
class HostMember:
    """One Java member the native host resolves by name at runtime."""

    class_name: str
    member_name: str
    descriptor: str
    static: bool
    field: bool
    resolved_by_name: bool


def _decode_type(descriptor: str, start: int) -> tuple[str, int]:
    dimensions = 0
    index = start
    while index < len(descriptor) and descriptor[index] == "[":
        dimensions += 1
        index += 1
    if index >= len(descriptor):
        raise HostContractError(f"truncated JNI type descriptor: {descriptor!r}")
    marker = descriptor[index]
    if marker == "L":
        end = descriptor.find(";", index)
        if end < 0:
            raise HostContractError(f"unterminated object type in JNI descriptor: {descriptor!r}")
        # Nested types keep their '$' separator, which is what both ProGuard member
        # specifications and dex member listings use.
        decoded = descriptor[index + 1 : end].replace("/", ".")
        if not decoded:
            raise HostContractError(f"empty object type in JNI descriptor: {descriptor!r}")
        index = end + 1
    elif marker in _PRIMITIVE_TYPES:
        decoded = _PRIMITIVE_TYPES[marker]
        index += 1
    else:
        raise HostContractError(f"unsupported JNI type marker {marker!r} in descriptor: {descriptor!r}")
    return decoded + "[]" * dimensions, index


def decode_method_descriptor(descriptor: str) -> tuple[str, tuple[str, ...]]:
    """Return the Java return type and argument types of a JNI method descriptor."""

    if not descriptor.startswith("("):
        raise HostContractError(f"method descriptor must start with '(': {descriptor!r}")
    close = descriptor.find(")")
    if close < 0:
        raise HostContractError(f"method descriptor has no ')': {descriptor!r}")
    arguments: list[str] = []
    index = 1
    while index < close:
        decoded, index = _decode_type(descriptor, index)
        arguments.append(decoded)
    if index != close:
        raise HostContractError(f"method descriptor arguments overrun ')': {descriptor!r}")
    return_type, index = _decode_type(descriptor, close + 1)
    if index != len(descriptor):
        raise HostContractError(f"trailing characters after method return type: {descriptor!r}")
    return return_type, tuple(arguments)


def decode_field_descriptor(descriptor: str) -> str:
    """Return the Java type of a JNI field descriptor."""

    decoded, index = _decode_type(descriptor, 0)
    if index != len(descriptor):
        raise HostContractError(f"trailing characters after field type: {descriptor!r}")
    return decoded


def member_signature(member: HostMember) -> str:
    """Return the ProGuard member specification for one host member."""

    modifier = "static " if member.static else ""
    if member.field:
        return f"{modifier}{decode_field_descriptor(member.descriptor)} {member.member_name};"
    return_type, arguments = decode_method_descriptor(member.descriptor)
    if member.member_name in ("<init>", "<clinit>"):
        # ProGuard spells out initializers without a return type.
        if return_type != "void":
            raise HostContractError(f"initializer {member.member_name} must return void: {member.descriptor!r}")
        return f"{modifier}{member.member_name}({', '.join(arguments)});"
    return f"{modifier}{return_type} {member.member_name}({', '.join(arguments)});"


def _scan_source(source: str, relative_path: str) -> tuple[list[HostMember], list[tuple[str, str]]]:
    bindings: dict[str, str] = {}
    members: list[HostMember] = []
    dynamic: list[tuple[str, str]] = []
    for line in source.splitlines():
        nested = _NESTED_INSTANCE_BINDING.search(line)
        if nested:
            bindings[nested.group("handle")] = _INSTANCE_HANDLE
        else:
            by_name = _BY_NAME_BINDING.search(line)
            if by_name:
                bindings[by_name.group("handle")] = by_name.group("class_name").replace("/", ".")
            else:
                alias = _ALIAS_BINDING.search(line)
                if alias:
                    bindings[alias.group("handle")] = bindings.get(alias.group("source"), _INSTANCE_HANDLE)
                else:
                    instance = _INSTANCE_BINDING.search(line)
                    if instance:
                        bindings[instance.group("handle")] = _INSTANCE_HANDLE
                    else:
                        # Any other assignment invalidates what the handle held, so a
                        # stale binding can never attribute a member to the wrong class.
                        for assignment in _ANY_ASSIGNMENT.finditer(line):
                            bindings.pop(assignment.group("handle"), None)

        lookup = _LOOKUP.search(line)
        if lookup is None:
            dynamic_lookup = _DYNAMIC_LOOKUP.search(line)
            if dynamic_lookup is not None:
                dynamic.append((relative_path, dynamic_lookup.group("kind")))
            continue

        handle = lookup.group("handle")
        member_name = lookup.group("member")
        descriptor = lookup.group("descriptor")
        class_name = bindings.get(handle, _INSTANCE_HANDLE)
        resolved_by_name = class_name != _INSTANCE_HANDLE
        if not resolved_by_name:
            declared = INSTANCE_CLASS_BY_HANDLE.get((relative_path, handle))
            if declared is None:
                declared = FRAMEWORK_DISPATCH_MEMBERS.get((relative_path, handle, member_name, descriptor))
            if declared is None:
                raise HostContractError(
                    f"{relative_path} resolves JNI member {member_name!r} on instance handle "
                    f"{handle!r} with no declared class; add it to INSTANCE_CLASS_BY_HANDLE "
                    "or FRAMEWORK_DISPATCH_MEMBERS"
                )
            class_name = declared

        kind = lookup.group("kind")
        members.append(
            HostMember(
                class_name=class_name,
                member_name=member_name,
                descriptor=descriptor,
                static=kind.startswith("GetStatic"),
                field=kind.endswith("FieldID"),
                resolved_by_name=resolved_by_name,
            )
        )
    return members, dynamic


def derive_host_members(repo_root: Path) -> tuple[HostMember, ...]:
    """Return every Foundry-owned Java member the native host resolves at runtime."""

    sources: dict[str, str] = {}
    for relative_path in SCANNED_SOURCES:
        path = repo_root / relative_path
        try:
            sources[relative_path] = path.read_text(encoding="utf-8")
        except OSError as error:
            raise HostContractError(f"unable to read native source {relative_path}: {error}") from error
    return derive_host_members_from_sources(sources)


def derive_host_members_from_sources(sources: dict[str, str]) -> tuple[HostMember, ...]:
    """Return the host contract implied by already-read native sources."""

    members: list[HostMember] = []
    dynamic: list[tuple[str, str]] = []
    for relative_path, source in sources.items():
        scanned, scanned_dynamic = _scan_source(source, relative_path)
        members.extend(scanned)
        dynamic.extend(scanned_dynamic)

    unexpected = sorted(set(dynamic) - set(DYNAMIC_LOOKUP_SITES))
    if unexpected:
        raise HostContractError(
            "native sources gained runtime-named JNI lookups that no keep rule can cover: "
            + ", ".join(f"{path} {kind}" for path, kind in unexpected)
        )

    owned = {
        member
        for member in members
        if member.class_name.startswith(FOUNDRY_PACKAGE_PREFIX)
        and (member.class_name, member.member_name, member.descriptor) not in FRAMEWORK_INHERITED_MEMBERS
    }
    if not owned:
        raise HostContractError("native sources resolve no Foundry-owned Java members")

    by_identity: dict[tuple[str, str, str], HostMember] = {}
    for member in sorted(owned):
        identity = (member.class_name, member.member_name, member.descriptor)
        existing = by_identity.get(identity)
        if existing is None:
            by_identity[identity] = member
            continue
        if existing.static != member.static or existing.field != member.field:
            raise HostContractError(f"native sources resolve {member.class_name}.{member.member_name} inconsistently")
        if member.resolved_by_name and not existing.resolved_by_name:
            by_identity[identity] = member
    return tuple(sorted(by_identity.values()))


def emit_keep_rules(members: tuple[HostMember, ...]) -> str:
    """Return the ProGuard/R8 keep rules pinning every host member."""

    resolved_by_name = {member.class_name for member in members if member.resolved_by_name}
    grouped: dict[str, list[HostMember]] = {}
    for member in members:
        grouped.setdefault(member.class_name, []).append(member)

    blocks: list[str] = []
    for class_name in sorted(grouped):
        # A class the native host resolves by name must also keep its own name.
        directive = "-keep" if class_name in resolved_by_name else "-keepclassmembers"
        signatures = sorted(member_signature(member) for member in grouped[class_name])
        body = "\n".join(f"    {signature}" for signature in signatures)
        blocks.append(f"{directive},includedescriptorclasses class {class_name} {{\n{body}\n}}\n")
    return GENERATED_HEADER + "\n" + "\n".join(blocks)


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo-root", type=Path, default=Path(__file__).resolve().parents[2])
    parser.add_argument("--emit-keep-rules", action="store_true", help="write the generated rules file in place")
    parser.add_argument("--check", action="store_true", help="fail when the checked-in rules file is stale")
    return parser


def keep_rules_path(repo_root: Path) -> Path:
    return repo_root / "platform/android/java/lib/proguard-rules.pro"


def main() -> int:
    arguments = build_parser().parse_args()
    try:
        rules = emit_keep_rules(derive_host_members(arguments.repo_root))
    except HostContractError as error:
        print(f"error: {error}", file=sys.stderr)
        return 2
    path = keep_rules_path(arguments.repo_root)
    if arguments.emit_keep_rules:
        path.write_text(rules, encoding="utf-8")
        return 0
    if arguments.check:
        try:
            current = path.read_text(encoding="utf-8")
        except OSError as error:
            print(f"error: unable to read {path}: {error}", file=sys.stderr)
            return 2
        if current != rules:
            print(
                f"error: {path} is stale; regenerate with "
                "'python3 platform/android/android_host_contract.py --emit-keep-rules'",
                file=sys.stderr,
            )
            return 2
        return 0
    sys.stdout.write(rules)
    return 0


if __name__ == "__main__":
    sys.exit(main())
