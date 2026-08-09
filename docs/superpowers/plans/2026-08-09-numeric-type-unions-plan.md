# Numeric Constraints and User-Defined Type Unions Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add transparent file-local type aliases and unions, a closed scalar `Number` union, union generic bounds, set-wise numeric operator checking, and flow-sensitive narrowing in Foundry Script, per `docs/superpowers/specs/2026-08-09-numeric-type-unions-design.md`.

**Architecture:** Parse aliases and union members into a canonical type-set carried by a new `FSParser::DataType` kind. Normalize (expand, flatten, hoist nullability, dedupe, canonicalize) before every compatibility, bound, inference, operator, and tooling decision. Multi-member unions erase to untyped at runtime; single-member aliases stay fully transparent. Numeric behavior routes through the existing `FSAnalyzer::get_operation_type` / `FSNumericConversion` machinery, and narrowing extends `FSAnalyzer::FlowFinalityContext`.

**Tech Stack:** C++ parser/analyzer (`modules/foundry_script`), Foundry Script corpus fixtures, doctest, formatter/LSP fixtures, `scripts/agent_build.py`.

---

## Test harness constraints — read before writing any fixture

These are properties of the harness, verified in code. Getting them wrong wastes a build cycle each time.

- **The whole script corpus is one doctest case.** `modules/foundry_script/tests/fs_test_runner_suite.h:70-95` registers `TEST_CASE("Script compilation and runtime")` and a second case that re-runs the identical corpus through compiled bytecode. Fixture filenames never appear in a case name.
- **Therefore `--case "*type_alias_union*"` matches nothing, and a filter matching nothing is a hard failure**, not an empty pass: `tests/test_main.cpp:469-474` prints `The --case/--suite filter matched no tests; nothing was run.` and returns `EXIT_FAILURE`. Use:

  ```sh
  ./bin/foundry.* --headless test run --case "*Script compilation and runtime*" --print-filenames --force-colors
  ```

  `--print-filenames` (`fs_test_runner_suite.h:72`) is how you see which fixture is executing.
- **Every corpus `.fs` needs a paired `.out`.** `fs_test_runner.cpp:511` hard-fails discovery for a `.fs` with no `.out`, which fails the entire case, not just that fixture. Generate `.out` files with `test generate-fixtures`.
- **Four subtrees are excluded from the corpus runner** and have their own doctest cases: `completion`, `lsp`, `refactor`, `format` (`fs_test_runner.cpp:475`). `analyzer/**`, `parser/**`, `runtime/**` are the corpus.
- **`[Format] Idempotent over the script corpus`** (`test_format.h:1438`) sweeps *all* of `scripts/`, so any corpus fixture containing union syntax fails until the formatter handles it. This is why formatter support lands in Task 1, not last.
- **`[Format] Refuses to format unparsable error fixtures`** (`test_format.h:1460`) sweeps `analyzer/errors`, `runtime/errors`, `parser/errors`. A fixture under `analyzer/errors` must still *parse* cleanly. Parse-level negatives (empty union, malformed member, parameterized alias) belong in `parser/errors/`.
- **`--case` matches doctest case names, `--suite` matches suite names, and the two are OR'd** (`tests/test_case_filter.cpp:162-177`). Bracket tags embedded in a case-name string are part of the name, so `--case "*NumericTypes*"` works for `test_integer_promotion.h`; `--case "*IntegerPromotion*"` does not, because no case is named that.
- **Binary path:** use the `./bin/foundry.*` glob. This checkout builds `foundry.macos.*` locally and `foundry.linuxbsd.editor.dev.x86_64` in the cloud VM.

## File map and ownership

