# Cross-Pane Node Drop Lifetime Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Prevent cross-pane node-to-script drops from destroying their live `CodeEdit` during input dispatch, and contain the same stale-control failure class at generic GUI callback boundaries.

**Architecture:** Workspace focus lands immediately, while content activation waits for a generation-checked next-`process_frame` callback. `Control` and `Viewport` capture `ObjectID` before user-extensible callbacks and stop the current dispatch branch when the identity no longer resolves. A real-editor subprocess workflow drives the reported pointer gesture through pure-script and mixed-pane layouts.

**Tech Stack:** Godot/Foundry C++, doctest, editor automation workflow driver, SCons/Ninja through `scripts/agent_build.py`, GitHub CLI.

---

## File Map

- `scene/gui/control.cpp`: revalidate a `Control` after its signal and script virtual callbacks.
- `scene/main/viewport.cpp`: stop input propagation or drop-target reuse when callbacks destroy the current control.
- `tests/scene/test_viewport.h`: focused engine regressions for input and drag/drop callback invalidation.
- `editor/editor_node.h`: pending script-leaf activation identity and generation state.
- `editor/editor_node.cpp`: next-frame script activation queue and cancellation rules.
- `editor/automation/editor_automation_acceptance_workflow.h`: expose the node-to-script lifetime workflow.
- `editor/automation/editor_automation_acceptance_workflow.cpp`: run real pointer drags in pure and mixed workspace layouts.
- `editor/automation/editor_automation_workflow_registry.cpp`: register the workflow for subprocess execution.
- `tests/editor/test_script_leaf_node_drop.h`: launch the workflow and assert its observable result.

### Task 1: Add failing generic GUI lifetime regressions

**Files:**
- Modify: `tests/scene/test_viewport.h`

- [ ] **Step 1: Add callback-invalidating test controls**

Add focused helpers near `DragTarget`:

```cpp
class GuiInputLifetimeTarget : public Control {
	FOUNDRY_CLASS(GuiInputLifetimeTarget, Control);

public:
	int *native_input_count = nullptr;
	bool delete_during_native_input = false;

	void gui_input(const Ref<InputEvent> &p_event) override {
		if (native_input_count) {
			(*native_input_count)++;
		}
		if (delete_during_native_input) {
			memdelete(this);
		}
	}
};

class GuiInputLifetimeDestroyer : public Object {
	FOUNDRY_CLASS(GuiInputLifetimeDestroyer, Object);

public:
	ObjectID target_id;

	void destroy_target(const Ref<InputEvent> &p_event) {
		if (Control *target = ObjectDB::get_instance<Control>(target_id)) {
			memdelete(target);
		}
	}
};

class DropLifetimeTarget : public Control {
	FOUNDRY_CLASS(DropLifetimeTarget, Control);

public:
	bool delete_during_validation = false;
	bool delete_during_drop = false;
	mutable int validation_count = 0;
	int drop_count = 0;

	bool can_drop_data(const Point2 &p_point, const Variant &p_data) const override {
		validation_count++;
		if (delete_during_validation) {
			memdelete(const_cast<DropLifetimeTarget *>(this));
		}
		return true;
	}

	void drop_data(const Point2 &p_point, const Variant &p_data) override {
		drop_count++;
		if (delete_during_drop) {
			memdelete(this);
		}
	}
};
```

- [ ] **Step 2: Add the real viewport-input cases**

Add `TEST_CASE("[SceneTree][Viewport] GUI callback target lifetime")` using a mounted `Window` and real mouse events. The signal case must connect `gui_input` to `GuiInputLifetimeDestroyer::destroy_target`, retain the target `ObjectID`, suppress only the expected destructor-during-signal diagnostic with `ERR_PRINT_OFF`/`ERR_PRINT_ON`, and assert:

```cpp
CHECK(ObjectDB::get_instance(target_id) == nullptr);
CHECK(native_input_count == 0);
```

The native-input case must put a `MOUSE_FILTER_PASS` child inside a counting parent, delete the child from `gui_input()`, and assert the child identity is gone and the parent did not receive the same event after the hierarchy disappeared.

- [ ] **Step 3: Add drag/drop invalidation cases**

Use the existing `DragStart` gesture path twice. For validation-time deletion, assert the target identity is gone, `gui_is_drag_successful()` is false, and the gesture returns to `gui_is_dragging() == false`. For accepted-drop deletion, retain the target ID before release and assert:

```cpp
CHECK(ObjectDB::get_instance(target_id) == nullptr);
CHECK(root->gui_is_drag_successful());
CHECK_FALSE(root->gui_is_dragging());
```

