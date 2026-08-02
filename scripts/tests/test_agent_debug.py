#!/usr/bin/env python3
"""Behavioral tests for scripts/agent_debug.py."""

from __future__ import annotations

import importlib.util
import sys
import unittest
from pathlib import Path
from typing import Any

_MODULE_PATH = Path(__file__).resolve().parents[1] / "agent_debug.py"
_spec = importlib.util.spec_from_file_location("agent_debug", _MODULE_PATH)
assert _spec is not None and _spec.loader is not None
agent_debug: Any = importlib.util.module_from_spec(_spec)
sys.modules[_spec.name] = agent_debug
_spec.loader.exec_module(agent_debug)


class AgentDebugCaseFilterTests(unittest.TestCase):
    def test_single_case_is_retained_as_a_list(self) -> None:
        args = agent_debug.parse_args(["--case", "*FoundryCLI*"])
        self.assertEqual(args.test_case, ["*FoundryCLI*"])

    def test_repeated_case_retains_every_value_in_order(self) -> None:
        args = agent_debug.parse_args(["--case", "*A*", "--case", "*B*", "--case", "*C*"])
        self.assertEqual(args.test_case, ["*A*", "*B*", "*C*"])

    def test_no_case_leaves_the_filter_unset(self) -> None:
        args = agent_debug.parse_args([])
        self.assertIsNone(args.test_case)

    def test_foundry_args_forward_every_repeated_case_filter_in_order(self) -> None:
        args = agent_debug.parse_args(["--case", "*A*", "--case", "*B*"])
        self.assertEqual(
            agent_debug.foundry_args(args),
            ["--headless", "test", "run", "--case", "*A*", "--case", "*B*", "--force-colors"],
        )

    def test_foundry_args_omit_case_when_no_filter_was_supplied(self) -> None:
        args = agent_debug.parse_args([])
        self.assertEqual(agent_debug.foundry_args(args), ["--headless", "test", "run", "--force-colors"])

    def test_gdb_command_forwards_every_repeated_case_filter(self) -> None:
        args = agent_debug.parse_args(["--case", "*A*", "--case", "*B*", "--binary", "/tmp/foundry"])
        command = agent_debug.gdb_command(args, "/usr/bin/gdb")
        self.assertEqual(
            command,
            [
                "/usr/bin/gdb",
                "-q",
                "--args",
                "/tmp/foundry",
                "--headless",
                "test",
                "run",
                "--case",
                "*A*",
                "--case",
                "*B*",
                "--force-colors",
            ],
        )

    def test_extra_foundry_args_still_follow_the_case_filters(self) -> None:
        args = agent_debug.parse_args(["--case", "*A*", "--case", "*B*", "--", "--quiet"])
        self.assertEqual(
            agent_debug.foundry_args(args),
            ["--headless", "test", "run", "--case", "*A*", "--case", "*B*", "--force-colors", "--quiet"],
        )


if __name__ == "__main__":
    unittest.main()
