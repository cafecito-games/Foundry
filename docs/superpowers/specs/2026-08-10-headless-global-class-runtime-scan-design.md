# Standalone Runtime Global-Class Scan Design

**Issue:** [#804 — Extend headless global class scan to non-script runtime paths][issue-804]

[issue-804]: https://github.com/cafecito-games/Foundry/issues/804

**Status:** Approved design

## Summary

Standalone runs from project source must resolve global script declarations from the current files on disk rather than
depending on `.foundry/global_script_class_cache.cfg`. A run performs an in-memory global-declaration scan before any
project build provider or runtime script is analyzed. If `pre_compile` actually runs a task, the run scans once more so
generated, renamed, and deleted scripts are reflected before runtime loading begins.

Editor-launched children do not scan. The editor already runs the project build, refreshes generated outputs, and owns
the persistent global-class cache before it spawns a child with `--editor-pid`. Exported/datapack runs also do not scan,
because their source may be absent or compiled. No fallback scan writes the cache file.

This replaces the entry-point-specific scans added after #801 with one startup policy shared by game, script, eval,
project-test, and project-aware engine-test paths.

## Current Behavior

PR #805 added `ScriptServer::scan_global_classes()` and invokes it before loading a script supplied to a non-editor
source run. Later entry points added equivalent calls for project test runners and project-backed inline evaluation.
The game and build-stage paths still have gaps:

- A cacheless `foundry --headless project run --project <dir>` can load the main scene, but a scene script that refers
  to a global `class_name` fails analysis because the name was never registered.
- `application/run/main_loop_type` is read and resolved before scene loading. When it names a global script class and
  the cache is missing or stale, startup reports that the main-loop type does not exist.
- `pre_compile` providers are analyzed before the existing runtime scans. Provider-local preloads work without a
  global index, but allowed namespace imports and other global declarations inside the provider bootstrap root require
  that index. The bootstrap analyzer still rejects dependencies outside the provider root.
- `test run --project` executes its pre-compile stage through `Main::test_entrypoint()`, earlier than the runtime path,
  and currently receives no cacheless scan.

The game-run failure was reproduced on `develop` at `1f97a599af` with a scratch project containing no cache: the main
scene's script failed with `Identifier "CachelessDependency" not declared in the current scope.`

## Goals

1. Make current project sources authoritative for global declarations in every standalone source invocation that
   analyzes or loads project scripts.
2. Cover missing and stale caches equally.
3. Make global declarations available before `pre_compile` provider analysis.
4. Incorporate successful pre-compile generation before main-loop, autoload, runner, eval, or scene loading.
5. Preserve editor play-loop startup performance by skipping fallback scans in editor-launched children.
6. Keep exported/datapack and persistent editor-cache behavior unchanged.

## Non-Goals

- Changing `ScriptServer::scan_global_classes()` discovery, reconciliation, collision, or directory-skip semantics.
- Writing, validating, versioning, or changing the format of `global_script_class_cache.cfg`.
- Widening the build-provider bootstrap dependency sandbox.
- Scanning exported or mounted datapack contents.
- Changing `post_compile` semantics or making post-compile outputs available to the already-started runtime.
- Changing format, lint, migration, projectless eval, editor, tooling-host, or documentation-generation behavior.
- Adding a public scan-control option or exposing `--editor-pid` as a user workflow.
- Changing Foundry Script grammar.

## Locked Decisions

1. **Standalone source is authoritative.** A present cache does not suppress a scan. This fixes stale entries as well as
   a missing file.
2. **Use a centralized two-phase flow.** Scan before `pre_compile`; scan again only when that stage successfully ran at
   least one task.
3. **Skip editor children by launch identity.** A nonzero `editor_pid`, populated by the existing `--editor-pid`
   runtime argument, identifies an editor-spawned process whose cache and generated outputs were refreshed before
   launch.
4. **Do not gate on headless display mode.** Direct graphical runs from project source need the same correctness as
   direct headless runs.
5. **Keep the scan language-agnostic.** The existing `ScriptServer` scan continues to query every registered script
   language.
6. **Never persist fallback results.** `EditorFileSystem` remains the only normal writer of the global declaration
   cache.
7. **Prove the performance contract structurally.** Tests assert scan invocation counts rather than elapsed time.

## Eligibility Policy

A startup fallback scan is eligible only when all of the following are true:

- a real project is loaded;
- the project is running from source rather than a datapack;
- the process is not the editor;
- the process is not an editor-launched child (`editor_pid == 0`); and
- the selected invocation will analyze or load project scripts.

The following matrix is normative:

| Invocation | Scan? | Notes |
| --- | --- | --- |
| Direct `project run` of a main or custom scene | Yes | Covers main loop, autoloads, and scene scripts. |
| Direct `project run --script`, including `--check-only` | Yes | Replaces the existing script-only call. |
| Direct `project test` | Yes | Replaces the existing runner-only call. |
| `script eval --project` | Yes | Replaces the existing project-eval call. |
| `test run --project` | Yes | Runs through the earlier test entry point. |
| Successful `pre_compile` ran work | Again | Reconciles generated/deleted/moved scripts. |
| Editor-launched project or project-test child | No | Identified by `--editor-pid`. |
| Editor, project manager, or tooling host | No | Editor filesystem owns indexing. |
| Exported/datapack run | No | Trust the bundled cache. |
| Projectless eval | No | There is no project tree to scan. |
| Format, lint, migration, and other early-return tools | No new scan | Retain their own behavior. |

## Architecture

### 1. Startup policy helper

Centralize the eligibility predicate near `Main` instead of duplicating condition chains at each entry point. The helper
accepts explicit state, or otherwise remains narrow enough to exercise as a decision table in tests. It must not inspect
cache-file existence: standalone source correctness is independent of whether the file is present.

The policy is about source availability and launch ownership. It does not decide what files the scan visits; that stays
inside `ScriptServer::scan_global_classes()`.

### 2. Build-stage result plumbing

The CLI build-stage wrapper currently collapses `FoundryBuildPipelineRunner::StageRunResult` to a Boolean. Preserve its
existing diagnostic and error behavior while also returning, or writing to an optional output, whether the stage
actually ran a task.

The post-build rescan condition is:

- the invocation was eligible for the initial scan;
- `pre_compile` succeeded; and
- `StageRunResult::ran_any_task` is true.

A dirty task that runs and fails still aborts startup through the existing path. It does not trigger a rescan or runtime
loading. A clean or disabled stage does not trigger the second scan.

### 3. Runtime startup order

For the normal `Main::start()` runtime path, the required order is:

1. Finish project selection and all early-return CLI tool handling.
2. Determine whether this invocation is an eligible project-script consumer.
3. Scan eligible source projects in memory.
4. Run `pre_compile` under the existing trust policy.
5. If the stage succeeded and ran work, scan again.
6. Register custom resource loaders and savers as today.
7. Resolve/load the project test runner or project-backed eval runner.
8. Read and resolve `application/run/main_loop_type`.
9. Load the script entry point, autoloads, and scene.
10. Run `post_compile` at its existing point.

This ensures no provider, main loop, autoload, runner, eval source, or scene script is the first project script analyzed
against a missing or stale table.

Remove the mode-specific scan calls in the project-test, eval, and script branches after the centralized flow is in
place. Otherwise those modes would perform redundant scans and make the scan-count contract false.

### 4. Project-aware engine-test startup

`Main::test_entrypoint()` loads an optional project and runs its pre-compile stage before `Main::start()`. Apply the
same eligibility and two-phase order there after project setup and before the build stage. The filtered child test
process is responsible for cleaning any process-global declaration state during ordinary test cleanup, as it is today.

The engine-test path has no editor-child use case today, but it must use the shared policy rather than create a second
definition of source/datapack eligibility.

### 5. Existing scan behavior

`ScriptServer::scan_global_classes()` remains an in-memory, analyzer-light reconciliation pass:

- it visits extensions owned by registered script languages;
- it skips hidden entries, linked directories, nested projects, `.fsignore` trees, and project data;
- it sorts files for deterministic collision handling;
- it removes stale declarations under the scanned root and re-registers declarations from current files; and
- it refreshes annotation and conformance declaration indexes without writing the cache.

The initial scan can temporarily register a generated file left from an earlier build. If `pre_compile` deletes, moves,
or replaces it, the conditional second scan reconciles that change before runtime loading.

## Error Handling

- Preserve existing scan diagnostics. Duplicate global names continue to report collisions and resolve
  deterministically according to sorted scan order.
- Preserve existing handling for unreadable or excluded directories. This issue does not add a new fatal scan status.
- A blocked, untrusted, or failed `pre_compile` retains its existing diagnostics and stops startup before project
  runtime code executes.
- Do not silently fall back from a failed source scan to treating a stale cache as authoritative. Subsequent provider or
  script analysis reports the concrete unresolved declaration just as it does today.
- The hidden `--editor-pid` contract is an assertion that the editor prepared the child. A manually constructed command
  that supplies it also opts into the editor-child behavior and may therefore depend on a valid cache.

## Work Items

### W1 — Centralize scan eligibility

- Add one source-project scan policy used by normal runtime startup and project-aware engine-test startup.
- Cover the full decision matrix, including direct graphical runs and editor-child exclusion.
- Do not consult cache-file existence or cache contents.

### W2 — Preserve build-stage execution detail

- Extend the CLI build-stage wrapper so callers can observe `ran_any_task` without losing current Boolean success,
  diagnostics, or error reporting.
- Keep all existing call sites source-compatible where the execution detail is irrelevant.

### W3 — Reorder normal runtime startup

- Perform the initial eligible scan before `pre_compile`.
- Perform the second scan only after successful executed pre-compile work.
- Ensure both scans occur before runner/eval/main-loop/script/autoload/scene loading.
- Delete the redundant script, eval, and project-test scan calls.

### W4 — Cover `test run --project`

- Apply the same initial and conditional post-build scans around the pre-compile stage in `Main::test_entrypoint()`.
- Keep project setup, trust restoration, custom loader registration, post-compile execution, and cleanup behavior
  intact.

### W5 — Add behavioral process coverage

- Build isolated scratch projects and invoke the current executable through supported command-first CLI syntax.
- Bound every spawned runtime so a failed scene script cannot leave the test hanging.
- Capture output and exit status, and assert on behavior/artifacts rather than implementation source.

## Test Design

Place new coverage in a focused Foundry Script global-class startup suite. Reuse the existing temporary-project and
process helpers where practical; extend them rather than creating source-text checks or writing fixtures into tracked
directories.

All generated projects, caches, markers, build state, and scripts must live under `FOUNDRY_TEST_SCRATCH`. Process tests
must invoke the current executable and use command-first forms such as:

```text
foundry --headless --verbose project run --project <scratch-project>
foundry --headless --trusted test run --project <scratch-project> --case <child-probe>
```

Required behavioral scenarios:

1. **Cacheless main scene.** A scene script constructs a global class by name and prints a unique success marker.
2. **Stale-cache main scene.** The cache points at a deleted or renamed declaration while current source declares the
   required class elsewhere. The current declaration wins and the success marker is printed.
3. **Cacheless script main loop.** `application/run/main_loop_type` names a global script class extending `MainLoop` or
   `SceneTree`; it initializes, prints a marker, and exits successfully.
4. **Pre-compile provider import.** With no cache, a trusted provider uses a namespace import whose declaration is
   inside its allowed bootstrap root. The provider runs and produces an artifact before runtime loading. An outside-root
   control remains rejected by the existing sandbox tests.
5. **Generated runtime class.** A trusted pre-compile task creates a new global-class script referenced by the main
   scene. The run succeeds only if the post-build rescan observes it.
6. **Clean build avoids a second scan.** Re-run the generated-class project after its build state is clean. The run
   succeeds and emits exactly one verbose scan summary.
7. **Editor child avoids every scan.** Launch a project with a valid cache and a nonzero `--editor-pid`. The run
   succeeds and emits no verbose scan summary.
8. **Cache persistence is untouched.** A missing cache remains missing. A present stale cache is byte-for-byte identical
   after a successful standalone run.
9. **Project-aware engine tests.** A filtered child `test run --project` process proves global declarations are
   available before its pre-compile provider and test probe execute. Filtering prevents recursive execution of the
   parent process test.
10. **Datapack exclusion.** Existing export coverage or a focused policy/process test proves datapack runs do not invoke
    a source scan.

The existing verbose message emitted by `ScriptServer::scan_global_classes()` supplies a deterministic scan counter:

- zero summaries for an editor child;
- one summary for an ordinary standalone run with a clean/disabled pre-compile stage; and
- two summaries when pre-compile successfully ran work.

Do not add wall-clock assertions. They are noisy and weaker than proving the expensive operation was skipped.

## Acceptance Criteria

- [ ] A cacheless standalone main-scene run resolves global class references.
- [ ] A stale-cache standalone main-scene run resolves current source rather than stale entries.
- [ ] A cacheless global script `main_loop_type` starts successfully.
- [ ] Allowed provider-local global declarations and namespace imports resolve before `pre_compile` analysis.
- [ ] A class generated by successful pre-compile work is available to the same run.
- [ ] A clean/disabled pre-compile stage does not cause a second scan.
- [ ] Editor-launched children perform no fallback scan.
- [ ] Direct graphical source runs receive the same policy as direct headless source runs.
- [ ] `project run --script`, project test, project-backed eval, and `test run --project` share the centralized policy.
- [ ] Missing caches remain absent and present caches remain byte-for-byte unchanged.
- [ ] Exported/datapack runs do not scan source.
- [ ] The provider bootstrap dependency sandbox remains enforced.
- [ ] Existing `ScriptServer::scan_global_classes()` discovery, reconciliation, collision, and skip-rule tests pass.
- [ ] Focused startup tests pass:
  `./bin/foundry.* --headless test run --suite "*[Modules][FoundryScript][GlobalClassStartup]*" --force-colors`.
- [ ] The full suite reports `[doctest] Status: SUCCESS!`.
- [ ] Final native strict validation passes with `python3 scripts/agent_build.py --test`.

## Risks and Mitigations

- **Entry-point drift:** a future project-script entry point could bypass the scan. Keep one policy helper and route
  every project-aware startup through it.
- **Editor performance regression:** accidentally scanning F5 children would restore the original concern. Exercise the
  real `--editor-pid` argument in a process test and require zero scan summaries.
- **Unnecessary double scans:** rescanning merely because a build stage is configured would penalize clean runs. Key the
  second pass specifically to `StageRunResult::ran_any_task`.
- **Generated-output race or stale declaration:** the first scan may see old generated files. The second pass reconciles
  the tree only after successful generation completes.
- **Bootstrap sandbox regression:** a populated global index could expose declarations outside a provider root. The
  analyzer's scoped dependency validation remains authoritative, and its outside-root rejection tests stay required.
- **Datapack source assumptions:** exported scripts may not be parseable source. Keep `is_using_datapack()` in the
  shared eligibility policy and test the exclusion.
- **Process-test hangs:** a parse failure can leave a scene tree running without its scripted quit path. Every child run
  needs an engine-side quit bound or harness timeout and forced cleanup.
- **Test contamination:** global declaration state is process-wide. Use subprocesses for startup scenarios and unique
  project identities so one case cannot satisfy another accidentally.

## Expected Files

Implementation is expected to concentrate in:

- `main/main.cpp` for policy, startup ordering, build-result plumbing, and `test run --project` handling;
- the Foundry Script test registration and a focused startup/process test header under
  `modules/foundry_script/tests/`; and
- existing shared temporary-project/process helpers if they need narrowly reusable extensions.

No change to `core/object/script_language.*` should be necessary unless testing reveals that the existing scan cannot
report or expose an already-required behavior without a small, general-purpose adjustment.
