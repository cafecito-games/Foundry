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

#include "core/io/json.h"

#include "scene/2d/node_2d.h"
#include "scene/gui/control.h"
#include "scene/main/window.h"

#include "tests/editor/editor_workflow_test_fixtures.h"
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

// #1981: dragging a pane out of one board and into another is the interaction the
// whole canvas-transform hosting architecture exists to make possible. This proves
// handle_tab_drop resolves a source pane on a different board through
// EditorBoardStrip::find_board_for_leaf(), moves EditorData scene-tile ownership,
// and resyncs both the source and destination workspaces -- not just the one the
// call landed on.
TEST_CASE("[Editor][Boards] A scene tab dragged across boards moves ownership and collapses the emptied source pane") {
	WorkspacePane::get_shared_tab_registry().clear_canonical_index();

	CrossBoardHarness h;
	h.mount();
	h.pump();

	EditorBoard *board_a = h.strip->get_board(0);
	EditorBoard *board_b = h.strip->add_board("Board B");
	REQUIRE(board_b != nullptr);
	h.pump();

	EditorSceneWorkspace *workspace_a = board_a->get_workspace();
	EditorSceneWorkspace *workspace_b = board_b->get_workspace();
	REQUIRE(workspace_a != nullptr);
	REQUIRE(workspace_b != nullptr);

	// Split board A so the scene lives on a non-default leaf: the sole/default leaf
	// of a workspace is never collapsed, so this is the only way to exercise the
	// "emptied source pane collapses" half of the acceptance criteria.
	WorkspaceLeafNode *leaf_a_default = workspace_a->get_focused_leaf();
	REQUIRE(leaf_a_default != nullptr);
	WorkspaceLeafNode *leaf_a_source = workspace_a->split(leaf_a_default, false, EditorSceneWorkspace::SPLIT_SIDE_SECOND);
	h.pump();
	REQUIRE(leaf_a_source != nullptr);
	REQUIRE(workspace_a->get_leaf_count() == 2);

	Node2D *root = memnew(Node2D);
	const int scene_idx = TestSceneWorkspace::add_test_scene(h.editor_data, leaf_a_source->get_leaf_id(), root);
	workspace_a->sync_scene_tabs_from_editor_data();

	WorkspacePane *pane_a_source = leaf_a_source->get_workspace_pane();
	REQUIRE(pane_a_source != nullptr);
	REQUIRE(pane_a_source->get_tab_count() == 1);

	WorkspaceLeafNode *leaf_b = workspace_b->get_focused_leaf();
	REQUIRE(leaf_b != nullptr);
	WorkspacePane *pane_b = leaf_b->get_workspace_pane();
	REQUIRE(pane_b != nullptr);
	REQUIRE(pane_b->get_tab_count() == 0);

	// Called on board B's workspace -- the destination -- with a source pane id that
	// belongs to board A. This is the shape a real drop takes: the model call
	// resolves the source itself instead of the caller pre-locating it.
	// handle_tab_drop moves scene-tile ownership and resyncs both panes' tab counts
	// synchronously; only the emptied source leaf's collapse is deferred to an idle
	// frame (collapse_if_empty_deferred). Assert the synchronous effects before the
	// first pump so that pump does not race ahead of the leaf_count == 2 check below.
	WorkspaceLeafNode *dest = workspace_b->handle_tab_drop(leaf_a_source->get_leaf_id(), 0, leaf_b, EditorSceneWorkspace::DROP_CENTER);

	REQUIRE(dest == leaf_b);
	CHECK(h.editor_data.get_scene_tile(scene_idx) == leaf_b->get_leaf_id());
	CHECK(pane_b->get_tab_count() == 1);
	CHECK(pane_b->get_tab(0).get_type_id() == StringName("scene"));

	// Both workspaces were resynced by the single handle_tab_drop call: the
	// destination gained the tab and the source's pane lost it.
	CHECK(pane_a_source->get_tab_count() == 0);

	// The now-empty non-default pane on board A collapses on the next idle frame;
	// board A's default leaf, which the move never touched, survives.
	CHECK(workspace_a->get_leaf_count() == 2);
	h.pump();
	CHECK(workspace_a->get_leaf_count() == 1);
	CHECK(workspace_a->get_leaf_by_id(leaf_a_default->get_leaf_id()) == leaf_a_default);

	h.unmount();
}

