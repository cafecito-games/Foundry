/**************************************************************************/
/*  test_multi_scene_phase_d.h                                            */
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

#include "editor/editor_scene_context.h"
#include "editor/editor_scene_pane_tile.h"
#include "editor/scene/3d/node_3d_editor_plugin.h"
#include "editor/scene/canvas_item_editor_plugin.h"

#include "scene/2d/node_2d.h"
#include "scene/3d/mesh_instance_3d.h"
#include "scene/3d/node_3d.h"
#include "scene/gui/box_container.h"
#include "scene/main/viewport.h"
#include "scene/main/window.h"
#include "scene/resources/3d/world_3d.h"

#include "tests/test_macros.h"

#include <type_traits>

namespace TestMultiScenePhaseD {

struct CanvasItemEditorCallbackAccess {
	static bool emitted_argument_callbacks_accept_bound_view_index_last() {
		return std::is_same_v<decltype(&CanvasItemEditor::_view_gui_input_viewport), void (CanvasItemEditor::*)(const Ref<InputEvent> &, int)> &&
				std::is_same_v<decltype(&CanvasItemEditor::_view_update_scroll), void (CanvasItemEditor::*)(real_t, int)> &&
				std::is_same_v<decltype(&CanvasItemEditor::_view_update_zoom), void (CanvasItemEditor::*)(real_t, int)> &&
				std::is_same_v<decltype(&CanvasItemEditor::_view_pan_callback), void (CanvasItemEditor::*)(Vector2, Ref<InputEvent>, int)> &&
				std::is_same_v<decltype(&CanvasItemEditor::_view_zoom_callback), void (CanvasItemEditor::*)(float, Vector2, Ref<InputEvent>, int)> &&
				std::is_same_v<decltype(&CanvasItemEditor::_view_selection_result_pressed), void (CanvasItemEditor::*)(int, int)>;
	}

	static bool has_registered_view(CanvasItemEditor *p_editor, const CanvasItemEditorView *p_view) {
		if (!p_editor) {
			return false;
		}
		for (CanvasItemEditorView *view : p_editor->views) {
			if (view == p_view) {
				return true;
			}
		}
		return false;
	}

	static int claim_view_slot(Vector<CanvasItemEditorView *> &p_views, CanvasItemEditorView *p_view) {
		return CanvasItemEditor::_claim_view_slot(p_views, p_view);
	}

