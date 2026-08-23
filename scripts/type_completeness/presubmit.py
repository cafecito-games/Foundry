"""Capability-scoped type-completeness gate for pull-request validation.

The wrapper orchestrates four steps and decides nothing the steps already decide:

* selection is `foundry test completeness select`, the only selector there is;
* execution is `foundry test completeness run`, once per selected family;
* comparison against the `develop` baseline is `python3 -m scripts.type_completeness compare`;
* reconciliation of an unchanged in-slice failure is `reconcile.reconcile_finding`.

Every refusal is fail-closed. A selection the selector itself distrusts, a baseline that cannot be
loaded, a run that broke or crossed its budget, and a comparison carrying a status this module has no
disposition for all end the run with a distinct non-zero exit code. A missing baseline is not an
error: it makes every in-slice failure compare against an absent `develop` side, which classifies as
`new` and therefore blocks. A family the branch itself introduces has no baseline by construction:
it is told apart from a missing one by the catalog at the merge base, never by artifact presence alone.
"""

from __future__ import annotations

import argparse
import enum
import json
import os
import shutil
import subprocess
import sys
import time
from datetime import datetime, timezone
from pathlib import Path
from typing import Any, Mapping, Optional, Sequence

if __package__ in (None, ""):
    # Run as a script (`python3 scripts/type_completeness/presubmit.py`), which is the documented
    # local-equivalence entry point. The package form (`python3 -m scripts.type_completeness.presubmit`)
    # takes the relative branch below; both reach the same modules.
    sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
    from scripts.type_completeness import comparator, deadline, ledger, provisional, reconcile, report
else:
    from . import comparator, deadline, ledger, provisional, reconcile, report

VERDICT_SCHEMA_VERSION = 1
SELECTION_SCHEMA_VERSION = 1

# Seconds allowed on top of the runner's own budget before the wrapper stops waiting. The runner
# enforces the budget itself and republishes its reports when it crosses it; this deadline only covers
# a child that stopped honoring its own budget, so it is a margin rather than a second policy.
RUNNER_GRACE_SECONDS = 30


class PresubmitError(Exception):
    """Raised for an input this wrapper refuses to interpret."""


class Verdict(str, enum.Enum):
    """Every state the gate can end in. Exactly one exit code is assigned to each below."""

    PASSED = "passed"
    NOTHING_SELECTED = "nothing_selected"
    BLOCKED = "blocked"
    BASELINE_MISSING = "baseline_missing"
    STRUCTURAL_FAILURE = "structural_failure"
    BASELINE_MALFORMED = "baseline_malformed"
    MALFORMED_INPUT = "malformed_input"
    TIMEOUT = "timeout"
    SELECTOR_VALIDATION_FAILED = "selector_validation_failed"


EXIT_CODES: dict[Verdict, int] = {
    Verdict.PASSED: 0,
    Verdict.NOTHING_SELECTED: 0,
    Verdict.BLOCKED: 1,
    Verdict.BASELINE_MISSING: 1,
    Verdict.STRUCTURAL_FAILURE: 2,
    Verdict.BASELINE_MALFORMED: 2,
    Verdict.MALFORMED_INPUT: 2,
    Verdict.TIMEOUT: 3,
    Verdict.SELECTOR_VALIDATION_FAILED: 4,
}

# Worst first. A selector that refused to scope the change set outranks everything, because nothing
# downstream of it can be trusted to be about the right families; a timeout outranks a structural
# failure so a scheduler can tell an over-budget run from a broken one, matching the runner's own
# exit-code ordering.
VERDICT_PRECEDENCE: tuple[Verdict, ...] = (
    Verdict.SELECTOR_VALIDATION_FAILED,
    Verdict.TIMEOUT,
    Verdict.STRUCTURAL_FAILURE,
    Verdict.BASELINE_MALFORMED,
    Verdict.MALFORMED_INPUT,
    Verdict.BLOCKED,
    Verdict.BASELINE_MISSING,
    Verdict.NOTHING_SELECTED,
    Verdict.PASSED,
)


def exit_code_for(verdict: Verdict) -> int:
    return EXIT_CODES[verdict]


