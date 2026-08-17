# Foundry Script Namespace Modules Design

**Date:** 2026-08-16
**Revised:** 2026-08-17
**Status:** Approved

## Problem

Foundry Script source files currently describe one implicit class, one globally named class or trait, or one
whole-file enum or tuple. Type aliases are lexical declarations. They are visible only within their declaring
file and nested bodies, so a project cannot publish a reusable alias through a namespace.

Adding only a whole-file `type_name` form would preserve the one-global-type-per-file model, but it would force
libraries to create one file per public alias. Promoting an alias declared in a `class_name` file directly into
the surrounding namespace would be worse: the source says the alias belongs to the class body while name
resolution would pretend that it does not.

Foundry Script needs a source-unit form that can contribute several named type declarations directly to a
namespace. It must preserve the existing namespace system, support selective imports, and distinguish the
non-attachable module container from exported classes that can be instantiated or attached independently.

## Goals

- Add an explicit module source-unit kind that has no implicit `RefCounted` class.
- Allow a module to export type aliases, classes, traits, enums, and tuples directly into a named namespace.
- Allow private type declarations in a module as implementation details.
- Add selective namespace imports with local renaming.
- Keep existing namespace-wide imports working and make module exports visible through them.
- Give every export one canonical, namespace-qualified identity independent of its provider file's path.
- Allow exported runtime classes to be loaded, instantiated, serialized, and attached independently of the
  non-attachable module container.
- Make module and export metadata available to the parser, project index, compiler, runtime, editor, and LSP.
- Preserve precise dependency invalidation, source diagnostics, bytecode export, and stripped-runtime behavior.

## Non-goals

- Do not replace namespaces with TypeScript-style file modules or package resolution.
- Do not add default exports, re-export declarations, module objects, or import side effects.
- Do not add module-level variables, constants, functions, signals, or executable initialization.
- Do not make a module itself a runtime type, node script, autoload, or main-loop script.
- Do not infer module semantics from a filename such as `module.fs`.
- Do not implement exports by generating synthetic source files.
- Do not change ordinary aliases in class-shaped files; they remain lexical and file-local.
- Do not allow `export` on nested declarations; only direct members of a Foundry Script module file can export.
- Do not admit custom annotation or retroactive conformance declarations in a Foundry Script module file.

## Source Syntax

### Module source units

A module uses the existing `namespace` declaration and an explicit `module` head marker:

```foundryscript
namespace my_project.models

import my_project.shared

module

type PrivateInteger = int | uint

export type EntityID = PrivateInteger

export enum EntityState:
	IDLE
	ACTIVE

export trait Serializable:
	func serialize() -> Dictionary

@tool
@icon("res://icons/entity.svg")
export class Entity extends RefCounted:
	var id: EntityID
```

The `module` marker occupies the same source-unit position as `class_name`, `trait_name`, `enum_name`, or
`tuple_name`. A module must declare a non-empty namespace. Imports remain between `namespace` and the source-unit
head, matching existing file ordering. The marker takes no colon and its members remain at file-scope indentation;
an `INDENT` immediately after `module` is diagnosed rather than interpreted as a module block.

`module` is a mutually exclusive alternative to the existing named source-unit heads in the top-level `program`
production. Head modifiers such as `final` and `abstract` cannot precede it because the container is not a class.
Leading script annotations before `namespace` are also invalid in a Foundry Script module file. Per-export
annotations and documentation comments must appear with the direct declaration they describe, after the `module`
marker, so there is no implicit "first exported class" target.

Conceptually, the grammar adds these productions:

```ebnf
module_head          = "module", NEWLINE, { module_member } ;
module_member        = { documentation_comment }, { class_annotation },
                       { module_declaration_modifier }, module_type_decl ;
module_declaration_modifier = "export" | declaration_modifier ;
module_type_decl     = type_alias_decl
                     | inner_class_decl
                     | trait_decl
                     | enum_decl
                     | tuple_decl ;
selective_import     = "import", "{", import_item,
                       { ",", import_item }, [ "," ], "}",
                       "from", dotted_name, NEWLINE ;
import_item          = identifier, [ "as", identifier ] ;
```

Here `documentation_comment` denotes the leading `##` documentation trivia already captured by the tokenizer, and
`class_annotation` reuses the existing annotation-usage production. The normative `GRAMMAR.md` update adds
`module_head` as a source-unit alternative in `program` rather than leaving it as a detached production.

Selective import braces allow line breaks and a trailing comma. `module`, `export`, and `from` are contextual in
their declaration positions so existing value-position identifiers retain their current meaning. The formatter
places `export` before other declaration modifiers, as in `export abstract class`; the parser accepts the normal
leading modifier run in any order. Documentation comments and declaration annotations remain before that modifier
run.

### Allowed module members

A direct module member may be a type alias, class, trait, enum, or tuple. Each kind accepts `export`. A declaration
without `export` is private to that module file.

Documentation comments and declaration annotations remain valid where the corresponding declaration kind already
accepts them. They describe the following private or exported declaration and do not create module exports of their
own. The script-level `@tool`, `@icon`, and `@static_unload` annotations gain a declaration target for direct
module classes and apply only to the class immediately following them. They remain invalid on the module container
itself and on non-class module declarations. An exported class therefore carries its own tool, icon, and static
unload metadata independently of every other class in the same file.

A module cannot directly declare variables, constants, functions, signals, properties, an `extends` or `uses`
clause, or executable statements. Exported classes and traits remain normal class bodies and may contain the
members that their existing grammar permits.

