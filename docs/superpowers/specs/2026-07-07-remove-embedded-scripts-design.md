# Remove support for embedded (built-in) scripts in scenes

Design spec for issue #1046. Grounded in `develop @ 18fe78a262`.

## Summary

Remove support for embedded/built-in scripts stored inside scenes — the
`res://scene.tscn::<subresource>` script form. The bytecode compilation pipeline
does not compile embedded scripts, so they cannot ship; keeping partial support
across the editor adds recurring special-casing (script editor open/restore,
workspace tab restore ordering, `::` path splitting to resolve the owner scene)
without a working runtime path.

This engine is a clean break — it has not shipped, so there is no backwards
compatibility to preserve and no legacy data to migrate. The embedded-script
**concept is removed entirely**: creation is disabled, every editor `::`-script
code path is deleted, and a script reference that happens to be scene-local is
treated like any other unrecognized field in a resource file — silently ignored.
No migration tool, no extract-to-file, no read-only fallback, and no
embedded-script-aware code anywhere.

## Key finding that shapes the work

Foundry Script is the **only** script language in this fork that permits
embedding. `FSLanguage::supports_builtin_mode()` (`modules/foundry_script/fs_editor.cpp:235`)
returns `true`; `CSharpLanguage::supports_builtin_mode()`
(`modules/mono/csharp_script.cpp:407`) returns `false` and `mono` is disabled by
default; there is no GDScript module. Once FS returns `false`, no language can
produce an embedded script, so the built-in UI in `ScriptCreateDialog` is dead and
can be removed outright rather than merely hidden.

## Locked decisions (do not re-litigate)

1. **Clean break — zero legacy behavior.** The engine has not shipped; there is no
   backwards compatibility and no migration. No extract-to-file, no read-only
   editor surface, no CLI/headless migration command, and — critically — **no code
   anywhere that recognizes "embedded script" as a concept** (no `is_embedded_script_path`,
   no `::` splitting for scripts, no owner-scene resolution, no migration
   messaging). Confirmed 2026-07-07.
2. **General invariant, as if the feature never existed.** The only load-side rule
   is a single general invariant: *a Node's script must be a standalone resource.*
   Enforced at the shared `SceneState::instantiate` seam (editor + runtime agree)
   with the pre-existing general `Resource::is_built_in()` predicate — if a node's
   script value is built-in (in-memory/scene-local), it is not applied and the node
   loads scriptless. **No `::` splitting, no owner-scene lookup, no "embedded"/"builtin
   script" vocabulary, no migration messaging** — nothing that references the removed
   feature. `is_built_in()` is a property of all resources, not an embedded-script
   concept. This drop-at-attach also prevents any leftover editor behavior: a
   scene-local script never attaches, so no downstream inspector/script-editor path
   ever meets one. Erroring would also be acceptable; ignoring matches how the
   resource layer treats unknown fields.
3. **Scripts only.** Embedded/built-in shaders (`ShaderCreateDialog`,
   `visual_shader.cpp`) are out of scope and untouched. The bytecode motivation
   does not apply to shaders.
4. **Physically delete** the editor `::`-script code paths — do not leave them
   dormant for a follow-up.
5. **Supersedes and closes #1045.** The workspace tab restore owner-scene ordering
   gating exists only because embedded scripts need their owner scene open before
   mounting; removing embedded scripts removes the reason for that gating.

## Boundary: what stays untouched

`Resource::is_built_in()` (`core/io/resource.h:148`) and the generic
`[sub_resource]` / `local://` serialization stay. They are load-bearing for
materials, meshes, unsaved/in-memory resources, and inspector sub-resource
editing. The removal targets only the **script-specific** `::` handling — the
`get_path().get_slice("::", 0)` logic that resolves a script's owner scene — not
the general built-in predicate or sub-resource machinery.

## Work breakdown (four sub-issues)

### A — Stop creation of embedded scripts

- `modules/foundry_script/fs_editor.cpp:235` — `FSLanguage::supports_builtin_mode()`
  returns `false`.
- `editor/docks/scene_tree_dock.cpp:2664-2666` — delete the
  `if (p_script->is_built_in()) { p_script->set_path(scene + "::" + generate_scene_unique_id()); }`
  branch in `_script_created()` so attach-script always writes a `.fs` file.
- `editor/script/script_create_dialog.{cpp,h}` — remove the built-in `CheckBox`
  and `built_in_name` `LineEdit`, `_built_in_pressed()`, `_can_be_built_in()`,
  `supports_built_in`/`built_in_enabled`/`is_built_in` state, `MSG_ID_BUILT_IN`,
  the `create_built_in_script` project-metadata read/write (`:195-210`, `:349-356`),
  and the `_create_new()` built-in name branch (`:365-398`). Path controls become
  unconditional.

### B — Enforce "a Node's script must be a standalone resource" on load

