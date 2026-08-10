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

#include "core/input/input_event.h"
#include "core/io/config_file.h"
#include "scene/gui/button.h"
#include "scene/gui/control.h"
#include "scene/main/window.h"
#include "scene/scene_string_names.h"

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

	void pump(double p_delta = 0.016) {
		SceneTree::get_singleton()->process(p_delta);
		MessageQueue::get_singleton()->flush();
	}

	// Advances well past a switch's transition duration so its ease-out settles and the
	// outgoing board sleeps, without a test hard-coding that duration itself.
	void settle_transition() {
		pump(1.0);
	}

	void unmount() {
		SceneTree::get_singleton()->get_root()->remove_child(host);
		memdelete(host);
		memdelete(selection);
	}
};

// Stands in for EditorNode's board scene-close handler. The strip never discards edited
// scenes itself; it asks, and the editor answers once its per-scene prompts are done.
struct CloseHandlerRecord {
	int board_index = -1;
	PackedInt32Array scene_indices;
	int call_count = 0;
	bool allow = true;

	void reset(bool p_allow) {
		board_index = -1;
		scene_indices.clear();
		call_count = 0;
		allow = p_allow;
	}
};

static CloseHandlerRecord close_handler_record;

static bool record_board_close(int p_board_index, const PackedInt32Array &p_scene_indices) {
	close_handler_record.board_index = p_board_index;
	close_handler_record.scene_indices = p_scene_indices;
	close_handler_record.call_count++;
	return close_handler_record.allow;
}

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

	h.strip->set_active_board(1);
	h.pump();
	CHECK(h.strip->get_active_workspace() == second_board->get_workspace());
	// The outgoing board stays awake for the duration of the slide; it only goes dormant
	// once the switch settles.
	CHECK_FALSE(first_board->is_dormant());
	CHECK_FALSE(second_board->is_dormant());
	// The board left behind remembers where the user was.
	CHECK(first_board->get_remembered_focused_leaf_id() == first_board->get_workspace()->get_focused_leaf_id());

	h.settle_transition();
	CHECK(first_board->is_dormant());
	CHECK_FALSE(second_board->is_dormant());

	h.unmount();
}

TEST_CASE("[Editor][Boards] Boards can be added, activated, and closed") {
	BoardStripHarness h;
	h.mount();
	h.pump();

	SIGNAL_WATCH(h.strip, "board_added");
	SIGNAL_WATCH(h.strip, "active_board_changed");
	SIGNAL_WATCH(h.strip, "board_removed");

	EditorBoard *second = h.strip->add_board();
	REQUIRE(second != nullptr);
	h.pump();
	CHECK(h.strip->get_board_count() == 2);
	CHECK(second->get_workspace()->get_leaf_count() == 1);
	CHECK(h.strip->get_board_index(second) == 1);
	SIGNAL_CHECK("board_added", { { 1 } });

	// #1970 locked decision 5: leaf ids are editor-wide, so two boards never share one.
	const int leaf_a = h.strip->get_board(0)->get_workspace()->get_focused_leaf()->get_leaf_id();
	const int leaf_b = second->get_workspace()->get_focused_leaf()->get_leaf_id();
	CHECK(leaf_a != leaf_b);

	h.strip->set_active_board(1);
	h.pump();
	CHECK(h.strip->get_active_index() == 1);
	// Both halves of the switch stay awake for the duration of the slide.
	CHECK_FALSE(h.strip->get_board(1)->is_dormant());
	CHECK_FALSE(h.strip->get_board(0)->is_dormant());
	SIGNAL_CHECK("active_board_changed", { { 1 } });

	h.settle_transition();
	CHECK_FALSE(h.strip->get_board(1)->is_dormant());
	CHECK(h.strip->get_board(0)->is_dormant());

	// Re-activating the board already on screen is not a switch and must stay silent.
	h.strip->set_active_board(1);
	h.pump();
	SIGNAL_CHECK_FALSE("active_board_changed");

	// Closing the visible board hands the screen to a neighbour rather than leaving the
	// editor with no live board.
	CHECK(h.strip->close_board(1));
	h.pump();
	CHECK(h.strip->get_board_count() == 1);
	CHECK(h.strip->get_active_index() == 0);
	CHECK_FALSE(h.strip->get_board(0)->is_dormant());
	SIGNAL_CHECK("board_removed", { { 1 } });
	SIGNAL_CHECK("active_board_changed", { { 0 } });

	SIGNAL_UNWATCH(h.strip, "board_added");
	SIGNAL_UNWATCH(h.strip, "active_board_changed");
	SIGNAL_UNWATCH(h.strip, "board_removed");
	h.unmount();
}

