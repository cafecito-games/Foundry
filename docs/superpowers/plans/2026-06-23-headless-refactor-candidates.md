# Headless Add Type Annotation Candidates — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers-extended-cc:subagent-driven-development (recommended) or superpowers-extended-cc:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Expose a headless, caret-independent `GDScriptRefactoring::find_candidates` that returns every Add Type Annotation candidate and its edits for a file, while keeping the existing caret-driven path byte-for-byte unchanged.

**Architecture:** The caret dependency lives only in the type-annotation walk's leaf functions. Task 1 moves the caret gate out of the leaves: the walk collects *all* candidates (each tagged with its caret-test span) and the existing caret entry point becomes a thin filter over that list — a behavior-preserving refactor verified by the existing test suite. Task 2 adds the public `RefactorCandidate` / `RefactorCandidatesResult` types and `find_candidates`, which reuses the same collected list and skips the caret filter, covered by new tests.

**Tech Stack:** C++ (Godot engine module), doctest unit tests (`tests/test_macros.h`), SCons build.

**Reference spec:** `docs/superpowers/specs/2026-06-23-headless-refactor-candidates-design.md`

**Build/test commands (macOS, this repo):**
- Build: `scons platform=macos target=editor dev_build=yes tests=yes`
- Test: `./bin/godot.macos.editor.dev.arm64 --headless --test "[Modules][GDScript][Refactor]" --force-colors`

---

### Task 1: Unify the type-annotation walk (behavior-preserving refactor)

**Goal:** Collect all Add Type Annotation candidates in one traversal; make the caret-driven path a filter over that list. No observable behavior change.

**Files:**
- Modify: `modules/gdscript/editor/gdscript_refactoring.cpp`

**Acceptance Criteria:**
- [ ] `TypeAnnotationCandidate` carries its caret-test span (`line`, `caret_span_start`, `caret_span_end`).
- [ ] Leaf functions (`find_assignable_type_annotation`, `find_function_return_type_annotation`) compute the candidate unconditionally and report the span — they no longer call `caret_on_segment`.
- [ ] A new `collect_type_annotation_candidates_in_tree(...)` returns `Vector<TypeAnnotationCandidate>` with every annotatable declaration (enabled and disabled).
- [ ] `find_type_annotation_candidate_in_tree(...)` selects the caret-matching candidate from the collected list (or the default disabled candidate), preserving prior behavior.
- [ ] Existing refactor test suite passes unchanged.

**Verify:** `./bin/godot.macos.editor.dev.arm64 --headless --test "[Modules][GDScript][Refactor]" --force-colors` → all cases pass (same count as before).

**Steps:**

- [ ] **Step 1: Add span fields to the internal `TypeAnnotationCandidate` struct**

In `modules/gdscript/editor/gdscript_refactoring.cpp` (currently lines 57–62), extend the struct:

```cpp
struct TypeAnnotationCandidate {
	bool matched = false;
	bool enabled = false;
	String disabled_reason;
	RefactorTextEdit edit;
	// Caret-test span of the declaration this candidate was found at. Used by the
	// caret-driven path to select the candidate under the caret; the headless
	// collector ignores it.
	int line = -1;
	int caret_span_start = -1;
	int caret_span_end = -1;
};
```

- [ ] **Step 2: Drop the caret gate from `find_assignable_type_annotation`, record the span instead**

Replace the caret check block (currently lines 816–820) so the function records the span and always proceeds. The function no longer needs `p_location` for gating, but still uses it as the identifier disambiguator for parameters. Change the block:

```cpp
	// The parser does not expose the assignment operator token, so this refactor
	// intentionally handles declarations whose assignment delimiter is on the
	// declaration line.
	const int equal_index = line.find("=", name_end);
	const int declaration_end = equal_index >= 0 ? equal_index : name_end;

	r_candidate.matched = true;
	r_candidate.line = line_index;
	r_candidate.caret_span_start = declaration_start;
	r_candidate.caret_span_end = declaration_end;
	if (p_assignable->datatype_specifier != nullptr) {
```

(That is: delete the `if (!caret_on_segment(...)) { return false; }` guard and the separate `r_candidate.matched = true;` line that followed it, replacing them with the span assignments above. The rest of the function body — the `datatype_specifier`, `initializer`, `render_annotation_or_disable`, and edit-building code — is unchanged.)

