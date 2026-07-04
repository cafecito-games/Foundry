# Review: multi-scene split-pane workspace branch

Branch: `csueiras/multi-scene-phase-c-split-pane-fed5`
Scope reviewed: `294a70bf08` (Multi-scene phase C) through `e383ff89a3` (Fix split-pane scene viewport state) — i.e. the split-pane feature plus its 7 follow-up "Fix…" commits. The `753ed5e04f` automation PR is a separate, already-merged change and was **not** reviewed here.

Method: read the core new files (`editor_scene_workspace.*`, `editor_scene_context.*`) and the central `editor_node.cpp` reparenting/scene-switch logic directly, plus four focused deep-review passes over `editor_node.cpp`, `editor_data.cpp`/`editor_scene_context.cpp`, `scene_tree_editor.cpp` + the two engine files, and the docks + `editor_scene_tabs.cpp`. Findings below are marked **confirmed** (I traced it in the code) or **plausible** (strong by inspection, needs a runtime repro).

---

## TL;DR

The feature works but carries a visible sediment of failed debugging attempts. Three themes:

1. **Dead code left behind by superseded approaches** — most notably `EditorSceneContext::activate()` (no production callers anymore) and an entire dock "click-to-focus-pane" mechanism that is effectively inert.
2. **Layout/timing band-aids that fight each other** — the `_ensure_split_offset()` re-centering deferred call actively *clobbers* the restored split position on session load; the main screen is an absolute-positioned overlay manually re-fitted on every resize.
3. **The last commit (`e383ff89a3`) is a scatter of defensive guards** that patch symptoms (nodes detaching, paths going stale, viewports rendering at the wrong size) rather than the root causes. The two **core-engine** changes it makes are necessary *given the chosen design*, but they compensate for editor-side gaps and one of them slightly changes behavior for user projects.

The single thing I'd fix regardless of everything else: the `update_all_scene_tabs()` self-recursion (H2) and the split-offset restore clobber (H3).

---

## High-value findings

### H1 — `EditorSceneContext::activate()` is dead production code (superseded by `set_display_parent()`) — confirmed
`editor/editor_scene_context.cpp:85-106`. Grep shows the only callers of `activate()` are tests (`test_editor_scene_context.h`, `test_dock_scene_context_binding.h`). In production, `EditorNode` only ever calls `deactivate()` and `set_display_parent()`. `set_display_parent()` (added later) duplicates `activate()`'s job — reparent the viewport, set `active = true`, restore the selection — but via a different code path (`get/set_selected_node_ids` round-trip vs. draining `retained_selection_ids`). This is a classic iteration artifact: `activate()` was the original mechanism, `set_display_parent()` replaced it, and `activate()` was never removed. It now survives only because its own tests keep it compiled. **Recommend:** delete `activate()` and its tests, or if the retained-selection-drain semantics are worth keeping, fold them into `set_display_parent()` so there is one reparent path.

### H2 — `update_all_scene_tabs()` contains an infinite self-recursion — confirmed
`editor/editor_node.cpp:4849-4855`:
```cpp
void EditorNode::update_all_scene_tabs() {
    if (!scene_workspace) {
        if (scene_tabs) {
            update_all_scene_tabs();   // <-- recurses into itself, unbounded
        }
        return;
    }
    ...
```
The no-workspace fallback calls itself instead of `scene_tabs->update_scene_tabs();`. It is *currently* unreachable (whenever `scene_workspace` is null, `scene_tabs` is also null, since `scene_tabs` is derived from the workspace), so the whole inner block is dead — but if that path ever becomes reachable it is a stack overflow. Copy/paste bug; the inner block should either call `scene_tabs->update_scene_tabs();` or just be deleted.

