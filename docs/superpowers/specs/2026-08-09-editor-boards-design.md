# Editor Boards

## Summary

Today the editor has exactly one tiling workspace. `EditorSceneWorkspace` is
instantiated once in `EditorNode` (`editor/editor_node.cpp:11040`) and reached
everywhere through the `EditorNode::get_scene_workspace()` singleton accessor
(`editor/editor_node.h:882`). Everything the user arranges — the split tree, the
panes, the per-tile Scene Tree/Inspector/Signals/Groups/History docks, the pane
tab strips — lives inside that one instance.

A **board** is one whole such arrangement. This feature turns the single
workspace into an ordered list of boards, adds a title-bar switcher with an
animated horizontal slide between them, and adds a zoomed-out overview in which
every board shrinks into a live filmstrip that panes can be dragged between and
that boards themselves can be reordered in.

The naming is deliberate: `board` is unused in both the engine and this fork,
pluralizes cleanly in menus ("Move Pane to Board >", "Show All Boards"), and does
not collide with `workspace`, `pane`, `leaf`, `tile`, `screen`, `panel`, or
`layout`, all of which already mean something specific here.

### Scope

A board owns exactly one `EditorSceneWorkspace` and nothing else. The FileSystem
dock, Import dock, bottom panel and Log, the menu bar, and the run controls stay
global and single-instance. There is no per-board bottom-panel state and no
per-board FileSystem state.

Explicitly out of scope:

- Detaching a board into its own OS window. That is the `WindowWrapper` path and
  a separate problem.
- Any startup behavior beyond restoring what the user had.

## Architecture

Five new units across four file pairs. Everything at or below
`EditorSceneWorkspace` is unchanged.

```
center_overlay
 └─ top_split (VSplitContainer)
     └─ srt (VBoxContainer)
         └─ EditorBoardStrip          ← replaces scene_workspace at editor_node.cpp:11040
             ├─ EditorBoard  "Board 1"
             │   └─ EditorSceneWorkspace   (unchanged)
             ├─ EditorBoard  "face shader"
             │   └─ EditorSceneWorkspace
             └─ EditorBoard  "Board 3"
                 └─ EditorSceneWorkspace
```

### `EditorBoard` — `editor/editor_board.{h,cpp}`

A `Container` owning exactly one `EditorSceneWorkspace` child plus the board's
identity: a stable `board_id`, a user-editable `title`, and `focused_leaf_id`,
the leaf its workspace had focused when the user last left it.

Its one behavioral method is `set_dormant(bool)`. Dormant is implemented as
`hide()` plus `PROCESS_MODE_DISABLED` on the workspace subtree — see
[Performance](#performance) for why that specific mechanism and not
`SubViewport` update-mode gating.

Depends on: `EditorSceneWorkspace`, `EditorSelection`, `EditorData`.

### `EditorBoardStrip` — `editor/editor_board_strip.{h,cpp}`

Owns the board list and is the only unit that knows board geometry. Three
responsibilities:

1. Lay boards edge-to-edge horizontally, each sized to the full editor rect.
2. Own the single `next_leaf_id` allocator, so leaf ids are unique editor-wide.
3. Apply the view transform produced by `EditorBoardView`.

Public surface: `get_board_count()`, `get_board(index)`, `get_active_board()`,
`set_active_board(index)`, `add_board()`, `close_board(index)`,
`move_board(from, to)`, `set_overview(bool)`, `is_overview_active()`,
`allocate_leaf_id()`, and `find_leaf_by_id(id)` for cross-board lookup.

Signals: `board_added`, `board_removed`, `board_moved`, `active_board_changed`,
`overview_entered`, `overview_exited`.

### `EditorBoardView` — `editor/editor_board_view.{h,cpp}`

A `Control`-free value type holding `{ scroll_x, scale, active_index }`, the
tween that animates between states, and the geometry math: index to scroll
offset, overview scale from board count and viewport size, and board-index-at-
point.

It is split out from the strip specifically so the geometry is unit-testable
headlessly, the same way `EditorSceneWorkspace::drop_region_at` and
`drop_preview_rect` already are (`editor/editor_scene_workspace.h:217`).

Depends on: nothing but math types.

### `EditorBoardSwitcher` — `editor/gui/editor_board_switcher.{h,cpp}`

Title-bar chrome, inserted after `project_run_bar` (`editor/editor_node.cpp:11311`).
One button per board showing its title, inline rename on double-click, a `+` to
add a board, and a toggle for the overview.

Depends on: `EditorBoardStrip` (reads the list, calls the mutators). It holds no
state of its own.

### `EditorBoardOverviewLabels` — part of `editor/editor_board_strip.cpp`

The row of board captions drawn above the shrunk boards in overview mode. It is
a sibling overlay of the strip, positioned from `EditorBoardView` geometry, and
is deliberately **not** scaled with the boards: captions stay crisp and
clickable at full font size while the boards behind them shrink.

### What changes in existing code

`EditorNode::get_scene_workspace()` keeps its signature and returns the *active*
board's workspace. All 15 existing call sites (main screen, file system, script
editor plugin and controller, automation) mean "the workspace the user is
looking at", so their behavior is unchanged. Cross-board work goes through a new
`EditorNode::get_board_strip()`.

