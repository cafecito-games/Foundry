#!/usr/bin/env python3
"""Unit tests for scripts/type_completeness.

Run with: python3 -m unittest discover -s scripts/tests -p "test_type_completeness_*.py"
"""

from __future__ import annotations

import hashlib
import importlib.util
import json
import subprocess
import sys
import tempfile
import unittest
import unittest.mock
from datetime import datetime, timedelta, timezone
from pathlib import Path
from typing import Any

_PACKAGE_ROOT = Path(__file__).resolve().parents[1] / "type_completeness"


def _load(name: str) -> Any:
    spec = importlib.util.spec_from_file_location(f"type_completeness.{name}", _PACKAGE_ROOT / f"{name}.py")
    assert spec is not None and spec.loader is not None
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


if "type_completeness" not in sys.modules:
    package_spec = importlib.util.spec_from_file_location(
        "type_completeness", _PACKAGE_ROOT / "__init__.py", submodule_search_locations=[str(_PACKAGE_ROOT)]
    )
    assert package_spec is not None and package_spec.loader is not None
    package = importlib.util.module_from_spec(package_spec)
    sys.modules["type_completeness"] = package
    package_spec.loader.exec_module(package)

report = _load("report")
comparator = _load("comparator")
deadline = _load("deadline")
ledger = _load("ledger")
provisional = _load("provisional")
reconcile = _load("reconcile")
github = _load("github")
cli = _load("cli")

NY = deadline.NEW_YORK


def _case(case_id: str = "case-a", passed: bool = True, **overrides: Any) -> dict[str, Any]:
    base: dict[str, Any] = {
        "case_id": case_id,
        "coordinates": {"surface": "text", "destination": "variable"},
        "expected": {"outcome": "accept"},
        "actual": {"outcome": "accept" if passed else "reject"},
        "canonical_provenance": "seed",
        "agreeing_provenance": ["seed"],
        "status": "passed" if passed else "failed",
        "passed": passed,
        "runtime_passed": True,
        "runtime_status": "ok",
        "diagnostics": [] if passed else ["Cannot assign"],
        "produced_output": "ok",
        "expected_output": "ok",
        "artifact_path": f"scratch/{case_id}.fs",
    }
    base.update(overrides)
    return base


def _runner_finding_id(case_id: str, dimension: str) -> str:
    """Mirror of make_finding_id in fs_type_completeness_runner.cpp: "fstcf-v1-" + sha256(case_id|dimension)[:20]."""
    return "fstcf-v1-" + hashlib.sha256(f"{case_id}|{dimension}".encode()).hexdigest()[:20]


def _finding(case_id: str, dimension: str) -> dict[str, Any]:
    return {
        "finding_id": _runner_finding_id(case_id, dimension),
        "case_id": case_id,
        "family": "union_destination_membership",
        "dimension": dimension,
        "expected": "accept",
        "actual": "reject",
        "classification": "unclassified",
        "artifact_path": f"scratch/{case_id}.fs",
        "parity_evidence": {},
        "issue_url": "",
        "closure_packet_url": "",
        "permanent_test_paths": [],
        "migrated_from": "",
        "resolved_case_ids": [],
    }


def _report(
    cases: list[dict[str, Any]],
    family: str = "union_destination_membership",
    findings: list[dict[str, Any]] | None = None,
) -> dict[str, Any]:
    return {
        "schema_version": 1,
        "family": family,
        "success": all(case["passed"] for case in cases),
        "cell_count": len(cases),
        "executed_by_surface": {"text": len(cases), "bytecode": 0},
        "coverage_by_chain_length": {},
        "coverage_by_dimension": {},
        "uncovered_required_dimensions": 0,
        "text_bytecode_parity_failures": 0,
        "published_surface": "",
        "outcome": "passed" if all(case["passed"] for case in cases) else "product_mismatch",
        "structural_failures": [],
        "ledger": {"reconciled": [], "stale": []},
        "exceptions": [],
        "findings": findings or [],
        "cases": cases,
        # Observability only: every consumer must ignore it, so every synthetic report carries it.
        "timings_ms": {
            "load": 1.0,
            "resolve": 2.0,
            "render": 3.0,
            "analyze": 4.0,
            "execute": 5.0,
            "report": 6.0,
            "total": 21.0,
        },
    }


CAPABILITIES = {
    "schema_version": 1,
    "production": [
        {
            "paths": ["modules/foundry_script/fs_analyzer.cpp", "modules/foundry_script/fs_vm.cpp"],
            "families": ["union_destination_membership"],
        }
    ],
    "nonproduction_prefixes": ["docs/"],
    "broad_core_families": ["union_destination_membership"],
}


_CAPABILITIES_FILE = Path(tempfile.mkdtemp(prefix="type_completeness_capabilities_")) / "capabilities.json"
_CAPABILITIES_FILE.write_text(json.dumps(CAPABILITIES))


def _ny(year: int, month: int, day: int, hour: int, minute: int = 0, second: int = 0) -> datetime:
    return datetime(year, month, day, hour, minute, second, tzinfo=NY)


# A report exactly as FSCompletenessRunner writes it, derived from fs_type_completeness_runner.cpp:
# the top-level key set and value types come from the report assembly (schema_version is the Variant
# FLOAT 1.0; cell_count, executed_by_surface values, coverage counters, uncovered_required_dimensions and
# text_bytecode_parity_failures are all double()), each case from the case_report block, and each finding from
# finding_report(). The text is what JSON::stringify(report, "\t", false, true) produces: sorted keys, tab
# indentation, every numeric rendered as a float ("1.0", "2.0"), and a trailing newline.
RUNNER_REPORT_TEXT = """{
\t"cases": [
\t\t{
\t\t\t"actual": {
\t\t\t\t"destination": "reject"
\t\t\t},
\t\t\t"agreeing_provenance": [
\t\t\t\t"seed"
\t\t\t],
\t\t\t"artifact_path": "/scratch/type_completeness/union_destination_membership/case_union_store_variable.fs",
\t\t\t"canonical_provenance": "seed",
\t\t\t"case_id": "case_union_store_variable",
\t\t\t"coordinates": {
\t\t\t\t"destination": "variable",
\t\t\t\t"surface": "text"
\t\t\t},
\t\t\t"diagnostics": [
\t\t\t\t"Cannot assign a value of type String to a variable of type int | float."
\t\t\t],
\t\t\t"expected": {
\t\t\t\t"destination": "accept"
\t\t\t},
\t\t\t"expected_output": "ok\\n",
\t\t\t"passed": false,
\t\t\t"produced_output": "",
\t\t\t"runtime_passed": false,
\t\t\t"runtime_status": "analyzer_error",
\t\t\t"status": "failed"
\t\t},
\t\t{
\t\t\t"actual": {
\t\t\t\t"destination": "accept"
\t\t\t},
\t\t\t"agreeing_provenance": [
\t\t\t\t"seed"
\t\t\t],
\t\t\t"artifact_path": "/scratch/type_completeness/union_destination_membership/case_union_store_member.fs",
\t\t\t"canonical_provenance": "seed",
\t\t\t"case_id": "case_union_store_member",
\t\t\t"coordinates": {
\t\t\t\t"destination": "member",
\t\t\t\t"surface": "text"
\t\t\t},
\t\t\t"diagnostics": [],
\t\t\t"expected": {
\t\t\t\t"destination": "accept"
\t\t\t},
\t\t\t"expected_output": "ok\\n",
\t\t\t"passed": true,
\t\t\t"produced_output": "ok\\n",
\t\t\t"runtime_passed": true,
\t\t\t"runtime_status": "ok",
\t\t\t"status": "passed"
\t\t}
\t],
\t"cell_count": 2.0,
\t"coverage_by_chain_length": {
\t\t"1": 2.0
\t},
\t"coverage_by_dimension": {
\t\t"destination": 2.0
\t},
\t"exceptions": [],
\t"executed_by_surface": {
\t\t"bytecode": 0.0,
\t\t"text": 2.0
\t},
\t"family": "union_destination_membership",
\t"findings": [
\t\t{
\t\t\t"actual": "reject",
\t\t\t"artifact_path": "/scratch/type_completeness/union_destination_membership/case_union_store_variable.fs",
\t\t\t"case_id": "case_union_store_variable",
\t\t\t"classification": "unclassified",
\t\t\t"closure_packet_url": "",
\t\t\t"dimension": "destination",
\t\t\t"expected": "accept",
\t\t\t"family": "union_destination_membership",
\t\t\t"finding_id": "fstcf-v1-%s",
\t\t\t"issue_url": "",
\t\t\t"migrated_from": "",
\t\t\t"parity_evidence": {},
\t\t\t"permanent_test_paths": [],
\t\t\t"resolved_case_ids": []
\t\t}
\t],
\t"ledger": {
\t\t"reconciled": [],
\t\t"stale": []
\t},
\t"outcome": "product_mismatch",
\t"published_surface": "",
\t"schema_version": 1.0,
\t"structural_failures": [],
\t"success": false,
\t"text_bytecode_parity_failures": 0.0,
\t"timings_ms": {
\t\t"analyze": 3.5,
\t\t"execute": 812.25,
\t\t"load": 11.75,
\t\t"render": 4.0,
\t\t"report": 1.5,
\t\t"resolve": 6.25,
\t\t"total": 839.5
\t},
\t"uncovered_required_dimensions": 0.0
}
""" % hashlib.sha256(b"case_union_store_variable|destination").hexdigest()[:20]


# `Error::ERR_TIMEOUT`, the code the runner stamps on a `run_timeout` structural failure. The runner
# writes every error code as a Variant FLOAT, so it reads back as 24.0.
RUNNER_ERR_TIMEOUT = 24.0


def _republished_after_timeout(text: str) -> str:
    """The document `run_family` republishes when its budget is found crossed after the last write.

    Applying the runner's own transformation to the fixture above is what makes this the runner's
    document rather than an invented one: the verdict members are replaced in place (so the engine's
    member order is preserved), the `run_timeout` record carries exactly the members
    `structural_failure_report()` writes, and Python's JSON writer with a tab indent reproduces the
    engine's writer byte for byte - checked against a report written by a real `test completeness
    run`. The evidence stays in the document; only the verdict changes.
    """
    document = json.loads(text)
    document["success"] = False
    document["outcome"] = "structural_failure"
    document["structural_failures"] = [
        {
            "stage": "run_timeout",
            "detail": "The run exceeded its wall-clock budget while publishing its report.",
            "case_id": "",
            "witness_id": "",
            "exception_id": "",
            "error_code": RUNNER_ERR_TIMEOUT,
        }
    ]
    return json.dumps(document, indent="\t") + "\n"


RUNNER_TIMEOUT_REPORT_TEXT = _republished_after_timeout(RUNNER_REPORT_TEXT)


