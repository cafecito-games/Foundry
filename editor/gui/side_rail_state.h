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

#include "core/math/math_defs.h"
#include "core/templates/vector.h"
#include "core/typedefs.h"

// Scene-free helpers for per-tile side-rail state. Kept free of scene/editor
// dependencies so it is unit-testable headlessly, mirroring
// editor/gui/bottom_drawer_geometry.h.
//
// It holds the tile-dock split-gap identity mapping, the rail label-fit
// decision, and the per-side collapse state machine. It owns no controls: every
// dock is referred to by its index in that side's ordered dock list, and the
// caller maps indices back to controls.

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

// Which side of a tile's dock region a rail mirrors.
enum class SideRailSide {
	LEFT,
	RIGHT,
};

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

// The collapse mode of one side of one tile. DOCKED is the side as it has
// always behaved; RAILED costs one rail width and shows at most one dock.
enum class SideRailMode {
	DOCKED,
	RAILED,
};

// A side's whole collapse state. Docks are identified by their index in that
// side's ordered dock list, so this stays free of any control dependency; the
// owner re-resolves indices against the live list before every transition.
struct SideRailSideState {
	SideRailMode mode = SideRailMode::DOCKED;
	// The dock the drawer is open on. -1 means the drawer is closed. Only
	// meaningful in RAILED.
	int drawer_dock = -1;
	// The dock a later return to RAILED reopens. Survives closing the drawer
	// and switching back to DOCKED, so the mode toggle is reversible without
	// losing which dock the user was looking at. -1 when there is none.
	int last_drawer_dock = -1;
};

// What pressing a rail toggle does. The state alone cannot express
// FOCUS_DOCK, which changes no collapse state and only moves the side's
// selection, so the transition reports its action separately.
enum class SideRailToggleAction {
	COLLAPSE_SIDE, // DOCKED -> RAILED with the drawer closed.
	FOCUS_DOCK, // Stays DOCKED; the caller selects the pressed dock.
	OPEN_DRAWER, // RAILED; the drawer opens on the pressed dock.
	CLOSE_DRAWER, // RAILED; the drawer closes.
};

struct SideRailToggleResult {
	SideRailToggleAction action = SideRailToggleAction::FOCUS_DOCK;
	SideRailSideState state;
};

// Closing the drawer from DOCKED collapses the side, which is what the close
// button and pressing the already-shown dock's toggle both mean. p_shown_dock
// is the dock the side currently shows in DOCKED mode, or -1 when it shows
// none; it is remembered so the side can be reopened on it.
inline SideRailSideState side_rail_close_drawer(const SideRailSideState &p_state, int p_shown_dock) {
	SideRailSideState next = p_state;
	const int closing_dock = p_state.mode == SideRailMode::RAILED ? p_state.drawer_dock : p_shown_dock;
	if (closing_dock >= 0) {
		next.last_drawer_dock = closing_dock;
	}
	next.mode = SideRailMode::RAILED;
	next.drawer_dock = -1;
	return next;
}

// Returning to DOCKED never clears which dock the drawer was on, so toggling
// the mode twice lands back where it started.
inline SideRailSideState side_rail_expand(const SideRailSideState &p_state) {
	SideRailSideState next = p_state;
	if (p_state.drawer_dock >= 0) {
		next.last_drawer_dock = p_state.drawer_dock;
	}
	next.mode = SideRailMode::DOCKED;
	next.drawer_dock = -1;
	return next;
}

inline SideRailToggleResult side_rail_press_toggle(const SideRailSideState &p_state, int p_pressed_dock, int p_shown_dock) {
	SideRailToggleResult result;
	if (p_state.mode == SideRailMode::DOCKED) {
		if (p_pressed_dock >= 0 && p_pressed_dock == p_shown_dock) {
			result.action = SideRailToggleAction::COLLAPSE_SIDE;
			result.state = side_rail_close_drawer(p_state, p_shown_dock);
			return result;
		}
		result.action = SideRailToggleAction::FOCUS_DOCK;
		result.state = p_state;
		return result;
	}

	if (p_pressed_dock >= 0 && p_pressed_dock == p_state.drawer_dock) {
		result.action = SideRailToggleAction::CLOSE_DRAWER;
		result.state = side_rail_close_drawer(p_state, p_shown_dock);
		return result;
	}

	result.action = SideRailToggleAction::OPEN_DRAWER;
	result.state = p_state;
	result.state.drawer_dock = p_pressed_dock;
	if (p_pressed_dock >= 0) {
		result.state.last_drawer_dock = p_pressed_dock;
	}
	return result;
}

// The mode toggle the side's shortcut drives. Collapsing reopens the drawer on
// the last dock the side had, falling back to whatever it currently shows.
inline SideRailSideState side_rail_toggle_mode(const SideRailSideState &p_state, int p_shown_dock) {
	if (p_state.mode == SideRailMode::RAILED) {
		return side_rail_expand(p_state);
	}

	SideRailSideState next = p_state;
	next.mode = SideRailMode::RAILED;
	next.drawer_dock = p_state.last_drawer_dock >= 0 ? p_state.last_drawer_dock : p_shown_dock;
	if (next.drawer_dock >= 0) {
		next.last_drawer_dock = next.drawer_dock;
	}
	return next;
}

// Focusing a dock on a railed side must open it in the drawer, or the focus
// lands on a hidden control.
inline SideRailSideState side_rail_focus_dock(const SideRailSideState &p_state, int p_dock) {
	SideRailSideState next = p_state;
	if (p_state.mode == SideRailMode::RAILED && p_dock >= 0) {
		next.drawer_dock = p_dock;
		next.last_drawer_dock = p_dock;
	}
	return next;
}

// A dock that has been disabled, or has gone away entirely, can never remain
// the open drawer dock, nor be the dock a later expansion would reopen.
inline SideRailSideState side_rail_drawer_dock_unavailable(const SideRailSideState &p_state, int p_dock) {
	SideRailSideState next = p_state;
	if (p_dock < 0) {
		return next;
	}
	if (next.drawer_dock == p_dock) {
		next.drawer_dock = -1;
	}
	if (next.last_drawer_dock == p_dock) {
		next.last_drawer_dock = -1;
	}
	return next;
}

// What one side of the tile body should show, given its state and how many
// docks that side owns.
struct SideRailSideVisibility {
	// Whether the side's dock column is shown at all. False collapses the
	// column away, leaving only the rail.
	bool side_shown = true;
	// DOCKED: the side keeps its ordinary content (every enabled left dock, or
	// the right tab stack's own selection).
	bool all_docks_shown = true;
	// RAILED with an open drawer: the only dock index the side shows.
	int single_dock = -1;
};

inline SideRailSideVisibility side_rail_side_visibility(const SideRailSideState &p_state, int p_dock_count) {
	SideRailSideVisibility visibility;
	if (p_state.mode == SideRailMode::DOCKED) {
		return visibility;
	}

	visibility.all_docks_shown = false;
	if (p_state.drawer_dock < 0 || p_state.drawer_dock >= p_dock_count) {
		visibility.side_shown = false;
		return visibility;
	}
	visibility.single_dock = p_state.drawer_dock;
	return visibility;
}
