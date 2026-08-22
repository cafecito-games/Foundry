# Foundry Script Explicit Declarations Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make every user-written Foundry Script declaration explicitly typed or explicitly inferred, while preserving hard contextual inference for loops, patterns, and property accessors.

**Architecture:** Convert the repository's existing Foundry Script sources under the legacy parser first, then make the parser reject omitted declaration syntax and make the analyzer normalize contextual bindings. Keep the existing AST representation (`datatype_specifier` versus `infer_datatype`) and treat the neither-set state only as parser recovery. Remove obsolete warning and editor-setting surfaces, update every source generator, and preserve gradual type-system coverage through the raw-`Array` completeness witness.

**Tech Stack:** C++17, Foundry Script parser/analyzer/formatter, doctest, script fixture corpus, SCons through `scripts/agent_build.py`, class-reference XML, ISO 14977 EBNF.

---

## Source of truth and ordering constraints

Implement against [the approved design](../specs/2026-08-21-foundry-script-explicit-declarations-design.md). The third design review approved the spec and added two implementation guardrails: emit `raw_source` only for gradual union-pilot cells, and prove the `5U` payload from that declaration's array literal rather than from the boundary argument.

The source conversion must precede unconditional parser enforcement. Intermediate commits may use the pre-change binary to run script fixtures, but the final generated `.out` files must come from the exact post-change binary built in this worktree. Do not add a compatibility setting or ship a migration utility.

All agent-driven builds use `python3 scripts/agent_build.py`. Use `--backend ninja` for iteration if its prerequisites are present; use the native default backend before handoff.

## File map

### Language front end

- `modules/foundry_script/fs_parser.cpp` — enforce typed/inferred declarations, typed signals, return arrows, destructuring `:=`, and recovery.
- `modules/foundry_script/fs_parser.h` — add only parser helpers needed to keep the enforcement paths consistent.
- `modules/foundry_script/fs_analyzer.cpp` — skip parser-diagnosed recovery assignables, preserve `:=` semantics, normalize loop types, and remove untyped warnings.
- `modules/foundry_script/fs_analyzer_surface.cpp` — remove signal-parameter untyped warnings after parser enforcement.
- `modules/foundry_script/fs_warning.h` and `modules/foundry_script/fs_warning.cpp` — remove `UNTYPED_DECLARATION` and default `INFERENCE_ON_VARIANT` to `IGNORE`.
- `modules/foundry_script/fs_format.cpp` — canonicalize every inference form to compact `:=`.
- `modules/foundry_script/GRAMMAR.md` — make all changed productions normative in the same parser commit.

### Generated source and settings

- `modules/foundry_script/fs_editor.cpp` — always preserve template types and generate fully typed functions and overrides.
- `modules/foundry_script/language_server/fs_workspace.cpp` — always add `-> void` to workspace-created functions.
- `editor/script/script_text_editor.cpp` — stop passing an untyped-generation choice to node-drop source generation.
- `editor/settings/editor_settings.cpp` — remove `text_editor/completion/add_type_hints`.
- `doc/classes/EditorSettings.xml` — remove the obsolete setting member.
- `doc/classes/ProjectSettings.xml` — remove the untyped-warning member, revise inferred-warning prose, and change the Variant-inference default.

### Behavioral tests and completeness gate

- `modules/foundry_script/tests/test_foundry_script.cpp` — parser, analyzer, metadata, settings, and generator doctests.
- `modules/foundry_script/tests/test_format.h` — formatter round trips and whitespace normalization.
- `modules/foundry_script/tests/test_lsp.h` — parser-recovery completion and typed workspace edits.
- `modules/foundry_script/tests/fs_test_runner.cpp` — stop special-casing the removed warning.
- `modules/foundry_script/tests/test_suite_language_fixture.h` — update warning-policy commentary and fixtures if it names the removed warning.
- `modules/foundry_script/tests/fs_type_completeness_union_adapter.cpp` — emit the raw-array gradual witness.
- `modules/foundry_script/tests/test_type_completeness_union_pilot.h` — recognize the new syntactic and resolved proof.
- `modules/foundry_script/tests/test_type_completeness_gate_safety.h` — retarget diagnostic plumbing from `UNTYPED_DECLARATION`.
- `modules/foundry_script/tests/scripts/` — convert checked-in `.fs` sources and regenerate behavioral `.out` files.

### Repository source conversion

