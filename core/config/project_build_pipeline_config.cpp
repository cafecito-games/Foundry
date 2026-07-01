/**************************************************************************/
/*  project_build_pipeline_config.cpp                                     */
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

#include "project_build_pipeline_config.h"

#include "core/config/foundry_build_task_registry.h"

static const char *BUILD_SECTION = "build";
static const char *PROVIDER_SECTION_PREFIX = "build/providers/";
static const char *TASK_SECTION_PREFIX = "build/tasks/";
static const char *COMMAND_PROVIDER_ID = "command";

bool ProjectBuildPipelineConfig::_is_valid_id(const String &p_id) {
	if (p_id.is_empty()) {
		return false;
	}

	for (int i = 0; i < p_id.length(); i++) {
		const char32_t c = p_id[i];
		const bool valid = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.';
		if (!valid) {
			return false;
		}
	}

	return true;
}

bool ProjectBuildPipelineConfig::_is_valid_project_path_or_glob(const String &p_path) {
	const String path = p_path.strip_edges().replace_char('\\', '/');
	if (path.is_empty() || !path.begins_with("res://")) {
		return false;
	}

	const String rest = path.substr(6);
	if (rest.begins_with("../") || rest.contains("/../") || rest.ends_with("/..") || rest == "..") {
		return false;
	}

	return true;
}

bool ProjectBuildPipelineConfig::_is_valid_project_path(const String &p_path) {
	const String path = p_path.strip_edges().replace_char('\\', '/');
	if (!_is_valid_project_path_or_glob(path)) {
		return false;
	}

	const String rest = path.substr(6);
	return !rest.contains("*") && !rest.contains("?");
}

bool ProjectBuildPipelineConfig::_is_valid_provider_script_path(const String &p_path) {
	const String path = p_path.strip_edges().replace_char('\\', '/').simplify_path();
	if (!_is_valid_project_path_or_glob(path)) {
		return false;
	}

	const String rest = path.substr(6);
	if (rest.is_empty() || rest.ends_with("/") || rest.contains("*") || rest.contains("?")) {
		return false;
	}

	return path.get_extension() == "fs";
}

bool ProjectBuildPipelineConfig::_is_unsafe_output_path(const String &p_path) {
	const String path = p_path.strip_edges().replace_char('\\', '/').simplify_path();
	if (!_is_valid_project_path_or_glob(path)) {
		return true;
	}

	const String rest = path.substr(6).strip_edges();
	if (rest.is_empty() || rest == "." || rest == "/" || rest.begins_with("*")) {
		return true;
	}

	const String rest_lower = rest.to_lower();
	if (rest_lower == ".foundry" || rest_lower.begins_with(".foundry/") || rest_lower == ".godot" || rest_lower.begins_with(".godot/")) {
		return true;
	}

	return false;
}

String ProjectBuildPipelineConfig::_nested_section(const String &p_prefix, const String &p_id) {
	return p_prefix + p_id;
}

void ProjectBuildPipelineConfig::_add_validation_error(Vector<ValidationError> &r_errors, const String &p_section, const String &p_key, const String &p_message) const {
	ValidationError error;
	error.section = p_section;
	error.key = p_key;
	error.message = p_message;
	r_errors.push_back(error);
}

void ProjectBuildPipelineConfig::_add_parse_error(const String &p_section, const String &p_key, const String &p_message) {
	ValidationError error;
	error.section = p_section;
	error.key = p_key;
	error.message = p_message;
	parse_errors.push_back(error);
}

void ProjectBuildPipelineConfig::_clear_parse_errors_for(const String &p_section, const String &p_key) {
	// Drop load-time parse errors for a section (optionally a single key) once the authoring API edits
	// it, so a structured repair is not still reported as invalid by validate(), which seeds from
	// parse_errors.
	for (int i = parse_errors.size() - 1; i >= 0; i--) {
		if (parse_errors[i].section == p_section && (p_key.is_empty() || parse_errors[i].key == p_key)) {
			parse_errors.remove_at(i);
		}
	}
}

void ProjectBuildPipelineConfig::_append_ordered(Vector<String> &r_order, const String &p_name) const {
	if (!r_order.has(p_name)) {
		r_order.push_back(p_name);
	}
}

void ProjectBuildPipelineConfig::_clear_build_sections(const Ref<ConfigFile> &p_config) const {
	Vector<String> sections = p_config->get_sections();
	for (const String &section : sections) {
		if (section == BUILD_SECTION || section.begins_with(PROVIDER_SECTION_PREFIX) || section.begins_with(TASK_SECTION_PREFIX)) {
			p_config->erase_section(section);
		}
	}
}

bool ProjectBuildPipelineConfig::_bool_from_variant(const Variant &p_value, const String &p_section, const String &p_key, bool p_default) {
	if (p_value.get_type() == Variant::BOOL) {
		return p_value;
	}

	_add_parse_error(p_section, p_key, "Expected a bool value.");
	return p_default;
}

