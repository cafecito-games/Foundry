# This file is part of Foundry Engine - https://www.cafecito.games/
# Foundry Engine is a fork of the Godot Engine; see NOTICE.
# Copyright (c) 2026-present Cafecito Games LLC. MIT License.

"""Unit tests for misc/checks/check_file_policy.py.

The runner's decision logic is pure: `load_rules` turns TOML text into rules and
`find_policy_violations` evaluates them against a directory tree. These tests
drive both against synthetic fixture trees, so every failure mode is proven
without depending on the real repository contents.
"""

from __future__ import annotations

import subprocess
import sys
import tempfile
import textwrap
import unittest
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(REPO_ROOT / "misc/checks"))

import check_file_policy  # noqa: E402

CHECK_SCRIPT = REPO_ROOT / "misc/checks/check_file_policy.py"
POLICY_TABLE = REPO_ROOT / "misc/checks/file_policy.toml"


class FilePolicyRunnerTests(unittest.TestCase):
    def build_tree(self, files: dict[str, str]) -> Path:
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        root = Path(temporary.name)
        for relative, contents in files.items():
            path = root / relative
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(contents, encoding="utf-8")
        return root

    def evaluate(self, policy: str, files: dict[str, str]) -> list[str]:
        rules = check_file_policy.load_rules(textwrap.dedent(policy))
        return check_file_policy.find_policy_violations(rules, self.build_tree(files))

    def test_required_token_present_passes(self) -> None:
        violations = self.evaluate(
            """
            [[rule]]
            name = "keeps-the-entry-point"
            reason = "The launcher activity is the only documented entry point."
            paths = ["app/Manifest.xml"]
            required = ["FoundryAppLauncher"]
            """,
            {"app/Manifest.xml": "<activity android:name='.FoundryAppLauncher' />\n"},
        )

        self.assertEqual([], violations)

    def test_required_token_absent_is_reported_with_path_and_rule_name(self) -> None:
        violations = self.evaluate(
            """
            [[rule]]
            name = "keeps-the-entry-point"
            reason = "The launcher activity is the only documented entry point."
            paths = ["app/Manifest.xml"]
            required = ["FoundryAppLauncher"]
            """,
            {"app/Manifest.xml": "<activity android:name='.Other' />\n"},
        )

        self.assertEqual(1, len(violations))
        self.assertIn("app/Manifest.xml", violations[0])
        self.assertIn("keeps-the-entry-point", violations[0])
        self.assertIn("FoundryAppLauncher", violations[0])
        self.assertIn("The launcher activity is the only documented entry point.", violations[0])

    def test_forbidden_token_present_is_reported(self) -> None:
        violations = self.evaluate(
            """
            [[rule]]
            name = "no-legacy-plugin-model"
            reason = "The legacy Android plugin model was removed."
            paths = ["android/*.kt"]
            forbidden = ["FoundryPluginRegistry"]
            """,
            {"android/Host.kt": "class Host { val registry = FoundryPluginRegistry() }\n"},
        )

        self.assertEqual(1, len(violations))
        self.assertIn("android/Host.kt", violations[0])
        self.assertIn("no-legacy-plugin-model", violations[0])
        self.assertIn("FoundryPluginRegistry", violations[0])
        self.assertIn("The legacy Android plugin model was removed.", violations[0])

    def test_forbidden_token_absent_passes(self) -> None:
        violations = self.evaluate(
            """
            [[rule]]
            name = "no-legacy-plugin-model"
            reason = "The legacy Android plugin model was removed."
            paths = ["android/*.kt"]
            forbidden = ["FoundryPluginRegistry"]
            """,
            {"android/Host.kt": "class Host\n"},
        )

        self.assertEqual([], violations)

    def test_required_path_missing_is_reported(self) -> None:
        violations = self.evaluate(
            """
            [[rule]]
            name = "host-runtime-stays-in-tree"
            reason = "Foundry owns its Android host runtime."
            required_paths = ["android/Foundry.kt"]
            """,
            {"android/Other.kt": "class Other\n"},
        )

        self.assertEqual(1, len(violations))
        self.assertIn("android/Foundry.kt", violations[0])
        self.assertIn("host-runtime-stays-in-tree", violations[0])
        self.assertIn("Foundry owns its Android host runtime.", violations[0])

    def test_forbidden_path_present_is_reported(self) -> None:
        violations = self.evaluate(
            """
            [[rule]]
            name = "publishing-stays-out-of-tree"
            reason = "Publication happens outside the engine repository."
            forbidden_paths = ["android/PUBLISHING.md"]
            """,
            {"android/PUBLISHING.md": "publish\n"},
        )

        self.assertEqual(1, len(violations))
        self.assertIn("android/PUBLISHING.md", violations[0])
        self.assertIn("publishing-stays-out-of-tree", violations[0])

    def test_forbidden_path_absent_passes(self) -> None:
        violations = self.evaluate(
            """
            [[rule]]
            name = "publishing-stays-out-of-tree"
            reason = "Publication happens outside the engine repository."
            forbidden_paths = ["android/PUBLISHING.md"]
            """,
            {"android/README.md": "read\n"},
        )

        self.assertEqual([], violations)

    def test_every_violation_is_reported_not_just_the_first(self) -> None:
        violations = self.evaluate(
            """
            [[rule]]
            name = "no-legacy-plugin-model"
            reason = "The legacy Android plugin model was removed."
            paths = ["android/*.kt"]
            forbidden = ["FoundryPluginRegistry", "AndroidRuntimePlugin"]

            [[rule]]
            name = "publishing-stays-out-of-tree"
            reason = "Publication happens outside the engine repository."
            forbidden_paths = ["android/PUBLISHING.md"]
            """,
            {
                "android/Host.kt": "FoundryPluginRegistry AndroidRuntimePlugin\n",
                "android/Other.kt": "AndroidRuntimePlugin\n",
                "android/PUBLISHING.md": "publish\n",
            },
        )

        self.assertEqual(4, len(violations))
        self.assertEqual(
            [
                "android/Host.kt: no-legacy-plugin-model",
                "android/Host.kt: no-legacy-plugin-model",
                "android/Other.kt: no-legacy-plugin-model",
                "android/PUBLISHING.md: publishing-stays-out-of-tree",
            ],
            sorted(": ".join(violation.split(": ")[:2]) for violation in violations),
        )

    def test_rule_matching_zero_files_is_an_error(self) -> None:
        # A glob that matches nothing silently converts an invariant into
        # decoration. It is the primary silent-failure mode of a data-driven
        # policy table, so it is a violation rather than a pass.
        violations = self.evaluate(
            """
            [[rule]]
            name = "no-legacy-plugin-model"
            reason = "The legacy Android plugin model was removed."
            paths = ["android/renamed/*.kt"]
            forbidden = ["FoundryPluginRegistry"]
            """,
            {"android/Host.kt": "class Host\n"},
        )

        self.assertEqual(1, len(violations))
        self.assertIn("android/renamed/*.kt", violations[0])
        self.assertIn("no-legacy-plugin-model", violations[0])
        self.assertIn("matched no files", violations[0])

    def test_every_unmatched_pattern_in_a_rule_is_reported(self) -> None:
        violations = self.evaluate(
            """
            [[rule]]
            name = "no-legacy-plugin-model"
            reason = "The legacy Android plugin model was removed."
            paths = ["android/gone/*.kt", "android/also-gone/*.kt", "android/*.kt"]
            forbidden = ["FoundryPluginRegistry"]
            """,
            {"android/Host.kt": "class Host\n"},
        )

        self.assertEqual(2, len(violations))
        self.assertTrue(all("matched no files" in violation for violation in violations))

    def test_a_directory_alone_does_not_satisfy_a_token_pattern(self) -> None:
        # Globbing a directory yields a path that cannot be scanned for tokens;
        # counting it as a match would reintroduce the vacuous-rule failure.
        violations = self.evaluate(
            """
            [[rule]]
            name = "no-legacy-plugin-model"
            reason = "The legacy Android plugin model was removed."
            paths = ["android/*"]
            forbidden = ["FoundryPluginRegistry"]
            """,
            {"android/nested/Host.kt": "class Host\n"},
        )

        self.assertEqual(1, len(violations))
        self.assertIn("matched no files", violations[0])

    def test_rule_without_a_reason_is_a_configuration_error(self) -> None:
        with self.assertRaisesRegex(check_file_policy.FilePolicyError, "reason"):
            check_file_policy.load_rules(
                textwrap.dedent(
                    """
                    [[rule]]
                    name = "no-legacy-plugin-model"
                    paths = ["android/*.kt"]
                    forbidden = ["FoundryPluginRegistry"]
                    """
                )
            )

    def test_rule_with_a_blank_reason_is_a_configuration_error(self) -> None:
        with self.assertRaisesRegex(check_file_policy.FilePolicyError, "reason"):
            check_file_policy.load_rules(
                textwrap.dedent(
                    """
                    [[rule]]
                    name = "no-legacy-plugin-model"
                    reason = "   "
                    paths = ["android/*.kt"]
                    forbidden = ["FoundryPluginRegistry"]
                    """
                )
            )

    def test_rule_without_a_name_is_a_configuration_error(self) -> None:
        with self.assertRaisesRegex(check_file_policy.FilePolicyError, "name"):
            check_file_policy.load_rules(
                textwrap.dedent(
                    """
                    [[rule]]
                    reason = "The legacy Android plugin model was removed."
                    paths = ["android/*.kt"]
                    forbidden = ["FoundryPluginRegistry"]
                    """
                )
            )

    def test_duplicate_rule_names_are_a_configuration_error(self) -> None:
        with self.assertRaisesRegex(check_file_policy.FilePolicyError, "duplicate"):
            check_file_policy.load_rules(
                textwrap.dedent(
                    """
                    [[rule]]
                    name = "same"
                    reason = "First."
                    paths = ["a.txt"]
                    required = ["a"]

                    [[rule]]
                    name = "same"
                    reason = "Second."
                    paths = ["b.txt"]
                    required = ["b"]
                    """
                )
            )

    def test_rule_asserting_nothing_is_a_configuration_error(self) -> None:
        with self.assertRaisesRegex(check_file_policy.FilePolicyError, "asserts nothing"):
            check_file_policy.load_rules(
                textwrap.dedent(
                    """
                    [[rule]]
                    name = "empty"
                    reason = "Nothing at all."
                    paths = ["a.txt"]
                    """
                )
            )

    def test_token_rule_without_paths_is_a_configuration_error(self) -> None:
        with self.assertRaisesRegex(check_file_policy.FilePolicyError, "paths"):
            check_file_policy.load_rules(
                textwrap.dedent(
                    """
                    [[rule]]
                    name = "unscoped"
                    reason = "Tokens need files to look in."
                    required = ["a"]
                    """
                )
            )

    def test_unknown_rule_key_is_a_configuration_error(self) -> None:
        # The schema is deliberately closed: a typo'd or invented key such as
        # `regex` must fail loudly instead of being ignored.
        with self.assertRaisesRegex(check_file_policy.FilePolicyError, "regex"):
            check_file_policy.load_rules(
                textwrap.dedent(
                    """
                    [[rule]]
                    name = "invented"
                    reason = "Regex is not part of the schema."
                    paths = ["a.txt"]
                    regex = ["a.*b"]
                    """
                )
            )

    def test_malformed_policy_text_fails_with_a_clear_message(self) -> None:
        with self.assertRaisesRegex(check_file_policy.FilePolicyError, "is not valid TOML"):
            check_file_policy.load_rules("[[rule]\nname = 'broken'\n")

    def test_glob_expansion_covers_nested_directories(self) -> None:
        violations = self.evaluate(
            """
            [[rule]]
            name = "no-legacy-plugin-model"
            reason = "The legacy Android plugin model was removed."
            paths = ["android/**/*.kt"]
            forbidden = ["FoundryPluginRegistry"]
            """,
            {
                "android/Host.kt": "class Host\n",
                "android/a/b/Deep.kt": "FoundryPluginRegistry\n",
                "android/a/b/Clean.kt": "class Clean\n",
            },
        )

        self.assertEqual(1, len(violations))
        self.assertIn("android/a/b/Deep.kt", violations[0])

    def test_generated_output_directories_are_never_scanned(self) -> None:
        # Gradle and SCons write sources into `build/` trees that mirror real
        # ones. Scanning them would make the check depend on whether someone
        # happened to build locally.
        violations = self.evaluate(
            """
            [[rule]]
            name = "no-legacy-plugin-model"
            reason = "The legacy Android plugin model was removed."
            paths = ["android/**/*.kt"]
            forbidden = ["FoundryPluginRegistry"]
            """,
            {
                "android/Host.kt": "class Host\n",
                "android/build/generated/Host.kt": "FoundryPluginRegistry\n",
            },
        )

        self.assertEqual([], violations)

    def test_exit_code_is_zero_only_when_every_rule_passes(self) -> None:
        policy = textwrap.dedent(
            """
            [[rule]]
            name = "no-legacy-plugin-model"
            reason = "The legacy Android plugin model was removed."
            paths = ["android/*.kt"]
            forbidden = ["FoundryPluginRegistry"]
            """
        )
        clean = self.build_tree({"android/Host.kt": "class Host\n"})
        dirty = self.build_tree({"android/Host.kt": "FoundryPluginRegistry\n"})
        policy_path = clean / "policy.toml"
        policy_path.write_text(policy, encoding="utf-8")

        self.assertEqual(0, check_file_policy.main(["--policy", str(policy_path), "--root", str(clean)]))
        self.assertEqual(1, check_file_policy.main(["--policy", str(policy_path), "--root", str(dirty)]))

    def test_configuration_error_exits_non_zero_without_a_traceback(self) -> None:
        root = self.build_tree({"android/Host.kt": "class Host\n"})
        policy_path = root / "policy.toml"
        policy_path.write_text("[[rule]]\nname = 'no-reason'\npaths = ['a']\nrequired = ['a']\n", encoding="utf-8")

        result = subprocess.run(
            [sys.executable, str(CHECK_SCRIPT), "--policy", str(policy_path), "--root", str(root)],
            capture_output=True,
            text=True,
            check=False,
        )

        self.assertEqual(2, result.returncode)
        self.assertNotIn("Traceback", result.stderr)
        self.assertIn("reason", result.stderr)


class RepositoryPolicyTableTests(unittest.TestCase):
    """The checked-in table is itself data that can be wrong."""

    def test_checked_in_policy_table_loads(self) -> None:
        rules = check_file_policy.load_rules(POLICY_TABLE.read_text(encoding="utf-8"))

        self.assertNotEqual([], rules)

    def test_every_checked_in_rule_states_a_reason(self) -> None:
        for rule in check_file_policy.load_rules(POLICY_TABLE.read_text(encoding="utf-8")):
            with self.subTest(rule=rule.name):
                self.assertTrue(rule.reason.strip())

    def test_checked_in_policy_table_passes_against_this_repository(self) -> None:
        rules = check_file_policy.load_rules(POLICY_TABLE.read_text(encoding="utf-8"))

        self.assertEqual([], check_file_policy.find_policy_violations(rules, REPO_ROOT))


if __name__ == "__main__":
    unittest.main()
