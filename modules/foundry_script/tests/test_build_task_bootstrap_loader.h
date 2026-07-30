/**************************************************************************/
/*  test_build_task_bootstrap_loader.h                                    */
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

#include "../foundry_build_task.h"
#include "../foundry_script.h"
#include "../fs_analyzer.h"
#include "../fs_build_task_bootstrap_loader.h"

#include "core/config/foundry_build_task_registry.h"
#include "core/config/project_settings.h"
#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/object/script_diagnostic_capture.h"
#include "core/object/script_language.h"
#include "core/os/os.h"
#include "core/os/thread.h"
#include "tests/core/config/test_project_settings.h"
#include "tests/test_macros.h"
#include "tests/test_utils.h"

namespace FSTests {

struct ScopedBuildTaskProject {
	String old_resource_path;
	String root_path;

	explicit ScopedBuildTaskProject(const String &p_name) {
		old_resource_path = TestProjectSettingsInternalsAccessor::resource_path();
		root_path = TestUtils::get_temp_path(p_name + "_" + itos(OS::get_singleton()->get_ticks_usec()));
		const Error err = DirAccess::make_dir_recursive_absolute(root_path);
		CHECK_EQ(err, OK);
		if (err != OK) {
			return;
		}
		TestProjectSettingsInternalsAccessor::resource_path() = root_path;
	}

	~ScopedBuildTaskProject() {
		remove_recursive(root_path);
		TestProjectSettingsInternalsAccessor::resource_path() = old_resource_path;
	}

	String write_script(const String &p_path, const String &p_source) const {
		Ref<DirAccess> dir = DirAccess::create(DirAccess::ACCESS_RESOURCES);
		CHECK(dir.is_valid());
		if (dir.is_null()) {
			return String();
		}
		const Error mkdir_err = dir->make_dir_recursive(p_path.get_base_dir());
		CHECK_EQ(mkdir_err, OK);
		if (mkdir_err != OK) {
			return String();
		}

		Ref<FileAccess> file = FileAccess::open(p_path, FileAccess::WRITE);
		CHECK_MESSAGE(file.is_valid(), vformat("Cannot write test script '%s'.", p_path));
		if (file.is_null()) {
			return String();
		}
		file->store_string(p_source);
		return p_path;
	}

	static void remove_recursive(const String &p_absolute_path) {
		Ref<DirAccess> dir = DirAccess::open(p_absolute_path);
		if (dir.is_null()) {
			return;
		}

		dir->set_include_hidden(true);
		dir->list_dir_begin();
		for (String entry = dir->get_next(); !entry.is_empty(); entry = dir->get_next()) {
			if (entry == "." || entry == "..") {
				continue;
			}

			const String child = p_absolute_path.path_join(entry);
			if (dir->current_is_dir() && !dir->is_link(child)) {
				remove_recursive(child);
			} else {
				DirAccess::remove_absolute(child);
			}
		}
		dir->list_dir_end();
		DirAccess::remove_absolute(p_absolute_path);
	}
};

class ScopedBootstrapGlobalClass {
	StringName class_name;

public:
	ScopedBootstrapGlobalClass(
			const StringName &p_class_name, const String &p_base, const String &p_path, bool p_is_trait = false) {
		class_name = p_class_name;
		ScriptServer::remove_global_class(class_name);
		ScriptServer::add_global_class(class_name, p_base, SNAME("FoundryScript"), p_path, false, false, p_is_trait);
	}

	~ScopedBootstrapGlobalClass() {
		ScriptServer::remove_global_class(class_name);
	}
};

class ScopedBootstrapGlobalAnnotation {
	String path;

public:
	ScopedBootstrapGlobalAnnotation(const StringName &p_qualified_name, const String &p_path) {
		path = p_path;
		FSLanguage::get_singleton()->add_global_annotation(p_qualified_name, path);
	}

	~ScopedBootstrapGlobalAnnotation() {
		FSLanguage::get_singleton()->remove_global_annotations_by_path(path);
	}
};

static ProjectBuildPipelineConfig::ProviderDescriptor make_bootstrap_provider(
		const String &p_id, const String &p_script, const String &p_class_name) {
	ProjectBuildPipelineConfig::ProviderDescriptor provider;
	provider.id = p_id;
	provider.script = p_script;
	provider.class_name = p_class_name;
	provider.display_name = p_class_name;
	return provider;
}

static FoundryBuildTaskRegistry::SourceMetadata make_bootstrap_source(const String &p_provider_id) {
	FoundryBuildTaskRegistry::SourceMetadata source;
	source.type = FoundryBuildTaskRegistry::SOURCE_PROJECT;
	source.identifier = "test";
	source.path = "res://project.foundry";
	source.section = "build/providers/" + p_provider_id;
	return source;
}

static FoundryBuildTaskRegistry::SourceMetadata make_bootstrap_addon_source(
		const String &p_addon, const String &p_provider_id, const String &p_config_path = String()) {
	FoundryBuildTaskRegistry::SourceMetadata source;
	source.type = FoundryBuildTaskRegistry::SOURCE_ADDON;
	source.identifier = p_addon;
	source.path = p_config_path.is_empty() ? "res://addons/" + p_addon + "/plugin.cfg" : p_config_path;
	source.section = "build_tasks/" + p_provider_id;
	return source;
}

static String bootstrap_diagnostics_to_string(const Vector<FoundryBuildTaskRegistry::Diagnostic> &p_diagnostics) {
	PackedStringArray messages;
	for (const FoundryBuildTaskRegistry::Diagnostic &diagnostic : p_diagnostics) {
		messages.push_back(vformat("%s:%s: %s", diagnostic.provider_id, diagnostic.source.key, diagnostic.message));
	}
	return String("; ").join(messages);
}

struct BootstrapDependencyRootThreadProbe {
	String observed_root;