int ProjectBuildPipelineConfig::_int_from_variant(const Variant &p_value, const String &p_section, const String &p_key, int p_default, bool *r_has_value) {
	if (p_value.get_type() == Variant::INT) {
		if (r_has_value != nullptr) {
			*r_has_value = true;
		}
		return p_value;
	}

	if (r_has_value != nullptr) {
		*r_has_value = false;
	}
	_add_parse_error(p_section, p_key, "Expected an int value.");
	return p_default;
}

String ProjectBuildPipelineConfig::_string_from_variant(const Variant &p_value, const String &p_section, const String &p_key, const String &p_default) {
	if (p_value.get_type() == Variant::STRING || p_value.get_type() == Variant::STRING_NAME) {
		return p_value;
	}

	_add_parse_error(p_section, p_key, "Expected a string value.");
	return p_default;
}

PackedStringArray ProjectBuildPipelineConfig::_string_array_from_variant(const Variant &p_value, const String &p_section, const String &p_key) {
	if (p_value.get_type() == Variant::PACKED_STRING_ARRAY) {
		return p_value;
	}

	PackedStringArray strings;
	if (p_value.get_type() == Variant::ARRAY) {
		Array array = p_value;
		for (int i = 0; i < array.size(); i++) {
			if (array[i].get_type() != Variant::STRING && array[i].get_type() != Variant::STRING_NAME) {
				_add_parse_error(p_section, p_key, "Expected every array entry to be a string.");
				return PackedStringArray();
			}
			strings.push_back(array[i]);
		}
		return strings;
	}

	_add_parse_error(p_section, p_key, "Expected a PackedStringArray value.");
	return PackedStringArray();
}

Dictionary ProjectBuildPipelineConfig::_dictionary_from_variant(const Variant &p_value, const String &p_section, const String &p_key) {
	if (p_value.get_type() == Variant::DICTIONARY) {
		return p_value;
	}

	_add_parse_error(p_section, p_key, "Expected a Dictionary value.");
	return Dictionary();
}

bool ProjectBuildPipelineConfig::_read_bool(const Ref<ConfigFile> &p_config, const String &p_section, const String &p_key, bool p_default) {
	if (!p_config->has_section_key(p_section, p_key)) {
		return p_default;
	}
	return _bool_from_variant(p_config->get_value(p_section, p_key), p_section, p_key, p_default);
}

int ProjectBuildPipelineConfig::_read_int(const Ref<ConfigFile> &p_config, const String &p_section, const String &p_key, int p_default, bool *r_has_value) {
	if (!p_config->has_section_key(p_section, p_key)) {
		if (r_has_value != nullptr) {
			*r_has_value = false;
		}
		return p_default;
	}
	return _int_from_variant(p_config->get_value(p_section, p_key), p_section, p_key, p_default, r_has_value);
}

String ProjectBuildPipelineConfig::_read_string(const Ref<ConfigFile> &p_config, const String &p_section, const String &p_key, const String &p_default) {
	if (!p_config->has_section_key(p_section, p_key)) {
		return p_default;
	}
	return _string_from_variant(p_config->get_value(p_section, p_key), p_section, p_key, p_default);
}

PackedStringArray ProjectBuildPipelineConfig::_read_string_array(const Ref<ConfigFile> &p_config, const String &p_section, const String &p_key) {
	if (!p_config->has_section_key(p_section, p_key)) {
		return PackedStringArray();
	}
	return _string_array_from_variant(p_config->get_value(p_section, p_key), p_section, p_key);
}

Dictionary ProjectBuildPipelineConfig::_read_dictionary(const Ref<ConfigFile> &p_config, const String &p_section, const String &p_key) {
	if (!p_config->has_section_key(p_section, p_key)) {
		return Dictionary();
	}
	return _dictionary_from_variant(p_config->get_value(p_section, p_key), p_section, p_key);
}

void ProjectBuildPipelineConfig::_set_provider_field(ProviderDescriptor &r_provider, const String &p_field, const Variant &p_value, const String &p_section, const String &p_key) {
	if (p_field == "script") {
		r_provider.script = _string_from_variant(p_value, p_section, p_key);
	} else if (p_field == "class_name") {
		r_provider.class_name = _string_from_variant(p_value, p_section, p_key);
	} else if (p_field == "display_name") {
		r_provider.display_name = _string_from_variant(p_value, p_section, p_key);
	} else if (p_field == "description") {
		r_provider.description = _string_from_variant(p_value, p_section, p_key);
	} else if (p_field == "addon") {
		r_provider.addon = _string_from_variant(p_value, p_section, p_key);
	}
}

