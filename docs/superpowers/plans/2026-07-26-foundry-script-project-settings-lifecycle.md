# Foundry Script ProjectSettings Lifecycle Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Restore the exact pre-test ProjectSettings state when direct/empty-suite and named/hoisted
Foundry Script test lifecycles end.

**Architecture:** Let `init_language()` remain the sole owner of language-cycle ProjectSettings
capture and project activation. Represent doctest suite ownership with a boolean independent of the
suite-name string so an empty suite can own the hoisted language and trigger teardown on the next
named-suite boundary.

**Tech Stack:** C++17, doctest listeners and cases, Foundry Script test harness, SCons strict
optimized builds, Foundry command-first test CLI.

---

## File Structure

- Modify `modules/foundry_script/tests/test_script_runner.h`: add the direct-case lifecycle regression
  and remove the pre-`init_language()` project switch.
- Modify `modules/foundry_script/tests/test_suite_language_fixture.h`: represent empty-suite ownership explicitly.
- Modify `modules/foundry_script/tests/test_suite_language_hoist.h`: add callback-level empty-suite
  transition coverage and strengthen named teardown coverage.
- Keep `modules/foundry_script/tests/fs_test_runner.cpp` behavior unchanged: it already records and
  restores the correct three fields when called before an external project switch.

### Task 1: Add direct ScriptRunner lifecycle regression

**Files:**
- Modify: `modules/foundry_script/tests/test_script_runner.h`
- Test: `modules/foundry_script/tests/test_script_runner.h`

- [ ] **Step 1: Write the failing direct-case regression**

Add a direct `TEST_CASE` after `ScriptRunnerProjectFixture`:

```cpp
TEST_CASE("[Modules][FoundryScript][ScriptRunner][Lifecycle] Direct fixture teardown restores exact project settings") {
	finish_language();
	TestProjectSettingsRestoreScope restore_process_settings;

	ProjectSettings *settings = ProjectSettings::get_singleton();
	const String expected_resource_path;
	const bool expected_project_loaded = false;
	const String expected_app_name = "ScriptRunnerBeforeDirectCase";
	TestProjectSettingsInternalsAccessor::resource_path() = expected_resource_path;
	TestProjectSettingsInternalsAccessor::project_loaded() = expected_project_loaded;
	settings->set_setting("application/config/name", expected_app_name);

	{
		ScriptRunnerProjectFixture project;
		CHECK(is_fs_language_active());
		CHECK_NE(settings->get_resource_path(), expected_resource_path);
	}

	CHECK_EQ(settings->get_resource_path(), expected_resource_path);
	CHECK_EQ(settings->is_project_loaded(), expected_project_loaded);
	CHECK_EQ(String(GLOBAL_GET("application/config/name")), expected_app_name);

	finish_language();
	CHECK_FALSE(is_fs_language_active());
	CHECK_EQ(settings->get_resource_path(), expected_resource_path);
	CHECK_EQ(settings->is_project_loaded(), expected_project_loaded);
	CHECK_EQ(String(GLOBAL_GET("application/config/name")), expected_app_name);
}
```

Use an empty path deliberately: it is the canonical projectless pre-test state, and
`OS::get_resource_dir()` mirrors any nonempty `ProjectSettings::resource_path`, causing
`ProjectSettings::setup()` to reuse that root instead of resolving the fixture path.

- [ ] **Step 2: Build the unchanged branch with the new test**

Run:

```sh
python3 scripts/agent_build.py
```

Expected: build succeeds so the test can execute against the current lifecycle.

- [ ] **Step 3: Run the direct regression to verify RED**

Run:

```sh
./bin/foundry.* --headless test run \
  --case "*Direct fixture teardown restores exact project settings*" \
  --force-colors
```

Expected: FAIL after `finish_language()` because at least the resource path is restored to
`modules/foundry_script/tests/scripts` instead of `expected_resource_path`.

### Task 2: Add empty-suite listener and teardown-cleanliness regressions

**Files:**
- Modify: `modules/foundry_script/tests/test_suite_language_hoist.h`
- Test: `modules/foundry_script/tests/test_suite_language_hoist.h`

- [ ] **Step 1: Add explicit dependencies**

Add:

```cpp
#include "core/io/resource.h"
#include "modules/foundry_script/fs_cache.h"
#include "tests/core/config/test_project_settings.h"

#include "test_suite_language_fixture.h"
```

- [ ] **Step 2: Add a callback-level empty-suite ownership test**

Add this case to the `SuiteLanguageHoist` suite:

```cpp
TEST_CASE("Empty-suite ownership survives repeated cases and tears down before a named suite") {
	finish_language();
	TestProjectSettingsRestoreScope restore_process_settings;

	ProjectSettings *settings = ProjectSettings::get_singleton();
	const String expected_resource_path;
	const bool expected_project_loaded = false;
	const String expected_app_name = "BeforeEmptySuite";
	TestProjectSettingsInternalsAccessor::resource_path() = expected_resource_path;
	TestProjectSettingsInternalsAccessor::project_loaded() = expected_project_loaded;
	settings->set_setting("application/config/name", expected_app_name);

	doctest::ContextOptions context_options{};
	FSLanguageSuiteFixture listener(context_options);
	doctest::CurrentTestCaseStats stats{};
	doctest::TestCaseData empty_case{};
	empty_case.m_name = "direct empty-suite case";
	empty_case.m_test_suite = "";
	doctest::TestCaseData named_case{};
	named_case.m_name = "following named-suite case";
	named_case.m_test_suite = "[Modules][FoundryScript][FollowingSuite]";

	listener.test_case_start(empty_case);
	init_language(String(root));
	listener.test_case_end(stats);
	CHECK(is_language_initialized());

	const uint64_t count_before_repeat = get_init_language_count();
	listener.test_case_start(empty_case);
	CHECK(is_language_initialized());
	CHECK_EQ(get_init_language_count(), count_before_repeat);
	listener.test_case_end(stats);

	Error cache_error = OK;
	Ref<FoundryScript> cached_script = FSCache::get_full_script(
			"res://test_runner_host/sync_exit_0.notest.fs", cache_error);
	REQUIRE_EQ(cache_error, OK);
	REQUIRE(cached_script.is_valid());
	const String script_path = cached_script->get_path();
	CHECK(FSCache::get_cached_script(script_path).is_valid());
	CHECK(ResourceCache::has(script_path));
	cached_script.unref();

	listener.test_case_start(named_case);
	CHECK_FALSE(is_fs_language_active());
	CHECK_FALSE(is_language_initialized());
	CHECK_EQ(settings->get_resource_path(), expected_resource_path);
	CHECK_EQ(settings->is_project_loaded(), expected_project_loaded);
	CHECK_EQ(String(GLOBAL_GET("application/config/name")), expected_app_name);
	CHECK(FSCache::get_cached_script(script_path).is_null());
	CHECK_FALSE(ResourceCache::has(script_path));
	CHECK(FSLanguage::get_singleton()->get_reflection_singleton().is_null());
	CHECK(FSLanguage::get_singleton()->get_project_scripts_singleton().is_null());
	CHECK(FSLanguage::get_singleton()->get_namespace_singleton().is_null());
}
```

The empty path has the same projectless meaning here and allows `init_language(String(root))` to
activate the staged fixture project before the listener restores the exact empty state.

- [ ] **Step 3: Strengthen named direct-init teardown restoration**

Replace the current `finish_language tears down direct init() state` body with:

```cpp
TEST_CASE("finish_language restores exact project settings after named-suite initialization") {
	finish_language();
	TestProjectSettingsRestoreScope restore_process_settings;

	ProjectSettings *settings = ProjectSettings::get_singleton();
	const String expected_resource_path;
	const bool expected_project_loaded = false;
	const String expected_app_name = "BeforeNamedSuite";
	TestProjectSettingsInternalsAccessor::resource_path() = expected_resource_path;
	TestProjectSettingsInternalsAccessor::project_loaded() = expected_project_loaded;
	settings->set_setting("application/config/name", expected_app_name);

	init_language(String(root));
	CHECK(is_fs_language_active());
	finish_language();

	CHECK_FALSE(is_fs_language_active());
	CHECK_FALSE(is_language_initialized());
	CHECK_EQ(settings->get_resource_path(), expected_resource_path);
	CHECK_EQ(settings->is_project_loaded(), expected_project_loaded);
	CHECK_EQ(String(GLOBAL_GET("application/config/name")), expected_app_name);
}
```

- [ ] **Step 4: Rebuild the test translation unit**

Run:

```sh
python3 scripts/agent_build.py
```

Expected: build succeeds.

- [ ] **Step 5: Run the listener regressions to verify RED**

Run:

```sh
./bin/foundry.* --headless test run \
  --case "*Empty-suite ownership survives repeated cases*" \
  --force-colors
```

Expected: FAIL because the current listener leaves `owner_suite` empty and does not finish the
language when the named case starts.

Run:

```sh
./bin/foundry.* --headless test run \
  --case "*finish_language restores exact project settings*" \
  --force-colors
```

Expected: PASS. This is a characterization/coverage strengthening case; RED is supplied by the
direct fixture and empty-suite transition regressions.

- [ ] **Step 6: Commit the RED tests**

```sh
git add modules/foundry_script/tests/test_script_runner.h \
  modules/foundry_script/tests/test_suite_language_hoist.h
git commit -m "test: cover Foundry Script settings lifecycle"
```

### Task 3: Correct project activation and empty-suite ownership

**Files:**
- Modify: `modules/foundry_script/tests/test_script_runner.h`
- Modify: `modules/foundry_script/tests/test_suite_language_fixture.h`

- [ ] **Step 1: Delegate ScriptRunner setup to `init_language()`**

Replace the fixture constructor with:

```cpp
explicit ScriptRunnerProjectFixture() {
	const String scripts_path = String(test_runner_scripts_root);
	init_language(scripts_path);
	REQUIRE_MESSAGE(is_fs_language_active(), "Failed to initialize the test runner project.");
}
```

