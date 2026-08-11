/**************************************************************************/
/*  editor_automation_workflow_registry.cpp                               */
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

#include "editor_automation_workflow_registry.h"

#include "editor/automation/editor_workflow_test_driver.h"

#include "core/string/ustring.h"

namespace {

EditorAutomationAcceptanceWorkflow::Result _run_basic_scene_editing(EditorWorkflowTestDriver &p_driver) {
	return EditorAutomationAcceptanceWorkflow::run_basic_scene_editing(p_driver);
}

EditorAutomationAcceptanceWorkflow::Result _run_canvas_2d_zoom_automation(EditorWorkflowTestDriver &p_driver) {
	return EditorAutomationAcceptanceWorkflow::run_canvas_2d_zoom_automation(p_driver);
}

EditorAutomationAcceptanceWorkflow::Result _run_cross_board_tile_body_drop(EditorWorkflowTestDriver &p_driver) {
	return EditorAutomationAcceptanceWorkflow::run_cross_board_tile_body_drop(p_driver);
}

EditorAutomationAcceptanceWorkflow::Result _run_cross_board_tab_strip_drop(EditorWorkflowTestDriver &p_driver) {
	return EditorAutomationAcceptanceWorkflow::run_cross_board_tab_strip_drop(p_driver);
}

EditorAutomationAcceptanceWorkflow::Result _run_continuous_drag_arms_drop_overlay(EditorWorkflowTestDriver &p_driver) {
	return EditorAutomationAcceptanceWorkflow::run_continuous_drag_arms_drop_overlay(p_driver);
}

EditorAutomationAcceptanceWorkflow::Result _run_mixed_workspace_editing(EditorWorkflowTestDriver &p_driver) {
	return EditorAutomationAcceptanceWorkflow::run_mixed_workspace_editing(p_driver);
}

EditorAutomationAcceptanceWorkflow::Result _run_board_switch_3d_scene(EditorWorkflowTestDriver &p_driver) {
	return EditorAutomationAcceptanceWorkflow::run_board_switch_3d_scene(p_driver);
}

#ifdef TESTS_ENABLED
EditorAutomationAcceptanceWorkflow::Result _run_board_close_3d_context_lifetime(EditorWorkflowTestDriver &p_driver) {
	return EditorAutomationAcceptanceWorkflow::run_board_close_3d_context_lifetime(p_driver);
}
#endif

EditorAutomationAcceptanceWorkflow::Result _run_passive_preview_input_policy(EditorWorkflowTestDriver &p_driver) {
	return EditorAutomationAcceptanceWorkflow::run_passive_preview_input_policy(p_driver);
}

EditorAutomationAcceptanceWorkflow::Result _run_close_last_scene_empty_pane(EditorWorkflowTestDriver &p_driver) {
	return EditorAutomationAcceptanceWorkflow::run_close_last_scene_empty_pane(p_driver);
}

EditorAutomationAcceptanceWorkflow::Result _run_split_scene_root_button_context(EditorWorkflowTestDriver &p_driver) {
	return EditorAutomationAcceptanceWorkflow::run_split_scene_root_button_context(p_driver);
}

EditorAutomationAcceptanceWorkflow::Result _run_mixed_workspace_seed(EditorWorkflowTestDriver &p_driver) {
	return EditorAutomationAcceptanceWorkflow::run_mixed_workspace_seed(p_driver);
}

EditorAutomationAcceptanceWorkflow::Result _run_mixed_workspace_restore(EditorWorkflowTestDriver &p_driver) {
	return EditorAutomationAcceptanceWorkflow::run_mixed_workspace_restore(p_driver);
}

EditorAutomationAcceptanceWorkflow::Result _run_tile_local_scene_modes(EditorWorkflowTestDriver &p_driver) {
	return EditorAutomationAcceptanceWorkflow::run_tile_local_scene_modes(p_driver);
}

EditorAutomationAcceptanceWorkflow::Result _run_tile_local_scene_modes_restore(EditorWorkflowTestDriver &p_driver) {
	return EditorAutomationAcceptanceWorkflow::run_tile_local_scene_modes_restore(p_driver);
}

EditorAutomationAcceptanceWorkflow::Result _run_main_screen_hidden_shortcuts(EditorWorkflowTestDriver &p_driver) {
	return EditorAutomationAcceptanceWorkflow::run_main_screen_hidden_shortcuts(p_driver);
}

EditorAutomationAcceptanceWorkflow::Result _run_projectless_shell_smoke(EditorWorkflowTestDriver &p_driver) {
	return EditorAutomationAcceptanceWorkflow::run_projectless_shell_smoke(p_driver);
}

EditorAutomationAcceptanceWorkflow::Result _run_projectless_shell_dismiss_quits(EditorWorkflowTestDriver &p_driver) {
	return EditorAutomationAcceptanceWorkflow::run_projectless_shell_dismiss_quits(p_driver);
}

EditorAutomationAcceptanceWorkflow::Result _run_projectless_shell_open_in_process(EditorWorkflowTestDriver &p_driver) {
	return EditorAutomationAcceptanceWorkflow::run_projectless_shell_open_in_process(p_driver);
}

EditorAutomationAcceptanceWorkflow::Result _run_projectless_shell_open_in_process_fallback(EditorWorkflowTestDriver &p_driver) {
	return EditorAutomationAcceptanceWorkflow::run_projectless_shell_open_in_process_fallback(p_driver);
}

EditorAutomationAcceptanceWorkflow::Result _run_projectless_shell_open_in_process_autoload(EditorWorkflowTestDriver &p_driver) {
	return EditorAutomationAcceptanceWorkflow::run_projectless_shell_open_in_process_autoload(p_driver);
}

EditorAutomationAcceptanceWorkflow::Result _run_projectless_shell_open_in_process_debug_options(EditorWorkflowTestDriver &p_driver) {
	return EditorAutomationAcceptanceWorkflow::run_projectless_shell_open_in_process_debug_options(p_driver);
}

EditorAutomationAcceptanceWorkflow::Result _run_startup_dialog_projects_tab(EditorWorkflowTestDriver &p_driver) {
	return EditorAutomationAcceptanceWorkflow::run_startup_dialog_projects_tab(p_driver);
}

EditorAutomationAcceptanceWorkflow::Result _run_startup_dialog_manage_tab(EditorWorkflowTestDriver &p_driver) {
	return EditorAutomationAcceptanceWorkflow::run_startup_dialog_manage_tab(p_driver);
}

EditorAutomationAcceptanceWorkflow::Result _run_startup_dialog_about_tab(EditorWorkflowTestDriver &p_driver) {
	return EditorAutomationAcceptanceWorkflow::run_startup_dialog_about_tab(p_driver);
}

} // namespace

