# Namespaced Native Classes — Design

**Date:** 2026-08-06
**Status:** Approved (design); pending spec review → implementation plan
**Scope:** Machinery + one pilot class end-to-end across every surface. Existing classes stay flat; migration is a follow-up.

## Goal

Give native (C++) classes a qualified, namespaced identity so that a class can live at a path like `foundry.http.server.HTTPServer`, reachable in Foundry Script via `import foundry.http.server`, usable as a node in scenes, and able to coexist with other classes that share the same simple name as long as they sit in different namespaces. The long-term direction is to move classes out of the flat global namespace into proper namespaces; this spec builds the foundation and proves it on one class.

The namespacing is the *script/scene-facing* identity layer. Native classes continue to be normal C++ classes usable everywhere they are today; what changes is the name by which scripts, scenes, and the editor reach them, and the ability for the qualified identity to be canonical and the global (bare) name to be an explicit, removable re-export.

## Non-goals (deferred to follow-up specs)

- Cross-native conflict coexistence at the language level with two *real* same-named natives (the registry model supports it; exercising it needs a second real namespaced native).
- Migrating existing flat classes into namespaces (the move itself, re-export aliases, alias removal).
- Full `HTTPServer` functionality (HTTP semantics). The pilot ships a minimal-but-real class.
- Editor polish: inheritance tree grouped by namespace, exhaustive namespace completion.

## Locked decisions

1. **Namespacing model: full namespacing with re-export.** The qualified path is the canonical identity; the bare global name is an explicit, opt-in alias (a re-export). This is consistent with the clean-break philosophy: the alias is deletable, not legacy behavior that must be preserved.
2. **Source of truth: on the C++ class.** Namespace membership is declared at the registration site and stored on `ClassInfo`, not in an external map or build config.
3. **Registration seam: a companion macro.** `FOUNDRY_REGISTER_NAMESPACE(Class, "ns.path")` runs alongside `FOUNDRY_REGISTER_CLASS`, leaving every existing `FOUNDRY_CLASS` call site untouched.
4. **Canonical key = qualified name.** The `ClassDB` registry is keyed by the qualified name. Existing classes (empty namespace) keep a byte-identical key, so all current consumers are unchanged.
5. **Namespace-only by default.** A namespaced class is reachable only through its namespace; appearing in the global (bare) namespace requires an explicit alias registration. The pilot class registers no global alias.

## Section 1 — ClassDB identity model

### Registration

A companion macro declares the namespace at the registration site, next to the existing registration call (e.g. in `register_types.cpp`):

```cpp
FOUNDRY_REGISTER_CLASS(HTTPServer);
FOUNDRY_REGISTER_NAMESPACE(HTTPServer, "foundry.http.server");
```

`FOUNDRY_REGISTER_CLASS` and `FOUNDRY_CLASS` are unchanged; namespace defaults to empty.

### ClassInfo fields

`core/object/class_db.h` (`ClassInfo`, currently fields `api`, `inherits`, `name`, `disabled`, `exposed`, `reloadable`, `is_virtual`, `is_runtime`, `creation_func`) gains:

- `StringName namespace_path;` — dotted, e.g. `foundry.http.server`. Empty = global namespace.
- `StringName qualified_name;` — derived: `namespace_path.is_empty() ? name : namespace_path + "." + name`.

### Canonical key

The `classes` HashMap (`core/object/class_db.h`) is keyed by `qualified_name`. Because of the derivation rule, every existing class (empty namespace) hashes to the same string it does today (`Node`, `HTTPRequest`, …), so every existing `class_exists` / `get_class_list` / `instantiate` consumer is byte-for-byte unchanged. Only namespaced classes gain a new canonical identity.

### Bare-name alias map (re-export layer)

A new `HashMap<StringName, LocalVector<StringName>> bare_aliases` maps a flat name → the qualified names that expose it. Bare-name lookup walks this map:

