/**************************************************************************/
/*  test_startup_dialog.h                                                 */
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

#include "core/io/dir_access.h"
#include "core/os/os.h"

#include "editor/gui/editor_about.h"
#include "editor/project_manager/known_project_store.h"
#include "editor/project_manager/startup_dialog.h"
#include "editor/project_manager/startup_router.h"

#include "tests/editor/editor_workflow_test_fixtures.h"
#include "tests/test_macros.h"
#include "tests/test_utils.h"

namespace TestStartupDialog {

static String make_empty_launch_dir() {
	const String dir = TestUtils::get_temp_path(
			"startup_dialog_launch_" + String::num_uint64(OS::get_singleton()->get_ticks_usec()));
	DirAccess::make_dir_recursive_absolute(dir);
	return dir;
}

static String run_startup_dialog_workflow(const String &p_cwd, int &r_exit_code, const String &p_workflow = "startup_dialog_projects_tab") {
	List<String> arguments;
	arguments.push_back("--headless");
	arguments.push_back("editor");
	arguments.push_back("open");
	arguments.push_back("--automation");
	arguments.push_back("--automation-run-workflow=" + p_workflow);
	return EditorWorkflowTestFixtures::workflow_run_subprocess(arguments, r_exit_code, p_cwd);
}

TEST_CASE("[StartupDialog][Editor] manage filter matches by name, path, and tag") {
	KnownProjectStore::KnownProject project;
	project.path = "/Games/Moonlight/Courier";
	project.display_name = "Moonlight Courier";
	project.tags.push_back("client");
	project.tags.push_back("2d");

	// Empty or whitespace-only queries match everything.
	CHECK(StartupDialog::manage_filter_matches(project, ""));
	CHECK(StartupDialog::manage_filter_matches(project, "   "));

	// Name match, case-insensitive.
	CHECK(StartupDialog::manage_filter_matches(project, "moonlight"));
	CHECK(StartupDialog::manage_filter_matches(project, "COURIER"));

	// Path match, including a path segment not present in the display name.
	CHECK(StartupDialog::manage_filter_matches(project, "games"));

	// Tag match.
	CHECK(StartupDialog::manage_filter_matches(project, "client"));
	CHECK(StartupDialog::manage_filter_matches(project, "2d"));

	// Non-matching query.
	CHECK_FALSE(StartupDialog::manage_filter_matches(project, "platformer"));

	// A project with no cached display name falls back to the path's file name.
	KnownProjectStore::KnownProject unnamed;
	unnamed.path = "/work/foundry-rpg";
	CHECK(StartupDialog::manage_filter_matches(unnamed, "foundry-rpg"));
	CHECK(StartupDialog::manage_filter_matches(unnamed, "work"));
	CHECK_FALSE(StartupDialog::manage_filter_matches(unnamed, "moonlight"));
}

TEST_CASE("[StartupDialog][Editor] opening a recent updates store recents and auto-open") {
	const String scratch = TestUtils::get_temp_path(
			"startup_dialog_store_" + String::num_uint64(OS::get_singleton()->get_ticks_usec()));
	DirAccess::make_dir_recursive_absolute(scratch);

	const String project_dir = scratch.path_join("demo");
	DirAccess::make_dir_recursive_absolute(project_dir);
	Ref<FileAccess> project_file = FileAccess::open(project_dir.path_join("project.foundry"), FileAccess::WRITE);
	REQUIRE(project_file.is_valid());
	project_file->store_string("[application]\nconfig/name=\"Demo\"\n");
	project_file->close();

	KnownProjectStore store(scratch.path_join("known_projects.cfg"));
	store.load();
	const Error save_err = StartupRouter::record_project_opened(store, project_dir);
	CHECK(save_err == OK);
	CHECK(store.get_auto_open_path() == project_dir);

	KnownProjectStore reloaded(scratch.path_join("known_projects.cfg"));
	reloaded.load();
	CHECK(reloaded.get_auto_open_path() == project_dir);
	Vector<KnownProjectStore::KnownProject> recents = reloaded.get_recent_projects();
	REQUIRE(recents.size() == 1);
	CHECK(recents[0].path == project_dir);
}

TEST_CASE("[StartupDialog][Editor] startup dialog projects tab workflow subprocess") {
	if (!EditorWorkflowTestFixtures::workflow_has_display()) {
		MESSAGE("Requires a GUI display. Run with DISPLAY=:1 ./bin/foundry.* --headless test run --case \"*StartupDialog*\" --force-colors");
		return;
	}

	const String launch_dir = make_empty_launch_dir();
	int exit_code = -1;
	const String output = run_startup_dialog_workflow(launch_dir, exit_code);

	INFO(output);
	CHECK(exit_code == 0);
	CHECK(output.contains("\"workflow\":\"startup_dialog_projects_tab\""));
	CHECK(output.contains("\"ok\":true"));
}

TEST_CASE("[StartupDialog][Editor] startup dialog manage tab workflow subprocess") {
	if (!EditorWorkflowTestFixtures::workflow_has_display()) {
		MESSAGE("Requires a GUI display. Run with DISPLAY=:1 ./bin/foundry.* --headless test run --case \"*StartupDialog*\" --force-colors");
		return;
	}

	const String launch_dir = make_empty_launch_dir();
	int exit_code = -1;
	const String output = run_startup_dialog_workflow(launch_dir, exit_code, "startup_dialog_manage_tab");

	INFO(output);
	CHECK(exit_code == 0);
	CHECK(output.contains("\"workflow\":\"startup_dialog_manage_tab\""));
	CHECK(output.contains("\"ok\":true"));
}

TEST_CASE("[StartupDialog][Editor] about copyright text names Foundry and Godot") {
	// The About tab reuses this shared source instead of re-embedding legal text,
	// so both the Foundry and upstream Godot copyright must be present.
	const String copyright = EditorAbout::get_copyright_text();
	CHECK(copyright.contains("Godot Engine contributors"));
	CHECK(copyright.contains("Cafecito Games"));
	CHECK(copyright.contains("Foundry"));
	CHECK(copyright.contains("Juan Linietsky"));
}

TEST_CASE("[StartupDialog][Editor] startup dialog about tab workflow subprocess") {
	if (!EditorWorkflowTestFixtures::workflow_has_display()) {
		MESSAGE("Requires a GUI display. Run with DISPLAY=:1 ./bin/foundry.* --headless test run --case \"*StartupDialog*\" --force-colors");
		return;
	}

	const String launch_dir = make_empty_launch_dir();
	int exit_code = -1;
	const String output = run_startup_dialog_workflow(launch_dir, exit_code, "startup_dialog_about_tab");

	INFO(output);
	CHECK(exit_code == 0);
	CHECK(output.contains("\"workflow\":\"startup_dialog_about_tab\""));
	CHECK(output.contains("\"ok\":true"));
}

} // namespace TestStartupDialog
