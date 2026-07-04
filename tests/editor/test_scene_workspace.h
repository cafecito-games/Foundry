/**************************************************************************/
/*  test_scene_workspace.h                                                */
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

#include "core/io/config_file.h"
#include "editor/editor_data.h"
#include "editor/editor_scene_context.h"
#include "editor/editor_scene_pane_tile.h"
#include "editor/editor_scene_workspace.h"
#include "editor/scene/editor_scene_tabs.h"

#include "scene/2d/node_2d.h"
#include "scene/3d/node_3d.h"
#include "scene/gui/subviewport_container.h"
#include "scene/main/viewport.h"
#include "scene/main/window.h"

#include "tests/test_macros.h"
#include "tests/test_tools.h"

namespace TestSceneWorkspace {

// W1 — EditorData tile model.

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
	CHECK(editor_data.tile_tab_to_scene_index(1, 0) == b);
	CHECK(editor_data.scene_index_to_tile_tab(a) == 0);
	CHECK(editor_data.scene_index_to_tile_tab(c) == 1);
	CHECK(editor_data.scene_index_to_tile_tab(b) == 0);

	// Out-of-range tabs resolve to -1 without erroring.
	ErrorDetector error_detector;
	CHECK(editor_data.tile_tab_to_scene_index(0, 2) == -1);
	CHECK(editor_data.tile_tab_to_scene_index(1, 1) == -1);
	CHECK(editor_data.tile_tab_to_scene_index(0, -1) == -1);
	CHECK_FALSE(error_detector.has_error);

	// Invariant I1: the focused tile's current is the global current scene.
	CHECK(editor_data.get_edited_scene() == editor_data.get_tile_current_scene(editor_data.get_focused_tile()));

	editor_data.clear_edited_scenes();
}

TEST_CASE("[SceneTree][Editor] tile-model-cross-move") {
	EditorData editor_data;

	const int a = editor_data.add_edited_scene(-1);
	const int b = editor_data.add_edited_scene(-1);
	editor_data.set_tile_current_scene(0, a);

	// Move scene b into tile 7 (tile ids are stable and non-contiguous).
	editor_data.set_scene_tile(b, 7);

	CHECK(editor_data.get_tile_scene_indices(0) == Vector<int>{ a });
	CHECK(editor_data.get_tile_scene_indices(7) == Vector<int>{ b });
	// The destination tile had no current scene, so the moved scene becomes it.
	CHECK(editor_data.get_tile_current_scene(7) == b);
	CHECK(editor_data.get_tile_current_scene(0) == a);
	CHECK(editor_data.get_edited_scene() == editor_data.get_tile_current_scene(editor_data.get_focused_tile()));

	// Moving the source tile's current scene falls back to a neighbour tab.
	editor_data.set_focused_tile(0);
	editor_data.set_tile_current_scene(0, a);
	editor_data.set_scene_tile(a, 7);
	CHECK(editor_data.get_tile_current_scene(0) == -1);
	CHECK(editor_data.get_edited_scene() == -1);
	// Tile 7 already had a current scene (b); it is kept.
	CHECK(editor_data.get_tile_current_scene(7) == b);
	CHECK(editor_data.get_edited_scene() == editor_data.get_tile_current_scene(editor_data.get_focused_tile()));

	editor_data.clear_edited_scenes();
}

