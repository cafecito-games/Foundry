# Iterative fixpoint inference ordering

**Issue:** cafecito-games/godot#32 (parent epic #29 — GDScript Migration Wizard)
**Date:** 2026-06-24

## Goal

Run Add Type Annotation repeatedly across a set of scripts until a pass produces
no new types, so that typing one declaration unlocks dependent ones. Typing a
leaf function's return type lets a caller's `var x = leaf()` be inferred, which
unlocks the next layer. A single pass under-types significantly because Add Type
Annotation only writes what the analyzer already resolves; re-analysis after each
applied round exposes new candidates. This is pure orchestration — no new
refactor primitive.

## Background

The Add Type Annotation refactor already resolves and writes concrete types for
untyped declarations from the analyzer's resolved `DataType`. Issue #30 (closed)
made candidate finding callable headlessly:
`GDScriptRefactoring::find_candidates(ctx, RefactorKind::ADD_TYPE_ANNOTATION)`
returns every candidate and its `RefactorTextEdit`s for a file with no caret.
`GDScriptRefactorEdits::apply()` applies a set of edits to a source string.

**Key constraint — cross-file analysis reads from disk only.** When the refactor
pipeline analyzes a script that depends on another, it resolves the dependency by
parsing it from disk: `RefactorParseResultProvider::get_parse_result()`
intentionally parses non-active scripts from disk rather than consulting LSP
unsaved buffers or the protocol cache
(`modules/gdscript/editor/gdscript_refactoring.cpp:163`), and
`GDScriptCache::get_source_code()` is disk-backed with no in-memory override.
Consequence: a caller file only "sees" a dependency's freshly-inferred return
type after that dependency's annotation has been **written to disk and
re-analyzed**. Within a single file, one `analyze()` resolves the whole tree in a
single pass, so the fixpoint loop's value is fundamentally a cross-file
phenomenon.

This sub-issue's siblings #33 (batch candidate collection) and #34 (post-edit
verification harness) are still open. This design keeps #32 self-contained on
primitives that already ship: `find_candidates` serves as batch collection and a
re-parse + `GDScriptAnalyzer::analyze()` step serves as verification, both used
inline. #33/#34 can later extract and formalize those concerns as named,
reusable components without changing this orchestrator's public contract.

## Scope

In scope:
- A new headless orchestrator that drives Add Type Annotation to a fixpoint over
  a caller-supplied set of script paths.
- Disk-based iteration with `GDScriptCache` invalidation so dependents re-read
  edited files on the next pass.
- Per-file post-edit verification (re-parse + analyze) that rejects a file's
  batch if it introduces new analyzer errors versus baseline.
- Before/after reporting per changed file and an honest count of skipped
  candidates.
- Cross-file fixture tests proving convergence beyond the leaf, plus a cycle
  fixture proving termination.

Out of scope:
- Explicit dependency graph / topological ordering (recorded as a future
  performance optimization).
- Editor UI, preview, and atomic undo wiring (the wizard, #41/#42, consumes this
  engine and owns persistence/undo).
- Enabling the strict-mode project settings.
- Per-candidate (finer than per-file) verification — deferred to #34.

## Design decisions

### Ordering strategy: iterate-to-fixpoint, ordering is emergent

No explicit dependency graph or topological sort. The loop collects candidates
across the working set, applies, verifies, re-collects, and stops when a pass
produces no new types or an iteration bound is hit. Leaves-first ordering emerges
naturally: only leaf return types are resolvable in pass 1; their callers become
resolvable in pass 2; and so on.

This resolves both items the issue flagged as "needs design":

- **Cycle handling:** none needed. With no topological sort there is no cycle
  problem — a cyclic group simply stops producing new types and the loop
  terminates.
- **Iteration bound / convergence:** the bound is `(candidate count in the first
  pass) + 1`, with a hard safety ceiling. Each productive pass types at least one
  declaration, so termination is guaranteed.

### Acceptance fixture: cross-file (separate scripts on disk)