`EditorSceneWorkspace` changes in exactly two ways:

- `save_to_config` / `restore_from_config` / `has_workspace_session` take a
  section name instead of reading the private `WORKSPACE_CONFIG_SECTION`
  constant.
- `handle_tab_drop` accepts a source pane and target leaf from another board.
  See [Cross-board drag](#cross-board-drag).

Leaf ids move from per-workspace allocation (`next_leaf_id++`,
`editor/editor_scene_workspace.cpp:544` and `:569`) to the strip's allocator.

`EditorMainScreen` remains a single instance. There is exactly one focused tile
in the whole editor, and it always lives on the active board.

## Data flow

### Board switch

`EditorBoardSwitcher`, or the `editor/previous_board` and `editor/next_board`
shortcuts (registered through `ED_SHORTCUT_AND_COMMAND` so they are rebindable
and reachable from the command palette; default `CMD_OR_CTRL | ALT | LEFT` and
`| RIGHT`, both currently unbound), calls
`EditorBoardStrip::set_active_board(index)`. The strip:

1. Wakes the incoming board (`set_dormant(false)`) **before** starting the
   tween, so it renders during the slide.
2. Tweens `EditorBoardView::scroll_x`.
3. On completion, puts the outgoing board dormant.
4. Asks the incoming board for its `focused_leaf_id` and calls
   `request_leaf_focus()` on that board's workspace.

Step 4 lands in the existing `EditorNode::_on_leaf_focus_requested`, which
reparents `EditorMainScreen` into the newly focused tile through the path that
already exists. From `EditorNode`'s perspective a board switch is
indistinguishable from clicking a different tile, so no new focus machinery is
introduced.

### Cross-board drag

`EditorSceneWorkspace::handle_tab_drop` (`editor/editor_scene_workspace.cpp:143`)
already performs one of two board-agnostic operations:

- A **scene tab** moves by `editor_data->set_scene_tile(scene_idx, dest_leaf_id)`
  followed by `sync_scene_tabs_from_editor_data()`. `EditorData` is global and
  leaf ids are globally unique, so the ownership transfer is already correct
  across boards.
- A **non-scene tab** (script, help, text) moves by `source_pane->take_tab()` →
  `dest_pane->add_tab()`, a pure data move with no workspace-level state.

The delta is therefore three changes, not a new subsystem:

1. Resolve `p_source_pane_id` through `EditorBoardStrip::find_leaf_by_id()`
   instead of only the local `leaves` vector.
2. Relax `ERR_FAIL_COND_V(!leaves.has(p_target_leaf))` to "the target belongs to
   some board".
3. After a cross-board move, run `sync_scene_tabs_from_editor_data()` and
   `collapse_if_empty_deferred()` on **both** the source and destination
   workspaces. Today only one workspace exists, so only one is synced; without
   this the source board keeps showing a tab for a scene it no longer owns.

`EditorTileDropOverlay`, the rosette hit-testing, and `drop_region_at` need no
changes: in overview mode the target board is a live, scaled `Control`
receiving ordinary mouse events, and Godot's `Viewport::_gui_input_event`
hit-tests through `get_global_transform_with_canvas()`. This is the specific
reason the strip uses a canvas transform rather than per-board `SubViewport`s —
drag-and-drop state lives on `Viewport`, so a viewport boundary between boards
would make cross-board drag impossible without reimplementing drag-and-drop.

### Overview

`EditorBoardStrip::set_overview(true)`:

1. Wakes every board.
2. Resizes each board's preview `SubViewport`s to their on-screen size (see
   [Performance](#performance)).
3. Tweens `EditorBoardView::scale` down and `scroll_x` to centre the active
   board.
4. Fades in the caption overlay.

While overview is active, the strip suppresses board-internal focus changes:
clicking inside a shrunk board selects *that board*, it does not retarget the
main screen mid-animation. Dropping a pane, or clicking a caption, exits
overview onto that board.

### Board reorder

Dragging a board's caption in overview reorders it. The drag uses the same
`EditorBoardView` index-at-point math as caption clicking; the drop calls
`EditorBoardStrip::move_board(from, to)`, which reorders the child list and
retunes `scroll_x` so the dragged board stays under the cursor. No workspace
state is touched — board order is presentation only.

### Create and close

`add_board()` allocates a `board_id` and a fresh leaf id, builds an
`EditorBoard` around `EditorSceneWorkspace::create_single_leaf_workspace()`, and
registers its initial tile with `EditorData::register_tile` — the same sequence
`EditorNode` runs today at `editor/editor_node.cpp:11064-11070`.

`close_board(index)` walks the board's leaves and routes each through the
existing scene-close path, so unsaved-changes prompts fire per scene and a
cancel at any prompt aborts the whole board close, leaving the board intact.
Closing the last remaining board is refused, mirroring how the sole leaf is
never collapsed.

## Persistence

State goes through `EditorLayoutStore`, which already has a versioned migration
framework (`CURRENT_VERSION`, `run_migrations`, `editor/editor_layout_store.h:61`).

The per-leaf schema does not change at all. Because leaf ids are globally
unique, `leaf_layout_section()` still produces collision-free section names
across every board, so `WorkspacePane` tab serialization — scene tabs, script
leaves, help tabs, text tabs — persists exactly as today.

```ini
[Boards]
board_count = 3
active_board = 1
next_leaf_id = 9
board_0_id = 0
board_0_title = "Board 1"
board_0_focused_leaf = 0
board_1_id = 1
board_1_title = "face shader"
board_1_focused_leaf = 4
board_2_id = 2
board_2_title = "Board 3"
board_2_focused_leaf = 7

[Board_0]
; verbatim the schema previously written under [Workspace]
node_count = 3
root_node = 0
focused_leaf_id = 0
node_0_type = "split"
node_0_vertical = false
; ...

[Board_1]
; ...

[WorkspaceLeaf_0]
; unchanged, globally keyed
[WorkspaceLeaf_4]
; ...
```

`EditorNode::_save_workspace_to_config` (`editor/editor_node.cpp:8198`) and
`_load_workspace_from_config` (`:8211`) become board loops.

**The global steps hoist out of the loop.** `_load_workspace_from_config`
currently does several things that are global rather than per-workspace, and
they must run once around the whole loop, not once per board:

- Detaching the shared scene-mode surface from `EditorMainScreen` *before* any
  old workspace tree is freed. Running this per board would detach and reattach
  repeatedly, and running it after the first board's restore would leave the
  surface parented to a freed tile host.
- `EditorDebuggerNode::detach_remote_scene_tree()` / `rebind_remote_scene_tree()`.
- The final focused-tile resolution and `_bind_all_leaf_docks()`.

Per-board inside the loop: `restore_from_config()`, `_on_leaf_added()` for each
restored leaf, `restore_scene_tile_ownership_from_tabs()`,
`resolve_script_leaf_associated_scenes()`, and `reconcile_empty_leaves()`.

Getting this nesting wrong is the most likely source of a use-after-free in this
change, so it is called out as its own implementation step with its own test.

`next_leaf_id` is persisted explicitly. Restore keeps the existing self-healing
`MAX`-over-restored-ids backstop, now taken across all boards.

### Migration

`EditorLayoutStore` gains a v1 → v2 migration that renames a legacy
`[Workspace]` section to `[Board_0]` and synthesizes a one-board `[Boards]`
section pointing at it. `[WorkspaceLeaf_*]` sections are left untouched, since
their schema and keying are unchanged.

Without this, every developer silently loses their pane layout on first launch
after this merges. The migration framework exists for exactly this case and the
branch is small.

## Performance

The requirement is that switching and the overview feel fluid with no visible
lag on a fully populated editor.

### Dormancy is visibility

`EditorBoard::set_dormant(true)` calls `hide()` and sets
`PROCESS_MODE_DISABLED`. A hidden board skips layout, skips drawing, and every
`SubViewportContainer` beneath it stops rendering through the mechanism Godot
already has.

The rejected alternative was gating `SubViewport::UPDATE_*` flags directly. That
is a trap: `UPDATE_WHEN_VISIBLE` keys off the visibility *flag*, not off-screen
position, so a board parked outside the viewport rect would keep rendering its
3D previews indefinitely while the 2D renderer culled the result — paying full
cost for nothing.

### Steady-state cost is unchanged from today

A single board with four tiles already pays for four live
`CanvasItemEditorView` / `Node3DEditorViewport` surfaces. Adding four dormant
boards adds no rendering.

The same holds CPU-side, for a structural reason: `get_scene_workspace()`
returns the *active* board's workspace, so every existing per-frame loop over
`get_tiles()` (for example `editor/editor_node.cpp:7751`) stays scoped to one
board by construction. Cross-board iteration must be asked for by name through
`get_board_strip()`. Nothing becomes O(boards) by accident.

### Overview is bounded twice

Overview is the only mode where every board is live, and it is bounded two ways:

- **Resolution.** While in overview, each board's preview `SubViewport`s are
  resized to their *on-screen* size rather than their layout size. A board drawn
  at 1/4 scale renders its 3D previews at 1/16 the pixels, so five boards in
  overview cost roughly one board's worth of fill rate. This is the main reason
  a live filmstrip is affordable at all.
- **Cadence.** Overview previews refresh on a throttled tick (~15 Hz) rather
  than every frame. On a miniature this is invisible.

### The scale transform is free

`Control::set_scale()` writes the `CanvasItem` transform. It does not invalidate
layout, minimum sizes, or trigger `NOTIFICATION_SORT_CHILDREN`. The zoom
animation is one matrix per frame regardless of how many panes exist in the
tree. The cost that is paid is text and border rasterization at non-integer
scale — a quality artifact, not a throughput one.

### Known cost: memory

Each board carries a full set of per-tile docks (`SceneTreeDock`,
`InspectorDock`, `SignalsDock`, `GroupsDock`, `HistoryDock` per tile), and those
stay allocated while dormant. Five boards of three tiles is fifteen inspector
instances.

This is accepted, not mitigated: boards are a user-driven feature and the user
is expected to understand that more boards cost more memory. It is documented
here so it is not later diagnosed as a mystery regression, and so that any
future "restore many boards at startup" behavior is understood to require
revisiting this.

## Implementation sequencing

The feature is one design but not one landing. Three stages, each independently
shippable and independently testable:

1. **Boards without motion.** `EditorBoard`, `EditorBoardStrip`, the global leaf
   id allocator, the `get_scene_workspace()` redirection, per-board persistence
   and the v1 → v2 migration, and a plain switcher with instant switching. At
   the end of this stage the editor has N boards and restores them correctly;
   it simply has no slide and no overview. This stage carries all the invariant
   risk (leaf id uniqueness, restore ordering), so it lands and stabilises
   first.
2. **Motion.** `EditorBoardView`, the animated slide, the overview with live
   scaled boards and the caption overlay, the overview resolution and cadence
   bounds, cross-board drag, and board reorder.
3. **Verification surfaces.** Per-board automation capture and the board
   switch / overview automation actions, and the review gallery walkthrough.

## Error handling

- **Closing the last board** is refused, mirroring the existing rule that the
  sole leaf is never collapsed.
- **Cancelling an unsaved-changes prompt** during a board close aborts the whole
  close; the board and every scene in it are left intact.
- **A cross-board drop with an unresolvable source pane** returns `nullptr` from
  `handle_tab_drop`, exactly as an unresolvable same-board drop does today. No
  partial move is performed.
- **A restored config referencing a board that fails to rebuild** falls back to
  the existing per-workspace self-healing: `reconcile_empty_leaves()` collapses
  phantom panes, and a board that restores with no leaves is dropped from the
  list. If every board fails, the strip creates one default board, matching
  today's cold-start behavior.
- **A persisted `active_board` out of range** clamps to the nearest valid index.
- **A persisted `focused_leaf_id` pointing at a script leaf** falls back to the
  first scene tile in that board, reusing the existing rule in
  `_load_workspace_from_config`.

## Testing

The design deliberately puts the tricky math in a `Control`-free class so most
coverage is headless doctests.

**`EditorBoardView` geometry.** Scroll offset for index *n*; overview scale for
*N* boards at a given viewport size; board-index-at-point for caption and click
targeting. Pure functions, same pattern as the existing `drop_region_at` /
`drop_preview_rect` tests.

**Board strip model.** Create, close, reorder. Leaf ids unique across boards —
this is the invariant that silently corrupts `EditorData` scene ownership if it
breaks, so it gets a dedicated test. Refusing to close the last board.

**Persistence round-trip.** Three boards with mixed scene, script, help, and
text tabs, saved and restored to identical trees, tabs, and per-board focus.
Scratch files under `FOUNDRY_TEST_SCRATCH`.

**Migration.** A v1 config carrying a legacy `[Workspace]` section migrates to a
single `[Board_0]` plus a one-board `[Boards]`, with `[WorkspaceLeaf_*]`
sections untouched.

**Restore ordering.** A multi-board restore asserts the scene-mode surface is
detached exactly once before any board rebuild and reattached exactly once
after, guarding the use-after-free identified in
[Persistence](#persistence).

**Cross-board tab drop.** Drive `handle_tab_drop` with the source pane in board
A and the target leaf in board B, asserting that `EditorData` scene-tile
ownership moved, that *both* workspaces re-synced, and that the emptied source
pane collapsed. This is the regression test for the three-change relaxation
described above.

### Editor automation

`EditorAutomationWorkspace::capture_workspace_state` is currently called with
the single workspace at `editor/automation/editor_automation_state.cpp:381`. It
becomes a per-board capture including the active board index, and the automation
surface gains actions to switch boards and toggle the overview.

This is part of this change rather than a follow-up: without it no agent can
verify any of this through the GUI, including the acceptance workflow.

### Visual review

This is a change whose correctness is judged visually, so it ships with a
`scripts/review_gallery.py` **walkthrough** board:

1. Single board, normal editing.
2. A switch caught mid-slide, with the neighbouring board visible at the edge.
3. Overview with every board live.
4. Mid-drag, with the drop rosette lit on a *different* board than the drag
   started in.
5. Landed, back on a single board, with the pane in its new home.

Captions state what the reviewer should confirm, not what the shot is.
