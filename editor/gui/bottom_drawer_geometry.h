/**************************************************************************/
/*  bottom_drawer_geometry.h                                              */
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

// Pure geometry and migration math for the editor bottom drawer.
// Kept free of scene/editor dependencies so it is unit-testable headlessly.
//
// Terms:
// - strip:  the always-visible tab strip along the bottom of the workspace area.
// - body:   the content region of the drawer above the strip when a tab is open.
// - area:   the full height of the workspace overlay region (center_overlay).
// - inset:  vertical space the workspace reserves at its bottom edge.
struct BottomDrawerGeometry {
	static int drawer_height(bool p_open, bool p_expanded, int p_strip_height, int p_body_height, int p_area_height) {
		if (!p_open) {
			return p_strip_height;
		}
		if (p_expanded) {
			return p_area_height;
		}
		int height = p_strip_height + p_body_height;
		return height < p_area_height ? height : p_area_height;
	}

	// Unpinned drawers overlay the workspace, so only the strip is reserved.
	// Expanded drawers cover the workspace entirely; keeping the inset at the
	// strip lets un-expanding restore the previous workspace size instantly.
	static int workspace_inset(bool p_open, bool p_pinned, bool p_expanded, int p_strip_height, int p_body_height, int p_area_height) {
		if (!p_open || !p_pinned || p_expanded) {
			return p_strip_height;
		}
		int inset = p_strip_height + p_body_height;
		return inset < p_area_height ? inset : p_area_height;
	}

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
};
