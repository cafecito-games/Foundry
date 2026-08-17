#!/usr/bin/env python3
"""Behavioral tests for scripts/agent_build.py."""

from __future__ import annotations

import contextlib
import importlib.util
import io
import json
import os
import shutil
import subprocess
import sys
import tempfile
import unittest
from collections.abc import Iterator
from datetime import datetime
from pathlib import Path
from typing import Any
from unittest import mock

_MODULE_PATH = Path(__file__).resolve().parents[1] / "agent_build.py"
_spec = importlib.util.spec_from_file_location("agent_build", _MODULE_PATH)
assert _spec is not None and _spec.loader is not None
agent_build: Any = importlib.util.module_from_spec(_spec)
sys.modules[_spec.name] = agent_build
_spec.loader.exec_module(agent_build)

RESULT_PREFIX = agent_build.RESULT_PREFIX


@contextlib.contextmanager
def scratch_directory() -> Iterator[Path]:
    """A temporary directory rooted in the shared test scratch space when one is configured."""
    scratch_root = os.environ.get("FOUNDRY_TEST_SCRATCH")
    parent = Path(scratch_root) if scratch_root else None
    if parent is not None:
        parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(dir=parent) as temporary:
        yield Path(temporary)


def progress_events(path: Path) -> list[dict[str, Any]]:
    return [json.loads(line) for line in path.read_text(encoding="utf-8").splitlines() if line.strip()]


