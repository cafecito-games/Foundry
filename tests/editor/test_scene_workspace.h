/**************************************************************************/
/*  test_scene_workspace.h                                                */
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

#include "core/io/config_file.h"
#include "editor/docks/editor_dock.h"
#include "editor/docks/groups_dock.h"
#include "editor/docks/groups_editor.h"
#include "editor/docks/history_dock.h"
#include "editor/docks/inspector_dock.h"
#include "editor/docks/scene_tree_dock.h"
#include "editor/docks/signals_dock.h"
#include "editor/editor_data.h"
#include "editor/editor_scene_context.h"
#include "editor/editor_scene_pane_tile.h"
#include "editor/editor_scene_workspace.h"
#include "editor/editor_script_leaf.h"
#include "editor/editor_tile_dock_region.h"
#include "editor/editor_undo_redo_manager.h"
#include "editor/editor_workspace_leaf_content.h"
#include "editor/scene/editor_scene_tabs.h"
#include "editor/themes/editor_scale.h"

#include "scene/2d/node_2d.h"
#include "scene/3d/camera_3d.h"
#include "scene/gui/panel_container.h"
#include "scene/gui/split_container.h"
#include "scene/gui/subviewport_container.h"
#include "scene/gui/tab_container.h"
#include "scene/main/viewport.h"
#include "scene/main/window.h"

#include "tests/test_macros.h"
#include "tests/test_tools.h"

namespace TestSceneWorkspace {

class LeafRemovedTracker : public Object {
	FOUNDRY_CLASS(LeafRemovedTracker, Object);

public:
	int removed_leaf_id = -1;
	int successor_leaf_id = -1;

