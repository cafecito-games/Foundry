"""Behavior of the capability-scoped type-completeness presubmit gate.

Every producer-owned document these tests consume was captured from the real binary:
`scripts/tests/fixtures/type_completeness/selection_*.json` are the stdout of
`foundry --headless test completeness select --json`, and `runner_report_*.json` is the report
`foundry --headless test completeness run` published. Reports that must carry a failure are derived
from that captured report by marking a case failed, using the runner's own finding-ID formula; nothing
is typed out by hand for a producer that exists.
"""

from __future__ import annotations

import copy
import json
import os
import shutil
import subprocess
import sys
import tempfile
import unittest
from datetime import datetime, timezone
from pathlib import Path
from typing import Any, Optional, Sequence

REPOSITORY_ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPOSITORY_ROOT))

from scripts.type_completeness import (  # noqa: E402
    comparator,
    ledger,
    presubmit,  # noqa: E402
    report,
)

FIXTURES = Path(__file__).resolve().parent / "fixtures" / "type_completeness"
FAKE_BINARY = Path(__file__).resolve().parent / "type_completeness_fake_binary.py"
FAMILY = "union_destination_membership"
# A real catalog family the temporary repository's baseline branch deliberately lacks, so a test can
# introduce it on the branch the way a family-adding pull request does.
NEW_FAMILY = "lifecycle_reload"
CATALOG = REPOSITORY_ROOT / "modules" / "foundry_script" / "tests" / "type_completeness"


def read_fixture(name: str) -> str:
    return (FIXTURES / name).read_text(encoding="utf-8")


def runner_report() -> dict[str, Any]:
    document = json.loads(read_fixture(f"runner_report_{FAMILY}.json"))
    assert isinstance(document, dict)
    return document


def failed_report(dimension: str = "destination", classification: str = "unclassified") -> dict[str, Any]:
    """The captured report with its first case marked failed and one finding filed against it."""
    document = copy.deepcopy(runner_report())
    document["success"] = False
    document["outcome"] = "product_mismatch"
    case = document["cases"][0]
    case["passed"] = False
    case["status"] = "failed"
    finding = {
        "finding_id": ledger.runner_finding_id(case["case_id"], dimension),
        "case_id": case["case_id"],
        "family": document["family"],
        "dimension": dimension,
        "expected": case["expected"],
        "actual": case["actual"],
        "classification": classification,
    }
    document["findings"] = [finding]
    return document


# `Error::ERR_TIMEOUT`, the code the runner stamps on a `run_timeout` structural failure; it reads
# back as a float because the runner writes every number as one.
RUNNER_ERR_TIMEOUT = 24.0


def timed_out_report() -> dict[str, Any]:
    """The captured report as the runner republishes it after crossing its budget: evidence kept, verdict replaced."""
    document = copy.deepcopy(runner_report())
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
    return document


def renamed_report(document: dict[str, Any], family: str) -> dict[str, Any]:
    """The captured report republished under another family name, findings included."""
    document = copy.deepcopy(document)
    document["family"] = family
    for finding in document["findings"]:
        finding["family"] = family
    return document


class VerdictTableTests(unittest.TestCase):
    def test_every_verdict_has_exactly_one_exit_code(self) -> None:
        self.assertEqual(set(presubmit.EXIT_CODES), set(presubmit.Verdict))
        for verdict in presubmit.Verdict:
            self.assertIsInstance(presubmit.exit_code_for(verdict), int)

    def test_exit_codes_match_the_specified_contract(self) -> None:
        self.assertEqual(
            {verdict.value: code for verdict, code in presubmit.EXIT_CODES.items()},
            {
                "passed": 0,
                "nothing_selected": 0,
                "blocked": 1,
                "baseline_missing": 1,
                "baseline_invalid": 1,
                "structural_failure": 2,
                "baseline_malformed": 2,
                "malformed_input": 2,
                "timeout": 3,
                "selector_validation_failed": 4,
            },
        )

    def test_only_the_passing_verdicts_are_green(self) -> None:
        green = {verdict for verdict, code in presubmit.EXIT_CODES.items() if code == 0}
        self.assertEqual(green, {presubmit.Verdict.PASSED, presubmit.Verdict.NOTHING_SELECTED})

    def test_precedence_orders_every_verdict_once(self) -> None:
        self.assertEqual(len(presubmit.VERDICT_PRECEDENCE), len(set(presubmit.VERDICT_PRECEDENCE)))
        self.assertEqual(set(presubmit.VERDICT_PRECEDENCE), set(presubmit.Verdict))

    def test_the_worst_recorded_verdict_wins(self) -> None:
        for worse, better in zip(presubmit.VERDICT_PRECEDENCE, presubmit.VERDICT_PRECEDENCE[1:]):
            gate = presubmit.Gate.__new__(presubmit.Gate)
            gate.verdicts = [better, worse]
            gate.reasons = []
            self.assertIs(gate.worst_verdict(), worse)