TEST_CASE("[Editor][Boards] The last board cannot be closed") {
	BoardStripHarness h;
	h.mount();
	h.pump();

	SIGNAL_WATCH(h.strip, "board_removed");

	CHECK(h.strip->get_board_count() == 1);
	CHECK_FALSE(h.strip->close_board(0));
	CHECK(h.strip->get_board_count() == 1);
	CHECK(h.strip->get_active_board() != nullptr);
	SIGNAL_CHECK_FALSE("board_removed");

	SIGNAL_UNWATCH(h.strip, "board_removed");
	h.unmount();
}

TEST_CASE("[Editor][Boards] Closing a board with scenes goes through the scene-close handler") {
	BoardStripHarness h;
	h.mount();
	h.pump();

	EditorBoard *second = h.strip->add_board("Second");
	REQUIRE(second != nullptr);
	h.pump();

	const int second_leaf = second->get_workspace()->get_focused_leaf()->get_leaf_id();
	h.editor_data.register_tile(second_leaf);
	h.editor_data.set_focused_tile_id(second_leaf);
	const int scene_index = h.editor_data.add_edited_scene(-1);
	REQUIRE(h.editor_data.get_scene_tile(scene_index) == second_leaf);

	SIGNAL_WATCH(h.strip, "board_removed");

	// With no handler installed the strip refuses rather than discarding the scene.
	CHECK_FALSE(h.strip->close_board(1));
	CHECK(h.strip->get_board_count() == 2);
	SIGNAL_CHECK_FALSE("board_removed");

	// A handler that declines -- the editor's answer while unsaved-changes prompts are
	// still on screen, and its final answer when one of them is cancelled -- leaves the
	// board and its scene intact, and emits nothing: a deferred or cancelled close must
	// be silent so listeners (like EditorBoardSwitcher) never rebuild around a board that
	// is still there.
	h.strip->set_board_scene_close_handler(callable_mp_static(&record_board_close));
	close_handler_record.reset(false);
	CHECK_FALSE(h.strip->close_board(1));
	CHECK(close_handler_record.call_count == 1);
	// The handler is told which board is closing and every scene index it owns.
	CHECK(close_handler_record.board_index == 1);
	REQUIRE(close_handler_record.scene_indices.size() == 1);
	CHECK(close_handler_record.scene_indices[0] == scene_index);
	CHECK(h.strip->get_board_count() == 2);
	CHECK(h.editor_data.get_edited_scene_count() == 1);
	CHECK(h.editor_data.get_scene_tile(scene_index) == second_leaf);
	SIGNAL_CHECK_FALSE("board_removed");

	// Once the editor reports every scene dealt with, the board goes.
	close_handler_record.reset(true);
	CHECK(h.strip->close_board(1));
	CHECK(close_handler_record.call_count == 1);
	CHECK(h.strip->get_board_count() == 1);
	SIGNAL_CHECK("board_removed", { { 1 } });

	SIGNAL_UNWATCH(h.strip, "board_removed");
	h.unmount();
}

TEST_CASE("[Editor][Boards] Closing a non-active board shifts trailing indices without reactivating") {
	BoardStripHarness h;
	h.mount();
	h.pump();

	EditorBoard *board_a = h.strip->get_board(0);
	EditorBoard *board_b = h.strip->add_board("B");
	REQUIRE(board_b != nullptr);
	h.pump();
	EditorBoard *board_c = h.strip->add_board("C");
	REQUIRE(board_c != nullptr);
	h.pump();

	h.strip->set_active_board(2);
	h.pump();
	REQUIRE(h.strip->get_active_index() == 2);
	REQUIRE(h.strip->get_active_board() == board_c);

	SIGNAL_WATCH(h.strip, "active_board_changed");
	SIGNAL_WATCH(h.strip, "board_removed");

	// Board B sits before the active board (C) but is not itself active: closing it must
	// shift C's index down without touching which board is on screen or emitting
	// active_board_changed, since the visible board never moved.
	CHECK(h.strip->close_board(1));
	h.pump();

	CHECK(h.strip->get_board_count() == 2);
	CHECK(h.strip->get_board_index(board_a) == 0);
	CHECK(h.strip->get_board_index(board_c) == 1);
	CHECK(h.strip->get_active_index() == 1);
	CHECK(h.strip->get_active_board() == board_c);
	CHECK_FALSE(board_c->is_dormant());
	SIGNAL_CHECK("board_removed", { { 1 } });
	SIGNAL_CHECK_FALSE("active_board_changed");

	SIGNAL_UNWATCH(h.strip, "active_board_changed");
	SIGNAL_UNWATCH(h.strip, "board_removed");
	h.unmount();
}