class Disposition(str, enum.Enum):
    """What the gate does with a comparison status."""

    BLOCKING = "blocking"
    KNOWN = "known"
    IGNORED = "ignored"


# One disposition per comparator status, spelled out rather than derived, so adding a status to the
# comparator forces a decision here instead of silently falling into "ignored".
STATUS_DISPOSITION: dict[comparator.Status, Disposition] = {
    comparator.Status.NEW: Disposition.BLOCKING,
    comparator.Status.WORSENED: Disposition.BLOCKING,
    comparator.Status.MISSING: Disposition.BLOCKING,
    comparator.Status.VANISHED: Disposition.BLOCKING,
    comparator.Status.UNCHANGED: Disposition.KNOWN,
    comparator.Status.RESOLVED: Disposition.IGNORED,
    comparator.Status.PASSING: Disposition.IGNORED,
    # Neither side judged the case, so the gate has nothing to block on and nothing to propose.
    comparator.Status.NOT_COVERED: Disposition.IGNORED,
}


def disposition_of(status: comparator.Status) -> Disposition:
    try:
        return STATUS_DISPOSITION[status]
    except KeyError as error:
        raise PresubmitError(f"comparison status {status!r} has no presubmit disposition") from error


class BaselineState(str, enum.Enum):
    """Every state one family's `develop` side can be in; the verdict each one records is in one table below."""

    PRESENT = "present"
    MISSING = "missing"
    MALFORMED = "malformed"
    # The family is in the branch catalog but not in the catalog at the merge base, so no develop run
    # could ever have produced a baseline for it. Every branch case compares as `new` against an empty
    # develop side; a product finding still blocks, and a clean run is a pass rather than a refusal.
    NEW_FAMILY = "new_family"
    # No family reached the comparison step, so no baseline was looked at. Reported as itself rather
    # than as "present", which would claim a baseline this run never saw.
    NOT_CONSULTED = "not_consulted"


# Worst first: one malformed baseline refuses the whole gate, and one missing baseline degrades the
# whole gate to "everything is new" rather than being averaged away by the families that had one.
BASELINE_PRECEDENCE: tuple[BaselineState, ...] = (
    BaselineState.MALFORMED,
    BaselineState.MISSING,
    BaselineState.NEW_FAMILY,
    BaselineState.PRESENT,
)

# The verdict one family's baseline state records, or `None` when the comparison proceeds with nothing
# to hold against the branch. Spelled out per state so adding a state forces a decision here; the
# aggregate-only state is absent on purpose and `verdict_for_baseline` refuses it.
BASELINE_VERDICTS: dict[BaselineState, Optional[Verdict]] = {
    BaselineState.PRESENT: None,
    BaselineState.NEW_FAMILY: None,
    BaselineState.MISSING: Verdict.BASELINE_MISSING,
    BaselineState.MALFORMED: Verdict.BASELINE_MALFORMED,
}


def verdict_for_baseline(state: BaselineState) -> Optional[Verdict]:
    try:
        return BASELINE_VERDICTS[state]
    except KeyError as error:
        raise PresubmitError(f"baseline state {state!r} is not a per-family state") from error


class RunnerExit(enum.IntEnum):
    """The `test completeness run` exit codes, as FSCompletenessCLI::ExitCode defines them."""

    PASSED = 0
    PRODUCT_MISMATCH = 1
    STRUCTURAL_FAILURE = 2
    TIMEOUT = 3


class Selection:
    """The selector's document, validated member by member."""

    def __init__(self, families: Sequence[str], used_broad_core_fallback: bool, validation_errors: Sequence[str]):
        self.families = tuple(families)
        self.used_broad_core_fallback = used_broad_core_fallback
        self.validation_errors = tuple(validation_errors)

    def to_dict(self) -> dict[str, Any]:
        return {
            "schema_version": SELECTION_SCHEMA_VERSION,
            "families": list(self.families),
            "used_broad_core_fallback": self.used_broad_core_fallback,
            "validation_errors": list(self.validation_errors),
        }


