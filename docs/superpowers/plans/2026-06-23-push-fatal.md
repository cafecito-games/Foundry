# push_fatal() Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers-extended-cc:subagent-driven-development (recommended) or superpowers-extended-cc:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a global `push_fatal(...)` utility function that logs an error and then gracefully terminates the running project with a non-zero exit code, with an editor exemption and a project-setting downgrade to plain `push_error` behavior.

**Architecture:** `push_fatal` is a `core` Variant utility function (sibling to `push_error`/`push_warning`). It logs via `ERR_PRINT`, then — unless running inside the editor or disabled by the `application/run/push_fatal_terminates` project setting — calls a new `OS::request_exit()`. `OS` stores an atomic exit-request flag; `Main::iteration()` reads it after the main-loop `process()` step and returns, so the loop unwinds and `finalize()` runs (graceful, not mid-frame).

**Tech Stack:** C++ (Godot Engine), SCons build, doctest unit tests, ProjectSettings, Foundry Script-facing utility-function binding.

**Spec:** `docs/superpowers/specs/2026-06-23-push-fatal-design.md`

**Build/test commands (this fork, macOS):**
- Build: `scons platform=macos target=editor dev_build=yes tests=yes -j$(sysctl -n hw.ncpu)`
- Output binary: `bin/godot.macos.editor.dev.<arch>` (arch is `arm64` or `x86_64`)
- Run unit tests: `./bin/godot.macos.editor.dev.* --headless --test --force-colors`
- Run a single doctest case: append `--test-case="<name>"` (doctest filter), e.g. `--test-case="[OS] Exit request flag"`

---

### Task 1: OS exit-request flag (`request_exit` / `is_exit_requested`)

**Goal:** Give `core` a thread-safe, one-way "please quit with this exit code" signal that does not depend on `scene/`.

**Files:**
- Modify: `core/os/os.h` (add include, private `SafeFlag` member, two public method decls)
- Modify: `core/os/os.cpp` (implement the two methods)
- Test: `tests/core/os/test_os.h` (new `TEST_CASE` in `namespace TestOS`)

**Acceptance Criteria:**
- [ ] `OS::request_exit(int p_exit_code = EXIT_FAILURE)` stores the exit code and sets the flag.
- [ ] `OS::is_exit_requested()` returns whether the flag is set.
- [ ] The flag uses `SafeFlag` so it is safe to set from a non-main thread.
- [ ] New doctest passes.

**Verify:** `./bin/godot.macos.editor.dev.* --headless --test --test-case="[OS] Exit request flag"` → `[doctest] test cases: 1 | 1 passed`

**Steps:**

- [ ] **Step 1: Write the failing test**

In `tests/core/os/test_os.h`, inside `namespace TestOS { ... }`, add a new test case (place it after the existing test cases, before the closing `}` of the namespace):

```cpp
TEST_CASE("[OS] Exit request flag") {
	OS *os = OS::get_singleton();

	const int saved_exit_code = os->get_exit_code();

	os->request_exit(123);
	CHECK(os->is_exit_requested());
	CHECK(os->get_exit_code() == 123);

	// Restore the exit code so this test does not leak state into others.
	os->set_exit_code(saved_exit_code);
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `scons platform=macos target=editor dev_build=yes tests=yes -j$(sysctl -n hw.ncpu)`
Expected: compile error — `'class OS' has no member named 'request_exit'` / `'is_exit_requested'`.

- [ ] **Step 3: Add the include and member to `core/os/os.h`**

Near the top includes of `core/os/os.h` (the block around `#include "core/templates/list.h"`), add:

```cpp
#include "core/templates/safe_refcount.h"
```

In the private member area (next to `int _exit_code = EXIT_SUCCESS;`, currently `core/os/os.h:59`), add:

```cpp
	SafeFlag _exit_requested;
```

In the public section, immediately after the existing `virtual void set_exit_code(int p_code);` (currently `core/os/os.h:331`), add:

```cpp
	// Requests a graceful shutdown of the main loop with the given exit code.
	// Unlike `set_exit_code`, this can be called from anywhere (including from
	// utility functions like `push_fatal`); `Main::iteration()` observes the
	// request and ends the loop at the next frame boundary.
	void request_exit(int p_exit_code = EXIT_FAILURE);
	bool is_exit_requested() const;
```

