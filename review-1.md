# Code Review 1

**Worktree:** `/workspace`
**Branch:** `csueiras/demoted-tile-preview-layout-dce7`
**Diff range:** `e657f61863ffc81e1c086d284b34b45c8bc0176d...HEAD` (commits `e944401a21`, `83e8b663f0`). Working tree and index are clean; no untracked files.
**Date:** 2026-08-10
**Scope requested:** adversarial — correctness, gap-memory races, persistence contract, focus/input, test gaps. Style ignored.

## Summary

The change adds a `presentation_hidden` override to `EditorTileDockRegion` that hides both dock columns as one atomic operation without touching stored side modes, drawer docks, tab selection, or remembered split widths, and wires it (plus rail visibility) into `ScenePaneTile::set_preview_mode()` so demoted `LIVE_2D`/`LIVE_3D` tiles become preview-only.

The gap-memory mechanism itself is sound. I traced every hide/show ordering against `SplitContainer::_add_valid_child` / `_remove_valid_child` / `_set_desired_sizes` and the remembered-by-identity restore is correct in all four column-visibility combinations, including the tricky "one side already collapsed" case that `_set_desired_sizes` re-aliases. The persistence contract in `save_layout()` also holds: nothing the override touches is read back into the config.

The problem is not inside `EditorTileDockRegion`. It is that the *promotion* half of the state machine is hung off a call site that is conditional in two independent places, so `presentation_hidden` can get stuck `true` on a focused tile — and once it is stuck, the rail shortcut writes corrupted side state that `save_layout()` then persists. There is also a new mid-drag interaction with `Viewport::_gui_hide_control()`. Several of the new assertions are structurally incapable of failing.

## Findings

### Critical

#### C1. `presentation_hidden` can get permanently stuck `true` on a focused tile, leaving it with no docks, no rails, and no way back

`ScenePaneTile::set_preview_mode()` (`editor/editor_scene_pane_tile.cpp:234-241`) is now the *only* writer of `presentation_hidden` and rail visibility. Its only production caller is `EditorNode::_update_tile_display_attachments()` (`editor/editor_node.cpp:5373-5486`), which is guarded twice against ever visiting a tile with no current scene:

```cpp
// editor/editor_node.cpp:5379-5393
for (int i = 0; i < editor_data.get_edited_scene_count(); i++) {
    ...
    const bool is_tile_current = tile && editor_data.get_tile_current_scene(tile_id) == i;
    if (!is_tile_current) {
        ...
        continue;                       // scene-less tile is never visited at all
    }
```

and `_focus_tile_internal()` returns before it ever reaches `_set_current_scene_nocheck()`:

```cpp
// editor/editor_node.cpp:7988-7993
if (scene_idx < 0) {
    ...
    return;                             // focusing an empty tile never re-promotes it
}
```

So the invariant "`presentation_hidden` is a pure function of focus" is never enforced; it is only *written* on a path that skips scene-less tiles.

**Reachable repro.** A scene pane that also carries a script tab (allowed — `EditorSceneWorkspace::handle_tab_drop` only rejects a *scene* tab onto a non-scene pane, `editor/editor_scene_workspace.cpp:200-205`):

1. Split so tile A is demoted (`set_preview_mode(LIVE_*)` → `presentation_hidden = true`, both rails hidden).
2. Drag tile A's last **scene** tab onto tile B. `handle_tile_tab_drop` focuses the *destination* (`editor/editor_node.cpp:8090-8094`), not the source.
3. `_collapse_if_empty()` does not collapse tile A because `pane->get_tab_count() != 0` — the script tab is still there (`editor/editor_scene_workspace.cpp:114`).
4. Tile A now has `get_tile_current_scene() < 0`. `_update_tile_display_attachments()` skips it forever; focusing it hits the `scene_idx < 0` early return.

Result: a focused, on-screen tile with the scene tree dock hidden, the inspector tab stack hidden, and both side rails hidden. There is no UI affordance to recover — the rail buttons are gone, and the `docks/toggle_left_tile_rail` shortcut routes through `toggle_side_mode()` → `_apply_side()` → `_update_side_visibility()`, whose `presentation_hidden` branch (`editor/editor_tile_dock_region.cpp:242-260`) forces the columns away regardless. The user must drop a scene back into the tile.

