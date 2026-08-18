"""Behavior of the static-checks file-scope resolver."""

from __future__ import annotations

import subprocess
import sys
import unittest
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(REPO_ROOT / ".github/scripts"))

import resolve_hook_scope  # noqa: E402

SCRIPT = REPO_ROOT / ".github/scripts/resolve_hook_scope.py"


class ResolveHookScopeTests(unittest.TestCase):
    def test_a_pull_request_is_checked_over_its_own_diff(self) -> None:
        self.assertEqual(
            "--files './core/object/object.cpp' './doc/classes/Node.xml'",
            resolve_hook_scope.resolve_hook_scope("pull_request", ["core/object/object.cpp", "doc/classes/Node.xml"]),
        )

    def test_a_push_sweeps_the_whole_tree(self) -> None:
        self.assertEqual(
            resolve_hook_scope.ALL_FILES,
            resolve_hook_scope.resolve_hook_scope("push", ["core/object/object.cpp"]),
        )

    def test_a_scheduled_run_sweeps_the_whole_tree(self) -> None:
        self.assertEqual(resolve_hook_scope.ALL_FILES, resolve_hook_scope.resolve_hook_scope("schedule", []))

    def test_a_manual_dispatch_sweeps_the_whole_tree(self) -> None:
        self.assertEqual(resolve_hook_scope.ALL_FILES, resolve_hook_scope.resolve_hook_scope("workflow_dispatch", []))

    def test_blank_diff_lines_are_dropped(self) -> None:
        self.assertEqual(
            "--files './main/main.cpp'",
            resolve_hook_scope.resolve_hook_scope("pull_request", ["", "main/main.cpp", ""]),
        )

    def test_a_path_with_edge_whitespace_is_passed_through_verbatim(self) -> None:
        # Git allows a pathname with leading or trailing spaces; trimming one would
        # name a file that does not exist and leave the changed file unchecked.
        self.assertEqual(
            "--files './ spaced.txt '",
            resolve_hook_scope.resolve_hook_scope("pull_request", [" spaced.txt "]),
        )

    def test_a_pull_request_with_no_changed_files_falls_back_to_the_whole_tree(self) -> None:
        # `--files` with no operand is an error, so the degenerate diff sweeps instead
        # of producing an argument list that would fail the run for the wrong reason.
        self.assertEqual(resolve_hook_scope.ALL_FILES, resolve_hook_scope.resolve_hook_scope("pull_request", [""]))

    def test_a_path_containing_a_space_stays_one_argument(self) -> None:
        self.assertEqual(
            "--files './misc/dist/my file.txt'",
            resolve_hook_scope.resolve_hook_scope("pull_request", ["misc/dist/my file.txt"]),
        )

    def test_a_path_the_argument_splitter_cannot_carry_sweeps_the_whole_tree(self) -> None:
        # `string-argv` has no escape syntax, so a quote or a backslash in a path
        # cannot be expressed; sweeping everything checks that file instead of
        # silently dropping it from the gate.
        for path in ("misc/dist/it's.txt", 'misc/dist/say "hi".txt', "misc/dist/back\\slash.txt"):
            with self.subTest(path=path):
                self.assertEqual(
                    resolve_hook_scope.ALL_FILES,
                    resolve_hook_scope.resolve_hook_scope("pull_request", ["main/main.cpp", path]),
                )

    def test_the_command_line_reads_the_diff_from_standard_input(self) -> None:
        completed = subprocess.run(
            [sys.executable, str(SCRIPT), "--event-name", "pull_request"],
            input="main/main.cpp\nSConstruct\n",
            capture_output=True,
            text=True,
            check=True,
        )
        self.assertEqual("--files './main/main.cpp' './SConstruct'", completed.stdout.strip())

    def test_the_command_line_sweeps_the_whole_tree_for_a_schedule(self) -> None:
        completed = subprocess.run(
            [sys.executable, str(SCRIPT), "--event-name", "schedule"],
            input="",
            capture_output=True,
            text=True,
            check=True,
        )
        self.assertEqual(resolve_hook_scope.ALL_FILES, completed.stdout.strip())


if __name__ == "__main__":
    unittest.main()
