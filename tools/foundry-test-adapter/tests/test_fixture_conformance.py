# This file is part of Foundry — https://www.cafecito.games/
# Copyright (c) 2026-present Cafecito Games. MIT License.
"""Runs the conformance validator against every checked-in normative fixture.

The manifest at `fixtures/v1/expectations.json` is the hand-written statement of
what each fixture proves. Downstream implementations reuse the same fixtures and
manifest, so a drift between the validator and the manifest is a protocol
regression rather than a test-maintenance chore.
"""

from __future__ import annotations

import json
import unittest
from pathlib import Path
from typing import Any

from foundry_test_adapter import (
    expectations_path,
    fixtures_root,
    validate_capabilities_document,
    validate_discovery_stream,
    validate_run,
    validate_tap_report,
)
from foundry_test_adapter.run import expected_leaf_ids
from foundry_test_adapter.violations import Violation


def _load_manifest() -> dict[str, Any]:
    manifest: dict[str, Any] = json.loads(expectations_path().read_text(encoding="utf-8"))
    return manifest


def _read(relative_path: str) -> str:
    return (fixtures_root() / relative_path).read_text(encoding="utf-8")


def _signatures(violations: list[Violation]) -> list[tuple[str, str]]:
    return sorted((violation.code, violation.location) for violation in violations)


def _expected_signatures(entry: dict[str, Any]) -> list[tuple[str, str]]:
    return sorted((item["code"], item["location"]) for item in entry["violations"])


MANIFEST = _load_manifest()


class CapabilitiesFixtureTests(unittest.TestCase):
    def test_every_capabilities_fixture_matches_the_manifest(self) -> None:
        for relative_path, entry in MANIFEST["capabilities"].items():
            with self.subTest(fixture=relative_path):
                result = validate_capabilities_document(_read("capabilities/" + relative_path))
                self.assertEqual(_expected_signatures(entry), _signatures(result.violations))
                if "supported_versions" in entry:
                    self.assertEqual(tuple(entry["supported_versions"]), result.supported_versions)
                if "framework_id" in entry:
                    self.assertEqual(entry["framework_id"], result.framework_id)

    def test_a_conforming_document_negotiates_the_highest_shared_version(self) -> None:
        result = validate_capabilities_document(_read("capabilities/valid/multi_version.json"))
        self.assertEqual(2, result.negotiate((1, 2, 3)))
        self.assertEqual(1, result.negotiate((1,)))
        self.assertIsNone(result.negotiate((7,)))

    def test_an_invalid_document_never_negotiates(self) -> None:
        result = validate_capabilities_document(_read("capabilities/invalid/wrong_protocol.json"))
        self.assertIsNone(result.negotiate((1,)))


class DiscoveryFixtureTests(unittest.TestCase):
    def test_every_discovery_fixture_matches_the_manifest(self) -> None:
        for relative_path, entry in MANIFEST["discovery"].items():
            with self.subTest(fixture=relative_path):
                result = validate_discovery_stream(_read("discovery/" + relative_path))
                self.assertEqual(_expected_signatures(entry), _signatures(result.violations))
                if "complete" in entry:
                    self.assertEqual(entry["complete"], result.complete)
                if "suite_count" in entry:
                    self.assertEqual(entry["suite_count"], len(result.suites))
                if "test_count" in entry:
                    self.assertEqual(entry["test_count"], len(result.tests))
                if "error_count" in entry:
                    self.assertEqual(entry["error_count"], len(result.errors))

    def test_a_truncated_stream_still_exposes_its_flushed_records(self) -> None:
        result = validate_discovery_stream(_read("discovery/invalid/truncated.jsonl"))
        self.assertFalse(result.complete)
        self.assertEqual(["S:math"], [item.id for item in result.suites])
        self.assertEqual(["S:math::adds"], [item.id for item in result.tests])

    def test_recoverable_errors_are_interleaved_with_discovered_items(self) -> None:
        result = validate_discovery_stream(_read("discovery/valid/nested_with_error.jsonl"))
        self.assertTrue(result.conforms)
        self.assertTrue(result.complete)
        self.assertEqual("res://tests", result.root)
        self.assertEqual(["E:broken"], [error.id for error in result.errors])
        parameterized = result.item_by_id("S:math/vectors::scaled[2]")
        assert parameterized is not None
        self.assertEqual("2", parameterized.case_key)
        pending = result.item_by_id("S:math/vectors::pending")
        assert pending is not None
        self.assertTrue(pending.skipped)
        self.assertEqual("not implemented yet", pending.skip_reason)


