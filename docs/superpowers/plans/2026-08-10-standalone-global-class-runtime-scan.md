# Standalone Global-Class Runtime Scan Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or
> superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make every standalone source-project entry point resolve current global script declarations before build or
runtime analysis without scanning editor children or datapacks.

**Architecture:** A small, pure policy object decides whether a project-script entry point may scan. `Main` runs the
existing in-memory scan before `pre_compile`, then repeats it only when the stage reports that it ran work. Normal
runtime startup and `test run --project` share this policy, while process-isolated tests prove ordering and scan counts.

**Tech Stack:** C++17, Foundry/Godot core startup, Foundry Script build pipeline, doctest, `TemporaryProjectTree`,
command-first Foundry CLI, SCons/Ninja through `scripts/agent_build.py`.

---

## File Structure

- Create `main/global_class_scan_policy.h`: declaration of the pure startup eligibility decision.
- Create `main/global_class_scan_policy.cpp`: five-condition implementation with no cache-file inspection.
- Modify `main/main.cpp`: use the policy, preserve `ran_any_task`, reorder runtime and engine-test startup, and remove
  entry-point-specific scans.
- Create `modules/foundry_script/tests/test_global_class_startup.h`: policy matrix plus bounded subprocess projects for
  game, main-loop, build-provider, generated-class, editor-child, persistence, and `test run --project` behavior.
- Do not edit generated `modules/modules_tests.gen.h`; SCons discovers every module test header automatically.

## Task 1: Lock the Eligibility Policy

**Files:**

- Create: `main/global_class_scan_policy.h`
- Create: `main/global_class_scan_policy.cpp`
- Create: `modules/foundry_script/tests/test_global_class_startup.h`

- [ ] **Step 1: Write the failing policy decision table**

Create the test header with the standard license header, `#pragma once`, and these includes and cases:

```cpp
#include "main/global_class_scan_policy.h"
#include "tests/test_macros.h"

namespace FSTests {

TEST_SUITE("[Modules][FoundryScript][GlobalClassStartup]") {
TEST_CASE("GlobalClassStartup policy scans only standalone source consumers") {
	CHECK(GlobalClassScanPolicy::should_scan(true, false, false, false, true));
	CHECK_FALSE(GlobalClassScanPolicy::should_scan(false, false, false, false, true));
	CHECK_FALSE(GlobalClassScanPolicy::should_scan(true, true, false, false, true));
	CHECK_FALSE(GlobalClassScanPolicy::should_scan(true, false, true, false, true));
	CHECK_FALSE(GlobalClassScanPolicy::should_scan(true, false, false, true, true));
	CHECK_FALSE(GlobalClassScanPolicy::should_scan(true, false, false, false, false));
}
} // TEST_SUITE

} // namespace FSTests
```

- [ ] **Step 2: Build to verify the policy test fails**

Run:

```sh
python3 scripts/agent_build.py --backend ninja --case "*GlobalClassStartup policy*"
```

Expected: compilation fails because `main/global_class_scan_policy.h` does not exist.

- [ ] **Step 3: Add the minimal pure policy**

Create `main/global_class_scan_policy.h`:

```cpp
#pragma once

class GlobalClassScanPolicy {
public:
	static bool should_scan(bool p_project_loaded, bool p_using_datapack, bool p_editor_mode,
			bool p_editor_child, bool p_consumes_project_scripts);
};
```

Create `main/global_class_scan_policy.cpp` with the standard engine license header:

```cpp
#include "global_class_scan_policy.h"

bool GlobalClassScanPolicy::should_scan(bool p_project_loaded, bool p_using_datapack, bool p_editor_mode,
		bool p_editor_child, bool p_consumes_project_scripts) {
	return p_project_loaded && !p_using_datapack && !p_editor_mode && !p_editor_child &&
			p_consumes_project_scripts;
}
```

The policy deliberately has no headless or cache-exists input.

- [ ] **Step 4: Run the focused policy test**

Run:

```sh
python3 scripts/agent_build.py --backend ninja --case "*GlobalClassStartup policy*"
```

Expected: one selected case passes and doctest reports success.

- [ ] **Step 5: Commit the policy**

```sh
git add main/global_class_scan_policy.h main/global_class_scan_policy.cpp \
  modules/foundry_script/tests/test_global_class_startup.h
git commit -m "Define standalone global class scan policy"
```

## Task 2: Scan Before Runtime Build and Loading

**Files:**

- Modify: `main/main.cpp:31-90,373-386,4698-4775`
- Modify: `modules/foundry_script/tests/test_global_class_startup.h`

- [ ] **Step 1: Add bounded process-test helpers**

Add these includes to `test_global_class_startup.h`:

```cpp
#include "fs_test_python.h"
#include "fs_temporary_project_tree.h"

#include "core/io/config_file.h"
#include "core/io/file_access.h"
#include "core/os/os.h"

#include <cstring>
```

Inside `namespace FSTests`, before the suite, add a subprocess result and a bounded runner. This is a focused copy of
the existing pipe-pumping pattern in `fs_name_mangler_export_test_utils.h`, with a shorter runtime timeout:

```cpp
struct GlobalClassStartupProcessResult {
	Error error = FAILED;
	int exit_code = -1;
	String output;
};

static GlobalClassStartupProcessResult run_global_class_startup_process(
		const List<String> &p_arguments, const String &p_working_directory = String()) {
	GlobalClassStartupProcessResult result;
	Vector<uint8_t> stdout_bytes;
	Vector<uint8_t> stderr_bytes;
	Dictionary pipe_info = OS::get_singleton()->execute_with_pipe(
			OS::get_singleton()->get_executable_path(), p_arguments, false,
			p_working_directory, Dictionary(), false);
	if (pipe_info.is_empty()) {
		return result;
	}

	Ref<FileAccess> stdout_pipe = pipe_info["stdio"];
	Ref<FileAccess> stderr_pipe = pipe_info["stderr"];
	const OS::ProcessID pid = pipe_info["pid"];
	auto pump = [](const Ref<FileAccess> &p_pipe, Vector<uint8_t> &r_bytes) {
		if (p_pipe.is_null() || !p_pipe->is_open()) {
			return;
		}
		const uint64_t available = p_pipe->get_length();
		if (available == 0) {
			return;
		}
		Vector<uint8_t> chunk;
		chunk.resize(available);
		const uint64_t read = p_pipe->get_buffer(chunk.ptrw(), available);
		const int old_size = r_bytes.size();
		r_bytes.resize(old_size + read);
		if (read > 0) {
			memcpy(r_bytes.ptrw() + old_size, chunk.ptr(), read);
		}
	};

	const uint64_t deadline = OS::get_singleton()->get_ticks_msec() + 30000;
	while (OS::get_singleton()->get_ticks_msec() < deadline) {
		pump(stdout_pipe, stdout_bytes);
		pump(stderr_pipe, stderr_bytes);
		if (!OS::get_singleton()->is_process_running(pid)) {
			pump(stdout_pipe, stdout_bytes);
			pump(stderr_pipe, stderr_bytes);
			result.error = OK;
			break;
		}
		OS::get_singleton()->delay_usec(20000);
	}
	if (result.error != OK && OS::get_singleton()->is_process_running(pid)) {
		OS::get_singleton()->kill(pid);
	}
	if (stdout_pipe.is_valid()) {
		stdout_pipe->close();
	}
	if (stderr_pipe.is_valid()) {
		stderr_pipe->close();
	}
	result.exit_code = OS::get_singleton()->get_process_exit_code(pid);
	result.output = String::utf8((const char *)stdout_bytes.ptr(), stdout_bytes.size());
	result.output += String::utf8((const char *)stderr_bytes.ptr(), stderr_bytes.size());
	return result;
}

static List<String> global_class_project_run_args(const String &p_project, bool p_verbose = false) {
	List<String> args;
	args.push_back("--headless");
	args.push_back("--no-header");
	if (p_verbose) {
		args.push_back("--verbose");
	}
	args.push_back("project");
	args.push_back("run");
	args.push_back("--project");
	args.push_back(p_project);
	args.push_back("--quit-after");
	args.push_back("60");
	return args;
}

static int count_startup_scan_summaries(const String &p_output) {
	return p_output.count("ScriptServer: Scanned ");
}
```

