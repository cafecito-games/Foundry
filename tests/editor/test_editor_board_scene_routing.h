/**************************************************************************/
/*  test_editor_board_scene_routing.h                                     */
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
#include "editor/editor_scene_workspace.h"

#include "scene/gui/control.h"
#include "scene/main/window.h"

#include "tests/test_macros.h"

// Opening a scene files it under EditorData's single editor-wide focused tile id.
// That id used to move only when a workspace raised a focus change, and a board
// switch usually raises none -- the incoming board's workspace already holds the
// leaf it means to focus -- so the id stayed on the board being left and every
// scene opened afterwards landed in the first board's tile. These tests drive the
// real routing path: switch boards through the strip, then open a scene through
// EditorData exactly as the editor's scene load does, and check which tile owns it.
namespace TestEditorBoardSceneRouting {

struct SceneRoutingHarness {
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

	void pump(double p_delta = 0.016) {
		SceneTree::get_singleton()->process(p_delta);
		MessageQueue::get_singleton()->flush();
	}

	// Well past the switch's ease-out, so the slide settles and the outgoing board sleeps.
	void settle_transition() {
		pump(1.0);
	}

	// The tile a board would file a newly opened scene under.
	int focused_tile_of(int p_board_index) const {
		EditorBoard *board = strip->get_board(p_board_index);
		if (!board || !board->get_workspace()) {
			return -1;
		}
		return board->get_workspace()->get_effective_focused_tile_id();
	}

	void unmount() {
		SceneTree::get_singleton()->get_root()->remove_child(host);
		memdelete(host);
		memdelete(selection);
	}
};

TEST_CASE("[Editor][Boards] A scene opened while board 2 is active lands in board 2's tile") {
	SceneRoutingHarness h;
	h.mount();
	h.pump();

	const int first_tile = h.focused_tile_of(0);
	REQUIRE(first_tile >= 0);
	const int first_scene = h.editor_data.add_edited_scene(-1);
	CHECK(h.editor_data.get_scene_tile(first_scene) == first_tile);

	EditorBoard *second = h.strip->add_board("Second");
	REQUIRE(second != nullptr);
	h.pump();
	h.strip->set_active_board(1);
	h.settle_transition();

	const int second_tile = h.focused_tile_of(1);
	REQUIRE(second_tile >= 0);
	CHECK(second_tile != first_tile);

	const int second_scene = h.editor_data.add_edited_scene(-1);
	CHECK(h.editor_data.get_scene_tile(second_scene) == second_tile);
	CHECK(h.editor_data.get_tile_scene_indices(first_tile).size() == 1);
	CHECK(h.editor_data.get_tile_scene_indices(second_tile).size() == 1);

	// A third board is not a special case of the second: each switch re-points the
	// editor-wide id at whichever board is now active.
	EditorBoard *third = h.strip->add_board("Third");
	REQUIRE(third != nullptr);
	h.pump();
	h.strip->set_active_board(2);
	h.settle_transition();

	const int third_tile = h.focused_tile_of(2);
	REQUIRE(third_tile >= 0);
	const int third_scene = h.editor_data.add_edited_scene(-1);
	CHECK(h.editor_data.get_scene_tile(third_scene) == third_tile);
	CHECK(h.editor_data.get_tile_scene_indices(first_tile).size() == 1);
	CHECK(h.editor_data.get_tile_scene_indices(second_tile).size() == 1);
	CHECK(h.editor_data.get_tile_scene_indices(third_tile).size() == 1);

	h.unmount();
}

TEST_CASE("[Editor][Boards] The editor-wide focused tile follows the active board") {
	SceneRoutingHarness h;
	h.mount();
	h.pump();

	const int first_tile = h.focused_tile_of(0);
	REQUIRE(first_tile >= 0);
	CHECK(h.editor_data.get_focused_tile_id() == first_tile);

	REQUIRE(h.strip->add_board("Second") != nullptr);
	h.pump();

	// Merely adding a board leaves the editor pointed at the board still on screen.
	CHECK(h.editor_data.get_focused_tile_id() == first_tile);

	h.strip->set_active_board(1);
	const int second_tile = h.focused_tile_of(1);
	REQUIRE(second_tile >= 0);
	// Committed at the start of the slide, not once it settles: a scene opened while the
	// boards are still sliding must already belong to the incoming board.
	CHECK(h.editor_data.get_focused_tile_id() == second_tile);
	h.settle_transition();
	CHECK(h.editor_data.get_focused_tile_id() == second_tile);

	h.strip->set_active_board(0);
	h.settle_transition();
	CHECK(h.editor_data.get_focused_tile_id() == first_tile);

	h.unmount();
}

TEST_CASE("[Editor][Boards] Switching to a split board targets that board's focused tile") {
	SceneRoutingHarness h;
	h.mount();
	h.pump();

	const int first_tile = h.focused_tile_of(0);
	REQUIRE(first_tile >= 0);

	EditorBoard *second = h.strip->add_board("Second");
	REQUIRE(second != nullptr);
	if (!second) {
		h.unmount();
		return;
	}
	EditorSceneWorkspace *second_workspace = second->get_workspace();
	REQUIRE(second_workspace != nullptr);
	if (!second_workspace) {
		h.unmount();
		return;
	}
	h.pump();

	WorkspaceLeafNode *original = second_workspace->get_focused_leaf();
	REQUIRE(original != nullptr);
	if (!original) {
		h.unmount();
		return;
	}
	WorkspaceLeafNode *added = second_workspace->split(original, false, EditorSceneWorkspace::SPLIT_SIDE_SECOND);
	REQUIRE(added != nullptr);
	if (!added) {
		h.unmount();
		return;
	}
	second_workspace->set_focused_leaf(added->get_leaf_id());
	h.pump();

	h.strip->set_active_board(1);
	h.settle_transition();

	CHECK(h.editor_data.get_focused_tile_id() == added->get_leaf_id());
	const int scene_index = h.editor_data.add_edited_scene(-1);
	CHECK(h.editor_data.get_scene_tile(scene_index) == added->get_leaf_id());
	CHECK(h.editor_data.get_tile_scene_indices(first_tile).is_empty());

	h.unmount();
}

TEST_CASE("[Editor][Boards] Scene routing with a single board is unchanged") {
	SceneRoutingHarness h;
	h.mount();
	h.pump();

	REQUIRE(h.strip->get_board_count() == 1);
	const int only_tile = h.focused_tile_of(0);
	REQUIRE(only_tile >= 0);
	CHECK(h.editor_data.get_focused_tile_id() == only_tile);

	// Re-activating the board already on screen is a no-op, including for the focus it
	// would otherwise re-derive.
	h.strip->set_active_board(0);
	h.pump();
	CHECK(h.editor_data.get_focused_tile_id() == only_tile);

	const int scene_a = h.editor_data.add_edited_scene(-1);
	CHECK(h.editor_data.get_scene_tile(scene_a) == only_tile);

	// A scene opened right after the no-op reactivation above must still land on the
	// one board's tile: if the sync ever mis-derived the editor-wide id in the
	// single-board case, this scene would be filed under a stale or invalid tile
	// instead of the board's own.
	const int scene_b = h.editor_data.add_edited_scene(-1);
	CHECK(h.editor_data.get_scene_tile(scene_b) == only_tile);
	CHECK(h.editor_data.get_tile_scene_indices(only_tile).size() == 2);

	h.unmount();
}

} // namespace TestEditorBoardSceneRouting