- [ ] **Step 3: Drop the caret gate from `find_function_return_type_annotation`, record the span instead**

Replace the caret check block (currently lines 870–872) similarly:

```cpp
	if (function_start < 0 || body_colon < 0) {
		return false;
	}

	r_candidate.matched = true;
	r_candidate.line = line_index;
	r_candidate.caret_span_start = function_start;
	r_candidate.caret_span_end = body_colon;
	if (p_function->return_type != nullptr) {
```

(Delete the `if (!caret_on_segment(...)) { return false; }` guard and the old standalone `r_candidate.matched = true;` line; replace with the span assignments above. Remainder unchanged.)

- [ ] **Step 4: Make the walk collect every candidate instead of short-circuiting**

The walk functions currently return `bool` and stop at the first match. Add collecting variants that push every matched candidate into a vector. Add these above `find_type_annotation_candidate_in_tree` (near line 2837). They mirror the existing `find_type_annotation_in_class/_function/_suite` structure but never short-circuit:

```cpp
void collect_type_annotation_in_suite(const Vector<String> &p_lines, const GDScriptParser::SuiteNode *p_suite, Vector<TypeAnnotationCandidate> &r_candidates);

void collect_assignable_candidate(const Vector<String> &p_lines, const GDScriptParser::AssignableNode *p_assignable, const String &p_kind, bool p_has_keyword, Vector<TypeAnnotationCandidate> &r_candidates) {
	TypeAnnotationCandidate candidate;
	// nullptr location: collect mode has no caret to disambiguate identifiers,
	// matching the existing var/const/member path which already passes nullptr.
	const RefactorLocation no_caret;
	if (find_assignable_type_annotation(no_caret, p_lines, p_assignable, p_kind, p_has_keyword, candidate) && candidate.matched) {
		r_candidates.push_back(candidate);
	}
}

void collect_type_annotation_in_function(const Vector<String> &p_lines, const GDScriptParser::FunctionNode *p_function, Vector<TypeAnnotationCandidate> &r_candidates) {
	if (p_function == nullptr) {
		return;
	}
	for (const GDScriptParser::ParameterNode *parameter : p_function->parameters) {
		collect_assignable_candidate(p_lines, parameter, "parameter", false, r_candidates);
	}
	if (p_function->rest_parameter != nullptr) {
		collect_assignable_candidate(p_lines, p_function->rest_parameter, "parameter", false, r_candidates);
	}
	TypeAnnotationCandidate return_candidate;
	const RefactorLocation no_caret;
	if (find_function_return_type_annotation(no_caret, p_lines, p_function, return_candidate) && return_candidate.matched) {
		r_candidates.push_back(return_candidate);
	}
	collect_type_annotation_in_suite(p_lines, p_function->body, r_candidates);
}

void collect_type_annotation_in_class(const Vector<String> &p_lines, const GDScriptParser::ClassNode *p_class, Vector<TypeAnnotationCandidate> &r_candidates) {
	if (p_class == nullptr) {
		return;
	}
	for (const GDScriptParser::ClassNode::Member &member : p_class->members) {
		switch (member.type) {
			case GDScriptParser::ClassNode::Member::CONSTANT:
				collect_assignable_candidate(p_lines, member.constant, "constant", true, r_candidates);
				break;
			case GDScriptParser::ClassNode::Member::VARIABLE:
				collect_assignable_candidate(p_lines, member.variable, "variable", true, r_candidates);
				break;
			case GDScriptParser::ClassNode::Member::FUNCTION:
				collect_type_annotation_in_function(p_lines, member.function, r_candidates);
				break;
			case GDScriptParser::ClassNode::Member::CLASS:
				collect_type_annotation_in_class(p_lines, member.m_class, r_candidates);
				break;
			default:
				break;
		}
	}
}

void collect_type_annotation_in_suite(const Vector<String> &p_lines, const GDScriptParser::SuiteNode *p_suite, Vector<TypeAnnotationCandidate> &r_candidates) {
	if (p_suite == nullptr) {
		return;
	}
	for (const GDScriptParser::Node *statement : p_suite->statements) {
		if (statement == nullptr) {
			continue;
		}
		switch (statement->type) {
			case GDScriptParser::Node::VARIABLE:
				collect_assignable_candidate(p_lines, static_cast<const GDScriptParser::VariableNode *>(statement), "variable", true, r_candidates);
				break;
			case GDScriptParser::Node::CONSTANT:
				collect_assignable_candidate(p_lines, static_cast<const GDScriptParser::ConstantNode *>(statement), "constant", true, r_candidates);
				break;
			case GDScriptParser::Node::IF: {
				const GDScriptParser::IfNode *if_node = static_cast<const GDScriptParser::IfNode *>(statement);
				collect_type_annotation_in_suite(p_lines, if_node->true_block, r_candidates);
				collect_type_annotation_in_suite(p_lines, if_node->false_block, r_candidates);
			} break;
			case GDScriptParser::Node::FOR: {
				const GDScriptParser::ForNode *for_node = static_cast<const GDScriptParser::ForNode *>(statement);
				collect_type_annotation_in_suite(p_lines, for_node->loop, r_candidates);
			} break;
			case GDScriptParser::Node::WHILE: {
				const GDScriptParser::WhileNode *while_node = static_cast<const GDScriptParser::WhileNode *>(statement);
				collect_type_annotation_in_suite(p_lines, while_node->loop, r_candidates);
			} break;
			case GDScriptParser::Node::MATCH: {
				const GDScriptParser::MatchNode *match_node = static_cast<const GDScriptParser::MatchNode *>(statement);
				for (const GDScriptParser::MatchBranchNode *branch : match_node->branches) {
					if (branch != nullptr) {
						collect_type_annotation_in_suite(p_lines, branch->block, r_candidates);
					}
				}
			} break;
			default:
				break;
		}
	}
}

Vector<TypeAnnotationCandidate> collect_type_annotation_candidates_in_tree(const Vector<String> &p_lines, const GDScriptParser::ClassNode *p_tree) {
	Vector<TypeAnnotationCandidate> candidates;
	collect_type_annotation_in_class(p_lines, p_tree, candidates);
	return candidates;
}
```

