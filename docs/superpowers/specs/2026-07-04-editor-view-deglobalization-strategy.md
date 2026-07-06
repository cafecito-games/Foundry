# Strategy: De-globalize the 2D/3D editors, then tile (multi-scene workspace, re-sequenced)

Status: proposed for review
Date: 2026-07-04
Grounding: all `file:line` and counts below are pinned to `develop @ b51a8d5938`.
Epic: #821 (Multi-scene editing). This document **re-sequences** the delivery of the
tiled-workspace design (`docs/superpowers/specs/2026-07-04-multi-scene-tiled-workspace-design.md`)
and **supersedes** the current Phase D branch and issue #918.

## 1. Why this document exists

Phases A (#825) and B (#826) — per-scene `EditorSceneContext` and instantiable
scene-tree/inspector docks — are merged to `develop` and are clean. The tiled workspace
(Phase C, #905) and per-tile liveness (Phase D, #906) were built on a long-lived integration
branch as one large, entangled change and have proven very hard to stabilize. The failure is
not the tiling design; it is **how the work was sequenced and observed**:

1. **Shared mutable singleton state.** `CanvasItemEditor` and `Node3DEditor` are process-wide
   singletons; a change made "in one tile" mutates global state that another tile reads, and
   nothing catches the bleed. The #905 lifecycle bugs (black panes on focus switch, stale 2D
   disable flags, preview framing) were all render-coupling from this single-global-editor model.
2. **No green baseline between steps.** Phase C and Phase D landed together as a ~4,900-line
   diff (a near-total rewrite of `canvas_item_editor_plugin.cpp`, +1,706 lines). Every bug had
   thousands of lines of suspects.
3. **No invariant or cross-context observability.** Nothing asserts the compatibility invariant
   (`current_edited_scene == current_scene_of(focused_tile)`) or "no state bleed between
   contexts," so corruption only surfaced as weird downstream behavior.

None of these are cured by building a new workspace container. The singletons — the actual pain
— live in the editor *plugins*, not in the tiling code. A parallel workspace would replace the
least-broken layer and inherit the most-broken one unchanged. Two workspaces also cannot run
side by side: there is exactly one `EditorMainScreen`, one `CanvasItemEditor`, one
`Node3DEditor`, and the compatibility model is "the focused tile *is* the global current scene."

## 2. The decision

- **`develop` is the clean baseline.** It already has the de-globalized foundation (Phase A/B)
  and none of the entangled Phase C/D code. We start here.
- **Retire the Phase D branch** (`csueiras/multi-scene-phase-d-liveness-d139`) and the
  integration branch's Phase D work. Mine them for the member-classification map (which
  `CanvasItemEditor` members turned out per-view vs. global, and which bugs they hit), then
  delete. Do **not** cherry-pick the editor-plugin rewrites — that code is the tangle we are
  escaping.
