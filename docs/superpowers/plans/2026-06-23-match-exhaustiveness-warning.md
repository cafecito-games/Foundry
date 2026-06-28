# Match Exhaustiveness Warning Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers-extended-cc:subagent-driven-development (recommended) or superpowers-extended-cc:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add two Foundry Script compiler warnings — `NON_EXHAUSTIVE_MATCH` (an enum/`bool` `match` that misses values with no `_` branch) and `MATCH_WITHOUT_DEFAULT` (any other `match` with no `_` branch).

**Architecture:** Register two new codes in the existing `GDScriptWarning` enum/tables, then add a `#ifdef DEBUG_ENABLED` analysis pass `check_match_exhaustiveness()` called at the end of `GDScriptAnalyzer::resolve_match`. The pass reads the already-reduced match-test `DataType`, classifies its domain (enum / `bool` = finite; everything else = non-finite), and emits the appropriate warning. Project settings auto-register via the existing `WARNING_MAX` loop. Behavior is validated with Foundry Script `.fs`/`.out` fixtures.

**Tech Stack:** C++ (Godot engine), SCons build, Foundry Script analyzer test-runner fixtures.

**Reference spec:** `docs/superpowers/specs/2026-06-23-match-exhaustiveness-warning-design.md`

**Build/test note:** This repo builds the editor binary with SCons. Per `CLAUDE.md`, on macOS:
`scons platform=macos target=editor dev_build=yes tests=yes` → binary `bin/godot.macos.editor.dev.<arch>`.
On linuxbsd: `python3 -m SCons platform=linuxbsd target=editor dev_build=yes tests=yes module_text_server_fb_enabled=yes -j$(nproc)` → `bin/godot.linuxbsd.editor.dev.x86_64`.
In all commands below, `./bin/godot.<...>` means whichever of these binaries your build produced. Warnings are compiled only when `DEBUG_ENABLED` is set, which `dev_build=yes` provides.

---

## File Structure

| File | Responsibility | Change |
| --- | --- | --- |
| `modules/foundry_script/gdscript_warning.h` | Warning code enum + default levels | Add 2 codes + 2 default-level entries |
| `modules/foundry_script/gdscript_warning.cpp` | Warning names + messages | Add 2 names + 2 message cases |
| `modules/foundry_script/gdscript_analyzer.h` | Analyzer method declarations | Declare `check_match_exhaustiveness` (DEBUG only) |
| `modules/foundry_script/gdscript_analyzer.cpp` | Match analysis | Implement helper + call from `resolve_match` |
| `doc/classes/ProjectSettings.xml` | Setting documentation | Add 2 `<member>` entries (alphabetical) |
| `modules/foundry_script/tests/scripts/analyzer/warnings/*.fs` + `*.out` | Behavior fixtures | Add 7 fixture pairs |

---

### Task 1: Register the two warning codes

**Goal:** Add `NON_EXHAUSTIVE_MATCH` and `MATCH_WITHOUT_DEFAULT` to the warning enum, default-level table, name table, and message switch so the engine compiles with the new codes available (no behavior yet).

**Files:**
- Modify: `modules/foundry_script/gdscript_warning.h` (enum near line 93; `default_warning_levels[]` near line 152)
- Modify: `modules/foundry_script/gdscript_warning.cpp` (`get_message()` near line 169; `names[]` near line 247)

**Acceptance Criteria:**
- [ ] Both codes exist in the `Code` enum, inserted **after** `ONREADY_WITH_EXPORT` and **before** the `#ifndef DISABLE_DEPRECATED` block (so the deprecated codes stay contiguous at the end and `FIRST_DEPRECATED_WARNING` is unaffected).
- [ ] `default_warning_levels[]`, `names[]` get matching entries in the same positions.
- [ ] `NON_EXHAUSTIVE_MATCH` default = `WARN`; `MATCH_WITHOUT_DEFAULT` default = `IGNORE`.
- [ ] Both `static_assert`s on array size vs `WARNING_MAX` still hold.
- [ ] Engine builds cleanly.

**Verify:** Build the engine; it must compile with no `static_assert` failure:
`scons platform=macos target=editor dev_build=yes tests=yes` → build succeeds.