- Every tracked `*.fs` outside `thirdparty/`, including Android test assets, grammar/highlighting fixtures, editor templates, and `misc/foundry_perf/bench.fs`.
- C++ raw strings and source-building code found by `rg -l 'func |var |const |signal ' --glob '*.{cpp,h}'` and confirmed to contain Foundry Script.
- Foundry Script examples in `modules/foundry_script/README.md`, `doc/`, and module class-reference XML.
- Generated translations under `doc/translations/` only through the repository's translation workflow; never hand-edit `.po` entries.

## Task 1: Capture the legacy baseline and source inventory

**Files:**
- Modify: none

- [ ] **Step 1: Verify tools, binary identity, and clean tracked state**

Run:

```bash
scons --version
pre-commit --version
git status --short
ls -l bin/foundry.*
git rev-parse HEAD
```

Expected: SCons and pre-commit report versions, a legacy editor binary exists, and no tracked user changes are overwritten. Preserve all unrelated untracked files.

- [ ] **Step 2: Record every legacy declaration family before rewriting**

Use the same conservative patterns used for the design inventory:

```bash
rg -n --glob '*.fs' '^\s*(?:@[A-Za-z_][A-Za-z0-9_]*(?:\([^\n]*\))?\s*)*(?:(?:static|final)\s+)*var\s+(?:\([^\n]*\)|[A-Za-z_][A-Za-z0-9_]*)\s*(?:=|$)'
rg -n --glob '*.fs' '^\s*(?:(?:static|final)\s+)*const\s+[A-Za-z_][A-Za-z0-9_]*\s*='
rg -n --glob '*.fs' '^\s*(?:(?:static|abstract|async|final)\s+)*func\b' | awk 'index($0, "->") == 0'
```

Expected: the output is non-empty and includes representative analyzer, runtime, LSP, formatter, Android, documentation-fixture, and performance sources. Retain the terminal output in the implementation session for comparison with Task 10.

- [ ] **Step 3: Capture the legacy test baseline**

Run:

```bash
./bin/foundry.* --headless test fixtures '*' --pass text
./bin/foundry.* --headless test run --case '*TypeCompleteness*' --force-colors
```

Expected: both commands pass with the existing binary. Keep the exact binary path and `git rev-parse HEAD` in the implementation log so no later fixture generation is mistaken for the legacy baseline.

- [ ] **Step 4: Commit nothing**

The inventory is session output only. Do not add a repository migration command or utility.

## Task 2: Convert tracked Foundry Script sources under the legacy parser

**Files:**
- Modify: all tracked `*.fs` files reported by Task 1, excluding vendored `thirdparty/` content unless Foundry directly owns and parses it
- Test: `modules/foundry_script/tests/scripts/**/*.fs`

- [ ] **Step 1: Apply the semantic rewrite policy in reviewable directory batches**

For each declaration, make exactly one of these source-level replacements:

```foundry
var stable = expression       # -> var stable := expression
const STABLE = expression     # -> const STABLE := expression
var dynamic = expression      # -> var dynamic: Variant = expression
var late = null               # -> var late: Variant = null, or the proven nullable domain type
var uninitialized             # -> var uninitialized: Variant, or the proven domain type
func f(value):                # -> func f(value: Variant) -> void:
func f(value = 1):            # -> func f(value := 1) -> void:
var fn = func(value):         # -> var fn := func(value: Variant) -> void:
var (left, right) = pair      # -> var (left, right) := pair
signal changed(value)         # -> signal changed(value: Variant)
func f(...values):            # -> func f(...values: Array[Variant]) -> void:
```

Use `:=` only when later assignments and calls agree with the initializer's hard type. Use an explicit domain type or `Variant` for intentionally wide storage. When the initializer is statically `Variant`, use `: Variant =` so the forced corpus warning policy does not create unrelated `INFERENCE_ON_VARIANT` output.

- [ ] **Step 2: Review behavior-sensitive fixture directories first**

Inspect every changed declaration under these directories in the staged diff:

```text
modules/foundry_script/tests/scripts/analyzer/
modules/foundry_script/tests/scripts/runtime/
modules/foundry_script/tests/scripts/bytecode/
```

Expected: tests about dynamic assignment, unsafe lines, strict dynamic checks, raw containers, nullability, Variant calls, and warning counts use explicit `Variant` where dynamic behavior is the subject rather than accidentally becoming hard-inferred.

- [ ] **Step 3: Run each converted fixture family with the legacy binary**

Run after each directory batch:

```bash
./bin/foundry.* --headless test fixtures analyzer
./bin/foundry.* --headless test fixtures runtime
./bin/foundry.* --headless test fixtures bytecode
```

Expected: every selected fixture passes both corpus passes. Do not regenerate `.out` files to hide a semantic change; preserve the old observation or make the intended changed observation explicit in the same reviewed fixture diff.

