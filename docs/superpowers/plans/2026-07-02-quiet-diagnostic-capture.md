# Quiet Diagnostic Capture for `ScriptDiagnosticCapture` Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers-extended-cc:subagent-driven-development (recommended) or superpowers-extended-cc:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add an opt-in quiet mode to `ScriptDiagnosticCapture` so captured `push_error`/`push_warning`/`push_fatal` diagnostics stop reaching stderr during a successful headless test run, while remaining fully recorded as structured events.

**Architecture:** `ScriptDiagnosticCapture::start()` gains an optional `p_quiet` bool parameter. A ref-counted static counter, mirroring the class's existing `active_capture_count` pattern, toggles the engine's existing `CoreGlobals::print_error_enabled` flag (the same flag `ERR_PRINT_OFF`/`ERR_PRINT_ON` already use) off for as long as at least one quiet capture is active, and restores whatever value it had before the first quiet capture started. No other engine code changes; fatal-diagnostic process isolation is already correct and only needs regression-test coverage.

**Tech Stack:** C++ (Godot engine core), doctest (`tests/test_macros.h`), FoundryScript (`.fs`/`.out` fixtures under `modules/foundry_script/tests/scripts/`).

**Design doc:** `docs/superpowers/specs/2026-07-02-quiet-diagnostic-capture-design.md`

---

### Task 1: Add the quiet-capture mechanism to `ScriptDiagnosticCapture`

**Goal:** `ScriptDiagnosticCapture::start(true)` suppresses stderr printing of subsequently captured diagnostics while still recording them as events; `start()`/`start(false)` is unchanged.

**Files:**
- Modify: `core/object/script_diagnostic_capture.h`
- Modify: `core/object/script_diagnostic_capture.cpp`
- Test: `tests/core/object/test_script_diagnostic_capture.h`

**Acceptance Criteria:**
- [ ] `start(bool p_quiet = false)` exists; `is_quiet()` exists and reflects whether the currently active capture is quiet.
- [ ] A quiet capture sets `CoreGlobals::print_error_enabled = false` for its duration and restores the exact prior value on `stop()`.
- [ ] A non-quiet capture (`start()` / `start(false)`) never touches `CoreGlobals::print_error_enabled`.
- [ ] Diagnostics are still recorded as events regardless of quiet mode.

**Verify:** `./bin/godot.* --test --test-case="*ScriptDiagnosticCapture*" --force-colors` → `[doctest] Status: SUCCESS!`

**Steps:**

- [ ] **Step 1: Write the failing tests**

Add to `tests/core/object/test_script_diagnostic_capture.h`, after the existing two `TEST_CASE`s (before the closing `} // namespace TestScriptDiagnosticCapture`). First add the missing include at the top of the file (with the existing includes):

```cpp
#include "core/core_globals.h"
#include "core/object/script_diagnostic_capture.h"

#include "tests/test_macros.h"
```

Then add:

```cpp
TEST_CASE("[ScriptDiagnosticCapture] Quiet mode suppresses error printing while active") {
	const bool errors_enabled_before = CoreGlobals::print_error_enabled;
	CoreGlobals::print_error_enabled = true;

	Ref<ScriptDiagnosticCapture> capture;
	capture.instantiate();

	capture->start(true);
	CHECK(capture->is_quiet());
	CHECK_FALSE(CoreGlobals::print_error_enabled);

	ERR_PRINT("captured error");
	WARN_PRINT("captured warning");

	capture->stop();
	CHECK_FALSE(capture->is_quiet());
	CHECK(CoreGlobals::print_error_enabled);

	CHECK(capture->get_event_count() == 2);
	CHECK(capture->has_error("captured error"));
	CHECK(capture->has_warning("captured warning"));

	CoreGlobals::print_error_enabled = errors_enabled_before;
}

TEST_CASE("[ScriptDiagnosticCapture] Default start() keeps error printing enabled") {
	const bool errors_enabled_before = CoreGlobals::print_error_enabled;
	CoreGlobals::print_error_enabled = true;

	Ref<ScriptDiagnosticCapture> capture;
	capture.instantiate();

	capture->start();
	CHECK_FALSE(capture->is_quiet());
	CHECK(CoreGlobals::print_error_enabled);

	ERR_PRINT("captured error");
	capture->stop();

	CHECK(capture->get_event_count() == 1);
	CHECK(CoreGlobals::print_error_enabled);

	CoreGlobals::print_error_enabled = errors_enabled_before;
}
```

