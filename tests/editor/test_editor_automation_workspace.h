/**************************************************************************/
/*  test_editor_automation_workspace.h                                    */
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

#include "editor/automation/editor_automation_driver.h"
#include "editor/automation/editor_automation_selector.h"
#include "editor/automation/editor_automation_snapshot.h"
#include "editor/automation/editor_automation_workspace.h"
#include "editor/editor_data.h"
#include "editor/editor_scene_workspace.h"

#include "core/object/message_queue.h"
#include "scene/gui/panel_container.h"

#include "tests/test_macros.h"

namespace TestEditorAutomationWorkspace {

struct WorkspaceHarness {
	Control *host = nullptr;
	EditorData editor_data;
	EditorSelection *selection = nullptr;
	EditorSceneWorkspace *workspace = nullptr;

	void mount() {
		host = memnew(Control);
		host->set_custom_minimum_size(Size2(800, 600));
		SceneTree::get_singleton()->get_root()->add_child(host);
		selection = memnew(EditorSelection);
		workspace = EditorSceneWorkspace::create_single_leaf_workspace(selection, &editor_data);
		host->add_child(workspace);
		workspace->set_anchors_and_offsets_preset(Control::PRESET_FULL_RECT);
		host->set_size(Size2(800, 600));
		workspace->set_size(Size2(800, 600));
		sync_tiles();
	}

	void sync_tiles() {
		for (WorkspaceLeafNode *leaf : workspace->get_leaves()) {
			editor_data.register_tile(leaf->get_leaf_id());
		}
		editor_data.set_focused_tile_id(workspace->get_focused_leaf_id());
	}

	void pump() {
		SceneTree::get_singleton()->process(0.016);
		MessageQueue::get_singleton()->flush();
	}