- exactly one owner → resolves (this is the transition re-export);
- zero owners → not found;
- two or more owners → **ambiguous**; lookup fails and the caller must use the qualified name.

This is what makes conflict coexistence real: `foundry.http.Server` and `foundry.ftp.Server` register under distinct canonical keys; if both also alias to bare `Server`, bare lookup is forced to disambiguate.

### New ClassDB API (additive only)

- `register_namespace(p_class, p_namespace)` — sets the fields (called by the macro).
- `class_get_qualified_name(p_class)` / `class_get_namespace(p_class)`.
- `class_get_by_qualified_name(p_qualified)` — exact canonical lookup.
- `class_register_global_alias(p_class, p_alias)` — explicit re-export of a class to a bare name; how a class opts into the global namespace.
- `resolve_type_name(StringName)` — central resolver used by the scene loader (see Section 3): qualified → canonical hit; bare-unique → resolves; bare-ambiguous → fails; flat → passthrough.

### Pilot scope for this section

Add the fields, macro, alias map, and the accessors. Register `HTTPServer` under `foundry.http.server` with no global alias. A regression test registers a second internal class under the same bare name via alias to prove the ambiguity error fires.

## Section 2 — Foundry Script namespace/import for native classes

Foundry Script already has `namespace`/`import` machinery, but it only consults `ScriptServer` (script-defined globals). Native classes sit outside it as flat globals. This section wires native classes into the existing system.

### Changes

1. **Exclude namespaced natives from the flat global table.** In `FSLanguage::init` (`modules/foundry_script/foundry_script.cpp`, the loop that wraps every ClassDB class in an `FSNativeClass` and registers it as a bare global), skip classes with a non-empty `namespace_path`. `HTTPServer` is no longer a global.

2. **Qualified `FSNativeClass` map.** Add a `native_class_by_qualified` map (qualified name → `FSNativeClass`) populated at init for all natives. Empty-namespace classes still go into the flat global map under their bare name (as today); namespaced classes live under their qualified name. This is the object the compiler resolves against.

3. **Extend the analyzer resolution helpers to consult ClassDB**, not just `ScriptServer`:
   - `get_global_class_in_namespace` (`fs_analyzer.cpp`) — after the ScriptServer miss, call `ClassDB::class_get_in_namespace(p_namespace, p_class_name)`; on hit return the qualified name tagged *native*.
   - `get_imported_global_class` — same, so after `import foundry.http.server` a bare `HTTPServer` resolves; ambiguity between two imported namespaces still errors (unchanged behavior).
   - `get_namespace_global_class_from_type_chain` — handle `foundry.http.server.HTTPServer` via ClassDB qualified lookup.
   - `class_exists(bare)` is unchanged: a namespaced native used bare without import correctly fails to resolve.

4. **Reduction carries the qualified identity.** `reduce_identifier` and `resolve_datatype` produce a `NATIVE_CLASS` / `NATIVE` datatype whose `native_type` is the **qualified** name for namespaced natives, so the compiler pulls the right `FSNativeClass` from the qualified map.

5. **Import model mirrors existing semantics.** `import foundry.http.server` brings `HTTPServer` into scope as a bare name (disambiguated by the analyzer); fully-qualified `foundry.http.server.HTTPServer` also works. No new keyword.

6. **Completion + grammar.** `_list_importable_namespaces` (`fs_editor.cpp`) includes native namespaces from ClassDB; member completion after `import foundry.http.server` lists `HTTPServer`. `GRAMMAR.md` §4.2 reachability rules gain: a native class is reachable under the same conditions as a script class, with native namespaces sourced from ClassDB. No new tokens or keywords are added, so the token and precedence tables are untouched.

### Pilot fixtures

- A script that `import foundry.http.server` then `extends HTTPServer` and instantiates it.
- A negative fixture using bare `HTTPServer` with no import → resolution error.
- The qualified-chain form `foundry.http.server.HTTPServer` resolving.

