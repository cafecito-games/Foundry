/**************************************************************************/
/*  fs_build_pipeline_settings.cpp                                        */
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

#include "fs_build_pipeline_settings.h"

#ifdef TOOLS_ENABLED

#include "../foundry_build_task.h"
#include "../fs_build_task_bootstrap_loader.h"

#include "core/config/foundry_build_task_registry.h"
#include "core/config/project_build_pipeline_status.h"
#include "core/config/project_build_state.h"
#include "core/config/project_settings.h"
#include "core/io/config_file.h"
#include "core/io/file_access.h"
#include "editor/file_system/editor_file_system.h"
#include "editor/themes/editor_scale.h"
#include "scene/gui/box_container.h"
#include "scene/gui/button.h"
#include "scene/gui/check_button.h"
#include "scene/gui/item_list.h"
#include "scene/gui/label.h"
#include "scene/gui/line_edit.h"
#include "scene/gui/option_button.h"
#include "scene/gui/rich_text_label.h"
#include "scene/gui/scroll_container.h"
#include "scene/gui/separator.h"
#include "scene/gui/spin_box.h"
#include "scene/gui/split_container.h"
#include "scene/gui/text_edit.h"

static const char *PROJECT_CONFIG_PATH = "res://project.foundry";
static const char *COMMAND_PROVIDER_ID = "command";

PackedStringArray FSBuildPipelineSettingsDialog::_lines_to_string_array(const String &p_text) {
	PackedStringArray result;
	const PackedStringArray lines = p_text.split("\n");
	for (int i = 0; i < lines.size(); i++) {
		const String line = lines[i].strip_edges();
		if (!line.is_empty()) {
			result.push_back(line);
		}
	}
	return result;
}

PackedStringArray FSBuildPipelineSettingsDialog::_lines_to_argv(const String &p_text) {
	// argv entries are preserved verbatim: leading/trailing whitespace and empty entries (including a
	// trailing one) can be meaningful, so one line maps to exactly one argument. Empty text is no
	// arguments rather than a single empty argument. This round-trips exactly with
	// _string_array_to_lines() for every argv except the pathological single empty-string argument.
	if (p_text.is_empty()) {
		return PackedStringArray();
	}
	return p_text.split("\n");
}

String FSBuildPipelineSettingsDialog::_string_array_to_lines(const PackedStringArray &p_array) {
	return String("\n").join(p_array);
}

Dictionary FSBuildPipelineSettingsDialog::_lines_to_string_dictionary(const String &p_text) {
	Dictionary result;
	const PackedStringArray lines = p_text.split("\n");
	for (int i = 0; i < lines.size(); i++) {
		const String line = lines[i].strip_edges();
		if (line.is_empty()) {
			continue;
		}
		const int divider = line.find_char('=');
		if (divider <= 0) {
			continue;
		}
		const String key = line.substr(0, divider).strip_edges();
		const String value = line.substr(divider + 1).strip_edges();
		if (!key.is_empty()) {
			result[key] = value;
		}
	}
	return result;
}

String FSBuildPipelineSettingsDialog::_string_dictionary_to_lines(const Dictionary &p_dictionary) {
	String result;
	const Array keys = p_dictionary.keys();
	for (int i = 0; i < keys.size(); i++) {
		if (i > 0) {
			result += "\n";
		}
		result += String(keys[i]) + "=" + String(p_dictionary[keys[i]]);
	}
	return result;
}

void FSBuildPipelineSettingsDialog::_build_available_registry(FoundryBuildTaskRegistry &r_registry) const {
	r_registry.register_builtin_providers();
	ProjectSettings *project_settings = ProjectSettings::get_singleton();
	if (project_settings != nullptr && project_settings->has_setting("editor_plugins/enabled")) {
		r_registry.register_enabled_addon_metadata(project_settings->get("editor_plugins/enabled"));
	}
	r_registry.register_project_providers(working_config, PROJECT_CONFIG_PATH);
}

void FSBuildPipelineSettingsDialog::_load_from_project() {
	working_config.clear();
	if (FileAccess::exists(PROJECT_CONFIG_PATH)) {
		Ref<ConfigFile> config;
		config.instantiate();
		if (config->load(PROJECT_CONFIG_PATH) == OK) {
			working_config.load_from_config_file(config);
		}
	}

	ProjectBuildTrustStore trust_store;
	trust_store.load();
	project_trusted = trust_store.is_project_trusted();

	selected_task = String();
	selected_stage = ProjectBuildPipelineConfig::STAGE_PRE_COMPILE;
	saved_preview_snapshot = working_config.generate_preview();
}

bool FSBuildPipelineSettingsDialog::_has_unsaved_changes() const {
	return working_config.generate_preview() != saved_preview_snapshot;
}

void FSBuildPipelineSettingsDialog::_save_to_project() {
	Ref<ConfigFile> config;
	config.instantiate();
	if (FileAccess::exists(PROJECT_CONFIG_PATH)) {
		// Refuse to overwrite a project file we could not read: writing the build sections onto an empty
		// ConfigFile would drop every unrelated section the existing file already holds.
		const Error load_err = config->load(PROJECT_CONFIG_PATH);
		if (load_err != OK) {
			ERR_PRINT(vformat("Could not read '%s' (error %d); build pipeline configuration was not saved to avoid overwriting existing project settings.", PROJECT_CONFIG_PATH, load_err));
			run_output->set_text(vformat(TTR("Could not read %s; nothing was saved. Your edits are still open."), PROJECT_CONFIG_PATH));
			return;
		}
	}
	working_config.write_to_config_file(config);
	const Error err = config->save(PROJECT_CONFIG_PATH);
	if (err != OK) {
		ERR_PRINT(vformat("Failed to write build pipeline configuration to '%s'.", PROJECT_CONFIG_PATH));
		run_output->set_text(vformat(TTR("Failed to save %s (error %d). Your edits are still open."), PROJECT_CONFIG_PATH, err));
		return;
	}
	saved_preview_snapshot = working_config.generate_preview();
	// The dialog does not hide on OK, so it can stay open to report a save failure; hide only on success.
	hide();
}

