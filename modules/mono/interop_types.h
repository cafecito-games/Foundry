/**************************************************************************/
/*  interop_types.h                                                       */
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

#include "core/math/math_defs.h"

#ifdef __cplusplus
extern "C" {
#endif

// This is taken from the old GDNative, which was removed.

#define FOUNDRY_VARIANT_SIZE (sizeof(real_t) * 4 + sizeof(int64_t))

typedef struct {
	uint8_t _dont_touch_that[FOUNDRY_VARIANT_SIZE];
} godot_variant;

#define FOUNDRY_ARRAY_SIZE sizeof(void *)

typedef struct {
	uint8_t _dont_touch_that[FOUNDRY_ARRAY_SIZE];
} godot_array;

#define FOUNDRY_DICTIONARY_SIZE sizeof(void *)

typedef struct {
	uint8_t _dont_touch_that[FOUNDRY_DICTIONARY_SIZE];
} godot_dictionary;

#define FOUNDRY_STRING_SIZE sizeof(void *)

typedef struct {
	uint8_t _dont_touch_that[FOUNDRY_STRING_SIZE];
} godot_string;

#define FOUNDRY_STRING_NAME_SIZE sizeof(void *)

typedef struct {
	uint8_t _dont_touch_that[FOUNDRY_STRING_NAME_SIZE];
} godot_string_name;

#define FOUNDRY_PACKED_ARRAY_SIZE (2 * sizeof(void *))

typedef struct {
	uint8_t _dont_touch_that[FOUNDRY_PACKED_ARRAY_SIZE];
} godot_packed_array;

#define FOUNDRY_VECTOR2_SIZE (sizeof(real_t) * 2)

typedef struct {
	uint8_t _dont_touch_that[FOUNDRY_VECTOR2_SIZE];
} godot_vector2;

#define FOUNDRY_VECTOR2I_SIZE (sizeof(int32_t) * 2)

typedef struct {
	uint8_t _dont_touch_that[FOUNDRY_VECTOR2I_SIZE];
} godot_vector2i;

#define FOUNDRY_RECT2_SIZE (sizeof(real_t) * 4)

typedef struct godot_rect2 {
	uint8_t _dont_touch_that[FOUNDRY_RECT2_SIZE];
} godot_rect2;

#define FOUNDRY_RECT2I_SIZE (sizeof(int32_t) * 4)

typedef struct godot_rect2i {
	uint8_t _dont_touch_that[FOUNDRY_RECT2I_SIZE];
} godot_rect2i;

#define FOUNDRY_VECTOR3_SIZE (sizeof(real_t) * 3)

typedef struct {
	uint8_t _dont_touch_that[FOUNDRY_VECTOR3_SIZE];
} godot_vector3;

#define FOUNDRY_VECTOR3I_SIZE (sizeof(int32_t) * 3)

typedef struct {
	uint8_t _dont_touch_that[FOUNDRY_VECTOR3I_SIZE];
} godot_vector3i;

#define FOUNDRY_TRANSFORM2D_SIZE (sizeof(real_t) * 6)

typedef struct {
	uint8_t _dont_touch_that[FOUNDRY_TRANSFORM2D_SIZE];
} godot_transform2d;

#define FOUNDRY_VECTOR4_SIZE (sizeof(real_t) * 4)

typedef struct {
	uint8_t _dont_touch_that[FOUNDRY_VECTOR4_SIZE];
} godot_vector4;

#define FOUNDRY_VECTOR4I_SIZE (sizeof(int32_t) * 4)

typedef struct {
	uint8_t _dont_touch_that[FOUNDRY_VECTOR4I_SIZE];
} godot_vector4i;

#define FOUNDRY_PLANE_SIZE (sizeof(real_t) * 4)

typedef struct {
	uint8_t _dont_touch_that[FOUNDRY_PLANE_SIZE];
} godot_plane;

#define FOUNDRY_QUATERNION_SIZE (sizeof(real_t) * 4)

typedef struct {
	uint8_t _dont_touch_that[FOUNDRY_QUATERNION_SIZE];
} godot_quaternion;

#define FOUNDRY_AABB_SIZE (sizeof(real_t) * 6)

typedef struct {
	uint8_t _dont_touch_that[FOUNDRY_AABB_SIZE];
} godot_aabb;

#define FOUNDRY_BASIS_SIZE (sizeof(real_t) * 9)

typedef struct {
	uint8_t _dont_touch_that[FOUNDRY_BASIS_SIZE];
} godot_basis;

#define FOUNDRY_TRANSFORM3D_SIZE (sizeof(real_t) * 12)

typedef struct {
	uint8_t _dont_touch_that[FOUNDRY_TRANSFORM3D_SIZE];
} godot_transform3d;

#define FOUNDRY_PROJECTION_SIZE (sizeof(real_t) * 4 * 4)

typedef struct {
	uint8_t _dont_touch_that[FOUNDRY_PROJECTION_SIZE];
} godot_projection;

// Colors should always use 32-bit floats, so don't use real_t here.
#define FOUNDRY_COLOR_SIZE (sizeof(float) * 4)

typedef struct {
	uint8_t _dont_touch_that[FOUNDRY_COLOR_SIZE];
} godot_color;

#define FOUNDRY_NODE_PATH_SIZE sizeof(void *)

typedef struct {
	uint8_t _dont_touch_that[FOUNDRY_NODE_PATH_SIZE];
} godot_node_path;

#define FOUNDRY_RID_SIZE sizeof(uint64_t)

typedef struct {
	uint8_t _dont_touch_that[FOUNDRY_RID_SIZE];
} godot_rid;

// Alignment hardcoded in `core/variant/callable.h`.
#define FOUNDRY_CALLABLE_SIZE (16)

typedef struct {
	uint8_t _dont_touch_that[FOUNDRY_CALLABLE_SIZE];
} godot_callable;

// Alignment hardcoded in `core/variant/callable.h`.
#define FOUNDRY_SIGNAL_SIZE (16)

typedef struct {
	uint8_t _dont_touch_that[FOUNDRY_SIGNAL_SIZE];
} godot_signal;

#ifdef __cplusplus
}
#endif