class TapFixtureTests(unittest.TestCase):
    def test_every_tap_fixture_matches_the_manifest(self) -> None:
        for relative_path, entry in MANIFEST["tap"].items():
            with self.subTest(fixture=relative_path):
                report = validate_tap_report(_read("tap/" + relative_path))
                self.assertEqual(_expected_signatures(entry), _signatures(report.violations))
                if "plan" in entry:
                    self.assertEqual(entry["plan"], report.plan)
                if "point_count" in entry:
                    self.assertEqual(entry["point_count"], len(report.points))
                if "complete" in entry:
                    self.assertEqual(entry["complete"], report.complete)
                if "bailed_out" in entry:
                    self.assertEqual(entry["bailed_out"], report.bailed_out)

    def test_mixed_outcomes_decode_pass_failure_and_skip(self) -> None:
        report = validate_tap_report(_read("tap/valid/mixed_outcomes.tap"))
        self.assertTrue(report.conforms)
        self.assertTrue(report.complete)
        self.assertFalse(report.passed)
        passed, failed, skipped = report.points
        self.assertTrue(passed.ok)
        self.assertEqual("id-1", passed.id)
        self.assertEqual(2, passed.duration_ms)
        self.assertEqual("", passed.status_detail)
        self.assertFalse(failed.ok)
        self.assertEqual("runtime_error", failed.status_detail)
        self.assertEqual("Expected 2, got 3", failed.message)
        assert failed.at is not None
        self.assertEqual("res://tests/math_tests.fs", failed.at.file_name)
        self.assertEqual(12, failed.at.line_number)
        self.assertEqual(3, failed.at.column_number)
        self.assertTrue(skipped.skipped)
        self.assertEqual("not implemented yet", skipped.skip_reason)

    def test_a_cancelled_run_keeps_flushed_points_and_is_never_success(self) -> None:
        report = validate_tap_report(_read("tap/invalid/plan_unsatisfied.tap"))
        self.assertEqual(["id-1"], report.point_ids())
        self.assertFalse(report.complete)
        self.assertFalse(report.passed)

    def test_a_bailed_out_report_is_incomplete_without_being_malformed(self) -> None:
        report = validate_tap_report(_read("tap/valid/bail_out.tap"))
        self.assertTrue(report.conforms)
        self.assertTrue(report.bailed_out)
        self.assertFalse(report.complete)
        self.assertEqual("Test host terminated before the plan completed", report.bail_message)


class SelectionCorrelationTests(unittest.TestCase):
    def test_every_run_case_matches_the_manifest(self) -> None:
        for case in MANIFEST["run"]:
            with self.subTest(case=case["name"]):
                discovery = validate_discovery_stream(_read(case["discovery"]))
                self.assertTrue(discovery.conforms, msg=str(discovery.violations))
                report = validate_tap_report(_read(case["report"]))
                self.assertTrue(report.conforms, msg=str(report.violations))
                leaves, _ = expected_leaf_ids(discovery, case["select"])
                self.assertEqual(case["expected_leaves"], leaves)
                violations = validate_run(discovery, report, case["select"])
                expected = sorted((item["code"], item["location"]) for item in case["violations"])
                self.assertEqual(expected, _signatures(violations))


class ManifestCoverageTests(unittest.TestCase):
    def test_every_fixture_file_is_covered_by_the_manifest(self) -> None:
        declared = set()
        for artifact in ("capabilities", "discovery", "tap"):
            for relative_path in MANIFEST[artifact]:
                declared.add("{}/{}".format(artifact, relative_path))
        for case in MANIFEST["run"]:
            declared.add(case["discovery"])
            declared.add(case["report"])

        present = {
            path.relative_to(fixtures_root()).as_posix()
            for path in fixtures_root().rglob("*")
            if path.is_file() and path.name != "expectations.json"
        }
        self.assertEqual(present, declared)

    def test_declared_fixtures_exist_on_disk(self) -> None:
        for artifact in ("capabilities", "discovery", "tap"):
            for relative_path in MANIFEST[artifact]:
                path = fixtures_root() / artifact / relative_path
                self.assertTrue(path.is_file(), msg="Missing fixture {}".format(path))


class SchemaAssetTests(unittest.TestCase):
    def test_schemas_are_well_formed_and_identify_the_protocol_version(self) -> None:
        from foundry_test_adapter import CAPABILITIES_SCHEMA, DISCOVERY_RECORD_SCHEMA, schema_path

        for name in (CAPABILITIES_SCHEMA, DISCOVERY_RECORD_SCHEMA):
            with self.subTest(schema=name):
                document = json.loads(Path(schema_path(name)).read_text(encoding="utf-8"))
                self.assertEqual("https://json-schema.org/draft/2020-12/schema", document["$schema"])
                self.assertIn("/test-adapter/v1/", document["$id"])

    def test_capabilities_schema_pins_the_protocol_name(self) -> None:
        from foundry_test_adapter import CAPABILITIES_SCHEMA, PROTOCOL_NAME, schema_path

        document = json.loads(Path(schema_path(CAPABILITIES_SCHEMA)).read_text(encoding="utf-8"))
        self.assertEqual(PROTOCOL_NAME, document["properties"]["protocol"]["const"])


if __name__ == "__main__":
    unittest.main()
