/**************************************************************************/
/*  fs_build_task_bootstrap_loader.cpp                                    */
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

#include "fs_build_task_bootstrap_loader.h"

#include "fs_analyzer.h"
#include "fs_compiler.h"
#include "fs_parser.h"

#include "core/config/project_build_pipeline_config.h"
#include "core/config/project_build_pipeline_status.h"
#include "core/config/project_settings.h"
#include "core/io/config_file.h"
#include "core/io/file_access.h"
#include "core/io/resource_uid.h"
#include "core/object/class_db.h"

namespace {

class ScopedBootstrapDependencyRoot {
	String previous_root;

public:
	explicit ScopedBootstrapDependencyRoot(const String &p_root) {
		previous_root = FSAnalyzer::get_bootstrap_allowed_dependency_root();
		FSAnalyzer::set_bootstrap_allowed_dependency_root(p_root);
	}

	~ScopedBootstrapDependencyRoot() {
		FSAnalyzer::set_bootstrap_allowed_dependency_root(previous_root);
	}
};

} // namespace

void FoundryBuildTaskBootstrapLoader::_add_diagnostic(FoundryBuildTaskRegistry::DiagnosticKind p_kind,
		const FoundryBuildTaskRegistry::ProviderEntry &p_provider, const String &p_key,
		const String &p_message) {
	FoundryBuildTaskRegistry::Diagnostic diagnostic;
	diagnostic.kind = p_kind;
	diagnostic.provider_id = p_provider.id;
	diagnostic.source = p_provider.source;
	diagnostic.source.key = p_key;
	diagnostic.message = p_message;
	diagnostics.push_back(diagnostic);
}

String FoundryBuildTaskBootstrapLoader::_diagnostic_message_from_parser(
		const FoundryBuildTaskRegistry::ProviderEntry &p_provider, const FSParser &p_parser,
		const String &p_context) {
	String message = vformat("%s build task provider '%s' from '%s'.", p_context, p_provider.class_name, p_provider.script);
	if (!p_parser.get_errors().is_empty()) {
		const FSParser::ParserError &error = p_parser.get_errors().front()->get();
		message += vformat(" %s:%d:%d: %s", p_provider.script, error.line, error.column, error.message);
	}
	return message;
}

bool FoundryBuildTaskBootstrapLoader::_path_is_within_root(const String &p_path, const String &p_root) {
	const String path = ResourceUID::ensure_path(p_path).replace_char('\\', '/').simplify_path();
	String root = ResourceUID::ensure_path(p_root).replace_char('\\', '/').simplify_path();
	if (!root.ends_with("/")) {
		root += "/";
	}

	return path == root.trim_suffix("/") || path.begins_with(root);
}

String FoundryBuildTaskBootstrapLoader::_provider_allowed_root(
		const FoundryBuildTaskRegistry::ProviderEntry &p_provider) {
	if (p_provider.source.type == FoundryBuildTaskRegistry::SOURCE_ADDON) {
		if (!p_provider.source.path.is_empty()) {
			String root = p_provider.source.path.ends_with("plugin.cfg") ? p_provider.source.path.get_base_dir() : p_provider.source.path;
			if (!root.ends_with("/")) {
				root += "/";
			}
			return root;
		}

		const String addon = p_provider.source.identifier.is_empty() ? p_provider.addon : p_provider.source.identifier;
		if (!addon.is_empty()) {
			return "res://addons/" + addon + "/";
		}
	}

	return p_provider.script.get_base_dir() + "/";
}

Error FoundryBuildTaskBootstrapLoader::_merge_error(Error p_current, Error p_candidate) {
	return p_current == OK ? p_candidate : p_current;
}

Error FoundryBuildTaskBootstrapLoader::_validate_bootstrap_scope(
		const FoundryBuildTaskRegistry::ProviderEntry &p_provider, const FSParser &p_parser) {
	const FSParser::ClassNode *root = p_parser.get_tree();
	if (root == nullptr) {
		_add_diagnostic(FoundryBuildTaskRegistry::DIAGNOSTIC_LOADER_FAILURE, p_provider, "script",
				vformat("Build task provider '%s' did not produce a script class.", p_provider.class_name));
		return ERR_PARSE_ERROR;
	}

	if (p_provider.class_name.is_empty()) {
		_add_diagnostic(FoundryBuildTaskRegistry::DIAGNOSTIC_INVALID_DESCRIPTOR, p_provider, "class_name",
				"Script-backed build task providers require a class_name.");
		return ERR_INVALID_DATA;
	}

	if (String(root->get_global_name()) != p_provider.class_name) {
		_add_diagnostic(FoundryBuildTaskRegistry::DIAGNOSTIC_INVALID_DESCRIPTOR, p_provider, "class_name",
				vformat("Provider descriptor class_name '%s' does not match script class '%s'.",
						p_provider.class_name, String(root->get_global_name())));
		return ERR_INVALID_DATA;
	}

	const String allowed_root = _provider_allowed_root(p_provider);
	for (const String &dependency : p_parser.get_dependencies()) {
		if (dependency.get_extension() != "fs") {
			continue;
		}
		if (!_path_is_within_root(dependency, allowed_root)) {
			_add_diagnostic(FoundryBuildTaskRegistry::DIAGNOSTIC_INVALID_DESCRIPTOR, p_provider, "script",
					vformat("Build task provider dependency '%s' is outside the provider bootstrap root '%s'.",
							dependency, allowed_root));
			return ERR_INVALID_DATA;
		}
	}

	return OK;
}

