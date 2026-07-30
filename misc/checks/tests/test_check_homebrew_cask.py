# This file is part of Foundry Engine - https://www.cafecito.games/
# Foundry Engine is a fork of the Godot Engine; see NOTICE.
# Copyright (c) 2026-present Cafecito Games LLC. MIT License.

"""Unit tests for misc/checks/check_homebrew_cask.py.

These drive the check's pure release-workflow scan with synthetic text so each
violation class is proven to fire without running a release.
"""

from __future__ import annotations

import sys
import unittest
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(REPO_ROOT / "misc/checks"))

import check_homebrew_cask  # noqa: E402

# Minimal workflow text carrying every snippet the check requires, in the order
# the check requires, and none of the snippets it forbids.
CLEAN_WORKFLOW = """\
      - name: Checkout
        uses: actions/checkout@v6
      - name: Update Homebrew tap
        if: needs.resolve.outputs.draft == 'false'
        env:
          HOMEBREW_TAP_TOKEN: ${{ secrets.HOMEBREW_TAP_TOKEN }}
          RELEASE_VERSION: ${{ needs.resolve.outputs.release_version }}
          RELEASE_STATUS: ${{ needs.resolve.outputs.status }}
        run: |
          macos_asset="Foundry_v${RELEASE_VERSION}_macos.universal.zip"
          linux_asset="Foundry_v${RELEASE_VERSION}_linux.x86_64.zip"
          git clone https://x-access-token:${HOMEBREW_TAP_TOKEN}@github.com/cafecito-games/homebrew-tap.git "$tap_dir"
          cask_path="$(python3 .github/scripts/generate_homebrew_cask.py \\
            --macos-asset "$macos_asset" \\
            --linux-asset "$linux_asset")"
          ruby -c "$cask_path"
          brew tap cafecito-games/tap "$tap_dir"
          brew_tap_dir="$(brew --repository cafecito-games/tap)"
          mkdir -p "$brew_tap_dir/Casks"
          cp "$cask_path" "$brew_tap_dir/Casks/${cask_token}.rb"
          brew audit --cask --tap cafecito-games/tap "$cask_token"
          brew untap cafecito-games/tap
          git status --porcelain -- "$cask_path"
          git commit -m "Brew cask update for ${cask_token} version v${RELEASE_VERSION}"
          git push origin HEAD:main
"""


class WorkflowWiringTests(unittest.TestCase):
    def test_clean_workflow_reports_no_violations(self) -> None:
        self.assertEqual(check_homebrew_cask.find_workflow_wiring_violations(CLEAN_WORKFLOW), [])

    def test_missing_required_wiring_is_reported(self) -> None:
        workflow = CLEAN_WORKFLOW.replace("          git push origin HEAD:main\n", "")

        violations = check_homebrew_cask.find_workflow_wiring_violations(workflow)

        self.assertEqual(len(violations), 1)
        self.assertIn("missing Homebrew tap wiring", violations[0])
        self.assertIn("git push origin HEAD:main", violations[0])

    def test_reintroduced_broken_wiring_is_reported(self) -> None:
        workflow = CLEAN_WORKFLOW.replace(
            '          brew audit --cask --tap cafecito-games/tap "$cask_token"\n',
            '          brew audit --cask --tap cafecito-games/tap "$cask_token"\n'
            '          git diff --quiet -- "$cask_path"\n',
        )

        violations = check_homebrew_cask.find_workflow_wiring_violations(workflow)

        self.assertEqual(len(violations), 1)
        self.assertIn("still contains broken Homebrew tap wiring", violations[0])
        self.assertIn('git diff --quiet -- "$cask_path"', violations[0])

    def test_auditing_before_copying_the_cask_is_reported(self) -> None:
        audit = '          brew audit --cask --tap cafecito-games/tap "$cask_token"\n'
        copy = '          cp "$cask_path" "$brew_tap_dir/Casks/${cask_token}.rb"\n'
        workflow = CLEAN_WORKFLOW.replace(copy + audit, audit + copy)

        violations = check_homebrew_cask.find_workflow_wiring_violations(workflow)

        self.assertEqual(len(violations), 1)
        self.assertIn("audits the cask before copying", violations[0])

    def test_every_violation_is_reported_not_just_the_first(self) -> None:
        workflow = CLEAN_WORKFLOW.replace("          git push origin HEAD:main\n", "")
        workflow = workflow.replace(
            '          brew audit --cask --tap cafecito-games/tap "$cask_token"\n',
            '          brew audit --cask --tap cafecito-games/tap "$cask_token"\n'
            '          git diff --quiet -- "$cask_path"\n',
        )

        violations = check_homebrew_cask.find_workflow_wiring_violations(workflow)

        self.assertEqual(len(violations), 2)
        self.assertIn("missing Homebrew tap wiring", violations[0])
        self.assertIn("still contains broken Homebrew tap wiring", violations[1])

    def test_empty_workflow_reports_the_missing_wiring_rather_than_raising(self) -> None:
        violations = check_homebrew_cask.find_workflow_wiring_violations("")

        self.assertEqual(len(violations), 1)
        self.assertIn("missing Homebrew tap wiring", violations[0])

    def test_the_real_release_workflow_satisfies_the_check(self) -> None:
        workflow = (REPO_ROOT / ".github/workflows/release.yml").read_text(encoding="utf-8")

        self.assertEqual(check_homebrew_cask.find_workflow_wiring_violations(workflow), [])


if __name__ == "__main__":
    unittest.main()
