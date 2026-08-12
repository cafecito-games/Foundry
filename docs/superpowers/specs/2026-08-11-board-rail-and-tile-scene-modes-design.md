# Board Rail and Tile-Local Scene Modes

## Summary

Foundry's title bar currently presents two unrelated navigation systems: the
centered main-screen buttons (`2D`, `3D`, `Game`, and other registered screens)
and a board switcher made from one ordinary button per board. Boards now define
the editor's top-level workspace, while 2D and 3D describe how an individual
scene tile is being edited. The chrome should express that hierarchy.

This change replaces the visible main-screen buttons with a centered, themed
**Board Rail**. It also moves 2D/3D selection into every scene tile and makes
that selection persistent per tile. Two tiles may therefore show different
modes at the same time, and switching boards or tile focus restores the mode of
the destination tile.

The underlying `EditorMainScreen` plugin machinery remains intact. Foundry
continues to use the shared full 2D or 3D editor for the focused tile and the
existing secondary live views for non-focused tiles. The new state model makes
the effective mode tile-local without duplicating either full editor.

## Goals

- Make named boards the only persistent workspace navigation in the title bar.
- Optimize the Board Rail for two to six boards and keep switching one click.
- Put secondary board operations behind one trailing menu.
- Give every scene tile an independent, persistent 2D/3D mode.
- Infer a new tile's initial mode from its first active scene, then keep the
  user's choice sticky across scene-tab, focus, board, and editor-session
  changes.
- Preserve editor shortcuts, command-palette commands, plugin APIs, global
  screens, accessibility, feature profiles, and theme scaling.

## Non-goals

- Duplicating the full `CanvasItemEditor` or `Node3DEditor` per tile.
- Adding Script or Game to the tile-local mode control. Scripts remain workspace
  leaf content; Game remains a global screen reached by the run flow, shortcut,
  command palette, or plugin API.
- Changing what a board owns, the board transition animation, overview
  geometry, or cross-board drag behavior.
- Adding arbitrary icons, colors, or per-board customization.
- Optimizing the full rail for dozens of boards. Narrow-window fallback covers
  constrained space; large board collections remain better served by overview.

## User Experience

### Board Rail

The title bar's centered control is a single low-contrast capsule containing
one text segment per board and a stable trailing menu button. The active segment
uses a subtle raised fill and a short theme-accent indicator. Inactive segments
have no permanent container fill; hover adds a quiet translucent fill.

Board titles are the visual focus. The current standalone add and overview
buttons are removed. The trailing menu contains:

1. New Board
2. Board Overview
3. Rename the active board
4. Move Left / Move Right
5. Close Board / Close Other Boards

Clicking an inactive board switches immediately. Clicking the active board is
normally inert and leaves it selected. The dedicated menu opens immediately,
so the current delayed active-button popup and its double-click cancellation
timer are deleted. Double-clicking a board title still starts inline rename,
and right-clicking any title still opens the menu targeted at that board.

When a global screen such as Game is visible, clicking any Board Rail segment,
including the active one, exits the global screen and reveals that board's
focused tile in its remembered mode. This is the visible return path after the
main-screen buttons leave the title bar.

Board segments use their intrinsic label width up to a themed maximum and then
ellipsize, with the full title in the tooltip and accessibility name. When the
title bar cannot fit the complete rail, it collapses to one active-board segment
plus a disclosure button. The disclosure lists every board followed by the same
management commands. Returning to sufficient width restores the full rail
without changing selection.

### Tile-local 2D/3D control

Every `ScenePaneTile` places a compact two-segment `2D | 3D` control at the right
edge of its scene-tab strip. It is part of the tile rather than the reparented
scene editor, so it remains spatially stable when focus changes and is visible
on live, non-focused previews.

Clicking a mode first records the choice on that tile, then focuses the tile.
The focus transition activates the matching full editor and reparents it into
the tile through the existing path. Other tiles do not change mode. Existing
Open 2D Workspace and Open 3D Workspace shortcuts and command-palette actions
now set the focused scene tile's mode instead of changing unowned global state.

A new tile has no user choice until it receives its first current scene. At that
point it selects 3D when the scene contains 3D content and 2D otherwise. The
selection then belongs to the tile: changing scene tabs or selecting a node of a
different dimensional type does not switch it automatically. An empty tile's
control is disabled until a scene becomes current.

The control only exists on scene tiles. Script/text leaves do not display it.
When a feature profile disables 3D, the tile resolves to 2D and hides the
now-redundant single-choice control. Re-enabling 3D does not invent a prior 3D
choice for tiles that were persisted as 2D.

## Architecture

### Title-bar ownership

`EditorNode` makes `EditorBoardSwitcher` the title bar's center control instead
of `EditorMainScreen`'s button container. The switcher's current placement after
the run controls is removed.

`EditorMainScreen` still registers all main-screen plugins and owns their
selection state. Its button container remains instantiated as a hidden internal
control rather than visible title-bar chrome. This preserves the existing
button-indexed implementation, feature-profile enablement, plugin registration,
shortcuts, `EditorInterface::set_main_screen_editor()`, Game auto-selection, and
layout compatibility without presenting duplicate navigation.

