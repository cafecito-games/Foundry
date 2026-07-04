# Handoff — multi-scene split-pane workspace

Branch: `csueiras/multi-scene-phase-c-split-pane-fed5` (base: `develop`, merge-base `dd0fa4b957`)

This branch adds a split-pane scene workspace: the scene-editing area can split into
two panes, each with its own scene tabs and bound docks, so two scenes can be edited
side by side. It had a long, buggy iteration history. A review + fix pass has been done;
this document hands off the remaining work.

Read `review-claude-1.md` (repo root) first — it is the full findings list with
file:line and severities. This file is the "what's done / what's left / how to work" layer
on top of it.

## Session state

- Latest fix commit: **`20e5108b27`** "Fix split-pane workspace defects and clean up focus/scene bookkeeping".
- Full C++ + Foundry Script test suite is **green** (2701 cases, 0 failed) as of that commit.
- The build is warm; incremental rebuilds are ~30s.

## Architecture (orient here before touching focus/layout)

- `editor/editor_scene_workspace.{h,cpp}` — `EditorSceneWorkspace` (a `VBoxContainer`)
  holds one or two `EditorScenePane`s in a `SplitContainer`. Each `EditorScenePane` owns
  its `EditorSceneTabs`, a `content_host`, a `SubViewportContainer` **preview**, and a 3D
  placeholder.
- `editor/editor_scene_context.{h,cpp}` — `EditorSceneContext` owns one edited scene's
  state (its `SubViewport`, selection, history, plugin state). `set_display_parent()`
  attaches its viewport to a display container (making it active/rendering);
  `deactivate()` detaches it.
- `editor/editor_data.{h,cpp}` — per-pane scene bookkeeping. Key invariant (asserted in
  DEV in `set_pane_current_scene` / `set_focused_pane`):
  **`current_edited_scene == pane_current_scenes[focused_pane]`**, and
  `pane_current_scenes[p]` always points at a scene whose `.pane == p`. Most bugs in this
  branch are violations of that invariant.
- **The load-bearing design choice**: the *real* editor main screen (2D/3D editors, etc.)
  is NOT reparented into a pane. It lives in a `center_overlay` and is manually
  repositioned over the **focused** pane's `content_host` on every resize
  (`EditorNode::_fit_main_screen_to_focused_pane` → `EditorSceneWorkspace::fit_overlay_to_focused_pane`).
  Non-focused panes instead show a static `SubViewport` preview (2D) or a 3D placeholder.
  **Consequence: only the focused pane has the live, interactive editor.** This is why
  drops and viewport interaction only work in the focused pane, and it is the source of
  most of the layout/timing churn in the git history. If polishing gets stuck fighting
  this, the higher-leverage fix is to reconsider the overlay model.
- Core scene-switch entry points in `editor/editor_node.cpp`:
  `focus_pane()`, `_set_current_scene_nocheck()`, `_activate_scene_context()`,
  `_update_pane_display_attachments()` (decides preview vs live vs 3D-placeholder per pane).

## Fixed this session (don't re-litigate)

See the commit body for the full list. Highlights:
- `update_all_scene_tabs()` infinite recursion.
- Split offset no longer reset on restore (removed the deferred `_ensure_split_offset`).
- Dock click-to-focus now works (folded into dock `input()`); pane focus paths collapsed.
- `transfer_scene_to_pane` drop index + invariant-safe ordering (was tripping a DEV assert per cross-pane drag).
- `set_edited_scene` no longer hijacks a non-focused pane's current tab.
- `EditorSceneTabs` destructor clears its singleton.
- Empty-focused-pane (`current_edited_scene == -1`) guards in live-edit-root + scene-root-script.
- `EditorInspectorSection` connects `property_edited` to its own inspector, not the moving singleton.
- `EditorScenePane::input()` respects z-order and follows a drag (see "needs verification").
- Dead code removed: `EditorSceneContext::activate()`, self-assignment, tautological assert, unused `get_owning_pane`, etc.

## Verified in the live editor (GUI, macOS)

- Split **restores** from the saved layout (2 panes, correct scenes), split is centered.
- Focus visual (orange border) renders on the focused pane.
- The empty-focused-pane state that previously spammed
  `editor_data.cpp:1067 / 1145 index out of bounds` is now **clean** (0 errors),
  reproduced deterministically by seeding an empty focused pane in `editor_layout.cfg`.

## TOP priority to verify (I could not — no macOS accessibility to script DnD)

