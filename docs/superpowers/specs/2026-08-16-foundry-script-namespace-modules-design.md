# Foundry Script Namespace Modules Design

**Date:** 2026-08-16
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

export class Entity extends RefCounted:
	var id: EntityID
```

The `module` marker occupies the same source-unit position as `class_name`, `trait_name`, `enum_name`, or
`tuple_name`. A module must declare a non-empty namespace. Imports remain between `namespace` and the source-unit
head, matching existing file ordering.

Conceptually, the grammar adds these productions:

```ebnf
module_head          = "module", NEWLINE, { module_member } ;
module_member        = { module_declaration_modifier }, module_type_decl ;
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

Selective import braces allow line breaks and a trailing comma. `module`, `export`, and `from` are contextual in
their declaration positions so existing value-position identifiers retain their current meaning. The formatter
places `export` before other declaration modifiers, as in `export abstract class`; the parser accepts the normal
leading modifier run in any order.

### Allowed module members

A direct module member may be a type alias, class, trait, enum, or tuple. Each kind accepts `export`. A declaration
without `export` is private to that module file.

Documentation strings and declaration annotations remain valid where the corresponding declaration kind already
accepts them. They describe the following private or exported declaration and do not create module exports of their
own. Script-instance annotations such as `@tool` are invalid on the module container.

A module cannot directly declare variables, constants, functions, signals, properties, an `extends` or `uses`
clause, or executable statements. Exported classes and traits remain normal class bodies and may contain the
members that their existing grammar permits.

`export` is valid only on a direct module type declaration. It is an error on ordinary class members, nested
declarations, module-disallowed declarations, or declarations in a non-module source unit.

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

A public alias may expand through private aliases when the complete expansion contains only public nominal types,
built-in types, and other legal structural members. For example, `EntityID` may expand through a private alias to
`int | uint` because no private identity survives.

An exported declaration must not expose a private nominal declaration through any externally visible surface.
This includes class bases, used traits, property and method signatures, enum payloads, tuple fields, generic bounds,
and an exported alias's fully expanded type. The analyzer reports the declaration path that leaks the private type.

An exported identity collides with every other global type-bearing declaration of the same canonical name,
including exports from other modules and existing `class_name`, `trait_name`, `enum_name`, and `tuple_name`
declarations. Duplicate identities are errors at every conflicting declaration. A module may still have private
declarations with names used by declarations in other files because their identity is file-local.

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
abstract/tool/trait metadata, when applicable
```

The source range is tooling metadata and is not the persistent identity. The declaration name is sufficient to
find a direct module member after reparsing; an implementation may also retain a parser-local declaration ID while
the parser tree is alive.

Index extraction parses the whole file but does not run the analyzer, following the existing custom-annotation and
retroactive-conformance index pattern. A syntactically broken module contributes no partial exports. Re-indexing a
path atomically replaces every declaration previously indexed for that path. Create, edit, delete, rename, and move
operations must not leave stale identities.

The engine's current global-script-class scan returns one declaration per path. It must be generalized to accept a
list of global declarations from a language. Other languages can keep implementing the existing single-declaration
API through a compatibility adapter. Global registry removal by path must remove every declaration at that path.

Global declaration consumers should resolve a declaration through one centralized resolver rather than assuming
that `ResourceLoader::load(global_class_path)` always returns the requested class. Standalone files continue to
resolve to their root script. Module exports resolve to the matching declaration inside the compiled module.

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

- The namespace must exist and export the requested name.
- The source identity must not be duplicated.
- Two imported items must not introduce the same local binding.
- A local declaration must not collide with a selective-import binding.
- Imports from different namespaces that introduce the same local name require `as` disambiguation.
- Repeated identical import items receive a duplicate-import diagnostic rather than being silently coalesced.

Wildcard imports preserve their existing ambiguity rules and include all module exports. A selective import does
not make other declarations from its namespace visible.

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

The module source path identifies the compilation bundle. Each runtime-bearing export additionally carries its
canonical declaration identity. Bytecode records both so stripped runtimes can load the bundle once and select the
requested export without the source front end.

Static `preload` of a module path does not produce a class handle and cannot be used with `.new()`, `extends`, or
`uses`. Imports are the source-language mechanism for reaching module exports. The resource system may load the
module container internally for compilation, tooling, or dependency purposes, but that container has no user-level
runtime value.

## Attachment and Scene Persistence

Module capability metadata is available at the parser, declaration-index, compiled-resource, and editor descriptor
layers. The root source unit reports `MODULE`, no base class, `can_instantiate = false`, and
`can_attach_to_object = false`.

`can_instantiate` alone is not an attachment guard because the editor creates placeholders for some ordinary
non-instantiable scripts. The generic `Script` interface should gain an attachment-capability query whose default
preserves existing language behavior. `Object::set_script` and editor attachment flows consult it. A module returns
false, produces no placeholder, and reports a module-specific error when assignment is attempted.

An exported class has its own metadata: canonical identity, provider module path, base type, abstract state, tool
state, and attachment compatibility. A concrete exported `Node` subclass may be attached to a compatible node. An
abstract class, trait, non-`Node` class, enum, tuple, alias, or module cannot be offered as an attachable script.

Scenes persist the exported class's canonical identity, not the raw module path. Conceptually, a scene reference is
a declaration-aware resource such as `foundry-export://my_project.models.Entity`; the exact encoded representation
may be structured rather than textual, but its persistent key is the canonical identity. Resolution uses the
project/export declaration index to locate the current provider module, load its bundle, and return the compiled
class handle.

