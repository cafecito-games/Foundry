"""GitHub side effects through the ``gh`` CLI with an injectable command runner.

The client never touches a protected branch and never enables auto-merge or merges a pull request.
"""

from __future__ import annotations

import json
import subprocess
from pathlib import Path
from typing import Any, Callable, List, Optional

PROTECTED_BRANCHES = ("develop", "main", "master")
BOT_BRANCH_PREFIX = "bot/type-completeness/"
TRACKING_LABEL = "type-completeness-finding"
FORBIDDEN_ARGUMENTS = ("--auto", "merge")
PULL_REQUEST_PRECEDENCE = {"OPEN": 0, "MERGED": 1, "CLOSED": 2}

CommandRunner = Callable[[List[str]], str]


class ProtectedBranchError(RuntimeError):
    pass


class AutomationError(RuntimeError):
    pass


def _state_of(entry: dict[str, Any], allowed: tuple[str, ...], kind: str) -> str:
    state = str(entry.get("state", "")).upper()
    if state not in allowed:
        raise AutomationError(f"gh reported {kind} #{entry.get('number')} with unexpected state {state!r}")
    return state


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

    def _require_bot_branch(self, branch: str) -> str:
        if branch in PROTECTED_BRANCHES or not branch.startswith(BOT_BRANCH_PREFIX):
            raise ProtectedBranchError(f"refusing to touch {branch!r}; only {BOT_BRANCH_PREFIX}* branches are allowed")
        return f"refs/heads/{branch}"

    def observe_remote_branch(self, branch: str, repository_root: Path) -> Optional[str]:
        """The SHA the bot branch has on origin right now, or None when it does not exist yet.

        Record this before doing any work and hand it to push_ledger_branch as the lease so a push can only
        replace the commit the automation actually observed.
        """
        ref = self._require_bot_branch(branch)
        output = self._run(["git", "-C", str(repository_root), "ls-remote", "--heads", "origin", ref])
        for line in output.splitlines():
            sha, _, listed_ref = line.strip().partition("\t")
            if listed_ref == ref and sha:
                return sha
        return None

    def push_ledger_branch(self, branch: str, repository_root: Path, *, expected_sha: Optional[str]) -> None:
        """Push HEAD to the bot branch only if origin still holds ``expected_sha`` (None: the branch must not
        exist). A rejected lease is an error; it is never retried after re-fetching, because a newer reviewed
        commit may have landed on the branch."""
        ref = self._require_bot_branch(branch)
        lease = f"--force-with-lease={ref}:{expected_sha or ''}"
        try:
            self._run(["git", "-C", str(repository_root), "push", lease, "origin", f"HEAD:{ref}"])
        except subprocess.CalledProcessError as error:
            current = self.observe_remote_branch(branch, repository_root)
            raise AutomationError(
                f"push to {branch!r} rejected: expected origin at {expected_sha or '<absent>'} but it is now at "
                f"{current or '<absent>'}; not retrying because a newer commit may be a reviewed one "
                f"({(error.stderr or '').strip()})"
            ) from error

    def _list_pull_requests(self, branch: str) -> list[dict[str, Any]]:
        output = self._gh(
            "pr",
            "list",
            "--repo",
            self._repository,
            "--head",
            branch,
            "--state",
            "all",
            "--json",
            "url,number,state,baseRefName",
        )
        entries = json.loads(output or "[]")
        for entry in entries:
            entry["state"] = _state_of(entry, ("OPEN", "CLOSED", "MERGED"), "pull request")
            if not isinstance(entry.get("baseRefName"), str) or not entry["baseRefName"]:
                raise AutomationError(f"gh reported pull request #{entry.get('number')} without a base branch")
        # OPEN is the live proposal; MERGED is authoritative and must never be shadowed by an older CLOSED one.
        entries.sort(key=lambda entry: (PULL_REQUEST_PRECEDENCE[entry["state"]], int(entry["number"])))
        return list(entries)

    def open_or_update_ledger_pull_request(self, branch: str, title: str, body: str, base: str) -> str:
        if branch in PROTECTED_BRANCHES or not branch.startswith(BOT_BRANCH_PREFIX):
            raise ProtectedBranchError(
                f"refusing to open or update a pull request for {branch!r}; only {BOT_BRANCH_PREFIX}* branches are allowed"
            )
        entries = self._list_pull_requests(branch)
        matching = [entry for entry in entries if entry["baseRefName"] == base]
        if matching:
            chosen = matching[0]
            number, url, state = int(chosen["number"]), str(chosen["url"]), chosen["state"]
            if state == "MERGED":
                # The ledger entry already landed; the merged record is the source of truth now.
                return url
            if state != "OPEN":
                # One ledger pull request per finding: a closed unmerged one is reopened rather than duplicated.
                self._gh("pr", "reopen", str(number), "--repo", self._repository)
            self._gh("pr", "edit", str(number), "--repo", self._repository, "--title", title, "--body", body)
            return url
        # A pull request from this bot branch to another base is never silently reused: retarget the only open
        # one, refuse when several are open, and otherwise open a fresh one against the requested base.
        other_open = [entry for entry in entries if entry["state"] == "OPEN"]
        if len(other_open) > 1:
            raise AutomationError(
                f"bot branch {branch!r} has several open pull requests to other bases: "
                f"{[entry['url'] for entry in other_open]}; resolve them by hand"
            )
        if other_open:
            chosen = other_open[0]
            self._gh(
                "pr",
                "edit",
                str(chosen["number"]),
                "--repo",
                self._repository,
                "--base",
                base,
                "--title",
                title,
                "--body",
                body,
            )
            return str(chosen["url"])
        created = self._gh(
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
        return self._converge_pull_requests(branch, base, created)

    def _converge_pull_requests(self, branch: str, base: str, created_url: str) -> str:
        """GitHub has no create-if-absent, so a concurrent run may have created a second pull request between
        our lookup and our create. Keep the lowest-numbered open one against the same base and close the rest
        pointing at it; merged or closed ones and ones to other bases are never duplicates."""
        entries = sorted(
            (
                entry
                for entry in self._list_pull_requests(branch)
                if entry["state"] == "OPEN" and entry["baseRefName"] == base
            ),
            key=lambda entry: int(entry["number"]),
        )
        if len(entries) <= 1:
            return str(entries[0]["url"]) if entries else created_url
        survivor = entries[0]
        for duplicate in entries[1:]:
            self._gh(
                "pr",
                "close",
                str(duplicate["number"]),
                "--repo",
                self._repository,
                "--comment",
                f"Duplicate ledger pull request created concurrently; superseded by {survivor['url']}.",
            )
        return str(survivor["url"])

    def _find_tracking_issue(self, finding_id: str) -> Optional[tuple[int, str, bool]]:
        """Find the one tracking issue for a finding in any state; an open one wins over closed duplicates."""
        output = self._gh(
            "issue",
            "list",
            "--repo",
            self._repository,
            "--label",
            TRACKING_LABEL,
            "--state",
            "all",
            "--search",
            f'"{finding_id}" in:title',
            "--json",
            "url,number,state",
        )
        entries = json.loads(output or "[]")
        if not entries:
            return None
        states = {int(entry["number"]): _state_of(entry, ("OPEN", "CLOSED"), "issue") for entry in entries}
        entries.sort(key=lambda entry: (states[int(entry["number"])] != "OPEN", int(entry["number"])))
        chosen = entries[0]
        return int(chosen["number"]), str(chosen["url"]), states[int(chosen["number"])] == "OPEN"

    def create_or_update_tracking_issue(self, finding_id: str, title: str, body: str) -> str:
        # Lookup searches the title for the finding ID, so creation must always embed it there.
        if finding_id not in title:
            title = f"{title} [{finding_id}]"
        existing = self._find_tracking_issue(finding_id)
        if existing is not None:
            number, url, is_open = existing
            if not is_open:
                # One tracking issue per finding: a closed one is reopened rather than duplicated.
                self._gh("issue", "reopen", str(number), "--repo", self._repository)
            self._gh("issue", "edit", str(number), "--repo", self._repository, "--title", title, "--body", body)
            return url
        created = self._gh(
            "issue", "create", "--repo", self._repository, "--title", title, "--body", body, "--label", TRACKING_LABEL
        ).strip()
        return self._converge_tracking_issues(finding_id, created)

    def _converge_tracking_issues(self, finding_id: str, created_url: str) -> str:
        """The finding ID in the title is the idempotency key, but GitHub cannot enforce it atomically: after
        creating, re-list and converge any concurrent duplicates onto the lowest-numbered open issue."""
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
            "url,number,state",
        )
        entries = sorted(json.loads(output or "[]"), key=lambda entry: int(entry["number"]))
        if len(entries) <= 1:
            return str(entries[0]["url"]) if entries else created_url
        survivor = entries[0]
        for duplicate in entries[1:]:
            self._gh(
                "issue",
                "comment",
                str(duplicate["number"]),
                "--repo",
                self._repository,
                "--body",
                f"Duplicate tracking issue created concurrently; superseded by {survivor['url']}.",
            )
            self._gh("issue", "close", str(duplicate["number"]), "--repo", self._repository)
        return str(survivor["url"])
