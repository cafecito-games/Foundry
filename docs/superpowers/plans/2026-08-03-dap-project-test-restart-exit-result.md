# DAP Project-Test Restart Exit Result Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or
> superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Preserve the natural exit code and `exited` event of a restarted structured `project_test` DAP launch.

**Architecture:** Propagate the stopped debuggee's PID from `ScriptEditorDebugger` to `EditorRunBar`, then ignore
debugger-stop notifications that do not identify the current launch's represented process. Keep launch IDs
authoritative for process-result acceptance and cover the complete breakpoint/restart/continue lifecycle over the real
tooling-host DAP socket.

**Tech Stack:** C++17, Godot/Foundry signals, doctest, Foundry tooling-host DAP integration fixtures, SCons agent build
wrapper.

---

## File Map

- `tests/editor/test_editor_tooling_host.h`: add the native structured `project_test` restart regression.
- `editor/debugger/script_editor_debugger.cpp`: retain the PID while stopping and include it in the internal `stopped`
  signal.
- `editor/debugger/editor_debugger_node.h`: accept the stopped PID in the debugger callback.
- `editor/debugger/editor_debugger_node.cpp`: forward the stopped PID after result coordination.
- `editor/editor_node.h`: add the PID to the run-bar forwarding method.
- `editor/editor_node.cpp`: forward the PID to `EditorRunBar`.
- `editor/run/editor_run_bar.h`: make debug-session closure process-specific.
- `editor/run/editor_run_bar.cpp`: ignore stale closures from replaced processes.

### Task 1: Add the structured project-test restart regression

**Files:**
- Modify: `tests/editor/test_editor_tooling_host.h`

- [ ] **Step 1: Write the failing end-to-end test**

Add a test after `A structured test launch reports the runner's real result`. It must use `ExitStatusSession`,
register line 41 of `exit_status_runner.fs`, launch `exit::0`, restart while stopped, retain only the replacement
lifecycle window, wait beyond the five-second result grace while the replacement remains stopped, continue it, and
assert one natural result:

```cpp
TEST_CASE("[Editor][ToolingHost] A structured project_test restart preserves the replacement's natural result") {
	ExitStatusSession session;
	REQUIRE_MESSAGE(!session.project_path.is_empty(), "Failed to stage the exit-status project.");
	INFO("Tooling host output:\n", session.host.output);
	REQUIRE_MESSAGE(session.ready, "The tooling host never accepted a debug adapter session.");

	const int breakpoint_line = 41;
	Dictionary source;
	source["path"] = session.artifact("exit_status_runner.fs");
	Dictionary breakpoint;
	breakpoint["line"] = breakpoint_line;
	Array requested_breakpoints;
	requested_breakpoints.push_back(breakpoint);
	Dictionary set_breakpoints_arguments;
	set_breakpoints_arguments["source"] = source;
	set_breakpoints_arguments["breakpoints"] = requested_breakpoints;
	const Dictionary set_breakpoints_response = session.client.await_response(
			session.client.send_request("setBreakpoints", set_breakpoints_arguments), 30000);
	REQUIRE_MESSAGE(bool(set_breakpoints_response.get("success", false)), "setBreakpoints did not succeed.");

	const Dictionary launch_arguments = make_project_test_launch(session.artifact("restart.tap"), "exit::0");
	const int launch_seq = session.client.send_request("launch", launch_arguments);
	session.client.await_response(session.client.send_request("configurationDone", Dictionary()), 30000);
	REQUIRE_MESSAGE(bool(session.client.await_response(launch_seq, 60000).get("success", false)),
			"launch did not succeed.");
	REQUIRE_MESSAGE(!session.client.await_event("process", 120000).is_empty(), "The first runner never started.");
	REQUIRE_MESSAGE(!session.client.await_event("stopped", 120000).is_empty(), "The first runner never stopped.");

	session.client.clear_events();
	Dictionary restart_arguments;
	restart_arguments["arguments"] = launch_arguments;
	const Dictionary restart_response = session.client.await_response(
			session.client.send_request("restart", restart_arguments), 60000);
	REQUIRE_MESSAGE(bool(restart_response.get("success", false)), "restart did not succeed.");
	REQUIRE_MESSAGE(!session.client.await_event("process", 120000).is_empty(), "The replacement runner never started.");
	REQUIRE_MESSAGE(!session.client.await_event("stopped", 120000).is_empty(), "The replacement runner never stopped.");

	// The replaced debugger's delayed stop must not start the replacement's five-second
	// process-result grace window while the replacement is intentionally paused.
	CHECK(session.client.await_event("terminated", 7000).is_empty());

	Dictionary continue_arguments;
	continue_arguments["threadId"] = 1;
	const Dictionary continue_response = session.client.await_response(
			session.client.send_request("continue", continue_arguments), 30000);
	REQUIRE_MESSAGE(bool(continue_response.get("success", false)), "continue did not succeed.");
	REQUIRE_MESSAGE(!session.client.await_event("terminated", 120000).is_empty(),
			"The replacement runner never terminated.");

	CHECK_EQ(session.client.lifecycle_events(), expected_known_result_lifecycle());
	CHECK_EQ(session.client.exit_code_of_first_exited(), 0);
}
```