Before this change the same stale-mode bug existed but was cosmetic: it only left a hidden `preview_container`, and the docks stayed up. This change turns it into a dead tile.

**Fix.** Make promotion unconditional rather than a side effect of the per-scene loop. Minimum:

```cpp
// editor/editor_node.cpp, in _focus_tile_internal, before the scene_idx < 0 return
tile->set_preview_mode(TilePreviewMode::FOCUSED_LIVE);
```

Better: in `_update_tile_display_attachments()`, walk every tile on every board once up front and reset any tile that is not the demotable set (scene-less tiles and the focused tile) to `FOCUSED_LIVE`, then run the existing per-scene loop. That also removes the dependence on scene-index bookkeeping for a purely presentational property.

#### C2. Composed with C1: the rail shortcut on a stuck-hidden tile writes corrupted side state, and `save_layout()` persists it

`_shown_dock_index()` (`editor/editor_tile_dock_region.cpp:186-212`) reads *live control visibility* — `right_tabs->is_visible()` for the right side, `docks[i]->is_visible()` for the left. Under `presentation_hidden` both are forced false, so `_shown_dock_index()` returns `-1` for both sides. Nothing guards the three entry points that feed it into a pure-state transition:

- `press_rail_toggle()` — `editor/editor_tile_dock_region.cpp:397`
- `close_drawer()` — `editor/editor_tile_dock_region.cpp:407`
- `toggle_side_mode()` / `set_side_mode()` — `editor/editor_tile_dock_region.cpp:412, 420`

Concretely, `EditorNode::_toggle_focused_tile_rail()` (`editor/editor_node.cpp:8265-8273`) → `toggle_side_mode()` → `side_rail_toggle_mode()` (`editor/gui/side_rail_state.h:238-250`):

```cpp
next.drawer_dock = p_state.last_drawer_dock >= 0 ? p_state.last_drawer_dock : p_shown_dock;
```

With `p_shown_dock == -1` and no `last_drawer_dock`, a `DOCKED → RAILED` toggle produces `drawer_dock = -1` (railed with a *closed* drawer) instead of railed-open-on-the-shown-dock. `save_layout()` then writes `tile_rail_left = true` with `tile_drawer_dock_left = ""` (`editor/editor_tile_dock_region.cpp:549-554`), permanently losing the drawer dock. `side_rail_close_drawer()` (`side_rail_state.h:185-194`) likewise fails to record `last_drawer_dock` from a `DOCKED` side, breaking the documented "toggling the mode twice lands back where it started" property.

Today this is only reachable when a `presentation_hidden` tile is also the focused tile — i.e. exactly the state C1 creates. But the header advertises `set_presentation_hidden` as leaving stored state untouched (`editor/editor_tile_dock_region.h:135-137`) and nothing enforces that against these three callers.

**Fix.** Either guard the transitions:

```cpp
void EditorTileDockRegion::toggle_side_mode(Side p_side) {
    ERR_FAIL_COND_MSG(presentation_hidden, "Side transitions are not defined while presentation-hidden.");
    ...
}
```

or, preferably, make `_shown_dock_index()` independent of the override so the transitions stay well-defined:

```cpp
int EditorTileDockRegion::_shown_dock_index(Side p_side) const {
    if (presentation_hidden) {
        // Report what the side *would* show, so state transitions taken while
        // the columns are hidden produce the same result as when visible.
        const Vector<EditorDock *> docks = get_side_docks(p_side);
        const SideRailSideVisibility visibility = side_rail_side_visibility(_pure_state(p_side), docks.size());
        ...
    }
    ...
}
```

Add a test that toggles a side mode while `presentation_hidden` and asserts the resulting stored state matches the same toggle performed with the columns visible.

### High

#### H1. Hiding the dock columns mid-drag synthesizes a mouse-button release into the drag source

`ScenePaneTile::input()` implements focus-follows-drag (`editor/editor_scene_pane_tile.cpp:179-188`): while `gui_is_dragging()`, moving the cursor over a tile focuses it. That focus change eventually runs `_update_tile_display_attachments()`, which demotes the *previously* focused tile and now hides its dock columns.

`Control::_notification(NOTIFICATION_VISIBILITY_CHANGED)` → `Viewport::_gui_hide_control()` (`scene/main/viewport.cpp:2522-2529`):