- **Salvage the container as code**, not the editor-plugin edits. The tiling tree, tile,
  drop overlay, persistence scaffolding, focus-routing entry points, and `EditorData` tile-id
  bookkeeping live on `feature/multi-scene-workspace` (#905) in **new files** that do not
  collide with the editor-plugin refactors.
- **De-globalize first, as parity-only refactors on `develop`, before wiring any tiling.**
  The scary decomposition of the editor singletons does not need tiling to exist; doing it
  first, single-scene, with "nothing changed" as the acceptance test, is what makes each step
  verifiable in isolation.
- **Scope per unit: the smallest change that lands green with parity.** Every issue below is
  individually revertible and individually mergeable to `develop`.

## 3. Architecture: separate State, Logic, and View

The guiding principle for the de-globalization is a three-way separation of concerns, applied
to both editors. This is what makes the per-view surfaces instantiable *and* testable without a
GUI.

| Concern | What it holds | Lifetime / ownership | Testability goal |
| --- | --- | --- | --- |
| **State** | Per-view editing state: 2D `transform`/`view_offset`/zoom/`drag_type`/selection & hover results/guides; 3D camera cursor, per-view drag `EditData`. Plain data. | One instance **per editing context/tile**. | Unit-testable as pure data + transitions, no `Control` instantiation. |
| **Logic** | The global controller: active tool, snap/grid config, gizmo-plugin registry, toolbar/menus/dialogs, shared meshes/materials, operations (`snap_point`, selection resolution). | **One** global instance (the surviving singleton). Shared across all tiles by design. | Unit-testable operations that take State + inputs and return results. |
| **View** | The widget surface: viewport `Control`, overlay draw, input forwarding, scrollbars, panner, per-world 3D furniture. Owns no authoritative state beyond ephemeral UI. | One instance **per tile**; bound to a State and the Logic controller. | Constructible under the test bootstrap (same bar as Phase B docks). |

**Why the controller stays shared (not "fully independent editors per tile"):** the ~219
external `get_singleton()` call sites split cleanly into *global config* and *per-view state*.
On `develop @ b51a8d5938`:

- **Global config — stays on the shared Logic controller, unchanged.** 3D snap alone is ~44
  calls (`is_snap_enabled` ×21, `get_translate_snap` ×20, plus rotate/scale); toolbar panel
  injection (`add_control_to_menu_panel` and left/right panels) ~20; gizmo registry
  (`add_gizmo_plugin` + registry) ~10; 2D `get_current_tool` ×5, `snap_point`. Users want one
  toolbar, one active tool, one snap setting, one gizmo registry across every tile.
- **Per-view state — resolves to the focused View.** 2D `get_canvas_transform` ×14,
  `get_viewport_control` ×5, `set_cursor_shape_override` ×8; 3D `get_editor_viewport` ×6,
  `update_transform_gizmo` ×4, the BVH queries. ~50 calls total across both.

So de-globalization does **not** touch ~170 of the 219 sites; it redirects ~50 per-view
accessors to "the focused View." One `edit()` target and focused-tile plugin forwarding are
retained (see the tiled-workspace design D3/D5).

### 3.1 3D already has the seam

`Node3DEditorViewport` is already a real, instantiable `Control`
(`editor/scene/3d/node_3d_editor_plugin.h:108`), and `Node3DEditor` already owns four of them
(`viewports[VIEWPORTS_COUNT]`, `.h:693`, `VIEWPORTS_COUNT = 4` at `.h:661`). Each already owns
its camera, cursor, drag/transform `EditData`, and per-viewport gizmo instances. De-globalizing
3D means: bind each viewport to its context's `World3D`, make the origin/grid/indicator/gizmo
*furniture* per-world (today single-world), and allow a dynamic count of world-bound views —
not a rewrite.

### 3.2 2D has no seam

`CanvasItemEditor` (`editor/scene/canvas_item_editor_plugin.h`) is one ~6,763-line class
(`.cpp`) that flat-mixes per-view State (`transform` `.h:216`, `view_offset` `.h:229`,
`drag_type` `.h:374`, `selection_results` `.h:289`, `hovering_results` `.h:297`, viewport
`Control` `.h:202`, scrollbars `.h:205-206`) with global Logic (`tool` `.h:201`, the `snap_*`
flags `.h:244-262`, grid config, toolbar buttons `.h:343-355`, shortcuts). The real work is the
extraction the engine never did: carve a `CanvasItemEditorView` (+ its State) out of the
singleton, State first.

## 3.3 Workspace content model — panes, docks, scripts, and cross-pane DnD

A pane (tile) is a **self-contained editing unit bound to one `EditorSceneContext`**. The
organizing rule: *scene-bound* things live in the pane; *project/app-global* things stay shared.

- **In the pane (per-context):** its scene tab strip, scene tree, inspector, and — extending
  Phase B to the rest — signals (`ConnectionsDock`), groups (`GroupsEditor`), and history
  (`HistoryDock`). On `develop @ b51a8d5938` only `SceneTreeDock`/`InspectorDock` expose
  `set_scene_context()`; the other three are still global singletons (U12), and all of them
  relocate into the pane with a persisted per-pane dock layout (U13).
- **Global (shared / app-level):** the tool/snap/toolbar/gizmo controller; the **Game** view
  (one running game, a single app-level screen); FileSystem, Output, Debugger, Import.
- **Removed:** the **AssetLib** editor is dropped entirely (separate cleanup issue).

**The workspace holds typed leaves, not just scenes (U14).** `EditorMainScreen`'s modes split
into *per-pane scene modes* (2D/3D, chosen by scene root type) and *global screens* (Game). U6's
"honest reparent" therefore reparents only the **2D/3D scene-editing surface** into the focused
pane, not the whole fullscreen mode switcher. The `LeafNode` gains a generic content type so a
non-scene leaf can coexist with scene leaves.

**Scripts are a workspace leaf (U15), not a fullscreen mode.** A script is a project resource
(not scene-owned), so a `ScriptLeaf` carries no `EditorSceneContext`; it is splittable beside any
scene leaf. This is forced by the node→script drag: the scene tree emits a `"nodes"` drag payload
(`editor/scene/scene_tree_editor.cpp:1903`) that the script editor consumes as a `get_node(...)`
reference. That drag is ordinary same-window Control DnD, so it works across panes for free — its
*only* requirement is that source and target are co-visible, which a script leaf beside a scene
leaf provides (today it only works because the scene tree is a global side dock).

**Cross-pane DnD with drop-focus.** A reference may be grabbed from **any** pane and dropped onto
**any** pane with a valid target; completing the drop **focuses the receiving pane** before the
payload is applied (so its context/edit target is current). This extends #905's "interaction
focuses the tile first" rule to drop targets, and is a general workspace rule, not script-specific.

## 4. Sequencing — parallelizable tracks of bite-size units

Tracks 1, 2, and 3 touch disjoint files (2D plugin / 3D plugin / new container files) and can
proceed in parallel. Each unit lands green on `develop` with single-scene parity before the
next in its track. Liveness (Track 4) is mostly mechanical wiring once the hard extraction
(Tracks 1–2) is done and verified.

### Track 1 — 2D de-globalization (pure refactor, single-scene parity)
- **U1 — Extract `CanvasItemEditorViewState`.** Move per-view State fields out of
  `CanvasItemEditor` into a plain data struct still owned singly by the singleton. Zero behavior
  change. Test: state transitions (pan/zoom/select math) unit-tested with no `Control`.
- **U2 — Extract `CanvasItemEditorView`.** Move the viewport `Control`, overlay draw, input,
  scrollbars, and panner into an instantiable class bound to `(ViewState, controller)`. Singleton
  owns exactly one. Single-view parity (identical pixels/behavior). Test: `canvas-view-bind` —
  constructs under the test bootstrap and pushes its transform to a bound context's viewport,
  not a global.
- **U3 — Route per-view accessors to the focused View.** `get_canvas_transform`,
  `get_viewport_control`, `update_viewport` (fan-out redraw), `set_cursor_shape_override`,
  `get_state`/`set_state` resolve through the focused View. Global-config accessors unchanged.

### Track 2 — 3D de-globalization (pure refactor, single-scene parity)
- **U4 — Per-context `World3D` isolation** (tiled-design milestone-2 W1). Each
  `EditorSceneContext` gets its own `World3D`; retarget the finite root-world census
  (`node_3d_editor_plugin.cpp` transform-gizmo scenarios `4407,4416,4425,4434,4446,4457`;
  placement/snap raycasts `4955,8777`; origin `7788`; grid `8476`; preview sun/env reparent;
  `editor_node.cpp` fallback-env; `grid_map_editor_plugin.cpp:607,1282,1382`). Parity-only, one
  tile. Test: `context-own-world`, `world-rebind`.
- **U5 — World-bound instantiable 3D view + per-world furniture.** Make origin/grid/indicator/
  gizmo furniture per-world sets; allow a single `Node3DEditorViewport` bound to an arbitrary
  context world (secondary view); scope the gizmo BVH by world. Focused 4-viewport editor
  unchanged. Test: `bvh-world-filter`.

### Track 3 — Container (salvage #905, re-land clean on `develop`)
- **U6 — Recursive tiling tree + self-contained tile + honest reparent + persistence.**
  `SplitNode`/`LeafNode` tree; `ScenePaneTile` with in-tile scene tree + inspector (Phase B
  instances) + own tabs; `EditorMainScreen` reparented as a real child of the focused tile
  (delete the overlay-fit machinery and the two engine band-aid patches); nested-tree layout
  persistence; `EditorData` `int pane` → stable tile id. Non-focused tiles show a preview (no
  liveness yet). Single-tile parity. Tests: tree `split`/`collapse`/N-tile, `move_scene`,
  persistence round-trip, the focused-tile invariant.
- **U7 — Drag-a-tab-to-an-edge splitting.** Shared tab-rearrange group; 5-region drop overlay;
  drop→tree-op mapping; auto-collapse on last-tab close. Test: drop-region action selection.

### Track 4 — Liveness wiring (mechanical, after Tracks 1–3)
- **U8 — 2D live views in non-focused tiles** (W4). Bind a `CanvasItemEditorView` to each
  non-focused 2D tile; first input focuses the tile. Depends on U3 + U6.
- **U9 — 3D live views in non-focused tiles** (W2 preview → W5 editing). Bind a world-bound
  `Node3DEditorViewport` to each non-focused 3D tile. Depends on U5 + U6.

### Track 5 — Public surface + observability
- **U10 — Plugin forwarding + `EditorInterface` focused-tile routing + class-ref docs** (W6).
  In-repo module verification pass (csg, gridmap, navigation gizmos/forwarding).
- **U11 — Invariant + cross-context automation harness.** First-class tests for
  `current_edited_scene == current_scene_of(focused_tile)` and no-state-bleed; MCP editor
  automation that drives real tiles and asserts cross-tile isolation (select in tile A → tile B
  untouched). The foundational (single-context) assertions land early alongside U1–U5; the
  cross-tile assertions extend after U6.
- **U16 — MCP editor automation: multi-pane awareness.** The automation surface is single-scene
  today (`read_editor_state` reports one main screen / edited scene; `within` can't name a tile;
  no semantic dock/drag). Extend it to report the workspace tree + focused tile, scope selectors
  by tile, add a semantic dock/drag action, and add tile-lifecycle wait conditions. **Enables**
  U11's cross-tile scenario and makes the #933 (drag-to-dock) and U15b (cross-pane drag) manual
  checklists scriptable. Needs U6c (tile ids + focus).

### Track 6 — Per-scene docks in the pane
- **U12 — Signals/groups/history docks per-context.** Extend Phase B's `set_scene_context()` to
  `ConnectionsDock`, `GroupsEditor`, `HistoryDock`. Parity-only.
- **U13 — Relocate per-context docks into the pane + per-pane dock layout.** Inspector, signals,
  groups, history live in the pane bound to its context; persisted per-pane dock arrangement.
  Depends U12 + U6.
- **U13b — Remove the focused-tile dock singleton indirection.** U6/U12/U13 keep a
  compatibility trick — `EditorNode::_update_focused_dock_singletons()` repoints each dock's
  static `get_singleton()` to the focused tile. Replace it with explicit focused-tile accessors
  and delete the repoint machinery. Follow-up to U13; keeps new code from silently breaking
  multi-tile behavior via `::get_singleton()`. Delivered in two slices:
- **U13b — slice 1:** signals/groups/history migrated to `get_focused_*` accessors; their
  singletons removed (PR #988).
- **U13c — slice 2:** extend to scene tree + inspector (the ~69 + ~89 census sites), then delete
  `_update_focused_dock_singletons()` entirely. Depends U13b.

### Track 7 — Workspace content model
- **U14 — Partition `EditorMainScreen` + generic leaf model.** 2D/3D per-pane scene modes; Game
  global; `LeafNode` generalized to typed content. Defines U6's reparent boundary. (Assumes the
  standalone AssetLib-removal cleanup.)
- **U15 — Scripts as workspace leaves.** `ScriptLeaf` (no `EditorSceneContext`), splittable beside
  a scene; cross-pane node→script drag with drop-focus. Depends U14 + U6.

### Dependency summary
```
U1 → U2 → U3 ┐
U4 → U5 ─────┤→ U8 (needs U3,U6) ─┐
U6 → U7 ─────┘  U9 (needs U5,U6) ─┴→ U10 → (U11 grows across all)
U12 → U13 (into pane; needs U6)     U14 → U15 (typed leaves, scripts; needs U6)
```
Standalone cleanup (not a unit): Remove the AssetLib editor plugin (assumed by U14).

## 5. Observability strategy (the missing safety net)

- **Parity is the acceptance test for Tracks 1–3.** Because U1–U6 are behavior-neutral
  single-scene refactors, "identical behavior" is a concrete, cheap check *before* any
  multi-context complexity exists.
- **Invariant tests run every build** (U11): the focused-tile compatibility invariant and
  per-context isolation (distinct `World3D`/scenario RIDs; a `MeshInstance3D` under each root
  registers into its own world).
- **MCP editor automation** exercises the real GUI path for the corruption class unit tests
  miss (focus switches, cross-tile selection/inspector isolation, per-scene state round-trip
  across tile moves and restarts). Where the automation surface is insufficient, extend
  `editor/automation/` as part of the unit that needs it.

## 6. Retirement & salvage

- **Retire (delete after mining):** `csueiras/multi-scene-phase-d-liveness-d139`. Read it as a
  map of which members are per-view vs. global and which bugs the big-bang hit; import no
  editor-plugin code from it.
- **Salvage as code (from `feature/multi-scene-workspace`, #905):** the container files
  (`editor_scene_workspace.*`, `editor_scene_pane_tile.*`, `editor_tile_drop_overlay.*`),
  persistence scaffolding, focus-routing entry points, `EditorData` tile-id bookkeeping — all in
  new files that do not collide with Tracks 1–2.
- **No toe-stepping**, because Tracks 1–2 (editor-plugin refactors) and Track 3 (new container
  files) touch disjoint files, and the Phase D branch is retired rather than maintained in
  parallel. The only thing incompatible with Tracks 1–2 is keeping the old branch alive.

## 7. Relationship to existing issues

- **#821** (epic) — unchanged umbrella. This strategy re-sequences its remaining phases.
- **#905** (Phase C) — its container code becomes the U6/U7 salvage source; the issue is closed
  in favor of U6/U7.
- **#906** (Phase D) — superseded; its liveness goals are redistributed across U4/U5/U8/U9 with
  parity-first sequencing. Closed in favor of this decomposition.
- **#918** ("shed the global main-screen model") — this is the concrete, sequenced realization
  of that follow-up; #918 is superseded and closed in favor of the new sub-epic.

## 8. Out of scope (kept deferred from the tiled-workspace design)

- Fully independent per-tile editors (own toolbar/tool/snap, two concurrently-live `edit()`
  targets, forwarding to multiple views).
- Tear-off tiles to a separate `Window`/monitor; unifying `EditorDockManager`.
- Game/other main-screen modes as per-pane leaves (Game stays a single global screen); only
  scenes (U14) and scripts (U15) are pane content in this epic.
- Full per-leaf script-editor de-globalization (many distinct scripts in distinct leaves at once);
  U15 delivers script-as-leaf + cross-pane drag first and may stage the deeper extraction.

## 9. Open questions

None blocking. Whether U6/U7 land directly on `develop` behind a feature flag or on a
short-lived integration branch is a delivery detail decided at U6 time; the default is directly
on `develop` since each unit is parity-verified.