- [ ] **Step 2: Run tests to verify they fail to compile/fail**

Run: `./bin/godot.* --test --test-case="*ScriptDiagnosticCapture*" --force-colors`
Expected: build error (`start(bool)`/`is_quiet()` do not exist yet). If a stale binary exists without the new test file compiled in, rebuild first with the command from Task 1's Verify line's build step (see project `scons` command in `CLAUDE.md`).

- [ ] **Step 3: Implement the quiet-capture mechanism**

In `core/object/script_diagnostic_capture.h`, add `quiet` state and the new methods:

```cpp
	ErrorHandlerList error_handler;
	LocalVector<Event> events;
	bool active = false;
	bool quiet = false;
```

```cpp
public:
	static bool has_active_capture();

	void start(bool p_quiet = false);
	void stop();
	bool is_active() const { return active; }
	bool is_quiet() const { return quiet; }
```

In `core/object/script_diagnostic_capture.cpp`, add the include and the second ref-counted static:

```cpp
#include "script_diagnostic_capture.h"

#include "core/core_globals.h"

#include <atomic>

namespace {

std::atomic<int> active_capture_count = 0;
std::atomic<int> quiet_capture_count = 0;
bool saved_print_error_enabled = true;
```

Update `_bind_methods()`'s `start` binding:

```cpp
	ClassDB::bind_method(D_METHOD("start", "quiet"), &ScriptDiagnosticCapture::start, DEFVAL(false));
	ClassDB::bind_method(D_METHOD("stop"), &ScriptDiagnosticCapture::stop);
	ClassDB::bind_method(D_METHOD("is_active"), &ScriptDiagnosticCapture::is_active);
	ClassDB::bind_method(D_METHOD("is_quiet"), &ScriptDiagnosticCapture::is_quiet);
```

Replace `start()`/`stop()`:

```cpp
void ScriptDiagnosticCapture::start(bool p_quiet) {
	if (active) {
		return;
	}

	error_handler.errfunc = _error_handler;
	error_handler.userdata = this;
	add_error_handler(&error_handler);
	active = true;
	active_capture_count.fetch_add(1, std::memory_order_relaxed);

	quiet = p_quiet;
	if (quiet && quiet_capture_count.fetch_add(1, std::memory_order_relaxed) == 0) {
		saved_print_error_enabled = CoreGlobals::print_error_enabled;
		CoreGlobals::print_error_enabled = false;
	}
}

void ScriptDiagnosticCapture::stop() {
	if (!active) {
		return;
	}

	remove_error_handler(&error_handler);
	active = false;
	active_capture_count.fetch_sub(1, std::memory_order_relaxed);

	if (quiet) {
		quiet = false;
		if (quiet_capture_count.fetch_sub(1, std::memory_order_relaxed) == 1) {
			CoreGlobals::print_error_enabled = saved_print_error_enabled;
		}
	}
}
```

- [ ] **Step 4: Build and run tests to verify they pass**

Run: `scons platform=macos target=editor dev_build=yes tests=yes` then `./bin/godot.* --test --test-case="*ScriptDiagnosticCapture*" --force-colors`
Expected: `[doctest] Status: SUCCESS!` with all `[ScriptDiagnosticCapture]` cases passing, including the two pre-existing ones.

- [ ] **Step 5: Commit**

```bash
git add core/object/script_diagnostic_capture.h core/object/script_diagnostic_capture.cpp tests/core/object/test_script_diagnostic_capture.h
git commit -m "Add quiet mode to ScriptDiagnosticCapture"
```

