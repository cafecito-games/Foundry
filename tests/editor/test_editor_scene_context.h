/**************************************************************************/
/*  test_editor_scene_context.h                                           */
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
#include "editor/editor_scene_context.h"

#include "scene/2d/node_2d.h"
#include "scene/3d/mesh_instance_3d.h"
#include "scene/3d/node_3d.h"
#include "scene/main/viewport.h"
#include "scene/main/window.h"
#include "scene/resources/3d/world_3d.h"
#include "scene/resources/mesh.h"

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

TEST_CASE("[SceneTree][Editor] EditorSceneContext supports in-place root replacement") {
	Window *tree_root = SceneTree::get_singleton()->get_root();

	EditorSceneContext *context = memnew(EditorSceneContext);
	Node2D *old_root = memnew(Node2D);
	context->set_scene_root_node(old_root);
	context->activate(tree_root);

	// Mirrors SceneTreeDock's change-root-type flow: the context is told
	// about the new root first, then replace_by moves it into the old root's
	// parent slot. The old root must keep its parent until replace_by runs.
	Node2D *new_root = memnew(Node2D);
	context->set_scene_root_node(new_root, false);
	CHECK(old_root->get_parent() == context->get_viewport());
	old_root->replace_by(new_root, true);

	CHECK(context->get_scene_root_node() == new_root);
	CHECK(new_root->get_parent() == context->get_viewport());
	CHECK(old_root->get_parent() == nullptr);

	memdelete(old_root);
	context->deactivate();
	memdelete(context);
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

	// Reparenting the viewport between display containers preserves the selection.
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
	memdelete(scene_2d);
	CHECK(context->scene_has_3d_content());

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

TEST_CASE("[SceneTree][Editor] context-own-world") {
	EditorSceneContext *context_a = memnew(EditorSceneContext);
	EditorSceneContext *context_b = memnew(EditorSceneContext);

	Ref<World3D> world_a = context_a->get_world_3d();
	Ref<World3D> world_b = context_b->get_world_3d();
	REQUIRE(world_a.is_valid());
	REQUIRE(world_b.is_valid());
	CHECK(world_a != world_b);
	CHECK(world_a->get_scenario() != world_b->get_scenario());

	Window *tree_root = SceneTree::get_singleton()->get_root();

	Node3D *scene_a = memnew(Node3D);
	context_a->set_scene_root_node(scene_a);
	MeshInstance3D *mesh_a = memnew(MeshInstance3D);
	scene_a->add_child(mesh_a);
	Ref<ArrayMesh> mesh_resource;
	mesh_resource.instantiate();
	mesh_a->set_mesh(mesh_resource);

	Node3D *scene_b = memnew(Node3D);
	context_b->set_scene_root_node(scene_b);
	MeshInstance3D *mesh_b = memnew(MeshInstance3D);
	scene_b->add_child(mesh_b);
	mesh_b->set_mesh(mesh_resource);

	context_a->activate(tree_root);
	CHECK(mesh_a->get_world_3d() == world_a);
	CHECK(mesh_a->get_instance().is_valid());

	context_a->deactivate();
	context_b->activate(tree_root);
	CHECK(mesh_b->get_world_3d() == world_b);
	CHECK(mesh_b->get_instance().is_valid());

	context_b->deactivate();

	world_a.unref();
	world_b.unref();
	memdelete(context_a);
	memdelete(context_b);
	CHECK_FALSE(world_a.is_valid());
	CHECK_FALSE(world_b.is_valid());
}

TEST_CASE("[SceneTree][Editor] world-rebind") {
	Window *tree_root = SceneTree::get_singleton()->get_root();

	EditorSceneContext *context_a = memnew(EditorSceneContext);
	EditorSceneContext *context_b = memnew(EditorSceneContext);
	Ref<World3D> world_a = context_a->get_world_3d();
	Ref<World3D> world_b = context_b->get_world_3d();

	Node3D *scene_a = memnew(Node3D);
	context_a->set_scene_root_node(scene_a);
	Node3D *scene_b = memnew(Node3D);
	context_b->set_scene_root_node(scene_b);

	MeshInstance3D *mesh = memnew(MeshInstance3D);
	scene_a->add_child(mesh);
	Ref<ArrayMesh> mesh_resource;
	mesh_resource.instantiate();
	mesh->set_mesh(mesh_resource);

	context_a->activate(tree_root);
	REQUIRE(mesh->is_inside_tree());
	CHECK(mesh->get_world_3d() == world_a);

	scene_a->remove_child(mesh);
	CHECK_FALSE(mesh->is_inside_tree());

	context_a->deactivate();
	context_b->activate(tree_root);
	scene_b->add_child(mesh);
	REQUIRE(mesh->is_inside_tree());
	CHECK(mesh->get_world_3d() == world_b);

	context_b->deactivate();
	memdelete(context_a);
	memdelete(context_b);
}

} // namespace TestEditorSceneContext
