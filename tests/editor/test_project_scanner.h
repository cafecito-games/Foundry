/**************************************************************************/
/*  test_project_scanner.h                                                */
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

#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/os/os.h"
#include "core/templates/safe_refcount.h"

#include "editor/project_manager/known_project_store.h"
#include "editor/project_manager/project_scanner.h"

#include "tests/test_macros.h"
#include "tests/test_utils.h"

namespace TestProjectScanner {

static String make_scratch_dir(const String &p_name) {
	const String dir = TestUtils::get_temp_path(p_name + "_" + itos(OS::get_singleton()->get_ticks_usec()));
	DirAccess::make_dir_recursive_absolute(dir);
	return dir;
}

static void write_project(const String &p_dir) {
	DirAccess::make_dir_recursive_absolute(p_dir);
	Ref<FileAccess> f = FileAccess::open(p_dir.path_join("project.foundry"), FileAccess::WRITE);
	REQUIRE(f.is_valid());
	f->store_string("[application]\nconfig/name=\"Scanned\"\n");
	f->close();
}

TEST_CASE("[ProjectScanner][Editor] recursive discovery finds nested projects and skips hidden dirs") {
	const String scratch = make_scratch_dir("scan_tree");
	write_project(scratch.path_join("root"));
	write_project(scratch.path_join("nested/inner"));
	write_project(scratch.path_join("nested/inner/deeper"));
	// A hidden directory must not be descended into.
	write_project(scratch.path_join(".hidden/skipme"));
	// A plain directory with no project.foundry contributes nothing.
	DirAccess::make_dir_recursive_absolute(scratch.path_join("empty"));

	List<String> found;
	SafeFlag active;
	active.set();
	ProjectScanner::scan_folder_recursive(scratch, &found, active);

	Vector<String> found_paths;
	for (const String &path : found) {
		found_paths.push_back(path);
	}
	CHECK(found_paths.size() == 3);
	CHECK(found_paths.has(scratch.path_join("root")));
	CHECK(found_paths.has(scratch.path_join("nested/inner")));
	CHECK(found_paths.has(scratch.path_join("nested/inner/deeper")));
	CHECK_FALSE(found_paths.has(scratch.path_join(".hidden/skipme")));
}

TEST_CASE("[ProjectScanner][Editor] a cleared active flag aborts discovery immediately") {
	const String scratch = make_scratch_dir("scan_abort");
	write_project(scratch.path_join("root"));

	List<String> found;
	SafeFlag active; // Left clear: the scan should short-circuit and find nothing.
	ProjectScanner::scan_folder_recursive(scratch, &found, active);
	CHECK(found.is_empty());
}

TEST_CASE("[ProjectScanner][Editor] discovered projects added to the store dedup existing entries") {
	const String scratch = make_scratch_dir("scan_dedup");
	const String tree = scratch.path_join("tree");
	write_project(tree.path_join("alpha"));
	write_project(tree.path_join("beta"));

	KnownProjectStore store(scratch.path_join("known_projects.cfg"));
	// Pre-seed one of the projects so the scan must not create a duplicate for it.
	store.add_project(tree.path_join("alpha"));
	CHECK(store.get_project_count() == 1);

	List<String> found;
	SafeFlag active;
	active.set();
	ProjectScanner::scan_folder_recursive(tree, &found, active);
	REQUIRE(found.size() == 2);

	for (const String &path : found) {
		store.add_project(path);
	}

	// alpha was already present; only beta is new, so the count is two, not three.
	CHECK(store.get_project_count() == 2);
	CHECK(store.has_project(tree.path_join("alpha")));
	CHECK(store.has_project(tree.path_join("beta")));
}

} // namespace TestProjectScanner
