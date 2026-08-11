# 3D Preview Context Lifetime Design

**Issue:** [#2082](https://github.com/cafecito-games/Foundry/issues/2082)
**Epic:** [#1970](https://github.com/cafecito-games/Foundry/issues/1970)
**Status:** Approved design

## Summary

A secondary `Node3DEditorViewport` can outlive the `EditorSceneContext` whose `SubViewport` hosts its
preview furniture. The context deletes that viewport immediately, while a board close deletes the
board, `ScenePaneTile`, and secondary 3D viewport later through a deferred callback. The surviving
3D view and its `EditorWorldFurniture` entry therefore retain addresses of deleted `Object`s.

The same binding path has a second defect: each repeated `bind_world()` call increments
`live_view_count`, even when the view remains bound to the same world. A single view can therefore
hold several logical references and prevent furniture cleanup after the view is released.

The fix replaces every cross-lifetime 3D preview-furniture address with `ObjectID`, resolves each
object through `ObjectDB` immediately before use, and makes view-to-world accounting exact and
idempotent.

## Investigation Result

The lifetime window is reachable. The relevant sequence is:

1. Board 1 displays a 3D scene.
2. Activating Board 2 and opening a scene demotes Board 1's tile, creating a secondary
   `Node3DEditorViewport` bound to Board 1's `World3D` and context viewport.
3. Closing Board 1 asks `EditorNode::_close_board_scenes()` to close its saved scenes.
4. `EditorData::remove_scene()` calls `scene_context_about_to_be_removed()` and deletes the
   `EditorSceneContext` synchronously.
5. `EditorNode::_finish_pending_board_close()` is deferred, so Board 1, its `ScenePaneTile`, and the
   secondary 3D viewport remain alive until a later message-queue turn.
6. The context destructor deletes its `SubViewport` and the preview sun/environment children
   parented beneath it. The secondary view's `Ref<World3D>` keeps the world and its furniture entry
   alive during the gap.

This disproves the possible higher-level ordering guarantee noted in the original issue. Manual
semantic editor automation reached the sequence on the current development binary without a crash,
so the invalid access remains allocator- and timing-sensitive. The regression must assert the
expired identities directly instead of depending on a process crash.

Repeated board switches also call `bind_world()` again for an existing secondary view. The current
implementation increments `live_view_count` on every call and decrements only once when the view is
released. The counter is therefore not a count of live views.

## Goals

- Make every 3D preview-furniture reference safe across independent object destruction.
- Keep exactly one furniture binding for each live secondary view and its current world.
- Make repeated same-world binding idempotent.
- Transfer a binding exactly once when a view changes worlds.
- Treat expired preview objects as expected teardown state without error spam or double deletion.
- Prove the real deferred board-close lifetime window with deterministic behavior assertions.
- Mutation-check the identity fix and the binding-count fix independently.

## Non-goals

- Registering 3D views with `EditorSceneContext`.
- Reordering board or scene-context destruction.
- Changing board UI or scene-close behavior.
- Changing 2D canvas-view lifetime handling from #2081.
- Changing renderer behavior, preview appearance, or preview settings.
- Adding scripting-language, grammar, or class-reference APIs.

## Design

### 1. Identity-only preview references

`Node3DEditorViewport` replaces its stored `SubViewport *preview_parent_viewport` with an
`ObjectID`. A private resolver returns
`ObjectDB::get_instance<SubViewport>(preview_parent_viewport_id)` or `nullptr` after destruction.
Keep the existing pointer-valued binding API for callers, but convert the pointer to an ID
immediately and never store the address.

`EditorWorldFurniture` stores only identities for its node objects:

- `preview_parent_viewport_id`
- `preview_sun_id`
- `preview_environment_id`

The raw `DirectionalLight3D *`, `WorldEnvironment *`, and `SubViewport *` members are removed. Small
private resolver helpers return typed pointers for the duration of one operation. No resolved
pointer is cached across a deferred call or retained in the furniture map.

This rule applies to every consumer, not only `_sync_preview_environment_parenting()`:

- furniture creation and recovery;
- parenting synchronization;
- preview-settings propagation across all worlds;
- sun-direction drawing and updates;
- undo-state capture for sun settings;
- duplicating the preview sun or environment into the edited scene; and
- furniture cleanup.

`_ensure_world_furniture()` resolves the recorded sun and environment identities before deciding
whether either object exists. If an identity has expired and the world has a valid live preview
parent again, it creates replacement furniture and records the new identities.

### 2. Parent resolution and fallback

Parent synchronization resolves the recorded parent identity on each pass. If it does not resolve:

- for a non-active world, synchronization skips that furniture entry quietly;
- for the active edited world only, synchronization uses a live `EditorNode::get_scene_root()` as
  the replacement parent and records its identity; if no live scene root exists, it skips the entry.

An expired secondary-world parent must never fall back to an unrelated context viewport. Missing
sun or environment identities are also expected after their former parent was deleted. Consumers
either recreate them through `_ensure_world_furniture()` when a live parent exists or skip them.

### 3. Exact view-to-world accounting

A secondary view owns at most one registered world binding.

`Node3DEditorViewport::bind_world()` follows this protocol:

1. Reject a null target world as it does today.
2. Compute the target parent `ObjectID`.
3. If the view is already registered with a different world, call `_note_world_view_unbound()` for
   the old world before changing `bound_world`.
4. Store the new world and parent identity and apply the world to the render viewport and gizmos.
5. If this is the first binding or the world changed, call `_note_world_view_bound()` exactly once.
6. If the world is unchanged, update/ensure that world's furniture and schedule parenting without
   incrementing `live_view_count`.

`release_secondary_viewport()` unregisters the view's current world exactly once before deleting
the view. Changing worlds therefore transfers one association; refreshing the same association
does not create another one.

`_note_world_view_unbound()` no longer hides underflow with `MAX(count - 1, 0)`. It uses an
`ERR_FAIL_COND_MSG` guard before decrementing when the furniture entry is missing or its count is
already zero. A successful decrement to zero immediately releases the world's furniture entry.

### 4. Cleanup behavior

`_free_world_furniture()` resolves each stored identity before acting:

- a live sun/environment is detached and deleted;
- an already-deleted sun/environment is absent and requires no action;
- renderer RIDs are freed when their servers are available; and
- all identities, flags, and count state are cleared before the map entry is erased.

Cleanup remains safe when the parent viewport deleted its children first. It must neither dereference
their former addresses nor attempt to delete them twice.

## Lifecycle

The fixed deferred-close sequence is:

1. Demoting the 3D tile registers one view for world W and records parent P's identity.
2. Repeated attachment refreshes for W update the parent identity but leave W's count at one.
3. Closing the board deletes P and its furniture children while the tile and secondary view survive.
4. P, the sun, and the environment stop resolving through `ObjectDB` immediately.
5. Deferred parenting/settings work observes missing objects and skips or recovers safely.
6. The deferred board deletion releases the secondary view's one association with W.
7. W's count reaches zero, so its furniture entry and renderer resources are removed.

If a view changes from W1 to W2 instead, W1 is decremented before W2 is incremented. W1 is released
if that was its last view; W2 ends with one association for the view.

## Error Handling and Invariants

- Expired parent, sun, and environment identities are normal teardown state and do not emit errors.
- A null world passed to a binding API remains a caller error.
- A missing furniture entry during a valid unbind, or an unbind at count zero, is an invariant
  failure rather than a silently clamped counter.
- Parent fallback is restricted to the active edited world.
- Deferred callbacks never retain resolved pointers between scheduling and execution.
- Cleanup tolerates objects that disappeared through parent ownership before furniture release.

## Regression Coverage

### Real-editor workflow

Add a `TESTS_ENABLED` workflow named `board_close_3d_context_lifetime` and a subprocess test named
`[Editor][Boards] 3D preview context lifetime survives deferred board close` alongside the existing
board/3D-switch coverage. Use the saved fixture scenes so no confirmation dialog interrupts the
close.

The workflow must:

1. Load the 3D fixture on Board 1.
2. Add Board 2, activate it, and load the 2D fixture.
3. Verify Board 1's demoted tile owns a secondary 3D viewport.
4. Switch between boards enough to exercise the existing same-world rebind path, return to Board 2,
   and assert the old world's `live_view_count` is still exactly one.
5. Capture identities for Board 1, its tile, its secondary view, its world, its context viewport,
   and its preview sun/environment.
6. Assert that the captured parent, sun, and environment resolve before the close and that both
   furniture nodes are parented to the captured context viewport. This proves the close will destroy
   the exact objects under test.
7. Call `close_board(0)`. The first call must return `false` because scene closing is in progress.
8. Before flushing the deferred board deletion, assert:
   - the board, tile, and secondary view identities still resolve;
   - the context viewport, preview sun, and preview environment identities no longer resolve;
   - the secondary view's resolved parent is `nullptr`; and
   - the world still reports exactly one live view.
9. Invoke the parenting synchronization and preview-settings propagation paths directly through
   `TESTS_ENABLED` accessors. Require process survival and no new editor errors.
10. Flush until the pending board close completes.
11. Assert Board 1, its tile, and its secondary view are gone and the old world's furniture entry no
    longer exists.

The test must fail if it never creates a secondary viewport or never observes the context-dead /
tile-live gap. Process survival alone is insufficient coverage.

### Test-only access

Add the minimum `TESTS_ENABLED` access needed to inspect observable runtime state:

- resolve a secondary view's preview-parent identity;
- read a world's live-view count;
- read/resolve a world's parent, sun, and environment identities;
- report whether a world-furniture entry exists; and
- invoke parenting synchronization and preview-settings propagation synchronously.

These accessors and the workflow registration exist only in test builds. They do not become
scripting or editor automation APIs in production binaries.

### Mutation checks

Perform and record two independent mutations while leaving the regression test intact:

1. Restore address-based parent/sun/environment behavior behind the test-facing accessors. The test
   must fail in the context-dead/tile-live gap because at least one expired object is reported as
   live or is consumed by forced synchronization.
2. Restore unconditional `live_view_count` increments for same-world `bind_world()` calls. The test
   must fail at the pre-close `live_view_count == 1` assertion and/or leave the furniture entry alive
   after the view is released.

Record the actual failing assertion or crash output for each mutation in the implementation PR.

No test may assert on source text.

## Files in Scope

- `editor/scene/3d/node_3d_editor_plugin.h`
- `editor/scene/3d/node_3d_editor_plugin.cpp`
- `editor/automation/editor_automation_acceptance_workflow.h`
- `editor/automation/editor_automation_acceptance_workflow.cpp`
- `editor/automation/editor_automation_workflow_registry.cpp`
- `tests/editor/test_editor_board_3d_switch.h`

Reuse the existing workflow fixture and 3D board-switch test header rather than duplicating either.

## Acceptance Criteria

- [ ] The regression observes an `EditorSceneContext` viewport destroyed while its secondary 3D
      viewport remains alive.
- [ ] `Node3DEditorViewport` does not retain a raw preview-parent viewport pointer.
- [ ] `EditorWorldFurniture` does not retain raw pointers to its parent, sun, or environment.
- [ ] Every furniture consumer resolves identities through `ObjectDB` immediately before use.
- [ ] Expired furniture identities are handled without crashes, invalid reads, double deletion, or
      editor error spam.
- [ ] Rebinding one view repeatedly to the same world leaves `live_view_count` at one.
- [ ] Rebinding a view to another world transfers one association from the old world to the new one.
- [ ] Releasing the last view removes the world's furniture entry.
- [ ] Counter underflow is reported as an invariant violation instead of being clamped.
- [ ] Both identity and counting mutations produce recorded regression failures.
- [ ] Focused tests, the native strict validation build, and the full suite pass.

## Verification

Fast iteration:

```sh
python3 scripts/agent_build.py --backend ninja --test --case "*3D preview context lifetime*"
```

Required native validation before handoff:

```sh
python3 scripts/agent_build.py
```

Focused regression on the validated binary:

```sh
./bin/foundry.* --headless test run --case "*3D preview context lifetime*" --force-colors
```

Full suite with structured progress:

```sh
./bin/foundry.* --headless test run \
  --progress-format=jsonl \
  --progress-file /tmp/foundry-2082-test-progress.jsonl \
  --force-colors
```
