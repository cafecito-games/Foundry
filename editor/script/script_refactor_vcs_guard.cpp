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
#include "core/io/file_access.h"
#include "core/os/os.h"

namespace ScriptRefactorVCSGuard {

Result evaluate(const WorkingTreeState &p_state) {
	Result result;

	if (!p_state.git_available) {
		result.status = Status::UNKNOWN;
		result.message = TTR("Could not check the version-control status of this project because git is not available. Make sure your working tree is committed or backed up before applying the migration.");
		return result;
	}

	if (!p_state.inside_work_tree) {
		result.status = Status::UNVERSIONED;
		result.message = TTR("This project is not under version control. Applying the migration overwrites your scripts in place; undo is only available until you close the editor. Back up or initialize version control before continuing.");
		return result;
	}

	if (p_state.git_status_exit_code != 0) {
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

// Reads a pipe FileAccess to EOF. Pipes are not seekable, so get_as_text() (which
// seeks to 0) yields nothing; drain it with blocking buffer reads instead.
String drain_pipe(const Ref<FileAccess> &p_pipe) {
	if (p_pipe.is_null()) {
		return String();
	}
	Vector<uint8_t> bytes;
	uint8_t chunk[4096];
	while (!p_pipe->eof_reached()) {
		const uint64_t read = p_pipe->get_buffer(chunk, sizeof(chunk));
		if (read == 0) {
			break;
		}
		const int64_t start = bytes.size();
		bytes.resize(start + (int64_t)read);
		memcpy(bytes.ptrw() + start, chunk, read);
	}
	String text;
	if (!bytes.is_empty()) {
		const Error err = text.append_utf8((const char *)bytes.ptr(), bytes.size());
		(void)err;
	}
	return text;
}

// Runs `git -C <project> <args...>`. Returns false if git could not be launched
// at all (not installed / not on PATH), in which case exit code and output are
// untouched. A successful launch with a non-zero exit code still returns true.
//
// This deliberately uses execute_with_pipe() rather than the simpler
// OS::execute(..., &output, ...) overload: the latter builds a single shell
// command string and runs it through popen() on Unix, so a project or target
// path containing shell metacharacters (quotes, backticks, $(...)) would be
// interpreted by the shell. execute_with_pipe() spawns git directly with an argv
// vector (execvp / CreateProcess), so paths are passed verbatim and cannot inject
// shell commands.
bool run_git(const String &p_project_path, const Vector<String> &p_args, int &r_exit_code, String &r_output) {
	List<String> arguments;
	arguments.push_back("-C");
	arguments.push_back(p_project_path);
	for (const String &arg : p_args) {
		arguments.push_back(arg);
	}

	Dictionary pipe_info = OS::get_singleton()->execute_with_pipe("git", arguments, true);
	if (pipe_info.is_empty()) {
		return false; // git could not be launched.
	}

	Ref<FileAccess> stdout_pipe = pipe_info["stdio"];
	Ref<FileAccess> stderr_pipe = pipe_info["stderr"];
	OS::ProcessID pid = pipe_info["pid"];

	// Blocking pipes: read stdout to EOF (the child closes it on exit). stderr is
	// drained too so a child that fills the stderr buffer cannot deadlock on write.
	const String output = drain_pipe(stdout_pipe);
	const String discarded_stderr = drain_pipe(stderr_pipe); // Drain so a chatty child cannot block on a full stderr buffer.
	(void)discarded_stderr;
	if (stdout_pipe.is_valid()) {
		stdout_pipe->close();
	}
	if (stderr_pipe.is_valid()) {
		stderr_pipe->close();
	}

	// stdout closes when git exits, but reaping the PID can briefly lag behind the
	// fd close, during which get_process_exit_code() returns -1. Poll a bounded
	// number of times so a transient "still running" never masquerades as an error
	// exit code that would wrongly mark the check indeterminate.
	int exit_code = OS::get_singleton()->get_process_exit_code(pid);
	for (int attempt = 0; exit_code < 0 && OS::get_singleton()->is_process_running(pid) && attempt < 1000; attempt++) {
		OS::get_singleton()->delay_usec(1000);
		exit_code = OS::get_singleton()->get_process_exit_code(pid);
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

	// `git rev-parse --is-inside-work-tree` walks up from the project directory,
	// so a project nested anywhere inside a repository is recognized as versioned
	// (not just a directory containing `.git`). It prints "true" and exits 0 from
	// inside a work tree, and exits non-zero when outside any repository.
	{
		Vector<String> args;
		args.push_back("rev-parse");
		args.push_back("--is-inside-work-tree");
		int exit_code = 0;
		String output;
		if (!run_git(project_path, args, exit_code, output)) {
			// git is not installed / not runnable; cannot determine anything.
			return evaluate(state);
		}
		state.git_available = true;
		state.inside_work_tree = (exit_code == 0) && (output.strip_edges() == "true");
	}

	if (state.inside_work_tree) {
		Vector<String> args;
		// `-c status.showUntrackedFiles=all` overrides a repo/user setting of `no`
		// or `normal` that would otherwise hide untracked (or untracked-in-subdir)
		// scripts the migration can still overwrite, and `--untracked-files=all`
		// enforces the same. Ignored paths are deliberately NOT surfaced here:
		// normal Godot checkouts ignore `.godot/`, `bin/`, generated headers, etc.,
		// and reporting those would mark every clean project dirty. Detecting a
		// git-ignored file that is also an actual migration target is the driver's
		// job (it knows the scanned set); see the follow-up tracked on epic #29.
		args.push_back("-c");
		args.push_back("status.showUntrackedFiles=all");
		args.push_back("status");
		args.push_back("--porcelain");
		args.push_back("--untracked-files=all");
		int exit_code = 0;
		String output;
		if (run_git(project_path, args, exit_code, output)) {
			state.git_status_exit_code = exit_code;
			state.git_status_output = output;
		} else {
			// rev-parse ran but status could not: treat as indeterminate.
			state.git_status_exit_code = -1;
		}
	}

	return evaluate(state);
}

Vector<String> find_ignored_targets(const String &p_project_path, const Vector<String> &p_target_paths, bool &r_check_succeeded) {
	Vector<String> ignored;
	r_check_succeeded = true;
	if (p_target_paths.is_empty()) {
		return ignored;
	}

	String project_path = p_project_path;
	if (project_path.is_empty()) {
		project_path = ProjectSettings::get_singleton()->globalize_path("res://");
	}
	project_path = project_path.trim_suffix("/");

	// `git check-ignore <paths...>` prints the matching paths, one per line, and
	// exits 0 if any matched, 1 if none, and >1 on error. The target list is
	// processed in bounded batches so a large project never exceeds the platform's
	// argv length limit; any batch that fails to run or errors marks the whole
	// check indeterminate rather than silently reporting "nothing ignored".
	const int batch_size = 256;
	for (int start = 0; start < p_target_paths.size(); start += batch_size) {
		const int end = MIN(start + batch_size, p_target_paths.size());

		Vector<String> args;
		args.push_back("check-ignore");
		for (int i = start; i < end; i++) {
			args.push_back(p_target_paths[i]);
		}

		int exit_code = 0;
		String output;
		if (!run_git(project_path, args, exit_code, output)) {
			r_check_succeeded = false; // git could not be launched.
			return Vector<String>();
		}
		// Exit code 0 (some matched) and 1 (none matched) are clean determinations.
		// >1 is an error (e.g. not a repo) and a negative code means the exit status
		// could not be read; neither may be read as "nothing ignored".
		if (exit_code < 0 || exit_code > 1) {
			r_check_succeeded = false;
			return Vector<String>();
		}

		const PackedStringArray lines = output.split("\n", false);
		for (const String &line : lines) {
			const String trimmed = line.strip_edges();
			if (!trimmed.is_empty()) {
				ignored.push_back(trimmed);
			}
		}
	}
	return ignored;
}

} // namespace ScriptRefactorVCSGuard

#endif // TOOLS_ENABLED
