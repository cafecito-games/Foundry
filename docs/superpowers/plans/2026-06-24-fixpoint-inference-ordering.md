# Iterative fixpoint inference ordering Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers-extended-cc:subagent-driven-development (recommended) or superpowers-extended-cc:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a headless orchestrator that drives the existing Add Type Annotation refactor to a fixpoint across a set of Foundry Script files, so typing a leaf unlocks its callers across iterations.

**Architecture:** A new `GDScriptFixpointInference::run()` in `modules/foundry_script/editor/` composes existing primitives — `GDScriptRefactoring::find_candidates` (collect), `GDScriptRefactorEdits::apply` (apply), re-parse + `GDScriptAnalyzer::analyze()` (verify) — and writes accepted files to disk, invalidating `GDScriptCache` so dependents re-read them on the next pass. Each pass snapshots all sources and commits writes at pass end, advancing one dependency layer per iteration. No new refactor primitive.

**Tech Stack:** C++17, Godot module code under `#ifdef TOOLS_ENABLED`, SCons build, doctest tests via `modules/*/tests/*.h` (auto-globbed into `modules_tests.gen.h`).

---

## File Structure

- `modules/foundry_script/editor/gdscript_fixpoint_inference.h` — public types (`FixpointInferenceOptions`, `FixpointFileChange`, `FixpointSkipped`, `FixpointInferenceResult`) and the `GDScriptFixpointInference::run` entry point.
- `modules/foundry_script/editor/gdscript_fixpoint_inference.cpp` — the fixpoint loop and its file/cache/verify helpers. Auto-compiled by `modules/foundry_script/SCsub` (`./editor/*.cpp` glob); no SCsub edit needed.
- `modules/foundry_script/tests/test_fixpoint_inference.h` — doctest suite. Auto-included by the `modules/*/tests/*.h` glob in `modules/SCsub`; no registration edit needed. Reuses `GDScriptTests::TemporaryScriptFile` and `make_context` from `test_refactor.h`.

---

### Task 1: Orchestrator with single-file fixpoint

**Goal:** Implement `GDScriptFixpointInference::run()` end to end (snapshot → collect → apply → verify → commit-with-cache-invalidation → report), proven on a single file whose two functions both get typed.

**Files:**
- Create: `modules/foundry_script/editor/gdscript_fixpoint_inference.h`
- Create: `modules/foundry_script/editor/gdscript_fixpoint_inference.cpp`
- Create: `modules/foundry_script/tests/test_fixpoint_inference.h`

**Acceptance Criteria:**
- [ ] `GDScriptFixpointInference::run()` exists with the public API below.
- [ ] Running it on a single untyped file types every resolvable declaration and reports the change.
- [ ] A second run on the now-typed file reports `ok` with no changed files (idempotent).
- [ ] The build compiles with `tests=yes` and the new suite is discovered.

**Verify:** `./bin/godot.macos.editor.dev.arm64 --headless --test --test-suite="*Fixpoint*" --force-colors` → all assertions pass.

**Steps:**

- [ ] **Step 1: Write the header**

Create `modules/foundry_script/editor/gdscript_fixpoint_inference.h`:

```cpp
/**************************************************************************/
/*  gdscript_fixpoint_inference.h                                         */
/**************************************************************************/

#pragma once

#ifdef TOOLS_ENABLED

#include "core/string/ustring.h"
#include "core/templates/vector.h"

// Options controlling a fixpoint inference run.
struct FixpointInferenceOptions {
	// 0 => auto: (first-pass candidate count) + 1, clamped by a hard ceiling.
	int max_iterations = 0;
	bool strict_null_checks = false;
	bool strict_dynamic_checks = false;
};

// Before/after record for one file the run modified.
struct FixpointFileChange {
	String path;
	String before_source;
	String after_source;
	int annotations_applied = 0;
};

// A candidate batch that was found but not applied, with why.
struct FixpointSkipped {
	String path;
	int line = -1;
	String reason;
};

struct FixpointInferenceResult {
	bool ok = false;
	String error_message; // Set only on a fatal, no-op failure (e.g. unreadable file).
	int iterations = 0;
	int total_annotations_applied = 0;
	Vector<FixpointFileChange> changed_files;
	Vector<FixpointSkipped> skipped;
};

class GDScriptFixpointInference {
public:
	// Drives Add Type Annotation to a fixpoint over p_paths, writing accepted
	// changes to disk. Returns a report of what changed and what was skipped.
	static FixpointInferenceResult run(
			const Vector<String> &p_paths,
			const FixpointInferenceOptions &p_options = FixpointInferenceOptions());
};

#endif // TOOLS_ENABLED
```