class BaselineStateTableTests(unittest.TestCase):
    def test_every_per_family_state_has_exactly_one_verdict_entry(self) -> None:
        per_family = set(presubmit.BaselineState) - {presubmit.BaselineState.NOT_CONSULTED}
        self.assertEqual(set(presubmit.BASELINE_VERDICTS), per_family)
        self.assertEqual(set(presubmit.BASELINE_PRECEDENCE), per_family)
        self.assertEqual(len(presubmit.BASELINE_PRECEDENCE), len(set(presubmit.BASELINE_PRECEDENCE)))

    def test_every_state_is_handled_or_refused(self) -> None:
        for state in presubmit.BaselineState:
            with self.subTest(state=state.value):
                if state is presubmit.BaselineState.NOT_CONSULTED:
                    with self.assertRaises(presubmit.PresubmitError):
                        presubmit.verdict_for_baseline(state)
                    continue
                verdict = presubmit.verdict_for_baseline(state)
                self.assertTrue(verdict is None or isinstance(verdict, presubmit.Verdict))

    def test_each_state_maps_to_its_own_verdict(self) -> None:
        expected = {
            presubmit.BaselineState.PRESENT: None,
            presubmit.BaselineState.NEW_FAMILY: None,
            presubmit.BaselineState.MISSING: presubmit.Verdict.BASELINE_MISSING,
            presubmit.BaselineState.INVALID: presubmit.Verdict.BASELINE_INVALID,
            presubmit.BaselineState.MALFORMED: presubmit.Verdict.BASELINE_MALFORMED,
        }
        for state, verdict in expected.items():
            with self.subTest(state=state.value):
                self.assertIs(presubmit.verdict_for_baseline(state), verdict)

    def test_only_the_states_with_verdicts_to_compare_proceed(self) -> None:
        # A missing baseline still compares (against the absent develop side, so every failure is
        # new); an invalid or malformed one carries no develop verdict and refuses the family.
        self.assertEqual(
            presubmit.COMPARABLE_BASELINE_STATES,
            {presubmit.BaselineState.PRESENT, presubmit.BaselineState.NEW_FAMILY, presubmit.BaselineState.MISSING},
        )
        self.assertTrue(presubmit.COMPARABLE_BASELINE_STATES <= set(presubmit.BASELINE_VERDICTS))


class StatusDispositionTests(unittest.TestCase):
    def test_every_comparator_status_has_a_disposition(self) -> None:
        self.assertEqual(set(presubmit.STATUS_DISPOSITION), set(comparator.Status))
        for status in comparator.Status:
            self.assertIsInstance(presubmit.disposition_of(status), presubmit.Disposition)

    def test_the_blocking_statuses_are_exactly_the_regression_statuses(self) -> None:
        blocking = {
            status
            for status, disposition in presubmit.STATUS_DISPOSITION.items()
            if disposition is presubmit.Disposition.BLOCKING
        }
        self.assertEqual(blocking, set(comparator.REGRESSION_STATUSES))

    def test_the_known_status_is_exactly_the_known_baseline_status(self) -> None:
        known = {
            status
            for status, disposition in presubmit.STATUS_DISPOSITION.items()
            if disposition is presubmit.Disposition.KNOWN
        }
        self.assertEqual(known, set(comparator.KNOWN_BASELINE_STATUSES))

    def test_the_ignored_statuses_conclude_nothing_against_the_branch(self) -> None:
        # A status is ignored only because the branch has nothing to answer for: it no longer fails, or
        # neither side reached a verdict to compare. Pinning the set to those partitions keeps a status
        # from being made harmless one entry at a time.
        ignored = {
            status
            for status, disposition in presubmit.STATUS_DISPOSITION.items()
            if disposition is presubmit.Disposition.IGNORED
        }
        self.assertEqual(ignored, set(comparator.NO_LONGER_FAILING_STATUSES) | set(comparator.NOT_COMPARABLE_STATUSES))

    def test_an_unlisted_status_is_refused_rather_than_ignored(self) -> None:
        with self.assertRaises(presubmit.PresubmitError):
            presubmit.disposition_of("not_a_status")  # type: ignore[arg-type]


