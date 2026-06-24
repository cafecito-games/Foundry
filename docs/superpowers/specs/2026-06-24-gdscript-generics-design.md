# GDScript Generics Design

Date: 2026-06-24

## Summary

Add **generic classes and generic methods** to GDScript: classes and methods
parameterized by one or more type parameters, declared and applied with the same
bracket convention already used by `Array[T]` and `Dictionary[K, V]`.

```gdscript
class_name Box[T]

var value: T

func set_value(v: T) -> void:
	value = v

func get_value() -> T:
	return value
```

```gdscript
# Generic method, independent of any class type parameter.
func swap[T](a: T, b: T) -> Array[T]:
	return [b, a]

var b := Box[int].new()
b.set_value(42)
var result := swap(1, 2)        # T inferred as int
var pair := swap[float](1, 2)   # T applied explicitly
```

Generics are **reified**: type arguments survive to runtime. A `Box[int]` instance
carries its bound type arguments and validates writes to `T`-typed members exactly
as `Array[int]` validates element insertion — `b.value = "x"` on a `Box[int]` is a
runtime error, not just a static one. Reification reuses the engine's existing
container-type machinery rather than monomorphizing scripts.

Generics interoperate with this fork's [traits](2026-06-23-gdscript-traits-design.md):
a trait may serve as an upper bound on a type parameter (`Box[T: Damageable]`),
constraining the type argument to types that `uses` the trait.

## Goals

- Generic **classes**: `class_name Box[T]`, `class Inner[T]:`, with members and
  methods typed by the parameter(s).
- Generic **methods**: `func swap[T](...)`, independent of the enclosing class's
  type parameters, with call-site type inference and explicit application fallback.
- **Type bounds**: upper bounds on parameters — `Box[T: Node3D]` (class bound) and
  `Box[T: Damageable]` (trait bound). Members of `T` are typed by the bound inside
  the body.
- **Generic inheritance**: extending generic classes and concrete specializations —
  `class_name Stack[T] extends List[T]` and `extends List[int]`.
- **Reified runtime semantics**: type arguments persist on instances; `T`-typed
  member writes are runtime-validated; `is`/`as` and serialization see the
  arguments.
- **Inference** for generic methods: solve type parameters by unifying argument
  types against declared parameter types; require explicit application only when
  inference is ambiguous or unsolvable.
- **Tooling**: LSP completion, hover, and signature help understand type parameters
  and present substituted (concrete) types.
- **Serialization**: reified generic instances round-trip through `.tres`/scene save
  and load, and survive hot-reload.

## Non-Goals (v1)

