# This file is part of Foundry Engine - https://www.cafecito.games/
# Foundry Engine is a fork of the Godot Engine; see NOTICE.
# Copyright (c) 2026-present Cafecito Games LLC. MIT License.

"""Behavioral tests for misc/scripts/copyright_headers.py.

Run with: python3 -m unittest scripts.tests.test_copyright_headers
or:       python3 scripts/tests/test_copyright_headers.py
"""

from __future__ import annotations

import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
HEADER_SCRIPT = REPO_ROOT / "misc/scripts/copyright_headers.py"
CREATE_TEST_SCRIPT = REPO_ROOT / "tests/create_test.py"

INHERITED_GODOT_SOURCE = """/**************************************************************************/
/*  inherited.cpp                                                         */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/
/* Copyright (c) 2014-present Godot Engine contributors (see AUTHORS.md). */
/* Copyright (c) 2007-2014 Juan Linietsky, Ariel Manzur.                  */
/**************************************************************************/

int inherited() { return 1; }
"""


class CopyrightHeaderTests(unittest.TestCase):
    def setUp(self) -> None:
        temporary_directory = tempfile.TemporaryDirectory()
        self.addCleanup(temporary_directory.cleanup)
        self.temporary_path = Path(temporary_directory.name)

    def run_script(self, script: Path, *arguments: Path | str) -> subprocess.CompletedProcess[str]:
        result = subprocess.run(
            [sys.executable, str(script), *[str(argument) for argument in arguments]],
            cwd=REPO_ROOT,
            capture_output=True,
            text=True,
            check=False,
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        return result

    def test_new_file_gets_foundry_engine_header(self) -> None:
        source = self.temporary_path / "net_new.cpp"
        source.write_text("int answer() { return 42; }\n", encoding="utf-8")

        self.run_script(HEADER_SCRIPT, source)

        text = source.read_text(encoding="utf-8")
        self.assertIn("FOUNDRY ENGINE", text)
        self.assertIn("A fork of the Godot Engine", text)
        self.assertIn("Copyright (c) 2026-present Cafecito Games LLC.", text)
        self.assertNotIn("Godot Engine contributors", text)
        self.assertNotIn("Juan Linietsky", text)
        self.assertTrue(text.endswith("int answer() { return 42; }\n"))

    def test_inherited_godot_header_does_not_gain_cafecito_copyright(self) -> None:
        source = self.temporary_path / "inherited.cpp"
        source.write_text(INHERITED_GODOT_SOURCE, encoding="utf-8")

        self.run_script(HEADER_SCRIPT, source)

        text = source.read_text(encoding="utf-8")
        self.assertIn("GODOT ENGINE", text)
        self.assertIn("Godot Engine contributors", text)
        self.assertIn("Juan Linietsky", text)
        self.assertNotIn("Cafecito Games LLC", text)
        self.assertTrue(text.endswith("int inherited() { return 1; }\n"))

    def test_create_test_uses_foundry_engine_header(self) -> None:
        self.run_script(CREATE_TEST_SCRIPT, "HeaderPolicy", self.temporary_path)

        text = (self.temporary_path / "test_header_policy.h").read_text(encoding="utf-8")
        self.assertIn("FOUNDRY ENGINE", text)
        self.assertIn("A fork of the Godot Engine", text)
        self.assertIn("Copyright (c) 2026-present Cafecito Games LLC.", text)
        self.assertNotIn("Godot Engine contributors", text)
        self.assertNotIn("Juan Linietsky", text)


if __name__ == "__main__":
    unittest.main()
