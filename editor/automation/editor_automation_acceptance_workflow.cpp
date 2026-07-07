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
#include "editor/editor_scene_workspace.h"
#include "editor/editor_script_leaf.h"
#include "editor/gui/code_editor.h"
#include "editor/script/script_editor_controller.h"
#include "editor/script/script_editor_plugin.h"
#include "editor/script/script_editor_view.h"
#include "editor/workspace/workspace_pane.h"
#include "editor/workspace/workspace_tab_type.h"

#include "core/io/json.h"
#include "core/os/os.h"
#include "scene/gui/dialogs.h"

namespace {

static constexpr const char *MIXED_WORKSPACE_SCENE = "res://scenes/main.tscn";
static constexpr const char *MIXED_WORKSPACE_SCRIPT = "res://scripts/player.fs";

ScriptLeaf *_find_mounted_script_leaf(WorkspacePane *p_pane) {
	Control *host = p_pane ? p_pane->get_chrome_host() : nullptr;
	if (host == nullptr) {
		return nullptr;
	}
	for (int i = 0; i < host->get_child_count(false); i++) {
		if (ScriptLeaf *leaf = Object::cast_to<ScriptLeaf>(host->get_child(i, false))) {
			return leaf;
		}
	}
	return nullptr;
}

ConfirmationDialog *_find_erase_confirm(ScriptEditorView *p_view) {
	if (p_view == nullptr) {
		return nullptr;
	}
	for (int i = 0; i < p_view->get_child_count(); i++) {
		if (ConfirmationDialog *dialog = Object::cast_to<ConfirmationDialog>(p_view->get_child(i))) {
			return dialog;
		}
	}
	return nullptr;
}

int _find_workspace_tab_index(WorkspacePane *p_pane, const StringName &p_type_id, const String &p_resource_key = String()) {
	if (p_pane == nullptr) {
		return -1;
	}
	for (int i = 0; i < p_pane->get_tab_count(); i++) {
		const WorkspaceTab &tab = p_pane->get_tab(i);
		if (tab.get_type_id() == p_type_id && (p_resource_key.is_empty() || tab.get_resource_key() == p_resource_key)) {
			return i;
		}
	}
	return -1;
}

bool _pane_has_tab(WorkspacePane *p_pane, const StringName &p_type_id, const String &p_resource_key = String()) {
	return _find_workspace_tab_index(p_pane, p_type_id, p_resource_key) >= 0;
}

EditorAutomationAcceptanceWorkflow::Result _failure_with_message(EditorWorkflowTestDriver &p_driver, const String &p_workflow, const String &p_message) {
	EditorAutomationAcceptanceWorkflow::Result fail;
	fail.ok = false;
	fail.workflow = p_workflow;
	fail.message = p_message;
	fail.details = p_driver.make_failure_details();
	return fail;
}

bool _assert_visible_workspace_tab(EditorWorkflowTestDriver &p_driver, const String &p_type_id, const String &p_resource_key, int p_tile_id, const String &p_context) {
	Dictionary metadata;
	metadata["type_id"] = p_type_id;
	metadata["resource_key"] = p_resource_key;
	if (p_tile_id >= 0) {
		metadata["tile_id"] = p_tile_id;
	}

	Dictionary selector;
	selector["role"] = "tab";
	selector["metadata"] = metadata;

	const Dictionary found = p_driver.find(selector);
	if (!p_driver.require_ok(found, p_context)) {
		return false;
	}
	return int(found.get("match_count", 0)) >= 1;
}

struct MixedWorkspaceContext {
	EditorNode *editor_node = nullptr;
	EditorSceneWorkspace *workspace = nullptr;
	WorkspaceLeafNode *scene_leaf = nullptr;
	WorkspacePane *scene_pane = nullptr;
	int scene_leaf_id = -1;
};

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

void _expand_inspector(EditorWorkflowTestDriver &p_driver) {
	if (!p_driver.require_ok(p_driver.run_command("property_editor/expand_all"), "expand_inspector")) {
		return;
	}
	p_driver.flush_frames(15);
}

bool _load_mixed_workspace_scene(EditorWorkflowTestDriver &p_driver, const String &p_workflow, MixedWorkspaceContext &r_context, EditorAutomationAcceptanceWorkflow::Result &r_failure) {
	p_driver.set_step("setup_open_scene");

	EditorNode *editor_node = EditorNode::get_singleton();
	if (editor_node == nullptr || !editor_node->is_editor_ready()) {
		r_failure = _failure_with_message(p_driver, p_workflow, "EditorNode is not ready.");
		return false;
	}
	if (editor_node->load_scene(MIXED_WORKSPACE_SCENE) != OK) {
		r_failure = _failure_with_message(p_driver, p_workflow, vformat("Failed to load scene '%s'.", MIXED_WORKSPACE_SCENE));
		return false;
	}
	p_driver.flush_frames(30);

	EditorSceneWorkspace *workspace = EditorNode::get_scene_workspace();
	if (workspace == nullptr) {
		r_failure = _failure_with_message(p_driver, p_workflow, "Scene workspace is unavailable.");
		return false;
	}
	workspace->sync_scene_tabs_from_editor_data();
	p_driver.flush_frames(20);

	WorkspaceLeafNode *scene_leaf = workspace->get_focused_leaf();
	if (scene_leaf == nullptr || scene_leaf->get_pane_tile() == nullptr) {
		for (WorkspaceLeafNode *leaf : workspace->get_leaves()) {
			if (leaf->get_pane_tile() != nullptr) {
				scene_leaf = leaf;
				break;
			}
		}
	}
	WorkspacePane *scene_pane = scene_leaf ? scene_leaf->get_workspace_pane() : nullptr;
	if (scene_leaf == nullptr || scene_pane == nullptr || scene_pane->get_scene_tile() == nullptr) {
		r_failure = _failure_with_message(p_driver, p_workflow, "Could not resolve the scene workspace pane.");
		return false;
	}

	r_context.editor_node = editor_node;
	r_context.workspace = workspace;
	r_context.scene_leaf = scene_leaf;
	r_context.scene_pane = scene_pane;
	r_context.scene_leaf_id = scene_leaf->get_leaf_id();
	return true;
}

bool _add_script_tab_to_scene_pane(EditorWorkflowTestDriver &p_driver, const String &p_workflow, MixedWorkspaceContext &r_context, EditorAutomationAcceptanceWorkflow::Result &r_failure) {
	p_driver.set_step("open_script_in_scene_pane");

	WorkspaceTabRegistry &registry = WorkspacePane::get_shared_tab_registry();
	WorkspaceTabType *script_type = registry.find_type(StringName("script"));
	if (script_type == nullptr) {
		r_failure = _failure_with_message(p_driver, p_workflow, "Workspace script tab type is unavailable.");
		return false;
	}

	WorkspaceTab script_tab = script_type->make_tab(MIXED_WORKSPACE_SCRIPT, registry.allocate_stable_id());
	r_context.scene_pane->add_tab(script_tab);
	r_context.scene_pane->set_active_tab(r_context.scene_pane->get_tab_count() - 1);
	p_driver.flush_frames(30);

	if (r_context.scene_pane->get_tab_count() != 2 ||
			!_pane_has_tab(r_context.scene_pane, StringName("scene"), MIXED_WORKSPACE_SCENE) ||
			!_pane_has_tab(r_context.scene_pane, StringName("script"), MIXED_WORKSPACE_SCRIPT)) {
		r_failure = _failure_with_message(p_driver, p_workflow, "Expected scene and script tabs to share the focused pane.");
		return false;
	}

	if (!_assert_visible_workspace_tab(p_driver, "scene", MIXED_WORKSPACE_SCENE, r_context.scene_leaf_id, "find_scene_tab_same_pane") ||
			!_assert_visible_workspace_tab(p_driver, "script", MIXED_WORKSPACE_SCRIPT, r_context.scene_leaf_id, "find_script_tab_same_pane")) {
		r_failure = _failure_with_message(p_driver, p_workflow, "Scene/script tabs were not visible through semantic tab selectors.");
		return false;
	}

	return true;
}

WorkspaceLeafNode *_split_script_tab_to_right(EditorWorkflowTestDriver &p_driver, const String &p_workflow, MixedWorkspaceContext &r_context, EditorAutomationAcceptanceWorkflow::Result &r_failure) {
	p_driver.set_step("edge_split_script_tab");
	const int script_index = _find_workspace_tab_index(r_context.scene_pane, StringName("script"), MIXED_WORKSPACE_SCRIPT);
	if (script_index < 0) {
		r_failure = _failure_with_message(p_driver, p_workflow, "Could not find script tab before edge split.");
		return nullptr;
	}

	WorkspaceLeafNode *script_leaf = r_context.workspace->handle_tab_drop(
			r_context.scene_leaf_id,
			script_index,
			r_context.scene_leaf,
			EditorSceneWorkspace::DROP_RIGHT);
	p_driver.flush_frames(40);

	WorkspacePane *script_pane = script_leaf ? script_leaf->get_workspace_pane() : nullptr;
	if (script_leaf == nullptr || script_pane == nullptr ||
			r_context.workspace->get_leaf_count() != 2 ||
			script_pane->get_tab_count() != 1 ||
			!_pane_has_tab(script_pane, StringName("script"), MIXED_WORKSPACE_SCRIPT) ||
			!_pane_has_tab(r_context.scene_pane, StringName("scene"), MIXED_WORKSPACE_SCENE)) {
		r_failure = _failure_with_message(p_driver, p_workflow, "Edge-drop did not split the script tab into its own pane.");
		return nullptr;
	}

	return script_leaf;
}

bool _assert_command_routing(EditorWorkflowTestDriver &p_driver, const String &p_workflow, MixedWorkspaceContext &r_context, WorkspaceLeafNode *p_script_leaf, EditorAutomationAcceptanceWorkflow::Result &r_failure) {
	p_driver.set_step("verify_command_routing");

	if (p_script_leaf == nullptr || p_script_leaf->get_workspace_pane() == nullptr) {
		r_failure = _failure_with_message(p_driver, p_workflow, "Script pane is unavailable for command routing verification.");
		return false;
	}

	r_context.workspace->set_focused_leaf(r_context.scene_leaf_id);
	p_driver.flush_frames(5);
	ScenePaneTile *scene_tile = r_context.scene_pane->get_scene_tile();
	if (scene_tile == nullptr || r_context.workspace->get_effective_focused_tile() != scene_tile) {
		r_failure = _failure_with_message(p_driver, p_workflow, "Scene command routing did not target the focused scene tile.");
		return false;
	}

	r_context.workspace->set_focused_leaf(p_script_leaf->get_leaf_id());
	p_driver.flush_frames(15);
	if (r_context.workspace->get_focused_tile() != nullptr || r_context.workspace->get_effective_focused_tile() != scene_tile) {
		r_failure = _failure_with_message(p_driver, p_workflow, "Scene command routing did not fall back to the last focused scene tile while a script pane was focused.");
		return false;
	}

	if (!p_driver.require_ok(p_driver.run_command("docks/open_scene"), "scene_command_from_script_focus")) {
		r_failure = _failure_with_message(p_driver, p_workflow, "Scene dock command failed while script pane was focused.");
		return false;
	}

	WorkspacePane *script_pane = p_script_leaf->get_workspace_pane();
	script_pane->on_focus_entered();
	p_driver.flush_frames(10);
	ScriptEditorController *controller = ScriptEditorController::get_singleton();
	if (controller == nullptr) {
		r_failure = _failure_with_message(p_driver, p_workflow, "Script editor controller is unavailable.");
		return false;
	}
	String script_path;
	int line = -1;
	int column = -1;
	if (!controller->get_current_script_view_state(script_path, line, column) || script_path != MIXED_WORKSPACE_SCRIPT) {
		r_failure = _failure_with_message(p_driver, p_workflow, "Script command routing did not target the focused script tab.");
		return false;
	}

	return true;
}

bool _move_script_tab_back_to_scene_pane(EditorWorkflowTestDriver &p_driver, const String &p_workflow, MixedWorkspaceContext &r_context, WorkspaceLeafNode *p_script_leaf, EditorAutomationAcceptanceWorkflow::Result &r_failure) {
	p_driver.set_step("center_drop_script_back");
	if (p_script_leaf == nullptr) {
		r_failure = _failure_with_message(p_driver, p_workflow, "Script pane is unavailable before center-drop.");
		return false;
	}

	WorkspaceLeafNode *target_scene_leaf = r_context.workspace->get_leaf_by_id(r_context.scene_leaf_id);
	if (target_scene_leaf == nullptr) {
		r_failure = _failure_with_message(p_driver, p_workflow, "Scene pane disappeared before center-drop.");
		return false;
	}

	WorkspaceLeafNode *dest_leaf = r_context.workspace->handle_tab_drop(
			p_script_leaf->get_leaf_id(),
			0,
			target_scene_leaf,
			EditorSceneWorkspace::DROP_CENTER);
	p_driver.flush_frames(80);

	target_scene_leaf = r_context.workspace->get_leaf_by_id(r_context.scene_leaf_id);
	WorkspacePane *target_scene_pane = target_scene_leaf ? target_scene_leaf->get_workspace_pane() : nullptr;
	if (dest_leaf == nullptr ||
			target_scene_leaf == nullptr ||
			target_scene_pane == nullptr ||
			r_context.workspace->get_leaf_count() != 1 ||
			target_scene_pane->get_tab_count() != 2 ||
			!_pane_has_tab(target_scene_pane, StringName("scene"), MIXED_WORKSPACE_SCENE) ||
			!_pane_has_tab(target_scene_pane, StringName("script"), MIXED_WORKSPACE_SCRIPT)) {
		r_failure = _failure_with_message(p_driver, p_workflow, "Center-drop did not move the script tab back and collapse the empty pane.");
		return false;
	}

	r_context.scene_leaf = target_scene_leaf;
	r_context.scene_pane = target_scene_pane;
	return true;
}

bool _close_dirty_script_tab(EditorWorkflowTestDriver &p_driver, const String &p_workflow, MixedWorkspaceContext &r_context, EditorAutomationAcceptanceWorkflow::Result &r_failure) {
	p_driver.set_step("dirty_script_close_prompt");
	const int script_index = _find_workspace_tab_index(r_context.scene_pane, StringName("script"), MIXED_WORKSPACE_SCRIPT);
	if (script_index < 0) {
		r_failure = _failure_with_message(p_driver, p_workflow, "Could not find script tab before dirty close.");
		return false;
	}
	r_context.scene_pane->set_active_tab(script_index);
	p_driver.flush_frames(20);

	ScriptLeaf *script_leaf = _find_mounted_script_leaf(r_context.scene_pane);
	ScriptEditorView *view = script_leaf ? script_leaf->get_script_editor_view() : nullptr;
	TabContainer *tabs = view ? view->get_tab_container() : nullptr;
	ScriptEditorBase *editor = (tabs && tabs->get_tab_count() > 0) ? Object::cast_to<ScriptEditorBase>(tabs->get_tab_control(tabs->get_current_tab())) : nullptr;
	if (editor == nullptr || editor->get_code_editor() == nullptr || editor->get_code_editor()->get_text_editor() == nullptr) {
		r_failure = _failure_with_message(p_driver, p_workflow, "Could not resolve the active script editor for dirty close.");
		return false;
	}

	editor->get_code_editor()->get_text_editor()->insert_text_at_caret("# dirty close acceptance edit\n");
	p_driver.flush_frames(10);
	if (!editor->is_unsaved()) {
		r_failure = _failure_with_message(p_driver, p_workflow, "Script edit did not mark the tab dirty.");
		return false;
	}

	ConfirmationDialog *prompt = _find_erase_confirm(view);
	const WorkspaceTabCloseResult close_result = r_context.scene_pane->request_close_active_tab();
	p_driver.flush_frames(10);
	if (close_result != WorkspaceTabCloseResult::DEFERRED || prompt == nullptr || r_context.scene_pane->get_tab_count() != 2) {
		r_failure = _failure_with_message(p_driver, p_workflow, "Dirty script close did not route through the save/discard prompt.");
		return false;
	}

	prompt->emit_signal(SNAME("custom_action"), "discard");
	p_driver.flush_frames(60);
	if (r_context.scene_pane->get_tab_count() != 1 ||
			!_pane_has_tab(r_context.scene_pane, StringName("scene"), MIXED_WORKSPACE_SCENE) ||
			_pane_has_tab(r_context.scene_pane, StringName("script"), MIXED_WORKSPACE_SCRIPT)) {
		r_failure = _failure_with_message(p_driver, p_workflow, "Discarding the dirty script prompt did not close the workspace script tab.");
		return false;
	}

	r_context.workspace->set_focused_leaf(r_context.scene_leaf_id);
	p_driver.flush_frames(10);
	if (r_context.workspace->get_effective_focused_tile() != r_context.scene_pane->get_scene_tile()) {
		r_failure = _failure_with_message(p_driver, p_workflow, "Scene tile focus was not retained after closing the dirty script tab.");
		return false;
	}
	return true;
}

bool _save_mixed_workspace_layout(EditorWorkflowTestDriver &p_driver, const String &p_workflow, MixedWorkspaceContext &r_context, EditorAutomationAcceptanceWorkflow::Result &r_failure) {
	p_driver.set_step("save_mixed_workspace_layout");
	r_context.workspace->set_focused_leaf(r_context.scene_leaf_id);
	r_context.editor_node->save_editor_layout_delayed();
	p_driver.flush_frames(120);
	if (!p_driver.wait_editor_idle(10000)) {
		r_failure = _failure_with_message(p_driver, p_workflow, "Editor did not become idle after saving mixed workspace layout.");
		return false;
	}
	return true;
}

EditorAutomationAcceptanceWorkflow::Result _failure_from_driver(EditorWorkflowTestDriver &p_driver, const String &p_workflow, const String &p_message = String()) {
	EditorAutomationAcceptanceWorkflow::Result result;
	result.ok = false;
	result.workflow = p_workflow;
	result.message = p_message.is_empty() ? p_driver.get_failure().message : p_message;
	result.details = p_driver.make_failure_details();
	return result;
}

} // namespace

