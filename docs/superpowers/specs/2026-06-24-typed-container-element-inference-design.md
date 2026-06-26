# Typed-Container Element Inference (Phase 2) — Design

Issue: cafecito-games/godot#36 (epic #29, GDScript Migration Wizard).

## Goal

Infer the element type of a bare `Array` local variable from how it is used, so
the migration wizard can upgrade `var items = []` to `var items: Array[int]`
when — and only when — that is provably correct. A clearly monomorphic container
is typed `Array[T]`; a mixed-element or unprovable container is skipped and
reported.

This first slice is **arrays only** and **local variables only**. Dictionaries,
member variables, and `const` locals are tracked as follow-ups (see below).

## Why usage-based inference is needed

The analyzer already infers `Array[T]` from *context* — an array literal
assigned to an already-typed target or passed to a typed parameter
(`update_array_literal_element_type` in `gdscript_analyzer.cpp`). But a bare
`var x = [1, 2, 3]` resolves to plain `Array`, so the existing **Add Type
Annotation** refactor renders `Array`, losing the element type. Recovering it
requires looking at how `x` is used.

## Soundness model

Arrays are reference types in GDScript: a container can be mutated through any
reference that escapes the declaration. Inference is therefore attempted only
when both hold:

1. **The initializer is an array literal** — so the full initial contents are
   known. A non-literal initializer (`var x = make()`) could already hold
   elements of an unknown type, so it is `NOT_APPLICABLE`.
2. **The variable never escapes its function** — it is never returned, passed as
   a call argument, aliased to another lvalue, stored through a member, or
   captured by a lambda. Any escape ⇒ `ESCAPES`.

Within those bounds, the pass accumulates the **union** of every element type
ever stored into the variable (flow-insensitively): the array literal's own
elements, `append` / `push_back` / `push_front` / `fill` / `insert` / `set`
arguments, `append_array` / `assign` source-array elements, and indexed writes
`x[i] = v`. Read-only or remove/reorder methods (`size`, `sort`, `pop_back`,
`erase`, `map`, …) contribute nothing. **Any unmodelled method call on the
variable forces a conservative skip** (`UNPROVABLE`).

