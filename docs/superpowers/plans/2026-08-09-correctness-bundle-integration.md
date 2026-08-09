# Foundry Script Correctness Bundle Integration Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or
> superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Integrate, verify, review, and publish all six fixes as one auto-merged pull request.

**Architecture:** Execute component plans in parallel waves on `issue-1808`, serialize overlapping analyzer work
and builds, then converge through strict validation and supervised adversarial review before publication.

**Tech Stack:** Git worktrees, GitHub CLI, SCons/Ninja build wrapper, doctest, pre-commit, supervised Codex review.

---

### Task 1: Mark all issues in progress and dispatch wave one

- [ ] **Step 1: Set Experiment project status**

For issues `1808 1967 1310 1388 1966 1811`, add missing project items and set the Experiment status field to
`In Progress` (`47fc9ee4`) using project id `PVT_kwDODvOSms4Bbc4g` and status field
`PVTSSF_lADODvOSms4Bbc4gzhWNUKA`.

- [ ] **Step 2: Dispatch non-overlapping ownership**

Wave one assignments:

- Analyzer worker: execute #1808, then #1966.
- Runtime worker: execute #1967.
- Formatter/tooling worker: execute #1310 and the non-mutating edits of #1811.

Workers edit only assigned files, do not stage/commit/revert others' changes, and report test evidence and file lists.

- [ ] **Step 3: Review and commit each completed component**

Inspect every diff, run its focused test serialized, and use the exact commit step from that component plan.

### Task 2: Execute the analyzer visibility wave

- [ ] **Step 1: Dispatch #1388 after #1808/#1966 integration**

Reuse an idle worker only after the analyzer worktree is clean at a component commit. Tell it to preserve and build on
the integrated analyzer helpers and not revert prior changes.

- [ ] **Step 2: Review, test, and commit #1388**

Run the focused conformance command from its component plan, inspect all five delegated scope sites, and commit only the
listed files.

### Task 3: Run strict integrated verification

- [ ] **Step 1: Run the native strict build**

```sh
python3 scripts/agent_build.py
```

Expected: `dev_mode=yes dev_build=yes tests=yes` build succeeds with warnings as errors.

- [ ] **Step 2: Run the full suite with structured progress**

Use the built binary path printed by the wrapper:

```sh
DISPLAY=:1 ./bin/foundry.* --headless test run \
  --progress-format=jsonl \
  --progress-file /tmp/foundry-issue-1808-test-progress.jsonl \
  --progress-heartbeat-seconds 30 \
  --force-colors
```

Expected: final doctest summary contains `[doctest] Status: SUCCESS!`. Cleanup leak warnings alone do not override that
summary.

- [ ] **Step 3: Run all-files pre-commit twice**

Run Task 4 of the #1811 plan. The first run must pass; the second run must also pass and leave `git diff --exit-code`
clean.

### Task 4: Converge supervised review

- [ ] **Step 1: Start the blocking branch review**

```sh
python3 ~/.claude/scripts/codex_review/await_review.py start-wait \
  --cwd /Users/christian/CafecitoGames/Foundry/.worktrees/issue-1808 \
  --scope branch --base origin/develop --deadline 540
```

Expected: exit `0` for clean or `10` with actionable findings.

- [ ] **Step 2: Triage every finding**

Fix all in-scope critical/blocking findings, rerun proportional focused tests, commit, and start a fresh review on the
new HEAD. File every real out-of-scope follow-up in GitHub, link its relevant epic, and add it to Experiment.

- [ ] **Step 3: Stop only at convergence**

Record review round count, fixes, and the final clean or explicitly triaged outcome.

### Task 5: Publish one PR and monitor auto-merge

- [ ] **Step 1: Push and create the pull request**

Push `issue-1808` and open a draft PR targeting `develop`. Describe all six behavior changes and verification evidence.
End the body with:

```text
Closes #1808
Closes #1967
Closes #1310
Closes #1388
Closes #1966
Closes #1811
```

- [ ] **Step 2: Enable squash auto-merge**

```sh
gh pr merge --repo cafecito-games/Foundry --squash --auto <pr-number>
```

- [ ] **Step 3: Monitor checks and review state**

Use the GitHub issue/CI workflows for any actionable review or failing Actions check. Do not bypass required checks.

- [ ] **Step 4: Clean up only after merge**

After GitHub reports the PR merged, remove the worktree with the repository's required Supacode workflow if it is
locked; otherwise use ordinary `git worktree remove`. Delete the local `issue-1808` branch and report the PR, review
rounds, follow-ups, merge state, and cleanup status.
