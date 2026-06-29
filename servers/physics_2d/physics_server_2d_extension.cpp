/**************************************************************************/
/*  physics_server_2d_extension.cpp                                       */
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

#include "physics_server_2d_extension.h"

bool PhysicsDirectSpaceState2DExtension::is_body_excluded_from_query(const RID &p_body) const {
	return exclude && exclude->has(p_body);
}

thread_local const HashSet<RID> *PhysicsDirectSpaceState2DExtension::exclude = nullptr;

void PhysicsDirectSpaceState2DExtension::_bind_methods() {
	ClassDB::bind_method(D_METHOD("is_body_excluded_from_query", "body"), &PhysicsDirectSpaceState2DExtension::is_body_excluded_from_query);

	FOUNDRY_VIRTUAL_BIND(_intersect_ray, "from", "to", "collision_mask", "collide_with_bodies", "collide_with_areas", "hit_from_inside", "result");
	FOUNDRY_VIRTUAL_BIND(_intersect_point, "position", "canvas_instance_id", "collision_mask", "collide_with_bodies", "collide_with_areas", "results", "max_results");
	FOUNDRY_VIRTUAL_BIND(_intersect_shape, "shape_rid", "transform", "motion", "margin", "collision_mask", "collide_with_bodies", "collide_with_areas", "result", "max_results");
	FOUNDRY_VIRTUAL_BIND(_cast_motion, "shape_rid", "transform", "motion", "margin", "collision_mask", "collide_with_bodies", "collide_with_areas", "closest_safe", "closest_unsafe");
	FOUNDRY_VIRTUAL_BIND(_collide_shape, "shape_rid", "transform", "motion", "margin", "collision_mask", "collide_with_bodies", "collide_with_areas", "results", "max_results", "result_count");
	FOUNDRY_VIRTUAL_BIND(_rest_info, "shape_rid", "transform", "motion", "margin", "collision_mask", "collide_with_bodies", "collide_with_areas", "rest_info");
}

PhysicsDirectSpaceState2DExtension::PhysicsDirectSpaceState2DExtension() {
}

void PhysicsDirectBodyState2DExtension::_bind_methods() {
	FOUNDRY_VIRTUAL_BIND(_get_total_gravity);
	FOUNDRY_VIRTUAL_BIND(_get_total_linear_damp);
	FOUNDRY_VIRTUAL_BIND(_get_total_angular_damp);

	FOUNDRY_VIRTUAL_BIND(_get_center_of_mass);
	FOUNDRY_VIRTUAL_BIND(_get_center_of_mass_local);
	FOUNDRY_VIRTUAL_BIND(_get_inverse_mass);
	FOUNDRY_VIRTUAL_BIND(_get_inverse_inertia);

	FOUNDRY_VIRTUAL_BIND(_set_linear_velocity, "velocity");
	FOUNDRY_VIRTUAL_BIND(_get_linear_velocity);

	FOUNDRY_VIRTUAL_BIND(_set_angular_velocity, "velocity");
	FOUNDRY_VIRTUAL_BIND(_get_angular_velocity);

	FOUNDRY_VIRTUAL_BIND(_set_transform, "transform");
	FOUNDRY_VIRTUAL_BIND(_get_transform);

	FOUNDRY_VIRTUAL_BIND(_get_velocity_at_local_position, "local_position");

	FOUNDRY_VIRTUAL_BIND(_apply_central_impulse, "impulse");
	FOUNDRY_VIRTUAL_BIND(_apply_impulse, "impulse", "position");
	FOUNDRY_VIRTUAL_BIND(_apply_torque_impulse, "impulse");

	FOUNDRY_VIRTUAL_BIND(_apply_central_force, "force");
	FOUNDRY_VIRTUAL_BIND(_apply_force, "force", "position");
	FOUNDRY_VIRTUAL_BIND(_apply_torque, "torque");

	FOUNDRY_VIRTUAL_BIND(_add_constant_central_force, "force");
	FOUNDRY_VIRTUAL_BIND(_add_constant_force, "force", "position");
	FOUNDRY_VIRTUAL_BIND(_add_constant_torque, "torque");

	FOUNDRY_VIRTUAL_BIND(_set_constant_force, "force");
	FOUNDRY_VIRTUAL_BIND(_get_constant_force);

	FOUNDRY_VIRTUAL_BIND(_set_constant_torque, "torque");
	FOUNDRY_VIRTUAL_BIND(_get_constant_torque);

	FOUNDRY_VIRTUAL_BIND(_set_sleep_state, "enabled");
	FOUNDRY_VIRTUAL_BIND(_is_sleeping);

	FOUNDRY_VIRTUAL_BIND(_set_collision_layer, "layer");
	FOUNDRY_VIRTUAL_BIND(_get_collision_layer);

	FOUNDRY_VIRTUAL_BIND(_set_collision_mask, "mask");
	FOUNDRY_VIRTUAL_BIND(_get_collision_mask);

	FOUNDRY_VIRTUAL_BIND(_get_contact_count);

	FOUNDRY_VIRTUAL_BIND(_get_contact_local_position, "contact_idx");
	FOUNDRY_VIRTUAL_BIND(_get_contact_local_normal, "contact_idx");
	FOUNDRY_VIRTUAL_BIND(_get_contact_local_shape, "contact_idx");
	FOUNDRY_VIRTUAL_BIND(_get_contact_local_velocity_at_position, "contact_idx");
	FOUNDRY_VIRTUAL_BIND(_get_contact_collider, "contact_idx");
	FOUNDRY_VIRTUAL_BIND(_get_contact_collider_position, "contact_idx");
	FOUNDRY_VIRTUAL_BIND(_get_contact_collider_id, "contact_idx");
	FOUNDRY_VIRTUAL_BIND(_get_contact_collider_object, "contact_idx");
	FOUNDRY_VIRTUAL_BIND(_get_contact_collider_shape, "contact_idx");
	FOUNDRY_VIRTUAL_BIND(_get_contact_collider_velocity_at_position, "contact_idx");
	FOUNDRY_VIRTUAL_BIND(_get_contact_impulse, "contact_idx");

	FOUNDRY_VIRTUAL_BIND(_get_step);
	FOUNDRY_VIRTUAL_BIND(_integrate_forces);

	FOUNDRY_VIRTUAL_BIND(_get_space_state);
}

