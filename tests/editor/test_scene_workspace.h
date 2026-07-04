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
#include "editor/editor_data.h"
#include "editor/editor_scene_context.h"
#include "editor/editor_scene_pane_tile.h"
#include "editor/editor_scene_workspace.h"

#include "scene/2d/node_2d.h"
#include "scene/3d/node_3d.h"
#include "scene/gui/box_container.h"
#include "scene/gui/split_container.h"
#include "scene/gui/subviewport_container.h"
#include "scene/main/viewport.h"
#include "scene/main/window.h"

#include "tests/test_macros.h"
#include "tests/test_tools.h"

namespace TestSceneWorkspace {

// Mounts a workspace under the live SceneTree root inside a full-rect host so
// layout-affecting operations settle. Pumps process + flush on request.
struct WorkspaceFixture {
	EditorData editor_data;
	EditorSelection *selection = nullptr;
	VBoxContainer *host = nullptr;
	EditorSceneWorkspace *workspace = nullptr;

	WorkspaceFixture() {
		Window *tree_root = SceneTree::get_singleton()->get_root();
		selection = memnew(EditorSelection);
		host = memnew(VBoxContainer);
		host->set_anchors_and_offsets_preset(Control::PRESET_FULL_RECT);
		tree_root->add_child(host);

		workspace = EditorSceneWorkspace::create_single_tile_workspace(selection, &editor_data);
		workspace->set_custom_minimum_size(Size2(800, 600));
		workspace->set_v_size_flags(Control::SIZE_EXPAND_FILL);
		workspace->set_h_size_flags(Control::SIZE_EXPAND_FILL);
		host->add_child(workspace);
		settle();
	}

	void settle() {
		SceneTree::get_singleton()->process(0.016);
		MessageQueue::get_singleton()->flush();
	}