- [ ] **Step 2: Write the implementation**

Create `modules/foundry_script/editor/gdscript_fixpoint_inference.cpp`:

```cpp
/**************************************************************************/
/*  gdscript_fixpoint_inference.cpp                                       */
/**************************************************************************/

#include "gdscript_fixpoint_inference.h"

#ifdef TOOLS_ENABLED

#include "gdscript_refactoring.h"
#include "gdscript_refactoring_edits.h"

#include "../gdscript_analyzer.h"
#include "../gdscript_cache.h"
#include "../gdscript_parser.h"

#include "core/io/file_access.h"
#include "core/templates/hash_map.h"

namespace {

String read_source(const String &p_path, bool &r_ok) {
	Error err = OK;
	const String source = FileAccess::get_file_as_string(p_path, &err);
	r_ok = (err == OK);
	return source;
}

bool write_source(const String &p_path, const String &p_source) {
	Error err = OK;
	Ref<FileAccess> file = FileAccess::open(p_path, FileAccess::WRITE, &err);
	if (file.is_null() || err != OK) {
		return false;
	}
	file->store_string(p_source);
	return true;
}

// Drop cached parse/script state so a later analysis re-reads p_path from disk.
void invalidate_cache(const String &p_path) {
	GDScriptCache::remove_parser(p_path);
	GDScriptCache::remove_script(p_path);
}

// Re-parse and analyze p_source as if saved at p_path. True when analysis
// succeeds, i.e. the applied annotations introduced no new errors.
bool verify_source(const String &p_path, const String &p_source, const FixpointInferenceOptions &p_options) {
	GDScriptParser parser;
	if (parser.parse(p_source, p_path, false) != OK) {
		return false;
	}
	GDScriptAnalyzer analyzer(&parser);
	analyzer.set_strict_null_checks(p_options.strict_null_checks);
	analyzer.set_strict_dynamic_checks(p_options.strict_dynamic_checks);
	return analyzer.analyze() == OK;
}

int count_enabled_edits(const RefactorContext &p_ctx, Vector<RefactorTextEdit> &r_edits) {
	RefactorCandidatesResult candidates = GDScriptRefactoring::find_candidates(p_ctx, RefactorKind::ADD_TYPE_ANNOTATION);
	if (!candidates.ok) {
		return 0;
	}
	int collected = 0;
	for (const RefactorCandidate &candidate : candidates.candidates) {
		if (!candidate.enabled) {
			continue;
		}
		for (const RefactorTextEdit &edit : candidate.edits) {
			r_edits.push_back(edit);
		}
		collected++;
	}
	return collected;
}

} // namespace

FixpointInferenceResult GDScriptFixpointInference::run(const Vector<String> &p_paths, const FixpointInferenceOptions &p_options) {
	FixpointInferenceResult result;

	// Capture originals for before/after reporting and change detection.
	HashMap<String, String> original_source;
	for (const String &path : p_paths) {
		bool ok = false;
		const String source = read_source(path, ok);
		if (!ok) {
			result.ok = false;
			result.error_message = vformat("Cannot read '%s'.", path);
			return result;
		}
		original_source[path] = source;
	}

	// Determine the iteration ceiling.
	int bound = p_options.max_iterations;
	if (bound <= 0) {
		int first_pass_candidates = 0;
		for (const String &path : p_paths) {
			RefactorContext ctx;
			ctx.path = path;
			ctx.source = original_source[path];
			RefactorCandidatesResult candidates = GDScriptRefactoring::find_candidates(ctx, RefactorKind::ADD_TYPE_ANNOTATION);
			first_pass_candidates += candidates.candidates.size();
		}
		bound = first_pass_candidates + 1;
	}
	const int hard_ceiling = 1000;
	if (bound > hard_ceiling) {
		bound = hard_ceiling;
	}

	HashMap<String, int> applied_per_file;

	int iteration = 0;
	while (iteration < bound) {
		iteration++;

		// Snapshot a consistent view of every file for this pass.
		HashMap<String, String> snapshot;
		for (const String &path : p_paths) {
			bool ok = false;
			const String source = read_source(path, ok);
			if (ok) {
				snapshot[path] = source;
			}
		}

		// Collect + verify accepted rewrites without committing yet.
		HashMap<String, String> pending_source;
		HashMap<String, int> pending_count;
		for (const String &path : p_paths) {
			if (!snapshot.has(path)) {
				continue;
			}
			RefactorContext ctx;
			ctx.path = path;
			ctx.source = snapshot[path];

			Vector<RefactorTextEdit> edits;
			const int enabled = count_enabled_edits(ctx, edits);
			if (enabled == 0 || edits.is_empty()) {
				continue;
			}

			String new_source;
			if (!GDScriptRefactorEdits::apply(snapshot[path], edits, new_source)) {
				continue; // Overlapping/out-of-range edits; skip this file this pass.
			}
			if (!verify_source(path, new_source, p_options)) {
				result.skipped.push_back({ path, -1, "verification rejected the applied annotations" });
				continue;
			}
			pending_source[path] = new_source;
			pending_count[path] = edits.size();
		}

		// Commit at pass end so each iteration advances exactly one dependency layer.
		int applied_this_pass = 0;
		for (const KeyValue<String, String> &entry : pending_source) {
			if (!write_source(entry.key, entry.value)) {
				result.skipped.push_back({ entry.key, -1, "could not write file" });
				continue;
			}
			invalidate_cache(entry.key);
			const int count = pending_count[entry.key];
			applied_this_pass += count;
			applied_per_file[entry.key] += count;
		}

		if (applied_this_pass == 0) {
			break; // Fixpoint reached.
		}
	}

	result.iterations = iteration;

	// Build the change report from captured originals vs final on-disk source.
	for (const String &path : p_paths) {
		bool ok = false;
		const String after = read_source(path, ok);
		if (!ok) {
			continue;
		}
		const String &before = original_source[path];
		if (before == after) {
			continue;
		}
		FixpointFileChange change;
		change.path = path;
		change.before_source = before;
		change.after_source = after;
		change.annotations_applied = applied_per_file.has(path) ? applied_per_file[path] : 0;
		result.total_annotations_applied += change.annotations_applied;
		result.changed_files.push_back(change);
	}

	result.ok = true;
	return result;
}

#endif // TOOLS_ENABLED
```