- [ ] **Step 2: Add failing cacheless, stale-cache, main-loop, and pre-build-provider cases**

Inside the suite, add four cases. Each uses a unique `TemporaryProjectTree`, requires `tree.is_valid()`, and writes a
real `project.foundry` plus scripts.

The cacheless and stale-cache scene projects share these runtime files:

```text
# main.tscn
[gd_scene load_steps=2 format=3]

[ext_resource path="res://main.fs" type="Script" id="1"]

[node name="Main" type="Node"]
script = ExtResource("1")

# main.fs
extends Node

func _ready() -> void:
	var dependency := StartupCachelessDependency.new()
	print("GLOBAL_CLASS_STARTUP_SCENE_OK:" + dependency.value())
	get_tree().quit()

# dependency.fs
class_name StartupCachelessDependency
extends RefCounted

func value() -> String:
	return "resolved"
```

Write `project.foundry` with `run/main_scene="res://main.tscn"`. For the stale case, create
`.foundry/global_script_class_cache.cfg` through `ConfigFile` with one dictionary for
`StartupCachelessDependency` whose path is `res://deleted_dependency.fs`; save its bytes before launch. Assert both
runs contain `GLOBAL_CLASS_STARTUP_SCENE_OK:resolved`. Afterward, assert the cacheless project still has no
`.foundry/global_script_class_cache.cfg` and the stale cache bytes are unchanged.

For the main-loop case, use an otherwise empty main scene and:

```text
# project.foundry application keys
run/main_scene="res://empty.tscn"
run/main_loop_type="StartupCachelessMainLoop"

# startup_loop.fs
class_name StartupCachelessMainLoop
extends SceneTree

func _initialize() -> void:
	print("GLOBAL_CLASS_STARTUP_MAIN_LOOP_OK")
	quit()
```

Assert the marker appears.

For pre-build ordering, write `build/helper.fs` and `build/provider.fs`:

```text
# helper.fs
namespace startup.provider
class_name StartupProviderHelper extends RefCounted

static func value() -> String:
	return "provider-ok"

# provider.fs
import startup.provider
class_name StartupBuildProvider extends FoundryBuildTask

func run(context: FoundryBuildContext) -> FoundryBuildResult:
	var file := FileAccess.open("res://generated/provider.marker", FileAccess.WRITE)
	file.store_string(StartupProviderHelper.value())
	var result := FoundryBuildResult.new()
	result.success = true
	result.fingerprint = StartupProviderHelper.value()
	return result
```

Configure a trusted `pre_compile` task using that provider, launch with global `--trusted`, and assert
`generated/provider.marker` contains `provider-ok`. Keep the helper and provider under the same `res://build/` root so
the existing bootstrap restriction permits the dependency.

- [ ] **Step 3: Add direct script, project-test, and project-backed eval regression cases**

Use three independent cacheless `TemporaryProjectTree` projects. Each declares this dependency and references it from
the selected entry point:

```text
# dependency.fs
class_name StartupDirectEntryDependency
extends RefCounted

func value() -> String:
	return "resolved"
```

For `project run --script`, write `entry.fs` extending `SceneTree`; `_initialize()` prints
`GLOBAL_CLASS_STARTUP_SCRIPT_OK:resolved` and quits. Launch `project run --project <root> --script res://entry.fs`.

For direct project test, write a `runner.fs` extending `ScriptRunner`; `run(args: PackedStringArray) -> int` prints
`GLOBAL_CLASS_STARTUP_PROJECT_TEST_OK:resolved` and returns zero. Launch
`project test --project <root> --runner res://runner.fs`.

