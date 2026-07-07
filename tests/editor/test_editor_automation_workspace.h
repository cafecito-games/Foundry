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

#include "editor/automation/editor_automation_selector.h"
#include "editor/automation/editor_automation_snapshot.h"
#include "editor/automation/editor_automation_workspace.h"
#include "editor/editor_data.h"
#include "editor/editor_scene_pane_tile.h"
#include "editor/editor_scene_workspace.h"
#include "editor/editor_undo_redo_manager.h"
#include "editor/workspace/workspace_pane.h"

#include "core/object/message_queue.h"
#include "scene/2d/node_2d.h"
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

	void sync_panes() {
		for (WorkspaceLeafNode *leaf : workspace->get_leaves()) {
			if (WorkspacePane *pane = leaf->get_workspace_pane()) {
				pane->sync_from_editor_data();
			}
		}
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
	Array tree_children = tree.get("children", Array());
	REQUIRE(tree_children.size() == 2);
	for (int i = 0; i < tree_children.size(); i++) {
		const Dictionary leaf = tree_children[i];
		CHECK(String(leaf.get("type", String())) == "leaf");
		CHECK(String(leaf.get("content_type", String())) == "pane");
	}

	h.unmount();
}

TEST_CASE("[Editor][Automation][MCP] mcp-tile-scoped-selector") {
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
	h.sync_panes();
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
	Node2D *root_a = memnew(Node2D);
	h.editor_data.get_scene_context(scene_a)->set_scene_root_node(root_a);
	const int scene_b = h.editor_data.add_edited_scene(-1);
	h.editor_data.set_scene_path(scene_b, "res://dock_b.tscn");
	h.editor_data.set_scene_tile(scene_b, 0);
	Node2D *root_b = memnew(Node2D);
	Node2D *selected_b = memnew(Node2D);
	root_b->add_child(selected_b);
	h.editor_data.get_scene_context(scene_b)->set_scene_root_node(root_b);
	Vector<ObjectID> selected_b_ids;
	selected_b_ids.push_back(selected_b->get_instance_id());
	h.editor_data.get_scene_context(scene_b)->set_selected_node_ids(selected_b_ids);
	EditorUndoRedoManager::get_singleton()->set_history_as_unsaved(h.editor_data.get_scene_history_id(scene_b));
	h.editor_data.set_tile_current_scene(0, scene_a);
	h.sync_tiles();

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
	CHECK(h.editor_data.get_edited_scene_root(scene_b) == root_b);
	CHECK(h.editor_data.get_scene_context(scene_b)->get_selected_node_ids().has(selected_b->get_instance_id()));
	CHECK(EditorUndoRedoManager::get_singleton()->is_history_unsaved(h.editor_data.get_scene_history_id(scene_b)));

	ScenePaneTile *target_tile = h.workspace->get_tile_by_id(0);
	REQUIRE(target_tile != nullptr);
	const Vector2 drop_point = EditorAutomationWorkspace::global_drop_point(
			target_tile, EditorSceneWorkspace::DROP_RIGHT);
	CHECK(drop_point.x > target_tile->get_global_rect().position.x);

	h.unmount();
}

TEST_CASE("[Editor][Automation][MCP] mcp-pane-strip-tab-has-tile-id") {
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

	// Populate each pane's generic tab strip from editor data so the visible
	// TabBar (owned by WorkspacePane, not EditorSceneTabs) carries scene tabs.
	for (WorkspaceLeafNode *leaf : h.workspace->get_leaves()) {
		if (WorkspacePane *pane = leaf->get_workspace_pane()) {
			pane->sync_scene_tabs_from_editor_data(leaf->get_leaf_id() == 1);
		}
	}
	h.pump();

	WorkspaceLeafNode *leaf_b = h.workspace->get_leaf_by_id(1);
	REQUIRE(leaf_b != nullptr);
	WorkspacePane *pane_b = leaf_b->get_workspace_pane();
	REQUIRE(pane_b != nullptr);
	TabBar *strip_b = pane_b->get_tab_strip();
	REQUIRE(strip_b != nullptr);
	REQUIRE(strip_b->get_tab_count() == 1);

	const EditorAutomationSnapshot snapshot = EditorAutomationSnapshot::capture_from_node(h.host);

	// The pane strip's tab element must expose tile_id metadata so the semantic
	// dock/drag routes can resolve the source through the primary metadata path.
	const String tab_key = vformat("%s:%d", String::num_uint64(strip_b->get_instance_id()), 0);
	const EditorAutomationElement *tab_element = snapshot.find_by_durable_key("tab", tab_key);
	REQUIRE(tab_element != nullptr);
	CHECK(int(tab_element->metadata.get("tile_id", -1)) == 1);
	CHECK(int(tab_element->metadata.get("tab_index", -1)) == 0);

	int resolved_tile_id = -1;
	int resolved_tab_index = -1;
	CHECK(EditorAutomationWorkspace::resolve_scene_tab_source(*tab_element, resolved_tile_id, resolved_tab_index));
	CHECK(resolved_tile_id == 1);
	CHECK(resolved_tab_index == 0);

	h.unmount();
}

} // namespace TestEditorAutomationWorkspace