- [ ] **Step 4: Convert the remaining tracked `.fs` sources and validate them**

Convert completion, LSP, refactor, formatter inputs, Android assets, grammar fixtures, editor templates, demos, and performance scripts. Then run:

```bash
./bin/foundry.* --headless test run --suite '*[Modules][FoundryScript][Completion]*' --suite '*[Modules][FoundryScript][LSP*' --force-colors
```

Expected: the focused C++ suites, which own the completion/LSP/refactor inputs excluded from the executable script corpus, pass. A no-match result means the filter must be corrected to a case/suite name discovered with `test run --help`, not ignored.

- [ ] **Step 5: Confirm no mechanically detectable legacy forms remain in tracked `.fs` files**

Run the Task 1 inventory commands again and inspect every remaining row. Remaining hits must be parser-error fixtures whose purpose is to reject the legacy spelling; verify each one has an expected targeted diagnostic in Task 5.

- [ ] **Step 6: Commit the script conversion**

```bash
git add -- '*.fs'
git commit -m 'Migrate Foundry Script declarations'
```

Expected: the commit contains only reviewed source conversion, without the scratch ledger or generated `.out` churn.

## Task 3: Convert embedded source, generators, and documentation examples

**Files:**
- Modify: Foundry-owning `*.cpp` and `*.h` files containing embedded Foundry Script
- Modify: `modules/foundry_script/README.md`
- Modify: Foundry Script examples under `doc/` and module `doc_classes/`
- Test: C++ doctests that parse or analyze embedded source

- [ ] **Step 1: Convert embedded Foundry Script with the same semantic policy**

Locate candidates with:

```bash
rg -l '(^|["\\n])\s*(func|var|const|signal)\b' --glob '*.{cpp,h}' --glob '!thirdparty/**'
```

For each actual Foundry Script raw string or concatenated source builder, add parameter and return types, use `:=` for intentional hard inference, and use explicit `Variant` for gradual test cases. Do not rewrite C++ declarations that merely match the search.

- [ ] **Step 2: Re-derive embedded diagnostic coordinates from the rewritten source**

For every test with pinned `line`, `column`, `start_column`, `end_column`, or display-column values, calculate positions from the new source text or update the asserted literal to the parser/analyzer's observed coordinate. Run the narrowest owning doctest after each group.

Expected: coordinate assertions describe the rewritten source exactly; none retain a number merely because it was correct before inserted type text shifted the token.

- [ ] **Step 3: Convert documentation examples**

Update examples so ordinary declarations use `: Type`, `:=`, explicit `Variant`, and full `->` returns. In gradual-typing prose, explain that dynamic boundaries and raw projections can remain soft even though declarations are explicit. Preserve intentionally invalid examples only when their surrounding text identifies the parser error.

- [ ] **Step 4: Build and run the embedded-source owners before parser enforcement**

Run:

```bash
python3 scripts/agent_build.py --backend ninja --test --case '*FoundryScript*'
```

Expected: the build and selected tests pass. If Ninja prerequisites are unavailable, rerun the same command without `--backend ninja`.

- [ ] **Step 5: Commit embedded-source and documentation conversion**

```bash
git add -p -- '*.cpp' '*.h' '*.md' '*.xml'
git commit -m 'Type embedded Foundry Script examples'
```

Before committing, inspect `git diff --cached --name-only` and unstage unrelated user files or generated translations.

## Task 4: Remove optional type-hint generation and always emit valid source

**Files:**
- Modify: `modules/foundry_script/fs_editor.cpp:86-118,560-585,5470-5500`
- Modify: `modules/foundry_script/language_server/fs_workspace.cpp:70-105`
- Modify: `editor/script/script_text_editor.cpp:2695-2715`
- Modify: `editor/settings/editor_settings.cpp:825-845`
- Modify: `doc/classes/EditorSettings.xml:1485-1505`
- Test: `modules/foundry_script/tests/test_foundry_script.cpp`
- Test: `modules/foundry_script/tests/test_lsp.h`

- [ ] **Step 1: Write failing generator tests**

Add observable tests that set `text_editor/completion/add_type_hints` to `false` when the setting still exists, invoke `FSLanguage::make_template()`, `FSLanguage::make_function()`, override completion, node-drop generation, and `FSWorkspace` function creation, and parse the generated text. The assertions must check generated results, not source substrings in implementation files:

```cpp
CHECK_EQ(language.make_function("Node", "created", { "value: int" }),
		"func created(value: int) -> void:\n\tpass # Replace with function body.\n");
FSParser parser;
CHECK_EQ(parser.parse(generated_source, "user://generated.fs", false), OK);
CHECK(parser.get_errors().is_empty());
```

