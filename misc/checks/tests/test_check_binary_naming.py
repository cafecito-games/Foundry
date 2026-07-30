# This file is part of Foundry Engine - https://www.cafecito.games/
# Foundry Engine is a fork of the Godot Engine; see NOTICE.
# Copyright (c) 2026-present Cafecito Games LLC. MIT License.

"""Unit tests for misc/checks/check_binary_naming.py.

The check itself needs a compiled binary. These tests drive its pure token
matcher with synthetic `strings`/`nm` output, so the guard is proven to fire for
every token in the policy without a build.
"""

from __future__ import annotations

import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(REPO_ROOT / "misc/checks"))

import check_binary_naming  # noqa: E402

CHECK_SCRIPT = REPO_ROOT / "misc/checks/check_binary_naming.py"

# Shaped like real `strings -a` output from a clean Foundry binary: plausible
# neighbors of the forbidden tokens that must not themselves trip the check.
CLEAN_STRINGS_OUTPUT = """\
FoundryScriptInternal
ENetFoundrySocket
foundry_packHalf2x16
org.cafecitogames.foundry.popup_window
Foundry Shading Language
Foundry Engine/1.0
user://logs/foundry.log
Open Foundry online documentation
"""

CLEAN_SYMBOL_OUTPUT = """\
0000000100000000 T _foundry_init_profiler
0000000100000010 T _foundry_unzip_open
0000000100000020 T __ZN14FoundryScript6parseEv
"""


class ForbiddenTokenMatchingTests(unittest.TestCase):
    def test_clean_scan_output_passes(self) -> None:
        scan_text = f"{CLEAN_STRINGS_OUTPUT}\n{CLEAN_SYMBOL_OUTPUT}"

        self.assertEqual(check_binary_naming.find_forbidden_tokens(scan_text), [])

    def test_each_forbidden_token_is_detected_in_strings_output(self) -> None:
        for token in check_binary_naming.FORBIDDEN_BINARY_STRINGS:
            with self.subTest(token=token):
                scan_text = f"{CLEAN_STRINGS_OUTPUT}{token}\n\n{CLEAN_SYMBOL_OUTPUT}"

                self.assertEqual(check_binary_naming.find_forbidden_tokens(scan_text), [token])

    def test_each_forbidden_token_is_detected_in_symbol_output(self) -> None:
        for token in check_binary_naming.FORBIDDEN_BINARY_STRINGS:
            with self.subTest(token=token):
                scan_text = f"{CLEAN_STRINGS_OUTPUT}\n{CLEAN_SYMBOL_OUTPUT}0000000100000030 T _{token}\n"

                self.assertEqual(check_binary_naming.find_forbidden_tokens(scan_text), [token])

    def test_every_leaked_token_is_reported_not_just_the_first(self) -> None:
        leaked = ["godotDelegate", "Waiting for Godot", "Godot v"]
        scan_text = CLEAN_STRINGS_OUTPUT + "".join(f"{token}\n" for token in leaked)

        found = check_binary_naming.find_forbidden_tokens(scan_text)

        self.assertEqual(sorted(found), sorted(leaked))

    def test_policy_has_no_duplicate_tokens(self) -> None:
        tokens = check_binary_naming.FORBIDDEN_BINARY_STRINGS

        self.assertEqual(len(tokens), len(set(tokens)))


class CommandLineTests(unittest.TestCase):
    def run_check(self, *arguments: str) -> subprocess.CompletedProcess[str]:
        return subprocess.run(
            [sys.executable, str(CHECK_SCRIPT), *arguments],
            capture_output=True,
            text=True,
            check=False,
        )

    def test_missing_binary_argument_fails_with_a_clear_message(self) -> None:
        result = self.run_check()

        self.assertNotEqual(result.returncode, 0)
        self.assertIn("binary", result.stderr)

    def test_nonexistent_binary_path_fails_with_a_clear_message(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            missing = Path(temporary_directory) / "does-not-exist"

            result = self.run_check(str(missing))

            self.assertEqual(result.returncode, 1)
            self.assertIn("Binary not found", result.stderr)
            self.assertIn(str(missing), result.stderr)


if __name__ == "__main__":
    unittest.main()
