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

static String run_projectless_shell_workflow(const String &p_cwd, int &r_exit_code, const String &p_workflow = "projectless_shell_smoke") {
	List<String> arguments;
	arguments.push_back("editor");
	arguments.push_back("open");
	arguments.push_back("--automation");
	arguments.push_back("--automation-run-workflow=" + p_workflow);
	return EditorWorkflowTestFixtures::workflow_run_subprocess(arguments, r_exit_code, p_cwd);
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
	CHECK_FALSE(output.contains("Do not use progress dialog (task) while flushing the message queue or using call_deferred()"));
	CHECK_FALSE(output.contains("Condition \"!tasks.has(p_task)\" is true"));
	CHECK_FALSE(FileAccess::exists(launch_dir.path_join("project.foundry")));
	// The launcher contract: projectless mode with the workspace suppressed.
	CHECK(output.contains("\"mode\":\"projectless_shell\""));
	CHECK(output.contains("\"workspace_exposed\":false"));
}

TEST_CASE("[Editor][ProjectlessShell] opening a project loads it in-process without a relaunch") {
	if (!EditorWorkflowTestFixtures::workflow_has_display()) {
		MESSAGE("Requires a GUI display. Run with DISPLAY=:1 ./bin/foundry.* --headless test run --case \"*ProjectlessShell*\" --force-colors");
		return;
	}

	const String launch_dir = make_empty_launch_dir();
	REQUIRE_FALSE(launch_dir.is_empty());
	REQUIRE_FALSE(FileAccess::exists(launch_dir.path_join("project.foundry")));

	int exit_code = -1;
	const String output = run_projectless_shell_workflow(launch_dir, exit_code, "projectless_shell_open_in_process");
	INFO("Subprocess output:\n", output);

	CHECK(exit_code == 0);
	CHECK(output.contains("\"workflow\":\"projectless_shell_open_in_process\""));
	CHECK(output.contains("\"ok\":true"));
	// The transition target: the same process is now in project mode with the
	// workspace exposed.
	CHECK(output.contains("\"mode\":\"project\""));
	CHECK(output.contains("\"workspace_exposed\":true"));
	// The launch dir itself was never turned into a project; the in-process load
	// targets a throwaway project created by the workflow.
	CHECK_FALSE(FileAccess::exists(launch_dir.path_join("project.foundry")));
}

TEST_CASE("[Editor][ProjectlessShell] a rejected in-process load leaves the shell intact") {
	if (!EditorWorkflowTestFixtures::workflow_has_display()) {
		MESSAGE("Requires a GUI display. Run with DISPLAY=:1 ./bin/foundry.* --headless test run --case \"*ProjectlessShell*\" --force-colors");
		return;
	}

	const String launch_dir = make_empty_launch_dir();
	REQUIRE_FALSE(launch_dir.is_empty());

	int exit_code = -1;
	const String output = run_projectless_shell_workflow(launch_dir, exit_code, "projectless_shell_open_in_process_fallback");
	INFO("Subprocess output:\n", output);

	CHECK(exit_code == 0);
	CHECK(output.contains("\"workflow\":\"projectless_shell_open_in_process_fallback\""));
	CHECK(output.contains("\"ok\":true"));
	// A load that cannot proceed must keep the projectless launcher contract intact.
	CHECK(output.contains("\"mode\":\"projectless_shell\""));
	CHECK(output.contains("\"workspace_exposed\":false"));
}

TEST_CASE("[Editor][ProjectlessShell] dismissing the startup dialog quits the process") {
	if (!EditorWorkflowTestFixtures::workflow_has_display()) {
		MESSAGE("Requires a GUI display. Run with DISPLAY=:1 ./bin/foundry.* --headless test run --case \"*ProjectlessShell*\" --force-colors");
		return;
	}

	const String launch_dir = make_empty_launch_dir();
	REQUIRE_FALSE(launch_dir.is_empty());

	int exit_code = -1;
	const String output = run_projectless_shell_workflow(launch_dir, exit_code, "projectless_shell_dismiss_quits");
	INFO("Subprocess output:\n", output);

	CHECK(exit_code == 0);
	CHECK(output.contains("\"workflow\":\"projectless_shell_dismiss_quits\""));
	CHECK(output.contains("\"ok\":true"));
	// Dismissal must not have created or opened a project on the way out.
	CHECK_FALSE(FileAccess::exists(launch_dir.path_join("project.foundry")));
}

} // namespace TestProjectlessEditorShell
