/**************************************************************************/
/*  canvas_item_editor_view.cpp                                           */
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

#include "canvas_item_editor_view.h"

#include "canvas_item_editor_plugin.h"
#include "editor/docks/scene_tree_dock.h"
#include "editor/editor_node.h"
#include "editor/editor_scene_context.h"
#include "editor/editor_string_names.h"
#include "editor/editor_undo_redo_manager.h"
#include "editor/gui/editor_toaster.h"
#include "editor/gui/editor_zoom_widget.h"
#include "editor/plugins/editor_plugin_list.h"
#include "editor/settings/editor_feature_profile.h"
#include "editor/settings/editor_settings.h"
#include "editor/themes/editor_scale.h"
#include "editor/translations/editor_translation_preview_button.h"
#include "scene/2d/node_2d.h"
#include "scene/gui/box_container.h"
#include "scene/gui/subviewport_container.h"
#include "scene/gui/view_panner.h"
#include "scene/main/canvas_item.h"
#include "scene/main/canvas_layer.h"
#include "scene/main/timer.h"

#include "core/config/project_settings.h"
#include "core/string/translation_server.h"

#define DRAG_THRESHOLD (8 * EDSCALE)
constexpr real_t SCALE_HANDLE_DISTANCE = 25;
constexpr real_t MOVE_HANDLE_DISTANCE = 25;

CanvasItemEditorView::CanvasItemEditorView(CanvasItemEditor *p_editor, CanvasItemEditorViewState &p_view_state) :
		editor(p_editor),
		view_state(p_view_state) {
}

CanvasItemEditorView::~CanvasItemEditorView() = default;

CanvasItemEditorView *CanvasItemEditorViewRouting::get_focused_view(const Vector<CanvasItemEditorView *> &p_views) {
	return p_views.is_empty() ? nullptr : p_views[0];
}

Transform2D CanvasItemEditorViewRouting::get_canvas_transform(const Vector<CanvasItemEditorView *> &p_views) {
	const CanvasItemEditorView *view = get_focused_view(p_views);
	return view ? view->get_canvas_transform() : Transform2D();
}

Control *CanvasItemEditorViewRouting::get_viewport_control(const Vector<CanvasItemEditorView *> &p_views) {
	CanvasItemEditorView *view = get_focused_view(p_views);
	return view ? view->get_viewport_control() : nullptr;
}

void CanvasItemEditorViewRouting::set_cursor_shape_override(const Vector<CanvasItemEditorView *> &p_views, Control::CursorShape p_shape) {
	if (CanvasItemEditorView *view = get_focused_view(p_views)) {
		view->set_cursor_shape_override(p_shape);
	}
}

void CanvasItemEditorViewRouting::update_all_viewports(const Vector<CanvasItemEditorView *> &p_views) {
	for (CanvasItemEditorView *view : p_views) {
		if (!view) {
			continue;
		}
		view->update_viewport();
	}
}

SubViewport *CanvasItemEditorView::get_scene_viewport() const {
	if (scene_context) {
		return scene_context->get_viewport();
	}
	if (EditorNode::get_singleton()) {
		return EditorNode::get_singleton()->get_scene_root();
	}
	return nullptr;
}

Node *CanvasItemEditorView::get_edited_scene() const {
	if (scene_context) {
		return scene_context->get_scene_root_node();
	}
	if (EditorNode::get_singleton()) {
		return EditorNode::get_singleton()->get_edited_scene();
	}
	return nullptr;
}

EditorSelection *CanvasItemEditorView::get_editor_selection() const {
	if (scene_context) {
		return scene_context->get_selection();
	}
	if (editor) {
		return editor->editor_selection;
	}
	return nullptr;
}

void CanvasItemEditorView::bind_context(EditorSceneContext *p_context) {
	scene_context = p_context;
}

void CanvasItemEditorView::push_viewport_state() {
	SubViewport *scene_viewport = get_scene_viewport();
	ERR_FAIL_NULL(scene_viewport);

	CanvasItemEditorViewMath::update_canvas_transform(view_state);
	scene_viewport->set_global_canvas_transform(view_state.transform);
	scene_viewport->set_snap_controls_to_pixels(GLOBAL_GET("gui/common/snap_controls_to_pixels"));
	if (editor) {
		scene_viewport->set_oversampling_override(editor->auto_resampling_enabled ? view_state.zoom : 0.0);
	}
}

void CanvasItemEditorView::build_ui(Control *p_parent, bool p_register_primary_container) {
	ERR_FAIL_NULL(p_parent);

	plugin_forwarding_target = p_register_primary_container;

	ERR_FAIL_NULL(editor);

	viewport_scrollable = memnew(Control);
	p_parent->add_child(viewport_scrollable);
	viewport_scrollable->set_mouse_filter(Control::MOUSE_FILTER_PASS);
	viewport_scrollable->set_clip_contents(true);
	viewport_scrollable->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	viewport_scrollable->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	viewport_scrollable->connect(SceneStringName(draw), callable_mp(this, &CanvasItemEditorView::_update_scrollbars));

	scene_tree = memnew(SubViewportContainer);
	viewport_scrollable->add_child(scene_tree);
	scene_tree->set_stretch(true);
	scene_tree->set_anchors_and_offsets_preset(Control::PRESET_FULL_RECT);
	if (p_register_primary_container && EditorNode::get_singleton()) {
		EditorNode::get_singleton()->set_scene_viewport_container(scene_tree);
	}

	controls_vb = memnew(VBoxContainer);
	controls_vb->set_begin(Point2(5, 5));

	HBoxContainer *controls_hb = memnew(HBoxContainer);
	controls_vb->add_child(controls_hb);

	button_center_view = memnew(Button);
	controls_hb->add_child(button_center_view);
	button_center_view->set_flat(true);
	button_center_view->set_tooltip_text(TTR("Center View"));
	button_center_view->connect(SceneStringName(pressed), callable_mp(editor, &CanvasItemEditor::_popup_callback).bind(CanvasItemEditor::VIEW_CENTER_TO_SELECTION));

	zoom_widget = memnew(EditorZoomWidget);
	zoom_widget->set_anchors_and_offsets_preset(Control::PRESET_TOP_LEFT, Control::PRESET_MODE_MINSIZE, 2 * EDSCALE);
	zoom_widget->set_shortcut_context(editor);
	controls_hb->add_child(zoom_widget);
	zoom_widget->connect("zoom_changed", callable_mp(this, &CanvasItemEditorView::_update_zoom));

	EditorTranslationPreviewButton *translation_preview_button = memnew(EditorTranslationPreviewButton);
	translation_preview_button->set_flat(true);
	translation_preview_button->add_theme_constant_override("outline_size", Math::ceil(2 * EDSCALE));
	translation_preview_button->add_theme_color_override("font_outline_color", Color(0, 0, 0));
	translation_preview_button->add_theme_color_override(SceneStringName(font_color), Color(1, 1, 1));
	controls_hb->add_child(translation_preview_button);

	panner.instantiate();
	panner->set_callbacks(callable_mp(this, &CanvasItemEditorView::_pan_callback), callable_mp(this, &CanvasItemEditorView::_zoom_callback));

	viewport = memnew(CanvasItemEditorViewport(editor, this));
	viewport_scrollable->add_child(viewport);
	viewport->set_mouse_filter(Control::MOUSE_FILTER_PASS);
	viewport->set_anchors_and_offsets_preset(Control::PRESET_FULL_RECT);
	viewport->set_clip_contents(true);
	viewport->set_focus_mode(Control::FOCUS_ALL);
	viewport->connect(SceneStringName(draw), callable_mp(this, &CanvasItemEditorView::_draw_viewport));
	viewport->connect(SceneStringName(gui_input), callable_mp(this, &CanvasItemEditorView::_gui_input_viewport));
	viewport->connect(SceneStringName(focus_exited), callable_mp(panner.ptr(), &ViewPanner::release_pan_key));

	h_scroll = memnew(HScrollBar);
	viewport->add_child(h_scroll);
	h_scroll->connect(SceneStringName(value_changed), callable_mp(this, &CanvasItemEditorView::_update_scroll));
	h_scroll->hide();

	v_scroll = memnew(VScrollBar);
	viewport->add_child(v_scroll);
	v_scroll->connect(SceneStringName(value_changed), callable_mp(this, &CanvasItemEditorView::_update_scroll));
	v_scroll->hide();

	viewport->add_child(controls_vb);
}

void CanvasItemEditorView::active_scene_context_changed() {
	if (editor) {
		editor->editor_selection = EditorNode::get_singleton()->get_editor_selection();
	}
	push_viewport_state();
	update_viewport();
}

bool CanvasItemEditorView::_gui_input_rulers_and_guides(const Ref<InputEvent> &p_event) {
	EditorUndoRedoManager *undo_redo = EditorUndoRedoManager::get_singleton();
	Ref<InputEventMouseButton> b = p_event;
	Ref<InputEventMouseMotion> m = p_event;

	if (view_state.drag_type == CanvasItemEditorViewState::DRAG_NONE) {
		if (editor->show_guides && editor->show_rulers && get_edited_scene()) {
			Transform2D xform = viewport_scrollable->get_transform() * view_state.transform;
			// Retrieve the guide lists
			Array vguides = get_edited_scene()->get_meta("_edit_vertical_guides_", Array());
			Array hguides = get_edited_scene()->get_meta("_edit_horizontal_guides_", Array());

			// Hover over guides
			real_t minimum = 1e20;
			view_state.is_hovering_h_guide = false;
			view_state.is_hovering_v_guide = false;

			if (m.is_valid() && m->get_position().x < editor->ruler_width_scaled) {
				// Check if we are hovering an existing horizontal guide
				for (int i = 0; i < hguides.size(); i++) {
					if (Math::abs(xform.xform(Point2(0, hguides[i])).y - m->get_position().y) < MIN(minimum, 8)) {
						view_state.is_hovering_h_guide = true;
						view_state.is_hovering_v_guide = false;
						break;
					}
				}

			} else if (m.is_valid() && m->get_position().y < editor->ruler_width_scaled) {
				// Check if we are hovering an existing vertical guide
				for (int i = 0; i < vguides.size(); i++) {
					if (Math::abs(xform.xform(Point2(vguides[i], 0)).x - m->get_position().x) < MIN(minimum, 8)) {
						view_state.is_hovering_v_guide = true;
						view_state.is_hovering_h_guide = false;
						break;
					}
				}
			}

			// Start dragging a guide
			if (b.is_valid() && b->get_button_index() == MouseButton::LEFT && b->is_pressed()) {
				// Press button
				if (b->get_position().x < editor->ruler_width_scaled && b->get_position().y < editor->ruler_width_scaled) {
					// Drag a new double guide
					view_state.drag_type = CanvasItemEditorViewState::DRAG_DOUBLE_GUIDE;
					view_state.dragged_guide_index = -1;
					return true;
				} else if (b->get_position().x < editor->ruler_width_scaled) {
					// Check if we drag an existing horizontal guide
					view_state.dragged_guide_index = -1;
					for (int i = 0; i < hguides.size(); i++) {
						if (Math::abs(xform.xform(Point2(0, hguides[i])).y - b->get_position().y) < MIN(minimum, 8)) {
							view_state.dragged_guide_index = i;
						}
					}

					if (view_state.dragged_guide_index >= 0) {
						// Drag an existing horizontal guide
						view_state.drag_type = CanvasItemEditorViewState::DRAG_H_GUIDE;
					} else {
						// Drag a new vertical guide
						view_state.drag_type = CanvasItemEditorViewState::DRAG_V_GUIDE;
					}
					return true;
				} else if (b->get_position().y < editor->ruler_width_scaled) {
					// Check if we drag an existing vertical guide
					view_state.dragged_guide_index = -1;
					for (int i = 0; i < vguides.size(); i++) {
						if (Math::abs(xform.xform(Point2(vguides[i], 0)).x - b->get_position().x) < MIN(minimum, 8)) {
							view_state.dragged_guide_index = i;
						}
					}

					if (view_state.dragged_guide_index >= 0) {
						// Drag an existing vertical guide
						view_state.drag_type = CanvasItemEditorViewState::DRAG_V_GUIDE;
					} else {
						// Drag a new vertical guide
						view_state.drag_type = CanvasItemEditorViewState::DRAG_H_GUIDE;
					}
					view_state.drag_from = xform.affine_inverse().xform(b->get_position());
					return true;
				}
			}
		}
	}

	if (view_state.drag_type == CanvasItemEditorViewState::DRAG_DOUBLE_GUIDE || view_state.drag_type == CanvasItemEditorViewState::DRAG_V_GUIDE || view_state.drag_type == CanvasItemEditorViewState::DRAG_H_GUIDE) {
		// Move the guide
		if (m.is_valid()) {
			Transform2D xform = viewport_scrollable->get_transform() * view_state.transform;
			view_state.drag_to = xform.affine_inverse().xform(m->get_position());

			view_state.dragged_guide_pos = xform.xform(editor->snap_point(view_state.drag_to, CanvasItemEditor::SNAP_GRID | CanvasItemEditor::SNAP_PIXEL | CanvasItemEditor::SNAP_OTHER_NODES));
			viewport->queue_redraw();
			return true;
		}

		// Release confirms the guide move
		if (b.is_valid() && b->get_button_index() == MouseButton::LEFT && !b->is_pressed()) {
			if (editor->show_guides && get_edited_scene()) {
				Transform2D xform = viewport_scrollable->get_transform() * view_state.transform;

				// Retrieve the guide lists
				Array vguides = get_edited_scene()->get_meta("_edit_vertical_guides_", Array());
				Array hguides = get_edited_scene()->get_meta("_edit_horizontal_guides_", Array());

				Point2 edited = editor->snap_point(xform.affine_inverse().xform(b->get_position()), CanvasItemEditor::SNAP_GRID | CanvasItemEditor::SNAP_PIXEL | CanvasItemEditor::SNAP_OTHER_NODES);
				if (view_state.drag_type == CanvasItemEditorViewState::DRAG_V_GUIDE) {
					Array prev_vguides = vguides.duplicate();
					if (b->get_position().x > editor->ruler_width_scaled) {
						// Adds a new vertical guide
						if (view_state.dragged_guide_index >= 0) {
							vguides[view_state.dragged_guide_index] = edited.x;
							undo_redo->create_action(TTR("Move Vertical Guide"));
							undo_redo->add_do_method(get_edited_scene(), "set_meta", "_edit_vertical_guides_", vguides);
							undo_redo->add_undo_method(get_edited_scene(), "set_meta", "_edit_vertical_guides_", prev_vguides);
							undo_redo->add_undo_method(viewport, "queue_redraw");
							undo_redo->commit_action();
						} else {
							vguides.push_back(edited.x);
							undo_redo->create_action(TTR("Create Vertical Guide"));
							undo_redo->add_do_method(get_edited_scene(), "set_meta", "_edit_vertical_guides_", vguides);
							if (prev_vguides.is_empty()) {
								undo_redo->add_undo_method(get_edited_scene(), "remove_meta", "_edit_vertical_guides_");
							} else {
								undo_redo->add_undo_method(get_edited_scene(), "set_meta", "_edit_vertical_guides_", prev_vguides);
							}
							undo_redo->add_undo_method(viewport, "queue_redraw");
							undo_redo->commit_action();
						}
					} else {
						if (view_state.dragged_guide_index >= 0) {
							vguides.remove_at(view_state.dragged_guide_index);
							undo_redo->create_action(TTR("Remove Vertical Guide"));
							if (vguides.is_empty()) {
								undo_redo->add_do_method(get_edited_scene(), "remove_meta", "_edit_vertical_guides_");
							} else {
								undo_redo->add_do_method(get_edited_scene(), "set_meta", "_edit_vertical_guides_", vguides);
							}
							undo_redo->add_undo_method(get_edited_scene(), "set_meta", "_edit_vertical_guides_", prev_vguides);
							undo_redo->add_undo_method(viewport, "queue_redraw");
							undo_redo->commit_action();
						}
					}
				} else if (view_state.drag_type == CanvasItemEditorViewState::DRAG_H_GUIDE) {
					Array prev_hguides = hguides.duplicate();
					if (b->get_position().y > editor->ruler_width_scaled) {
						// Adds a new horizontal guide
						if (view_state.dragged_guide_index >= 0) {
							hguides[view_state.dragged_guide_index] = edited.y;
							undo_redo->create_action(TTR("Move Horizontal Guide"));
							undo_redo->add_do_method(get_edited_scene(), "set_meta", "_edit_horizontal_guides_", hguides);
							undo_redo->add_undo_method(get_edited_scene(), "set_meta", "_edit_horizontal_guides_", prev_hguides);
							undo_redo->add_undo_method(viewport, "queue_redraw");
							undo_redo->commit_action();
						} else {
							hguides.push_back(edited.y);
							undo_redo->create_action(TTR("Create Horizontal Guide"));
							undo_redo->add_do_method(get_edited_scene(), "set_meta", "_edit_horizontal_guides_", hguides);
							if (prev_hguides.is_empty()) {
								undo_redo->add_undo_method(get_edited_scene(), "remove_meta", "_edit_horizontal_guides_");
							} else {
								undo_redo->add_undo_method(get_edited_scene(), "set_meta", "_edit_horizontal_guides_", prev_hguides);
							}
							undo_redo->add_undo_method(viewport, "queue_redraw");
							undo_redo->commit_action();
						}
					} else {
						if (view_state.dragged_guide_index >= 0) {
							hguides.remove_at(view_state.dragged_guide_index);
							undo_redo->create_action(TTR("Remove Horizontal Guide"));
							if (hguides.is_empty()) {
								undo_redo->add_do_method(get_edited_scene(), "remove_meta", "_edit_horizontal_guides_");
							} else {
								undo_redo->add_do_method(get_edited_scene(), "set_meta", "_edit_horizontal_guides_", hguides);
							}
							undo_redo->add_undo_method(get_edited_scene(), "set_meta", "_edit_horizontal_guides_", prev_hguides);
							undo_redo->add_undo_method(viewport, "queue_redraw");
							undo_redo->commit_action();
						}
					}
				} else if (view_state.drag_type == CanvasItemEditorViewState::DRAG_DOUBLE_GUIDE) {
					Array prev_hguides = hguides.duplicate();
					Array prev_vguides = vguides.duplicate();
					if (b->get_position().x > editor->ruler_width_scaled && b->get_position().y > editor->ruler_width_scaled) {
						// Adds a new horizontal guide a new vertical guide
						vguides.push_back(edited.x);
						hguides.push_back(edited.y);
						undo_redo->create_action(TTR("Create Horizontal and Vertical Guides"));
						undo_redo->add_do_method(get_edited_scene(), "set_meta", "_edit_vertical_guides_", vguides);
						undo_redo->add_do_method(get_edited_scene(), "set_meta", "_edit_horizontal_guides_", hguides);
						if (prev_vguides.is_empty()) {
							undo_redo->add_undo_method(get_edited_scene(), "remove_meta", "_edit_vertical_guides_");
						} else {
							undo_redo->add_undo_method(get_edited_scene(), "set_meta", "_edit_vertical_guides_", prev_vguides);
						}
						if (prev_hguides.is_empty()) {
							undo_redo->add_undo_method(get_edited_scene(), "remove_meta", "_edit_horizontal_guides_");
						} else {
							undo_redo->add_undo_method(get_edited_scene(), "set_meta", "_edit_horizontal_guides_", prev_hguides);
						}
						undo_redo->add_undo_method(viewport, "queue_redraw");
						undo_redo->commit_action();
					}
				}
			}
			view_state.snap_target[0] = CanvasItemEditorViewState::SNAP_TARGET_NONE;
			view_state.snap_target[1] = CanvasItemEditorViewState::SNAP_TARGET_NONE;
			editor->_reset_drag();
			viewport->queue_redraw();
			return true;
		}
	}
	return false;
}

bool CanvasItemEditorView::_gui_input_zoom_or_pan(const Ref<InputEvent> &p_event, bool p_already_accepted) {
	panner->set_force_drag(editor->tool == CanvasItemEditor::TOOL_PAN);
	bool panner_active = panner->gui_input(p_event, viewport->get_global_rect());
	if (panner->is_panning() != view_state.pan_pressed) {
		view_state.pan_pressed = panner->is_panning();
		_update_cursor();
	}

	if (panner_active) {
		return true;
	}

	Ref<InputEventKey> k = p_event;
	if (k.is_valid()) {
		if (k->is_pressed()) {
			if (ED_IS_SHORTCUT("canvas_item_editor/zoom_3.125_percent", p_event)) {
				_shortcut_zoom_set(1.0 / 32.0);
			} else if (ED_IS_SHORTCUT("canvas_item_editor/zoom_6.25_percent", p_event)) {
				_shortcut_zoom_set(1.0 / 16.0);
			} else if (ED_IS_SHORTCUT("canvas_item_editor/zoom_12.5_percent", p_event)) {
				_shortcut_zoom_set(1.0 / 8.0);
			} else if (ED_IS_SHORTCUT("canvas_item_editor/zoom_25_percent", p_event)) {
				_shortcut_zoom_set(1.0 / 4.0);
			} else if (ED_IS_SHORTCUT("canvas_item_editor/zoom_50_percent", p_event)) {
				_shortcut_zoom_set(1.0 / 2.0);
			} else if (ED_IS_SHORTCUT("canvas_item_editor/zoom_100_percent", p_event)) {
				_shortcut_zoom_set(1.0);
			} else if (ED_IS_SHORTCUT("canvas_item_editor/zoom_200_percent", p_event)) {
				_shortcut_zoom_set(2.0);
			} else if (ED_IS_SHORTCUT("canvas_item_editor/zoom_400_percent", p_event)) {
				_shortcut_zoom_set(4.0);
			} else if (ED_IS_SHORTCUT("canvas_item_editor/zoom_800_percent", p_event)) {
				_shortcut_zoom_set(8.0);
			} else if (ED_IS_SHORTCUT("canvas_item_editor/zoom_1600_percent", p_event)) {
				_shortcut_zoom_set(16.0);
			}
		}
	}

	return false;
}

void CanvasItemEditorView::_pan_callback(Vector2 p_scroll_vec, Ref<InputEvent> p_event) {
	CanvasItemEditorViewMath::apply_pan(view_state, p_scroll_vec);
	update_viewport();
}

