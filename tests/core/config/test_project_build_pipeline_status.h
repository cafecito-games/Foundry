/**************************************************************************/
/*  test_project_build_pipeline_status.h                                  */
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

namespace TestProjectBuildPipelineStatus {

struct ScopedPipelineProject {
	String old_resource_path;
	String old_project_data_dir_name;
	String root_path;

	explicit ScopedPipelineProject(const String &p_name) {
		old_resource_path = TestProjectSettingsInternalsAccessor::resource_path();
		old_project_data_dir_name = TestProjectSettingsInternalsAccessor::project_data_dir_name();
		root_path = TestUtils::get_temp_path(p_name + "_" + itos(OS::get_singleton()->get_ticks_usec()));
		const Error err = DirAccess::make_dir_recursive_absolute(root_path);
		CHECK_EQ(err, OK);
		if (err == OK) {
			TestProjectSettingsInternalsAccessor::resource_path() = root_path;
			TestProjectSettingsInternalsAccessor::project_data_dir_name() = "." + ProjectSettings::PROJECT_DATA_DIR_NAME_SUFFIX;
		}
	}

	~ScopedPipelineProject() {
		ProjectBuildTrustStore trust;
		remove_recursive(trust.get_trust_path().get_base_dir());
		remove_recursive(root_path);
		TestProjectSettingsInternalsAccessor::project_data_dir_name() = old_project_data_dir_name;
		TestProjectSettingsInternalsAccessor::resource_path() = old_resource_path;
	}

