/**************************************************************************/
/*  test_editor_automation_workflow.h                                     */
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

#include "editor/automation/editor_automation_mcp_dispatcher.h"
#include "editor/automation/editor_automation_snapshot.h"
#include "editor/automation/editor_automation_state.h"
#include "editor/automation/editor_automation_workflow.h"
#include "editor/docks/editor_dock.h"
#include "editor/scene/scene_tree_editor.h"

#include "scene/gui/button.h"
#include "scene/gui/tree.h"
#include "scene/main/window.h"

#include "tests/test_macros.h"

namespace TestEditorAutomationWorkflow {

static Dictionary make_request(const Variant &p_id, const String &p_method, const Dictionary &p_params = Dictionary()) {
	Dictionary request;
	request["jsonrpc"] = "2.0";
	request["id"] = p_id;
	request["method"] = p_method;
	if (!p_params.is_empty()) {
		request["params"] = p_params;
	}
	return request;
}

static const EditorAutomationElement *find_element_by_role_and_name(const EditorAutomationSnapshot &p_snapshot, const String &p_role, const String &p_name) {
	for (int i = 0; i < p_snapshot.get_element_count(); i++) {
		const EditorAutomationElement &element = p_snapshot.get_element(i);
		if (element.role == p_role && element.name == p_name) {
			return &element;
		}
	}
	return nullptr;
}

TEST_CASE("[Editor][Automation] named dock roots appear in semantic snapshots") {
	Window *window = memnew(Window);
	SceneTree::get_singleton()->get_root()->add_child(window);

	EditorDock *scene_dock = memnew(EditorDock);
	scene_dock->set_name("Scene");
	EditorDock *inspector_dock = memnew(EditorDock);
	inspector_dock->set_name("Inspector");
	EditorDock *filesystem_dock = memnew(EditorDock);
	filesystem_dock->set_name("FileSystem");

	Button *run_button = memnew(Button);
	run_button->set_accessibility_name("Run Project");

	window->add_child(scene_dock);
	window->add_child(inspector_dock);
	window->add_child(filesystem_dock);
	window->add_child(run_button);

	const EditorAutomationSnapshot snapshot = EditorAutomationSnapshot::capture_from_node(window);

	const EditorAutomationElement *scene = find_element_by_role_and_name(snapshot, "dock", "Scene");
	REQUIRE(scene != nullptr);
	CHECK(String(scene->metadata.get("dock_title", String())) == "Scene");

	const EditorAutomationElement *inspector = find_element_by_role_and_name(snapshot, "dock", "Inspector");
	REQUIRE(inspector != nullptr);

	const EditorAutomationElement *filesystem = find_element_by_role_and_name(snapshot, "dock", "FileSystem");
	REQUIRE(filesystem != nullptr);

	const EditorAutomationElement *run_project = find_element_by_role_and_name(snapshot, "button", "Run Project");
	REQUIRE(run_project != nullptr);

	window->queue_free();
}

TEST_CASE("[Editor][Automation] scene tree item metadata includes node path and class") {
	Window *window = memnew(Window);
	SceneTree::get_singleton()->get_root()->add_child(window);

	SceneTreeEditor *scene_tree_editor = memnew(SceneTreeEditor(false, false, false));
	window->add_child(scene_tree_editor);

	Node *child = memnew(Node);
	child->set_name("ChildNode");
	scene_tree_editor->add_child(child);

	Tree *tree = scene_tree_editor->get_scene_tree();
	TreeItem *root_item = tree->create_item();
	TreeItem *child_item = tree->create_item(root_item);
	child_item->set_text(0, "ChildNode");
	child_item->set_metadata(0, NodePath("ChildNode"));

	const Dictionary metadata = EditorAutomationWorkflow::metadata_for_tree_item(tree, child_item);
	CHECK(String(metadata.get("node_name", String())) == "ChildNode");
	CHECK(String(metadata.get("node_class", String())) == "Node");
	CHECK(NodePath(metadata.get("node_path", NodePath())) == NodePath("ChildNode"));

	window->queue_free();
}

TEST_CASE("[Editor][Automation] editor state readback includes filesystem keys") {
	const Dictionary state = EditorAutomationState::read_editor_state();
	CHECK(state.has("filesystem"));
	const Dictionary filesystem = state.get("filesystem", Dictionary());
	CHECK(filesystem.has("supported"));
}

TEST_CASE("[Editor][Automation][MCP] run_command rejects unknown command") {
	EditorAutomationMCPDispatcher dispatcher;
	Dictionary params;
	params["name"] = "run_command";
	Dictionary arguments;
	arguments["command"] = "editor/nonexistent_test_command";
	params["arguments"] = arguments;

	const Dictionary response = dispatcher.handle_message(make_request(99, "tools/call", params));
	CHECK(response.has("result"));

	const Dictionary result = response["result"];
	CHECK((bool)result["isError"]);
	const Dictionary structured = result["structuredContent"];
	CHECK((bool)structured.get("ok", true) == false);
	CHECK(String(structured.get("kind", String())) == "unknown_command");
}

} // namespace TestEditorAutomationWorkflow