void CanvasItemEditorView::_zoom_callback(float p_zoom_factor, Vector2 p_origin, Ref<InputEvent> p_event) {
	Ref<InputEventMouseButton> mb = p_event;
	if (mb.is_valid()) {
		// Special behavior for scroll events, as the zoom_by_increment method can smartly end up on powers of two.
		int increment = p_zoom_factor > 1.0 ? 1 : -1;
		bool by_integer = mb->is_alt_pressed();

		if (EDITOR_GET("editors/2d/use_integer_zoom_by_default")) {
			by_integer = !by_integer;
		}

		zoom_widget->set_zoom_by_increments(increment, by_integer);
	} else {
		zoom_widget->set_zoom(zoom_widget->get_zoom() * p_zoom_factor);
	}

	_zoom_on_position(zoom_widget->get_zoom(), p_origin);
}

bool CanvasItemEditorView::_gui_input_pivot(const Ref<InputEvent> &p_event) {
	Ref<InputEventMouseMotion> m = p_event;
	Ref<InputEventMouseButton> b = p_event;
	Ref<InputEventKey> k = p_event;

	// Drag the pivot (in pivot mode / with V key)
	if (view_state.drag_type == CanvasItemEditorViewState::DRAG_NONE) {
		bool move_temp_pivot = ((b.is_valid() && b->is_shift_pressed()) || (k.is_valid() && k->is_shift_pressed()));

		if ((b.is_valid() && b->is_pressed() && b->get_button_index() == MouseButton::LEFT && editor->tool == CanvasItemEditor::TOOL_EDIT_PIVOT) ||
				(k.is_valid() && k->is_pressed() && !k->is_echo() && k->get_keycode() == Key::V && editor->tool == CanvasItemEditor::TOOL_SELECT && (k->get_modifiers_mask().is_empty() || move_temp_pivot))) {
			List<CanvasItem *> selection = editor->_get_edited_canvas_items();

			// Filters the selection with nodes that allow setting the pivot
			view_state.drag_selection = List<CanvasItem *>();
			for (CanvasItem *ci : selection) {
				if (ci->_edit_use_pivot() || move_temp_pivot) {
					view_state.drag_selection.push_back(ci);
				}
			}

			// Start dragging if we still have nodes
			if (view_state.drag_selection.size() > 0) {
				Vector2 event_pos = (b.is_valid()) ? b->get_position() : viewport->get_local_mouse_position();

				if (move_temp_pivot) {
					view_state.drag_type = CanvasItemEditorViewState::DRAG_TEMP_PIVOT;
					view_state.temp_pivot = view_state.transform.affine_inverse().xform(event_pos);
					viewport->queue_redraw();
					return true;
				}

				editor->_save_canvas_item_state(view_state.drag_selection);
				view_state.drag_from = view_state.transform.affine_inverse().xform(event_pos);
				Vector2 new_pos;
				if (view_state.drag_selection.size() == 1) {
					new_pos = editor->snap_point(view_state.drag_from, CanvasItemEditor::SNAP_NODE_SIDES | CanvasItemEditor::SNAP_NODE_CENTER | CanvasItemEditor::SNAP_NODE_ANCHORS | CanvasItemEditor::SNAP_OTHER_NODES | CanvasItemEditor::SNAP_GRID | CanvasItemEditor::SNAP_PIXEL, 0, view_state.drag_selection.front()->get());
				} else {
					new_pos = editor->snap_point(view_state.drag_from, CanvasItemEditor::SNAP_OTHER_NODES | CanvasItemEditor::SNAP_GRID | CanvasItemEditor::SNAP_PIXEL, 0, nullptr, view_state.drag_selection);
				}
				for (CanvasItem *ci : view_state.drag_selection) {
					ci->_edit_set_pivot(ci->get_screen_transform().affine_inverse().xform(new_pos));
				}

				view_state.drag_type = CanvasItemEditorViewState::DRAG_PIVOT;
			}
			return true;
		}
	}

	if (view_state.drag_type == CanvasItemEditorViewState::DRAG_PIVOT) {
		// Move the pivot
		if (m.is_valid()) {
			view_state.drag_to = view_state.transform.affine_inverse().xform(m->get_position());
			editor->_restore_canvas_item_state(view_state.drag_selection);
			Vector2 new_pos;
			if (view_state.drag_selection.size() == 1) {
				new_pos = editor->snap_point(view_state.drag_to, CanvasItemEditor::SNAP_NODE_SIDES | CanvasItemEditor::SNAP_NODE_CENTER | CanvasItemEditor::SNAP_NODE_ANCHORS | CanvasItemEditor::SNAP_OTHER_NODES | CanvasItemEditor::SNAP_GRID | CanvasItemEditor::SNAP_PIXEL, 0, view_state.drag_selection.front()->get());
			} else {
				new_pos = editor->snap_point(view_state.drag_to, CanvasItemEditor::SNAP_OTHER_NODES | CanvasItemEditor::SNAP_GRID | CanvasItemEditor::SNAP_PIXEL);
			}
			for (CanvasItem *ci : view_state.drag_selection) {
				ci->_edit_set_pivot(ci->get_screen_transform().affine_inverse().xform(new_pos));
			}
			return true;
		}

		// Confirm the pivot move
		if (view_state.drag_selection.size() >= 1 &&
				((b.is_valid() && !b->is_pressed() && b->get_button_index() == MouseButton::LEFT && editor->tool == CanvasItemEditor::TOOL_EDIT_PIVOT) ||
						(k.is_valid() && !k->is_pressed() && k->get_keycode() == Key::V))) {
			_commit_drag();
			view_state.snap_target[0] = CanvasItemEditorViewState::SNAP_TARGET_NONE;
			view_state.snap_target[1] = CanvasItemEditorViewState::SNAP_TARGET_NONE;
			return true;
		}

		// Cancel a drag
		if (ED_IS_SHORTCUT("canvas_item_editor/cancel_transform", p_event) || (b.is_valid() && b->get_button_index() == MouseButton::RIGHT && b->is_pressed())) {
			editor->_restore_canvas_item_state(view_state.drag_selection);
			view_state.snap_target[0] = CanvasItemEditorViewState::SNAP_TARGET_NONE;
			view_state.snap_target[1] = CanvasItemEditorViewState::SNAP_TARGET_NONE;
			editor->_reset_drag();
			viewport->queue_redraw();
			return true;
		}
	}

	if (view_state.drag_type == CanvasItemEditorViewState::DRAG_TEMP_PIVOT) {
		if (m.is_valid()) {
			view_state.temp_pivot = view_state.transform.affine_inverse().xform(m->get_position());
			viewport->queue_redraw();
			return true;
		}

		if ((b.is_valid() && !b->is_pressed() && b->get_button_index() == MouseButton::LEFT && editor->tool == CanvasItemEditor::TOOL_EDIT_PIVOT) ||
				(k.is_valid() && !k->is_pressed() && k->get_keycode() == Key::V)) {
			view_state.drag_type = CanvasItemEditorViewState::DRAG_NONE;
			return true;
		}
	}
	return false;
}

bool CanvasItemEditorView::_gui_input_rotate(const Ref<InputEvent> &p_event) {
	Ref<InputEventMouseButton> b = p_event;
	Ref<InputEventMouseMotion> m = p_event;

	// Start rotation
	if (view_state.drag_type == CanvasItemEditorViewState::DRAG_NONE) {
		if (b.is_valid() && b->get_button_index() == MouseButton::LEFT && b->is_pressed()) {
			if ((b->is_command_or_control_pressed() && !b->is_alt_pressed() && editor->tool == CanvasItemEditor::TOOL_SELECT) || editor->tool == CanvasItemEditor::TOOL_ROTATE) {
				bool has_locked_items = false;
				List<CanvasItem *> selection = editor->_get_edited_canvas_items(false, true, &has_locked_items);

				// Remove not movable nodes
				for (List<CanvasItem *>::Element *E = selection.front(); E;) {
					List<CanvasItem *>::Element *N = E->next();
					if (!editor->_is_node_movable(E->get(), true)) {
						selection.erase(E);
					}
					E = N;
				}

				view_state.drag_selection = selection;
				if (view_state.drag_selection.size() > 0) {
					view_state.drag_type = CanvasItemEditorViewState::DRAG_ROTATE;
					view_state.drag_from = view_state.transform.affine_inverse().xform(b->get_position());
					CanvasItem *ci = view_state.drag_selection.front()->get();
					if (!Math::is_inf(view_state.temp_pivot.x) || !Math::is_inf(view_state.temp_pivot.y)) {
						view_state.drag_rotation_center = view_state.temp_pivot;
					} else if (ci->_edit_use_pivot()) {
						view_state.drag_rotation_center = ci->get_screen_transform().xform(ci->_edit_get_pivot());
					} else {
						view_state.drag_rotation_center = ci->get_screen_transform().get_origin();
					}
					editor->_save_canvas_item_state(view_state.drag_selection);
					return true;
				} else {
					if (has_locked_items) {
						EditorToaster::get_singleton()->popup_str(TTR(editor->locked_transform_warning), EditorToaster::SEVERITY_WARNING);
					}
					return has_locked_items;
				}
			}
		}
	}

	if (view_state.drag_type == CanvasItemEditorViewState::DRAG_ROTATE) {
		// Rotate the node
		if (m.is_valid()) {
			editor->_restore_canvas_item_state(view_state.drag_selection);
			for (CanvasItem *ci : view_state.drag_selection) {
				view_state.drag_to = view_state.transform.affine_inverse().xform(m->get_position());
				//Rotate the opposite way if the canvas item's compounded scale has an uneven number of negative elements
				bool opposite = (ci->get_global_transform().get_scale().sign().dot(ci->get_transform().get_scale().sign()) == 0);
				real_t prev_rotation = ci->_edit_get_rotation();
				real_t new_rotation = editor->snap_angle(ci->_edit_get_rotation() + (opposite ? -1 : 1) * (view_state.drag_from - view_state.drag_rotation_center).angle_to(view_state.drag_to - view_state.drag_rotation_center), prev_rotation);

				ci->_edit_set_rotation(new_rotation);
				if (!Math::is_inf(view_state.temp_pivot.x) || !Math::is_inf(view_state.temp_pivot.y)) {
					Transform2D xform = ci->get_screen_transform() * ci->get_transform().affine_inverse();
					Vector2 radius = xform.xform(ci->_edit_get_position()) - view_state.temp_pivot;
					radius = radius.rotated(new_rotation - prev_rotation);
					ci->_edit_set_position(xform.affine_inverse().xform(view_state.temp_pivot + radius));
				}
				viewport->queue_redraw();
			}
			return true;
		}

		// Confirms the node rotation
		if (b.is_valid() && b->get_button_index() == MouseButton::LEFT && !b->is_pressed()) {
			_commit_drag();
			return true;
		}

		// Cancel a drag
		if (ED_IS_SHORTCUT("canvas_item_editor/cancel_transform", p_event) || (b.is_valid() && b->get_button_index() == MouseButton::RIGHT && b->is_pressed())) {
			editor->_restore_canvas_item_state(view_state.drag_selection);
			editor->_reset_drag();
			viewport->queue_redraw();
			return true;
		}
	}
	return false;
}

bool CanvasItemEditorView::_gui_input_open_scene_on_double_click(const Ref<InputEvent> &p_event) {
	Ref<InputEventMouseButton> b = p_event;

	// Open a sub-scene on double-click
	if (b.is_valid() && b->get_button_index() == MouseButton::LEFT && b->is_pressed() && b->is_double_click() && editor->tool == CanvasItemEditor::TOOL_SELECT) {
		List<CanvasItem *> selection = editor->_get_edited_canvas_items();
		if (selection.size() == 1) {
			CanvasItem *ci = selection.front()->get();
			if (ci->is_instance() && ci != get_edited_scene()) {
				EditorNode::get_singleton()->load_scene(ci->get_scene_file_path());
				return true;
			}
		}
	}
	return false;
}

bool CanvasItemEditorView::_gui_input_anchors(const Ref<InputEvent> &p_event) {
	Ref<InputEventMouseButton> b = p_event;
	Ref<InputEventMouseMotion> m = p_event;

	// Starts anchor dragging if needed
	if (view_state.drag_type == CanvasItemEditorViewState::DRAG_NONE) {
		if (b.is_valid() && b->get_button_index() == MouseButton::LEFT && b->is_pressed() && editor->tool == CanvasItemEditor::TOOL_SELECT) {
			List<CanvasItem *> selection = editor->_get_edited_canvas_items();
			if (selection.size() == 1) {
				Control *control = Object::cast_to<Control>(selection.front()->get());
				if (control && editor->_is_node_movable(control)) {
					Vector2 anchor_pos[4];
					anchor_pos[0] = Vector2(control->get_anchor(SIDE_LEFT), control->get_anchor(SIDE_TOP));
					anchor_pos[1] = Vector2(control->get_anchor(SIDE_RIGHT), control->get_anchor(SIDE_TOP));
					anchor_pos[2] = Vector2(control->get_anchor(SIDE_RIGHT), control->get_anchor(SIDE_BOTTOM));
					anchor_pos[3] = Vector2(control->get_anchor(SIDE_LEFT), control->get_anchor(SIDE_BOTTOM));

					Rect2 anchor_rects[4];
					for (int i = 0; i < 4; i++) {
						anchor_pos[i] = (view_state.transform * control->get_screen_transform()).xform(editor->_anchor_to_position(control, anchor_pos[i]));
						anchor_rects[i] = Rect2(anchor_pos[i], editor->anchor_handle->get_size());
						if (control->is_layout_rtl()) {
							anchor_rects[i].position -= editor->anchor_handle->get_size() * Vector2(real_t(i == 1 || i == 2), real_t(i <= 1));
						} else {
							anchor_rects[i].position -= editor->anchor_handle->get_size() * Vector2(real_t(i == 0 || i == 3), real_t(i <= 1));
						}
					}

					const CanvasItemEditorViewState::DragType dragger[] = {
						CanvasItemEditorViewState::DRAG_ANCHOR_TOP_LEFT,
						CanvasItemEditorViewState::DRAG_ANCHOR_TOP_RIGHT,
						CanvasItemEditorViewState::DRAG_ANCHOR_BOTTOM_RIGHT,
						CanvasItemEditorViewState::DRAG_ANCHOR_BOTTOM_LEFT,
					};

					for (int i = 0; i < 4; i++) {
						if (anchor_rects[i].has_point(b->get_position())) {
							if ((anchor_pos[0] == anchor_pos[2]) && (anchor_pos[0].distance_to(b->get_position()) < editor->anchor_handle->get_size().length() / 3.0)) {
								view_state.drag_type = CanvasItemEditorViewState::DRAG_ANCHOR_ALL;
							} else {
								view_state.drag_type = dragger[i];
							}
							view_state.drag_from = view_state.transform.affine_inverse().xform(b->get_position());
							view_state.drag_selection = List<CanvasItem *>();
							view_state.drag_selection.push_back(control);
							editor->_save_canvas_item_state(view_state.drag_selection);
							return true;
						}
					}
				}
			}
		}
	}

	if (view_state.drag_type == CanvasItemEditorViewState::DRAG_ANCHOR_TOP_LEFT || view_state.drag_type == CanvasItemEditorViewState::DRAG_ANCHOR_TOP_RIGHT || view_state.drag_type == CanvasItemEditorViewState::DRAG_ANCHOR_BOTTOM_RIGHT || view_state.drag_type == CanvasItemEditorViewState::DRAG_ANCHOR_BOTTOM_LEFT || view_state.drag_type == CanvasItemEditorViewState::DRAG_ANCHOR_ALL) {
		// Drag the anchor
		if (m.is_valid()) {
			editor->_restore_canvas_item_state(view_state.drag_selection);
			Control *control = Object::cast_to<Control>(view_state.drag_selection.front()->get());

			view_state.drag_to = view_state.transform.affine_inverse().xform(m->get_position());

			Transform2D xform = control->get_screen_transform().affine_inverse();

			Point2 previous_anchor;
			previous_anchor.x = (view_state.drag_type == CanvasItemEditorViewState::DRAG_ANCHOR_TOP_LEFT || view_state.drag_type == CanvasItemEditorViewState::DRAG_ANCHOR_BOTTOM_LEFT) ? control->get_anchor(SIDE_LEFT) : control->get_anchor(SIDE_RIGHT);
			previous_anchor.y = (view_state.drag_type == CanvasItemEditorViewState::DRAG_ANCHOR_TOP_LEFT || view_state.drag_type == CanvasItemEditorViewState::DRAG_ANCHOR_TOP_RIGHT) ? control->get_anchor(SIDE_TOP) : control->get_anchor(SIDE_BOTTOM);
			previous_anchor = xform.affine_inverse().xform(editor->_anchor_to_position(control, previous_anchor));

			Vector2 new_anchor = xform.xform(editor->snap_point(previous_anchor + (view_state.drag_to - view_state.drag_from), CanvasItemEditor::SNAP_GRID | CanvasItemEditor::SNAP_OTHER_NODES, CanvasItemEditor::SNAP_NODE_PARENT | CanvasItemEditor::SNAP_NODE_SIDES | CanvasItemEditor::SNAP_NODE_CENTER, control));
			new_anchor = editor->_position_to_anchor(control, new_anchor).snappedf(0.001);

			bool use_single_axis = m->is_shift_pressed();
			Vector2 drag_vector = xform.xform(view_state.drag_to) - xform.xform(view_state.drag_from);
			bool use_y = Math::abs(drag_vector.y) > Math::abs(drag_vector.x);

			switch (view_state.drag_type) {
				case CanvasItemEditorViewState::DRAG_ANCHOR_TOP_LEFT:
					if (!use_single_axis || !use_y) {
						control->set_anchor(SIDE_LEFT, new_anchor.x, false, false);
					}
					if (!use_single_axis || use_y) {
						control->set_anchor(SIDE_TOP, new_anchor.y, false, false);
					}
					break;
				case CanvasItemEditorViewState::DRAG_ANCHOR_TOP_RIGHT:
					if (!use_single_axis || !use_y) {
						control->set_anchor(SIDE_RIGHT, new_anchor.x, false, false);
					}
					if (!use_single_axis || use_y) {
						control->set_anchor(SIDE_TOP, new_anchor.y, false, false);
					}
					break;
				case CanvasItemEditorViewState::DRAG_ANCHOR_BOTTOM_RIGHT:
					if (!use_single_axis || !use_y) {
						control->set_anchor(SIDE_RIGHT, new_anchor.x, false, false);
					}
					if (!use_single_axis || use_y) {
						control->set_anchor(SIDE_BOTTOM, new_anchor.y, false, false);
					}
					break;
				case CanvasItemEditorViewState::DRAG_ANCHOR_BOTTOM_LEFT:
					if (!use_single_axis || !use_y) {
						control->set_anchor(SIDE_LEFT, new_anchor.x, false, false);
					}
					if (!use_single_axis || use_y) {
						control->set_anchor(SIDE_BOTTOM, new_anchor.y, false, false);
					}
					break;
				case CanvasItemEditorViewState::DRAG_ANCHOR_ALL:
					if (!use_single_axis || !use_y) {
						control->set_anchor(SIDE_LEFT, new_anchor.x, false, true);
						control->set_anchor(SIDE_RIGHT, new_anchor.x, false, true);
					}
					if (!use_single_axis || use_y) {
						control->set_anchor(SIDE_TOP, new_anchor.y, false, true);
						control->set_anchor(SIDE_BOTTOM, new_anchor.y, false, true);
					}
					break;
				default:
					break;
			}
			return true;
		}

		// Confirms new anchor position
		if (view_state.drag_selection.size() >= 1 && b.is_valid() && b->get_button_index() == MouseButton::LEFT && !b->is_pressed()) {
			_commit_drag();
			return true;
		}

		// Cancel a drag
		if (ED_IS_SHORTCUT("canvas_item_editor/cancel_transform", p_event) || (b.is_valid() && b->get_button_index() == MouseButton::RIGHT && b->is_pressed())) {
			editor->_restore_canvas_item_state(view_state.drag_selection);
			view_state.snap_target[0] = CanvasItemEditorViewState::SNAP_TARGET_NONE;
			view_state.snap_target[1] = CanvasItemEditorViewState::SNAP_TARGET_NONE;
			editor->_reset_drag();
			viewport->queue_redraw();
			return true;
		}
	}
	return false;
}

