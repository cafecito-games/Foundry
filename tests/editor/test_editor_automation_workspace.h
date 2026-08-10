/**************************************************************************/
/*  test_editor_automation_workspace.h                                    */
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

#pragma once

#include "editor/automation/editor_automation_driver.h"
#include "editor/automation/editor_automation_mcp_contracts.h"
#include "editor/automation/editor_automation_selector.h"
#include "editor/automation/editor_automation_snapshot.h"
#include "editor/automation/editor_automation_workspace.h"
#include "editor/editor_board.h"
#include "editor/editor_board_strip.h"
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

TEST_CASE("[Editor][Automation][MCP] mcp-workspace-state-reports-detached-viewport-parent-without-errors") {
	WorkspaceHarness h;
	h.mount();
	h.pump();

	const int scene_a = h.editor_data.add_edited_scene(-1);
	h.editor_data.set_scene_tile(scene_a, 0);
	h.editor_data.set_tile_current_scene(0, scene_a);
	Node2D *root_a = memnew(Node2D);
	h.editor_data.get_scene_context(scene_a)->set_scene_root_node(root_a);

	Node *detached_parent = memnew(Node);
	detached_parent->add_child(h.editor_data.get_scene_context(scene_a)->get_viewport());

	ErrorDetector error_detector;
	const Dictionary workspace_state = EditorAutomationWorkspace::capture_workspace_state(&h.editor_data, h.workspace);
	CHECK_FALSE(error_detector.has_error);

	const Array tiles = workspace_state.get("tiles", Array());
	REQUIRE(tiles.size() == 1);
	const Dictionary tile = tiles[0];
	CHECK(String(tile.get("current_scene_viewport_parent", String())) == "<detached>");

	detached_parent->remove_child(h.editor_data.get_scene_context(scene_a)->get_viewport());
	memdelete(detached_parent);
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

TEST_CASE("[Editor][Automation][MCP] snapshot-pane-role-stable") {
	WorkspaceHarness h;
	h.mount();
	h.pump();

	WorkspaceLeafNode *leaf = h.workspace->get_focused_leaf();
	REQUIRE(leaf != nullptr);
	WorkspacePane *pane = leaf->get_workspace_pane();
	REQUIRE(pane != nullptr);

	const uint64_t pane_object_id = pane->get_instance_id();
	const String pane_key = String::num_uint64(pane_object_id);
	const String expected_handle = EditorAutomationSnapshot::make_durable_handle("object", pane_key);

	const EditorAutomationSnapshot snapshot = EditorAutomationSnapshot::capture_from_node(h.host);

	const EditorAutomationElement *pane_element = nullptr;
	for (int i = 0; i < snapshot.get_element_count(); i++) {
		if (snapshot.get_element(i).object_id == pane_object_id) {
			pane_element = &snapshot.get_element(i);
			break;
		}
	}
	REQUIRE(pane_element != nullptr);

	// A generic workspace pane reports a stable "pane" role and a durable handle so
	// automation can address it without depending on transient geometry.
	CHECK(pane_element->role == "pane");
	CHECK_FALSE(pane_element->handle.is_empty());
	CHECK(pane_element->handle == expected_handle);

	// The durable handle is stable across captures (same underlying node).
	const EditorAutomationSnapshot snapshot_again = EditorAutomationSnapshot::capture_from_node(h.host);
	const EditorAutomationElement *pane_element_again = snapshot_again.find_by_durable_key("object", pane_key);
	REQUIRE(pane_element_again != nullptr);
	CHECK(pane_element_again->role == "pane");

	h.unmount();
}

TEST_CASE("[Editor][Automation][MCP] snapshot-mixed-tabs-distinct-selectors") {
	WorkspacePane::get_shared_tab_registry().clear_canonical_index();

	WorkspaceHarness h;
	h.mount();
	h.pump();

	WorkspaceLeafNode *leaf = h.workspace->get_focused_leaf();
	REQUIRE(leaf != nullptr);
	WorkspacePane *pane = leaf->get_workspace_pane();
	REQUIRE(pane != nullptr);

	WorkspaceTabRegistry &registry = WorkspacePane::get_shared_tab_registry();
	WorkspaceTabType *scene_type = registry.find_type(StringName("scene"));
	WorkspaceTabType *script_type = registry.find_type(StringName("script"));
	REQUIRE(scene_type != nullptr);
	REQUIRE(script_type != nullptr);

	// A scene tab and a script tab share one pane's tab strip.
	const String scene_key = "res://alpha.tscn";
	const String script_key = "res://alpha.fs";
	pane->add_tab(scene_type->make_tab(scene_key, registry.allocate_stable_id()));
	pane->add_tab(script_type->make_tab(script_key, registry.allocate_stable_id()));
	h.pump();

	TabBar *strip = pane->get_tab_strip();
	REQUIRE(strip != nullptr);
	REQUIRE(strip->get_tab_count() == 2);

	const EditorAutomationSnapshot snapshot = EditorAutomationSnapshot::capture_from_node(h.host);

	// Both tab elements carry type_id/resource_key metadata so a scene tab and a
	// script tab are distinguishable even when they live in the same strip.
	const EditorAutomationElement *scene_tab_element = snapshot.find_by_durable_key("tab", vformat("%s:%d", String::num_uint64(strip->get_instance_id()), 0));
	const EditorAutomationElement *script_tab_element = snapshot.find_by_durable_key("tab", vformat("%s:%d", String::num_uint64(strip->get_instance_id()), 1));
	REQUIRE(scene_tab_element != nullptr);
	REQUIRE(script_tab_element != nullptr);
	CHECK(String(scene_tab_element->metadata.get("type_id", String())) == "scene");
	CHECK(String(scene_tab_element->metadata.get("resource_key", String())) == scene_key);
	CHECK(String(script_tab_element->metadata.get("type_id", String())) == "script");
	CHECK(String(script_tab_element->metadata.get("resource_key", String())) == script_key);

	// Selecting a tab by role alone is ambiguous: two tabs match.
	Dictionary unscoped;
	unscoped["role"] = "tab";
	const EditorAutomationSelectorResult ambiguous = EditorAutomationSelector::resolve(snapshot, unscoped);
	CHECK(ambiguous.status == EditorAutomationSelectorStatus::AMBIGUOUS);

	// The type_id metadata disambiguates: each type resolves to exactly one tab,
	// scoped within the owning pane by its stable "pane" role.
	Dictionary within_pane;
	within_pane["role"] = "pane";

	Dictionary scene_type_metadata;
	scene_type_metadata["type_id"] = "scene";
	Dictionary scene_selector;
	scene_selector["role"] = "tab";
	scene_selector["metadata"] = scene_type_metadata;
	scene_selector["within"] = within_pane;
	const EditorAutomationSelectorResult scene_resolved = EditorAutomationSelector::resolve(snapshot, scene_selector);
	REQUIRE(scene_resolved.status == EditorAutomationSelectorStatus::OK);
	REQUIRE(scene_resolved.match_indices.size() == 1);
	CHECK(String(snapshot.get_element(scene_resolved.match_indices[0]).metadata.get("resource_key", String())) == scene_key);

	Dictionary script_type_metadata;
	script_type_metadata["type_id"] = "script";
	Dictionary script_selector;
	script_selector["role"] = "tab";
	script_selector["metadata"] = script_type_metadata;
	script_selector["within"] = within_pane;
	const EditorAutomationSelectorResult script_resolved = EditorAutomationSelector::resolve(snapshot, script_selector);
	REQUIRE(script_resolved.status == EditorAutomationSelectorStatus::OK);
	REQUIRE(script_resolved.match_indices.size() == 1);
	CHECK(String(snapshot.get_element(script_resolved.match_indices[0]).metadata.get("resource_key", String())) == script_key);

	h.unmount();
}

TEST_CASE("[Editor][Automation][MCP] snapshot-nested-tabbar-skips-workspace-metadata") {
	WorkspacePane::get_shared_tab_registry().clear_canonical_index();

	WorkspaceHarness h;
	h.mount();
	h.pump();

	WorkspaceLeafNode *leaf = h.workspace->get_focused_leaf();
	REQUIRE(leaf != nullptr);
	WorkspacePane *pane = leaf->get_workspace_pane();
	REQUIRE(pane != nullptr);

	WorkspaceTabRegistry &registry = WorkspacePane::get_shared_tab_registry();
	WorkspaceTabType *scene_type = registry.find_type(StringName("scene"));
	REQUIRE(scene_type != nullptr);
	pane->add_tab(scene_type->make_tab("res://alpha.tscn", registry.allocate_stable_id()));
	h.pump();

	// A TabBar that lives under the pane but is NOT the pane's own tab strip (e.g.
	// a tab bar inside the active scene/script surface) must not inherit the
	// workspace tab's type_id/resource_key by index.
	TabBar *nested = memnew(TabBar);
	nested->add_tab("Nested A");
	nested->add_tab("Nested B");
	pane->add_child(nested);
	h.pump();

	const EditorAutomationSnapshot snapshot = EditorAutomationSnapshot::capture_from_node(h.host);

	TabBar *strip = pane->get_tab_strip();
	REQUIRE(strip != nullptr);
	const EditorAutomationElement *strip_tab = snapshot.find_by_durable_key("tab", vformat("%s:%d", String::num_uint64(strip->get_instance_id()), 0));
	REQUIRE(strip_tab != nullptr);
	// The pane's own strip still carries workspace tab metadata.
	CHECK(strip_tab->metadata.has("type_id"));
	CHECK(strip_tab->metadata.has("resource_key"));

	const EditorAutomationElement *nested_tab = snapshot.find_by_durable_key("tab", vformat("%s:%d", String::num_uint64(nested->get_instance_id()), 0));
	REQUIRE(nested_tab != nullptr);
	// The unrelated nested tab keeps its owning leaf id but never the workspace
	// tab's type_id/resource_key, which would mislabel it and confuse selectors.
	CHECK(nested_tab->metadata.has("tile_id"));
	CHECK_FALSE(nested_tab->metadata.has("type_id"));
	CHECK_FALSE(nested_tab->metadata.has("resource_key"));

	h.unmount();
}

struct BoardAutomationHarness {
	Control *host = nullptr;
	EditorData editor_data;
	EditorSelection *selection = nullptr;
	EditorBoardStrip *strip = nullptr;

	// Boards are laid out at the full strip rect, so the strip needs a real size before any
	// board geometry means anything to the transition-settled predicate.
	void mount(int p_board_count = 3) {
		host = memnew(Control);
		host->set_custom_minimum_size(Size2(800, 600));
		SceneTree::get_singleton()->get_root()->add_child(host);
		selection = memnew(EditorSelection);
		strip = EditorBoardStrip::create(selection, &editor_data);
		host->add_child(strip);
		strip->set_anchors_and_offsets_preset(Control::PRESET_FULL_RECT);
		host->set_size(Size2(800, 600));
		strip->set_size(Size2(800, 600));
		for (int i = strip->get_board_count(); i < p_board_count; i++) {
			strip->add_board(vformat("Board %d", i + 1));
		}
		pump();
	}

	void pump(double p_delta = 0.016) {
		SceneTree::get_singleton()->process(p_delta);
		MessageQueue::get_singleton()->flush();
	}

	// Advances past the transition duration so the ease-out lands and dormancy settles.
	void settle() {
		pump(1.0);
		pump();
	}

	void unmount() {
		SceneTree::get_singleton()->get_root()->remove_child(host);
		memdelete(host);
		memdelete(selection);
	}
};

static Dictionary board_entry_at(const Dictionary &p_boards_state, int p_index) {
	const Array boards = p_boards_state.get("boards", Array());
	if (p_index < 0 || p_index >= boards.size()) {
		return Dictionary();
	}
	return boards[p_index];
}

TEST_CASE("[Editor][Automation][MCP] mcp-boards-state-reports-every-board") {
	BoardAutomationHarness h;
	h.mount(3);
	h.settle();

	const Dictionary initial = EditorAutomationWorkspace::capture_boards_state(&h.editor_data, h.strip);
	CHECK((bool)initial.get("supported", false));
	CHECK(int(initial.get("board_count", 0)) == 3);
	CHECK(int(initial.get("active_board", -1)) == 0);
	CHECK_FALSE((bool)initial.get("overview_active", true));

	const Array boards = initial.get("boards", Array());
	if (boards.size() != 3) {
		h.unmount();
		FAIL_CHECK("a three-board strip must report three board entries");
		return;
	}

	for (int i = 0; i < 3; i++) {
		const Dictionary entry = board_entry_at(initial, i);
		CHECK(int(entry.get("index", -1)) == i);
		CHECK(int(entry.get("id", -1)) == h.strip->get_board(i)->get_board_id());
		CHECK(String(entry.get("title", String())) == h.strip->get_board(i)->get_title());
		// Only the active board is awake once a switch has settled.
		CHECK((bool)entry.get("dormant", true) == (i != 0));
		CHECK((bool)entry.get("active", false) == (i == 0));
		const Dictionary board_workspace = entry.get("workspace", Dictionary());
		CHECK((bool)board_workspace.get("supported", false));
	}

	h.strip->set_active_board(2);
	h.settle();

	const Dictionary switched = EditorAutomationWorkspace::capture_boards_state(&h.editor_data, h.strip);
	CHECK(int(switched.get("active_board", -1)) == 2);
	for (int i = 0; i < 3; i++) {
		const Dictionary entry = board_entry_at(switched, i);
		CHECK((bool)entry.get("dormant", true) == (i != 2));
		CHECK((bool)entry.get("active", false) == (i == 2));
	}

	h.unmount();
}

TEST_CASE("[Editor][Automation][MCP] mcp-boards-state-active-workspace-matches-single-board-shape") {
	BoardAutomationHarness h;
	h.mount(1);
	h.settle();

	// The board-aware capture must not change what a single-board editor reports for its
	// one workspace: read_editor_state's `workspace` key keeps describing the active board.
	const Dictionary boards_state = EditorAutomationWorkspace::capture_boards_state(&h.editor_data, h.strip);
	const Dictionary board_workspace = board_entry_at(boards_state, 0).get("workspace", Dictionary());
	const Dictionary active_workspace = EditorAutomationWorkspace::capture_workspace_state(
			&h.editor_data, h.strip->get_active_workspace());

	const Array expected_keys = active_workspace.keys();
	CHECK(expected_keys.size() == board_workspace.keys().size());
	for (int i = 0; i < expected_keys.size(); i++) {
		const String key = expected_keys[i];
		CHECK(board_workspace.has(key));
	}
	CHECK((bool)board_workspace.get("supported", false) == (bool)active_workspace.get("supported", false));
	CHECK(int(board_workspace.get("tile_count", -1)) == int(active_workspace.get("tile_count", -2)));
	CHECK(int(board_workspace.get("focused_leaf_id", -1)) == int(active_workspace.get("focused_leaf_id", -2)));
	CHECK(int(board_workspace.get("focused_tile_id", -1)) == int(active_workspace.get("focused_tile_id", -2)));

	h.unmount();
}

TEST_CASE("[Editor][Automation][MCP] mcp-boards-state-unsupported-without-a-strip") {
	EditorData editor_data;
	const Dictionary state = EditorAutomationWorkspace::capture_boards_state(&editor_data, nullptr);
	CHECK_FALSE((bool)state.get("supported", true));
	CHECK(int(state.get("board_count", -1)) == 0);
	CHECK(int(state.get("active_board", 0)) == -1);
	CHECK_FALSE((bool)state.get("overview_active", true));
	const Array boards = state.get("boards", Array());
	CHECK(boards.is_empty());
}

TEST_CASE("[Editor][Automation][MCP] mcp-activate-board-resolves-index-and-title") {
	BoardAutomationHarness h;
	h.mount(3);
	h.settle();

	Dictionary by_index;
	by_index["board_index"] = 2;
	const EditorAutomationBoardResolution index_resolution = EditorAutomationWorkspace::resolve_board(h.strip, by_index);
	CHECK(index_resolution.ok());
	CHECK(index_resolution.index == 2);

	Dictionary by_title;
	by_title["board_title"] = h.strip->get_board(1)->get_title();
	const EditorAutomationBoardResolution title_resolution = EditorAutomationWorkspace::resolve_board(h.strip, by_title);
	CHECK(title_resolution.ok());
	CHECK(title_resolution.index == 1);

	// An unknown title is an error, never a silent no-op: the agent gets the titles it
	// could have meant instead of a success that changed nothing.
	Dictionary unknown_title;
	unknown_title["board_title"] = "Not A Board";
	const EditorAutomationBoardResolution unknown = EditorAutomationWorkspace::resolve_board(h.strip, unknown_title);
	CHECK_FALSE(unknown.ok());
	CHECK(unknown.failure_kind == "invalid_target");
	CHECK(unknown.message.contains("Not A Board"));
	CHECK(unknown.message.contains(h.strip->get_board(0)->get_title()));

	Dictionary out_of_range;
	out_of_range["board_index"] = 7;
	const EditorAutomationBoardResolution too_large = EditorAutomationWorkspace::resolve_board(h.strip, out_of_range);
	CHECK_FALSE(too_large.ok());
	CHECK(too_large.failure_kind == "invalid_target");

	const EditorAutomationBoardResolution missing = EditorAutomationWorkspace::resolve_board(h.strip, Dictionary());
	CHECK_FALSE(missing.ok());
	CHECK(missing.failure_kind == "invalid_parameter");

	h.unmount();
}

TEST_CASE("[Editor][Automation][MCP] mcp-board-transition-settled-waits-for-the-slide") {
	BoardAutomationHarness h;
	h.mount(3);
	h.settle();

	// An idle strip is settled as soon as two consecutive samples agree.
	PackedVector2Array previous = EditorAutomationWorkspace::capture_board_geometry(h.strip);
	h.pump();
	PackedVector2Array current = EditorAutomationWorkspace::capture_board_geometry(h.strip);
	CHECK(EditorAutomationWorkspace::board_transition_settled(h.strip, previous, current));

	h.strip->set_active_board(1);
	previous = EditorAutomationWorkspace::capture_board_geometry(h.strip);
	h.pump();
	current = EditorAutomationWorkspace::capture_board_geometry(h.strip);
	CHECK_FALSE(EditorAutomationWorkspace::board_transition_settled(h.strip, previous, current));

	// The slide takes several frames, and the condition holds off for all of them rather
	// than reporting settled the moment the switch was requested.
	int frames = 1;
	while (!EditorAutomationWorkspace::board_transition_settled(h.strip, previous, current) && frames < 240) {
		previous = current;
		h.pump();
		current = EditorAutomationWorkspace::capture_board_geometry(h.strip);
		frames++;
	}
	CHECK(frames > 2);
	CHECK(frames < 240);
	CHECK_FALSE(h.strip->get_board(1)->is_dormant());
	CHECK(h.strip->get_board(0)->is_dormant());
	CHECK(h.strip->get_board(1)->get_position().is_equal_approx(Point2()));

	h.unmount();
}

static bool board_geometry_is_bit_identical(const PackedVector2Array &p_previous, const PackedVector2Array &p_current) {
	if (p_previous.size() != p_current.size()) {
		return false;
	}
	for (int i = 0; i < p_current.size(); i++) {
		if (p_previous[i] != p_current[i]) {
			return false;
		}
	}
	return true;
}

TEST_CASE("[Editor][Automation][MCP] mcp-board-transition-settled-waits-for-the-overview-zoom") {
	BoardAutomationHarness h;
	h.mount(3);
	h.settle();

	h.strip->set_overview(true);

	// Frame steps far below one display frame push the cubic ease-out deep into its tail,
	// where the remaining motion per frame drops under one float ulp of the board positions
	// and consecutive geometry samples come out bit-identical while the zoom is still
	// running. Those are exactly the frames a geometry-only predicate reported as settled.
	const double fine_delta = 0.0002;
	PackedVector2Array previous = EditorAutomationWorkspace::capture_board_geometry(h.strip);
	bool settled_while_animating = false;
	bool identical_geometry_while_animating = false;
	bool processing_while_animating = true;
	bool settled = false;
	int frames = 0;
	while (frames < 4000) {
		h.pump(fine_delta);
		frames++;
		const PackedVector2Array current = EditorAutomationWorkspace::capture_board_geometry(h.strip);
		const bool animating = h.strip->is_transition_animating();
		const bool condition = EditorAutomationWorkspace::board_transition_settled(h.strip, previous, current);
		if (animating) {
			// The acceptance criterion: never settled on any frame the zoom is still moving.
			settled_while_animating = settled_while_animating || condition;
			identical_geometry_while_animating = identical_geometry_while_animating ||
					board_geometry_is_bit_identical(previous, current);
			processing_while_animating = processing_while_animating && h.strip->is_processing();
		} else if (condition) {
			settled = true;
			break;
		}
		previous = current;
	}

	CHECK_FALSE(settled_while_animating);
#ifndef REAL_T_IS_DOUBLE
	// Single precision is where the tail collapses to bit-identical samples; a double build
	// keeps resolving the same frames, so it simply never reaches that degenerate case.
	CHECK(identical_geometry_while_animating);
#endif
	CHECK(settled);
	CHECK(frames < 4000);
	CHECK(h.strip->is_overview_active());
	// The overview keeps the strip processing to drive its throttled preview refresh, both
	// during and after the zoom, which is why the process flag cannot end this wait here.
	CHECK(processing_while_animating);
	CHECK(h.strip->is_processing());

	h.unmount();
}

TEST_CASE("[Editor][Automation][MCP] mcp-board-overview-is-observable-in-state") {
	BoardAutomationHarness h;
	h.mount(3);
	h.settle();

	h.strip->set_overview(true);
	h.settle();
	const Dictionary overview_state = EditorAutomationWorkspace::capture_boards_state(&h.editor_data, h.strip);
	CHECK((bool)overview_state.get("overview_active", false));
	for (int i = 0; i < 3; i++) {
		// Every board is live in the overview, so none of them reports dormant.
		CHECK_FALSE((bool)board_entry_at(overview_state, i).get("dormant", true));
	}

	h.strip->set_overview(false);
	h.settle();
	const Dictionary closed_state = EditorAutomationWorkspace::capture_boards_state(&h.editor_data, h.strip);
	CHECK_FALSE((bool)closed_state.get("overview_active", true));
	CHECK((bool)board_entry_at(closed_state, 1).get("dormant", false));

	h.unmount();
}

TEST_CASE("[Editor][Automation][MCP] mcp-board-actions-report-a-missing-strip") {
	// Outside a live editor there is no board strip; the board actions are still routed
	// (rather than rejected as unknown actions) and fail with a diagnosable message.
	Node *root = memnew(Node);
	SceneTree::get_singleton()->get_root()->add_child(root);
	const EditorAutomationSnapshot snapshot = EditorAutomationSnapshot::capture_from_node(root);

	Dictionary options;
	options["board_index"] = 1;
	const EditorAutomationActionResult activate = EditorAutomationDriver::perform(snapshot, "activate_board", Dictionary(), options);
	CHECK_FALSE(activate.ok);
	CHECK(activate.kind == "unsupported_action");
	CHECK(activate.message.contains("board"));

	Dictionary overview_options;
	overview_options["overview"] = true;
	const EditorAutomationActionResult overview = EditorAutomationDriver::perform(snapshot, "set_board_overview", Dictionary(), overview_options);
	CHECK_FALSE(overview.ok);
	CHECK(overview.kind == "unsupported_action");
	CHECK(overview.message.contains("board"));

	root->queue_free();
}

TEST_CASE("[Editor][Automation][MCP] mcp-board-actions-are-published-in-tool-schemas") {
	const PackedStringArray actions = EditorAutomationMCPContracts::action_names();
	CHECK(actions.has("activate_board"));
	CHECK(actions.has("set_board_overview"));
	CHECK(actions.has("set_canvas_2d_zoom"));

	const PackedStringArray conditions = EditorAutomationMCPContracts::wait_condition_types();
	CHECK(conditions.has("board_transition_settled"));

	const Array tools = EditorAutomationMCPContracts::build_tools_list();
	Dictionary act;
	for (int i = 0; i < tools.size(); i++) {
		const Dictionary tool = tools[i];
		if (String(tool.get("name", String())) == "act") {
			act = tool;
			break;
		}
	}
	if (act.is_empty()) {
		FAIL_CHECK("the act tool must be published in tools/list");
		return;
	}

	const Dictionary act_input = act["inputSchema"];
	const Dictionary act_props = act_input["properties"];
	const Dictionary action_schema = act_props["action"];
	const Array action_enum = action_schema["enum"];
	CHECK(action_enum.has(Variant(String("activate_board"))));
	CHECK(action_enum.has(Variant(String("set_board_overview"))));
	CHECK(action_enum.has(Variant(String("set_canvas_2d_zoom"))));

	// Both actions carry their arguments in the published args contract, so an agent can
	// discover board_index/board_title/overview without reading engine source.
	const Dictionary args_schema = act_props["args"];
	const Dictionary args_props = args_schema["properties"];
	CHECK(args_props.has("board_index"));
	CHECK(args_props.has("board_title"));
	CHECK(args_props.has("overview"));
	CHECK(args_props.has("zoom"));
	CHECK(String(((Dictionary)args_props["zoom"]).get("type", String())) == "number");

	const Dictionary wait_schema = act_props["wait"];
	const Dictionary wait_props = wait_schema["properties"];
	const Dictionary wait_type = wait_props["type"];
	const Array wait_enum = wait_type["enum"];
	CHECK(wait_enum.has(Variant(String("board_transition_settled"))));
}

} // namespace TestEditorAutomationWorkspace
