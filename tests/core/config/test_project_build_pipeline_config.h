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

TEST_CASE("[ProjectBuildPipelineConfig] stage filter keeps only relevant tasks providers and parse errors") {
	Ref<ConfigFile> config = parse_build_config(
			"[build]\n"
			"enabled=true\n"
			"pre_compile=PackedStringArray(\"generate_proto\")\n"
			"post_compile=42\n"
			"\n"
			"[build/providers/generate_protobuf]\n"
			"script=\"res://addons/protobuf_build/generate_protobuf.fs\"\n"
			"class_name=\"GenerateProtobuf\"\n"
			"\n"
			"[build/providers/post_provider]\n"
			"script=42\n"
			"class_name=\"PostProvider\"\n"
			"\n"
			"[build/tasks/generate_proto]\n"
			"provider=\"generate_protobuf\"\n"
			"outputs=PackedStringArray(\"res://generated/protobuf/\")\n"
			"\n"
			"[build/tasks/post_bundle]\n"
			"provider=\"post_provider\"\n"
			"outputs=PackedStringArray(\"res://build/post.bundle\")\n");

	ProjectBuildPipelineConfig build_config;
	CHECK_EQ(build_config.load_from_config_file(config), OK);

	Vector<ProjectBuildPipelineConfig::ValidationError> full_errors = build_config.validate();
	CHECK(has_validation_error(full_errors, "build", "post_compile"));
	CHECK(has_validation_error(full_errors, "build/providers/post_provider", "script"));

	ProjectBuildPipelineConfig pre_compile =
			build_config.filtered_for_stage(ProjectBuildPipelineConfig::STAGE_PRE_COMPILE);
	CHECK_EQ(pre_compile.get_stage_tasks(ProjectBuildPipelineConfig::STAGE_POST_COMPILE).size(), 0);

	PackedStringArray pre_compile_tasks = pre_compile.get_stage_tasks(ProjectBuildPipelineConfig::STAGE_PRE_COMPILE);
	REQUIRE_EQ(pre_compile_tasks.size(), 1);
	CHECK_EQ(pre_compile_tasks[0], "generate_proto");
	CHECK(pre_compile.get_task("generate_proto") != nullptr);
	CHECK(pre_compile.get_task("post_bundle") == nullptr);
	CHECK(pre_compile.get_provider("generate_protobuf") != nullptr);
	CHECK(pre_compile.get_provider("post_provider") == nullptr);
	CHECK_EQ(pre_compile.validate().size(), 0);
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

static ProjectBuildPipelineConfig::TaskDefinition make_command_task(const String &p_name, const String &p_command,
		const String &p_output) {
	ProjectBuildPipelineConfig::TaskDefinition task;
	task.name = p_name;
	task.provider = "command";
	task.command = p_command;
	task.outputs.push_back(p_output);
	return task;
}

TEST_CASE("[ProjectBuildPipelineConfig] set_task inserts and replaces while preserving order") {
	ProjectBuildPipelineConfig build_config;

	build_config.set_task(make_command_task("generate", "foundryproto", "res://generated/"));
	build_config.set_task(make_command_task("bundle", "python3", "res://bundle/"));
	CHECK_EQ(build_config.get_task_order().size(), 2);
	CHECK_EQ(build_config.get_task_order()[0], "generate");
	CHECK_EQ(build_config.get_task_order()[1], "bundle");

	// Replacing an existing task keeps its position and updates fields.
	ProjectBuildPipelineConfig::TaskDefinition updated = make_command_task("generate", "regen", "res://generated/");
	build_config.set_task(updated);
	CHECK_EQ(build_config.get_task_order().size(), 2);
	CHECK_EQ(build_config.get_task_order()[0], "generate");
	REQUIRE(build_config.get_task("generate") != nullptr);
	CHECK_EQ(build_config.get_task("generate")->command, "regen");
}

TEST_CASE("[ProjectBuildPipelineConfig] remove_task strips definition and stage references") {
	ProjectBuildPipelineConfig build_config;
	build_config.set_enabled(true);
	build_config.set_task(make_command_task("generate", "foundryproto", "res://generated/"));
	build_config.set_task(make_command_task("bundle", "python3", "res://bundle/"));
	build_config.add_task_to_stage(ProjectBuildPipelineConfig::STAGE_PRE_COMPILE, "generate");
	build_config.add_task_to_stage(ProjectBuildPipelineConfig::STAGE_POST_COMPILE, "bundle");

	CHECK(build_config.remove_task("generate"));
	CHECK(build_config.get_task("generate") == nullptr);
	CHECK_FALSE(build_config.get_task_order().has("generate"));
	CHECK_EQ(build_config.get_stage_tasks(ProjectBuildPipelineConfig::STAGE_PRE_COMPILE).size(), 0);
	CHECK_EQ(build_config.get_stage_tasks(ProjectBuildPipelineConfig::STAGE_POST_COMPILE).size(), 1);

	CHECK_FALSE(build_config.remove_task("does_not_exist"));
}

TEST_CASE("[ProjectBuildPipelineConfig] rename_task updates order and stage references") {
	ProjectBuildPipelineConfig build_config;
	build_config.set_task(make_command_task("generate", "foundryproto", "res://generated/"));
	build_config.add_task_to_stage(ProjectBuildPipelineConfig::STAGE_PRE_COMPILE, "generate");

	CHECK(build_config.rename_task("generate", "generate_proto"));
	CHECK(build_config.get_task("generate") == nullptr);
	REQUIRE(build_config.get_task("generate_proto") != nullptr);
	CHECK_EQ(build_config.get_task("generate_proto")->name, "generate_proto");
	CHECK_EQ(build_config.get_task_order()[0], "generate_proto");
	const PackedStringArray pre = build_config.get_stage_tasks(ProjectBuildPipelineConfig::STAGE_PRE_COMPILE);
	REQUIRE_EQ(pre.size(), 1);
	CHECK_EQ(pre[0], "generate_proto");

	// Renaming onto an existing name is rejected.
	build_config.set_task(make_command_task("other", "python3", "res://other/"));
	CHECK_FALSE(build_config.rename_task("generate_proto", "other"));
	CHECK_FALSE(build_config.rename_task("missing", "whatever"));
}

TEST_CASE("[ProjectBuildPipelineConfig] rename_task replaces every stage reference") {
	ProjectBuildPipelineConfig build_config;
	build_config.set_task(make_command_task("generate", "foundryproto", "res://generated/"));
	// Simulate an invalid loaded stage list that references the same task twice.
	PackedStringArray duplicated;
	duplicated.push_back("generate");
	duplicated.push_back("generate");
	build_config.set_stage_tasks(ProjectBuildPipelineConfig::STAGE_PRE_COMPILE, duplicated);

	CHECK(build_config.rename_task("generate", "generate_proto"));
	const PackedStringArray pre = build_config.get_stage_tasks(ProjectBuildPipelineConfig::STAGE_PRE_COMPILE);
	REQUIRE_EQ(pre.size(), 2);
	CHECK_EQ(pre[0], "generate_proto");
	CHECK_EQ(pre[1], "generate_proto");
	CHECK_FALSE(pre.has("generate"));
}

TEST_CASE("[ProjectBuildPipelineConfig] duplicate_task creates a uniquely named copy after the original") {
	ProjectBuildPipelineConfig build_config;
	build_config.set_task(make_command_task("generate", "foundryproto", "res://generated/"));
	build_config.set_task(make_command_task("bundle", "python3", "res://bundle/"));

	const String copy = build_config.duplicate_task("generate");
	CHECK_EQ(copy, "generate_copy");
	REQUIRE(build_config.get_task(copy) != nullptr);
	CHECK_EQ(build_config.get_task(copy)->command, "foundryproto");
	// Inserted right after the original in definition order.
	CHECK_EQ(build_config.get_task_order()[0], "generate");
	CHECK_EQ(build_config.get_task_order()[1], "generate_copy");
	CHECK_EQ(build_config.get_task_order()[2], "bundle");

	// A second duplicate gets a further-incremented unique name.
	const String copy2 = build_config.duplicate_task("generate");
	CHECK_EQ(copy2, "generate_copy_2");
	CHECK(build_config.duplicate_task("missing").is_empty());
}

TEST_CASE("[ProjectBuildPipelineConfig] stage add/remove/move manage ordered references") {
	ProjectBuildPipelineConfig build_config;
	build_config.set_task(make_command_task("a", "cmd", "res://a/"));
	build_config.set_task(make_command_task("b", "cmd", "res://b/"));
	build_config.set_task(make_command_task("c", "cmd", "res://c/"));

	CHECK(build_config.add_task_to_stage(ProjectBuildPipelineConfig::STAGE_PRE_COMPILE, "a"));
	CHECK(build_config.add_task_to_stage(ProjectBuildPipelineConfig::STAGE_PRE_COMPILE, "b"));
	CHECK(build_config.add_task_to_stage(ProjectBuildPipelineConfig::STAGE_PRE_COMPILE, "c"));
	// Adding the same task again to a stage is rejected.
	CHECK_FALSE(build_config.add_task_to_stage(ProjectBuildPipelineConfig::STAGE_PRE_COMPILE, "a"));

	CHECK(build_config.move_stage_task(ProjectBuildPipelineConfig::STAGE_PRE_COMPILE, 2, 0));
	PackedStringArray pre = build_config.get_stage_tasks(ProjectBuildPipelineConfig::STAGE_PRE_COMPILE);
	REQUIRE_EQ(pre.size(), 3);
	CHECK_EQ(pre[0], "c");
	CHECK_EQ(pre[1], "a");
	CHECK_EQ(pre[2], "b");
	CHECK_FALSE(build_config.move_stage_task(ProjectBuildPipelineConfig::STAGE_PRE_COMPILE, 0, 9));

	CHECK(build_config.remove_task_from_stage(ProjectBuildPipelineConfig::STAGE_PRE_COMPILE, "a"));
	pre = build_config.get_stage_tasks(ProjectBuildPipelineConfig::STAGE_PRE_COMPILE);
	REQUIRE_EQ(pre.size(), 2);
	CHECK_EQ(pre[0], "c");
	CHECK_EQ(pre[1], "b");
	CHECK_FALSE(build_config.remove_task_from_stage(ProjectBuildPipelineConfig::STAGE_PRE_COMPILE, "a"));
}

TEST_CASE("[ProjectBuildPipelineConfig] set_task_enabled keeps stage reference but skips execution") {
	ProjectBuildPipelineConfig build_config;
	build_config.set_enabled(true);
	build_config.set_task(make_command_task("generate", "foundryproto", "res://generated/"));
	build_config.add_task_to_stage(ProjectBuildPipelineConfig::STAGE_PRE_COMPILE, "generate");

	CHECK(build_config.set_task_enabled("generate", false));
	CHECK_EQ(build_config.get_stage_tasks(ProjectBuildPipelineConfig::STAGE_PRE_COMPILE).size(), 1);
	CHECK_EQ(build_config.get_enabled_stage_tasks(ProjectBuildPipelineConfig::STAGE_PRE_COMPILE).size(), 0);
	CHECK_FALSE(build_config.set_task_enabled("missing", true));
}

TEST_CASE("[ProjectBuildPipelineConfig] generate_preview round-trips through load_from_config_file") {
	ProjectBuildPipelineConfig build_config;
	build_config.set_enabled(true);

	ProjectBuildPipelineConfig::TaskDefinition task = make_command_task("generate_proto", "foundryproto", "res://generated/protobuf/");
	task.args.push_back("--input");
	task.args.push_back("proto/");
	task.inputs.push_back("res://proto/**/*.proto");
	build_config.set_task(task);
	build_config.add_task_to_stage(ProjectBuildPipelineConfig::STAGE_PRE_COMPILE, "generate_proto");

	ProjectBuildPipelineConfig::ProviderDescriptor provider;
	provider.id = "generate_protobuf";
	provider.script = "res://addons/protobuf_build/generate_protobuf.fs";
	provider.class_name = "GenerateProtobuf";
	build_config.set_provider(provider);

	const String preview = build_config.generate_preview();
	CHECK(preview.contains("[build]"));
	CHECK(preview.contains("[build/tasks/generate_proto]"));
	CHECK(preview.contains("[build/providers/generate_protobuf]"));

	Ref<ConfigFile> reparsed = parse_build_config(preview);
	ProjectBuildPipelineConfig reloaded;
	CHECK_EQ(reloaded.load_from_config_file(reparsed), OK);
	CHECK(reloaded.is_enabled());
	REQUIRE(reloaded.get_task("generate_proto") != nullptr);
	CHECK_EQ(reloaded.get_task("generate_proto")->command, "foundryproto");
	CHECK_EQ(reloaded.get_task("generate_proto")->args, task.args);
	REQUIRE(reloaded.get_provider("generate_protobuf") != nullptr);
	CHECK_EQ(reloaded.get_provider("generate_protobuf")->class_name, "GenerateProtobuf");
	const PackedStringArray pre = reloaded.get_stage_tasks(ProjectBuildPipelineConfig::STAGE_PRE_COMPILE);
	REQUIRE_EQ(pre.size(), 1);
	CHECK_EQ(pre[0], "generate_proto");

	// Editing model output must satisfy pipeline validation.
	CHECK_EQ(reloaded.validate().size(), 0);
}

TEST_CASE("[ProjectBuildPipelineConfig] provider set/remove manage descriptor order") {
	ProjectBuildPipelineConfig build_config;

	ProjectBuildPipelineConfig::ProviderDescriptor provider;
	provider.id = "generate_protobuf";
	provider.script = "res://addons/protobuf_build/generate_protobuf.fs";
	provider.class_name = "GenerateProtobuf";
	build_config.set_provider(provider);
	REQUIRE(build_config.get_provider("generate_protobuf") != nullptr);
	CHECK_EQ(build_config.get_provider_order().size(), 1);

	provider.class_name = "GenerateProtobufV2";
	build_config.set_provider(provider);
	CHECK_EQ(build_config.get_provider_order().size(), 1);
	CHECK_EQ(build_config.get_provider("generate_protobuf")->class_name, "GenerateProtobufV2");

	CHECK(build_config.remove_provider("generate_protobuf"));
	CHECK(build_config.get_provider("generate_protobuf") == nullptr);
	CHECK_EQ(build_config.get_provider_order().size(), 0);
	CHECK_FALSE(build_config.remove_provider("generate_protobuf"));
}

TEST_CASE("[ProjectBuildPipelineConfig] make_unique_task_name avoids collisions") {
	ProjectBuildPipelineConfig build_config;
	CHECK_EQ(build_config.make_unique_task_name("generate"), "generate");
	build_config.set_task(make_command_task("generate", "cmd", "res://generated/"));
	CHECK_EQ(build_config.make_unique_task_name("generate"), "generate_2");
	build_config.set_task(make_command_task("generate_2", "cmd", "res://generated/"));
	CHECK_EQ(build_config.make_unique_task_name("generate"), "generate_3");
}

} // namespace TestProjectBuildPipelineConfig
