/**************************************************************************/
/*  known_project_store.cpp                                               */
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

#include "known_project_store.h"

#include "core/io/file_access.h"
#include "core/os/time.h"

#include "editor/file_system/editor_paths.h"

namespace {

const char *META_SECTION = "meta";
const char *VERSION_KEY = "version";
const char *AUTO_OPEN_KEY = "auto_open";

const char *DISPLAY_NAME_KEY = "display_name";
const char *LAST_OPENED_KEY = "last_opened_unix_time";
const char *LAST_VERSION_KEY = "last_known_version";
const char *TAGS_KEY = "tags";
const char *MISSING_KEY = "missing";

// Matches the version-detection heuristic used by the project list: a feature that
// starts with digits and contains a dot (e.g. "4.6") is treated as a version stamp.
bool feature_looks_like_version(const String &p_feature) {
	return p_feature.contains_char('.') && p_feature.substr(0, 3).is_numeric();
}

// Most-recently-opened first, with the path as a deterministic tie-breaker so
// never-opened entries (all sharing timestamp 0) still sort stably.
struct RecentSort {
	bool operator()(const KnownProjectStore::KnownProject &p_a, const KnownProjectStore::KnownProject &p_b) const {
		if (p_a.last_opened_unix_time != p_b.last_opened_unix_time) {
			return p_a.last_opened_unix_time > p_b.last_opened_unix_time;
		}
		return p_a.path < p_b.path;
	}
};

struct PathSort {
	bool operator()(const KnownProjectStore::KnownProject &p_a, const KnownProjectStore::KnownProject &p_b) const {
		return p_a.path < p_b.path;
	}
};

} // namespace

const int KnownProjectStore::CURRENT_VERSION;

String KnownProjectStore::canonicalize_path(const String &p_path) {
	String canonical = p_path.simplify_path();
	// Strip a trailing separator so "/a/b" and "/a/b/" resolve to the same key. The
	// root path ("/") is left intact so it never collapses to an empty key.
	if (canonical.length() > 1 && canonical.ends_with("/")) {
		canonical = canonical.trim_suffix("/");
	}
	return canonical;
}

int KnownProjectStore::find_project(const String &p_canonical_path) const {
	for (uint32_t i = 0; i < projects.size(); i++) {
		if (projects[i].path == p_canonical_path) {
			return (int)i;
		}
	}
	return -1;
}

int KnownProjectStore::_ensure_project(const String &p_canonical_path) {
	const int index = find_project(p_canonical_path);
	if (index != -1) {
		return index;
	}
	KnownProject project;
	project.path = p_canonical_path;
	projects.push_back(project);
	return (int)projects.size() - 1;
}

String KnownProjectStore::_resolve_path() const {
	if (!config_path.is_empty()) {
		return config_path;
	}
	return EditorPaths::get_singleton()->get_data_dir().path_join("known_projects.cfg");
}

bool KnownProjectStore::_read_project_config(const String &p_canonical_path, KnownProject &r_project) {
	const String conf = p_canonical_path.path_join("project.foundry");
	if (!FileAccess::exists(conf)) {
		return false;
	}

	Ref<ConfigFile> cf;
	cf.instantiate();
	if (cf->load(conf) != OK) {
		return false;
	}

	const String config_name = cf->get_value("application", "config/name", "");
	if (!config_name.is_empty()) {
		r_project.display_name = config_name.xml_unescape();
	}

	r_project.tags = cf->get_value("application", "config/tags", PackedStringArray());

	const PackedStringArray features = cf->get_value("application", "config/features", PackedStringArray());
	for (const String &feature : features) {
		if (feature_looks_like_version(feature)) {
			r_project.last_known_version = feature;
			break;
		}
	}

	return true;
}

Error KnownProjectStore::load() {
	projects.clear();
	auto_open_path = String();

	Ref<ConfigFile> config;
	config.instantiate();
	const Error err = config->load(_resolve_path());
	if (err != OK) {
		// A missing store is expected on first launch: start empty and report the
		// error to callers that branch on it.
		return err;
	}

	auto_open_path = config->get_value(META_SECTION, AUTO_OPEN_KEY, String());

	for (const String &section : config->get_sections()) {
		if (section == META_SECTION) {
			continue;
		}
		KnownProject project;
		project.path = section;
		project.display_name = config->get_value(section, DISPLAY_NAME_KEY, String());
		project.last_opened_unix_time = (uint64_t)(int64_t)config->get_value(section, LAST_OPENED_KEY, 0);
		project.last_known_version = config->get_value(section, LAST_VERSION_KEY, String());
		project.tags = config->get_value(section, TAGS_KEY, PackedStringArray());
		project.missing = config->get_value(section, MISSING_KEY, false);
		projects.push_back(project);
	}

	return OK;
}

