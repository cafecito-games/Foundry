/**************************************************************************/
/*  test_temporary_project_tree.h                                         */
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

#include "tests/test_macros.h"

#include "fs_temporary_project_tree.h"

#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/os/os.h"
#include "core/templates/list.h"

namespace FSTests {

// Every scenario below works on disposable sentinels inside the shared scratch tree. Nothing here
// may attempt to delete the checkout, the running executable, or any path a developer cares about:
// the executable guard is proven through a refusal path, never by trying the deletion.
struct ScratchSandbox {
	String path;

	explicit ScratchSandbox(const String &p_name) {
		path = TemporaryProjectTree::get_test_scratch_path(p_name);
		if (path.is_empty()) {
			return;
		}
		if (TemporaryProjectTree::remove_owned_path(path) != OK) {
			path = String();
			return;
		}
		Ref<DirAccess> dir = DirAccess::create(DirAccess::ACCESS_FILESYSTEM);
		if (dir.is_null() || dir->make_dir_recursive(path) != OK) {
			path = String();
		}
	}

	~ScratchSandbox() {
		if (path.is_empty()) {
			return;
		}
		TemporaryProjectTree::remove_owned_path(path);
	}

	bool is_valid() const {
		return !path.is_empty();
	}
};

static bool make_sentinel_file(const String &p_absolute_path, const String &p_contents) {
	Ref<DirAccess> dir = DirAccess::create(DirAccess::ACCESS_FILESYSTEM);
	if (dir.is_null() || dir->make_dir_recursive(p_absolute_path.get_base_dir()) != OK) {
		return false;
	}
	Ref<FileAccess> file = FileAccess::open(p_absolute_path, FileAccess::WRITE);
	if (file.is_null()) {
		return false;
	}
	file->store_string(p_contents);
	return true;
}

static bool directory_exists(const String &p_absolute_path) {
	Ref<DirAccess> dir = DirAccess::create(DirAccess::ACCESS_FILESYSTEM);
	if (dir.is_null()) {
		return false;
	}
	return dir->dir_exists(p_absolute_path);
}

TEST_SUITE("[Modules][FoundryScript][TemporaryProjectTree]") {
	TEST_CASE("TemporaryProjectTree creates writes and removes only its own child") {
		const String scratch_root = TemporaryProjectTree::get_test_scratch_root();
		REQUIRE_FALSE(scratch_root.is_empty());
		if (scratch_root.is_empty()) {
			return;
		}

		// A disposable neighbor inside the scratch root that must survive the tree's lifetime.
		const String neighbor = TemporaryProjectTree::get_test_scratch_path("temporary_project_tree_neighbor");
		REQUIRE_FALSE(neighbor.is_empty());
		if (neighbor.is_empty()) {
			return;
		}
		const String neighbor_sentinel = neighbor.path_join("keep.txt");
		REQUIRE(make_sentinel_file(neighbor_sentinel, "keep\n"));

		String owned_root;
		{
			TemporaryProjectTree tree("temporary_project_tree_lifecycle");
			REQUIRE(tree.is_valid());
			if (!tree.is_valid()) {
				TemporaryProjectTree::remove_owned_path(neighbor);
				return;
			}
			owned_root = tree.root;

			CHECK(TemporaryProjectTree::is_strict_descendant(scratch_root, owned_root));
			CHECK(directory_exists(owned_root));

			tree.write_file("nested/main.fs", "var value = 1\n");
			CHECK(FileAccess::exists(owned_root.path_join("nested/main.fs")));
		}

		// Destruction removes the owned child and nothing else.
		CHECK_FALSE(directory_exists(owned_root));
		CHECK(FileAccess::exists(neighbor_sentinel));
		CHECK(directory_exists(scratch_root));

		CHECK_EQ(TemporaryProjectTree::remove_owned_path(neighbor), OK);
		CHECK_FALSE(directory_exists(neighbor));
	}

	TEST_CASE("TemporaryProjectTree rejects a relative scratch root before any mutation") {
		const String scratch_root = TemporaryProjectTree::get_test_scratch_root();
		REQUIRE_FALSE(scratch_root.is_empty());
		if (scratch_root.is_empty()) {
			return;
		}

		ScratchSandbox sandbox("temporary_project_tree_relative_root");
		REQUIRE(sandbox.is_valid());
		if (!sandbox.is_valid()) {
			return;
		}
		const String sentinel = sandbox.path.path_join("outside_sentinel.txt");
		REQUIRE(make_sentinel_file(sentinel, "keep\n"));

		Ref<DirAccess> filesystem = DirAccess::create(DirAccess::ACCESS_FILESYSTEM);
		REQUIRE(filesystem.is_valid());
		if (filesystem.is_null()) {
			return;
		}
		const String working_directory = filesystem->get_current_dir();
		const String relative_root = "temporary_project_tree_relative_root_should_not_exist";

		String resolved_root = "unchanged";
		const Error relative_error = TemporaryProjectTree::resolve_scratch_root(relative_root, resolved_root);
		CHECK_EQ(relative_error, ERR_INVALID_PARAMETER);
		CHECK(resolved_root.is_empty());

		// Rejected before creation: neither the working directory nor the scratch root gained it.
		CHECK_FALSE(directory_exists(working_directory.path_join(relative_root)));
		CHECK_FALSE(directory_exists(scratch_root.path_join(relative_root)));

		// A scheme-qualified value is not a filesystem path either.
		String scheme_root = "unchanged";
		CHECK_EQ(TemporaryProjectTree::resolve_scratch_root("res://scratch", scheme_root), ERR_INVALID_PARAMETER);
		CHECK(scheme_root.is_empty());

		String empty_root = "unchanged";
		CHECK_EQ(TemporaryProjectTree::resolve_scratch_root(String(), empty_root), ERR_INVALID_PARAMETER);
		CHECK(empty_root.is_empty());

		// An absolute root does resolve, and resolves to an absolute canonical path.
		String absolute_root;
		CHECK_EQ(TemporaryProjectTree::resolve_scratch_root(sandbox.path, absolute_root), OK);
		CHECK_FALSE(absolute_root.is_empty());

		CHECK(FileAccess::exists(sentinel));
	}

	TEST_CASE("TemporaryProjectTree rejects escaping child names before any mutation") {
		const String scratch_root = TemporaryProjectTree::get_test_scratch_root();
		REQUIRE_FALSE(scratch_root.is_empty());
		if (scratch_root.is_empty()) {
			return;
		}

		const String escape_marker = "temporary_project_tree_escape_marker";
		Vector<String> rejected_names;
		rejected_names.push_back("..");
		rejected_names.push_back("../" + escape_marker);
		rejected_names.push_back("nested/../../" + escape_marker);
		rejected_names.push_back("/" + escape_marker);
		rejected_names.push_back("res://" + escape_marker);
		rejected_names.push_back("C:/" + escape_marker);
		rejected_names.push_back("//share/" + escape_marker);
		rejected_names.push_back(String());

		for (const String &name : rejected_names) {
			String resolved = "unchanged";
			const Error resolve_error = TemporaryProjectTree::resolve_owned_path(name, resolved);
			CHECK_MESSAGE(resolve_error != OK, vformat("Child name '%s' must be rejected", name));
			CHECK(resolved.is_empty());
			CHECK(TemporaryProjectTree::get_test_scratch_path(name).is_empty());
		}

		// Nothing was created anywhere the escaping names pointed at.
		CHECK_FALSE(directory_exists(scratch_root.get_base_dir().path_join(escape_marker)));
		CHECK_FALSE(directory_exists("/" + escape_marker));
		CHECK(directory_exists(scratch_root));

		// A plain relative name still resolves inside the scratch root.
		String accepted;
		CHECK_EQ(TemporaryProjectTree::resolve_owned_path("temporary_project_tree_accepted/nested", accepted), OK);
		CHECK(TemporaryProjectTree::is_strict_descendant(scratch_root, accepted));
	}

	TEST_CASE("TemporaryProjectTree resolves only existing canonical paths owned by test scratch") {
		TemporaryProjectTree tree(vformat("temporary_project_tree_existing_owned_%d", OS::get_singleton()->get_process_id()));
		REQUIRE(tree.is_valid());
		tree.write_file("nested/value.txt", "value\n");

		String resolved = "unchanged";
		CHECK_EQ(TemporaryProjectTree::resolve_existing_owned_path(tree.root, resolved), OK);
		CHECK_EQ(resolved, tree.root);

		resolved = "unchanged";
		CHECK_EQ(TemporaryProjectTree::resolve_existing_owned_path(
						 tree.root.path_join("nested/value.txt"), resolved),
				OK);
		CHECK_EQ(resolved, tree.root.path_join("nested/value.txt"));

		resolved = "unchanged";
		CHECK_EQ(TemporaryProjectTree::resolve_existing_owned_path(
						 tree.root.path_join("missing"), resolved),
				ERR_CANT_RESOLVE);
		CHECK(resolved.is_empty());
	}

	TEST_CASE("TemporaryProjectTree rejects canonical paths reached through outside symlinks") {
		TemporaryProjectTree tree(vformat("temporary_project_tree_existing_symlink_%d", OS::get_singleton()->get_process_id()));
		REQUIRE(tree.is_valid());
		const String executable_path = OS::get_singleton()->get_executable_path();
		REQUIRE(FileAccess::exists(executable_path));

		Ref<DirAccess> filesystem = DirAccess::create(DirAccess::ACCESS_FILESYSTEM);
		REQUIRE(filesystem.is_valid());
		const String outside_link = tree.root.path_join("outside_link");
		if (filesystem->create_link(executable_path.get_base_dir(), outside_link) != OK) {
			// Some filesystems forbid symlink creation; the behavior under test is unavailable here.
			return;
		}

		String resolved = "unchanged";
		CHECK_EQ(TemporaryProjectTree::resolve_existing_owned_path(outside_link, resolved), ERR_UNAUTHORIZED);
		CHECK(resolved.is_empty());

		resolved = "unchanged";
		CHECK_EQ(TemporaryProjectTree::resolve_existing_owned_path(
						 outside_link.path_join(executable_path.get_file()), resolved),
				ERR_UNAUTHORIZED);
		CHECK(resolved.is_empty());
	}

	TEST_CASE("TemporaryProjectTree rejects an existing lexical prefix sibling") {
		ScratchSandbox sandbox(vformat("temporary_project_tree_existing_prefix_%d", OS::get_singleton()->get_process_id()));
		REQUIRE(sandbox.is_valid());
		const String configured_root = sandbox.path.path_join("configured");
		const String prefix_sibling = sandbox.path.path_join("configured_sibling");
		Ref<DirAccess> filesystem = DirAccess::create(DirAccess::ACCESS_FILESYSTEM);
		REQUIRE(filesystem.is_valid());
		REQUIRE_EQ(filesystem->make_dir_recursive(configured_root), OK);
		REQUIRE_EQ(filesystem->make_dir_recursive(prefix_sibling), OK);

		const bool had_scratch_environment = OS::get_singleton()->has_environment("FOUNDRY_TEST_SCRATCH");
		const String previous_scratch_environment =
				OS::get_singleton()->get_environment("FOUNDRY_TEST_SCRATCH");
		OS::get_singleton()->set_environment("FOUNDRY_TEST_SCRATCH", configured_root);
		String resolved = "unchanged";
		const Error resolve_error =
				TemporaryProjectTree::resolve_existing_owned_path(prefix_sibling, resolved);
		if (had_scratch_environment) {
			OS::get_singleton()->set_environment("FOUNDRY_TEST_SCRATCH", previous_scratch_environment);
		} else {
			OS::get_singleton()->unset_environment("FOUNDRY_TEST_SCRATCH");
		}

		CHECK_EQ(resolve_error, ERR_UNAUTHORIZED);
		CHECK(resolved.is_empty());
	}

	TEST_CASE("TemporaryProjectTree refuses to delete the scratch root itself") {
		const String scratch_root = TemporaryProjectTree::get_test_scratch_root();
		REQUIRE_FALSE(scratch_root.is_empty());
		if (scratch_root.is_empty()) {
			return;
		}

		ScratchSandbox sandbox("temporary_project_tree_root_deletion");
		REQUIRE(sandbox.is_valid());
		if (!sandbox.is_valid()) {
			return;
		}
		const String sentinel = sandbox.path.path_join("keep.txt");
		REQUIRE(make_sentinel_file(sentinel, "keep\n"));

		ERR_PRINT_OFF;
		const Error root_error = TemporaryProjectTree::remove_owned_path(scratch_root);
		// A container is never a strict descendant of itself.
		const Error self_error = TemporaryProjectTree::remove_validated_descendant(sandbox.path, sandbox.path);
		ERR_PRINT_ON;

		CHECK_EQ(root_error, ERR_UNAUTHORIZED);
		CHECK_EQ(self_error, ERR_UNAUTHORIZED);
		CHECK(directory_exists(scratch_root));
		CHECK(directory_exists(sandbox.path));
		CHECK(FileAccess::exists(sentinel));
	}

	TEST_CASE("TemporaryProjectTree refuses a sibling that only shares a name prefix") {
		ScratchSandbox sandbox("temporary_project_tree_prefix_trap");
		REQUIRE(sandbox.is_valid());
		if (!sandbox.is_valid()) {
			return;
		}

		// `container` and `container_sibling` share a string prefix but not a path component, so a
		// raw `begins_with` containment check would happily delete the sibling.
		const String container = sandbox.path.path_join("a");
		const String sibling = sandbox.path.path_join("ab");
		const String sibling_sentinel = sibling.path_join("keep.txt");
		const String owned_child = container.path_join("child");
		const String owned_sentinel = owned_child.path_join("scratch.txt");
		REQUIRE(make_sentinel_file(sibling_sentinel, "keep\n"));
		REQUIRE(make_sentinel_file(owned_sentinel, "scratch\n"));

		CHECK_FALSE(TemporaryProjectTree::is_strict_descendant(container, sibling));
		CHECK(TemporaryProjectTree::is_strict_descendant(container, owned_child));

		ERR_PRINT_OFF;
		const Error sibling_error = TemporaryProjectTree::remove_validated_descendant(container, sibling);
		ERR_PRINT_ON;

		CHECK_EQ(sibling_error, ERR_UNAUTHORIZED);
		CHECK(FileAccess::exists(sibling_sentinel));

		// The genuinely owned child is still removable through the same operation.
		CHECK_EQ(TemporaryProjectTree::remove_validated_descendant(container, owned_child), OK);
		CHECK_FALSE(directory_exists(owned_child));
		CHECK(FileAccess::exists(sibling_sentinel));
	}

	TEST_CASE("TemporaryProjectTree removes symlinks as leaves and never traverses them") {
		ScratchSandbox sandbox("temporary_project_tree_symlink");
		REQUIRE(sandbox.is_valid());
		if (!sandbox.is_valid()) {
			return;
		}

		const String external_target = sandbox.path.path_join("external_target");
		const String external_sentinel = external_target.path_join("keep.txt");
		REQUIRE(make_sentinel_file(external_sentinel, "keep\n"));

		Ref<DirAccess> filesystem = DirAccess::create(DirAccess::ACCESS_FILESYSTEM);
		REQUIRE(filesystem.is_valid());
		if (filesystem.is_null()) {
			return;
		}

		// The cleanup root is itself a symlink pointing outside the tree being removed.
		const String owned_link = sandbox.path.path_join("owned_link");
		if (filesystem->create_link(external_target, owned_link) != OK) {
			// Some filesystems forbid symlink creation; the behavior under test is unavailable here.
			return;
		}

		CHECK_EQ(TemporaryProjectTree::remove_validated_descendant(sandbox.path, owned_link), OK);
		CHECK_FALSE(FileAccess::exists(owned_link));
		CHECK_FALSE(directory_exists(owned_link));
		CHECK(FileAccess::exists(external_sentinel));

		// A symlink nested inside a removed tree is also a leaf: its target survives the walk.
		const String owned_tree = sandbox.path.path_join("owned_tree");
		const String owned_file = owned_tree.path_join("data.txt");
		REQUIRE(make_sentinel_file(owned_file, "data\n"));
		if (filesystem->create_link(external_target, owned_tree.path_join("nested_link")) != OK) {
			return;
		}

		CHECK_EQ(TemporaryProjectTree::remove_validated_descendant(sandbox.path, owned_tree), OK);
		CHECK_FALSE(directory_exists(owned_tree));
		CHECK(FileAccess::exists(external_sentinel));
		CHECK(directory_exists(external_target));
	}

	TEST_CASE("TemporaryProjectTree refuses any path containing the running executable") {
		const String executable_path = OS::get_singleton()->get_executable_path();
		REQUIRE_FALSE(executable_path.is_empty());
		if (executable_path.is_empty()) {
			return;
		}
		// The executable is only ever inspected, never a deletion target.
		REQUIRE(FileAccess::exists(executable_path));
		if (!FileAccess::exists(executable_path)) {
			return;
		}

		const String executable_dir = executable_path.get_base_dir();
		const String scratch_root = TemporaryProjectTree::get_test_scratch_root();

		CHECK(TemporaryProjectTree::path_contains_running_executable(executable_path));
		CHECK(TemporaryProjectTree::path_contains_running_executable(executable_dir));
		CHECK_FALSE(TemporaryProjectTree::path_contains_running_executable(executable_path + "_sibling"));
		CHECK_FALSE(TemporaryProjectTree::path_contains_running_executable(executable_dir.path_join("unrelated")));
		CHECK_FALSE(TemporaryProjectTree::path_contains_running_executable(scratch_root));

		// The executable guard refuses independently of containment: this request would otherwise
		// pass the strict-descendant check against the executable directory's own parent.
		ERR_PRINT_OFF;
		const Error executable_dir_error = TemporaryProjectTree::remove_validated_descendant(executable_dir.get_base_dir(), executable_dir);
		const Error executable_error = TemporaryProjectTree::remove_validated_descendant(executable_dir, executable_path);
		const Error scratch_error = TemporaryProjectTree::remove_owned_path(executable_dir);
		ERR_PRINT_ON;

		CHECK_EQ(executable_dir_error, ERR_UNAUTHORIZED);
		CHECK_EQ(scratch_error, ERR_UNAUTHORIZED);
		// The executable file itself is a strict descendant of its directory, so only this guard
		// stands between the helper and the binary the suite is running from.
		CHECK_EQ(executable_error, ERR_UNAUTHORIZED);

		CHECK(FileAccess::exists(executable_path));
		CHECK(directory_exists(executable_dir));
	}

#ifdef UNIX_ENABLED
	TEST_CASE("TemporaryProjectTree confines a real export launched from another working directory") {
		const String executable_path = OS::get_singleton()->get_executable_path();
		if (executable_path.is_empty() || !FileAccess::exists(executable_path)) {
			return;
		}

		ScratchSandbox sandbox("temporary_project_tree_cwd_subprocess");
		REQUIRE(sandbox.is_valid());
		if (!sandbox.is_valid()) {
			return;
		}

		// The subprocess runs from a deliberately different working directory, with an absolute
		// scratch root of its own, so anything it writes outside that root is observable here.
		const String working_directory = sandbox.path.path_join("working_directory");
		const String working_sentinel = working_directory.path_join("sentinel.txt");
		const String child_scratch = sandbox.path.path_join("child_scratch");
		REQUIRE(make_sentinel_file(working_sentinel, "sentinel\n"));
		Ref<DirAccess> filesystem = DirAccess::create(DirAccess::ACCESS_FILESYSTEM);
		REQUIRE(filesystem.is_valid());
		if (filesystem.is_null()) {
			return;
		}
		REQUIRE_EQ(filesystem->make_dir_recursive(child_scratch), OK);

		const String command = vformat(
				"cd '%s' && FOUNDRY_TEST_SCRATCH='%s' exec '%s' --headless test run --case '%s'",
				working_directory, child_scratch, executable_path,
				"*Command-first export runs a real mangled pack*");
		List<String> arguments;
		arguments.push_back("-c");
		arguments.push_back(command);

		String output;
		int exit_code = 0;
		const Error execute_error = OS::get_singleton()->execute("/bin/sh", arguments, &output, &exit_code, true);
		REQUIRE_EQ(execute_error, OK);
		if (execute_error != OK) {
			return;
		}

		// A run can still exit non-zero while cleaning up, so the doctest summary is the verdict.
		CHECK_MESSAGE(output.contains("[doctest] Status: SUCCESS!"), output);

		// The real export mutated only its own scratch root.
		CHECK(FileAccess::exists(working_sentinel));
		Ref<DirAccess> working_dir = DirAccess::open(working_directory);
		REQUIRE(working_dir.is_valid());
		if (working_dir.is_valid()) {
			working_dir->set_include_hidden(true);
			Vector<String> entries;
			working_dir->list_dir_begin();
			for (String entry = working_dir->get_next(); !entry.is_empty(); entry = working_dir->get_next()) {
				if (entry == "." || entry == "..") {
					continue;
				}
				entries.push_back(entry);
			}
			working_dir->list_dir_end();
			CHECK_EQ(entries.size(), 1);
		}

		// The launched binary is still there.
		CHECK(FileAccess::exists(executable_path));
	}
#endif // UNIX_ENABLED
}

} // namespace FSTests
