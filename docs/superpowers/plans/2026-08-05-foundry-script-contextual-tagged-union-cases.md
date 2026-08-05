# FoundryScript Contextual Tagged-Union Case Shorthand — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or
> superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Support expected-type-driven contextual case construction in every consumer position the issue names —
`var x: Result[int, String] = .Ok(1)`, `return .Err("bad")`, `items.append(.Ok(2))`, `[.Ok(1)]`, `.Ok(1) as Result[..]`,
`cond ? .Ok(1) : .Err("x")`, and `.Ok(value)` as a `match`/`is` pattern head — for generic and non-generic tagged unions,
while preserving explicit `Result[int, String].Ok(1)` as the unambiguous fallback.

**Architecture:** Add a prefix `.` parser rule that produces a lightweight contextual-case AST node. At each consumer
site that already computes an expected type (var init, return, assignment, call argument, container element, cast,
ternary branch, match/is pattern), detect the marker and dispatch into the existing
`FSAnalyzer::reduce_call_enum_case_construction` (shipped by epic #1595) after synthesizing the qualified callee from
the expected specialization. No expected-type parameter is added to `reduce_expression`; the design reuses the codebase's
"reduce standalone, then patch in place" architecture.

**Tech Stack:** C++17 engine/module code, FoundryScript `.fs` integration fixtures, doctest C++ tests, LSP/completion
fixtures, SCons/Ninja through `scripts/agent_build.py`, and the normative `modules/foundry_script/GRAMMAR.md` grammar.

**Design references:**

- `docs/superpowers/specs/2026-08-02-foundry-script-generic-tagged-unions-design.md` §8, §13.F2, §15.5
- `docs/superpowers/plans/2026-08-02-foundry-script-generic-tagged-unions.md`

**GitHub issue:** [#1606](https://github.com/cafecito-games/Foundry/issues/1606)
**Parent epic:** [#1595](https://github.com/cafecito-games/Foundry/issues/1595) (closed; foundation shipped)

---

## Execution rules

- Implement each numbered task as one focused PR corresponding to one native GitHub subissue.
- Start each task from current `develop` in its own worktree.
- Do not regenerate unrelated `.out` or formatter fixtures.
- Tests that create files use the wrapper-provided `.test_scratch`; never write generated files into tracked fixtures.
- Use the command-first Foundry CLI. Do not introduce legacy `--test` or `--path` invocation forms.
- During iteration use `python3 scripts/agent_build.py --backend ninja`; before PR handoff rerun the native strict
  backend (no `--backend` flag).
- Stage only files owned by the current task. Each task ends with the exact focused commit listed below.
- Use `--progress-file /tmp/foundry-test-progress.jsonl` for any long full-suite run.

## File responsibility map

- **Parser / AST:** `modules/foundry_script/fs_parser.{h,cpp}` — prefix rule for `PERIOD`, contextual-case node
  storage, `parse_match_pattern_dotted_head` extension.
- **Grammar:** `modules/foundry_script/GRAMMAR.md` — normative production for the contextual case expression.
- **Formatter:** `modules/foundry_script/fs_format.cpp` — null-base/null-callee guards in `print_subscript`/`print_call`.
- **Analyzer core:** `modules/foundry_script/fs_analyzer.{h,cpp}` — new `resolve_contextual_enum_case` helper and
  wiring at consumer sites; ternary expected-type thread.
- **Argument validation:** `modules/foundry_script/fs_analyzer_call_validation.cpp` — post-callee argument hook.
- **Pattern analysis:** `modules/foundry_script/fs_analyzer.cpp` — `resolve_match_case_pattern`, `reduce_type_test`,
  `resolve_type_test_case_binds`.
- **Completion:** `modules/foundry_script/fs_editor.cpp` — new completion context, reuse `_find_identifiers_in_base`
  ENUM branch (`2269-2324`) and `_populate_global_enum_completion_values` (`:2849`).
- **LSP / refactoring:** `modules/foundry_script/language_server/fs_extend_parser.cpp`,
  `fs_semantic_tokens.cpp`, `fs_refactoring.cpp` (hover, semantic tokens, go-to-def/rename).
- **Docgen / docs:** `modules/foundry_script/editor/fs_docgen.cpp`, `modules/foundry_script/README.md`,
  language reference.
- **C++ tests:** `modules/foundry_script/tests/test_contextual_tagged_union.h` (new), wired through
  `tests/test_main.cpp`.
- **Script fixtures:** parser/analyzer/runtime/completion/lsp/format directories under
  `modules/foundry_script/tests/scripts/`.

## Resolved design decisions

These were settled during plan review. Each entry includes the evidence an implementer needs; do not re-derive.

1. **No user-level function overloading.** `member_functions` is keyed by name with single signatures
   (`foundry_script.h:316`); duplicate member names are errors (`fs_analyzer_surface.cpp:299, 314, 331`). Native
   ClassDB also stores one signature per name. **The "ambiguous overload" scenario cannot be written by users.**
   Argument-position `.Case(...)` uses the post-callee hook in `validate_call_arg`
   (`fs_analyzer_call_validation.cpp:982-1004`), mirroring how Array/Dictionary literals already get their parameter
   type after the callee is selected.
2. **Lambda last-expression return.** `reduce_lambda` (`fs_analyzer.cpp:10520`) → `resolve_function_signature`
   (`:3745-378`) → `resolve_function_body` (`:4113`) saves and restores `current_function` so it points at the
   lambda's own function during body analysis, with the declared return type installed on the function node.
   `return .Ok(1)` inside a lambda annotated `() -> Result[int, String]` therefore picks up the expected type
   automatically through `resolve_return`'s existing `expected_type = parser->current_function->get_datatype()`
   read at `:5271`. **No new plumbing required.**
3. **Default parameter values.** Routed through `resolve_assignable` (same path as `var`), so the expected-type hook
   added there covers defaults. Payload-less defaults already constant-fold via `fs_tagged_union_case_singleton`.
   Payload-bearing defaults (`func f(x: Result[int, String] = .Ok(1))`) require extending case-construction
   constant-folding — see Task 2 step 9. **Payload-bearing defaults are in scope and land at parity with the
   explicit form** (`func f(x: Result[int, String] = Result[int, String].Ok(1))`), which also does not constant-fold
   today.
4. **`as` to a non-tagged-union type.** `expr as Result[..]` is a compatibility check, not a constructor
   (`fs_analyzer.cpp:7283-7300`; `fs_bytecodegen.cpp:1564-1573` emits `OPCODE_CAST_TO_SCRIPT` which keeps the same
   value). `.Ok(1) as int` is meaningless. **Emit the missing-expected-type diagnostic** when the cast target is not
   a specialized tagged union.
5. **Variant / untyped / inferred targets.** `var x = .Ok(1)`, `var x: Variant = .Ok(1)`, and `var x := .Ok(1)`
   all lack a complete specialized tagged-union expected type. **All three error** with the missing-expected-type
   diagnostic. `Variant` is not a complete specialized union.
6. **Payload-less `.Case`.** Supported in all 8 contexts (var init, return, assignment, argument, container element,
   cast, ternary branch, match/is pattern). Reuses `fs_tagged_union_case_singleton` (`fs_tagged_union.h:43`,
   consumed at `fs_analyzer.cpp:9588`). Specialization lives on the expression's datatype, not the singleton.
7. **Pattern exhaustiveness.** The DEBUG-only checker at `fs_analyzer.cpp:4974-5041` keys on
   `case_datatype.enum_type == p_match_type.enum_type` and reads the case set from `p_match_type.enum_values`
   (the subject's declaration). **The synthesized pattern must carry the subject's `enum_type`** so the equality
   check passes; otherwise the case is silently dropped from coverage. Exhaustiveness is otherwise unchanged.
8. **Self-method shorthand conflict.** The prefix slot for `PERIOD` in the Pratt table (`fs_parser.cpp:6767`) is
   `nullptr`; no `implicit_self`/`self_method_shorthand` fixtures exist. A leading `.` is currently a hard parse
   error. **Introducing a prefix rule conflicts with nothing.**
9. **Recursive contextual shorthand.** `var x: Result[Result[int, String], String] = .Ok(.Ok(1))` works because the
   inner `.Ok(1)` is a function-argument-position shorthand resolved against the outer case's specialized `T`
   field via the argument-position hook. No special handling beyond what Task 2 already adds.

## Task 1: Parse and format the leading-`.` contextual case expression

**Boundary:** Parser, AST, formatter, grammar, and regression coverage only. No analyzer behavior change beyond
preserving today's "this is currently an error" semantic — the new node reaches the analyzer but analyzer-side
resolution lands in Task 2.

**Files:**

- Modify: `modules/foundry_script/GRAMMAR.md`
- Modify: `modules/foundry_script/fs_parser.h`
- Modify: `modules/foundry_script/fs_parser.cpp`
- Modify: `modules/foundry_script/fs_format.cpp`
- Create: `modules/foundry_script/tests/test_contextual_tagged_union.h`
- Modify: `tests/test_main.cpp`
- Create: `modules/foundry_script/tests/scripts/parser/features/contextual_tagged_union_shorthand.norun.fs`
- Create: `modules/foundry_script/tests/scripts/parser/errors/contextual_tagged_union_leading_digit.fs`
- Create: `modules/foundry_script/tests/scripts/parser/errors/contextual_tagged_union_leading_digit.out`
- Create: `modules/foundry_script/tests/scripts/format/contextual_tagged_union/input.fs`
- Create: `modules/foundry_script/tests/scripts/format/contextual_tagged_union/expected.fs`

- [ ] **Step 1: Add the failing parser doctest**

In the new `test_contextual_tagged_union.h`, assert that parsing `var x = .Ok(1)` produces a `CallNode` with
`is_contextual_enum_case = true`, `function_name == "Ok"`, one argument, and `callee == nullptr`. Also assert
`.None` produces a `SubscriptNode` with `is_attribute = true`, `base == nullptr`,
`is_contextual_enum_case = true`, and `attribute->name == "None"`.

- [ ] **Step 2: Run the focused test and verify the prefix `.` currently fails**

```sh
python3 scripts/agent_build.py --backend ninja --test --case "*ContextualTaggedUnion*"
```

Expected: the new doctest fails because the Pratt table has no prefix rule for `PERIOD` and `parse_precedence`
returns `nullptr` ("Expected expression").

- [ ] **Step 3: Add AST storage**

On `SubscriptNode` add `bool is_contextual_enum_case = false;` and on `CallNode` add
`bool is_contextual_enum_case = false;` (`fs_parser.h`, beside the existing `is_enum_case_construction` /
`enum_case_tag` fields at line 842). These flags are how the analyzer recognizes a contextual case without re-walking
the tree.

- [ ] **Step 4: Add a prefix rule for `PERIOD`**

In the Pratt rule table (`fs_parser.cpp:6767`) replace the `nullptr` prefix entry for `PERIOD` with
`&FSParser::parse_contextual_attribute`. The new function:

- Guards on the *next* token being an `IDENTIFIER` (or a keyword convertible via `is_node_name()`); otherwise it
  does **not** consume and signals a parse error. The existing `.5` numeric-literal path
  (`fs_tokenizer.cpp:1728-1743`, gated on `!_last_token_precedes_bin_op()`) is unaffected — that path produces a
  single `LITERAL` token, never `PERIOD IDENTIFIER`.
- Consumes `.Identifier`, produces a `SubscriptNode` with `base = nullptr`, `is_attribute = true`,
  `attribute = parsed identifier`, `is_contextual_enum_case = true`.

- [ ] **Step 5: Compose with `parse_call` for the payload form**

When `parse_precedence` continues after the prefix rule and sees `(`, the existing `parse_call` path runs and
produces a `CallNode` whose `callee` is the new `SubscriptNode`. Mark the `CallNode` as `is_contextual_enum_case = true`
whenever its callee is a contextual-case subscript; add the flag copy in `parse_call`. Template: `parse_call`
already special-cases `super.method(...)` at `5526-5554`.

- [ ] **Step 6: Update `parse_match_pattern_dotted_head`**

`fs_parser.cpp:4473-4531` currently handles `Result[int, String].Ok(...)` inside `match`/`is` patterns. Extend it
to accept the bare-`.` form (no base) and tag the resulting pattern node with `is_contextual_enum_case = true`.
Task 4 consumes the marker.

- [ ] **Step 7: Formatter guards**

In `print_subscript` (`fs_format.cpp:2719-2750`) and `print_call` (`2677-2700`) gate the
`print_operand(FPREC_CALL, base/callee)` calls on `base != nullptr` / `callee != nullptr`. Verify `.Ok(1)`, `.None`,
and nested (`[.Ok(1)]`, `cond ? .Ok(1) : .Err("x")`) round-trip.

- [ ] **Step 8: Grammar**

Add the normative production:

```ebnf
contextual_enum_case = PERIOD, identifier, [ "(" arguments ) ")" ] ;
```

and add the alternative to the primary expression production, with a note that it is accepted only where the
surrounding consumer can supply a complete specialized tagged-union expected type (enforced semantically, not
syntactically).

- [ ] **Step 9: Add positive/negative parser fixtures**

The positive fixture contains both shorthand forms in expression positions:

```foundry
var a = .Ok(1)
var b = .None
var c = [.Ok(1), .Err("x")]
var d = cond ? .Ok(1) : .Err("no")
```

Add a regression fixture asserting `.5` still tokenizes as a float literal and `t.0` still parses as a tuple index.
The `.norun.fs` form is parsed/analyzed but not executed.

- [ ] **Step 10: Verify parser and canonical formatting**

```sh
python3 scripts/agent_build.py --backend ninja --test --case "*ContextualTaggedUnion*"
./bin/foundry.* --headless test generate-format-fixtures \
  modules/foundry_script/tests/scripts/format/contextual_tagged_union
git diff --exit-code -- modules/foundry_script/tests/scripts/format/contextual_tagged_union/expected.fs
```

Expected: focused doctests pass; `.5`, `t.0`, and existing member-access suites remain green; fixture generation
leaves the checked-in expected file unchanged.

- [ ] **Step 11: Commit Task 1**

```sh
git add modules/foundry_script/GRAMMAR.md \
  modules/foundry_script/fs_parser.h modules/foundry_script/fs_parser.cpp \
  modules/foundry_script/fs_format.cpp modules/foundry_script/tests/test_contextual_tagged_union.h \
  modules/foundry_script/tests/scripts/parser modules/foundry_script/tests/scripts/format/contextual_tagged_union \
  tests/test_main.cpp
git commit -m "feat(foundry_script): Parse contextual tagged-union case shorthand"
```

## Task 2: Resolve the four core expression contexts

**Boundary:** Var/const/parameter-default initializer, `return`, assignment, and function argument positions. All
contextual-case diagnostics. Default-value constant-folding for payload-bearing cases. No container/cast/ternary/
pattern work yet.

**Files:**

- Modify: `modules/foundry_script/fs_analyzer.h`
- Modify: `modules/foundry_script/fs_analyzer.cpp`
- Modify: `modules/foundry_script/fs_analyzer_call_validation.cpp`
- Modify: `modules/foundry_script/fs_compiler.cpp` (constant-folding for case construction, shared by explicit form)
- Modify: `modules/foundry_script/tests/test_contextual_tagged_union.h`
- Create: `modules/foundry_script/tests/scripts/analyzer/features/contextual_tagged_union_var.fs`
- Create: `modules/foundry_script/tests/scripts/analyzer/features/contextual_tagged_union_return.fs`
- Create: `modules/foundry_script/tests/scripts/analyzer/features/contextual_tagged_union_assignment.fs`
- Create: `modules/foundry_script/tests/scripts/analyzer/features/contextual_tagged_union_argument.fs`
- Create: `modules/foundry_script/tests/scripts/analyzer/features/contextual_tagged_union_payloadless.fs`
- Create: `modules/foundry_script/tests/scripts/analyzer/features/contextual_tagged_union_default_value.fs`
- Create: `modules/foundry_script/tests/scripts/analyzer/errors/contextual_tagged_union_no_expected_type.fs`
- Create: `modules/foundry_script/tests/scripts/analyzer/errors/contextual_tagged_union_no_expected_type.out`
- Create: `modules/foundry_script/tests/scripts/analyzer/errors/contextual_tagged_union_unknown_case.fs`
- Create: `modules/foundry_script/tests/scripts/analyzer/errors/contextual_tagged_union_unknown_case.out`
- Create: `modules/foundry_script/tests/scripts/analyzer/errors/contextual_tagged_union_payload_arity.fs`
- Create: `modules/foundry_script/tests/scripts/analyzer/errors/contextual_tagged_union_payload_arity.out`
- Create: `modules/foundry_script/tests/scripts/analyzer/errors/contextual_tagged_union_payload_type.fs`
- Create: `modules/foundry_script/tests/scripts/analyzer/errors/contextual_tagged_union_payload_type.out`
- Create: `modules/foundry_script/tests/scripts/analyzer/errors/contextual_tagged_union_variant_target.fs`
- Create: `modules/foundry_script/tests/scripts/analyzer/errors/contextual_tagged_union_variant_target.out`
- Create: `modules/foundry_script/tests/scripts/runtime/features/contextual_tagged_union_core.fs`

- [ ] **Step 1: Add failing analyzer/runtime fixtures**

Cover the four positive contexts, the payload-less form, the default-value position, and the five error categories
(missing expected type, unknown case, wrong arity, payload type mismatch, Variant target). Each error fixture's
`.out` first line is `FS_TEST_ANALYZER_ERROR` followed by `>> ERROR at line N: <message>` lines.

The runtime fixture constructs the same value via shorthand and via explicit `Result[int, String].Ok(...)` and
asserts the resulting `[tag, payload...]` arrays are byte-identical.

- [ ] **Step 2: Implement the central helper**

Add to `fs_analyzer.h`:

```cpp
bool resolve_contextual_enum_case(FSParser::ExpressionNode *p_expr,
        const FSParser::DataType &p_expected_type,
        FSParser::Node *p_error_context);
```

Behavior:

- Returns `false` if `p_expr` is not a contextual-case node (no work done). Caller proceeds with the normal path.
- If `p_expected_type` is not a fully-specialized tagged-union meta type (not `ENUM`, not `is_tagged_union`,
  unresolved open parameters, or `Variant`), emit the missing-expected-type diagnostic and return `true`.
- For payload form (`CallNode` with `is_contextual_enum_case`): synthesize the qualified callee by setting
  `p_call->callee` to a synthetic identifier resolved against `type_to_metatype(p_expected_type)`, then dispatch
  into the existing `reduce_call_enum_case_construction(p_call, expected_meta_type)`. Reuse that helper's existing
  arity / payload-type / collection-literal-patching logic unchanged.
- For payload-less form (`SubscriptNode` with `is_contextual_enum_case`): look up the case in
  `expected_meta_type.enum_values`; if absent, emit the unknown-case diagnostic; otherwise produce the constant
  case value via the existing `fs_tagged_union_case_singleton` path, preserving specialization on the node datatype.

- [ ] **Step 3: Diagnostic wording**

Match the style of `fs_analyzer.cpp:9579` and `:11021`:

- Missing expected type:
  `Contextual shorthand ".%s" needs an expected tagged-union type; annotate the target, e.g. "var x: Result[int, String] = .%s(...)".`
- Unknown case: `Enum case "%s" has no case "%s".`
- Wrong arity: reuse the existing
  `Enum case "%s.%s" expects %d argument(s), but %d were given.` from `reduce_call_enum_case_construction`.
- Payload type mismatch: reuse the existing field-mismatch diagnostic from the same reducer.
- Variant / untyped target: same as missing-expected-type.

- [ ] **Step 4: Wire into `resolve_assignable`**

`fs_analyzer.cpp:4373-4561`, right after the existing `reduce_expression(p_assignable->initializer)` call
(line 4396), before the literal-patch checks. Only invoke when `has_specified_type` is true. Pass
`type_to_metatype(specified_type)` as the expected type. This single wire covers `var`, `const`, and parameter
default values (parameters route through `resolve_assignable` via `resolve_parameter` at `:4655`).

- [ ] **Step 5: Wire into `resolve_return`**

`fs_analyzer.cpp:5264-5399`, after `reduce_expression(p_return->return_value)` (line 5289). Only invoke when
`has_expected_type` is true. Pass `type_to_metatype(compatibility_expected_type)`. This covers top-level function
returns and lambda last-expression returns automatically (see resolved decision #2).

- [ ] **Step 6: Wire into `reduce_assignment`**

`fs_analyzer.cpp:5874-`, after `reduce_expression(p_assignment->assigned_value)` (line 5875) and after the assignee
type is known. Pass `type_to_metatype(assignee_type)`.

- [ ] **Step 7: Wire into `reduce_call` argument position**

After the callee is resolved and parameter types are known, walk arguments and call the helper for each one that
carries `is_contextual_enum_case`, passing the corresponding parameter type. The natural place is inside
`validate_call_arg` (`fs_analyzer_call_validation.cpp:982-1004`): right before the existing
`validate_argument_against_type` call, dispatch the contextual resolver with `expected_type` (the same loop pointer
the function already computes). This automatically threads the right parameter type per call site. See resolved
decision #1: FoundryScript has no user-level overloading, so the post-callee parameter type is always unambiguous.

- [ ] **Step 8: Reuse, do not duplicate, payload patching**

The existing `reduce_call_enum_case_construction` already runs `update_array_literal_element_type` /
`update_dictionary_literal_element_type` / `update_const_expression_builtin_type` on payload arguments. The helper
must not re-run them.

- [ ] **Step 9: Constant-fold payload-bearing case construction**

Defaults must constant-fold (`resolve_function_signature` at `fs_analyzer.cpp:3813` only folds
`initializer->reduced_value` when `is_constant`). Payload-less defaults already work via
`fs_tagged_union_case_singleton`; payload-bearing defaults do not.

Extend case-construction constant-folding so a fully-constant `reduce_call_enum_case_construction` call (one whose
arguments all constant-fold) sets `p_call->is_constant = true` and builds a read-only `[tag, payload...]` Array in
`p_call->reduced_value`. Apply the same path to the explicit `Result[int, String].Ok(1)` form so the two stay at
parity. Add a doctest verifying `func f(x: Result[int, String] = .Ok(1))` survives analysis and that the folded
default round-trips through bytecode export.

- [ ] **Step 10: Runtime fixture**

`runtime/features/contextual_tagged_union_core.fs` constructs via shorthand in all four contexts (var init,
return, assignment, argument) and prints tag/payload; expected output mirrors `Result[int, String].Ok(7)` →
`0, 7` etc. Verify the runtime Array representation is byte-identical to the explicit form.

- [ ] **Step 11: Verify and commit Task 2**

```sh
python3 scripts/agent_build.py --backend ninja --test --case "*ContextualTaggedUnion*"
python3 scripts/agent_build.py --backend ninja --test --case "*FoundryScript*"
git add modules/foundry_script/fs_analyzer.h modules/foundry_script/fs_analyzer.cpp \
  modules/foundry_script/fs_analyzer_call_validation.cpp modules/foundry_script/fs_compiler.cpp \
  modules/foundry_script/tests/test_contextual_tagged_union.h \
  modules/foundry_script/tests/scripts/analyzer modules/foundry_script/tests/scripts/runtime
git commit -m "feat(foundry_script): Resolve contextual union case shorthand in core contexts"
```

Expected: positive and negative analyzer fixtures pass; runtime fixture demonstrates byte-identical shorthand vs
explicit construction; existing generic-tagged-union and call-validation suites remain green.

## Task 3: Resolve nested contexts — container elements, casts, conditional branches

**Boundary:** Array/Dictionary literal elements/entries, `as` casts, ternary (`cond ? a : b`) branches. Reuses
Task 2's helper.

**Files:**

- Modify: `modules/foundry_script/fs_analyzer.h`
- Modify: `modules/foundry_script/fs_analyzer.cpp`
- Modify: `modules/foundry_script/tests/test_contextual_tagged_union.h`
- Create: `modules/foundry_script/tests/scripts/analyzer/features/contextual_tagged_union_container_element.fs`
- Create: `modules/foundry_script/tests/scripts/analyzer/features/contextual_tagged_union_dictionary_entry.fs`
- Create: `modules/foundry_script/tests/scripts/analyzer/features/contextual_tagged_union_cast.fs`
- Create: `modules/foundry_script/tests/scripts/analyzer/features/contextual_tagged_union_ternary.fs`
- Create: `modules/foundry_script/tests/scripts/analyzer/features/contextual_tagged_union_nested_generic.fs`
- Create: `modules/foundry_script/tests/scripts/analyzer/errors/contextual_tagged_union_cast_to_non_union.fs`
- Create: `modules/foundry_script/tests/scripts/analyzer/errors/contextual_tagged_union_cast_to_non_union.out`
- Create: `modules/foundry_script/tests/scripts/analyzer/errors/contextual_tagged_union_ternary_mismatch.fs`
- Create: `modules/foundry_script/tests/scripts/analyzer/errors/contextual_tagged_union_ternary_mismatch.out`
- Create: `modules/foundry_script/tests/scripts/runtime/features/contextual_tagged_union_nested.fs`

- [ ] **Step 1: Add failing fixtures**

Cover: `[.Ok(1), .Err("x")]` typed as `Array[Result[int, String]]`;
`{1: .Ok(1)} : Dictionary[int, Result[int, String]]`;
`.Ok(1) as Result[int, String]`;
`cond ? .Ok(1) : .Err("x")`;
nested-generic `Array[Array[Result[int, String]]]` with `[.Ok(1)]` recursively. Error fixtures: cast to `int`;
ternary with branches incompatible with the contextual type; ternary with no expected type at all
(`var x := cond ? .Ok(1) : .Err("x")`).

- [ ] **Step 2: Container element resolver hook**

Modify `update_array_literal_element_type` (`fs_analyzer.cpp:5645`) and `update_dictionary_literal_element_type`
(`fs_analyzer.cpp:5733`): at the top of the element loop, before the existing recursion/patching, call
`resolve_contextual_enum_case(element, expected_type, p_array)`. Because these patchers run *after* the consumer
has supplied the expected type, this is sufficient for `[.Ok(1)]` in `var x: Array[Result[int, String]] = [.Ok(1)]`.
For nested literals (`Array[Array[Result[...]]]`), the patchers recurse and the same hook fires at each level.

- [ ] **Step 3: Cast resolver hook**

In `reduce_cast` (`fs_analyzer.cpp:7245-7271`) after `reduce_expression(p_cast->operand)` (line 7247), invoke
`resolve_contextual_enum_case(p_cast->operand, type_to_metatype(cast_type), p_cast)`. If `cast_type` is not a
specialized tagged union (resolved decision #4), the helper emits the missing-expected-type diagnostic and the rest
of `reduce_cast` proceeds normally (the operand already carries an error datatype). After the resolver runs the
operand is already typed as `Result[..]`, matching the cast's no-op-construction semantics.

- [ ] **Step 4: Ternary branch resolver**

`reduce_ternary_op` (`fs_analyzer.cpp:11671`) currently has no expected-type input. Add an optional
`FSParser::DataType p_expected_type` parameter defaulting to an unspecified variant. At each branch reduction site
inside the ternary, invoke `resolve_contextual_enum_case(branch, p_expected_type, p_ternary)` when `p_expected_type`
is a specialized tagged union. Update each ternary call site to pass the local expected type when one is available
(var init, return, assignment, argument, cast, container element — all already compute it). For un-typed ternaries
(`var x := cond ? .Ok(1) : .Err("x")`), `p_expected_type` is unset and the missing-expected-type diagnostic fires
on the first contextual branch.

- [ ] **Step 5: Nested-generic verification**

`Array[Result[int, String]]`, `Dictionary[String, Result[int, String]]`, `Array[Array[Result[int, String]]]` —
verify specialization propagates through recursive patcher calls and the inner shorthand resolves with the correct
inner `T`/`E`.

- [ ] **Step 6: Verify and commit Task 3**

```sh
python3 scripts/agent_build.py --backend ninja --test --case "*ContextualTaggedUnion*"
git add modules/foundry_script/fs_analyzer.h modules/foundry_script/fs_analyzer.cpp \
  modules/foundry_script/tests/test_contextual_tagged_union.h \
  modules/foundry_script/tests/scripts/analyzer modules/foundry_script/tests/scripts/runtime
git commit -m "feat(foundry_script): Resolve contextual union case shorthand in nested contexts"
```

Expected: every nested-context fixture resolves; cast-to-non-union and ternary-mismatch error fixtures produce
stable diagnostics; existing container-literal typing behavior unchanged.

## Task 4: Resolve contextual case patterns in `match` and `is`

**Boundary:** Allow `.Case(...)` and `.Case` as a pattern head inside `match` arms and as the type test in
`expr is .Case(bind)`. Subject specialization supplies the expected union.

**Files:**

- Modify: `modules/foundry_script/fs_parser.cpp` (`parse_match_pattern_dotted_head` — finalize Task 1's marker)
- Modify: `modules/foundry_script/fs_analyzer.h`
- Modify: `modules/foundry_script/fs_analyzer.cpp` (`resolve_match_case_pattern`, `reduce_type_test`,
  `resolve_type_test_case_binds`)
- Modify: `modules/foundry_script/tests/test_contextual_tagged_union.h`
- Create: `modules/foundry_script/tests/scripts/analyzer/features/contextual_tagged_union_match_pattern.fs`
- Create: `modules/foundry_script/tests/scripts/analyzer/features/contextual_tagged_union_is_pattern.fs`
- Create: `modules/foundry_script/tests/scripts/analyzer/features/contextual_tagged_union_exhaustive_match.fs`
- Create: `modules/foundry_script/tests/scripts/analyzer/errors/contextual_tagged_union_pattern_subject_unspecialized.fs`
- Create: `modules/foundry_script/tests/scripts/analyzer/errors/contextual_tagged_union_pattern_subject_unspecialized.out`
- Create: `modules/foundry_script/tests/scripts/analyzer/errors/contextual_tagged_union_pattern_unknown_case.fs`
- Create: `modules/foundry_script/tests/scripts/analyzer/errors/contextual_tagged_union_pattern_unknown_case.out`
- Create: `modules/foundry_script/tests/scripts/runtime/features/contextual_tagged_union_patterns.fs`

- [ ] **Step 1: Add failing fixtures**

`match subject { .Ok(value): ..., .Err(_): ... }` and `if subject is .Ok(value): ...` where
`subject: Result[int, String]`. An exhaustive match fixture proves `.Ok(_)` + `.Err(_)` is exhaustive. Error
fixtures: pattern with subject of an unspecialized generic union (open parameter), pattern with case name not in
subject's declaration.

- [ ] **Step 2: Match-arm resolver**

In `resolve_match_case_pattern` (`fs_analyzer.cpp:5215-5262`), when the pattern is contextual, fetch the subject
specialization from the enclosing `p_match->test->get_datatype()`. If the subject is not a fully-specialized
tagged union, emit the subject-unspecialized error. Otherwise synthesize the qualified case pattern by substituting
the subject's type arguments into the declared payload schema (reuse the existing `specialize_enum_type` helper
from `fs_analyzer_surface.cpp:883-928`), then proceed through the existing bind/exhaustiveness path.

- [ ] **Step 3: `is` test resolver**

In `reduce_type_test` (`fs_analyzer.cpp:11725-11760`) and `resolve_type_test_case_binds`, when the test type AST is
a contextual case, fetch the subject specialization from `p_type_test->operand->get_datatype()` and synthesize the
qualified test type. Same error paths as the match case.

- [ ] **Step 4: Preserve `enum_type` for exhaustiveness**

Per resolved decision #7, the DEBUG-only exhaustiveness checker at `fs_analyzer.cpp:4974-5041` keys on
`case_datatype.enum_type == p_match_type.enum_type`. Ensure synthesis copies the subject's `enum_type` onto the
synthesized `case_datatype` so the case is counted toward coverage. Add the exhaustive-match fixture to prove it.

- [ ] **Step 5: Runtime fixture**

Exercise both forms end-to-end and verify binds carry the specialized payload types (`value: int` for
`Result[int, String].Ok`, etc.).

- [ ] **Step 6: Verify and commit Task 4**

```sh
python3 scripts/agent_build.py --backend ninja --test --case "*ContextualTaggedUnion*"
git add modules/foundry_script/fs_parser.cpp modules/foundry_script/fs_analyzer.h \
  modules/foundry_script/fs_analyzer.cpp modules/foundry_script/tests/test_contextual_tagged_union.h \
  modules/foundry_script/tests/scripts/analyzer modules/foundry_script/tests/scripts/runtime
git commit -m "feat(foundry_script): Resolve contextual union case shorthand in patterns"
```

Expected: pattern fixtures pass; exhaustiveness semantics unchanged for explicit-form patterns; error fixtures
produce stable diagnostics for unspecialized subjects and unknown cases.

## Task 5: Tooling, documentation, and epic closeout

**Boundary:** Completion, hover, signature help, semantic tokens, go-to-definition, rename, docgen, formatter
polish, user-facing documentation, GRAMMAR review, and the strict full-suite gate.

**Files:**

- Modify: `modules/foundry_script/fs_editor.cpp`
- Modify: `modules/foundry_script/language_server/fs_extend_parser.cpp`
- Modify: `modules/foundry_script/language_server/fs_semantic_tokens.cpp`
- Modify: `modules/foundry_script/editor/fs_refactoring.cpp`
- Modify: `modules/foundry_script/editor/fs_docgen.cpp`
- Modify: `modules/foundry_script/README.md`
- Modify: `modules/foundry_script/GRAMMAR.md`
- Modify: `modules/foundry_script/tests/test_lsp.h`
- Modify: `modules/foundry_script/tests/test_foundry_script.cpp`
- Create: `modules/foundry_script/tests/scripts/completion/tagged_union_cases/contextual_shorthand.fs`
- Create: `modules/foundry_script/tests/scripts/completion/tagged_union_cases/contextual_shorthand.cfg`
- Create: `modules/foundry_script/tests/scripts/completion/tagged_union_cases/contextual_shorthand.notest.fs`
- Create: `modules/foundry_script/tests/scripts/lsp/contextual_tagged_union.fs`
- Modify: `modules/foundry_script/tests/scripts/format/contextual_tagged_union/input.fs`
- Modify: `modules/foundry_script/tests/scripts/format/contextual_tagged_union/expected.fs`

- [ ] **Step 1: Failing completion fixture**

After `var x: Result[int, String] = .➡`, completion must list `Ok(value: int)` and `Err(error: String)` with
specialized signatures, and nothing else. After `var x: Option[int] = .➡`, must list `None` and
`Some(value: int)`.

- [ ] **Step 2: Completion plumbing**

Add a new completion context kind (e.g. `COMPLETION_CONTEXTUAL_UNION_CASE`) set when the parser reaches a prefix-`.`
in expression position with a known expected type. The expected union meta type must be derived from the surrounding
consumer and threaded into the completion request — `_find_enumeration_candidates` (`fs_editor.cpp:4126`) already
threads an enum hint (`arg_info.class_name`); extend the same channel. Once the expected meta type is supplied as
`base_type`, the existing `_find_identifiers_in_base` ENUM branch (`fs_editor.cpp:2269-2324`) and
`_populate_global_enum_completion_values` (`:2849`) produce the case list unchanged.

- [ ] **Step 3: Hover and signature help**

Extend `ExtendFSParser::enum_case_detail` (`language_server/fs_extend_parser.cpp:253`) consumption in the hover
path to handle the contextual marker. Hover on `.Ok` in `var x: Result[int, String] = .Ok(1)` displays
`Result[int, String].Ok(value: int)` and links to the declaration. Signature help inside `.Ok(` shows the
specialized parameter list.

- [ ] **Step 4: Semantic tokens**

The case identifier in `.Ok(...)` is classified as an enum-case reference (same token type as `Result.Ok` today).
Add a semantic-token fixture covering declaration, contextual expression use, and pattern use.

- [ ] **Step 5: Go-to-definition and rename**

Extend refactoring traversal so a contextual case identifier resolves to the same `EnumNode` case declaration as
the explicit form. Renaming a case through a contextual reference renames every explicit and contextual use. Add
a `refactor/` fixture.

- [ ] **Step 6: Docgen**

No new doc model surface is required for contextual cases (the case itself is unchanged), but verify the generated
enum doc still lists cases correctly.

- [ ] **Step 7: User-facing documentation**

Update `modules/foundry_script/README.md` (currently says "Construction is always explicit in v1" around line 237)
to introduce the shorthand with its rule: valid only where a complete expected specialized union type is
available. Update the language reference with sections on each of the 8 contexts, the diagnostics, and the
explicit-form fallback.

- [ ] **Step 8: Final GRAMMAR review**

Ensure the production added in Task 1 is consistent with the surrounding rules and that the contextual rule's
semantic constraint ("accepted only where a complete expected specialized tagged-union type is available") is
referenced from the relevant expression productions.

- [ ] **Step 9: Extend formatter fixtures**

Re-run `test generate-format-fixtures` after extending `format/contextual_tagged_union/input.fs` to cover all
nested forms (container elements, ternary branches, casts, patterns). Verify the expected file round-trips.

- [ ] **Step 10: Run focused tooling suites**

```sh
python3 scripts/agent_build.py --backend ninja --test --case "*ContextualTaggedUnion*"
python3 scripts/agent_build.py --backend ninja --test --case "*LSP*"
python3 scripts/agent_build.py --backend ninja --test --case "*FoundryScript*"
```

- [ ] **Step 11: Run native strict full-suite gate (epic closeout)**

```sh
python3 scripts/agent_build.py --compiler-cache ccache --jobs 4 --test \
  --progress-file /tmp/foundry-test-progress.jsonl
```

Expected: wrapper `build_summary` reports success; the test log ends with `[doctest] Status: SUCCESS!`. On Linux
use `DISPLAY=:1` so GUI-dependent subprocess tests do not silently skip.

- [ ] **Step 12: Run repository policy checks**

```sh
git diff --check origin/develop...HEAD
git grep -nE '\.split\("(void|bool|String|private fun|const val|def )' -- '*.py'
git grep -n "docs/superpowers" -- '*/test_*.py' '*/check_*.py'
git grep -n "hasattr(" -- '*/test_*.py'
git grep -l "assert" -- 'misc/checks/check_*.py'
```

Expected: `git diff --check` empty; the three policy searches add no new violations attributable to this epic.

- [ ] **Step 13: Commit Task 5**

```sh
git add modules/foundry_script/fs_editor.cpp modules/foundry_script/language_server \
  modules/foundry_script/editor/fs_docgen.cpp modules/foundry_script/editor/fs_refactoring.cpp \
  modules/foundry_script/README.md modules/foundry_script/GRAMMAR.md \
  modules/foundry_script/tests/test_lsp.h modules/foundry_script/tests/test_foundry_script.cpp \
  modules/foundry_script/tests/scripts/completion modules/foundry_script/tests/scripts/lsp \
  modules/foundry_script/tests/scripts/format/contextual_tagged_union
git commit -m "feat(foundry_script): Tooling and docs for contextual union case shorthand"
```

## Acceptance matrix

| Requirement (from issue #1606) | Owning task | Mechanical proof |
|---|---|---|
| All 8 expected-type contexts covered | 2, 3, 4 | analyzer + runtime fixtures per context |
| Payload-bearing `.Case(...)` | 2 | runtime fixture prints `[tag, payload...]` |
| Payload-less `.Case` | 2 | `payloadless` fixture |
| Specialization propagated into payload/result typing | 2, 3 | nested-generic fixtures in Task 3 |
| Missing expected type rejected | 2 | `no_expected_type.out` and `variant_target.out` |
| Ambiguous expected union/case rejected | 2 | N/A — FoundryScript has no user-level overloading (resolved decision #1); argument-position always resolves to a single parameter type |
| Unknown case rejected | 2, 4 | `unknown_case.out` for expression + pattern |
| Wrong payload arity rejected | 2 | reuse from `reduce_call_enum_case_construction` |
| Payload type mismatch rejected | 2 | reuse from `reduce_call_enum_case_construction` |
| Nullable unions | 2, 3 | `Result[String?, int]` fixture |
| Nested generic arguments | 3 | `Array[Result[int, String]]` fixture |
| Overload resolution | 2 | argument-position resolves against the single callee's parameter type (no overloading exists) |
| Lambdas | 2 | lambda-return fixture (automatic via `current_function` save/restore) |
| Default parameter values (payload-bearing) | 2 | `default_value` fixture + constant-folding doctest |
| Explicit `Result[int, String].Ok(1)` remains valid | 2 | regression — all epic #1595 fixtures stay green |
| Grammar updated | 1, 5 | `GRAMMAR.md` normative production |
| Formatter preserves shorthand | 1, 5 | `format/contextual_tagged_union` round-trip |
| Syntax highlighting | 5 | semantic-token fixture |
| Completion lists cases from expected union | 5 | `completion/tagged_union_cases/contextual_shorthand.cfg` |
| Hover / signature / rename / refactoring | 5 | `lsp/contextual_tagged_union.fs` assertions |
| Docgen | 5 | docgen doctest |
| Parser regression coverage (`.5`, `t.0`, member access) | 1 | parser regression fixtures |
| Native strict build + full suite | 5 | wrapper `build_summary` success + doctest `Status: SUCCESS!` |

## Verification commands (cheat sheet)

```sh
# Focused iteration (any task)
python3 scripts/agent_build.py --backend ninja --test --case "*ContextualTaggedUnion*"

# Pre-PR native strict (per task)
python3 scripts/agent_build.py --test --case "*ContextualTaggedUnion*"

# Epic closeout full suite
python3 scripts/agent_build.py --compiler-cache ccache --jobs 4 --test \
  --progress-file /tmp/foundry-test-progress.jsonl

# Regenerate fixtures after intentional format changes only
./bin/foundry.* --headless test generate-format-fixtures modules/foundry_script/tests/scripts/format/contextual_tagged_union
./bin/foundry.* --headless test generate-fixtures modules/foundry_script/tests/scripts
```

## External references

- Parent epic: [#1595](https://github.com/cafecito-games/Foundry/issues/1595) (closed)
- This issue: [#1606](https://github.com/cafecito-games/Foundry/issues/1606)
- Sibling follow-ups: [#1605](https://github.com/cafecito-games/Foundry/issues/1605) (runtime reification),
  [#1607](https://github.com/cafecito-games/Foundry/issues/1607) (`JsonResult[T]` migration)
- Design spec: `docs/superpowers/specs/2026-08-02-foundry-script-generic-tagged-unions-design.md`
- Parent plan: `docs/superpowers/plans/2026-08-02-foundry-script-generic-tagged-unions.md`

## Publication and implementation handoff check

Before implementation begins, verify:

- every requirement in the issue body's "Semantic matrix" and "Mechanical acceptance" sections maps to a task above;
- the resolved design decisions in this plan are recorded in the design spec annex (or referenced from it) so the
  spec stays the normative source;
- every task has a failing-test-first step, a focused file boundary, focused verification, native pre-PR
  verification, and a commit boundary;
- no task changes the runtime Array representation introduced by #1595;
- no task adds unconstrained `Result.Ok(1)` constructor inference;
- the explicit `Result[int, String].Ok(1)` form remains the unambiguous fallback and continues to pass every
  existing generic-tagged-union fixture unchanged.
