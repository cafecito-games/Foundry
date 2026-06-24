# GDScript Traits Design

Date: 2026-06-23

## Summary

Add **traits** to GDScript: reusable units of state and behavior that classes mix
in with the `uses` keyword. Traits are nominal types, so they participate fully in
the type system — `is`, `as`, type annotations, parameters, return types, typed
containers, and flow-sensitive narrowing all work with traits exactly as they do
with classes.

Two declaration forms are supported:

Inline traits, declared as a class member like an inner class:

```gdscript
class_name Player
extends Node
uses Damageable, Movable

trait Damageable:
	var health: int = 100
	func take_damage(amount: int) -> void:
		health -= amount
```

Global traits, declared at file level and globally named (parallel to `class_name`),
living in normal `.gd` files:

```gdscript
trait_name Damageable
extends Node

signal died

const MAX_HEALTH := 100
var health: int = MAX_HEALTH

func take_damage(amount: int) -> void:
	health -= amount
	if health <= 0:
		died.emit()
```

The design reuses the upstream Godot trait work where practical — flattening of
trait members from PR 107227 and `is`/`as` type testing from PR 112933 — while
adapting conflict resolution, typing discipline, and tooling to this fork's
stricter, statically-typed direction.

## Goals

- Provide inline traits (`trait X:`) and global traits (`trait_name X`).
- Apply traits to classes with `uses A, B, C`.
- Make traits nominal types usable in `is`, `as`, annotations, parameters, return
  types, and typed containers such as `Array[Damageable]`.
- Extend the fork's existing flow-sensitive `is` narrowing to trait types.
- Support base-class constraints, required (abstract) methods, trait-requires-trait
  composition, and trait state (vars, signals, constants, enums).
- Resolve member conflicts by explicit disambiguation rather than silent ordering.
- Be namespace-aware and async-aware from the first version.
- Reuse upstream implementation code where it fits.

## Non-Goals (v1)

- First-class trait objects with a per-class trait table (dispatch indirection).
  This is tracked as a follow-up and may replace the flattening model later.
- Structural / duck typing. Trait identity is nominal: a class satisfies a trait
  only if it explicitly applies that trait (directly or transitively).
- Separate `.gdt` trait files. Global traits live in `.gd` files via `trait_name`.
- Generic traits.

## Declaration Model

A trait may be declared in two ways.

Inline traits are class members, declared with the `trait` keyword and an indented
body, similar to an inner class. They are addressable from outside via the
enclosing class using qualified syntax (`MyClass.MyTrait`).

Global traits are declared with a `trait_name` header at the top of a `.gd` file,
parallel to `class_name`. A global trait is registered in the global script class
registry under its name (and qualified namespace name when a `namespace` is
present — see Namespace Interop). A file declares at most one global trait via
`trait_name`, and `trait_name` and `class_name` are mutually exclusive in the same
file.

Traits may contain:

- Methods, including required (bodyless) methods and `async func` methods.
- Variables.
- Signals.
- Constants.
- Enums.

Traits may not be attached to a node and may not be instantiated directly.

## Base-Class Constraint

A trait may declare an `extends` clause naming a native or script base class:

```gdscript
trait Movable extends Node2D
```

A class may only apply a trait whose base-class constraint it satisfies (the class
derives from the constraint type). Violations are analyzer errors. When a trait has
no `extends` clause it may be applied to any class.

## Application

A class applies one or more traits with a `uses` clause in its header, after
`extends`:

```gdscript
class_name Player
extends Node
uses Damageable, Movable
```

Applying a trait flattens its members into the class and adds the trait's nominal
identity to the class's trait set.

## Required Methods

A bodyless method signature inside a trait declares a required method that
implementers must provide:

```gdscript
trait Damageable:
	func take_damage(amount: int) -> void   # required, no body
```

Implementers must provide a method with a compatible signature, mirroring
`@abstract` semantics. Required methods integrate with the async contracts epic:
`@abstract async func` is allowed in a trait, and async invariance applies to the
implementing override.

## Trait State

Variables, signals, constants, and enums declared in a trait are flattened and
recompiled into each implementing class so constructors and initializers run
correctly per implementer. Identical state arriving via a diamond (the same trait
reached through multiple paths) is included once.

## Trait-Requires-Trait Composition

A trait may itself apply other traits:

```gdscript
trait Movable extends Node2D:
	uses Positioned
```

Application is transitive: a class that uses `Movable` also gains `Positioned`'s
members and trait identity. Each trait is included exactly once regardless of how
many paths reach it (diamond-safe).

## Conflict Resolution: Explicit Disambiguation

When two applied traits, or a trait and the using class, define overlapping members,
the conflict is resolved explicitly rather than by `uses`-list ordering:

- Methods: a name collision between two applied traits is an error unless the using
  class explicitly overrides the method with a compatible signature. The override
  may delegate to a specific trait's implementation.
- Variables, constants, enums, signals: a collision is an error unless the using
  class redeclares the member. Redeclared state must have an identical type.
- Diamond inclusion is not a conflict: when the same trait arrives via multiple
  paths it is included once and does not require disambiguation.