bool CanvasItemEditorView::_gui_input_resize(const Ref<InputEvent> &p_event) {
	Ref<InputEventMouseButton> b = p_event;
	Ref<InputEventMouseMotion> m = p_event;

	// Drag resize handles
	if (view_state.drag_type == CanvasItemEditorViewState::DRAG_NONE) {
		if (b.is_valid() && b->get_button_index() == MouseButton::LEFT && b->is_pressed() && editor->tool == CanvasItemEditor::TOOL_SELECT) {
			List<CanvasItem *> selection = editor->_get_edited_canvas_items();
			if (selection.size() == 1) {
				CanvasItem *ci = selection.front()->get();
				if (ci->_edit_use_rect() && editor->_is_node_movable(ci)) {
					Rect2 rect = ci->_edit_get_rect();
					Transform2D xform = view_state.transform * ci->get_screen_transform();

					const Vector2 endpoints[4] = {
						xform.xform(rect.position),
						xform.xform(rect.position + Vector2(rect.size.x, 0)),
						xform.xform(rect.position + rect.size),
						xform.xform(rect.position + Vector2(0, rect.size.y))
					};

					const CanvasItemEditorViewState::DragType dragger[] = {
						CanvasItemEditorViewState::DRAG_TOP_LEFT,
						CanvasItemEditorViewState::DRAG_TOP,
						CanvasItemEditorViewState::DRAG_TOP_RIGHT,
						CanvasItemEditorViewState::DRAG_RIGHT,
						CanvasItemEditorViewState::DRAG_BOTTOM_RIGHT,
						CanvasItemEditorViewState::DRAG_BOTTOM,
						CanvasItemEditorViewState::DRAG_BOTTOM_LEFT,
						CanvasItemEditorViewState::DRAG_LEFT
					};

					CanvasItemEditorViewState::DragType resize_drag = CanvasItemEditorViewState::DRAG_NONE;
					real_t radius = (editor->select_handle->get_size().width / 2) * 1.5;

					for (int i = 0; i < 4; i++) {
						int prev = (i + 3) % 4;
						int next = (i + 1) % 4;

						Vector2 ofs = ((endpoints[i] - endpoints[prev]).normalized() + ((endpoints[i] - endpoints[next]).normalized())).normalized();
						ofs *= (editor->select_handle->get_size().width / 2);
						ofs += endpoints[i];
						if (ofs.distance_to(b->get_position()) < radius) {
							resize_drag = dragger[i * 2];
						}

						ofs = (endpoints[i] + endpoints[next]) / 2;
						ofs += (endpoints[next] - endpoints[i]).orthogonal().normalized() * (editor->select_handle->get_size().width / 2);
						if (ofs.distance_to(b->get_position()) < radius) {
							resize_drag = dragger[i * 2 + 1];
						}
					}

					if (resize_drag != CanvasItemEditorViewState::DRAG_NONE) {
						view_state.drag_type = resize_drag;
						view_state.drag_from = view_state.transform.affine_inverse().xform(b->get_position());
						view_state.drag_selection = List<CanvasItem *>();
						view_state.drag_selection.push_back(ci);
						editor->_save_canvas_item_state(view_state.drag_selection);
						return true;
					}
				}
			}
		}
	}

	if (view_state.drag_type == CanvasItemEditorViewState::DRAG_LEFT || view_state.drag_type == CanvasItemEditorViewState::DRAG_RIGHT || view_state.drag_type == CanvasItemEditorViewState::DRAG_TOP || view_state.drag_type == CanvasItemEditorViewState::DRAG_BOTTOM ||
			view_state.drag_type == CanvasItemEditorViewState::DRAG_TOP_LEFT || view_state.drag_type == CanvasItemEditorViewState::DRAG_TOP_RIGHT || view_state.drag_type == CanvasItemEditorViewState::DRAG_BOTTOM_LEFT || view_state.drag_type == CanvasItemEditorViewState::DRAG_BOTTOM_RIGHT) {
		// Resize the node
		if (m.is_valid()) {
			CanvasItem *ci = view_state.drag_selection.front()->get();
			CanvasItemEditorSelectedItem *se = editor->editor_selection->get_node_editor_data<CanvasItemEditorSelectedItem>(ci);
			//Reset state
			ci->_edit_set_state(se->undo_state);

			bool uniform = m->is_shift_pressed();
			bool symmetric = m->is_alt_pressed();

			Rect2 local_rect = ci->_edit_get_rect();
			real_t aspect = local_rect.has_area() ? (local_rect.get_size().y / local_rect.get_size().x) : (local_rect.get_size().y + 1.0) / (local_rect.get_size().x + 1.0);
			Point2 current_begin = local_rect.get_position();
			Point2 current_end = local_rect.get_position() + local_rect.get_size();
			Point2 max_begin = (symmetric) ? (current_begin + current_end - ci->_edit_get_minimum_size()) / 2.0 : current_end - ci->_edit_get_minimum_size();
			Point2 min_end = (symmetric) ? (current_begin + current_end + ci->_edit_get_minimum_size()) / 2.0 : current_begin + ci->_edit_get_minimum_size();
			Point2 center = (current_begin + current_end) / 2.0;

			view_state.drag_to = view_state.transform.affine_inverse().xform(m->get_position());

			Transform2D xform = ci->get_screen_transform();

			Point2 drag_to_snapped_begin;
			Point2 drag_to_snapped_end;

			drag_to_snapped_end = editor->snap_point(xform.xform(current_end) + (view_state.drag_to - view_state.drag_from), CanvasItemEditor::SNAP_NODE_ANCHORS | CanvasItemEditor::SNAP_NODE_PARENT | CanvasItemEditor::SNAP_OTHER_NODES | CanvasItemEditor::SNAP_GRID | CanvasItemEditor::SNAP_PIXEL, 0, ci);
			drag_to_snapped_begin = editor->snap_point(xform.xform(current_begin) + (view_state.drag_to - view_state.drag_from), CanvasItemEditor::SNAP_NODE_ANCHORS | CanvasItemEditor::SNAP_NODE_PARENT | CanvasItemEditor::SNAP_OTHER_NODES | CanvasItemEditor::SNAP_GRID | CanvasItemEditor::SNAP_PIXEL, 0, ci);

			Point2 drag_begin = xform.affine_inverse().xform(drag_to_snapped_begin);
			Point2 drag_end = xform.affine_inverse().xform(drag_to_snapped_end);

			// Horizontal resize
			if (view_state.drag_type == CanvasItemEditorViewState::DRAG_LEFT || view_state.drag_type == CanvasItemEditorViewState::DRAG_TOP_LEFT || view_state.drag_type == CanvasItemEditorViewState::DRAG_BOTTOM_LEFT) {
				current_begin.x = MIN(drag_begin.x, max_begin.x);
			} else if (view_state.drag_type == CanvasItemEditorViewState::DRAG_RIGHT || view_state.drag_type == CanvasItemEditorViewState::DRAG_TOP_RIGHT || view_state.drag_type == CanvasItemEditorViewState::DRAG_BOTTOM_RIGHT) {
				current_end.x = MAX(drag_end.x, min_end.x);
			}

			// Vertical resize
			if (view_state.drag_type == CanvasItemEditorViewState::DRAG_TOP || view_state.drag_type == CanvasItemEditorViewState::DRAG_TOP_LEFT || view_state.drag_type == CanvasItemEditorViewState::DRAG_TOP_RIGHT) {
				current_begin.y = MIN(drag_begin.y, max_begin.y);
			} else if (view_state.drag_type == CanvasItemEditorViewState::DRAG_BOTTOM || view_state.drag_type == CanvasItemEditorViewState::DRAG_BOTTOM_LEFT || view_state.drag_type == CanvasItemEditorViewState::DRAG_BOTTOM_RIGHT) {
				current_end.y = MAX(drag_end.y, min_end.y);
			}

			// Uniform resize
			if (uniform) {
				if (view_state.drag_type == CanvasItemEditorViewState::DRAG_LEFT || view_state.drag_type == CanvasItemEditorViewState::DRAG_RIGHT) {
					current_end.y = current_begin.y + aspect * (current_end.x - current_begin.x);
				} else if (view_state.drag_type == CanvasItemEditorViewState::DRAG_TOP || view_state.drag_type == CanvasItemEditorViewState::DRAG_BOTTOM) {
					current_end.x = current_begin.x + (current_end.y - current_begin.y) / aspect;
				} else {
					if (aspect >= 1.0) {
						if (view_state.drag_type == CanvasItemEditorViewState::DRAG_TOP_LEFT || view_state.drag_type == CanvasItemEditorViewState::DRAG_TOP_RIGHT) {
							current_begin.y = current_end.y - aspect * (current_end.x - current_begin.x);
						} else {
							current_end.y = current_begin.y + aspect * (current_end.x - current_begin.x);
						}
					} else {
						if (view_state.drag_type == CanvasItemEditorViewState::DRAG_TOP_LEFT || view_state.drag_type == CanvasItemEditorViewState::DRAG_BOTTOM_LEFT) {
							current_begin.x = current_end.x - (current_end.y - current_begin.y) / aspect;
						} else {
							current_end.x = current_begin.x + (current_end.y - current_begin.y) / aspect;
						}
					}
				}
			}

			// Symmetric resize
			if (symmetric) {
				if (view_state.drag_type == CanvasItemEditorViewState::DRAG_LEFT || view_state.drag_type == CanvasItemEditorViewState::DRAG_TOP_LEFT || view_state.drag_type == CanvasItemEditorViewState::DRAG_BOTTOM_LEFT) {
					current_end.x = 2.0 * center.x - current_begin.x;
				} else if (view_state.drag_type == CanvasItemEditorViewState::DRAG_RIGHT || view_state.drag_type == CanvasItemEditorViewState::DRAG_TOP_RIGHT || view_state.drag_type == CanvasItemEditorViewState::DRAG_BOTTOM_RIGHT) {
					current_begin.x = 2.0 * center.x - current_end.x;
				}
				if (view_state.drag_type == CanvasItemEditorViewState::DRAG_TOP || view_state.drag_type == CanvasItemEditorViewState::DRAG_TOP_LEFT || view_state.drag_type == CanvasItemEditorViewState::DRAG_TOP_RIGHT) {
					current_end.y = 2.0 * center.y - current_begin.y;
				} else if (view_state.drag_type == CanvasItemEditorViewState::DRAG_BOTTOM || view_state.drag_type == CanvasItemEditorViewState::DRAG_BOTTOM_LEFT || view_state.drag_type == CanvasItemEditorViewState::DRAG_BOTTOM_RIGHT) {
					current_begin.y = 2.0 * center.y - current_end.y;
				}
			}
			ci->_edit_set_rect(Rect2(current_begin, current_end - current_begin));
			return true;
		}

		// Confirm resize
		if (view_state.drag_selection.size() >= 1 && b.is_valid() && b->get_button_index() == MouseButton::LEFT && !b->is_pressed()) {
			_commit_drag();
			return true;
		}

		// Cancel a drag
		if (ED_IS_SHORTCUT("canvas_item_editor/cancel_transform", p_event) || (b.is_valid() && b->get_button_index() == MouseButton::RIGHT && b->is_pressed())) {
			editor->_restore_canvas_item_state(view_state.drag_selection);
			view_state.snap_target[0] = CanvasItemEditorViewState::SNAP_TARGET_NONE;
			view_state.snap_target[1] = CanvasItemEditorViewState::SNAP_TARGET_NONE;
			editor->_reset_drag();
			viewport->queue_redraw();
			return true;
		}
	}
	return false;
}

bool CanvasItemEditorView::_gui_input_scale(const Ref<InputEvent> &p_event) {
	Ref<InputEventMouseButton> b = p_event;
	Ref<InputEventMouseMotion> m = p_event;

	// Drag resize handles
	if (view_state.drag_type == CanvasItemEditorViewState::DRAG_NONE) {
		if (b.is_valid() && b->get_button_index() == MouseButton::LEFT && b->is_pressed() &&
				((editor->tool == CanvasItemEditor::TOOL_SELECT && b->is_alt_pressed() && b->is_command_or_control_pressed()) || editor->tool == CanvasItemEditor::TOOL_SCALE)) {
			bool has_locked_items = false;
			List<CanvasItem *> selection = editor->_get_edited_canvas_items(false, true, &has_locked_items);

			// Remove non-movable nodes.
			for (CanvasItem *ci : selection) {
				if (!editor->_is_node_movable(ci, true)) {
					selection.erase(ci);
				}
			}

			if (!selection.is_empty()) {
				CanvasItem *ci = selection.front()->get();

				Transform2D edit_transform;
				if (!Math::is_inf(view_state.temp_pivot.x) || !Math::is_inf(view_state.temp_pivot.y)) {
					edit_transform = Transform2D(ci->_edit_get_rotation(), view_state.temp_pivot);
				} else {
					edit_transform = ci->_edit_get_transform();
				}

				Transform2D xform = view_state.transform * ci->get_screen_transform();
				Transform2D unscaled_transform = (xform * ci->get_transform().affine_inverse() * edit_transform).orthonormalized();
				Transform2D simple_xform;
				if (editor->use_local_space) {
					simple_xform = viewport->get_transform() * unscaled_transform;
				} else {
					Transform2D translation = Transform2D(0.0f, unscaled_transform.get_origin());
					simple_xform = viewport->get_transform() * translation;
				}

				view_state.drag_type = CanvasItemEditorViewState::DRAG_SCALE_BOTH;

				if (editor->show_transformation_gizmos) {
					Size2 scale_factor = Size2(SCALE_HANDLE_DISTANCE, SCALE_HANDLE_DISTANCE);
					Rect2 x_handle_rect = Rect2(scale_factor.x * EDSCALE, -5 * EDSCALE, 10 * EDSCALE, 10 * EDSCALE);
					if (x_handle_rect.has_point(simple_xform.affine_inverse().xform(b->get_position()))) {
						view_state.drag_type = CanvasItemEditorViewState::DRAG_SCALE_X;
					}
					Rect2 y_handle_rect = Rect2(-5 * EDSCALE, scale_factor.y * EDSCALE, 10 * EDSCALE, 10 * EDSCALE);
					if (y_handle_rect.has_point(simple_xform.affine_inverse().xform(b->get_position()))) {
						view_state.drag_type = CanvasItemEditorViewState::DRAG_SCALE_Y;
					}
				}

				view_state.drag_from = view_state.transform.affine_inverse().xform(b->get_position());
				view_state.drag_selection = selection;
				editor->_save_canvas_item_state(view_state.drag_selection);
				return true;
			} else {
				if (has_locked_items) {
					EditorToaster::get_singleton()->popup_str(TTR(editor->locked_transform_warning), EditorToaster::SEVERITY_WARNING);
				}
				return has_locked_items;
			}
		}
	} else if (view_state.drag_type == CanvasItemEditorViewState::DRAG_SCALE_BOTH || view_state.drag_type == CanvasItemEditorViewState::DRAG_SCALE_X || view_state.drag_type == CanvasItemEditorViewState::DRAG_SCALE_Y) {
		// Resize the node
		if (m.is_valid()) {
			editor->_restore_canvas_item_state(view_state.drag_selection);

			view_state.drag_to = view_state.transform.affine_inverse().xform(m->get_position());

			Size2 scale_max;
			if (view_state.drag_type != CanvasItemEditorViewState::DRAG_SCALE_BOTH) {
				for (CanvasItem *ci : view_state.drag_selection) {
					Size2 scale = ci->_edit_get_scale();

					if (Math::abs(scale.x) > Math::abs(scale_max.x)) {
						scale_max.x = scale.x;
					}
					if (Math::abs(scale.y) > Math::abs(scale_max.y)) {
						scale_max.y = scale.y;
					}
				}
			}

			Transform2D edit_transform;
			bool using_temp_pivot = !Math::is_inf(view_state.temp_pivot.x) || !Math::is_inf(view_state.temp_pivot.y);
			if (using_temp_pivot) {
				edit_transform = Transform2D(view_state.drag_selection.front()->get()->_edit_get_rotation(), view_state.temp_pivot);
			} else {
				edit_transform = view_state.drag_selection.front()->get()->_edit_get_transform();
			}
			for (CanvasItem *ci : view_state.drag_selection) {
				Transform2D parent_xform = ci->get_screen_transform() * ci->get_transform().affine_inverse();
				Transform2D unscaled_transform = (view_state.transform * parent_xform * edit_transform).orthonormalized();
				Transform2D simple_xform;

				if (editor->use_local_space || view_state.drag_type == CanvasItemEditorViewState::DRAG_SCALE_BOTH) {
					simple_xform = (viewport->get_transform() * unscaled_transform).affine_inverse() * view_state.transform;
				} else {
					Transform2D translation = Transform2D(0.0f, unscaled_transform.get_origin());
					simple_xform = (viewport->get_transform() * translation).affine_inverse() * view_state.transform;
				}

				bool uniform = m->is_shift_pressed();
				bool is_ctrl = m->is_command_or_control_pressed();

				Point2 drag_from_local = simple_xform.xform(view_state.drag_from);
				Point2 drag_to_local = simple_xform.xform(view_state.drag_to);
				Point2 offset = drag_to_local - drag_from_local;

				Transform2D object_transform = ci->_edit_get_transform();
				if (ci->is_class("Node2D")) {
					object_transform.set_skew(ci->get("skew"));
				}

				Size2 scale = ci->_edit_get_scale();
				Size2 original_scale = scale;
				real_t ratio = scale.y / scale.x;
				if (view_state.drag_type == CanvasItemEditorViewState::DRAG_SCALE_BOTH) {
					Size2 scale_factor = drag_to_local / drag_from_local;
					if (uniform) {
						scale *= (scale_factor.x + scale_factor.y) / 2.0;
					} else {
						scale *= scale_factor;
					}
				} else {
					Size2 scale_factor = Vector2(offset.x, -offset.y) / SCALE_HANDLE_DISTANCE;
					Size2 parent_scale = parent_xform.get_scale();
					// Take into account the biggest scale, so all nodes are scaled uniformly.
					scale_factor *= Vector2(1.0 / parent_scale.x, 1.0 / parent_scale.y) / (scale_max / original_scale);

					if (view_state.drag_type == CanvasItemEditorViewState::DRAG_SCALE_X) {
						if (!editor->use_local_space && !uniform) {
							object_transform.set_origin(Vector2(0.0, 0.0));
							object_transform.scale(Size2(scale_factor.x + 1.0, 1.0));
							scale *= object_transform.get_scale();
						} else {
							scale.x += scale_factor.x;
						}
						if (uniform) {
							scale.y = scale.x * ratio;
						}
					} else if (view_state.drag_type == CanvasItemEditorViewState::DRAG_SCALE_Y) {
						if (!editor->use_local_space && !uniform) {
							object_transform.set_origin(Vector2(0.0, 0.0));
							object_transform.scale(Size2(1.0, -scale_factor.y + 1.0));
							scale *= object_transform.get_scale();
						} else {
							scale.y -= scale_factor.y;
						}
						if (uniform) {
							scale.x = scale.y / ratio;
						}
					}
				}

				if (editor->snap_scale && !is_ctrl) {
					if (editor->snap_relative) {
						scale.x = original_scale.x * (Math::round((scale.x / original_scale.x) / editor->snap_scale_step) * editor->snap_scale_step);
						scale.y = original_scale.y * (Math::round((scale.y / original_scale.y) / editor->snap_scale_step) * editor->snap_scale_step);
					} else {
						scale.x = Math::round(scale.x / editor->snap_scale_step) * editor->snap_scale_step;
						scale.y = Math::round(scale.y / editor->snap_scale_step) * editor->snap_scale_step;
					}
				}

				ci->_edit_set_scale(scale);
				if (!editor->use_local_space && !uniform) {
					Node2D *n2d = Object::cast_to<Node2D>(ci);
					if (n2d) {
						n2d->_edit_set_rotation(object_transform.get_rotation());
						n2d->set_skew(object_transform.get_skew());
					}
				}

				if (using_temp_pivot) {
					Point2 ci_origin = ci->_edit_get_transform().get_origin();
					ci->_edit_set_position(ci_origin + (ci_origin - view_state.temp_pivot) * ((scale - original_scale) / original_scale));
				}
			}

			return true;
		}

		// Confirm resize
		if (b.is_valid() && b->get_button_index() == MouseButton::LEFT && !b->is_pressed()) {
			_commit_drag();
			return true;
		}

		// Cancel a drag
		if (ED_IS_SHORTCUT("canvas_item_editor/cancel_transform", p_event) || (b.is_valid() && b->get_button_index() == MouseButton::RIGHT && b->is_pressed())) {
			editor->_restore_canvas_item_state(view_state.drag_selection);
			editor->_reset_drag();
			viewport->queue_redraw();
			return true;
		}
	}
	return false;
}