(Registry-level ambiguity is covered by the Section 1 alias-collision test; only one real namespaced native exists in the pilot, so cross-native ambiguity is a migration-spec concern.)

## Section 3 — Scene format + loader

### Format: no structural change

Class names in `SceneState` are already arbitrary interned `StringName`s in a shared names table (`scene/resources/packed_scene.h`), referenced by index via `NodeData.type`. A namespaced node is written and read as one string:

```
[node name="Srv" type="foundry.http.server.HTTPServer"]
```

No format-version bump, no new fields. Dots never appear in flat native type names today, so any dotted `type=` is unambiguously namespaced.

### Write path (packer)

When packing a node, store the class's `qualified_name` if it has a namespace, else the bare name.

### Read path (loader)

The single choke point is `ClassDB::instantiate(snames[n.type])` in `scene/resources/packed_scene.cpp`. Route it through `ClassDB::resolve_type_name` (Section 1). For the pilot, scenes store the qualified name so this is essentially a direct lookup; centralizing it now means bare/alias cases and later migration need no further scene-side changes. Because the qualified name is the canonical `classes` key, `instantiate` resolves it directly once canonicalized.

### Failure handling

If resolution fails or is ambiguous, the existing `MissingNode` placeholder captures `set_original_class(...)` with the **qualified** name, so a namespaced type survives a missing-class round-trip intact (no silent flattening to bare).

### Editor node creation

`CreateDialog::_fill_type_list` (`editor/gui/create_dialog.cpp`) enumerates `ClassDB::get_class_list`; namespaced classes are already present under their qualified keys. Two minimal changes for the pilot: display them grouped under a namespace header rather than dumped into the flat list, and have the confirm path write the qualified name into the new node's scene entry. Full inheritance-tree-by-namespace is a later polish.

### Pilot verification

A `.tscn` with `type="foundry.http.server.HTTPServer"` loads and instantiates; an unknown/ambiguous qualified type produces a `MissingNode` preserving the qualified string; the Create Node dialog lists `HTTPServer` under its namespace group and adding it produces a working node.

## Section 4 — Docs, editor, and runtime identity policy

### Runtime identity policy

Because the qualified name is the canonical `ClassInfo` key:

- `Object::get_class()` returns the **qualified name** for namespaced classes (`foundry.http.server.HTTPServer`), bare name for flat classes. Flat classes are byte-identical to today, so existing `get_class() == "..."` checks are untouched.
- `is_class(p)` / `is_class_ptr` resolve through the same identity and inheritance chain, so `node->is_class("foundry.http.server.HTTPServer")` and the parent chain (`is_class("Node")`) both hold.
- Bare-name matching (`is_class("HTTPServer")`) is true only where a global alias is registered — none for the pilot, so namespaced classes are namespace-only at runtime too.

One canonical identity everywhere; global access is an explicit, removable alias.

### Inspector / property system

No special work. The inspector reads property lists via the resolved class identity from ClassDB, which holds `HTTPServer` under its qualified key. Selecting a namespaced node shows its properties normally.

### Class reference / docs

The doc schema (`doc/class.xsd`) and `<class>` element gain a `namespace` attribute. A namespaced class's doc file is `foundry.http.server.HTTPServer.xml` with `<class name="HTTPServer" namespace="foundry.http.server" inherits="Node">`. `DocData` stores namespace alongside name; the editor help system resolves a qualified name from script (F1 on `foundry.http.server.HTTPServer`) to the right doc entry. The build hook (`config.py:get_doc_classes`) already collects a directory, so dotted filenames are fine.

### Pilot verification

`get_class()` on an `HTTPServer` node returns the qualified string and `is_class("Node")` holds; the inspector shows its properties; F1 on the qualified name opens its doc page.

## Section 5 — Pilot class, testing, and rollout

### Pilot class