void ProjectBuildPipelineConfig::_set_task_field(TaskDefinition &r_task, const String &p_field, const Variant &p_value, const String &p_section, const String &p_key) {
	if (p_field == "provider") {
		r_task.provider = _string_from_variant(p_value, p_section, p_key);
	} else if (p_field == "inputs") {
		r_task.inputs = _string_array_from_variant(p_value, p_section, p_key);
	} else if (p_field == "outputs") {
		r_task.outputs = _string_array_from_variant(p_value, p_section, p_key);
	} else if (p_field == "options") {
		r_task.options = _dictionary_from_variant(p_value, p_section, p_key);
	} else if (p_field == "command") {
		r_task.command = _string_from_variant(p_value, p_section, p_key);
	} else if (p_field == "args") {
		r_task.args = _string_array_from_variant(p_value, p_section, p_key);
	} else if (p_field == "working_directory") {
		r_task.working_directory = _string_from_variant(p_value, p_section, p_key, "res://");
	} else if (p_field == "environment") {
		r_task.environment = _dictionary_from_variant(p_value, p_section, p_key);
	} else if (p_field == "timeout_seconds") {
		r_task.timeout_seconds = _int_from_variant(p_value, p_section, p_key, 60, &r_task.has_timeout_seconds);
	} else if (p_field == "tool_version_command") {
		r_task.tool_version_command = _string_array_from_variant(p_value, p_section, p_key);
	} else if (p_field == "enabled") {
		r_task.enabled = _bool_from_variant(p_value, p_section, p_key, true);
	}
}

void ProjectBuildPipelineConfig::_load_nested_sections(const Ref<ConfigFile> &p_config) {
	Vector<String> sections = p_config->get_sections();
	for (const String &section : sections) {
		if (section.begins_with(PROVIDER_SECTION_PREFIX)) {
			const String id = section.substr(String(PROVIDER_SECTION_PREFIX).length());
			if (id.is_empty()) {
				_add_parse_error(section, "id", "Provider id cannot be empty.");
				continue;
			}

			ProviderDescriptor provider;
			if (providers.has(id)) {
				provider = providers[id];
			} else {
				provider.id = id;
				_append_ordered(provider_order, id);
			}

			Vector<String> keys = p_config->get_section_keys(section);
			for (const String &key : keys) {
				_set_provider_field(provider, key, p_config->get_value(section, key), section, key);
			}
			providers[id] = provider;
		} else if (section.begins_with(TASK_SECTION_PREFIX)) {
			const String name = section.substr(String(TASK_SECTION_PREFIX).length());
			if (name.is_empty()) {
				_add_parse_error(section, "name", "Task name cannot be empty.");
				continue;
			}

			TaskDefinition task;
			if (tasks.has(name)) {
				task = tasks[name];
			} else {
				task.name = name;
				_append_ordered(task_order, name);
			}

			Vector<String> keys = p_config->get_section_keys(section);
			for (const String &key : keys) {
				_set_task_field(task, key, p_config->get_value(section, key), section, key);
			}
			tasks[name] = task;
		}
	}
}

void ProjectBuildPipelineConfig::_load_project_settings_style_keys(const Ref<ConfigFile> &p_config) {
	if (!p_config->has_section(BUILD_SECTION)) {
		return;
	}

	Vector<String> keys = p_config->get_section_keys(BUILD_SECTION);
	for (const String &key : keys) {
		if (key.begins_with("providers/")) {
			const String rest = key.substr(String("providers/").length());
			const int div = rest.find_char('/');
			if (div < 0) {
				_add_parse_error(BUILD_SECTION, key, "Expected provider key format providers/<id>/<field>.");
				continue;
			}

			const String id = rest.substr(0, div);
			const String field = rest.substr(div + 1);
			if (id.is_empty() || field.is_empty()) {
				_add_parse_error(BUILD_SECTION, key, "Expected provider key format providers/<id>/<field>.");
				continue;
			}

			ProviderDescriptor provider;
			if (providers.has(id)) {
				provider = providers[id];
			} else {
				provider.id = id;
				_append_ordered(provider_order, id);
			}

			_set_provider_field(provider, field, p_config->get_value(BUILD_SECTION, key), _nested_section(PROVIDER_SECTION_PREFIX, id), field);
			providers[id] = provider;
		} else if (key.begins_with("tasks/")) {
			const String rest = key.substr(String("tasks/").length());
			const int div = rest.find_char('/');
			if (div < 0) {
				_add_parse_error(BUILD_SECTION, key, "Expected task key format tasks/<name>/<field>.");
				continue;
			}

			const String name = rest.substr(0, div);
			const String field = rest.substr(div + 1);
			if (name.is_empty() || field.is_empty()) {
				_add_parse_error(BUILD_SECTION, key, "Expected task key format tasks/<name>/<field>.");
				continue;
			}

			TaskDefinition task;
			if (tasks.has(name)) {
				task = tasks[name];
			} else {
				task.name = name;
				_append_ordered(task_order, name);
			}

			_set_task_field(task, field, p_config->get_value(BUILD_SECTION, key), _nested_section(TASK_SECTION_PREFIX, name), field);
			tasks[name] = task;
		}
	}
}

