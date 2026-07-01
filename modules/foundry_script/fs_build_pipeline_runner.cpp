/**************************************************************************/
/*  fs_build_pipeline_runner.cpp                                          */
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

#include "fs_build_pipeline_runner.h"

#include "foundry_build_task.h"
#include "fs_build_task_bootstrap_loader.h"

#include "core/config/foundry_build_task_registry.h"
#include "core/config/project_build_state.h"
#include "core/config/project_settings.h"
#include "core/error/error_macros.h"
#include "core/io/config_file.h"
#include "core/io/file_access.h"
#include "core/string/print_string.h"
#include "core/templates/hash_set.h"

namespace {

struct PreparedStage {
	bool disabled = false;
	bool blocked = false;
	ProjectBuildPipelineConfig stage_config;
	FoundryBuildTaskRegistry available_registry;
	FoundryBuildTaskRegistry registry;
	Vector<FoundryBuildTaskRegistry::Diagnostic> provider_diagnostics;
	ProjectBuildState state;
	ProjectBuildTrustStore trust;
	ProjectBuildPipelineStatusSnapshot snapshot;
};

static void _register_available_build_task_providers(const ProjectBuildPipelineConfig &p_config,
		FoundryBuildTaskRegistry &r_registry) {
	r_registry.register_builtin_providers();
	ProjectSettings *project_settings = ProjectSettings::get_singleton();
	if (project_settings != nullptr && project_settings->has_setting("editor_plugins/enabled")) {
		r_registry.register_enabled_addon_metadata(project_settings->get("editor_plugins/enabled"));
	}
	r_registry.register_project_providers(p_config);
}

static PackedStringArray _enabled_stage_task_provider_ids(const ProjectBuildPipelineConfig &p_config,
		ProjectBuildPipelineConfig::Stage p_stage) {
	PackedStringArray provider_ids;
	const PackedStringArray stage_tasks = p_config.get_enabled_stage_tasks(p_stage);
	for (int i = 0; i < stage_tasks.size(); i++) {
		const ProjectBuildPipelineConfig::TaskDefinition *task = p_config.get_task(stage_tasks[i]);
		if (task == nullptr || task->provider.is_empty() || provider_ids.has(task->provider)) {
			continue;
		}
		provider_ids.push_back(task->provider);
	}
	return provider_ids;
}

static void _register_stage_task_providers(const ProjectBuildPipelineConfig &p_config,
		const FoundryBuildTaskRegistry &p_available_registry, ProjectBuildPipelineConfig::Stage p_stage,
		FoundryBuildTaskRegistry &r_registry) {
	r_registry.register_builtin_providers();

	const PackedStringArray provider_ids = _enabled_stage_task_provider_ids(p_config, p_stage);
	for (int i = 0; i < provider_ids.size(); i++) {
		const String provider_id = provider_ids[i];
		if (provider_id == "command" || r_registry.has_provider(provider_id)) {
			continue;
		}

		const FoundryBuildTaskRegistry::ProviderEntry *entry = p_available_registry.get_provider(provider_id);
		if (entry == nullptr) {
			continue;
		}

		ProjectBuildPipelineConfig::ProviderDescriptor descriptor;
		descriptor.id = entry->id;
		descriptor.script = entry->script;
		descriptor.class_name = entry->class_name;
		descriptor.display_name = entry->display_name;
		descriptor.description = entry->description;
		descriptor.addon = entry->addon;
		r_registry.register_provider_descriptor(descriptor, entry->source);
	}
}

static Vector<FoundryBuildTaskRegistry::Diagnostic> _provider_diagnostics_for_stage(
		const FoundryBuildTaskRegistry &p_available_registry, const ProjectBuildPipelineConfig &p_config,
		ProjectBuildPipelineConfig::Stage p_stage) {
	Vector<FoundryBuildTaskRegistry::Diagnostic> diagnostics;
	HashSet<String> stage_tasks;
	const PackedStringArray task_names = p_config.get_enabled_stage_tasks(p_stage);
	for (int i = 0; i < task_names.size(); i++) {
		stage_tasks.insert(task_names[i]);
	}

	HashSet<String> stage_providers;
	const PackedStringArray provider_ids = _enabled_stage_task_provider_ids(p_config, p_stage);
	for (int i = 0; i < provider_ids.size(); i++) {
		stage_providers.insert(provider_ids[i]);
	}

	for (const FoundryBuildTaskRegistry::Diagnostic &diagnostic : p_available_registry.get_diagnostics()) {
		if ((!diagnostic.task_name.is_empty() && stage_tasks.has(diagnostic.task_name)) ||
				(!diagnostic.provider_id.is_empty() && stage_providers.has(diagnostic.provider_id))) {
			diagnostics.push_back(diagnostic);
		}
	}
	return diagnostics;
}

static ProjectBuildPipelineStatusSnapshot _blocked_config_load_snapshot(const String &p_message) {
	ProjectBuildPipelineStatusSnapshot snapshot;
	snapshot.state = ProjectBuildPipelineStatus::STATE_BLOCKED;
	snapshot.blocks_downstream_indexing = true;

	ProjectBuildPipelineDiagnostic diagnostic;
	diagnostic.kind = ProjectBuildPipelineDiagnostic::KIND_TASK;
	diagnostic.file = "res://project.foundry";
	diagnostic.message = p_message;
	snapshot.diagnostics.push_back(diagnostic);
	return snapshot;
}

static ProjectBuildPipelineStatusSnapshot _blocked_config_validation_snapshot(
		const Vector<ProjectBuildPipelineConfig::ValidationError> &p_errors) {
	ProjectBuildPipelineStatusSnapshot snapshot;
	snapshot.state = ProjectBuildPipelineStatus::STATE_BLOCKED;
	snapshot.blocks_downstream_indexing = true;

	for (const ProjectBuildPipelineConfig::ValidationError &error : p_errors) {
		ProjectBuildPipelineDiagnostic diagnostic;
		diagnostic.kind = ProjectBuildPipelineDiagnostic::KIND_TASK;
		diagnostic.file = "res://project.foundry";
		diagnostic.provider_source_section = error.section;
		diagnostic.provider_source_key = error.key;
		diagnostic.message = error.message;
		if (!error.section.is_empty()) {
			diagnostic.message = vformat("%s/%s: %s", error.section, error.key, error.message);
		}
		snapshot.diagnostics.push_back(diagnostic);
	}

	return snapshot;
}

static Dictionary _build_task_options(const ProjectBuildPipelineConfig::TaskDefinition &p_task) {
	Dictionary options = p_task.options.duplicate(true);
	if (!p_task.command.is_empty()) {
		options["command"] = p_task.command;
	}
	if (!p_task.args.is_empty()) {
		options["args"] = p_task.args;
	}
	if (!p_task.working_directory.is_empty()) {
		options["working_directory"] = p_task.working_directory;
	}
	if (!p_task.environment.is_empty()) {
		options["environment"] = p_task.environment;
	}
	if (!p_task.inputs.is_empty()) {
		options["inputs"] = p_task.inputs;
	}
	if (!p_task.outputs.is_empty()) {
		options["outputs"] = p_task.outputs;
	}
	if (p_task.has_timeout_seconds) {
		options["timeout_seconds"] = p_task.timeout_seconds;
	}
	if (!p_task.tool_version_command.is_empty()) {
		options["tool_version_command"] = p_task.tool_version_command;
	}
	return options;
}

static Ref<FoundryBuildContext> _build_task_context(const ProjectBuildPipelineConfig::TaskDefinition &p_task,
		bool p_trusted_execution) {
	Ref<FoundryBuildContext> context;
	context.instantiate();
	context->set_provider_id(p_task.provider);
	context->set_task_name(p_task.name);
	context->set_project_config_path("res://project.foundry");
	context->set_trusted_execution(p_trusted_execution);
	context->set_options(_build_task_options(p_task));
	return context;
}

static String _current_task_fingerprint(const ProjectBuildPipelineConfig::TaskDefinition &p_task,
		const ProjectBuildState &p_state, const ProjectBuildTrustStore &p_trust) {
	const ProjectBuildState::TaskRecord *record = p_state.get_task_record(p_task.name);
	String fingerprint = record != nullptr ? record->fingerprint : String();
	if (p_task.provider == "command" && p_trust.is_project_trusted()) {
		Ref<FoundryCommandBuildTask> command_task;
		command_task.instantiate();

		String fingerprint_error;
		const String current_fingerprint = command_task->compute_fingerprint(
				_build_task_context(p_task, p_trust.is_project_trusted()), &fingerprint_error);
		fingerprint = current_fingerprint;
	}
	return fingerprint;
}

static void _append_stage_task_fingerprints(const ProjectBuildPipelineConfig &p_config,
		const ProjectBuildState &p_state, const ProjectBuildTrustStore &p_trust,
		ProjectBuildPipelineConfig::Stage p_stage, PackedStringArray &r_fingerprints) {
	const PackedStringArray stage_tasks = p_config.get_enabled_stage_tasks(p_stage);

	for (int i = 0; i < stage_tasks.size(); i++) {
		const ProjectBuildPipelineConfig::TaskDefinition *task = p_config.get_task(stage_tasks[i]);
		const String fingerprint = task != nullptr ? _current_task_fingerprint(*task, p_state, p_trust) : String();
		r_fingerprints.push_back(fingerprint);
	}
}

static PackedStringArray _current_pipeline_fingerprints(const ProjectBuildPipelineConfig &p_config,
		const ProjectBuildState &p_state, const ProjectBuildTrustStore &p_trust) {
	PackedStringArray fingerprints;
	_append_stage_task_fingerprints(p_config, p_state, p_trust,
			ProjectBuildPipelineConfig::STAGE_PRE_COMPILE, fingerprints);
	_append_stage_task_fingerprints(p_config, p_state, p_trust,
			ProjectBuildPipelineConfig::STAGE_POST_COMPILE, fingerprints);
	return fingerprints;
}

static bool _dirty_reason_allows_stage_run(ProjectBuildState::DirtyReason p_reason, bool p_retry_previous_failures) {
	return p_reason != ProjectBuildState::DIRTY_UNREADABLE_STATE &&
			(p_retry_previous_failures || p_reason != ProjectBuildState::DIRTY_PREVIOUS_FAILURE);
}

static ProjectBuildState::RunMode _build_state_run_mode(FoundryBuildPipelineRunner::RunMode p_mode) {
	switch (p_mode) {
		case FoundryBuildPipelineRunner::RUN_AUTOMATIC_DIRTY_TASKS:
			return ProjectBuildState::RUN_MODE_AUTOMATIC_DIRTY_TASKS;
		case FoundryBuildPipelineRunner::RUN_DIRTY_TASKS:
			return ProjectBuildState::RUN_MODE_DIRTY_TASKS;
		case FoundryBuildPipelineRunner::RUN_ALL_TASKS:
			return ProjectBuildState::RUN_MODE_ALL_TASKS;
	}

	return ProjectBuildState::RUN_MODE_DIRTY_TASKS;
}

static bool _has_runnable_dirty_stage_tasks(const ProjectBuildPipelineConfig &p_config,
		const ProjectBuildState &p_state, ProjectBuildPipelineConfig::Stage p_stage,
		const PackedStringArray &p_current_fingerprints, bool p_retry_previous_failures) {
	bool has_runnable_dirty_task = false;
	const PackedStringArray stage_tasks = p_config.get_enabled_stage_tasks(p_stage);
	for (int i = 0; i < stage_tasks.size(); i++) {
		const ProjectBuildPipelineConfig::TaskDefinition *task = p_config.get_task(stage_tasks[i]);
		if (task == nullptr) {
			continue;
		}

		const String fingerprint = i < p_current_fingerprints.size() ? p_current_fingerprints[i] : String();
		const ProjectBuildState::DirtyStatus task_status =
				p_state.get_task_dirty_status(task->name, fingerprint, task->outputs);
		if (!task_status.dirty) {
			continue;
		}
		if (!_dirty_reason_allows_stage_run(task_status.reason, p_retry_previous_failures)) {
			return false;
		}
		has_runnable_dirty_task = true;
	}
	return has_runnable_dirty_task;
}

static bool _snapshot_has_provider_diagnostic(const ProjectBuildPipelineStatusSnapshot &p_snapshot) {
	for (const ProjectBuildPipelineDiagnostic &diagnostic : p_snapshot.diagnostics) {
		if (diagnostic.kind == ProjectBuildPipelineDiagnostic::KIND_PROVIDER) {
			return true;
		}
	}
	return false;
}

static String _tail_text(const String &p_text, int p_max_chars = 8192) {
	if (p_text.length() <= p_max_chars) {
		return p_text;
	}
	return p_text.substr(p_text.length() - p_max_chars);
}

static ProjectBuildPipelineStatusSnapshot _blocked_task_snapshot(
		const ProjectBuildPipelineConfig::TaskDefinition &p_task,
		const String &p_message,
		const Ref<FoundryBuildResult> &p_result = Ref<FoundryBuildResult>()) {
	ProjectBuildPipelineStatusSnapshot snapshot;
	snapshot.state = ProjectBuildPipelineStatus::STATE_BLOCKED;
	snapshot.blocks_downstream_indexing = true;

	ProjectBuildPipelineDiagnostic diagnostic;
	diagnostic.kind = ProjectBuildPipelineDiagnostic::KIND_TASK;
	diagnostic.task_name = p_task.name;
	diagnostic.provider_id = p_task.provider;
	diagnostic.command = p_task.command;
	diagnostic.message = p_message;
	if (p_result.is_valid()) {
		diagnostic.exit_code = p_result->get_exit_code();
		diagnostic.stdout_tail = _tail_text(p_result->get_stdout());
		diagnostic.stderr_tail = _tail_text(p_result->get_stderr());
	}
	snapshot.diagnostics.push_back(diagnostic);
	return snapshot;
}

static ProjectBuildPipelineStatusSnapshot _evaluate_prepared_stage(
		const PreparedStage &p_prepared, bool p_compute_current_fingerprints) {
	const PackedStringArray current_fingerprints = p_compute_current_fingerprints ? _current_pipeline_fingerprints(p_prepared.stage_config, p_prepared.state, p_prepared.trust) : PackedStringArray();
	return ProjectBuildPipelineStatus::evaluate(p_prepared.stage_config, p_prepared.registry, p_prepared.state,
			p_prepared.trust, false, current_fingerprints, p_prepared.provider_diagnostics);
}

static PreparedStage _prepare_stage(ProjectBuildPipelineConfig::Stage p_stage) {
	PreparedStage prepared;

	if (!FileAccess::exists("res://project.foundry")) {
		prepared.disabled = true;
		prepared.snapshot = ProjectBuildPipelineStatusSnapshot();
		return prepared;
	}

	ProjectBuildPipelineConfig build_config;
	Ref<ConfigFile> config;
	config.instantiate();
	const Error config_err = config->load("res://project.foundry");
	if (config_err != OK || build_config.load_from_config_file(config) != OK) {
		prepared.blocked = true;
		prepared.snapshot = _blocked_config_load_snapshot("Project build pipeline configuration could not be loaded.");
		return prepared;
	}

	prepared.stage_config = build_config.filtered_for_stage(p_stage);
	if (prepared.stage_config.get_status() == ProjectBuildPipelineConfig::STATUS_DISABLED) {
		prepared.disabled = true;
		prepared.snapshot = ProjectBuildPipelineStatusSnapshot();
		return prepared;
	}

	_register_available_build_task_providers(build_config, prepared.available_registry);
	_register_stage_task_providers(prepared.stage_config, prepared.available_registry, p_stage, prepared.registry);

	const Vector<ProjectBuildPipelineConfig::ValidationError> validation_errors =
			prepared.stage_config.validate(&prepared.registry);
	if (!validation_errors.is_empty()) {
		prepared.blocked = true;
		prepared.snapshot = _blocked_config_validation_snapshot(validation_errors);
		return prepared;
	}

	prepared.state.load();
	prepared.trust.load();
	prepared.provider_diagnostics =
			_provider_diagnostics_for_stage(prepared.available_registry, prepared.stage_config, p_stage);
	return prepared;
}

} // namespace