EditorAutomationAcceptanceWorkflow::Result EditorAutomationAcceptanceWorkflow::run_basic_scene_editing(EditorWorkflowTestDriver &p_driver, const String &p_scene_path) {
	Result result;
	result.workflow = "basic_scene_editing";

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
	p_driver.flush_frames(30);

	// 1. Open the Create/Add Node dialog from the focused tile's scene tree dock.
	p_driver.set_step("focus_scene_tree_dock");
	if (!p_driver.require_ok(p_driver.run_command("docks/open_scene"), "focus_scene_tree_dock")) {
		return _failure_from_driver(p_driver, result.workflow);
	}
	p_driver.flush_frames(10);

	p_driver.set_step("open_add_child_node_dialog");
	SceneTreeDock *scene_dock = EditorNode::get_singleton()->get_focused_scene_tree_dock();
	if (scene_dock == nullptr) {
		result.ok = false;
		result.message = "Focused scene tree dock is unavailable.";
		return result;
	}
	scene_dock->open_add_child_dialog();
	p_driver.flush_frames(10);

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
	p_driver.flush_frames(10);

	p_driver.set_step("select_node2d_match");
	Dictionary node2d_base;
	node2d_base["role"] = "tree_item";
	node2d_base["name"] = "Node2D";
	Dictionary node2d_item = _selector_within_modal(node2d_base);
	node2d_item["nth"] = 0;
	if (!p_driver.require_ok(p_driver.act(node2d_item, "select"), "select_node2d_match")) {
		return _failure_from_driver(p_driver, result.workflow);
	}
	p_driver.flush_frames(3);

	p_driver.set_step("confirm_create_node");
	Dictionary create_button_base;
	create_button_base["role"] = "button";
	create_button_base["name"] = "Create";
	Dictionary create_button = _selector_within_modal(create_button_base);
	if (!p_driver.require_ok(p_driver.act(create_button, "click"), "confirm_create_node")) {
		return _failure_from_driver(p_driver, result.workflow);
	}
	p_driver.flush_frames(20);

	// 3. Confirm Node2D is selected in the scene tree.
	p_driver.set_step("verify_node2d_selected");
	if (!p_driver.wait_editor_idle(5000)) {
		return _failure_from_driver(p_driver, result.workflow);
	}
	if (!p_driver.assert_selected_node_class("Node2D", "Main")) {
		Result fail;
		fail.ok = false;
		fail.workflow = result.workflow;
		fail.message = "Expected the new Node2D child to be selected in the scene tree.";
		fail.details = p_driver.make_failure_details("verify_node2d_selected");
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
	p_driver.flush_frames(30);

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
		p_driver.flush_frames(5);
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
	p_driver.flush_frames(10);

	p_driver.set_step("verify_undo_unsaved_state");
	if (!p_driver.assert_scene_unsaved()) {
		Result fail;
		fail.ok = false;
		fail.workflow = result.workflow;
		fail.message = "Inspector edit did not mark the scene unsaved (undo/redo history).";
		fail.details = p_driver.make_failure_details("verify_undo_unsaved_state");
		return fail;
	}

	// 5. Save the scene through the command palette.
	p_driver.set_step("save_scene");
	if (!p_driver.require_ok(p_driver.run_command("editor/save_scene"), "save_scene")) {
		return _failure_from_driver(p_driver, result.workflow);
	}
	p_driver.flush_frames(30);

	p_driver.set_step("wait_after_save");
	if (!p_driver.wait_editor_idle(15000)) {
		return _failure_from_driver(p_driver, result.workflow);
	}

	// 6. Run and stop the current scene through command palette paths.
	p_driver.set_step("run_current_scene");
	if (!p_driver.require_ok(p_driver.run_command("editor/run_current_scene"), "run_current_scene")) {
		return _failure_from_driver(p_driver, result.workflow);
	}

	p_driver.set_step("wait_for_playing");
	if (!p_driver.assert_playing(true, 20000)) {
		Result fail;
		fail.ok = false;
		fail.workflow = result.workflow;
		fail.message = "Scene did not enter playing state after run_current_scene.";
		fail.details = p_driver.make_failure_details("wait_for_playing");
		return fail;
	}

	p_driver.set_step("stop_running_scene");
	if (!p_driver.require_ok(p_driver.run_command("editor/stop_running_project"), "stop_running_scene")) {
		return _failure_from_driver(p_driver, result.workflow);
	}

	p_driver.set_step("wait_for_stopped");
	if (!p_driver.assert_playing(false, 20000)) {
		Result fail;
		fail.ok = false;
		fail.workflow = result.workflow;
		fail.message = "Scene did not stop after stop_running_project.";
		fail.details = p_driver.make_failure_details("wait_for_stopped");
		return fail;
	}

	// 7. Assert no new editor errors during the workflow.
	p_driver.set_step("assert_no_new_errors");
	if (!p_driver.assert_no_new_errors()) {
		return _failure_from_driver(p_driver, result.workflow);
	}

	result.ok = true;
	result.message = "Basic scene-editing automation workflow completed.";
	return result;
}

EditorAutomationAcceptanceWorkflow::Result EditorAutomationAcceptanceWorkflow::run_mixed_workspace_editing(EditorWorkflowTestDriver &p_driver) {
	Result result;
	result.workflow = "mixed_workspace_editing";

	p_driver.begin_workflow();

	MixedWorkspaceContext context;
	Result failure;
	if (!_load_mixed_workspace_scene(p_driver, result.workflow, context, failure)) {
		return failure;
	}
	if (!_add_script_tab_to_scene_pane(p_driver, result.workflow, context, failure)) {
		return failure;
	}

	WorkspaceLeafNode *script_leaf = _split_script_tab_to_right(p_driver, result.workflow, context, failure);
	if (script_leaf == nullptr) {
		return failure;
	}
	if (!_assert_command_routing(p_driver, result.workflow, context, script_leaf, failure)) {
		return failure;
	}
	if (!_move_script_tab_back_to_scene_pane(p_driver, result.workflow, context, script_leaf, failure)) {
		return failure;
	}
	if (!_close_dirty_script_tab(p_driver, result.workflow, context, failure)) {
		return failure;
	}

	p_driver.set_step("assert_no_new_errors");
	if (!p_driver.assert_no_new_errors()) {
		return _failure_from_driver(p_driver, result.workflow);
	}

	result.ok = true;
	result.message = "Mixed workspace editing automation workflow completed.";
	Dictionary details;
	details["workspace"] = p_driver.read_editor_state().get("workspace", Dictionary());
	result.details = details;
	return result;
}

EditorAutomationAcceptanceWorkflow::Result EditorAutomationAcceptanceWorkflow::run_mixed_workspace_seed(EditorWorkflowTestDriver &p_driver) {
	Result result;
	result.workflow = "mixed_workspace_seed";

	p_driver.begin_workflow();

	MixedWorkspaceContext context;
	Result failure;
	if (!_load_mixed_workspace_scene(p_driver, result.workflow, context, failure)) {
		return failure;
	}
	if (!_add_script_tab_to_scene_pane(p_driver, result.workflow, context, failure)) {
		return failure;
	}

	WorkspaceLeafNode *script_leaf = _split_script_tab_to_right(p_driver, result.workflow, context, failure);
	if (script_leaf == nullptr) {
		return failure;
	}
	if (!_assert_command_routing(p_driver, result.workflow, context, script_leaf, failure)) {
		return failure;
	}
	if (!_save_mixed_workspace_layout(p_driver, result.workflow, context, failure)) {
		return failure;
	}

	p_driver.set_step("assert_no_new_errors");
	if (!p_driver.assert_no_new_errors()) {
		return _failure_from_driver(p_driver, result.workflow);
	}

	result.ok = true;
	result.message = "Mixed workspace seed layout saved.";
	Dictionary details;
	details["workspace"] = p_driver.read_editor_state().get("workspace", Dictionary());
	result.details = details;
	return result;
}

EditorAutomationAcceptanceWorkflow::Result EditorAutomationAcceptanceWorkflow::run_mixed_workspace_restore(EditorWorkflowTestDriver &p_driver) {
	Result result;
	result.workflow = "mixed_workspace_restore";

	p_driver.begin_workflow();
	p_driver.set_step("verify_restored_workspace");
	p_driver.flush_frames(60);

	EditorSceneWorkspace *workspace = EditorNode::get_scene_workspace();
	if (workspace == nullptr) {
		return _failure_with_message(p_driver, result.workflow, "Scene workspace is unavailable.");
	}
	if (workspace->get_leaf_count() != 2) {
		return _failure_with_message(p_driver, result.workflow, vformat("Expected restored workspace to have 2 panes, found %d.", workspace->get_leaf_count()));
	}

	WorkspaceLeafNode *scene_leaf = nullptr;
	WorkspaceLeafNode *script_leaf = nullptr;
	WorkspacePane *scene_pane = nullptr;
	WorkspacePane *script_pane = nullptr;
	for (WorkspaceLeafNode *leaf : workspace->get_leaves()) {
		WorkspacePane *pane = leaf->get_workspace_pane();
		if (pane == nullptr) {
			continue;
		}
		if (_pane_has_tab(pane, StringName("scene"), MIXED_WORKSPACE_SCENE)) {
			scene_leaf = leaf;
			scene_pane = pane;
		}
		if (_pane_has_tab(pane, StringName("script"), MIXED_WORKSPACE_SCRIPT)) {
			script_leaf = leaf;
			script_pane = pane;
		}
	}

	if (scene_leaf == nullptr || scene_pane == nullptr || script_leaf == nullptr || script_pane == nullptr) {
		return _failure_with_message(p_driver, result.workflow, "Restored workspace did not contain the expected scene and script tabs.");
	}
	if (scene_pane->get_tab_count() != 1 || script_pane->get_tab_count() != 1) {
		return _failure_with_message(p_driver, result.workflow, "Restored workspace panes did not preserve expected tab counts.");
	}
	if (scene_pane->get_active_tab_index() != 0 || script_pane->get_active_tab_index() != 0) {
		return _failure_with_message(p_driver, result.workflow, "Restored workspace did not preserve active tabs.");
	}
	if (workspace->get_focused_leaf_id() != scene_leaf->get_leaf_id()) {
		return _failure_with_message(p_driver, result.workflow, "Restored workspace did not preserve the scene pane as focused.");
	}

	const Dictionary state = p_driver.read_editor_state();
	const Dictionary workspace_state = state.get("workspace", Dictionary());
	if (!(bool)workspace_state.get("supported", false) || int(workspace_state.get("tile_count", 0)) != 1) {
		return _failure_with_message(p_driver, result.workflow, "Restored workspace state readback did not report the expected scene tile.");
	}
	const Dictionary tree = workspace_state.get("tree", Dictionary());
	if (String(tree.get("type", String())) != "split") {
		return _failure_with_message(p_driver, result.workflow, "Restored workspace state did not preserve a split tree.");
	}

	if (!_assert_visible_workspace_tab(p_driver, "scene", MIXED_WORKSPACE_SCENE, scene_leaf->get_leaf_id(), "find_restored_scene_tab") ||
			!_assert_visible_workspace_tab(p_driver, "script", MIXED_WORKSPACE_SCRIPT, script_leaf->get_leaf_id(), "find_restored_script_tab")) {
		return _failure_from_driver(p_driver, result.workflow, "Restored tabs were not visible through semantic tab selectors.");
	}

	MixedWorkspaceContext context;
	context.editor_node = EditorNode::get_singleton();
	context.workspace = workspace;
	context.scene_leaf = scene_leaf;
	context.scene_pane = scene_pane;
	context.scene_leaf_id = scene_leaf->get_leaf_id();
	Result failure;
	if (!_assert_command_routing(p_driver, result.workflow, context, script_leaf, failure)) {
		return failure;
	}

	p_driver.set_step("assert_no_new_errors");
	if (!p_driver.assert_no_new_errors()) {
		return _failure_from_driver(p_driver, result.workflow);
	}

	result.ok = true;
	result.message = "Mixed workspace layout restored.";
	Dictionary details;
	details["workspace"] = workspace_state;
	result.details = details;
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