HashMap<String, EditorAutomationWorkflowRegistry::WorkflowEntry> EditorAutomationWorkflowRegistry::workflows;
HashMap<String, String> EditorAutomationWorkflowRegistry::alias_to_canonical;
bool EditorAutomationWorkflowRegistry::initialized = false;

void EditorAutomationWorkflowRegistry::_register_workflow(const String &p_canonical_name, WorkflowFn p_run, const PackedStringArray &p_aliases) {
	WorkflowEntry entry;
	entry.canonical_name = p_canonical_name;
	entry.run = p_run;
	entry.aliases = p_aliases;
	workflows[p_canonical_name] = entry;
	alias_to_canonical[p_canonical_name] = p_canonical_name;
	for (int i = 0; i < p_aliases.size(); i++) {
		alias_to_canonical[p_aliases[i]] = p_canonical_name;
	}
}

void EditorAutomationWorkflowRegistry::_ensure_initialized() {
	if (initialized) {
		return;
	}
	register_builtin_workflows();
	initialized = true;
}

void EditorAutomationWorkflowRegistry::register_builtin_workflows() {
	workflows.clear();
	alias_to_canonical.clear();

	PackedStringArray basic_scene_aliases;
	basic_scene_aliases.push_back("mvp");
	_register_workflow("basic_scene_editing", &_run_basic_scene_editing, basic_scene_aliases);
	_register_workflow("canvas_2d_zoom_automation", &_run_canvas_2d_zoom_automation);
	_register_workflow("close_last_scene_empty_pane", &_run_close_last_scene_empty_pane);
	_register_workflow("split_scene_root_button_context", &_run_split_scene_root_button_context);
	_register_workflow("cross_board_tile_body_drop", &_run_cross_board_tile_body_drop);
	_register_workflow("cross_board_tab_strip_drop", &_run_cross_board_tab_strip_drop);
	_register_workflow("continuous_drag_arms_drop_overlay", &_run_continuous_drag_arms_drop_overlay);
	_register_workflow("mixed_workspace_editing", &_run_mixed_workspace_editing);
	_register_workflow("mixed_workspace_seed", &_run_mixed_workspace_seed);
	_register_workflow("mixed_workspace_restore", &_run_mixed_workspace_restore);
	_register_workflow("tile_local_scene_modes", &_run_tile_local_scene_modes);
	_register_workflow("tile_local_scene_modes_restore", &_run_tile_local_scene_modes_restore);
	_register_workflow("main_screen_hidden_shortcuts", &_run_main_screen_hidden_shortcuts);
	_register_workflow("board_switch_3d_scene", &_run_board_switch_3d_scene);
#ifdef TESTS_ENABLED
	_register_workflow("board_close_3d_context_lifetime", &_run_board_close_3d_context_lifetime);
#endif
	_register_workflow("passive_preview_input_policy", &_run_passive_preview_input_policy);
	_register_workflow("projectless_shell_smoke", &_run_projectless_shell_smoke);
	_register_workflow("projectless_shell_dismiss_quits", &_run_projectless_shell_dismiss_quits);
	_register_workflow("projectless_shell_open_in_process", &_run_projectless_shell_open_in_process);
	_register_workflow("projectless_shell_open_in_process_fallback", &_run_projectless_shell_open_in_process_fallback);
	_register_workflow("projectless_shell_open_in_process_autoload", &_run_projectless_shell_open_in_process_autoload);
	_register_workflow("projectless_shell_open_in_process_debug_options", &_run_projectless_shell_open_in_process_debug_options);
	_register_workflow("startup_dialog_projects_tab", &_run_startup_dialog_projects_tab);
	_register_workflow("startup_dialog_manage_tab", &_run_startup_dialog_manage_tab);
	_register_workflow("startup_dialog_about_tab", &_run_startup_dialog_about_tab);
}