void FSBuildPipelineSettingsDialog::popup_settings() {
	_load_from_project();
	_refresh_all();
	popup_centered_clamped(Size2(1100, 720) * EDSCALE, 0.8);
}

void FSBuildPipelineSettingsDialog::_refresh_all() {
	updating_ui = true;
	enabled_toggle->set_pressed(working_config.is_enabled());
	updating_ui = false;

	_refresh_stage_lists();
	_refresh_provider_select();
	_refresh_details();
	_refresh_validation_and_preview();
	_refresh_actions();
}

static String _stage_item_label(const ProjectBuildPipelineConfig &p_config, const String &p_task) {
	const ProjectBuildPipelineConfig::TaskDefinition *task = p_config.get_task(p_task);
	String label = p_task;
	if (task == nullptr) {
		label += "  (undefined)";
	} else {
		if (!task->provider.is_empty()) {
			label += "  [" + task->provider + "]";
		}
		if (!task->enabled) {
			label += "  (disabled)";
		}
	}
	return label;
}

void FSBuildPipelineSettingsDialog::_refresh_stage_lists() {
	struct StageWidget {
		ItemList *list;
		ProjectBuildPipelineConfig::Stage stage;
	};
	StageWidget widgets[2] = {
		{ pre_compile_list, ProjectBuildPipelineConfig::STAGE_PRE_COMPILE },
		{ post_compile_list, ProjectBuildPipelineConfig::STAGE_POST_COMPILE },
	};

	for (const StageWidget &widget : widgets) {
		widget.list->clear();
		const PackedStringArray stage_tasks = working_config.get_stage_tasks(widget.stage);
		for (int i = 0; i < stage_tasks.size(); i++) {
			const String task_name = stage_tasks[i];
			const int idx = widget.list->add_item(_stage_item_label(working_config, task_name));
			widget.list->set_item_metadata(idx, task_name);
			const ProjectBuildPipelineConfig::TaskDefinition *task = working_config.get_task(task_name);
			if (task == nullptr || !task->enabled) {
				widget.list->set_item_custom_fg_color(idx, Color(0.6, 0.6, 0.6));
			}
			if (widget.stage == selected_stage && task_name == selected_task) {
				widget.list->select(idx);
			}
		}
	}
}

void FSBuildPipelineSettingsDialog::_refresh_provider_select() {
	updating_ui = true;
	provider_select->clear();

	FoundryBuildTaskRegistry registry;
	_build_available_registry(registry);

	int select_index = -1;
	const Vector<String> &order = registry.get_provider_order();
	for (int i = 0; i < order.size(); i++) {
		const FoundryBuildTaskRegistry::ProviderEntry *entry = registry.get_provider(order[i]);
		if (entry == nullptr) {
			continue;
		}
		String label = entry->display_name.is_empty() ? entry->id : vformat("%s (%s)", entry->display_name, entry->id);
		switch (entry->source.type) {
			case FoundryBuildTaskRegistry::SOURCE_NATIVE:
				label += "  – built-in";
				break;
			case FoundryBuildTaskRegistry::SOURCE_ADDON:
				label += "  – addon";
				break;
			case FoundryBuildTaskRegistry::SOURCE_PROJECT:
				label += "  – project";
				break;
		}
		const int item_index = provider_select->get_item_count();
		provider_select->add_item(label);
		provider_select->set_item_metadata(item_index, entry->id);
	}

	const ProjectBuildPipelineConfig::TaskDefinition *task = selected_task.is_empty() ? nullptr : working_config.get_task(selected_task);
	if (task != nullptr) {
		for (int i = 0; i < provider_select->get_item_count(); i++) {
			if (String(provider_select->get_item_metadata(i)) == task->provider) {
				select_index = i;
				break;
			}
		}
		// The task's provider may not match any registered entry: it can be empty (a task still being
		// authored) or reference an unregistered id (a typo or a disabled addon). Add an explicit entry
		// carrying that exact provider value and select it, so the shown selection matches the model
		// instead of OptionButton auto-selecting its first entry (which would silently display e.g.
		// "command" while the saved provider stays empty and the validation error is unrepairable here).
		if (select_index < 0) {
			const String label = task->provider.is_empty()
					? TTR("<none> (select a provider)")
					: vformat("%s  – unregistered", task->provider);
			const int item_index = provider_select->get_item_count();
			provider_select->add_item(label);
			provider_select->set_item_metadata(item_index, task->provider);
			select_index = item_index;
		}
	}
	if (select_index >= 0) {
		provider_select->select(select_index);
	}
	updating_ui = false;
}

void FSBuildPipelineSettingsDialog::_refresh_details() {
	const ProjectBuildPipelineConfig::TaskDefinition *task = selected_task.is_empty() ? nullptr : working_config.get_task(selected_task);

	details_placeholder->set_visible(task == nullptr);
	details_container->set_visible(task != nullptr);
	if (task == nullptr) {
		return;
	}

	updating_ui = true;
	name_edit->set_text(task->name);
	task_enabled_toggle->set_pressed(task->enabled);
	working_directory_edit->set_text(task->working_directory);
	inputs_edit->set_text(_string_array_to_lines(task->inputs));
	outputs_edit->set_text(_string_array_to_lines(task->outputs));
	environment_edit->set_text(_string_dictionary_to_lines(task->environment));
	timeout_toggle->set_pressed(task->has_timeout_seconds);
	timeout_spin->set_value(task->timeout_seconds);
	timeout_spin->set_editable(task->has_timeout_seconds);

	command_edit->set_text(task->command);
	args_edit->set_text(_string_array_to_lines(task->args));
	tool_version_edit->set_text(_string_array_to_lines(task->tool_version_command));

	const bool is_command = task->provider == COMMAND_PROVIDER_ID;
	command_fields->set_visible(is_command);
	options_section->set_visible(!is_command);
	updating_ui = false;

	_refresh_provider_select();
	_refresh_options_editor();
}

