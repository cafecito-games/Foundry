# Numeric Constraints and User-Defined Type Unions Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add transparent user-defined type aliases/unions, a closed scalar `Number` union, union generic bounds, numeric promotion for valid bounded operations, and flow-sensitive narrowing in Foundry Script.

**Architecture:** Parse aliases and union members into a canonical static type-set representation carried by `FSParser::DataType`. Resolve aliases before compatibility, bound, inference, operator, and tooling decisions; retain the concrete runtime Variant representation and existing generic erasure. Implement numeric behavior through the existing `NumericType`/`FSNumericConversion` promotion machinery and reuse existing `is` flow analysis for narrowing.

**Tech Stack:** C++ parser/analyzer (`modules/foundry_script`), Foundry Script fixtures, doctest, grammar/LSP/formatter fixtures, `scripts/agent_build.py`.

---

## File map and ownership

- `modules/foundry_script/fs_parser.h`: add alias declaration/type-union AST and `DataType` union metadata.
- `modules/foundry_script/fs_parser.cpp`: parse top-level/inner alias declarations, parse `|` in type contexts, resolve and print aliases.
- `modules/foundry_script/fs_analyzer.h`: declare alias registry, union normalization, compatibility, narrowing, and operator helpers.
- `modules/foundry_script/fs_analyzer.cpp`: implement alias resolution, union bound checks, control-flow refinement, and bounded operator validation.
- `modules/foundry_script/fs_type.h` / `fs_type.cpp`: expose reusable set-wise numeric operation/promotion helpers without changing existing scalar promotion behavior.
- `modules/foundry_script/GRAMMAR.md`: document alias declarations, union grammar, precedence/context rules, and static/runtime semantics.
- `modules/foundry_script/tests/scripts/analyzer/features/`: passing alias/union/bound/arithmetic fixtures.
- `modules/foundry_script/tests/scripts/analyzer/errors/`: negative fixtures with exact diagnostics.
- `modules/foundry_script/tests/scripts/runtime/features/`: runtime fixtures proving aliases erase to existing concrete values.
- `modules/foundry_script/tests/scripts/lsp/` and `tests/scripts/format/`: presentation and formatter coverage.
- `tests/test_integer_promotion.h` and/or `modules/foundry_script/tests/test_integer_promotion.h`: focused C++ tests for set-wise numeric result calculation.

## Task 1: Add the alias and union AST/data model

**Files:**
- Modify: `modules/foundry_script/fs_parser.h`
- Modify: `modules/foundry_script/fs_parser.cpp`
- Test: `modules/foundry_script/tests/scripts/parser/features/type_alias_union.fs`

- [ ] **Step 1: Write parser fixtures for declarations and context-sensitive `|`**

Create a fixture containing `type Unsigned = uint | ulong`, aliases nested in generic arguments, and an expression using bitwise `|`. The parser fixture must assert the tree/diagnostic output through the existing parser test convention rather than inspecting source text.

- [ ] **Step 2: Run the focused parser case and verify it fails**

Run:

```sh
./bin/foundry.linuxbsd.editor.dev.x86_64 --headless test run --case "*type_alias_union*" --force-colors
```

Expected: the new declaration is rejected or parsed as an unknown declaration before implementation.

- [ ] **Step 3: Add AST nodes and `DataType::UNION`/alias metadata**

Add a declaration node that stores the alias name and one or more `TypeNode` members. Add a union representation to `FSParser::DataType` with canonical member storage, equality, substitution, nullable propagation, `to_string`, and serialization/property-info behavior explicitly preserving the existing runtime carrier erasure. Keep aliases transparent after resolution; do not add a runtime Variant type.

- [ ] **Step 4: Parse aliases and type-context unions**

Register aliases wherever a script-level type declaration is legal. Extend `parse_type()` so `|` is consumed only while parsing a type expression; leave expression precedence and bitwise OR unchanged. Reject an empty union and emit a source-located diagnostic for an invalid member.

- [ ] **Step 5: Run parser and formatting checks**

Run:

```sh
./bin/foundry.linuxbsd.editor.dev.x86_64 --headless test run --case "*Parser*" --force-colors
./bin/foundry.linuxbsd.editor.dev.x86_64 --headless test generate-format-fixtures modules/foundry_script/tests/scripts/format
```

Expected: existing parser cases remain green and the alias fixture parses with stable formatting.

- [ ] **Step 6: Commit**

