"""GitHub side effects through the ``gh`` CLI with an injectable command runner.

The client never touches a protected branch and never enables auto-merge or merges a pull request.
"""

from __future__ import annotations

import json
import subprocess
from pathlib import Path
from typing import Callable, List, Optional

PROTECTED_BRANCHES = ("develop", "main", "master")
BOT_BRANCH_PREFIX = "bot/type-completeness/"
TRACKING_LABEL = "type-completeness-finding"
FORBIDDEN_ARGUMENTS = ("--auto", "merge")

CommandRunner = Callable[[List[str]], str]


class ProtectedBranchError(RuntimeError):
    pass


def run_command(arguments: list[str]) -> str:
    completed = subprocess.run(arguments, check=True, capture_output=True, text=True)
    return completed.stdout


def bot_branch_name(finding_id: str) -> str:
    return BOT_BRANCH_PREFIX + finding_id


class AutomationClient:
    def __init__(self, run: CommandRunner, repository: str) -> None:
        self._run = run
        self._repository = repository

    def _gh(self, *arguments: str) -> str:
        command = ["gh", *arguments]
        for forbidden in FORBIDDEN_ARGUMENTS:
            if forbidden in command:
                raise RuntimeError(f"automation must never invoke gh with {forbidden!r}")
        return self._run(command)

    def push_ledger_branch(self, branch: str, repository_root: Path) -> None:
        if branch in PROTECTED_BRANCHES or not branch.startswith(BOT_BRANCH_PREFIX):
            raise ProtectedBranchError(
                f"refusing to push to {branch!r}; only {BOT_BRANCH_PREFIX}* branches are allowed"
            )
        self._run(
            ["git", "-C", str(repository_root), "push", "--force-with-lease", "origin", f"HEAD:refs/heads/{branch}"]
        )

    def _find_pull_request(self, branch: str) -> Optional[tuple[int, str]]:
        output = self._gh(
            "pr", "list", "--repo", self._repository, "--head", branch, "--state", "open", "--json", "url,number"
        )
        entries = json.loads(output or "[]")
        return (int(entries[0]["number"]), str(entries[0]["url"])) if entries else None

    def open_or_update_ledger_pull_request(self, branch: str, title: str, body: str, base: str) -> str:
        if branch in PROTECTED_BRANCHES:
            raise ProtectedBranchError(f"refusing to open a ledger pull request from {branch!r}")
        existing = self._find_pull_request(branch)
        if existing is not None:
            number, url = existing
            self._gh("pr", "edit", str(number), "--repo", self._repository, "--title", title, "--body", body)
            return url
        return self._gh(
            "pr",
            "create",
            "--repo",
            self._repository,
            "--head",
            branch,
            "--base",
            base,
            "--title",
            title,
            "--body",
            body,
        ).strip()

    def _find_tracking_issue(self, finding_id: str) -> Optional[tuple[int, str]]:
        output = self._gh(
            "issue",
            "list",
            "--repo",
            self._repository,
            "--label",
            TRACKING_LABEL,
            "--state",
            "open",
            "--search",
            f'"{finding_id}" in:title',
            "--json",
            "url,number",
        )
        entries = json.loads(output or "[]")
        return (int(entries[0]["number"]), str(entries[0]["url"])) if entries else None

    def create_or_update_tracking_issue(self, finding_id: str, title: str, body: str) -> str:
        existing = self._find_tracking_issue(finding_id)
        if existing is not None:
            number, url = existing
            self._gh("issue", "edit", str(number), "--repo", self._repository, "--title", title, "--body", body)
            return url
        return self._gh(
            "issue", "create", "--repo", self._repository, "--title", title, "--body", body, "--label", TRACKING_LABEL
        ).strip()