- [ ] **Step 4: Build the new tests without production changes**

Run:

```sh
python3 scripts/agent_build.py --backend ninja --jobs 4
```

Expected: build succeeds, because the regression helpers use existing public test APIs.

- [ ] **Step 5: Verify RED**

Run:

```sh
./bin/foundry.macos.editor.dev.arm64 --headless test run --case "*GUI callback target lifetime*" --force-colors
```

Expected: the focused process exits nonzero from a stale-pointer crash or a failed lifetime assertion on current `develop`. Record which subcase exposes each reuse site.

- [ ] **Step 6: Commit the failing regressions**

```sh
git add tests/scene/test_viewport.h
git commit -m "test: cover destroyed GUI callback targets"
```

### Task 2: Revalidate controls across generic callback boundaries

**Files:**
- Modify: `scene/gui/control.cpp:2286`
- Modify: `scene/main/viewport.cpp:1712`
- Modify: `scene/main/viewport.cpp:1860`
- Test: `tests/scene/test_viewport.h`

- [ ] **Step 1: Harden `Control::_call_gui_input()`**

Replace raw `this` reuse after externally controlled callbacks with identity resolution:

```cpp
void Control::_call_gui_input(const Ref<InputEvent> &p_event) {
	const ObjectID control_id = get_instance_id();
	Control *control = this;
	if (p_event->get_device() != InputEvent::DEVICE_ID_INTERNAL) {
		emit_signal(SceneStringName(gui_input), p_event);
		control = ObjectDB::get_instance<Control>(control_id);
		if (!control) {
			return;
		}
	}
	if (!control->is_inside_tree() || control->get_viewport()->is_input_handled()) {
		return;
	}

	if (p_event->get_device() != InputEvent::DEVICE_ID_INTERNAL) {
		FOUNDRY_VIRTUAL_CALL_PTR(control, _gui_input, p_event);
		control = ObjectDB::get_instance<Control>(control_id);
		if (!control) {
			return;
		}
	}
	if (!control->is_inside_tree() || control->get_viewport()->is_input_handled()) {
		return;
	}
	control->gui_input(p_event);
}
```

- [ ] **Step 2: Stop viewport input propagation when the current control disappears**

Immediately before `control->_call_gui_input(ev)`, capture `control->get_instance_id()`. Immediately after the call, resolve it through `ObjectDB::get_instance<Control>()`. If it is null, `break` from the propagation loop before reading tree state, mouse filter, transform, or parent data. If it survives, assign both `control` and `ci` to the resolved identity before continuing.

- [ ] **Step 3: Harden `_gui_drop()` validation and delivery**

For each candidate control, capture its ID before `can_drop_data()`. If validation destroys it, return `false` without bubbling through the changed hierarchy. If validation accepts and this is a real drop, call `drop_data()`, then return `true` without any further target dereference; resolving the identity afterward is allowed only for an explicit assertion or cleanup that does not affect acceptance. Preserve existing mouse-filter propagation when the candidate survives and rejects the drop.

- [ ] **Step 4: Verify GREEN and unchanged normal propagation**

Run:

```sh
python3 scripts/agent_build.py --backend ninja --jobs 4
./bin/foundry.macos.editor.dev.arm64 --headless test run --case "*GUI callback target lifetime*" --case "*Controls and InputEvent handling*" --force-colors
```

Expected: both focused cases report doctest `Status: SUCCESS!` with zero failed assertions.

- [ ] **Step 5: Commit generic hardening**

```sh
git add scene/gui/control.cpp scene/main/viewport.cpp
git commit -m "Harden GUI dispatch against destroyed controls"
```

### Task 3: Add the real-editor node-to-script regression workflow

**Files:**
- Modify: `editor/automation/editor_automation_acceptance_workflow.h`
- Modify: `editor/automation/editor_automation_acceptance_workflow.cpp`
- Modify: `editor/automation/editor_automation_workflow_registry.cpp`
- Modify: `tests/editor/test_script_leaf_node_drop.h`

- [ ] **Step 1: Declare and register the workflow**

Add:

```cpp
static Result run_cross_pane_node_to_script_drop(EditorWorkflowTestDriver &p_driver);
```

Add a registry wrapper and register the canonical name:

```cpp
EditorAutomationAcceptanceWorkflow::Result _run_cross_pane_node_to_script_drop(EditorWorkflowTestDriver &p_driver) {
	return EditorAutomationAcceptanceWorkflow::run_cross_pane_node_to_script_drop(p_driver);
}

_register_workflow("cross_pane_node_to_script_drop", &_run_cross_pane_node_to_script_drop);
```

