/**************************************************************************/
/*  fs_temporary_project_tree.h                                           */
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

#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/os/os.h"

namespace FSTests {

// Builds a throwaway project tree on disk under the shared test scratch path and removes it on
// destruction, so filesystem-walking code runs against real directories without touching the test
// project or res://.
struct TemporaryProjectTree {
	String root;

	explicit TemporaryProjectTree(const String &p_name) {
		root = get_test_scratch_path(p_name);
		// Start from a clean slate in case a previous aborted run left the tree behind.
		remove_recursive(root);
		Ref<DirAccess> dir = DirAccess::create(DirAccess::ACCESS_FILESYSTEM);
		CHECK_EQ(dir->make_dir_recursive(root), OK);
	}

	~TemporaryProjectTree() {
		remove_recursive(root);
	}

	// Writes p_contents to root/p_relative_path, creating intermediate directories as needed.
	void write_file(const String &p_relative_path, const String &p_contents) const {
		const String absolute_path = root.path_join(p_relative_path);
		Ref<DirAccess> dir = DirAccess::create(DirAccess::ACCESS_FILESYSTEM);
		CHECK_EQ(dir->make_dir_recursive(absolute_path.get_base_dir()), OK);
		Ref<FileAccess> file = FileAccess::open(absolute_path, FileAccess::WRITE);
		CHECK_MESSAGE(file.is_valid(), vformat("Cannot write '%s'", absolute_path));
		if (file.is_null()) {
			return;
		}
		file->store_string(p_contents);
	}

	static String get_test_scratch_root() {
		if (OS::get_singleton()->has_environment("FOUNDRY_TEST_SCRATCH")) {
			const String configured_root = OS::get_singleton()->get_environment("FOUNDRY_TEST_SCRATCH");
			if (!configured_root.is_empty()) {
				return configured_root.simplify_path();
			}
		}
		// Without an explicit `FOUNDRY_TEST_SCRATCH`, fall back to a directory scoped to this
		// process. A fixed shared path would let two `foundry` test processes running
		// concurrently on the same machine (e.g. separate worktrees during a multi-agent
		// session) race on the same staged project tree: one process's `remove_recursive` +
		// `copy_dir` in `stage_project_copy` can interleave with another's, corrupting the
		// staged files each currently-running test depends on.
		static const String scoped_root = [] {
			const String base = OS::get_singleton()->get_temp_path().simplify_path();
			// Sweep scratch directories left behind by processes that are no longer
			// running (crashed, killed, or otherwise never reached their own cleanup) so
			// direct, `FOUNDRY_TEST_SCRATCH`-less invocations don't accumulate one staged
			// project copy per past run.
			reap_dead_process_scratch_dirs(base);
			const String directory_name = vformat("foundry-tests-%d", OS::get_singleton()->get_process_id());
			return base.path_join(directory_name).simplify_path();
		}();
		return scoped_root;
	}

	static String get_test_scratch_path(const String &p_name) {
		return get_test_scratch_root().path_join(p_name).simplify_path();
	}

	static String stage_project_copy(const String &p_source_root, const String &p_name) {
		Error err = OK;
		Ref<DirAccess> source_dir = DirAccess::open(p_source_root, &err);
		CHECK_MESSAGE(err == OK, vformat("Cannot open source project root '%s'", p_source_root));
		if (source_dir.is_null()) {
			return String();
		}
		source_dir->set_include_hidden(true);

		const String source_root = source_dir->get_current_dir().simplify_path();
		const String staged_root = get_test_scratch_path(p_name);

		static String last_source_root;
		static String last_staged_root;
		if (last_source_root != source_root || last_staged_root != staged_root ||
				!FileAccess::exists(staged_root.path_join("project.foundry"))) {
			remove_recursive(staged_root);
			const Error copy_err = source_dir->copy_dir(source_root, staged_root);
			CHECK_MESSAGE(copy_err == OK, vformat("Cannot stage test project '%s' at '%s'", source_root, staged_root));
			if (copy_err != OK) {
				return String();
			}
			last_source_root = source_root;
			last_staged_root = staged_root;
		}

		return staged_root;
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
			// Remove a symlink as a leaf; never descend through it, or a link back into the tree
			// would make cleanup recurse forever (and could delete files outside the tree).
			if (dir->current_is_dir() && !dir->is_link(child)) {
				remove_recursive(child);
			} else {
				DirAccess::remove_absolute(child);
			}
		}
		dir->list_dir_end();
		DirAccess::remove_absolute(p_path);
	}

private:
	// Removes `foundry-tests-<pid>` directories under `p_base` whose owning process has
	// exited, so scratch directories from earlier direct test invocations are reclaimed
	// the next time any `foundry` test process starts.
	static void reap_dead_process_scratch_dirs(const String &p_base) {
		Ref<DirAccess> dir = DirAccess::open(p_base);
		if (dir.is_null()) {
			return;
		}
		dir->list_dir_begin();
		for (String entry = dir->get_next(); !entry.is_empty(); entry = dir->get_next()) {
			if (!dir->current_is_dir() || !entry.begins_with("foundry-tests-")) {
				continue;
			}
			const String pid_text = entry.trim_prefix("foundry-tests-");
			if (!pid_text.is_valid_int()) {
				continue;
			}
			const OS::ProcessID owning_pid = pid_text.to_int();
			if (owning_pid == OS::get_singleton()->get_process_id() || OS::get_singleton()->is_process_running(owning_pid)) {
				continue;
			}
			remove_recursive(p_base.path_join(entry));
		}
		dir->list_dir_end();
	}
};

} // namespace FSTests
