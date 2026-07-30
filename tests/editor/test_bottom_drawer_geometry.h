/**************************************************************************/
/*  test_bottom_drawer_geometry.h                                         */
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

#include "editor/gui/bottom_drawer_geometry.h"

#include "tests/test_macros.h"

namespace TestBottomDrawerGeometry {

TEST_CASE("[Editor][BottomDrawerGeometry] Clamp body height") {
	// Within range: unchanged.
	CHECK(BottomDrawerGeometry::clamp_body_height(200, 50, 30, 600) == 200);
	// Below content minimum: raised.
	CHECK(BottomDrawerGeometry::clamp_body_height(10, 50, 30, 600) == 50);
	// Above area minus strip: lowered.
	CHECK(BottomDrawerGeometry::clamp_body_height(900, 50, 30, 600) == 570);
	// Degenerate area smaller than the minimum: minimum wins.
	CHECK(BottomDrawerGeometry::clamp_body_height(200, 50, 30, 40) == 50);
}

TEST_CASE("[Editor][BottomDrawerGeometry] Migrate stored layout values") {
	// Legacy split offsets were negative; magnitude is the panel height.
	CHECK(BottomDrawerGeometry::body_height_from_stored(-450, 0) == 450);
	// New-format positive values pass through.
	CHECK(BottomDrawerGeometry::body_height_from_stored(300, 0) == 300);
	// Zero or unset falls back.
	CHECK(BottomDrawerGeometry::body_height_from_stored(0, 120) == 120);
}

TEST_CASE("[Editor][BottomDrawerGeometry] Island width and position") {
	// Nominal: 64% of the window.
	CHECK(BottomDrawerGeometry::island_width(2000, 480, 48) == 1280);
	CHECK(BottomDrawerGeometry::island_x(2000, 1280) == 360);
	// Tiny window with a small minimum: the margin clamp binds (64% would exceed it).
	CHECK(BottomDrawerGeometry::island_width(260, 100, 48) == 164);
	// Narrow window where 64% falls below the minimum: the width floor wins.
	CHECK(BottomDrawerGeometry::island_width(700, 480, 48) == 480);
	// Narrower still: minimum width floor wins over the margin clamp.
	CHECK(BottomDrawerGeometry::island_width(500, 480, 48) == 480);
	// Degenerate: window narrower than the minimum; minimum still wins.
	CHECK(BottomDrawerGeometry::island_width(300, 480, 48) == 480);
	// Odd leftover pixels center deterministically.
	CHECK(BottomDrawerGeometry::island_x(1001, 640) == 180);
}

TEST_CASE("[Editor][BottomDrawerGeometry] Clamp island width") {
	// Within bounds: unchanged.
	CHECK(BottomDrawerGeometry::clamp_island_width(800, 2000, 480, 48) == 800);
	// Below minimum: raised.
	CHECK(BottomDrawerGeometry::clamp_island_width(300, 2000, 480, 48) == 480);
	// Above maximum: lowered.
	CHECK(BottomDrawerGeometry::clamp_island_width(1950, 2000, 480, 48) == 1904);
	// Very narrow window: minimum wins over the margin clamp.
	CHECK(BottomDrawerGeometry::clamp_island_width(900, 500, 480, 48) == 480);
}

TEST_CASE("[Editor][BottomDrawerGeometry] Island width with override") {
	// No override: 64% default.
	CHECK(BottomDrawerGeometry::island_width_with_override(2000, 480, 48, 0) == 1280);
	CHECK(BottomDrawerGeometry::island_width_with_override(2000, 480, 48, -1) == 1280);
	// Override inside bounds: used exactly.
	CHECK(BottomDrawerGeometry::island_width_with_override(2000, 480, 48, 900) == 900);
	// Override below minimum: clamped.
	CHECK(BottomDrawerGeometry::island_width_with_override(2000, 480, 48, 300) == 480);
	// Override above maximum: clamped.
	CHECK(BottomDrawerGeometry::island_width_with_override(2000, 480, 48, 1950) == 1904);
}

TEST_CASE("[Editor][BottomDrawerGeometry] Island width from edge drag") {
	const int start = 1000;
	const int window = 2000;
	const int min_w = 480;
	const int margin = 48;
	// Right edge: positive delta grows by twice the mouse delta.
	CHECK(BottomDrawerGeometry::island_width_from_edge_drag(start, 50, true, window, min_w, margin) == 1100);
	// Right edge: negative delta shrinks by twice the mouse delta.
	CHECK(BottomDrawerGeometry::island_width_from_edge_drag(start, -50, true, window, min_w, margin) == 900);
	// Left edge: negative delta grows by twice the mouse delta.
	CHECK(BottomDrawerGeometry::island_width_from_edge_drag(start, -50, false, window, min_w, margin) == 1100);
	// Left edge: positive delta shrinks by twice the mouse delta.
	CHECK(BottomDrawerGeometry::island_width_from_edge_drag(start, 50, false, window, min_w, margin) == 900);
	// Clamps at minimum.
	CHECK(BottomDrawerGeometry::island_width_from_edge_drag(500, -100, true, window, min_w, margin) == 480);
	// Clamps at maximum.
	CHECK(BottomDrawerGeometry::island_width_from_edge_drag(1800, 100, true, window, min_w, margin) == 1904);
}

TEST_CASE("[Editor][BottomDrawerGeometry] Island height") {
	// Not expanded: body height, capped by the available area.
	CHECK(BottomDrawerGeometry::island_height(false, 200, 600, 24) == 200);
	CHECK(BottomDrawerGeometry::island_height(false, 900, 600, 24) == 576);
	// Expanded: full area minus the top margin.
	CHECK(BottomDrawerGeometry::island_height(true, 200, 600, 24) == 576);
	// Degenerate area smaller than the margin never goes negative.
	CHECK(BottomDrawerGeometry::island_height(true, 200, 20, 24) == 0);
}

} // namespace TestBottomDrawerGeometry
