/**************************************************************************/
/*  test_script_refactor_vcs_guard.h                                      */
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
	state.git_available = true;
	state.inside_work_tree = true;
	state.git_status_exit_code = 0;
	state.git_status_output = "";

	const Result result = evaluate(state);
	CHECK_EQ(result.status, Status::SAFE);
	CHECK_FALSE(result.should_warn());
	CHECK(result.message.is_empty());
}

TEST_CASE("[Editor][ScriptRefactorVCSGuard] Clean tree ignores trailing whitespace output") {
	WorkingTreeState state;
	state.git_available = true;
	state.inside_work_tree = true;
	state.git_status_exit_code = 0;
	state.git_status_output = "\n";

	const Result result = evaluate(state);
	CHECK_EQ(result.status, Status::SAFE);
	CHECK_FALSE(result.should_warn());
}

TEST_CASE("[Editor][ScriptRefactorVCSGuard] Unversioned project warns") {
	WorkingTreeState state;
	state.git_available = true;
	state.inside_work_tree = false;

	const Result result = evaluate(state);
	CHECK_EQ(result.status, Status::UNVERSIONED);
	CHECK(result.should_warn());
	CHECK_FALSE(result.message.is_empty());
}

TEST_CASE("[Editor][ScriptRefactorVCSGuard] Dirty working tree warns") {
	WorkingTreeState state;
	state.git_available = true;
	state.inside_work_tree = true;
	state.git_status_exit_code = 0;
	state.git_status_output = " M player.fs\n?? new_file.fs\n";

	const Result result = evaluate(state);
	CHECK_EQ(result.status, Status::DIRTY);
	CHECK(result.should_warn());
	CHECK_FALSE(result.message.is_empty());
}

TEST_CASE("[Editor][ScriptRefactorVCSGuard] Git unavailable is unknown") {
	WorkingTreeState state;
	state.git_available = false;

	const Result result = evaluate(state);
	CHECK_EQ(result.status, Status::UNKNOWN);
	CHECK(result.should_warn());
	CHECK_FALSE(result.message.is_empty());
}

TEST_CASE("[Editor][ScriptRefactorVCSGuard] Non-zero git status exit code is unknown") {
	WorkingTreeState state;
	state.git_available = true;
	state.inside_work_tree = true;
	state.git_status_exit_code = 128;
	state.git_status_output = "fatal: not a git repository";

	const Result result = evaluate(state);
	CHECK_EQ(result.status, Status::UNKNOWN);
	CHECK(result.should_warn());
}

// Whether a usable `git` binary is on PATH. inspect_project() integration tests
// only assert specific statuses when git can actually be run; otherwise every
// path collapses to UNKNOWN and the assertions would not be meaningful.
static bool git_is_available() {
	List<String> args;
	args.push_back("--version");
	int exit_code = -1;
	const Error err = OS::get_singleton()->execute("git", args, nullptr, &exit_code, true);
	return err == OK && exit_code == 0;
}

TEST_CASE("[Editor][ScriptRefactorVCSGuard] inspect_project flags a directory outside any repository") {
	if (!git_is_available()) {
		return;
	}
	// Use the system temp root, which is not a git repository, to avoid the test
	// runner's own checkout being picked up by rev-parse's upward walk.
	const String dir = OS::get_singleton()->get_cache_path().path_join("vcs_guard_unversioned_" + itos(OS::get_singleton()->get_ticks_usec()));
	REQUIRE_EQ(DirAccess::make_dir_recursive_absolute(dir), OK);

	const Result result = inspect_project(dir);
	CHECK_EQ(result.status, Status::UNVERSIONED);
	CHECK(result.should_warn());

	DirAccess::remove_absolute(dir);
}

TEST_CASE("[Editor][ScriptRefactorVCSGuard] inspect_project detects a real git working tree and its dirty state") {
	if (!git_is_available()) {
		return;
	}
	const String dir = OS::get_singleton()->get_cache_path().path_join("vcs_guard_repo_" + itos(OS::get_singleton()->get_ticks_usec()));
	REQUIRE_EQ(DirAccess::make_dir_recursive_absolute(dir), OK);

	auto run_git_in = [&dir](const Vector<String> &p_args) {
		List<String> args;
		args.push_back("-C");
		args.push_back(dir);
		for (const String &arg : p_args) {
			args.push_back(arg);
		}
		int exit_code = -1;
		const Error err = OS::get_singleton()->execute("git", args, nullptr, &exit_code, true);
		return err == OK && exit_code == 0;
	};

	REQUIRE(run_git_in({ "init" }));
	REQUIRE(run_git_in({ "config", "user.email", "test@example.com" }));
	REQUIRE(run_git_in({ "config", "user.name", "Test" }));

	// Empty new repo: nothing committed, nothing untracked -> clean working tree.
	{
		const Result result = inspect_project(dir);
		CHECK_EQ(result.status, Status::SAFE);
	}

	// Add an untracked file -> dirty.
	{
		const String path = dir.path_join("player.fs");
		Error err = OK;
		Ref<FileAccess> file = FileAccess::open(path, FileAccess::WRITE, &err);
		REQUIRE_EQ(err, OK);
		REQUIRE(file.is_valid());
		CHECK(file->store_string("var speed := 10\n"));
		file->close();

		const Result result = inspect_project(dir);
		CHECK_EQ(result.status, Status::DIRTY);
		CHECK(result.should_warn());
	}

	// A project in a *subdirectory* of the repo is still recognized as versioned
	// (regression guard for the monorepo/nested-project false negative).
	{
		const String sub = dir.path_join("game");
		REQUIRE_EQ(DirAccess::make_dir_recursive_absolute(sub), OK);
		const Result result = inspect_project(sub);
		CHECK_NE(result.status, Status::UNVERSIONED);
	}

	// Best-effort recursive cleanup of the throwaway repository.
	Ref<DirAccess> cleanup = DirAccess::open(dir);
	if (cleanup.is_valid()) {
		cleanup->erase_contents_recursive();
	}
	DirAccess::remove_absolute(dir);
}

