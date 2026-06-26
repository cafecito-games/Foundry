/**************************************************************************/
/*  script_refactor_vcs_guard.cpp                                         */
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

#include "script_refactor_vcs_guard.h"

#ifdef TOOLS_ENABLED

#include "core/config/project_settings.h"
#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/os/os.h"

namespace ScriptRefactorVCSGuard {

Result evaluate(const WorkingTreeState &p_state) {
	Result result;

	if (!p_state.has_git_metadata) {
		result.status = Status::UNVERSIONED;
		result.message = TTR("This project is not under version control. Applying the migration overwrites your scripts in place; undo is only available until you close the editor. Back up or initialize version control before continuing.");
		return result;
	}

	if (!p_state.git_available || p_state.git_status_exit_code != 0) {
		result.status = Status::UNKNOWN;
		result.message = TTR("Could not check the version-control status of this project. Make sure your working tree is committed or backed up before applying the migration.");
		return result;
	}

	// `git status --porcelain` prints one line per changed/untracked path and
	// nothing at all for a clean tree, so any non-whitespace output is dirty.
	if (!p_state.git_status_output.strip_edges().is_empty()) {
		result.status = Status::DIRTY;
		result.message = TTR("This project has uncommitted version-control changes. Applying the migration mixes its edits with your in-progress work and makes them hard to separate. Commit or stash your changes before continuing.");
		return result;
	}

	result.status = Status::SAFE;
	return result;
}

namespace {

bool has_git_metadata(const String &p_project_path) {
	const String git_path = p_project_path.path_join(".git");
	// A `.git` entry is a directory in a normal clone and a regular file in a
	// linked worktree or submodule, so accept either.
	return DirAccess::exists(git_path) || FileAccess::exists(git_path);
}

bool run_git_status(const String &p_project_path, int &r_exit_code, String &r_output) {
	List<String> arguments;
	arguments.push_back("-C");
	arguments.push_back(p_project_path);
	arguments.push_back("status");
	arguments.push_back("--porcelain");

	int exit_code = 0;
	String output;
	const Error err = OS::get_singleton()->execute("git", arguments, &output, &exit_code, true);
	if (err != OK) {
		return false;
	}

	r_exit_code = exit_code;
	r_output = output;
	return true;
}

} // namespace

Result inspect_project(const String &p_project_path) {
	String project_path = p_project_path;
	if (project_path.is_empty()) {
		project_path = ProjectSettings::get_singleton()->globalize_path("res://");
	}
	project_path = project_path.trim_suffix("/");

	WorkingTreeState state;
	state.has_git_metadata = has_git_metadata(project_path);

	if (state.has_git_metadata) {
		int exit_code = 0;
		String output;
		if (run_git_status(project_path, exit_code, output)) {
			state.git_available = true;
			state.git_status_exit_code = exit_code;
			state.git_status_output = output;
		} else {
			state.git_available = false;
		}
	}

	return evaluate(state);
}

} // namespace ScriptRefactorVCSGuard

#endif // TOOLS_ENABLED
