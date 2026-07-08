/**************************************************************************/
/*  test_projectless_editor_shell.h                                       */
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

#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/os/os.h"

#include "editor/project_manager/known_project_store.h"
#include "editor/project_manager/startup_router.h"

#include "tests/editor/editor_workflow_test_fixtures.h"
#include "tests/test_macros.h"
#include "tests/test_utils.h"

namespace TestProjectlessEditorShell {

static String make_empty_launch_dir() {
	const String dir = TestUtils::get_temp_path(
			"projectless_editor_shell_" + String::num_uint64(OS::get_singleton()->get_ticks_usec()));
	DirAccess::make_dir_recursive_absolute(dir);
	return dir;
}

static String run_projectless_shell_workflow(const String &p_cwd, int &r_exit_code) {
	const String executable = OS::get_singleton()->get_executable_path();
	const String command = vformat(
			"cd %s && %s --headless editor open --automation --automation-run-workflow=projectless_shell_smoke",
			String(p_cwd).c_escape(),
			String(executable).c_escape());

	List<String> args;
	args.push_back("-c");
	args.push_back(command);

	Dictionary environment;
	if (EditorWorkflowTestFixtures::workflow_has_display()) {
		environment["DISPLAY"] = OS::get_singleton()->get_environment("DISPLAY");
	}

	Vector<uint8_t> stdout_bytes;
	Vector<uint8_t> stderr_bytes;
	Dictionary pipe_info = OS::get_singleton()->execute_with_pipe("sh", args, false, String(), environment, false);
	if (pipe_info.is_empty()) {
		r_exit_code = -1;
		return String();
	}

	Ref<FileAccess> stdout_pipe = pipe_info["stdio"];
	Ref<FileAccess> stderr_pipe = pipe_info["stderr"];
	const OS::ProcessID pid = pipe_info["pid"];

	auto pump_pipe = [](const Ref<FileAccess> &p_pipe, Vector<uint8_t> &r_bytes) {
		if (p_pipe.is_null() || !p_pipe->is_open()) {
			return;
		}
		const uint64_t available = p_pipe->get_length();
		if (available == 0) {
			return;
		}
		Vector<uint8_t> chunk;
		chunk.resize(available);
		const uint64_t read = p_pipe->get_buffer(chunk.ptrw(), available);
		if (read > 0) {
			const int offset = r_bytes.size();
			r_bytes.resize(offset + read);
			memcpy(r_bytes.ptrw() + offset, chunk.ptr(), read);
		}
	};

	const uint64_t deadline = OS::get_singleton()->get_ticks_msec() + 120000;
	while (OS::get_singleton()->is_process_running(pid)) {
		pump_pipe(stdout_pipe, stdout_bytes);
		pump_pipe(stderr_pipe, stderr_bytes);
		if (OS::get_singleton()->get_ticks_msec() > deadline) {
			OS::get_singleton()->kill(pid);
			r_exit_code = -1;
			return String();
		}
		OS::get_singleton()->delay_usec(10000);
	}
	pump_pipe(stdout_pipe, stdout_bytes);
	pump_pipe(stderr_pipe, stderr_bytes);

	r_exit_code = OS::get_singleton()->get_process_exit_code(pid);
	String output = String::utf8((const char *)stdout_bytes.ptr(), stdout_bytes.size());
	output += String::utf8((const char *)stderr_bytes.ptr(), stderr_bytes.size());
	return output;
}

TEST_CASE("[Editor][ProjectlessShell] startup router selects projectless shell for empty cwd") {
	const String root = make_empty_launch_dir();
	const String cwd = TestUtils::get_temp_path("projectless_router_cwd_" + String::num_uint64(OS::get_singleton()->get_ticks_usec()));
	DirAccess::make_dir_recursive_absolute(cwd);

	KnownProjectStore store(root.path_join("known_projects.cfg"));
	store.load();

	const StartupRouter::Decision decision = StartupRouter::resolve_launch(false, false, cwd, store);
	CHECK(decision.route == StartupRouter::ROUTE_PROJECTLESS_SHELL);
}

TEST_CASE("[Editor][ProjectlessShell] projectless shell smoke workflow subprocess") {
	if (!EditorWorkflowTestFixtures::workflow_has_display()) {
		MESSAGE("Requires a GUI display. Run with DISPLAY=:1 ./bin/foundry.* --headless test run --case \"*ProjectlessShell*\" --force-colors");
		return;
	}

	const String launch_dir = make_empty_launch_dir();
	REQUIRE_FALSE(launch_dir.is_empty());
	REQUIRE_FALSE(FileAccess::exists(launch_dir.path_join("project.foundry")));

	int exit_code = -1;
	const String output = run_projectless_shell_workflow(launch_dir, exit_code);
	INFO("Subprocess output:\n", output);

	CHECK(exit_code == 0);
	CHECK(output.contains("FOUNDRY_AUTOMATION_WORKFLOW"));
	CHECK(output.contains("\"workflow\":\"projectless_shell_smoke\""));
	CHECK(output.contains("\"ok\":true"));
	CHECK_FALSE(FileAccess::exists(launch_dir.path_join("project.foundry")));
}

} // namespace TestProjectlessEditorShell