The internal 2D/3D selection is no longer authoritative by itself. Whenever a
scene tile becomes focused, `EditorNode` synchronizes `EditorMainScreen` from
that tile before reparenting the shared scene-mode surface. Global screens stay
authoritative while visible.

### `EditorBoardSwitcher`

`editor/gui/editor_board_switcher.{h,cpp}` remains a stateless projection of
`EditorBoardStrip`. It continues rebuilding from strip signals because the
expected list is small and complete rebuilds cannot drift from board order.

The rebuild creates:

- one themed, ellipsizing toggle button per board;
- one dedicated menu button;
- a hidden/collapsed active-board picker used only when width is insufficient;
- the existing `EditorBoardActionsMenu`, expanded with New Board and Board
  Overview commands.

Opening the trailing menu targets the active board by `ObjectID`. Opening it by
right-click targets the clicked board. All board-specific commands resolve that
identity at activation time, preserving the existing safe behavior when a
board closes while a menu is open.

The switcher removes `pending_menu_board_id`, `pending_menu_timer`, and
`BOARD_ACTIONS_MENU_OPEN_DELAY_SEC`. Inline rename continues to use an
identity-keyed target. Collapsed/full presentation is view state only and is
recomputed from available width; it is not persisted.

### `EditorSceneModeSwitcher`

A small new control in `editor/gui/editor_scene_mode_switcher.{h,cpp}` owns two
toggle buttons, their accessible labels, and themed selected/hover/focus states.
It exposes `set_mode()` and `set_3d_enabled()` and emits `mode_selected(mode)`.
It contains no editor or scene state.

`EditorSceneTabs` gains `add_extra_control(Control *)`; the existing
`add_extra_button(Button *)` remains as a compatibility wrapper. Each
`ScenePaneTile` creates one `EditorSceneModeSwitcher` and mounts it through this
extra-control slot. The method inserts the control before the opened-scenes
menu, regardless of when the caller mounts it, so tile construction order does
not alter the final tab-strip layout.

### `ScenePaneTile` state

`ScenePaneTile` owns:

```text
SceneEditorMode scene_editor_mode
bool scene_editor_mode_initialized = false
EditorSceneModeSwitcher *scene_mode_switcher
```

`scene_editor_mode` is read only when `scene_editor_mode_initialized` is true;
before then the switcher is disabled and first-scene inference supplies the
value.

The exact enum lives beside `ScenePaneTile`; it is distinct from
`TilePreviewMode`. `SceneEditorMode` is the user's durable choice.
`TilePreviewMode` remains presentation state describing whether the tile hosts
the focused shared editor or a secondary live surface.

The tile provides:

- `set_scene_editor_mode(mode, user_initiated)`;
- `initialize_scene_editor_mode(EditorSceneContext *)`;
- `get_scene_editor_mode()`;
- `is_scene_editor_mode_initialized()`;
- a `scene_editor_mode_requested(tile_id, mode)` signal for `EditorNode`.

`user_initiated` is not persisted separately. It distinguishes a click or
shortcut, which requests focus and editor synchronization, from restore and
initial inference, which update state without stealing focus.

### Focus and display synchronization

The focused tile continues to host the single full `EditorMainScreen` scene
surface. The focus path becomes:

1. A tile mode click records the new `SceneEditorMode`.
2. The tile requests leaf focus.
3. `EditorNode` resolves the focused tile's initialized mode.
4. `EditorNode` selects internal `EDITOR_2D` or `EDITOR_3D`.
5. The shared scene surface is reparented into the focused tile.
6. `_update_tile_display_attachments()` promotes the tile and updates every
   non-focused live preview.

Focus caused by clicking a tile, switching boards, restoring layout, moving a
tab, or using previous/next-board shortcuts enters at step 3 and therefore
restores the same tile-owned state.

The current selection-driven call to
`editor_data.get_handling_main_editor(selected_node)` must not overwrite an
initialized tile mode. It may participate only in first-scene inference. A 3D
node selected while a tile is in 2D remains selected without forcing the tile
to 3D, and vice versa.

### Non-focused live previews

`EditorNode::_update_tile_display_attachments()` currently chooses a
non-focused tile's live 2D or 3D surface from
`EditorSceneContext::scene_has_3d_content()`. It will instead initialize the
tile if needed and branch on `tile->get_scene_editor_mode()`.

The existing `CanvasItemEditorView` and secondary `Node3DEditorViewport` remain
lazy and are reused when a tile switches back to a previously visited mode.
Changing mode refreshes attachments for the affected scene context and does not
recreate unrelated tile views. Overview shrink and throttling continue to
operate on both types through the existing `TilePreviewMode` paths.

## Persistence and Compatibility

`ScenePaneTile::save_layout()` writes `scene_editor_mode` as the stable string
`"2d"` or `"3d"` in that tile's existing layout section.
`load_layout()` accepts those values. A missing or invalid key leaves
`scene_editor_mode_initialized` false, so layouts written before this feature
receive normal first-scene inference. No layout-version migration is required.