For project-backed eval, launch
`script eval --project <root> 'print("GLOBAL_CLASS_STARTUP_EVAL_OK:" + StartupDirectEntryDependency.new().value())'`.

Pass global `--verbose` in all three commands. Assert each marker, exit code zero, and exactly one
`ScriptServer: Scanned ` summary. These are regression cases for the previously mode-specific scans: they may already
pass before the refactor, but they must continue to pass through the centralized branch after those calls are removed.

- [ ] **Step 4: Run the new process tests to verify the newly exposed paths fail**

```sh
python3 scripts/agent_build.py --backend ninja --case "*GlobalClassStartup*"
```

Expected before the runtime change: the policy and direct script/project-test/eval regression cases pass, while the
cacheless scene, stale-cache scene, cacheless main loop, and provider-import cases fail their success-marker/artifact
assertions.

- [ ] **Step 5: Add the initial centralized runtime scan**

Include the new policy in `main/main.cpp`:

```cpp
#include "main/global_class_scan_policy.h"
```

Immediately after the early-return migration block and before `foundry_runtime_build_stages_enabled`, compute the
consumer and eligibility once:

```cpp
bool runtime_consumes_project_scripts = !game_path.is_empty() || !script.is_empty() ||
		!test_runner_path.is_empty();
#ifdef MODULE_FOUNDRY_SCRIPT_ENABLED
runtime_consumes_project_scripts = runtime_consumes_project_scripts ||
		(eval_requested && ProjectSettings::get_singleton()->is_project_loaded());
#endif
const bool scan_runtime_global_classes = GlobalClassScanPolicy::should_scan(
		ProjectSettings::get_singleton()->is_project_loaded(),
		ProjectSettings::get_singleton()->is_using_datapack(), editor, editor_pid != 0,
		runtime_consumes_project_scripts);
if (scan_runtime_global_classes) {
	ScriptServer::scan_global_classes();
}
```

Leave this before the call to `run_foundry_build_stage_for_cli(STAGE_PRE_COMPILE, ...)`. Delete the three later scan
blocks in the project-test, eval, and script branches; those entry points now use the centralized pass.

- [ ] **Step 6: Add the editor-child zero-scan guard**

Create a project with a valid cache entry for its global dependency, launch with `--verbose --editor-pid` and the
current process ID, and assert the runtime marker appears while `count_startup_scan_summaries(output) == 0`.

- [ ] **Step 7: Run the focused startup suite**

```sh
python3 scripts/agent_build.py --backend ninja --case "*GlobalClassStartup*"
```

Expected: all Task 1-2 cases pass. The editor-child case reports no scan summary; ordinary direct runs report one.

- [ ] **Step 8: Commit initial runtime ordering**

```sh
git add main/main.cpp modules/foundry_script/tests/test_global_class_startup.h
git commit -m "Scan global classes before standalone runtime build"
```

## Task 3: Reconcile Successful Pre-Compile Generation

**Files:**

- Modify: `main/main.cpp:373-386,4707-4720`
- Modify: `modules/foundry_script/tests/test_global_class_startup.h`

- [ ] **Step 1: Add the failing generated-class two-pass test**

Create a scratch project whose checked-in `main.fs` constructs `StartupGeneratedDependency`, but where that class file
does not exist initially. Configure a trusted command-provider pre-compile task:

```ini
[build]
enabled=true
pre_compile=PackedStringArray("generate_dependency")

[build/tasks/generate_dependency]
provider="command"
command="python3"
args=PackedStringArray("res://tools/generate_dependency.py", "res://generated/dependency.fs")
outputs=PackedStringArray("res://generated/dependency.fs")
working_directory="res://"
timeout_seconds=5
```

Generate the command value with `fs_test_python_command()` rather than hard-coding `python3`. The Python script writes:

```text
class_name StartupGeneratedDependency
extends RefCounted

func value() -> String:
	return "generated"
```

