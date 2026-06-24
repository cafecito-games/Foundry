# Implement Abstract Methods Refactor — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers-extended-cc:subagent-driven-development (recommended) or superpowers-extended-cc:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a GDScript refactor that generates concrete stub methods for inherited `@abstract` methods a class has not yet implemented, surfaced in the script editor's refactor submenu and over LSP.

**Architecture:** A new `RefactorKind::IMPLEMENT_ABSTRACT_METHODS` in the existing `GDScriptRefactoring` module. It follows the established `find_*_candidate` (cached availability + collection) → `prepare_*` (edit production) pattern. Detection walks the analyzer-resolved base-class chain to find un-overridden abstract methods; rendering reuses `GDScriptRefactorTypes::render_annotatable_type`. Both the editor and LSP go through `get_available_refactors`/`prepare`, so both surfaces, undo/redo, and caching come for free.

**Tech Stack:** C++ (Godot engine module), SCons build, doctest unit tests (`tests/test_macros.h`), GDScript AST (`GDScriptParser` / `GDScriptAnalyzer`).

**Spec:** `docs/superpowers/specs/2026-06-23-implement-abstract-methods-design.md`

**Build command (this VM / fork):**
```
scons platform=macos target=editor dev_build=yes tests=yes -j$(sysctl -n hw.ncpu)
```
Binary: `bin/godot.macos.editor.dev.<arch>` (e.g. `arm64`). Substitute the matching name on your machine.

**Test command (whole refactor suite):**
```
./bin/godot.macos.editor.dev.* --headless --test --test-suite="*Refactor*" --force-colors
```

---

## File Structure

| File | Responsibility | Change |
|------|----------------|--------|
| `modules/gdscript/editor/gdscript_refactoring.h` | Public refactor API + `RefactorKind` enum | Modify: add enum value |
| `modules/gdscript/editor/gdscript_refactoring.cpp` | All refactor logic | Modify: add candidate struct, cache, detection, rendering, prepare, availability |
| `editor/script/script_text_editor.h` | Script editor menu IDs | Modify: add `EDIT_REFACTOR_IMPLEMENT_ABSTRACT_METHODS` |
| `editor/script/script_text_editor.cpp` | Script editor menu wiring | Modify: route new id + add static_assert |
| `modules/gdscript/tests/test_refactor.h` | Refactor unit tests | Modify: helpers, availability-count fixes, new test cases |

All new logic lives inside the existing anonymous namespace in `gdscript_refactoring.cpp`, co-located with the other refactors it mirrors.

---

## Task 1: Plumbing skeleton — refactor appears in menu, disabled

**Goal:** Wire a new `IMPLEMENT_ABSTRACT_METHODS` refactor end-to-end as an inert, always-disabled entry so the engine builds, the menu shows it, LSP lists it, and existing tests are updated for the new count.

**Files:**
- Modify: `modules/gdscript/editor/gdscript_refactoring.h` (RefactorKind enum)
- Modify: `modules/gdscript/editor/gdscript_refactoring.cpp` (candidate struct, cache, stub finder, availability entry, prepare case)
- Modify: `editor/script/script_text_editor.h` (menu id)
- Modify: `editor/script/script_text_editor.cpp` (switch route + static_assert)
- Modify: `modules/gdscript/tests/test_refactor.h` (update `available.size()` assertions 5 → 6)

**Acceptance Criteria:**
- [ ] Engine builds with `dev_build=yes tests=yes`.
- [ ] `get_available_refactors` returns 6 entries; the 6th is `IMPLEMENT_ABSTRACT_METHODS` titled "Implement Abstract Methods", `enabled == false`, with a non-empty `disabled_reason`.
- [ ] Existing refactor tests pass after updating count assertions.

**Verify:** `./bin/godot.macos.editor.dev.* --headless --test --test-suite="*Refactor*" --force-colors` → all pass.

**Steps:**

- [ ] **Step 1: Add the enum value (header).** In `modules/gdscript/editor/gdscript_refactoring.h`, append to `enum class RefactorKind` (keep it LAST so the editor id mapping stays valid):

```cpp
enum class RefactorKind {
	RENAME,
	EXTRACT_VARIABLE,
	EXTRACT_METHOD,
	ADD_TYPE_ANNOTATION,
	INLINE_VARIABLE,
	IMPLEMENT_ABSTRACT_METHODS,
};
```

- [ ] **Step 2: Add the candidate struct + cache.** In `gdscript_refactoring.cpp`, inside the anonymous namespace next to `InlineVariableCandidate` (~line 102), add:

```cpp
struct ImplementAbstractCandidate {
	bool matched = false;
	bool enabled = false;
	String disabled_reason;
	// Owed abstract methods, most-derived-first, each paired with the class that
	// declared it (needed to render faithful signatures).
	Vector<const GDScriptParser::FunctionNode *> abstract_methods;
	// Insertion point: the line AFTER the last member of the target class (1-based,
	// matching FunctionNode::end_line semantics used by extract method).
	int insertion_line = -1;
	String class_body_indent;
};
```

Next to `InlineVariableCandidateCache` (~line 210), add the cache struct + global, mirroring it exactly:

```cpp
struct ImplementAbstractCandidateCache {
	bool valid = false;
	String path;
	uint64_t source_hash = 0;
	int source_length = 0;
	RefactorLocation location;
	ImplementAbstractCandidate candidate;
};

ImplementAbstractCandidateCache implement_abstract_cache;
```

> NOTE: `ImplementAbstractCandidate` holds raw `FunctionNode *` pointers into a parse tree that is freed when the analyzer/parser goes out of scope. The cache stores a *copy* of these pointers, so a cached candidate must only ever be consulted while its source buffer is unchanged (the cache key already guards on path + hash + length + location). `prepare` re-runs detection in the same call scope where the tree is alive — it does not read pointers out of a stale cache. This matches how the other candidates carry already-resolved `RefactorTextEdit` values rather than live pointers; in Task 3 we will resolve all pointer-derived text during detection so the cached candidate carries only value types. For Task 1 the stub finder stores no pointers, so this is safe.