`HTTPServer` ships as a minimal-but-real native class — `class HTTPServer : public Node` in `scene/main/http_server.{h,cpp}`, registered in `scene/register_scene_types.cpp` with `FOUNDRY_REGISTER_NAMESPACE(HTTPServer, "foundry.http.server")`, with just enough surface to exercise every resolution path (constructible, a method and a property, usable as a node in a scene and as a script base class). Full HTTP semantics is a follow-up.

### Testing

- **C++ doctest** (`tests/`): `register_namespace` stamps the fields; canonical key is the qualified name; empty-namespace classes keep identical keys (regression); the alias map returns ambiguity for a bare name with two or more owners; `resolve_type_name` resolves qualified / bare-unique / rejects bare-ambiguous.
- **Foundry Script fixtures** (`modules/foundry_script/tests/scripts/`): `import foundry.http.server` → `extends HTTPServer` + instantiation succeeds; qualified-chain `foundry.http.server.HTTPServer` resolves; bare `HTTPServer` with no import → resolution error.
- **Scene fixture**: a `.tscn` with `type="foundry.http.server.HTTPServer"` loads and instantiates; an unknown/ambiguous qualified type yields a `MissingNode` preserving the qualified string.

All generated local files use the shared test scratch space; fixtures are checked-in inputs/outputs only.

### Rollout and decomposition

This spec is the foundation. It will be filed as an epic with sub-issues, one per surface, so the work breaks down cleanly:

- **S1 — ClassDB identity model:** `ClassInfo` fields, companion macro, alias map, new accessors, `resolve_type_name`, ambiguity regression test.
- **S2 — Foundry Script resolution:** exclude namespaced natives from flat globals, qualified `FSNativeClass` map, extend the three analyzer helpers, qualified identity in reduction/compiler, import + completion, `GRAMMAR.md` §4.2, fixtures.
- **S3 — Scene format + loader:** qualified on-disk type, write path, `resolve_type_name` at the instantiation choke point, `MissingNode` qualified preservation, scene fixture.
- **S4 — Editor surfaces:** Create Node dialog namespace grouping + qualified instantiation; inspector sanity; editor help search.
- **S5 — Docs:** `namespace` attribute in schema/`<class>`, `DocData` storage, qualified-name help resolution, doc file for `HTTPServer`.
- **S6 — Pilot class + integration:** minimal `HTTPServer`, registration with namespace, end-to-end fixtures across all surfaces.

Suggested dependency order: S1 → (S2, S3, S4, S5 in parallel where independent) → S6 integrates. Conflict-coexistence, migration, full `HTTPServer`, and editor polish are separate follow-up epics.

## Key file references

- `core/object/class_db.h` (`ClassInfo`, `classes` HashMap, `register_*`, `FOUNDRY_REGISTER_*` macros, `compat_classes`/`get_compatibility_remapped_class`)
- `core/object/class_db.cpp` (`_add_class`, `_instantiate_internal`, `get_class_list`, `class_exists`)
- `core/object/object.h` (`FOUNDRY_CLASS` macro, name stringification)
- `modules/foundry_script/foundry_script.cpp` (`FSLanguage::init` native global registration, `FSNativeClass`)
- `modules/foundry_script/fs_analyzer.cpp` (`class_exists`, `reduce_identifier`, `resolve_datatype`, `get_global_class_in_namespace`, `get_imported_global_class`, `get_namespace_global_class_from_type_chain`)
- `modules/foundry_script/fs_compiler.cpp` (native base resolution via the global map)
- `modules/foundry_script/fs_editor.cpp` (`_list_type_completion_options`, `_list_importable_namespaces`)
- `modules/foundry_script/GRAMMAR.md` (§3 namespace/import, §4.2 reachability)
- `scene/resources/packed_scene.{h,cpp}` (`SceneState` names table, `NodeData.type`, instantiation choke point, `MissingNode`)
- `editor/gui/create_dialog.cpp` (`_fill_type_list`, confirm/instantiate)
- `doc/class.xsd`, `doc/classes/` (flat-by-name doc XML, schema)
- `scene/register_scene_types.cpp` (scene subsystem registration site for the pilot class)
