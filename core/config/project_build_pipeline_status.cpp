/**************************************************************************/
/*  project_build_pipeline_status.cpp                                     */
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

#include "project_build_pipeline_status.h"

#include "core/config/project_settings.h"
#include "core/io/config_file.h"
#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/os/os.h"

namespace {

static const char *BUILD_TRUST_SECTION = "build_trust";
static const int BUILD_TRUST_VERSION = 1;
static const char *COMMAND_PROVIDER_ID = "command";

static String _source_type_name(FoundryBuildTaskRegistry::SourceType p_type) {
	switch (p_type) {
		case FoundryBuildTaskRegistry::SOURCE_NATIVE:
			return "native";
		case FoundryBuildTaskRegistry::SOURCE_ADDON:
			return "addon";
		case FoundryBuildTaskRegistry::SOURCE_PROJECT:
			return "project";
	}

	return "unknown";
}

static bool _source_requires_trust(const FoundryBuildTaskRegistry::ProviderEntry *p_provider) {
	if (p_provider == nullptr) {
		return false;
	}
	if (p_provider->id == COMMAND_PROVIDER_ID) {
		return true;
	}
	return p_provider->source.type == FoundryBuildTaskRegistry::SOURCE_ADDON ||
			p_provider->source.type == FoundryBuildTaskRegistry::SOURCE_PROJECT;
}

static bool _task_requires_trust(const ProjectBuildPipelineConfig::TaskDefinition &p_task,
		const FoundryBuildTaskRegistry &p_registry) {
	if (p_task.provider == COMMAND_PROVIDER_ID) {
		return true;
	}
	return _source_requires_trust(p_registry.get_provider(p_task.provider));
}

static void _copy_source(ProjectBuildPipelineDiagnostic &r_diagnostic,
		const FoundryBuildTaskRegistry::SourceMetadata &p_source) {
	r_diagnostic.provider_source_type = _source_type_name(p_source.type);
	r_diagnostic.provider_source_identifier = p_source.identifier;
	r_diagnostic.provider_source_path = p_source.path;
	r_diagnostic.provider_source_section = p_source.section;
	r_diagnostic.provider_source_key = p_source.key;
	r_diagnostic.file = p_source.path;
}

static void _copy_conflicting_source(ProjectBuildPipelineDiagnostic &r_diagnostic,
		const FoundryBuildTaskRegistry::SourceMetadata &p_source) {
	r_diagnostic.conflicting_provider_source_type = _source_type_name(p_source.type);
	r_diagnostic.conflicting_provider_source_identifier = p_source.identifier;
	r_diagnostic.conflicting_provider_source_path = p_source.path;
	r_diagnostic.conflicting_provider_source_section = p_source.section;
	r_diagnostic.conflicting_provider_source_key = p_source.key;
}

static ProjectBuildPipelineDiagnostic _provider_diagnostic_to_pipeline(
		const FoundryBuildTaskRegistry::Diagnostic &p_diagnostic) {
	ProjectBuildPipelineDiagnostic diagnostic;
	diagnostic.kind = ProjectBuildPipelineDiagnostic::KIND_PROVIDER;
	diagnostic.provider_id = p_diagnostic.provider_id;
	diagnostic.task_name = p_diagnostic.task_name;
	diagnostic.message = p_diagnostic.message;
	_copy_source(diagnostic, p_diagnostic.source);
	if (!p_diagnostic.conflicting_source.section.is_empty() || !p_diagnostic.conflicting_source.path.is_empty()) {
		_copy_conflicting_source(diagnostic, p_diagnostic.conflicting_source);
	}
	return diagnostic;
}

static ProjectBuildPipelineDiagnostic _trust_diagnostic(const ProjectBuildPipelineConfig::TaskDefinition &p_task,
		const FoundryBuildTaskRegistry::ProviderEntry *p_provider) {
	ProjectBuildPipelineDiagnostic diagnostic;
	diagnostic.kind = ProjectBuildPipelineDiagnostic::KIND_TRUST;
	diagnostic.task_name = p_task.name;
	diagnostic.provider_id = p_task.provider;
	diagnostic.command = p_task.command;
	diagnostic.message = vformat("Build task '%s' requires trusted execution before it can run automatically.",
			p_task.name);
	if (p_provider != nullptr) {
		_copy_source(diagnostic, p_provider->source);
	}
	return diagnostic;
}

static ProjectBuildPipelineDiagnostic _dirty_diagnostic(const ProjectBuildPipelineConfig::TaskDefinition &p_task,
		const FoundryBuildTaskRegistry::ProviderEntry *p_provider, const ProjectBuildState::DirtyStatus &p_status) {
	ProjectBuildPipelineDiagnostic diagnostic;
	diagnostic.kind = ProjectBuildPipelineDiagnostic::KIND_DIRTY;
	diagnostic.task_name = p_task.name;
	diagnostic.provider_id = p_task.provider;
	diagnostic.command = p_task.command;
	diagnostic.dirty_reason = p_status.reason;
	diagnostic.message = p_status.message;
	if (p_provider != nullptr) {
		_copy_source(diagnostic, p_provider->source);
	}
	return diagnostic;
}

static PackedStringArray _enabled_tasks_in_pipeline_order(const ProjectBuildPipelineConfig &p_config) {
	PackedStringArray tasks;
	tasks.append_array(p_config.get_enabled_stage_tasks(ProjectBuildPipelineConfig::STAGE_PRE_COMPILE));
	tasks.append_array(p_config.get_enabled_stage_tasks(ProjectBuildPipelineConfig::STAGE_POST_COMPILE));
	return tasks;
}

static bool _stage_has_task(const PackedStringArray &p_stage_tasks, const String &p_task_name) {
	for (int i = 0; i < p_stage_tasks.size(); i++) {
		if (p_stage_tasks[i] == p_task_name) {
			return true;
		}
	}
	return false;
}

static String _fingerprint_for_task(const ProjectBuildState &p_state,
		const PackedStringArray &p_fingerprints, int p_task_index, const String &p_task_name) {
	if (p_task_index >= 0 && p_task_index < p_fingerprints.size()) {
		return p_fingerprints[p_task_index];
	}

	const ProjectBuildState::TaskRecord *record = p_state.get_task_record(p_task_name);
	return record != nullptr ? record->fingerprint : String();
}

static Vector<FoundryBuildTaskRegistry::Diagnostic> _validate_enabled_task_providers(
		const ProjectBuildPipelineConfig &p_config, const FoundryBuildTaskRegistry &p_registry) {
	Vector<FoundryBuildTaskRegistry::Diagnostic> provider_diagnostics;
	PackedStringArray checked_tasks;
	const PackedStringArray ordered_tasks = _enabled_tasks_in_pipeline_order(p_config);
	for (int i = 0; i < ordered_tasks.size(); i++) {
		const String task_name = ordered_tasks[i];
		if (checked_tasks.has(task_name)) {
			continue;
		}
		checked_tasks.push_back(task_name);

		const ProjectBuildPipelineConfig::TaskDefinition *task = p_config.get_task(task_name);
		if (task == nullptr || task->provider.is_empty() || p_registry.has_provider(task->provider)) {
			continue;
		}

		FoundryBuildTaskRegistry::Diagnostic diagnostic;
		diagnostic.kind = FoundryBuildTaskRegistry::DIAGNOSTIC_MISSING_PROVIDER;
		diagnostic.provider_id = task->provider;
		diagnostic.task_name = task_name;
		diagnostic.source.type = FoundryBuildTaskRegistry::SOURCE_PROJECT;
		diagnostic.source.identifier = "project";
		diagnostic.source.path = "res://project.foundry";
		diagnostic.source.section = "build/tasks/" + task_name;
		diagnostic.source.key = "provider";
		diagnostic.message = vformat(
				"Build task '%s' references provider id '%s', but no native, addon, or project provider descriptor registered it.",
				task_name, task->provider);
		provider_diagnostics.push_back(diagnostic);
	}
	return provider_diagnostics;
}

} // namespace

