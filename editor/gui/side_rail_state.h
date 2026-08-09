/**************************************************************************/
/*  side_rail_state.h                                                     */
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

#include "core/templates/vector.h"
#include "core/typedefs.h"

// Scene-free helpers for per-tile side-rail state. Kept free of scene/editor
// dependencies so it is unit-testable headlessly, mirroring
// editor/gui/bottom_drawer_geometry.h.
//
// This header currently holds only the tile-dock split-gap identity mapping
// (issue #1989). Later issues under the side-rails epic add the per-side state
// machine and rail-fit math here.

// Which get_split_offsets() entry each persisted tile-dock key refers to, for
// the body's *current* visible-child list. ABSENT means the gap does not
// currently exist and its stored key must be left untouched.
struct TileDockGapMap {
	static constexpr int ABSENT = -1;
	int left_center = ABSENT; // persisted as tile_dock_hsplit_1
	int center_right = ABSENT; // persisted as tile_dock_hsplit_2
	int gap_count = 0; // visible children minus one, floored at 0
};

// p_child_visible has one entry per non-internal body child, in
// get_child(i, false) order. An entry is true only when that child is a
// non-top-level visible Control — the same predicate SplitContainer uses for
// valid_children. p_center_child_index is the centre host's get_index(false)
// in that same list. Gap g sits between the g-th and (g+1)-th *true* entries.
inline TileDockGapMap tile_dock_gap_map(const Vector<bool> &p_child_visible, int p_center_child_index) {
	TileDockGapMap map;

	int visible_count = 0;
	for (int i = 0; i < p_child_visible.size(); i++) {
		if (p_child_visible[i]) {
			visible_count++;
		}
	}
	map.gap_count = MAX(0, visible_count - 1);

	if (p_center_child_index < 0 || p_center_child_index >= p_child_visible.size()) {
		return map;
	}
	if (!p_child_visible[p_center_child_index]) {
		return map;
	}

	int center_visible_index = 0;
	for (int i = 0; i < p_center_child_index; i++) {
		if (p_child_visible[i]) {
			center_visible_index++;
		}
	}

	if (center_visible_index >= 1) {
		map.left_center = center_visible_index - 1;
	}
	if (center_visible_index <= visible_count - 2) {
		map.center_right = center_visible_index;
	}
	return map;
}