Custom `annotation` declarations and retroactive `extend Target uses Trait` conformances are also forbidden at
module scope. A module is deliberately a named-type declaration unit, not a general namespace declaration unit.
Those declarations keep using ordinary namespace files and their existing indexing and reach rules. The parser
reports their unsupported module context directly rather than treating them as malformed type declarations.

`export` is valid only on a direct module type declaration. It is an error on ordinary class members, nested
declarations, module-disallowed declarations, or declarations in a non-module source unit. Because `export` and
the existing `@export` annotation have different meanings, `export var` and `export func` receive a targeted
diagnostic: module exports are named types, while an inspector property must be declared with `@export` inside a
class.

### Import forms

Existing namespace-wide imports remain valid:

```foundryscript
import my_project.models

var entity := Entity.new()
var id: EntityID
```

The new selective form binds only the requested declarations and supports local renaming:

```foundryscript
import { EntityID, Entity as ModelEntity, EntityState as State } from my_project.models

var id: EntityID
var entity := ModelEntity.new()
var state := State.ACTIVE
```

There is no required `import type` form. The imported declaration determines its capabilities. A type alias is
valid only in type position; a class or enum retains the value-position operations it already supports. A future
type-restricted import would be additive and is not part of this design.

A selective import may name any public global declaration in the namespace, regardless of whether it comes from a
module export or a standalone `class_name`, `trait_name`, `enum_name`, or `tuple_name` file. Refactoring a public
declaration between standalone and module storage therefore does not break consumers. The `export` modifier governs
module-member visibility only; diagnostics and tooling use the broader term "public declaration" for lookup.

Selective imports also include namespaced native classes from the engine class registry:

```foundryscript
import { NativeWidget as Widget } from engine.ui
```

Native namespaces are static for the life of an engine build. They participate in the same local binding and
ambiguity rules, but the native registry itself does not carry a project namespace generation or trigger
script-index invalidation. If project scripts also contribute to that canonical namespace, their declarations and
retroactive conformances retain the ordinary script generation and reach behavior independently.

Wildcard and selective imports may interleave freely in the import section. The formatter preserves their source
order and normalizes spacing and line wrapping within each selective list; it does not reorder imports. A file may
selectively import from its own namespace, including with `as`, and that explicit binding has the precedence stated
below. Import cycles between module files are legal because imports perform no initialization. Only an actual type,
inheritance, trait, enum-payload, or tuple cycle reached through those imports is rejected.

Exports also support fully qualified access:

```foundryscript
var id: my_project.models.EntityID
var entity := my_project.models.Entity.new()
```

A file's own named namespace continues to act as implicitly imported. Code in another file in
`my_project.models` can therefore use `EntityID` and `Entity` by short name.

## Names and Visibility

Each export's canonical identity is its namespace plus its declared name. The provider filename and the module
container do not appear in the identity:

```text
my_project.models.EntityID
my_project.models.EntityState
my_project.models.Serializable
my_project.models.Entity
```

Private declarations are visible only inside their module file. Other module fragments in the same namespace do
not see them. Exported declarations in any file in the namespace follow the existing implicit-import rule.

Exported aliases remain transparent and parameter-less. `export type Pair[T] = ...` receives the existing
`A type alias cannot declare type parameters.` diagnostic. Two aliases in different namespaces may expand to the
same type without colliding: their declaration names are different public API names even though neither introduces
a nominal runtime identity.

A public alias may expand through private aliases when the complete expansion contains only public nominal types,
built-in types, and other legal structural members. For example, `EntityID` may expand through a private alias to
`int | uint` because no private identity survives.

An exported declaration must not expose a private nominal declaration through any externally visible surface.
This includes class bases, used traits, property and method signatures, enum payloads, tuple fields, generic bounds,
and an exported alias's fully expanded type. The analyzer reports the declaration path that leaks the private type.
The implementation uses one cycle-safe traversal helper over `FSParser::DataType`, visiting every nested slot,
including union members, generic arguments, collection elements, callable parameters and returns, enum payloads,
tuple fields, and nullable or type-handle wrappers. Individual declaration surfaces feed their resolved types into
that helper rather than implementing separate incomplete leak checks.

An exported identity collides with every other global type-bearing declaration of the same canonical name,
including exports from other modules and existing `class_name`, `trait_name`, `enum_name`, and `tuple_name`
declarations. Duplicate identities are errors at every conflicting declaration. A module may still have private
declarations with names used by declarations in other files because their identity is file-local.

## Type Lookup Precedence

The feature preserves the existing rule that compiler-provided and unqualified native type names cannot be
shadowed. For a bare type name, lookup proceeds in this order:

1. An enclosing function or type parameter, `Self`, or a function-local constant that denotes a type.
2. Compiler-provided types (`Variant`, `Number`, built-in types, `AsyncCallable`) and exposed unqualified native
   classes.
3. The innermost lexical type declaration, then successive enclosing declaration scopes: aliases, classes, traits,
   enums, tuples, and constants that denote types.
4. A selective-import local binding, including its `as` name.
5. A public declaration in the file's own named namespace, which is implicitly imported.
6. Public declarations from wildcard-imported namespaces. More than one matching namespace is ambiguous.
7. An unnamespaced flat global declaration and the existing native/global-enum fallbacks.

A declaration error prevents a selective import from binding a built-in, compiler-provided, or unqualified native
class name, whether written directly or introduced with `as`; for example, `import { Count as int } from metrics`
is invalid for the same reason as `type int = Count`. A lexical declaration and a selective import also may not
claim the same local name. These collision diagnostics keep the lookup order from silently making declarations
unreachable.