bool ProjectBuildTrustStore::cli_trusted_execution = false;

String ProjectBuildTrustStore::_default_trust_path() {
	const String project_root = ProjectSettings::get_singleton()->get_resource_path().simplify_path();
	const String project_name = OS::get_singleton()->get_safe_dir_name(project_root.get_file().is_empty() ? "unnamed_project" : project_root.get_file());
	const String project_key = project_name + "-" + project_root.sha256_text().substr(0, 16);
	return OS::get_singleton()->get_data_path().path_join(OS::get_singleton()->get_godot_dir_name()).path_join("project_build_trust").path_join(project_key).path_join("build_trust.cfg");
}

ProjectBuildTrustStore::ProjectBuildTrustStore() :
		trust_path(_default_trust_path()) {
}

ProjectBuildTrustStore::ProjectBuildTrustStore(const String &p_trust_path) :
		trust_path(p_trust_path) {
}

Error ProjectBuildTrustStore::load() {
	project_trusted = false;
	load_error = OK;

	if (!FileAccess::exists(trust_path)) {
		return OK;
	}

	Ref<ConfigFile> config;
	config.instantiate();
	const Error err = config->load(trust_path);
	if (err != OK) {
		load_error = err;
		return err;
	}

	if (!config->has_section_key(BUILD_TRUST_SECTION, "version") ||
			config->get_value(BUILD_TRUST_SECTION, "version").get_type() != Variant::INT ||
			int(config->get_value(BUILD_TRUST_SECTION, "version")) != BUILD_TRUST_VERSION) {
		load_error = ERR_FILE_CORRUPT;
		return load_error;
	}

	if (config->has_section_key(BUILD_TRUST_SECTION, "trusted")) {
		const Variant trusted_value = config->get_value(BUILD_TRUST_SECTION, "trusted");
		if (trusted_value.get_type() != Variant::BOOL) {
			load_error = ERR_FILE_CORRUPT;
			return load_error;
		}
		project_trusted = trusted_value;
	}

	return OK;
}