	static void read_root(void *p_userdata) {
		BootstrapDependencyRootThreadProbe *probe = static_cast<BootstrapDependencyRootThreadProbe *>(p_userdata);
		probe->observed_root = FSAnalyzer::get_bootstrap_allowed_dependency_root();
	}
};

TEST_CASE("[Modules][FoundryScript][BuildTaskBootstrap] Dependency roots do not leak across threads") {
	const String previous_root = FSAnalyzer::get_bootstrap_allowed_dependency_root();
	FSAnalyzer::set_bootstrap_allowed_dependency_root("res://addons/bootstrap/");

	BootstrapDependencyRootThreadProbe probe;
	Thread thread;
	const Thread::ID thread_id = thread.start(BootstrapDependencyRootThreadProbe::read_root, &probe);
	CHECK_NE(thread_id, Thread::UNASSIGNED_ID);
	if (thread_id != Thread::UNASSIGNED_ID) {
		thread.wait_to_finish();
		CHECK(probe.observed_root.is_empty());
	}

	FSAnalyzer::set_bootstrap_allowed_dependency_root(previous_root);
}

TEST_CASE("[Modules][FoundryScript][BuildTaskBootstrap] Loads a simple script-backed provider") {
	ScopedBuildTaskProject project("build_task_bootstrap_loads");
	const String script_path = project.write_script(
			"res://addons/bootstrap/simple_provider.fs",
			"class_name BootstrapSimpleProvider extends FoundryBuildTask\n");

	FoundryBuildTaskRegistry registry;
	registry.register_provider_descriptor(
			make_bootstrap_provider("bootstrap.simple", script_path, "BootstrapSimpleProvider"),
			make_bootstrap_source("bootstrap.simple"));

	FoundryBuildTaskBootstrapLoader loader;
	CHECK_EQ(loader.load_registered_providers(registry), OK);
	CHECK_MESSAGE(loader.get_diagnostics().is_empty(), bootstrap_diagnostics_to_string(loader.get_diagnostics()));
	CHECK(loader.has_loaded_provider("bootstrap.simple"));

	const FoundryBuildTaskBootstrapLoader::LoadedProvider *loaded = loader.get_loaded_provider("bootstrap.simple");
	CHECK(loaded != nullptr);
	if (loaded == nullptr) {
		return;
	}
	CHECK(loaded->script.is_valid());
	CHECK(loaded->instance.is_valid());
	CHECK(Object::cast_to<FoundryBuildTask>(loaded->instance.ptr()) != nullptr);
}

TEST_CASE("[Modules][FoundryScript][BuildTaskBootstrap] Loads the native command provider") {
	FoundryBuildTaskRegistry registry;
	registry.register_builtin_providers();

	FoundryBuildTaskBootstrapLoader loader;
	CHECK_EQ(loader.load_registered_providers(registry), OK);
	CHECK_MESSAGE(loader.get_diagnostics().is_empty(), bootstrap_diagnostics_to_string(loader.get_diagnostics()));
	CHECK(loader.has_loaded_provider("command"));

	Ref<FoundryBuildTaskConfigSchema> schema;
	CHECK_EQ(loader.load_provider_schema("command", schema), OK);
	CHECK(schema.is_valid());
	if (schema.is_valid()) {
		CHECK(schema->get_properties().has("command"));
	}
}

TEST_CASE("[Modules][FoundryScript][BuildTaskBootstrap] Rejects script providers that do not extend FoundryBuildTask") {
	ScopedBuildTaskProject project("build_task_bootstrap_rejects_base");
	const String script_path = project.write_script(
			"res://addons/bootstrap/not_a_build_task.fs",
			"class_name NotABuildTask extends RefCounted\n");

	FoundryBuildTaskRegistry registry;
	registry.register_provider_descriptor(
			make_bootstrap_provider("bootstrap.invalid_base", script_path, "NotABuildTask"),
			make_bootstrap_source("bootstrap.invalid_base"));

	FoundryBuildTaskBootstrapLoader loader;
	CHECK_EQ(loader.load_registered_providers(registry), ERR_INVALID_DATA);
	CHECK_FALSE(loader.has_loaded_provider("bootstrap.invalid_base"));

	const Vector<FoundryBuildTaskRegistry::Diagnostic> &diagnostics = loader.get_diagnostics();
	CHECK_EQ(diagnostics.size(), 1);
	if (diagnostics.size() != 1) {
		return;
	}
	CHECK_EQ(diagnostics[0].kind, FoundryBuildTaskRegistry::DIAGNOSTIC_INVALID_DESCRIPTOR);
	CHECK_EQ(diagnostics[0].provider_id, "bootstrap.invalid_base");
	CHECK_EQ(diagnostics[0].source.section, "build/providers/bootstrap.invalid_base");
	CHECK_EQ(diagnostics[0].source.key, "class_name");
	CHECK(diagnostics[0].message.contains("FoundryBuildTask"));
}

TEST_CASE("[Modules][FoundryScript][BuildTaskBootstrap] Loader failures are build diagnostics") {
	ScopedBuildTaskProject project("build_task_bootstrap_diagnostics");
	const String script_path = project.write_script(
			"res://addons/bootstrap/broken_provider.fs",
			"class_name BrokenBuildProvider extends FoundryBuildTask\n"
			"func broken(:\n"
			"\tpass\n");

	FoundryBuildTaskRegistry registry;
	registry.register_provider_descriptor(
			make_bootstrap_provider("bootstrap.broken", script_path, "BrokenBuildProvider"),
			make_bootstrap_source("bootstrap.broken"));

	Ref<ScriptDiagnosticCapture> script_diagnostics;
	script_diagnostics.instantiate();
	script_diagnostics->start();

	FoundryBuildTaskBootstrapLoader loader;
	const Error err = loader.load_registered_providers(registry);

	script_diagnostics->stop();

	CHECK_EQ(err, ERR_PARSE_ERROR);
	CHECK_EQ(script_diagnostics->get_event_count(), 0);
	CHECK_FALSE(loader.has_loaded_provider("bootstrap.broken"));

	const Vector<FoundryBuildTaskRegistry::Diagnostic> &diagnostics = loader.get_diagnostics();
	CHECK_EQ(diagnostics.size(), 1);
	if (diagnostics.size() != 1) {
		return;
	}
	CHECK_EQ(diagnostics[0].kind, FoundryBuildTaskRegistry::DIAGNOSTIC_LOADER_FAILURE);
	CHECK_EQ(diagnostics[0].provider_id, "bootstrap.broken");
	CHECK_EQ(diagnostics[0].source.section, "build/providers/bootstrap.broken");
	CHECK_EQ(diagnostics[0].source.key, "script");
	const bool message_names_provider = diagnostics[0].message.contains("BrokenBuildProvider") ||
			diagnostics[0].message.contains("broken_provider.fs");
	CHECK(message_names_provider);
}

TEST_CASE("[Modules][FoundryScript][BuildTaskBootstrap] Loads provider schema after explicit trust") {
	ScopedBuildTaskProject project("build_task_bootstrap_schema");
	const String script_path = project.write_script(
			"res://addons/bootstrap/schema_provider.fs",
			"class_name BootstrapSchemaProvider extends FoundryBuildTask\n"
			"\n"
			"func get_config_schema() -> FoundryBuildTaskConfigSchema:\n"
			"\tvar schema := FoundryBuildTaskConfigSchema.new()\n"
			"\tschema.properties = { \"target\": \"String\" }\n"
			"\treturn schema\n");

	FoundryBuildTaskRegistry registry;
	registry.register_provider_descriptor(
			make_bootstrap_provider("bootstrap.schema", script_path, "BootstrapSchemaProvider"),
			make_bootstrap_source("bootstrap.schema"));

	FoundryBuildTaskBootstrapLoader loader;
	CHECK_EQ(loader.load_registered_providers(registry), OK);
	CHECK_MESSAGE(loader.get_diagnostics().is_empty(), bootstrap_diagnostics_to_string(loader.get_diagnostics()));

	Ref<FoundryBuildTaskConfigSchema> schema;
	CHECK_EQ(loader.load_provider_schema("bootstrap.schema", schema), OK);
	CHECK(schema.is_valid());
	if (schema.is_null()) {
		return;
	}

	Dictionary properties = schema->get_properties();
	CHECK_EQ(String(properties["target"]), "String");
}

TEST_CASE("[Modules][FoundryScript][BuildTaskBootstrap] Loads command subclass provider schema after explicit trust") {
	ScopedBuildTaskProject project("build_task_bootstrap_command_subclass_schema");
	const String script_path = project.write_script(
			"res://addons/bootstrap/command_subclass_schema_provider.fs",
			"class_name BootstrapCommandSubclassSchemaProvider extends FoundryCommandBuildTask\n"
			"\n"
			"func get_config_schema() -> FoundryBuildTaskConfigSchema:\n"
			"\tvar schema := FoundryBuildTaskConfigSchema.new()\n"
			"\tschema.properties = { \"mode\": \"String\" }\n"
			"\treturn schema\n");

	FoundryBuildTaskRegistry registry;
	registry.register_provider_descriptor(
			make_bootstrap_provider("bootstrap.command_subclass_schema", script_path,
					"BootstrapCommandSubclassSchemaProvider"),
			make_bootstrap_source("bootstrap.command_subclass_schema"));

	FoundryBuildTaskBootstrapLoader loader;
	CHECK_EQ(loader.load_registered_providers(registry), OK);
	CHECK_MESSAGE(loader.get_diagnostics().is_empty(), bootstrap_diagnostics_to_string(loader.get_diagnostics()));

	Ref<FoundryBuildTaskConfigSchema> schema;
	CHECK_EQ(loader.load_provider_schema("bootstrap.command_subclass_schema", schema), OK);
	CHECK(schema.is_valid());
	if (schema.is_null()) {
		return;
	}

	Dictionary properties = schema->get_properties();
	CHECK_EQ(String(properties["mode"]), "String");
}

TEST_CASE("[Modules][FoundryScript][BuildTaskBootstrap] Provider load does not require schema evaluation") {
	ScopedBuildTaskProject project("build_task_bootstrap_lazy_schema");
	const String script_path = project.write_script(
			"res://addons/bootstrap/lazy_schema_provider.fs",
			"class_name BootstrapLazySchemaProvider extends FoundryBuildTask\n"
			"\n"
			"func get_config_schema():\n"
			"\treturn null\n");

	FoundryBuildTaskRegistry registry;
	registry.register_provider_descriptor(
			make_bootstrap_provider("bootstrap.lazy_schema", script_path, "BootstrapLazySchemaProvider"),
			make_bootstrap_source("bootstrap.lazy_schema"));

	FoundryBuildTaskBootstrapLoader loader;
	CHECK_EQ(loader.load_registered_providers(registry), OK);
	CHECK_MESSAGE(loader.get_diagnostics().is_empty(), bootstrap_diagnostics_to_string(loader.get_diagnostics()));
	CHECK(loader.has_loaded_provider("bootstrap.lazy_schema"));

	Ref<FoundryBuildTaskConfigSchema> schema;
	CHECK_EQ(loader.load_provider_schema("bootstrap.lazy_schema", schema), ERR_INVALID_DATA);
	CHECK(schema.is_null());
	CHECK_EQ(loader.get_diagnostics().size(), 1);
	if (loader.get_diagnostics().size() != 1) {
		return;
	}
	CHECK_EQ(loader.get_diagnostics()[0].kind, FoundryBuildTaskRegistry::DIAGNOSTIC_LOADER_FAILURE);
}

TEST_CASE("[Modules][FoundryScript][BuildTaskBootstrap] Allows provider-local script helpers") {
	ScopedBuildTaskProject project("build_task_bootstrap_local_helper");
	const String helper_path = project.write_script(
			"res://addons/bootstrap/helper.fs",
			"class_name BootstrapLocalHelper extends RefCounted\n"
			"\n"
			"static func value() -> int:\n"
			"\treturn 7\n");
	CHECK_FALSE(helper_path.is_empty());
	const String script_path = project.write_script(
			"res://addons/bootstrap/provider_with_helper.fs",
			"class_name BootstrapProviderWithHelper extends FoundryBuildTask\n"
			"var helper = preload(\"helper.fs\")\n");

	FoundryBuildTaskRegistry registry;
	registry.register_provider_descriptor(
			make_bootstrap_provider("bootstrap.local_helper", script_path, "BootstrapProviderWithHelper"),
			make_bootstrap_source("bootstrap.local_helper"));

	FoundryBuildTaskBootstrapLoader loader;
	CHECK_MESSAGE(loader.load_registered_providers(registry) == OK,
			bootstrap_diagnostics_to_string(loader.get_diagnostics()));
	CHECK_MESSAGE(loader.get_diagnostics().is_empty(), bootstrap_diagnostics_to_string(loader.get_diagnostics()));
	CHECK(loader.has_loaded_provider("bootstrap.local_helper"));
}

TEST_CASE("[Modules][FoundryScript][BuildTaskBootstrap] Allows provider-local preload constants") {
	ScopedBuildTaskProject project("build_task_bootstrap_local_preload_constant");
	const String helper_path = project.write_script(
			"res://addons/bootstrap/constant_helper.fs",
			"class_name BootstrapConstantHelper extends RefCounted\n"
			"\n"
			"static func value() -> int:\n"
			"\treturn 7\n");
	CHECK_FALSE(helper_path.is_empty());
	const String script_path = project.write_script(
			"res://addons/bootstrap/provider_with_constant_preload.fs",
			"class_name BootstrapProviderWithConstantPreload extends FoundryBuildTask\n"
			"const Helper = preload(\"constant_helper.fs\")\n"
			"\n"
			"func get_config_schema() -> FoundryBuildTaskConfigSchema:\n"
			"\tvar schema := FoundryBuildTaskConfigSchema.new()\n"
			"\tschema.properties = { \"value\": Helper.value() }\n"
			"\treturn schema\n");

	FoundryBuildTaskRegistry registry;
	registry.register_provider_descriptor(
			make_bootstrap_provider(
					"bootstrap.local_preload_constant", script_path, "BootstrapProviderWithConstantPreload"),
			make_bootstrap_source("bootstrap.local_preload_constant"));

	FoundryBuildTaskBootstrapLoader loader;
	CHECK_MESSAGE(loader.load_registered_providers(registry) == OK,
			bootstrap_diagnostics_to_string(loader.get_diagnostics()));
	CHECK_MESSAGE(loader.get_diagnostics().is_empty(), bootstrap_diagnostics_to_string(loader.get_diagnostics()));
	CHECK(loader.has_loaded_provider("bootstrap.local_preload_constant"));

	Ref<FoundryBuildTaskConfigSchema> schema;
	CHECK_EQ(loader.load_provider_schema("bootstrap.local_preload_constant", schema), OK);
	CHECK(schema.is_valid());
	if (schema.is_valid()) {
		CHECK_EQ(int(schema->get_properties()["value"]), 7);
	}
}

TEST_CASE("[Modules][FoundryScript][BuildTaskBootstrap] Allows provider-local namespace imports") {
	ScopedBuildTaskProject project("build_task_bootstrap_local_import");
	const String helper_path = project.write_script(
			"res://addons/bootstrap/helpers/imported_helper.fs",
			"namespace bootstrap.helpers\n"
			"class_name BootstrapImportedHelper extends RefCounted\n"
			"\n"
			"static func value() -> int:\n"
			"\treturn 7\n");
	CHECK_FALSE(helper_path.is_empty());
	ScopedBootstrapGlobalClass local_helper(
			SNAME("bootstrap.helpers.BootstrapImportedHelper"), "RefCounted", helper_path);

	const String script_path = project.write_script(
			"res://addons/bootstrap/provider_with_import.fs",
			"import bootstrap.helpers\n"
			"class_name BootstrapProviderWithImport extends FoundryBuildTask\n"
			"\n"
			"func get_config_schema() -> FoundryBuildTaskConfigSchema:\n"
			"\tvar schema := FoundryBuildTaskConfigSchema.new()\n"
			"\tschema.properties = { \"value\": BootstrapImportedHelper.value() }\n"
			"\treturn schema\n");

	FoundryBuildTaskRegistry registry;
	registry.register_provider_descriptor(
			make_bootstrap_provider("bootstrap.local_import", script_path, "BootstrapProviderWithImport"),
			make_bootstrap_source("bootstrap.local_import"));

	FoundryBuildTaskBootstrapLoader loader;
	CHECK_MESSAGE(loader.load_registered_providers(registry) == OK,
			bootstrap_diagnostics_to_string(loader.get_diagnostics()));
	CHECK_MESSAGE(loader.get_diagnostics().is_empty(), bootstrap_diagnostics_to_string(loader.get_diagnostics()));
	CHECK(loader.has_loaded_provider("bootstrap.local_import"));

	Ref<FoundryBuildTaskConfigSchema> schema;
	CHECK_EQ(loader.load_provider_schema("bootstrap.local_import", schema), OK);
	CHECK(schema.is_valid());
	if (schema.is_valid()) {
		CHECK_EQ(int(schema->get_properties()["value"]), 7);
	}
}

TEST_CASE("[Modules][FoundryScript][BuildTaskBootstrap] Rejects addon provider scripts outside addon root") {
	ScopedBuildTaskProject project("build_task_bootstrap_addon_script_root");
	const String script_path = project.write_script(
			"res://generated/project_provider.fs",
			"class_name BootstrapSpoofedAddonProvider extends FoundryBuildTask\n");

	ProjectBuildPipelineConfig::ProviderDescriptor provider = make_bootstrap_provider(
			"bootstrap.spoofed_addon", script_path, "BootstrapSpoofedAddonProvider");
	provider.addon = "generated";

	FoundryBuildTaskRegistry registry;
	registry.register_provider_descriptor(provider, make_bootstrap_addon_source("bootstrap", "bootstrap.spoofed_addon"));

	Ref<ScriptDiagnosticCapture> script_diagnostics;
	script_diagnostics.instantiate();
	script_diagnostics->start();

	FoundryBuildTaskBootstrapLoader loader;
	const Error err = loader.load_registered_providers(registry);

	script_diagnostics->stop();

	CHECK_EQ(err, ERR_INVALID_DATA);
	CHECK_EQ(script_diagnostics->get_event_count(), 0);
	CHECK_FALSE(loader.has_loaded_provider("bootstrap.spoofed_addon"));

	const Vector<FoundryBuildTaskRegistry::Diagnostic> &diagnostics = loader.get_diagnostics();
	CHECK_EQ(diagnostics.size(), 1);
	if (diagnostics.size() == 1) {
		CHECK_EQ(diagnostics[0].kind, FoundryBuildTaskRegistry::DIAGNOSTIC_INVALID_DESCRIPTOR);
		CHECK_EQ(diagnostics[0].source.type, FoundryBuildTaskRegistry::SOURCE_ADDON);
		CHECK_EQ(diagnostics[0].source.identifier, "bootstrap");
		CHECK_EQ(diagnostics[0].source.key, "script");
		CHECK(diagnostics[0].message.contains("res://generated/project_provider.fs"));
		CHECK(diagnostics[0].message.contains("outside the provider bootstrap root"));
		CHECK(diagnostics[0].message.contains("res://addons/bootstrap/"));
	}
}

TEST_CASE("[Modules][FoundryScript][BuildTaskBootstrap] Anchors addon roots to plugin config directories") {
	ScopedBuildTaskProject project("build_task_bootstrap_nested_addon_root");
	const String script_path = project.write_script(
			"res://addons/vendor/bootstrap/provider.fs",
			"class_name BootstrapNestedAddonProvider extends FoundryBuildTask\n");

	ProjectBuildPipelineConfig::ProviderDescriptor provider = make_bootstrap_provider(
			"bootstrap.nested_addon", script_path, "BootstrapNestedAddonProvider");
	provider.addon = "bootstrap";

	FoundryBuildTaskRegistry registry;
	registry.register_provider_descriptor(provider,
			make_bootstrap_addon_source("bootstrap", "bootstrap.nested_addon",
					"res://addons/vendor/bootstrap/plugin.cfg"));

	FoundryBuildTaskBootstrapLoader loader;
	CHECK_MESSAGE(loader.load_registered_providers(registry) == OK,
			bootstrap_diagnostics_to_string(loader.get_diagnostics()));
	CHECK_MESSAGE(loader.get_diagnostics().is_empty(), bootstrap_diagnostics_to_string(loader.get_diagnostics()));
	CHECK(loader.has_loaded_provider("bootstrap.nested_addon"));
}

TEST_CASE("[Modules][FoundryScript][BuildTaskBootstrap] Loads configured project providers before indexing") {
	ScopedBuildTaskProject project("build_task_bootstrap_project_loader");
	const String script_path = project.write_script(
			"res://build/bootstrap_project_provider.fs",
			"class_name BootstrapProjectConfiguredProvider extends FoundryBuildTask\n");
	const String project_config_path = project.write_script(
			"res://project.foundry",
			"[build]\n"
			"enabled=true\n"
			"\n"
			"[build/providers/bootstrap.configured]\n"
			"script=\"res://build/bootstrap_project_provider.fs\"\n"
			"class_name=\"BootstrapProjectConfiguredProvider\"\n"
			"display_name=\"Configured Provider\"\n");
	CHECK_FALSE(project_config_path.is_empty());

	FoundryBuildTaskBootstrapLoader loader;
	loader.set_trusted_execution(true);
	CHECK_EQ(loader.load_project_bootstrap_providers("res://project.foundry"), OK);
	CHECK_MESSAGE(loader.get_diagnostics().is_empty(), bootstrap_diagnostics_to_string(loader.get_diagnostics()));
	CHECK(loader.has_loaded_provider("bootstrap.configured"));

	const FoundryBuildTaskBootstrapLoader::LoadedProvider *loaded = loader.get_loaded_provider("bootstrap.configured");
	CHECK(loaded != nullptr);
	if (loaded != nullptr) {
		CHECK_EQ(loaded->descriptor.source.type, FoundryBuildTaskRegistry::SOURCE_PROJECT);
		CHECK_EQ(loaded->descriptor.script, script_path);
	}
}

TEST_CASE("[Modules][FoundryScript][BuildTaskBootstrap] Blocks automatic project providers before trust") {
	ScopedBuildTaskProject project("build_task_bootstrap_project_loader_untrusted");
	const String script_path = project.write_script(
			"res://build/untrusted_project_provider.fs",
			"class_name BootstrapUntrustedProjectProvider extends FoundryBuildTask\n");
	CHECK_FALSE(script_path.is_empty());
	const String project_config_path = project.write_script(
			"res://project.foundry",
			"[build]\n"
			"enabled=true\n"
			"\n"
			"[build/providers/bootstrap.untrusted]\n"
			"script=\"res://build/untrusted_project_provider.fs\"\n"
			"class_name=\"BootstrapUntrustedProjectProvider\"\n");
	CHECK_FALSE(project_config_path.is_empty());

	FoundryBuildTaskBootstrapLoader loader;
	CHECK_EQ(loader.load_project_bootstrap_providers("res://project.foundry"), ERR_UNAUTHORIZED);
	CHECK_FALSE(loader.has_loaded_provider("bootstrap.untrusted"));
	REQUIRE_EQ(loader.get_diagnostics().size(), 1);
	CHECK_EQ(loader.get_diagnostics()[0].kind, FoundryBuildTaskRegistry::DIAGNOSTIC_UNTRUSTED_PROVIDER);
	CHECK_EQ(loader.get_diagnostics()[0].provider_id, "bootstrap.untrusted");
	CHECK_EQ(loader.get_diagnostics()[0].source.type, FoundryBuildTaskRegistry::SOURCE_PROJECT);
	CHECK(loader.get_diagnostics()[0].message.contains("trust"));
}

TEST_CASE("[Modules][FoundryScript][BuildTaskBootstrap] Rejects invalid project provider script paths") {
	ScopedBuildTaskProject project("build_task_bootstrap_project_loader_invalid_script");
	const String project_config_path = project.write_script(
			"res://project.foundry",
			"[build]\n"
			"enabled=true\n"
			"\n"
			"[build/providers/bootstrap.invalid_project_script]\n"
			"script=\"user://outside_provider.fs\"\n"
			"class_name=\"BootstrapInvalidProjectProvider\"\n"
			"display_name=\"Invalid Provider\"\n");
	CHECK_FALSE(project_config_path.is_empty());

	Ref<ScriptDiagnosticCapture> script_diagnostics;
	script_diagnostics.instantiate();
	script_diagnostics->start();

	FoundryBuildTaskBootstrapLoader loader;
	const Error err = loader.load_project_bootstrap_providers("res://project.foundry");

	script_diagnostics->stop();

	CHECK_EQ(err, ERR_INVALID_DATA);
	CHECK_EQ(script_diagnostics->get_event_count(), 0);
	CHECK_FALSE(loader.has_loaded_provider("bootstrap.invalid_project_script"));

	const Vector<FoundryBuildTaskRegistry::Diagnostic> &diagnostics = loader.get_diagnostics();
	CHECK_EQ(diagnostics.size(), 1);
	if (diagnostics.size() == 1) {
		CHECK_EQ(diagnostics[0].kind, FoundryBuildTaskRegistry::DIAGNOSTIC_INVALID_DESCRIPTOR);
		CHECK_EQ(diagnostics[0].provider_id, "bootstrap.invalid_project_script");
		CHECK_EQ(diagnostics[0].source.type, FoundryBuildTaskRegistry::SOURCE_PROJECT);
		CHECK_EQ(diagnostics[0].source.section, "build/providers/bootstrap.invalid_project_script");
		CHECK_EQ(diagnostics[0].source.key, "script");
		CHECK(diagnostics[0].message.contains("concrete res:// Foundry Script file"));
	}
}

TEST_CASE("[Modules][FoundryScript][BuildTaskBootstrap] Rejects global traits outside provider root") {
	ScopedBuildTaskProject project("build_task_bootstrap_global_trait");
	const String trait_path = project.write_script(
			"res://generated/outside_trait.fs",
			"trait_name BootstrapGeneratedTrait\n");
	ScopedBootstrapGlobalClass project_trait(
			SNAME("BootstrapGeneratedTrait"), "RefCounted", trait_path, true);

	const String script_path = project.write_script(
			"res://addons/bootstrap/provider_with_global_trait.fs",
			"class_name BootstrapProviderWithGlobalTrait extends FoundryBuildTask\n"
			"uses BootstrapGeneratedTrait\n");

	FoundryBuildTaskRegistry registry;
	registry.register_provider_descriptor(
			make_bootstrap_provider("bootstrap.global_trait", script_path, "BootstrapProviderWithGlobalTrait"),
			make_bootstrap_source("bootstrap.global_trait"));

	FoundryBuildTaskBootstrapLoader loader;
	CHECK_EQ(loader.load_registered_providers(registry), ERR_PARSE_ERROR);
	CHECK_FALSE(loader.has_loaded_provider("bootstrap.global_trait"));

	const Vector<FoundryBuildTaskRegistry::Diagnostic> &diagnostics = loader.get_diagnostics();
	CHECK_EQ(diagnostics.size(), 1);
	if (diagnostics.size() == 1) {
		CHECK_EQ(diagnostics[0].kind, FoundryBuildTaskRegistry::DIAGNOSTIC_LOADER_FAILURE);
		CHECK_EQ(diagnostics[0].source.key, "script");
		CHECK(diagnostics[0].message.contains("outside the provider bootstrap root"));
	}
}

TEST_CASE("[Modules][FoundryScript][BuildTaskBootstrap] Rejects external annotations outside provider root") {
	ScopedBuildTaskProject project("build_task_bootstrap_external_annotation");
	const String annotation_path = project.write_script(
			"res://generated/outside_annotations.fs",
			"namespace generated.annotations\n"
			"annotation marker targets CLASS\n");
	ScopedBootstrapGlobalAnnotation annotation(
			SNAME("generated.annotations.marker"), annotation_path);

	const String script_path = project.write_script(
			"res://addons/bootstrap/provider_with_external_annotation.fs",
			"@generated.annotations.marker\n"
			"class_name BootstrapProviderWithExternalAnnotation extends FoundryBuildTask\n");

	FoundryBuildTaskRegistry registry;
	registry.register_provider_descriptor(
			make_bootstrap_provider(
					"bootstrap.external_annotation", script_path, "BootstrapProviderWithExternalAnnotation"),
			make_bootstrap_source("bootstrap.external_annotation"));

	FoundryBuildTaskBootstrapLoader loader;
	CHECK_EQ(loader.load_registered_providers(registry), ERR_PARSE_ERROR);
	CHECK_FALSE(loader.has_loaded_provider("bootstrap.external_annotation"));

	const Vector<FoundryBuildTaskRegistry::Diagnostic> &diagnostics = loader.get_diagnostics();
	CHECK_EQ(diagnostics.size(), 1);
	if (diagnostics.size() == 1) {
		CHECK_EQ(diagnostics[0].kind, FoundryBuildTaskRegistry::DIAGNOSTIC_LOADER_FAILURE);
		CHECK_EQ(diagnostics[0].source.key, "script");
		CHECK(diagnostics[0].message.contains("outside the provider bootstrap root"));
	}
}

TEST_CASE("[Modules][FoundryScript][BuildTaskBootstrap] Rejects script dependencies outside provider root") {
	ScopedBuildTaskProject project("build_task_bootstrap_outside_dependency");
	const String helper_path = project.write_script(
			"res://generated/project_helper.fs",
			"class_name BootstrapGeneratedHelper extends RefCounted\n");
	CHECK_FALSE(helper_path.is_empty());
	const String script_path = project.write_script(
			"res://addons/bootstrap/provider_with_outside_dependency.fs",
			"class_name BootstrapProviderWithOutsideDependency extends FoundryBuildTask\n"
			"var helper = preload(\"res://generated/project_helper.fs\")\n");

	FoundryBuildTaskRegistry registry;
	registry.register_provider_descriptor(
			make_bootstrap_provider(
					"bootstrap.outside_dependency", script_path, "BootstrapProviderWithOutsideDependency"),
			make_bootstrap_source("bootstrap.outside_dependency"));

	FoundryBuildTaskBootstrapLoader loader;
	CHECK_EQ(loader.load_registered_providers(registry), ERR_INVALID_DATA);
	CHECK_FALSE(loader.has_loaded_provider("bootstrap.outside_dependency"));

	const Vector<FoundryBuildTaskRegistry::Diagnostic> &diagnostics = loader.get_diagnostics();
	CHECK_EQ(diagnostics.size(), 1);
	if (diagnostics.size() == 1) {
		CHECK_EQ(diagnostics[0].kind, FoundryBuildTaskRegistry::DIAGNOSTIC_INVALID_DESCRIPTOR);
		CHECK_EQ(diagnostics[0].source.key, "script");
		CHECK(diagnostics[0].message.contains("outside the provider bootstrap root"));
	}
}

TEST_CASE("[Modules][FoundryScript][BuildTaskBootstrap] Rejects project global class dependencies") {
	ScopedBuildTaskProject project("build_task_bootstrap_project_global");
	const String global_path = project.write_script(
			"res://generated/project_global.fs",
			"class_name BootstrapGeneratedProjectType extends RefCounted\n");
	ScopedBootstrapGlobalClass project_global(
			SNAME("BootstrapGeneratedProjectType"), "RefCounted", global_path);

	const String script_path = project.write_script(
			"res://addons/bootstrap/provider_with_project_global.fs",
			"class_name BootstrapProviderWithProjectGlobal extends FoundryBuildTask\n"
			"var generated: BootstrapGeneratedProjectType\n");

	FoundryBuildTaskRegistry registry;
	registry.register_provider_descriptor(
			make_bootstrap_provider("bootstrap.project_global", script_path, "BootstrapProviderWithProjectGlobal"),
			make_bootstrap_source("bootstrap.project_global"));

	FoundryBuildTaskBootstrapLoader loader;
	CHECK_EQ(loader.load_registered_providers(registry), ERR_PARSE_ERROR);
	CHECK_FALSE(loader.has_loaded_provider("bootstrap.project_global"));

	const Vector<FoundryBuildTaskRegistry::Diagnostic> &diagnostics = loader.get_diagnostics();
	CHECK_EQ(diagnostics.size(), 1);
	if (diagnostics.size() == 1) {
		CHECK_EQ(diagnostics[0].kind, FoundryBuildTaskRegistry::DIAGNOSTIC_LOADER_FAILURE);
		CHECK_EQ(diagnostics[0].source.key, "script");
		CHECK(diagnostics[0].message.contains("outside the provider bootstrap root"));
	}
}

} // namespace FSTests