> Note on `find_assignable_type_annotation` signature: it keeps its `const RefactorLocation &p_location` parameter (still used as the identifier disambiguator for parameters via `get_identifier_text_span`). In collect mode we pass a default-constructed `RefactorLocation` (a zero caret), which matches the existing var/const/member behavior that already passes `nullptr` as the identifier location for keyworded declarations. Parameters are the only case that consults the location, and a single declaration has exactly one identifier on its line, so the first-occurrence resolution is correct.

- [ ] **Step 5: Rewrite `find_type_annotation_candidate_in_tree` as a filter over the collected list**

Replace the existing function (currently lines 2837–2845) with one that collects then selects the caret-matching candidate using the existing `caret_on_segment`:

```cpp
TypeAnnotationCandidate find_type_annotation_candidate_in_tree(const RefactorLocation &p_location, const Vector<String> &p_lines, const GDScriptParser::ClassNode *p_tree) {
	const Vector<TypeAnnotationCandidate> candidates = collect_type_annotation_candidates_in_tree(p_lines, p_tree);
	for (const TypeAnnotationCandidate &candidate : candidates) {
		if (caret_on_segment(p_location, candidate.line, candidate.caret_span_start, candidate.caret_span_end)) {
			return candidate;
		}
	}

	TypeAnnotationCandidate candidate;
	candidate.disabled_reason = "Place the caret on an untyped declaration with an inferred concrete type.";
	return candidate;
}
```

> Behavior note: the original walk returned the *first* declaration whose span contained the caret. `caret_on_segment` only ever matches one declaration's span for a point caret (spans on a line do not overlap), so iterating the collected list and returning the first span match reproduces the original result, including the matched-but-disabled candidate's `disabled_reason`.

- [ ] **Step 6: Build**

Run: `scons platform=macos target=editor dev_build=yes tests=yes`
Expected: clean build, no warnings about unused `find_type_annotation_in_class/_function/_suite`.

> If the compiler warns that the original `find_type_annotation_in_class` / `_in_function` / `_in_suite` are now unused, delete those three now-dead caret-walk functions (currently lines 894–1001) — the collecting variants plus the `caret_on_segment` filter fully replace them. Leave the leaf functions (`find_assignable_type_annotation`, `find_function_return_type_annotation`) in place; they are still called by the collectors.

