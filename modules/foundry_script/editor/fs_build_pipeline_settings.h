/**************************************************************************/
/*  fs_build_pipeline_settings.h                                          */
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

#ifdef TOOLS_ENABLED

#include "core/config/project_build_pipeline_config.h"

#include "scene/gui/dialogs.h"

class Button;
class CheckButton;
class ItemList;
class Label;
class LineEdit;
class OptionButton;
class RichTextLabel;
class SpinBox;
class TextEdit;
class VBoxContainer;

// The editor authoring UI for the project build pipeline. It is the GUI counterpart to hand-editing
// the `[build]`, `[build/tasks/*]`, and `[build/providers/*]` sections of `project.foundry`: the
// dialog edits an in-memory ProjectBuildPipelineConfig and, on save, writes those sections back
// through ConfigFile conventions so the project stays the source of truth.
//
// Provider discovery for the selector is descriptor-based and never executes provider code, so an
// untrusted project can still be inspected and edited. Provider-supplied option schemas require
// loading provider code and are therefore only used once the project's build tasks are trusted;
// before that the dialog falls back to a generic options editor. Run actions (run task, run stage,
// clear cached build state) are disabled until the project is trusted.
class FSBuildPipelineSettingsDialog : public ConfirmationDialog {
	FOUNDRY_CLASS(FSBuildPipelineSettingsDialog, ConfirmationDialog);

	ProjectBuildPipelineConfig working_config;
	ProjectBuildPipelineConfig::Stage selected_stage = ProjectBuildPipelineConfig::STAGE_PRE_COMPILE;
	String selected_task;
	// Snapshot of the configuration as last loaded from or saved to project.foundry. Run actions
	// operate on the persisted project, so they are only allowed while the editor matches this.
	String saved_preview_snapshot;
	bool project_trusted = false;
	// Guards widget-population against the change signals it would otherwise re-trigger.
	bool updating_ui = false;

	CheckButton *enabled_toggle = nullptr;
	Label *trust_label = nullptr;
	Button *trust_button = nullptr;

	ItemList *pre_compile_list = nullptr;
	ItemList *post_compile_list = nullptr;
	Button *add_button = nullptr;
	Button *duplicate_button = nullptr;
	Button *remove_button = nullptr;
	Button *move_up_button = nullptr;
	Button *move_down_button = nullptr;

	VBoxContainer *details_container = nullptr;
	Label *details_placeholder = nullptr;
	LineEdit *name_edit = nullptr;
	OptionButton *provider_select = nullptr;
	CheckButton *task_enabled_toggle = nullptr;

	VBoxContainer *command_fields = nullptr;
	LineEdit *command_edit = nullptr;
	TextEdit *args_edit = nullptr;
	TextEdit *tool_version_edit = nullptr;

	LineEdit *working_directory_edit = nullptr;
	TextEdit *inputs_edit = nullptr;
	TextEdit *outputs_edit = nullptr;
	TextEdit *environment_edit = nullptr;
	CheckButton *timeout_toggle = nullptr;
	SpinBox *timeout_spin = nullptr;

	VBoxContainer *options_section = nullptr;
	Label *options_hint = nullptr;
	TextEdit *generic_options_edit = nullptr;

	Label *validation_label = nullptr;
	TextEdit *preview_text = nullptr;
	RichTextLabel *run_output = nullptr;
	Button *run_task_button = nullptr;
	Button *run_stage_button = nullptr;
	Button *clear_state_button = nullptr;

	void _build_available_registry(class FoundryBuildTaskRegistry &r_registry) const;

	void _load_from_project();
	void _save_to_project();

	void _refresh_all();
	void _refresh_stage_lists();
	void _refresh_provider_select();
	void _refresh_details();
	void _refresh_options_editor();
	void _refresh_validation_and_preview();
	void _refresh_actions();

	void _select_stage_task(ProjectBuildPipelineConfig::Stage p_stage, const String &p_task);
	ProjectBuildPipelineConfig::TaskDefinition _read_details_into_task(const String &p_name) const;
	void _commit_details();

	void _enabled_toggled(bool p_pressed);
	void _trust_pressed();
	void _pre_list_selected(int p_index);
	void _post_list_selected(int p_index);
	void _add_task();
	void _duplicate_task();
	void _remove_task();
	void _move_task(int p_delta);
	void _provider_selected(int p_index);
	void _name_submitted();
	void _details_changed();
	void _task_enabled_toggled(bool p_pressed);
	void _timeout_toggled(bool p_pressed);

	void _run_selected_task();
	void _run_selected_stage();
	void _clear_cached_state();

	static PackedStringArray _lines_to_string_array(const String &p_text);
	static PackedStringArray _lines_to_argv(const String &p_text);
	static String _string_array_to_lines(const PackedStringArray &p_array);
	bool _has_unsaved_changes() const;
	static Dictionary _lines_to_string_dictionary(const String &p_text);
	static String _string_dictionary_to_lines(const Dictionary &p_dictionary);

public:
	void popup_settings();

	FSBuildPipelineSettingsDialog();
};

#endif // TOOLS_ENABLED