void ProjectBuildPipelineConfig::_validate_stage(const PackedStringArray &p_stage_tasks, const String &p_key, HashSet<String> &r_seen_tasks, Vector<ValidationError> &r_errors) const {
	HashSet<String> stage_seen;
	for (int i = 0; i < p_stage_tasks.size(); i++) {
		const String task_name = p_stage_tasks[i];
		if (!_is_valid_id(task_name)) {
			_add_validation_error(r_errors, BUILD_SECTION, p_key, vformat("Task reference '%s' is not a valid task name.", task_name));
			continue;
		}

		if (stage_seen.has(task_name)) {
			_add_validation_error(r_errors, BUILD_SECTION, p_key, vformat("Task '%s' is listed more than once in this stage.", task_name));
			continue;
		}
		if (r_seen_tasks.has(task_name)) {
			_add_validation_error(r_errors, BUILD_SECTION, p_key, vformat("Task '%s' is listed in more than one stage.", task_name));
		}
		stage_seen.insert(task_name);
		r_seen_tasks.insert(task_name);

		if (!tasks.has(task_name)) {
			_add_validation_error(r_errors, BUILD_SECTION, p_key, vformat("Task '%s' is referenced by the stage list but has no build/tasks definition.", task_name));
		}
	}
}

void ProjectBuildPipelineConfig::_write_provider_to_config(const Ref<ConfigFile> &p_config, const ProviderDescriptor &p_provider) const {
	const String section = _nested_section(PROVIDER_SECTION_PREFIX, p_provider.id);
	if (!p_provider.script.is_empty()) {
		p_config->set_value(section, "script", p_provider.script);
	}
	if (!p_provider.class_name.is_empty()) {
		p_config->set_value(section, "class_name", p_provider.class_name);
	}
	if (!p_provider.display_name.is_empty()) {
		p_config->set_value(section, "display_name", p_provider.display_name);
	}
	if (!p_provider.description.is_empty()) {
		p_config->set_value(section, "description", p_provider.description);
	}
	if (!p_provider.addon.is_empty()) {
		p_config->set_value(section, "addon", p_provider.addon);
	}
}

void ProjectBuildPipelineConfig::_write_task_to_config(const Ref<ConfigFile> &p_config, const TaskDefinition &p_task) const {
	const String section = _nested_section(TASK_SECTION_PREFIX, p_task.name);
	if (!p_task.provider.is_empty()) {
		p_config->set_value(section, "provider", p_task.provider);
	}
	if (!p_task.inputs.is_empty()) {
		p_config->set_value(section, "inputs", p_task.inputs);
	}
	if (!p_task.outputs.is_empty()) {
		p_config->set_value(section, "outputs", p_task.outputs);
	}
	if (!p_task.options.is_empty()) {
		p_config->set_value(section, "options", p_task.options);
	}
	if (!p_task.command.is_empty()) {
		p_config->set_value(section, "command", p_task.command);
	}
	if (!p_task.args.is_empty()) {
		p_config->set_value(section, "args", p_task.args);
	}
	if (!p_task.working_directory.is_empty() && p_task.working_directory != "res://") {
		p_config->set_value(section, "working_directory", p_task.working_directory);
	}
	if (!p_task.environment.is_empty()) {
		p_config->set_value(section, "environment", p_task.environment);
	}
	if (p_task.has_timeout_seconds) {
		p_config->set_value(section, "timeout_seconds", p_task.timeout_seconds);
	}
	if (!p_task.tool_version_command.is_empty()) {
		p_config->set_value(section, "tool_version_command", p_task.tool_version_command);
	}
	if (!p_task.enabled) {
		p_config->set_value(section, "enabled", p_task.enabled);
	}
}

void ProjectBuildPipelineConfig::_write_provider_to_custom_map(ProjectSettings::CustomMap &r_custom, const ProviderDescriptor &p_provider) const {
	const String prefix = "build/providers/" + p_provider.id + "/";
	if (!p_provider.script.is_empty()) {
		r_custom[prefix + "script"] = p_provider.script;
	}
	if (!p_provider.class_name.is_empty()) {
		r_custom[prefix + "class_name"] = p_provider.class_name;
	}
	if (!p_provider.display_name.is_empty()) {
		r_custom[prefix + "display_name"] = p_provider.display_name;
	}
	if (!p_provider.description.is_empty()) {
		r_custom[prefix + "description"] = p_provider.description;
	}
	if (!p_provider.addon.is_empty()) {
		r_custom[prefix + "addon"] = p_provider.addon;
	}
}