class SelectionParsingTests(unittest.TestCase):
    def test_a_mapped_change_set_selects_its_families(self) -> None:
        selection = presubmit.parse_selection(read_fixture("selection_mapped.json"))
        self.assertIn(FAMILY, selection.families)
        self.assertFalse(selection.used_broad_core_fallback)
        self.assertEqual(selection.validation_errors, ())

    def test_an_unmapped_production_path_reports_the_fallback_and_an_error(self) -> None:
        selection = presubmit.parse_selection(read_fixture("selection_fallback.json"))
        self.assertTrue(selection.used_broad_core_fallback)
        self.assertIn(FAMILY, selection.families)
        self.assertTrue(selection.validation_errors)

    def test_an_invalid_path_is_a_validation_error(self) -> None:
        selection = presubmit.parse_selection(read_fixture("selection_invalid.json"))
        self.assertTrue(selection.validation_errors)

    def test_a_nonproduction_change_set_selects_nothing(self) -> None:
        selection = presubmit.parse_selection(read_fixture("selection_none.json"))
        self.assertEqual(selection.families, ())
        self.assertEqual(selection.validation_errors, ())

    # The selection CLI writes JSON integers because it builds its document from int64_t members, while
    # the runner writes every number as a float. Both are accepted as a schema version; the difference is
    # a property of two different producers and neither may be "fixed" by normalizing the other.
    def test_the_selection_schema_version_is_a_json_integer(self) -> None:
        raw = json.loads(read_fixture("selection_mapped.json"))
        self.assertIsInstance(raw["schema_version"], int)
        self.assertNotIsInstance(raw["schema_version"], float)
        self.assertIsInstance(runner_report()["schema_version"], float)

    def test_engine_noise_before_the_document_is_tolerated(self) -> None:
        selection = presubmit.parse_selection("Vulkan is unavailable\n" + read_fixture("selection_mapped.json"))
        self.assertIn(FAMILY, selection.families)

    def test_trailing_text_after_the_document_is_refused(self) -> None:
        with self.assertRaises(presubmit.PresubmitError):
            presubmit.parse_selection(read_fixture("selection_mapped.json") + "{}")

    def test_a_stream_without_a_document_is_refused(self) -> None:
        with self.assertRaises(presubmit.PresubmitError):
            presubmit.parse_selection("no document here")

    def test_a_missing_member_is_named(self) -> None:
        for member in ("families", "used_broad_core_fallback", "validation_errors"):
            document = json.loads(read_fixture("selection_mapped.json"))
            del document[member]
            with self.assertRaises(presubmit.PresubmitError) as raised:
                presubmit.parse_selection(json.dumps(document))
            self.assertIn(member, str(raised.exception))

    def test_families_must_be_an_array_of_strings(self) -> None:
        document = json.loads(read_fixture("selection_mapped.json"))
        document["families"] = [1]
        with self.assertRaises(presubmit.PresubmitError):
            presubmit.parse_selection(json.dumps(document))
        document["families"] = "union_destination_membership"
        with self.assertRaises(presubmit.PresubmitError):
            presubmit.parse_selection(json.dumps(document))

    def test_an_unsupported_schema_version_is_refused(self) -> None:
        document = json.loads(read_fixture("selection_mapped.json"))
        document["schema_version"] = 2
        with self.assertRaises(presubmit.PresubmitError):
            presubmit.parse_selection(json.dumps(document))


class AbsentBaselineTests(unittest.TestCase):
    def test_the_absent_baseline_report_loads_and_carries_no_cases(self) -> None:
        loaded = report.load_report(presubmit.absent_baseline_report(FAMILY))
        self.assertEqual(loaded.family, FAMILY)
        self.assertEqual(loaded.cases, ())
        self.assertFalse(loaded.is_structural_failure)

    def test_a_branch_failure_against_an_absent_baseline_is_new(self) -> None:
        branch = report.load_report(failed_report())
        develop = report.load_report(presubmit.absent_baseline_report(FAMILY))
        artifacts = comparator.compare_reports(branch, develop, "text")
        self.assertTrue(artifacts)
        for artifact in artifacts:
            self.assertIs(artifact.status, comparator.Status.NEW)


class ChangedPathTests(unittest.TestCase):
    """The change set the selector is given must contain every path a change touched."""

    def setUp(self) -> None:
        self.repository = Path(tempfile.mkdtemp(prefix="type_completeness_repository_"))
        self.addCleanup(shutil.rmtree, str(self.repository), True)
        self._git("init", "--initial-branch", "main")
        self._git("config", "user.email", "gate@example.invalid")
        self._git("config", "user.name", "Gate")

    def _git(self, *arguments: str) -> str:
        completed = subprocess.run(
            ["git", "-C", str(self.repository), *arguments],
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            universal_newlines=True,
        )
        self.assertEqual(0, completed.returncode, completed.stdout)
        return completed.stdout

    def _write(self, relative: str, text: str) -> None:
        path = self.repository / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text, encoding="utf-8")

    def test_a_move_reports_both_of_its_endpoints(self) -> None:
        # With rename detection a move reports only its destination, so moving a mapped production
        # file under a nonproduction prefix would hide the family the move can break.
        self._write("modules/foundry_script/fs_analyzer.cpp", "int analyzer() { return 0; }\n")
        self._git("add", "-A")
        self._git("commit", "-m", "seed")
        self._git("branch", "baseline")
        (self.repository / "modules" / "foundry_script" / "tests").mkdir(parents=True, exist_ok=True)
        self._git("mv", "modules/foundry_script/fs_analyzer.cpp", "modules/foundry_script/tests/fs_analyzer.cpp")
        self._git("commit", "-m", "move")

        paths, sha = presubmit.git_changed_paths("baseline", self.repository)
        self.assertTrue(sha)
        self.assertEqual(
            sorted(paths),
            ["modules/foundry_script/fs_analyzer.cpp", "modules/foundry_script/tests/fs_analyzer.cpp"],
        )


