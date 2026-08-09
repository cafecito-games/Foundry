/**************************************************************************/
/*  test_editor_board_cross_board.h                                       */
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

#include "tests/editor/test_workspace_tab_model.h"
#include "tests/test_macros.h"

namespace TestEditorBoardCrossBoard {

struct CrossBoardHarness {
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

// #1976: refresh_help_tab() and the script-leaf restore probe used to resolve
// through EditorNode::get_scene_workspace(), which only looks at the active
// board. A class-reference page (or a script leaf) left open on a dormant
// board was invisible to those call sites. The fix routes them through
// EditorBoardStrip::get_workspaces() instead; this proves that a help tab
// opened on a non-active board is reachable through that iteration.
TEST_CASE("[Editor][Boards] Help tab on a dormant board is reachable through every workspace") {
	using namespace TestWorkspaceTabModel;
	WorkspacePane::get_shared_tab_registry().clear_canonical_index();
	install_headless_help_type();

	CrossBoardHarness h;
	h.mount();
	h.pump();

	EditorBoard *first_board = h.strip->get_board(0);
	EditorBoard *second_board = h.strip->add_board("Second");
	REQUIRE(second_board != nullptr);
	h.pump();

	// Only the first board is active; the second is dormant and hidden.
	CHECK_FALSE(first_board->is_dormant());
	CHECK(second_board->is_dormant());

	EditorSceneWorkspace *dormant_workspace = second_board->get_workspace();
	REQUIRE(dormant_workspace != nullptr);
	WorkspaceLeafNode *dormant_source = dormant_workspace->get_focused_leaf();
	REQUIRE(dormant_source != nullptr);

	WorkspaceLeafNode *help_leaf = dormant_workspace->open_help_tab(dormant_source, "Node2D");
	h.pump();
	REQUIRE(help_leaf != nullptr);

	// A leaf lookup scoped to the active workspace alone would never see this page.
	EditorSceneWorkspace *active_workspace = h.strip->get_active_workspace();
	REQUIRE(active_workspace != nullptr);
	CHECK(active_workspace != dormant_workspace);
	CHECK(active_workspace->find_leaf_by_help_class("Node2D") == nullptr);

	// Iterating every workspace in the strip -- the pattern refresh_help_tab call
	// sites and the script-leaf restore probe now use -- finds the page.
	const Vector<EditorSceneWorkspace *> workspaces = h.strip->get_workspaces();
	REQUIRE(workspaces.size() == 2);

	WorkspaceLeafNode *found = nullptr;
	for (EditorSceneWorkspace *workspace : workspaces) {
		if (WorkspaceLeafNode *leaf = workspace->find_leaf_by_help_class("Node2D")) {
			found = leaf;
			break;
		}
	}
	CHECK(found == help_leaf);
	CHECK(dormant_workspace->find_leaf_by_help_class("Node2D") == help_leaf);

	// The same board-wide iteration drives a documentation refresh: every
	// workspace's refresh_help_tab() is called, and only the one actually
	// hosting the page does anything.
	headless_help_type_singleton->refreshed_stable_ids.clear();
	for (EditorSceneWorkspace *workspace : workspaces) {
		workspace->refresh_help_tab("Node2D");
	}
	CHECK(headless_help_type_singleton->refreshed_stable_ids.size() == 1);

	h.unmount();
}

} // namespace TestEditorBoardCrossBoard