- [ ] **Step 4: Implement the methods in `core/os/os.cpp`**

Immediately after the existing `set_exit_code` definition (currently `core/os/os.cpp:224-226`), add:

```cpp
void OS::request_exit(int p_exit_code) {
	_exit_code = p_exit_code;
	_exit_requested.set();
}

bool OS::is_exit_requested() const {
	return _exit_requested.is_set();
}
```

- [ ] **Step 5: Build and run the test**

Run: `scons platform=macos target=editor dev_build=yes tests=yes -j$(sysctl -n hw.ncpu)`
Then: `./bin/godot.macos.editor.dev.* --headless --test --test-case="[OS] Exit request flag"`
Expected: `[doctest] test cases: 1 | 1 passed | 0 failed`

- [ ] **Step 6: Commit**

```bash
git add core/os/os.h core/os/os.cpp tests/core/os/test_os.h
git commit -m "Add OS exit-request flag for graceful programmatic quit"
```

---

### Task 2: Honor the exit request in `Main::iteration()`

**Goal:** Make the main loop end gracefully when `OS::is_exit_requested()` becomes true (e.g. set from a script during `process()`).

**Files:**
- Modify: `main/main.cpp` (`Main::iteration()`, just after the main-loop `process()` block)

**Acceptance Criteria:**
- [ ] After the main loop's `process()` call, `Main::iteration()` ORs `OS::is_exit_requested()` into its `exit` result.
- [ ] The check sits before the `fixed_fps != -1` early return so it is honored on both return paths.
- [ ] Engine builds cleanly.
- [ ] Manual repro: a script calling `OS.request_exit()`-equivalent (via `push_fatal`, validated in Task 3) ends the running project.

**Verify:** `scons platform=macos target=editor dev_build=yes tests=yes -j$(sysctl -n hw.ncpu)` → build succeeds (this is a main-loop integration point; behavior is exercised end-to-end by the Task 3 manual repro).

**Steps:**

- [ ] **Step 1: Locate the insertion point**

In `main/main.cpp`, find the main-loop `process()` block in `Main::iteration()` (currently around `main/main.cpp:4962-4965`):

```cpp
	GodotProfileZoneGrouped(_profile_zone, "process");
	if (OS::get_singleton()->get_main_loop()->process(process_step * time_scale)) {
		exit = true;
	}
	message_queue->flush();
```

- [ ] **Step 2: Add the exit-request check**

Immediately after that `message_queue->flush();` line (and before the navigation `process` calls that follow), insert:

```cpp
	// A component may have requested a graceful shutdown during process()
	// (e.g. via push_fatal()). Honor it here so this iteration ends the loop;
	// this sits before the `fixed_fps` early-return below so both return paths
	// observe it.
	if (OS::get_singleton()->is_exit_requested()) {
		exit = true;
	}
```

- [ ] **Step 3: Build**

Run: `scons platform=macos target=editor dev_build=yes tests=yes -j$(sysctl -n hw.ncpu)`
Expected: build succeeds with no warnings about `exit` or `is_exit_requested`.

- [ ] **Step 4: Commit**

```bash
git add main/main.cpp
git commit -m "End main loop when an exit request is pending"
```

---

### Task 3: `push_fatal()` utility function + project setting

**Goal:** Add the `push_fatal` vararg utility function (log + conditional graceful quit), register it, define its project setting, and unit-test the decision logic.

**Files:**
- Modify: `core/variant/variant_utility.h` (declaration after `push_error`/`push_warning`, currently `core/variant/variant_utility.h:140-141`)
- Modify: `core/variant/variant_utility.cpp` (add includes, implement function, register binding)
- Modify: `main/main.cpp` (define the project setting via `GLOBAL_DEF`, near other `application/run/*` defs around `main/main.cpp:2216`)
- Test: `tests/core/variant/test_variant_utility.h` (new `TEST_CASE` in `namespace TestVariantUtility`)

