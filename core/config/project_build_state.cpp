/**************************************************************************/
/*  project_build_state.cpp                                               */
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

#include "project_build_state.h"

#include "core/config/project_settings.h"
#include "core/io/config_file.h"
#include "core/io/dir_access.h"
#include "core/io/file_access.h"

namespace {

static const char *BUILD_STATE_SECTION = "build_state";
static const char *BUILD_STATE_TASK_SECTION_PREFIX = "build_state/tasks/";
static const char *BUILD_STATE_AUDIT_SECTION_PREFIX = "build_state/audit/";
static const int BUILD_STATE_VERSION = 1;

static bool _has_glob_wildcard(const String &p_path) {
	return p_path.contains("*") || p_path.contains("?");
}

static String _globalize_project_path(const String &p_path) {
	const String path = p_path.replace_char('\\', '/');
	if (path.begins_with("res://")) {
		return ProjectSettings::get_singleton()->globalize_path(path).simplify_path();
	}
	return path.simplify_path();
}

static String _glob_search_root(const String &p_pattern) {
	const int star = p_pattern.find_char('*');
	const int question = p_pattern.find_char('?');
	int wildcard = -1;
	if (star >= 0 && question >= 0) {
		wildcard = MIN(star, question);
	} else {
		wildcard = MAX(star, question);
	}

	if (wildcard < 0) {
		return p_pattern.get_base_dir();
	}

	String prefix = p_pattern.substr(0, wildcard);
	if (prefix.ends_with("/") && prefix.length() > 1) {
		if (prefix.ends_with("://")) {
			return prefix;
		}
		const String stripped_prefix = prefix.substr(0, prefix.length() - 1);
		if (!stripped_prefix.ends_with(":")) {
			return stripped_prefix;
		}
	}
	String root = prefix.get_base_dir();
	if (root == "res:") {
		root = "res://";
	}
	return root.is_empty() ? "." : root;
}

static bool _glob_match_path(const String &p_path, const String &p_pattern, int p_path_index, int p_pattern_index) {
	const int path_length = p_path.length();
	const int pattern_length = p_pattern.length();
	if (p_pattern_index == pattern_length) {
		return p_path_index == path_length;
	}

	const char32_t pattern_char = p_pattern[p_pattern_index];
	if (pattern_char == '*') {
		const bool globstar = p_pattern_index + 1 < pattern_length && p_pattern[p_pattern_index + 1] == '*';
		if (globstar) {
			const int next_pattern_index = p_pattern_index + 2;
			if (next_pattern_index < pattern_length && p_pattern[next_pattern_index] == '/') {
				if (_glob_match_path(p_path, p_pattern, p_path_index, next_pattern_index + 1)) {
					return true;
				}
				for (int i = p_path_index; i < path_length; i++) {
					if (p_path[i] == '/' && _glob_match_path(p_path, p_pattern, i + 1, next_pattern_index + 1)) {
						return true;
					}
				}
				return false;
			}
			for (int i = p_path_index; i <= path_length; i++) {
				if (_glob_match_path(p_path, p_pattern, i, next_pattern_index)) {
					return true;
				}
			}
			return false;
		}

		for (int i = p_path_index; i <= path_length; i++) {
			if (_glob_match_path(p_path, p_pattern, i, p_pattern_index + 1)) {
				return true;
			}
			if (i == path_length || p_path[i] == '/') {
				break;
			}
		}
		return false;
	}

	if (p_path_index == path_length) {
		return false;
	}
	if (pattern_char == '?') {
		return p_path[p_path_index] != '/' && _glob_match_path(p_path, p_pattern, p_path_index + 1, p_pattern_index + 1);
	}
	return pattern_char == p_path[p_path_index] && _glob_match_path(p_path, p_pattern, p_path_index + 1, p_pattern_index + 1);
}

static bool _path_matches_output_pattern(const String &p_path, const String &p_pattern) {
	return _glob_match_path(p_path.replace_char('\\', '/'), p_pattern.replace_char('\\', '/'), 0, 0);
}

static bool _path_is_link(const String &p_path) {
	Ref<DirAccess> dir = DirAccess::create_for_path(p_path);
	return dir.is_valid() && dir->is_link(p_path);
}

static bool _path_has_link_in_chain(const String &p_path) {
	String path = p_path.replace_char('\\', '/').simplify_path();
	String stop_path;
	if (ProjectSettings::get_singleton() != nullptr) {
		stop_path = ProjectSettings::get_singleton()->get_resource_path().replace_char('\\', '/').simplify_path();
		if (stop_path.is_empty() || (path != stop_path && !path.begins_with(stop_path + "/"))) {
			stop_path = String();
		}
	}

	while (!path.is_empty()) {
		if (!stop_path.is_empty() && path == stop_path) {
			return false;
		}
		if (_path_is_link(path)) {
			return true;
		}

		const String parent = path.get_base_dir();
		if (parent == path || parent.is_empty() || parent == "." || parent.ends_with("://")) {
			break;
		}
		path = parent;
	}
	return false;
}

static bool _append_file_manifest_entry(const String &p_path, PackedStringArray &r_manifest) {
	if (_path_has_link_in_chain(p_path)) {
		return false;
	}

	const String sha256 = FileAccess::get_sha256(p_path);
	if (sha256.is_empty()) {
		return false;
	}

	r_manifest.push_back("file:" + p_path + ":" + sha256);
	return true;
}

static bool _append_dir_manifest_entry(const String &p_path, PackedStringArray &r_manifest) {
	if (_path_has_link_in_chain(p_path)) {
		return false;
	}

	r_manifest.push_back("dir:" + p_path);
	return true;
}

static bool _collect_directory_manifest(const String &p_root, PackedStringArray &r_manifest) {
	Ref<DirAccess> dir = DirAccess::open(p_root);
	if (dir.is_null()) {
		return false;
	}

	if (!_append_dir_manifest_entry(p_root.simplify_path(), r_manifest)) {
		return false;
	}

	dir->set_include_hidden(true);
	if (dir->list_dir_begin() != OK) {
		return false;
	}

	bool ok = true;
	for (String entry = dir->get_next(); !entry.is_empty(); entry = dir->get_next()) {
		if (entry == "." || entry == "..") {
			continue;
		}

		const String child = p_root.path_join(entry).simplify_path();
		if (dir->is_link(child)) {
			ok = false;
		} else if (dir->current_is_dir()) {
			ok = _collect_directory_manifest(child, r_manifest) && ok;
		} else if (FileAccess::exists(child)) {
			ok = _append_file_manifest_entry(child, r_manifest) && ok;
		}
	}
	dir->list_dir_end();
	return ok;
}

static bool _collect_matching_output_files(const String &p_root, const String &p_pattern, PackedStringArray &r_files) {
	if (_path_has_link_in_chain(p_root)) {
		return false;
	}

	Ref<DirAccess> dir = DirAccess::open(p_root);
	if (dir.is_null()) {
		return false;
	}
	dir->set_include_hidden(true);
	if (dir->list_dir_begin() != OK) {
		return false;
	}

	bool ok = true;
	for (String entry = dir->get_next(); !entry.is_empty(); entry = dir->get_next()) {
		if (entry == "." || entry == "..") {
			continue;
		}

		const String child = p_root.path_join(entry).simplify_path();
		if (dir->is_link(child)) {
			if (_path_matches_output_pattern(child, p_pattern)) {
				ok = false;
			}
		} else if (dir->current_is_dir()) {
			ok = _collect_matching_output_files(child, p_pattern, r_files) && ok;
		} else if (_path_matches_output_pattern(child, p_pattern)) {
			r_files.push_back(child);
		}
	}
	dir->list_dir_end();
	return ok;
}

static bool _manifest_equals(const PackedStringArray &p_left, const PackedStringArray &p_right) {
	if (p_left.size() != p_right.size()) {
		return false;
	}
	for (int i = 0; i < p_left.size(); i++) {
		if (p_left[i] != p_right[i]) {
			return false;
		}
	}
	return true;
}

static ProjectBuildState::DirtyStatus _dirty(ProjectBuildState::DirtyReason p_reason, const String &p_message) {
	ProjectBuildState::DirtyStatus status;
	status.dirty = true;
	status.reason = p_reason;
	status.message = p_message;
	return status;
}

} // namespace

