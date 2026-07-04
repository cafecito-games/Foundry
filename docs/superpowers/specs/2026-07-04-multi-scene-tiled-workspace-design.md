# Design: Self-contained tiled scene panes (multi-scene workspace)

Status: approved for spec review
Date: 2026-07-04
Epic: #821 (Multi-scene editing). Revises the locked decisions of Phase C (#827) and
Phase D (#828); supersedes the current Phase C branch `csueiras/multi-scene-phase-c-split-pane-fed5`.

## Motivation

The current split-pane branch is Phase C as originally specced: exactly two panes in a
single `SplitContainer`, the second scene tree + inspector registered in the editor's **global**
dock slots (tabbed behind the primaries), and only the focused pane hosting a live editor
(others show a static preview). The target model refines this into a more flexible workflow —
open multiple scenes at once, each with its own inspector and scene tree, arranged into freely
subdividable areas (and, as later stretch work, torn off to a separate window/monitor). That
target differs from Phase C's locked decisions in three ways:

| Phase C locked decision | New target |
| --- | --- |
| **D1** — exactly two panes, one split, no recursion | Recursive N-way tiling |
| **D6** — second dock pair registered in the **global** dock slots | Scene tree + inspector live **inside** the pane, wrapped around its viewport |
| **D2** — only the focused pane is live; others show a preview | Every pane fully live (Phase D) — kept, sequenced as the second milestone |

The user-facing mental model becomes: **a pane is one self-contained editing unit —
tab strip + scene tree + viewport + inspector — and the workspace is a freely subdividable
arrangement of these units. You split by dragging a scene tab to a pane's edge.**

This is a revision of the epic's design, not a bugfix of the branch. The branch's durable
plumbing is salvaged; its superseded UI is replaced.

## Scope

**In scope (target model "B", designed to be "A"-ready):**
- A recursive, generic dock-tree workspace container (N tiles via nested splits).
- Self-contained scene-pane tiles: scene tabs + in-tile scene tree + viewport + in-tile
  inspector.
- Drag-a-scene-tab-to-an-edge to split a tile; drop-on-center to move a scene between tiles;
  auto-collapse a tile when it loses its last scene.
- Full per-tile liveness (every tile shows gizmos and accepts direct manipulation), delivered
  as the **second** milestone (folds in #828).
- Nested-tree layout persistence; focused-tile-is-global-current compatibility model.

**Explicitly deferred (later "A" work, not a redo):**
- Tear-off a tile/panel to a separate OS `Window`/monitor.
- Arbitrary non-scene panels (FileSystem, Output, Debugger) as free-floating dockable tiles.
- Unifying/absorbing the global `EditorDockManager` slot model into the tree.

**Deferred within B (follow-up issues, staged after milestone 1):**
- Moving signals (`ConnectionsDock`), groups, node, and history docks **into** the tile.
  Phase B only made the scene tree and inspector instantiable per context; these others are
  still global singletons. Milestone 1 keeps them global and focus-following; follow-up
  issues make them per-context and relocate them into the tile's dock region.

## Relationship to the epic

- **Phase A (#825)** and **Phase B (#826)** are already merged to `develop` (`#866` etc.).
  Their de-globalization (per-scene `EditorSceneContext`; instantiable scene tree + inspector
  docks bound via `set_scene_context()`; the `"<key>:<n>"` dock layout-key scheme;
  `EditorNode::no_scene_context`) is the foundation this design builds on and keeps.
- **Phase C (#827)** is revised: D1 (2 panes → recursive N) and D6 (global-slot docks →
  in-tile docks) change; D3, D4, D5, D7, D8 are kept (with D7's per-scene `int pane` tag
  generalized to a stable tile id). The current Phase C branch is **not merged**; its durable
  code is salvaged.
- **Phase D (#828)** is revised only in that its per-view liveness now targets tiles rather
  than the 2-pane model. Its locked decisions D1–D6 (singletons stay; views become
  instantiable; one toolbar; single edit-object model; per-context `World3D`; focused-view
  plugin forwarding) are otherwise intact and become milestone 2 here.

## Architecture

### 1. Generic recursive dock tree (the workspace container)

Replace `EditorSceneWorkspace`'s single `SplitContainer`
(`editor/editor_scene_workspace.h:79`, split logic at `editor/editor_scene_workspace.cpp:255`)
with a binary tree of nodes:

- **SplitNode** — wraps a `SplitContainer` (horizontal or vertical) with exactly two child
  nodes and a draggable divider (offset persisted). Nesting SplitNodes yields arbitrary
  tiling. `SplitContainer`'s natural two-child shape maps cleanly onto a binary split tree.
- **LeafNode** — holds exactly one `ScenePaneTile`.

Tree operations:
- `split(leaf, orientation, side)` — replace `leaf` with a new SplitNode whose children are
  `[leaf, newLeaf]` (order per `side`), the new leaf hosting a fresh tile.
- `collapse(leaf)` — when a tile loses its last scene, remove the leaf and promote its
  sibling into the parent SplitNode's slot (deleting the now-single-child SplitNode).
- `move_scene(scene, from_leaf, to_leaf)` — retag the scene and refresh both tabs.

The node type is intentionally general so **A** can later add a `TabNode` (stack arbitrary
panels), non-scene leaf types, and a per-`Window` host for tear-off, **without** reshaping
this layer. In B, leaf contents are constrained to `ScenePaneTile`.

### 2. `ScenePaneTile` — the self-contained editing unit

Evolves the current `EditorScenePane` (`editor/editor_scene_workspace.h:45`). Layout:

```
VBoxContainer (tile):
  ├─ EditorSceneTabs        (this tile's scene strip; per-tile instance)
  └─ HSplitContainer (body):
       ├─ [ left dock region ]   SceneTreeDock instance (bound to tile's context)
       ├─ [ center ]             viewport host
       └─ [ right dock region ]  InspectorDock instance (bound to tile's context)
                                 (+ signals/groups/node/history added by follow-ups)
```

- The tile **owns** its `SceneTreeDock` and `InspectorDock` instances (Phase B made both
  instantiable and bindable via `set_scene_context()`; see `editor/docks/scene_tree_dock.h`,
  `editor/docks/inspector_dock.h`). They are **not** registered in the global
  `EditorDockManager` slots — this replaces Phase C's `_create_secondary_docks()` /
  `"Scene:2"`/`"Inspector:2"` global-slot registration (`editor/editor_node.cpp:4886`).
- **Responsibility split:** *per-scene* docks (scene tree, inspector; later signals/groups/
  node/history) live in the tile. *Project-global* docks (FileSystem, Output/log, Debugger,
  Import) stay in the editor shell / bottom drawer — consistent with the editor's collapsible
  bottom drawer for filesystem/logs.
- The center viewport host is where the live editor surface (milestone 1: focused tile only;
  milestone 2: every tile) or the non-focused preview renders.

### 3. Focus and global-state compatibility (kept from #827 D3/D4)

Unchanged from Phase C — this is the merged, load-bearing compatibility trick and it stays:

- **The focused tile's scene IS the global current scene.** `EditorData::current_edited_scene`,
  `EditorNode::active_scene_context`, `editor_selection` / `editor_history`,
  `SceneTree::edited_scene_root`, window title, unsaved cache, and every menu/shortcut/run
  handler keep their existing meaning, now tracking the focused tile. This is what keeps the
  ~110 `SceneTree::get_edited_scene_root()` consumers and all menu routing correct with no
  per-site change.
- **Any interaction inside a tile focuses it first** (tab strip, docks, viewport), so global
  machinery (undo-history selection, `push_item`, `edit_current`) always targets the right
  context.
- On focus change, the dock class `singleton` pointers re-point to the focused tile's
  instances (`SceneTreeDock::set_focused_instance`, `InspectorDock::set_focused_instance`,
  `EditorSceneTabs::set_focused_singleton`; driven today by
  `_update_focused_dock_singletons()` at `editor/editor_node.cpp:4859`), preserving the
  ~69 + ~89 `get_singleton()` call sites.
- Focus flow generalizes from the fixed `focused_pane` (0/1) to walking the tree to the
  focused `LeafNode` (`focus_pane()` at `editor/editor_node.cpp:4987` becomes tile-based).

### 4. Drag-a-tab-to-an-edge (headline interaction)

- All tiles' `EditorSceneTabs` share a tab-rearrange group
  (`TabBar::set_tabs_rearrange_group`; dock-manager precedent at
  `editor/docks/editor_dock_manager.cpp:1058`), enabling cross-strip tab drag.
- While a tab drag hovers a target tile, a **drop-zone overlay** draws five regions:
  **center** (move the scene into that tile's strip) and **four edges** (split that tile in
  the corresponding direction; the new tile receives the dragged scene). Hit-testing the
  cursor against these regions selects the action on drop.
- Drop actions map to tree ops: edge → `split(targetLeaf, orientation, side)` + `move_scene`;
  center → `move_scene` into the target tile. Closing the last tab in a tile → `collapse`.
- Note: `TabBar`'s cross-bar move path (`TabBar::_move_tab_from`) emits no signal; the
  workspace mediates the move explicitly (same gap Phase C already had to bridge).

### 5. Liveness — two milestones

**Milestone 1 (structure; revises #827):** honest reparent, no overlay.
- The single `EditorMainScreen` (2D/3D/Script switcher) is **reparented as a real child** of
  the focused tile's viewport host and laid out by the container — **not** absolute-positioned
  and manually fitted. This deletes the overlay-fit machinery
  (`fit_overlay_to_focused_pane()` at `editor/editor_scene_workspace.cpp:316`,
  `_fit_main_screen_to_focused_pane()` at `editor/editor_node.cpp:4932`) and the two engine
  band-aid patches (`scene/gui/subviewport_container.cpp` update-mode override,
  `scene/main/viewport.cpp` guard). The Phase C handoff identifies this reparent as the
  "higher-leverage fix" the overlay model was avoiding.
- Non-focused tiles show a live 2D preview (per-context viewport) or a 3D placeholder
  (Phase C parity), until milestone 2.

**Milestone 2 (liveness; revises #828):** every tile fully interactive.
- Per-context `World3D` isolation (`#828` W1): each `EditorSceneContext` gets its own
  `World3D`; the finite retarget list of root-world sites (transform-gizmo instances, origin,
  grid, indicators/cursor, placement/snap raycasts, preview sun/env, GridMap overlays — all
  enumerated in #828) moves to the context's world. Lifts the 3D-preview restriction.
- Views become instantiable while the `CanvasItemEditor` / `Node3DEditor` **singletons stay**
  (#828 D1): each singleton splits into a global controller (tool/snap/toolbar/gizmo registry)
  plus per-view surfaces; compatibility accessors resolve to the focused tile's view.
- Every tile shows gizmos and accepts direct manipulation; the first input focuses the tile
  (D4), then the single global `edit()` dispatch (#828 D3) targets it. "Simultaneous editing"
  means every tile is manipulation-ready with visible gizmos, not two concurrently-live
  `edit()` objects.

### 6. Data model and persistence

- `EditorData`'s per-scene `int pane` (currently 0/1; `editor/editor_data.h:114`) generalizes
  to a **stable tile id**. `pane_current_scenes` becomes keyed by tile id; `focused_pane`
  becomes the focused tile id. A scene belongs to exactly one tile; a tile's tab order is the
  global `edited_scene` vector filtered by tile id (keeps #827 D7's single global vector).
- The `[Workspace]` section of `editor_layout.cfg` is replaced by a **serialized nested tree**:
  each SplitNode records orientation + divider offset + its two children; each LeafNode
  records its scene list, current scene, and in-tile dock layout. No backward compatibility
  (#827 D8 already established the owner requires none; old configs start a fresh single-tile
  session). Per-scene `-editstate-<md5>.cfg` files are path-keyed and unchanged.

### 7. Plugins and public API (kept from the epic)

- Plugin viewport forwarding (`forward_canvas_gui_input`, `forward_3d_gui_input`,
  `forward_*_draw_over_viewport`, `update_overlays()`), `EditorInterface` accessors
  (`get_editor_viewport_2d/3d`, `get_edited_scene_root`), and the toolbar/menus all resolve to
  the **focused tile** (#828 D2/D5). Documented as focused-tile semantics. Extending forwarding
  to secondary tile views is deferred (#828 defers it too).

## Files touched (indicative)

- `editor/editor_scene_workspace.{h,cpp}` — reworked to host the recursive node tree
  (SplitNode/LeafNode + tree ops) instead of a single `SplitContainer`.
- `editor/editor_scene_pane_tile.{h,cpp}` (new, evolving `EditorScenePane`) — the
  self-contained tile with in-tile dock regions and its own tabs.
- A drop-zone overlay control (new; folded into the workspace or its own file) — draws and
  hit-tests the 5 drag-to-edge regions.
- `editor/editor_node.cpp` — replace `_create_secondary_docks()` /
  `_destroy_secondary_docks()` / global-slot registration with per-tile dock construction;
  generalize focus routing (`focus_pane`, `_activate_scene_context`, `_bind_pane_docks`) to
  the tree; reparent `EditorMainScreen` as a child (drop overlay-fit and the resize wiring).
- `editor/editor_data.{h,cpp}` — `int pane` → stable tile id; `pane_current_scenes` and
  `focused_pane` generalized; invariant updated (`current_edited_scene ==
  current_scene_of(focused_tile)`).
- Persistence: nested-tree serialize/restore in the workspace + `editor_node` load/save paths.
- Revert the two engine patches (`scene/gui/subviewport_container.cpp`,
  `scene/main/viewport.cpp`) once the honest reparent removes their need.

## Testing

- Extend `tests/editor/test_scene_workspace.h`: tree `split`/`collapse`/N-tile shapes,
  `move_scene` between tiles, tab-drag-to-edge action selection, nested-tree persistence
  round-trip, and the `current_edited_scene == current_scene_of(focused_tile)` invariant
  across focus/split/collapse.
- Extend `tests/editor/test_dock_scene_context_binding.h`: in-tile dock instances bind and
  rebind to the tile's context; safe rebind after a bound context is freed (existing crash
  guard) still holds with tile-owned docks.
- Milestone 2: per-context `World3D` isolation tests (no geometry bleed between tiles; gizmo/
  raycast/preview-sun retargeting) alongside #828's acceptance criteria.

## Milestones and issue plan

1. **PR1 — structure (revises #827).** Recursive tiling tree, self-contained tiles with
   in-tile scene tree + inspector, drag-a-tab-to-edge splitting, honest `EditorMainScreen`
   reparent (overlay + engine patches removed), nested-tree persistence, tile-id data model.
   Focused-tile-live; non-focused tiles preview as in Phase C. Lands with single-tile parity.
2. **PR2 — liveness (revises #828).** Per-context `World3D` + per-view instantiation → every
   tile live and manipulable.
3. **Follow-up issues (after PR1).** Make signals (`ConnectionsDock`), groups, node, and
   history docks instantiable per context and relocate them into the tile's dock region.
4. **Later "A" issues (deferred).** Tear-off to separate `Window`/monitor; arbitrary non-scene
   panels as dockable tiles; unify with `EditorDockManager`.

## Salvage list from the unmerged Phase C branch

Carry forward: `EditorData` per-pane bookkeeping (generalized to tile ids), per-pane
`EditorSceneTabs`, `EditorSceneContext` display-parent refinements, focus-routing entry
points, persistence scaffolding, and all invariant/crash fixes from the review pass.
Drop/replace: the single-`SplitContainer` workspace, global-slot secondary-dock registration,
the overlay-fit model, and the two engine patches.

## Open questions

None blocking. Signals/groups/node/history relocation is intentionally staged to follow-ups;
tear-off and full docking are intentionally deferred to "A".