Launch the project twice with `--trusted --verbose`. Assert both runs print
`GLOBAL_CLASS_STARTUP_GENERATED_OK:generated`; the first output contains two scan summaries and the clean second output
contains one.

- [ ] **Step 2: Run the generated-class test to verify it fails**

```sh
python3 scripts/agent_build.py --backend ninja --case "*GlobalClassStartup generated*"
```

Expected: the build task creates the file, but the same run cannot resolve the generated class and emits only one scan
summary.

- [ ] **Step 3: Preserve `ran_any_task` in the CLI wrapper**

Change the helper signature and body in `main/main.cpp`:

```cpp
static bool run_foundry_build_stage_for_cli(ProjectBuildPipelineConfig::Stage p_stage,
		const String &p_context, bool *r_ran_any_task = nullptr) {
	const FoundryBuildPipelineRunner::StageRunResult result =
			FoundryBuildPipelineRunner::run_stage(p_stage);
	if (r_ran_any_task != nullptr) {
		*r_ran_any_task = result.ran_any_task;
	}
	if (result.is_success()) {
		return true;
	}

	FoundryBuildPipelineRunner::print_diagnostics(result.snapshot, p_context);
	if (result.error != OK) {
		ERR_PRINT(vformat("%s returned error %d.", p_context, int(result.error)));
	}
	return false;
}
```

- [ ] **Step 4: Add the conditional post-build scan**

Replace the runtime pre-compile call with:

```cpp
bool pre_compile_ran_any_task = false;
if (foundry_runtime_build_stages_enabled) {
	if (!run_foundry_build_stage_for_cli(ProjectBuildPipelineConfig::STAGE_PRE_COMPILE,
				"Foundry pre_compile runtime stage", &pre_compile_ran_any_task)) {
		return EXIT_FAILURE;
	}
	if (scan_runtime_global_classes && pre_compile_ran_any_task) {
		ScriptServer::scan_global_classes();
	}

	ResourceLoader::add_custom_loaders();
	ResourceSaver::add_custom_savers();
	custom_resource_handlers_registered = true;
}
```

- [ ] **Step 5: Run the generated and full focused suites**

```sh
python3 scripts/agent_build.py --backend ninja --case "*GlobalClassStartup generated*"
python3 scripts/agent_build.py --backend ninja --case "*GlobalClassStartup*"
```

Expected: first generated run has two summaries, clean rerun has one, and every startup case passes.

- [ ] **Step 6: Commit two-phase scanning**

```sh
git add main/main.cpp modules/foundry_script/tests/test_global_class_startup.h
git commit -m "Rescan global classes after generated sources"
```

## Task 4: Apply the Policy to `test run --project`

**Files:**

- Modify: `main/main.cpp:950-990`
- Modify: `modules/foundry_script/tests/test_global_class_startup.h`

- [ ] **Step 1: Add a filtered child probe and failing parent process test**

Add a child case that is inert outside its special scratch project:

```cpp
TEST_CASE("GlobalClassStartup child project probe") {
	ProjectSettings *settings = ProjectSettings::get_singleton();
	if (settings == nullptr || !settings->get_setting("global_class_startup/child_probe", false)) {
		return;
	}
	CHECK(ScriptServer::is_global_class("startup.test.StartupEngineTestDependency"));
	CHECK_EQ(FileAccess::get_file_as_string("res://generated/provider.marker"), "provider-ok");
}
```

The parent case creates a project with `global_class_startup/child_probe=true`, a namespaced global class, and the same
provider-local import pattern from Task 2. It launches:

```text
foundry --headless --no-header --trusted test run --project <scratch> \
  --case "GlobalClassStartup child project probe"
```

Assert the child output contains `[doctest] Status: SUCCESS!` and the provider marker exists. The exact case filter
prevents the parent subprocess test from recursing.

- [ ] **Step 2: Run the parent test to verify it fails before startup changes**

```sh
python3 scripts/agent_build.py --backend ninja --case "*GlobalClassStartup test run project*"
```

Expected: the child pre-compile provider cannot resolve its local namespace import, or the child probe cannot see the
project global class.

