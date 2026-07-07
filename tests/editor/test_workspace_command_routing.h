/**************************************************************************/
/*  test_workspace_command_routing.h                                      */
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

#include "editor/editor_data.h"
#include "editor/editor_scene_pane_tile.h"
#include "editor/editor_scene_workspace.h"
#include "editor/editor_script_leaf.h"
#include "editor/script/script_editor_controller.h"
#include "editor/script/script_editor_view.h"

#include "scene/gui/control.h"
#include "scene/main/window.h"
#include "scene/resources/text_file.h"

// Reuses WorkspaceHarness + replace_leaf_with_script.
#include "tests/editor/test_scene_workspace.h"
#include "tests/test_macros.h"
#include "tests/test_tools.h"

namespace TestWorkspaceCommandRouting {

using TestSceneWorkspace::replace_leaf_with_script;
using TestSceneWorkspace::WorkspaceHarness;

static Ref<TextFile> make_text_file(const String &p_path, const String &p_source) {
	Ref<TextFile> text_file;
	text_file.instantiate();
	text_file->set_path(p_path);
	text_file->set_text(p_source);
	return text_file;
}

// A scene command must act on the focused pane's active scene tab, not on some
// other pane's scene. Focus resolution is the routing contract that scene
// File-menu actions (save/close/save-all) build on.
TEST_CASE("[SceneWorkspace][command-routing][Editor] scene-command-targets-focused-scene-tab") {
	WorkspaceHarness h;
	h.mount();
	h.pump();

	WorkspaceLeafNode *leaf_a = h.workspace->get_focused_leaf();
	WorkspaceLeafNode *leaf_b = h.workspace->split(leaf_a, false, EditorSceneWorkspace::SPLIT_SIDE_SECOND);
	h.pump();
	REQUIRE(leaf_b != nullptr);

	ScenePaneTile *tile_a = leaf_a->get_pane_tile();
	ScenePaneTile *tile_b = leaf_b->get_pane_tile();
	REQUIRE(tile_a != nullptr);
	REQUIRE(tile_b != nullptr);

	h.workspace->set_focused_leaf(leaf_b->get_leaf_id());
	CHECK(h.workspace->get_effective_focused_tile() == tile_b);

	h.workspace->set_focused_leaf(leaf_a->get_leaf_id());
	CHECK(h.workspace->get_effective_focused_tile() == tile_a);

	h.unmount();
}

// A script command must act on the focused script view. The global controller
// routes global script actions (save/run/search) through the focused view.
TEST_CASE("[Editor][command-routing] script-command-targets-focused-script-tab") {
	Control *host = memnew(Control);
	SceneTree::get_singleton()->get_root()->add_child(host);
	ScriptEditorController *controller = memnew(ScriptEditorController);
	controller->init_global_services(host);

	ScriptLeaf *leaf_a = memnew(ScriptLeaf);
	ScriptLeaf *leaf_b = memnew(ScriptLeaf);
	host->add_child(leaf_a);
	host->add_child(leaf_b);
	SceneTree::get_singleton()->process(0.016);
	MessageQueue::get_singleton()->flush();

	ScriptEditorView *view_a = controller->create_view_for_leaf(leaf_a);
	ScriptEditorView *view_b = controller->create_view_for_leaf(leaf_b);
	REQUIRE(view_a != nullptr);
	REQUIRE(view_b != nullptr);

	CHECK(view_a->edit(make_text_file("res://alpha.fs", "func run(): pass"), true));
	CHECK(view_b->edit(make_text_file("res://beta.fs", "func run(): pass"), true));
	SceneTree::get_singleton()->process(0.016);
	MessageQueue::get_singleton()->flush();

	String path;
	int line = 0;
	int column = 0;

	controller->set_focused_view(view_a);
	CHECK(controller->get_current_script_view_state(path, line, column));
	CHECK(path == "res://alpha.fs");

	controller->set_focused_view(view_b);
	CHECK(controller->get_current_script_view_state(path, line, column));
	CHECK(path == "res://beta.fs");

	host->remove_child(leaf_a);
	host->remove_child(leaf_b);
	memdelete(leaf_a);
	memdelete(leaf_b);
	memdelete(controller);
	SceneTree::get_singleton()->get_root()->remove_child(host);
	memdelete(host);
}

// A scene API invoked while a script tab is focused resolves to the
// most-recently-focused scene tab via get_effective_focused_tile().
TEST_CASE("[SceneWorkspace][command-routing][Editor] scene-command-with-script-tab-focused-uses-fallback") {
	WorkspaceHarness h;
	h.mount();
	h.pump();

	WorkspaceLeafNode *scene_leaf = h.workspace->get_focused_leaf();
	WorkspaceLeafNode *script_leaf = h.workspace->split(scene_leaf, false, EditorSceneWorkspace::SPLIT_SIDE_SECOND);
	h.pump();
	REQUIRE(script_leaf != nullptr);

	ScenePaneTile *scene_tile = scene_leaf->get_pane_tile();
	REQUIRE(scene_tile != nullptr);

	// Focus the scene leaf first so it becomes the most-recently-focused scene.
	h.workspace->set_focused_leaf(scene_leaf->get_leaf_id());

	// Turn the second leaf into a script pane and focus it.
	replace_leaf_with_script(script_leaf, "player.fs");
	h.workspace->set_focused_leaf(script_leaf->get_leaf_id());
	h.pump();

	// Focused leaf hosts no scene tile, so scene APIs fall back to the last
	// focused scene tile instead of resolving against a null tile.
	CHECK(h.workspace->get_focused_tile() == nullptr);
	CHECK(h.workspace->get_effective_focused_tile() == scene_tile);

	h.unmount();
}

// A script-only workspace (no edited scene at all) must not crash or spam
// out-of-bounds errors when scene accessors are queried during normal use.
TEST_CASE("[Editor][command-routing] script-only-workspace-no-crash") {
	// EditorData with no edited scene models the script-only state.
	EditorData editor_data;
	CHECK(editor_data.get_edited_scene() == -1);
	CHECK(editor_data.get_edited_scene_count() == 0);

	{
		ErrorDetector error_detector;
		Node *root = editor_data.get_edited_scene_root();
		CHECK(root == nullptr);
		// No edited scene is a legitimate state here, not an index error.
		CHECK_FALSE(error_detector.has_error);
	}

	{
		ErrorDetector error_detector;
		CHECK(editor_data.get_current_edited_scene_history_id() == 0);
		CHECK_FALSE(error_detector.has_error);
	}

	// The workspace resolves no scene tile when every pane hosts a script.
	WorkspaceHarness h;
	h.mount();
	h.pump();

	replace_leaf_with_script(h.workspace->get_focused_leaf(), "only.fs");
	h.pump();

	CHECK(h.workspace->get_focused_tile() == nullptr);
	CHECK(h.workspace->get_effective_focused_tile() == nullptr);
	CHECK(h.workspace->get_tiles().is_empty());

	h.unmount();
}

// Invoking a scene-only command while only script tabs exist must be a no-op
// against a null target and must not corrupt editor scene state.
TEST_CASE("[Editor][command-routing] inapplicable-command-no-corruption") {
	WorkspaceHarness h;
	h.mount();
	h.pump();

	replace_leaf_with_script(h.workspace->get_focused_leaf(), "only.fs");
	h.pump();

	const int scene_count_before = h.editor_data.get_edited_scene_count();
	const int current_before = h.editor_data.get_edited_scene();

	// A scene command resolves its target through the focused tile. With no
	// scene tile the target is null and the command must skip its scene work.
	ScenePaneTile *target = h.workspace->get_effective_focused_tile();
	CHECK(target == nullptr);
	if (target) {
		FAIL("scene command should have no target in a script-only workspace");
	}

	// Editor scene state is untouched by the skipped command.
	CHECK(h.editor_data.get_edited_scene_count() == scene_count_before);
	CHECK(h.editor_data.get_edited_scene() == current_before);
	CHECK(h.editor_data.get_edited_scene() == -1);

	h.unmount();
}

} // namespace TestWorkspaceCommandRouting