The canonical demonstration uses three separate scripts (A's `var x =
B.relay()` → B's `return C.value()` → C's leaf returning a literal). Each layer
must be written and re-analyzed before the next sees it, so this genuinely
exercises the multi-pass loop; a single-file fixture could be fully typed in one
`analyze()` pass and prove nothing.

### Scope: self-contained on existing primitives

See Background. `find_candidates` is the collection step; re-parse + `analyze()`
is the verification step; both inline.

## Architecture

A new headless orchestrator, `GDScriptFixpointInference`, in
`modules/gdscript/editor/gdscript_fixpoint_inference.{h,cpp}`. It owns no new
refactor primitive — it composes existing ones:

1. `GDScriptRefactoring::find_candidates(ctx, ADD_TYPE_ANNOTATION)` — collect.
2. `GDScriptRefactorEdits::apply(source, edits, out)` — apply a file's enabled
   candidate edits.
3. Re-parse + `GDScriptAnalyzer::analyze()` — verify the edited file introduces
   no new errors versus its baseline.
4. Write the accepted source to disk and invalidate the file's `GDScriptCache`
   entry so dependents re-read it on the next pass.

The orchestrator is disk-based by necessity (see the cross-file constraint). It
captures every path's original source up front, so the net result is reportable
as before/after per file and is undoable by the wizard.

### Public API

```cpp
struct FixpointInferenceOptions {
    int max_iterations = 0; // 0 = auto: (first-pass candidate count) + 1
};

struct FixpointFileChange {
    String path;
    String before_source;
    String after_source;
    int annotations_applied = 0; // Counts applied annotations (candidates), not text edits.
};

struct FixpointSkipped {
    String path;
    int line = -1;
    String reason; // unprovable/disabled, or verification rejected the file's batch
};

struct FixpointInferenceResult {
    bool ok = false;
    String error_message;
    bool converged = false; // true => a pass produced nothing (real fixpoint);
                            // false => the iteration bound stopped the loop early.
    int iterations = 0;
    int total_annotations_applied = 0;
    Vector<FixpointFileChange> changed_files;
    Vector<FixpointSkipped> skipped;
};

class GDScriptFixpointInference {
public:
    static FixpointInferenceResult run(
        const Vector<String> &p_paths,
        const FixpointInferenceOptions &p_options = FixpointInferenceOptions());
};
```

Notes:
- The strict-mode toggles are intentionally absent: enabling strict checks is a
  later opt-in step (out of scope here), and `find_candidates` collects under
  default analyzer settings, so the verification gate must use the same settings
  to stay symmetric.
- Duplicate paths in `p_paths` are de-duplicated once; each unique file is
  processed and reported a single time.
- `annotations_applied` and `total_annotations_applied` count applied annotations
  (candidates), not the underlying text edits.

## Algorithm

```
capture original source for every path
bound = p_options.max_iterations > 0 ? p_options.max_iterations
                                     : (first-pass candidate count) + 1
        (clamped by a hard safety ceiling)
iteration = 0
loop:
    iteration += 1
    snapshot = read every path's current source from disk   // one consistent view
    pending = {}                                            // path -> (new_source, count)
    for each path:
        candidates = find_candidates({path, snapshot[path]}, ADD_TYPE_ANNOTATION)
        enabled = candidates where enabled
        if enabled is empty: continue
        new_source = apply(snapshot[path], enabled edits)
        if verify(path, new_source): // re-parse + analyze, no new errors vs baseline
            pending[path] = (new_source, enabled.size())
        else:
            record FixpointSkipped for the file; leave it unchanged
    applied_this_pass = 0
    for each (path, (new_source, count)) in pending:        // commit at pass end
        write new_source to disk
        invalidate GDScriptCache for path
        applied_this_pass += count
    if applied_this_pass == 0: break          // fixpoint reached
    if iteration >= bound: break              // safety
build changed_files from captured originals vs final on-disk source
return result
```

Snapshotting every source at the start of a pass and committing all writes at the
end means each iteration advances exactly one dependency layer, independent of the
order paths are supplied in. This makes the single-pass behavior deterministic
(a one-iteration run types only the leaves) and keeps `iterations` a meaningful
measure of dependency depth.

### Verification granularity

Per-file batch: apply all of a file's enabled candidates, verify once, and roll
back the whole file's batch on failure. The verification gate is simply that the
edited file still analyzes cleanly (`analyze() == OK`). Because `find_candidates`
only produces candidates for a file that already analyzes cleanly (it returns
`!ok` otherwise), this absolute gate is equivalent to "the applied annotations
introduced no new errors" — a file that does not analyze yields no candidates in
the first place, so there is no separate baseline to capture. Verification is a
safety net rather than the common path. Finer-grained, per-candidate verification
is #34's responsibility.

### Error handling

- A path that cannot be **read** is fatal: the run sets `ok = false` with a
  descriptive `error_message` and modifies no files. (Capturing originals up front
  requires every input to be readable.)
- A path that **reads but does not analyze** is not fatal: it simply yields no
  candidates and is left untouched while the rest of the working set proceeds.
  This keeps a single broken file from aborting a whole-project migration. The
  wizard surfaces such files through its own scan/report (#38/#41); honest
  per-file diagnostics from the orchestrator itself are deferred to that layer.
- A file whose batch fails verification is left untouched and the run continues
  (still `ok = true`). Rather than recording transient per-pass rejections (which
  may resolve on a later pass once a dependency is typed), `skipped` is built once
  from the **final** on-disk state: after the loop, each declaration that is still
  an enabled candidate (inferable but never landed — verification rejected it or
  the bound was hit) or a genuinely-unprovable disabled candidate is reported.
  Declarations that already carry an annotation (pre-existing or applied by this
  run) are excluded — `find_candidates` still returns them as disabled candidates,
  so they are filtered by their `disabled_reason`. (A structural "already
  annotated" flag on `RefactorCandidate` would be more robust than the reason
  match; a regression test guards the current behavior. Tracked as a follow-up.)
- Empty input is valid: the run does nothing and returns `ok = true` with no
  changes.
- The hard iteration ceiling guards against any non-convergence; reaching it is
  not an error but bounds the work.
- `run()` mutates files on disk and global `GDScriptCache` state; it is not safe
  to run concurrently with a live editing session or another `run()`.
- Disk writes use a temp-file + flush + rename so a failed or partial write
  cannot truncate or corrupt the target file.

### Limitation: per-file verification only (cross-file regressions deferred to #34)

Verification re-analyzes only the **edited** file. It does not check that the
file's *dependents* still analyze after an annotation is written. An annotation
that is locally valid can still introduce a new error in a caller — e.g. writing
`func value() -> int` makes a caller's `var x: String = value()` a type error,
where `var x: String = <Variant>` was previously allowed. In that case the
orchestrator commits the annotation and the dependent then fails to analyze on the
next pass (it simply yields no further candidates), leaving a regression on disk.

Closing this hole — re-analyzing inverse dependents and rolling back edits that
break them — is the job of the post-edit verification harness (#34). Until #34
lands, `ok = true` does **not** guarantee every dependent still analyzes, and the
wizard (#41/#42) must not apply `run()`'s output to a real project without that
harness gating it. This limitation is documented on the public API.

## Testing

New `TEST_CASE`s under `[Modules][GDScript][Refactor]` (or a dedicated
`[Fixpoint]` suite). Tests stage writable copies of the chain via the existing
`TemporaryScriptFile` RAII helper (`modules/gdscript/tests/test_refactor.h:186`),
which writes on construction and removes on destruction, so the orchestrator's
disk writes never touch committed fixtures.

- **Under-typing reproduced:** run with `max_iterations = 1`; assert only the
  leaf C is typed.
- **Acceptance criterion:** run to fixpoint; assert all three of A, B, C are
  typed, and that `iterations` reflects more than one pass.
- **Termination on cycles:** a fixture where A and B reference each other; assert
  the run terminates (does not hit the ceiling spuriously, does not crash) and
  types what is provable.
- **Edge cases:** empty input returns `ok = true` with no changes; an unreadable
  path returns `ok = false` with an `error_message` and modifies nothing; a file
  that reads but does not analyze is skipped while the rest of the set still
  converges.
- **Honest reporting:** assert `changed_files` before/after and
  `total_annotations_applied` match expectations.

## Future work

- Explicit dependency ordering (from `GDScriptCache`'s forward/inverse dependency
  maps) as a performance optimization to reduce redundant re-collection on large
  projects.
- Formalizing batch collection (#33) and the verification harness (#34) as
  reusable components this orchestrator delegates to.
