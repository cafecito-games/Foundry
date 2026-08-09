/**************************************************************************/
/*  test_editor_board_strip.h                                             */
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

#include "editor/editor_board.h"
#include "editor/editor_board_strip.h"
#include "editor/editor_data.h"
#include "editor/editor_scene_pane_tile.h"
#include "editor/editor_scene_workspace.h"

#include "scene/gui/control.h"
#include "scene/main/window.h"

#include "tests/test_macros.h"

namespace TestEditorBoardStrip {

struct BoardStripHarness {
	Control *host = nullptr;
	EditorData editor_data;
	EditorSelection *selection = nullptr;
	EditorBoardStrip *strip = nullptr;

	void mount() {
		host = memnew(Control);
		host->set_custom_minimum_size(Size2(800, 600));
		SceneTree::get_singleton()->get_root()->add_child(host);
		selection = memnew(EditorSelection);
		strip = EditorBoardStrip::create(selection, &editor_data);
		host->add_child(strip);
		strip->set_anchors_and_offsets_preset(Control::PRESET_FULL_RECT);
		host->set_size(Size2(800, 600));
		strip->set_size(Size2(800, 600));
	}

	void pump() {
		SceneTree::get_singleton()->process(0.016);
		MessageQueue::get_singleton()->flush();
	}

	void unmount() {
		SceneTree::get_singleton()->get_root()->remove_child(host);
		memdelete(host);
		memdelete(selection);
	}
};

static HashSet<int> collect_leaf_ids(EditorSceneWorkspace *p_workspace) {
	HashSet<int> ids;
	for (WorkspaceLeafNode *leaf : p_workspace->get_leaves()) {
		ids.insert(leaf->get_leaf_id());
	}
	return ids;
}

TEST_CASE("[Editor][Boards] LocalWorkspaceLeafIdAllocator hands out fresh ids") {
	LocalWorkspaceLeafIdAllocator allocator;

	HashSet<int> seen;
	for (int i = 0; i < 16; i++) {
		const int id = allocator.allocate_leaf_id();
		CHECK_FALSE(seen.has(id));
		seen.insert(id);
	}

	// A restored id must never be handed out again.
	allocator.reserve_leaf_id(100);
	CHECK(allocator.allocate_leaf_id() == 101);
}

TEST_CASE("[Editor][Boards] A fresh strip has exactly one active board") {
	BoardStripHarness h;
	h.mount();
	h.pump();

	CHECK(h.strip->get_board_count() == 1);
	CHECK(h.strip->get_active_index() == 0);
	CHECK(h.strip->get_active_workspace() != nullptr);
	CHECK(h.strip->get_board(0)->is_dormant() == false);
	CHECK(h.strip->get_board(0)->get_workspace() == h.strip->get_active_workspace());

	// The strip resolves its own board's leaves and tiles.
	WorkspaceLeafNode *leaf = h.strip->get_active_workspace()->get_focused_leaf();
	REQUIRE(leaf != nullptr);
	CHECK(h.strip->find_leaf_by_id(leaf->get_leaf_id()) == leaf);
	CHECK(h.strip->find_board_for_leaf(leaf->get_leaf_id()) == h.strip->get_board(0));
	CHECK(h.strip->find_tile_by_id(leaf->get_leaf_id()) == leaf->get_pane_tile());

	h.unmount();
}

TEST_CASE("[Editor][Boards] Boards built by the strip never share a leaf id") {
	BoardStripHarness h;
	h.mount();
	h.pump();

	EditorSceneWorkspace *first = h.strip->get_board(0)->get_workspace();
	// Grow the first board so its ids are not a single value; a shared allocator has
	// to keep the second board clear of all of them, not just of the first one.
	first->split(first->get_focused_leaf(), false, EditorSceneWorkspace::SPLIT_SIDE_SECOND);
	h.pump();

	EditorBoard *second_board = h.strip->add_board("Second");
	REQUIRE(second_board != nullptr);
	h.pump();

	EditorSceneWorkspace *second = second_board->get_workspace();
	REQUIRE(second != nullptr);

	// The second board's first leaf is allocated during construction. If the strip
	// injected its allocator after the fact, that leaf would come from the workspace's
	// own counter and start at 0, silently colliding with the first board.
	WorkspaceLeafNode *second_initial_leaf = second->get_focused_leaf();
	REQUIRE(second_initial_leaf != nullptr);
	CHECK(second_initial_leaf->get_leaf_id() != 0);

	const HashSet<int> first_ids = collect_leaf_ids(first);
	const HashSet<int> second_ids = collect_leaf_ids(second);
	REQUIRE(first_ids.size() == 2);
	REQUIRE(second_ids.size() == 1);
	for (const int id : second_ids) {
		CHECK_FALSE(first_ids.has(id));
	}

	// Leaves created after both boards exist keep clearing every issued id.
	WorkspaceLeafNode *extra = second->split(second_initial_leaf, false, EditorSceneWorkspace::SPLIT_SIDE_SECOND);
	h.pump();
	REQUIRE(extra != nullptr);
	CHECK_FALSE(first_ids.has(extra->get_leaf_id()));
	CHECK(extra->get_leaf_id() != second_initial_leaf->get_leaf_id());

	h.unmount();
}

TEST_CASE("[Editor][Boards] Leaf and tile lookup spans dormant boards") {
	BoardStripHarness h;
	h.mount();
	h.pump();

	EditorBoard *first_board = h.strip->get_board(0);
	EditorBoard *second_board = h.strip->add_board("Second");
	REQUIRE(second_board != nullptr);
	h.pump();

	// Only the active board is awake; the rest are hidden and unprocessed.
	CHECK_FALSE(first_board->is_dormant());
	CHECK(second_board->is_dormant());
	CHECK_FALSE(second_board->is_visible());

	WorkspaceLeafNode *dormant_leaf = second_board->get_workspace()->get_focused_leaf();
	REQUIRE(dormant_leaf != nullptr);

	// Tile ownership in EditorData is editor-wide, so resolution must not stop at the
	// active board: a miss here is what would deactivate another board's scene.
	CHECK(h.strip->find_leaf_by_id(dormant_leaf->get_leaf_id()) == dormant_leaf);
	CHECK(h.strip->find_board_for_leaf(dormant_leaf->get_leaf_id()) == second_board);
	CHECK(h.strip->find_tile_by_id(dormant_leaf->get_leaf_id()) == dormant_leaf->get_pane_tile());
	CHECK(h.strip->get_workspaces().size() == 2);

	h.strip->set_active_index(1);
	h.pump();
	CHECK(h.strip->get_active_workspace() == second_board->get_workspace());
	CHECK(first_board->is_dormant());
	CHECK_FALSE(second_board->is_dormant());
	// The board left behind remembers where the user was.
	CHECK(first_board->get_remembered_focused_leaf_id() == first_board->get_workspace()->get_focused_leaf_id());

	h.unmount();
}

} // namespace TestEditorBoardStrip
