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
#include "editor/docks/scene_tree_dock.h"
#include "editor/editor_node.h"

#include "core/io/json.h"
#include "core/object/message_queue.h"
#include "core/os/os.h"
#include "scene/main/scene_tree.h"

namespace {

Dictionary _inspector_visible_property_selector() {
	Dictionary selector;
	selector["role"] = "property_row";
	selector["class"] = "EditorPropertyCheck";
	Dictionary metadata;
	metadata["property_path"] = "visible";
	selector["metadata"] = metadata;
	return selector;
}

Dictionary _selector_within_modal(const Dictionary &p_selector) {
	Dictionary within;
	within["role"] = "dialog";
	within["focused"] = true;
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

bool _selected_node_is_child_node2d(EditorWorkflowTestDriver &p_driver) {
	const Dictionary state = p_driver.read_editor_state();
	const Array selected = state.get("selected_nodes", Array());
	for (int i = 0; i < selected.size(); i++) {
		const Dictionary entry = selected[i];
		if (String(entry.get("class", String())) == "Node2D" && String(entry.get("name", String())) != "Main") {
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

void _expand_inspector(EditorWorkflowTestDriver &p_driver) {
	if (!p_driver.require_ok(p_driver.run_command("property_editor/expand_all"), "expand_inspector")) {
		return;
	}
	_flush_frames(15);
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

	// 1. Open the Create/Add Node dialog from the focused tile's scene tree dock.
	p_driver.set_step("focus_scene_tree_dock");
	if (!p_driver.require_ok(p_driver.run_command("docks/open_scene"), "focus_scene_tree_dock")) {
		return _failure_from_driver(p_driver, result.workflow);
	}
	_flush_frames(10);

	p_driver.set_step("open_add_child_node_dialog");
	SceneTreeDock *scene_dock = SceneTreeDock::get_singleton();
	if (scene_dock == nullptr) {
		result.ok = false;
		result.message = "Focused scene tree dock is unavailable.";
		return result;
	}
	scene_dock->open_add_child_dialog();
	_flush_frames(10);

	// 2. Search for Node2D and create it through the dialog.
	p_driver.set_step("search_node2d");
	Dictionary search_field_base;
	search_field_base["role"] = "text_field";
	search_field_base["name"] = "Search";
	Dictionary search_field = _selector_within_modal(search_field_base);
	Dictionary search_args;
	search_args["text"] = "Node2D";
	if (!p_driver.require_ok(p_driver.act(search_field, "set_text", search_args), "search_node2d")) {
		return _failure_from_driver(p_driver, result.workflow);
	}
	_flush_frames(10);

	p_driver.set_step("select_node2d_match");
	Dictionary node2d_base;
	node2d_base["role"] = "tree_item";
	node2d_base["name"] = "Node2D";
	Dictionary node2d_item = _selector_within_modal(node2d_base);
	node2d_item["nth"] = 0;
	if (!p_driver.require_ok(p_driver.act(node2d_item, "select"), "select_node2d_match")) {
		return _failure_from_driver(p_driver, result.workflow);
	}
	_flush_frames(3);

	p_driver.set_step("confirm_create_node");
	Dictionary create_button_base;
	create_button_base["role"] = "button";
	create_button_base["name"] = "Create";
	Dictionary create_button = _selector_within_modal(create_button_base);
	if (!p_driver.require_ok(p_driver.act(create_button, "click"), "confirm_create_node")) {
		return _failure_from_driver(p_driver, result.workflow);
	}
	_flush_frames(20);

	// 3. Confirm Node2D is selected in the scene tree.
	p_driver.set_step("verify_node2d_selected");
	Dictionary wait_selected;
	wait_selected["type"] = "editor_idle";
	p_driver.wait_for(wait_selected, 5000);
	if (!_selected_node_is_child_node2d(p_driver)) {
		Result fail;
		fail.ok = false;
		fail.workflow = result.workflow;
		fail.message = "Expected the new Node2D child to be selected in the scene tree.";
		Dictionary details;
		details["step"] = "verify_node2d_selected";
		details["editor_state"] = p_driver.read_editor_state();
		fail.details = details;
		return fail;
	}

	// 4. Edit the Visible property through the inspector property row UI.
	p_driver.set_step("open_inspector_dock");
	if (!p_driver.require_ok(p_driver.run_command("docks/open_inspector"), "open_inspector_dock")) {
		return _failure_from_driver(p_driver, result.workflow);
	}
#ifdef TOOLS_ENABLED
	if (editor_node != nullptr) {
		editor_node->edit_current();
	}
#endif
	_flush_frames(30);

	p_driver.set_step("expand_inspector");
	_expand_inspector(p_driver);
	if (p_driver.has_failed()) {
		return _failure_from_driver(p_driver, result.workflow);
	}

	p_driver.set_step("edit_inspector_visible");
	const Dictionary visible_property = _inspector_visible_property_selector();
	const uint64_t inspector_deadline = OS::get_singleton()->get_ticks_msec() + 15000;
	bool edited_visible = false;
	int last_match_count = 0;
	while (OS::get_singleton()->get_ticks_msec() < inspector_deadline) {
		const Dictionary found = p_driver.find(visible_property);
		last_match_count = (int)found.get("match_count", 0);
		if ((bool)found.get("ok", false) && last_match_count >= 1) {
			Dictionary act_selector = visible_property;
			if (last_match_count > 1) {
				act_selector["nth"] = 0;
			}
			if (p_driver.require_ok(p_driver.act(act_selector, "click"), "edit_inspector_visible")) {
				edited_visible = true;
				break;
			}
			return _failure_from_driver(p_driver, result.workflow);
		}
		_flush_frames(5);
	}
	if (!edited_visible) {
		Result fail;
		fail.ok = false;
		fail.workflow = result.workflow;
		fail.message = "Could not find the Visible inspector property row.";
		Dictionary details;
		details["step"] = "edit_inspector_visible";
		details["last_match_count"] = last_match_count;
		const EditorAutomationSnapshot snapshot = EditorAutomationSnapshot::capture_from_editor();
		details["snapshot_element_count"] = snapshot.get_element_count();
		int property_row_hits = 0;
		for (int i = 0; i < snapshot.get_element_count(); i++) {
			if (snapshot.get_element(i).role == "property_row") {
				property_row_hits++;
			}
		}
		details["property_row_hits"] = property_row_hits;
		fail.details = details;
		return fail;
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
