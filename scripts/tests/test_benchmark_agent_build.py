#!/usr/bin/env python3
"""Behavioral tests for scripts/benchmark_agent_build.py."""

from __future__ import annotations

import contextlib
import importlib.util
import io
import json
import sys
import tempfile
import unittest
from pathlib import Path
from typing import Any
from unittest import mock

_MODULE_PATH = Path(__file__).resolve().parents[1] / "benchmark_agent_build.py"
_spec = importlib.util.spec_from_file_location("benchmark_agent_build", _MODULE_PATH)
assert _spec is not None and _spec.loader is not None
benchmark_agent_build: Any = importlib.util.module_from_spec(_spec)
sys.modules[_spec.name] = benchmark_agent_build
_spec.loader.exec_module(benchmark_agent_build)


class BenchmarkAgentBuildTests(unittest.TestCase):
    def test_run_once_collects_metrics_for_a_successful_child(self) -> None:
        result = benchmark_agent_build.run_once([sys.executable, "-c", "print('ok')"], label="smoke", repetition=2)

        self.assertEqual(result.label, "smoke")
        self.assertEqual(result.repetition, 2)
        self.assertEqual(result.exit_code, 0)
        self.assertGreaterEqual(result.wall_ms, 0)
        self.assertGreaterEqual(result.user_ms, 0)
        self.assertGreaterEqual(result.system_ms, 0)

    def test_write_result_appends_valid_jsonl(self) -> None:
        result = self._result("first", 1)
        with tempfile.TemporaryDirectory() as temporary_directory:
            output = Path(temporary_directory) / "nested" / "results.jsonl"
            benchmark_agent_build.write_result(output, result)
            payload = json.loads(output.read_text(encoding="utf-8"))

        self.assertEqual(payload["label"], "first")
        self.assertEqual(payload["command"], ["command", "first"])

    def test_write_result_preserves_prior_records_in_order(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            output = Path(temporary_directory) / "results.jsonl"
            benchmark_agent_build.write_result(output, self._result("first", 1))
            benchmark_agent_build.write_result(output, self._result("second", 2))
            payloads = [json.loads(line) for line in output.read_text(encoding="utf-8").splitlines()]

        self.assertEqual([payload["label"] for payload in payloads], ["first", "second"])

    def test_parse_args_validates_repeat_and_command_separator(self) -> None:
        args = benchmark_agent_build.parse_args(
            ["--label", "smoke", "--output", "/tmp/out.jsonl", "--repeat", "2", "--", "echo", "ok"]
        )

        self.assertEqual(args.repeat, 2)
        self.assertEqual(args.command, ["echo", "ok"])
        with contextlib.redirect_stderr(io.StringIO()), self.assertRaises(SystemExit):
            benchmark_agent_build.parse_args(
                ["--label", "smoke", "--output", "/tmp/out.jsonl", "--repeat", "0", "echo"]
            )
        with contextlib.redirect_stderr(io.StringIO()), self.assertRaises(SystemExit):
            benchmark_agent_build.parse_args(["--label", "smoke", "--output", "/tmp/out.jsonl"])

    def test_main_stops_after_the_first_nonzero_result(self) -> None:
        results = [self._result("smoke", 1, exit_code=0), self._result("smoke", 2, exit_code=9)]
        with mock.patch.object(benchmark_agent_build, "run_once", side_effect=results) as run_once:
            with mock.patch.object(benchmark_agent_build, "write_result") as write_result:
                with contextlib.redirect_stdout(io.StringIO()):
                    exit_code = benchmark_agent_build.main(
                        ["--label", "smoke", "--output", "/tmp/out.jsonl", "--repeat", "3", "echo", "ok"]
                    )

        self.assertEqual(exit_code, 9)
        self.assertEqual(run_once.call_count, 2)
        self.assertEqual(write_result.call_count, 2)
        self.assertEqual([call.args[1].repetition for call in write_result.call_args_list], [1, 2])

    @staticmethod
    def _result(label: str, repetition: int, *, exit_code: int = 0) -> Any:
        return benchmark_agent_build.BenchmarkResult(
            version=1,
            label=label,
            repetition=repetition,
            command=["command", label],
            exit_code=exit_code,
            wall_ms=1.0,
            user_ms=2.0,
            system_ms=3.0,
            max_rss_kib=4,
            input_blocks=5,
            output_blocks=6,
        )