Selective bindings outrank both the implicit own-namespace import and wildcard imports because they are explicit
choices. The file's own namespace outranks wildcard imports. A fully qualified namespace chain resolves its public
declaration without an import, after the normal lexical and reserved-name checks on the chain's first component.

## Declaration Index

The project-wide declaration index must support many global declarations per source path. Every exported
declaration records at least:

```text
canonical_name
declaration_kind
source_path
declaration_name
source_range
base_identity, when applicable
abstract/tool/static-unload/trait metadata, when applicable
icon_path, when applicable
```

The source range is tooling metadata and is not the persistent identity.

The declaration name is sufficient to find a direct module member after reparsing; an implementation may also
retain a parser-local declaration ID while the parser tree is alive. `base_identity` is the resolved canonical
script identity or qualified ClassDB native identity, not the spelling of the source `extends` clause. Editor
consumers derive their display spelling from that identity instead of treating the legacy `extends` string as an
authoritative key.

Index extraction parses the whole file but does not run the analyzer, following the existing custom-annotation and
retroactive-conformance index pattern. A syntactically broken module contributes no replacement snapshot: the last
complete list remains published and the failed candidate generation carries the diagnostics. A successful reparse
atomically replaces every declaration previously indexed for that path, even if later analysis or compilation
fails. Explicit delete, rename, and move events remove or transfer the complete old list and must not leave stale
identities. Every snapshot carries its source generation so asynchronous consumers can discard older updates.

The engine's current global-script-class scan returns one declaration per path. It must be generalized to accept a
list of global declarations from a language. The base `ScriptLanguage` API provides a compatibility adapter that
wraps its existing single-declaration result in a list, so languages such as C# retain their current behavior even
when their module is enabled. Foundry Script overrides the list API for module files. Global registry removal by path
must remove every declaration at that path. `ScriptLanguageExtension` uses the same adapter; this change does not add
a new extension virtual. A future extension language that needs multiple declarations can add an extension binding
without changing Foundry Script or the compatibility behavior of existing extensions.

This work extends the existing `ScriptLanguage::update_global_declaration_index(search_path, target_path)` and
`clear_global_declaration_index_under(prefix)` lifecycle rather than creating a parallel module index. The export
records join the indexed custom annotations and retroactive conformances in the persisted declaration data. This is
required because an exported project never performs an editor filesystem rescan: its public export table must be
available from the generated global declaration cache and exported bytecode alone.

The persisted global declaration cache gains a format version and stores a declaration list per source path. In the
editor, an older or malformed version is discarded and rebuilt from source. An exported project cannot rebuild it,
so loading an incompatible cache fails with a clear "re-export the project with this engine version" error. The
editor filesystem's one-per-file `ScriptClassInfo` storage and its name/base accessors likewise become list-based;
ordinary files still contribute a one-element list. The Foundry Script autoload index consumes the same declaration
list instead of calling the legacy single-class query.

`ScriptServer` registers a runtime-bearing module export under its qualified resource path. For example,
`get_global_class_path("my_project.models.Entity")` returns
`res://models/module.fs::my_project.models.Entity`. Existing consumers that load a global class path therefore keep
working and receive the selected export. Consumers that need a file path explicitly split off the provider. Scan
collision checks compare the complete registered path for a canonical class name, while file-based removal matches
the provider portion and removes every matching export. In particular,
`ScriptServer::remove_global_class_by_path()` must not use its current exact-path comparison or return after the
first match. The versioned persisted global-class cache stores these qualified paths. Non-runtime declarations stay
in the generalized declaration index and resolve through its declaration-kind-aware resolver.

## Analysis and Dependencies

When a consumer resolves an export, it loads the provider parser through the normal dependency parser access and
asks the provider analyzer to resolve the declaration in its own lexical and import context. An alias is never
expanded using the consumer's scope.

The analyzer maintains a cross-parser resolution stack keyed by canonical exported identity. This detects alias,
inheritance, trait, enum-payload, and tuple cycles that cross module files without relying on one analyzer's local
stack. Cycle diagnostics list the canonical identities and provider locations in traversal order.

A successfully resolved export adds a direct dependency edge from the consumer to the provider path. Namespace
lookups also record the namespace index generation they observed. When a module adds, removes, renames, or changes
the kind of an export, cached parsers that import, implicitly import, selectively import, or perform qualified
lookups in that namespace are invalidated even if they had no prior edge to the changed declaration. Direct inverse
dependencies then provide transitive invalidation.

Selective imports are validated at the import item:

- The namespace must exist and have the requested public declaration.
- The source identity must not be duplicated.
- Two imported items must not introduce the same local binding.
- A local declaration must not collide with a selective-import binding.
- Imports from different namespaces that introduce the same local name require `as` disambiguation.
- Repeated identical import items receive a duplicate-import diagnostic rather than being silently coalesced.

Wildcard imports preserve their existing ambiguity rules and include all module exports. A selective import does
not make other declarations from its namespace visible.

### Namespace reach

Selective imports follow Swift's separation between scoped name visibility and module-wide semantic metadata. An
import such as `import { Entity } from my_project.models` binds only `Entity`, but it establishes reach to the named
namespace. The consumer therefore loads every retroactive conformance contributed by `my_project.models`, exactly
as if it had written `import my_project.models`. This keeps wildcard and selective imports behaviorally consistent
and prevents a statically accepted witness call from missing at runtime.

Namespace reach does not widen the selective import's lexical name set. In particular, unrelated type declarations
and custom annotation names from the namespace do not become visible. Custom annotations remain visible by short
name only through the existing wildcard or implicit-own-namespace rules. A selective import cannot name an
annotation because annotations occupy a separate symbol space.

