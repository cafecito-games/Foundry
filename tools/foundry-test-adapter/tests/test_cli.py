# This file is part of Foundry — https://www.cafecito.games/
# Copyright (c) 2026-present Cafecito Games. MIT License.
"""Exercises the validator's command-line surface as downstream repositories use it."""

from __future__ import annotations

import json
import os
import subprocess
import sys
import unittest
from pathlib import Path
from typing import Sequence

from foundry_test_adapter import fixtures_root

TOOL_ROOT = Path(__file__).resolve().parent.parent


def _run(arguments: Sequence[str]) -> tuple[int, str, str]:
    environment = dict(os.environ)
    existing = environment.get("PYTHONPATH", "")
    environment["PYTHONPATH"] = str(TOOL_ROOT) + (os.pathsep + existing if existing else "")
    command: list[str] = [sys.executable, "-m", "foundry_test_adapter"]
    command.extend(arguments)
    completed = subprocess.run(
        command,
        cwd=str(TOOL_ROOT),
        env=environment,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        universal_newlines=True,
    )
    return (completed.returncode, completed.stdout, completed.stderr)


def _fixture(relative_path: str) -> str:
    return str(fixtures_root() / relative_path)


class ValidatorCommandTests(unittest.TestCase):
    def test_a_conforming_artifact_set_exits_zero(self) -> None:
        exit_code, stdout, _ = _run(
            [
                "--capabilities",
                _fixture("capabilities/valid/minimal.json"),
                "--discovery",
                _fixture("run/discovery.jsonl"),
                "--report",
                _fixture("run/report_all.tap"),
            ]
        )
        self.assertEqual(0, exit_code)
        self.assertIn("conforms to Foundry Test Adapter Protocol v1", stdout)

    def test_violations_exit_one_and_are_all_reported(self) -> None:
        exit_code, stdout, _ = _run(["--discovery", _fixture("discovery/invalid/invalid_range.jsonl")])
        self.assertEqual(1, exit_code)
        self.assertIn("invalid_range", stdout)
        self.assertIn("invalid_field_value", stdout)
        self.assertIn("2 violation(s)", stdout)

    def test_selection_correlation_is_reported_against_the_discovery_stream(self) -> None:
        exit_code, stdout, _ = _run(
            [
                "--discovery",
                _fixture("run/discovery.jsonl"),
                "--report",
                _fixture("run/report_unselected.tap"),
                "--select",
                "S1",
            ]
        )
        self.assertEqual(1, exit_code)
        self.assertIn("unexpected_result_id", stdout)
        self.assertIn("missing_result_id", stdout)

    def test_json_output_is_machine_readable(self) -> None:
        exit_code, stdout, _ = _run(["--json", "--report", _fixture("tap/invalid/plan_unsatisfied.tap")])
        self.assertEqual(1, exit_code)
        payload = json.loads(stdout)
        self.assertEqual(1, payload["protocol_version"])
        self.assertFalse(payload["conforms"])
        self.assertEqual(1, payload["violation_count"])
        report = payload["artifacts"]["report"]
        self.assertEqual(3, report["plan"])
        self.assertEqual(1, report["point_count"])
        self.assertFalse(report["complete"])
        self.assertEqual(
            ["plan_unsatisfied"],
            [violation["code"] for violation in report["violations"]],
        )

    def test_no_artifact_is_a_usage_error(self) -> None:
        exit_code, _, stderr = _run([])
        self.assertEqual(2, exit_code)
        self.assertIn("at least one of", stderr)

    def test_an_unreadable_artifact_is_a_usage_error(self) -> None:
        exit_code, _, stderr = _run(["--report", _fixture("tap/valid/does_not_exist.tap")])
        self.assertEqual(2, exit_code)
        self.assertIn("Cannot read", stderr)


if __name__ == "__main__":
    unittest.main()