bool FoundryBuildPipelineRunner::status_blocks_flow(const ProjectBuildPipelineStatusSnapshot &p_snapshot) {
	switch (p_snapshot.state) {
		case ProjectBuildPipelineStatus::STATE_DISABLED:
		case ProjectBuildPipelineStatus::STATE_CLEAN:
			return false;
		case ProjectBuildPipelineStatus::STATE_UNTRUSTED:
		case ProjectBuildPipelineStatus::STATE_DIRTY:
		case ProjectBuildPipelineStatus::STATE_RUNNING:
		case ProjectBuildPipelineStatus::STATE_BLOCKED:
			return true;
	}

	return true;
}

bool FoundryBuildPipelineRunner::StageRunResult::blocks_flow() const {
	return FoundryBuildPipelineRunner::status_blocks_flow(snapshot);
}

bool FoundryBuildPipelineRunner::StageRunResult::is_success() const {
	return error == OK && !blocks_flow();
}

ProjectBuildPipelineStatusSnapshot FoundryBuildPipelineRunner::get_stage_status(
		ProjectBuildPipelineConfig::Stage p_stage, bool p_compute_current_fingerprints) {
	const PreparedStage prepared = _prepare_stage(p_stage);
	if (prepared.disabled || prepared.blocked) {
		return prepared.snapshot;
	}
	return _evaluate_prepared_stage(prepared, p_compute_current_fingerprints);
}