**Steps:**

- [ ] **Step 1: Add enum entries** in `modules/foundry_script/gdscript_warning.h`. Find this region (line ~93):

```cpp
		ONREADY_WITH_EXPORT, // The `@onready` annotation will set the value after `@export` which is likely not intended.
#ifndef DISABLE_DEPRECATED
```

Change it to:

```cpp
		ONREADY_WITH_EXPORT, // The `@onready` annotation will set the value after `@export` which is likely not intended.
		NON_EXHAUSTIVE_MATCH, // A `match` over an enum or `bool` does not handle all values and has no wildcard `_` branch.
		MATCH_WITHOUT_DEFAULT, // A `match` over a non-finite-domain value has no wildcard `_` branch.
#ifndef DISABLE_DEPRECATED
```

- [ ] **Step 2: Add default levels** in the same file. Find (line ~152):

```cpp
		ERROR, // ONREADY_WITH_EXPORT // May not work as expected.
#ifndef DISABLE_DEPRECATED
		WARN, // PROPERTY_USED_AS_FUNCTION
```

Change to:

```cpp
		ERROR, // ONREADY_WITH_EXPORT // May not work as expected.
		WARN, // NON_EXHAUSTIVE_MATCH
		IGNORE, // MATCH_WITHOUT_DEFAULT // Requiring a default branch on open-domain matches is noisy; opt-in.
#ifndef DISABLE_DEPRECATED
		WARN, // PROPERTY_USED_AS_FUNCTION
```

- [ ] **Step 3: Add name entries** in `modules/foundry_script/gdscript_warning.cpp`. Find (line ~247):

```cpp
			PNAME("ONREADY_WITH_EXPORT"),
#ifndef DISABLE_DEPRECATED
			"PROPERTY_USED_AS_FUNCTION",
```

Change to:

```cpp
			PNAME("ONREADY_WITH_EXPORT"),
			PNAME("NON_EXHAUSTIVE_MATCH"),
			PNAME("MATCH_WITHOUT_DEFAULT"),
#ifndef DISABLE_DEPRECATED
			"PROPERTY_USED_AS_FUNCTION",
```

- [ ] **Step 4: Add message cases** in the same file. Find (line ~168):

```cpp
		case ONREADY_WITH_EXPORT:
			return R"("@onready" will set the default value after "@export" takes effect and will override it.)";
#ifndef DISABLE_DEPRECATED
```

Change to:

```cpp
		case ONREADY_WITH_EXPORT:
			return R"("@onready" will set the default value after "@export" takes effect and will override it.)";
		case NON_EXHAUSTIVE_MATCH:
			CHECK_SYMBOLS(2);
			return vformat(R"(The "match" statement does not cover all values of "%s". Unhandled: %s. Add the missing patterns or a "_" wildcard branch.)", symbols[0], symbols[1]);
		case MATCH_WITHOUT_DEFAULT:
			return R"(The "match" statement has no "_" wildcard branch; some values may go unhandled.)";
#ifndef DISABLE_DEPRECATED
```

- [ ] **Step 5: Build to verify the static_asserts hold and it compiles.**

Run: `scons platform=macos target=editor dev_build=yes tests=yes` (use the linuxbsd invocation from the header on Linux).
Expected: build completes with no errors (in particular no "Amount of default levels does not match" / "Amount of warning types don't match" static_assert failure).

- [ ] **Step 6: Commit.**

```bash
git add modules/foundry_script/gdscript_warning.h modules/foundry_script/gdscript_warning.cpp
git commit -m "feat(gdscript): Register match exhaustiveness warning codes"
```

---

### Task 2: Implement the exhaustiveness analysis

**Goal:** Add `check_match_exhaustiveness()` and call it from `resolve_match`, so the two warnings are actually emitted according to the spec's rules (finite-domain coverage, guard exclusion, constant-only coverage, safe bail-out).

**Files:**
- Modify: `modules/foundry_script/gdscript_analyzer.h` (private method declarations, near line 98)
- Modify: `modules/foundry_script/gdscript_analyzer.cpp` (`resolve_match`, line ~2733; add helper just after it)

