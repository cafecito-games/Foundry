#!/usr/bin/env python3
"""Cross-reference this fork's public API names against its own naming.

Unlike the literal token rules in `misc/checks/file_policy.toml`, this check
extracts *name sets* from structured inputs — the glTF class reference XML and
`core/extension/foundry_extension_interface.json` — and compares those sets
against the names this fork publishes. A name that moved to a different type,
member, or argument is still found; a substring grep would not notice.

`collect_doc_names`, `collect_interface_names`, and `find_naming_violations` are
pure and are driven from `misc/checks/tests/test_check_extension_api_naming.py`.
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path
from typing import Any, Iterable, Sequence

DEFAULT_ROOT = Path(__file__).resolve().parents[2]

DOC_NAME_ATTRIBUTE = re.compile(r'\b(?:name|setter|getter)="([^"]+)"')

GLTF_DOC_PATHS = (
    "modules/gltf/doc_classes/GLTFDocument.xml",
    "modules/gltf/doc_classes/GLTFDocumentExtension.xml",
    "modules/gltf/doc_classes/GLTFObjectModelProperty.xml",
    "modules/gltf/doc_classes/GLTFSkeleton.xml",
    "modules/gltf/doc_classes/GLTFSkin.xml",
    "modules/gltf/doc_classes/GLTFState.xml",
)
INTERFACE_JSON_PATH = "core/extension/foundry_extension_interface.json"

FORBIDDEN_GLTF_DOC_NAMES = frozenset(
    {
        "godot_node",
        "godot_scene_node",
        "gltf_to_godot_expression",
        "get_gltf_to_godot_expression",
        "set_gltf_to_godot_expression",
        "godot_to_gltf_expression",
        "get_godot_to_gltf_expression",
        "set_godot_to_gltf_expression",
        "get_godot_skeleton",
        "godot_bone_node",
        "get_godot_bone_node",
        "set_godot_bone_node",
        "godot_skin",
        "get_godot_skin",
        "set_godot_skin",
    }
)
REQUIRED_GLTF_DOC_NAMES = frozenset(
    {
        "foundry_node",
        "foundry_scene_node",
        "gltf_to_foundry_expression",
        "get_gltf_to_foundry_expression",
        "set_gltf_to_foundry_expression",
        "foundry_to_gltf_expression",
        "get_foundry_to_gltf_expression",
        "set_foundry_to_gltf_expression",
        "get_foundry_skeleton",
        "foundry_bone_node",
        "get_foundry_bone_node",
        "set_foundry_bone_node",
        "foundry_skin",
        "get_foundry_skin",
        "set_foundry_skin",
    }
)

FORBIDDEN_INTERFACE_NAMES = frozenset(
    {
        "FoundryExtensionGodotVersion",
        "FoundryExtensionGodotVersion2",
        "get_godot_version",
        "get_godot_version2",
        "r_godot_version",
        "FoundryExtensionGodotVersion*",
        "FoundryExtensionGodotVersion2*",
    }
)
REQUIRED_INTERFACE_NAMES = frozenset(
    {
        "FoundryExtensionFoundryVersion",
        "FoundryExtensionFoundryVersion2",
        "get_foundry_version",
        "get_foundry_version2",
        "r_foundry_version",
        "FoundryExtensionFoundryVersion*",
        "FoundryExtensionFoundryVersion2*",
    }
)


class ExtensionApiNamingError(RuntimeError):
    """An input could not be parsed into a name set."""


def collect_doc_names(documents: Iterable[str]) -> set[str]:
    """Collect every `name`, `setter`, and `getter` attribute value in *documents*."""

    names: set[str] = set()
    for document in documents:
        names.update(DOC_NAME_ATTRIBUTE.findall(document))
    return names


def _require_list(document: Any, key: str) -> list[Any]:
    value = document.get(key) if isinstance(document, dict) else None
    if not isinstance(value, list):
        raise ExtensionApiNamingError(f"the extension interface has no {key!r} list")
    return value


def _require_name(entry: Any, context: str) -> str:
    if not isinstance(entry, dict):
        raise ExtensionApiNamingError(f"{context} is not an object")
    name = entry.get("name")
    if not isinstance(name, str):
        raise ExtensionApiNamingError(f"{context} has no name")
    return name


def collect_interface_names(document: Any) -> set[str]:
    """Collect every type, member, argument, and return-type name in *document*."""

    names: set[str] = set()
    for type_data in _require_list(document, "types"):
        names.add(_require_name(type_data, "a type"))
        for member in type_data.get("members", []):
            names.add(_require_name(member, "a type member"))
        for argument in type_data.get("arguments", []):
            argument_name = argument.get("name") if isinstance(argument, dict) else None
            if argument_name:
                names.add(argument_name)
            if not isinstance(argument, dict) or not isinstance(argument.get("type"), str):
                raise ExtensionApiNamingError("a type argument has no type")
            names.add(argument["type"])

    for function in _require_list(document, "interface"):
        names.add(_require_name(function, "an interface function"))
        for argument in function.get("arguments", []):
            names.add(_require_name(argument, "an interface argument"))
            if not isinstance(argument.get("type"), str):
                raise ExtensionApiNamingError("an interface argument has no type")
            names.add(argument["type"])
        return_value = function.get("return_value")
        if return_value:
            if not isinstance(return_value, dict) or not isinstance(return_value.get("type"), str):
                raise ExtensionApiNamingError("an interface return value has no type")
            names.add(return_value["type"])
    return names


def _compare(names: set[str], forbidden: frozenset[str], required: frozenset[str], subject: str) -> list[str]:
    violations = [f"{subject} still exposes the upstream name {name!r}" for name in sorted(forbidden & names)]
    violations.extend(f"{subject} is missing the Foundry name {name!r}" for name in sorted(required - names))
    return violations


def find_naming_violations(doc_names: set[str], interface_names: set[str]) -> list[str]:
    """Return every naming violation across the glTF docs and the interface."""

    return [
        *_compare(doc_names, FORBIDDEN_GLTF_DOC_NAMES, REQUIRED_GLTF_DOC_NAMES, "the glTF class reference"),
        *_compare(
            interface_names,
            FORBIDDEN_INTERFACE_NAMES,
            REQUIRED_INTERFACE_NAMES,
            "the extension interface",
        ),
    ]


def load_interface_document(text: str) -> Any:
    try:
        return json.loads(text)
    except json.JSONDecodeError as error:
        raise ExtensionApiNamingError(f"the extension interface is not valid JSON: {error}") from error


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Cross-reference Foundry's public API names.")
    parser.add_argument("--root", type=Path, default=DEFAULT_ROOT, help="repository to inspect")
    args = parser.parse_args(argv)
    root = args.root.resolve()

    doc_names = collect_doc_names((root / path).read_text(encoding="utf-8") for path in GLTF_DOC_PATHS)
    interface_names = collect_interface_names(
        load_interface_document((root / INTERFACE_JSON_PATH).read_text(encoding="utf-8"))
    )

    violations = find_naming_violations(doc_names, interface_names)
    if violations:
        print(f"Extension API naming failed ({len(violations)} violations):", file=sys.stderr)
        for violation in violations:
            print(f"- {violation}", file=sys.stderr)
        return 1

    print("Extension API naming check passed.")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except ExtensionApiNamingError as error:
        print(f"Extension API naming inputs are unusable: {error}", file=sys.stderr)
        sys.exit(2)
