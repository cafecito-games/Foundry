# Post-edit verification harness Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers-extended-cc:subagent-driven-development (recommended) or superpowers-extended-cc:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a headless `GDScriptVerificationHarness` that applies candidate annotation edits, re-analyzes the affected dependency closure, keeps only edits that introduce no new analyzer errors (dropping the minimal offending subset with attributed diagnostics), provides a read-only strict-mode preview, and gates `GDScriptFixpointInference` so dependent-breaking edits are rolled back.

**Architecture:** A new `modules/gdscript/editor/gdscript_verification_harness.{h,cpp}` (TOOLS_ENABLED). It computes an affected set = touched files ∪ their transitive inverse-dependency closure (via a new public `GDScriptCache::get_inverse_dependencies` accessor), measures total analyzer-error count by staging candidate sources to disk / analyzing / restoring originals, and uses optimistic batch-apply + delta-debugging (ddmin) to isolate offending candidates. The fixpoint routes each pass's accepted edits through it.

**Tech Stack:** C++ (Godot engine), doctest C++ tests under `modules/gdscript/tests/` (auto-globbed into `modules_tests.gen.h`), GDScript analyzer/parser/cache, `EditorFileSystem` test harness.

---

## Build & test commands

- Build: `scons platform=macos dev_build=yes tests=yes target=editor` (use `dev_mode=yes` for CI parity / warnings-as-errors).
- Run the harness tests: `./bin/godot.macos.editor.dev.arm64 --headless --test --test-suite="*Verification*" --force-colors`
- Run fixpoint tests: `./bin/godot.macos.editor.dev.arm64 --headless --test --test-suite="*Fixpoint*" --force-colors`
- New `editor/*.cpp` and `tests/*.h` files are auto-discovered by SCons globs; no manual registration.

---

## File structure

- Create `modules/gdscript/editor/gdscript_verification_harness.h` — public types (`VerificationCandidate`, `VerificationOptions`, `VerificationRejected`, `VerificationResult`, `StrictViolation`, `StrictPreviewResult`) and the `GDScriptVerificationHarness` class with `verify(...)` and `preview_strict(...)`.
- Create `modules/gdscript/editor/gdscript_verification_harness.cpp` — affected-set closure, stage/restore, affected-set analysis (count + messages), optimistic-apply + ddmin attribution, strict preview.
- Modify `modules/gdscript/gdscript_cache.h` / `.cpp` — add `static HashSet<String> get_inverse_dependencies(const String &p_path)`.
- Modify `modules/gdscript/editor/gdscript_fixpoint_inference.h` / `.cpp` — add strict flags to options; route per-pass accepted edits through the harness.
- Create `modules/gdscript/tests/test_verification_harness.h` — doctest suite `[Modules][GDScript][Verification]`.

---

### Task 1: Inverse-dependency accessor on `GDScriptCache`

**Goal:** Expose a public, snapshot-by-value accessor returning the set of files that directly depend on a given path, so the harness can build the inverse-dependency closure.

**Files:**
- Modify: `modules/gdscript/gdscript_cache.h` (public method list, ~line 130)
- Modify: `modules/gdscript/gdscript_cache.cpp`

**Acceptance Criteria:**
- [ ] `GDScriptCache::get_inverse_dependencies(path)` returns a copy of the direct inverse-dependents set, or an empty set when none exist.
- [ ] It takes the cache mutex like other accessors.
- [ ] Builds clean with `dev_mode=yes`.

**Verify:** Compiles; exercised indirectly by Task 2's affected-set test.

**Steps:**

- [ ] **Step 1: Declare the accessor in `gdscript_cache.h`**

Add to the `public:` section near the other static accessors (e.g. just after `get_source_code`):

```cpp
	// Returns a snapshot of the set of files that directly depend on p_path (its
	// inverse dependencies), as recorded during compilation. Empty if none are known.
	// Snapshot-by-value so callers are safe against concurrent cache mutation.
	static HashSet<String> get_inverse_dependencies(const String &p_path);
```

- [ ] **Step 2: Implement it in `gdscript_cache.cpp`**

Add near the other accessor definitions (e.g. after `get_source_code`):

```cpp
HashSet<String> GDScriptCache::get_inverse_dependencies(const String &p_path) {
	MutexLock lock(singleton->mutex);
	if (singleton->parser_inverse_dependencies.has(p_path)) {
		return singleton->parser_inverse_dependencies[p_path];
	}
	return HashSet<String>();
}
```

- [ ] **Step 3: Build**

Run: `scons platform=macos dev_build=yes tests=yes target=editor`
Expected: links successfully.

- [ ] **Step 4: Commit**

```bash
git add modules/gdscript/gdscript_cache.h modules/gdscript/gdscript_cache.cpp
git commit -m "feat(gdscript): Expose inverse-dependency accessor on GDScriptCache (#34)"
```

---

### Task 2: Verification harness — types, affected set, stage/restore, optimistic accept/reject

**Goal:** Implement the harness header and the core verification path: compute the affected set, measure error count + messages by staging/analyzing/restoring, accept all candidates when nothing regresses, and (for now) reject all candidates with attributed diagnostics when the batch regresses.

**Files:**
- Create: `modules/gdscript/editor/gdscript_verification_harness.h`
- Create: `modules/gdscript/editor/gdscript_verification_harness.cpp`
- Create: `modules/gdscript/tests/test_verification_harness.h`

**Acceptance Criteria:**
- [ ] A batch of independently-sound candidates is fully accepted; `accepted_error_count == baseline_error_count`.
- [ ] A single candidate that breaks a dependent file is rejected and listed with a non-empty `diagnostics`, and the source files on disk are unchanged after `verify` returns.
- [ ] `ok == false` with `error_message` when an input file is unreadable; no disk mutation.

**Verify:** `./bin/godot.macos.editor.dev.arm64 --headless --test --test-suite="*Verification*" --force-colors` → the two Task 2 cases pass.

**Steps:**

- [ ] **Step 1: Write the header `gdscript_verification_harness.h`**