**Acceptance Criteria:**
- [ ] `resolve_match` calls `check_match_exhaustiveness(p_match)` inside `#ifdef DEBUG_ENABLED` after all branches are resolved.
- [ ] Enum/`bool` match missing values with no unguarded `_` → `NON_EXHAUSTIVE_MATCH` listing the unhandled value names.
- [ ] Fully-covered enum / both-bool-values covered → no warning even without `_`.
- [ ] Any unguarded `_`/bind branch suppresses both warnings.
- [ ] Guarded branches (`guard_body != nullptr`) do not count toward coverage and a guarded wildcard does not count as default.
- [ ] An unguarded non-constant pattern suppresses `NON_EXHAUSTIVE_MATCH` (no false positive).
- [ ] Non-finite-domain match (int, String, Array, etc.) with no unguarded `_` → `MATCH_WITHOUT_DEFAULT`.
- [ ] Builds cleanly in both `DEBUG_ENABLED` and release (helper is fully inside `#ifdef DEBUG_ENABLED`).

**Verify:** Build succeeds; behavior is verified by the fixtures in Task 3. (This task's standalone check is the build plus a manual smoke test in Task 3.)

**Steps:**

- [ ] **Step 1: Declare the helper** in `modules/foundry_script/gdscript_analyzer.h`. Find (line ~98):

```cpp
	void resolve_match(GDScriptParser::MatchNode *p_match);
```

Insert immediately after it:

```cpp
	void resolve_match(GDScriptParser::MatchNode *p_match);
#ifdef DEBUG_ENABLED
	void check_match_exhaustiveness(GDScriptParser::MatchNode *p_match);
#endif
```

- [ ] **Step 2: Ensure `HashSet` is available.** Check the top of `modules/foundry_script/gdscript_analyzer.cpp` for `#include "core/templates/hash_set.h"`. If it is not present, add it alongside the other `core/templates/...` includes. (`HashMap` is already used by the analyzer.)

- [ ] **Step 3: Call the helper from `resolve_match`.** Find (line ~2733):

```cpp
void GDScriptAnalyzer::resolve_match(GDScriptParser::MatchNode *p_match) {
	reduce_expression(p_match->test);

	for (int i = 0; i < p_match->branches.size(); i++) {
		resolve_match_branch(p_match->branches[i], p_match->test);

		decide_suite_type(p_match, p_match->branches[i]);
	}
}
```

Replace with:

```cpp
void GDScriptAnalyzer::resolve_match(GDScriptParser::MatchNode *p_match) {
	reduce_expression(p_match->test);

	for (int i = 0; i < p_match->branches.size(); i++) {
		resolve_match_branch(p_match->branches[i], p_match->test);

		decide_suite_type(p_match, p_match->branches[i]);
	}

#ifdef DEBUG_ENABLED
	check_match_exhaustiveness(p_match);
#endif
}
```

- [ ] **Step 4: Add the helper implementation** immediately after `resolve_match` (before `resolve_match_branch`). Insert:

```cpp
#ifdef DEBUG_ENABLED
void GDScriptAnalyzer::check_match_exhaustiveness(GDScriptParser::MatchNode *p_match) {
	const GDScriptParser::DataType match_type = p_match->test->get_datatype();
	if (!match_type.is_set()) {
		return; // Type unknown; cannot classify the domain.
	}

	// A branch counts as a default only with an unguarded wildcard/bind pattern.
	// The parser already clears `has_wildcard` when a guard is present.
	bool has_default = false;
	for (GDScriptParser::MatchBranchNode *branch : p_match->branches) {
		if (branch->has_wildcard) {
			has_default = true;
			break;
		}
	}

	// Classify the matched type's domain.
	// `domain_values` maps each value's display name to its integer value.
	// Iteration order follows insertion order (Godot HashMap), i.e. enum
	// declaration order, so the unhandled list is deterministic.
	bool is_finite_domain = false;
	HashMap<StringName, int64_t> domain_values;
	String type_name;
	if (match_type.kind == GDScriptParser::DataType::ENUM) {
		is_finite_domain = true;
		domain_values = match_type.enum_values;
		type_name = match_type.enum_type == StringName() ? String("enum") : String(match_type.enum_type);
	} else if (match_type.kind == GDScriptParser::DataType::BUILTIN && match_type.builtin_type == Variant::BOOL) {
		is_finite_domain = true;
		domain_values[SNAME("false")] = 0;
		domain_values[SNAME("true")] = 1;
		type_name = "bool";
	}

	if (!is_finite_domain) {
		if (!has_default) {
			parser->push_warning(p_match, GDScriptWarning::MATCH_WITHOUT_DEFAULT);
		}
		return;
	}

	if (has_default || domain_values.is_empty()) {
		return; // Exhaustive via default, or nothing to check.
	}

	// Collect values covered by unguarded, statically-constant patterns.
	HashSet<int64_t> covered_values;
	for (GDScriptParser::MatchBranchNode *branch : p_match->branches) {
		if (branch->guard_body != nullptr) {
			continue; // Guard may fail; does not guarantee coverage.
		}
		for (GDScriptParser::PatternNode *pattern : branch->patterns) {
			const GDScriptParser::ExpressionNode *value_node = nullptr;
			if (pattern->pattern_type == GDScriptParser::PatternNode::PT_LITERAL) {
				value_node = pattern->literal;
			} else if (pattern->pattern_type == GDScriptParser::PatternNode::PT_EXPRESSION) {
				value_node = pattern->expression;
			} else {
				return; // Wildcard/bind/array/dict: cannot reason about coverage; bail out.
			}

			if (value_node == nullptr || !value_node->is_constant) {
				return; // Non-constant pattern: cannot prove coverage; bail out.
			}
			const Variant::Type reduced_type = value_node->reduced_value.get_type();
			if (reduced_type != Variant::INT && reduced_type != Variant::BOOL) {
				return; // Unexpected value type for a finite domain; bail out.
			}
			covered_values.insert((int64_t)value_node->reduced_value);
		}
	}

	// Report any domain value with no covering pattern.
	Vector<String> unhandled;
	for (const KeyValue<StringName, int64_t> &E : domain_values) {
		if (!covered_values.has(E.value)) {
			unhandled.push_back(String(E.key));
		}
	}

	if (!unhandled.is_empty()) {
		parser->push_warning(p_match, GDScriptWarning::NON_EXHAUSTIVE_MATCH, type_name, String(", ").join(unhandled));
	}
}
#endif // DEBUG_ENABLED
```

> Note: `PT_LITERAL` patterns store the value in `pattern->literal` (a `LiteralNode`, which is an `ExpressionNode` with `is_constant`/`reduced_value` set by `reduce_literal`). `PT_EXPRESSION` patterns store it in `pattern->expression` with `is_constant`/`reduced_value` set by `reduce_expression`. Reading `reduced_value` uniformly handles bare int literals (`0`) and enum-member references (`Direction.NORTH`).

- [ ] **Step 5: Build to verify it compiles** (DEBUG build).

Run: `scons platform=macos target=editor dev_build=yes tests=yes`
Expected: build completes with no errors or warnings-as-errors.

- [ ] **Step 6: Commit.**

```bash
git add modules/foundry_script/gdscript_analyzer.h modules/foundry_script/gdscript_analyzer.cpp
git commit -m "feat(gdscript): Analyze match exhaustiveness and emit warnings"
```

---

### Task 3: Add Foundry Script fixtures and verify behavior

**Goal:** Add `.fs`/`.out` fixture pairs that exercise each rule, generate expected output with the engine, verify it matches intent, and confirm the full test suite passes.

**Files (all under `modules/foundry_script/tests/scripts/analyzer/warnings/`):**
- Create: `match_non_exhaustive_enum.fs` + `.out`
- Create: `match_exhaustive_enum_no_warning.fs` + `.out`
- Create: `match_non_exhaustive_enum_with_wildcard.fs` + `.out`
- Create: `match_enum_guarded_branch.fs` + `.out`
- Create: `match_enum_non_constant_pattern.fs` + `.out`
- Create: `match_bool_non_exhaustive.fs` + `.out`
- Create: `match_without_default.fs` + `.out`

**Acceptance Criteria:**
- [ ] Each `.fs` has a `func test():` entry point and produces only the intended warning(s) — no incidental `UNUSED_*` warnings (the runner forces all warnings except `UNTYPED_DECLARATION`/`INFERRED_DECLARATION` to `Warn`).
- [ ] `.out` files contain `GDTEST_OK`, the expected `~~ WARNING ... (CODE) ...` lines, then program output.
- [ ] The non-exhaustive enum fixture lists unhandled enumerators in declaration order.
- [ ] The full-coverage, wildcard, and non-constant-pattern fixtures emit no match warning.
- [ ] `match_without_default` fixture emits `MATCH_WITHOUT_DEFAULT` (runner forces its level to `Warn` despite the `Ignore` default).
- [ ] Full Foundry Script + C++ test suite passes.

**Verify:** `./bin/godot.<...> --headless --test --force-colors` → all tests pass (look for the analyzer warning suite passing, no failures).

**Background on fixtures:** The test runner (`modules/foundry_script/tests/gdscript_test_runner.cpp`) forces every warning level to `Warn` (except `UNTYPED_DECLARATION`/`INFERRED_DECLARATION`). The expected-output format is, e.g.:

```
GDTEST_OK
~~ WARNING at line 9: (NON_EXHAUSTIVE_MATCH) The "match" statement does not cover all values of "Direction". Unhandled: EAST, WEST. Add the missing patterns or a "_" wildcard branch.
ok
```

**Pitfalls to avoid in `.fs` files:**
- Match enums using enum-member references (`Direction.NORTH`), not bare ints, to avoid `INT_AS_ENUM_WITHOUT_MATCH`/`INT_AS_ENUM_WITHOUT_CAST`.
- Prefix unused binds with `_` and `print()` any locals to avoid `UNUSED_VARIABLE`/`UNUSED_PARAMETER`.
- Keep each file minimal so the `.out` is stable.

**Steps:**

- [ ] **Step 1: Write `match_non_exhaustive_enum.fs`** (partial enum, no wildcard → warning).

```gdscript
enum Direction { NORTH, EAST, SOUTH, WEST }

func test():
	var direction := Direction.NORTH
	match direction:
		Direction.NORTH:
			print("north")
		Direction.SOUTH:
			print("south")
	print("ok")
```

- [ ] **Step 2: Write `match_exhaustive_enum_no_warning.fs`** (full coverage, no wildcard → no warning).

```gdscript
enum Direction { NORTH, EAST, SOUTH, WEST }

func test():
	var direction := Direction.NORTH
	match direction:
		Direction.NORTH:
			print("north")
		Direction.EAST:
			print("east")
		Direction.SOUTH:
			print("south")
		Direction.WEST:
			print("west")
	print("ok")
```

- [ ] **Step 3: Write `match_non_exhaustive_enum_with_wildcard.fs`** (partial + `_` → no warning).

```gdscript
enum Direction { NORTH, EAST, SOUTH, WEST }

func test():
	var direction := Direction.NORTH
	match direction:
		Direction.NORTH:
			print("north")
		_:
			print("other")
	print("ok")
```

- [ ] **Step 4: Write `match_enum_guarded_branch.fs`** (guarded branch does not count → warning still fires for the guarded value, and the guarded wildcard is not a default).

```gdscript
enum Direction { NORTH, EAST, SOUTH, WEST }

func test():
	var direction := Direction.NORTH
	match direction:
		Direction.NORTH:
			print("north")
		Direction.EAST:
			print("east")
		Direction.SOUTH:
			print("south")
		_ when direction == Direction.WEST:
			print("guarded west")
	print("ok")
```

- [ ] **Step 5: Write `match_enum_non_constant_pattern.fs`** (unguarded non-constant pattern → bail out, no warning). `other` is a non-constant local used as an expression pattern.

```gdscript
enum Direction { NORTH, EAST, SOUTH, WEST }

func test():
	var direction := Direction.NORTH
	var other := Direction.WEST
	match direction:
		Direction.NORTH:
			print("north")
		other:
			print("matched other")
	print("ok")
```

- [ ] **Step 6: Write `match_bool_non_exhaustive.fs`** (bool missing `false` → warning, unhandled: false).

```gdscript
func test():
	var flag := true
	match flag:
		true:
			print("yes")
	print("ok")
```

- [ ] **Step 7: Write `match_without_default.fs`** (non-finite domain, no wildcard → `MATCH_WITHOUT_DEFAULT`).

```gdscript
func test():
	var number := 3
	match number:
		1:
			print("one")
		2:
			print("two")
	print("ok")
```

- [ ] **Step 8: Generate the `.out` files** with the engine (it writes one `.out` per `.fs`).

Run: `./bin/godot.<...> --headless --gdscript-generate-tests modules/foundry_script/tests/scripts`

- [ ] **Step 9: Manually review each generated `.out`** — do NOT blindly trust generation. Confirm:
  - `match_non_exhaustive_enum.out` contains a `(NON_EXHAUSTIVE_MATCH)` line for `Direction` with `Unhandled: EAST, WEST`.
  - `match_exhaustive_enum_no_warning.out`, `match_non_exhaustive_enum_with_wildcard.out`, `match_enum_non_constant_pattern.out` contain **no** `NON_EXHAUSTIVE_MATCH`/`MATCH_WITHOUT_DEFAULT` line.
  - `match_enum_guarded_branch.out` contains a `(NON_EXHAUSTIVE_MATCH)` line with `Unhandled: WEST`.
  - `match_bool_non_exhaustive.out` contains a `(NON_EXHAUSTIVE_MATCH)` line for `bool` with `Unhandled: false`.
  - `match_without_default.out` contains a `(MATCH_WITHOUT_DEFAULT)` line.
  - No file contains unexpected `UNUSED_*` or `INT_AS_ENUM_*` warnings. If any do, fix the `.fs` (prefix unused vars with `_`, use enum-member references) and regenerate.

  If any expectation is wrong because of an implementation bug (not a fixture bug), return to Task 2, fix, rebuild, and regenerate.

- [ ] **Step 10: Run the full suite to confirm green.**

Run: `./bin/godot.<...> --headless --test --force-colors`
Expected: all tests pass; no failures in the Foundry Script analyzer warning suite.

- [ ] **Step 11: Commit.**

```bash
git add modules/foundry_script/tests/scripts/analyzer/warnings/match_*.fs modules/foundry_script/tests/scripts/analyzer/warnings/match_*.out
git commit -m "test(gdscript): Cover match exhaustiveness warnings"
```

---

### Task 4: Document the new settings

**Goal:** Add the two `<member>` entries to `ProjectSettings.xml` so the warning settings are documented and the doc-sync pre-commit checks pass.

**Files:**
- Modify: `doc/classes/ProjectSettings.xml` (members are alphabetically ordered)

**Acceptance Criteria:**
- [ ] `debug/gdscript/warnings/match_without_default` documented with `default="0"`.
- [ ] `debug/gdscript/warnings/non_exhaustive_match` documented with `default="1"`.
- [ ] Both placed in correct alphabetical position.
- [ ] `pre-commit run --all-files` passes the doc checks (`make-rst`, `doc-status`, `validate-xml`).

**Verify:** `pre-commit run --all-files` → doc-related hooks pass (no "files were modified" failures for the XML doc checks).

**Steps:**

- [ ] **Step 1: Add `match_without_default`** — alphabetically it sits between `integer_division` and `missing_await`. Find (line ~597):

```xml
		<member name="debug/gdscript/warnings/integer_division" type="int" setter="" getter="" default="1">
			When set to [b]Warn[/b] or [b]Error[/b], produces a warning or an error respectively when dividing an integer by another integer (the decimal part will be discarded).
		</member>
		<member name="debug/gdscript/warnings/missing_await" type="int" setter="" getter="" default="0">
```

Insert the new member between them:

```xml
		<member name="debug/gdscript/warnings/integer_division" type="int" setter="" getter="" default="1">
			When set to [b]Warn[/b] or [b]Error[/b], produces a warning or an error respectively when dividing an integer by another integer (the decimal part will be discarded).
		</member>
		<member name="debug/gdscript/warnings/match_without_default" type="int" setter="" getter="" default="0">
			When set to [b]Warn[/b] or [b]Error[/b], produces a warning or an error respectively when a [code]match[/code] statement over a value without a finite set of cases has no [code]_[/code] wildcard (default) branch. Disabled by default.
		</member>
		<member name="debug/gdscript/warnings/missing_await" type="int" setter="" getter="" default="0">
```

- [ ] **Step 2: Add `non_exhaustive_match`** — alphabetically it sits between `native_method_override` and `onready_with_export`. Find (line ~612):

```xml
		<member name="debug/gdscript/warnings/native_method_override" type="int" setter="" getter="" default="2">
			When set to [b]Warn[/b] or [b]Error[/b], produces a warning or an error respectively when a method in the script overrides a native method, because it may not behave as expected.
		</member>
		<member name="debug/gdscript/warnings/onready_with_export" type="int" setter="" getter="" default="2">
```

Insert the new member between them:

```xml
		<member name="debug/gdscript/warnings/native_method_override" type="int" setter="" getter="" default="2">
			When set to [b]Warn[/b] or [b]Error[/b], produces a warning or an error respectively when a method in the script overrides a native method, because it may not behave as expected.
		</member>
		<member name="debug/gdscript/warnings/non_exhaustive_match" type="int" setter="" getter="" default="1">
			When set to [b]Warn[/b] or [b]Error[/b], produces a warning or an error respectively when a [code]match[/code] statement over an enum or [bool] does not handle all possible values and has no [code]_[/code] wildcard (default) branch.
		</member>
		<member name="debug/gdscript/warnings/onready_with_export" type="int" setter="" getter="" default="2">
```

- [ ] **Step 3: Run the doc/pre-commit checks.**

Run: `pre-commit run --all-files`
Expected: doc hooks (`make-rst`, `doc-status`, `validate-xml`) pass. If a hook reports the XML needs reformatting, accept its in-place fix and re-run.

- [ ] **Step 4: Commit.**

```bash
git add doc/classes/ProjectSettings.xml
git commit -m "docs(gdscript): Document match exhaustiveness warning settings"
```

---

## Self-Review

**Spec coverage:**
- `NON_EXHAUSTIVE_MATCH` (enum/bool, missing values, no `_`) → Task 1 (register) + Task 2 (analysis) + Task 3 fixtures 1/4/6. ✓
- Full-coverage enum / both-bool no warning → Task 2 + Task 3 fixtures 2/6. ✓
- `MATCH_WITHOUT_DEFAULT` (non-finite, no `_`), default Ignore → Task 1 (level `IGNORE`) + Task 2 + Task 3 fixture 7. ✓
- Guard exclusion → Task 2 (`guard_body`/`has_wildcard`) + Task 3 fixture 4. ✓
- Coverage by value, constant patterns only, aliased enumerators → Task 2 (`reduced_value`, `covered_values` by int). ✓
- Bail out on non-constant pattern → Task 2 + Task 3 fixture 5. ✓
- Wildcard suppresses → Task 2 + Task 3 fixture 3. ✓
- No-branches / unset type → Task 2 (`is_set()` guard; empty `domain_values` guard). ✓
- Settings auto-register → no code change needed (existing `WARNING_MAX` loop); documented in Task 4. ✓
- Docs sync → Task 4. ✓

**Placeholder scan:** No TBD/TODO; all code shown in full. ✓

**Type/name consistency:** `check_match_exhaustiveness` (declared Task 2 Step 1, defined Step 4, called Step 3); `NON_EXHAUSTIVE_MATCH`/`MATCH_WITHOUT_DEFAULT` identical across `.h`, `.cpp`, analyzer, fixtures, docs; message symbol count (2) matches `CHECK_SYMBOLS(2)` and the `push_warning(..., type_name, joined)` call. ✓

**Ordering note:** Tasks 1 → 2 → 3 are strictly sequential (3 depends on the build from 1+2). Task 4 is independent of 2/3 and can be done any time after Task 1, but is ordered last.
