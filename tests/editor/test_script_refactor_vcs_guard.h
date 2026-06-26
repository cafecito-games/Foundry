/**************************************************************************/
/*  test_script_refactor_vcs_guard.h                                      */
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

#include "editor/script/script_refactor_vcs_guard.h"

#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/os/os.h"
#include "tests/test_macros.h"
#include "tests/test_utils.h"

namespace TestScriptRefactorVCSGuard {

using namespace ScriptRefactorVCSGuard;

TEST_CASE("[Editor][ScriptRefactorVCSGuard] Clean tracked tree is safe") {
	WorkingTreeState state;
	state.has_git_metadata = true;
	state.git_available = true;
	state.git_status_exit_code = 0;
	state.git_status_output = "";

	const Result result = evaluate(state);
	CHECK_EQ(result.status, Status::SAFE);
	CHECK_FALSE(result.should_warn());
	CHECK(result.message.is_empty());
}

TEST_CASE("[Editor][ScriptRefactorVCSGuard] Clean tree ignores trailing whitespace output") {
	WorkingTreeState state;
	state.has_git_metadata = true;
	state.git_available = true;
	state.git_status_exit_code = 0;
	state.git_status_output = "\n";

	const Result result = evaluate(state);
	CHECK_EQ(result.status, Status::SAFE);
	CHECK_FALSE(result.should_warn());
}

TEST_CASE("[Editor][ScriptRefactorVCSGuard] Unversioned project warns") {
	WorkingTreeState state;
	state.has_git_metadata = false;

	const Result result = evaluate(state);
	CHECK_EQ(result.status, Status::UNVERSIONED);
	CHECK(result.should_warn());
	CHECK_FALSE(result.message.is_empty());
}

TEST_CASE("[Editor][ScriptRefactorVCSGuard] Dirty working tree warns") {
	WorkingTreeState state;
	state.has_git_metadata = true;
	state.git_available = true;
	state.git_status_exit_code = 0;
	state.git_status_output = " M player.gd\n?? new_file.gd\n";

	const Result result = evaluate(state);
	CHECK_EQ(result.status, Status::DIRTY);
	CHECK(result.should_warn());
	CHECK_FALSE(result.message.is_empty());
}

TEST_CASE("[Editor][ScriptRefactorVCSGuard] Git metadata present but git unavailable is unknown") {
	WorkingTreeState state;
	state.has_git_metadata = true;
	state.git_available = false;

	const Result result = evaluate(state);
	CHECK_EQ(result.status, Status::UNKNOWN);
	CHECK(result.should_warn());
	CHECK_FALSE(result.message.is_empty());
}

TEST_CASE("[Editor][ScriptRefactorVCSGuard] Non-zero git exit code is unknown") {
	WorkingTreeState state;
	state.has_git_metadata = true;
	state.git_available = true;
	state.git_status_exit_code = 128;
	state.git_status_output = "fatal: not a git repository";

	const Result result = evaluate(state);
	CHECK_EQ(result.status, Status::UNKNOWN);
	CHECK(result.should_warn());
}

TEST_CASE("[Editor][ScriptRefactorVCSGuard] inspect_project flags a directory without git metadata") {
	const String dir = TestUtils::get_temp_path("vcs_guard_unversioned_" + itos(OS::get_singleton()->get_ticks_usec()));
	REQUIRE_EQ(DirAccess::make_dir_recursive_absolute(dir), OK);

	const Result result = inspect_project(dir);
	CHECK_EQ(result.status, Status::UNVERSIONED);
	CHECK(result.should_warn());

	DirAccess::remove_absolute(dir);
}

TEST_CASE("[Editor][ScriptRefactorVCSGuard] inspect_project detects a git worktree-style .git file") {
	const String dir = TestUtils::get_temp_path("vcs_guard_gitfile_" + itos(OS::get_singleton()->get_ticks_usec()));
	REQUIRE_EQ(DirAccess::make_dir_recursive_absolute(dir), OK);

	// A `.git` *file* (as used by linked worktrees/submodules) counts as
	// version-control metadata even though it is not a directory.
	const String git_file = dir.path_join(".git");
	{
		Error err = OK;
		Ref<FileAccess> file = FileAccess::open(git_file, FileAccess::WRITE, &err);
		REQUIRE_EQ(err, OK);
		REQUIRE(file.is_valid());
		CHECK(file->store_string("gitdir: /somewhere/else\n"));
		file->close();
	}

	// git is not actually run against a real repo here; without a working repo
	// `git status` fails, so the result is UNKNOWN rather than UNVERSIONED. The
	// key assertion is that metadata presence is detected (not UNVERSIONED).
	const Result result = inspect_project(dir);
	CHECK_NE(result.status, Status::UNVERSIONED);

	DirAccess::remove_absolute(git_file);
	DirAccess::remove_absolute(dir);
}

} // namespace TestScriptRefactorVCSGuard

#endif // TOOLS_ENABLED