def _string_array(data: Mapping[str, Any], member: str) -> list[str]:
    value = data.get(member)
    if member not in data:
        raise PresubmitError(f"selection document is missing required member {member!r}")
    if not isinstance(value, list) or any(not isinstance(entry, str) for entry in value):
        raise PresubmitError(f"selection document member {member!r} must be an array of strings")
    return list(value)


def parse_selection(text: str) -> Selection:
    """Parse the selector's stdout.

    The selector writes the document and nothing else on stdout, but a headless engine boot may print
    diagnostics ahead of it, so the document is located rather than assumed to start at byte zero. Any
    trailing text after it is a refusal: a stream carrying two documents has no single selection.
    """
    start = text.find("{")
    if start < 0:
        raise PresubmitError("selector produced no JSON document")
    try:
        data, end = json.JSONDecoder().raw_decode(text[start:])
    except ValueError as error:
        raise PresubmitError(f"selector document is not valid JSON: {error}") from error
    if text[start + end :].strip():
        raise PresubmitError("selector stdout carries trailing text after its JSON document")
    if not isinstance(data, Mapping):
        raise PresubmitError("selector document must be a JSON object")
    if not report.schema_version_matches(data.get("schema_version"), SELECTION_SCHEMA_VERSION):
        raise PresubmitError(f"unsupported selection schema_version {data.get('schema_version')!r}")
    fallback = data.get("used_broad_core_fallback")
    if not isinstance(fallback, bool):
        raise PresubmitError("selection document member 'used_broad_core_fallback' must be a JSON boolean")
    return Selection(
        families=_string_array(data, "families"),
        used_broad_core_fallback=fallback,
        validation_errors=_string_array(data, "validation_errors"),
    )


def absent_baseline_report(family: str) -> dict[str, Any]:
    """The `develop` side of a family whose baseline artifact does not exist.

    It is a real, loadable report that states exactly what is known: `develop` contributed no cases.
    Comparing a branch failure against it classifies as `new`, which is the fail-closed reading a
    missing baseline must have. It is never published as evidence of a run.
    """
    return {
        "schema_version": 1,
        "family": family,
        "success": True,
        "outcome": "passed",
        "cases": [],
        "findings": [],
    }


def write_json(path: Path, payload: Any) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload, sort_keys=True, indent=2) + "\n", encoding="utf-8")


def _resolve_scratch_root(requested: str) -> Path:
    if requested:
        return Path(requested).resolve()
    configured = os.environ.get("FOUNDRY_TEST_SCRATCH", "")
    if not configured:
        raise PresubmitError(
            "--scratch is required unless FOUNDRY_TEST_SCRATCH names the test scratch root; the runner "
            "refuses a report path outside it"
        )
    return Path(configured).resolve() / "type-completeness-presubmit"


def _run(command: Sequence[str], timeout: Optional[float] = None) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        list(command),
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        universal_newlines=True,
        timeout=timeout,
    )


def load_presubmit_budget(budgets_path: Path) -> int:
    try:
        data = json.loads(budgets_path.read_text(encoding="utf-8"))
    except (OSError, ValueError) as error:
        raise PresubmitError(f"cannot read budgets document {budgets_path}: {error}") from error
    value = report.require_object(data, f"budgets document {budgets_path}", PresubmitError).get(
        "presubmit_hard_timeout_seconds"
    )
    if not report.is_integral_number(value) or not isinstance(value, (int, float)) or int(value) <= 0:
        raise PresubmitError(f"budgets document {budgets_path} has no positive 'presubmit_hard_timeout_seconds'")
    return int(value)


def git_merge_base(merge_base_ref: str, repository_root: Path) -> str:
    """The `develop` commit the branch is measured against."""
    merge_base = _run(["git", "-C", str(repository_root), "merge-base", merge_base_ref, "HEAD"])
    if merge_base.returncode != 0:
        raise PresubmitError(f"cannot resolve the merge base with {merge_base_ref}: {merge_base.stderr.strip()}")
    return merge_base.stdout.strip()


def git_path_exists_at(commit: str, relative_path: str, repository_root: Path) -> bool:
    """Whether a tracked path exists in one commit's tree, read from git rather than from the checkout."""
    listed = _run(["git", "-C", str(repository_root), "ls-tree", "--name-only", commit, "--", relative_path])
    if listed.returncode != 0:
        raise PresubmitError(f"cannot read {relative_path} at {commit}: {listed.stderr.strip()}")
    return bool(listed.stdout.strip())