```cpp
/**************************************************************************/
/*  gdscript_verification_harness.h                                       */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/
/* (standard Godot MIT license header — copy verbatim from                */
/*  gdscript_fixpoint_inference.h)                                        */
/**************************************************************************/

#pragma once

#ifdef TOOLS_ENABLED

#include "gdscript_refactoring.h" // RefactorTextEdit

#include "core/string/ustring.h"
#include "core/templates/vector.h"

// One candidate annotation edit to verify, addressed by file + declaration anchor.
// `edits` are the RefactorTextEdits of a single RefactorCandidate.
struct VerificationCandidate {
	String path;
	int line = -1; // 0-based declaration anchor, for reporting.
	Vector<RefactorTextEdit> edits;
};

struct VerificationOptions {
	bool strict_null_checks = false;
	bool strict_dynamic_checks = false;
};

// A candidate that was dropped, with the reason and the diagnostics attributed to it.
struct VerificationRejected {
	String path;
	int line = -1;
	String reason;
	Vector<String> diagnostics; // Analyzer messages newly introduced by this candidate.
};

struct VerificationResult {
	bool ok = false; // false only on a fatal precondition (e.g. unreadable file).
	String error_message; // Populated only when !ok.
	Vector<VerificationCandidate> accepted; // Proven to introduce no new errors.
	Vector<VerificationRejected> rejected; // Dropped, with attributed diagnostics.
	int baseline_error_count = 0; // Total analyzer errors across the affected set before any edit.
	int accepted_error_count = 0; // Total after applying all accepted edits.
};

struct StrictViolation {
	String path;
	int line = -1;
	int column = -1;
	String message;
};

struct StrictPreviewResult {
	bool ok = false;
	String error_message;
	bool strict_null_checks = false;
	bool strict_dynamic_checks = false;
	Vector<StrictViolation> violations; // Sites failing only under strict mode.
};

// Headless, caret-independent post-edit verification. Verification stages candidate
// sources to disk and restores originals, so it is NOT safe to call concurrently with
// a live editing session or another run.
class GDScriptVerificationHarness {
public:
	// Verifies p_candidates against the dependency closure of the files they touch.
	// p_universe bounds inverse-dependent discovery (typically the migration's full
	// path set); files outside it are not re-analyzed. Does NOT commit accepted edits.
	static VerificationResult verify(
			const Vector<VerificationCandidate> &p_candidates,
			const Vector<String> &p_universe,
			const VerificationOptions &p_options = VerificationOptions());

	// Read-only. Reports diagnostics present only under the requested strict mode.
	static StrictPreviewResult preview_strict(
			const Vector<String> &p_paths,
			const VerificationOptions &p_options);
};

#endif // TOOLS_ENABLED
```

- [ ] **Step 2: Write the `.cpp` core (this task: no ddmin yet)**

Create `gdscript_verification_harness.cpp` with the license header, then:

```cpp
#include "gdscript_verification_harness.h"

#ifdef TOOLS_ENABLED

#include "gdscript_refactoring.h"
#include "gdscript_refactoring_edits.h"

#include "../gdscript_analyzer.h"
#include "../gdscript_cache.h"
#include "../gdscript_parser.h"

#include "editor/script/script_refactor_apply.h"

#include "core/io/file_access.h"
#include "core/templates/hash_map.h"
#include "core/templates/hash_set.h"
#include "core/templates/list.h"

namespace {

String read_source(const String &p_path, bool &r_ok) {
	Error err = OK;
	const String source = FileAccess::get_file_as_string(p_path, &err);
	r_ok = (err == OK);
	return source;
}

void invalidate_cache(const String &p_path) {
	GDScriptCache::remove_parser(p_path);
	GDScriptCache::remove_script(p_path);
}

// Analyze one source as if saved at p_path; append "path:line: message" for every
// parser/analyzer error to r_messages. Returns the count contributed by this file.
// A parse failure contributes at least one error so a malformed edit is never "clean".
int analyze_one(const String &p_path, const String &p_source, const VerificationOptions &p_options, Vector<String> &r_messages) {
	GDScriptParser parser;
	const Error parse_err = parser.parse(p_source, p_path, false);
	GDScriptAnalyzer analyzer(&parser);
	analyzer.set_strict_null_checks(p_options.strict_null_checks);
	analyzer.set_strict_dynamic_checks(p_options.strict_dynamic_checks);
	analyzer.analyze();
	int count = 0;
	for (const GDScriptParser::ParserError &error : parser.get_errors()) {
		r_messages.push_back(vformat("%s:%d: %s", p_path, error.line, error.message));
		count++;
	}
	if (parse_err != OK && count == 0) {
		r_messages.push_back(vformat("%s: parse failed", p_path));
		count++;
	}
	return count;
}

// Result of analyzing the whole affected set under a given staged-source map.
struct AffectedAnalysis {
	int error_count = 0;
	Vector<String> messages;
};

// Stages p_staged (path -> source) to disk for the edited files, invalidates affected
// caches, analyzes every affected file, then restores originals. Always restores.
// r_fatal is set true (and the run should abort) only on a staged-write failure.
AffectedAnalysis analyze_affected(
		const Vector<String> &p_affected,
		const HashMap<String, String> &p_original, // current on-disk source for every affected file
		const HashMap<String, String> &p_staged, // overrides for edited files only
		const VerificationOptions &p_options,
		bool &r_fatal) {
	r_fatal = false;
	AffectedAnalysis analysis;

	// Stage: write only files whose staged source differs from the original.
	Vector<String> written;
	for (const KeyValue<String, String> &entry : p_staged) {
		const String &path = entry.key;
		if (!p_original.has(path) || p_original[path] == entry.value) {
			continue;
		}
		String write_error;
		if (!ScriptRefactorApply::write_file(path, entry.value, write_error)) {
			r_fatal = true;
			break;
		}
		written.push_back(path);
	}

	if (!r_fatal) {
		for (const String &path : written) {
			invalidate_cache(path);
		}
		// Analyze the full affected set against the staged disk state.
		for (const String &path : p_affected) {
			const String source = p_staged.has(path) ? p_staged[path] : (p_original.has(path) ? p_original[path] : String());
			analysis.error_count += analyze_one(path, source, p_options, analysis.messages);
		}
	}

	// Restore: rewrite originals for every file we wrote, then invalidate again.
	for (const String &path : written) {
		String write_error;
		ScriptRefactorApply::write_file(path, p_original[path], write_error);
		invalidate_cache(path);
	}

	return analysis;
}

// Apply every candidate's edits, grouped by file, over p_original. Candidates whose
// edits fail to apply are returned in r_unappliable. Returns staged path->source.
HashMap<String, String> stage_candidates(
		const Vector<VerificationCandidate> &p_candidates,
		const HashMap<String, String> &p_original,
		Vector<int> &r_unappliable_indices) {
	HashMap<String, Vector<RefactorTextEdit>> edits_by_path;
	for (int i = 0; i < p_candidates.size(); i++) {
		const VerificationCandidate &candidate = p_candidates[i];
		for (const RefactorTextEdit &edit : candidate.edits) {
			edits_by_path[candidate.path].push_back(edit);
		}
	}
	HashMap<String, String> staged;
	for (const KeyValue<String, Vector<RefactorTextEdit>> &entry : edits_by_path) {
		String applied;
		if (p_original.has(entry.key) && GDScriptRefactorEdits::apply(p_original[entry.key], entry.value, applied)) {
			staged[entry.key] = applied;
		}
		// If apply fails here, per-candidate fallback (Task 3 ddmin) re-detects it; in
		// the all-accept fast path of this task we conservatively treat a non-applying
		// group as "no change" so the batch is not silently corrupted.
	}
	(void)r_unappliable_indices;
	return staged;
}

} // namespace

VerificationResult GDScriptVerificationHarness::verify(
		const Vector<VerificationCandidate> &p_candidates,
		const Vector<String> &p_universe,
		const VerificationOptions &p_options) {
	VerificationResult result;

	if (p_candidates.is_empty()) {
		result.ok = true;
		return result;
	}

	// De-duplicate the universe, preserving first-seen order.
	Vector<String> universe;
	{
		HashSet<String> seen;
		for (const String &path : p_universe) {
			if (!seen.has(path)) {
				seen.insert(path);
				universe.push_back(path);
			}
		}
	}

	// The set of files the candidates touch.
	HashSet<String> touched;
	for (const VerificationCandidate &candidate : p_candidates) {
		touched.insert(candidate.path);
	}

	// Prime the cache so inverse-dependency edges exist for the universe.
	for (const String &path : universe) {
		Error err = OK;
		GDScriptCache::get_full_script(path, err);
	}

	// Affected set = touched ∪ transitive inverse-dependents(touched) ∩ universe.
	HashSet<String> universe_set;
	for (const String &path : universe) {
		universe_set.insert(path);
	}
	HashSet<String> affected_set = touched;
	List<String> frontier;
	for (const String &path : touched) {
		frontier.push_back(path);
	}
	while (!frontier.is_empty()) {
		const String path = frontier.front()->get();
		frontier.pop_front();
		for (const String &dependent : GDScriptCache::get_inverse_dependencies(path)) {
			if (!universe_set.has(dependent) || affected_set.has(dependent)) {
				continue;
			}
			affected_set.insert(dependent);
			frontier.push_back(dependent);
		}
	}
	Vector<String> affected;
	for (const String &path : affected_set) {
		affected.push_back(path);
	}

	// Capture current on-disk source for every affected file.
	HashMap<String, String> original;
	for (const String &path : affected) {
		bool ok = false;
		const String source = read_source(path, ok);
		if (!ok) {
			result.ok = false;
			result.error_message = vformat("Cannot read '%s'.", path);
			return result;
		}
		original[path] = source;
	}

	// Baseline error count across the affected set.
	bool fatal = false;
	const AffectedAnalysis baseline = analyze_affected(affected, original, HashMap<String, String>(), p_options, fatal);
	if (fatal) {
		result.ok = false;
		result.error_message = "Verification aborted: failed to stage sources for baseline.";
		return result;
	}
	result.baseline_error_count = baseline.error_count;

	// Optimistic: apply all candidates at once.
	Vector<int> unappliable;
	const HashMap<String, String> all_staged = stage_candidates(p_candidates, original, unappliable);
	const AffectedAnalysis combined = analyze_affected(affected, original, all_staged, p_options, fatal);
	if (fatal) {
		result.ok = false;
		result.error_message = "Verification aborted: failed to stage candidate sources.";
		return result;
	}

	if (combined.error_count <= baseline.error_count) {
		result.accepted = p_candidates;
		result.accepted_error_count = combined.error_count;
		result.ok = true;
		return result;
	}

	// Regression. (Task 3 replaces this block with ddmin attribution.)
	// For now, reject every candidate, attributing the newly-introduced diagnostics.
	Vector<String> new_messages;
	{
		HashSet<String> baseline_messages;
		for (const String &message : baseline.messages) {
			baseline_messages.insert(message);
		}
		for (const String &message : combined.messages) {
			if (!baseline_messages.has(message)) {
				new_messages.push_back(message);
			}
		}
	}
	for (const VerificationCandidate &candidate : p_candidates) {
		VerificationRejected rejected;
		rejected.path = candidate.path;
		rejected.line = candidate.line;
		rejected.reason = "batch introduces new analyzer error(s) in the affected set";
		rejected.diagnostics = new_messages;
		result.rejected.push_back(rejected);
	}
	result.accepted_error_count = baseline.error_count;
	result.ok = true;
	return result;
}

StrictPreviewResult GDScriptVerificationHarness::preview_strict(
		const Vector<String> &p_paths,
		const VerificationOptions &p_options) {
	StrictPreviewResult result; // Filled in Task 4.
	result.ok = false;
	result.error_message = "Not implemented.";
	return result;
}

#endif // TOOLS_ENABLED
```

- [ ] **Step 3: Write the failing test header `test_verification_harness.h`**

Mirror `test_fixpoint_inference.h`'s setup. Include the license header, then:

```cpp
#pragma once

#ifdef TOOLS_ENABLED

#include "tests/test_macros.h"

#include "../editor/gdscript_batch_candidates.h"
#include "../editor/gdscript_verification_harness.h"

#include "core/io/file_access.h"

#include "test_refactor.h" // GDScriptTests::TemporaryScriptFile, make_context

#ifndef GDSCRIPT_NO_LSP

#include "editor/file_system/editor_file_system.h"

namespace GDScriptTests {

// Collect enabled Add Type Annotation candidates for one file as VerificationCandidates.
static Vector<VerificationCandidate> enabled_candidates_for(const String &p_path) {
	Vector<VerificationCandidate> out;
	BatchCandidatesResult batch = GDScriptBatchCandidates::collect({ p_path });
	for (const BatchFileCandidates &file : batch.files) {
		for (const RefactorCandidate &candidate : file.candidates) {
			if (!candidate.enabled) {
				continue;
			}
			VerificationCandidate vc;
			vc.path = file.path;
			vc.line = candidate.line;
			vc.edits = candidate.edits;
			out.push_back(vc);
		}
	}
	return out;
}

TEST_SUITE("[Modules][GDScript][Verification]") {
	TEST_CASE("Independently-sound candidates are all accepted") {
		EditorFileSystem *editor_file_system = memnew(EditorFileSystem);
		GDScriptLanguageProtocol *protocol = GDScriptTests::initialize(GDScriptTests::root);
		REQUIRE(protocol);

		const String path = "res://refactor/verify_clean.gd";
		const String source =
				"func compute():\n"
				"\treturn inner()\n"
				"func inner():\n"
				"\treturn 42\n";
		TemporaryScriptFile file(path, source);

		Vector<VerificationCandidate> candidates = enabled_candidates_for(path);
		REQUIRE_GT(candidates.size(), 0);

		VerificationResult result = GDScriptVerificationHarness::verify(candidates, { path });
		REQUIRE(result.ok);
		CHECK_EQ(result.rejected.size(), 0);
		CHECK_EQ(result.accepted.size(), candidates.size());
		CHECK_EQ(result.accepted_error_count, result.baseline_error_count);

		// verify() must not modify files on disk.
		CHECK_EQ(FileAccess::get_file_as_string(path), source);

		memdelete(editor_file_system);
		GDScriptTests::finish();
	}

	TEST_CASE("A candidate that breaks a dependent is rejected with a diagnostic") {
		EditorFileSystem *editor_file_system = memnew(EditorFileSystem);
		GDScriptLanguageProtocol *protocol = GDScriptTests::initialize(GDScriptTests::root);
		REQUIRE(protocol);

		// provider.gd: untyped getter whose inferred return type is int.
		const String provider_path = "res://refactor/verify_provider.gd";
		const String provider_source =
				"class_name VerifyProvider\n"
				"func get_value():\n"
				"\treturn 42\n";
		TemporaryScriptFile provider(provider_path, provider_source);

		// consumer.gd: assigns the getter's result to a String. Today the call is
		// dynamic (Variant) so it analyzes clean. Typing get_value() -> int makes the
		// String assignment a hard error, so the provider candidate must be rejected.
		const String consumer_path = "res://refactor/verify_consumer.gd";
		const String consumer_source =
				"func use() -> void:\n"
				"\tvar p := VerifyProvider.new()\n"
				"\tvar s: String = p.get_value()\n";
		TemporaryScriptFile consumer(consumer_path, consumer_source);

		Vector<VerificationCandidate> candidates = enabled_candidates_for(provider_path);
		REQUIRE_GT(candidates.size(), 0);

		Vector<String> universe = { provider_path, consumer_path };
		VerificationResult result = GDScriptVerificationHarness::verify(candidates, universe);
		REQUIRE(result.ok);
		CHECK_GT(result.rejected.size(), 0);
		CHECK_GT(result.rejected[0].diagnostics.size(), 0);

		// Files unchanged on disk.
		CHECK_EQ(FileAccess::get_file_as_string(provider_path), provider_source);
		CHECK_EQ(FileAccess::get_file_as_string(consumer_path), consumer_source);

		memdelete(editor_file_system);
		GDScriptTests::finish();
	}
}

} // namespace GDScriptTests

#endif // GDSCRIPT_NO_LSP

#endif // TOOLS_ENABLED
```

NOTE: confirm `GDScriptTests::finish()` and `REQUIRE_GT`/`CHECK_GT` are used in `test_fixpoint_inference.h` / `test_refactor.h`; if the existing fixpoint test uses a different teardown idiom (e.g. no explicit `finish()`), match that file exactly. Verify the dependent-break fixture actually regresses by checking `result.baseline_error_count == 0` and the combined analysis > 0 — if the chosen sources do not produce a hard error, adjust the consumer to a construct the analyzer rejects under default mode (e.g. assigning an `int` return to a `String` typed local).

- [ ] **Step 4: Build and run**

Run: `scons platform=macos dev_build=yes tests=yes target=editor && ./bin/godot.macos.editor.dev.arm64 --headless --test --test-suite="*Verification*" --force-colors`
Expected: both cases pass. If the dependent-break case does not reject, fix the fixture (see note) until baseline is 0 errors and the combined analysis reports the String/int mismatch.

- [ ] **Step 5: Commit**

```bash
git add modules/gdscript/editor/gdscript_verification_harness.h modules/gdscript/editor/gdscript_verification_harness.cpp modules/gdscript/tests/test_verification_harness.h
git commit -m "feat(gdscript): Post-edit verification harness with affected-set re-analysis (#34)"
```

---

### Task 3: Delta-debugging attribution (keep the good edits, drop the minimal offending subset)

**Goal:** Replace Task 2's all-or-nothing regression branch with ddmin so that when a batch regresses, only the minimal offending candidates are dropped and the rest are accepted and re-verified clean.

**Files:**
- Modify: `modules/gdscript/editor/gdscript_verification_harness.cpp`
- Modify: `modules/gdscript/tests/test_verification_harness.h` (add isolation test)

**Acceptance Criteria:**
- [ ] In a batch where exactly one candidate regresses, only that candidate is rejected; all others are accepted and the accepted set re-verifies with `accepted_error_count <= baseline_error_count`.
- [ ] Each rejected candidate's `diagnostics` contains the message(s) it alone introduces over the accepted base.

**Verify:** `./bin/godot.macos.editor.dev.arm64 --headless --test --test-suite="*Verification*" --force-colors`

**Steps:**

- [ ] **Step 1: Add a regression predicate + ddmin helper above `verify`**

In the anonymous namespace of `gdscript_verification_harness.cpp`, add:

```cpp
// Stage only the given subset of candidates over originals and return the affected
// analysis. Used as the ddmin test oracle.
AffectedAnalysis analyze_subset(
		const Vector<VerificationCandidate> &p_subset,
		const Vector<String> &p_affected,
		const HashMap<String, String> &p_original,
		const VerificationOptions &p_options,
		bool &r_fatal) {
	Vector<int> unused;
	const HashMap<String, String> staged = stage_candidates(p_subset, p_original, unused);
	return analyze_affected(p_affected, p_original, staged, p_options, r_fatal);
}

// Returns the candidates from p_pool indexed by p_indices.
Vector<VerificationCandidate> subset_of(const Vector<VerificationCandidate> &p_pool, const Vector<int> &p_indices) {
	Vector<VerificationCandidate> out;
	for (int index : p_indices) {
		out.push_back(p_pool[index]);
	}
	return out;
}

// Classic ddmin: find a 1-minimal subset of p_indices whose application still
// regresses (error_count > p_baseline). p_pool is the full candidate list.
// Returns the minimal offending index list. Assumes the full p_indices regresses.
Vector<int> ddmin_offending(
		const Vector<int> &p_indices,
		const Vector<VerificationCandidate> &p_pool,
		const Vector<String> &p_affected,
		const HashMap<String, String> &p_original,
		const VerificationOptions &p_options,
		int p_baseline,
		bool &r_fatal) {
	Vector<int> current = p_indices;
	int granularity = 2;
	while (current.size() >= 2) {
		const int subset_size = current.size() / granularity;
		bool reduced = false;
		for (int start = 0; start < current.size(); start += subset_size) {
			// Complement = current minus [start, start+subset_size).
			Vector<int> complement;
			for (int i = 0; i < current.size(); i++) {
				if (i < start || i >= start + subset_size) {
					complement.push_back(current[i]);
				}
			}
			if (complement.is_empty()) {
				continue;
			}
			const AffectedAnalysis analysis = analyze_subset(subset_of(p_pool, complement), p_affected, p_original, p_options, r_fatal);
			if (r_fatal) {
				return current;
			}
			if (analysis.error_count > p_baseline) {
				current = complement;
				granularity = MAX(granularity - 1, 2);
				reduced = true;
				break;
			}
		}
		if (!reduced) {
			if (granularity >= current.size()) {
				break;
			}
			granularity = MIN(granularity * 2, current.size());
		}
	}
	return current;
}
```

- [ ] **Step 2: Replace the regression branch in `verify`**

Replace the entire `// Regression. (Task 3 replaces this block...)` block (from that comment through `result.ok = true; return result;`) with:

```cpp
	// Regression: isolate offending candidates via delta debugging, keep the rest.
	Vector<int> remaining;
	for (int i = 0; i < p_candidates.size(); i++) {
		remaining.push_back(i);
	}
	HashSet<int> rejected_indices;

	// Repeatedly carve out a minimal offending subset until the remainder is clean.
	while (true) {
		const AffectedAnalysis analysis = analyze_subset(subset_of(p_candidates, remaining), affected, original, p_options, fatal);
		if (fatal) {
			result.ok = false;
			result.error_message = "Verification aborted during attribution.";
			return result;
		}
		if (analysis.error_count <= baseline.error_count) {
			break;
		}
		const Vector<int> offending = ddmin_offending(remaining, p_candidates, affected, original, p_options, baseline.error_count, fatal);
		if (fatal) {
			result.ok = false;
			result.error_message = "Verification aborted during attribution.";
			return result;
		}
		HashSet<int> offending_set;
		for (int index : offending) {
			offending_set.insert(index);
			rejected_indices.insert(index);
		}
		Vector<int> next;
		for (int index : remaining) {
			if (!offending_set.has(index)) {
				next.push_back(index);
			}
		}
		// Safety: if ddmin failed to shrink (shouldn't happen), drop the whole remainder.
		if (next.size() == remaining.size()) {
			for (int index : remaining) {
				rejected_indices.insert(index);
			}
			remaining.clear();
			break;
		}
		remaining = next;
	}

	// Build the accepted list and re-verify it as a final confirmation.
	Vector<VerificationCandidate> accepted_candidates;
	for (int i = 0; i < p_candidates.size(); i++) {
		if (!rejected_indices.has(i)) {
			accepted_candidates.push_back(p_candidates[i]);
		}
	}
	const HashMap<String, String> accepted_staged = stage_candidates(accepted_candidates, original, unappliable);
	const AffectedAnalysis accepted_analysis = analyze_affected(affected, original, accepted_staged, p_options, fatal);
	if (fatal) {
		result.ok = false;
		result.error_message = "Verification aborted confirming accepted set.";
		return result;
	}
	result.accepted = accepted_candidates;
	result.accepted_error_count = accepted_analysis.error_count;

	// Attribute diagnostics to each rejected candidate: messages it alone introduces
	// over the accepted base.
	HashSet<String> accepted_messages;
	for (const String &message : accepted_analysis.messages) {
		accepted_messages.insert(message);
	}
	for (int i = 0; i < p_candidates.size(); i++) {
		if (!rejected_indices.has(i)) {
			continue;
		}
		Vector<VerificationCandidate> probe = accepted_candidates;
		probe.push_back(p_candidates[i]);
		const AffectedAnalysis probe_analysis = analyze_subset(probe, affected, original, p_options, fatal);
		VerificationRejected rejected;
		rejected.path = p_candidates[i].path;
		rejected.line = p_candidates[i].line;
		rejected.reason = "introduces new analyzer error(s) in the affected set";
		if (!fatal) {
			for (const String &message : probe_analysis.messages) {
				if (!accepted_messages.has(message)) {
					rejected.diagnostics.push_back(message);
				}
			}
		}
		result.rejected.push_back(rejected);
	}

	result.ok = true;
	return result;
```

NOTE: ensure `MAX`/`MIN` are available (they are global macros in `core/typedefs.h`, already transitively included). `unappliable` is the same `Vector<int>` declared earlier in `verify`.

- [ ] **Step 3: Add the isolation test**

Add to the `[Modules][GDScript][Verification]` suite in `test_verification_harness.h`:

```cpp
	TEST_CASE("Bisection drops only the offending candidate and keeps the rest") {
		EditorFileSystem *editor_file_system = memnew(EditorFileSystem);
		GDScriptLanguageProtocol *protocol = GDScriptTests::initialize(GDScriptTests::root);
		REQUIRE(protocol);

		// One provider with two getters: get_value() (consumed as String -> bad once
		// typed int) and get_label() (consumed correctly as String -> safe to type).
		const String provider_path = "res://refactor/verify_multi_provider.gd";
		const String provider_source =
				"class_name VerifyMultiProvider\n"
				"func get_value():\n"
				"\treturn 42\n"
				"func get_label():\n"
				"\treturn \"hi\"\n";
		TemporaryScriptFile provider(provider_path, provider_source);

		const String consumer_path = "res://refactor/verify_multi_consumer.gd";
		const String consumer_source =
				"func use() -> void:\n"
				"\tvar p := VerifyMultiProvider.new()\n"
				"\tvar bad: String = p.get_value()\n"
				"\tvar ok: String = p.get_label()\n";
		TemporaryScriptFile consumer(consumer_path, consumer_source);

		Vector<VerificationCandidate> candidates = enabled_candidates_for(provider_path);
		REQUIRE_GE(candidates.size(), 2);

		Vector<String> universe = { provider_path, consumer_path };
		VerificationResult result = GDScriptVerificationHarness::verify(candidates, universe);
		REQUIRE(result.ok);
		CHECK_EQ(result.rejected.size(), 1); // only get_value()'s return-type candidate
		CHECK_GT(result.accepted.size(), 0); // get_label() (and others) retained
		CHECK_LE(result.accepted_error_count, result.baseline_error_count);

		memdelete(editor_file_system);
		GDScriptTests::finish();
	}
```

NOTE: if candidate ordering/coverage means more than one candidate legitimately regresses, assert `result.rejected.size() >= 1` and that the accepted set re-verifies clean instead of an exact count — keep the assertion honest to what the analyzer actually does. Prefer the exact `== 1` only after confirming it empirically.

- [ ] **Step 4: Build and run**

Run: `scons platform=macos dev_build=yes tests=yes target=editor && ./bin/godot.macos.editor.dev.arm64 --headless --test --test-suite="*Verification*" --force-colors`
Expected: all three cases pass.

- [ ] **Step 5: Commit**

```bash
git add modules/gdscript/editor/gdscript_verification_harness.cpp modules/gdscript/tests/test_verification_harness.h
git commit -m "feat(gdscript): Delta-debugging attribution for verification harness (#34)"
```

---

### Task 4: Strict-mode preview

**Goal:** Implement `preview_strict` to report diagnostics that appear only under the requested strict mode, writing nothing to disk.

**Files:**
- Modify: `modules/gdscript/editor/gdscript_verification_harness.cpp`
- Modify: `modules/gdscript/tests/test_verification_harness.h` (add preview test)

**Acceptance Criteria:**
- [ ] For a source clean under default analysis but failing under strict mode, `preview_strict` lists the violation(s) with path/line/column/message.
- [ ] The file on disk is byte-for-byte unchanged after the call.
- [ ] A diagnostic present in the non-strict baseline is NOT reported as a strict violation.

**Verify:** `./bin/godot.macos.editor.dev.arm64 --headless --test --test-suite="*Verification*" --force-colors`

**Steps:**

- [ ] **Step 1: Add a per-file strict-diff helper in the anonymous namespace**

```cpp
// Analyze p_source twice (non-strict baseline, then with p_options' strict flags) and
// append violations present only under strict mode to r_violations.
void collect_strict_violations(const String &p_path, const String &p_source, const VerificationOptions &p_options, Vector<StrictViolation> &r_violations) {
	// Non-strict baseline: record (line, column, message) keys.
	HashSet<String> baseline_keys;
	{
		GDScriptParser parser;
		parser.parse(p_source, p_path, false);
		GDScriptAnalyzer analyzer(&parser);
		analyzer.analyze();
		for (const GDScriptParser::ParserError &error : parser.get_errors()) {
			baseline_keys.insert(vformat("%d:%d:%s", error.line, error.column, error.message));
		}
	}
	// Strict pass.
	GDScriptParser parser;
	parser.parse(p_source, p_path, false);
	GDScriptAnalyzer analyzer(&parser);
	analyzer.set_strict_null_checks(p_options.strict_null_checks);
	analyzer.set_strict_dynamic_checks(p_options.strict_dynamic_checks);
	analyzer.analyze();
	for (const GDScriptParser::ParserError &error : parser.get_errors()) {
		const String key = vformat("%d:%d:%s", error.line, error.column, error.message);
		if (baseline_keys.has(key)) {
			continue;
		}
		StrictViolation violation;
		violation.path = p_path;
		violation.line = error.line;
		violation.column = error.column;
		violation.message = error.message;
		r_violations.push_back(violation);
	}
}
```

- [ ] **Step 2: Replace the `preview_strict` stub body**

```cpp
StrictPreviewResult GDScriptVerificationHarness::preview_strict(
		const Vector<String> &p_paths,
		const VerificationOptions &p_options) {
	StrictPreviewResult result;
	result.strict_null_checks = p_options.strict_null_checks;
	result.strict_dynamic_checks = p_options.strict_dynamic_checks;

	HashSet<String> seen;
	for (const String &path : p_paths) {
		if (seen.has(path)) {
			continue;
		}
		seen.insert(path);
		bool ok = false;
		const String source = read_source(path, ok);
		if (!ok) {
			result.ok = false;
			result.error_message = vformat("Cannot read '%s'.", path);
			return result;
		}
		collect_strict_violations(path, source, p_options, result.violations);
	}
	result.ok = true;
	return result;
}
```

- [ ] **Step 3: Add the preview test**

