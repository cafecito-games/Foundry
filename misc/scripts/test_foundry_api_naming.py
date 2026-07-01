#!/usr/bin/env python3
from __future__ import annotations

import json
import re
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]


def fail(message: str) -> None:
    print(message, file=sys.stderr)
    sys.exit(1)


def assert_not_contains(path: Path, forbidden: list[str]) -> None:
    text = path.read_text()
    hits = [token for token in forbidden if token in text]
    if hits:
        fail(f"{path.relative_to(REPO_ROOT)} still contains forbidden API naming: {hits}")


def assert_contains(path: Path, required: list[str]) -> None:
    text = path.read_text()
    missing = [token for token in required if token not in text]
    if missing:
        fail(f"{path.relative_to(REPO_ROOT)} is missing expected Foundry API naming: {missing}")


def collect_gltf_doc_names(paths: list[Path]) -> set[str]:
    names: set[str] = set()
    attribute_re = re.compile(r'\b(?:name|setter|getter)="([^"]+)"')
    for path in paths:
        names.update(attribute_re.findall(path.read_text()))
    return names


def collect_interface_names(path: Path) -> set[str]:
    data = json.loads(path.read_text())
    names: set[str] = set()
    for type_data in data["types"]:
        names.add(type_data["name"])
        for member in type_data.get("members", []):
            names.add(member["name"])
        for argument in type_data.get("arguments", []):
            argument_name = argument.get("name")
            if argument_name:
                names.add(argument_name)
            names.add(argument["type"])
    for interface in data["interface"]:
        names.add(interface["name"])
        for argument in interface.get("arguments", []):
            names.add(argument["name"])
            names.add(argument["type"])
        return_value = interface.get("return_value")
        if return_value:
            names.add(return_value["type"])
    return names