Error KnownProjectStore::save() const {
	Ref<ConfigFile> config;
	config.instantiate();

	config->set_value(META_SECTION, VERSION_KEY, CURRENT_VERSION);
	if (!auto_open_path.is_empty()) {
		config->set_value(META_SECTION, AUTO_OPEN_KEY, auto_open_path);
	}

	for (const KnownProject &project : projects) {
		config->set_value(project.path, DISPLAY_NAME_KEY, project.display_name);
		config->set_value(project.path, LAST_OPENED_KEY, (int64_t)project.last_opened_unix_time);
		config->set_value(project.path, LAST_VERSION_KEY, project.last_known_version);
		config->set_value(project.path, TAGS_KEY, project.tags);
		config->set_value(project.path, MISSING_KEY, project.missing);
	}

	return config->save(_resolve_path());
}

void KnownProjectStore::add_project(const String &p_path) {
	const String canonical = canonicalize_path(p_path);
	const int index = _ensure_project(canonical);
	_read_project_config(canonical, projects[index]);
}

bool KnownProjectStore::refresh_project(const String &p_path) {
	const int index = find_project(canonicalize_path(p_path));
	if (index == -1) {
		return false;
	}
	return _read_project_config(projects[index].path, projects[index]);
}

void KnownProjectStore::mark_project_opened(const String &p_path, uint64_t p_unix_time) {
	const String canonical = canonicalize_path(p_path);
	const int index = _ensure_project(canonical);
	_read_project_config(canonical, projects[index]);

	uint64_t unix_time = p_unix_time;
	if (unix_time == 0) {
		unix_time = (uint64_t)Time::get_singleton()->get_unix_time_from_system();
	}

	projects[index].last_opened_unix_time = unix_time;
	projects[index].missing = false;
	auto_open_path = canonical;
}

Vector<KnownProjectStore::KnownProject> KnownProjectStore::get_recent_projects() const {
	Vector<KnownProject> result;
	for (const KnownProject &project : projects) {
		result.push_back(project);
	}
	result.sort_custom<RecentSort>();
	return result;
}

Vector<KnownProjectStore::KnownProject> KnownProjectStore::get_known_projects() const {
	Vector<KnownProject> result;
	for (const KnownProject &project : projects) {
		result.push_back(project);
	}
	result.sort_custom<PathSort>();
	return result;
}

bool KnownProjectStore::has_project(const String &p_path) const {
	return find_project(canonicalize_path(p_path)) != -1;
}

bool KnownProjectStore::get_project(const String &p_path, KnownProject &r_project) const {
	const int index = find_project(canonicalize_path(p_path));
	if (index == -1) {
		return false;
	}
	r_project = projects[index];
	return true;
}

void KnownProjectStore::mark_project_missing(const String &p_path) {
	const int index = find_project(canonicalize_path(p_path));
	if (index == -1) {
		return;
	}
	projects[index].missing = true;
}

bool KnownProjectStore::remove_project(const String &p_path) {
	const String canonical = canonicalize_path(p_path);
	const int index = find_project(canonical);
	if (index == -1) {
		return false;
	}
	projects.remove_at(index);
	if (auto_open_path == canonical) {
		auto_open_path = String();
	}
	return true;
}

int KnownProjectStore::remove_missing_projects() {
	int removed = 0;
	uint32_t i = 0;
	while (i < projects.size()) {
		const String conf = projects[i].path.path_join("project.foundry");
		if (!FileAccess::exists(conf)) {
			if (auto_open_path == projects[i].path) {
				auto_open_path = String();
			}
			projects.remove_at(i);
			removed++;
		} else {
			i++;
		}
	}
	return removed;
}

void KnownProjectStore::add_tag(const String &p_path, const String &p_tag) {
	const int index = find_project(canonicalize_path(p_path));
	if (index == -1) {
		return;
	}
	if (projects[index].tags.has(p_tag)) {
		return;
	}
	projects[index].tags.push_back(p_tag);
}

void KnownProjectStore::remove_tag(const String &p_path, const String &p_tag) {
	const int index = find_project(canonicalize_path(p_path));
	if (index == -1) {
		return;
	}
	const int tag_index = projects[index].tags.find(p_tag);
	if (tag_index != -1) {
		projects[index].tags.remove_at(tag_index);
	}
}

KnownProjectStore::KnownProjectStore(const String &p_config_path) {
	config_path = p_config_path;
}
