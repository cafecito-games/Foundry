/**************************************************************************/
/*  test_project_build_pipeline_config.h                                  */
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
#include "tests/test_macros.h"

namespace TestProjectBuildPipelineConfig {

static Ref<ConfigFile> parse_build_config(const String &p_text) {
	Ref<ConfigFile> config;
	config.instantiate();
	CHECK_EQ(config->parse(p_text), OK);
	return config;
}

static bool has_validation_error(const Vector<ProjectBuildPipelineConfig::ValidationError> &p_errors, const String &p_section, const String &p_key) {
	for (int i = 0; i < p_errors.size(); i++) {
		if (p_errors[i].section == p_section && p_errors[i].key == p_key) {
			return true;
		}
	}
	return false;
}

static int count_validation_errors(const Vector<ProjectBuildPipelineConfig::ValidationError> &p_errors, const String &p_section, const String &p_key) {
	int count = 0;
	for (int i = 0; i < p_errors.size(); i++) {
		if (p_errors[i].section == p_section && p_errors[i].key == p_key) {
			count++;
		}
	}
	return count;
}

static Ref<ConfigFile> config_from_project_settings_custom_map(const ProjectSettings::CustomMap &p_custom) {
	Ref<ConfigFile> config;
	config.instantiate();

	for (const KeyValue<String, Variant> &E : p_custom) {
		const int div = E.key.find_char('/');
		if (div < 0) {
			config->set_value(String(), E.key, E.value);
		} else {
			config->set_value(E.key.substr(0, div), E.key.substr(div + 1), E.value);
		}
	}

	return config;
}

TEST_CASE("[ProjectBuildPipelineConfig] parses valid build config") {
	Ref<ConfigFile> config = parse_build_config(
			"[build]\n"
			"enabled=true\n"
			"pre_compile=PackedStringArray(\"generate_proto\", \"post_process_proto\")\n"
			"post_compile=PackedStringArray(\"bundle_metadata\")\n"
			"\n"
			"[build/providers/generate_protobuf]\n"
			"script=\"res://addons/protobuf_build/generate_protobuf.fs\"\n"
			"class_name=\"GenerateProtobuf\"\n"
			"display_name=\"Generate Protobuf\"\n"
			"description=\"Generates Foundry Script protobuf bindings.\"\n"
			"addon=\"protobuf_build\"\n"
			"\n"
			"[build/tasks/generate_proto]\n"
			"provider=\"generate_protobuf\"\n"
			"inputs=PackedStringArray(\"res://proto/**/*.proto\")\n"
			"outputs=PackedStringArray(\"res://generated/protobuf/\")\n"
			"options={\"language\": \"foundry_script\", \"package\": \"game.net\"}\n"
			"\n"
			"[build/tasks/post_process_proto]\n"
			"provider=\"command\"\n"
			"command=\"python3\"\n"
			"args=PackedStringArray(\"tools/postprocess_proto.py\", \"res://generated/protobuf\")\n"
			"inputs=PackedStringArray(\"res://generated/protobuf/**/*.fs\")\n"
			"outputs=PackedStringArray(\"res://generated/protobuf/\")\n"
			"working_directory=\"res://\"\n"
			"environment={\"PYTHONUNBUFFERED\": \"1\"}\n"
			"timeout_seconds=60\n"
			"\n"
			"[build/tasks/bundle_metadata]\n"
			"provider=\"command\"\n"
			"command=\"python3\"\n"
			"args=PackedStringArray(\"tools/bundle_metadata.py\")\n"
			"inputs=PackedStringArray(\"res://generated/protobuf/metadata.json\")\n"
			"outputs=PackedStringArray(\"res://build/metadata.bundle\")\n");

	ProjectBuildPipelineConfig build_config;
	CHECK_EQ(build_config.load_from_config_file(config), OK);

	Vector<ProjectBuildPipelineConfig::ValidationError> errors = build_config.validate();
	CHECK_EQ(errors.size(), 0);
	CHECK_EQ(build_config.get_status(), ProjectBuildPipelineConfig::STATUS_CONFIGURED);

	PackedStringArray pre_compile = build_config.get_stage_tasks(ProjectBuildPipelineConfig::STAGE_PRE_COMPILE);
	REQUIRE_EQ(pre_compile.size(), 2);
	CHECK_EQ(pre_compile[0], "generate_proto");
	CHECK_EQ(pre_compile[1], "post_process_proto");

	const ProjectBuildPipelineConfig::ProviderDescriptor *provider = build_config.get_provider("generate_protobuf");
	REQUIRE(provider != nullptr);
	CHECK_EQ(provider->script, "res://addons/protobuf_build/generate_protobuf.fs");
	CHECK_EQ(provider->class_name, "GenerateProtobuf");
	CHECK_EQ(provider->display_name, "Generate Protobuf");
	CHECK_EQ(provider->description, "Generates Foundry Script protobuf bindings.");
	CHECK_EQ(provider->addon, "protobuf_build");

	const ProjectBuildPipelineConfig::TaskDefinition *task = build_config.get_task("generate_proto");
	REQUIRE(task != nullptr);
	CHECK_EQ(task->provider, "generate_protobuf");
	REQUIRE_EQ(task->inputs.size(), 1);
	CHECK_EQ(task->inputs[0], "res://proto/**/*.proto");
	REQUIRE_EQ(task->outputs.size(), 1);
	CHECK_EQ(task->outputs[0], "res://generated/protobuf/");
	CHECK_EQ(String(task->options["language"]), "foundry_script");
	CHECK_EQ(String(task->options["package"]), "game.net");

	const ProjectBuildPipelineConfig::TaskDefinition *command_task = build_config.get_task("post_process_proto");
	REQUIRE(command_task != nullptr);
	CHECK_EQ(command_task->provider, "command");
	CHECK_EQ(command_task->command, "python3");
	REQUIRE_EQ(command_task->args.size(), 2);
	CHECK_EQ(command_task->args[0], "tools/postprocess_proto.py");
	CHECK_EQ(command_task->working_directory, "res://");
	CHECK_EQ(String(command_task->environment["PYTHONUNBUFFERED"]), "1");
	CHECK_EQ(command_task->timeout_seconds, 60);
}

TEST_CASE("[ProjectBuildPipelineConfig] round trips through ConfigFile and ProjectSettings custom map") {
	Ref<ConfigFile> config = parse_build_config(
			"[build]\n"
			"enabled=true\n"
			"pre_compile=PackedStringArray(\"generate_proto\", \"post_process_proto\")\n"
			"\n"
			"[build/providers/generate_protobuf]\n"
			"script=\"res://addons/protobuf_build/generate_protobuf.fs\"\n"
			"class_name=\"GenerateProtobuf\"\n"
			"\n"
			"[build/tasks/generate_proto]\n"
			"provider=\"generate_protobuf\"\n"
			"inputs=PackedStringArray(\"res://proto/**/*.proto\")\n"
			"outputs=PackedStringArray(\"res://generated/protobuf/\")\n"
			"options={\"language\": \"foundry_script\"}\n"
			"\n"
			"[build/tasks/post_process_proto]\n"
			"provider=\"command\"\n"
			"command=\"python3\"\n"
			"args=PackedStringArray(\"tools/postprocess_proto.py\")\n"
			"inputs=PackedStringArray(\"res://generated/protobuf/**/*.fs\")\n"
			"outputs=PackedStringArray(\"res://generated/protobuf/\")\n"
			"enabled=false\n");

	ProjectBuildPipelineConfig original;
	CHECK_EQ(original.load_from_config_file(config), OK);
	CHECK_EQ(original.validate().size(), 0);

	Ref<ConfigFile> serialized;
	serialized.instantiate();
	original.write_to_config_file(serialized);

	ProjectBuildPipelineConfig reread;
	CHECK_EQ(reread.load_from_config_file(serialized), OK);
	CHECK_EQ(reread.validate().size(), 0);
	CHECK_EQ(reread.get_stage_tasks(ProjectBuildPipelineConfig::STAGE_PRE_COMPILE).size(), 2);
	CHECK_EQ(reread.get_enabled_stage_tasks(ProjectBuildPipelineConfig::STAGE_PRE_COMPILE).size(), 1);

	const ProjectBuildPipelineConfig::TaskDefinition *disabled_task = reread.get_task("post_process_proto");
	REQUIRE(disabled_task != nullptr);
	CHECK_FALSE(disabled_task->enabled);
	CHECK_EQ(disabled_task->command, "python3");

	ProjectBuildPipelineConfig project_settings_style;
	CHECK_EQ(project_settings_style.load_from_config_file(config_from_project_settings_custom_map(original.to_project_settings_custom_map())), OK);
	CHECK_EQ(project_settings_style.validate().size(), 0);
	CHECK_EQ(project_settings_style.get_stage_tasks(ProjectBuildPipelineConfig::STAGE_PRE_COMPILE).size(), 2);
	CHECK_EQ(project_settings_style.get_enabled_stage_tasks(ProjectBuildPipelineConfig::STAGE_PRE_COMPILE).size(), 1);
}

TEST_CASE("[ProjectBuildPipelineConfig] validation reports structured section and key context") {
	Ref<ConfigFile> config = parse_build_config(
			"[build]\n"
			"enabled=true\n"
			"pre_compile=PackedStringArray(\"generate_proto\", \"missing_task\", \"generate_proto\")\n"
			"post_compile=PackedStringArray(\"generate_proto\")\n"
			"\n"
			"[build/providers/command]\n"
			"script=\"res://addons/not_allowed.fs\"\n"
			"class_name=\"NotAllowed\"\n"
			"\n"
			"[build/tasks/generate_proto]\n"
			"provider=\"missing_provider\"\n"
			"outputs=PackedStringArray(\"res://\")\n"
			"\n"
			"[build/tasks/run_codegen]\n"
			"provider=\"command\"\n"
			"outputs=PackedStringArray(\"res://generated/\")\n");

	ProjectBuildPipelineConfig build_config;
	CHECK_EQ(build_config.load_from_config_file(config), OK);

	Vector<ProjectBuildPipelineConfig::ValidationError> errors = build_config.validate();
	CHECK(has_validation_error(errors, "build", "pre_compile"));
	CHECK(has_validation_error(errors, "build", "post_compile"));
	CHECK(has_validation_error(errors, "build/providers/command", "id"));
	CHECK(has_validation_error(errors, "build/tasks/generate_proto", "provider"));
	CHECK(has_validation_error(errors, "build/tasks/generate_proto", "outputs"));
	CHECK(has_validation_error(errors, "build/tasks/run_codegen", "command"));
}

TEST_CASE("[ProjectBuildPipelineConfig] provider scripts must be concrete Foundry Script files") {
	const char *invalid_scripts[] = {
		"res://",
		"res://addons/*.fs",
		"res://addons/provider.gd",
	};

	for (const char *invalid_script : invalid_scripts) {
		Ref<ConfigFile> config = parse_build_config(vformat(
				"[build]\n"
				"enabled=true\n"
				"pre_compile=PackedStringArray(\"generate_proto\")\n"
				"\n"
				"[build/providers/generate_protobuf]\n"
				"script=\"%s\"\n"
				"class_name=\"GenerateProtobuf\"\n"
				"\n"
				"[build/tasks/generate_proto]\n"
				"provider=\"generate_protobuf\"\n"
				"outputs=PackedStringArray(\"res://generated/protobuf/\")\n",
				invalid_script));

		ProjectBuildPipelineConfig build_config;
		CHECK_EQ(build_config.load_from_config_file(config), OK);

		Vector<ProjectBuildPipelineConfig::ValidationError> errors = build_config.validate();
		CHECK(has_validation_error(errors, "build/providers/generate_protobuf", "script"));
	}
}

TEST_CASE("[ProjectBuildPipelineConfig] duplicate tasks in one stage do not report cross-stage duplicates") {
	Ref<ConfigFile> config = parse_build_config(
			"[build]\n"
			"enabled=true\n"
			"pre_compile=PackedStringArray(\"generate_proto\", \"generate_proto\")\n"
			"\n"
			"[build/tasks/generate_proto]\n"
			"provider=\"command\"\n"
			"command=\"python3\"\n"
			"outputs=PackedStringArray(\"res://generated/protobuf/\")\n");

	ProjectBuildPipelineConfig build_config;
	CHECK_EQ(build_config.load_from_config_file(config), OK);

	Vector<ProjectBuildPipelineConfig::ValidationError> errors = build_config.validate();
	CHECK_EQ(errors.size(), 1);
	CHECK_EQ(count_validation_errors(errors, "build", "pre_compile"), 1);
	CHECK_FALSE(has_validation_error(errors, "build", "post_compile"));
}

TEST_CASE("[ProjectBuildPipelineConfig] disabled stage tasks remain ordered but are skipped for execution") {
	Ref<ConfigFile> config = parse_build_config(
			"[build]\n"
			"enabled=true\n"
			"pre_compile=PackedStringArray(\"generate_proto\", \"disabled_codegen\")\n"
			"\n"
			"[build/tasks/generate_proto]\n"
			"provider=\"command\"\n"
			"command=\"python3\"\n"
			"outputs=PackedStringArray(\"res://generated/protobuf/\")\n"
			"\n"
			"[build/tasks/disabled_codegen]\n"
			"provider=\"command\"\n"
			"command=\"python3\"\n"
			"outputs=PackedStringArray(\"res://generated/disabled/\")\n"
			"enabled=false\n");

	ProjectBuildPipelineConfig build_config;
	CHECK_EQ(build_config.load_from_config_file(config), OK);
	CHECK_EQ(build_config.validate().size(), 0);

	PackedStringArray stage_tasks = build_config.get_stage_tasks(ProjectBuildPipelineConfig::STAGE_PRE_COMPILE);
	REQUIRE_EQ(stage_tasks.size(), 2);
	CHECK_EQ(stage_tasks[0], "generate_proto");
	CHECK_EQ(stage_tasks[1], "disabled_codegen");

	PackedStringArray executable_tasks = build_config.get_enabled_stage_tasks(ProjectBuildPipelineConfig::STAGE_PRE_COMPILE);
	REQUIRE_EQ(executable_tasks.size(), 1);
	CHECK_EQ(executable_tasks[0], "generate_proto");
}

TEST_CASE("[ProjectBuildPipelineConfig] globally disabled build tasks remain ordered but are skipped for execution") {
	Ref<ConfigFile> config = parse_build_config(
			"[build]\n"
			"enabled=false\n"
			"pre_compile=PackedStringArray(\"generate_proto\")\n"
			"\n"
			"[build/tasks/generate_proto]\n"
			"provider=\"command\"\n"
			"command=\"python3\"\n"
			"outputs=PackedStringArray(\"res://generated/protobuf/\")\n");

	ProjectBuildPipelineConfig build_config;
	CHECK_EQ(build_config.load_from_config_file(config), OK);
	CHECK_EQ(build_config.validate().size(), 0);
	CHECK_EQ(build_config.get_status(), ProjectBuildPipelineConfig::STATUS_DISABLED);

	PackedStringArray stage_tasks = build_config.get_stage_tasks(ProjectBuildPipelineConfig::STAGE_PRE_COMPILE);
	REQUIRE_EQ(stage_tasks.size(), 1);
	CHECK_EQ(stage_tasks[0], "generate_proto");

	CHECK_EQ(build_config.get_enabled_stage_tasks(ProjectBuildPipelineConfig::STAGE_PRE_COMPILE).size(), 0);
}

TEST_CASE("[ProjectBuildPipelineConfig] rejects generated outputs that normalize to the project root") {
	Ref<ConfigFile> config = parse_build_config(
			"[build]\n"
			"enabled=true\n"
			"pre_compile=PackedStringArray(\"generate_proto\")\n"
			"\n"
			"[build/tasks/generate_proto]\n"
			"provider=\"command\"\n"
			"command=\"python3\"\n"
			"outputs=PackedStringArray(\"res://generated/..\")\n");

	ProjectBuildPipelineConfig build_config;
	CHECK_EQ(build_config.load_from_config_file(config), OK);

	Vector<ProjectBuildPipelineConfig::ValidationError> errors = build_config.validate();
	CHECK(has_validation_error(errors, "build/tasks/generate_proto", "outputs"));
}

TEST_CASE("[ProjectBuildPipelineConfig] rejects generated outputs with dot segments that normalize to unsafe roots") {
	const char *unsafe_outputs[] = {
		"res://./",
		"res://./.godot/cache",
		"res://.//.foundry/tmp",
	};

	for (const char *unsafe_output : unsafe_outputs) {
		Ref<ConfigFile> config = parse_build_config(vformat(
				"[build]\n"
				"enabled=true\n"
				"pre_compile=PackedStringArray(\"generate_proto\")\n"
				"\n"
				"[build/tasks/generate_proto]\n"
				"provider=\"command\"\n"
				"command=\"python3\"\n"
				"outputs=PackedStringArray(\"%s\")\n",
				unsafe_output));

		ProjectBuildPipelineConfig build_config;
		CHECK_EQ(build_config.load_from_config_file(config), OK);

		Vector<ProjectBuildPipelineConfig::ValidationError> errors = build_config.validate();
		CHECK(has_validation_error(errors, "build/tasks/generate_proto", "outputs"));
	}
}

TEST_CASE("[ProjectBuildPipelineConfig] rejects reserved generated output roots case insensitively") {
	const char *unsafe_outputs[] = {
		"res://.Godot/cache",
		"res://.FOUNDRY/tmp",
	};

	for (const char *unsafe_output : unsafe_outputs) {
		Ref<ConfigFile> config = parse_build_config(vformat(
				"[build]\n"
				"enabled=true\n"
				"pre_compile=PackedStringArray(\"generate_proto\")\n"
				"\n"
				"[build/tasks/generate_proto]\n"
				"provider=\"command\"\n"
				"command=\"python3\"\n"
				"outputs=PackedStringArray(\"%s\")\n",
				unsafe_output));

		ProjectBuildPipelineConfig build_config;
		CHECK_EQ(build_config.load_from_config_file(config), OK);

		Vector<ProjectBuildPipelineConfig::ValidationError> errors = build_config.validate();
		CHECK(has_validation_error(errors, "build/tasks/generate_proto", "outputs"));
	}
}

TEST_CASE("[ProjectBuildPipelineConfig] rejects glob working directories") {
	const char *invalid_working_directories[] = {
		"res://*/",
		"res://tools?",
	};

	for (const char *working_directory : invalid_working_directories) {
		Ref<ConfigFile> config = parse_build_config(vformat(
				"[build]\n"
				"enabled=true\n"
				"pre_compile=PackedStringArray(\"generate_proto\")\n"
				"\n"
				"[build/tasks/generate_proto]\n"
				"provider=\"command\"\n"
				"command=\"python3\"\n"
				"working_directory=\"%s\"\n"
				"outputs=PackedStringArray(\"res://generated/protobuf/\")\n",
				working_directory));

		ProjectBuildPipelineConfig build_config;
		CHECK_EQ(build_config.load_from_config_file(config), OK);

		Vector<ProjectBuildPipelineConfig::ValidationError> errors = build_config.validate();
		CHECK(has_validation_error(errors, "build/tasks/generate_proto", "working_directory"));
	}
}

TEST_CASE("[ProjectBuildPipelineConfig] no configured build tasks reports disabled without errors") {
	Ref<ConfigFile> config = parse_build_config(
			"[application]\n"
			"config/name=\"No Build Tasks\"\n");

	ProjectBuildPipelineConfig build_config;
	CHECK_EQ(build_config.load_from_config_file(config), OK);

	CHECK_EQ(build_config.get_status(), ProjectBuildPipelineConfig::STATUS_DISABLED);
	CHECK_EQ(build_config.validate().size(), 0);
	CHECK_EQ(build_config.get_stage_tasks(ProjectBuildPipelineConfig::STAGE_PRE_COMPILE).size(), 0);
	CHECK_EQ(build_config.get_enabled_stage_tasks(ProjectBuildPipelineConfig::STAGE_PRE_COMPILE).size(), 0);
}

} // namespace TestProjectBuildPipelineConfig