1. **Drag-and-drop focus behavior** (`EditorScenePane::input()`).
   - Fix #1 (z-order): dragging a file from the FileSystem dock (bottom drawer, drawn over
     the bottom pane) into the **focused** pane should drop correctly and NOT steal focus
     to the bottom pane. This is the reported bug.
   - Fix #2 (focus-follows-drag): dragging into a **non-focused** pane should focus that
     pane mid-drag (so its live editor receives the drop).
   - **Risk**: fix #2 reparents the editor overlay during a drag, and
     `pane_focus_requested → focus_pane` is a **synchronous** connection
     (`editor_node.cpp:9490`). If cross-pane drops misfire or feel janky, either defer the
     focus switch (`call_deferred`) or scope back to just the z-order check (fix #1, which
     stands alone). This is the single most likely thing to still be wrong.

## Known remaining issues / suggested next work (not yet fixed)

1. **Reload-in-split reassigns pane** — `_reload_modified_scenes` →
   `load_scene` → `EditorData::add_edited_scene` sets `es.pane = focused_pane`. Reloading a
   modified scene that lives in a *non-focused* pane will move it into the focused pane.
   Pre-existing, separate from the `set_edited_scene` hijack already fixed. Fix in the
   reload path by preserving/restoring the scene's original pane.
2. **Only the focused pane is interactive** (overlay model, above). Decide whether that is
   acceptable UX or worth reworking. Everything drop/interaction-related traces back here.
3. **Engine changes compensate for editor gaps** — `scene/gui/subviewport_container.cpp`
   `add_child_notify` force-overrides a dynamically-added SubViewport's update mode (minor
   regression for user projects adding an `UPDATE_ONCE` viewport into a live container);
   `scene/main/viewport.cpp` guard is safe. Consider moving the container fix editor-side
   (re-run `recalc_force_viewport_sizes` after the reparent instead).
4. **macOS global dock menu regression** — only pane 0 populates `DOCK_MENU`, and the
   per-tab unsaved `(*)` marker / metadata was dropped from `_update_tab_titles`
   (`editor/scene/editor_scene_tabs.cpp`). (review-claude-1 L9)
5. **`menu_initialized` static** in `EditorSceneTabs::update_scene_tabs` is cross-instance
   and not reset on unsplit→re-split. (review L8)
6. **`_recompute_3d_content` staleness** for non-focused panes + O(N) subtree walk per
   `set_display_parent` call. (review L11)
7. **`changing_scene` cleared both synchronously and via the deferred `_set_main_scene_state`**
   — decide on one. (review M5)

## How to build / test / drive

```bash
# Build (incremental ~30s; binary at bin/foundry.macos.editor.dev.arm64)
scons platform=macos target=editor dev_build=yes tests=yes -j$(sysctl -n hw.ncpu)

# Full suite (trust the "[doctest] Status: SUCCESS!" line; leaks-at-exit are normal)
./bin/foundry.macos.editor.dev.arm64 --headless test run --force-colors

# Scoped
./bin/foundry.macos.editor.dev.arm64 --headless test run \
  --case "*SceneWorkspace*,*pane-model*,*EditorSceneContext*,*Inspector*" --force-colors
```

Relevant tests: `tests/editor/test_scene_workspace.h` (pane model, transfer invariant,
split sizes/offset, config round-trip), `tests/editor/test_editor_scene_context.h`,
`tests/editor/test_dock_scene_context_binding.h`.

### Driving the GUI on macOS (what worked / what didn't)

```bash
# A throwaway project: create <dir>/project.foundry + a couple of .tscn scenes,
# then import + open:
./bin/foundry.macos.editor.dev.arm64 --headless project import --project <dir>
./bin/foundry.macos.editor.dev.arm64 editor open --project <dir> res://scene_a.tscn &
screencapture -x /tmp/shot.png          # screenshot the whole screen
pkill -INT -f "foundry.macos.editor.dev.arm64 editor open"   # graceful quit (kill -INT)
```

- Per-project layout lives at `<project>/.foundry/editor/editor_layout.cfg`. You can seed
  a split (or an empty focused pane) by editing its `[Workspace]` section, then reopening
  — this is how the restore + empty-pane states were verified without clicking.
- **macOS accessibility is NOT granted** to the binary in this environment, so
  `osascript`/System Events cannot click menus and native drag-and-drop cannot be scripted.
  Menu-driven and DnD flows must be tested by hand (the Split commands are in the Scene
  menu; there are no default shortcuts).
- The VM has a GPU here (Metal), so 3D/2D viewports render in screenshots.

## Foundry-specific notes

- Binary is `foundry.*` (not `godot`); project config is `project.foundry` (not `project.godot`).
- Use the command-first CLI (`foundry <cmd> <subcmd>`); legacy flags like `--import`,
  `--path`, `--test` are removed.