- [ ] **Step 7: Run the existing test suite to prove no behavior change**

Run: `./bin/godot.macos.editor.dev.arm64 --headless --test "[Modules][GDScript][Refactor]" --force-colors`
Expected: all existing refactor cases PASS (e.g. "Add type annotation inserts concrete inferred types", "Add type annotation preserves strict type spelling", etc.).

- [ ] **Step 8: Commit**

```bash
git add modules/gdscript/editor/gdscript_refactoring.cpp
git commit -m "Unify Add Type Annotation walk for caret-independent collection"
```

---

### Task 2: Add the public `find_candidates` API and headless tests

**Goal:** Public `RefactorCandidate` / `RefactorCandidatesResult` types and `GDScriptRefactoring::find_candidates`, returning all Add Type Annotation candidates for a file with no `RefactorLocation`, covered by new tests and fixtures.

**Files:**
- Modify: `modules/gdscript/editor/gdscript_refactoring.h`
- Modify: `modules/gdscript/editor/gdscript_refactoring.cpp`
- Modify: `modules/gdscript/tests/test_refactor.h`
- Create: `modules/gdscript/tests/scripts/refactor/type_annotation_candidates.gd`

**Acceptance Criteria:**
- [ ] `RefactorCandidate` and `RefactorCandidatesResult` declared in the header.
- [ ] `GDScriptRefactoring::find_candidates(context, kind)` returns all annotatable declarations (enabled + disabled) for `ADD_TYPE_ANNOTATION`.
- [ ] Unsupported kinds return `ok = false` with the not-implemented message.
- [ ] Unparsable files return `ok = false`, `error_message = "Cannot analyze this script."`.
- [ ] New tests assert the full candidate set, a disabled candidate, an unsupported kind, and edit-parity with the caret path — constructing no `RefactorLocation` for the collection assertions.

**Verify:** `./bin/godot.macos.editor.dev.arm64 --headless --test "[Modules][GDScript][Refactor]" --force-colors` → new cases pass.

**Steps:**

- [ ] **Step 1: Write the failing test (TDD red) + fixture**

Create `modules/gdscript/tests/scripts/refactor/type_annotation_candidates.gd`:

```gdscript
var member_score = 1
var member_typed: int = 2

func make_total(amount = 3):
	var local_name = "cafe"
	if true:
		var nested_flag = false
```

Add these cases to `modules/gdscript/tests/test_refactor.h` inside the `TEST_SUITE("[Modules][GDScript][Refactor]")` block (after the existing type-annotation cases, e.g. following line 401):