- [ ] **Step 3: Write the failing test**

Create `modules/foundry_script/tests/test_fixpoint_inference.h`:

```cpp
/**************************************************************************/
/*  test_fixpoint_inference.h                                            */
/**************************************************************************/

#pragma once

#ifdef TOOLS_ENABLED

#include "tests/test_macros.h"

#include "../editor/gdscript_fixpoint_inference.h"

#include "core/io/file_access.h"

#include "test_refactor.h" // GDScriptTests::TemporaryScriptFile

#ifndef GDSCRIPT_NO_LSP

namespace GDScriptTests {

TEST_SUITE("[Modules][Foundry Script][Fixpoint]") {
	TEST_CASE("Single-file run types every resolvable declaration and is idempotent") {
		const String path = "res://refactor/fixpoint_single.fs";
		const String source =
				"func compute():\n"
				"\treturn inner()\n"
				"func inner():\n"
				"\treturn 42\n";
		TemporaryScriptFile file(path, source);

		Vector<String> paths;
		paths.push_back(path);

		FixpointInferenceResult result = GDScriptFixpointInference::run(paths);
		REQUIRE(result.ok);
		REQUIRE_EQ(result.changed_files.size(), 1);

		const String after = FileAccess::get_file_as_string(path);
		CHECK(after.contains("func compute() -> int:"));
		CHECK(after.contains("func inner() -> int:"));

		// Running again on the now-typed file changes nothing.
		FixpointInferenceResult second = GDScriptFixpointInference::run(paths);
		REQUIRE(second.ok);
		CHECK_EQ(second.changed_files.size(), 0);
	}
}

} // namespace GDScriptTests

#endif // !GDSCRIPT_NO_LSP

#endif // TOOLS_ENABLED
```

- [ ] **Step 4: Build and run to verify red→green**

Build: `scons platform=macos target=editor dev_build=yes tests=yes -j$(sysctl -n hw.ncpu)`
Run: `./bin/godot.macos.editor.dev.arm64 --headless --test --test-suite="*Fixpoint*" --force-colors`
Expected: the `[Fixpoint]` suite passes. (If the orchestrator were stubbed, the `contains` checks would FAIL first — implement Step 2 fully to make them pass.)

