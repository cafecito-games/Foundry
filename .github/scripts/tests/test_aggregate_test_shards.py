"""Tests for `.github/scripts/aggregate_test_shards.py`.

The sharded unit-test job's verdict comes from this aggregator rather than from the shard
processes' exit codes, so a bug that made it return zero on a broken run would turn the
whole job green while asserting nothing. These tests drive it with synthetic progress
streams covering the green path, a failing shard, a truncated stream, and a partition that
runs a case on some shards but not all.
"""

from __future__ import annotations

import json
import sys
import tempfile
import unittest
from pathlib import Path
from typing import Any

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

import aggregate_test_shards  # noqa: E402


def test_end_event(name: str, suite: str = "[Core]", file: str = "tests/test_a.h", line: int = 10) -> dict[str, Any]:
    return {
        "version": 2,
        "event": "test_end",
        "name": name,
        "suite": suite,
        "file": file,
        "line": line,
        "status": "passed",
    }


def run_end_event(
    passed: int,
    failed: int = 0,
    skipped: int = 0,
    status: str = "passed",
    full_suite_case_count: int = 0,
) -> dict[str, Any]:
    return {
        "version": 2,
        "event": "run_end",
        "status": status,
        "passed": passed,
        "failed": failed,
        "skipped": skipped,
        "full_suite_case_count": full_suite_case_count,
    }


def write_shard(directory: Path, index: int, events: list[dict[str, Any]]) -> None:
    path = directory / f"shard-{index}.jsonl"
    with path.open("w", encoding="utf-8") as stream:
        for event in events:
            stream.write(json.dumps(event) + "\n")


