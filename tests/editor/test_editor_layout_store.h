/**************************************************************************/
/*  test_editor_layout_store.h                                            */
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

#include "core/io/config_file.h"
#include "core/os/os.h"

#include "editor/editor_layout_store.h"

#include "tests/test_macros.h"
#include "tests/test_utils.h"

namespace TestEditorLayoutStore {

static String make_layout_path(const String &p_name) {
	return TestUtils::get_temp_path(p_name + "_" + itos(OS::get_singleton()->get_ticks_usec()) + ".cfg");
}

TEST_CASE("[EditorLayoutStore][Editor] save stamps the current schema version") {
	const String path = make_layout_path("layout_version");

	EditorLayoutStore *store = memnew(EditorLayoutStore(path));
	store->get_config()->set_value("EditorNode", "current_scene", "res://main.tscn");
	CHECK(store->save() == OK);
	memdelete(store);

	// Read the raw file back with an independent ConfigFile: the version key must be
	// present and equal to the current schema version.
	Ref<ConfigFile> on_disk;
	on_disk.instantiate();
	REQUIRE(on_disk->load(path) == OK);
	CHECK(EditorLayoutStore::read_version(on_disk) == EditorLayoutStore::CURRENT_VERSION);
	CHECK(String(on_disk->get_value("EditorNode", "current_scene", String())) == "res://main.tscn");
}

TEST_CASE("[EditorLayoutStore][Editor] read_version defaults to 0 for an unversioned legacy file") {
	Ref<ConfigFile> legacy;
	legacy.instantiate();
	legacy->set_value("EditorNode", "current_scene", "res://main.tscn");
	// No [meta] version written: a pre-versioning layout reads as version 0.
	CHECK(EditorLayoutStore::read_version(legacy) == 0);
}

TEST_CASE("[EditorLayoutStore][Editor] loading an old file runs the retired-key migration") {
	const String path = make_layout_path("layout_migrate");

	// A legacy layout: no version key, carries the retired raw-index main-screen key
	// alongside an unrelated key that a migration must not touch.
	{
		Ref<ConfigFile> legacy;
		legacy.instantiate();
		legacy->set_value("EditorNode", "selected_main_editor_idx", 1);
		legacy->set_value("EditorNode", "current_scene", "res://main.tscn");
		legacy->set_value("editor_log", "collapse", true);
		REQUIRE(legacy->save(path) == OK);
	}

	EditorLayoutStore *store = memnew(EditorLayoutStore(path));
	CHECK(store->load() == OK);
	Ref<ConfigFile> config = store->get_config();

	// The migration dropped the retired key and left the unrelated keys intact.
	CHECK_FALSE(config->has_section_key("EditorNode", "selected_main_editor_idx"));
	CHECK(String(config->get_value("EditorNode", "current_scene", String())) == "res://main.tscn");
	CHECK(bool(config->get_value("editor_log", "collapse", false)) == true);
	// The in-memory config now reports the current schema version.
	CHECK(EditorLayoutStore::read_version(config) == EditorLayoutStore::CURRENT_VERSION);

	memdelete(store);
}

TEST_CASE("[EditorLayoutStore][Editor] migration leaves an up-to-date config untouched") {
	// A config already at the current version should be treated as up to date: a key
	// that merely shares the retired name is not dropped, because no migration for
	// this version runs.
	Ref<ConfigFile> current;
	current.instantiate();
	current->set_value(EditorLayoutStore::META_SECTION, EditorLayoutStore::VERSION_KEY, EditorLayoutStore::CURRENT_VERSION);
	current->set_value("EditorNode", "selected_main_editor_idx", 1);

	EditorLayoutStore::run_migrations(current);

	CHECK(current->has_section_key("EditorNode", "selected_main_editor_idx"));
	CHECK(EditorLayoutStore::read_version(current) == EditorLayoutStore::CURRENT_VERSION);
}

TEST_CASE("[EditorLayoutStore][Editor] two independent writers do not clobber each other's sections") {
	const String path = make_layout_path("layout_clobber");

	// Both a main-editor-style writer and a log-style writer amend the one config the
	// store owns, then the store performs the single save. Previously these were
	// independent full-file saves that could erase each other's section.
	EditorLayoutStore *store = memnew(EditorLayoutStore(path));

	Ref<ConfigFile> config = store->get_config();
	config->set_value("EditorNode", "current_scene", "res://main.tscn");
	config->set_value("editor_log", "collapse", true);
	config->set_value("editor_log", "show_search", false);
	CHECK(store->save() == OK);
	memdelete(store);

	// A fresh store reading the same path sees both sections fully preserved.
	EditorLayoutStore *reopened = memnew(EditorLayoutStore(path));
	Ref<ConfigFile> reloaded = reopened->get_config();
	CHECK(String(reloaded->get_value("EditorNode", "current_scene", String())) == "res://main.tscn");
	CHECK(bool(reloaded->get_value("editor_log", "collapse", false)) == true);
	CHECK(bool(reloaded->get_value("editor_log", "show_search", true)) == false);
	memdelete(reopened);
}

TEST_CASE("[EditorLayoutStore][Editor] a missing file surfaces a load error but yields a usable config") {
	const String path = make_layout_path("layout_missing");

	EditorLayoutStore *store = memnew(EditorLayoutStore(path));
	// No file exists at the path yet: load() reports the error the way EditorNode
	// branches on, but participants still get a valid empty config to read defaults.
	CHECK(store->load() != OK);
	Ref<ConfigFile> config = store->get_config();
	REQUIRE(config.is_valid());
	CHECK(String(config->get_value("EditorNode", "current_scene", "default")) == "default");
	memdelete(store);
}

} // namespace TestEditorLayoutStore