String ProjectBuildState::_default_state_path() {
	return ProjectSettings::get_singleton()->get_project_data_path().path_join("build_state.cfg");
}

String ProjectBuildState::_task_section(const String &p_task_name) {
	return String(BUILD_STATE_TASK_SECTION_PREFIX) + p_task_name;
}

bool ProjectBuildState::_is_task_section(const String &p_section) {
	return p_section.begins_with(BUILD_STATE_TASK_SECTION_PREFIX) &&
			p_section.length() > String(BUILD_STATE_TASK_SECTION_PREFIX).length();
}

String ProjectBuildState::_task_name_from_section(const String &p_section) {
	return p_section.substr(String(BUILD_STATE_TASK_SECTION_PREFIX).length());
}

String ProjectBuildState::_audit_section(int p_index) {
	return String(BUILD_STATE_AUDIT_SECTION_PREFIX) + String::num_int64(p_index).pad_zeros(4);
}

bool ProjectBuildState::_is_audit_section(const String &p_section) {
	return p_section.begins_with(BUILD_STATE_AUDIT_SECTION_PREFIX) &&
			p_section.length() > String(BUILD_STATE_AUDIT_SECTION_PREFIX).length();
}

int ProjectBuildState::_audit_index_from_section(const String &p_section) {
	return p_section.substr(String(BUILD_STATE_AUDIT_SECTION_PREFIX).length()).to_int();
}

