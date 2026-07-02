/**************************************************************************/
/*  test_project_scan.h                                                   */
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

#include "../editor/fs_project_scan.h"
#include "fs_temporary_project_tree.h"

#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/os/os.h"

namespace FSTests {

static bool scan_contains(const ProjectScanResult &p_result, const String &p_relative, const String &p_root) {
	const String absolute = p_root.path_join(p_relative);
	for (const String &file : p_result.files) {
		if (file == absolute) {
			return true;
		}
	}
	return false;
}

static bool skipped_contains(const ProjectScanResult &p_result, const String &p_relative, const String &p_root) {
	const String absolute = p_root.path_join(p_relative);
	for (const String &dir : p_result.skipped_directories) {
		if (dir == absolute) {
			return true;
		}
	}
	return false;
}

// Lays down a representative project: ordinary scripts, a non-script file, an addons plugin, a
// `.fsignore`-marked vendor tree, a hidden directory, and a nested project.
static void build_sample_project(const TemporaryProjectTree &p_tree) {
	p_tree.write_file("main.fs", "var value = 1\n");
	p_tree.write_file("notes.txt", "not a script\n");
	p_tree.write_file("scene/player.fs", "var health = 100\n");
	p_tree.write_file("scene/ui/hud.fs", "var label = \"hi\"\n");
	p_tree.write_file("addons/plugin/tool.fs", "var plugin = true\n");
	p_tree.write_file("vendor/lib.fs", "var vendored = true\n");
	p_tree.write_file("vendor/.fsignore", "");
	p_tree.write_file(".hidden/secret.fs", "var secret = true\n");
	p_tree.write_file("nested/project.foundry", "[application]\n");
	p_tree.write_file("nested/sub.fs", "var nested = true\n");
}

TEST_SUITE("[Modules][FoundryScript][ProjectScan]") {
	TEST_CASE("Enumerates project scripts and prunes excluded directories by default") {
		TemporaryProjectTree tree("fs_project_scan_default");
		build_sample_project(tree);

		const ProjectScanResult result = FSProjectScan::scan(tree.root);
		REQUIRE(result.ok);

		// Ordinary scripts are migrated.
		CHECK(scan_contains(result, "main.fs", tree.root));
		CHECK(scan_contains(result, "scene/player.fs", tree.root));
		CHECK(scan_contains(result, "scene/ui/hud.fs", tree.root));

		// Non-scripts, addons, .fsignore'd, hidden, and nested projects are not.
		CHECK_FALSE(scan_contains(result, "notes.txt", tree.root));
		CHECK_FALSE(scan_contains(result, "addons/plugin/tool.fs", tree.root));
		CHECK_FALSE(scan_contains(result, "vendor/lib.fs", tree.root));
		CHECK_FALSE(scan_contains(result, ".hidden/secret.fs", tree.root));
		CHECK_FALSE(scan_contains(result, "nested/sub.fs", tree.root));

		CHECK_EQ(result.files.size(), 3);

		// Pruned directories are reported honestly (the hidden directory is silently skipped).
		CHECK(skipped_contains(result, "addons", tree.root));
		CHECK(skipped_contains(result, "vendor", tree.root));
		CHECK(skipped_contains(result, "nested", tree.root));
	}

	TEST_CASE("Produces a deterministic, lexicographically ordered work list") {
		TemporaryProjectTree tree("fs_project_scan_order");
		build_sample_project(tree);

		const ProjectScanResult first = FSProjectScan::scan(tree.root);
		const ProjectScanResult second = FSProjectScan::scan(tree.root);
		REQUIRE(first.ok);
		REQUIRE(second.ok);

		// Repeated scans of the same project yield the identical ordered list.
		REQUIRE_EQ(first.files.size(), second.files.size());
		for (int i = 0; i < first.files.size(); i++) {
			CHECK_EQ(first.files[i], second.files[i]);
		}

		// The order is the sorted order, independent of filesystem enumeration.
		for (int i = 1; i < first.files.size(); i++) {
			CHECK(first.files[i - 1] < first.files[i]);
		}
	}

	TEST_CASE("Opts into addons when requested") {
		TemporaryProjectTree tree("fs_project_scan_addons");
		build_sample_project(tree);

		ProjectScanOptions options;
		options.include_addons = true;
		const ProjectScanResult result = FSProjectScan::scan(tree.root, options);
		REQUIRE(result.ok);

		CHECK(scan_contains(result, "addons/plugin/tool.fs", tree.root));
		CHECK_FALSE(skipped_contains(result, "addons", tree.root));
		// The other exclusions still hold.
		CHECK_FALSE(scan_contains(result, "vendor/lib.fs", tree.root));
	}

	TEST_CASE("Opts into .fsignore'd directories when requested") {
		TemporaryProjectTree tree("fs_project_scan_gdignore");
		build_sample_project(tree);

		ProjectScanOptions options;
		options.respect_gdignore = false;
		const ProjectScanResult result = FSProjectScan::scan(tree.root, options);
		REQUIRE(result.ok);

		CHECK(scan_contains(result, "vendor/lib.fs", tree.root));
		CHECK_FALSE(skipped_contains(result, "vendor", tree.root));
		// addons remains excluded; the two opt-ins are independent.
		CHECK_FALSE(scan_contains(result, "addons/plugin/tool.fs", tree.root));
	}

	TEST_CASE("Reports a fatal error when the root cannot be opened") {
		const String missing_root = OS::get_singleton()->get_temp_path().path_join("fs_project_scan_missing");
		TemporaryProjectTree::remove_recursive(missing_root); // Ensure it does not exist.

		const ProjectScanResult result = FSProjectScan::scan(missing_root);
		CHECK_FALSE(result.ok);
		CHECK_FALSE(result.error_message.is_empty());
		CHECK(result.files.is_empty());
	}

	TEST_CASE("Returns an empty list for a project with no scripts") {
		TemporaryProjectTree tree("fs_project_scan_empty");
		tree.write_file("readme.md", "# docs\n");
		tree.write_file("data/values.json", "{}\n");

		const ProjectScanResult result = FSProjectScan::scan(tree.root);
		REQUIRE(result.ok);
		CHECK(result.files.is_empty());
	}

	TEST_CASE("Only the project-root addons folder is treated as third-party") {
		TemporaryProjectTree tree("fs_project_scan_nested_addons");
		tree.write_file("main.fs", "var value = 1\n");
		// Root-level addons holds third-party plugins and is excluded by default.
		tree.write_file("addons/plugin/tool.fs", "var plugin = true\n");
		// A first-party directory deeper in the tree that merely shares the name is kept.
		tree.write_file("gameplay/addons/mod.fs", "var mod = true\n");

		const ProjectScanResult result = FSProjectScan::scan(tree.root);
		REQUIRE(result.ok);

		CHECK(scan_contains(result, "main.fs", tree.root));
		CHECK(scan_contains(result, "gameplay/addons/mod.fs", tree.root));
		CHECK_FALSE(scan_contains(result, "addons/plugin/tool.fs", tree.root));

		CHECK(skipped_contains(result, "addons", tree.root));
		CHECK_FALSE(skipped_contains(result, "gameplay/addons", tree.root));
	}

	TEST_CASE("Does not follow directory symlinks") {
		TemporaryProjectTree tree("fs_project_scan_symlink");
		tree.write_file("main.fs", "var value = 1\n");
		tree.write_file("real/lib.fs", "var lib = true\n");

		// A symlink pointing back at the project root would make a naive walk recurse forever and
		// re-collect everything through the link; the scan must record it and never descend.
		Ref<DirAccess> dir = DirAccess::create(DirAccess::ACCESS_FILESYSTEM);
		const String link_path = tree.root.path_join("loop");
		if (dir->create_link(tree.root, link_path) != OK) {
			// Some filesystems forbid symlink creation; the behavior under test is unavailable here.
			return;
		}

		const ProjectScanResult result = FSProjectScan::scan(tree.root);
		REQUIRE(result.ok); // Terminates: the loop did not run forever.

		// Real scripts are found exactly once; nothing is reached through the link.
		CHECK(scan_contains(result, "main.fs", tree.root));
		CHECK(scan_contains(result, "real/lib.fs", tree.root));
		CHECK_EQ(result.files.size(), 2);
		CHECK(skipped_contains(result, "loop", tree.root));
	}

	TEST_CASE("Does not collect symlinked script files") {
		TemporaryProjectTree tree("fs_project_scan_file_symlink");
		tree.write_file("real/lib.fs", "var lib = true\n");

		// A symlinked `.fs` whose target lives elsewhere must not enter the work list: its real
		// location is migrated where it actually lives, and following the link could escape the root.
		Ref<DirAccess> dir = DirAccess::create(DirAccess::ACCESS_FILESYSTEM);
		const String link_path = tree.root.path_join("alias.fs");
		if (dir->create_link(tree.root.path_join("real/lib.fs"), link_path) != OK) {
			// Some filesystems forbid symlink creation; the behavior under test is unavailable here.
			return;
		}

		const ProjectScanResult result = FSProjectScan::scan(tree.root);
		REQUIRE(result.ok);

		// Only the real file is collected; the symlink is not.
		CHECK(scan_contains(result, "real/lib.fs", tree.root));
		CHECK_FALSE(scan_contains(result, "alias.fs", tree.root));
		CHECK_EQ(result.files.size(), 1);
	}
}

} // namespace FSTests

#endif // TOOLS_ENABLED