Also assert through `EditorSettings::has_setting()` that the obsolete setting is absent after registration.

- [ ] **Step 2: Run the focused tests and verify failure**

```bash
python3 scripts/agent_build.py --backend ninja --test --case '*make_function*' --case '*Workspace*function*'
```

Expected: at least one test fails because current code still strips or conditionally omits type syntax. Use the actual case names introduced in Step 1 if these discovery patterns match no tests.

- [ ] **Step 3: Remove every conditional generation branch**

Make `make_template()` perform only placeholder/indentation replacement. Make `make_function()` preserve parsed argument types and always append ` -> void`. Make override and workspace generation always append typed arguments and ` -> void`; use `Variant` when metadata supplies no narrower type. Remove the setting read from node-drop generation and call the typed generation path unconditionally.

Replace `make_function()`'s conditional body with this complete typed construction:

```cpp
String result = "func " + p_name + "(";
for (int i = 0; i < p_args.size(); i++) {
	if (i > 0) {
		result += ", ";
	}
	const int separator = p_args[i].find(":");
	const String name = (separator >= 0 ? p_args[i].left(separator) : p_args[i]).strip_edges();
	const String declared_type = separator >= 0 ? p_args[i].substr(separator + 1).strip_edges() : String();
	result += name + ": " + (declared_type.is_empty() ? "Variant" : declared_type);
}
result += ") -> void:\n" + _get_indentation() + "pass # Replace with function body.\n";
return result;
```

Delete `_initial_set("text_editor/completion/add_type_hints", ...)` and its `EditorSettings.xml` member.

- [ ] **Step 4: Run generator, completion, LSP, and settings tests**

```bash
python3 scripts/agent_build.py --backend ninja --test --suite '*[Modules][FoundryScript][Completion]*' --suite '*[Modules][FoundryScript][LSP*'
```

Expected: all generated-source tests pass and every generated snippet parses.

- [ ] **Step 5: Commit the generator change**

```bash
git add modules/foundry_script/fs_editor.cpp modules/foundry_script/language_server/fs_workspace.cpp editor/script/script_text_editor.cpp editor/settings/editor_settings.cpp doc/classes/EditorSettings.xml modules/foundry_script/tests/test_foundry_script.cpp modules/foundry_script/tests/test_lsp.h
git commit -m 'Always generate typed Foundry Script'
```

## Task 5: Enforce explicit declaration syntax in the parser and formatter

**Files:**
- Modify: `modules/foundry_script/fs_parser.cpp:2431-2848,3308-3375,4043-4105,6100`
- Modify: `modules/foundry_script/fs_parser.h:2499-2543`
- Modify: `modules/foundry_script/fs_format.cpp:1736-2210,3160-3205`
- Modify: `modules/foundry_script/GRAMMAR.md`
- Test: `modules/foundry_script/tests/test_foundry_script.cpp`
- Test: `modules/foundry_script/tests/test_format.h`

- [ ] **Step 1: Add parser acceptance and rejection doctests**

Use `parse_source_errors()`, `has_parser_error()`, and `count_parser_errors()` to cover members, locals, `static`, `final`, properties, export/onready declarations, constants, fixed/default/rest parameters, signals, named functions, constructors, static initializers, abstract/trait/enum methods, lambdas, and destructuring. Pin these focused diagnostics:

```text
Variable "enabled" must declare a type with ": Type" or infer it with ":=".
Variable "node" initialized with "null" must declare a type with ": Type".
Constant "MAX_COUNT" must declare a type with ": Type" or infer it with ":=".
Parameter "value" must declare a type or infer it from a default value with ":=".
Signal parameter "amount" must declare a type.
Function "update" must declare a return type with "-> Type" or "-> void".
```

Add one recovery source containing an invalid declaration followed by a valid typed declaration and function. Assert the targeted omission occurs exactly once and the later nodes exist in the parse tree.

Add paired null cases. `var node = null` must produce only the focused parser diagnostic above. `var node := null` must parse without that diagnostic and, when analyzed, produce exactly the existing `Cannot infer the type ... because the value is "null".` analyzer error. Add a constructor/static-initializer pair proving omission is a parser error, `-> void` is accepted, and any other written return type retains the existing analyzer rejection.

Keep the existing signal-default recovery test and strengthen it to assert that `signal changed(value = 1)` and `signal changed(value := 1)` each produce only `Signal parameters cannot have a default value.` rather than a stacked missing-type diagnostic.

- [ ] **Step 2: Add formatter failures and normalization tests**

Add a canonical round-trip test:

```cpp
CHECK_EQ(format_or_fail(
		"const LIMIT : = 2\n"
		"func use(value : = 1) -> void:\n"
		"\tvar (left, right) : = (value, LIMIT)\n"),
		"const LIMIT := 2\n"
		"func use(value := 1) -> void:\n"
		"\tvar (left, right) := (value, LIMIT)\n");
```

Also assert formatting refuses each invalid legacy spelling because parsing fails.

- [ ] **Step 3: Run the new tests and verify they fail**

```bash
python3 scripts/agent_build.py --backend ninja --test --case '*explicit declaration*' --case '*inference spelling*'
```

Expected: rejection and destructuring-format tests fail against the legacy parser/printer.

- [ ] **Step 4: Implement ordinary declaration enforcement with recovery**

In `parse_variable()`, remember whether a colon/inference marker was consumed. Continue parsing a plain initializer for recovery, then emit the variable diagnostic. Special-case only a literal `null` initializer for the focused type-only diagnostic. Leave `var x := null` to analyzer inference.

In `parse_constant()` and `parse_parameter()`, retain initializer/default recovery but emit the targeted omission. A fixed parameter with plain `=` consumes its default and reports the parameter error once. A bare rest parameter reports the parameter error because rest parameters cannot infer.

The recovery invariant after parsing is:

```cpp
const bool is_parser_recovery_assignable =
		assignable->datatype_specifier == nullptr && !assignable->infer_datatype;
```

Do not synthesize `Variant` or flip `infer_datatype`; the analyzer uses this exact state to recognize a parser-diagnosed node.

- [ ] **Step 5: Implement signal-specific typed parameters**

Parse signal parameter names followed by `:` and a type. If `=` or `:=` follows the name, consume the expression and emit only `Signal parameters cannot have a default value.` for that parameter. Otherwise an omitted colon emits only `Signal parameter "name" must declare a type.`. Do not route valid signal parameters through default-capable ordinary parameter grammar.

- [ ] **Step 6: Require function and lambda return annotations**

In `parse_function_signature()`, require `->` plus a parsed return type for every named function and lambda, including bodyless abstract methods. Preserve completion contexts after `)` and `->`, consume the body or declaration newline after reporting an omission, and let the analyzer retain its existing rule that constructors/static initializers accept only `-> void`.

- [ ] **Step 7: Require `:=` for destructuring and preserve contextual syntax**

After the destructuring binding list, consume `COLON` and then `EQUAL`; report a targeted marker diagnostic while still consuming a plain `=` initializer for recovery. Keep each binding's `infer_datatype = true`. Do not alter `parse_for()`, match-pattern syntax, or inline property setter parsing.

- [ ] **Step 8: Canonicalize the formatter**

Change `print_variable_destructure()` from `" = "` to `" := "`. Retain compact `:=` output in variables, constants, parameters, and lambdas. Remove ordinary printer branches that only represent formerly valid gradual syntax, while allowing parser recovery trees to print without crashing.

- [ ] **Step 9: Update the normative grammar**

In `GRAMMAR.md`, make variable/constant/parameter/signal/function/lambda/destructuring productions match the approved spec; retain `COLON` then `EQUAL` tokens and existing accessor/loop/pattern productions. Update prose and examples that describe optional annotations or returns.

- [ ] **Step 10: Run parser and formatter tests**

```bash
python3 scripts/agent_build.py --backend ninja --test --case '*explicit declaration*' --case '*inference spelling*' --suite '*[Format]*'
```

Expected: all new acceptance, rejection, recovery, completion-context, and round-trip tests pass.

- [ ] **Step 11: Commit parser, formatter, grammar, and tests together**

```bash
git add modules/foundry_script/fs_parser.cpp modules/foundry_script/fs_parser.h modules/foundry_script/fs_format.cpp modules/foundry_script/GRAMMAR.md modules/foundry_script/tests/test_foundry_script.cpp modules/foundry_script/tests/test_format.h
git commit -m 'Require explicit Foundry Script declarations'
```

## Task 6: Normalize contextual inference and remove obsolete warnings

**Files:**
- Modify: `modules/foundry_script/fs_analyzer.cpp:4740-5010,5349-5590,5715-5840,6260-6360`
- Modify: `modules/foundry_script/fs_analyzer_surface.cpp:1880-1900`
- Modify: `modules/foundry_script/fs_warning.h:55-150`
- Modify: `modules/foundry_script/fs_warning.cpp:75-260`
- Modify: `modules/foundry_script/tests/fs_test_runner.cpp:335-352`
- Modify: `modules/foundry_script/tests/test_suite_language_fixture.h`
- Modify: `doc/classes/ProjectSettings.xml`
- Test: `modules/foundry_script/tests/test_foundry_script.cpp`