PackedStringArray ProjectBuildState::_compute_output_manifest(const PackedStringArray &p_outputs, bool *r_missing_output) {
	if (r_missing_output != nullptr) {
		*r_missing_output = false;
	}

	PackedStringArray manifest;
	for (int i = 0; i < p_outputs.size(); i++) {
		const String output = _globalize_project_path(p_outputs[i]);
		if (_has_glob_wildcard(output)) {
			PackedStringArray matches;
			const bool glob_readable = _collect_matching_output_files(_glob_search_root(output), output, matches);
			matches.sort();
			if ((!glob_readable || matches.is_empty()) && r_missing_output != nullptr) {
				*r_missing_output = true;
			}
			for (int j = 0; j < matches.size(); j++) {
				if (!_append_file_manifest_entry(matches[j], manifest) && r_missing_output != nullptr) {
					*r_missing_output = true;
				}
			}
		} else if (FileAccess::exists(output)) {
			if (!_append_file_manifest_entry(output, manifest) && r_missing_output != nullptr) {
				*r_missing_output = true;
			}
		} else if (DirAccess::dir_exists_absolute(output)) {
			if (!_collect_directory_manifest(output, manifest) && r_missing_output != nullptr) {
				*r_missing_output = true;
			}
		} else if (r_missing_output != nullptr) {
			*r_missing_output = true;
		}
	}

	manifest.sort();
	return manifest;
}

ProjectBuildState::ProjectBuildState() :
		state_path(_default_state_path()) {
}

ProjectBuildState::ProjectBuildState(const String &p_state_path) :
		state_path(p_state_path) {
}

void ProjectBuildState::clear() {
	records.clear();
	audit_log.clear();
	load_error = OK;
}