**Acceptance Criteria:**
- [ ] `push_fatal("msg")` logs the joined arguments as an error (`ERR_PRINT`).
- [ ] Calling with zero arguments reports `CALL_ERROR_TOO_FEW_ARGUMENTS` and does **not** request exit.
- [ ] When `Engine::is_editor_hint()` is true, it logs only and does not request exit.
- [ ] When `application/run/push_fatal_terminates` is `false`, it logs only and does not request exit.
- [ ] Otherwise it calls `OS::request_exit(EXIT_FAILURE)`.
- [ ] The setting is registered with default `true`.
- [ ] New doctest passes.

**Verify:** `./bin/godot.macos.editor.dev.* --headless --test --test-case="[VariantUtility] push_fatal decision logic"` → `1 passed`

**Steps:**

- [ ] **Step 1: Write the failing test**

In `tests/core/variant/test_variant_utility.h`, inside `namespace TestVariantUtility { ... }`, add:

```cpp
TEST_CASE("[VariantUtility] push_fatal decision logic") {
	OS *os = OS::get_singleton();
	Engine *engine = Engine::get_singleton();
	ProjectSettings *settings = ProjectSettings::get_singleton();

	// Save state we will mutate so the test does not leak.
	const int saved_exit_code = os->get_exit_code();
	const bool saved_editor_hint = engine->is_editor_hint();
	settings->set_setting("application/run/push_fatal_terminates", true);

	Variant message = "fatal!";
	const Variant *args[1] = { &message };
	Variant ret;
	Callable::CallError call_error;

	// Case 1: too few arguments -> error, no exit request.
	{
		const bool before = os->is_exit_requested();
		Variant::call_utility_function("push_fatal", &ret, nullptr, 0, call_error);
		CHECK(call_error.error == Callable::CallError::CALL_ERROR_TOO_FEW_ARGUMENTS);
		CHECK(os->is_exit_requested() == before);
	}

	// Case 2: editor hint on -> log only, no exit request.
	{
		engine->set_editor_hint(true);
		settings->set_setting("application/run/push_fatal_terminates", true);
		const bool before = os->is_exit_requested();
		Variant::call_utility_function("push_fatal", &ret, args, 1, call_error);
		CHECK(call_error.error == Callable::CallError::CALL_OK);
		CHECK(os->is_exit_requested() == before);
		engine->set_editor_hint(false);
	}

	// Case 3: setting disabled -> log only, no exit request.
	{
		settings->set_setting("application/run/push_fatal_terminates", false);
		const bool before = os->is_exit_requested();
		Variant::call_utility_function("push_fatal", &ret, args, 1, call_error);
		CHECK(call_error.error == Callable::CallError::CALL_OK);
		CHECK(os->is_exit_requested() == before);
	}

	// Case 4: not editor, setting enabled -> request exit with EXIT_FAILURE.
	{
		settings->set_setting("application/run/push_fatal_terminates", true);
		Variant::call_utility_function("push_fatal", &ret, args, 1, call_error);
		CHECK(call_error.error == Callable::CallError::CALL_OK);
		CHECK(os->is_exit_requested());
		CHECK(os->get_exit_code() == EXIT_FAILURE);
	}

	// Restore mutated state.
	engine->set_editor_hint(saved_editor_hint);
	settings->set_setting("application/run/push_fatal_terminates", true);
	os->set_exit_code(saved_exit_code);
}
```

> Note: The test intentionally orders the log-only cases (1–3) before the terminate case (4), because `is_exit_requested()` is one-way within a process; cases 1–3 assert the flag is *unchanged*, so they are robust regardless of prior global state.

Also ensure the test file has the needed includes at the top (after the existing `#include "core/variant/variant_utility.h"`):

```cpp
#include "core/config/engine.h"
#include "core/config/project_settings.h"
#include "core/os/os.h"
```

- [ ] **Step 2: Run test to verify it fails**

Run: `scons platform=macos target=editor dev_build=yes tests=yes -j$(sysctl -n hw.ncpu)`
Expected: link/compile failure or assertion failure — `push_fatal` is not a registered utility function yet (`call_error.error == CALL_ERROR_INVALID_METHOD`).