def git_changed_paths(merge_base_ref: str, repository_root: Path) -> tuple[list[str], str]:
    """The change set and the `develop` commit it is measured against.

    Both come from one merge base, so the baseline the gate downloads is the report of exactly the
    commit the diff was taken against.
    """
    sha = git_merge_base(merge_base_ref, repository_root)
    # `--no-renames` on purpose: with rename detection a moved file reports only its destination, so
    # a mapped production file moved under a nonproduction prefix would disappear from the change set
    # and its family would never be selected. Both endpoints of a move must reach the capability map.
    diff = _run(["git", "-C", str(repository_root), "diff", "--name-only", "--no-renames", sha, "HEAD"])
    if diff.returncode != 0:
        raise PresubmitError(f"cannot list changed paths against {sha}: {diff.stderr.strip()}")
    return [line for line in diff.stdout.split("\n") if line], sha


class Gate:
    def __init__(self, arguments: argparse.Namespace):
        self.binary = Path(arguments.binary)
        self.output_dir = Path(arguments.output_dir)
        self.repository_root = Path(arguments.repository_root).resolve()
        self.catalog = Path(arguments.catalog)
        # The runner only writes below the configured test scratch space and refuses a report path
        # outside it, so there is no defaulting to invent here: an unnamed scratch root is refused
        # rather than turned into a path every family run would fail on.
        self.scratch = _resolve_scratch_root(arguments.scratch)
        self.configuration = arguments.configuration
        self.baseline_dir = Path(arguments.baseline_dir) if arguments.baseline_dir else None
        self.ledger_dir = Path(arguments.ledger_dir) if arguments.ledger_dir else None
        self.provisional_dir = Path(arguments.provisional_dir) if arguments.provisional_dir else None
        self.capabilities = Path(arguments.capabilities)
        self.merge_base_ref = arguments.merge_base_ref
        self.changed_paths_file = Path(arguments.changed_paths) if arguments.changed_paths else None
        self.budget_seconds = load_presubmit_budget(Path(arguments.budgets))
        self.grace_seconds = arguments.runner_grace_seconds
        self.now = deadline.parse_timestamp(arguments.now) if arguments.now else datetime.now(timezone.utc)
        self.reasons: list[str] = []
        self.verdicts: list[Verdict] = []

    # Recording, not deciding: the worst recorded verdict is chosen once, at the end.
    def record(self, verdict: Verdict, reason: str) -> None:
        self.verdicts.append(verdict)
        self.reasons.append(reason)

    def worst_verdict(self) -> Verdict:
        for verdict in VERDICT_PRECEDENCE:
            if verdict in self.verdicts:
                return verdict
        return Verdict.PASSED

    def changed_paths(self) -> tuple[Path, str]:
        if self.changed_paths_file is not None:
            # The change set is given, but the merge base is still what decides whether a family is
            # new to the branch, so it is resolved either way.
            return self.changed_paths_file, git_merge_base(self.merge_base_ref, self.repository_root)
        paths, sha = git_changed_paths(self.merge_base_ref, self.repository_root)
        destination = self.output_dir / "changed_paths.txt"
        destination.parent.mkdir(parents=True, exist_ok=True)
        destination.write_text("".join(f"{path}\n" for path in paths), encoding="utf-8")
        return destination, sha

    def select(self, changed_paths_file: Path) -> Selection:
        completed = _run(
            [
                str(self.binary),
                "--headless",
                "test",
                "completeness",
                "select",
                "--changed-paths",
                str(changed_paths_file),
                "--catalog",
                str(self.catalog),
                "--json",
            ]
        )
        try:
            return parse_selection(completed.stdout)
        except PresubmitError as error:
            raise PresubmitError(f"{error} (selector exit {completed.returncode}: {completed.stderr.strip()})")

    def run_family(self, family: str) -> tuple[Path, Optional[Verdict], str]:
        """Run one family and translate the runner's own verdict; the report path is always returned."""
        report_path = self.scratch / f"{family}" / "report.json"
        report_path.parent.mkdir(parents=True, exist_ok=True)
        command = [
            str(self.binary),
            "--headless",
            "test",
            "completeness",
            "run",
            "--family",
            family,
            "--catalog",
            str(self.catalog),
            "--scratch",
            str(report_path.parent),
            "--report",
            str(report_path),
            "--tier",
            "presubmit",
            "--timeout-seconds",
            str(self.budget_seconds),
        ]
        try:
            completed = _run(command, timeout=self.budget_seconds + self.grace_seconds)
        except subprocess.TimeoutExpired:
            # A partial report is still evidence, so whatever the run managed to publish is published.
            return (
                self.publish_report(family, report_path),
                Verdict.TIMEOUT,
                f"family {family} exceeded the wrapper deadline",
            )
        if completed.returncode == RunnerExit.TIMEOUT:
            return (
                self.publish_report(family, report_path),
                Verdict.TIMEOUT,
                f"family {family} crossed its {self.budget_seconds}s budget",
            )
        if completed.returncode == RunnerExit.STRUCTURAL_FAILURE:
            return (
                self.publish_report(family, report_path),
                Verdict.STRUCTURAL_FAILURE,
                f"family {family} reported a structural failure",
            )
        if completed.returncode not in (RunnerExit.PASSED, RunnerExit.PRODUCT_MISMATCH):
            # An exit code outside the runner's vocabulary says nothing about the product, so it is read
            # as a broken run rather than as a clean one.
            return (
                self.publish_report(family, report_path),
                Verdict.STRUCTURAL_FAILURE,
                f"family {family} exited {completed.returncode}, which is outside the runner's exit codes",
            )
        return self.publish_report(family, report_path), None, ""

    def publish_report(self, family: str, report_path: Path) -> Path:
        """Copy a published report next to the other gate documents and read it from there.

        The runner writes below the test scratch space, which the gate does not own and CI does not
        upload. Every report the run produced therefore lands in the output directory, so the artifact
        carries the evidence behind the verdict rather than a verdict with no evidence.
        """
        if not report_path.exists():
            return report_path
        destination = self.output_dir / f"report-{family}.json"
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(report_path, destination)
        return destination

    def family_exists_at(self, family: str, merge_base: str) -> bool:
        """Whether the catalog at the merge base defines the family, read from the rule manifest's path."""
        try:
            catalog = self.catalog.resolve().relative_to(self.repository_root)
        except ValueError as error:
            raise PresubmitError(
                f"catalog {self.catalog} is outside repository {self.repository_root}, so the merge-base "
                "catalog cannot be consulted"
            ) from error
        return git_path_exists_at(merge_base, (catalog / "rules" / f"{family}.json").as_posix(), self.repository_root)

    def baseline_for(self, family: str, merge_base: str) -> tuple[Path, BaselineState, str]:
        """Resolve the `develop` side of one family, materializing the absent-baseline report when needed.

        An absent artifact is read two ways, decided by the merge-base catalog and never by the artifact
        alone: a family that catalog does not define is new to the branch, and a family it does define
        has a baseline that could not be fetched.
        """
        candidate = None if self.baseline_dir is None else self.baseline_dir / f"{family}.json"
        if candidate is None or not candidate.exists():
            destination = self.output_dir / f"baseline-absent-{family}.json"
            write_json(destination, absent_baseline_report(family))
            if not self.family_exists_at(family, merge_base):
                return (
                    destination,
                    BaselineState.NEW_FAMILY,
                    f"family {family} is not in the develop catalog at {merge_base}; every case is new",
                )
            return destination, BaselineState.MISSING, f"no develop baseline artifact for family {family}"
        try:
            report.load_report_file(candidate)
        except report.ReportError as error:
            return candidate, BaselineState.MALFORMED, f"develop baseline for family {family} is malformed: {error}"
        return candidate, BaselineState.PRESENT, ""

    def compare(self, family: str, branch_report: Path, develop_report: Path) -> tuple[Path, Optional[Verdict], str]:
        output = self.output_dir / f"comparison-{family}.json"
        completed = _run(
            [
                sys.executable,
                "-m",
                "scripts.type_completeness",
                "compare",
                "--branch-report",
                str(branch_report),
                "--develop-report",
                str(develop_report),
                "--configuration",
                self.configuration,
                "--capabilities",
                str(self.capabilities),
                "--output",
                str(output),
            ],
            timeout=self.budget_seconds + self.grace_seconds,
        )
        if completed.returncode != 0:
            return (
                output,
                Verdict.STRUCTURAL_FAILURE,
                f"comparison of family {family} refused a verdict: {completed.stdout.strip()}",
            )
        return output, None, ""

    def provisional_records(self) -> dict[str, provisional.ProvisionalRecord]:
        if self.provisional_dir is None or not self.provisional_dir.is_dir():
            return {}
        capabilities = report.load_capabilities_file(self.capabilities)
        records: dict[str, provisional.ProvisionalRecord] = {}
        for path in sorted(self.provisional_dir.glob("*.json")):
            record = provisional.ProvisionalRecord.from_dict(
                json.loads(path.read_text(encoding="utf-8")), capabilities=capabilities
            )
            records[record.finding_id] = record
        return records

    def evaluate(self, artifacts: Sequence[comparator.ComparisonArtifact], selected: Sequence[str]) -> dict[str, Any]:
        merged_ledger = ledger.load_ledger(self.ledger_dir) if self.ledger_dir is not None else {}
        records = self.provisional_records()
        blocking: list[str] = []
        known: list[dict[str, str]] = []
        out_of_slice: list[dict[str, str]] = []
        for artifact in sorted(artifacts, key=lambda entry: entry.comparison_id):
            if artifact.family not in selected:
                # A failure outside the selected slice is published and never blocks: this change set was
                # not scoped to it, so it carries no evidence about it either way.
                out_of_slice.append({"comparison_id": artifact.comparison_id, "family": artifact.family})
                continue
            disposition = disposition_of(artifact.status)
            if disposition is Disposition.IGNORED:
                continue
            if disposition is Disposition.BLOCKING:
                blocking.append(artifact.comparison_id)
                self.record(
                    Verdict.BLOCKED,
                    f"{artifact.case_id} is {artifact.status.value} in family {artifact.family}",
                )
                continue
            findings = artifact.branch.get("findings") or []
            if not findings:
                # An unchanged failure carrying no finding has nothing a ledger entry could ever be
                # filed against, so no authority can exist for it and it cannot be waved through.
                blocking.append(artifact.comparison_id)
                self.record(
                    Verdict.BLOCKED,
                    f"{artifact.case_id} fails unchanged in family {artifact.family} with no finding to reconcile",
                )
                continue
            for finding in findings:
                finding_id = str(finding["finding_id"])
                result = reconcile.reconcile_finding(
                    finding_id=finding_id,
                    provisional_record=records.get(finding_id),
                    merged_record=merged_ledger.get(finding_id),
                    pull_request=None,
                    now=self.now,
                    comparison_status=artifact.status.value,
                )
                entry = {
                    "comparison_id": artifact.comparison_id,
                    "finding_id": finding_id,
                    "state": result.state.value,
                }
                if result.blocks_slice:
                    blocking.append(artifact.comparison_id)
                    self.record(
                        Verdict.BLOCKED,
                        f"{finding_id} reconciles as {result.state.value}: {result.reason}",
                    )
                else:
                    known.append(entry)
        return {
            "blocking_comparison_ids": sorted(set(blocking)),
            "known_mismatches": known,
            "known_mismatch_ids": sorted({entry["finding_id"] for entry in known}),
            "out_of_slice_failures": out_of_slice,
        }