bool CanvasItemEditorView::_gui_input_move(const Ref<InputEvent> &p_event) {
	Ref<InputEventMouseButton> b = p_event;
	Ref<InputEventMouseMotion> m = p_event;
	Ref<InputEventKey> k = p_event;

	if (view_state.drag_type == CanvasItemEditorViewState::DRAG_NONE) {
		//Start moving the nodes
		if (b.is_valid() && b->get_button_index() == MouseButton::LEFT && b->is_pressed()) {
			if ((editor->tool == CanvasItemEditor::TOOL_SELECT && b->is_alt_pressed() && !b->is_command_or_control_pressed()) || editor->tool == CanvasItemEditor::TOOL_MOVE) {
				bool has_locked_items = false;
				List<CanvasItem *> selection = editor->_get_edited_canvas_items(false, true, &has_locked_items);

				if (selection.size() > 0) {
					view_state.drag_selection.clear();
					for (CanvasItem *E : selection) {
						if (editor->_is_node_movable(E, true)) {
							view_state.drag_selection.push_back(E);
						}
					}

					view_state.drag_type = CanvasItemEditorViewState::DRAG_MOVE;

					CanvasItem *ci = selection.front()->get();
					Transform2D parent_xform = ci->get_screen_transform() * ci->get_transform().affine_inverse();
					Transform2D unscaled_transform = (view_state.transform * parent_xform * ci->_edit_get_transform()).orthonormalized();
					Transform2D simple_xform;
					if (editor->use_local_space) {
						simple_xform = viewport->get_transform() * unscaled_transform;
					} else {
						Transform2D translation = Transform2D(0.0f, unscaled_transform.get_origin());
						simple_xform = viewport->get_transform() * translation;
					}

					if (editor->show_transformation_gizmos) {
						Size2 move_factor = Size2(MOVE_HANDLE_DISTANCE, MOVE_HANDLE_DISTANCE);
						Rect2 x_handle_rect = Rect2(move_factor.x * EDSCALE, -5 * EDSCALE, 10 * EDSCALE, 10 * EDSCALE);
						if (x_handle_rect.has_point(simple_xform.affine_inverse().xform(b->get_position()))) {
							view_state.drag_type = CanvasItemEditorViewState::DRAG_MOVE_X;
						}
						Rect2 y_handle_rect = Rect2(-5 * EDSCALE, move_factor.y * EDSCALE, 10 * EDSCALE, 10 * EDSCALE);
						if (y_handle_rect.has_point(simple_xform.affine_inverse().xform(b->get_position()))) {
							view_state.drag_type = CanvasItemEditorViewState::DRAG_MOVE_Y;
						}
					}

					view_state.drag_from = view_state.transform.affine_inverse().xform(b->get_position());
					editor->_save_canvas_item_state(view_state.drag_selection);

					return true;
				} else {
					if (has_locked_items) {
						EditorToaster::get_singleton()->popup_str(TTR(editor->locked_transform_warning), EditorToaster::SEVERITY_WARNING);
					}
					return has_locked_items;
				}
			}
		}
	}

	if (view_state.drag_type == CanvasItemEditorViewState::DRAG_MOVE || view_state.drag_type == CanvasItemEditorViewState::DRAG_MOVE_X || view_state.drag_type == CanvasItemEditorViewState::DRAG_MOVE_Y) {
		// Move the nodes
		if (m.is_valid() && !view_state.drag_selection.is_empty()) {
			editor->_restore_canvas_item_state(view_state.drag_selection, true);

			view_state.drag_to = view_state.transform.affine_inverse().xform(m->get_position());
			Point2 previous_pos;
			if (view_state.drag_selection.size() == 1) {
				Transform2D parent_xform = view_state.drag_selection.front()->get()->get_screen_transform() * view_state.drag_selection.front()->get()->get_transform().affine_inverse();
				previous_pos = parent_xform.xform(view_state.drag_selection.front()->get()->_edit_get_position());
			} else {
				previous_pos = editor->_get_encompassing_rect_from_list(view_state.drag_selection).position;
			}

			Point2 drag_delta = view_state.drag_to - view_state.drag_from;
			if (view_state.drag_type == CanvasItemEditorViewState::DRAG_MOVE_X || view_state.drag_type == CanvasItemEditorViewState::DRAG_MOVE_Y) {
				const CanvasItem *selected = view_state.drag_selection.front()->get();
				Transform2D parent_xform = selected->get_screen_transform() * selected->get_transform().affine_inverse();
				Transform2D unscaled_transform = (view_state.transform * parent_xform * selected->_edit_get_transform()).orthonormalized();
				Transform2D simple_xform;
				if (editor->use_local_space) {
					simple_xform = viewport->get_transform() * unscaled_transform;
				} else {
					simple_xform = viewport->get_transform();
				}

				drag_delta = simple_xform.affine_inverse().basis_xform(drag_delta);
				if (view_state.drag_type == CanvasItemEditorViewState::DRAG_MOVE_X) {
					drag_delta.y = 0;
				} else {
					drag_delta.x = 0;
				}
				drag_delta = simple_xform.basis_xform(drag_delta);
			}
			Point2 new_pos = editor->snap_point(previous_pos + drag_delta, CanvasItemEditor::SNAP_GRID | CanvasItemEditor::SNAP_GUIDES | CanvasItemEditor::SNAP_PIXEL | CanvasItemEditor::SNAP_NODE_PARENT | CanvasItemEditor::SNAP_NODE_ANCHORS | CanvasItemEditor::SNAP_OTHER_NODES, 0, nullptr, view_state.drag_selection);

			bool single_axis = m->is_shift_pressed();
			if (single_axis) {
				if (Math::abs(new_pos.x - previous_pos.x) > Math::abs(new_pos.y - previous_pos.y)) {
					new_pos.y = previous_pos.y;
				} else {
					new_pos.x = previous_pos.x;
				}
			}

			for (CanvasItem *ci : view_state.drag_selection) {
				Transform2D parent_xform_inv = ci->get_transform() * ci->get_screen_transform().affine_inverse();
				ci->_edit_set_position(ci->_edit_get_position() + parent_xform_inv.basis_xform(new_pos - previous_pos));
			}
			return true;
		}

		// Confirm the move (only if it was moved)
		if (b.is_valid() && !b->is_pressed() && b->get_button_index() == MouseButton::LEFT) {
			_commit_drag();
			return true;
		}

		// Cancel a drag
		if (ED_IS_SHORTCUT("canvas_item_editor/cancel_transform", p_event) || (b.is_valid() && b->get_button_index() == MouseButton::RIGHT && b->is_pressed())) {
			editor->_restore_canvas_item_state(view_state.drag_selection, true);
			view_state.snap_target[0] = CanvasItemEditorViewState::SNAP_TARGET_NONE;
			view_state.snap_target[1] = CanvasItemEditorViewState::SNAP_TARGET_NONE;
			editor->_reset_drag();
			viewport->queue_redraw();
			return true;
		}
	}

	// Move the canvas items with the arrow keys
	if (k.is_valid() && k->is_pressed() && (editor->tool == CanvasItemEditor::TOOL_SELECT || editor->tool == CanvasItemEditor::TOOL_MOVE) &&
			(k->get_keycode() == Key::UP || k->get_keycode() == Key::DOWN || k->get_keycode() == Key::LEFT || k->get_keycode() == Key::RIGHT)) {
		if (!k->is_echo()) {
			// Start moving the canvas items with the keyboard, if they are movable
			List<CanvasItem *> selection = editor->_get_edited_canvas_items();

			view_state.drag_selection.clear();
			for (CanvasItem *item : selection) {
				if (editor->_is_node_movable(item, true)) {
					view_state.drag_selection.push_back(item);
				}
			}

			view_state.drag_type = CanvasItemEditorViewState::DRAG_KEY_MOVE;
			view_state.drag_from = Vector2();
			view_state.drag_to = Vector2();
			editor->_save_canvas_item_state(view_state.drag_selection, true);
		}

		if (view_state.drag_selection.size() > 0) {
			editor->_restore_canvas_item_state(view_state.drag_selection, true);

			bool move_local_base = k->is_alt_pressed();
			bool move_local_base_rotated = k->is_ctrl_pressed() || k->is_meta_pressed();

			Vector2 dir;
			if (k->get_keycode() == Key::UP) {
				dir += Vector2(0, -1);
			} else if (k->get_keycode() == Key::DOWN) {
				dir += Vector2(0, 1);
			} else if (k->get_keycode() == Key::LEFT) {
				dir += Vector2(-1, 0);
			} else if (k->get_keycode() == Key::RIGHT) {
				dir += Vector2(1, 0);
			}
			if (k->is_shift_pressed()) {
				dir *= editor->grid_step * Math::pow(2.0, editor->grid_step_multiplier);
			}

			view_state.drag_to += dir;
			if (k->is_shift_pressed()) {
				view_state.drag_to = view_state.drag_to.snapped(editor->grid_step * Math::pow(2.0, editor->grid_step_multiplier));
			}

			Point2 previous_pos;
			if (view_state.drag_selection.size() == 1) {
				Transform2D xform = view_state.drag_selection.front()->get()->get_global_transform_with_canvas() * view_state.drag_selection.front()->get()->get_transform().affine_inverse();
				previous_pos = xform.xform(view_state.drag_selection.front()->get()->_edit_get_position());
			} else {
				previous_pos = editor->_get_encompassing_rect_from_list(view_state.drag_selection).position;
			}

			Point2 new_pos;
			if (view_state.drag_selection.size() == 1) {
				Node2D *node_2d = Object::cast_to<Node2D>(view_state.drag_selection.front()->get());
				if (node_2d && move_local_base_rotated) {
					Transform2D m2;
					m2.rotate(node_2d->get_rotation());
					new_pos += m2.xform(view_state.drag_to);
				} else if (move_local_base) {
					new_pos += view_state.drag_to;
				} else {
					new_pos = previous_pos + (view_state.drag_to - view_state.drag_from);
				}
			} else {
				new_pos = previous_pos + (view_state.drag_to - view_state.drag_from);
			}

			for (CanvasItem *ci : view_state.drag_selection) {
				Transform2D xform = ci->get_global_transform_with_canvas().affine_inverse() * ci->get_transform();
				ci->_edit_set_position(ci->_edit_get_position() + xform.xform(new_pos) - xform.xform(previous_pos));
			}
		}
		return true;
	}

	// Confirm canvas items move by arrow keys.
	if (k.is_valid() && !k->is_pressed() && view_state.drag_type == CanvasItemEditorViewState::DRAG_KEY_MOVE && (editor->tool == CanvasItemEditor::TOOL_SELECT || editor->tool == CanvasItemEditor::TOOL_MOVE) &&
			(k->get_keycode() == Key::UP || k->get_keycode() == Key::DOWN || k->get_keycode() == Key::LEFT || k->get_keycode() == Key::RIGHT)) {
		_commit_drag();
		return true;
	}

	return (k.is_valid() && (k->get_keycode() == Key::UP || k->get_keycode() == Key::DOWN || k->get_keycode() == Key::LEFT || k->get_keycode() == Key::RIGHT)); // Accept the key event in any case
}

bool CanvasItemEditorView::_gui_input_select(const Ref<InputEvent> &p_event) {
	Ref<InputEventMouseButton> b = p_event;
	Ref<InputEventMouseMotion> m = p_event;
	Ref<InputEventKey> k = p_event;

	if (view_state.drag_type == CanvasItemEditorViewState::DRAG_NONE || (view_state.drag_type == CanvasItemEditorViewState::DRAG_BOX_SELECTION && b.is_valid() && !b->is_pressed())) {
		if (b.is_valid() && b->is_pressed() &&
				((b->get_button_index() == MouseButton::RIGHT && b->is_alt_pressed()) ||
						(b->get_button_index() == MouseButton::LEFT && editor->tool == CanvasItemEditor::TOOL_LIST_SELECT))) {
			// Popup the selection menu list
			Point2 click = view_state.transform.affine_inverse().xform(b->get_position());

			editor->_get_canvas_items_at_pos(click, view_state.selection_results, b->is_alt_pressed());

			if (view_state.selection_results.size() == 1) {
				CanvasItem *item = view_state.selection_results[0].item;
				view_state.selection_results.clear();

				editor->_select_click_on_item(item, click, b->is_shift_pressed());

				return true;
			} else if (!view_state.selection_results.is_empty()) {
				// Sorts items according the their z-index
				view_state.selection_results.sort();

				NodePath root_path = get_tree()->get_edited_scene_root()->get_path();
				StringName root_name = root_path.get_name(root_path.get_name_count() - 1);
				int icon_max_width = EditorNode::get_singleton()->get_editor_theme()->get_constant(SNAME("class_icon_size"), EditorStringName(Editor));

				for (int i = 0; i < view_state.selection_results.size(); i++) {
					CanvasItem *item = view_state.selection_results[i].item;

					Ref<Texture2D> icon = EditorNode::get_singleton()->get_object_icon(item);
					String node_path = "/" + root_name + "/" + String(root_path.rel_path_to(item->get_path()));

					int locked = 0;
					if (editor->_is_node_locked(item)) {
						locked = 1;
					} else {
						Node *scene = get_edited_scene();
						Node *node = item;

						while (node && node != scene->get_parent()) {
							CanvasItem *ci_tmp = Object::cast_to<CanvasItem>(node);
							if (ci_tmp && node->has_meta("_edit_group_")) {
								locked = 2;
							}
							node = node->get_parent();
						}
					}

					String suffix;
					if (locked == 1) {
						suffix = " (" + TTR("Locked") + ")";
					} else if (locked == 2) {
						suffix = " (" + TTR("Grouped") + ")";
					}
					editor->selection_menu->add_item((String)item->get_name() + suffix);
					editor->selection_menu->set_item_icon(i, icon);
					editor->selection_menu->set_item_icon_max_width(i, icon_max_width);
					editor->selection_menu->set_item_metadata(i, node_path);
					editor->selection_menu->set_item_tooltip(i, String(item->get_name()) + "\nType: " + item->get_class() + "\nPath: " + node_path);
				}

				view_state.selection_results_menu = view_state.selection_results;
				editor->selection_menu_additive_selection = b->is_shift_pressed();
				editor->selection_menu->set_position(viewport->get_screen_transform().xform(b->get_position()));
				editor->selection_menu->reset_size();
				editor->selection_menu->popup();
				return true;
			}
		}

		if (b.is_valid() && b->is_pressed() && b->get_button_index() == MouseButton::RIGHT) {
			editor->add_node_menu->clear();
			editor->add_node_menu->add_icon_item(get_editor_theme_icon(SNAME("Add")), TTRC("Add Node Here..."), CanvasItemEditor::ADD_NODE);
			editor->add_node_menu->add_icon_item(get_editor_theme_icon(SNAME("Instance")), TTRC("Instantiate Scene Here..."), CanvasItemEditor::ADD_INSTANCE);
			for (Node *node : SceneTreeDock::get_singleton()->get_node_clipboard()) {
				if (Object::cast_to<CanvasItem>(node)) {
					editor->add_node_menu->add_icon_item(get_editor_theme_icon(SNAME("ActionPaste")), TTRC("Paste Node(s) Here"), CanvasItemEditor::ADD_PASTE);
					break;
				}
			}
			for (Node *node : EditorNode::get_singleton()->get_editor_selection()->get_top_selected_node_list()) {
				if (Object::cast_to<CanvasItem>(node)) {
					editor->add_node_menu->add_icon_item(get_editor_theme_icon(SNAME("ToolMove")), TTRC("Move Node(s) Here"), CanvasItemEditor::ADD_MOVE);
					break;
				}
			}

			// Context menu plugin receives paths of nodes under cursor. It's a complex operation, so perform it only when necessary.
			if (EditorContextMenuPluginManager::get_singleton()->has_plugins_for_slot(EditorContextMenuPlugin::CONTEXT_SLOT_2D_EDITOR)) {
				view_state.selection_results.clear();
				editor->_get_canvas_items_at_pos(view_state.transform.affine_inverse().xform(viewport->get_local_mouse_position()), view_state.selection_results, true);

				PackedStringArray paths;
				paths.resize(view_state.selection_results.size());
				String *paths_write = paths.ptrw();

				for (int i = 0; i < paths.size(); i++) {
					paths_write[i] = String(view_state.selection_results[i].item->get_path());
				}
				EditorContextMenuPluginManager::get_singleton()->add_options_from_plugins(editor->add_node_menu, EditorContextMenuPlugin::CONTEXT_SLOT_2D_EDITOR, paths);
			}

			editor->add_node_menu->reset_size();
			editor->add_node_menu->set_position(viewport->get_screen_transform().xform(b->get_position()));
			editor->add_node_menu->popup();
			view_state.node_create_position = view_state.transform.affine_inverse().xform(b->get_position());
			return true;
		}

		Point2 click;
		bool can_select = b.is_valid() && b->get_button_index() == MouseButton::LEFT && !panner->is_panning() && (editor->tool == CanvasItemEditor::TOOL_SELECT || editor->tool == CanvasItemEditor::TOOL_MOVE || editor->tool == CanvasItemEditor::TOOL_SCALE || editor->tool == CanvasItemEditor::TOOL_ROTATE);
		if (can_select) {
			click = view_state.transform.affine_inverse().xform(b->get_position());
			// Allow selecting on release when performed very small box selection (necessary when Shift is pressed, see below).
			can_select = b->is_pressed() || (view_state.drag_type == CanvasItemEditorViewState::DRAG_BOX_SELECTION && click.distance_to(view_state.drag_from) <= DRAG_THRESHOLD);
		}

		if (can_select) {
			// Single item selection.
			Node *scene = get_edited_scene();
			if (!scene) {
				return true;
			}

			// Find the item to select.
			CanvasItem *ci = nullptr;

			Vector<CanvasItemEditorViewState::SelectResult> selection = Vector<CanvasItemEditorViewState::SelectResult>();
			// Retrieve the canvas items.
			editor->_get_canvas_items_at_pos(click, selection);
			if (!selection.is_empty()) {
				ci = selection[0].item;
			}

			// Shift also allows forcing box selection when item was clicked.
			if (!ci || (b->is_shift_pressed() && b->is_pressed())) {
				// Start a box selection.
				if (!b->is_shift_pressed()) {
					// Clear the selection if not additive.
					editor->editor_selection->clear();
					viewport->queue_redraw();
					editor->selected_from_canvas = true;
				};

				if (b->is_pressed()) {
					view_state.drag_from = click;
					view_state.drag_type = CanvasItemEditorViewState::DRAG_BOX_SELECTION;
					view_state.box_selecting_to = view_state.drag_from;
					return true;
				}
			} else {
				bool still_selected = editor->_select_click_on_item(ci, click, b->is_shift_pressed());
				// Start dragging.
				if (still_selected && (editor->tool == CanvasItemEditor::TOOL_SELECT || editor->tool == CanvasItemEditor::TOOL_MOVE) && b->is_pressed()) {
					// Drag the node(s) if requested.
					view_state.drag_start_origin = click;
					view_state.drag_type = CanvasItemEditorViewState::DRAG_QUEUED;
				} else if (!b->is_pressed()) {
					editor->_reset_drag();
				}
				// Select the item.
				return true;
			}
		}
	}

	if (view_state.drag_type == CanvasItemEditorViewState::DRAG_QUEUED) {
		if (b.is_valid() && !b->is_pressed()) {
			editor->_reset_drag();
			return true;
		}
		if (m.is_valid()) {
			Point2 click = view_state.transform.affine_inverse().xform(m->get_position());
			bool movement_threshold_passed = view_state.drag_start_origin.distance_to(click) > (8 * MAX(1, EDSCALE)) / view_state.zoom;
			if (m.is_valid() && movement_threshold_passed) {
				List<CanvasItem *> selection2 = editor->_get_edited_canvas_items();

				view_state.drag_selection.clear();
				for (CanvasItem *E : selection2) {
					if (editor->_is_node_movable(E, true)) {
						view_state.drag_selection.push_back(E);
					}
				}

				if (selection2.size() > 0) {
					view_state.drag_type = CanvasItemEditorViewState::DRAG_MOVE;
					view_state.drag_from = view_state.drag_start_origin;
					editor->_save_canvas_item_state(view_state.drag_selection);
				}
				return true;
			}
		}
	}

	if (view_state.drag_type == CanvasItemEditorViewState::DRAG_BOX_SELECTION) {
		if (b.is_valid() && !b->is_pressed() && b->get_button_index() == MouseButton::LEFT) {
			// Confirms box selection.
			Node *scene = get_edited_scene();
			if (scene) {
				List<CanvasItem *> selitems;

				Point2 bsfrom = view_state.drag_from;
				Point2 bsto = view_state.box_selecting_to;
				if (bsfrom.x > bsto.x) {
					SWAP(bsfrom.x, bsto.x);
				}
				if (bsfrom.y > bsto.y) {
					SWAP(bsfrom.y, bsto.y);
				}

				editor->_find_canvas_items_in_rect(Rect2(bsfrom, bsto - bsfrom), scene, &selitems);
				if (selitems.size() == 1 && editor->editor_selection->get_selection().is_empty()) {
					EditorNode::get_singleton()->push_item(selitems.front()->get());
				}
				for (CanvasItem *E : selitems) {
					editor->editor_selection->add_node(E);
				}
			}

			editor->_reset_drag();
			viewport->queue_redraw();
			return true;
		}

		if (b.is_valid() && b->is_pressed() && b->get_button_index() == MouseButton::RIGHT) {
			// Cancel box selection.
			editor->_reset_drag();
			viewport->queue_redraw();
			return true;
		}

		if (m.is_valid()) {
			// Update box selection.
			view_state.box_selecting_to = view_state.transform.affine_inverse().xform(m->get_position());
			viewport->queue_redraw();
			return true;
		}
	}

	if (k.is_valid() && k->is_action_pressed(SNAME("ui_cancel"), false, true) && view_state.drag_type == CanvasItemEditorViewState::DRAG_NONE) {
		// Unselect everything
		editor->editor_selection->clear();
		viewport->queue_redraw();
	}
	return false;
}

bool CanvasItemEditorView::_gui_input_ruler_tool(const Ref<InputEvent> &p_event) {
	if (editor->tool != CanvasItemEditor::TOOL_RULER) {
		view_state.ruler_tool_active = false;
		return false;
	}

	Ref<InputEventMouseButton> b = p_event;
	Ref<InputEventMouseMotion> m = p_event;

	Point2 previous_origin = view_state.ruler_tool_origin;
	if (!view_state.ruler_tool_active) {
		view_state.ruler_tool_origin = editor->snap_point(viewport->get_local_mouse_position() / view_state.zoom + view_state.view_offset);
	}

	if (view_state.ruler_tool_active && b.is_valid() && b->get_button_index() == MouseButton::RIGHT) {
		view_state.ruler_tool_active = false;
		viewport->queue_redraw();
		return true;
	}

	if (b.is_valid() && b->get_button_index() == MouseButton::LEFT) {
		if (b->is_pressed()) {
			view_state.ruler_tool_active = true;
		} else {
			view_state.ruler_tool_active = false;
		}

		viewport->queue_redraw();
		return true;
	}

	if (m.is_valid() && (view_state.ruler_tool_active || (editor->grid_snap_active && previous_origin != view_state.ruler_tool_origin))) {
		viewport->queue_redraw();
		return true;
	}

	return false;
}

bool CanvasItemEditorView::_gui_input_hover(const Ref<InputEvent> &p_event) {
	Ref<InputEventMouseMotion> m = p_event;
	if (m.is_valid()) {
		Point2 click = view_state.transform.affine_inverse().xform(m->get_position());

		// Checks if the hovered items changed, redraw the viewport if so
		Vector<CanvasItemEditorViewState::SelectResult> hovering_results_items;
		editor->_get_canvas_items_at_pos(click, hovering_results_items);
		hovering_results_items.sort();

		// Compute the nodes names and icon position
		Vector<CanvasItemEditorViewState::HoverResult> hovering_results_tmp;
		for (int i = 0; i < hovering_results_items.size(); i++) {
			CanvasItem *ci = hovering_results_items[i].item;

			if (ci->_edit_use_rect()) {
				continue;
			}

			CanvasItemEditorViewState::HoverResult hover_result;
			hover_result.position = ci->get_screen_transform().get_origin();
			hover_result.icon = EditorNode::get_singleton()->get_object_icon(ci);
			hover_result.name = ci->get_name();

			hovering_results_tmp.push_back(hover_result);
		}

		// Check if changed, if so, redraw.
		bool changed = false;
		if (hovering_results_tmp.size() == view_state.hovering_results.size()) {
			for (int i = 0; i < hovering_results_tmp.size(); i++) {
				CanvasItemEditorViewState::HoverResult a = hovering_results_tmp[i];
				CanvasItemEditorViewState::HoverResult b = view_state.hovering_results[i];
				if (a.icon != b.icon || a.name != b.name || a.position != b.position) {
					changed = true;
					break;
				}
			}
		} else {
			changed = true;
		}

		if (changed) {
			view_state.hovering_results = hovering_results_tmp;
			viewport->queue_redraw();
		}

		return true;
	}

	return false;
}

void CanvasItemEditorView::_gui_input_viewport(const Ref<InputEvent> &p_event) {
	bool accepted = false;

	Ref<InputEventMouseButton> mb = p_event;
	bool release_lmb = (mb.is_valid() && !mb->is_pressed() && mb->get_button_index() == MouseButton::LEFT); // Required to properly release some stuff (e.g. selection box) while panning.

	if (editor->simple_panning || !view_state.pan_pressed || release_lmb) {
		accepted = true;
		if (_gui_input_rulers_and_guides(p_event)) {
			// print_line("Rulers and guides");
		} else if (plugin_forwarding_target && EditorNode::get_singleton()->get_editor_plugins_over()->forward_gui_input(p_event)) {
			// print_line("Plugin");
		} else if (_gui_input_open_scene_on_double_click(p_event)) {
			// print_line("Open scene on double click");
		} else if (_gui_input_scale(p_event)) {
			// print_line("Set scale");
		} else if (_gui_input_pivot(p_event)) {
			// print_line("Set pivot");
		} else if (_gui_input_resize(p_event)) {
			// print_line("Resize");
		} else if (_gui_input_rotate(p_event)) {
			// print_line("Rotate");
		} else if (_gui_input_move(p_event)) {
			// print_line("Move");
		} else if (_gui_input_anchors(p_event)) {
			// print_line("Anchors");
		} else if (_gui_input_ruler_tool(p_event)) {
			// print_line("Measure");
		} else if (_gui_input_select(p_event)) {
			// print_line("Selection");
		} else {
			// print_line("Not accepted");
			accepted = false;
		}
	}

	accepted = (_gui_input_zoom_or_pan(p_event, accepted) || accepted);

	if (accepted) {
		accept_event();
	}

	// Handles the mouse hovering
	_gui_input_hover(p_event);

	if (mb.is_valid()) {
		// Update the default cursor.
		_update_cursor();
	}

	// Grab focus
	if (!viewport->has_focus() && (!get_viewport()->gui_get_focus_owner() || !get_viewport()->gui_get_focus_owner()->is_text_field())) {
		callable_mp((Control *)viewport, &Control::grab_focus).call_deferred(false);
	}
}

