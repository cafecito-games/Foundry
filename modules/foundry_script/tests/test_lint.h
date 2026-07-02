/**************************************************************************/
/*  test_lint.h                                                           */
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

#include "../fs_lint.h"

#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/os/os.h"

#include "tests/test_macros.h"

namespace FSTests {

struct TemporaryLintTree {
	String root;

	explicit TemporaryLintTree(const String &p_name) {
		root = OS::get_singleton()->get_temp_path().path_join(p_name + "_" + itos(OS::get_singleton()->get_ticks_usec()));
		remove_recursive(root);
		Ref<DirAccess> dir = DirAccess::create(DirAccess::ACCESS_FILESYSTEM);
		REQUIRE(dir.is_valid());
		REQUIRE_EQ(dir->make_dir_recursive(root), OK);
	}

	~TemporaryLintTree() {
		remove_recursive(root);
	}

	void write_file(const String &p_relative_path, const String &p_contents) const {
		const String absolute_path = root.path_join(p_relative_path);
		Ref<DirAccess> dir = DirAccess::create(DirAccess::ACCESS_FILESYSTEM);
		REQUIRE(dir.is_valid());
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
		if (dir->list_dir_begin() != OK) {
			return;
		}
		for (String entry = dir->get_next(); !entry.is_empty(); entry = dir->get_next()) {
			if (entry == "." || entry == "..") {
				continue;
			}
			const String child = p_path.path_join(entry);
			if (dir->current_is_dir() && !dir->is_link(entry)) {
				remove_recursive(child);
			} else {
				DirAccess::remove_absolute(child);
			}
		}
		dir->list_dir_end();
		DirAccess::remove_absolute(p_path);
	}
};

TEST_CASE("[Modules][FoundryScript][Lint] CLI option parsing uses CI defaults") {
	List<String> args;
	args.push_back("--headless");
	args.push_back("--foundry_script-lint");

	String error;
	FSLintCLI::Options options = FSLintCLI::parse_options(args, error);

	CHECK(error.is_empty());
	CHECK_EQ(options.output_format, FSLintCLI::OUTPUT_JSON);
	CHECK_EQ(options.fail_on, FSLintCLI::FAIL_ON_ERROR);
	CHECK(options.output_path.is_empty());
	CHECK(options.paths.is_empty());
}

TEST_CASE("[Modules][FoundryScript][Lint] CLI option parsing accepts SARIF output file and warning threshold") {
	List<String> args;
	args.push_back("--foundry_script-lint");
	args.push_back("--format=sarif");
	args.push_back("--out");
	args.push_back("lint.sarif");
	args.push_back("--fail-on=warning");
	args.push_back("res://scripts");

	String error;
	FSLintCLI::Options options = FSLintCLI::parse_options(args, error);

	CHECK(error.is_empty());
	CHECK_EQ(options.output_format, FSLintCLI::OUTPUT_SARIF);
	CHECK_EQ(options.fail_on, FSLintCLI::FAIL_ON_WARNING);
	CHECK_EQ(options.output_path, "lint.sarif");
	REQUIRE_EQ(options.paths.size(), 1);
	CHECK_EQ(options.paths[0], "res://scripts");
}

TEST_CASE("[Modules][FoundryScript][Lint] CLI option parsing rejects invalid choices") {
	List<String> bad_format;
	bad_format.push_back("--foundry_script-lint");
	bad_format.push_back("--format=xml");
	String format_error;
	FSLintCLI::parse_options(bad_format, format_error);
	CHECK(format_error.contains("Invalid --format value"));

	List<String> bad_fail_on;
	bad_fail_on.push_back("--foundry_script-lint");
	bad_fail_on.push_back("--fail-on=note");
	String fail_on_error;
	FSLintCLI::parse_options(bad_fail_on, fail_on_error);
	CHECK(fail_on_error.contains("Invalid --fail-on value"));

	List<String> missing_out;
	missing_out.push_back("--foundry_script-lint");
	missing_out.push_back("--out");
	String out_error;
	FSLintCLI::parse_options(missing_out, out_error);
	CHECK(out_error.contains("Missing file path after --out"));
}

TEST_CASE("[Modules][FoundryScript][Lint] File collection recurses deterministically") {
	TemporaryLintTree tree("fs_lint_collection");
	tree.write_file("z_root.fs", "var z = 1\n");
	tree.write_file("nested/b_mid.fs", "var b = 1\n");
	tree.write_file("a_root.fs", "var a = 1\n");
	tree.write_file("nested/ignore.txt", "not a script\n");
	tree.write_file("nested/wrong.fs.txt", "not a script\n");
	tree.write_file(".godot/generated/cache.fs", "var hidden = 1\n");

	Vector<String> paths;
	paths.push_back(tree.root);

	bool had_error = false;
	const Vector<String> files = FSLintCLI::collect_files(paths, had_error);

	CHECK_FALSE(had_error);
	REQUIRE_EQ(files.size(), 3);
	CHECK_EQ(files[0], tree.root.path_join("a_root.fs"));
	CHECK_EQ(files[1], tree.root.path_join("nested/b_mid.fs"));
	CHECK_EQ(files[2], tree.root.path_join("z_root.fs"));
}

TEST_CASE("[Modules][FoundryScript][Lint] File collection flags a missing path") {
	Vector<String> paths;
	paths.push_back(OS::get_singleton()->get_temp_path().path_join("fs_lint_missing_path_" + itos(OS::get_singleton()->get_ticks_usec())).path_join("none.fs"));

	bool had_error = false;
	ERR_PRINT_OFF;
	const Vector<String> files = FSLintCLI::collect_files(paths, had_error);
	ERR_PRINT_ON;

	CHECK(files.is_empty());
	CHECK_MESSAGE(had_error, "A missing path must mark the collection as failed.");
}

} // namespace FSTests