```cpp
	TEST_CASE("find_candidates collects all type annotations without a caret") {
		RefactorContext ctx = make_context("modules/gdscript/tests/scripts/refactor/type_annotation_candidates.gd");
		RefactorCandidatesResult result = GDScriptRefactoring::find_candidates(ctx, RefactorKind::ADD_TYPE_ANNOTATION);
		REQUIRE(result.ok);

		// Enabled candidates: member_score (int), make_total param (int),
		// make_total return (void/int as inferred), local_name (String), nested_flag (bool),
		// plus member_typed reported as disabled (already typed).
		int enabled_count = 0;
		int disabled_count = 0;
		bool saw_member_score = false;
		bool saw_nested_flag = false;
		for (const RefactorCandidate &candidate : result.candidates) {
			CHECK_EQ(candidate.kind, RefactorKind::ADD_TYPE_ANNOTATION);
			if (candidate.enabled) {
				enabled_count++;
				REQUIRE_FALSE(candidate.edits.is_empty());
				if (candidate.line == 0) {
					saw_member_score = true;
					CHECK_EQ(candidate.edits[0].new_text, ": int = ");
				}
				if (candidate.line == 6) {
					saw_nested_flag = true;
				}
			} else {
				disabled_count++;
				CHECK_FALSE(candidate.disabled_reason.is_empty());
			}
		}
		CHECK(saw_member_score);
		CHECK(saw_nested_flag);
		CHECK_GE(enabled_count, 4);
		CHECK_GE(disabled_count, 1); // member_typed already has a type.
	}

	TEST_CASE("find_candidates rejects unsupported refactor kinds") {
		RefactorContext ctx = make_context("modules/gdscript/tests/scripts/refactor/type_annotation_candidates.gd");
		RefactorCandidatesResult result = GDScriptRefactoring::find_candidates(ctx, RefactorKind::RENAME);
		CHECK_FALSE(result.ok);
		CHECK_EQ(result.error_message, "Headless candidate collection is not implemented for this refactor.");
	}

	TEST_CASE("find_candidates edit matches the caret-driven path") {
		RefactorContext ctx = make_context("modules/gdscript/tests/scripts/refactor/type_annotation_candidates.gd");
		RefactorCandidatesResult result = GDScriptRefactoring::find_candidates(ctx, RefactorKind::ADD_TYPE_ANNOTATION);
		REQUIRE(result.ok);

		const RefactorCandidate *member_score = nullptr;
		for (const RefactorCandidate &candidate : result.candidates) {
			if (candidate.enabled && candidate.line == 0) {
				member_score = &candidate;
				break;
			}
		}
		REQUIRE(member_score != nullptr);

		RefactorParams params;
		RefactorResult caret_result = GDScriptRefactoring::prepare(ctx, caret(0, 5), RefactorKind::ADD_TYPE_ANNOTATION, params);
		REQUIRE(caret_result.ok);
		REQUIRE_EQ(caret_result.edits.size(), 1);
		CHECK_EQ(member_score->edits[0].new_text, caret_result.edits[0].new_text);
		CHECK_EQ(member_score->edits[0].start_line, caret_result.edits[0].start_line);
		CHECK_EQ(member_score->edits[0].start_column, caret_result.edits[0].start_column);
		CHECK_EQ(member_score->edits[0].end_column, caret_result.edits[0].end_column);
	}
```

- [ ] **Step 2: Run the test to confirm it fails (red)**

Run: `./bin/godot.macos.editor.dev.arm64 --headless --test "[Modules][GDScript][Refactor]" --force-colors`
Expected: compile error — `find_candidates` / `RefactorCandidate` / `RefactorCandidatesResult` are not declared. (This confirms the test exercises the new API.)

- [ ] **Step 3: Add the public types and method to the header**

In `modules/gdscript/editor/gdscript_refactoring.h`, after the `RefactorAvailability` struct (line 76) and before `RefactorResult`, add:

```cpp
// One independently-applicable refactor opportunity found without a caret.
struct RefactorCandidate {
	RefactorKind kind = RefactorKind::ADD_TYPE_ANNOTATION;
	bool enabled = false; // false => found but not applicable.
	String disabled_reason; // Populated when !enabled.
	int line = -1; // 0-based anchor line of the declaration.
	int column = -1; // 0-based anchor column.
	Vector<RefactorTextEdit> edits; // Active-file edits (reuses RefactorTextEdit).
};

struct RefactorCandidatesResult {
	bool ok = false;
	String error_message; // Set only when the file cannot be analyzed at all.
	Vector<RefactorCandidate> candidates;
};
```

Then add the method to the `GDScriptRefactoring` class (after `prepare`, line 123):

```cpp
	static RefactorCandidatesResult find_candidates(const RefactorContext &p_context, RefactorKind p_kind);
```

- [ ] **Step 4: Implement `find_candidates` (TDD green)**

In `modules/gdscript/editor/gdscript_refactoring.cpp`, add a namespace-internal helper that parses and collects, mirroring `find_type_annotation_candidate_uncached`'s parse/analyze path. Add it in the anonymous namespace near the other type-annotation helpers (e.g. after `prepare_type_annotation`, around line 3192):

