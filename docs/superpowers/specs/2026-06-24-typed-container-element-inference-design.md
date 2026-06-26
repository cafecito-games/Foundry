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
upgrade. The walker therefore records each local/iterator bound to such a read
(unless it carries an explicit annotation, which pins its type) and, once the
element type is known, bails with the `READ_NARROWS` outcome when any of those
bindings is reassigned a value whose rendered type does not match the element
type it would narrow to. This applies uniformly to the array element, dictionary
key (loop bindings), and dictionary value (subscript bindings) paths.

## Follow-ups (deferred)

- Dictionary `Dictionary[K, V]` element inference (key/value from indexed
  writes and dictionary literals).
- Member-variable element inference (needs whole-class + cross-file escape
  analysis).
- `const` local element inference (immutable binding, but contents can still
  escape, so it needs the same escape analysis).