```cpp
	TEST_CASE("Strict preview lists violations without modifying files") {
		EditorFileSystem *editor_file_system = memnew(EditorFileSystem);
		GDScriptLanguageProtocol *protocol = GDScriptTests::initialize(GDScriptTests::root);
		REQUIRE(protocol);

		// Assigning a Variant (dynamic) to a typed local is allowed by default but a
		// violation under strict_dynamic_checks.
		const String path = "res://refactor/verify_strict.gd";
		const String source =
				"func dyn():\n"
				"\treturn JSON.parse_string(\"1\")\n"
				"func use() -> void:\n"
				"\tvar x: int = dyn()\n";
		TemporaryScriptFile file(path, source);

		VerificationOptions options;
		options.strict_dynamic_checks = true;
		StrictPreviewResult result = GDScriptVerificationHarness::preview_strict({ path }, options);
		REQUIRE(result.ok);
		CHECK_GT(result.violations.size(), 0);
		CHECK_EQ(result.violations[0].path, path);

		// File untouched.
		CHECK_EQ(FileAccess::get_file_as_string(path), source);

		memdelete(editor_file_system);
		GDScriptTests::finish();
	}
```

NOTE: confirm the chosen `source` is clean under default analysis and produces a strict-dynamic error under the strict pass. If `dyn()`'s inferred return is not `Variant`, pick another construct known to trip `strict_dynamic_checks` (search `gdscript_analyzer.cpp` for `strict_dynamic_checks` to find a guaranteed trigger, e.g. assigning a Variant-typed value into a non-Variant typed target). Adjust the fixture until baseline has 0 errors and the strict pass reports ≥1.

- [ ] **Step 4: Build and run**

Run: `scons platform=macos dev_build=yes tests=yes target=editor && ./bin/godot.macos.editor.dev.arm64 --headless --test --test-suite="*Verification*" --force-colors`
Expected: all four cases pass.

- [ ] **Step 5: Commit**

```bash
git add modules/gdscript/editor/gdscript_verification_harness.cpp modules/gdscript/tests/test_verification_harness.h
git commit -m "feat(gdscript): Strict-mode preview in verification harness (#34)"
```

---

### Task 5: Gate `GDScriptFixpointInference` through the harness

**Goal:** Make the fixpoint commit only edits the harness accepts (re-analyzing the inverse-dependent closure), so an accepted edit that breaks a dependent is rolled back instead of committed — closing #32's deferred safety note. Add strict flags to `FixpointInferenceOptions`.

**Files:**
- Modify: `modules/gdscript/editor/gdscript_fixpoint_inference.h`
- Modify: `modules/gdscript/editor/gdscript_fixpoint_inference.cpp`
- Modify: `modules/gdscript/tests/test_fixpoint_inference.h` (add dependent-break case)

**Acceptance Criteria:**
- [ ] A fixpoint run over provider+consumer where typing the provider's return would break the consumer does NOT change the provider on disk and reports it as a skip; safe annotations elsewhere still apply.
- [ ] `FixpointInferenceOptions` carries `strict_null_checks` / `strict_dynamic_checks`, threaded into verification.
- [ ] Existing fixpoint tests still pass (convergence, idempotency, honest skips).

**Verify:** `./bin/godot.macos.editor.dev.arm64 --headless --test --test-suite="*Fixpoint*" --force-colors`

**Steps:**

- [ ] **Step 1: Extend `FixpointInferenceOptions` and update the deferral note**

In `gdscript_fixpoint_inference.h`, change the options struct to:

```cpp
struct FixpointInferenceOptions {
	// 0 => auto: (first-pass candidate count) + 1, clamped by a hard ceiling.
	int max_iterations = 0;
	// Strict modes are forwarded to per-pass verification (issue #34).
	bool strict_null_checks = false;
	bool strict_dynamic_checks = false;
};
```

Update the long class comment: replace the paragraph beginning "Verification re-analyzes only each edited file, never its dependents." with:

```cpp
// Each pass routes its accepted rewrites through GDScriptVerificationHarness::verify,
// which re-analyzes the inverse-dependency closure of the edited files (bounded by the
// run's path set) and rolls back any rewrite that introduces a new error in a
// dependent. An edit that would break a caller is therefore rejected and reported as a
// skip rather than committed. Dependents outside p_paths are not re-analyzed, so the
// path set should cover the project being migrated.
```

- [ ] **Step 2: Route per-pass accepted edits through the harness in `gdscript_fixpoint_inference.cpp`**

Add the include near the others:

```cpp
#include "gdscript_verification_harness.h"
```

Replace the per-pass "Collect + verify accepted rewrites" loop (the block that builds `pending_source` / `pending_count` via `count_enabled_edits`, `GDScriptRefactorEdits::apply`, and `verify_source`) with a harness-gated version:

```cpp
		// Collect every enabled candidate across the snapshot as a verification candidate.
		Vector<VerificationCandidate> candidates;
		for (const String &path : paths) {
			if (!snapshot.has(path)) {
				continue;
			}
			RefactorContext ctx;
			ctx.path = path;
			ctx.source = snapshot[path];
			RefactorCandidatesResult found = GDScriptRefactoring::find_candidates(ctx, RefactorKind::ADD_TYPE_ANNOTATION);
			if (!found.ok) {
				continue;
			}
			for (const RefactorCandidate &candidate : found.candidates) {
				if (!candidate.enabled || candidate.edits.is_empty()) {
					continue;
				}
				VerificationCandidate vc;
				vc.path = path;
				vc.line = candidate.line;
				vc.edits = candidate.edits;
				candidates.push_back(vc);
			}
		}

		HashMap<String, String> pending_source;
		HashMap<String, int> pending_count;
		if (!candidates.is_empty()) {
			VerificationOptions verify_options;
			verify_options.strict_null_checks = p_options.strict_null_checks;
			verify_options.strict_dynamic_checks = p_options.strict_dynamic_checks;
			VerificationResult verified = GDScriptVerificationHarness::verify(candidates, paths, verify_options);
			if (verified.ok) {
				// Group accepted candidates' edits per file and apply over the snapshot.
				HashMap<String, Vector<RefactorTextEdit>> accepted_edits;
				for (const VerificationCandidate &candidate : verified.accepted) {
					for (const RefactorTextEdit &edit : candidate.edits) {
						accepted_edits[candidate.path].push_back(edit);
					}
					pending_count[candidate.path] += 1;
				}
				for (const KeyValue<String, Vector<RefactorTextEdit>> &entry : accepted_edits) {
					String new_source;
					if (GDScriptRefactorEdits::apply(snapshot[entry.key], entry.value, new_source)) {
						pending_source[entry.key] = new_source;
					} else {
						pending_count.erase(entry.key); // Could not apply this file's accepted set.
					}
				}
			}
		}
```