## Compilation and Runtime Identity

A module is one compilation and export unit, not a collection of generated files. The compiler may use an internal
holder object to own compiled declaration data, but that holder is not a user-visible class and cannot be
constructed or attached.

The compiled module contains an export table keyed by canonical identity:

- Type aliases have analysis metadata only and are erased before runtime.
- Exported classes compile to independent `FoundryScript` class handles.
- Exported traits retain the metadata and compiled bodies required by existing trait application.
- Exported enums retain their nominal identity, values, payload constructors, and methods.
- Exported tuples retain their nominal identity and field metadata.

For a direct exported class or trait handle, the canonical identity and `FoundryScript::fully_qualified_name` are the
same dotted string, such as `my_project.models.Entity`. A lexical class nested inside an exported class continues the
existing runtime FQN convention as `my_project.models.Entity::Nested`. The script cache, conformance registry,
orphan-subclass adoption, bytecode resolver, and release name mangler all use this one FQN; the feature does not
introduce a second runtime class identity.

The module container has an empty `global_name` and an internal FQN equal to its canonicalized provider path. A
direct export is a non-prefix child: its parser and runtime direct-child key is its complete dotted canonical FQN,
not `<container FQN>::<short name>`. A private direct module class remains keyed by its declared short name and has
the internal FQN `<canonical provider path>::<short name>`. Lexical children below either kind keep the existing
`::Nested` convention. `FSParser::find_class()` / `has_class()` and `FoundryScript::find_class()` / `has_class()`
must therefore recognize explicit non-prefix direct children before traversing nested `::` segments; they must not
infer module ownership solely from `begins_with(container_fqn)`.

Provider-path mutation observes the same split. `FoundryScript::set_path()` and `set_path_cache()` assign the
provider path to the module root's resource path, compose a qualified resource path for each addressable direct
export, and leave private or lexical children without external resource paths. Every owned handle still records the
provider in its internal script-path field. `FSCache` keeps parser and bundle entries provider-keyed while its
runtime identity tables remain FQN-keyed. `FSCache::move_script()` moves the provider bundle and reparses under the
new provider key, then rewrites and rekeys every live export resource path and stored tombstone path without treating
those qualified resource keys as source-parser paths.

The module source path identifies the compilation bundle. Each runtime-bearing export additionally carries its
canonical declaration identity. Bytecode records both so stripped runtimes can load the bundle once and select the
requested export without the source front end.

Adding module bundles and export tables increments the Foundry Script bytecode format version. Older bytecode is
rejected before execution with a diagnostic that identifies the provider and asks the user to rebuild or re-export;
the loader never guesses at a root class or silently treats an old script as a module.

The generalized public declaration index is also the release name mangler's source of truth. It protects every
export's declared name, canonical FQN, and qualified resource-path spelling because those names survive in scenes,
project settings, bytecode, reflection, and cross-file references. Private module declarations remain mangling
candidates unless an existing reflection or `@keep_name` rule protects them; their rewritten internal identities
never appear in the public export table.

Source-level `preload("res://models/module.fs")` is an analyzer error at the call site. A literal
`load("res://models/module.fs")` receives the same diagnostic, while a dynamic `load(path)` that resolves to a raw
module reports the error at runtime and returns `null`. The internal module container remains loadable through
`FSCache` and editor tooling APIs, but the Foundry Script `load` and `preload` surfaces never expose it as a value.

The script-bound `ResourceLoader` wrapper in `core/core_bind.cpp` enforces the same boundary without changing core
C++ loading. Its resource-acquisition surface rejects an unqualified module provider with the import-instead
diagnostic: synchronous `load`, threaded request/status/get, `has_cached`, and `get_cached_ref` cannot return or
advertise the cached container. Qualified export paths remain loadable. Metadata operations may still describe the
provider source without exposing its holder. Core C++ callers, `FSCache`, the script editor, and `EditorFileSystem`
continue to use their internal loading paths, so ordinary editor opening and compilation of the module are unchanged.

Path inheritance from a provider root is also rejected. Both `extends "res://models/module.fs"` and
`extends "res://models/module.fs".Entity` explain that a Foundry Script module is not a base class and suggest
importing the exported class. The declaration-qualified resource path defined below is a real class resource, so
`load`, `preload`, and path-based `extends` may use that full path when path coupling is intentional. Namespace
imports and canonical type names remain the normal source-language mechanism.

## Reload and Partial Failure

A module bundle is the unit of compilation and reload. Editing any member reparses, analyzes, and compiles a complete
staged module revision before touching the active bundle. This requires separating Foundry Script compilation from
publication: the current compiler mutates live handles and registers conformances while compiling, which cannot
provide rollback. A staged revision owns temporary compiled class state and pending conformance entries until the
whole module succeeds.

If parsing, analysis, or staged compilation fails, the editor and a hot-reloading game keep the complete
last-known-good runtime bundle active. Live class handles, instances, qualified-path cache entries, and runtime
conformances remain unchanged while diagnostics refer to the failing source revision. Fixing the file attempts the
whole transaction again.

On a successful reload, the runtime reconciles exports by canonical FQN and commits the staged revision:

- A surviving class keeps the same `FoundryScript` resource object and qualified resource path. A new
  `FoundryScript::adopt_compiled_state_from(staged, live_handle_map)` operation transfers the staged payload into
  that handle, remapping internal script references to the reconciled live handles. Existing instances then use the
  normal state-transfer path for compatible properties and functions.