void ProjectBuildPipelineConfig::_write_task_to_custom_map(ProjectSettings::CustomMap &r_custom, const TaskDefinition &p_task) const {
	const String prefix = "build/tasks/" + p_task.name + "/";
	if (!p_task.provider.is_empty()) {
		r_custom[prefix + "provider"] = p_task.provider;
	}
	if (!p_task.inputs.is_empty()) {
		r_custom[prefix + "inputs"] = p_task.inputs;
	}
	if (!p_task.outputs.is_empty()) {
		r_custom[prefix + "outputs"] = p_task.outputs;
	}
	if (!p_task.options.is_empty()) {
		r_custom[prefix + "options"] = p_task.options;
	}
	if (!p_task.command.is_empty()) {
		r_custom[prefix + "command"] = p_task.command;
	}
	if (!p_task.args.is_empty()) {
		r_custom[prefix + "args"] = p_task.args;
	}
	if (!p_task.working_directory.is_empty() && p_task.working_directory != "res://") {
		r_custom[prefix + "working_directory"] = p_task.working_directory;
	}
	if (!p_task.environment.is_empty()) {
		r_custom[prefix + "environment"] = p_task.environment;
	}
	if (p_task.has_timeout_seconds) {
		r_custom[prefix + "timeout_seconds"] = p_task.timeout_seconds;
	}
	if (!p_task.tool_version_command.is_empty()) {
		r_custom[prefix + "tool_version_command"] = p_task.tool_version_command;
	}
	if (!p_task.enabled) {
		r_custom[prefix + "enabled"] = p_task.enabled;
	}
}

void ProjectBuildPipelineConfig::clear() {
	enabled = false;
	pre_compile_tasks.clear();
	post_compile_tasks.clear();
	providers.clear();
	tasks.clear();
	provider_order.clear();
	task_order.clear();
	parse_errors.clear();
}

Error ProjectBuildPipelineConfig::load_from_config_file(const Ref<ConfigFile> &p_config) {
	ERR_FAIL_COND_V(p_config.is_null(), ERR_INVALID_PARAMETER);

	clear();

	enabled = _read_bool(p_config, BUILD_SECTION, "enabled", false);
	pre_compile_tasks = _read_string_array(p_config, BUILD_SECTION, "pre_compile");
	post_compile_tasks = _read_string_array(p_config, BUILD_SECTION, "post_compile");

	_load_nested_sections(p_config);
	_load_project_settings_style_keys(p_config);

	return OK;
}

void ProjectBuildPipelineConfig::write_to_config_file(const Ref<ConfigFile> &p_config) const {
	ERR_FAIL_COND(p_config.is_null());

	_clear_build_sections(p_config);

	p_config->set_value(BUILD_SECTION, "enabled", enabled);
	if (!pre_compile_tasks.is_empty()) {
		p_config->set_value(BUILD_SECTION, "pre_compile", pre_compile_tasks);
	}
	if (!post_compile_tasks.is_empty()) {
		p_config->set_value(BUILD_SECTION, "post_compile", post_compile_tasks);
	}

	for (const String &id : provider_order) {
		const ProviderDescriptor *provider = providers.getptr(id);
		if (provider != nullptr) {
			_write_provider_to_config(p_config, *provider);
		}
	}

	for (const String &name : task_order) {
		const TaskDefinition *task = tasks.getptr(name);
		if (task != nullptr) {
			_write_task_to_config(p_config, *task);
		}
	}
}

ProjectSettings::CustomMap ProjectBuildPipelineConfig::to_project_settings_custom_map() const {
	ProjectSettings::CustomMap custom;
	custom["build/enabled"] = enabled;
	if (!pre_compile_tasks.is_empty()) {
		custom["build/pre_compile"] = pre_compile_tasks;
	}
	if (!post_compile_tasks.is_empty()) {
		custom["build/post_compile"] = post_compile_tasks;
	}

	for (const String &id : provider_order) {
		const ProviderDescriptor *provider = providers.getptr(id);
		if (provider != nullptr) {
			_write_provider_to_custom_map(custom, *provider);
		}
	}

	for (const String &name : task_order) {
		const TaskDefinition *task = tasks.getptr(name);
		if (task != nullptr) {
			_write_task_to_custom_map(custom, *task);
		}
	}

	return custom;
}

static bool _stage_filter_has_prefixed_id(const String &p_key, const String &p_prefix, const HashSet<String> &p_ids) {
	if (!p_key.begins_with(p_prefix)) {
		return false;
	}

	const String rest = p_key.substr(p_prefix.length());
	const int div = rest.find_char('/');
	const String id = div >= 0 ? rest.substr(0, div) : rest;
	return p_ids.has(id);
}

static bool _stage_filter_has_section_id(const String &p_section, const String &p_prefix, const HashSet<String> &p_ids) {
	if (!p_section.begins_with(p_prefix)) {
		return false;
	}

	return p_ids.has(p_section.substr(p_prefix.length()));
}

static bool _stage_filter_keeps_parse_error(const ProjectBuildPipelineConfig::ValidationError &p_error,
		ProjectBuildPipelineConfig::Stage p_stage, const HashSet<String> &p_stage_tasks,
		const HashSet<String> &p_stage_providers) {
	if (p_error.section == BUILD_SECTION) {
		if (p_error.key == "enabled") {
			return true;
		}
		if (p_error.key == "pre_compile") {
			return p_stage == ProjectBuildPipelineConfig::STAGE_PRE_COMPILE;
		}
		if (p_error.key == "post_compile") {
			return p_stage == ProjectBuildPipelineConfig::STAGE_POST_COMPILE;
		}
		if (_stage_filter_has_prefixed_id(p_error.key, "tasks/", p_stage_tasks)) {
			return true;
		}
		if (_stage_filter_has_prefixed_id(p_error.key, "providers/", p_stage_providers)) {
			return true;
		}
		return false;
	}

	if (_stage_filter_has_section_id(p_error.section, TASK_SECTION_PREFIX, p_stage_tasks)) {
		return true;
	}
	if (_stage_filter_has_section_id(p_error.section, PROVIDER_SECTION_PREFIX, p_stage_providers)) {
		return true;
	}

	return false;
}

