/**************************************************************************/
/*  test_verification_harness.h                                           */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/
/* Copyright (c) 2014-present Godot Engine contributors (see AUTHORS.md). */
/* Copyright (c) 2007-2014 Juan Linietsky, Ariel Manzur.                  */
/*                                                                        */
/* Permission is hereby granted, free of charge, to any person obtaining  */
/* a copy of this software and associated documentation files (the        */
/* "Software"), to deal in the Software without restriction, including    */
/* without limitation the rights to use, copy, modify, merge, publish,    */
/* distribute, sublicense, and/or sell copies of the Software, and to     */
/* permit persons to whom the Software is furnished to do so, subject to  */
/* the following conditions:                                              */
/*                                                                        */
/* The above copyright notice and this permission notice shall be         */
/* included in all copies or substantial portions of the Software.        */
/*                                                                        */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,        */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF     */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. */
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY   */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,   */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE      */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                 */
/**************************************************************************/

#pragma once

#ifdef TOOLS_ENABLED

#include "tests/test_macros.h"

#include "../editor/gdscript_batch_candidates.h"
#include "../editor/gdscript_verification_harness.h"
#include "../gdscript_cache.h"

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

		memdelete(protocol);
		memdelete(editor_file_system);
		GDScriptTests::finish_language();
	}

	TEST_CASE("Cross-file verification runs purely in memory and leaves no residual override or disk state") {
		// A clean provider edit must be accepted, which is only possible if the consumer's
		// analysis resolved the provider through the in-memory override map (the provider's
		// edited source is never written to disk). After the run, no override may remain in
		// the cache and both files must be byte-identical to their original on-disk content.
		EditorFileSystem *editor_file_system = memnew(EditorFileSystem);
		GDScriptLanguageProtocol *protocol = GDScriptTests::initialize(GDScriptTests::root);
		REQUIRE(protocol);

		// provider.gd: untyped getter whose inferred return type is String.
		const String provider_path = "res://refactor/verify_inmem_provider.gd";
		const String provider_source =
				"func get_label():\n"
				"\treturn \"hi\"\n";
		TemporaryScriptFile provider(provider_path, provider_source);

		// consumer.gd: consumes the getter as a String, so typing the provider's getter
		// `-> String` keeps the consumer clean and the candidate must be accepted.
		const String consumer_path = "res://refactor/verify_inmem_consumer.gd";
		const String consumer_source =
				"const Provider = preload(\"res://refactor/verify_inmem_provider.gd\")\n"
				"func use() -> void:\n"
				"\tvar p: Provider = Provider.new()\n"
				"\tvar s: String = p.get_label()\n";
		TemporaryScriptFile consumer(consumer_path, consumer_source);

		Vector<VerificationCandidate> candidates = enabled_candidates_for(provider_path);
		REQUIRE_GT(candidates.size(), 0);

		Vector<String> universe = { provider_path, consumer_path };
		VerificationResult result = GDScriptVerificationHarness::verify(candidates, universe);
		REQUIRE(result.ok);
		CHECK_EQ(result.rejected.size(), 0);
		CHECK_EQ(result.accepted.size(), candidates.size());

		// No disk writes: both files are byte-identical to the originals.
		CHECK_EQ(FileAccess::get_file_as_string(provider_path), provider_source);
		CHECK_EQ(FileAccess::get_file_as_string(consumer_path), consumer_source);

		// No residual override state: rollback is a full clear of the map.
		CHECK_FALSE(GDScriptCache::has_source_override(provider_path));
		CHECK_FALSE(GDScriptCache::has_source_override(consumer_path));

		memdelete(protocol);
		memdelete(editor_file_system);
		GDScriptTests::finish_language();
	}

	TEST_CASE("A candidate that breaks a dependent is rejected with a diagnostic") {
		EditorFileSystem *editor_file_system = memnew(EditorFileSystem);
		GDScriptLanguageProtocol *protocol = GDScriptTests::initialize(GDScriptTests::root);
		REQUIRE(protocol);

		// provider.gd: untyped getter whose inferred return type is int.
		const String provider_path = "res://refactor/verify_provider.gd";
		const String provider_source =
				"func get_value():\n"
				"\treturn 42\n";
		TemporaryScriptFile provider(provider_path, provider_source);

		// consumer.gd: uses preload to depend on the provider and assigns the getter's
		// result to a String. Before get_value() is typed, the call returns Variant so
		// the assignment is allowed. Once typed -> int the assignment is a hard error,
		// so the provider candidate must be rejected.
		const String consumer_path = "res://refactor/verify_consumer.gd";
		const String consumer_source =
				"const Provider = preload(\"res://refactor/verify_provider.gd\")\n"
				"func use() -> void:\n"
				"\tvar p: Provider = Provider.new()\n"
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

		memdelete(protocol);
		memdelete(editor_file_system);
		GDScriptTests::finish_language();
	}

	TEST_CASE("Bisection drops only the offending candidate and keeps the rest") {
		EditorFileSystem *editor_file_system = memnew(EditorFileSystem);
		GDScriptLanguageProtocol *protocol = GDScriptTests::initialize(GDScriptTests::root);
		REQUIRE(protocol);

		// One provider with two getters: get_value() (consumed as String -> bad once
		// typed int) and get_label() (consumed correctly as String -> safe to type).
		const String provider_path = "res://refactor/verify_multi_provider.gd";
		const String provider_source =
				"func get_value():\n"
				"\treturn 42\n"
				"func get_label():\n"
				"\treturn \"hi\"\n";
		TemporaryScriptFile provider(provider_path, provider_source);

		const String consumer_path = "res://refactor/verify_multi_consumer.gd";
		const String consumer_source =
				"const Provider = preload(\"res://refactor/verify_multi_provider.gd\")\n"
				"func use() -> void:\n"
				"\tvar p: Provider = Provider.new()\n"
				"\tvar bad: String = p.get_value()\n"
				"\tvar ok: String = p.get_label()\n";
		TemporaryScriptFile consumer(consumer_path, consumer_source);

		Vector<VerificationCandidate> candidates = enabled_candidates_for(provider_path);
		REQUIRE_GE(candidates.size(), 2);

		Vector<String> universe = { provider_path, consumer_path };
		VerificationResult result = GDScriptVerificationHarness::verify(candidates, universe);
		REQUIRE(result.ok);

		// Declaration anchors of the two getters in provider_source (0-based):
		// get_value() is on line 0, get_label() on line 2.
		const int get_value_line = 0;
		const int get_label_line = 2;

		// Exactly one candidate is dropped, and it must be get_value()'s return-type
		// edit (the only one that regresses the consumer once typed int), not get_label().
		CHECK_EQ(result.rejected.size(), 1);
		CHECK_EQ(result.rejected[0].line, get_value_line);
		CHECK_GT(result.rejected[0].diagnostics.size(), 0);

		// get_label()'s candidate must be retained, and the accepted set re-verifies clean.
		bool kept_get_label = false;
		for (const VerificationCandidate &accepted : result.accepted) {
			if (accepted.line == get_label_line) {
				kept_get_label = true;
			}
		}
		CHECK(kept_get_label);
		CHECK_LE(result.accepted_error_count, result.baseline_error_count);

		memdelete(protocol);
		memdelete(editor_file_system);
		GDScriptTests::finish_language();
	}

	TEST_CASE("A batch above the bisection ceiling still drops only the offending candidates") {
		// Above the per-pass bisection ceiling the harness must not fall back to rejecting the
		// whole batch. It partitions the batch into ceiling-sized chunks and attributes each
		// chunk independently, so a single offender among many candidates is isolated and the
		// rest are retained, at a bounded per-chunk cost.
		EditorFileSystem *editor_file_system = memnew(EditorFileSystem);
		GDScriptLanguageProtocol *protocol = GDScriptTests::initialize(GDScriptTests::root);
		REQUIRE(protocol);

		// Provider with enough getters to exceed the ceiling (64). Each returns an int, so each
		// gets an Add Type Annotation candidate. get_value_0() is consumed as a String, so typing
		// it `-> int` regresses the consumer; every other getter is unconsumed and safe to type.
		const int getter_count = 70;
		String provider_source;
		for (int i = 0; i < getter_count; i++) {
			provider_source += vformat("func get_value_%d():\n\treturn 42\n", i);
		}
		const String provider_path = "res://refactor/verify_ceiling_provider.gd";
		TemporaryScriptFile provider(provider_path, provider_source);

		const String consumer_path = "res://refactor/verify_ceiling_consumer.gd";
		const String consumer_source =
				"const Provider = preload(\"res://refactor/verify_ceiling_provider.gd\")\n"
				"func use() -> void:\n"
				"\tvar p: Provider = Provider.new()\n"
				"\tvar bad: String = p.get_value_0()\n";
		TemporaryScriptFile consumer(consumer_path, consumer_source);

		Vector<VerificationCandidate> candidates = enabled_candidates_for(provider_path);
		REQUIRE_GT(candidates.size(), 64);

		Vector<String> universe = { provider_path, consumer_path };
		VerificationResult result = GDScriptVerificationHarness::verify(candidates, universe);
		REQUIRE(result.ok);

		// Exactly one candidate is dropped (get_value_0()'s return-type edit on line 0), and it
		// is attributed a diagnostic. The whole batch is NOT rejected as a unit.
		CHECK_EQ(result.rejected.size(), 1);
		CHECK_EQ(result.rejected[0].line, 0);
		CHECK_GT(result.rejected[0].diagnostics.size(), 0);
		CHECK_EQ(result.accepted.size(), candidates.size() - 1);
		CHECK_LE(result.accepted_error_count, result.baseline_error_count);

		// Files untouched on disk.
		CHECK_EQ(FileAccess::get_file_as_string(provider_path), provider_source);
		CHECK_EQ(FileAccess::get_file_as_string(consumer_path), consumer_source);

		memdelete(protocol);
		memdelete(editor_file_system);
		GDScriptTests::finish_language();
	}

	TEST_CASE("Dependent-break rejection is stable across repeated verify calls") {
		// Regression guard for inverse-dependency edge loss: after the first verify() call
		// invalidates caches, a second call must still discover the consumer as a dependent
		// of the provider and reject the breaking candidate.
		EditorFileSystem *editor_file_system = memnew(EditorFileSystem);
		GDScriptLanguageProtocol *protocol = GDScriptTests::initialize(GDScriptTests::root);
		REQUIRE(protocol);

		const String provider_path = "res://refactor/verify_repeat_provider.gd";
		const String provider_source =
				"func get_value():\n"
				"\treturn 42\n";
		TemporaryScriptFile provider(provider_path, provider_source);

		const String consumer_path = "res://refactor/verify_repeat_consumer.gd";
		const String consumer_source =
				"const Provider = preload(\"res://refactor/verify_repeat_provider.gd\")\n"
				"func use() -> void:\n"
				"\tvar p: Provider = Provider.new()\n"
				"\tvar s: String = p.get_value()\n";
		TemporaryScriptFile consumer(consumer_path, consumer_source);

		Vector<VerificationCandidate> candidates = enabled_candidates_for(provider_path);
		REQUIRE_GT(candidates.size(), 0);

		Vector<String> universe = { provider_path, consumer_path };

		// First call — establishes baseline behavior.
		VerificationResult first = GDScriptVerificationHarness::verify(candidates, universe);
		REQUIRE(first.ok);
		CHECK_GT(first.rejected.size(), 0);

		// Second call — must still detect the dependent via a freshly rebuilt dep graph.
		VerificationResult second = GDScriptVerificationHarness::verify(candidates, universe);
		REQUIRE(second.ok);
		CHECK_GT(second.rejected.size(), 0);
		CHECK_GT(second.rejected[0].diagnostics.size(), 0);

		// Files unchanged.
		CHECK_EQ(FileAccess::get_file_as_string(provider_path), provider_source);
		CHECK_EQ(FileAccess::get_file_as_string(consumer_path), consumer_source);

		memdelete(protocol);
		memdelete(editor_file_system);
		GDScriptTests::finish_language();
	}

	TEST_CASE("Dependent rejection survives a provider edited on disk between verify calls") {
		// The cross-call dependency-graph cache keys edges by source hash. When a provider's
		// on-disk content changes between two verify() calls, the cached edges for it (and its
		// dependents) must be reprimed, so the consumer is still discovered as a dependent and
		// the type-narrowing candidate is still rejected. A stale cache that reused the first
		// call's edges would silently miss the consumer and wrongly accept.
		EditorFileSystem *editor_file_system = memnew(EditorFileSystem);
		GDScriptLanguageProtocol *protocol = GDScriptTests::initialize(GDScriptTests::root);
		REQUIRE(protocol);

		const String provider_path = "res://refactor/verify_edited_provider.gd";
		// First version: getter returns an int, plus a second untyped getter.
		const String provider_v1 =
				"func get_value():\n"
				"\treturn 42\n"
				"func get_other():\n"
				"\treturn 7\n";
		TemporaryScriptFile provider(provider_path, provider_v1);

		const String consumer_path = "res://refactor/verify_edited_consumer.gd";
		const String consumer_source =
				"const Provider = preload(\"res://refactor/verify_edited_provider.gd\")\n"
				"func use() -> void:\n"
				"\tvar p: Provider = Provider.new()\n"
				"\tvar s: String = p.get_value()\n";
		TemporaryScriptFile consumer(consumer_path, consumer_source);

		Vector<String> universe = { provider_path, consumer_path };

		// First call primes the graph cache with the provider's v1 edges.
		Vector<VerificationCandidate> first_candidates = enabled_candidates_for(provider_path);
		REQUIRE_GT(first_candidates.size(), 0);
		VerificationResult first = GDScriptVerificationHarness::verify(first_candidates, universe);
		REQUIRE(first.ok);
		CHECK_GT(first.rejected.size(), 0);

		// Rewrite the provider on disk: reorder the getters so the candidate anchors differ
		// from the cached call, while the consumer still narrows get_value() to String.
		{
			Ref<FileAccess> file = FileAccess::open(provider_path, FileAccess::WRITE);
			REQUIRE(file.is_valid());
			file->store_string(
					"func get_other():\n"
					"\treturn 7\n"
					"func get_value():\n"
					"\treturn 42\n");
		}

		// Second call must reprime against the edited provider and still reject the candidate
		// that narrows get_value() to a String the consumer cannot accept.
		Vector<VerificationCandidate> second_candidates = enabled_candidates_for(provider_path);
		REQUIRE_GT(second_candidates.size(), 0);
		VerificationResult second = GDScriptVerificationHarness::verify(second_candidates, universe);
		REQUIRE(second.ok);
		CHECK_GT(second.rejected.size(), 0);
		CHECK_GT(second.rejected[0].diagnostics.size(), 0);

		memdelete(protocol);
		memdelete(editor_file_system);
		GDScriptTests::finish_language();
	}

	TEST_CASE("Dependent rejection survives a universe expanded between verify calls") {
		// The dependency-graph cache records a path's edges only relative to the universe it was
		// primed against. If a first verify() runs with a narrow universe that omits a consumer,
		// the provider is cached without the edge to that consumer. A second verify() that adds
		// the consumer to the universe must still discover it as a dependent and reject the
		// breaking candidate, rather than reusing the narrow-universe edges and wrongly accepting.
		EditorFileSystem *editor_file_system = memnew(EditorFileSystem);
		GDScriptLanguageProtocol *protocol = GDScriptTests::initialize(GDScriptTests::root);
		REQUIRE(protocol);

		const String provider_path = "res://refactor/verify_expand_provider.gd";
		const String provider_source =
				"func get_value():\n"
				"\treturn 42\n";
		TemporaryScriptFile provider(provider_path, provider_source);

		const String consumer_path = "res://refactor/verify_expand_consumer.gd";
		const String consumer_source =
				"const Provider = preload(\"res://refactor/verify_expand_provider.gd\")\n"
				"func use() -> void:\n"
				"\tvar p: Provider = Provider.new()\n"
				"\tvar s: String = p.get_value()\n";
		TemporaryScriptFile consumer(consumer_path, consumer_source);

		Vector<VerificationCandidate> candidates = enabled_candidates_for(provider_path);
		REQUIRE_GT(candidates.size(), 0);

		// First call: universe is the provider alone, so the consumer is not in scope and the
		// candidate is accepted (no dependent observes the narrowed return type).
		Vector<String> narrow_universe = { provider_path };
		VerificationResult narrow = GDScriptVerificationHarness::verify(candidates, narrow_universe);
		REQUIRE(narrow.ok);
		CHECK_EQ(narrow.rejected.size(), 0);

		// Second call: universe now includes the consumer. The cache must rebuild against the new
		// universe and detect the consumer as a dependent, rejecting the breaking candidate.
		Vector<String> wide_universe = { provider_path, consumer_path };
		VerificationResult wide = GDScriptVerificationHarness::verify(candidates, wide_universe);
		REQUIRE(wide.ok);
		CHECK_GT(wide.rejected.size(), 0);
		CHECK_GT(wide.rejected[0].diagnostics.size(), 0);

		memdelete(protocol);
		memdelete(editor_file_system);
		GDScriptTests::finish_language();
	}

	TEST_CASE("Dependent rejection holds when the touched provider is outside the universe") {
		// A candidate may touch a provider that is not itself listed in the universe, while an
		// in-universe consumer depends on it. The universe bounds which dependents are
		// re-analyzed, not which providers can be edited, so the consumer must still be
		// discovered and the breaking candidate rejected. A graph cache that only recorded edges
		// keyed on universe providers would miss the consumer here and wrongly accept.
		EditorFileSystem *editor_file_system = memnew(EditorFileSystem);
		GDScriptLanguageProtocol *protocol = GDScriptTests::initialize(GDScriptTests::root);
		REQUIRE(protocol);

		const String provider_path = "res://refactor/verify_outside_provider.gd";
		const String provider_source =
				"func get_value():\n"
				"\treturn 42\n";
		TemporaryScriptFile provider(provider_path, provider_source);

		const String consumer_path = "res://refactor/verify_outside_consumer.gd";
		const String consumer_source =
				"const Provider = preload(\"res://refactor/verify_outside_provider.gd\")\n"
				"func use() -> void:\n"
				"\tvar p: Provider = Provider.new()\n"
				"\tvar s: String = p.get_value()\n";
		TemporaryScriptFile consumer(consumer_path, consumer_source);

		Vector<VerificationCandidate> candidates = enabled_candidates_for(provider_path);
		REQUIRE_GT(candidates.size(), 0);

		// Universe lists only the consumer; the touched provider is outside it.
		Vector<String> universe = { consumer_path };
		VerificationResult result = GDScriptVerificationHarness::verify(candidates, universe);
		REQUIRE(result.ok);
		CHECK_GT(result.rejected.size(), 0);
		CHECK_GT(result.rejected[0].diagnostics.size(), 0);

		memdelete(protocol);
		memdelete(editor_file_system);
		GDScriptTests::finish_language();
	}

	TEST_CASE("A candidate with an out-of-range edit is rejected with 'could not be applied'") {
		EditorFileSystem *editor_file_system = memnew(EditorFileSystem);
		GDScriptLanguageProtocol *protocol = GDScriptTests::initialize(GDScriptTests::root);
		REQUIRE(protocol);

		const String path = "res://refactor/verify_bad_edit.gd";
		const String source =
				"func foo():\n"
				"\treturn 1\n";
		TemporaryScriptFile file(path, source);

		// Construct a candidate whose edit references a line far beyond the file.
		RefactorTextEdit bad_edit;
		bad_edit.start_line = 9999;
		bad_edit.start_column = 0;
		bad_edit.end_line = 9999;
		bad_edit.end_column = 0;
		bad_edit.new_text = "-> int";

		VerificationCandidate candidate;
		candidate.path = path;
		candidate.line = 0;
		candidate.edits.push_back(bad_edit);

		Vector<VerificationCandidate> candidates;
		candidates.push_back(candidate);

		VerificationResult result = GDScriptVerificationHarness::verify(candidates, { path });
		REQUIRE(result.ok);

		// The candidate must appear in rejected, not accepted.
		CHECK_EQ(result.accepted.size(), 0);
		REQUIRE_GT(result.rejected.size(), 0);
		bool found_reason = false;
		for (const VerificationRejected &rejected : result.rejected) {
			if (rejected.reason.contains("could not be applied")) {
				found_reason = true;
			}
		}
		CHECK(found_reason);

		// File untouched.
		CHECK_EQ(FileAccess::get_file_as_string(path), source);

		memdelete(protocol);
		memdelete(editor_file_system);
		GDScriptTests::finish_language();
	}

	TEST_CASE("A net-zero diagnostic swap is rejected, not accepted") {
		// A candidate that removes one baseline error but introduces a different one at a
		// new location must be rejected. A count-only oracle would incorrectly accept it
		// because the total error count is the same; the multiset-difference oracle must
		// detect the new diagnostic key and reject the candidate.
		//
		// We verify behavior via the harness using real files where the swap occurs.
		EditorFileSystem *editor_file_system = memnew(EditorFileSystem);
		GDScriptLanguageProtocol *protocol = GDScriptTests::initialize(GDScriptTests::root);
		REQUIRE(protocol);

		// Provider is typed `-> int` so the consumer's `var s: String = p.get_value()`
		// produces one baseline error (int assigned to String). The candidate replaces
		// `return 42` with `return "hello"`, removing the consumer error (provider now
		// returns String) but introducing a NEW error inside the provider (return "hello"
		// does not match declared -> int). Net count = 1 either way; only the multiset
		// oracle detects the moved diagnostic key and rejects the candidate.
		const String provider_path = "res://refactor/verify_swap_provider.gd";
		const String provider_source =
				"func get_value() -> int:\n"
				"\treturn 42\n";
		TemporaryScriptFile provider(provider_path, provider_source);

		const String consumer_path = "res://refactor/verify_swap_consumer.gd";
		const String consumer_source =
				"const Provider = preload(\"res://refactor/verify_swap_provider.gd\")\n"
				"func use() -> void:\n"
				"\tvar p: Provider = Provider.new()\n"
				"\tvar s: String = p.get_value()\n";
		TemporaryScriptFile consumer(consumer_path, consumer_source);

		// Replace `return 42` with `return "hello"` on line 1 (0-based, after the tab).
		RefactorTextEdit swap_edit;
		swap_edit.start_line = 1;
		swap_edit.start_column = 1;
		swap_edit.end_line = 1;
		swap_edit.end_column = 10;
		swap_edit.new_text = "return \"hello\"";

		VerificationCandidate swap_candidate;
		swap_candidate.path = provider_path;
		swap_candidate.line = 0;
		swap_candidate.edits.push_back(swap_edit);

		Vector<VerificationCandidate> candidates;
		candidates.push_back(swap_candidate);

		Vector<String> universe = { provider_path, consumer_path };
		VerificationResult result = GDScriptVerificationHarness::verify(candidates, universe);
		REQUIRE(result.ok);

		// The swap candidate introduces a new diagnostic key even though the total error
		// count does not increase; the multiset oracle must reject it.
		CHECK_EQ(result.accepted.size(), 0);
		CHECK_GT(result.rejected.size(), 0);

		// Files untouched.
		CHECK_EQ(FileAccess::get_file_as_string(provider_path), provider_source);
		CHECK_EQ(FileAccess::get_file_as_string(consumer_path), consumer_source);

		memdelete(protocol);
		memdelete(editor_file_system);
		GDScriptTests::finish_language();
	}

	TEST_CASE("Strict preview lists violations without modifying files") {
		EditorFileSystem *editor_file_system = memnew(EditorFileSystem);
		GDScriptLanguageProtocol *protocol = GDScriptTests::initialize(GDScriptTests::root);
		REQUIRE(protocol);

		// Assigning a Variant (from an untyped function) to a typed local is silently
		// allowed under default analysis but is a hard error under strict_dynamic_checks.
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
		// A Variant-boundary violation carries the category whose suggested fix is an
		// explicit cast.
		CHECK_EQ(result.violations[0].category, StrictViolationCategory::VARIANT_BOUNDARY);
		CHECK_EQ(StrictViolation::category_name(result.violations[0].category), "variant-boundary");

		// File untouched.
		CHECK_EQ(FileAccess::get_file_as_string(path), source);

		memdelete(protocol);
		memdelete(editor_file_system);
		GDScriptTests::finish_language();
	}

	TEST_CASE("Strict preview categorizes nullable violations") {
		EditorFileSystem *editor_file_system = memnew(EditorFileSystem);
		GDScriptLanguageProtocol *protocol = GDScriptTests::initialize(GDScriptTests::root);
		REQUIRE(protocol);

		// A function returning a nullable `int?` whose result lands in a non-nullable
		// `int` local is allowed by default analysis but is a strict_null_checks error.
		const String path = "res://refactor/verify_strict_null.gd";
		const String source =
				"func maybe() -> int?:\n"
				"\treturn null\n"
				"func use() -> void:\n"
				"\tvar x: int = maybe()\n";
		TemporaryScriptFile file(path, source);

		VerificationOptions options;
		options.strict_null_checks = true;
		StrictPreviewResult result = GDScriptVerificationHarness::preview_strict({ path }, options);
		REQUIRE(result.ok);
		CHECK_GT(result.violations.size(), 0);
		CHECK_EQ(result.violations[0].category, StrictViolationCategory::NULLABLE);
		CHECK_EQ(StrictViolation::category_name(result.violations[0].category), "nullable");

		// File untouched.
		CHECK_EQ(FileAccess::get_file_as_string(path), source);

		memdelete(protocol);
		memdelete(editor_file_system);
		GDScriptTests::finish_language();
	}
}

} // namespace GDScriptTests

#endif // GDSCRIPT_NO_LSP

#endif // TOOLS_ENABLED