```cpp
RefactorCandidatesResult collect_type_annotation_candidates(
		const RefactorContext &p_context,
		const GDScriptParseResultProvider *p_parse_results) {
	RefactorCandidatesResult result;
	const Vector<String> lines = p_context.source.split("\n");

	const GDScriptParser::ClassNode *tree = nullptr;
#ifndef GDSCRIPT_NO_LSP
	if (p_parse_results != nullptr) {
		const ExtendGDScriptParser *lsp_parser = p_parse_results->get_parse_result(p_context.path);
		if (lsp_parser != nullptr && source_lines_match(lsp_parser->get_lines(), lines)) {
			if (lsp_parser->parse_result != OK) {
				result.error_message = "Cannot analyze this script.";
				return result;
			}
			tree = lsp_parser->get_tree();
		}
	}
#endif // GDSCRIPT_NO_LSP

	GDScriptParser parser;
	if (tree == nullptr) {
		Error err = parser.parse(p_context.source, p_context.path, false);
		if (err == OK) {
			GDScriptAnalyzer analyzer(&parser);
			err = analyzer.analyze();
		}
		if (err != OK) {
			result.error_message = "Cannot analyze this script.";
			return result;
		}
		tree = parser.get_tree();
	}

	const Vector<TypeAnnotationCandidate> candidates = collect_type_annotation_candidates_in_tree(lines, tree);
	for (const TypeAnnotationCandidate &candidate : candidates) {
		RefactorCandidate public_candidate;
		public_candidate.kind = RefactorKind::ADD_TYPE_ANNOTATION;
		public_candidate.enabled = candidate.enabled;
		public_candidate.disabled_reason = candidate.disabled_reason;
		public_candidate.line = candidate.line;
		public_candidate.column = candidate.caret_span_start;
		if (candidate.enabled) {
			public_candidate.edits.push_back(candidate.edit);
		}
		result.candidates.push_back(public_candidate);
	}
	result.ok = true;
	return result;
}
```

Then add the public method definition next to the other `GDScriptRefactoring::` definitions (after `prepare`, around line 3540, inside `#ifdef TOOLS_ENABLED`):

```cpp
RefactorCandidatesResult GDScriptRefactoring::find_candidates(const RefactorContext &p_context, RefactorKind p_kind) {
	if (p_kind != RefactorKind::ADD_TYPE_ANNOTATION) {
		RefactorCandidatesResult result;
		result.error_message = "Headless candidate collection is not implemented for this refactor.";
		return result;
	}

#ifndef GDSCRIPT_NO_LSP
	RefactorParseResultProviderScope parse_results(p_context);
	const GDScriptParseResultProvider *parse_result_provider = parse_results.get();
#else
	const GDScriptParseResultProvider *parse_result_provider = nullptr;
#endif // GDSCRIPT_NO_LSP

	return collect_type_annotation_candidates(p_context, parse_result_provider);
}
```

> The `collect_type_annotation_candidates` helper lives in the anonymous namespace (lines ~55–3194) so it is visible to the `GDScriptRefactoring::find_candidates` definition that follows the `}` closing that namespace. Confirm the helper is placed *before* the namespace's closing brace and the method definition *after* it.

- [ ] **Step 5: Build**

Run: `scons platform=macos target=editor dev_build=yes tests=yes`
Expected: clean build.

- [ ] **Step 6: Run the new tests to confirm they pass (green)**

Run: `./bin/godot.macos.editor.dev.arm64 --headless --test "[Modules][GDScript][Refactor]" --force-colors`
Expected: the three new cases pass, all prior cases still pass.

- [ ] **Step 7: Commit**

```bash
git add modules/gdscript/editor/gdscript_refactoring.h modules/gdscript/editor/gdscript_refactoring.cpp modules/gdscript/tests/test_refactor.h modules/gdscript/tests/scripts/refactor/type_annotation_candidates.gd
git commit -m "Add headless find_candidates for Add Type Annotation"
```

---

## Self-Review Notes

- **Spec coverage:** `find_candidates` + types (spec "Public API") → Task 2. Unified walk preserving caret behavior (spec "Internal design") → Task 1. Caching left unchanged (spec "Caching") → no task needed. Tests/fixtures (spec "Testing") → Task 2 Step 1. Acceptance criteria (headless collection with no `RefactorLocation`; existing flows unchanged) → Task 2 tests + Task 1 regression run.
- **Type consistency:** `collect_type_annotation_candidates_in_tree` defined in Task 1 Step 4, consumed in Task 1 Step 5 and Task 2 Step 4. `RefactorCandidate.edits` (Vector) matches usage. `caret_span_start/caret_span_end/line` fields defined Task 1 Step 1, used in Steps 2/3/5 and Task 2 Step 4.
- **Disabled-candidate edits:** disabled candidates carry no edit (the internal candidate's `edit` is only built when enabled), so `find_candidates` only pushes `edit` when `enabled` — consistent with the spec.