This makes a provider-file move transparent when the namespace and export name remain unchanged. Renaming an export
is a symbol refactor and must update scene references. A missing, private, duplicated, or wrong-kind identity leaves
the node unscripted and reports a precise load error rather than falling back to the module container.

## Editor and Language-Server Behavior

The filesystem and script editor display module files as a distinct "Foundry Script Module" source kind with a
dedicated icon. Modules open in the ordinary script editor and support formatting, diagnostics, document symbols,
folding, semantic tokens, and navigation.

The raw module is excluded from Attach Script, script-property, autoload, main-loop, and script-inheritance choices.
Exported classes appear individually in global type searches and compatible attachment pickers. An attachment entry
shows both identity and provider context, for example `Entity — my_project.models (module.fs)`.

Completion inside a selective import lists only public declarations in the named namespace and includes declaration
kind and provider detail. Completion elsewhere respects wildcard, selective, implicit-own-namespace, and qualified
visibility. Hover displays the export signature and provider. Definition navigates to the original declaration.
References and rename operate on canonical identity rather than matching text. Rename updates declarations, imports,
qualified uses, and serialized scene references.

Unsaved open module buffers participate in indexing through the existing source-override mechanism. Editing an
export refreshes diagnostics and completion in open consumers without requiring a save.

## Diagnostics

Diagnostics should name the declaration kind, canonical identity, and provider path when those details help the
user act. Representative messages include:

```text
Module files must declare a named namespace.

Module files cannot declare variables, constants, functions, or signals at module scope.

"export" is only valid on a module-level type, class, trait, enum, or tuple declaration.

Export "my_project.models.Entity" is also declared in
"res://my_project/other_models.fs".

Public class "Entity" exposes private module type "InternalState"
through property "state".

Could not import "EntityID": namespace "my_project.models"
does not export that name.

Type alias "EntityID" has no runtime value.

Cyclic type aliases: "models.A" -> "shared.B" -> "models.A".

Cannot assign module "res://my_project/models/module.fs" as an object script.
Import or select one of its exported classes instead.
```

An exported class that is valid but incompatible with a selected node reports the same base-compatibility style as
ordinary scripts, while naming the exported identity. The editor should filter known-incompatible choices before
the user attempts attachment.

## Compatibility

Existing source files and `import dotted.namespace` declarations keep their current meaning. Ordinary type aliases
remain lexical and are neither inherited nor imported. No filename gains special behavior. Contextual recognition of
the new words avoids stealing `module`, `export`, or `from` from existing expression positions.

Namespace-wide imports gain visibility of module exports. This can reveal a new ambiguity when a project adds an
export whose short name collides with another imported namespace; that ambiguity is the intended consequence of
changing the namespace's public API and is diagnosed at the use site or import.

## Verification

Tests must assert behavior rather than source text. Required coverage includes:

- Parser acceptance, recovery, ordering rules, contextual-keyword behavior, and formatter round trips.
- `GRAMMAR.md` updates for module source units, exports, selective imports, and type lookup precedence.
- Declaration indexing after create, edit, delete, rename, move, parse failure, and duplicate identities.
- Many declarations at one path and removal of every indexed declaration by path.
- Wildcard, selective, renamed, implicit-own-namespace, and fully qualified resolution.
- Local/import/export collisions, duplicate imports, missing exports, and ambiguous namespaces.
- Private declaration visibility and public-API leakage through every supported type surface.
- Local and cross-module alias, inheritance, trait, enum-payload, and tuple cycles.
- Alias expansion in the provider's lexical context and runtime erasure.
- Exported class construction, inheritance, trait use, enum operations, and tuple behavior.
- Direct and transitive cache invalidation, including a previously missing export becoming available.
- Unsaved LSP provider changes refreshing diagnostics and completion in consumers.
- Completion, hover, definition, references, semantic tokens, document symbols, and rename.
- Module bytecode generation/loading and stripped-runtime resolution without source.
- Raw-module attachment rejection through editor UI and `Object::set_script`.
- Exported-class attachment compatibility, save/reopen scene round trips, provider moves, renames, and missing exports.
- Export dependency inclusion and one-time loading of a module bundle with several referenced exports.

Final validation uses the repository's agent build wrapper and the full Foundry Script test suite. Editor attachment
and scene-persistence behavior must also be exercised through the editor automation MCP workflow.

## Delivery Plan

The feature should be implemented through dependency-ordered changes while remaining unavailable as a partial
alias-only public language feature:

1. Add source-unit AST, parser, formatter, and diagnostics behind the complete feature branch.
2. Generalize the global declaration index and centralize declaration loading.
3. Add module visibility, selective imports, alias resolution, dependency tracking, and cycle detection.
4. Compile and load exported classes, traits, enums, and tuples with bytecode/export support.
5. Add declaration-specific resource identity, attachment capability, and scene persistence.
6. Complete editor, LSP, refactoring, documentation, and end-to-end verification.

The estimated implementation cost is 8-14 engineer-weeks for an engineer already familiar with the Foundry Script
front end and Godot editor/resource systems. Runtime class identity, global-registry generalization, and scene
persistence are the highest-risk parts; parsing `module`, `export`, and selective imports is comparatively small.
