/**************************************************************************/
/*  test_annotation_index.h                                               */
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

#include "modules/gdscript/gdscript.h"

#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/os/os.h"

#include "tests/test_macros.h"

namespace GDScriptTests {

// These tests exercise the cross-file custom annotation index directly so the
// duplicate-identity and annotation-only-namespace lookups stay correct
// independently of the analyzer that consumes them.

TEST_CASE("[Modules][GDScript] Annotation index resolves namespaces and duplicates") {
	GDScriptLanguage *language = GDScriptLanguage::get_singleton();
	REQUIRE(language != nullptr);

	language->clear_global_annotations();

	language->add_global_annotation(SNAME("cafecito.test.suite"), "res://library_a.gd");
	language->add_global_annotation(SNAME("cafecito.test.fixture"), "res://library_a.gd");

	CHECK(language->is_global_annotation(SNAME("cafecito.test.suite")));
	CHECK_FALSE(language->is_global_annotation(SNAME("cafecito.test.missing")));

	SUBCASE("Annotation-only namespaces are discoverable") {
		CHECK(language->namespace_has_annotations("cafecito.test"));
		// Parent namespaces are considered to exist, matching global-class prefix semantics.
		CHECK(language->namespace_has_annotations("cafecito"));
		// A name that is not a namespace prefix must not match.
		CHECK_FALSE(language->namespace_has_annotations("cafecito.testing"));
		CHECK_FALSE(language->namespace_has_annotations("other"));
		CHECK_FALSE(language->namespace_has_annotations(String()));
	}

	SUBCASE("A canonical identity in a single file is not a duplicate") {
		// Re-registering the same canonical from the same path keeps it single.
		language->add_global_annotation(SNAME("cafecito.test.suite"), "res://library_a.gd");
		CHECK_FALSE(language->is_duplicated_global_annotation(SNAME("cafecito.test.suite")));
	}

	SUBCASE("A canonical identity in two files is a duplicate") {
		language->add_global_annotation(SNAME("cafecito.test.suite"), "res://library_b.gd");
		CHECK(language->is_duplicated_global_annotation(SNAME("cafecito.test.suite")));

		// Removing one declaring file collapses the duplicate again.
		language->remove_global_annotations_by_path("res://library_b.gd");
		CHECK_FALSE(language->is_duplicated_global_annotation(SNAME("cafecito.test.suite")));
		CHECK(language->is_global_annotation(SNAME("cafecito.test.suite")));
	}

	SUBCASE("Clearing drops every indexed annotation") {
		language->clear_global_annotations();
		CHECK_FALSE(language->is_global_annotation(SNAME("cafecito.test.suite")));
		CHECK_FALSE(language->namespace_has_annotations("cafecito.test"));
	}

	language->clear_global_annotations();
}

// These tests exercise `update_global_class_annotations`, the entry point the editor file-system
// scan and the LSP call to refresh the index from real files on disk. They write throwaway scripts
// under the OS temp path so the disk-extraction path runs against actual files.

TEST_CASE("[Modules][GDScript] Annotation index refreshes from disk") {
	GDScriptLanguage *language = GDScriptLanguage::get_singleton();
	REQUIRE(language != nullptr);

	language->clear_global_annotations();

	const String root = OS::get_singleton()->get_temp_path().path_join("gdscript_annotation_index_refresh");
	Ref<DirAccess> dir = DirAccess::create(DirAccess::ACCESS_FILESYSTEM);
	REQUIRE_EQ(dir->make_dir_recursive(root), OK);

	const String library_path = root.path_join("library.gd");
	const auto write_file = [](const String &p_path, const String &p_contents) {
		Ref<FileAccess> file = FileAccess::open(p_path, FileAccess::WRITE);
		REQUIRE(file.is_valid());
		file->store_string(p_contents);
	};

	// An annotation-only library that declares no `class_name`/`trait_name`.
	write_file(library_path,
			"namespace cafecito.test\n"
			"annotation suite targets CLASS\n"
			"annotation fixture targets VARIABLE\n");

	language->update_global_class_annotations(library_path, library_path);

	SUBCASE("Annotation-only files are indexed by the scan entry point") {
		CHECK(language->is_global_annotation(SNAME("cafecito.test.suite")));
		CHECK(language->is_global_annotation(SNAME("cafecito.test.fixture")));
		CHECK(language->namespace_has_annotations("cafecito.test"));
		CHECK_EQ(language->get_global_annotation_path(SNAME("cafecito.test.suite")), library_path);
	}

	SUBCASE("Re-scanning the same file does not create duplicates") {
		language->update_global_class_annotations(library_path, library_path);
		CHECK_FALSE(language->is_duplicated_global_annotation(SNAME("cafecito.test.suite")));
	}

	SUBCASE("Editing a file replaces its previous declarations") {
		write_file(library_path,
				"namespace cafecito.test\n"
				"annotation suite targets CLASS\n");
		language->update_global_class_annotations(library_path, library_path);

		CHECK(language->is_global_annotation(SNAME("cafecito.test.suite")));
		// The removed declaration is no longer indexed.
		CHECK_FALSE(language->is_global_annotation(SNAME("cafecito.test.fixture")));
	}

	SUBCASE("Removing a file drops its declarations from the index") {
		REQUIRE_EQ(dir->remove(library_path), OK);
		// Re-running against the now-missing path mirrors the editor's file-removal path.
		language->update_global_class_annotations(library_path, library_path);

		CHECK_FALSE(language->is_global_annotation(SNAME("cafecito.test.suite")));
		CHECK_FALSE(language->is_global_annotation(SNAME("cafecito.test.fixture")));
		CHECK_FALSE(language->namespace_has_annotations("cafecito.test"));
	}

	SUBCASE("Renaming a file moves its declarations to the new path") {
		const String renamed_path = root.path_join("renamed.gd");
		REQUIRE_EQ(dir->rename(library_path, renamed_path), OK);
		language->update_global_class_annotations(library_path, renamed_path);

		CHECK(language->is_global_annotation(SNAME("cafecito.test.suite")));
		// The identity is declared by exactly one path, not duplicated across old and new.
		CHECK_FALSE(language->is_duplicated_global_annotation(SNAME("cafecito.test.suite")));
		CHECK_EQ(language->get_global_annotation_path(SNAME("cafecito.test.suite")), renamed_path);

		REQUIRE_EQ(dir->remove(renamed_path), OK);
	}

	SUBCASE("A file that fails to parse is not indexed") {
		const String broken_path = root.path_join("broken.gd");
		write_file(broken_path,
				"namespace cafecito.broken\n"
				"annotation suite targets\n"); // Missing target list: a parse error.
		language->update_global_class_annotations(broken_path, broken_path);

		CHECK_FALSE(language->is_global_annotation(SNAME("cafecito.broken.suite")));
		CHECK_FALSE(language->namespace_has_annotations("cafecito.broken"));

		REQUIRE_EQ(dir->remove(broken_path), OK);
	}

	language->clear_global_annotations();
	dir->remove(library_path); // No-op if a subcase already removed it.
	dir->remove(root);
}

} // namespace GDScriptTests