TEST_CASE("[Editor][Boards] A script tab dragged across boards moves via take_tab/add_tab") {
	WorkspacePane::get_shared_tab_registry().clear_canonical_index();

	CrossBoardHarness h;
	h.mount();
	h.pump();

	EditorBoard *board_a = h.strip->get_board(0);
	EditorBoard *board_b = h.strip->add_board("Board B");
	REQUIRE(board_b != nullptr);
	h.pump();

	EditorSceneWorkspace *workspace_a = board_a->get_workspace();
	EditorSceneWorkspace *workspace_b = board_b->get_workspace();

	WorkspaceLeafNode *leaf_a = workspace_a->get_focused_leaf();
	REQUIRE(leaf_a != nullptr);
	WorkspacePane *pane_a = leaf_a->get_workspace_pane();
	REQUIRE(pane_a != nullptr);
	TestSceneWorkspace::add_script_tab(pane_a, "res://move.fs");
	REQUIRE(pane_a->get_tab_count() == 1);

	WorkspaceLeafNode *leaf_b = workspace_b->get_focused_leaf();
	REQUIRE(leaf_b != nullptr);
	WorkspacePane *pane_b = leaf_b->get_workspace_pane();
	REQUIRE(pane_b != nullptr);
	REQUIRE(pane_b->get_tab_count() == 0);

	WorkspaceLeafNode *dest = workspace_b->handle_tab_drop(leaf_a->get_leaf_id(), 0, leaf_b, EditorSceneWorkspace::DROP_CENTER);
	h.pump();

	REQUIRE(dest == leaf_b);
	CHECK(pane_b->get_tab_count() == 1);
	CHECK(pane_b->get_tab(0).get_resource_key() == "res://move.fs");
	// Board A's leaf is its sole/default leaf, so emptying it never collapses it.
	CHECK(pane_a->get_tab_count() == 0);
	CHECK(workspace_a->get_leaf_count() == 1);

	h.unmount();
}

TEST_CASE("[Editor][Boards] A cross-board drop with an unresolvable source pane makes no partial move") {
	WorkspacePane::get_shared_tab_registry().clear_canonical_index();

	CrossBoardHarness h;
	h.mount();
	h.pump();

	EditorBoard *board_b = h.strip->add_board("Board B");
	REQUIRE(board_b != nullptr);
	h.pump();
	EditorSceneWorkspace *workspace_b = board_b->get_workspace();

	WorkspaceLeafNode *leaf_b = workspace_b->get_focused_leaf();
	REQUIRE(leaf_b != nullptr);
	WorkspacePane *pane_b = leaf_b->get_workspace_pane();
	REQUIRE(pane_b != nullptr);
	REQUIRE(pane_b->get_tab_count() == 0);

	// No leaf anywhere in the strip carries this id.
	const int unresolvable_source_id = 999999;
	WorkspaceLeafNode *dest = workspace_b->handle_tab_drop(unresolvable_source_id, 0, leaf_b, EditorSceneWorkspace::DROP_CENTER);

	CHECK(dest == nullptr);
	CHECK(pane_b->get_tab_count() == 0);

	h.unmount();
}

