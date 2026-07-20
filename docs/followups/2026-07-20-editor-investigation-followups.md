# Editor / Foundry Script investigation follow-ups

Date: 2026-07-20  
Context: exploratory pass over the Foundry editor + `modules/foundry_script`.  
Active workstream (this branch): **A — Profile/fix docked scene-switch hitch**.

This file parks the other digs so they are not lost.

---

## B. Bug hunt from existing review docs

### B1. Per-leaf script editor (`pr-1016-review.md`)

- [ ] ClassDB / public `ScriptEditor` API mismatch after `ScriptEditorController` + `ScriptEditorView` split
- [ ] Global signals connected per script view → duplicate work / wrong-view handling with multiple leaves
- [ ] Debugger `script_menu` dangling after leaf collapse (`memdelete` path in workspace)
- [ ] Layout/cache restore only correct for single-view sessions

### B2. Per-tile scene tree context (`review-scene-tree-context-fixes.md`)

- [ ] Scene-root override cache with weak/no invalidation when `update_tree()` is skipped on non-focused docks
- [ ] Same-bug-class leftovers: `add_root_node` still global-active-scene; reconcile path focused-tile accessor; scene-mode orphan if focus signal skipped
- [ ] Strengthen tests so they actually exercise the multi-tile interaction they name

---

## C. Broader editor quality pass

- [ ] Finish / advance deglobalization of shared `CanvasItemEditor` / `Node3DEditor` per `docs/superpowers/specs/2026-07-04-editor-view-deglobalization-strategy.md` (root cause of black panes / focus bleed)
- [ ] Focus-follows-drag focus churn (`editor_scene_pane_tile.cpp`) — may spam the scene-switch cascade while dragging
- [ ] Non-focused tiles on `UPDATE_ALWAYS` LIVE_2D/LIVE_3D previews — ongoing dual-render cost after switches
- [ ] Remote scene tree dock stutter notes in `scene_tree_dock.cpp`
- [ ] Automation gaps: any dock/control without stable role/name that blocks agent verification
- [ ] Visual review galleries for substantial dock/workspace UI changes

---

## D. Foundry Script editor / LSP correctness & perf

- [ ] Align completion type guessing in `fs_editor.cpp` with analyzer flow-narrowing (`fs_analyzer_flow_finality.cpp`)
- [ ] Script enum completion gap (`Script::get_script_enum_list` TODO)
- [ ] LSP `reload_all_workspace_scripts` + per-request parse of non-open files on medium/large projects
- [ ] Strict-settings `invalidate_analysis()` blast radius vs incremental invalidation
- [ ] Compiler subclass `script_type` lifetime note (`fs_compiler.cpp`)
- [ ] `FSCache` re-entrancy during parse/raise_status
- [ ] Hygiene: `script_language_gd` naming, tokenizer magic `GDSC` vs bytecode `FSBC`, stale GDScript docs/links

---

## Workstream A status (this branch)

Landed / in progress on `fix/scene-switch-hitch`:

- [x] Root cause: deferred `_update_all_scene_tabs` always rebuilt pane tabs (`_unmount_active_tab` + remount) and could re-activate the focused scene tab after `_focus_tile_internal` already reparented the shared editor
- [x] `WorkspacePane::sync_scene_tabs_from_editor_data` fast path when membership/order/keys unchanged
- [x] `EditorNode::activate_workspace_scene_tab` early-out when already on that tile/scene with scene-mode hosted there
- [x] Opt-in timing: set `FOUNDRY_PROFILE_SCENE_SWITCH=1` and watch `[scene-switch]` lines from `_focus_tile_internal`
- [x] Regression: `scene-tab-sync-skips-rebuild-when-membership-unchanged`

Still expensive on a real focus switch (expected until deglobalization — see C):

- `_reparent_scene_mode_into` of the shared 2D/3D chrome
- `_set_current_scene_nocheck` (plugin get/set state, viewport detach/LIVE rewire, inspector `_edit_current`, `sdfgi_reset`, plugin notify fan-out)

---

## Suggested order after A

1. **B2** (correctness of multi-tile trees — interacts with focus)  
2. **B1** (script leaves — multi-leaf fan-out amplifies switch cost)  
3. **C** deglobalization / LIVE preview update modes  
4. **D** LSP/completion (independent of workspace tiling)

---

## Also on this branch: #1142 DisplayServer hover warning

Fixed on `fix/scene-switch-hitch` (same session):

- Root cause: `DisplayServerMacOS::mouse_exit_window` cleared `window_mouseover_id` even when the exiting window was not the hovered one, so a later `MOUSE_ENTER` (e.g. Tree rename `Popup`) hit stale `windowmanager_window_over`.
- Fix: only clear hover when `p_window == window_mouseover_id`; use `NSTrackingActiveAlways` on `FoundryContentView` so enter/exit are not tied to first-responder.
