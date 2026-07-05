/**************************************************************************/
/*  test_node_3d_editor_world_scope.h                                     */
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

#include "editor/editor_scene_context.h"
#include "editor/scene/3d/node_3d_editor_world_scope.h"

#include "scene/3d/node_3d.h"
#include "scene/main/window.h"
#include "scene/resources/3d/world_3d.h"

#include "tests/test_macros.h"

namespace TestNode3DEditorWorldScope {

TEST_CASE("[SceneTree][Editor] bvh-world-filter") {
	Window *tree_root = SceneTree::get_singleton()->get_root();

	EditorSceneContext *context_a = memnew(EditorSceneContext);
	EditorSceneContext *context_b = memnew(EditorSceneContext);
	Ref<World3D> world_a = context_a->get_world_3d();
	Ref<World3D> world_b = context_b->get_world_3d();

	Node3D *scene_a = memnew(Node3D);
	context_a->set_scene_root_node(scene_a);
	Node3D *node_a = memnew(Node3D);
	node_a->set_position(Vector3(0, 0, 0));
	scene_a->add_child(node_a);

	Node3D *scene_b = memnew(Node3D);
	context_b->set_scene_root_node(scene_b);
	Node3D *node_b = memnew(Node3D);
	node_b->set_position(Vector3(10, 0, 0));
	scene_b->add_child(node_b);

	context_a->activate(tree_root);
	context_b->activate(tree_root);
	REQUIRE(node_a->get_world_3d() == world_a);
	REQUIRE(node_b->get_world_3d() == world_b);

	Node3DEditorWorldGizmoBVH bvh;
	const AABB pick_aabb(Vector3(-1, -1, -1), Vector3(2, 2, 2));
	DynamicBVH::ID id_a = bvh.insert(node_a, pick_aabb);
	DynamicBVH::ID id_b = bvh.insert(node_b, pick_aabb);
	CHECK(id_a.is_valid());
	CHECK(id_b.is_valid());

	const Vector3 ray_start(0, 0, 5);
	const Vector3 ray_end(0, 0, -5);
	Vector<Node3D *> hits_a = bvh.ray_query(ray_start, ray_end, world_a);
	Vector<Node3D *> hits_b = bvh.ray_query(ray_start, ray_end, world_b);
	CHECK(hits_a.size() == 1);
	CHECK(hits_b.size() == 1);
	CHECK(hits_a[0] == node_a);
	CHECK(hits_b[0] == node_b);

	Vector<Plane> frustum;
	frustum.push_back(Plane(Vector3(0, 0, 1), 2));
	frustum.push_back(Plane(Vector3(0, 0, -1), 2));
	frustum.push_back(Plane(Vector3(1, 0, 0), 2));
	frustum.push_back(Plane(Vector3(-1, 0, 0), 2));
	frustum.push_back(Plane(Vector3(0, 1, 0), 2));
	frustum.push_back(Plane(Vector3(0, -1, 0), 2));

	Vector<Node3D *> frustum_a = bvh.frustum_query(frustum, world_a);
	Vector<Node3D *> frustum_b = bvh.frustum_query(frustum, world_b);
	CHECK(frustum_a.size() == 1);
	CHECK(frustum_b.size() == 1);
	CHECK(frustum_a[0] == node_a);
	CHECK(frustum_b[0] == node_b);

	Vector<Node3D *> misses = bvh.ray_query(Vector3(20, 0, 5), Vector3(20, 0, -5), world_a);
	CHECK(misses.is_empty());

	bvh.remove(node_a, id_a);
	bvh.remove(node_b, id_b);
	hits_a = bvh.ray_query(ray_start, ray_end, world_a);
	hits_b = bvh.ray_query(ray_start, ray_end, world_b);
	CHECK(hits_a.is_empty());
	CHECK(hits_b.is_empty());

	context_a->deactivate();
	context_b->deactivate();
	memdelete(context_a);
	memdelete(context_b);
}


} // namespace TestNode3DEditorWorldScope