- `modules/foundry_script/fs_tokenizer.cpp`: contextual `type` recognition (no new hard keyword).
- `modules/foundry_script/fs_parser.h`: alias declaration node, union `DataType` kind and member storage, `operator==` arm (`fs_parser.h:361-434`).
- `modules/foundry_script/fs_parser.cpp`: alias declarations at file/class scope, `|` inside `parse_type()` (`fs_parser.cpp:6294-6576`), `@export` rejection (`fs_parser.cpp:7446-7521`).
- `modules/foundry_script/fs_parser_data_type.cpp`: normalization, `to_string`, `to_property_info` (`:660-679`), substitution.
- `modules/foundry_script/fs_analyzer.h` / `fs_analyzer.cpp`: alias registry, compatibility, bounds (`type_argument_satisfies_bound`, `:8870`), set-wise operators (`get_operation_type`, `:15736`).
- `modules/foundry_script/fs_analyzer_flow_finality.cpp`: narrowing, downward-closed removal, unreachable-test warning.
- `modules/foundry_script/fs_type.h` / `fs_type.cpp`: set-wise numeric helper beside `FSNumericConversion::promote_integer_pair` (`:311`).
- `modules/foundry_script/fs_warning.h`: new unreachable-type-test warning.
- `modules/foundry_script/fs_compiler.cpp`: union metadata in the rich `FSDataType` channel for the bytecode round trip.
- `modules/foundry_script/GRAMMAR.md`: normative grammar, updated in the same change as the parser.
- Fixtures: `tests/scripts/parser/{features,errors}/`, `tests/scripts/analyzer/{features,errors,warnings}/`, `tests/scripts/runtime/{features,errors}/`, `tests/scripts/format/<case>/{input.fs,expected.fs}`, `tests/scripts/lsp/`.
- `modules/foundry_script/tests/test_integer_promotion.h`: the only such file; there is **no** `tests/test_integer_promotion.h` at repo root. Its cases are named `[Modules][FoundryScript][NumericTypes] ...`.

---

## Task 1: Parser, data model, formatter, and grammar

Formatter and `GRAMMAR.md` land here rather than at the end, because the corpus formatter-idempotency sweep fails the moment a fixture contains `|` in a type, and because the repository requires grammar changes in the same change as parser changes.

**Files:**
- Modify: `fs_tokenizer.cpp`, `fs_parser.h`, `fs_parser.cpp`, `fs_parser_data_type.cpp`, formatter, `GRAMMAR.md`
- Test: `tests/scripts/parser/features/type_alias_union.fs` + `.out`
- Test: `tests/scripts/parser/errors/type_alias_union_invalid.fs` + `.out`
- Test: `tests/scripts/format/type_alias_union/{input.fs,expected.fs}`

- [ ] **Step 1: Write parser fixtures**

Cover `type Unsigned = uint | ulong`, a single-member alias, an alias used in a generic argument and a bound, a nullable member, an expression using bitwise `|`, and `var type = 5` proving `type` still works as an identifier. The error fixture covers an empty union, a malformed member, a parameterized alias, and an alias declared inside a function body. Assert through the fixture `.out` convention, never by inspecting source text.

- [ ] **Step 2: Run the corpus and confirm the new fixtures fail**

```sh
./bin/foundry.* --headless test run --case "*Script compilation and runtime*" --print-filenames --force-colors
```

Expected: the alias declaration is rejected as an unknown declaration.

- [ ] **Step 3: Add the union `DataType` kind and alias AST**

Add a `Kind` value for a type set with canonical member storage, plus a declaration node holding the alias name and its member `TypeNode`s. Extend `DataType::operator==` (`fs_parser.h:361-434`), substitution, `to_string`, serialization, and `to_property_info` so a multi-member union reports `Variant::NIL` and a single-member union collapses to its member with its `NumericType` intact.

- [ ] **Step 4: Parse aliases and type-context unions**

