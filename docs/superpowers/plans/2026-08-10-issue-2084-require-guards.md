# Issue 2084 REQUIRE Guard Hardening Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Keep failed cross-board test setup assertions from reaching unsafe pointer dereferences or tab indexing and terminating the full doctest process.

**Architecture:** Keep the change inside `tests/editor/test_editor_board_cross_board.h`. Make `CrossBoardHarness` cleanup idempotent and automatic so early returns are safe, then pair each safety-critical assertion with an explicit return and validate tab counts immediately before indexed reads.

**Tech Stack:** C++17, Godot/Foundry editor test fixtures, doctest 2.4.12, SCons/Ninja through `scripts/agent_build.py`.

---

### Task 1: Establish the failing mutation

**Files:**
- Temporarily modify: `tests/editor/test_editor_board_cross_board.h:347`
- Test: `tests/editor/test_editor_board_cross_board.h`

- [ ] **Step 1: Record the clean baseline.**

Run:

```sh
python3 scripts/agent_build.py --backend ninja --jobs 4 --test --case "*Boards*"
```

Expected: the strict development build succeeds and every selected Boards case passes.

- [ ] **Step 2: Break one setup precondition without changing the assertion.**

In the dormant-board reorder case, temporarily replace:

```cpp
EditorBoard *board_b = h.strip->add_board("Board B");
```

with:

```cpp
EditorBoard *board_b = nullptr;
```

- [ ] **Step 3: Run the broken case with a later sentinel case.**

Run:

```sh
./bin/foundry.macos.editor.dev.arm64 --headless test run \
  --case "*dormant board does not steal focus*" \
  --case "*active board keeps claiming focus*" \
  --force-colors
```

Expected before the fix: the `REQUIRE(board_b != nullptr)` failure is printed, execution reaches the null dereference, the process terminates by signal, and the sentinel case does not pass.

- [ ] **Step 4: Restore the precondition source line.**

Restore:

```cpp
EditorBoard *board_b = h.strip->add_board("Board B");
```

Do not retain the mutation in the branch diff.

### Task 2: Make early-return cleanup safe

**Files:**
- Modify: `tests/editor/test_editor_board_cross_board.h:50-77`

- [ ] **Step 1: Add automatic cleanup.**

Add a destructor that delegates to `unmount()`:

```cpp
~CrossBoardHarness() {
	unmount();
}
```

- [ ] **Step 2: Make `unmount()` idempotent and clear dangling pointers.**

Implement this shape:

```cpp
void unmount() {
	if (host == nullptr) {
		return;
	}
	SceneTree::get_singleton()->get_root()->remove_child(host);
	memdelete(host);
	memdelete(selection);
	host = nullptr;
	selection = nullptr;
	strip = nullptr;
}
```

Existing explicit normal-path `h.unmount()` calls may remain; the destructor becomes a no-op after them.

### Task 3: Guard pointers and indexed operations

**Files:**
- Modify: `tests/editor/test_editor_board_cross_board.h:86-461`
- Test: `tests/editor/test_editor_board_cross_board.h`

- [ ] **Step 1: Guard every required pointer before its first unsafe use.**

Use this exact pattern for single pointers:

```cpp
REQUIRE(board_b != nullptr);
if (board_b == nullptr) {
	return;
}
```

Use a combined guard only when no pointer is dereferenced between assertions:

```cpp
REQUIRE(workspace_a != nullptr);
REQUIRE(workspace_b != nullptr);
if (workspace_a == nullptr || workspace_b == nullptr) {
	return;
}
```

Cover `first_board`/`board_a`, `board_b`/`second_board`, every workspace, every leaf that is dereferenced or passed as a required destination/source, and every pane that is dereferenced. Add missing `REQUIRE` assertions for pointers that are currently used unchecked.

- [ ] **Step 2: Guard tab-index preconditions.**

Before passing tab index `0` into a drop operation, pair the existing exact-count assertion with:

```cpp
REQUIRE(pane_a->get_tab_count() == 1);
if (pane_a->get_tab_count() != 1) {
	return;
}
```

Before the reorder operations that use indices `0` and `2`, pair the three-tab assertion with the same `!= 3` early return.

- [ ] **Step 3: Guard every post-operation `get_tab()` sequence.**

Replace the scene-drop `CHECK(count == 1)` before `get_tab(0)` with:

```cpp
REQUIRE(pane_b->get_tab_count() == 1);
if (pane_b->get_tab_count() != 1) {
	return;
}
CHECK(pane_b->get_tab(0).get_type_id() == StringName("scene"));
```

Apply the same assertion and guard to the script-drop case, retaining its existing indexed comparison:

```cpp
CHECK(pane_b->get_tab(0).get_resource_key() == "res://move.fs");
```

Immediately before each three-element reorder verification, add:

```cpp
REQUIRE(pane->get_tab_count() == 3);
if (pane->get_tab_count() != 3) {
	return;
}
```

Keep non-safety assertions such as dormant state, focused tile, destination equality, and workspace vector size unchanged when their failure cannot make the next operation unsafe.

- [ ] **Step 4: Build and run the focused cases.**

Run:

```sh
python3 scripts/agent_build.py --backend ninja --jobs 4 --test --case "*Boards*"
```

Expected: build succeeds and all selected cases pass without new leak diagnostics.

### Task 4: Prove the guard changes the failure mode

**Files:**
- Temporarily modify: `tests/editor/test_editor_board_cross_board.h` in the dormant-board reorder setup

- [ ] **Step 1: Reapply the same `board_b = nullptr` mutation.**

Retain the new `REQUIRE` and early-return guard.

- [ ] **Step 2: Rerun the broken and sentinel cases.**

Run:

```sh
./bin/foundry.macos.editor.dev.arm64 --headless test run \
  --case "*dormant board does not steal focus*" \
  --case "*active board keeps claiming focus*" \
  --force-colors
```

Expected after the fix: two cases execute; the mutated case reports one ordinary assertion failure, the sentinel case passes, no fatal signal occurs, and the process exits with doctest failure status `1`.

- [ ] **Step 3: Restore the mutation and prove green again.**

Restore `h.strip->add_board("Board B")`, rebuild incrementally, rerun `--case "*Boards*"`, and confirm all selected tests pass.

### Task 5: Validate, commit, review, and publish

**Files:**
- Modify: `tests/editor/test_editor_board_cross_board.h`
- Create: `docs/superpowers/plans/2026-08-10-issue-2084-require-guards.md`

- [ ] **Step 1: Run the required native strict validation.**

Run:

```sh
python3 scripts/agent_build.py --jobs 4 --test --case "*Boards*"
```

Expected: native SCons strict build succeeds and all selected Boards cases pass.

- [ ] **Step 2: Inspect the final diff and commit.**

Run:

```sh
git diff --check
git status --short
git diff -- tests/editor/test_editor_board_cross_board.h docs/superpowers/plans/2026-08-10-issue-2084-require-guards.md
git add tests/editor/test_editor_board_cross_board.h docs/superpowers/plans/2026-08-10-issue-2084-require-guards.md
git commit -m "Harden cross-board test preconditions"
```

Expected: only the test header and this plan are committed.

- [ ] **Step 3: Run supervised Codex review to a clean verdict.**

Run the blocking review driver against `origin/develop`; fix every in-scope finding, revalidate, commit, and repeat until clean.

- [ ] **Step 4: Push and open the PR.**

Push `issue-2084`, open a PR targeting `develop` whose body includes the focused/native validation and mutation output and ends with `Closes #2084`, then enable squash auto-merge.

- [ ] **Step 5: Clean up only after merge.**

After GitHub reports the PR merged, remove the worktree through the repository-supported worktree flow and delete the local `issue-2084` branch.