This ensures `init_language()` snapshots the state already captured by the fixture's restore scope
before activating the scripts project.

- [ ] **Step 2: Add explicit listener ownership state**

Add beside `owner_suite`:

```cpp
// Whether a suite currently owns the live language. Kept separately because
// direct TEST_CASE declarations legitimately have an empty suite name.
bool has_owner_suite = false;
```

Change the start transition:

```cpp
if (is_fs_language_active() && has_owner_suite && owner_suite != current_suite) {
	finish_language();
	owner_suite = String();
	has_owner_suite = false;
}
```

Change first ownership assignment:

```cpp
if (!has_owner_suite) {
	owner_suite = current_suite;
	has_owner_suite = true;
}
```

Change run-end reset:

```cpp
owner_suite = String();
has_owner_suite = false;
```

- [ ] **Step 3: Rebuild**

Run:

```sh
python3 scripts/agent_build.py
```

Expected: exit 0 under strict `dev_mode=yes` warnings-as-errors.

- [ ] **Step 4: Run both formerly RED tests for GREEN**

Run:

```sh
./bin/foundry.* --headless test run \
  --case "*Direct fixture teardown restores exact project settings*" \
  --force-colors
./bin/foundry.* --headless test run \
  --case "*Empty-suite ownership survives repeated cases*" \
  --force-colors
```

Expected: both commands report doctest `Status: SUCCESS!`.

- [ ] **Step 5: Run the focused lifecycle group**

Run:

```sh
./bin/foundry.* --headless test run --case "*ScriptRunner*" --force-colors
./bin/foundry.* --headless test run --case "*SuiteLanguageHoist*" --force-colors
```

Expected: all matching cases pass with no cache, resource, singleton, or ObjectDB leak failures.

- [ ] **Step 6: Commit the implementation**

```sh
git add modules/foundry_script/tests/test_script_runner.h \
  modules/foundry_script/tests/test_suite_language_fixture.h
git commit -m "Restore Foundry Script test project settings"
```

### Task 4: Verify the lifecycle broadly

**Files:**
- Verify: all branch changes

- [ ] **Step 1: Run broader Foundry Script and downstream manifest cases**

Run:

```sh
./bin/foundry.* --headless test run --case "*FoundryScript*" --force-colors
./bin/foundry.* --headless test run --case "*ExportManifest*" --force-colors
```

Expected: all filtered cases pass.

- [ ] **Step 2: Run a fresh strict optimized build**

Run:

```sh
python3 scripts/agent_build.py
```

Expected: exit 0 with `dev_mode=yes`, optimized test target, and warnings as errors.

- [ ] **Step 3: Run the exact full suite with structured progress**

Run:

```sh
DISPLAY=:1 ./bin/foundry.* --headless test run \
  --progress-format=jsonl \
  --progress-file /tmp/issue-1234-full-progress.jsonl \
  --progress-heartbeat-seconds 30 \
  --force-colors
```

Expected: JSONL `run_end` reports zero failed cases and doctest reports `Status: SUCCESS!`. Treat
documented cleanup-only exit behavior according to `AGENTS.md`.

- [ ] **Step 4: Verify scratch and branch hygiene**

Run:

```sh
git diff --check origin/develop...HEAD
git status --short --untracked-files=all
```

Expected: no whitespace errors and no generated artifacts or uncommitted changes.

### Task 5: Converge review and integrate

**Files:**
- Review: `origin/develop...HEAD`

- [ ] **Step 1: Run a fresh read-only Cursor review**

Fetch `origin/develop` and run the `cursor-review` skill's foreground command with:

```sh
CURSOR_REVIEW_BASE=origin/develop
CURSOR_REVIEW_WORKSPACE=/Users/christian/CafecitoGames/Foundry/.worktrees/issue-1234
```

Expected: valid structured output. Triage every finding with
`superpowers:receiving-code-review`; use systematic debugging and TDD for real defects, commit
verified fixes, and repeat on the new HEAD until `RESULT: clean`.

- [ ] **Step 2: Push and open the PR**

```sh
git push -u origin issue-1234
gh pr create --repo cafecito-games/Foundry --base develop \
  --head issue-1234 \
  --title "Restore Foundry Script test project settings" \
  --body-file /tmp/issue-1234-pr-body.md
gh pr merge --repo cafecito-games/Foundry --squash --auto <pr-number>
```

The PR body must summarize the lifecycle fix and tests and end with `Closes #1234`.

- [ ] **Step 3: Monitor required checks through merge**

Use `gh pr checks --watch` and `gh pr view` until all required checks pass and the PR state is
`MERGED`. Diagnose any failure with the repository's GitHub Actions workflow before updating the
branch.

- [ ] **Step 4: Verify issue/project completion and clean up**

Confirm issue #1234 is closed and its Experiment project item is `Done`. Then remove
`.worktrees/issue-1234`, delete local `issue-1234`, and delete the remote branch if GitHub did not
remove it automatically. Verify `git worktree list`, local branch refs, and remote branch refs no
longer contain `issue-1234`.
