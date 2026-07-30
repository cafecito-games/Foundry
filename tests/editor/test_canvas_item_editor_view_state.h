/**************************************************************************/
/*  test_canvas_item_editor_view_state.h                                  */
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

#include "editor/scene/canvas_item_editor_view_state.h"

#include "tests/test_macros.h"

namespace TestCanvasItemEditorViewState {

TEST_CASE("[Editor][canvas-viewstate-transitions] Pan updates view offset") {
	CanvasItemEditorViewState state;
	state.zoom = 2.0;
	state.view_offset = Point2(10, 20);

	CanvasItemEditorViewMath::apply_pan(state, Vector2(8, -4));

	CHECK(state.view_offset.is_equal_approx(Point2(6, 22)));
}

TEST_CASE("[Editor][canvas-viewstate-transitions] Zoom-to-point preserves anchor") {
	CanvasItemEditorViewState state;
	state.zoom = 1.0;
	state.view_offset = Point2(100, 50);
	const Point2 anchor(200, 120);

	const Point2 scene_before = anchor / state.zoom + state.view_offset;
	const bool changed = CanvasItemEditorViewMath::apply_zoom_at_point(state, 2.0, anchor);
	CHECK(changed);
	const Point2 scene_after = anchor / state.zoom + state.view_offset;
	CHECK(scene_before.is_equal_approx(scene_after));
	CHECK(state.zoom == 2.0);
}

TEST_CASE("[Editor][canvas-viewstate-transitions] Integer zoom aligns view offset") {
	CanvasItemEditorViewState state;
	state.zoom = 1.0;
	state.view_offset = Point2(10.3, 7.6);

	CanvasItemEditorViewMath::apply_zoom_at_point(state, 2.0, Point2());

	CHECK(Math::is_equal_approx(state.zoom, real_t(2.0)));
	CHECK(state.view_offset.is_equal_approx(Point2(10.5, 7.5)));
}

TEST_CASE("[Editor][canvas-viewstate-transitions] Canvas transform matches zoom and offset") {
	CanvasItemEditorViewState state;
	state.zoom = 3.0;
	state.view_offset = Point2(4, -2);

	CanvasItemEditorViewMath::update_canvas_transform(state);

	CHECK(state.transform.get_scale().is_equal_approx(Size2(3, 3)));
	CHECK(state.transform.columns[2].is_equal_approx(Point2(-12, 6)));
}

TEST_CASE("[Editor][canvas-viewstate-transitions] Select results accumulate and sort") {
	Vector<CanvasItemEditorViewState::SelectResult> results;
	CanvasItemEditorViewMath::accumulate_select_result(results, nullptr, 5, true);
	CanvasItemEditorViewMath::accumulate_select_result(results, nullptr, 1, true);
	CanvasItemEditorViewMath::accumulate_select_result(results, nullptr, 3, true);

	CHECK(results.size() == 3);
	results.sort();
	CHECK(results[0].z_index == 5);
	CHECK(results[1].z_index == 3);
	CHECK(results[2].z_index == 1);
}

TEST_CASE("[Editor][canvas-viewstate-transitions] Reset drag clears interaction state") {
	CanvasItemEditorViewState state;
	state.drag_type = CanvasItemEditorViewState::DRAG_MOVE;
	state.message = "Moving";
	state.drag_selection.push_back(nullptr);

	state.reset_drag();

	CHECK(state.drag_type == CanvasItemEditorViewState::DRAG_NONE);
	CHECK(state.message.is_empty());
	CHECK(state.drag_selection.is_empty());
}

} // namespace TestCanvasItemEditorViewState