def test_gltf_public_api_uses_foundry_names() -> None:
    docs = [
        REPO_ROOT / "modules/gltf/doc_classes/GLTFDocument.xml",
        REPO_ROOT / "modules/gltf/doc_classes/GLTFDocumentExtension.xml",
        REPO_ROOT / "modules/gltf/doc_classes/GLTFObjectModelProperty.xml",
        REPO_ROOT / "modules/gltf/doc_classes/GLTFSkeleton.xml",
        REPO_ROOT / "modules/gltf/doc_classes/GLTFSkin.xml",
        REPO_ROOT / "modules/gltf/doc_classes/GLTFState.xml",
    ]
    forbidden_doc_names = {
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
    foundry_doc_names = {
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
    names = collect_gltf_doc_names(docs)
    leaked = sorted(forbidden_doc_names & names)
    if leaked:
        fail(f"GLTF class docs still expose Godot names: {leaked}")
    missing = sorted(foundry_doc_names - names)
    if missing:
        fail(f"GLTF class docs are missing Foundry names: {missing}")

    gltf_sources = [
        REPO_ROOT / "modules/gltf/gltf_document.cpp",
        REPO_ROOT / "modules/gltf/gltf_state.cpp",
        REPO_ROOT / "modules/gltf/extensions/gltf_document_extension.cpp",
        REPO_ROOT / "modules/gltf/structures/gltf_object_model_property.cpp",
        REPO_ROOT / "modules/gltf/structures/gltf_skeleton.cpp",
        REPO_ROOT / "modules/gltf/structures/gltf_skin.cpp",
    ]
    forbidden_binding_strings = [
        'D_METHOD("export_object_model_property", "state", "node_path", "godot_node"',
        'FOUNDRY_VIRTUAL_BIND(_export_object_model_property, "state", "node_path", "godot_node"',
        'D_METHOD("append_gltf_node", "gltf_node", "godot_scene_node"',
        'D_METHOD("get_gltf_to_godot_expression"',
        'D_METHOD("set_gltf_to_godot_expression"',
        'D_METHOD("get_godot_to_gltf_expression"',
        'D_METHOD("set_godot_to_gltf_expression"',
        '"gltf_to_godot_expression"',
        '"godot_to_gltf_expression"',
        'D_METHOD("get_godot_skeleton"',
        'D_METHOD("get_godot_bone_node"',
        'D_METHOD("set_godot_bone_node"',
        '"godot_bone_node"',
        'D_METHOD("get_godot_skin"',
        'D_METHOD("set_godot_skin"',
        '"godot_skin"',
    ]
    for source in gltf_sources:
        assert_not_contains(source, forbidden_binding_strings)


def test_foundry_extension_c_interface_uses_foundry_names() -> None:
    interface_json = REPO_ROOT / "core/extension/foundry_extension_interface.json"
    forbidden_interface_names = {
        "FoundryExtensionGodotVersion",
        "FoundryExtensionGodotVersion2",
        "get_godot_version",
        "get_godot_version2",
        "r_godot_version",
        "FoundryExtensionGodotVersion*",
        "FoundryExtensionGodotVersion2*",
    }
    foundry_interface_names = {
        "FoundryExtensionFoundryVersion",
        "FoundryExtensionFoundryVersion2",
        "get_foundry_version",
        "get_foundry_version2",
        "r_foundry_version",
        "FoundryExtensionFoundryVersion*",
        "FoundryExtensionFoundryVersion2*",
    }
    names = collect_interface_names(interface_json)
    leaked = sorted(forbidden_interface_names & names)
    if leaked:
        fail(f"FoundryExtension interface JSON still exposes Godot names: {leaked}")
    missing = sorted(foundry_interface_names - names)
    if missing:
        fail(f"FoundryExtension interface JSON is missing Foundry names: {missing}")

    assert_not_contains(
        interface_json,
        [
            "GODOT ENGINE",
            "https://godotengine.org",
            "interface to Godot",
            "Godot's built-in debugger",
            "built-in godot class",
            "recompile Godot",
            "Gets the Godot version",
            "Godot is started",
            "Godot is shutdown",
        ],
    )
    assert_contains(interface_json, ["FOUNDRY ENGINE", "https://docs.cafecito.games/foundry", "interface to Foundry"])

    assert_not_contains(
        REPO_ROOT / "core/extension/foundry_extension_interface.cpp",
        [
            "foundry_extension_get_godot_version",
            "REGISTER_INTERFACE_FUNC(get_godot_version",
            "FoundryExtensionGodotVersion",
            "r_godot_version",
        ],
    )
    assert_contains(
        REPO_ROOT / "core/extension/foundry_extension_interface.cpp",
        [
            "foundry_extension_get_foundry_version",
            "REGISTER_INTERFACE_FUNC(get_foundry_version",
            "FoundryExtensionFoundryVersion",
            "r_foundry_version",
        ],
    )

    for generator in [
        REPO_ROOT / "core/extension/make_interface_header.py",
        REPO_ROOT / "core/extension/foundry_extension_interface_header_generator.cpp",
    ]:
        assert_not_contains(generator, ["Deprecated in Godot"])
        assert_contains(generator, ["Deprecated in Foundry"])


def test_embedded_instance_docs_use_foundry_names() -> None:
    for path in [
        REPO_ROOT / "doc/classes/FoundryExtensionManager.xml",
        REPO_ROOT / "doc/classes/FoundryInstance.xml",
    ]:
        assert_not_contains(
            path,
            ["libgodot://", "embedded Godot instance", "running Godot instance", "libgodot_create_foundry_instance"],
        )
    assert_contains(REPO_ROOT / "doc/classes/FoundryExtensionManager.xml", ["libfoundry://"])
    assert_contains(
        REPO_ROOT / "doc/classes/FoundryInstance.xml",
        ["embedded Foundry instance", "running Foundry instance", "libfoundry_create_foundry_instance"],
    )


def test_extension_api_doc_prose_uses_foundry_names() -> None:
    global_scope = REPO_ROOT / "doc/classes/@GlobalScope.xml"
    assert_not_contains(global_scope, ["Godot's built-in debugger"])
    assert_contains(global_scope, ["Foundry's built-in debugger"])


def main() -> None:
    test_gltf_public_api_uses_foundry_names()
    test_foundry_extension_c_interface_uses_foundry_names()
    test_embedded_instance_docs_use_foundry_names()
    test_extension_api_doc_prose_uses_foundry_names()
    print("Foundry API naming tests passed")


if __name__ == "__main__":
    main()