Conflict diagnostics name both originating traits (and the resolution paths) so the
error is actionable.

Open decision for the spec review: whether redeclared state must only match type, or
must also match default value. Default position is type-match only.

## Type System Integration

Each trait is registered as a nominal type. Traits are usable in:

- Variable, parameter, and return type annotations: `var d: Damageable`.
- Typed containers: `Array[Damageable]`, `Dictionary[String, Damageable]`.
- Type tests and casts: `obj is Damageable`, `obj as Damageable`.

`obj is Damageable` is true only when `obj`'s script applies `Damageable` directly or
transitively (nominal membership in the trait set). `as` returns the object typed as
the trait on success and `null` on failure. Trait-typed values are `Object`-backed,
so they do not benefit from native-class optimizations; this is a documented
tradeoff of the flattening model.

### Flow-Sensitive Narrowing

This fork already narrows the static type of an identifier inside an `is` branch for
class types (`resolve_if` → `type_test_narrowing_identifier` → `apply_flow_narrowing`
in `gdscript_analyzer.cpp`, handling `not` and `else` negation). Trait types extend
this existing path so the following works:

```gdscript
if obj is MyTrait:
	obj.some_method_in_my_trait()   # obj is narrowed to MyTrait in this block
```

Member access on a trait-narrowed identifier resolves the trait's members, including
members contributed by transitively-required traits. Narrowing clears on
reassignment, consistent with current behavior for classes.

## Parser / AST

- Tokenizer: add `TRAIT` and `USES` tokens. Handle `trait_name` like `class_name`.
  Following the precedent set by the contextual `async` modifier, evaluate
  contextual handling where it limits breakage of existing identifiers.
- AST: introduce a `TraitNode`, reusing `ClassNode` member infrastructure where it
  fits. Extend `ClassNode` with a resolved `used_traits` list. For global traits,
  carry `trait_name` and qualified-name metadata mirroring the namespace epic's
  `ClassNode` fields.
- Ordering rules (parser errors): `uses` appears only in the class header after
  `extends`; `trait_name` is a top-level header like `class_name`; a file may not
  combine `trait_name` with `class_name`.

## Analyzer

- Resolution: resolve each `uses` entry to a trait — an inline trait (local or
  `OuterClass.Trait`), a global `trait_name` trait, or a namespace-qualified trait.
  Namespace resolution reuses the helpers from the namespaces epic.
- Base-class constraints: verify the implementer satisfies every used trait's
  `extends` constraint.
- Required methods: verify every required (and `@abstract`) method, including
  `@abstract async func`, is implemented with a compatible signature; async
  invariance from the async epic applies.
- Conflicts: apply explicit-disambiguation rules above; treat diamonds as single
  inclusion.
- Type integration: register traits as nominal types; allow them in annotations,
  parameters, returns, and typed containers; resolve `is`/`as`/narrowing against
  trait-set membership.

## Compiler / Runtime (Flatten + Trait-Set Tagging)

The runtime model is member flattening with nominal trait identity, borrowing
upstream PR 107227 for flattening and PR 112933 for type tests.

- Flattening: copy trait members into each implementing `GDScript` at compile time.
  To limit code bloat, identical method bytecode points at a single shared
  `GDScriptTrait` function object rather than being physically duplicated; state is
  recompiled per implementer for correct construction.
- Trait set: each compiled `GDScript` records its transitive set of trait
  identities. `is` and `as` test membership across the script inheritance chain.
- Cache: `GDScriptCache` manages trait compilation artifacts and purges them after
  compilation to avoid stale cross-file caches (a lesson from proposal 6416).
- Reflection: surface trait identities and required-method contracts through script
  reflection / `MethodInfo` so tooling and other languages can observe them.

A future migration to first-class trait objects with a per-class trait table is
tracked as a follow-up and would replace flattening without changing the language
surface.

### Implementation Status (compiler / runtime)

The compiler/runtime flattening landed with the following shape:

- Flattening is physical: each implementer recompiles the trait's variables,
  constants, enums, signals, and method bodies into itself. The flattenable trait
  members are collected in declaration order across the transitive trait set
  (`GDScriptCompiler::_collect_flattened_trait_members`), skipping names the
  implementer or any of its base classes already defines (an override that shadows
  the trait, matching analyzer resolution) and including a diamond-reached trait
  once. Abstract requirements, inner classes, and export groups are not flattened.
  Instance and static trait state is initialized per implementer in the
  implicit/static initializers, and trait-provided static data drives the script's
  static registration just like an inline `static var`.
- The shared `GDScriptTrait` function object (pointing identical method bytecode at
  one shared object) is intentionally deferred to a follow-up. Physical
  recompilation is correct because trait methods bind member access by name against
  the implementer's flattened member layout, which a shared object would have to
  normalize. This is a pure code-size optimization, not a correctness requirement.
- Trait-set tagging and `is`/`as` membership across the inheritance chain were
  already in place (`script_trait_list`, `has_script_trait`); this work makes the
  flattened members usable, which required extending analyzer member/identifier and
  call resolution so a class can see its applied traits' members (previously only
  trait-typed values resolved trait members).
