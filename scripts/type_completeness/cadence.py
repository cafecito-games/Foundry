"""Cadence check over the scheduled mutation workflow.

``check`` reads a capture of ``gh run list --workflow type_completeness_nightly.yml --json
databaseId,conclusion,createdAt,headBranch``, the tracked shard list, and a directory of
downloaded run artifacts; it fails unless every recipe of every shard published a terminal
mutation result inside the window. The script never calls ``gh`` itself: the caller captures the
JSON and downloads each run's shard artifacts into the one canonical layout::

    gh run download <run_id> -n <artifact> -D <artifacts>/<artifact>

where ``<artifact>`` is ``ARTIFACT_NAME_TEMPLATE`` filled with the shard and run id, so
``<artifacts>/<artifact>/<recipe_id>/mutation_result.json`` is the file the workflow's run step
wrote (the upload packs ``results/`` at the artifact root). The check is testable without network.

A run is terminal only when its ``conclusion`` is ``success`` or ``failure``; a cancelled, skipped,
or still-running (``null``) run published no verdict. Neither a run's nor a job's conclusion says
whether a recipe reached its verdict - a shard job can fail at checkout before any recipe runs - so
a recipe is credited only when the run's artifact
``<artifacts>/type-completeness-mutation-<shard_id>-<run_id>/<recipe_id>/mutation_result.json``
exists and parses as a recipe result (through ``mutation.load_result``) naming that shard and
recipe. A terminal run with no such result is named in the output and leaves the recipe missing.

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
from .mutation import RESULT_FILE_NAME, MutationError, Shard, load_result, load_shards

# Exit code for a document the check cannot read: malformed is never "no runs".
EXIT_INVALID_INPUT = 2
EXIT_MISSING = 1

DEFAULT_BRANCH = "develop"
DEFAULT_WINDOW_HOURS = 24

# The nightly workflow's artifact name per shard and run; the workflow test asserts the two agree,
# and `recipe_result_path` is the only place the download layout is spelled.
ARTIFACT_NAME_TEMPLATE = "type-completeness-mutation-{shard_id}-{run_id}"


def download_command(run_id: int, shard_id: str, artifacts: Path) -> list[str]:
    """The exact `gh run download` invocation that produces the layout `recipe_result_path` reads."""
    artifact = ARTIFACT_NAME_TEMPLATE.format(shard_id=shard_id, run_id=run_id)
    return ["gh", "run", "download", str(run_id), "-n", artifact, "-D", str(artifacts / artifact)]


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


def load_shards_file(path: Path) -> list[Shard]:
    data = _read_json(path, "shards")
    if not isinstance(data, dict):
        raise CadenceError(f"shards {path} must contain a JSON object")
    try:
        return load_shards(data, path.parent)
    except ValueError as error:
        raise CadenceError(str(error)) from error


def recipe_result_path(artifacts: Path, shard_id: str, run_id: int, recipe_id: str) -> Path:
    # `download_command` extracts the artifact into <artifacts>/<artifact>; the artifact root is the
    # workflow's results/ directory, whose layout is results/<recipe_id>/mutation_result.json.
    artifact_root = Path(download_command(run_id, shard_id, artifacts)[-1])
    return artifact_root / recipe_id / RESULT_FILE_NAME


def load_recipe_result(path: Path, shard_id: str, recipe_id: str) -> dict[str, Any]:
    """The recipe's result file as the runner wrote it; anything else is a named error, never a verdict."""
    data = _read_json(path, "mutation result")
    if not isinstance(data, Mapping):
        raise CadenceError(f"mutation result {path} must contain a JSON object")
    try:
        result = load_result(data, str(path))
    except MutationError as error:
        raise CadenceError(str(error)) from error
    if result["shard_id"] != shard_id or result["recipe_id"] != recipe_id:
        raise CadenceError(
            f"{path}: result names shard {result['shard_id']!r} recipe {result['recipe_id']!r}; "
            f"expected {shard_id!r} {recipe_id!r}"
        )
    return result


@dataclass(frozen=True)
class CadenceVerdict:
    # (shard_id, recipe_id) -> the run whose artifact holds the recipe's terminal result.
    covered: dict[tuple[str, str], WorkflowRun]
    missing: list[tuple[str, str]]
    # Terminal runs in the window that could not vouch for a recipe, with the reason.
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
    artifacts: Path,
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
    covered: dict[tuple[str, str], WorkflowRun] = {}
    missing: list[tuple[str, str]] = []
    unattributed: list[tuple[WorkflowRun, str]] = []
    for shard in shards:
        for recipe_id in shard.recipes:
            for run in terminal:
                path = recipe_result_path(artifacts, shard.shard_id, run.run_id, recipe_id)
                if not path.is_file():
                    unattributed.append((run, f"no result for shard {shard.shard_id!r} recipe {recipe_id!r} at {path}"))
                    continue
                # A malformed result is a named error for the whole check, never "no result".
                load_recipe_result(path, shard.shard_id, recipe_id)
                covered[(shard.shard_id, recipe_id)] = run
                break
            else:
                missing.append((shard.shard_id, recipe_id))
    return CadenceVerdict(covered, missing, unattributed)


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(prog="cadence")
    commands = parser.add_subparsers(dest="command", required=True)
    check = commands.add_parser("check", help="Fail unless every recipe has a terminal result inside the window.")
    check.add_argument("--window-hours", type=int, default=DEFAULT_WINDOW_HOURS)
    check.add_argument(
        "--runs-json",
        required=True,
        help="Captured `gh run list --json databaseId,conclusion,createdAt,headBranch`.",
    )
    check.add_argument("--shards", required=True, help="The tracked mutations/shards.json.")
    check.add_argument(
        "--artifacts",
        required=True,
        help="Directory each run's shard artifacts were downloaded into with `gh run download <run_id> -n <artifact> -D <dir>/<artifact>`.",
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
        artifacts = Path(arguments.artifacts)
        if not artifacts.is_dir():
            raise CadenceError(f"--artifacts {artifacts} is not a directory")
        verdict = check_cadence(runs, shards, now, timedelta(hours=arguments.window_hours), arguments.branch, artifacts)
    except CadenceError as error:
        print(f"error: {error}", file=sys.stderr)
        return EXIT_INVALID_INPUT
    for (shard_id, recipe_id), run in sorted(verdict.covered.items()):
        print(
            f"ok shard={shard_id} recipe={recipe_id} run={run.run_id} conclusion={run.conclusion} "
            f"created_at={run.raw['createdAt']}"
        )
    for run, reason in verdict.unattributed:
        print(f"unattributed run={run.run_id} conclusion={run.conclusion} created_at={run.raw['createdAt']}: {reason}")
    for shard_id, recipe_id in verdict.missing:
        print(
            f"missing shard={shard_id} recipe={recipe_id}: no terminal mutation result within "
            f"{arguments.window_hours}h on {arguments.branch}"
        )
    return EXIT_MISSING if verdict.missing else 0


if __name__ == "__main__":
    sys.exit(main())
