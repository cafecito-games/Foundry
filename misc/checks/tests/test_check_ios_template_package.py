# This file is part of Foundry Engine - https://www.cafecito.games/
# Foundry Engine is a fork of the Godot Engine; see NOTICE.
# Copyright (c) 2026-present Cafecito Games LLC. MIT License.

"""Unit tests for misc/checks/check_ios_template_package.py.

These drive the check's pure archive inspection with synthetic zip entry names,
so each violation class is proven to fire without running the real packager.
"""

from __future__ import annotations

import sys
import unittest
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(REPO_ROOT / "misc/checks"))

import check_ios_template_package  # noqa: E402

VISIONOS_ENTRY = "ios_xcode/libfoundry.visionos.release.xcframework/xros-arm64/libfoundry.a"


class ArchiveViolationTests(unittest.TestCase):
    def clean_entries(self) -> set[str]:
        return set(check_ios_template_package.EXPECTED_ARCHIVE_ENTRIES)

    def test_complete_ios_only_archive_reports_no_violations(self) -> None:
        self.assertEqual(check_ios_template_package.find_archive_violations(self.clean_entries()), [])

    def test_each_missing_slice_is_reported(self) -> None:
        for entry in check_ios_template_package.EXPECTED_ARCHIVE_ENTRIES:
            with self.subTest(entry=entry):
                names = self.clean_entries() - {entry}

                violations = check_ios_template_package.find_archive_violations(names)

                self.assertEqual(len(violations), 1)
                self.assertIn("missing expected entries", violations[0])
                self.assertIn(entry, violations[0])

    def test_non_ios_frameworks_are_reported(self) -> None:
        names = self.clean_entries() | {VISIONOS_ENTRY}

        violations = check_ios_template_package.find_archive_violations(names)

        self.assertEqual(len(violations), 1)
        self.assertIn("non-iOS framework entries", violations[0])
        self.assertIn(VISIONOS_ENTRY, violations[0])

    def test_every_violation_is_reported_not_just_the_first(self) -> None:
        dropped = sorted(check_ios_template_package.EXPECTED_ARCHIVE_ENTRIES)[0]
        names = (self.clean_entries() - {dropped}) | {VISIONOS_ENTRY}

        violations = check_ios_template_package.find_archive_violations(names)

        self.assertEqual(len(violations), 2)
        self.assertIn("missing expected entries", violations[0])
        self.assertIn("non-iOS framework entries", violations[1])

    def test_empty_archive_reports_every_missing_slice(self) -> None:
        violations = check_ios_template_package.find_archive_violations(set())

        self.assertEqual(len(violations), 1)
        for entry in check_ios_template_package.EXPECTED_ARCHIVE_ENTRIES:
            self.assertIn(entry, violations[0])


if __name__ == "__main__":
    unittest.main()