void FSBuildPipelineSettingsDialog::_refresh_options_editor() {
	const ProjectBuildPipelineConfig::TaskDefinition *task = selected_task.is_empty() ? nullptr : working_config.get_task(selected_task);
	if (task == nullptr || task->provider == COMMAND_PROVIDER_ID) {
		return;
	}

	updating_ui = true;
	generic_options_edit->set_text(_string_dictionary_to_lines(task->options));

	String hint = TTR("Provider options are edited as key=value lines.");
	if (!project_trusted) {
		hint = TTR("Trust project build tasks to load provider-specific option schemas. Until then, edit options as key=value lines.");
	} else {
		// Provider-supplied schemas require loading provider code, so only attempt it once trusted.
		FoundryBuildTaskRegistry registry;
		_build_available_registry(registry);
		if (registry.has_provider(task->provider)) {
			FoundryBuildTaskBootstrapLoader loader;
			loader.set_trusted_execution(true);
			PackedStringArray ids;
			ids.push_back(task->provider);
			loader.load_registered_providers(registry, ids);
			Ref<FoundryBuildTaskConfigSchema> schema;
			if (loader.load_provider_schema(task->provider, schema) == OK && schema.is_valid() && !schema->is_empty()) {
				const Dictionary properties = schema->get_properties();
				const PackedStringArray required = schema->get_required();
				String schema_hint = TTR("Provider options (declared by provider schema):");
				const Array keys = properties.keys();
				for (int i = 0; i < keys.size(); i++) {
					const String key = keys[i];
					const bool is_required = required.has(key);
					schema_hint += vformat("\n  • %s: %s%s", key, String(properties[key]), is_required ? TTR(" (required)") : String());
				}
				hint = schema_hint;
			}
		}
	}
	options_hint->set_text(hint);
	updating_ui = false;
}

void FSBuildPipelineSettingsDialog::_refresh_validation_and_preview() {
	FoundryBuildTaskRegistry registry;
	_build_available_registry(registry);
	const Vector<ProjectBuildPipelineConfig::ValidationError> errors = working_config.validate(&registry);

	String text;
	for (int i = 0; i < errors.size(); i++) {
		if (!text.is_empty()) {
			text += "\n";
		}
		const ProjectBuildPipelineConfig::ValidationError &error = errors[i];
		if (error.section.is_empty()) {
			text += error.message;
		} else {
			text += vformat("%s/%s: %s", error.section, error.key, error.message);
		}
	}
	// Registry diagnostics (e.g. provider ID collisions) are blocking for the pipeline even when the
	// config parses, so surface them here too instead of reporting the configuration as valid.
	for (const FoundryBuildTaskRegistry::Diagnostic &diagnostic : registry.get_diagnostics()) {
		if (!text.is_empty()) {
			text += "\n";
		}
		if (!diagnostic.provider_id.is_empty()) {
			text += vformat("provider '%s': %s", diagnostic.provider_id, diagnostic.message);
		} else {
			text += diagnostic.message;
		}
	}

	if (text.is_empty()) {
		validation_label->set_text(TTR("Configuration is valid."));
		validation_label->remove_theme_color_override(SceneStringName(font_color));
	} else {
		validation_label->set_text(text);
		validation_label->add_theme_color_override(SceneStringName(font_color), Color(0.94, 0.42, 0.42));
	}

	preview_text->set_text(working_config.generate_preview());

	// Editing changes dirtiness, which gates the run actions; keep their enabled state in sync.
	_refresh_actions();
}

void FSBuildPipelineSettingsDialog::_refresh_actions() {
	if (project_trusted) {
		trust_label->set_text(TTR("Project build tasks: Trusted"));
		trust_button->set_text(TTR("Revoke Trust"));
	} else {
		trust_label->set_text(TTR("Project build tasks: Untrusted (run actions disabled)"));
		trust_button->set_text(TTR("Trust Project Build Tasks"));
	}

	// Run actions execute the persisted project.foundry, so they are only enabled while the editor has
	// no unsaved changes; otherwise a run would record build state for a config that is not on disk.
	// A disabled task (or a globally disabled pipeline) is skipped by stage execution, so it must not
	// be runnable here either.
	const ProjectBuildPipelineConfig::TaskDefinition *selected = selected_task.is_empty() ? nullptr : working_config.get_task(selected_task);
	const bool has_task = selected != nullptr;
	const bool pipeline_enabled = working_config.is_enabled();
	const bool can_run = project_trusted && pipeline_enabled && !_has_unsaved_changes();
	run_task_button->set_disabled(!can_run || !has_task || (selected != nullptr && !selected->enabled));
	run_stage_button->set_disabled(!can_run);
	const String run_hint = _has_unsaved_changes() ? TTR("Save changes before running.") : String();
	run_task_button->set_tooltip_text(run_hint);
	run_stage_button->set_tooltip_text(run_hint);
	clear_state_button->set_disabled(false);
	duplicate_button->set_disabled(!has_task);
	remove_button->set_disabled(!has_task);
	move_up_button->set_disabled(!has_task);
	move_down_button->set_disabled(!has_task);
}

void FSBuildPipelineSettingsDialog::_select_stage_task(ProjectBuildPipelineConfig::Stage p_stage, const String &p_task) {
	selected_stage = p_stage;
	selected_task = p_task;

	updating_ui = true;
	if (p_stage == ProjectBuildPipelineConfig::STAGE_PRE_COMPILE) {
		post_compile_list->deselect_all();
	} else {
		pre_compile_list->deselect_all();
	}
	updating_ui = false;

	_refresh_details();
	_refresh_actions();
}