Error FoundryBuildTaskBootstrapLoader::_compile_provider(
		const FoundryBuildTaskRegistry::ProviderEntry &p_provider, Ref<FoundryScript> &r_script) {
	if (p_provider.script.is_empty()) {
		_add_diagnostic(FoundryBuildTaskRegistry::DIAGNOSTIC_INVALID_DESCRIPTOR, p_provider, "script",
				"Script-backed build task providers require a script path.");
		return ERR_INVALID_DATA;
	}

	const String allowed_root = _provider_allowed_root(p_provider);
	if (p_provider.source.type == FoundryBuildTaskRegistry::SOURCE_ADDON &&
			!_path_is_within_root(p_provider.script, allowed_root)) {
		_add_diagnostic(FoundryBuildTaskRegistry::DIAGNOSTIC_INVALID_DESCRIPTOR, p_provider, "script",
				vformat("Build task provider script '%s' is outside the provider bootstrap root '%s'.",
						p_provider.script, allowed_root));
		return ERR_INVALID_DATA;
	}

	Error read_error = OK;
	const String source = FileAccess::get_file_as_string(p_provider.script, &read_error);
	if (read_error != OK) {
		_add_diagnostic(FoundryBuildTaskRegistry::DIAGNOSTIC_LOADER_FAILURE, p_provider, "script",
				vformat("Cannot read build task provider '%s' from '%s'.", p_provider.class_name, p_provider.script));
		return read_error;
	}

	FSParser parser;
	Error err = parser.parse(source, p_provider.script, false);
	if (err != OK) {
		_add_diagnostic(FoundryBuildTaskRegistry::DIAGNOSTIC_LOADER_FAILURE, p_provider, "script",
				_diagnostic_message_from_parser(p_provider, parser, "Failed to parse"));
		return err;
	}

	err = _validate_bootstrap_scope(p_provider, parser);
	if (err != OK) {
		return err;
	}

	if (!FSLanguage::get_singleton()->has_any_global_constant(SNAME("RefCounted"))) {
		FSLanguage::get_singleton()->init();
	}

	ScopedBootstrapDependencyRoot scoped_dependency_root(_provider_allowed_root(p_provider));

	FSAnalyzer analyzer(&parser);
	err = analyzer.analyze();
	if (err != OK) {
		_add_diagnostic(FoundryBuildTaskRegistry::DIAGNOSTIC_LOADER_FAILURE, p_provider, "script",
				_diagnostic_message_from_parser(p_provider, parser, "Failed to analyze"));
		return err;
	}

	Ref<FoundryScript> script;
	script.instantiate();
	script->set_path(p_provider.script);
	script->set_source_code(source);

	FSCompiler compiler;
	err = compiler.compile(&parser, script.ptr(), false);
	if (err != OK) {
		_add_diagnostic(FoundryBuildTaskRegistry::DIAGNOSTIC_LOADER_FAILURE, p_provider, "script",
				vformat("Failed to compile build task provider '%s' from '%s'. %s:%d:%d: %s",
						p_provider.class_name, p_provider.script, p_provider.script, compiler.get_error_line(),
						compiler.get_error_column(), compiler.get_error()));
		return err;
	}

	r_script = script;
	return OK;
}