TEST_CASE("[SceneTree][Editor] tile-model-remove-fixup") {
	EditorData editor_data;

	const int a = editor_data.add_edited_scene(-1);
	const int b = editor_data.add_edited_scene(-1);
	const int c = editor_data.add_edited_scene(-1);
	editor_data.set_scene_tile(b, 1);
	editor_data.set_focused_tile(0);
	editor_data.set_tile_current_scene(0, a);
	editor_data.set_tile_current_scene(1, b);
	CHECK(editor_data.get_scene_tile(c) == 0);

	// Removing tile 0's current scene falls forward to the next tab in the
	// same tile (c, whose index shifts down after the removal).
	editor_data.remove_scene(a);
	CHECK(editor_data.get_tile_current_scene(0) == 1);
	CHECK(editor_data.get_edited_scene() == 1);
	CHECK(editor_data.get_edited_scene() == editor_data.get_tile_current_scene(editor_data.get_focused_tile()));

	// Removing the only scene of the focused tile leaves it with no current.
	editor_data.set_focused_tile(1);
	editor_data.remove_scene(0); // b, now at index 0.
	CHECK(editor_data.get_tile_current_scene(1) == -1);
	CHECK(editor_data.get_edited_scene() == -1);
	CHECK(editor_data.get_edited_scene() == editor_data.get_tile_current_scene(editor_data.get_focused_tile()));

	editor_data.set_focused_tile(0);
	editor_data.remove_scene(0); // c.
	CHECK(editor_data.get_edited_scene_count() == 0);

	editor_data.clear_edited_scenes();
}

TEST_CASE("[SceneTree][Editor] tile-model-current-fixup") {
	EditorData editor_data;

	const int a = editor_data.add_edited_scene(-1);
	const int b = editor_data.add_edited_scene(-1);
	editor_data.set_focused_tile(0);
	editor_data.set_tile_current_scene(0, a);

	// Moving the focused tile's current scene away repoints the tile at the
	// neighbouring tab and keeps invariant I1 intact.
	editor_data.set_scene_tile(a, 3);
	CHECK(editor_data.get_tile_current_scene(0) == b);
	CHECK(editor_data.get_edited_scene() == b);
	CHECK(editor_data.get_tile_current_scene(3) == a);
	CHECK(editor_data.get_edited_scene() == editor_data.get_tile_current_scene(editor_data.get_focused_tile()));

	// Selecting a scene that lives in a non-focused tile (as save-all and
	// reload-from-disk loops do) must not hijack that tile's visible tab.
	const int c = editor_data.add_edited_scene(-1);
	editor_data.set_tile_current_scene(0, c);
	editor_data.set_edited_scene(a); // a lives in tile 3; tile 3's current stays a, tile 0's stays c.
	CHECK(editor_data.get_edited_scene() == a);
	CHECK(editor_data.get_tile_current_scene(0) == c);
	CHECK(editor_data.get_tile_current_scene(3) == a);

	editor_data.set_edited_scene(c); // Restore the focused-tile cursor.
	editor_data.clear_edited_scenes();
}

// W3 — Per-tile EditorSceneTabs.

TEST_CASE("[SceneTree][Editor] scene-tabs-focused-singleton") {
	EditorSceneTabs *previous_singleton = EditorSceneTabs::get_singleton();
	EditorSceneTabs::set_focused_singleton(nullptr);

	EditorSceneTabs *tabs_a = memnew(EditorSceneTabs(0));
	EditorSceneTabs *tabs_b = memnew(EditorSceneTabs(5));
	CHECK(tabs_a->get_tile_id() == 0);
	CHECK(tabs_b->get_tile_id() == 5);
	// The first constructed instance is the default singleton.
	CHECK(EditorSceneTabs::get_singleton() == tabs_a);

	EditorSceneTabs::set_focused_singleton(tabs_b);
	CHECK(EditorSceneTabs::get_singleton() == tabs_b);

	// Destroying the focused instance resets the singleton so it never
	// dangles; destroying a non-focused instance leaves it alone.
	memdelete(tabs_b);
	CHECK(EditorSceneTabs::get_singleton() == nullptr);
	EditorSceneTabs::set_focused_singleton(tabs_a);
	memdelete(tabs_a);
	CHECK(EditorSceneTabs::get_singleton() == nullptr);

	EditorSceneTabs::set_focused_singleton(previous_singleton);
}

// W2 — EditorSceneContext display + 3D helpers.

