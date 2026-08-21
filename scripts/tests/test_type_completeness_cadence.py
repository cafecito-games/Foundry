#!/usr/bin/env python3
"""Unit tests for scripts/type_completeness/cadence.py.

Run with: python3 -m unittest discover -s scripts/tests -p "test_type_completeness_*.py"
"""

from __future__ import annotations

import io
import json
import tempfile
import unittest
import unittest.mock
from datetime import datetime, timedelta, timezone
from pathlib import Path
from typing import Any

from test_type_completeness_comparator import _load

deadline = _load("deadline")
cadence = _load("cadence")

FIXTURES = Path(__file__).resolve().parent / "fixtures" / "type_completeness"
REPO_ROOT = Path(__file__).resolve().parents[2]
TRACKED_SHARDS = REPO_ROOT / "modules" / "foundry_script" / "tests" / "type_completeness" / "mutations" / "shards.json"

# The newest run in the captured `gh run list` JSON; a window anchored shortly after it contains it.
NEWEST_CAPTURED_RUN = "2026-08-16T08:05:19Z"


def _write(root: Path, name: str, payload: Any) -> Path:
    path = root / name
    path.write_text(json.dumps(payload), encoding="utf-8")
    return path


def _shards(*shard_ids: str) -> dict[str, Any]:
    return {
        "schema_version": 1,
        "shards": [{"shard_id": shard_id, "recipes": ["recipe_a"], "budget_seconds": 600} for shard_id in shard_ids],
    }


def _run(conclusion: Any, created_at: str, run_id: int = 1, branch: str = "develop") -> dict[str, Any]:
    return {"databaseId": run_id, "conclusion": conclusion, "createdAt": created_at, "headBranch": branch}


def _check(root: Path, runs: Any, shards: Any, now: str, extra: list[str] | None = None) -> tuple[int, str]:
    runs_path = _write(root, "runs.json", runs)
    shards_path = _write(root, "shards.json", shards)
    _write(root, "recipe_a.json", {})
    out = io.StringIO()
    with unittest.mock.patch("sys.stdout", out):
        code = cadence.main(
            ["check", "--window-hours", "24", "--runs-json", str(runs_path), "--shards", str(shards_path), "--now", now]
            + (extra or [])
        )
    return code, out.getvalue()