- [ ] **Step 1: Add analyzer tests for explicit and contextual typing**

Parse/analyze valid sources proving:

```foundry
func variant_value() -> Variant:
	return null

func exercise(items: Array[Variant], raw: Array) -> void:
	var concrete := 1
	var dynamic := variant_value()
	for typed in items:
		var keep: Variant = typed
	for projected in raw:
		var keep: Variant = projected
```

Assert `concrete`, `dynamic`, `typed`, and `projected` have hard types with `type_source == ANNOTATED_INFERRED`; `dynamic`, `typed`, and `projected` are `Variant`. Add a soft/unresolved iterable case that keeps its upstream analyzer error while the loop binding becomes hard `Variant`. Add pattern payload, catch-all pattern, raw structural source, tuple destructuring, and inline property accessor assertions.

- [ ] **Step 2: Add warning-level tests**

Assert `INFERENCE_ON_VARIANT` is absent by default but appears when its setting is explicitly `WARN`. Assert `INFERRED_DECLARATION` still covers written/contextual inference. Assert `...values: Array` and `...values: Array[Variant]` emit no inferred-declaration warning.
Also retain the existing type-import exemption: `const ImportedType := preload("res://imported_type.fs")` must not emit `INFERRED_DECLARATION` when the initializer resolves to a metatype.

- [ ] **Step 3: Run the analyzer tests and verify failure**

```bash
python3 scripts/agent_build.py --backend ninja --test --case '*contextual inference*' --case '*Variant inference warning*'
```

Expected: soft loop normalization and default warning-level assertions fail before implementation.

- [ ] **Step 4: Skip semantic work for parser-diagnosed assignables**

At the start of `resolve_assignable()`, detect `datatype_specifier == nullptr && !infer_datatype`. If the parser already has errors, assign a hard recovery `Variant` only as needed for downstream tree stability, then return without inference, `INFERRED_DECLARATION`, or a second omission error. Do not apply this shortcut to loops or patterns.

- [ ] **Step 5: Normalize contextual bindings**

In `resolve_for()`, after determining the element type for an unannotated loop, preserve a hard concrete element type but replace every non-hard or unresolved element result with:

```cpp
variable_type = FSParser::DataType::get_variant_type();
variable_type.type_source = FSParser::DataType::ANNOTATED_INFERRED;
```

For a hard contextual result, set `type_source = ANNOTATED_INFERRED` before assigning it to the binding. Preserve `mark_node_unsafe()`, strict-dynamic conversion decisions, and the original error on an unresolved iterable. Apply the same hardening rule at match/pattern binding assignment sites without changing pattern grammar.

- [ ] **Step 6: Remove `UNTYPED_DECLARATION` and revise warning defaults**

Delete the enum entry, message switch arm, warning-name table entry, default-level entry, analyzer emission sites, fixture-runner exception, project setting member, and documentation references. Keep enum/table ordering aligned. Change only `INFERENCE_ON_VARIANT`'s default entry from `ERROR` to `IGNORE`. Revise `INFERRED_DECLARATION` prose so it no longer compares itself to the removed warning.

- [ ] **Step 7: Run analyzer and warning tests**

```bash
python3 scripts/agent_build.py --backend ninja --test --case '*contextual inference*' --case '*declaration warning*' --case '*strict dynamic*'
```

Expected: hard Variant normalization, upstream error preservation, rest-parameter silence, and opt-in Variant warning behavior pass. Existing strict-dynamic assignment observations remain unchanged.

- [ ] **Step 8: Commit analyzer semantics and warning removal**

```bash
git add modules/foundry_script/fs_analyzer.cpp modules/foundry_script/fs_analyzer_surface.cpp modules/foundry_script/fs_warning.h modules/foundry_script/fs_warning.cpp modules/foundry_script/tests/fs_test_runner.cpp modules/foundry_script/tests/test_suite_language_fixture.h modules/foundry_script/tests/test_foundry_script.cpp doc/classes/ProjectSettings.xml
git commit -m 'Harden contextual Foundry Script bindings'
```

## Task 7: Retarget the type-completeness gradual witness

**Files:**
- Modify: `modules/foundry_script/tests/fs_type_completeness_union_adapter.cpp:83-100,927-970`
- Modify: `modules/foundry_script/tests/test_type_completeness_union_pilot.h:206-365`
- Modify: `modules/foundry_script/tests/test_type_completeness_gate_safety.h:403-1010`
- Test: existing type-completeness manifest, census, parity, and gate-safety doctests

- [ ] **Step 1: Add failing classifier coverage for the raw-array witness**