TEST_CASE("[Editor][Boards] A pending board close survives an interleaved unrelated close") {
	// Reproduces issue #1975: a caller that defers a board close across an asynchronous
	// gap (EditorNode waits on unsaved-changes prompts) must not trust a board index
	// captured before the gap, because an unrelated close in the meantime shifts every
	// index after it.
	BoardStripHarness h;
	h.mount();
	h.pump();

	EditorBoard *board_a = h.strip->get_board(0);
	EditorBoard *board_b = h.strip->add_board("B");
	REQUIRE(board_b != nullptr);
	h.pump();
	EditorBoard *board_c = h.strip->add_board("C");
	REQUIRE(board_c != nullptr);
	h.pump();

	REQUIRE(h.strip->get_board_index(board_a) == 0);
	REQUIRE(h.strip->get_board_index(board_b) == 1);
	REQUIRE(h.strip->get_board_index(board_c) == 2);

	const int leaf_b = board_b->get_workspace()->get_focused_leaf()->get_leaf_id();
	h.editor_data.register_tile(leaf_b);
	h.editor_data.set_focused_tile_id(leaf_b);
	const int scene_index = h.editor_data.add_edited_scene(-1);
	REQUIRE(h.editor_data.get_scene_tile(scene_index) == leaf_b);

	h.strip->set_board_scene_close_handler(callable_mp_static(&record_board_close));

	// The handler declines once, standing in for the asynchronous unsaved-changes prompt
	// that keeps B alive while the editor waits on the user.
	close_handler_record.reset(false);
	CHECK_FALSE(h.strip->close_board(1));
	CHECK(close_handler_record.call_count == 1);
	CHECK(h.strip->get_board_count() == 3);

	// Capture B's identity the way a deferred caller must: by instance id, not by the
	// index the handler was called with.
	const ObjectID pending_board_id = board_b->get_instance_id();
	const int stale_index = close_handler_record.board_index;
	REQUIRE(stale_index == 1);

	// An unrelated, scene-less board closes in between and shifts every later index.
	CHECK(h.strip->close_board(0));
	h.pump();
	CHECK(h.strip->get_board_count() == 2);

	// The stale index now names a different board than the one that was actually asked
	// to close.
	CHECK(h.strip->get_board_index(board_b) != stale_index);
	CHECK(h.strip->get_board(stale_index) == board_c);

	// Re-resolving by identity finds B at its new position instead of trusting the stale
	// index.
	const int resolved_index = h.strip->resolve_board_index(pending_board_id);
	CHECK(resolved_index == h.strip->get_board_index(board_b));
	CHECK(resolved_index != stale_index);

	// Finishing the close at the resolved index removes B and leaves C untouched.
	SIGNAL_WATCH(h.strip, "board_removed");
	close_handler_record.reset(true);
	CHECK(h.strip->close_board(resolved_index));
	h.pump();
	SIGNAL_CHECK("board_removed", { { resolved_index } });
	SIGNAL_UNWATCH(h.strip, "board_removed");

	CHECK(h.strip->get_board_count() == 1);
	CHECK(h.strip->get_board(0) == board_c);

	// A board id that no longer exists resolves to -1 rather than aliasing whatever now
	// occupies its old index.
	CHECK(h.strip->resolve_board_index(pending_board_id) == -1);

	h.unmount();
}

TEST_CASE("[Editor][Boards] Activating a board restores its remembered focus") {
	BoardStripHarness h;
	h.mount();
	h.pump();

	EditorBoard *second = h.strip->add_board("Second");
	REQUIRE(second != nullptr);
	EditorSceneWorkspace *workspace = second->get_workspace();
	REQUIRE(workspace != nullptr);
	WorkspaceLeafNode *initial_leaf = workspace->get_focused_leaf();
	REQUIRE(initial_leaf != nullptr);
	WorkspaceLeafNode *split_leaf = workspace->split(initial_leaf, false, EditorSceneWorkspace::SPLIT_SIDE_SECOND);
	REQUIRE(split_leaf != nullptr);
	h.pump();

	// The strip only requests focus; applying it is the editor's job, so stand in for
	// EditorNode's leaf_focus_requested handler.
	workspace->connect("leaf_focus_requested", callable_mp(workspace, &EditorSceneWorkspace::set_focused_leaf));

	h.strip->set_active_board(1);
	workspace->set_focused_leaf(split_leaf->get_leaf_id());
	h.pump();

	h.strip->set_active_board(0);
	h.pump();
	CHECK(second->get_remembered_focused_leaf_id() == split_leaf->get_leaf_id());

	// A scene closing on the dormant board can move its focus while the user is away;
	// coming back must land on the leaf the user left, not on wherever it drifted.
	workspace->set_focused_leaf(initial_leaf->get_leaf_id());

	h.strip->set_active_board(1);
	h.pump();
	CHECK(workspace->get_focused_leaf_id() == split_leaf->get_leaf_id());

	h.unmount();
}

