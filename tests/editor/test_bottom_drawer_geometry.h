/**************************************************************************/
/*  test_bottom_drawer_geometry.h                                         */
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

#include "editor/gui/bottom_drawer_geometry.h"

#include "tests/test_macros.h"

namespace TestBottomDrawerGeometry {

TEST_CASE("[Editor][BottomDrawerGeometry] Drawer height") {
	// Closed: only the tab strip is visible.
	CHECK(BottomDrawerGeometry::drawer_height(false, false, 30, 200, 600) == 30);
	// Closed and expanded flag leftover: still only the strip.
	CHECK(BottomDrawerGeometry::drawer_height(false, true, 30, 200, 600) == 30);
	// Open: strip + body.
	CHECK(BottomDrawerGeometry::drawer_height(true, false, 30, 200, 600) == 230);
	// Open + expanded: full area.
	CHECK(BottomDrawerGeometry::drawer_height(true, true, 30, 200, 600) == 600);
	// Open body taller than the area is capped by the area.
	CHECK(BottomDrawerGeometry::drawer_height(true, false, 30, 900, 600) == 600);
}

TEST_CASE("[Editor][BottomDrawerGeometry] Workspace inset") {
	// Closed: workspace reserves only the strip.
	CHECK(BottomDrawerGeometry::workspace_inset(false, false, false, 30, 200, 600) == 30);
	CHECK(BottomDrawerGeometry::workspace_inset(false, true, false, 30, 200, 600) == 30);
	// Open + unpinned: overlay, workspace still reserves only the strip.
	CHECK(BottomDrawerGeometry::workspace_inset(true, false, false, 30, 200, 600) == 30);
	// Open + pinned: workspace reserves strip + body.
	CHECK(BottomDrawerGeometry::workspace_inset(true, true, false, 30, 200, 600) == 230);
	// Expanded covers the workspace in both pin modes; inset stays strip-only so
	// un-expanding restores instantly.
	CHECK(BottomDrawerGeometry::workspace_inset(true, false, true, 30, 200, 600) == 30);
	CHECK(BottomDrawerGeometry::workspace_inset(true, true, true, 30, 200, 600) == 30);
	// Pinned body taller than the area is capped so the inset never exceeds the area.
	CHECK(BottomDrawerGeometry::workspace_inset(true, true, false, 30, 900, 600) == 600);
}

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

} // namespace TestBottomDrawerGeometry
