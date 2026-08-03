# Debugger Payload Cleanup Order Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Prevent restarted structured test children from crashing while queued debugger payloads are released, preserving one natural `exited(0)` event before `terminated`.

**Architecture:** Make the engine debugger a consumer that shuts down before the script-language runtimes backing values in its queues. Reproduce the old ordering failure with a no-main-scene tooling-host fixture that exercises stack values, then move the single debugger teardown point earlier in `Main::cleanup()` and validate the real external conformance matrix.

**Tech Stack:** C++17, Foundry/Godot debugger transport, Foundry Script, doctest tooling-host integration tests, SCons agent build wrapper, Vitest real-engine conformance.

---

## File Map

- `tests/editor/test_editor_tooling_host.h`: stage the no-main-scene configuration and exercise breakpoint, inspection, restart, continue, and natural exit.
- `main/main.cpp`: deinitialize the engine debugger before finishing/unregistering script languages.

### Task 1: Add a reliable no-main-scene RED regression

**Files:**
- Modify: `tests/editor/test_editor_tooling_host.h`

- [ ] **Step 1: Stage the existing fixture without a main scene**

Change `prepare_exit_status_project()` to accept `bool p_with_main_scene = true`. After the existing files are copied into scratch, load the staged `project.foundry` with `ConfigFile`; when the argument is false, erase `application/run/main_scene` and save it back. Return an empty path on load or save failure. Existing callers keep the default project shape, while the restart regression calls `prepare_exit_status_project(false)` through an `ExitStatusSession` constructor parameter.

- [ ] **Step 2: Strengthen the restart test around observed debugger values**

Use the no-main-scene session. After the first `stopped` event, request `stackTrace`, `scopes`, and `variables`, and assert the runner's `file` local is reported as an object. Preserve the existing restart and replacement-stop sequence. Continue the replacement, then assert its lifecycle is exactly:

```cpp
PackedStringArray expected;
expected.push_back("process");
expected.push_back("exited");
expected.push_back("terminated");
CHECK_EQ(session.client.lifecycle_events(), expected);
CHECK_EQ(session.client.exit_code_of_first_exited(), 0);
```

The existing `file` local is the object-bearing value implicated by the external crash; do not add synthetic source-text assertions or a second runner protocol.

- [ ] **Step 3: Run the exact external conformance to record RED**

Before changing production code, run the Foundry-Scripting `origin/main` `test-debugging-live.test.ts` repeatedly against `/Users/christian/bin/foundry` at `2b399079d`. Stop at the first failure and retain its DAP transcript and native backtrace. The investigation already reproduced `exited(6)` with `FSInstance::~FSInstance()` under `RemoteDebuggerPeerTCP` destruction on run 2; the implementer must observe a fresh failure in its own turn.

- [ ] **Step 4: Build the native regression and confirm its baseline behavior**

Run:

```sh
python3 scripts/agent_build.py --backend ninja --test \
  --case "*structured project_test restart preserves the replacement's natural result*"
```

Expected: the new native case executes the exact no-main-scene lifecycle. It may pass on one baseline run because the queue race is timing-dependent; the mandatory RED evidence is the repeated external conformance failure from Step 3.

- [ ] **Step 5: Commit the regression**

```sh
git add tests/editor/test_editor_tooling_host.h
git commit -m "test(dap): Reproduce queued payload teardown crash"
```

### Task 2: Restore debugger-before-language teardown ordering

**Files:**
- Modify: `main/main.cpp`

- [ ] **Step 1: Move the debugger teardown point**

In `Main::cleanup()`, after `WorkerThreadPool::get_singleton()->exit_languages_threads()` and before `ScriptServer::finish_languages()`, add the existing teardown call with an invariant comment:

```cpp
	// Debugger queues can retain script-backed Variant values. Stop the transport
	// and release those values before their script languages are finished or deleted.
	EngineDebugger::deinitialize();

	ScriptServer::finish_languages();
```

Remove the later `EngineDebugger::deinitialize()` after server-module uninitialization. Do not add a second call or alter error/test cleanup paths without evidence that they share the defect.

- [ ] **Step 2: Run GREEN**

Run the same focused command from Task 1. Expected: PASS with `process`, one `exited(0)`, and `terminated`, with no child crash backtrace.

- [ ] **Step 3: Prove the regression detects the production change**

Temporarily restore the old ordering, rerun the focused test, and confirm it fails for the same cleanup-lifetime reason. Restore the fix and rerun to PASS. Do not commit the temporary reversion.

- [ ] **Step 4: Commit the minimal fix**

```sh
git add main/main.cpp
git commit -m "fix(debugger): Release payloads before script teardown"
```

### Task 3: Validate lifecycle contracts and external conformance

**Files:**
- Verify only

- [ ] **Step 1: Run the focused native lifecycle matrix**

```sh
python3 scripts/agent_build.py --backend ninja --test \
  --case "*structured project_test restart preserves the replacement's natural result*" \
  --case "*A structured test launch reports the runner's real result*" \
  --case "*Attaching to an editor launch ends without fabricating a result*" \
  --case "*A forcibly terminated launch reports no exit code*"
```

Expected: all selected cases pass; natural sessions report their real results and forced sessions remain result-less.

- [ ] **Step 2: Run the exact external selected-test conformance repeatedly**

Against the Foundry-Scripting `origin/main` worktree and the newly built binary, run `src/debug/conformance/test-debugging-live.test.ts` at least five consecutive times. Expected: every run passes and every replacement reports `exited(0)` before `terminated`.

- [ ] **Step 3: Run required strict native validation**

```sh
python3 scripts/agent_build.py
```

Expected: exit code `0` with `dev_mode=yes`, `dev_build=yes`, `tests=yes`, and no warnings-as-errors failures.

- [ ] **Step 4: Run the tooling-host group on the strict binary**

```sh
./bin/foundry.* --headless test run --case "*[Editor][ToolingHost]*" --force-colors
```

Expected: doctest reports success for the complete tooling-host group.

- [ ] **Step 5: Check repository hygiene**

```sh
git diff --check origin/develop...HEAD
git status --short
git grep -nE '\.split\("(void|bool|String|private fun|const val|def )' -- '*.py'
git grep -n "docs/superpowers" -- '*/test_*.py' '*/check_*.py'
git grep -n "hasattr(" -- '*/test_*.py'
git grep -l "assert" -- 'misc/checks/check_*.py'
```

Expected: no whitespace errors or uncommitted implementation changes. Identify any pre-existing policy output separately.