void FSBuildPipelineSettingsDialog::_pre_list_selected(int p_index) {
	if (updating_ui) {
		return;
	}
	_select_stage_task(ProjectBuildPipelineConfig::STAGE_PRE_COMPILE, pre_compile_list->get_item_metadata(p_index));
}

void FSBuildPipelineSettingsDialog::_post_list_selected(int p_index) {
	if (updating_ui) {
		return;
	}
	_select_stage_task(ProjectBuildPipelineConfig::STAGE_POST_COMPILE, post_compile_list->get_item_metadata(p_index));
}

void FSBuildPipelineSettingsDialog::_enabled_toggled(bool p_pressed) {
	if (updating_ui) {
		return;
	}
	working_config.set_enabled(p_pressed);
	_refresh_validation_and_preview();
}

void FSBuildPipelineSettingsDialog::_trust_pressed() {
	ProjectBuildTrustStore trust_store;
	trust_store.load();
	trust_store.set_project_trusted(!trust_store.is_project_trusted());
	if (trust_store.save() != OK) {
		// Reflect on-disk reality: if the change did not persist, do not let the UI claim a new trust
		// state that a future session will not honor.
		ProjectBuildTrustStore persisted;
		persisted.load();
		project_trusted = persisted.is_project_trusted();
		run_output->set_text(TTR("Failed to update the project trust decision; the trust file could not be written."));
	} else {
		project_trusted = trust_store.is_project_trusted();
	}
	_refresh_options_editor();
	_refresh_actions();
}

void FSBuildPipelineSettingsDialog::_add_task() {
	const String name = working_config.make_unique_task_name("task");
	ProjectBuildPipelineConfig::TaskDefinition task;
	task.name = name;
	task.provider = COMMAND_PROVIDER_ID;
	working_config.set_task(task);
	working_config.add_task_to_stage(selected_stage, name);

	selected_task = name;
	_refresh_stage_lists();
	_refresh_details();
	_refresh_validation_and_preview();
	_refresh_actions();
}

void FSBuildPipelineSettingsDialog::_duplicate_task() {
	if (selected_task.is_empty()) {
		return;
	}
	const String copy = working_config.duplicate_task(selected_task);
	if (copy.is_empty()) {
		return;
	}
	// Insert the copy right after the original within the stage it belongs to.
	const PackedStringArray stage_tasks = working_config.get_stage_tasks(selected_stage);
	const int original_index = stage_tasks.find(selected_task);
	working_config.add_task_to_stage(selected_stage, copy);
	if (original_index >= 0) {
		const PackedStringArray updated = working_config.get_stage_tasks(selected_stage);
		working_config.move_stage_task(selected_stage, updated.size() - 1, original_index + 1);
	}

	selected_task = copy;
	_refresh_stage_lists();
	_refresh_details();
	_refresh_validation_and_preview();
	_refresh_actions();
}

void FSBuildPipelineSettingsDialog::_remove_task() {
	if (selected_task.is_empty()) {
		return;
	}
	working_config.remove_task(selected_task);
	selected_task = String();
	_refresh_stage_lists();
	_refresh_details();
	_refresh_validation_and_preview();
	_refresh_actions();
}

void FSBuildPipelineSettingsDialog::_move_task(int p_delta) {
	if (selected_task.is_empty()) {
		return;
	}
	const PackedStringArray stage_tasks = working_config.get_stage_tasks(selected_stage);
	const int index = stage_tasks.find(selected_task);
	if (index < 0) {
		return;
	}
	if (working_config.move_stage_task(selected_stage, index, index + p_delta)) {
		_refresh_stage_lists();
		_refresh_validation_and_preview();
	}
}

// Drops fields the selected provider does not use, so switching a task's provider never leaves stale
// hidden data (command args on a script provider, or provider options on the command provider) that
// write_to_config_file() would still persist.
static void _strip_inapplicable_fields(ProjectBuildPipelineConfig::TaskDefinition &r_task) {
	if (r_task.provider == COMMAND_PROVIDER_ID) {
		r_task.options.clear();
	} else {
		r_task.command = String();
		r_task.args = PackedStringArray();
		r_task.tool_version_command = PackedStringArray();
	}
}

void FSBuildPipelineSettingsDialog::_provider_selected(int p_index) {
	if (updating_ui || selected_task.is_empty()) {
		return;
	}
	const ProjectBuildPipelineConfig::TaskDefinition *existing = working_config.get_task(selected_task);
	if (existing == nullptr) {
		return;
	}
	ProjectBuildPipelineConfig::TaskDefinition task = *existing;
	task.provider = provider_select->get_item_metadata(p_index);
	_strip_inapplicable_fields(task);
	working_config.set_task(task);

	_refresh_stage_lists();
	_refresh_details();
	_refresh_validation_and_preview();
}