ProjectBuildPipelineConfig ProjectBuildPipelineConfig::filtered_for_stage(Stage p_stage) const {
	ProjectBuildPipelineConfig filtered;
	filtered.enabled = enabled;

	const PackedStringArray stage_tasks = get_stage_tasks(p_stage);
	switch (p_stage) {
		case STAGE_PRE_COMPILE:
			filtered.pre_compile_tasks = stage_tasks;
			break;
		case STAGE_POST_COMPILE:
			filtered.post_compile_tasks = stage_tasks;
			break;
	}

	HashSet<String> selected_tasks;
	HashSet<String> selected_providers;
	for (int i = 0; i < stage_tasks.size(); i++) {
		const String task_name = stage_tasks[i];
		selected_tasks.insert(task_name);

		const TaskDefinition *task = tasks.getptr(task_name);
		if (task == nullptr) {
			continue;
		}

		filtered.tasks[task_name] = *task;
		filtered._append_ordered(filtered.task_order, task_name);
		if (!task->provider.is_empty()) {
			selected_providers.insert(task->provider);
		}
	}

	for (const String &provider_id : provider_order) {
		if (!selected_providers.has(provider_id)) {
			continue;
		}

		const ProviderDescriptor *provider = providers.getptr(provider_id);
		if (provider == nullptr) {
			continue;
		}
		filtered.providers[provider_id] = *provider;
		filtered._append_ordered(filtered.provider_order, provider_id);
	}

	for (const ValidationError &error : parse_errors) {
		if (_stage_filter_keeps_parse_error(error, p_stage, selected_tasks, selected_providers)) {
			filtered.parse_errors.push_back(error);
		}
	}

	return filtered;
}

Vector<ProjectBuildPipelineConfig::ValidationError> ProjectBuildPipelineConfig::validate(
		const FoundryBuildTaskRegistry *p_provider_registry) const {
	Vector<ValidationError> errors = parse_errors;

	HashSet<String> seen_stage_tasks;
	_validate_stage(pre_compile_tasks, "pre_compile", seen_stage_tasks, errors);
	_validate_stage(post_compile_tasks, "post_compile", seen_stage_tasks, errors);

	for (const String &id : provider_order) {
		const ProviderDescriptor *provider = providers.getptr(id);
		if (provider == nullptr) {
			continue;
		}

		const String section = _nested_section(PROVIDER_SECTION_PREFIX, id);
		if (!_is_valid_id(id)) {
			_add_validation_error(errors, section, "id", vformat("Provider id '%s' is not valid.", id));
		}
		if (id == COMMAND_PROVIDER_ID) {
			_add_validation_error(errors, section, "id", "The command provider id is reserved for the built-in command provider.");
		}
		if (provider->script.is_empty()) {
			_add_validation_error(errors, section, "script", "Project provider descriptors require a script path.");
		} else if (!_is_valid_provider_script_path(provider->script)) {
			_add_validation_error(errors, section, "script", "Provider script must be a concrete res:// Foundry Script file.");
		}
		if (provider->class_name.is_empty()) {
			_add_validation_error(errors, section, "class_name", "Project provider descriptors require a class_name.");
		}
	}

	for (const String &name : task_order) {
		const TaskDefinition *task = tasks.getptr(name);
		if (task == nullptr) {
			continue;
		}

		const String section = _nested_section(TASK_SECTION_PREFIX, name);
		if (!_is_valid_id(name)) {
			_add_validation_error(errors, section, "name", vformat("Task name '%s' is not valid.", name));
		}
		const bool provider_registered = task->provider == COMMAND_PROVIDER_ID ||
				providers.has(task->provider) ||
				(p_provider_registry != nullptr && p_provider_registry->has_provider(task->provider));
		if (task->provider.is_empty()) {
			_add_validation_error(errors, section, "provider", "Build tasks require a provider id.");
		} else if (!provider_registered) {
			_add_validation_error(errors, section, "provider", vformat("Provider id '%s' is not registered.", task->provider));
		}

		for (int i = 0; i < task->inputs.size(); i++) {
			if (!_is_valid_project_path_or_glob(task->inputs[i])) {
				_add_validation_error(errors, section, "inputs", vformat("Input path '%s' must be a res:// project path or glob.", task->inputs[i]));
				break;
			}
		}

		if (task->outputs.is_empty()) {
			_add_validation_error(errors, section, "outputs", "Build tasks must declare at least one output.");
		}
		for (int i = 0; i < task->outputs.size(); i++) {
			if (_is_unsafe_output_path(task->outputs[i])) {
				_add_validation_error(errors, section, "outputs", vformat("Output path '%s' is not a safe generated output root.", task->outputs[i]));
				break;
			}
		}

		if (task->provider == COMMAND_PROVIDER_ID && task->command.is_empty()) {
			_add_validation_error(errors, section, "command", "Command provider tasks require a command.");
		}
		if (!task->working_directory.is_empty() && !_is_valid_project_path(task->working_directory)) {
			_add_validation_error(errors, section, "working_directory", "Working directory must be a concrete res:// project path.");
		}
		if (task->has_timeout_seconds && task->timeout_seconds <= 0) {
			_add_validation_error(errors, section, "timeout_seconds", "Timeout must be greater than zero seconds.");
		}
	}

	return errors;
}

