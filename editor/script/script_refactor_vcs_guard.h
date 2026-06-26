/**************************************************************************/
/*  script_refactor_vcs_guard.h                                           */
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

#include "core/string/ustring.h"
#include "core/templates/vector.h"

// Safety guard for the migration wizard's atomic apply step.
//
// Before a batch refactor writes over a project, the wizard should encourage
// the user to commit or back up first so that the change can be reverted from
// version control even after the editor's single-step undo history is gone. The
// guard inspects the project's working tree and reports whether applying is
// safe, or why the user should pause first.
//
// The decision logic is deliberately separated from the side effects (running
// git, reading the filesystem) so it can be unit-tested headlessly across every
// branch without a real git checkout.
namespace ScriptRefactorVCSGuard {

enum class Status {
	// The working tree is tracked by git and has no uncommitted changes.
	SAFE,
	// The project directory is not under (detectable) version control.
	UNVERSIONED,
	// The working tree has uncommitted changes.
	DIRTY,
	// The working tree is otherwise clean, but one or more files the migration
	// would overwrite are git-ignored and therefore not recoverable from git.
	IGNORED_TARGETS,
	// The project is versioned but git could not be run to inspect it.
	UNKNOWN,
};

// Raw inputs gathered from the environment, passed to evaluate() so the
// decision is a pure function of observable facts.
struct WorkingTreeState {
	// Whether a git executable could be located and run at all.
	bool git_available = false;
	// Whether the project path is inside a git working tree (the result of
	// `git rev-parse --is-inside-work-tree`). This correctly recognizes a
	// project nested in a subdirectory of a repository, not just a repo root.
	bool inside_work_tree = false;
	// Exit code of `git status --porcelain` (only meaningful when git ran and
	// the project is inside a work tree).
	int git_status_exit_code = 0;
	// Output of `git status --porcelain` (only meaningful when git ran).
	String git_status_output;
};

struct Result {
	Status status = Status::SAFE;
	// Human-readable, translated explanation; empty when SAFE.
	String message;

	// Whether the wizard should block-by-default and ask the user to confirm
	// before overwriting the project. True for every non-SAFE status.
	bool should_warn() const {
		return status != Status::SAFE;
	}
};

// Pure decision function over the observed working-tree state.
Result evaluate(const WorkingTreeState &p_state);

// Inspects the project at p_project_path (an absolute OS path) by checking for
// git metadata and, when present, running `git status --porcelain`, then
// returns the guard result. Pass the empty string to inspect the current
// editor project (res://).
Result inspect_project(const String &p_project_path = String());

// Returns the subset of p_target_paths (absolute OS paths) that git considers
// ignored under p_project_path's repository. The tree-level status check
// deliberately ignores `.gitignore`d build artifacts to avoid false positives,
// but a git-ignored file that is itself a migration target would be overwritten
// with no version-control recovery; callers that know their concrete target set
// use this to detect that case.
//
// r_check_succeeded reports whether the check ran to a reliable conclusion. It is
// false when git could not be launched or returned an error (exit code > 1), in
// which case the returned list is empty but MUST NOT be read as "nothing ignored"
// — the caller should treat an unsuccessful check as indeterminate (UNKNOWN)
// rather than safe. It is true on a clean determination, whether or not any
// targets turned out to be ignored.
Vector<String> find_ignored_targets(const String &p_project_path, const Vector<String> &p_target_paths, bool &r_check_succeeded);

} // namespace ScriptRefactorVCSGuard

#endif // TOOLS_ENABLED