TEST_CASE("[Editor][Boards] Editor-wide activation is rejected for leaves on dormant boards") {
	BoardStripHarness h;
	h.mount();
	h.pump();

	EditorBoard *first = h.strip->get_board(0);
	EditorBoard *second = h.strip->add_board("Second");
	REQUIRE(second != nullptr);
	h.pump();

	const int active_leaf = first->get_workspace()->get_focused_leaf()->get_leaf_id();
	const int dormant_leaf = second->get_workspace()->get_focused_leaf()->get_leaf_id();

	// A dormant board's pane replays its persisted active tab after the restore bracket
	// has closed. Honouring that would drag editor-wide focus and the edited scene onto
	// a board the user cannot see, which is silent: nothing fails, the wrong board wins.
	CHECK(h.strip->is_leaf_on_active_board(active_leaf));
	CHECK_FALSE(h.strip->is_leaf_on_active_board(dormant_leaf));

	h.strip->set_active_board(1);
	h.pump();
	CHECK_FALSE(h.strip->is_leaf_on_active_board(active_leaf));
	CHECK(h.strip->is_leaf_on_active_board(dormant_leaf));

	// A leaf that belongs to no board at all is not a cross-board conflict, so it is not
	// rejected; callers keep their own null handling.
	CHECK(h.strip->is_leaf_on_active_board(9999));

	h.unmount();
}

TEST_CASE("[Editor][Boards] A scene open on a dormant board is revealed where it lives") {
	BoardStripHarness h;
	h.mount();
	h.pump();

	EditorBoard *second = h.strip->add_board("Second");
	REQUIRE(second != nullptr);
	h.pump();

	EditorSceneWorkspace *dormant_workspace = second->get_workspace();
	const int dormant_leaf = dormant_workspace->get_focused_leaf()->get_leaf_id();
	h.editor_data.register_tile(dormant_leaf);
	h.editor_data.set_focused_tile_id(dormant_leaf);
	const int scene_index = h.editor_data.add_edited_scene(-1);
	REQUIRE(h.editor_data.get_scene_tile(scene_index) == dormant_leaf);
	h.pump();

	// This is the resolution load_scene() performs when the requested path is already
	// open: the owning board is found across the whole strip, activated, and only then
	// asked to focus the tab. Resolving through the active board alone would leave the
	// current scene pointing at a tile the user cannot see.
	EditorBoard *owner = h.strip->find_board_for_leaf(h.editor_data.get_scene_tile(scene_index));
	REQUIRE(owner == second);
	h.strip->set_active_board(h.strip->get_board_index(owner));
	h.pump();
	CHECK(h.strip->get_active_board() == second);
	CHECK(owner->get_workspace()->focus_scene_tab(scene_index));
	CHECK(dormant_workspace->get_focused_leaf_id() == dormant_leaf);

	// The scene's context stays resolvable from the strip throughout, which is what
	// keeps _update_tile_display_attachments from deactivating scenes it cannot place.
	CHECK(h.strip->find_tile_by_id(h.editor_data.get_scene_tile(scene_index)) != nullptr);

	h.unmount();
}

TEST_CASE("[Editor][Boards] A switch keeps the outgoing board awake until the slide settles") {
	BoardStripHarness h;
	h.mount();
	h.pump();

	REQUIRE(h.strip->add_board("Second") != nullptr);
	h.pump();
	REQUIRE(h.strip->add_board("Third") != nullptr);
	h.pump();
	REQUIRE(h.strip->get_board_count() == 3);

	h.strip->set_active_board(1);
	// Immediately after the call -- before a single frame of animation runs -- both halves
	// of the switch are awake and the board not involved in it stays dormant.
	CHECK_FALSE(h.strip->get_board(0)->is_dormant());
	CHECK_FALSE(h.strip->get_board(1)->is_dormant());
	CHECK(h.strip->get_board(2)->is_dormant());

	h.settle_transition();
	CHECK(h.strip->get_board(0)->is_dormant());
	CHECK_FALSE(h.strip->get_board(1)->is_dormant());
	CHECK(h.strip->get_board(2)->is_dormant());

	h.unmount();
}

