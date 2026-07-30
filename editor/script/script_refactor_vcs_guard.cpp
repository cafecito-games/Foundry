/**************************************************************************/
/*  script_refactor_vcs_guard.cpp                                         */
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

// Whether a usable git executable can be launched. The no-pipe OS::execute()
// path spawns the process directly (create_process / posix_spawn) and returns a
// non-OK Error when the binary cannot be spawned, so it is a reliable presence
// probe — unlike execute_with_pipe(), which returns a pipe dictionary on Unix
// before the child's execvp() has had a chance to fail.
bool git_available() {
	List<String> args;
	args.push_back("--version");
	int exit_code = -1;
	const Error err = OS::get_singleton()->execute("git", args, nullptr, &exit_code, false);
	return err == OK && exit_code == 0;
}

// Appends any bytes currently available on p_pipe to r_bytes and returns how many
// were read. A pipe FileAccess never reports eof_reached(), so callers detect the
// end of output by the child process having exited together with a zero-length
// read, not by polling eof.
uint64_t pump_pipe(const Ref<FileAccess> &p_pipe, Vector<uint8_t> &r_bytes) {
	if (p_pipe.is_null()) {
		return 0;
	}
	uint8_t chunk[4096];
	const uint64_t read = p_pipe->get_buffer(chunk, sizeof(chunk));
	if (read > 0) {
		const int64_t start = r_bytes.size();
		r_bytes.resize(start + (int64_t)read);
		memcpy(r_bytes.ptrw() + start, chunk, read);
	}
	return read;
}

String bytes_to_string(const Vector<uint8_t> &p_bytes) {
	String text;
	if (!p_bytes.is_empty()) {
		const Error err = text.append_utf8((const char *)p_bytes.ptr(), p_bytes.size());
		(void)err;
	}
	return text;
}

// Runs `git -C <project> <args...>`. Returns false if git could not be launched
// at all, in which case exit code and output are untouched. A successful launch
// with a non-zero exit code still returns true.
//
// This deliberately uses execute_with_pipe() rather than the simpler
// OS::execute(..., &output, ...) overload: the latter builds a single shell
// command string and runs it through popen() on Unix, so a project or target
// path containing shell metacharacters (quotes, backticks, $(...)) would be
// interpreted by the shell. execute_with_pipe() spawns git directly with an argv
// vector (execvp / CreateProcess), so paths are passed verbatim and cannot inject
// shell commands. Callers must verify git is present (git_available()) first,
// since on Unix this returns a pipe dictionary before the child's execvp() fails.
bool run_git(const String &p_project_path, const Vector<String> &p_args, int &r_exit_code, String &r_output) {
	List<String> arguments;
	arguments.push_back("-C");
	arguments.push_back(p_project_path);
	for (const String &arg : p_args) {
		arguments.push_back(arg);
	}

	// Non-blocking pipes so stdout and stderr are pumped in the same loop; draining
	// one fully before the other (with blocking pipes) can deadlock if the child
	// fills the not-yet-read pipe's buffer before exiting.
	Dictionary pipe_info = OS::get_singleton()->execute_with_pipe("git", arguments, false);
	if (pipe_info.is_empty()) {
		return false; // git could not be launched.
	}

	Ref<FileAccess> stdout_pipe = pipe_info["stdio"];
	Ref<FileAccess> stderr_pipe = pipe_info["stderr"];
	OS::ProcessID pid = pipe_info["pid"];

	Vector<uint8_t> stdout_bytes;
	Vector<uint8_t> stderr_bytes; // Drained but discarded; keeps the child from blocking on stderr.
	while (true) {
		// Pump both pipes each iteration so neither side can deadlock by filling its
		// buffer while the other is being drained.
		const uint64_t read_out = pump_pipe(stdout_pipe, stdout_bytes);
		const uint64_t read_err = pump_pipe(stderr_pipe, stderr_bytes);

		if (read_out == 0 && read_err == 0) {
			if (!OS::get_singleton()->is_process_running(pid)) {
				// The process has exited and this pass read nothing. Make one final pass
				// to collect anything written just before exit, then stop if it is also
				// empty (avoids a race where output lands between read and exit check).
				const uint64_t final_out = pump_pipe(stdout_pipe, stdout_bytes);
				const uint64_t final_err = pump_pipe(stderr_pipe, stderr_bytes);
				if (final_out == 0 && final_err == 0) {
					break;
				}
			} else {
				OS::get_singleton()->delay_usec(1000);
			}
		}
	}

	if (stdout_pipe.is_valid()) {
		stdout_pipe->close();
	}
	if (stderr_pipe.is_valid()) {
		stderr_pipe->close();
	}

	r_exit_code = OS::get_singleton()->get_process_exit_code(pid);
	r_output = bytes_to_string(stdout_bytes);
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

	// Probe for git first: on Unix execute_with_pipe() reports a successful launch
	// before the child's execvp() can fail, so run_git() alone cannot tell a
	// missing git from a real error. Without this, a missing git would look like a
	// clean rev-parse failure and be misread as "not a work tree" (UNVERSIONED).
	if (!git_available()) {
		return evaluate(state); // git_available stays false -> UNKNOWN.
	}
	state.git_available = true;

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
			state.git_available = false;
			return evaluate(state);
		}
		state.inside_work_tree = (exit_code == 0) && (output.strip_edges() == "true");
	}

	if (state.inside_work_tree) {
		Vector<String> args;
		// `-c status.showUntrackedFiles=all` overrides a repo/user setting of `no`
		// or `normal` that would otherwise hide untracked (or untracked-in-subdir)
		// scripts the migration can still overwrite, and `--untracked-files=all`
		// enforces the same. Ignored paths are deliberately NOT surfaced here:
		// normal Godot checkouts ignore `.foundry/`, `bin/`, generated headers, etc.,
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

	if (!git_available()) {
		r_check_succeeded = false;
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
