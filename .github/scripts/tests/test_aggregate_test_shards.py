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

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

import aggregate_test_shards  # noqa: E402


def test_end_event(name: str, suite: str = "[Core]", file: str = "tests/test_a.h", line: int = 10) -> dict:
    return {
        "version": 1,
        "event": "test_end",
        "name": name,
        "suite": suite,
        "file": file,
        "line": line,
        "status": "passed",
    }


def run_end_event(passed: int, failed: int = 0, skipped: int = 0, status: str = "passed") -> dict:
    return {
        "version": 1,
        "event": "run_end",
        "status": status,
        "passed": passed,
        "failed": failed,
        "skipped": skipped,
    }


def write_shard(directory: Path, index: int, events: list[dict]) -> None:
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
                    run_end_event(passed=2),
                ],
            )
            write_shard(
                directory,
                2,
                [
                    test_end_event("case b", line=20),
                    test_end_event("corpus", line=99),
                    run_end_event(passed=2),
                ],
            )

            exit_code, report = aggregate_test_shards.aggregate(directory)

        self.assertEqual(exit_code, 0, report)
        self.assertIn("distinct cases executed: 3", report)
        self.assertIn("all shards reported success", report)

    def test_expected_case_count_mismatch_fails(self):
        with tempfile.TemporaryDirectory() as raw_directory:
            directory = Path(raw_directory)
            write_shard(directory, 1, [test_end_event("case a", line=10), run_end_event(passed=1)])
            write_shard(directory, 2, [test_end_event("case b", line=20), run_end_event(passed=1)])

            matching, _ = aggregate_test_shards.aggregate(directory, expected_case_count=2)
            mismatching, report = aggregate_test_shards.aggregate(directory, expected_case_count=3)

        self.assertEqual(matching, 0)
        self.assertEqual(mismatching, 1)
        self.assertIn("expected 3 distinct case(s)", report)

    def test_failing_shard_fails_the_run(self):
        with tempfile.TemporaryDirectory() as raw_directory:
            directory = Path(raw_directory)
            write_shard(directory, 1, [test_end_event("case a", line=10), run_end_event(passed=1)])
            write_shard(
                directory,
                2,
                [test_end_event("case b", line=20), run_end_event(passed=0, failed=1, status="failed")],
            )

            exit_code, report = aggregate_test_shards.aggregate(directory)

        self.assertEqual(exit_code, 1)
        self.assertIn("shard-2.jsonl", report)
        self.assertIn("failed case(s)", report)

    def test_truncated_shard_stream_fails_the_run(self):
        with tempfile.TemporaryDirectory() as raw_directory:
            directory = Path(raw_directory)
            write_shard(directory, 1, [test_end_event("case a", line=10), run_end_event(passed=1)])
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
            write_shard(directory, 1, [shared, test_end_event("case a", line=10), run_end_event(passed=2)])
            write_shard(directory, 2, [shared, test_end_event("case b", line=20), run_end_event(passed=2)])
            write_shard(directory, 3, [test_end_event("case c", line=30), run_end_event(passed=1)])

            exit_code, report = aggregate_test_shards.aggregate(directory)

        self.assertEqual(exit_code, 1)
        self.assertIn("ran on some shards but not all", report)
        self.assertIn("shared", report)

    def test_empty_shard_directory_fails(self):
        with tempfile.TemporaryDirectory() as raw_directory:
            exit_code, report = aggregate_test_shards.aggregate(Path(raw_directory))

        self.assertEqual(exit_code, 1)
        self.assertIn("no shard-*.jsonl", report)

    def test_shard_that_executed_nothing_fails(self):
        with tempfile.TemporaryDirectory() as raw_directory:
            directory = Path(raw_directory)
            write_shard(directory, 1, [test_end_event("case a", line=10), run_end_event(passed=1)])
            write_shard(directory, 2, [run_end_event(passed=0)])

            exit_code, report = aggregate_test_shards.aggregate(directory)

        self.assertEqual(exit_code, 1)
        self.assertIn("executed no test cases", report)


if __name__ == "__main__":
    unittest.main()