- New identities receive new handles and become resolvable only when the complete bundle is committed.
- A removed identity is removed from new lookup. Reconciliation copies its qualified resource path and live handle
  into a per-bundle tombstone table, clears the handle's `Resource::path` with `set_path("")` to evict its
  `ResourceCache` entry, and marks the handle invalid. In the editor, existing instances become missing-script
  placeholders that retain serializable property state and the qualified path independently of the handle. In a
  running game, existing instances are detached after a runtime error. Loading the removed qualified path therefore
  reaches the loader and fails instead of returning a stale cached handle.
- Renaming is remove-plus-add unless the rename refactor updates declaration references as part of the edit.

The reconciliation extends the mechanisms already used by inner classes: `make_scripts(..., keep_state)` reuses
surviving subclass objects, and the orphan-subclass table re-adopts a still-live handle by FQN and `ObjectID` when its
owner is reconstructed. Module exports use canonical FQNs in those mechanisms. A held `Ref<Script>` therefore stays
the same object and observes the new compiled state after a successful reload.

If a tombstoned identity returns, reconciliation reuses its live handle, adopts the new compiled state, and restores
the qualified path with `set_path(path, true)`. Canonical-identity uniqueness guarantees no legitimate active handle
can own that path; takeover also prevents an incidental stale claimant from blocking recovery. Tombstoning and
instance detachment or placeholder replacement occur within the same runtime commit, so no scene-save-visible object
retains the tombstoned script. A tombstoned direct-export handle explicitly continues to report
`is_built_in() == false`; Foundry and scene savers reject such an invalid handle if it somehow reaches them, rather
than writing an empty external path or inlining it as an embedded subresource. Restoring the export reconnects
placeholders and held references to the same handle.

The atomicity guarantee covers the active compiled bundle, every per-export compiled-state transfer, the bundle's
qualified-path lookup table, and replacement of its runtime conformance registrations. These publish under one
language-level commit with a fixed lock order. The runtime never observes a mixture such as three exports from the
new revision and two from the old one.

`ScriptServer` globals, the editor filesystem declaration list, the autoload index, and LSP document state are not
part of that runtime lock. Each consumes a generation-tagged, whole-file declaration snapshot and replaces all
entries for the provider atomically on its own thread; it may briefly lag the runtime commit but never receives a
partial list. Runtime declaration resolution verifies the active bundle generation instead of assuming an editor
index entry proves that an export compiled. Release exports with hot reload disabled load one compiled bundle and do
not create staging revisions.

## Attachment and Scene Persistence

Module capability metadata is available at the parser, declaration-index, compiled-resource, and editor descriptor
layers. The root source unit reports `MODULE`, no base class, `can_instantiate = false`, and
`can_attach_to_object = false`.

`can_instantiate` alone is not an attachment guard because the editor creates placeholders for some ordinary
non-instantiable scripts. The generic `Script` interface gains an attachment-capability query whose default is
`true`, preserving existing behavior for other script languages. `Object::set_script` and editor attachment flows
consult it before the existing abstract-script and editor-placeholder branches. A Foundry Script module overrides it
to return false, produces no placeholder, and reports a module-specific error when assignment is attempted.
`Object::set_script()` remains a `void` defense-in-depth check: text and binary scene loaders reject an unqualified
module reference during provider/export validation before property assignment. A hand-edited or future resource that
bypasses that validation logs the module-specific error and leaves the object unscripted.

An exported class has its own metadata: canonical identity, provider module UID and path, base type, abstract state,
tool state, icon, and attachment compatibility. A concrete exported `Node` subclass may be attached to a compatible
node. An abstract class, trait, non-`Node` class, enum, tuple, alias, or module cannot be offered as an attachable
script.

### Qualified resource paths

A direct exported class is an externally addressable `Script` resource. Its path combines the provider path and the
canonical export identity:

```text
provider path:     res://models/module.fs
canonical/FQN:     my_project.models.Entity
resource path:     res://models/module.fs::my_project.models.Entity
```

`FoundryScript::get_script_path()` returns the provider path for the module container and every declaration it owns.
The container's `Resource::get_path()` is the provider path. A direct exported class's `Resource::get_path()` is the
qualified resource path above. Private module classes and lexical classes nested inside an export do not gain
external resource paths merely because they have runtime FQNs. Direct exports set their internal provider `path` and
`path_valid = true` before assigning the qualified `Resource::path`; the current fallback from `get_script_path()`
to `get_path()` is never used for them. `FoundryScript::load_source_code()` opens `get_script_path()`, not the
export's qualified resource path.

The qualified string is the existing `ResourceCache` and in-flight `ResourceLoader` key. Two exports in one module
therefore have distinct cache entries without changing either cache's key type, while both handles point back to one
provider bundle. Loading the same qualified path twice returns the same handle. The module container remains cached
under the unqualified provider path for internal compiler and editor use.

The current `Resource::is_built_in()` path heuristic treats every `::` path as an embedded subresource. It becomes a
virtual query with the same default behavior; a direct module export overrides it to report external, while ordinary
built-in resources and private inner scripts retain existing behavior. `FoundryScript::set_path()` likewise
distinguishes the module root, direct exported handles, and non-addressable nested handles instead of propagating one
cache path to every subclass. This virtualizes a currently forced-inline, ClassDB-bound core method; the cost estimate
includes the resulting small inlining cost across its existing call sites. `String::is_resource_file()` keeps its
current meaning of standalone filesystem resource, so it correctly returns false for a qualified export. Script
editor and workspace-tab call sites use `get_script_path()` or the provider portion when they need to open the source
file rather than changing that string predicate.