ProjectBuildPipelineConfig::TaskDefinition FSBuildPipelineSettingsDialog::_read_details_into_task(const String &p_name) const {
	ProjectBuildPipelineConfig::TaskDefinition task;
	const ProjectBuildPipelineConfig::TaskDefinition *existing = working_config.get_task(p_name);
	if (existing != nullptr) {
		task = *existing;
	}
	task.name = p_name;

	task.enabled = task_enabled_toggle->is_pressed();
	task.working_directory = working_directory_edit->get_text().strip_edges();
	if (task.working_directory.is_empty()) {
		task.working_directory = "res://";
	}
	task.inputs = _lines_to_string_array(inputs_edit->get_text());
	task.outputs = _lines_to_string_array(outputs_edit->get_text());
	task.environment = _lines_to_string_dictionary(environment_edit->get_text());
	task.has_timeout_seconds = timeout_toggle->is_pressed();
	task.timeout_seconds = (int)timeout_spin->get_value();

	if (task.provider == COMMAND_PROVIDER_ID) {
		task.command = command_edit->get_text().strip_edges();
		task.args = _lines_to_argv(args_edit->get_text());
		task.tool_version_command = _lines_to_argv(tool_version_edit->get_text());
	} else {
		// The generic editor is text-only, so it would coerce every value to a String. Preserve the
		// original typed Variant for any key whose textual form is unchanged; only keys the user
		// actually retyped (or added) become strings.
		const Dictionary previous = existing != nullptr ? existing->options : Dictionary();
		const Dictionary edited = _lines_to_string_dictionary(generic_options_edit->get_text());
		Dictionary options;
		const Array edited_keys = edited.keys();
		for (int i = 0; i < edited_keys.size(); i++) {
			const Variant key = edited_keys[i];
			if (previous.has(key) && String(previous[key]) == String(edited[key])) {
				options[key] = previous[key];
			} else {
				options[key] = edited[key];
			}
		}
		task.options = options;
	}
	_strip_inapplicable_fields(task);
	return task;
}

void FSBuildPipelineSettingsDialog::_commit_details() {
	if (updating_ui || selected_task.is_empty()) {
		return;
	}
	if (working_config.get_task(selected_task) == nullptr) {
		return;
	}
	working_config.set_task(_read_details_into_task(selected_task));
	_refresh_stage_lists();
	_refresh_validation_and_preview();
}

void FSBuildPipelineSettingsDialog::_details_changed() {
	_commit_details();
}

void FSBuildPipelineSettingsDialog::_task_enabled_toggled(bool p_pressed) {
	if (updating_ui || selected_task.is_empty()) {
		return;
	}
	working_config.set_task_enabled(selected_task, p_pressed);
	_refresh_stage_lists();
	_refresh_validation_and_preview();
}

void FSBuildPipelineSettingsDialog::_timeout_toggled(bool p_pressed) {
	if (updating_ui) {
		return;
	}
	timeout_spin->set_editable(p_pressed);
	_commit_details();
}

void FSBuildPipelineSettingsDialog::_name_submitted() {
	if (updating_ui || selected_task.is_empty()) {
		return;
	}
	const String new_name = name_edit->get_text().strip_edges();
	if (new_name == selected_task) {
		return;
	}
	if (working_config.rename_task(selected_task, new_name)) {
		selected_task = new_name;
		_refresh_stage_lists();
		_refresh_validation_and_preview();
		_refresh_actions();
	} else {
		// Reject the rename (empty, unchanged, or a name collision) and restore the field.
		updating_ui = true;
		name_edit->set_text(selected_task);
		updating_ui = false;
	}
}

static Ref<FoundryBuildResult> _make_failure_result(const String &p_message) {
	Ref<FoundryBuildResult> result;
	result.instantiate();
	result->set_success(false);
	result->set_message(p_message);
	return result;
}

static Ref<FoundryBuildResult> _run_task_definition(const ProjectBuildPipelineConfig::TaskDefinition &p_task,
		const FoundryBuildTaskRegistry &p_registry) {
	Ref<FoundryBuildContext> context;
	context.instantiate();
	context->set_provider_id(p_task.provider);
	context->set_task_name(p_task.name);
	context->set_project_config_path(PROJECT_CONFIG_PATH);
	context->set_trusted_execution(true);

	// Every provider receives the declared task fields through the context options, matching what the
	// command runner reads. Provider-specific `options` entries are merged on top for non-command
	// providers so a provider still sees its own configuration.
	Dictionary options;
	options["working_directory"] = p_task.working_directory;
	options["environment"] = p_task.environment;
	options["inputs"] = p_task.inputs;
	options["outputs"] = p_task.outputs;
	// Only forward the timeout when the task actually overrides it, so a manual run uses the same
	// default the saved pipeline would when the override is off.
	if (p_task.has_timeout_seconds) {
		options["timeout_seconds"] = p_task.timeout_seconds;
	}

	if (p_task.provider == COMMAND_PROVIDER_ID) {
		options["command"] = p_task.command;
		options["args"] = p_task.args;
		options["tool_version_command"] = p_task.tool_version_command;
		context->set_options(options);

		Ref<FoundryCommandBuildTask> command_task;
		command_task.instantiate();
		const Ref<FoundryBuildResult> result = command_task->run(context);
		return result.is_valid() ? result : _make_failure_result(vformat("Command task '%s' returned no result.", p_task.name));
	}

	const Array option_keys = p_task.options.keys();
	for (int i = 0; i < option_keys.size(); i++) {
		options[option_keys[i]] = p_task.options[option_keys[i]];
	}
	context->set_options(options);

	FoundryBuildTaskBootstrapLoader loader;
	loader.set_trusted_execution(true);
	PackedStringArray ids;
	ids.push_back(p_task.provider);
	loader.load_registered_providers(p_registry, ids);
	const FoundryBuildTaskBootstrapLoader::LoadedProvider *provider = loader.get_loaded_provider(p_task.provider);
	if (provider == nullptr || provider->instance.is_null()) {
		return _make_failure_result(vformat("Provider '%s' could not be loaded.", p_task.provider));
	}
	const Ref<FoundryBuildResult> result = provider->instance->run(context);
	return result.is_valid() ? result : _make_failure_result(vformat("Provider '%s' returned no result.", p_task.provider));
}

static String _format_run_output(const String &p_task_name, const Ref<FoundryBuildResult> &p_result) {
	String output = vformat("[b]%s[/b]: %s\n", p_task_name, p_result->is_success() ? TTR("Success") : TTR("Failed"));
	if (!p_result->get_message().is_empty()) {
		output += p_result->get_message() + "\n";
	}
	const String stdout_text = p_result->get_stdout().strip_edges();
	if (!stdout_text.is_empty()) {
		output += "stdout:\n" + stdout_text + "\n";
	}
	const String stderr_text = p_result->get_stderr().strip_edges();
	if (!stderr_text.is_empty()) {
		output += "stderr:\n" + stderr_text + "\n";
	}
	return output;
}