def _empty_evaluation() -> dict[str, Any]:
    return {
        "blocking_comparison_ids": [],
        "known_mismatches": [],
        "known_mismatch_ids": [],
        "out_of_slice_failures": [],
    }


def run_gate(arguments: argparse.Namespace) -> int:
    started = time.monotonic()
    gate = Gate(arguments)
    gate.output_dir.mkdir(parents=True, exist_ok=True)
    baseline_states: dict[str, BaselineState] = {}
    families_run: list[str] = []
    evaluation = _empty_evaluation()
    merge_base = ""
    selection = Selection((), False, ())

    try:
        changed_paths_file, merge_base = gate.changed_paths()
        selection = gate.select(changed_paths_file)
    except PresubmitError as error:
        gate.record(Verdict.MALFORMED_INPUT, str(error))
        return _publish(gate, selection, families_run, baseline_states, evaluation, merge_base, started)

    write_json(gate.output_dir / "selection.json", selection.to_dict())

    if selection.validation_errors:
        # The selection cannot be trusted to scope anything, so nothing is compared against develop.
        # The families the selector did reach - which include the broad core, because every validation
        # error also triggers the fallback - are still run and published, and the job still fails.
        for error_text in selection.validation_errors:
            gate.record(Verdict.SELECTOR_VALIDATION_FAILED, error_text)
    elif not selection.families:
        gate.record(Verdict.NOTHING_SELECTED, "no changed path maps to a type-completeness family")
        write_json(
            gate.output_dir / "comparison.json",
            {"schema_version": comparator.COMPARISON_SCHEMA_VERSION, "artifacts": []},
        )
        return _publish(gate, selection, families_run, baseline_states, evaluation, merge_base, started)

    artifacts: list[comparator.ComparisonArtifact] = []
    refusals: list[str] = []
    for family in selection.families:
        branch_report, verdict, reason = gate.run_family(family)
        families_run.append(family)
        if verdict is not None:
            gate.record(verdict, reason)
            refusals.append(reason)
            continue
        if selection.validation_errors:
            continue
        try:
            loaded = report.load_report_file(branch_report)
        except report.ReportError as error:
            gate.record(Verdict.STRUCTURAL_FAILURE, f"branch report for family {family} is unusable: {error}")
            refusals.append(str(error))
            continue
        if loaded.is_structural_failure:
            gate.record(Verdict.STRUCTURAL_FAILURE, f"family {family} published a structural failure")
            refusals.append(f"family {family} published a structural failure")
            continue
        try:
            develop_report, baseline_state, baseline_reason = gate.baseline_for(family, merge_base)
            baseline_verdict = verdict_for_baseline(baseline_state)
        except PresubmitError as error:
            gate.record(Verdict.MALFORMED_INPUT, str(error))
            refusals.append(str(error))
            continue
        baseline_states[family] = baseline_state
        if baseline_verdict is Verdict.BASELINE_MALFORMED:
            gate.record(baseline_verdict, baseline_reason)
            refusals.append(baseline_reason)
            continue
        if baseline_verdict is not None:
            # Without a develop side the comparison can only see the branch: a case that existed on
            # develop and vanished from the branch produces no artifact at all, so a run with no
            # baseline cannot demonstrate the absence of a regression and must not report one. A new
            # family is exempt: nothing could have vanished from a develop catalog that never had it.
            gate.record(baseline_verdict, baseline_reason)
        comparison_path, verdict, reason = gate.compare(family, branch_report, develop_report)
        if verdict is not None:
            gate.record(verdict, reason)
            refusals.append(reason)
            continue
        try:
            artifacts.extend(comparator.deserialize_many(comparison_path.read_text(encoding="utf-8")))
        except report.ReportError as error:
            gate.record(Verdict.MALFORMED_INPUT, f"comparison of family {family} is unusable: {error}")
            refusals.append(str(error))

    if refusals:
        write_json(gate.output_dir / "comparison.json", json.loads(comparator.serialize_structural_failure(refusals)))
    else:
        write_json(
            gate.output_dir / "comparison.json",
            json.loads(comparator.serialize_many(sorted(artifacts, key=lambda entry: entry.comparison_id))),
        )
        try:
            evaluation = gate.evaluate(artifacts, selection.families)
        # Evaluation reads the tracked ledger and the provisional records the tracking issues carry, and
        # every one of those is an input this run did not write. A malformed one is a verdict about the
        # input, not a reason for the gate to terminate without publishing anything. `ValueError` is the
        # base of both loaders' refusals - `ProvisionalError`, `ReportError` - and is what the ledger
        # raises directly.
        except (PresubmitError, ValueError) as error:
            gate.record(Verdict.MALFORMED_INPUT, str(error))

    return _publish(gate, selection, families_run, baseline_states, evaluation, merge_base, started)


