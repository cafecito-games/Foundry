# Issues #1482 and #1538 Single-PR Handoff

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans or
> superpowers:subagent-driven-development for each implementation session. This document coordinates the two detailed
> plans; it is not a substitute for either one.

**Goal:** Implement #1538 and #1482 in separate, reviewable sessions on one branch, then deliver both fixes in one PR
without losing test, review, or ownership boundaries.

**Detailed plans:**

- `docs/superpowers/plans/2026-08-02-issue-1538-inner-class-conformance-identity.md`
- `docs/superpowers/plans/2026-08-02-issue-1482-static-self-call-context.md`
- Design rationale: `docs/superpowers/specs/2026-08-02-exact-receiver-identity-combined-design.md`

---

## Why the issues share one PR

Both failures discard exact receiver identity in the runtime type/conformance pipeline:

- #1538 asks whether a class has a conformance and incorrectly treats the shared source-file path as an inner-class
  identity.
- #1482 has already selected a static implementation, but executes its symbolic `Self` metadata against the declaring
  class instead of the exact class handle at the call boundary.

They are not duplicate bugs. #1538 is an identity-query correction; #1482 introduces a call-context carrier and
late recursive type resolution. Keeping focused commits and fixtures for each issue makes the combined PR bisectable.

## Branch and session protocol

### Session A: implement #1538

- [ ] Create `issue-1482-1538` from current `origin/develop` in an isolated worktree.
- [ ] Execute the #1538 plan from Task 1 through Task 4.
- [ ] Keep the failing fixtures and registry/runtime fix in focused commits.
- [ ] Run both script-corpus passes and the conformance-focused C++ tests.
- [ ] Stop with a clean worktree. Do not open a PR and do not close either issue.
- [ ] Record the branch name, worktree path, HEAD commit, test commands, and results in the session handoff.

### Session B: implement #1482

- [ ] Reuse the exact branch and worktree from Session A; confirm #1538 commits are present and tests are green.
- [ ] Rebase onto `origin/develop` only if needed, resolving conflicts without squashing the #1538 boundary.
- [ ] Execute the #1482 plan from Task 1 through Task 6.
- [ ] Run the native strict build and full suite required by `AGENTS.md`.
- [ ] Stop with a clean worktree and record exact build/test evidence.

### Session C: review and publish

- [ ] Compare the complete branch with `origin/develop`; reject unrelated files or generated churn.
- [ ] Run the repository's supervised Codex branch review and repeat after fixes until clean.
- [ ] Re-run affected tests after every review-driven code change.
- [ ] Push the branch and open one PR targeting `develop` with `Closes #1482` and `Closes #1538`.
- [ ] Include separate `#1538` and `#1482` sections in the PR body, plus exact verification commands/results.
- [ ] Do not force-merge the implementation PR without fresh user authorization in Session C.

## Required acceptance matrix

| Surface | #1538 expectation | #1482 expectation |
| --- | --- | --- |
| Script identity | Siblings do not share path identity | Exact derived/specialized handle enters the call |
| Root compatibility | Root path aliases still work | Explicit root/base calls resolve `Self` to root/base |
| Membership | `is`, `as`, assignment, `is_instance_of` agree | Trait signatures resolve recursive `Self` late |
| Dispatch | Real methods and exact witnesses keep precedence | Selection stays unchanged; context is carried |
| Receiver kinds | Script/native-base membership still works | Script, native, backed, specialized, supported builtin |
| Reified types | No sibling leakage through typed slots | Containers, `Type`, generics, casts, defaults, returns |
| Lifetime | Source and restored bytecode agree | Direct, dynamic, callable, nested, lambda, await, bytecode |

## Final implementation PR body template

```markdown
Closes #1482
Closes #1538

## #1538 — exact conformance membership

- treats a source path as canonical only for a root script
- uses exact FQCN/global identity for inner classes
- keeps `is`, `as`, typed assignment, `is_instance_of`, and witness dispatch aligned

## #1482 — exact static `Self` receiver

- preserves symbolic recursive `Self` metadata until runtime
- carries the exact class handle through dispatch, callables, lambdas, and async state
- resolves parameters, returns, construction, casts, type tests, and reified containers from that context

## Verification

- `python3 scripts/agent_build.py --test` — record the strict build and final doctest summary
- `python3 scripts/agent_build.py --backend ninja --test --case "*Script compilation and runtime*"`
- `foundry --headless test run --case "*[Conformance]*" --force-colors`
- `foundry --headless test run --case "*[FoundryScript][Bytecode]*" --force-colors`
```

## Planning-PR boundary

The PR that adds these documents is documentation-only. Its body and issue comments must say that it does not close
#1482 or #1538. Force-merging the planning PR does not authorize force-merging the later implementation PR.