TEST_CASE("[Editor][Boards] handle_tab_strip_drop moves a scene tab across boards and resyncs both") {
	WorkspacePane::get_shared_tab_registry().clear_canonical_index();

	CrossBoardHarness h;
	h.mount();
	h.pump();

	EditorBoard *board_a = h.strip->get_board(0);
	EditorBoard *board_b = h.strip->add_board("Board B");
	REQUIRE(board_b != nullptr);
	h.pump();

	EditorSceneWorkspace *workspace_a = board_a->get_workspace();
	EditorSceneWorkspace *workspace_b = board_b->get_workspace();

	WorkspaceLeafNode *leaf_a = workspace_a->get_focused_leaf();
	REQUIRE(leaf_a != nullptr);
	Node2D *root = memnew(Node2D);
	const int scene_idx = TestSceneWorkspace::add_test_scene(h.editor_data, leaf_a->get_leaf_id(), root);
	workspace_a->sync_scene_tabs_from_editor_data();

	WorkspacePane *pane_a = leaf_a->get_workspace_pane();
	REQUIRE(pane_a != nullptr);
	REQUIRE(pane_a->get_tab_count() == 1);

	WorkspaceLeafNode *leaf_b = workspace_b->get_focused_leaf();
	REQUIRE(leaf_b != nullptr);
	WorkspacePane *pane_b = leaf_b->get_workspace_pane();
	REQUIRE(pane_b != nullptr);
	REQUIRE(pane_b->get_tab_count() == 0);

	// Same cross-board shape as handle_tab_drop, but through the strip-based entry
	// point used for a drop landing at a specific tab-strip position.
	WorkspaceLeafNode *dest = workspace_b->handle_tab_strip_drop(leaf_a->get_leaf_id(), 0, leaf_b->get_leaf_id(), 0);
	h.pump();

	REQUIRE(dest == leaf_b);
	CHECK(h.editor_data.get_scene_tile(scene_idx) == leaf_b->get_leaf_id());
	CHECK(pane_b->get_tab_count() == 1);
	CHECK(pane_a->get_tab_count() == 0);

	h.unmount();
}

// #2047: the rosette/tile-body drop path resolved its destination leaf only in
// the active board's workspace, so a drop onto a non-active board's tile body
// failed with a null target leaf while the tab-strip path (which resolves the
// destination across boards) worked. A strip mounted on its own cannot reach
// that code: the resolution lives in EditorNode, and the failing gesture is a
// real pointer drag. This drives a real editor subprocess through the workflow
// harness so the whole path -- tab drag, drop overlay, EditorNode resolution --
// runs as a user's drag does.
TEST_CASE("[Editor][Boards] Cross-board tile body drop workflow subprocess") {
	if (!EditorWorkflowTestFixtures::workflow_has_display()) {
		MESSAGE("Requires a GUI display. Re-run with DISPLAY set so the editor subprocess starts.");
		return;
	}

	const String project_path = EditorWorkflowTestFixtures::prepare_disposable_project();
	if (project_path.is_empty()) {
		FAIL("Failed to prepare a temporary workflow project copy.");
		return;
	}

	List<String> arguments;
	arguments.push_back("editor");
	arguments.push_back("open");
	arguments.push_back("--headless");
	arguments.push_back("--project");
	arguments.push_back(project_path);
	arguments.push_back("--automation");
	arguments.push_back("--automation-run-workflow=cross_board_tile_body_drop");

	int exit_code = -1;
	const String output = EditorWorkflowTestFixtures::workflow_run_subprocess(arguments, exit_code);
	INFO("Subprocess output:\n", output);

	const int marker = output.find("FOUNDRY_AUTOMATION_WORKFLOW");
	CHECK_MESSAGE(marker >= 0, "Workflow result line was not printed.");
	if (marker < 0) {
		return;
	}

	const int line_start = marker + String("FOUNDRY_AUTOMATION_WORKFLOW ").length();
	const int line_end = output.find_char('\n', line_start);
	const String json_text = line_end >= 0 ? output.substr(line_start, line_end - line_start) : output.substr(line_start);
	JSON json;
	if (json.parse(json_text.strip_edges()) != OK) {
		FAIL("Workflow result line was not valid JSON: ", json_text);
		return;
	}
	const Dictionary payload = json.get_data();
	CHECK(String(payload.get("workflow", String())) == "cross_board_tile_body_drop");
	CHECK_MESSAGE((bool)payload.get("ok", false), String(payload.get("message", String())));
	CHECK(exit_code == 0);
}

} // namespace TestEditorBoardCrossBoard
