# Headless, caret-independent Add Type Annotation candidates

**Issue:** cafecito-games/godot#30 (parent epic #29 — Foundry Script Migration Wizard)
**Date:** 2026-06-23

## Goal

Make Add Type Annotation's candidate-finding and edit-computation callable
without a caret/selection or editor UI, so a batch driver (the migration
wizard) can request every candidate and its edits for a whole file headlessly.

The existing caret-driven entry points must keep working unchanged, delegating
to the same underlying logic.

## Background

Refactors are driven through `GDScriptRefactoring::get_available_refactors()` /
`prepare()` keyed on a `RefactorLocation` (caret/selection) in
`modules/foundry_script/editor/gdscript_refactoring.cpp`. The Add Type Annotation
logic walks the whole tree (`find_type_annotation_in_class` / `_in_function` /
`_in_suite`) but short-circuits the moment a declaration's span contains the
caret, via `caret_on_segment(...)` inside the leaf functions
`find_assignable_type_annotation` and `find_function_return_type_annotation`.

The reusable, refactor-agnostic grouped-preview / atomic-apply / single-undo
layer shipped in #51 (closed) — `editor/script/script_refactor_apply.*` plus the
`RefactorTextEdit` / `RefactorFileEdit` edit types. The headless result is built
on top of that facility: it reuses `RefactorTextEdit` so candidates feed the
apply layer directly. The migration's project-wide collection (#33) and
signature edits (#35) are separate sub-issues that will reuse the structures
introduced here.

## Scope

In scope:
- A new generic multi-candidate result structure.
- A new public method that returns all Add Type Annotation candidates and their
  `RefactorTextEdit`s for a file, independent of any caret position.
- Refactor the internal walk so the caret-driven path and the headless path
  share one traversal and one per-declaration edit computation.

Out of scope:
- Headless collection for the other refactor kinds (Rename, Extract Variable,
  Extract Method, Inline Variable). The new method returns a not-implemented
  error for them; they are clean extension points for sibling issues.
- Project-wide / multi-file collection (#33).
- Any UI, preview, or apply wiring (covered by #41 / #42 on top of #51).

## Public API

Added to `modules/foundry_script/editor/gdscript_refactoring.h`:

```cpp
// One independently-applicable refactor opportunity found without a caret.
struct RefactorCandidate {
    RefactorKind kind = RefactorKind::ADD_TYPE_ANNOTATION;
    bool enabled = false;            // false => found but not applicable
    String disabled_reason;          // populated when !enabled
    int line = -1;                   // 0-based anchor line of the declaration
    int column = -1;                 // 0-based anchor column
    Vector<RefactorTextEdit> edits;  // active-file edits (reuses the #51 edit type)
};

struct RefactorCandidatesResult {
    bool ok = false;
    String error_message;            // set only when the file can't be analyzed at all
    Vector<RefactorCandidate> candidates;
};

class GDScriptRefactoring {
public:
    // ... existing get_available_refactors / prepare ...
    static RefactorCandidatesResult find_candidates(const RefactorContext &p_context, RefactorKind p_kind);
};
```

### Behavior of `find_candidates`

- `ADD_TYPE_ANNOTATION`: returns one `RefactorCandidate` per annotatable
  declaration in the file. Both enabled and disabled candidates are reported
  (the epic's "honest reporting" principle — the wizard counts what it skips).
  An enabled candidate carries exactly the `edit` the caret-driven refactor
  would produce at that declaration. A disabled candidate carries the same
  `disabled_reason` the caret path would surface (e.g. "This declaration already
  has a type annotation.", "Cannot infer a type for this variable.").
  `anchor (line, column)` is the start of the declaration segment.
- If the file cannot be parsed/analyzed at all: `ok = false`,
  `error_message = "Cannot analyze this script."`, empty candidates — mirroring
  the existing single-candidate failure text.
- On success: `ok = true` even when `candidates` is empty (a valid file with no
  annotatable declarations).
- Other `RefactorKind` values: `ok = false`,
  `error_message = "Headless candidate collection is not implemented for this refactor."`.

## Internal design — unify the walk

The caret dependency lives only in the leaf functions. The change moves the
caret gate out of the leaves so one traversal serves both callers.

1. **Leaves compute unconditionally + report their span.**
   `find_assignable_type_annotation` and `find_function_return_type_annotation`
   compute the candidate (`matched` / `enabled` / `disabled_reason` / `edit`)
   regardless of the caret, and report the caret-test span
   `(line, declaration_start, declaration_end)`. The identifier-span resolution
   that currently takes `&p_location` passes `nullptr` in collect mode — the
   same path already used today for `var` / `const` / member declarations.

2. **Walk collects all candidates.**
   `find_type_annotation_in_class` / `_in_function` / `_in_suite` accumulate
   every candidate into a `Vector<TypeAnnotationCandidate>` (each carrying its
   span) instead of returning on the first match. The `TypeAnnotationCandidate`
   struct gains span fields (`line`, `caret_span_start`, `caret_span_end`).

3. **Caret path becomes a filter.**
   `find_type_annotation_candidate(...)` collects the list once, then selects
   the candidate whose span contains the caret via the existing
   `caret_on_segment`. If none match, it returns the default disabled candidate
   ("Place the caret on an untyped declaration with an inferred concrete
   type."). This reproduces the existing observable behavior exactly — same
   `matched` / `enabled` / `disabled_reason` / `edit` for any given caret — so
   `get_available_refactors`, `prepare`, and `prepare_type_annotation` are
   unaffected.

4. **Headless path reuses the list.**
   `find_candidates` collects the same list and maps each
   `TypeAnnotationCandidate` to a public `RefactorCandidate`, skipping the caret
   filter entirely.

This yields zero duplication of the traversal or the per-declaration edit
computation.

## Caching

The existing `type_annotation_cache`, keyed by `(path, source_hash, location)`,
serves the per-keystroke single-candidate path and is left unchanged.
`find_candidates` is location-independent and driven in deliberate batches, not
per-keystroke, so it is not cached initially (YAGNI). A file-keyed cache can be
added later if a profile shows it is needed.

## Testing

C++ unit tests in `modules/foundry_script/tests/test_refactor.h`:

- A `find_candidates` test helper.
- A fixture with multiple annotatable declarations — a member `var`, a local in
  a nested `if`/`for` suite, a function return type, and a parameter with a
  default value — asserting the full set of returned candidates and their edits,
  constructing **no** `RefactorLocation`.
- A disabled-candidate assertion (e.g. an already-typed declaration is reported
  with `enabled = false` and the expected reason).
- A regression assertion that an existing caret case produces an identical
  `edit` through both the caret-driven path and the matching candidate from
  `find_candidates`.
- An unsupported-kind assertion (`RENAME` → `ok = false` with the
  not-implemented message).

New fixtures live under `modules/foundry_script/tests/scripts/refactor/`.

Run: `./bin/godot.* --headless --test "[Modules][Foundry Script]"`.

## Acceptance criteria (from the issue)

- A headless test can request all Add Type Annotation candidates for a file
  without constructing a `RefactorLocation`. ✓ (`find_candidates`)
- Existing caret-driven Rename and Add Type Annotation flows are unchanged. ✓
  (caret path delegates to the same unified walk; Rename is untouched)