- [ ] **Step 2: Build and run the new test to verify RED**

Run:

```sh
python3 scripts/agent_build.py --backend ninja --test \
  --case "*structured project_test restart preserves the replacement's natural result*"
```

Expected: FAIL after the replacement emits `terminated` during or after the stale grace window without `exited(0)`. If
the race does not reproduce, stop and strengthen the lifecycle synchronization before changing production code.

- [ ] **Step 3: Commit the regression test**

```sh
git add tests/editor/test_editor_tooling_host.h
git commit -m "test(dap): Reproduce lost project test restart result"
```

### Task 2: Scope debugger-stop notifications to their process

**Files:**
- Modify: `editor/debugger/script_editor_debugger.cpp`
- Modify: `editor/debugger/editor_debugger_node.h`
- Modify: `editor/debugger/editor_debugger_node.cpp`
- Modify: `editor/editor_node.h`
- Modify: `editor/editor_node.cpp`
- Modify: `editor/run/editor_run_bar.h`
- Modify: `editor/run/editor_run_bar.cpp`

- [ ] **Step 1: Preserve the stopped PID in the signal**

Change `ScriptEditorDebugger::_stop_and_notify()` and the signal declaration:

```cpp
void ScriptEditorDebugger::_stop_and_notify() {
	const OS::ProcessID stopped_process = remote_pid;
	stop();
	emit_signal(SNAME("stopped"), (int64_t)stopped_process);
	_set_reason_text(TTRC("Debug session closed."), MESSAGE_WARNING);
}

ADD_SIGNAL(MethodInfo("stopped", PropertyInfo(Variant::INT, "process_id")));
```

- [ ] **Step 2: Forward the PID through debugger and editor ownership layers**

Update the callback and forwarding signatures:

```cpp
// editor/debugger/editor_debugger_node.h
void _debugger_stopped(int64_t p_process_id, int p_id);

// editor/debugger/editor_debugger_node.cpp
void EditorDebuggerNode::_debugger_stopped(int64_t p_process_id, int p_id) {
	ScriptEditorDebugger *dbg = get_debugger(p_id);
	ERR_FAIL_NULL(dbg);

	bool found = false;
	_for_all(tabs, [&](ScriptEditorDebugger *p_debugger) {
		if (p_debugger->is_session_active()) {
			found = true;
		}
	});
	if (!found) {
		_finalize_debug_session(session_coordinator.observe_debugger_stopped());
		EditorRunBar::get_singleton()->get_pause_button()->set_pressed(false);
		EditorRunBar::get_singleton()->get_pause_button()->set_disabled(true);
		SceneTreeDock *dock = EditorNode::get_singleton()->get_focused_scene_tree_dock();
		if (dock->is_inside_tree()) {
			dock->hide_remote_tree();
			dock->hide_tab_buttons();
		}
		EditorNode::get_singleton()->notify_all_debug_sessions_exited((OS::ProcessID)p_process_id);
	}
}

// editor/editor_node.h
void notify_all_debug_sessions_exited(OS::ProcessID p_process_id);

// editor/editor_node.cpp
void EditorNode::notify_all_debug_sessions_exited(OS::ProcessID p_process_id) {
	project_run_bar->debug_sessions_exited(p_process_id);
}
```