ProjectBuildPipelineConfig::Status ProjectBuildPipelineConfig::get_status() const {
	if (!enabled || (pre_compile_tasks.is_empty() && post_compile_tasks.is_empty())) {
		return STATUS_DISABLED;
	}

	return STATUS_CONFIGURED;
}

PackedStringArray ProjectBuildPipelineConfig::get_stage_tasks(Stage p_stage) const {
	switch (p_stage) {
		case STAGE_PRE_COMPILE:
			return pre_compile_tasks;
		case STAGE_POST_COMPILE:
			return post_compile_tasks;
	}

	return PackedStringArray();
}

PackedStringArray ProjectBuildPipelineConfig::get_enabled_stage_tasks(Stage p_stage) const {
	PackedStringArray executable_tasks;
	if (!enabled) {
		return executable_tasks;
	}

	const PackedStringArray stage_tasks = get_stage_tasks(p_stage);
	for (int i = 0; i < stage_tasks.size(); i++) {
		const TaskDefinition *task = tasks.getptr(stage_tasks[i]);
		if (task != nullptr && task->enabled) {
			executable_tasks.push_back(stage_tasks[i]);
		}
	}

	return executable_tasks;
}

const ProjectBuildPipelineConfig::ProviderDescriptor *ProjectBuildPipelineConfig::get_provider(const String &p_id) const {
	return providers.getptr(p_id);
}

const ProjectBuildPipelineConfig::TaskDefinition *ProjectBuildPipelineConfig::get_task(const String &p_name) const {
	return tasks.getptr(p_name);
}

static PackedStringArray *_mutable_stage_array(ProjectBuildPipelineConfig::Stage p_stage,
		PackedStringArray *p_pre, PackedStringArray *p_post) {
	switch (p_stage) {
		case ProjectBuildPipelineConfig::STAGE_PRE_COMPILE:
			return p_pre;
		case ProjectBuildPipelineConfig::STAGE_POST_COMPILE:
			return p_post;
	}
	return nullptr;
}

void ProjectBuildPipelineConfig::set_enabled(bool p_enabled) {
	enabled = p_enabled;
	_clear_parse_errors_for(BUILD_SECTION, "enabled");
}

void ProjectBuildPipelineConfig::set_provider(const ProviderDescriptor &p_provider) {
	ERR_FAIL_COND(p_provider.id.is_empty());
	if (!providers.has(p_provider.id)) {
		_append_ordered(provider_order, p_provider.id);
	}
	providers[p_provider.id] = p_provider;
	_clear_parse_errors_for(_nested_section(PROVIDER_SECTION_PREFIX, p_provider.id));
}

bool ProjectBuildPipelineConfig::remove_provider(const String &p_id) {
	if (!providers.has(p_id)) {
		return false;
	}
	providers.erase(p_id);
	const int index = provider_order.find(p_id);
	if (index >= 0) {
		provider_order.remove_at(index);
	}
	_clear_parse_errors_for(_nested_section(PROVIDER_SECTION_PREFIX, p_id));
	return true;
}

void ProjectBuildPipelineConfig::set_task(const TaskDefinition &p_task) {
	ERR_FAIL_COND(p_task.name.is_empty());
	if (!tasks.has(p_task.name)) {
		_append_ordered(task_order, p_task.name);
	}
	tasks[p_task.name] = p_task;
	_clear_parse_errors_for(_nested_section(TASK_SECTION_PREFIX, p_task.name));
}

bool ProjectBuildPipelineConfig::remove_task(const String &p_name) {
	if (!tasks.has(p_name)) {
		return false;
	}
	tasks.erase(p_name);
	const int index = task_order.find(p_name);
	if (index >= 0) {
		task_order.remove_at(index);
	}
	remove_task_from_stage(STAGE_PRE_COMPILE, p_name);
	remove_task_from_stage(STAGE_POST_COMPILE, p_name);
	_clear_parse_errors_for(_nested_section(TASK_SECTION_PREFIX, p_name));
	return true;
}