- [ ] **Step 3: Add the cache get/set helpers.** After `cache_inline_variable_candidate` (~line 2938), add — identical shape to the inline-variable helpers:

```cpp
bool get_cached_implement_abstract_candidate(const RefactorContext &p_context, const RefactorLocation &p_location, ImplementAbstractCandidate &r_candidate) {
	MutexLock lock(refactor_candidate_cache_mutex);

	if (!implement_abstract_cache.valid ||
			implement_abstract_cache.path != p_context.path ||
			implement_abstract_cache.source_hash != p_context.source.hash64() ||
			implement_abstract_cache.source_length != p_context.source.length() ||
			!same_location(implement_abstract_cache.location, p_location)) {
		return false;
	}
	r_candidate = implement_abstract_cache.candidate;
	return true;
}

void cache_implement_abstract_candidate(const RefactorContext &p_context, const RefactorLocation &p_location, const ImplementAbstractCandidate &p_candidate) {
	MutexLock lock(refactor_candidate_cache_mutex);

	implement_abstract_cache.valid = true;
	implement_abstract_cache.path = p_context.path;
	implement_abstract_cache.source_hash = p_context.source.hash64();
	implement_abstract_cache.source_length = p_context.source.length();
	implement_abstract_cache.location = p_location;
	implement_abstract_cache.candidate = p_candidate;
}
```

- [ ] **Step 4: Add a stub finder.** Above `prepare_type_annotation` (~line 3300), add the cached entry point with placeholder detection (real detection lands in Task 2):

```cpp
ImplementAbstractCandidate find_implement_abstract_candidate(
		const RefactorContext &p_context,
		const RefactorLocation &p_location,
		const GDScriptParseResultProvider *p_parse_results) {
	ImplementAbstractCandidate candidate;
	if (get_cached_implement_abstract_candidate(p_context, p_location, candidate)) {
		return candidate;
	}

	// Detection is implemented in Task 2. Until then the refactor is inert.
	candidate.disabled_reason = "No unimplemented abstract methods.";

	cache_implement_abstract_candidate(p_context, p_location, candidate);
	return candidate;
}
```

- [ ] **Step 5: Add the `prepare_implement_abstract` function.** Next to `prepare_type_annotation` (~line 3319), add:

```cpp
RefactorResult prepare_implement_abstract(
		const RefactorContext &p_context,
		const RefactorLocation &p_location,
		const GDScriptParseResultProvider *p_parse_results) {
	RefactorResult result;
	const ImplementAbstractCandidate candidate = find_implement_abstract_candidate(p_context, p_location, p_parse_results);
	if (!candidate.enabled) {
		result.ok = false;
		result.error_message = candidate.disabled_reason;
		return result;
	}

	// Edit production is implemented in Task 3.
	result.ok = false;
	result.error_message = "Implement Abstract Methods is not implemented yet.";
	return result;
}
```

- [ ] **Step 6: Add the availability entry.** In `get_available_refactors`, after the `inline_variable` block (~line 3655, before `return result;`), add:

```cpp
	RefactorAvailability implement_abstract;
	implement_abstract.kind = RefactorKind::IMPLEMENT_ABSTRACT_METHODS;
	implement_abstract.title = "Implement Abstract Methods";
	const ImplementAbstractCandidate implement_abstract_candidate = find_implement_abstract_candidate(p_context, p_location, parse_result_provider);
	implement_abstract.enabled = implement_abstract_candidate.enabled;
	if (!implement_abstract.enabled) {
		implement_abstract.disabled_reason = implement_abstract_candidate.disabled_reason;
	}
	result.push_back(implement_abstract);
```

- [ ] **Step 7: Add the `prepare` switch case.** In `GDScriptRefactoring::prepare` (~line 3677), add before `default:`:

```cpp
		case RefactorKind::IMPLEMENT_ABSTRACT_METHODS:
			return prepare_implement_abstract(p_context, p_location, parse_result_provider);
```

- [ ] **Step 8: Add the editor menu id.** In `editor/script/script_text_editor.h`, append after `EDIT_REFACTOR_INLINE_VARIABLE` (line 209):

```cpp
		EDIT_REFACTOR_INLINE_VARIABLE,
		EDIT_REFACTOR_IMPLEMENT_ABSTRACT_METHODS,
```

- [ ] **Step 9: Route the id + add static_assert.** In `editor/script/script_text_editor.cpp`, add the case to the refactor switch (~line 2088):

```cpp
		case EDIT_REFACTOR_ADD_TYPE_ANNOTATION:
		case EDIT_REFACTOR_INLINE_VARIABLE:
		case EDIT_REFACTOR_IMPLEMENT_ABSTRACT_METHODS: {
			// The option id encodes the RefactorKind; see the enum ordering note.
			_run_refactor(p_op - EDIT_REFACTOR_RENAME);
		} break;
```

And add the matching static_assert in `_run_refactor` after the `INLINE_VARIABLE` one (~line 2962):

```cpp
	static_assert(EDIT_REFACTOR_RENAME + (int)RefactorKind::IMPLEMENT_ABSTRACT_METHODS == EDIT_REFACTOR_IMPLEMENT_ABSTRACT_METHODS, "RefactorKind/EDIT_REFACTOR_* mapping mismatch");
```

No new dialog is needed: the generic path in `_run_refactor` (`prepare` → `_apply_refactor_result`) already handles a no-parameter refactor.

- [ ] **Step 10: Update existing test count assertions.** In `modules/gdscript/tests/test_refactor.h`, every assertion of the form `CHECK_EQ(available.size(), 5)` (and any `if (available.size() < 5)` guard) must become `6`. Find them:

```bash
grep -n "available.size()" modules/gdscript/tests/test_refactor.h
```

Update each `5` to `6`. Example edit:

```cpp
	Vector<RefactorAvailability> available = GDScriptRefactoring::get_available_refactors(ctx, caret(0, 0));
	CHECK_EQ(available.size(), 6);
	if (available.size() < 6) {
		return;
	}
```

- [ ] **Step 11: Add an availability test for the new entry.** In `test_refactor.h`, inside `TEST_SUITE("[Modules][GDScript][Refactor]")`, add:

```cpp
	TEST_CASE("Implement abstract methods is listed and disabled with no abstract base") {
		RefactorContext ctx = make_context("modules/gdscript/tests/scripts/refactor/empty.gd");
		Vector<RefactorAvailability> available = GDScriptRefactoring::get_available_refactors(ctx, caret(0, 0));
		const RefactorAvailability *entry = nullptr;
		for (const RefactorAvailability &a : available) {
			if (a.kind == RefactorKind::IMPLEMENT_ABSTRACT_METHODS) {
				entry = &a;
				break;
			}
		}
		REQUIRE(entry != nullptr);
		CHECK_EQ(entry->title, String("Implement Abstract Methods"));
		CHECK_FALSE(entry->enabled);
		CHECK_FALSE(entry->disabled_reason.is_empty());
	}
```

- [ ] **Step 12: Build and run tests.**

Run:
```
scons platform=macos target=editor dev_build=yes tests=yes -j$(sysctl -n hw.ncpu)
./bin/godot.macos.editor.dev.* --headless --test --test-suite="*Refactor*" --force-colors
```
Expected: build succeeds; all refactor tests pass.

- [ ] **Step 13: Commit.**

```bash
git add modules/gdscript/editor/gdscript_refactoring.h modules/gdscript/editor/gdscript_refactoring.cpp editor/script/script_text_editor.h editor/script/script_text_editor.cpp modules/gdscript/tests/test_refactor.h
git commit -m "feat(gdscript): Scaffold implement-abstract-methods refactor"
```

```json:metadata
{"files": ["modules/gdscript/editor/gdscript_refactoring.h", "modules/gdscript/editor/gdscript_refactoring.cpp", "editor/script/script_text_editor.h", "editor/script/script_text_editor.cpp", "modules/gdscript/tests/test_refactor.h"], "verifyCommand": "./bin/godot.macos.editor.dev.* --headless --test --test-case=\"*Refactor*\" --force-colors", "acceptanceCriteria": ["Builds with dev_build=yes tests=yes", "get_available_refactors returns 6 entries, 6th is disabled IMPLEMENT_ABSTRACT_METHODS", "Existing refactor tests pass after count update"]}
```

---

## Task 2: Detection — find owed abstract methods + availability

**Goal:** Implement real detection: walk the analyzer-resolved base chain, collect the abstract methods the target class still owes, set availability accordingly, and handle the abstract-target-class and inner-class edge cases. (Same-file / analyzer-resolved bases only; cross-file fallback is Task 4.)

**Files:**
- Modify: `modules/gdscript/editor/gdscript_refactoring.cpp` (replace the stub `find_implement_abstract_candidate` body with real detection + helpers)
- Modify: `modules/gdscript/tests/test_refactor.h` (availability test cases)

**Acceptance Criteria:**
- [ ] Availability is `enabled` when the enclosing class has ≥1 un-overridden inherited `@abstract` method, regardless of caret position within the class.
- [ ] Disabled with `"No unimplemented abstract methods."` when none are owed.
- [ ] Disabled with `"Abstract classes don't need to implement abstract methods."` when the enclosing class is itself `@abstract`.
- [ ] Disabled with `"Cannot analyze this script."` on parse/analyze failure.
- [ ] A method concretely overridden by the target or any intermediate class is excluded; an intermediate `@abstract` re-declaration is NOT.
- [ ] Inner classes resolve relative to the class enclosing the caret.

**Verify:** `./bin/godot.macos.editor.dev.* --headless --test --test-suite="*Refactor*" --force-colors` → new detection cases pass.

**Steps:**

- [ ] **Step 1: Determine whether a `FunctionNode` is abstract.** GDScript marks abstract methods via `FunctionNode::is_abstract` (`modules/gdscript/gdscript_parser.h:908`) and abstract classes via the `@abstract` annotation on the class. Confirm the class-level abstract flag name before coding:

```bash
grep -n "is_abstract" modules/gdscript/gdscript_parser.h
```
Expected: `ClassNode` carries `bool is_abstract = false;` (line ~783) and `FunctionNode` carries `bool is_abstract = false;` (line ~908). Use `ClassNode::is_abstract` for the abstract-class guard.

- [ ] **Step 2: Add the enclosing-class + base-walk helpers.** Above `find_implement_abstract_candidate`, add:

```cpp
// Deepest class whose [start_line, end_line] span contains the caret line.
// Node line numbers are 1-based; RefactorLocation lines are 0-based.
const GDScriptParser::ClassNode *find_enclosing_class(const GDScriptParser::ClassNode *p_class, int p_caret_line_0based) {
	if (p_class == nullptr) {
		return nullptr;
	}
	const int line_1based = p_caret_line_0based + 1;
	const GDScriptParser::ClassNode *best = p_class;
	for (const GDScriptParser::ClassNode::Member &member : p_class->members) {
		if (member.type != GDScriptParser::ClassNode::Member::CLASS) {
			continue;
		}
		const GDScriptParser::ClassNode *inner = member.m_class;
		if (inner == nullptr) {
			continue;
		}
		if (line_1based >= inner->start_line && line_1based <= inner->end_line) {
			const GDScriptParser::ClassNode *deeper = find_enclosing_class(inner, p_caret_line_0based);
			if (deeper != nullptr) {
				best = deeper;
			}
		}
	}
	return best;
}

// Next GDScript class up the inheritance chain, or nullptr when the base is not a
// resolved GDScript class reachable in-tree (native bases / unresolved). Cross-file
// resolution is added in Task 4.
const GDScriptParser::ClassNode *resolve_base_class(const GDScriptParser::ClassNode *p_class) {
	if (p_class == nullptr) {
		return nullptr;
	}
	const GDScriptParser::DataType &base = p_class->base_type;
	if (base.kind == GDScriptParser::DataType::CLASS) {
		return base.class_type;
	}
	return nullptr;
}
```

- [ ] **Step 3: Add the abstract-collection helper.** Add:

```cpp
// Collect abstract methods the target class still owes, most-derived-first.
// Walks {target, base, base-of-base, ...}; for each method name the FIRST
// declaration encountered (the most-derived) decides its fate: if that
// declaration is abstract and lives in an ancestor (not the target), it is owed.
void collect_owed_abstract_methods(
		const GDScriptParser::ClassNode *p_target,
		Vector<const GDScriptParser::FunctionNode *> &r_owed) {
	HashSet<StringName> decided;
	const GDScriptParser::ClassNode *current = p_target;
	bool is_target = true;
	while (current != nullptr) {
		for (const GDScriptParser::ClassNode::Member &member : current->members) {
			if (member.type != GDScriptParser::ClassNode::Member::FUNCTION) {
				continue;
			}
			const GDScriptParser::FunctionNode *function = member.function;
			if (function == nullptr || function->identifier == nullptr) {
				continue;
			}
			const StringName name = function->identifier->name;
			if (decided.has(name)) {
				continue; // A more-derived declaration already decided this name.
			}
			decided.insert(name);
			if (function->is_abstract && !is_target) {
				r_owed.push_back(function);
			}
		}
		current = resolve_base_class(current);
		is_target = false;
	}
}
```

- [ ] **Step 4: Add an uncached detection core that builds the tree.** Replace the placeholder `find_implement_abstract_candidate` from Task 1 with the cached wrapper plus an `_in_tree` core. Mirror the analyzer/LSP-parse pattern of `find_inline_variable_candidate_uncached` (`gdscript_refactoring.cpp:3061`):

```cpp
ImplementAbstractCandidate find_implement_abstract_in_tree(
		const RefactorLocation &p_location,
		const Vector<String> &p_lines,
		const GDScriptParser::ClassNode *p_tree) {
	ImplementAbstractCandidate candidate;

	const GDScriptParser::ClassNode *target = find_enclosing_class(p_tree, p_location.start_line);
	if (target == nullptr) {
		candidate.disabled_reason = "No unimplemented abstract methods.";
		return candidate;
	}

	if (target->is_abstract) {
		candidate.disabled_reason = "Abstract classes don't need to implement abstract methods.";
		return candidate;
	}

	collect_owed_abstract_methods(target, candidate.abstract_methods);
	if (candidate.abstract_methods.is_empty()) {
		candidate.disabled_reason = "No unimplemented abstract methods.";
		return candidate;
	}

	// Insertion point + indentation are finalized in Task 3.
	candidate.insertion_line = target->end_line;
	candidate.matched = true;
	candidate.enabled = true;
	return candidate;
}

ImplementAbstractCandidate find_implement_abstract_candidate_uncached(
		const RefactorContext &p_context,
		const RefactorLocation &p_location,
		const Vector<String> &p_lines,
		const GDScriptParseResultProvider *p_parse_results) {
#ifndef GDSCRIPT_NO_LSP
	if (p_parse_results != nullptr) {
		const ExtendGDScriptParser *lsp_parser = p_parse_results->get_parse_result(p_context.path);
		if (lsp_parser != nullptr && source_lines_match(lsp_parser->get_lines(), p_lines)) {
			if (lsp_parser->parse_result != OK) {
				ImplementAbstractCandidate candidate;
				candidate.disabled_reason = "Cannot analyze this script.";
				return candidate;
			}
			return find_implement_abstract_in_tree(p_location, p_lines, lsp_parser->get_tree());
		}
	}
#endif // GDSCRIPT_NO_LSP

	GDScriptParser parser;
	Error err = parser.parse(p_context.source, p_context.path, false);
	if (err == OK) {
		GDScriptAnalyzer analyzer(&parser);
		err = analyzer.analyze();
	}
	if (err != OK) {
		ImplementAbstractCandidate candidate;
		candidate.disabled_reason = "Cannot analyze this script.";
		return candidate;
	}

	return find_implement_abstract_in_tree(p_location, p_lines, parser.get_tree());
}
```

And the cached wrapper (replacing the Task 1 stub body):

```cpp
ImplementAbstractCandidate find_implement_abstract_candidate(
		const RefactorContext &p_context,
		const RefactorLocation &p_location,
		const GDScriptParseResultProvider *p_parse_results) {
	ImplementAbstractCandidate candidate;
	if (get_cached_implement_abstract_candidate(p_context, p_location, candidate)) {
		return candidate;
	}

	const Vector<String> lines = p_context.source.split("\n");
	candidate = find_implement_abstract_candidate_uncached(p_context, p_location, lines, p_parse_results);
	cache_implement_abstract_candidate(p_context, p_location, candidate);
	return candidate;
}
```

> NOTE on caching live pointers: this candidate caches `FunctionNode *` values into `candidate.abstract_methods`. That is only safe because, when the LSP parse-result path is taken, the `ExtendGDScriptParser` tree is owned by the provider and outlives the call; when the fallback parser path is taken, the pointers must NOT survive the function. To stay safe, Task 3 finalizes all rendered text inside `find_implement_abstract_in_tree` (while the tree is alive) and stores only `String` results in the candidate, then clears `abstract_methods` before returning. Implement that in Task 3; for Task 2 the cached pointers are only ever read back within `find_implement_abstract_candidate` of a subsequent identical call, where availability uses only `enabled`/`disabled_reason` — never dereferencing the pointers — so detection tests are safe.

- [ ] **Step 5: Add `HashSet` include if missing.** Confirm `core/templates/hash_set.h` is available in the translation unit:

```bash
grep -n "hash_set.h\|HashSet" modules/gdscript/editor/gdscript_refactoring.cpp | head
```
If `HashSet` is not already used/included, add `#include "core/templates/hash_set.h"` with the other core includes at the top of the file.

- [ ] **Step 6: Add detection test fixtures + cases.** In `test_refactor.h`, add an availability helper and cases using inline sources. Add near the other `run_*` helpers:

```cpp
inline const RefactorAvailability *find_availability(const Vector<RefactorAvailability> &p_available, RefactorKind p_kind) {
	for (const RefactorAvailability &a : p_available) {
		if (a.kind == p_kind) {
			return &a;
		}
	}
	return nullptr;
}

inline bool implement_abstract_enabled(const String &p_source, int p_line, int p_column) {
	RefactorContext ctx;
	ctx.path = "user://implement_abstract_refactor.gd";
	ctx.source = p_source;
	const Vector<RefactorAvailability> available = GDScriptRefactoring::get_available_refactors(ctx, caret(p_line, p_column));
	const RefactorAvailability *entry = find_availability(available, RefactorKind::IMPLEMENT_ABSTRACT_METHODS);
	return entry != nullptr && entry->enabled;
}

inline String implement_abstract_reason(const String &p_source, int p_line, int p_column) {
	RefactorContext ctx;
	ctx.path = "user://implement_abstract_refactor.gd";
	ctx.source = p_source;
	const Vector<RefactorAvailability> available = GDScriptRefactoring::get_available_refactors(ctx, caret(p_line, p_column));
	const RefactorAvailability *entry = find_availability(available, RefactorKind::IMPLEMENT_ABSTRACT_METHODS);
	return entry != nullptr ? entry->disabled_reason : String();
}
```

Add test cases inside the suite (inline sources keep abstract base + derived in one file, exercising the same-file chain):

```cpp
	TEST_CASE("Implement abstract: enabled when a derived class owes an abstract method") {
		const String source =
				"@abstract class Base:\n"
				"\t@abstract func area() -> float\n"
				"class Circle extends Base:\n"
				"\tvar radius := 1.0\n";
		CHECK(GDScriptTests::implement_abstract_enabled(source, 3, 1));
	}

	TEST_CASE("Implement abstract: disabled when the method is already overridden") {
		const String source =
				"@abstract class Base:\n"
				"\t@abstract func area() -> float\n"
				"class Circle extends Base:\n"
				"\tfunc area() -> float:\n"
				"\t\treturn 3.14\n";
		CHECK_FALSE(GDScriptTests::implement_abstract_enabled(source, 2, 1));
		CHECK_EQ(GDScriptTests::implement_abstract_reason(source, 2, 1), String("No unimplemented abstract methods."));
	}

	TEST_CASE("Implement abstract: disabled for an abstract derived class") {
		const String source =
				"@abstract class Base:\n"
				"\t@abstract func area() -> float\n"
				"@abstract class Shape extends Base:\n"
				"\tvar name := \"\"\n";
		CHECK_FALSE(GDScriptTests::implement_abstract_enabled(source, 3, 1));
		CHECK_EQ(GDScriptTests::implement_abstract_reason(source, 3, 1), String("Abstract classes don't need to implement abstract methods."));
	}

	TEST_CASE("Implement abstract: intermediate concrete override satisfies the contract") {
		const String source =
				"@abstract class Base:\n"
				"\t@abstract func area() -> float\n"
				"\t@abstract func name() -> String\n"
				"@abstract class Mid extends Base:\n"
				"\tfunc area() -> float:\n"
				"\t\treturn 0.0\n"
				"class Leaf extends Mid:\n"
				"\tvar tag := 1\n";
		// Leaf still owes name() but not area().
		CHECK(GDScriptTests::implement_abstract_enabled(source, 7, 1));
	}
```

> If the inline `@abstract class` / `@abstract func` syntax differs in this fork, confirm against an existing fixture: `grep -rn "@abstract" modules/gdscript/tests/scripts | head`. Adjust the fixture source to the real syntax; the assertions stay the same.

- [ ] **Step 7: Build and run.**

Run:
```
scons platform=macos target=editor dev_build=yes tests=yes -j$(sysctl -n hw.ncpu)
./bin/godot.macos.editor.dev.* --headless --test --test-suite="*Refactor*" --force-colors
```
Expected: all detection cases pass.

- [ ] **Step 8: Commit.**

```bash
git add modules/gdscript/editor/gdscript_refactoring.cpp modules/gdscript/tests/test_refactor.h
git commit -m "feat(gdscript): Detect owed abstract methods for implement refactor"
```

```json:metadata
{"files": ["modules/gdscript/editor/gdscript_refactoring.cpp", "modules/gdscript/tests/test_refactor.h"], "verifyCommand": "./bin/godot.macos.editor.dev.* --headless --test --test-case=\"*Refactor*\" --force-colors", "acceptanceCriteria": ["Enabled when class owes an inherited abstract method", "Disabled with correct reason when none owed", "Abstract target class disabled", "Parse failure disabled", "Intermediate concrete override excluded", "Inner classes resolve relative to caret"]}
```

---

## Task 3: Stub rendering + edit production

**Goal:** Generate concrete stub methods for the owed abstract methods and emit a single insertion edit at the end of the target class body, so the refactor produces correct text.

**Files:**
- Modify: `modules/gdscript/editor/gdscript_refactoring.cpp` (render stubs, build edit, finish `prepare_implement_abstract`)
- Modify: `modules/gdscript/tests/test_refactor.h` (output snapshot tests)

**Acceptance Criteria:**
- [ ] Each owed method is rendered as `func <name>(<params>) -> <ret>:` (or no `-> ret` for void), preserving `static`, parameter names, annotated parameter/return types, and base default values.
- [ ] Body is `push_error("Not implemented: <name>")` plus `return <literal>` for clean-literal return types, `push_error(...)` only for void, and `push_error(...)` + `pass` for object/custom-class return types.
- [ ] Stubs are appended at the end of the target class body with correct indentation, a blank line separating each.
- [ ] Applying the result (`GDScriptRefactorEdits::apply`) yields the expected source text.

**Verify:** `./bin/godot.macos.editor.dev.* --headless --test --test-suite="*Refactor*" --force-colors` → rendering cases pass.

**Steps:**

- [ ] **Step 1: Add a literal-default helper.** Add above `find_implement_abstract_in_tree`:

```cpp
// Returns the GDScript literal default for a return type, or false when the type
// has no clean literal (objects, custom classes) so the caller emits `pass`.
bool default_return_literal(const GDScriptParser::DataType &p_type, String &r_literal) {
	if (p_type.kind != GDScriptParser::DataType::BUILTIN) {
		return false; // NATIVE / SCRIPT / CLASS / ENUM -> no clean literal.
	}
	switch (p_type.builtin_type) {
		case Variant::BOOL:
			r_literal = "false";
			return true;
		case Variant::INT:
			r_literal = "0";
			return true;
		case Variant::FLOAT:
			r_literal = "0.0";
			return true;
		case Variant::STRING:
			r_literal = "\"\"";
			return true;
		case Variant::STRING_NAME:
			r_literal = "&\"\"";
			return true;
		case Variant::ARRAY:
			r_literal = "[]";
			return true;
		case Variant::DICTIONARY:
			r_literal = "{}";
			return true;
		default:
			return false; // Other builtins (Vector2, Color, ...) -> emit pass.
	}
}
```