void CanvasItemEditorView::_commit_drag() {
	if (!view_state.drag_selection.is_empty()) {
		switch (view_state.drag_type) {
			// Confirm the pivot move.
			case CanvasItemEditorViewState::DRAG_PIVOT: {
				editor->_commit_canvas_item_state(
						view_state.drag_selection,
						vformat(
								TTR("Set CanvasItem \"%s\" Pivot Offset to (%d, %d)"),
								view_state.drag_selection.front()->get()->get_name(),
								view_state.drag_selection.front()->get()->_edit_get_pivot().x,
								view_state.drag_selection.front()->get()->_edit_get_pivot().y));
			} break;

			// Confirm the node rotation.
			case CanvasItemEditorViewState::DRAG_ROTATE: {
				if (view_state.drag_selection.size() != 1) {
					editor->_commit_canvas_item_state(
							view_state.drag_selection,
							vformat(TTR("Rotate %d CanvasItems"), view_state.drag_selection.size()),
							true);
				} else {
					editor->_commit_canvas_item_state(
							view_state.drag_selection,
							vformat(TTR("Rotate CanvasItem \"%s\" to %d degrees"),
									view_state.drag_selection.front()->get()->get_name(),
									Math::rad_to_deg(view_state.drag_selection.front()->get()->_edit_get_rotation())),
							true);
				}

				if (editor->key_auto_insert_button->is_pressed()) {
					editor->_insert_animation_keys(false, true, false, true);
				}
			} break;

			// Confirm new anchor position.
			case CanvasItemEditorViewState::DRAG_ANCHOR_TOP_LEFT:
			case CanvasItemEditorViewState::DRAG_ANCHOR_TOP_RIGHT:
			case CanvasItemEditorViewState::DRAG_ANCHOR_BOTTOM_RIGHT:
			case CanvasItemEditorViewState::DRAG_ANCHOR_BOTTOM_LEFT:
			case CanvasItemEditorViewState::DRAG_ANCHOR_ALL: {
				editor->_commit_canvas_item_state(
						view_state.drag_selection,
						vformat(TTR("Move CanvasItem \"%s\" Anchor"), view_state.drag_selection.front()->get()->get_name()));
				view_state.snap_target[0] = CanvasItemEditorViewState::SNAP_TARGET_NONE;
				view_state.snap_target[1] = CanvasItemEditorViewState::SNAP_TARGET_NONE;
			} break;

			// Confirm resize.
			case CanvasItemEditorViewState::DRAG_LEFT:
			case CanvasItemEditorViewState::DRAG_RIGHT:
			case CanvasItemEditorViewState::DRAG_TOP:
			case CanvasItemEditorViewState::DRAG_BOTTOM:
			case CanvasItemEditorViewState::DRAG_TOP_LEFT:
			case CanvasItemEditorViewState::DRAG_TOP_RIGHT:
			case CanvasItemEditorViewState::DRAG_BOTTOM_LEFT:
			case CanvasItemEditorViewState::DRAG_BOTTOM_RIGHT: {
				const Node2D *node2d = Object::cast_to<Node2D>(view_state.drag_selection.front()->get());
				if (node2d) {
					// Extends from Node2D.
					// Node2D doesn't have an actual stored rect size, unlike Controls.
					editor->_commit_canvas_item_state(
							view_state.drag_selection,
							vformat(
									TTR("Scale Node2D \"%s\" to (%s, %s)"),
									view_state.drag_selection.front()->get()->get_name(),
									Math::snapped(view_state.drag_selection.front()->get()->_edit_get_scale().x, 0.01),
									Math::snapped(view_state.drag_selection.front()->get()->_edit_get_scale().y, 0.01)),
							true);
				} else {
					// Extends from Control.
					editor->_commit_canvas_item_state(
							view_state.drag_selection,
							vformat(
									TTR("Resize Control \"%s\" to (%d, %d)"),
									view_state.drag_selection.front()->get()->get_name(),
									view_state.drag_selection.front()->get()->_edit_get_rect().size.x,
									view_state.drag_selection.front()->get()->_edit_get_rect().size.y),
							true);
				}

				if (editor->key_auto_insert_button->is_pressed()) {
					editor->_insert_animation_keys(false, false, true, true);
				}

				view_state.snap_target[0] = CanvasItemEditorViewState::SNAP_TARGET_NONE;
				view_state.snap_target[1] = CanvasItemEditorViewState::SNAP_TARGET_NONE;
			} break;

			// Confirm resize.
			case CanvasItemEditorViewState::DRAG_SCALE_BOTH:
			case CanvasItemEditorViewState::DRAG_SCALE_X:
			case CanvasItemEditorViewState::DRAG_SCALE_Y: {
				if (view_state.drag_selection.size() != 1) {
					editor->_commit_canvas_item_state(
							view_state.drag_selection,
							vformat(TTR("Scale %d CanvasItems"), view_state.drag_selection.size()),
							true);
				} else {
					editor->_commit_canvas_item_state(
							view_state.drag_selection,
							vformat(TTR("Scale CanvasItem \"%s\" to (%s, %s)"),
									view_state.drag_selection.front()->get()->get_name(),
									Math::snapped(view_state.drag_selection.front()->get()->_edit_get_scale().x, 0.01),
									Math::snapped(view_state.drag_selection.front()->get()->_edit_get_scale().y, 0.01)),
							true);
				}
				if (editor->key_auto_insert_button->is_pressed()) {
					editor->_insert_animation_keys(false, false, true, true);
				}
			} break;

			// Confirm the canvas items move.
			case CanvasItemEditorViewState::DRAG_MOVE:
			case CanvasItemEditorViewState::DRAG_MOVE_X:
			case CanvasItemEditorViewState::DRAG_MOVE_Y: {
				if (view_state.transform.affine_inverse().xform(get_viewport()->get_mouse_position()) != view_state.drag_from) {
					if (view_state.drag_selection.size() != 1) {
						editor->_commit_canvas_item_state(
								view_state.drag_selection,
								vformat(TTR("Move %d CanvasItems"), view_state.drag_selection.size()),
								true);
					} else {
						editor->_commit_canvas_item_state(
								view_state.drag_selection,
								vformat(
										TTR("Move CanvasItem \"%s\" to (%d, %d)"),
										view_state.drag_selection.front()->get()->get_name(),
										view_state.drag_selection.front()->get()->_edit_get_position().x,
										view_state.drag_selection.front()->get()->_edit_get_position().y),
								true);
					}
				}

				if (editor->key_auto_insert_button->is_pressed()) {
					editor->_insert_animation_keys(true, false, false, true);
				}

				// Make sure smart snapping lines disappear.
				view_state.snap_target[0] = CanvasItemEditorViewState::SNAP_TARGET_NONE;
				view_state.snap_target[1] = CanvasItemEditorViewState::SNAP_TARGET_NONE;
			} break;

			// Confirm the canvas items move by arrow keys.
			case CanvasItemEditorViewState::DRAG_KEY_MOVE: {
				if (editor->tool != CanvasItemEditor::TOOL_SELECT && editor->tool != CanvasItemEditor::TOOL_MOVE) {
					return;
				}

				if (view_state.drag_selection.size() > 1) {
					editor->_commit_canvas_item_state(
							view_state.drag_selection,
							vformat(TTR("Move %d CanvasItems"), view_state.drag_selection.size()),
							true);
				} else if (view_state.drag_selection.size() == 1) {
					editor->_commit_canvas_item_state(
							view_state.drag_selection,
							vformat(TTR("Move CanvasItem \"%s\" to (%d, %d)"),
									view_state.drag_selection.front()->get()->get_name(),
									view_state.drag_selection.front()->get()->_edit_get_position().x,
									view_state.drag_selection.front()->get()->_edit_get_position().y),
							true);
				}
			} break;

			default:
				break;
		}
	}

	editor->_reset_drag();
	viewport->queue_redraw();
	_update_cursor();
}

void CanvasItemEditorView::_update_cursor() {
	if (view_state.cursor_shape_override != CURSOR_ARROW) {
		set_default_cursor_shape(view_state.cursor_shape_override);
		return;
	}

	// Choose the correct default cursor.
	CursorShape c = CURSOR_ARROW;
	switch (editor->tool) {
		case CanvasItemEditor::TOOL_MOVE:
			c = CURSOR_MOVE;
			break;
		case CanvasItemEditor::TOOL_EDIT_PIVOT:
			c = CURSOR_CROSS;
			break;
		case CanvasItemEditor::TOOL_PAN:
			c = CURSOR_DRAG;
			break;
		case CanvasItemEditor::TOOL_RULER:
			c = CURSOR_CROSS;
			break;
		default:
			break;
	}
	if (view_state.pan_pressed) {
		c = CURSOR_DRAG;
	}
	set_default_cursor_shape(c);
}

void CanvasItemEditorView::set_cursor_shape_override(CursorShape p_shape) {
	if (view_state.cursor_shape_override == p_shape) {
		return;
	}
	view_state.cursor_shape_override = p_shape;
	_update_cursor();
}

Control::CursorShape CanvasItemEditorView::get_cursor_shape(const Point2 &p_pos) const {
	// Compute an eventual rotation of the cursor
	const CursorShape rotation_array[4] = { CURSOR_HSIZE, CURSOR_BDIAGSIZE, CURSOR_VSIZE, CURSOR_FDIAGSIZE };
	int rotation_array_index = 0;

	List<CanvasItem *> selection = editor->_get_edited_canvas_items();
	if (selection.size() == 1) {
		const double angle = Math::fposmod((double)selection.front()->get()->get_global_transform_with_canvas().get_rotation(), Math::PI);
		if (angle > Math::PI * 7.0 / 8.0) {
			rotation_array_index = 0;
		} else if (angle > Math::PI * 5.0 / 8.0) {
			rotation_array_index = 1;
		} else if (angle > Math::PI * 3.0 / 8.0) {
			rotation_array_index = 2;
		} else if (angle > Math::PI * 1.0 / 8.0) {
			rotation_array_index = 3;
		} else {
			rotation_array_index = 0;
		}
	}

	// Choose the correct cursor
	CursorShape c = get_default_cursor_shape();
	switch (view_state.drag_type) {
		case CanvasItemEditorViewState::DRAG_LEFT:
		case CanvasItemEditorViewState::DRAG_RIGHT:
			c = rotation_array[rotation_array_index];
			break;
		case CanvasItemEditorViewState::DRAG_V_GUIDE:
			c = CURSOR_HSIZE;
			break;
		case CanvasItemEditorViewState::DRAG_TOP:
		case CanvasItemEditorViewState::DRAG_BOTTOM:
			c = rotation_array[(rotation_array_index + 2) % 4];
			break;
		case CanvasItemEditorViewState::DRAG_H_GUIDE:
			c = CURSOR_VSIZE;
			break;
		case CanvasItemEditorViewState::DRAG_TOP_LEFT:
		case CanvasItemEditorViewState::DRAG_BOTTOM_RIGHT:
			c = rotation_array[(rotation_array_index + 3) % 4];
			break;
		case CanvasItemEditorViewState::DRAG_DOUBLE_GUIDE:
			c = CURSOR_FDIAGSIZE;
			break;
		case CanvasItemEditorViewState::DRAG_TOP_RIGHT:
		case CanvasItemEditorViewState::DRAG_BOTTOM_LEFT:
			c = rotation_array[(rotation_array_index + 1) % 4];
			break;
		case CanvasItemEditorViewState::DRAG_MOVE:
			c = CURSOR_MOVE;
			break;
		default:
			break;
	}

	if (view_state.is_hovering_h_guide) {
		c = CURSOR_VSIZE;
	} else if (view_state.is_hovering_v_guide) {
		c = CURSOR_HSIZE;
	}

	if (view_state.pan_pressed) {
		c = CURSOR_DRAG;
	}
	return c;
}

void CanvasItemEditorView::_draw_text_at_position(Point2 p_position, const String &p_string, Side p_side) {
	Color color = get_theme_color(SceneStringName(font_color), EditorStringName(Editor));
	color.a = 0.8;
	Ref<Font> font = get_theme_font(SceneStringName(font), SNAME("Label"));
	int font_size = get_theme_font_size(SceneStringName(font_size), SNAME("Label"));
	Size2 text_size = font->get_string_size(p_string, HORIZONTAL_ALIGNMENT_LEFT, -1, font_size);
	switch (p_side) {
		case SIDE_LEFT:
			p_position += Vector2(-text_size.x - 5, text_size.y / 2);
			break;
		case SIDE_TOP:
			p_position += Vector2(-text_size.x / 2, -5);
			break;
		case SIDE_RIGHT:
			p_position += Vector2(5, text_size.y / 2);
			break;
		case SIDE_BOTTOM:
			p_position += Vector2(-text_size.x / 2, text_size.y + 5);
			break;
	}
	viewport->draw_string(font, p_position, p_string, HORIZONTAL_ALIGNMENT_LEFT, -1, font_size, color);
}

void CanvasItemEditorView::_draw_margin_at_position(int p_value, Point2 p_position, Side p_side) {
	String str = TranslationServer::get_singleton()->format_number(vformat("%d " + TTR("px"), p_value), _get_locale());
	if (p_value != 0) {
		_draw_text_at_position(p_position, str, p_side);
	}
}

void CanvasItemEditorView::_draw_percentage_at_position(real_t p_value, Point2 p_position, Side p_side) {
	const String &lang = _get_locale();
	String str = TranslationServer::get_singleton()->format_number(vformat("%.1f ", p_value * 100.0), lang) + TranslationServer::get_singleton()->get_percent_sign(lang);
	if (p_value != 0) {
		_draw_text_at_position(p_position, str, p_side);
	}
}

void CanvasItemEditorView::_draw_focus() {
	// Draw the focus around the base viewport
	if (viewport->has_focus()) {
		get_theme_stylebox(SNAME("FocusViewport"), EditorStringName(EditorStyles))->draw(viewport->get_canvas_item(), Rect2(Point2(), viewport->get_size()));
	}
}

void CanvasItemEditorView::_draw_guides() {
	Color guide_color = EDITOR_GET("editors/2d/guides_color");
	Transform2D xform = viewport_scrollable->get_transform() * view_state.transform;

	// Guides already there.
	if (Node *scene = get_edited_scene()) {
		Array vguides = scene->get_meta("_edit_vertical_guides_", Array());
		for (int i = 0; i < vguides.size(); i++) {
			if (view_state.drag_type == CanvasItemEditorViewState::DRAG_V_GUIDE && i == view_state.dragged_guide_index) {
				continue;
			}
			real_t x = xform.xform(Point2(vguides[i], 0)).x;
			viewport->draw_line(Point2(x, 0), Point2(x, viewport->get_size().y), guide_color, Math::round(EDSCALE));
		}

		Array hguides = scene->get_meta("_edit_horizontal_guides_", Array());
		for (int i = 0; i < hguides.size(); i++) {
			if (view_state.drag_type == CanvasItemEditorViewState::DRAG_H_GUIDE && i == view_state.dragged_guide_index) {
				continue;
			}
			real_t y = xform.xform(Point2(0, hguides[i])).y;
			viewport->draw_line(Point2(0, y), Point2(viewport->get_size().x, y), guide_color, Math::round(EDSCALE));
		}
	}

	// Dragged guide.
	Color text_color = get_theme_color(SceneStringName(font_color), EditorStringName(Editor));
	Color outline_color = text_color.inverted();
	const float outline_size = 2;
	const String &lang = _get_locale();
	if (view_state.drag_type == CanvasItemEditorViewState::DRAG_DOUBLE_GUIDE || view_state.drag_type == CanvasItemEditorViewState::DRAG_V_GUIDE) {
		String str = TranslationServer::get_singleton()->format_number(vformat("%d px", Math::round(xform.affine_inverse().xform(view_state.dragged_guide_pos).x)), lang);
		Ref<Font> font = get_theme_font(SNAME("bold"), EditorStringName(EditorFonts));
		int font_size = 1.3 * get_theme_font_size(SNAME("bold_size"), EditorStringName(EditorFonts));
		Size2 text_size = font->get_string_size(str, HORIZONTAL_ALIGNMENT_LEFT, -1, font_size);
		viewport->draw_string_outline(font, Point2(view_state.dragged_guide_pos.x + 10, editor->ruler_width_scaled + text_size.y / 2 + 10), str, HORIZONTAL_ALIGNMENT_LEFT, -1, font_size, outline_size, outline_color);
		viewport->draw_string(font, Point2(view_state.dragged_guide_pos.x + 10, editor->ruler_width_scaled + text_size.y / 2 + 10), str, HORIZONTAL_ALIGNMENT_LEFT, -1, font_size, text_color);
		viewport->draw_line(Point2(view_state.dragged_guide_pos.x, 0), Point2(view_state.dragged_guide_pos.x, viewport->get_size().y), guide_color, Math::round(EDSCALE));
	}
	if (view_state.drag_type == CanvasItemEditorViewState::DRAG_DOUBLE_GUIDE || view_state.drag_type == CanvasItemEditorViewState::DRAG_H_GUIDE) {
		String str = TranslationServer::get_singleton()->format_number(vformat("%d px", Math::round(xform.affine_inverse().xform(view_state.dragged_guide_pos).y)), lang);
		Ref<Font> font = get_theme_font(SNAME("bold"), EditorStringName(EditorFonts));
		int font_size = 1.3 * get_theme_font_size(SNAME("bold_size"), EditorStringName(EditorFonts));
		Size2 text_size = font->get_string_size(str, HORIZONTAL_ALIGNMENT_LEFT, -1, font_size);
		viewport->draw_string_outline(font, Point2(editor->ruler_width_scaled + 10, view_state.dragged_guide_pos.y + text_size.y / 2 + 10), str, HORIZONTAL_ALIGNMENT_LEFT, -1, font_size, outline_size, outline_color);
		viewport->draw_string(font, Point2(editor->ruler_width_scaled + 10, view_state.dragged_guide_pos.y + text_size.y / 2 + 10), str, HORIZONTAL_ALIGNMENT_LEFT, -1, font_size, text_color);
		viewport->draw_line(Point2(0, view_state.dragged_guide_pos.y), Point2(viewport->get_size().x, view_state.dragged_guide_pos.y), guide_color, Math::round(EDSCALE));
	}
}

void CanvasItemEditorView::_draw_smart_snapping() {
	Color line_color = EDITOR_GET("editors/2d/smart_snapping_line_color");
	if (view_state.snap_target[0] != CanvasItemEditorViewState::SNAP_TARGET_NONE && view_state.snap_target[0] != CanvasItemEditorViewState::SNAP_TARGET_GRID) {
		viewport->draw_set_transform_matrix(viewport->get_transform() * view_state.transform * view_state.snap_transform);
		viewport->draw_line(Point2(0, -1.0e+10F), Point2(0, 1.0e+10F), line_color);
		viewport->draw_set_transform_matrix(viewport->get_transform());
	}
	if (view_state.snap_target[1] != CanvasItemEditorViewState::SNAP_TARGET_NONE && view_state.snap_target[1] != CanvasItemEditorViewState::SNAP_TARGET_GRID) {
		viewport->draw_set_transform_matrix(viewport->get_transform() * view_state.transform * view_state.snap_transform);
		viewport->draw_line(Point2(-1.0e+10F, 0), Point2(1.0e+10F, 0), line_color);
		viewport->draw_set_transform_matrix(viewport->get_transform());
	}
}

void CanvasItemEditorView::_draw_rulers() {
	Color bg_color = get_theme_color(SNAME("ruler_color"), EditorStringName(Editor));
	Color graduation_color = get_theme_color(SceneStringName(font_color), EditorStringName(Editor)).lerp(bg_color, 0.5);
	Color font_color = get_theme_color(SceneStringName(font_color), EditorStringName(Editor));
	font_color.a = 0.9;
	Ref<Font> font = get_theme_font(SNAME("rulers"), EditorStringName(EditorFonts));
	real_t ruler_tick_scale = editor->ruler_width_scaled / 15.0;
	const String lang = _get_locale();

	// The rule view_state.transform
	Transform2D ruler_transform;
	if (editor->grid_snap_active || editor->_is_grid_visible()) {
		List<CanvasItem *> selection = editor->_get_edited_canvas_items();
		if (editor->snap_relative && selection.size() > 0) {
			ruler_transform.translate_local(editor->_get_encompassing_rect_from_list(selection).position);
			ruler_transform.scale_basis(editor->grid_step * Math::pow(2.0, editor->grid_step_multiplier));
		} else {
			ruler_transform.translate_local(editor->grid_offset);
			ruler_transform.scale_basis(editor->grid_step * Math::pow(2.0, editor->grid_step_multiplier));
		}
		while ((view_state.transform * ruler_transform).get_scale().x < 50.0 * ruler_tick_scale || (view_state.transform * ruler_transform).get_scale().y < 50.0 * ruler_tick_scale) {
			ruler_transform.scale_basis(Point2(2, 2));
		}
	} else {
		real_t basic_rule = 100;
		for (int i = 0; basic_rule * view_state.zoom > 100 * ruler_tick_scale; i++) {
			basic_rule /= (i % 2) ? 5.0 : 2.0;
		}
		for (int i = 0; basic_rule * view_state.zoom < 60 * ruler_tick_scale; i++) {
			basic_rule *= (i % 2) ? 2.0 : 5.0;
		}
		ruler_transform.scale(Size2(basic_rule, basic_rule));
	}

	// Subdivisions
	int major_subdivision = 2;
	Transform2D major_subdivide;
	major_subdivide.scale(Size2(1.0 / major_subdivision, 1.0 / major_subdivision));

	int minor_subdivision = 5;
	Transform2D minor_subdivide;
	minor_subdivide.scale(Size2(1.0 / minor_subdivision, 1.0 / minor_subdivision));

	// First and last graduations to draw (in the ruler space)
	Point2 first = (view_state.transform * ruler_transform * major_subdivide * minor_subdivide).affine_inverse().xform(Point2(editor->ruler_width_scaled, editor->ruler_width_scaled));
	Point2 last = (view_state.transform * ruler_transform * major_subdivide * minor_subdivide).affine_inverse().xform(viewport->get_size());

	// Draw top ruler
	viewport->draw_rect(Rect2(Point2(editor->ruler_width_scaled, 0), Size2(viewport->get_size().x, editor->ruler_width_scaled)), bg_color);
	for (int i = Math::ceil(first.x); i < last.x; i++) {
		Point2 position = (view_state.transform * ruler_transform * major_subdivide * minor_subdivide).xform(Point2(i, 0)).round();
		if (i % (major_subdivision * minor_subdivision) == 0) {
			viewport->draw_line(Point2(position.x, 0), Point2(position.x, editor->ruler_width_scaled), graduation_color, Math::round(EDSCALE));
			real_t val = (ruler_transform * major_subdivide * minor_subdivide).xform(Point2(i, 0)).x;
			const String &formatted = TranslationServer::get_singleton()->format_number(vformat(((int)val == val) ? "%d" : "%.1f", val), lang);
			viewport->draw_string(font, Point2(position.x + MAX(Math::round(editor->ruler_font_size / 8.0), 2), font->get_ascent(editor->ruler_font_size) + Math::round(EDSCALE)), formatted, HORIZONTAL_ALIGNMENT_LEFT, -1, editor->ruler_font_size, font_color);
		} else {
			if (i % minor_subdivision == 0) {
				viewport->draw_line(Point2(position.x, editor->ruler_width_scaled * 0.33), Point2(position.x, editor->ruler_width_scaled), graduation_color, Math::round(EDSCALE));
			} else {
				viewport->draw_line(Point2(position.x, editor->ruler_width_scaled * 0.75), Point2(position.x, editor->ruler_width_scaled), graduation_color, Math::round(EDSCALE));
			}
		}
	}

	// Draw left ruler
	viewport->draw_rect(Rect2(Point2(0, editor->ruler_width_scaled), Size2(editor->ruler_width_scaled, viewport->get_size().y)), bg_color);
	for (int i = Math::ceil(first.y); i < last.y; i++) {
		Point2 position = (view_state.transform * ruler_transform * major_subdivide * minor_subdivide).xform(Point2(0, i)).round();
		if (i % (major_subdivision * minor_subdivision) == 0) {
			viewport->draw_line(Point2(0, position.y), Point2(editor->ruler_width_scaled, position.y), graduation_color, Math::round(EDSCALE));
			real_t val = (ruler_transform * major_subdivide * minor_subdivide).xform(Point2(0, i)).y;

			Transform2D text_xform = Transform2D(-Math::PI / 2.0, Point2(font->get_ascent(editor->ruler_font_size) + Math::round(EDSCALE), position.y - 2));
			viewport->draw_set_transform_matrix(viewport->get_transform() * text_xform);
			const String &formatted = TranslationServer::get_singleton()->format_number(vformat(((int)val == val) ? "%d" : "%.1f", val), lang);
			viewport->draw_string(font, Point2(), formatted, HORIZONTAL_ALIGNMENT_LEFT, -1, editor->ruler_font_size, font_color);
			viewport->draw_set_transform_matrix(viewport->get_transform());

		} else {
			if (i % minor_subdivision == 0) {
				viewport->draw_line(Point2(editor->ruler_width_scaled * 0.33, position.y), Point2(editor->ruler_width_scaled, position.y), graduation_color, Math::round(EDSCALE));
			} else {
				viewport->draw_line(Point2(editor->ruler_width_scaled * 0.75, position.y), Point2(editor->ruler_width_scaled, position.y), graduation_color, Math::round(EDSCALE));
			}
		}
	}

	// Draw the top left corner
	viewport->draw_rect(Rect2(Point2(), Size2(editor->ruler_width_scaled, editor->ruler_width_scaled)), graduation_color);
}

