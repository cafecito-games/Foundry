#!/usr/bin/env python3
"""Aggregate the JSONL progress streams written by `foundry test run --shard i/n`.

The unit-test job runs the suite as several concurrent shard processes. Their exit codes
are not a usable verdict: a fully green run can still exit non-zero while reporting leaked
objects during cleanup. Each shard therefore writes `--progress-file`, and this script
derives the job's verdict from the structured `run_end` event instead.

Beyond pass/fail it guards the partition itself. Every doctest case must be executed by
exactly one shard, except the cases that loop over a fixture corpus internally: those are
on the run-everywhere allowlist, partition their own fixtures, and therefore appear on
every shard. A case that shows up on more than one shard but not on all of them means the
allowlist and the partition disagree, which would silently duplicate or drop work.
"""

from __future__ import annotations

import argparse
import json
import sys
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any


class ShardReportError(Exception):
    """A shard's progress stream is missing, unreadable, or incomplete."""


@dataclass
class ShardReport:
    name: str
    passed: int = 0
    failed: int = 0
    skipped: int = 0
    status: str = ""
    full_suite_case_count: int | None = None
    case_identities: set[str] = field(default_factory=set)

    @property
    def ok(self) -> bool:
        return self.status == "passed" and self.failed == 0


def case_identity(event: dict[str, Any]) -> str:
    """Stable identity of a doctest case across shards."""
    return "{}|{}|{}:{}".format(
        event.get("suite", ""),
        event.get("name", ""),
        event.get("file", ""),
        event.get("line", ""),
    )


def read_shard_report(path: Path) -> ShardReport:
    report = ShardReport(name=path.name)
    run_end_seen = False
    with path.open(encoding="utf-8") as stream:
        for line_number, line in enumerate(stream, start=1):
            line = line.strip()
            if not line:
                continue
            try:
                event = json.loads(line)
            except json.JSONDecodeError as error:
                raise ShardReportError(f"{path}:{line_number}: malformed JSONL: {error}") from error
            kind = event.get("event")
            if kind == "test_end":
                report.case_identities.add(case_identity(event))
            elif kind == "run_end":
                if run_end_seen:
                    raise ShardReportError(f"{path}: more than one run_end event")
                run_end_seen = True
                report.status = str(event.get("status", ""))
                report.passed = int(event.get("passed", 0))
                report.failed = int(event.get("failed", 0))
                report.skipped = int(event.get("skipped", 0))
                # Each shard self-reports the full-suite case count (the size of the whole
                # filtered selection, identical on every shard). A stream without it is a
                # version-1 (or older) stream; the aggregator offers no compatibility with it.
                if "full_suite_case_count" not in event:
                    raise ShardReportError(
                        f"{path}: run_end is missing 'full_suite_case_count'; the shard wrote a "
                        "version-1 (or older) progress stream. Rebuild and rerun this shard."
                    )
                report.full_suite_case_count = int(event["full_suite_case_count"])
    if not run_end_seen:
        raise ShardReportError(f"{path}: no run_end event; the shard did not finish")
    return report


def collect_shard_reports(directory: Path, expected_shards: int | None = None) -> list[ShardReport]:
    if expected_shards is not None:
        # Discovery alone cannot tell a shard that never started from a shard that was never
        # asked for. The workflow ignores the shard processes' exit codes on purpose, so a
        # shard killed before it opened its progress file would otherwise just disappear and
        # leave the surviving shards to report a green run over a fraction of the suite.
        paths = [directory / f"shard-{index}.jsonl" for index in range(1, expected_shards + 1)]
        missing = [path.name for path in paths if not path.is_file()]
        if missing:
            raise ShardReportError(
                "{}: expected {} shard progress file(s), missing {}".format(
                    directory, expected_shards, ", ".join(missing)
                )
            )
    else:
        paths = sorted(directory.glob("shard-*.jsonl"))
        if not paths:
            raise ShardReportError(f"{directory}: no shard-*.jsonl progress files found")
    return [read_shard_report(path) for path in paths]


def find_partition_problems(reports: list[ShardReport]) -> list[str]:
    """Cases executed by more than one shard but not by all of them."""
    if len(reports) < 2:
        return []
    occurrences: dict[str, int] = {}
    for report in reports:
        for identity in report.case_identities:
            occurrences[identity] = occurrences.get(identity, 0) + 1
    return sorted(identity for identity, count in occurrences.items() if 1 < count < len(reports))


def format_table(reports: list[ShardReport]) -> str:
    width = max((len(report.name) for report in reports), default=5)
    lines = [f"{'shard'.ljust(width)}  status   passed  failed  skipped"]
    for report in reports:
        lines.append(
            "{}  {}  {:>6}  {:>6}  {:>7}".format(
                report.name.ljust(width),
                (report.status or "missing").ljust(7),
                report.passed,
                report.failed,
                report.skipped,
            )
        )
    return "\n".join(lines)


def aggregate(
    directory: Path, expected_shards: int | None = None
) -> tuple[int, str]:
    """Returns the process exit code and the report to print."""
    try:
        reports = collect_shard_reports(directory, expected_shards)
    except ShardReportError as error:
        return 1, str(error)

    lines = [format_table(reports)]
    failures = []

    for report in reports:
        if not report.ok:
            failures.append(
                f"{report.name}: reported status '{report.status or 'missing'}' with {report.failed} failed case(s)"
            )
        if report.passed == 0 and report.failed == 0:
            failures.append(f"{report.name}: executed no test cases")

    problems = find_partition_problems(reports)
    if problems:
        failures.append(
            "{} case(s) ran on some shards but not all; the run-everywhere allowlist and the "
            "case partition disagree:\n  {}".format(len(problems), "\n  ".join(problems[:20]))
        )

    distinct_cases: set[str] = set()
    for report in reports:
        distinct_cases.update(report.case_identities)
    lines.append(f"distinct cases executed: {len(distinct_cases)}")

    # Every shard self-reports the size of the whole filtered selection. They must agree,
    # and the cross-shard union of executed cases must equal that count. The union check is
    # the only thing that catches a case selected by no shard (a partition regression or a
    # suite silently dropped from registration); the partial-overlap check above cannot see
    # a case on zero shards.
    reported_counts = {report.full_suite_case_count for report in reports}
    if len(reported_counts) == 1:
        expected_count = next(iter(reported_counts))
        if expected_count is not None and len(distinct_cases) != expected_count:
            failures.append(
                f"each shard reported {expected_count} case(s) in the full suite but the shards "
                f"together executed {len(distinct_cases)} distinct case(s); a case ran on zero "
                "shards (partition regression), a suite dropped out of registration, or a shard "
                "died mid-run."
            )
    elif len(reported_counts) > 1:
        failures.append(
            "shards disagree on full_suite_case_count: "
            + ", ".join(f"{r.name}={r.full_suite_case_count}" for r in reports)
        )

    if failures:
        lines.append("")
        lines.extend(f"error: {failure}" for failure in failures)
        return 1, "\n".join(lines)

    lines.append("all shards reported success")
    return 0, "\n".join(lines)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path, help="Directory holding shard-<i>.jsonl progress files.")
    parser.add_argument(
        "--expected-shards",
        type=int,
        default=None,
        help="Number of shards the run launched; fail unless every shard-<i>.jsonl is present.",
    )
    arguments = parser.parse_args(argv)
    exit_code, report = aggregate(arguments.directory, arguments.expected_shards)
    print(report)
    return exit_code


if __name__ == "__main__":
    sys.exit(main())
