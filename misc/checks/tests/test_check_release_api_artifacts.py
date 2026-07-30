# This file is part of Foundry Engine - https://www.cafecito.games/
# Foundry Engine is a fork of the Godot Engine; see NOTICE.
# Copyright (c) 2026-present Cafecito Games LLC. MIT License.

"""Unit tests for misc/checks/check_release_api_artifacts.py.

These drive the check's pure bundle and workflow inspection with synthetic
inputs, so each violation class is proven to fire without running the packager.
"""

from __future__ import annotations

import sys
import unittest
from collections.abc import Iterable
from pathlib import Path
from typing import Any

REPO_ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(REPO_ROOT / "misc/checks"))

import check_release_api_artifacts  # noqa: E402

VERSION = "0.1.0-alpha.1"
TAG = "v0.1.0-alpha.1"
COMMIT = "abc123"
HEADER_FULL_NAME = "Foundry v0.1.alpha1.test_build"

PAYLOAD_ENTRIES = sorted(check_release_api_artifacts.EXPECTED_BUNDLE_ENTRIES - {"metadata.json"})


def clean_metadata() -> dict[str, Any]:
    return {
        "version": VERSION,
        "tag": TAG,
        "commit": COMMIT,
        "extension_api_header": {"version_full_name": HEADER_FULL_NAME},
        "files": [{"name": name, "sha256": "0" * 64} for name in PAYLOAD_ENTRIES],
    }


def find_violations(names: Iterable[str] | None = None, metadata: dict[str, Any] | None = None) -> list[str]:
    violations: list[str] = check_release_api_artifacts.find_bundle_violations(
        check_release_api_artifacts.EXPECTED_BUNDLE_ENTRIES if names is None else names,
        clean_metadata() if metadata is None else metadata,
        version=VERSION,
        tag=TAG,
        commit=COMMIT,
        header_full_name=HEADER_FULL_NAME,
    )
    return violations


class BundleViolationTests(unittest.TestCase):
    def test_clean_bundle_reports_no_violations(self) -> None:
        self.assertEqual(find_violations(), [])

    def test_unexpected_zip_entries_are_reported(self) -> None:
        names = set(check_release_api_artifacts.EXPECTED_BUNDLE_ENTRIES) | {"stowaway.txt"}

        violations = find_violations(names=names)

        self.assertEqual(len(violations), 1)
        self.assertIn("unexpected zip entries", violations[0])
        self.assertIn("stowaway.txt", violations[0])

    def test_each_metadata_identity_mismatch_is_reported(self) -> None:
        for field in ("version", "tag", "commit"):
            with self.subTest(field=field):
                metadata = clean_metadata()
                metadata[field] = "wrong"

                violations = find_violations(metadata=metadata)

                self.assertEqual(len(violations), 1)
                self.assertIn(f"metadata {field} mismatch", violations[0])

    def test_dropped_extension_api_header_is_reported(self) -> None:
        metadata = clean_metadata()
        metadata["extension_api_header"] = {}

        violations = find_violations(metadata=metadata)

        self.assertEqual(violations, ["metadata did not preserve extension API header"])

    def test_metadata_file_inventory_mismatch_is_reported(self) -> None:
        metadata = clean_metadata()
        metadata["files"] = metadata["files"][:-1]

        violations = find_violations(metadata=metadata)

        self.assertEqual(len(violations), 1)
        self.assertIn("metadata file inventory mismatch", violations[0])

    def test_short_sha256_digests_are_reported(self) -> None:
        metadata = clean_metadata()
        metadata["files"][0]["sha256"] = "deadbeef"

        violations = find_violations(metadata=metadata)

        self.assertEqual(len(violations), 1)
        self.assertIn("sha256 values are not hex digests", violations[0])
        self.assertIn(PAYLOAD_ENTRIES[0], violations[0])

    def test_every_violation_is_reported_not_just_the_first(self) -> None:
        metadata = clean_metadata()
        metadata["version"] = "wrong"
        metadata["commit"] = "wrong"
        metadata["files"][0]["sha256"] = "deadbeef"
        names = set(check_release_api_artifacts.EXPECTED_BUNDLE_ENTRIES) | {"stowaway.txt"}

        violations = find_violations(names=names, metadata=metadata)

        self.assertEqual(len(violations), 4)

    def test_empty_metadata_is_reported_rather_than_raising(self) -> None:
        violations = find_violations(names=set(), metadata={})

        self.assertEqual(len(violations), 6)


class WorkflowWiringTests(unittest.TestCase):
    def test_clean_workflow_reports_no_violations(self) -> None:
        workflow = "\n".join(check_release_api_artifacts.REQUIRED_WORKFLOW_SNIPPETS)

        self.assertEqual(check_release_api_artifacts.find_workflow_wiring_violations(workflow), [])

    def test_each_missing_snippet_is_reported(self) -> None:
        snippets = check_release_api_artifacts.REQUIRED_WORKFLOW_SNIPPETS
        for snippet in snippets:
            with self.subTest(snippet=snippet):
                workflow = "\n".join(other for other in snippets if other != snippet)

                violations = check_release_api_artifacts.find_workflow_wiring_violations(workflow)

                self.assertEqual(len(violations), 1)
                self.assertIn(snippet, violations[0])

    def test_empty_workflow_reports_every_missing_snippet(self) -> None:
        violations = check_release_api_artifacts.find_workflow_wiring_violations("")

        self.assertEqual(len(violations), 1)
        for snippet in check_release_api_artifacts.REQUIRED_WORKFLOW_SNIPPETS:
            self.assertIn(snippet, violations[0])

    def test_the_real_release_workflow_satisfies_the_check(self) -> None:
        workflow = (REPO_ROOT / ".github/workflows/release.yml").read_text(encoding="utf-8")

        self.assertEqual(check_release_api_artifacts.find_workflow_wiring_violations(workflow), [])


if __name__ == "__main__":
    unittest.main()