PhysicsDirectBodyState2DExtension::PhysicsDirectBodyState2DExtension() {
}

thread_local const HashSet<RID> *PhysicsServer2DExtension::exclude_bodies = nullptr;
thread_local const HashSet<ObjectID> *PhysicsServer2DExtension::exclude_objects = nullptr;

bool PhysicsServer2DExtension::body_test_motion_is_excluding_body(RID p_body) const {
	return exclude_bodies && exclude_bodies->has(p_body);
}

bool PhysicsServer2DExtension::body_test_motion_is_excluding_object(ObjectID p_object) const {
	return exclude_objects && exclude_objects->has(p_object);
}

void PhysicsServer2DExtension::_bind_methods() {
	/* SHAPE API */

	FOUNDRY_VIRTUAL_BIND(_world_boundary_shape_create);
	FOUNDRY_VIRTUAL_BIND(_separation_ray_shape_create);
	FOUNDRY_VIRTUAL_BIND(_segment_shape_create);
	FOUNDRY_VIRTUAL_BIND(_circle_shape_create);
	FOUNDRY_VIRTUAL_BIND(_rectangle_shape_create);
	FOUNDRY_VIRTUAL_BIND(_capsule_shape_create);
	FOUNDRY_VIRTUAL_BIND(_convex_polygon_shape_create);
	FOUNDRY_VIRTUAL_BIND(_concave_polygon_shape_create);

	FOUNDRY_VIRTUAL_BIND(_shape_set_data, "shape", "data");
	FOUNDRY_VIRTUAL_BIND(_shape_set_custom_solver_bias, "shape", "bias");

	FOUNDRY_VIRTUAL_BIND(_shape_get_type, "shape");
	FOUNDRY_VIRTUAL_BIND(_shape_get_data, "shape");
	FOUNDRY_VIRTUAL_BIND(_shape_get_custom_solver_bias, "shape");
	FOUNDRY_VIRTUAL_BIND(_shape_collide, "shape_A", "xform_A", "motion_A", "shape_B", "xform_B", "motion_B", "results", "result_max", "result_count");

	/* SPACE API */

	FOUNDRY_VIRTUAL_BIND(_space_create);
	FOUNDRY_VIRTUAL_BIND(_space_set_active, "space", "active");
	FOUNDRY_VIRTUAL_BIND(_space_is_active, "space");

	FOUNDRY_VIRTUAL_BIND(_space_set_param, "space", "param", "value");
	FOUNDRY_VIRTUAL_BIND(_space_get_param, "space", "param");

	FOUNDRY_VIRTUAL_BIND(_space_get_direct_state, "space");

	FOUNDRY_VIRTUAL_BIND(_space_set_debug_contacts, "space", "max_contacts");
	FOUNDRY_VIRTUAL_BIND(_space_get_contacts, "space");
	FOUNDRY_VIRTUAL_BIND(_space_get_contact_count, "space");

	/* AREA API */

	FOUNDRY_VIRTUAL_BIND(_area_create);

	FOUNDRY_VIRTUAL_BIND(_area_set_space, "area", "space");
	FOUNDRY_VIRTUAL_BIND(_area_get_space, "area");

	FOUNDRY_VIRTUAL_BIND(_area_add_shape, "area", "shape", "transform", "disabled");
	FOUNDRY_VIRTUAL_BIND(_area_set_shape, "area", "shape_idx", "shape");
	FOUNDRY_VIRTUAL_BIND(_area_set_shape_transform, "area", "shape_idx", "transform");
	FOUNDRY_VIRTUAL_BIND(_area_set_shape_disabled, "area", "shape_idx", "disabled");

	FOUNDRY_VIRTUAL_BIND(_area_get_shape_count, "area");
	FOUNDRY_VIRTUAL_BIND(_area_get_shape, "area", "shape_idx");
	FOUNDRY_VIRTUAL_BIND(_area_get_shape_transform, "area", "shape_idx");

	FOUNDRY_VIRTUAL_BIND(_area_remove_shape, "area", "shape_idx");
	FOUNDRY_VIRTUAL_BIND(_area_clear_shapes, "area");

	FOUNDRY_VIRTUAL_BIND(_area_attach_object_instance_id, "area", "id");
	FOUNDRY_VIRTUAL_BIND(_area_get_object_instance_id, "area");

	FOUNDRY_VIRTUAL_BIND(_area_attach_canvas_instance_id, "area", "id");
	FOUNDRY_VIRTUAL_BIND(_area_get_canvas_instance_id, "area");

	FOUNDRY_VIRTUAL_BIND(_area_set_param, "area", "param", "value");
	FOUNDRY_VIRTUAL_BIND(_area_set_transform, "area", "transform");

	FOUNDRY_VIRTUAL_BIND(_area_get_param, "area", "param");
	FOUNDRY_VIRTUAL_BIND(_area_get_transform, "area");

	FOUNDRY_VIRTUAL_BIND(_area_set_collision_layer, "area", "layer");
	FOUNDRY_VIRTUAL_BIND(_area_get_collision_layer, "area");

	FOUNDRY_VIRTUAL_BIND(_area_set_collision_mask, "area", "mask");
	FOUNDRY_VIRTUAL_BIND(_area_get_collision_mask, "area");

	FOUNDRY_VIRTUAL_BIND(_area_set_monitorable, "area", "monitorable");
	FOUNDRY_VIRTUAL_BIND(_area_set_pickable, "area", "pickable");

	FOUNDRY_VIRTUAL_BIND(_area_set_monitor_callback, "area", "callback");
	FOUNDRY_VIRTUAL_BIND(_area_set_area_monitor_callback, "area", "callback");

	/* BODY API */

	ClassDB::bind_method(D_METHOD("body_test_motion_is_excluding_body", "body"), &PhysicsServer2DExtension::body_test_motion_is_excluding_body);
	ClassDB::bind_method(D_METHOD("body_test_motion_is_excluding_object", "object"), &PhysicsServer2DExtension::body_test_motion_is_excluding_object);

	FOUNDRY_VIRTUAL_BIND(_body_create);

	FOUNDRY_VIRTUAL_BIND(_body_set_space, "body", "space");
	FOUNDRY_VIRTUAL_BIND(_body_get_space, "body");

	FOUNDRY_VIRTUAL_BIND(_body_set_mode, "body", "mode");
	FOUNDRY_VIRTUAL_BIND(_body_get_mode, "body");

	FOUNDRY_VIRTUAL_BIND(_body_add_shape, "body", "shape", "transform", "disabled");
	FOUNDRY_VIRTUAL_BIND(_body_set_shape, "body", "shape_idx", "shape");
	FOUNDRY_VIRTUAL_BIND(_body_set_shape_transform, "body", "shape_idx", "transform");

	FOUNDRY_VIRTUAL_BIND(_body_get_shape_count, "body");
	FOUNDRY_VIRTUAL_BIND(_body_get_shape, "body", "shape_idx");
	FOUNDRY_VIRTUAL_BIND(_body_get_shape_transform, "body", "shape_idx");

	FOUNDRY_VIRTUAL_BIND(_body_set_shape_disabled, "body", "shape_idx", "disabled");
	FOUNDRY_VIRTUAL_BIND(_body_set_shape_as_one_way_collision, "body", "shape_idx", "enable", "margin");

	FOUNDRY_VIRTUAL_BIND(_body_remove_shape, "body", "shape_idx");
	FOUNDRY_VIRTUAL_BIND(_body_clear_shapes, "body");

	FOUNDRY_VIRTUAL_BIND(_body_attach_object_instance_id, "body", "id");
	FOUNDRY_VIRTUAL_BIND(_body_get_object_instance_id, "body");

	FOUNDRY_VIRTUAL_BIND(_body_attach_canvas_instance_id, "body", "id");
	FOUNDRY_VIRTUAL_BIND(_body_get_canvas_instance_id, "body");

	FOUNDRY_VIRTUAL_BIND(_body_set_continuous_collision_detection_mode, "body", "mode");
	FOUNDRY_VIRTUAL_BIND(_body_get_continuous_collision_detection_mode, "body");

	FOUNDRY_VIRTUAL_BIND(_body_set_collision_layer, "body", "layer");
	FOUNDRY_VIRTUAL_BIND(_body_get_collision_layer, "body");

	FOUNDRY_VIRTUAL_BIND(_body_set_collision_mask, "body", "mask");
	FOUNDRY_VIRTUAL_BIND(_body_get_collision_mask, "body");

	FOUNDRY_VIRTUAL_BIND(_body_set_collision_priority, "body", "priority");
	FOUNDRY_VIRTUAL_BIND(_body_get_collision_priority, "body");

	FOUNDRY_VIRTUAL_BIND(_body_set_param, "body", "param", "value");
	FOUNDRY_VIRTUAL_BIND(_body_get_param, "body", "param");

	FOUNDRY_VIRTUAL_BIND(_body_reset_mass_properties, "body");

	FOUNDRY_VIRTUAL_BIND(_body_set_state, "body", "state", "value");
	FOUNDRY_VIRTUAL_BIND(_body_get_state, "body", "state");

	FOUNDRY_VIRTUAL_BIND(_body_apply_central_impulse, "body", "impulse");
	FOUNDRY_VIRTUAL_BIND(_body_apply_torque_impulse, "body", "impulse");
	FOUNDRY_VIRTUAL_BIND(_body_apply_impulse, "body", "impulse", "position");

	FOUNDRY_VIRTUAL_BIND(_body_apply_central_force, "body", "force");
	FOUNDRY_VIRTUAL_BIND(_body_apply_force, "body", "force", "position");
	FOUNDRY_VIRTUAL_BIND(_body_apply_torque, "body", "torque");

	FOUNDRY_VIRTUAL_BIND(_body_add_constant_central_force, "body", "force");
	FOUNDRY_VIRTUAL_BIND(_body_add_constant_force, "body", "force", "position");
	FOUNDRY_VIRTUAL_BIND(_body_add_constant_torque, "body", "torque");

	FOUNDRY_VIRTUAL_BIND(_body_set_constant_force, "body", "force");
	FOUNDRY_VIRTUAL_BIND(_body_get_constant_force, "body");

	FOUNDRY_VIRTUAL_BIND(_body_set_constant_torque, "body", "torque");
	FOUNDRY_VIRTUAL_BIND(_body_get_constant_torque, "body");

	FOUNDRY_VIRTUAL_BIND(_body_set_axis_velocity, "body", "axis_velocity");

	FOUNDRY_VIRTUAL_BIND(_body_add_collision_exception, "body", "excepted_body");
	FOUNDRY_VIRTUAL_BIND(_body_remove_collision_exception, "body", "excepted_body");
	FOUNDRY_VIRTUAL_BIND(_body_get_collision_exceptions, "body");

	FOUNDRY_VIRTUAL_BIND(_body_set_max_contacts_reported, "body", "amount");
	FOUNDRY_VIRTUAL_BIND(_body_get_max_contacts_reported, "body");

	FOUNDRY_VIRTUAL_BIND(_body_set_contacts_reported_depth_threshold, "body", "threshold");
	FOUNDRY_VIRTUAL_BIND(_body_get_contacts_reported_depth_threshold, "body");

	FOUNDRY_VIRTUAL_BIND(_body_set_omit_force_integration, "body", "enable");
	FOUNDRY_VIRTUAL_BIND(_body_is_omitting_force_integration, "body");

	FOUNDRY_VIRTUAL_BIND(_body_set_state_sync_callback, "body", "callable");
	FOUNDRY_VIRTUAL_BIND(_body_set_force_integration_callback, "body", "callable", "userdata");

	FOUNDRY_VIRTUAL_BIND(_body_collide_shape, "body", "body_shape", "shape", "shape_xform", "motion", "results", "result_max", "result_count");

	FOUNDRY_VIRTUAL_BIND(_body_set_pickable, "body", "pickable");

	FOUNDRY_VIRTUAL_BIND(_body_get_direct_state, "body");

	FOUNDRY_VIRTUAL_BIND(_body_test_motion, "body", "from", "motion", "margin", "collide_separation_ray", "recovery_as_collision", "result");

	/* JOINT API */

	FOUNDRY_VIRTUAL_BIND(_joint_create);
	FOUNDRY_VIRTUAL_BIND(_joint_clear, "joint");

	FOUNDRY_VIRTUAL_BIND(_joint_set_param, "joint", "param", "value");
	FOUNDRY_VIRTUAL_BIND(_joint_get_param, "joint", "param");

	FOUNDRY_VIRTUAL_BIND(_joint_disable_collisions_between_bodies, "joint", "disable");
	FOUNDRY_VIRTUAL_BIND(_joint_is_disabled_collisions_between_bodies, "joint");

	FOUNDRY_VIRTUAL_BIND(_joint_make_pin, "joint", "anchor", "body_a", "body_b");
	FOUNDRY_VIRTUAL_BIND(_joint_make_groove, "joint", "a_groove1", "a_groove2", "b_anchor", "body_a", "body_b");
	FOUNDRY_VIRTUAL_BIND(_joint_make_damped_spring, "joint", "anchor_a", "anchor_b", "body_a", "body_b");

	FOUNDRY_VIRTUAL_BIND(_pin_joint_set_flag, "joint", "flag", "enabled");
	FOUNDRY_VIRTUAL_BIND(_pin_joint_get_flag, "joint", "flag");

	FOUNDRY_VIRTUAL_BIND(_pin_joint_set_param, "joint", "param", "value");
	FOUNDRY_VIRTUAL_BIND(_pin_joint_get_param, "joint", "param");

	FOUNDRY_VIRTUAL_BIND(_damped_spring_joint_set_param, "joint", "param", "value");
	FOUNDRY_VIRTUAL_BIND(_damped_spring_joint_get_param, "joint", "param");

	FOUNDRY_VIRTUAL_BIND(_joint_get_type, "joint");

	/* MISC */

	FOUNDRY_VIRTUAL_BIND(_free_rid, "rid");

	FOUNDRY_VIRTUAL_BIND(_set_active, "active");

	FOUNDRY_VIRTUAL_BIND(_init);
	FOUNDRY_VIRTUAL_BIND(_step, "step");
	FOUNDRY_VIRTUAL_BIND(_sync);
	FOUNDRY_VIRTUAL_BIND(_flush_queries);
	FOUNDRY_VIRTUAL_BIND(_end_sync);
	FOUNDRY_VIRTUAL_BIND(_finish);

	FOUNDRY_VIRTUAL_BIND(_is_flushing_queries);
	FOUNDRY_VIRTUAL_BIND(_get_process_info, "process_info");
}

PhysicsServer2DExtension::PhysicsServer2DExtension() {
}

PhysicsServer2DExtension::~PhysicsServer2DExtension() {
}