def _aggregate_baseline_state(states: Mapping[str, BaselineState]) -> BaselineState:
    for state in BASELINE_PRECEDENCE:
        if state in states.values():
            return state
    return BaselineState.NOT_CONSULTED


def _publish(
    gate: Gate,
    selection: Selection,
    families_run: Sequence[str],
    baseline_states: Mapping[str, BaselineState],
    evaluation: Mapping[str, Any],
    merge_base: str,
    started: float,
) -> int:
    verdict = gate.worst_verdict()
    baseline_state = _aggregate_baseline_state(baseline_states)
    # Exactly the fields a rerun on the same branch and baseline must reproduce. Timings, artifact
    # paths, run ids, and the merge-base commit are all excluded: none of them is a conclusion.
    digested: dict[str, Any] = {
        "selection": selection.to_dict(),
        "families_run": list(families_run),
        "blocking_comparison_ids": list(evaluation["blocking_comparison_ids"]),
        "known_mismatch_ids": list(evaluation["known_mismatch_ids"]),
        "baseline_state": baseline_state.value,
        "baseline_states": {family: state.value for family, state in sorted(baseline_states.items())},
        "state": verdict.value,
    }
    payload: dict[str, Any] = dict(digested)
    payload.update(
        {
            "schema_version": VERDICT_SCHEMA_VERSION,
            "exit_code": exit_code_for(verdict),
            "digest": comparator.digest_of(digested),
            "known_mismatches": list(evaluation["known_mismatches"]),
            "out_of_slice_failures": list(evaluation["out_of_slice_failures"]),
            "reasons": list(gate.reasons),
            "merge_base": merge_base,
            "timings": {"total_seconds": round(time.monotonic() - started, 3)},
        }
    )
    write_json(gate.output_dir / "verdict.json", payload)
    for reason in gate.reasons:
        print(f"{verdict.value}: {reason}")
    print(f"type-completeness presubmit {verdict.value} (exit {exit_code_for(verdict)})")
    return exit_code_for(verdict)


