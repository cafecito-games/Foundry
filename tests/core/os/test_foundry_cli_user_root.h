/**************************************************************************/
/*  test_foundry_cli_user_root.h                                          */
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

#include "main/cli_user_root.h"

#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/os/os.h"

#include "tests/test_macros.h"

namespace TestFoundryCLIUserRoot {

static String owned_scratch_root(const String &p_name) {
	String root;
	if (OS::get_singleton()->has_environment("FOUNDRY_TEST_SCRATCH")) {
		root = OS::get_singleton()->get_environment("FOUNDRY_TEST_SCRATCH");
	}
	if (root.is_empty()) {
		root = OS::get_singleton()->get_temp_path();
	}
	return root.simplify_path().path_join(vformat("foundry-cli-user-root-%s-%d", p_name, OS::get_singleton()->get_process_id()));
}

static void write_scratch_file(const String &p_path, const String &p_contents) {
	Ref<DirAccess> dir = DirAccess::create(DirAccess::ACCESS_FILESYSTEM);
	REQUIRE(dir.is_valid());
	REQUIRE_EQ(dir->make_dir_recursive(p_path.get_base_dir()), OK);
	Ref<FileAccess> file = FileAccess::open(p_path, FileAccess::WRITE);
	REQUIRE_MESSAGE(file.is_valid(), vformat("Cannot write '%s'", p_path));
	file->store_string(p_contents);
}

static void remove_recursive(const String &p_path) {
	Ref<DirAccess> dir = DirAccess::open(p_path);
	if (dir.is_null()) {
		return;
	}
	dir->set_include_hidden(true);
	dir->list_dir_begin();
	for (String entry = dir->get_next(); !entry.is_empty(); entry = dir->get_next()) {
		if (entry == "." || entry == "..") {
			continue;
		}
		const String child = p_path.path_join(entry);
		if (dir->current_is_dir()) {
			remove_recursive(child);
		} else {
			DirAccess::remove_absolute(child);
		}
	}
	dir->list_dir_end();
	DirAccess::remove_absolute(p_path);
}

TEST_CASE("[FoundryCLI][UserRoot] A written artifact inside the root is recognized") {
	const String scratch = owned_scratch_root("holds");
	remove_recursive(scratch);
	const String user_root = scratch.path_join("user-benchmark-9");
	const String artifact = user_root.path_join("foundry/app_userdata/project/benchmark.json");
	write_scratch_file(artifact, "{}");
	write_scratch_file(scratch.path_join("user-benchmark-91/benchmark.json"), "{}");
	write_scratch_file(scratch.path_join("benchmark.json"), "{}");

	CHECK(FoundryCLIUserRoot::root_holds_artifact(artifact, user_root));
	CHECK(FoundryCLIUserRoot::root_holds_artifact(artifact, user_root + "/"));
	// A sibling whose name merely starts with the root's name is not contained.
	CHECK_FALSE(FoundryCLIUserRoot::root_holds_artifact(scratch.path_join("user-benchmark-91/benchmark.json"), user_root));
	CHECK_FALSE(FoundryCLIUserRoot::root_holds_artifact(scratch.path_join("benchmark.json"), user_root));
	CHECK_FALSE(FoundryCLIUserRoot::root_holds_artifact("", user_root));
	CHECK_FALSE(FoundryCLIUserRoot::root_holds_artifact(artifact, ""));
	// A path inside the root that was never written is no artifact at all.
	CHECK_FALSE(FoundryCLIUserRoot::root_holds_artifact(user_root.path_join("never-written.json"), user_root));

	remove_recursive(scratch);
}

TEST_CASE("[FoundryCLI][UserRoot] A directory of outputs inside the root is an artifact") {
	const String scratch = owned_scratch_root("holds-directory");
	remove_recursive(scratch);
	const String user_root = scratch.path_join("user-completeness-9");
	const String artifact_directory = user_root.path_join("foundry/app_userdata/project/completeness");
	write_scratch_file(artifact_directory.path_join("report.json"), "{}");

	// A run whose outputs are a tree, not a single file, must be able to name that tree. Recognizing
	// only files would leave such a run's evidence unprotected from the root's own cleanup.
	CHECK(FoundryCLIUserRoot::root_holds_artifact(artifact_directory, user_root));
	CHECK(FoundryCLIUserRoot::root_holds_artifact(artifact_directory + "/", user_root));
	CHECK_FALSE(FoundryCLIUserRoot::root_holds_artifact(user_root.path_join("never-created"), user_root));
	CHECK_FALSE(FoundryCLIUserRoot::root_holds_artifact(scratch.path_join("outside-directory"), user_root));

	remove_recursive(scratch);
}

TEST_CASE("[FoundryCLI][UserRoot] A root that holds a directory of outputs is kept") {
	const String scratch = owned_scratch_root("kept-directory");
	remove_recursive(scratch);
	const String user_root = scratch.path_join("user-completeness-2");
	const String artifact_directory = user_root.path_join("foundry/app_userdata/project/completeness");
	const String published = artifact_directory.path_join("report.json");
	write_scratch_file(published, "{}");

	Vector<String> artifacts;
	artifacts.push_back(artifact_directory);
	// The path a run writes last may never be written at all; the directory it publishes into is what
	// keeps everything the run did publish.
	artifacts.push_back(artifact_directory.path_join("index-that-was-never-written.json"));
	FoundryCLIUserRoot::remove_owned_root(user_root, artifacts);

	CHECK(DirAccess::exists(user_root));
	CHECK(FileAccess::exists(published));

	remove_recursive(scratch);
}

TEST_CASE("[FoundryCLI][UserRoot] A run's root is removed when it holds no artifact") {
	const String scratch = owned_scratch_root("removed");
	remove_recursive(scratch);
	const String user_root = scratch.path_join("user-benchmark-1");
	write_scratch_file(user_root.path_join("foundry/app_userdata/project/leftover.txt"), "leftover");
	REQUIRE(DirAccess::exists(user_root));

	Vector<String> artifacts;
	artifacts.push_back(scratch.path_join("benchmark.json"));
	FoundryCLIUserRoot::remove_owned_root(user_root, artifacts);

	CHECK_FALSE(DirAccess::exists(user_root));

	remove_recursive(scratch);
}

TEST_CASE("[FoundryCLI][UserRoot] A root that holds the requested artifact is kept") {
	const String scratch = owned_scratch_root("kept");
	remove_recursive(scratch);
	const String user_root = scratch.path_join("user-benchmark-2");
	const String artifact = user_root.path_join("foundry/app_userdata/project/benchmark.json");
	write_scratch_file(artifact, "{}");

	Vector<String> artifacts;
	artifacts.push_back(String());
	artifacts.push_back(artifact);
	FoundryCLIUserRoot::remove_owned_root(user_root, artifacts);

	CHECK(DirAccess::exists(user_root));
	CHECK(FileAccess::exists(artifact));

	remove_recursive(scratch);
}

TEST_CASE("[FoundryCLI][UserRoot] A root whose requested artifact was never written is removed") {
	const String scratch = owned_scratch_root("unwritten");
	remove_recursive(scratch);
	const String user_root = scratch.path_join("user-benchmark-3");
	write_scratch_file(user_root.path_join("foundry/app_userdata/project/other.txt"), "other");

	Vector<String> artifacts;
	artifacts.push_back(user_root.path_join("foundry/app_userdata/project/benchmark.json"));
	FoundryCLIUserRoot::remove_owned_root(user_root, artifacts);

	CHECK_FALSE(DirAccess::exists(user_root));

	remove_recursive(scratch);
}

} // namespace TestFoundryCLIUserRoot
