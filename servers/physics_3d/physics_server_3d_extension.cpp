/**************************************************************************/
/*  physics_server_3d_extension.cpp                                       */
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

#include "physics_server_3d_extension.h"

bool PhysicsDirectSpaceState3DExtension::is_body_excluded_from_query(const RID &p_body) const {
	return exclude && exclude->has(p_body);
}

thread_local const HashSet<RID> *PhysicsDirectSpaceState3DExtension::exclude = nullptr;

void PhysicsDirectSpaceState3DExtension::_bind_methods() {
	ClassDB::bind_method(D_METHOD("is_body_excluded_from_query", "body"), &PhysicsDirectSpaceState3DExtension::is_body_excluded_from_query);

	FOUNDRY_VIRTUAL_BIND(_intersect_ray, "from", "to", "collision_mask", "collide_with_bodies", "collide_with_areas", "hit_from_inside", "hit_back_faces", "pick_ray", "result");
	FOUNDRY_VIRTUAL_BIND(_intersect_point, "position", "collision_mask", "collide_with_bodies", "collide_with_areas", "results", "max_results");
	FOUNDRY_VIRTUAL_BIND(_intersect_shape, "shape_rid", "transform", "motion", "margin", "collision_mask", "collide_with_bodies", "collide_with_areas", "result_count", "max_results");
	FOUNDRY_VIRTUAL_BIND(_cast_motion, "shape_rid", "transform", "motion", "margin", "collision_mask", "collide_with_bodies", "collide_with_areas", "closest_safe", "closest_unsafe", "info");
	FOUNDRY_VIRTUAL_BIND(_collide_shape, "shape_rid", "transform", "motion", "margin", "collision_mask", "collide_with_bodies", "collide_with_areas", "results", "max_results", "result_count");
	FOUNDRY_VIRTUAL_BIND(_rest_info, "shape_rid", "transform", "motion", "margin", "collision_mask", "collide_with_bodies", "collide_with_areas", "rest_info");
	FOUNDRY_VIRTUAL_BIND(_get_closest_point_to_object_volume, "object", "point");
}

PhysicsDirectSpaceState3DExtension::PhysicsDirectSpaceState3DExtension() {
}

void PhysicsDirectBodyState3DExtension::_bind_methods() {
	FOUNDRY_VIRTUAL_BIND(_get_total_gravity);
	FOUNDRY_VIRTUAL_BIND(_get_total_linear_damp);
	FOUNDRY_VIRTUAL_BIND(_get_total_angular_damp);

	FOUNDRY_VIRTUAL_BIND(_get_center_of_mass);
	FOUNDRY_VIRTUAL_BIND(_get_center_of_mass_local);
	FOUNDRY_VIRTUAL_BIND(_get_principal_inertia_axes);

	FOUNDRY_VIRTUAL_BIND(_get_inverse_mass);
	FOUNDRY_VIRTUAL_BIND(_get_inverse_inertia);
	FOUNDRY_VIRTUAL_BIND(_get_inverse_inertia_tensor);

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
	FOUNDRY_VIRTUAL_BIND(_get_contact_impulse, "contact_idx");
	FOUNDRY_VIRTUAL_BIND(_get_contact_local_shape, "contact_idx");
	FOUNDRY_VIRTUAL_BIND(_get_contact_local_velocity_at_position, "contact_idx");
	FOUNDRY_VIRTUAL_BIND(_get_contact_collider, "contact_idx");
	FOUNDRY_VIRTUAL_BIND(_get_contact_collider_position, "contact_idx");
	FOUNDRY_VIRTUAL_BIND(_get_contact_collider_id, "contact_idx");
	FOUNDRY_VIRTUAL_BIND(_get_contact_collider_object, "contact_idx");
	FOUNDRY_VIRTUAL_BIND(_get_contact_collider_shape, "contact_idx");
	FOUNDRY_VIRTUAL_BIND(_get_contact_collider_velocity_at_position, "contact_idx");
	FOUNDRY_VIRTUAL_BIND(_get_step);
	FOUNDRY_VIRTUAL_BIND(_integrate_forces);
	FOUNDRY_VIRTUAL_BIND(_get_space_state);
}