Add a generated gradual program whose test body is exactly shaped as:

```foundry
func test() -> void:
	var raw_source: Array = [5U]
	@warning_ignore("unsafe_call_argument")
	var stored: Variant = accept(raw_source[0])
```

For reflective-write cells, use `holder.set(&"value", raw_source[0])`. Assert the syntactic and resolved fingerprints return `gradual`, the boundary fingerprint remains `argument_binding` or `reflective_write`, and the stable case ID remains `text_gradual_argument_binding` for that coordinate.

- [ ] **Step 2: Run the union-pilot tests and verify failure**

```bash
python3 scripts/agent_build.py --backend ninja --test --case '*TypeCompleteness UnionPilot*'
```

Expected: current classifiers reject the subscript and still expect `supply(5U)`.

- [ ] **Step 3: Change the adapter without perturbing other cells**

Return `raw_source[0]` for `source_proof == "gradual"`. Remove the invalid untyped `supply` function. Emit `\tvar raw_source: Array = [5U]\n` only when the source proof is gradual; do not emit it for `static_member`, `numeric_constant`, `erased`, or `variant`, because `UNUSED_VARIABLE` would change their warning ledgers.

- [ ] **Step 4: Rewrite syntactic and resolved classifiers**

Recognize a non-attribute `SubscriptNode` with base identifier `raw_source` and literal integer index `0`. Find the local `raw_source`, prove its annotation is raw `Array`, and inspect its initializer array's single element with `union_pilot_is_unsigned_five()`; do not call that predicate on the boundary argument. Finally prove the subscript datatype is `kind == VARIANT`, `type_source == UNDETECTED`, and `!is_hard_type()`.

Keep the existing literal/call shapes for `numeric_constant`, `erased`, and `variant`. Do not change manifest coordinates, case-ID construction, `gradual_to_erased_parity`, or `gradual_to_variant_parity`.

- [ ] **Step 5: Retarget gate-safety diagnostic plumbing**

Replace diagnostic-capture, suppression, column-mapping, and severity-mutation cases that use `UNTYPED_DECLARATION` with `INFERRED_DECLARATION`, paired with `UNUSED_VARIABLE` where the test requires two warnings. Delete only tests whose sole observable behavior was the removed warning. Update expected code strings and computed columns from the valid typed fixture source.

- [ ] **Step 6: Run the complete gate suite**

```bash
python3 scripts/agent_build.py --backend ninja --test --case '*TypeCompleteness*'
```

Expected: census, baseline comparison, both parity relations, stable case IDs, diagnostic reconciliation, and severity-mutation tests pass with no `UNTYPED_DECLARATION` input.

- [ ] **Step 7: Commit the witness migration**

```bash
git add modules/foundry_script/tests/fs_type_completeness_union_adapter.cpp modules/foundry_script/tests/test_type_completeness_union_pilot.h modules/foundry_script/tests/test_type_completeness_gate_safety.h
git commit -m 'Preserve gradual type completeness coverage'
```

## Task 8: Close tooling recovery, reflection, and runtime coverage

**Files:**
- Modify: `modules/foundry_script/tests/test_foundry_script.cpp`
- Modify: `modules/foundry_script/tests/test_lsp.h`
- Modify: completion/LSP/refactor fixtures under `modules/foundry_script/tests/scripts/`
- Modify: runtime/analyzer fixtures under `modules/foundry_script/tests/scripts/`

- [ ] **Step 1: Add recovery completion tests**

Request completion after a declaration name, after `:`, after a parameter, after `)`, and after `->` in incomplete editor buffers. Assert the completion context remains the corresponding type/property/return context even though a completed declaration without syntax would be a parser error. Assert LSP publishes exactly the parser omission once and remains able to inspect later symbols.

- [ ] **Step 2: Add metadata and runtime fixtures**

Cover reflected property types, method parameter/return/default metadata, typed signals, callable/lambda signatures, inferred parameter call checking, inferred return assignment, explicit `Variant` reassignment, and typed rest arguments. Each fixture must assert runtime output or reflected metadata, not source text.

- [ ] **Step 3: Run focused tooling and fixture tests**

```bash
python3 scripts/agent_build.py --backend ninja --test --suite '*[Modules][FoundryScript][Completion]*' --suite '*[Modules][FoundryScript][LSP*'
./bin/foundry.* --headless test fixtures explicit_declaration contextual_binding typed_signal typed_rest
```

Expected: C++ suites pass. Fixture patterns must match at least one checked-in path and all selected fixtures pass both text and bytecode passes.

- [ ] **Step 4: Commit coverage additions**