---

### Task 2: Regression-test quiet-capture composition (nesting and restore precedence)

**Goal:** Pin down and prove the documented "ref-counted, engine-wide while any quiet capture is active" behavior, including correct restoration when `CoreGlobals::print_error_enabled` was already `false` before the quiet capture started.

**Files:**
- Test: `tests/core/object/test_script_diagnostic_capture.h`

**Acceptance Criteria:**
- [ ] Two overlapping quiet captures keep printing suppressed until the last one stops.
- [ ] A quiet capture started while printing was already disabled leaves it disabled afterward (does not force it back on).
- [ ] A non-quiet capture nested inside an active quiet capture does not re-enable printing while the quiet capture is still active.
- [ ] A quiet capture nested inside an active non-quiet capture suppresses printing only for its own duration, restoring the prior (enabled) state once it stops, independent of the still-active outer non-quiet capture.

**Verify:** `./bin/godot.* --test --test-case="*ScriptDiagnosticCapture*" --force-colors` → `[doctest] Status: SUCCESS!`

**Steps:**

- [ ] **Step 1: Write the tests**

Add to `tests/core/object/test_script_diagnostic_capture.h`, after the tests added in Task 1:

```cpp
TEST_CASE("[ScriptDiagnosticCapture] Overlapping quiet captures stay suppressed until the last one stops") {
	const bool errors_enabled_before = CoreGlobals::print_error_enabled;
	CoreGlobals::print_error_enabled = true;

	Ref<ScriptDiagnosticCapture> outer;
	outer.instantiate();
	Ref<ScriptDiagnosticCapture> inner;
	inner.instantiate();

	outer->start(true);
	CHECK_FALSE(CoreGlobals::print_error_enabled);

	inner->start(true);
	CHECK_FALSE(CoreGlobals::print_error_enabled);

	inner->stop();
	CHECK_FALSE(CoreGlobals::print_error_enabled);

	outer->stop();
	CHECK(CoreGlobals::print_error_enabled);

	CoreGlobals::print_error_enabled = errors_enabled_before;
}

TEST_CASE("[ScriptDiagnosticCapture] Quiet capture restores prior disabled state instead of forcing enabled") {
	const bool errors_enabled_before = CoreGlobals::print_error_enabled;
	CoreGlobals::print_error_enabled = false;

	Ref<ScriptDiagnosticCapture> capture;
	capture.instantiate();

	capture->start(true);
	CHECK_FALSE(CoreGlobals::print_error_enabled);

	capture->stop();
	CHECK_FALSE(CoreGlobals::print_error_enabled);

	CoreGlobals::print_error_enabled = errors_enabled_before;
}

TEST_CASE("[ScriptDiagnosticCapture] Non-quiet capture nested inside a quiet capture does not re-enable printing") {
	const bool errors_enabled_before = CoreGlobals::print_error_enabled;
	CoreGlobals::print_error_enabled = true;

	Ref<ScriptDiagnosticCapture> quiet_capture;
	quiet_capture.instantiate();
	Ref<ScriptDiagnosticCapture> loud_capture;
	loud_capture.instantiate();

	quiet_capture->start(true);
	loud_capture->start();
	CHECK_FALSE(CoreGlobals::print_error_enabled);

	loud_capture->stop();
	CHECK_FALSE(CoreGlobals::print_error_enabled);

	quiet_capture->stop();
	CHECK(CoreGlobals::print_error_enabled);

	CoreGlobals::print_error_enabled = errors_enabled_before;
}

TEST_CASE("[ScriptDiagnosticCapture] Quiet capture nested inside a non-quiet capture only suppresses for its own duration") {
	const bool errors_enabled_before = CoreGlobals::print_error_enabled;
	CoreGlobals::print_error_enabled = true;

	Ref<ScriptDiagnosticCapture> loud_capture;
	loud_capture.instantiate();
	Ref<ScriptDiagnosticCapture> quiet_capture;
	quiet_capture.instantiate();

	loud_capture->start();
	CHECK(CoreGlobals::print_error_enabled);

	quiet_capture->start(true);
	CHECK_FALSE(CoreGlobals::print_error_enabled);

	quiet_capture->stop();
	CHECK(CoreGlobals::print_error_enabled);

	loud_capture->stop();
	CHECK(CoreGlobals::print_error_enabled);

	CoreGlobals::print_error_enabled = errors_enabled_before;
}
```

