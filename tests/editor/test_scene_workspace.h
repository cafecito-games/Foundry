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
#include "editor/editor_scene_workspace.h"

#include "scene/gui/split_container.h"
#include "scene/main/window.h"

#include "tests/test_macros.h"
#include "tests/test_tools.h"

namespace TestSceneWorkspace {

struct WorkspaceHarness {
	Control *host = nullptr;
	EditorSceneWorkspace *workspace = nullptr;

	void mount() {
		host = memnew(Control);
		host->set_custom_minimum_size(Size2(800, 600));
		SceneTree::get_singleton()->get_root()->add_child(host);
		workspace = EditorSceneWorkspace::create_single_leaf_workspace();
		host->add_child(workspace);
		workspace->set_anchors_and_offsets_preset(Control::PRESET_FULL_RECT);
		host->set_size(Size2(800, 600));
		workspace->set_size(Size2(800, 600));
	}

	void pump() {
		SceneTree::get_singleton()->process_frame();
		MessageQueue::get_singleton()->flush();
	}

	void unmount() {
		memdelete(workspace);
		SceneTree::get_singleton()->get_root()->remove_child(host);
		memdelete(host);
	}
};

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

TEST_CASE("[SceneTree][Editor] tree-move") {
	WorkspaceHarness h;
	h.mount();

	WorkspaceLeafNode *first = h.workspace->get_focused_leaf();
	WorkspaceLeafNode *second = h.workspace->split(first, false, EditorSceneWorkspace::SPLIT_SIDE_SECOND);
	h.pump();
	REQUIRE(second != nullptr);

	first->set_content_descriptor("pane_a");
	second->set_content_descriptor(String());

	CHECK(h.workspace->move_content("pane_a", first, second));
	CHECK(first->get_content_descriptor().is_empty());
	CHECK(second->get_content_descriptor() == "pane_a");

	// Moving content that is not present fails cleanly.
	CHECK_FALSE(h.workspace->move_content("pane_a", first, second));
	CHECK_FALSE(h.workspace->move_content("pane_a", second, second));

	h.unmount();
}

TEST_CASE("[SceneTree][Editor] tree-persist") {
	WorkspaceHarness h;
	h.mount();

	WorkspaceLeafNode *first = h.workspace->get_focused_leaf();
	first->set_content_descriptor("main");
	WorkspaceLeafNode *second = h.workspace->split(first, false, EditorSceneWorkspace::SPLIT_SIDE_SECOND);
	h.pump();
	WorkspaceLeafNode *third = h.workspace->split(second, true, EditorSceneWorkspace::SPLIT_SIDE_SECOND);
	h.pump();
	REQUIRE(h.workspace->get_leaf_count() == 3);

	second->set_content_descriptor("aux");
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
	const String saved_main = "main";
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

	WorkspaceLeafNode *restored_main = nullptr;
	WorkspaceLeafNode *restored_aux = nullptr;
	for (WorkspaceLeafNode *leaf : h2.workspace->get_leaves()) {
		if (leaf->get_content_descriptor() == saved_main) {
			restored_main = leaf;
		}
		if (leaf->get_content_descriptor() == saved_aux) {
			restored_aux = leaf;
		}
	}
	REQUIRE(restored_main != nullptr);
	REQUIRE(restored_aux != nullptr);
	CHECK(restored_aux->get_leaf_id() == saved_focus);

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

} // namespace TestSceneWorkspace