> Rationale: only types with an unambiguous, always-valid literal get a `return`. Everything else (objects, `Vector2`, `Color`, typed arrays with element constraints) gets `push_error` + `pass`, surfacing a strict "not all paths return" error the developer resolves — matching the spec decision.

- [ ] **Step 2: Add the signature + body renderer.** Add:

```cpp
String render_abstract_stub(
		const GDScriptParser::FunctionNode *p_function,
		const String &p_class_indent) {
	const String body_indent = p_class_indent + "\t";
	const String name = String(p_function->identifier->name);

	String signature = p_class_indent;
	if (p_function->is_static) {
		signature += "static ";
	}
	signature += "func " + name + "(";

	for (int i = 0; i < p_function->parameters.size(); i++) {
		if (i > 0) {
			signature += ", ";
		}
		const GDScriptParser::ParameterNode *parameter = p_function->parameters[i];
		signature += String(parameter->identifier->name);

		String rendered_type;
		if (GDScriptRefactorTypes::render_annotatable_type(parameter->get_datatype(), rendered_type)) {
			signature += ": " + rendered_type;
		}

		if (parameter->initializer != nullptr) {
			// Reproduce the base default verbatim from the source span when available;
			// fall back to "= null" only if the span cannot be recovered.
			String default_text;
			if (render_node_source(parameter->initializer, default_text)) {
				signature += (rendered_type.is_empty() ? " = " : " = ") + default_text;
			}
		}
	}
	signature += ")";

	// Return type + body.
	const GDScriptParser::DataType return_type = p_function->get_datatype();
	String rendered_return;
	const bool has_return = GDScriptRefactorTypes::render_annotatable_type(return_type, rendered_return);

	String result = signature;
	if (has_return) {
		result += " -> " + rendered_return;
	}
	result += ":\n";
	result += body_indent + "push_error(\"Not implemented: " + name + "\")\n";

	if (has_return) {
		String literal;
		if (default_return_literal(return_type, literal)) {
			result += body_indent + "return " + literal + "\n";
		} else {
			result += body_indent + "pass\n";
		}
	}
	return result;
}
```

> `render_node_source` returns the original source text of an expression node from its `[start_line/start_column, end_line/end_column]` span. Check whether a helper already exists before adding one:
> ```bash
> grep -n "node_source\|source_span\|slice_source\|get_source_text\|extract_source" modules/gdscript/editor/gdscript_refactoring.cpp
> ```
> If none exists, add a small helper that slices `p_lines` using the node's `start_line`/`start_column`/`end_line`/`end_column` (1-based lines, mirroring how extract-variable reads spans around `gdscript_refactoring.cpp:1690`). It needs `p_lines`, so thread the `Vector<String> &p_lines` through `render_abstract_stub` and `find_implement_abstract_in_tree`. If recovering the span is not feasible for a given initializer, return false and omit the default (the stub still compiles; the developer re-adds the default if needed) — note this in a `RefactorResult::warning` via the candidate.

- [ ] **Step 3: Finalize text during detection (pointer-safety).** In `find_implement_abstract_in_tree` (Task 2), after collecting `candidate.abstract_methods` and before returning, compute the joined stub block and store it as a `String` on the candidate so no live `FunctionNode *` is cached. Add a field to `ImplementAbstractCandidate`:

```cpp
	String rendered_block; // Finalized stub text (no trailing pointers cached).
```

Then in `find_implement_abstract_in_tree`, replace the `candidate.insertion_line = target->end_line;` tail with:

```cpp
	const Vector<String> &lines = p_lines;
	const String class_indent = class_member_indent(target, lines);
	String block;
	for (int i = 0; i < candidate.abstract_methods.size(); i++) {
		if (i > 0) {
			block += "\n";
		}
		block += render_abstract_stub(candidate.abstract_methods[i], class_indent);
	}
	candidate.rendered_block = block;
	candidate.class_body_indent = class_indent;
	candidate.insertion_line = target->end_line;
	candidate.abstract_methods.clear(); // Do not cache live pointers.
	candidate.matched = true;
	candidate.enabled = true;
	return candidate;
```

