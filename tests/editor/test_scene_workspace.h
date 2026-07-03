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
#include "editor/editor_scene_workspace.h"

#include "scene/2d/node_2d.h"
#include "scene/3d/node_3d.h"
#include "scene/gui/subviewport_container.h"
#include "scene/main/viewport.h"
#include "scene/main/window.h"

#include "tests/test_macros.h"

namespace TestSceneWorkspace {

TEST_CASE("[SceneTree][Editor] pane-model-mapping") {
	EditorData editor_data;

	const int a = editor_data.add_edited_scene(-1);
	const int b = editor_data.add_edited_scene(-1);
	const int c = editor_data.add_edited_scene(-1);

	editor_data.set_scene_pane(a, 0);
	editor_data.set_scene_pane(b, 1);
	editor_data.set_scene_pane(c, 0);
	editor_data.set_pane_current_scene(0, a);
	editor_data.set_pane_current_scene(1, b);

	const Vector<int> pane_0 = editor_data.get_pane_scene_indices(0);
	const Vector<int> pane_1 = editor_data.get_pane_scene_indices(1);
	REQUIRE(pane_0.size() == 2);
	REQUIRE(pane_1.size() == 1);
	CHECK(pane_0[0] == a);
	CHECK(pane_0[1] == c);
	CHECK(pane_1[0] == b);

	CHECK(editor_data.pane_tab_to_scene_index(0, 0) == a);
	CHECK(editor_data.pane_tab_to_scene_index(0, 1) == c);
	CHECK(editor_data.scene_index_to_pane_tab(c) == 1);
	CHECK(editor_data.scene_index_to_pane_tab(b) == 0);

	editor_data.remove_scene(c);
	editor_data.remove_scene(b);
	editor_data.remove_scene(a);
}

TEST_CASE("[SceneTree][Editor] pane-model-remove-fixup") {
	EditorData editor_data;

	const int a = editor_data.add_edited_scene(-1);
	const int b = editor_data.add_edited_scene(-1);
	const int c = editor_data.add_edited_scene(-1);
	editor_data.set_scene_pane(a, 0);
	editor_data.set_scene_pane(b, 1);
	editor_data.set_scene_pane(c, 0);
	editor_data.set_focused_pane(0);
	editor_data.set_pane_current_scene(0, a);
	editor_data.set_pane_current_scene(1, b);

	editor_data.remove_scene(a);
	CHECK(editor_data.get_pane_current_scene(0) == 1);
	CHECK(editor_data.get_edited_scene() == 1);

	editor_data.set_focused_pane(1);
	editor_data.remove_scene(0);
	CHECK(editor_data.get_pane_current_scene(1) == -1);
	CHECK(editor_data.get_edited_scene() == -1);

	editor_data.set_focused_pane(0);
	editor_data.remove_scene(0);
	CHECK(editor_data.get_edited_scene_count() == 0);
}

TEST_CASE("[SceneTree][Editor] pane-model-cross-move") {
	EditorData editor_data;

	const int a = editor_data.add_edited_scene(-1);
	const int b = editor_data.add_edited_scene(-1);
	editor_data.set_scene_pane(a, 0);
	editor_data.set_scene_pane(b, 0);
	editor_data.set_pane_current_scene(0, a);
	editor_data.set_pane_current_scene(1, -1);

	editor_data.set_scene_pane(b, 1);
	editor_data.set_pane_current_scene(1, b);

	CHECK(editor_data.get_pane_scene_indices(0) == Vector<int>{ a });
	CHECK(editor_data.get_pane_scene_indices(1) == Vector<int>{ b });
	CHECK(editor_data.get_pane_current_scene(1) == b);

	editor_data.remove_scene(a);
	editor_data.remove_scene(b);
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
	CHECK(context->scene_has_3d_content());

	memdelete(context);
}

TEST_CASE("[SceneTree][Editor] workspace-config-round-trip") {
	EditorData editor_data;
	Ref<ConfigFile> config;
	config.instantiate();

	const int a = editor_data.add_edited_scene(-1);
	const int b = editor_data.add_edited_scene(-1);
	editor_data.set_scene_path(a, "res://pane0_a.tscn");
	editor_data.set_scene_path(b, "res://pane1_b.tscn");
	editor_data.set_scene_pane(a, 0);
	editor_data.set_scene_pane(b, 1);
	editor_data.set_pane_current_scene(0, a);
	editor_data.set_pane_current_scene(1, b);
	editor_data.set_focused_pane(1);

	config->set_value("Workspace", "pane_count", 2);
	config->set_value("Workspace", "split_vertical", true);
	config->set_value("Workspace", "split_offset", 123);
	config->set_value("Workspace", "focused_pane", 1);
	config->set_value("Workspace", "pane_0_scenes", PackedStringArray{ "res://pane0_a.tscn" });
	config->set_value("Workspace", "pane_1_scenes", PackedStringArray{ "res://pane1_b.tscn" });
	config->set_value("Workspace", "pane_0_current", "res://pane0_a.tscn");
	config->set_value("Workspace", "pane_1_current", "res://pane1_b.tscn");

	CHECK(EditorSceneWorkspace::get_saved_pane_count(config) == 2);
	CHECK(EditorSceneWorkspace::get_saved_split_vertical(config));
	CHECK(EditorSceneWorkspace::get_saved_split_offset(config) == 123);
	CHECK(EditorSceneWorkspace::get_saved_focused_pane(config) == 1);
	CHECK(EditorSceneWorkspace::get_saved_pane_scenes(config, 0) == PackedStringArray{ "res://pane0_a.tscn" });
	CHECK(EditorSceneWorkspace::get_saved_pane_scenes(config, 1) == PackedStringArray{ "res://pane1_b.tscn" });
	CHECK(EditorSceneWorkspace::get_saved_pane_current(config, 0) == "res://pane0_a.tscn");
	CHECK(EditorSceneWorkspace::get_saved_pane_current(config, 1) == "res://pane1_b.tscn");
	CHECK(EditorSceneWorkspace::has_workspace_session(config));

	EditorSceneWorkspace *workspace = EditorSceneWorkspace::create_single_pane_workspace();
	workspace->split_workspace(true);
	workspace->get_split()->set_split_offset(456);
	workspace->set_focused_pane(1);
	Ref<ConfigFile> saved_config;
	saved_config.instantiate();
	EditorSceneWorkspace::save_to_config(saved_config, editor_data, workspace);
	CHECK(EditorSceneWorkspace::get_saved_split_offset(saved_config) == 456);
	CHECK(saved_config->get_value("Workspace", "pane_1_current") == "res://pane1_b.tscn");
	memdelete(workspace);

	editor_data.remove_scene(b);
	editor_data.remove_scene(a);
}

} // namespace TestSceneWorkspace
