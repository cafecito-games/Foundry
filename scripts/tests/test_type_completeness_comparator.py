#!/usr/bin/env python3
"""Unit tests for scripts/type_completeness.

Run with: python3 -m unittest discover -s scripts/tests -p "test_type_completeness_*.py"
"""

from __future__ import annotations

import importlib.util
import json
import sys
import tempfile
import unittest
from datetime import datetime, timezone
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


def _report(cases: list[dict[str, Any]], family: str = "union_destination_membership") -> dict[str, Any]:
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
        "findings": [],
        "cases": cases,
    }


def _ny(year: int, month: int, day: int, hour: int, minute: int = 0, second: int = 0) -> datetime:
    return datetime(year, month, day, hour, minute, second, tzinfo=NY)


class ReportLoadingTests(unittest.TestCase):
    def test_loads_failed_cases_with_observations(self) -> None:
        loaded = report.load_report(_report([_case("a", passed=True), _case("b", passed=False)]))
        self.assertEqual(loaded.family, "union_destination_membership")
        self.assertEqual([case.case_id for case in loaded.failed_cases()], ["b"])
        observation = loaded.case("b").observation
        self.assertEqual(observation["diagnostics"], ["Cannot assign"])
        self.assertEqual(observation["actual"], {"outcome": "reject"})

    def test_rejects_unknown_schema_version(self) -> None:
        bad = _report([_case()])
        bad["schema_version"] = 2
        with self.assertRaises(report.ReportError):
            report.load_report(bad)

    def test_default_category_is_product_finding_until_runner_emits_categories(self) -> None:
        loaded = report.load_report(_report([_case("b", passed=False)]))
        self.assertEqual(loaded.case("b").category, report.Category.PRODUCT_FINDING)

    def test_structured_category_from_runner_is_consumed_when_present(self) -> None:
        loaded = report.load_report(_report([_case("b", passed=False, category="structural_failure")]))
        self.assertEqual(loaded.case("b").category, report.Category.STRUCTURAL_FAILURE)

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

    def test_overdue_boundary_at_1700(self) -> None:
        due = _ny(2026, 8, 19, 17)
        self.assertFalse(deadline.is_overdue(due, now=_ny(2026, 8, 19, 16, 59, 59)))
        self.assertFalse(deadline.is_overdue(due, now=_ny(2026, 8, 19, 17, 0, 0)))
        self.assertTrue(deadline.is_overdue(due, now=_ny(2026, 8, 19, 17, 0, 1)))


