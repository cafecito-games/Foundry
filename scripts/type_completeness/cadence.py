"""Cadence check over the scheduled mutation workflow.

``check`` reads a capture of ``gh run list --workflow type_completeness_nightly.yml --json
databaseId,conclusion,createdAt,headBranch`` and the tracked shard list, and fails unless every
shard (and therefore every recipe it schedules) has a terminal run inside the window. The script
never calls ``gh`` itself: the caller captures the JSON, so the check is testable without network.

A run is terminal only when its ``conclusion`` is ``success`` or ``failure``; a cancelled, skipped,
or still-running (``null``) run published no verdict. A run's conclusion alone does not say which
shard reached its verdict - a workflow can fail at checkout before any recipe runs - so a shard
counts as covered only when the run's shard job (``Mutation shard <shard_id>``, captured with
``gh run view <id> --json databaseId,conclusion,jobs``) itself concluded ``success`` or ``failure``.
A terminal run with no jobs capture, or whose shard job is not terminal, leaves the shard missing
and is named in the output. Every run of the workflow executes every
shard of the matrix, so a shard's cadence is the workflow's cadence on the watched branch.

Stdlib only, Python 3.8.
"""

from __future__ import annotations

import argparse
import json
import sys
from dataclasses import dataclass
from datetime import datetime, timedelta, timezone
from pathlib import Path
from typing import Any, Mapping, Optional, Sequence

from .deadline import parse_timestamp
from .mutation import Shard, load_shards

# Exit code for a document the check cannot read: malformed is never "no runs".
EXIT_INVALID_INPUT = 2
EXIT_MISSING = 1

DEFAULT_BRANCH = "develop"
DEFAULT_WINDOW_HOURS = 24

# The nightly workflow's shard job name; the workflow test asserts the two agree.
SHARD_JOB_NAME_TEMPLATE = "Mutation shard {shard_id}"

# Every conclusion GitHub Actions writes for a workflow run, plus null for a run still in progress.
KNOWN_CONCLUSIONS = (
    "success",
    "failure",
    "cancelled",
    "skipped",
    "timed_out",
    "action_required",
    "neutral",
    "stale",
    "startup_failure",
)
TERMINAL_CONCLUSIONS = ("success", "failure")


class CadenceError(ValueError):
    """Raised when an input document cannot be interpreted."""


def is_terminal(conclusion: Optional[str]) -> bool:
    return conclusion in TERMINAL_CONCLUSIONS


@dataclass(frozen=True)
class WorkflowRun:
    run_id: int
    conclusion: Optional[str]
    created_at: datetime
    branch: str
    raw: dict[str, Any]


def _require(mapping: Mapping[str, Any], key: str, context: str) -> Any:
    if key not in mapping:
        raise CadenceError(f"{context} is missing member {key!r}")
    return mapping[key]


def load_runs(data: Any) -> list[WorkflowRun]:
    if not isinstance(data, list):
        raise CadenceError("gh run list output must be a JSON array of runs")
    runs: list[WorkflowRun] = []
    for index, raw in enumerate(data):
        context = f"run[{index}]"
        if not isinstance(raw, Mapping):
            raise CadenceError(f"{context} must be an object")
        run_id = _require(raw, "databaseId", context)
        if isinstance(run_id, bool) or not isinstance(run_id, int):
            raise CadenceError(f"{context}.databaseId must be an integer; got {run_id!r}")
        conclusion = _require(raw, "conclusion", context)
        if conclusion is not None and (not isinstance(conclusion, str) or conclusion not in KNOWN_CONCLUSIONS):
            raise CadenceError(f"{context}.conclusion {conclusion!r} is not a known conclusion")
        created_text = _require(raw, "createdAt", context)
        if not isinstance(created_text, str):
            raise CadenceError(f"{context}.createdAt must be a timestamp string; got {created_text!r}")
        try:
            created_at = parse_timestamp(created_text)
        except ValueError as error:
            raise CadenceError(f"{context}.createdAt: {error}") from error
        branch = _require(raw, "headBranch", context)
        if not isinstance(branch, str):
            raise CadenceError(f"{context}.headBranch must be a string; got {branch!r}")
        runs.append(WorkflowRun(run_id, conclusion, created_at, branch, dict(raw)))
    return runs


def _read_json(path: Path, context: str) -> Any:
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except (OSError, ValueError) as error:
        raise CadenceError(f"cannot read {context} {path}: {error}") from error


def load_runs_file(path: Path) -> list[WorkflowRun]:
    return load_runs(_read_json(path, "gh run list capture"))


@dataclass(frozen=True)
class RunJobs:
    run_id: int
    job_conclusions: dict[str, Optional[str]]


def load_run_jobs(data: Any) -> RunJobs:
    """One ``gh run view <id> --json databaseId,conclusion,jobs`` capture."""
    if not isinstance(data, Mapping):
        raise CadenceError("gh run view output must be a JSON object")
    run_id = _require(data, "databaseId", "run view")
    if isinstance(run_id, bool) or not isinstance(run_id, int):
        raise CadenceError(f"run view databaseId must be an integer; got {run_id!r}")
    jobs = _require(data, "jobs", "run view")
    if not isinstance(jobs, list):
        raise CadenceError(f"run view {run_id} jobs must be an array")
    conclusions: dict[str, Optional[str]] = {}
    for index, job in enumerate(jobs):
        context = f"run view {run_id} jobs[{index}]"
        if not isinstance(job, Mapping):
            raise CadenceError(f"{context} must be an object")
        name = _require(job, "name", context)
        if not isinstance(name, str):
            raise CadenceError(f"{context}.name must be a string")
        conclusion = _require(job, "conclusion", context)
        if conclusion is not None and (not isinstance(conclusion, str) or conclusion not in KNOWN_CONCLUSIONS):
            raise CadenceError(f"{context}.conclusion {conclusion!r} is not a known conclusion")
        if name in conclusions:
            raise CadenceError(f"{context} repeats job name {name!r}")
        conclusions[name] = conclusion
    return RunJobs(run_id, conclusions)


