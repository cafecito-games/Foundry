# Design: `match` exhaustiveness and missing-default warnings

Date: 2026-06-23
Status: Approved (design)
Area: `modules/gdscript/` (analyzer + warning system)

## Summary

Add two new GDScript compiler warnings that flag `match` statements which may
leave values unhandled:

1. **`NON_EXHAUSTIVE_MATCH`** — the matched expression has a *finite domain*
   (an `enum` type, or `bool`), one or more of its values are not covered by an
   unguarded pattern, and the statement has no unguarded wildcard `_` / bind
   branch. Defaults to **Warn**.
2. **`MATCH_WITHOUT_DEFAULT`** — the matched expression has a *non-finite
   domain* (`int`, `float`, `String`, `Array`, `Dictionary`, objects, `Variant`,
   etc.) and the statement has no unguarded wildcard `_` / bind branch. Defaults
   to **Ignore** (opt-in, since requiring a default branch on every such match is
   noisy in existing codebases).

Both are independently configurable to Ignore / Warn / Error through the existing
GDScript warning project settings mechanism.

## Motivation

`match` is frequently used to switch over an enum. When a new enumerator is added
later, existing `match` statements silently fall through with no branch executed,
which is a common source of bugs. Languages such as Rust and Swift catch this at
compile time via exhaustiveness checking. GDScript already resolves enough type
information in the analyzer to do the same, plus a softer "you have no default
branch" hint for the open-domain cases.

## Behavior

### `NON_EXHAUSTIVE_MATCH`

Applies when the matched expression's resolved type is a finite domain:

- An `enum` type (`DataType::ENUM`), or
- `bool` (treated as the finite domain `{ true, false }`).

Fires when **both**:

- At least one value of the domain is not covered by an unguarded pattern, and
- There is no unguarded wildcard `_` / bind branch (a "default").

A fully-covered enum, or a bool covering both `true` and `false`, produces **no
warning** even without a `_` branch — it is already exhaustive.

Message lists the missing values, for example:

```
The match statement does not cover all values of "Direction". Unhandled: EAST, WEST. Add the missing patterns or a "_" wildcard branch.
```

For `bool`, the type name is reported as `bool` and the unhandled value(s) as
`true` / `false`.

### `MATCH_WITHOUT_DEFAULT`

Applies when the matched expression's resolved type is *not* a finite domain.
Fires when there is no unguarded wildcard `_` / bind branch.

Message:

```
The match statement has no "_" wildcard branch; some values may go unhandled.
```

### Decision summary

| Matched type | Fully covered | Has unguarded `_` | Warning |
| --- | --- | --- | --- |
| enum / bool | yes | — | none |
| enum / bool | no | yes | none |
| enum / bool | no | no | `NON_EXHAUSTIVE_MATCH` |
| other | — | yes | none |
| other | — | no | `MATCH_WITHOUT_DEFAULT` |

## Coverage rules (correctness details)

These rules keep the analysis sound and free of false positives:

1. **Guards exclude a branch from coverage.** A branch with a `when` guard is not
   guaranteed to execute. Its patterns therefore do **not** count toward covering
   enum/bool values, and a guarded wildcard does **not** count as a default. The
   parser already sets `branch->has_wildcard = false` when a guard is present
   (`gdscript_parser.cpp`); the new check applies the same principle to value
   coverage by skipping any branch where `guard_body != nullptr`.
2. **Coverage is tracked by value, not by name.** Counted patterns are
   `PT_LITERAL` integer literals and `PT_EXPRESSION` patterns that reduce to a
   compile-time constant (e.g. `Direction.NORTH`, which reduces to its int value).
   An enumerator is covered if its integer value is present in the matched set.
   This correctly handles aliased enumerators that share a value (matching the
   value covers all names with that value). For `bool`, coverage tracks whether
   `true` and `false` literals are present.
3. **Bail out on uncertainty.** If a finite-domain match contains an *unguarded*
   pattern that is not a compile-time constant (e.g. a non-constant identifier or
   attribute access permitted by the existing pattern rules), the analyzer cannot
   prove what it covers. In that case suppress `NON_EXHAUSTIVE_MATCH` rather than
   risk a false positive. (`MATCH_WITHOUT_DEFAULT` does not apply to finite-domain
   types, so it is unaffected.)
4. **No branches** → no warning (nothing to analyze).
5. **Unresolved / unset test type** → no warning (cannot classify the domain).

## Implementation

### Site

A new `#ifdef DEBUG_ENABLED` helper, `check_match_exhaustiveness(...)`, is called
at the end of `GDScriptAnalyzer::resolve_match` in
`modules/gdscript/gdscript_analyzer.cpp` (declared in `gdscript_analyzer.h`). By
that point `p_match->test` has been reduced, so its `DataType` is available, and
all branches/patterns have been resolved (constants reduced).

Algorithm:

1. Read `match_test_type = p_match->test->get_datatype()`. If not set → return.
2. Determine `has_default`: any branch with `branch->has_wildcard == true`
   (already false for guarded branches).