String EditorAutomationWorkflowRegistry::resolve_canonical_name(const String &p_name) {
	_ensure_initialized();
	const String *canonical = alias_to_canonical.getptr(p_name);
	if (canonical != nullptr) {
		return *canonical;
	}
	return String();
}

bool EditorAutomationWorkflowRegistry::has_workflow(const String &p_name) {
	return !resolve_canonical_name(p_name).is_empty();
}

EditorAutomationAcceptanceWorkflow::Result EditorAutomationWorkflowRegistry::run(const String &p_name, EditorWorkflowTestDriver &p_driver) {
	_ensure_initialized();
	const String canonical = resolve_canonical_name(p_name);
	if (canonical.is_empty()) {
		EditorAutomationAcceptanceWorkflow::Result result;
		result.ok = false;
		result.workflow = p_name;
		result.message = format_unknown_workflow_message(p_name);
		return result;
	}

	const WorkflowEntry *entry = workflows.getptr(canonical);
	ERR_FAIL_NULL_V(entry, EditorAutomationAcceptanceWorkflow::Result());
	ERR_FAIL_NULL_V(entry->run, EditorAutomationAcceptanceWorkflow::Result());
	return entry->run(p_driver);
}

PackedStringArray EditorAutomationWorkflowRegistry::list_workflow_names() {
	_ensure_initialized();
	PackedStringArray names;
	for (const KeyValue<String, WorkflowEntry> &pair : workflows) {
		names.push_back(pair.key);
	}
	names.sort();
	return names;
}

String EditorAutomationWorkflowRegistry::format_unknown_workflow_message(const String &p_name) {
	_ensure_initialized();
	const PackedStringArray names = list_workflow_names();
	String message = vformat("Unknown automation workflow '%s'.", p_name);
	if (!names.is_empty()) {
		message += " Known workflows: " + String(", ").join(names) + ".";
	}
	return message;
}
