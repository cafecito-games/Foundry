# Design: Full Workspace Tab Model

Status: approved for spec PR
Date: 2026-07-06
Epic: follow-up to #821 multi-scene editing and #951/#1016 per-leaf script editor de-globalization.

## Motivation

The tiled workspace now supports recursive panes and separate scene/script leaves, but it still has
two different tab models:

- scene tabs are workspace-facing and can move/split scene panes;
- script tabs are internal to each `ScriptEditorView` and cannot be moved or split like scene tabs.

That is not the target mental model. The target is: **the workspace owns tabs; panes are generic;
scene and script tabs are peers; the active tab determines what editor chrome is mounted.**

This spec defines that full model. It deliberately keeps the first implementation focused on
scene tabs and script resource tabs, while designing the tab system so later `HelpTab`, `TextTab`,
asset/resource tabs, debugger tabs, and other editor surfaces can be registered without rewriting
pane mechanics.

## Product Decisions

- A workspace pane can host mixed tab types in one tab strip.
- The workspace tab model is the source of truth for tab identity, ordering, active tab, movement,
  splitting, closing, pane collapse, and persistence.
- Scene and script resources are canonical by default: the same resource appears in at most one
  workspace tab. Opening an already-open resource reveals/focuses that existing tab.
- Dragging a tab moves it; it never prompts for dirty state because location changes only.
- Closing a dirty scene/script tab prompts through the tab type's close policy.
- When the final tab leaves or closes from a pane, the pane collapses if another pane exists.
  The final remaining pane stays visible as an empty workspace placeholder.
- Layout persistence restores the split tree, pane tab order, active tab per pane, focused pane,
  split offsets, tab type/resource identity, and tab-type-owned view state.
- No compatibility with old editor workspace layouts is required.

## Architecture

### 1. Generic `WorkspacePane`

Replace the current leaf-content split between `ScenePaneTile` and `ScriptLeaf` with a generic
pane owned by `EditorSceneWorkspace`.

A pane contains:

```text
WorkspacePane
  WorkspaceTabBar
  WorkspaceChromeHost
    active tab chrome/content
  EditorTileDropOverlay / generic drop overlay
```

The pane does not know scene/script internals. It delegates behavior through the active
`WorkspaceTabType`.

The recursive workspace tree stays: `WorkspaceSplitNode` branches and `WorkspaceLeafNode` leaves
remain the structural model. The leaf content becomes `WorkspacePane` instead of being typed as a
scene pane or script leaf.

### 2. Typed `WorkspaceTab`

A workspace tab is a serializable record:

```text
WorkspaceTab {
  stable_id
  type_id
  resource_key
  title cache
  icon key/cache
  persisted_payload
}
```

`resource_key` is canonical within a tab type. For the first implementation:

- `SceneTab`: `resource_key = scene resource path` for saved scenes, or a generated unsaved-scene
  key for unsaved scenes.
- `ScriptResourceTab`: `resource_key = script resource path`.

The workspace maintains a canonical index from `(type_id, resource_key)` to a tab location. Opening
an existing key focuses the current location rather than creating a duplicate.

### 3. Tab Type Registry

Each tab type registers a small behavior surface:

```text
WorkspaceTabType {
  type_id
  can_open(resource)
  make_tab(resource)
  get_title(tab)
  get_icon(tab)
  mount(tab, pane_chrome_host)
  unmount(tab)
  activate(tab)
  request_close(tab) -> close/cancel/deferred
  save_payload(tab)
  restore_payload(tab, payload)
}
```

This keeps workspace mechanics generic and lets future tab types plug in without changing drag,
split, close, or persistence code.

### 4. Dynamic Pane Chrome

The active tab type owns the mounted chrome.

For `SceneTab`, the pane mounts scene editor chrome:

- scene tab body with scene tree, viewport, inspector, and scene-related docks;
- focused scene compatibility: the active `SceneTab` in the focused pane is the global current
  edited scene;
- existing scene editor APIs continue to resolve through the focused scene tab.

For `ScriptResourceTab`, the pane mounts script editor chrome:

- script editor controls/content for the active script;
- script file list, members outline, and search controls scoped to the workspace's open
  script-resource tabs;
- global script actions route to the focused script tab/view.

The tab strip remains stable while switching between scene and script tabs; only the mounted chrome
below the tab strip changes.

### 5. Scene Integration

`SceneTab` owns the workspace identity for an open scene. `EditorData` still owns scene edit state,
scene root, unsaved state, undo history, and compatibility accessors.

The scene tab type bridges the two models:

- open scene -> create or reveal `SceneTab`;
- activate `SceneTab` -> set the matching `EditorData` scene current and activate its
  `EditorSceneContext`;
- close `SceneTab` -> run existing scene close/save/discard flow;
- move `SceneTab` -> update workspace tab location only; scene edit state remains attached to the
  tab's scene identity.

The current `tile_id` scene membership model becomes tab-location-derived. A scene's effective
pane is the pane containing its `SceneTab`.

### 6. Script Integration

`ScriptResourceTab` owns the workspace identity for an open script resource. `ScriptEditorController`
continues to own global script services: find-in-files, debug menu integration, autosave, recent
scripts, documentation registries, and focused-view routing.

The script tab type bridges to script editor UI:

- open script -> create or reveal `ScriptResourceTab`;
- activate `ScriptResourceTab` -> mount or focus the editor for that script;
- close `ScriptResourceTab` -> run existing script dirty/save/discard flow;
- move `ScriptResourceTab` -> move only workspace ownership.

The current per-`ScriptLeaf` `ScriptEditorView::TabContainer` must stop being a top-level
workspace tab owner for scripts. During migration, script editor internals may keep helper
containers, but the visible workspace tab strip is the authoritative tab model.