TEST_CASE("[Editor][Boards] active_board_changed fires once per switch regardless of animation length") {
	BoardStripHarness h;
	h.mount();
	h.pump();
	REQUIRE(h.strip->add_board("Second") != nullptr);
	h.pump();

	SIGNAL_WATCH(h.strip, "active_board_changed");
	h.strip->set_active_board(1);
	// Several frames of animation elapse; none of them is a fresh switch, so none of them
	// may emit the signal again.
	for (int i = 0; i < 10; i++) {
		h.pump();
	}
	h.settle_transition();
	SIGNAL_CHECK("active_board_changed", { { 1 } });

	SIGNAL_UNWATCH(h.strip, "active_board_changed");
	h.unmount();
}

TEST_CASE("[Editor][Boards] Interrupting a switch retargets instead of snapping") {
	BoardStripHarness h;
	h.mount();
	h.pump();
	REQUIRE(h.strip->add_board("Second") != nullptr);
	h.pump();
	REQUIRE(h.strip->add_board("Third") != nullptr);
	h.pump();
	REQUIRE(h.strip->get_board_count() == 3);

	h.strip->set_active_board(1);
	// Let the slide toward board 1 make real progress before interrupting it.
	for (int i = 0; i < 5; i++) {
		h.pump();
	}
	// Board 0's local position is 0, so its on-screen position is exactly the current
	// scroll offset shared by every board -- a direct, API-free read of the transform.
	const real_t mid_position = h.strip->get_board(0)->get_position().x;
	CHECK(mid_position < real_t(-1.0));
	CHECK(mid_position > real_t(-799.0));

	h.strip->set_active_board(2);
	h.pump();
	const real_t just_after_retarget = h.strip->get_board(0)->get_position().x;

	// Retargeting continues from mid_position toward the new target (-1600) rather than
	// snapping back to the interrupted slide's start (0, which is greater than
	// mid_position) or jumping straight to its old target (-800, which is less negative
	// than the new one can have reached after only one frame): the new value must lie
	// strictly between where the slide already was and where it is now headed.
	CHECK(just_after_retarget < mid_position);
	CHECK(just_after_retarget > real_t(-1600.0));

	// Board 1 -- the interrupted target -- is no longer part of the live pair once the new
	// slide settles; every board but the new outgoing/incoming pair stays dormant.
	h.settle_transition();
	CHECK(h.strip->get_board(0)->is_dormant());
	CHECK(h.strip->get_board(1)->is_dormant());
	CHECK_FALSE(h.strip->get_board(2)->is_dormant());

	h.unmount();
}

TEST_CASE("[Editor][Boards] Closing an unrelated board mid-slide still sleeps the outgoing board") {
	// Reproduces the stranded-outgoing-board defect: closing a third board that is neither
	// the active nor the outgoing half of an in-flight switch must not skip settling the
	// outgoing board's dormancy. Before the fix, close_board() unconditionally cleared
	// transition_outgoing without ever calling set_dormant(true) on the board it pointed
	// at, so a board that survived the close was left awake, visible, and processing
	// forever at its settled off-screen position.
	BoardStripHarness h;
	h.mount();
	h.pump();
	REQUIRE(h.strip->add_board("Second") != nullptr);
	h.pump();
	REQUIRE(h.strip->add_board("Third") != nullptr);
	h.pump();
	REQUIRE(h.strip->get_board_count() == 3);

	EditorBoard *board_a = h.strip->get_board(0);

	h.strip->set_active_board(1);
	// The slide has started but not settled; A is the outgoing half and must still be awake.
	CHECK_FALSE(board_a->is_dormant());

	// Close C -- neither the active board (1) nor the outgoing board (A) -- before the
	// slide toward B settles.
	CHECK(h.strip->close_board(2));
	h.pump();
	CHECK(h.strip->get_board_count() == 2);
	CHECK(h.strip->get_active_index() == 1);

	// A must not be stranded awake: it is off-screen once the strip settles, so it must
	// also be dormant.
	CHECK(board_a->is_dormant());
	CHECK_FALSE(board_a->is_visible());
	CHECK_FALSE(h.strip->get_board(1)->is_dormant());

	h.unmount();
}

