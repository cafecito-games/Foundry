/**************************************************************************/
/*  test_build_pipeline_runner.h                                          */
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

#include "../fs_build_pipeline_runner.h"
#include "../fs_script_extensible_native_hooks.h"

#include "core/config/project_build_pipeline_config.h"
#include "core/config/project_build_pipeline_status.h"
#include "core/config/project_build_state.h"
#include "core/config/project_settings.h"
#include "core/io/config_file.h"
#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/os/os.h"
#include "tests/core/config/test_project_settings.h"
#include "tests/test_macros.h"
#include "tests/test_utils.h"

namespace FSTests {

TEST_CASE("[Modules][FoundryScript][BuildPipelineRunner] Build task hooks are registered as script-extensible") {
	CHECK(FSScriptExtensibleNativeHooks::is_allowed_override(SNAME("FoundryBuildTask"), SNAME("get_config_schema")));
	CHECK(FSScriptExtensibleNativeHooks::is_allowed_override(SNAME("FoundryBuildTask"), SNAME("run")));
	CHECK(FSScriptExtensibleNativeHooks::is_allowed_override(
			SNAME("FoundryCommandBuildTask"), SNAME("get_config_schema")));
	CHECK(FSScriptExtensibleNativeHooks::is_allowed_override(SNAME("FoundryCommandBuildTask"), SNAME("run")));
	CHECK_FALSE(FSScriptExtensibleNativeHooks::is_allowed_override(SNAME("Object"), SNAME("get")));
}

struct ScopedPipelineProject {
	String old_resource_path;
	String root_path;
	bool old_cli_trusted = false;
	bool had_enabled_addons = false;
	Variant old_enabled_addons;

	explicit ScopedPipelineProject(const String &p_name) {
		old_resource_path = TestProjectSettingsInternalsAccessor::resource_path();
		old_cli_trusted = ProjectBuildTrustStore::is_cli_trusted_execution();
		ProjectBuildTrustStore::set_cli_trusted_execution(false);
		had_enabled_addons = ProjectSettings::get_singleton()->has_setting("editor_plugins/enabled");
		if (had_enabled_addons) {
			old_enabled_addons = ProjectSettings::get_singleton()->get("editor_plugins/enabled");
		}

		root_path = TestUtils::get_temp_path(p_name + "_" + itos(OS::get_singleton()->get_ticks_usec()));
		const Error err = DirAccess::make_dir_recursive_absolute(root_path);
		CHECK_EQ(err, OK);
		if (err == OK) {
			TestProjectSettingsInternalsAccessor::resource_path() = root_path;
		}
	}

	~ScopedPipelineProject() {
		remove_recursive(root_path);
		TestProjectSettingsInternalsAccessor::resource_path() = old_resource_path;
		ProjectBuildTrustStore::set_cli_trusted_execution(old_cli_trusted);
		if (had_enabled_addons) {
			ProjectSettings::get_singleton()->set_setting("editor_plugins/enabled", old_enabled_addons);
		} else {
			ProjectSettings::get_singleton()->set_setting("editor_plugins/enabled", Variant());
		}
	}

	String globalize(const String &p_path) const {
		return ProjectSettings::get_singleton()->globalize_path(p_path);
	}

	void save_config(const Ref<ConfigFile> &p_config) const {
		const Error err = p_config->save(globalize("res://project.foundry"));
		CHECK_EQ(err, OK);
	}

