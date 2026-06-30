/**************************************************************************/
/*  test_foundry_build_task_registry.h                                    */
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

#include "core/config/foundry_build_task_registry.h"
#include "core/config/project_build_pipeline_config.h"
#include "core/io/config_file.h"
#include "tests/test_macros.h"

namespace TestFoundryBuildTaskRegistry {

static Ref<ConfigFile> parse_registry_config(const String &p_text) {
	Ref<ConfigFile> config;
	config.instantiate();
	CHECK_EQ(config->parse(p_text), OK);
	return config;
}

static bool has_registry_diagnostic(const Vector<FoundryBuildTaskRegistry::Diagnostic> &p_diagnostics,
		FoundryBuildTaskRegistry::DiagnosticKind p_kind, const String &p_provider_id) {
	for (int i = 0; i < p_diagnostics.size(); i++) {
		if (p_diagnostics[i].kind == p_kind && p_diagnostics[i].provider_id == p_provider_id) {
			return true;
		}
	}
	return false;
}

TEST_CASE("[FoundryBuildTaskRegistry] registers the native command provider") {
	FoundryBuildTaskRegistry registry;
	registry.register_builtin_providers();

	const FoundryBuildTaskRegistry::ProviderEntry *provider = registry.get_provider("command");
	REQUIRE(provider != nullptr);
	CHECK_EQ(provider->id, "command");
	CHECK_EQ(provider->display_name, "Command");
	CHECK(provider->script.is_empty());
	CHECK(provider->class_name.is_empty());
	CHECK_EQ(provider->source.type, FoundryBuildTaskRegistry::SOURCE_NATIVE);
	CHECK_EQ(provider->source.identifier, "engine");
	CHECK(registry.get_diagnostics().is_empty());
}

TEST_CASE("[FoundryBuildTaskRegistry] merges native addon and project provider descriptors without loading scripts") {
	Ref<ConfigFile> addon_config = parse_registry_config(
			"[plugin]\n"
			"name=\"Protobuf Build\"\n"
			"\n"
			"[build_tasks]\n"
			"providers=PackedStringArray(\"protobuf.generate\")\n"
			"\n"
			"[build_tasks/protobuf.generate]\n"
			"script=\"res://addons/protobuf_build/missing_if_loaded.fs\"\n"
			"class_name=\"GenerateProtobuf\"\n"
			"display_name=\"Generate Protobuf\"\n"
			"description=\"Generates Foundry Script protobuf bindings.\"\n");

	Ref<ConfigFile> project_config = parse_registry_config(
			"[build]\n"
			"enabled=true\n"
			"pre_compile=PackedStringArray(\"generate_proto\", \"project_codegen\", \"post_process\")\n"
			"\n"
			"[build/providers/project.codegen]\n"
			"script=\"res://tools/build/project_codegen.fs\"\n"
			"class_name=\"ProjectCodegen\"\n"
			"display_name=\"Project Codegen\"\n"
			"\n"
			"[build/tasks/generate_proto]\n"
			"provider=\"protobuf.generate\"\n"
			"outputs=PackedStringArray(\"res://generated/protobuf/\")\n"
			"\n"
			"[build/tasks/project_codegen]\n"
			"provider=\"project.codegen\"\n"
			"outputs=PackedStringArray(\"res://generated/project/\")\n"
			"\n"
			"[build/tasks/post_process]\n"
			"provider=\"command\"\n"
			"command=\"python3\"\n"
			"outputs=PackedStringArray(\"res://generated/post/\")\n");

	ProjectBuildPipelineConfig build_config;
	CHECK_EQ(build_config.load_from_config_file(project_config), OK);

	FoundryBuildTaskRegistry registry;
	registry.register_builtin_providers();
	registry.register_addon_metadata("protobuf_build", "res://addons/protobuf_build/plugin.cfg", addon_config);
	registry.register_project_providers(build_config, "res://project.foundry");

	const FoundryBuildTaskRegistry::ProviderEntry *addon_provider = registry.get_provider("protobuf.generate");
	REQUIRE(addon_provider != nullptr);
	CHECK_EQ(addon_provider->script, "res://addons/protobuf_build/missing_if_loaded.fs");
	CHECK_EQ(addon_provider->class_name, "GenerateProtobuf");
	CHECK_EQ(addon_provider->display_name, "Generate Protobuf");
	CHECK_EQ(addon_provider->description, "Generates Foundry Script protobuf bindings.");
	CHECK_EQ(addon_provider->addon, "protobuf_build");
	CHECK_EQ(addon_provider->source.type, FoundryBuildTaskRegistry::SOURCE_ADDON);
	CHECK_EQ(addon_provider->source.identifier, "protobuf_build");
	CHECK_EQ(addon_provider->source.path, "res://addons/protobuf_build/plugin.cfg");
	CHECK_EQ(addon_provider->source.section, "build_tasks/protobuf.generate");

	const FoundryBuildTaskRegistry::ProviderEntry *project_provider = registry.get_provider("project.codegen");
	REQUIRE(project_provider != nullptr);
	CHECK_EQ(project_provider->script, "res://tools/build/project_codegen.fs");
	CHECK_EQ(project_provider->class_name, "ProjectCodegen");
	CHECK_EQ(project_provider->source.type, FoundryBuildTaskRegistry::SOURCE_PROJECT);
	CHECK_EQ(project_provider->source.path, "res://project.foundry");
	CHECK_EQ(project_provider->source.section, "build/providers/project.codegen");

	CHECK(build_config.validate(&registry).is_empty());
	CHECK(registry.validate_task_providers(build_config, "res://project.foundry").is_empty());
	CHECK(registry.get_diagnostics().is_empty());
}

TEST_CASE("[FoundryBuildTaskRegistry] reports provider collisions with both descriptor sources") {
	Ref<ConfigFile> addon_config = parse_registry_config(
			"[build_tasks]\n"
			"providers=PackedStringArray(\"command\", \"shared.codegen\")\n"
			"\n"
			"[build_tasks/command]\n"
			"script=\"res://addons/protobuf_build/command.fs\"\n"
			"class_name=\"AddonCommand\"\n"
			"\n"
			"[build_tasks/shared.codegen]\n"
			"script=\"res://addons/protobuf_build/shared.fs\"\n"
			"class_name=\"SharedCodegen\"\n");

	Ref<ConfigFile> project_config = parse_registry_config(
			"[build/providers/shared.codegen]\n"
			"script=\"res://tools/build/shared.fs\"\n"
			"class_name=\"ProjectSharedCodegen\"\n");

	ProjectBuildPipelineConfig build_config;
	CHECK_EQ(build_config.load_from_config_file(project_config), OK);

	FoundryBuildTaskRegistry registry;
	registry.register_builtin_providers();
	registry.register_addon_metadata("protobuf_build", "res://addons/protobuf_build/plugin.cfg", addon_config);
	registry.register_project_providers(build_config, "res://project.foundry");

	const Vector<FoundryBuildTaskRegistry::Diagnostic> diagnostics = registry.get_diagnostics();
	REQUIRE_EQ(diagnostics.size(), 2);
	CHECK(has_registry_diagnostic(diagnostics, FoundryBuildTaskRegistry::DIAGNOSTIC_PROVIDER_COLLISION, "command"));
	CHECK(has_registry_diagnostic(diagnostics, FoundryBuildTaskRegistry::DIAGNOSTIC_PROVIDER_COLLISION, "shared.codegen"));

	const FoundryBuildTaskRegistry::ProviderEntry *command_provider = registry.get_provider("command");
	REQUIRE(command_provider != nullptr);
	CHECK_EQ(command_provider->source.type, FoundryBuildTaskRegistry::SOURCE_NATIVE);

	const FoundryBuildTaskRegistry::Diagnostic &project_collision = diagnostics[1];
	CHECK_EQ(project_collision.provider_id, "shared.codegen");
	CHECK_EQ(project_collision.source.type, FoundryBuildTaskRegistry::SOURCE_PROJECT);
	CHECK_EQ(project_collision.source.section, "build/providers/shared.codegen");
	CHECK_EQ(project_collision.conflicting_source.type, FoundryBuildTaskRegistry::SOURCE_ADDON);
	CHECK_EQ(project_collision.conflicting_source.section, "build_tasks/shared.codegen");
}

TEST_CASE("[FoundryBuildTaskRegistry] rejects invalid addon provider descriptors before task validation") {
	Ref<ConfigFile> addon_config = parse_registry_config(
			"[build_tasks]\n"
			"providers=PackedStringArray(\"missing.script\", \"bad.script\", \"missing.class\", \"valid.codegen\")\n"
			"\n"
			"[build_tasks/missing.script]\n"
			"class_name=\"MissingScript\"\n"
			"\n"
			"[build_tasks/bad.script]\n"
			"script=\"res://addons/protobuf_build/bad.gd\"\n"
			"class_name=\"BadScript\"\n"
			"\n"
			"[build_tasks/missing.class]\n"
			"script=\"res://addons/protobuf_build/missing_class.fs\"\n"
			"\n"
			"[build_tasks/valid.codegen]\n"
			"script=\"res://addons/protobuf_build/valid.fs\"\n"
			"class_name=\"ValidCodegen\"\n");
	Ref<ConfigFile> project_config = parse_registry_config(
			"[build]\n"
			"enabled=true\n"
			"pre_compile=PackedStringArray(\"generate_proto\")\n"
			"\n"
			"[build/tasks/generate_proto]\n"
			"provider=\"missing.script\"\n"
			"outputs=PackedStringArray(\"res://generated/protobuf/\")\n");

	ProjectBuildPipelineConfig build_config;
	CHECK_EQ(build_config.load_from_config_file(project_config), OK);

	FoundryBuildTaskRegistry registry;
	registry.register_addon_metadata("protobuf_build", "res://addons/protobuf_build/plugin.cfg", addon_config);

	CHECK(!registry.has_provider("missing.script"));
	CHECK(!registry.has_provider("bad.script"));
	CHECK(!registry.has_provider("missing.class"));
	CHECK(registry.has_provider("valid.codegen"));

	const Vector<FoundryBuildTaskRegistry::Diagnostic> registry_diagnostics = registry.get_diagnostics();
	REQUIRE_EQ(registry_diagnostics.size(), 3);
	CHECK(has_registry_diagnostic(registry_diagnostics,
			FoundryBuildTaskRegistry::DIAGNOSTIC_INVALID_DESCRIPTOR, "missing.script"));
	CHECK(has_registry_diagnostic(registry_diagnostics,
			FoundryBuildTaskRegistry::DIAGNOSTIC_INVALID_DESCRIPTOR, "bad.script"));
	CHECK(has_registry_diagnostic(registry_diagnostics,
			FoundryBuildTaskRegistry::DIAGNOSTIC_INVALID_DESCRIPTOR, "missing.class"));
	if (registry_diagnostics.size() == 3) {
		CHECK_EQ(registry_diagnostics[0].source.section, "build_tasks/missing.script");
		CHECK_EQ(registry_diagnostics[0].source.key, "script");
		CHECK_EQ(registry_diagnostics[1].source.section, "build_tasks/bad.script");
		CHECK_EQ(registry_diagnostics[1].source.key, "script");
		CHECK_EQ(registry_diagnostics[2].source.section, "build_tasks/missing.class");
		CHECK_EQ(registry_diagnostics[2].source.key, "class_name");
	}

	const Vector<ProjectBuildPipelineConfig::ValidationError> validation_errors = build_config.validate(&registry);
	REQUIRE_EQ(validation_errors.size(), 1);
	CHECK_EQ(validation_errors[0].section, "build/tasks/generate_proto");
	CHECK_EQ(validation_errors[0].key, "provider");

	const Vector<FoundryBuildTaskRegistry::Diagnostic> task_diagnostics =
			registry.validate_task_providers(build_config, "res://project.foundry");
	REQUIRE_EQ(task_diagnostics.size(), 1);
	if (task_diagnostics.size() == 1) {
		CHECK_EQ(task_diagnostics[0].kind, FoundryBuildTaskRegistry::DIAGNOSTIC_MISSING_PROVIDER);
		CHECK_EQ(task_diagnostics[0].provider_id, "missing.script");
	}
}

TEST_CASE("[FoundryBuildTaskRegistry] reports missing task providers with task source context") {
	Ref<ConfigFile> project_config = parse_registry_config(
			"[build]\n"
			"enabled=true\n"
			"pre_compile=PackedStringArray(\"generate_proto\")\n"
			"\n"
			"[build/tasks/generate_proto]\n"
			"provider=\"missing.codegen\"\n"
			"outputs=PackedStringArray(\"res://generated/protobuf/\")\n");

	ProjectBuildPipelineConfig build_config;
	CHECK_EQ(build_config.load_from_config_file(project_config), OK);

	FoundryBuildTaskRegistry registry;
	registry.register_builtin_providers();

	const Vector<FoundryBuildTaskRegistry::Diagnostic> diagnostics =
			registry.validate_task_providers(build_config, "res://project.foundry");
	REQUIRE_EQ(diagnostics.size(), 1);
	CHECK_EQ(diagnostics[0].kind, FoundryBuildTaskRegistry::DIAGNOSTIC_MISSING_PROVIDER);
	CHECK_EQ(diagnostics[0].provider_id, "missing.codegen");
	CHECK_EQ(diagnostics[0].task_name, "generate_proto");
	CHECK_EQ(diagnostics[0].source.type, FoundryBuildTaskRegistry::SOURCE_PROJECT);
	CHECK_EQ(diagnostics[0].source.path, "res://project.foundry");
	CHECK_EQ(diagnostics[0].source.section, "build/tasks/generate_proto");
	CHECK_EQ(diagnostics[0].source.key, "provider");

	const Vector<FoundryBuildTaskRegistry::Diagnostic> repeated_diagnostics =
			registry.validate_task_providers(build_config, "res://project.foundry");
	CHECK(has_registry_diagnostic(repeated_diagnostics,
			FoundryBuildTaskRegistry::DIAGNOSTIC_MISSING_PROVIDER, "missing.codegen"));
}

} // namespace TestFoundryBuildTaskRegistry