`ResourceFormatLoaderFoundryScript` recognizes a qualified export path by splitting its first `::`, validating the
provider as a Foundry Script module, and treating the entire suffix as a canonical direct-export identity. It loads
the provider bundle through `FSCache`, selects that exact export, verifies its kind and FQN, and returns its stable
handle. It never searches another provider or falls back to a same-named declaration.

The default `ResourceFormatLoader::recognize_path()` cannot dispatch a qualified path because the selector, which
may itself contain dots, replaces `.fs` as the apparent suffix. `ResourceFormatLoaderFoundryScript` therefore
overrides `recognize_path`, `exists`, `get_resource_type`, `get_resource_uid`, `get_dependencies`,
`rename_dependencies`, and `get_classes_used` in addition to `load`. Every override uses one shared qualified-path
split, performs filesystem and UID work on the provider, and retains the complete selector for selection or output.
A bogus selector fails as a missing export and never degrades to a successful provider-root result.

Qualified-path rewriting is shared core behavior rather than loader-local behavior. A common provider/selector
splitter is used by `ResourceLoader::_validate_local_path()` to localize only the provider and by
`ResourceLoader::_path_remap()` to remap only the provider before reattaching the selector. The Foundry loader still
owns final provider-kind, selector-visibility, declaration-kind, and canonical-FQN validation.

### Scene, project-setting, and dependency encoding

Text scenes store the qualified path in the existing external-resource record and use the provider's UID:

```text
[ext_resource type="Script" uid="uid://c..." path="res://m.fs::models.Entity" id="1_entity"]
```

The saver obtains the path from the exported handle, classifies it as external through the capability above, and
writes it without a new `declaration` attribute. UID lookup strips the selector and queries the provider. The text
scene loader's external-resource UID replacement and remap branches in
`scene/resources/resource_format_text.cpp`, and their binary equivalents in
`core/io/resource_format_binary.cpp`, use the shared qualified-path splitter. A successful UID lookup replaces only
the provider, a `.remap` lookup keys on and replaces only the provider, and both reattach the unchanged selector.
These scene-loader changes are core persistence work; they are not delegated to the Foundry format loader.

The binary external-resource table already stores a UID and path string, so it stores the same qualified path with
no new field and no binary scene-format version bump. The new engine continues to read previous `.scn` versions.
An older engine does not understand Foundry Script modules or their qualified export paths and rejects such a new
resource explicitly; it must not discard the suffix and load the raw module.

Dependency strings remain in the existing `uid::type::fallback-path` representation. For a module export they are,
conceptually:

```text
uid://c...::Script::res://models/module.fs::my_project.models.Entity
```

Dependency readers gain one shared
`split_dependency_reference(dep, &uid, &type, &fallback_path)` helper that splits only the first two metadata
separators and preserves the rest as the fallback path. `DependencyEditor::_get_resolved_dep_path()` and
`_get_stored_dep_path()` in `editor/file_system/dependency_editor.cpp`, plus the script-dependency icon lookup in
`editor/file_system/editor_file_system.cpp`, use it instead of `get_slice("::", 2)`. The existing editor-filesystem
`EditorFileSystemDirectory::get_file_deps()` and reimport file-inclusion sites that intentionally split at the first
`::` continue to retain only the provider. The text and binary dependency writers already append the fallback path
last, so they need no wire-format change.
Dependency display, rename/remap, and PCK inclusion preserve the selector in the serialized reference while using
the provider for filesystem work. Several referenced exports include and load their module bundle once.

Project settings cannot reuse the legacy autoload string for an export. That syntax is
`[*]path::uid`, so a qualified path would make `ProjectSettings::parse_autoload_value()` treat the selector as the
start of the UID and silently fall back to the raw provider. Autoload parsing and stringification therefore gain a
Dictionary branch and keep the existing string branch. A declaration-bearing selection is written as a structured
value equivalent to
`{ "uid": "uid://c...", "path": "res://models/module.fs::my_project.models.Entity", "singleton": true }`.
Untouched legacy autoload strings remain strings and retain their stored value; conversion occurs only when an
editor write newly selects or edits a declaration-bearing export. Standalone selections continue using the legacy
form.

`application/run/main_loop_type` follows the same on-write rule. Existing native class names, global class names,
and standalone paths remain strings. Selecting or editing a declaration-bearing compatible export writes the
provider UID and qualified fallback path as a Dictionary, and startup accepts both representations. The node's
existing `script` property remains a normal `Script` resource in both text and binary scenes.

A provider-file move is transparent because the UID resolves to its new path. Renaming an export, moving it to a
different provider, or changing its namespace changes the selector, UID, or both and is a symbol refactor that
updates scenes and project settings. If the provider resolves but the suffix names a missing, private, duplicated,
or wrong-kind declaration, the dependency editor reports the provider and canonical identity separately and offers
compatible public exports as repair targets. It never falls back to the module container or another declaration
with the same short name.

The same declaration reference is used anywhere the project persists a script choice. A concrete exported `Node`
class may be an autoload, a compatible concrete `MainLoop` subclass may be the project main loop, and exported
classes appear in inheritance and attachment pickers wherever an equivalent standalone global class would. The raw
module is never eligible for any of those roles. Autoload and main-loop indexing therefore consume declaration
lists and validate the selected export's base and abstract state rather than treating the provider path as a class.

### Persistence alternatives considered