class GateTestCase(unittest.TestCase):
    """Drives the wrapper end to end against the fake binary, one fail-closed row per test.

    The change set is measured in a temporary repository whose `baseline` branch carries the catalog's
    rule manifest for `FAMILY` alone, so the merge-base catalog a test sees is exactly what it committed.
    """

    def setUp(self) -> None:
        self.work = Path(self._make_temporary_directory())
        self.output = self.work / "out"
        self.reports = self.work / "reports"
        self.reports.mkdir(parents=True)
        self.changed_paths = self.work / "changed.txt"
        self.changed_paths.write_text("modules/foundry_script/fs_analyzer.cpp\n", encoding="utf-8")
        self.environment = dict(os.environ)
        self.repository = self.work / "repository"
        self.catalog = self.repository / "catalog"
        self.repository.mkdir()
        self._git("init", "--initial-branch", "main")
        self._git("config", "user.email", "gate@example.invalid")
        self._git("config", "user.name", "Gate")
        self._commit_rule_manifest(FAMILY, "seed the develop catalog")
        self._git("branch", "baseline")

    def _git(self, *arguments: str) -> str:
        completed = subprocess.run(
            ["git", "-C", str(self.repository), *arguments],
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            universal_newlines=True,
        )
        self.assertEqual(0, completed.returncode, completed.stdout)
        return completed.stdout

    def _commit_rule_manifest(self, family: str, message: str) -> None:
        destination = self.catalog / "rules" / f"{family}.json"
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(CATALOG / "rules" / f"{family}.json", destination)
        self._git("add", "-A")
        self._git("commit", "-q", "-m", message)

    def add_family_on_branch(self, family: str = NEW_FAMILY) -> None:
        """Introduce a family the way a pull request does: its rule manifest is committed after the merge base."""
        self._commit_rule_manifest(family, f"add family {family}")

    def write_selection(self, families: Sequence[str]) -> Path:
        """The captured selection document with its family list replaced; no other member is invented."""
        document = json.loads(read_fixture("selection_mapped.json"))
        document["families"] = list(families)
        destination = self.work / "selection.json"
        destination.write_text(json.dumps(document), encoding="utf-8")
        return destination

    def _make_temporary_directory(self) -> str:
        directory = tempfile.mkdtemp(prefix="type_completeness_presubmit_")
        self.addCleanup(shutil.rmtree, directory, True)
        return directory

    def write_report(self, document: dict[str, Any], family: str = FAMILY, directory: Optional[Path] = None) -> Path:
        destination = (directory or self.reports) / f"{family}.json"
        destination.parent.mkdir(parents=True, exist_ok=True)
        destination.write_text(json.dumps(document, sort_keys=True, indent=2) + "\n", encoding="utf-8")
        return destination

    def run_gate(
        self,
        selection_fixture: str = "selection_mapped.json",
        baseline_dir: Optional[Path] = None,
        environment: Optional[dict[str, str]] = None,
        extra_arguments: Sequence[str] = (),
        output_dir: Optional[Path] = None,
        as_script: bool = False,
    ) -> tuple[int, dict[str, Any], Path]:
        destination = output_dir or self.output
        env = dict(self.environment)
        env["FOUNDRY_TEST_SCRATCH"] = str(self.work / "scratch-root")
        env["FOUNDRY_FAKE_SELECTION"] = str(
            selection_fixture if Path(selection_fixture).is_absolute() else FIXTURES / selection_fixture
        )
        env["FOUNDRY_FAKE_REPORT_DIR"] = str(self.reports)
        env.update(environment or {})
        arguments = [
            "--binary",
            str(FAKE_BINARY),
            "--output-dir",
            str(destination),
            "--repository-root",
            str(self.repository),
            "--catalog",
            str(self.catalog),
            "--merge-base-ref",
            "baseline",
            "--changed-paths",
            str(self.changed_paths),
            "--capabilities",
            str(CATALOG / "capabilities.json"),
            "--budgets",
            str(CATALOG / "budgets.json"),
            *extra_arguments,
        ]
        if baseline_dir is not None:
            arguments += ["--baseline-dir", str(baseline_dir)]
        entry = (
            [sys.executable, str(REPOSITORY_ROOT / "scripts" / "type_completeness" / "presubmit.py")]
            if as_script
            else [sys.executable, "-m", "scripts.type_completeness.presubmit"]
        )
        completed = subprocess.run(
            entry + arguments,
            cwd=str(REPOSITORY_ROOT),
            env=env,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            universal_newlines=True,
        )
        verdict_path = destination / "verdict.json"
        verdict = json.loads(verdict_path.read_text(encoding="utf-8")) if verdict_path.exists() else {}
        return completed.returncode, verdict, destination