### 7. Drag, Split, and Collapse

All workspace tabs use one drag/drop path.

- Center drop on a pane: move tab into that pane at the drop index.
- Edge drop on a pane: split the target pane and move the tab into the new pane.
- Reorder within a pane: move tab within that pane's list.
- Move final tab out of a pane: collapse the emptied pane if another pane exists.
- Close final tab in a pane: collapse the pane after close succeeds if another pane exists.
- Final empty pane: stay visible with an empty placeholder offering common open actions.

The current scene-tab rosette/drop overlay becomes generic and works for every registered
workspace tab type.

### 8. Persistence

Persist a nested workspace tree:

- split orientation and divider offsets;
- pane ids;
- pane tab ordered list;
- active tab id per pane;
- focused pane id;
- each tab's `type_id`, `resource_key`, `stable_id`, and type payload.

Each tab type owns its payload:

- `SceneTab`: scene path or unsaved key. Scene edit state remains in `EditorData` and existing
  per-scene edit-state files; the workspace payload stores no duplicate scene edit state.
- `ScriptResourceTab`: script path plus caret/scroll/fold state that belongs to that editor tab.

Old layout compatibility is intentionally out of scope. If the old config cannot be read as the new
tree, the editor starts with a clean single empty pane.

## Public Semantics

The focused-pane compatibility rule stays:

- If the focused pane's active tab is a `SceneTab`, scene editor APIs resolve to that scene.
- If the focused pane's active tab is a `ScriptResourceTab`, script editor APIs resolve to that
  script.
- APIs that require a scene while a script tab is focused use the most recently focused scene tab
  as a compatibility fallback, matching the stabilization work from PR #1016.

Plugin forwarding and `EditorInterface` methods must document focused-tab semantics explicitly.

## First Implementation Scope

The first implementation includes:

- generic pane and workspace tab model;
- `SceneTab`;
- `ScriptResourceTab`;
- canonical open/reveal behavior for scenes and scripts;
- generic drag/reorder/center-drop/edge-drop;
- tab close and pane collapse rules;
- persistence for mixed scene/script panes;
- automation and unit coverage for mixed workspace workflows.

Explicitly deferred typed tabs:

- `HelpTab`;
- `TextTab`;
- arbitrary resource/asset tabs;
- debugger/output/tool tabs;
- tear-off windows and arbitrary dock-manager integration.

The registry is still designed so these can be added as new tab types later.

## Work Breakdown

### W1. Workspace Tab Model and Registry

Introduce `WorkspaceTab`, `WorkspaceTabType`, canonical tab keys, tab locations, and a registry.
Add unit coverage for canonical identity, open/reveal behavior, reorder within a pane, and
serialization of tab records.

### W2. Generic Workspace Pane

Replace scene/script leaf content with `WorkspacePane`: one tab strip, one chrome host, empty-pane
placeholder, active-tab switching, and pane focus. Preserve the recursive split tree.

### W3. SceneTab Integration

Move scene tab ownership into the workspace tab model while preserving `EditorData` scene edit
state and focused-scene compatibility. Existing scene open/close/save/tab commands route through
`SceneTab`.

### W4. ScriptResourceTab Integration

Move visible script resource tabs into the workspace tab model. `ScriptEditorController` remains
global, but script resource tab identity, movement, close, and persistence belong to the workspace.

### W5. Generic Drag, Split, Close, and Collapse

Generalize the tab drag/drop overlay and operations so all workspace tab types can reorder, move,
edge-split, close, and trigger pane collapse through one path.

### W6. Mixed Workspace Persistence

Persist and restore mixed pane layouts: split tree, pane tab order, active tabs, focused pane,
stable tab ids, type ids, resource keys, and type-owned payloads.

### W7. Command Routing and Public API Semantics

Route scene/script open commands, shortcuts, global menu actions, and `EditorInterface` accessors
through the focused tab model. Document focused-tab semantics and most-recent-scene fallback.

### W8. Tests and Editor Automation

Add C++ unit tests and MCP/editor automation for:

- opening scene/script resources into canonical tabs;
- dragging scene and script tabs between panes;
- edge-drop splitting for both tab types;
- closing dirty/clean tabs and canceling close;
- pane collapse after final tab close/move;
- restart persistence of mixed layouts;
- focused-tab scene/script command routing.

### W9. Future Tab Type Follow-up

Design and implement the first non-scene/script follow-up tab type after the core model lands,
preferably `HelpTab` or `TextTab`, to prove the registry extension path.

## Risks

- **Two sources of truth during migration.** Scene and script editors already own tab containers.
  The migration must make workspace tabs authoritative and avoid long-lived mirrored state.
- **Dirty-close behavior.** Existing save/discard/cancel flows must remain type-owned and must not
  be bypassed by pane collapse.
- **Focused-scene compatibility.** Many editor systems assume a current edited scene. The
  most-recent-scene fallback must be explicit and tested while script tabs are focused.
- **Spec creep into arbitrary docking.** This is a document/workspace tab model, not a full
  dock-manager replacement. Tool docks and tear-off windows remain deferred.

## Acceptance Criteria

- A single pane can contain `level.tscn` and `player.fs` in one tab strip.
- Switching active tabs swaps pane chrome between scene editor and script editor surfaces.
- Opening an already-open scene/script focuses its existing tab.
- Dragging a scene or script tab to a pane center moves it.
- Dragging a scene or script tab to a pane edge splits the pane and moves it.
- Closing/moving the final tab collapses the emptied pane when another pane exists.
- The final empty pane remains visible with an empty placeholder.
- Dirty scene/script close prompts are preserved.
- Mixed scene/script workspace layouts restore after editor restart.
- The model can add `HelpTab` or `TextTab` later without changing pane drag/split/close mechanics.