A canonical-identity-only URI was rejected because it would require every resource-path consumer to implement a
second addressing and remapping system. A separate `declaration` field beside an unqualified path was rejected
because the resource cache, in-flight loader table, saver, and dependency APIs are path-keyed; threading a selector
through all of them would be a pervasive core change. A scene-local wrapper resource was rejected because it would
duplicate `Script` identity and complicate reload. The selected qualified path uses the existing string keys and
`::` convention, while the provider UID supplies move stability and the full canonical suffix supplies validation.

## Editor and Language-Server Behavior

The filesystem and script editor display module files as a distinct "Foundry Script Module" source kind with a
dedicated icon. Modules open in the ordinary script editor and support formatting, diagnostics, document symbols,
folding, semantic tokens, and navigation.

The editor filesystem stores all public declarations returned for a path rather than one `ScriptClassInfo` record.
The raw module is excluded from Attach Script, script-property, autoload, main-loop, and script-inheritance choices.
Exported classes appear individually in global type searches and every picker where their kind and base are
compatible. An attachment entry shows both identity and provider context, for example
`Entity — my_project.models (module.fs)`, and uses the exported class's own `@icon` metadata.

Completion inside a selective import lists only public declarations in the named namespace and includes declaration
kind and provider detail. Completion elsewhere respects wildcard, selective, implicit-own-namespace, and qualified
visibility. Hover displays the export signature and provider. Definition navigates to the original declaration.
References and rename operate on canonical identity rather than matching text. Rename updates declarations, imports,
qualified uses, serialized scene references, and declaration-aware project settings.

Doc comments on exported declarations feed the same generated script-class reference pipeline as standalone global
declarations. Each export gets its own documented page or symbol entry keyed by canonical identity; private module
declarations remain available to in-file hover and completion but are omitted from public generated documentation.

Unsaved open module buffers participate in indexing through the existing source-override mechanism. Editing an
export refreshes diagnostics and completion in open consumers without requiring a save.

## Diagnostics

Diagnostics should name the declaration kind, canonical identity, and provider path when those details help the
user act. Representative messages include:

```text
Foundry Script module files must declare a named namespace.

Foundry Script module member "make_entity" cannot be a function.
Declare functions, variables, constants, and signals inside a class or trait.

"export" is only valid on a Foundry Script module-level type, class, trait, enum, or tuple declaration.

"export var" does not declare an inspector property. Use "@export var" inside a class;
Foundry Script modules export only named types.

Export "my_project.models.Entity" is also declared in
"res://my_project/other_models.fs".

Public class "my_project.models.Entity" exposes private Foundry Script module type "InternalState"
through property "state".

Could not import "EntityID": namespace "my_project.models"
has no public declaration with that name.

Type alias "EntityID" has no runtime value. Use it in a type annotation;
construct or import one of its concrete member types instead.

Cyclic exported type aliases: "models.A" (res://models/a.fs) ->
"shared.B" (res://shared/b.fs) -> "models.A". Break the cycle in one alias expansion.

Cannot preload Foundry Script module "res://my_project/models/module.fs".
Import one of its exported declarations, or load its full qualified export path.

Cannot extend Foundry Script module "res://my_project/models/module.fs" through ".Entity".
Import "my_project.models.Entity" and extend "Entity" instead.

Cannot assign Foundry Script module "res://my_project/models/module.fs" as an object script.
Import or select one of its exported classes instead.
```

An exported class that is valid but incompatible with a selected node reports the same base-compatibility style as
ordinary scripts, while naming the exported identity. The editor should filter known-incompatible choices before
the user attempts attachment.

## Compatibility

Existing source files and `import dotted.namespace` declarations keep their current meaning. Ordinary type aliases
remain lexical and are neither inherited nor imported. No filename gains special behavior. Contextual recognition of
the new words avoids stealing `module`, `export`, or `from` from existing expression positions.

The list-returning global-declaration API is source-compatible for script languages that implement only the current
single-declaration query: the base adapter returns zero or one entry, and `Script::can_attach_to_object()` defaults
to true. Foundry Script alone opts module resources into multi-declaration scanning and non-attachability.

Namespace-wide imports gain visibility of module exports. This can reveal a new ambiguity when a project adds an
export whose short name collides with another imported namespace; that ambiguity is the intended consequence of
changing the namespace's public API and is diagnosed at the use site or import.

## Verification

Tests must assert behavior rather than source text. Required coverage includes:

- Parser acceptance, recovery, ordering rules, contextual-keyword behavior, and formatter round trips, including a
  colonless/unindented `module`, rejected head modifiers, and rejected pre-namespace `@tool`.
- `GRAMMAR.md` updates for module source units, exports, selective imports, and type lookup precedence.
- Rejection of module-level annotations, retroactive conformances, nested exports, and executable declarations, with
  declaration-specific recovery diagnostics.
- Per-export `@tool`, `@icon`, and `@static_unload` parsing, metadata, editor display, and invalid-target diagnostics.
- Declaration indexing after create, edit, delete, rename, move, parse failure, and duplicate identities.
- Many declarations at one path and removal of every indexed declaration by path.
- Persisted declaration lists in the global cache, editor rebuild after a cache-version change, and stripped-runtime
  rejection of an incompatible cache.
- Wildcard, selective, renamed, implicit-own-namespace, and fully qualified resolution, including selective imports
  of standalone global declarations.
- Selective imports from native namespaces, selective imports from the file's own namespace, freely interleaved
  wildcard/selective imports, and formatter source-order preservation.
- Legal import cycles between module files in different namespaces, independently from illegal type cycles.
- The complete bare-type precedence order, including built-in/native protection and selective-import aliases.
- Local/import/export collisions, duplicate imports, missing public declarations, and ambiguous namespaces.
- Selective-import lexical narrowing together with namespace-wide retroactive-conformance reach, while unrelated
  type and custom-annotation names remain unavailable.
