/**************************************************************************/
/*  test_startup_router.h                                                 */
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

#include "core/io/config_file.h"
#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/os/os.h"

#include "core/config/project_settings.h"

#include "editor/project_manager/known_project_store.h"
#include "editor/project_manager/startup_router.h"

#include "tests/test_macros.h"
#include "tests/test_utils.h"

namespace TestStartupRouter {

// A unique scratch directory under the test scratch space, so parallel/aborted runs
// never collide or pollute the repo tree.
static String make_scratch_dir(const String &p_name) {
	const String dir = TestUtils::get_temp_path(p_name + "_" + itos(OS::get_singleton()->get_ticks_usec()));
	DirAccess::make_dir_recursive_absolute(dir);
	return dir;
}

static String config_path_in(const String &p_dir) {
	return p_dir.path_join("known_projects.cfg");
}

// Writes a minimal, current-version `project.foundry` into a fresh subdirectory of
// p_root and returns that project directory path, so the router's on-disk validation
// treats it as openable. p_config_version overrides the stamped config version so tests
// can exercise the compatibility gate.
static String make_project(const String &p_root, const String &p_name, int p_config_version = ProjectSettings::CONFIG_VERSION) {
	const String project_dir = p_root.path_join(p_name);
	DirAccess::make_dir_recursive_absolute(project_dir);

	Ref<ConfigFile> cf;
	cf.instantiate();
	cf->set_value("", "config_version", p_config_version);
	cf->set_value("application", "config/name", p_name);
	cf->set_value("application", "config/features", PackedStringArray());
	REQUIRE(cf->save(project_dir.path_join("project.foundry")) == OK);

	return project_dir;
}

// A directory that exists but has no project.foundry, standing in for a working
// directory that is not itself a project.
static String make_empty_dir(const String &p_root, const String &p_name) {
	const String dir = p_root.path_join(p_name);
	DirAccess::make_dir_recursive_absolute(dir);
	return dir;
}

// A directory whose project.foundry exists but cannot be parsed, standing in for a
// corrupt/invalid remembered project.
static String make_malformed_project(const String &p_root, const String &p_name) {
	const String project_dir = p_root.path_join(p_name);
	DirAccess::make_dir_recursive_absolute(project_dir);

	Ref<FileAccess> f = FileAccess::open(project_dir.path_join("project.foundry"), FileAccess::WRITE);
	REQUIRE(f.is_valid());
	// Unterminated section header: valid file existence, invalid ConfigFile content.
	f->store_string("[application\nconfig/name=\"broken");
	f->close();

	return project_dir;
}

TEST_CASE("[StartupRouter][Editor] is_openable_project recognizes project.foundry") {
	const String scratch = make_scratch_dir("openable");
	const String project_dir = make_project(scratch, "game");
	const String empty_dir = make_empty_dir(scratch, "empty");

	CHECK(StartupRouter::is_openable_project(project_dir));
	CHECK_FALSE(StartupRouter::is_openable_project(empty_dir));
	CHECK_FALSE(StartupRouter::is_openable_project("/definitely/not/here"));
	CHECK_FALSE(StartupRouter::is_openable_project(String()));

	// A present but unparseable project.foundry is not openable.
	const String malformed_dir = make_malformed_project(scratch, "malformed");
	CHECK_FALSE(StartupRouter::is_openable_project(malformed_dir));

	// A project from an older, newer, or unversioned engine needs a conversion/warning
	// prompt, so it is not eligible for silent auto-open.
	CHECK_FALSE(StartupRouter::is_openable_project(make_project(scratch, "older", ProjectSettings::CONFIG_VERSION - 1)));
	CHECK_FALSE(StartupRouter::is_openable_project(make_project(scratch, "newer", ProjectSettings::CONFIG_VERSION + 1)));
	CHECK_FALSE(StartupRouter::is_openable_project(make_project(scratch, "unversioned", 0)));
}

TEST_CASE("[StartupRouter][Editor] an incompatible remembered project falls back and is marked missing") {
	const String scratch = make_scratch_dir("incompatibleremembered");
	const String cwd = make_empty_dir(scratch, "cwd");
	// A parseable project from a newer, incompatible engine version.
	const String incompatible = make_project(scratch, "fromfuture", ProjectSettings::CONFIG_VERSION + 1);

	KnownProjectStore store(config_path_in(scratch));
	store.mark_project_opened(incompatible, 100);

	const StartupRouter::Decision decision = StartupRouter::resolve_launch(
			/*explicit_requested=*/false, /*explicit_valid=*/false, cwd, store);

	CHECK(decision.route == StartupRouter::ROUTE_PROJECTLESS_SHELL);
	CHECK(decision.store_modified);
	KnownProjectStore::KnownProject project;
	REQUIRE(store.get_project(incompatible, project));
	CHECK(project.missing);
	CHECK(store.get_project_count() == 1);
}

TEST_CASE("[StartupRouter][Editor] runtime launch args are detected") {
	// Bare / editor-only args are not runtime launches.
	{
		List<String> args;
		CHECK_FALSE(StartupRouter::args_request_runtime_launch(args));
	}
	{
		List<String> args;
		args.push_back("--verbose");
		args.push_back("--rendering-driver");
		args.push_back("vulkan"); // An option value, not a scene positional.
		CHECK_FALSE(StartupRouter::args_request_runtime_launch(args));
	}

	// Runtime scene/script/main-loop flags are runtime launches.
	for (const String &flag : { String("-s"), String("--script"), String("--main-loop"),
				 String("--scene"), String("--run-test-runner") }) {
		List<String> args;
		args.push_back(flag);
		args.push_back("value");
		CHECK(StartupRouter::args_request_runtime_launch(args));
	}

	// A positional scene resource path is a runtime launch.
	{
		List<String> args;
		args.push_back("res://main.tscn");
		CHECK(StartupRouter::args_request_runtime_launch(args));
	}
	// A positional argument that is not a scene resource is not a runtime launch.
	{
		List<String> args;
		args.push_back("customarg");
		CHECK_FALSE(StartupRouter::args_request_runtime_launch(args));
	}
}

TEST_CASE("[StartupRouter][Editor] a malformed remembered project falls back and is marked missing") {
	const String scratch = make_scratch_dir("malformedremembered");
	const String cwd = make_empty_dir(scratch, "cwd");
	const String malformed = make_malformed_project(scratch, "broken");

	KnownProjectStore store(config_path_in(scratch));
	store.mark_project_opened(malformed, 100);

	const StartupRouter::Decision decision = StartupRouter::resolve_launch(
			/*explicit_requested=*/false, /*explicit_valid=*/false, cwd, store);

	// A corrupt remembered project must not auto-open (and retry) every launch: it routes
	// to the projectless shell and is marked missing.
	CHECK(decision.route == StartupRouter::ROUTE_PROJECTLESS_SHELL);
	CHECK(decision.store_modified);
	KnownProjectStore::KnownProject project;
	REQUIRE(store.get_project(malformed, project));
	CHECK(project.missing);
	CHECK(store.get_project_count() == 1);
}

TEST_CASE("[StartupRouter][Editor] a valid remembered project auto-opens") {
	const String scratch = make_scratch_dir("autoopen");
	const String project_dir = make_project(scratch, "remembered");
	const String cwd = make_empty_dir(scratch, "cwd");

	KnownProjectStore store(config_path_in(scratch));
	store.mark_project_opened(project_dir, 100);

	const StartupRouter::Decision decision = StartupRouter::resolve_launch(
			/*explicit_requested=*/false, /*explicit_valid=*/false, cwd, store);

	CHECK(decision.route == StartupRouter::ROUTE_OPEN_REMEMBERED);
	CHECK(decision.project_path == KnownProjectStore::canonicalize_path(project_dir));
	CHECK_FALSE(decision.store_modified);
}

TEST_CASE("[StartupRouter][Editor] no remembered project routes to the projectless shell") {
	const String scratch = make_scratch_dir("noremembered");
	const String cwd = make_empty_dir(scratch, "cwd");

	KnownProjectStore store(config_path_in(scratch));

	const StartupRouter::Decision decision = StartupRouter::resolve_launch(
			/*explicit_requested=*/false, /*explicit_valid=*/false, cwd, store);

	CHECK(decision.route == StartupRouter::ROUTE_PROJECTLESS_SHELL);
	CHECK(decision.project_path.is_empty());
	CHECK_FALSE(decision.store_modified);
}

TEST_CASE("[StartupRouter][Editor] a missing remembered project falls back and is marked missing") {
	const String scratch = make_scratch_dir("missingremembered");
	const String cwd = make_empty_dir(scratch, "cwd");

	KnownProjectStore store(config_path_in(scratch));
	// Recorded as opened, then the directory never gets a project.foundry (it is gone).
	store.mark_project_opened("/games/gone", 100);

	const StartupRouter::Decision decision = StartupRouter::resolve_launch(
			/*explicit_requested=*/false, /*explicit_valid=*/false, cwd, store);

	CHECK(decision.route == StartupRouter::ROUTE_PROJECTLESS_SHELL);
	CHECK(decision.store_modified);

	// The entry is marked missing but never silently removed.
	KnownProjectStore::KnownProject project;
	REQUIRE(store.get_project("/games/gone", project));
	CHECK(project.missing);
	CHECK(store.get_project_count() == 1);
}

TEST_CASE("[StartupRouter][Editor] a project in the working directory opens directly") {
	const String scratch = make_scratch_dir("cwdproject");
	const String cwd = make_project(scratch, "cwd_game");
	const String remembered = make_project(scratch, "remembered");

	KnownProjectStore store(config_path_in(scratch));
	store.mark_project_opened(remembered, 100);

	const StartupRouter::Decision decision = StartupRouter::resolve_launch(
			/*explicit_requested=*/false, /*explicit_valid=*/false, cwd, store);

	// The working-directory project wins over the remembered project.
	CHECK(decision.route == StartupRouter::ROUTE_OPEN_CWD);
	CHECK_FALSE(decision.store_modified);
}

TEST_CASE("[StartupRouter][Editor] an explicit valid project opens directly") {
	const String scratch = make_scratch_dir("explicitvalid");
	const String cwd = make_empty_dir(scratch, "cwd");

	KnownProjectStore store(config_path_in(scratch));

	const StartupRouter::Decision decision = StartupRouter::resolve_launch(
			/*explicit_requested=*/true, /*explicit_valid=*/true, cwd, store);

	CHECK(decision.route == StartupRouter::ROUTE_OPEN_EXPLICIT);
	CHECK_FALSE(decision.store_modified);
}

TEST_CASE("[StartupRouter][Editor] an explicit invalid project never auto-opens a remembered one") {
	const String scratch = make_scratch_dir("explicitinvalid");
	const String remembered = make_project(scratch, "remembered");
	const String cwd = make_empty_dir(scratch, "cwd");

	KnownProjectStore store(config_path_in(scratch));
	// A perfectly valid remembered project exists; it must NOT be opened as a fallback.
	store.mark_project_opened(remembered, 100);

	const StartupRouter::Decision decision = StartupRouter::resolve_launch(
			/*explicit_requested=*/true, /*explicit_valid=*/false, cwd, store);

	CHECK(decision.route == StartupRouter::ROUTE_EXPLICIT_INVALID);
	CHECK(decision.project_path.is_empty());
	// The invalid explicit path must not mark the healthy remembered project missing.
	CHECK_FALSE(decision.store_modified);
	KnownProjectStore::KnownProject project;
	REQUIRE(store.get_project(remembered, project));
	CHECK_FALSE(project.missing);
}

TEST_CASE("[StartupRouter][Editor] recording an open updates auto-open and recents ordering") {
	const String scratch = make_scratch_dir("record");
	const String project_a = make_project(scratch, "a");
	const String project_b = make_project(scratch, "b");
	const String path = config_path_in(scratch);

	{
		KnownProjectStore store(path);
		REQUIRE(StartupRouter::record_project_opened(store, project_a) == OK);
		REQUIRE(StartupRouter::record_project_opened(store, project_b) == OK);
	}

	// The record persisted: reloading sees B (the last open) as the auto-open candidate
	// and both projects present in recents. Timestamp-based recents ordering is covered
	// with explicit timestamps in the KnownProjectStore tests; here two opens can share a
	// wall-clock second, so only the last-opened/auto-open guarantee is asserted.
	KnownProjectStore reloaded(path);
	REQUIRE(reloaded.load() == OK);
	CHECK(reloaded.get_auto_open_path() == KnownProjectStore::canonicalize_path(project_b));

	Vector<KnownProjectStore::KnownProject> recents = reloaded.get_recent_projects();
	REQUIRE(recents.size() == 2);
	CHECK(reloaded.has_project(project_a));
	CHECK(reloaded.has_project(project_b));

	// A subsequent launch from a non-project working directory auto-opens B.
	const String cwd = make_empty_dir(scratch, "cwd");
	const StartupRouter::Decision decision = StartupRouter::resolve_launch(
			/*explicit_requested=*/false, /*explicit_valid=*/false, cwd, reloaded);
	CHECK(decision.route == StartupRouter::ROUTE_OPEN_REMEMBERED);
	CHECK(decision.project_path == KnownProjectStore::canonicalize_path(project_b));
}

} // namespace TestStartupRouter
