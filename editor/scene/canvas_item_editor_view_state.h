/**************************************************************************/
/*  canvas_item_editor_view_state.h                                       */
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

#include "core/math/transform_2d.h"
#include "core/templates/list.h"
#include "core/templates/vector.h"
#include "scene/gui/control.h"

class CanvasItem;

// Per-view editing state for the 2D canvas editor. Plain data only — no Control,
// no signals — so transitions can be unit-tested without instantiating widgets.
struct CanvasItemEditorViewState {
	enum DragType {
		DRAG_NONE,
		DRAG_BOX_SELECTION,
		DRAG_LEFT,
		DRAG_TOP_LEFT,
		DRAG_TOP,
		DRAG_TOP_RIGHT,
		DRAG_RIGHT,
		DRAG_BOTTOM_RIGHT,
		DRAG_BOTTOM,
		DRAG_BOTTOM_LEFT,
		DRAG_ANCHOR_TOP_LEFT,
		DRAG_ANCHOR_TOP_RIGHT,
		DRAG_ANCHOR_BOTTOM_RIGHT,
		DRAG_ANCHOR_BOTTOM_LEFT,
		DRAG_ANCHOR_ALL,
		DRAG_QUEUED,
		DRAG_MOVE,
		DRAG_MOVE_X,
		DRAG_MOVE_Y,
		DRAG_SCALE_X,
		DRAG_SCALE_Y,
		DRAG_SCALE_BOTH,
		DRAG_ROTATE,
		DRAG_PIVOT,
		DRAG_TEMP_PIVOT,
		DRAG_V_GUIDE,
		DRAG_H_GUIDE,
		DRAG_DOUBLE_GUIDE,
		DRAG_KEY_MOVE
	};

	enum SnapTarget {
		SNAP_TARGET_NONE = 0,
		SNAP_TARGET_PARENT,
		SNAP_TARGET_SELF_ANCHORS,
		SNAP_TARGET_SELF,
		SNAP_TARGET_OTHER_NODE,
		SNAP_TARGET_GUIDE,
		SNAP_TARGET_GRID,
		SNAP_TARGET_PIXEL
	};

	struct SelectResult {
		CanvasItem *item = nullptr;
		real_t z_index = 0;
		bool has_z = true;
		_FORCE_INLINE_ bool operator<(const SelectResult &p_rr) const {
			return has_z && p_rr.has_z ? p_rr.z_index < z_index : p_rr.has_z;
		}
	};

	struct HoverResult {
		Point2 position;
		Ref<Texture2D> icon;
		String name;
	};

	Transform2D transform;
	real_t zoom = 1.0;
	Point2 view_offset;
	Point2 previous_update_view_offset;

	bool pan_pressed = false;
	Vector2 temp_pivot = Vector2(Math::INF, Math::INF);

	bool ruler_tool_active = false;
	Point2 ruler_tool_origin;
	Point2 node_create_position;
	real_t grab_distance = 0.0;

	Vector<SelectResult> selection_results;
	Vector<SelectResult> selection_results_menu;
	Vector<HoverResult> hovering_results;

	Point2 drag_start_origin;
	DragType drag_type = DRAG_NONE;
	Point2 drag_from;
	Point2 drag_to;
	Point2 drag_rotation_center;
	List<CanvasItem *> drag_selection;
	int dragged_guide_index = -1;
	Point2 dragged_guide_pos;
	bool is_hovering_h_guide = false;
	bool is_hovering_v_guide = false;

	bool updating_value_dialog = false;
	Transform2D original_transform;
	Point2 box_selecting_to;
	Control::CursorShape cursor_shape_override = Control::CURSOR_ARROW;

	SnapTarget snap_target[2];
	Transform2D snap_transform;

	String message;
	bool updating_scroll = false;

	void reset_drag();
	void reset_snap_in_progress();
	void clear_selection_results();
};

// View math helpers that operate on plain ViewState data.
struct CanvasItemEditorViewMath {
	static void apply_pan(CanvasItemEditorViewState &p_state, const Vector2 &p_scroll_vec);
	static bool apply_zoom_at_point(CanvasItemEditorViewState &p_state, real_t p_new_zoom, const Point2 &p_anchor_in_viewport);
	static void update_canvas_transform(CanvasItemEditorViewState &p_state);
	static void center_view_on_point(CanvasItemEditorViewState &p_state, const Point2 &p_scene_pos, const Vector2 &p_viewport_size, const Transform2D &p_global_canvas_transform);
	static void accumulate_select_result(Vector<CanvasItemEditorViewState::SelectResult> &p_results, CanvasItem *p_item, real_t p_z_index, bool p_has_z);
};