	void on_leaf_removed(int p_leaf_id, int p_successor_leaf_id) {
		removed_leaf_id = p_leaf_id;
		successor_leaf_id = p_successor_leaf_id;
	}
};

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

TEST_CASE("[SceneTree][Editor] tile-self-contained") {
	WorkspaceHarness h;
	h.mount();
	h.pump();

	ScenePaneTile *tile = h.workspace->get_focused_tile();
	REQUIRE(tile != nullptr);

	REQUIRE(tile->get_scene_tabs() != nullptr);
	REQUIRE(tile->get_scene_tree_dock() != nullptr);
	REQUIRE(tile->get_inspector_dock() != nullptr);
	REQUIRE(tile->get_content_host() != nullptr);
	CHECK(tile->is_ancestor_of(tile->get_scene_tabs()));
	CHECK(tile->is_ancestor_of(tile->get_scene_tree_dock()));
	CHECK(tile->is_ancestor_of(tile->get_inspector_dock()));
	CHECK(tile->is_ancestor_of(tile->get_content_host()));
	CHECK(tile->get_scene_tabs()->get_tile_id() == tile->get_tile_id());

	EditorSceneContext *context = memnew(EditorSceneContext);
	Node2D *root = memnew(Node2D);
	root->set_name("TileScene");
	context->set_scene_root_node(root);
	Node2D *child = memnew(Node2D);
	child->set_name("TileChild");
	root->add_child(child);

	SceneTreeDock *tree_dock = tile->get_scene_tree_dock();
	InspectorDock *inspector_dock = tile->get_inspector_dock();

	tree_dock->set_scene_context(context);
	inspector_dock->set_scene_context(context);
	h.pump();

	CHECK(tree_dock->get_scene_context() == context);
	CHECK(inspector_dock->get_scene_context() == context);
	CHECK(String(tree_dock->get_scene_context()->get_scene_root_node()->get_name()) == "TileScene");

	context->activate(SceneTree::get_singleton()->get_root());
	Vector<Node *> select;
	select.push_back(child);
	tree_dock->set_selection(select);
	h.pump();
	CHECK(context->get_selection()->is_selected(child));

	EditorSceneContext *other_context = memnew(EditorSceneContext);
	Node2D *other_root = memnew(Node2D);
	other_context->set_scene_root_node(other_root);
	tree_dock->set_scene_context(other_context);
	inspector_dock->set_scene_context(other_context);
	h.pump();

	CHECK(tree_dock->get_scene_context() == other_context);
	CHECK(inspector_dock->get_scene_context() == other_context);
	CHECK(context->get_selection()->is_selected(child));

	tree_dock->set_scene_context(nullptr);
	inspector_dock->set_scene_context(nullptr);
	inspector_dock->update(nullptr);
	h.pump();

	CHECK(tree_dock->get_scene_context() == nullptr);
	CHECK(inspector_dock->get_scene_context() == nullptr);

	// Rebind to a fresh context after the previous binding was cleared.
	EditorSceneContext *replacement = memnew(EditorSceneContext);
	tree_dock->set_scene_context(replacement);
	inspector_dock->set_scene_context(replacement);
	h.pump();
	CHECK(tree_dock->get_scene_context() == replacement);

	context->deactivate();
	memdelete(context);
	memdelete(replacement);
	memdelete(other_context);

	h.unmount();
}

TEST_CASE("[SceneTree][Editor] tile-isolation-across-leaves") {
	WorkspaceHarness h;
	h.mount();
	h.pump();

	WorkspaceLeafNode *first = h.workspace->get_focused_leaf();
	WorkspaceLeafNode *second = h.workspace->split(first, false, EditorSceneWorkspace::SPLIT_SIDE_SECOND);
	h.pump();
	REQUIRE(second != nullptr);

	ScenePaneTile *tile_a = first->get_pane_tile();
	ScenePaneTile *tile_b = second->get_pane_tile();
	REQUIRE(tile_a != nullptr);
	REQUIRE(tile_b != nullptr);

	EditorSceneContext *context_a = memnew(EditorSceneContext);
	Node2D *root_a = memnew(Node2D);
	context_a->set_scene_root_node(root_a);
	Node2D *child_a = memnew(Node2D);
	root_a->add_child(child_a);

	EditorSceneContext *context_b = memnew(EditorSceneContext);
	Node2D *root_b = memnew(Node2D);
	context_b->set_scene_root_node(root_b);
	Node2D *child_b = memnew(Node2D);
	root_b->add_child(child_b);

	context_a->activate(SceneTree::get_singleton()->get_root());
	context_b->activate(SceneTree::get_singleton()->get_root());

	tile_a->get_scene_tree_dock()->set_scene_context(context_a);
	tile_a->get_inspector_dock()->set_scene_context(context_a);
	tile_b->get_scene_tree_dock()->set_scene_context(context_b);
	tile_b->get_inspector_dock()->set_scene_context(context_b);

	Vector<Node *> select_a;
	select_a.push_back(child_a);
	tile_a->get_scene_tree_dock()->set_selection(select_a);
	h.pump();

	CHECK(context_a->get_selection()->is_selected(child_a));
	CHECK_FALSE(context_b->get_selection()->is_selected(child_b));

	Vector<Node *> select_b;
	select_b.push_back(child_b);
	tile_b->get_scene_tree_dock()->set_selection(select_b);
	h.pump();

	CHECK(context_b->get_selection()->is_selected(child_b));
	// Selecting in tile B must not alter tile A's bound context or selection.
	CHECK(context_a->get_selection()->is_selected(child_a));
	CHECK_FALSE(context_b->get_selection()->is_selected(child_a));
	CHECK(tile_a->get_scene_tree_dock()->get_scene_context() == context_a);
	CHECK(tile_b->get_scene_tree_dock()->get_scene_context() == context_b);

	context_a->deactivate();
	context_b->deactivate();
	memdelete(context_a);
	memdelete(context_b);

	h.unmount();
}

TEST_CASE("[SceneTree][Editor] tree-split-collapse") {
	WorkspaceHarness h;
	h.mount();

	CHECK(h.workspace->get_leaf_count() == 1);
	WorkspaceLeafNode *first = h.workspace->get_focused_leaf();
	REQUIRE(first != nullptr);

	WorkspaceLeafNode *second = h.workspace->split(first, false, EditorSceneWorkspace::SPLIT_SIDE_SECOND);
	h.pump();
	REQUIRE(second != nullptr);
	CHECK(h.workspace->get_leaf_count() == 2);

	WorkspaceLeafNode *third = h.workspace->split(second, true, EditorSceneWorkspace::SPLIT_SIDE_FIRST);
	h.pump();
	REQUIRE(third != nullptr);
	CHECK(h.workspace->get_leaf_count() == 3);

	HashSet<int> ids;
	for (WorkspaceLeafNode *leaf : h.workspace->get_leaves()) {
		ids.insert(leaf->get_leaf_id());
	}
	CHECK(ids.size() == 3);

	const int third_id = third->get_leaf_id();

	// Collapse promotes the sibling and removes the single-child split.
	h.workspace->collapse(third);
	h.pump();
	CHECK(h.workspace->get_leaf_count() == 2);
	CHECK(h.workspace->get_leaf_by_id(third_id) == nullptr);

	h.workspace->collapse(second);
	h.pump();
	CHECK(h.workspace->get_leaf_count() == 1);
	CHECK(first->get_parent() == h.workspace);

	// Collapsing the only leaf is a no-op.
	h.workspace->collapse(first);
	h.pump();
	CHECK(h.workspace->get_leaf_count() == 1);

	h.unmount();
}

static void replace_leaf_with_script(WorkspaceLeafNode *p_leaf, const String &p_title) {
	ERR_FAIL_NULL(p_leaf);
	WorkspaceLeafContent *previous = p_leaf->take_leaf_content();
	if (previous) {
		memdelete(previous->get_root_control());
	}
	ScriptLeaf *script = memnew(ScriptLeaf);
	script->set_tab_title(p_title);
	p_leaf->set_leaf_content(script);
}

TEST_CASE("[SceneTree][Editor] tree-move") {
	WorkspaceHarness h;
	h.mount();

	WorkspaceLeafNode *first = h.workspace->get_focused_leaf();
	WorkspaceLeafNode *second = h.workspace->split(first, false, EditorSceneWorkspace::SPLIT_SIDE_SECOND);
	h.pump();
	REQUIRE(second != nullptr);

	REQUIRE(first->get_leaf_content() != nullptr);
	REQUIRE(second->get_leaf_content() != nullptr);
	CHECK(first->get_leaf_content()->get_content_type() == StringName("scene"));
	CHECK(second->get_leaf_content()->get_content_type() == StringName("scene"));

	replace_leaf_with_script(first, "pane_a");
	CHECK(first->get_leaf_content()->get_content_type() == StringName("script"));

	CHECK(h.workspace->move_content(first, second));
	CHECK(first->get_leaf_content()->get_content_type() == StringName("scene"));
	CHECK(second->get_leaf_content()->get_content_type() == StringName("script"));
	CHECK(second->get_leaf_content()->get_tab_title() == "pane_a");

	CHECK(h.workspace->move_content(first, second));
	CHECK(first->get_leaf_content()->get_content_type() == StringName("script"));
	CHECK(second->get_leaf_content()->get_content_type() == StringName("scene"));

	CHECK_FALSE(h.workspace->move_content(first, first));

	h.unmount();
}

TEST_CASE("[SceneTree][Editor] tree-persist") {
	WorkspaceHarness h;
	h.mount();

	WorkspaceLeafNode *first = h.workspace->get_focused_leaf();
	WorkspaceLeafNode *second = h.workspace->split(first, false, EditorSceneWorkspace::SPLIT_SIDE_SECOND);
	h.pump();
	WorkspaceLeafNode *third = h.workspace->split(second, true, EditorSceneWorkspace::SPLIT_SIDE_SECOND);
	h.pump();
	REQUIRE(h.workspace->get_leaf_count() == 3);

	replace_leaf_with_script(second, "aux");
	h.workspace->set_focused_leaf(second->get_leaf_id());

	WorkspaceSplitNode *root_split = Object::cast_to<WorkspaceSplitNode>(h.workspace->get_child(0, false));
	REQUIRE(root_split != nullptr);
	root_split->set_split_offset(180);

	Ref<ConfigFile> config;
	config.instantiate();
	EditorSceneWorkspace::save_to_config(config, h.workspace);

	CHECK(int(config->get_value("Workspace", "node_count")) == 5);
	CHECK(int(config->get_value("Workspace", "focused_leaf_id")) == second->get_leaf_id());
	CHECK(EditorSceneWorkspace::has_workspace_session(config));

	const Vector<int> expected_leaf_ids = { first->get_leaf_id(), second->get_leaf_id(), third->get_leaf_id() };
	const int saved_focus = second->get_leaf_id();
	const String saved_aux = "aux";

	h.unmount();

	WorkspaceHarness h2;
	h2.mount();
	h2.workspace->restore_from_config(config);
	h2.pump();

	CHECK(h2.workspace->get_leaf_count() == 3);
	for (int leaf_id : expected_leaf_ids) {
		CHECK(h2.workspace->get_leaf_by_id(leaf_id) != nullptr);
	}
	CHECK(h2.workspace->get_focused_leaf_id() == saved_focus);

	WorkspaceLeafNode *restored_script = nullptr;
	for (WorkspaceLeafNode *leaf : h2.workspace->get_leaves()) {
		if (leaf->get_leaf_content() && leaf->get_leaf_content()->get_content_type() == StringName("script")) {
			restored_script = leaf;
		}
	}
	REQUIRE(restored_script != nullptr);
	CHECK(restored_script->get_leaf_content()->get_tab_title() == saved_aux);
	CHECK(restored_script->get_leaf_id() == saved_focus);

	WorkspaceSplitNode *restored_root = Object::cast_to<WorkspaceSplitNode>(h2.workspace->get_child(0, false));
	REQUIRE(restored_root != nullptr);
	CHECK(restored_root->get_split_offset() == 180);

	WorkspaceLeafNode *fourth = h2.workspace->split(h2.workspace->get_focused_leaf(), false, EditorSceneWorkspace::SPLIT_SIDE_SECOND);
	h2.pump();
	for (int leaf_id : expected_leaf_ids) {
		CHECK(fourth->get_leaf_id() != leaf_id);
	}

	h2.unmount();
}

static int add_test_scene(EditorData &p_data, int p_tile_id, Node2D *p_root = nullptr) {
	p_data.register_tile(p_tile_id);
	p_data.set_focused_tile_id(p_tile_id);
	const int idx = p_data.add_edited_scene(-1);
	if (p_data.get_scene_tile(idx) != p_tile_id) {
		p_data.set_scene_tile(idx, p_tile_id);
	}
	p_data.set_edited_scene(idx);
	if (p_root) {
		EditorSceneContext *context = p_data.get_scene_context(idx);
		context->set_scene_root_node(p_root);
	}
	return idx;
}

static void check_focus_invariant(const EditorData &p_data, const EditorSceneWorkspace *p_workspace) {
	const int focused_tile = p_data.get_focused_tile_id();
	CHECK(p_data.get_edited_scene() == p_data.get_tile_current_scene(focused_tile));
	if (p_workspace) {
		CHECK(focused_tile == p_workspace->get_focused_leaf_id());
	}
}

TEST_CASE("[SceneTree][Editor] focus-invariant") {
	WorkspaceHarness h;
	h.mount();
	h.pump();

	const int tile_a = h.workspace->get_focused_leaf_id();
	h.editor_data.register_tile(tile_a);
	h.editor_data.set_focused_tile_id(tile_a);

	Node2D *root_a = memnew(Node2D);
	const int scene_a = add_test_scene(h.editor_data, tile_a, root_a);
	Node2D *root_b = memnew(Node2D);
	const int scene_b = add_test_scene(h.editor_data, tile_a, root_b);
	h.pump();

	h.editor_data.set_tile_current_scene(tile_a, scene_a);
	h.editor_data.set_edited_scene(scene_a);
	check_focus_invariant(h.editor_data, h.workspace);

	WorkspaceLeafNode *second = h.workspace->split(h.workspace->get_focused_leaf(), false, EditorSceneWorkspace::SPLIT_SIDE_SECOND);
	h.pump();
	REQUIRE(second != nullptr);
	const int tile_b = second->get_leaf_id();
	h.editor_data.register_tile(tile_b);

	Node2D *root_c = memnew(Node2D);
	const int scene_c = add_test_scene(h.editor_data, tile_b, root_c);
	h.pump();

	h.workspace->set_focused_leaf(tile_b);
	h.editor_data.set_focused_tile_id(tile_b);
	h.editor_data.set_edited_scene(scene_c);
	check_focus_invariant(h.editor_data, h.workspace);

	h.workspace->set_focused_leaf(tile_a);
	h.editor_data.set_focused_tile_id(tile_a);
	h.editor_data.set_edited_scene(scene_b);
	check_focus_invariant(h.editor_data, h.workspace);

	const int collapsed_id = tile_b;
	WorkspaceLeafNode *survivor = h.workspace->get_focused_leaf();
	h.workspace->collapse(second);
	h.pump();
	h.editor_data.migrate_tile_scenes(collapsed_id, survivor->get_leaf_id());
	h.editor_data.unregister_tile(collapsed_id);
	check_focus_invariant(h.editor_data, h.workspace);
	CHECK(h.editor_data.get_tile_scene_indices(survivor->get_leaf_id()).has(scene_c));

	h.unmount();
}

TEST_CASE("[SceneTree][Editor] collapse-focused-leaf-with-split-sibling-migrates-to-surviving-tile") {
	WorkspaceHarness h;
	h.mount();
	h.pump();

	const int tile_a = h.workspace->get_focused_leaf_id();
	Node2D *root_a = memnew(Node2D);
	const int scene_a = add_test_scene(h.editor_data, tile_a, root_a);

	WorkspaceLeafNode *leaf_a = h.workspace->get_leaf_by_id(tile_a);
	WorkspaceLeafNode *leaf_b = h.workspace->split(leaf_a, false, EditorSceneWorkspace::SPLIT_SIDE_SECOND);
	h.pump();
	REQUIRE(leaf_b != nullptr);
	const int tile_b = leaf_b->get_leaf_id();
	h.editor_data.register_tile(tile_b);

	WorkspaceLeafNode *leaf_c = h.workspace->split(leaf_b, true, EditorSceneWorkspace::SPLIT_SIDE_SECOND);
	h.pump();
	REQUIRE(leaf_c != nullptr);
	const int tile_c = leaf_c->get_leaf_id();
	h.editor_data.register_tile(tile_c);

	h.workspace->set_focused_leaf(tile_a);
	h.editor_data.set_focused_tile_id(tile_a);
	h.editor_data.set_tile_current_scene(tile_a, scene_a);
	h.editor_data.set_edited_scene(scene_a);
	check_focus_invariant(h.editor_data, h.workspace);

	LeafRemovedTracker tracker;
	h.workspace->connect("leaf_removed", callable_mp(&tracker, &LeafRemovedTracker::on_leaf_removed));

	h.workspace->collapse(leaf_a);
	h.pump();

	CHECK(tracker.removed_leaf_id == tile_a);
	CHECK(tracker.successor_leaf_id != tile_a);
	CHECK(h.workspace->get_leaf_by_id(tracker.successor_leaf_id) != nullptr);
	CHECK((tracker.successor_leaf_id == tile_b || tracker.successor_leaf_id == tile_c));

	if (tracker.successor_leaf_id != tile_a && h.workspace->get_leaf_by_id(tracker.successor_leaf_id)) {
		h.editor_data.migrate_tile_scenes(tile_a, tracker.successor_leaf_id);
		h.editor_data.unregister_tile(tile_a);
		CHECK(h.editor_data.get_tile_scene_indices(tracker.successor_leaf_id).has(scene_a));
	}

	h.unmount();
}

TEST_CASE("[SceneTree][Editor] tile-id-model") {
	WorkspaceHarness h;
	h.mount();
	h.pump();

	const int tile_a = h.workspace->get_focused_leaf_id();
	Node2D *root_a = memnew(Node2D);
	const int scene_a = add_test_scene(h.editor_data, tile_a, root_a);
	Node2D *root_b = memnew(Node2D);
	const int scene_b = add_test_scene(h.editor_data, tile_a, root_b);
	h.pump();

	CHECK(h.editor_data.get_edited_scenes()[scene_a].tile_id == tile_a);
	CHECK(h.editor_data.get_edited_scenes()[scene_b].tile_id == tile_a);
	CHECK(h.editor_data.get_tile_current_scene(tile_a) == scene_b);

	WorkspaceLeafNode *second = h.workspace->split(h.workspace->get_focused_leaf(), true, EditorSceneWorkspace::SPLIT_SIDE_SECOND);
	h.pump();
	REQUIRE(second != nullptr);
	const int tile_b = second->get_leaf_id();
	h.editor_data.register_tile(tile_b);

	Node2D *root_c = memnew(Node2D);
	const int scene_c = add_test_scene(h.editor_data, tile_b, root_c);
	h.pump();

	CHECK(h.editor_data.get_edited_scenes()[scene_a].tile_id == tile_a);
	CHECK(h.editor_data.get_edited_scenes()[scene_c].tile_id == tile_b);
	CHECK(h.editor_data.get_tile_scene_indices(tile_a).size() == 2);
	CHECK(h.editor_data.get_tile_scene_indices(tile_b).size() == 1);

	h.workspace->move_content(h.workspace->get_leaf_by_id(tile_a), second);
	h.pump();
	CHECK(second->get_leaf_content()->get_content_type() == StringName("scene"));

	Ref<ConfigFile> config;
	config.instantiate();
	EditorSceneWorkspace::save_to_config(config, h.workspace);
	const Vector<int> saved_tile_ids = { tile_a, tile_b };
	const int saved_focus = h.workspace->get_focused_leaf_id();

	h.unmount();

	WorkspaceHarness h2;
	h2.mount();
	h2.workspace->restore_from_config(config);
	h2.pump();

	for (int tile_id : saved_tile_ids) {
		CHECK(h2.workspace->get_leaf_by_id(tile_id) != nullptr);
	}
	CHECK(h2.workspace->get_focused_leaf_id() == saved_focus);

	WorkspaceLeafNode *fourth = h2.workspace->split(h2.workspace->get_focused_leaf(), false, EditorSceneWorkspace::SPLIT_SIDE_SECOND);
	h2.pump();
	for (int tile_id : saved_tile_ids) {
		CHECK(fourth->get_leaf_id() != tile_id);
	}
	CHECK(fourth->get_leaf_id() > tile_b);

	h2.unmount();
}

static void reparent_stand_in_main_screen(Control *p_main_screen, ScenePaneTile *p_tile) {
	ERR_FAIL_NULL(p_main_screen);
	ERR_FAIL_NULL(p_tile);
	Control *host = p_tile->get_content_host();
	ERR_FAIL_NULL(host);
	if (p_main_screen->get_parent() == host) {
		return;
	}
	if (p_main_screen->get_parent()) {
		p_main_screen->get_parent()->remove_child(p_main_screen);
	}
	host->add_child(p_main_screen);
	p_main_screen->set_anchors_and_offsets_preset(Control::PRESET_FULL_RECT);
}

static void attach_tile_scene_display(
		EditorData &p_data,
		EditorSceneWorkspace *p_workspace,
		SubViewportContainer *p_live_container,
		int p_focused_tile_id) {
	for (int i = 0; i < p_data.get_edited_scene_count(); i++) {
		EditorSceneContext *ctx = p_data.get_scene_context(i);
		const int tile_id = p_data.get_scene_tile(i);
		ScenePaneTile *tile = p_workspace->get_tile_by_id(tile_id);
		const bool is_tile_current = tile && p_data.get_tile_current_scene(tile_id) == i;

		if (!is_tile_current) {
			if (ctx->is_active()) {
				ctx->deactivate();
			}
			continue;
		}

		const bool is_focused_tile = tile_id == p_focused_tile_id;
		if (is_focused_tile) {
			tile->set_preview_mode(TilePreviewMode::FOCUSED_LIVE);
			ctx->set_display_parent(p_live_container, true, true);
			ctx->get_viewport()->set_update_mode(SubViewport::UPDATE_ALWAYS);
		} else if (ctx->scene_has_3d_content()) {
			tile->set_preview_mode(TilePreviewMode::LIVE_3D);
			SubViewportContainer *context_host = tile->get_context_viewport_host();
			ctx->set_display_parent(context_host, false);
			tile->bind_3d_preview_world(ctx->get_world_3d());
			tile->apply_3d_preview_camera_state(Dictionary());
			ctx->get_viewport()->set_update_mode(context_host->is_visible_in_tree() ? SubViewport::UPDATE_ALWAYS : SubViewport::UPDATE_DISABLED);
			context_host->recalc_force_viewport_sizes();
			context_host->queue_redraw();
		} else {
			tile->set_preview_mode(TilePreviewMode::LIVE_2D);
			SubViewportContainer *preview = tile->get_preview_container();
			ctx->set_display_parent(preview, false);
			ctx->get_viewport()->set_update_mode(preview->is_visible_in_tree() ? SubViewport::UPDATE_ALWAYS : SubViewport::UPDATE_DISABLED);
			preview->recalc_force_viewport_sizes();
			preview->queue_redraw();
		}
	}
}

TEST_CASE("[SceneTree][Editor] reparent-render") {
	WorkspaceHarness h;
	h.mount();
	h.pump();

	WorkspaceLeafNode *first = h.workspace->get_focused_leaf();
	WorkspaceLeafNode *second = h.workspace->split(first, false, EditorSceneWorkspace::SPLIT_SIDE_SECOND);
	h.pump();
	REQUIRE(second != nullptr);

	ScenePaneTile *tile_a = first->get_pane_tile();
	ScenePaneTile *tile_b = second->get_pane_tile();
	REQUIRE(tile_a != nullptr);
	REQUIRE(tile_b != nullptr);

	const int tile_a_id = tile_a->get_tile_id();
	const int tile_b_id = tile_b->get_tile_id();
	h.editor_data.register_tile(tile_a_id);
	h.editor_data.register_tile(tile_b_id);

	Node2D *root_a = memnew(Node2D);
	const int scene_a = add_test_scene(h.editor_data, tile_a_id, root_a);
	Node2D *root_b = memnew(Node2D);
	const int scene_b = add_test_scene(h.editor_data, tile_b_id, root_b);
	h.pump();

	PanelContainer *main_screen = memnew(PanelContainer);
	main_screen->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	main_screen->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	SubViewportContainer *live_container = memnew(SubViewportContainer);
	live_container->set_stretch(true);
	live_container->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	live_container->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	main_screen->add_child(live_container);

	auto check_focused_live = [&](int p_focused_tile_id, int p_focused_scene_idx, int p_preview_scene_idx, int p_preview_tile_id) {
		reparent_stand_in_main_screen(main_screen, h.workspace->get_tile_by_id(p_focused_tile_id));
		h.workspace->set_focused_leaf(p_focused_tile_id);
		h.editor_data.set_focused_tile_id(p_focused_tile_id);
		attach_tile_scene_display(h.editor_data, h.workspace, live_container, p_focused_tile_id);
		h.pump();

		EditorSceneContext *focused_ctx = h.editor_data.get_scene_context(p_focused_scene_idx);
		EditorSceneContext *preview_ctx = h.editor_data.get_scene_context(p_preview_scene_idx);
		ScenePaneTile *preview_tile = h.workspace->get_tile_by_id(p_preview_tile_id);

		CHECK(main_screen->get_parent() == h.workspace->get_tile_by_id(p_focused_tile_id)->get_content_host());
		CHECK(focused_ctx->get_viewport()->get_parent() == live_container);
		CHECK(focused_ctx->get_viewport()->get_update_mode() == SubViewport::UPDATE_ALWAYS);
		CHECK(focused_ctx->get_viewport()->is_inside_tree());
		CHECK(preview_ctx->get_viewport()->get_parent() == preview_tile->get_preview_container());
		CHECK(preview_ctx->get_viewport()->get_update_mode() == SubViewport::UPDATE_ALWAYS);
		CHECK(preview_ctx->get_viewport()->is_inside_tree());
	};

	check_focused_live(tile_a_id, scene_a, scene_b, tile_b_id);

	// Focus switch must keep both tiles rendering (no stale UPDATE_DISABLED black panes).
	check_focused_live(tile_b_id, scene_b, scene_a, tile_a_id);

	// Switch back to the first tile and verify the live path again.
	check_focused_live(tile_a_id, scene_a, scene_b, tile_b_id);

	for (int i = 0; i < h.editor_data.get_edited_scene_count(); i++) {
		h.editor_data.get_scene_context(i)->deactivate();
	}
	if (main_screen->get_parent()) {
		main_screen->get_parent()->remove_child(main_screen);
	}
	memdelete(main_screen);
	h.editor_data.clear_edited_scenes();

	h.unmount();
}

TEST_CASE("[SceneTree][Editor] drop-region-select") {
	const Size2 pane(400, 300);

	CHECK(EditorSceneWorkspace::drop_region_at(pane, Point2(200, 150)) == EditorSceneWorkspace::DROP_CENTER);
	CHECK(EditorSceneWorkspace::drop_region_at(pane, Point2(200, 140)) == EditorSceneWorkspace::DROP_CENTER);
	CHECK(EditorSceneWorkspace::drop_region_at(pane, Point2(190, 150)) == EditorSceneWorkspace::DROP_CENTER);

	CHECK(EditorSceneWorkspace::drop_region_at(pane, Point2(10, 150)) == EditorSceneWorkspace::DROP_LEFT);
	CHECK(EditorSceneWorkspace::drop_region_at(pane, Point2(390, 150)) == EditorSceneWorkspace::DROP_RIGHT);
	CHECK(EditorSceneWorkspace::drop_region_at(pane, Point2(200, 10)) == EditorSceneWorkspace::DROP_TOP);
	CHECK(EditorSceneWorkspace::drop_region_at(pane, Point2(200, 290)) == EditorSceneWorkspace::DROP_BOTTOM);

	// Near-corner: dominant axis wins (|dx| vs |dy|).
	CHECK(EditorSceneWorkspace::drop_region_at(pane, Point2(10, 10)) == EditorSceneWorkspace::DROP_LEFT);
	CHECK(EditorSceneWorkspace::drop_region_at(pane, Point2(390, 10)) == EditorSceneWorkspace::DROP_RIGHT);
	CHECK(EditorSceneWorkspace::drop_region_at(pane, Point2(10, 290)) == EditorSceneWorkspace::DROP_LEFT);
	CHECK(EditorSceneWorkspace::drop_region_at(pane, Point2(390, 290)) == EditorSceneWorkspace::DROP_RIGHT);

	// On the diagonal of a square pane, relative offsets break the tie.
	const Size2 square(400, 400);
	CHECK(EditorSceneWorkspace::drop_region_at(square, Point2(50, 50)) == EditorSceneWorkspace::DROP_LEFT);

	// Preview rects cover real halves / whole pane.
	const Rect2 left_preview = EditorSceneWorkspace::drop_preview_rect(pane, EditorSceneWorkspace::DROP_LEFT);
	CHECK(left_preview.size.x == doctest::Approx(pane.x * 0.5f));
	CHECK(left_preview.size.y == doctest::Approx(pane.y));

	const Rect2 center_preview = EditorSceneWorkspace::drop_preview_rect(pane, EditorSceneWorkspace::DROP_CENTER);
	CHECK(center_preview.size == pane);
}

TEST_CASE("[SceneTree][Editor] preview-camera-state") {
	ScenePaneTile *tile = memnew(ScenePaneTile);
	EditorSelection selection;
	EditorData editor_data;
	tile->setup(0, &selection, editor_data);

	Dictionary viewport_state;
	viewport_state["position"] = Vector3(1, 2, 3);
	viewport_state["x_rotation"] = 0.0;
	viewport_state["y_rotation"] = 0.0;
	viewport_state["distance"] = 8.0;
	tile->apply_3d_preview_camera_state(viewport_state);

	Camera3D *camera = tile->get_preview_3d_camera();
	REQUIRE(camera != nullptr);
	const Vector3 expected_origin = camera->get_transform().origin;
	CHECK(expected_origin.is_equal_approx(Vector3(1, 2, 11)));

	memdelete(tile);
}

TEST_CASE("[SceneTree][Editor] leaf-content-generic") {
	WorkspaceHarness h;
	h.mount();

	WorkspaceLeafNode *scene_leaf = h.workspace->get_focused_leaf();
	REQUIRE(scene_leaf != nullptr);
	REQUIRE(scene_leaf->get_leaf_content() != nullptr);
	CHECK(scene_leaf->get_leaf_content()->get_content_type() == StringName("scene"));
	CHECK(scene_leaf->get_pane_tile() != nullptr);

	WorkspaceLeafNode *script_leaf_node = h.workspace->split(scene_leaf, false, EditorSceneWorkspace::SPLIT_SIDE_SECOND);
	h.pump();
	REQUIRE(script_leaf_node != nullptr);
	replace_leaf_with_script(script_leaf_node, "ScriptPane");
	CHECK(script_leaf_node->get_leaf_content()->get_content_type() == StringName("script"));
	CHECK(script_leaf_node->get_leaf_content()->get_scene_context() == nullptr);
	CHECK(scene_leaf->get_leaf_content()->get_scene_context() == nullptr); // no open scene in harness

	CHECK(h.workspace->move_content(scene_leaf, script_leaf_node));
	CHECK(scene_leaf->get_leaf_content()->get_content_type() == StringName("script"));
	CHECK(script_leaf_node->get_leaf_content()->get_content_type() == StringName("scene"));
	CHECK(script_leaf_node->get_pane_tile() != nullptr);

	WorkspaceLeafNode *third = h.workspace->split(script_leaf_node, true, EditorSceneWorkspace::SPLIT_SIDE_FIRST);
	h.pump();
	REQUIRE(third != nullptr);
	CHECK(h.workspace->get_leaf_count() == 3);

	h.workspace->collapse(third);
	h.pump();
	CHECK(h.workspace->get_leaf_count() == 2);

	Ref<ConfigFile> config;
	config.instantiate();
	EditorSceneWorkspace::save_to_config(config, h.workspace);

	const int scene_leaf_id = scene_leaf->get_leaf_id();
	const int script_leaf_id = script_leaf_node->get_leaf_id();
	const String saved_script_title = scene_leaf->get_leaf_content()->get_tab_title();

	h.unmount();

	WorkspaceHarness h2;
	h2.mount();
	h2.workspace->restore_from_config(config);
	h2.pump();

	CHECK(h2.workspace->get_leaf_count() == 2);
	WorkspaceLeafNode *restored_scene = h2.workspace->get_leaf_by_id(script_leaf_id);
	WorkspaceLeafNode *restored_script = h2.workspace->get_leaf_by_id(scene_leaf_id);
	REQUIRE(restored_scene != nullptr);
	REQUIRE(restored_script != nullptr);
	CHECK(restored_scene->get_leaf_content()->get_content_type() == StringName("scene"));
	CHECK(restored_script->get_leaf_content()->get_content_type() == StringName("script"));
	CHECK(restored_script->get_leaf_content()->get_tab_title() == saved_script_title);
	CHECK(restored_scene->get_leaf_content()->get_scene_context() == nullptr);
	CHECK(restored_script->get_leaf_content()->get_scene_context() == nullptr);

	h2.unmount();
}

class TileHistoryDockTestAccess {
public:
	static void refresh(HistoryDock *p_dock) { p_dock->refresh_history(); }
	static String newest_action_name(HistoryDock *p_dock) {
		if (p_dock->action_list->get_item_count() <= 1) {
			return String();
		}
		return p_dock->action_list->get_item_text(0);
	}
};

class TileConnectionsDockTestAccess {
public:
	static Object *selected_object(SignalsDock *p_dock) {
		return p_dock->connections ? p_dock->connections->selected_object : nullptr;
	}
};

static int _right_tab_index(TabContainer *p_tabs, EditorDock *p_dock) {
	if (!p_tabs || !p_dock) {
		return -1;
	}
	for (int i = 0; i < p_tabs->get_tab_count(); i++) {
		if (p_tabs->get_tab_control(i) == p_dock) {
			return i;
		}
	}
	return -1;
}

TEST_CASE("[SceneTree][Editor] tile-dock-region") {
	Window *tree_root = SceneTree::get_singleton()->get_root();
	EditorUndoRedoManager *ur_manager = memnew(EditorUndoRedoManager);

	WorkspaceHarness h;
	h.mount();
	h.pump();

	WorkspaceLeafNode *first = h.workspace->get_focused_leaf();
	WorkspaceLeafNode *second = h.workspace->split(first, false, EditorSceneWorkspace::SPLIT_SIDE_SECOND);
	h.pump();
	REQUIRE(second != nullptr);

	ScenePaneTile *tile_a = first->get_pane_tile();
	ScenePaneTile *tile_b = second->get_pane_tile();
	REQUIRE(tile_a != nullptr);
	REQUIRE(tile_b != nullptr);

	CHECK(tile_a->get_signals_dock() != nullptr);
	CHECK(tile_a->get_groups_dock() != nullptr);
	CHECK(tile_a->get_history_dock() != nullptr);
	CHECK(tile_a->is_ancestor_of(tile_a->get_signals_dock()));
	CHECK(tile_a->is_ancestor_of(tile_a->get_groups_dock()));
	CHECK(tile_a->is_ancestor_of(tile_a->get_history_dock()));

	EditorSceneContext *context_a = memnew(EditorSceneContext);
	context_a->set_history_id(42);
	Node2D *root_a = memnew(Node2D);
	context_a->set_scene_root_node(root_a);
	Node2D *node_a = memnew(Node2D);
	root_a->add_child(node_a);
	node_a->add_to_group("tile_a_group", false);

	ur_manager->create_action_for_history("Tile A action", 42);
	ur_manager->add_do_method(node_a, "set_name", "RenamedA");
	ur_manager->commit_action();

	EditorSceneContext *context_b = memnew(EditorSceneContext);
	context_b->set_history_id(99);
	Node2D *root_b = memnew(Node2D);
	context_b->set_scene_root_node(root_b);
	Node2D *node_b = memnew(Node2D);
	root_b->add_child(node_b);
	node_b->add_to_group("tile_b_group", false);

	ur_manager->create_action_for_history("Tile B action", 99);
	ur_manager->add_do_method(node_b, "set_name", "RenamedB");
	ur_manager->commit_action();

	tile_a->get_scene_tree_dock()->set_scene_context(context_a);
	tile_a->get_inspector_dock()->set_scene_context(context_a);
	tile_a->get_signals_dock()->set_scene_context(context_a);
	tile_a->get_groups_dock()->set_scene_context(context_a);
	tile_a->get_history_dock()->set_scene_context(context_a);

	tile_b->get_scene_tree_dock()->set_scene_context(context_b);
	tile_b->get_inspector_dock()->set_scene_context(context_b);
	tile_b->get_signals_dock()->set_scene_context(context_b);
	tile_b->get_groups_dock()->set_scene_context(context_b);
	tile_b->get_history_dock()->set_scene_context(context_b);

	tile_a->get_signals_dock()->show();
	tile_a->get_groups_dock()->show();
	tile_a->get_history_dock()->show();
	tile_b->get_signals_dock()->show();
	tile_b->get_groups_dock()->show();
	tile_b->get_history_dock()->show();
	h.pump();

	// GroupsEditor defers group-tree rebuilds until visible; rebind after show().
	tile_a->get_groups_dock()->set_scene_context(context_a);
	tile_b->get_groups_dock()->set_scene_context(context_b);
	h.pump();

	tile_a->get_signals_dock()->set_object(node_a);
	context_a->activate(tree_root);
	Vector<Node *> select_a;
	select_a.push_back(node_a);
	tile_a->get_groups_dock()->set_selection(select_a);
	h.pump();

	CHECK(TileConnectionsDockTestAccess::selected_object(tile_a->get_signals_dock()) == node_a);
	CHECK(tile_a->get_groups_dock()->get_scene_context() == context_a);
	TileHistoryDockTestAccess::refresh(tile_a->get_history_dock());
	CHECK(TileHistoryDockTestAccess::newest_action_name(tile_a->get_history_dock()) == "Tile A action");

	tile_b->get_signals_dock()->set_object(node_b);
	context_b->activate(tree_root);
	Vector<Node *> select_b;
	select_b.push_back(node_b);
	tile_b->get_groups_dock()->set_selection(select_b);
	h.pump();

	CHECK(TileConnectionsDockTestAccess::selected_object(tile_b->get_signals_dock()) == node_b);
	CHECK(tile_b->get_groups_dock()->get_scene_context() == context_b);
	TileHistoryDockTestAccess::refresh(tile_b->get_history_dock());
	CHECK(TileHistoryDockTestAccess::newest_action_name(tile_b->get_history_dock()) == "Tile B action");

	// Tile B edits must not bleed into tile A's bound panels.
	CHECK(TileConnectionsDockTestAccess::selected_object(tile_a->get_signals_dock()) == node_a);
	CHECK(tile_a->get_groups_dock()->get_scene_context() == context_a);
	CHECK(TileHistoryDockTestAccess::newest_action_name(tile_a->get_history_dock()) == "Tile A action");

	TabContainer *right_tabs = tile_a->get_dock_region()->get_right_tabs();
	REQUIRE(right_tabs != nullptr);
	const int history_tab = _right_tab_index(right_tabs, tile_a->get_history_dock());
	REQUIRE(history_tab >= 0);
	right_tabs->set_current_tab(history_tab);

	HSplitContainer *body = tile_a->get_dock_region()->get_body();
	REQUIRE(body != nullptr);
	h.host->set_size(Size2(900, 600));
	h.pump();
	PackedInt32Array offsets = body->get_split_offsets();
	if (offsets.size() < 2) {
		offsets.resize(2);
	}
	offsets.write[0] = 220;
	offsets.write[1] = -240;
	body->set_split_offsets(offsets);
	h.pump();

	Ref<ConfigFile> config;
	config.instantiate();
	EditorSceneWorkspace::save_to_config(config, h.workspace);

	const String layout_section = EditorSceneWorkspace::leaf_layout_section(tile_a->get_tile_id());
	CHECK(config->has_section_key(layout_section, "tile_dock_right"));
	CHECK(int(config->get_value(layout_section, "tile_dock_hsplit_1")) == 220);
	CHECK(int(config->get_value(layout_section, "tile_dock_hsplit_2")) == -240);
	CHECK(int(config->get_value(layout_section, "tile_dock_right_selected_tab_idx")) == history_tab);

	const int tile_a_id = tile_a->get_tile_id();
	const int tile_b_id = tile_b->get_tile_id();

	context_a->deactivate();
	context_b->deactivate();
	memdelete(context_a);
	memdelete(context_b);

	h.unmount();

	WorkspaceHarness h2;
	h2.mount();
	h2.workspace->restore_from_config(config);
	h2.pump();

	ScenePaneTile *restored_a = h2.workspace->get_tile_by_id(tile_a_id);
	ScenePaneTile *restored_b = h2.workspace->get_tile_by_id(tile_b_id);
	REQUIRE(restored_a != nullptr);
	REQUIRE(restored_b != nullptr);

	TabContainer *restored_tabs = restored_a->get_dock_region()->get_right_tabs();
	REQUIRE(restored_tabs != nullptr);
	const int restored_history_tab = _right_tab_index(restored_tabs, restored_a->get_history_dock());
	REQUIRE(restored_history_tab >= 0);
	CHECK(restored_tabs->get_current_tab() == restored_history_tab);

	HSplitContainer *restored_body = restored_a->get_dock_region()->get_body();
	REQUIRE(restored_body != nullptr);
	h2.host->set_size(Size2(900, 600));
	h2.pump();
	PackedInt32Array restored_offsets = restored_body->get_split_offsets();
	REQUIRE(restored_offsets.size() >= 2);
	CHECK(restored_offsets[0] == 220 * EDSCALE);
	CHECK(restored_offsets[1] == -240 * EDSCALE);

	h2.unmount();
	memdelete(ur_manager);
}

TEST_CASE("[SceneTree][Editor] script-leaf-open") {
	WorkspaceHarness h;
	h.mount();
	h.pump();

	WorkspaceLeafNode *scene_leaf = h.workspace->get_focused_leaf();
	REQUIRE(scene_leaf != nullptr);
	CHECK(h.workspace->get_leaf_count() == 1);
	CHECK(h.workspace->get_script_leaf() == nullptr);

	// Opening a script from the scene tile creates a script leaf beside it.
	WorkspaceLeafNode *script_leaf_node = h.workspace->open_script_leaf(scene_leaf, "res://player.fs");
	h.pump();
	REQUIRE(script_leaf_node != nullptr);
	CHECK(script_leaf_node != scene_leaf);
	CHECK(h.workspace->get_leaf_count() == 2);
	REQUIRE(script_leaf_node->get_leaf_content() != nullptr);
	CHECK(script_leaf_node->get_leaf_content()->get_content_type() == StringName("script"));
	CHECK(script_leaf_node->get_leaf_content()->get_scene_context() == nullptr);
	CHECK(h.workspace->get_script_leaf() == script_leaf_node);

	ScriptLeaf *script_leaf = Object::cast_to<ScriptLeaf>(script_leaf_node->get_leaf_content()->get_root_control());
	REQUIRE(script_leaf != nullptr);
	CHECK(script_leaf->get_script_path() == "res://player.fs");
	CHECK(script_leaf->get_tab_title() == "player.fs");

	// Opening a different script creates a second script leaf (U15c).
	WorkspaceLeafNode *second_leaf = h.workspace->open_script_leaf(scene_leaf, "res://enemy.fs");
	h.pump();
	CHECK(second_leaf != script_leaf_node);
	CHECK(h.workspace->get_leaf_count() == 3);
	CHECK(h.workspace->get_script_leaves().size() == 2);
	ScriptLeaf *enemy_leaf = Object::cast_to<ScriptLeaf>(second_leaf->get_leaf_content()->get_root_control());
	REQUIRE(enemy_leaf != nullptr);
	CHECK(enemy_leaf->get_script_path() == "res://enemy.fs");
	CHECK(script_leaf->get_script_path() == "res://player.fs");

	// Re-opening the same path reuses the existing leaf rather than splitting again.
	WorkspaceLeafNode *again = h.workspace->open_script_leaf(scene_leaf, "res://enemy.fs");
	h.pump();
	CHECK(again == second_leaf);
	CHECK(h.workspace->get_leaf_count() == 3);

	// Both script leaves round-trip through persistence.
	Ref<ConfigFile> config;
	config.instantiate();
	EditorSceneWorkspace::save_to_config(config, h.workspace);
	const int scene_leaf_id = scene_leaf->get_leaf_id();
	const int player_leaf_id = script_leaf_node->get_leaf_id();
	const int enemy_leaf_id = second_leaf->get_leaf_id();

	h.unmount();

	WorkspaceHarness h2;
	h2.mount();
	h2.workspace->restore_from_config(config);
	h2.pump();

	CHECK(h2.workspace->get_leaf_count() == 3);
	CHECK(h2.workspace->get_leaf_by_id(scene_leaf_id) != nullptr);
	CHECK(h2.workspace->get_script_leaves().size() == 2);
	WorkspaceLeafNode *restored_player = h2.workspace->find_script_leaf_for_path("res://player.fs");
	WorkspaceLeafNode *restored_enemy = h2.workspace->find_script_leaf_for_path("res://enemy.fs");
	REQUIRE(restored_player != nullptr);
	REQUIRE(restored_enemy != nullptr);
	CHECK(restored_player->get_leaf_id() == player_leaf_id);
	CHECK(restored_enemy->get_leaf_id() == enemy_leaf_id);
	ScriptLeaf *restored_player_leaf = Object::cast_to<ScriptLeaf>(restored_player->get_leaf_content()->get_root_control());
	ScriptLeaf *restored_enemy_leaf = Object::cast_to<ScriptLeaf>(restored_enemy->get_leaf_content()->get_root_control());
	REQUIRE(restored_player_leaf != nullptr);
	REQUIRE(restored_enemy_leaf != nullptr);
	CHECK(restored_player_leaf->get_script_path() == "res://player.fs");
	CHECK(restored_enemy_leaf->get_script_path() == "res://enemy.fs");

	h2.unmount();
}

TEST_CASE("[SceneTree][Editor] script-leaf-collapse-keeps-scene-focus") {
	WorkspaceHarness h;
	h.mount();
	h.pump();

	WorkspaceLeafNode *scene_a = h.workspace->get_focused_leaf();
	REQUIRE(scene_a != nullptr);

	// Two scene tiles, with a script leaf opened beside the second one, so that
	// scene_b's collapse successor is the (non-scene) script leaf.
	WorkspaceLeafNode *scene_b = h.workspace->split(scene_a, false, EditorSceneWorkspace::SPLIT_SIDE_SECOND);
	h.pump();
	REQUIRE(scene_b != nullptr);
	WorkspaceLeafNode *script_leaf = h.workspace->open_script_leaf(scene_b, "res://a.fs");
	h.pump();
	REQUIRE(script_leaf != nullptr);
	CHECK(script_leaf->get_pane_tile() == nullptr);

	// Collapsing the focused scene tile promotes the script leaf structurally, but
	// focus must land on a scene tile rather than the non-focusable script leaf.
	h.workspace->set_focused_leaf(scene_b->get_leaf_id());
	h.workspace->collapse(scene_b);
	h.pump();

	WorkspaceLeafNode *focused = h.workspace->get_focused_leaf();
	REQUIRE(focused != nullptr);
	CHECK(focused->get_pane_tile() != nullptr);
	CHECK(focused->get_leaf_content()->get_content_type() == StringName("scene"));
	CHECK(h.workspace->get_script_leaf() == script_leaf);

	h.unmount();
}

static SignalsDock *_focused_signals_dock(EditorSceneWorkspace *p_workspace) {
	ScenePaneTile *tile = p_workspace ? p_workspace->get_focused_tile() : nullptr;
	return tile ? tile->get_signals_dock() : nullptr;
}

static GroupsDock *_focused_groups_dock(EditorSceneWorkspace *p_workspace) {
	ScenePaneTile *tile = p_workspace ? p_workspace->get_focused_tile() : nullptr;
	return tile ? tile->get_groups_dock() : nullptr;
}

static HistoryDock *_focused_history_dock(EditorSceneWorkspace *p_workspace) {
	ScenePaneTile *tile = p_workspace ? p_workspace->get_focused_tile() : nullptr;
	return tile ? tile->get_history_dock() : nullptr;
}

static SceneTreeDock *_focused_scene_tree_dock(EditorSceneWorkspace *p_workspace) {
	ScenePaneTile *tile = p_workspace ? p_workspace->get_focused_tile() : nullptr;
	return tile ? tile->get_scene_tree_dock() : nullptr;
}

static InspectorDock *_focused_inspector_dock(EditorSceneWorkspace *p_workspace) {
	ScenePaneTile *tile = p_workspace ? p_workspace->get_focused_tile() : nullptr;
	return tile ? tile->get_inspector_dock() : nullptr;
}

class TileInspectorDockTestAccess {
public:
	static String info_button_text(InspectorDock *p_dock) {
		return p_dock && p_dock->info ? p_dock->info->get_text() : String();
	}
};

TEST_CASE("[SceneTree][Editor] focused-scenetree-inspector-accessor") {
	Window *tree_root = SceneTree::get_singleton()->get_root();

	WorkspaceHarness h;
	h.mount();
	h.pump();

	WorkspaceLeafNode *first = h.workspace->get_focused_leaf();
	WorkspaceLeafNode *second = h.workspace->split(first, false, EditorSceneWorkspace::SPLIT_SIDE_SECOND);
	h.pump();
	REQUIRE(second != nullptr);

	ScenePaneTile *tile_a = first->get_pane_tile();
	ScenePaneTile *tile_b = second->get_pane_tile();
	REQUIRE(tile_a != nullptr);
	REQUIRE(tile_b != nullptr);

	CHECK(_focused_scene_tree_dock(h.workspace) == tile_a->get_scene_tree_dock());
	CHECK(_focused_inspector_dock(h.workspace) == tile_a->get_inspector_dock());

	h.workspace->set_focused_leaf(tile_b->get_tile_id());
	h.pump();

	CHECK(_focused_scene_tree_dock(h.workspace) == tile_b->get_scene_tree_dock());
	CHECK(_focused_inspector_dock(h.workspace) == tile_b->get_inspector_dock());

	EditorSceneContext *context_b = memnew(EditorSceneContext);
	Node2D *root_b = memnew(Node2D);
	context_b->set_scene_root_node(root_b);
	Node2D *node_b = memnew(Node2D);
	root_b->add_child(node_b);

	tile_b->get_scene_tree_dock()->set_scene_context(context_b);
	tile_b->get_inspector_dock()->set_scene_context(context_b);

	SceneTreeDock *scene_tree = _focused_scene_tree_dock(h.workspace);
	InspectorDock *inspector = _focused_inspector_dock(h.workspace);
	REQUIRE(scene_tree == tile_b->get_scene_tree_dock());
	REQUIRE(inspector == tile_b->get_inspector_dock());

	scene_tree->set_filter("FocusedTileFilter");
	context_b->activate(tree_root);
	Vector<Node *> select_b;
	select_b.push_back(node_b);
	scene_tree->set_selection(select_b);
	inspector->set_info("FocusedTileInfo", "Focused tile inspector message", false);
	h.pump();

	CHECK(scene_tree->get_filter() == "FocusedTileFilter");
	CHECK(context_b->get_selection()->is_selected(node_b));
	CHECK(TileInspectorDockTestAccess::info_button_text(inspector) == "FocusedTileInfo");

	context_b->deactivate();
	memdelete(context_b);

	h.unmount();
}

TEST_CASE("[SceneTree][Editor] focused-dock-accessor") {
	Window *tree_root = SceneTree::get_singleton()->get_root();
	EditorUndoRedoManager *ur_manager = memnew(EditorUndoRedoManager);

	WorkspaceHarness h;
	h.mount();
	h.pump();

	WorkspaceLeafNode *first = h.workspace->get_focused_leaf();
	WorkspaceLeafNode *second = h.workspace->split(first, false, EditorSceneWorkspace::SPLIT_SIDE_SECOND);
	h.pump();
	REQUIRE(second != nullptr);

	ScenePaneTile *tile_a = first->get_pane_tile();
	ScenePaneTile *tile_b = second->get_pane_tile();
	REQUIRE(tile_a != nullptr);
	REQUIRE(tile_b != nullptr);

	CHECK(_focused_signals_dock(h.workspace) == tile_a->get_signals_dock());
	CHECK(_focused_groups_dock(h.workspace) == tile_a->get_groups_dock());
	CHECK(_focused_history_dock(h.workspace) == tile_a->get_history_dock());

	h.workspace->set_focused_leaf(tile_b->get_tile_id());
	h.pump();

	CHECK(_focused_signals_dock(h.workspace) == tile_b->get_signals_dock());
	CHECK(_focused_groups_dock(h.workspace) == tile_b->get_groups_dock());
	CHECK(_focused_history_dock(h.workspace) == tile_b->get_history_dock());

	EditorSceneContext *context_b = memnew(EditorSceneContext);
	context_b->set_history_id(77);
	Node2D *root_b = memnew(Node2D);
	context_b->set_scene_root_node(root_b);
	Node2D *node_b = memnew(Node2D);
	root_b->add_child(node_b);

	ur_manager->create_action_for_history("Focused tile action", 77);
	ur_manager->add_do_method(node_b, "set_name", "FocusedB");
	ur_manager->commit_action();

	tile_b->get_signals_dock()->set_scene_context(context_b);
	tile_b->get_groups_dock()->set_scene_context(context_b);
	tile_b->get_history_dock()->set_scene_context(context_b);

	SignalsDock *signals = _focused_signals_dock(h.workspace);
	GroupsDock *groups = _focused_groups_dock(h.workspace);
	HistoryDock *history = _focused_history_dock(h.workspace);
	REQUIRE(signals == tile_b->get_signals_dock());
	REQUIRE(groups == tile_b->get_groups_dock());
	REQUIRE(history == tile_b->get_history_dock());

	signals->set_object(node_b);
	context_b->activate(tree_root);
	Vector<Node *> select_b;
	select_b.push_back(node_b);
	groups->set_selection(select_b);
	h.pump();

	CHECK(TileConnectionsDockTestAccess::selected_object(signals) == node_b);
	CHECK(groups->get_scene_context() == context_b);
	TileHistoryDockTestAccess::refresh(history);
	CHECK(TileHistoryDockTestAccess::newest_action_name(history) == "Focused tile action");

	context_b->deactivate();
	memdelete(context_b);

	h.unmount();
	memdelete(ur_manager);
}

TEST_CASE("[SceneTree][Editor] stale-singleton-guard") {
	Window *tree_root = SceneTree::get_singleton()->get_root();
	EditorUndoRedoManager *ur_manager = memnew(EditorUndoRedoManager);

	WorkspaceHarness h;
	h.mount();
	h.pump();

	WorkspaceLeafNode *first = h.workspace->get_focused_leaf();
	WorkspaceLeafNode *second = h.workspace->split(first, false, EditorSceneWorkspace::SPLIT_SIDE_SECOND);
	h.pump();
	REQUIRE(second != nullptr);

	ScenePaneTile *tile_a = first->get_pane_tile();
	ScenePaneTile *tile_b = second->get_pane_tile();
	REQUIRE(tile_a != nullptr);
	REQUIRE(tile_b != nullptr);

	CHECK(tile_a->get_signals_dock() != tile_b->get_signals_dock());
	CHECK(tile_a->get_groups_dock() != tile_b->get_groups_dock());
	CHECK(tile_a->get_history_dock() != tile_b->get_history_dock());

	EditorSceneContext *context_a = memnew(EditorSceneContext);
	context_a->set_history_id(11);
	Node2D *root_a = memnew(Node2D);
	context_a->set_scene_root_node(root_a);
	Node2D *node_a = memnew(Node2D);
	root_a->add_child(node_a);

	EditorSceneContext *context_b = memnew(EditorSceneContext);
	context_b->set_history_id(22);
	Node2D *root_b = memnew(Node2D);
	context_b->set_scene_root_node(root_b);
	Node2D *node_b = memnew(Node2D);
	root_b->add_child(node_b);

	ur_manager->create_action_for_history("Tile A stale guard", 11);
	ur_manager->add_do_method(node_a, "set_name", "A");
	ur_manager->commit_action();
	ur_manager->create_action_for_history("Tile B stale guard", 22);
	ur_manager->add_do_method(node_b, "set_name", "B");
	ur_manager->commit_action();

	for (ScenePaneTile *tile : { tile_a, tile_b }) {
		EditorSceneContext *ctx = tile == tile_a ? context_a : context_b;
		tile->get_signals_dock()->set_scene_context(ctx);
		tile->get_groups_dock()->set_scene_context(ctx);
		tile->get_history_dock()->set_scene_context(ctx);
		tile->get_signals_dock()->show();
		tile->get_groups_dock()->show();
		tile->get_history_dock()->show();
	}
	h.pump();

	// Seed tile A through the focused-tile accessor path while tile A is focused.
	tile_a->get_signals_dock()->set_object(node_a);
	context_a->activate(tree_root);
	Vector<Node *> select_a;
	select_a.push_back(node_a);
	tile_a->get_groups_dock()->set_selection(select_a);
	TileHistoryDockTestAccess::refresh(tile_a->get_history_dock());
	tile_a->get_scene_tree_dock()->set_filter("TileAFilter");
	tile_a->get_inspector_dock()->set_info("TileAInfo", "Tile A inspector message", false);
	h.pump();

	CHECK(TileConnectionsDockTestAccess::selected_object(tile_a->get_signals_dock()) == node_a);
	CHECK(TileHistoryDockTestAccess::newest_action_name(tile_a->get_history_dock()) == "Tile A stale guard");
	CHECK(tile_a->get_scene_tree_dock()->get_filter() == "TileAFilter");
	CHECK(TileInspectorDockTestAccess::info_button_text(tile_a->get_inspector_dock()) == "TileAInfo");

	// Focus tile B and edit only through focused-tile accessors.
	h.workspace->set_focused_leaf(tile_b->get_tile_id());
	h.pump();

	SignalsDock *focused_signals = _focused_signals_dock(h.workspace);
	GroupsDock *focused_groups = _focused_groups_dock(h.workspace);
	HistoryDock *focused_history = _focused_history_dock(h.workspace);
	SceneTreeDock *focused_scene_tree = _focused_scene_tree_dock(h.workspace);
	InspectorDock *focused_inspector = _focused_inspector_dock(h.workspace);
	REQUIRE(focused_signals == tile_b->get_signals_dock());
	REQUIRE(focused_groups == tile_b->get_groups_dock());
	REQUIRE(focused_history == tile_b->get_history_dock());
	REQUIRE(focused_scene_tree == tile_b->get_scene_tree_dock());
	REQUIRE(focused_inspector == tile_b->get_inspector_dock());

	focused_signals->set_object(node_b);
	context_b->activate(tree_root);
	Vector<Node *> select_b;
	select_b.push_back(node_b);
	focused_groups->set_selection(select_b);
	TileHistoryDockTestAccess::refresh(focused_history);
	focused_scene_tree->set_filter("TileBFilter");
	focused_inspector->set_info("TileBInfo", "Tile B inspector message", false);
	h.pump();

	CHECK(TileConnectionsDockTestAccess::selected_object(tile_b->get_signals_dock()) == node_b);
	CHECK(TileHistoryDockTestAccess::newest_action_name(tile_b->get_history_dock()) == "Tile B stale guard");
	CHECK(tile_b->get_scene_tree_dock()->get_filter() == "TileBFilter");
	CHECK(TileInspectorDockTestAccess::info_button_text(tile_b->get_inspector_dock()) == "TileBInfo");

	// Tile A must remain unchanged — a global singleton repoint would have overwritten this.
	CHECK(TileConnectionsDockTestAccess::selected_object(tile_a->get_signals_dock()) == node_a);
	CHECK(tile_a->get_groups_dock()->get_scene_context() == context_a);
	CHECK(TileHistoryDockTestAccess::newest_action_name(tile_a->get_history_dock()) == "Tile A stale guard");
	CHECK(tile_a->get_scene_tree_dock()->get_filter() == "TileAFilter");
	CHECK(TileInspectorDockTestAccess::info_button_text(tile_a->get_inspector_dock()) == "TileAInfo");

	context_a->deactivate();
	context_b->deactivate();
	memdelete(context_a);
	memdelete(context_b);

	h.unmount();
	memdelete(ur_manager);
}

} // namespace TestSceneWorkspace