void CanvasItemEditorView::_draw_grid() {
	if (editor->_is_grid_visible()) {
		// Draw the grid
		Vector2 real_grid_offset;
		const List<CanvasItem *> selection = editor->_get_edited_canvas_items();

		if (editor->snap_relative && selection.size() > 0) {
			const Vector2 topleft = editor->_get_encompassing_rect_from_list(selection).position;
			real_grid_offset.x = std::fmod(topleft.x, editor->grid_step.x * (real_t)Math::pow(2.0, editor->grid_step_multiplier));
			real_grid_offset.y = std::fmod(topleft.y, editor->grid_step.y * (real_t)Math::pow(2.0, editor->grid_step_multiplier));
		} else {
			real_grid_offset = editor->grid_offset;
		}

		// Draw a "primary" line every several lines to make measurements easier.
		// The step is configurable in the Configure Snap dialog.
		const Color secondary_grid_color = EDITOR_GET("editors/2d/grid_color");
		const Color primary_grid_color =
				Color(secondary_grid_color.r, secondary_grid_color.g, secondary_grid_color.b, secondary_grid_color.a * 2.5);

		const Size2 viewport_size = viewport->get_size();
		const Transform2D xform = view_state.transform.affine_inverse();
		int last_cell = 0;

		if (editor->grid_step.x != 0) {
			for (int i = 0; i < viewport_size.width; i++) {
				const int cell =
						Math::fast_ftoi(Math::floor((xform.xform(Vector2(i, 0)).x - real_grid_offset.x) / (editor->grid_step.x * Math::pow(2.0, editor->grid_step_multiplier))));

				if (i == 0) {
					last_cell = cell;
				}

				if (last_cell != cell) {
					Color grid_color;
					if (editor->primary_grid_step.x <= 1) {
						grid_color = secondary_grid_color;
					} else {
						grid_color = cell % editor->primary_grid_step.x == 0 ? primary_grid_color : secondary_grid_color;
					}

					viewport->draw_line(Point2(i, 0), Point2(i, viewport_size.height), grid_color, Math::round(EDSCALE));
				}
				last_cell = cell;
			}
		}

		if (editor->grid_step.y != 0) {
			for (int i = 0; i < viewport_size.height; i++) {
				const int cell =
						Math::fast_ftoi(Math::floor((xform.xform(Vector2(0, i)).y - real_grid_offset.y) / (editor->grid_step.y * Math::pow(2.0, editor->grid_step_multiplier))));

				if (i == 0) {
					last_cell = cell;
				}

				if (last_cell != cell) {
					Color grid_color;
					if (editor->primary_grid_step.y <= 1) {
						grid_color = secondary_grid_color;
					} else {
						grid_color = cell % editor->primary_grid_step.y == 0 ? primary_grid_color : secondary_grid_color;
					}

					viewport->draw_line(Point2(0, i), Point2(viewport_size.width, i), grid_color, Math::round(EDSCALE));
				}
				last_cell = cell;
			}
		}
	}
}

void CanvasItemEditorView::_draw_ruler_tool() {
	if (editor->tool != CanvasItemEditor::TOOL_RULER) {
		return;
	}

	const Ref<Texture2D> position_icon = get_editor_theme_icon(SNAME("EditorPosition"));
	if (view_state.ruler_tool_active) {
		const String &lang = _get_locale();
		const TranslationServer *ts = TranslationServer::get_singleton();

		Color ruler_primary_color = get_theme_color(SNAME("accent_color"), EditorStringName(Editor));
		Color ruler_secondary_color = ruler_primary_color;
		ruler_secondary_color.a = 0.5;

		Point2 begin = (view_state.ruler_tool_origin - view_state.view_offset) * view_state.zoom;
		Point2 end = editor->snap_point(viewport->get_local_mouse_position() / view_state.zoom + view_state.view_offset) * view_state.zoom - view_state.view_offset * view_state.zoom;
		Point2 corner = Point2(begin.x, end.y);
		Vector2 length_vector = (begin - end).abs() / view_state.zoom;

		const real_t horizontal_angle_rad = length_vector.angle();
		const real_t vertical_angle_rad = Math::PI / 2.0 - horizontal_angle_rad;

		Ref<Font> font = get_theme_font(SNAME("bold"), EditorStringName(EditorFonts));
		int font_size = 1.3 * get_theme_font_size(SNAME("bold_size"), EditorStringName(EditorFonts));
		Color font_color = get_theme_color(SceneStringName(font_color), EditorStringName(Editor));
		Color font_secondary_color = font_color;
		font_secondary_color.set_v(font_secondary_color.get_v() > 0.5 ? 0.7 : 0.3);
		Color outline_color = font_color.inverted();
		float text_height = font->get_height(font_size);

		const float outline_size = 4;
		const float text_width = 76;
		const float angle_text_width = 54;

		Point2 text_pos = (begin + end) / 2 - Vector2(text_width / 2, text_height / 2);
		text_pos.x = CLAMP(text_pos.x, text_width / 2, viewport->get_rect().size.x - text_width * 1.5);
		text_pos.y = CLAMP(text_pos.y, text_height * 1.5, viewport->get_rect().size.y - text_height * 1.5);

		// Draw lines.
		viewport->draw_line(begin, end, ruler_primary_color, Math::round(EDSCALE * 3));

		bool draw_secondary_lines = !(Math::is_equal_approx(begin.y, corner.y) || Math::is_equal_approx(end.x, corner.x));
		if (draw_secondary_lines) {
			viewport->draw_line(begin, corner, ruler_secondary_color, Math::round(EDSCALE));
			viewport->draw_line(corner, end, ruler_secondary_color, Math::round(EDSCALE));

			// Angle arcs.
			int arc_point_count = 8;
			real_t arc_radius_max_length_percent = 0.1;
			real_t ruler_length = length_vector.length() * view_state.zoom;
			real_t arc_max_radius = 50.0;
			real_t arc_line_width = 2.0;

			const Vector2 end_to_begin = (end - begin);

			real_t arc_1_start_angle = end_to_begin.x < 0
					? (end_to_begin.y < 0 ? 3.0 * Math::PI / 2.0 - vertical_angle_rad : Math::PI / 2.0)
					: (end_to_begin.y < 0 ? 3.0 * Math::PI / 2.0 : Math::PI / 2.0 - vertical_angle_rad);
			real_t arc_1_end_angle = arc_1_start_angle + vertical_angle_rad;
			// Constrain arc to triangle height & max size.
			real_t arc_1_radius = MIN(MIN(arc_radius_max_length_percent * ruler_length, Math::abs(end_to_begin.y)), arc_max_radius);

			real_t arc_2_start_angle = end_to_begin.x < 0
					? (end_to_begin.y < 0 ? 0.0 : -horizontal_angle_rad)
					: (end_to_begin.y < 0 ? Math::PI - horizontal_angle_rad : Math::PI);
			real_t arc_2_end_angle = arc_2_start_angle + horizontal_angle_rad;
			// Constrain arc to triangle width & max size.
			real_t arc_2_radius = MIN(MIN(arc_radius_max_length_percent * ruler_length, Math::abs(end_to_begin.x)), arc_max_radius);

			viewport->draw_arc(begin, arc_1_radius, arc_1_start_angle, arc_1_end_angle, arc_point_count, ruler_primary_color, Math::round(EDSCALE * arc_line_width));
			viewport->draw_arc(end, arc_2_radius, arc_2_start_angle, arc_2_end_angle, arc_point_count, ruler_primary_color, Math::round(EDSCALE * arc_line_width));
		}

		// Draw text.
		if (begin.is_equal_approx(end)) {
			viewport->draw_string_outline(font, text_pos, (String)view_state.ruler_tool_origin, HORIZONTAL_ALIGNMENT_LEFT, -1, font_size, outline_size, outline_color);
			viewport->draw_string(font, text_pos, (String)view_state.ruler_tool_origin, HORIZONTAL_ALIGNMENT_LEFT, -1, font_size, font_color);
			viewport->draw_texture(position_icon, (view_state.ruler_tool_origin - view_state.view_offset) * view_state.zoom - position_icon->get_size() / 2);
			return;
		}

		viewport->draw_string_outline(font, text_pos, ts->format_number(vformat("%.1f px", length_vector.length()), lang), HORIZONTAL_ALIGNMENT_LEFT, -1, font_size, outline_size, outline_color);
		viewport->draw_string(font, text_pos, ts->format_number(vformat("%.1f px", length_vector.length()), lang), HORIZONTAL_ALIGNMENT_LEFT, -1, font_size, font_color);

		if (draw_secondary_lines) {
			const int horizontal_angle = std::round(180 * horizontal_angle_rad / Math::PI);
			const int vertical_angle = std::round(180 * vertical_angle_rad / Math::PI);

			Point2 text_pos2 = text_pos;
			text_pos2.x = begin.x < text_pos.x ? MIN(text_pos.x - text_width, begin.x - text_width / 2) : MAX(text_pos.x + text_width, begin.x - text_width / 2);
			viewport->draw_string_outline(font, text_pos2, ts->format_number(vformat("%.1f px", length_vector.y), lang), HORIZONTAL_ALIGNMENT_LEFT, -1, font_size, outline_size, outline_color);
			viewport->draw_string(font, text_pos2, ts->format_number(vformat("%.1f px", length_vector.y), lang), HORIZONTAL_ALIGNMENT_LEFT, -1, font_size, font_secondary_color);

			Point2 v_angle_text_pos;
			v_angle_text_pos.x = CLAMP(begin.x - angle_text_width / 2, angle_text_width / 2, viewport->get_rect().size.x - angle_text_width);
			v_angle_text_pos.y = begin.y < end.y ? MIN(text_pos2.y - 2 * text_height, begin.y - text_height * 0.5) : MAX(text_pos2.y + text_height * 3, begin.y + text_height * 1.5);
			viewport->draw_string_outline(font, v_angle_text_pos, ts->format_number(vformat(U"%d°", vertical_angle), lang), HORIZONTAL_ALIGNMENT_LEFT, -1, font_size, outline_size, outline_color);
			viewport->draw_string(font, v_angle_text_pos, ts->format_number(vformat(U"%d°", vertical_angle), lang), HORIZONTAL_ALIGNMENT_LEFT, -1, font_size, font_secondary_color);

			text_pos2 = text_pos;
			text_pos2.y = end.y < text_pos.y ? MIN(text_pos.y - text_height * 2, end.y - text_height / 2) : MAX(text_pos.y + text_height * 2, end.y - text_height / 2);
			viewport->draw_string_outline(font, text_pos2, ts->format_number(vformat("%.1f px", length_vector.x), lang), HORIZONTAL_ALIGNMENT_LEFT, -1, font_size, outline_size, outline_color);
			viewport->draw_string(font, text_pos2, ts->format_number(vformat("%.1f px", length_vector.x), lang), HORIZONTAL_ALIGNMENT_LEFT, -1, font_size, font_secondary_color);

			Point2 h_angle_text_pos;
			h_angle_text_pos.x = CLAMP(end.x - angle_text_width / 2, angle_text_width / 2, viewport->get_rect().size.x - angle_text_width);
			if (begin.y < end.y) {
				h_angle_text_pos.y = end.y + text_height * 1.5;
				if (Math::abs(text_pos2.x - h_angle_text_pos.x) < text_width) {
					int height_multiplier = 1.5 + (int)editor->grid_snap_active;
					h_angle_text_pos.y = MAX(text_pos.y + height_multiplier * text_height, MAX(end.y + text_height * 1.5, text_pos2.y + height_multiplier * text_height));
				}
			} else {
				h_angle_text_pos.y = end.y - text_height * 0.5;
				if (Math::abs(text_pos2.x - h_angle_text_pos.x) < text_width) {
					int height_multiplier = 1 + (int)editor->grid_snap_active;
					h_angle_text_pos.y = MIN(text_pos.y - height_multiplier * text_height, MIN(end.y - text_height * 0.5, text_pos2.y - height_multiplier * text_height));
				}
			}
			viewport->draw_string_outline(font, h_angle_text_pos, ts->format_number(vformat(U"%d°", horizontal_angle), lang), HORIZONTAL_ALIGNMENT_LEFT, -1, font_size, outline_size, outline_color);
			viewport->draw_string(font, h_angle_text_pos, ts->format_number(vformat(U"%d°", horizontal_angle), lang), HORIZONTAL_ALIGNMENT_LEFT, -1, font_size, font_secondary_color);
		}

		if (editor->grid_snap_active) {
			text_pos = (begin + end) / 2 + Vector2(-text_width / 2, text_height / 2);
			text_pos.x = CLAMP(text_pos.x, text_width / 2, viewport->get_rect().size.x - text_width * 1.5);
			text_pos.y = CLAMP(text_pos.y, text_height * 2.5, viewport->get_rect().size.y - text_height / 2);

			if (draw_secondary_lines) {
				viewport->draw_string_outline(font, text_pos, ts->format_number(vformat("%.2f " + TTR("units"), (length_vector / editor->grid_step).length()), lang), HORIZONTAL_ALIGNMENT_LEFT, -1, font_size, outline_size, outline_color);
				viewport->draw_string(font, text_pos, ts->format_number(vformat("%.2f " + TTR("units"), (length_vector / editor->grid_step).length()), lang), HORIZONTAL_ALIGNMENT_LEFT, -1, font_size, font_color);

				Point2 text_pos2 = text_pos;
				text_pos2.x = begin.x < text_pos.x ? MIN(text_pos.x - text_width, begin.x - text_width / 2) : MAX(text_pos.x + text_width, begin.x - text_width / 2);
				viewport->draw_string_outline(font, text_pos2, ts->format_number(vformat("%d " + TTR("units"), std::round(length_vector.y / editor->grid_step.y)), lang), HORIZONTAL_ALIGNMENT_LEFT, -1, font_size, outline_size, outline_color);
				viewport->draw_string(font, text_pos2, ts->format_number(vformat("%d " + TTR("units"), std::round(length_vector.y / editor->grid_step.y)), lang), HORIZONTAL_ALIGNMENT_LEFT, -1, font_size, font_secondary_color);

				text_pos2 = text_pos;
				text_pos2.y = end.y < text_pos.y ? MIN(text_pos.y - text_height * 2, end.y + text_height / 2) : MAX(text_pos.y + text_height * 2, end.y + text_height / 2);
				viewport->draw_string_outline(font, text_pos2, ts->format_number(vformat("%d " + TTR("units"), std::round(length_vector.x / editor->grid_step.x)), lang), HORIZONTAL_ALIGNMENT_LEFT, -1, font_size, outline_size, outline_color);
				viewport->draw_string(font, text_pos2, ts->format_number(vformat("%d " + TTR("units"), std::round(length_vector.x / editor->grid_step.x)), lang), HORIZONTAL_ALIGNMENT_LEFT, -1, font_size, font_secondary_color);
			} else {
				viewport->draw_string_outline(font, text_pos, ts->format_number(vformat("%d " + TTR("units"), std::round((length_vector / editor->grid_step).length())), lang), HORIZONTAL_ALIGNMENT_LEFT, -1, font_size, outline_size, outline_color);
				viewport->draw_string(font, text_pos, ts->format_number(vformat("%d " + TTR("units"), std::round((length_vector / editor->grid_step).length())), lang), HORIZONTAL_ALIGNMENT_LEFT, -1, font_size, font_color);
			}
		}
	} else {
		if (editor->grid_snap_active) {
			viewport->draw_texture(position_icon, (view_state.ruler_tool_origin - view_state.view_offset) * view_state.zoom - position_icon->get_size() / 2);
		}
	}
}

void CanvasItemEditorView::_draw_control_anchors(Control *control) {
	Transform2D xform = view_state.transform * control->get_screen_transform();
	RID ci = viewport->get_canvas_item();
	if (editor->tool == CanvasItemEditor::TOOL_SELECT && !Object::cast_to<Container>(control->get_parent())) {
		// Compute the anchors
		real_t anchors_values[4];
		anchors_values[0] = control->get_anchor(SIDE_LEFT);
		anchors_values[1] = control->get_anchor(SIDE_TOP);
		anchors_values[2] = control->get_anchor(SIDE_RIGHT);
		anchors_values[3] = control->get_anchor(SIDE_BOTTOM);

		Vector2 anchors_pos[4];
		for (int i = 0; i < 4; i++) {
			Vector2 value = Vector2((i % 2 == 0) ? anchors_values[i] : anchors_values[(i + 1) % 4], (i % 2 == 1) ? anchors_values[i] : anchors_values[(i + 1) % 4]);
			anchors_pos[i] = xform.xform(editor->_anchor_to_position(control, value));
		}

		// Draw the anchors handles
		Rect2 anchor_rects[4];
		if (control->is_layout_rtl()) {
			anchor_rects[0] = Rect2(anchors_pos[0] - Vector2(0.0, editor->anchor_handle->get_size().y), Point2(-editor->anchor_handle->get_size().x, editor->anchor_handle->get_size().y));
			anchor_rects[1] = Rect2(anchors_pos[1] - editor->anchor_handle->get_size(), editor->anchor_handle->get_size());
			anchor_rects[2] = Rect2(anchors_pos[2] - Vector2(editor->anchor_handle->get_size().x, 0.0), Point2(editor->anchor_handle->get_size().x, -editor->anchor_handle->get_size().y));
			anchor_rects[3] = Rect2(anchors_pos[3], -editor->anchor_handle->get_size());
		} else {
			anchor_rects[0] = Rect2(anchors_pos[0] - editor->anchor_handle->get_size(), editor->anchor_handle->get_size());
			anchor_rects[1] = Rect2(anchors_pos[1] - Vector2(0.0, editor->anchor_handle->get_size().y), Point2(-editor->anchor_handle->get_size().x, editor->anchor_handle->get_size().y));
			anchor_rects[2] = Rect2(anchors_pos[2], -editor->anchor_handle->get_size());
			anchor_rects[3] = Rect2(anchors_pos[3] - Vector2(editor->anchor_handle->get_size().x, 0.0), Point2(editor->anchor_handle->get_size().x, -editor->anchor_handle->get_size().y));
		}

		for (int i = 0; i < 4; i++) {
			editor->anchor_handle->draw_rect(ci, anchor_rects[i]);
		}
	}
}