- [ ] **Step 3: Reject stale process notifications in the run bar**

Update the public signature and add the identity guard before the existing grace handling:

```cpp
// editor/run/editor_run_bar.h
void debug_sessions_exited(OS::ProcessID p_process_id);

// editor/run/editor_run_bar.cpp
void EditorRunBar::debug_sessions_exited(OS::ProcessID p_process_id) {
	if (editor_run.get_status() == EditorRun::STATUS_STOP) {
		return;
	}

	if (represented_process != 0 && p_process_id != represented_process) {
		// A replaced debugger can finish closing after its replacement launch starts.
		// Its socket lifecycle must not start or finish cleanup for the new process.
		return;
	}

	if (represented_process != 0 && editor_run.has_child_process(represented_process)) {
		if (process_result_deadline_msec == 0) {
			process_result_deadline_msec = OS::get_singleton()->get_ticks_msec() + PROCESS_RESULT_GRACE_MSEC;
		}
		return;
	}

	stop_playing();
}
```

- [ ] **Step 4: Run the new test to verify GREEN**

Run:

```sh
python3 scripts/agent_build.py --backend ninja --test \
  --case "*structured project_test restart preserves the replacement's natural result*"
```

Expected: PASS with lifecycle `process`, `exited`, `terminated` and exit code `0`.

- [ ] **Step 5: Commit the minimal fix**

```sh
git add editor/debugger/script_editor_debugger.cpp \
  editor/debugger/editor_debugger_node.h editor/debugger/editor_debugger_node.cpp \
  editor/editor_node.h editor/editor_node.cpp \
  editor/run/editor_run_bar.h editor/run/editor_run_bar.cpp
git commit -m "fix(dap): Preserve project test result across restart"
```

### Task 3: Verify adjacent lifecycle contracts and strict compilation

**Files:**
- Verify only

- [ ] **Step 1: Run the focused lifecycle matrix with the fast backend**

Run:

```sh
python3 scripts/agent_build.py --backend ninja --test \
  --case "*structured project_test restart preserves the replacement's natural result*" \
  --case "*A breakpoint exposes frame locals members and a clean exit*" \
  --case "*A structured test launch reports the runner's real result*" \
  --case "*A forcibly terminated launch reports no exit code*"
```

Expected: all selected cases PASS; natural launches include real `exited` results and forced termination still omits
`exited`.

- [ ] **Step 2: Run the required strict native validation build**

Run:

```sh
python3 scripts/agent_build.py
```

Expected: exit code `0` with `dev_mode=yes`, `dev_build=yes`, `tests=yes`, and no warnings-as-errors failures.

- [ ] **Step 3: Run the complete tooling-host test group on the strict binary**

Run:

```sh
./bin/foundry.macos.editor.dev.arm64 --headless test run \
  --case "*[Editor][ToolingHost]*" --force-colors
```

Expected: every tooling-host test passes. Trust the doctest status line and report any cleanup-only nonzero exit
separately.

- [ ] **Step 4: Check the final diff and repository policy hygiene**

Run:

```sh
git diff --check origin/develop...HEAD
git status --short
git grep -nE '\.split\("(void|bool|String|private fun|const val|def )' -- '*.py'
git grep -n "docs/superpowers" -- '*/test_*.py' '*/check_*.py'
git grep -n "hasattr(" -- '*/test_*.py'
git grep -l "assert" -- 'misc/checks/check_*.py'
```

Expected: no whitespace errors or uncommitted implementation changes. Any pre-existing policy output must be identified
rather than attributed to this fix.
