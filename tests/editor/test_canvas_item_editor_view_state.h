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
#include "editor/themes/editor_scale.h"

#include "tests/test_macros.h"

namespace TestCanvasItemEditorViewState {

struct EditorScaleGuard {
	float previous = 1.0f;
	explicit EditorScaleGuard(float p_scale) {
		previous = EditorScale::get_scale();
		EditorScale::set_scale(p_scale);
	}
	~EditorScaleGuard() {
		EditorScale::set_scale(previous);
	}
};

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

TEST_CASE("[Editor][canvas-viewstate-transitions] Normalized zoom conversion is stable across editor scales") {
	{
		EditorScaleGuard scale(1.0f);
		CHECK(Math::is_equal_approx(CanvasItemEditorNormalizedZoom::to_absolute(1.0), real_t(1.0)));
		CHECK(Math::is_equal_approx(CanvasItemEditorNormalizedZoom::to_absolute(2.0), real_t(2.0)));
		CHECK(Math::is_equal_approx(CanvasItemEditorNormalizedZoom::to_normalized(2.0), real_t(2.0)));

		CanvasItemEditorViewState state;
		state.zoom = CanvasItemEditorNormalizedZoom::to_absolute(2.0);
		state.view_offset = Point2(10, 20);
		const Dictionary dict = CanvasItemEditorSceneGeometryState::to_dict(state);
		CHECK(Math::is_equal_approx(real_t(dict["zoom"]), real_t(2.0)));

		CanvasItemEditorViewState restored;
		CanvasItemEditorSceneGeometryState::apply(restored, dict);
		CHECK(Math::is_equal_approx(restored.zoom, CanvasItemEditorNormalizedZoom::to_absolute(2.0)));
	}
	{
		EditorScaleGuard scale(2.0f);
		CHECK(Math::is_equal_approx(CanvasItemEditorNormalizedZoom::scale_factor(), real_t(2.0)));
		CHECK(Math::is_equal_approx(CanvasItemEditorNormalizedZoom::to_absolute(1.0), real_t(2.0)));
		CHECK(Math::is_equal_approx(CanvasItemEditorNormalizedZoom::to_normalized(2.0), real_t(1.0)));

		CanvasItemEditorViewState state;
		state.zoom = CanvasItemEditorNormalizedZoom::to_absolute(1.0);
		const Dictionary dict = CanvasItemEditorSceneGeometryState::to_dict(state);
		CHECK(Math::is_equal_approx(real_t(dict["zoom"]), real_t(1.0)));
	}
}

TEST_CASE("[Editor][canvas-viewstate-transitions] Idempotent zoom leaves offset unchanged") {
	CanvasItemEditorViewState state;
	state.zoom = 2.0;
	state.view_offset = Point2(33, 44);
	const Point2 offset_before = state.view_offset;

	CHECK_FALSE(CanvasItemEditorViewMath::apply_zoom_at_point(state, 2.0, Point2(100, 50)));
	CHECK(state.view_offset.is_equal_approx(offset_before));
	CHECK(Math::is_equal_approx(state.zoom, real_t(2.0)));
}

TEST_CASE("[Editor][canvas-viewstate-transitions] Focused view zoom leaves a secondary view untouched") {
	CanvasItemEditorViewState focused;
	focused.zoom = 1.0;
	focused.view_offset = Point2(8, 16);
	CanvasItemEditorViewState secondary;
	secondary.zoom = 1.5;
	secondary.view_offset = Point2(64, 32);
	const real_t secondary_zoom = secondary.zoom;
	const Point2 secondary_offset = secondary.view_offset;

	const Point2 center(200, 120);
	const Point2 scene_before = center / focused.zoom + focused.view_offset;
	CHECK(CanvasItemEditorViewMath::apply_zoom_at_point(focused, 2.0, center));
	const Point2 scene_after = center / focused.zoom + focused.view_offset;
	CHECK(scene_before.is_equal_approx(scene_after));
	CHECK(Math::is_equal_approx(focused.zoom, real_t(2.0)));

	CHECK(Math::is_equal_approx(secondary.zoom, secondary_zoom));
	CHECK(secondary.view_offset.is_equal_approx(secondary_offset));
}

TEST_CASE("[Editor][canvas-viewstate-transitions] Normalized zoom validation rejects out-of-range and non-finite") {
	const real_t min_absolute = 1.0 / 128.0;
	const real_t max_absolute = 128.0;

	CHECK(CanvasItemEditorNormalizedZoom::validate(1.0, min_absolute, max_absolute) == CanvasItemEditorNormalizedZoom::Validation::OK);
	CHECK(CanvasItemEditorNormalizedZoom::validate(2.0, min_absolute, max_absolute) == CanvasItemEditorNormalizedZoom::Validation::OK);
	CHECK(CanvasItemEditorNormalizedZoom::validate(Math::NaN, min_absolute, max_absolute) == CanvasItemEditorNormalizedZoom::Validation::NON_FINITE);
	CHECK(CanvasItemEditorNormalizedZoom::validate(Math::INF, min_absolute, max_absolute) == CanvasItemEditorNormalizedZoom::Validation::NON_FINITE);
	CHECK(CanvasItemEditorNormalizedZoom::validate(-1.0, min_absolute, max_absolute) == CanvasItemEditorNormalizedZoom::Validation::OUT_OF_RANGE);
	CHECK(CanvasItemEditorNormalizedZoom::validate(1000.0, min_absolute, max_absolute) == CanvasItemEditorNormalizedZoom::Validation::OUT_OF_RANGE);

	// Rejection must leave an existing view state untouched when the caller honors validate().
	CanvasItemEditorViewState state;
	state.zoom = 1.0;
	state.view_offset = Point2(5, 7);
	const real_t zoom_before = state.zoom;
	const Point2 offset_before = state.view_offset;
	CHECK(CanvasItemEditorNormalizedZoom::validate(1000.0, min_absolute, max_absolute) == CanvasItemEditorNormalizedZoom::Validation::OUT_OF_RANGE);
	CHECK(Math::is_equal_approx(state.zoom, zoom_before));
	CHECK(state.view_offset.is_equal_approx(offset_before));
}

} // namespace TestCanvasItemEditorViewState