def load_run_jobs_files(paths: Sequence[Path]) -> dict[int, RunJobs]:
    captures: dict[int, RunJobs] = {}
    for path in paths:
        capture = load_run_jobs(_read_json(path, "gh run view capture"))
        if capture.run_id in captures:
            raise CadenceError(f"{path}: run {capture.run_id} is captured twice")
        captures[capture.run_id] = capture
    return captures


def load_shards_file(path: Path) -> list[Shard]:
    data = _read_json(path, "shards")
    if not isinstance(data, dict):
        raise CadenceError(f"shards {path} must contain a JSON object")
    try:
        return load_shards(data, path.parent)
    except ValueError as error:
        raise CadenceError(str(error)) from error


@dataclass(frozen=True)
class CadenceVerdict:
    covered: dict[str, WorkflowRun]
    missing: list[tuple[str, str]]
    # Terminal runs in the window that could not vouch for a shard, with the reason.
    unattributed: list[tuple[WorkflowRun, str]]

    @property
    def ok(self) -> bool:
        return not self.missing


def check_cadence(
    runs: Sequence[WorkflowRun],
    shards: Sequence[Shard],
    now: datetime,
    window: timedelta,
    branch: str,
    run_jobs: Mapping[int, RunJobs],
) -> CadenceVerdict:
    if now.tzinfo is None:
        raise CadenceError("now must be timezone-aware")
    if window <= timedelta(0):
        raise CadenceError("the window must be positive")
    start = now - window
    terminal = [
        run for run in runs if run.branch == branch and is_terminal(run.conclusion) and start <= run.created_at <= now
    ]
    terminal.sort(key=lambda run: run.created_at, reverse=True)
    covered: dict[str, WorkflowRun] = {}
    missing: list[tuple[str, str]] = []
    unattributed: list[tuple[WorkflowRun, str]] = []
    for shard in shards:
        job_name = SHARD_JOB_NAME_TEMPLATE.format(shard_id=shard.shard_id)
        for run in terminal:
            jobs = run_jobs.get(run.run_id)
            if jobs is None:
                unattributed.append((run, "no gh run view capture for this run"))
                continue
            if job_name not in jobs.job_conclusions:
                unattributed.append((run, f"no job named {job_name!r}"))
                continue
            conclusion = jobs.job_conclusions[job_name]
            if not is_terminal(conclusion):
                unattributed.append((run, f"job {job_name!r} concluded {conclusion!r}"))
                continue
            covered[shard.shard_id] = run
            break
        else:
            for recipe_id in shard.recipes:
                missing.append((shard.shard_id, recipe_id))
    return CadenceVerdict(covered, missing, unattributed)


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(prog="cadence")
    commands = parser.add_subparsers(dest="command", required=True)
    check = commands.add_parser("check", help="Fail unless every shard has a terminal run inside the window.")
    check.add_argument("--window-hours", type=int, default=DEFAULT_WINDOW_HOURS)
    check.add_argument(
        "--runs-json", required=True, help="Captured `gh run list --json databaseId,conclusion,createdAt,headBranch`."
    )
    check.add_argument("--shards", required=True, help="The tracked mutations/shards.json.")
    check.add_argument(
        "--jobs-json",
        nargs="*",
        default=[],
        help="Captured `gh run view <id> --json databaseId,conclusion,jobs`, one file per run in the window.",
    )
    check.add_argument("--branch", default=DEFAULT_BRANCH, help="Only runs on this branch count.")
    check.add_argument("--now", help="ISO-8601 instant with an explicit offset; defaults to the current UTC time.")
    return parser


def main(argv: Optional[list[str]] = None) -> int:
    arguments = build_parser().parse_args(argv)
    try:
        if arguments.window_hours <= 0:
            raise CadenceError("--window-hours must be a positive number of hours")
        try:
            now = parse_timestamp(arguments.now) if arguments.now else datetime.now(timezone.utc)
        except ValueError as error:
            raise CadenceError(f"--now: {error}") from error
        runs = load_runs_file(Path(arguments.runs_json))
        shards = load_shards_file(Path(arguments.shards))
        run_jobs = load_run_jobs_files([Path(entry) for entry in arguments.jobs_json])
        verdict = check_cadence(runs, shards, now, timedelta(hours=arguments.window_hours), arguments.branch, run_jobs)
    except CadenceError as error:
        print(f"error: {error}", file=sys.stderr)
        return EXIT_INVALID_INPUT
    for shard_id, run in sorted(verdict.covered.items()):
        print(f"ok shard={shard_id} run={run.run_id} conclusion={run.conclusion} created_at={run.raw['createdAt']}")
    for run, reason in verdict.unattributed:
        print(f"unattributed run={run.run_id} conclusion={run.conclusion} created_at={run.raw['createdAt']}: {reason}")
    for shard_id, recipe_id in verdict.missing:
        print(
            f"missing shard={shard_id} recipe={recipe_id}: no terminal run within {arguments.window_hours}h on {arguments.branch}"
        )
    return EXIT_MISSING if verdict.missing else 0


if __name__ == "__main__":
    sys.exit(main())