The old `selected_main_editor` layout key remains readable and writable for
global-screen and extension compatibility. On scene-workspace restore, the
active board's focused tile mode wins over a saved `2D` or `3D` main-screen
name. A saved global screen name still restores that global screen. `Script`
continues to reveal the script leaf through the existing compatibility path.

Main-screen plugins remain registered even though their buttons are hidden.
Game still opens automatically when configured by `GameViewPlugin`; Asset
Library and third-party screens remain reachable through their existing
commands, shortcuts, and `EditorInterface`. The Board Rail remains visible on a
global screen so the user can return to a board.

## Theme, Accessibility, and Input

Board Rail and tile-mode visuals are defined through editor theme variations,
styleboxes, fonts, icons, constants, and `EDSCALE`; no control hard-codes the
mockup palette. Light and dark themes must both expose distinguishable normal,
hovered, pressed, keyboard-focus, and disabled states.

Each board button exposes its full title and selected/toggle state. The trailing
button is named `Board Menu`; the collapsed control is named with the active
board. Tile buttons are named `2D Scene Mode` and `3D Scene Mode`, and the group
exposes the owning tile through its accessible description.

Keyboard focus uses the existing editor convention (`FOCUS_ACCESSIBILITY`) for
title-bar chrome. Editor shortcuts continue to work when no control has focus.
Clicking the tile-local control participates in the tile's normal focus request
and cannot leave the full editor mounted in a different tile.

## Error Handling and Edge Cases

- Menu and rename targets resolve by `ObjectID`; a freed board is a no-op.
- A mode request for a removed or dormant tile is ignored unless normal board
  activation first makes that tile focusable.
- Closing the focused tile promotes its successor, then applies the successor's
  remembered mode before reparenting the shared surface.
- Moving a scene tab between tiles does not move mode state. Mode belongs to the
  destination tile, not the scene or tab.
- An empty tile disables its mode control and cannot change the internal main
  screen until it owns a current scene.
- If 3D becomes unavailable while a 3D tile is visible, the tile resolves to 2D
  through the normal mode path before its secondary 3D view is hidden.
- Narrow/full Board Rail transitions never commit or cancel an active inline
  rename silently. A pending rename is committed before presentation rebuild,
  matching structural rebuild behavior.

## Testing

### Headless C++ tests

Update `tests/editor/test_editor_board_switcher.h` to cover observable behavior:

- the rail mirrors two to six board titles and marks one active segment;
- an inactive segment switches immediately;
- the active segment is inert in scene mode and exits a global screen through
  the integration path;
- the dedicated menu opens immediately for the active board;
- New Board and Board Overview work through the menu;
- right-click targeting, inline rename, reorder, close, and freed-target safety
  remain intact;
- narrow/full presentation preserves active selection and board identity.

Add focused tests for `ScenePaneTile` and `EditorSceneModeSwitcher`:

- first-scene inference selects 2D or 3D from observable scene context data;
- a manual choice stays unchanged when the current scene tab changes;
- save/load round-trips independent modes for two tiles;
- a missing or invalid persisted mode remains uninitialized for inference;
- the mode switcher emits exactly one request and reflects feature-profile
  disablement;
- changing `TilePreviewMode` does not mutate `SceneEditorMode`.

### Editor integration and automation

Extend the editor automation acceptance workflow to:

1. Create a split board with two scene tiles.
2. Put the left tile in 2D and the right tile in 3D.
3. Switch focus repeatedly and assert the internal main editor follows the
   focused tile.
4. Assert the non-focused tile keeps the corresponding `LIVE_2D` or `LIVE_3D`
   presentation.
5. Switch boards and return, verifying both tile modes are unchanged.
6. Save and restore the layout, then verify the same state.
7. Change scene tabs and select dimensionally different nodes without an
   automatic mode flip.
8. Open Game and use the active Board Rail segment to return to the remembered
   tile mode.

Tests assert state, signals, visibility, and rendered control semantics. They do
not inspect source text or Markdown.

## Visual Review

This is substantial editor UI work. Implementation handoff includes a remote
review gallery built with `scripts/review_gallery.py`:

- a `design` board comparing the old main-screen buttons and board buttons with
  the new centered Board Rail;
- a `proof` board showing two tiles simultaneously in independent 2D and 3D
  modes, plus the trailing board menu and narrow-width fallback;
- captions that state the exact visual or behavioral property to confirm.

Screenshots come from the real editor through its automation MCP, not from the
brainstorming mockups.

## Verification

Implementation verification uses the repository's agent build wrapper. During
iteration, focused BoardSwitcher, workspace, and editor automation suites may
run through the Ninja backend. Before handoff, run the native strict validation
build through `python3 scripts/agent_build.py`, followed by the relevant focused
tests and the GUI-dependent acceptance workflow with `DISPLAY=:1` where
required. The final handoff reports both command output and gallery location.
