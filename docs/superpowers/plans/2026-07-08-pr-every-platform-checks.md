# PR Every Platform Checks Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or
> superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add an authorized PR comment command, `check every platform`, that runs every existing platform build workflow
against the PR head commit.

**Architecture:** A small Python helper parses and plans PR comment requests, mirroring the existing PR editor artifact
helper style. A new `issue_comment` workflow performs authorization and fans out to the existing reusable platform
workflows. The platform workflows gain optional checkout inputs so reusable callers can build an explicit PR head while
manual dispatch keeps the current behavior.

**Tech Stack:** GitHub Actions YAML, Python `unittest`, existing local workflow helper scripts, reusable workflow calls.

---

### Task 1: Request Parser and Planner

**Files:**
- Create: `.github/scripts/test_pr_platform_check_request.py`
- Create: `.github/scripts/pr_platform_check_request.py`

- [ ] **Step 1: Write the failing parser/planner tests**

```python
import unittest

import pr_platform_check_request as request


class PrPlatformCheckRequestTest(unittest.TestCase):
    def test_request_detection_accepts_every_platform_commands(self):
        self.assertTrue(request.is_every_platform_request("check every platform"))
        self.assertTrue(request.is_every_platform_request("Please CHECK every PLATFORM before merge."))
        self.assertTrue(request.is_every_platform_request("build every platform"))

    def test_request_detection_ignores_unrelated_comments(self):
        self.assertFalse(request.is_every_platform_request("check the linux platform"))
        self.assertFalse(request.is_every_platform_request("the platform build failed"))

    def test_has_write_access_accepts_write_level_permissions(self):
        for permission in ("write", "maintain", "admin"):
            with self.subTest(permission=permission):
                self.assertTrue(request.has_write_access(permission))

        for permission in ("", "none", "read", "triage"):
            with self.subTest(permission=permission):
                self.assertFalse(request.has_write_access(permission))

    def test_build_plan_emits_authorized_pr_metadata(self):
        plan = request.build_plan(
            body="check every platform",
            permission="write",
            is_pr=True,
            pr_number=42,
            head_sha="0123456789abcdef0123456789abcdef01234567",
            head_repo="cafecito-games/Foundry",
        )

        self.assertTrue(plan["request_found"])
        self.assertTrue(plan["requested"])
        self.assertTrue(plan["authorized"])
        self.assertEqual(plan["pr_number"], "42")
        self.assertEqual(plan["pr_head_sha"], "0123456789abcdef0123456789abcdef01234567")
        self.assertEqual(plan["pr_short_sha"], "0123456789ab")
        self.assertEqual(plan["pr_head_repo"], "cafecito-games/Foundry")

    def test_build_plan_marks_unauthorized_requests(self):
        plan = request.build_plan(
            body="check every platform",
            permission="read",
            is_pr=True,
            pr_number=42,
            head_sha="0123456789abcdef0123456789abcdef01234567",
            head_repo="cafecito-games/Foundry",
        )

        self.assertTrue(plan["request_found"])
        self.assertTrue(plan["requested"])
        self.assertFalse(plan["authorized"])

    def test_build_plan_ignores_matching_comments_outside_pull_requests(self):
        plan = request.build_plan(
            body="build every platform",
            permission="write",
            is_pr=False,
            pr_number=42,
            head_sha="0123456789abcdef0123456789abcdef01234567",
            head_repo="cafecito-games/Foundry",
        )

        self.assertTrue(plan["request_found"])
        self.assertFalse(plan["requested"])
        self.assertFalse(plan["authorized"])


if __name__ == "__main__":
    unittest.main()
```

- [ ] **Step 2: Run the test to verify it fails**

Run: `PYTHONPATH=.github/scripts python3 -m unittest discover -s .github/scripts -p 'test_pr_platform_check_request.py'`

Expected: FAIL with `ModuleNotFoundError: No module named 'pr_platform_check_request'`.

- [ ] **Step 3: Implement the helper**

