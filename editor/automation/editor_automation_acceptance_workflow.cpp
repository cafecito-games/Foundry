/**************************************************************************/
/*  editor_automation_acceptance_workflow.cpp                             */
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

#include "editor_automation_acceptance_workflow.h"

#include "editor/automation/editor_automation_snapshot.h"
#include "editor/automation/editor_workflow_test_driver.h"
#include "editor/editor_node.h"

#include "core/io/json.h"
#include "core/object/message_queue.h"
#include "core/os/os.h"
#include "scene/main/scene_tree.h"

namespace {

Dictionary _selector_button_in_dock(const String &p_dock_name, const String &p_button_name) {
	Dictionary within;
	within["role"] = "dock";
	within["name"] = p_dock_name;
	Dictionary selector;
	selector["role"] = "button";
	selector["name"] = p_button_name;
	selector["within"] = within;
	return selector;
}

Dictionary _selector_within_modal(const Dictionary &p_selector) {
	Dictionary within;
	within["role"] = "dialog";
	Dictionary selector = p_selector;
	selector["within"] = within;
	return selector;
}

void _flush_frames(int p_count = 1) {
	for (int i = 0; i < p_count; i++) {
		if (SceneTree::get_singleton() != nullptr) {
			SceneTree::get_singleton()->process(1.0 / 60.0);
		}
		if (MessageQueue::get_singleton() != nullptr) {
			MessageQueue::get_singleton()->flush();
		}
		OS::get_singleton()->delay_usec(1000);
	}
}

bool _selected_nodes_contain_class(EditorWorkflowTestDriver &p_driver, const String &p_class_name) {
	const Dictionary state = p_driver.read_editor_state();
	const Array selected = state.get("selected_nodes", Array());
	for (int i = 0; i < selected.size(); i++) {
		const Dictionary entry = selected[i];
		if (String(entry.get("class", String())) == p_class_name) {
			return true;
		}
	}
	return false;
}

bool _scene_is_unsaved(EditorWorkflowTestDriver &p_driver) {
	const Dictionary state = p_driver.read_editor_state();
	const Dictionary unsaved = state.get("unsaved", Dictionary());
	return (bool)unsaved.get("current_scene", false);
}

bool _is_playing(EditorWorkflowTestDriver &p_driver) {
	const Dictionary state = p_driver.read_editor_state();
	const Dictionary playing = state.get("playing", Dictionary());
	return (bool)playing.get("is_playing", false);
}

EditorAutomationAcceptanceWorkflow::Result _failure_from_driver(EditorWorkflowTestDriver &p_driver, const String &p_workflow, const String &p_message = String()) {
	EditorAutomationAcceptanceWorkflow::Result result;
	result.ok = false;
	result.workflow = p_workflow;
	result.message = p_message.is_empty() ? p_driver.get_failure().message : p_message;
	Dictionary details;
	details["step"] = p_driver.get_current_step();
	details["failure_report"] = p_driver.format_failure_report();
	if (!p_driver.get_failure().diagnostics.is_empty()) {
		details["diagnostics"] = p_driver.get_failure().diagnostics;
	}
	result.details = details;
	return result;
}

} // namespace