### H3 — Restored split offset is clobbered to 50/50 on session load — confirmed
`editor/editor_node.cpp:6740-6743` restores the saved split offset:
```cpp
_split_workspace(get_saved_split_vertical(...));                 // schedules deferred _ensure_split_offset()
scene_workspace->get_split()->set_split_offset(get_saved_split_offset(...));  // synchronous
```
But `split_workspace()` schedules `_ensure_split_offset()` via `call_deferred()` (`editor_scene_workspace.cpp:283`), and that method does `set_split_offset(0); clamp_split_offset(0);` (`editor_scene_workspace.cpp:233-234`), which forces the dragger back to its default center. The deferred call runs *after* the synchronous restore at 6742, so the user's saved split position is overwritten with 50/50 every load. There is no test covering split-offset restore, so this is unguarded. The `_ensure_split_offset()` centering is itself a leftover: a fresh `SplitContainer` with two equal-stretch expand-fill children already centers at offset 0, so the forced re-center is close to a no-op *except* for the damage it does to restore. **Recommend:** remove `_ensure_split_offset()` (and its one-shot `resized` reconnect) entirely, or make it only run for the initial split (never during restore).

### H4 — The dock "click to focus its pane" mechanism is effectively inert — confirmed
Two follow-up commits (`c08f5fec9f` "Connect pane-focus handlers on dock content roots", `70151253d9` "Fix crash when focusing empty pane") were spent wiring this, and it still doesn't work for the common case:

- `_dock_focus_entered` never fires. `scene_tree_dock.cpp:5145` / `inspector_dock.cpp:947` connect `focus_entered` on `main_vbox`/`main_vb`, but those are plain `VBoxContainer`s whose `focus_mode` defaults to `FOCUS_NONE`, and a `FOCUS_NONE` control never emits `focus_entered`. Pure dead weight.
- The `gui_input` handler on the same container only receives events its children didn't consume. The scene `Tree`, `EditorInspector`, filter fields, and buttons are all `MOUSE_FILTER_STOP`, so clicking actual dock content never reaches the container. Only clicks in the narrow empty margins focus the pane.

Net effect: clicking a node in the *secondary* pane's scene tree does **not** make that pane focused, so a subsequent add-node / inspector edit routed through the focused-singleton can target the wrong pane. Either wire this to the leaf controls (or route through the existing `EditorScenePane` focus plumbing), or delete both handlers and `owning_pane`/`get_owning_pane()` (the latter is an unused accessor: `scene_tree_dock.h:344`, `inspector_dock.h:178`).

### H5 — `EditorScenePane` has five overlapping paths to `_request_focus()` — confirmed
`editor/editor_scene_workspace.cpp:136-192`. `setup()` connects `gui_input` on the pane, on `scene_tabs`, and on `content_host`, plus `focus_entered` on the pane, *and* the pane overrides `input()` with `set_process_input(true)` doing a global hit-test on every input event. That is five routes to the same `request_pane_focus()`. The `input()` override in particular runs for **every** pane on **every** global input event and re-does a global-rect hit test — redundant with the `gui_input` connections and wasteful. This reads as accumulated attempts to make focus-on-click work (see H4 — the pane-level plumbing is why margin clicks focus at all). Collapse to one mechanism.

---

## Medium findings

### M1 — `set_edited_scene()` writes the wrong pane's "current" and can break the pane/current invariant — plausible
`editor/editor_data.cpp:814-820` sets `current_edited_scene = p_idx` and `pane_current_scenes[edited_scene[p_idx].pane] = p_idx`, without consulting `focused_pane`. When the scene being set belongs to a non-focused pane, this both leaves the invariant `current_edited_scene == pane_current_scenes[focused_pane]` (asserted elsewhere) broken and silently rewrites the non-focused pane's current scene. The disk-reload / save-all loops (`editor_node.cpp` around 1566-1587, 7628, 7656, 7693, 8029) call `set_edited_scene(i)` across both panes; after such a loop pane 1's current can be left pointing at whatever scene the loop last touched. Worth a targeted test of "reload-from-disk with a split workspace, focus stays on pane 0".