// Feeds a caption the mouse events a real drag would deliver. The strip takes them from the
// caption's gui_input, ahead of the button's own handling, so driving that signal exercises
// exactly the path a pointer does.
static Ref<InputEventMouseButton> caption_mouse_button(real_t p_x, bool p_pressed) {
	Ref<InputEventMouseButton> event;
	event.instantiate();
	event->set_button_index(MouseButton::LEFT);
	event->set_pressed(p_pressed);
	event->set_position(Point2(p_x, 0));
	event->set_global_position(Point2(p_x, 0));
	return event;
}

static Ref<InputEventMouseMotion> caption_mouse_motion(real_t p_x) {
	Ref<InputEventMouseMotion> event;
	event.instantiate();
	event->set_button_mask(MouseButtonMask::LEFT);
	event->set_position(Point2(p_x, 0));
	event->set_global_position(Point2(p_x, 0));
	return event;
}

static bool same_leaf_ids(const HashSet<int> &p_left, const HashSet<int> &p_right) {
	if (p_left.size() != p_right.size()) {
		return false;
	}
	for (const int id : p_left) {
		if (!p_right.has(id)) {
			return false;
		}
	}
	return true;
}

static Button *caption_for(EditorBoardStrip *p_strip, int p_index) {
	Control *overlay = p_strip->get_caption_overlay();
	if (!overlay || p_index < 0 || p_index >= overlay->get_child_count()) {
		return nullptr;
	}
	return Object::cast_to<Button>(overlay->get_child(p_index));
}

TEST_CASE("[Editor][Boards] Moving a board keeps the same board active by identity") {
	BoardStripHarness h;
	h.mount();
	h.pump();

	EditorBoard *board_a = h.strip->get_board(0);
	EditorBoard *board_b = h.strip->add_board("B");
	EditorBoard *board_c = h.strip->add_board("C");
	if (!board_b || !board_c) {
		h.unmount();
		FAIL_CHECK("the strip must be able to add boards");
		return;
	}
	h.pump();
	h.settle_transition();

	SIGNAL_WATCH(h.strip, "board_moved");
	SIGNAL_WATCH(h.strip, "active_board_changed");

	// The active board is the one being moved: it must ride along to its new index rather
	// than the index staying put and a different board silently becoming active.
	REQUIRE(h.strip->get_active_index() == 0);
	h.strip->move_board(0, 2);
	h.pump();

	CHECK(h.strip->get_board(0) == board_b);
	CHECK(h.strip->get_board(1) == board_c);
	CHECK(h.strip->get_board(2) == board_a);
	CHECK(h.strip->get_active_board() == board_a);
	CHECK(h.strip->get_active_index() == 2);
	CHECK_FALSE(board_a->is_dormant());
	// A reorder is not a board switch: nothing about which board is on screen changed.
	SIGNAL_CHECK_FALSE("active_board_changed");
	// Exactly one emission for the whole reorder, not one per index it shifted.
	SIGNAL_CHECK("board_moved", { { 0, 2 } });

	// Child order carries draw order and hit-testing, so it has to match the board order.
	for (int i = 0; i < h.strip->get_board_count(); i++) {
		CHECK(h.strip->get_board(i)->get_index() == i);
	}
	// The captions still draw above every board.
	CHECK(h.strip->get_caption_overlay()->get_index() == h.strip->get_child_count() - 1);

	// A board moved past the active one displaces it: the active index changes even though
	// the active board itself was not the one moved.
	h.strip->move_board(0, 2);
	h.pump();
	CHECK(h.strip->get_board(0) == board_c);
	CHECK(h.strip->get_board(1) == board_a);
	CHECK(h.strip->get_board(2) == board_b);
	CHECK(h.strip->get_active_board() == board_a);
	CHECK(h.strip->get_active_index() == 1);
	SIGNAL_CHECK("board_moved", { { 0, 2 } });

	// A move that lands where it started is not a reorder and stays silent.
	h.strip->move_board(1, 1);
	h.pump();
	SIGNAL_CHECK_FALSE("board_moved");
	SIGNAL_CHECK_FALSE("active_board_changed");

	SIGNAL_UNWATCH(h.strip, "board_moved");
	SIGNAL_UNWATCH(h.strip, "active_board_changed");
	h.unmount();
}