class ReportLoadingTests(unittest.TestCase):
    def test_loads_failed_cases_with_observations(self) -> None:
        loaded = report.load_report(_report([_case("a", passed=True), _case("b", passed=False)]))
        self.assertEqual(loaded.family, "union_destination_membership")
        self.assertEqual([case.case_id for case in loaded.failed_cases()], ["b"])
        observation = loaded.case("b").observation
        self.assertEqual(observation["diagnostics"], ["Cannot assign"])
        self.assertEqual(observation["actual"], {"outcome": "reject"})

    def test_loads_the_report_the_runner_actually_writes(self) -> None:
        loaded = report.load_report(json.loads(RUNNER_REPORT_TEXT))
        self.assertEqual([case.case_id for case in loaded.failed_cases()], ["case_union_store_variable"])
        failed = loaded.case("case_union_store_variable")
        self.assertEqual(len(failed.findings), 1)
        self.assertEqual(
            failed.findings[0]["finding_id"], _runner_finding_id("case_union_store_variable", "destination")
        )
        self.assertEqual(loaded.case("case_union_store_member").findings, ())

    def _compare_exit_code(self, root: Path, branch_text: str, develop_text: str) -> tuple[int, dict[str, Any]]:
        (root / "branch.json").write_text(branch_text)
        (root / "develop.json").write_text(develop_text)
        code = cli.main(
            [
                "compare",
                "--branch-report",
                str(root / "branch.json"),
                "--develop-report",
                str(root / "develop.json"),
                "--configuration",
                "text",
                "--capabilities",
                str(_CAPABILITIES_FILE),
                "--output",
                str(root / "comparison.json"),
                "--fail-on-regression",
            ]
        )
        return code, json.loads((root / "comparison.json").read_text())

    def test_compare_refuses_a_republished_timeout_report(self) -> None:
        for label, branch_text, develop_text in (
            ("branch", RUNNER_TIMEOUT_REPORT_TEXT, RUNNER_REPORT_TEXT),
            ("develop", RUNNER_REPORT_TEXT, RUNNER_TIMEOUT_REPORT_TEXT),
            ("both", RUNNER_TIMEOUT_REPORT_TEXT, RUNNER_TIMEOUT_REPORT_TEXT),
        ):
            with self.subTest(side=label), tempfile.TemporaryDirectory() as directory:
                code, comparison = self._compare_exit_code(Path(directory), branch_text, develop_text)
                # Refused whatever --fail-on-regression says: this is not a regression judgment.
                self.assertEqual(code, cli.STRUCTURAL_FAILURE_EXIT_CODE)
                self.assertEqual(comparison["artifacts"], [])
                self.assertIn("structural_failure", comparison)
                self.assertTrue(comparison["structural_failure"]["reasons"])
                # Nothing a consumer could read as a clean verdict.
                self.assertNotIn("unchanged", json.dumps(comparison))

    def test_a_refused_comparison_cannot_be_deserialized_as_a_verdict(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            _, comparison = self._compare_exit_code(root, RUNNER_TIMEOUT_REPORT_TEXT, RUNNER_REPORT_TEXT)
            with self.assertRaises(report.ReportError):
                comparator.deserialize_many(json.dumps(comparison))

    def test_compare_still_classifies_a_product_mismatch(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            code, comparison = self._compare_exit_code(Path(directory), RUNNER_REPORT_TEXT, RUNNER_REPORT_TEXT)
            self.assertEqual(code, 0)
            self.assertEqual([entry["status"] for entry in comparison["artifacts"]], ["unchanged"])
            self.assertNotIn("structural_failure", comparison)

    def test_runner_report_survives_compare_and_propose_end_to_end(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "branch.json").write_text(RUNNER_REPORT_TEXT)
            (root / "develop.json").write_text(RUNNER_REPORT_TEXT)
            self.assertEqual(
                cli.main(
                    [
                        "compare",
                        "--branch-report",
                        str(root / "branch.json"),
                        "--develop-report",
                        str(root / "develop.json"),
                        "--configuration",
                        "text",
                        "--capabilities",
                        str(_CAPABILITIES_FILE),
                        "--output",
                        str(root / "comparison.json"),
                        "--fail-on-regression",
                    ]
                ),
                0,
            )
            comparison = json.loads((root / "comparison.json").read_text())
            self.assertEqual([entry["status"] for entry in comparison["artifacts"]], ["unchanged"])
            self.assertEqual(
                cli.main(
                    [
                        "propose",
                        "--comparison",
                        str(root / "comparison.json"),
                        "--case-id",
                        "case_union_store_variable",
                        "--dimension",
                        "destination",
                        "--issue-url",
                        "https://x/1",
                        "--closure-packet-url",
                        "https://x/2",
                        "--permanent-test-path",
                        "p",
                        "--capability-path",
                        "modules/foundry_script/fs_analyzer.cpp",
                        "--workstream-owner",
                        "o",
                        "--detection-artifact",
                        "d",
                        "--detected-at",
                        "2026-08-17T10:00:00-04:00",
                        "--capabilities",
                        str(_CAPABILITIES_FILE),
                        "--output-dir",
                        str(root / "out"),
                    ]
                ),
                0,
            )
            finding_id = _runner_finding_id("case_union_store_variable", "destination")
            self.assertEqual(ledger.read_record(root / "out" / f"{finding_id}.json")["finding_id"], finding_id)

    def test_integral_float_schema_version_is_accepted_everywhere(self) -> None:
        ok = _report([_case()])
        ok["schema_version"] = 1.0
        report.load_report(ok)
        record = ledger.proposed_record(
            finding_id="fstcf-v1-eeeeeeeeeeeeeeeeeeee",
            family="f",
            case_id="c",
            dimension="d",
            issue_url="https://x/1",
            closure_packet_url="https://x/2",
            permanent_test_paths=["p"],
        )
        ledger.validate_record(dict(record, schema_version=1.0))
        for bad in (1.5, "1", True, 2.0):
            with self.assertRaises(report.ReportError, msg=repr(bad)):
                report.load_report(dict(ok, schema_version=bad))
            with self.assertRaises(ValueError, msg=repr(bad)):
                ledger.validate_record(dict(record, schema_version=bad))

    def test_rejects_unknown_schema_version(self) -> None:
        bad = _report([_case()])
        bad["schema_version"] = 2
        with self.assertRaises(report.ReportError):
            report.load_report(bad)

    def test_non_boolean_passed_is_rejected_rather_than_coerced(self) -> None:
        bad_values: list[Any] = ["false", "true", 0, 1, None, []]
        for bad in bad_values:
            with self.assertRaises(report.ReportError, msg=repr(bad)):
                report.load_report(_report([dict(_case("a"), passed=bad)]))

    def test_string_schema_version_is_rejected(self) -> None:
        bad = _report([_case()])
        bad["schema_version"] = "1"
        with self.assertRaises(report.ReportError):
            report.load_report(bad)

    def test_missing_runner_members_are_errors_not_defaults(self) -> None:
        complete = _report([_case("a", passed=False)], findings=[_finding("a", "destination")])
        for member in ("findings", "success"):
            broken = dict(complete)
            del broken[member]
            with self.assertRaises(report.ReportError, msg=member):
                report.load_report(broken)
        for member in ("coordinates", "artifact_path", "expected", "actual", "diagnostics", "runtime_status"):
            broken_case = dict(_case("a", passed=False))
            del broken_case[member]
            with self.assertRaises(report.ReportError, msg=member):
                report.load_report(_report([broken_case]))
        for member in ("finding_id", "case_id", "dimension", "expected", "actual", "classification"):
            broken_finding = dict(_finding("a", "destination"))
            del broken_finding[member]
            with self.assertRaises(report.ReportError, msg=member):
                report.load_report(_report([_case("a", passed=False)], findings=[broken_finding]))
        with self.assertRaises(report.ReportError):
            report.load_report(
                _report(
                    [_case("a", passed=False)], findings=[dict(_finding("a", "destination"), classification="bogus")]
                )
            )

    def test_malformed_capabilities_manifest_is_an_error(self) -> None:
        for manifest in (
            {"schema_version": 1},
            {"schema_version": 1, "production": "nope", "broad_core_families": []},
            {"schema_version": 1, "production": [{"families": ["f"]}], "broad_core_families": []},
            {"schema_version": 1, "production": [{"paths": ["p"], "families": ["f"]}]},
        ):
            with self.assertRaises(report.ReportError, msg=repr(manifest)):
                report.capability_slice_for_family(manifest, "f")

    def test_orphan_finding_is_a_report_error(self) -> None:
        with self.assertRaises(report.ReportError):
            report.load_report(_report([_case("a", passed=False)], findings=[_finding("ghost", "destination")]))

    def test_finding_on_a_passed_case_is_contradictory(self) -> None:
        # The runner marks a case failed whenever a finding targets it (fs_type_completeness_runner.cpp:1614-1619).
        with self.assertRaises(report.ReportError):
            report.load_report(_report([_case("a", passed=True)], findings=[_finding("a", "destination")]))

    def test_every_category_value_loads(self) -> None:
        for category in report.Category:
            loaded = report.load_report(_report([_case("a", passed=False, category=category.value)]))
            self.assertEqual(loaded.case("a").category, category)

    def test_default_category_is_product_finding_until_runner_emits_categories(self) -> None:
        loaded = report.load_report(_report([_case("b", passed=False)]))
        self.assertEqual(loaded.case("b").category, report.Category.PRODUCT_FINDING)

    def test_structured_category_from_runner_is_consumed_when_present(self) -> None:
        loaded = report.load_report(_report([_case("b", passed=False, category="structural_failure")]))
        self.assertEqual(loaded.case("b").category, report.Category.STRUCTURAL_FAILURE)

    def test_the_outcome_is_required_and_must_agree_with_success(self) -> None:
        complete = json.loads(RUNNER_REPORT_TEXT)
        report.load_report(complete)

        missing = {member: value for member, value in complete.items() if member != "outcome"}
        with self.assertRaises(report.ReportError):
            report.load_report(missing)
        for malformed in (None, 1, True, "", "clean", ["passed"]):
            with self.subTest(outcome=malformed):
                with self.assertRaises(report.ReportError):
                    report.load_report(dict(complete, outcome=malformed))
        # A document whose two verdict members disagree was not written by a run.
        with self.assertRaises(report.ReportError):
            report.load_report(dict(complete, success=True))
        with self.assertRaises(report.ReportError):
            report.load_report(dict(json.loads(RUNNER_TIMEOUT_REPORT_TEXT), success=True))

    def test_a_republished_timeout_report_is_a_structural_failure(self) -> None:
        loaded = report.load_report(json.loads(RUNNER_TIMEOUT_REPORT_TEXT))
        self.assertTrue(loaded.is_structural_failure)
        self.assertFalse(loaded.is_clean)
        self.assertFalse(loaded.success)
        self.assertEqual(loaded.outcome, "structural_failure")
        # The cases the run did collect are still there, and they still read as they were observed:
        # nothing about the case list reveals that the run failed.
        self.assertEqual(len(loaded.cases), len(report.load_report(json.loads(RUNNER_REPORT_TEXT)).cases))

    def test_a_product_mismatch_report_is_not_a_structural_failure(self) -> None:
        loaded = report.load_report(json.loads(RUNNER_REPORT_TEXT))
        self.assertEqual(loaded.outcome, "product_mismatch")
        self.assertFalse(loaded.is_structural_failure)
        self.assertFalse(loaded.is_clean)

    def test_timings_are_dropped_from_the_loaded_evidence(self) -> None:
        loaded = report.load_report(json.loads(RUNNER_REPORT_TEXT))
        self.assertIn("timings_ms", json.loads(RUNNER_REPORT_TEXT))
        self.assertNotIn("timings_ms", loaded.raw)
        for case in loaded.cases:
            self.assertNotIn("timings_ms", case.observation)

    def test_a_report_without_timings_loads_identically(self) -> None:
        timed = json.loads(RUNNER_REPORT_TEXT)
        untimed = {member: value for member, value in timed.items() if member != "timings_ms"}
        self.assertEqual(report.load_report(timed).raw, report.load_report(untimed).raw)

    def test_capability_slice_is_resolved_from_capabilities_manifest(self) -> None:
        manifest = {
            "schema_version": 1,
            "production": [
                {"paths": ["modules/foundry_script/fs_analyzer.cpp"], "families": ["union_destination_membership"]}
            ],
            "nonproduction_prefixes": ["docs/"],
            "broad_core_families": ["union_destination_membership"],
        }
        capability_slice = report.capability_slice_for_family(manifest, "union_destination_membership")
        self.assertEqual(capability_slice.paths, ("modules/foundry_script/fs_analyzer.cpp",))
        self.assertTrue(capability_slice.broad_core)


class ComparatorTests(unittest.TestCase):
    def _compare(self, branch_case: dict[str, Any], develop_case: dict[str, Any] | None) -> Any:
        branch = report.load_report(_report([branch_case]))
        develop = report.load_report(_report([develop_case] if develop_case else []))
        return comparator.compare_case(branch, develop, branch_case["case_id"], configuration="text")

    def test_failure_absent_on_develop_is_new(self) -> None:
        artifact = self._compare(_case("a", passed=False), _case("a", passed=True))
        self.assertEqual(artifact.status, comparator.Status.NEW)
        self.assertEqual(artifact.case_id, "a")
        self.assertIsNotNone(artifact.develop_digest)

    def test_identical_failure_on_develop_is_unchanged(self) -> None:
        artifact = self._compare(_case("a", passed=False), _case("a", passed=False))
        self.assertEqual(artifact.status, comparator.Status.UNCHANGED)
        self.assertEqual(artifact.branch_digest, artifact.develop_digest)

    def test_different_failure_signature_is_worsened(self) -> None:
        artifact = self._compare(
            _case("a", passed=False, runtime_status="crash", runtime_passed=False),
            _case("a", passed=False),
        )
        self.assertEqual(artifact.status, comparator.Status.WORSENED)

    def test_branch_pass_with_develop_failure_is_resolved(self) -> None:
        artifact = self._compare(_case("a", passed=True), _case("a", passed=False))
        self.assertEqual(artifact.status, comparator.Status.RESOLVED)

    def test_case_missing_from_develop_report_is_new(self) -> None:
        artifact = self._compare(_case("a", passed=False), None)
        self.assertEqual(artifact.status, comparator.Status.NEW)
        self.assertIsNone(artifact.develop_digest)

    def test_comparison_is_deterministic_and_idempotent(self) -> None:
        first = self._compare(_case("a", passed=False), _case("a", passed=False))
        second = self._compare(_case("a", passed=False), _case("a", passed=False))
        self.assertEqual(first.to_dict(), second.to_dict())
        self.assertEqual(comparator.serialize(first), comparator.serialize(second))
        self.assertEqual(first.comparison_id, second.comparison_id)

    def test_digest_ignores_key_order_but_not_content(self) -> None:
        ordered = _case("a", passed=False)
        reordered = dict(reversed(list(ordered.items())))
        self.assertEqual(comparator.observation_digest(ordered), comparator.observation_digest(reordered))
        changed = _case("a", passed=False, produced_output="different")
        self.assertNotEqual(comparator.observation_digest(ordered), comparator.observation_digest(changed))

    def test_digests_and_comparison_ignore_timings(self) -> None:
        timed = _case("a", passed=False)
        branch = report.load_report(_report([timed]))
        untimed_report = {member: value for member, value in _report([timed]).items() if member != "timings_ms"}
        untimed = report.load_report(untimed_report)
        timed_artifact = comparator.compare_case(branch, branch, "a", configuration="text")
        untimed_artifact = comparator.compare_case(untimed, untimed, "a", configuration="text")
        self.assertEqual(timed_artifact.branch_digest, untimed_artifact.branch_digest)
        self.assertEqual(timed_artifact.comparison_id, untimed_artifact.comparison_id)
        self.assertEqual(timed_artifact.to_dict(), untimed_artifact.to_dict())

    def test_a_run_that_only_differs_in_timings_is_unchanged(self) -> None:
        slow = _report([_case("a", passed=False)])
        fast = _report([_case("a", passed=False)])
        fast["timings_ms"] = {member: value * 17.0 for member, value in fast["timings_ms"].items()}
        artifact = comparator.compare_case(
            report.load_report(slow), report.load_report(fast), "a", configuration="text"
        )
        self.assertEqual(artifact.status, comparator.Status.UNCHANGED)

    def test_artifact_preserves_full_evidence_and_slice(self) -> None:
        artifact = self._compare(_case("a", passed=False), _case("a", passed=False))
        payload = artifact.to_dict()
        self.assertEqual(payload["family"], "union_destination_membership")
        self.assertEqual(payload["configuration"], "text")
        self.assertEqual(payload["branch"]["observation"]["diagnostics"], ["Cannot assign"])
        self.assertEqual(payload["develop"]["observation"]["diagnostics"], ["Cannot assign"])
        self.assertEqual(payload["category"], "product_finding")

    def test_develop_failure_absent_from_branch_report_is_missing(self) -> None:
        branch = report.load_report(_report([_case("b", passed=True)]))
        develop = report.load_report(_report([_case("a", passed=False), _case("b", passed=True)]))
        artifacts = comparator.compare_reports(branch, develop, configuration="text")
        self.assertEqual(
            [(artifact.case_id, artifact.status) for artifact in artifacts], [("a", comparator.Status.MISSING)]
        )
        self.assertFalse(artifacts[0].branch["present"])

    def test_artifact_lists_every_finding_dimension_for_the_case(self) -> None:
        findings = [_finding("a", "destination"), _finding("a", "source_proof")]
        branch = report.load_report(_report([_case("a", passed=False)], findings=findings))
        develop = report.load_report(_report([_case("a", passed=False)], findings=findings[:1]))
        artifact = comparator.compare_case(branch, develop, "a", configuration="text")
        self.assertEqual(
            [finding["dimension"] for finding in artifact.branch["findings"]], ["destination", "source_proof"]
        )
        self.assertEqual([finding["dimension"] for finding in artifact.develop["findings"]], ["destination"])
        self.assertEqual(artifact.status, comparator.Status.WORSENED)
        self.assertEqual(
            artifact.to_dict()["branch"]["findings"][1]["finding_id"], _runner_finding_id("a", "source_proof")
        )

    def test_non_semantic_finding_metadata_does_not_make_failure_worsened(self) -> None:
        branch_finding = dict(
            _finding("a", "destination"),
            artifact_path="/branch/worktree/scratch/a.fs",
            issue_url="https://x/issues/9",
            classification="product_defect",
            parity_evidence={"artifact_path": "/branch/a.txt"},
        )
        develop_finding = dict(_finding("a", "destination"), artifact_path="/develop/worktree/scratch/a.fs")
        branch = report.load_report(
            _report([_case("a", passed=False, artifact_path="/branch/a.fs")], findings=[branch_finding])
        )
        develop = report.load_report(
            _report([_case("a", passed=False, artifact_path="/develop/a.fs")], findings=[develop_finding])
        )
        artifact = comparator.compare_case(branch, develop, "a", configuration="text")
        self.assertEqual(artifact.status, comparator.Status.UNCHANGED)
        self.assertEqual(artifact.branch_digest, artifact.develop_digest)

    def test_parity_evidence_differing_only_by_path_is_unchanged(self) -> None:
        evidence = {
            "text_case_id": "a.text",
            "bytecode_case_id": "a.bytecode",
            "output": {"text": "1", "bytecode": "2", "text_case_id": "a.text", "bytecode_case_id": "a.bytecode"},
            "dimensions": {"destination": {"text": "accept", "bytecode": "reject"}},
        }
        branch_finding = dict(
            _finding("a", "destination"), parity_evidence=dict(evidence, artifact_path="/branch/a.fs")
        )
        develop_finding = dict(
            _finding("a", "destination"), parity_evidence=dict(evidence, artifact_path="/develop/a.fs")
        )
        branch = report.load_report(_report([_case("a", passed=False)], findings=[branch_finding]))
        develop = report.load_report(_report([_case("a", passed=False)], findings=[develop_finding]))
        artifact = comparator.compare_case(branch, develop, "a", configuration="text")
        self.assertEqual(artifact.status, comparator.Status.UNCHANGED)

    def test_semantic_parity_evidence_change_is_worsened(self) -> None:
        evidence = {"output": {"text": "1", "bytecode": "2"}}
        branch_finding = dict(
            _finding("a", "destination"), parity_evidence={"output": {"text": "1", "bytecode": "crash"}}
        )
        develop_finding = dict(_finding("a", "destination"), parity_evidence=evidence)
        branch = report.load_report(_report([_case("a", passed=False)], findings=[branch_finding]))
        develop = report.load_report(_report([_case("a", passed=False)], findings=[develop_finding]))
        artifact = comparator.compare_case(branch, develop, "a", configuration="text")
        self.assertEqual(artifact.status, comparator.Status.WORSENED)

    def test_semantic_finding_change_is_worsened(self) -> None:
        branch = report.load_report(
            _report([_case("a", passed=False)], findings=[dict(_finding("a", "destination"), actual="crash")])
        )
        develop = report.load_report(_report([_case("a", passed=False)], findings=[_finding("a", "destination")]))
        artifact = comparator.compare_case(branch, develop, "a", configuration="text")
        self.assertEqual(artifact.status, comparator.Status.WORSENED)

    def test_artifact_carries_capability_slice_from_manifest(self) -> None:
        branch = report.load_report(_report([_case("a", passed=False)]))
        develop = report.load_report(_report([_case("a", passed=True)]))
        artifacts = comparator.compare_reports(branch, develop, configuration="text", capabilities=CAPABILITIES)
        payload = artifacts[0].to_dict()
        self.assertEqual(
            payload["capability_slice"],
            {
                "family": "union_destination_membership",
                "paths": ["modules/foundry_script/fs_analyzer.cpp", "modules/foundry_script/fs_vm.cpp"],
                "broad_core": True,
            },
        )
        round_trip = comparator.deserialize_many(comparator.serialize_many(artifacts))[0]
        self.assertEqual(round_trip.capability_slice, artifacts[0].capability_slice)

    def test_develop_passing_case_absent_from_branch_report_is_vanished(self) -> None:
        branch = report.load_report(_report([_case("b", passed=True)]))
        develop = report.load_report(_report([_case("a", passed=True), _case("b", passed=True)]))
        artifacts = comparator.compare_reports(branch, develop, configuration="text")
        self.assertEqual(
            [(artifact.case_id, artifact.status) for artifact in artifacts], [("a", comparator.Status.VANISHED)]
        )
        self.assertIn(comparator.Status.VANISHED, comparator.REGRESSION_STATUSES)
        self.assertNotIn(comparator.Status.VANISHED, comparator.NO_LONGER_FAILING_STATUSES)

    def test_compare_report_emits_one_artifact_per_branch_failure(self) -> None:
        branch = report.load_report(
            _report([_case("a", passed=False), _case("b", passed=True), _case("c", passed=False)])
        )
        develop = report.load_report(
            _report([_case("a", passed=False), _case("b", passed=False), _case("c", passed=True)])
        )
        artifacts = comparator.compare_reports(branch, develop, configuration="text")
        self.assertEqual(
            [(artifact.case_id, artifact.status) for artifact in artifacts],
            [("a", comparator.Status.UNCHANGED), ("b", comparator.Status.RESOLVED), ("c", comparator.Status.NEW)],
        )


class DeadlineTests(unittest.TestCase):
    def test_monday_detection_is_due_wednesday_1700(self) -> None:
        self.assertEqual(deadline.classification_deadline(_ny(2026, 8, 17, 10)), _ny(2026, 8, 19, 17))

    def test_thursday_detection_skips_weekend_to_monday(self) -> None:
        self.assertEqual(deadline.classification_deadline(_ny(2026, 8, 20, 9)), _ny(2026, 8, 24, 17))

    def test_friday_detection_is_due_tuesday(self) -> None:
        self.assertEqual(deadline.classification_deadline(_ny(2026, 8, 21, 16, 59)), _ny(2026, 8, 25, 17))

    def test_saturday_detection_counts_weekdays_from_monday(self) -> None:
        self.assertEqual(deadline.classification_deadline(_ny(2026, 8, 22, 12)), _ny(2026, 8, 25, 17))

    def test_detection_after_1700_does_not_shift_the_date(self) -> None:
        self.assertEqual(deadline.classification_deadline(_ny(2026, 8, 17, 23, 30)), _ny(2026, 8, 19, 17))

    def test_utc_detection_is_converted_to_new_york(self) -> None:
        # 01:00 UTC on Tuesday is still Monday 21:00 in New York during DST.
        detected = datetime(2026, 8, 18, 1, 0, tzinfo=timezone.utc)
        self.assertEqual(deadline.classification_deadline(detected), _ny(2026, 8, 19, 17))

    def test_naive_detection_is_rejected(self) -> None:
        with self.assertRaises(ValueError):
            deadline.classification_deadline(datetime(2026, 8, 17, 10))

    def test_deadline_tracks_us_daylight_saving_across_march_and_november(self) -> None:
        # Thursday 2026-03-05 is before the DST switch (second Sunday of March); the deadline on Monday
        # 2026-03-09 17:00 falls after it, so it is 21:00 UTC (EDT), not 22:00 UTC (EST).
        march = deadline.classification_deadline(datetime(2026, 3, 5, 15, 0, tzinfo=timezone.utc))
        self.assertEqual(march.astimezone(timezone.utc), datetime(2026, 3, 9, 21, 0, tzinfo=timezone.utc))
        # Thursday 2026-10-29 is on EDT; Monday 2026-11-02 17:00 is after the first Sunday of November, so EST.
        november = deadline.classification_deadline(datetime(2026, 10, 29, 15, 0, tzinfo=timezone.utc))
        self.assertEqual(november.astimezone(timezone.utc), datetime(2026, 11, 2, 22, 0, tzinfo=timezone.utc))
        self.assertEqual(deadline.format_timestamp(march), "2026-03-09T17:00:00-04:00")
        self.assertEqual(deadline.format_timestamp(november), "2026-11-02T17:00:00-05:00")

    def test_new_york_offsets_without_zoneinfo(self) -> None:
        self.assertNotIn("ZoneInfo", vars(deadline))
        self.assertNotIn("zoneinfo", vars(deadline))
        self.assertEqual(_ny(2026, 1, 15, 12).utcoffset(), timedelta(hours=-5))
        self.assertEqual(_ny(2026, 7, 15, 12).utcoffset(), timedelta(hours=-4))
        self.assertEqual(_ny(2026, 3, 8, 1, 59).utcoffset(), timedelta(hours=-5))
        self.assertEqual(_ny(2026, 3, 8, 3, 0).utcoffset(), timedelta(hours=-4))
        self.assertEqual(_ny(2026, 11, 1, 0, 59).utcoffset(), timedelta(hours=-4))
        self.assertEqual(_ny(2026, 11, 1, 2, 0).utcoffset(), timedelta(hours=-5))

    def test_repeated_november_hour_converts_from_utc_to_standard_time(self) -> None:
        # 05:00 UTC is the first 01:00 (EDT); 06:00 UTC is the second 01:00 (EST) and must round-trip.
        first = datetime(2026, 11, 1, 5, 0, tzinfo=timezone.utc).astimezone(NY)
        second = datetime(2026, 11, 1, 6, 0, tzinfo=timezone.utc).astimezone(NY)
        self.assertEqual(first.utcoffset(), timedelta(hours=-4))
        self.assertEqual(second.utcoffset(), timedelta(hours=-5))
        self.assertEqual(second.astimezone(timezone.utc), datetime(2026, 11, 1, 6, 0, tzinfo=timezone.utc))
        self.assertEqual(deadline.format_timestamp(second), "2026-11-01T01:00:00-05:00")

    def test_parse_timestamp_accepts_iso_8601_offsets_including_z(self) -> None:
        expected = datetime(2026, 8, 17, 14, 0, tzinfo=timezone.utc)
        for text in (
            "2026-08-17T14:00:00Z",
            "2026-08-17T14:00:00+00:00",
            "2026-08-17T10:00:00-04:00",
            "2026-08-17T14:00:00+0000",
            "2026-08-17T09:00:00-0500",
            "2026-08-17T14:00:00.0Z",
            "2026-08-17T14:00:00.000000+00:00",
        ):
            parsed = deadline.parse_timestamp(text)
            self.assertEqual(parsed, expected, text)
            self.assertEqual(parsed.utcoffset(), timedelta(0), text)
        self.assertEqual(
            deadline.parse_timestamp("2026-08-17T14:00:00.25Z"),
            datetime(2026, 8, 17, 14, 0, 0, 250000, tzinfo=timezone.utc),
        )
        self.assertEqual(deadline.parse_timestamp("2026-08-17T14:00:00.123456789Z").microsecond, 123456)

    def test_parse_timestamp_rejects_naive_and_malformed_input(self) -> None:
        for text in (
            "2026-08-17T14:00:00",
            "2026-08-17",
            "2026-08-17 14:00:00Z",
            "2026-08-17T14:00Z",
            "2026-08-17T25:00:00Z",
            "2026-13-01T00:00:00Z",
            "2026-08-17T14:00:00+24:00",
            "2026-08-17T14:00:00+05",
            "garbage",
            "",
        ):
            with self.assertRaises(ValueError, msg=repr(text)) as context:
                deadline.parse_timestamp(text)
            self.assertIn("ISO-8601", str(context.exception), text)

    def test_parse_timestamp_does_not_depend_on_fromisoformat(self) -> None:
        class GuardedDatetime(datetime):
            @classmethod
            def fromisoformat(cls, text: str) -> GuardedDatetime:
                raise AssertionError("parse_timestamp must not call datetime.fromisoformat")

        with unittest.mock.patch.object(deadline, "datetime", GuardedDatetime):
            parsed = deadline.parse_timestamp("2026-08-17T14:00:00Z")
        self.assertEqual(parsed, datetime(2026, 8, 17, 14, 0, tzinfo=timezone.utc))

    def test_utc_timestamps_round_trip_through_z(self) -> None:
        moment = datetime(2026, 8, 17, 14, 30, 15, 500000, tzinfo=timezone.utc)
        text = deadline.format_utc_timestamp(moment)
        self.assertEqual(text, "2026-08-17T14:30:15.500000Z")
        self.assertEqual(deadline.parse_timestamp(text), moment)
        self.assertEqual(deadline.format_utc_timestamp(_ny(2026, 8, 19, 17)), "2026-08-19T21:00:00Z")
        new_york_text = deadline.format_timestamp(_ny(2026, 8, 19, 17))
        self.assertEqual(new_york_text, "2026-08-19T17:00:00-04:00")
        self.assertEqual(deadline.parse_timestamp(new_york_text), _ny(2026, 8, 19, 17))

    def test_overdue_boundary_at_1700(self) -> None:
        due = _ny(2026, 8, 19, 17)
        self.assertFalse(deadline.is_overdue(due, now=_ny(2026, 8, 19, 16, 59, 59)))
        self.assertFalse(deadline.is_overdue(due, now=_ny(2026, 8, 19, 17, 0, 0)))
        self.assertTrue(deadline.is_overdue(due, now=_ny(2026, 8, 19, 17, 0, 1)))


class LedgerTests(unittest.TestCase):
    def test_proposed_record_preserves_the_runner_finding_id(self) -> None:
        record = ledger.proposed_record(
            finding_id="fstcf-v1-0123456789abcdef0123",
            family="f",
            case_id="c",
            dimension="d",
            issue_url="https://x/1",
            closure_packet_url="https://x/2",
            permanent_test_paths=["p"],
        )
        self.assertEqual(record["finding_id"], "fstcf-v1-0123456789abcdef0123")
        with self.assertRaises(ValueError):
            ledger.proposed_record(
                finding_id="",
                family="f",
                case_id="c",
                dimension="d",
                issue_url="https://x/1",
                closure_packet_url="https://x/2",
                permanent_test_paths=["p"],
            )

    def test_finding_id_must_match_the_runner_format_before_any_path_use(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for bad in ("../escape", "fstcf-v1-ABCDEF0123456789ABCD", "fstcf-v1-0123", "x/fstcf-v1-" + "0" * 20, "."):
                with self.assertRaises(ValueError, msg=repr(bad)):
                    ledger.proposed_record(
                        finding_id=bad,
                        family="f",
                        case_id="c",
                        dimension="d",
                        issue_url="https://x/1",
                        closure_packet_url="https://x/2",
                        permanent_test_paths=["p"],
                    )
                record = ledger.proposed_record(
                    finding_id="fstcf-v1-" + "0" * 20,
                    family="f",
                    case_id="c",
                    dimension="d",
                    issue_url="https://x/1",
                    closure_packet_url="https://x/2",
                    permanent_test_paths=["p"],
                )
                with self.assertRaises(ValueError, msg=repr(bad)):
                    ledger.write_record(root, dict(record, finding_id=bad))
            self.assertEqual(sorted(root.iterdir()), [])
            self.assertFalse((root.parent / "escape.json").exists())

    def test_proposed_record_matches_runner_ledger_schema(self) -> None:
        record = ledger.proposed_record(
            finding_id="fstcf-v1-eeeeeeeeeeeeeeeeeeee",
            family="union_destination_membership",
            case_id="case-a",
            dimension="destination",
            issue_url="https://github.com/cafecito-games/Foundry/issues/1",
            closure_packet_url="https://github.com/cafecito-games/Foundry/pull/2",
            permanent_test_paths=["modules/foundry_script/tests/scripts/x.fs"],
        )
        self.assertEqual(
            set(record),
            {
                "schema_version",
                "finding_id",
                "case_id",
                "family",
                "dimension",
                "classification",
                "issue_url",
                "closure_packet_url",
                "permanent_test_paths",
            },
        )
        self.assertEqual(record["classification"], "unclassified")
        self.assertEqual(record["schema_version"], 1)

    def test_validate_record_rejects_what_the_runner_rejects(self) -> None:
        valid = ledger.proposed_record(
            finding_id="fstcf-v1-eeeeeeeeeeeeeeeeeeee",
            family="f",
            case_id="c",
            dimension="d",
            issue_url="https://x/1",
            closure_packet_url="https://x/2",
            permanent_test_paths=["p"],
        )
        ledger.validate_record(valid)
        rejected = [
            dict(valid, schema_version="1"),
            dict(valid, schema_version=2),
            dict(valid, dimension=""),
            dict(valid, issue_url="ftp://x/1"),
            dict(valid, closure_packet_url="pull/2"),
            dict(valid, permanent_test_paths=[]),
            dict(valid, permanent_test_paths=["p", "p"]),
            dict(valid, permanent_test_paths=[""]),
            dict(valid, classification="bogus"),
        ]
        for record in rejected:
            with self.assertRaises(ValueError, msg=repr(record)):
                ledger.validate_record(record)

    def test_record_digest_is_canonical(self) -> None:
        record = {"b": 1, "a": [1, 2]}
        self.assertEqual(ledger.record_digest(record), ledger.record_digest({"a": [1, 2], "b": 1}))

    def test_write_and_read_ledger_file_round_trips(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            record = ledger.proposed_record(
                finding_id="fstcf-v1-eeeeeeeeeeeeeeeeeeee",
                family="f",
                case_id="c",
                dimension="d",
                issue_url="https://x/1",
                closure_packet_url="https://x/2",
                permanent_test_paths=["p"],
            )
            path = ledger.write_record(Path(directory), record)
            self.assertEqual(path.name, record["finding_id"] + ".json")
            self.assertEqual(ledger.read_record(path), record)
            self.assertEqual(ledger.load_ledger(Path(directory)), {record["finding_id"]: record})


class ProvisionalRecordTests(unittest.TestCase):
    def _record(self, **overrides: Any) -> Any:
        payload = ledger.proposed_record(
            finding_id="fstcf-v1-eeeeeeeeeeeeeeeeeeee",
            family="f",
            case_id="c",
            dimension="d",
            issue_url="https://x/1",
            closure_packet_url="https://x/2",
            permanent_test_paths=["p"],
        )
        fields: dict[str, Any] = {
            "finding_id": payload["finding_id"],
            "payload": payload,
            "capability_slice": ["modules/foundry_script/fs_analyzer.cpp"],
            "workstream_owner": "owner",
            "detection_artifact": "https://ci/artifact",
            "develop_comparison": _artifact_with_status(comparator.Status.UNCHANGED).to_dict(),
            "capabilities": CAPABILITIES,
            "detected_at": _ny(2026, 8, 17, 10),
            "bot_pr_url": "https://x/2",
            "origin": "automation",
        }
        fields.update(overrides)
        return provisional.ProvisionalRecord.create(**fields)

    def test_due_at_is_derived_from_detection(self) -> None:
        record = self._record()
        self.assertEqual(record.due_at, _ny(2026, 8, 19, 17))
        self.assertEqual(record.state, "pending_merge")
        self.assertEqual(record.payload_digest, ledger.record_digest(record.payload))

    def test_issue_body_round_trip(self) -> None:
        record = self._record()
        body = provisional.render_issue_body(record, title_note="Provisional finding")
        parsed = provisional.parse_issue_body(body, capabilities=CAPABILITIES)
        self.assertEqual(parsed, record)

    def test_parse_rejects_body_without_record(self) -> None:
        with self.assertRaises(provisional.ProvisionalError):
            provisional.parse_issue_body("no machine-readable block here")

    def test_provisional_state_is_required_on_parse(self) -> None:
        data = self._record().to_dict()
        del data["state"]
        with self.assertRaises(provisional.ProvisionalError):
            provisional.ProvisionalRecord.from_dict(data, capabilities=CAPABILITIES)

    def test_parse_rejects_malformed_capability_slice(self) -> None:
        record = self._record()
        for bad_slice in ("modules/foundry_script/fs_analyzer.cpp", [], [""], [1], None):
            data = dict(record.to_dict(), capability_slice=bad_slice)
            with self.assertRaises(provisional.ProvisionalError, msg=repr(bad_slice)):
                provisional.ProvisionalRecord.from_dict(data, capabilities=CAPABILITIES)
        with self.assertRaises(provisional.ProvisionalError):
            self._record(capability_slice=[])

    def test_capability_slice_outside_the_manifest_slice_is_rejected(self) -> None:
        record = self._record()
        self.assertEqual(record.capability_slice, ("modules/foundry_script/fs_analyzer.cpp",))
        with self.assertRaises(provisional.ProvisionalError):
            self._record(capability_slice=["editor/editor_node.cpp"])
        tampered = dict(record.to_dict(), capability_slice=["editor/editor_node.cpp"])
        with self.assertRaises(provisional.ProvisionalError):
            provisional.ProvisionalRecord.from_dict(tampered, capabilities=CAPABILITIES)

    def test_tampering_both_slice_lists_is_rejected(self) -> None:
        record = self._record()
        comparison = dict(record.develop_comparison)
        comparison["capability_slice"] = dict(comparison["capability_slice"], paths=["editor/editor_node.cpp"])
        tampered = dict(record.to_dict(), capability_slice=["editor/editor_node.cpp"], develop_comparison=comparison)
        with self.assertRaises(provisional.ProvisionalError):
            provisional.ProvisionalRecord.from_dict(tampered, capabilities=CAPABILITIES)
        # Fixing the comparison_id to match the edited slice still fails: the slice must come from the manifest.
        rebound = comparator.ComparisonArtifact.from_dict
        comparison_with_id = dict(comparison)
        comparison_with_id["comparison_id"] = comparator.digest_of(
            {
                "case_id": comparison["case_id"],
                "family": comparison["family"],
                "configuration": comparison["configuration"],
                "category": comparison["category"],
                "capability_slice": comparison["capability_slice"],
                "branch_digest": comparison["branch"]["digest"],
                "develop_digest": comparison["develop"]["digest"],
            }
        )[:16]
        self.assertIsNotNone(rebound)
        tampered = dict(
            record.to_dict(), capability_slice=["editor/editor_node.cpp"], develop_comparison=comparison_with_id
        )
        with self.assertRaises(provisional.ProvisionalError):
            provisional.ProvisionalRecord.from_dict(tampered, capabilities=CAPABILITIES)

    def test_embedded_comparison_is_validated_as_an_artifact(self) -> None:
        record = self._record()
        relabelled = dict(record.develop_comparison, status="resolved")
        with self.assertRaises(provisional.ProvisionalError):
            self._record(develop_comparison=relabelled)
        with self.assertRaises(provisional.ProvisionalError):
            self._record(develop_comparison={"status": "unchanged"})
        with self.assertRaises(provisional.ProvisionalError):
            self._record(develop_comparison=dict(record.develop_comparison, capability_slice=None))

    def test_manual_record_carries_manual_origin_with_same_fields(self) -> None:
        manual = self._record(origin="manual")
        automatic = self._record()
        self.assertEqual(manual.due_at, automatic.due_at)
        self.assertEqual(set(manual.to_dict()), set(automatic.to_dict()))
        self.assertEqual(manual.origin, "manual")


class ReconciliationTests(unittest.TestCase):
    def _provisional(self, **overrides: Any) -> Any:
        payload = ledger.proposed_record(
            finding_id="fstcf-v1-eeeeeeeeeeeeeeeeeeee",
            family="f",
            case_id="c",
            dimension="d",
            issue_url="https://x/1",
            closure_packet_url="https://x/2",
            permanent_test_paths=["p"],
        )
        fields: dict[str, Any] = {
            "finding_id": payload["finding_id"],
            "payload": payload,
            "capability_slice": ["modules/foundry_script/fs_analyzer.cpp"],
            "workstream_owner": "owner",
            "detection_artifact": "https://ci/artifact",
            "develop_comparison": _artifact_with_status(comparator.Status.UNCHANGED).to_dict(),
            "capabilities": CAPABILITIES,
            "detected_at": _ny(2026, 8, 17, 10),
            "bot_pr_url": "https://x/2",
            "origin": "automation",
        }
        fields.update(overrides)
        return provisional.ProvisionalRecord.create(**fields)

    def _reconcile(self, record: Any, merged: dict[str, Any] | None, pr_state: str | None, now: datetime) -> Any:
        return reconcile.reconcile_finding(
            finding_id=record.finding_id if record else "missing",
            provisional_record=record,
            merged_record=merged,
            pull_request=reconcile.PullRequest(url="https://x/2", state=pr_state) if pr_state else None,
            now=now,
        )

    def test_open_pr_without_merged_record_is_pending_merge(self) -> None:
        result = self._reconcile(self._provisional(), None, "open", _ny(2026, 8, 18, 9))
        self.assertEqual(result.state, reconcile.State.PENDING_MERGE)
        self.assertFalse(result.blocks_slice)

    def test_merged_pr_with_matching_record_is_merged(self) -> None:
        record = self._provisional()
        result = self._reconcile(record, record.payload, "merged", _ny(2026, 8, 18, 9))
        self.assertEqual(result.state, reconcile.State.MERGED)

    def test_merged_record_identity_mismatch_is_conflicting(self) -> None:
        record = self._provisional()
        merged = dict(record.payload, case_id="other-case")
        result = self._reconcile(record, merged, "merged", _ny(2026, 8, 18, 9))
        self.assertEqual(result.state, reconcile.State.CONFLICTING)
        self.assertIn("identity", result.reason)
        self.assertTrue(result.blocks_slice)

    def test_classifying_the_merged_record_is_merged_not_conflicting(self) -> None:
        record = self._provisional()
        merged = dict(record.payload, classification="product_defect")
        result = self._reconcile(record, merged, "merged", _ny(2026, 8, 18, 9))
        self.assertEqual(result.state, reconcile.State.MERGED)

    def test_merged_record_with_different_permanent_tests_is_conflicting(self) -> None:
        record = self._provisional()
        merged = dict(record.payload, permanent_test_paths=["somewhere/else.fs"])
        result = self._reconcile(record, merged, "merged", _ny(2026, 8, 18, 9))
        self.assertEqual(result.state, reconcile.State.CONFLICTING)

    def test_reclassifying_an_already_classified_provisional_record_is_conflicting(self) -> None:
        classified = dict(self._provisional().payload, classification="harness_defect")
        record = self._provisional(payload=classified)
        merged = dict(classified, classification="product_defect")
        result = self._reconcile(record, merged, "merged", _ny(2026, 8, 18, 9))
        self.assertEqual(result.state, reconcile.State.CONFLICTING)

    def test_merged_pr_without_ledger_record_is_conflicting(self) -> None:
        result = self._reconcile(self._provisional(), None, "merged", _ny(2026, 8, 18, 9))
        self.assertEqual(result.state, reconcile.State.CONFLICTING)

    def test_closed_unmerged_pr_is_conflicting(self) -> None:
        result = self._reconcile(self._provisional(), None, "closed", _ny(2026, 8, 18, 9))
        self.assertEqual(result.state, reconcile.State.CONFLICTING)

    def test_both_records_absent_cannot_disappear_silently(self) -> None:
        result = self._reconcile(None, None, None, _ny(2026, 8, 18, 9))
        self.assertEqual(result.state, reconcile.State.CONFLICTING)
        self.assertIn("no authority", result.reason)

    def test_pending_record_past_deadline_is_overdue_and_blocks_slice(self) -> None:
        result = self._reconcile(self._provisional(), None, "open", _ny(2026, 8, 19, 17, 0, 1))
        self.assertEqual(result.state, reconcile.State.OVERDUE)
        self.assertTrue(result.blocks_slice)
        self.assertEqual(result.capability_slice, ("modules/foundry_script/fs_analyzer.cpp",))

    def test_merged_but_unclassified_record_past_deadline_is_overdue(self) -> None:
        record = self._provisional()
        result = self._reconcile(record, record.payload, "merged", _ny(2026, 8, 20, 9))
        self.assertEqual(result.state, reconcile.State.OVERDUE)

    def test_merged_classified_record_past_deadline_is_merged(self) -> None:
        record = self._provisional()
        merged = dict(record.payload, classification="product_defect")
        record = self._provisional(payload=merged)
        result = self._reconcile(record, merged, "merged", _ny(2026, 8, 20, 9))
        self.assertEqual(result.state, reconcile.State.MERGED)

    def test_resolved_when_case_passes_on_both_and_ledger_entry_withdrawn(self) -> None:
        record = self._provisional()
        result = reconcile.reconcile_finding(
            finding_id=record.finding_id,
            provisional_record=record,
            merged_record=None,
            pull_request=reconcile.PullRequest(url="https://x/2", state="closed"),
            now=_ny(2026, 8, 18, 9),
            comparison_status="resolved",
        )
        self.assertEqual(result.state, reconcile.State.RESOLVED)
        self.assertFalse(result.blocks_slice)

    def test_resolved_comparison_does_not_mask_a_merged_pr_without_ledger_entry(self) -> None:
        record = self._provisional()
        result = reconcile.reconcile_finding(
            finding_id=record.finding_id,
            provisional_record=record,
            merged_record=None,
            pull_request=reconcile.PullRequest(url="https://x/2", state="merged"),
            now=_ny(2026, 8, 18, 9),
            comparison_status="resolved",
        )
        self.assertEqual(result.state, reconcile.State.CONFLICTING)

    def test_passing_case_with_withdrawn_ledger_pr_is_resolved(self) -> None:
        record = self._provisional()
        for status in comparator.NO_LONGER_FAILING_STATUSES:
            result = reconcile.reconcile_finding(
                finding_id=record.finding_id,
                provisional_record=record,
                merged_record=None,
                pull_request=reconcile.PullRequest(url="https://x/2", state="closed"),
                now=_ny(2026, 8, 18, 9),
                comparison_status=status.value,
            )
            self.assertEqual(result.state, reconcile.State.RESOLVED, status)
            self.assertFalse(result.blocks_slice)
            self.assertFalse(result.blocks_release)
        self.assertEqual(
            set(comparator.NO_LONGER_FAILING_STATUSES), {comparator.Status.RESOLVED, comparator.Status.PASSING}
        )

    def test_manual_records_reconcile_identically(self) -> None:
        manual = self._provisional(origin="manual")
        result = self._reconcile(manual, None, "open", _ny(2026, 8, 19, 17, 0, 1))
        self.assertEqual(result.state, reconcile.State.OVERDUE)

    def test_ledger_file_without_pull_request_evidence_never_clears_blocking(self) -> None:
        record = self._provisional()
        classified = dict(record.payload, classification="product_defect")
        result = self._reconcile(record, classified, None, _ny(2026, 8, 18, 9))
        self.assertEqual(result.state, reconcile.State.PENDING_MERGE)
        self.assertTrue(result.blocks_release)
        self.assertTrue(result.unclassified)
        overdue = self._reconcile(record, classified, None, _ny(2026, 8, 25, 9))
        self.assertEqual(overdue.state, reconcile.State.OVERDUE)
        self.assertTrue(overdue.blocks_slice)

    def test_blocked_slices_only_include_overdue_and_conflicting(self) -> None:
        pending = self._reconcile(self._provisional(), None, "open", _ny(2026, 8, 18, 9))
        overdue = self._reconcile(
            self._provisional(capability_slice=["modules/foundry_script/fs_vm.cpp"]),
            None,
            "open",
            _ny(2026, 8, 25, 9),
        )
        blocked = reconcile.blocked_slices([pending, overdue])
        self.assertEqual(blocked, {("modules/foundry_script/fs_vm.cpp",)})
        self.assertTrue(reconcile.blocks_release([pending, overdue]))
        self.assertFalse(reconcile.blocks_path([pending, overdue], "modules/foundry_script/fs_analyzer.cpp"))
        self.assertTrue(reconcile.blocks_path([pending, overdue], "modules/foundry_script/fs_vm.cpp"))

    def test_unclassified_findings_block_release_even_before_the_deadline(self) -> None:
        record = self._provisional()
        pending = self._reconcile(record, None, "open", _ny(2026, 8, 18, 9))
        merged_unclassified = self._reconcile(record, record.payload, "merged", _ny(2026, 8, 18, 9))
        classified = dict(record.payload, classification="product_defect")
        merged_classified = self._reconcile(record, classified, "merged", _ny(2026, 8, 18, 9))
        self.assertTrue(reconcile.blocks_release([pending]))
        self.assertTrue(reconcile.blocks_release([merged_unclassified]))
        self.assertFalse(reconcile.blocks_release([merged_classified]))
        self.assertFalse(pending.blocks_slice)
        self.assertTrue(pending.to_dict()["blocks_release"])


def _artifact_with_status(status: Any) -> Any:
    """Build an artifact of the given Status through the comparator itself, never by hand."""
    failing = _case("a", passed=False)
    worse = _case("a", passed=False, runtime_status="crash", runtime_passed=False)
    passing = _case("a", passed=True)
    finding = [_finding("a", "destination")]
    sides: dict[Any, tuple[list[dict[str, Any]], list[dict[str, Any]], list[dict[str, Any]]]] = {
        comparator.Status.NEW: ([failing], [passing], finding),
        comparator.Status.WORSENED: ([worse], [failing], finding),
        comparator.Status.UNCHANGED: ([failing], [failing], finding),
        comparator.Status.RESOLVED: ([passing], [failing], []),
        comparator.Status.MISSING: ([], [failing], []),
        comparator.Status.VANISHED: ([], [passing], []),
        comparator.Status.PASSING: ([passing], [passing], []),
    }
    branch_cases, develop_cases, branch_findings = sides[status]
    branch = report.load_report(_report(branch_cases, findings=branch_findings))
    develop = report.load_report(
        _report(develop_cases, findings=finding if develop_cases and not develop_cases[0]["passed"] else [])
    )
    artifact = comparator.compare_case(branch, develop, "a", configuration="text", capabilities=CAPABILITIES)
    assert artifact.status is status, (artifact.status, status)
    return artifact


class EveryStatusConsumerTests(unittest.TestCase):
    def test_status_partitions_are_complete(self) -> None:
        every = set(comparator.Status)
        self.assertEqual(
            every,
            set(comparator.REGRESSION_STATUSES)
            | set(comparator.NO_LONGER_FAILING_STATUSES)
            | set(comparator.PROPOSABLE_STATUSES)
            | set(comparator.KNOWN_BASELINE_STATUSES),
        )
        self.assertEqual(
            set(comparator.PROPOSABLE_STATUSES),
            {comparator.Status.NEW, comparator.Status.WORSENED, comparator.Status.UNCHANGED},
        )
        self.assertFalse(set(comparator.REGRESSION_STATUSES) & set(comparator.NO_LONGER_FAILING_STATUSES))

    def test_every_status_round_trips_and_every_consumer_handles_it(self) -> None:
        for status in comparator.Status:
            with self.subTest(status=status.value):
                artifact = _artifact_with_status(status)
                text = comparator.serialize_many([artifact])
                restored = comparator.deserialize_many(text)[0]
                self.assertEqual(restored.to_dict(), artifact.to_dict())
                with tempfile.TemporaryDirectory() as directory:
                    root = Path(directory)
                    (root / "comparison.json").write_text(text)
                    exit_code = cli.main(
                        [
                            "propose",
                            "--comparison",
                            str(root / "comparison.json"),
                            "--case-id",
                            "a",
                            "--dimension",
                            "destination",
                            "--issue-url",
                            "https://x/1",
                            "--closure-packet-url",
                            "https://x/2",
                            "--permanent-test-path",
                            "p",
                            "--workstream-owner",
                            "o",
                            "--detection-artifact",
                            "d",
                            "--detected-at",
                            "2026-08-17T10:00:00-04:00",
                            "--capabilities",
                            str(_CAPABILITIES_FILE),
                            "--output-dir",
                            str(root / "out"),
                        ]
                    )
                    if status in comparator.PROPOSABLE_STATUSES:
                        self.assertEqual(exit_code, 0)
                        self.assertTrue((root / "out" / "provisional.json").exists())
                    else:
                        self.assertEqual(exit_code, 2)
                        self.assertFalse((root / "out").exists())
                payload = ledger.proposed_record(
                    finding_id="fstcf-v1-ffffffffffffffffffff",
                    family="union_destination_membership",
                    case_id="a",
                    dimension="destination",
                    issue_url="https://x/1",
                    closure_packet_url="https://x/2",
                    permanent_test_paths=["p"],
                )
                record = provisional.ProvisionalRecord.create(
                    finding_id=payload["finding_id"],
                    payload=payload,
                    capability_slice=["modules/foundry_script/fs_analyzer.cpp"],
                    workstream_owner="o",
                    detection_artifact="d",
                    develop_comparison=artifact.to_dict(),
                    detected_at=_ny(2026, 8, 17, 10),
                    bot_pr_url="https://x/2",
                    origin="automation",
                    capabilities=CAPABILITIES,
                )
                for pull_request in (None, "open", "closed", "merged"):
                    result = reconcile.reconcile_finding(
                        finding_id=record.finding_id,
                        provisional_record=record,
                        merged_record=None,
                        pull_request=None
                        if pull_request is None
                        else reconcile.PullRequest(url="u", state=pull_request),
                        now=_ny(2026, 8, 18, 9),
                        comparison_status=status.value,
                    )
                    self.assertIsInstance(result.state, reconcile.State)
                    self.assertIsInstance(result.to_dict()["blocks_release"], bool)

    def test_deserialize_rejects_artifact_shape_inconsistent_with_status(self) -> None:
        artifact = _artifact_with_status(comparator.Status.UNCHANGED).to_dict()
        artifact["branch"] = dict(artifact["branch"], present=False)
        with self.assertRaises(report.ReportError):
            comparator.deserialize_many(json.dumps({"schema_version": 1, "artifacts": [artifact]}))
        vanished = _artifact_with_status(comparator.Status.VANISHED).to_dict()
        vanished["status"] = "new"
        with self.assertRaises(report.ReportError):
            comparator.deserialize_many(json.dumps({"schema_version": 1, "artifacts": [vanished]}))
        unknown = _artifact_with_status(comparator.Status.NEW).to_dict()
        unknown["status"] = "sideways"
        with self.assertRaises(report.ReportError):
            comparator.deserialize_many(json.dumps({"schema_version": 1, "artifacts": [unknown]}))

    def test_relabelled_status_is_rejected_for_every_pair(self) -> None:
        for status in comparator.Status:
            artifact = _artifact_with_status(status).to_dict()
            for other in comparator.Status:
                if other is status:
                    continue
                with self.subTest(status=status.value, relabelled=other.value):
                    tampered = dict(artifact, status=other.value)
                    with self.assertRaises(report.ReportError):
                        comparator.deserialize_many(json.dumps({"schema_version": 1, "artifacts": [tampered]}))

    def test_tampered_side_values_are_rejected(self) -> None:
        artifact = _artifact_with_status(comparator.Status.UNCHANGED).to_dict()
        # Claim the branch passes while keeping the 'unchanged' label.
        tampered = dict(artifact, branch=dict(artifact["branch"], passed=True))
        with self.assertRaises(report.ReportError):
            comparator.deserialize_many(json.dumps({"schema_version": 1, "artifacts": [tampered]}))
        # Claim a different develop digest while keeping the 'unchanged' label.
        tampered = dict(artifact, develop=dict(artifact["develop"], digest="0" * 64))
        with self.assertRaises(report.ReportError):
            comparator.deserialize_many(json.dumps({"schema_version": 1, "artifacts": [tampered]}))
        # A digest that does not match the recorded observation is rejected too.
        tampered = dict(artifact, branch=dict(artifact["branch"], digest="0" * 64))
        with self.assertRaises(report.ReportError):
            comparator.deserialize_many(json.dumps({"schema_version": 1, "artifacts": [tampered]}))

    def _reject(self, artifact: dict[str, Any], label: str) -> None:
        with self.subTest(edit=label):
            with self.assertRaises(report.ReportError):
                comparator.deserialize_many(json.dumps({"schema_version": 1, "artifacts": [artifact]}))

    def test_every_identity_field_is_bound(self) -> None:
        base = _artifact_with_status(comparator.Status.UNCHANGED).to_dict()
        finding = base["branch"]["findings"][0]
        self._reject(dict(base, case_id="other"), "artifact case_id")
        self._reject(dict(base, family="other_family"), "artifact family")
        self._reject(dict(base, configuration="bytecode"), "configuration")
        self._reject(dict(base, category="structural_failure"), "category")
        self._reject(dict(base, comparison_id="0" * 16), "comparison_id")
        self._reject(
            dict(base, capability_slice=dict(base["capability_slice"], paths=["editor/editor_node.cpp"])), "slice paths"
        )
        self._reject(dict(base, capability_slice=dict(base["capability_slice"], family="other")), "slice family")
        self._reject(dict(base, capability_slice=dict(base["capability_slice"], broad_core=False)), "slice broad_core")
        tampered_finding = dict(finding, finding_id="fstcf-v1-" + "0" * 20)
        self._reject(
            dict(base, branch=dict(base["branch"], findings=[tampered_finding])), "finding_id (digest still matches)"
        )
        self._reject(
            dict(base, branch=dict(base["branch"], findings=[dict(finding, case_id="other")])), "finding case_id"
        )
        self._reject(
            dict(base, branch=dict(base["branch"], findings=[dict(finding, family="other")])), "finding family"
        )
        self._reject(
            dict(base, branch=dict(base["branch"], findings=[dict(finding, dimension="source_proof")])),
            "finding dimension",
        )

    def test_migrated_finding_identity_uses_the_historical_case(self) -> None:
        historical_id = _runner_finding_id("old", "destination")
        echoed = dict(
            _finding("new_a", "destination"),
            finding_id=historical_id,
            migrated_from="old",
            resolved_case_ids=["new_a", "new_b"],
        )
        branch = report.load_report(_report([_case("new_a", passed=False)], findings=[echoed]))
        artifact = comparator.compare_case(branch, branch, "new_a", configuration="text", capabilities=CAPABILITIES)
        restored = comparator.deserialize_many(comparator.serialize_many([artifact]))[0]
        self.assertEqual(restored.branch["findings"][0]["finding_id"], historical_id)
        wrong = dict(echoed, migrated_from="someone_else")
        branch = report.load_report(_report([_case("new_a", passed=False)], findings=[wrong]))
        artifact = comparator.compare_case(branch, branch, "new_a", configuration="text", capabilities=CAPABILITIES)
        with self.assertRaises(report.ReportError):
            comparator.deserialize_many(comparator.serialize_many([artifact]))

    def test_every_reconcile_state_serializes(self) -> None:
        for state in reconcile.State:
            result = reconcile.Reconciliation("fstcf-v1-" + "0" * 20, state, "r", ("p",), None, True)
            payload = result.to_dict()
            self.assertEqual(payload["state"], state.value)
            self.assertIn(payload["blocks_capability_slice"], (True, False))


class FakeCommandRunner:
    def __init__(self, responses: dict[str, str] | None = None) -> None:
        self.calls: list[list[str]] = []
        self.responses = responses or {}

    def __call__(self, arguments: list[str]) -> str:
        self.calls.append(list(arguments))
        for key, value in self.responses.items():
            if key in " ".join(arguments):
                return value
        return ""


class SequencedCommandRunner:
    """Fake gh whose listing answers change after a create, to model a concurrent run racing ours."""

    def __init__(self, before: str, after: str, create_url: str) -> None:
        self.calls: list[list[str]] = []
        self.before, self.after, self.create_url = before, after, create_url
        self.created = False

    def __call__(self, arguments: list[str]) -> str:
        self.calls.append(list(arguments))
        joined = " ".join(arguments)
        if " list" in joined:
            return self.after if self.created else self.before
        if " create" in joined:
            self.created = True
            return self.create_url
        return ""


class ConcurrentCreateTests(unittest.TestCase):
    def test_duplicate_tracking_issues_after_create_converge_on_the_lowest(self) -> None:
        after = json.dumps(
            [
                {"url": "https://github.com/x/issues/12", "number": 12, "state": "OPEN"},
                {"url": "https://github.com/x/issues/11", "number": 11, "state": "OPEN"},
            ]
        )
        runner = SequencedCommandRunner("[]", after, "https://github.com/x/issues/12\n")
        client = github.AutomationClient(run=runner, repository="cafecito-games/Foundry")
        url = client.create_or_update_tracking_issue("finding-1", title="t", body="b")
        self.assertEqual(url, "https://github.com/x/issues/11")
        joined = [" ".join(call) for call in runner.calls]
        self.assertEqual(sum(call[1:3] == ["issue", "create"] for call in runner.calls), 1)
        self.assertTrue(any("issue comment 12" in call and "issues/11" in call for call in joined))
        self.assertTrue(any("issue close 12" in call for call in joined))
        self.assertFalse(any("close 11" in call for call in joined))

    def test_single_issue_after_create_is_returned_unchanged(self) -> None:
        after = json.dumps([{"url": "https://github.com/x/issues/5", "number": 5, "state": "OPEN"}])
        runner = SequencedCommandRunner("[]", after, "https://github.com/x/issues/5\n")
        client = github.AutomationClient(run=runner, repository="cafecito-games/Foundry")
        self.assertEqual(
            client.create_or_update_tracking_issue("finding-1", title="t", body="b"), "https://github.com/x/issues/5"
        )
        self.assertFalse(any("close" in " ".join(call) for call in runner.calls))

    def test_duplicate_ledger_pull_requests_after_create_converge_on_the_lowest(self) -> None:
        after = json.dumps(
            [
                {"url": "https://github.com/x/pull/22", "number": 22, "state": "OPEN", "baseRefName": "develop"},
                {"url": "https://github.com/x/pull/21", "number": 21, "state": "OPEN", "baseRefName": "develop"},
            ]
        )
        runner = SequencedCommandRunner("[]", after, "https://github.com/x/pull/22\n")
        client = github.AutomationClient(run=runner, repository="cafecito-games/Foundry")
        url = client.open_or_update_ledger_pull_request(
            "bot/type-completeness/abc", title="t", body="b", base="develop"
        )
        self.assertEqual(url, "https://github.com/x/pull/21")
        joined = [" ".join(call) for call in runner.calls]
        self.assertEqual(sum(call[1:3] == ["pr", "create"] for call in runner.calls), 1)
        self.assertTrue(any("pr close 22" in call and "pull/21" in call for call in joined))
        self.assertFalse(any("close 21" in call for call in joined))
        self.assertFalse(any("merge" in call or "--auto" in call for call in joined))


class FakeGit:
    """Fake git: answers ls-remote with a configured SHA (or nothing) and optionally rejects the push."""

    def __init__(self, remote_sha: str | None, reject_push: bool = False) -> None:
        self.calls: list[list[str]] = []
        self.remote_sha = remote_sha
        self.reject_push = reject_push

    def __call__(self, arguments: list[str]) -> str:
        self.calls.append(list(arguments))
        if "ls-remote" in arguments:
            return f"{self.remote_sha}\trefs/heads/bot/type-completeness/abc\n" if self.remote_sha else ""
        if "push" in arguments and self.reject_push:
            raise subprocess.CalledProcessError(1, arguments, stderr="! [rejected] stale info")
        return ""


class LeasePushTests(unittest.TestCase):
    BRANCH = "bot/type-completeness/abc"

    def test_observed_remote_sha_is_carried_into_the_lease(self) -> None:
        git = FakeGit("a" * 40)
        client = github.AutomationClient(run=git, repository="cafecito-games/Foundry")
        observed = client.observe_remote_branch(self.BRANCH, Path("/repo"))
        self.assertEqual(observed, "a" * 40)
        client.push_ledger_branch(self.BRANCH, Path("/repo"), expected_sha=observed)
        push = next(call for call in git.calls if "push" in call)
        self.assertIn(f"--force-with-lease=refs/heads/{self.BRANCH}:{'a' * 40}", push)
        self.assertNotIn("--force-with-lease", push)
        self.assertNotIn("--force", push)

    def test_new_branch_expects_an_empty_lease(self) -> None:
        git = FakeGit(None)
        client = github.AutomationClient(run=git, repository="cafecito-games/Foundry")
        observed = client.observe_remote_branch(self.BRANCH, Path("/repo"))
        self.assertIsNone(observed)
        client.push_ledger_branch(self.BRANCH, Path("/repo"), expected_sha=observed)
        push = next(call for call in git.calls if "push" in call)
        self.assertIn(f"--force-with-lease=refs/heads/{self.BRANCH}:", push)

    def test_rejected_lease_is_an_error_with_no_retry(self) -> None:
        git = FakeGit("b" * 40, reject_push=True)
        client = github.AutomationClient(run=git, repository="cafecito-games/Foundry")
        with self.assertRaises(github.AutomationError) as context:
            client.push_ledger_branch(self.BRANCH, Path("/repo"), expected_sha="a" * 40)
        message = str(context.exception)
        self.assertIn("a" * 40, message)
        self.assertIn("b" * 40, message)
        self.assertEqual(sum("push" in call for call in git.calls), 1)
        self.assertFalse(any("fetch" in call for call in git.calls))

    def test_push_requires_an_explicit_lease_decision(self) -> None:
        git = FakeGit("a" * 40)
        client = github.AutomationClient(run=git, repository="cafecito-games/Foundry")
        with self.assertRaises(TypeError):
            client.push_ledger_branch(self.BRANCH, Path("/repo"))  # type: ignore[call-arg]
        self.assertEqual(git.calls, [])


class GitHubAutomationTests(unittest.TestCase):
    def test_refuses_to_push_to_protected_branch(self) -> None:
        runner = FakeCommandRunner()
        client = github.AutomationClient(run=runner, repository="cafecito-games/Foundry")
        for branch in ("develop", "main", "master"):
            with self.assertRaises(github.ProtectedBranchError):
                client.push_ledger_branch(branch, Path("/nowhere"), expected_sha=None)
        self.assertEqual(runner.calls, [])

    def test_refuses_to_open_or_update_a_pull_request_from_a_non_bot_branch(self) -> None:
        runner = FakeCommandRunner(
            {
                "pr list": json.dumps(
                    [{"url": "https://github.com/x/pull/3", "number": 3, "state": "OPEN", "baseRefName": "develop"}]
                )
            }
        )
        client = github.AutomationClient(run=runner, repository="cafecito-games/Foundry")
        with self.assertRaises(github.ProtectedBranchError):
            client.open_or_update_ledger_pull_request("feature/unrelated", title="t", body="b", base="develop")
        self.assertEqual(runner.calls, [])

    def test_bot_branch_name_is_stable_per_finding(self) -> None:
        self.assertEqual(github.bot_branch_name("abc-0123"), "bot/type-completeness/abc-0123")

    def test_open_ledger_pr_never_enables_auto_merge(self) -> None:
        runner = FakeCommandRunner({"pr list": "[]", "pr create": "https://github.com/x/pull/9"})
        client = github.AutomationClient(run=runner, repository="cafecito-games/Foundry")
        url = client.open_or_update_ledger_pull_request(
            "bot/type-completeness/abc", title="t", body="b", base="develop"
        )
        self.assertEqual(url, "https://github.com/x/pull/9")
        joined = [" ".join(call) for call in runner.calls]
        self.assertFalse(any("merge" in call for call in joined))
        self.assertFalse(any("--auto" in call for call in joined))

    def test_closed_unmerged_ledger_pr_is_reopened_and_updated(self) -> None:
        listing = json.dumps(
            [{"url": "https://github.com/x/pull/3", "number": 3, "state": "CLOSED", "baseRefName": "develop"}]
        )
        runner = FakeCommandRunner({"pr list": listing})
        client = github.AutomationClient(run=runner, repository="cafecito-games/Foundry")
        url = client.open_or_update_ledger_pull_request(
            "bot/type-completeness/abc", title="t", body="b", base="develop"
        )
        self.assertEqual(url, "https://github.com/x/pull/3")
        joined = [" ".join(call) for call in runner.calls]
        self.assertTrue(any("pr list" in call and "--state all" in call for call in joined))
        self.assertTrue(any("pr reopen 3" in call for call in joined))
        self.assertTrue(any("pr edit 3" in call for call in joined))
        self.assertFalse(any("pr create" in call for call in joined))

    def test_merged_ledger_pr_is_left_alone(self) -> None:
        listing = json.dumps(
            [{"url": "https://github.com/x/pull/3", "number": 3, "state": "MERGED", "baseRefName": "develop"}]
        )
        runner = FakeCommandRunner({"pr list": listing})
        client = github.AutomationClient(run=runner, repository="cafecito-games/Foundry")
        url = client.open_or_update_ledger_pull_request(
            "bot/type-completeness/abc", title="t", body="b", base="develop"
        )
        self.assertEqual(url, "https://github.com/x/pull/3")
        joined = [" ".join(call) for call in runner.calls]
        self.assertEqual([call for call in joined if "pr list" not in call], [])

    def test_merged_ledger_pr_outranks_an_older_closed_one(self) -> None:
        listing = json.dumps(
            [
                {"url": "https://github.com/x/pull/5", "number": 5, "state": "CLOSED", "baseRefName": "develop"},
                {"url": "https://github.com/x/pull/9", "number": 9, "state": "MERGED", "baseRefName": "develop"},
            ]
        )
        runner = FakeCommandRunner({"pr list": listing})
        client = github.AutomationClient(run=runner, repository="cafecito-games/Foundry")
        url = client.open_or_update_ledger_pull_request(
            "bot/type-completeness/abc", title="t", body="b", base="develop"
        )
        self.assertEqual(url, "https://github.com/x/pull/9")
        self.assertEqual([call for call in runner.calls if call[1:3] != ["pr", "list"]], [])

    def test_open_pr_to_another_base_is_retargeted_not_reused(self) -> None:
        listing = json.dumps(
            [{"url": "https://github.com/x/pull/4", "number": 4, "state": "OPEN", "baseRefName": "main"}]
        )
        runner = FakeCommandRunner({"pr list": listing})
        client = github.AutomationClient(run=runner, repository="cafecito-games/Foundry")
        url = client.open_or_update_ledger_pull_request(
            "bot/type-completeness/abc", title="t", body="b", base="develop"
        )
        self.assertEqual(url, "https://github.com/x/pull/4")
        joined = [" ".join(call) for call in runner.calls]
        self.assertTrue(any("pr edit 4" in call and "--base develop" in call for call in joined))
        self.assertFalse(any(call[1:3] == ["pr", "create"] for call in runner.calls))

    def test_two_open_prs_with_other_bases_is_an_error(self) -> None:
        listing = json.dumps(
            [
                {"url": "https://github.com/x/pull/4", "number": 4, "state": "OPEN", "baseRefName": "main"},
                {"url": "https://github.com/x/pull/6", "number": 6, "state": "OPEN", "baseRefName": "release"},
            ]
        )
        runner = FakeCommandRunner({"pr list": listing})
        client = github.AutomationClient(run=runner, repository="cafecito-games/Foundry")
        with self.assertRaises(github.AutomationError):
            client.open_or_update_ledger_pull_request("bot/type-completeness/abc", title="t", body="b", base="develop")
        self.assertFalse(any(call[1:3] in (["pr", "create"], ["pr", "edit"]) for call in runner.calls))

    def test_pr_listing_requests_base_ref(self) -> None:
        runner = FakeCommandRunner({"pr list": "[]", "pr create": "https://github.com/x/pull/9"})
        client = github.AutomationClient(run=runner, repository="cafecito-games/Foundry")
        client.open_or_update_ledger_pull_request("bot/type-completeness/abc", title="t", body="b", base="develop")
        listing = next(call for call in runner.calls if call[1:3] == ["pr", "list"])
        self.assertIn("baseRefName", listing[listing.index("--json") + 1])

    def test_merged_pr_to_another_base_is_not_authoritative(self) -> None:
        listing = json.dumps(
            [
                {"url": "https://github.com/x/pull/4", "number": 4, "state": "MERGED", "baseRefName": "main"},
            ]
        )
        runner = FakeCommandRunner({"pr list": listing, "pr create": "https://github.com/x/pull/10"})
        client = github.AutomationClient(run=runner, repository="cafecito-games/Foundry")
        url = client.open_or_update_ledger_pull_request(
            "bot/type-completeness/abc", title="t", body="b", base="develop"
        )
        self.assertEqual(url, "https://github.com/x/pull/10")

    def test_open_ledger_pr_is_preferred_over_closed_ones(self) -> None:
        listing = json.dumps(
            [
                {"url": "https://github.com/x/pull/3", "number": 3, "state": "CLOSED", "baseRefName": "develop"},
                {"url": "https://github.com/x/pull/5", "number": 5, "state": "OPEN", "baseRefName": "develop"},
            ]
        )
        runner = FakeCommandRunner({"pr list": listing})
        client = github.AutomationClient(run=runner, repository="cafecito-games/Foundry")
        url = client.open_or_update_ledger_pull_request(
            "bot/type-completeness/abc", title="t", body="b", base="develop"
        )
        self.assertEqual(url, "https://github.com/x/pull/5")
        joined = [" ".join(call) for call in runner.calls]
        self.assertTrue(any("pr edit 5" in call for call in joined))
        self.assertFalse(any("reopen" in call for call in joined))

    def test_existing_pr_is_updated_not_duplicated(self) -> None:
        runner = FakeCommandRunner(
            {
                "pr list": json.dumps(
                    [{"url": "https://github.com/x/pull/3", "number": 3, "state": "OPEN", "baseRefName": "develop"}]
                )
            }
        )
        client = github.AutomationClient(run=runner, repository="cafecito-games/Foundry")
        url = client.open_or_update_ledger_pull_request(
            "bot/type-completeness/abc", title="t", body="b", base="develop"
        )
        self.assertEqual(url, "https://github.com/x/pull/3")
        joined = [" ".join(call) for call in runner.calls]
        self.assertTrue(any("pr edit 3" in call for call in joined))
        self.assertFalse(any("pr create" in call for call in joined))

    def test_created_tracking_issue_title_embeds_finding_id_so_lookup_finds_it(self) -> None:
        runner = FakeCommandRunner({"issue list": "[]", "issue create": "https://github.com/x/issues/5"})
        client = github.AutomationClient(run=runner, repository="cafecito-games/Foundry")
        client.create_or_update_tracking_issue("finding-1", title="Union store mismatch", body="b")
        create_call = next(call for call in runner.calls if call[1:3] == ["issue", "create"])
        created_title = create_call[create_call.index("--title") + 1]
        self.assertIn("finding-1", created_title)
        list_call = next(call for call in runner.calls if call[1:3] == ["issue", "list"])
        search = list_call[list_call.index("--search") + 1]
        self.assertIn("finding-1", search)
        self.assertIn("in:title", search)

    def test_closed_tracking_issue_is_reopened_and_updated_not_duplicated(self) -> None:
        listing = json.dumps([{"url": "https://github.com/x/issues/7", "number": 7, "state": "CLOSED"}])
        runner = FakeCommandRunner({"issue list": listing})
        client = github.AutomationClient(run=runner, repository="cafecito-games/Foundry")
        url = client.create_or_update_tracking_issue("finding-1", title="t", body="b")
        self.assertEqual(url, "https://github.com/x/issues/7")
        joined = [" ".join(call) for call in runner.calls]
        self.assertTrue(any("issue list" in call and "--state all" in call for call in joined))
        self.assertTrue(any("issue reopen 7" in call for call in joined))
        self.assertTrue(any("issue edit 7" in call for call in joined))
        self.assertFalse(any("issue create" in call for call in joined))

    def test_open_tracking_issue_is_preferred_over_closed_duplicates(self) -> None:
        listing = json.dumps(
            [
                {"url": "https://github.com/x/issues/7", "number": 7, "state": "CLOSED"},
                {"url": "https://github.com/x/issues/9", "number": 9, "state": "OPEN"},
            ]
        )
        runner = FakeCommandRunner({"issue list": listing})
        client = github.AutomationClient(run=runner, repository="cafecito-games/Foundry")
        self.assertEqual(
            client.create_or_update_tracking_issue("finding-1", title="t", body="b"), "https://github.com/x/issues/9"
        )
        joined = [" ".join(call) for call in runner.calls]
        self.assertTrue(any("issue edit 9" in call for call in joined))
        self.assertFalse(any("reopen" in call for call in joined))

    def test_unknown_github_state_is_an_error_not_a_reopen(self) -> None:
        listing = json.dumps([{"url": "https://github.com/x/issues/7", "number": 7}])
        runner = FakeCommandRunner({"issue list": listing, "pr list": listing})
        client = github.AutomationClient(run=runner, repository="cafecito-games/Foundry")
        with self.assertRaises(github.AutomationError):
            client.create_or_update_tracking_issue("finding-1", title="t", body="b")
        with self.assertRaises(github.AutomationError):
            client.open_or_update_ledger_pull_request("bot/type-completeness/abc", title="t", body="b", base="develop")
        joined = [" ".join(call) for call in runner.calls]
        self.assertFalse(any("reopen" in call or "edit" in call or "create" in call for call in joined))

    def test_tracking_issue_is_created_once_and_then_updated(self) -> None:
        runner = FakeCommandRunner({"issue list": "[]", "issue create": "https://github.com/x/issues/5"})
        client = github.AutomationClient(run=runner, repository="cafecito-games/Foundry")
        url = client.create_or_update_tracking_issue("finding-1", title="t", body="b")
        self.assertEqual(url, "https://github.com/x/issues/5")
        runner = FakeCommandRunner(
            {"issue list": json.dumps([{"url": "https://github.com/x/issues/5", "number": 5, "state": "OPEN"}])}
        )
        client = github.AutomationClient(run=runner, repository="cafecito-games/Foundry")
        self.assertEqual(
            client.create_or_update_tracking_issue("finding-1", title="t", body="b"), "https://github.com/x/issues/5"
        )
        self.assertTrue(any("issue edit 5" in " ".join(call) for call in runner.calls))


class CliTests(unittest.TestCase):
    def test_compare_command_writes_artifacts(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "branch.json").write_text(json.dumps(_report([_case("a", passed=False)])))
            (root / "develop.json").write_text(json.dumps(_report([_case("a", passed=True)])))
            exit_code = cli.main(
                [
                    "compare",
                    "--branch-report",
                    str(root / "branch.json"),
                    "--develop-report",
                    str(root / "develop.json"),
                    "--configuration",
                    "text",
                    "--capabilities",
                    str(_CAPABILITIES_FILE),
                    "--output",
                    str(root / "comparison.json"),
                ]
            )
            self.assertEqual(exit_code, 0)
            written = json.loads((root / "comparison.json").read_text())
            self.assertEqual(written["artifacts"][0]["status"], "new")

    def test_compare_fail_on_regression_fails_for_vanished_passing_case(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "branch.json").write_text(json.dumps(_report([])))
            (root / "develop.json").write_text(json.dumps(_report([_case("a", passed=True)])))
            (root / "capabilities.json").write_text(json.dumps(CAPABILITIES))
            arguments = [
                "compare",
                "--branch-report",
                str(root / "branch.json"),
                "--develop-report",
                str(root / "develop.json"),
                "--configuration",
                "text",
                "--capabilities",
                str(root / "capabilities.json"),
                "--output",
                str(root / "comparison.json"),
            ]
            self.assertEqual(cli.main(arguments + ["--fail-on-regression"]), 1)
            self.assertEqual(json.loads((root / "comparison.json").read_text())["artifacts"][0]["status"], "vanished")

    def test_compare_fail_on_regression_fails_for_missing_baseline_failure(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "branch.json").write_text(json.dumps(_report([])))
            (root / "develop.json").write_text(json.dumps(_report([_case("a", passed=False)])))
            arguments = [
                "compare",
                "--branch-report",
                str(root / "branch.json"),
                "--develop-report",
                str(root / "develop.json"),
                "--configuration",
                "text",
                "--capabilities",
                str(_CAPABILITIES_FILE),
                "--output",
                str(root / "comparison.json"),
            ]
            self.assertEqual(cli.main(arguments), 0)
            self.assertEqual(cli.main(arguments + ["--fail-on-regression"]), 1)

    def test_propose_command_emits_ledger_payload_and_provisional_record(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            findings = [_finding("a", "destination")]
            branch = report.load_report(_report([_case("a", passed=False)], findings=findings))
            develop = report.load_report(_report([_case("a", passed=False)], findings=findings))
            artifact = comparator.compare_reports(branch, develop, configuration="text", capabilities=CAPABILITIES)[0]
            (root / "comparison.json").write_text(comparator.serialize_many([artifact]))
            exit_code = cli.main(
                [
                    "propose",
                    "--comparison",
                    str(root / "comparison.json"),
                    "--case-id",
                    "a",
                    "--dimension",
                    "destination",
                    "--issue-url",
                    "https://x/1",
                    "--closure-packet-url",
                    "https://x/2",
                    "--permanent-test-path",
                    "modules/foundry_script/tests/scripts/a.fs",
                    "--capability-path",
                    "modules/foundry_script/fs_analyzer.cpp",
                    "--workstream-owner",
                    "owner",
                    "--detection-artifact",
                    "https://ci/artifact",
                    "--detected-at",
                    "2026-08-17T10:00:00-04:00",
                    "--origin",
                    "manual",
                    "--capabilities",
                    str(_CAPABILITIES_FILE),
                    "--output-dir",
                    str(root / "out"),
                ]
            )
            self.assertEqual(exit_code, 0)
            record = provisional.ProvisionalRecord.from_dict(
                json.loads((root / "out" / "provisional.json").read_text()), capabilities=CAPABILITIES
            )
            self.assertEqual(record.due_at, _ny(2026, 8, 19, 17))
            self.assertEqual(record.origin, "manual")
            self.assertEqual(record.finding_id, _runner_finding_id("a", "destination"))
            ledger_file = root / "out" / (record.finding_id + ".json")
            self.assertEqual(ledger.read_record(ledger_file), record.payload)

    def _comparison_with_slice(self, root: Path) -> None:
        findings = [_finding("a", "destination")]
        branch = report.load_report(_report([_case("a", passed=False)], findings=findings))
        artifact = comparator.compare_reports(branch, branch, configuration="text", capabilities=CAPABILITIES)[0]
        (root / "comparison.json").write_text(comparator.serialize_many([artifact]))

    def _propose_arguments(self, root: Path) -> list[str]:
        return [
            "propose",
            "--comparison",
            str(root / "comparison.json"),
            "--case-id",
            "a",
            "--dimension",
            "destination",
            "--issue-url",
            "https://x/1",
            "--closure-packet-url",
            "https://x/2",
            "--permanent-test-path",
            "p",
            "--workstream-owner",
            "o",
            "--detection-artifact",
            "d",
            "--detected-at",
            "2026-08-17T10:00:00-04:00",
            "--capabilities",
            str(_CAPABILITIES_FILE),
            "--output-dir",
            str(root / "out"),
        ]

    def test_propose_defaults_capability_slice_to_the_artifact(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            self._comparison_with_slice(root)
            self.assertEqual(cli.main(self._propose_arguments(root)), 0)
            record = provisional.ProvisionalRecord.from_dict(
                json.loads((root / "out" / "provisional.json").read_text()), capabilities=CAPABILITIES
            )
            self.assertEqual(
                record.capability_slice,
                ("modules/foundry_script/fs_analyzer.cpp", "modules/foundry_script/fs_vm.cpp"),
            )

    def test_propose_rejects_capability_path_outside_the_artifact_slice(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            self._comparison_with_slice(root)
            arguments = self._propose_arguments(root) + ["--capability-path", "editor/editor_node.cpp"]
            self.assertEqual(cli.main(arguments), 2)
            self.assertFalse((root / "out").exists())
            narrowed = self._propose_arguments(root) + ["--capability-path", "modules/foundry_script/fs_vm.cpp"]
            self.assertEqual(cli.main(narrowed), 0)
            record = provisional.ProvisionalRecord.from_dict(
                json.loads((root / "out" / "provisional.json").read_text()), capabilities=CAPABILITIES
            )
            self.assertEqual(record.capability_slice, ("modules/foundry_script/fs_vm.cpp",))

    def test_propose_without_slice_on_artifact_requires_explicit_paths(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            findings = [_finding("a", "destination")]
            branch = report.load_report(_report([_case("a", passed=False)], findings=findings))
            artifact = comparator.compare_reports(branch, branch, configuration="text")[0]
            (root / "comparison.json").write_text(comparator.serialize_many([artifact]))
            self.assertEqual(cli.main(self._propose_arguments(root)), 2)
            self.assertFalse((root / "out").exists())

    def test_propose_keeps_the_runner_echoed_classification_and_ledger_fields(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            known = dict(
                _finding("a", "destination"),
                classification="product_defect",
                issue_url="https://github.com/x/issues/40",
                closure_packet_url="https://github.com/x/pull/41",
                permanent_test_paths=["modules/foundry_script/tests/scripts/known.fs"],
            )
            branch = report.load_report(_report([_case("a", passed=False)], findings=[known]))
            artifact = comparator.compare_reports(branch, branch, configuration="text", capabilities=CAPABILITIES)[0]
            (root / "comparison.json").write_text(comparator.serialize_many([artifact]))
            arguments = [
                "propose",
                "--comparison",
                str(root / "comparison.json"),
                "--case-id",
                "a",
                "--dimension",
                "destination",
                "--workstream-owner",
                "o",
                "--detection-artifact",
                "d",
                "--detected-at",
                "2026-08-17T10:00:00-04:00",
                "--capabilities",
                str(_CAPABILITIES_FILE),
                "--output-dir",
                str(root / "out"),
            ]
            self.assertEqual(cli.main(arguments), 0)
            record = provisional.ProvisionalRecord.from_dict(
                json.loads((root / "out" / "provisional.json").read_text()), capabilities=CAPABILITIES
            )
            self.assertEqual(record.payload["classification"], "product_defect")
            self.assertEqual(record.payload["issue_url"], "https://github.com/x/issues/40")
            self.assertEqual(record.payload["closure_packet_url"], "https://github.com/x/pull/41")
            self.assertEqual(record.payload["permanent_test_paths"], ["modules/foundry_script/tests/scripts/known.fs"])

    def test_propose_requires_ledger_fields_for_a_genuinely_new_finding(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            self._comparison_with_slice(root)
            arguments = [
                "propose",
                "--comparison",
                str(root / "comparison.json"),
                "--case-id",
                "a",
                "--dimension",
                "destination",
                "--workstream-owner",
                "o",
                "--detection-artifact",
                "d",
                "--capabilities",
                str(_CAPABILITIES_FILE),
                "--output-dir",
                str(root / "out"),
            ]
            self.assertEqual(cli.main(arguments), 2)
            self.assertFalse((root / "out").exists())

    def _split_comparison(self, root: Path) -> str:
        """A historical ledger entry for case 'old' that migrations split into 'new_a' and 'new_b'.

        The runner echoes the historical finding onto both children: same finding_id, migrated_from='old',
        resolved_case_ids=['new_a', 'new_b'] (fs_type_completeness_runner.cpp:785-802, 1566-1575).
        """
        historical_id = _runner_finding_id("old", "destination")
        echoed = {
            "classification": "product_defect",
            "issue_url": "https://github.com/x/issues/40",
            "closure_packet_url": "https://github.com/x/pull/41",
            "permanent_test_paths": ["modules/foundry_script/tests/scripts/known.fs"],
            "migrated_from": "old",
            "resolved_case_ids": ["new_a", "new_b"],
        }
        findings = [
            dict(_finding("new_a", "destination"), finding_id=historical_id, **echoed),
            dict(_finding("new_b", "destination"), finding_id=historical_id, **echoed),
        ]
        cases = [_case("new_a", passed=False), _case("new_b", passed=False)]
        branch = report.load_report(_report(cases, findings=findings))
        artifacts = comparator.compare_reports(branch, branch, configuration="text", capabilities=CAPABILITIES)
        (root / "comparison.json").write_text(comparator.serialize_many(artifacts))
        return historical_id

    def _split_arguments(self, root: Path, *case_ids: str) -> list[str]:
        arguments = ["propose", "--comparison", str(root / "comparison.json")]
        for case_id in case_ids:
            arguments += ["--case-id", case_id]
        return arguments + [
            "--dimension",
            "destination",
            "--workstream-owner",
            "o",
            "--detection-artifact",
            "d",
            "--detected-at",
            "2026-08-17T10:00:00-04:00",
            "--capabilities",
            str(_CAPABILITIES_FILE),
            "--output-dir",
            str(root / "out"),
        ]

    def test_proposing_one_child_of_a_split_keeps_the_historical_entry_intact(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            historical_id = self._split_comparison(root)
            self.assertEqual(cli.main(self._split_arguments(root, "new_a")), 0)
            written = sorted(path.name for path in (root / "out").glob("fstcf-*.json"))
            self.assertEqual(written, [f"{historical_id}.json"])
            historical = ledger.read_record(root / "out" / f"{historical_id}.json")
            self.assertEqual(historical["case_id"], "old")
            self.assertEqual(historical["classification"], "product_defect")
            record = provisional.ProvisionalRecord.from_dict(
                json.loads((root / "out" / "provisional.json").read_text()), capabilities=CAPABILITIES
            )
            self.assertEqual(record.finding_id, historical_id)
            self.assertEqual(record.migrated_from, "old")
            self.assertEqual(record.resolved_case_ids, ("new_a", "new_b"))
            self.assertEqual(record.proposed_case_ids, ("new_a",))

    def test_proposing_every_child_of_a_split_writes_one_record_per_child(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            historical_id = self._split_comparison(root)
            self.assertEqual(cli.main(self._split_arguments(root, "new_a", "new_b")), 0)
            child_ids = [_runner_finding_id("new_a", "destination"), _runner_finding_id("new_b", "destination")]
            written = sorted(path.name for path in (root / "out").glob("fstcf-*.json"))
            self.assertEqual(written, sorted(f"{child_id}.json" for child_id in child_ids))
            self.assertFalse((root / "out" / f"{historical_id}.json").exists())
            for child_id, case_id in zip(child_ids, ("new_a", "new_b")):
                child = ledger.read_record(root / "out" / f"{child_id}.json")
                self.assertEqual(child["case_id"], case_id)
                self.assertEqual(child["classification"], "product_defect")
                record = provisional.ProvisionalRecord.from_dict(
                    json.loads((root / "out" / f"provisional_{child_id}.json").read_text()), capabilities=CAPABILITIES
                )
                self.assertEqual(record.migrated_from, "old")
                self.assertEqual(record.proposed_case_ids, ("new_a", "new_b"))
            self.assertFalse((root / "out" / "provisional.json").exists())

    def test_runner_finding_id_mirror_matches_the_runner_fixture(self) -> None:
        runner_id = json.loads(RUNNER_REPORT_TEXT)["findings"][0]["finding_id"]
        self.assertEqual(ledger.runner_finding_id("case_union_store_variable", "destination"), runner_id)

    def test_propose_command_refuses_a_dimension_the_report_did_not_find(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            findings = [_finding("a", "destination")]
            branch = report.load_report(_report([_case("a", passed=False)], findings=findings))
            artifact = comparator.compare_reports(branch, branch, configuration="text")[0]
            (root / "comparison.json").write_text(comparator.serialize_many([artifact]))
            exit_code = cli.main(
                [
                    "propose",
                    "--comparison",
                    str(root / "comparison.json"),
                    "--case-id",
                    "a",
                    "--dimension",
                    "source_proof",
                    "--issue-url",
                    "https://x/1",
                    "--closure-packet-url",
                    "https://x/2",
                    "--permanent-test-path",
                    "p",
                    "--capability-path",
                    "c.cpp",
                    "--workstream-owner",
                    "o",
                    "--detection-artifact",
                    "d",
                    "--capabilities",
                    str(_CAPABILITIES_FILE),
                    "--output-dir",
                    str(root / "out"),
                ]
            )
            self.assertEqual(exit_code, 2)
            self.assertFalse((root / "out").exists())

    def test_reconcile_command_reports_state_and_exit_code(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            payload = ledger.proposed_record(
                finding_id="fstcf-v1-eeeeeeeeeeeeeeeeeeee",
                family="f",
                case_id="c",
                dimension="d",
                issue_url="https://x/1",
                closure_packet_url="https://x/2",
                permanent_test_paths=["p"],
            )
            record = provisional.ProvisionalRecord.create(
                finding_id=payload["finding_id"],
                payload=payload,
                capability_slice=["modules/foundry_script/fs_vm.cpp"],
                workstream_owner="o",
                detection_artifact="a",
                develop_comparison=_artifact_with_status(comparator.Status.UNCHANGED).to_dict(),
                capabilities=CAPABILITIES,
                detected_at=_ny(2026, 8, 17, 10),
                bot_pr_url="https://x/2",
                origin="automation",
            )
            (root / "provisional.json").write_text(json.dumps(record.to_dict()))
            ledger_dir = root / "ledger"
            ledger_dir.mkdir()
            exit_code = cli.main(
                [
                    "reconcile",
                    "--provisional",
                    str(root / "provisional.json"),
                    "--capabilities",
                    str(_CAPABILITIES_FILE),
                    "--ledger-dir",
                    str(ledger_dir),
                    "--pull-request-state",
                    "open",
                    "--now",
                    "2026-08-25T09:00:00-04:00",
                    "--output",
                    str(root / "reconciliation.json"),
                ]
            )
            self.assertEqual(exit_code, 1)
            written = json.loads((root / "reconciliation.json").read_text())
            self.assertEqual(written["state"], "overdue")
            self.assertEqual(written["blocked_capability_slice"], ["modules/foundry_script/fs_vm.cpp"])

    def test_reconcile_command_without_pull_request_state_keeps_classified_ledger_blocking(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            payload = ledger.proposed_record(
                finding_id="fstcf-v1-dddddddddddddddddddd",
                family="f",
                case_id="c",
                dimension="d",
                issue_url="https://x/1",
                closure_packet_url="https://x/2",
                permanent_test_paths=["p"],
            )
            record = provisional.ProvisionalRecord.create(
                finding_id=payload["finding_id"],
                payload=payload,
                capability_slice=["modules/foundry_script/fs_vm.cpp"],
                workstream_owner="o",
                detection_artifact="a",
                develop_comparison=_artifact_with_status(comparator.Status.UNCHANGED).to_dict(),
                capabilities=CAPABILITIES,
                detected_at=_ny(2026, 8, 17, 10),
                bot_pr_url="https://x/2",
                origin="automation",
            )
            (root / "provisional.json").write_text(json.dumps(record.to_dict()))
            ledger_dir = root / "ledger"
            ledger.write_record(ledger_dir, dict(payload, classification="product_defect"))
            arguments = [
                "reconcile",
                "--provisional",
                str(root / "provisional.json"),
                "--capabilities",
                str(_CAPABILITIES_FILE),
                "--ledger-dir",
                str(ledger_dir),
                "--now",
                "2026-08-18T09:00:00-04:00",
                "--output",
                str(root / "reconciliation.json"),
            ]
            cli.main(arguments)
            written = json.loads((root / "reconciliation.json").read_text())
            self.assertEqual(written["state"], "pending_merge")
            self.assertTrue(written["blocks_release"])
            self.assertEqual(cli.main(arguments + ["--pull-request-state", "merged"]), 0)
            self.assertEqual(json.loads((root / "reconciliation.json").read_text())["state"], "merged")
            self.assertFalse(json.loads((root / "reconciliation.json").read_text())["blocks_release"])


if __name__ == "__main__":
    unittest.main()