	void unmount() {
		memdelete(workspace);
		SceneTree::get_singleton()->get_root()->remove_child(host);
		memdelete(host);
		memdelete(selection);
	}
};

static WorkspaceHarness *prepare_two_tile_workspace(WorkspaceHarness &r_harness) {
	r_harness.mount();
	r_harness.pump();
	WorkspaceLeafNode *first = r_harness.workspace->get_focused_leaf();
	WorkspaceLeafNode *second = r_harness.workspace->split(first, false, EditorSceneWorkspace::SPLIT_SIDE_SECOND);
	r_harness.pump();
	REQUIRE(second != nullptr);
	r_harness.sync_tiles();
	return &r_harness;
}

TEST_CASE("[Editor][Automation][MCP] mcp-workspace-state") {
	WorkspaceHarness h;
	prepare_two_tile_workspace(h);

	const int scene_a = h.editor_data.add_edited_scene(-1);
	h.editor_data.set_scene_path(scene_a, "res://tile_a.tscn");
	h.editor_data.set_scene_tile(scene_a, 0);
	const int scene_b = h.editor_data.add_edited_scene(-1);
	h.editor_data.set_scene_path(scene_b, "res://tile_b.tscn");
	h.editor_data.set_scene_tile(scene_b, 1);
	h.editor_data.set_tile_current_scene(0, scene_a);
	h.editor_data.set_tile_current_scene(1, scene_b);
	h.editor_data.set_focused_tile_id(1);
	h.workspace->set_focused_leaf(1);

	const Dictionary workspace_state = EditorAutomationWorkspace::capture_workspace_state(&h.editor_data, h.workspace);
	CHECK((bool)workspace_state.get("supported", false));
	CHECK(int(workspace_state.get("focused_tile_id", -1)) == 1);
	CHECK(int(workspace_state.get("tile_count", 0)) == 2);

	const Array tiles = workspace_state.get("tiles", Array());
	REQUIRE(tiles.size() == 2);

	Dictionary tile_a = tiles[0];
	Dictionary tile_b = tiles[1];
	CHECK(int(tile_a.get("tile_id", -1)) == 0);
	CHECK(int(tile_b.get("tile_id", -1)) == 1);
	CHECK((bool)tile_b.get("focused", false));
	CHECK(String(tile_a.get("current_scene_path", String())) == "res://tile_a.tscn");
	CHECK(String(tile_b.get("current_scene_path", String())) == "res://tile_b.tscn");

	const Dictionary tree = workspace_state.get("tree", Dictionary());
	CHECK(String(tree.get("type", String())) == "split");

	h.unmount();
}

TEST_CASE("[Editor][Automation][MCP] mcp-tile-scoped-selector") {
	WorkspaceHarness h;
	prepare_two_tile_workspace(h);
	h.pump();

	const EditorAutomationSnapshot snapshot = EditorAutomationSnapshot::capture_from_node(h.host);

	Dictionary unscoped;
	unscoped["role"] = "dock";
	unscoped["name"] = "Scene";
	const EditorAutomationSelectorResult ambiguous = EditorAutomationSelector::resolve(snapshot, unscoped);
	CHECK(ambiguous.status == EditorAutomationSelectorStatus::AMBIGUOUS);

	Dictionary within;
	within["tile_id"] = 1;
	Dictionary scoped;
	scoped["role"] = "dock";
	scoped["name"] = "Scene";
	scoped["within"] = within;
	const EditorAutomationSelectorResult resolved = EditorAutomationSelector::resolve(snapshot, scoped);
	REQUIRE(resolved.status == EditorAutomationSelectorStatus::OK);
	REQUIRE(resolved.match_indices.size() == 1);

	const EditorAutomationElement &dock = snapshot.get_element(resolved.match_indices[0]);
	CHECK(int(dock.metadata.get("tile_id", -1)) == 1);

	h.unmount();
}

TEST_CASE("[Editor][Automation][MCP] mcp-dock-action") {
	WorkspaceHarness h;
	h.mount();
	h.pump();

	const int scene_a = h.editor_data.add_edited_scene(-1);
	h.editor_data.set_scene_path(scene_a, "res://dock_a.tscn");
	h.editor_data.set_scene_tile(scene_a, 0);
	const int scene_b = h.editor_data.add_edited_scene(-1);
	h.editor_data.set_scene_path(scene_b, "res://dock_b.tscn");
	h.editor_data.set_scene_tile(scene_b, 0);
	h.editor_data.set_tile_current_scene(0, scene_a);
	h.sync_tiles();

	ScenePaneTile *tile = h.workspace->get_focused_tile();
	REQUIRE(tile != nullptr);
	tile->get_scene_tabs()->update_scene_tabs();
	h.pump();

	CHECK(h.workspace->get_tile_count() == 1);
	CHECK(EditorAutomationWorkspace::dock_scene_tab(
			&h.editor_data,
			h.workspace,
			0,
			1,
			0,
			EditorSceneWorkspace::DROP_RIGHT,
			nullptr));
	h.pump();

	CHECK(h.workspace->get_tile_count() == 2);
	CHECK(h.editor_data.get_scene_tile(scene_b) == h.editor_data.get_focused_tile_id());

	const EditorAutomationSnapshot snapshot = EditorAutomationSnapshot::capture_from_node(h.host);
	Dictionary tab_selector;
	tab_selector["role"] = "tab";
	tab_selector["text"] = "dock_b.tscn";
	Dictionary dock_args;
	dock_args["target_tile_id"] = 0;
	dock_args["region"] = "center";
	const EditorAutomationSelectorResult tab_match = EditorAutomationSelector::resolve(snapshot, tab_selector);
	REQUIRE(tab_match.status == EditorAutomationSelectorStatus::OK);
	const EditorAutomationElement &tab = snapshot.get_element(tab_match.match_indices[0]);
	const EditorAutomationActionResult drag_result = EditorAutomationDriver::perform(
			snapshot,
			"dock",
			tab_selector,
			dock_args);
	CHECK(drag_result.ok);
	CHECK(drag_result.route == EditorAutomationActionRouteNames::SEMANTIC_DOCK);

	h.unmount();
}

} // namespace TestEditorAutomationWorkspace