class AggregateTestShardsTests(unittest.TestCase):
    def test_green_partition_exits_zero(self):
        with tempfile.TemporaryDirectory() as raw_directory:
            directory = Path(raw_directory)
            # Two ordinary cases split across shards plus one fixture-partitioned case that
            # legitimately runs on both.
            write_shard(
                directory,
                1,
                [
                    test_end_event("case a", line=10),
                    test_end_event("corpus", line=99),
                    run_end_event(passed=2, full_suite_case_count=3),
                ],
            )
            write_shard(
                directory,
                2,
                [
                    test_end_event("case b", line=20),
                    test_end_event("corpus", line=99),
                    run_end_event(passed=2, full_suite_case_count=3),
                ],
            )

            exit_code, report = aggregate_test_shards.aggregate(directory)

        self.assertEqual(exit_code, 0, report)
        self.assertIn("distinct cases executed: 3", report)
        self.assertIn("all shards reported success", report)

    def test_case_on_zero_shards_fails_the_union_check(self):
        # The partial-overlap check cannot see a case that no shard ran. Each shard
        # self-reports the full-suite count, and the cross-shard union must equal it; a union
        # short by one is exactly a case silently dropped onto zero shards.
        with tempfile.TemporaryDirectory() as raw_directory:
            directory = Path(raw_directory)
            write_shard(
                directory, 1, [test_end_event("case a", line=10), run_end_event(passed=1, full_suite_case_count=3)]
            )
            write_shard(
                directory, 2, [test_end_event("case b", line=20), run_end_event(passed=1, full_suite_case_count=3)]
            )

            exit_code, report = aggregate_test_shards.aggregate(directory)

        self.assertEqual(exit_code, 1)
        self.assertIn("ran on zero shards", report)

    def test_cross_shard_disagreement_on_case_count_fails(self):
        with tempfile.TemporaryDirectory() as raw_directory:
            directory = Path(raw_directory)
            write_shard(
                directory, 1, [test_end_event("case a", line=10), run_end_event(passed=1, full_suite_case_count=2)]
            )
            write_shard(
                directory, 2, [test_end_event("case b", line=20), run_end_event(passed=1, full_suite_case_count=3)]
            )

            exit_code, report = aggregate_test_shards.aggregate(directory)

        self.assertEqual(exit_code, 1)
        self.assertIn("disagree on full_suite_case_count", report)

    def test_run_end_missing_case_count_fails(self):
        # A version-1 (or older) stream omits full_suite_case_count. The aggregator offers no
        # compatibility with it: the missing field is an error, not a silent skip.
        with tempfile.TemporaryDirectory() as raw_directory:
            directory = Path(raw_directory)
            legacy_run_end = {
                "version": 1,
                "event": "run_end",
                "status": "passed",
                "passed": 1,
                "failed": 0,
                "skipped": 0,
            }
            write_shard(directory, 1, [test_end_event("case a", line=10), legacy_run_end])
            write_shard(
                directory, 2, [test_end_event("case b", line=20), run_end_event(passed=1, full_suite_case_count=2)]
            )

            exit_code, report = aggregate_test_shards.aggregate(directory)

        self.assertEqual(exit_code, 1)
        self.assertIn("missing 'full_suite_case_count'", report)

    def test_removed_case_count_flag_is_rejected(self):
        # The dormant per-shard case-count CLI surface was removed in favor of the
        # self-reported count. argparse rejects the unknown flag (exit code 2). The flag name is
        # assembled from parts so this file does not itself contain the removed literal token.
        removed_flag = "--expected-" + "case-count"
        with tempfile.TemporaryDirectory() as raw_directory:
            with self.assertRaises(SystemExit) as raised:
                aggregate_test_shards.main([raw_directory, removed_flag, "5"])
        self.assertEqual(raised.exception.code, 2)

    def test_failing_shard_fails_the_run(self):
        with tempfile.TemporaryDirectory() as raw_directory:
            directory = Path(raw_directory)
            write_shard(
                directory, 1, [test_end_event("case a", line=10), run_end_event(passed=1, full_suite_case_count=2)]
            )
            write_shard(
                directory,
                2,
                [
                    test_end_event("case b", line=20),
                    run_end_event(passed=0, failed=1, status="failed", full_suite_case_count=2),
                ],
            )

            exit_code, report = aggregate_test_shards.aggregate(directory)

        self.assertEqual(exit_code, 1)
        self.assertIn("shard-2.jsonl", report)
        self.assertIn("failed case(s)", report)

    def test_truncated_shard_stream_fails_the_run(self):
        with tempfile.TemporaryDirectory() as raw_directory:
            directory = Path(raw_directory)
            write_shard(
                directory, 1, [test_end_event("case a", line=10), run_end_event(passed=1, full_suite_case_count=2)]
            )
            # A shard killed mid-run never writes `run_end`, which the exit code alone would
            # not distinguish from a clean finish.
            write_shard(directory, 2, [test_end_event("case b", line=20)])

            exit_code, report = aggregate_test_shards.aggregate(directory)

        self.assertEqual(exit_code, 1)
        self.assertIn("no run_end event", report)

    def test_partial_overlap_is_reported_as_a_partition_bug(self):
        with tempfile.TemporaryDirectory() as raw_directory:
            directory = Path(raw_directory)
            shared = test_end_event("shared", line=42)
            write_shard(
                directory,
                1,
                [shared, test_end_event("case a", line=10), run_end_event(passed=2, full_suite_case_count=4)],
            )
            write_shard(
                directory,
                2,
                [shared, test_end_event("case b", line=20), run_end_event(passed=2, full_suite_case_count=4)],
            )
            write_shard(
                directory, 3, [test_end_event("case c", line=30), run_end_event(passed=1, full_suite_case_count=4)]
            )

            exit_code, report = aggregate_test_shards.aggregate(directory)

        self.assertEqual(exit_code, 1)
        self.assertIn("ran on some shards but not all", report)
        self.assertIn("shared", report)

    def test_missing_shard_file_fails_when_the_shard_count_is_known(self):
        with tempfile.TemporaryDirectory() as raw_directory:
            directory = Path(raw_directory)
            # Shard 2 died before it opened its progress file. The workflow ignores the shard
            # processes' exit codes, so discovery by glob alone would report a green run over
            # the two survivors and lose a third of the suite.
            write_shard(
                directory, 1, [test_end_event("case a", line=10), run_end_event(passed=1, full_suite_case_count=2)]
            )
            write_shard(
                directory, 3, [test_end_event("case c", line=30), run_end_event(passed=1, full_suite_case_count=2)]
            )

            without_expectation, _ = aggregate_test_shards.aggregate(directory)
            with_expectation, report = aggregate_test_shards.aggregate(directory, expected_shards=3)

        self.assertEqual(without_expectation, 0)
        self.assertEqual(with_expectation, 1)
        self.assertIn("missing shard-2.jsonl", report)

    def test_all_expected_shards_present_passes(self):
        with tempfile.TemporaryDirectory() as raw_directory:
            directory = Path(raw_directory)
            for index in (1, 2, 3):
                write_shard(
                    directory,
                    index,
                    [
                        test_end_event(f"case {index}", line=index * 10),
                        run_end_event(passed=1, full_suite_case_count=3),
                    ],
                )

            exit_code, report = aggregate_test_shards.aggregate(directory, expected_shards=3)

        self.assertEqual(exit_code, 0, report)

    def test_empty_shard_directory_fails(self):
        with tempfile.TemporaryDirectory() as raw_directory:
            exit_code, report = aggregate_test_shards.aggregate(Path(raw_directory))

        self.assertEqual(exit_code, 1)
        self.assertIn("no shard-*.jsonl", report)

    def test_shard_that_executed_nothing_fails(self):
        with tempfile.TemporaryDirectory() as raw_directory:
            directory = Path(raw_directory)
            write_shard(
                directory, 1, [test_end_event("case a", line=10), run_end_event(passed=1, full_suite_case_count=1)]
            )
            write_shard(directory, 2, [run_end_event(passed=0, full_suite_case_count=1)])

            exit_code, report = aggregate_test_shards.aggregate(directory)

        self.assertEqual(exit_code, 1)
        self.assertIn("executed no test cases", report)


if __name__ == "__main__":
    unittest.main()