```python
#!/usr/bin/env python3

from __future__ import annotations

import argparse
import os
import re
from typing import Any


ALLOWED_PERMISSIONS = {"write", "maintain", "admin"}
REQUEST_PATTERN = re.compile(r"\b(?:check|build)\s+every\s+platform\b", re.I)


def is_every_platform_request(body: str) -> bool:
    return bool(REQUEST_PATTERN.search(body))


def has_write_access(permission: str) -> bool:
    return permission.lower() in ALLOWED_PERMISSIONS


def short_sha(head_sha: str) -> str:
    return head_sha[:12].lower()


def build_plan(
    *,
    body: str,
    permission: str,
    is_pr: bool,
    pr_number: int,
    head_sha: str,
    head_repo: str,
) -> dict[str, Any]:
    request_found = is_every_platform_request(body)
    requested = request_found and is_pr
    authorized = requested and has_write_access(permission)

    return {
        "request_found": request_found,
        "requested": requested,
        "authorized": authorized,
        "pr_number": str(pr_number),
        "pr_head_sha": head_sha,
        "pr_short_sha": short_sha(head_sha),
        "pr_head_repo": head_repo,
    }


def emit_output(name: str, value: Any) -> None:
    if isinstance(value, bool):
        text = "true" if value else "false"
    else:
        text = str(value)

    github_output = os.environ.get("GITHUB_OUTPUT")
    if github_output:
        with open(github_output, "a", encoding="utf-8") as output_file:
            output_file.write(f"{name}={text}\n")
    else:
        print(f"{name}={text}")


def emit_outputs(values: dict[str, Any]) -> None:
    for name, value in values.items():
        emit_output(name, value)


def command_plan(args: argparse.Namespace) -> int:
    emit_outputs(
        build_plan(
            body=args.body,
            permission=args.permission,
            is_pr=args.is_pr,
            pr_number=args.pr_number,
            head_sha=args.head_sha,
            head_repo=args.head_repo,
        )
    )
    return 0


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Plan on-demand PR platform checks.")
    subparsers = parser.add_subparsers(dest="command", required=True)

    plan = subparsers.add_parser("plan", help="Emit GitHub Actions outputs for a PR comment.")
    plan.add_argument("--body", required=True)
    plan.add_argument("--permission", required=True)
    pr_group = plan.add_mutually_exclusive_group(required=True)
    pr_group.add_argument("--is-pr", dest="is_pr", action="store_true")
    pr_group.add_argument("--no-is-pr", dest="is_pr", action="store_false")
    plan.add_argument("--pr-number", type=int, required=True)
    plan.add_argument("--head-sha", required=True)
    plan.add_argument("--head-repo", required=True)
    plan.set_defaults(func=command_plan)

    return parser


def main(argv: list[str] | None = None) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)
    return args.func(args)


if __name__ == "__main__":
    raise SystemExit(main())
```

- [ ] **Step 4: Run the parser/planner tests**

Run: `PYTHONPATH=.github/scripts python3 -m unittest discover -s .github/scripts -p 'test_pr_platform_check_request.py'`

Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add .github/scripts/pr_platform_check_request.py .github/scripts/test_pr_platform_check_request.py
git commit -m "Add PR platform check request parser"
```

### Task 2: Workflow Shape Tests

**Files:**
- Create: `.github/scripts/test_pr_platform_checks_workflow.py`

- [ ] **Step 1: Write failing workflow-shape tests**

```python
from pathlib import Path
import unittest


REPO_ROOT = Path(__file__).resolve().parents[2]
WORKFLOW = REPO_ROOT / ".github/workflows/pr_platform_checks.yml"
PLATFORM_WORKFLOWS = [
    ".github/workflows/linux_builds.yml",
    ".github/workflows/macos_builds.yml",
    ".github/workflows/windows_builds.yml",
    ".github/workflows/android_builds.yml",
    ".github/workflows/ios_builds.yml",
    ".github/workflows/web_builds.yml",
]