Error ProjectBuildTrustStore::save() const {
	const Error dir_err = DirAccess::make_dir_recursive_absolute(trust_path.get_base_dir());
	if (dir_err != OK) {
		return dir_err;
	}

	Ref<ConfigFile> config;
	config.instantiate();
	config->set_value(BUILD_TRUST_SECTION, "version", BUILD_TRUST_VERSION);
	config->set_value(BUILD_TRUST_SECTION, "trusted", project_trusted);
	return config->save(trust_path);
}

void ProjectBuildTrustStore::set_cli_trusted_execution(bool p_trusted) {
	cli_trusted_execution = p_trusted;
}

bool ProjectBuildTrustStore::is_cli_trusted_execution() {
	return cli_trusted_execution;
}

Dictionary ProjectBuildPipelineDiagnostic::to_dictionary() const {
	Dictionary payload;
	payload["kind"] = kind;
	payload["task_name"] = task_name;
	payload["provider_id"] = provider_id;
	payload["provider_source_type"] = provider_source_type;
	payload["source_identifier"] = provider_source_identifier;
	payload["source_path"] = provider_source_path;
	payload["source_section"] = provider_source_section;
	payload["source_key"] = provider_source_key;
	payload["conflicting_source_type"] = conflicting_provider_source_type;
	payload["conflicting_source_identifier"] = conflicting_provider_source_identifier;
	payload["conflicting_source_path"] = conflicting_provider_source_path;
	payload["conflicting_source_section"] = conflicting_provider_source_section;
	payload["conflicting_source_key"] = conflicting_provider_source_key;
	payload["command"] = command;
	payload["exit_code"] = exit_code;
	payload["stdout_tail"] = stdout_tail;
	payload["stderr_tail"] = stderr_tail;
	payload["message"] = message;
	payload["file"] = file;
	payload["line"] = line;
	payload["column"] = column;
	payload["dirty_reason"] = dirty_reason;
	return payload;
}

ProjectBuildPipelineStatusSnapshot::ProjectBuildPipelineStatusSnapshot() :
		state(ProjectBuildPipelineStatus::STATE_DISABLED) {
}