```cpp
if (gui.mouse_focus == p_control) {
    _drop_mouse_focus();
}
if (gui.key_focus == p_control) {
    gui_release_focus();
}
```

and `_drop_mouse_focus()` (`scene/main/viewport.cpp:2724-2745`) fabricates a `pressed = false` `InputEventMouseButton` and dispatches it into the control. The visibility notification propagates to every `Control` descendant, so the `Tree` inside `SceneTreeDock` — the actual drag source — receives this.

Sequence: drag a node out of tile A's scene tree, move the cursor over tile B → tile B is focused → tile A is demoted → tile A's `SceneTreeDock` (and the `Tree` holding `gui.mouse_focus`) is hidden → synthetic release into a hidden control while `gui.dragging` is still set. Before this change tile A's docks stayed visible and nothing was hidden mid-drag.

Note that the existing deferral in `_on_leaf_focus_requested` (`editor/editor_node.cpp:8025-8030`, and `_queue_focus_tile_activation` at `8017`) does *not* save you here: it defers by one process frame, and a drag spans many frames.

**Verify this** by dragging a node from one tile's scene tree into a sibling tile and checking whether the drop lands and whether errors appear. If it reproduces, suppress the chrome transition while a drag is in flight:

```cpp
// ScenePaneTile::set_preview_mode
Viewport *vp = get_viewport();
if (preview_only && vp && vp->gui_is_dragging()) {
    // Defer the chrome hide until the drag resolves; hiding the drag source's
    // dock mid-drag drops mouse focus and fabricates a release into it.
    callable_mp(this, &ScenePaneTile::_apply_preview_chrome).call_deferred(preview_only);
    return;
}
```

#### H2. `set_presentation_hidden()` notifies side-changed observers on restore but not on hide

```cpp
// editor/editor_tile_dock_region.cpp:299-318
if (p_hidden) {
    _sync_remembered_gaps();
    presentation_hidden = true;
    _update_side_visibility(Side::LEFT);
    _update_side_visibility(Side::RIGHT);
    _reapply_gaps();
    return;                      // <-- no _notify_side_changed
}
presentation_hidden = false;
...
_notify_side_changed(Side::LEFT);
_notify_side_changed(Side::RIGHT);
```

The asymmetry only happens to be invisible because the single production observer — `EditorSideRailStrip::_update_active_states()` (`editor/gui/editor_side_rail_strip.cpp:154-176`) — is itself hidden by `ScenePaneTile` immediately afterwards. It leaves the rail's toggle buttons in a stale `pressed` state and its close button at a stale position for the whole demoted period. It also makes the new `is_dock_shown()` override (`editor/editor_tile_dock_region.cpp:214-217`) effectively unobservable in production: the only consumer is never asked while hidden.

This is a contract bug waiting for the second consumer. Either notify on both edges, or document in the header that the hide edge is deliberately silent because the caller is responsible for hiding the mirrors. As written, `editor/editor_tile_dock_region.h:135-139` says neither.

#### H3. `_reapply_gaps()` is called unconditionally, contradicting the documented invariant it was written for

`_apply_side()` (`editor/editor_tile_dock_region.cpp:359-373`) carries an explicit comment that re-deriving gaps is only correct/necessary "when a body column's visibility actually changed" (`editor/editor_tile_dock_region.h:93-95`), and guards accordingly:

```cpp
if (_update_side_visibility(p_side)) {
    _reapply_gaps();
}
```

`set_presentation_hidden()` calls `_reapply_gaps()` unconditionally on both edges and discards both `_update_side_visibility()` return values. The `presentation_hidden` branch of `_update_side_visibility()` even computes `changed` for the left side (`editor/editor_tile_dock_region.cpp:240, 247-253`) and then has its return value thrown away.

I traced this and it is *currently* benign — when nothing changed, `_reapply_gaps()` resolves the same gap map and writes back the same values, and `SplitContainer::set_split_offsets` early-returns on an equal array (`scene/gui/split_container.cpp:1170-1172`). But the codebase treats "only re-derive on a real visibility change" as a load-bearing rule with a long comment explaining why, and this is the one call site that ignores it. Either honor the return values:

```cpp
bool changed = _update_side_visibility(Side::LEFT);
changed = _update_side_visibility(Side::RIGHT) || changed;
if (changed) {
    _reapply_gaps();
}
```

or add a comment at `editor/editor_tile_dock_region.cpp:308` explaining why the unconditional call is required here and the guard elsewhere is not.

### Medium

#### M1. A demoted tile's split widths are no longer persisted from live offsets, only from `has_remembered_gap`

`save_layout()` (`editor/editor_tile_dock_region.cpp:538-547`) writes the live offset when the gap exists and otherwise falls back to the remembered one — and skips the key entirely when nothing is remembered. While `presentation_hidden`, both gaps are always `ABSENT`, so a demoted tile's widths come exclusively from `has_remembered_gap[]`.

`_sync_remembered_gaps()` (`editor/editor_tile_dock_region.cpp:320-334`) only sets that flag when the gap index is in range:

```cpp
if (gap_map.left_center != TileDockGapMap::ABSENT && gap_map.left_center < offsets.size()) {
```

`SplitContainer::split_offsets` starts as a single-element array (`scene/gui/split_container.cpp:1427`) and is only grown by `_set_desired_sizes` / `_update_dragger_positions`, both of which require the container to have been laid out. If a tile is demoted before the body's first layout pass — plausible at startup, where the workspace is built, tiles are created, and `_update_tile_display_attachments()` runs — `gap_map.center_right == 1` while `offsets.size() == 1`, so `has_remembered_gap[GAP_CENTER_RIGHT]` stays `false`. A layout saved while that tile is still demoted then omits `tile_dock_hsplit_2` entirely and the user's right dock width is lost on the next load.

Pre-change this could not happen, because a demoted tile's columns were still visible and `save_layout()` wrote live offsets.

Verify by instrumenting `_sync_remembered_gaps()` for `has_remembered_gap[i] == false` after a startup demotion. Defensive fix: when the gap exists in the map but the offsets array is short, derive the width from the child's current size instead of skipping, or resize/normalize the offsets array before reading.

#### M2. `TileChromeSnapshot::split_offsets` is captured and never compared — gap restoration has no end-to-end coverage

`editor/automation/editor_automation_acceptance_workflow.cpp:251, 277` capture the body's split offsets into the snapshot. `_assert_chrome_restored()` only checks the array *size*:

```cpp
// editor/automation/editor_automation_acceptance_workflow.cpp:403
if (after.left_column_visible && after.right_column_visible && after.split_offsets.size() != 2) {
```

The values are never read. So the whole point of the gap-identity machinery — "the dock width you had before demotion is the width you get back" — has no coverage in a real laid-out workspace. The unit test at `tests/editor/test_side_rail_collapse.h:1090-1095` does compare offsets, but against a `CollapseFixture` body that is added to the scene root and never sized or pumped (`tests/editor/test_side_rail_collapse.h:74-101`), so it exercises the bookkeeping arithmetic on a zero-width container, not real layout.

Add a workspace-level assertion: set an explicit dock width on a laid-out tile, demote, promote, and assert the restored offset for the surviving gap. Comparing raw pre-split values is wrong (the split itself resizes the tile), so snapshot the offsets *after* the split and immediately before demotion.

#### M3. `set_dock_enabled()` on demoted tiles is a real production path with only vacuous coverage

`EditorNode::_feature_profile_changed()` iterates every tile on every board (`editor/editor_node.cpp:10335-10341, 10354-10360`) and calls `set_signals_dock_enabled` / `set_groups_dock_enabled` / `set_history_dock_enabled`, which route into `EditorTileDockRegion::set_dock_enabled()`. Demoted tiles are included.

The new test tries to cover this (`tests/editor/test_side_rail_collapse.h:1057`, asserted at `1072`):

```cpp
fixture.region.set_dock_enabled(fixture.inspector, false);
...
CHECK_FALSE(fixture.region.is_dock_shown(fixture.inspector));
```

but the assertion cannot fail in either parametrization. In `order == 0` the right side is `RAILED` with the *signals* drawer, so the inspector is not shown regardless of enablement. In `order == 1` the right side is `DOCKED` with *groups* as the current tab, so `is_dock_shown()` returns false on the current-tab check at `editor/editor_tile_dock_region.cpp:232-234` regardless of enablement. Disable the dock that *is* currently shown (the drawer dock in `order == 0`, the current tab in `order == 1`) so the assertion has something to prove — and separately assert that re-*enabling* while hidden materializes on restore, which nothing covers.

