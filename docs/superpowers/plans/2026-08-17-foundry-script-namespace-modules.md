# Foundry Script Namespace Modules Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or
> superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add Foundry Script module source units that publish multiple namespace-scoped type aliases, classes,
traits, enums, and tuples with selective imports, stable runtime identity, persistence, and complete editor tooling.

**Architecture:** A module remains one provider file, parser, compiler bundle, and reload transaction. A generalized
declaration index publishes many public declarations per provider; runtime-bearing exports receive stable
`FoundryScript` handles selected through qualified resource paths of the form `provider.fs::canonical.name`.
Provider/selector splitting is shared by loading and persistence, while source visibility and tooling resolve against
canonical declaration identity rather than filenames.

**Tech Stack:** C++17 engine and Foundry Script module code, EBNF in `GRAMMAR.md`, `.fs` behavioral fixtures,
doctest C++ tests, editor/LSP tests, SCons/Ninja through `scripts/agent_build.py`, GitHub native sub-issues, and the
editor automation MCP acceptance workflow.

**Design:** `docs/superpowers/specs/2026-08-16-foundry-script-namespace-modules-design.md`

**GitHub epic:**
[#2239 — Epic: Foundry Script namespace modules](https://github.com/cafecito-games/Foundry/issues/2239)

---

## Execution rules

- Implement each numbered task as one native GitHub sub-issue and one focused PR.
- Create `feature/foundry-script-namespace-modules` from current `develop`; tasks 1-14 target that branch in the
  dependency order below. Task 15 opens the integration PR from the feature branch to `develop`.
- Do not merge a partial `module` grammar into `develop`. The long-lived feature branch is the visibility boundary;
  no second feature flag or alias-only public subset is introduced.
- Start every child from the current feature-branch head after all of its native blockers merge. Independent children
  may use separate worktrees only when the dependency graph permits it.
- Begin each behavior with an observable failing test. Do not add tests that inspect source or Markdown text.
- Grammar changes update `modules/foundry_script/GRAMMAR.md` in the same child.
- Generated files and test projects use the wrapper-provided `.test_scratch`; do not mutate tracked fixtures except
  through the documented fixture-generation commands.
- Focused iteration may use `python3 scripts/agent_build.py --backend ninja --test ...`. Before each PR handoff run
  the native strict wrapper without `--backend`; task 15 runs the complete suite with GUI coverage enabled.
- Use the command-first Foundry CLI and `--case`/`--suite`. Do not introduce deprecated legacy CLI forms.
- Stage only files owned by the current child. Every child ends with the focused commit subject listed in its task.

## Dependency graph

```text
N1 -> N2
N1 + N2 -> N3 -> N4
N2 + N4 -> N5 -> N6
N2 + N6 -> N7
N2 + N5 -> N8
N6 + N8 -> N9
N8 -> N10
N5 + N8 -> N11
N2 + N8 + N11 -> N12
N2 + N5 + N8 + N11 + N12 -> N13
N2 + N3 + N4 -> N14
N7 + N9 + N10 + N11 + N12 + N13 + N14 -> N15
```

## Native issue manifest

| ID | Issue | Exact title | Native blockers |
| --- | --- | --- | --- |
| N1 | [#2240][n1] | Parse and format Foundry Script module source units | none |
| N2 | [#2241][n2] | Generalize global declaration indexing to many declarations per file | N1 |
| N3 | [#2242][n3] | Resolve selective imports and namespace-scoped public declarations | N1, N2 |
| N4 | [#2243][n4] | Enforce module visibility, alias cycles, and public API leakage | N3 |
| N5 | [#2244][n5] | Compile module bundles and stable exported class handles | N2, N4 |
| N6 | [#2245][n6] | Compile exported traits, enums, tuples, and erased aliases | N5 |
| N7 | [#2246][n7] | Export module bundles to bytecode and protect public names | N2, N6 |
| N8 | [#2247][n8] | Load qualified export resource paths through core and FSCache | N2, N5 |
| N9 | [#2248][n9] | Reconcile module reloads transactionally with export tombstones | N6, N8 |
| N10 | [#2249][n10] | Preserve qualified export references through scenes and dependencies | N8 |
| N11 | [#2250][n11] | Enforce module attachment and script-facing ResourceLoader boundaries | N5, N8 |
| N12 | [#2251][n12] | Persist exported autoload and main-loop selections | N2, N8, N11 |
| N13 | [#2252][n13] | Integrate module declarations with editor filesystem and pickers | N2, N5, N8, N11, N12 |
| N14 | [#2253][n14] | Add LSP, refactoring, and documentation support for module exports | N2, N3, N4 |
| N15 | [#2254][n15] | Complete namespace-module end-to-end validation and integration | N7, N9-N14 |

[n1]: https://github.com/cafecito-games/Foundry/issues/2240
[n2]: https://github.com/cafecito-games/Foundry/issues/2241
[n3]: https://github.com/cafecito-games/Foundry/issues/2242
[n4]: https://github.com/cafecito-games/Foundry/issues/2243
[n5]: https://github.com/cafecito-games/Foundry/issues/2244
[n6]: https://github.com/cafecito-games/Foundry/issues/2245
[n7]: https://github.com/cafecito-games/Foundry/issues/2246
[n8]: https://github.com/cafecito-games/Foundry/issues/2247
[n9]: https://github.com/cafecito-games/Foundry/issues/2248
[n10]: https://github.com/cafecito-games/Foundry/issues/2249
[n11]: https://github.com/cafecito-games/Foundry/issues/2250
[n12]: https://github.com/cafecito-games/Foundry/issues/2251
[n13]: https://github.com/cafecito-games/Foundry/issues/2252
[n14]: https://github.com/cafecito-games/Foundry/issues/2253
[n15]: https://github.com/cafecito-games/Foundry/issues/2254

## File responsibility map

- Syntax and AST: `modules/foundry_script/fs_tokenizer.{h,cpp}`, `fs_parser.{h,cpp}`, `fs_format.{h,cpp}`, and
  `GRAMMAR.md` own contextual keywords, module source kind, structured imports, export modifiers, and formatting.
- Declaration index: `core/object/script_language.{h,cpp}`, `script_language_extension.h`,
  `modules/foundry_script/foundry_script.{h,cpp}`, and `editor/file_system/editor_file_system.{h,cpp}` own the
  one-to-many language API, generation-tagged snapshots, persisted cache, and `ScriptServer` registration.
- Name resolution and visibility: `fs_analyzer*.{h,cpp}`, `fs_parser_data_type.cpp`, `fs_cache.{h,cpp}`, and the
  conformance registry own import bindings, lookup precedence, dependency edges, cycles, and public API checks.
- Compilation and reload: `fs_compiler.{h,cpp}`, `foundry_script.{h,cpp}`, `fs_cache.{h,cpp}`, and
  `fs_conformance_registry.*` own bundles, export tables, stable handles, FQNs, staged compilation, and tombstones.
- Export: `fs_bytecode_format.h`, `fs_bytecode_{export,loader}.*`, `fs_name_mangler_*`, and editor export integration
  own stripped bundles, versioning, public-name preservation, and dependency closure.
- Resource paths: new `core/io/resource_path.{h,cpp}`, `core/io/resource_loader.*`, `core/io/resource.*`, and the
  Foundry format loader own provider/selector parsing, cache keys, external classification, and loader dispatch.
- Persistence: `scene/resources/resource_format_text.cpp`, `core/io/resource_format_binary.cpp`, dependency editor,
  editor filesystem, and export/PCK code own UID/remap reattachment and dependency-string preservation.
- Attachment and settings: `core/object/script_language.h`, `core/object/object.cpp`, `core/core_bind.cpp`,
  `core/config/project_settings.*`, `main/main.cpp`, and editor settings/pickers own non-attachability and structured
  autoload/main-loop selections.
- Editor and language tooling: editor filesystem/script/picker code plus `fs_editor.cpp`, `language_server/*`,
  `editor/fs_refactoring*`, and `editor/fs_docgen.cpp` own display, navigation, completion, rename, and documentation.
- Tests: focused headers under `modules/foundry_script/tests/`, core/editor headers under `tests/`, script fixtures
  under `modules/foundry_script/tests/scripts/`, and `tests/test_main.cpp` own observable acceptance coverage.

## Task N1: Parse and format Foundry Script module source units

**Issue boundary:** Syntax, AST storage, source-kind diagnostics, and formatter behavior only. Do not resolve imports,
publish declarations, or compile exports in this child.

**Labels:** `enhancement`, `priority:P2-normal`, `area:foundry-script`, `area:test-infra`

**Files:**

- Modify: `modules/foundry_script/fs_tokenizer.h`
- Modify: `modules/foundry_script/fs_tokenizer.cpp`
- Modify: `modules/foundry_script/fs_parser.h`
- Modify: `modules/foundry_script/fs_parser.cpp`
- Modify: `modules/foundry_script/fs_format.h`
- Modify: `modules/foundry_script/fs_format.cpp`
- Modify: `modules/foundry_script/GRAMMAR.md`
- Create: `modules/foundry_script/tests/test_namespace_module_parser.h`
- Modify: `tests/test_main.cpp`
- Create: `modules/foundry_script/tests/scripts/parser/features/namespace_module_declarations.norun.fs`
- Create: `modules/foundry_script/tests/scripts/parser/errors/namespace_module_invalid_members.fs`
- Create: `modules/foundry_script/tests/scripts/parser/errors/namespace_module_invalid_members.out`
- Create: `modules/foundry_script/tests/scripts/format/namespace_module/input.fs`
- Create: `modules/foundry_script/tests/scripts/format/namespace_module/expected.fs`

- [ ] **Step 1: Add the failing source-kind and structured-import doctest**

Add the new header to `tests/test_main.cpp`. Parse this source and assert the complete stored shape:

```cpp
TEST_CASE("[Modules][FoundryScript][NamespaceModule] Parser stores module exports and selective imports") {
	FSParser parser;
	const Error err = parser.parse(
			"namespace my_project.models\n"
			"import shared.core\n"
			"import { EntityID, Entity as ModelEntity, } from shared.models\n\n"
			"module\n\n"
			"type PrivateInteger = int | uint\n"
			"export type EntityID = PrivateInteger\n"
			"@tool\nexport abstract class Entity extends RefCounted:\n\tpass\n",
			"user://namespace_module_parser.fs", false);
	REQUIRE_EQ(err, OK);
	CHECK_EQ(parser.head->source_kind, FSParser::ClassNode::SOURCE_MODULE);
	CHECK_EQ(parser.head->namespace_name, "my_project.models");
	REQUIRE_EQ(parser.head->import_declarations.size(), 2);
	CHECK_EQ(parser.head->import_declarations[1]->items[1].local_name(), SNAME("ModelEntity"));
	CHECK_FALSE(parser.head->get_member(SNAME("PrivateInteger")).type_alias->is_exported);
	CHECK(parser.head->get_member(SNAME("EntityID")).type_alias->is_exported);
	CHECK(parser.head->get_member(SNAME("Entity")).m_class->is_exported);
}
```

- [ ] **Step 2: Run the focused test and record the expected failure**

Run:

```sh
python3 scripts/agent_build.py --backend ninja --test --case "*NamespaceModule*Parser*"
```

Expected: compile failure because `source_kind`, `import_declarations`, and `is_exported` do not exist, or parser
failure at `module`/the selective import.

- [ ] **Step 3: Add contextual tokens and AST records**

Add `MODULE`, `EXPORT`, and `FROM` token spellings and keep them identifier-compatible outside their declaration
positions. Add these parser records (names are normative for later tasks):

```cpp
struct ImportNode : public Node {
	struct Item {
		IdentifierNode *imported = nullptr;
		IdentifierNode *alias = nullptr;
		StringName local_name() const { return alias != nullptr ? alias->name : imported->name; }
	};
	String namespace_name;
	Vector<Item> items; // Empty means namespace-wide import.
};

enum SourceKind { SOURCE_CLASS, SOURCE_ENUM, SOURCE_TUPLE, SOURCE_MODULE };
SourceKind source_kind = SOURCE_CLASS;
Vector<ImportNode *> import_declarations;
bool is_exported = false; // On direct type declaration nodes; false everywhere else.
```

`ImportNode` is the source of truth. Replace direct iteration over `head->imports` with helpers that project namespace
reach from `import_declarations`; do not maintain two mutable import lists.

- [ ] **Step 4: Parse the module head and legal direct declaration set**

Teach `parse_program()` to recognize `module` in the source-unit-head slot, require a non-empty namespace, reject
top-level script annotations/head modifiers, require a newline without an indent, and parse file-scope declarations
until EOF. Reuse the normal declaration parsers after collecting modifiers:

```cpp
void FSParser::parse_module_member() {
	DeclarationModifiers modifiers = collect_declaration_modifiers(true); // Includes contextual `export`.
	// Dispatch only TYPE_ALIAS, CLASS, TRAIT, ENUM, or TUPLE and copy modifiers.is_exported.
}
```

Emit targeted diagnostics for `export var`, `export func`, nested/non-module `export`, `extends`, `uses`, constants,
signals, properties, statements, annotations, and retroactive conformances at module scope. Apply `@tool`, `@icon`,
and `@static_unload` only to the following direct class.

- [ ] **Step 5: Parse namespace-wide and selective imports into one ordered list**

Implement `parse_import()` so both forms append an `ImportNode` in source order. Selective braces accept newlines and
a trailing comma; `as` stores the local alias; `from` is contextual only in this production. Preserve repeated items
for analyzer diagnostics instead of deduplicating in the parser.

- [ ] **Step 6: Format module heads, modifiers, comments, annotations, and imports**

Teach `FSPrinter` to emit imports in their original interleaved order, normalize selective-list commas/indentation,
emit a colonless/unindented `module`, and print `export` before `abstract`/`final` regardless of accepted source order.
The formatter must retain documentation comments and annotations on the following declaration.

- [ ] **Step 7: Update the normative grammar and exact fixtures**

Add `module_head`, `module_member`, `module_declaration_modifier`, `module_type_decl`, `selective_import`, and
`import_item` to `GRAMMAR.md` in the actual `program` and import productions. Fixtures cover every accepted member,
private/exported declarations, multiline selective imports, own-namespace imports, contextual value-position uses,
invalid head annotations/modifiers/indentation, and all forbidden members.

- [ ] **Step 8: Regenerate fixtures and run focused verification**

```sh
./bin/foundry.* --headless test generate-fixtures modules/foundry_script/tests/scripts/parser
./bin/foundry.* --headless test generate-format-fixtures modules/foundry_script/tests/scripts/format/namespace_module
python3 scripts/agent_build.py --backend ninja --test --case "*NamespaceModule*Parser*"
./bin/foundry.* --headless test run --suite "*[Modules][FoundryScript][Parser]*" --force-colors
python3 scripts/agent_build.py
```

Expected: focused tests and parser suite pass; native strict build exits zero.

- [ ] **Step 9: Commit**

```sh
git add modules/foundry_script/GRAMMAR.md modules/foundry_script/fs_tokenizer.* \
  modules/foundry_script/fs_parser.* modules/foundry_script/fs_format.* \
  modules/foundry_script/tests/test_namespace_module_parser.h \
  modules/foundry_script/tests/scripts/parser modules/foundry_script/tests/scripts/format/namespace_module \
  tests/test_main.cpp
git commit -m "Add Foundry Script module source syntax"
```

## Task N2: Generalize global declaration indexing to many declarations per file

**Issue boundary:** Project-wide declaration records, snapshot/cache lifecycle, and `ScriptServer` registration only.
Do not make imports resolve or compile module exports.

**Labels:** `enhancement`, `priority:P2-normal`, `area:foundry-script`, `area:engine-runtime`, `area:editor`,
`area:test-infra`

**Files:**

- Create: `core/io/resource_path.h`
- Create: `core/io/resource_path.cpp`
- Modify: `core/object/script_language.h`
- Modify: `core/object/script_language.cpp`
- Modify: `core/object/script_language_extension.h`
- Modify: `modules/foundry_script/foundry_script.h`
- Modify: `modules/foundry_script/foundry_script.cpp`
- Modify: `editor/file_system/editor_file_system.h`
- Modify: `editor/file_system/editor_file_system.cpp`
- Create: `tests/core/io/test_resource_path.h`
- Create: `modules/foundry_script/tests/test_namespace_module_index.h`
- Modify: `modules/foundry_script/tests/test_global_class_startup.h`
- Modify: `tests/test_main.cpp`

- [ ] **Step 1: Add failing path and declaration-list tests**

The core test pins composition/splitting without treating ordinary engine subresources as module exports:

```cpp
TEST_CASE("[Core][ResourcePath] Qualified declaration paths preserve the complete selector") {
	QualifiedResourcePath path = QualifiedResourcePath::parse(
			"res://models/module.fs::my_project.models.Entity");
	CHECK(path.is_qualified());
	CHECK_EQ(path.provider, "res://models/module.fs");
	CHECK_EQ(path.selector, "my_project.models.Entity");
	CHECK_EQ(path.join(), "res://models/module.fs::my_project.models.Entity");
}
```

The Foundry test writes one module with five exports and asserts `get_global_declarations()` returns all five in
source order, with canonical name, kind, provider, declaration name/range, base, flags, and icon metadata.

- [ ] **Step 2: Run the focused tests and verify missing APIs fail**

```sh
python3 scripts/agent_build.py --backend ninja --test \
  --case "*Qualified declaration paths*" --case "*NamespaceModule*Index*"
```

Expected: compile failure for `QualifiedResourcePath`, `GlobalDeclaration`, and the list API.

- [ ] **Step 3: Add the compatibility-safe declaration-list API**

Define a core record with stable serialization fields:

```cpp
struct ScriptGlobalDeclaration {
	enum Kind { CLASS, TRAIT, ENUM, TUPLE, TYPE_ALIAS };
	StringName canonical_name;
	Kind kind = CLASS;
	String source_path;
	StringName declaration_name;
	int start_line = 0;
	int start_column = 0;
	int end_line = 0;
	int end_column = 0;
	StringName base_identity;
	String icon_path;
	bool is_abstract = false;
	bool is_tool = false;
	bool is_static_unload = false;
};
```

Add `ScriptLanguage::get_global_declarations(path, List<ScriptGlobalDeclaration> *)`. Its default calls the legacy
single-declaration API and returns zero or one record. Foundry Script overrides the list API. Do not add a new
`ScriptLanguageExtension` virtual; extensions inherit the adapter.

- [ ] **Step 4: Extract complete module snapshots without running the analyzer**

Extend the existing `_parse_indexed_declarations()` path. On a successful parse, collect every exported direct type
declaration. On parse failure, retain the last complete snapshot; on successful reparse, delete/move, or rename,
atomically replace the entire provider list under the existing generation token. Store both:

```cpp
HashMap<String, Vector<ScriptGlobalDeclaration>> declarations_by_path;
HashMap<StringName, Vector<String>> declaration_paths_by_identity;
```

Keep custom annotations and conformances in the same `commit_declaration_index_refresh()` transaction. Duplicate
canonical identities retain every provider for deterministic diagnostics.

- [ ] **Step 5: Generalize `ScriptServer` registration and cache persistence**

Register runtime-bearing module records under
`QualifiedResourcePath::compose(provider, canonical_name)`. Type aliases remain only in the declaration index.
Replace one-result scan loops with the list API. `remove_global_class_by_path(provider)` compares parsed provider
portions and removes every match without returning early. Version the persisted global declaration/global class
cache, store declaration lists and qualified paths, rebuild invalid editor caches, and fail stripped startup clearly
when an incompatible cache cannot be rebuilt.

- [ ] **Step 6: Make editor filesystem class metadata one-to-many**

Replace the single `ScriptClassInfo` field/accessors with an ordered vector. Ordinary scripts still expose one entry.
All create/update/delete/move paths replace the complete vector, and stale async generations cannot overwrite a newer
snapshot. Keep compatibility accessors only where a caller truly requires the legacy root declaration.

- [ ] **Step 7: Add lifecycle and cache-version regression coverage**

Tests cover create, edit, add/remove export, delete, rename, provider move, parse failure retaining last-good index,
successful parse replacing it, duplicate identity, removal of all classes by provider, ordinary-language adapter,
extension adapter, cache save/load, old-cache rebuild in editor, and old-cache rejection in stripped startup.

- [ ] **Step 8: Run focused and strict verification**

```sh
python3 scripts/agent_build.py --backend ninja --test \
  --case "*NamespaceModule*Index*" --case "*Global class*cache*" --case "*GlobalClassStartup*"
python3 scripts/agent_build.py
```

Expected: every selected case passes and the native strict build exits zero.

- [ ] **Step 9: Commit**

```sh
git add core/io/resource_path.* core/object/script_language.* \
  core/object/script_language_extension.h modules/foundry_script/foundry_script.* \
  editor/file_system/editor_file_system.* tests/core/io/test_resource_path.h \
  modules/foundry_script/tests/test_namespace_module_index.h \
  modules/foundry_script/tests/test_global_class_startup.h tests/test_main.cpp
git commit -m "Index multiple global declarations per script"
```

## Task N3: Resolve selective imports and namespace-scoped public declarations

**Issue boundary:** Import validation, local bindings, lookup precedence, native selections, namespace reach, and
dependency invalidation. Private leakage and cross-file type-cycle diagnostics belong to N4.

**Labels:** `enhancement`, `priority:P2-normal`, `area:foundry-script`, `area:test-infra`

**Files:**

- Modify: `modules/foundry_script/fs_analyzer.h`
- Modify: `modules/foundry_script/fs_analyzer.cpp`
- Modify: `modules/foundry_script/fs_analyzer_surface.cpp`
- Modify: `modules/foundry_script/fs_analyzer_conformance.cpp`
- Modify: `modules/foundry_script/fs_cache.h`
- Modify: `modules/foundry_script/fs_cache.cpp`
- Create: `modules/foundry_script/tests/scripts/analyzer/features/namespace_module_imports/provider.fs`
- Create: `modules/foundry_script/tests/scripts/analyzer/features/namespace_module_imports/consumer.fs`
- Create: `modules/foundry_script/tests/scripts/analyzer/errors/namespace_module_import_collisions.fs`
- Create: `modules/foundry_script/tests/scripts/analyzer/errors/namespace_module_import_collisions.out`
- Create: `modules/foundry_script/tests/test_namespace_module_resolution.h`
- Modify: `tests/test_main.cpp`

- [ ] **Step 1: Add failing resolution fixtures and an invalidation doctest**

The positive fixtures must exercise wildcard, selective, renamed, implicit-own-namespace, fully qualified,
standalone-global, and namespaced-native imports. The C++ test parses a consumer before an export exists, adds the
export to the provider snapshot, calls the namespace invalidator, and verifies the consumer reparses successfully.

- [ ] **Step 2: Run focused tests and verify unresolved imports fail**

```sh
python3 scripts/agent_build.py --backend ninja --test \
  --case "*NamespaceModule*Resolution*" --suite "*[Modules][FoundryScript][Analyzer]*"
```

Expected: selective imports are unknown/unresolved and the invalidation test fails before implementation.

- [ ] **Step 3: Build a validated selective-binding table**

Add an analyzer-owned record and populate it during `validate_imports()`:

```cpp
struct SelectiveImportBinding {
	StringName local_name;
	StringName canonical_name;
	ScriptGlobalDeclaration::Kind kind;
	String provider_path; // Empty for a native declaration.
};
HashMap<StringName, SelectiveImportBinding> selective_import_bindings;
```

Diagnose missing/private declarations, duplicate source items, duplicate local names, lexical collisions, aliases to
reserved compiler/builtin/unqualified native names, and canonical duplicates. A selective import from the file's own
namespace is valid. Namespace-wide and selective import cycles remain legal.

- [ ] **Step 4: Implement the exact bare-type precedence**

Refactor type lookup into one ordered path matching the design: local/type parameters; compiler and flat native
types; lexical types; selective bindings; own namespace; wildcard imports; flat globals. Value lookup admits classes,
enums, tuples, and traits according to existing capabilities but rejects erased aliases with the specific type-only
diagnostic. Fully qualified chains query the declaration index directly after root-shadow checks.

- [ ] **Step 5: Resolve namespaced native declarations without project generations**

Use `ClassDB::class_get_in_namespace()` for selective native items. Native namespace reach validates against the
existing registry-version cache but does not subscribe the consumer to a project declaration generation. If scripts
also contribute to that namespace, only the script declarations create project invalidation edges.

- [ ] **Step 6: Record provider edges and namespace generations**

Every resolved script export adds a direct parser dependency on its provider. Every namespace lookup records the
declaration-index generation observed, including failed lookups. Add `notify_declaration_namespace_changed()` beside
the existing conformance invalidator so add/remove/rename/kind changes invalidate cached consumers that previously
had no provider edge. Selective imports establish conformance reach for the whole namespace without widening lexical
name visibility.

- [ ] **Step 7: Cover collision, ambiguity, and reach behavior**

Fixtures assert all precedence levels, wildcard ambiguity, explicit alias disambiguation, duplicate imports,
local/import conflicts, built-in protection, native selection, own-namespace selection, unrelated-name invisibility,
custom-annotation invisibility, and namespace-wide retroactive-conformance reach.

- [ ] **Step 8: Run focused and strict verification**

```sh
./bin/foundry.* --headless test generate-fixtures modules/foundry_script/tests/scripts/analyzer
python3 scripts/agent_build.py --backend ninja --test \
  --case "*NamespaceModule*Resolution*" --suite "*[Modules][FoundryScript][Analyzer]*"
python3 scripts/agent_build.py
```

- [ ] **Step 9: Commit**

```sh
git add modules/foundry_script/fs_analyzer* modules/foundry_script/fs_cache.* \
  modules/foundry_script/tests/test_namespace_module_resolution.h \
  modules/foundry_script/tests/scripts/analyzer tests/test_main.cpp
git commit -m "Resolve selective namespace imports"
```

## Task N4: Enforce module visibility, alias cycles, and public API leakage

**Issue boundary:** Provider-context alias expansion, private visibility, cross-parser type cycles, duplicate export
diagnostics, and one complete public-surface leak validator.

**Labels:** `enhancement`, `priority:P2-normal`, `area:foundry-script`, `area:test-infra`

**Files:**

- Modify: `modules/foundry_script/fs_analyzer.h`
- Modify: `modules/foundry_script/fs_analyzer.cpp`
- Modify: `modules/foundry_script/fs_analyzer_surface.cpp`
- Modify: `modules/foundry_script/fs_parser_data_type.cpp`
- Modify: `modules/foundry_script/fs_cache.h`
- Modify: `modules/foundry_script/fs_cache.cpp`
- Create: `modules/foundry_script/tests/test_namespace_module_visibility.h`
- Create: `modules/foundry_script/tests/scripts/analyzer/features/namespace_module_aliases/provider.fs`
- Create: `modules/foundry_script/tests/scripts/analyzer/features/namespace_module_aliases/consumer.fs`
- Create: `modules/foundry_script/tests/scripts/analyzer/errors/namespace_module_private_leak.fs`
- Create: `modules/foundry_script/tests/scripts/analyzer/errors/namespace_module_private_leak.out`
- Create: `modules/foundry_script/tests/scripts/analyzer/errors/namespace_module_cycles/a.fs`
- Create: `modules/foundry_script/tests/scripts/analyzer/errors/namespace_module_cycles/b.fs`
- Modify: `tests/test_main.cpp`

- [ ] **Step 1: Add failing alias-context, cycle, and leak tests**

Fixtures prove that a public alias expands using the provider's imports, a private alias may erase to public/builtin
structure, private nominal types stay file-local, and cycles crossing providers report every canonical identity and
provider. The doctest feeds representative nested `DataType` values to one leak checker.

- [ ] **Step 2: Run focused tests and verify current lexical alias behavior fails**

```sh
python3 scripts/agent_build.py --backend ninja --test \
  --case "*NamespaceModule*Visibility*" --suite "*[Modules][FoundryScript][Analyzer]*"
```

- [ ] **Step 3: Resolve exports through their provider analyzer**

Introduce a dependency-parser access operation that returns the declaration node and analyzer for an indexed record.
Alias expansion and declaration analysis execute under `ForeignAnalyzerVisibilityScope` for that provider; they never
reuse the consumer's imports. Cache the result by canonical identity plus provider generation.

- [ ] **Step 4: Add one cross-parser resolution stack**

Use a scoped stack entry shared by alias, inheritance, trait, enum-payload, and tuple resolution:

```cpp
struct ExportResolutionKey {
	StringName canonical_name;
	String provider_path;
	bool operator==(const ExportResolutionKey &p_other) const;
};
```

On re-entry, emit the ordered canonical/provider chain. Legal import-only cycles never push this stack and remain
accepted.

- [ ] **Step 5: Implement the cycle-safe public-type traversal**

Add one visitor over `FSParser::DataType` that traverses unions, generic arguments, array/dictionary elements,
callable parameters/return, enum payloads, tuple fields, nullable wrappers, and `Type[...]` wrappers. Feed it every
exported class base, used trait, generic bound, property, constant type that is public API, method signature, enum
payload, tuple field, and fully expanded alias. Report the outward member path and private canonical/internal type.

- [ ] **Step 6: Enforce file-local private identity and global export uniqueness**

Private declarations resolve only while the provider analyzer is active and use provider-qualified internal FQNs.
Exported canonical identities collide with module exports and standalone class/trait/enum/tuple declarations. Emit a
diagnostic at every conflicting declaration in deterministic provider/source order.

- [ ] **Step 7: Expand fixtures across every nested type slot**

Add positive and negative cases for private aliases erasing to `int | uint`, private classes in each API surface,
two equal alias expansions in different namespaces, parameterized-alias rejection, and cross-file alias/inheritance/
trait/enum/tuple cycles.

- [ ] **Step 8: Run focused and strict verification**

```sh
./bin/foundry.* --headless test generate-fixtures modules/foundry_script/tests/scripts/analyzer
python3 scripts/agent_build.py --backend ninja --test \
  --case "*NamespaceModule*Visibility*" --suite "*[Modules][FoundryScript][Analyzer]*"
python3 scripts/agent_build.py
```

- [ ] **Step 9: Commit**

```sh
git add modules/foundry_script/fs_analyzer* modules/foundry_script/fs_parser_data_type.cpp \
  modules/foundry_script/fs_cache.* modules/foundry_script/tests/test_namespace_module_visibility.h \
  modules/foundry_script/tests/scripts/analyzer tests/test_main.cpp
git commit -m "Enforce namespace module visibility"
```

## Task N5: Compile module bundles and stable exported class handles

**Issue boundary:** Compile one module container and its direct classes, establish FQN/ownership invariants, and
construct/execute exported classes. Qualified external resource paths, non-class export kinds, and staged reload are
separate children.

**Labels:** `enhancement`, `priority:P2-normal`, `area:foundry-script`, `area:test-infra`

**Files:**

- Modify: `modules/foundry_script/foundry_script.h`
- Modify: `modules/foundry_script/foundry_script.cpp`
- Modify: `modules/foundry_script/fs_compiler.h`
- Modify: `modules/foundry_script/fs_compiler.cpp`
- Modify: `modules/foundry_script/fs_cache.h`
- Modify: `modules/foundry_script/fs_cache.cpp`
- Modify: `modules/foundry_script/fs_conformance_registry.cpp`
- Create: `modules/foundry_script/tests/test_namespace_module_runtime.h`
- Create: `modules/foundry_script/tests/scripts/runtime/features/namespace_module_classes/module.fs`
- Create: `modules/foundry_script/tests/scripts/runtime/features/namespace_module_classes/main.fs`
- Modify: `tests/test_main.cpp`

- [ ] **Step 1: Add failing bundle, FQN, and construction tests**

The test compiles a module with private, public, abstract, nested, and two same-provider public classes. Assert:

```cpp
REQUIRE(module->is_module_root());
CHECK_EQ(module->get_global_name(), StringName());
CHECK_EQ(module->get_fully_qualified_name(), FoundryScript::canonicalize_path(provider));
Ref<FoundryScript> entity = module->get_module_export("my_project.models.Entity");
REQUIRE(entity.is_valid());
CHECK_EQ(entity->get_fully_qualified_name(), "my_project.models.Entity");
CHECK_EQ(entity->find_class("my_project.models.Entity::Nested")->get_fully_qualified_name(),
		"my_project.models.Entity::Nested");
CHECK(module->has_class(entity.ptr()));
Callable::CallError call_error;
Variant instance = entity->_new(nullptr, 0, call_error);
CHECK_EQ(call_error.error, Callable::CallError::CALL_OK);
CHECK_EQ(instance.call("describe"), "entity");
```

- [ ] **Step 2: Run the focused test and verify no module bundle exists**

```sh
python3 scripts/agent_build.py --backend ninja --test --case "*NamespaceModule*Runtime*"
```

- [ ] **Step 3: Add explicit module-root and export-table state**

Add a compiled source-unit kind and export descriptor:

```cpp
enum SourceUnitKind { SOURCE_UNIT_SCRIPT, SOURCE_UNIT_MODULE };
struct ModuleExport {
	ScriptGlobalDeclaration declaration;
	Ref<FoundryScript> script; // Valid for class/trait; later children add enum/tuple runtime metadata.
};
SourceUnitKind source_unit_kind = SOURCE_UNIT_SCRIPT;
HashMap<StringName, ModuleExport> module_exports;
```

The module root has no `global_name`, no base, cannot construct, and keeps its provider-canonical internal FQN. It is
an internal compilation owner, not a user-visible class.

- [ ] **Step 4: Make `FSCompiler::make_scripts()` module-aware**

Compile direct module classes as non-prefix children. Key public direct classes in `subclasses` by complete canonical
FQN; key private direct classes by short name and assign `<provider>::PrivateName`. Key lexical children by short name
under their real owner. Populate the export table only after each declaration is compiled successfully.

- [ ] **Step 5: Extend parser/runtime class navigation**

Update `FSParser::find_class()` / `has_class()` and `FoundryScript::find_class()` / `has_class()` to check the module's
explicit direct-child table before prefix traversal, then traverse `::Nested` segments. Cover module root, public
direct export, nested public child, private direct class, and ordinary script behavior.

- [ ] **Step 6: Keep FSCache provider-keyed and runtime identity FQN-keyed**

`shallow_fs_cache`, `full_fs_cache`, and parser maps store only provider paths. `static_fs_cache`, conformances, and
orphan adoption use class FQNs. Add `FSCache::get_module_export(provider, canonical_name, error)` that compiles the
bundle once and selects the exact record without exposing the root through script APIs.

- [ ] **Step 7: Execute exported class behavior and rejection paths**

Runtime fixtures construct an exported `RefCounted`, inherit from an exported base, call nested classes, use a
private helper internally, and prove two exports share one bundle load. Analyzer/runtime diagnostics reject treating
the module root as a value, constructor, or base.

- [ ] **Step 8: Run focused and strict verification**

```sh
python3 scripts/agent_build.py --backend ninja --test --case "*NamespaceModule*Runtime*"
./bin/foundry.* --headless test run --case "*namespace_module_classes*" --force-colors
python3 scripts/agent_build.py
```

- [ ] **Step 9: Commit**

```sh
git add modules/foundry_script/foundry_script.* modules/foundry_script/fs_compiler.* \
  modules/foundry_script/fs_cache.* modules/foundry_script/fs_conformance_registry.cpp \
  modules/foundry_script/tests/test_namespace_module_runtime.h \
  modules/foundry_script/tests/scripts/runtime/features/namespace_module_classes tests/test_main.cpp
git commit -m "Compile namespace module class bundles"
```

## Task N6: Compile exported traits, enums, tuples, and erased aliases

**Issue boundary:** Complete runtime/value behavior for every allowed declaration kind using the bundle from N5.
Bytecode/export and transactional reload remain out of scope.

**Labels:** `enhancement`, `priority:P2-normal`, `area:foundry-script`, `area:test-infra`

**Files:**

- Modify: `modules/foundry_script/foundry_script.h`
- Modify: `modules/foundry_script/foundry_script.cpp`
- Modify: `modules/foundry_script/fs_compiler.h`
- Modify: `modules/foundry_script/fs_compiler.cpp`
- Modify: `modules/foundry_script/fs_analyzer.cpp`
- Modify: `modules/foundry_script/fs_analyzer_conformance.cpp`
- Modify: `modules/foundry_script/fs_parser_data_type.cpp`
- Create: `modules/foundry_script/tests/test_namespace_module_kinds.h`
- Create: `modules/foundry_script/tests/scripts/runtime/features/namespace_module_kinds/module.fs`
- Create: `modules/foundry_script/tests/scripts/runtime/features/namespace_module_kinds/main.fs`
- Create: `modules/foundry_script/tests/scripts/runtime/errors/namespace_module_alias_value.fs`
- Create: `modules/foundry_script/tests/scripts/runtime/errors/namespace_module_alias_value.out`
- Modify: `tests/test_main.cpp`

- [ ] **Step 1: Add failing per-kind runtime tests**

Define one module exporting an alias, trait, payload enum, plain enum, and tuple. Tests use each through selective,
wildcard, implicit, and qualified resolution; alias use in value position must produce the type-only diagnostic.

- [ ] **Step 2: Run focused tests and verify export-table kinds are incomplete**

```sh
python3 scripts/agent_build.py --backend ninja --test --case "*NamespaceModule*Kinds*"
```

- [ ] **Step 3: Extend `ModuleExport` with kind-specific compiled metadata**

Use one tagged descriptor rather than synthesizing files:

```cpp
struct ModuleExport {
	ScriptGlobalDeclaration declaration;
	Ref<FoundryScript> script;
	FSParser::DataType erased_alias_type;
	FSParser::DataType enum_type;
	FSParser::DataType tuple_type;
};
```

Only class/trait entries own `FoundryScript` handles. Alias metadata is analysis-only and erased from runtime values;
enum and tuple metadata retain existing nominal representations and constructors.

- [ ] **Step 4: Compile exported traits and conformance identities**

Give a direct exported trait the canonical FQN and stable script handle rules from N5. Trait use and retroactive
conformance lookup register against that FQN. Private module traits remain provider-local and may be used only by
declarations in their provider.

- [ ] **Step 5: Publish enum and tuple value surfaces**

Reuse existing global enum and tuple lowering with the provider path plus canonical declaration identity. Preserve
enum values/payload constructors/methods, tuple fields/nominal identity, equality/hash/serialization, and namespace
lookup. Do not introduce wrapper objects or generated source files.

- [ ] **Step 6: Keep aliases transparent and value-less**

Store the provider-resolved expanded `DataType` in analysis caches. Type annotations substitute it transparently;
reflection and runtime bytecode never expose an alias value. Calls, member access, `preload`, or construction against
an alias emit the exact type-only diagnostic.

- [ ] **Step 7: Add cross-kind inheritance and operation coverage**

Tests cover an exported class using an exported trait, a consumer conforming to it, enum payload construction/match,
enum methods, tuple construction/destructure/field access, alias expansion through private structure, and ordinary
standalone global declarations continuing to behave identically.

- [ ] **Step 8: Run focused and strict verification**

```sh
./bin/foundry.* --headless test generate-fixtures modules/foundry_script/tests/scripts/runtime/errors
python3 scripts/agent_build.py --backend ninja --test --case "*NamespaceModule*Kinds*"
./bin/foundry.* --headless test run --case "*namespace_module_kinds*" --force-colors
python3 scripts/agent_build.py
```

- [ ] **Step 9: Commit**

```sh
git add modules/foundry_script/foundry_script.* modules/foundry_script/fs_compiler.* \
  modules/foundry_script/fs_analyzer* modules/foundry_script/fs_parser_data_type.cpp \
  modules/foundry_script/tests/test_namespace_module_kinds.h \
  modules/foundry_script/tests/scripts/runtime tests/test_main.cpp
git commit -m "Compile every namespace module export kind"
```

## Task N7: Export module bundles to bytecode and protect public names

**Issue boundary:** Bytecode representation, stripped-runtime declaration selection, export dependency closure, and
release mangling. Source/editor loading is already provided by earlier children.

**Labels:** `enhancement`, `priority:P2-normal`, `area:foundry-script`, `area:engine-runtime`, `area:test-infra`

**Files:**

- Modify: `modules/foundry_script/fs_bytecode_format.h`
- Modify: `modules/foundry_script/fs_bytecode_export.h`
- Modify: `modules/foundry_script/fs_bytecode_export.cpp`
- Modify: `modules/foundry_script/fs_bytecode_loader.h`
- Modify: `modules/foundry_script/fs_bytecode_loader.cpp`
- Modify: `modules/foundry_script/fs_name_mangler_analysis.cpp`
- Modify: `modules/foundry_script/fs_name_mangler_application.cpp`
- Modify: `modules/foundry_script/editor/fs_name_mangler_export.cpp`
- Modify: `modules/foundry_script/tests/test_bytecode_hardening.h`
- Create: `modules/foundry_script/tests/test_namespace_module_bytecode.h`
- Modify: `modules/foundry_script/tests/test_name_mangler_export.h`
- Modify: `tests/test_main.cpp`

- [ ] **Step 1: Add failing source-to-bytecode bundle round-trip tests**

Compile one module containing every export kind, serialize it, load with source access disabled, select two class
handles plus enum/tuple metadata, and assert one bundle decode. Add a stale-version test and a missing-selector test.

- [ ] **Step 2: Run focused tests and verify the current single-root format fails**

```sh
python3 scripts/agent_build.py --backend ninja --test \
  --case "*NamespaceModule*Bytecode*" --case "*BytecodeHardening*"
```

- [ ] **Step 3: Version and encode the module export table**

Bump `FSBytecodeFormat::FORMAT_VERSION` and update the hardening pin. Encode provider identity once, then a stable
source-order table containing canonical name, kind, declaration metadata, and the section/index needed to restore its
class/trait/enum/tuple payload. Aliases contribute no runtime payload.

- [ ] **Step 4: Decode one bundle and select exact exports**

Reject any non-current format before partially publishing handles. Validate unique canonical names, valid kind tags,
section bounds, class FQN equality, and selector presence. Older bytecode reports provider plus rebuild/re-export
guidance; it never guesses a root declaration.

- [ ] **Step 5: Make the declaration index the mangler source of truth**

Feed every public declaration's short name, canonical FQN, and qualified resource-path spelling into keep evidence.
Private module declarations remain manglable unless existing reflection or `@keep_name` evidence protects them.
Ensure scene/project-setting strings and bytecode selectors are rewritten consistently from the sealed manifest.

- [ ] **Step 6: Close export dependencies and PCK inclusion**

Export one provider artifact even when several qualified exports are referenced. Include every provider dependency,
persist the declaration cache required by stripped startup, and fail export if an indexed runtime export is absent
from the compiled bundle generation.

- [ ] **Step 7: Add malformed-input and release-name coverage**

Tests mutate duplicate names, invalid kinds, out-of-range sections, mismatched FQNs, stale versions, and missing
provider dependencies. Name-mangler tests prove public spellings survive, private names can change, and qualified
scene/settings paths resolve after mangling.

- [ ] **Step 8: Run focused and strict verification**

```sh
python3 scripts/agent_build.py --backend ninja --test \
  --case "*NamespaceModule*Bytecode*" --case "*BytecodeHardening*" \
  --case "*NameManglerExport*"
python3 scripts/agent_build.py
```

- [ ] **Step 9: Commit**

```sh
git add modules/foundry_script/fs_bytecode_* modules/foundry_script/fs_name_mangler_* \
  modules/foundry_script/editor/fs_name_mangler_export.cpp \
  modules/foundry_script/tests/test_namespace_module_bytecode.h \
  modules/foundry_script/tests/test_bytecode_hardening.h \
  modules/foundry_script/tests/test_name_mangler_export.h tests/test_main.cpp
git commit -m "Export namespace module bundles"
```

## Task N8: Load qualified export resource paths through core and FSCache

**Issue boundary:** Make `provider.fs::canonical.name` a real external load input and stable resource cache key. Scene
records, project settings, attachment filtering, and reload tombstones are later children.

**Labels:** `enhancement`, `priority:P2-normal`, `area:engine-runtime`, `area:foundry-script`, `area:test-infra`

**Files:**

- Modify: `core/io/resource_path.h`
- Modify: `core/io/resource_path.cpp`
- Modify: `core/io/resource.h`
- Modify: `core/io/resource.cpp`
- Modify: `core/io/resource_loader.cpp`
- Modify: `modules/foundry_script/foundry_script.h`
- Modify: `modules/foundry_script/foundry_script.cpp`
- Modify: `modules/foundry_script/fs_cache.h`
- Modify: `modules/foundry_script/fs_cache.cpp`
- Create: `modules/foundry_script/tests/test_namespace_module_resource_path.h`
- Modify: `tests/core/io/test_resource_path.h`
- Modify: `tests/test_main.cpp`

- [ ] **Step 1: Add failing loader and cache-identity tests**

Create a module provider in test scratch and assert qualified `load`, `exists`, type, UID, dependencies, classes,
repeat-load identity, two-export distinction, bogus-selector failure, provider move, and source loading. Assert the
module root remains provider-cached for internal C++ use only.

- [ ] **Step 2: Run focused tests and verify suffix dispatch fails**

```sh
python3 scripts/agent_build.py --backend ninja --test \
  --case "*NamespaceModule*ResourcePath*" --case "*Qualified declaration paths*"
```

Expected: no loader recognizes the selector-suffixed input.

- [ ] **Step 3: Apply provider-only localization and remapping in core**

Extend `QualifiedResourcePath` with `map_provider(callable)`. `ResourceLoader::_validate_local_path()` resolves a UID,
relative path, or global path on `provider` and reattaches `selector`; `_path_remap()` performs translation/import
remapping on `provider` and reattaches the selector. Ordinary paths and existing local subresources retain behavior.

- [ ] **Step 4: Override the complete Foundry loader surface**

Declare and implement `recognize_path`, `exists`, `get_resource_type`, `get_resource_uid`,
`has_custom_uid_support`, `get_dependencies`, `rename_dependencies`, and `get_classes_used` in
`ResourceFormatLoaderFoundryScript`, in addition to `load`. Each splits first, validates `.fs`/`.fsc`/`.fsb` on the
provider, loads the bundle once through `FSCache`, and validates the exact public direct selector without fallback.

- [ ] **Step 5: Separate provider script paths from external resource paths**

Every owned handle sets internal `path` plus `path_valid = true` before assigning resource cache paths. Module root
`Resource::get_path()` is the provider; direct exported class/trait handles use the qualified path; private/nested
handles have no external path. `load_source_code()` opens `get_script_path()`.

- [ ] **Step 6: Virtualize external classification narrowly**

Change `Resource::is_built_in()` to a virtual with its current default. Direct addressable exports return false;
ordinary embedded/private resources retain the heuristic. Keep the ClassDB binding. Confirm `String::is_resource_file()`
continues to mean standalone file and remains false for qualified exports; script editor source opening uses provider
paths.

- [ ] **Step 7: Make provider moves rekey all live export resources**

`FoundryScript::set_path()` / `set_path_cache()` distinguish root/direct/private/nested handles.
`FSCache::move_script()` moves parser/bundle entries by provider and uses `set_path(new_qualified, true)` for each live
direct export. Do not insert qualified strings into parser caches.

- [ ] **Step 8: Run focused and strict verification**

```sh
python3 scripts/agent_build.py --backend ninja --test \
  --case "*NamespaceModule*ResourcePath*" --case "*ResourceCache*FSCache*"
python3 scripts/agent_build.py
```

- [ ] **Step 9: Commit**

```sh
git add core/io/resource_path.* core/io/resource.* core/io/resource_loader.cpp \
  modules/foundry_script/foundry_script.* modules/foundry_script/fs_cache.* \
  modules/foundry_script/tests/test_namespace_module_resource_path.h \
  tests/core/io/test_resource_path.h tests/test_main.cpp
git commit -m "Load qualified namespace module exports"
```

## Task N9: Reconcile module reloads transactionally with export tombstones

**Issue boundary:** Whole-bundle staged compile, last-known-good behavior, stable handle adoption, atomic publication,
and safe removal/restoration. Ordinary initial compilation and qualified loading already exist.

**Labels:** `enhancement`, `priority:P2-normal`, `area:foundry-script`, `area:engine-runtime`, `area:test-infra`

**Files:**

- Modify: `modules/foundry_script/fs_compiler.h`
- Modify: `modules/foundry_script/fs_compiler.cpp`
- Modify: `modules/foundry_script/foundry_script.h`
- Modify: `modules/foundry_script/foundry_script.cpp`
- Modify: `modules/foundry_script/fs_cache.h`
- Modify: `modules/foundry_script/fs_cache.cpp`
- Modify: `modules/foundry_script/fs_conformance_registry.h`
- Modify: `modules/foundry_script/fs_conformance_registry.cpp`
- Create: `modules/foundry_script/tests/test_namespace_module_reload.h`
- Modify: `modules/foundry_script/tests/test_foundry_script.cpp`
- Modify: `tests/test_main.cpp`

- [ ] **Step 1: Add failing transactional reload tests**

Compile a module, hold class `Ref`s and live instances, then exercise successful method/property changes, add, remove,
rename, compile failure, conformance replacement, and restore. Assert no mixed generation and exact handle identity.

- [ ] **Step 2: Run focused tests and expose live-mutation failure**

```sh
python3 scripts/agent_build.py --backend ninja --test --case "*NamespaceModule*Reload*"
```

Expected: current compilation mutates live handles before the complete module succeeds, or removal returns stale cache
entries.

- [ ] **Step 3: Add an unpublished compiled revision**

Separate compilation from publication:

```cpp
struct FSCompiledModuleRevision {
	uint64_t source_generation = 0;
	Ref<FoundryScript> staged_root;
	HashMap<StringName, Ref<FoundryScript>> staged_handles;
	Vector<FSConformanceRegistry::Entry> pending_conformances;
	Error error = OK;
};
```

Parsing, analysis, compiler output, and conformances target this revision only. A failure destroys it without touching
the active bundle, resource cache, instances, or runtime conformances.

- [ ] **Step 4: Adopt staged state into surviving live handles**

Implement `FoundryScript::adopt_compiled_state_from(staged, live_handle_map)`. Transfer functions, constants, member
metadata, trait/enum/tuple tables, and debugger/runtime state while remapping every internal script reference to the
reconciled live handle. Reuse ordinary state transfer for compatible instance properties.

- [ ] **Step 5: Reconcile add/remove/rename by canonical FQN**

Survivors keep handles and qualified paths; additions receive new handles. Rename is remove-plus-add. Removal copies
the qualified path and handle to a per-bundle tombstone table, marks the handle invalid, and calls `set_path("")` to
evict it from `ResourceCache`. Editor instances atomically become missing-script placeholders retaining serialized
properties and the qualified path; runtime instances detach after one precise error.

- [ ] **Step 6: Make tombstones unsavable and restorable**

A tombstoned direct export returns `is_built_in() == false`; Foundry and scene savers reject the invalid handle if it
reaches them. Placeholder replacement and tombstoning publish in one commit, so a scene save records a recoverable
missing external script, never an embedded subresource or empty external path. Restoration reuses the tombstoned
handle, adopts state, and calls `set_path(qualified, true)` after asserting canonical exclusivity.

- [ ] **Step 7: Publish under one lock and generation**

Commit live-state adoption, export table, qualified lookup table, tombstones, and conformance replacement under one
language-level lock with a documented lock order. `ScriptServer`, editor filesystem, autoload index, and LSP consume
separate whole-file generation-tagged snapshots and may lag, but runtime lookup verifies the active bundle generation.

- [ ] **Step 8: Add failure, cache, placeholder, and restore coverage**

Tests assert last-good execution after parser/analyzer/compiler failure, held `Ref` currency, stale qualified loads
failing after removal, no embedded save during tombstone state, same-handle recovery, runtime detachment, provider
move rewriting live/tombstone paths, and no mixed conformance/export generation.

- [ ] **Step 9: Run focused and strict verification**

```sh
python3 scripts/agent_build.py --backend ninja --test \
  --case "*NamespaceModule*Reload*" --case "*TraitSignalReload*"
python3 scripts/agent_build.py
```

- [ ] **Step 10: Commit**

```sh
git add modules/foundry_script/fs_compiler.* modules/foundry_script/foundry_script.* \
  modules/foundry_script/fs_cache.* modules/foundry_script/fs_conformance_registry.* \
  modules/foundry_script/tests/test_namespace_module_reload.h \
  modules/foundry_script/tests/test_foundry_script.cpp tests/test_main.cpp
git commit -m "Reload namespace modules transactionally"
```

## Task N10: Preserve qualified export references through scenes and dependencies

**Issue boundary:** Text/binary scene records, UID/remap reattachment, dependency-string parsing, dependency repair,
and file inclusion. Attachment eligibility and project settings are separate children.

**Labels:** `enhancement`, `priority:P2-normal`, `area:engine-runtime`, `area:foundry-script`, `area:editor`,
`area:test-infra`

**Files:**

- Modify: `core/io/resource_path.h`
- Modify: `core/io/resource_path.cpp`
- Modify: `scene/resources/resource_format_text.cpp`
- Modify: `core/io/resource_format_binary.cpp`
- Modify: `editor/file_system/dependency_editor.cpp`
- Modify: `editor/file_system/editor_file_system.cpp`
- Modify: `editor/export/editor_export_platform.cpp`
- Create: `tests/core/io/test_resource_dependency_reference.h`
- Create: `tests/scene/test_namespace_module_scene_persistence.h`
- Create: `modules/foundry_script/tests/scripts/persistence/namespace_module/module.fs`
- Create: `modules/foundry_script/tests/scripts/persistence/namespace_module/exported_scene.tscn`
- Modify: `tests/test_main.cpp`

- [ ] **Step 1: Add failing dependency split and scene round-trip tests**

Pin the wire format:

```cpp
TEST_CASE("[Core][ResourcePath] Dependency metadata preserves a qualified fallback") {
	ResourceDependencyReference dep = ResourceDependencyReference::parse(
			"uid://abc::Script::res://models/module.fs::my_project.models.Entity");
	CHECK_EQ(dep.uid_text, "uid://abc");
	CHECK_EQ(dep.type, "Script");
	CHECK_EQ(dep.fallback_path,
			"res://models/module.fs::my_project.models.Entity");
}
```

Scene tests save/reopen/resave text and binary scenes referencing two exports and assert UID moves and `.remap`
preserve selectors.

- [ ] **Step 2: Run focused tests and verify selector truncation**

```sh
python3 scripts/agent_build.py --backend ninja --test \
  --case "*Dependency metadata preserves*" --case "*NamespaceModule*Scene*"
```

- [ ] **Step 3: Add the shared dependency-reference parser**

Implement `ResourceDependencyReference::parse()` and `join()` in `resource_path.*`. Split only the first two `::`
metadata separators and preserve the entire remaining fallback. A path without UID/type remains accepted. Use this
helper in `DependencyEditor::_get_resolved_dep_path()`, `_get_stored_dep_path()`, and editor filesystem script-icon
lookup. Keep provider-only behavior in `EditorFileSystemDirectory::get_file_deps()` and reimport inclusion paths.

- [ ] **Step 4: Reattach selectors after text-scene UID/remap replacement**

In the external-resource reader, split the stored path before UID work. On UID hit replace only `provider`; on miss,
compare the provider UID; resolve relative providers; apply the `remaps` table to provider only; then reattach selector
before `_load_start()`. Reject raw-module fallback if selector validation fails.

- [ ] **Step 5: Apply the same rule to binary external resources**

Preserve the existing binary record fields and format version. UID and path strings continue to use the provider UID
plus qualified fallback. Replace/localize/remap the provider only, then store/load the joined path. Previous binary
scene versions still load; older engines explicitly reject new qualified module references.

- [ ] **Step 6: Preserve writer and file-inclusion contracts**

Text/binary dependency writers already append fallback last; retain them and add assertions. Export/PCK collection
includes each provider once even when several selectors reference it. Rename dependencies maps provider paths while
retaining selectors. Dependency editor errors display provider and canonical identity separately and offer compatible
public repair candidates.

- [ ] **Step 7: Add full persistence regression coverage**

Cover text byte stability, binary round-trip, previous binary format, UID provider move, `.remap`, relative provider,
missing/private/wrong-kind selector, dependency display/rename round-trip, PCK inclusion, and two exports loading one
bundle.

- [ ] **Step 8: Run focused and strict verification**

```sh
python3 scripts/agent_build.py --backend ninja --test \
  --case "*NamespaceModule*Scene*" --case "*ResourceFormatText*" --case "*ResourceFormatBinary*"
python3 scripts/agent_build.py
```

- [ ] **Step 9: Commit**

```sh
git add core/io/resource_path.* scene/resources/resource_format_text.cpp \
  core/io/resource_format_binary.cpp editor/file_system/dependency_editor.cpp \
  editor/file_system/editor_file_system.cpp editor/export/editor_export_platform.cpp \
  tests/core/io/test_resource_dependency_reference.h \
  tests/scene/test_namespace_module_scene_persistence.h \
  modules/foundry_script/tests/scripts/persistence/namespace_module tests/test_main.cpp
git commit -m "Persist qualified namespace module exports"
```

## Task N11: Enforce module attachment and script-facing ResourceLoader boundaries

**Issue boundary:** Generic script attachment capability, raw-module rejection in source/path APIs, and the bound
`ResourceLoader` acquisition seam. Editor picker population is N13.

**Labels:** `enhancement`, `priority:P2-normal`, `area:engine-runtime`, `area:foundry-script`, `area:editor`,
`area:test-infra`

**Files:**

- Modify: `core/object/script_language.h`
- Modify: `core/object/object.cpp`
- Modify: `core/core_bind.h`
- Modify: `core/core_bind.cpp`
- Modify: `modules/foundry_script/foundry_script.h`
- Modify: `modules/foundry_script/foundry_script.cpp`
- Modify: `modules/foundry_script/fs_analyzer.cpp`
- Modify: `scene/resources/resource_format_text.cpp`
- Modify: `core/io/resource_format_binary.cpp`
- Create: `modules/foundry_script/tests/test_namespace_module_attachment.h`
- Create: `modules/foundry_script/tests/scripts/analyzer/errors/namespace_module_raw_load.fs`
- Create: `modules/foundry_script/tests/scripts/analyzer/errors/namespace_module_raw_load.out`
- Modify: `tests/test_main.cpp`

- [ ] **Step 1: Add failing capability and public-loader tests**

Assert the generic default is attachable, module root is not, exported concrete class is, and `Object::set_script()`
rejects the module before placeholder creation. Through the bound singleton, test synchronous load, threaded
request/status/get, `has_cached`, and `get_cached_ref` against an internally cached module root.

- [ ] **Step 2: Run focused tests and verify the holder escapes**

```sh
python3 scripts/agent_build.py --backend ninja --test --case "*NamespaceModule*Attachment*"
```

- [ ] **Step 3: Add a generic attachment capability**

Add `virtual bool can_attach_to_object() const { return true; }` to `Script`. Foundry module roots return false;
addressable class handles return true subject to existing abstract/base compatibility. `Object::set_script()` checks
the capability before abstract and `can_instantiate()`/placeholder branches, logs the module-specific error, and
leaves the object unscripted.

- [ ] **Step 4: Reject raw modules before scene property assignment**

Text and binary external-script validation reject an unqualified module provider root before `Object::set_script()`.
Hand-edited/future resources that bypass this path still hit the defense-in-depth check and produce an unscripted node
plus the same precise error.

- [ ] **Step 5: Reject raw module source operations**

Analyzer errors cover literal `preload(provider)`, literal `load(provider)`, `extends "provider.fs"`, and
`extends "provider.fs".Entity`. Dynamic Foundry `load(path)` checks the loaded root and returns `null` with the
import-instead diagnostic. The full qualified export path remains legal for load/preload/path inheritance.

- [ ] **Step 6: Close the bound `ResourceLoader` acquisition surface**

In the separate wrapper class in `core/core_bind.cpp`, detect an unqualified Foundry module provider before returning
a resource. Reject `load`, threaded request/status/get, `has_cached`, and `get_cached_ref`; clear/refuse user tokens so
threaded get cannot retrieve a request started elsewhere. Core C++ `::ResourceLoader`, `FSCache`, editor filesystem,
and script editor calls remain untouched. Metadata-only methods may describe the source without exposing the holder.

- [ ] **Step 7: Add ordinary-script and qualified-export regressions**

Prove ordinary abstract placeholder behavior is unchanged, non-Foundry resource loads are unchanged, qualified export
loading works through the bound singleton, and a module export used as a custom `ResourceFormatLoader` or
`EditorPlugin`-style global loads or reports a precise base incompatibility.

- [ ] **Step 8: Run focused and strict verification**

```sh
./bin/foundry.* --headless test generate-fixtures modules/foundry_script/tests/scripts/analyzer/errors
python3 scripts/agent_build.py --backend ninja --test --case "*NamespaceModule*Attachment*"
python3 scripts/agent_build.py
```

- [ ] **Step 9: Commit**

```sh
git add core/object/script_language.h core/object/object.cpp core/core_bind.* \
  modules/foundry_script/foundry_script.* modules/foundry_script/fs_analyzer.cpp \
  scene/resources/resource_format_text.cpp core/io/resource_format_binary.cpp \
  modules/foundry_script/tests/test_namespace_module_attachment.h \
  modules/foundry_script/tests/scripts/analyzer/errors tests/test_main.cpp
git commit -m "Reject raw namespace module resources"
```

## Task N12: Persist exported autoload and main-loop selections

**Issue boundary:** Structured project-setting values, migration-on-write rules, startup, editor settings, and export
collection for autoload/main-loop script selections.

**Labels:** `enhancement`, `priority:P2-normal`, `area:engine-runtime`, `area:foundry-script`, `area:editor`,
`area:test-infra`

**Files:**

- Modify: `core/config/project_settings.h`
- Modify: `core/config/project_settings.cpp`
- Modify: `main/main.cpp`
- Modify: `editor/settings/editor_autoload_settings.cpp`
- Modify: `editor/docks/filesystem_dock.cpp`
- Modify: `editor/export/editor_export_platform.cpp`
- Modify: `modules/foundry_script/fs_autoload_index.h`
- Modify: `modules/foundry_script/fs_autoload_index.cpp`
- Modify: `modules/foundry_script/tests/test_autoload_index.h`
- Modify: `tests/editor/test_editor_autoload_settings.h`
- Modify: `tests/editor/test_editor_export_platform_autoload.h`
- Create: `modules/foundry_script/tests/test_namespace_module_project_settings.h`
- Modify: `tests/test_main.cpp`

- [ ] **Step 1: Add failing structured/legacy round-trip and startup tests**

Pin the declaration-bearing autoload representation:

```cpp
Dictionary value;
value["uid"] = "uid://provider";
value["path"] = "res://models/module.fs::my_project.models.Autoload";
value["singleton"] = true;
ProjectSettings::AutoloadInfo info;
REQUIRE(ProjectSettings::parse_autoload_value(value, info));
CHECK_EQ(info.path, value["path"]);
CHECK(info.is_singleton);
```

Also save an untouched legacy `*res://legacy.fs::uid://...` value and assert the stored Variant remains the identical
String. Launch a project whose autoload and main loop are module exports.

- [ ] **Step 2: Run focused tests and reproduce first-`::` truncation**

```sh
python3 scripts/agent_build.py --backend ninja --test \
  --case "*NamespaceModule*ProjectSettings*" --case "*AutoloadSettings*"
```

- [ ] **Step 3: Generalize autoload parse/stringify to Variant**

Change the helpers to accept/return `Variant` while retaining the legacy String branch. The Dictionary branch requires
`path`, accepts `uid`, and carries `singleton`; it resolves UID on the provider portion and reattaches the selector.
Malformed dictionaries fail with an actionable setting name/path diagnostic and never fall back to the raw module.

- [ ] **Step 4: Enforce conversion only for edited declaration selections**

Untouched legacy values remain their original String Variant. New/edited standalone selections keep legacy strings.
Only choosing or editing a declaration-bearing export writes the Dictionary. Rename/move code updates provider/UID or
selector within that Dictionary without rewriting unrelated settings.

- [ ] **Step 5: Add dual-form main-loop startup**

Teach `main/main.cpp` to accept the existing native/global/path String or a declaration Dictionary. Resolve the
qualified export, require concrete `MainLoop`/`SceneTree` compatibility, and instantiate that handle. Editor writes a
Dictionary only for an edited export selection; existing class/path strings remain strings.

- [ ] **Step 6: Generalize autoload indexing and export collection**

Consume the declaration list rather than the one-root query. Offer concrete compatible exports individually, reject
raw/abstract/incompatible declarations, resolve project-vs-script conflicts by existing rules, and force/include only
the provider file in exported PCKs.

- [ ] **Step 7: Add migration, boot, rename, and export coverage**

Tests cover exported Node autoload boot, exported MainLoop boot, singleton false/true, legacy preservation, edit-time
conversion, provider UID move, export rename repair, malformed/missing/private selector, incompatible base, editor
view entries, feature overrides, and exported provider inclusion.

- [ ] **Step 8: Run focused and strict verification**

```sh
python3 scripts/agent_build.py --backend ninja --test \
  --case "*NamespaceModule*ProjectSettings*" --case "*Autoload*" --case "*GlobalClassStartup*"
python3 scripts/agent_build.py
```

- [ ] **Step 9: Commit**

```sh
git add core/config/project_settings.* main/main.cpp editor/settings/editor_autoload_settings.cpp \
  editor/docks/filesystem_dock.cpp editor/export/editor_export_platform.cpp \
  modules/foundry_script/fs_autoload_index.* modules/foundry_script/tests/test_autoload_index.h \
  modules/foundry_script/tests/test_namespace_module_project_settings.h \
  tests/editor/test_editor_autoload_settings.h tests/editor/test_editor_export_platform_autoload.h \
  tests/test_main.cpp
git commit -m "Persist namespace module project selections"
```

## Task N13: Integrate module declarations with editor filesystem and pickers

**Issue boundary:** Editor filesystem presentation, script navigation, compatible declaration pickers, and semantic
editor automation. Parser, loader, project-setting persistence, and LSP behavior are owned by earlier/later children.

**Labels:** `enhancement`, `priority:P2-normal`, `area:editor`, `area:foundry-script`, `area:test-infra`

**Files:**

- Modify: `editor/file_system/editor_file_system.h`
- Modify: `editor/file_system/editor_file_system.cpp`
- Modify: `editor/docks/filesystem_dock.cpp`
- Modify: `editor/docks/scene_tree_dock.cpp`
- Modify: `editor/gui/create_dialog.cpp`
- Modify: `editor/inspector/editor_resource_picker.cpp`
- Modify: `editor/script/script_editor_plugin.cpp`
- Modify: `editor/script/script_editor_view.cpp`
- Modify: `modules/foundry_script/fs_editor.cpp`
- Create: `editor/icons/FoundryModule.svg`
- Create: `tests/editor/file_system/test_namespace_module_editor_file_system.h`
- Create: `tests/editor/test_namespace_module_picker.h`
- Modify: `tests/editor/test_editor_automation_workflow.h`
- Modify: `tests/test_main.cpp`

- [ ] **Step 1: Add failing editor-model and picker tests**

Stage a scratch project with one module containing two exported classes, an enum, an alias, and a private class.
Assert the filesystem model reports one provider container plus the four public declarations in source order; each
entry carries canonical name, kind, declaration range, icon, and qualified resource path. Assert a `Node` script
picker offers only compatible concrete exported classes and never the module root, abstract class, alias, or enum.

- [ ] **Step 2: Run the focused tests and verify the one-file/one-class assumptions fail**

```sh
python3 scripts/agent_build.py --backend ninja --test \
  --case "*NamespaceModule*EditorFileSystem*" --case "*NamespaceModule*Picker*"
```

- [ ] **Step 3: Present modules as containers with declaration children**

Consume the declaration vector introduced in N2. Keep one filesystem file row with a distinct `FoundryModule` icon,
non-attachable metadata, and an expandable ordered declaration list. Public classes, traits, enums, tuples, and type
aliases receive kind-specific labels; private declarations do not leave the source editor. Do not synthesize files or
duplicate provider rows. A module's primary file type remains Foundry Script so normal rescan/import behavior applies.

- [ ] **Step 4: Navigate declarations by provider and source range**

Add a single editor helper accepting `ScriptGlobalDeclaration`. Opening a declaration loads the provider once,
selects the matching script editor, and moves the caret to the indexed range. Filesystem children, global-type
search, create-dialog results, dependency repair, and inspector links call this helper instead of trying to open the
qualified selector as a physical file. Stale ranges trigger a provider reparse and canonical-name lookup.

- [ ] **Step 5: Generalize type and script pickers to declaration candidates**

Replace file-root assumptions with declaration records. Filter on public visibility, runtime kind, abstractness, and
base compatibility before display. Selecting an exported class writes/returns its qualified path; selecting an
autoload or main-loop candidate delegates to the structured-setting writer from N12. Drag/drop and Scene dock
attachment reject the module container before mutation and show an actionable declaration-selection message.

- [ ] **Step 6: Preserve ordinary-script workflows and editor state**

Standalone classes remain single entries with their existing icons and paths. Reopen, recent-files, favorites,
filesystem move/rename, script history, breakpoints, and external-change refresh store the provider path plus optional
canonical declaration identity. Removing/renaming an export keeps the provider open, marks the declaration reference
unresolved, and offers public compatible repairs without silently choosing another export.

- [ ] **Step 7: Add semantic editor automation coverage**

Extend the checked-in editor automation workflow to open the scratch project, expand a module row, open the second
export at its declaration, attach the compatible export to a node, save/reopen the scene, and verify the selected
qualified script. Then attempt to attach the module row and assert the UI rejects it without changing the node. Use
semantic selectors and `foundry_wait_for`; add missing automation roles/metadata in `editor/automation/` if the real
controls cannot be selected stably.

- [ ] **Step 8: Run focused, GUI, and strict verification**

```sh
python3 scripts/agent_build.py --backend ninja --test \
  --case "*NamespaceModule*EditorFileSystem*" --case "*NamespaceModule*Picker*" \
  --case "*EditorAutomation*NamespaceModule*"
python3 scripts/agent_build.py
```

On Linux, run the automation case with `DISPLAY=:1`; on macOS, run it in the active GUI test environment. Expected:
the model, picker, and semantic editor workflow pass, and the native strict build exits zero.

- [ ] **Step 9: Commit**

```sh
git add editor/file_system/editor_file_system.* editor/docks/filesystem_dock.cpp \
  editor/docks/scene_tree_dock.cpp editor/gui/create_dialog.cpp \
  editor/inspector/editor_resource_picker.cpp editor/script/script_editor_plugin.cpp \
  editor/script/script_editor_view.cpp editor/icons/FoundryModule.svg \
  modules/foundry_script/fs_editor.cpp tests/editor/file_system/test_namespace_module_editor_file_system.h \
  tests/editor/test_namespace_module_picker.h tests/editor/test_editor_automation_workflow.h tests/test_main.cpp
git commit -m "Integrate namespace modules with the editor"
```

## Task N14: Add LSP, refactoring, and documentation support for module exports

**Issue boundary:** Language-server symbols/navigation/completion/diagnostics, canonical-identity refactoring, and
generated documentation. Runtime loading, editor pickers, and bytecode behavior are outside this child.

**Labels:** `enhancement`, `priority:P2-normal`, `area:lsp-tooling`, `area:foundry-script`, `area:test-infra`,
`area:docs`

**Files:**

- Modify: `modules/foundry_script/language_server/fs_workspace.h`
- Modify: `modules/foundry_script/language_server/fs_workspace.cpp`
- Modify: `modules/foundry_script/language_server/fs_text_document.h`
- Modify: `modules/foundry_script/language_server/fs_text_document.cpp`
- Modify: `modules/foundry_script/language_server/fs_semantic_tokens.cpp`
- Modify: `modules/foundry_script/editor/fs_refactoring.h`
- Modify: `modules/foundry_script/editor/fs_refactoring.cpp`
- Modify: `modules/foundry_script/editor/fs_refactoring_names.cpp`
- Modify: `modules/foundry_script/editor/fs_docgen.cpp`
- Modify: `modules/foundry_script/fs_editor.cpp`
- Modify: `modules/foundry_script/tests/test_completion.h`
- Modify: `modules/foundry_script/tests/test_lsp.h`
- Modify: `modules/foundry_script/tests/test_refactor.h`
- Modify: `modules/foundry_script/tests/test_foundry_script_type.h`
- Create: `modules/foundry_script/tests/scripts/completion/namespace_module_exports/provider.fs`
- Create: `modules/foundry_script/tests/scripts/completion/namespace_module_exports/selective_consumer.fs`
- Create: `modules/foundry_script/tests/scripts/lsp/namespace_module_exports/provider.fs`
- Create: `modules/foundry_script/tests/scripts/lsp/namespace_module_exports/consumer.fs`
- Create: `modules/foundry_script/tests/scripts/refactor/namespace_module_export_provider.fs`
- Create: `modules/foundry_script/tests/scripts/refactor/namespace_module_export_consumer.fs`
- Modify: `tests/test_main.cpp`

- [ ] **Step 1: Add failing completion, symbol, navigation, rename, and docgen tests**

Tests pin module document symbols, workspace symbols for every public declaration, selective-import completion before
and after `as`, fully qualified completion, hover/signature text, definition to provider range, references across
selective/wildcard/qualified uses, rename edits across providers, semantic tokens for contextual keywords, and one
documentation page per public canonical identity. Private declarations appear only in their provider document.

- [ ] **Step 2: Run the focused tooling suites and verify root-script assumptions fail**

```sh
python3 scripts/agent_build.py --backend ninja --test \
  --suite "*[Modules][FoundryScript][Completion]*" \
  --case "*NamespaceModule*LSP*" --case "*NamespaceModule*Refactor*" --case "*NamespaceModule*Docgen*"
```

- [ ] **Step 3: Use declaration identity as the workspace symbol key**

Index workspace symbols by `(canonical_name, provider_path, kind)` and retain source range plus provider generation.
Publish public module declarations individually while the module root remains a document container, not a symbol
that can be instantiated. Duplicate canonical identities remain separate diagnostic candidates until analysis
rejects them. Closed-file and open-document snapshots obey the last-complete generation rule from N2.

- [ ] **Step 4: Complete imports and declarations from the shared index**

Inside `import { ... } from namespace`, offer public declarations in that namespace with kind/detail/provider data;
after `as`, offer collision-free local identifiers. Qualified-chain completion and own/wildcard namespace completion
use the same resolver and precedence as N3. Never suggest unrelated names for a selective import. Classes/traits/
enums/tuples have value/type behavior matching the analyzer; aliases are marked type-only.

- [ ] **Step 5: Implement navigation, hover, references, and semantic tokens**

Resolve every imported, wildcard, fully qualified, inherited, conformance, payload, and expanded-alias occurrence to
the canonical declaration record. Definition opens the provider range; hover shows canonical identity, kind,
visibility, provider, and provider-context-expanded type where applicable. Tokenize contextual `module`, `export`,
and `from` only in their grammar positions, and classify import items by resolved declaration kind.

- [ ] **Step 6: Make rename a canonical, cross-provider transaction**

Rename a public declaration at its declaration or any reference. Update the declaration, selective import items,
aliases when they name the imported symbol, wildcard/own-namespace references, qualified chains, and structured
autoload/main-loop selectors. Refuse collisions and stale provider generations before emitting edits. Private rename
stays provider-local; provider file rename remains a separate filesystem operation. Preserve comments/strings unless
the existing refactor contract intentionally covers a structured setting.

- [ ] **Step 7: Generate documentation for every public declaration**

Emit a page keyed by canonical identity for public classes, traits, enums, tuples, and aliases. Use provider-context
resolution, include source links to the physical provider/range, render alias expansion without exposing private
names, and link cross-module types by canonical identity. The module holder itself has no public class page. Private
documentation comments remain available to provider-local hover but are absent from generated public docs.

- [ ] **Step 8: Add invalidation and negative tooling coverage**

Cover add/remove/rename/kind changes, parse failure retaining last-good symbols, successful recovery, duplicate
identity, private/missing import, wildcard ambiguity, alias cycle, public leakage diagnostic refresh, native namespace
selection, and unrelated namespace edits not refreshing consumers. Assert identical diagnostic ranges/messages in
editor and LSP paths.

- [ ] **Step 9: Regenerate fixtures and run focused and strict verification**

```sh
./bin/foundry.* --headless test generate-fixtures modules/foundry_script/tests/scripts
python3 scripts/agent_build.py --backend ninja --test \
  --suite "*[Modules][FoundryScript][Completion]*" \
  --case "*NamespaceModule*LSP*" --case "*NamespaceModule*Refactor*" --case "*NamespaceModule*Docgen*"
python3 scripts/agent_build.py
```

- [ ] **Step 10: Commit**

```sh
git add modules/foundry_script/language_server modules/foundry_script/editor/fs_refactoring* \
  modules/foundry_script/editor/fs_docgen.cpp modules/foundry_script/fs_editor.cpp \
  modules/foundry_script/tests/test_completion.h modules/foundry_script/tests/test_lsp.h \
  modules/foundry_script/tests/test_refactor.h modules/foundry_script/tests/test_foundry_script_type.h \
  modules/foundry_script/tests/scripts/completion/namespace_module_exports \
  modules/foundry_script/tests/scripts/lsp/namespace_module_exports \
  modules/foundry_script/tests/scripts/refactor/namespace_module_export_* tests/test_main.cpp
git commit -m "Add namespace module language tooling"
```

## Task N15: Complete namespace-module end-to-end validation and integration

**Issue boundary:** Cross-subsystem acceptance, user documentation, compatibility audit, and the final feature-branch
integration PR. Do not introduce new behavior here unless a failing acceptance test exposes a missing design contract;
route substantial subsystem work back to its owning child.

**Labels:** `enhancement`, `priority:P2-normal`, `area:foundry-script`, `area:engine-runtime`, `area:editor`,
`area:lsp-tooling`, `area:test-infra`, `area:docs`

**Files:**

- Create: `modules/foundry_script/tests/fixtures/namespace_modules/project.foundry`
- Create: `modules/foundry_script/tests/fixtures/namespace_modules/models/module.fs`
- Create: `modules/foundry_script/tests/fixtures/namespace_modules/main.fs`
- Create: `modules/foundry_script/tests/fixtures/namespace_modules/main.tscn`
- Create: `modules/foundry_script/tests/test_namespace_module_integration.h`
- Modify: `tests/editor/test_editor_automation_workflow.h`
- Modify: `modules/foundry_script/README.md`
- Modify: `modules/foundry_script/GRAMMAR.md`
- Modify: `tests/test_main.cpp`

- [ ] **Step 1: Add the complete scratch-project acceptance fixture**

The fixture uses one module with private helpers and public alias, class, trait, enum, and tuple declarations. Separate
consumers exercise selective/renamed/wildcard/own/fully qualified imports; a scene attaches two exported classes; an
autoload and main loop use structured selections; runtime code saves/loads text and binary resources; and the project
contains a renameable exported declaration referenced from source and settings.

- [ ] **Step 2: Add failing source and bytecode acceptance tests**

Run the project from source, export it with source stripped, then run the exported PCK. Assert identical observable
output, canonical FQNs, public behavior, alias erasure, enum/tuple values, trait dispatch, scene scripts, autoload,
main loop, and dependency lists. Assert the PCK contains the provider once, contains no private source name, and
rejects raw module acquisition/attachment with the specified diagnostic.

- [ ] **Step 3: Add reload and persistence acceptance tests**

While consumers and scene instances are live, change method bodies, add/remove/rename exports, introduce an analysis
failure, recover it, move the provider under the same UID, and apply a remap. Assert staged reload is all-or-nothing,
surviving handles retain identity, removed handles are tombstoned after placeholder detachment, saving during the
broken build cannot inline or write an empty external reference, and text/binary scenes reopen with selectors intact.

- [ ] **Step 4: Audit compatibility adapters and serialized formats**

Exercise an ordinary one-class language, `ScriptLanguageExtension`, standalone Foundry class/trait/enum/tuple files,
legacy import syntax, old global-class cache, old bytecode, old text/binary scenes, and untouched legacy project
settings. Document intentional version bumps and rejection diagnostics. Remove temporary dual stores, debug flags,
and migration-only branches that are not part of the approved compatibility contract.

- [ ] **Step 5: Run the real editor workflow and capture structured evidence**

Through the Foundry editor automation MCP, open the fixture, inspect the module/declaration tree, navigate to an
export, attach it to a node, configure an exported autoload, save/reopen/run the scene, edit the provider, wait for
reload, and verify diagnostics plus recovered runtime behavior. Use semantic selectors and event polling. If this
requires an automation surface improvement, add it with a behavioral editor test before using it.

- [ ] **Step 6: Update user and normative documentation**

Document module syntax, legal exports, private/public visibility, selective and wildcard imports, lookup precedence,
canonical names, qualified resource paths, alias erasure, attachment/loading limits, reload behavior, and settings
persistence in `modules/foundry_script/README.md`. Reconcile every relevant production and contextual keyword in
`GRAMMAR.md` with the implemented parser; do not test documentation prose.

- [ ] **Step 7: Run the complete native validation**

```sh
python3 scripts/agent_build.py --test
pre-commit run --all-files
```

On Linux, prefix the wrapper invocation with `DISPLAY=:1` so GUI-dependent acceptance does not skip. Monitor the
progress file printed by the wrapper. Completion requires a zero strict build, doctest `Status: SUCCESS!`, all GUI
acceptance cases executed rather than skipped, and clean pre-commit output. Cleanup leak noise is recorded separately
when the doctest summary succeeds, per repository guidance.

- [ ] **Step 8: Verify repository and public-surface hygiene**

```sh
git diff --check develop...HEAD
git grep -nE '\.split\("(void|bool|String|private fun|const val|def )' -- '*.py'
git grep -n "docs/superpowers" -- '*/test_*.py' '*/check_*.py'
git grep -n "hasattr(" -- '*/test_*.py'
git grep -l "assert" -- 'misc/checks/check_*.py'
```

Review the output against the existing baseline, confirm generated/test scratch files are absent from `git status`,
and verify every qualified path is created through `QualifiedResourcePath` rather than ad hoc string splitting.

- [ ] **Step 9: Open and merge the integration PR**

Commit the acceptance/docs delta:

```sh
git add modules/foundry_script/tests/fixtures/namespace_modules \
  modules/foundry_script/tests/test_namespace_module_integration.h \
  tests/editor/test_editor_automation_workflow.h modules/foundry_script/README.md \
  modules/foundry_script/GRAMMAR.md tests/test_main.cpp
git commit -m "Complete Foundry Script namespace modules"
```

Open the feature-branch PR to `develop`, link this epic and all child issues, attach the complete verification evidence,
and request the normal independent code review. Merge only after all native blockers are closed, all review threads
are resolved, required checks pass, and the integration acceptance above is green.

## Specification coverage matrix

| Design contract | Owning issue(s) | Required observable proof |
| --- | --- | --- |
| Module grammar, legal declaration set, contextual keywords, formatting | N1 | Parser/formatter fixtures |
| Many public declarations per provider and cache lifecycle | N2 | Index lifecycle and cache-version tests |
| Selective/wildcard/own/native imports and lookup precedence | N3 | Analyzer fixtures and generation invalidation |
| Private visibility, provider-context aliases, cycles, public leakage | N4 | Cross-provider analyzer tests |
| One bundle, stable handles, canonical runtime identity | N5, N6 | Runtime construction/value/trait tests |
| Bytecode, stripping, dependency closure, name protection | N7 | Source/PCK equivalence and bytecode compatibility |
| Qualified loading, UID/cache identity, provider sharing | N8 | Core loader and FSCache tests |
| Transactional reload and safe tombstones | N9 | Live-handle failure/recovery/save tests |
| Scene/dependency persistence and remap | N10 | Text/binary/UID/remap round-trips |
| Non-attachable/non-public module holder | N11 | Object, scene, analyzer, and bound-loader rejection tests |
| Autoload/main-loop structured selections and legacy preservation | N12 | Startup, settings round-trip, and PCK tests |
| Editor container/declaration presentation and compatible pickers | N13 | Editor model, picker, and automation tests |
| Completion, symbols, navigation, rename, diagnostics, docgen | N14 | LSP/refactor/docgen suites |
| Cross-subsystem source/export/reload/editor compatibility | N15 | Full fixture, build, suite, and editor automation |

## GitHub publication contract

- Create one parent issue titled `Epic: Foundry Script namespace modules` with `epic`, `enhancement`,
  `priority:P2-normal`, and every area label used by its children.
- Create exactly the 15 children in the native issue manifest. Each body copies its corresponding task as the
  normative implementation contract and links both the merged design and this plan on `develop`.
- Attach every child with GitHub's native sub-issue relationship. A checklist in the epic is a readable mirror, not a
  substitute for the native hierarchy.
- Add GitHub native blocked-by relationships exactly as listed in the manifest. Repeat the blockers in prose so the
  graph remains understandable in clients that do not render relationships.
- Add the epic and every child to the `Experiment` project with status `Todo`. Do not assign an owner or milestone.
- Epic closure requires all 15 native children closed, the N15 integration PR merged to `develop`, and every design
  row in the coverage matrix backed by the cited behavioral evidence.

## Plan self-review checklist

- [x] Every design section has exactly one primary implementation owner in the coverage matrix.
- [x] Every issue can begin from its listed native blockers without depending on an unlisted future API.
- [x] Every issue starts with an observable failing test and ends with focused plus native strict verification.
- [x] Paths and test commands match the current repository; newly created paths are explicitly marked `Create`.
- [x] Serialized/cache/bytecode changes name compatibility behavior and version/rebuild or rejection handling.
- [x] Runtime holder safety covers acquisition, attachment, save, reload, tombstone, and stripped export surfaces.
- [x] Editor and LSP behavior uses the declaration index and canonical identity instead of inventing parallel stores.
- [x] No task accepts source-text or documentation-text assertions as behavioral coverage.