ProjectBuildPipelineStatusSnapshot ProjectBuildPipelineStatus::evaluate(const ProjectBuildPipelineConfig &p_config,
		const FoundryBuildTaskRegistry &p_registry, const ProjectBuildState &p_state,
		const ProjectBuildTrustStore &p_trust, bool p_running,
		const PackedStringArray &p_current_fingerprints,
		const Vector<FoundryBuildTaskRegistry::Diagnostic> &p_provider_diagnostics) {
	ProjectBuildPipelineStatusSnapshot snapshot;

	if (p_config.get_status() == ProjectBuildPipelineConfig::STATUS_DISABLED) {
		snapshot.state = STATE_DISABLED;
		return snapshot;
	}

	const PackedStringArray ordered_tasks = _enabled_tasks_in_pipeline_order(p_config);
	const PackedStringArray pre_compile_tasks = p_config.get_enabled_stage_tasks(ProjectBuildPipelineConfig::STAGE_PRE_COMPILE);

	for (int i = 0; i < ordered_tasks.size(); i++) {
		const ProjectBuildPipelineConfig::TaskDefinition *task = p_config.get_task(ordered_tasks[i]);
		if (task == nullptr) {
			continue;
		}
		const FoundryBuildTaskRegistry::ProviderEntry *provider = p_registry.get_provider(task->provider);
		if (_task_requires_trust(*task, p_registry) && !p_trust.is_project_trusted()) {
			snapshot.state = STATE_UNTRUSTED;
			snapshot.blocks_downstream_indexing = snapshot.blocks_downstream_indexing ||
					_stage_has_task(pre_compile_tasks, task->name);
			snapshot.diagnostics.push_back(_trust_diagnostic(*task, provider));
		}
	}
	if (snapshot.state == STATE_UNTRUSTED) {
		return snapshot;
	}

	Vector<FoundryBuildTaskRegistry::Diagnostic> all_provider_diagnostics = p_registry.get_diagnostics();
	all_provider_diagnostics.append_array(_validate_enabled_task_providers(p_config, p_registry));
	all_provider_diagnostics.append_array(p_provider_diagnostics);
	if (!all_provider_diagnostics.is_empty()) {
		snapshot.state = STATE_BLOCKED;
		snapshot.blocks_downstream_indexing = true;
		for (const FoundryBuildTaskRegistry::Diagnostic &diagnostic : all_provider_diagnostics) {
			snapshot.diagnostics.push_back(_provider_diagnostic_to_pipeline(diagnostic));
		}
		return snapshot;
	}

	if (p_running) {
		snapshot.state = STATE_RUNNING;
		snapshot.blocks_downstream_indexing = true;
		return snapshot;
	}

	bool dirty = false;
	bool blocked = false;
	for (int i = 0; i < ordered_tasks.size(); i++) {
		const ProjectBuildPipelineConfig::TaskDefinition *task = p_config.get_task(ordered_tasks[i]);
		if (task == nullptr) {
			continue;
		}

		const String fingerprint = _fingerprint_for_task(p_state, p_current_fingerprints, i, task->name);
		const ProjectBuildState::DirtyStatus task_status =
				p_state.get_task_dirty_status(task->name, fingerprint, task->outputs);
		if (!task_status.dirty) {
			continue;
		}

		const bool task_blocked = task_status.reason == ProjectBuildState::DIRTY_PREVIOUS_FAILURE ||
				task_status.reason == ProjectBuildState::DIRTY_UNREADABLE_STATE ||
				task_status.reason == ProjectBuildState::DIRTY_OUTPUT_MISSING;
		blocked = blocked || task_blocked;
		dirty = dirty || !task_blocked;
		snapshot.blocks_downstream_indexing = snapshot.blocks_downstream_indexing ||
				_stage_has_task(pre_compile_tasks, task->name);
		snapshot.diagnostics.push_back(_dirty_diagnostic(*task, p_registry.get_provider(task->provider), task_status));
	}

	if (blocked) {
		snapshot.state = STATE_BLOCKED;
	} else if (dirty) {
		snapshot.state = STATE_DIRTY;
	} else {
		snapshot.state = STATE_CLEAN;
		snapshot.blocks_downstream_indexing = false;
	}

	return snapshot;
}