- [ ] **Step 3: Declare the function in `core/variant/variant_utility.h`**

Immediately after `static void push_warning(...)` (currently `core/variant/variant_utility.h:141`), add:

```cpp
	static void push_fatal(const Variant **p_args, int p_arg_count, Callable::CallError &r_error);
```

- [ ] **Step 4: Add includes to `core/variant/variant_utility.cpp`**

Near the existing `#include "core/os/os.h"` (currently `core/variant/variant_utility.cpp:36`), add:

```cpp
#include "core/config/engine.h"
#include "core/config/project_settings.h"
```

- [ ] **Step 5: Implement `push_fatal` in `core/variant/variant_utility.cpp`**

Immediately after the existing `VariantUtilityFunctions::push_warning` definition (currently ends `core/variant/variant_utility.cpp:1036`), add:

```cpp
void VariantUtilityFunctions::push_fatal(const Variant **p_args, int p_arg_count, Callable::CallError &r_error) {
	if (p_arg_count < 1) {
		r_error.error = Callable::CallError::CALL_ERROR_TOO_FEW_ARGUMENTS;
		r_error.expected = 1;
		// Do not terminate on a malformed call.
		return;
	}

	ERR_PRINT(join_string(p_args, p_arg_count));
	r_error.error = Callable::CallError::CALL_OK;

	// Never terminate the editor process itself (e.g. when called from a
	// @tool script). Termination only applies to the running project.
	if (Engine::get_singleton()->is_editor_hint()) {
		return;
	}

	// A project setting can downgrade push_fatal to plain push_error behavior.
	if (!GLOBAL_GET_CACHED(bool, "application/run/push_fatal_terminates")) {
		return;
	}

	OS::get_singleton()->request_exit(EXIT_FAILURE);
}
```

- [ ] **Step 6: Register the binding in `core/variant/variant_utility.cpp`**

Immediately after the existing `FUNCBINDVARARGV(push_warning, ...)` line (currently `core/variant/variant_utility.cpp:1762`), add:

```cpp
	FUNCBINDVARARGV(push_fatal, sarray(), Variant::UTILITY_FUNC_TYPE_GENERAL);
```

- [ ] **Step 7: Register the project setting in `main/main.cpp`**

Near the other `application/run/*` definitions (currently around `main/main.cpp:2216`, by `GLOBAL_DEF("application/run/delta_smoothing", true);`), add:

```cpp
	GLOBAL_DEF("application/run/push_fatal_terminates", true);
```

- [ ] **Step 8: Build and run the test**

Run: `scons platform=macos target=editor dev_build=yes tests=yes -j$(sysctl -n hw.ncpu)`
Then: `./bin/godot.macos.editor.dev.* --headless --test --test-case="[VariantUtility] push_fatal decision logic"`
Expected: `[doctest] test cases: 1 | 1 passed | 0 failed`

- [ ] **Step 9: Manual end-to-end repro (validates Task 2 too)**

Create a throwaway project script `/tmp/push_fatal_repro/main.fs` and `project.foundry`, or run inline:

```bash
mkdir -p /tmp/push_fatal_repro
cat > /tmp/push_fatal_repro/project.foundry <<'EOF'
config_version=5
[application]
run/main_scene="res://main.tscn"
EOF
cat > /tmp/push_fatal_repro/main.fs <<'EOF'
extends Node
func _ready() -> void:
	print("about to fatal")
	push_fatal("intentional fatal for repro")
	print("THIS SHOULD NOT PRINT")
EOF
cat > /tmp/push_fatal_repro/main.tscn <<'EOF'
[gd_scene load_steps=2 format=3]
[ext_resource type="Script" path="res://main.fs" id="1"]
[node name="Main" type="Node"]
script = ExtResource("1")
EOF
./bin/godot.macos.editor.dev.* --path /tmp/push_fatal_repro 2>&1 | tail -20
echo "exit code: $?"
```

Expected: output contains `about to fatal` and the error, does **not** contain `THIS SHOULD NOT PRINT`, and the process exits non-zero.

- [ ] **Step 10: Commit**