class GateTests(GateTestCase):
    def test_a_clean_branch_against_a_matching_baseline_passes(self) -> None:
        self.write_report(runner_report())
        baseline = self.work / "baseline"
        self.write_report(runner_report(), directory=baseline)
        code, verdict, output = self.run_gate(baseline_dir=baseline)
        self.assertEqual(verdict["state"], "passed")
        self.assertEqual(code, 0)
        self.assertEqual(verdict["baseline_state"], "present")
        self.assertEqual(verdict["families_run"], [FAMILY])
        # The artifact carries the evidence behind the verdict, not only the verdict.
        for name in ("selection.json", "comparison.json", "verdict.json", f"report-{FAMILY}.json"):
            self.assertTrue((output / name).exists(), name)

    def test_a_new_failure_blocks(self) -> None:
        self.write_report(failed_report())
        baseline = self.work / "baseline"
        self.write_report(runner_report(), directory=baseline)
        code, verdict, _ = self.run_gate(baseline_dir=baseline, environment={"FOUNDRY_FAKE_RUN_EXIT": "1"})
        self.assertEqual(verdict["state"], "blocked")
        self.assertEqual(code, 1)
        self.assertTrue(verdict["blocking_comparison_ids"])

    def test_a_missing_baseline_makes_every_failure_new(self) -> None:
        self.write_report(failed_report())
        code, verdict, _ = self.run_gate(baseline_dir=self.work / "absent", environment={"FOUNDRY_FAKE_RUN_EXIT": "1"})
        self.assertEqual(verdict["baseline_state"], "missing")
        self.assertEqual(verdict["state"], "blocked")
        self.assertEqual(code, 1)
        self.assertTrue(verdict["blocking_comparison_ids"])

    def test_a_missing_baseline_never_reports_a_clean_run(self) -> None:
        # A vanished case produces no artifact at all when the develop side is absent, so a run with
        # no baseline cannot demonstrate the absence of a regression and must not read as passed.
        self.write_report(runner_report())
        code, verdict, _ = self.run_gate(baseline_dir=self.work / "absent")
        self.assertEqual(verdict["baseline_state"], "missing")
        self.assertEqual(verdict["state"], "baseline_missing")
        self.assertEqual(code, 1)

    def test_a_family_the_branch_introduces_passes_on_a_clean_run(self) -> None:
        # The family cannot have a develop baseline by construction, so its absence is not a refusal.
        self.add_family_on_branch()
        self.write_report(renamed_report(runner_report(), NEW_FAMILY), family=NEW_FAMILY)
        code, verdict, output = self.run_gate(
            selection_fixture=str(self.write_selection([NEW_FAMILY])), baseline_dir=self.work / "absent"
        )
        self.assertEqual(verdict["state"], "passed")
        self.assertEqual(code, 0)
        self.assertEqual(verdict["baseline_state"], "new_family")
        self.assertEqual(verdict["baseline_states"], {NEW_FAMILY: "new_family"})
        self.assertEqual(verdict["families_run"], [NEW_FAMILY])
        self.assertTrue((output / f"comparison-{NEW_FAMILY}.json").exists())

    def test_a_failure_in_a_family_the_branch_introduces_still_blocks(self) -> None:
        self.add_family_on_branch()
        self.write_report(renamed_report(failed_report(), NEW_FAMILY), family=NEW_FAMILY)
        code, verdict, _ = self.run_gate(
            selection_fixture=str(self.write_selection([NEW_FAMILY])),
            baseline_dir=self.work / "absent",
            environment={"FOUNDRY_FAKE_RUN_EXIT": "1"},
        )
        self.assertEqual(verdict["baseline_states"], {NEW_FAMILY: "new_family"})
        self.assertEqual(verdict["state"], "blocked")
        self.assertEqual(code, 1)
        self.assertTrue(verdict["blocking_comparison_ids"])

    def test_a_new_family_is_decided_by_the_merge_base_catalog_not_by_the_artifact(self) -> None:
        # The same absent artifact for a family the develop catalog does define stays a missing
        # baseline: only the merge-base catalog tells the two apart.
        self.write_report(runner_report())
        code, verdict, _ = self.run_gate(baseline_dir=self.work / "absent")
        self.assertEqual(verdict["baseline_states"], {FAMILY: "missing"})
        self.assertEqual(verdict["state"], "baseline_missing")
        self.assertEqual(code, 1)

    def test_a_mixed_selection_reports_each_family_state(self) -> None:
        self.add_family_on_branch()
        self.write_report(runner_report())
        self.write_report(renamed_report(runner_report(), NEW_FAMILY), family=NEW_FAMILY)
        baseline = self.work / "baseline"
        self.write_report(runner_report(), directory=baseline)
        code, verdict, _ = self.run_gate(
            selection_fixture=str(self.write_selection([FAMILY, NEW_FAMILY])), baseline_dir=baseline
        )
        self.assertEqual(verdict["baseline_states"], {FAMILY: "present", NEW_FAMILY: "new_family"})
        self.assertEqual(verdict["baseline_state"], "new_family")
        self.assertEqual(verdict["state"], "passed")
        self.assertEqual(code, 0)
        self.assertEqual(verdict["families_run"], [FAMILY, NEW_FAMILY])

    def test_a_new_family_never_hides_a_missing_baseline_beside_it(self) -> None:
        self.add_family_on_branch()
        self.write_report(runner_report())
        self.write_report(renamed_report(runner_report(), NEW_FAMILY), family=NEW_FAMILY)
        code, verdict, _ = self.run_gate(
            selection_fixture=str(self.write_selection([FAMILY, NEW_FAMILY])), baseline_dir=self.work / "absent"
        )
        self.assertEqual(verdict["baseline_states"], {FAMILY: "missing", NEW_FAMILY: "new_family"})
        self.assertEqual(verdict["baseline_state"], "missing")
        self.assertEqual(verdict["state"], "baseline_missing")
        self.assertEqual(code, 1)

    def test_an_unresolvable_merge_base_is_refused_before_any_family_runs(self) -> None:
        self.write_report(runner_report())
        code, verdict, _ = self.run_gate(extra_arguments=["--merge-base-ref", "no-such-ref"])
        self.assertEqual(verdict["state"], "malformed_input")
        self.assertEqual(code, 2)
        self.assertEqual(verdict["families_run"], [])

    def test_a_catalog_outside_the_repository_cannot_decide_a_new_family(self) -> None:
        # Without the merge-base catalog the gate cannot tell a new family from a missing baseline, so
        # it refuses rather than guessing from the artifact.
        self.write_report(runner_report())
        code, verdict, _ = self.run_gate(baseline_dir=self.work / "absent", extra_arguments=["--catalog", str(CATALOG)])
        self.assertEqual(verdict["state"], "malformed_input")
        self.assertEqual(code, 2)
        self.assertEqual(verdict["baseline_states"], {})

    def test_an_unnamed_scratch_root_is_refused(self) -> None:
        # The runner refuses a report path outside the test scratch space, so a wrapper that invented
        # one would turn every family run into a structural failure.
        self.write_report(runner_report())
        code, verdict, _ = self.run_gate(environment={"FOUNDRY_TEST_SCRATCH": ""})
        self.assertEqual(code, 2)
        self.assertEqual(verdict, {})

    def test_a_malformed_baseline_is_not_a_missing_one(self) -> None:
        self.write_report(runner_report())
        baseline = self.work / "baseline"
        baseline.mkdir()
        (baseline / f"{FAMILY}.json").write_text("{ not json", encoding="utf-8")
        code, verdict, _ = self.run_gate(baseline_dir=baseline)
        self.assertEqual(verdict["state"], "baseline_malformed")
        self.assertEqual(verdict["baseline_state"], "malformed")
        self.assertEqual(code, 2)

    def test_a_structurally_failed_baseline_is_invalid_not_present(self) -> None:
        # The develop run timed out: its per-case verdicts all read passed although the run never
        # finished, so a branch regression compared against it would read as unchanged.
        self.write_report(failed_report())
        baseline = self.work / "baseline"
        self.write_report(timed_out_report(), directory=baseline)
        code, verdict, _ = self.run_gate(baseline_dir=baseline, environment={"FOUNDRY_FAKE_RUN_EXIT": "1"})
        self.assertEqual(verdict["state"], "baseline_invalid")
        self.assertEqual(code, 1)
        self.assertEqual(verdict["baseline_states"], {FAMILY: "invalid"})
        self.assertEqual(verdict["blocking_comparison_ids"], [])
        self.assertTrue(any("run_timeout" in reason for reason in verdict["reasons"]), verdict["reasons"])

    def test_a_structurally_failed_baseline_refuses_even_a_clean_branch(self) -> None:
        self.write_report(runner_report())
        baseline = self.work / "baseline"
        self.write_report(timed_out_report(), directory=baseline)
        code, verdict, _ = self.run_gate(baseline_dir=baseline)
        self.assertEqual(verdict["state"], "baseline_invalid")
        self.assertEqual(verdict["baseline_state"], "invalid")
        self.assertEqual(code, 1)

    def test_the_runner_timeout_code_is_a_timeout(self) -> None:
        self.write_report(runner_report())
        code, verdict, _ = self.run_gate(environment={"FOUNDRY_FAKE_RUN_EXIT": "3"})
        self.assertEqual(verdict["state"], "timeout")
        self.assertEqual(code, 3)

    def test_the_wrapper_deadline_is_a_timeout(self) -> None:
        self.write_report(runner_report())
        code, verdict, _ = self.run_gate(
            environment={"FOUNDRY_FAKE_RUN_SLEEP": "3"},
            extra_arguments=[
                "--budgets",
                str(FIXTURES / "budgets_one_second.json"),
                "--runner-grace-seconds",
                "0",
            ],
        )
        self.assertEqual(verdict["state"], "timeout")
        self.assertEqual(code, 3)

    def test_a_structural_failure_exit_code_refuses_a_verdict(self) -> None:
        self.write_report(runner_report())
        code, verdict, _ = self.run_gate(environment={"FOUNDRY_FAKE_RUN_EXIT": "2"})
        self.assertEqual(verdict["state"], "structural_failure")
        self.assertEqual(code, 2)

    def test_an_exit_code_outside_the_runner_vocabulary_refuses_a_verdict(self) -> None:
        self.write_report(runner_report())
        code, verdict, _ = self.run_gate(environment={"FOUNDRY_FAKE_RUN_EXIT": "9"})
        self.assertEqual(verdict["state"], "structural_failure")
        self.assertEqual(code, 2)

    def test_a_structural_failure_report_refuses_a_verdict(self) -> None:
        document = runner_report()
        document["success"] = False
        document["outcome"] = "structural_failure"
        self.write_report(document)
        code, verdict, _ = self.run_gate()
        self.assertEqual(verdict["state"], "structural_failure")
        self.assertEqual(code, 2)

    def test_selector_validation_errors_fail_the_job_and_still_publish(self) -> None:
        self.write_report(runner_report())
        code, verdict, output = self.run_gate(
            selection_fixture="selection_fallback.json", environment={"FOUNDRY_FAKE_SELECT_EXIT": "2"}
        )
        self.assertEqual(verdict["state"], "selector_validation_failed")
        self.assertEqual(code, 4)
        # The broad core is inside the families the selector reported, so it still ran and published.
        self.assertEqual(verdict["families_run"], [FAMILY])
        self.assertTrue((output / f"report-{FAMILY}.json").exists())

    def test_a_change_set_that_maps_to_nothing_selects_nothing(self) -> None:
        code, verdict, output = self.run_gate(selection_fixture="selection_none.json")
        self.assertEqual(verdict["state"], "nothing_selected")
        self.assertEqual(code, 0)
        self.assertEqual(verdict["families_run"], [])
        # No family reached the comparison step, so the gate says it looked at no baseline rather than
        # claiming one was present.
        self.assertEqual(verdict["baseline_state"], "not_consulted")
        self.assertTrue((output / "comparison.json").exists())

    def test_a_rule_only_change_set_runs_the_family_comparison(self) -> None:
        # A rule manifest defines a family's matrix, so editing it can remove or rekey cases. The gate
        # must run and compare that family; a change set the selector scoped to nothing would let a
        # vanished case through silently.
        self.changed_paths.write_text(
            "modules/foundry_script/tests/type_completeness/rules/union_destination_membership.json\n",
            encoding="utf-8",
        )
        self.write_report(runner_report())
        baseline = self.work / "baseline"
        self.write_report(runner_report(), directory=baseline)
        code, verdict, output = self.run_gate(selection_fixture="selection_rules_only.json", baseline_dir=baseline)
        self.assertEqual(verdict["state"], "passed")
        self.assertEqual(code, 0)
        self.assertEqual(verdict["families_run"], [FAMILY])
        self.assertTrue((output / f"report-{FAMILY}.json").exists())
        self.assertTrue((output / "comparison.json").exists())

    def test_a_shared_catalog_change_set_runs_the_broad_core(self) -> None:
        self.changed_paths.write_text(
            "modules/foundry_script/tests/type_completeness/partitions/boundary.json\n", encoding="utf-8"
        )
        self.write_report(runner_report())
        baseline = self.work / "baseline"
        self.write_report(runner_report(), directory=baseline)
        code, verdict, _ = self.run_gate(selection_fixture="selection_catalog_shared.json", baseline_dir=baseline)
        self.assertEqual(verdict["state"], "passed")
        self.assertEqual(code, 0)
        self.assertEqual(verdict["families_run"], [FAMILY])

    def test_an_unchanged_failure_with_no_finding_to_reconcile_blocks(self) -> None:
        # A failed case the runner filed no finding against has nothing a ledger entry could name, so
        # no authority for it can exist and it must not be waved through as a known mismatch.
        document = failed_report()
        document["findings"] = []
        self.write_report(document)
        baseline = self.work / "baseline"
        self.write_report(document, directory=baseline)
        code, verdict, _ = self.run_gate(baseline_dir=baseline, environment={"FOUNDRY_FAKE_RUN_EXIT": "1"})
        self.assertEqual(verdict["state"], "blocked")
        self.assertEqual(code, 1)
        self.assertEqual(verdict["known_mismatch_ids"], [])

    def test_an_empty_changed_paths_file_selects_nothing(self) -> None:
        self.changed_paths.write_text("", encoding="utf-8")
        code, verdict, _ = self.run_gate(selection_fixture="selection_none.json")
        self.assertEqual(verdict["state"], "nothing_selected")
        self.assertEqual(code, 0)

    def test_a_malformed_selection_document_is_refused(self) -> None:
        # The real document with one member removed: a producer artifact minus a member, not a
        # hand-written stand-in for one.
        document = json.loads(read_fixture("selection_mapped.json"))
        del document["families"]
        mutated = self.work / "selection_missing_member.json"
        mutated.write_text(json.dumps(document), encoding="utf-8")
        self.write_report(runner_report())
        code, verdict, _ = self.run_gate(selection_fixture=str(mutated))
        self.assertEqual(verdict["state"], "malformed_input")
        self.assertEqual(code, 2)

    def test_an_unknown_comparison_status_is_refused(self) -> None:
        # deserialize_many rejects a status outside comparator.Status, and the wrapper never
        # reinterprets it: the gate refuses rather than treating the artifact as ignorable.
        branch = report.load_report(failed_report())
        develop = report.load_report(presubmit.absent_baseline_report(FAMILY))
        document = json.loads(comparator.serialize_many(comparator.compare_reports(branch, develop, "text")))
        document["artifacts"][0]["status"] = "definitely_not_a_status"
        with self.assertRaises(report.ReportError):
            comparator.deserialize_many(json.dumps(document))

    def test_a_failure_outside_the_selected_slice_never_blocks(self) -> None:
        branch = report.load_report(failed_report())
        develop = report.load_report(presubmit.absent_baseline_report(FAMILY))
        artifacts = comparator.compare_reports(branch, develop, "text")
        gate = presubmit.Gate.__new__(presubmit.Gate)
        gate.verdicts = []
        gate.reasons = []
        gate.ledger_dir = None
        gate.provisional_dir = None
        gate.capabilities = CATALOG / "capabilities.json"
        gate.now = datetime(2026, 1, 1, tzinfo=timezone.utc)
        evaluation = gate.evaluate(artifacts, ["some_other_family"])
        self.assertEqual(evaluation["blocking_comparison_ids"], [])
        self.assertEqual([entry["family"] for entry in evaluation["out_of_slice_failures"]], [FAMILY] * len(artifacts))
        self.assertEqual(gate.verdicts, [])

    def test_an_unchanged_failure_without_any_ledger_authority_blocks(self) -> None:
        document = failed_report()
        self.write_report(document)
        baseline = self.work / "baseline"
        self.write_report(document, directory=baseline)
        code, verdict, _ = self.run_gate(baseline_dir=baseline, environment={"FOUNDRY_FAKE_RUN_EXIT": "1"})
        self.assertEqual(verdict["state"], "blocked")
        self.assertEqual(code, 1)
        self.assertTrue(any("conflicting" in reason for reason in verdict["reasons"]))

    def test_an_unchanged_failure_with_a_merged_ledger_entry_is_a_known_mismatch(self) -> None:
        document = failed_report(classification="product_defect")
        self.write_report(document)
        baseline = self.work / "baseline"
        self.write_report(document, directory=baseline)
        ledger_dir = self.work / "ledger"
        ledger_dir.mkdir()
        finding = document["findings"][0]
        ledger.write_record(
            ledger_dir,
            ledger.proposed_record(
                finding_id=finding["finding_id"],
                family=finding["family"],
                case_id=finding["case_id"],
                dimension=finding["dimension"],
                issue_url="https://example.invalid/issues/1",
                closure_packet_url="https://example.invalid/packets/1",
                permanent_test_paths=["modules/foundry_script/tests/test_type_completeness_harness.h"],
                classification="product_defect",
            ),
        )
        code, verdict, _ = self.run_gate(
            baseline_dir=baseline,
            environment={"FOUNDRY_FAKE_RUN_EXIT": "1"},
            extra_arguments=["--ledger-dir", str(ledger_dir)],
        )
        self.assertEqual(verdict["state"], "passed")
        self.assertEqual(code, 0)
        self.assertEqual(verdict["known_mismatch_ids"], [finding["finding_id"]])
        self.assertEqual(verdict["blocking_comparison_ids"], [])

    def test_a_known_mismatch_still_reports_the_case_as_failed(self) -> None:
        document = failed_report(classification="product_defect")
        self.write_report(document)
        published = json.loads((self.reports / f"{FAMILY}.json").read_text(encoding="utf-8"))
        self.assertFalse(published["cases"][0]["passed"])
        self.assertFalse(published["success"])