### M2 — `transfer_scene_to_pane()` computes the drop index *after* the scene already moved into the target pane — confirmed (mis-drop), plausible (severity)
`editor/editor_node.cpp:5050-5061`. `set_scene_pane(p_scene_idx, p_target_pane)` (5051) runs first, so `get_pane_scene_indices(p_target_pane)` (5053) already contains the moved scene. `global_target = target_scenes[p_target_tab]` can therefore resolve to the moved scene's own index, making the subsequent `move_edited_scene_to_index()` a no-op or off-by-one versus the visual drop slot. Repro: with 2+ scenes already in the destination pane, drag a tab from the other pane and drop it at a specific slot — it can land in the wrong position. Capture the target global index *before* `set_scene_pane`, or account for the +1.

### M3 — Dangling/again-null singletons during unsplit teardown — confirmed (window exists), plausible (that anything derefs in it)
`editor/editor_node.cpp:4980-4982`: `_destroy_secondary_docks()` → `scene_workspace->unsplit_workspace()` (which `memdelete`s pane 1 and its `EditorSceneTabs`) → `focus_pane(0)`. `EditorSceneTabs` keeps an `inline static` singleton but has **no destructor to clear it** (`editor_scene_tabs.h:47`), so if pane 1 was focused, `EditorSceneTabs::get_focused_singleton()` points at freed memory for the window between `unsplit_workspace()` and `focus_pane(0)`. The docks have the same shape but self-heal via `if (singleton == this) singleton = nullptr` destructors, leaving `get_singleton()` transiently null instead of dangling. This is the class of bug behind the "crash when focusing empty pane" history; the ordering is load-bearing and undocumented. Give `EditorSceneTabs` a destructor that nulls its singleton, and restore singletons before deleting.

### M4 — Double full scene-switch on cross-pane tab activation — confirmed
`editor/editor_node.cpp:5012-5020` (`on_pane_tab_changed`): `focus_pane(p_pane)` switches to the pane's *previous* current scene (a full context switch — viewport reattach, plugin-state save/restore, folding save), then `_set_current_scene(scene_idx)` immediately switches again to the actually-clicked tab. Clicking a tab in a non-focused pane pays for two full activations. `move_scene_to_other_pane()` (5026-5039) has the same shape (`focus_pane` + redundant `set_pane_current_scene` + `_set_current_scene`). Not incorrect, but wasteful and a likely iteration leftover.

### M5 — `changing_scene` is now cleared both synchronously and via the deferred `_set_main_scene_state` — confirmed, needs a decision
The branch adds `changing_scene = false;` at `editor_node.cpp:4689`, but `_set_main_scene_state` still clears it too (≈4573) and runs deferred. This changes the semantics of `is_changing_scene()` during the deferred window (guarded reads such as the main-screen auto-switch suppression at ≈3256). It may be a *needed* fix (the deferred `_set_main_scene_state` is skipped during restore / pending tab closes, which could otherwise leave the flag stuck true), but the two resets are now redundant and the timing change is unvetted. Decide deliberately rather than leaving both.

### M6 — `subviewport_container.cpp add_child_notify()` compensates for a missing editor re-layout, and changes normal-project behavior — confirmed
`scene/gui/subviewport_container.cpp:256-273`. The container normally sizes/sets-update-mode on its child viewports only from its own `ENTER_TREE`/`VISIBILITY_CHANGED`/`RESIZED` notifications — none of which fire when the split-pane code reparents the *already-live* editing SubViewport into a different, already-sized container. So this new code re-does that work at add-time. Two caveats: (a) it's papering over the editor never calling `recalc_force_viewport_sizes()` after a runtime reparent — the narrower fix would live on the editor side; (b) it now force-overrides a dynamically-added SubViewport's `update_mode` to `UPDATE_ALWAYS`/`UPDATE_DISABLED` earlier than before, a minor but real regression for a user project that adds an `UPDATE_ONCE` viewport into a live container. Also, the `set_size_force()` runs *outside* the `is_inside_tree()` guard while the update-mode block runs inside it — inconsistent guarding (harmless in practice, later `RESIZED` corrects size).

