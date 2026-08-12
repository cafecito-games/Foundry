# Cross-Board Existing Scene Focus Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Reopening a scene that already lives on another board reveals that board without violating EditorData focus invariants or attaching the scene context to the wrong tile.

**Architecture:** Keep cross-board navigation in `EditorNode::load_scene()`, where scene paths are resolved. Activate the owning board and focus its canonical scene tab before changing the editor-wide current scene; retain direct `_set_current_scene()` only as a fallback when no workspace can perform the reveal.

**Tech Stack:** C++17, Foundry editor automation workflows, doctest, `scripts/agent_build.py`.

---

### Task 1: Add the failing real-editor regression

**Files:**
- Modify: `editor/automation/editor_automation_acceptance_workflow.h`
- Modify: `editor/automation/editor_automation_acceptance_workflow.cpp`
- Modify: `editor/automation/editor_automation_workflow_registry.cpp`
- Test: `tests/editor/test_editor_board_3d_switch.h`

- [ ] **Step 1: Register a `cross_board_existing_scene_reveal` workflow**

Declare and register a workflow that starts with `res://scenes/main.tscn` open on Board 1, adds an empty Board 2, activates Board 2, then calls:

```cpp
if (editor_node->load_scene(BOARD_SWITCH_2D_SCENE) != OK) {
	return _failure_with_message(p_driver, result.workflow, "Failed to reveal the already-open scene.");
}
```

- [ ] **Step 2: Assert the observable postconditions**

After flushing frames, assert that Board 1 is active, the owning tile is the focused tile, the active scene index is the original scene, the scene context is active, its viewport is parented beneath the owning tile's display host, and `assert_no_new_errors_since_step()` succeeds.

- [ ] **Step 3: Add a subprocess doctest**

Launch the disposable editor fixture with:

```text
editor open --headless --project <project> --automation --automation-run-workflow=cross_board_existing_scene_reveal
```

Parse `FOUNDRY_AUTOMATION_WORKFLOW`, require `ok=true`, and require exit code 0.

- [ ] **Step 4: Build and verify RED**

Run:

```sh
python3 scripts/agent_build.py --compiler-cache ccache --jobs 4
./bin/foundry.macos.editor.dev.arm64 --headless test run --case "*[Editor][Boards] Reopening an existing scene from another board preserves focus*" --force-colors
```

Expected: the workflow fails because `EditorData focus invariant broken` is logged by the existing `load_scene()` ordering.

### Task 2: Reorder the existing-scene reveal

**Files:**
- Modify: `editor/editor_node.cpp:5742-5759`

- [ ] **Step 1: Implement the minimal ordering fix**

Replace the unconditional leading `_set_current_scene(i)` with a `revealed` flag. When the board strip resolves an owner, activate it and call `owner->get_workspace()->focus_scene_tab(i)`; otherwise try the active workspace. Call `_set_current_scene(i)` only when neither workspace reveals the scene.

- [ ] **Step 2: Verify GREEN**

Run the same focused doctest. Expected: one test passes, zero failures, and the workflow reports success.

- [ ] **Step 3: Run focused board regressions**

Run:

```sh
./bin/foundry.macos.editor.dev.arm64 --headless test run --case "*[Editor][Boards]*" --force-colors
```

Expected: all selected board tests pass.

### Task 3: Final validation and real UI proof

**Files:**
- No additional code changes expected.

- [ ] **Step 1: Run the native strict validation build**

Run:

```sh
python3 scripts/agent_build.py --compiler-cache ccache --jobs 4
```

Expected: exit code 0 with `dev_mode=yes dev_build=yes tests=yes`.

- [ ] **Step 2: Reproduce through StartupPerfProbe**

Launch the worktree binary with automation against `/Users/christian/.foundry-startup-profiles/probe-project`, activate Board 2, open the already-open `main.tscn`, then assert Board 1 owns the focused active scene context and no focus-invariant error appears.

- [ ] **Step 3: Review the final diff**

Confirm only the approved workflow/test, `load_scene()` ordering, and this plan changed; ensure no fixture cache or unrelated user files are included.
