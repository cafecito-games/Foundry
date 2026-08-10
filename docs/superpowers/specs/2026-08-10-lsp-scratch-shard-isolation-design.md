# LSP Scratch Shard Isolation Design

Status: approved
Issue: [#2065](https://github.com/cafecito-games/Foundry/issues/2065)
Date: 2026-08-10

## Summary

The Foundry Script LSP scratch contract tests race when separate test processes run concurrently.
Each process overrides `FOUNDRY_TEST_SCRATCH` with the same fixed temporary directory, then stages the
same fixture project below it. One process can delete or partially replace files while another is
loading them.

The fix is to preserve the explicit scratch-override contract while giving every process its own
stable child root. A behavioral regression will launch the three affected cases concurrently against
one shared parent scratch directory and require every child process to pass.

## Confirmed Evidence

Affected CI runs:

- [31398027785](https://github.com/cafecito-games/Foundry/actions/runs/31398027785), shard 3:
  `temp files resolve inside staged project`.
- [31400238902](https://github.com/cafecito-games/Foundry/actions/runs/31400238902), shards 1 and 3:
  `temp file cleanup uses original resolved path after language teardown` and
  `temp files resolve inside staged project`.

Correction to the original report: run
[31394450699](https://github.com/cafecito-games/Foundry/actions/runs/31394450699) did not fail an LSP
scratch case. Its failing case was the unrelated 3D board-switch test, so it is not evidence for this
issue.

A controlled reproduction ran the three matching shards concurrently for 12 rounds against the
current fixed-root behavior. All 36 child processes failed. The failures included:

- `Cannot clear staged project '/tmp/foundry_lsp_scratch_contract/foundry_script_lsp_project'`.
- Failure to load the staged `project.foundry`.
- Resolved LSP temporary paths outside the expected staged project.
- Missing staged contents during cleanup after language teardown.

The reproduction also showed that the third contract case, `test project root is staged under
scratch`, is vulnerable even though the original issue named only the other two cases.

## Root Cause

`modules/foundry_script/tests/test_lsp.h` currently returns one process-independent path from
`lsp_scratch_contract_root()`:

```text
<OS temporary directory>/foundry_lsp_scratch_contract
```

Each of the three top-level `[Modules][FoundryScript][LSP scratch]` cases installs that path as
`FOUNDRY_TEST_SCRATCH`. Sharding can place those cases in distinct processes.

`TemporaryProjectTree::stage_project_copy()` then resolves the same child in every process:

```text
<scratch root>/foundry_script_lsp_project
```

When the staged project is absent or does not match the current process-local cache, the helper
removes that child and copies the fixture directory again. Its static last-root fields are
process-local and therefore cannot coordinate other processes. Concurrent remove/copy operations
produce the observed partial and missing project trees.

This is related to but not fixed by [#1837](https://github.com/cafecito-games/Foundry/issues/1837).
That issue isolates engine `user://` state per shard. These tests explicitly replace
`FOUNDRY_TEST_SCRATCH`, so they bypass that isolation and collide in a separate filesystem tree.

## Decision

`lsp_scratch_contract_root()` will return a stable process-scoped path:

```text
<inherited test scratch or OS temporary directory>/
  foundry_lsp_scratch_contract_<process-id>/
    foundry_script_lsp_project/
```

The root must be computed once per process before the scoped override is installed. All three cases
within one process may reuse it, while separate shards and independent Foundry invocations must
resolve different roots.

The base directory is the valid inherited `FOUNDRY_TEST_SCRATCH` when present. Direct invocations
without that environment variable use the existing process-safe OS temporary fallback. PID reuse is
safe because the first staging operation clears its owned child before copying.

Do not change `TemporaryProjectTree` so that every explicit `FOUNDRY_TEST_SCRATCH` value is silently
process-scoped. An explicit value is a caller-owned contract and other tests may intentionally share
its parent. The defect is the LSP tests choosing a process-independent override.

## Behavioral Contract

- The cases continue to exercise an explicit `FOUNDRY_TEST_SCRATCH` override.
- The staged fixture and every temporary LSP file remain strict descendants of that override.
- `ScopedLSPTempFile` removes the path captured before language teardown, even if global project
  state changes afterward.
- No case creates or modifies the checked-in fixture project or repository-root `project.foundry`.
- Existing canonicalization, strict-containment, symlink, and running-executable deletion guards
  remain unchanged.
- Ordinary focused execution, sharded execution, and concurrent independent invocations use the same
  code path.
- No CI serialization rule or fixed shard assignment is introduced.

## Regression Test

Add a parent C++ test whose name does not match the child case filter. It will:

1. Create one owned parent directory below the shared test scratch root.
2. Launch three Foundry subprocesses asynchronously, selecting exactly one existing LSP scratch case
   in each subprocess through command-first `test run --case` arguments.
3. Give every child the same parent `FOUNDRY_TEST_SCRATCH` value.
4. Set a test-only barrier environment variable to a parent-owned barrier directory. At the start of
   each selected case, before the first staged-root resolution, the child writes a ready marker named
   with its PID and waits until all three markers exist. The wait has a bounded timeout and is active
   only when the parent supplies the barrier variable. Releasing all three children together makes
   the old shared-root staging operations overlap instead of depending on incidental scheduler
   timing.
5. Continuously drain stdout and stderr so a full pipe cannot deadlock a child.
6. Enforce a bounded timeout, terminate and reap every surviving child on failure, and include each
   child's captured output in the assertion diagnostics.
7. Assert that all children exit successfully and report passing doctest results.

The three exact child cases are:

- `[Modules][FoundryScript][LSP scratch] test project root is staged under scratch`
- `[Modules][FoundryScript][LSP scratch] temp files resolve inside staged project`
- `[Modules][FoundryScript][LSP scratch] temp file cleanup uses original resolved path after language
  teardown`

The test asserts process behavior and produced files. It must not inspect implementation source text.

## Validation

- Demonstrate that the concurrent regression fails against the fixed shared root and passes after
  process scoping.
- Run the three-child concurrency scenario for at least ten consecutive rounds. All 30 child runs
  must pass.
- Run the focused LSP scratch cases through the command-first test CLI.
- Run the native strict validation build through `python3 scripts/agent_build.py` with the focused
  cases enabled.

## Acceptance Criteria

- Concurrent processes never resolve the same LSP contract scratch root.
- All three contract cases pass when run concurrently and when run normally.
- The checked-in concurrency regression fails with the old fixed-root behavior.
- No run reports a staged-project removal/copy conflict, a missing staged `project.foundry`, or an LSP
  temporary path outside its configured scratch root.
- The relationship to #1837 is documented as complementary rather than duplicative.
- The issue body cites only CI runs that actually contain the LSP scratch failure.

## Non-Goals

- Changing production LSP path resolution.
- Serializing the full Foundry Script test suite.
- Assigning the cases permanently to one CI shard.
- Redesigning the general `TemporaryProjectTree` ownership and containment model.