// Returns a non-empty reason when the registry recorded a blocking diagnostic (provider collision,
// invalid/untrusted descriptor, loader failure) for this provider. The normal pipeline blocks on
// these, so the editor must not silently run a possibly-wrong provider.
static String _provider_run_block_reason(const FoundryBuildTaskRegistry &p_registry, const String &p_provider_id) {
	for (const FoundryBuildTaskRegistry::Diagnostic &diagnostic : p_registry.get_diagnostics()) {
		if (diagnostic.provider_id == p_provider_id) {
			return diagnostic.message;
		}
	}
	return String();
}

// The full reason a task must not be run: a blocking registry diagnostic, or any pipeline validation
// error for the requested stage. The pipeline validates the stage-filtered configuration and blocks
// that stage before executing any task, so a stage-level error (duplicate task, a task listed in
// both stages) or another invalid task in the same stage must stop manual runs too — but an invalid
// task in the *other* stage must not, matching the pipeline's per-stage gating. Task/provider-scoped
// errors are reported first for a clearer message.
static String _task_run_block_reason(const ProjectBuildPipelineConfig &p_config,
		const FoundryBuildTaskRegistry &p_registry, ProjectBuildPipelineConfig::Stage p_stage,
		const ProjectBuildPipelineConfig::TaskDefinition &p_task) {
	const String provider_reason = _provider_run_block_reason(p_registry, p_task.provider);
	if (!provider_reason.is_empty()) {
		return provider_reason;
	}

	const String task_section = "build/tasks/" + p_task.name;
	const String provider_section = "build/providers/" + p_task.provider;
	const ProjectBuildPipelineConfig filtered = p_config.filtered_for_stage(p_stage);
	const Vector<ProjectBuildPipelineConfig::ValidationError> errors = filtered.validate(&p_registry);
	for (const ProjectBuildPipelineConfig::ValidationError &error : errors) {
		if (error.section == task_section || error.section == provider_section) {
			return vformat("%s/%s: %s", error.section, error.key, error.message);
		}
	}
	// Any other validation error still blocks execution, mirroring the pipeline.
	if (!errors.is_empty()) {
		const ProjectBuildPipelineConfig::ValidationError &error = errors[0];
		if (error.section.is_empty()) {
			return error.message;
		}
		return vformat("%s/%s: %s", error.section, error.key, error.message);
	}
	return String();
}

void FSBuildPipelineSettingsDialog::_run_selected_task() {
	if (!project_trusted || selected_task.is_empty() || _has_unsaved_changes() || !working_config.is_enabled()) {
		return;
	}
	const ProjectBuildPipelineConfig::TaskDefinition *task = working_config.get_task(selected_task);
	if (task == nullptr || !task->enabled) {
		return;
	}

	FoundryBuildTaskRegistry registry;
	_build_available_registry(registry);

	const String block_reason = _task_run_block_reason(working_config, registry, selected_stage, *task);
	if (!block_reason.is_empty()) {
		run_output->set_text(vformat(TTR("Cannot run task '%s': %s"), task->name, block_reason));
		return;
	}

	const Ref<FoundryBuildResult> result = _run_task_definition(*task, registry);
	String output = _format_run_output(selected_task, result);

	ProjectBuildState state;
	state.load();
	state.record_task_result(task->name, result->get_fingerprint(), task->outputs, result->is_success());
	if (state.save() != OK) {
		output += TTR("Warning: could not persist build state; cached status may be stale.") + String("\n");
	}
	run_output->set_text(output);

	if (result->is_success() && EditorFileSystem::get_singleton() != nullptr) {
		EditorFileSystem::get_singleton()->scan_changes();
	}
}

void FSBuildPipelineSettingsDialog::_run_selected_stage() {
	if (!project_trusted || _has_unsaved_changes() || !working_config.is_enabled()) {
		return;
	}
	FoundryBuildTaskRegistry registry;
	_build_available_registry(registry);

	ProjectBuildState state;
	state.load();

	const PackedStringArray stage_tasks = working_config.get_enabled_stage_tasks(selected_stage);
	String output;
	bool changed_outputs = false;
	for (int i = 0; i < stage_tasks.size(); i++) {
		const ProjectBuildPipelineConfig::TaskDefinition *task = working_config.get_task(stage_tasks[i]);
		if (task == nullptr) {
			continue;
		}
		const String block_reason = _task_run_block_reason(working_config, registry, selected_stage, *task);
		if (!block_reason.is_empty()) {
			output += vformat(TTR("%s: cannot run – %s\n"), task->name, block_reason);
			output += TTR("Stage stopped: task is blocked.") + String("\n");
			break;
		}
		const Ref<FoundryBuildResult> result = _run_task_definition(*task, registry);
		output += _format_run_output(task->name, result);
		state.record_task_result(task->name, result->get_fingerprint(), task->outputs, result->is_success());
		if (result->is_success()) {
			changed_outputs = true;
		} else {
			output += TTR("Stage stopped on first failure.") + String("\n");
			break;
		}
	}
	if (stage_tasks.is_empty()) {
		output = TTR("No enabled tasks in this stage.");
	}
	if (state.save() != OK) {
		output += TTR("Warning: could not persist build state; cached status may be stale.") + String("\n");
	}
	run_output->set_text(output);

	if (changed_outputs && EditorFileSystem::get_singleton() != nullptr) {
		EditorFileSystem::get_singleton()->scan_changes();
	}
}

