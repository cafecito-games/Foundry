/**************************************************************************/
/*  test_variant.h                                                        */
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

#include "core/variant/container_type_validate.h"
#include "core/variant/variant.h"
#include "core/variant/variant_internal.h"
#include "core/variant/variant_parser.h"

#include "tests/test_macros.h"
#include "tests/test_tools.h"

namespace TestVariant {
TEST_CASE("[Variant] Writer and parser integer") {
	int64_t a32 = 2147483648; // 2^31, so out of bounds for 32-bit signed int [-2^31, +2^31-1].
	String a32_str;
	VariantWriter::write_to_string(a32, a32_str);

	CHECK_MESSAGE(a32_str != "-2147483648", "Should not wrap around");

	int64_t b64 = 9223372036854775807; // 2^63-1, upper bound for signed 64-bit int.
	String b64_str;
	VariantWriter::write_to_string(b64, b64_str);

	CHECK_MESSAGE(b64_str == "9223372036854775807", "Should not wrap around.");

	VariantParser::StreamString ss;
	String errs;
	int line;
	Variant b64_parsed;
	int64_t b64_int_parsed;

	ss.s = b64_str;
	VariantParser::parse(&ss, b64_parsed, errs, line);
	b64_int_parsed = b64_parsed;

	CHECK_MESSAGE(b64_int_parsed == 9223372036854775807, "Should parse back.");

	ss.s = "9223372036854775808"; // Overflowed by one.
	VariantParser::parse(&ss, b64_parsed, errs, line);
	b64_int_parsed = b64_parsed;

	CHECK_MESSAGE(b64_int_parsed == 9223372036854775807, "The result should be clamped to max value.");

	ss.s = "1e100"; // Googol! Scientific notation.
	VariantParser::parse(&ss, b64_parsed, errs, line);
	b64_int_parsed = b64_parsed;

	CHECK_MESSAGE(b64_int_parsed == 9223372036854775807, "The result should be clamped to max value.");
}

TEST_CASE("[Variant] Writer and parser Variant::FLOAT") {
	// Variant::FLOAT is always 64-bit (C++ double).
	// This is the maximum non-infinity double-precision float.
	double a64 = 179769313486231570814527423731704356798070567525844996598917476803157260780028538760589558632766878171540458953514382464234321326889464182768467546703537516986049910576551282076245490090389328944075868508455133942304583236903222948165808559332123348274797826204144723168738177180919299881250404026184124858368.0;
	String a64_str;
	VariantWriter::write_to_string(a64, a64_str);

	CHECK_MESSAGE(a64_str == "1.7976931348623157e+308", "Writes in scientific notation.");
	CHECK_MESSAGE(a64_str != "inf", "Should not overflow.");
	CHECK_MESSAGE(a64_str != "nan", "The result should be defined.");

	String errs;
	int line;
	Variant variant_parsed;
	double float_parsed;

	VariantParser::StreamString bss;
	bss.s = a64_str;
	VariantParser::parse(&bss, variant_parsed, errs, line);
	float_parsed = variant_parsed;
	// Loses precision, but that's alright.
	CHECK_MESSAGE(float_parsed == 1.797693134862315708145274237317e+308, "Should parse back.");

	// Approximation of Googol with a double-precision float.
	VariantParser::StreamString css;
	css.s = "1.0e+100";
	VariantParser::parse(&css, variant_parsed, errs, line);
	float_parsed = variant_parsed;
	CHECK_MESSAGE(float_parsed == 1.0e+100, "Should match the double literal.");
}

TEST_CASE("[Variant] Assignment To Bool from Int,Float,String,Vec2,Vec2i,Vec3,Vec3i,Vec4,Vec4i,Rect2,Rect2i,Trans2d,Trans3d,Color,Call,Plane,Basis,AABB,Quant,Proj,RID,and Object") {
	Variant int_v = 0;
	Variant bool_v = true;
	int_v = bool_v; // int_v is now a bool
	CHECK(int_v == Variant(true));
	bool_v = false;
	int_v = bool_v;
	CHECK(int_v.get_type() == Variant::BOOL);

	Variant float_v = 0.0f;
	bool_v = true;
	float_v = bool_v;
	CHECK(float_v == Variant(true));
	bool_v = false;
	float_v = bool_v;
	CHECK(float_v.get_type() == Variant::BOOL);

	Variant string_v = "";
	bool_v = true;
	string_v = bool_v;
	CHECK(string_v == Variant(true));
	bool_v = false;
	string_v = bool_v;
	CHECK(string_v.get_type() == Variant::BOOL);

	Variant vec2_v = Vector2(0, 0);
	bool_v = true;
	vec2_v = bool_v;
	CHECK(vec2_v == Variant(true));
	bool_v = false;
	vec2_v = bool_v;
	CHECK(vec2_v.get_type() == Variant::BOOL);

	Variant vec2i_v = Vector2i(0, 0);
	bool_v = true;
	vec2i_v = bool_v;
	CHECK(vec2i_v == Variant(true));
	bool_v = false;
	vec2i_v = bool_v;
	CHECK(vec2i_v.get_type() == Variant::BOOL);

	Variant vec3_v = Vector3(0, 0, 0);
	bool_v = true;
	vec3_v = bool_v;
	CHECK(vec3_v == Variant(true));
	bool_v = false;
	vec3_v = bool_v;
	CHECK(vec3_v.get_type() == Variant::BOOL);

	Variant vec3i_v = Vector3i(0, 0, 0);
	bool_v = true;
	vec3i_v = bool_v;
	CHECK(vec3i_v == Variant(true));
	bool_v = false;
	vec3i_v = bool_v;
	CHECK(vec3i_v.get_type() == Variant::BOOL);

	Variant vec4_v = Vector4(0, 0, 0, 0);
	bool_v = true;
	vec4_v = bool_v;
	CHECK(vec4_v == Variant(true));
	bool_v = false;
	vec4_v = bool_v;
	CHECK(vec4_v.get_type() == Variant::BOOL);

	Variant vec4i_v = Vector4i(0, 0, 0, 0);
	bool_v = true;
	vec4i_v = bool_v;
	CHECK(vec4i_v == Variant(true));
	bool_v = false;
	vec4i_v = bool_v;
	CHECK(vec4i_v.get_type() == Variant::BOOL);

	Variant rect2_v = Rect2();
	bool_v = true;
	rect2_v = bool_v;
	CHECK(rect2_v == Variant(true));
	bool_v = false;
	rect2_v = bool_v;
	CHECK(rect2_v.get_type() == Variant::BOOL);

	Variant rect2i_v = Rect2i();
	bool_v = true;
	rect2i_v = bool_v;
	CHECK(rect2i_v == Variant(true));
	bool_v = false;
	rect2i_v = bool_v;
	CHECK(rect2i_v.get_type() == Variant::BOOL);

	Variant transform2d_v = Transform2D();
	bool_v = true;
	transform2d_v = bool_v;
	CHECK(transform2d_v == Variant(true));
	bool_v = false;
	transform2d_v = bool_v;
	CHECK(transform2d_v.get_type() == Variant::BOOL);

	Variant transform3d_v = Transform3D();
	bool_v = true;
	transform3d_v = bool_v;
	CHECK(transform3d_v == Variant(true));
	bool_v = false;
	transform3d_v = bool_v;
	CHECK(transform3d_v.get_type() == Variant::BOOL);

	Variant col_v = Color(0.5f, 0.2f, 0.75f);
	bool_v = true;
	col_v = bool_v;
	CHECK(col_v == Variant(true));
	bool_v = false;
	col_v = bool_v;
	CHECK(col_v.get_type() == Variant::BOOL);

	Variant call_v = Callable();
	bool_v = true;
	call_v = bool_v;
	CHECK(call_v == Variant(true));
	bool_v = false;
	call_v = bool_v;
	CHECK(call_v.get_type() == Variant::BOOL);

	Variant plane_v = Plane();
	bool_v = true;
	plane_v = bool_v;
	CHECK(plane_v == Variant(true));
	bool_v = false;
	plane_v = bool_v;
	CHECK(plane_v.get_type() == Variant::BOOL);

	Variant basis_v = Basis();
	bool_v = true;
	basis_v = bool_v;
	CHECK(basis_v == Variant(true));
	bool_v = false;
	basis_v = bool_v;
	CHECK(basis_v.get_type() == Variant::BOOL);

	Variant aabb_v = AABB();
	bool_v = true;
	aabb_v = bool_v;
	CHECK(aabb_v == Variant(true));
	bool_v = false;
	aabb_v = bool_v;
	CHECK(aabb_v.get_type() == Variant::BOOL);

	Variant quaternion_v = Quaternion();
	bool_v = true;
	quaternion_v = bool_v;
	CHECK(quaternion_v == Variant(true));
	bool_v = false;
	quaternion_v = bool_v;
	CHECK(quaternion_v.get_type() == Variant::BOOL);

	Variant projection_v = Projection();
	bool_v = true;
	projection_v = bool_v;
	CHECK(projection_v == Variant(true));
	bool_v = false;
	projection_v = bool_v;
	CHECK(projection_v.get_type() == Variant::BOOL);

	Variant rid_v = RID();
	bool_v = true;
	rid_v = bool_v;
	CHECK(rid_v == Variant(true));
	bool_v = false;
	rid_v = bool_v;
	CHECK(rid_v.get_type() == Variant::BOOL);

	Object obj_one = Object();
	Variant object_v = &obj_one;
	bool_v = true;
	object_v = bool_v;
	CHECK(object_v == Variant(true));
	bool_v = false;
	object_v = bool_v;
	CHECK(object_v.get_type() == Variant::BOOL);
}

TEST_CASE("[Variant] Assignment To Int from Bool,Float,String,Vec2,Vec2i,Vec3,Vec3i Vec4,Vec4i,Rect2,Rect2i,Trans2d,Trans3d,Color,Call,Plane,Basis,AABB,Quant,Proj,RID,and Object") {
	Variant bool_v = false;
	Variant int_v = 2;
	bool_v = int_v; // Now bool_v is int
	CHECK(bool_v == Variant(2));
	int_v = -3;
	bool_v = int_v;
	CHECK(bool_v.get_type() == Variant::INT);

	Variant float_v = 0.0f;
	int_v = 2;
	float_v = int_v;
	CHECK(float_v == Variant(2));
	int_v = -3;
	float_v = int_v;
	CHECK(float_v.get_type() == Variant::INT);

	Variant string_v = "";
	int_v = 2;
	string_v = int_v;
	CHECK(string_v == Variant(2));
	int_v = -3;
	string_v = int_v;
	CHECK(string_v.get_type() == Variant::INT);

	Variant vec2_v = Vector2(0, 0);
	int_v = 2;
	vec2_v = int_v;
	CHECK(vec2_v == Variant(2));
	int_v = -3;
	vec2_v = int_v;
	CHECK(vec2_v.get_type() == Variant::INT);

	Variant vec2i_v = Vector2i(0, 0);
	int_v = 2;
	vec2i_v = int_v;
	CHECK(vec2i_v == Variant(2));
	int_v = -3;
	vec2i_v = int_v;
	CHECK(vec2i_v.get_type() == Variant::INT);

	Variant vec3_v = Vector3(0, 0, 0);
	int_v = 2;
	vec3_v = int_v;
	CHECK(vec3_v == Variant(2));
	int_v = -3;
	vec3_v = int_v;
	CHECK(vec3_v.get_type() == Variant::INT);

	Variant vec3i_v = Vector3i(0, 0, 0);
	int_v = 2;
	vec3i_v = int_v;
	CHECK(vec3i_v == Variant(2));
	int_v = -3;
	vec3i_v = int_v;
	CHECK(vec3i_v.get_type() == Variant::INT);

	Variant vec4_v = Vector4(0, 0, 0, 0);
	int_v = 2;
	vec4_v = int_v;
	CHECK(vec4_v == Variant(2));
	int_v = -3;
	vec4_v = int_v;
	CHECK(vec4_v.get_type() == Variant::INT);

	Variant vec4i_v = Vector4i(0, 0, 0, 0);
	int_v = 2;
	vec4i_v = int_v;
	CHECK(vec4i_v == Variant(2));
	int_v = -3;
	vec4i_v = int_v;
	CHECK(vec4i_v.get_type() == Variant::INT);

	Variant rect2_v = Rect2();
	int_v = 2;
	rect2_v = int_v;
	CHECK(rect2_v == Variant(2));
	int_v = -3;
	rect2_v = int_v;
	CHECK(rect2_v.get_type() == Variant::INT);

	Variant rect2i_v = Rect2i();
	int_v = 2;
	rect2i_v = int_v;
	CHECK(rect2i_v == Variant(2));
	int_v = -3;
	rect2i_v = int_v;
	CHECK(rect2i_v.get_type() == Variant::INT);

	Variant transform2d_v = Transform2D();
	int_v = 2;
	transform2d_v = int_v;
	CHECK(transform2d_v == Variant(2));
	int_v = -3;
	transform2d_v = int_v;
	CHECK(transform2d_v.get_type() == Variant::INT);

	Variant transform3d_v = Transform3D();
	int_v = 2;
	transform3d_v = int_v;
	CHECK(transform3d_v == Variant(2));
	int_v = -3;
	transform3d_v = int_v;
	CHECK(transform3d_v.get_type() == Variant::INT);

	Variant col_v = Color(0.5f, 0.2f, 0.75f);
	int_v = 2;
	col_v = int_v;
	CHECK(col_v == Variant(2));
	int_v = -3;
	col_v = int_v;
	CHECK(col_v.get_type() == Variant::INT);

	Variant call_v = Callable();
	int_v = 2;
	call_v = int_v;
	CHECK(call_v == Variant(2));
	int_v = -3;
	call_v = int_v;
	CHECK(call_v.get_type() == Variant::INT);

	Variant plane_v = Plane();
	int_v = 2;
	plane_v = int_v;
	CHECK(plane_v == Variant(2));
	int_v = -3;
	plane_v = int_v;
	CHECK(plane_v.get_type() == Variant::INT);

	Variant basis_v = Basis();
	int_v = 2;
	basis_v = int_v;
	CHECK(basis_v == Variant(2));
	int_v = -3;
	basis_v = int_v;
	CHECK(basis_v.get_type() == Variant::INT);

	Variant aabb_v = AABB();
	int_v = 2;
	aabb_v = int_v;
	CHECK(aabb_v == Variant(2));
	int_v = -3;
	aabb_v = int_v;
	CHECK(aabb_v.get_type() == Variant::INT);

	Variant quaternion_v = Quaternion();
	int_v = 2;
	quaternion_v = int_v;
	CHECK(quaternion_v == Variant(2));
	int_v = -3;
	quaternion_v = int_v;
	CHECK(quaternion_v.get_type() == Variant::INT);

	Variant projection_v = Projection();
	int_v = 2;
	projection_v = int_v;
	CHECK(projection_v == Variant(2));
	int_v = -3;
	projection_v = int_v;
	CHECK(projection_v.get_type() == Variant::INT);

	Variant rid_v = RID();
	int_v = 2;
	rid_v = int_v;
	CHECK(rid_v == Variant(2));
	bool_v = -3;
	rid_v = int_v;
	CHECK(rid_v.get_type() == Variant::INT);

	Object obj_one = Object();
	Variant object_v = &obj_one;
	int_v = 2;
	object_v = int_v;
	CHECK(object_v == Variant(2));
	int_v = -3;
	object_v = int_v;
	CHECK(object_v.get_type() == Variant::INT);
}

TEST_CASE("[Variant] Assignment To Float from Bool,Int,String,Vec2,Vec2i,Vec3,Vec3i,Vec4,Vec4i,Rect2,Rect2i,Trans2d,Trans3d,Color,Call,Plane,Basis,AABB,Quant,Proj,RID,and Object") {
	Variant bool_v = false;
	Variant float_v = 1.5f;
	bool_v = float_v; // Now bool_v is float
	CHECK(bool_v == Variant(1.5f));
	float_v = -4.6f;
	bool_v = float_v;
	CHECK(bool_v.get_type() == Variant::FLOAT);

	Variant int_v = 1;
	float_v = 1.5f;
	int_v = float_v;
	CHECK(int_v == Variant(1.5f));
	float_v = -4.6f;
	int_v = float_v;
	CHECK(int_v.get_type() == Variant::FLOAT);

	Variant string_v = "";
	float_v = 1.5f;
	string_v = float_v;
	CHECK(string_v == Variant(1.5f));
	float_v = -4.6f;
	string_v = float_v;
	CHECK(string_v.get_type() == Variant::FLOAT);

	Variant vec2_v = Vector2(0, 0);
	float_v = 1.5f;
	vec2_v = float_v;
	CHECK(vec2_v == Variant(1.5f));
	float_v = -4.6f;
	vec2_v = float_v;
	CHECK(vec2_v.get_type() == Variant::FLOAT);

	Variant vec2i_v = Vector2i(0, 0);
	float_v = 1.5f;
	vec2i_v = float_v;
	CHECK(vec2i_v == Variant(1.5f));
	float_v = -4.6f;
	vec2i_v = float_v;
	CHECK(vec2i_v.get_type() == Variant::FLOAT);

	Variant vec3_v = Vector3(0, 0, 0);
	float_v = 1.5f;
	vec3_v = float_v;
	CHECK(vec3_v == Variant(1.5f));
	float_v = -4.6f;
	vec3_v = float_v;
	CHECK(vec3_v.get_type() == Variant::FLOAT);

	Variant vec3i_v = Vector3i(0, 0, 0);
	float_v = 1.5f;
	vec3i_v = float_v;
	CHECK(vec3i_v == Variant(1.5f));
	float_v = -4.6f;
	vec3i_v = float_v;
	CHECK(vec3i_v.get_type() == Variant::FLOAT);

	Variant vec4_v = Vector4(0, 0, 0, 0);
	float_v = 1.5f;
	vec4_v = float_v;
	CHECK(vec4_v == Variant(1.5f));
	float_v = -4.6f;
	vec4_v = float_v;
	CHECK(vec4_v.get_type() == Variant::FLOAT);

	Variant vec4i_v = Vector4i(0, 0, 0, 0);
	float_v = 1.5f;
	vec4i_v = float_v;
	CHECK(vec4i_v == Variant(1.5f));
	float_v = -4.6f;
	vec4i_v = float_v;
	CHECK(vec4i_v.get_type() == Variant::FLOAT);

	Variant rect2_v = Rect2();
	float_v = 1.5f;
	rect2_v = float_v;
	CHECK(rect2_v == Variant(1.5f));
	float_v = -4.6f;
	rect2_v = float_v;
	CHECK(rect2_v.get_type() == Variant::FLOAT);

	Variant rect2i_v = Rect2i();
	float_v = 1.5f;
	rect2i_v = float_v;
	CHECK(rect2i_v == Variant(1.5f));
	float_v = -4.6f;
	rect2i_v = float_v;
	CHECK(rect2i_v.get_type() == Variant::FLOAT);

	Variant transform2d_v = Transform2D();
	float_v = 1.5f;
	transform2d_v = float_v;
	CHECK(transform2d_v == Variant(1.5f));
	float_v = -4.6f;
	transform2d_v = float_v;
	CHECK(transform2d_v.get_type() == Variant::FLOAT);

	Variant transform3d_v = Transform3D();
	float_v = 1.5f;
	transform3d_v = float_v;
	CHECK(transform3d_v == Variant(1.5f));
	float_v = -4.6f;
	transform3d_v = float_v;
	CHECK(transform2d_v.get_type() == Variant::FLOAT);

	Variant col_v = Color(0.5f, 0.2f, 0.75f);
	float_v = 1.5f;
	col_v = float_v;
	CHECK(col_v == Variant(1.5f));
	float_v = -4.6f;
	col_v = float_v;
	CHECK(col_v.get_type() == Variant::FLOAT);

	Variant call_v = Callable();
	float_v = 1.5f;
	call_v = float_v;
	CHECK(call_v == Variant(1.5f));
	float_v = -4.6f;
	call_v = float_v;
	CHECK(call_v.get_type() == Variant::FLOAT);

	Variant plane_v = Plane();
	float_v = 1.5f;
	plane_v = float_v;
	CHECK(plane_v == Variant(1.5f));
	float_v = -4.6f;
	plane_v = float_v;
	CHECK(plane_v.get_type() == Variant::FLOAT);

	Variant basis_v = Basis();
	float_v = 1.5f;
	basis_v = float_v;
	CHECK(basis_v == Variant(1.5f));
	float_v = -4.6f;
	basis_v = float_v;
	CHECK(basis_v.get_type() == Variant::FLOAT);

	Variant aabb_v = AABB();
	float_v = 1.5f;
	aabb_v = float_v;
	CHECK(aabb_v == Variant(1.5f));
	float_v = -4.6f;
	aabb_v = float_v;
	CHECK(aabb_v.get_type() == Variant::FLOAT);

	Variant quaternion_v = Quaternion();
	float_v = 1.5f;
	quaternion_v = float_v;
	CHECK(quaternion_v == Variant(1.5f));
	float_v = -4.6f;
	quaternion_v = float_v;
	CHECK(quaternion_v.get_type() == Variant::FLOAT);

	Variant projection_v = Projection();
	float_v = 1.5f;
	projection_v = float_v;
	CHECK(projection_v == Variant(1.5f));
	float_v = -4.6f;
	projection_v = float_v;
	CHECK(projection_v.get_type() == Variant::FLOAT);

	Variant rid_v = RID();
	float_v = 1.5f;
	rid_v = float_v;
	CHECK(rid_v == Variant(1.5f));
	float_v = -4.6f;
	rid_v = float_v;
	CHECK(rid_v.get_type() == Variant::FLOAT);

	Object obj_one = Object();
	Variant object_v = &obj_one;
	float_v = 1.5f;
	object_v = float_v;
	CHECK(object_v == Variant(1.5f));
	float_v = -4.6f;
	object_v = float_v;
	CHECK(object_v.get_type() == Variant::FLOAT);
}

TEST_CASE("[Variant] Assignment To String from Bool,Int,Float,Vec2,Vec2i,Vec3,Vec3i,Vec4,Vec4i,Rect2,Rect2i,Trans2d,Trans3d,Color,Call,Plane,Basis,AABB,Quant,Proj,RID,and Object") {
	Variant bool_v = false;
	Variant string_v = "Hello";
	bool_v = string_v; // Now bool_v is string
	CHECK(bool_v == Variant("Hello"));
	string_v = "Hello there";
	bool_v = string_v;
	CHECK(bool_v.get_type() == Variant::STRING);

	Variant int_v = 0;
	string_v = "Hello";
	int_v = string_v;
	CHECK(int_v == Variant("Hello"));
	string_v = "Hello there";
	int_v = string_v;
	CHECK(int_v.get_type() == Variant::STRING);

	Variant float_v = 0.0f;
	string_v = "Hello";
	float_v = string_v;
	CHECK(float_v == Variant("Hello"));
	string_v = "Hello there";
	float_v = string_v;
	CHECK(float_v.get_type() == Variant::STRING);

	Variant vec2_v = Vector2(0, 0);
	string_v = "Hello";
	vec2_v = string_v;
	CHECK(vec2_v == Variant("Hello"));
	string_v = "Hello there";
	vec2_v = string_v;
	CHECK(vec2_v.get_type() == Variant::STRING);

	Variant vec2i_v = Vector2i(0, 0);
	string_v = "Hello";
	vec2i_v = string_v;
	CHECK(vec2i_v == Variant("Hello"));
	string_v = "Hello there";
	vec2i_v = string_v;
	CHECK(vec2i_v.get_type() == Variant::STRING);

	Variant vec3_v = Vector3(0, 0, 0);
	string_v = "Hello";
	vec3_v = string_v;
	CHECK(vec3_v == Variant("Hello"));
	string_v = "Hello there";
	vec3_v = string_v;
	CHECK(vec3_v.get_type() == Variant::STRING);

	Variant vec3i_v = Vector3i(0, 0, 0);
	string_v = "Hello";
	vec3i_v = string_v;
	CHECK(vec3i_v == Variant("Hello"));
	string_v = "Hello there";
	vec3i_v = string_v;
	CHECK(vec3i_v.get_type() == Variant::STRING);

	Variant vec4_v = Vector4(0, 0, 0, 0);
	string_v = "Hello";
	vec4_v = string_v;
	CHECK(vec4_v == Variant("Hello"));
	string_v = "Hello there";
	vec4_v = string_v;
	CHECK(vec4_v.get_type() == Variant::STRING);

	Variant vec4i_v = Vector4i(0, 0, 0, 0);
	string_v = "Hello";
	vec4i_v = string_v;
	CHECK(vec4i_v == Variant("Hello"));
	string_v = "Hello there";
	vec4i_v = string_v;
	CHECK(vec4i_v.get_type() == Variant::STRING);

	Variant rect2_v = Rect2();
	string_v = "Hello";
	rect2_v = string_v;
	CHECK(rect2_v == Variant("Hello"));
	string_v = "Hello there";
	rect2_v = string_v;
	CHECK(rect2_v.get_type() == Variant::STRING);

	Variant rect2i_v = Rect2i();
	string_v = "Hello";
	rect2i_v = string_v;
	CHECK(rect2i_v == Variant("Hello"));
	string_v = "Hello there";
	rect2i_v = string_v;
	CHECK(rect2i_v.get_type() == Variant::STRING);

	Variant transform2d_v = Transform2D();
	string_v = "Hello";
	transform2d_v = string_v;
	CHECK(transform2d_v == Variant("Hello"));
	string_v = "Hello there";
	transform2d_v = string_v;
	CHECK(transform2d_v.get_type() == Variant::STRING);

	Variant transform3d_v = Transform3D();
	string_v = "Hello";
	transform3d_v = string_v;
	CHECK(transform3d_v == Variant("Hello"));
	string_v = "Hello there";
	transform3d_v = string_v;
	CHECK(transform3d_v.get_type() == Variant::STRING);

	Variant col_v = Color(0.5f, 0.2f, 0.75f);
	string_v = "Hello";
	col_v = string_v;
	CHECK(col_v == Variant("Hello"));
	string_v = "Hello there";
	col_v = string_v;
	CHECK(col_v.get_type() == Variant::STRING);

	Variant call_v = Callable();
	string_v = "Hello";
	call_v = string_v;
	CHECK(call_v == Variant("Hello"));
	string_v = "Hello there";
	call_v = string_v;
	CHECK(call_v.get_type() == Variant::STRING);

	Variant plane_v = Plane();
	string_v = "Hello";
	plane_v = string_v;
	CHECK(plane_v == Variant("Hello"));
	string_v = "Hello there";
	plane_v = string_v;
	CHECK(plane_v.get_type() == Variant::STRING);

	Variant basis_v = Basis();
	string_v = "Hello";
	basis_v = string_v;
	CHECK(basis_v == Variant("Hello"));
	string_v = "Hello there";
	basis_v = string_v;
	CHECK(basis_v.get_type() == Variant::STRING);

	Variant aabb_v = AABB();
	string_v = "Hello";
	aabb_v = string_v;
	CHECK(aabb_v == Variant("Hello"));
	string_v = "Hello there";
	aabb_v = string_v;
	CHECK(aabb_v.get_type() == Variant::STRING);

	Variant quaternion_v = Quaternion();
	string_v = "Hello";
	quaternion_v = string_v;
	CHECK(quaternion_v == Variant("Hello"));
	string_v = "Hello there";
	quaternion_v = string_v;
	CHECK(quaternion_v.get_type() == Variant::STRING);

	Variant projection_v = Projection();
	string_v = "Hello";
	projection_v = string_v;
	CHECK(projection_v == Variant("Hello"));
	string_v = "Hello there";
	projection_v = string_v;
	CHECK(projection_v.get_type() == Variant::STRING);

	Variant rid_v = RID();
	string_v = "Hello";
	rid_v = string_v;
	CHECK(rid_v == Variant("Hello"));
	string_v = "Hello there";
	rid_v = string_v;
	CHECK(rid_v.get_type() == Variant::STRING);

	Object obj_one = Object();
	Variant object_v = &obj_one;
	string_v = "Hello";
	object_v = string_v;
	CHECK(object_v == Variant("Hello"));
	string_v = "Hello there";
	object_v = string_v;
	CHECK(object_v.get_type() == Variant::STRING);
}

TEST_CASE("[Variant] Assignment To Vec2 from Bool,Int,Float,String,Vec2i,Vec3,Vec3i,Vec4,Vec4i,Rect2,Rect2i,Trans2d,Trans3d,Color,Call,Plane,Basis,AABB,Quant,Proj,RID,and Object") {
	Variant bool_v = false;
	Variant vec2_v = Vector2(2.2f, 3.5f);
	bool_v = vec2_v; // Now bool_v is Vector2
	CHECK(bool_v == Variant(Vector2(2.2f, 3.5f)));
	vec2_v = Vector2(-5.4f, -7.9f);
	bool_v = vec2_v;
	CHECK(bool_v.get_type() == Variant::VECTOR2);

	Variant int_v = 0;
	vec2_v = Vector2(2.2f, 3.5f);
	int_v = vec2_v;
	CHECK(int_v == Variant(Vector2(2.2f, 3.5f)));
	vec2_v = Vector2(-5.4f, -7.9f);
	int_v = vec2_v;
	CHECK(int_v.get_type() == Variant::VECTOR2);

	Variant float_v = 0.0f;
	vec2_v = Vector2(2.2f, 3.5f);
	float_v = vec2_v;
	CHECK(float_v == Variant(Vector2(2.2f, 3.5f)));
	vec2_v = Vector2(-5.4f, -7.9f);
	float_v = vec2_v;
	CHECK(float_v.get_type() == Variant::VECTOR2);

	Variant string_v = "";
	vec2_v = Vector2(2.2f, 3.5f);
	string_v = vec2_v;
	CHECK(string_v == Variant(Vector2(2.2f, 3.5f)));
	vec2_v = Vector2(-5.4f, -7.9f);
	string_v = vec2_v;
	CHECK(string_v.get_type() == Variant::VECTOR2);

	Variant vec2i_v = Vector2i(0, 0);
	vec2_v = Vector2(2.2f, 3.5f);
	vec2i_v = vec2_v;
	CHECK(vec2i_v == Variant(Vector2(2.2f, 3.5f)));
	vec2_v = Vector2(-5.4f, -7.9f);
	vec2i_v = vec2_v;
	CHECK(vec2i_v.get_type() == Variant::VECTOR2);

	Variant vec3_v = Vector3(0, 0, 0);
	vec2_v = Vector2(2.2f, 3.5f);
	vec3_v = vec2_v;
	CHECK(vec3_v == Variant(Vector2(2.2f, 3.5f)));
	vec2_v = Vector2(-5.4f, -7.9f);
	vec3_v = vec2_v;
	CHECK(vec3_v.get_type() == Variant::VECTOR2);

	Variant vec3i_v = Vector3i(0, 0, 0);
	vec2_v = Vector2(2.2f, 3.5f);
	vec3i_v = vec2_v;
	CHECK(vec3i_v == Variant(Vector2(2.2f, 3.5f)));
	vec2_v = Vector2(-5.4f, -7.9f);
	vec3i_v = vec2_v;
	CHECK(vec3i_v.get_type() == Variant::VECTOR2);

	Variant vec4_v = Vector4(0, 0, 0, 0);
	vec2_v = Vector2(2.2f, 3.5f);
	vec4_v = vec2_v;
	CHECK(vec4_v == Variant(Vector2(2.2f, 3.5f)));
	vec2_v = Vector2(-5.4f, -7.9f);
	vec4_v = vec2_v;
	CHECK(vec4_v.get_type() == Variant::VECTOR2);

	Variant vec4i_v = Vector4i(0, 0, 0, 0);
	vec2_v = Vector2(2.2f, 3.5f);
	vec4i_v = vec2_v;
	CHECK(vec4i_v == Variant(Vector2(2.2f, 3.5f)));
	vec2_v = Vector2(-5.4f, -7.9f);
	vec4i_v = vec2_v;
	CHECK(vec4i_v.get_type() == Variant::VECTOR2);

	Variant rect2_v = Rect2();
	vec2_v = Vector2(2.2f, 3.5f);
	rect2_v = vec2_v;
	CHECK(rect2_v == Variant(Vector2(2.2f, 3.5f)));
	vec2_v = Vector2(-5.4f, -7.9f);
	rect2_v = vec2_v;
	CHECK(rect2_v.get_type() == Variant::VECTOR2);

	Variant rect2i_v = Rect2i();
	vec2_v = Vector2(2.2f, 3.5f);
	rect2i_v = vec2_v;
	CHECK(rect2i_v == Variant(Vector2(2.2f, 3.5f)));
	vec2_v = Vector2(-5.4f, -7.9f);
	rect2i_v = vec2_v;
	CHECK(rect2i_v.get_type() == Variant::VECTOR2);

	Variant transform2d_v = Transform2D();
	vec2_v = Vector2(2.2f, 3.5f);
	transform2d_v = vec2_v;
	CHECK(transform2d_v == Variant(Vector2(2.2f, 3.5f)));
	vec2_v = Vector2(-5.4f, -7.9f);
	transform2d_v = vec2_v;
	CHECK(transform2d_v.get_type() == Variant::VECTOR2);

	Variant transform3d_v = Transform3D();
	vec2_v = Vector2(2.2f, 3.5f);
	transform3d_v = vec2_v;
	CHECK(transform3d_v == Variant(Vector2(2.2f, 3.5f)));
	vec2_v = Vector2(-5.4f, -7.9f);
	transform3d_v = vec2_v;
	CHECK(transform3d_v.get_type() == Variant::VECTOR2);

	Variant col_v = Color(0.5f, 0.2f, 0.75f);
	vec2_v = Vector2(2.2f, 3.5f);
	col_v = vec2_v;
	CHECK(col_v == Variant(Vector2(2.2f, 3.5f)));
	vec2_v = Vector2(-5.4f, -7.9f);
	col_v = vec2_v;
	CHECK(col_v.get_type() == Variant::VECTOR2);

	Variant call_v = Callable();
	vec2_v = Vector2(2.2f, 3.5f);
	call_v = vec2_v;
	CHECK(call_v == Variant(Vector2(2.2f, 3.5f)));
	vec2_v = Vector2(-5.4f, -7.9f);
	call_v = vec2_v;
	CHECK(call_v.get_type() == Variant::VECTOR2);

	Variant plane_v = Plane();
	vec2_v = Vector2(2.2f, 3.5f);
	plane_v = vec2_v;
	CHECK(plane_v == Variant(Vector2(2.2f, 3.5f)));
	vec2_v = Vector2(-5.4f, -7.9f);
	plane_v = vec2_v;
	CHECK(plane_v.get_type() == Variant::VECTOR2);

	Variant basis_v = Basis();
	vec2_v = Vector2(2.2f, 3.5f);
	basis_v = vec2_v;
	CHECK(basis_v == Variant(Vector2(2.2f, 3.5f)));
	vec2_v = Vector2(-5.4f, -7.9f);
	basis_v = vec2_v;
	CHECK(basis_v.get_type() == Variant::VECTOR2);

	Variant aabb_v = AABB();
	vec2_v = Vector2(2.2f, 3.5f);
	aabb_v = vec2_v;
	CHECK(aabb_v == Variant(Vector2(2.2f, 3.5f)));
	vec2_v = Vector2(-5.4f, -7.9f);
	aabb_v = vec2_v;
	CHECK(aabb_v.get_type() == Variant::VECTOR2);

	Variant quaternion_v = Quaternion();
	vec2_v = Vector2(2.2f, 3.5f);
	quaternion_v = vec2_v;
	CHECK(quaternion_v == Variant(Vector2(2.2f, 3.5f)));
	vec2_v = Vector2(-5.4f, -7.9f);
	quaternion_v = vec2_v;
	CHECK(quaternion_v.get_type() == Variant::VECTOR2);

	Variant projection_v = Projection();
	vec2_v = Vector2(2.2f, 3.5f);
	projection_v = vec2_v;
	CHECK(projection_v == Variant(Vector2(2.2f, 3.5f)));
	vec2_v = Vector2(-5.4f, -7.9f);
	projection_v = vec2_v;
	CHECK(projection_v.get_type() == Variant::VECTOR2);

	Variant rid_v = RID();
	vec2_v = Vector2(2.2f, 3.5f);
	rid_v = vec2_v;
	CHECK(rid_v == Variant(Vector2(2.2f, 3.5f)));
	vec2_v = Vector2(-5.4f, -7.9f);
	rid_v = vec2_v;
	CHECK(rid_v.get_type() == Variant::VECTOR2);

	Object obj_one = Object();
	Variant object_v = &obj_one;
	vec2_v = Vector2(2.2f, 3.5f);
	object_v = vec2_v;
	CHECK(object_v == Variant(Vector2(2.2f, 3.5f)));
	vec2_v = Vector2(-5.4f, -7.9f);
	object_v = vec2_v;
	CHECK(object_v.get_type() == Variant::VECTOR2);
}

TEST_CASE("[Variant] Assignment To Vec2i from Bool,Int,Float,String,Vec2,Vec3,Vec3i,Vec4,Vec4i,Rect2,Rect2i,Trans2d,Trans3d,Color,Call,Plane,Basis,AABB,Quant,Proj,RID,and Object") {
	Variant bool_v = false;
	Variant vec2i_v = Vector2i(2, 3);
	bool_v = vec2i_v; // Now bool_v is Vector2i
	CHECK(bool_v == Variant(Vector2i(2, 3)));
	vec2i_v = Vector2i(-5, -7);
	bool_v = vec2i_v;
	CHECK(bool_v.get_type() == Variant::VECTOR2I);

	Variant int_v = 0;
	vec2i_v = Vector2i(2, 3);
	int_v = vec2i_v;
	CHECK(int_v == Variant(Vector2i(2, 3)));
	vec2i_v = Vector2i(-5, -7);
	int_v = vec2i_v;
	CHECK(int_v.get_type() == Variant::VECTOR2I);

	Variant float_v = 0.0f;
	vec2i_v = Vector2i(2, 3);
	float_v = vec2i_v;
	CHECK(float_v == Variant(Vector2i(2, 3)));
	vec2i_v = Vector2i(-5, -7);
	float_v = vec2i_v;
	CHECK(float_v.get_type() == Variant::VECTOR2I);

	Variant string_v = "";
	vec2i_v = Vector2i(2, 3);
	string_v = vec2i_v;
	CHECK(string_v == Variant(Vector2i(2, 3)));
	vec2i_v = Vector2i(-5, -7);
	string_v = vec2i_v;
	CHECK(string_v.get_type() == Variant::VECTOR2I);

	Variant vec2_v = Vector2(0, 0);
	vec2i_v = Vector2i(2, 3);
	vec2_v = vec2i_v;
	CHECK(vec2_v == Variant(Vector2i(2, 3)));
	vec2i_v = Vector2i(-5, -7);
	vec2_v = vec2i_v;
	CHECK(vec2i_v.get_type() == Variant::VECTOR2I);

	Variant vec3_v = Vector3(0, 0, 0);
	vec2i_v = Vector2i(2, 3);
	vec3_v = vec2i_v;
	CHECK(vec3_v == Variant(Vector2i(2, 3)));
	vec2i_v = Vector2i(-5, -7);
	vec3_v = vec2i_v;
	CHECK(vec3_v.get_type() == Variant::VECTOR2I);

	Variant vec3i_v = Vector3i(0, 0, 0);
	vec2i_v = Vector2i(2, 3);
	vec3i_v = vec2i_v;
	CHECK(vec3i_v == Variant(Vector2i(2, 3)));
	vec2i_v = Vector2i(-5, -7);
	vec3i_v = vec2i_v;
	CHECK(vec3i_v.get_type() == Variant::VECTOR2I);

	Variant vec4_v = Vector4(0, 0, 0, 0);
	vec2i_v = Vector2i(2, 3);
	vec4_v = vec2i_v;
	CHECK(vec4_v == Variant(Vector2i(2, 3)));
	vec2i_v = Vector2i(-5, -7);
	vec4_v = vec2i_v;
	CHECK(vec4_v.get_type() == Variant::VECTOR2I);

	Variant vec4i_v = Vector4i(0, 0, 0, 0);
	vec2i_v = Vector2i(2, 3);
	vec4i_v = vec2i_v;
	CHECK(vec4i_v == Variant(Vector2i(2, 3)));
	vec2i_v = Vector2i(-5, -7);
	vec4i_v = vec2i_v;
	CHECK(vec4i_v.get_type() == Variant::VECTOR2I);

	Variant rect2_v = Rect2();
	vec2i_v = Vector2i(2, 3);
	rect2_v = vec2i_v;
	CHECK(rect2_v == Variant(Vector2i(2, 3)));
	vec2i_v = Vector2i(-5, -7);
	rect2_v = vec2i_v;
	CHECK(rect2_v.get_type() == Variant::VECTOR2I);

	Variant rect2i_v = Rect2i();
	vec2i_v = Vector2i(2, 3);
	rect2i_v = vec2i_v;
	CHECK(rect2i_v == Variant(Vector2i(2, 3)));
	vec2i_v = Vector2i(-5, -7);
	rect2i_v = vec2i_v;
	CHECK(rect2i_v.get_type() == Variant::VECTOR2I);

	Variant transform2d_v = Transform2D();
	vec2i_v = Vector2i(2, 3);
	transform2d_v = vec2i_v;
	CHECK(transform2d_v == Variant(Vector2i(2, 3)));
	vec2i_v = Vector2i(-5, -7);
	transform2d_v = vec2i_v;
	CHECK(transform2d_v.get_type() == Variant::VECTOR2I);

	Variant transform3d_v = Transform3D();
	vec2i_v = Vector2i(2, 3);
	transform3d_v = vec2i_v;
	CHECK(transform3d_v == Variant(Vector2i(2, 3)));
	vec2i_v = Vector2i(-5, -7);
	transform3d_v = vec2i_v;
	CHECK(transform3d_v.get_type() == Variant::VECTOR2I);

	Variant col_v = Color(0.5f, 0.2f, 0.75f);
	vec2i_v = Vector2i(2, 3);
	col_v = vec2i_v;
	CHECK(col_v == Variant(Vector2i(2, 3)));
	vec2i_v = Vector2i(-5, -7);
	col_v = vec2i_v;
	CHECK(col_v.get_type() == Variant::VECTOR2I);

	Variant call_v = Callable();
	vec2i_v = Vector2i(2, 3);
	call_v = vec2i_v;
	CHECK(call_v == Variant(Vector2i(2, 3)));
	vec2i_v = Vector2i(-5, -7);
	call_v = vec2i_v;
	CHECK(call_v.get_type() == Variant::VECTOR2I);

	Variant plane_v = Plane();
	vec2i_v = Vector2i(2, 3);
	plane_v = vec2i_v;
	CHECK(plane_v == Variant(Vector2i(2, 3)));
	vec2i_v = Vector2i(-5, -7);
	plane_v = vec2i_v;
	CHECK(plane_v.get_type() == Variant::VECTOR2I);

	Variant basis_v = Basis();
	vec2i_v = Vector2i(2, 3);
	basis_v = vec2i_v;
	CHECK(basis_v == Variant(Vector2i(2, 3)));
	vec2i_v = Vector2i(-5, -7);
	basis_v = vec2i_v;
	CHECK(basis_v.get_type() == Variant::VECTOR2I);

	Variant aabb_v = AABB();
	vec2i_v = Vector2i(2, 3);
	aabb_v = vec2i_v;
	CHECK(aabb_v == Variant(Vector2i(2, 3)));
	vec2i_v = Vector2i(-5, -7);
	aabb_v = vec2i_v;
	CHECK(aabb_v.get_type() == Variant::VECTOR2I);

	Variant quaternion_v = Quaternion();
	vec2i_v = Vector2i(2, 3);
	quaternion_v = vec2i_v;
	CHECK(quaternion_v == Variant(Vector2i(2, 3)));
	vec2i_v = Vector2i(-5, -7);
	quaternion_v = vec2i_v;
	CHECK(quaternion_v.get_type() == Variant::VECTOR2I);

	Variant projection_v = Projection();
	vec2i_v = Vector2i(2, 3);
	projection_v = vec2i_v;
	CHECK(projection_v == Variant(Vector2i(2, 3)));
	vec2i_v = Vector2i(-5, -7);
	projection_v = vec2i_v;
	CHECK(projection_v.get_type() == Variant::VECTOR2I);

	Variant rid_v = RID();
	vec2i_v = Vector2i(2, 3);
	rid_v = vec2i_v;
	CHECK(rid_v == Variant(Vector2i(2, 3)));
	vec2i_v = Vector2i(-5, -7);
	rid_v = vec2i_v;
	CHECK(rid_v.get_type() == Variant::VECTOR2I);

	Object obj_one = Object();
	Variant object_v = &obj_one;
	vec2i_v = Vector2i(2, 3);
	object_v = vec2i_v;
	CHECK(object_v == Variant(Vector2i(2, 3)));
	vec2i_v = Vector2i(-5, -7);
	object_v = vec2i_v;
	CHECK(object_v.get_type() == Variant::VECTOR2I);
}

TEST_CASE("[Variant] Assignment To Vec3 from Bool,Int,Float,String,Vec2,Vec2i,Vec3i,Vec4,Vec4i,Rect2,Rect2i,Trans2d,Trans3d,Color,Call,Plane,Basis,AABB,Quant,Proj,RID,and Object") {
	Variant bool_v = false;
	Variant vec3_v = Vector3(2.2f, 3.5f, 5.3f);
	bool_v = vec3_v; // Now bool_v is Vector3
	CHECK(bool_v == Variant(Vector3(2.2f, 3.5f, 5.3f)));
	vec3_v = Vector3(-5.4f, -7.9f, -2.1f);
	bool_v = vec3_v;
	CHECK(bool_v.get_type() == Variant::VECTOR3);

	Variant int_v = 0;
	vec3_v = Vector3(2.2f, 3.5f, 5.3f);
	int_v = vec3_v;
	CHECK(int_v == Variant(Vector3(2.2f, 3.5f, 5.3f)));
	vec3_v = Vector3(-5.4f, -7.9f, -2.1f);
	int_v = vec3_v;
	CHECK(int_v.get_type() == Variant::VECTOR3);

	Variant float_v = 0.0f;
	vec3_v = Vector3(2.2f, 3.5f, 5.3f);
	float_v = vec3_v;
	CHECK(float_v == Variant(Vector3(2.2f, 3.5f, 5.3f)));
	vec3_v = Vector3(-5.4f, -7.9f, -2.1f);
	float_v = vec3_v;
	CHECK(float_v.get_type() == Variant::VECTOR3);

	Variant string_v = "";
	vec3_v = Vector3(2.2f, 3.5f, 5.3f);
	string_v = vec3_v;
	CHECK(string_v == Variant(Vector3(2.2f, 3.5f, 5.3f)));
	vec3_v = Vector3(-5.4f, -7.9f, -2.1f);
	string_v = vec3_v;
	CHECK(string_v.get_type() == Variant::VECTOR3);

	Variant vec2_v = Vector2(0, 0);
	vec3_v = Vector3(2.2f, 3.5f, 5.3f);
	vec2_v = vec3_v;
	CHECK(vec2_v == Variant(Vector3(2.2f, 3.5f, 5.3f)));
	vec3_v = Vector3(-5.4f, -7.9f, -2.1f);
	vec2_v = vec3_v;
	CHECK(vec2_v.get_type() == Variant::VECTOR3);

	Variant vec2i_v = Vector2i(0, 0);
	vec3_v = Vector3(2.2f, 3.5f, 5.3f);
	vec2i_v = vec3_v;
	CHECK(vec2i_v == Variant(Vector3(2.2f, 3.5f, 5.3f)));
	vec3_v = Vector3(-5.4f, -7.9f, -2.1f);
	vec2i_v = vec3_v;
	CHECK(vec2i_v.get_type() == Variant::VECTOR3);

	Variant vec3i_v = Vector3i(0, 0, 0);
	vec3_v = Vector3(2.2f, 3.5f, 5.3f);
	vec3i_v = vec3_v;
	CHECK(vec3i_v == Variant(Vector3(2.2f, 3.5f, 5.3f)));
	vec3_v = Vector3(-5.4f, -7.9f, -2.1f);
	vec3i_v = vec3_v;
	CHECK(vec3i_v.get_type() == Variant::VECTOR3);

	Variant vec4_v = Vector4(0, 0, 0, 0);
	vec3_v = Vector3(2.2f, 3.5f, 5.3f);
	vec4_v = vec3_v;
	CHECK(vec4_v == Variant(Vector3(2.2f, 3.5f, 5.3f)));
	vec3_v = Vector3(-5.4f, -7.9f, -2.1f);
	vec4_v = vec3_v;
	CHECK(vec4_v.get_type() == Variant::VECTOR3);

	Variant vec4i_v = Vector4i(0, 0, 0, 0);
	vec3_v = Vector3(2.2f, 3.5f, 5.3f);
	vec4i_v = vec3_v;
	CHECK(vec4i_v == Variant(Vector3(2.2f, 3.5f, 5.3f)));
	vec3_v = Vector3(-5.4f, -7.9f, -2.1f);
	vec4i_v = vec3_v;
	CHECK(vec4i_v.get_type() == Variant::VECTOR3);

	Variant rect2_v = Rect2();
	vec3_v = Vector3(2.2f, 3.5f, 5.3f);
	rect2_v = vec3_v;
	CHECK(rect2_v == Variant(Vector3(2.2f, 3.5f, 5.3f)));
	vec3_v = Vector3(-5.4f, -7.9f, -2.1f);
	rect2_v = vec3_v;
	CHECK(rect2_v.get_type() == Variant::VECTOR3);

	Variant rect2i_v = Rect2i();
	vec3_v = Vector3(2.2f, 3.5f, 5.3f);
	rect2i_v = vec3_v;
	CHECK(rect2i_v == Variant(Vector3(2.2f, 3.5f, 5.3f)));
	vec3_v = Vector3(-5.4f, -7.9f, -2.1f);
	rect2i_v = vec3_v;
	CHECK(rect2i_v.get_type() == Variant::VECTOR3);

	Variant transform2d_v = Transform2D();
	vec3_v = Vector3(2.2f, 3.5f, 5.3f);
	transform2d_v = vec3_v;
	CHECK(transform2d_v == Variant(Vector3(2.2f, 3.5f, 5.3f)));
	vec3_v = Vector3(-5.4f, -7.9f, -2.1f);
	transform2d_v = vec3_v;
	CHECK(transform2d_v.get_type() == Variant::VECTOR3);

	Variant transform3d_v = Transform3D();
	vec3_v = Vector3(2.2f, 3.5f, 5.3f);
	transform3d_v = vec3_v;
	CHECK(transform3d_v == Variant(Vector3(2.2f, 3.5f, 5.3f)));
	vec3_v = Vector3(-5.4f, -7.9f, -2.1f);
	transform3d_v = vec3_v;
	CHECK(transform3d_v.get_type() == Variant::VECTOR3);

	Variant col_v = Color(0.5f, 0.2f, 0.75f);
	vec3_v = Vector3(2.2f, 3.5f, 5.3f);
	col_v = vec3_v;
	CHECK(col_v == Variant(Vector3(2.2f, 3.5f, 5.3f)));
	vec3_v = Vector3(-5.4f, -7.9f, -2.1f);
	col_v = vec3_v;
	CHECK(col_v.get_type() == Variant::VECTOR3);

	Variant call_v = Callable();
	vec3_v = Vector3(2.2f, 3.5f, 5.3f);
	call_v = vec3_v;
	CHECK(call_v == Variant(Vector3(2.2f, 3.5f, 5.3f)));
	vec3_v = Vector3(-5.4f, -7.9f, -2.1f);
	call_v = vec3_v;
	CHECK(call_v.get_type() == Variant::VECTOR3);

	Variant plane_v = Plane();
	vec3_v = Vector3(2.2f, 3.5f, 5.3f);
	plane_v = vec3_v;
	CHECK(plane_v == Variant(Vector3(2.2f, 3.5f, 5.3f)));
	vec3_v = Vector3(-5.4f, -7.9f, -2.1f);
	plane_v = vec3_v;
	CHECK(plane_v.get_type() == Variant::VECTOR3);

	Variant basis_v = Basis();
	vec3_v = Vector3(2.2f, 3.5f, 5.3f);
	basis_v = vec3_v;
	CHECK(basis_v == Variant(Vector3(2.2f, 3.5f, 5.3f)));
	vec3_v = Vector3(-5.4f, -7.9f, -2.1f);
	basis_v = vec3_v;
	CHECK(basis_v.get_type() == Variant::VECTOR3);

	Variant aabb_v = AABB();
	vec3_v = Vector3(2.2f, 3.5f, 5.3f);
	aabb_v = vec3_v;
	CHECK(aabb_v == Variant(Vector3(2.2f, 3.5f, 5.3f)));
	vec3_v = Vector3(-5.4f, -7.9f, -2.1f);
	aabb_v = vec3_v;
	CHECK(aabb_v.get_type() == Variant::VECTOR3);

	Variant quaternion_v = Quaternion();
	vec3_v = Vector3(2.2f, 3.5f, 5.3f);
	quaternion_v = vec3_v;
	CHECK(quaternion_v == Variant(Vector3(2.2f, 3.5f, 5.3f)));
	vec3_v = Vector3(-5.4f, -7.9f, -2.1f);
	quaternion_v = vec3_v;
	CHECK(quaternion_v.get_type() == Variant::VECTOR3);

	Variant projection_v = Projection();
	vec3_v = Vector3(2.2f, 3.5f, 5.3f);
	quaternion_v = vec3_v;
	CHECK(quaternion_v == Variant(Vector3(2.2f, 3.5f, 5.3f)));
	vec3_v = Vector3(-5.4f, -7.9f, -2.1f);
	quaternion_v = vec3_v;
	CHECK(quaternion_v.get_type() == Variant::VECTOR3);

	Variant rid_v = RID();
	vec3_v = Vector3(2.2f, 3.5f, 5.3f);
	rid_v = vec3_v;
	CHECK(rid_v == Variant(Vector3(2.2f, 3.5f, 5.3f)));
	vec3_v = Vector3(-5.4f, -7.9f, -2.1f);
	rid_v = vec3_v;
	CHECK(rid_v.get_type() == Variant::VECTOR3);

	Object obj_one = Object();
	Variant object_v = &obj_one;
	vec3_v = Vector3(2.2f, 3.5f, 5.3f);
	object_v = vec3_v;
	CHECK(object_v == Variant(Vector3(2.2f, 3.5f, 5.3f)));
	vec3_v = Vector3(-5.4f, -7.9f, -2.1f);
	object_v = vec3_v;
	CHECK(object_v.get_type() == Variant::VECTOR3);
}

TEST_CASE("[Variant] Assignment To Vec3i from Bool,Int,Float,String,Vec2,Vec2i,Vec3 and Color") {
	Variant bool_v = false;
	Variant vec3i_v = Vector3i(2, 3, 5);
	bool_v = vec3i_v; // Now bool_v is Vector3i
	CHECK(bool_v == Variant(Vector3i(2, 3, 5)));
	vec3i_v = Vector3i(-5, -7, -2);
	bool_v = vec3i_v;
	CHECK(bool_v.get_type() == Variant::VECTOR3I);

	Variant int_v = 0;
	vec3i_v = Vector3i(2, 3, 5);
	int_v = vec3i_v;
	CHECK(int_v == Variant(Vector3i(2, 3, 5)));
	vec3i_v = Vector3i(-5, -7, -2);
	int_v = vec3i_v;
	CHECK(int_v.get_type() == Variant::VECTOR3I);

	Variant float_v = 0.0f;
	vec3i_v = Vector3i(2, 3, 5);
	float_v = vec3i_v;
	CHECK(float_v == Variant(Vector3i(2, 3, 5)));
	vec3i_v = Vector3i(-5, -7, -2);
	float_v = vec3i_v;
	CHECK(float_v.get_type() == Variant::VECTOR3I);

	Variant string_v = "";
	vec3i_v = Vector3i(2, 3, 5);
	string_v = vec3i_v;
	CHECK(string_v == Variant(Vector3i(2, 3, 5)));
	vec3i_v = Vector3i(-5, -7, -2);
	string_v = vec3i_v;
	CHECK(string_v.get_type() == Variant::VECTOR3I);

	Variant vec2_v = Vector2(0, 0);
	vec3i_v = Vector3i(2, 3, 5);
	vec2_v = vec3i_v;
	CHECK(vec2_v == Variant(Vector3i(2, 3, 5)));
	vec3i_v = Vector3i(-5, -7, -2);
	vec2_v = vec3i_v;
	CHECK(vec2_v.get_type() == Variant::VECTOR3I);

	Variant vec2i_v = Vector2i(0, 0);
	vec3i_v = Vector3i(2, 3, 5);
	vec2i_v = vec3i_v;
	CHECK(vec2i_v == Variant(Vector3i(2, 3, 5)));
	vec3i_v = Vector3i(-5, -7, -2);
	vec2i_v = vec3i_v;
	CHECK(vec2i_v.get_type() == Variant::VECTOR3I);

	Variant vec3_v = Vector3(0, 0, 0);
	vec3i_v = Vector3i(2, 3, 5);
	vec3_v = vec3i_v;
	CHECK(vec3_v == Variant(Vector3i(2, 3, 5)));
	vec3i_v = Vector3i(-5, -7, -2);
	vec3_v = vec3i_v;
	CHECK(vec3_v.get_type() == Variant::VECTOR3I);

	Variant vec4_v = Vector4(0, 0, 0, 0);
	vec3i_v = Vector3i(2, 3, 5);
	vec4_v = vec3i_v;
	CHECK(vec4_v == Variant(Vector3i(2, 3, 5)));
	vec3i_v = Vector3i(-5, -7, -2);
	vec4_v = vec3i_v;
	CHECK(vec4_v.get_type() == Variant::VECTOR3I);

	Variant vec4i_v = Vector4i(0, 0, 0, 0);
	vec3i_v = Vector3i(2, 3, 5);
	vec4i_v = vec3i_v;
	CHECK(vec4i_v == Variant(Vector3i(2, 3, 5)));
	vec3i_v = Vector3i(-5, -7, -2);
	vec4i_v = vec3i_v;
	CHECK(vec4i_v.get_type() == Variant::VECTOR3I);

	Variant rect2_v = Rect2();
	vec3i_v = Vector3i(2, 3, 5);
	rect2_v = vec3i_v;
	CHECK(rect2_v == Variant(Vector3i(2, 3, 5)));
	vec3i_v = Vector3i(-5, -7, -2);
	rect2_v = vec3i_v;
	CHECK(rect2_v.get_type() == Variant::VECTOR3I);

	Variant rect2i_v = Rect2i();
	vec3i_v = Vector3i(2, 3, 5);
	rect2i_v = vec3i_v;
	CHECK(rect2i_v == Variant(Vector3i(2, 3, 5)));
	vec3i_v = Vector3i(-5, -7, -2);
	rect2i_v = vec3i_v;
	CHECK(rect2i_v.get_type() == Variant::VECTOR3I);

	Variant transform2d_v = Transform2D();
	vec3i_v = Vector3i(2, 3, 5);
	transform2d_v = vec3i_v;
	CHECK(transform2d_v == Variant(Vector3i(2, 3, 5)));
	vec3i_v = Vector3i(-5, -7, -2);
	transform2d_v = vec3i_v;
	CHECK(transform2d_v.get_type() == Variant::VECTOR3I);

	Variant transform3d_v = Transform3D();
	vec3i_v = Vector3i(2, 3, 5);
	transform3d_v = vec3i_v;
	CHECK(transform3d_v == Variant(Vector3i(2, 3, 5)));
	vec3i_v = Vector3i(-5, -7, -2);
	transform3d_v = vec3i_v;
	CHECK(transform3d_v.get_type() == Variant::VECTOR3I);

	Variant col_v = Color(0.5f, 0.2f, 0.75f);
	vec3i_v = Vector3i(2, 3, 5);
	col_v = vec3i_v;
	CHECK(col_v == Variant(Vector3i(2, 3, 5)));
	vec3i_v = Vector3i(-5, -7, -2);
	col_v = vec3i_v;
	CHECK(col_v.get_type() == Variant::VECTOR3I);

	Variant call_v = Callable();
	vec3i_v = Vector3i(2, 3, 5);
	call_v = vec3i_v;
	CHECK(call_v == Variant(Vector3i(2, 3, 5)));
	vec3i_v = Vector3i(-5, -7, -2);
	call_v = vec3i_v;
	CHECK(call_v.get_type() == Variant::VECTOR3I);

	Variant plane_v = Plane();
	vec3i_v = Vector3i(2, 3, 5);
	plane_v = vec3i_v;
	CHECK(plane_v == Variant(Vector3i(2, 3, 5)));
	vec3i_v = Vector3i(-5, -7, -2);
	plane_v = vec3i_v;
	CHECK(plane_v.get_type() == Variant::VECTOR3I);

	Variant basis_v = Basis();
	vec3i_v = Vector3i(2, 3, 5);
	basis_v = vec3i_v;
	CHECK(basis_v == Variant(Vector3i(2, 3, 5)));
	vec3i_v = Vector3i(-5, -7, -2);
	basis_v = vec3i_v;
	CHECK(basis_v.get_type() == Variant::VECTOR3I);

	Variant aabb_v = AABB();
	vec3i_v = Vector3i(2, 3, 5);
	aabb_v = vec3i_v;
	CHECK(aabb_v == Variant(Vector3i(2, 3, 5)));
	vec3i_v = Vector3i(-5, -7, -2);
	aabb_v = vec3i_v;
	CHECK(aabb_v.get_type() == Variant::VECTOR3I);

	Variant quaternion_v = Quaternion();
	vec3i_v = Vector3i(2, 3, 5);
	quaternion_v = vec3i_v;
	CHECK(quaternion_v == Variant(Vector3i(2, 3, 5)));
	vec3i_v = Vector3i(-5, -7, -2);
	quaternion_v = vec3i_v;
	CHECK(quaternion_v.get_type() == Variant::VECTOR3I);

	Variant projection_v = Projection();
	vec3i_v = Vector3i(2, 3, 5);
	projection_v = vec3i_v;
	CHECK(projection_v == Variant(Vector3i(2, 3, 5)));
	vec3i_v = Vector3i(-5, -7, -2);
	projection_v = vec3i_v;
	CHECK(projection_v.get_type() == Variant::VECTOR3I);

	Variant rid_v = RID();
	vec3i_v = Vector3i(2, 3, 5);
	rid_v = vec3i_v;
	CHECK(rid_v == Variant(Vector3i(2, 3, 5)));
	vec3i_v = Vector3i(-5, -7, -2);
	rid_v = vec3i_v;
	CHECK(rid_v.get_type() == Variant::VECTOR3I);

	Object obj_one = Object();
	Variant object_v = &obj_one;
	vec3i_v = Vector3i(2, 3, 5);
	object_v = vec3i_v;
	CHECK(object_v == Variant(Vector3i(2, 3, 5)));
	vec3i_v = Vector3i(-5, -7, -2);
	object_v = vec3i_v;
	CHECK(object_v.get_type() == Variant::VECTOR3I);
}

TEST_CASE("[Variant] Assignment To Color from Bool,Int,Float,String,Vec2,Vec2i,Vec3,Vec3i,Vec4,Vec4i,Rect2,Rect2i,Trans2d,Trans3d,Color,Call,Plane,Basis,AABB,Quant,Proj,RID,and Object") {
	Variant bool_v = false;
	Variant col_v = Color(0.25f, 0.4f, 0.78f);
	bool_v = col_v; // Now bool_v is Color
	CHECK(bool_v == Variant(Color(0.25f, 0.4f, 0.78f)));
	col_v = Color(0.33f, 0.75f, 0.21f);
	bool_v = col_v;
	CHECK(bool_v.get_type() == Variant::COLOR);

	Variant int_v = 0;
	col_v = Color(0.25f, 0.4f, 0.78f);
	int_v = col_v;
	CHECK(int_v == Variant(Color(0.25f, 0.4f, 0.78f)));
	col_v = Color(0.33f, 0.75f, 0.21f);
	int_v = col_v;
	CHECK(int_v.get_type() == Variant::COLOR);

	Variant float_v = 0.0f;
	col_v = Color(0.25f, 0.4f, 0.78f);
	float_v = col_v;
	CHECK(float_v == Variant(Color(0.25f, 0.4f, 0.78f)));
	col_v = Color(0.33f, 0.75f, 0.21f);
	float_v = col_v;
	CHECK(float_v.get_type() == Variant::COLOR);

	Variant string_v = "";
	col_v = Color(0.25f, 0.4f, 0.78f);
	string_v = col_v;
	CHECK(string_v == Variant(Color(0.25f, 0.4f, 0.78f)));
	col_v = Color(0.33f, 0.75f, 0.21f);
	string_v = col_v;
	CHECK(string_v.get_type() == Variant::COLOR);

	Variant vec2_v = Vector2(0, 0);
	col_v = Color(0.25f, 0.4f, 0.78f);
	vec2_v = col_v;
	CHECK(vec2_v == Variant(Color(0.25f, 0.4f, 0.78f)));
	col_v = Color(0.33f, 0.75f, 0.21f);
	vec2_v = col_v;
	CHECK(vec2_v.get_type() == Variant::COLOR);

	Variant vec2i_v = Vector2i(0, 0);
	col_v = Color(0.25f, 0.4f, 0.78f);
	vec2i_v = col_v;
	CHECK(vec2i_v == Variant(Color(0.25f, 0.4f, 0.78f)));
	col_v = Color(0.33f, 0.75f, 0.21f);
	vec2i_v = col_v;
	CHECK(vec2i_v.get_type() == Variant::COLOR);

	Variant vec3_v = Vector3(0, 0, 0);
	col_v = Color(0.25f, 0.4f, 0.78f);
	vec3_v = col_v;
	CHECK(vec3_v == Variant(Color(0.25f, 0.4f, 0.78f)));
	col_v = Color(0.33f, 0.75f, 0.21f);
	vec3_v = col_v;
	CHECK(vec3_v.get_type() == Variant::COLOR);

	Variant vec3i_v = Vector3i(0, 0, 0);
	col_v = Color(0.25f, 0.4f, 0.78f);
	vec3i_v = col_v;
	CHECK(vec3i_v == Variant(Color(0.25f, 0.4f, 0.78f)));
	col_v = Color(0.33f, 0.75f, 0.21f);
	vec3i_v = col_v;
	CHECK(vec3i_v.get_type() == Variant::COLOR);

	Variant vec4_v = Vector4(0, 0, 0, 0);
	col_v = Color(0.25f, 0.4f, 0.78f);
	vec4_v = col_v;
	CHECK(vec4_v == Variant(Color(0.25f, 0.4f, 0.78f)));
	col_v = Color(0.33f, 0.75f, 0.21f);
	vec4_v = col_v;
	CHECK(vec4_v.get_type() == Variant::COLOR);

	Variant vec4i_v = Vector4i(0, 0, 0, 0);
	col_v = Color(0.25f, 0.4f, 0.78f);
	vec4i_v = col_v;
	CHECK(vec4i_v == Variant(Color(0.25f, 0.4f, 0.78f)));
	col_v = Color(0.33f, 0.75f, 0.21f);
	vec4i_v = col_v;
	CHECK(vec4i_v.get_type() == Variant::COLOR);

	Variant rect2_v = Rect2();
	col_v = Color(0.25f, 0.4f, 0.78f);
	rect2_v = col_v;
	CHECK(rect2_v == Variant(Color(0.25f, 0.4f, 0.78f)));
	col_v = Color(0.33f, 0.75f, 0.21f);
	rect2_v = col_v;
	CHECK(rect2_v.get_type() == Variant::COLOR);

	Variant rect2i_v = Rect2i();
	col_v = Color(0.25f, 0.4f, 0.78f);
	rect2i_v = col_v;
	CHECK(rect2i_v == Variant(Color(0.25f, 0.4f, 0.78f)));
	col_v = Color(0.33f, 0.75f, 0.21f);
	rect2i_v = col_v;
	CHECK(rect2i_v.get_type() == Variant::COLOR);

	Variant transform2d_v = Transform2D();
	col_v = Color(0.25f, 0.4f, 0.78f);
	transform2d_v = col_v;
	CHECK(transform2d_v == Variant(Color(0.25f, 0.4f, 0.78f)));
	col_v = Color(0.33f, 0.75f, 0.21f);
	transform2d_v = col_v;
	CHECK(transform2d_v.get_type() == Variant::COLOR);

	Variant transform3d_v = Transform3D();
	col_v = Color(0.25f, 0.4f, 0.78f);
	transform3d_v = col_v;
	CHECK(transform3d_v == Variant(Color(0.25f, 0.4f, 0.78f)));
	col_v = Color(0.33f, 0.75f, 0.21f);
	transform3d_v = col_v;
	CHECK(transform3d_v.get_type() == Variant::COLOR);

	Variant call_v = Callable();
	col_v = Color(0.25f, 0.4f, 0.78f);
	call_v = col_v;
	CHECK(call_v == Variant(Color(0.25f, 0.4f, 0.78f)));
	col_v = Color(0.33f, 0.75f, 0.21f);
	call_v = col_v;
	CHECK(call_v.get_type() == Variant::COLOR);

	Variant plane_v = Plane();
	col_v = Color(0.25f, 0.4f, 0.78f);
	plane_v = col_v;
	CHECK(plane_v == Variant(Color(0.25f, 0.4f, 0.78f)));
	col_v = Color(0.33f, 0.75f, 0.21f);
	plane_v = col_v;
	CHECK(plane_v.get_type() == Variant::COLOR);

	Variant basis_v = Basis();
	col_v = Color(0.25f, 0.4f, 0.78f);
	basis_v = col_v;
	CHECK(basis_v == Variant(Color(0.25f, 0.4f, 0.78f)));
	col_v = Color(0.33f, 0.75f, 0.21f);
	basis_v = col_v;
	CHECK(basis_v.get_type() == Variant::COLOR);

	Variant aabb_v = AABB();
	col_v = Color(0.25f, 0.4f, 0.78f);
	aabb_v = col_v;
	CHECK(aabb_v == Variant(Color(0.25f, 0.4f, 0.78f)));
	col_v = Color(0.33f, 0.75f, 0.21f);
	aabb_v = col_v;
	CHECK(aabb_v.get_type() == Variant::COLOR);

	Variant quaternion_v = Quaternion();
	col_v = Color(0.25f, 0.4f, 0.78f);
	quaternion_v = col_v;
	CHECK(quaternion_v == Variant(Color(0.25f, 0.4f, 0.78f)));
	col_v = Color(0.33f, 0.75f, 0.21f);
	quaternion_v = col_v;
	CHECK(quaternion_v.get_type() == Variant::COLOR);

	Variant projection_v = Projection();
	col_v = Color(0.25f, 0.4f, 0.78f);
	projection_v = col_v;
	CHECK(projection_v == Variant(Color(0.25f, 0.4f, 0.78f)));
	col_v = Color(0.33f, 0.75f, 0.21f);
	projection_v = col_v;
	CHECK(projection_v.get_type() == Variant::COLOR);

	Variant rid_v = RID();
	col_v = Color(0.25f, 0.4f, 0.78f);
	rid_v = col_v;
	CHECK(rid_v == Variant(Color(0.25f, 0.4f, 0.78f)));
	col_v = Color(0.33f, 0.75f, 0.21f);
	rid_v = col_v;
	CHECK(rid_v.get_type() == Variant::COLOR);

	Object obj_one = Object();
	Variant object_v = &obj_one;
	col_v = Color(0.25f, 0.4f, 0.78f);
	object_v = col_v;
	CHECK(object_v == Variant(Color(0.25f, 0.4f, 0.78f)));
	col_v = Color(0.33f, 0.75f, 0.21f);
	object_v = col_v;
	CHECK(object_v.get_type() == Variant::COLOR);
}

TEST_CASE("[Variant] array initializer list") {
	Variant arr_v = { 0, 1, "test", true, { 0.0, 1.0 } };
	CHECK(arr_v.get_type() == Variant::ARRAY);
	Array arr = (Array)arr_v;
	CHECK(arr.size() == 5);
	CHECK(arr[0] == Variant(0));
	CHECK(arr[1] == Variant(1));
	CHECK(arr[2] == Variant("test"));
	CHECK(arr[3] == Variant(true));
	CHECK(arr[4] == Variant({ 0.0, 1.0 }));

	PackedInt32Array packed_arr = { 2, 1, 0 };
	CHECK(packed_arr.size() == 3);
	CHECK(packed_arr[0] == 2);
	CHECK(packed_arr[1] == 1);
	CHECK(packed_arr[2] == 0);
}

TEST_CASE("[Variant] Writer and parser Vector2") {
	Variant vec2_parsed;
	String vec2_str;
	String errs;
	int line;
	// Variant::VECTOR2 and Vector2 can be either 32-bit or 64-bit depending on the precision level of real_t.
	{
		Vector2 vec2 = Vector2(1.2, 3.4);
		VariantWriter::write_to_string(vec2, vec2_str);
		// Reminder: "1.2" and "3.4" are not exactly those decimal numbers. They are the closest float to them.
		CHECK_MESSAGE(vec2_str == "Vector2(1.2, 3.4)", "Should write with enough digits to ensure parsing back is exact.");
		VariantParser::StreamString stream;
		stream.s = vec2_str;
		VariantParser::parse(&stream, vec2_parsed, errs, line);
		CHECK_MESSAGE(Vector2(vec2_parsed) == vec2, "Should parse back to the same Vector2.");
	}
	// Check with big numbers and small numbers.
	{
		Vector2 vec2 = Vector2(1.234567898765432123456789e30, 1.234567898765432123456789e-10);
		VariantWriter::write_to_string(vec2, vec2_str);
#ifdef REAL_T_IS_DOUBLE
		CHECK_MESSAGE(vec2_str == "Vector2(1.2345678987654322e+30, 1.2345678987654322e-10)", "Should write with enough digits to ensure parsing back is exact.");
#else
		CHECK_MESSAGE(vec2_str == "Vector2(1.2345679e+30, 1.2345679e-10)", "Should write with enough digits to ensure parsing back is exact.");
#endif
		VariantParser::StreamString stream;
		stream.s = vec2_str;
		VariantParser::parse(&stream, vec2_parsed, errs, line);
		CHECK_MESSAGE(Vector2(vec2_parsed) == vec2, "Should parse back to the same Vector2.");
	}
}

TEST_CASE("[Variant] Writer and parser array") {
	Array a = { 1, String("hello"), Array({ Variant() }) };
	String a_str;
	VariantWriter::write_to_string(a, a_str);

	CHECK_EQ(a_str, "[1, \"hello\", [null]]");

	VariantParser::StreamString ss;
	String errs;
	int line;
	Variant a_parsed;

	ss.s = a_str;
	VariantParser::parse(&ss, a_parsed, errs, line);

	CHECK_MESSAGE(a_parsed == Variant(a), "Should parse back.");
}

TEST_CASE("[Variant] Writer and parser nested typed container metadata") {
	ContainerType dictionary_type;
	dictionary_type.builtin_type = Variant::DICTIONARY;

	ContainerType key_type;
	key_type.builtin_type = Variant::STRING;
	dictionary_type.element_types.push_back(key_type);

	ContainerType value_type;
	value_type.builtin_type = Variant::INT;
	dictionary_type.element_types.push_back(value_type);

	Dictionary dictionary;
	dictionary["score"] = 10;

	Array array;
	array.set_typed(dictionary_type);
	array.push_back(dictionary);

	String array_str;
	VariantWriter::write_to_string(array, array_str);
	CHECK_EQ(array_str, "Array[Dictionary[String, int]]([Dictionary[String, int]({\n\"score\": 10\n})])");

	VariantParser::StreamString ss;
	String errs;
	int line;
	Variant parsed;

	ss.s = array_str;
	CHECK_EQ(VariantParser::parse(&ss, parsed, errs, line), OK);

	Array parsed_array = parsed;
	ContainerType parsed_element_type = parsed_array.get_element_type();
	CHECK_EQ(parsed_element_type, dictionary_type);

	Dictionary parsed_dictionary = parsed_array[0];
	CHECK_EQ(parsed_dictionary.get_key_type(), key_type);
	CHECK_EQ(parsed_dictionary.get_value_type(), value_type);
}

TEST_CASE("[Variant] Writer recursive array") {
	// There is no way to accurately represent a recursive array,
	// the only thing we can do is make sure the writer doesn't blow up

	// Self recursive
	Array a;
	a.push_back(a);

	// Writer should it recursion limit while visiting the array
	ERR_PRINT_OFF;
	String a_str;
	VariantWriter::write_to_string(a, a_str);
	ERR_PRINT_ON;

	// Nested recursive
	Array a1;
	Array a2;
	a1.push_back(a2);
	a2.push_back(a1);

	// Writer should it recursion limit while visiting the array
	ERR_PRINT_OFF;
	String a1_str;
	VariantWriter::write_to_string(a1, a1_str);
	ERR_PRINT_ON;

	// Break the recursivity otherwise Dictionary tearndown will leak memory
	a.clear();
	a1.clear();
	a2.clear();
}

TEST_CASE("[Variant] Writer and parser dictionary") {
	// d = {{1: 2}: 3, 4: "hello", 5: {null: []}}
	Dictionary d = { { Dictionary({ { 1, 2 } }), 3 }, { 4, String("hello") }, { 5, Dictionary({ { Variant(), Array() } }) } };
	String d_str;
	VariantWriter::write_to_string(d, d_str);

	CHECK_EQ(d_str, "{\n4: \"hello\",\n5: {\nnull: []\n},\n{\n1: 2\n}: 3\n}");

	VariantParser::StreamString ss;
	String errs;
	int line;
	Variant d_parsed;

	ss.s = d_str;
	VariantParser::parse(&ss, d_parsed, errs, line);

	CHECK_MESSAGE(d_parsed == Variant(d), "Should parse back.");
}

TEST_CASE("[Variant] Writer key sorting") {
	Dictionary d = { { StringName("C"), 3 }, { "A", 1 }, { StringName("B"), 2 }, { "D", 4 } };
	String d_str;
	VariantWriter::write_to_string(d, d_str);

	CHECK_EQ(d_str, "{\n\"A\": 1,\n&\"B\": 2,\n&\"C\": 3,\n\"D\": 4\n}");
}

TEST_CASE("[Variant] Writer recursive dictionary") {
	// There is no way to accurately represent a recursive dictionary,
	// the only thing we can do is make sure the writer doesn't blow up

	// Self recursive
	Dictionary d;
	d[1] = d;

	// Writer should it recursion limit while visiting the dictionary
	ERR_PRINT_OFF;
	String d_str;
	VariantWriter::write_to_string(d, d_str);
	ERR_PRINT_ON;

	// Nested recursive
	Dictionary d1;
	Dictionary d2;
	d1[2] = d2;
	d2[1] = d1;

	// Writer should it recursion limit while visiting the dictionary
	ERR_PRINT_OFF;
	String d1_str;
	VariantWriter::write_to_string(d1, d1_str);
	ERR_PRINT_ON;

	// Break the recursivity otherwise Dictionary tearndown will leak memory
	d.clear();
	d1.clear();
	d2.clear();
}

#if 0 // TODO: recursion in dict key is currently buggy
TEST_CASE("[Variant] Writer recursive dictionary on keys") {
	// There is no way to accurately represent a recursive dictionary,
	// the only thing we can do is make sure the writer doesn't blow up

	// Self recursive
	Dictionary d;
	d[d] = 1;

	// Writer should it recursion limit while visiting the dictionary
	ERR_PRINT_OFF;
	String d_str;
	VariantWriter::write_to_string(d, d_str);
	ERR_PRINT_ON;

	// Nested recursive
	Dictionary d1;
	Dictionary d2;
	d1[d2] = 2;
	d2[d1] = 1;

	// Writer should it recursion limit while visiting the dictionary
	ERR_PRINT_OFF;
	String d1_str;
	VariantWriter::write_to_string(d1, d1_str);
	ERR_PRINT_ON;

	// Break the recursivity otherwise Dictionary tearndown will leak memory
	d.clear();
	d1.clear();
	d2.clear();
}
#endif

TEST_CASE("[Variant] Basic comparison") {
	CHECK_EQ(Variant(1), Variant(1));
	CHECK_FALSE(Variant(1) != Variant(1));
	CHECK_NE(Variant(1), Variant(2));
	CHECK_EQ(Variant(String("foo")), Variant(String("foo")));
	CHECK_NE(Variant(String("foo")), Variant(String("bar")));
	// Check "empty" version of different types are not equivalents
	CHECK_NE(Variant(0), Variant());
	CHECK_NE(Variant(String()), Variant());
	CHECK_NE(Variant(Array()), Variant());
	CHECK_NE(Variant(Dictionary()), Variant());
}

TEST_CASE("[Variant] Identity comparison") {
	// Value types are compared by value
	Variant aabb = AABB();
	CHECK(aabb.identity_compare(aabb));
	CHECK(aabb.identity_compare(AABB()));
	CHECK_FALSE(aabb.identity_compare(AABB(Vector3(1, 2, 3), Vector3(1, 2, 3))));

	Variant basis = Basis();
	CHECK(basis.identity_compare(basis));
	CHECK(basis.identity_compare(Basis()));
	CHECK_FALSE(basis.identity_compare(Basis(Quaternion(Vector3(1, 2, 3).normalized(), 45))));

	Variant bool_var = true;
	CHECK(bool_var.identity_compare(bool_var));
	CHECK(bool_var.identity_compare(true));
	CHECK_FALSE(bool_var.identity_compare(false));

	Variant callable = Callable();
	CHECK(callable.identity_compare(callable));
	CHECK(callable.identity_compare(Callable()));
	CHECK_FALSE(callable.identity_compare(Callable(ObjectID(), StringName("lambda"))));

	Variant color = Color();
	CHECK(color.identity_compare(color));
	CHECK(color.identity_compare(Color()));
	CHECK_FALSE(color.identity_compare(Color(255, 0, 255)));

	Variant float_var = 1.0;
	CHECK(float_var.identity_compare(float_var));
	CHECK(float_var.identity_compare(1.0));
	CHECK_FALSE(float_var.identity_compare(2.0));

	Variant int_var = 1;
	CHECK(int_var.identity_compare(int_var));
	CHECK(int_var.identity_compare(1));
	CHECK_FALSE(int_var.identity_compare(2));

	Variant nil = Variant();
	CHECK(nil.identity_compare(nil));
	CHECK(nil.identity_compare(Variant()));
	CHECK_FALSE(nil.identity_compare(true));

	Variant node_path = NodePath("foundry");
	CHECK(node_path.identity_compare(node_path));
	CHECK(node_path.identity_compare(NodePath("foundry")));
	CHECK_FALSE(node_path.identity_compare(NodePath("waiting")));

	Variant plane = Plane();
	CHECK(plane.identity_compare(plane));
	CHECK(plane.identity_compare(Plane()));
	CHECK_FALSE(plane.identity_compare(Plane(Vector3(1, 2, 3), 42)));

	Variant projection = Projection();
	CHECK(projection.identity_compare(projection));
	CHECK(projection.identity_compare(Projection()));
	CHECK_FALSE(projection.identity_compare(Projection(Transform3D(Basis(Vector3(1, 2, 3).normalized(), 45), Vector3(1, 2, 3)))));

	Variant quaternion = Quaternion();
	CHECK(quaternion.identity_compare(quaternion));
	CHECK(quaternion.identity_compare(Quaternion()));
	CHECK_FALSE(quaternion.identity_compare(Quaternion(Vector3(1, 2, 3).normalized(), 45)));

	Variant rect2 = Rect2();
	CHECK(rect2.identity_compare(rect2));
	CHECK(rect2.identity_compare(Rect2()));
	CHECK_FALSE(rect2.identity_compare(Rect2(Point2(Vector2(1, 2)), Size2(Vector2(1, 2)))));

	Variant rect2i = Rect2i();
	CHECK(rect2i.identity_compare(rect2i));
	CHECK(rect2i.identity_compare(Rect2i()));
	CHECK_FALSE(rect2i.identity_compare(Rect2i(Point2i(Vector2i(1, 2)), Size2i(Vector2i(1, 2)))));

	Variant rid = RID();
	CHECK(rid.identity_compare(rid));
	CHECK(rid.identity_compare(RID()));
	CHECK_FALSE(rid.identity_compare(RID::from_uint64(123)));

	Variant signal = Signal();
	CHECK(signal.identity_compare(signal));
	CHECK(signal.identity_compare(Signal()));
	CHECK_FALSE(signal.identity_compare(Signal(ObjectID(), StringName("lambda"))));

	Variant str = "foundry";
	CHECK(str.identity_compare(str));
	CHECK(str.identity_compare("foundry"));
	CHECK_FALSE(str.identity_compare("waiting"));

	Variant str_name = StringName("foundry");
	CHECK(str_name.identity_compare(str_name));
	CHECK(str_name.identity_compare(StringName("foundry")));
	CHECK_FALSE(str_name.identity_compare(StringName("waiting")));

	Variant transform2d = Transform2D();
	CHECK(transform2d.identity_compare(transform2d));
	CHECK(transform2d.identity_compare(Transform2D()));
	CHECK_FALSE(transform2d.identity_compare(Transform2D(45, Vector2(1, 2))));

	Variant transform3d = Transform3D();
	CHECK(transform3d.identity_compare(transform3d));
	CHECK(transform3d.identity_compare(Transform3D()));
	CHECK_FALSE(transform3d.identity_compare(Transform3D(Basis(Quaternion(Vector3(1, 2, 3).normalized(), 45)), Vector3(1, 2, 3))));

	Variant vect2 = Vector2();
	CHECK(vect2.identity_compare(vect2));
	CHECK(vect2.identity_compare(Vector2()));
	CHECK_FALSE(vect2.identity_compare(Vector2(1, 2)));

	Variant vect2i = Vector2i();
	CHECK(vect2i.identity_compare(vect2i));
	CHECK(vect2i.identity_compare(Vector2i()));
	CHECK_FALSE(vect2i.identity_compare(Vector2i(1, 2)));

	Variant vect3 = Vector3();
	CHECK(vect3.identity_compare(vect3));
	CHECK(vect3.identity_compare(Vector3()));
	CHECK_FALSE(vect3.identity_compare(Vector3(1, 2, 3)));

	Variant vect3i = Vector3i();
	CHECK(vect3i.identity_compare(vect3i));
	CHECK(vect3i.identity_compare(Vector3i()));
	CHECK_FALSE(vect3i.identity_compare(Vector3i(1, 2, 3)));

	Variant vect4 = Vector4();
	CHECK(vect4.identity_compare(vect4));
	CHECK(vect4.identity_compare(Vector4()));
	CHECK_FALSE(vect4.identity_compare(Vector4(1, 2, 3, 4)));

	Variant vect4i = Vector4i();
	CHECK(vect4i.identity_compare(vect4i));
	CHECK(vect4i.identity_compare(Vector4i()));
	CHECK_FALSE(vect4i.identity_compare(Vector4i(1, 2, 3, 4)));

	// Reference types are compared by reference
	Variant array = Array();
	CHECK(array.identity_compare(array));
	CHECK_FALSE(array.identity_compare(Array()));

	Variant dictionary = Dictionary();
	CHECK(dictionary.identity_compare(dictionary));
	CHECK_FALSE(dictionary.identity_compare(Dictionary()));

	Variant packed_byte_array = PackedByteArray();
	CHECK(packed_byte_array.identity_compare(packed_byte_array));
	CHECK_FALSE(packed_byte_array.identity_compare(PackedByteArray()));

	Variant packed_color_array = PackedColorArray();
	CHECK(packed_color_array.identity_compare(packed_color_array));
	CHECK_FALSE(packed_color_array.identity_compare(PackedColorArray()));

	Variant packed_vector4_array = PackedVector4Array();
	CHECK(packed_vector4_array.identity_compare(packed_vector4_array));
	CHECK_FALSE(packed_vector4_array.identity_compare(PackedVector4Array()));

	Variant packed_float32_array = PackedFloat32Array();
	CHECK(packed_float32_array.identity_compare(packed_float32_array));
	CHECK_FALSE(packed_float32_array.identity_compare(PackedFloat32Array()));

	Variant packed_float64_array = PackedFloat64Array();
	CHECK(packed_float64_array.identity_compare(packed_float64_array));
	CHECK_FALSE(packed_float64_array.identity_compare(PackedFloat64Array()));

	Variant packed_int32_array = PackedInt32Array();
	CHECK(packed_int32_array.identity_compare(packed_int32_array));
	CHECK_FALSE(packed_int32_array.identity_compare(PackedInt32Array()));

	Variant packed_int64_array = PackedInt64Array();
	CHECK(packed_int64_array.identity_compare(packed_int64_array));
	CHECK_FALSE(packed_int64_array.identity_compare(PackedInt64Array()));

	Variant packed_string_array = PackedStringArray();
	CHECK(packed_string_array.identity_compare(packed_string_array));
	CHECK_FALSE(packed_string_array.identity_compare(PackedStringArray()));

	Variant packed_vector2_array = PackedVector2Array();
	CHECK(packed_vector2_array.identity_compare(packed_vector2_array));
	CHECK_FALSE(packed_vector2_array.identity_compare(PackedVector2Array()));

	Variant packed_vector3_array = PackedVector3Array();
	CHECK(packed_vector3_array.identity_compare(packed_vector3_array));
	CHECK_FALSE(packed_vector3_array.identity_compare(PackedVector3Array()));

	Object obj_one = Object();
	Variant obj_one_var = &obj_one;
	Object obj_two = Object();
	Variant obj_two_var = &obj_two;
	CHECK(obj_one_var.identity_compare(obj_one_var));
	CHECK_FALSE(obj_one_var.identity_compare(obj_two_var));

	Variant obj_null_one_var = Variant((Object *)nullptr);
	Variant obj_null_two_var = Variant((Object *)nullptr);
	CHECK(obj_null_one_var.identity_compare(obj_null_one_var));
	CHECK(obj_null_one_var.identity_compare(obj_null_two_var));

	Object *freed_one = new Object();
	Variant freed_one_var = freed_one;
	delete freed_one;
	Object *freed_two = new Object();
	Variant freed_two_var = freed_two;
	delete freed_two;
	CHECK_FALSE(freed_one_var.identity_compare(freed_two_var));
}

TEST_CASE("[Variant] Nested array comparison") {
	Array a1 = { 1, { 2, 3 } };
	Array a2 = { 1, { 2, 3 } };
	Array a_other = { 1, { 2, 4 } };
	Variant v_a1 = a1;
	Variant v_a1_ref2 = a1;
	Variant v_a2 = a2;
	Variant v_a_other = a_other;

	// test both operator== and operator!=
	CHECK_EQ(v_a1, v_a1);
	CHECK_FALSE(v_a1 != v_a1);
	CHECK_EQ(v_a1, v_a1_ref2);
	CHECK_FALSE(v_a1 != v_a1_ref2);
	CHECK_EQ(v_a1, v_a2);
	CHECK_FALSE(v_a1 != v_a2);
	CHECK_NE(v_a1, v_a_other);
	CHECK_FALSE(v_a1 == v_a_other);
}

TEST_CASE("[Variant] Nested dictionary comparison") {
	Dictionary d1 = { { Dictionary({ { 1, 2 } }), Dictionary({ { 3, 4 } }) } };
	Dictionary d2 = { { Dictionary({ { 1, 2 } }), Dictionary({ { 3, 4 } }) } };
	Dictionary d_other_key = { { Dictionary({ { 1, 0 } }), Dictionary({ { 3, 4 } }) } };
	Dictionary d_other_val = { { Dictionary({ { 1, 2 } }), Dictionary({ { 3, 0 } }) } };
	Variant v_d1 = d1;
	Variant v_d1_ref2 = d1;
	Variant v_d2 = d2;
	Variant v_d_other_key = d_other_key;
	Variant v_d_other_val = d_other_val;

	// test both operator== and operator!=
	CHECK_EQ(v_d1, v_d1);
	CHECK_FALSE(v_d1 != v_d1);
	CHECK_EQ(v_d1, v_d1_ref2);
	CHECK_FALSE(v_d1 != v_d1_ref2);
	CHECK_EQ(v_d1, v_d2);
	CHECK_FALSE(v_d1 != v_d2);
	CHECK_NE(v_d1, v_d_other_key);
	CHECK_FALSE(v_d1 == v_d_other_key);
	CHECK_NE(v_d1, v_d_other_val);
	CHECK_FALSE(v_d1 == v_d_other_val);
}

struct ArgumentData {
	Variant::Type type;
	String name;
	bool has_defval = false;
	Variant defval;
	int position;
};

struct MethodData {
	StringName name;
	Variant::Type return_type;
	List<ArgumentData> arguments;
	bool is_virtual = false;
	bool is_vararg = false;
};

TEST_CASE("[Variant] Utility functions") {
	List<MethodData> functions;

	List<StringName> function_names;
	Variant::get_utility_function_list(&function_names);
	function_names.sort_custom<StringName::AlphCompare>();

	for (const StringName &E : function_names) {
		MethodData md;
		md.name = E;

		// Utility function's return type.
		if (Variant::has_utility_function_return_value(E)) {
			md.return_type = Variant::get_utility_function_return_type(E);
		}

		// Utility function's arguments.
		if (Variant::is_utility_function_vararg(E)) {
			md.is_vararg = true;
		} else {
			for (int i = 0; i < Variant::get_utility_function_argument_count(E); i++) {
				ArgumentData arg;
				arg.type = Variant::get_utility_function_argument_type(E, i);
				arg.name = Variant::get_utility_function_argument_name(E, i);
				arg.position = i;

				md.arguments.push_back(arg);
			}
		}

		functions.push_back(md);
	}

	SUBCASE("[Variant] Validate utility functions") {
		for (const MethodData &E : functions) {
			for (const ArgumentData &F : E.arguments) {
				const ArgumentData &arg = F;

				TEST_COND((arg.name.is_empty() || arg.name.begins_with("_unnamed_arg")),
						vformat("Unnamed argument in position %d of function '%s'.", arg.position, E.name));
			}
		}
	}
}

TEST_CASE("[Variant] Operator NOT") {
	// Verify that operator NOT works for all types and is consistent with booleanize().
	for (int i = 0; i < Variant::VARIANT_MAX; i++) {
		Variant value;
		Callable::CallError err;
		Variant::construct((Variant::Type)i, value, nullptr, 0, err);

		REQUIRE_EQ(err.error, Callable::CallError::CALL_OK);

		Variant result = Variant::evaluate(Variant::OP_NOT, value, Variant());

		REQUIRE_EQ(result.get_type(), Variant::BOOL);
		CHECK_EQ(!value.booleanize(), result.operator bool());
	}
}

TEST_CASE("[Variant][UInt] The unsigned carrier is appended without renumbering") {
	// Stored type IDs and generated extension interfaces depend on these numbers, so the unsigned
	// carrier must occupy the slot after the last previously defined type.
	CHECK_EQ(int(Variant::NIL), 0);
	CHECK_EQ(int(Variant::BOOL), 1);
	CHECK_EQ(int(Variant::INT), 2);
	CHECK_EQ(int(Variant::FLOAT), 3);
	CHECK_EQ(int(Variant::STRING), 4);
	CHECK_EQ(int(Variant::VECTOR2), 5);
	CHECK_EQ(int(Variant::VECTOR2I), 6);
	CHECK_EQ(int(Variant::RECT2), 7);
	CHECK_EQ(int(Variant::RECT2I), 8);
	CHECK_EQ(int(Variant::VECTOR3), 9);
	CHECK_EQ(int(Variant::VECTOR3I), 10);
	CHECK_EQ(int(Variant::TRANSFORM2D), 11);
	CHECK_EQ(int(Variant::VECTOR4), 12);
	CHECK_EQ(int(Variant::VECTOR4I), 13);
	CHECK_EQ(int(Variant::PLANE), 14);
	CHECK_EQ(int(Variant::QUATERNION), 15);
	CHECK_EQ(int(Variant::AABB), 16);
	CHECK_EQ(int(Variant::BASIS), 17);
	CHECK_EQ(int(Variant::TRANSFORM3D), 18);
	CHECK_EQ(int(Variant::PROJECTION), 19);
	CHECK_EQ(int(Variant::COLOR), 20);
	CHECK_EQ(int(Variant::STRING_NAME), 21);
	CHECK_EQ(int(Variant::NODE_PATH), 22);
	CHECK_EQ(int(Variant::RID), 23);
	CHECK_EQ(int(Variant::OBJECT), 24);
	CHECK_EQ(int(Variant::CALLABLE), 25);
	CHECK_EQ(int(Variant::SIGNAL), 26);
	CHECK_EQ(int(Variant::DICTIONARY), 27);
	CHECK_EQ(int(Variant::ARRAY), 28);
	CHECK_EQ(int(Variant::PACKED_BYTE_ARRAY), 29);
	CHECK_EQ(int(Variant::PACKED_INT32_ARRAY), 30);
	CHECK_EQ(int(Variant::PACKED_INT64_ARRAY), 31);
	CHECK_EQ(int(Variant::PACKED_FLOAT32_ARRAY), 32);
	CHECK_EQ(int(Variant::PACKED_FLOAT64_ARRAY), 33);
	CHECK_EQ(int(Variant::PACKED_STRING_ARRAY), 34);
	CHECK_EQ(int(Variant::PACKED_VECTOR2_ARRAY), 35);
	CHECK_EQ(int(Variant::PACKED_VECTOR3_ARRAY), 36);
	CHECK_EQ(int(Variant::PACKED_COLOR_ARRAY), 37);
	CHECK_EQ(int(Variant::PACKED_VECTOR4_ARRAY), 38);
	CHECK_EQ(int(Variant::UINT), 39);
	CHECK_EQ(int(Variant::VARIANT_MAX), 40);
}

TEST_CASE("[Variant][UInt] The carrier does not grow Variant storage") {
	// The unsigned payload shares the existing inline data union, so neither footprint changes.
	constexpr size_t expected_size = sizeof(real_t) == 4 ? 24 : 40;
	CHECK_EQ(sizeof(Variant), expected_size);
	CHECK_EQ(alignof(Variant), 8u);
}

// The unsigned carrier has no C++ nominal type and no source spelling, so tests build it through
// the same Variant storage entry point the engine uses.
static Variant make_uint(uint64_t p_value) {
	Variant value;
	VariantInternal::initialize(&value, Variant::UINT);
	*VariantInternal::get_uint(&value) = p_value;
	return value;
}

TEST_CASE("[Variant][UInt] Construction preserves the full unsigned range") {
	const uint64_t values[] = { 0, 1, uint64_t(INT64_MAX), uint64_t(INT64_MAX) + 1, UINT64_MAX };
	for (uint64_t value : values) {
		const Variant variant = make_uint(value);
		CHECK_EQ(variant.get_type(), Variant::UINT);
		CHECK_EQ(variant.operator uint64_t(), value);
	}
}

TEST_CASE("[Variant][UInt] Unsigned C++ constructors select the unsigned carrier") {
	CHECK_EQ(Variant(uint8_t(200)).get_type(), Variant::UINT);
	CHECK_EQ(Variant(uint16_t(60000)).get_type(), Variant::UINT);
	CHECK_EQ(Variant(uint32_t(4000000000u)).get_type(), Variant::UINT);
	CHECK_EQ(Variant(uint64_t(1)).get_type(), Variant::UINT);
	CHECK_EQ(Variant(Math::uint_alt_t(7)).get_type(), Variant::UINT);
	CHECK_EQ(Variant(int64_t(-1)).get_type(), Variant::INT);

	// Nominal wrappers keep their existing carrier contract regardless.
	CHECK_EQ(Variant(ObjectID(uint64_t(12345))).get_type(), Variant::INT);
	CHECK_EQ(Variant(ObjectID(uint64_t(12345))).operator ObjectID(), ObjectID(uint64_t(12345)));
}

TEST_CASE("[Variant][UInt] The registered constructor default-initializes to zero") {
	CHECK_GT(Variant::get_constructor_count(Variant::UINT), 0);

	Callable::CallError error;
	Variant constructed = "not an integer";
	Variant::construct(Variant::UINT, constructed, nullptr, 0, error);
	REQUIRE_EQ(error.error, Callable::CallError::CALL_OK);
	CHECK_EQ(constructed.get_type(), Variant::UINT);
	CHECK_EQ(constructed.operator uint64_t(), 0u);

	const Variant source = make_uint(UINT64_MAX);
	const Variant *arguments[] = { &source };
	Variant copied;
	Variant::construct(Variant::UINT, copied, arguments, 1, error);
	REQUIRE_EQ(error.error, Callable::CallError::CALL_OK);
	CHECK_EQ(copied.get_type(), Variant::UINT);
	CHECK_EQ(copied.operator uint64_t(), UINT64_MAX);

	// The registered constructor also has to tolerate an aliased destination.
	Variant aliased = make_uint(uint64_t(INT64_MAX) + 1);
	const Variant *self_arguments[] = { &aliased };
	Variant::construct(Variant::UINT, aliased, self_arguments, 1, error);
	REQUIRE_EQ(error.error, Callable::CallError::CALL_OK);
	CHECK_EQ(aliased.get_type(), Variant::UINT);
	CHECK_EQ(aliased.operator uint64_t(), uint64_t(INT64_MAX) + 1);
}

TEST_CASE("[Variant][UInt] Copy, move, assignment, and clear preserve the carrier") {
	const Variant original = make_uint(UINT64_MAX);

	const Variant copied = original;
	CHECK_EQ(copied.get_type(), Variant::UINT);
	CHECK_EQ(copied.operator uint64_t(), UINT64_MAX);

	Variant movable = make_uint(uint64_t(INT64_MAX) + 1);
	const Variant moved = std::move(movable);
	CHECK_EQ(moved.get_type(), Variant::UINT);
	CHECK_EQ(moved.operator uint64_t(), uint64_t(INT64_MAX) + 1);

	Variant assigned;
	assigned = original;
	CHECK_EQ(assigned.get_type(), Variant::UINT);
	CHECK_EQ(assigned.operator uint64_t(), UINT64_MAX);

	// Assigning across an allocating type and back must release and re-tag cleanly.
	assigned = String("a string that owns memory");
	CHECK_EQ(assigned.get_type(), Variant::STRING);
	assigned = make_uint(7);
	CHECK_EQ(assigned.get_type(), Variant::UINT);
	CHECK_EQ(assigned.operator uint64_t(), 7u);

	assigned.zero();
	CHECK_EQ(assigned.get_type(), Variant::UINT);
	CHECK_EQ(assigned.operator uint64_t(), 0u);

	CHECK_FALSE(Variant::has_destructor(Variant::UINT));
	CHECK_FALSE(Variant::is_type_shared(Variant::UINT));
	CHECK_FALSE(make_uint(1).is_array());
	CHECK(make_uint(1).is_num());
	CHECK(make_uint(0).is_zero());
	CHECK(make_uint(1).is_one());
	CHECK_EQ(make_uint(UINT64_MAX).stringify(), "18446744073709551615");
}

TEST_CASE("[Variant][UInt] Same-carrier values compare and hash by value") {
	CHECK(make_uint(UINT64_MAX) == make_uint(UINT64_MAX));
	CHECK_FALSE(make_uint(UINT64_MAX) == make_uint(0));
	CHECK_EQ(make_uint(UINT64_MAX).hash(), make_uint(UINT64_MAX).hash());

	Dictionary dictionary;
	dictionary[make_uint(UINT64_MAX)] = "answer";
	CHECK_EQ(dictionary[make_uint(UINT64_MAX)], "answer");
}

// Asserts that `p_left` relates to `p_right` exactly as `p_order` says (`-1` less, `0` equal,
// `1` greater) across every comparison surface a Variant exposes.
static void check_integer_relation(const Variant &p_left, const Variant &p_right, int p_order) {
	struct RelationalOperator {
		Variant::Operator op;
		bool expected_for_less;
		bool expected_for_equal;
		bool expected_for_greater;
	};
	const RelationalOperator operators[] = {
		{ Variant::OP_EQUAL, false, true, false },
		{ Variant::OP_NOT_EQUAL, true, false, true },
		{ Variant::OP_LESS, true, false, false },
		{ Variant::OP_LESS_EQUAL, true, true, false },
		{ Variant::OP_GREATER, false, false, true },
		{ Variant::OP_GREATER_EQUAL, false, true, true },
	};

	for (const RelationalOperator &relational : operators) {
		const bool expected = p_order < 0 ? relational.expected_for_less
										  : (p_order == 0 ? relational.expected_for_equal : relational.expected_for_greater);
		bool valid = false;
		Variant result;
		Variant::evaluate(relational.op, p_left, p_right, result, valid);
		INFO(vformat("Operator '%s' on '%s' and '%s'.", Variant::get_operator_name(relational.op), p_left.stringify(), p_right.stringify()));
		REQUIRE(valid);
		REQUIRE_EQ(result.get_type(), Variant::BOOL);
		CHECK_EQ(result.operator bool(), expected);
	}

	CHECK_EQ(p_left == p_right, p_order == 0);
	CHECK_EQ(p_left < p_right, p_order < 0);
	if (p_order == 0) {
		CHECK_EQ(p_left.hash(), p_right.hash());
	}
}

TEST_CASE("[Variant][UInt] Signed and unsigned values compare as one number line") {
	const int64_t signed_values[] = { -1, 0, 1, INT64_MAX };
	const uint64_t unsigned_values[] = { 0, 1, uint64_t(INT64_MAX), uint64_t(INT64_MAX) + 1, UINT64_MAX };

	for (int64_t signed_value : signed_values) {
		for (uint64_t unsigned_value : unsigned_values) {
			// A negative signed value is below every unsigned value; otherwise both sides are
			// exact non-negative magnitudes.
			int order = 0;
			if (signed_value < 0) {
				order = -1;
			} else if (uint64_t(signed_value) < unsigned_value) {
				order = -1;
			} else if (uint64_t(signed_value) > unsigned_value) {
				order = 1;
			}

			check_integer_relation(Variant(signed_value), make_uint(unsigned_value), order);
			check_integer_relation(make_uint(unsigned_value), Variant(signed_value), -order);
		}
	}

	for (uint64_t left : unsigned_values) {
		for (uint64_t right : unsigned_values) {
			const int order = left < right ? -1 : (left == right ? 0 : 1);
			check_integer_relation(make_uint(left), make_uint(right), order);
		}
	}
}

TEST_CASE("[Variant][UInt] Equal integer values across carriers hash and key identically") {
	const int64_t shared_values[] = { 0, 1, 4096, INT64_MAX };
	for (int64_t value : shared_values) {
		CHECK_EQ(Variant(value).hash(), make_uint(uint64_t(value)).hash());
	}

	// The upper half of the unsigned range must not be truncated or folded into the signed range.
	CHECK_NE(make_uint(uint64_t(INT64_MAX) + 1).hash(), make_uint(0).hash());
	CHECK_NE(make_uint(UINT64_MAX).hash(), make_uint(uint64_t(UINT32_MAX)).hash());
	CHECK_NE(make_uint(uint64_t(1) << 63).hash(), make_uint(1).hash());

	Dictionary keyed_by_signed;
	keyed_by_signed[Variant(int64_t(42))] = "answer";
	CHECK_EQ(keyed_by_signed[make_uint(42)], "answer");
	CHECK_EQ(keyed_by_signed.size(), 1);
	keyed_by_signed[make_uint(42)] = "replaced";
	CHECK_EQ(keyed_by_signed.size(), 1);
	CHECK_EQ(keyed_by_signed[Variant(int64_t(42))], "replaced");

	Dictionary keyed_by_unsigned;
	keyed_by_unsigned[make_uint(42)] = "answer";
	CHECK_EQ(keyed_by_unsigned[Variant(int64_t(42))], "answer");

	// A negative signed value never matches an unsigned key, even when the bit patterns agree.
	Dictionary negative_keys;
	negative_keys[Variant(int64_t(-1))] = "negative";
	CHECK_FALSE(negative_keys.has(make_uint(UINT64_MAX)));
}

TEST_CASE("[Variant][UInt] Comparing the unsigned carrier against null works in both operand orders") {
	const Variant null_value;
	const Variant unsigned_value = make_uint(UINT64_MAX);

	for (int order = 0; order < 2; order++) {
		const Variant &left = order == 0 ? null_value : unsigned_value;
		const Variant &right = order == 0 ? unsigned_value : null_value;

		bool valid = false;
		Variant result;
		Variant::evaluate(Variant::OP_EQUAL, left, right, result, valid);
		REQUIRE(valid);
		CHECK_FALSE(result.operator bool());

		valid = false;
		Variant::evaluate(Variant::OP_NOT_EQUAL, left, right, result, valid);
		REQUIRE(valid);
		CHECK(result.operator bool());
	}

	CHECK_FALSE(null_value == unsigned_value);
	CHECK_FALSE(unsigned_value == null_value);
}

TEST_CASE("[Variant][UInt] Ordering stays a strict weak ordering once carriers are mixed") {
	// `Variant::operator<` backs `Comparator<Variant>`, so introducing value-based ordering between
	// the integer carriers must not make the relation cyclic against any third type.
	const Variant values[] = {
		Variant(),
		Variant(false),
		Variant(int64_t(INT64_MIN)),
		Variant(int64_t(-1)),
		Variant(int64_t(0)),
		Variant(int64_t(5)),
		Variant(int64_t(INT64_MAX)),
		make_uint(0),
		make_uint(5),
		make_uint(uint64_t(INT64_MAX)),
		make_uint(uint64_t(INT64_MAX) + 1),
		make_uint(UINT64_MAX),
		Variant(2.5),
		Variant(String("text")),
		Variant(Vector2(1, 2)),
	};
	constexpr int value_count = int(sizeof(values) / sizeof(values[0]));

	for (int a = 0; a < value_count; a++) {
		CHECK_FALSE(values[a] < values[a]);
		for (int b = 0; b < value_count; b++) {
			const bool a_before_b = values[a] < values[b];
			// Antisymmetric.
			const bool both_directions = a_before_b && values[b] < values[a];
			CHECK_FALSE(both_directions);
			for (int c = 0; c < value_count; c++) {
				if (a_before_b && values[b] < values[c]) {
					INFO(vformat("'%s' < '%s' < '%s'.", values[a].stringify(), values[b].stringify(), values[c].stringify()));
					CHECK(values[a] < values[c]);
				}
			}
		}
	}

	// Sorting a mixed sequence still groups the integer carriers together and by value.
	Vector<Variant> mixed;
	mixed.push_back(Variant(2.5));
	mixed.push_back(make_uint(UINT64_MAX));
	mixed.push_back(Variant(int64_t(-1)));
	mixed.push_back(make_uint(0));
	mixed.push_back(Variant(int64_t(7)));
	mixed.sort();

	REQUIRE_EQ(mixed.size(), 5);
	CHECK_EQ(mixed[0].operator int64_t(), -1);
	CHECK_EQ(mixed[1].get_type(), Variant::UINT);
	CHECK_EQ(mixed[1].operator uint64_t(), 0u);
	CHECK_EQ(mixed[2].operator int64_t(), 7);
	CHECK_EQ(mixed[3].operator uint64_t(), UINT64_MAX);
	CHECK_EQ(mixed[4].get_type(), Variant::FLOAT);
}

TEST_CASE("[Variant][UInt] Checked conversion between the integer carriers reports range failure") {
	// Both directions exist as conversions, but neither is a strict conversion because either can
	// fail on a value outside the destination range.
	CHECK(Variant::can_convert(Variant::INT, Variant::UINT));
	CHECK(Variant::can_convert(Variant::UINT, Variant::INT));
	CHECK_FALSE(Variant::can_convert_strict(Variant::INT, Variant::UINT));
	CHECK_FALSE(Variant::can_convert_strict(Variant::UINT, Variant::INT));

	Callable::CallError error;
	Variant converted;

	const int64_t accepted_signed[] = { 0, 1, INT64_MAX };
	for (int64_t value : accepted_signed) {
		const Variant source = value;
		const Variant *arguments[] = { &source };
		Variant::construct(Variant::UINT, converted, arguments, 1, error);
		REQUIRE_EQ(error.error, Callable::CallError::CALL_OK);
		CHECK_EQ(converted.get_type(), Variant::UINT);
		CHECK_EQ(converted.operator uint64_t(), uint64_t(value));
	}

	const int64_t rejected_signed[] = { -1, INT64_MIN };
	for (int64_t value : rejected_signed) {
		const Variant source = value;
		const Variant *arguments[] = { &source };
		Variant::construct(Variant::UINT, converted, arguments, 1, error);
		CHECK_EQ(error.error, Callable::CallError::CALL_ERROR_INVALID_ARGUMENT);
	}

	const uint64_t accepted_unsigned[] = { 0, 1, uint64_t(INT64_MAX) };
	for (uint64_t value : accepted_unsigned) {
		const Variant source = make_uint(value);
		const Variant *arguments[] = { &source };
		Variant::construct(Variant::INT, converted, arguments, 1, error);
		REQUIRE_EQ(error.error, Callable::CallError::CALL_OK);
		CHECK_EQ(converted.get_type(), Variant::INT);
		CHECK_EQ(converted.operator int64_t(), int64_t(value));
	}

	const uint64_t rejected_unsigned[] = { uint64_t(INT64_MAX) + 1, UINT64_MAX };
	for (uint64_t value : rejected_unsigned) {
		const Variant source = make_uint(value);
		const Variant *arguments[] = { &source };
		Variant::construct(Variant::INT, converted, arguments, 1, error);
		CHECK_EQ(error.error, Callable::CallError::CALL_ERROR_INVALID_ARGUMENT);
	}
}

TEST_CASE("[Variant][UInt] Mixed signed and unsigned arithmetic stays unregistered") {
	const Variant signed_one = int64_t(1);
	const Variant unsigned_one = make_uint(1);
	const Variant::Operator arithmetic[] = {
		Variant::OP_ADD,
		Variant::OP_SUBTRACT,
		Variant::OP_MULTIPLY,
		Variant::OP_DIVIDE,
		Variant::OP_MODULE,
		Variant::OP_POWER,
		Variant::OP_SHIFT_LEFT,
		Variant::OP_SHIFT_RIGHT,
		Variant::OP_BIT_AND,
		Variant::OP_BIT_OR,
		Variant::OP_BIT_XOR,
	};

	for (Variant::Operator op : arithmetic) {
		bool valid = true;
		Variant result;
		Variant::evaluate(op, signed_one, unsigned_one, result, valid);
		CHECK_FALSE(valid);
		CHECK_EQ(Variant::get_operator_return_type(op, Variant::INT, Variant::UINT), Variant::NIL);

		valid = true;
		Variant::evaluate(op, unsigned_one, signed_one, result, valid);
		CHECK_FALSE(valid);
		CHECK_EQ(Variant::get_operator_return_type(op, Variant::UINT, Variant::INT), Variant::NIL);
	}

	// Unary negation has no meaning for a carrier that cannot hold a negative value.
	bool valid = true;
	Variant result;
	Variant::evaluate(Variant::OP_NEGATE, unsigned_one, Variant(), result, valid);
	CHECK_FALSE(valid);
}

// Evaluates `p_op` on the unsigned carrier through the plain, validated, and pointer operator
// tables, checks the three agree, and returns the plain-path result.
static uint64_t evaluate_uint_binary(Variant::Operator p_op, uint64_t p_left, uint64_t p_right) {
	const Variant left = make_uint(p_left);
	const Variant right = make_uint(p_right);

	bool valid = false;
	Variant result;
	Variant::evaluate(p_op, left, right, result, valid);
	REQUIRE(valid);
	REQUIRE_EQ(result.get_type(), Variant::UINT);
	REQUIRE_EQ(Variant::get_operator_return_type(p_op, Variant::UINT, Variant::UINT), Variant::UINT);

	const Variant::ValidatedOperatorEvaluator validated = Variant::get_validated_operator_evaluator(p_op, Variant::UINT, Variant::UINT);
	REQUIRE(validated != nullptr);
	if (validated != nullptr) {
		Variant validated_result;
		VariantInternal::initialize(&validated_result, Variant::UINT);
		validated(&left, &right, &validated_result);
		CHECK_EQ(validated_result.get_type(), Variant::UINT);
		CHECK_EQ(validated_result.operator uint64_t(), result.operator uint64_t());
	}

	const Variant::PTROperatorEvaluator pointer = Variant::get_ptr_operator_evaluator(p_op, Variant::UINT, Variant::UINT);
	REQUIRE(pointer != nullptr);
	if (pointer != nullptr) {
		uint64_t pointer_result = 0;
		pointer(&p_left, &p_right, &pointer_result);
		CHECK_EQ(pointer_result, result.operator uint64_t());
	}

	return result.operator uint64_t();
}

TEST_CASE("[Variant][UInt] Same-carrier arithmetic wraps modulo 2^64") {
	// The signed carrier cannot express these results, so a UINT return type is itself evidence that
	// the evaluators use unsigned arithmetic rather than overflowing `int64_t`.
	CHECK_EQ(evaluate_uint_binary(Variant::OP_ADD, UINT64_MAX, 1), 0u);
	CHECK_EQ(evaluate_uint_binary(Variant::OP_ADD, UINT64_MAX, UINT64_MAX), UINT64_MAX - 1);
	CHECK_EQ(evaluate_uint_binary(Variant::OP_ADD, uint64_t(INT64_MAX), 1), uint64_t(INT64_MAX) + 1);

	CHECK_EQ(evaluate_uint_binary(Variant::OP_SUBTRACT, 0, 1), UINT64_MAX);
	CHECK_EQ(evaluate_uint_binary(Variant::OP_SUBTRACT, 0, UINT64_MAX), 1u);
	CHECK_EQ(evaluate_uint_binary(Variant::OP_SUBTRACT, UINT64_MAX, UINT64_MAX), 0u);

	CHECK_EQ(evaluate_uint_binary(Variant::OP_MULTIPLY, UINT64_MAX, 2), UINT64_MAX - 1);
	CHECK_EQ(evaluate_uint_binary(Variant::OP_MULTIPLY, uint64_t(1) << 32, uint64_t(1) << 32), 0u);
	CHECK_EQ(evaluate_uint_binary(Variant::OP_MULTIPLY, UINT64_MAX, 0), 0u);
}

TEST_CASE("[Variant][UInt] Division and remainder use the full unsigned range") {
	CHECK_EQ(evaluate_uint_binary(Variant::OP_DIVIDE, UINT64_MAX, 2), uint64_t(INT64_MAX));
	CHECK_EQ(evaluate_uint_binary(Variant::OP_DIVIDE, UINT64_MAX, UINT64_MAX), 1u);
	CHECK_EQ(evaluate_uint_binary(Variant::OP_DIVIDE, 0, UINT64_MAX), 0u);
	CHECK_EQ(evaluate_uint_binary(Variant::OP_MODULE, UINT64_MAX, 10), 5u);
	CHECK_EQ(evaluate_uint_binary(Variant::OP_MODULE, uint64_t(INT64_MAX) + 1, 3), 2u);

	bool valid = true;
	Variant result;
	Variant::evaluate(Variant::OP_DIVIDE, make_uint(UINT64_MAX), make_uint(0), result, valid);
	CHECK_FALSE(valid);
	REQUIRE_EQ(result.get_type(), Variant::STRING);
	CHECK_EQ(result.operator String(), String("Division by zero error"));

	valid = true;
	Variant::evaluate(Variant::OP_MODULE, make_uint(UINT64_MAX), make_uint(0), result, valid);
	CHECK_FALSE(valid);
	REQUIRE_EQ(result.get_type(), Variant::STRING);
	CHECK_EQ(result.operator String(), String("Modulo by zero error"));
}

TEST_CASE("[Variant][UInt] Power is exact unsigned exponentiation that wraps") {
	CHECK_EQ(evaluate_uint_binary(Variant::OP_POWER, 2, 63), uint64_t(1) << 63);
	CHECK_EQ(evaluate_uint_binary(Variant::OP_POWER, 2, 64), 0u);
	CHECK_EQ(evaluate_uint_binary(Variant::OP_POWER, UINT64_MAX, 1), UINT64_MAX);
	CHECK_EQ(evaluate_uint_binary(Variant::OP_POWER, UINT64_MAX, 0), 1u);
	CHECK_EQ(evaluate_uint_binary(Variant::OP_POWER, 0, 0), 1u);
	CHECK_EQ(evaluate_uint_binary(Variant::OP_POWER, 0, 5), 0u);

	// 3^40 needs 64 bits of mantissa, so a floating-point implementation would round it.
	CHECK_EQ(evaluate_uint_binary(Variant::OP_POWER, 3, 40), uint64_t(12157665459056928801ULL));
}

TEST_CASE("[Variant][UInt] Shifts are logical across the full width") {
	CHECK_EQ(evaluate_uint_binary(Variant::OP_SHIFT_LEFT, 1, 63), uint64_t(1) << 63);
	CHECK_EQ(evaluate_uint_binary(Variant::OP_SHIFT_LEFT, uint64_t(1) << 63, 1), 0u);
	CHECK_EQ(evaluate_uint_binary(Variant::OP_SHIFT_LEFT, UINT64_MAX, 63), uint64_t(1) << 63);

	// A signed right shift of the same bit pattern would replicate the sign bit instead.
	CHECK_EQ(evaluate_uint_binary(Variant::OP_SHIFT_RIGHT, uint64_t(1) << 63, 63), 1u);
	CHECK_EQ(evaluate_uint_binary(Variant::OP_SHIFT_RIGHT, UINT64_MAX, 1), uint64_t(INT64_MAX));
	CHECK_EQ(evaluate_uint_binary(Variant::OP_SHIFT_RIGHT, UINT64_MAX, 63), 1u);

	const Variant::Operator shifts[] = { Variant::OP_SHIFT_LEFT, Variant::OP_SHIFT_RIGHT };
	for (Variant::Operator op : shifts) {
		for (uint64_t count : { uint64_t(64), UINT64_MAX }) {
			bool valid = true;
			Variant result;
			Variant::evaluate(op, make_uint(1), make_uint(count), result, valid);
			CHECK_FALSE(valid);
			CHECK_EQ(result.get_type(), Variant::STRING);
		}
	}
}

TEST_CASE("[Variant][UInt] Validated and pointer paths refuse operands they cannot compute") {
	// Neither entry point can report failure through its signature, so instead of running the
	// undefined C++ operation they must fall back to a defined result.
	struct RejectedCase {
		Variant::Operator op;
		uint64_t right;
	};
	const RejectedCase cases[] = {
		{ Variant::OP_DIVIDE, 0 },
		{ Variant::OP_MODULE, 0 },
		{ Variant::OP_SHIFT_LEFT, 64 },
		{ Variant::OP_SHIFT_RIGHT, 64 },
		{ Variant::OP_SHIFT_LEFT, UINT64_MAX },
		{ Variant::OP_SHIFT_RIGHT, UINT64_MAX },
	};

	ERR_PRINT_OFF;
	for (const RejectedCase &rejected : cases) {
		const Variant left = make_uint(UINT64_MAX);
		const Variant right = make_uint(rejected.right);

		const Variant::ValidatedOperatorEvaluator validated = Variant::get_validated_operator_evaluator(rejected.op, Variant::UINT, Variant::UINT);
		REQUIRE(validated != nullptr);
		if (validated != nullptr) {
			Variant validated_result;
			VariantInternal::initialize(&validated_result, Variant::UINT);
			VariantInternal::get_uint(&validated_result)[0] = 7;
			validated(&left, &right, &validated_result);
			CHECK_EQ(validated_result.get_type(), Variant::UINT);
			CHECK_EQ(validated_result.operator uint64_t(), 0u);
		}

		const Variant::PTROperatorEvaluator pointer = Variant::get_ptr_operator_evaluator(rejected.op, Variant::UINT, Variant::UINT);
		REQUIRE(pointer != nullptr);
		if (pointer != nullptr) {
			const uint64_t left_value = UINT64_MAX;
			const uint64_t right_value = rejected.right;
			uint64_t pointer_result = 7;
			pointer(&left_value, &right_value, &pointer_result);
			CHECK_EQ(pointer_result, 0u);
		}
	}
	ERR_PRINT_ON;
}

TEST_CASE("[Variant][UInt] Bitwise operators cover the sign bit") {
	CHECK_EQ(evaluate_uint_binary(Variant::OP_BIT_OR, uint64_t(1) << 63, uint64_t(INT64_MAX)), UINT64_MAX);
	CHECK_EQ(evaluate_uint_binary(Variant::OP_BIT_AND, UINT64_MAX, uint64_t(1) << 63), uint64_t(1) << 63);
	CHECK_EQ(evaluate_uint_binary(Variant::OP_BIT_XOR, UINT64_MAX, uint64_t(1) << 63), uint64_t(INT64_MAX));

	const uint64_t negated_inputs[] = { 0, UINT64_MAX, uint64_t(1) << 63, uint64_t(INT64_MAX) };
	for (uint64_t value : negated_inputs) {
		bool valid = false;
		Variant result;
		Variant::evaluate(Variant::OP_BIT_NEGATE, make_uint(value), Variant(), result, valid);
		REQUIRE(valid);
		CHECK_EQ(result.get_type(), Variant::UINT);
		CHECK_EQ(result.operator uint64_t(), ~value);
	}

	// Unary `+` is an identity that must preserve the carrier.
	bool valid = false;
	Variant result;
	Variant::evaluate(Variant::OP_POSITIVE, make_uint(UINT64_MAX), Variant(), result, valid);
	REQUIRE(valid);
	CHECK_EQ(result.get_type(), Variant::UINT);
	CHECK_EQ(result.operator uint64_t(), UINT64_MAX);
}

TEST_CASE("[Variant][UInt] Same-carrier ordering spans the upper half of the range") {
	const Variant::Operator comparisons[] = {
		Variant::OP_EQUAL,
		Variant::OP_NOT_EQUAL,
		Variant::OP_LESS,
		Variant::OP_LESS_EQUAL,
		Variant::OP_GREATER,
		Variant::OP_GREATER_EQUAL,
	};
	const bool expected[][6] = {
		{ false, true, true, true, false, false },
		{ true, false, false, true, false, true },
	};
	const uint64_t left_values[] = { uint64_t(INT64_MAX), UINT64_MAX };
	const uint64_t right_values[] = { UINT64_MAX, UINT64_MAX };

	for (int pair = 0; pair < 2; pair++) {
		for (int index = 0; index < 6; index++) {
			bool valid = false;
			Variant result;
			Variant::evaluate(comparisons[index], make_uint(left_values[pair]), make_uint(right_values[pair]), result, valid);
			REQUIRE(valid);
			REQUIRE_EQ(result.get_type(), Variant::BOOL);
			CHECK_EQ(result.operator bool(), expected[pair][index]);
		}
	}
}

TEST_CASE("[Variant][UInt] Truthiness participates in the logical operators") {
	Object *object = memnew(Object);
	const Variant truthy = make_uint(uint64_t(1) << 63);
	const Variant falsy = make_uint(0);
	const Variant others[] = { Variant(), Variant(true), Variant(false), Variant(int64_t(0)), Variant(int64_t(1)), Variant(0.0), Variant(1.0), Variant(object) };

	bool valid = false;
	Variant result;
	Variant::evaluate(Variant::OP_NOT, truthy, Variant(), result, valid);
	REQUIRE(valid);
	CHECK_EQ(result.operator bool(), false);
	Variant::evaluate(Variant::OP_NOT, falsy, Variant(), result, valid);
	REQUIRE(valid);
	CHECK_EQ(result.operator bool(), true);

	for (const Variant &other : others) {
		const bool other_truth = other.booleanize();
		for (const Variant &unsigned_value : { truthy, falsy }) {
			const bool unsigned_truth = unsigned_value.booleanize();

			valid = false;
			Variant::evaluate(Variant::OP_AND, unsigned_value, other, result, valid);
			REQUIRE(valid);
			CHECK_EQ(result.operator bool(), unsigned_truth && other_truth);

			valid = false;
			Variant::evaluate(Variant::OP_AND, other, unsigned_value, result, valid);
			REQUIRE(valid);
			CHECK_EQ(result.operator bool(), other_truth && unsigned_truth);

			valid = false;
			Variant::evaluate(Variant::OP_OR, unsigned_value, other, result, valid);
			REQUIRE(valid);
			CHECK_EQ(result.operator bool(), unsigned_truth || other_truth);

			valid = false;
			Variant::evaluate(Variant::OP_OR, other, unsigned_value, result, valid);
			REQUIRE(valid);
			CHECK_EQ(result.operator bool(), other_truth || unsigned_truth);

			valid = false;
			Variant::evaluate(Variant::OP_XOR, unsigned_value, other, result, valid);
			REQUIRE(valid);
			CHECK_EQ(result.operator bool(), unsigned_truth != other_truth);

			valid = false;
			Variant::evaluate(Variant::OP_XOR, other, unsigned_value, result, valid);
			REQUIRE(valid);
			CHECK_EQ(result.operator bool(), other_truth != unsigned_truth);
		}
	}

	// Both unsigned operands must resolve too.
	valid = false;
	Variant::evaluate(Variant::OP_XOR, truthy, falsy, result, valid);
	REQUIRE(valid);
	CHECK_EQ(result.operator bool(), true);

	memdelete(object);
}

TEST_CASE("[Variant][UInt] String modulo formats the unsigned magnitude") {
	bool valid = false;
	Variant result;
	Variant::evaluate(Variant::OP_MODULE, Variant("value: %s"), make_uint(UINT64_MAX), result, valid);
	REQUIRE(valid);
	CHECK_EQ(result.operator String(), String("value: 18446744073709551615"));

	valid = false;
	Variant::evaluate(Variant::OP_MODULE, Variant(StringName("value: %s")), make_uint(uint64_t(INT64_MAX) + 1), result, valid);
	REQUIRE(valid);
	CHECK_EQ(result.operator String(), String("value: 9223372036854775808"));
}

// Evaluates `p_value in p_container` and requires the containment operator to be registered.
static bool evaluate_uint_containment(uint64_t p_value, const Variant &p_container) {
	bool valid = false;
	Variant result;
	Variant::evaluate(Variant::OP_IN, make_uint(p_value), p_container, result, valid);
	REQUIRE(valid);
	REQUIRE_EQ(result.get_type(), Variant::BOOL);
	return result.operator bool();
}

TEST_CASE("[Variant][UInt] Containment finds unsigned values in every supported container") {
	Dictionary dictionary;
	dictionary[make_uint(UINT64_MAX)] = "top";
	dictionary[int64_t(7)] = "seven";
	CHECK(evaluate_uint_containment(UINT64_MAX, dictionary));
	CHECK(evaluate_uint_containment(7, dictionary));
	CHECK_FALSE(evaluate_uint_containment(0, dictionary));

	Array array;
	array.push_back(make_uint(UINT64_MAX));
	array.push_back(int64_t(7));
	CHECK(evaluate_uint_containment(UINT64_MAX, array));
	CHECK(evaluate_uint_containment(7, array));
	CHECK_FALSE(evaluate_uint_containment(8, array));

	PackedByteArray bytes;
	bytes.push_back(2);
	CHECK(evaluate_uint_containment(2, bytes));
	CHECK_FALSE(evaluate_uint_containment(300, bytes));
	CHECK_FALSE(evaluate_uint_containment(UINT64_MAX, bytes));

	PackedInt32Array int32_values;
	int32_values.push_back(-1);
	int32_values.push_back(5);
	CHECK(evaluate_uint_containment(5, int32_values));
	// A value outside the element range must never alias onto a negative element.
	CHECK_FALSE(evaluate_uint_containment(UINT64_MAX, int32_values));
	CHECK_FALSE(evaluate_uint_containment(uint64_t(UINT32_MAX), int32_values));

	PackedInt64Array int64_values;
	int64_values.push_back(-1);
	int64_values.push_back(9);
	CHECK(evaluate_uint_containment(9, int64_values));
	CHECK_FALSE(evaluate_uint_containment(UINT64_MAX, int64_values));

	PackedFloat32Array float32_values;
	float32_values.push_back(1.0f);
	CHECK(evaluate_uint_containment(1, float32_values));
	CHECK_FALSE(evaluate_uint_containment(2, float32_values));

	PackedFloat64Array float64_values;
	float64_values.push_back(2.0);
	CHECK(evaluate_uint_containment(2, float64_values));
	CHECK_FALSE(evaluate_uint_containment(3, float64_values));
}

TEST_CASE("[Variant][UInt] The carrier name round-trips") {
	CHECK_EQ(Variant::get_type_name(Variant::UINT), "uint");
	CHECK_EQ(Variant::get_type_by_name("uint"), Variant::UINT);

	// Every type-indexed name table entry stays populated and reversible.
	for (int i = 0; i < Variant::VARIANT_MAX; i++) {
		const Variant::Type type = Variant::Type(i);
		const String name = Variant::get_type_name(type);
		CHECK_FALSE(name.is_empty());
		CHECK_EQ(Variant::get_type_by_name(name), type);
	}
}

static Error parse_variant_text(const String &p_text, Variant &r_value, String &r_error) {
	VariantParser::StreamString stream;
	stream.s = p_text;
	int line = 0;
	return VariantParser::parse(&stream, r_value, r_error, line);
}

static String write_variant_text(const Variant &p_value, bool p_compat = true) {
	String text;
	VariantWriter::write_to_string(p_value, text, nullptr, nullptr, p_compat);
	return text;
}

TEST_CASE("[Variant][UInt] Text parsing accepts the canonical integer suffixes") {
	struct AcceptedForm {
		const char *text;
		Variant::Type type;
		uint64_t unsigned_value;
		int64_t signed_value;
	};

	const AcceptedForm forms[] = {
		{ "0U", Variant::UINT, 0, 0 },
		{ "4294967295U", Variant::UINT, uint64_t(UINT32_MAX), 0 },
		{ "0L", Variant::INT, 0, 0 },
		{ "-9223372036854775808L", Variant::INT, 0, INT64_MIN },
		{ "9223372036854775807L", Variant::INT, 0, INT64_MAX },
		{ "0UL", Variant::UINT, 0, 0 },
		{ "9223372036854775808UL", Variant::UINT, uint64_t(INT64_MAX) + 1, 0 },
		{ "18446744073709551615UL", Variant::UINT, UINT64_MAX, 0 },
	};

	for (const AcceptedForm &form : forms) {
		Variant parsed;
		String error;
		REQUIRE_MESSAGE(parse_variant_text(form.text, parsed, error) == OK, form.text);
		CHECK_EQ(parsed.get_type(), form.type);
		if (form.type == Variant::UINT) {
			CHECK_EQ(parsed.operator uint64_t(), form.unsigned_value);
		} else {
			CHECK_EQ(parsed.operator int64_t(), form.signed_value);
		}
	}

	// Redundant leading zeroes still parse; only writer output is canonical.
	Variant parsed;
	String error;
	REQUIRE(parse_variant_text("007UL", parsed, error) == OK);
	CHECK_EQ(parsed.get_type(), Variant::UINT);
	CHECK_EQ(parsed.operator uint64_t(), uint64_t(7));
}

TEST_CASE("[Variant][UInt] Text parsing rejects malformed integer suffixes") {
	struct RejectedForm {
		const char *text;
		const char *error;
	};

	const RejectedForm forms[] = {
		{ "4294967296U", "Integer literal \"4294967296\" is out of range for suffix \"U\"." },
		{ "9223372036854775808L", "Integer literal \"9223372036854775808\" is out of range for suffix \"L\"." },
		{ "-9223372036854775809L", "Integer literal \"-9223372036854775809\" is out of range for suffix \"L\"." },
		{ "18446744073709551616UL", "Integer literal \"18446744073709551616\" is out of range for suffix \"UL\"." },
		{ "-0U", "Unsigned integer literal \"-0\" cannot be negative." },
		{ "-1U", "Unsigned integer literal \"-1\" cannot be negative." },
		{ "-0UL", "Unsigned integer literal \"-0\" cannot be negative." },
		{ "-1UL", "Unsigned integer literal \"-1\" cannot be negative." },
		{ "1u", "Invalid integer suffix \"u\"; use \"U\"." },
		{ "1l", "Invalid integer suffix \"l\"; use \"L\"." },
		{ "1ul", "Invalid integer suffix \"ul\"; use \"UL\"." },
		{ "1Ul", "Invalid integer suffix \"Ul\"; use \"UL\"." },
		{ "1uL", "Invalid integer suffix \"uL\"; use \"UL\"." },
		{ "1LU", "Invalid integer suffix \"LU\"; use \"UL\"." },
		{ "1lu", "Invalid integer suffix \"lu\"; use \"UL\"." },
		{ "1ULL", "Invalid integer suffix \"ULL\"." },
		{ "1ULx", "Invalid integer suffix \"ULx\"." },
		{ "1UL2", "Invalid integer suffix \"UL2\"." },
		{ "1.0L", "Invalid integer suffix \"L\"." },
		{ "1e2UL", "Invalid integer suffix \"UL\"." },
	};

	for (const RejectedForm &form : forms) {
		Variant parsed = "sentinel";
		String error;
		CHECK_MESSAGE(parse_variant_text(form.text, parsed, error) == ERR_PARSE_ERROR, form.text);
		CHECK_EQ(error, String(form.error));
		// A rejected literal must never partially replace the destination.
		CHECK_EQ(parsed.get_type(), Variant::STRING);
		CHECK_EQ(parsed.operator String(), "sentinel");
	}
}

TEST_CASE("[Variant][UInt] Unsuffixed integer and float text is unchanged") {
	Variant parsed;
	String error;

	REQUIRE(parse_variant_text("42", parsed, error) == OK);
	CHECK_EQ(parsed.get_type(), Variant::INT);
	CHECK_EQ(parsed.operator int64_t(), 42);

	REQUIRE(parse_variant_text("-9223372036854775808", parsed, error) == OK);
	CHECK_EQ(parsed.get_type(), Variant::INT);
	CHECK_EQ(parsed.operator int64_t(), INT64_MIN);

	REQUIRE(parse_variant_text("1.5", parsed, error) == OK);
	CHECK_EQ(parsed.get_type(), Variant::FLOAT);
	CHECK_EQ(parsed.operator double(), doctest::Approx(1.5));

	REQUIRE(parse_variant_text("1e3", parsed, error) == OK);
	CHECK_EQ(parsed.get_type(), Variant::FLOAT);
	CHECK_EQ(parsed.operator double(), doctest::Approx(1000.0));

	CHECK_EQ(write_variant_text(Variant(int64_t(INT64_MIN))), "-9223372036854775808");
	CHECK_EQ(write_variant_text(Variant(int64_t(42))), "42");
}

TEST_CASE("[Variant][UInt] The writer emits canonical unsigned persistence text") {
	CHECK_EQ(write_variant_text(make_uint(0)), "0UL");
	CHECK_EQ(write_variant_text(make_uint(uint64_t(INT64_MAX))), "9223372036854775807UL");
	CHECK_EQ(write_variant_text(make_uint(uint64_t(INT64_MAX) + 1)), "9223372036854775808UL");
	CHECK_EQ(write_variant_text(make_uint(UINT64_MAX)), "18446744073709551615UL");

	// There is no lossy legacy spelling for the unsigned carrier.
	CHECK_EQ(write_variant_text(make_uint(UINT64_MAX), false), "18446744073709551615UL");
	CHECK_EQ(write_variant_text(make_uint(UINT64_MAX), true), "18446744073709551615UL");
}

TEST_CASE("[Variant][UInt] Writer and parser preserve unsigned magnitude") {
	const uint64_t values[] = { 0, 1, uint64_t(INT64_MAX), uint64_t(INT64_MAX) + 1, UINT64_MAX };
	for (uint64_t value : values) {
		const String text = write_variant_text(make_uint(value));
		Variant decoded;
		String error;
		REQUIRE(parse_variant_text(text, decoded, error) == OK);
		CHECK_EQ(decoded.get_type(), Variant::UINT);
		CHECK_EQ(decoded.operator uint64_t(), value);
	}
}

TEST_CASE("[Variant][UInt] Nested containers round-trip the unsigned carrier") {
	Array array;
	array.push_back(make_uint(UINT64_MAX));
	array.push_back(Variant(int64_t(-1)));

	Dictionary dictionary;
	dictionary["unsigned"] = make_uint(uint64_t(INT64_MAX) + 1);
	dictionary["signed"] = Variant(int64_t(INT64_MAX));
	dictionary["nested"] = array;

	Variant decoded;
	String error;
	REQUIRE(parse_variant_text(write_variant_text(dictionary), decoded, error) == OK);
	REQUIRE_EQ(decoded.get_type(), Variant::DICTIONARY);

	const Dictionary loaded = decoded;
	const Variant loaded_unsigned = loaded["unsigned"];
	CHECK_EQ(loaded_unsigned.get_type(), Variant::UINT);
	CHECK_EQ(loaded_unsigned.operator uint64_t(), uint64_t(INT64_MAX) + 1);

	const Variant loaded_signed = loaded["signed"];
	CHECK_EQ(loaded_signed.get_type(), Variant::INT);
	CHECK_EQ(loaded_signed.operator int64_t(), INT64_MAX);

	const Array loaded_array = loaded["nested"];
	REQUIRE_EQ(loaded_array.size(), 2);
	const Variant loaded_element = loaded_array[0];
	CHECK_EQ(loaded_element.get_type(), Variant::UINT);
	CHECK_EQ(loaded_element.operator uint64_t(), UINT64_MAX);
	const Variant loaded_signed_element = loaded_array[1];
	CHECK_EQ(loaded_signed_element.get_type(), Variant::INT);
	CHECK_EQ(loaded_signed_element.operator int64_t(), -1);
}

// Evaluates `p_op` on the signed carrier through the plain, validated, and pointer operator tables,
// checks the three agree, and returns the plain-path result.
static int64_t evaluate_int_binary(Variant::Operator p_op, int64_t p_left, int64_t p_right) {
	const Variant left = p_left;
	const Variant right = p_right;

	bool valid = false;
	Variant result;
	Variant::evaluate(p_op, left, right, result, valid);
	REQUIRE(valid);
	REQUIRE_EQ(result.get_type(), Variant::INT);
	REQUIRE_EQ(Variant::get_operator_return_type(p_op, Variant::INT, Variant::INT), Variant::INT);

	const Variant::ValidatedOperatorEvaluator validated = Variant::get_validated_operator_evaluator(p_op, Variant::INT, Variant::INT);
	REQUIRE(validated != nullptr);
	if (validated != nullptr) {
		Variant validated_result;
		VariantInternal::initialize(&validated_result, Variant::INT);
		validated(&left, &right, &validated_result);
		CHECK_EQ(validated_result.get_type(), Variant::INT);
		CHECK_EQ(validated_result.operator int64_t(), result.operator int64_t());
	}

	const Variant::PTROperatorEvaluator pointer = Variant::get_ptr_operator_evaluator(p_op, Variant::INT, Variant::INT);
	REQUIRE(pointer != nullptr);
	if (pointer != nullptr) {
		int64_t pointer_result = 0;
		pointer(&p_left, &p_right, &pointer_result);
		CHECK_EQ(pointer_result, result.operator int64_t());
	}

	return result.operator int64_t();
}

TEST_CASE("[Variant][Int] Division, remainder, and shifts agree across every evaluator path") {
	CHECK_EQ(evaluate_int_binary(Variant::OP_DIVIDE, -7, 3), -2);
	CHECK_EQ(evaluate_int_binary(Variant::OP_MODULE, -7, 3), -1);
	CHECK_EQ(evaluate_int_binary(Variant::OP_DIVIDE, 7, -3), -2);
	CHECK_EQ(evaluate_int_binary(Variant::OP_MODULE, 7, -3), 1);
	CHECK_EQ(evaluate_int_binary(Variant::OP_DIVIDE, INT64_MIN, 1), INT64_MIN);
	CHECK_EQ(evaluate_int_binary(Variant::OP_MODULE, INT64_MIN, 1), 0);
	CHECK_EQ(evaluate_int_binary(Variant::OP_DIVIDE, INT64_MIN, -2), INT64_MAX / 2 + 1);

	// The maximum left shift produces the sign bit; its bit pattern must not come from overflowing a
	// raw signed shift.
	CHECK_EQ(evaluate_int_binary(Variant::OP_SHIFT_LEFT, 1, 63), INT64_MIN);
	CHECK_EQ(evaluate_int_binary(Variant::OP_SHIFT_LEFT, 3, 62), INT64_MIN + (INT64_MAX / 2 + 1));
	CHECK_EQ(evaluate_int_binary(Variant::OP_SHIFT_LEFT, 3, 2), 12);
	CHECK_EQ(evaluate_int_binary(Variant::OP_SHIFT_LEFT, 1, 0), 1);
	CHECK_EQ(evaluate_int_binary(Variant::OP_SHIFT_LEFT, 0, 63), 0);

	CHECK_EQ(evaluate_int_binary(Variant::OP_SHIFT_RIGHT, 12, 2), 3);
	CHECK_EQ(evaluate_int_binary(Variant::OP_SHIFT_RIGHT, INT64_MAX, 63), 0);
	CHECK_EQ(evaluate_int_binary(Variant::OP_SHIFT_RIGHT, INT64_MAX, 62), 1);
	CHECK_EQ(evaluate_int_binary(Variant::OP_SHIFT_RIGHT, 12, 0), 12);
}

TEST_CASE("[Variant][Int] Operands the operation cannot evaluate are rejected on every path") {
	// None of these operand pairs has a defined signed C++ result, so no entry point may run the raw
	// operation. The reporting path answers through its validity flag; the two `void` entry points
	// cannot, so they fall back to zero and raise an engine error.
	struct RejectedCase {
		Variant::Operator op;
		int64_t left;
		int64_t right;
	};
	const RejectedCase cases[] = {
		{ Variant::OP_DIVIDE, 7, 0 },
		{ Variant::OP_MODULE, 7, 0 },
		{ Variant::OP_DIVIDE, INT64_MIN, -1 },
		{ Variant::OP_MODULE, INT64_MIN, -1 },
		{ Variant::OP_SHIFT_LEFT, 1, -1 },
		{ Variant::OP_SHIFT_RIGHT, 1, -1 },
		{ Variant::OP_SHIFT_LEFT, 1, 64 },
		{ Variant::OP_SHIFT_RIGHT, 1, 64 },
		{ Variant::OP_SHIFT_LEFT, 1, 65 },
		{ Variant::OP_SHIFT_RIGHT, 1, 65 },
		{ Variant::OP_SHIFT_LEFT, -1, 1 },
		{ Variant::OP_SHIFT_RIGHT, -1, 1 },
	};

	for (const RejectedCase &rejected : cases) {
		const Variant left = rejected.left;
		const Variant right = rejected.right;

		bool valid = true;
		Variant result;
		Variant::evaluate(rejected.op, left, right, result, valid);
		CHECK_FALSE(valid);
		REQUIRE_EQ(result.get_type(), Variant::STRING);
		CHECK_FALSE(result.operator String().is_empty());

		ErrorDetector detector;
		ERR_PRINT_OFF;

		const Variant::ValidatedOperatorEvaluator validated = Variant::get_validated_operator_evaluator(rejected.op, Variant::INT, Variant::INT);
		REQUIRE(validated != nullptr);
		if (validated != nullptr) {
			Variant validated_result;
			VariantInternal::initialize(&validated_result, Variant::INT);
			*VariantInternal::get_int(&validated_result) = 7;
			detector.clear();
			validated(&left, &right, &validated_result);
			CHECK_EQ(validated_result.get_type(), Variant::INT);
			CHECK_EQ(validated_result.operator int64_t(), int64_t(0));
			CHECK(detector.has_error);
		}

		const Variant::PTROperatorEvaluator pointer = Variant::get_ptr_operator_evaluator(rejected.op, Variant::INT, Variant::INT);
		REQUIRE(pointer != nullptr);
		if (pointer != nullptr) {
			const int64_t left_value = rejected.left;
			const int64_t right_value = rejected.right;
			int64_t pointer_result = 7;
			detector.clear();
			pointer(&left_value, &right_value, &pointer_result);
			CHECK_EQ(pointer_result, int64_t(0));
			CHECK(detector.has_error);
		}

		ERR_PRINT_ON;
	}
}

TEST_CASE("[Variant][Int] Each rejected operand pair reports its own diagnostic") {
	auto diagnostic = [](Variant::Operator p_op, int64_t p_left, int64_t p_right) {
		bool valid = true;
		Variant result;
		Variant::evaluate(p_op, Variant(p_left), Variant(p_right), result, valid);
		CHECK_FALSE(valid);
		return result.operator String();
	};

	CHECK_EQ(diagnostic(Variant::OP_DIVIDE, 7, 0), "Division by zero error");
	CHECK_EQ(diagnostic(Variant::OP_MODULE, 7, 0), "Modulo by zero error");

	// The quotient of the minimum integer and -1 is not representable; reporting it as a zero divisor
	// would describe the wrong operand.
	CHECK_NE(diagnostic(Variant::OP_DIVIDE, INT64_MIN, -1), "Division by zero error");
	CHECK_NE(diagnostic(Variant::OP_MODULE, INT64_MIN, -1), "Modulo by zero error");
	CHECK(diagnostic(Variant::OP_DIVIDE, INT64_MIN, -1).contains("overflow"));
	CHECK(diagnostic(Variant::OP_MODULE, INT64_MIN, -1).contains("overflow"));

	CHECK(diagnostic(Variant::OP_SHIFT_LEFT, 1, 64).contains("0...63"));
	CHECK(diagnostic(Variant::OP_SHIFT_RIGHT, 1, -1).contains("0...63"));
	CHECK(diagnostic(Variant::OP_SHIFT_LEFT, -1, 1).contains("positive"));
	CHECK(diagnostic(Variant::OP_SHIFT_RIGHT, -1, 1).contains("positive"));
}

// Builds an integer vector whose every component holds `p_value`.
template <typename VectorType>
static VectorType vector_int_filled(int32_t p_value) {
	VectorType filled;
	for (int axis = 0; axis < VectorType::AXIS_COUNT; axis++) {
		filled[axis] = p_value;
	}
	return filled;
}

// Evaluates `p_op` on an integer vector and a divisor through the plain, validated, and pointer
// operator tables, checks the three agree, and returns the plain-path result.
template <typename VectorType, typename DivisorType>
static VectorType evaluate_vector_int_binary(Variant::Operator p_op, const VectorType &p_left, const DivisorType &p_right) {
	const Variant left = p_left;
	const Variant right = p_right;
	const Variant::Type left_type = left.get_type();
	const Variant::Type right_type = right.get_type();

	bool valid = false;
	Variant result;
	Variant::evaluate(p_op, left, right, result, valid);
	REQUIRE(valid);
	REQUIRE_EQ(result.get_type(), left_type);
	REQUIRE_EQ(Variant::get_operator_return_type(p_op, left_type, right_type), left_type);
	const VectorType expected = result;

	const Variant::ValidatedOperatorEvaluator validated = Variant::get_validated_operator_evaluator(p_op, left_type, right_type);
	REQUIRE(validated != nullptr);
	if (validated != nullptr) {
		Variant validated_result = vector_int_filled<VectorType>(7);
		validated(&left, &right, &validated_result);
		CHECK_EQ(validated_result.get_type(), left_type);
		const VectorType validated_value = validated_result;
		CHECK_EQ(validated_value, expected);
	}

	const Variant::PTROperatorEvaluator pointer = Variant::get_ptr_operator_evaluator(p_op, left_type, right_type);
	REQUIRE(pointer != nullptr);
	if (pointer != nullptr) {
		VectorType pointer_result = vector_int_filled<VectorType>(7);
		pointer(&p_left, &p_right, &pointer_result);
		CHECK_EQ(pointer_result, expected);
	}

	return expected;
}

// Checks that no entry point runs an operation the operands cannot support: the reporting path
// answers through its validity flag, and the two `void` paths write a zero vector over their
// destination and raise an engine error.
template <typename VectorType, typename DivisorType>
static void check_vector_int_rejected(Variant::Operator p_op, const VectorType &p_left, const DivisorType &p_right) {
	const Variant left = p_left;
	const Variant right = p_right;
	const Variant::Type left_type = left.get_type();
	const Variant::Type right_type = right.get_type();

	bool valid = true;
	Variant result;
	Variant::evaluate(p_op, left, right, result, valid);
	CHECK_FALSE(valid);
	REQUIRE_EQ(result.get_type(), Variant::STRING);
	CHECK_FALSE(result.operator String().is_empty());

	ErrorDetector detector;
	ERR_PRINT_OFF;

	const Variant::ValidatedOperatorEvaluator validated = Variant::get_validated_operator_evaluator(p_op, left_type, right_type);
	REQUIRE(validated != nullptr);
	if (validated != nullptr) {
		Variant validated_result = vector_int_filled<VectorType>(7);
		detector.clear();
		validated(&left, &right, &validated_result);
		CHECK_EQ(validated_result.get_type(), left_type);
		const VectorType validated_value = validated_result;
		CHECK_EQ(validated_value, VectorType());
		CHECK(detector.has_error);
	}

	const Variant::PTROperatorEvaluator pointer = Variant::get_ptr_operator_evaluator(p_op, left_type, right_type);
	REQUIRE(pointer != nullptr);
	if (pointer != nullptr) {
		VectorType pointer_result = vector_int_filled<VectorType>(7);
		detector.clear();
		pointer(&p_left, &p_right, &pointer_result);
		CHECK_EQ(pointer_result, VectorType());
		CHECK(detector.has_error);
	}

	ERR_PRINT_ON;
}

// Runs the rejected-operand contract for one integer vector type: a zero divisor component in every
// position, the minimum dividend against -1 in every position, a zero scalar divisor, and nonzero
// scalar divisors that do not fit in the 32-bit componentwise operation.
template <typename VectorType>
static void check_vector_int_rejected_operands(Variant::Operator p_op) {
	for (int axis = 0; axis < VectorType::AXIS_COUNT; axis++) {
		VectorType zero_divisor = vector_int_filled<VectorType>(3);
		zero_divisor[axis] = 0;
		check_vector_int_rejected(p_op, vector_int_filled<VectorType>(7), zero_divisor);

		VectorType overflow_dividend = vector_int_filled<VectorType>(7);
		overflow_dividend[axis] = INT32_MIN;
		VectorType overflow_divisor = vector_int_filled<VectorType>(3);
		overflow_divisor[axis] = -1;
		check_vector_int_rejected(p_op, overflow_dividend, overflow_divisor);
	}

	check_vector_int_rejected(p_op, vector_int_filled<VectorType>(7), int64_t(0));
	check_vector_int_rejected(p_op, vector_int_filled<VectorType>(INT32_MIN), int64_t(-1));

	// A nonzero 64-bit divisor that narrows to zero would silently become a division by zero, and one
	// that narrows to a different nonzero value would silently answer a different question.
	check_vector_int_rejected(p_op, vector_int_filled<VectorType>(7), int64_t(1) << 32);
	check_vector_int_rejected(p_op, vector_int_filled<VectorType>(7), -(int64_t(1) << 32));
	check_vector_int_rejected(p_op, vector_int_filled<VectorType>(7), int64_t(INT32_MAX) + 1);
}

TEST_CASE("[Variant][VectorInt] Componentwise division and remainder agree across every evaluator path") {
	CHECK_EQ(evaluate_vector_int_binary(Variant::OP_DIVIDE, Vector2i(-7, 7), Vector2i(3, -3)), Vector2i(-2, -2));
	CHECK_EQ(evaluate_vector_int_binary(Variant::OP_MODULE, Vector2i(-7, 7), Vector2i(3, -3)), Vector2i(-1, 1));
	CHECK_EQ(evaluate_vector_int_binary(Variant::OP_DIVIDE, Vector2i(INT32_MIN, INT32_MAX), Vector2i(-2, 2)), Vector2i(INT32_MAX / 2 + 1, INT32_MAX / 2));
	CHECK_EQ(evaluate_vector_int_binary(Variant::OP_DIVIDE, Vector2i(-7, 7), int64_t(2)), Vector2i(-3, 3));
	CHECK_EQ(evaluate_vector_int_binary(Variant::OP_MODULE, Vector2i(-7, 7), int64_t(2)), Vector2i(-1, 1));
	CHECK_EQ(evaluate_vector_int_binary(Variant::OP_DIVIDE, Vector2i(INT32_MIN, 6), int64_t(-2)), Vector2i(INT32_MAX / 2 + 1, -3));

	CHECK_EQ(evaluate_vector_int_binary(Variant::OP_DIVIDE, Vector3i(-7, 7, 9), Vector3i(3, -3, 2)), Vector3i(-2, -2, 4));
	CHECK_EQ(evaluate_vector_int_binary(Variant::OP_MODULE, Vector3i(-7, 7, 9), Vector3i(3, -3, 2)), Vector3i(-1, 1, 1));
	CHECK_EQ(evaluate_vector_int_binary(Variant::OP_DIVIDE, Vector3i(-7, 7, 9), int64_t(2)), Vector3i(-3, 3, 4));
	CHECK_EQ(evaluate_vector_int_binary(Variant::OP_MODULE, Vector3i(-7, 7, 9), int64_t(2)), Vector3i(-1, 1, 1));

	CHECK_EQ(evaluate_vector_int_binary(Variant::OP_DIVIDE, Vector4i(-7, 7, 9, INT32_MIN), Vector4i(3, -3, 2, -2)), Vector4i(-2, -2, 4, INT32_MAX / 2 + 1));
	CHECK_EQ(evaluate_vector_int_binary(Variant::OP_MODULE, Vector4i(-7, 7, 9, INT32_MIN), Vector4i(3, -3, 2, -2)), Vector4i(-1, 1, 1, 0));
	CHECK_EQ(evaluate_vector_int_binary(Variant::OP_DIVIDE, Vector4i(-7, 7, 9, 11), int64_t(2)), Vector4i(-3, 3, 4, 5));
	CHECK_EQ(evaluate_vector_int_binary(Variant::OP_MODULE, Vector4i(-7, 7, 9, 11), int64_t(2)), Vector4i(-1, 1, 1, 1));
}

TEST_CASE("[Variant][VectorInt] Component pairs the operation cannot evaluate are rejected on every path") {
	check_vector_int_rejected_operands<Vector2i>(Variant::OP_DIVIDE);
	check_vector_int_rejected_operands<Vector2i>(Variant::OP_MODULE);
	check_vector_int_rejected_operands<Vector3i>(Variant::OP_DIVIDE);
	check_vector_int_rejected_operands<Vector3i>(Variant::OP_MODULE);
	check_vector_int_rejected_operands<Vector4i>(Variant::OP_DIVIDE);
	check_vector_int_rejected_operands<Vector4i>(Variant::OP_MODULE);
}

TEST_CASE("[Variant][VectorInt] Each rejected component pair reports its own diagnostic") {
	auto diagnostic = [](Variant::Operator p_op, const Variant &p_left, const Variant &p_right) {
		bool valid = true;
		Variant result;
		Variant::evaluate(p_op, p_left, p_right, result, valid);
		CHECK_FALSE(valid);
		return result.operator String();
	};

	CHECK_EQ(diagnostic(Variant::OP_DIVIDE, Vector2i(7, 7), Vector2i(0, 3)), "Division by zero error");
	CHECK_EQ(diagnostic(Variant::OP_MODULE, Vector3i(7, 7, 7), Vector3i(3, 0, 3)), "Modulo by zero error");
	CHECK_EQ(diagnostic(Variant::OP_DIVIDE, Vector4i(7, 7, 7, 7), int64_t(0)), "Division by zero error");
	CHECK_EQ(diagnostic(Variant::OP_MODULE, Vector2i(7, 7), int64_t(0)), "Modulo by zero error");

	// The quotient of the minimum component and -1 is not representable; reporting it as a zero
	// divisor would describe the wrong operand.
	CHECK(diagnostic(Variant::OP_DIVIDE, Vector4i(7, 7, 7, INT32_MIN), Vector4i(3, 3, 3, -1)).contains("overflow"));
	CHECK(diagnostic(Variant::OP_MODULE, Vector2i(INT32_MIN, 7), Vector2i(-1, 3)).contains("overflow"));
	CHECK(diagnostic(Variant::OP_DIVIDE, Vector3i(INT32_MIN, 7, 7), int64_t(-1)).contains("overflow"));

	// A nonzero scalar divisor that does not fit the componentwise operation is its own failure, not a
	// zero divisor.
	CHECK(diagnostic(Variant::OP_DIVIDE, Vector2i(7, 7), int64_t(1) << 32).contains("32-bit"));
	CHECK(diagnostic(Variant::OP_MODULE, Vector3i(7, 7, 7), int64_t(1) << 32).contains("32-bit"));
	CHECK(diagnostic(Variant::OP_DIVIDE, Vector4i(7, 7, 7, 7), int64_t(INT32_MAX) + 1).contains("32-bit"));
}

} // namespace TestVariant
