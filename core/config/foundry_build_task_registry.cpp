/**************************************************************************/
/*  foundry_build_task_registry.cpp                                       */
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

#include "foundry_build_task_registry.h"

static const char *BUILD_TASKS_SECTION = "build_tasks";
static const char *BUILD_TASKS_PROVIDER_SECTION_PREFIX = "build_tasks/";
static const char *PROJECT_PROVIDER_SECTION_PREFIX = "build/providers/";
static const char *PROJECT_TASK_SECTION_PREFIX = "build/tasks/";
static const char *COMMAND_PROVIDER_ID = "command";

bool FoundryBuildTaskRegistry::_is_valid_id(const String &p_id) {
	if (p_id.is_empty()) {
		return false;
	}

	for (int i = 0; i < p_id.length(); i++) {
		const char32_t c = p_id[i];
		const bool valid = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
				(c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.';
		if (!valid) {
			return false;
		}
	}

	return true;
}

bool FoundryBuildTaskRegistry::_is_valid_project_path_or_glob(const String &p_path) {
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

bool FoundryBuildTaskRegistry::_is_valid_provider_script_path(const String &p_path) {
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

String FoundryBuildTaskRegistry::_source_description(const SourceMetadata &p_source) {
	String description;
	switch (p_source.type) {
		case SOURCE_NATIVE:
			description = "native provider";
			break;
		case SOURCE_ADDON:
			description = "addon provider";
			break;
		case SOURCE_PROJECT:
			description = "project provider";
			break;
	}

	if (!p_source.identifier.is_empty()) {
		description += vformat(" '%s'", p_source.identifier);
	}
	if (!p_source.path.is_empty()) {
		description += vformat(" at '%s'", p_source.path);
	}
	if (!p_source.section.is_empty()) {
		description += vformat(" [%s]", p_source.section);
	}

	return description;
}

FoundryBuildTaskRegistry::SourceMetadata FoundryBuildTaskRegistry::_native_source(
		const String &p_identifier, const String &p_provider_id) {
	SourceMetadata source;
	source.type = SOURCE_NATIVE;
	source.identifier = p_identifier;
	source.section = "native/" + p_provider_id;
	return source;
}

FoundryBuildTaskRegistry::SourceMetadata FoundryBuildTaskRegistry::_addon_source(
		const String &p_addon, const String &p_config_path, const String &p_section, const String &p_key) {
	SourceMetadata source;
	source.type = SOURCE_ADDON;
	source.identifier = p_addon;
	source.path = p_config_path;
	source.section = p_section;
	source.key = p_key;
	return source;
}

FoundryBuildTaskRegistry::SourceMetadata FoundryBuildTaskRegistry::_project_source(
		const String &p_config_path, const String &p_section, const String &p_key) {
	SourceMetadata source;
	source.type = SOURCE_PROJECT;
	source.identifier = "project";
	source.path = p_config_path;
	source.section = p_section;
	source.key = p_key;
	return source;
}

String FoundryBuildTaskRegistry::_addon_identifier_from_path(const String &p_addon_or_config_path) {
	if (p_addon_or_config_path.begins_with("res://")) {
		const String base_dir = p_addon_or_config_path.ends_with("plugin.cfg") ? p_addon_or_config_path.get_base_dir() : p_addon_or_config_path;
		return base_dir.get_file();
	}

	return p_addon_or_config_path;
}

void FoundryBuildTaskRegistry::_add_invalid_descriptor_diagnostic(
		const String &p_provider_id, const SourceMetadata &p_source, const String &p_message) {
	Diagnostic diagnostic;
	diagnostic.kind = DIAGNOSTIC_INVALID_DESCRIPTOR;
	diagnostic.provider_id = p_provider_id;
	diagnostic.source = p_source;
	diagnostic.message = p_message;
	diagnostics.push_back(diagnostic);
}

bool FoundryBuildTaskRegistry::_validate_addon_provider_descriptor(
		const ProjectBuildPipelineConfig::ProviderDescriptor &p_descriptor, const SourceMetadata &p_source) {
	bool valid = true;

	SourceMetadata script_source = p_source;
	script_source.key = "script";
	if (p_descriptor.script.is_empty()) {
		_add_invalid_descriptor_diagnostic(p_descriptor.id, script_source,
				"Addon provider descriptors require a script path.");
		valid = false;
	} else if (!_is_valid_provider_script_path(p_descriptor.script)) {
		_add_invalid_descriptor_diagnostic(p_descriptor.id, script_source,
				"Provider script must be a concrete res:// Foundry Script file.");
		valid = false;
	}

	if (p_descriptor.class_name.is_empty()) {
		SourceMetadata class_source = p_source;
		class_source.key = "class_name";
		_add_invalid_descriptor_diagnostic(p_descriptor.id, class_source,
				"Addon provider descriptors require a class_name.");
		valid = false;
	}

	return valid;
}

bool FoundryBuildTaskRegistry::_register_provider_entry(const ProviderEntry &p_entry) {
	if (!_is_valid_id(p_entry.id)) {
		_add_invalid_descriptor_diagnostic(p_entry.id, p_entry.source, vformat("Provider id '%s' is not valid.", p_entry.id));
		return false;
	}

	const ProviderEntry *existing = providers.getptr(p_entry.id);
	if (existing != nullptr) {
		Diagnostic diagnostic;
		diagnostic.kind = DIAGNOSTIC_PROVIDER_COLLISION;
		diagnostic.provider_id = p_entry.id;
		diagnostic.source = p_entry.source;
		diagnostic.conflicting_source = existing->source;
		const String incoming_source = _source_description(p_entry.source);
		const String existing_source = _source_description(existing->source);
		diagnostic.message = vformat("Provider id '%s' from %s conflicts with an existing descriptor from %s.",
				p_entry.id, incoming_source, existing_source);
		diagnostics.push_back(diagnostic);
		return false;
	}

	providers[p_entry.id] = p_entry;
	provider_order.push_back(p_entry.id);
	return true;
}

PackedStringArray FoundryBuildTaskRegistry::_string_array_from_variant(
		const Variant &p_value, const SourceMetadata &p_source, const String &p_provider_id) {
	if (p_value.get_type() == Variant::PACKED_STRING_ARRAY) {
		return p_value;
	}

	PackedStringArray strings;
	if (p_value.get_type() == Variant::ARRAY) {
		Array array = p_value;
		for (int i = 0; i < array.size(); i++) {
			if (array[i].get_type() != Variant::STRING && array[i].get_type() != Variant::STRING_NAME) {
				_add_invalid_descriptor_diagnostic(p_provider_id, p_source, "Expected every providers array entry to be a string.");
				return PackedStringArray();
			}
			strings.push_back(array[i]);
		}
		return strings;
	}

	_add_invalid_descriptor_diagnostic(p_provider_id, p_source, "Expected providers to be a PackedStringArray.");
	return PackedStringArray();
}

String FoundryBuildTaskRegistry::_string_from_config(const Ref<ConfigFile> &p_config, const String &p_section,
		const String &p_key, const String &p_provider_id, const SourceMetadata &p_source) {
	if (!p_config->has_section_key(p_section, p_key)) {
		return String();
	}

	const Variant value = p_config->get_value(p_section, p_key);
	if (value.get_type() == Variant::STRING || value.get_type() == Variant::STRING_NAME) {
		return value;
	}

	SourceMetadata source = p_source;
	source.key = p_key;
	_add_invalid_descriptor_diagnostic(p_provider_id, source, vformat("Expected '%s' to be a string.", p_key));
	return String();
}

void FoundryBuildTaskRegistry::clear() {
	providers.clear();
	provider_order.clear();
	diagnostics.clear();
}

void FoundryBuildTaskRegistry::register_builtin_providers() {
	register_native_provider(COMMAND_PROVIDER_ID, "Command", "Runs an external command with argv-safe arguments.");
}

bool FoundryBuildTaskRegistry::register_native_provider(
		const String &p_id, const String &p_display_name, const String &p_description) {
	ProviderEntry entry;
	entry.id = p_id;
	entry.display_name = p_display_name;
	entry.description = p_description;
	entry.source = _native_source("engine", p_id);
	return _register_provider_entry(entry);
}

bool FoundryBuildTaskRegistry::register_provider_descriptor(
		const ProjectBuildPipelineConfig::ProviderDescriptor &p_descriptor, const SourceMetadata &p_source) {
	ProviderEntry entry;
	entry.id = p_descriptor.id;
	entry.script = p_descriptor.script;
	entry.class_name = p_descriptor.class_name;
	entry.display_name = p_descriptor.display_name;
	entry.description = p_descriptor.description;
	entry.addon = p_descriptor.addon;
	entry.source = p_source;
	if (entry.addon.is_empty() && p_source.type == SOURCE_ADDON) {
		entry.addon = p_source.identifier;
	}
	return _register_provider_entry(entry);
}

void FoundryBuildTaskRegistry::register_project_providers(
		const ProjectBuildPipelineConfig &p_config, const String &p_project_config_path) {
	for (const String &id : p_config.get_provider_order()) {
		const ProjectBuildPipelineConfig::ProviderDescriptor *provider = p_config.get_provider(id);
		if (provider == nullptr) {
			continue;
		}

		const SourceMetadata source = _project_source(p_project_config_path,
				String(PROJECT_PROVIDER_SECTION_PREFIX) + id);
		register_provider_descriptor(*provider, source);
	}
}

void FoundryBuildTaskRegistry::register_addon_metadata(
		const String &p_addon, const String &p_config_path, const Ref<ConfigFile> &p_config) {
	if (p_config.is_null()) {
		const SourceMetadata source = _addon_source(p_addon, p_config_path, BUILD_TASKS_SECTION, "providers");
		_add_invalid_descriptor_diagnostic(String(), source,
				"Cannot read addon build task metadata from a null ConfigFile.");
		return;
	}
	if (!p_config->has_section_key(BUILD_TASKS_SECTION, "providers")) {
		return;
	}

	SourceMetadata provider_list_source = _addon_source(p_addon, p_config_path, BUILD_TASKS_SECTION, "providers");
	const PackedStringArray provider_ids = _string_array_from_variant(
			p_config->get_value(BUILD_TASKS_SECTION, "providers"), provider_list_source, String());
	for (int i = 0; i < provider_ids.size(); i++) {
		const String id = provider_ids[i];
		const String section = String(BUILD_TASKS_PROVIDER_SECTION_PREFIX) + id;
		const SourceMetadata source = _addon_source(p_addon, p_config_path, section);
		if (!p_config->has_section(section)) {
			_add_invalid_descriptor_diagnostic(id, source,
					vformat("Addon build task provider '%s' is listed but has no descriptor section.", id));
			continue;
		}

		ProjectBuildPipelineConfig::ProviderDescriptor provider;
		provider.id = id;
		provider.addon = p_addon;
		provider.script = _string_from_config(p_config, section, "script", id, source);
		provider.class_name = _string_from_config(p_config, section, "class_name", id, source);
		provider.display_name = _string_from_config(p_config, section, "display_name", id, source);
		provider.description = _string_from_config(p_config, section, "description", id, source);
		if (p_config->has_section_key(section, "addon")) {
			provider.addon = _string_from_config(p_config, section, "addon", id, source);
		}

		if (!_validate_addon_provider_descriptor(provider, source)) {
			continue;
		}

		register_provider_descriptor(provider, source);
	}
}

void FoundryBuildTaskRegistry::register_enabled_addon_metadata(const PackedStringArray &p_enabled_addons) {
	for (int i = 0; i < p_enabled_addons.size(); i++) {
		const String enabled_addon = p_enabled_addons[i];
		const String config_path = enabled_addon.begins_with("res://") ? enabled_addon : "res://addons/" + enabled_addon + "/plugin.cfg";
		const String addon = _addon_identifier_from_path(enabled_addon);

		Ref<ConfigFile> config;
		config.instantiate();
		const Error err = config->load(config_path);
		if (err != OK) {
			const SourceMetadata source = _addon_source(addon, config_path);
			_add_invalid_descriptor_diagnostic(String(), source,
					vformat("Cannot load addon build task metadata from '%s'.", config_path));
			continue;
		}

		register_addon_metadata(addon, config_path, config);
	}
}

Vector<FoundryBuildTaskRegistry::Diagnostic> FoundryBuildTaskRegistry::validate_task_providers(
		const ProjectBuildPipelineConfig &p_config, const String &p_project_config_path) const {
	Vector<Diagnostic> provider_diagnostics;
	for (const String &task_name : p_config.get_task_order()) {
		const ProjectBuildPipelineConfig::TaskDefinition *task = p_config.get_task(task_name);
		if (task == nullptr || task->provider.is_empty()) {
			continue;
		}

		if (!has_provider(task->provider)) {
			Diagnostic diagnostic;
			diagnostic.kind = DIAGNOSTIC_MISSING_PROVIDER;
			diagnostic.provider_id = task->provider;
			diagnostic.task_name = task_name;
			diagnostic.source = _project_source(p_project_config_path,
					String(PROJECT_TASK_SECTION_PREFIX) + task_name, "provider");
			diagnostic.message = vformat(
					"Build task '%s' references provider id '%s', but no native, addon, or project provider descriptor registered it.",
					task_name, task->provider);
			provider_diagnostics.push_back(diagnostic);
		}
	}

	return provider_diagnostics;
}

bool FoundryBuildTaskRegistry::has_provider(const String &p_id) const {
	return providers.has(p_id);
}

const FoundryBuildTaskRegistry::ProviderEntry *FoundryBuildTaskRegistry::get_provider(const String &p_id) const {
	return providers.getptr(p_id);
}