Error ProjectBuildState::load() {
	records.clear();
	audit_log.clear();
	load_error = OK;

	if (!FileAccess::exists(state_path)) {
		return OK;
	}

	Ref<ConfigFile> config;
	config.instantiate();
	const Error err = config->load(state_path);
	if (err != OK) {
		load_error = err;
		return err;
	}

	if (!config->has_section_key(BUILD_STATE_SECTION, "version") ||
			config->get_value(BUILD_STATE_SECTION, "version").get_type() != Variant::INT ||
			int(config->get_value(BUILD_STATE_SECTION, "version")) != BUILD_STATE_VERSION) {
		load_error = ERR_FILE_CORRUPT;
		return load_error;
	}

	Vector<String> sections = config->get_sections();
	HashMap<int, TaskRunAudit> loaded_audit;
	for (const String &section : sections) {
		if (_is_task_section(section)) {
			if (!config->has_section_key(section, "fingerprint") ||
					!config->has_section_key(section, "success") ||
					!config->has_section_key(section, "output_manifest")) {
				load_error = ERR_FILE_CORRUPT;
				records.clear();
				audit_log.clear();
				return load_error;
			}

			const Variant fingerprint_value = config->get_value(section, "fingerprint");
			const Variant success_value = config->get_value(section, "success");
			const Variant manifest_value = config->get_value(section, "output_manifest");
			if ((fingerprint_value.get_type() != Variant::STRING && fingerprint_value.get_type() != Variant::STRING_NAME) ||
					success_value.get_type() != Variant::BOOL ||
					manifest_value.get_type() != Variant::PACKED_STRING_ARRAY) {
				load_error = ERR_FILE_CORRUPT;
				records.clear();
				audit_log.clear();
				return load_error;
			}

			TaskRecord record;
			record.task_name = _task_name_from_section(section);
			record.fingerprint = fingerprint_value;
			record.success = success_value;
			record.output_manifest = manifest_value;
			records[record.task_name] = record;
			continue;
		}

		if (!_is_audit_section(section)) {
			continue;
		}

		if (!config->has_section_key(section, "task_name") ||
				!config->has_section_key(section, "fingerprint") ||
				!config->has_section_key(section, "success") ||
				!config->has_section_key(section, "output_manifest") ||
				!config->has_section_key(section, "dirty_reason") ||
				!config->has_section_key(section, "dirty_message") ||
				!config->has_section_key(section, "run_mode")) {
			load_error = ERR_FILE_CORRUPT;
			records.clear();
			audit_log.clear();
			return load_error;
		}

		const Variant task_name_value = config->get_value(section, "task_name");
		const Variant fingerprint_value = config->get_value(section, "fingerprint");
		const Variant success_value = config->get_value(section, "success");
		const Variant manifest_value = config->get_value(section, "output_manifest");
		const Variant dirty_reason_value = config->get_value(section, "dirty_reason");
		const Variant dirty_message_value = config->get_value(section, "dirty_message");
		const Variant run_mode_value = config->get_value(section, "run_mode");
		if ((task_name_value.get_type() != Variant::STRING && task_name_value.get_type() != Variant::STRING_NAME) ||
				(fingerprint_value.get_type() != Variant::STRING && fingerprint_value.get_type() != Variant::STRING_NAME) ||
				success_value.get_type() != Variant::BOOL ||
				manifest_value.get_type() != Variant::PACKED_STRING_ARRAY ||
				dirty_reason_value.get_type() != Variant::INT ||
				(dirty_message_value.get_type() != Variant::STRING && dirty_message_value.get_type() != Variant::STRING_NAME) ||
				run_mode_value.get_type() != Variant::INT) {
			load_error = ERR_FILE_CORRUPT;
			records.clear();
			audit_log.clear();
			return load_error;
		}

		TaskRunAudit audit;
		audit.task_name = task_name_value;
		audit.fingerprint = fingerprint_value;
		audit.success = success_value;
		audit.output_manifest = manifest_value;
		audit.dirty_reason = DirtyReason(int(dirty_reason_value));
		audit.dirty_message = dirty_message_value;
		audit.run_mode = RunMode(int(run_mode_value));
		loaded_audit[_audit_index_from_section(section)] = audit;
	}

	Vector<int> audit_indices;
	for (const KeyValue<int, TaskRunAudit> &E : loaded_audit) {
		audit_indices.push_back(E.key);
	}
	audit_indices.sort();
	for (int index : audit_indices) {
		audit_log.push_back(loaded_audit[index]);
	}
	_trim_audit_log();

	return OK;
}

