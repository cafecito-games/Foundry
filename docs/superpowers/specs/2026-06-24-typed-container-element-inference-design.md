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

The other binding forms do **not** need tracking:

- `var v = c[i]` (plain `=`) is soft-typed: an incompatible reassignment downgrades
  it to `Variant` rather than erroring, so narrowing the read never breaks it.
- `var v := c[i]` (`:=`, hard) cannot occur over a *bare* container in the first
  place: the element read is `Variant`, and `:=` from a value with no set type is
  already a parse error, so no valid pre-upgrade source has this shape.
- An explicit annotation (`var v: Variant = c[i]`) pins the type, so it never
  narrows.
- A `match c[i]: var v:` pattern bind is a constant -- GDScript rejects
  reassigning it -- so it can never trigger the reassignment check.

### Boundary

The reassignment check covers the case where the loop iterator is overwritten with
an incompatible value -- a write the narrowed type unambiguously rejects,
detectable cheaply and soundly from the assignment alone. It does **not** cover
every later *use* of a narrowed read in a type-sensitive position (e.g. passing a
loop iterator or a read result to a typed parameter, returning it from a typed
function, or assigning it to a typed target), where narrowing from `Variant` to
the element type could also change analysis. Proving those positions safe requires the same whole-body
type re-analysis the post-edit verification harness (#34) already performs, which
re-analyses every applied edit and rejects one that newly fails. Detecting them
up front in this pass (resolving call signatures, return contracts, etc.) is
tracked as a follow-up; until then the harness remains the second line of defense
for downstream typed uses, consistent with the soundness model above.

## Follow-ups (deferred)

- Dictionary `Dictionary[K, V]` element inference (key/value from indexed
  writes and dictionary literals).
- Member-variable element inference (needs whole-class + cross-file escape
  analysis).
- `const` local element inference (immutable binding, but contents can still
  escape, so it needs the same escape analysis).