TEST_CASE("[SceneTree][Editor] context-dual-attach") {
	Window *tree_root = SceneTree::get_singleton()->get_root();
	SubViewportContainer *container_a = memnew(SubViewportContainer);
	SubViewportContainer *container_b = memnew(SubViewportContainer);
	tree_root->add_child(container_a);
	tree_root->add_child(container_b);

	EditorSceneContext *context = memnew(EditorSceneContext);
	Node2D *scene = memnew(Node2D);
	Node2D *child = memnew(Node2D);
	scene->add_child(child);
	context->set_scene_root_node(scene);

	context->set_display_parent(container_a, true);
	CHECK(context->is_active());
	CHECK(context->get_viewport()->get_parent() == container_a);
	context->get_selection()->add_node(child);

	// Reparent between display containers; the selection survives the move.
	context->set_display_parent(container_b, false);
	CHECK(context->get_viewport()->get_parent() == container_b);
	CHECK(context->get_selection()->is_selected(child));

	// Idempotent when the parent does not change.
	context->set_display_parent(container_b, false);
	CHECK(context->get_viewport()->get_parent() == container_b);
	CHECK(context->get_selection()->is_selected(child));

	context->set_display_parent(container_a, true);
	CHECK(context->get_viewport()->get_parent() == container_a);
	CHECK(context->get_selection()->is_selected(child));

	memdelete(context);
	tree_root->remove_child(container_a);
	tree_root->remove_child(container_b);
	memdelete(container_a);
	memdelete(container_b);
}

TEST_CASE("[SceneTree][Editor] context-exclusive-display-parent-removes-stale-viewports") {
	Window *tree_root = SceneTree::get_singleton()->get_root();

	SubViewportContainer *display_parent = memnew(SubViewportContainer);
	tree_root->add_child(display_parent);
	SubViewport *stale_viewport = memnew(SubViewport);
	display_parent->add_child(stale_viewport);

	EditorSceneContext *context = memnew(EditorSceneContext);
	Node2D *scene = memnew(Node2D);
	context->set_scene_root_node(scene);

	context->set_display_parent(display_parent, true, true);

	CHECK(context->is_active());
	CHECK(context->get_viewport()->get_parent() == display_parent);
	CHECK(stale_viewport->get_parent() == nullptr);
	CHECK(display_parent->get_child_count() == 1);
	CHECK(display_parent->get_child(0) == context->get_viewport());

	memdelete(context);
	memdelete(stale_viewport);
	tree_root->remove_child(display_parent);
	memdelete(display_parent);
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

	// Nested 3D content under a 2D root also counts.
	Node2D *mixed_root = memnew(Node2D);
	Node3D *nested_3d = memnew(Node3D);
	mixed_root->add_child(nested_3d);
	context->set_scene_root_node(mixed_root);
	memdelete(scene_3d);
	CHECK(context->scene_has_3d_content());

	memdelete(context);
}

// W5/W6 — ScenePaneTile + recursive EditorSceneWorkspace.

struct WorkspaceHarness {
	VBoxContainer *host = nullptr;
	EditorSelection *selection = nullptr;
	EditorSceneWorkspace *workspace = nullptr;