- [ ] **Step 5: Commit**

```bash
git add modules/foundry_script/editor/gdscript_fixpoint_inference.h \
        modules/foundry_script/editor/gdscript_fixpoint_inference.cpp \
        modules/foundry_script/tests/test_fixpoint_inference.h
git commit -m "feat(gdscript): Headless fixpoint inference orchestrator (#32)"
```

---

### Task 2: Cross-file convergence (acceptance criterion)

**Goal:** Prove the fixpoint types an A→B→C chain across three separate files, whereas a single pass types only the leaf.

**Files:**
- Modify: `modules/foundry_script/tests/test_fixpoint_inference.h` (add a `TEST_CASE`)

**Acceptance Criteria:**
- [ ] A run with `max_iterations = 1` types only the leaf C; A and B remain untyped.
- [ ] A full run types all of A, B, and C, and reports `iterations > 1`.
- [ ] Both runs leave behavior unchanged (files still analyze clean).

**Verify:** `./bin/godot.macos.editor.dev.arm64 --headless --test --test-suite="*Fixpoint*" --force-colors` → passes.

**Steps:**

- [ ] **Step 1: Add the cross-file test case**

Add inside the `TEST_SUITE("[Modules][Foundry Script][Fixpoint]")` block in `modules/foundry_script/tests/test_fixpoint_inference.h`:

```cpp
	TEST_CASE("Cross-file chain converges past the leaf; one pass types only the leaf") {
		const String path_a = "res://refactor/fixpoint_chain_a.fs";
		const String path_b = "res://refactor/fixpoint_chain_b.fs";
		const String path_c = "res://refactor/fixpoint_chain_c.fs";

		// C is the leaf; B relays C; A reads B. Each layer needs the one below it
		// typed and written before its own return/var becomes inferable.
		const String source_c =
				"static func value():\n"
				"\treturn 42\n";
		const String source_b =
				"const C = preload(\"res://refactor/fixpoint_chain_c.fs\")\n"
				"static func relay():\n"
				"\treturn C.value()\n";
		const String source_a =
				"const B = preload(\"res://refactor/fixpoint_chain_b.fs\")\n"
				"var x = B.relay()\n";

		Vector<String> paths;
		paths.push_back(path_a);
		paths.push_back(path_b);
		paths.push_back(path_c);

		SUBCASE("single pass types only the leaf") {
			TemporaryScriptFile file_a(path_a, source_a);
			TemporaryScriptFile file_b(path_b, source_b);
			TemporaryScriptFile file_c(path_c, source_c);

			FixpointInferenceOptions options;
			options.max_iterations = 1;
			FixpointInferenceResult result = GDScriptFixpointInference::run(paths, options);
			REQUIRE(result.ok);

			CHECK(FileAccess::get_file_as_string(path_c).contains("static func value() -> int:"));
			CHECK_FALSE(FileAccess::get_file_as_string(path_b).contains("-> int"));
			CHECK_FALSE(FileAccess::get_file_as_string(path_a).contains(": int"));
		}

		SUBCASE("full fixpoint types the whole chain") {
			TemporaryScriptFile file_a(path_a, source_a);
			TemporaryScriptFile file_b(path_b, source_b);
			TemporaryScriptFile file_c(path_c, source_c);

			FixpointInferenceResult result = GDScriptFixpointInference::run(paths);
			REQUIRE(result.ok);
			CHECK(result.iterations > 1);

			CHECK(FileAccess::get_file_as_string(path_c).contains("static func value() -> int:"));
			CHECK(FileAccess::get_file_as_string(path_b).contains("static func relay() -> int:"));
			CHECK(FileAccess::get_file_as_string(path_a).contains("var x: int = B.relay()"));
		}
	}
```

- [ ] **Step 2: Build and run**

Run: `./bin/godot.macos.editor.dev.arm64 --headless --test --test-suite="*Fixpoint*" --force-colors`
Expected: both subcases pass.

> Note on assertion brittleness: the exact rendered form of A's member (`var x: int = B.relay()` vs `var x := B.relay()`) depends on how Add Type Annotation renders a member with an initializer. If the build shows a different-but-correct spelling, update the `contains` text in the full-fixpoint subcase to match the engine's actual output — do NOT change the orchestrator to force a spelling. Confirm the chosen spelling by reading the produced file from the test failure message.

- [ ] **Step 3: Commit**

