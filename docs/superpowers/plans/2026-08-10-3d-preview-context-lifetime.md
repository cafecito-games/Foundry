# 3D Preview Context Lifetime Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or
> superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for
> tracking.

**Goal:** Eliminate stale 3D preview-furniture pointers across deferred board teardown and make secondary-view
world accounting exact.

**Architecture:** Store the preview parent, sun, and environment as `ObjectID`, resolve each object through
`ObjectDB` immediately before use, and treat expired identities as normal teardown. Make each secondary viewport
own at most one registered world binding so a same-world refresh is idempotent and a world change transfers
exactly one association.

**Tech Stack:** Foundry editor C++, doctest, `TESTS_ENABLED` editor workflows, SCons/Ninja through
`scripts/agent_build.py`.

---

## Preconditions

- [ ] Read the approved design at
      `docs/superpowers/specs/2026-08-10-3d-preview-context-lifetime-design.md` and issue #2082.
- [ ] Work from a feature branch based on current `develop` and confirm the worktree is clean with `git status --short`.
- [ ] Preserve the exact deferred lifecycle under test: the scene context dies synchronously and the board, tile,
      and secondary viewport die later.
- [ ] Do not change board-close ordering, register views with scene contexts, or add production
      scripting/editor-automation APIs.

## Task 1: Lock down same-world binding accounting

**Files:**

- Modify: `editor/scene/3d/node_3d_editor_plugin.h`
- Modify: `editor/scene/3d/node_3d_editor_plugin.cpp`
- Modify: `editor/automation/editor_automation_acceptance_workflow.cpp`

### Add the failing observation

- [ ] Under `#ifdef TESTS_ENABLED`, add a narrow `Node3DEditor` accessor that returns a world's live-view count
      without creating a furniture entry:

```cpp
int get_world_live_view_count_for_tests(const Ref<World3D> &p_world) const;
```

- [ ] Implement it by calling the const `_get_world_furniture()` overload and returning `0` when the entry is absent.
- [ ] Extend `EditorAutomationAcceptanceWorkflow::run_board_switch_3d_scene()` after the existing repeated
      board-switch/rebind path. Obtain the demoted tile's secondary viewport and assert its bound world's count is
      exactly one.
- [ ] Make the assertion diagnostic name both expected and actual counts.

### Prove the existing defect

- [ ] Build and run the focused test:

```sh
python3 scripts/agent_build.py --backend ninja --test --case "*Switching boards with a 3D scene open*"
```

- [ ] Confirm the test fails because repeated same-world `bind_world()` calls increased `live_view_count` above
      one. If it fails earlier, correct the test setup before editing production behavior.

### Implement exact ownership

- [ ] Add a private helper beside `_note_world_view_bound()`:

```cpp
void _note_world_view_rebound(const Ref<World3D> &p_world, SubViewport *p_preview_parent_viewport);
```

- [ ] Change `Node3DEditorViewport::bind_world()` to retain whether it already had a binding and whether the
      target world differs:

```cpp
ERR_FAIL_COND(p_world.is_null());

const bool had_bound_world = bound_world.is_valid();
const bool world_changed = had_bound_world && bound_world != p_world;
if (world_changed && spatial_editor) {
	spatial_editor->_note_world_view_unbound(bound_world);
}

bound_world = p_world;
preview_parent_viewport = p_preview_parent_viewport;
viewport->set_world_3d(bound_world);
_update_gizmos();

if (spatial_editor) {
	if (!had_bound_world || world_changed) {
		spatial_editor->_note_world_view_bound(bound_world, p_preview_parent_viewport);
	} else {
		spatial_editor->_note_world_view_rebound(bound_world, p_preview_parent_viewport);
	}
}
```

- [ ] Implement `_note_world_view_rebound()` by ensuring/updating furniture and scheduling the same parenting
      synchronization as a first bind, without incrementing `live_view_count`.
- [ ] Keep `release_secondary_viewport()` as the single unregister point for the view's current world.
- [ ] Replace the current underflow clamp in `_note_world_view_unbound()` with invariant guards before decrementing:

```cpp
ERR_FAIL_NULL_MSG(furniture, "Cannot unbind a 3D view from a world with no furniture entry.");
ERR_FAIL_COND_MSG(furniture->live_view_count == 0, "Cannot unbind a 3D view from a world with no live views.");
furniture->live_view_count--;
```

- [ ] Preserve immediate furniture release when the successful decrement reaches zero.

### Verify and checkpoint

- [ ] Rerun the focused test and confirm it passes.
- [ ] Run `git diff --check` and inspect the Task 1 diff.
- [ ] Commit with subject `Fix 3D preview world binding accounting`.

## Task 2: Add the deferred-close regression before changing identities

**Files:**

- Modify: `editor/scene/3d/node_3d_editor_plugin.h`
- Modify: `editor/scene/3d/node_3d_editor_plugin.cpp`
- Modify: `editor/automation/editor_automation_acceptance_workflow.h`
- Modify: `editor/automation/editor_automation_acceptance_workflow.cpp`
- Modify: `editor/automation/editor_automation_workflow_registry.cpp`
- Modify: `tests/editor/test_editor_board_3d_switch.h`

### Add test-only inspection hooks

- [ ] Under `#ifdef TESTS_ENABLED`, add the minimum accessors needed by the real-editor workflow. At this red-test
      stage they may expose the current raw identities, but they must not become production APIs:

```cpp
SubViewport *get_preview_parent_viewport_for_tests() const;

bool has_world_furniture_for_tests(const Ref<World3D> &p_world) const;
ObjectID get_world_preview_parent_id_for_tests(const Ref<World3D> &p_world) const;
ObjectID get_world_preview_sun_id_for_tests(const Ref<World3D> &p_world) const;
ObjectID get_world_preview_environment_id_for_tests(const Ref<World3D> &p_world) const;
void sync_preview_environment_parenting_for_tests();
void preview_settings_changed_for_tests();
```

- [ ] Keep accessors observational: do not create furniture while reading it.
- [ ] Make the two action hooks call `_sync_preview_environment_parenting()` and `_preview_settings_changed()`
      synchronously.

### Register the workflow

- [ ] Declare `run_board_close_3d_context_lifetime()` beside `run_board_switch_3d_scene()` in
      `editor_automation_acceptance_workflow.h`.
- [ ] Add a registry wrapper and register the exact workflow name `board_close_3d_context_lifetime` under
      `#ifdef TESTS_ENABLED`.

### Implement the exact lifecycle

- [ ] Reuse the saved 3D and 2D fixture scenes already used by the board-switch workflow.
- [ ] In the new workflow, perform these steps in order:

  1. Load the 3D fixture on Board 1.
  2. Add and activate Board 2, then load the 2D fixture.
  3. Switch back and away enough to exercise the same-world rebind path.
  4. Locate Board 1's demoted tile and require a secondary `Node3DEditorViewport`.
  5. Capture `ObjectID`s for Board 1, tile, secondary view, world, context viewport, preview sun, and preview
     environment.
  6. Assert the parent, sun, and environment resolve and the furniture nodes are children of the captured context
     viewport.
  7. Assert the world's live-view count is one.
  8. Call `SceneBoardStrip::close_board(0)` and require `false`, proving scene close is pending.
  9. Before flushing deferred work, require board/tile/view identities to resolve and context
     viewport/sun/environment identities not to resolve.
  10. Require the secondary view's resolved preview parent to be `nullptr` and the live-view count to remain one.
  11. Snapshot the editor error count, force parenting synchronization and preview-settings propagation, then
      require no new editor errors.
  12. Flush until pending board close completes.
  13. Require board/tile/view identities not to resolve and the old world's furniture entry to be absent.

- [ ] Every lookup must fail with a specific workflow message. Process survival by itself is not a passing result.
- [ ] Do not flush the message queue between `close_board(0)` and the assertions for the context-dead/tile-live gap.

### Add the subprocess doctest

- [ ] Add a test case next to the existing 3D board-switch coverage:

```cpp
TEST_CASE("[Editor][Boards] 3D preview context lifetime survives deferred board close") {
	EditorAutomationAcceptanceTest::run_workflow("board_close_3d_context_lifetime");
}
```

- [ ] Use the repository's existing workflow runner/helper signature exactly; adapt the body to nearby tests
      rather than adding a second runner.

### Prove the regression is red

- [ ] Build and run only the new case:

```sh
python3 scripts/agent_build.py --backend ninja --test --case "*3D preview context lifetime*"
```

- [ ] Confirm it reaches the exact gap and fails because the surviving secondary view still reports or consumes a
      deleted preview object. If the process happens not to crash, the explicit identity assertion must still fail.
- [ ] Record the failing assertion/output for the implementation PR.

## Task 3: Replace every cross-lifetime furniture address with identity

**Files:**

- Modify: `editor/scene/3d/node_3d_editor_plugin.h`
- Modify: `editor/scene/3d/node_3d_editor_plugin.cpp`

### Change stored state

- [ ] Replace `Node3DEditorViewport::preview_parent_viewport` with:

```cpp
ObjectID preview_parent_viewport_id;
SubViewport *_resolve_preview_parent_viewport() const;
```

- [ ] Keep `bind_world(..., SubViewport *)` pointer-valued for callers, but immediately store
      `p_preview_parent_viewport->get_instance_id()` or an empty `ObjectID`.
- [ ] Replace the three raw object members in `EditorWorldFurniture` with:

```cpp
ObjectID preview_sun_id;
ObjectID preview_environment_id;
ObjectID preview_parent_viewport_id;
```

- [ ] Add typed, short-lived resolvers on `Node3DEditor`:

```cpp
DirectionalLight3D *_resolve_preview_sun(const EditorWorldFurniture &p_furniture) const;
WorldEnvironment *_resolve_preview_environment(const EditorWorldFurniture &p_furniture) const;
SubViewport *_resolve_preview_parent(const EditorWorldFurniture &p_furniture) const;
```

- [ ] Implement all resolvers with `ObjectDB::get_instance<T>(id)`. An empty or expired identity returns `nullptr`
      without logging.

### Update binding and furniture creation

- [ ] Update `bind_world()`, `_rebind_editor_world_furniture()`, and the Task 2 test accessor to resolve the viewport
      ID instead of retaining or returning a stored address.
- [ ] In `_ensure_world_furniture()`, resolve the recorded sun and environment into local variables before testing them.
- [ ] If either child identity expired and a valid preview parent is available, create a replacement child, attach
      it, and store its new identity.
- [ ] Never cache a resolved pointer in `EditorWorldFurniture` or across a deferred callback.

### Update parenting behavior

- [ ] In `_sync_preview_environment_parenting()`, resolve parent, sun, and environment into operation-local pointers.
- [ ] If a non-active world's parent identity is expired, skip it quietly.
- [ ] Only for the active edited world, use a live `EditorNode::get_scene_root()` as fallback and record its
      identity. If no live root exists, skip it.
- [ ] Never substitute another context viewport for an expired secondary-world parent.

### Update every remaining consumer

- [ ] Resolve before use in `_free_world_furniture()` and clear all three identities after live children/RIDs are
      released.
- [ ] Resolve before use in `_add_sun_to_scene()` and `_add_environment_to_scene()`.
- [ ] Resolve before use in `_sun_direction_draw()` and all preview-sun setters/undo-state capture.
- [ ] Resolve before use in `_preview_settings_changed()`, including its loop over all world-furniture entries.
- [ ] Resolve before use in any grid/furniture synchronization path discovered by the final search.
- [ ] Search until no removed raw field remains:

```sh
rg -n "preview_parent_viewport|preview_sun|preview_environment" editor/scene/3d/node_3d_editor_plugin.{h,cpp}
```

- [ ] Inspect every match. Allowed matches are IDs, resolver names, local variables, and object creation; no
      furniture or secondary-view struct may retain these objects as raw members.

### Verify the fix

- [ ] Run both focused cases:

```sh
python3 scripts/agent_build.py --backend ninja --test \
  --case "*3D preview context lifetime*" \
  --case "*Switching boards with a 3D scene open*"
```

- [ ] Confirm the lifetime test observes all three expired identities, survives forced synchronization/settings
      propagation without new errors, and removes furniture after deferred deletion.
- [ ] Confirm the existing board-switch workflow still renders and maintains count one.
- [ ] Run `git diff --check`, inspect all changed consumers, and commit with subject
      `Hold 3D preview furniture by identity`.

## Task 4: Mutation-check both protections

**Files:**

- Temporarily modify and then restore: `editor/scene/3d/node_3d_editor_plugin.h`
- Temporarily modify and then restore: `editor/scene/3d/node_3d_editor_plugin.cpp`
- Do not commit mutation changes.

### Identity mutation

- [ ] Temporarily reintroduce an address-backed secondary preview parent or make the resolver report the stale
      stored address while leaving the regression unchanged.
- [ ] Run:

```sh
python3 scripts/agent_build.py --backend ninja --test --case "*3D preview context lifetime*"
```

- [ ] Require a failure in the context-dead/tile-live assertion or forced-consumer phase. Save the exact failure
      output for the PR.
- [ ] Restore the identity implementation and rerun the case green.

### Accounting mutation

- [ ] Temporarily make the same-world branch call `_note_world_view_bound()` instead of `_note_world_view_rebound()`.
- [ ] Run:

```sh
python3 scripts/agent_build.py --backend ninja --test \
  --case "*3D preview context lifetime*" \
  --case "*Switching boards with a 3D scene open*"
```

- [ ] Require a count assertion failure above one or a furniture entry that survives final release. Save the exact
      failure output for the PR.
- [ ] Restore idempotent accounting and rerun both cases green.

### Confirm mutation cleanup

- [ ] Verify only intended implementation/test changes remain:

```sh
git diff --check
git status --short
```

- [ ] Compare against the pre-mutation commit and confirm no mutation-only raw pointer or unconditional increment
      remains.

## Task 5: Strict validation and handoff

**Files:** Review all files changed by Tasks 1–3.

### Final build and focused test

- [ ] Run the required native strict validation build:

```sh
python3 scripts/agent_build.py
```

- [ ] Run the focused regression with the strict-build binary:

```sh
./bin/foundry.* --headless test run \
  --case "*3D preview context lifetime*" \
  --case "*Switching boards with a 3D scene open*" \
  --force-colors
```

### Full suite

- [ ] Run the complete suite with structured progress:

```sh
DISPLAY=:1 ./bin/foundry.* --headless test run \
  --progress-format=jsonl \
  --progress-file /tmp/foundry-2082-test-progress.jsonl \
  --progress-heartbeat-seconds 30 \
  --force-colors
```

- [ ] Require the final doctest success summary. If cleanup reports leaked instances after that summary,
      distinguish known cleanup behavior from a test failure and report it accurately.

### Repository policy and diff review

- [ ] Run the applicable policy checks:

```sh
git grep -nE '\.split\("(void|bool|String|private fun|const val|def )' -- '*.py'
git grep -n "docs/superpowers" -- '*/test_*.py' '*/check_*.py'
git grep -n "hasattr(" -- '*/test_*.py'
git grep -l "assert" -- 'misc/checks/check_*.py'
git diff --check
```

- [ ] Confirm the new tests execute behavior and do not assert on source text, documentation prose, or the
      presence of another test.
- [ ] Confirm no production API, class-reference XML, grammar, renderer behavior, or board UI changed.
- [ ] Review `git diff develop...HEAD` for complete pointer-consumer coverage, test-only guards, invariant
      messages, and accidental unrelated edits.
- [ ] Commit any final corrections in a focused commit.

### PR evidence

- [ ] In the implementation PR, link #2082 and summarize the deferred lifetime cause, identity-based fix, and
      exact-count fix.
- [ ] Include commands and results for the strict build, both focused tests, and full suite.
- [ ] Include the actual failing output from both independent mutation checks.
- [ ] State explicitly that no visual/editor layout changed, so a review gallery is not required.
