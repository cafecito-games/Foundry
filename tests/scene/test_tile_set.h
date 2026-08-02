/**************************************************************************/
/*  test_tile_set.h                                                       */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             FOUNDRY ENGINE                             */
/*          A fork of the Godot Engine (https://godotengine.org)          */
/*                       https://www.cafecito.games                       */
/**************************************************************************/
/* Copyright (c) 2026-present Cafecito Games LLC.                         */
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

#include "scene/resources/2d/tile_set.h"

#include "tests/test_macros.h"

namespace TestTileSet {

TEST_CASE("[TileSet] Retyping a custom data layer keeps a value of the layer's type") {
	Ref<TileSet> tile_set;
	tile_set.instantiate();
	tile_set->add_custom_data_layer(-1);
	tile_set->set_custom_data_layer_type(0, Variant::INT);

	TileData *tile_data = memnew(TileData);
	tile_data->set_tile_set(tile_set.ptr());

	SUBCASE("A value the destination type can represent is carried over") {
		tile_data->set_custom_data_by_layer_id(0, int64_t(7));
		tile_set->set_custom_data_layer_type(0, Variant::FLOAT);
		tile_data->notify_tile_data_properties_should_change();

		CHECK_EQ(tile_data->get_custom_data_by_layer_id(0).get_type(), Variant::FLOAT);
		CHECK_EQ(tile_data->get_custom_data_by_layer_id(0).operator double(), doctest::Approx(7.0));
	}

	SUBCASE("A value the destination type cannot represent falls back to that type's default") {
		// Conversion between the integer carriers exists but is checked, so a negative value has no
		// unsigned representation and must not leave the layer holding a foreign type.
		tile_data->set_custom_data_by_layer_id(0, int64_t(-5));
		tile_set->set_custom_data_layer_type(0, Variant::UINT);
		tile_data->notify_tile_data_properties_should_change();

		CHECK_EQ(tile_data->get_custom_data_by_layer_id(0).get_type(), Variant::UINT);
		CHECK_EQ(tile_data->get_custom_data_by_layer_id(0).operator uint64_t(), 0u);
	}

	SUBCASE("An unconvertible type falls back to the destination type's default") {
		tile_data->set_custom_data_by_layer_id(0, int64_t(3));
		tile_set->set_custom_data_layer_type(0, Variant::VECTOR2);
		tile_data->notify_tile_data_properties_should_change();

		CHECK_EQ(tile_data->get_custom_data_by_layer_id(0).get_type(), Variant::VECTOR2);
		CHECK_EQ(tile_data->get_custom_data_by_layer_id(0).operator Vector2(), Vector2());
	}

	memdelete(tile_data);
}

} // namespace TestTileSet