- [ ] **Step 2: Run tests to verify they pass**

Run: `./bin/godot.* --test --test-case="*ScriptDiagnosticCapture*" --force-colors`
Expected: `[doctest] Status: SUCCESS!`. These should pass immediately against Task 1's implementation with no further production-code changes — if any fail, the ref-counting logic from Task 1 has a bug and must be fixed before proceeding.

- [ ] **Step 3: Commit**

```bash
git add tests/core/object/test_script_diagnostic_capture.h
git commit -m "Test ScriptDiagnosticCapture quiet-mode composition"
```

---

### Task 3: Regression-test fatal-diagnostic process isolation

**Goal:** Explicitly document, via tests, that a captured fatal diagnostic never triggers `OS::request_exit`, and that quiet mode suppresses its stderr output the same as any other captured diagnostic — matching the request doc's "Existing Fatal Diagnostic Behavior" acceptance criteria.

**Files:**
- Modify: `tests/core/variant/test_variant_utility.h`
- Modify: `tests/core/object/test_script_diagnostic_capture.h`

**Acceptance Criteria:**
- [ ] A new case in the existing `[VariantUtility] push_fatal decision logic` test proves an active `ScriptDiagnosticCapture` prevents `push_fatal` from requesting process exit, even when `application/run/push_fatal_terminates` is enabled.
- [ ] A new `ScriptDiagnosticCapture` test proves a quiet capture suppresses stderr printing for a captured fatal diagnostic while still recording it.

**Verify:** `./bin/godot.* --test --test-case="*VariantUtility*push_fatal*" --test-case="*ScriptDiagnosticCapture*" --force-colors` → `[doctest] Status: SUCCESS!`

**Steps:**

- [ ] **Step 1: Write the failing test — capture guards `push_fatal` exit request**

In `tests/core/variant/test_variant_utility.h`, add the include:

```cpp
#include "core/config/engine.h"
#include "core/config/project_settings.h"
#include "core/object/script_diagnostic_capture.h"
#include "core/os/os.h"
```

Then, inside `TEST_CASE("[VariantUtility] push_fatal decision logic")`, insert a new case between the existing "Case 4" block and the final state-restoration comment:

```cpp
	// Case 5: active capture -> log only, no exit request, even with the setting enabled.
	{
		settings->set_setting("application/run/push_fatal_terminates", true);

		Ref<ScriptDiagnosticCapture> capture;
		capture.instantiate();
		capture->start();

		const bool before = os->is_exit_requested();
		Variant::call_utility_function("push_fatal", &ret, args, 1, call_error);
		CHECK(call_error.error == Callable::CallError::CALL_OK);
		CHECK(os->is_exit_requested() == before);
		CHECK(capture->has_fatal("fatal!"));

		capture->stop();
	}
```

- [ ] **Step 2: Write the failing test — quiet capture suppresses a fatal's stderr output**

In `tests/core/object/test_script_diagnostic_capture.h`, add after the tests from Task 2:

```cpp
TEST_CASE("[ScriptDiagnosticCapture] Quiet capture suppresses stderr output for a captured fatal diagnostic") {
	const bool errors_enabled_before = CoreGlobals::print_error_enabled;
	CoreGlobals::print_error_enabled = true;

	Ref<ScriptDiagnosticCapture> capture;
	capture.instantiate();

	capture->start(true);
	_err_print_error(FUNCTION_STR, __FILE__, __LINE__, "quiet fatal", false, ERR_HANDLER_FATAL);
	CHECK_FALSE(CoreGlobals::print_error_enabled);
	capture->stop();

	CHECK(capture->has_fatal("quiet fatal"));
	CHECK(CoreGlobals::print_error_enabled);

	CoreGlobals::print_error_enabled = errors_enabled_before;
}
```

