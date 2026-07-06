/**************************************************************************/
/*  node_3d_editor_world_scope.h                                         */
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

#include "core/math/dynamic_bvh.h"
#include "core/templates/hash_map.h"

#include "scene/3d/node_3d.h"
#include "scene/resources/3d/world_3d.h"

class Node3DEditorViewport;

// Per-world gizmo BVH registry. Queries are filtered to the requesting view's world.
class Node3DEditorWorldGizmoBVH {
	HashMap<ObjectID, DynamicBVH *> bvhs;

	static ObjectID _world_key(const Ref<World3D> &p_world);
	DynamicBVH &_bvh_for_world(const Ref<World3D> &p_world);

public:
	~Node3DEditorWorldGizmoBVH();
	DynamicBVH::ID insert(Node3D *p_node, const AABB &p_aabb);
	void update(Node3D *p_node, DynamicBVH::ID p_id, const AABB &p_aabb);
	void remove(Node3D *p_node, DynamicBVH::ID p_id);
	void clear_world(const Ref<World3D> &p_world);
	void clear();

	Vector<Node3D *> ray_query(const Vector3 &p_ray_start, const Vector3 &p_ray_end, const Ref<World3D> &p_world);
	Vector<Node3D *> frustum_query(const Vector<Plane> &p_frustum, const Ref<World3D> &p_world);
};

// Focused-view routing for tile and secondary 3D views (#931 / U5).
struct Node3DEditorViewRouting {
	static Node3DEditorViewport *get_focused_viewport(
			Node3DEditorViewport *p_focused_viewport,
			Node3DEditorViewport *const *p_tile_viewports,
			uint32_t p_tile_viewport_count,
			int p_last_used_tile_viewport_index);
};
