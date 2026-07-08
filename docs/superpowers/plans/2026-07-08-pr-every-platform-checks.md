# PR Platform Checks Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or
> superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add authorized PR comment commands for `check every platform` and `check <platform>` that run reusable
platform build workflows against the PR head commit.

**Architecture:** A Python helper parses PR comments into requested platform flags. A new `issue_comment` workflow gates
requests by write-level permissions, then calls the existing reusable platform workflows whose flags are enabled. The
platform workflows accept an optional checkout ref so the comment workflow can build the PR head SHA while normal manual
dispatch keeps the current checkout behavior.

**Tech Stack:** GitHub Actions YAML, Python `unittest`, existing reusable platform workflows.

---

### Task 1: Parser and Planner

**Files:**
- Create: `.github/scripts/pr_platform_check_request.py`
- Create: `.github/scripts/test_pr_platform_check_request.py`

- [x] Write failing tests for `check every platform`, `build every platform`, and specific commands:
  `check linux`, `check macOS`, `check windows`, `check android`, `check ios`, and `check web`.
- [x] Add alias coverage for `linuxbsd`, `mac`, and `osx`.
- [x] Add planner coverage for authorized requests, unauthorized requests, issue comments, PR metadata, platform labels,
  and per-platform `run_<platform>` outputs.
- [x] Implement the parser/planner helper and CLI output writer.
- [x] Verify with:

```sh
PYTHONPATH=.github/scripts python3 -m unittest discover -s .github/scripts -p 'test_pr_platform_check_request.py'
```

### Task 2: Workflow Shape Tests

**Files:**
- Create: `.github/scripts/test_pr_platform_checks_workflow.py`

- [x] Write failing workflow-shape tests for the new PR comment workflow.
- [x] Assert the workflow resolves the PR head SHA, emits `platforms_label` and `run_<platform>` outputs, gates each
  platform job independently, and comments on acceptance/completion.
- [x] Assert each reusable platform workflow accepts `checkout-ref` but does not accept a checkout repository override.
- [x] Verify with:

```sh
PYTHONPATH=.github/scripts python3 -m unittest discover -s .github/scripts -p 'test_pr_platform_checks_workflow.py'
```

### Task 3: Reusable Workflow Checkout Ref

**Files:**
- Modify: `.github/workflows/linux_builds.yml`
- Modify: `.github/workflows/macos_builds.yml`
- Modify: `.github/workflows/windows_builds.yml`
- Modify: `.github/workflows/android_builds.yml`
- Modify: `.github/workflows/ios_builds.yml`
- Modify: `.github/workflows/web_builds.yml`

- [x] Add optional `workflow_call.inputs.checkout-ref` to every reusable platform workflow.
- [x] Route every `actions/checkout@v6` step through `ref: ${{ inputs.checkout-ref || github.ref }}`.
- [x] Keep repository checkout fixed to the current repository; fork checkout overrides are out of scope.

### Task 4: PR Comment Workflow

**Files:**
- Create: `.github/workflows/pr_platform_checks.yml`

- [x] Add an `issue_comment` workflow that ignores non-PR comments and unrelated comments.
- [x] Gate accepted requests to commenters with `write`, `maintain`, or `admin` permissions.
- [x] Resolve the PR head SHA and pass it to requested reusable platform workflows as `checkout-ref`.
- [x] Add per-platform concurrency groups keyed by PR head SHA.
- [x] Post accepted-request and completion comments. Completion comments list only requested platforms.

### Task 5: Final Verification

- [x] Run all GitHub helper tests:

```sh
PYTHONPATH=.github/scripts python3 -m unittest discover -s .github/scripts -p 'test_*.py'
```

- [x] Check whitespace:

```sh
git diff --check develop...HEAD
```

- [x] Lint the new workflow:

```sh
actionlint .github/workflows/pr_platform_checks.yml
```
