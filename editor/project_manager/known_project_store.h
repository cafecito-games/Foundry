/**************************************************************************/
/*  known_project_store.h                                                 */
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
#include "core/string/ustring.h"
#include "core/templates/local_vector.h"
#include "core/variant/variant.h"

// Global, project-independent store of the projects Foundry knows about. This is
// the data layer the projectless startup surface renders from before any project
// is loaded, so it deliberately lives outside per-project `project_metadata.cfg`
// and instead persists to a single global editor config file (resolved from
// EditorPaths, or an injected scratch path in tests).
//
// The store owns the recent-project list, the auto-open candidate, per-entry
// missing marking, and a cache of each project's display name, last-known version,
// and tags so those can be shown without opening the project. Entries are keyed by
// canonical project path so the same project cannot be recorded twice.
class KnownProjectStore {
public:
	// One record per known project. `path` is always the canonical form used as the
	// dedup key; the cache fields are refreshed from `project.foundry` when it exists.
	struct KnownProject {
		String path;
		String display_name;
		uint64_t last_opened_unix_time = 0;
		String last_known_version;
		PackedStringArray tags;
		bool missing = false;

		bool operator==(const KnownProject &p_other) const {
			return path == p_other.path &&
					display_name == p_other.display_name &&
					last_opened_unix_time == p_other.last_opened_unix_time &&
					last_known_version == p_other.last_known_version &&
					tags == p_other.tags &&
					missing == p_other.missing;
		}
	};

	// Bump when the on-disk schema changes. Version 0 is the implicit version of any
	// file written before the `[meta] version` key existed.
	static const int CURRENT_VERSION = 1;

	// Normalizes a project directory path into the canonical form used for dedup and
	// as the persisted section key: simplified and stripped of a trailing separator.
	static String canonicalize_path(const String &p_path);

private:
	// Explicit override for the config path. When empty the path is resolved from
	// EditorPaths at load/save time; tests inject a scratch path here.
	String config_path;

	LocalVector<KnownProject> projects;
	String auto_open_path;

	int find_project(const String &p_canonical_path) const;
	// Returns the index of the entry for p_canonical_path, appending a bare entry
	// (no config read) when the path is not yet known.
	int _ensure_project(const String &p_canonical_path);
	String _resolve_path() const;

	// Reads `<canonical_path>/project.foundry` and refreshes `r_project`'s cached
	// display name, version, and tags wholesale, clearing its missing flag since the
	// file resolves. Returns false (leaving the record untouched) when the file does
	// not exist or cannot be parsed.
	static bool _read_project_config(const String &p_canonical_path, KnownProject &r_project);

public:
	// Loads the store from disk, replacing any in-memory state. A missing file is not
	// an error: the store simply starts empty. Returns the underlying load error.
	Error load();
	// Writes the whole store back to disk, stamped with CURRENT_VERSION.
	Error save() const;

	// Adds the project if it is not already known (dedup by canonical path) and, when
	// its `project.foundry` exists, refreshes the cached name/version/tags.
	void add_project(const String &p_path);
	// Refreshes the cached name/version/tags of an existing entry from its
	// `project.foundry`, clearing its missing flag when the file resolves. Returns
	// true when the file existed and the cache was updated.
	bool refresh_project(const String &p_path);

	// Records that a project was opened: stamps `last_opened_unix_time`, sets it as
	// the auto-open candidate, and (because recents are sorted by that timestamp)
	// moves it to the front of recents. Adds the project first if unknown. When
	// `p_unix_time` is 0 the current system time is used.
	void mark_project_opened(const String &p_path, uint64_t p_unix_time = 0);

	// The canonical path of the project to auto-open on next launch, or empty if none.
	String get_auto_open_path() const { return auto_open_path; }
	void clear_auto_open() { auto_open_path = String(); }

	// All known projects sorted by most-recently-opened first (ties broken by path).
	Vector<KnownProject> get_recent_projects() const;
	// All known projects sorted by path, for stable `Manage`-tab listing/filtering.
	Vector<KnownProject> get_known_projects() const;

	bool has_project(const String &p_path) const;
	bool get_project(const String &p_path, KnownProject &r_project) const;
	int get_project_count() const { return projects.size(); }

	// Marks an entry missing without removing it. No-op if the path is unknown.
	void mark_project_missing(const String &p_path);
	// Removes a single entry by path. Returns true when an entry was removed. Clears
	// the auto-open candidate if it pointed at the removed entry.
	bool remove_project(const String &p_path);
	// Removes every entry whose `project.foundry` no longer resolves on disk, leaving
	// valid entries untouched. Returns the number of entries removed.
	int remove_missing_projects();

	// Adds a tag to a project's cache if not already present. No-op if unknown.
	void add_tag(const String &p_path, const String &p_tag);
	// Removes a tag from a project's cache. No-op if unknown or the tag is absent.
	void remove_tag(const String &p_path, const String &p_tag);
	// Persists the given tags (sorted) into the project's `project.foundry`
	// (`application/config/tags`) and refreshes this entry's cache from the file
	// that was just written. Returns ERR_DOES_NOT_EXIST when the path is unknown to
	// the store, or the underlying config load/save error on failure.
	Error set_project_tags(const String &p_path, const PackedStringArray &p_tags);

	// p_config_path overrides the EditorPaths-derived location; leave empty in the
	// editor and pass a scratch path in tests.
	explicit KnownProjectStore(const String &p_config_path = String());
};