- [ ] **Step 2: Build a same-scene drag source at runtime**

In the new workflow, call `_load_mixed_workspace_scene()`, resolve the edited `Main` root and focused `SceneTreeDock`, add a `Node2D` named `DropProbe`, set its owner to `Main`, call `scene_dock->update_tree()`, and flush frames until the tree item appears. This keeps the shared fixture unchanged.

- [ ] **Step 3: Add a real pointer-drag helper**

Implement a local helper that accepts the source `Tree`, source `TreeItem`, target `CodeEdit`, and expected target leaf ID. Compute the source point with `Tree::get_item_area_rect()` and the target point from the `CodeEdit` global rect. Drive one continuous gesture:

```cpp
PackedStringArray events;
EditorAutomationInputModifiers modifiers;
if (!EditorAutomationInput::begin_mouse_gesture(viewport, source_point, MouseButton::LEFT, modifiers, events) ||
		!EditorAutomationInput::move_mouse_gesture(viewport, target_point, Vector<Vector2>(), modifiers, events) ||
		!viewport->gui_is_dragging() ||
		!EditorAutomationInput::end_mouse_gesture(viewport, target_point, modifiers, events)) {
	return false;
}
```

Retain the target `CodeEdit` ID before release, flush exactly one frame, send an input-routed text event to the re-resolved editor, then flush until workspace and script analysis settle. Return failure unless the target ID still resolves, the workspace focused leaf matches the receiver, and the editor text contains `$DropProbe` followed by the immediate probe character.

- [ ] **Step 4: Exercise the pure script pane**

Use `_add_script_tab_to_scene_pane()` and `_split_script_tab_to_right()` to produce a separate script pane. Re-focus the scene leaf before each drag, resolve the mounted `ScriptLeaf` and active `CodeEdit`, then run the drag helper twice. Assert both insertions occur, the following input is accepted, and `assert_no_new_errors_since_step()` covers each complete drag/release/input window.

- [ ] **Step 5: Exercise the mixed scene-capable pane**

Move the script tab back into the scene pane with `_move_script_tab_back_to_scene_pane()`, make the script tab active, focus a separate source scene pane, and repeat the same real pointer drag twice. Assert the mixed pane remains focused, its active script surface survives, and both insertions plus following input are present.

- [ ] **Step 6: Exercise cross-scene rejection**

Create a second edited scene with a `ForeignProbe` child, place it in a separate scene pane, drag that child into the original script, and assert the `CodeEdit` text is unchanged while the editor remains alive. Treat the existing user-facing rejection message as allowed feedback, but require `assert_no_new_errors_since_step()` to remain clean.

- [ ] **Step 7: Return structured workflow evidence**

On success, set:

```cpp
result.ok = true;
result.message = "Cross-pane node-to-script drops preserve the live script editor.";
Dictionary details;
details["pure_script_drop_count"] = 2;
details["mixed_pane_drop_count"] = 2;
details["cross_scene_rejected"] = true;
details["immediate_input_safe"] = true;
result.details = details;
```

- [ ] **Step 8: Add the subprocess doctest**

Add a display-gated case named `[Editor][SceneTree] Cross-pane node-to-script drop workflow subprocess`. Prepare the disposable `editor_automation_workflow` project, run:

```cpp
arguments.push_back("editor");
arguments.push_back("open");
arguments.push_back("--headless");
arguments.push_back("--project");
arguments.push_back(project_path);
arguments.push_back("--automation");
arguments.push_back("--automation-run-workflow=cross_pane_node_to_script_drop");
```

Parse `FOUNDRY_AUTOMATION_WORKFLOW`, assert the workflow name, `ok`, detail counts, cross-scene rejection, immediate-input safety, and `exit_code == 0`.

- [ ] **Step 9: Build the workflow without the workspace fix**

```sh
python3 scripts/agent_build.py --backend ninja --jobs 4
```

Expected: build succeeds.

- [ ] **Step 10: Verify RED through the real editor**

```sh
./bin/foundry.macos.editor.dev.arm64 --headless test run --case "*Cross-pane node-to-script drop workflow subprocess*" --force-colors
```

Expected: the workflow exits nonzero, reports a failed lifetime/error assertion, or reproduces the crash before emitting a successful workflow result.

- [ ] **Step 11: Commit the failing workflow**

```sh
git add editor/automation/editor_automation_acceptance_workflow.h editor/automation/editor_automation_acceptance_workflow.cpp editor/automation/editor_automation_workflow_registry.cpp tests/editor/test_script_leaf_node_drop.h
git commit -m "test: reproduce cross-pane node drop lifetime"
```

