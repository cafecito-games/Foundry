# Foundry Script Single-Artifact Namespace Files

## Status

Design approved for planning. This spec supersedes the namespace-module portion of
`2026-06-28-gdscript-brace-modules-design.md`; brace-only blocks should be handled by a separate spec and epic.

This spec assumes top-level enum support is complete. The current `enum_name` implementation is treated as the
baseline behavior to generalize.

## Goals

- Make each `.fs` file declare exactly one top-level exported artifact.
- Replace `class_name`, `trait_name`, and `enum_name` with plain top-level `class`, `trait`, and `enum`.
- Add top-level `annotation` as a first-class file artifact.
- Keep `namespace` and `import` as file header declarations.
- Ensure only class artifact files are loadable or attachable as `Script` resources.
- Preserve nested classes, traits, and enums inside class and trait bodies.

## Non-Goals

- No multiple top-level artifacts per file.
- No file-scope functions, variables, constants, or signals.
- No namespace-level static storage model.
- No `default class` marker.
- No change to block delimiters in this spec.
- No compatibility mode for mixing `class_name`, `trait_name`, or `enum_name` with the new artifact syntax.

## File Shape

A `.fs` file is a single-artifact namespace file:

```text
namespace_decl?
import_decl*
artifact_decl
```

`namespace` and `import` are header declarations and do not count as artifacts. The required `artifact_decl` is exactly
one of:

```text
class Name ...
trait Name ...
enum Name ...
annotation Name ...
```

The artifact's canonical name is `namespace + "." + artifact_name`, or just `artifact_name` in the global namespace.

Examples:

```gdscript
namespace games.services

class LoginService extends RefCounted:
	class Request extends RefCounted:
		pass

	enum State {
		IDLE,
		ACTIVE,
	}
```

```gdscript
namespace games.dtos

enum CombatType {
	PVP,
	NONPVP,
}
```

```gdscript
namespace games.contracts

trait Damageable:
	func take_damage(amount: int) -> void
```

```gdscript
namespace games.annotations

annotation RequiresAuth(role: String = "") targets CLASS, METHOD
```

This keeps the Java-like invariant:

```text
one file path = one exported artifact identity
```

## Artifact Kinds

### Class Files

A top-level `class` artifact replaces `class_name`. It is the only file artifact kind that can be loaded as a
`Foundry Script`/`Script` resource, attached to a scene node, assigned to an object's script property, or registered as an
autoload script.

Top-level class files may declare inheritance, traits, generic parameters, annotations, variables, constants,
functions, signals, nested classes, nested traits, and nested enums using the existing class-body rules.

### Trait Files

A top-level `trait` artifact replaces `trait_name`. Trait files are namespace symbols and can be imported or referenced
by their canonical name, but they are not loadable as `Script` resources and cannot be attached to scenes.

Trait bodies continue to use existing trait-body rules and may support nested declarations where traits already allow
them.

### Enum Files

A top-level `enum` artifact replaces `enum_name`. It uses the existing enum body syntax and semantics from the completed
top-level enum work, including namespaced global resolution and enum value constants.

Enum files are namespace symbols, not script resources.

### Annotation Files

A top-level `annotation` artifact replaces annotation-only library files. Annotation declarations become one artifact
per file, which keeps the file model uniform and leaves room for richer annotation syntax later.

The initial syntax can preserve the current custom annotation declaration shape:

```gdscript
annotation RequiresAuth(role: String = "") targets CLASS, METHOD
```

This spec does not require an annotation body syntax, but the file-artifact model should not prevent adding one later.

## Name Resolution

Every top-level artifact is globally addressable by canonical name:

```gdscript
var service: games.services.LoginService = games.services.LoginService.new()
var mode: games.dtos.CombatType = games.dtos.CombatType.PVP
```

Imports expose artifacts by short name, preserving current namespace/import behavior:

```gdscript
import games.dtos

var mode: CombatType = CombatType.PVP
```

If multiple imported namespaces expose the same short artifact name, unqualified use is ambiguous and must be reported
as an error. Callers can always use the fully qualified name.

Nested declarations remain addressed through their containing artifact:

```gdscript
var state: games.services.LoginService.State = games.services.LoginService.State.IDLE
var request := games.services.LoginService.Request.new()
```

## Script Resource Eligibility

Path-based loading remains only as a class-script bridge for Godot editor/runtime systems that operate on `Script`
resources. Normal Foundry Script code should prefer namespace references over `load()`/`preload()` when referring to artifact
types.

Hard rule:

```text
Only class artifact files can load as Script resources.
```

Allowed:

```gdscript
const LoginService = preload("res://services/login_service.fs")
var service := LoginService.new()
```

Rejected:

```gdscript
const CombatType = preload("res://dtos/combat_type.fs")
```

Diagnostic:

```text
Cannot load "res://dtos/combat_type.fs" as a Script because it declares enum
"games.dtos.CombatType". Only class artifacts can be loaded as scripts.
```

The same class-only rule applies to:

- scene script attachment,
- Inspector script property assignment,
- drag-and-drop script assignment,
- ScriptCreateDialog "load existing",
- autoload registration when the autoload entry expects a script resource,
- editor script execution paths such as "run editor script",
- editor code actions that insert class members.

## Central Artifact Query

Eligibility should be centralized so editor and runtime paths do not each re-parse partial language rules. Add or expose
a shared artifact query with a shape like:

```text
GDScriptArtifactInfo:
  path
  namespace
  name
  canonical_name
  kind: CLASS | TRAIT | ENUM | ANNOTATION
  base_type
  is_abstract
  is_final
  is_tool
```

`base_type` and class flags are meaningful only for class artifacts. Enum, trait, and annotation artifacts may leave
class-specific fields empty.

Every consumer that needs a script resource should use a single predicate:

```text
can_use_as_script(path, required_base):
  artifact = get_artifact_info(path)
  return artifact.kind == CLASS
      && artifact's class inheritance is compatible with required_base
```

This predicate gates both resource loading and editor affordances. For example, dragging a scene node into a script to
generate an `@onready var` should be available only when the currently edited `.fs` file is a class artifact. Trait,
enum, and annotation files should hide the action or show a direct diagnostic such as:

```text
This action requires a class script.
```

`@export` insertion and export-group manipulation follow the same rule: they are class-member edits and should never be
offered for enum, trait, or annotation artifact files.

## Parser And AST Design

The parser should keep a file-root concept, but the root records one explicit artifact instead of an implicit script
class. At file scope:

- `class` parses as the exported class artifact.
- `trait` parses as the exported trait artifact.
- `enum` parses as the exported enum artifact.
- `annotation` parses as the exported annotation artifact.
- a second artifact declaration is an error.
- `class_name`, `trait_name`, and `enum_name` are errors with migration diagnostics.
- free `func`, `var`, `const`, and `signal` declarations are errors at file scope.
- ordinary executable statements are errors at file scope.

Inside class and trait bodies, existing nested declaration behavior remains. `class`, `trait`, and `enum` continue to
mean nested declarations when parsed in a class/trait body.

The current `enum_name` implementation can be migrated by removing the `_name` spelling and using the same
single-artifact restrictions for `enum`.

## Indexing And Registration

The global artifact index remains path-keyed with one artifact per `.fs` file. Compared with a multi-symbol namespace
module design, this avoids indexing multiple unrelated exported symbols from one source path.

The registered artifact metadata must include kind:

```text
CLASS
TRAIT
ENUM
ANNOTATION
```

Class artifacts should continue to provide native base type and script metadata needed by editor filters, autoloads, and
scene attachment. Trait, enum, and annotation artifacts participate in namespace resolution and editor navigation but
fail script-resource eligibility checks.

## Diagnostics

Diagnostics should be explicit and migration-oriented:

```text
"class_name" is no longer supported; use a top-level "class" artifact.
"trait_name" is no longer supported; use a top-level "trait" artifact.
"enum_name" is no longer supported; use a top-level "enum" artifact.
Only one top-level artifact declaration is allowed per file.
Expected a top-level artifact declaration after the namespace/import header.
Top-level functions are not supported; declare functions inside a class or trait.
Top-level variables are not supported; declare variables inside a class.
Cannot attach "res://dtos/combat_type.fs" because it declares enum "games.dtos.CombatType".
Only class artifacts can be attached as scripts.
Cannot generate "@onready" members in an enum file. This action requires a class script.
```

## Testing Strategy

Coverage should include:

- parser success for top-level `class`, `trait`, `enum`, and `annotation` artifacts,
- parser errors for `class_name`, `trait_name`, and `enum_name`,
- parser errors for multiple artifacts in one file,
- parser errors for file-scope `func`, `var`, `const`, `signal`, and executable statements,
- nested class/trait/enum declarations inside class and trait artifacts,
- namespace-qualified and imported references to each artifact kind,
- ambiguous imported artifact names,
- class-only `load()` and `preload()` success,
- `load()` and `preload()` failures for enum, trait, and annotation artifact files,
- scene attachment and Inspector script assignment filters for artifact kind,
- autoload validation for non-class artifact files,
- editor drag/drop `@onready` generation disabled for non-class files,
- editor `@export` insertion/manipulation disabled for non-class files,
- LSP/editor completion and go-to-definition for each artifact kind.

## Implementation Staging

This should be a separate epic from brace-only blocks.

1. Introduce artifact metadata and central artifact query.
2. Rename file-head parser forms from `*_name` to top-level `class`, `trait`, `enum`, and `annotation`.
3. Enforce one top-level artifact per file and reject file-scope non-artifact declarations.
4. Thread artifact kind through global registration and namespace/import resolution.
5. Gate `load()`/`preload()` and script-resource paths on class artifacts.
6. Gate editor assignment and member-generation affordances on class artifacts.
7. Update LSP, completion, symbols, and go-to-definition around artifact kinds.
8. Migrate tests, docs, and language primer references from `*_name` to artifact files.

Brace-only block syntax should get its own spec and epic after this file model is settled.
