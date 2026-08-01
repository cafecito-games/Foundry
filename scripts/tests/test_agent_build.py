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

    def test_cache_mode_defaults_by_backend(self) -> None:
        native = agent_build.parse_args([])
        ninja = agent_build.parse_args(["--backend", "ninja"])
        self.assertEqual(agent_build.resolve_compiler_cache(native), "none")
        self.assertEqual(agent_build.resolve_compiler_cache(ninja), "ccache")

    def test_ccache_build_command_disables_scons_cache_and_normalizes_debug_paths(self) -> None:
        args = agent_build.parse_args(["--compiler-cache", "ccache", "--jobs", "2"])
        target = agent_build.BuildTarget("macos", Path("bin/foundry.macos.editor.dev.arm64"), None)
        with mock.patch.object(agent_build, "scons_prefix", return_value=["scons"]):
            command = agent_build.build_command(args, target)
        self.assertIn("c_compiler_launcher=ccache", command)
        self.assertIn("cpp_compiler_launcher=ccache", command)
        self.assertIn("debug_paths_relative=yes", command)
        self.assertIn("cache_path=", command)
        self.assertNotIn(f"cache_path={agent_build.DEFAULT_CACHE_PATH}", command)

    def test_ccache_build_command_keeps_wrapper_cache_policy_after_raw_scons_args(self) -> None:
        args = agent_build.parse_args(
            [
                "--compiler-cache",
                "ccache",
                "--scons-arg",
                "cache_path=/tmp/rogue-scons-cache",
                "--scons-arg",
                "c_compiler_launcher=rogue-cc",
                "--scons-arg",
                "cpp_compiler_launcher=rogue-cxx",
                "--scons-arg",
                "debug_paths_relative=no",
                "--scons-arg",
                "verbose=yes",
            ]
        )
        target = agent_build.BuildTarget("macos", Path("bin/foundry.macos.editor.dev.arm64"), None)
        with mock.patch.object(agent_build, "scons_prefix", return_value=["scons"]):
            command = agent_build.build_command(args, target)

        expected_policy = {
            "cache_path": "cache_path=",
            "c_compiler_launcher": "c_compiler_launcher=ccache",
            "cpp_compiler_launcher": "cpp_compiler_launcher=ccache",
            "debug_paths_relative": "debug_paths_relative=yes",
        }
        for key, expected in expected_policy.items():
            assignments = [argument for argument in command if argument.startswith(f"{key}=")]
            self.assertGreaterEqual(len(assignments), 2)
            self.assertEqual(assignments[-1], expected)
        self.assertIn("verbose=yes", command)

    def test_ccache_environment_is_worktree_normalized(self) -> None:
        args = agent_build.parse_args(
            ["--compiler-cache", "ccache", "--ccache-dir", "/tmp/foundry-ccache", "--ccache-file-clone"]
        )
        with mock.patch.dict(agent_build.os.environ, {}, clear=True):
            env = agent_build.build_environment(args, "ccache", repo_root=Path("/work/Foundry"))
        self.assertEqual(env["CCACHE_DIR"], "/tmp/foundry-ccache")
        self.assertEqual(env["CCACHE_BASEDIR"], "/work/Foundry")
        self.assertEqual(env["CCACHE_NAMESPACE"], "foundry")
        self.assertEqual(env["CCACHE_FILECLONE"], "1")

    def test_ccache_environment_overrides_polluted_cache_settings(self) -> None:
        args = agent_build.parse_args(
            ["--compiler-cache", "ccache", "--ccache-dir", "/tmp/foundry-ccache", "--ccache-file-clone"]
        )
        polluted = {
            "PATH": "/bin",
            "CCACHE": "legacy-ccache",
            "SCONS_CACHE": "/tmp/legacy-scons-cache",
            "SCONS_CACHE_LIMIT": "1024",
            "CCACHE_DIR": "/tmp/wrong-ccache",
            "CCACHE_BASEDIR": "/tmp/wrong-base",
            "CCACHE_NAMESPACE": "wrong",
            "CCACHE_COMPILERCHECK": "none",
            "CCACHE_NOHASHDIR": "1",
            "CCACHE_HARDLINK": "1",
            "CCACHE_NOFILECLONE": "1",
            "CCACHE_COMPRESS": "1",
            "CCACHE_DISABLE": "1",
            "CCACHE_RECACHE": "1",
            "CCACHE_READONLY": "1",
            "CCACHE_READONLY_DIRECT": "1",
            "CCACHE_SLOPPINESS": "time_macros",
            "CCACHE_IGNOREOPTIONS": "-fdebug-prefix-map=*",
            "CCACHE_IGNOREHEADERS": "/tmp/ignored-header.h",
            "CCACHE_COMPILER": "/tmp/rogue-compiler",
            "CCACHE_PREFIX": "rogue-prefix",
            "CCACHE_REMOTE_ONLY": "1",
            "CCACHE_CONFIGPATH": "/tmp/rogue-ccache.conf",
            "CCACHE_UNKNOWN_FUTURE_SETTING": "unsafe",
        }
        with mock.patch.dict(agent_build.os.environ, polluted, clear=True):
            env = agent_build.build_environment(args, "ccache", repo_root=Path("/work/Foundry"))

        self.assertEqual(env["PATH"], "/bin")
        self.assertEqual(env["CCACHE_DIR"], "/tmp/foundry-ccache")
        self.assertEqual(env["CCACHE_BASEDIR"], "/work/Foundry")
        self.assertEqual(env["CCACHE_NAMESPACE"], "foundry")
        self.assertEqual(env["CCACHE_COMPILERCHECK"], "content")
        self.assertEqual(env["CCACHE_HASHDIR"], "1")
        self.assertEqual(env["CCACHE_NOHARDLINK"], "1")
        self.assertEqual(env["CCACHE_FILECLONE"], "1")
        self.assertEqual(env["CCACHE_NOCOMPRESS"], "1")
        self.assertEqual(env["CCACHE_NODISABLE"], "1")
        self.assertEqual(env["CCACHE_NORECACHE"], "1")
        self.assertEqual(env["CCACHE_NOREADONLY"], "1")
        self.assertEqual(env["CCACHE_NOREADONLY_DIRECT"], "1")
        self.assertEqual(env["CCACHE_CONFIGPATH"], agent_build.os.devnull)
        self.assertEqual(
            {key for key in env if key == "CCACHE" or key.startswith("CCACHE_")},
            {
                "CCACHE_DIR",
                "CCACHE_BASEDIR",
                "CCACHE_NAMESPACE",
                "CCACHE_CONFIGPATH",
                "CCACHE_COMPILERCHECK",
                "CCACHE_HASHDIR",
                "CCACHE_NOHARDLINK",
                "CCACHE_FILECLONE",
                "CCACHE_NOCOMPRESS",
                "CCACHE_NODISABLE",
                "CCACHE_NORECACHE",
                "CCACHE_NOREADONLY",
                "CCACHE_NOREADONLY_DIRECT",
            },
        )
        self.assertNotIn("SCONS_CACHE", env)
        self.assertNotIn("SCONS_CACHE_LIMIT", env)

    def test_ccache_environment_uses_compression_without_file_cloning(self) -> None:
        args = agent_build.parse_args(["--compiler-cache", "ccache"])
        polluted = {
            "CCACHE_FILECLONE": "1",
            "CCACHE_NOCOMPRESS": "1",
        }
        with mock.patch.dict(agent_build.os.environ, polluted, clear=True):
            env = agent_build.build_environment(args, "ccache")
        self.assertEqual(env["CCACHE_NOFILECLONE"], "1")
        self.assertEqual(env["CCACHE_COMPRESS"], "1")
        self.assertNotIn("CCACHE_FILECLONE", env)
        self.assertNotIn("CCACHE_NOCOMPRESS", env)

    def test_relative_ccache_directory_becomes_absolute(self) -> None:
        args = agent_build.parse_args(["--compiler-cache", "ccache", "--ccache-dir", "shared/ccache"])
        with mock.patch.dict(agent_build.os.environ, {}, clear=True):
            with mock.patch.object(agent_build.os, "getcwd", return_value="/work"):
                env = agent_build.build_environment(args, "ccache")
        self.assertEqual(env["CCACHE_DIR"], "/work/shared/ccache")

    def test_native_cache_environment_preserves_scons_compatibility(self) -> None:
        args = agent_build.parse_args([])
        polluted = {
            "PATH": "/bin",
            "CCACHE": "legacy-ccache",
            "SCONS_CACHE": "/tmp/legacy-scons-cache",
            "SCONS_CACHE_LIMIT": "1024",
            "CCACHE_DISABLE": "1",
        }
        with mock.patch.dict(agent_build.os.environ, polluted, clear=True):
            env = agent_build.build_environment(args, "none")
        self.assertEqual(
            env,
            {
                "PATH": "/bin",
                "SCONS_CACHE": "/tmp/legacy-scons-cache",
                "SCONS_CACHE_LIMIT": "1024",
                "CCACHE_DISABLE": "1",
            },
        )

    def test_missing_ccache_returns_actionable_failure(self) -> None:
        stderr = io.StringIO()
        with contextlib.redirect_stderr(stderr):
            with mock.patch.object(agent_build, "scons_prefix", return_value=["scons"]):
                with mock.patch.object(agent_build.shutil, "which", return_value=None):
                    exit_code = agent_build.main(["--compiler-cache", "ccache"])
        self.assertEqual(exit_code, 127)
        self.assertEqual(
            stderr.getvalue(),
            "[agent-build] ccache is required for this build mode but was not found in PATH. "
            "Use --backend scons --compiler-cache none for the native fallback.\n",
        )

    def test_main_passes_normalized_ccache_environment_to_build(self) -> None:
        polluted = {
            "PATH": "/bin",
            "CCACHE": "legacy-ccache",
            "SCONS_CACHE": "/tmp/legacy-scons-cache",
            "CCACHE_DISABLE": "1",
        }
        with mock.patch.dict(agent_build.os.environ, polluted, clear=True):
            with mock.patch.object(agent_build, "scons_prefix", return_value=["scons"]):
                with mock.patch.object(agent_build.shutil, "which", return_value="/bin/ccache"):
                    with mock.patch.object(agent_build, "run_logged_command", return_value=23) as run_command:
                        exit_code = agent_build.main(
                            ["--compiler-cache", "ccache", "--ccache-dir", "/tmp/foundry-ccache"]
                        )
        self.assertEqual(exit_code, 23)
        run_command.assert_called_once()
        build_env = run_command.call_args.kwargs["env"]
        self.assertEqual(build_env["CCACHE_DIR"], "/tmp/foundry-ccache")
        self.assertEqual(build_env["CCACHE_BASEDIR"], str(agent_build.REPO_ROOT.resolve()))
        self.assertNotIn("CCACHE", build_env)
        self.assertNotIn("SCONS_CACHE", build_env)

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

    def test_native_build_command_keeps_raw_cache_path_as_effective_override(self) -> None:
        args = agent_build.parse_args(
            ["--jobs", "3", "--scons-arg", "cache_path=/tmp/custom-scons-cache", "--scons-arg", "verbose=yes"]
        )
        target = agent_build.BuildTarget("macos", Path("bin/foundry.macos.editor.dev.arm64"), None)
        with mock.patch.object(agent_build, "scons_prefix", return_value=["scons"]):
            command = agent_build.build_command(args, target)
        cache_assignments = [argument for argument in command if argument.startswith("cache_path=")]
        self.assertEqual(
            cache_assignments,
            [f"cache_path={agent_build.DEFAULT_CACHE_PATH}", "cache_path=/tmp/custom-scons-cache"],
        )
        self.assertIn("verbose=yes", command)


if __name__ == "__main__":
    unittest.main()