	static bool release_view_slot(Vector<CanvasItemEditorView *> &p_views, const CanvasItemEditorView *p_focused_view, CanvasItemEditorView *p_view) {
		return CanvasItemEditor::_release_view_slot(p_views, p_focused_view, p_view);
	}
};

TEST_CASE("[SceneTree][Editor] canvas-view-bound-callback-signatures") {
	CHECK(CanvasItemEditorCallbackAccess::emitted_argument_callbacks_accept_bound_view_index_last());
}

TEST_CASE("[SceneTree][Editor] canvas-view-registry-keeps-stable-slots") {
	CanvasItemEditorView primary(nullptr);
	CanvasItemEditorView view_a(nullptr);
	CanvasItemEditorView view_b(nullptr);
	CanvasItemEditorView replacement(nullptr);
	Vector<CanvasItemEditorView *> views;

	CHECK(CanvasItemEditorCallbackAccess::claim_view_slot(views, &primary) == 0);
	CHECK(CanvasItemEditorCallbackAccess::claim_view_slot(views, &view_a) == 1);
	CHECK(CanvasItemEditorCallbackAccess::claim_view_slot(views, &view_b) == 2);

	CHECK_FALSE(CanvasItemEditorCallbackAccess::release_view_slot(views, &primary, &primary));
	CHECK(CanvasItemEditorCallbackAccess::release_view_slot(views, &primary, &view_a));
	REQUIRE(views.size() == 3);
	CHECK(views[0] == &primary);
	CHECK(views[1] == nullptr);
	CHECK(views[2] == &view_b);

	CHECK(CanvasItemEditorCallbackAccess::claim_view_slot(views, &replacement) == 1);
	REQUIRE(views.size() == 3);
	CHECK(views[0] == &primary);
	CHECK(views[1] == &replacement);
	CHECK(views[2] == &view_b);
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

	MeshInstance3D *mesh_a = memnew(MeshInstance3D);
	MeshInstance3D *mesh_b = memnew(MeshInstance3D);
	context_a->set_scene_root_node(mesh_a);
	context_b->set_scene_root_node(mesh_b);

	Window *tree_root = SceneTree::get_singleton()->get_root();
	context_a->activate(tree_root);
	context_b->activate(tree_root);

	CHECK(mesh_a->get_world_3d() == world_a);
	CHECK(mesh_b->get_world_3d() == world_b);

	context_a->deactivate();
	context_b->deactivate();

	const ObjectID world_a_id = world_a->get_instance_id();
	const ObjectID world_b_id = world_b->get_instance_id();
	world_a.unref();
	world_b.unref();

	memdelete(context_a);
	memdelete(context_b);

	CHECK_FALSE(ObjectDB::get_instance(world_a_id));
	CHECK_FALSE(ObjectDB::get_instance(world_b_id));
}

TEST_CASE("[SceneTree][Editor] world-rebind") {
	Window *tree_root = SceneTree::get_singleton()->get_root();

	SubViewport *viewport_a = memnew(SubViewport);
	SubViewport *viewport_b = memnew(SubViewport);
	Ref<World3D> world_a;
	world_a.instantiate();
	Ref<World3D> world_b;
	world_b.instantiate();
	viewport_a->set_world_3d(world_a);
	viewport_b->set_world_3d(world_b);
	tree_root->add_child(viewport_a);
	tree_root->add_child(viewport_b);

	MeshInstance3D *mesh = memnew(MeshInstance3D);
	viewport_a->add_child(mesh);
	CHECK(mesh->get_world_3d() == world_a);

	viewport_a->remove_child(mesh);
	viewport_b->add_child(mesh);
	CHECK(mesh->get_world_3d() == world_b);

	viewport_b->remove_child(mesh);
	memdelete(mesh);
	tree_root->remove_child(viewport_a);
	tree_root->remove_child(viewport_b);
	memdelete(viewport_a);
	memdelete(viewport_b);
}

TEST_CASE("[SceneTree][Editor] preview-camera-state") {
	ScenePaneTile *tile = memnew(ScenePaneTile);
	EditorSelection selection;
	EditorData editor_data;
	tile->setup(0, &selection, editor_data);

	Dictionary viewport_state;
	viewport_state["position"] = Vector3(1, 2, 3);
	viewport_state["x_rotation"] = 0.0;
	viewport_state["y_rotation"] = 0.0;
	viewport_state["distance"] = 8.0;
	tile->apply_3d_preview_camera_state(viewport_state);

	Camera3D *camera = tile->get_preview_3d_camera();
	REQUIRE(camera != nullptr);
	const Vector3 expected_origin = camera->get_transform().origin;
	CHECK(expected_origin.is_equal_approx(Vector3(1, 2, 11)));

	memdelete(tile);
}

TEST_CASE("[SceneTree][Editor] canvas-view-bind") {
	EditorSceneContext *context = memnew(EditorSceneContext);
	Node2D *scene = memnew(Node2D);
	context->set_scene_root_node(scene);

	Window *tree_root = SceneTree::get_singleton()->get_root();
	context->activate(tree_root);

	CanvasItemEditorView view(nullptr);
	view.bind_context(context);
	Dictionary state;
	state["zoom"] = 2.0;
	state["ofs"] = Vector2(100, 50);
	view.set_view_state(state);
	view.push_viewport_state();

	const Transform2D pushed = context->get_viewport()->get_global_canvas_transform();
	CHECK(pushed.get_scale().x > 1.5);

	context->deactivate();
	memdelete(context);
}

TEST_CASE("[SceneTree][Editor] bvh-world-filter") {
	Node3DEditor *editor = Node3DEditor::get_singleton();
	if (!editor) {
		return;
	}

	Window *tree_root = SceneTree::get_singleton()->get_root();

	SubViewport *viewport_a = memnew(SubViewport);
	SubViewport *viewport_b = memnew(SubViewport);
	Ref<World3D> world_a;
	world_a.instantiate();
	Ref<World3D> world_b;
	world_b.instantiate();
	viewport_a->set_world_3d(world_a);
	viewport_b->set_world_3d(world_b);
	tree_root->add_child(viewport_a);
	tree_root->add_child(viewport_b);

	Node3D *node_a = memnew(Node3D);
	Node3D *node_b = memnew(Node3D);
	viewport_a->add_child(node_a);
	viewport_b->add_child(node_b);

	const AABB bounds(Vector3(), Vector3(1, 1, 1));
	DynamicBVH::ID id_a = editor->insert_gizmo_bvh_node(node_a, bounds);
	DynamicBVH::ID id_b = editor->insert_gizmo_bvh_node(node_b, bounds);

	const Vector3 ray_start = Vector3(-1, 0.5, 0.5);
	const Vector3 ray_end = Vector3(2, 0.5, 0.5);

	Vector<Node3D *> filtered_a = editor->gizmo_bvh_ray_query(ray_start, ray_end, world_a);
	bool found_a = false;
	bool found_foreign = false;
	for (Node3D *node : filtered_a) {
		if (node == node_a) {
			found_a = true;
		}
		if (node == node_b) {
			found_foreign = true;
		}
	}
	CHECK(found_a);
	CHECK_FALSE(found_foreign);

	editor->remove_gizmo_bvh_node(id_a);
	editor->remove_gizmo_bvh_node(id_b);

	viewport_a->remove_child(node_a);
	viewport_b->remove_child(node_b);
	memdelete(node_a);
	memdelete(node_b);
	tree_root->remove_child(viewport_a);
	tree_root->remove_child(viewport_b);
	memdelete(viewport_a);
	memdelete(viewport_b);
}

} // namespace TestMultiScenePhaseD