class PrPlatformChecksWorkflowTest(unittest.TestCase):
    def test_workflow_gates_platform_jobs_on_authorized_request(self):
        workflow = WORKFLOW.read_text(encoding="utf-8")

        self.assertIn("on:\n  issue_comment:\n    types: [created]", workflow)
        self.assertIn("python3 .github/scripts/pr_platform_check_request.py plan", workflow)
        self.assertIn("Comment on unauthorized request", workflow)
        self.assertIn("Comment on accepted request", workflow)
        self.assertLess(workflow.find("Comment on accepted request"), workflow.find("linux:"))

        for job in ("linux", "macos", "windows", "android", "ios", "web"):
            with self.subTest(job=job):
                self.assertIn(f"{job}:", workflow)
                self.assertIn("if: ${{ needs.preflight.outputs.requested == 'true' && needs.preflight.outputs.authorized == 'true' }}", workflow)

    def test_workflow_calls_every_platform_with_pr_head_checkout(self):
        workflow = WORKFLOW.read_text(encoding="utf-8")

        for platform_workflow in PLATFORM_WORKFLOWS:
            with self.subTest(platform_workflow=platform_workflow):
                call = f"uses: ./{platform_workflow}"
                self.assertIn(call, workflow)
                call_block = workflow[workflow.find(call) : workflow.find(call) + 300]
                self.assertIn("checkout-repository: ${{ needs.preflight.outputs.pr_head_repo }}", call_block)
                self.assertIn("checkout-ref: ${{ needs.preflight.outputs.pr_head_sha }}", call_block)

    def test_workflow_posts_completion_comment_after_platform_jobs(self):
        workflow = WORKFLOW.read_text(encoding="utf-8")

        self.assertIn("finish:", workflow)
        self.assertIn("needs: [preflight, linux, macos, windows, android, ios, web]", workflow)
        self.assertIn("if: ${{ always() && needs.preflight.outputs.requested == 'true' && needs.preflight.outputs.authorized == 'true' }}", workflow)
        for job in ("linux", "macos", "windows", "android", "ios", "web"):
            with self.subTest(job=job):
                self.assertIn(f"{job}: ${{{{ needs.{job}.result }}}}", workflow)

    def test_platform_workflows_accept_checkout_inputs(self):
        for relative_path in PLATFORM_WORKFLOWS:
            with self.subTest(relative_path=relative_path):
                workflow = (REPO_ROOT / relative_path).read_text(encoding="utf-8")
                self.assertIn("checkout-repository:", workflow)
                self.assertIn("checkout-ref:", workflow)
                self.assertIn("repository: ${{ inputs.checkout-repository || github.repository }}", workflow)
                self.assertIn("ref: ${{ inputs.checkout-ref || github.ref }}", workflow)
```

- [ ] **Step 2: Run workflow-shape tests to verify they fail**

Run: `PYTHONPATH=.github/scripts python3 -m unittest discover -s .github/scripts -p 'test_pr_platform_checks_workflow.py'`

Expected: FAIL with `FileNotFoundError` for `.github/workflows/pr_platform_checks.yml`.

- [ ] **Step 3: Commit after the failing tests are in place**

```bash
git add .github/scripts/test_pr_platform_checks_workflow.py
git commit -m "test: Cover PR platform check workflow shape"
```

### Task 3: Reusable Workflow Checkout Inputs

**Files:**
- Modify: `.github/workflows/linux_builds.yml`
- Modify: `.github/workflows/macos_builds.yml`
- Modify: `.github/workflows/windows_builds.yml`
- Modify: `.github/workflows/android_builds.yml`
- Modify: `.github/workflows/ios_builds.yml`
- Modify: `.github/workflows/web_builds.yml`

- [ ] **Step 1: Add optional workflow_call inputs to every platform workflow**

Replace each existing top-level trigger block:

```yaml
on:
  workflow_call:
  workflow_dispatch:
```

with:

```yaml
on:
  workflow_call:
    inputs:
      checkout-repository:
        description: Repository to check out. Defaults to this repository.
        required: false
        type: string
        default: ""
      checkout-ref:
        description: Git ref or SHA to check out. Defaults to the workflow ref.
        required: false
        type: string
        default: ""
  workflow_dispatch:
```

- [ ] **Step 2: Route every checkout through the optional inputs**

For each `actions/checkout@v6` step in those platform workflows, change:

```yaml
with:
  submodules: recursive
```

to:

```yaml
with:
  repository: ${{ inputs.checkout-repository || github.repository }}
  ref: ${{ inputs.checkout-ref || github.ref }}
  submodules: recursive
```

- [ ] **Step 3: Run the workflow-shape test**

Run: `PYTHONPATH=.github/scripts python3 -m unittest discover -s .github/scripts -p 'test_pr_platform_checks_workflow.py'`

Expected: still FAIL because `.github/workflows/pr_platform_checks.yml` does not exist yet, but the
`test_platform_workflows_accept_checkout_inputs` assertion no longer reports platform workflow input failures.

### Task 4: PR Platform Check Workflow

**Files:**
- Create: `.github/workflows/pr_platform_checks.yml`

- [ ] **Step 1: Create the workflow**

```yaml
name: PR Every Platform Checks

