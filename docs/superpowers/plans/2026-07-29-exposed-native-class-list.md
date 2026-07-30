# Exposed Native Class List Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan
> task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Expose the analyzer-visible native class-name set as
`ClassDB.get_exposed_class_list()` with deterministic ordering.

**Architecture:** Native `ClassDB` collects and sorts exposed registrations under one read lock.
The script singleton converts that native list to `PackedStringArray`, and the extension API dump
uses the same helper so both public surfaces share the filter.

**Tech Stack:** C++17, Godot/Foundry ClassDB bindings, doctest, Foundry Script CLI evaluation,
class-reference XML, SCons.

---

### Task 1: Establish the worktree baseline

**Files:**
- Verify: existing checkout and test binary

- [ ] **Step 1: Confirm branch isolation and ignored local configuration**

Run:

```sh
git status --short --branch
git check-ignore -v custom.py
git merge-base --is-ancestor origin/develop HEAD
```

Expected: branch `issue-1341`, only the committed design and plan differ from `origin/develop`,
`custom.py` is ignored, and the merge-base check exits 0.

- [ ] **Step 2: Build the unchanged baseline**

Run:

```sh
scons platform=macos target=editor dev_mode=yes dev_build=yes tests=yes
```

Expected: exit 0 with no warnings promoted to errors.

- [ ] **Step 3: Run the relevant existing baseline tests**

Run:

```sh
./bin/foundry.macos.editor.dev.* --headless test run --case "*ClassDB*" --force-colors
./bin/foundry.macos.editor.dev.* --headless test run --case "*FoundryCLI*ScriptEval*" --force-colors
```

Expected: doctest reports success for both filters.

### Task 2: Add failing public-contract tests

**Files:**
- Modify: `tests/core/object/test_class_db.h`
- Modify: `tests/core/os/test_foundry_cli_project_test.h`

- [ ] **Step 1: Add the ClassDB contract test**

Add this test inside `TEST_SUITE("[ClassDB]")`:

```cpp
TEST_CASE("[ClassDB] Exposed class list matches the script-visible native class set") {
	Special::ClassDB script_class_db;
	const Variant result = script_class_db.call("get_exposed_class_list");
	REQUIRE_EQ(result.get_type(), Variant::PACKED_STRING_ARRAY);

	const PackedStringArray exposed_classes = result;
	LocalVector<StringName> all_classes;
	ClassDB::get_class_list(all_classes);

	LocalVector<StringName> expected_classes;
	for (const StringName &class_name : all_classes) {
		if (ClassDB::is_class_exposed(class_name)) {
			expected_classes.push_back(class_name);
		}
	}

	REQUIRE_EQ(exposed_classes.size(), int64_t(expected_classes.size()));
	for (uint32_t i = 0; i < expected_classes.size(); i++) {
		CHECK_EQ(exposed_classes[i], String(expected_classes[i]));
	}
	CHECK(exposed_classes.find("Node") >= 0);
	CHECK_EQ(exposed_classes.find("FSNativeClass"), -1);
	CHECK_EQ(exposed_classes.find("ThemeContext"), -1);
}
```

- [ ] **Step 2: Add the projectless CLI eval test**

Add this test beside the existing ScriptEval cases:

```cpp
TEST_CASE("[FoundryCLI][ScriptEval] Exposed native classes are available projectless") {
	List<String> arguments;
	arguments.push_back("--headless");
	arguments.push_back("script");
	arguments.push_back("eval");
	arguments.push_back(
			"var classes := ClassDB.get_exposed_class_list(); "
			"print(\"has-node=\", classes.has(\"Node\")); "
			"print(\"has-fs-native-class=\", classes.has(\"FSNativeClass\")); "
			"print(\"has-theme-context=\", classes.has(\"ThemeContext\"))");

	int exit_code = -1;
	const String output = run_foundry_subprocess(arguments, exit_code);
	INFO("Subprocess output:\n", output);
	CHECK(output.contains("has-node=true"));
	CHECK(output.contains("has-fs-native-class=false"));
	CHECK(output.contains("has-theme-context=false"));
	CHECK_EQ(exit_code, 0);
}
```

- [ ] **Step 3: Rebuild the tests without production changes**

Run:

```sh
scons platform=macos target=editor dev_mode=yes dev_build=yes tests=yes
```

Expected: exit 0; the tests compile because they reach the API dynamically.

- [ ] **Step 4: Verify both tests fail for the missing method**

Run:

```sh
./bin/foundry.macos.editor.dev.* --headless test run --case "*Exposed class list*" --force-colors
./bin/foundry.macos.editor.dev.* --headless test run --case "*Exposed native classes*" --force-colors
```

Expected: the ClassDB test receives a non-`PACKED_STRING_ARRAY` result, and script eval reports that
`get_exposed_class_list` is not defined. These are the intended RED failures.

### Task 3: Implement the native helper and script binding

**Files:**
- Modify: `core/object/class_db.h`
- Modify: `core/object/class_db.cpp`
- Modify: `core/core_bind.h`
- Modify: `core/core_bind.cpp`

- [ ] **Step 1: Declare and implement the native exposed-list helper**

Add beside `get_class_list()`:

```cpp
static void get_exposed_class_list(LocalVector<StringName> &p_classes);
```

Implement it using the established append-and-sort pattern:

```cpp
// This function only sorts items added by this function.
// If `p_classes` is not empty before calling and a global sort is needed, caller must handle that separately.
void ClassDB::get_exposed_class_list(LocalVector<StringName> &p_classes) {
	Locker::Lock lock(Locker::STATE_READ);

	const uint32_t original_size = p_classes.size();
	for (const KeyValue<StringName, ClassInfo> &class_info : classes) {
		if (class_info.value.exposed) {
			p_classes.push_back(class_info.key);
		}
	}

	if (p_classes.size() == original_size) {
		return;
	}

	SortArray<StringName, StringName::AlphCompare> sorter;
	sorter.sort(&p_classes[original_size], p_classes.size() - original_size);
}
```

- [ ] **Step 2: Add and bind the script wrapper**

Declare `PackedStringArray get_exposed_class_list() const;` beside `get_class_list()`. Implement it
by calling native `::ClassDB::get_exposed_class_list()` and copying the names into a
`PackedStringArray`, matching the existing `get_class_list()` wrapper. Bind it with:

```cpp
::ClassDB::bind_method(D_METHOD("get_exposed_class_list"), &ClassDB::get_exposed_class_list);
```

- [ ] **Step 3: Rebuild and verify GREEN**

Run:

```sh
scons platform=macos target=editor dev_mode=yes dev_build=yes tests=yes
./bin/foundry.macos.editor.dev.* --headless test run --case "*Exposed class list*" --force-colors
./bin/foundry.macos.editor.dev.* --headless test run --case "*Exposed native classes*" --force-colors
```

Expected: build exit 0 and both tests report doctest success.

### Task 4: Share the helper with extension API generation and document it

**Files:**
- Modify: `core/extension/extension_api_dump.cpp`
- Modify: `doc/classes/ClassDB.xml`

- [ ] **Step 1: Reuse the native exposed-list helper**

Replace the extension dump's `get_class_list()` call plus per-entry
`is_class_exposed()` guard with:

```cpp
ClassDB::get_exposed_class_list(class_list);
```

Keep the remainder of class serialization unchanged.

- [ ] **Step 2: Document the method and clarify the existing list**

Add a `get_exposed_class_list` method entry returning `PackedStringArray`. State that it is
lexically sorted, matches native classes visible to Foundry Script, and omits internal
registrations. Clarify that `get_class_list()` includes internal registrations.

- [ ] **Step 3: Run focused verification**

Run:

```sh
scons platform=macos target=editor dev_mode=yes dev_build=yes tests=yes
./bin/foundry.macos.editor.dev.* --headless test run --case "*ClassDB*" --force-colors
./bin/foundry.macos.editor.dev.* --headless test run --case "*FoundryCLI*ScriptEval*" --force-colors
git diff --check
```

Expected: build and tests succeed; `git diff --check` prints nothing.

### Task 5: Verify, commit, and converge with Cursor review

**Files:**
- Verify: all branch changes

- [ ] **Step 1: Run final CI-style build and full test suite**

Run:

```sh
scons platform=macos target=editor dev_mode=yes dev_build=yes tests=yes
./bin/foundry.macos.editor.dev.* --headless test run \
  --progress-format=jsonl \
  --progress-file /tmp/foundry-1341-test-progress.jsonl \
  --force-colors
```

Expected: SCons exits 0 and the final doctest summary reports success.

- [ ] **Step 2: Inspect and commit the implementation**

Run:

```sh
git diff --check
git diff --stat origin/develop...HEAD
git status --short
git add core/object/class_db.h core/object/class_db.cpp core/core_bind.h core/core_bind.cpp \
  core/extension/extension_api_dump.cpp doc/classes/ClassDB.xml \
  tests/core/object/test_class_db.h tests/core/os/test_foundry_cli_project_test.h \
  docs/superpowers/plans/2026-07-29-exposed-native-class-list.md
git commit -m "Expose analyzer-visible native classes"
```

Expected: one focused implementation commit after the design commit.

- [ ] **Step 3: Run Cursor review against `origin/develop`**

Use the exact foreground, read-only command from the `cursor-review` skill with
`CURSOR_REVIEW_BASE=origin/develop`. Validate every finding with
`superpowers:receiving-code-review`; for real bugs also use
`superpowers:systematic-debugging`. Fix, verify, commit, and repeat until the latest valid review
returns `RESULT: clean`.

### Task 6: Publish the pull request and enable auto-merge

**Files:**
- Publish: branch `issue-1341`

- [ ] **Step 1: Re-run fresh pre-push verification**

Run the final CI-style build and focused ClassDB plus ScriptEval filters again on the reviewed HEAD.
Expected: all exit 0 with successful doctest summaries.

- [ ] **Step 2: Push and open the PR**

Run:

```sh
git push -u origin issue-1341
gh pr create --repo cafecito-games/Foundry --base develop --head issue-1341 \
  --title "Expose analyzer-visible native classes" \
  --body $'## Summary\n\n- add a deterministic ClassDB API for analyzer-visible native classes\n- share the exposed-class filter with extension API generation\n- document and test direct and projectless CLI access\n\n## Verification\n\n- CI-style macOS editor build with tests\n- focused ClassDB and FoundryCLI ScriptEval tests\n- full headless doctest suite\n- Cursor review converged clean\n\nCloses #1341'
```

- [ ] **Step 3: Enable squash auto-merge**

Run:

```sh
gh pr merge --repo cafecito-games/Foundry --squash --auto
gh pr view --repo cafecito-games/Foundry --json url,state,mergeStateStatus,autoMergeRequest
```

Expected: the PR reports an active squash auto-merge request, or has already merged.

- [ ] **Step 4: Clean up only if merged**

If the PR is already merged, remove the worktree and delete the local branch from the main checkout.
If checks or review are pending, leave the worktree intact and report its path.
