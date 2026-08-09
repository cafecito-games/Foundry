/**************************************************************************/
/*  test_editor_board_persistence.h                                       */
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
#include "editor/editor_board.h"
#include "editor/editor_board_strip.h"
#include "editor/editor_data.h"
#include "editor/editor_scene_workspace.h"

#include "scene/gui/control.h"
#include "scene/main/window.h"

#include "tests/test_macros.h"

namespace TestEditorBoardPersistence {

struct BoardPersistenceHarness {
	Control *host = nullptr;
	EditorData editor_data;
	EditorSelection *selection = nullptr;
	EditorBoardStrip *strip = nullptr;

	void mount() {
		host = memnew(Control);
		SceneTree::get_singleton()->get_root()->add_child(host);
		selection = memnew(EditorSelection);
		strip = EditorBoardStrip::create(selection, &editor_data);
		host->add_child(strip);
	}

	void unmount() {
		SceneTree::get_singleton()->get_root()->remove_child(host);
		memdelete(host);
		memdelete(selection);
	}
};

// Every leaf id currently live in the strip, across every board.
static Vector<int> collect_all_leaf_ids(EditorBoardStrip *p_strip) {
	Vector<int> ids;
	for (EditorSceneWorkspace *workspace : p_strip->get_workspaces()) {
		for (WorkspaceLeafNode *leaf : workspace->get_leaves()) {
			ids.push_back(leaf->get_leaf_id());
		}
	}
	return ids;
}

TEST_CASE("[Editor][Boards] Two workspaces persist to independent sections") {
	Ref<ConfigFile> config;
	config.instantiate();

	EditorData editor_data_a;
	EditorSelection *selection_a = memnew(EditorSelection);
	EditorSceneWorkspace *workspace_a = EditorSceneWorkspace::create_single_leaf_workspace(selection_a, &editor_data_a);
	SceneTree::get_singleton()->get_root()->add_child(workspace_a);
	workspace_a->split(workspace_a->get_focused_leaf(), false, EditorSceneWorkspace::SPLIT_SIDE_SECOND);

	EditorData editor_data_b;
	EditorSelection *selection_b = memnew(EditorSelection);
	EditorSceneWorkspace *workspace_b = EditorSceneWorkspace::create_single_leaf_workspace(selection_b, &editor_data_b);
	SceneTree::get_singleton()->get_root()->add_child(workspace_b);

	EditorSceneWorkspace::save_to_config(config, workspace_a, "Board_0");
	EditorSceneWorkspace::save_to_config(config, workspace_b, "Board_1");

	CHECK(EditorSceneWorkspace::has_workspace_session(config, "Board_0"));
	CHECK(EditorSceneWorkspace::has_workspace_session(config, "Board_1"));
	// Two leaves in A, one in B -- the sections must not have merged.
	CHECK(int(config->get_value("Board_0", "node_count")) == 3);
	CHECK(int(config->get_value("Board_1", "node_count")) == 1);

	memdelete(workspace_a);
	memdelete(workspace_b);
	memdelete(selection_a);
	memdelete(selection_b);
}

TEST_CASE("[Editor][Boards] Three boards round-trip independently") {
	Ref<ConfigFile> config;
	config.instantiate();

	BoardPersistenceHarness saved;
	saved.mount();
	saved.strip->add_board(String());
	saved.strip->add_board(String());
	saved.strip->get_board(1)->set_title("face shader");

	// Give each board a structurally distinct tree: 1, 3 and 5 nodes.
	EditorSceneWorkspace *workspace_1 = saved.strip->get_board(1)->get_workspace();
	workspace_1->split(workspace_1->get_focused_leaf(), true, EditorSceneWorkspace::SPLIT_SIDE_SECOND);
	EditorSceneWorkspace *workspace_2 = saved.strip->get_board(2)->get_workspace();
	WorkspaceLeafNode *second = workspace_2->split(workspace_2->get_focused_leaf(), false, EditorSceneWorkspace::SPLIT_SIDE_SECOND);
	workspace_2->split(second, true, EditorSceneWorkspace::SPLIT_SIDE_SECOND);

	saved.strip->set_active_index(1);
	const int board_1_focus = workspace_1->get_focused_leaf_id();
	const int board_2_focus = workspace_2->get_focused_leaf_id();
	const int leaf_high_water = saved.strip->peek_next_leaf_id();

	EditorBoardStrip::save_to_config(config, saved.strip);

	CHECK(EditorBoardStrip::has_board_session(config));
	CHECK(int(config->get_value("Boards", "board_count")) == 3);
	CHECK(int(config->get_value("Boards", "active_board")) == 1);
	CHECK(String(config->get_value("Boards", "board_1_title")) == "face shader");
	CHECK(int(config->get_value("Boards", "next_leaf_id")) == leaf_high_water);
	CHECK(int(config->get_value("Board_0", "node_count")) == 1);
	CHECK(int(config->get_value("Board_1", "node_count")) == 3);
	CHECK(int(config->get_value("Board_2", "node_count")) == 5);

	saved.unmount();

	BoardPersistenceHarness restored;
	restored.mount();
	restored.strip->restore_from_config(config);

	CHECK(restored.strip->get_board_count() == 3);
	CHECK(restored.strip->get_active_index() == 1);
	CHECK(restored.strip->get_board(1)->get_title() == "face shader");
	CHECK(restored.strip->get_board(0)->get_workspace()->get_leaf_count() == 1);
	CHECK(restored.strip->get_board(1)->get_workspace()->get_leaf_count() == 2);
	CHECK(restored.strip->get_board(2)->get_workspace()->get_leaf_count() == 3);
	CHECK(restored.strip->get_board(1)->get_workspace()->get_focused_leaf_id() == board_1_focus);
	CHECK(restored.strip->get_board(2)->get_workspace()->get_focused_leaf_id() == board_2_focus);
	// Only the active board is awake.
	CHECK_FALSE(restored.strip->get_board(1)->is_dormant());
	CHECK(restored.strip->get_board(0)->is_dormant());
	CHECK(restored.strip->get_board(2)->is_dormant());

	// A board added after restore must not reuse any restored leaf id.
	const Vector<int> restored_ids = collect_all_leaf_ids(restored.strip);
	EditorBoard *fresh = restored.strip->add_board(String());
	REQUIRE(fresh != nullptr);
	for (WorkspaceLeafNode *leaf : fresh->get_workspace()->get_leaves()) {
		CHECK_FALSE(restored_ids.has(leaf->get_leaf_id()));
	}

	restored.unmount();
}

TEST_CASE("[Editor][Boards] A legacy Workspace section is ignored, not adopted") {
	Ref<ConfigFile> config;
	config.instantiate();
	// A pre-boards layout: one workspace with a split, and no [Boards] section.
	config->set_value("Workspace", "node_count", 3);
	config->set_value("Workspace", "root_node", 0);
	config->set_value("Workspace", "focused_leaf_id", 0);

	BoardPersistenceHarness h;
	h.mount();

	CHECK_FALSE(EditorBoardStrip::has_board_session(config));

	// Restoring is a no-op; the strip keeps its single default board.
	h.strip->restore_from_config(config);
	CHECK(h.strip->get_board_count() == 1);
	CHECK(h.strip->get_active_workspace()->get_leaf_count() == 1);

	h.unmount();
}

TEST_CASE("[Editor][Boards] An out-of-range active_board clamps") {
	Ref<ConfigFile> config;
	config.instantiate();
	config->set_value("Boards", "board_count", 2);
	config->set_value("Boards", "active_board", 7);
	config->set_value("Boards", "board_0_id", 0);
	config->set_value("Boards", "board_1_id", 1);
	config->set_value("Board_0", "node_count", 1);
	config->set_value("Board_0", "root_node", 0);
	config->set_value("Board_0", "node_0_leaf_id", 0);
	config->set_value("Board_1", "node_count", 1);
	config->set_value("Board_1", "root_node", 0);
	config->set_value("Board_1", "node_0_leaf_id", 1);

	BoardPersistenceHarness h;
	h.mount();
	h.strip->restore_from_config(config);

	CHECK(h.strip->get_board_count() == 2);
	CHECK(h.strip->get_active_index() == 1);

	h.unmount();
}

TEST_CASE("[Editor][Boards] Restore brackets every board rebuild exactly once") {
	Ref<ConfigFile> config;
	config.instantiate();
	config->set_value("Boards", "board_count", 3);
	config->set_value("Boards", "active_board", 0);
	for (int i = 0; i < 3; i++) {
		config->set_value("Boards", vformat("board_%d_id", i), i);
		config->set_value(vformat("Board_%d", i), "node_count", 1);
		config->set_value(vformat("Board_%d", i), "root_node", 0);
		config->set_value(vformat("Board_%d", i), "node_0_leaf_id", i);
	}

	BoardPersistenceHarness h;
	h.mount();

	SIGNAL_WATCH(h.strip, "boards_about_to_restore");
	SIGNAL_WATCH(h.strip, "boards_restored");

	h.strip->restore_from_config(config);

	// Exactly one bracket for three rebuilt boards. A per-board detach would fire
	// these three times and leave the shared scene-mode surface parented to a freed
	// tile host on every board after the first.
	SIGNAL_CHECK("boards_about_to_restore", { {} });
	SIGNAL_CHECK("boards_restored", { {} });
	CHECK(h.strip->get_board_count() == 3);

	SIGNAL_UNWATCH(h.strip, "boards_about_to_restore");
	SIGNAL_UNWATCH(h.strip, "boards_restored");
	h.unmount();
}

TEST_CASE("[Editor][Boards] A restore with no board session emits no bracket") {
	Ref<ConfigFile> config;
	config.instantiate();
	config->set_value("Workspace", "node_count", 1);

	BoardPersistenceHarness h;
	h.mount();

	SIGNAL_WATCH(h.strip, "boards_about_to_restore");
	SIGNAL_WATCH(h.strip, "boards_restored");

	h.strip->restore_from_config(config);

	SIGNAL_CHECK_FALSE("boards_about_to_restore");
	SIGNAL_CHECK_FALSE("boards_restored");

	SIGNAL_UNWATCH(h.strip, "boards_about_to_restore");
	SIGNAL_UNWATCH(h.strip, "boards_restored");
	h.unmount();
}

TEST_CASE("[Editor][Boards] A fallback leaf id never collides with a later persisted id") {
	Ref<ConfigFile> config;
	config.instantiate();
	config->set_value("Boards", "board_count", 2);
	config->set_value("Boards", "active_board", 0);
	config->set_value("Boards", "board_0_id", 0);
	config->set_value("Boards", "board_1_id", 1);

	// Board 0 is corrupt: its root points outside the node range, so the restore falls
	// back to a freshly allocated leaf id.
	config->set_value("Board_0", "node_count", 1);
	config->set_value("Board_0", "root_node", 9);

	// Board 1 restores three leaves carrying ids 0, 1 and 2 -- exactly the range a
	// naive fresh allocation would have handed the fallback above.
	config->set_value("Board_1", "node_count", 5);
	config->set_value("Board_1", "root_node", 0);
	config->set_value("Board_1", "node_0_type", "split");
	config->set_value("Board_1", "node_0_vertical", false);
	config->set_value("Board_1", "node_0_offset", 0);
	config->set_value("Board_1", "node_0_child_a", 1);
	config->set_value("Board_1", "node_0_child_b", 2);
	config->set_value("Board_1", "node_1_type", "leaf");
	config->set_value("Board_1", "node_1_leaf_id", 0);
	config->set_value("Board_1", "node_2_type", "split");
	config->set_value("Board_1", "node_2_vertical", true);
	config->set_value("Board_1", "node_2_offset", 0);
	config->set_value("Board_1", "node_2_child_a", 3);
	config->set_value("Board_1", "node_2_child_b", 4);
	config->set_value("Board_1", "node_3_type", "leaf");
	config->set_value("Board_1", "node_3_leaf_id", 1);
	config->set_value("Board_1", "node_4_type", "leaf");
	config->set_value("Board_1", "node_4_leaf_id", 2);

	BoardPersistenceHarness h;
	h.mount();
	h.strip->restore_from_config(config);

	CHECK(h.strip->get_board_count() == 2);

	const Vector<int> ids = collect_all_leaf_ids(h.strip);
	CHECK(ids.size() == 4);
	HashSet<int> distinct;
	for (int id : ids) {
		CHECK_FALSE(distinct.has(id));
		distinct.insert(id);
	}
	// Each id resolves back to exactly the board that owns it, so scene-tile ownership
	// cannot be silently reassigned across boards.
	for (int id : ids) {
		CHECK(h.strip->find_board_for_leaf(id) != nullptr);
	}
	CHECK(h.strip->find_board_for_leaf(0) == h.strip->get_board(1));

	h.unmount();
}

TEST_CASE("[Editor][Boards] The allocator is seeded past every persisted id before any leaf exists") {
	Ref<ConfigFile> config;
	config.instantiate();
	config->set_value("Boards", "board_count", 2);
	config->set_value("Boards", "active_board", 0);
	config->set_value("Boards", "next_leaf_id", 3);
	config->set_value("Board_0", "node_count", 1);
	config->set_value("Board_0", "root_node", 0);
	config->set_value("Board_0", "node_0_type", "leaf");
	config->set_value("Board_0", "node_0_leaf_id", 4);
	// The highest persisted id lives in the *last* board, so a seed computed board by
	// board would already have minted colliding ids by the time it is seen.
	config->set_value("Board_1", "node_count", 1);
	config->set_value("Board_1", "root_node", 0);
	config->set_value("Board_1", "node_0_type", "leaf");
	config->set_value("Board_1", "node_0_leaf_id", 41);

	BoardPersistenceHarness h;
	h.mount();
	h.strip->restore_from_config(config);

	CHECK(h.strip->peek_next_leaf_id() > 41);
	const Vector<int> ids = collect_all_leaf_ids(h.strip);
	CHECK(ids.has(4));
	CHECK(ids.has(41));
	CHECK(h.strip->allocate_leaf_id() > 41);

	h.unmount();
}

} // namespace TestEditorBoardPersistence