class CaptureFixtureTests(unittest.TestCase):
    def test_the_captured_gh_run_list_is_read_through_parse_timestamp(self) -> None:
        runs = cadence.load_runs_file(FIXTURES / "gh_run_list.json")
        self.assertTrue(runs)
        for run in runs:
            self.assertEqual(run.created_at, deadline.parse_timestamp(run.raw["createdAt"]))
            self.assertEqual(run.created_at.tzinfo, timezone.utc)

    def test_complete_fixture_passes_when_the_window_contains_a_terminal_run(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            runs = json.loads((FIXTURES / "gh_run_list.json").read_text(encoding="utf-8"))
            code, output = _check(Path(directory), runs, _shards("union_core"), "2026-08-16T20:00:00Z")
            self.assertEqual(code, 0, output)
            self.assertIn("ok", output)

    def test_missing_shard_fixture_exits_1_with_one_line_per_shard(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            runs = json.loads((FIXTURES / "gh_run_list.json").read_text(encoding="utf-8"))
            code, output = _check(Path(directory), runs, _shards("union_core", "second"), "2026-08-18T20:00:00Z")
            self.assertEqual(code, 1)
            lines = [line for line in output.splitlines() if line.startswith("missing")]
            self.assertEqual(len(lines), 2)
            self.assertTrue(any("union_core" in line for line in lines))
            self.assertTrue(any("second" in line for line in lines))

    def test_tracked_shards_file_is_readable_by_the_check(self) -> None:
        shards = cadence.load_shards_file(TRACKED_SHARDS)
        self.assertTrue(shards)


class WindowTests(unittest.TestCase):
    def test_only_success_and_failure_are_terminal(self) -> None:
        for conclusion, expected in (
            ("success", 0),
            ("failure", 0),
            ("cancelled", 1),
            ("skipped", 1),
            (None, 1),
            ("timed_out", 1),
            ("action_required", 1),
        ):
            with self.subTest(conclusion=conclusion), tempfile.TemporaryDirectory() as directory:
                runs = [_run(conclusion, "2026-08-20T10:00:00Z")]
                code, _ = _check(Path(directory), runs, _shards("union_core"), "2026-08-20T12:00:00Z")
                self.assertEqual(code, expected)

    def test_every_known_conclusion_is_classified(self) -> None:
        for conclusion in cadence.KNOWN_CONCLUSIONS:
            self.assertEqual(conclusion in cadence.TERMINAL_CONCLUSIONS, conclusion in ("success", "failure"))
        self.assertFalse(cadence.is_terminal(None))

    def test_an_unknown_conclusion_is_a_named_error(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            code, output = _check(
                Path(directory), [_run("mystery", "2026-08-20T10:00:00Z")], _shards("a"), "2026-08-20T12:00:00Z"
            )
            self.assertEqual(code, cadence.EXIT_INVALID_INPUT)

    def test_a_terminal_run_older_than_the_window_does_not_count(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            runs = [_run("success", "2026-08-19T11:59:00Z")]
            code, _ = _check(Path(directory), runs, _shards("a"), "2026-08-20T12:00:00Z")
            self.assertEqual(code, 1)

    def test_a_terminal_run_exactly_at_the_window_edge_counts(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            runs = [_run("success", "2026-08-19T12:00:00Z")]
            code, _ = _check(Path(directory), runs, _shards("a"), "2026-08-20T12:00:00Z")
            self.assertEqual(code, 0)

    def test_a_run_in_the_future_does_not_count(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            runs = [_run("success", "2026-08-20T12:00:01Z")]
            code, _ = _check(Path(directory), runs, _shards("a"), "2026-08-20T12:00:00Z")
            self.assertEqual(code, 1)

    def test_runs_on_other_branches_are_ignored(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            runs = [_run("success", "2026-08-20T10:00:00Z", branch="feature")]
            code, _ = _check(Path(directory), runs, _shards("a"), "2026-08-20T12:00:00Z")
            self.assertEqual(code, 1)
            code, _ = _check(Path(directory), runs, _shards("a"), "2026-08-20T12:00:00Z", ["--branch", "feature"])
            self.assertEqual(code, 0)

    def test_offsets_other_than_z_are_normalized_to_utc(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            runs = [_run("success", "2026-08-20T06:00:00-04:00")]
            code, _ = _check(Path(directory), runs, _shards("a"), "2026-08-20T12:00:00Z")
            self.assertEqual(code, 0)

    def test_default_now_is_the_current_utc_time(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            recent = datetime.now(timezone.utc) - timedelta(hours=1)
            runs_path = _write(root, "runs.json", [_run("success", deadline.format_utc_timestamp(recent))])
            shards_path = _write(root, "shards.json", _shards("a"))
            _write(root, "recipe_a.json", {})
            code = cadence.main(
                ["check", "--window-hours", "24", "--runs-json", str(runs_path), "--shards", str(shards_path)]
            )
            self.assertEqual(code, 0)


class MalformedInputTests(unittest.TestCase):
    def test_missing_created_at_is_exit_2_not_no_runs(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            runs = [{"databaseId": 1, "conclusion": "success", "headBranch": "develop"}]
            code, output = _check(Path(directory), runs, _shards("a"), "2026-08-20T12:00:00Z")
            self.assertEqual(code, cadence.EXIT_INVALID_INPUT)
            self.assertNotIn("missing", output)

    def test_unparsable_timestamp_is_exit_2(self) -> None:
        for bad in ("2026-08-20 10:00:00", "2026-08-20T10:00:00", "yesterday", 17, None):
            with self.subTest(created_at=bad), tempfile.TemporaryDirectory() as directory:
                runs = [_run("success", "x")]
                runs[0]["createdAt"] = bad
                code, _ = _check(Path(directory), runs, _shards("a"), "2026-08-20T12:00:00Z")
                self.assertEqual(code, cadence.EXIT_INVALID_INPUT)

    def test_non_array_runs_document_is_exit_2(self) -> None:
        payloads: tuple[Any, ...] = ({}, "runs", 3)
        for payload in payloads:
            with self.subTest(payload=payload), tempfile.TemporaryDirectory() as directory:
                code, _ = _check(Path(directory), payload, _shards("a"), "2026-08-20T12:00:00Z")
                self.assertEqual(code, cadence.EXIT_INVALID_INPUT)

    def test_unreadable_runs_file_is_exit_2(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            runs_path = root / "runs.json"
            runs_path.write_text("{not json", encoding="utf-8")
            shards_path = _write(root, "shards.json", _shards("a"))
            _write(root, "recipe_a.json", {})
            code = cadence.main(
                [
                    "check",
                    "--window-hours",
                    "24",
                    "--runs-json",
                    str(runs_path),
                    "--shards",
                    str(shards_path),
                    "--now",
                    "2026-08-20T12:00:00Z",
                ]
            )
            self.assertEqual(code, cadence.EXIT_INVALID_INPUT)
            code = cadence.main(
                [
                    "check",
                    "--window-hours",
                    "24",
                    "--runs-json",
                    str(root / "absent.json"),
                    "--shards",
                    str(shards_path),
                    "--now",
                    "2026-08-20T12:00:00Z",
                ]
            )
            self.assertEqual(code, cadence.EXIT_INVALID_INPUT)

    def test_an_empty_run_list_is_missing_not_an_error(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            code, output = _check(Path(directory), [], _shards("a"), "2026-08-20T12:00:00Z")
            self.assertEqual(code, 1)
            self.assertIn("missing", output)

    def test_missing_run_members_are_errors(self) -> None:
        for member in ("databaseId", "conclusion", "headBranch"):
            with self.subTest(member=member), tempfile.TemporaryDirectory() as directory:
                run = _run("success", "2026-08-20T10:00:00Z")
                del run[member]
                code, _ = _check(Path(directory), [run], _shards("a"), "2026-08-20T12:00:00Z")
                self.assertEqual(code, cadence.EXIT_INVALID_INPUT)

    def test_unparsable_now_and_bad_window_are_exit_2(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            code, _ = _check(Path(directory), [_run("success", "2026-08-20T10:00:00Z")], _shards("a"), "noon")
            self.assertEqual(code, cadence.EXIT_INVALID_INPUT)
            code, _ = _check(
                Path(directory),
                [_run("success", "2026-08-20T10:00:00Z")],
                _shards("a"),
                "2026-08-20T12:00:00Z",
                ["--window-hours", "0"],
            )
            self.assertEqual(code, cadence.EXIT_INVALID_INPUT)

    def test_malformed_shards_file_is_exit_2(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            code, _ = _check(
                Path(directory),
                [_run("success", "2026-08-20T10:00:00Z")],
                {"schema_version": 1, "shards": []},
                "2026-08-20T12:00:00Z",
            )
            self.assertEqual(code, cadence.EXIT_INVALID_INPUT)


if __name__ == "__main__":
    unittest.main()