void FSBuildPipelineSettingsDialog::_clear_cached_state() {
	ProjectBuildState state;
	state.load();
	state.clear();
	if (state.save() != OK) {
		run_output->set_text(TTR("Failed to clear cached build state; the state file could not be written."));
		return;
	}
	run_output->set_text(TTR("Cleared cached build state."));
}

FSBuildPipelineSettingsDialog::FSBuildPipelineSettingsDialog() {
	set_title(TTR("Build Pipeline"));
	set_ok_button_text(TTR("Save"));
	// Keep the dialog open on OK so _save_to_project() can report (and preserve edits through) a save
	// or read failure; it hides itself only after a successful write.
	set_hide_on_ok(false);
	connect(SceneStringName(confirmed), callable_mp(this, &FSBuildPipelineSettingsDialog::_save_to_project));

	VBoxContainer *root = memnew(VBoxContainer);
	add_child(root);

	HBoxContainer *header = memnew(HBoxContainer);
	root->add_child(header);

	enabled_toggle = memnew(CheckButton);
	enabled_toggle->set_text(TTR("Enable build pipeline"));
	enabled_toggle->connect(SceneStringName(toggled), callable_mp(this, &FSBuildPipelineSettingsDialog::_enabled_toggled));
	header->add_child(enabled_toggle);

	header->add_spacer(false);

	trust_label = memnew(Label);
	header->add_child(trust_label);

	trust_button = memnew(Button);
	trust_button->connect(SceneStringName(pressed), callable_mp(this, &FSBuildPipelineSettingsDialog::_trust_pressed));
	header->add_child(trust_button);

	root->add_child(memnew(HSeparator));

	HSplitContainer *split = memnew(HSplitContainer);
	split->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	root->add_child(split);

	// Left: the ordered stage lists and their editing toolbar.
	VBoxContainer *stages_column = memnew(VBoxContainer);
	stages_column->set_custom_minimum_size(Size2(320, 0) * EDSCALE);
	split->add_child(stages_column);

	HBoxContainer *toolbar = memnew(HBoxContainer);
	stages_column->add_child(toolbar);

	add_button = memnew(Button);
	add_button->set_text(TTR("Add"));
	add_button->connect(SceneStringName(pressed), callable_mp(this, &FSBuildPipelineSettingsDialog::_add_task));
	toolbar->add_child(add_button);

	duplicate_button = memnew(Button);
	duplicate_button->set_text(TTR("Duplicate"));
	duplicate_button->connect(SceneStringName(pressed), callable_mp(this, &FSBuildPipelineSettingsDialog::_duplicate_task));
	toolbar->add_child(duplicate_button);

	remove_button = memnew(Button);
	remove_button->set_text(TTR("Remove"));
	remove_button->connect(SceneStringName(pressed), callable_mp(this, &FSBuildPipelineSettingsDialog::_remove_task));
	toolbar->add_child(remove_button);

	move_up_button = memnew(Button);
	move_up_button->set_text(TTR("Up"));
	move_up_button->connect(SceneStringName(pressed), callable_mp(this, &FSBuildPipelineSettingsDialog::_move_task).bind(-1));
	toolbar->add_child(move_up_button);

	move_down_button = memnew(Button);
	move_down_button->set_text(TTR("Down"));
	move_down_button->connect(SceneStringName(pressed), callable_mp(this, &FSBuildPipelineSettingsDialog::_move_task).bind(1));
	toolbar->add_child(move_down_button);

	Label *pre_label = memnew(Label);
	pre_label->set_text(TTR("Pre-compile"));
	stages_column->add_child(pre_label);

	pre_compile_list = memnew(ItemList);
	pre_compile_list->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	pre_compile_list->connect("item_selected", callable_mp(this, &FSBuildPipelineSettingsDialog::_pre_list_selected));
	stages_column->add_child(pre_compile_list);

	Label *post_label = memnew(Label);
	post_label->set_text(TTR("Post-compile"));
	stages_column->add_child(post_label);

	post_compile_list = memnew(ItemList);
	post_compile_list->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	post_compile_list->connect("item_selected", callable_mp(this, &FSBuildPipelineSettingsDialog::_post_list_selected));
	stages_column->add_child(post_compile_list);

	// Right: the details editor for the selected task plus validation, preview, and run actions.
	VBoxContainer *right_column = memnew(VBoxContainer);
	right_column->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	split->add_child(right_column);

	details_placeholder = memnew(Label);
	details_placeholder->set_text(TTR("Select or add a task to edit its details."));
	right_column->add_child(details_placeholder);

	ScrollContainer *details_scroll = memnew(ScrollContainer);
	details_scroll->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	right_column->add_child(details_scroll);

	details_container = memnew(VBoxContainer);
	details_container->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	details_scroll->add_child(details_container);

	auto add_labeled = [&](const String &p_label, Control *p_control) {
		Label *label = memnew(Label);
		label->set_text(p_label);
		details_container->add_child(label);
		details_container->add_child(p_control);
	};

	name_edit = memnew(LineEdit);
	name_edit->connect(SceneStringName(text_submitted), callable_mp(this, &FSBuildPipelineSettingsDialog::_name_submitted).unbind(1));
	name_edit->connect("focus_exited", callable_mp(this, &FSBuildPipelineSettingsDialog::_name_submitted));
	add_labeled(TTR("Task name"), name_edit);

	provider_select = memnew(OptionButton);
	provider_select->connect("item_selected", callable_mp(this, &FSBuildPipelineSettingsDialog::_provider_selected));
	add_labeled(TTR("Provider"), provider_select);

	task_enabled_toggle = memnew(CheckButton);
	task_enabled_toggle->set_text(TTR("Task enabled"));
	task_enabled_toggle->connect(SceneStringName(toggled), callable_mp(this, &FSBuildPipelineSettingsDialog::_task_enabled_toggled));
	details_container->add_child(task_enabled_toggle);

	command_fields = memnew(VBoxContainer);
	command_fields->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	details_container->add_child(command_fields);

	command_edit = memnew(LineEdit);
	command_edit->connect(SceneStringName(text_changed), callable_mp(this, &FSBuildPipelineSettingsDialog::_details_changed).unbind(1));
	{
		Label *label = memnew(Label);
		label->set_text(TTR("Command"));
		command_fields->add_child(label);
		command_fields->add_child(command_edit);
	}

	args_edit = memnew(TextEdit);
	args_edit->set_custom_minimum_size(Size2(0, 60) * EDSCALE);
	args_edit->connect(SceneStringName(text_changed), callable_mp(this, &FSBuildPipelineSettingsDialog::_details_changed));
	{
		Label *label = memnew(Label);
		label->set_text(TTR("Arguments (one per line)"));
		command_fields->add_child(label);
		command_fields->add_child(args_edit);
	}

	tool_version_edit = memnew(TextEdit);
	tool_version_edit->set_custom_minimum_size(Size2(0, 50) * EDSCALE);
	tool_version_edit->connect(SceneStringName(text_changed), callable_mp(this, &FSBuildPipelineSettingsDialog::_details_changed));
	{
		Label *label = memnew(Label);
		label->set_text(TTR("Tool version command (one arg per line)"));
		command_fields->add_child(label);
		command_fields->add_child(tool_version_edit);
	}

	options_section = memnew(VBoxContainer);
	options_section->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	details_container->add_child(options_section);

	options_hint = memnew(Label);
	options_hint->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
	options_section->add_child(options_hint);

	generic_options_edit = memnew(TextEdit);
	generic_options_edit->set_custom_minimum_size(Size2(0, 70) * EDSCALE);
	generic_options_edit->connect(SceneStringName(text_changed), callable_mp(this, &FSBuildPipelineSettingsDialog::_details_changed));
	options_section->add_child(generic_options_edit);

	working_directory_edit = memnew(LineEdit);
	working_directory_edit->set_placeholder("res://");
	working_directory_edit->connect(SceneStringName(text_changed), callable_mp(this, &FSBuildPipelineSettingsDialog::_details_changed).unbind(1));
	add_labeled(TTR("Working directory"), working_directory_edit);

	inputs_edit = memnew(TextEdit);
	inputs_edit->set_custom_minimum_size(Size2(0, 60) * EDSCALE);
	inputs_edit->connect(SceneStringName(text_changed), callable_mp(this, &FSBuildPipelineSettingsDialog::_details_changed));
	add_labeled(TTR("Inputs (one path or glob per line)"), inputs_edit);

	outputs_edit = memnew(TextEdit);
	outputs_edit->set_custom_minimum_size(Size2(0, 60) * EDSCALE);
	outputs_edit->connect(SceneStringName(text_changed), callable_mp(this, &FSBuildPipelineSettingsDialog::_details_changed));
	add_labeled(TTR("Outputs (one path or glob per line)"), outputs_edit);

	environment_edit = memnew(TextEdit);
	environment_edit->set_custom_minimum_size(Size2(0, 50) * EDSCALE);
	environment_edit->connect(SceneStringName(text_changed), callable_mp(this, &FSBuildPipelineSettingsDialog::_details_changed));
	add_labeled(TTR("Environment (KEY=VALUE per line)"), environment_edit);

	HBoxContainer *timeout_row = memnew(HBoxContainer);
	timeout_toggle = memnew(CheckButton);
	timeout_toggle->set_text(TTR("Override timeout"));
	timeout_toggle->connect(SceneStringName(toggled), callable_mp(this, &FSBuildPipelineSettingsDialog::_timeout_toggled));
	timeout_row->add_child(timeout_toggle);
	timeout_spin = memnew(SpinBox);
	timeout_spin->set_min(1);
	timeout_spin->set_max(86400);
	timeout_spin->set_value(60);
	timeout_spin->set_suffix(TTR("s"));
	timeout_spin->connect(SceneStringName(value_changed), callable_mp(this, &FSBuildPipelineSettingsDialog::_details_changed).unbind(1));
	timeout_row->add_child(timeout_spin);
	details_container->add_child(timeout_row);

	root->add_child(memnew(HSeparator));

	validation_label = memnew(Label);
	validation_label->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
	root->add_child(validation_label);

	Label *preview_label = memnew(Label);
	preview_label->set_text(TTR("project.foundry preview"));
	root->add_child(preview_label);

	preview_text = memnew(TextEdit);
	preview_text->set_editable(false);
	preview_text->set_custom_minimum_size(Size2(0, 140) * EDSCALE);
	root->add_child(preview_text);

	HBoxContainer *actions = memnew(HBoxContainer);
	root->add_child(actions);

	run_task_button = memnew(Button);
	run_task_button->set_text(TTR("Run Selected Task"));
	run_task_button->connect(SceneStringName(pressed), callable_mp(this, &FSBuildPipelineSettingsDialog::_run_selected_task));
	actions->add_child(run_task_button);

	run_stage_button = memnew(Button);
	run_stage_button->set_text(TTR("Run Stage"));
	run_stage_button->connect(SceneStringName(pressed), callable_mp(this, &FSBuildPipelineSettingsDialog::_run_selected_stage));
	actions->add_child(run_stage_button);

	clear_state_button = memnew(Button);
	clear_state_button->set_text(TTR("Clear Cached Build State"));
	clear_state_button->connect(SceneStringName(pressed), callable_mp(this, &FSBuildPipelineSettingsDialog::_clear_cached_state));
	actions->add_child(clear_state_button);

	run_output = memnew(RichTextLabel);
	run_output->set_use_bbcode(true);
	run_output->set_custom_minimum_size(Size2(0, 90) * EDSCALE);
	root->add_child(run_output);
}

#endif // TOOLS_ENABLED