bool ProjectBuildPipelineConfig::rename_task(const String &p_old_name, const String &p_new_name) {
	if (p_new_name.is_empty() || p_old_name == p_new_name) {
		return false;
	}
	if (!tasks.has(p_old_name) || tasks.has(p_new_name)) {
		return false;
	}

	TaskDefinition task = tasks[p_old_name];
	task.name = p_new_name;
	tasks.erase(p_old_name);
	tasks[p_new_name] = task;
	_clear_parse_errors_for(_nested_section(TASK_SECTION_PREFIX, p_old_name));
	_clear_parse_errors_for(_nested_section(TASK_SECTION_PREFIX, p_new_name));

	const int order_index = task_order.find(p_old_name);
	if (order_index >= 0) {
		task_order.set(order_index, p_new_name);
	}

	// Replace every occurrence: an invalid loaded config can list a task more than once in a stage,
	// and a partial rename would leave dangling references to the old, now-undefined name.
	PackedStringArray *stage_arrays[2] = { &pre_compile_tasks, &post_compile_tasks };
	for (PackedStringArray *stage : stage_arrays) {
		for (int i = 0; i < stage->size(); i++) {
			if ((*stage)[i] == p_old_name) {
				stage->set(i, p_new_name);
			}
		}
	}
	return true;
}

String ProjectBuildPipelineConfig::make_unique_task_name(const String &p_base) const {
	if (!tasks.has(p_base)) {
		return p_base;
	}
	int suffix = 2;
	String candidate = p_base + "_" + itos(suffix);
	while (tasks.has(candidate)) {
		suffix++;
		candidate = p_base + "_" + itos(suffix);
	}
	return candidate;
}

String ProjectBuildPipelineConfig::duplicate_task(const String &p_name) {
	const TaskDefinition *original = tasks.getptr(p_name);
	if (original == nullptr) {
		return String();
	}

	TaskDefinition copy = *original;
	copy.name = make_unique_task_name(p_name + "_copy");
	tasks[copy.name] = copy;

	const int index = task_order.find(p_name);
	if (index >= 0) {
		task_order.insert(index + 1, copy.name);
	} else {
		task_order.push_back(copy.name);
	}
	return copy.name;
}

bool ProjectBuildPipelineConfig::add_task_to_stage(Stage p_stage, const String &p_name) {
	PackedStringArray *stage = _mutable_stage_array(p_stage, &pre_compile_tasks, &post_compile_tasks);
	if (stage == nullptr || stage->has(p_name)) {
		return false;
	}
	stage->push_back(p_name);
	_clear_parse_errors_for(BUILD_SECTION, p_stage == STAGE_PRE_COMPILE ? "pre_compile" : "post_compile");
	return true;
}

bool ProjectBuildPipelineConfig::remove_task_from_stage(Stage p_stage, const String &p_name) {
	PackedStringArray *stage = _mutable_stage_array(p_stage, &pre_compile_tasks, &post_compile_tasks);
	if (stage == nullptr) {
		return false;
	}
	bool removed = false;
	int index = stage->find(p_name);
	while (index >= 0) {
		stage->remove_at(index);
		removed = true;
		index = stage->find(p_name);
	}
	if (removed) {
		_clear_parse_errors_for(BUILD_SECTION, p_stage == STAGE_PRE_COMPILE ? "pre_compile" : "post_compile");
	}
	return removed;
}

bool ProjectBuildPipelineConfig::move_stage_task(Stage p_stage, int p_from_index, int p_to_index) {
	PackedStringArray *stage = _mutable_stage_array(p_stage, &pre_compile_tasks, &post_compile_tasks);
	if (stage == nullptr) {
		return false;
	}
	const int count = stage->size();
	if (p_from_index < 0 || p_from_index >= count || p_to_index < 0 || p_to_index >= count) {
		return false;
	}
	if (p_from_index == p_to_index) {
		return true;
	}
	const String name = (*stage)[p_from_index];
	stage->remove_at(p_from_index);
	stage->insert(p_to_index, name);
	_clear_parse_errors_for(BUILD_SECTION, p_stage == STAGE_PRE_COMPILE ? "pre_compile" : "post_compile");
	return true;
}

void ProjectBuildPipelineConfig::set_stage_tasks(Stage p_stage, const PackedStringArray &p_tasks) {
	PackedStringArray *stage = _mutable_stage_array(p_stage, &pre_compile_tasks, &post_compile_tasks);
	if (stage == nullptr) {
		return;
	}
	*stage = p_tasks;
	_clear_parse_errors_for(BUILD_SECTION, p_stage == STAGE_PRE_COMPILE ? "pre_compile" : "post_compile");
}

bool ProjectBuildPipelineConfig::set_task_enabled(const String &p_name, bool p_enabled) {
	TaskDefinition *task = tasks.getptr(p_name);
	if (task == nullptr) {
		return false;
	}
	task->enabled = p_enabled;
	_clear_parse_errors_for(_nested_section(TASK_SECTION_PREFIX, p_name), "enabled");
	return true;
}

String ProjectBuildPipelineConfig::generate_preview() const {
	Ref<ConfigFile> config;
	config.instantiate();
	write_to_config_file(config);
	return config->encode_to_text();
}
