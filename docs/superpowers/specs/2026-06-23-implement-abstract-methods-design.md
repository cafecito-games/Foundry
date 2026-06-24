# Implement Abstract Methods Refactor — Design

**Date:** 2026-06-23
**Status:** Approved design, pending implementation plan
**Branch:** `feature/implement-abstract-methods`

## Summary

Add a GDScript refactor that automatically generates concrete stub methods for
inherited `@abstract` methods a class has not yet implemented. The refactor is
offered in the script editor's refactor submenu and over the language server,
reusing the existing `GDScriptRefactoring` infrastructure.

## Goals

- Let a developer extending an abstract GDScript class fill in all owed abstract
  method stubs in one action.
- Generate signatures that faithfully mirror the base declaration (parameters,
  types, defaults, `static`, return type).
- Surface in both the editor and LSP automatically, with undo/redo and the
  existing parse-result caching.

## Non-Goals

- Native engine virtual methods (e.g. `_input`, `_draw`) are out of scope. Only
  GDScript `@abstract`-annotated methods are considered.
- No checklist/picker UI: all unimplemented abstract methods are generated at
  once.
- No cross-file edits. The refactor only edits the current script.

## Approach

Add a new `RefactorKind::IMPLEMENT_ABSTRACT_METHODS` to the existing unified
refactoring module (Approach A). Both the editor submenu
(`_populate_refactor_submenu`) and the LSP go through
`GDScriptRefactoring::get_available_refactors` / `prepare`, so both surfaces,
undo/redo, and caching come for free. This matches every existing refactor in
the fork.

## Touchpoints

- **`modules/gdscript/editor/gdscript_refactoring.h`** — append
  `IMPLEMENT_ABSTRACT_METHODS` to the `RefactorKind` enum (appended last so the
  editor's `EDIT_REFACTOR_*` ordering static_asserts remain valid).
- **`modules/gdscript/editor/gdscript_refactoring.cpp`**
  - `find_implement_abstract_candidate(context, location, parse_results)` —
    cached availability + collection, mirroring `find_inline_variable_candidate`.
    Runs the analyzer (or reuses the LSP `ExtendGDScriptParser` parse result),
    walks the resolved base chain, collects un-overridden `@abstract` methods,
    and sets a `disabled_reason` when none are missing or the script cannot be
    analyzed.
  - A `prepare` branch for the new kind that renders stubs and emits edits.
  - A results cache struct + get/cache helpers, like the other refactors.
  - `get_available_refactors` — add a `RefactorAvailability` titled
    "Implement Abstract Methods", enabled whenever the enclosing class has
    missing abstract methods regardless of caret position; otherwise disabled
    with a reason.
- **`editor/script/script_text_editor.cpp`** — add
  `EDIT_REFACTOR_IMPLEMENT_ABSTRACT_METHODS` enum constant + ordering
  static_assert, and wire it into `_run_refactor`. No new dialog.

## Detection

After analysis, starting from the `ClassNode` enclosing the caret:

1. **Walk the base chain** via `class_node->base_type`. For each ancestor that
   resolves to a GDScript class, obtain its `ClassNode` — from the analyzer's
   resolved tree for same-file bases, or via `GDScriptCache` / the parse-result
   provider for bases in other files (already resolved during `analyze()`).
2. **Collect abstract methods** — `FunctionNode`s with `is_abstract == true`,
   keyed by method name.
3. **Subtract concrete overrides** — remove any name overridden by a
   non-abstract definition lower in the chain, including the target class and any
   intermediate class. An intermediate concrete override satisfies the contract;
   an intermediate `@abstract` re-declaration does not. The remainder is what the
   target class still owes.
4. **Availability**
   - Empty set → disabled, reason "No unimplemented abstract methods."
   - Analysis failure → disabled, reason "Cannot analyze this script."
   - Otherwise enabled.

Edge cases handled explicitly:

- **Target class is itself `@abstract`** → disabled, reason "Abstract classes
  don't need to implement abstract methods."
- **Inner classes** → resolve relative to the class enclosing the caret, so the
  refactor works inside inner-class bodies.

## Stub Rendering

For each owed abstract method, render a concrete method mirroring the base
`FunctionNode`:

- **Signature** — name, parameters (with names), parameter and return types via
  `GDScriptRefactorTypes::render_annotatable_type` where the base annotated them
  (untyped params render bare, per the helper's "only annotate hard, non-Variant
  types" rule). Default values reproduced from the base `ParameterNode` defaults
  when present.
- **`static`** preserved if the base declared it. Coroutine/`async` is not
  auto-applied — the override decides.
- **Body** — `push_error("Not implemented: <name>")` followed by a return:
  - Hard return type with a clean literal default → `return <literal>` (`0`,
    `0.0`, `""`, `false`, `[]`, `{}`).
  - Object / custom-class return type (no clean literal) → no return statement
    (just `push_error` + `pass`), intentionally surfacing a strict-typing
    "not all paths return" error the developer must resolve.
  - Void return → `push_error(...)` only.
- **Indentation** matches the target class body (file tab/space convention and
  inner-class nesting depth). A blank line separates each generated method.

Example:

```gdscript
func _compute_total() -> int:
    push_error("Not implemented: _compute_total")
    return 0
```

## Edit Production & Insertion

- **Insertion point** — end of the target class body, after the last member
  (computed from the `ClassNode` member spans, not raw file end, so it lands
  correctly for inner classes and trailing comments).
- **Output** — a single `RefactorTextEdit` (zero-width insertion range at end of
  class) whose `new_text` is the joined stub block prefixed with a separating
  blank line, wrapped in one `RefactorFileEdit` for the current file.
  Single-file only.
- **`expected_text` guard** — set per the existing edit convention so a stale
  buffer is detected before applying.
- Returned through the normal `RefactorResult`; the editor applies it as one
  undoable action and the LSP returns it as a workspace edit. No special caret
  placement after insertion.
- **Warning channel** — if an owed method resolved from a base whose source
  could not be fully analyzed, surface a non-fatal `RefactorResult::warning`
  rather than failing.

## Testing

- **`modules/gdscript/tests/test_refactor.h`** — cases mirroring existing
  refactor tests:
  - single abstract method; multiple methods
  - multi-level inheritance
  - partially-implemented base (intermediate concrete override excluded)
  - typed vs untyped parameters
  - void vs typed return
  - object-return → `push_error` + `pass`
  - static method preserved
  - inner class
  - abstract target class → disabled
  - no missing methods → disabled
- **`modules/gdscript/tests/scripts/`** — paired `.gd` fixtures with expected
  output where the runner expects them, regenerated via
  `--gdscript-generate-tests`.
- **`modules/gdscript/tests/test_lsp.h`** — the refactor surfaces as an available
  code action and produces the expected workspace edit over the LSP path.