	~WorkspaceFixture() {
		memdelete(workspace);
		Window *tree_root = SceneTree::get_singleton()->get_root();
		tree_root->remove_child(host);
		memdelete(host);
		memdelete(selection);
	}
};

TEST_CASE("[SceneTree][Editor] tile-model-mapping") {
	EditorData editor_data;

	const int a = editor_data.add_edited_scene(-1);
	const int b = editor_data.add_edited_scene(-1);
	const int c = editor_data.add_edited_scene(-1);

	editor_data.set_scene_tile(a, 0);
	editor_data.set_scene_tile(b, 1);
	editor_data.set_scene_tile(c, 0);
	editor_data.set_tile_current_scene(0, a);
	editor_data.set_tile_current_scene(1, b);

	const Vector<int> tile_0 = editor_data.get_tile_scene_indices(0);
	const Vector<int> tile_1 = editor_data.get_tile_scene_indices(1);
	REQUIRE(tile_0.size() == 2);
	REQUIRE(tile_1.size() == 1);
	CHECK(tile_0[0] == a);
	CHECK(tile_0[1] == c);
	CHECK(tile_1[0] == b);

	CHECK(editor_data.tile_tab_to_scene_index(0, 0) == a);
	CHECK(editor_data.tile_tab_to_scene_index(0, 1) == c);
	CHECK(editor_data.scene_index_to_tile_tab(c) == 1);
	CHECK(editor_data.scene_index_to_tile_tab(b) == 0);

	// An out-of-range tab resolves to -1 without raising an error.
	ErrorDetector error_detector;
	CHECK(editor_data.tile_tab_to_scene_index(0, 5) == -1);
	CHECK_FALSE(error_detector.has_error);

	editor_data.clear_edited_scenes();
}

TEST_CASE("[SceneTree][Editor] tile-model-cross-move") {
	EditorData editor_data;

	const int a = editor_data.add_edited_scene(-1);
	const int b = editor_data.add_edited_scene(-1);
	editor_data.set_scene_tile(a, 0);
	editor_data.set_scene_tile(b, 0);
	editor_data.set_focused_tile(0);
	editor_data.set_tile_current_scene(0, a);

	// Moving the source tile's current scene into another tile hands the source
	// tile its neighboring scene and adopts the moved scene as the target's current.
	editor_data.set_scene_tile(a, 1);

	CHECK(editor_data.get_tile_scene_indices(0) == Vector<int>{ b });
	CHECK(editor_data.get_tile_scene_indices(1) == Vector<int>{ a });
	CHECK(editor_data.get_tile_current_scene(0) == b);
	CHECK(editor_data.get_tile_current_scene(1) == a);
	// I1: the focused tile (0) now tracks its surviving scene.
	CHECK(editor_data.get_edited_scene() == b);

	editor_data.clear_edited_scenes();
}

TEST_CASE("[SceneTree][Editor] tile-model-remove-fixup") {
	EditorData editor_data;

	const int a = editor_data.add_edited_scene(-1);
	const int b = editor_data.add_edited_scene(-1);
	const int c = editor_data.add_edited_scene(-1);
	editor_data.set_scene_tile(a, 0);
	editor_data.set_scene_tile(b, 1);
	editor_data.set_scene_tile(c, 0);
	editor_data.set_focused_tile(0);
	editor_data.set_tile_current_scene(0, a);
	editor_data.set_tile_current_scene(1, b);

	// Removing the focused tile's current scene falls back to the neighbor and
	// remaps every other tile's stored index across the removal.
	editor_data.remove_scene(a);
	CHECK(editor_data.get_tile_current_scene(0) == 1); // c shifted from index 2 to 1.
	CHECK(editor_data.get_edited_scene() == 1);

	// Removing the last scene of a tile leaves it with no current scene.
	editor_data.set_focused_tile(1);
	editor_data.remove_scene(0); // b was shifted to index 0.
	CHECK(editor_data.get_tile_current_scene(1) == -1);
	CHECK(editor_data.get_edited_scene() == -1);

	editor_data.set_focused_tile(0);
	editor_data.remove_scene(0); // c.
	CHECK(editor_data.get_edited_scene_count() == 0);
}

TEST_CASE("[SceneTree][Editor] tile-model-current-fixup") {
	EditorData editor_data;

	const int a = editor_data.add_edited_scene(-1);
	const int b = editor_data.add_edited_scene(-1);
	const int c = editor_data.add_edited_scene(-1);
	editor_data.set_scene_tile(a, 0);
	editor_data.set_scene_tile(b, 1);
	editor_data.set_scene_tile(c, 1);
	editor_data.set_focused_tile(0);
	editor_data.set_tile_current_scene(0, a);
	editor_data.set_tile_current_scene(1, b);

	// I1: the edited scene tracks the focused tile's current scene.
	CHECK(editor_data.get_edited_scene() == editor_data.get_tile_current_scene(editor_data.get_focused_tile()));

	// Selecting a scene in a non-focused tile (as the save-all and reload loops
	// do while iterating every open scene) must not change that tile's current tab.
	editor_data.set_edited_scene(c);
	CHECK(editor_data.get_tile_current_scene(1) == b);
	CHECK(editor_data.get_tile_current_scene(0) == a);
	editor_data.set_edited_scene(a); // Restore the focused-tile cursor.

	// Switching focus re-points the edited scene to the newly focused tile.
	editor_data.set_focused_tile(1);
	CHECK(editor_data.get_edited_scene() == b);
	CHECK(editor_data.get_edited_scene() == editor_data.get_tile_current_scene(editor_data.get_focused_tile()));

	editor_data.clear_edited_scenes();
}

TEST_CASE("[SceneTree][Editor] context-dual-attach") {
	Window *tree_root = SceneTree::get_singleton()->get_root();
	SubViewportContainer *container_a = memnew(SubViewportContainer);
	SubViewportContainer *container_b = memnew(SubViewportContainer);
	tree_root->add_child(container_a);
	tree_root->add_child(container_b);

	EditorSceneContext *context_a = memnew(EditorSceneContext);
	EditorSceneContext *context_b = memnew(EditorSceneContext);
	Node2D *scene_a = memnew(Node2D);
	Node2D *scene_b = memnew(Node2D);
	Node2D *child_a = memnew(Node2D);
	Node2D *child_b = memnew(Node2D);
	scene_a->add_child(child_a);
	scene_b->add_child(child_b);
	context_a->set_scene_root_node(scene_a);
	context_b->set_scene_root_node(scene_b);

	context_a->set_display_parent(container_a, true);
	context_b->set_display_parent(container_b, false);

	CHECK(context_a->get_viewport()->is_inside_tree());
	CHECK(context_b->get_viewport()->is_inside_tree());
	context_a->get_selection()->add_node(child_a);
	context_b->get_selection()->add_node(child_b);

	// Reparenting the viewport between display containers preserves the selection.
	context_a->set_display_parent(container_b, false);
	CHECK(context_a->get_selection()->is_selected(child_a));

	context_a->set_display_parent(container_a, true);
	CHECK(context_a->get_selection()->is_selected(child_a));

	memdelete(context_a);
	memdelete(context_b);
	tree_root->remove_child(container_a);
	tree_root->remove_child(container_b);
	memdelete(container_a);
	memdelete(container_b);
}

TEST_CASE("[SceneTree][Editor] context-3d-heuristic") {
	EditorSceneContext *context = memnew(EditorSceneContext);
	Node2D *scene_2d = memnew(Node2D);
	context->set_scene_root_node(scene_2d);
	CHECK_FALSE(context->scene_has_3d_content());

	Node3D *scene_3d = memnew(Node3D);
	context->set_scene_root_node(scene_3d);
	memdelete(scene_2d);
	CHECK(context->scene_has_3d_content());

	memdelete(context);
}

TEST_CASE("[SceneTree][Editor] tile-widget-builds-in-tile-docks") {
	WorkspaceFixture fixture;
	ScenePaneTile *tile = fixture.workspace->get_focused_tile();
	REQUIRE(tile);
	CHECK(tile->get_scene_tabs() != nullptr);
	CHECK(tile->get_scene_tree_dock() != nullptr);
	CHECK(tile->get_inspector_dock() != nullptr);
	CHECK(tile->get_content_host() != nullptr);
	// The docks live inside the tile, not in any EditorDockManager slot.
	CHECK(tile->is_ancestor_of(tile->get_scene_tree_dock()));
	CHECK(tile->is_ancestor_of(tile->get_inspector_dock()));
	CHECK(tile->is_ancestor_of(tile->get_content_host()));
}

TEST_CASE("[SceneTree][Editor] workspace-single-tile") {
	WorkspaceFixture fixture;
	CHECK(fixture.workspace->get_tile_count() == 1);
	CHECK(fixture.workspace->get_focused_tile_id() == 0);
	CHECK(fixture.workspace->get_focused_tile()->get_parent() == fixture.workspace);
}

TEST_CASE("[SceneTree][Editor] workspace-split-produces-n-tiles") {
	WorkspaceFixture fixture;
	ScenePaneTile *root_tile = fixture.workspace->get_focused_tile();
	ScenePaneTile *tile_b = fixture.workspace->split_tile(root_tile, false, false);
	fixture.settle();
	REQUIRE(tile_b);
	CHECK(fixture.workspace->get_tile_count() == 2);

	ScenePaneTile *tile_c = fixture.workspace->split_tile(tile_b, true, false);
	fixture.settle();
	REQUIRE(tile_c);
	CHECK(fixture.workspace->get_tile_count() == 3);

	// All tile ids are distinct.
	HashSet<int> ids;
	for (ScenePaneTile *tile : fixture.workspace->get_tiles()) {
		CHECK_FALSE(ids.has(tile->get_tile_id()));
		ids.insert(tile->get_tile_id());
	}
	CHECK(ids.size() == 3);
}

TEST_CASE("[SceneTree][Editor] workspace-collapse-promotes-sibling") {
	WorkspaceFixture fixture;
	ScenePaneTile *root_tile = fixture.workspace->get_focused_tile();
	ScenePaneTile *tile_b = fixture.workspace->split_tile(root_tile, false, false);
	fixture.settle();
	REQUIRE(fixture.workspace->get_tile_count() == 2);

	fixture.workspace->collapse_tile(tile_b);
	fixture.settle();
	CHECK(fixture.workspace->get_tile_count() == 1);
	// The surviving tile is promoted to the workspace's structural child.
	ScenePaneTile *survivor = fixture.workspace->get_tiles()[0];
	CHECK(survivor->get_parent() == fixture.workspace);
}

TEST_CASE("[SceneTree][Editor] workspace-collapse-noop-on-last-tile") {
	WorkspaceFixture fixture;
	fixture.workspace->collapse_tile(fixture.workspace->get_focused_tile());
	fixture.settle();
	CHECK(fixture.workspace->get_tile_count() == 1);
}

TEST_CASE("[SceneTree][Editor] tile-drop-center-moves-scene") {
	WorkspaceFixture fixture;
	EditorData &editor_data = fixture.editor_data;

	ScenePaneTile *tile_a = fixture.workspace->get_focused_tile();
	ScenePaneTile *tile_b = fixture.workspace->split_tile(tile_a, false, false);
	fixture.settle();

	const int a = editor_data.add_edited_scene(-1);
	editor_data.set_scene_tile(a, tile_a->get_tile_id());
	editor_data.set_tile_current_scene(tile_a->get_tile_id(), a);
	const int b = editor_data.add_edited_scene(-1);
	editor_data.set_scene_tile(b, tile_b->get_tile_id());
	editor_data.set_tile_current_scene(tile_b->get_tile_id(), b);

	// Dropping a's only scene into tile_b's center reassigns it and empties
	// tile_a, which is then collapsed.
	ScenePaneTile *dest = fixture.workspace->handle_scene_drop(a, tile_b, EditorSceneWorkspace::DROP_CENTER);
	fixture.settle();
	CHECK(dest == tile_b);
	CHECK(editor_data.get_scene_tile(a) == tile_b->get_tile_id());
	CHECK(fixture.workspace->get_tile_count() == 1);

	editor_data.clear_edited_scenes();
}

TEST_CASE("[SceneTree][Editor] tile-drop-edge-splits-and-moves") {
	WorkspaceFixture fixture;
	EditorData &editor_data = fixture.editor_data;

	ScenePaneTile *tile_a = fixture.workspace->get_focused_tile();
	// Two scenes in the single tile so it survives after one is dragged out.
	const int a = editor_data.add_edited_scene(-1);
	editor_data.set_scene_tile(a, tile_a->get_tile_id());
	editor_data.set_tile_current_scene(tile_a->get_tile_id(), a);
	const int b = editor_data.add_edited_scene(-1);
	editor_data.set_scene_tile(b, tile_a->get_tile_id());

	const int count_before = fixture.workspace->get_tile_count();
	ScenePaneTile *new_tile = fixture.workspace->handle_scene_drop(a, tile_a, EditorSceneWorkspace::DROP_RIGHT);
	fixture.settle();
	REQUIRE(new_tile);
	CHECK(new_tile != tile_a);
	CHECK(fixture.workspace->get_tile_count() == count_before + 1);
	CHECK(editor_data.get_scene_tile(a) == new_tile->get_tile_id());
	CHECK(editor_data.get_tile_current_scene(new_tile->get_tile_id()) == a);

	editor_data.clear_edited_scenes();
}

TEST_CASE("[SceneTree][Editor] workspace-config-round-trip") {
	WorkspaceFixture fixture;
	EditorData &editor_data = fixture.editor_data;

	ScenePaneTile *tile_a = fixture.workspace->get_focused_tile();
	ScenePaneTile *tile_b = fixture.workspace->split_tile(tile_a, false, false);
	ScenePaneTile *tile_c = fixture.workspace->split_tile(tile_b, true, false);
	fixture.settle();
	REQUIRE(fixture.workspace->get_tile_count() == 3);

	const int a = editor_data.add_edited_scene(-1);
	editor_data.set_scene_path(a, "res://a.tscn");
	editor_data.set_scene_tile(a, tile_a->get_tile_id());
	editor_data.set_tile_current_scene(tile_a->get_tile_id(), a);
	const int b = editor_data.add_edited_scene(-1);
	editor_data.set_scene_path(b, "res://b.tscn");
	editor_data.set_scene_tile(b, tile_b->get_tile_id());
	editor_data.set_tile_current_scene(tile_b->get_tile_id(), b);
	const int c = editor_data.add_edited_scene(-1);
	editor_data.set_scene_path(c, "res://c.tscn");
	editor_data.set_scene_tile(c, tile_c->get_tile_id());
	editor_data.set_tile_current_scene(tile_c->get_tile_id(), c);

	const int focused_id = tile_c->get_tile_id();
	editor_data.set_focused_tile(focused_id);
	fixture.workspace->set_focused_tile(focused_id);

	Ref<ConfigFile> config;
	config.instantiate();
	EditorSceneWorkspace::save_to_config(config, editor_data, fixture.workspace);
	CHECK(EditorSceneWorkspace::has_workspace_session(config));

	const PackedStringArray saved_paths = EditorSceneWorkspace::get_saved_scene_paths(config);
	CHECK(saved_paths.has("res://a.tscn"));
	CHECK(saved_paths.has("res://b.tscn"));
	CHECK(saved_paths.has("res://c.tscn"));

	// A fresh workspace with the same scenes pre-loaded rebuilds the same tree.
	Window *tree_root = SceneTree::get_singleton()->get_root();
	EditorSelection *restored_selection = memnew(EditorSelection);
	VBoxContainer *restored_host = memnew(VBoxContainer);
	restored_host->set_anchors_and_offsets_preset(Control::PRESET_FULL_RECT);
	tree_root->add_child(restored_host);

	EditorData restored_data;
	const int ra = restored_data.add_edited_scene(-1);
	restored_data.set_scene_path(ra, "res://a.tscn");
	const int rb = restored_data.add_edited_scene(-1);
	restored_data.set_scene_path(rb, "res://b.tscn");
	const int rc = restored_data.add_edited_scene(-1);
	restored_data.set_scene_path(rc, "res://c.tscn");

	EditorSceneWorkspace *restored = EditorSceneWorkspace::create_single_tile_workspace(restored_selection, &restored_data);
	restored->set_custom_minimum_size(Size2(800, 600));
	restored_host->add_child(restored);
	restored->restore_from_config(config);
	SceneTree::get_singleton()->process(0.016);
	MessageQueue::get_singleton()->flush();

	CHECK(restored->get_tile_count() == 3);
	CHECK(restored->get_focused_tile_id() == focused_id);
	REQUIRE(restored->get_tile_by_id(tile_a->get_tile_id()) != nullptr);
	REQUIRE(restored->get_tile_by_id(tile_b->get_tile_id()) != nullptr);
	REQUIRE(restored->get_tile_by_id(tile_c->get_tile_id()) != nullptr);
	CHECK(restored_data.get_tile_scene_indices(tile_a->get_tile_id()) == Vector<int>{ ra });
	CHECK(restored_data.get_tile_scene_indices(tile_b->get_tile_id()) == Vector<int>{ rb });
	CHECK(restored_data.get_tile_scene_indices(tile_c->get_tile_id()) == Vector<int>{ rc });
	CHECK(restored_data.get_tile_current_scene(focused_id) == rc);

	memdelete(restored);
	tree_root->remove_child(restored_host);
	memdelete(restored_host);
	memdelete(restored_selection);
	editor_data.clear_edited_scenes();
}

} // namespace TestSceneWorkspace