TEST_CASE("[Editor][Boards] A reorder leaves leaf ids, tabs, and scene ownership untouched") {
	BoardStripHarness h;
	h.mount();
	h.pump();

	EditorBoard *board_a = h.strip->get_board(0);
	EditorBoard *board_b = h.strip->add_board("B");
	if (!board_b) {
		h.unmount();
		FAIL_CHECK("the strip must be able to add boards");
		return;
	}
	h.pump();

	EditorSceneWorkspace *workspace_a = board_a->get_workspace();
	EditorSceneWorkspace *workspace_b = board_b->get_workspace();
	workspace_a->split(workspace_a->get_focused_leaf(), false, EditorSceneWorkspace::SPLIT_SIDE_SECOND);
	h.pump();

	const HashSet<int> ids_a = collect_leaf_ids(workspace_a);
	const HashSet<int> ids_b = collect_leaf_ids(workspace_b);
	const int focus_a = workspace_a->get_focused_leaf_id();
	const int next_leaf_id = h.strip->peek_next_leaf_id();

	// A scene owned by a tile on the board that is about to move: reordering must not so
	// much as touch which tile owns it.
	const int leaf_b = workspace_b->get_focused_leaf()->get_leaf_id();
	h.editor_data.register_tile(leaf_b);
	h.editor_data.set_focused_tile_id(leaf_b);
	const int scene_index = h.editor_data.add_edited_scene(-1);
	REQUIRE(h.editor_data.get_scene_tile(scene_index) == leaf_b);

	h.strip->move_board(1, 0);
	h.pump();

	CHECK(h.strip->get_board(0) == board_b);
	CHECK(h.strip->get_board(1) == board_a);
	// Same workspaces, same leaves, same ids: no tree was rebuilt and no id reissued.
	CHECK(board_a->get_workspace() == workspace_a);
	CHECK(board_b->get_workspace() == workspace_b);
	CHECK(same_leaf_ids(collect_leaf_ids(workspace_a), ids_a));
	CHECK(same_leaf_ids(collect_leaf_ids(workspace_b), ids_b));
	CHECK(workspace_a->get_focused_leaf_id() == focus_a);
	CHECK(h.strip->peek_next_leaf_id() == next_leaf_id);
	// Scene ownership is keyed on leaf id, which the reorder never touches.
	CHECK(h.editor_data.get_edited_scene_count() == 1);
	CHECK(h.editor_data.get_scene_tile(scene_index) == leaf_b);
	CHECK(h.strip->find_board_for_leaf(leaf_b) == board_b);
	CHECK(h.strip->find_tile_by_id(leaf_b) == workspace_b->get_focused_leaf()->get_pane_tile());

	h.unmount();
}

TEST_CASE("[Editor][Boards] Board order round-trips through save and restore after a reorder") {
	Ref<ConfigFile> config;
	config.instantiate();

	BoardStripHarness saved;
	saved.mount();
	saved.pump();
	saved.strip->get_board(0)->set_title("A");
	REQUIRE(saved.strip->add_board("B") != nullptr);
	REQUIRE(saved.strip->add_board("C") != nullptr);
	saved.pump();

	saved.strip->set_active_board(2);
	saved.settle_transition();
	saved.strip->move_board(0, 2);
	saved.pump();

	REQUIRE(saved.strip->get_board(0)->get_title() == "B");
	REQUIRE(saved.strip->get_board(1)->get_title() == "C");
	REQUIRE(saved.strip->get_board(2)->get_title() == "A");
	// C was active at index 2 and the reorder displaced it to index 1.
	REQUIRE(saved.strip->get_active_index() == 1);

	EditorBoardStrip::save_to_config(config, saved.strip);
	saved.unmount();

	// The order is carried by the existing board_<i>_* keys, so no schema change is needed
	// for a reordered strip to come back in the order the user left it in.
	CHECK(String(config->get_value("Boards", "board_0_title")) == "B");
	CHECK(String(config->get_value("Boards", "board_1_title")) == "C");
	CHECK(String(config->get_value("Boards", "board_2_title")) == "A");
	CHECK(int(config->get_value("Boards", "active_board")) == 1);

	BoardStripHarness restored;
	restored.mount();
	restored.strip->restore_from_config(config);
	restored.pump();

	CHECK(restored.strip->get_board_count() == 3);
	CHECK(restored.strip->get_board(0)->get_title() == "B");
	CHECK(restored.strip->get_board(1)->get_title() == "C");
	CHECK(restored.strip->get_board(2)->get_title() == "A");
	CHECK(restored.strip->get_active_index() == 1);

	restored.unmount();
}