### Task 4: Queue script content activation at the next frame

**Files:**
- Modify: `editor/editor_node.h:379`
- Modify: `editor/editor_node.h:803`
- Modify: `editor/editor_node.cpp:7990`
- Modify: `editor/editor_node.cpp:8124`
- Test: `tests/editor/test_script_leaf_node_drop.h`

- [ ] **Step 1: Add pending script activation state and helpers**

Add fields:

```cpp
int pending_script_leaf_id = -1;
uint64_t pending_script_leaf_generation = 0;
```

Add declarations:

```cpp
void _cancel_queued_script_leaf_activation();
void _queue_script_leaf_activation(int p_leaf_id);
void _activate_queued_script_leaf(int p_leaf_id, uint64_t p_generation);
```

- [ ] **Step 2: Implement generation-checked next-frame activation**

Implement cancellation, queueing, and activation beside the tile queue. Queue through one-shot `process_frame`, with `call_deferred()` only as the no-`SceneTree` fallback. `_activate_queued_script_leaf()` must verify the generation, pending ID, active workspace, current leaf, workspace pane, and script leaf before calling `_complete_script_leaf_focus()`.

- [ ] **Step 3: Make scene and script requests supersede each other**

Before queueing a tile activation, cancel any pending script activation. Before queueing a script activation, cancel any pending tile activation. Both `_focus_tile()` and `_focus_script_leaf()` must cancel both queue types before synchronous activation so an older next-frame callback cannot steal focus or remount content later.

- [ ] **Step 4: Replace drag-time `call_deferred()`**

Keep this ordering in `_on_leaf_focus_requested()`:

```cpp
get_scene_workspace()->set_focused_leaf(p_leaf_id);
Viewport *editor_viewport = get_viewport();
if (editor_viewport && editor_viewport->gui_is_dragging()) {
	_queue_script_leaf_activation(p_leaf_id);
	return;
}
_cancel_queued_script_leaf_activation();
_complete_script_leaf_focus(p_leaf_id);
```

The receiving leaf is focused before `drop_data_fw()` inserts text, but `ScriptLeaf::on_focus_entered()` and `reveal_script_leaf()` cannot mount or unmount content until the next frame.

- [ ] **Step 5: Verify GREEN**

Run:

```sh
python3 scripts/agent_build.py --backend ninja --jobs 4
./bin/foundry.macos.editor.dev.arm64 --headless test run --case "*Cross-pane node-to-script drop workflow subprocess*" --case "*node-to-script-drag*" --case "*drop-focuses-target*" --case "*cross-scene-node-drop*" --force-colors
```

Expected: the subprocess workflow and all three helper cases report doctest `Status: SUCCESS!`.

- [ ] **Step 6: Prove the regression test detects the workspace fix**

Temporarily restore only the `_on_leaf_focus_requested()` drag branch to `call_deferred()`, rebuild, and rerun the subprocess test. Expected: it fails in the same lifetime/error window observed in Task 3. Restore the next-frame queue, rebuild, and rerun; expected: PASS. Do not commit the temporary reversion.

- [ ] **Step 7: Commit the workspace fix**

```sh
git add editor/editor_node.h editor/editor_node.cpp
git commit -m "Fix script drop focus lifetime"
```

### Task 5: File the broader GUI-dispatch architecture investigation

**Files:**
- No repository files.

- [ ] **Step 1: Create the follow-up issue**

Run:

```sh
FOLLOWUP_URL=$(gh issue create --repo cafecito-games/Foundry \
  --title "Investigate identity-safe GUI dispatch architecture" \
  --body $'Part of #942. Follow-up to #2131.\n\n## Question\n\nShould viewport GUI focus, hover, drag, and propagation state store `ObjectID` and resolve at every use, or should GUI dispatch defer destruction until user callbacks unwind?\n\n## Investigation\n\n- Audit every user-extensible callback boundary in Control and Viewport input and drag/drop dispatch.\n- Prototype complete `ObjectID` storage/resolution for viewport GUI state.\n- Prototype dispatch-scoped destruction deferral with explicit hierarchy-mutation semantics.\n- Measure input-path overhead and compare implementation and migration risk.\n- Recommend one direction or document why the targeted #2131 guards should remain.\n\n## Acceptance\n\n- Focused input and drag/drop tests exercise callback-time target destruction.\n- Prototype measurements and semantic differences are recorded.\n- A written decision identifies the preferred architecture before implementation work is opened.')
```

- [ ] **Step 2: Add the issue to Experiment as Todo**

