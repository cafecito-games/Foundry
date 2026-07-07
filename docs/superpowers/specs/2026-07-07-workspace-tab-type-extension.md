# Workspace tab type extension path

Status: reference / validation (issue #1030, epic #1021)
Grounded to: `develop` after W1–W8 (registry #1022, scene type #1024, script type
#1025, generic pane mechanics #1026, per-tab persistence #1027).

This document proves the typed workspace tab architecture can absorb future tab
types (`HelpTab`, `TextTab`, and beyond) without touching pane drag / split /
close / collapse / persistence mechanics. It records:

1. The minimal steps to add a new `WorkspaceTabType`.
2. Worked sketches for `HelpTab` and `TextTab` (identity, payload, close policy,
   mount chrome). No implementation — that is a later epic item.
3. An audit of the scene/script assumptions in the pane/persistence code, each
   marked *generalize-now* or *follow-up*.

The `[workspace-tab]` guardrail tests in
`tests/editor/test_workspace_tab_model.h` register a **fake** tab type with no
scene/script identity and drive it through the real add / move / edge-split /
close / collapse / persist code paths, so this doc stays honest.

---

## 1. Adding a new `WorkspaceTabType`

The behavior surface is `WorkspaceTabType` in
`editor/workspace/workspace_tab_type.h`. A new type is three mechanical steps:

1. **Implement the interface.** Subclass `WorkspaceTabType` and override:
   - `type_id()` — a unique, stable `StringName` (the persistence key; never
     reuse another type's id).
   - `can_open(resource)` — whether this type handles a given resource string.
   - `make_tab(resource, stable_id)` — build the serializable `WorkspaceTab`
     identity record: set `stable_id`, `type_id`, `resource_key`, and the
     `title_cache` / `icon_key_cache` display hints.
   - `get_title(tab)` / `get_icon(tab)` — chrome labels for the tab strip.
   - `mount(tab, chrome_host)` / `unmount(tab)` — attach/detach the live surface
     under the pane's `chrome_host` control. `unmount` is where the type captures
     its transient view state (caret, scroll, …) back into the tab's payload.
   - `activate(tab)` — run activation side effects (claim focus, etc.).
   - `request_close(tab, on_deferred_close)` — return `CLOSE`, `CANCEL`, or
     `DEFERRED`. `DEFERRED` means the type is driving its own async confirmation
     (e.g. a dirty-buffer prompt) and will invoke `on_deferred_close` once iff
     the flow resolves to a close.
   - `save_payload(tab)` / `restore_payload(tab, payload)` — the per-tab
     `Dictionary` blob persisted under the tab's `payload` sub-section.
   - `is_resource_available(tab)` — override to return `false` when the backing
     resource is gone so a stale tab is dropped on restore instead of resurrected
     (default `true`, correct for tabs with no file-backed resource).

2. **Register it.** Add one line to
   `WorkspaceTabRegistry::register_builtin_tab_types()` in
   `editor/workspace/workspace_tab_registry.cpp` (a `static` instance +
   `register_type(&instance)`). This is the **only** site that names a concrete
   type; panes resolve types by id via `tab_registry->find_type(type_id)`.

3. **Provide identity + payload.** The registry deduplicates open tabs by
   `(type_id, resource_key)` (`insert_canonical` / `find_canonical`). Choose a
   `resource_key` that is unique per logical document of the type and stable
   across sessions (a resource path, a class name, a scratch-buffer id). The
   payload holds only transient view state; durable content belongs either in the
   backing resource or is serialized into the payload for buffer-only types.

Nothing else changes. Pane mechanics (`WorkspacePane::add_tab`, `move_tab`,
`take_tab`, `request_close_tab`, `save_layout`, `load_layout`) and the workspace
drag/split router (`EditorSceneWorkspace::handle_tab_drop`) dispatch through the
registry and the `WorkspaceTabType` virtuals; they do not switch on a new
`type_id`.

---

## 2. Worked sketches: `HelpTab` and `TextTab`

Both follow the `ScriptResourceTab` shape (a leaf/tab with no `EditorSceneContext`
that owns its own surface under the chrome host). Neither needs the scene tile
bridge.

### `HelpTab` — a class-reference help page

- **`type_id`**: `"help"`.
- **`resource_key` identity**: the fully-qualified help class name, e.g.
  `class:Node2D`. One tab per class; opening the same class reveals the existing
  tab via canonical dedup.
- **`payload`**: `{ scroll_position: int, section_anchor: String }` — where the
  reader was scrolled and the last section anchor. Purely presentational.
- **`request_close`**: always `CLOSE`. Help has no unsaved state, so no prompt.
- **`mount`**: instantiate/reparent the help viewer control under `chrome_host`,
  seek to `section_anchor`/`scroll_position`. `unmount` writes the current scroll
  and anchor back into the payload. `is_resource_available` returns
  `ClassDB::class_exists(class)` so a tab for a class that no longer exists is
  dropped on restore.

### `TextTab` — a plain-text buffer (file-backed or scratch)

- **`type_id`**: `"text"`.
- **`resource_key` identity**: the text file path (`res://notes.txt`) for
  file-backed buffers, or a synthetic `scratch:<id>` for unsaved scratch buffers
  (mirroring the scene type's `unsaved:` keys).
- **`payload`**: `{ caret_line: int, caret_column: int, scroll_position: int,
  dirty: bool, buffer_contents: String (scratch only) }`. File-backed tabs
  persist only view state and reload contents from disk; scratch buffers must
  serialize `buffer_contents` because they have no backing file.
- **`request_close`**: `CLOSE` when clean; when `dirty`, return `DEFERRED` and run
  a save/discard/cancel prompt exactly like the script tab, invoking
  `on_deferred_close` only on save-or-discard. (Reuse the script tab's deferred
  close plumbing.)
- **`mount`**: reparent the text editor control under `chrome_host`, load the
  buffer (from disk or `buffer_contents`), restore caret/scroll. `unmount`
  captures caret/scroll/dirty (and scratch contents) into the payload.
  `is_resource_available` returns `true` for scratch buffers and
  `FileAccess::exists(path)` for file-backed ones.

Both types are added by the three steps in §1 with **zero** edits to pane or
persistence code — the deferred-close flow `TextTab` needs already exists
(`WorkspaceTabCloseResult::DEFERRED` + `on_deferred_close`), proven by the script
tab and the `RecordingTabType`/`PromptSpyTabType` tests.

---

## 3. Scene/script assumption audit

`git grep -n '"scene"\|"script"' editor/workspace/` at grounding. Registration
sites are expected; the audit is whether any **non-registration** site would block
a third type.

### Registration / type-definition sites (expected, not blockers)

- `workspace_tab_registry.cpp:136` — `register_builtin_tab_types()` names the
  script type. This is the sanctioned single naming site.
- `scene_tab.cpp:97,116` — `SceneTabType` defining its own `type_id()` / stamping
  it in `make_tab`. Self-identification, not branching.
- `script_resource_tab.h:67` — `ScriptResourceTabType` default id argument.

### Non-registration sites in `workspace_pane.{h,cpp}`

All of the following gate the **legacy scene-tile bridge** — the scene type is
the one type with no surface of its own; it bridges to the pane's shared
`ScenePaneTile`. Every other type (script today, `HelpTab`/`TextTab` tomorrow)
owns its surface and takes the generic `else` path, so these checks are *scene
special-cases a third type never triggers*, not *branches a third type must be
added to*.

| Site | What it does | Blocks a 3rd type? | Verdict |
| --- | --- | --- | --- |
| `workspace_pane.h:117,118` | `is_scene_pane()` / `is_script_pane()` seed-content helpers | No — seed content type of an empty pane; a third type still opens as a generic tab | follow-up (cosmetic) |
| `workspace_pane.cpp:238` | show the scene tile when a **scene** tab mounts | No — non-scene tabs mount their own surface | keep (scene bridge) |
| `workspace_pane.cpp:388,404,441,449` | `sync_scene_tabs_from_editor_data` reconciles scene tabs with `EditorData` tile membership; guarded by `is_scene_pane()` early-return (`:380`) | No — a third type carries its tabs as plain records; only scene tabs mirror `EditorData` | keep (scene ⇄ EditorData bridge) |
| `workspace_pane.cpp:531` | `move_tab` scene fast-path that also moves the edited-scene index in `EditorData`; falls through to the generic reorder for non-scene tabs | No — non-scene tabs use the generic branch below it | keep (scene bookkeeping) |
| `workspace_pane.cpp:608` | `has_non_scene_tabs()` — "does this pane hold anything that isn't a scene?" collapse guard | No — a third type counts as non-scene, which is the safe answer | keep |
| `workspace_pane.cpp:621` | `find_scene_tab_index` — scene-only lookup helper | No — scene-specific accessor, not on the generic path | keep |
| `workspace_pane.cpp:714,717` | `get_scene_context()` returns `nullptr` for script tabs, the tile context for scene tabs | Minor — a third type falls to the final `return nullptr`, which is correct (no scene context). The explicit `== "script"` check is redundant with that fallthrough | follow-up (redundant, harmless) |

**Conclusion:** no non-registration site *blocks* a third type. Every check is a
scene-tile bridge special-case (the scene type's structural exception) or a
scene↔`EditorData` bookkeeping helper; a non-scene type takes the generic path in
every case. Two cosmetic follow-ups (`is_scene_pane`/`is_script_pane` seed
helpers; the redundant `== "script"` check in `get_scene_context`) can be
generalized when the first real non-scene, non-script type lands, but neither is
required now. The AC's ideal — `git grep` returning *only* registration sites — is
not literally met because the scene-tile bridge is inherently scene-coupled;
that coupling is by design (scene tabs have no surface of their own) and is
documented here rather than removed.

The guardrail tests enforce the invariant that matters: a fake type with an
unrelated `type_id` drives move / split / close / collapse / persist without any
of these branches firing on its behalf.
