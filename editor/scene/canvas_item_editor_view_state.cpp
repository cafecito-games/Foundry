/**************************************************************************/
/*  canvas_item_editor_view_state.cpp                                     */
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

#include "canvas_item_editor_view_state.h"

#include "scene/main/canvas_item.h"

void CanvasItemEditorViewState::reset_drag() {
	message = "";
	drag_type = DRAG_NONE;
	drag_selection.clear();
}

void CanvasItemEditorViewState::reset_snap_in_progress() {
	snap_target[0] = SNAP_TARGET_NONE;
	snap_target[1] = SNAP_TARGET_NONE;
	snap_transform = Transform2D();
}

void CanvasItemEditorViewState::clear_selection_results() {
	selection_results.clear();
	selection_results_menu.clear();
}

void CanvasItemEditorViewMath::apply_pan(CanvasItemEditorViewState &p_state, const Vector2 &p_scroll_vec) {
	p_state.view_offset.x -= p_scroll_vec.x / p_state.zoom;
	p_state.view_offset.y -= p_scroll_vec.y / p_state.zoom;
}

bool CanvasItemEditorViewMath::apply_zoom_at_point(CanvasItemEditorViewState &p_state, real_t p_new_zoom, const Point2 &p_anchor_in_viewport) {
	if (p_new_zoom == p_state.zoom) {
		return false;
	}

	const real_t prev_zoom = p_state.zoom;
	p_state.zoom = p_new_zoom;
	p_state.view_offset += p_anchor_in_viewport / prev_zoom - p_anchor_in_viewport / p_state.zoom;

	// Align in-scene pixels to screen pixels when zoom is an integer factor.
	const real_t closest_zoom_factor = Math::round(p_state.zoom);
	if (Math::is_zero_approx(p_state.zoom - closest_zoom_factor)) {
		const Vector2 view_offset_int = p_state.view_offset.floor();
		const Vector2 view_offset_frac = p_state.view_offset - view_offset_int;
		p_state.view_offset = view_offset_int + (view_offset_frac * closest_zoom_factor).round() / closest_zoom_factor;
	}

	return true;
}

void CanvasItemEditorViewMath::update_canvas_transform(CanvasItemEditorViewState &p_state) {
	p_state.transform = Transform2D();
	p_state.transform.scale_basis(Size2(p_state.zoom, p_state.zoom));
	p_state.transform.columns[2] = -p_state.view_offset * p_state.zoom;
}

void CanvasItemEditorViewMath::center_view_on_point(CanvasItemEditorViewState &p_state, const Point2 &p_scene_pos, const Vector2 &p_viewport_size, const Transform2D &p_global_canvas_transform) {
	const Vector2 offset = p_viewport_size / 2 - p_global_canvas_transform.xform(p_scene_pos);
	p_state.view_offset -= (offset / p_state.zoom).round();
}

void CanvasItemEditorViewMath::accumulate_select_result(Vector<CanvasItemEditorViewState::SelectResult> &p_results, CanvasItem *p_item, real_t p_z_index, bool p_has_z) {
	CanvasItemEditorViewState::SelectResult result;
	result.item = p_item;
	result.z_index = p_z_index;
	result.has_z = p_has_z;
	p_results.push_back(result);
}