```sh
gh project item-add 3 --owner cafecito-games --url "$FOLLOWUP_URL"
```

Resolve the new project item ID through GraphQL and set Status to Todo option `f75ad846` if the default is not already Todo.

- [ ] **Step 3: Record the issue URL for the PR and final report**

Add the URL to the PR body under `Follow-up` when the PR is opened. Do not add a repository-only placeholder file.

### Task 6: Run complete validation

**Files:**
- Verify all changed files.

- [ ] **Step 1: Run formatting and diff checks**

```sh
pre-commit run --files scene/gui/control.cpp scene/main/viewport.cpp tests/scene/test_viewport.h editor/editor_node.h editor/editor_node.cpp editor/automation/editor_automation_acceptance_workflow.h editor/automation/editor_automation_acceptance_workflow.cpp editor/automation/editor_automation_workflow_registry.cpp tests/editor/test_script_leaf_node_drop.h docs/superpowers/specs/2026-08-12-cross-pane-node-drop-lifetime-design.md docs/superpowers/plans/2026-08-12-cross-pane-node-drop-lifetime.md
git diff --check origin/develop...HEAD
```

Expected: every hook passes and `git diff --check` prints nothing.

- [ ] **Step 2: Run the strict native build and full suite**

```sh
python3 scripts/agent_build.py --compiler-cache ccache --jobs 4 --test
```

Expected: strict `dev_mode=yes dev_build=yes tests=yes` build succeeds and the final doctest summary is `Status: SUCCESS!`. Use the exact JSONL progress path printed by the wrapper to monitor long-running cases.

- [ ] **Step 3: Run the focused lifetime suite once more**

```sh
./bin/foundry.macos.editor.dev.arm64 --headless test run --case "*GUI callback target lifetime*" --case "*Cross-pane node-to-script drop workflow subprocess*" --case "*node-to-script-drag*" --case "*drop-focuses-target*" --case "*cross-scene-node-drop*" --force-colors
```

Expected: zero failed cases and assertions.

- [ ] **Step 4: Inspect scope**

```sh
git status --short
git diff --stat origin/develop...HEAD
git log --oneline origin/develop..HEAD
```

Expected: only the design, plan, generic guards/tests, workspace queue, workflow/test, and focused commits are present.

### Task 7: Review convergence and pull request

**Files:**
- Fix only files implicated by in-scope review findings.

- [ ] **Step 1: Run supervised Codex review**

```sh
python3 ~/.claude/scripts/codex_review/await_review.py start-wait \
  --cwd /Users/christian/CafecitoGames/Foundry/.worktrees/issue-2131 \
  --scope branch --base origin/develop --deadline 540
```

Triage every finding. Fix all in-scope critical or blocking findings, rerun affected tests and strict validation, commit, and start a fresh review against the new HEAD until the verdict is clean or only filed out-of-scope follow-ups remain.

- [ ] **Step 2: Push and open the PR**

```sh
git push -u origin issue-2131
FOLLOWUP_URL=$(gh issue list --repo cafecito-games/Foundry --state open --search '"Investigate identity-safe GUI dispatch architecture" in:title' --json url --jq '.[0].url')
gh pr create --repo cafecito-games/Foundry --base develop --head issue-2131 \
  --title "Fix cross-pane node drop lifetime" \
  --body "$(printf '%s\n' \
    '## Summary' \
    '- Queue cross-pane script activation at the next process-frame boundary.' \
    '- Revalidate GUI callback targets before Control or Viewport reuses them.' \
    '- Cover pure-script and mixed-pane node drops in a real editor subprocess.' \
    '' \
    '## Tests' \
    '- Strict native build and full test suite through scripts/agent_build.py.' \
    '- Focused GUI lifetime and cross-pane node-drop doctest cases.' \
    '- Pre-commit checks for every changed file.' \
    '' \
    '## Follow-up' \
    "- Broader GUI dispatch architecture investigation: $FOLLOWUP_URL" \
    '' \
    'Closes #2131')"
```

Add the actual supervised-review round count and any review-driven fixes to the PR with `gh pr edit --body-file` if they are not already captured in commit messages.

- [ ] **Step 3: Enable squash auto-merge**

```sh
gh pr merge --repo cafecito-games/Foundry --squash --auto
```

- [ ] **Step 4: Clean up only after merge**

When GitHub reports the PR merged, verify `git worktree list --porcelain` does not mark this git-created worktree as locked, then run `git worktree remove /Users/christian/CafecitoGames/Foundry/.worktrees/issue-2131` followed by `git branch -D issue-2131`. If auto-merge is pending, preserve the worktree.