EditorAutomationAcceptanceWorkflow::Result EditorAutomationAcceptanceWorkflow::run_mvp(EditorWorkflowTestDriver &p_driver, const String &p_scene_path) {
	Result result;
	result.workflow = "mvp";

	p_driver.begin_workflow();

	// Setup: open the fixture scene directly. Post-setup steps use automation only.
	p_driver.set_step("setup_open_scene");
#ifdef TOOLS_ENABLED
	EditorNode *editor_node = EditorNode::get_singleton();
	if (editor_node == nullptr || !editor_node->is_editor_ready()) {
		result.ok = false;
		result.message = "EditorNode is not ready.";
		return result;
	}
	if (editor_node->load_scene(p_scene_path) != OK) {
		result.ok = false;
		result.message = vformat("Failed to load setup scene '%s'.", p_scene_path);
		return result;
	}
#endif
	_flush_frames(30);

	// 1. Open the Create/Add Node dialog from the scene tree dock.
	p_driver.set_step("open_add_child_node_dialog");
	Dictionary add_child_button = _selector_button_in_dock("Scene", "Add Child Node");
	if (!p_driver.require_ok(p_driver.act(add_child_button, "click"), "open_add_child_node_dialog")) {
		return _failure_from_driver(p_driver, result.workflow);
	}
	_flush_frames(5);

	p_driver.set_step("wait_for_create_dialog");
	Dictionary wait_dialog;
	wait_dialog["type"] = "selector_appears";
	Dictionary search_selector;
	search_selector["role"] = "text_field";
	search_selector["name"] = "Search";
	wait_dialog["selector"] = _selector_within_modal(search_selector);
	if (!p_driver.require_ok(p_driver.wait_for(wait_dialog, 15000), "wait_for_create_dialog")) {
		return _failure_from_driver(p_driver, result.workflow);
	}

	// 2. Search for Node2D and create it through the dialog.
	p_driver.set_step("search_node2d");
	Dictionary search_within;
	search_within["role"] = "dialog";
	Dictionary search_field;
	search_field["role"] = "text_field";
	search_field["name"] = "Search";
	search_field["within"] = search_within;
	Dictionary search_args;
	search_args["text"] = "Node2D";
	if (!p_driver.require_ok(p_driver.act(search_field, "set_text", search_args), "search_node2d")) {
		return _failure_from_driver(p_driver, result.workflow);
	}
	_flush_frames(10);

	p_driver.set_step("select_node2d_match");
	Dictionary node2d_item;
	node2d_item["role"] = "tree_item";
	node2d_item["name"] = "Node2D";
	node2d_item["within"] = search_within;
	if (!p_driver.require_ok(p_driver.act(node2d_item, "select"), "select_node2d_match")) {
		return _failure_from_driver(p_driver, result.workflow);
	}
	_flush_frames(3);

	p_driver.set_step("confirm_create_node");
	Dictionary create_button;
	create_button["role"] = "button";
	create_button["name"] = "Create";
	create_button["within"] = search_within;
	if (!p_driver.require_ok(p_driver.act(create_button, "click"), "confirm_create_node")) {
		return _failure_from_driver(p_driver, result.workflow);
	}
	_flush_frames(20);

	// 3. Confirm Node2D is selected in the scene tree.
	p_driver.set_step("verify_node2d_selected");
	Dictionary wait_selected;
	wait_selected["type"] = "next_frame";
	p_driver.wait_for(wait_selected, 1000);
	if (!_selected_nodes_contain_class(p_driver, "Node2D")) {
		Result fail;
		fail.ok = false;
		fail.workflow = result.workflow;
		fail.message = "Expected Node2D to be selected in the scene tree.";
		Dictionary details;
		details["step"] = "verify_node2d_selected";
		details["editor_state"] = p_driver.read_editor_state();
		fail.details = details;
		return fail;
	}

	// 4. Edit position.x through the inspector spinbox UI.
	p_driver.set_step("edit_inspector_position_x");
	Dictionary inspector_dock;
	inspector_dock["role"] = "dock";
	inspector_dock["name"] = "Inspector";
	Dictionary within_inspector;
	within_inspector["role"] = "dock";
	within_inspector["name"] = "Inspector";
	Dictionary within_position;
	within_position["role"] = "property_row";
	within_position["name"] = "Position";
	within_position["within"] = within_inspector;
	Dictionary position_x_spin;
	position_x_spin["role"] = "control";
	position_x_spin["name"] = "x";
	position_x_spin["within"] = within_position;
	Dictionary position_args;
	position_args["value"] = 42.0;
	if (!p_driver.require_ok(p_driver.act(position_x_spin, "set_value", position_args), "edit_inspector_position_x")) {
		return _failure_from_driver(p_driver, result.workflow);
	}
	_flush_frames(10);

	p_driver.set_step("verify_undo_unsaved_state");
	if (!_scene_is_unsaved(p_driver)) {
		Result fail;
		fail.ok = false;
		fail.workflow = result.workflow;
		fail.message = "Inspector edit did not mark the scene unsaved (undo/redo history).";
		Dictionary details;
		details["step"] = "verify_undo_unsaved_state";
		details["editor_state"] = p_driver.read_editor_state();
		fail.details = details;
		return fail;
	}

	// 5. Save the scene through the command palette.
	p_driver.set_step("save_scene");
	if (!p_driver.require_ok(p_driver.run_command("editor/save_scene"), "save_scene")) {
		return _failure_from_driver(p_driver, result.workflow);
	}
	_flush_frames(30);

	p_driver.set_step("wait_after_save");
	Dictionary wait_idle;
	wait_idle["type"] = "editor_idle";
	if (!p_driver.require_ok(p_driver.wait_for(wait_idle, 15000), "wait_after_save")) {
		return _failure_from_driver(p_driver, result.workflow);
	}

	// 6. Run and stop the current scene through command palette paths.
	p_driver.set_step("run_current_scene");
	if (!p_driver.require_ok(p_driver.run_command("editor/run_current_scene"), "run_current_scene")) {
		return _failure_from_driver(p_driver, result.workflow);
	}

	p_driver.set_step("wait_for_playing");
	uint64_t play_deadline = OS::get_singleton()->get_ticks_msec() + 20000;
	while (OS::get_singleton()->get_ticks_msec() < play_deadline) {
		_flush_frames(5);
		if (_is_playing(p_driver)) {
			break;
		}
	}
	if (!_is_playing(p_driver)) {
		Result fail;
		fail.ok = false;
		fail.workflow = result.workflow;
		fail.message = "Scene did not enter playing state after run_current_scene.";
		Dictionary details;
		details["step"] = "wait_for_playing";
		details["editor_state"] = p_driver.read_editor_state();
		fail.details = details;
		return fail;
	}

	p_driver.set_step("stop_running_scene");
	if (!p_driver.require_ok(p_driver.run_command("editor/stop_running_project"), "stop_running_scene")) {
		return _failure_from_driver(p_driver, result.workflow);
	}

	p_driver.set_step("wait_for_stopped");
	uint64_t stop_deadline = OS::get_singleton()->get_ticks_msec() + 20000;
	while (OS::get_singleton()->get_ticks_msec() < stop_deadline) {
		_flush_frames(5);
		if (!_is_playing(p_driver)) {
			break;
		}
	}
	if (_is_playing(p_driver)) {
		Result fail;
		fail.ok = false;
		fail.workflow = result.workflow;
		fail.message = "Scene did not stop after stop_running_project.";
		Dictionary details;
		details["step"] = "wait_for_stopped";
		details["editor_state"] = p_driver.read_editor_state();
		fail.details = details;
		return fail;
	}

	// 7. Assert no new editor errors during the workflow.
	p_driver.set_step("assert_no_new_errors");
	if (!p_driver.assert_no_new_errors()) {
		return _failure_from_driver(p_driver, result.workflow);
	}

	result.ok = true;
	result.message = "MVP editor automation acceptance workflow completed.";
	return result;
}

void EditorAutomationAcceptanceWorkflow::print_result(const Result &p_result) {
	Dictionary payload;
	payload["workflow"] = p_result.workflow;
	payload["ok"] = p_result.ok;
	payload["message"] = p_result.message;
	if (!p_result.details.is_empty()) {
		payload["details"] = p_result.details;
	}
	OS::get_singleton()->print("FOUNDRY_AUTOMATION_WORKFLOW %s\n", JSON::stringify(payload, "", false).utf8().get_data());
	if (!p_result.ok) {
		if (p_result.details.has("failure_report")) {
			OS::get_singleton()->printerr("%s", String(p_result.details["failure_report"]).utf8().get_data());
		} else {
			OS::get_singleton()->printerr("%s\n", p_result.message.utf8().get_data());
		}
	}
}