- **Generic traits** (`trait Container[T]`). Filed as a follow-up blocked on the
  trait epic ([#89](https://github.com/cafecito-games/godot/issues/89)) maturing
  past its current state and lifting its own generic-traits non-goal.
- **Generic-method requirements within traits** (a trait requiring
  `func map[U](...)`). Filed as a follow-up blocked on generic methods landing.
- **Runtime reflection API** additions exposing generic type information beyond what
  serialization needs. Filed as a follow-up.
- **C# / GDExtension parity** — surfacing generic type information to other language
  layers. Filed as a follow-up.
- **Selective monomorphization** for performance. The reified-descriptor model is
  the v1 implementation; specialization can be layered later if profiling demands.
- **Variance** (covariance/contravariance). Type parameters are invariant in v1.

## Background

Two external references informed this design:

- The upstream proposal
  [godotengine/godot-proposals#13800](https://github.com/godotengine/godot-proposals/issues/13800),
  which favors `@generic(T)` decorators and constrained generics. We diverge on
  syntax (brackets, for consistency with typed containers) but share the
  compile-time-safety goal and adopt its constrained-generics and
  generic-inheritance ambitions.
- The [Reginleif](https://github.com/chesedcore/Reginleif) fork, which ships
  *type-erased* generics with bracket syntax (`class_name Box[T]`, `Box[T: Node3D]`).
  We adopt its syntax and static-analysis model, and add a reified runtime layer on
  top so type arguments are enforced at runtime, not only statically.

This fork already has the infrastructure generics build on: `GDScriptParser::DataType`
carries `container_element_types`, the analyzer's `resolve_datatype()` resolves
bracketed type arguments, and `ContainerType` / `ContainerTypeValidate`
(`core/variant/container_type_validate.h`) validate them at runtime for `Array[T]`
and `Dictionary[K, V]`. Generics generalize this existing parametric-type pattern to
user-defined classes and methods rather than introducing a parallel system.

## Approach

We chose a **reified, single-script** strategy (one compiled `GDScript` per file;
type arguments are reified descriptors carried on instances) over monomorphization
(a distinct compiled script per instantiation) and over a hybrid that selectively
specializes hot paths. The single-script model:

- delivers genuine runtime checking without per-instantiation script copies;
- plays naturally with hot-reload (one script per file, unchanged reload semantics)
  and serialization (persist base script + type-argument descriptors);
- reuses the typed-container machinery (`ContainerType`, `ContainerTypeValidate`,
  `container_element_types`) instead of forking compilation;
- is the direct generalization of how `Array[int]` already works.

Monomorphization was rejected for its combinatorial script explosion, compile-time
and memory blowup, and its conflict with GDScript's single-script-per-file model and
`.tres` serialization. Selective monomorphization was rejected as premature; it can
be layered on later behind the same surface.

## Type Representation

Generics add three concepts to the existing type system rather than a new one:

1. **Type parameter** — a named placeholder declared on a class or method. A new
   `Kind`, `TYPE_PARAMETER`, is added to both `GDScriptParser::DataType` (analysis)
   and `GDScriptDataType` (`gdscript_function.h`, runtime). It carries the parameter
   name, declaration scope (owning class or method), an ordinal index, and an
   optional bound (`DataType`).

2. **Type-argument binding** — the concrete types supplied at a use site
   (`Box[int]` → `{T: int}`), represented as an ordered `Vector<DataType>` attached
   to the specialized type, mirroring how `container_element_types` attaches element
   types to `Array`/`Dictionary` today.

3. **Specialized type handle** — `Box[int]` *as a type*: the existing `SCRIPT`/`CLASS`
   `DataType` for `Box` plus a `Vector<DataType> type_arguments`. A `substitute(type,
   bindings)` operation walks a `DataType`, replaces every `TYPE_PARAMETER` with its
   bound argument, and recurses through containers and nested specializations.

At runtime, `ContainerType` is extended so it can describe a specialized script type
(base script + `Vector<ContainerType> type_arguments`), and a generic instance stores
its bound type arguments alongside its script reference. This is the reification
anchor that validation, `is`/`as`, and serialization all read from.

## Tokenizer & Parser

**Tokenizer**: no new tokens. Generics reuse `[`, `]`, `,`, and `:`.

**Parser nodes**:

- `ClassNode` gains `Vector<TypeParameterNode> type_parameters`, parsed after the
  class name in both `class_name Box[T]` and inner `class Box[T]:` forms. Each
  `TypeParameterNode` holds an identifier and an optional bound (`TypeNode`).
- `FunctionNode` gains the same vector, parsed between the function name and the
  `(`: `func swap[T](...)`.
- `TypeNode`'s existing `container_types` field carries type arguments at use sites
  (`Box[int]`, `Stack[T]`), so a generic use parses structurally like `Array[int]`;
  the analyzer decides whether the base names a container or a generic class.

**Disambiguation**: `Box[int]` as a type annotation versus `arr[i]` as a subscript.
Type-annotation position is already handled by `parse_type()` (as for `Array[int]`).
The new case is expression position — `Box[int].new()` and `swap[int](...)` — handled
in subscript/call reduction: when the base of a `[...]` resolves to a generic class
or generic method (a meta-type), the brackets are a type-argument list rather than an
index. This mirrors the existing handling of `Array[int].new()`.

**Bounds grammar**: `[T: Bound]` and multiple parameters `[K, V: Resource]`. The `:`
inside type-parameter declaration brackets is a bound separator, distinguished from a
dictionary `K: V` literal by parse context (immediately after a class/func name).

## Analyzer

- **Type-parameter scope**: entering a generic class or method pushes a type-parameter
  environment mapping each name to its `TYPE_PARAMETER` `DataType` (with bound).
  `resolve_datatype()` consults this before normal name lookup, so `var v: T` resolves
  to the parameter.
- **Specialization & substitution**: resolving `Box[int]` yields a specialized
  `DataType` (base `Box` + `type_arguments = [int]`); member access runs `substitute()`
  so every `T` in a member's declared type becomes `int`. This generalizes the existing
  `Array[T]` element rewriting.
- **Inference (generic methods)**: unify each argument's type against its parameter's
  declared type to solve type parameters (collect constraints, check consistency, bind).
  Conflicts (`swap(1, "x")`) or unsolved parameters error and direct the user to explicit
  application. Explicit type arguments short-circuit inference.
- **Bounds checking**: a type argument bound to `[T: Bound]` must satisfy `Bound`. Class
  bounds reuse `datatype_derives_from_datatype()`. Trait bounds query the argument class's
  `resolved_traits` (the nominal-membership check modeled by `class_satisfies_trait_base()`).
  Inside the body, `T`-typed values expose the bound's members.
- **Generic inheritance**: substitution flows through the hierarchy — `Stack[T] extends
  List[T]` rebinds `List`'s parameter to `Stack`'s `T`; `extends List[int]` binds it to a
  concrete type. Member lookup substitutes along the chain.

## Trait Interaction

Three intersection points, ordered by dependency on trait progress. Traits in this fork
are `ClassNode` subtypes (`is_trait = true`), applied via `uses`, with nominal transitive
membership tracked in `ClassNode::resolved_traits`. Traits currently cannot be used as
static types (the analyzer hard-errors when a trait appears in an annotation, cast, or
`is` test).

1. **Traits as generic bounds** — `Box[T: Damageable]`. *In scope.* The "traits can't be
   static types" rule stays for annotations/casts/`is`, but a narrow exception is carved
   for **bound position only**: a bound is a constraint, not a storage type, so allowing a
   trait there does not permit declaring `var x: Damageable`. Satisfaction is the nominal
   check — the type argument's `resolved_traits` must contain the bound trait — via a new
   `type_satisfies_trait()` helper reusing existing conformance machinery. Inside the body,
   `T`-typed values expose the trait's required members.

2. **Generic-method requirements within traits** — a trait requiring
   `func map[U](...) -> Array[U]`. *Deferred follow-up, blocked on generic methods.*
   `validate_trait_method_signature()` must compare signatures up to type-parameter
   renaming (alpha-equivalence): an implementing `func map[V](...)` satisfies the required
   `func map[U](...)` when their shapes match after aligning `U`↔`V`.

3. **Generic traits** — `trait Container[T]`, used as `uses Container[int]`. *Deferred
   follow-up, blocked on the trait epic ([#89](https://github.com/cafecito-games/godot/issues/89)).*
   Because `TraitNode` is a `ClassNode`, the class-generics machinery (parameters,
   substitution) carries over. The new work is conformance under substitution: a class
   declaring `uses Container[int]` must implement every trait member with `T := int`
   applied, extending `find_trait_implementation()` / `validate_trait_requirements()` to
   substitute the trait's type arguments before matching signatures. `resolved_traits`
   membership becomes "trait + type arguments."

## Runtime, Codegen & Serialization

This layer is what makes the generics reified.

- **Runtime descriptor**: `ContainerType` is extended to describe a specialized script
  type (base script + `Vector<ContainerType> type_arguments`), mirroring the analyzer's
  specialized `DataType`. `GDScriptDataType` gains the matching `TYPE_PARAMETER` kind and
  `type_arguments` vector so compiled slots carry the information.
- **Instance binding**: a generic instance stores its bound type arguments alongside its
  script reference in `GDScriptInstance`. Construction (`Box[int].new()`) passes resolved
  arguments into instantiation; codegen models a `write_construct_specialized` on the
  existing `write_construct_typed_array` pattern.
- **Codegen**: `T`-typed slots compile as Variant slots tagged with the parameter. Where
  the analyzer knows the concrete binding (member access through a specialized type), it
  emits the already-substituted concrete type, with no runtime cost. Where only the
  parameter is known (inside a generic body operating on `T`), the slot stays a validated
  Variant and writes go through `ContainerTypeValidate`.
- **Validation**: reuse `ContainerTypeValidate::validate()`. Writing to a `T`-typed member
  resolves `T` from the instance's bindings and validates, as typed-array element insertion
  does today — this is what makes `box.value = "x"` on a `Box[int]` fail at runtime.
- **Serialization / hot-reload**: a reified instance persists as base script path +
  type-argument descriptors (each descriptor is the existing builtin/class/script triple,
  recursively). `.tres`/scene save and load round-trip the `type_arguments`. Hot-reload
  keeps one `GDScript` per file, so reload semantics are unchanged; only the per-instance
  binding vector must survive, which it does as plain serialized data.

## Tooling (LSP)

In `modules/gdscript/language_server/`:

- **Completion**: after `Box[`, suggest type arguments; on a `Box[int]` value, list members
  with `T` shown as `int`. Generic methods surface their `[T]` parameters in signature help.
- **Hover**: display the specialized signature (`func get_value() -> int` on a `Box[int]`,
  not `-> T`); hover on a type parameter shows its bound.
- **Signature help**: for `swap[…](…)`, show both the type-parameter list and the value
  parameters, reflecting inference when arguments are already typed.

Tooling reads the analyzer's specialized `DataType` and `substitute()`, so its correctness
follows the analyzer's rather than duplicating logic.

## Testing Strategy

Using the `.gd` + `.out` fixture pattern under `modules/gdscript/tests/scripts/`:

- `analyzer/features/`: generic class declaration/use; generic methods with inference and
  explicit application; class and trait bounds; generic inheritance; nested specialization
  (`Box[Array[int]]`).
- `analyzer/errors/`: bound violations; inference conflicts (`swap(1, "x")`); arity
  mismatches (`Box[int, int]`); unsatisfied trait bounds; bare type parameter used out of
  scope.
- `runtime/features/`: reified behavior — wrong-typed write to a `T` member fails; `is`/`as`
  against specialized types; serialization round-trip.
- `completion/` and `lsp/`: substituted-type completion and hover fixtures.
- C++ unit tests (`tests/`) for `substitute()` and `ContainerType` specialization
  round-trips where pure-GDScript fixtures cannot reach the internals.

## Work Breakdown

Epic: **Generic classes and generic methods for GDScript**.

In-scope sub-issues:

1. Parser: type-parameter declaration on classes and methods (`[T]`, `[T: Bound]`,
   multi-parameter).
2. Parser: type-argument application at use sites, including `Box[int].new()` /
   `swap[int]()` disambiguation.
3. Type model: `TYPE_PARAMETER` kind, `type_arguments` on `DataType`/`GDScriptDataType`,
   and `substitute()`.
4. Analyzer: type-parameter scope and specialized-type resolution.
5. Analyzer: bounds checking (class bounds).
6. Analyzer: generic-method type inference with explicit-application fallback.
7. Analyzer: generic inheritance and substitution through the hierarchy.
8. Trait bounds: bound-position trait exception and nominal satisfaction (intersection 1).
9. Runtime: `ContainerType` / instance reified bindings and construction codegen.
10. Runtime: `T`-member write validation via `ContainerTypeValidate`.
11. Serialization: `.tres`/scene round-trip and hot-reload of reified instances.
12. LSP: completion, hover, and signature help for generics.
13. Docs: GDScript reference and `doc/` updates.
14. Tests: analyzer, runtime, and completion fixtures plus C++ unit tests for `substitute()`.

Deferred follow-up sub-issues (filed, out of this epic's critical path):

15. Generic-method requirements within traits — signature matching modulo type-parameter
    renaming (intersection 2; blocked on item 6).
16. Generic traits — `trait Container[T]`, conformance under substitution (intersection 3;
    blocked on trait epic #89).
17. Runtime reflection API for generic type information.
18. C# / GDExtension parity for generic type information.