```sh
git add modules/foundry_script/fs_parser.h modules/foundry_script/fs_parser.cpp modules/foundry_script/tests/scripts/parser/features/type_alias_union.fs
git commit -m "feat(foundry_script): parse static type unions"
```

## Task 2: Resolve aliases and normalize unions

**Files:**
- Modify: `modules/foundry_script/fs_analyzer.h`
- Modify: `modules/foundry_script/fs_analyzer.cpp`
- Modify: `modules/foundry_script/fs_parser.cpp`
- Test: `modules/foundry_script/tests/scripts/analyzer/features/type_alias_union.fs`
- Test: `modules/foundry_script/tests/scripts/analyzer/errors/type_alias_union_invalid.fs`

- [ ] **Step 1: Write executable analyzer fixtures**

Cover aliases in variables, parameters, returns, `Array[Alias]`, `Dictionary[String, Alias]`, callable signatures, nested aliases, duplicate members, and alias cycles/unknown names. Include a user-defined class union to prove unions are not runtime wrappers.

- [ ] **Step 2: Run the focused analyzer cases and verify failures**

Run:

```sh
./bin/foundry.linuxbsd.editor.dev.x86_64 --headless test run --case "*type_alias_union*" --force-colors
```

Expected: alias fixtures fail resolution or compatibility checks before the registry exists.

- [ ] **Step 3: Implement declaration-scope alias registration**

Register aliases with the same lexical/import visibility rules as existing named types. Resolve alias members lazily with cycle detection, preserving source locations for diagnostics. Normalize recursively by expanding aliases, flattening unions, removing duplicates, and canonicalizing member order.

- [ ] **Step 4: Integrate normalized unions into compatibility**

Update `is_type_compatible`, assignment checks, return checks, container element checks, and callable signature checks so a source satisfies a union target when it satisfies one member. Ensure a union source is accepted by a target only when every possible member is compatible, unless existing flow narrowing has removed alternatives.

- [ ] **Step 5: Verify runtime erasure**

Add a runtime fixture that passes `int`, `uint`, and a class through alias-typed parameters and returns them unchanged. Assert observable values and concrete operations, not serialized source text or internal implementation fields.

- [ ] **Step 6: Regenerate fixtures and commit**

```sh
./bin/foundry.linuxbsd.editor.dev.x86_64 --headless test generate-fixtures modules/foundry_script/tests/scripts
./bin/foundry.linuxbsd.editor.dev.x86_64 --headless test run --case "*type_alias_union*" --force-colors
git add modules/foundry_script/fs_analyzer.h modules/foundry_script/fs_analyzer.cpp modules/foundry_script/fs_parser.cpp modules/foundry_script/tests/scripts/analyzer modules/foundry_script/tests/scripts/runtime
git commit -m "feat(foundry_script): resolve transparent type aliases"
```

## Task 3: Add `Number` and union generic-bound enforcement

**Files:**
- Modify: `modules/foundry_script/fs_analyzer.cpp`
- Modify: `modules/foundry_script/fs_type.cpp`
- Test: `modules/foundry_script/tests/scripts/analyzer/features/generic_union_bounds.fs`
- Test: `modules/foundry_script/tests/scripts/analyzer/errors/generic_union_bound_violation.fs`

- [ ] **Step 1: Write bound fixtures**

Declare `type Number = int | uint | long | ulong | float` and a generic method/class with `[T: Number]`. Instantiate it with each scalar, with a non-scalar, with `Variant`, and with a type parameter whose own bound is either sufficient or insufficient.

- [ ] **Step 2: Run the bound cases before implementation**

```sh
./bin/foundry.linuxbsd.editor.dev.x86_64 --headless test run --case "*generic_union_bound*" --force-colors
```

Expected: valid scalar applications are rejected because union bounds are not yet recognized; invalid applications must retain stable diagnostics after implementation.

- [ ] **Step 3: Implement “any member” bound satisfaction**

Extend `type_argument_satisfies_bound()` so a concrete argument satisfies a union bound if one normalized member accepts it. For a type-parameter argument, recurse through its declared bound and reject unbounded parameters against a concrete union. Preserve strict-null and handle-layer behavior.

- [ ] **Step 4: Integrate aliases with generic inference and substitutions**

Ensure inferred type arguments are checked against normalized aliases/unions, explicit type arguments use the same path, inherited generic substitutions retain union metadata, and raw generic behavior remains unchanged. Diagnostic text must name the alias and/or normalized union in a deterministic form.