Flow-insensitive union is sound: if the union is a single concrete type, every
element the variable can ever hold is that type, so `Array[T]` is correct
regardless of statement order. The only way to be wrong is to *miss* a
mutation — hence escapes, lambda captures, non-literal initializers, and unknown
methods all bail. The post-edit verification harness (#34) re-analyses applied
edits and is a second line of defense, not the first.

Outcomes: `INFERRED` (upgrade to `Array[T]`), `MIXED` / `ESCAPES` / `UNPROVABLE`
(skip and report a reason), `NO_EVIDENCE` (empty literal never pinned — left as
bare `Array`), `NOT_APPLICABLE` (already typed, not an array literal, …).

## Implementation

- `modules/gdscript/editor/gdscript_container_inference.{h,cpp}` —
  `GDScriptContainerInference::infer_local_array_element_type(decl, function_body)`
  returns the outcome plus, on success, the `Array[T]` `DataType`. It is a
  headless, caret-independent API the wizard can also call for reporting.
- `gdscript_refactoring.cpp` threads the enclosing function body through the
  candidate-collection suite walk. When a local `var` resolves to a bare
  `Array`, the renderer substitutes the inferred `Array[T]` type; every other
  outcome falls back to the analyzer's own type, so no declaration is ever
  narrowed unsafely. Members/params/return keep the existing behavior.

## Tests

`modules/gdscript/tests/test_container_inference.h` covers each outcome directly
against a parsed-and-analyzed tree (monomorphic literal, append-grown, nested
block, indexed write, `append_array`, read-only-method tolerance, mixed,
int/float mixing, return/argument/alias/lambda escapes, unmodelled method,
no-evidence, already-annotated, non-literal initializer) plus the end-to-end
candidate-collection path asserting the rendered `: Array[int] = ` edit and the
bare `: Array = ` fallback for a mixed container.

## Read narrowing (issue #313)

Element *reads* (`var v = c[i]`, `for v in c:`) are safe for escape purposes, but
once the container is typed they narrow from `Variant` to the element type. Code
that was valid only because the read produced a `Variant` -- a binding that is
later reassigned to an incompatible value -- would fail analysis after the
upgrade. The only binding that is **both** valid before the upgrade **and** breaks on
reassignment after it is the **`for` loop iterator**: `for v in c:` binds `v` as
`Variant` over a bare container, but as a hard element type (array element /
dictionary key) over a typed one, so a `v = <incompatible>` (or `v op= ...`) in
the loop body becomes an error. The walker records each such loop iterator and,
once the element type is known, bails with the `READ_NARROWS` outcome when the
iterator is reassigned a value whose rendered type does not match the element type
it narrows to (compound reassignments always bail). Reassignments inside a lambda
that captures the iterator are caught too.

A narrowed read binding can also be **used** later in a type-sensitive position that
is valid only while it remains `Variant` -- a typed call argument, a typed return, or
a typed assignment target -- even if it is never reassigned. The `Variant` a bare
read produces coerces into every such position; once the container is typed the
binding narrows to its slot type, which the position rejects unless that slot type is
exactly the type the position requires. The walker records each such use (resolving
call-argument parameter types from the analyzer's `resolved_parameter_types`, returns
against the enclosing function/lambda/method return contract, and assignment targets
against the assignee's hard type) and bails with `READ_NARROWS` when the narrowed slot
type does not match. This covers both the loop iterator **and** the plain
`var v = c[i]` soft local (and element-returning accessor reads bound to one), because
a typed use type-checks the binding directly rather than downgrading it. A position
with no hard requirement (an untyped parameter/return/target) accepts the narrowed
type exactly as it accepted `Variant`, so it never blocks inference. The comparison is
exact rendered-type equality -- conservative (a compatible subtype is treated as a
mismatch) but always sound. A "hard requirement" here means the position's resolved
type is hard: an untyped parameter/return is `Variant`, and a *weak* (unannotated)
assignment target carries only an inferred type that downgrades on an incompatible
store, so neither blocks. The same check also fires for a **direct** element/value read
used without an intermediate local (`take_string(c[i])`, `return c.pop_back()`,
`var s: String = c[i]`), which narrows at the use site just as a bound read would.

The remaining binding forms still do **not** need tracking:

- `var v = c[i]` (plain `=`) is soft-typed for *reassignment*: an incompatible
  reassignment downgrades it to `Variant` rather than erroring, so reassignment never
  narrows it (only a typed use does, handled above).
- `var v := c[i]` (`:=`, hard) cannot occur over a *bare* container in the first
  place: the element read is `Variant`, and `:=` from a value with no set type is
  already a parse error, so no valid pre-upgrade source has this shape.
- An explicit annotation (`var v: Variant = c[i]`) pins the type, so it never
  narrows.
- A `match c[i]: var v:` pattern bind is a constant -- GDScript rejects
  reassigning it -- so it can never trigger the reassignment check.

### Boundary

The reassignment check covers a loop iterator overwritten with an incompatible value
-- a write the narrowed type unambiguously rejects -- and the typed-use check (above)
covers a narrowed read flowing into a typed call argument, a typed return, or a typed
assignment target, the cases identified by #409. Both are detectable cheaply and
soundly from the AST plus the analyzer's resolved types, without a second analysis
pass. What remains outside this pass is genuinely flow- or value-dependent reasoning
that a single conservative annotation cannot capture (e.g. a narrowed read passed to
an overloaded native method whose selected overload depends on the runtime value, or a
typed use reached only on a path where the binding was already downgraded). Proving
those safe requires the whole-body type re-analysis the post-edit verification harness
(#34) already performs, which re-analyses every applied edit and rejects one that newly
fails; it remains the second line of defense, consistent with the soundness model
above.

## Follow-ups (deferred)

- Dictionary `Dictionary[K, V]` element inference (key/value from indexed
  writes and dictionary literals).
- Member-variable element inference (needs whole-class + cross-file escape
  analysis).
- `const` local element inference (immutable binding, but contents can still
  escape, so it needs the same escape analysis).