Error ProjectBuildState::save() const {
	const Error dir_err = DirAccess::make_dir_recursive_absolute(state_path.get_base_dir());
	if (dir_err != OK) {
		return dir_err;
	}

	Ref<ConfigFile> config;
	config.instantiate();
	config->set_value(BUILD_STATE_SECTION, "version", BUILD_STATE_VERSION);

	for (const KeyValue<String, TaskRecord> &E : records) {
		const TaskRecord &record = E.value;
		const String section = _task_section(record.task_name);
		config->set_value(section, "fingerprint", record.fingerprint);
		config->set_value(section, "success", record.success);
		config->set_value(section, "output_manifest", record.output_manifest);
	}

	for (int i = 0; i < audit_log.size(); i++) {
		const TaskRunAudit &audit = audit_log[i];
		const String section = _audit_section(i);
		config->set_value(section, "task_name", audit.task_name);
		config->set_value(section, "fingerprint", audit.fingerprint);
		config->set_value(section, "success", audit.success);
		config->set_value(section, "output_manifest", audit.output_manifest);
		config->set_value(section, "dirty_reason", audit.dirty_reason);
		config->set_value(section, "dirty_message", audit.dirty_message);
		config->set_value(section, "run_mode", audit.run_mode);
	}

	return config->save(state_path);
}

void ProjectBuildState::record_task_result(const String &p_task_name, const String &p_fingerprint,
		const PackedStringArray &p_outputs, bool p_success) {
	load_error = OK;

	TaskRecord record;
	record.task_name = p_task_name;
	record.fingerprint = p_fingerprint;
	record.output_manifest = _compute_output_manifest(p_outputs);
	record.success = p_success;
	records[p_task_name] = record;
}

void ProjectBuildState::record_task_run(const String &p_task_name, const String &p_fingerprint,
		const PackedStringArray &p_outputs, bool p_success, const DirtyStatus &p_dirty_status,
		RunMode p_run_mode) {
	record_task_result(p_task_name, p_fingerprint, p_outputs, p_success);

	TaskRunAudit audit;
	audit.task_name = p_task_name;
	audit.fingerprint = p_fingerprint;
	audit.output_manifest = records[p_task_name].output_manifest;
	audit.success = p_success;
	audit.dirty_reason = p_dirty_status.reason;
	audit.dirty_message = p_dirty_status.message;
	audit.run_mode = p_run_mode;
	audit_log.push_back(audit);
	_trim_audit_log();
}

bool ProjectBuildState::has_task_record(const String &p_task_name) const {
	return records.has(p_task_name);
}

void ProjectBuildState::_trim_audit_log() {
	while (audit_log.size() > MAX_TASK_RUN_AUDIT_ENTRIES) {
		audit_log.remove_at(0);
	}
}

const ProjectBuildState::TaskRecord *ProjectBuildState::get_task_record(const String &p_task_name) const {
	return records.getptr(p_task_name);
}

ProjectBuildState::DirtyStatus ProjectBuildState::get_task_dirty_status(const String &p_task_name,
		const String &p_current_fingerprint, const PackedStringArray &p_outputs, bool p_provider_forced) const {
	if (load_error != OK) {
		return _dirty(DIRTY_UNREADABLE_STATE, "Persisted build state could not be read.");
	}
	if (p_provider_forced) {
		return _dirty(DIRTY_PROVIDER_FORCED, "Provider requested a forced rerun.");
	}

	const TaskRecord *record = get_task_record(p_task_name);
	if (record == nullptr) {
		return _dirty(DIRTY_MISSING_STATE, "No persisted build state exists for this task.");
	}
	if (!record->success) {
		return _dirty(DIRTY_PREVIOUS_FAILURE, "The previous task run failed.");
	}
	if (record->fingerprint != p_current_fingerprint) {
		return _dirty(DIRTY_FINGERPRINT_CHANGED, "The current task fingerprint differs from persisted state.");
	}

	bool missing_output = false;
	const PackedStringArray current_manifest = _compute_output_manifest(p_outputs, &missing_output);
	if (missing_output) {
		return _dirty(DIRTY_OUTPUT_MISSING, "A declared task output is missing.");
	}
	if (!_manifest_equals(record->output_manifest, current_manifest)) {
		return _dirty(DIRTY_OUTPUT_CHANGED, "The current task output manifest differs from persisted state.");
	}

	DirtyStatus status;
	status.dirty = false;
	status.reason = DIRTY_NONE;
	return status;
}
