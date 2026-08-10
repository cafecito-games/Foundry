/**************************************************************************/
/*  editor_board_view.h                                                   */
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

#include "core/math/transform_2d.h"
#include "core/math/vector2.h"

// Control-free geometry and animation state behind board switching and the
// overview. Boards are laid edge-to-edge horizontally at the viewport width
// while switching; scrolling the strip by a whole viewport width per board
// centers the board at that index.
//
// A single Transform2D cannot, on its own, open up gaps between boards that
// are otherwise laid out edge-to-edge: get_transform() only ever supplies one
// uniform scale and one origin for the whole board container. Producing the
// gutter visible between boards in the overview additionally requires whoever
// positions the board children (the strip, not this class, which stays
// Control-free) to widen each board's local horizontal pitch by
// get_board_pitch_gutter(), instead of the fixed `p_viewport.width` pitch used
// while switching. get_transform()'s origin already accounts for the
// resulting wider content block, and get_board_pitch_gutter() itself ramps
// from 0 to OVERVIEW_GUTTER (and back) in step with scale and origin, so the
// three combine every frame to produce exactly the layout index_at_point()
// hit-tests against, with no jump at the start or end of the transition.
//
// Every member is either a plain data field or a static/const function so
// this class is testable headlessly: it never includes any scene-tree UI or
// node headers and never touches a Control.
class EditorBoardView {
public:
	// Horizontal gap, in pixels, between adjacent boards while in overview.
	static constexpr real_t OVERVIEW_GUTTER = 24.0;

private:
	// Time, in seconds, a switch or overview transition takes to complete.
	static constexpr real_t TRANSITION_DURATION = 0.25;
	// overview_scale_for() never returns less than this, so a board count large
	// enough to make the fixed gutters exceed the viewport width still produces
	// a finite, strictly positive pitch instead of dividing by zero.
	static constexpr real_t MIN_OVERVIEW_SCALE = 0.05;

	Point2 origin;
	real_t scale = 1.0;
	// Current on-screen gutter width in pixels, animated in step with origin
	// and scale: 0 while switching, OVERVIEW_GUTTER once settled in overview.
	real_t pitch_gutter = 0.0;
	int active_index = 0;

	Point2 transition_start_origin;
	real_t transition_start_scale = 1.0;
	real_t transition_start_pitch_gutter = 0.0;
	Point2 target_origin;
	real_t target_scale = 1.0;
	real_t target_pitch_gutter = 0.0;

	// Normalised transition progress in [0, 1]. 1 means idle: origin, scale,
	// and pitch_gutter already equal their targets.
	real_t transition = 1.0;

	bool overview = false;

	// Captures the current origin/scale/gutter as the transition's starting
	// point, so switching targets mid-flight retargets smoothly instead of
	// jumping.
	void _begin_transition(const Point2 &p_target_origin, real_t p_target_scale, real_t p_target_pitch_gutter);
	// The top-left corner, in screen pixels, at which board index 0 must be
	// drawn so that p_board_count boards laid out at p_scale, separated by
	// OVERVIEW_GUTTER, are centered inside p_viewport. Shared by
	// index_at_point() and enter_overview()/exit_overview() so the clickable
	// geometry and the animation target can never drift apart.
	static Point2 _overview_origin(int p_board_count, const Size2 &p_viewport, real_t p_scale);

public:
	// The scroll offset that lands board p_index at the viewport origin at
	// scale 1: boards are laid edge-to-edge, so each one is a full viewport
	// width apart.
	static real_t scroll_offset_for_index(int p_index, const Size2 &p_viewport);

	// The uniform scale that fits p_board_count boards, plus the gutters
	// between them, inside the viewport width. Never exceeds 1: a single
	// board is never blown up past its natural size. Never goes below
	// MIN_OVERVIEW_SCALE either, even when the fixed-width gutters alone would
	// exceed the viewport for a large enough board count, so the resulting
	// scale always stays finite and strictly positive.
	static real_t overview_scale_for(int p_board_count, const Size2 &p_viewport);

	// Begins an animated switch to the board at p_index, retargeting from the
	// current position rather than snapping if a transition is already in
	// progress.
	void switch_to_index(int p_index, const Size2 &p_viewport);

	// Enters the overview layout: every board visible at once, scaled to fit
	// the viewport width and centered vertically. p_active_index records which
	// board is highlighted as active; it does not affect board placement.
	void enter_overview(int p_board_count, int p_active_index, const Size2 &p_viewport);

	// Leaves the overview layout, animating back to p_active_index at scale 1.
	void exit_overview(int p_active_index, const Size2 &p_viewport);

	// Resolves the board under p_point while the overview is active, or -1
	// when the view is not in overview or the point is outside every board
	// (including the gutters between them and the space above/below them).
	// Targets the overview's settled layout regardless of transition progress,
	// so a click during the brief zoom-out animation still selects the board
	// it is animating towards.
	int index_at_point(const Point2 &p_point, int p_board_count, const Size2 &p_viewport) const;

	// Advances the current transition by p_delta seconds along an ease-out
	// curve. No-op once the transition has already completed.
	void advance(real_t p_delta);
	bool is_animating() const { return transition < 1.0; }

	// Immediately lands on the current transition's target instead of animating towards
	// it. Used when the animation's premise breaks mid-flight -- most notably a board
	// central to the switch being closed -- so the view settles on a consistent, current
	// layout rather than keep sliding an extra frame toward geometry that no longer
	// matches the board list.
	void finish_transition() {
		origin = target_origin;
		scale = target_scale;
		pitch_gutter = target_pitch_gutter;
		transition = 1.0;
	}

	// The transform the strip applies to its board container: uniform scaling
	// by the current scale, translated by the current origin. See the class
	// comment for how this combines with the strip's own child pitch to
	// produce the overview's inter-board gutters.
	Transform2D get_transform() const;

	// The additional local (pre-transform) horizontal pitch, in local pixels,
	// the strip must add to each board's position beyond p_viewport.width so
	// that, once get_transform() scales the container, adjacent boards end up
	// OVERVIEW_GUTTER pixels apart on screen. Ramps continuously between 0 and
	// OVERVIEW_GUTTER / (current scale) alongside get_transform()'s own origin
	// and scale, so entering or leaving the overview never jumps a board's
	// position.
	real_t get_board_pitch_gutter() const {
		return scale > CMP_EPSILON ? pitch_gutter / scale : 0.0;
	}

	bool is_overview_active() const { return overview; }
	int get_active_index() const { return active_index; }
	real_t get_scroll_x() const { return origin.x; }
	real_t get_scale() const { return scale; }

	EditorBoardView() {}
};