	String globalize(const String &p_path) const {
		return ProjectSettings::get_singleton()->globalize_path(p_path);
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

static Ref<ConfigFile> parse_build_config(const String &p_text) {
	Ref<ConfigFile> config;
	config.instantiate();
	CHECK_EQ(config->parse(p_text), OK);
	return config;
}

static PackedStringArray make_paths(const String &p_a) {
	PackedStringArray paths;
	paths.push_back(p_a);
	return paths;
}

static ProjectBuildPipelineConfig make_command_pipeline() {
	Ref<ConfigFile> config = parse_build_config(
			"[build]\n"
			"enabled=true\n"
			"pre_compile=PackedStringArray(\"generate\")\n"
			"\n"
			"[build/tasks/generate]\n"
			"provider=\"command\"\n"
			"command=\"python3\"\n"
			"outputs=PackedStringArray(\"res://generated/out.fs\")\n");

	ProjectBuildPipelineConfig pipeline;
	CHECK_EQ(pipeline.load_from_config_file(config), OK);
	return pipeline;
}

TEST_CASE("[ProjectBuildPipelineTrust] decisions persist outside project file") {
	ScopedPipelineProject project("project_build_pipeline_trust");
	project.write_file("res://.foundry/build_trust.cfg",
			"[build_trust]\n"
			"version=1\n"
			"trusted=true\n");

	ProjectBuildTrustStore trust;
	CHECK_FALSE(trust.get_trust_path().begins_with("res://"));
	CHECK_FALSE(trust.get_trust_path().begins_with(project.root_path));
	CHECK_EQ(trust.load(), OK);
	CHECK_FALSE(trust.is_project_trusted());

	trust.set_project_trusted(true);
	CHECK_EQ(trust.save(), OK);
	CHECK(FileAccess::exists(trust.get_trust_path()));
	CHECK_FALSE(FileAccess::exists(project.globalize("res://project.foundry")));

	ProjectBuildTrustStore reloaded;
	CHECK_EQ(reloaded.load(), OK);
	CHECK(reloaded.is_project_trusted());
}

TEST_CASE("[ProjectBuildPipelineStatus] reports untrusted dirty blocked and recovered states") {
	ScopedPipelineProject project("project_build_pipeline_states");
	project.write_file("res://generated/out.fs", "class_name Generated\n");

	ProjectBuildPipelineConfig pipeline = make_command_pipeline();

	FoundryBuildTaskRegistry registry;
	registry.register_builtin_providers();

	ProjectBuildState state;
	ProjectBuildTrustStore trust;
	CHECK_FALSE(trust.is_project_trusted());

	ProjectBuildPipelineStatusSnapshot snapshot =
			ProjectBuildPipelineStatus::evaluate(pipeline, registry, state, trust);
	CHECK_EQ(snapshot.state, ProjectBuildPipelineStatus::STATE_UNTRUSTED);
	CHECK(snapshot.blocks_downstream_indexing);
	REQUIRE_EQ(snapshot.diagnostics.size(), 1);
	CHECK_EQ(snapshot.diagnostics[0].task_name, "generate");
	CHECK_EQ(snapshot.diagnostics[0].provider_id, "command");

	trust.set_project_trusted(true);
	snapshot = ProjectBuildPipelineStatus::evaluate(pipeline, registry, state, trust);
	CHECK_EQ(snapshot.state, ProjectBuildPipelineStatus::STATE_DIRTY);
	CHECK(snapshot.blocks_downstream_indexing);
	REQUIRE_EQ(snapshot.diagnostics.size(), 1);
	CHECK_EQ(snapshot.diagnostics[0].dirty_reason, ProjectBuildState::DIRTY_MISSING_STATE);

	state.record_task_result("generate", "fingerprint-v1", make_paths("res://generated/out.fs"), false);
	snapshot = ProjectBuildPipelineStatus::evaluate(pipeline, registry, state, trust);
	CHECK_EQ(snapshot.state, ProjectBuildPipelineStatus::STATE_BLOCKED);
	CHECK(snapshot.blocks_downstream_indexing);
	REQUIRE_EQ(snapshot.diagnostics.size(), 1);
	CHECK_EQ(snapshot.diagnostics[0].dirty_reason, ProjectBuildState::DIRTY_PREVIOUS_FAILURE);

	state.record_task_result("generate", "fingerprint-v1", make_paths("res://generated/out.fs"), true);
	snapshot = ProjectBuildPipelineStatus::evaluate(pipeline, registry, state, trust);
	CHECK_EQ(snapshot.state, ProjectBuildPipelineStatus::STATE_CLEAN);
	CHECK_FALSE(snapshot.blocks_downstream_indexing);
	CHECK(snapshot.diagnostics.is_empty());
}

TEST_CASE("[ProjectBuildPipelineStatus] provider diagnostics are structured for editor and LSP callers") {
	ProjectBuildPipelineConfig pipeline = make_command_pipeline();

	FoundryBuildTaskRegistry registry;
	registry.register_builtin_providers();
	FoundryBuildTaskRegistry::Diagnostic provider_diagnostic;
	provider_diagnostic.kind = FoundryBuildTaskRegistry::DIAGNOSTIC_LOADER_FAILURE;
	provider_diagnostic.provider_id = "command";
	provider_diagnostic.task_name = "generate";
	provider_diagnostic.message = "provider failed";
	provider_diagnostic.source.type = FoundryBuildTaskRegistry::SOURCE_PROJECT;
	provider_diagnostic.source.path = "res://project.foundry";
	provider_diagnostic.source.section = "build/tasks/generate";
	provider_diagnostic.source.key = "provider";

	ProjectBuildTrustStore trust;
	trust.set_project_trusted(true);

	ProjectBuildState state;
	state.record_task_result("generate", "fingerprint-v1", make_paths("res://generated/out.fs"), true);

	ProjectBuildPipelineStatusSnapshot snapshot =
			ProjectBuildPipelineStatus::evaluate(pipeline, registry, state, trust, false, make_paths("fingerprint-v1"), Vector<FoundryBuildTaskRegistry::Diagnostic>({ provider_diagnostic }));
	CHECK_EQ(snapshot.state, ProjectBuildPipelineStatus::STATE_BLOCKED);
	REQUIRE_EQ(snapshot.diagnostics.size(), 1);
	const ProjectBuildPipelineDiagnostic &diagnostic = snapshot.diagnostics[0];
	CHECK_EQ(diagnostic.task_name, "generate");
	CHECK_EQ(diagnostic.provider_id, "command");
	CHECK_EQ(diagnostic.provider_source_path, "res://project.foundry");
	CHECK_EQ(diagnostic.provider_source_section, "build/tasks/generate");
	CHECK_EQ(diagnostic.provider_source_key, "provider");

	Dictionary payload = diagnostic.to_dictionary();
	CHECK_EQ(String(payload["task_name"]), "generate");
	CHECK_EQ(String(payload["provider_id"]), "command");
	CHECK_EQ(String(payload["source_path"]), "res://project.foundry");
	CHECK_EQ(String(payload["source_section"]), "build/tasks/generate");
	CHECK_EQ(String(payload["source_key"]), "provider");
}

TEST_CASE("[ProjectBuildPipelineStatus] inactive task definitions do not block active pipeline status") {
	ScopedPipelineProject project("project_build_pipeline_inactive_tasks");
	project.write_file("res://generated/out.fs", "class_name Generated\n");

	Ref<ConfigFile> config = parse_build_config(
			"[build]\n"
			"enabled=true\n"
			"pre_compile=PackedStringArray(\"generate\")\n"
			"\n"
			"[build/tasks/generate]\n"
			"provider=\"command\"\n"
			"command=\"python3\"\n"
			"outputs=PackedStringArray(\"res://generated/out.fs\")\n"
			"\n"
			"[build/tasks/disabled_missing]\n"
			"enabled=false\n"
			"provider=\"missing.provider\"\n"
			"outputs=PackedStringArray(\"res://generated/disabled.fs\")\n");

	ProjectBuildPipelineConfig pipeline;
	CHECK_EQ(pipeline.load_from_config_file(config), OK);

	FoundryBuildTaskRegistry registry;
	registry.register_builtin_providers();
	ProjectBuildTrustStore trust;
	trust.set_project_trusted(true);
	ProjectBuildState state;
	state.record_task_result("generate", "fingerprint-v1", make_paths("res://generated/out.fs"), true);

	const ProjectBuildPipelineStatusSnapshot snapshot =
			ProjectBuildPipelineStatus::evaluate(pipeline, registry, state, trust);
	CHECK_EQ(snapshot.state, ProjectBuildPipelineStatus::STATE_CLEAN);
	CHECK_FALSE(snapshot.blocks_downstream_indexing);
	CHECK(snapshot.diagnostics.is_empty());
}

TEST_CASE("[ProjectBuildPipelineStatus] running state is distinct from dirty and blocked") {
	ProjectBuildPipelineConfig pipeline = make_command_pipeline();
	FoundryBuildTaskRegistry registry;
	registry.register_builtin_providers();
	ProjectBuildState state;
	ProjectBuildTrustStore trust;
	trust.set_project_trusted(true);

	ProjectBuildPipelineStatusSnapshot snapshot =
			ProjectBuildPipelineStatus::evaluate(pipeline, registry, state, trust, true);
	CHECK_EQ(snapshot.state, ProjectBuildPipelineStatus::STATE_RUNNING);
	CHECK(snapshot.blocks_downstream_indexing);
}

} // namespace TestProjectBuildPipelineStatus