Recognize `type IDENTIFIER =` at file and class scope only, by two-token lookahead, leaving `type` an ordinary identifier elsewhere. Extend `parse_type()` so `|` is consumed only inside a type expression at the lowest precedence, below `?`. Leave expression precedence and `Token::PIPE` handling at `fs_parser.cpp:5076` untouched. Reject empty unions, `void`/`Variant`/bare-type-parameter members, and parameterized aliases with source-located diagnostics. Reject `@export` on a multi-member union at `fs_parser.cpp:7446-7521`.

- [ ] **Step 5: Formatter and grammar**

Emit canonical `type Name = A | B` with single spaces around `|`, and confirm expression-level bitwise OR formatting is unchanged. Document the alias production, contextual `type` and `|`, precedence relative to `?`, normalization including nullability hoisting, and runtime erasure in `GRAMMAR.md`.

- [ ] **Step 6: Regenerate fixtures and verify**

```sh
./bin/foundry.* --headless test generate-fixtures modules/foundry_script/tests/scripts
./bin/foundry.* --headless test generate-format-fixtures modules/foundry_script/tests/scripts/format
./bin/foundry.* --headless test run --case "*Script compilation and runtime*" --force-colors
./bin/foundry.* --headless test run --suite "*[Modules][FoundryScript][Format]*" --force-colors
```

Review the regenerated diff before staging — both generators rewrite every fixture they sweep and neither takes a path filter.

- [ ] **Step 7: Commit**

```sh
git add modules/foundry_script/fs_tokenizer.cpp modules/foundry_script/fs_parser.h modules/foundry_script/fs_parser.cpp modules/foundry_script/fs_parser_data_type.cpp modules/foundry_script/GRAMMAR.md modules/foundry_script/tests/scripts
git commit -m "feat(foundry_script): parse static type unions"
```

## Task 2: Resolve aliases and normalize unions

**Files:**
- Modify: `fs_analyzer.h`, `fs_analyzer.cpp`, `fs_parser_data_type.cpp`, `fs_compiler.cpp`
- Test: `tests/scripts/analyzer/features/type_alias_union.fs`, `tests/scripts/analyzer/errors/type_alias_union_invalid.fs`, `tests/scripts/runtime/features/type_alias_union_erasure.fs` (each with `.out`)

- [ ] **Step 1: Write analyzer and runtime fixtures**

Cover aliases in variables, parameters, returns, and callable signatures; nested aliases; duplicate members; nullability hoisting proving `int? | uint` and `int | uint?` are the same type; single-member collapse retaining width; a user-defined class union proving no wrapper exists. Negative fixtures (analyzer-level, so they must still parse): alias cycles, unknown members, an alias used as an expression/constructor/`extends`/`uses` target, `is` against a multi-member alias, and a union as a typed-container element type.

- [ ] **Step 2: Run the corpus and confirm failures**

```sh
./bin/foundry.* --headless test run --case "*Script compilation and runtime*" --print-filenames --force-colors
```

- [ ] **Step 3: Implement file-local alias registration and normalization**

Register aliases per file, not as globals. Resolve members lazily with cycle detection, preserving source locations. Normalize in the locked order: expand, flatten, hoist nullability, dedupe, canonicalize, collapse single members.

- [ ] **Step 4: Integrate normalized unions into compatibility**

Update `is_type_compatible`, assignment, return, and callable-signature checks so a source satisfies a union target when it satisfies one member, and a union source satisfies a concrete target only when every member does. Reject unions as typed-container element types.

- [ ] **Step 5: Preserve union metadata through compiled bytecode**

The corpus runs a second time through compiled bytecode (`fs_test_runner_suite.h:84`), so union `DataType`s must serialize and reload. Carry them in the rich `FSDataType` channel (`fs_compiler.cpp:343`, `:5138-5151`), not the lossy `PropertyInfo`.

- [ ] **Step 6: Verify runtime erasure**

The runtime fixture passes `int`, `uint`, and a class through alias-typed parameters and returns them unchanged, and asserts a single-member alias keeps its width. Assert observable values, not internal fields.

