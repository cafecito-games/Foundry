/**************************************************************************/
/*  test_editor_scene_context.h                                           */
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

#include "editor/editor_data.h"
#include "editor/editor_scene_context.h"

#include "scene/2d/node_2d.h"
#include "scene/main/viewport.h"
#include "scene/main/window.h"

#include "tests/test_macros.h"

namespace TestEditorSceneContext {

TEST_CASE("[SceneTree][Editor] EditorSceneContext owns distinct per-scene state objects") {
	EditorSceneContext *context_a = memnew(EditorSceneContext);
	EditorSceneContext *context_b = memnew(EditorSceneContext);

	CHECK(context_a->get_selection() != nullptr);
	CHECK(context_a->get_history() != nullptr);
	CHECK(context_a->get_viewport() != nullptr);

	CHECK(context_a->get_selection() != context_b->get_selection());
	CHECK(context_a->get_history() != context_b->get_history());
	CHECK(context_a->get_viewport() != context_b->get_viewport());

	memdelete(context_a);
	memdelete(context_b);
}

TEST_CASE("[SceneTree][Editor] EditorSceneContext keeps the scene parented to its viewport for its whole lifetime") {
	Window *tree_root = SceneTree::get_singleton()->get_root();

	EditorSceneContext *context = memnew(EditorSceneContext);
	Node2D *scene = memnew(Node2D);
	context->set_scene_root_node(scene);

	CHECK(context->get_scene_root_node() == scene);
	CHECK(scene->get_parent() == context->get_viewport());
	CHECK_FALSE(context->get_viewport()->is_inside_tree());
	CHECK_FALSE(context->is_active());

	context->activate(tree_root);
	CHECK(context->is_active());
	CHECK(context->get_viewport()->is_inside_tree());
	CHECK(context->get_viewport()->get_parent() == tree_root);
	CHECK(scene->is_inside_tree());
	CHECK(scene->get_parent() == context->get_viewport());

	context->deactivate();
	CHECK_FALSE(context->is_active());
	// Inactive contexts leave the tree entirely, so they do not render and
	// their 3D content does not leak into the shared editor world.
	CHECK_FALSE(context->get_viewport()->is_inside_tree());
	// The scene is never reparented; it stays under its context viewport.
	CHECK(scene->get_parent() == context->get_viewport());

	context->activate(tree_root);
	CHECK(scene->is_inside_tree());
	CHECK(scene->get_parent() == context->get_viewport());
	context->deactivate();

	memdelete(context); // Frees the viewport and the scene with it.
}

TEST_CASE("[SceneTree][Editor] EditorSceneContext retains its selection across deactivation") {
	Window *tree_root = SceneTree::get_singleton()->get_root();

	EditorSceneContext *context = memnew(EditorSceneContext);
	Node2D *scene = memnew(Node2D);
	context->set_scene_root_node(scene);
	Node2D *child = memnew(Node2D);
	scene->add_child(child);

	context->activate(tree_root);

	EditorSelection *selection = context->get_selection();
	selection->add_node(child);
	CHECK(selection->is_selected(child));

	context->deactivate();
	// The nodes left the tree, so the live selection no longer reports them...
	CHECK_FALSE(selection->is_selected(child));
	// ...but the context still knows what was selected.
	Vector<ObjectID> retained = context->get_selected_node_ids();
	REQUIRE(retained.size() == 1);
	CHECK(retained[0] == child->get_instance_id());

	context->activate(tree_root);
	// Same selection object, repopulated.
	CHECK(context->get_selection() == selection);
	CHECK(selection->is_selected(child));

	context->deactivate();
	memdelete(context);
}

TEST_CASE("[SceneTree][Editor] EditorSceneContext selected node ids can be rewritten while inactive") {
	Window *tree_root = SceneTree::get_singleton()->get_root();

	EditorSceneContext *context = memnew(EditorSceneContext);
	Node2D *scene = memnew(Node2D);
	context->set_scene_root_node(scene);
	Node2D *child = memnew(Node2D);
	scene->add_child(child);

	// While inactive (e.g. during scene reimport), selection updates go
	// through the retained id list without any tree manipulation.
	Vector<ObjectID> ids;
	ids.push_back(child->get_instance_id());
	context->set_selected_node_ids(ids);
	CHECK(context->get_selected_node_ids() == ids);

	context->activate(tree_root);
	CHECK(context->get_selection()->is_selected(child));

	context->deactivate();
	memdelete(context);
}

TEST_CASE("[SceneTree][Editor] EditorSceneContext drops freed nodes from the retained selection") {
	Window *tree_root = SceneTree::get_singleton()->get_root();

	EditorSceneContext *context = memnew(EditorSceneContext);
	Node2D *scene = memnew(Node2D);
	context->set_scene_root_node(scene);
	Node2D *child = memnew(Node2D);
	scene->add_child(child);

	context->activate(tree_root);
	context->get_selection()->add_node(child);
	context->deactivate();

	memdelete(child);

	context->activate(tree_root);
	CHECK(context->get_selection()->get_full_selected_node_list().is_empty());
	CHECK(context->get_selected_node_ids().is_empty());

	context->deactivate();
	memdelete(context);
}

TEST_CASE("[SceneTree][Editor] EditorSceneContext preserves inspector history across activation cycles") {
	Window *tree_root = SceneTree::get_singleton()->get_root();

	EditorSceneContext *context = memnew(EditorSceneContext);
	Node2D *scene = memnew(Node2D);
	context->set_scene_root_node(scene);

	context->activate(tree_root);

	EditorSelectionHistory *history = context->get_history();
	history->add_object(scene->get_instance_id());
	CHECK(history->get_history_len() == 1);

	context->deactivate();
	context->activate(tree_root);

	// History is owned by the context; no copy in or out happens on switches.
	CHECK(context->get_history() == history);
	CHECK(history->get_history_len() == 1);
	CHECK(history->get_current() == scene->get_instance_id());

	context->deactivate();
	memdelete(context);
}

TEST_CASE("[SceneTree][Editor] EditorData creates one context per edited scene") {
	EditorData editor_data;

	int index_a = editor_data.add_edited_scene(-1);
	int index_b = editor_data.add_edited_scene(-1);

	EditorSceneContext *context_a = editor_data.get_scene_context(index_a);
	EditorSceneContext *context_b = editor_data.get_scene_context(index_b);
	REQUIRE(context_a != nullptr);
	REQUIRE(context_b != nullptr);
	CHECK(context_a != context_b);
	CHECK(editor_data.get_scene_history_id(index_a) != editor_data.get_scene_history_id(index_b));

	editor_data.set_edited_scene(index_a);
	CHECK(editor_data.get_active_scene_context() == context_a);

	Node2D *scene = memnew(Node2D);
	editor_data.set_edited_scene_root(scene);
	CHECK(editor_data.get_edited_scene_root(index_a) == scene);
	CHECK(scene->get_parent() == context_a->get_viewport());

	editor_data.remove_scene(index_b);
	editor_data.remove_scene(index_a);
	CHECK(editor_data.get_edited_scene_count() == 0);
}

} // namespace TestEditorSceneContext