FoundryBuildPipelineRunner::StageRunResult FoundryBuildPipelineRunner::run_stage(
		ProjectBuildPipelineConfig::Stage p_stage, RunMode p_mode, const OutputCallback &p_output_callback) {
	StageRunResult run_result;

	PreparedStage prepared = _prepare_stage(p_stage);
	if (prepared.disabled || prepared.blocked) {
		run_result.snapshot = prepared.snapshot;
		return run_result;
	}

	const PackedStringArray current_fingerprints =
			_current_pipeline_fingerprints(prepared.stage_config, prepared.state, prepared.trust);
	run_result.snapshot = ProjectBuildPipelineStatus::evaluate(prepared.stage_config, prepared.registry,
			prepared.state, prepared.trust, false, current_fingerprints, prepared.provider_diagnostics);

	if (run_result.snapshot.state == ProjectBuildPipelineStatus::STATE_BLOCKED &&
			_snapshot_has_provider_diagnostic(run_result.snapshot)) {
		return run_result;
	}

	const bool run_all_tasks = p_mode == RUN_ALL_TASKS;
	const bool retry_previous_failures = p_mode != RUN_AUTOMATIC_DIRTY_TASKS;
	const bool has_runnable_dirty_task = _has_runnable_dirty_stage_tasks(
			prepared.stage_config, prepared.state, p_stage, current_fingerprints, retry_previous_failures);
	if (!run_all_tasks) {
		if (run_result.snapshot.state == ProjectBuildPipelineStatus::STATE_UNTRUSTED ||
				run_result.snapshot.state == ProjectBuildPipelineStatus::STATE_RUNNING ||
				!has_runnable_dirty_task) {
			return run_result;
		}
	} else if (run_result.snapshot.state == ProjectBuildPipelineStatus::STATE_UNTRUSTED ||
			run_result.snapshot.state == ProjectBuildPipelineStatus::STATE_RUNNING ||
			(run_result.snapshot.state == ProjectBuildPipelineStatus::STATE_BLOCKED && !has_runnable_dirty_task)) {
		return run_result;
	}

	FoundryBuildTaskBootstrapLoader loader;
	loader.set_trusted_execution(prepared.trust.is_project_trusted());
	const Error loader_err = loader.load_registered_providers(prepared.registry,
			_enabled_stage_task_provider_ids(prepared.stage_config, p_stage));
	if (loader_err != OK) {
		run_result.error = loader_err;
		run_result.snapshot = ProjectBuildPipelineStatus::evaluate(prepared.stage_config, prepared.registry,
				prepared.state, prepared.trust, false, PackedStringArray(), loader.get_diagnostics());
		return run_result;
	}

	const PackedStringArray stage_tasks = prepared.stage_config.get_enabled_stage_tasks(p_stage);
	for (int i = 0; i < stage_tasks.size(); i++) {
		const ProjectBuildPipelineConfig::TaskDefinition *task = prepared.stage_config.get_task(stage_tasks[i]);
		if (task == nullptr) {
			continue;
		}

		const String task_fingerprint = _current_task_fingerprint(*task, prepared.state, prepared.trust);
		const ProjectBuildState::DirtyStatus task_status =
				prepared.state.get_task_dirty_status(task->name, task_fingerprint, task->outputs);
		const bool should_run_task = run_all_tasks || task_status.dirty;
		if (!should_run_task) {
			continue;
		}
		if (!run_all_tasks && !_dirty_reason_allows_stage_run(task_status.reason, retry_previous_failures)) {
			run_result.snapshot = ProjectBuildPipelineStatus::evaluate(prepared.stage_config, prepared.registry,
					prepared.state, prepared.trust, false,
					_current_pipeline_fingerprints(prepared.stage_config, prepared.state, prepared.trust),
					prepared.provider_diagnostics);
			return run_result;
		}

		const FoundryBuildTaskBootstrapLoader::LoadedProvider *provider = loader.get_loaded_provider(task->provider);
		if (provider == nullptr || provider->instance.is_null()) {
			prepared.state.record_task_run(task->name, String(), task->outputs, false, task_status,
					_build_state_run_mode(p_mode));
			const Error save_err = prepared.state.save();
			if (save_err != OK) {
				run_result.error = save_err;
				run_result.snapshot = _blocked_task_snapshot(*task,
						vformat("Build task '%s' result could not be persisted (error %d).", task->name, int(save_err)));
				return run_result;
			}
			run_result.snapshot = _blocked_task_snapshot(*task,
					vformat("Build task '%s' could not load provider '%s'.", task->name, task->provider));
			return run_result;
		}

		Ref<FoundryBuildContext> context = _build_task_context(*task, prepared.trust.is_project_trusted());

		Ref<FoundryBuildResult> result = FoundryBuildTask::call_run_script_hook(provider->instance, context);
		const bool success = result.is_valid() && result->is_success();
		const String fingerprint = result.is_valid() ? result->get_fingerprint() : String();
		prepared.state.record_task_run(task->name, fingerprint, task->outputs, success, task_status,
				_build_state_run_mode(p_mode));
		const Error save_err = prepared.state.save();
		if (save_err != OK) {
			run_result.error = save_err;
			run_result.snapshot = _blocked_task_snapshot(*task,
					vformat("Build task '%s' result could not be persisted (error %d).", task->name, int(save_err)), result);
			return run_result;
		}

		run_result.ran_any_task = true;
		if (!success) {
			const String message = result.is_valid() && !result->get_message().is_empty() ? result->get_message() : vformat("Build task '%s' failed.", task->name);
			run_result.snapshot = _blocked_task_snapshot(*task, message, result);
			return run_result;
		}

		p_output_callback.call(task->outputs);
	}

	run_result.snapshot = ProjectBuildPipelineStatus::evaluate(prepared.stage_config, prepared.registry,
			prepared.state, prepared.trust, false,
			_current_pipeline_fingerprints(prepared.stage_config, prepared.state, prepared.trust),
			prepared.provider_diagnostics);
	return run_result;
}

void FoundryBuildPipelineRunner::print_diagnostics(
		const ProjectBuildPipelineStatusSnapshot &p_snapshot, const String &p_context) {
	for (const ProjectBuildPipelineDiagnostic &diagnostic : p_snapshot.diagnostics) {
		String message;
		if (!p_context.is_empty()) {
			message += p_context + ": ";
		}
		if (!diagnostic.task_name.is_empty()) {
			message += vformat("task '%s': ", diagnostic.task_name);
		}
		message += diagnostic.message;
		if (diagnostic.exit_code != 0) {
			message += vformat(" (exit code %d)", diagnostic.exit_code);
		}
		ERR_PRINT(message);
		if (!diagnostic.stderr_tail.is_empty()) {
			ERR_PRINT("Build task stderr:\n" + diagnostic.stderr_tail);
		}
		if (!diagnostic.stdout_tail.is_empty()) {
			ERR_PRINT("Build task stdout:\n" + diagnostic.stdout_tail);
		}
	}
}