Where `class_member_indent` derives the per-member indentation of the target class (one tab deeper than the class header for inner classes; the file's existing convention for top-level classes). Implement it by reading the leading whitespace of the target's first member line via `get_leading_whitespace` (used at `gdscript_refactoring.cpp:1689`); if the class has no members, derive it from the class header indent + one tab. For a top-level class (no header line), member indent is empty for top-level or `\t` for inner — base it on `get_leading_whitespace(p_lines[target->start_line - 1]) ` of the first member.

- [ ] **Step 4: Build the insertion edit in `prepare`.** Replace the placeholder body of `prepare_implement_abstract` (Task 1) with:

```cpp
RefactorResult prepare_implement_abstract(
		const RefactorContext &p_context,
		const RefactorLocation &p_location,
		const GDScriptParseResultProvider *p_parse_results) {
	RefactorResult result;
	const ImplementAbstractCandidate candidate = find_implement_abstract_candidate(p_context, p_location, p_parse_results);
	if (!candidate.enabled) {
		result.ok = false;
		result.error_message = candidate.disabled_reason;
		return result;
	}

	const Vector<String> lines = p_context.source.split("\n");
	const bool has_final_newline = p_context.source.ends_with("\n");

	RefactorTextEdit edit;
	set_extract_method_insertion(edit, lines, has_final_newline, candidate.insertion_line, candidate.rendered_block);

	result.ok = true;
	result.edits.push_back(edit);
	return result;
}
```

> Reuse `set_extract_method_insertion` (`gdscript_refactoring.cpp`, near line of the same name) — it already handles end-of-file vs mid-file insertion and prepends the right number of newlines. `candidate.insertion_line` is the target class `end_line` (1-based), matching how extract method inserts after `p_function->end_line`.

- [ ] **Step 5: Add a `run_implement_abstract` test helper.** In `test_refactor.h`, near the other `run_*` helpers:

```cpp
inline RefactorResult run_implement_abstract(const String &p_source, int p_line, int p_column, String &r_out) {
	RefactorContext ctx;
	ctx.path = "user://implement_abstract_refactor.gd";
	ctx.source = p_source;
	RefactorParams params;
	RefactorResult r = GDScriptRefactoring::prepare(ctx, caret(p_line, p_column), RefactorKind::IMPLEMENT_ABSTRACT_METHODS, params);
	if (r.ok) {
		GDScriptRefactorEdits::apply(ctx.source, r.edits, r_out);
	}
	return r;
}
```

- [ ] **Step 6: Add rendering output tests.** Add cases asserting the applied source. Example (typed return → literal):

```cpp
	TEST_CASE("Implement abstract: renders typed stub with push_error and default return") {
		const String source =
				"@abstract class Base:\n"
				"\t@abstract func area() -> float\n"
				"class Circle extends Base:\n"
				"\tvar radius := 1.0\n";
		String out;
		RefactorResult r = GDScriptTests::run_implement_abstract(source, 3, 1, out);
		REQUIRE(r.ok);
		CHECK(out.contains("func area() -> float:"));
		CHECK(out.contains("push_error(\"Not implemented: area\")"));
		CHECK(out.contains("return 0.0"));
	}

	TEST_CASE("Implement abstract: void method has no return") {
		const String source =
				"@abstract class Base:\n"
				"\t@abstract func tick() -> void\n"
				"class Clock extends Base:\n"
				"\tvar t := 0\n";
		String out;
		RefactorResult r = GDScriptTests::run_implement_abstract(source, 3, 1, out);
		REQUIRE(r.ok);
		CHECK(out.contains("func tick() -> void:"));
		CHECK(out.contains("push_error(\"Not implemented: tick\")"));
		CHECK_FALSE(out.contains("return"));
	}

	TEST_CASE("Implement abstract: object return type uses pass") {
		const String source =
				"@abstract class Base:\n"
				"\t@abstract func make() -> Node\n"
				"class Factory extends Base:\n"
				"\tvar count := 0\n";
		String out;
		RefactorResult r = GDScriptTests::run_implement_abstract(source, 3, 1, out);
		REQUIRE(r.ok);
		CHECK(out.contains("func make() -> Node:"));
		CHECK(out.contains("push_error(\"Not implemented: make\")"));
		CHECK(out.contains("pass"));
		CHECK_FALSE(out.contains("return"));
	}

	TEST_CASE("Implement abstract: preserves params, types and static") {
		const String source =
				"@abstract class Base:\n"
				"\t@abstract static func combine(a: int, b: int) -> int\n"
				"class Math extends Base:\n"
				"\tvar seed := 0\n";
		String out;
		RefactorResult r = GDScriptTests::run_implement_abstract(source, 3, 1, out);
		REQUIRE(r.ok);
		CHECK(out.contains("static func combine(a: int, b: int) -> int:"));
		CHECK(out.contains("return 0"));
	}

	TEST_CASE("Implement abstract: generates all owed methods at once") {
		const String source =
				"@abstract class Base:\n"
				"\t@abstract func area() -> float\n"
				"\t@abstract func name() -> String\n"
				"class Circle extends Base:\n"
				"\tvar radius := 1.0\n";
		String out;
		RefactorResult r = GDScriptTests::run_implement_abstract(source, 4, 1, out);
		REQUIRE(r.ok);
		CHECK(out.contains("func area() -> float:"));
		CHECK(out.contains("func name() -> String:"));
		CHECK(out.contains("return \"\""));
	}
```

> Confirm the exact `@abstract` syntax against a real fixture (`grep -rn "@abstract" modules/gdscript/tests/scripts`). If the renderer's whitespace differs from these `contains` checks, the `contains` assertions are intentionally lenient about surrounding indentation — keep them substring-based rather than full-buffer equality to avoid brittleness.

- [ ] **Step 7: Build and run.**

Run:
```
scons platform=macos target=editor dev_build=yes tests=yes -j$(sysctl -n hw.ncpu)
./bin/godot.macos.editor.dev.* --headless --test --test-suite="*Refactor*" --force-colors
```
Expected: all rendering cases pass.

- [ ] **Step 8: Commit.**

```bash
git add modules/gdscript/editor/gdscript_refactoring.cpp modules/gdscript/tests/test_refactor.h
git commit -m "feat(gdscript): Render abstract method stubs and insertion edit"
```

```json:metadata
{"files": ["modules/gdscript/editor/gdscript_refactoring.cpp", "modules/gdscript/tests/test_refactor.h"], "verifyCommand": "./bin/godot.macos.editor.dev.* --headless --test --test-case=\"*Refactor*\" --force-colors", "acceptanceCriteria": ["Typed return -> literal; void -> no return; object -> pass", "Preserves static, params, annotated types, defaults", "All owed methods generated at end of class with separators", "Applied source matches expectations"]}
```

---

## Task 4: Cross-file base resolution + LSP coverage

**Goal:** Ensure abstract methods inherited from a base class in a *separate* file are detected and stubbed, and verify the refactor over the LSP surface (availability + workspace edit).

**Files:**
- Modify: `modules/gdscript/editor/gdscript_refactoring.cpp` (cross-file fallback in `resolve_base_class`)
- Modify: `modules/gdscript/tests/test_refactor.h` (cross-file + LSP test cases)

**Acceptance Criteria:**
- [ ] A class extending an `@abstract` base defined in another `.gd` file is offered the refactor and stubs the inherited abstract methods.
- [ ] The refactor surfaces over LSP `get_available_refactors`/`prepare` and produces an applicable edit.
- [ ] No regression in same-file behavior from Task 2/3.

**Verify:** `./bin/godot.macos.editor.dev.* --headless --test --test-suite="*Refactor*" --force-colors` → cross-file + LSP cases pass.

**Steps:**

- [ ] **Step 1: Verify what `base_type` carries for a cross-file base.** Before adding a fallback, check whether the analyzer already populates `class_type` for a GDScript base loaded from another file. Add a temporary debug test (or inspect) using a `TemporaryScriptFile` base and assert `implement_abstract_enabled` is already true. If it is, the existing `resolve_base_class` (Task 2) already handles cross-file and you only add tests (skip Step 2). If detection fails, proceed to Step 2.

- [ ] **Step 2: Add a cross-file fallback to `resolve_base_class`.** When `base_type.kind` indicates a GDScript base but `class_type` is null (base lives in another file), resolve the base's parsed tree via the parse-result provider. Thread the provider into the walk. Change `resolve_base_class` to:

```cpp
const GDScriptParser::ClassNode *resolve_base_class(
		const GDScriptParser::ClassNode *p_class,
		const GDScriptParseResultProvider *p_parse_results) {
	if (p_class == nullptr) {
		return nullptr;
	}
	const GDScriptParser::DataType &base = p_class->base_type;
	if (base.kind == GDScriptParser::DataType::CLASS && base.class_type != nullptr) {
		return base.class_type;
	}
#ifndef GDSCRIPT_NO_LSP
	if (p_parse_results != nullptr && base.kind == GDScriptParser::DataType::SCRIPT && !base.script_path.is_empty()) {
		const ExtendGDScriptParser *base_parser = p_parse_results->get_parse_result(base.script_path);
		if (base_parser != nullptr && base_parser->parse_result == OK) {
			return base_parser->get_tree();
		}
	}
#endif
	return nullptr;
}
```

> Confirm the correct DataType field for the base script path before coding:
> ```bash
> grep -n "script_path\|String script\|Ref<Script>\|get_path" modules/gdscript/gdscript_parser.h | head
> ```
> Use whatever the analyzer populates for a SCRIPT-kind DataType (e.g. `script_path`). Thread `p_parse_results` through `collect_owed_abstract_methods` and `find_implement_abstract_in_tree` so the fallback is reachable. Note the analyzer must already have analyzed the base for `base.script_path` to be set — the `parse_result_provider` parses the base file from disk on demand (`RefactorParseResultProvider::get_parse_result`, `gdscript_refactoring.cpp:133`).

- [ ] **Step 3: Add a cross-file test.** Using the `TemporaryScriptFile` helper (already in `test_refactor.h`), write the abstract base to a real path, then run availability/prepare on a derived source that `extends` it:

```cpp
#ifndef GDSCRIPT_NO_LSP
	TEST_CASE("Implement abstract: detects abstract base defined in another file") {
		TemporaryScriptFile base_file(
				"user://implement_abstract_base.gd",
				"@abstract class_name AbstractShape\n"
				"@abstract func area() -> float\n");

		const String derived =
				"extends \"user://implement_abstract_base.gd\"\n"
				"var radius := 1.0\n";
		String out;
		RefactorResult r = GDScriptTests::run_implement_abstract(derived, 1, 0, out);
		REQUIRE(r.ok);
		CHECK(out.contains("func area() -> float:"));
		CHECK(out.contains("push_error(\"Not implemented: area\")"));
	}
#endif
```

> Confirm the fork's syntax for a standalone abstract class file (`@abstract class_name X` vs `@abstract extends Y`). Check existing scripts: `grep -rn "@abstract" modules/gdscript/tests/scripts modules/gdscript/tests`. Match the real syntax; the assertions stay the same. If LSP/workspace is required for cross-file parse-result resolution, this case is correctly guarded by `#ifndef GDSCRIPT_NO_LSP`.

- [ ] **Step 4: Add an LSP availability test.** Mirror existing LSP-based refactor tests in `test_refactor.h` (they `#include "test_lsp.h"` and use the workspace). Find a template:

```bash
grep -n "GDSCRIPT_NO_LSP\|workspace\|test_lsp" modules/gdscript/tests/test_refactor.h | head
```
Add a case that loads a derived script through the workspace and asserts `get_available_refactors` reports `IMPLEMENT_ABSTRACT_METHODS` enabled and `prepare` returns `ok` with a non-empty edit. Keep it guarded by `#ifndef GDSCRIPT_NO_LSP`.

- [ ] **Step 5: Build and run the full suite (not just Refactor).** Cross-file touches shared analysis paths, so run everything once:

```
scons platform=macos target=editor dev_build=yes tests=yes -j$(sysctl -n hw.ncpu)
./bin/godot.macos.editor.dev.* --headless --test --force-colors
```
Expected: full C++ + GDScript suite passes.

- [ ] **Step 6: Commit.**

```bash
git add modules/gdscript/editor/gdscript_refactoring.cpp modules/gdscript/tests/test_refactor.h
git commit -m "feat(gdscript): Resolve cross-file abstract bases and add LSP tests"
```

```json:metadata
{"files": ["modules/gdscript/editor/gdscript_refactoring.cpp", "modules/gdscript/tests/test_refactor.h"], "verifyCommand": "./bin/godot.macos.editor.dev.* --headless --test --force-colors", "acceptanceCriteria": ["Cross-file abstract base detected and stubbed", "Refactor works over LSP availability + prepare", "No regression in same-file behavior", "Full test suite passes"]}
```

---

## Self-Review Notes

- **Spec coverage:** scope (GDScript `@abstract` only) → Task 2 detection; stub body (push_error + return/pass) → Task 3 Steps 1–2; insertion at end of class → Task 3 Step 4; all-at-once → Task 3 Step 6; availability anywhere-in-class + disabled reasons → Task 2; cross-file + LSP surfaces → Task 4; testing plan → tests in every task.
- **Pointer-safety:** addressed explicitly — text is finalized during detection (Task 3 Step 3) and live `FunctionNode *` pointers are cleared before caching.
- **Editor-count coupling:** Task 1 Step 10 updates every `available.size()` assertion; the static_assert chain enforces the enum/menu-id ordering invariant.
- **Open confirmations the implementer must check against the live tree** (each has a `grep` in-step): exact `@abstract` source syntax, the `ClassNode::is_abstract` flag name, the SCRIPT-kind `DataType` path field, and whether a source-span slice helper already exists. These are verifications, not unknowns that block the design.
