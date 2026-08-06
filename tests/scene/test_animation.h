/**************************************************************************/
/*  test_animation.h                                                      */
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

#include "scene/resources/animation.h"

#include "tests/test_macros.h"

namespace TestAnimation {

TEST_CASE("[Animation] Empty animation getters") {
	const Ref<Animation> animation = memnew(Animation);

	CHECK(animation->get_length() == doctest::Approx(real_t(1.0)));
	CHECK(animation->get_step() == doctest::Approx(real_t(1.0 / 30)));
}

TEST_CASE("[Animation] Create value track") {
	// This creates an animation that makes the node "Enemy" move to the right by
	// 100 pixels in 0.5 seconds.
	Ref<Animation> animation = memnew(Animation);
	const int track_index = animation->add_track(Animation::TYPE_VALUE);
	CHECK(track_index == 0);
	animation->track_set_path(track_index, NodePath("Enemy:position:x"));
	animation->track_insert_key(track_index, 0.0, 0);
	animation->track_insert_key(track_index, 0.5, 100);

	CHECK(animation->get_track_count() == 1);
	CHECK(!animation->track_is_compressed(0));
	CHECK(int(animation->track_get_key_value(0, 0)) == 0);
	CHECK(int(animation->track_get_key_value(0, 1)) == 100);

	CHECK(animation->value_track_interpolate(0, -0.2) == doctest::Approx(0.0));
	CHECK(animation->value_track_interpolate(0, 0.0) == doctest::Approx(0.0));
	CHECK(animation->value_track_interpolate(0, 0.2) == doctest::Approx(40.0));
	CHECK(animation->value_track_interpolate(0, 0.4) == doctest::Approx(80.0));
	CHECK(animation->value_track_interpolate(0, 0.5) == doctest::Approx(100.0));
	CHECK(animation->value_track_interpolate(0, 0.6) == doctest::Approx(100.0));

	CHECK(animation->track_get_key_transition(0, 0) == doctest::Approx(real_t(1.0)));
	CHECK(animation->track_get_key_transition(0, 1) == doctest::Approx(real_t(1.0)));

	ERR_PRINT_OFF;
	// Nonexistent keys.
	CHECK(animation->track_get_key_value(0, 2).is_null());
	CHECK(animation->track_get_key_value(0, -1).is_null());
	CHECK(animation->track_get_key_transition(0, 2) == doctest::Approx(real_t(-1.0)));
	// Nonexistent track (and keys).
	CHECK(animation->track_get_key_value(1, 0).is_null());
	CHECK(animation->track_get_key_value(1, 1).is_null());
	CHECK(animation->track_get_key_value(1, 2).is_null());
	CHECK(animation->track_get_key_value(1, -1).is_null());
	CHECK(animation->track_get_key_transition(1, 0) == doctest::Approx(real_t(-1.0)));

	// This is a value track, so the methods below should return errors.
	CHECK(animation->try_position_track_interpolate(0, 0.0, nullptr) == ERR_INVALID_PARAMETER);
	CHECK(animation->try_rotation_track_interpolate(0, 0.0, nullptr) == ERR_INVALID_PARAMETER);
	CHECK(animation->try_scale_track_interpolate(0, 0.0, nullptr) == ERR_INVALID_PARAMETER);
	CHECK(animation->bezier_track_interpolate(0, 0.0) == doctest::Approx(0.0));
	CHECK(animation->try_blend_shape_track_interpolate(0, 0.0, nullptr) == ERR_INVALID_PARAMETER);
	ERR_PRINT_ON;
}

TEST_CASE("[Animation] Interpolate uint value track like the equivalent int value track") {
	// Regression test: a uint value track (e.g. an unsigned layer/mask property) must
	// interpolate identically to an int value track with the same numeric key values,
	// instead of snapping to the discrete midpoint.
	Ref<Animation> animation = memnew(Animation);

	const int uint_track = animation->add_track(Animation::TYPE_VALUE);
	animation->track_set_path(uint_track, NodePath("Enemy:mask"));
	animation->track_insert_key(uint_track, 0.0, Variant(uint64_t(0)));
	animation->track_insert_key(uint_track, 0.5, Variant(uint64_t(100)));

	const int int_track = animation->add_track(Animation::TYPE_VALUE);
	animation->track_set_path(int_track, NodePath("Enemy:position:x"));
	animation->track_insert_key(int_track, 0.0, 0);
	animation->track_insert_key(int_track, 0.5, 100);

	CHECK(animation->track_get_key_value(uint_track, 0).get_type() == Variant::UINT);
	CHECK(animation->track_get_key_value(uint_track, 1).get_type() == Variant::UINT);

	const double sample_times[] = { -0.2, 0.0, 0.2, 0.4, 0.5, 0.6 };
	for (double time : sample_times) {
		const Variant uint_value = animation->value_track_interpolate(uint_track, time);
		const Variant int_value = animation->value_track_interpolate(int_track, time);
		CHECK(double(uint_value) == doctest::Approx(double(int_value)));
	}

	// Explicit non-snapping check: halfway through the animation, the uint track must
	// have lerped to 40, not snapped to either endpoint (0 or 100).
	CHECK(animation->value_track_interpolate(uint_track, 0.2) == doctest::Approx(40.0));
}

TEST_CASE("[Animation] validate_type_match reconciles uint against int and float") {
	// Tween::interpolate_property() and AnimationTree property blending both rely on
	// validate_type_match() to reconcile a property's current value type with the
	// value type of an incoming key; uint-typed properties must be reconciled the
	// same way int-typed properties already are.
	Variant from_uint = Variant(uint64_t(5));
	Variant to_int = 7;
	CHECK(Animation::validate_type_match(from_uint, to_int));
	CHECK(to_int.get_type() == Variant::UINT);
	CHECK(uint64_t(to_int) == 7);

	Variant from_int = 5;
	Variant to_uint = Variant(uint64_t(7));
	CHECK(Animation::validate_type_match(from_int, to_uint));
	CHECK(to_uint.get_type() == Variant::INT);
	CHECK(int64_t(to_uint) == 7);

	Variant from_uint_float = Variant(uint64_t(5));
	Variant to_float = 7.0;
	CHECK(Animation::validate_type_match(from_uint_float, to_float));
	CHECK(to_float.get_type() == Variant::UINT);
	CHECK(uint64_t(to_float) == 7);

	Variant from_float = 5.0;
	Variant to_uint_float = Variant(uint64_t(7));
	CHECK(Animation::validate_type_match(from_float, to_uint_float));
	CHECK(to_uint_float.get_type() == Variant::FLOAT);
	CHECK(double(to_uint_float) == doctest::Approx(7.0));

	ERR_PRINT_OFF;
	Variant from_uint_string = Variant(uint64_t(5));
	Variant to_string = String("mismatch");
	CHECK_FALSE(Animation::validate_type_match(from_uint_string, to_string));
	ERR_PRINT_ON;
}

TEST_CASE("[Animation] cast_to_blendwise and cast_from_blendwise round-trip uint like int") {
	const Variant uint_value = Variant(uint64_t(100));
	const Variant blendwise = Animation::cast_to_blendwise(uint_value);
	CHECK(blendwise.get_type() == Variant::FLOAT);
	CHECK(double(blendwise) == doctest::Approx(100.0));

	const Variant round_tripped = Animation::cast_from_blendwise(blendwise, Variant::UINT);
	CHECK(round_tripped.get_type() == Variant::UINT);
	CHECK(uint64_t(round_tripped) == 100);
}

TEST_CASE("[Animation] add_variant, subtract_variant, and blend_variant treat uint like int") {
	const Variant uint_a = Variant(uint64_t(10));
	const Variant uint_b = Variant(uint64_t(4));
	const Variant int_a = int64_t(10);
	const Variant int_b = int64_t(4);

	const Variant uint_sum = Animation::add_variant(uint_a, uint_b);
	const Variant int_sum = Animation::add_variant(int_a, int_b);
	CHECK(uint_sum.get_type() == Variant::UINT);
	CHECK(double(uint_sum) == doctest::Approx(double(int_sum)));

	const Variant uint_difference = Animation::subtract_variant(uint_a, uint_b);
	const Variant int_difference = Animation::subtract_variant(int_a, int_b);
	CHECK(uint_difference.get_type() == Variant::UINT);
	CHECK(double(uint_difference) == doctest::Approx(double(int_difference)));

	const Variant uint_blend = Animation::blend_variant(uint_a, uint_b, 0.5);
	const Variant int_blend = Animation::blend_variant(int_a, int_b, 0.5);
	CHECK(uint_blend.get_type() == Variant::UINT);
	CHECK(double(uint_blend) == doctest::Approx(double(int_blend)));
}

TEST_CASE("[Animation] cubic_interpolate_in_time_variant treats uint like int") {
	const Variant uint_pre_a = Variant(uint64_t(0));
	const Variant uint_a = Variant(uint64_t(0));
	const Variant uint_b = Variant(uint64_t(100));
	const Variant uint_post_b = Variant(uint64_t(100));

	const Variant int_pre_a = int64_t(0);
	const Variant int_a = int64_t(0);
	const Variant int_b = int64_t(100);
	const Variant int_post_b = int64_t(100);

	const Variant uint_result = Animation::cubic_interpolate_in_time_variant(uint_pre_a, uint_a, uint_b, uint_post_b, 0.5, 0.5, 0.5, 0.5);
	const Variant int_result = Animation::cubic_interpolate_in_time_variant(int_pre_a, int_a, int_b, int_post_b, 0.5, 0.5, 0.5, 0.5);
	CHECK(uint_result.get_type() == Variant::UINT);
	CHECK(double(uint_result) == doctest::Approx(double(int_result)));
}

TEST_CASE("[Animation] is_variant_interpolatable treats uint as interpolatable") {
	CHECK(Animation::is_variant_interpolatable(Variant(uint64_t(5))));
}

TEST_CASE("[Animation] Uint value track with angle interpolation does not overflow the type mask") {
	// Regression test: _interpolate_angle()/_cubic_interpolate_angle_in_time() build a
	// bitmask via `1 << type`. Variant::UINT sits past the packed array block in the
	// enum, so an unguarded shift would be undefined behavior for a uint-typed angle
	// track. This exercises that path end to end through the public API.
	Ref<Animation> animation = memnew(Animation);
	const int track_index = animation->add_track(Animation::TYPE_VALUE);
	animation->track_set_path(track_index, NodePath("Enemy:mask"));
	animation->track_set_interpolation_type(track_index, Animation::INTERPOLATION_LINEAR_ANGLE);
	animation->track_insert_key(track_index, 0.0, Variant(uint64_t(0)));
	animation->track_insert_key(track_index, 0.5, Variant(uint64_t(100)));

	const Variant result = animation->value_track_interpolate(track_index, 0.25);
	CHECK(result.get_type() == Variant::UINT);
}

TEST_CASE("[Animation] Create 3D position track") {
	Ref<Animation> animation = memnew(Animation);
	const int track_index = animation->add_track(Animation::TYPE_POSITION_3D);
	animation->track_set_path(track_index, NodePath("Enemy:position"));
	animation->position_track_insert_key(track_index, 0.0, Vector3(0, 1, 2));
	animation->position_track_insert_key(track_index, 0.5, Vector3(3.5, 4, 5));

	CHECK(animation->get_track_count() == 1);
	CHECK(!animation->track_is_compressed(0));
	CHECK(Vector3(animation->track_get_key_value(0, 0)).is_equal_approx(Vector3(0, 1, 2)));
	CHECK(Vector3(animation->track_get_key_value(0, 1)).is_equal_approx(Vector3(3.5, 4, 5)));

	Vector3 r_interpolation;

	CHECK(animation->try_position_track_interpolate(0, -0.2, &r_interpolation) == OK);
	CHECK(r_interpolation.is_equal_approx(Vector3(0, 1, 2)));

	CHECK(animation->try_position_track_interpolate(0, 0.0, &r_interpolation) == OK);
	CHECK(r_interpolation.is_equal_approx(Vector3(0, 1, 2)));

	CHECK(animation->try_position_track_interpolate(0, 0.2, &r_interpolation) == OK);
	CHECK(r_interpolation.is_equal_approx(Vector3(1.4, 2.2, 3.2)));

	CHECK(animation->try_position_track_interpolate(0, 0.4, &r_interpolation) == OK);
	CHECK(r_interpolation.is_equal_approx(Vector3(2.8, 3.4, 4.4)));

	CHECK(animation->try_position_track_interpolate(0, 0.5, &r_interpolation) == OK);
	CHECK(r_interpolation.is_equal_approx(Vector3(3.5, 4, 5)));

	CHECK(animation->try_position_track_interpolate(0, 0.6, &r_interpolation) == OK);
	CHECK(r_interpolation.is_equal_approx(Vector3(3.5, 4, 5)));

	// 3D position tracks always use linear interpolation for performance reasons.
	CHECK(animation->track_get_key_transition(0, 0) == doctest::Approx(real_t(1.0)));
	CHECK(animation->track_get_key_transition(0, 1) == doctest::Approx(real_t(1.0)));

	// This is a 3D position track, so the methods below should return errors.
	ERR_PRINT_OFF;
	CHECK(animation->value_track_interpolate(0, 0.0).is_null());
	CHECK(animation->try_rotation_track_interpolate(0, 0.0, nullptr) == ERR_INVALID_PARAMETER);
	CHECK(animation->try_scale_track_interpolate(0, 0.0, nullptr) == ERR_INVALID_PARAMETER);
	CHECK(animation->bezier_track_interpolate(0, 0.0) == doctest::Approx(0.0));
	CHECK(animation->try_blend_shape_track_interpolate(0, 0.0, nullptr) == ERR_INVALID_PARAMETER);
	ERR_PRINT_ON;
}

TEST_CASE("[Animation] Create 3D rotation track") {
	Ref<Animation> animation = memnew(Animation);
	const int track_index = animation->add_track(Animation::TYPE_ROTATION_3D);
	animation->track_set_path(track_index, NodePath("Enemy:rotation"));
	animation->rotation_track_insert_key(track_index, 0.0, Quaternion::from_euler(Vector3(0, 1, 2)));
	animation->rotation_track_insert_key(track_index, 0.5, Quaternion::from_euler(Vector3(3.5, 4, 5)));

	CHECK(animation->get_track_count() == 1);
	CHECK(!animation->track_is_compressed(0));
	CHECK(Quaternion(animation->track_get_key_value(0, 0)).is_equal_approx(Quaternion::from_euler(Vector3(0, 1, 2))));
	CHECK(Quaternion(animation->track_get_key_value(0, 1)).is_equal_approx(Quaternion::from_euler(Vector3(3.5, 4, 5))));

	Quaternion r_interpolation;

	CHECK(animation->try_rotation_track_interpolate(0, -0.2, &r_interpolation) == OK);
	CHECK(r_interpolation.is_equal_approx(Quaternion(0.403423, 0.259035, 0.73846, 0.47416)));

	CHECK(animation->try_rotation_track_interpolate(0, 0.0, &r_interpolation) == OK);
	CHECK(r_interpolation.is_equal_approx(Quaternion(0.403423, 0.259035, 0.73846, 0.47416)));

	CHECK(animation->try_rotation_track_interpolate(0, 0.2, &r_interpolation) == OK);
	CHECK(r_interpolation.is_equal_approx(Quaternion(0.336182, 0.30704, 0.751515, 0.477425)));

	CHECK(animation->try_rotation_track_interpolate(0, 0.4, &r_interpolation) == OK);
	CHECK(r_interpolation.is_equal_approx(Quaternion(0.266585, 0.352893, 0.759303, 0.477344)));

	CHECK(animation->try_rotation_track_interpolate(0, 0.5, &r_interpolation) == OK);
	CHECK(r_interpolation.is_equal_approx(Quaternion(0.231055, 0.374912, 0.761204, 0.476048)));

	CHECK(animation->try_rotation_track_interpolate(0, 0.6, &r_interpolation) == OK);
	CHECK(r_interpolation.is_equal_approx(Quaternion(0.231055, 0.374912, 0.761204, 0.476048)));

	// 3D rotation tracks always use linear interpolation for performance reasons.
	CHECK(animation->track_get_key_transition(0, 0) == doctest::Approx(real_t(1.0)));
	CHECK(animation->track_get_key_transition(0, 1) == doctest::Approx(real_t(1.0)));

	// This is a 3D rotation track, so the methods below should return errors.
	ERR_PRINT_OFF;
	CHECK(animation->value_track_interpolate(0, 0.0).is_null());
	CHECK(animation->try_position_track_interpolate(0, 0.0, nullptr) == ERR_INVALID_PARAMETER);
	CHECK(animation->try_scale_track_interpolate(0, 0.0, nullptr) == ERR_INVALID_PARAMETER);
	CHECK(animation->bezier_track_interpolate(0, 0.0) == doctest::Approx(real_t(0.0)));
	CHECK(animation->try_blend_shape_track_interpolate(0, 0.0, nullptr) == ERR_INVALID_PARAMETER);
	ERR_PRINT_ON;
}

TEST_CASE("[Animation] Create 3D scale track") {
	Ref<Animation> animation = memnew(Animation);
	const int track_index = animation->add_track(Animation::TYPE_SCALE_3D);
	animation->track_set_path(track_index, NodePath("Enemy:scale"));
	animation->scale_track_insert_key(track_index, 0.0, Vector3(0, 1, 2));
	animation->scale_track_insert_key(track_index, 0.5, Vector3(3.5, 4, 5));

	CHECK(animation->get_track_count() == 1);
	CHECK(!animation->track_is_compressed(0));
	CHECK(Vector3(animation->track_get_key_value(0, 0)).is_equal_approx(Vector3(0, 1, 2)));
	CHECK(Vector3(animation->track_get_key_value(0, 1)).is_equal_approx(Vector3(3.5, 4, 5)));

	Vector3 r_interpolation;

	CHECK(animation->try_scale_track_interpolate(0, -0.2, &r_interpolation) == OK);
	CHECK(r_interpolation.is_equal_approx(Vector3(0, 1, 2)));

	CHECK(animation->try_scale_track_interpolate(0, 0.0, &r_interpolation) == OK);
	CHECK(r_interpolation.is_equal_approx(Vector3(0, 1, 2)));

	CHECK(animation->try_scale_track_interpolate(0, 0.2, &r_interpolation) == OK);
	CHECK(r_interpolation.is_equal_approx(Vector3(1.4, 2.2, 3.2)));

	CHECK(animation->try_scale_track_interpolate(0, 0.4, &r_interpolation) == OK);
	CHECK(r_interpolation.is_equal_approx(Vector3(2.8, 3.4, 4.4)));

	CHECK(animation->try_scale_track_interpolate(0, 0.5, &r_interpolation) == OK);
	CHECK(r_interpolation.is_equal_approx(Vector3(3.5, 4, 5)));

	CHECK(animation->try_scale_track_interpolate(0, 0.6, &r_interpolation) == OK);
	CHECK(r_interpolation.is_equal_approx(Vector3(3.5, 4, 5)));

	// 3D scale tracks always use linear interpolation for performance reasons.
	CHECK(animation->track_get_key_transition(0, 0) == doctest::Approx(1.0));
	CHECK(animation->track_get_key_transition(0, 1) == doctest::Approx(1.0));

	// This is a 3D scale track, so the methods below should return errors.
	ERR_PRINT_OFF;
	CHECK(animation->value_track_interpolate(0, 0.0).is_null());
	CHECK(animation->try_position_track_interpolate(0, 0.0, nullptr) == ERR_INVALID_PARAMETER);
	CHECK(animation->try_rotation_track_interpolate(0, 0.0, nullptr) == ERR_INVALID_PARAMETER);
	CHECK(animation->bezier_track_interpolate(0, 0.0) == doctest::Approx(0.0));
	CHECK(animation->try_blend_shape_track_interpolate(0, 0.0, nullptr) == ERR_INVALID_PARAMETER);
	ERR_PRINT_ON;
}

TEST_CASE("[Animation] Create blend shape track") {
	Ref<Animation> animation = memnew(Animation);
	const int track_index = animation->add_track(Animation::TYPE_BLEND_SHAPE);
	animation->track_set_path(track_index, NodePath("Enemy:scale"));
	// Negative values for blend shapes should work as expected.
	animation->blend_shape_track_insert_key(track_index, 0.0, -1.0);
	animation->blend_shape_track_insert_key(track_index, 0.5, 1.0);

	CHECK(animation->get_track_count() == 1);
	CHECK(!animation->track_is_compressed(0));

	float r_blend = 0.0f;

	CHECK(animation->blend_shape_track_get_key(0, 0, &r_blend) == OK);
	CHECK(r_blend == doctest::Approx(-1.0f));

	CHECK(animation->blend_shape_track_get_key(0, 1, &r_blend) == OK);
	CHECK(r_blend == doctest::Approx(1.0f));

	CHECK(animation->try_blend_shape_track_interpolate(0, -0.2, &r_blend) == OK);
	CHECK(r_blend == doctest::Approx(-1.0f));

	CHECK(animation->try_blend_shape_track_interpolate(0, 0.0, &r_blend) == OK);
	CHECK(r_blend == doctest::Approx(-1.0f));

	CHECK(animation->try_blend_shape_track_interpolate(0, 0.2, &r_blend) == OK);
	CHECK(r_blend == doctest::Approx(-0.2f));

	CHECK(animation->try_blend_shape_track_interpolate(0, 0.4, &r_blend) == OK);
	CHECK(r_blend == doctest::Approx(0.6f));

	CHECK(animation->try_blend_shape_track_interpolate(0, 0.5, &r_blend) == OK);
	CHECK(r_blend == doctest::Approx(1.0f));

	CHECK(animation->try_blend_shape_track_interpolate(0, 0.6, &r_blend) == OK);
	CHECK(r_blend == doctest::Approx(1.0f));

	// Blend shape tracks always use linear interpolation for performance reasons.
	CHECK(animation->track_get_key_transition(0, 0) == doctest::Approx(real_t(1.0)));
	CHECK(animation->track_get_key_transition(0, 1) == doctest::Approx(real_t(1.0)));

	// This is a blend shape track, so the methods below should return errors.
	ERR_PRINT_OFF;
	CHECK(animation->value_track_interpolate(0, 0.0).is_null());
	CHECK(animation->try_position_track_interpolate(0, 0.0, nullptr) == ERR_INVALID_PARAMETER);
	CHECK(animation->try_rotation_track_interpolate(0, 0.0, nullptr) == ERR_INVALID_PARAMETER);
	CHECK(animation->try_scale_track_interpolate(0, 0.0, nullptr) == ERR_INVALID_PARAMETER);
	CHECK(animation->bezier_track_interpolate(0, 0.0) == doctest::Approx(0.0));
	ERR_PRINT_ON;
}

TEST_CASE("[Animation] Create Bezier track") {
	Ref<Animation> animation = memnew(Animation);
	const int track_index = animation->add_track(Animation::TYPE_BEZIER);
	animation->track_set_path(track_index, NodePath("Enemy:scale"));
	animation->bezier_track_insert_key(track_index, 0.0, -1.0, Vector2(-1, -1), Vector2(1, 1));
	animation->bezier_track_insert_key(track_index, 0.5, 1.0, Vector2(0, 1), Vector2(1, 0.5));

	CHECK(animation->get_track_count() == 1);
	CHECK(!animation->track_is_compressed(0));

	CHECK(animation->bezier_track_get_key_value(0, 0) == doctest::Approx(real_t(-1.0)));
	CHECK(animation->bezier_track_get_key_value(0, 1) == doctest::Approx(real_t(1.0)));

	CHECK(animation->bezier_track_interpolate(0, -0.2) == doctest::Approx(real_t(-1.0)));
	CHECK(animation->bezier_track_interpolate(0, 0.0) == doctest::Approx(real_t(-1.0)));
	CHECK(animation->bezier_track_interpolate(0, 0.2) == doctest::Approx(real_t(-0.76057207584381)));
	CHECK(animation->bezier_track_interpolate(0, 0.4) == doctest::Approx(real_t(-0.39975279569626)));
	CHECK(animation->bezier_track_interpolate(0, 0.5) == doctest::Approx(real_t(1.0)));
	CHECK(animation->bezier_track_interpolate(0, 0.6) == doctest::Approx(real_t(1.0)));

	// This is a bezier track, so the methods below should return errors.
	ERR_PRINT_OFF;
	CHECK(animation->value_track_interpolate(0, 0.0).is_null());
	CHECK(animation->try_position_track_interpolate(0, 0.0, nullptr) == ERR_INVALID_PARAMETER);
	CHECK(animation->try_rotation_track_interpolate(0, 0.0, nullptr) == ERR_INVALID_PARAMETER);
	CHECK(animation->try_scale_track_interpolate(0, 0.0, nullptr) == ERR_INVALID_PARAMETER);
	CHECK(animation->try_blend_shape_track_interpolate(0, 0.0, nullptr) == ERR_INVALID_PARAMETER);
	ERR_PRINT_ON;
}

} // namespace TestAnimation
