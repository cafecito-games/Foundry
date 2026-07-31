/**************************************************************************/
/*  test_declaration_index_generation.h                                   */
/**************************************************************************/
/*                         This file is part of:                          */
/*                              GODOT ENGINE                              */
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

#include "modules/foundry_script/foundry_script.h"

#include "tests/test_macros.h"

namespace FSTests {

// Reaches the private claim/commit seam of the cross-file declaration indexes so two refreshes of
// the same path can be interleaved deterministically. The race this guards against happens between
// the editor file-system scan (main thread) and the language server thread, and reproducing it with
// real threads would be timing-dependent; driving the seam directly pins the exact interleaving.
class TestFSDeclarationIndexAccessor {
public:
	static uint64_t claim(FSLanguage *p_language, const String &p_path) {
		return p_language->claim_declaration_index_refresh(p_path);
	}

	static bool commit(FSLanguage *p_language, const String &p_path, uint64_t p_token, const List<StringName> &p_annotations, bool p_declares_conformances, const String &p_conformance_namespace) {
		return p_language->commit_declaration_index_refresh(p_path, p_token, p_path, p_token, p_annotations, p_declares_conformances, p_conformance_namespace);
	}

	static bool commit_rename(FSLanguage *p_language, const String &p_search_path, uint64_t p_search_token, const String &p_target_path, uint64_t p_target_token, const List<StringName> &p_annotations, bool p_declares_conformances, const String &p_conformance_namespace) {
		return p_language->commit_declaration_index_refresh(p_search_path, p_search_token, p_target_path, p_target_token, p_annotations, p_declares_conformances, p_conformance_namespace);
	}
};

namespace {

List<StringName> declaration_index_annotations(const StringName &p_qualified_name) {
	List<StringName> annotations;
	annotations.push_back(p_qualified_name);
	return annotations;
}

// Restores the shared indexes so an interleaving test cannot leak state into the rest of the suite.
struct DeclarationIndexGenerationFixture {
	FSLanguage *language = FSLanguage::get_singleton();

	DeclarationIndexGenerationFixture() {
		reset();
	}

	~DeclarationIndexGenerationFixture() {
		reset();
	}

