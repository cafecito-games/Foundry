# Post-edit verification harness (#34)

## Goal

Given a set of candidate annotation edits across a batch of Foundry Script files, apply
them, re-run the analyzer over the affected dependency closure, and keep only the
edits that introduce no new analyzer errors. Drop any edit (or minimal edit subset)
that increases the error count and report it with its diagnostics. Additionally,
provide a read-only strict-mode preview that re-analyzes under
`strict_null_checks` / `strict_dynamic_checks` and reports newly failing sites
without modifying anything.

This is the engine capability that makes batch migration safe. It closes the
dependent-verification gap that #32 explicitly deferred: the fixpoint inference only
re-analyzes each edited file in isolation, so an accepted edit in file A (for
example typing `func value() -> int`) can silently break a caller B. This harness
re-analyzes inverse dependents and rolls back conflicting changes.

## Background

- `GDScriptFixpointInference` (#32, `modules/foundry_script/editor/gdscript_fixpoint_inference.*`)
  drives Add Type Annotation to a fixpoint. Its `verify_source` re-parses and
  analyzes only the edited file. Its header documents the deferral: "Re-analyzing
  inverse dependents and rolling back conflicting changes is deferred to issue #34."
- `GDScriptBatchCandidates::collect` (#33) returns, per file, every Add Type
  Annotation candidate (`RefactorCandidate`, enabled and disabled, with `edits` and
  `disabled_reason`) across a batch of paths, read-only.
- `GDScriptRefactorEdits::apply(source, edits, out)` applies text edits to an
  in-memory source string, returning false on overlap/out-of-range.
- `GDScriptAnalyzer` exposes `set_strict_null_checks(bool)` /
  `set_strict_dynamic_checks(bool)`; both flags default false.
- `GDScriptCache` builds a real dependency graph as a side effect of compilation:
  `get_parser(path, status, err, owner)` populates `dependencies[owner].insert(path)`
  and `parser_inverse_dependencies[path].insert(owner)`. These maps are currently
  private with no public accessor.
- `GDScriptCache::get_source_code(path)` reads the file from disk via `FileAccess`.
  There is no hook to supply an alternate in-memory buffer for a path, so the
  analyzer always resolves cross-file references (`preload`, `extends`, global
  `class_name` / namespaced types) against on-disk content. A pure in-memory
  source-override substrate is tracked separately as #167; until it lands, cross-file
  verification stages edits to disk and restores them.

## Design principles (from epic #29)

- **Safe by default:** apply an edit only when re-analysis of the affected closure
  proves no new errors are introduced.
- **Honest reporting:** every dropped edit is reported with the diagnostics
  attributed to it; strict-mode violations are reported, never hidden.
- **Maximize coverage:** prefer keeping as many good edits as possible; do not
  discard a whole file's edits because one of them regressed.

## Components

### 1. `GDScriptCache` inverse-dependency accessor

A small, surgical addition to existing core infrastructure (the only change outside
the new files):

```cpp
// gdscript_cache.h (public)
static HashSet<String> get_inverse_dependencies(const String &p_path);
```

Returns a snapshot copy of `parser_inverse_dependencies[p_path]` (the set of files
that directly depend on `p_path`), or an empty set if none. Snapshot-by-value keeps
callers safe against concurrent cache mutation. The harness builds the transitive
closure itself from these direct edges.

### 2. `GDScriptVerificationHarness` (new)

`modules/foundry_script/editor/gdscript_verification_harness.{h,cpp}`, guarded by
`#ifdef TOOLS_ENABLED`, mirroring the structure and conventions of
`GDScriptFixpointInference`.

#### Input model

```cpp
// One candidate annotation edit to verify, addressed by file + declaration anchor.
// edits are the RefactorTextEdits for a single RefactorCandidate.
struct VerificationCandidate {
    String path;
    int line = -1;             // 0-based declaration anchor, for reporting.
    Vector<RefactorTextEdit> edits;
};

struct VerificationOptions {
    bool strict_null_checks = false;
    bool strict_dynamic_checks = false;
};
```

#### Verification result

```cpp
// A candidate that was dropped, with the reason and the diagnostics attributed to it.
struct VerificationRejected {
    String path;
    int line = -1;
    String reason;                 // e.g. "introduces N new analyzer error(s) in the affected set".
    Vector<String> diagnostics;    // Human-readable analyzer messages newly present.
};

struct VerificationResult {
    bool ok = false;               // false only on a fatal precondition (e.g. unreadable file).
    String error_message;          // Populated only when !ok.
    Vector<VerificationCandidate> accepted; // Edits proven to introduce no new errors.
    Vector<VerificationRejected> rejected;  // Edits dropped, with attributed diagnostics.
    int baseline_error_count = 0;  // Total analyzer errors across the affected set before any edit.
    int accepted_error_count = 0;  // Total after applying all accepted edits (== baseline).
};
```

#### Entry point

```cpp
class GDScriptVerificationHarness {
public:
    // Verifies p_candidates against the dependency closure of the files they touch.
    // p_universe bounds inverse-dependent discovery (typically the migration's full
    // path set); files outside it are not re-analyzed. Returns the accepted/rejected
    // split. Does NOT commit accepted edits; the caller decides whether to write them.
    // Verification stages candidate sources to disk and restores originals, so it is
    // NOT safe to call concurrently with a live editing session or another run.
    static VerificationResult verify(
            const Vector<VerificationCandidate> &p_candidates,
            const Vector<String> &p_universe,
            const VerificationOptions &p_options = VerificationOptions());
};
```

#### Verification primitive: stage / analyze / restore

The unit of measurement is the total analyzer-error count across the **affected
set**. Error count (not diagnostic identity) is the comparison key because applying
an edit shifts line numbers, making identity matching across before/after fragile;
count is robust and is the issue's stated criterion.

```
affected_set(E) = E ∪ transitive_inverse_dependents(E) ∩ universe
```

Transitive inverse dependents are computed by BFS over
`GDScriptCache::get_inverse_dependencies`, after a priming pass that calls
`get_full_script(path)` for each universe file so the cache's dependency graph is
populated.

`count_errors(affected_set, staged_sources, options)`:
1. For each file in the affected set, atomically write its staged source to disk
   (via `ScriptRefactorApply::write_file`, the shared safe writer) if it differs
   from the current on-disk content; remember which files were written.
2. Invalidate caches for every written file (`GDScriptCache::remove_parser` +
   `remove_script`).
3. For each file in the affected set, parse + analyze with a fresh
   `GDScriptParser` / `GDScriptAnalyzer` (strict flags applied from options) and sum
   `parser.get_errors().size()` over all of them. A file that fails to parse counts
   as a positive error contribution so a malformed edit is never treated as "clean".
4. Return the total.

Staging and restoring are paired by a scope guard so originals are always restored
(and caches re-invalidated) even on early return:
- `stage(sources)` writes the given sources and records originals.
- `restore()` writes originals back for every staged file and invalidates caches.

#### Attribution: optimistic batch + delta-debugging bisection

1. Compute `baseline = count_errors(affected_set, original_sources, options)`.
2. Build `all_applied`: original sources with **all** candidate edits applied
   per file (group candidates by path, apply that file's edit set via
   `GDScriptRefactorEdits::apply`). A candidate whose edits fail to apply
   (overlap/out-of-range) is rejected immediately with reason "edit could not be
   applied".
3. `combined = count_errors(affected_set, all_applied, options)`.
4. If `combined <= baseline`: accept every candidate. Done in one analysis.
5. Otherwise, run **ddmin** over the candidate list to find the minimal subset whose
   removal brings the count back to `<= baseline`:
   - Standard delta-debugging: partition the candidate set, test subsets/complements,
     narrow to the minimal offending set. Each test stages "originals + (candidate
     set under test) applied" and calls `count_errors`.
   - The offending candidates are rejected; for each, the attributed diagnostics are
     the analyzer messages present when only that candidate (over the accepted base)
     is applied but absent at baseline — computed with one final targeted
     `count_errors`-style diff that collects messages, not just the count.
   - The remaining candidates are accepted and verified together as a final
     confirmation pass (`<= baseline`).

This keeps the common case (no regression) at a single affected-set analysis, bounds
the regression case to O(log N) analyses, and retains the maximal set of good edits.

To bound worst-case cost (every analysis stages to disk and re-analyzes the whole
affected set), a hard ceiling caps how many candidates the bisection path will
attribute. When a regressing batch exceeds the ceiling, the harness logs that the
ceiling was hit and falls back to all-or-nothing rejection of that batch rather than
running ddmin over an unbounded candidate set.

### 3. Strict-mode preview

```cpp
struct StrictViolation {
    String path;
    int line = -1;
    int column = -1;
    String message;
};

struct StrictPreviewResult {
    bool ok = false;
    String error_message;
    bool strict_null_checks = false;   // Echo of which modes were previewed.
    bool strict_dynamic_checks = false;
    Vector<StrictViolation> violations; // Sites failing only under strict mode.
};

// Read-only. Analyzes each path twice (non-strict baseline, then with the requested
// strict flags) and reports diagnostics present only under strict mode. Writes
// nothing to disk.
static StrictPreviewResult preview_strict(
        const Vector<String> &p_paths,
        const VerificationOptions &p_options);
```

A diagnostic is a strict violation when it appears in the strict analysis but not in
the non-strict baseline for the same file. Because both passes analyze the same
unmodified source, line/column/message identity matching is reliable here (no line
shifts), so violations carry precise locations. The baseline is held as a multiset
(count keyed by line/column/message) and the strict pass consumes one baseline count
per match, so a diagnostic that occurs more often under strict mode than at baseline
still surfaces its additional occurrences as violations.

### 4. Wire into `GDScriptFixpointInference`

- `FixpointInferenceOptions` gains `bool strict_null_checks` / `bool
  strict_dynamic_checks` (default false), threaded into verification.
- Each pass, after collecting the accepted in-memory rewrites for the changed files,
  the fixpoint routes them through `GDScriptVerificationHarness::verify` with the run's
  full path set as the universe. Only candidates the harness accepts are committed to
  disk; rejected ones are recorded as skips (reason carried from the harness). This
  replaces the single-file `verify_source` so an accepted edit that breaks a dependent
  is now caught and rolled back instead of committed.
- The fixpoint's existing convergence / iteration-bound / honest-final-skip semantics
  are preserved; only the per-pass acceptance gate changes.

## Data flow

```
migration path set ─┐
                    ├─► BatchCandidates::collect ─► enabled candidates
                    │                                      │
                    │            (group as VerificationCandidate per declaration)
                    ▼                                      ▼
            VerificationHarness::verify(candidates, universe, options)
                    │
       prime cache (get_full_script per universe file)
                    │
       affected_set = touched ∪ inverse-dep closure
                    │
       baseline count ─► apply-all count ─► (ddmin if regressed)
                    │
            accepted / rejected split  ◄─ originals always restored
                    │
   fixpoint commits accepted edits to disk; records rejected as skips
```

## Error handling

- Unreadable universe/candidate file → `ok = false`, `error_message` set, no disk
  mutation. (Matches fixpoint precondition handling.)
- Edit fails to apply (overlap/out-of-range) → that candidate rejected with reason,
  batch continues.
- Staged write failure → treated as a fatal verification error for that run (cannot
  guarantee a clean measurement); originals restored, `ok = false`.
- A staged source that fails to parse → counted as errors so the edit is rejected,
  never silently accepted.
- Restore is guaranteed via scope guard on every exit path.

## Testing

C++ tests under `modules/foundry_script/tests/test_verification_harness.h`, registered in
the Foundry Script test suite, using on-disk temp files + `EditorFileSystem` (the
`test_fixpoint_inference.h` pattern, `TemporaryScriptFile`):

1. **Dependent-breaking edit dropped (acceptance criterion 1):** file A defines
   `func value(): return 42`; file B has `var x: String = A.new().value()`. The
   candidate that types `value() -> int` regresses B. Assert it is rejected and
   listed with a diagnostic, while an unrelated good candidate in the same batch is
   accepted.
2. **Strict-mode preview lists violations without modifying files (acceptance
   criterion 2):** a source clean under default analysis but failing under
   `strict_dynamic_checks` / `strict_null_checks`. Assert the violations are reported
   with locations and the file on disk is byte-for-byte unchanged.
3. **Bisection isolates one bad edit among good ones:** a batch where exactly one
   candidate regresses; assert only that candidate is rejected and all others
   accepted, and the final accepted set re-verifies clean.
4. **No regression → accept all in one pass:** a batch of independently-sound
   candidates; assert all accepted and `accepted_error_count == baseline_error_count`.
5. **Fixpoint integration:** a fixpoint run over A+B where typing A's return would
   break B; assert the run does not commit the breaking edit (B unchanged on disk)
   and reports it as a skip, while still applying safe annotations elsewhere.

Regenerate any `.out` fixtures only if behavior fixtures are added; these tests are
C++-driven and assert on disk state directly.

## Out of scope

- Pure in-memory cross-file verification via a cache source-override map — tracked as
  #167. This harness uses stage/restore until that substrate exists, then migrates to
  it with no API change.
- File discovery / ignore rules / ordering for the universe (#38); the harness
  operates on the explicit path list it is given.
- Editor-facing strict-mode preview UI (wizard sub-issues); this delivers the
  headless API the UI will consume.
```