Error FoundryBuildTaskBootstrapLoader::_instantiate_provider(
		const FoundryBuildTaskRegistry::ProviderEntry &p_provider, const Ref<FoundryScript> &p_script,
		Ref<FoundryBuildTask> &r_instance) {
	ERR_FAIL_COND_V(p_script.is_null(), ERR_INVALID_PARAMETER);

	if (!ClassDB::is_parent_class(p_script->get_instance_base_type(), FoundryBuildTask::get_class_static())) {
		_add_diagnostic(FoundryBuildTaskRegistry::DIAGNOSTIC_INVALID_DESCRIPTOR, p_provider, "class_name",
				vformat("Provider class '%s' must extend FoundryBuildTask.", p_provider.class_name));
		return ERR_INVALID_DATA;
	}

	Callable::CallError call_error;
	const Variant instance_value = p_script->_new(nullptr, 0, call_error);
	if (call_error.error != Callable::CallError::CALL_OK) {
		_add_diagnostic(FoundryBuildTaskRegistry::DIAGNOSTIC_LOADER_FAILURE, p_provider, "class_name",
				vformat("Cannot instantiate build task provider '%s'.", p_provider.class_name));
		return ERR_CANT_CREATE;
	}

	Ref<FoundryBuildTask> instance = instance_value;
	if (instance.is_null()) {
		_add_diagnostic(FoundryBuildTaskRegistry::DIAGNOSTIC_LOADER_FAILURE, p_provider, "class_name",
				vformat("Provider class '%s' did not instantiate as FoundryBuildTask.", p_provider.class_name));
		return ERR_CANT_CREATE;
	}

	r_instance = instance;
	return OK;
}

Error FoundryBuildTaskBootstrapLoader::_load_provider(
		const FoundryBuildTaskRegistry::ProviderEntry &p_provider) {
	if (p_provider.source.type == FoundryBuildTaskRegistry::SOURCE_NATIVE) {
		LoadedProvider loaded;
		loaded.descriptor = p_provider;
		if (p_provider.id == "command") {
			Ref<FoundryCommandBuildTask> command_task;
			command_task.instantiate();
			loaded.instance = command_task;
		} else {
			loaded.instance.instantiate();
		}
		loaded_providers[p_provider.id] = loaded;
		loaded_order.push_back(p_provider.id);
		return OK;
	}

	Ref<FoundryScript> script;
	Error err = _compile_provider(p_provider, script);
	if (err != OK) {
		return err;
	}

	Ref<FoundryBuildTask> instance;
	err = _instantiate_provider(p_provider, script, instance);
	if (err != OK) {
		return err;
	}

	LoadedProvider loaded;
	loaded.descriptor = p_provider;
	loaded.script = script;
	loaded.instance = instance;
	loaded_providers[p_provider.id] = loaded;
	loaded_order.push_back(p_provider.id);
	return OK;
}

void FoundryBuildTaskBootstrapLoader::clear() {
	loaded_providers.clear();
	loaded_order.clear();
	diagnostics.clear();
}

void FoundryBuildTaskBootstrapLoader::set_trusted_execution(bool p_trusted_execution) {
	trusted_execution = p_trusted_execution;
}

bool FoundryBuildTaskBootstrapLoader::is_trusted_execution() const {
	return trusted_execution || ProjectBuildTrustStore::is_cli_trusted_execution();
}

Error FoundryBuildTaskBootstrapLoader::load_registered_providers(const FoundryBuildTaskRegistry &p_registry) {
	clear();

	Error result = OK;
	for (const String &provider_id : p_registry.get_provider_order()) {
		const FoundryBuildTaskRegistry::ProviderEntry *provider = p_registry.get_provider(provider_id);
		if (provider == nullptr) {
			continue;
		}

		result = _merge_error(result, _load_provider(*provider));
	}

	return result;
}

Error FoundryBuildTaskBootstrapLoader::load_registered_providers(
		const FoundryBuildTaskRegistry &p_registry, const PackedStringArray &p_provider_ids) {
	clear();

	Error result = OK;
	for (int i = 0; i < p_provider_ids.size(); i++) {
		const String provider_id = p_provider_ids[i];
		if (has_loaded_provider(provider_id)) {
			continue;
		}

		const FoundryBuildTaskRegistry::ProviderEntry *provider = p_registry.get_provider(provider_id);
		if (provider == nullptr) {
			continue;
		}

		result = _merge_error(result, _load_provider(*provider));
	}

	return result;
}