	void reset() {
		language->clear_global_annotations();
		language->clear_conformance_files();
	}
};

} // namespace

TEST_CASE("[Modules][FoundryScript][Conformance] a superseded declaration-index refresh does not overwrite a newer one") {
	// The editor scan and the language server both read the file from disk with no lock held, so the
	// parse that started first can finish last. Without ordering it would publish the older content
	// into both indexes — and the conformance side is persisted to the project cache, so the stale
	// win would survive into an export.
	DeclarationIndexGenerationFixture fixture;
	REQUIRE(fixture.language != nullptr);

	const String path = "res://fsg_library.fs";
	const uint64_t older_token = TestFSDeclarationIndexAccessor::claim(fixture.language, path);
	const uint64_t newer_token = TestFSDeclarationIndexAccessor::claim(fixture.language, path);

	CHECK(TestFSDeclarationIndexAccessor::commit(fixture.language, path, newer_token, declaration_index_annotations(SNAME("fsg.newer")), true, "fsg.newer"));
	CHECK_FALSE(TestFSDeclarationIndexAccessor::commit(fixture.language, path, older_token, declaration_index_annotations(SNAME("fsg.older")), true, "fsg.older"));

	CHECK(fixture.language->is_global_annotation(SNAME("fsg.newer")));
	CHECK_FALSE(fixture.language->is_global_annotation(SNAME("fsg.older")));
	CHECK(fixture.language->get_conformance_files_in_namespace("fsg.newer").has(path));
	CHECK(fixture.language->get_conformance_files_in_namespace("fsg.older").is_empty());
}

TEST_CASE("[Modules][FoundryScript][Conformance] declaration-index refreshes committed in claim order both land") {
	// Ordering must only reject a refresh that was actually superseded: sequential refreshes of the
	// same path — the common case, an edit followed by a rescan — all have to commit.
	DeclarationIndexGenerationFixture fixture;

	const String path = "res://fsg_sequential.fs";

	const uint64_t first_token = TestFSDeclarationIndexAccessor::claim(fixture.language, path);
	CHECK(TestFSDeclarationIndexAccessor::commit(fixture.language, path, first_token, declaration_index_annotations(SNAME("fsg.first")), true, "fsg.first"));

	const uint64_t second_token = TestFSDeclarationIndexAccessor::claim(fixture.language, path);
	CHECK(TestFSDeclarationIndexAccessor::commit(fixture.language, path, second_token, declaration_index_annotations(SNAME("fsg.second")), true, "fsg.second"));

	CHECK(fixture.language->is_global_annotation(SNAME("fsg.second")));
	CHECK_FALSE(fixture.language->is_global_annotation(SNAME("fsg.first")));
	CHECK(fixture.language->get_conformance_files_in_namespace("fsg.second").has(path));
	CHECK(fixture.language->get_conformance_files_in_namespace("fsg.first").is_empty());
}

TEST_CASE("[Modules][FoundryScript][Conformance] a rescan sweep drops an in-flight declaration-index refresh") {
	// A rescan of a root is the source of truth for it. A refresh that read the file before the sweep
	// must not commit afterwards and resurrect an entry the sweep just dropped.
	DeclarationIndexGenerationFixture fixture;

	const String root = "res://fsg_swept/";
	const String path = root + "library.fs";

	const uint64_t seeded_token = TestFSDeclarationIndexAccessor::claim(fixture.language, path);
	REQUIRE(TestFSDeclarationIndexAccessor::commit(fixture.language, path, seeded_token, declaration_index_annotations(SNAME("fsg.swept")), true, "fsg.swept"));

	const uint64_t in_flight_token = TestFSDeclarationIndexAccessor::claim(fixture.language, path);
	fixture.language->clear_global_declaration_index_under(root);

	CHECK_FALSE(TestFSDeclarationIndexAccessor::commit(fixture.language, path, in_flight_token, declaration_index_annotations(SNAME("fsg.swept")), true, "fsg.swept"));
	CHECK_FALSE(fixture.language->is_global_annotation(SNAME("fsg.swept")));
	CHECK(fixture.language->get_conformance_files_in_namespace("fsg.swept").is_empty());

	// A refresh claimed after the sweep is newer than it and commits normally.
	const uint64_t rescan_token = TestFSDeclarationIndexAccessor::claim(fixture.language, path);
	CHECK(TestFSDeclarationIndexAccessor::commit(fixture.language, path, rescan_token, declaration_index_annotations(SNAME("fsg.swept")), true, "fsg.swept"));
	CHECK(fixture.language->get_conformance_files_in_namespace("fsg.swept").has(path));
}

TEST_CASE("[Modules][FoundryScript][Conformance] a full declaration-index clear drops every in-flight refresh") {
	// A full clear cannot enumerate the paths it invalidates, so it raises a floor instead; every
	// token claimed before it is rejected.
	DeclarationIndexGenerationFixture fixture;

	const String path = "res://fsg_cleared.fs";
	const uint64_t in_flight_token = TestFSDeclarationIndexAccessor::claim(fixture.language, path);

	fixture.language->clear_conformance_files();
	fixture.language->clear_global_annotations();

	CHECK_FALSE(TestFSDeclarationIndexAccessor::commit(fixture.language, path, in_flight_token, declaration_index_annotations(SNAME("fsg.cleared")), true, "fsg.cleared"));
	CHECK_FALSE(fixture.language->is_global_annotation(SNAME("fsg.cleared")));
	CHECK(fixture.language->get_conformance_files_in_namespace("fsg.cleared").is_empty());

	const uint64_t later_token = TestFSDeclarationIndexAccessor::claim(fixture.language, path);
	CHECK(TestFSDeclarationIndexAccessor::commit(fixture.language, path, later_token, declaration_index_annotations(SNAME("fsg.cleared")), true, "fsg.cleared"));
}

TEST_CASE("[Modules][FoundryScript][Conformance] a rename keeps the side a newer refresh owns") {
	// A rename publishes a removal at the old path and an addition at the new one, each guarded by
	// that path's own token. When a newer refresh takes over the old path — the file was recreated
	// there — the rename must not erase what that refresh wrote, but its own reading of the new path
	// is still the newest one anybody has, and dropping it would leave the new path unindexed with no
	// other refresh in line to publish it.
	DeclarationIndexGenerationFixture fixture;

	const String old_path = "res://fsg_old.fs";
	const String new_path = "res://fsg_new.fs";

	const uint64_t seeded_token = TestFSDeclarationIndexAccessor::claim(fixture.language, old_path);
	REQUIRE(TestFSDeclarationIndexAccessor::commit(fixture.language, old_path, seeded_token, declaration_index_annotations(SNAME("fsg.moved")), true, "fsg.moved"));

	// The rename claims both paths before reading the file.
	const uint64_t rename_target_token = TestFSDeclarationIndexAccessor::claim(fixture.language, new_path);
	const uint64_t rename_search_token = TestFSDeclarationIndexAccessor::claim(fixture.language, old_path);

	// A newer refresh of the old path wins the race and owns the outcome for it.
	const uint64_t superseding_token = TestFSDeclarationIndexAccessor::claim(fixture.language, old_path);
	REQUIRE(TestFSDeclarationIndexAccessor::commit(fixture.language, old_path, superseding_token, declaration_index_annotations(SNAME("fsg.recreated")), true, "fsg.recreated"));

	CHECK(TestFSDeclarationIndexAccessor::commit_rename(fixture.language, old_path, rename_search_token, new_path, rename_target_token, declaration_index_annotations(SNAME("fsg.moved")), true, "fsg.moved"));

	// The superseding refresh's entries at the old path stand: the rename's removal was dropped.
	CHECK_EQ(fixture.language->get_global_annotation_path(SNAME("fsg.recreated")), old_path);
	CHECK(fixture.language->get_conformance_files_in_namespace("fsg.recreated").has(old_path));
	// The new path is still indexed by the rename, which is the only refresh that ever read it.
	CHECK_EQ(fixture.language->get_global_annotation_path(SNAME("fsg.moved")), new_path);
	CHECK(fixture.language->get_conformance_files_in_namespace("fsg.moved").has(new_path));
}

TEST_CASE("[Modules][FoundryScript][Conformance] a superseded rename target still clears the old path") {
	// Only the rename knows the file moved away from the old path, so its removal has to land even
	// when a newer refresh has taken over the new path. Dropping it would strand the old path's
	// entries with no refresh in line to remove them, and nothing rescans a path that no longer
	// exists.
	DeclarationIndexGenerationFixture fixture;

	const String old_path = "res://fsg_stranded_old.fs";
	const String new_path = "res://fsg_stranded_new.fs";

	const uint64_t seeded_token = TestFSDeclarationIndexAccessor::claim(fixture.language, old_path);
	REQUIRE(TestFSDeclarationIndexAccessor::commit(fixture.language, old_path, seeded_token, declaration_index_annotations(SNAME("fsg.stranded")), true, "fsg.stranded"));

	const uint64_t rename_target_token = TestFSDeclarationIndexAccessor::claim(fixture.language, new_path);
	const uint64_t rename_search_token = TestFSDeclarationIndexAccessor::claim(fixture.language, old_path);

	// A newer refresh of the new path wins the race and owns the outcome for it.
	const uint64_t superseding_token = TestFSDeclarationIndexAccessor::claim(fixture.language, new_path);
	REQUIRE(TestFSDeclarationIndexAccessor::commit(fixture.language, new_path, superseding_token, declaration_index_annotations(SNAME("fsg.landed")), true, "fsg.landed"));

	CHECK(TestFSDeclarationIndexAccessor::commit_rename(fixture.language, old_path, rename_search_token, new_path, rename_target_token, declaration_index_annotations(SNAME("fsg.stranded")), true, "fsg.stranded"));

	// The old path is gone from both indexes, and the superseding refresh's new-path entries stand.
	CHECK_FALSE(fixture.language->is_global_annotation(SNAME("fsg.stranded")));
	CHECK(fixture.language->get_conformance_files_in_namespace("fsg.stranded").is_empty());
	CHECK_EQ(fixture.language->get_global_annotation_path(SNAME("fsg.landed")), new_path);
	CHECK(fixture.language->get_conformance_files_in_namespace("fsg.landed").has(new_path));
}

TEST_CASE("[Modules][FoundryScript][Conformance] a fully superseded rename publishes nothing") {
	// Both sides taken over by newer refreshes: this one has nothing left to say.
	DeclarationIndexGenerationFixture fixture;

	const String old_path = "res://fsg_dropped_old.fs";
	const String new_path = "res://fsg_dropped_new.fs";

	const uint64_t rename_target_token = TestFSDeclarationIndexAccessor::claim(fixture.language, new_path);
	const uint64_t rename_search_token = TestFSDeclarationIndexAccessor::claim(fixture.language, old_path);

	const uint64_t old_token = TestFSDeclarationIndexAccessor::claim(fixture.language, old_path);
	REQUIRE(TestFSDeclarationIndexAccessor::commit(fixture.language, old_path, old_token, declaration_index_annotations(SNAME("fsg.kept_old")), true, "fsg.kept"));
	const uint64_t new_token = TestFSDeclarationIndexAccessor::claim(fixture.language, new_path);
	REQUIRE(TestFSDeclarationIndexAccessor::commit(fixture.language, new_path, new_token, declaration_index_annotations(SNAME("fsg.kept_new")), true, "fsg.kept"));

	CHECK_FALSE(TestFSDeclarationIndexAccessor::commit_rename(fixture.language, old_path, rename_search_token, new_path, rename_target_token, declaration_index_annotations(SNAME("fsg.dropped")), true, "fsg.dropped"));

	CHECK(fixture.language->is_global_annotation(SNAME("fsg.kept_old")));
	CHECK(fixture.language->is_global_annotation(SNAME("fsg.kept_new")));
	CHECK_FALSE(fixture.language->is_global_annotation(SNAME("fsg.dropped")));
	CHECK(fixture.language->get_conformance_files_in_namespace("fsg.dropped").is_empty());
	const Vector<String> kept = fixture.language->get_conformance_files_in_namespace("fsg.kept");
	CHECK(kept.has(old_path));
	CHECK(kept.has(new_path));
}

TEST_CASE("[Modules][FoundryScript][Conformance] a current rename commit moves both index entries") {
	// The uncontended rename still has to behave: old path emptied, new path indexed, as one commit.
	DeclarationIndexGenerationFixture fixture;

	const String old_path = "res://fsg_rename_old.fs";
	const String new_path = "res://fsg_rename_new.fs";

	const uint64_t seeded_token = TestFSDeclarationIndexAccessor::claim(fixture.language, old_path);
	REQUIRE(TestFSDeclarationIndexAccessor::commit(fixture.language, old_path, seeded_token, declaration_index_annotations(SNAME("fsg.rename")), true, "fsg.rename"));

	const uint64_t target_token = TestFSDeclarationIndexAccessor::claim(fixture.language, new_path);
	const uint64_t search_token = TestFSDeclarationIndexAccessor::claim(fixture.language, old_path);
	CHECK(TestFSDeclarationIndexAccessor::commit_rename(fixture.language, old_path, search_token, new_path, target_token, declaration_index_annotations(SNAME("fsg.rename")), true, "fsg.rename"));

	CHECK_EQ(fixture.language->get_global_annotation_path(SNAME("fsg.rename")), new_path);
	CHECK_FALSE(fixture.language->is_duplicated_global_annotation(SNAME("fsg.rename")));

	const Vector<String> conformance_files = fixture.language->get_conformance_files_in_namespace("fsg.rename");
	CHECK(conformance_files.has(new_path));
	CHECK_FALSE(conformance_files.has(old_path));
}

} // namespace FSTests
