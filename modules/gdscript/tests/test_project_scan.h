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

#include "../editor/gdscript_project_scan.h"

#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/os/os.h"

namespace GDScriptTests {

// Builds a throwaway project tree on disk under the OS temp path and removes it on destruction,
// so the scan runs against real directories without touching the test project or res://.
struct TemporaryProjectTree {
	String root;

	explicit TemporaryProjectTree(const String &p_name) {
		root = OS::get_singleton()->get_temp_path().path_join(p_name);
		// Start from a clean slate in case a previous aborted run left the tree behind.
		remove_recursive(root);
		Ref<DirAccess> dir = DirAccess::create(DirAccess::ACCESS_FILESYSTEM);
		REQUIRE_EQ(dir->make_dir_recursive(root), OK);
	}

	~TemporaryProjectTree() {
		remove_recursive(root);
	}

	// Writes p_contents to root/p_relative_path, creating intermediate directories as needed.
	void write_file(const String &p_relative_path, const String &p_contents) const {
		const String absolute_path = root.path_join(p_relative_path);
		Ref<DirAccess> dir = DirAccess::create(DirAccess::ACCESS_FILESYSTEM);
		REQUIRE_EQ(dir->make_dir_recursive(absolute_path.get_base_dir()), OK);
		Ref<FileAccess> file = FileAccess::open(absolute_path, FileAccess::WRITE);
		REQUIRE_MESSAGE(file.is_valid(), vformat("Cannot write '%s'", absolute_path));
		file->store_string(p_contents);
	}

	static void remove_recursive(const String &p_path) {
		Ref<DirAccess> dir = DirAccess::open(p_path);
		if (dir.is_null()) {
			return;
		}
		dir->set_include_hidden(true);
		dir->list_dir_begin();
		for (String entry = dir->get_next(); !entry.is_empty(); entry = dir->get_next()) {
			if (entry == "." || entry == "..") {
				continue;
			}
			const String child = p_path.path_join(entry);
			if (dir->current_is_dir()) {
				remove_recursive(child);
			} else {
				DirAccess::remove_absolute(child);
			}
		}
		dir->list_dir_end();
		DirAccess::remove_absolute(p_path);
	}
};

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
// `.gdignore`-marked vendor tree, a hidden directory, and a nested project.
static void build_sample_project(const TemporaryProjectTree &p_tree) {
	p_tree.write_file("main.gd", "var value = 1\n");
	p_tree.write_file("notes.txt", "not a script\n");
	p_tree.write_file("scene/player.gd", "var health = 100\n");
	p_tree.write_file("scene/ui/hud.gd", "var label = \"hi\"\n");
	p_tree.write_file("addons/plugin/tool.gd", "var plugin = true\n");
	p_tree.write_file("vendor/lib.gd", "var vendored = true\n");
	p_tree.write_file("vendor/.gdignore", "");
	p_tree.write_file(".hidden/secret.gd", "var secret = true\n");
	p_tree.write_file("nested/project.godot", "[application]\n");
	p_tree.write_file("nested/sub.gd", "var nested = true\n");
}

TEST_SUITE("[Modules][GDScript][ProjectScan]") {
	TEST_CASE("Enumerates project scripts and prunes excluded directories by default") {
		TemporaryProjectTree tree("gdscript_project_scan_default");
		build_sample_project(tree);

		const ProjectScanResult result = GDScriptProjectScan::scan(tree.root);
		REQUIRE(result.ok);

		// Ordinary scripts are migrated.
		CHECK(scan_contains(result, "main.gd", tree.root));
		CHECK(scan_contains(result, "scene/player.gd", tree.root));
		CHECK(scan_contains(result, "scene/ui/hud.gd", tree.root));

		// Non-scripts, addons, .gdignore'd, hidden, and nested projects are not.
		CHECK_FALSE(scan_contains(result, "notes.txt", tree.root));
		CHECK_FALSE(scan_contains(result, "addons/plugin/tool.gd", tree.root));
		CHECK_FALSE(scan_contains(result, "vendor/lib.gd", tree.root));
		CHECK_FALSE(scan_contains(result, ".hidden/secret.gd", tree.root));
		CHECK_FALSE(scan_contains(result, "nested/sub.gd", tree.root));

		CHECK_EQ(result.files.size(), 3);

		// Pruned directories are reported honestly (the hidden directory is silently skipped).
		CHECK(skipped_contains(result, "addons", tree.root));
		CHECK(skipped_contains(result, "vendor", tree.root));
		CHECK(skipped_contains(result, "nested", tree.root));
	}

	TEST_CASE("Produces a deterministic, lexicographically ordered work list") {
		TemporaryProjectTree tree("gdscript_project_scan_order");
		build_sample_project(tree);

		const ProjectScanResult first = GDScriptProjectScan::scan(tree.root);
		const ProjectScanResult second = GDScriptProjectScan::scan(tree.root);
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
		TemporaryProjectTree tree("gdscript_project_scan_addons");
		build_sample_project(tree);

		ProjectScanOptions options;
		options.include_addons = true;
		const ProjectScanResult result = GDScriptProjectScan::scan(tree.root, options);
		REQUIRE(result.ok);

		CHECK(scan_contains(result, "addons/plugin/tool.gd", tree.root));
		CHECK_FALSE(skipped_contains(result, "addons", tree.root));
		// The other exclusions still hold.
		CHECK_FALSE(scan_contains(result, "vendor/lib.gd", tree.root));
	}

	TEST_CASE("Opts into .gdignore'd directories when requested") {
		TemporaryProjectTree tree("gdscript_project_scan_gdignore");
		build_sample_project(tree);

		ProjectScanOptions options;
		options.respect_gdignore = false;
		const ProjectScanResult result = GDScriptProjectScan::scan(tree.root, options);
		REQUIRE(result.ok);

		CHECK(scan_contains(result, "vendor/lib.gd", tree.root));
		CHECK_FALSE(skipped_contains(result, "vendor", tree.root));
		// addons remains excluded; the two opt-ins are independent.
		CHECK_FALSE(scan_contains(result, "addons/plugin/tool.gd", tree.root));
	}

	TEST_CASE("Reports a fatal error when the root cannot be opened") {
		const String missing_root = OS::get_singleton()->get_temp_path().path_join("gdscript_project_scan_missing");
		TemporaryProjectTree::remove_recursive(missing_root); // Ensure it does not exist.

		const ProjectScanResult result = GDScriptProjectScan::scan(missing_root);
		CHECK_FALSE(result.ok);
		CHECK_FALSE(result.error_message.is_empty());
		CHECK(result.files.is_empty());
	}

	TEST_CASE("Returns an empty list for a project with no scripts") {
		TemporaryProjectTree tree("gdscript_project_scan_empty");
		tree.write_file("readme.md", "# docs\n");
		tree.write_file("data/values.json", "{}\n");

		const ProjectScanResult result = GDScriptProjectScan::scan(tree.root);
		REQUIRE(result.ok);
		CHECK(result.files.is_empty());
	}
}

} // namespace GDScriptTests

#endif // TOOLS_ENABLED