class MalformedProvisionalInputTests(GateTestCase):
    """An input the run did not write is a verdict about that input, never a terminated gate."""

    def test_a_provisional_record_that_is_not_an_object_is_a_malformed_verdict(self) -> None:
        for document in ("null", "[]", '"x"', "1"):
            with self.subTest(document=document):
                provisional_dir = Path(self._make_temporary_directory())
                (provisional_dir / "fstcf-v1-eeeeeeeeeeeeeeeeeeee.json").write_text(document, encoding="utf-8")
                self.write_report(failed_report())
                baseline = Path(self._make_temporary_directory())
                self.write_report(runner_report(), directory=baseline)
                code, verdict, _ = self.run_gate(
                    baseline_dir=baseline,
                    extra_arguments=["--provisional-dir", str(provisional_dir)],
                    output_dir=Path(self._make_temporary_directory()),
                )
                self.assertEqual("malformed_input", verdict["state"])
                self.assertEqual(2, code)
                self.assertTrue(
                    any("must be a JSON object" in reason for reason in verdict["reasons"]), verdict["reasons"]
                )


class IntegrityTests(GateTestCase):
    def test_two_runs_on_the_same_inputs_agree_apart_from_timings(self) -> None:
        self.write_report(runner_report())
        baseline = self.work / "baseline"
        self.write_report(runner_report(), directory=baseline)
        first = self.run_gate(baseline_dir=baseline, output_dir=self.work / "first")
        second = self.run_gate(baseline_dir=baseline, output_dir=self.work / "second")
        self.assertEqual(first[0], second[0])
        for name in ("selection.json", "comparison.json"):
            self.assertEqual(
                (first[2] / name).read_text(encoding="utf-8"),
                (second[2] / name).read_text(encoding="utf-8"),
                name,
            )
        left, right = dict(first[1]), dict(second[1])
        for payload in (left, right):
            payload.pop("timings")
        self.assertEqual(left, right)

    def test_the_digest_excludes_timings_and_covers_the_conclusions(self) -> None:
        self.write_report(runner_report())
        _, verdict, _ = self.run_gate()
        digested = {
            "selection": verdict["selection"],
            "families_run": verdict["families_run"],
            "blocking_comparison_ids": verdict["blocking_comparison_ids"],
            "known_mismatch_ids": verdict["known_mismatch_ids"],
            "baseline_state": verdict["baseline_state"],
            "baseline_states": verdict["baseline_states"],
            "state": verdict["state"],
        }
        self.assertEqual(verdict["digest"], comparator.digest_of(digested))

    def test_the_digest_changes_when_a_conclusion_changes(self) -> None:
        self.write_report(runner_report())
        clean = self.run_gate(output_dir=self.work / "clean")[1]
        self.write_report(failed_report())
        blocked = self.run_gate(output_dir=self.work / "blocked", environment={"FOUNDRY_FAKE_RUN_EXIT": "1"})[1]
        self.assertNotEqual(clean["digest"], blocked["digest"])

    def test_the_script_entry_point_matches_the_module_entry_point(self) -> None:
        self.write_report(runner_report())
        baseline = self.work / "baseline"
        self.write_report(runner_report(), directory=baseline)
        module_run = self.run_gate(baseline_dir=baseline, output_dir=self.work / "module")
        script_run = self.run_gate(baseline_dir=baseline, output_dir=self.work / "script", as_script=True)
        self.assertEqual(module_run[0], script_run[0])
        for name in ("selection.json", "comparison.json"):
            self.assertEqual(
                (module_run[2] / name).read_text(encoding="utf-8"),
                (script_run[2] / name).read_text(encoding="utf-8"),
                name,
            )
        left, right = dict(module_run[1]), dict(script_run[1])
        for payload in (left, right):
            payload.pop("timings")
        self.assertEqual(left, right)


if __name__ == "__main__":
    unittest.main()