void CanvasItemEditorView::_draw_control_helpers(Control *control) {
	Transform2D xform = view_state.transform * control->get_screen_transform();
	if (editor->tool == CanvasItemEditor::TOOL_SELECT && editor->show_helpers && !Object::cast_to<Container>(control->get_parent())) {
		// Draw the helpers
		Color color_base = Color(0.8, 0.8, 0.8, 0.5);

		// Compute the anchors
		real_t anchors_values[4];
		anchors_values[0] = control->get_anchor(SIDE_LEFT);
		anchors_values[1] = control->get_anchor(SIDE_TOP);
		anchors_values[2] = control->get_anchor(SIDE_RIGHT);
		anchors_values[3] = control->get_anchor(SIDE_BOTTOM);

		Vector2 anchors[4];
		Vector2 anchors_pos[4];
		for (int i = 0; i < 4; i++) {
			anchors[i] = Vector2((i % 2 == 0) ? anchors_values[i] : anchors_values[(i + 1) % 4], (i % 2 == 1) ? anchors_values[i] : anchors_values[(i + 1) % 4]);
			anchors_pos[i] = xform.xform(editor->_anchor_to_position(control, anchors[i]));
		}

		// Get which anchor is dragged
		int dragged_anchor = -1;
		switch (view_state.drag_type) {
			case CanvasItemEditorViewState::DRAG_ANCHOR_ALL:
			case CanvasItemEditorViewState::DRAG_ANCHOR_TOP_LEFT:
				dragged_anchor = 0;
				break;
			case CanvasItemEditorViewState::DRAG_ANCHOR_TOP_RIGHT:
				dragged_anchor = 1;
				break;
			case CanvasItemEditorViewState::DRAG_ANCHOR_BOTTOM_RIGHT:
				dragged_anchor = 2;
				break;
			case CanvasItemEditorViewState::DRAG_ANCHOR_BOTTOM_LEFT:
				dragged_anchor = 3;
				break;
			default:
				break;
		}

		if (dragged_anchor >= 0) {
			// Draw the 4 lines when dragged
			Color color_snapped = Color(0.64, 0.93, 0.67, 0.5);

			Vector2 corners_pos[4];
			for (int i = 0; i < 4; i++) {
				corners_pos[i] = xform.xform(editor->_anchor_to_position(control, Vector2((i == 0 || i == 3) ? ANCHOR_BEGIN : ANCHOR_END, (i <= 1) ? ANCHOR_BEGIN : ANCHOR_END)));
			}

			Vector2 line_starts[4];
			Vector2 line_ends[4];
			for (int i = 0; i < 4; i++) {
				real_t anchor_val = (i >= 2) ? (real_t)ANCHOR_END - anchors_values[i] : anchors_values[i];
				line_starts[i] = corners_pos[i].lerp(corners_pos[(i + 1) % 4], anchor_val);
				line_ends[i] = corners_pos[(i + 3) % 4].lerp(corners_pos[(i + 2) % 4], anchor_val);
				bool anchor_snapped = anchors_values[i] == 0.0 || anchors_values[i] == 0.5 || anchors_values[i] == 1.0;
				viewport->draw_line(line_starts[i], line_ends[i], anchor_snapped ? color_snapped : color_base, (i == dragged_anchor || (i + 3) % 4 == dragged_anchor) ? 2 : 1);
			}

			// Display the percentages next to the lines
			real_t percent_val;
			percent_val = anchors_values[(dragged_anchor + 2) % 4] - anchors_values[dragged_anchor];
			percent_val = (dragged_anchor >= 2) ? -percent_val : percent_val;
			_draw_percentage_at_position(percent_val, (anchors_pos[dragged_anchor] + anchors_pos[(dragged_anchor + 1) % 4]) / 2, (Side)((dragged_anchor + 1) % 4));

			percent_val = anchors_values[(dragged_anchor + 3) % 4] - anchors_values[(dragged_anchor + 1) % 4];
			percent_val = ((dragged_anchor + 1) % 4 >= 2) ? -percent_val : percent_val;
			_draw_percentage_at_position(percent_val, (anchors_pos[dragged_anchor] + anchors_pos[(dragged_anchor + 3) % 4]) / 2, (Side)(dragged_anchor));

			percent_val = anchors_values[(dragged_anchor + 1) % 4];
			percent_val = ((dragged_anchor + 1) % 4 >= 2) ? (real_t)ANCHOR_END - percent_val : percent_val;
			_draw_percentage_at_position(percent_val, (line_starts[dragged_anchor] + anchors_pos[dragged_anchor]) / 2, (Side)(dragged_anchor));

			percent_val = anchors_values[dragged_anchor];
			percent_val = (dragged_anchor >= 2) ? (real_t)ANCHOR_END - percent_val : percent_val;
			_draw_percentage_at_position(percent_val, (line_ends[(dragged_anchor + 1) % 4] + anchors_pos[dragged_anchor]) / 2, (Side)((dragged_anchor + 1) % 4));
		}

		// Draw the margin values and the node width/height when dragging control side
		const real_t ratio = 0.33;
		Transform2D parent_transform = xform * control->get_transform().affine_inverse();
		real_t node_pos_in_parent[4];

		Rect2 parent_rect = control->get_parent_anchorable_rect();

		node_pos_in_parent[0] = control->get_anchor(SIDE_LEFT) * parent_rect.size.width + control->get_offset(SIDE_LEFT) + parent_rect.position.x;
		node_pos_in_parent[1] = control->get_anchor(SIDE_TOP) * parent_rect.size.height + control->get_offset(SIDE_TOP) + parent_rect.position.y;
		node_pos_in_parent[2] = control->get_anchor(SIDE_RIGHT) * parent_rect.size.width + control->get_offset(SIDE_RIGHT) + parent_rect.position.x;
		node_pos_in_parent[3] = control->get_anchor(SIDE_BOTTOM) * parent_rect.size.height + control->get_offset(SIDE_BOTTOM) + parent_rect.position.y;

		Point2 start, end;
		switch (view_state.drag_type) {
			case CanvasItemEditorViewState::DRAG_LEFT:
			case CanvasItemEditorViewState::DRAG_TOP_LEFT:
			case CanvasItemEditorViewState::DRAG_BOTTOM_LEFT:
				_draw_margin_at_position(control->get_size().width, parent_transform.xform(Vector2((node_pos_in_parent[0] + node_pos_in_parent[2]) / 2, node_pos_in_parent[3])) + Vector2(0, 5), SIDE_BOTTOM);
				[[fallthrough]];
			case CanvasItemEditorViewState::DRAG_MOVE:
				start = Vector2(node_pos_in_parent[0], Math::lerp(node_pos_in_parent[1], node_pos_in_parent[3], ratio));
				end = start - Vector2(control->get_offset(SIDE_LEFT), 0);
				_draw_margin_at_position(control->get_offset(SIDE_LEFT), parent_transform.xform((start + end) / 2), SIDE_TOP);
				viewport->draw_line(parent_transform.xform(start), parent_transform.xform(end), color_base, Math::round(EDSCALE));
				break;
			default:
				break;
		}
		switch (view_state.drag_type) {
			case CanvasItemEditorViewState::DRAG_RIGHT:
			case CanvasItemEditorViewState::DRAG_TOP_RIGHT:
			case CanvasItemEditorViewState::DRAG_BOTTOM_RIGHT:
				_draw_margin_at_position(control->get_size().width, parent_transform.xform(Vector2((node_pos_in_parent[0] + node_pos_in_parent[2]) / 2, node_pos_in_parent[3])) + Vector2(0, 5), SIDE_BOTTOM);
				[[fallthrough]];
			case CanvasItemEditorViewState::DRAG_MOVE:
				start = Vector2(node_pos_in_parent[2], Math::lerp(node_pos_in_parent[3], node_pos_in_parent[1], ratio));
				end = start - Vector2(control->get_offset(SIDE_RIGHT), 0);
				_draw_margin_at_position(control->get_offset(SIDE_RIGHT), parent_transform.xform((start + end) / 2), SIDE_BOTTOM);
				viewport->draw_line(parent_transform.xform(start), parent_transform.xform(end), color_base, Math::round(EDSCALE));
				break;
			default:
				break;
		}
		switch (view_state.drag_type) {
			case CanvasItemEditorViewState::DRAG_TOP:
			case CanvasItemEditorViewState::DRAG_TOP_LEFT:
			case CanvasItemEditorViewState::DRAG_TOP_RIGHT:
				_draw_margin_at_position(control->get_size().height, parent_transform.xform(Vector2(node_pos_in_parent[2], (node_pos_in_parent[1] + node_pos_in_parent[3]) / 2)) + Vector2(5, 0), SIDE_RIGHT);
				[[fallthrough]];
			case CanvasItemEditorViewState::DRAG_MOVE:
				start = Vector2(Math::lerp(node_pos_in_parent[0], node_pos_in_parent[2], ratio), node_pos_in_parent[1]);
				end = start - Vector2(0, control->get_offset(SIDE_TOP));
				_draw_margin_at_position(control->get_offset(SIDE_TOP), parent_transform.xform((start + end) / 2), SIDE_LEFT);
				viewport->draw_line(parent_transform.xform(start), parent_transform.xform(end), color_base, Math::round(EDSCALE));
				break;
			default:
				break;
		}
		switch (view_state.drag_type) {
			case CanvasItemEditorViewState::DRAG_BOTTOM:
			case CanvasItemEditorViewState::DRAG_BOTTOM_LEFT:
			case CanvasItemEditorViewState::DRAG_BOTTOM_RIGHT:
				_draw_margin_at_position(control->get_size().height, parent_transform.xform(Vector2(node_pos_in_parent[2], (node_pos_in_parent[1] + node_pos_in_parent[3]) / 2) + Vector2(5, 0)), SIDE_RIGHT);
				[[fallthrough]];
			case CanvasItemEditorViewState::DRAG_MOVE:
				start = Vector2(Math::lerp(node_pos_in_parent[2], node_pos_in_parent[0], ratio), node_pos_in_parent[3]);
				end = start - Vector2(0, control->get_offset(SIDE_BOTTOM));
				_draw_margin_at_position(control->get_offset(SIDE_BOTTOM), parent_transform.xform((start + end) / 2), SIDE_RIGHT);
				viewport->draw_line(parent_transform.xform(start), parent_transform.xform(end), color_base, Math::round(EDSCALE));
				break;
			default:
				break;
		}

		switch (view_state.drag_type) {
			//Draw the ghost rect if the node if rotated/scaled
			case CanvasItemEditorViewState::DRAG_LEFT:
			case CanvasItemEditorViewState::DRAG_TOP_LEFT:
			case CanvasItemEditorViewState::DRAG_TOP:
			case CanvasItemEditorViewState::DRAG_TOP_RIGHT:
			case CanvasItemEditorViewState::DRAG_RIGHT:
			case CanvasItemEditorViewState::DRAG_BOTTOM_RIGHT:
			case CanvasItemEditorViewState::DRAG_BOTTOM:
			case CanvasItemEditorViewState::DRAG_BOTTOM_LEFT:
			case CanvasItemEditorViewState::DRAG_MOVE:
				if (control->get_rotation() != 0.0 || control->get_scale() != Vector2(1, 1)) {
					Rect2 rect = Rect2(Vector2(node_pos_in_parent[0], node_pos_in_parent[1]), control->get_size());
					viewport->draw_rect(parent_transform.xform(rect), color_base, false, Math::round(EDSCALE));
				}
				break;
			default:
				break;
		}
	}
}

void CanvasItemEditorView::_draw_selection() {
	Ref<Texture2D> pivot_icon = get_editor_theme_icon(SNAME("EditorPivot"));
	Ref<Texture2D> position_icon = get_editor_theme_icon(SNAME("EditorPosition"));
	Ref<Texture2D> previous_position_icon = get_editor_theme_icon(SNAME("EditorPositionPrevious"));

	RID vp_ci = viewport->get_canvas_item();
	List<CanvasItem *> selection = editor->_get_edited_canvas_items(true, false);
	bool single = selection.size() == 1;
	bool transform_tool = editor->tool == CanvasItemEditor::TOOL_SELECT || editor->tool == CanvasItemEditor::TOOL_MOVE || editor->tool == CanvasItemEditor::TOOL_SCALE || editor->tool == CanvasItemEditor::TOOL_ROTATE || editor->tool == CanvasItemEditor::TOOL_EDIT_PIVOT;

	for (CanvasItem *E : selection) {
		CanvasItem *ci = Object::cast_to<CanvasItem>(E);
		CanvasItemEditorSelectedItem *se = editor->editor_selection->get_node_editor_data<CanvasItemEditorSelectedItem>(ci);

		// Draw the previous position if we are dragging the node
		if (editor->show_helpers &&
				(view_state.drag_type == CanvasItemEditorViewState::DRAG_MOVE || view_state.drag_type == CanvasItemEditorViewState::DRAG_ROTATE ||
						view_state.drag_type == CanvasItemEditorViewState::DRAG_LEFT || view_state.drag_type == CanvasItemEditorViewState::DRAG_RIGHT || view_state.drag_type == CanvasItemEditorViewState::DRAG_TOP || view_state.drag_type == CanvasItemEditorViewState::DRAG_BOTTOM ||
						view_state.drag_type == CanvasItemEditorViewState::DRAG_TOP_LEFT || view_state.drag_type == CanvasItemEditorViewState::DRAG_TOP_RIGHT || view_state.drag_type == CanvasItemEditorViewState::DRAG_BOTTOM_LEFT || view_state.drag_type == CanvasItemEditorViewState::DRAG_BOTTOM_RIGHT)) {
			const Transform2D pre_drag_xform = view_state.transform * se->pre_drag_xform;
			const Color pre_drag_color = Color(0.4, 0.6, 1, 0.7);

			if (ci->_edit_use_rect()) {
				Vector2 pre_drag_endpoints[4] = {
					pre_drag_xform.xform(se->pre_drag_rect.position),
					pre_drag_xform.xform(se->pre_drag_rect.position + Vector2(se->pre_drag_rect.size.x, 0)),
					pre_drag_xform.xform(se->pre_drag_rect.position + se->pre_drag_rect.size),
					pre_drag_xform.xform(se->pre_drag_rect.position + Vector2(0, se->pre_drag_rect.size.y))
				};

				for (int i = 0; i < 4; i++) {
					viewport->draw_line(pre_drag_endpoints[i], pre_drag_endpoints[(i + 1) % 4], pre_drag_color, Math::round(2 * EDSCALE));
				}
			} else {
				viewport->draw_texture(previous_position_icon, (pre_drag_xform.xform(Point2()) - (previous_position_icon->get_size() / 2)).floor());
			}
		}

		bool item_locked = ci->has_meta("_edit_lock_");
		Transform2D xform = view_state.transform * ci->get_screen_transform();

		// Draw the selected items position / surrounding boxes
		if (ci->_edit_use_rect()) {
			Rect2 rect = ci->_edit_get_rect();
			const Vector2 endpoints[4] = {
				xform.xform(rect.position),
				xform.xform(rect.position + Vector2(rect.size.x, 0)),
				xform.xform(rect.position + rect.size),
				xform.xform(rect.position + Vector2(0, rect.size.y))
			};

			Color c = Color(1, 0.6, 0.4, 0.7);

			if (item_locked) {
				c = Color(0.7, 0.7, 0.7, 0.7);
			}

			for (int i = 0; i < 4; i++) {
				viewport->draw_line(endpoints[i], endpoints[(i + 1) % 4], c, Math::round(2 * EDSCALE));
			}
		} else {
			Transform2D unscaled_transform = (xform * ci->get_transform().affine_inverse() * ci->_edit_get_transform()).orthonormalized();
			Transform2D simple_xform;
			if (editor->use_local_space) {
				simple_xform = viewport->get_transform() * unscaled_transform;
			} else {
				Transform2D translation = Transform2D(0.0f, unscaled_transform.get_origin());
				simple_xform = viewport->get_transform() * translation;
			}

			viewport->draw_set_transform_matrix(simple_xform);
			viewport->draw_texture(position_icon, -(position_icon->get_size() / 2));
			viewport->draw_set_transform_matrix(viewport->get_transform());
		}

		if (single && !item_locked && transform_tool) {
			// Draw the pivot
			if (ci->_edit_use_pivot()) {
				// Draw the node's pivot
				Transform2D unscaled_transform = (xform * ci->get_transform().affine_inverse() * ci->_edit_get_transform()).orthonormalized();
				Transform2D simple_xform;
				if (editor->use_local_space) {
					simple_xform = viewport->get_transform() * unscaled_transform;
				} else {
					Transform2D translation = Transform2D(0.0f, unscaled_transform.get_origin());
					simple_xform = viewport->get_transform() * translation;
				}

				viewport->draw_set_transform_matrix(simple_xform);
				viewport->draw_texture(pivot_icon, -(pivot_icon->get_size() / 2).floor());
				viewport->draw_set_transform_matrix(viewport->get_transform());
			}

			// Draw control-related helpers
			Control *control = Object::cast_to<Control>(ci);
			if (control && editor->_is_node_movable(control)) {
				_draw_control_anchors(control);
				_draw_control_helpers(control);
			}

			// Draw the resize handles
			if (editor->tool == CanvasItemEditor::TOOL_SELECT && ci->_edit_use_rect() && editor->_is_node_movable(ci)) {
				Rect2 rect = ci->_edit_get_rect();
				const Vector2 endpoints[4] = {
					xform.xform(rect.position),
					xform.xform(rect.position + Vector2(rect.size.x, 0)),
					xform.xform(rect.position + rect.size),
					xform.xform(rect.position + Vector2(0, rect.size.y))
				};
				for (int i = 0; i < 4; i++) {
					int prev = (i + 3) % 4;
					int next = (i + 1) % 4;

					Vector2 ofs = ((endpoints[i] - endpoints[prev]).normalized() + ((endpoints[i] - endpoints[next]).normalized())).normalized();
					ofs *= Math::SQRT2 * (editor->select_handle->get_size().width / 2);

					editor->select_handle->draw(vp_ci, (endpoints[i] + ofs - (editor->select_handle->get_size() / 2)).floor());

					ofs = (endpoints[i] + endpoints[next]) / 2;
					ofs += (endpoints[next] - endpoints[i]).orthogonal().normalized() * (editor->select_handle->get_size().width / 2);

					editor->select_handle->draw(vp_ci, (ofs - (editor->select_handle->get_size() / 2)).floor());
				}
			}
		}
	}

	// Remove non-movable nodes.
	for (List<CanvasItem *>::Element *E = selection.front(); E;) {
		List<CanvasItem *>::Element *N = E->next();
		if (!editor->_is_node_movable(E->get())) {
			selection.erase(E);
		}
		E = N;
	}

	if (!selection.is_empty() && transform_tool && editor->show_transformation_gizmos) {
		CanvasItem *ci = selection.front()->get();

		Transform2D xform = view_state.transform * ci->get_screen_transform();
		bool is_ctrl = Input::get_singleton()->is_key_pressed(Key::CMD_OR_CTRL);
		bool is_alt = Input::get_singleton()->is_key_pressed(Key::ALT);

		// Draw the move handles.
		if ((editor->tool == CanvasItemEditor::TOOL_SELECT && is_alt && !is_ctrl) || editor->tool == CanvasItemEditor::TOOL_MOVE) {
			Transform2D unscaled_transform = (xform * ci->get_transform().affine_inverse() * ci->_edit_get_transform()).orthonormalized();
			Transform2D simple_xform;
			if (editor->use_local_space) {
				simple_xform = viewport->get_transform() * unscaled_transform;
			} else {
				Transform2D translation = Transform2D(0.0f, unscaled_transform.get_origin());
				simple_xform = viewport->get_transform() * translation;
			}

			Size2 move_factor = Size2(MOVE_HANDLE_DISTANCE, MOVE_HANDLE_DISTANCE);
			viewport->draw_set_transform_matrix(simple_xform);

			Vector<Point2> points = {
				Vector2(move_factor.x * EDSCALE, 5 * EDSCALE),
				Vector2(move_factor.x * EDSCALE, -5 * EDSCALE),
				Vector2((move_factor.x + 10) * EDSCALE, 0)
			};

			viewport->draw_colored_polygon(points, get_theme_color(SNAME("axis_x_color"), EditorStringName(Editor)));
			viewport->draw_line(Point2(), Point2(move_factor.x * EDSCALE, 0), get_theme_color(SNAME("axis_x_color"), EditorStringName(Editor)), Math::round(EDSCALE));

			points.clear();
			points.push_back(Vector2(5 * EDSCALE, move_factor.y * EDSCALE));
			points.push_back(Vector2(-5 * EDSCALE, move_factor.y * EDSCALE));
			points.push_back(Vector2(0, (move_factor.y + 10) * EDSCALE));

			viewport->draw_colored_polygon(points, get_theme_color(SNAME("axis_y_color"), EditorStringName(Editor)));
			viewport->draw_line(Point2(), Point2(0, move_factor.y * EDSCALE), get_theme_color(SNAME("axis_y_color"), EditorStringName(Editor)), Math::round(EDSCALE));

			viewport->draw_set_transform_matrix(viewport->get_transform());
		}

		// Draw the rescale handles.
		if ((editor->tool == CanvasItemEditor::TOOL_SELECT && is_alt && is_ctrl) || editor->tool == CanvasItemEditor::TOOL_SCALE || view_state.drag_type == CanvasItemEditorViewState::DRAG_SCALE_X || view_state.drag_type == CanvasItemEditorViewState::DRAG_SCALE_Y) {
			Transform2D edit_transform;
			if (!Math::is_inf(view_state.temp_pivot.x) || !Math::is_inf(view_state.temp_pivot.y)) {
				edit_transform = Transform2D(ci->_edit_get_rotation(), view_state.temp_pivot);
			} else {
				edit_transform = ci->_edit_get_transform();
			}
			Transform2D unscaled_transform = (xform * ci->get_transform().affine_inverse() * edit_transform).orthonormalized();
			Transform2D simple_xform;
			if (editor->use_local_space) {
				simple_xform = viewport->get_transform() * unscaled_transform;
			} else {
				Transform2D translation = Transform2D(0.0f, unscaled_transform.get_origin());
				simple_xform = viewport->get_transform() * translation;
			}

			Size2 scale_factor = Size2(SCALE_HANDLE_DISTANCE, SCALE_HANDLE_DISTANCE);
			bool uniform = Input::get_singleton()->is_key_pressed(Key::SHIFT);
			Point2 offset = (simple_xform.affine_inverse().xform(view_state.drag_to) - simple_xform.affine_inverse().xform(view_state.drag_from)) * view_state.zoom;

			if (view_state.drag_type == CanvasItemEditorViewState::DRAG_SCALE_X) {
				scale_factor.x += offset.x;
				if (uniform) {
					scale_factor.y += offset.x;
				}
			} else if (view_state.drag_type == CanvasItemEditorViewState::DRAG_SCALE_Y) {
				scale_factor.y += offset.y;
				if (uniform) {
					scale_factor.x += offset.y;
				}
			}

			viewport->draw_set_transform_matrix(simple_xform);
			Rect2 x_handle_rect = Rect2(scale_factor.x * EDSCALE, -5 * EDSCALE, 10 * EDSCALE, 10 * EDSCALE);
			viewport->draw_rect(x_handle_rect, get_theme_color(SNAME("axis_x_color"), EditorStringName(Editor)));
			viewport->draw_line(Point2(), Point2(scale_factor.x * EDSCALE, 0), get_theme_color(SNAME("axis_x_color"), EditorStringName(Editor)), Math::round(EDSCALE));

			Rect2 y_handle_rect = Rect2(-5 * EDSCALE, scale_factor.y * EDSCALE, 10 * EDSCALE, 10 * EDSCALE);
			viewport->draw_rect(y_handle_rect, get_theme_color(SNAME("axis_y_color"), EditorStringName(Editor)));
			viewport->draw_line(Point2(), Point2(0, scale_factor.y * EDSCALE), get_theme_color(SNAME("axis_y_color"), EditorStringName(Editor)), Math::round(EDSCALE));

			viewport->draw_set_transform_matrix(viewport->get_transform());
		}
	}

	if (view_state.drag_type == CanvasItemEditorViewState::DRAG_BOX_SELECTION) {
		// Draw the dragging box
		Point2 bsfrom = view_state.transform.xform(view_state.drag_from);
		Point2 bsto = view_state.transform.xform(view_state.box_selecting_to);

		viewport->draw_rect(
				Rect2(bsfrom, bsto - bsfrom),
				get_theme_color(SNAME("box_selection_fill_color"), EditorStringName(Editor)));

		viewport->draw_rect(
				Rect2(bsfrom, bsto - bsfrom),
				get_theme_color(SNAME("box_selection_stroke_color"), EditorStringName(Editor)),
				false,
				Math::round(EDSCALE));
	}

	if (view_state.drag_type == CanvasItemEditorViewState::DRAG_ROTATE) {
		// Draw the line when rotating a node
		viewport->draw_line(
				view_state.transform.xform(view_state.drag_rotation_center),
				view_state.transform.xform(view_state.drag_to),
				get_theme_color(SNAME("accent_color"), EditorStringName(Editor)) * Color(1, 1, 1, 0.6),
				Math::round(2 * EDSCALE));
	}

	if (!Math::is_inf(view_state.temp_pivot.x) || !Math::is_inf(view_state.temp_pivot.y)) {
		viewport->draw_texture(pivot_icon, (view_state.temp_pivot - view_state.view_offset) * view_state.zoom - (pivot_icon->get_size() / 2).floor(), get_theme_color(SNAME("accent_color"), EditorStringName(Editor)));
	}
}