- Private declaration visibility and public-API leakage through every supported type surface.
- One shared cycle-safe `DataType` traversal covering all nested type slots used by private-leak validation.
- Local and cross-module alias, inheritance, trait, enum-payload, and tuple cycles.
- Alias expansion in the provider's lexical context and runtime erasure.
- Exported class construction, inheritance, trait use, enum operations, and tuple behavior.
- Parser and runtime `find_class` / `has_class` round trips for a direct export, a class nested inside an export, and
  a private direct module class.
- Direct and transitive cache invalidation, including a previously missing export becoming available.
- Unsaved LSP provider changes refreshing diagnostics and completion in consumers.
- Completion, hover, definition, references, semantic tokens, document symbols, and rename.
- Generated documentation for each public export and omission of private module declarations.
- Module bytecode generation/loading and stripped-runtime resolution without source, plus clear rejection of older
  bytecode versions.
- Transactional reload: surviving-handle identity and state preservation, add/remove/rename reconciliation,
  last-known-good behavior on compile failure, a held `Ref<Script>` remaining current, and no mixed runtime bundle or
  conformance generation.
- Whole-file, generation-tagged updates for `ScriptServer`, editor filesystem, autoload, and LSP consumers, including
  permitted lag behind an atomic runtime commit without partially applied declaration lists.
- Qualified paths in the persisted `ScriptServer` cache, and provider-path removal dropping every registered export
  under that provider rather than only the first.
- Release mangling protection for public export names, canonical FQNs, and qualified paths, while private module
  declarations remain manglable.
- Import-instead diagnostics for `preload` and literal `load` of a provider root, dynamic-load runtime rejection, and
  both `extends "res://module.fs"` and `extends "res://module.fs".Entity`.
- Script-bound `ResourceLoader.load`, threaded request/status/get, `has_cached`, and `get_cached_ref` rejection of an
  unqualified module provider, while qualified loads, `FSCache`, and script-editor opening still work.
- Raw-module attachment rejection through editor UI and `Object::set_script`.
- Exported-class attachment compatibility, save/reopen scene round trips, provider UID moves, renames, missing
  exports, and dependency-editor repair.
- Two exports from one module producing distinct cached `Resource` objects, repeated loads returning the same object,
  and one provider bundle load.
- `ResourceLoader::exists()`, `get_resource_type()`, and `get_resource_uid()` returning export-specific answers for a
  qualified path and rejecting a bogus selector without falling back to the provider.
- Removing an export, loading its qualified path, and receiving a missing-export error rather than a stale cached
  handle; restoring it reuses the tombstoned handle and makes a held `Ref<Script>` current again.
- Saving while an export is tombstoned writes a recoverable missing-script reference and never inlines the invalid
  handle as an embedded subresource.
- Text scene save -> reopen -> save byte stability for a qualified export reference.
- Binary `.scn` round trips using the qualified path, loading a scene written in the previous binary format, and
  failure of an unsupported or missing selector without raw-module fallback.
- A scene reference surviving both a provider move resolved by UID and a `.remap`, with its selector reattached in
  each case.
- Exported-class autoload and main-loop eligibility, with raw-module and incompatible-export rejection; an exported
  autoload boots, and saving the same project leaves an untouched legacy `path::uid` autoload unchanged.
- A qualified dependency surviving `get_dependencies` -> dependency-editor display -> `rename_dependencies` with
  its selector intact, plus PCK inclusion and one-time loading of a bundle referenced through several exports.
- A module export registered as a custom `ResourceFormatLoader` or editor-plugin-style global loading through its
  qualified `ScriptServer` path, or receiving a precise incompatible-export diagnostic.

Final validation uses the repository's agent build wrapper and the full Foundry Script test suite. Editor attachment
and scene-persistence behavior must also be exercised through the editor automation MCP workflow.

## Delivery Plan

The feature should be implemented through dependency-ordered changes on the long-lived
`feature/foundry-script-namespace-modules` branch while remaining unavailable as a partial alias-only public
language feature. In particular, the parser must not accept `module` on `develop` until declaration indexing,
runtime loading, persistence, and editor support are complete end to end:

1. Add source-unit AST, parser, formatter, and diagnostics behind the complete feature branch.
2. Generalize the global declaration index and centralize declaration loading.
3. Add module visibility, selective imports, alias resolution, dependency tracking, and cycle detection.
4. Compile, atomically reload, and load exported classes, traits, enums, and tuples with bytecode/export support;
   extend parser/runtime FQN containment, provider moves, and export tombstones for non-prefix direct children.
5. Add UID-backed qualified export paths, complete loader-virtual dispatch, attachment capability, dependency-reader
   and core UID/remap preservation, structured project settings, and scene persistence.
6. Complete editor, LSP, refactoring, documentation, and end-to-end verification.

The estimated implementation cost is 10-16 engineer-weeks for an engineer already familiar with the Foundry Script
front end and Godot editor/resource systems. Atomic reload and state migration, declaration-aware persistence, and
the one-to-many editor/index conversion are the highest-risk parts. The estimate explicitly includes
`is_built_in()` virtualization, qualified-path coverage across the Foundry loader virtuals, core provider-only
localization/remapping and UID reattachment, dependency parsing, autoload/main-loop setting migration, and the
module FQN-containment changes. These are bounded changes to existing string keys and virtuals; they do not require
new cache-key types, a second selector API, or a core-wide cache-validity hook. Parsing `module`, `export`, and
selective imports is comparatively small.