```bash
git add core/variant/variant_utility.h core/variant/variant_utility.cpp main/main.cpp tests/core/variant/test_variant_utility.h
git commit -m "Add push_fatal utility function with editor exemption and project setting"
```

---

### Task 4: Documentation in `@GlobalScope.xml`

**Goal:** Document `push_fatal` and the `application/run/push_fatal_terminates` setting in the class reference.

**Files:**
- Modify: `doc/classes/@GlobalScope.xml` (add `push_fatal` method between `push_error` and `push_warning`, currently `doc/classes/@GlobalScope.xml:951-980`)

**Acceptance Criteria:**
- [ ] A `<method name="push_fatal" qualifiers="vararg">` entry exists with a clear description of: terminate-by-default, non-zero exit code, editor exemption, and the project setting.
- [ ] `pre-commit run --all-files` doc checks pass (XML validation + doc-status).

**Verify:** `pre-commit run --files doc/classes/@GlobalScope.xml` → `Passed` for validate-xml and doc-status.

**Steps:**

- [ ] **Step 1: Add the method entry**

In `doc/classes/@GlobalScope.xml`, between the closing `</method>` of `push_error` (currently `doc/classes/@GlobalScope.xml:964`) and the opening `<method name="push_warning"` (currently `doc/classes/@GlobalScope.xml:966`), insert (methods are kept in alphabetical order — `push_error`, `push_fatal`, `push_warning`):

```xml
		<method name="push_fatal" qualifiers="vararg">
			<return type="void" />
			<description>
				Pushes an error message to Godot's built-in debugger and to the OS terminal (like [method push_error]), then gracefully terminates the running project with a non-zero exit code. Use this for unrecoverable conditions the project should never continue past, such as a required asset failing to load.
				[codeblocks]
				[gdscript]
				push_fatal("Failed to load required asset.") # Logs the error, then quits the project.
				[/gdscript]
				[csharp]
				GD.PushFatal("Failed to load required asset."); // Logs the error, then quits the project.
				[/csharp]
				[/codeblocks]
				Unlike [code]assert()[/code], [method push_fatal] is not compiled out of release builds; it terminates in both debug and exported builds by default.
				[b]Note:[/b] When called from a [code]@tool[/code] script running inside the editor, it only logs the message and does [b]not[/b] terminate, so it cannot close the editor.
				[b]Note:[/b] Set the [member ProjectSettings.application/run/push_fatal_terminates] project setting to [code]false[/code] to downgrade [method push_fatal] to plain [method push_error] behavior (log only, no termination).
			</description>
		</method>
```

- [ ] **Step 2: Document the project setting**

The `application/run/push_fatal_terminates` setting is auto-listed in `ProjectSettings.xml` once registered (Task 3). If `pre-commit`'s `doc-status` reports it as undocumented, add a `<member>` entry for it under the `application/run` group in `doc/classes/ProjectSettings.xml` with a one-line description:

```xml
		<member name="application/run/push_fatal_terminates" type="bool" setter="" getter="" default="true">
			If [code]true[/code] (default), [method @GlobalScope.push_fatal] terminates the running project. If [code]false[/code], it behaves like [method @GlobalScope.push_error] (logs only, no termination).
		</member>
```

(Insert it in alphabetical position among the existing `application/run/*` members.)

- [ ] **Step 3: Run doc checks**

Run: `pre-commit run --files doc/classes/@GlobalScope.xml doc/classes/ProjectSettings.xml`
Expected: `validate-xml` and `doc-status` report `Passed`.

- [ ] **Step 4: Commit**

```bash
git add doc/classes/@GlobalScope.xml doc/classes/ProjectSettings.xml
git commit -m "Document push_fatal and its project setting"
```

---

## Final verification

After all tasks:

- [ ] Full test suite: `./bin/godot.macos.editor.dev.* --headless --test --force-colors` → all pass.
- [ ] CI-parity build (warnings as errors): `scons platform=macos target=editor dev_mode=yes tests=yes -j$(sysctl -n hw.ncpu)` → succeeds.
- [ ] `pre-commit run --all-files` → passes.
- [ ] Manual repro from Task 3 Step 9 still terminates the project and skips the post-`push_fatal` line.
