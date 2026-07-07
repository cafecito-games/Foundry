/**************************************************************************/
/*  editor_layout_store.h                                                 */
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
#include "core/string/ustring.h"

// Single owner of the on-disk `editor_layout.cfg`. Every component that persists
// editor UI state (docks, open scenes, window geometry, the log's filter/collapse
// state, plugin layouts, ...) reads and writes through this store instead of
// opening the file with its own `ConfigFile`. That guarantees exactly one
// serialized load/save cycle, so independently debounced writers can no longer
// clobber each other's sections, and gives the file a schema version so retiring
// or renaming keys can be expressed as an ordered migration instead of an inline
// special case at each writer.
class EditorLayoutStore {
	static EditorLayoutStore *singleton;

	// Explicit override for the config path. When empty the path is resolved from
	// EditorPaths at load/save time; tests inject a scratch path here.
	String config_path;
	Ref<ConfigFile> config;
	bool loaded = false;
	Error load_error = OK;

	String _resolve_path() const;
	void _ensure_loaded();

public:
	// Bump when the on-disk schema changes and add a matching migration branch in
	// run_migrations(). Version 0 is the implicit version of any legacy file that
	// predates the `[meta] version` key.
	static const int CURRENT_VERSION = 1;
	static const char *META_SECTION;
	static const char *VERSION_KEY;

	static EditorLayoutStore *get_singleton() { return singleton; }

	// Reads the persisted `[meta] version`, defaulting to 0 for legacy files that
	// never carried the key.
	static int read_version(const Ref<ConfigFile> &p_config);
	// Applies every migration whose target version is newer than the persisted
	// version, in ascending order, then stamps the config with CURRENT_VERSION.
	// Idempotent: a config already at CURRENT_VERSION is left unchanged.
	static void run_migrations(const Ref<ConfigFile> &p_config);

	// Loads the file from disk (running migrations) on first use and returns the
	// underlying load error. Idempotent — the disk read happens once.
	Error load();
	// Ensures the file is loaded, then stamps the current schema version and writes
	// the whole config back to disk.
	Error save();
	// The shared in-memory config every participant reads and writes. Guarantees the
	// file has been loaded (and migrated) before the caller touches it.
	Ref<ConfigFile> get_config();

	bool is_loaded() const { return loaded; }

	// p_config_path overrides the EditorPaths-derived location; leave empty in the
	// editor and pass a scratch path in tests.
	explicit EditorLayoutStore(const String &p_config_path = String());
	~EditorLayoutStore();
};