---

## Low findings (cleanup / smells)

- **L1 — `set_pane_current_scene(1, get_pane_current_scene(1))` self-assignment.** `editor_node.cpp:4949`. `get_pane_current_scene(1)` returns `-1` (slot doesn't exist yet); this is an obscure way to grow the vector to size 2, which `set_pane_current_scene` already does internally. Debugging residue — replace with an explicit init or drop it.
- **L2 — Tautological DEV assert.** `editor_data.cpp` `set_focused_pane` assigns `current_edited_scene = pane_current_scenes[p_pane]` then asserts `current_edited_scene != get_pane_current_scene(focused_pane)` — the two are the same value by construction, so the assert can never trip. Dead/misleading.
- **L3 — Redundant trailing write in `move_edited_scene_to_index`.** `editor_data.cpp:905-925`. The remap loop already maps the moved scene to `p_idx`; the explicit final `pane_current_scenes[es.pane] = p_idx` is dead in the healthy case and actively wrong if M1 already broke the invariant (it would clobber a legitimately-different pane current).
- **L4 — Redundant dock-context nulling on teardown.** `editor_node.cpp:942-953` calls `_activate_scene_context(nullptr)` (which already rebinds the focused pane's docks to null) and then nulls all four docks' contexts; the focused pair is set twice.
- **L5 — Always-true sub-condition.** `scene_tree_editor.cpp:281` guards `if (!is_inside_tree() && np.is_absolute())` but every item's metadata comes from `get_path()` (always absolute), so `np.is_absolute()` is always true — reads as if relative paths are handled when none exist.
- **L6 — `_reset()` leaves `current_scene_id` stale while zeroing `force_update`.** `scene_tree_editor.cpp:968-974`. When the edited root is temporarily detached, the early return `_reset()`s and sets `node_cache.force_update = false`, but does not clear `current_scene_id`. It only rebuilds correctly today because `_reset()` empties the cache; if cache-preservation ever changes, this becomes a stale-tree bug. `force_update = true` would be the safer default here.
- **L7 — `_is_node_displayable()` is fail-open when the editor is off-tree** (`scene_tree_editor.cpp:293`, returns `true`) and re-implements `get_scene_node()` inline (297) with an extra `is_inside_tree()` check instead of calling it.
- **L8 — `menu_initialized` promoted to a function-local `static LocalVector<bool>` keyed by `pane_index`.** `editor_scene_tabs.cpp:379-393`. This cross-instance global is never reset when a pane's tabs are destroyed and recreated (unsplit → re-split), so a fresh pane-1 tab bar inherits stale `menu_initialized[1] == true`. Survives today only because the skipped rebuild is a no-op.
- **L9 — macOS global dock menu regression.** `editor_scene_tabs.cpp:308-315` only populates `DOCK_MENU` for `pane_index == 0`, and the per-tab global-menu update (title, unsaved `(*)` marker, `set_tab_metadata`) was dropped from `_update_tab_titles`. On macOS the dock menu no longer reflects unsaved state.
- **L10 — `remove_scene` picks the pane's *first* member as the replacement current**, inconsistent with `set_scene_pane` (which picks the adjacent neighbor) and with normal tab-close UX. `editor_data.cpp:663-687`. Index remapping itself is correct; only the choice of replacement is off.
- **L11 — `_recompute_3d_content()` can go stale for a non-focused pane** (only recomputed on `set_scene_root_node`/`set_display_parent`), and `set_display_parent` runs an O(N) subtree walk on every call while `_update_pane_display_attachments` calls it once per scene per refresh. `editor_scene_context.cpp`.

---

## Architectural notes (not defects, but the source of the churn)

- **Main screen as an absolute-positioned overlay.** The real editor main screen (2D/3D viewports, etc.) is not reparented into the focused pane — it's kept in a `center_overlay` layer and manually re-fitted over the focused pane's `content_host` via `set_global_position`/`set_size` on every `resized` (`editor_node.cpp:4934-4939`, `editor_scene_workspace.cpp:323-333`). Non-focused panes then need the separate `SubViewport` preview path. This overlay-fitting is the root of most of the "layout / viewport clipping / visibility" fix commits (`b56ff5686b`, `239c62644e`, `5d951f6eef`). It works but is fragile; every timing bug in this branch traces back to keeping the overlay geometry in sync with a control it does not actually parent.

- **The engine changes are necessary *for this design* but compensate for editor-side gaps** (confirmed via the engine review):
  - `viewport.cpp:548` — the `is_inside_tree() && get_tree() == …` guard is a legitimate anti-spam/anti-empty-path fix (`get_path_to` already `ERR_FAIL`s on cross-tree nodes; the guard just stops it clobbering `E->path` with an empty value). Safe for normal single-scene use. It filters *detached* local scenes, not *different* scenes — necessary but not sufficient for true scene isolation, which is all it claims.
  - `subviewport_container.cpp` — see M6.
  Both exist because the shared editing viewport is referenced by other/detached scenes' `ViewportTexture`s and gets reparented live. A design that resolved these on the editor side (re-run container layout after reparent; scope path resolution to the owning scene root) would not need engine patches.

- **`scene_tree_editor.cpp`'s defensive refactor treats symptoms.** Swapping `get_node(metadata)` for `_get_node_from_item()`/`get_node_or_null()` everywhere (commit `e383ff89a3`) is individually sound (no new null-derefs, `can_drop_data_fw` is strictly safer), but the underlying fragility is that item metadata stores tree-global absolute paths (`p_node->get_path()`) that go stale when a scene detaches or its viewport is reparented. The commit hardens every read site instead of scoping resolution to `get_scene_node()`. It's defensible as a stability fix; just recognize it's a lot of surface area added to avoid changing the resolution model.

---

## What looked correct (spot-checked, not defects)

- `tab_bar.cpp` `tab_transferred` signal + `doc/classes/TabBar.xml` — clean, documented engine addition.
- Removal of `EditorSceneTabs`' own `tab_changed`/`tab_closed` signals — safe; no remaining connections target them.
- The `SceneTreeDock` selection rework (`editor_selection_id` + `ObjectDB::get_instance` guarded disconnect, nulling on rebind) is a genuine improvement over the old raw-pointer disconnect.
- `_is_context_pane_current()` gating in `_activate_scene_context` correctly avoids deactivating a context still shown in another pane.
- `_save_workspace_to_config` / `_load_workspace_from_config` are symmetric through the `EditorSceneWorkspace` static helpers.
- The `reload_scene_from_memory` ancestor check and the `editor_undo_redo_manager` multi-scene history lookup are legitimate multi-scene adaptations, not dead code.
- The clean removal of `_save_open_scenes_to_config`/`_load_open_scenes_from_config` left no dangling references.

---

## Suggested order of attack

1. **H2** (recursion) and **H3** (split-offset clobber) — smallest, clearly-wrong, one is a real user-facing regression.
2. **H1** (delete dead `activate()`) and the low-risk dead-code items **L1–L5, L7** — pure subtraction, shrinks the diff and the reader's confusion.
3. **H4/H5** — decide whether click-to-focus is a feature (then make it actually work on leaf controls) or cut it; collapse the five focus paths to one.
4. **M3** (`EditorSceneTabs` destructor + teardown ordering) — hardens the crash-prone path the history kept re-patching.
5. **M1/M2** — add the split-workspace reload and cross-pane drag-drop tests; fix the bookkeeping the tests expose.
6. Revisit whether the engine patches (M6, viewport guard) can be replaced by editor-side re-layout + scene-scoped path resolution, or at least add a comment on each engine change explaining the editor invariant it upholds.