TEST_CASE("[Editor][Boards] Dragging a caption reorders its board instead of opening it") {
	BoardStripHarness h;
	h.mount();
	h.pump();

	EditorBoard *board_a = h.strip->get_board(0);
	EditorBoard *board_b = h.strip->add_board("B");
	EditorBoard *board_c = h.strip->add_board("C");
	if (!board_b || !board_c) {
		h.unmount();
		FAIL_CHECK("the strip must be able to add boards");
		return;
	}
	h.pump();

	h.strip->set_overview(true);
	h.pump();
	REQUIRE(h.strip->is_overview_active());

	Button *caption_a = caption_for(h.strip, 0);
	if (!caption_a) {
		h.unmount();
		FAIL_CHECK("the overview must build one caption per board");
		return;
	}

	SIGNAL_WATCH(h.strip, "board_moved");

	// Press over the first board, then travel across the strip to the last one. Three boards
	// in an 800px strip put board 2 well past the middle, so the pointer ends up squarely
	// inside it rather than in a gutter.
	caption_a->emit_signal(SceneStringName(gui_input), caption_mouse_button(100.0, true));
	caption_a->emit_signal(SceneStringName(gui_input), caption_mouse_motion(700.0));
	h.pump();

	CHECK(h.strip->get_board(0) == board_b);
	CHECK(h.strip->get_board(1) == board_c);
	CHECK(h.strip->get_board(2) == board_a);
	SIGNAL_CHECK("board_moved", { { 0, 2 } });
	// The captions follow their boards without being rebuilt, so the button the drag is
	// coming from is never freed mid-input.
	CHECK(caption_for(h.strip, 2) == caption_a);
	// Reordering is not selecting: the overview stays up for the whole drag.
	CHECK(h.strip->is_overview_active());

	// Releasing ends the drag. The button reports its press on release, and that press must
	// not additionally read as "open this board".
	caption_a->emit_signal(SceneStringName(gui_input), caption_mouse_button(700.0, false));
	caption_a->emit_signal(SceneStringName(pressed));
	h.pump();
	CHECK(h.strip->is_overview_active());
	SIGNAL_CHECK_FALSE("board_moved");

	// A press that never travels is still a plain click, and still opens the board.
	Button *caption_b = caption_for(h.strip, 0);
	if (!caption_b) {
		SIGNAL_UNWATCH(h.strip, "board_moved");
		h.unmount();
		FAIL_CHECK("the overview must keep one caption per board");
		return;
	}
	caption_b->emit_signal(SceneStringName(gui_input), caption_mouse_button(100.0, true));
	caption_b->emit_signal(SceneStringName(gui_input), caption_mouse_button(100.0, false));
	caption_b->emit_signal(SceneStringName(pressed));
	h.pump();
	SIGNAL_CHECK_FALSE("board_moved");
	CHECK_FALSE(h.strip->is_overview_active());
	CHECK(h.strip->get_active_board() == board_b);

	SIGNAL_UNWATCH(h.strip, "board_moved");
	h.unmount();
}

TEST_CASE("[Editor][Boards] A caption drag that stays inside its own board leaves the order alone") {
	BoardStripHarness h;
	h.mount();
	h.pump();

	EditorBoard *board_a = h.strip->get_board(0);
	REQUIRE(h.strip->add_board("B") != nullptr);
	REQUIRE(h.strip->add_board("C") != nullptr);
	h.pump();

	h.strip->set_overview(true);
	h.pump();

	Button *caption_a = caption_for(h.strip, 0);
	if (!caption_a) {
		h.unmount();
		FAIL_CHECK("the overview must build one caption per board");
		return;
	}

	SIGNAL_WATCH(h.strip, "board_moved");

	// Past the drag threshold, but still over board 0 and then in the gutter after it:
	// neither resolves to a different board, so neither is a reorder.
	caption_a->emit_signal(SceneStringName(gui_input), caption_mouse_button(100.0, true));
	caption_a->emit_signal(SceneStringName(gui_input), caption_mouse_motion(200.0));
	caption_a->emit_signal(SceneStringName(gui_input), caption_mouse_motion(262.0));
	caption_a->emit_signal(SceneStringName(gui_input), caption_mouse_button(262.0, false));
	h.pump();

	CHECK(h.strip->get_board(0) == board_a);
	SIGNAL_CHECK_FALSE("board_moved");

	SIGNAL_UNWATCH(h.strip, "board_moved");
	h.unmount();
}

} // namespace TestEditorBoardStrip