```bash
git add modules/foundry_script/tests/test_foundry_script.cpp modules/foundry_script/tests/test_lsp.h modules/foundry_script/tests/scripts
git commit -m 'Cover explicit declaration tooling behavior'
```

## Task 9: Build the new binary and regenerate owned artifacts

**Files:**
- Modify: affected `modules/foundry_script/tests/scripts/**/*.out`
- Modify: affected formatter `modules/foundry_script/tests/scripts/format/**/expected.fs`
- Modify: generated class-reference or translation artifacts produced by repository workflows

- [ ] **Step 1: Produce a strict native binary in this worktree**

```bash
python3 scripts/agent_build.py
```

Expected: exit code 0 and the final `[agent-build] RESULT: success` line names a present binary whose identity changed for this invocation. Use that exact path for Steps 2 and 3.

- [ ] **Step 2: Regenerate script fixtures with that exact binary**

```bash
./bin/foundry.* --headless test generate-fixtures modules/foundry_script/tests/scripts
```

Expected: generated outputs reflect parser/analyzer changes. Review every diff. `INFERENCE_ON_VARIANT` churn is limited to fixtures that intentionally infer a hard Variant; unrelated churn indicates a missed explicit `Variant` conversion or stale binary.

- [ ] **Step 3: Regenerate formatter fixtures with the same binary**

```bash
./bin/foundry.* --headless test generate-format-fixtures modules/foundry_script/tests/scripts/format
```

Expected: inferred declarations and destructuring use compact `:=`; invalid-format fixtures remain intentionally invalid and carry their matching expectations.

- [ ] **Step 4: Regenerate documentation and translations through existing workflows**

Run the repository's generated-doc dry run through pre-commit first:

```bash
pre-commit run --all-files
```

If it reports generated class-reference or translation drift, run the exact generator printed by that hook, then rerun the hook. Do not hand-edit `doc/translations/*.po`.

- [ ] **Step 5: Audit obsolete surfaces**

```bash
rg -n 'UNTYPED_DECLARATION|untyped_declaration|text_editor/completion/add_type_hints' --glob '!thirdparty/**'
```

Expected: no product, test, class-reference, fixture, or gate input references remain. Historical approved specs/reviews may retain the terms as design history and should not be rewritten.

- [ ] **Step 6: Commit regenerated artifacts**

```bash
git add modules/foundry_script/tests/scripts doc/classes doc/translations
git commit -m 'Regenerate explicit declaration fixtures'
```

Inspect the staged diff first and include only generated artifacts owned by this change.

## Task 10: Final migration audit and full verification

**Files:**
- Modify: only defects discovered by verification
- Test: complete C++ and Foundry Script suites, pre-commit hooks, source audit

- [ ] **Step 1: Prove no valid repository source uses removed syntax**

Re-run the Task 1 declaration inventory. Parse every tracked `.fs` file through the formatter/corpus sweeps. The only remaining legacy spellings may be strings in negative parser tests and intentionally invalid fixture inputs whose expected diagnostic is the new targeted error.

- [ ] **Step 2: Run the native strict build and full suite in one authoritative invocation**

```bash
DISPLAY=:1 python3 scripts/agent_build.py --test
```

Expected: exit code 0, strict native SCons build succeeds, GUI-dependent tests run rather than silently skip, and the wrapper reports `RESULT: success`.

- [ ] **Step 3: Run pre-commit across the repository**

```bash
pre-commit run --all-files
```

Expected: formatting, linting, spelling, XML/class-reference, grammar, and generated-doc checks all pass.

- [ ] **Step 4: Run the repository test-authoring policy audit**

```bash
git grep -nE '\.split\("(void|bool|String|private fun|const val|def )' -- '*.py'
git grep -n 'docs/superpowers' -- '*/test_*.py' '*/check_*.py'
git grep -n 'hasattr(' -- '*/test_*.py'
git grep -l 'assert' -- 'misc/checks/check_*.py'
```

Expected: this change introduces no new output. Do not add a source-text assertion as a shortcut for parser, analyzer, generator, or settings behavior.

- [ ] **Step 5: Review the final diff against every design requirement**

Confirm variable, constant, parameter, signal, return, lambda, rest, and destructuring omissions fail; loops/patterns/accessors remain contextual; hard Variant inference is valid; parser recovery is singular; settings/warnings are removed; grammar and docs agree; completeness coordinates and parity stay fixed; and no migration tooling ships.

- [ ] **Step 6: Commit verification fixes, if any**

```bash
git add -p
git commit -m 'Polish explicit declaration enforcement'
```

Skip this commit when verification required no fixes. Finish with `git status --short` and preserve all unrelated user-owned untracked files.