- [ ] **Step 7: Regenerate fixtures and commit**

```sh
./bin/foundry.* --headless test generate-fixtures modules/foundry_script/tests/scripts
./bin/foundry.* --headless test run --case "*Script compilation and runtime*" --force-colors
git add modules/foundry_script/fs_analyzer.h modules/foundry_script/fs_analyzer.cpp modules/foundry_script/fs_parser_data_type.cpp modules/foundry_script/fs_compiler.cpp modules/foundry_script/tests/scripts
git commit -m "feat(foundry_script): resolve transparent type aliases"
```

## Task 3: `Number` and union generic bounds

**Files:**
- Modify: `fs_analyzer.cpp`, `fs_analyzer_surface.cpp`, `fs_analyzer_call_validation.cpp`
- Test: `tests/scripts/analyzer/features/generic_union_bounds.fs`, `tests/scripts/analyzer/errors/generic_union_bound_violation.fs` (each with `.out`)

- [ ] **Step 1: Write bound fixtures**

A generic method and a generic class with `[T: Number]` and `[T: A | B]`, instantiated with each scalar, a non-scalar, `Variant`, a union-typed argument, and a forwarded type parameter with a sufficient and an insufficient bound. Include an attempt to redeclare `Number`.

- [ ] **Step 2: Run the corpus and confirm failures**

```sh
./bin/foundry.* --headless test run --case "*Script compilation and runtime*" --print-filenames --force-colors
```

- [ ] **Step 3: Implement union bound satisfaction**

Extend `type_argument_satisfies_bound()` (`fs_analyzer.cpp:8870`): a concrete argument satisfies a union bound if one normalized member accepts it; a **union** argument satisfies it only if every member does; a type-parameter argument must prove satisfaction through its own bound; unbounded parameters remain rejected. Preserve the existing strict-null guard (`:8926-8928`) and `Type[T]` handle recursion (`:8895-8907`).

- [ ] **Step 4: Integrate with inference and substitution**

Inferred and explicit type arguments use the same path (`fs_analyzer_surface.cpp:1421-1450`, `fs_analyzer_call_validation.cpp:376-416`). Inference from a union-typed argument yields the normalized union. Note that `collect_method_type_parameter_bounds` (`fs_analyzer_call_validation.cpp:108-120`) and every other reader index `type_parameter_bound[0]`; the single-bound invariant is unchanged, only the bound's *kind* is new. Diagnostics name the alias and its normalized members deterministically.

- [ ] **Step 5: Add `Number`**

Provide `Number` as a compiler-defined, globally visible alias expanding to the source-spellable numeric types — derive it from the `NUMERIC_BUILTIN_TYPES` registry (`fs_parser_data_type.cpp:68-78`) plus `float` rather than hard-coding five names. Reject user redeclaration via `class_name`, `trait_name`, or `type`.

- [ ] **Step 6: Run and commit**

```sh
./bin/foundry.* --headless test generate-fixtures modules/foundry_script/tests/scripts
./bin/foundry.* --headless test run --case "*Script compilation and runtime*" --force-colors
git add modules/foundry_script/fs_analyzer.cpp modules/foundry_script/fs_analyzer_surface.cpp modules/foundry_script/fs_analyzer_call_validation.cpp modules/foundry_script/tests/scripts
git commit -m "feat(foundry_script): enforce union generic bounds"
```

## Task 4: Set-wise numeric operations

**Files:**
- Modify: `fs_type.h`, `fs_type.cpp`, `fs_analyzer.cpp`
- Test: `modules/foundry_script/tests/test_integer_promotion.h`
- Test: `tests/scripts/analyzer/features/generic_numeric_operations.fs`, `tests/scripts/analyzer/errors/generic_numeric_operation_invalid.fs` (each with `.out`)

- [ ] **Step 1: Write the operation-matrix C++ tests**