	void mount(EditorData *p_data) {
		host = memnew(VBoxContainer);
		host->set_anchors_and_offsets_preset(Control::PRESET_FULL_RECT);
		SceneTree::get_singleton()->get_root()->add_child(host);

		selection = memnew(EditorSelection);
		workspace = EditorSceneWorkspace::create_single_tile_workspace(selection, p_data);
		workspace->set_custom_minimum_size(Size2(800, 600));
		host->add_child(workspace);
		pump();
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

TEST_CASE("[SceneTree][Editor] workspace-single-tile") {
	EditorData editor_data;
	WorkspaceHarness h;
	h.mount(&editor_data);

	CHECK(h.workspace->get_tile_count() == 1);
	CHECK(h.workspace->get_focused_tile_id() == 0);
	ScenePaneTile *tile = h.workspace->get_focused_tile();
	REQUIRE(tile != nullptr);

	// The tile is self-contained: strip, scene tree, content host, and
	// inspector are all non-null descendants of the tile (in-tile docks, not
	// EditorDockManager slots).
	REQUIRE(tile->get_scene_tabs() != nullptr);
	REQUIRE(tile->get_scene_tree_dock() != nullptr);
	REQUIRE(tile->get_inspector_dock() != nullptr);
	REQUIRE(tile->get_content_host() != nullptr);
	CHECK(tile->is_ancestor_of(tile->get_scene_tabs()));
	CHECK(tile->is_ancestor_of(tile->get_scene_tree_dock()));
	CHECK(tile->is_ancestor_of(tile->get_inspector_dock()));
	CHECK(tile->is_ancestor_of(tile->get_content_host()));
	CHECK(tile->get_scene_tabs()->get_tile_id() == tile->get_tile_id());

	h.unmount();
}

TEST_CASE("[SceneTree][Editor] workspace-split-produces-n-tiles") {
	EditorData editor_data;
	WorkspaceHarness h;
	h.mount(&editor_data);

	ScenePaneTile *first = h.workspace->get_focused_tile();
	ScenePaneTile *second = h.workspace->split_tile(first, false, false);
	h.pump();
	REQUIRE(second != nullptr);
	CHECK(h.workspace->get_tile_count() == 2);

	ScenePaneTile *third = h.workspace->split_tile(second, true, true);
	h.pump();
	REQUIRE(third != nullptr);
	CHECK(h.workspace->get_tile_count() == 3);

	// All tile ids are distinct and stable.
	HashSet<int> ids;
	for (ScenePaneTile *tile : h.workspace->get_tiles()) {
		ids.insert(tile->get_tile_id());
	}
	CHECK(ids.size() == 3);
	CHECK(h.workspace->get_tile_by_id(first->get_tile_id()) == first);
	CHECK(h.workspace->get_tile_by_id(second->get_tile_id()) == second);
	CHECK(h.workspace->get_tile_by_id(third->get_tile_id()) == third);

	// Tiles have real, non-degenerate layout after a pump.
	CHECK(first->get_size().x > 100);
	CHECK(second->get_size().x > 100);

	h.unmount();
}

TEST_CASE("[SceneTree][Editor] workspace-collapse-promotes-sibling") {
	EditorData editor_data;
	WorkspaceHarness h;
	h.mount(&editor_data);

	ScenePaneTile *first = h.workspace->get_focused_tile();
	ScenePaneTile *second = h.workspace->split_tile(first, false, false);
	h.pump();
	REQUIRE(h.workspace->get_tile_count() == 2);

	h.workspace->collapse_tile(second);
	h.pump();

	CHECK(h.workspace->get_tile_count() == 1);
	// The surviving tile is promoted back to the workspace's structural child.
	CHECK(first->get_parent() == h.workspace);
	CHECK(h.workspace->get_focused_tile() == first);

	h.unmount();
}

TEST_CASE("[SceneTree][Editor] workspace-collapse-refocuses-on-focused-tile-removal") {
	EditorData editor_data;
	WorkspaceHarness h;
	h.mount(&editor_data);

	ScenePaneTile *first = h.workspace->get_focused_tile();
	ScenePaneTile *second = h.workspace->split_tile(first, false, false);
	h.pump();
	h.workspace->set_focused_tile(second->get_tile_id());

	SIGNAL_WATCH(h.workspace, "tile_focus_requested");
	h.workspace->collapse_tile(second);
	h.pump();

	CHECK(h.workspace->get_tile_count() == 1);
	CHECK(h.workspace->get_focused_tile_id() == first->get_tile_id());
	Array expected_emission;
	expected_emission.push_back(first->get_tile_id());
	Array expected;
	expected.push_back(expected_emission);
	SIGNAL_CHECK("tile_focus_requested", expected);
	SIGNAL_UNWATCH(h.workspace, "tile_focus_requested");

	h.unmount();
}

TEST_CASE("[SceneTree][Editor] workspace-collapse-noop-on-last-tile") {
	EditorData editor_data;
	WorkspaceHarness h;
	h.mount(&editor_data);

	ScenePaneTile *only = h.workspace->get_focused_tile();
	h.workspace->collapse_tile(only);
	h.pump();

	CHECK(h.workspace->get_tile_count() == 1);
	CHECK(h.workspace->get_focused_tile() == only);

	h.unmount();
}

// W8 — Drop resolution (drag a scene tab to a tile's center or edge).

TEST_CASE("[SceneTree][Editor] tile-drop-center-moves-scene") {
	EditorData editor_data;
	WorkspaceHarness h;
	h.mount(&editor_data);

	ScenePaneTile *first = h.workspace->get_focused_tile();
	ScenePaneTile *second = h.workspace->split_tile(first, false, false);
	h.pump();

	const int a = editor_data.add_edited_scene(-1);
	const int b = editor_data.add_edited_scene(-1);
	editor_data.set_scene_tile(b, second->get_tile_id());
	editor_data.set_tile_current_scene(0, a);

	// Center drop of the source tile's only scene onto the other tile: the
	// scene moves and the emptied source tile collapses.
	const int second_id = second->get_tile_id();
	CHECK(h.workspace->perform_tab_drop(second_id, 0, first->get_tile_id(), EditorSceneWorkspace::DROP_REGION_CENTER));
	h.pump();

	CHECK(editor_data.get_scene_tile(b) == first->get_tile_id());
	CHECK(editor_data.get_tile_scene_indices(first->get_tile_id()).size() == 2);
	CHECK(h.workspace->get_tile_count() == 1);
	CHECK(h.workspace->get_tile_by_id(second_id) == nullptr);

	// A center drop back onto the scene's own tile resolves to nothing.
	CHECK_FALSE(h.workspace->perform_tab_drop(first->get_tile_id(), 0, first->get_tile_id(), EditorSceneWorkspace::DROP_REGION_CENTER));

	editor_data.clear_edited_scenes();
	h.unmount();
}

TEST_CASE("[SceneTree][Editor] tile-drop-edge-splits-and-moves") {
	EditorData editor_data;
	WorkspaceHarness h;
	h.mount(&editor_data);

	ScenePaneTile *first = h.workspace->get_focused_tile();
	const int a = editor_data.add_edited_scene(-1);
	const int b = editor_data.add_edited_scene(-1);
	editor_data.set_tile_current_scene(0, a);

	// Edge drop splits the target tile and the new tile receives the scene.
	SIGNAL_WATCH(h.workspace, "tile_drop_completed");
	CHECK(h.workspace->perform_tab_drop(first->get_tile_id(), 1, first->get_tile_id(), EditorSceneWorkspace::DROP_REGION_RIGHT));
	h.pump();

	CHECK(h.workspace->get_tile_count() == 2);
	ScenePaneTile *new_tile = nullptr;
	for (ScenePaneTile *tile : h.workspace->get_tiles()) {
		if (tile != first) {
			new_tile = tile;
		}
	}
	REQUIRE(new_tile != nullptr);
	CHECK(editor_data.get_scene_tile(b) == new_tile->get_tile_id());
	// The moved scene is the new tile's current scene.
	CHECK(editor_data.get_tile_current_scene(new_tile->get_tile_id()) == b);
	CHECK(editor_data.get_tile_current_scene(first->get_tile_id()) == a);
	CHECK(h.workspace->get_focused_tile_id() == new_tile->get_tile_id());

	Array expected_emission;
	expected_emission.push_back(new_tile->get_tile_id());
	Array expected;
	expected.push_back(expected_emission);
	SIGNAL_CHECK("tile_drop_completed", expected);
	SIGNAL_UNWATCH(h.workspace, "tile_drop_completed");

	editor_data.clear_edited_scenes();
	h.unmount();
}

// W9 — Nested-tree persistence.

TEST_CASE("[SceneTree][Editor] workspace-config-round-trip") {
	EditorData editor_data;
	WorkspaceHarness h;
	h.mount(&editor_data);

	// Build a 3-tile tree (a split of a split) with scenes spread over it.
	ScenePaneTile *first = h.workspace->get_focused_tile();
	ScenePaneTile *second = h.workspace->split_tile(first, false, false);
	h.pump();
	ScenePaneTile *third = h.workspace->split_tile(second, true, false);
	h.pump();
	REQUIRE(h.workspace->get_tile_count() == 3);

	const int a = editor_data.add_edited_scene(-1);
	const int b = editor_data.add_edited_scene(-1);
	const int c = editor_data.add_edited_scene(-1);
	editor_data.set_scene_path(a, "res://tile_a.tscn");
	editor_data.set_scene_path(b, "res://tile_b.tscn");
	editor_data.set_scene_path(c, "res://tile_c.tscn");
	editor_data.set_scene_tile(b, second->get_tile_id());
	editor_data.set_scene_tile(c, third->get_tile_id());
	editor_data.set_tile_current_scene(first->get_tile_id(), a);
	editor_data.set_focused_tile(second->get_tile_id());
	h.workspace->set_focused_tile(second->get_tile_id());

	Ref<ConfigFile> config;
	config.instantiate();
	EditorSceneWorkspace::save_to_config(config, editor_data, h.workspace);

	// Every key reads back.
	CHECK(int(config->get_value("Workspace", "node_count")) == 5); // 2 splits + 3 leaves.
	CHECK(int(config->get_value("Workspace", "focused_tile_id")) == second->get_tile_id());
	CHECK(config->has_section_key("Workspace", "root_node"));
	CHECK(EditorSceneWorkspace::has_workspace_session(config));

	const Vector<int> expected_tile_ids = { first->get_tile_id(), second->get_tile_id(), third->get_tile_id() };
	const int saved_focus = second->get_tile_id();

	// A fresh workspace restored from the config reproduces the same tree.
	h.unmount();

	// Reset scene->tile assignments to the default tile, as after a plain
	// session restore that loads every scene before the tree is rebuilt.
	editor_data.set_focused_tile(0);
	editor_data.set_scene_tile(a, 0);
	editor_data.set_scene_tile(b, 0);
	editor_data.set_scene_tile(c, 0);

	WorkspaceHarness h2;
	h2.mount(&editor_data);
	h2.workspace->restore_from_config(config);
	h2.pump();

	CHECK(h2.workspace->get_tile_count() == 3);
	for (int tile_id : expected_tile_ids) {
		CHECK(h2.workspace->get_tile_by_id(tile_id) != nullptr);
	}
	CHECK(h2.workspace->get_focused_tile_id() == saved_focus);
	CHECK(editor_data.get_focused_tile() == saved_focus);
	CHECK(editor_data.get_scene_tile(a) == expected_tile_ids[0]);
	CHECK(editor_data.get_scene_tile(b) == expected_tile_ids[1]);
	CHECK(editor_data.get_scene_tile(c) == expected_tile_ids[2]);
	CHECK(editor_data.get_tile_current_scene(expected_tile_ids[0]) == a);
	CHECK(editor_data.get_tile_current_scene(expected_tile_ids[1]) == b);
	CHECK(editor_data.get_tile_current_scene(expected_tile_ids[2]) == c);
	// New tiles allocated after a restore never reuse restored ids.
	ScenePaneTile *fourth = h2.workspace->split_tile(h2.workspace->get_focused_tile(), false, false);
	h2.pump();
	for (int tile_id : expected_tile_ids) {
		CHECK(fourth->get_tile_id() != tile_id);
	}

	editor_data.clear_edited_scenes();
	h2.unmount();
}

} // namespace TestSceneWorkspace