```bash
git add modules/foundry_script/tests/test_fixpoint_inference.h
git commit -m "test(gdscript): Cross-file fixpoint convergence acceptance test (#32)"
```

---

### Task 3: Termination on cycles and honest reporting

**Goal:** Guarantee the loop terminates on a dependency cycle without crashing, and assert the change report counts are honest.

**Files:**
- Modify: `modules/foundry_script/tests/test_fixpoint_inference.h` (add a `TEST_CASE`)

**Acceptance Criteria:**
- [ ] A run over two mutually-referencing files terminates (does not hit the hard ceiling, does not crash) and types what is provable.
- [ ] `total_annotations_applied` equals the sum of per-file `annotations_applied`, and each changed file reports a positive count.

**Verify:** `./bin/godot.macos.editor.dev.arm64 --headless --test --test-suite="*Fixpoint*" --force-colors` → passes.

**Steps:**

- [ ] **Step 1: Add the cycle + reporting test case**

Add inside the `TEST_SUITE("[Modules][Foundry Script][Fixpoint]")` block:

```cpp
	TEST_CASE("Mutually-referencing files terminate and report honest counts") {
		const String path_a = "res://refactor/fixpoint_cycle_a.fs";
		const String path_b = "res://refactor/fixpoint_cycle_b.fs";

		// A and B reference each other; each also has an independently-typable leaf
		// so the run produces some annotations and then converges.
		const String source_a =
				"const B = preload(\"res://refactor/fixpoint_cycle_b.fs\")\n"
				"static func a_leaf():\n"
				"\treturn 1\n"
				"static func uses_b():\n"
				"\treturn B.b_leaf()\n";
		const String source_b =
				"const A = preload(\"res://refactor/fixpoint_cycle_a.fs\")\n"
				"static func b_leaf():\n"
				"\treturn 2\n"
				"static func uses_a():\n"
				"\treturn A.a_leaf()\n";

		TemporaryScriptFile file_a(path_a, source_a);
		TemporaryScriptFile file_b(path_b, source_b);

		Vector<String> paths;
		paths.push_back(path_a);
		paths.push_back(path_b);

		FixpointInferenceResult result = GDScriptFixpointInference::run(paths);
		REQUIRE(result.ok);

		// Terminated well under the safety ceiling.
		CHECK(result.iterations < 1000);

		// The independently-typable leaves are resolved.
		CHECK(FileAccess::get_file_as_string(path_a).contains("static func a_leaf() -> int:"));
		CHECK(FileAccess::get_file_as_string(path_b).contains("static func b_leaf() -> int:"));

		// Report counts are internally consistent.
		int summed = 0;
		for (const FixpointFileChange &change : result.changed_files) {
			CHECK(change.annotations_applied > 0);
			summed += change.annotations_applied;
		}
		CHECK_EQ(summed, result.total_annotations_applied);
	}
```

- [ ] **Step 2: Build and run the full suite**

Run: `./bin/godot.macos.editor.dev.arm64 --headless --test --test-suite="*Fixpoint*" --force-colors`
Expected: all three test cases pass.

- [ ] **Step 3: Run the broader refactor suite for regressions**

Run: `./bin/godot.macos.editor.dev.arm64 --headless --test --test-suite="*Refactor*" --force-colors`
Expected: no regressions (the new module compiles alongside existing refactor tests; nothing existing changed).

- [ ] **Step 4: Commit**

```bash
git add modules/foundry_script/tests/test_fixpoint_inference.h
git commit -m "test(gdscript): Fixpoint termination on cycles and report consistency (#32)"
```

---

## Notes for the implementer

- All new code is editor-only: keep everything under `#ifdef TOOLS_ENABLED`. The test header additionally guards with `#ifndef GDSCRIPT_NO_LSP` because it reuses `TemporaryScriptFile` from `test_refactor.h`, which lives under that guard.
- `res://` resolves to `modules/foundry_script/tests/scripts` in the test runner, so `res://refactor/...` temp paths land in the committed fixtures directory. `TemporaryScriptFile` deletes them on scope exit; do not commit any `fixpoint_*.fs` fixture files — the sources are generated inline by the tests.
- The orchestrator writes to disk by design (cross-file analysis reads dependencies from disk only). The wizard (#41/#42) will wrap this with undo using the captured `before_source`; that wrapping is out of scope here.
- If CI parity is needed, also build with `dev_mode=yes` to catch warnings-as-errors.