Add cases to `test_integer_promotion.h` using its existing `[Modules][FoundryScript][NumericTypes]` name prefix. Cover a member-set pair where all combinations agree (`int | long` with `int | long` → `long`), one where results differ (`int | float` with `int` → `int | float`), one with a combination that has no result (`int | ulong` with `long`), and the `Number`-with-`Number` case, which must be rejected.

- [ ] **Step 2: Run the C++ cases and confirm the new ones fail**

```sh
./bin/foundry.* --headless test run --case "*NumericTypes*" --force-colors
```

- [ ] **Step 3: Implement the set-wise helper**

Add a helper beside `FSNumericConversion` that enumerates normalized members, invokes the existing per-pair logic, and returns either a rejection identifying the offending pair or the normalized union of the per-pair results. Route through `FSAnalyzer::get_operation_type` (`fs_analyzer.cpp:15736`) rather than `promote_integer_pair` alone, because `float` carries `NumericType::NONE` and is not handled by the integer-only promoter. Do not weaken scalar rules and do not introduce mixed-carrier conversions.

- [ ] **Step 4: Apply the helper to binary expressions**

When an operand is a union or a union-bounded type parameter, use the helper. The result type is the union of per-pair results. Reject when any pair has no result, with a diagnostic naming the pair and directing the author to narrow or convert. Reuse the phrasing established by `make_integer_promotion_error` (`fs_analyzer.cpp:15872`).

- [ ] **Step 5: Add Foundry Script behavior fixtures**

Cover direct arithmetic under `[T: int | long]`; an `add[X: Number, Y: Number]` that narrows before operating and returns `long`; and an explicit negative fixture asserting that `left + right` under `[X: Number, Y: Number]` is rejected. That rejection is a designed outcome, not a bug — the promotion matrix has no common type for `int`/`ulong` or `long`/`ulong` (`fs_type.cpp:334-353`).

Also assert that the pre-existing `long + 1.5` acceptance (`fs_analyzer.cpp:15829-15836`) is unchanged, so set-wise checking does not accidentally tighten it.

- [ ] **Step 6: Run and commit**

```sh
./bin/foundry.* --headless test run --case "*NumericTypes*" --force-colors
./bin/foundry.* --headless test generate-fixtures modules/foundry_script/tests/scripts
./bin/foundry.* --headless test run --case "*Script compilation and runtime*" --force-colors
git add modules/foundry_script/fs_type.h modules/foundry_script/fs_type.cpp modules/foundry_script/fs_analyzer.cpp modules/foundry_script/tests/test_integer_promotion.h modules/foundry_script/tests/scripts
git commit -m "feat(foundry_script): check numeric union operations"
```

## Task 5: Flow-sensitive narrowing

**Files:**
- Modify: `fs_analyzer_flow_finality.cpp`, `fs_analyzer.h`, `fs_warning.h`
- Test: `tests/scripts/analyzer/features/type_union_narrowing.fs`, `tests/scripts/analyzer/errors/type_union_narrowing_invalid.fs`, `tests/scripts/analyzer/warnings/type_union_unreachable_test.fs` (each with `.out`)

- [ ] **Step 1: Write narrowing fixtures**

Cover `if value is int`, `is not`, chained and nested tests, aliases, `[T: Number]` parameters, nullable unions, and branch joins. Include these specific cases, each of which encodes a locked decision:

- `is not long` removes both `long` and `int` (downward-closed removal); `is not int` removes only `int`.
- Testing `long` before `int` in a chain produces the unreachable-test warning.
- A union-typed **member** variable is not narrowed, and the local-copy workaround is.
- In a function returning `X`, `return value` after `if value is int:` is an error.
- An operation attempted before narrowing is rejected.

- [ ] **Step 2: Run the corpus and confirm failures**

```sh
./bin/foundry.* --headless test run --case "*Script compilation and runtime*" --print-filenames --force-colors
```

- [ ] **Step 3: Refine the flow state**

