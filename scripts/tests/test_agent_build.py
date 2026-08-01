#!/usr/bin/env python3
"""Behavioral tests for scripts/agent_build.py."""

from __future__ import annotations

import contextlib
import importlib.util
import io
import json
import os
import subprocess
import sys
import tempfile
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

    def test_read_ccache_stats_parses_machine_output(self) -> None:
        completed = subprocess.CompletedProcess(
            ["ccache"], 0, stdout='{"cache_hit_direct": 7, "cache_miss": 3}', stderr=""
        )
        stats = agent_build.read_ccache_stats({"PATH": "/bin"}, run=lambda *args, **kwargs: completed)
        self.assertEqual(stats, {"cache_hit_direct": 7, "cache_miss": 3})

    def test_read_ccache_stats_uses_per_invocation_stats_log(self) -> None:
        completed = subprocess.CompletedProcess(["ccache"], 0, stdout="{}", stderr="")
        run = mock.Mock(return_value=completed)
        env = {"PATH": "/bin", "CCACHE_STATSLOG": "/tmp/invocation.stats"}

        self.assertEqual(agent_build.read_ccache_stats(env, run=run), {})

        run.assert_called_once_with(
            ["ccache", "--print-log-stats", "--format=json"],
            env=env,
            check=False,
            capture_output=True,
            text=True,
            timeout=agent_build.TELEMETRY_TIMEOUT_SECONDS,
        )

    def test_read_ccache_stats_reports_command_and_payload_errors(self) -> None:
        cases = (
            (
                "nonzero",
                subprocess.CompletedProcess(["ccache"], 1, stdout="", stderr="statistics unavailable"),
                "statistics unavailable",
            ),
            (
                "malformed",
                subprocess.CompletedProcess(["ccache"], 0, stdout="{", stderr=""),
                "invalid ccache statistics JSON",
            ),
            (
                "non-object",
                subprocess.CompletedProcess(["ccache"], 0, stdout="[]", stderr=""),
                "ccache statistics were not a JSON object",
            ),
        )
        for name, completed, expected_error in cases:
            with self.subTest(name=name):
                result = agent_build.read_ccache_stats({}, run=mock.Mock(return_value=completed))
                self.assertIn(expected_error, str(result["error"]))

    def test_read_ccache_stats_reports_timeout_and_os_errors(self) -> None:
        cases = (
            (
                "timeout",
                subprocess.TimeoutExpired(["ccache"], agent_build.TELEMETRY_TIMEOUT_SECONDS),
                "timed out",
            ),
            ("os-error", OSError("ccache disappeared"), "could not execute ccache"),
        )
        for name, error, expected_error in cases:
            with self.subTest(name=name):
                result = agent_build.read_ccache_stats({}, run=mock.Mock(side_effect=error))
                self.assertIn(expected_error, str(result["error"]))

    def test_ccache_delta_only_contains_numeric_changes(self) -> None:
        before = {"cache_hit_direct": 7, "cache_miss": 3, "cache_dir": "/tmp/cache", "enabled": False}
        after = {"cache_hit_direct": 11, "cache_miss": 4, "cache_dir": "/tmp/cache", "enabled": True}
        self.assertEqual(agent_build.stats_delta(before, after), {"cache_hit_direct": 4, "cache_miss": 1})

    def test_invocation_ids_produce_distinct_ccache_stats_logs(self) -> None:
        first_id = agent_build.new_invocation_id()
        second_id = agent_build.new_invocation_id()
        first_path = agent_build.ccache_stats_log_path(first_id, temp_root=Path("/tmp"))
        second_path = agent_build.ccache_stats_log_path(second_id, temp_root=Path("/tmp"))
        self.assertNotEqual(first_id, second_id)
        self.assertNotEqual(first_path, second_path)

    def test_append_progress_record_writes_valid_jsonl(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "progress.jsonl"
            agent_build.append_progress_record(path, "build_summary", backend="ninja", exit_code=0)
            payload = json.loads(path.read_text(encoding="utf-8"))
        self.assertEqual(payload["event"], "build_summary")
        self.assertEqual(payload["backend"], "ninja")
        self.assertEqual(payload["exit_code"], 0)

    def test_append_progress_record_preserves_existing_events(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "progress.jsonl"
            existing = {"event": "command_end", "invocation_id": "invocation-1"}
            path.write_text(json.dumps(existing) + "\n", encoding="utf-8")
            agent_build.append_progress_record(
                path,
                "build_summary",
                invocation_id="invocation-1",
                exit_code=0,
            )
            payloads = [json.loads(line) for line in path.read_text(encoding="utf-8").splitlines()]
        self.assertEqual(payloads[0], existing)
        self.assertEqual(payloads[1]["event"], "build_summary")

    def test_append_progress_record_is_a_noop_without_a_destination(self) -> None:
        stdout = io.StringIO()
        with contextlib.redirect_stdout(stdout):
            agent_build.append_progress_record(None, "ignored", unserializable=object())
        self.assertEqual(stdout.getvalue(), "")

    def test_append_progress_record_emits_stdout_without_a_progress_file(self) -> None:
        stdout = io.StringIO()
        with contextlib.redirect_stdout(stdout):
            agent_build.append_progress_record(
                None,
                "build_summary",
                stdout_jsonl=True,
                invocation_id="invocation-1",
                exit_code=0,
            )
        payload = json.loads(stdout.getvalue())
        self.assertEqual(payload["event"], "build_summary")
        self.assertEqual(payload["invocation_id"], "invocation-1")

    def test_progress_reporter_records_invocation_id(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "progress.jsonl"
            with mock.patch.object(agent_build.time, "monotonic", return_value=10.0):
                with agent_build.ProgressReporter(
                    phase="build",
                    invocation_id="invocation-1",
                    progress_path=path,
                    append_progress=False,
                    stdout_jsonl=False,
                    started=10.0,
                ) as progress:
                    progress.emit("command_start", command=["scons"])
            payload = json.loads(path.read_text(encoding="utf-8"))
        self.assertEqual(payload["invocation_id"], "invocation-1")

    def test_read_git_commit_has_explicit_timeout_fallback(self) -> None:
        commit, error = agent_build.read_git_commit(
            Path("/work/Foundry"),
            run=mock.Mock(
                side_effect=subprocess.TimeoutExpired(
                    ["git", "rev-parse", "HEAD"], agent_build.TELEMETRY_TIMEOUT_SECONDS
                )
            ),
        )
        self.assertEqual(commit, "unknown")
        self.assertIn("timed out", str(error))

    def test_read_git_commit_has_explicit_unexpected_error_fallback(self) -> None:
        commit, error = agent_build.read_git_commit(
            Path("/work/Foundry"),
            run=mock.Mock(side_effect=RuntimeError("git probe broke")),
        )
        self.assertEqual(commit, "unknown")
        self.assertIn("unexpected git commit probe failure", str(error))

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

    def test_ninja_state_is_specific_to_the_build_configuration(self) -> None:
        target = agent_build.BuildTarget("macos", Path("bin/foundry.macos.editor.dev.arm64"), None)
        strict_args = agent_build.parse_args(["--backend", "ninja"])
        dev_args = agent_build.parse_args(["--backend", "ninja", "--dev-build"])

        strict_state = agent_build.resolve_ninja_state(strict_args, target, repo_root=Path("/work/Foundry"))
        dev_state = agent_build.resolve_ninja_state(dev_args, target, repo_root=Path("/work/Foundry"))

        self.assertNotEqual(strict_state, dev_state)
        for state in (strict_state, dev_state):
            self.assertEqual(state.file, state.directory / "build.ninja")
            self.assertEqual(state.directory.parent, Path("/work/Foundry/.ninja/agent-build"))

    def test_ninja_state_is_stable_while_build_descriptions_are_unchanged(self) -> None:
        target = agent_build.BuildTarget("macos", Path("bin/foundry.macos.editor.dev.arm64"), None)
        args = agent_build.parse_args(["--backend", "ninja"])
        with tempfile.TemporaryDirectory() as temporary_directory:
            repo_root = Path(temporary_directory) / "Foundry"
            (repo_root / "core").mkdir(parents=True)
            (repo_root / "SConstruct").write_text("# root\n", encoding="utf-8")
            (repo_root / "core" / "SCsub").write_text("# core\n", encoding="utf-8")
            (repo_root / "methods.py").write_text("VALUE = 1\n", encoding="utf-8")

            first = agent_build.resolve_ninja_state(args, target, repo_root=repo_root)
            second = agent_build.resolve_ninja_state(args, target, repo_root=repo_root)

        self.assertEqual(first, second)

    def test_ninja_state_changes_when_repository_python_changes(self) -> None:
        target = agent_build.BuildTarget("macos", Path("bin/foundry.macos.editor.dev.arm64"), None)
        args = agent_build.parse_args(["--backend", "ninja"])
        with tempfile.TemporaryDirectory() as temporary_directory:
            repo_root = Path(temporary_directory) / "Foundry"
            repo_root.mkdir()
            methods = repo_root / "methods.py"
            methods.write_text("VALUE = 1\n", encoding="utf-8")
            before = agent_build.resolve_ninja_state(args, target, repo_root=repo_root)

            methods.write_text("VALUE = 2\n", encoding="utf-8")
            after = agent_build.resolve_ninja_state(args, target, repo_root=repo_root)

        self.assertNotEqual(before, after)

    def test_ninja_state_ignores_sibling_worktrees_inside_the_main_checkout(self) -> None:
        target = agent_build.BuildTarget("macos", Path("bin/foundry.macos.editor.dev.arm64"), None)
        args = agent_build.parse_args(["--backend", "ninja"])
        with tempfile.TemporaryDirectory() as temporary_directory:
            repo_root = Path(temporary_directory) / "Foundry"
            sibling_worktree = repo_root / ".worktrees" / "issue-1"
            sibling_worktree.mkdir(parents=True)
            (repo_root / "methods.py").write_text("VALUE = 1\n", encoding="utf-8")
            sibling_methods = sibling_worktree / "methods.py"
            sibling_methods.write_text("VALUE = 1\n", encoding="utf-8")
            before = agent_build.resolve_ninja_state(args, target, repo_root=repo_root)

            sibling_methods.write_text("VALUE = 2\n", encoding="utf-8")
            after = agent_build.resolve_ninja_state(args, target, repo_root=repo_root)

        self.assertEqual(before, after)

    def test_ninja_state_ignores_foundry_project_state(self) -> None:
        target = agent_build.BuildTarget("macos", Path("bin/foundry.macos.editor.dev.arm64"), None)
        args = agent_build.parse_args(["--backend", "ninja"])
        with tempfile.TemporaryDirectory() as temporary_directory:
            repo_root = Path(temporary_directory) / "Foundry"
            project_state = repo_root / ".foundry" / "editor"
            project_state.mkdir(parents=True)
            (repo_root / "methods.py").write_text("VALUE = 1\n", encoding="utf-8")
            generated_state = project_state / "filesystem_cache.py"
            generated_state.write_text("VALUE = 1\n", encoding="utf-8")
            before = agent_build.resolve_ninja_state(args, target, repo_root=repo_root)

            generated_state.write_text("VALUE = 2\n", encoding="utf-8")
            after = agent_build.resolve_ninja_state(args, target, repo_root=repo_root)

        self.assertEqual(before, after)

    def test_ninja_state_changes_when_custom_configuration_is_created_or_changed(self) -> None:
        target = agent_build.BuildTarget("macos", Path("bin/foundry.macos.editor.dev.arm64"), None)
        args = agent_build.parse_args(["--backend", "ninja"])
        with tempfile.TemporaryDirectory() as temporary_directory:
            repo_root = Path(temporary_directory) / "Foundry"
            repo_root.mkdir()
            missing = agent_build.resolve_ninja_state(args, target, repo_root=repo_root)

            custom = repo_root / "custom.py"
            custom.write_text("dev_build = True\n", encoding="utf-8")
            created = agent_build.resolve_ninja_state(args, target, repo_root=repo_root)
            custom.write_text("dev_build = False\n", encoding="utf-8")
            changed = agent_build.resolve_ninja_state(args, target, repo_root=repo_root)

        self.assertNotEqual(missing, created)
        self.assertNotEqual(created, changed)

    def test_ninja_state_changes_with_selected_external_profile_content(self) -> None:
        target = agent_build.BuildTarget("macos", Path("bin/foundry.macos.editor.dev.arm64"), None)
        with tempfile.TemporaryDirectory() as temporary_directory:
            temporary_root = Path(temporary_directory)
            repo_root = temporary_root / "Foundry"
            repo_root.mkdir()
            profile = temporary_root / "profile.py"
            build_profile = temporary_root / "features.json"
            profile.write_text("dev_build = True\n", encoding="utf-8")
            build_profile.write_text('{"disabled_classes": []}\n', encoding="utf-8")
            args = agent_build.parse_args(
                [
                    "--backend",
                    "ninja",
                    "--scons-arg",
                    f"profile={profile}",
                    "--scons-arg",
                    f"build_profile={build_profile}",
                ]
            )
            initial = agent_build.resolve_ninja_state(args, target, repo_root=repo_root)

            profile.write_text("dev_build = False\n", encoding="utf-8")
            profile_changed = agent_build.resolve_ninja_state(args, target, repo_root=repo_root)
            build_profile.write_text('{"disabled_classes": ["Node"]}\n', encoding="utf-8")
            build_profile_changed = agent_build.resolve_ninja_state(args, target, repo_root=repo_root)

        self.assertNotEqual(initial, profile_changed)
        self.assertNotEqual(profile_changed, build_profile_changed)

    def test_ninja_generation_command_owns_generation_and_cache_settings(self) -> None:
        args = agent_build.parse_args(
            ["--backend", "ninja", "--jobs", "7", "--scons-arg", "arch=arm64", "--scons-arg", "verbose=yes"]
        )
        target = agent_build.BuildTarget("macos", Path("bin/foundry.macos.editor.dev.arm64"), None)
        state = agent_build.NinjaState(Path("/work/.ninja/config"), Path("/work/.ninja/config/build.ninja"))

        with mock.patch.object(agent_build, "scons_prefix", return_value=["scons"]):
            command = agent_build.ninja_generation_command(args, target, state)

        self.assertEqual(command[0], "scons")
        for argument in (
            "platform=macos",
            "target=editor",
            "dev_mode=yes",
            "dev_build=yes",
            "tests=yes",
            "module_text_server_fb_enabled=yes",
            "cache_path=",
            "c_compiler_launcher=ccache",
            "cpp_compiler_launcher=ccache",
            "debug_paths_relative=yes",
            "ninja=yes",
            "ninja_auto_run=no",
            "ninja_file=/work/.ninja/config/build.ninja",
            "ninja_dir=/work/.ninja/config",
            "arch=arm64",
            "verbose=yes",
        ):
            self.assertIn(argument, command)
        self.assertFalse(any(argument.startswith("-j") for argument in command))

    def test_ninja_build_command_resolves_the_executable_and_state_file(self) -> None:
        state = agent_build.NinjaState(
            agent_build.REPO_ROOT / ".ninja/config", agent_build.REPO_ROOT / ".ninja/config/build.ninja"
        )
        with mock.patch.object(agent_build.shutil, "which", return_value="/opt/bin/ninja") as which:
            command = agent_build.ninja_build_command(state, 9)

        which.assert_called_once_with("ninja")
        self.assertEqual(command, ["/opt/bin/ninja", "-f", ".ninja/config/build.ninja", "-j9"])

    def test_ninja_backend_rejects_wrapper_owned_scons_settings(self) -> None:
        for key in sorted(agent_build.NINJA_OWNED_SCONS_KEYS):
            with self.subTest(key=key):
                stderr = io.StringIO()
                with contextlib.redirect_stderr(stderr), self.assertRaises(SystemExit) as raised:
                    agent_build.parse_args(["--backend", "ninja", "--scons-arg", f"{key}=rogue"])
                self.assertEqual(raised.exception.code, 2)
                self.assertTrue(
                    stderr.getvalue()
                    .splitlines()[-1]
                    .endswith(f": error: --backend ninja owns the SCons setting {key!r}; remove that --scons-arg")
                )

    def test_ninja_backend_rejects_disabling_the_compiler_cache(self) -> None:
        stderr = io.StringIO()
        with contextlib.redirect_stderr(stderr), self.assertRaises(SystemExit) as raised:
            agent_build.parse_args(["--backend", "ninja", "--compiler-cache", "none"])
        self.assertEqual(raised.exception.code, 2)
        self.assertTrue(
            stderr.getvalue()
            .splitlines()[-1]
            .endswith(
                ": error: --backend ninja requires ccache; remove --compiler-cache none or use "
                "--backend scons --compiler-cache none"
            )
        )

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

    def test_ccache_environment_owns_per_invocation_stats_log(self) -> None:
        args = agent_build.parse_args(["--compiler-cache", "ccache"])
        with mock.patch.dict(agent_build.os.environ, {"CCACHE_STATSLOG": "/tmp/global.stats"}, clear=True):
            env = agent_build.build_environment(
                args,
                "ccache",
                ccache_stats_log=Path("/tmp/invocation.stats"),
            )
        self.assertEqual(env["CCACHE_STATSLOG"], "/tmp/invocation.stats")

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

    def test_missing_ninja_returns_actionable_failure(self) -> None:
        stderr = io.StringIO()
        with tempfile.TemporaryDirectory() as tmp, contextlib.redirect_stderr(stderr):
            with mock.patch.object(agent_build, "scons_prefix", return_value=["scons"]):
                with mock.patch.object(
                    agent_build.shutil,
                    "which",
                    side_effect=lambda executable: "/bin/ccache" if executable == "ccache" else None,
                ):
                    with mock.patch.object(agent_build, "run_logged_command", return_value=23):
                        exit_code = agent_build.main(
                            ["--backend", "ninja", "--log", str(Path(tmp) / "build.log"), "--no-progress-file"]
                        )
        self.assertEqual(exit_code, 127)
        self.assertEqual(
            stderr.getvalue(),
            "[agent-build] Ninja is required for --backend ninja but was not found in PATH. "
            "Use --backend scons --compiler-cache none for the native fallback.\n",
        )

    def test_main_passes_normalized_ccache_environment_to_build(self) -> None:
        polluted = {
            "PATH": "/bin",
            "CCACHE": "legacy-ccache",
            "SCONS_CACHE": "/tmp/legacy-scons-cache",
            "CCACHE_DISABLE": "1",
        }
        with tempfile.TemporaryDirectory() as tmp:
            temp_root = Path(tmp)
            progress_path = temp_root / "progress.jsonl"
            log_path = temp_root / "build.log"
            stats_log_path = temp_root / "ccache.stats"

            def run_build(_command, **_kwargs):
                stats_log_path.write_text("compiler entry\n", encoding="utf-8")
                return 23

            with contextlib.redirect_stdout(io.StringIO()):
                with mock.patch.dict(agent_build.os.environ, polluted, clear=True):
                    with mock.patch.object(agent_build, "scons_prefix", return_value=["scons"]):
                        with mock.patch.object(agent_build.shutil, "which", return_value="/bin/ccache"):
                            with mock.patch.object(agent_build, "new_invocation_id", return_value="invocation-1"):
                                with mock.patch.object(
                                    agent_build,
                                    "ccache_stats_log_path",
                                    return_value=stats_log_path,
                                ):
                                    with mock.patch.object(
                                        agent_build,
                                        "read_git_commit",
                                        return_value=("deadbeef", None),
                                    ):
                                        with mock.patch.object(
                                            agent_build,
                                            "read_ccache_stats",
                                            side_effect=[{"cache_hit_direct": 0}, RuntimeError("probe exploded")],
                                        ) as read_stats:
                                            with mock.patch.object(
                                                agent_build.time,
                                                "monotonic",
                                                side_effect=[100.0, 101.5],
                                            ):
                                                with mock.patch.object(
                                                    agent_build,
                                                    "run_logged_command",
                                                    side_effect=run_build,
                                                ) as run_command:
                                                    exit_code = agent_build.main(
                                                        [
                                                            "--compiler-cache",
                                                            "ccache",
                                                            "--ccache-dir",
                                                            "/tmp/foundry-ccache",
                                                            "--platform",
                                                            "macos",
                                                            "--jobs",
                                                            "2",
                                                            "--scons-arg",
                                                            "arch=arm64",
                                                            "--log",
                                                            str(log_path),
                                                            "--progress-file",
                                                            str(progress_path),
                                                        ]
                                                    )

            self.assertEqual(exit_code, 23)
            run_command.assert_called_once()
            build_env = run_command.call_args.kwargs["env"]
            self.assertEqual(build_env["CCACHE_DIR"], "/tmp/foundry-ccache")
            self.assertEqual(build_env["CCACHE_BASEDIR"], str(agent_build.REPO_ROOT.resolve()))
            self.assertEqual(build_env["CCACHE_STATSLOG"], str(stats_log_path))
            self.assertNotIn("CCACHE", build_env)
            self.assertNotIn("SCONS_CACHE", build_env)
            self.assertEqual(read_stats.call_args_list, [mock.call(build_env), mock.call(build_env)])
            self.assertEqual(run_command.call_args.kwargs["invocation_id"], "invocation-1")
            self.assertFalse(stats_log_path.exists())

            summary = json.loads(progress_path.read_text(encoding="utf-8"))
            self.assertEqual(summary["event"], "build_summary")
            self.assertEqual(summary["invocation_id"], "invocation-1")
            self.assertEqual(summary["git_commit"], "deadbeef")
            self.assertIsNone(summary["git_commit_error"])
            self.assertEqual(summary["platform"], "macos")
            self.assertEqual(summary["arch"], "arm64")
            self.assertEqual(summary["jobs"], 2)
            self.assertEqual(summary["build_command"][0], "scons")
            self.assertEqual(summary["cache_dir"], "/tmp/foundry-ccache")
            self.assertEqual(summary["cache_policy"], "wrapper-managed-ccache")
            self.assertEqual(summary["cache_source"], "--ccache-dir")
            self.assertEqual(summary["cache_stats_source"], "per-invocation-stats-log")
            self.assertEqual(summary["cache_stats_status"], "error")
            self.assertIn("probe exploded", summary["cache_stats_error"]["after"])
            self.assertEqual(summary["cache_delta"], {})
            self.assertEqual(summary["status"], "failed")
            self.assertEqual(summary["exit_code"], 23)

    def test_ninja_main_generates_missing_state_then_builds_and_summarizes_both_steps(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            temp_root = Path(tmp)
            state = agent_build.NinjaState(temp_root / "state", temp_root / "state" / "build.ninja")
            progress_path = temp_root / "progress.jsonl"
            log_path = temp_root / "build.log"

            with contextlib.redirect_stdout(io.StringIO()):
                with mock.patch.object(agent_build, "scons_prefix", return_value=["scons"]):
                    with mock.patch.object(
                        agent_build.shutil,
                        "which",
                        side_effect=lambda executable: f"/bin/{executable}",
                    ):
                        with mock.patch.object(agent_build, "resolve_ninja_state", return_value=state):
                            with mock.patch.object(agent_build, "new_invocation_id", return_value="invocation-ninja"):
                                with mock.patch.object(agent_build, "read_git_commit", return_value=("deadbeef", None)):
                                    with mock.patch.object(
                                        agent_build,
                                        "read_ccache_stats_best_effort",
                                        return_value={},
                                    ):
                                        with mock.patch.object(
                                            agent_build,
                                            "run_logged_command",
                                            side_effect=[0, 23],
                                        ) as run_command:
                                            exit_code = agent_build.main(
                                                [
                                                    "--backend",
                                                    "ninja",
                                                    "--platform",
                                                    "macos",
                                                    "--jobs",
                                                    "4",
                                                    "--log",
                                                    str(log_path),
                                                    "--progress-file",
                                                    str(progress_path),
                                                ]
                                            )

            self.assertEqual(exit_code, 23)
            self.assertTrue(state.directory.is_dir())
            self.assertEqual(run_command.call_count, 2)
            generation_call, build_call = run_command.call_args_list
            self.assertEqual(generation_call.kwargs["label"], "generate")
            self.assertFalse(generation_call.kwargs["append_log"])
            self.assertFalse(generation_call.kwargs["append_progress"])
            self.assertEqual(build_call.kwargs["label"], "build")
            self.assertTrue(build_call.kwargs["append_log"])
            self.assertTrue(build_call.kwargs["append_progress"])
            self.assertEqual(generation_call.kwargs["invocation_id"], "invocation-ninja")
            self.assertEqual(build_call.kwargs["invocation_id"], "invocation-ninja")

            summary = json.loads(progress_path.read_text(encoding="utf-8"))
            self.assertEqual(
                summary["build_command"],
                ["/bin/ninja", "-f", os.path.relpath(state.file, agent_build.REPO_ROOT), "-j4"],
            )
            self.assertEqual(summary["generation_command"], generation_call.args[0])
            self.assertEqual(summary["generation_exit_code"], 0)
            self.assertEqual(summary["status"], "failed")
            self.assertEqual(summary["exit_code"], 23)

    def test_ninja_main_reuses_existing_state_without_generation(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            temp_root = Path(tmp)
            state = agent_build.NinjaState(temp_root / "state", temp_root / "state" / "build.ninja")
            state.directory.mkdir()
            state.file.write_text("# generated\n", encoding="utf-8")
            progress_path = temp_root / "progress.jsonl"

            with contextlib.redirect_stdout(io.StringIO()):
                with mock.patch.object(agent_build, "scons_prefix", return_value=["scons"]):
                    with mock.patch.object(agent_build.shutil, "which", side_effect=lambda name: f"/bin/{name}"):
                        with mock.patch.object(agent_build, "resolve_ninja_state", return_value=state):
                            with mock.patch.object(agent_build, "read_ccache_stats_best_effort", return_value={}):
                                with mock.patch.object(
                                    agent_build,
                                    "run_logged_command",
                                    return_value=23,
                                ) as run_command:
                                    exit_code = agent_build.main(
                                        [
                                            "--backend",
                                            "ninja",
                                            "--platform",
                                            "macos",
                                            "--append-log",
                                            "--append-progress",
                                            "--log",
                                            str(temp_root / "build.log"),
                                            "--progress-file",
                                            str(progress_path),
                                        ]
                                    )

            self.assertEqual(exit_code, 23)
            run_command.assert_called_once()
            self.assertEqual(run_command.call_args.kwargs["label"], "build")
            self.assertTrue(run_command.call_args.kwargs["append_log"])
            self.assertTrue(run_command.call_args.kwargs["append_progress"])
            summary = json.loads(progress_path.read_text(encoding="utf-8"))
            self.assertIsNone(summary["generation_command"])
            self.assertEqual(summary["generation_status"], "skipped-existing")

    def test_ninja_generation_setup_error_still_emits_a_summary(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            temp_root = Path(tmp)
            state_directory = temp_root / "state"
            state_directory.write_text("not a directory\n", encoding="utf-8")
            state = agent_build.NinjaState(state_directory, state_directory / "build.ninja")
            progress_path = temp_root / "progress.jsonl"

            with contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
                with mock.patch.object(agent_build, "scons_prefix", return_value=["scons"]):
                    with mock.patch.object(agent_build.shutil, "which", side_effect=lambda name: f"/bin/{name}"):
                        with mock.patch.object(agent_build, "resolve_ninja_state", return_value=state):
                            with mock.patch.object(agent_build, "read_ccache_stats_best_effort", return_value={}):
                                with mock.patch.object(agent_build, "run_logged_command") as run_command:
                                    exit_code = agent_build.main(
                                        [
                                            "--backend",
                                            "ninja",
                                            "--platform",
                                            "macos",
                                            "--log",
                                            str(temp_root / "build.log"),
                                            "--progress-file",
                                            str(progress_path),
                                        ]
                                    )

            self.assertEqual(exit_code, 127)
            run_command.assert_not_called()
            summary = json.loads(progress_path.read_text(encoding="utf-8"))
            self.assertEqual(summary["generation_status"], "error")
            self.assertIn("FileExistsError", summary["generation_error"])
            self.assertEqual(summary["status"], "error")
            self.assertEqual(summary["exit_code"], 127)

    def test_main_emits_summary_to_jsonl_stdout_without_progress_file(self) -> None:
        stdout = io.StringIO()
        stderr = io.StringIO()
        with tempfile.TemporaryDirectory() as tmp:
            log_path = Path(tmp) / "build.log"
            with contextlib.redirect_stdout(stdout), contextlib.redirect_stderr(stderr):
                with mock.patch.object(agent_build, "scons_prefix", return_value=["scons"]):
                    with mock.patch.object(agent_build, "new_invocation_id", return_value="invocation-stdout"):
                        with mock.patch.object(
                            agent_build,
                            "read_git_commit",
                            return_value=("deadbeef", None),
                        ):
                            with mock.patch.object(agent_build.time, "monotonic", side_effect=[100.0, 101.0]):
                                with mock.patch.object(
                                    agent_build,
                                    "run_logged_command",
                                    return_value=23,
                                ) as run_command:
                                    exit_code = agent_build.main(
                                        [
                                            "--platform",
                                            "macos",
                                            "--log",
                                            str(log_path),
                                            "--progress-format",
                                            "jsonl",
                                            "--no-progress-file",
                                        ]
                                    )

        self.assertEqual(exit_code, 23)
        payload = json.loads(stdout.getvalue())
        self.assertEqual(payload["event"], "build_summary")
        self.assertEqual(payload["invocation_id"], "invocation-stdout")
        self.assertEqual(payload["cache_stats_status"], "disabled")
        self.assertEqual(run_command.call_args.kwargs["invocation_id"], payload["invocation_id"])
        self.assertIn("[agent-build] summary: backend=scons cache=none duration=1s", stderr.getvalue())

    def test_main_records_spawn_error_summary_and_returns_127(self) -> None:
        stdout = io.StringIO()
        stderr = io.StringIO()
        with tempfile.TemporaryDirectory() as tmp:
            temp_root = Path(tmp)
            progress_path = temp_root / "progress.jsonl"
            with contextlib.redirect_stdout(stdout), contextlib.redirect_stderr(stderr):
                with mock.patch.object(agent_build, "scons_prefix", return_value=["scons"]):
                    with mock.patch.object(agent_build, "new_invocation_id", return_value="invocation-error"):
                        with mock.patch.object(
                            agent_build,
                            "read_git_commit",
                            return_value=("deadbeef", None),
                        ):
                            with mock.patch.object(agent_build.time, "monotonic", side_effect=[100.0, 100.25]):
                                with mock.patch.object(
                                    agent_build,
                                    "run_logged_command",
                                    side_effect=OSError("missing executable"),
                                ):
                                    exit_code = agent_build.main(
                                        [
                                            "--platform",
                                            "macos",
                                            "--log",
                                            str(temp_root / "build.log"),
                                            "--progress-file",
                                            str(progress_path),
                                        ]
                                    )

            summary = json.loads(progress_path.read_text(encoding="utf-8"))
        self.assertEqual(exit_code, 127)
        self.assertEqual(summary["event"], "build_summary")
        self.assertEqual(summary["status"], "error")
        self.assertEqual(summary["exit_code"], 127)
        self.assertIn("missing executable", summary["error"])
        self.assertIn("failed to start build command: missing executable", stderr.getvalue())

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