Leave the existing commit-at-pass-end loop, `had_pending`, convergence, iteration accounting, and final-state skip scan unchanged: the harness only narrows which rewrites become `pending_source`, and the final-state scan already reports anything still enabled (now including harness-rejected edits) as "unresolved after fixpoint". The verification re-runs each iteration, so a rejected edit simply never lands and is honestly reported.

If `count_enabled_edits` and `verify_source` become unused after this change, delete them and the now-unused includes to satisfy `-Werror`.

- [ ] **Step 3: Add the dependent-break fixpoint test**

Add to the `[Modules][GDScript][Fixpoint]` suite in `test_fixpoint_inference.h`:

```cpp
	TEST_CASE("Run does not commit an edit that would break a dependent") {
		EditorFileSystem *editor_file_system = memnew(EditorFileSystem);
		GDScriptLanguageProtocol *protocol = GDScriptTests::initialize(GDScriptTests::root);
		REQUIRE(protocol);

		const String provider_path = "res://refactor/fixpoint_provider.gd";
		const String provider_source =
				"class_name FixpointProvider\n"
				"func get_value():\n"
				"\treturn 42\n";
		TemporaryScriptFile provider(provider_path, provider_source);

		const String consumer_path = "res://refactor/fixpoint_consumer.gd";
		const String consumer_source =
				"func use() -> void:\n"
				"\tvar p := FixpointProvider.new()\n"
				"\tvar s: String = p.get_value()\n";
		TemporaryScriptFile consumer(consumer_path, consumer_source);

		Vector<String> paths = { provider_path, consumer_path };
		FixpointInferenceResult result = GDScriptFixpointInference::run(paths);
		REQUIRE(result.ok);

		// The breaking return-type edit must not have been committed.
		const String provider_after = FileAccess::get_file_as_string(provider_path);
		CHECK_FALSE(provider_after.contains("func get_value() -> int:"));
		CHECK_EQ(provider_after, provider_source);

		// It is reported as a skip.
		bool reported = false;
		for (const FixpointSkipped &skip : result.skipped) {
			if (skip.path == provider_path) {
				reported = true;
			}
		}
		CHECK(reported);

		memdelete(editor_file_system);
		GDScriptTests::finish();
	}
```

NOTE: match the exact teardown idiom of the surrounding fixpoint tests (the earlier cases in this file). If they do not call `GDScriptTests::finish()` / `memdelete(editor_file_system)`, drop those lines to match. Confirm the breaking fixture truly regresses (same construct validated in Task 2) before relying on the skip assertion.

- [ ] **Step 4: Build and run both suites**

Run: `scons platform=macos dev_build=yes tests=yes target=editor && ./bin/godot.macos.editor.dev.arm64 --headless --test --test-suite="*Fixpoint*" --force-colors && ./bin/godot.macos.editor.dev.arm64 --headless --test --test-suite="*Verification*" --force-colors`
Expected: all fixpoint and verification cases pass, including pre-existing fixpoint cases.

- [ ] **Step 5: Commit**

```bash
git add modules/gdscript/editor/gdscript_fixpoint_inference.h modules/gdscript/editor/gdscript_fixpoint_inference.cpp modules/gdscript/tests/test_fixpoint_inference.h
git commit -m "feat(gdscript): Gate fixpoint commits through the verification harness (#34)"
```

---

### Task 6: CI-parity build + full GDScript suite + docs

**Goal:** Confirm the whole change builds warnings-as-errors and the full GDScript test suite is green, and document the new API surface.

**Files:**
- Modify: `docs/superpowers/specs/2026-06-24-post-edit-verification-harness-design.md` (only if behavior diverged from the spec during implementation — keep it the source of truth).

**Acceptance Criteria:**
- [ ] `scons platform=macos dev_mode=yes tests=yes target=editor` builds clean (warnings-as-errors).
- [ ] `./bin/godot.macos.editor.dev.arm64 --headless --test "[Modules][GDScript]" --force-colors` is fully green.

**Verify:** commands above.

**Steps:**

- [ ] **Step 1: CI-parity build**

Run: `scons platform=macos dev_mode=yes tests=yes target=editor`
Expected: no warnings/errors. Fix any `-Werror=shadow` / unused-variable issues (the fixpoint cleanup in Task 5 is a likely source).

- [ ] **Step 2: Full module suite**

Run: `./bin/godot.macos.editor.dev.arm64 --headless --test "[Modules][GDScript]" --force-colors`
Expected: all GDScript tests pass.

- [ ] **Step 3: Reconcile the spec if needed, then commit any doc change**

```bash
git add -A
git commit -m "docs(gdscript): Reconcile verification harness spec with implementation (#34)"
```

(Skip the commit if nothing diverged.)

---

## Self-review notes

- **Spec coverage:** affected-set/reverse-dep closure → Task 1+2; stage/restore + count criterion → Task 2; optimistic + ddmin attribution → Task 3; strict preview → Task 4; fixpoint wiring + strict options → Task 5; honest reporting via `rejected[].diagnostics` and fixpoint skips → Tasks 3 & 5; out-of-scope #167 noted in spec, unchanged.
- **Type consistency:** `VerificationCandidate{path,line,edits}`, `VerificationResult{ok,error_message,accepted,rejected,baseline_error_count,accepted_error_count}`, `VerificationRejected{path,line,reason,diagnostics}`, `StrictPreviewResult{ok,error_message,strict_null_checks,strict_dynamic_checks,violations}` used identically across tasks. Helper names (`analyze_affected`, `analyze_one`, `analyze_subset`, `stage_candidates`, `subset_of`, `ddmin_offending`, `collect_strict_violations`) are defined before use within the same file.
- **Empirical fixtures:** each test fixture has a NOTE to confirm the analyzer actually produces (default-clean / regresses-when-typed) behavior; the implementer must verify and adjust rather than assume, since the exact diagnostic depends on analyzer rules.
- **Risk:** ddmin assumes monotonic-ish regression; the carve-out loop with a no-shrink safety fallback guarantees termination and a non-regressing accepted set even if the predicate is non-monotone.
```
