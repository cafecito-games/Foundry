# This file is part of Foundry Engine - https://www.cafecito.games/
# Foundry Engine is a fork of the Godot Engine; see NOTICE.
# Copyright (c) 2026-present Cafecito Games LLC. MIT License.

"""Unit tests for misc/checks/check_doc_class_registration.py.

The pure comparison lives in `find_mismatches`; the directory walk is exercised
against synthetic module trees so the guard is proven to fire without depending
on the real `modules/` layout. One test runs the check over the real repository
to keep the checked-in tree honest.
"""

from __future__ import annotations

import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(REPO_ROOT / "misc/checks"))

import check_doc_class_registration  # noqa: E402

CHECK_SCRIPT = REPO_ROOT / "misc/checks/check_doc_class_registration.py"

CONFIG_TEMPLATE = """\
def get_doc_classes():
    return {classes!r}


def get_doc_path():
    return "doc_classes"
"""


def write_module(modules_root: Path, name: str, config_source: str, doc_files: list[str]) -> None:
    module_directory = modules_root / name
    module_directory.mkdir(parents=True)
    (module_directory / "config.py").write_text(config_source)
    if doc_files:
        doc_directory = module_directory / "doc_classes"
        doc_directory.mkdir()
        for doc_file in doc_files:
            (doc_directory / f"{doc_file}.xml").write_text("<class/>\n")


class FindMismatchesTests(unittest.TestCase):
    def test_matching_lists_report_nothing(self):
        self.assertEqual(check_doc_class_registration.find_mismatches("demo", ["A", "B"], ["A", "B"]), [])

    def test_unlisted_file_is_reported_as_a_relocation_risk(self):
        messages = check_doc_class_registration.find_mismatches("demo", ["A"], ["A", "B"])
        self.assertEqual(len(messages), 1)
        self.assertIn("'B'", messages[0])
        self.assertIn("relocate", messages[0])

    def test_listed_name_without_a_file_is_reported_as_stale(self):
        messages = check_doc_class_registration.find_mismatches("demo", ["A", "B"], ["A"])
        self.assertEqual(len(messages), 1)
        self.assertIn("'B'", messages[0])
        self.assertIn("stale", messages[0])

    def test_every_mismatch_is_reported_not_just_the_first(self):
        messages = check_doc_class_registration.find_mismatches("demo", ["A", "X", "Y"], ["A", "B", "C"])
        self.assertEqual(len(messages), 4)

    def test_qualified_names_compare_by_full_basename(self):
        self.assertEqual(check_doc_class_registration.find_mismatches("demo", ["ns.pkg.Thing"], ["ns.pkg.Thing"]), [])
        messages = check_doc_class_registration.find_mismatches("demo", ["Thing"], ["ns.pkg.Thing"])
        self.assertEqual(len(messages), 2)


class CheckRepositoryTests(unittest.TestCase):
    def test_clean_tree_has_no_mismatches(self):
        with tempfile.TemporaryDirectory() as scratch:
            modules_root = Path(scratch)
            write_module(modules_root, "clean", CONFIG_TEMPLATE.format(classes=["A", "B"]), ["A", "B"])
            self.assertEqual(check_doc_class_registration.check_repository(modules_root), [])

    def test_module_without_doc_classes_is_skipped(self):
        with tempfile.TemporaryDirectory() as scratch:
            modules_root = Path(scratch)
            write_module(modules_root, "plain", "def can_build(env, platform):\n    return True\n", [])
            self.assertEqual(check_doc_class_registration.check_repository(modules_root), [])

    def test_doc_files_without_get_doc_classes_are_reported(self):
        with tempfile.TemporaryDirectory() as scratch:
            modules_root = Path(scratch)
            write_module(modules_root, "orphan", "def can_build(env, platform):\n    return True\n", ["A"])
            messages = check_doc_class_registration.check_repository(modules_root)
            self.assertEqual(len(messages), 1)
            self.assertIn("no get_doc_classes()", messages[0])

    def test_missing_get_doc_path_is_reported(self):
        with tempfile.TemporaryDirectory() as scratch:
            modules_root = Path(scratch)
            write_module(modules_root, "nopath", 'def get_doc_classes():\n    return ["A"]\n', ["A"])
            messages = check_doc_class_registration.check_repository(modules_root)
            self.assertEqual(len(messages), 1)
            self.assertIn("no get_doc_path()", messages[0])

    def test_unimportable_config_is_reported_and_other_modules_still_checked(self):
        with tempfile.TemporaryDirectory() as scratch:
            modules_root = Path(scratch)
            write_module(modules_root, "broken", "raise RuntimeError('boom')\n", ["A"])
            write_module(modules_root, "drifted", CONFIG_TEMPLATE.format(classes=["A"]), ["A", "B"])
            messages = check_doc_class_registration.check_repository(modules_root)
            self.assertEqual(len(messages), 2)
            self.assertTrue(any("cannot import config.py" in message for message in messages))
            self.assertTrue(any("'B'" in message for message in messages))


class CommandLineTests(unittest.TestCase):
    def run_check(self, modules_root: Path) -> subprocess.CompletedProcess[str]:
        return subprocess.run(
            [sys.executable, str(CHECK_SCRIPT), "--modules-root", str(modules_root)],
            capture_output=True,
            text=True,
            check=False,
        )

    def test_exits_zero_on_a_clean_tree(self):
        with tempfile.TemporaryDirectory() as scratch:
            modules_root = Path(scratch)
            write_module(modules_root, "clean", CONFIG_TEMPLATE.format(classes=["A"]), ["A"])
            result = self.run_check(modules_root)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(result.stderr, "")

    def test_exits_non_zero_and_reports_on_drift(self):
        with tempfile.TemporaryDirectory() as scratch:
            modules_root = Path(scratch)
            write_module(modules_root, "drifted", CONFIG_TEMPLATE.format(classes=["A"]), ["A", "B"])
            result = self.run_check(modules_root)
            self.assertEqual(result.returncode, 1)
            self.assertIn("'B'", result.stderr)
            self.assertIn("1 doc class registration mismatch(es) found.", result.stderr)

    def test_repository_modules_are_registered_consistently(self):
        result = subprocess.run(
            [sys.executable, str(CHECK_SCRIPT)],
            capture_output=True,
            text=True,
            check=False,
        )
        self.assertEqual(result.returncode, 0, result.stderr)


if __name__ == "__main__":
    unittest.main()
