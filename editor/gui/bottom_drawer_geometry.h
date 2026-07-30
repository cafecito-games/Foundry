/**************************************************************************/
/*  bottom_drawer_geometry.h                                              */
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

// Pure geometry and migration math for the editor bottom drawer.
// Kept free of scene/editor dependencies so it is unit-testable headlessly.
//
// Terms:
// - strip:  the slim status strip control along the bottom of the editor
//           window; a separate control, not part of the drawer.
// - body:   the content region of the drawer shown when a tab is open.
// - area:   the gui_base region above the strip available to the drawer.
// - island: the floating card shown for an unpinned open drawer.
struct BottomDrawerGeometry {
	static int clamp_body_height(int p_body_height, int p_min_body_height, int p_strip_height, int p_area_height) {
		int max_body = p_area_height - p_strip_height;
		int height = p_body_height < max_body ? p_body_height : max_body;
		return height > p_min_body_height ? height : p_min_body_height;
	}

	// Layout configs written before the drawer stored center_split offsets,
	// which were negative when the panel was resized taller. New configs store
	// positive drawer body heights.
	static int body_height_from_stored(int p_stored, int p_fallback) {
		if (p_stored < 0) {
			return -p_stored;
		}
		return p_stored > 0 ? p_stored : p_fallback;
	}

	// The island is the floating card shown for an unpinned open drawer.
	// Width targets 64% of the window, kept inside a minimum side margin,
	// with an absolute minimum so panel content stays usable. The minimum width
	// wins over the margin clamp, so on a window narrower than the minimum the
	// result exceeds the window; island_x then returns a negative x and the
	// island overflows symmetrically past both edges.
	static int island_width(int p_window_width, int p_min_width, int p_min_side_margin) {
		int nominal = (p_window_width * 64) / 100;
		return clamp_island_width(nominal, p_window_width, p_min_width, p_min_side_margin);
	}

	static int clamp_island_width(int p_width, int p_window_width, int p_min_width, int p_min_side_margin) {
		int max_width = p_window_width - 2 * p_min_side_margin;
		int width = p_width < max_width ? p_width : max_width;
		return width > p_min_width ? width : p_min_width;
	}

	static int island_width_with_override(int p_window_width, int p_min_width, int p_min_side_margin, int p_override_width) {
		if (p_override_width <= 0) {
			return island_width(p_window_width, p_min_width, p_min_side_margin);
		}
		return clamp_island_width(p_override_width, p_window_width, p_min_width, p_min_side_margin);
	}

	static int island_width_from_edge_drag(int p_start_width, int p_mouse_delta_x, bool p_right_edge, int p_window_width, int p_min_width, int p_min_side_margin) {
		const int grow_delta = p_right_edge ? p_mouse_delta_x : -p_mouse_delta_x;
		return clamp_island_width(p_start_width + 2 * grow_delta, p_window_width, p_min_width, p_min_side_margin);
	}

	static int island_x(int p_window_width, int p_island_width) {
		return (p_window_width - p_island_width) / 2;
	}

	// Height of the island body region (excludes nothing; the strip is a
	// separate control). Expanded fills the area minus a top margin.
	static int island_height(bool p_expanded, int p_body_height, int p_area_height, int p_top_margin) {
		int max_height = p_area_height - p_top_margin;
		if (max_height < 0) {
			max_height = 0;
		}
		if (p_expanded) {
			return max_height;
		}
		return p_body_height < max_height ? p_body_height : max_height;
	}
};
