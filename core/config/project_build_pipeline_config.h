/**************************************************************************/
/*  project_build_pipeline_config.h                                       */
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

#include "core/config/project_settings.h"
#include "core/io/config_file.h"
#include "core/templates/hash_map.h"
#include "core/templates/hash_set.h"

class FoundryBuildTaskRegistry;

class ProjectBuildPipelineConfig {
public:
	enum Stage {
		STAGE_PRE_COMPILE,
		STAGE_POST_COMPILE,
	};

	enum Status {
		STATUS_DISABLED,
		STATUS_CONFIGURED,
	};

	struct ValidationError {
		String section;
		String key;
		String message;
	};

	struct ProviderDescriptor {
		String id;
		String script;
		String class_name;
		String display_name;
		String description;
		String addon;
	};

	struct TaskDefinition {
		String name;
		String provider;
		PackedStringArray inputs;
		PackedStringArray outputs;
		Dictionary options;
		String command;
		PackedStringArray args;
		String working_directory = "res://";
		Dictionary environment;
		int timeout_seconds = 60;
		bool has_timeout_seconds = false;
		PackedStringArray tool_version_command;
		bool enabled = true;
	};

private:
	bool enabled = false;
	PackedStringArray pre_compile_tasks;
	PackedStringArray post_compile_tasks;
	HashMap<String, ProviderDescriptor> providers;
	HashMap<String, TaskDefinition> tasks;
	Vector<String> provider_order;
	Vector<String> task_order;
	Vector<ValidationError> parse_errors;

	static bool _is_valid_id(const String &p_id);
	static bool _is_valid_project_path_or_glob(const String &p_path);
	static bool _is_valid_project_path(const String &p_path);
	static bool _is_valid_provider_script_path(const String &p_path);
	static bool _is_unsafe_output_path(const String &p_path);
	static String _nested_section(const String &p_prefix, const String &p_id);

	void _add_validation_error(Vector<ValidationError> &r_errors, const String &p_section, const String &p_key, const String &p_message) const;
	void _add_parse_error(const String &p_section, const String &p_key, const String &p_message);
	void _append_ordered(Vector<String> &r_order, const String &p_name) const;
	void _clear_build_sections(const Ref<ConfigFile> &p_config) const;

	bool _read_bool(const Ref<ConfigFile> &p_config, const String &p_section, const String &p_key, bool p_default);
	int _read_int(const Ref<ConfigFile> &p_config, const String &p_section, const String &p_key, int p_default, bool *r_has_value = nullptr);
	String _read_string(const Ref<ConfigFile> &p_config, const String &p_section, const String &p_key, const String &p_default = String());
	PackedStringArray _read_string_array(const Ref<ConfigFile> &p_config, const String &p_section, const String &p_key);
	Dictionary _read_dictionary(const Ref<ConfigFile> &p_config, const String &p_section, const String &p_key);

	bool _bool_from_variant(const Variant &p_value, const String &p_section, const String &p_key, bool p_default);
	int _int_from_variant(const Variant &p_value, const String &p_section, const String &p_key, int p_default, bool *r_has_value = nullptr);
	String _string_from_variant(const Variant &p_value, const String &p_section, const String &p_key, const String &p_default = String());
	PackedStringArray _string_array_from_variant(const Variant &p_value, const String &p_section, const String &p_key);
	Dictionary _dictionary_from_variant(const Variant &p_value, const String &p_section, const String &p_key);

	void _set_provider_field(ProviderDescriptor &r_provider, const String &p_field, const Variant &p_value, const String &p_section, const String &p_key);
	void _set_task_field(TaskDefinition &r_task, const String &p_field, const Variant &p_value, const String &p_section, const String &p_key);
	void _load_nested_sections(const Ref<ConfigFile> &p_config);
	void _load_project_settings_style_keys(const Ref<ConfigFile> &p_config);
	void _validate_stage(const PackedStringArray &p_stage_tasks, const String &p_key, HashSet<String> &r_seen_tasks, Vector<ValidationError> &r_errors) const;
	void _write_provider_to_config(const Ref<ConfigFile> &p_config, const ProviderDescriptor &p_provider) const;
	void _write_task_to_config(const Ref<ConfigFile> &p_config, const TaskDefinition &p_task) const;
	void _write_provider_to_custom_map(ProjectSettings::CustomMap &r_custom, const ProviderDescriptor &p_provider) const;
	void _write_task_to_custom_map(ProjectSettings::CustomMap &r_custom, const TaskDefinition &p_task) const;

public:
	void clear();

	Error load_from_config_file(const Ref<ConfigFile> &p_config);
	void write_to_config_file(const Ref<ConfigFile> &p_config) const;
	ProjectSettings::CustomMap to_project_settings_custom_map() const;

	ProjectBuildPipelineConfig filtered_for_stage(Stage p_stage) const;
	Vector<ValidationError> validate(const FoundryBuildTaskRegistry *p_provider_registry = nullptr) const;
	Status get_status() const;

	bool is_enabled() const { return enabled; }
	void set_enabled(bool p_enabled) { enabled = p_enabled; }

	PackedStringArray get_stage_tasks(Stage p_stage) const;
	PackedStringArray get_enabled_stage_tasks(Stage p_stage) const;

	const ProviderDescriptor *get_provider(const String &p_id) const;
	const TaskDefinition *get_task(const String &p_name) const;

	// Authoring API used by the build configuration editor. These mutate the in-memory model; callers
	// persist changes with write_to_config_file()/to_project_settings_custom_map() and preview them with
	// generate_preview().
	void set_provider(const ProviderDescriptor &p_provider);
	bool remove_provider(const String &p_id);

	void set_task(const TaskDefinition &p_task);
	bool remove_task(const String &p_name);
	bool rename_task(const String &p_old_name, const String &p_new_name);
	String duplicate_task(const String &p_name);
	String make_unique_task_name(const String &p_base) const;

	bool add_task_to_stage(Stage p_stage, const String &p_name);
	bool remove_task_from_stage(Stage p_stage, const String &p_name);
	bool move_stage_task(Stage p_stage, int p_from_index, int p_to_index);
	void set_stage_tasks(Stage p_stage, const PackedStringArray &p_tasks);
	bool set_task_enabled(const String &p_name, bool p_enabled);

	String generate_preview() const;

	const HashMap<String, ProviderDescriptor> &get_providers() const { return providers; }
	const HashMap<String, TaskDefinition> &get_tasks() const { return tasks; }
	const Vector<String> &get_provider_order() const { return provider_order; }
	const Vector<String> &get_task_order() const { return task_order; }
};
