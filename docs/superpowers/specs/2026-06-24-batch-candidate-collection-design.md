# Batch annotation-candidate collection

**Issue:** cafecito-games/godot#33 (parent epic #29 — Foundry Script Migration Wizard)
**Date:** 2026-06-24

## Goal

Return every Add Type Annotation candidate across a batch of files in one call,
without modifying anything, so the migration wizard can present and count the
whole candidate set before applying any change.

## Background

Two layers already exist:

- **Per-file collection (#30, shipped).**
  `GDScriptRefactoring::find_candidates(context, kind)` returns every candidate
  in one file — enabled and disabled, each with its `RefactorTextEdit`s and
  `disabled_reason` — independent of a caret.
- **Multi-file application (#32, shipped).**
  `GDScriptFixpointInference::run(paths)` iterates files and *applies* Add Type
  Annotation to a fixpoint, mutating files on disk.

Missing is the read-only multi-file counterpart: collect every candidate across
a set of files at once, mutate nothing. That is the preview primitive the
wizard's reporting (#41) and any pre-apply summary consume. The #30 design
explicitly scoped "project-wide / multi-file collection (#33)" out of its own
work and into this issue, reusing its `RefactorCandidate` structures.

## Scope

In scope:

- A new `GDScriptBatchCandidates::collect(paths, kind)` that reads each path from
  disk, runs `find_candidates`, and returns per-file candidate sets plus batch
  totals.
- Honest, non-fatal per-file error reporting: an unreadable or unanalyzable file
  is recorded and skipped; the rest of the batch still runs.
- Duplicate input paths collapsed (first-seen order), so a file is never read or
  reported twice — matching the fixpoint runner's behavior.

Out of scope:

- **File discovery, ignore rules, and ordering (#38).** `collect` operates on
  the explicit path list it is given; the caller supplies the work list.
- **Verification / application (#34, #32).** This layer is strictly read-only.
- Headless collection for refactor kinds other than Add Type Annotation. The
  method returns a fatal not-implemented error for them, mirroring
  `find_candidates`.

## Public API

New `modules/foundry_script/editor/gdscript_batch_candidates.h`:

```cpp
struct BatchFileCandidates {
    String path;
    bool ok = false;            // false => file could not be read or analyzed
    String error_message;       // populated when !ok
    Vector<RefactorCandidate> candidates; // enabled and disabled (honest reporting)
};

struct BatchCandidatesResult {
    bool ok = false;            // false only on a fatal precondition (unsupported kind)
    String error_message;
    Vector<BatchFileCandidates> files;  // one per unique path, first-seen order
    int total_candidates = 0;
    int enabled_candidates = 0;
};

class GDScriptBatchCandidates {
public:
    static BatchCandidatesResult collect(
            const Vector<String> &p_paths,
            RefactorKind p_kind = RefactorKind::ADD_TYPE_ANNOTATION);
};
```

### Behavior

- Unsupported `p_kind`: `ok = false`, standard
  `"Headless candidate collection is not implemented for this refactor."`
  message, empty `files` (no per-file work is done).
- Per path: read from disk. On read failure, the file entry gets `ok = false`
  and a `"Cannot read '<path>'."` message and contributes nothing to the totals.
  On analyzer failure, the entry gets `ok = false` and `find_candidates`'
  `error_message`. Otherwise `ok = true` and the candidates are tallied.
- Top-level `ok = true` whenever the batch ran, even if some files failed or the
  list was empty; per-file `ok` flags carry individual failures.

## Tests

`modules/foundry_script/tests/test_batch_candidates.h` covers: multi-file collection
with enabled + disabled candidates and correct totals; duplicate-path collapse;
a non-fatal unreadable file alongside a good one; unsupported-kind rejection; and
parity between a single file's batch entry and `find_candidates` (same count,
edits, and reasons).