#### M4. The workspace-level width assertion falls back to a measurement that includes the dock columns

```cpp
// tests/editor/test_side_rail_collapse.h:1146-1159
real_t preview_width = p_tile->get_content_host()->get_size().x;
if (preview_width <= 0.0 && p_tile->get_dock_region()->get_body()) {
    preview_width = p_tile->get_dock_region()->get_body()->get_size().x;
}
...
CHECK(preview_width >= tile_width * 0.8);
```

`body` is the `HSplitContainer` that *contains* both dock columns, so when the fallback fires the assertion measures the whole split, not the preview, and would pass with both columns fully visible. Given the harness explicitly sizes the host and pumps twice (`tests/editor/test_side_rail_collapse.h:1129-1132`), the fallback should be a failure, not a silent substitution:

```cpp
const real_t preview_width = p_tile->get_content_host()->get_size().x;
REQUIRE(preview_width > 0.0); // Layout must have run for this assertion to mean anything.
CHECK(preview_width >= tile_width * 0.8);
```

#### M5. The acceptance workflow's window constraint is unbounded above, weakening the geometry assertion

```cpp
// editor/automation/editor_automation_acceptance_workflow.cpp:2524-2530
root_window->set_size(constrained_size);          // 1600x900
p_driver.flush_frames(10);
if (root_window->get_size().x < constrained_size.x / 2) { ... }
```

The guard only rejects sizes below 800px; it accepts anything larger. The `surface_width < tile_width * 0.8` check at `editor/automation/editor_automation_acceptance_workflow.cpp:355` is width-sensitive: with both dock columns visible the chrome costs roughly `180 + 180 + rails ≈ 400px`, so at a tile width above about 2000px a regression would still clear the 80% threshold and the assertion would pass. The step comment ("Narrow enough that ... collapses its preview surface") describes an intent the guard does not enforce.

The discriminating checks are the explicit `left_column_visible` / `right_column_visible` assertions in `_assert_preview_only_chrome()` (`editor/automation/editor_automation_acceptance_workflow.cpp:340-347`), which is fine — but then either tighten the guard to require the actual size to match the request in both directions, or restate the geometry check in absolute terms (`surface_width >= tile_width - rail_allowance`) so it does not silently degrade with window size.

#### M6. The workflow permanently resizes the editor window and never restores it

`root_window->set_size(constrained_size)` at `editor/automation/editor_automation_acceptance_workflow.cpp:2524` has no counterpart at the end of the workflow. For `tests/editor/test_editor_passive_scene_preview.h` this is harmless: the workflow runs in a dedicated `--automation-run-workflow` subprocess against a disposable project copy.

But the workflow is also registered for interactive invocation (`editor/automation/editor_automation_workflow_registry.cpp:161`), and `handle_tile_scene_drop` triggers `save_editor_layout_delayed()` (`editor/editor_node.cpp:8146`), while `_save_window_settings_to_config()` persists `w->get_size()` (`editor/editor_node.cpp:8545-8551`). Running this workflow through MCP against a real project will shrink the user's editor window to 1600x900 and persist it. Capture the size before and restore it before returning (including on every early-return failure path — a `defer`-style RAII guard is the only maintainable way given the ~15 `return _failure_with_message(...)` sites).

### Low / Nitpicks

- **`ScenePaneTile::preview_mode` is dead outside tests.** `editor/editor_scene_pane_tile.h:105, 184`. The obvious use — an early-out on `preview_mode == p_mode` — is *wrong* here, because `EditorNode` calls `set_preview_mode(LIVE_2D)` before `canvas_view` exists and relies on the later re-entry recomputing `show_canvas_view` (`editor/editor_node.cpp:5456-5467`; same for `LIVE_3D` at `5414-5425`). Add a one-line comment at `editor/editor_scene_pane_tile.cpp:209` saying so, or the next reader will "optimize" it into a stale-preview bug.

- **`CHECK_FALSE(during_config->has_section_key(section, "tile_presentation_hidden"))`** (`tests/editor/test_side_rail_collapse.h:1054`) asserts the absence of a key name that appears nowhere in the codebase. It can never fail. Either delete it or assert on the full key set actually written by `save_layout()`.