TEST_CASE("[Editor][ScriptRefactorVCSGuard] inspect_project does not run shell code embedded in the path") {
	if (!git_is_available()) {
		return;
	}
	// A directory whose name contains shell command substitution. If git were
	// invoked through a shell, `$(...)` / backticks would execute and create the
	// sentinel file; running git directly with an argv vector passes the path
	// verbatim and the sentinel must never appear.
	const String cache = OS::get_singleton()->get_cache_path();
	const String sentinel = cache.path_join("vcs_guard_pwned_" + itos(OS::get_singleton()->get_ticks_usec()));
	const String dir_name = "vcs_guard_inj_" + itos(OS::get_singleton()->get_ticks_usec()) + "_$(touch '" + sentinel + "')`touch '" + sentinel + "'`";
	const String dir = cache.path_join(dir_name);
	REQUIRE_EQ(DirAccess::make_dir_recursive_absolute(dir), OK);

	// We do not care about the verdict here, only that nothing was executed.
	inspect_project(dir);

	Vector<String> targets;
	targets.push_back(dir.path_join("x.fs"));
	bool ok = true;
	find_ignored_targets(dir, targets, ok);

	CHECK_FALSE(FileAccess::exists(sentinel));

	if (FileAccess::exists(sentinel)) {
		DirAccess::remove_absolute(sentinel);
	}
	DirAccess::remove_absolute(dir);
}

TEST_CASE("[Editor][ScriptRefactorVCSGuard] find_ignored_targets reports only git-ignored targets") {
	if (!git_is_available()) {
		return;
	}
	const String dir = OS::get_singleton()->get_cache_path().path_join("vcs_guard_ignored_" + itos(OS::get_singleton()->get_ticks_usec()));
	REQUIRE_EQ(DirAccess::make_dir_recursive_absolute(dir), OK);

	auto run_git_in = [&dir](const Vector<String> &p_args) {
		List<String> args;
		args.push_back("-C");
		args.push_back(dir);
		for (const String &arg : p_args) {
			args.push_back(arg);
		}
		int exit_code = -1;
		const Error err = OS::get_singleton()->execute("git", args, nullptr, &exit_code, true);
		return err == OK && exit_code == 0;
	};

	auto write = [](const String &p_path, const String &p_contents) {
		Error err = OK;
		Ref<FileAccess> file = FileAccess::open(p_path, FileAccess::WRITE, &err);
		REQUIRE_EQ(err, OK);
		REQUIRE(file.is_valid());
		CHECK(file->store_string(p_contents));
		file->close();
	};

	REQUIRE(run_git_in({ "init" }));
	REQUIRE(run_git_in({ "config", "user.email", "test@example.com" }));
	REQUIRE(run_git_in({ "config", "user.name", "Test" }));

	// generated.fs is git-ignored; tracked.fs is not.
	write(dir.path_join(".gitignore"), "generated.fs\n");
	const String tracked = dir.path_join("tracked.fs");
	const String ignored = dir.path_join("generated.fs");
	write(tracked, "var a := 1\n");
	write(ignored, "var b := 2\n");

	Vector<String> targets;
	targets.push_back(tracked);
	targets.push_back(ignored);

	bool check_succeeded = false;
	const Vector<String> result = find_ignored_targets(dir, targets, check_succeeded);
	CHECK(check_succeeded);
	REQUIRE_EQ(result.size(), 1);
	CHECK(result[0].ends_with("generated.fs"));

	// No ignored targets -> empty result, but the check still succeeded.
	Vector<String> only_tracked;
	only_tracked.push_back(tracked);
	bool only_tracked_succeeded = false;
	CHECK(find_ignored_targets(dir, only_tracked, only_tracked_succeeded).is_empty());
	CHECK(only_tracked_succeeded);

	// A path outside any repository: git check-ignore errors (exit > 1), so the
	// check is reported as unsuccessful rather than a false "nothing ignored".
	const String outside_dir = OS::get_singleton()->get_cache_path().path_join("vcs_guard_outside_" + itos(OS::get_singleton()->get_ticks_usec()));
	REQUIRE_EQ(DirAccess::make_dir_recursive_absolute(outside_dir), OK);
	Vector<String> outside_targets;
	outside_targets.push_back(outside_dir.path_join("x.fs"));
	bool outside_succeeded = true;
	find_ignored_targets(outside_dir, outside_targets, outside_succeeded);
	CHECK_FALSE(outside_succeeded);
	DirAccess::remove_absolute(outside_dir);

	Ref<DirAccess> cleanup = DirAccess::open(dir);
	if (cleanup.is_valid()) {
		cleanup->erase_contents_recursive();
	}
	DirAccess::remove_absolute(dir);
}

} // namespace TestScriptRefactorVCSGuard

#endif // TOOLS_ENABLED
