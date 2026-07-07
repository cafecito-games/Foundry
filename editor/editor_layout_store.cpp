/**************************************************************************/
/*  editor_layout_store.cpp                                               */
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

#include "editor/editor_layout_store.h"

#include "editor/file_system/editor_paths.h"

EditorLayoutStore *EditorLayoutStore::singleton = nullptr;

const int EditorLayoutStore::CURRENT_VERSION;

const char *EditorLayoutStore::META_SECTION = "meta";
const char *EditorLayoutStore::VERSION_KEY = "version";

// The retired raw-index main-screen key, dropped by the version 0 -> 1 migration.
// A layout written before the name-based selection landed would otherwise carry
// this dead entry forever, since ConfigFile::save preserves unrecognized keys.
static const char *EDITOR_NODE_SECTION = "EditorNode";
static const char *RETIRED_SELECTED_MAIN_EDITOR_IDX = "selected_main_editor_idx";

int EditorLayoutStore::read_version(const Ref<ConfigFile> &p_config) {
	ERR_FAIL_COND_V(p_config.is_null(), 0);
	return (int)p_config->get_value(META_SECTION, VERSION_KEY, 0);
}

void EditorLayoutStore::run_migrations(const Ref<ConfigFile> &p_config) {
	ERR_FAIL_COND(p_config.is_null());

	const int version = read_version(p_config);
	if (version >= CURRENT_VERSION) {
		return;
	}

	// Version 0 -> 1: name-based main-screen persistence retired the raw-index key.
	if (version < 1) {
		if (p_config->has_section_key(EDITOR_NODE_SECTION, RETIRED_SELECTED_MAIN_EDITOR_IDX)) {
			p_config->erase_section_key(EDITOR_NODE_SECTION, RETIRED_SELECTED_MAIN_EDITOR_IDX);
		}
	}

	// Record that the in-memory config now matches the current schema so a later
	// save (or a repeated migration pass) treats it as up to date.
	p_config->set_value(META_SECTION, VERSION_KEY, CURRENT_VERSION);
}

String EditorLayoutStore::_resolve_path() const {
	if (!config_path.is_empty()) {
		return config_path;
	}
	return EditorPaths::get_singleton()->get_project_settings_dir().path_join("editor_layout.cfg");
}

void EditorLayoutStore::_ensure_loaded() {
	if (loaded) {
		return;
	}
	if (config.is_null()) {
		config.instantiate();
	}
	// A missing file is not an error the store recovers from here: participants
	// still expect a valid (empty) config so they can fall back to defaults. The
	// load error is surfaced through load() for callers that branch on it.
	load_error = config->load(_resolve_path());
	run_migrations(config);
	loaded = true;
}

Error EditorLayoutStore::load() {
	_ensure_loaded();
	return load_error;
}

Ref<ConfigFile> EditorLayoutStore::get_config() {
	_ensure_loaded();
	return config;
}

Error EditorLayoutStore::save() {
	// Never persist without first pulling in the existing on-disk content, so a
	// save that races ahead of the initial load cannot truncate the file.
	_ensure_loaded();
	config->set_value(META_SECTION, VERSION_KEY, CURRENT_VERSION);
	return config->save(_resolve_path());
}

EditorLayoutStore::EditorLayoutStore(const String &p_config_path) {
	config_path = p_config_path;
	singleton = this;
}

EditorLayoutStore::~EditorLayoutStore() {
	if (singleton == this) {
		singleton = nullptr;
	}
}
