/**************************************************************************/
/*  node_3d_editor_world_scope.cpp                                       */
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

#include "node_3d_editor_world_scope.h"

#include "editor/scene/3d/node_3d_editor_plugin.h"

#include "core/math/geometry_3d.h"

ObjectID Node3DEditorWorldGizmoBVH::_world_key(const Ref<World3D> &p_world) {
	return p_world.is_valid() ? p_world->get_instance_id() : ObjectID();
}

DynamicBVH &Node3DEditorWorldGizmoBVH::_bvh_for_world(const Ref<World3D> &p_world) {
	const ObjectID world_key = _world_key(p_world);
	DynamicBVH **existing = bvhs.getptr(world_key);
	if (existing) {
		return **existing;
	}
	DynamicBVH *created = memnew(DynamicBVH);
	bvhs.insert(world_key, created);
	return *created;
}

Node3DEditorWorldGizmoBVH::~Node3DEditorWorldGizmoBVH() {
	for (const KeyValue<ObjectID, DynamicBVH *> &entry : bvhs) {
		memdelete(entry.value);
	}
	bvhs.clear();
}

DynamicBVH::ID Node3DEditorWorldGizmoBVH::insert(Node3D *p_node, const AABB &p_aabb) {
	ERR_FAIL_NULL_V(p_node, DynamicBVH::ID());
	Ref<World3D> world = p_node->get_world_3d();
	return _bvh_for_world(world).insert(p_aabb, p_node);
}

void Node3DEditorWorldGizmoBVH::update(Node3D *p_node, DynamicBVH::ID p_id, const AABB &p_aabb) {
	ERR_FAIL_NULL(p_node);
	ERR_FAIL_COND(!p_id.is_valid());
	Ref<World3D> world = p_node->get_world_3d();
	DynamicBVH &bvh = _bvh_for_world(world);
	bvh.update(p_id, p_aabb);
	bvh.optimize_incremental(1);
}

void Node3DEditorWorldGizmoBVH::remove(Node3D *p_node, DynamicBVH::ID p_id) {
	ERR_FAIL_NULL(p_node);
	ERR_FAIL_COND(!p_id.is_valid());
	const ObjectID world_key = _world_key(p_node->get_world_3d());
	DynamicBVH **existing = bvhs.getptr(world_key);
	if (!existing) {
		return;
	}
	(*existing)->remove(p_id);
}

void Node3DEditorWorldGizmoBVH::clear_world(const Ref<World3D> &p_world) {
	const ObjectID world_key = _world_key(p_world);
	DynamicBVH **existing = bvhs.getptr(world_key);
	if (!existing) {
		return;
	}
	memdelete(*existing);
	bvhs.erase(world_key);
}

Vector<Node3D *> Node3DEditorWorldGizmoBVH::ray_query(const Vector3 &p_ray_start, const Vector3 &p_ray_end, const Ref<World3D> &p_world) {
	Vector<Node3D *> nodes;
	const ObjectID world_key = _world_key(p_world);
	DynamicBVH **existing = bvhs.getptr(world_key);
	if (!existing) {
		return nodes;
	}

	struct Result {
		Vector<Node3D *> *nodes = nullptr;
		bool operator()(void *p_data) {
			nodes->append((Node3D *)p_data);
			return false;
		}
	} result;
	result.nodes = &nodes;

	(*existing)->ray_query(p_ray_start, p_ray_end, result);
	return nodes;
}

Vector<Node3D *> Node3DEditorWorldGizmoBVH::frustum_query(const Vector<Plane> &p_frustum, const Ref<World3D> &p_world) {
	Vector<Node3D *> nodes;
	const ObjectID world_key = _world_key(p_world);
	DynamicBVH **existing = bvhs.getptr(world_key);
	if (!existing) {
		return nodes;
	}

	Vector<Vector3> points = Geometry3D::compute_convex_mesh_points(&p_frustum[0], p_frustum.size());

	struct Result {
		Vector<Node3D *> *nodes = nullptr;
		bool operator()(void *p_data) {
			nodes->append((Node3D *)p_data);
			return false;
		}
	} result;
	result.nodes = &nodes;

	(*existing)->convex_query(p_frustum.ptr(), p_frustum.size(), points.ptr(), points.size(), result);
	return nodes;
}

Node3DEditorViewport *Node3DEditorViewRouting::get_focused_viewport(
		Node3DEditorViewport *p_focused_viewport,
		Node3DEditorViewport *const *p_tile_viewports,
		uint32_t p_tile_viewport_count,
		int p_last_used_tile_viewport_index) {
	if (p_focused_viewport) {
		return p_focused_viewport;
	}
	if (p_tile_viewport_count == 0) {
		return nullptr;
	}
	ERR_FAIL_INDEX_V(p_last_used_tile_viewport_index, (int)p_tile_viewport_count, p_tile_viewports[0]);
	return p_tile_viewports[p_last_used_tile_viewport_index];
}