DEFAULT_CATALOG = Path("modules/foundry_script/tests/type_completeness")


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(prog="type_completeness_presubmit")
    parser.add_argument("--binary", required=True, help="the foundry editor binary that owns selection and execution")
    parser.add_argument("--output-dir", required=True, help="destination of selection/comparison/verdict documents")
    parser.add_argument("--repository-root", default=".", help="repository the change set is measured in")
    parser.add_argument("--catalog", default=str(DEFAULT_CATALOG))
    parser.add_argument(
        "--scratch",
        default="",
        help="owned scratch root below the test scratch space; defaults to $FOUNDRY_TEST_SCRATCH/type-completeness-presubmit",
    )
    parser.add_argument("--configuration", default="text", help="capability configuration label the reports carry")
    parser.add_argument("--changed-paths", default="", help="pre-computed changed-paths file; otherwise git is used")
    parser.add_argument("--merge-base-ref", default="origin/develop")
    parser.add_argument("--baseline-dir", default="", help="directory of <family>.json develop baseline reports")
    parser.add_argument("--ledger-dir", default="")
    parser.add_argument("--provisional-dir", default="")
    parser.add_argument("--capabilities", default=str(report.DEFAULT_CAPABILITIES_PATH))
    parser.add_argument("--budgets", default=str(DEFAULT_CATALOG / "budgets.json"))
    parser.add_argument(
        "--runner-grace-seconds",
        type=int,
        default=RUNNER_GRACE_SECONDS,
        help="seconds allowed past the runner's own budget before the wrapper stops waiting",
    )
    parser.add_argument("--now", default="", help="ISO-8601 timestamp with offset used for reconciliation deadlines")
    return parser


def main(argv: Optional[list[str]] = None) -> int:
    arguments = build_parser().parse_args(argv)
    try:
        return run_gate(arguments)
    except PresubmitError as error:
        print(f"{Verdict.MALFORMED_INPUT.value}: {error}", file=sys.stderr)
        return exit_code_for(Verdict.MALFORMED_INPUT)


if __name__ == "__main__":
    sys.exit(main())
