/**************************************************************************/
/*  test_known_project_store.h                                            */
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
#include "core/os/os.h"

#include "editor/project_manager/known_project_store.h"

#include "tests/test_macros.h"
#include "tests/test_utils.h"

namespace TestKnownProjectStore {

// A unique scratch directory under the test scratch space, so parallel/aborted
// runs never collide or pollute the repo tree.
static String make_scratch_dir(const String &p_name) {
	const String dir = TestUtils::get_temp_path(p_name + "_" + itos(OS::get_singleton()->get_ticks_usec()));
	DirAccess::make_dir_recursive_absolute(dir);
	return dir;
}

static String config_path_in(const String &p_dir) {
	return p_dir.path_join("known_projects.cfg");
}

// Writes a minimal `project.foundry` into a fresh subdirectory of p_root and
// returns that project directory path.
static String make_project(const String &p_root, const String &p_name, const String &p_display_name, const String &p_version, const PackedStringArray &p_tags) {
	const String project_dir = p_root.path_join(p_name);
	DirAccess::make_dir_recursive_absolute(project_dir);

	Ref<ConfigFile> cf;
	cf.instantiate();
	cf->set_value("application", "config/name", p_display_name);
	if (!p_tags.is_empty()) {
		cf->set_value("application", "config/tags", p_tags);
	}
	PackedStringArray features;
	if (!p_version.is_empty()) {
		features.push_back(p_version);
	}
	cf->set_value("application", "config/features", features);
	REQUIRE(cf->save(project_dir.path_join("project.foundry")) == OK);

	return project_dir;
}

TEST_CASE("[KnownProjectStore][Editor] canonicalize_path dedups equivalent paths") {
	CHECK(KnownProjectStore::canonicalize_path("/a/b/") == "/a/b");
	CHECK(KnownProjectStore::canonicalize_path("/a/b") == "/a/b");
	CHECK(KnownProjectStore::canonicalize_path("/a/./b") == "/a/b");
	CHECK(KnownProjectStore::canonicalize_path("/") == "/");
}

TEST_CASE("[KnownProjectStore][Editor] adding the same path twice does not duplicate") {
	const String scratch = make_scratch_dir("dedup");
	KnownProjectStore store(config_path_in(scratch));

	store.add_project("/games/moonlight");
	store.add_project("/games/moonlight/"); // Trailing slash resolves to the same entry.
	store.add_project("/games/moonlight");

	CHECK(store.get_project_count() == 1);
	CHECK(store.has_project("/games/moonlight"));
}

TEST_CASE("[KnownProjectStore][Editor] mark_project_opened sets auto-open and fronts recents") {
	const String scratch = make_scratch_dir("markopen");
	KnownProjectStore store(config_path_in(scratch));

	store.mark_project_opened("/games/a", 100);
	store.mark_project_opened("/games/b", 200);
	store.mark_project_opened("/games/c", 300);

	// Most recent open becomes the auto-open candidate.
	CHECK(store.get_auto_open_path() == "/games/c");

	// Re-opening an older project moves it back to the front.
	store.mark_project_opened("/games/a", 400);
	CHECK(store.get_auto_open_path() == "/games/a");

	Vector<KnownProjectStore::KnownProject> recents = store.get_recent_projects();
	REQUIRE(recents.size() == 3);
	CHECK(recents[0].path == "/games/a");
	CHECK(recents[1].path == "/games/c");
	CHECK(recents[2].path == "/games/b");
}

TEST_CASE("[KnownProjectStore][Editor] recents are sorted by last-opened descending") {
	const String scratch = make_scratch_dir("recentsort");
	KnownProjectStore store(config_path_in(scratch));

	store.mark_project_opened("/games/older", 10);
	store.mark_project_opened("/games/newest", 30);
	store.mark_project_opened("/games/middle", 20);

	Vector<KnownProjectStore::KnownProject> recents = store.get_recent_projects();
	REQUIRE(recents.size() == 3);
	CHECK(recents[0].path == "/games/newest");
	CHECK(recents[1].path == "/games/middle");
	CHECK(recents[2].path == "/games/older");
}

TEST_CASE("[KnownProjectStore][Editor] cache is refreshed from project.foundry when it exists") {
	const String scratch = make_scratch_dir("refresh");
	PackedStringArray tags;
	tags.push_back("client");
	tags.push_back("2d");
	const String project_dir = make_project(scratch, "moonlight", "Moonlight Courier", "4.6", tags);

	KnownProjectStore store(config_path_in(scratch));
	store.add_project(project_dir);

	KnownProjectStore::KnownProject project;
	REQUIRE(store.get_project(project_dir, project));
	CHECK(project.display_name == "Moonlight Courier");
	CHECK(project.last_known_version == "4.6");
	REQUIRE(project.tags.size() == 2);
	CHECK(project.tags.has("client"));
	CHECK(project.tags.has("2d"));

	// A project without a config on disk keeps empty cache fields but is still known.
	store.add_project("/nonexistent/project");
	KnownProjectStore::KnownProject missing_cache;
	REQUIRE(store.get_project("/nonexistent/project", missing_cache));
	CHECK(missing_cache.display_name.is_empty());
	CHECK(missing_cache.last_known_version.is_empty());
	CHECK(missing_cache.tags.is_empty());
}

TEST_CASE("[KnownProjectStore][Editor] a missing path is markable and never auto-removed") {
	const String scratch = make_scratch_dir("markmissing");
	KnownProjectStore store(config_path_in(scratch));

	store.add_project("/games/gone");
	store.mark_project_missing("/games/gone");

	KnownProjectStore::KnownProject project;
	REQUIRE(store.get_project("/games/gone", project));
	CHECK(project.missing);
	// Marking missing must not remove the entry.
	CHECK(store.get_project_count() == 1);
}

TEST_CASE("[KnownProjectStore][Editor] a single entry can be removed explicitly") {
	const String scratch = make_scratch_dir("removeone");
	KnownProjectStore store(config_path_in(scratch));

	store.mark_project_opened("/games/a", 100);
	store.mark_project_opened("/games/b", 200);

	CHECK(store.remove_project("/games/b"));
	CHECK_FALSE(store.has_project("/games/b"));
	CHECK(store.has_project("/games/a"));
	CHECK(store.get_project_count() == 1);
	// Removing the auto-open candidate clears it.
	CHECK(store.get_auto_open_path().is_empty());

	// Removing an unknown path is a no-op returning false.
	CHECK_FALSE(store.remove_project("/games/unknown"));
}

TEST_CASE("[KnownProjectStore][Editor] bulk remove-missing prunes only unresolved paths") {
	const String scratch = make_scratch_dir("removemissing");
	const String present = make_project(scratch, "present", "Present", "4.6", PackedStringArray());

	KnownProjectStore store(config_path_in(scratch));
	store.add_project(present);
	store.add_project("/definitely/not/here");
	store.add_project("/also/gone");

	const int removed = store.remove_missing_projects();
	CHECK(removed == 2);
	CHECK(store.get_project_count() == 1);
	CHECK(store.has_project(present));
	CHECK_FALSE(store.has_project("/definitely/not/here"));
}

TEST_CASE("[KnownProjectStore][Editor] tags can be added and removed on the cache") {
	const String scratch = make_scratch_dir("tags");
	KnownProjectStore store(config_path_in(scratch));

	store.add_project("/games/rpg");
	store.add_tag("/games/rpg", "rpg");
	store.add_tag("/games/rpg", "prototype");
	store.add_tag("/games/rpg", "rpg"); // Duplicate is ignored.

	KnownProjectStore::KnownProject project;
	REQUIRE(store.get_project("/games/rpg", project));
	REQUIRE(project.tags.size() == 2);
	CHECK(project.tags.has("rpg"));
	CHECK(project.tags.has("prototype"));

	store.remove_tag("/games/rpg", "prototype");
	REQUIRE(store.get_project("/games/rpg", project));
	REQUIRE(project.tags.size() == 1);
	CHECK(project.tags.has("rpg"));
	CHECK_FALSE(project.tags.has("prototype"));
}

TEST_CASE("[KnownProjectStore][Editor] tag cache refreshes from project.foundry") {
	const String scratch = make_scratch_dir("tagrefresh");
	PackedStringArray tags;
	tags.push_back("archived");
	const String project_dir = make_project(scratch, "jam", "Old Jam Build", "4.5", tags);

	KnownProjectStore store(config_path_in(scratch));
	store.add_project(project_dir);
	store.add_tag(project_dir, "manual"); // A locally added tag before the refresh.

	// Refreshing pulls the on-disk tag set, replacing the cached tags.
	CHECK(store.refresh_project(project_dir));
	KnownProjectStore::KnownProject project;
	REQUIRE(store.get_project(project_dir, project));
	REQUIRE(project.tags.size() == 1);
	CHECK(project.tags.has("archived"));

	// Refreshing an unknown project reports no update.
	CHECK_FALSE(store.refresh_project("/games/unknown"));
}

TEST_CASE("[KnownProjectStore][Editor] untagged projects round-trip with empty tags") {
	const String scratch = make_scratch_dir("untagged");
	const String project_dir = make_project(scratch, "untagged", "No Tags", "4.6", PackedStringArray());

	{
		KnownProjectStore store(config_path_in(scratch));
		store.add_project(project_dir);
		KnownProjectStore::KnownProject project;
		REQUIRE(store.get_project(project_dir, project));
		CHECK(project.tags.is_empty());
		CHECK(store.save() == OK);
	}

	KnownProjectStore reloaded(config_path_in(scratch));
	CHECK(reloaded.load() == OK);
	KnownProjectStore::KnownProject project;
	REQUIRE(reloaded.get_project(project_dir, project));
	CHECK(project.tags.is_empty());
}

TEST_CASE("[KnownProjectStore][Editor] the store round-trips through save/load") {
	const String scratch = make_scratch_dir("roundtrip");
	const String path = config_path_in(scratch);

	{
		KnownProjectStore store(path);
		store.mark_project_opened("/games/a", 100);
		store.mark_project_opened("/games/b", 200);
		store.add_project("/games/c");
		store.mark_project_missing("/games/c");
		store.add_tag("/games/a", "client");
		CHECK(store.save() == OK);
	}

	KnownProjectStore reloaded(path);
	CHECK(reloaded.load() == OK);

	CHECK(reloaded.get_project_count() == 3);
	CHECK(reloaded.get_auto_open_path() == "/games/b");

	KnownProjectStore::KnownProject a;
	REQUIRE(reloaded.get_project("/games/a", a));
	CHECK(a.last_opened_unix_time == 100);
	REQUIRE(a.tags.size() == 1);
	CHECK(a.tags.has("client"));

	KnownProjectStore::KnownProject c;
	REQUIRE(reloaded.get_project("/games/c", c));
	CHECK(c.missing);

	// The recents ordering survives the round-trip.
	Vector<KnownProjectStore::KnownProject> recents = reloaded.get_recent_projects();
	REQUIRE(recents.size() == 3);
	CHECK(recents[0].path == "/games/b");
	CHECK(recents[1].path == "/games/a");
	CHECK(recents[2].path == "/games/c");
}

TEST_CASE("[KnownProjectStore][Editor] loading a missing store starts empty") {
	const String scratch = make_scratch_dir("emptyload");
	KnownProjectStore store(config_path_in(scratch));
	// No file written yet: load reports the failure but leaves a usable empty store.
	CHECK(store.load() != OK);
	CHECK(store.get_project_count() == 0);
	CHECK(store.get_auto_open_path().is_empty());
}

} // namespace TestKnownProjectStore