on:
  issue_comment:
    types: [created]

permissions:
  contents: read
  issues: write
  pull-requests: read

jobs:
  preflight:
    name: Parse request
    runs-on: ubuntu-24.04
    outputs:
      requested: ${{ steps.plan.outputs.requested }}
      authorized: ${{ steps.plan.outputs.authorized }}
      pr_number: ${{ steps.plan.outputs.pr_number }}
      pr_head_sha: ${{ steps.plan.outputs.pr_head_sha }}
      pr_short_sha: ${{ steps.plan.outputs.pr_short_sha }}
      pr_head_repo: ${{ steps.plan.outputs.pr_head_repo }}

    steps:
      - name: Checkout workflow helpers
        uses: actions/checkout@v6

      - name: Resolve commenter permission
        id: permission
        if: ${{ github.event.issue.pull_request != null }}
        env:
          COMMENT_AUTHOR: ${{ github.event.comment.user.login }}
          GH_TOKEN: ${{ github.token }}
        run: |
          permission="$(gh api "repos/${GITHUB_REPOSITORY}/collaborators/${COMMENT_AUTHOR}/permission" --jq ".permission" 2>/dev/null || true)"
          if [ -z "$permission" ]; then
            permission="none"
          fi
          echo "permission=$permission" >> "$GITHUB_OUTPUT"

      - name: Resolve pull request head
        id: pr
        if: ${{ github.event.issue.pull_request != null }}
        env:
          GH_TOKEN: ${{ github.token }}
          PR_NUMBER: ${{ github.event.issue.number }}
        run: |
          echo "head_sha=$(gh api "repos/${GITHUB_REPOSITORY}/pulls/${PR_NUMBER}" --jq ".head.sha")" >> "$GITHUB_OUTPUT"
          echo "head_repo=$(gh api "repos/${GITHUB_REPOSITORY}/pulls/${PR_NUMBER}" --jq ".head.repo.full_name")" >> "$GITHUB_OUTPUT"

      - name: Plan requested platform checks
        id: plan
        env:
          COMMENT_BODY: ${{ github.event.comment.body }}
          IS_PR: ${{ github.event.issue.pull_request != null }}
          PERMISSION: ${{ steps.permission.outputs.permission || 'none' }}
          PR_NUMBER: ${{ github.event.issue.number }}
          HEAD_SHA: ${{ steps.pr.outputs.head_sha || github.sha }}
          HEAD_REPO: ${{ steps.pr.outputs.head_repo || github.repository }}
        run: |
          is_pr_arg="--no-is-pr"
          if [ "$IS_PR" = "true" ]; then
            is_pr_arg="--is-pr"
          fi

          python3 .github/scripts/pr_platform_check_request.py plan \
            --body "$COMMENT_BODY" \
            --permission "$PERMISSION" \
            "$is_pr_arg" \
            --pr-number "$PR_NUMBER" \
            --head-sha "$HEAD_SHA" \
            --head-repo "$HEAD_REPO"

      - name: Comment on unauthorized request
        if: ${{ steps.plan.outputs.requested == 'true' && steps.plan.outputs.authorized != 'true' }}
        env:
          GH_TOKEN: ${{ github.token }}
          PR_NUMBER: ${{ github.event.issue.number }}
        run: |
          gh issue comment "$PR_NUMBER" --body "I can only run every-platform checks for comments from users with write access or higher."

      - name: Comment on accepted request
        if: ${{ steps.plan.outputs.requested == 'true' && steps.plan.outputs.authorized == 'true' }}
        env:
          GH_TOKEN: ${{ github.token }}
          PR_NUMBER: ${{ github.event.issue.number }}
          SHORT_SHA: ${{ steps.plan.outputs.pr_short_sha }}
          RUN_URL: ${{ github.server_url }}/${{ github.repository }}/actions/runs/${{ github.run_id }}
        run: |
          cat > /tmp/pr-platform-check-comment.md <<EOF
          Accepted request to check every platform for PR #${PR_NUMBER} at \`${SHORT_SHA}\`.

          Workflow run: ${RUN_URL}
          EOF
          gh issue comment "$PR_NUMBER" --body-file /tmp/pr-platform-check-comment.md
