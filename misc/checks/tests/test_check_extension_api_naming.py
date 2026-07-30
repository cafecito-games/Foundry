# This file is part of Foundry Engine - https://www.cafecito.games/
# Foundry Engine is a fork of the Godot Engine; see NOTICE.
# Copyright (c) 2026-present Cafecito Games LLC. MIT License.

"""Unit tests for misc/checks/check_extension_api_naming.py.

The check extracts name sets from structured inputs rather than grepping, so
these tests feed it synthetic documents and prove that a name which merely moved
to a different member is still found.
"""

from __future__ import annotations

import subprocess
import sys
import unittest
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(REPO_ROOT / "misc/checks"))

import check_extension_api_naming  # noqa: E402

CHECK_SCRIPT = REPO_ROOT / "misc/checks/check_extension_api_naming.py"


class ExtensionApiNamingTests(unittest.TestCase):
    def test_interface_names_are_collected_from_every_type_and_member(self) -> None:
        document = {
            "types": [
                {
                    "name": "FoundryExtensionFoundryVersion",
                    "members": [{"name": "major"}, {"name": "minor"}],
                    "arguments": [{"name": "r_foundry_version", "type": "FoundryExtensionFoundryVersion*"}],
                }
            ],
            "interface": [
                {
                    "name": "get_foundry_version",
                    "arguments": [{"name": "r_version", "type": "FoundryExtensionFoundryVersion*"}],
                    "return_value": {"type": "FoundryBool"},
                }
            ],
        }

        names = check_extension_api_naming.collect_interface_names(document)

        self.assertEqual(
            {
                "FoundryExtensionFoundryVersion",
                "FoundryExtensionFoundryVersion*",
                "FoundryBool",
                "major",
                "minor",
                "r_foundry_version",
                "get_foundry_version",
                "r_version",
            },
            names,
        )

    def test_doc_names_are_collected_from_name_setter_and_getter_attributes(self) -> None:
        document = """
        <class name="GLTFSkin">
          <member name="foundry_skin" setter="set_foundry_skin" getter="get_foundry_skin" />
          <method name="append_gltf_node">
            <param index="0" name="foundry_scene_node" type="Node" />
          </method>
        </class>
        """

        names = check_extension_api_naming.collect_doc_names([document])

        self.assertEqual(
            {
                "GLTFSkin",
                "foundry_skin",
                "set_foundry_skin",
                "get_foundry_skin",
                "append_gltf_node",
                "foundry_scene_node",
            },
            names,
        )

    def test_mismatched_name_between_interface_and_docs_is_reported(self) -> None:
        doc_names = set(check_extension_api_naming.REQUIRED_GLTF_DOC_NAMES)
        doc_names.remove("foundry_skin")
        doc_names.add("godot_skin")
        interface_names = set(check_extension_api_naming.REQUIRED_INTERFACE_NAMES)
        interface_names.remove("get_foundry_version")
        interface_names.add("get_godot_version")

        violations = check_extension_api_naming.find_naming_violations(doc_names, interface_names)

        self.assertEqual(4, len(violations))
        joined = "\n".join(violations)
        self.assertIn("'godot_skin'", joined)
        self.assertIn("'foundry_skin'", joined)
        self.assertIn("'get_godot_version'", joined)
        self.assertIn("'get_foundry_version'", joined)

    def test_a_name_that_merely_moved_to_another_member_is_still_found(self) -> None:
        # The point of comparing name sets rather than grepping: relocating a
        # forbidden name to a different member must not hide it.
        document = {
            "types": [{"name": "FoundryExtensionFoundryVersion", "members": [{"name": "r_godot_version"}]}],
            "interface": [],
        }

        names = check_extension_api_naming.collect_interface_names(document)

        self.assertIn("r_godot_version", names)

    def test_every_violation_is_reported_not_just_the_first(self) -> None:
        violations = check_extension_api_naming.find_naming_violations(set(), set())

        self.assertEqual(
            len(check_extension_api_naming.REQUIRED_GLTF_DOC_NAMES)
            + len(check_extension_api_naming.REQUIRED_INTERFACE_NAMES),
            len(violations),
        )

    def test_clean_name_sets_report_nothing(self) -> None:
        violations = check_extension_api_naming.find_naming_violations(
            set(check_extension_api_naming.REQUIRED_GLTF_DOC_NAMES),
            set(check_extension_api_naming.REQUIRED_INTERFACE_NAMES),
        )

        self.assertEqual([], violations)

    def test_malformed_interface_json_fails_with_a_clear_message(self) -> None:
        with self.assertRaisesRegex(check_extension_api_naming.ExtensionApiNamingError, "not valid JSON"):
            check_extension_api_naming.load_interface_document("{'types': [}")

    def test_interface_without_a_types_list_fails_with_a_clear_message(self) -> None:
        with self.assertRaisesRegex(check_extension_api_naming.ExtensionApiNamingError, "'types'"):
            check_extension_api_naming.collect_interface_names({"interface": []})

    def test_unnamed_interface_function_fails_with_a_clear_message(self) -> None:
        with self.assertRaisesRegex(check_extension_api_naming.ExtensionApiNamingError, "no name"):
            check_extension_api_naming.collect_interface_names({"types": [], "interface": [{"arguments": []}]})

    def test_check_passes_against_this_repository(self) -> None:
        result = subprocess.run(
            [sys.executable, str(CHECK_SCRIPT)],
            capture_output=True,
            text=True,
            check=False,
        )

        self.assertEqual(0, result.returncode, result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()