- **Config comparisons silently pass when both keys are absent.** `tests/editor/test_side_rail_collapse.h:1051-1053` do `int(during->get_value(k)) == int(before->get_value(k))`; a missing key yields `nil`, `int(nil) == 0`, and `0 == 0` passes. Precede each with `REQUIRE(before_config->has_section_key(section, k))`.

- **The "one side already collapsed at demotion" case is not exercised.** Both parametrizations of the region test leave *both* columns visible when `set_presentation_hidden(true)` runs: `order == 0` is left `DOCKED` + right `RAILED`-open-on-signals, `order == 1` is left `RAILED`-open-on-scene + right `DOCKED` (`tests/editor/test_side_rail_collapse.h:985-1013`). The `_sync_remembered_gaps()`-before-hide comment at `editor/editor_tile_dock_region.cpp:300-303` exists precisely for the asymmetric case where one column is already gone and its remembered gap must survive from an earlier `_apply_side()`. Add a third parametrization using `close_drawer()` without a follow-up `press_rail_toggle()` on one side.

- **Hard-coded leaf ids in the workflow.** `workspace->get_tile_by_id(1)` at `editor/automation/editor_automation_acceptance_workflow.cpp:2612` and `2784` assume the post-split leaf id. Resolve through `editor_data.get_focused_tile_id()` / `workspace->get_focused_leaf()` instead so a change to leaf-id allocation fails loudly rather than silently testing the wrong tile.

- **`configure_focused_2d_tile_chrome` bypasses the region.** `right_tabs->set_current_tab(groups_tab)` at `editor/automation/editor_automation_acceptance_workflow.cpp:2622-2625` sets tab selection directly rather than through `EditorTileDockRegion::focus_dock()`. The state happens to stay consistent (the region reads selection back from the `TabContainer`), but the setup exercises a different path than production.

- **No review gallery.** Per `AGENTS.md`, editor UI work whose correctness is judged visually should ship a captioned `scripts/review_gallery.py` board. This change alters what every demoted tile looks like, including every tile in the board-overview filmstrip, and there is nothing showing that.

## Test Coverage

**What is genuinely covered.** The region-level state-preservation contract (modes, drawer docks, tab selection, `save_layout` keys) across a hide/show cycle, idempotent re-entry on both edges, offset restoration by gap identity (on a zero-sized body), and a workspace-level demote/promote cycle across two tiles with asymmetric chrome. The acceptance workflow now asserts preview-only chrome and post-promotion restoration in a real editor process, which is the right shape for this feature.

**What is missing.**

1. No coverage of `set_preview_mode()` on a tile with no current scene — the exact hole C1 falls through. A regression test belongs at the `EditorNode` level: demote a tile, clear its current scene, focus it, assert `is_presentation_hidden() == false`.
2. No coverage of side-mode transitions taken while `presentation_hidden` (C2). This is the assertion that would have caught the `_shown_dock_index() == -1` hazard.
3. No end-to-end assertion that a *width* survives demote/promote in a laid-out workspace (M2).
4. No coverage of dock enable/disable while hidden that can actually fail (M3).
5. No coverage of demotion while a side is already collapsed closed (Low, above).
6. Three of the new assertions are structurally incapable of failing: the `tile_presentation_hidden` absence check, the inspector `is_dock_shown` check, and the `body`-width fallback in `assert_preview_only`.

## Overall Assessment

**Request changes.** The core `EditorTileDockRegion` change is well-reasoned and the gap-memory handling — the part I expected to be broken — holds up under a line-by-line trace against `SplitContainer`'s valid-child bookkeeping. The persistence contract for `save_layout()` is intact.

The blocker is C1: hanging a *presentational* invariant off `_update_tile_display_attachments()`, whose per-scene loop and whose `_focus_tile_internal()` caller both skip scene-less tiles, produces a reachable dead-tile state, and C2 turns that dead state into persisted side-state corruption. Both are `EditorNode`-side, not region-side. Fix C1 by making promotion unconditional, fix C2 by either guarding or well-defining the three transitions under the override, and verify H1 by hand with a cross-tile node drag.

The new tests should be tightened before merge — as written, four of them would not fail against a broken implementation.