- [ ] **Step 5: Add closed built-in `Number` handling**

Provide `Number` as a reserved compiler-defined alias or equivalent built-in type-set that expands exactly to the five scalar numeric types. Reject user-defined nominal types, vectors, strings, and `Variant` as `Number` arguments.

- [ ] **Step 6: Run focused tests and commit**

```sh
./bin/foundry.linuxbsd.editor.dev.x86_64 --headless test run --case "*generic_union_bound*" --force-colors
git add modules/foundry_script/fs_analyzer.cpp modules/foundry_script/fs_type.cpp modules/foundry_script/tests/scripts/analyzer
git commit -m "feat(foundry_script): enforce union generic bounds"
```

## Task 4: Support numeric operations over bounded values

**Files:**
- Modify: `modules/foundry_script/fs_type.h`
- Modify: `modules/foundry_script/fs_type.cpp`
- Modify: `modules/foundry_script/fs_analyzer.cpp`
- Test: `modules/foundry_script/tests/test_integer_promotion.h`
- Test: `modules/foundry_script/tests/scripts/analyzer/features/generic_numeric_operations.fs`
- Test: `modules/foundry_script/tests/scripts/analyzer/errors/generic_numeric_operation_invalid.fs`

- [ ] **Step 1: Write the operation matrix tests**

Add C++ tests for the existing scalar promotion matrix lifted over member sets: a pair is valid when each permitted combination has an operator result and the result set has a common representable type. Include valid same-carrier integer pairs, float combinations, mixed signed/unsigned cases, and an invalid pair with no common result.

- [ ] **Step 2: Run the C++ tests and verify the new cases fail**

```sh
./bin/foundry.linuxbsd.editor.dev.x86_64 --headless test run --case "*IntegerPromotion*" --force-colors
```

Expected: existing scalar tests pass; new member-set cases fail until the helper is implemented.

- [ ] **Step 3: Implement set-wise numeric result calculation**

Add a helper that enumerates normalized union members, invokes the existing validated operator/promotion logic for each pair, and joins compatible result descriptors. Do not weaken the existing scalar rules or silently introduce mixed-carrier conversions.

- [ ] **Step 4: Apply the helper to generic binary expressions**

When an operand is a type parameter or union with a numeric bound, use the set-wise helper. Permit direct arithmetic only when all permitted combinations have a valid common result; otherwise emit an actionable diagnostic requiring narrowing or explicit conversion.

- [ ] **Step 5: Add Foundry Script behavior fixtures**

Test direct arithmetic for a bound whose combinations are valid, rejection for an unconstrained mixed set, and an `add[X: Number, Y: Number]` implementation that narrows/converts cases and returns `long` according to its own policy.

- [ ] **Step 6: Run focused tests and commit**

```sh
./bin/foundry.linuxbsd.editor.dev.x86_64 --headless test run --case "*IntegerPromotion*" --force-colors
./bin/foundry.linuxbsd.editor.dev.x86_64 --headless test run --case "*generic_numeric_operations*" --force-colors
git add modules/foundry_script/fs_type.h modules/foundry_script/fs_type.cpp modules/foundry_script/fs_analyzer.cpp tests/test_integer_promotion.h modules/foundry_script/tests/scripts/analyzer
git commit -m "feat(foundry_script): check numeric union operations"
```

## Task 5: Add flow-sensitive narrowing for unions and bounded generics

**Files:**
- Modify: `modules/foundry_script/fs_analyzer.cpp`
- Test: `modules/foundry_script/tests/scripts/analyzer/features/type_union_narrowing.fs`
- Test: `modules/foundry_script/tests/scripts/analyzer/errors/type_union_narrowing_invalid.fs`

- [ ] **Step 1: Write narrowing fixtures**

Cover `if value is int`, `is not`, chained tests, nested aliases, generic `[T: Number]` values, nullable union members, and narrowing across the existing branch/merge rules. Include a negative case where an operation is attempted before narrowing.

- [ ] **Step 2: Run the narrowing fixture before implementation**

```sh
./bin/foundry.linuxbsd.editor.dev.x86_64 --headless test run --case "*type_union_narrowing*" --force-colors
```

Expected: branch-local operations remain opaque or report invalid operations before union refinement is integrated.

- [ ] **Step 3: Refine the existing type-test flow state**

