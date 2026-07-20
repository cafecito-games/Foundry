# Editor New Script Command — Design

Date: 2026-07-20  
Status: Approved for implementation after user review of this spec

## Problem

After scripts moved to workspace leaves (and the embedded Script Editor main screen was removed), creating a standalone script is hard to discover:

- FileSystem dock → New → Script… (shortcut unbound)
- Script leaf File → New Script… (`Cmd/Ctrl+N`, only while a script leaf is focused)
- Scene dock → Attach Script (node-attached only)

There is no global shortcut or command-palette entry that creates a new script and opens it in the focused workspace pane. Global `Cmd/Ctrl+N` remains **New Scene**.

## Goals

1. Add a global **New Script…** command with default shortcut **`Cmd/Ctrl+Alt+N`** (macOS: **Cmd+Option+N**).
2. Expose the same action in the **command palette**.
3. After creation, open the script by **reusing an existing script leaf in the focused pane**; create a script leaf only if none exists there.
4. Default dialog path matches FileSystem → New → Script… (`FileSystemDock` current directory + `new_script.fs`).

## Non-goals

- Changing **New Scene** (`Cmd/Ctrl+N`) or script-leaf-local File → New Script…
- Attach-script-to-selected-node (Scene dock path stays as-is)
- New Text File command
- Forcing a new script leaf when one already exists in the focused pane

## Approach

Register a single editor-level command that reuses FileSystemDock’s existing `ScriptCreateDialog` and the existing `push_item` → script edit → `open_script_leaf` open path.

### Command registration

- Shortcut/command id: `editor/new_script`
- Display name: `New Script…`
- Default binding: `KeyModifierMask::CMD_OR_CTRL | KeyModifierMask::ALT | Key::N`
- Register with `ED_SHORTCUT_AND_COMMAND` next to related script/scene commands in `EditorNode` (near `editor/quick_open_script`).
- Add a File/Project menu item near Quick Open Script for discoverability without the palette.

### Handler behavior

1. Resolve default path the same way as `FileSystemDock` `FILE_MENU_NEW_SCRIPT`:
   - `dir = FileSystemDock::get_singleton()->get_current_directory()` (or equivalent of current `current_path` directory normalization)
   - `path = dir.path_join("new_script.fs")`
2. Open FileSystemDock’s existing create dialog via `config("Node", path, false)` + `popup_centered()`.
3. On `script_created`, keep the existing FileSystemDock handler (`push_item`), which already routes into script editing / `reveal_script_leaf` / `open_script_leaf` with **reuse** semantics (`p_force_new_leaf = false`).

No second `ScriptCreateDialog` instance.

### Leaf placement (confirmed policy)

Prefer an existing script leaf hosted by the focused workspace pane; otherwise create one beside the focused leaf. This matches current `EditorSceneWorkspace::open_script_leaf(..., p_force_new_leaf = false)` behavior used by normal script opens.

## Implementation sketch

| Area | Change |
|------|--------|
| `editor/editor_node.cpp` | `ED_SHORTCUT_AND_COMMAND("editor/new_script", ...)`; menu item; handler that configs/pops FileSystemDock dialog |
| `editor/docks/filesystem_dock.*` | Optional thin public helper e.g. `open_script_create_dialog()` encapsulating path + popup (preferred over duplicating path logic in EditorNode) |
| Tests | Editor/automation or unit coverage: command exists; invoking it pops create dialog with FileSystem current-dir default; created script opens via reused leaf policy when a script leaf already exists in the focused pane |

## Acceptance criteria

- [ ] Command palette lists **New Script…** (`editor/new_script`)
- [ ] Default shortcut is Cmd+Option+N / Ctrl+Alt+N and is remappable in Editor Settings
- [ ] Invoking the command opens the create dialog with path under the FileSystem current directory (`…/new_script.fs`)
- [ ] Confirming create opens the new script in a script leaf in the focused pane, reusing an existing script leaf when present
- [ ] Scene **New Scene** shortcut and Attach Script flow remain unchanged

## Out of scope follow-ups

- Binding FileSystem’s local `filesystem_dock/new_script` to the same default key (avoid dual bindings unless we intentionally alias them later)
- Creating scripts from the script leaf File menu when no controller dialog parent exists outside a leaf