- [ ] **Step 3: Run tests to verify they pass**

Run: `./bin/godot.* --test --test-case="*VariantUtility*push_fatal*" --test-case="*ScriptDiagnosticCapture*" --force-colors`
Expected: `[doctest] Status: SUCCESS!`. Case 5 should already pass against current `push_fatal`/`has_active_capture()` behavior with no production-code changes (per the design doc, this invariant already holds); the new `ScriptDiagnosticCapture` case should pass against Task 1's implementation.

- [ ] **Step 4: Commit**

```bash
git add tests/core/variant/test_variant_utility.h tests/core/object/test_script_diagnostic_capture.h
git commit -m "Add fatal-diagnostic process-isolation regression tests"
```

---

### Task 4: FoundryScript-level fixture exercising quiet capture end-to-end

**Goal:** Prove the `start(bool)` binding works from actual FoundryScript source (not just direct C++ calls), covering the GDScript-facing API surface FoundryLib's testlib will actually call.

**Files:**
- Create: `modules/foundry_script/tests/scripts/runtime/features/script_diagnostic_capture_quiet.fs`
- Create: `modules/foundry_script/tests/scripts/runtime/features/script_diagnostic_capture_quiet.out`

**Acceptance Criteria:**
- [ ] A FoundryScript script calls `ScriptDiagnosticCapture.new()`, `start(true)`, `push_error`/`push_warning`/`push_fatal`, `stop()`, and asserts via `has_error`/`has_warning`/`has_fatal`/`get_event_count()` exactly like the existing non-quiet fixture.
- [ ] The fixture's generated `.out` matches on a clean run.

**Verify:** `./bin/godot.* --headless --gdscript-generate-tests modules/foundry_script/tests/scripts` regenerates `script_diagnostic_capture_quiet.out` with `FS_TEST_OK` as line 1 and no diff on a second run.

**Steps:**

- [ ] **Step 1: Write the fixture script**

Create `modules/foundry_script/tests/scripts/runtime/features/script_diagnostic_capture_quiet.fs`, mirroring the existing `script_diagnostic_capture.fs` but passing `true` to `start()`. `is_quiet()` is read before `stop()`, since `stop()` resets `quiet` back to `false` (per Task 1):

```
func test():
	var capture := ScriptDiagnosticCapture.new()
	capture.start(true)
	print(capture.is_quiet())
	push_error("captured error")
	push_warning("captured warning")
	emit_fatal()
	capture.stop()

	print(capture.get_event_count())
	print(capture.has_error("captured error"))
	print(capture.has_warning("captured warning"))
	print(capture.has_fatal("captured fatal"))
	print(capture.has_error("captured fatal"))

func emit_fatal() -> void:
	push_fatal("captured fatal")
```

- [ ] **Step 2: Generate the expected output**

Build first if needed (`scons platform=macos target=editor dev_build=yes tests=yes`), then run:

```bash
./bin/godot.* --headless --gdscript-generate-tests modules/foundry_script/tests/scripts
```

This creates `modules/foundry_script/tests/scripts/runtime/features/script_diagnostic_capture_quiet.out`. Inspect it — expected content is:

```
FS_TEST_OK
true
3
true
true
true
false
```

- [ ] **Step 3: Verify the fixture is stable**

Run the same generate command again:

```bash
./bin/godot.* --headless --gdscript-generate-tests modules/foundry_script/tests/scripts
```

Expected: no diff in `script_diagnostic_capture_quiet.out` (confirms the fixture is deterministic).

- [ ] **Step 4: Commit**

```bash
git add modules/foundry_script/tests/scripts/runtime/features/script_diagnostic_capture_quiet.fs modules/foundry_script/tests/scripts/runtime/features/script_diagnostic_capture_quiet.out
git commit -m "Add FoundryScript fixture for quiet diagnostic capture"
```