- External global traits are raised to `FULLY_SOLVED` before an implementer's body
  resolves, so identifier sources inside trait method bodies are resolved before the
  bodies are flattened and compiled.

### Object-backed trait-typed value tradeoff

A trait-typed value (`var d: Damageable`) is `Object`-backed: it is just the
implementing instance observed through the trait's nominal identity. Because the
trait has no native class of its own, trait-typed values do not benefit from
native-class call/property optimizations the way an engine-class-typed value does,
and member access through a trait type resolves against the flattened members rather
than a dedicated dispatch table. This is the documented cost of the flattening model
and is the motivation for the deferred first-class-trait-object follow-up.

### Cross-file cache lifecycle

Because flattening recompiles trait members directly into each implementer, there is
no separate trait compilation artifact to cache or purge. Cross-file freshness rides
on the existing dependency tracking: an implementer resolves an external global trait
through `get_depended_parser_for`, so a change to the trait's file invalidates and
recompiles the implementer through the normal reload path.

### Known limitations

- Transitive external traits: direct members of a directly applied trait flatten
  across files, but accessing a member that arrives only *transitively* through an
  external global trait (class uses global trait `A`, `A` uses global trait `B`, and
  the class names `B`'s members directly) currently fails analyzer resolution, because
  the implementer's parser cannot reach a transitively-used external trait's parser.
  Inline transitive traits and indirect use (an `A` method that touches `B`'s state)
  work. Wiring transitive external trait parsers is tracked as a follow-up.
- Static-vs-instance name collisions between a trait member and a base/class member
  are not yet diagnosed. A trait member whose name collides with a *native* base member
  or a non-overridable base member is rejected (as the class's own members would be),
  and a method may override a base method, but a static trait member sharing a name with
  an instance member (or vice versa) is not flagged and can resolve inconsistently
  between the analyzer and codegen. Tightening static/instance conflict detection is
  tracked as a follow-up.

## Namespace Interop

- Global traits share the qualified-name registry from the namespaces epic.
  `uses characters.Damageable` resolves by fully qualified name, and imported short
  names resolve through the same import rules as classes.
- The mixed-folder / mixed-namespace warning from the namespaces epic extends to
  cover trait declarations.

## Async Interop

- Traits may declare `async func` and `@abstract async func` methods.
- `METHOD_FLAG_ASYNC` reflection and async override invariance from the async epic
  apply to trait methods and to the classes that implement them.

## Editor / LSP / Docs

- Completion: `uses` suggests known traits; trait names appear in type positions;
  required-method override completion includes `async` where appropriate; member
  completion works on trait-typed and trait-narrowed values.
- Navigation: go-to-definition and project-wide rename across trait declarations and
  `uses` references, reusing the refactoring engine.
- Highlighting: `trait`, `trait_name`, and `uses` are recognized.
- Symbols and signatures: document symbols, signature help, and completion details
  include trait information.
- Docs: documentation generation renders traits and `trait_name` globals.

## Testing

- Parser: inline and global declarations; `uses` placement; ordering and
  mutual-exclusion errors.
- Analyzer: resolution (local, global, namespace-qualified); base-class constraint
  satisfaction and violation; required-method implementation and async invariance;
  conflict errors and explicit overrides; diamond single-inclusion; nominal `is`/`as`
  membership including transitive traits.
- Narrowing: `if obj is Trait:` narrows in the true block; `not` and `else`
  negation; nested and multiple trait narrowing; narrowing clears on reassignment.
- Runtime: flattening correctness; trait state initialization; `is`/`as` against the
  trait set; transitive trait identity.
- Tooling: completion, rename, docgen, LSP symbols.
- Negative: confirm structural typing is rejected — a class with matching members
  that does not apply a trait is not an implementer.

Use GDScript script fixtures under `modules/gdscript/tests/scripts/` plus C++ tests
where needed.

## GitHub Epic Breakdown

Create one tracking epic with native sub-issues:

1. Core / reflection plumbing: trait identity in script reflection and required-method
   surfacing through `MethodInfo`.
2. Parser / AST: `trait`, `trait_name`, `uses`; `TraitNode`; ordering diagnostics.
3. Analyzer — resolution and constraints: resolve `uses`, base-class constraints,
   required methods, and trait-requires-trait composition with diamond handling.
4. Analyzer — conflicts and type integration: explicit-disambiguation errors;
   nominal `is`/`as`; trait-typed variables, parameters, returns, and containers;
   flow-sensitive `is` narrowing for traits.
5. Compiler / runtime: flatten members, shared function objects, trait-set registry,
   and cache lifecycle.
6. Namespace interop: qualified trait names, imports, and mixed-folder warning.
7. Async interop: async and abstract-async trait methods, invariance, and flags.
8. Editor / LSP / docs: completion, go-to-definition, rename, highlighting, docgen,
   and symbols.
9. Tests: parser, analyzer, narrowing, runtime, and tooling coverage.
10. Follow-up research: migrate to first-class trait objects with a per-class trait
    table (replaces the flattening model).

## Open Decisions

- Redeclared state on conflict: type-match only (default) versus type-and-default
  match. To be confirmed during spec review.
