/**************************************************************************/
/*  editor_board_view.cpp                                                 */
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

#include "editor_board_view.h"

#include "core/math/math_funcs.h"

real_t EditorBoardView::scroll_offset_for_index(int p_index, const Size2 &p_viewport) {
	return -real_t(p_index) * p_viewport.width;
}

real_t EditorBoardView::overview_scale_for(int p_board_count, const Size2 &p_viewport) {
	if (p_board_count <= 1 || p_viewport.width <= 0.0) {
		return 1.0;
	}
	const real_t total_gutter = real_t(p_board_count - 1) * OVERVIEW_GUTTER;
	const real_t fitted = (p_viewport.width - total_gutter) / (real_t(p_board_count) * p_viewport.width);
	// Fixed-width gutters can exceed the viewport for a large enough board count;
	// clamp so the scale never goes negative and mirrors the strip.
	return CLAMP(fitted, real_t(0.0), real_t(1.0));
}

Point2 EditorBoardView::_overview_origin(int p_board_count, const Size2 &p_viewport, real_t p_scale) {
	const real_t board_width = p_viewport.width * p_scale;
	const real_t board_height = p_viewport.height * p_scale;
	const real_t total_width = real_t(p_board_count) * board_width + real_t(MAX(0, p_board_count - 1)) * OVERVIEW_GUTTER;
	const real_t left = (p_viewport.width - total_width) * 0.5;
	const real_t top = (p_viewport.height - board_height) * 0.5;
	return Point2(left, top);
}

void EditorBoardView::_begin_transition(real_t p_target_scroll_x, real_t p_target_scale) {
	transition_start_scroll_x = scroll_x;
	transition_start_scale = scale;
	target_scroll_x = p_target_scroll_x;
	target_scale = p_target_scale;
	transition = 0.0;
}

void EditorBoardView::switch_to_index(int p_index, const Size2 &p_viewport) {
	active_index = p_index;
	overview = false;
	_begin_transition(scroll_offset_for_index(p_index, p_viewport), 1.0);
}

void EditorBoardView::enter_overview(int p_board_count, int p_active_index, const Size2 &p_viewport) {
	active_index = p_active_index;
	overview = true;
	overview_board_count = p_board_count;
	overview_viewport = p_viewport;
	_begin_transition(0.0, overview_scale_for(p_board_count, p_viewport));
}

void EditorBoardView::exit_overview(int p_active_index, const Size2 &p_viewport) {
	active_index = p_active_index;
	overview = false;
	_begin_transition(scroll_offset_for_index(p_active_index, p_viewport), 1.0);
}

int EditorBoardView::index_at_point(const Point2 &p_point, int p_board_count, const Size2 &p_viewport) const {
	if (!overview || p_board_count <= 0) {
		return -1;
	}

	const real_t board_scale = overview_scale_for(p_board_count, p_viewport);
	const real_t board_width = p_viewport.width * board_scale;
	const real_t board_height = p_viewport.height * board_scale;

	const Point2 origin = _overview_origin(p_board_count, p_viewport, board_scale);
	if (p_point.y < origin.y || p_point.y > origin.y + board_height) {
		return -1;
	}

	for (int i = 0; i < p_board_count; i++) {
		const real_t board_left = origin.x + real_t(i) * (board_width + OVERVIEW_GUTTER);
		const real_t board_right = board_left + board_width;
		if (p_point.x >= board_left && p_point.x <= board_right) {
			return i;
		}
	}

	return -1;
}

void EditorBoardView::advance(real_t p_delta) {
	if (transition >= 1.0) {
		return;
	}

	transition = MIN(real_t(1.0), transition + p_delta / TRANSITION_DURATION);
	// Cubic ease-out: fast start, gentle settle at the target.
	const real_t eased = real_t(1.0) - Math::pow(real_t(1.0) - transition, real_t(3.0));

	scroll_x = Math::lerp(transition_start_scroll_x, target_scroll_x, eased);
	scale = Math::lerp(transition_start_scale, target_scale, eased);
}

Transform2D EditorBoardView::get_transform() const {
	Transform2D transform;
	transform.set_scale(Size2(scale, scale));
	// In overview, board 0's on-screen origin must match the centered, gutter-
	// aware layout index_at_point() hit-tests against; a plain (scroll_x, 0)
	// origin would leave every board drawn flush against the top-left corner.
	transform.set_origin(overview ? _overview_origin(overview_board_count, overview_viewport, scale) : Point2(scroll_x, 0.0));
	return transform;
}