class LedgerTests(unittest.TestCase):
    def test_finding_id_is_stable_for_family_and_case(self) -> None:
        first = ledger.finding_id("union_destination_membership", "case-a", "destination")
        second = ledger.finding_id("union_destination_membership", "case-a", "destination")
        self.assertEqual(first, second)
        self.assertNotEqual(first, ledger.finding_id("union_destination_membership", "case-b", "destination"))
        self.assertRegex(first, r"^[a-z0-9_]+-[0-9a-f]{16}$")

    def test_finding_id_distinguishes_dimensions_of_the_same_case(self) -> None:
        self.assertNotEqual(
            ledger.finding_id("f", "case-a", "destination"),
            ledger.finding_id("f", "case-a", "source_proof"),
        )

    def test_proposed_record_matches_runner_ledger_schema(self) -> None:
        record = ledger.proposed_record(
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

    def test_record_digest_is_canonical(self) -> None:
        record = {"b": 1, "a": [1, 2]}
        self.assertEqual(ledger.record_digest(record), ledger.record_digest({"a": [1, 2], "b": 1}))

    def test_write_and_read_ledger_file_round_trips(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            record = ledger.proposed_record(
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
            "develop_comparison": {"status": "unchanged"},
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
        parsed = provisional.parse_issue_body(body)
        self.assertEqual(parsed, record)

    def test_parse_rejects_body_without_record(self) -> None:
        with self.assertRaises(provisional.ProvisionalError):
            provisional.parse_issue_body("no machine-readable block here")

    def test_manual_record_carries_manual_origin_with_same_fields(self) -> None:
        manual = self._record(origin="manual")
        automatic = self._record()
        self.assertEqual(manual.due_at, automatic.due_at)
        self.assertEqual(set(manual.to_dict()), set(automatic.to_dict()))
        self.assertEqual(manual.origin, "manual")


class ReconciliationTests(unittest.TestCase):
    def _provisional(self, **overrides: Any) -> Any:
        payload = ledger.proposed_record(
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
            "develop_comparison": {"status": "unchanged"},
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

    def test_manual_records_reconcile_identically(self) -> None:
        manual = self._provisional(origin="manual")
        result = self._reconcile(manual, None, "open", _ny(2026, 8, 19, 17, 0, 1))
        self.assertEqual(result.state, reconcile.State.OVERDUE)

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
        self.assertFalse(reconcile.blocks_release([pending]))
        self.assertFalse(reconcile.blocks_path([pending, overdue], "modules/foundry_script/fs_analyzer.cpp"))
        self.assertTrue(reconcile.blocks_path([pending, overdue], "modules/foundry_script/fs_vm.cpp"))


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


class GitHubAutomationTests(unittest.TestCase):
    def test_refuses_to_push_to_protected_branch(self) -> None:
        runner = FakeCommandRunner()
        client = github.AutomationClient(run=runner, repository="cafecito-games/Foundry")
        for branch in ("develop", "main", "master"):
            with self.assertRaises(github.ProtectedBranchError):
                client.push_ledger_branch(branch, Path("/nowhere"))
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

    def test_existing_pr_is_updated_not_duplicated(self) -> None:
        runner = FakeCommandRunner({"pr list": json.dumps([{"url": "https://github.com/x/pull/3", "number": 3}])})
        client = github.AutomationClient(run=runner, repository="cafecito-games/Foundry")
        url = client.open_or_update_ledger_pull_request(
            "bot/type-completeness/abc", title="t", body="b", base="develop"
        )
        self.assertEqual(url, "https://github.com/x/pull/3")
        joined = [" ".join(call) for call in runner.calls]
        self.assertTrue(any("pr edit 3" in call for call in joined))
        self.assertFalse(any("pr create" in call for call in joined))

    def test_tracking_issue_is_created_once_and_then_updated(self) -> None:
        runner = FakeCommandRunner({"issue list": "[]", "issue create": "https://github.com/x/issues/5"})
        client = github.AutomationClient(run=runner, repository="cafecito-games/Foundry")
        url = client.create_or_update_tracking_issue("finding-1", title="t", body="b")
        self.assertEqual(url, "https://github.com/x/issues/5")
        runner = FakeCommandRunner({"issue list": json.dumps([{"url": "https://github.com/x/issues/5", "number": 5}])})
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
                    "--output",
                    str(root / "comparison.json"),
                ]
            )
            self.assertEqual(exit_code, 0)
            written = json.loads((root / "comparison.json").read_text())
            self.assertEqual(written["artifacts"][0]["status"], "new")

    def test_propose_command_emits_ledger_payload_and_provisional_record(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            branch = report.load_report(_report([_case("a", passed=False)]))
            develop = report.load_report(_report([_case("a", passed=False)]))
            artifact = comparator.compare_reports(branch, develop, configuration="text")[0]
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
                    "--output-dir",
                    str(root / "out"),
                ]
            )
            self.assertEqual(exit_code, 0)
            record = provisional.ProvisionalRecord.from_dict(
                json.loads((root / "out" / "provisional.json").read_text())
            )
            self.assertEqual(record.due_at, _ny(2026, 8, 19, 17))
            self.assertEqual(record.origin, "manual")
            ledger_file = root / "out" / (record.finding_id + ".json")
            self.assertEqual(ledger.read_record(ledger_file), record.payload)

    def test_reconcile_command_reports_state_and_exit_code(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            payload = ledger.proposed_record(
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
                capability_slice=["x.cpp"],
                workstream_owner="o",
                detection_artifact="a",
                develop_comparison={"status": "unchanged"},
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
            self.assertEqual(written["blocked_capability_slice"], ["x.cpp"])


if __name__ == "__main__":
    unittest.main()