Error FoundryBuildTaskBootstrapLoader::load_project_bootstrap_providers(const String &p_project_config_path) {
	FoundryBuildTaskRegistry registry;
	registry.register_builtin_providers();

	ProjectSettings *project_settings = ProjectSettings::get_singleton();
	if (project_settings != nullptr && project_settings->has_setting("editor_plugins/enabled")) {
		const PackedStringArray enabled_addons = project_settings->get("editor_plugins/enabled");
		registry.register_enabled_addon_metadata(enabled_addons);
	}

	if (!p_project_config_path.is_empty() && FileAccess::exists(p_project_config_path)) {
		Ref<ConfigFile> config;
		config.instantiate();
		const Error config_err = config->load(p_project_config_path);
		if (config_err != OK) {
			clear();
			return config_err;
		}

		ProjectBuildPipelineConfig build_config;
		build_config.load_from_config_file(config);
		const Vector<ProjectBuildPipelineConfig::ValidationError> validation_errors = build_config.validate();
		const String provider_section_prefix = "build/providers/";
		const String provider_key_prefix = "providers/";
		Vector<FoundryBuildTaskRegistry::Diagnostic> provider_diagnostics;
		for (const ProjectBuildPipelineConfig::ValidationError &validation_error : validation_errors) {
			const bool provider_section_error = validation_error.section.begins_with(provider_section_prefix);
			const bool provider_key_error = validation_error.section == "build" &&
					validation_error.key.begins_with(provider_key_prefix);
			if (!provider_section_error && !provider_key_error) {
				continue;
			}

			FoundryBuildTaskRegistry::Diagnostic diagnostic;
			diagnostic.kind = FoundryBuildTaskRegistry::DIAGNOSTIC_INVALID_DESCRIPTOR;
			diagnostic.message = validation_error.message;
			diagnostic.source.type = FoundryBuildTaskRegistry::SOURCE_PROJECT;
			diagnostic.source.identifier = "project";
			diagnostic.source.path = p_project_config_path;
			diagnostic.source.section = validation_error.section;
			diagnostic.source.key = validation_error.key;
			if (diagnostic.source.section.begins_with(provider_section_prefix)) {
				diagnostic.provider_id = diagnostic.source.section.trim_prefix(provider_section_prefix);
			} else {
				const String key_rest = diagnostic.source.key.trim_prefix(provider_key_prefix);
				const int provider_id_end = key_rest.find_char('/');
				if (provider_id_end >= 0) {
					diagnostic.provider_id = key_rest.substr(0, provider_id_end);
				}
			}
			provider_diagnostics.push_back(diagnostic);
		}
		if (!provider_diagnostics.is_empty()) {
			clear();
			for (const FoundryBuildTaskRegistry::Diagnostic &diagnostic : provider_diagnostics) {
				diagnostics.push_back(diagnostic);
			}
			return ERR_INVALID_DATA;
		}
		registry.register_project_providers(build_config, p_project_config_path);
	}

	const Vector<FoundryBuildTaskRegistry::Diagnostic> registry_diagnostics = registry.get_diagnostics();
	ProjectBuildTrustStore trust_store;
	trust_store.load();
	const bool can_load_script_providers = is_trusted_execution() || trust_store.is_project_trusted();

	Error result = OK;
	if (can_load_script_providers) {
		result = load_registered_providers(registry);
	} else {
		clear();
		for (const String &provider_id : registry.get_provider_order()) {
			const FoundryBuildTaskRegistry::ProviderEntry *provider = registry.get_provider(provider_id);
			if (provider == nullptr) {
				continue;
			}
			if (provider->source.type == FoundryBuildTaskRegistry::SOURCE_NATIVE) {
				result = _merge_error(result, _load_provider(*provider));
				continue;
			}

			FoundryBuildTaskRegistry::Diagnostic diagnostic;
			diagnostic.kind = FoundryBuildTaskRegistry::DIAGNOSTIC_UNTRUSTED_PROVIDER;
			diagnostic.provider_id = provider->id;
			diagnostic.source = provider->source;
			diagnostic.message = vformat("Build task provider '%s' requires trusted execution before it can be loaded automatically.",
					provider->id);
			diagnostics.push_back(diagnostic);
			result = _merge_error(result, ERR_UNAUTHORIZED);
		}
	}
	for (const FoundryBuildTaskRegistry::Diagnostic &diagnostic : registry_diagnostics) {
		diagnostics.push_back(diagnostic);
	}
	return result;
}

Error FoundryBuildTaskBootstrapLoader::load_provider_schema(
		const String &p_provider_id, Ref<FoundryBuildTaskConfigSchema> &r_schema) {
	r_schema.unref();

	const LoadedProvider *provider = get_loaded_provider(p_provider_id);
	if (provider == nullptr || provider->instance.is_null()) {
		return ERR_DOES_NOT_EXIST;
	}

	Ref<FoundryBuildTaskConfigSchema> schema =
			FoundryBuildTask::call_get_config_schema_script_hook(provider->instance);
	if (schema.is_null()) {
		_add_diagnostic(FoundryBuildTaskRegistry::DIAGNOSTIC_LOADER_FAILURE, provider->descriptor, "class_name",
				vformat("Provider class '%s' returned an invalid build task config schema.",
						provider->descriptor.class_name));
		return ERR_INVALID_DATA;
	}

	r_schema = schema;
	return OK;
}

bool FoundryBuildTaskBootstrapLoader::has_loaded_provider(const String &p_provider_id) const {
	return loaded_providers.has(p_provider_id);
}

const FoundryBuildTaskBootstrapLoader::LoadedProvider *FoundryBuildTaskBootstrapLoader::get_loaded_provider(
		const String &p_provider_id) const {
	return loaded_providers.getptr(p_provider_id);
}