In `FSAnalyzer::FlowFinalityContext` (`fs_analyzer.h:192-250`), when the tested type is a member of the subject's normalized union, set the branch-local type to that member. For the false branch, remove the tested member **and every member it subsumes** — `long` subsumes `int`, `ulong` subsumes `uint` — because `is` on a numeric type is a carrier-plus-range predicate (`fs_vm.cpp:2064-2084`), not a declared-width test. Preserve nullability and existing tagged-union case-bind behavior (`fs_analyzer_flow_finality.cpp:1736-1765`).

- [ ] **Step 4: Handle type parameters and joins**

Narrowing refines the *value's* static type; the type parameter itself is unchanged, so generic substitution and return checks continue to see `X`. At joins, union the surviving alternatives. Extend `apply_match_branch_flow_narrowing` (`:1680`) only insofar as `when value is T` already routes through the shared type-test path; bare-type match patterns for builtins stay unsupported.

- [ ] **Step 5: Add the unreachable-test warning**

Add a warning to `fs_warning.h` for a type test on a union subject that is statically unreachable because an earlier test in the chain subsumes it. Default severity WARN.

- [ ] **Step 6: Run and commit**

```sh
./bin/foundry.* --headless test generate-fixtures modules/foundry_script/tests/scripts
./bin/foundry.* --headless test run --case "*Script compilation and runtime*" --force-colors
git add modules/foundry_script/fs_analyzer_flow_finality.cpp modules/foundry_script/fs_analyzer.h modules/foundry_script/fs_warning.h modules/foundry_script/tests/scripts
git commit -m "feat(foundry_script): narrow union values by type tests"
```

## Task 6: LSP, completion, and refactoring

Grammar and formatter already landed in Task 1; this task covers the remaining tooling surfaces, which live outside the corpus runner.

**Files:**
- Modify: presentation/completion/refactor code reached by the existing generic-type tooling tests
- Test: `tests/scripts/lsp/type_alias_union_presentation.fs`

- [ ] **Step 1: Add tooling fixtures first**

Specify expected presentation for alias names, the expanded members shown where a diagnostic needs them, and completion behavior at a type position after `|`.

- [ ] **Step 2: Run the tooling suites before implementation**

```sh
./bin/foundry.* --headless test run --suite "*[Modules][FoundryScript][LSP]*" --suite "*[Modules][FoundryScript][Completion]*" --suite "*[Modules][FoundryScript][Refactor]*" --force-colors
```

- [ ] **Step 3: Update presentation, completion, and refactoring**

Show alias declarations and union members in symbol and completion results. Preserve the alias name in source-oriented presentation and show the normalized members in diagnostics. Rename and find-references must treat the alias declaration as the definition and its member types as references to *their* declarations, not to the alias.

- [ ] **Step 4: Run and commit**

```sh
./bin/foundry.* --headless test run --suite "*[Modules][FoundryScript][LSP]*" --suite "*[Modules][FoundryScript][Completion]*" --suite "*[Modules][FoundryScript][Refactor]*" --force-colors
git add modules/foundry_script/tests/scripts/lsp
git commit -m "feat(foundry_script): present type aliases in editor tooling"
```

## Task 7: Usage guidance in the language primer

Syntax is documented in `GRAMMAR.md` by Task 1. This task documents *when to use* the feature, which is a separate concern with a separate home. `GRAMMAR.md` declares itself a normative specification and scopes its sync obligation to tokens, keywords, precedence, and syntax (§10), so best-practice prose does not belong there. `docs/fs_language_primer.md` is explicitly the idiomatic-usage reference "for people and code-generating models that need to produce valid, idiomatic `.fs` files" and already carries a `## Common Pitfalls` section.

This matters because the feature's sharpest limits sit exactly where an author's first instinct lands: reaching for a union to distinguish cases (which needs a tagged union), or expecting `+` to work under a `Number` bound (which is rejected by design).

