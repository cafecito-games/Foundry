/**************************************************************************/
/*  foundry_build_task_registry.h                                         */
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

#include "core/config/project_build_pipeline_config.h"
#include "core/io/config_file.h"
#include "core/templates/hash_map.h"

class FoundryBuildTaskRegistry {
public:
	enum SourceType {
		SOURCE_NATIVE,
		SOURCE_ADDON,
		SOURCE_PROJECT,
	};

	enum DiagnosticKind {
		DIAGNOSTIC_PROVIDER_COLLISION,
		DIAGNOSTIC_MISSING_PROVIDER,
		DIAGNOSTIC_INVALID_DESCRIPTOR,
		DIAGNOSTIC_LOADER_FAILURE,
		DIAGNOSTIC_UNTRUSTED_PROVIDER,
	};

	struct SourceMetadata {
		SourceType type = SOURCE_PROJECT;
		String identifier;
		String path;
		String section;
		String key;
	};

	struct ProviderEntry {
		String id;
		String script;
		String class_name;
		String display_name;
		String description;
		String addon;
		SourceMetadata source;
	};

	struct Diagnostic {
		DiagnosticKind kind = DIAGNOSTIC_INVALID_DESCRIPTOR;
		String provider_id;
		String task_name;
		String message;
		SourceMetadata source;
		SourceMetadata conflicting_source;
	};

private:
	HashMap<String, ProviderEntry> providers;
	Vector<String> provider_order;
	Vector<Diagnostic> diagnostics;

	static bool _is_valid_id(const String &p_id);
	static bool _is_valid_project_path_or_glob(const String &p_path);
	static bool _is_valid_provider_script_path(const String &p_path);
	static String _source_description(const SourceMetadata &p_source);
	static SourceMetadata _native_source(const String &p_identifier, const String &p_provider_id);
	static SourceMetadata _addon_source(const String &p_addon, const String &p_config_path,
			const String &p_section = String(), const String &p_key = String());
	static SourceMetadata _project_source(const String &p_config_path, const String &p_section = String(),
			const String &p_key = String());
	static String _addon_identifier_from_path(const String &p_addon_or_config_path);

	void _add_invalid_descriptor_diagnostic(const String &p_provider_id, const SourceMetadata &p_source,
			const String &p_message);
	bool _validate_addon_provider_descriptor(const ProjectBuildPipelineConfig::ProviderDescriptor &p_descriptor,
			const SourceMetadata &p_source);
	bool _register_provider_entry(const ProviderEntry &p_entry);
	PackedStringArray _string_array_from_variant(const Variant &p_value, const SourceMetadata &p_source,
			const String &p_provider_id);
	String _string_from_config(const Ref<ConfigFile> &p_config, const String &p_section, const String &p_key,
			const String &p_provider_id, const SourceMetadata &p_source);

public:
	void clear();

	void register_builtin_providers();
	bool register_native_provider(const String &p_id, const String &p_display_name, const String &p_description);
	bool register_provider_descriptor(const ProjectBuildPipelineConfig::ProviderDescriptor &p_descriptor,
			const SourceMetadata &p_source);
	void register_project_providers(const ProjectBuildPipelineConfig &p_config,
			const String &p_project_config_path = "res://project.foundry");
	void register_addon_metadata(const String &p_addon, const String &p_config_path, const Ref<ConfigFile> &p_config);
	void register_enabled_addon_metadata(const PackedStringArray &p_enabled_addons);

	Vector<Diagnostic> validate_task_providers(const ProjectBuildPipelineConfig &p_config,
			const String &p_project_config_path = "res://project.foundry") const;

	bool has_provider(const String &p_id) const;
	const ProviderEntry *get_provider(const String &p_id) const;

	const HashMap<String, ProviderEntry> &get_providers() const { return providers; }
	const Vector<String> &get_provider_order() const { return provider_order; }
	const Vector<Diagnostic> &get_diagnostics() const { return diagnostics; }
};