3. Classify the domain:
   - `DataType::ENUM` → finite, values = `match_test_type.enum_values` (a
     `HashMap<StringName, int64_t>`).
   - `DataType::BUILTIN` with `builtin_type == Variant::BOOL` → finite,
     values = `{ false, true }`.
   - otherwise → non-finite.
4. **Non-finite domain:** if `!has_default`, emit `MATCH_WITHOUT_DEFAULT` on the
   match node and return.
5. **Finite domain:** if `has_default`, return (exhaustive via default).
   Otherwise walk unguarded branches collecting covered integer values from
   constant `PT_LITERAL` / `PT_EXPRESSION` patterns. If any unguarded,
   non-constant pattern is encountered → return (bail out, rule 3). Compute the
   set of domain values not covered; if non-empty, emit `NON_EXHAUSTIVE_MATCH`
   with the type name and a comma-joined list of the unhandled value names
   (enumerator names, or `true`/`false`).

Warnings are emitted with the existing
`parser->push_warning(p_match, code, symbols...)` API.

### Warning registration

- `modules/gdscript/gdscript_warning.h`
  - Add `NON_EXHAUSTIVE_MATCH` and `MATCH_WITHOUT_DEFAULT` to the `Code` enum
    before `WARNING_MAX`.
  - Add two entries to the `default_warning_levels[]` array in matching order:
    `WARN` for `NON_EXHAUSTIVE_MATCH`, `IGNORE` for `MATCH_WITHOUT_DEFAULT`.
  - The `static_assert(std_size(default_warning_levels) == WARNING_MAX)` continues
    to hold once both arrays are updated.
- `modules/gdscript/gdscript_warning.cpp`
  - Add the two string names to `names[]` (matching enum order):
    `"NON_EXHAUSTIVE_MATCH"`, `"MATCH_WITHOUT_DEFAULT"`.
  - Add two `case` blocks to `get_message()` producing the messages above, using
    the passed symbols.
- `modules/gdscript/gdscript.cpp`
  - No change. The existing `for (… i < WARNING_MAX …)` loop auto-registers the
    project settings `debug/gdscript/warnings/non_exhaustive_match` and
    `debug/gdscript/warnings/match_without_default` with the `Ignore,Warn,Error`
    enum hint and the per-code default value.

### Documentation

The pre-commit doc checks expect the warning list to be in sync. Update the
relevant class reference XML where the warning settings are documented
(`doc/classes/ProjectSettings.xml` and/or `@GDScript` docs, matching how existing
warnings are listed). Run the doc dry-run via `pre-commit run --all-files`.

## Files touched

- `modules/gdscript/gdscript_warning.h` — enum codes + default levels + assert.
- `modules/gdscript/gdscript_warning.cpp` — names + messages.
- `modules/gdscript/gdscript_analyzer.h` — declare `check_match_exhaustiveness`.
- `modules/gdscript/gdscript_analyzer.cpp` — helper + call in `resolve_match`.
- `doc/classes/*.xml` — warning documentation sync.
- `modules/gdscript/tests/scripts/analyzer/warnings/` — new fixtures (below).

## Testing

GDScript analyzer warning fixtures are `.gd` + `.out` pairs under
`modules/gdscript/tests/scripts/analyzer/warnings/`. Add cases covering:

- Enum, partial coverage, no `_` → `NON_EXHAUSTIVE_MATCH` listing missing names.
- Enum, full coverage, no `_` → no warning.
- Enum, partial coverage, with `_` → no warning.
- Enum, partial coverage where the missing case is only covered by a **guarded**
  branch → `NON_EXHAUSTIVE_MATCH` (guard does not count).
- Enum, partial coverage with an unguarded **non-constant** pattern → no warning
  (bail-out rule 3).
- Aliased enumerators sharing a value → covered together.
- `bool`, covering only `true` → `NON_EXHAUSTIVE_MATCH` (unhandled: `false`).
- `bool`, covering both → no warning.
- Non-enum (`int` / `String`) without `_`, with the setting enabled →
  `MATCH_WITHOUT_DEFAULT`.
- Non-enum with `_` → no warning.

Regenerate expected output with:

```
./bin/godot.<platform>.editor.dev.<arch> --headless --gdscript-generate-tests modules/gdscript/tests/scripts
```

Run the suite with:

```
./bin/godot.<platform>.editor.dev.<arch> --headless --test --force-colors
```

No special fixture setup is needed for `MATCH_WITHOUT_DEFAULT` despite its Ignore
default: the GDScript test runner force-sets every warning level to "Warn" before
running fixtures (`tests/gdscript_test_runner.cpp`, the `WARNING_MAX` loop that
calls `set_setting(setting_path, WARN)`), so the warning will appear in the
expected `.out`. This is the same reason existing Ignore-default warnings (e.g.
`MISSING_AWAIT`) have working fixtures.

## Out of scope

- Exhaustiveness over flag-style (bit-OR'd) enums — the value set of a flags enum
  is not a simple membership list; treated as a normal enum here (only declared
  enumerators are considered covered).
- Reachability of branches after a wildcard (already handled elsewhere).
- Quick-fix / editor code action to insert the missing branches.