void CanvasItemEditorView::_draw_straight_line(Point2 p_from, Point2 p_to, Color p_color) {
	// Draw a line going through the whole screen from a vector
	RID ci = viewport->get_canvas_item();
	Vector<Point2> points;
	Point2 from = view_state.transform.xform(p_from);
	Point2 to = view_state.transform.xform(p_to);
	Size2 viewport_size = viewport->get_size();

	if (to.x == from.x) {
		// Vertical line
		points.push_back(Point2(to.x, 0));
		points.push_back(Point2(to.x, viewport_size.y));
	} else if (to.y == from.y) {
		// Horizontal line
		points.push_back(Point2(0, to.y));
		points.push_back(Point2(viewport_size.x, to.y));
	} else {
		real_t y_for_zero_x = (to.y * from.x - from.y * to.x) / (from.x - to.x);
		real_t x_for_zero_y = (to.x * from.y - from.x * to.y) / (from.y - to.y);
		real_t y_for_viewport_x = ((to.y - from.y) * (viewport_size.x - from.x)) / (to.x - from.x) + from.y;
		real_t x_for_viewport_y = ((to.x - from.x) * (viewport_size.y - from.y)) / (to.y - from.y) + from.x; // faux

		//bool start_set = false;
		if (y_for_zero_x >= 0 && y_for_zero_x <= viewport_size.y) {
			points.push_back(Point2(0, y_for_zero_x));
		}
		if (x_for_zero_y >= 0 && x_for_zero_y <= viewport_size.x) {
			points.push_back(Point2(x_for_zero_y, 0));
		}
		if (y_for_viewport_x >= 0 && y_for_viewport_x <= viewport_size.y) {
			points.push_back(Point2(viewport_size.x, y_for_viewport_x));
		}
		if (x_for_viewport_y >= 0 && x_for_viewport_y <= viewport_size.x) {
			points.push_back(Point2(x_for_viewport_y, viewport_size.y));
		}
	}
	if (points.size() >= 2) {
		RenderingServer::get_singleton()->canvas_item_add_line(ci, points[0], points[1], p_color);
	}
}

void CanvasItemEditorView::_draw_axis() {
	if (editor->show_origin) {
		_draw_straight_line(Point2(), Point2(1, 0), get_theme_color(SNAME("axis_x_color"), EditorStringName(Editor)) * Color(1, 1, 1, 0.75));
		_draw_straight_line(Point2(), Point2(0, 1), get_theme_color(SNAME("axis_y_color"), EditorStringName(Editor)) * Color(1, 1, 1, 0.75));
	}

	if (editor->show_viewport) {
		RID ci = viewport->get_canvas_item();

		Color area_axis_color = EDITOR_GET("editors/2d/viewport_border_color");

		Size2 screen_size = Size2(GLOBAL_GET("display/window/size/viewport_width"), GLOBAL_GET("display/window/size/viewport_height"));

		Vector2 screen_endpoints[4] = {
			view_state.transform.xform(Vector2(0, 0)),
			view_state.transform.xform(Vector2(screen_size.width, 0)),
			view_state.transform.xform(Vector2(screen_size.width, screen_size.height)),
			view_state.transform.xform(Vector2(0, screen_size.height))
		};

		for (int i = 0; i < 4; i++) {
			RenderingServer::get_singleton()->canvas_item_add_line(ci, screen_endpoints[i], screen_endpoints[(i + 1) % 4], area_axis_color);
		}
	}
}

void CanvasItemEditorView::_draw_invisible_nodes_positions(Node *p_node, const Transform2D &p_parent_xform, const Transform2D &p_canvas_xform) {
	ERR_FAIL_NULL(p_node);

	Node *scene = get_edited_scene();
	if (p_node != scene && p_node->get_owner() != scene && !scene->is_editable_instance(p_node->get_owner())) {
		return;
	}
	CanvasItem *ci = Object::cast_to<CanvasItem>(p_node);
	if (ci && !ci->is_visible_in_tree()) {
		return;
	}

	Transform2D parent_xform = p_parent_xform;
	Transform2D canvas_xform = p_canvas_xform;

	if (ci && !ci->is_set_as_top_level()) {
		parent_xform = parent_xform * ci->get_transform();
	} else if (CanvasLayer *cl = Object::cast_to<CanvasLayer>(p_node)) {
		parent_xform = Transform2D();
		canvas_xform = cl->get_transform();
	} else if (Viewport *vp = Object::cast_to<Viewport>(p_node)) {
		if (!vp->is_visible_subviewport()) {
			return;
		}
		parent_xform = Transform2D();
		canvas_xform = vp->get_popup_base_transform();
	}

	for (int i = p_node->get_child_count() - 1; i >= 0; i--) {
		_draw_invisible_nodes_positions(p_node->get_child(i), parent_xform, canvas_xform);
	}

	if (editor->show_position_gizmos && ci && !ci->_edit_use_rect() && (!editor->editor_selection->is_selected(ci) || editor->_is_node_locked(ci))) {
		Transform2D xform = view_state.transform * canvas_xform * parent_xform;

		// Draw the node's position
		Ref<Texture2D> position_icon = get_editor_theme_icon(SNAME("EditorPositionUnselected"));
		Transform2D unscaled_transform = (xform * ci->get_transform().affine_inverse() * ci->_edit_get_transform()).orthonormalized();
		Transform2D simple_xform;
		if (editor->use_local_space) {
			simple_xform = viewport->get_transform() * unscaled_transform;
		} else {
			Transform2D translation = Transform2D(0.0f, unscaled_transform.get_origin());
			simple_xform = viewport->get_transform() * translation;
		}

		viewport->draw_set_transform_matrix(simple_xform);
		viewport->draw_texture(position_icon, -position_icon->get_size() / 2, Color(1.0, 1.0, 1.0, 0.5));
		viewport->draw_set_transform_matrix(viewport->get_transform());
	}
}

void CanvasItemEditorView::_draw_hover() {
	List<Rect2> previous_rects;
	Vector2 icon_size = Vector2(1, 1) * get_theme_constant(SNAME("class_icon_size"), EditorStringName(Editor));

	for (int i = 0; i < view_state.hovering_results.size(); i++) {
		Ref<Texture2D> node_icon = view_state.hovering_results[i].icon;
		String node_name = view_state.hovering_results[i].name;

		Ref<Font> font = get_theme_font(SceneStringName(font), SNAME("Label"));
		int font_size = get_theme_font_size(SceneStringName(font_size), SNAME("Label"));
		Size2 node_name_size = font->get_string_size(node_name, HORIZONTAL_ALIGNMENT_LEFT, -1, font_size);
		Size2 item_size = Size2(icon_size.x + 4 + node_name_size.x, MAX(icon_size.y, node_name_size.y - 3));

		Point2 pos = view_state.transform.xform(view_state.hovering_results[i].position) - Point2(0, item_size.y) + (Point2(icon_size.x, -icon_size.y) / 4);
		// Rectify the position to avoid overlapping items
		for (const Rect2 &E : previous_rects) {
			if (E.intersects(Rect2(pos, item_size))) {
				pos.y = E.get_position().y - item_size.y;
			}
		}

		previous_rects.push_back(Rect2(pos, item_size));

		// Draw icon
		viewport->draw_texture_rect(node_icon, Rect2(pos, icon_size), false, Color(1.0, 1.0, 1.0, 0.5));

		// Draw name
		viewport->draw_string(font, pos + Point2(icon_size.x + 4, item_size.y - 3), node_name, HORIZONTAL_ALIGNMENT_LEFT, -1, font_size, Color(1.0, 1.0, 1.0, 0.5));
	}
}

void CanvasItemEditorView::_draw_message() {
	if (view_state.drag_type != CanvasItemEditorViewState::DRAG_NONE && !view_state.drag_selection.is_empty() && view_state.drag_selection.front()->get()) {
		Transform2D current_transform = view_state.drag_selection.front()->get()->get_global_transform();

		double snap = EDITOR_GET("interface/inspector/default_float_step");
		int snap_step_decimals = Math::range_step_decimals(snap);
		const String &lang = _get_locale();
#define FORMAT(value) (TranslationServer::get_singleton()->format_number(String::num(value, snap_step_decimals), lang))

		switch (view_state.drag_type) {
			case CanvasItemEditorViewState::DRAG_MOVE:
			case CanvasItemEditorViewState::DRAG_MOVE_X:
			case CanvasItemEditorViewState::DRAG_MOVE_Y: {
				Vector2 delta = current_transform.get_origin() - view_state.original_transform.get_origin();
				if (view_state.drag_type == CanvasItemEditorViewState::DRAG_MOVE || editor->use_local_space) {
					view_state.message = TTR("Moving:") + " (" + FORMAT(delta.x) + ", " + FORMAT(delta.y) + ") px";
				} else if (view_state.drag_type == CanvasItemEditorViewState::DRAG_MOVE_X) {
					view_state.message = TTR("Moving:") + " " + FORMAT(delta.x) + " px";
				} else if (view_state.drag_type == CanvasItemEditorViewState::DRAG_MOVE_Y) {
					view_state.message = TTR("Moving:") + " " + FORMAT(delta.y) + " px";
				}
			} break;

			case CanvasItemEditorViewState::DRAG_ROTATE: {
				real_t delta = Math::rad_to_deg(current_transform.get_rotation() - view_state.original_transform.get_rotation());
				view_state.message = TTR("Rotating:") + " " + FORMAT(delta) + String::utf8(" °");
			} break;

			case CanvasItemEditorViewState::DRAG_SCALE_X:
			case CanvasItemEditorViewState::DRAG_SCALE_Y:
			case CanvasItemEditorViewState::DRAG_SCALE_BOTH: {
				Vector2 original_scale = (Math::is_zero_approx(view_state.original_transform.get_scale().x) || Math::is_zero_approx(view_state.original_transform.get_scale().y)) ? Vector2(CMP_EPSILON, CMP_EPSILON) : view_state.original_transform.get_scale();
				Vector2 delta = current_transform.get_scale() / original_scale;
				if (view_state.drag_type == CanvasItemEditorViewState::DRAG_SCALE_BOTH || !editor->use_local_space) {
					view_state.message = TTR("Scaling:") + String::utf8(" ×(") + FORMAT(delta.x) + ", " + FORMAT(delta.y) + ")";
				} else if (view_state.drag_type == CanvasItemEditorViewState::DRAG_SCALE_X) {
					view_state.message = TTR("Scaling:") + String::utf8(" ×") + FORMAT(delta.x);
				} else if (view_state.drag_type == CanvasItemEditorViewState::DRAG_SCALE_Y) {
					view_state.message = TTR("Scaling:") + String::utf8(" ×") + FORMAT(delta.y);
				}
			} break;

			default:
				break;
		}
#undef FORMAT
	}

	if (view_state.message.is_empty()) {
		return;
	}

	Ref<Font> font = get_theme_font(SceneStringName(font), SNAME("Label"));
	int font_size = get_theme_font_size(SceneStringName(font_size), SNAME("Label"));
	Point2 msgpos = Point2(editor->ruler_width_scaled + 10 * EDSCALE, viewport->get_size().y - 14 * EDSCALE);
	viewport->draw_string(font, msgpos + Point2(1, 1), view_state.message, HORIZONTAL_ALIGNMENT_LEFT, -1, font_size, Color(0, 0, 0, 0.8));
	viewport->draw_string(font, msgpos + Point2(-1, -1), view_state.message, HORIZONTAL_ALIGNMENT_LEFT, -1, font_size, Color(0, 0, 0, 0.8));
	viewport->draw_string(font, msgpos, view_state.message, HORIZONTAL_ALIGNMENT_LEFT, -1, font_size, Color(1, 1, 1, 1));
}

void CanvasItemEditorView::_draw_locks_and_groups(Node *p_node, const Transform2D &p_parent_xform, const Transform2D &p_canvas_xform) {
	ERR_FAIL_NULL(p_node);

	Node *scene = get_edited_scene();
	if (p_node != scene && p_node->get_owner() != scene && !scene->is_editable_instance(p_node->get_owner())) {
		return;
	}
	CanvasItem *ci = Object::cast_to<CanvasItem>(p_node);
	if (ci && !ci->is_visible_in_tree()) {
		return;
	}

	Transform2D parent_xform = p_parent_xform;
	Transform2D canvas_xform = p_canvas_xform;

	if (ci && !ci->is_set_as_top_level()) {
		parent_xform = parent_xform * ci->get_transform();
	} else if (CanvasLayer *cl = Object::cast_to<CanvasLayer>(p_node)) {
		parent_xform = Transform2D();
		canvas_xform = cl->get_transform();
	} else if (Viewport *vp = Object::cast_to<Viewport>(p_node)) {
		if (!vp->is_visible_subviewport()) {
			return;
		}
		parent_xform = Transform2D();
		canvas_xform = vp->get_popup_base_transform();
	}

	for (int i = p_node->get_child_count() - 1; i >= 0; i--) {
		_draw_locks_and_groups(p_node->get_child(i), parent_xform, canvas_xform);
	}

	RID viewport_ci = viewport->get_canvas_item();
	if (ci) {
		real_t offset = 0;

		Ref<Texture2D> lock = get_editor_theme_icon(SNAME("LockViewport"));
		if (editor->show_lock_gizmos && p_node->has_meta("_edit_lock_")) {
			lock->draw(viewport_ci, (view_state.transform * canvas_xform * parent_xform).xform(Point2(0, 0)) + Point2(offset, 0));
			offset += lock->get_size().x;
		}

		Ref<Texture2D> group = get_editor_theme_icon(SNAME("GroupViewport"));
		if (editor->show_group_gizmos && ci->has_meta("_edit_group_")) {
			group->draw(viewport_ci, (view_state.transform * canvas_xform * parent_xform).xform(Point2(0, 0)) + Point2(offset, 0));
			//offset += group->get_size().x;
		}
	}
}

void CanvasItemEditorView::_draw_viewport() {
	CanvasItemEditorViewMath::update_canvas_transform(view_state);
	get_scene_viewport()->set_global_canvas_transform(view_state.transform);

	_draw_grid();
	_draw_ruler_tool();
	_draw_axis();
	if (get_edited_scene()) {
		_draw_locks_and_groups(get_edited_scene());
		_draw_invisible_nodes_positions(get_edited_scene());
	}
	_draw_selection();

	RID ci = viewport->get_canvas_item();
	RenderingServer::get_singleton()->canvas_item_add_set_transform(ci, Transform2D());

	if (plugin_forwarding_target) {
		EditorNode::get_singleton()->get_editor_plugins_over()->forward_canvas_draw_over_viewport(viewport);
		EditorNode::get_singleton()->get_editor_plugins_force_over()->forward_canvas_force_draw_over_viewport(viewport);
	}

	if (editor->show_rulers) {
		_draw_rulers();
	}
	if (editor->show_guides) {
		_draw_guides();
	}
	_draw_smart_snapping();
	_draw_focus();
	_draw_hover();
	_draw_message();
}

void CanvasItemEditorView::update_viewport() {
	test_update_viewport_invocations++;
	if (!viewport) {
		return;
	}
	_update_scrollbars();
	viewport->queue_redraw();
}

void CanvasItemEditorView::_update_scrollbars() {
	view_state.updating_scroll = true;

	// Move the view_state.zoom buttons.
	Point2 controls_vb_begin = Point2(5, 5);
	controls_vb_begin += (editor->show_rulers) ? Point2(editor->ruler_width_scaled, editor->ruler_width_scaled) : Point2();
	controls_vb->set_begin(controls_vb_begin);

	Size2 hmin = h_scroll->get_minimum_size();
	Size2 vmin = v_scroll->get_minimum_size();

	// Get the visible frame.
	Size2 screen_rect = Size2(GLOBAL_GET("display/window/size/viewport_width"), GLOBAL_GET("display/window/size/viewport_height"));
	Rect2 local_rect = Rect2(Point2(), viewport->get_size() - Size2(vmin.width, hmin.height));

	// Calculate scrollable area.
	Rect2 canvas_item_rect = Rect2(Point2(), screen_rect);
	if (EditorNode::get_singleton()->is_inside_tree() && get_edited_scene()) {
		Rect2 content_rect = editor->_get_encompassing_rect(get_edited_scene());
		canvas_item_rect.expand_to(content_rect.position);
		canvas_item_rect.expand_to(content_rect.position + content_rect.size);
	}
	canvas_item_rect.size += screen_rect * 2;
	canvas_item_rect.position -= screen_rect;

	// Updates the scrollbars.
	const Size2 size = viewport->get_size();
	const Point2 begin = canvas_item_rect.position;
	const Point2 end = canvas_item_rect.position + canvas_item_rect.size - local_rect.size / view_state.zoom;

	if (canvas_item_rect.size.height <= (local_rect.size.y / view_state.zoom)) {
		v_scroll->hide();
	} else {
		v_scroll->show();
		v_scroll->set_min(MIN(view_state.view_offset.y, begin.y));
		v_scroll->set_max(MAX(view_state.view_offset.y, end.y) + screen_rect.y);
		v_scroll->set_page(screen_rect.y);
	}

	if (canvas_item_rect.size.width <= (local_rect.size.x / view_state.zoom)) {
		h_scroll->hide();
	} else {
		h_scroll->show();
		h_scroll->set_min(MIN(view_state.view_offset.x, begin.x));
		h_scroll->set_max(MAX(view_state.view_offset.x, end.x) + screen_rect.x);
		h_scroll->set_page(screen_rect.x);
	}

	// Move and resize the scrollbars, avoiding overlap.
	if (is_layout_rtl()) {
		v_scroll->set_begin(Point2(0, (editor->show_rulers) ? editor->ruler_width_scaled : 0));
		v_scroll->set_end(Point2(vmin.width, size.height - (h_scroll->is_visible() ? hmin.height : 0)));
	} else {
		v_scroll->set_begin(Point2(size.width - vmin.width, (editor->show_rulers) ? editor->ruler_width_scaled : 0));
		v_scroll->set_end(Point2(size.width, size.height - (h_scroll->is_visible() ? hmin.height : 0)));
	}
	h_scroll->set_begin(Point2((editor->show_rulers) ? editor->ruler_width_scaled : 0, size.height - hmin.height));
	h_scroll->set_end(Point2(size.width - (v_scroll->is_visible() ? vmin.width : 0), size.height));

	// Calculate scrollable area.
	v_scroll->set_value(view_state.view_offset.y);
	h_scroll->set_value(view_state.view_offset.x);

	view_state.previous_update_view_offset = view_state.view_offset;
	view_state.updating_scroll = false;
}

void CanvasItemEditorView::_update_scroll(real_t) {
	if (view_state.updating_scroll) {
		return;
	}

	view_state.view_offset.x = h_scroll->get_value();
	view_state.view_offset.y = v_scroll->get_value();
	viewport->queue_redraw();
}

void CanvasItemEditorView::_zoom_on_position(real_t p_zoom, Point2 p_position) {
	p_zoom = CLAMP(p_zoom, zoom_widget->get_min_zoom(), zoom_widget->get_max_zoom());

	if (!CanvasItemEditorViewMath::apply_zoom_at_point(view_state, p_zoom, p_position)) {
		return;
	}

	zoom_widget->set_zoom(view_state.zoom);
	update_viewport();
	if (editor->auto_resampling_enabled) {
		editor->resample_timer->start();
	}
}

void CanvasItemEditorView::_update_zoom(real_t p_zoom) {
	_zoom_on_position(p_zoom, viewport_scrollable->get_size() / 2.0);
}

void CanvasItemEditorView::_update_oversampling() {
	get_scene_viewport()->set_oversampling_override(editor->auto_resampling_enabled ? view_state.zoom : 0.0);
}

void CanvasItemEditorView::_shortcut_zoom_set(real_t p_zoom) {
	_zoom_on_position(p_zoom * MAX(1, EDSCALE), viewport->get_local_mouse_position());
}

void CanvasItemEditorView::center_at(const Point2 &p_pos) {
	CanvasItemEditorViewMath::center_view_on_point(
			view_state,
			p_pos,
			viewport->get_size(),
			get_scene_viewport()->get_global_canvas_transform());
	update_viewport();
}

void CanvasItemEditorView::update_scrollbars() {
	_update_scrollbars();
}

void CanvasItemEditorView::update_oversampling() {
	_update_oversampling();
}

void CanvasItemEditorView::commit_drag() {
	_commit_drag();
}

void CanvasItemEditorView::update_cursor() {
	_update_cursor();
}

void CanvasItemEditorView::update_panner_from_settings() {
	ERR_FAIL_NULL(editor);
	editor->simple_panning = EDITOR_GET("editors/panning/simple_panning");
	panner->setup((ViewPanner::ControlScheme)EDITOR_GET("editors/panning/2d_editor_panning_scheme").operator int(), ED_GET_SHORTCUT("canvas_item_editor/pan_view"), editor->simple_panning);
	panner->set_scroll_speed(EDITOR_GET("editors/panning/2d_editor_pan_speed"));
	panner->setup_warped_panning(editor->get_viewport(), EDITOR_GET("editors/panning/warped_mouse_panning"));
	panner->set_zoom_style((ViewPanner::ZoomStyle)EDITOR_GET("editors/panning/zoom_style").operator int());
}

void CanvasItemEditorView::update_center_button_icon(const Ref<Texture2D> &p_icon) {
	if (button_center_view) {
		button_center_view->set_button_icon(p_icon);
	}
}
