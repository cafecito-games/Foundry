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

// Per-view editing state for the 2D canvas editor.
//
// Plain data only — no Control, no signals — so pan/zoom/select transitions can be
// unit-tested without instantiating widgets. One instance is owned per editing surface
// (today a single instance on the CanvasItemEditor singleton; multi-instance comes with U2).
//
// Classification rule when adding fields:
//   - Per-view (belongs here): written/read during viewport input, draw, or scroll sync.
//   - Global (stays on CanvasItemEditor): written by toolbar, menus, or editor settings.
struct CanvasItemEditorViewState {
	// Active viewport drag operation. Set on mouse-down in the viewport, cleared by
	// reset_drag() when the interaction ends or is cancelled.
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

	// Transient snap target chosen during an in-progress snap_point() call.
	// Index 0 tracks the X axis, index 1 the Y axis.
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

	// A canvas item hit by a viewport click, queued before selection is committed.
	// Sorted by z_index (descending) when multiple items overlap.
	struct SelectResult {
		CanvasItem *item = nullptr; // Candidate item under the cursor.
		real_t z_index = 0; // Node2D draw order; 0 when the item has no z_index.
		bool has_z = true; // False for non-Node2D items (sorting deprioritizes them).
		_FORCE_INLINE_ bool operator<(const SelectResult &p_rr) const {
			return has_z && p_rr.has_z ? p_rr.z_index < z_index : p_rr.has_z;
		}
	};

	// One entry in the stacked-item hover tooltip drawn over the viewport.
	struct HoverResult {
		Point2 position; // Scene-space position of the hovered item origin.
		Ref<Texture2D> icon; // Type icon shown in the hover label.
		String name; // Display name shown in the hover label.
	};

	// --- View camera (pan / zoom / transform) ---

	// Canvas-to-screen transform pushed to the edited scene each draw frame.
	// Derived from zoom and view_offset by CanvasItemEditorViewMath::update_canvas_transform().
	Transform2D transform;
	// Current viewport zoom factor (1.0 = 100%). Scene units per screen pixel.
	real_t zoom = 1.0;
	// Top-left scene point visible in the viewport, in scene coordinates.
	Point2 view_offset;
	// view_offset snapshot from the last scrollbar update; used to detect scroll-driven changes.
	Point2 previous_update_view_offset;

	// --- Pan / pivot / ruler transient state ---

	// True while the view panner is actively dragging the viewport (middle-mouse or space-pan).
	bool pan_pressed = false;
	// Temporary pivot override while Shift+V is held during select. Math::INF means inactive.
	Vector2 temp_pivot = Vector2(Math::INF, Math::INF);

	// True while the ruler tool is measuring between two scene points.
	bool ruler_tool_active = false;
	// Scene-space origin of the active ruler measurement.
	Point2 ruler_tool_origin;
	// Scene-space position where the next dropped/created node will be placed.
	Point2 node_create_position;
	// Screen-pixel grab radius for hit-testing controls at the current zoom.
	real_t grab_distance = 0.0;

	// --- Selection / hover results (viewport hit-test output) ---

	// Items under the cursor from the latest click or box-select query.
	Vector<SelectResult> selection_results;
	// Copy of selection_results shown in the overlapping-items popup menu.
	Vector<SelectResult> selection_results_menu;
	// Items currently shown in the stacked hover tooltip.
	Vector<HoverResult> hovering_results;

	// --- Drag / guide interaction cluster ---

	// Scene-space point where the current drag gesture started (guides, box-select).
	Point2 drag_start_origin;
	// Kind of in-progress viewport drag; DRAG_NONE when idle.
	DragType drag_type = DRAG_NONE;
	// Drag start point in viewport-local coordinates.
	Point2 drag_from;
	// Current drag point in viewport-local coordinates.
	Point2 drag_to;
	// Pivot used as the rotation center during DRAG_ROTATE.
	Point2 drag_rotation_center;
	// Canvas items being transformed by the active drag (move/scale/rotate/pivot).
	List<CanvasItem *> drag_selection;
	// Index into the scene's guide list while dragging a guide; -1 when not dragging a guide.
	int dragged_guide_index = -1;
	// Viewport-local position of the guide being dragged (for overlay label).
	Point2 dragged_guide_pos;
	// True when the cursor is over a horizontal guide line in the viewport.
	bool is_hovering_h_guide = false;
	// True when the cursor is over a vertical guide line in the viewport.
	bool is_hovering_v_guide = false;

	// True while a numeric transform dialog is updating a selected property (suppresses feedback loops).
	bool updating_value_dialog = false;
	// Transform of the primary selected item captured at drag start (for relative move/rotate/scale).
	Transform2D original_transform;
	// Opposite corner of an in-progress box selection, in viewport-local coordinates.
	Point2 box_selecting_to;
	// Cursor shape forced by the active drag handle; CURSOR_ARROW restores the default.
	Control::CursorShape cursor_shape_override = Control::CURSOR_ARROW;

	// --- Snap-in-progress (written during snap_point(), cleared each call) ---

	// Closest snap target found so far on each axis during snap_point().
	SnapTarget snap_target[2];
	// Rotation + translation encoding the active snap alignment for smart-snap drawing.
	Transform2D snap_transform;

	// --- Ephemeral viewport UI ---

	// Status text drawn in the viewport corner (drag delta, drop preview, errors).
	String message;
	// True while scrollbar values are being pushed to view_offset (prevents re-entrant scroll sync).
	bool updating_scroll = false;

	// Clears drag_type, drag_selection, and message.
	void reset_drag();
	// Clears snap_target and snap_transform before a new snap_point() pass.
	void reset_snap_in_progress();
	// Clears selection_results and selection_results_menu.
	void clear_selection_results();
};

// Pure view math that operates on CanvasItemEditorViewState without Control dependencies.
struct CanvasItemEditorViewMath {
	// Applies a pan scroll delta to view_offset, accounting for the current zoom.
	static void apply_pan(CanvasItemEditorViewState &p_state, const Vector2 &p_scroll_vec);
	// Sets zoom while keeping the scene point under p_anchor_in_viewport fixed on screen.
	// Returns false when p_new_zoom equals the current zoom (no-op).
	static bool apply_zoom_at_point(CanvasItemEditorViewState &p_state, real_t p_new_zoom, const Point2 &p_anchor_in_viewport);
	// Recomputes transform from zoom and view_offset.
	static void update_canvas_transform(CanvasItemEditorViewState &p_state);
	// Adjusts view_offset so p_scene_pos appears at the center of a viewport of p_viewport_size.
	static void center_view_on_point(CanvasItemEditorViewState &p_state, const Point2 &p_scene_pos, const Vector2 &p_viewport_size, const Transform2D &p_global_canvas_transform);
	// Appends one hit-test result to a selection-results vector.
	static void accumulate_select_result(Vector<CanvasItemEditorViewState::SelectResult> &p_results, CanvasItem *p_item, real_t p_z_index, bool p_has_z);
};