	void write_file(const String &p_path, const String &p_text) const {
		const String absolute_path = globalize(p_path);
		const Error mkdir_err = DirAccess::make_dir_recursive_absolute(absolute_path.get_base_dir());
		CHECK_EQ(mkdir_err, OK);
		if (mkdir_err != OK) {
			return;
		}

		Ref<FileAccess> file = FileAccess::open(absolute_path, FileAccess::WRITE);
		CHECK_MESSAGE(file.is_valid(), vformat("Cannot write '%s'.", absolute_path));
		if (file.is_valid()) {
			file->store_string(p_text);
		}
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

static PackedStringArray pipeline_args(const String &p_a, const String &p_b = String(), const String &p_c = String(),
		const String &p_d = String()) {
	PackedStringArray args;
	args.push_back(p_a);
	if (!p_b.is_empty()) {
		args.push_back(p_b);
	}
	if (!p_c.is_empty()) {
		args.push_back(p_c);
	}
	if (!p_d.is_empty()) {
		args.push_back(p_d);
	}
	return args;
}

static String read_build_pipeline_fixture(const String &p_relative_path) {
	const String path = String("modules/foundry_script/tests/fixtures/build_pipeline/").path_join(p_relative_path);
	Ref<FileAccess> file = FileAccess::open(path, FileAccess::READ);
	REQUIRE_MESSAGE(file.is_valid(), vformat("Cannot read build pipeline fixture '%s'.", path));
	if (file.is_null()) {
		return String();
	}
	return file->get_as_utf8_string();
}

static Ref<ConfigFile> make_pipeline_config(ProjectBuildPipelineConfig::Stage p_stage, const String &p_task_name,
		const String &p_python_script, const String &p_output = "res://generated/output.txt") {
	Ref<ConfigFile> config;
	config.instantiate();
	config->set_value("build", "enabled", true);
	if (p_stage == ProjectBuildPipelineConfig::STAGE_PRE_COMPILE) {
		config->set_value("build", "pre_compile", pipeline_args(p_task_name));
	} else {
		config->set_value("build", "post_compile", pipeline_args(p_task_name));
	}

	const String section = "build/tasks/" + p_task_name;
	config->set_value(section, "provider", "command");
	config->set_value(section, "command", "python3");
	config->set_value(section, "args", pipeline_args("-c", p_python_script, p_output));
	config->set_value(section, "outputs", pipeline_args(p_output));
	config->set_value(section, "timeout_seconds", 5);
	return config;
}

TEST_CASE("[Modules][FoundryScript][BuildPipelineRunner] Missing pipeline is a successful no-op") {
	ScopedPipelineProject project("build_pipeline_runner_noop");

	const ProjectBuildPipelineStatusSnapshot status =
			FoundryBuildPipelineRunner::get_stage_status(ProjectBuildPipelineConfig::STAGE_PRE_COMPILE);
	CHECK_EQ(status.state, ProjectBuildPipelineStatus::STATE_DISABLED);
	CHECK_FALSE(FoundryBuildPipelineRunner::status_blocks_flow(status));

	const FoundryBuildPipelineRunner::StageRunResult result =
			FoundryBuildPipelineRunner::run_stage(ProjectBuildPipelineConfig::STAGE_PRE_COMPILE);
	CHECK(result.is_success());
	CHECK_FALSE(result.ran_any_task);
	CHECK_EQ(result.snapshot.state, ProjectBuildPipelineStatus::STATE_DISABLED);
}

TEST_CASE("[Modules][FoundryScript][BuildPipelineRunner] Untrusted pre-compile command blocks without running") {
	ScopedPipelineProject project("build_pipeline_runner_untrusted_pre");
	const String output_path = "res://generated/pre.txt";
	const String script =
			"import pathlib, sys\n"
			"pathlib.Path(sys.argv[1]).parent.mkdir(parents=True, exist_ok=True)\n"
			"pathlib.Path(sys.argv[1]).write_text('should-not-run')\n";
	project.save_config(make_pipeline_config(
			ProjectBuildPipelineConfig::STAGE_PRE_COMPILE, "pre_generate", script, output_path));

	const ProjectBuildPipelineStatusSnapshot status =
			FoundryBuildPipelineRunner::get_stage_status(ProjectBuildPipelineConfig::STAGE_PRE_COMPILE);
	CHECK_EQ(status.state, ProjectBuildPipelineStatus::STATE_UNTRUSTED);
	CHECK(FoundryBuildPipelineRunner::status_blocks_flow(status));
	REQUIRE_FALSE(status.diagnostics.is_empty());
	CHECK_EQ(status.diagnostics[0].kind, ProjectBuildPipelineDiagnostic::KIND_TRUST);
	CHECK_EQ(status.diagnostics[0].task_name, "pre_generate");

	const FoundryBuildPipelineRunner::StageRunResult result =
			FoundryBuildPipelineRunner::run_stage(ProjectBuildPipelineConfig::STAGE_PRE_COMPILE);
	CHECK_FALSE(result.is_success());
	CHECK_FALSE(result.ran_any_task);
	CHECK_EQ(result.snapshot.state, ProjectBuildPipelineStatus::STATE_UNTRUSTED);
	CHECK_FALSE(FileAccess::exists(project.globalize(output_path)));
}

TEST_CASE("[Modules][FoundryScript][BuildPipelineRunner] Trusted dirty pre-compile task runs and records clean state") {
	ScopedPipelineProject project("build_pipeline_runner_trusted_pre");
	ProjectBuildTrustStore::set_cli_trusted_execution(true);

	const String output_path = "res://generated/pre.txt";
	const String script =
			"import pathlib, sys\n"
			"pathlib.Path(sys.argv[1]).parent.mkdir(parents=True, exist_ok=True)\n"
			"pathlib.Path(sys.argv[1]).write_text('pre-ok')\n";
	project.save_config(make_pipeline_config(
			ProjectBuildPipelineConfig::STAGE_PRE_COMPILE, "pre_generate", script, output_path));

	const FoundryBuildPipelineRunner::StageRunResult result =
			FoundryBuildPipelineRunner::run_stage(ProjectBuildPipelineConfig::STAGE_PRE_COMPILE);
	CHECK(result.is_success());
	CHECK(result.ran_any_task);
	CHECK_EQ(FileAccess::get_file_as_string(project.globalize(output_path)), "pre-ok");

	ProjectBuildState state;
	CHECK_EQ(state.load(), OK);
	CHECK(state.has_task_record("pre_generate"));

	const ProjectBuildPipelineStatusSnapshot status =
			FoundryBuildPipelineRunner::get_stage_status(ProjectBuildPipelineConfig::STAGE_PRE_COMPILE);
	CHECK_EQ(status.state, ProjectBuildPipelineStatus::STATE_CLEAN);
	CHECK_FALSE(FoundryBuildPipelineRunner::status_blocks_flow(status));
}

TEST_CASE("[Modules][FoundryScript][BuildPipelineRunner] Script provider run controls task result") {
	ScopedPipelineProject project("build_pipeline_runner_script_provider_run");
	ProjectBuildTrustStore::set_cli_trusted_execution(true);

	project.write_file("res://build/script_provider.fs",
			"class_name PipelineScriptProvider extends FoundryBuildTask\n"
			"\n"
			"func run(context: FoundryBuildContext) -> FoundryBuildResult:\n"
			"\tvar result := FoundryBuildResult.new()\n"
			"\tresult.success = false\n"
			"\tresult.message = \"script-run:\" + context.task_name\n"
			"\tresult.fingerprint = \"script-fingerprint\"\n"
			"\treturn result\n");

	Ref<ConfigFile> config;
	config.instantiate();
	config->set_value("build", "enabled", true);
	config->set_value("build", "pre_compile", pipeline_args("script_task"));
	config->set_value("build/providers/pipeline.script", "script", "res://build/script_provider.fs");
	config->set_value("build/providers/pipeline.script", "class_name", "PipelineScriptProvider");
	config->set_value("build/tasks/script_task", "provider", "pipeline.script");
	config->set_value("build/tasks/script_task", "outputs", pipeline_args("res://generated/script_provider.txt"));
	project.save_config(config);

	const FoundryBuildPipelineRunner::StageRunResult result =
			FoundryBuildPipelineRunner::run_stage(ProjectBuildPipelineConfig::STAGE_PRE_COMPILE);
	CHECK_FALSE(result.is_success());
	CHECK(result.ran_any_task);
	REQUIRE_FALSE(result.snapshot.diagnostics.is_empty());
	CHECK_EQ(result.snapshot.diagnostics[0].message, "script-run:script_task");
}

TEST_CASE("[Modules][FoundryScript][BuildPipelineRunner] Command subclass script provider run controls task result") {
	ScopedPipelineProject project("build_pipeline_runner_command_subclass_provider_run");
	ProjectBuildTrustStore::set_cli_trusted_execution(true);

	project.write_file("res://build/command_subclass_provider.fs",
			"class_name PipelineCommandSubclassProvider extends FoundryCommandBuildTask\n"
			"\n"
			"func run(context: FoundryBuildContext) -> FoundryBuildResult:\n"
			"\tvar result := FoundryBuildResult.new()\n"
			"\tresult.success = false\n"
			"\tresult.message = \"command-subclass-run:\" + context.task_name\n"
			"\tresult.fingerprint = \"command-subclass-fingerprint\"\n"
			"\treturn result\n");

	Ref<ConfigFile> config;
	config.instantiate();
	config->set_value("build", "enabled", true);
	config->set_value("build", "pre_compile", pipeline_args("script_task"));
	config->set_value("build/providers/pipeline.command_subclass", "script", "res://build/command_subclass_provider.fs");
	config->set_value("build/providers/pipeline.command_subclass", "class_name", "PipelineCommandSubclassProvider");
	config->set_value("build/tasks/script_task", "provider", "pipeline.command_subclass");
	config->set_value("build/tasks/script_task", "outputs",
			pipeline_args("res://generated/command_subclass_provider.txt"));
	project.save_config(config);

	const FoundryBuildPipelineRunner::StageRunResult result =
			FoundryBuildPipelineRunner::run_stage(ProjectBuildPipelineConfig::STAGE_PRE_COMPILE);
	CHECK_FALSE(result.is_success());
	CHECK(result.ran_any_task);
	REQUIRE_FALSE(result.snapshot.diagnostics.is_empty());
	CHECK_EQ(result.snapshot.diagnostics[0].message, "command-subclass-run:script_task");
}

TEST_CASE("[Modules][FoundryScript][BuildPipelineRunner] Missing dirty output reruns automatically") {
	ScopedPipelineProject project("build_pipeline_runner_missing_output");
	ProjectBuildTrustStore::set_cli_trusted_execution(true);

	const String output_path = "res://generated/pre.txt";
	const String script =
			"import pathlib, sys\n"
			"pathlib.Path(sys.argv[1]).parent.mkdir(parents=True, exist_ok=True)\n"
			"pathlib.Path(sys.argv[1]).write_text('pre-ok')\n";
	project.save_config(make_pipeline_config(
			ProjectBuildPipelineConfig::STAGE_PRE_COMPILE, "pre_generate", script, output_path));

	const FoundryBuildPipelineRunner::StageRunResult first_result =
			FoundryBuildPipelineRunner::run_stage(ProjectBuildPipelineConfig::STAGE_PRE_COMPILE);
	REQUIRE(first_result.is_success());
	REQUIRE_EQ(FileAccess::get_file_as_string(project.globalize(output_path)), "pre-ok");

	DirAccess::remove_absolute(project.globalize(output_path));
	REQUIRE_FALSE(FileAccess::exists(project.globalize(output_path)));

	const ProjectBuildPipelineStatusSnapshot missing_output_status =
			FoundryBuildPipelineRunner::get_stage_status(ProjectBuildPipelineConfig::STAGE_PRE_COMPILE);
	REQUIRE_EQ(missing_output_status.state, ProjectBuildPipelineStatus::STATE_BLOCKED);
	REQUIRE_FALSE(missing_output_status.diagnostics.is_empty());
	CHECK_EQ(missing_output_status.diagnostics[0].dirty_reason, ProjectBuildState::DIRTY_OUTPUT_MISSING);

	const FoundryBuildPipelineRunner::StageRunResult second_result =
			FoundryBuildPipelineRunner::run_stage(ProjectBuildPipelineConfig::STAGE_PRE_COMPILE);
	CHECK(second_result.is_success());
	CHECK(second_result.ran_any_task);
	CHECK_EQ(FileAccess::get_file_as_string(project.globalize(output_path)), "pre-ok");
}

TEST_CASE("[Modules][FoundryScript][BuildPipelineRunner] Provider diagnostics block task execution") {
	ScopedPipelineProject project("build_pipeline_runner_provider_block");
	ProjectBuildTrustStore::set_cli_trusted_execution(true);

	const String addon_dir = project.globalize("res://addons/provider_collision");
	const Error addon_dir_err = DirAccess::make_dir_recursive_absolute(addon_dir);
	REQUIRE_EQ(addon_dir_err, OK);

	Ref<ConfigFile> addon_config;
	addon_config.instantiate();
	addon_config->set_value("build_tasks", "providers", pipeline_args("command"));
	addon_config->set_value("build_tasks/providers/command", "script", "res://addons/provider_collision/provider.fs");
	addon_config->set_value("build_tasks/providers/command", "class_name", "ProviderCollision");
	REQUIRE_EQ(addon_config->save(project.globalize("res://addons/provider_collision/plugin.cfg")), OK);

	ProjectSettings::get_singleton()->set_setting(
			"editor_plugins/enabled", pipeline_args("res://addons/provider_collision/plugin.cfg"));

	const String output_path = "res://generated/provider_block.txt";
	const String script =
			"import pathlib, sys\n"
			"pathlib.Path(sys.argv[1]).parent.mkdir(parents=True, exist_ok=True)\n"
			"pathlib.Path(sys.argv[1]).write_text('should-not-run')\n";
	project.save_config(make_pipeline_config(
			ProjectBuildPipelineConfig::STAGE_PRE_COMPILE, "pre_generate", script, output_path));

	const ProjectBuildPipelineStatusSnapshot status =
			FoundryBuildPipelineRunner::get_stage_status(ProjectBuildPipelineConfig::STAGE_PRE_COMPILE);
	REQUIRE_EQ(status.state, ProjectBuildPipelineStatus::STATE_BLOCKED);
	REQUIRE_FALSE(status.diagnostics.is_empty());
	CHECK_EQ(status.diagnostics[0].kind, ProjectBuildPipelineDiagnostic::KIND_PROVIDER);

	const FoundryBuildPipelineRunner::StageRunResult result =
			FoundryBuildPipelineRunner::run_stage(ProjectBuildPipelineConfig::STAGE_PRE_COMPILE);
	CHECK_FALSE(result.is_success());
	CHECK_FALSE(result.ran_any_task);
	CHECK_EQ(result.snapshot.state, ProjectBuildPipelineStatus::STATE_BLOCKED);
	CHECK_FALSE(FileAccess::exists(project.globalize(output_path)));
}

TEST_CASE("[Modules][FoundryScript][BuildPipelineRunner] Previous task failure retries on explicit stage run") {
	ScopedPipelineProject project("build_pipeline_runner_retry_failure");
	ProjectBuildTrustStore::set_cli_trusted_execution(true);

	const String output_path = "res://generated/pre.txt";
	const String flag_path = "res://generated/retry_flag.txt";
	const String absolute_flag_path = project.globalize(flag_path);
	const Error dir_err = DirAccess::make_dir_recursive_absolute(absolute_flag_path.get_base_dir());
	REQUIRE_EQ(dir_err, OK);
	{
		Ref<FileAccess> flag = FileAccess::open(absolute_flag_path, FileAccess::WRITE);
		REQUIRE(flag.is_valid());
		flag->store_string("fail");
	}

	const String script =
			"import pathlib, sys\n"
			"flag = pathlib.Path(sys.argv[2]).read_text().strip()\n"
			"if flag == 'fail':\n"
			"\tsys.stderr.write('transient failure')\n"
			"\tsys.exit(5)\n"
			"pathlib.Path(sys.argv[1]).parent.mkdir(parents=True, exist_ok=True)\n"
			"pathlib.Path(sys.argv[1]).write_text('pre-ok')\n";
	Ref<ConfigFile> config;
	config.instantiate();
	config->set_value("build", "enabled", true);
	config->set_value("build", "pre_compile", pipeline_args("pre_generate"));
	config->set_value("build/tasks/pre_generate", "provider", "command");
	config->set_value("build/tasks/pre_generate", "command", "python3");
	config->set_value("build/tasks/pre_generate", "args", pipeline_args("-c", script, output_path, flag_path));
	config->set_value("build/tasks/pre_generate", "outputs", pipeline_args(output_path));
	config->set_value("build/tasks/pre_generate", "timeout_seconds", 5);
	project.save_config(config);

	const FoundryBuildPipelineRunner::StageRunResult first_result =
			FoundryBuildPipelineRunner::run_stage(ProjectBuildPipelineConfig::STAGE_PRE_COMPILE);
	REQUIRE_FALSE(first_result.is_success());
	CHECK(first_result.ran_any_task);
	CHECK_EQ(first_result.snapshot.state, ProjectBuildPipelineStatus::STATE_BLOCKED);
	REQUIRE_FALSE(first_result.snapshot.diagnostics.is_empty());
	CHECK_EQ(first_result.snapshot.diagnostics[0].dirty_reason, ProjectBuildState::DIRTY_NONE);
	CHECK_EQ(first_result.snapshot.diagnostics[0].exit_code, 5);

	const ProjectBuildPipelineStatusSnapshot failed_status =
			FoundryBuildPipelineRunner::get_stage_status(ProjectBuildPipelineConfig::STAGE_PRE_COMPILE);
	REQUIRE_EQ(failed_status.state, ProjectBuildPipelineStatus::STATE_BLOCKED);
	REQUIRE_FALSE(failed_status.diagnostics.is_empty());
	CHECK_EQ(failed_status.diagnostics[0].dirty_reason, ProjectBuildState::DIRTY_PREVIOUS_FAILURE);

	const FoundryBuildPipelineRunner::StageRunResult automatic_result =
			FoundryBuildPipelineRunner::run_stage(ProjectBuildPipelineConfig::STAGE_PRE_COMPILE,
					FoundryBuildPipelineRunner::RUN_AUTOMATIC_DIRTY_TASKS);
	CHECK_FALSE(automatic_result.is_success());
	CHECK_FALSE(automatic_result.ran_any_task);
	CHECK_EQ(automatic_result.snapshot.state, ProjectBuildPipelineStatus::STATE_BLOCKED);
	REQUIRE_FALSE(automatic_result.snapshot.diagnostics.is_empty());
	CHECK_EQ(automatic_result.snapshot.diagnostics[0].dirty_reason, ProjectBuildState::DIRTY_PREVIOUS_FAILURE);

	{
		Ref<FileAccess> flag = FileAccess::open(absolute_flag_path, FileAccess::WRITE);
		REQUIRE(flag.is_valid());
		flag->store_string("ok");
	}

	const FoundryBuildPipelineRunner::StageRunResult second_result =
			FoundryBuildPipelineRunner::run_stage(ProjectBuildPipelineConfig::STAGE_PRE_COMPILE);
	CHECK(second_result.is_success());
	CHECK(second_result.ran_any_task);
	CHECK_EQ(FileAccess::get_file_as_string(project.globalize(output_path)), "pre-ok");
}

TEST_CASE("[Modules][FoundryScript][BuildPipelineRunner] Post-compile failure blocks requested stage") {
	ScopedPipelineProject project("build_pipeline_runner_post_failure");
	ProjectBuildTrustStore::set_cli_trusted_execution(true);

	const String script =
			"import sys\n"
			"sys.stderr.write('post failed')\n"
			"sys.exit(7)\n";
	project.save_config(make_pipeline_config(
			ProjectBuildPipelineConfig::STAGE_POST_COMPILE, "post_verify", script));

	const FoundryBuildPipelineRunner::StageRunResult result =
			FoundryBuildPipelineRunner::run_stage(ProjectBuildPipelineConfig::STAGE_POST_COMPILE);
	CHECK_FALSE(result.is_success());
	CHECK(result.ran_any_task);
	CHECK_EQ(result.snapshot.state, ProjectBuildPipelineStatus::STATE_BLOCKED);
	REQUIRE_FALSE(result.snapshot.diagnostics.is_empty());
	CHECK_EQ(result.snapshot.diagnostics[0].kind, ProjectBuildPipelineDiagnostic::KIND_TASK);
	CHECK_EQ(result.snapshot.diagnostics[0].task_name, "post_verify");
	CHECK_EQ(result.snapshot.diagnostics[0].exit_code, 7);
	CHECK(result.snapshot.diagnostics[0].stderr_tail.contains("post failed"));
}

TEST_CASE("[Modules][FoundryScript][BuildPipelineRunner] End-to-end command fixture generates deterministic source") {
	ScopedPipelineProject project("build_pipeline_runner_foundryproto_fixture");

	project.write_file("res://proto/player.proto", read_build_pipeline_fixture("foundryproto/player.proto"));
	project.write_file("res://tools/foundryproto_fixture.py",
			read_build_pipeline_fixture("foundryproto/foundryproto_fixture.py"));

	PackedStringArray generate_args;
	generate_args.push_back("res://tools/foundryproto_fixture.py");
	generate_args.push_back("--input");
	generate_args.push_back("res://proto/player.proto");
	generate_args.push_back("--output");
	generate_args.push_back("res://generated/protobuf/player_proto.fs");
	generate_args.push_back("--manifest");
	generate_args.push_back("res://generated/protobuf/manifest.txt");
	generate_args.push_back("--package");
	generate_args.push_back("game.net");

	const String post_compile_script =
			"import pathlib, sys\n"
			"generated = pathlib.Path(sys.argv[1]).read_text(encoding='utf-8')\n"
			"manifest = pathlib.Path(sys.argv[2]).read_text(encoding='utf-8')\n"
			"assert 'class_name PlayerProto extends RefCounted' in generated\n"
			"assert 'messages=Player' in manifest\n"
			"pathlib.Path(sys.argv[3]).parent.mkdir(parents=True, exist_ok=True)\n"
			"pathlib.Path(sys.argv[3]).write_text('report=ok\\nclass=PlayerProto\\nmanifest=Player\\n', encoding='utf-8')\n";

	Ref<ConfigFile> config;
	config.instantiate();
	config->set_value("build", "enabled", true);
	config->set_value("build", "pre_compile", pipeline_args("generate_player_proto"));
	config->set_value("build", "post_compile", pipeline_args("verify_generated_manifest"));

	config->set_value("build/tasks/generate_player_proto", "provider", "command");
	config->set_value("build/tasks/generate_player_proto", "command", "python3");
	config->set_value("build/tasks/generate_player_proto", "args", generate_args);
	config->set_value("build/tasks/generate_player_proto", "inputs",
			pipeline_args("res://proto/player.proto", "res://tools/foundryproto_fixture.py"));
	config->set_value("build/tasks/generate_player_proto", "outputs",
			pipeline_args("res://generated/protobuf/player_proto.fs", "res://generated/protobuf/manifest.txt"));
	config->set_value("build/tasks/generate_player_proto", "working_directory", "res://");
	config->set_value("build/tasks/generate_player_proto", "tool_version_command",
			pipeline_args("python3", "res://tools/foundryproto_fixture.py", "--version"));
	config->set_value("build/tasks/generate_player_proto", "timeout_seconds", 5);

	config->set_value("build/tasks/verify_generated_manifest", "provider", "command");
	config->set_value("build/tasks/verify_generated_manifest", "command", "python3");
	config->set_value("build/tasks/verify_generated_manifest", "args",
			pipeline_args("-c", post_compile_script, "res://generated/protobuf/player_proto.fs",
					"res://generated/protobuf/manifest.txt"));
	PackedStringArray post_args = config->get_value("build/tasks/verify_generated_manifest", "args");
	post_args.push_back("res://build/protobuf_report.txt");
	config->set_value("build/tasks/verify_generated_manifest", "args", post_args);
	config->set_value("build/tasks/verify_generated_manifest", "inputs",
			pipeline_args("res://generated/protobuf/player_proto.fs", "res://generated/protobuf/manifest.txt"));
	config->set_value("build/tasks/verify_generated_manifest", "outputs",
			pipeline_args("res://build/protobuf_report.txt"));
	config->set_value("build/tasks/verify_generated_manifest", "working_directory", "res://");
	config->set_value("build/tasks/verify_generated_manifest", "timeout_seconds", 5);
	project.save_config(config);

	const ProjectBuildPipelineStatusSnapshot untrusted_status =
			FoundryBuildPipelineRunner::get_stage_status(ProjectBuildPipelineConfig::STAGE_PRE_COMPILE);
	REQUIRE_EQ(untrusted_status.state, ProjectBuildPipelineStatus::STATE_UNTRUSTED);
	CHECK(FoundryBuildPipelineRunner::status_blocks_flow(untrusted_status));
	CHECK_FALSE(FileAccess::exists(project.globalize("res://generated/protobuf/player_proto.fs")));

	ProjectBuildTrustStore::set_cli_trusted_execution(true);

	const FoundryBuildPipelineRunner::StageRunResult pre_result =
			FoundryBuildPipelineRunner::run_stage(ProjectBuildPipelineConfig::STAGE_PRE_COMPILE);
	REQUIRE_MESSAGE(pre_result.is_success(), "pre_compile command fixture should generate deterministic source.");
	CHECK(pre_result.ran_any_task);
	CHECK_EQ(FileAccess::get_file_as_string(project.globalize("res://generated/protobuf/player_proto.fs")),
			read_build_pipeline_fixture("foundryproto/expected/player_proto.fs"));
	CHECK_EQ(FileAccess::get_file_as_string(project.globalize("res://generated/protobuf/manifest.txt")),
			read_build_pipeline_fixture("foundryproto/expected/manifest.txt"));

	const ProjectBuildPipelineStatusSnapshot clean_pre_status =
			FoundryBuildPipelineRunner::get_stage_status(ProjectBuildPipelineConfig::STAGE_PRE_COMPILE);
	CHECK_EQ(clean_pre_status.state, ProjectBuildPipelineStatus::STATE_CLEAN);
	CHECK_FALSE(FoundryBuildPipelineRunner::status_blocks_flow(clean_pre_status));

	const FoundryBuildPipelineRunner::StageRunResult post_result =
			FoundryBuildPipelineRunner::run_stage(ProjectBuildPipelineConfig::STAGE_POST_COMPILE);
	REQUIRE_MESSAGE(post_result.is_success(), "post_compile command fixture should consume generated source.");
	CHECK(post_result.ran_any_task);
	CHECK_EQ(FileAccess::get_file_as_string(project.globalize("res://build/protobuf_report.txt")),
			read_build_pipeline_fixture("foundryproto/expected/protobuf_report.txt"));

	const FoundryBuildPipelineRunner::StageRunResult clean_pre_result =
			FoundryBuildPipelineRunner::run_stage(ProjectBuildPipelineConfig::STAGE_PRE_COMPILE);
	CHECK(clean_pre_result.is_success());
	CHECK_FALSE(clean_pre_result.ran_any_task);
}

} // namespace FSTests
