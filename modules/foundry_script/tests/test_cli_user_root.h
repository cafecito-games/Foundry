/**************************************************************************/
/*  test_cli_user_root.h                                                */
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

#include "fs_cli_user_root.h"
#include "fs_temporary_project_tree.h"

#include "core/io/dir_access.h"
#include "core/io/file_access.h"

#include "tests/test_macros.h"

namespace FSTests {

TEST_SUITE("[Modules][FoundryScript][CLIUserRoot]") {
	TEST_CASE("An artifact path is recognized as living inside the run's user-data root") {
		CHECK(artifact_is_inside_user_root("/scratch/user-benchmark-9/report.json", "/scratch/user-benchmark-9"));
		CHECK(artifact_is_inside_user_root("/scratch/user-fixtures-9/nested/report.json", "/scratch/user-fixtures-9/"));
		// A sibling whose name merely starts with the root's name is not contained.
		CHECK_FALSE(artifact_is_inside_user_root("/scratch/user-fixtures-91/report.json", "/scratch/user-fixtures-9"));
		CHECK_FALSE(artifact_is_inside_user_root("/scratch/report.json", "/scratch/user-fixtures-9"));
		CHECK_FALSE(artifact_is_inside_user_root("", "/scratch/user-fixtures-9"));
		CHECK_FALSE(artifact_is_inside_user_root("/scratch/user-fixtures-9/report.json", ""));
	}

	TEST_CASE("A finished run's user-data root is removed when it holds no artifact") {
		TemporaryProjectTree tree("cli_user_root_removed");
		REQUIRE_FALSE(tree.root.is_empty());
		tree.write_file("user-benchmark-1/foundry/app_userdata/project/leftover.txt", "leftover");
		const String user_root = tree.root.path_join("user-benchmark-1");
		REQUIRE(DirAccess::exists(user_root));

		Vector<String> artifacts;
		artifacts.push_back(tree.root.path_join("benchmark.json"));
		remove_per_process_user_root(user_root, artifacts);

		CHECK_FALSE(DirAccess::exists(user_root));
	}

	TEST_CASE("A user-data root that holds the requested artifact is kept") {
		TemporaryProjectTree tree("cli_user_root_kept");
		REQUIRE_FALSE(tree.root.is_empty());
		tree.write_file("user-benchmark-2/foundry/app_userdata/project/benchmark.json", "{}");
		const String user_root = tree.root.path_join("user-benchmark-2");
		const String artifact = user_root.path_join("foundry/app_userdata/project/benchmark.json");

		Vector<String> artifacts;
		artifacts.push_back(String());
		artifacts.push_back(artifact);
		remove_per_process_user_root(user_root, artifacts);

		CHECK(DirAccess::exists(user_root));
		CHECK(FileAccess::exists(artifact));
	}
}

} // namespace FSTests