PhysicsDirectBodyState3DExtension::PhysicsDirectBodyState3DExtension() {
}

thread_local const HashSet<RID> *PhysicsServer3DExtension::exclude_bodies = nullptr;
thread_local const HashSet<ObjectID> *PhysicsServer3DExtension::exclude_objects = nullptr;

bool PhysicsServer3DExtension::body_test_motion_is_excluding_body(RID p_body) const {
	return exclude_bodies && exclude_bodies->has(p_body);
}

bool PhysicsServer3DExtension::body_test_motion_is_excluding_object(ObjectID p_object) const {
	return exclude_objects && exclude_objects->has(p_object);
}

void PhysicsServer3DExtension::_bind_methods() {
	/* SHAPE API */

	FOUNDRY_VIRTUAL_BIND(_world_boundary_shape_create);
	FOUNDRY_VIRTUAL_BIND(_separation_ray_shape_create);
	FOUNDRY_VIRTUAL_BIND(_sphere_shape_create);
	FOUNDRY_VIRTUAL_BIND(_box_shape_create);
	FOUNDRY_VIRTUAL_BIND(_capsule_shape_create);
	FOUNDRY_VIRTUAL_BIND(_cylinder_shape_create);
	FOUNDRY_VIRTUAL_BIND(_convex_polygon_shape_create);
	FOUNDRY_VIRTUAL_BIND(_concave_polygon_shape_create);
	FOUNDRY_VIRTUAL_BIND(_heightmap_shape_create);
	FOUNDRY_VIRTUAL_BIND(_custom_shape_create);

	FOUNDRY_VIRTUAL_BIND(_shape_set_data, "shape", "data");
	FOUNDRY_VIRTUAL_BIND(_shape_set_custom_solver_bias, "shape", "bias");

	FOUNDRY_VIRTUAL_BIND(_shape_set_margin, "shape", "margin");
	FOUNDRY_VIRTUAL_BIND(_shape_get_margin, "shape");

	FOUNDRY_VIRTUAL_BIND(_shape_get_type, "shape");
	FOUNDRY_VIRTUAL_BIND(_shape_get_data, "shape");
	FOUNDRY_VIRTUAL_BIND(_shape_get_custom_solver_bias, "shape");

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

	FOUNDRY_VIRTUAL_BIND(_area_set_param, "area", "param", "value");
	FOUNDRY_VIRTUAL_BIND(_area_set_transform, "area", "transform");

	FOUNDRY_VIRTUAL_BIND(_area_get_param, "area", "param");
	FOUNDRY_VIRTUAL_BIND(_area_get_transform, "area");

	FOUNDRY_VIRTUAL_BIND(_area_set_collision_layer, "area", "layer");
	FOUNDRY_VIRTUAL_BIND(_area_get_collision_layer, "area");

	FOUNDRY_VIRTUAL_BIND(_area_set_collision_mask, "area", "mask");
	FOUNDRY_VIRTUAL_BIND(_area_get_collision_mask, "area");

	FOUNDRY_VIRTUAL_BIND(_area_set_monitorable, "area", "monitorable");
	FOUNDRY_VIRTUAL_BIND(_area_set_ray_pickable, "area", "enable");

	FOUNDRY_VIRTUAL_BIND(_area_set_monitor_callback, "area", "callback");
	FOUNDRY_VIRTUAL_BIND(_area_set_area_monitor_callback, "area", "callback");

	/* BODY API */

	ClassDB::bind_method(D_METHOD("body_test_motion_is_excluding_body", "body"), &PhysicsServer3DExtension::body_test_motion_is_excluding_body);
	ClassDB::bind_method(D_METHOD("body_test_motion_is_excluding_object", "object"), &PhysicsServer3DExtension::body_test_motion_is_excluding_object);

	FOUNDRY_VIRTUAL_BIND(_body_create);

	FOUNDRY_VIRTUAL_BIND(_body_set_space, "body", "space");
	FOUNDRY_VIRTUAL_BIND(_body_get_space, "body");

	FOUNDRY_VIRTUAL_BIND(_body_set_mode, "body", "mode");
	FOUNDRY_VIRTUAL_BIND(_body_get_mode, "body");

	FOUNDRY_VIRTUAL_BIND(_body_add_shape, "body", "shape", "transform", "disabled");
	FOUNDRY_VIRTUAL_BIND(_body_set_shape, "body", "shape_idx", "shape");
	FOUNDRY_VIRTUAL_BIND(_body_set_shape_transform, "body", "shape_idx", "transform");
	FOUNDRY_VIRTUAL_BIND(_body_set_shape_disabled, "body", "shape_idx", "disabled");

	FOUNDRY_VIRTUAL_BIND(_body_get_shape_count, "body");
	FOUNDRY_VIRTUAL_BIND(_body_get_shape, "body", "shape_idx");
	FOUNDRY_VIRTUAL_BIND(_body_get_shape_transform, "body", "shape_idx");

	FOUNDRY_VIRTUAL_BIND(_body_remove_shape, "body", "shape_idx");
	FOUNDRY_VIRTUAL_BIND(_body_clear_shapes, "body");

	FOUNDRY_VIRTUAL_BIND(_body_attach_object_instance_id, "body", "id");
	FOUNDRY_VIRTUAL_BIND(_body_get_object_instance_id, "body");

	FOUNDRY_VIRTUAL_BIND(_body_set_enable_continuous_collision_detection, "body", "enable");
	FOUNDRY_VIRTUAL_BIND(_body_is_continuous_collision_detection_enabled, "body");

	FOUNDRY_VIRTUAL_BIND(_body_set_collision_layer, "body", "layer");
	FOUNDRY_VIRTUAL_BIND(_body_get_collision_layer, "body");

	FOUNDRY_VIRTUAL_BIND(_body_set_collision_mask, "body", "mask");
	FOUNDRY_VIRTUAL_BIND(_body_get_collision_mask, "body");

	FOUNDRY_VIRTUAL_BIND(_body_set_collision_priority, "body", "priority");
	FOUNDRY_VIRTUAL_BIND(_body_get_collision_priority, "body");

	FOUNDRY_VIRTUAL_BIND(_body_set_user_flags, "body", "flags");
	FOUNDRY_VIRTUAL_BIND(_body_get_user_flags, "body");

	FOUNDRY_VIRTUAL_BIND(_body_set_param, "body", "param", "value");
	FOUNDRY_VIRTUAL_BIND(_body_get_param, "body", "param");

	FOUNDRY_VIRTUAL_BIND(_body_reset_mass_properties, "body");

	FOUNDRY_VIRTUAL_BIND(_body_set_state, "body", "state", "value");
	FOUNDRY_VIRTUAL_BIND(_body_get_state, "body", "state");

	FOUNDRY_VIRTUAL_BIND(_body_apply_central_impulse, "body", "impulse");
	FOUNDRY_VIRTUAL_BIND(_body_apply_impulse, "body", "impulse", "position");
	FOUNDRY_VIRTUAL_BIND(_body_apply_torque_impulse, "body", "impulse");

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

	FOUNDRY_VIRTUAL_BIND(_body_set_axis_lock, "body", "axis", "lock");
	FOUNDRY_VIRTUAL_BIND(_body_is_axis_locked, "body", "axis");

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

	FOUNDRY_VIRTUAL_BIND(_body_set_ray_pickable, "body", "enable");

	FOUNDRY_VIRTUAL_BIND(_body_test_motion, "body", "from", "motion", "margin", "max_collisions", "collide_separation_ray", "recovery_as_collision", "result");

	FOUNDRY_VIRTUAL_BIND(_body_get_direct_state, "body");

	/* SOFT BODY API */

	FOUNDRY_VIRTUAL_BIND(_soft_body_create);

	FOUNDRY_VIRTUAL_BIND(_soft_body_update_rendering_server, "body", "rendering_server_handler");

	FOUNDRY_VIRTUAL_BIND(_soft_body_set_space, "body", "space");
	FOUNDRY_VIRTUAL_BIND(_soft_body_get_space, "body");

	FOUNDRY_VIRTUAL_BIND(_soft_body_set_ray_pickable, "body", "enable");

	FOUNDRY_VIRTUAL_BIND(_soft_body_set_collision_layer, "body", "layer");
	FOUNDRY_VIRTUAL_BIND(_soft_body_get_collision_layer, "body");

	FOUNDRY_VIRTUAL_BIND(_soft_body_set_collision_mask, "body", "mask");
	FOUNDRY_VIRTUAL_BIND(_soft_body_get_collision_mask, "body");

	FOUNDRY_VIRTUAL_BIND(_soft_body_add_collision_exception, "body", "body_b");
	FOUNDRY_VIRTUAL_BIND(_soft_body_remove_collision_exception, "body", "body_b");
	FOUNDRY_VIRTUAL_BIND(_soft_body_get_collision_exceptions, "body");

	FOUNDRY_VIRTUAL_BIND(_soft_body_set_state, "body", "state", "variant");
	FOUNDRY_VIRTUAL_BIND(_soft_body_get_state, "body", "state");

	FOUNDRY_VIRTUAL_BIND(_soft_body_set_transform, "body", "transform");

	FOUNDRY_VIRTUAL_BIND(_soft_body_set_simulation_precision, "body", "simulation_precision");
	FOUNDRY_VIRTUAL_BIND(_soft_body_get_simulation_precision, "body");

	FOUNDRY_VIRTUAL_BIND(_soft_body_set_total_mass, "body", "total_mass");
	FOUNDRY_VIRTUAL_BIND(_soft_body_get_total_mass, "body");

	FOUNDRY_VIRTUAL_BIND(_soft_body_set_linear_stiffness, "body", "linear_stiffness");
	FOUNDRY_VIRTUAL_BIND(_soft_body_get_linear_stiffness, "body");

	FOUNDRY_VIRTUAL_BIND(_soft_body_set_shrinking_factor, "body", "shrinking_factor");
	FOUNDRY_VIRTUAL_BIND(_soft_body_get_shrinking_factor, "body");

	FOUNDRY_VIRTUAL_BIND(_soft_body_set_pressure_coefficient, "body", "pressure_coefficient");
	FOUNDRY_VIRTUAL_BIND(_soft_body_get_pressure_coefficient, "body");

	FOUNDRY_VIRTUAL_BIND(_soft_body_set_damping_coefficient, "body", "damping_coefficient");
	FOUNDRY_VIRTUAL_BIND(_soft_body_get_damping_coefficient, "body");

	FOUNDRY_VIRTUAL_BIND(_soft_body_set_drag_coefficient, "body", "drag_coefficient");
	FOUNDRY_VIRTUAL_BIND(_soft_body_get_drag_coefficient, "body");

	FOUNDRY_VIRTUAL_BIND(_soft_body_set_mesh, "body", "mesh");

	FOUNDRY_VIRTUAL_BIND(_soft_body_get_bounds, "body");

	FOUNDRY_VIRTUAL_BIND(_soft_body_move_point, "body", "point_index", "global_position");
	FOUNDRY_VIRTUAL_BIND(_soft_body_get_point_global_position, "body", "point_index");

	FOUNDRY_VIRTUAL_BIND(_soft_body_remove_all_pinned_points, "body");
	FOUNDRY_VIRTUAL_BIND(_soft_body_pin_point, "body", "point_index", "pin");
	FOUNDRY_VIRTUAL_BIND(_soft_body_is_point_pinned, "body", "point_index");

	FOUNDRY_VIRTUAL_BIND(_soft_body_apply_point_impulse, "body", "point_index", "impulse");
	FOUNDRY_VIRTUAL_BIND(_soft_body_apply_point_force, "body", "point_index", "force");
	FOUNDRY_VIRTUAL_BIND(_soft_body_apply_central_impulse, "body", "impulse");
	FOUNDRY_VIRTUAL_BIND(_soft_body_apply_central_force, "body", "force");

	/* JOINT API */

	FOUNDRY_VIRTUAL_BIND(_joint_create);
	FOUNDRY_VIRTUAL_BIND(_joint_clear, "joint");

	FOUNDRY_VIRTUAL_BIND(_joint_make_pin, "joint", "body_A", "local_A", "body_B", "local_B");

	FOUNDRY_VIRTUAL_BIND(_pin_joint_set_param, "joint", "param", "value");
	FOUNDRY_VIRTUAL_BIND(_pin_joint_get_param, "joint", "param");

	FOUNDRY_VIRTUAL_BIND(_pin_joint_set_local_a, "joint", "local_A");
	FOUNDRY_VIRTUAL_BIND(_pin_joint_get_local_a, "joint");

	FOUNDRY_VIRTUAL_BIND(_pin_joint_set_local_b, "joint", "local_B");
	FOUNDRY_VIRTUAL_BIND(_pin_joint_get_local_b, "joint");

	FOUNDRY_VIRTUAL_BIND(_joint_make_hinge, "joint", "body_A", "hinge_A", "body_B", "hinge_B");
	FOUNDRY_VIRTUAL_BIND(_joint_make_hinge_simple, "joint", "body_A", "pivot_A", "axis_A", "body_B", "pivot_B", "axis_B");

	FOUNDRY_VIRTUAL_BIND(_hinge_joint_set_param, "joint", "param", "value");
	FOUNDRY_VIRTUAL_BIND(_hinge_joint_get_param, "joint", "param");

	FOUNDRY_VIRTUAL_BIND(_hinge_joint_set_flag, "joint", "flag", "enabled");
	FOUNDRY_VIRTUAL_BIND(_hinge_joint_get_flag, "joint", "flag");

	FOUNDRY_VIRTUAL_BIND(_joint_make_slider, "joint", "body_A", "local_ref_A", "body_B", "local_ref_B");

	FOUNDRY_VIRTUAL_BIND(_slider_joint_set_param, "joint", "param", "value");
	FOUNDRY_VIRTUAL_BIND(_slider_joint_get_param, "joint", "param");

	FOUNDRY_VIRTUAL_BIND(_joint_make_cone_twist, "joint", "body_A", "local_ref_A", "body_B", "local_ref_B");

	FOUNDRY_VIRTUAL_BIND(_cone_twist_joint_set_param, "joint", "param", "value");
	FOUNDRY_VIRTUAL_BIND(_cone_twist_joint_get_param, "joint", "param");

	FOUNDRY_VIRTUAL_BIND(_joint_make_generic_6dof, "joint", "body_A", "local_ref_A", "body_B", "local_ref_B");

	FOUNDRY_VIRTUAL_BIND(_generic_6dof_joint_set_param, "joint", "axis", "param", "value");
	FOUNDRY_VIRTUAL_BIND(_generic_6dof_joint_get_param, "joint", "axis", "param");

	FOUNDRY_VIRTUAL_BIND(_generic_6dof_joint_set_flag, "joint", "axis", "flag", "enable");
	FOUNDRY_VIRTUAL_BIND(_generic_6dof_joint_get_flag, "joint", "axis", "flag");

	FOUNDRY_VIRTUAL_BIND(_joint_get_type, "joint");

	FOUNDRY_VIRTUAL_BIND(_joint_set_solver_priority, "joint", "priority");
	FOUNDRY_VIRTUAL_BIND(_joint_get_solver_priority, "joint");

	FOUNDRY_VIRTUAL_BIND(_joint_disable_collisions_between_bodies, "joint", "disable");
	FOUNDRY_VIRTUAL_BIND(_joint_is_disabled_collisions_between_bodies, "joint");

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

PhysicsServer3DExtension::PhysicsServer3DExtension() {
}

PhysicsServer3DExtension::~PhysicsServer3DExtension() {
}