**Files:**
- Modify: `docs/fs_language_primer.md`
- Modify: `modules/foundry_script/GRAMMAR.md` (one-line cross-reference only)

- [ ] **Step 1: Add the type-alias/union subsection**

Under `## Grammar And Syntax Cheat Sheet`, near `### Types`, add `### Type Aliases And Unions`. Cover when to reach for a union — one generic function over several numeric types instead of overloads; `[T: int | long]` when direct arithmetic is wanted; a single-member alias such as `type Meters = float` as a zero-cost readability device; a parameter that branches immediately — and when to reach for something else: a tagged union when the cases must be reliably distinguished, a trait when a shared method surface is needed, `T?` for "value or nothing", `Variant` for genuinely dynamic values, and nothing at all for member variables, typed containers, and exports.

Include a short decision table (union / tagged union / trait / nullable / `Variant`); it carries more than paragraphs here.

- [ ] **Step 2: Add a worked example**

Under `## Few-Shot Examples`, show a `Number`-bounded function that narrows before operating. It must not imply that direct arithmetic works under a full `Number` bound, and its type tests must be ordered narrowest-first.

- [ ] **Step 3: Add pitfalls**

Extend `## Common Pitfalls` with entries for: expecting `+` to work under `[X: Number, Y: Number]`; using a union where a tagged union or trait is correct; expecting `is int` and `is long` to be disjoint; unions in typed containers or `@export`; and union-typed member variables, which never narrow.

- [ ] **Step 4: Cross-reference and verify samples**

Add a one-line pointer from the `GRAMMAR.md` type section to the primer, with no duplicated guidance. Verify every new code sample against the shipped implementation by compiling or running it, not by inspection. Do not add a test that asserts on this document's prose — documentation correctness is a review concern per the repository test-authoring rules.

- [ ] **Step 5: Commit**

```sh
git add docs/fs_language_primer.md modules/foundry_script/GRAMMAR.md
git commit -m "docs(foundry_script): document idiomatic use of type unions"
```

## Task 8: Full regression validation and handoff

- [ ] **Step 1: Run the focused suites**

```sh
python3 scripts/agent_build.py --backend ninja --test --case "*Script compilation and runtime*"
python3 scripts/agent_build.py --backend ninja --test --case "*NumericTypes*"
python3 scripts/agent_build.py --backend ninja --test --suite "*[Modules][FoundryScript][Format]*"
```

Expected: all pass with no unintended fixture churn.

- [ ] **Step 2: Run native strict validation**

```sh
python3 scripts/agent_build.py --compiler-cache ccache --jobs 4 --test
```

Expected: the strict `dev_mode=yes dev_build=yes tests=yes` build completes and doctest reports `Status: SUCCESS!`. Trust that summary line, not the process exit code — the run prints `ObjectDB instances leaked` at cleanup and can exit non-zero even when every test passes. Record any leak summary separately.

- [ ] **Step 3: Repository hygiene**

```sh
git diff --check
git status --short
```

Expected: no whitespace errors and only intentional tracked changes. `modules/foundry_script/tests/scripts/.foundry/autoload_index_cache.cfg` is generated by corpus runs and must not be staged; if the corpus project config or fixtures show unexpected modifications, reset them rather than committing the pollution.

- [ ] **Step 4: Commit final fixture updates if needed**

```sh
git add modules/foundry_script/tests/scripts
git commit -m "test(foundry_script): cover numeric type unions"
```

- [ ] **Step 5: Prepare review/merge handoff**

Summarize the commits, the focused and full test commands with their evidence, and the locked limitations — notably that direct arithmetic under a full `Number` bound is rejected by design, that `is int`/`is long` are subset predicates rather than disjoint discriminators, that member variables cannot be narrowed, and that aliases are file-local in v1. Link the epic and child issues. Do not claim completion without strict-validation evidence.