When the tested type is a normalized union member, replace the subject's branch-local type with the intersection of the current alternatives and the tested member. For `is not`, remove the tested member. Preserve nullability and existing tagged-union case binding behavior.

- [ ] **Step 4: Handle generic type parameters**

For a bounded type parameter, narrow its effective member set without discarding the original type-parameter identity needed for generic return/substitution checks. At branch joins, compute the union of surviving alternatives and avoid claiming a narrower type after divergent paths merge.

- [ ] **Step 5: Run focused tests and commit**

```sh
./bin/foundry.linuxbsd.editor.dev.x86_64 --headless test run --case "*type_union_narrowing*" --force-colors
git add modules/foundry_script/fs_analyzer.cpp modules/foundry_script/tests/scripts/analyzer
git commit -m "feat(foundry_script): narrow union values by type tests"
```

## Task 6: Update grammar, formatter, LSP, and refactoring surfaces

**Files:**
- Modify: `modules/foundry_script/GRAMMAR.md`
- Modify: relevant presentation/refactor code discovered by existing generic-type tooling tests
- Test: `modules/foundry_script/tests/scripts/lsp/type_alias_union_presentation.fs`
- Test: `modules/foundry_script/tests/scripts/format/type_alias_union/`

- [ ] **Step 1: Add grammar and tooling fixtures first**

Specify expected presentation for alias names, expanded union members where diagnostics require them, and canonical formatting for `type Name = A | B`. Include an expression-level bitwise OR fixture to prevent precedence regressions.

- [ ] **Step 2: Run LSP and formatter cases before implementation**

```sh
./bin/foundry.linuxbsd.editor.dev.x86_64 --headless test run --case "*type_alias_union*" --force-colors
```

Expected: parser/analyzer may succeed after earlier tasks, but LSP/formatter output is missing or unstable.

- [ ] **Step 3: Document the normative grammar**

Add productions for type aliases and type unions, state that `|` is contextual, document flattening/alias transparency, `Number`, generic-bound semantics, narrowing, runtime erasure, and operator restrictions.

- [ ] **Step 4: Update presentation/completion/refactoring**

Expose alias declarations and union members in symbol/completion results, preserve alias names in source-oriented presentation, and make rename/reference operations resolve aliases without treating their members as declarations of the alias itself.

- [ ] **Step 5: Regenerate formatter fixtures and commit**

```sh
./bin/foundry.linuxbsd.editor.dev.x86_64 --headless test generate-format-fixtures modules/foundry_script/tests/scripts/format
./bin/foundry.linuxbsd.editor.dev.x86_64 --headless test run --case "*LSP*" --force-colors
git add modules/foundry_script/GRAMMAR.md modules/foundry_script/tests/scripts/lsp modules/foundry_script/tests/scripts/format
git commit -m "docs(foundry_script): document type aliases and unions"
```

## Task 7: Full regression validation and handoff

**Files:**
- Modify: only fixture `.out`/`expected.fs` files intentionally regenerated by prior tasks.

- [ ] **Step 1: Run focused Foundry Script suites**

```sh
python3 scripts/agent_build.py --backend ninja --test --case "*Parser*"
python3 scripts/agent_build.py --backend ninja --test --case "*Generic*"
python3 scripts/agent_build.py --backend ninja --test --case "*IntegerPromotion*"
```

Expected: all focused cases pass with no unrelated fixture changes.

- [ ] **Step 2: Run native strict validation**

```sh
python3 scripts/agent_build.py --compiler-cache ccache --jobs 4 --test
```

Expected: strict `dev_mode=yes dev_build=yes tests=yes` build completes and the full test suite reports doctest success. Record any expected cleanup leak summary separately from the doctest result.

- [ ] **Step 3: Run repository hygiene checks**

```sh
git diff --check
git grep -nE '\.split\("(void|bool|String|private fun|const val|def )' -- '*.py' || true
git status --short
```

Expected: no whitespace errors, no new source-signature tests, and only intentional tracked changes.

- [ ] **Step 4: Commit final fixture updates if needed**

```sh
git add modules/foundry_script/tests/scripts
git commit -m "test(foundry_script): cover numeric type unions"
```

- [ ] **Step 5: Prepare review/merge handoff**

Summarize the exact commits, focused/full test commands, any limitations (especially mixed numeric conversion policy), and the issue/epic links. Do not claim completion until strict validation evidence is available.
