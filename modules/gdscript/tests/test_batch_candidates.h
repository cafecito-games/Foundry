/**************************************************************************/
/*  test_batch_candidates.h                                               */
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

#include "core/io/file_access.h"

#include "test_refactor.h" // GDScriptTests::TemporaryScriptFile, make_context.

#ifndef GDSCRIPT_NO_LSP

#include "editor/file_system/editor_file_system.h"

namespace GDScriptTests {

// Locates the candidate anchored at p_line within one file's batch entry.
static const RefactorCandidate *candidate_at_line(const BatchFileCandidates &p_file, int p_line) {
	for (const RefactorCandidate &candidate : p_file.candidates) {
		if (candidate.line == p_line) {
			return &candidate;
		}
	}
	return nullptr;
}

// Locates the per-file entry for p_path in a batch result.
static const BatchFileCandidates *file_entry(const BatchCandidatesResult &p_result, const String &p_path) {
	for (const BatchFileCandidates &file : p_result.files) {
		if (file.path == p_path) {
			return &file;
		}
	}
	return nullptr;
}

TEST_SUITE("[Modules][GDScript][BatchCandidates]") {
	TEST_CASE("Collects enabled and disabled candidates across multiple files in one call") {
		// Initialize the GDScript test project so res:// resolves to the test
		// scripts directory and the analyzer can read dependencies from disk.
		EditorFileSystem *editor_file_system = memnew(EditorFileSystem);
		GDScriptLanguageProtocol *protocol = GDScriptTests::initialize(GDScriptTests::root);
		REQUIRE(protocol);

		const String path_a = "res://refactor/batch_a.gd";
		const String source_a =
				"var member_score = 1\n"
				"var member_typed: int = 2\n";
		TemporaryScriptFile file_a(path_a, source_a);

		const String path_b = "res://refactor/batch_b.gd";
		const String source_b =
				"static func value():\n"
				"\treturn 42\n";
		TemporaryScriptFile file_b(path_b, source_b);

		Vector<String> paths;
		paths.push_back(path_a);
		paths.push_back(path_b);

		BatchCandidatesResult result = GDScriptBatchCandidates::collect(paths);
		REQUIRE(result.ok);
		REQUIRE_EQ(result.files.size(), 2);

		const BatchFileCandidates *entry_a = file_entry(result, path_a);
		const BatchFileCandidates *entry_b = file_entry(result, path_b);
		REQUIRE(entry_a != nullptr);
		REQUIRE(entry_b != nullptr);
		CHECK(entry_a->ok);
		CHECK(entry_b->ok);

		// File A: member_score is inferable (enabled with an edit); member_typed already
		// has a type (disabled with a reason). Honest reporting keeps both.
		const RefactorCandidate *member_score = candidate_at_line(*entry_a, 0);
		REQUIRE(member_score != nullptr);
		CHECK(member_score->enabled);
		REQUIRE_FALSE(member_score->edits.is_empty());
		CHECK_EQ(member_score->edits[0].new_text, ": int = ");

		const RefactorCandidate *member_typed = candidate_at_line(*entry_a, 1);
		REQUIRE(member_typed != nullptr);
		CHECK_FALSE(member_typed->enabled);
		CHECK_FALSE(member_typed->disabled_reason.is_empty());

		// File B contributes at least the inferable return type.
		bool saw_enabled_in_b = false;
		for (const RefactorCandidate &candidate : entry_b->candidates) {
			if (candidate.enabled) {
				saw_enabled_in_b = true;
				CHECK_FALSE(candidate.edits.is_empty());
			}
		}
		CHECK(saw_enabled_in_b);

		// The batch totals are the per-file sums, counted once.
		int expected_total = 0;
		int expected_enabled = 0;
		for (const BatchFileCandidates &file : result.files) {
			expected_total += file.candidates.size();
			for (const RefactorCandidate &candidate : file.candidates) {
				if (candidate.enabled) {
					expected_enabled++;
				}
			}
		}
		CHECK_EQ(result.total_candidates, expected_total);
		CHECK_EQ(result.enabled_candidates, expected_enabled);
		CHECK(result.enabled_candidates >= 2);

		memdelete(protocol);
		memdelete(editor_file_system);
	}

	TEST_CASE("Collapses duplicate input paths to a single entry") {
		EditorFileSystem *editor_file_system = memnew(EditorFileSystem);
		GDScriptLanguageProtocol *protocol = GDScriptTests::initialize(GDScriptTests::root);
		REQUIRE(protocol);

		const String path = "res://refactor/batch_dedup.gd";
		const String source = "var member_score = 1\n";
		TemporaryScriptFile file(path, source);

		Vector<String> paths;
		paths.push_back(path);
		paths.push_back(path);

		BatchCandidatesResult result = GDScriptBatchCandidates::collect(paths);
		REQUIRE(result.ok);
		CHECK_EQ(result.files.size(), 1);

		memdelete(protocol);
		memdelete(editor_file_system);
	}

	TEST_CASE("Reports an unreadable file without aborting the batch") {
		EditorFileSystem *editor_file_system = memnew(EditorFileSystem);
		GDScriptLanguageProtocol *protocol = GDScriptTests::initialize(GDScriptTests::root);
		REQUIRE(protocol);

		const String good_path = "res://refactor/batch_good.gd";
		const String source = "var member_score = 1\n";
		TemporaryScriptFile good_file(good_path, source);

		const String missing_path = "res://refactor/batch_does_not_exist.gd";

		Vector<String> paths;
		paths.push_back(good_path);
		paths.push_back(missing_path);

		BatchCandidatesResult result = GDScriptBatchCandidates::collect(paths);
		// A single unreadable file is not fatal: the batch still succeeds overall.
		REQUIRE(result.ok);
		REQUIRE_EQ(result.files.size(), 2);

		const BatchFileCandidates *good_entry = file_entry(result, good_path);
		const BatchFileCandidates *missing_entry = file_entry(result, missing_path);
		REQUIRE(good_entry != nullptr);
		REQUIRE(missing_entry != nullptr);

		CHECK(good_entry->ok);
		CHECK_FALSE(good_entry->candidates.is_empty());

		CHECK_FALSE(missing_entry->ok);
		CHECK_FALSE(missing_entry->error_message.is_empty());
		CHECK(missing_entry->candidates.is_empty());

		// The unreadable file contributes nothing to the totals.
		CHECK_EQ(result.total_candidates, good_entry->candidates.size());

		memdelete(protocol);
		memdelete(editor_file_system);
	}

	TEST_CASE("Reports an unanalyzable file without aborting the batch") {
		EditorFileSystem *editor_file_system = memnew(EditorFileSystem);
		GDScriptLanguageProtocol *protocol = GDScriptTests::initialize(GDScriptTests::root);
		REQUIRE(protocol);

		const String good_path = "res://refactor/batch_analyzable.gd";
		const String good_source = "var member_score = 1\n";
		TemporaryScriptFile good_file(good_path, good_source);

		// Reads fine from disk but fails to parse, so find_candidates returns !ok. This is
		// the distinct branch from an unreadable file: the bytes are there, the analysis is not.
		const String broken_path = "res://refactor/batch_broken.gd";
		const String broken_source = "func :\n\tpass\n";
		TemporaryScriptFile broken_file(broken_path, broken_source);

		Vector<String> paths;
		paths.push_back(good_path);
		paths.push_back(broken_path);

		BatchCandidatesResult result = GDScriptBatchCandidates::collect(paths);
		// An unanalyzable file is not fatal: the batch still succeeds overall.
		REQUIRE(result.ok);
		REQUIRE_EQ(result.files.size(), 2);

		const BatchFileCandidates *good_entry = file_entry(result, good_path);
		const BatchFileCandidates *broken_entry = file_entry(result, broken_path);
		REQUIRE(good_entry != nullptr);
		REQUIRE(broken_entry != nullptr);

		CHECK(good_entry->ok);
		CHECK_FALSE(good_entry->candidates.is_empty());

		CHECK_FALSE(broken_entry->ok);
		CHECK_FALSE(broken_entry->error_message.is_empty());
		CHECK(broken_entry->candidates.is_empty());

		// The unanalyzable file contributes nothing to the totals.
		CHECK_EQ(result.total_candidates, good_entry->candidates.size());

		memdelete(protocol);
		memdelete(editor_file_system);
	}

	TEST_CASE("Rejects unsupported refactor kinds") {
		const String path = "res://refactor/batch_unsupported.gd";
		Vector<String> paths;
		paths.push_back(path);

		BatchCandidatesResult result = GDScriptBatchCandidates::collect(paths, RefactorKind::RENAME);
		CHECK_FALSE(result.ok);
		CHECK_EQ(result.error_message, "Headless candidate collection is not implemented for this refactor.");
		// A rejected kind does no per-file work, so nothing is read or reported.
		CHECK(result.files.is_empty());
	}

	TEST_CASE("Batch candidates match the per-file find_candidates path") {
		EditorFileSystem *editor_file_system = memnew(EditorFileSystem);
		GDScriptLanguageProtocol *protocol = GDScriptTests::initialize(GDScriptTests::root);
		REQUIRE(protocol);

		const String path = "res://refactor/batch_parity.gd";
		const String source =
				"var member_score = 1\n"
				"var member_typed: int = 2\n";
		TemporaryScriptFile file(path, source);

		Vector<String> paths;
		paths.push_back(path);
		BatchCandidatesResult batch = GDScriptBatchCandidates::collect(paths);
		REQUIRE(batch.ok);
		REQUIRE_EQ(batch.files.size(), 1);
		const BatchFileCandidates &entry = batch.files[0];
		REQUIRE(entry.ok);

		RefactorContext context;
		context.path = path;
		context.source = source;
		RefactorCandidatesResult per_file = GDScriptRefactoring::find_candidates(context, RefactorKind::ADD_TYPE_ANNOTATION);
		REQUIRE(per_file.ok);

		// The batch entry for a single file reproduces find_candidates exactly.
		REQUIRE_EQ(entry.candidates.size(), per_file.candidates.size());
		for (int i = 0; i < entry.candidates.size(); i++) {
			const RefactorCandidate &batch_candidate = entry.candidates[i];
			const RefactorCandidate &per_file_candidate = per_file.candidates[i];
			CHECK_EQ(batch_candidate.enabled, per_file_candidate.enabled);
			CHECK_EQ(batch_candidate.line, per_file_candidate.line);
			CHECK_EQ(batch_candidate.disabled_reason, per_file_candidate.disabled_reason);
			REQUIRE_EQ(batch_candidate.edits.size(), per_file_candidate.edits.size());
			for (int j = 0; j < batch_candidate.edits.size(); j++) {
				CHECK_EQ(batch_candidate.edits[j].new_text, per_file_candidate.edits[j].new_text);
			}
		}

		memdelete(protocol);
		memdelete(editor_file_system);
	}
}

} // namespace GDScriptTests

#endif // GDSCRIPT_NO_LSP

#endif // TOOLS_ENABLED
