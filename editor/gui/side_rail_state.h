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

// Whether a side rail's toggles currently show icon plus rotated label, or
// icon only. The overflow fallback (issue #1991) rebuilds icon-only when the
// labelled toggles do not fit the rail's available height.
enum class SideRailLabelMode {
	LABELLED,
	ICON_ONLY,
};

// The two hysteresis thresholds a rail's fit decision is made against. Derived
// from the labelled-mode minimum heights of its toggles: dropping to icon-only
// requires less than the summed labelled height, and returning to labelled
// requires at least that sum plus the tallest single toggle's height. The gap
// between the two thresholds is what makes the decision non-oscillating: a
// rebuild that drops to icon-only shrinks the rail's own minimum width, never
// its available height, so the same available height can never satisfy both
// "drop" and "return" at once.
struct SideRailFitThresholds {
	real_t drop_to_icon_only = 0;
	real_t return_to_labelled = 0;
};

// p_toggle_labelled_heights are each toggle's minimum height in labelled mode,
// regardless of the rail's current mode.
inline SideRailFitThresholds side_rail_fit_thresholds(const Vector<real_t> &p_toggle_labelled_heights) {
	SideRailFitThresholds thresholds;
	real_t max_toggle_height = 0;
	for (int i = 0; i < p_toggle_labelled_heights.size(); i++) {
		thresholds.drop_to_icon_only += p_toggle_labelled_heights[i];
		max_toggle_height = MAX(max_toggle_height, p_toggle_labelled_heights[i]);
	}
	thresholds.return_to_labelled = thresholds.drop_to_icon_only + max_toggle_height;
	return thresholds;
}

// Pure hysteresis decision: given the rail's current mode and its available
// height, decide the mode it should rebuild with. Never returns a mode change
// that the same available height could immediately reverse.
inline SideRailLabelMode side_rail_fit_label_mode(SideRailLabelMode p_current_mode, real_t p_available_height, const SideRailFitThresholds &p_thresholds) {
	if (p_current_mode == SideRailLabelMode::ICON_ONLY) {
		return p_available_height >= p_thresholds.return_to_labelled ? SideRailLabelMode::LABELLED : SideRailLabelMode::ICON_ONLY;
	}
	return p_available_height < p_thresholds.drop_to_icon_only ? SideRailLabelMode::ICON_ONLY : SideRailLabelMode::LABELLED;
}