- `scene/resources/packed_scene.cpp` `SceneState::instantiate` — at the node
  script-application branch (the two `node->set_script(props[nprops[j].value])`
  sites around `:420`/`:423`), before applying, check the value as a `Ref<Script>`
  and skip it if `is_built_in()` is true — the node loads scriptless. Compute the
  `Ref<Script>` in both the `TOOLS_ENABLED` and export paths (the existing
  `value_as_script` local is TOOLS-only) so editor and runtime agree.
- Keep it a **general invariant**: use only the general `is_built_in()` predicate.
  No `::` splitting, no owner-scene lookup, no "embedded"/"builtin script" wording,
  no migration messaging. A silent skip (matching unknown-field handling) is fine.
- Because no shipped project or checked-in fixture contains such a reference, this
  is purely a robustness invariant, not a feature path — it must not grow one.

### C — Delete editor `::`-script code paths (closes #1045)

Remove the owner-scene-resolution and embedded-script branches at every script
site (leave shader/material/unsaved-resource handling intact):

- `editor/script/script_editor_view.cpp` — `is_embedded_script_path()` helper
  (`:101`) and its callers; `::` open (`:397-406`), `_mark_built_in_scripts_as_saved`
  (`:652-707`), `_script_exists` `::` split (`:868`), restore `built_in` resolve
  (`:954-958`), file-list scene-path display (`:1126`), scene-close tab close
  (`:1410`), refactor guard (`:2118`), save/apply built-in cases (`:2206`, `:2320`),
  and the two restore gates that require the owner scene open
  (`set_window_layout` `:2788-2794`, `set_view_layout` `:3459-3465`).
- `editor/script/script_editor_controller.cpp` — `_script_exists` split
  (`:86-88`), `save_all_scripts` scenes-to-save routing (`:647-663`).
- `editor/script/script_editor_plugin.cpp` — `edit()` owner-scene force-load
  (`:574-582`), `get_unsaved_status` built-in collection (`:622-644`).
- `editor/editor_node.cpp` — `save_resource` scene-path routing (`:1812-1826`),
  built-in remap guard (`:2007`), reveal-in-engine gating (`:3369`).
- `editor/inspector/editor_resource_picker.cpp` — save/edit menu enable logic
  (`:558-562`, `:750`, `:1253`, `:1421`) as it pertains to scripts.
- `editor/docks/scene_tree_dock.cpp` — branch-as-scene foreign-resource filter
  (`:4876`) and NodePath remap for scripts (`:2218`, `:4767`, `:4794`).
- `editor/scene/connections_dialog.cpp:399` — built-in script reload.
- Tab title/icon rendering: `editor/script/script_text_editor.cpp:880-916`
  (`Name (scene.tscn)` + `Internal` icon) and `editor/script/text_editor.cpp:75-80`.

Removing the two `set_window_layout`/`set_view_layout` gates and the
owner-scene ordering is the concrete work that **closes #1045**.

### D — Tests, docs, and close-out

- Update C++ tests that build embedded scripts: `tests/editor/test_script_editor_views.h`,
  `tests/editor/test_script_leaf_node_drop.h`, `tests/core/io/test_marshalls.h`,
  `tests/core/object/test_script_diagnostic_capture_scope.h`,
  `tests/editor/test_editor_export_platform_autoload.h`.
- Add a regression test for "ignored on load": a scene with an embedded script
  sub-resource instantiates with the node scriptless and no crash.
- Add a test asserting the Attach Script dialog exposes no built-in option and the
  created script is a `.fs` file.
- Scrub embedded/built-in *script* language from `doc/classes` where it describes
  the removed capability (leave built-in *function/annotation* docs and shader
  docs alone).
- Close #1045 as superseded.

## Acceptance (mechanical)

- `git grep -n 'get_slice("::", 0)' editor/script editor/editor_node.cpp` returns
  no script-owner-resolution matches.
- Attach Script dialog has no built-in/embedded option; a created script is always
  a `.fs` file on disk.
- Opening a scene with a legacy `res://scene.tscn::N` script warns once, the node
  loads without a script, and no dead editor surface or crash occurs.
- Full suite green:
  `DISPLAY=:1 ./bin/foundry.* --headless test run --force-colors`.

## Risks

- **Over-broad deletion of `is_built_in()` branches.** Only script `::` handling
  is in scope; material/mesh/unsaved-resource and shader paths must keep working.
  Each deleted site should be checked to be script-specific.
- **Reintroducing a legacy concept.** The invariant in B must stay generic; do not
  grow it into an embedded-script handler with `::` parsing or migration messaging.
- **`FSTranslationParserPlugin`/LSP owner-scene resolution** (`fs_workspace.cpp:896`)
  is for scene-attached *file* scripts, not embedded scripts — confirm it is not
  disturbed.

## PR slicing

A → B → C → D, in order. A and B are independently shippable and prevent new
embedded scripts + neutralize existing ones. C is the largest cleanup and depends
on A/B being in so the deleted paths are truly unreachable. D closes out.