- [ ] **Step 3: Add two-phase scanning to `Main::test_entrypoint()`**

After successful `ProjectSettings::setup()` and user-data directory setup, before setting build trust, add:

```cpp
const bool scan_test_project_global_classes = GlobalClassScanPolicy::should_scan(
		ProjectSettings::get_singleton()->is_project_loaded(),
		ProjectSettings::get_singleton()->is_using_datapack(), false, false, true);
if (scan_test_project_global_classes) {
	ScriptServer::scan_global_classes();
}
```

Preserve and use the build-stage execution result:

```cpp
bool pre_compile_ran_any_task = false;
ProjectBuildTrustStore::set_cli_trusted_execution(cli_parse.trusted);
const bool pre_compile_ok = run_foundry_build_stage_for_cli(
		ProjectBuildPipelineConfig::STAGE_PRE_COMPILE,
		"Foundry pre_compile test stage", &pre_compile_ran_any_task);
ProjectBuildTrustStore::set_cli_trusted_execution(old_foundry_build_trusted);
if (!pre_compile_ok) {
	test_cleanup();
	return EXIT_FAILURE;
}
if (scan_test_project_global_classes && pre_compile_ran_any_task) {
	ScriptServer::scan_global_classes();
}
```

Leave custom loader registration and later post-compile handling in their current positions.

- [ ] **Step 4: Run the child and complete focused suites**

```sh
python3 scripts/agent_build.py --backend ninja --case "*GlobalClassStartup test run project*"
python3 scripts/agent_build.py --backend ninja --case "*GlobalClassStartup*"
```

Expected: the filtered child reports doctest success and every global-class startup case passes.

- [ ] **Step 5: Commit engine-test startup coverage**

```sh
git add main/main.cpp modules/foundry_script/tests/test_global_class_startup.h
git commit -m "Scan project globals before engine test build"
```

## Task 5: Verify Scope, Formatting, and Full Validation

**Files:**

- Modify only if verification exposes an issue:
  `main/global_class_scan_policy.*`, `main/main.cpp`,
  `modules/foundry_script/tests/test_global_class_startup.h`

- [ ] **Step 1: Run formatting and diff hygiene checks**

```sh
git diff --check
pre-commit run clang-format --files \
  main/global_class_scan_policy.h main/global_class_scan_policy.cpp main/main.cpp \
  modules/foundry_script/tests/test_global_class_startup.h
git diff --check
```

Expected: no whitespace errors; formatting changes are limited to the files in scope.

- [ ] **Step 2: Run focused startup and existing scan coverage**

```sh
python3 scripts/agent_build.py --backend ninja --case "*GlobalClassStartup*"
./bin/foundry.* --headless test run \
  --case "*Filesystem scan*" \
  --suite "*[Modules][FoundryScript][BuildTaskBootstrap]*" \
  --force-colors
```

Expected: all selected tests pass, including outside-root bootstrap rejection controls.

- [ ] **Step 3: Run the native strict build and full suite**

```sh
python3 scripts/agent_build.py --test
```

Expected: the wrapper completes the native strict build and the test output contains `[doctest] Status: SUCCESS!`.
On Linux, the wrapper supplies `DISPLAY=:1` so GUI-dependent subprocess coverage does not silently skip.

- [ ] **Step 4: Confirm no cache or scratch artifacts entered the diff**

```sh
git status --short
git log --stat --oneline --max-count=5
```

Expected: no `.foundry` cache, build state, generated scratch project, or tracked fixture output is included. Preserve
pre-existing unrelated worktree changes.

- [ ] **Step 5: Commit any verification-only correction**

If Step 1 formatting changed an in-scope file, commit it:

```sh
git add main/global_class_scan_policy.h main/global_class_scan_policy.cpp main/main.cpp \
  modules/foundry_script/tests/test_global_class_startup.h
git commit -m "Polish standalone global class startup coverage"
```

If formatting made no change, do not create an empty commit.
