#!/usr/bin/env python3
"""Behavioral tests for scripts/agent_build.py."""

from __future__ import annotations

import contextlib
import importlib.util
import io
import sys
import unittest
from pathlib import Path
from typing import Any
from unittest import mock

_MODULE_PATH = Path(__file__).resolve().parents[1] / "agent_build.py"
_spec = importlib.util.spec_from_file_location("agent_build", _MODULE_PATH)
assert _spec is not None and _spec.loader is not None
agent_build: Any = importlib.util.module_from_spec(_spec)
sys.modules[_spec.name] = agent_build
_spec.loader.exec_module(agent_build)


class AgentBuildCharacterizationTests(unittest.TestCase):
    def test_worktree_identity_is_stable_and_path_specific(self) -> None:
        first = agent_build.worktree_identity(Path("/work/Foundry"))
        again = agent_build.worktree_identity(Path("/work/Foundry"))
        other = agent_build.worktree_identity(Path("/work/.worktrees/issue-1"))
        same_name_elsewhere = agent_build.worktree_identity(Path("/other/Foundry"))
        self.assertEqual(first, again)
        self.assertNotEqual(first, other)
        self.assertNotEqual(first, same_name_elsewhere)
        self.assertRegex(first, r"^Foundry-[0-9a-f]{10}$")

    def test_worktree_identity_is_bounded_and_filename_safe(self) -> None:
        unusual_name = "Rélease build \t\n:*?" + "x" * 220
        paths = agent_build.default_output_paths(Path("/work") / unusual_name, Path("/tmp"))
        for path in paths:
            self.assertTrue(path.name.isascii())
            self.assertLessEqual(len(path.name.encode("ascii")), 160)
            self.assertRegex(path.name, r"^[A-Za-z0-9._-]+$")

    def test_default_output_paths_do_not_collide_between_worktrees(self) -> None:
        first = agent_build.default_output_paths(Path("/work/Foundry"), Path("/tmp"))
        second = agent_build.default_output_paths(Path("/work/.worktrees/issue-1"), Path("/tmp"))
        self.assertNotEqual(first.log, second.log)
        self.assertNotEqual(first.progress, second.progress)
        self.assertEqual(first.log.parent, Path("/tmp"))
        self.assertEqual(first.progress.suffix, ".jsonl")

    def test_explicit_output_paths_still_win(self) -> None:
        args = agent_build.parse_args(["--log", "/tmp/custom.log", "--progress-file", "/tmp/custom.jsonl"])
        self.assertEqual(args.log, Path("/tmp/custom.log"))
        self.assertEqual(args.progress_file, Path("/tmp/custom.jsonl"))

    def test_default_output_paths_are_used_and_visible_in_help(self) -> None:
        args = agent_build.parse_args([])
        self.assertEqual(args.log, agent_build.DEFAULT_LOG)
        self.assertEqual(args.progress_file, agent_build.DEFAULT_PROGRESS_LOG)

        output = io.StringIO()
        with contextlib.redirect_stdout(output), self.assertRaises(SystemExit) as raised:
            agent_build.parse_args(["--help"])
        self.assertEqual(raised.exception.code, 0)
        help_text = "".join(output.getvalue().split())
        self.assertIn(str(agent_build.DEFAULT_LOG), help_text)
        self.assertIn(str(agent_build.DEFAULT_PROGRESS_LOG), help_text)

    def test_format_duration(self) -> None:
        self.assertEqual(agent_build.format_duration(0), "0s")
        self.assertEqual(agent_build.format_duration(65), "1m05s")
        self.assertEqual(agent_build.format_duration(3661), "1h01m01s")

    def test_normalize_arch(self) -> None:
        self.assertEqual(agent_build.normalize_arch("AMD64"), "x86_64")
        self.assertEqual(agent_build.normalize_arch("aarch64"), "arm64")
        self.assertEqual(agent_build.normalize_arch("arm64"), "arm64")

    def test_case_implies_test(self) -> None:
        args = agent_build.parse_args(["--case", "*FoundryCLI*"])
        self.assertTrue(args.test)
        self.assertEqual(args.test_case, "*FoundryCLI*")

    def test_native_build_command_keeps_strict_defaults(self) -> None:
        args = agent_build.parse_args(["--jobs", "3"])
        target = agent_build.BuildTarget("macos", Path("bin/foundry.macos.editor.dev.arm64"), None)
        with mock.patch.object(agent_build, "scons_prefix", return_value=["scons"]):
            command = agent_build.build_command(args, target)
        self.assertEqual(command[:2], ["scons", "platform=macos"])
        self.assertIn("dev_mode=yes", command)
        self.assertIn("dev_build=yes", command)
        self.assertIn("tests=yes", command)
        self.assertIn(f"cache_path={agent_build.DEFAULT_CACHE_PATH}", command)
        self.assertIn("-j3", command)


if __name__ == "__main__":
    unittest.main()
