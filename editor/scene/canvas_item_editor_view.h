/**************************************************************************/
/*  canvas_item_editor_view.h                                              */
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

#include "editor/scene/canvas_item_editor_view_state.h"
#include "scene/gui/control.h"

#include "editor/scene/canvas_item_editor_plugin.h"
class EditorSceneContext;
class EditorSelection;
class EditorZoomWidget;
class HScrollBar;
class SubViewport;
class SubViewportContainer;
class VBoxContainer;
class ViewPanner;
class VScrollBar;

// Instantiable 2D canvas editor view surface: viewport widgets, overlay draw,
// input forwarding, and scroll/zoom chrome. Bound to a ViewState, the global
// controller, and optionally an EditorSceneContext.
class CanvasItemEditorView : public Control {
	FOUNDRY_CLASS(CanvasItemEditorView, Control);

	friend class CanvasItemEditorViewport;

	CanvasItemEditor *editor = nullptr;
	EditorSceneContext *scene_context = nullptr;
	CanvasItemEditorViewState &view_state;

	Control *viewport_scrollable = nullptr;
	SubViewportContainer *scene_tree = nullptr;
	CanvasItemEditorViewport *viewport = nullptr;
	HScrollBar *h_scroll = nullptr;
	VScrollBar *v_scroll = nullptr;
	VBoxContainer *controls_vb = nullptr;
	Button *button_center_view = nullptr;
	EditorZoomWidget *zoom_widget = nullptr;
	Ref<ViewPanner> panner;
	bool plugin_forwarding_target = false;

	void _pan_callback(Vector2 p_scroll_vec, Ref<InputEvent> p_event);
	void _zoom_callback(float p_zoom_factor, Vector2 p_origin, Ref<InputEvent> p_event);

	void _draw_text_at_position(Point2 p_position, const String &p_string, Side p_side);
	void _draw_margin_at_position(int p_value, Point2 p_position, Side p_side);
	void _draw_percentage_at_position(real_t p_value, Point2 p_position, Side p_side);
	void _draw_straight_line(Point2 p_from, Point2 p_to, Color p_color);
	void _draw_smart_snapping();
	void _draw_rulers();
	void _draw_guides();
	void _draw_focus();
	void _draw_grid();
	void _draw_ruler_tool();
	void _draw_control_anchors(Control *control);
	void _draw_control_helpers(Control *control);
	void _draw_selection();
	void _draw_axis();
	void _draw_invisible_nodes_positions(Node *p_node, const Transform2D &p_parent_xform = Transform2D(), const Transform2D &p_canvas_xform = Transform2D());
	void _draw_locks_and_groups(Node *p_node, const Transform2D &p_parent_xform = Transform2D(), const Transform2D &p_canvas_xform = Transform2D());
	void _draw_hover();
	void _draw_message();
	void _draw_viewport();

	bool _gui_input_anchors(const Ref<InputEvent> &p_event);
	bool _gui_input_move(const Ref<InputEvent> &p_event);
	bool _gui_input_open_scene_on_double_click(const Ref<InputEvent> &p_event);
	bool _gui_input_scale(const Ref<InputEvent> &p_event);
	bool _gui_input_pivot(const Ref<InputEvent> &p_event);
	bool _gui_input_resize(const Ref<InputEvent> &p_event);
	bool _gui_input_rotate(const Ref<InputEvent> &p_event);
	bool _gui_input_select(const Ref<InputEvent> &p_event);
	bool _gui_input_ruler_tool(const Ref<InputEvent> &p_event);
	bool _gui_input_zoom_or_pan(const Ref<InputEvent> &p_event, bool p_already_accepted);
	bool _gui_input_rulers_and_guides(const Ref<InputEvent> &p_event);
	bool _gui_input_hover(const Ref<InputEvent> &p_event);
	void _gui_input_viewport(const Ref<InputEvent> &p_event);

	void _commit_drag();
	void _update_cursor();
	void _update_scroll(real_t);
	void _update_scrollbars();
	void _update_zoom(real_t p_zoom);
	void _zoom_on_position(real_t p_zoom, Point2 p_position = Point2());
	void _update_oversampling();
	void _shortcut_zoom_set(real_t p_zoom);

	SubViewport *get_scene_viewport() const;
	Node *get_edited_scene() const;
	EditorSelection *get_editor_selection() const;

public:
	CanvasItemEditorView(CanvasItemEditor *p_editor, CanvasItemEditorViewState &p_view_state);
	~CanvasItemEditorView();

	void build_ui(Control *p_parent, bool p_register_primary_container = true);
	void bind_context(EditorSceneContext *p_context);
	void push_viewport_state();

	EditorSceneContext *get_bound_context() const { return scene_context; }
	CanvasItemEditorViewState &get_view_state() { return view_state; }
	const CanvasItemEditorViewState &get_view_state() const { return view_state; }

	Transform2D get_canvas_transform() const { return view_state.transform; }

	Control *get_viewport_control() const { return viewport; }
	Control *get_viewport_scrollable() const { return viewport_scrollable; }
	SubViewportContainer *get_scene_viewport_container() const { return scene_tree; }
	VBoxContainer *get_controls_container() const { return controls_vb; }
	EditorZoomWidget *get_zoom_widget() const { return zoom_widget; }

	void update_viewport();
	void update_scrollbars();
	void update_oversampling();
	void commit_drag();
	void update_cursor();
	void update_panner_from_settings();
	void update_center_button_icon(const Ref<Texture2D> &p_icon);
	void center_at(const Point2 &p_pos);
	void active_scene_context_changed();
	void set_cursor_shape_override(Control::CursorShape p_shape = Control::CURSOR_ARROW);
	Control::CursorShape get_cursor_shape(const Point2 &p_pos) const override;
	bool is_plugin_forwarding_target() const { return plugin_forwarding_target; }

	// Test-only counter incremented by update_viewport(); used by unit tests.
	uint64_t test_update_viewport_invocations = 0;
};

// Shared routing for external per-view accessors (#929). CanvasItemEditor delegates here
// so focused-view selection and all-view fan-out stay unit-testable without EditorNode.
struct CanvasItemEditorViewRouting {
	static CanvasItemEditorView *get_focused_view(const Vector<CanvasItemEditorView *> &p_views);
	static Transform2D get_canvas_transform(const Vector<CanvasItemEditorView *> &p_views);
	static Control *get_viewport_control(const Vector<CanvasItemEditorView *> &p_views);
	static void set_cursor_shape_override(const Vector<CanvasItemEditorView *> &p_views, Control::CursorShape p_shape);
	static void update_all_viewports(const Vector<CanvasItemEditorView *> &p_views);
};