def sole_build_summary(events: list[dict[str, Any]]) -> dict[str, Any]:
    summaries = [event for event in events if event["event"] == "build_summary"]
    if len(summaries) != 1:
        raise AssertionError(f"expected exactly one build_summary, found {len(summaries)}")
    return summaries[0]


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
        self.assertEqual(args.test_case, ["*FoundryCLI*"])

    def test_repeated_case_retains_every_value_in_order(self) -> None:
        args = agent_build.parse_args(["--case", "*A*", "--case", "*B*", "--case", "*C*"])
        self.assertTrue(args.test)
        self.assertEqual(args.test_case, ["*A*", "*B*", "*C*"])

    def test_no_case_leaves_the_filter_unset_and_does_not_imply_test(self) -> None:
        args = agent_build.parse_args([])
        self.assertIsNone(args.test_case)
        self.assertIsNone(args.test_suite)
        self.assertFalse(args.test)

    def test_suite_implies_test(self) -> None:
        args = agent_build.parse_args(["--suite", "*[Modules][FoundryScript][Format]*"])
        self.assertTrue(args.test)
        self.assertEqual(args.test_suite, ["*[Modules][FoundryScript][Format]*"])

    def test_repeated_suite_retains_every_value_in_order(self) -> None:
        args = agent_build.parse_args(["--suite", "*A*", "--suite", "*B*"])
        self.assertEqual(args.test_suite, ["*A*", "*B*"])

    def test_command_forwards_every_repeated_case_filter_in_order(self) -> None:
        args = agent_build.parse_args(["--case", "*A*", "--case", "*B*"])
        target = agent_build.BuildTarget(
            scons_platform="macos",
            binary_path=Path("/tmp/foundry.macos.editor.dev.arm64"),
            default_display=None,
        )
        command = agent_build.test_command(args, target)
        self.assertEqual(
            command,
            [
                str(target.binary_path),
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

    def test_command_forwards_case_and_suite_filters_together(self) -> None:
        args = agent_build.parse_args(["--case", "*A*", "--suite", "*S*", "--suite", "*T*"])
        target = agent_build.BuildTarget(
            scons_platform="macos",
            binary_path=Path("/tmp/foundry.macos.editor.dev.arm64"),
            default_display=None,
        )
        command = agent_build.test_command(args, target)
        self.assertEqual(
            command,
            [
                str(target.binary_path),
                "--headless",
                "test",
                "run",
                "--case",
                "*A*",
                "--suite",
                "*S*",
                "--suite",
                "*T*",
                "--force-colors",
            ],
        )

    def test_command_omits_case_when_no_filter_was_supplied(self) -> None:
        args = agent_build.parse_args([])
        target = agent_build.BuildTarget(
            scons_platform="macos",
            binary_path=Path("/tmp/foundry.macos.editor.dev.arm64"),
            default_display=None,
        )
        command = agent_build.test_command(args, target)
        self.assertEqual(
            command,
            [str(target.binary_path), "--headless", "test", "run", "--force-colors"],
        )

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

    def test_ninja_state_changes_when_repository_file_inventory_changes(self) -> None:
        target = agent_build.BuildTarget("macos", Path("bin/foundry.macos.editor.dev.arm64"), None)
        args = agent_build.parse_args(["--backend", "ninja"])
        with tempfile.TemporaryDirectory() as temporary_directory:
            repo_root = Path(temporary_directory) / "Foundry"
            source_directory = repo_root / "core" / "object"
            source_directory.mkdir(parents=True)
            (repo_root / "SConstruct").write_text("# root\n", encoding="utf-8")
            subprocess.run(["git", "init", "-q"], cwd=repo_root, check=True)
            subprocess.run(["git", "add", "SConstruct"], cwd=repo_root, check=True)
            before = agent_build.resolve_ninja_state(args, target, repo_root=repo_root)

            source = source_directory / "class_handle.cpp"
            source.write_text("// implementation\n", encoding="utf-8")
            after_addition = agent_build.resolve_ninja_state(args, target, repo_root=repo_root)
            source.unlink()
            after_removal = agent_build.resolve_ninja_state(args, target, repo_root=repo_root)

        self.assertNotEqual(before, after_addition)
        self.assertEqual(before, after_removal)

    def test_ninja_state_ignores_repository_source_content_changes(self) -> None:
        target = agent_build.BuildTarget("macos", Path("bin/foundry.macos.editor.dev.arm64"), None)
        args = agent_build.parse_args(["--backend", "ninja"])
        with tempfile.TemporaryDirectory() as temporary_directory:
            repo_root = Path(temporary_directory) / "Foundry"
            source_directory = repo_root / "core" / "object"
            source_directory.mkdir(parents=True)
            source = source_directory / "class_handle.cpp"
            source.write_text("// first implementation\n", encoding="utf-8")
            before = agent_build.resolve_ninja_state(args, target, repo_root=repo_root)

            source.write_text("// changed implementation\n", encoding="utf-8")
            after = agent_build.resolve_ninja_state(args, target, repo_root=repo_root)

        self.assertEqual(before, after)

    def test_ninja_state_ignores_gitignored_generated_files(self) -> None:
        target = agent_build.BuildTarget("macos", Path("bin/foundry.macos.editor.dev.arm64"), None)
        args = agent_build.parse_args(["--backend", "ninja"])
        with tempfile.TemporaryDirectory() as temporary_directory:
            repo_root = Path(temporary_directory) / "Foundry"
            repo_root.mkdir()
            (repo_root / ".gitignore").write_text("*.gen.*\n", encoding="utf-8")
            (repo_root / "SConstruct").write_text("# root\n", encoding="utf-8")
            subprocess.run(["git", "init", "-q"], cwd=repo_root, check=True)
            subprocess.run(["git", "add", ".gitignore", "SConstruct"], cwd=repo_root, check=True)
            before = agent_build.resolve_ninja_state(args, target, repo_root=repo_root)

            generated = repo_root / "core" / "version.gen.cpp"
            generated.parent.mkdir(parents=True)
            generated.write_text("// generated\n", encoding="utf-8")
            after = agent_build.resolve_ninja_state(args, target, repo_root=repo_root)

        self.assertEqual(before, after)

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
        stdout = io.StringIO()
        with scratch_directory() as root:
            log_path = root / "build.log"
            with contextlib.redirect_stderr(stderr), contextlib.redirect_stdout(stdout):
                with mock.patch.object(agent_build, "scons_prefix", return_value=["scons"]):
                    with mock.patch.object(agent_build.shutil, "which", return_value=None):
                        exit_code = agent_build.main(
                            ["--compiler-cache", "ccache", "--no-progress-file", "--log", str(log_path)]
                        )
            log_lines = log_path.read_text(encoding="utf-8").splitlines()
        self.assertEqual(exit_code, 127)
        self.assertEqual(
            stderr.getvalue(),
            "[agent-build] ccache is required for this build mode but was not found in PATH. "
            "Use --backend scons --compiler-cache none for the native fallback.\n",
        )
        self.assertTrue(log_lines[-1].startswith(f"{RESULT_PREFIX} tooling-missing step=startup exit_code=127"))
        self.assertEqual(stdout.getvalue().splitlines(), log_lines)

    def test_missing_ninja_returns_actionable_failure(self) -> None:
        stderr = io.StringIO()
        with scratch_directory() as root:
            log_path = root / "build.log"
            with contextlib.redirect_stderr(stderr), contextlib.redirect_stdout(io.StringIO()):
                with mock.patch.object(agent_build, "scons_prefix", return_value=["scons"]):
                    with mock.patch.object(
                        agent_build.shutil,
                        "which",
                        side_effect=lambda executable: "/bin/ccache" if executable == "ccache" else None,
                    ):
                        exit_code = agent_build.main(
                            ["--backend", "ninja", "--log", str(log_path), "--no-progress-file"]
                        )
            result_line = log_path.read_text(encoding="utf-8").splitlines()[-1]
        self.assertEqual(exit_code, 127)
        self.assertEqual(
            stderr.getvalue(),
            "[agent-build] Ninja is required for --backend ninja but was not found in PATH. "
            "Use --backend scons --compiler-cache none for the native fallback.\n",
        )
        self.assertTrue(result_line.startswith(f"{RESULT_PREFIX} tooling-missing step=startup exit_code=127"))

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
                                                side_effect=[90.0, 100.0, 101.5, 101.6],
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

            summary = sole_build_summary(progress_events(progress_path))
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
            # `run_start` owns the progress file's truncation, so every command appends to it.
            self.assertTrue(generation_call.kwargs["append_progress"])
            self.assertEqual(build_call.kwargs["label"], "build")
            self.assertTrue(build_call.kwargs["append_log"])
            self.assertTrue(build_call.kwargs["append_progress"])
            self.assertEqual(generation_call.kwargs["invocation_id"], "invocation-ninja")
            self.assertEqual(build_call.kwargs["invocation_id"], "invocation-ninja")

            summary = sole_build_summary(progress_events(progress_path))
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
            summary = sole_build_summary(progress_events(progress_path))
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
            summary = sole_build_summary(progress_events(progress_path))
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
                            with mock.patch.object(
                                agent_build.time, "monotonic", side_effect=[90.0, 100.0, 101.0, 101.1]
                            ):
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
        payload = sole_build_summary([json.loads(line) for line in stdout.getvalue().splitlines() if line.strip()])
        self.assertEqual(payload["event"], "build_summary")
        self.assertEqual(payload["invocation_id"], "invocation-stdout")
        self.assertEqual(payload["cache_stats_status"], "disabled")
        self.assertEqual(run_command.call_args.kwargs["invocation_id"], payload["invocation_id"])
        source = payload["jobs_source"]
        self.assertIn(source, {"host-cpu-count", "cgroup-cpu-quota", agent_build.JOBS_ENVIRONMENT_VARIABLE})
        self.assertGreaterEqual(payload["jobs"], 1)
        self.assertIn(f"[agent-build] jobs: {payload['jobs']} (source: {source})", stderr.getvalue())
        self.assertIn(
            f"[agent-build] summary: backend=scons cache=none jobs={payload['jobs']}({source}) duration=1s",
            stderr.getvalue(),
        )

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
                            with mock.patch.object(
                                agent_build.time, "monotonic", side_effect=[90.0, 100.0, 100.25, 100.3]
                            ):
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

            summary = sole_build_summary(progress_events(progress_path))
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


class JobConcurrencyTests(unittest.TestCase):
    def _temporary_directory(self) -> str:
        directory = tempfile.TemporaryDirectory()
        self.addCleanup(directory.cleanup)
        return directory.name

    def _cgroup_v2(self, cpu_max: str | None, relative: str = "/") -> tuple[Path, Path]:
        root = Path(self._temporary_directory())
        cgroup_root = root / "cgroup"
        leaf = cgroup_root / relative.lstrip("/")
        leaf.mkdir(parents=True, exist_ok=True)
        if cpu_max is not None:
            (leaf / "cpu.max").write_text(f"{cpu_max}\n", encoding="utf-8")
        proc_cgroup = root / "proc_self_cgroup"
        proc_cgroup.write_text(f"0::{relative}\n", encoding="utf-8")
        return cgroup_root, proc_cgroup

    def _resolve(self, **overrides: Any) -> Any:
        cgroup_root, proc_cgroup = self._cgroup_v2(overrides.pop("cpu_max", None), overrides.pop("relative", "/"))
        arguments: dict[str, Any] = {
            "explicit_jobs": None,
            "environment": {},
            "host_cpu_count": 16,
            "cgroup_root": cgroup_root,
            "proc_cgroup": proc_cgroup,
        }
        arguments.update(overrides)
        return agent_build.resolve_job_selection(**arguments)

    def test_host_cpu_count_is_used_without_a_cgroup_quota(self) -> None:
        selection = self._resolve(cpu_max="max 100000")
        self.assertEqual(selection.jobs, 16)
        self.assertEqual(selection.source, "host-cpu-count")

    def test_missing_cgroup_files_fall_back_to_the_host_cpu_count(self) -> None:
        selection = self._resolve()
        self.assertEqual(selection.jobs, 16)
        self.assertEqual(selection.source, "host-cpu-count")

    def test_cgroup_quota_caps_the_default_job_count(self) -> None:
        selection = self._resolve(cpu_max="400000 100000")
        self.assertEqual(selection.jobs, 4)
        self.assertEqual(selection.source, "cgroup-cpu-quota")

    def test_fractional_cgroup_quota_rounds_down_but_stays_positive(self) -> None:
        self.assertEqual(self._resolve(cpu_max="250000 100000").jobs, 2)
        self.assertEqual(self._resolve(cpu_max="50000 100000").jobs, 1)

    def test_cgroup_quota_above_the_host_cpu_count_does_not_inflate_jobs(self) -> None:
        selection = self._resolve(cpu_max="3200000 100000", host_cpu_count=8)
        self.assertEqual(selection.jobs, 8)
        self.assertEqual(selection.source, "host-cpu-count")

    def test_nested_cgroup_quota_is_read_from_the_process_cgroup_path(self) -> None:
        selection = self._resolve(cpu_max="200000 100000", relative="/workspace/build")
        self.assertEqual(selection.jobs, 2)
        self.assertEqual(selection.source, "cgroup-cpu-quota")

    def test_cgroup_v1_quota_is_honored(self) -> None:
        root = Path(self._temporary_directory())
        cgroup_root = root / "cgroup"
        (cgroup_root / "cpu").mkdir(parents=True)
        (cgroup_root / "cpu" / "cpu.cfs_quota_us").write_text("300000\n", encoding="utf-8")
        (cgroup_root / "cpu" / "cpu.cfs_period_us").write_text("100000\n", encoding="utf-8")
        proc_cgroup = root / "proc_self_cgroup"
        proc_cgroup.write_text("1:cpu:/\n", encoding="utf-8")
        selection = agent_build.resolve_job_selection(
            explicit_jobs=None,
            environment={},
            host_cpu_count=16,
            cgroup_root=cgroup_root,
            proc_cgroup=proc_cgroup,
        )
        self.assertEqual(selection.jobs, 3)
        self.assertEqual(selection.source, "cgroup-cpu-quota")

    def test_nested_cgroup_v1_quota_is_read_from_the_process_cgroup_path(self) -> None:
        root = Path(self._temporary_directory())
        cgroup_root = root / "cgroup"
        scoped = cgroup_root / "cpu,cpuacct" / "docker" / "abc123"
        scoped.mkdir(parents=True)
        (scoped / "cpu.cfs_quota_us").write_text("200000\n", encoding="utf-8")
        (scoped / "cpu.cfs_period_us").write_text("100000\n", encoding="utf-8")
        proc_cgroup = root / "proc_self_cgroup"
        proc_cgroup.write_text("3:cpu,cpuacct:/docker/abc123\n", encoding="utf-8")
        selection = agent_build.resolve_job_selection(
            explicit_jobs=None,
            environment={},
            host_cpu_count=16,
            cgroup_root=cgroup_root,
            proc_cgroup=proc_cgroup,
        )
        self.assertEqual(selection.jobs, 2)
        self.assertEqual(selection.source, "cgroup-cpu-quota")

    def test_environment_override_beats_the_detected_default(self) -> None:
        selection = self._resolve(cpu_max="400000 100000", environment={"FOUNDRY_BUILD_JOBS": "6"})
        self.assertEqual(selection.jobs, 6)
        self.assertEqual(selection.source, "FOUNDRY_BUILD_JOBS")

    def test_explicit_jobs_beat_the_environment_override(self) -> None:
        selection = self._resolve(
            cpu_max="400000 100000",
            environment={"FOUNDRY_BUILD_JOBS": "6"},
            explicit_jobs=9,
        )
        self.assertEqual(selection.jobs, 9)
        self.assertEqual(selection.source, "--jobs")

    def test_invalid_environment_override_is_rejected(self) -> None:
        for value in ("0", "-2", "many", ""):
            with self.subTest(value=value):
                with self.assertRaises(ValueError):
                    self._resolve(environment={"FOUNDRY_BUILD_JOBS": value})

    def test_unreadable_cgroup_values_fall_back_to_the_host_cpu_count(self) -> None:
        for value in ("garbage", "100000 0", "100000"):
            with self.subTest(value=value):
                selection = self._resolve(cpu_max=value)
                self.assertEqual(selection.jobs, 16)
                self.assertEqual(selection.source, "host-cpu-count")

    def test_parse_args_records_the_job_count_and_its_source(self) -> None:
        with mock.patch.dict(os.environ, {"FOUNDRY_BUILD_JOBS": "5"}, clear=False):
            args = agent_build.parse_args([])
        self.assertEqual(args.jobs, 5)
        self.assertEqual(args.jobs_source, "FOUNDRY_BUILD_JOBS")
        with mock.patch.dict(os.environ, {"FOUNDRY_BUILD_JOBS": "5"}, clear=False):
            args = agent_build.parse_args(["--jobs", "3"])
        self.assertEqual(args.jobs, 3)
        self.assertEqual(args.jobs_source, "--jobs")

    def test_parse_args_rejects_an_invalid_environment_override(self) -> None:
        stderr = io.StringIO()
        with mock.patch.dict(os.environ, {"FOUNDRY_BUILD_JOBS": "nope"}, clear=False):
            with contextlib.redirect_stderr(stderr), self.assertRaises(SystemExit):
                agent_build.parse_args([])
        self.assertIn("FOUNDRY_BUILD_JOBS", stderr.getvalue())


class BinaryPathTests(unittest.TestCase):
    """The wrapper must look for the file SCons actually writes."""

    def binary_name(self, argv: list[str]) -> str:
        args = agent_build.parse_args(["--platform", "macos", "--scons-arg", "arch=arm64", *argv])
        return str(agent_build.resolve_build_target(args).binary_path.name)

    def test_default_editor_binary_name(self) -> None:
        self.assertEqual(self.binary_name([]), "foundry.macos.editor.dev.arm64")

    def test_double_precision_changes_the_binary_name(self) -> None:
        self.assertEqual(
            self.binary_name(["--scons-arg", "precision=double"]),
            "foundry.macos.editor.dev.double.arm64",
        )

    def test_disabled_threads_change_the_binary_name(self) -> None:
        self.assertEqual(
            self.binary_name(["--scons-arg", "threads=no"]),
            "foundry.macos.editor.dev.arm64.nothreads",
        )

    def test_extra_suffix_changes_the_binary_name(self) -> None:
        self.assertEqual(
            self.binary_name(["--scons-arg", "extra_suffix=probe"]),
            "foundry.macos.editor.dev.arm64.probe",
        )

    def test_overridden_dev_build_drops_the_dev_marker(self) -> None:
        self.assertEqual(self.binary_name(["--scons-arg", "dev_build=no"]), "foundry.macos.editor.arm64")

    def test_every_name_altering_setting_combines(self) -> None:
        self.assertEqual(
            self.binary_name(
                [
                    "--scons-arg",
                    "precision=double",
                    "--scons-arg",
                    "threads=false",
                    "--scons-arg",
                    "extra_suffix=probe",
                ]
            ),
            "foundry.macos.editor.dev.double.arm64.nothreads.probe",
        )


def fake_scons(directory: Path, *, exit_code: int, lines: list[str]) -> Path:
    """A stand-in SCons that prints the given output and exits with the given status."""
    payload = json.dumps({"lines": list(lines), "exit_code": exit_code})
    script = directory / f"fake_scons_{abs(hash(payload)) % 10**8}.py"
    script.write_text(
        "import json\n"
        "import sys\n"
        f"payload = json.loads({payload!r})\n"
        "for line in payload['lines']:\n"
        "    print(line)\n"
        "sys.exit(payload['exit_code'])\n",
        encoding="utf-8",
    )
    return script


def linking_fake_scons(directory: Path, binary_path: Path, content: bytes, *, settle_seconds: float = 0.0) -> Path:
    """A stand-in SCons that writes the editor binary, the way a real build's final link does."""
    script = directory / f"linking_fake_scons_{abs(hash((str(binary_path), content, settle_seconds))) % 10**8}.py"
    script.write_text(
        "import pathlib\n"
        "import time\n"
        f"pathlib.Path({str(binary_path)!r}).write_bytes({content!r})\n"
        f"time.sleep({settle_seconds!r})\n"
        "print('scons: done building targets.')\n",
        encoding="utf-8",
    )
    return script


def executable_stub(path: Path, exit_code: int) -> Path:
    """An executable file that exits with the given status, standing in for a built binary."""
    path.write_text(f"#!/bin/sh\nexit {exit_code}\n", encoding="utf-8")
    path.chmod(0o755)
    return path


SHADOW_DIAGNOSTIC = [
    "modules/foundry_script/tests/test_foundry_script_type.h:516:24: error: declaration shadows a "
    "variable in namespace FSTests [-Werror,-Wshadow]",
    "1 error generated.",
    "scons: *** [test.o] Error 1",
]


class WrapperHarness(unittest.TestCase):
    """Runs the wrapper end to end against a fake SCons, so a whole run takes milliseconds."""

    def run_wrapper(
        self,
        root: Path,
        scons: Path,
        *,
        extra_argv: list[str] | None = None,
        binary_path: Path | None = None,
        which=None,
    ) -> tuple[int, Path, Path, str]:
        binary_path = binary_path if binary_path is not None else root / "foundry.macos.editor.dev.arm64"
        log_path = root / "build.log"
        progress_path = root / "progress.jsonl"
        target = agent_build.BuildTarget("macos", binary_path, None)
        stdout = io.StringIO()
        with contextlib.redirect_stdout(stdout), contextlib.redirect_stderr(io.StringIO()):
            with mock.patch.object(agent_build, "scons_prefix", return_value=[sys.executable, str(scons)]):
                with mock.patch.object(agent_build, "resolve_build_target", return_value=target):
                    with contextlib.ExitStack() as stack:
                        if which is not None:
                            stack.enter_context(mock.patch.object(agent_build.shutil, "which", side_effect=which))
                        exit_code = agent_build.main(
                            [
                                "--jobs",
                                "1",
                                "--log",
                                str(log_path),
                                "--progress-file",
                                str(progress_path),
                                *(extra_argv or []),
                            ]
                        )
        return exit_code, log_path, progress_path, stdout.getvalue()

    def result_line(self, log_path: Path) -> str:
        lines = [line for line in log_path.read_text(encoding="utf-8").splitlines() if line.strip()]
        self.assertTrue(lines, "the build log is empty")
        return lines[-1]

    def result_lines(self, log_path: Path) -> list[str]:
        return [line for line in log_path.read_text(encoding="utf-8").splitlines() if line.startswith(RESULT_PREFIX)]


class ResultContractTests(WrapperHarness):
    """The wrapper's exit code and its terminal RESULT: line must agree with what the build did."""

    def test_failed_build_exits_with_the_scons_child_status(self) -> None:
        with scratch_directory() as root:
            scons = fake_scons(root, exit_code=2, lines=SHADOW_DIAGNOSTIC)
            exit_code, _, _, _ = self.run_wrapper(root, scons)
        self.assertEqual(exit_code, 2)

    def test_failed_build_summary_reports_failure(self) -> None:
        with scratch_directory() as root:
            scons = fake_scons(root, exit_code=2, lines=SHADOW_DIAGNOSTIC)
            _, _, progress_path, _ = self.run_wrapper(root, scons)
            events = progress_events(progress_path)
        summary = sole_build_summary(events)
        self.assertEqual(summary["status"], "failed")
        self.assertEqual(summary["exit_code"], 2)
        self.assertNotIn("success", [event.get("status") for event in events])

    def test_failed_build_writes_a_build_failure_result_line(self) -> None:
        with scratch_directory() as root:
            scons = fake_scons(root, exit_code=2, lines=SHADOW_DIAGNOSTIC)
            exit_code, log_path, _, stdout = self.run_wrapper(root, scons)
            line = self.result_line(log_path)
        self.assertEqual(exit_code, 2)
        self.assertTrue(line.startswith(f"{RESULT_PREFIX} build-failure"), line)
        self.assertIn("step=build", line)
        self.assertIn("exit_code=2", line)
        self.assertIn("binary_present=no", line)
        self.assertIn(line, stdout)

    def test_successful_build_without_expected_binary_fails(self) -> None:
        with scratch_directory() as root:
            scons = fake_scons(root, exit_code=0, lines=["scons: done building targets."])
            exit_code, log_path, _, _ = self.run_wrapper(root, scons)
            line = self.result_line(log_path)
        self.assertEqual(exit_code, 1)
        self.assertTrue(line.startswith(f"{RESULT_PREFIX} binary-missing"), line)
        self.assertIn("exit_code=1", line)
        self.assertIn("binary_present=no", line)

    def test_successful_build_with_binary_reports_success(self) -> None:
        with scratch_directory() as root:
            binary_path = root / "foundry.macos.editor.dev.arm64"
            binary_path.write_bytes(b"linked editor binary")
            scons = fake_scons(root, exit_code=0, lines=["scons: done building targets."])
            exit_code, log_path, progress_path, _ = self.run_wrapper(root, scons, binary_path=binary_path)
            summary = sole_build_summary(progress_events(progress_path))
            line = self.result_line(log_path)
        self.assertEqual(exit_code, 0)
        self.assertTrue(line.startswith(f"{RESULT_PREFIX} success"), line)
        self.assertIn("binary_present=yes", line)
        self.assertEqual(summary["status"], "success")

    def test_failing_tests_after_a_successful_build_report_test_failure(self) -> None:
        with scratch_directory() as root:
            binary_path = executable_stub(root / "foundry.macos.editor.dev.arm64", 7)
            scons = fake_scons(root, exit_code=0, lines=["scons: done building targets."])
            exit_code, log_path, progress_path, _ = self.run_wrapper(
                root, scons, binary_path=binary_path, extra_argv=["--test"]
            )
            summary = sole_build_summary(progress_events(progress_path))
            line = self.result_line(log_path)
        self.assertEqual(exit_code, 7)
        self.assertTrue(line.startswith(f"{RESULT_PREFIX} test-failure"), line)
        self.assertIn("step=test", line)
        self.assertEqual(summary["status"], "success")

    def test_ninja_build_failure_exits_with_the_ninja_child_status(self) -> None:
        if shutil.which("ninja") is None:
            self.skipTest("Ninja is not available")
        with scratch_directory() as root:
            state = agent_build.NinjaState(root / "state", root / "state" / "build.ninja")
            state.directory.mkdir(parents=True)
            state.file.write_text(
                "rule fail\n  command = sh -c 'exit 1'\n  description = fail\nbuild all: fail\ndefault all\n",
                encoding="utf-8",
            )
            scons = fake_scons(root, exit_code=0, lines=[])
            with mock.patch.object(agent_build, "resolve_ninja_state", return_value=state):
                exit_code, log_path, _, _ = self.run_wrapper(root, scons, extra_argv=["--backend", "ninja"])
                line = self.result_line(log_path)
        self.assertEqual(exit_code, 1)
        self.assertTrue(line.startswith(f"{RESULT_PREFIX} build-failure"), line)

    def test_ninja_generation_failure_reports_generation_failure(self) -> None:
        with scratch_directory() as root:
            state = agent_build.NinjaState(root / "state", root / "state" / "build.ninja")
            scons = fake_scons(root, exit_code=3, lines=["scons: *** generation failed"])
            with mock.patch.object(agent_build, "resolve_ninja_state", return_value=state):
                exit_code, log_path, progress_path, _ = self.run_wrapper(
                    root,
                    scons,
                    extra_argv=["--backend", "ninja"],
                    which=lambda executable: f"/usr/bin/{executable}",
                )
                summary = sole_build_summary(progress_events(progress_path))
                line = self.result_line(log_path)
        self.assertEqual(exit_code, 3)
        self.assertTrue(line.startswith(f"{RESULT_PREFIX} generation-failure"), line)
        self.assertIn("step=generate", line)
        self.assertEqual(summary["generation_status"], "failed")

    def test_a_platform_suffixed_binary_satisfies_the_link_check(self) -> None:
        with scratch_directory() as root:
            build_directory = root / "bin"
            build_directory.mkdir()
            (build_directory / "foundry.macos.editor.dev.arm64.san").write_bytes(b"linked editor binary")
            scons = fake_scons(root, exit_code=0, lines=["scons: done building targets."])
            exit_code, log_path, _, _ = self.run_wrapper(
                root, scons, binary_path=build_directory / "foundry.macos.editor.dev.arm64"
            )
            line = self.result_line(log_path)
        self.assertEqual(exit_code, 0)
        self.assertTrue(line.startswith(f"{RESULT_PREFIX} success"), line)
        self.assertIn("binary=" + str(build_directory / "foundry.macos.editor.dev.arm64.san"), line)
        self.assertIn("binary_present=yes", line)

    def test_an_ambiguous_build_directory_still_reports_binary_missing(self) -> None:
        with scratch_directory() as root:
            build_directory = root / "bin"
            build_directory.mkdir()
            (build_directory / "foundry.macos.editor.dev.arm64.san").write_bytes(b"one")
            (build_directory / "foundry.macos.editor.dev.x86_64").write_bytes(b"another")
            scons = fake_scons(root, exit_code=0, lines=["scons: done building targets."])
            exit_code, log_path, _, _ = self.run_wrapper(
                root, scons, binary_path=build_directory / "foundry.macos.editor.dev.arm64"
            )
            line = self.result_line(log_path)
        self.assertEqual(exit_code, 1)
        self.assertTrue(line.startswith(f"{RESULT_PREFIX} binary-missing"), line)

    def test_an_unlaunchable_test_command_still_reports_a_result(self) -> None:
        with scratch_directory() as root:
            binary_path = root / "foundry.macos.editor.dev.arm64"
            binary_path.write_text("#!/bin/sh\nexit 0\n", encoding="utf-8")
            binary_path.chmod(0o644)
            scons = fake_scons(root, exit_code=0, lines=["scons: done building targets."])
            exit_code, log_path, _, _ = self.run_wrapper(root, scons, binary_path=binary_path, extra_argv=["--test"])
            line = self.result_line(log_path)
        self.assertEqual(exit_code, 127)
        self.assertTrue(line.startswith(f"{RESULT_PREFIX} test-failure"), line)
        self.assertIn("step=test", line)

    def test_result_line_is_emitted_exactly_once(self) -> None:
        with scratch_directory() as root:
            failing = fake_scons(root, exit_code=2, lines=SHADOW_DIAGNOSTIC)
            _, failure_log, _, _ = self.run_wrapper(root, failing)
            self.assertEqual(len(self.result_lines(failure_log)), 1)

            binary_path = root / "foundry.macos.editor.dev.arm64"
            binary_path.write_bytes(b"linked editor binary")
            passing = fake_scons(root, exit_code=0, lines=["scons: done building targets."])
            _, success_log, _, _ = self.run_wrapper(
                root, passing, binary_path=binary_path, extra_argv=["--log", str(root / "success.log")]
            )
            self.assertEqual(len(self.result_lines(success_log)), 1)

    def test_an_interrupt_reports_a_defined_verdict(self) -> None:
        with scratch_directory() as root:
            log_path = root / "build.log"
            agent_build.RESULT_CONTEXT.step = "build"
            agent_build.RESULT_CONTEXT.binary_path = root / "foundry.macos.editor.dev.arm64"
            agent_build.RESULT_CONTEXT.invocation_id = "invocation-interrupt"
            agent_build.RESULT_CONTEXT.log_path = log_path
            agent_build.RESULT_CONTEXT.human_stream = io.StringIO()
            agent_build.RESULT_CONTEXT.progress_path = None
            with mock.patch.object(agent_build, "main", side_effect=KeyboardInterrupt):
                exit_code = agent_build.run([])
            line = self.result_line(log_path)
        self.assertEqual(exit_code, 130)
        self.assertTrue(line.startswith(f"{RESULT_PREFIX} interrupted"), line)
        self.assertIn("exit_code=130", line)


def run_end_for(records: list[dict[str, Any]], invocation_id: str) -> dict[str, Any] | None:
    """The selection the documented wait idiom performs: this invocation's terminal record."""
    matches = [
        record
        for record in records
        if record.get("event") == "run_end" and record.get("invocation_id") == invocation_id
    ]
    return matches[-1] if matches else None


class ProgressStreamIdentityTests(WrapperHarness):
    """A waiter must be able to tell this invocation's progress records from a previous run's."""

    def test_run_start_is_the_first_record_and_truncates_the_progress_file(self) -> None:
        with scratch_directory() as root:
            progress_path = root / "progress.jsonl"
            progress_path.write_text(
                json.dumps({"version": 1, "event": "build_summary", "invocation_id": "stale", "status": "success"})
                + "\n",
                encoding="utf-8",
            )
            binary_path = root / "foundry.macos.editor.dev.arm64"
            binary_path.write_bytes(b"linked editor binary")
            scons = fake_scons(root, exit_code=0, lines=["scons: done building targets."])
            self.run_wrapper(root, scons, binary_path=binary_path)
            records = progress_events(progress_path)
        self.assertEqual(records[0]["event"], "run_start")
        self.assertNotIn("stale", [record.get("invocation_id") for record in records])

    def test_append_progress_preserves_earlier_records(self) -> None:
        with scratch_directory() as root:
            progress_path = root / "progress.jsonl"
            progress_path.write_text(
                json.dumps({"version": 1, "event": "build_summary", "invocation_id": "stale"}) + "\n",
                encoding="utf-8",
            )
            binary_path = root / "foundry.macos.editor.dev.arm64"
            binary_path.write_bytes(b"linked editor binary")
            scons = fake_scons(root, exit_code=0, lines=["scons: done building targets."])
            self.run_wrapper(root, scons, binary_path=binary_path, extra_argv=["--append-progress"])
            records = progress_events(progress_path)
        self.assertEqual(records[0]["invocation_id"], "stale")
        self.assertEqual(records[1]["event"], "run_start")

    def test_missing_tooling_still_truncates_and_records_run_start_and_run_end(self) -> None:
        with scratch_directory() as root:
            progress_path = root / "progress.jsonl"
            progress_path.write_text(
                json.dumps({"version": 1, "event": "build_summary", "invocation_id": "stale"}) + "\n",
                encoding="utf-8",
            )
            with contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
                with mock.patch.object(agent_build, "scons_prefix", return_value=["scons"]):
                    with mock.patch.object(agent_build.shutil, "which", return_value=None):
                        exit_code = agent_build.main(
                            [
                                "--compiler-cache",
                                "ccache",
                                "--log",
                                str(root / "build.log"),
                                "--progress-file",
                                str(progress_path),
                                "--invocation-id",
                                "tooling-probe",
                            ]
                        )
            records = progress_events(progress_path)
        self.assertEqual(exit_code, 127)
        self.assertEqual([record["event"] for record in records], ["run_start", "run_end"])
        self.assertEqual(records[-1]["status"], "tooling-missing")
        self.assertEqual(records[-1]["exit_code"], 127)
        self.assertEqual({record["invocation_id"] for record in records}, {"tooling-probe"})

    def test_supplied_invocation_id_is_used_for_every_record(self) -> None:
        with scratch_directory() as root:
            binary_path = root / "foundry.macos.editor.dev.arm64"
            binary_path.write_bytes(b"linked editor binary")
            scons = fake_scons(root, exit_code=0, lines=["scons: done building targets."])
            _, _, progress_path, _ = self.run_wrapper(
                root, scons, binary_path=binary_path, extra_argv=["--invocation-id", "fixed-token"]
            )
            records = progress_events(progress_path)
        self.assertIn("build_summary", [record["event"] for record in records])
        self.assertEqual({record["invocation_id"] for record in records}, {"fixed-token"})

    def test_startup_line_announces_invocation_and_paths(self) -> None:
        with scratch_directory() as root:
            binary_path = root / "foundry.macos.editor.dev.arm64"
            binary_path.write_bytes(b"linked editor binary")
            scons = fake_scons(root, exit_code=0, lines=["scons: done building targets."])
            _, log_path, progress_path, stdout = self.run_wrapper(
                root, scons, binary_path=binary_path, extra_argv=["--invocation-id", "announced-token"]
            )
        expected = f"[agent-build] invocation: announced-token progress: {progress_path} log: {log_path}"
        self.assertEqual(
            [line for line in stdout.splitlines() if line.startswith("[agent-build] invocation:")], [expected]
        )

    def test_run_end_records_binary_identity_after_a_successful_build(self) -> None:
        with scratch_directory() as root:
            binary_path = root / "foundry.macos.editor.dev.arm64"
            scons = linking_fake_scons(root, binary_path, b"freshly linked editor binary")
            _, _, progress_path, _ = self.run_wrapper(
                root, scons, binary_path=binary_path, extra_argv=["--invocation-id", "linking"]
            )
            records = progress_events(progress_path)
            stats = binary_path.stat()
        run_end = run_end_for(records, "linking")
        assert run_end is not None
        self.assertEqual(run_end["status"], "success")
        self.assertEqual(
            run_end["binary_after"], {"path": str(binary_path), "size": stats.st_size, "mtime_ns": stats.st_mtime_ns}
        )
        self.assertTrue(run_end["binary_changed"])

    def test_run_end_reports_unchanged_binary_on_a_no_op_build(self) -> None:
        with scratch_directory() as root:
            binary_path = root / "foundry.macos.editor.dev.arm64"
            binary_path.write_bytes(b"already linked editor binary")
            scons = fake_scons(root, exit_code=0, lines=["scons: `.' is up to date."])
            _, _, progress_path, _ = self.run_wrapper(
                root, scons, binary_path=binary_path, extra_argv=["--invocation-id", "no-op"]
            )
            run_end = run_end_for(progress_events(progress_path), "no-op")
        assert run_end is not None
        self.assertEqual(run_end["status"], "success")
        self.assertFalse(run_end["binary_changed"])

    def test_run_end_reports_unchanged_binary_when_the_build_resolves_a_renamed_one(self) -> None:
        with scratch_directory() as root:
            build_directory = root / "bin"
            build_directory.mkdir()
            renamed = build_directory / "foundry.macos.editor.dev.arm64.san"
            renamed.write_bytes(b"already linked editor binary")
            scons = fake_scons(root, exit_code=0, lines=["scons: `.' is up to date."])
            _, _, progress_path, _ = self.run_wrapper(
                root,
                scons,
                binary_path=build_directory / "foundry.macos.editor.dev.arm64",
                extra_argv=["--invocation-id", "renamed-no-op"],
            )
            run_end = run_end_for(progress_events(progress_path), "renamed-no-op")
        assert run_end is not None
        self.assertEqual(run_end["status"], "success")
        self.assertEqual(run_end["binary_path"], str(renamed))
        self.assertEqual(run_end["binary_before"], run_end["binary_after"])
        self.assertFalse(run_end["binary_changed"])

    def test_run_end_is_last_and_singular_under_test_phase(self) -> None:
        with scratch_directory() as root:
            binary_path = executable_stub(root / "foundry.macos.editor.dev.arm64", 0)
            scons = fake_scons(root, exit_code=0, lines=["scons: done building targets."])
            _, _, progress_path, _ = self.run_wrapper(root, scons, binary_path=binary_path, extra_argv=["--test"])
            events = [record["event"] for record in progress_events(progress_path)]
        self.assertEqual(events[-1], "run_end")
        self.assertEqual(events.count("run_end"), 1)
        self.assertLess(events.index("build_summary"), events.index("run_end"))

    def test_run_end_reports_test_failure_after_a_successful_build(self) -> None:
        with scratch_directory() as root:
            binary_path = executable_stub(root / "foundry.macos.editor.dev.arm64", 7)
            scons = fake_scons(root, exit_code=0, lines=["scons: done building targets."])
            _, _, progress_path, _ = self.run_wrapper(
                root, scons, binary_path=binary_path, extra_argv=["--test", "--invocation-id", "failing-tests"]
            )
            records = progress_events(progress_path)
        run_end = run_end_for(records, "failing-tests")
        assert run_end is not None
        self.assertEqual(run_end["status"], "test-failure")
        self.assertEqual(run_end["exit_code"], 7)
        self.assertEqual(sole_build_summary(records)["status"], "success")

    def test_build_summary_follows_the_linked_binary(self) -> None:
        with scratch_directory() as root:
            binary_path = root / "foundry.macos.editor.dev.arm64"
            scons = linking_fake_scons(root, binary_path, b"linked editor binary", settle_seconds=0.05)
            _, _, progress_path, _ = self.run_wrapper(root, scons, binary_path=binary_path)
            summary = sole_build_summary(progress_events(progress_path))
            linked_at = binary_path.stat().st_mtime_ns
        summary_at = datetime.fromisoformat(str(summary["timestamp"]).replace("Z", "+00:00"))
        self.assertGreaterEqual(int(summary_at.timestamp() * 1_000_000_000), linked_at)
        self.assertEqual(summary["binary_after"]["mtime_ns"], linked_at)

    def test_an_unresolved_binary_is_reported_as_absent(self) -> None:
        with scratch_directory() as root:
            progress_path = root / "progress.jsonl"
            log_path = root / "build.log"
            stdout = io.StringIO()
            with contextlib.redirect_stdout(stdout), contextlib.redirect_stderr(io.StringIO()):
                with mock.patch.object(
                    agent_build, "host_scons_platform", side_effect=RuntimeError("unsupported host")
                ):
                    exit_code = agent_build.main(
                        [
                            "--log",
                            str(log_path),
                            "--progress-file",
                            str(progress_path),
                            "--invocation-id",
                            "unsupported-host",
                        ]
                    )
            run_end = run_end_for(progress_events(progress_path), "unsupported-host")
            result_line = self.result_line(log_path)
        assert run_end is not None
        self.assertEqual(exit_code, 127)
        self.assertEqual(run_end["status"], "tooling-missing")
        self.assertIsNone(run_end["binary_after"])
        self.assertFalse(run_end["binary_changed"])
        self.assertIn("binary_present=no", result_line)

    def test_a_generation_launch_failure_is_reported_against_the_generate_step(self) -> None:
        with scratch_directory() as root:
            state_directory = root / "state"
            state_directory.write_text("not a directory\n", encoding="utf-8")
            state = agent_build.NinjaState(state_directory, state_directory / "build.ninja")
            progress_path = root / "progress.jsonl"
            log_path = root / "build.log"
            with contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
                with mock.patch.object(agent_build, "scons_prefix", return_value=["scons"]):
                    with mock.patch.object(agent_build.shutil, "which", side_effect=lambda name: f"/bin/{name}"):
                        with mock.patch.object(agent_build, "resolve_ninja_state", return_value=state):
                            with mock.patch.object(agent_build, "read_ccache_stats_best_effort", return_value={}):
                                exit_code = agent_build.main(
                                    [
                                        "--backend",
                                        "ninja",
                                        "--platform",
                                        "macos",
                                        "--log",
                                        str(log_path),
                                        "--progress-file",
                                        str(progress_path),
                                        "--invocation-id",
                                        "generation-launch",
                                    ]
                                )
            run_end = run_end_for(progress_events(progress_path), "generation-launch")
            result_line = self.result_line(log_path)
        assert run_end is not None
        self.assertEqual(exit_code, 127)
        self.assertEqual(run_end["status"], "generation-failure")
        self.assertEqual(run_end["step"], "generate")
        self.assertIn("step=generate", result_line)

    def test_documented_wait_snippet_selects_only_the_matching_invocation(self) -> None:
        records: list[dict[str, Any]] = [
            {"event": "run_start", "invocation_id": "first"},
            {"event": "build_summary", "invocation_id": "first", "status": "success"},
            {"event": "run_end", "invocation_id": "first", "status": "success", "exit_code": 0},
            {"event": "run_start", "invocation_id": "second"},
            {"event": "run_end", "invocation_id": "second", "status": "build-failure", "exit_code": 2},
        ]
        self.assertEqual(run_end_for(records, "second"), records[-1])
        self.assertEqual(run_end_for(records, "first"), records[2])
        self.assertIsNone(run_end_for(records, "third"))


if __name__ == "__main__":
    unittest.main()