```

Then add six reusable workflow jobs named `linux`, `macos`, `windows`, `android`, `ios`, and `web`. Each job uses the
same authorization condition and passes the PR checkout inputs:

```yaml
  linux:
    name: Linux
    needs: preflight
    if: ${{ needs.preflight.outputs.requested == 'true' && needs.preflight.outputs.authorized == 'true' }}
    concurrency:
      group: pr-every-platform-linux-${{ needs.preflight.outputs.pr_head_sha }}
      cancel-in-progress: false
    uses: ./.github/workflows/linux_builds.yml
    with:
      checkout-repository: ${{ needs.preflight.outputs.pr_head_repo }}
      checkout-ref: ${{ needs.preflight.outputs.pr_head_sha }}
```

Repeat with `.github/workflows/macos_builds.yml`, `.github/workflows/windows_builds.yml`,
`.github/workflows/android_builds.yml`, `.github/workflows/ios_builds.yml`, and `.github/workflows/web_builds.yml`.
Use concurrency groups named `pr-every-platform-macos-${{ needs.preflight.outputs.pr_head_sha }}`,
`pr-every-platform-windows-${{ needs.preflight.outputs.pr_head_sha }}`,
`pr-every-platform-android-${{ needs.preflight.outputs.pr_head_sha }}`,
`pr-every-platform-ios-${{ needs.preflight.outputs.pr_head_sha }}`, and
`pr-every-platform-web-${{ needs.preflight.outputs.pr_head_sha }}` respectively.

Add the completion job:

```yaml
  finish:
    name: Report platform check result
    needs: [preflight, linux, macos, windows, android, ios, web]
    if: ${{ always() && needs.preflight.outputs.requested == 'true' && needs.preflight.outputs.authorized == 'true' }}
    runs-on: ubuntu-24.04
    env:
      GH_TOKEN: ${{ github.token }}
      PR_NUMBER: ${{ needs.preflight.outputs.pr_number }}
      SHORT_SHA: ${{ needs.preflight.outputs.pr_short_sha }}
      RUN_URL: ${{ github.server_url }}/${{ github.repository }}/actions/runs/${{ github.run_id }}
      linux: ${{ needs.linux.result }}
      macos: ${{ needs.macos.result }}
      windows: ${{ needs.windows.result }}
      android: ${{ needs.android.result }}
      ios: ${{ needs.ios.result }}
      web: ${{ needs.web.result }}
    steps:
      - name: Comment on platform check result
        run: |
          failed=()
          for platform in linux macos windows android ios web; do
            result="$(printenv "$platform")"
            if [ "$result" != "success" ]; then
              failed+=("${platform}: ${result}")
            fi
          done

          if [ "${#failed[@]}" -eq 0 ]; then
            summary="Every platform check passed for \`${SHORT_SHA}\`."
          else
            summary="Every platform check failed for \`${SHORT_SHA}\`."
          fi

          {
            echo "${summary}"
            echo
            echo "Results:"
            for platform in linux macos windows android ios web; do
              echo "- ${platform}: $(printenv "$platform")"
            done
            echo
            echo "Workflow run: ${RUN_URL}"
          } > /tmp/pr-platform-check-comment.md

          gh issue comment "$PR_NUMBER" --body-file /tmp/pr-platform-check-comment.md
```

- [ ] **Step 2: Run the helper and workflow tests**

Run: `PYTHONPATH=.github/scripts python3 -m unittest discover -s .github/scripts -p 'test_pr_platform*.py'`

Expected: PASS.

- [ ] **Step 3: Commit**

```bash
git add .github/workflows/pr_platform_checks.yml .github/workflows/*_builds.yml
git commit -m "Add on-demand PR every platform checks"
```

### Task 5: Final Verification

**Files:**
- Test-only verification.

- [ ] **Step 1: Run all GitHub helper tests**

Run: `PYTHONPATH=.github/scripts python3 -m unittest discover -s .github/scripts -p 'test_*.py'`

Expected: PASS.

- [ ] **Step 2: Check workflow edits for whitespace errors**

Run: `git diff --check HEAD`

Expected: no output and exit code 0.

- [ ] **Step 3: Review final diff**

Run: `git diff --stat HEAD` and `git diff HEAD -- .github/scripts .github/workflows docs/superpowers/plans/2026-07-08-pr-every-platform-checks.md`

Expected: diff contains only the parser/tests, new PR platform check workflow, checkout input additions to the six
platform workflows, and this plan.
