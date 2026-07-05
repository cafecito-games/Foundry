/**************************************************************************/
/*  canvas_item_editor_plugin.cpp                                         */
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

#include "canvas_item_editor_plugin.h"

#include "canvas_item_editor_view.h"

#include "core/config/project_settings.h"
#include "core/input/input.h"
#include "core/os/keyboard.h"
#include "core/string/translation_server.h"
#include "editor/animation/animation_player_editor_plugin.h"
#include "editor/debugger/editor_debugger_node.h"
#include "editor/docks/scene_tree_dock.h"
#include "editor/editor_main_screen.h"
#include "editor/editor_node.h"
#include "editor/editor_string_names.h"
#include "editor/editor_undo_redo_manager.h"
#include "editor/gui/editor_toaster.h"
#include "editor/gui/editor_zoom_widget.h"
#include "editor/inspector/editor_context_menu_plugin.h"
#include "editor/plugins/editor_plugin_list.h"
#include "editor/run/editor_run_bar.h"
#include "editor/script/script_editor_plugin.h"
#include "editor/settings/editor_settings.h"
#include "editor/themes/editor_scale.h"
#include "editor/themes/editor_theme_manager.h"
#include "editor/translations/editor_translation_preview_button.h"
#include "editor/translations/editor_translation_preview_menu.h"
#include "scene/2d/audio_stream_player_2d.h"
#include "scene/2d/physics/touch_screen_button.h"
#include "scene/2d/polygon_2d.h"
#include "scene/2d/skeleton_2d.h"
#include "scene/2d/sprite_2d.h"
#include "scene/gui/base_button.h"
#include "scene/gui/flow_container.h"
#include "scene/gui/grid_container.h"
#include "scene/gui/separator.h"
#include "scene/gui/split_container.h"
#include "scene/gui/subviewport_container.h"
#include "scene/gui/view_panner.h"
#include "scene/main/canvas_layer.h"
#include "scene/main/timer.h"
#include "scene/main/window.h"
#include "scene/resources/packed_scene.h"
#include "scene/resources/style_box_texture.h"

CanvasItemEditorViewport *CanvasItemEditor::_get_viewport() const {
	const CanvasItemEditorView *view = get_focused_view();
	if (!view) {
		return nullptr;
	}
	return Object::cast_to<CanvasItemEditorViewport>(view->get_viewport_control());
}

CanvasItemEditorView *CanvasItemEditor::get_focused_view() {
	return CanvasItemEditorViewRouting::get_focused_view(views);
}

const CanvasItemEditorView *CanvasItemEditor::get_focused_view() const {
	return CanvasItemEditorViewRouting::get_focused_view(views);
}

Transform2D CanvasItemEditor::get_canvas_transform() const {
	return CanvasItemEditorViewRouting::get_canvas_transform(views);
}

Control *CanvasItemEditor::get_viewport_control() {
	return CanvasItemEditorViewRouting::get_viewport_control(views);
}

Control *CanvasItemEditor::get_controls_container() {
	return editor_view ? editor_view->get_controls_container() : nullptr;
}

void CanvasItemEditor::update_viewport() {
	CanvasItemEditorViewRouting::update_all_viewports(views);
}

void CanvasItemEditor::center_at(const Point2 &p_pos) {
	if (editor_view) {
		editor_view->center_at(p_pos);
	}
}

void CanvasItemEditor::set_cursor_shape_override(CursorShape p_shape) {
	CanvasItemEditorViewRouting::set_cursor_shape_override(views, p_shape);
}

CanvasItemEditor::CursorShape CanvasItemEditor::get_cursor_shape(const Point2 &p_pos) const {
	if (editor_view) {
		return editor_view->get_cursor_shape(p_pos);
	}
	return CURSOR_ARROW;
}

void CanvasItemEditor::_active_scene_context_changed() {
	if (editor_view) {
		editor_view->active_scene_context_changed();
	}
}

void CanvasItemEditor::_commit_drag() {
	if (editor_view) {
		editor_view->commit_drag();
	}
}

void CanvasItemEditor::_update_oversampling() {
	if (editor_view) {
		editor_view->update_oversampling();
	}
}

void CanvasItemEditor::_update_scrollbars() {
	if (editor_view) {
		editor_view->update_scrollbars();
	}
}

class SnapDialog : public ConfirmationDialog {
	FOUNDRY_CLASS(SnapDialog, ConfirmationDialog);

	friend class CanvasItemEditor;

	SpinBox *grid_offset_x;
	SpinBox *grid_offset_y;
	SpinBox *grid_step_x;
	SpinBox *grid_step_y;
	SpinBox *primary_grid_step_x;
	SpinBox *primary_grid_step_y;
	SpinBox *rotation_offset;
	SpinBox *rotation_step;
	SpinBox *scale_step;

public:
	SnapDialog() {
		const int SPIN_BOX_GRID_RANGE = 16384;
		const int SPIN_BOX_ROTATION_RANGE = 360;
		const real_t SPIN_BOX_SCALE_MIN = 0.01;
		const real_t SPIN_BOX_SCALE_MAX = 100;

		Label *label;
		VBoxContainer *container;
		GridContainer *child_container;

		set_title(TTRC("Configure Snap"));

		container = memnew(VBoxContainer);
		add_child(container);

		child_container = memnew(GridContainer);
		child_container->set_columns(3);
		container->add_child(child_container);

		label = memnew(Label);
		label->set_text(TTRC("Grid Offset:"));
		child_container->add_child(label);
		label->set_h_size_flags(Control::SIZE_EXPAND_FILL);

		grid_offset_x = memnew(SpinBox);
		grid_offset_x->set_min(-SPIN_BOX_GRID_RANGE);
		grid_offset_x->set_max(SPIN_BOX_GRID_RANGE);
		grid_offset_x->set_allow_lesser(true);
		grid_offset_x->set_allow_greater(true);
		grid_offset_x->set_suffix("px");
		grid_offset_x->set_h_size_flags(Control::SIZE_EXPAND_FILL);
		grid_offset_x->set_select_all_on_focus(true);
		grid_offset_x->set_accessibility_name(TTRC("X Offset"));
		child_container->add_child(grid_offset_x);

		grid_offset_y = memnew(SpinBox);
		grid_offset_y->set_min(-SPIN_BOX_GRID_RANGE);
		grid_offset_y->set_max(SPIN_BOX_GRID_RANGE);
		grid_offset_y->set_allow_lesser(true);
		grid_offset_y->set_allow_greater(true);
		grid_offset_y->set_suffix("px");
		grid_offset_y->set_h_size_flags(Control::SIZE_EXPAND_FILL);
		grid_offset_y->set_select_all_on_focus(true);
		grid_offset_y->set_accessibility_name(TTRC("Y Offset"));
		child_container->add_child(grid_offset_y);

		label = memnew(Label);
		label->set_text(TTRC("Grid Step:"));
		child_container->add_child(label);
		label->set_h_size_flags(Control::SIZE_EXPAND_FILL);

		grid_step_x = memnew(SpinBox);
		grid_step_x->set_min(1);
		grid_step_x->set_max(SPIN_BOX_GRID_RANGE);
		grid_step_x->set_allow_greater(true);
		grid_step_x->set_suffix("px");
		grid_step_x->set_h_size_flags(Control::SIZE_EXPAND_FILL);
		grid_step_x->set_select_all_on_focus(true);
		grid_step_x->set_accessibility_name(TTRC("X Step"));
		child_container->add_child(grid_step_x);

		grid_step_y = memnew(SpinBox);
		grid_step_y->set_min(1);
		grid_step_y->set_max(SPIN_BOX_GRID_RANGE);
		grid_step_y->set_allow_greater(true);
		grid_step_y->set_suffix("px");
		grid_step_y->set_h_size_flags(Control::SIZE_EXPAND_FILL);
		grid_step_y->set_select_all_on_focus(true);
		grid_step_y->set_accessibility_name(TTRC("X Step"));
		child_container->add_child(grid_step_y);

		label = memnew(Label);
		label->set_text(TTRC("Primary Line Every:"));
		label->set_h_size_flags(Control::SIZE_EXPAND_FILL);
		child_container->add_child(label);

		primary_grid_step_x = memnew(SpinBox);
		primary_grid_step_x->set_min(1);
		primary_grid_step_x->set_step(1);
		primary_grid_step_x->set_max(SPIN_BOX_GRID_RANGE);
		primary_grid_step_x->set_allow_greater(true);
		primary_grid_step_x->set_suffix("steps");
		primary_grid_step_x->set_h_size_flags(Control::SIZE_EXPAND_FILL);
		primary_grid_step_x->set_select_all_on_focus(true);
		primary_grid_step_x->set_accessibility_name(TTRC("X Primary Step"));
		child_container->add_child(primary_grid_step_x);

		primary_grid_step_y = memnew(SpinBox);
		primary_grid_step_y->set_min(1);
		primary_grid_step_y->set_step(1);
		primary_grid_step_y->set_max(SPIN_BOX_GRID_RANGE);
		primary_grid_step_y->set_allow_greater(true);
		primary_grid_step_y->set_suffix(TTRC("steps")); // TODO: Add suffix auto-translation.
		primary_grid_step_y->set_h_size_flags(Control::SIZE_EXPAND_FILL);
		primary_grid_step_y->set_select_all_on_focus(true);
		primary_grid_step_y->set_accessibility_name(TTRC("Y Primary Step"));
		child_container->add_child(primary_grid_step_y);

		container->add_child(memnew(HSeparator));

		// We need to create another GridContainer with the same column count,
		// so we can put an HSeparator above
		child_container = memnew(GridContainer);
		child_container->set_columns(2);
		container->add_child(child_container);

		label = memnew(Label);
		label->set_text(TTRC("Rotation Offset:"));
		child_container->add_child(label);
		label->set_h_size_flags(Control::SIZE_EXPAND_FILL);

		rotation_offset = memnew(SpinBox);
		rotation_offset->set_min(-SPIN_BOX_ROTATION_RANGE);
		rotation_offset->set_max(SPIN_BOX_ROTATION_RANGE);
		rotation_offset->set_suffix(U"°");
		rotation_offset->set_h_size_flags(Control::SIZE_EXPAND_FILL);
		rotation_offset->set_select_all_on_focus(true);
		rotation_offset->set_accessibility_name(TTRC("Rotation Offset:"));
		child_container->add_child(rotation_offset);

		label = memnew(Label);
		label->set_text(TTRC("Rotation Step:"));
		child_container->add_child(label);
		label->set_h_size_flags(Control::SIZE_EXPAND_FILL);

		rotation_step = memnew(SpinBox);
		rotation_step->set_min(-SPIN_BOX_ROTATION_RANGE);
		rotation_step->set_max(SPIN_BOX_ROTATION_RANGE);
		rotation_step->set_suffix(U"°");
		rotation_step->set_h_size_flags(Control::SIZE_EXPAND_FILL);
		rotation_step->set_select_all_on_focus(true);
		rotation_step->set_accessibility_name(TTRC("Rotation Step:"));
		child_container->add_child(rotation_step);

		container->add_child(memnew(HSeparator));

		child_container = memnew(GridContainer);
		child_container->set_columns(2);
		container->add_child(child_container);
		label = memnew(Label);
		label->set_text(TTRC("Scale Step:"));
		child_container->add_child(label);
		label->set_h_size_flags(Control::SIZE_EXPAND_FILL);

		scale_step = memnew(SpinBox);
		scale_step->set_min(SPIN_BOX_SCALE_MIN);
		scale_step->set_max(SPIN_BOX_SCALE_MAX);
		scale_step->set_allow_greater(true);
		scale_step->set_h_size_flags(Control::SIZE_EXPAND_FILL);
		scale_step->set_step(0.01f);
		scale_step->set_select_all_on_focus(true);
		scale_step->set_accessibility_name(TTRC("Scale Step:"));
		child_container->add_child(scale_step);
	}

	void set_fields(const Point2 p_grid_offset, const Point2 p_grid_step, const Vector2i p_primary_grid_step, const real_t p_rotation_offset, const real_t p_rotation_step, const real_t p_scale_step) {
		grid_offset_x->set_value(p_grid_offset.x);
		grid_offset_y->set_value(p_grid_offset.y);
		grid_step_x->set_value(p_grid_step.x);
		grid_step_y->set_value(p_grid_step.y);
		primary_grid_step_x->set_value(p_primary_grid_step.x);
		primary_grid_step_y->set_value(p_primary_grid_step.y);
		rotation_offset->set_value(Math::rad_to_deg(p_rotation_offset));
		rotation_step->set_value(Math::rad_to_deg(p_rotation_step));
		scale_step->set_value(p_scale_step);
	}

	void get_fields(Point2 &p_grid_offset, Point2 &p_grid_step, Vector2i &p_primary_grid_step, real_t &p_rotation_offset, real_t &p_rotation_step, real_t &p_scale_step) {
		p_grid_offset = Point2(grid_offset_x->get_value(), grid_offset_y->get_value());
		p_grid_step = Point2(grid_step_x->get_value(), grid_step_y->get_value());
		p_primary_grid_step = Vector2i(primary_grid_step_x->get_value(), primary_grid_step_y->get_value());
		p_rotation_offset = Math::deg_to_rad(rotation_offset->get_value());
		p_rotation_step = Math::deg_to_rad(rotation_step->get_value());
		p_scale_step = scale_step->get_value();
	}
};

bool CanvasItemEditor::_is_node_locked(const Node *p_node) const {
	return p_node->get_meta("_edit_lock_", false);
}

bool CanvasItemEditor::_is_node_movable(const Node *p_node, bool p_popup_warning) {
	if (_is_node_locked(p_node)) {
		return false;
	}
	if (Object::cast_to<Control>(p_node) && Object::cast_to<Container>(p_node->get_parent())) {
		if (p_popup_warning) {
			EditorToaster::get_singleton()->popup_str(TTR("Children of a container get their position and size determined only by their parent."), EditorToaster::SEVERITY_WARNING);
		}
		return false;
	}
	return true;
}

void CanvasItemEditor::_snap_if_closer_float(
		const real_t p_value,
		real_t &r_current_snap, SnapTarget &r_current_snap_target,
		const real_t p_target_value, const SnapTarget p_snap_target,
		const real_t p_radius) {
	const real_t radius = p_radius / view_state.zoom;
	const real_t dist = Math::abs(p_value - p_target_value);
	if ((p_radius < 0 || dist < radius) && (r_current_snap_target == CanvasItemEditorViewState::SNAP_TARGET_NONE || dist < Math::abs(r_current_snap - p_value))) {
		r_current_snap = p_target_value;
		r_current_snap_target = p_snap_target;
	}
}

void CanvasItemEditor::_snap_if_closer_point(
		Point2 p_value,
		Point2 &r_current_snap, SnapTarget (&r_current_snap_target)[2],
		Point2 p_target_value, const SnapTarget p_snap_target,
		const real_t rotation,
		const real_t p_radius) {
	Transform2D rot_trans = Transform2D(rotation, Point2());
	p_value = rot_trans.inverse().xform(p_value);
	p_target_value = rot_trans.inverse().xform(p_target_value);
	r_current_snap = rot_trans.inverse().xform(r_current_snap);

	_snap_if_closer_float(
			p_value.x,
			r_current_snap.x,
			r_current_snap_target[0],
			p_target_value.x,
			p_snap_target,
			p_radius);

	_snap_if_closer_float(
			p_value.y,
			r_current_snap.y,
			r_current_snap_target[1],
			p_target_value.y,
			p_snap_target,
			p_radius);

	r_current_snap = rot_trans.xform(r_current_snap);
}

void CanvasItemEditor::_snap_other_nodes(
		const Point2 p_value,
		const Transform2D p_transform_to_snap,
		Point2 &r_current_snap, SnapTarget (&r_current_snap_target)[2],
		const SnapTarget p_snap_target, List<const CanvasItem *> p_exceptions,
		const Node *p_current) {
	const CanvasItem *ci = Object::cast_to<CanvasItem>(p_current);

	// Check if the element is in the exception
	bool exception = false;
	for (const CanvasItem *&E : p_exceptions) {
		if (E == p_current) {
			exception = true;
			break;
		}
	};

	if (ci && !exception) {
		Transform2D ci_transform = ci->get_screen_transform();
		if (std::fmod(ci_transform.get_rotation() - p_transform_to_snap.get_rotation(), (real_t)360.0) == 0.0) {
			if (ci->_edit_use_rect()) {
				Point2 begin = ci_transform.xform(ci->_edit_get_rect().get_position());
				Point2 end = ci_transform.xform(ci->_edit_get_rect().get_position() + ci->_edit_get_rect().get_size());

				_snap_if_closer_point(p_value, r_current_snap, r_current_snap_target, begin, p_snap_target, ci_transform.get_rotation());
				_snap_if_closer_point(p_value, r_current_snap, r_current_snap_target, end, p_snap_target, ci_transform.get_rotation());
			} else {
				Point2 position = ci_transform.xform(Point2());
				_snap_if_closer_point(p_value, r_current_snap, r_current_snap_target, position, p_snap_target, ci_transform.get_rotation());
			}
		}
	}
	for (int i = 0; i < p_current->get_child_count(); i++) {
		_snap_other_nodes(p_value, p_transform_to_snap, r_current_snap, r_current_snap_target, p_snap_target, p_exceptions, p_current->get_child(i));
	}
}

Point2 CanvasItemEditor::snap_point(Point2 p_target, unsigned int p_modes, unsigned int p_forced_modes, const CanvasItem *p_self_canvas_item, const List<CanvasItem *> &p_other_nodes_exceptions) {
	// snap_point() is global controller logic but writes per-view scratch on view_state
	// (snap_target, snap_transform). Must resolve to the focused/acting view's state.
	CanvasItemEditorViewState &snap_state = get_focused_view() ? get_focused_view()->get_view_state() : view_state;
	snap_state.snap_target[0] = CanvasItemEditorViewState::SNAP_TARGET_NONE;
	snap_state.snap_target[1] = CanvasItemEditorViewState::SNAP_TARGET_NONE;

	bool is_snap_active = smart_snap_active ^ Input::get_singleton()->is_key_pressed(Key::CMD_OR_CTRL);

	// Smart snap using the canvas position
	Vector2 output = p_target;
	real_t rotation = 0.0;

	if (p_self_canvas_item) {
		rotation = p_self_canvas_item->get_screen_transform().get_rotation();

		// Parent sides and center
		if ((is_snap_active && snap_node_parent && (p_modes & SNAP_NODE_PARENT)) || (p_forced_modes & SNAP_NODE_PARENT)) {
			if (const Control *c = Object::cast_to<Control>(p_self_canvas_item)) {
				Point2 begin = p_self_canvas_item->get_screen_transform().xform(_anchor_to_position(c, Point2(0, 0)));
				Point2 end = p_self_canvas_item->get_screen_transform().xform(_anchor_to_position(c, Point2(1, 1)));
				_snap_if_closer_point(p_target, output, snap_state.snap_target, begin, CanvasItemEditorViewState::SNAP_TARGET_PARENT, rotation);
				_snap_if_closer_point(p_target, output, snap_state.snap_target, (begin + end) / 2.0, CanvasItemEditorViewState::SNAP_TARGET_PARENT, rotation);
				_snap_if_closer_point(p_target, output, snap_state.snap_target, end, CanvasItemEditorViewState::SNAP_TARGET_PARENT, rotation);
			} else if (const CanvasItem *parent_ci = Object::cast_to<CanvasItem>(p_self_canvas_item->get_parent())) {
				if (parent_ci->_edit_use_rect()) {
					Point2 begin = p_self_canvas_item->get_transform().affine_inverse().xform(parent_ci->_edit_get_rect().get_position());
					Point2 end = p_self_canvas_item->get_transform().affine_inverse().xform(parent_ci->_edit_get_rect().get_position() + parent_ci->_edit_get_rect().get_size());
					_snap_if_closer_point(p_target, output, snap_state.snap_target, begin, CanvasItemEditorViewState::SNAP_TARGET_PARENT, rotation);
					_snap_if_closer_point(p_target, output, snap_state.snap_target, (begin + end) / 2.0, CanvasItemEditorViewState::SNAP_TARGET_PARENT, rotation);
					_snap_if_closer_point(p_target, output, snap_state.snap_target, end, CanvasItemEditorViewState::SNAP_TARGET_PARENT, rotation);
				} else {
					Point2 position = p_self_canvas_item->get_transform().affine_inverse().xform(Point2());
					_snap_if_closer_point(p_target, output, snap_state.snap_target, position, CanvasItemEditorViewState::SNAP_TARGET_PARENT, rotation);
				}
			}
		}

		// Self anchors
		if ((is_snap_active && snap_node_anchors && (p_modes & SNAP_NODE_ANCHORS)) || (p_forced_modes & SNAP_NODE_ANCHORS)) {
			if (const Control *c = Object::cast_to<Control>(p_self_canvas_item)) {
				Point2 begin = p_self_canvas_item->get_screen_transform().xform(_anchor_to_position(c, Point2(c->get_anchor(SIDE_LEFT), c->get_anchor(SIDE_TOP))));
				Point2 end = p_self_canvas_item->get_screen_transform().xform(_anchor_to_position(c, Point2(c->get_anchor(SIDE_RIGHT), c->get_anchor(SIDE_BOTTOM))));
				_snap_if_closer_point(p_target, output, snap_state.snap_target, begin, CanvasItemEditorViewState::SNAP_TARGET_SELF_ANCHORS, rotation);
				_snap_if_closer_point(p_target, output, snap_state.snap_target, end, CanvasItemEditorViewState::SNAP_TARGET_SELF_ANCHORS, rotation);
			}
		}

		// Self sides
		if ((is_snap_active && snap_node_sides && (p_modes & SNAP_NODE_SIDES)) || (p_forced_modes & SNAP_NODE_SIDES)) {
			if (p_self_canvas_item->_edit_use_rect()) {
				Point2 begin = p_self_canvas_item->get_screen_transform().xform(p_self_canvas_item->_edit_get_rect().get_position());
				Point2 end = p_self_canvas_item->get_screen_transform().xform(p_self_canvas_item->_edit_get_rect().get_position() + p_self_canvas_item->_edit_get_rect().get_size());
				_snap_if_closer_point(p_target, output, snap_state.snap_target, begin, CanvasItemEditorViewState::SNAP_TARGET_SELF, rotation);
				_snap_if_closer_point(p_target, output, snap_state.snap_target, end, CanvasItemEditorViewState::SNAP_TARGET_SELF, rotation);
			}
		}

		// Self center
		if ((is_snap_active && snap_node_center && (p_modes & SNAP_NODE_CENTER)) || (p_forced_modes & SNAP_NODE_CENTER)) {
			if (p_self_canvas_item->_edit_use_rect()) {
				Point2 center = p_self_canvas_item->get_screen_transform().xform(p_self_canvas_item->_edit_get_rect().get_center());
				_snap_if_closer_point(p_target, output, snap_state.snap_target, center, CanvasItemEditorViewState::SNAP_TARGET_SELF, rotation);
			} else {
				Point2 position = p_self_canvas_item->get_screen_transform().xform(Point2());
				_snap_if_closer_point(p_target, output, snap_state.snap_target, position, CanvasItemEditorViewState::SNAP_TARGET_SELF, rotation);
			}
		}
	}

	// Other nodes sides
	if ((is_snap_active && snap_other_nodes && (p_modes & SNAP_OTHER_NODES)) || (p_forced_modes & SNAP_OTHER_NODES)) {
		Transform2D to_snap_transform;
		List<const CanvasItem *> exceptions = List<const CanvasItem *>();
		for (const CanvasItem *E : p_other_nodes_exceptions) {
			exceptions.push_back(E);
		}
		if (p_self_canvas_item) {
			exceptions.push_back(p_self_canvas_item);
			to_snap_transform = p_self_canvas_item->get_screen_transform();
		}

		_snap_other_nodes(
				p_target, to_snap_transform,
				output, snap_state.snap_target,
				CanvasItemEditorViewState::SNAP_TARGET_OTHER_NODE,
				exceptions,
				get_tree()->get_edited_scene_root());
	}

	if (((is_snap_active && snap_guides && (p_modes & SNAP_GUIDES)) || (p_forced_modes & SNAP_GUIDES)) && std::fmod(rotation, (real_t)360.0) == 0.0) {
		// Guides.
		if (Node *scene = EditorNode::get_singleton()->get_edited_scene()) {
			Array vguides = scene->get_meta("_edit_vertical_guides_", Array());
			for (int i = 0; i < vguides.size(); i++) {
				_snap_if_closer_float(p_target.x, output.x, snap_state.snap_target[0], vguides[i], CanvasItemEditorViewState::SNAP_TARGET_GUIDE);
			}

			Array hguides = scene->get_meta("_edit_horizontal_guides_", Array());
			for (int i = 0; i < hguides.size(); i++) {
				_snap_if_closer_float(p_target.y, output.y, snap_state.snap_target[1], hguides[i], CanvasItemEditorViewState::SNAP_TARGET_GUIDE);
			}
		}
	}

	if (((grid_snap_active && (p_modes & SNAP_GRID)) || (p_forced_modes & SNAP_GRID)) && std::fmod(rotation, (real_t)360.0) == 0.0) {
		// Grid
		Point2 offset = grid_offset;
		if (snap_relative) {
			List<CanvasItem *> selection = _get_edited_canvas_items();
			if (selection.size() == 1 && Object::cast_to<Node2D>(selection.front()->get())) {
				offset = Object::cast_to<Node2D>(selection.front()->get())->get_global_position();
			} else if (selection.size() > 0) {
				offset = _get_encompassing_rect_from_list(selection).position;
			}
		}
		Point2 grid_output;
		grid_output.x = Math::snapped(p_target.x - offset.x, grid_step.x * Math::pow(2.0, grid_step_multiplier)) + offset.x;
		grid_output.y = Math::snapped(p_target.y - offset.y, grid_step.y * Math::pow(2.0, grid_step_multiplier)) + offset.y;
		_snap_if_closer_point(p_target, output, snap_state.snap_target, grid_output, CanvasItemEditorViewState::SNAP_TARGET_GRID, 0.0, -1.0);
	}

	if (((snap_pixel && (p_modes & SNAP_PIXEL)) || (p_forced_modes & SNAP_PIXEL)) && rotation == 0.0) {
		// Pixel
		output = output.snappedf(1);
	}

	snap_state.snap_transform = Transform2D(rotation, output);

	return output;
}

real_t CanvasItemEditor::snap_angle(real_t p_target, real_t p_start) const {
	if (((smart_snap_active || snap_rotation) ^ Input::get_singleton()->is_key_pressed(Key::CMD_OR_CTRL)) && snap_rotation_step != 0) {
		if (snap_relative) {
			return Math::snapped(p_target - snap_rotation_offset, snap_rotation_step) + snap_rotation_offset + (p_start - (int)(p_start / snap_rotation_step) * snap_rotation_step);
		} else {
			return Math::snapped(p_target - snap_rotation_offset, snap_rotation_step) + snap_rotation_offset;
		}
	} else {
		return p_target;
	}
}

void CanvasItemEditor::shortcut_input(const Ref<InputEvent> &p_ev) {
	ERR_FAIL_COND(p_ev.is_null());

	Ref<InputEventKey> k = p_ev;

	if (!is_visible_in_tree()) {
		return;
	}

	if (k.is_valid()) {
		if (k->get_keycode() == Key::CTRL || k->get_keycode() == Key::ALT || k->get_keycode() == Key::SHIFT) {
			_get_viewport()->queue_redraw();
		}

		if (k->is_pressed() && !k->is_command_or_control_pressed() && !k->is_echo() && (grid_snap_active || _is_grid_visible())) {
			if (multiply_grid_step_shortcut.is_valid() && multiply_grid_step_shortcut->matches_event(p_ev)) {
				// Multiply the grid size
				grid_step_multiplier = MIN(grid_step_multiplier + 1, 12);
				_get_viewport()->queue_redraw();
			} else if (divide_grid_step_shortcut.is_valid() && divide_grid_step_shortcut->matches_event(p_ev)) {
				// Divide the grid size
				Point2 new_grid_step = grid_step * Math::pow(2.0, grid_step_multiplier - 1);
				if (new_grid_step.x >= 1.0 && new_grid_step.y >= 1.0) {
					grid_step_multiplier--;
				}
				_get_viewport()->queue_redraw();
			}
		}

		if (k->is_pressed() && !k->is_echo()) {
			if (reset_transform_position_shortcut.is_valid() && reset_transform_position_shortcut->matches_event(p_ev)) {
				_reset_transform(TransformType::POSITION);
			}
			if (reset_transform_rotation_shortcut.is_valid() && reset_transform_rotation_shortcut->matches_event(p_ev)) {
				_reset_transform(TransformType::ROTATION);
			}
			if (reset_transform_scale_shortcut.is_valid() && reset_transform_scale_shortcut->matches_event(p_ev)) {
				_reset_transform(TransformType::SCALE);
			}
		}
	}
}

Object *CanvasItemEditor::_get_editor_data(Object *p_what) {
	CanvasItem *ci = Object::cast_to<CanvasItem>(p_what);
	if (!ci) {
		return nullptr;
	}

	return memnew(CanvasItemEditorSelectedItem);
}

void CanvasItemEditor::_keying_changed() {
	AnimationTrackEditor *te = AnimationPlayerEditor::get_singleton()->get_track_editor();
	if (te && te->is_visible_in_tree() && te->get_current_animation().is_valid()) {
		animation_hb->show();
	} else {
		animation_hb->hide();
	}
}

Rect2 CanvasItemEditor::_get_encompassing_rect_from_list(const List<CanvasItem *> &p_list) {
	ERR_FAIL_COND_V(p_list.is_empty(), Rect2());

	// Handles the first element
	CanvasItem *ci = p_list.front()->get();
	Rect2 rect = Rect2(ci->get_global_transform_with_canvas().xform(ci->_edit_get_rect().get_center()), Size2());

	// Expand with the other ones
	for (CanvasItem *ci2 : p_list) {
		Transform2D xform = ci2->get_global_transform_with_canvas();

		Rect2 current_rect = ci2->_edit_get_rect();
		rect.expand_to(xform.xform(current_rect.position));
		rect.expand_to(xform.xform(current_rect.position + Vector2(current_rect.size.x, 0)));
		rect.expand_to(xform.xform(current_rect.position + current_rect.size));
		rect.expand_to(xform.xform(current_rect.position + Vector2(0, current_rect.size.y)));
	}

	return rect;
}

void CanvasItemEditor::_expand_encompassing_rect_using_children(Rect2 &r_rect, const Node *p_node, bool &r_first, const Transform2D &p_parent_xform, const Transform2D &p_canvas_xform, bool include_locked_nodes) {
	if (!p_node) {
		return;
	}
	if (Object::cast_to<Viewport>(p_node)) {
		return;
	}

	const CanvasItem *ci = Object::cast_to<CanvasItem>(p_node);

	for (int i = p_node->get_child_count() - 1; i >= 0; i--) {
		if (ci && !ci->is_set_as_top_level()) {
			_expand_encompassing_rect_using_children(r_rect, p_node->get_child(i), r_first, p_parent_xform * ci->get_transform(), p_canvas_xform);
		} else {
			const CanvasLayer *cl = Object::cast_to<CanvasLayer>(p_node);
			_expand_encompassing_rect_using_children(r_rect, p_node->get_child(i), r_first, Transform2D(), cl ? cl->get_transform() : p_canvas_xform);
		}
	}

	if (ci && ci->is_visible_in_tree() && (include_locked_nodes || !_is_node_locked(ci))) {
		Transform2D xform = p_canvas_xform;
		if (!ci->is_set_as_top_level()) {
			xform *= p_parent_xform;
		}
		xform *= ci->get_transform();
		Rect2 rect = ci->_edit_get_rect();
		if (r_first) {
			r_rect = Rect2(xform.xform(rect.get_center()), Size2());
			r_first = false;
		}
		r_rect.expand_to(xform.xform(rect.position));
		r_rect.expand_to(xform.xform(rect.position + Point2(rect.size.x, 0)));
		r_rect.expand_to(xform.xform(rect.position + Point2(0, rect.size.y)));
		r_rect.expand_to(xform.xform(rect.position + rect.size));
	}
}

Rect2 CanvasItemEditor::_get_encompassing_rect(const Node *p_node) {
	Rect2 rect;
	bool first = true;
	_expand_encompassing_rect_using_children(rect, p_node, first);

	return rect;
}

void CanvasItemEditor::_find_canvas_items_at_pos(const Point2 &p_pos, Node *p_node, Vector<_SelectResult> &r_items, const Transform2D &p_parent_xform, const Transform2D &p_canvas_xform) {
	if (!p_node) {
		return;
	}

	CanvasItem *ci = Object::cast_to<CanvasItem>(p_node);

	Transform2D xform = p_canvas_xform;
	if (CanvasLayer *cl = Object::cast_to<CanvasLayer>(p_node)) {
		xform = cl->get_transform();
	} else if (Viewport *vp = Object::cast_to<Viewport>(p_node)) {
		if (!vp->is_visible_subviewport()) {
			return;
		}
		xform = vp->get_popup_base_transform();
		if (!vp->get_visible_rect().has_point(xform.affine_inverse().xform(p_pos))) {
			return;
		}
	}

	for (int i = p_node->get_child_count() - 1; i >= 0; i--) {
		if (ci) {
			if (!ci->is_set_as_top_level()) {
				_find_canvas_items_at_pos(p_pos, p_node->get_child(i), r_items, p_parent_xform * ci->get_transform(), xform);
			} else {
				_find_canvas_items_at_pos(p_pos, p_node->get_child(i), r_items, ci->get_transform(), xform);
			}
		} else {
			_find_canvas_items_at_pos(p_pos, p_node->get_child(i), r_items, Transform2D(), xform);
		}
	}

	if (ci && ci->is_visible_in_tree()) {
		if (!ci->is_set_as_top_level()) {
			xform *= p_parent_xform;
		}
		xform = (xform * ci->get_transform()).affine_inverse();
		const real_t local_grab_distance = xform.basis_xform(Vector2(view_state.grab_distance, 0)).length() / view_state.zoom;
		if (ci->_edit_is_selected_on_click(xform.xform(p_pos), local_grab_distance)) {
			Node2D *node = Object::cast_to<Node2D>(ci);
			CanvasItemEditorViewMath::accumulate_select_result(r_items, ci, node ? node->get_z_index() : 0, node);
		}
	}
}

void CanvasItemEditor::_get_canvas_items_at_pos(const Point2 &p_pos, Vector<_SelectResult> &r_items, bool p_allow_locked) {
	Node *scene = EditorNode::get_singleton()->get_edited_scene();

	_find_canvas_items_at_pos(p_pos, scene, r_items);

	//Remove invalid results
	for (int i = 0; i < r_items.size(); i++) {
		Node *node = r_items[i].item;

		// Make sure the selected node is in the current scene, or editable
		if (node && node != get_tree()->get_edited_scene_root()) {
			node = scene->get_deepest_editable_node(node);
		}

		CanvasItem *ci = Object::cast_to<CanvasItem>(node);
		if (!p_allow_locked) {
			// Replace the node by the group if grouped
			while (node && node != scene->get_parent()) {
				CanvasItem *ci_tmp = Object::cast_to<CanvasItem>(node);
				if (ci_tmp && node->has_meta("_edit_group_")) {
					ci = ci_tmp;
				}
				node = node->get_parent();
			}
		}

		// Check if the canvas item is already in the list (for groups or scenes)
		bool duplicate = false;
		for (int j = 0; j < i; j++) {
			if (r_items[j].item == ci) {
				duplicate = true;
				break;
			}
		}

		//Remove the item if invalid
		if (!ci || duplicate || (ci != scene && ci->get_owner() != scene && !scene->is_editable_instance(ci->get_owner())) || (!p_allow_locked && _is_node_locked(ci))) {
			r_items.remove_at(i);
			i--;
		} else {
			r_items.write[i].item = ci;
		}
	}
}

void CanvasItemEditor::_find_canvas_items_in_rect(const Rect2 &p_rect, Node *p_node, List<CanvasItem *> *r_items, const Transform2D &p_parent_xform, const Transform2D &p_canvas_xform) {
	if (!p_node) {
		return;
	}
	CanvasItem *ci = Object::cast_to<CanvasItem>(p_node);
	Node *scene = EditorNode::get_singleton()->get_edited_scene();

	if (p_node != scene && !p_node->get_owner()) {
		return;
	}

	bool editable = p_node == scene || p_node->get_owner() == scene || p_node == scene->get_deepest_editable_node(p_node);
	bool lock_children = p_node->get_meta("_edit_group_", false);
	bool locked = _is_node_locked(p_node);

	Transform2D xform = p_canvas_xform;
	if (CanvasLayer *cl = Object::cast_to<CanvasLayer>(p_node)) {
		xform = cl->get_transform();
	} else if (Viewport *vp = Object::cast_to<Viewport>(p_node)) {
		if (!vp->is_visible_subviewport()) {
			return;
		}
		xform = vp->get_popup_base_transform();
		if (!vp->get_visible_rect().intersects(xform.affine_inverse().xform(p_rect))) {
			return;
		}
	}

	if (!lock_children || !editable) {
		for (int i = p_node->get_child_count() - 1; i >= 0; i--) {
			if (ci) {
				if (!ci->is_set_as_top_level()) {
					_find_canvas_items_in_rect(p_rect, p_node->get_child(i), r_items, p_parent_xform * ci->get_transform(), xform);
				} else {
					_find_canvas_items_in_rect(p_rect, p_node->get_child(i), r_items, ci->get_transform(), xform);
				}
			} else {
				CanvasLayer *cl = Object::cast_to<CanvasLayer>(p_node);
				_find_canvas_items_in_rect(p_rect, p_node->get_child(i), r_items, Transform2D(), cl ? cl->get_transform() : xform);
			}
		}
	}

	if (ci && ci->is_visible_in_tree() && !locked && editable) {
		if (!ci->is_set_as_top_level()) {
			xform *= p_parent_xform;
		}
		xform *= ci->get_transform();

		if (ci->_edit_use_rect()) {
			Rect2 rect = ci->_edit_get_rect();
			if (p_rect.has_point(xform.xform(rect.position)) &&
					p_rect.has_point(xform.xform(rect.position + Vector2(rect.size.x, 0))) &&
					p_rect.has_point(xform.xform(rect.position + Vector2(rect.size.x, rect.size.y))) &&
					p_rect.has_point(xform.xform(rect.position + Vector2(0, rect.size.y)))) {
				r_items->push_back(ci);
			}
		} else {
			if (p_rect.has_point(xform.xform(Point2()))) {
				r_items->push_back(ci);
			}
		}
	}
}

bool CanvasItemEditor::_select_click_on_item(CanvasItem *item, Point2 p_click_pos, bool p_append) {
	bool still_selected = true;
	const List<Node *> &top_node_list = editor_selection->get_top_selected_node_list();
	if (p_append && !top_node_list.is_empty()) {
		if (editor_selection->is_selected(item)) {
			// Already in the selection, remove it from the selected nodes
			editor_selection->remove_node(item);
			still_selected = false;

			if (top_node_list.size() == 1) {
				EditorNode::get_singleton()->push_item(top_node_list.front()->get());
			}
		} else {
			// Add the item to the selection
			editor_selection->add_node(item);
		}
	} else {
		if (!editor_selection->is_selected(item)) {
			// Select a new one and clear previous selection
			editor_selection->clear();
			editor_selection->add_node(item);
			// Reselect
			if (Engine::get_singleton()->is_editor_hint()) {
				selected_from_canvas = true;
			}
		}
	}
	_get_viewport()->queue_redraw();
	return still_selected;
}

List<CanvasItem *> CanvasItemEditor::_get_edited_canvas_items(bool p_retrieve_locked, bool p_remove_canvas_item_if_parent_in_selection, bool *r_has_locked_items) const {
	List<CanvasItem *> selection;
	for (const KeyValue<ObjectID, Object *> &E : editor_selection->get_selection()) {
		CanvasItem *ci = ObjectDB::get_instance<CanvasItem>(E.key);
		if (ci) {
			if (ci->is_visible_in_tree() && (p_retrieve_locked || !_is_node_locked(ci))) {
				Viewport *vp = ci->get_viewport();
				if (vp && !vp->is_visible_subviewport()) {
					continue;
				}
				CanvasItemEditorSelectedItem *se = editor_selection->get_node_editor_data<CanvasItemEditorSelectedItem>(ci);
				if (se) {
					selection.push_back(ci);
				}
			} else if (r_has_locked_items) {
				// CanvasItem is selected, but can't be interacted with.
				*r_has_locked_items = true;
			}
		}
	}

	if (p_remove_canvas_item_if_parent_in_selection) {
		List<CanvasItem *> filtered_selection;
		HashSet<const Node *> nodes_in_selection;
		for (CanvasItem *E : selection) {
			nodes_in_selection.insert(E);
		}
		for (CanvasItem *E : selection) {
			if (!nodes_in_selection.has(E->get_parent())) {
				filtered_selection.push_back(E);
			}
		}
		return filtered_selection;
	} else {
		return selection;
	}
}

Vector2 CanvasItemEditor::_anchor_to_position(const Control *p_control, Vector2 anchor) {
	ERR_FAIL_NULL_V(p_control, Vector2());

	Transform2D parent_transform = p_control->get_transform().affine_inverse();
	Rect2 parent_rect = p_control->get_parent_anchorable_rect();

	if (p_control->is_layout_rtl()) {
		return parent_transform.xform(parent_rect.position + Vector2(parent_rect.size.x - parent_rect.size.x * anchor.x, parent_rect.size.y * anchor.y));
	} else {
		return parent_transform.xform(parent_rect.position + Vector2(parent_rect.size.x * anchor.x, parent_rect.size.y * anchor.y));
	}
}

Vector2 CanvasItemEditor::_position_to_anchor(const Control *p_control, Vector2 position) {
	ERR_FAIL_NULL_V(p_control, Vector2());

	Rect2 parent_rect = p_control->get_parent_anchorable_rect();

	Vector2 output;
	if (p_control->is_layout_rtl()) {
		output.x = (parent_rect.size.x == 0) ? 0.0 : (parent_rect.size.x - p_control->get_transform().xform(position).x - parent_rect.position.x) / parent_rect.size.x;
	} else {
		output.x = (parent_rect.size.x == 0) ? 0.0 : (p_control->get_transform().xform(position).x - parent_rect.position.x) / parent_rect.size.x;
	}
	output.y = (parent_rect.size.y == 0) ? 0.0 : (p_control->get_transform().xform(position).y - parent_rect.position.y) / parent_rect.size.y;
	return output;
}

void CanvasItemEditor::_save_canvas_item_state(const List<CanvasItem *> &p_canvas_items, bool save_bones) {
	view_state.original_transform = Transform2D();
	bool transform_stored = false;

	for (CanvasItem *ci : p_canvas_items) {
		CanvasItemEditorSelectedItem *se = editor_selection->get_node_editor_data<CanvasItemEditorSelectedItem>(ci);
		if (se) {
			if (!transform_stored) {
				view_state.original_transform = ci->get_global_transform();
				transform_stored = true;
			}

			se->undo_state = ci->_edit_get_state();
			se->pre_drag_xform = ci->get_screen_transform();
			if (ci->_edit_use_rect()) {
				se->pre_drag_rect = ci->_edit_get_rect();
			} else {
				se->pre_drag_rect = Rect2();
			}
		}
	}
}

void CanvasItemEditor::_restore_canvas_item_state(const List<CanvasItem *> &p_canvas_items, bool restore_bones) {
	for (CanvasItem *ci : view_state.drag_selection) {
		CanvasItemEditorSelectedItem *se = editor_selection->get_node_editor_data<CanvasItemEditorSelectedItem>(ci);
		ci->_edit_set_state(se->undo_state);
	}
}

void CanvasItemEditor::_commit_canvas_item_state(const List<CanvasItem *> &p_canvas_items, const String &action_name, bool commit_bones) {
	List<CanvasItem *> modified_canvas_items;
	for (CanvasItem *ci : p_canvas_items) {
		Dictionary old_state = editor_selection->get_node_editor_data<CanvasItemEditorSelectedItem>(ci)->undo_state;
		Dictionary new_state = ci->_edit_get_state();

		if (old_state.hash() != new_state.hash()) {
			modified_canvas_items.push_back(ci);
		}
	}

	if (modified_canvas_items.is_empty()) {
		return;
	}

	EditorUndoRedoManager *undo_redo = EditorUndoRedoManager::get_singleton();
	undo_redo->create_action(action_name);
	for (CanvasItem *ci : modified_canvas_items) {
		CanvasItemEditorSelectedItem *se = editor_selection->get_node_editor_data<CanvasItemEditorSelectedItem>(ci);
		if (se) {
			undo_redo->add_do_method(ci, "_edit_set_state", ci->_edit_get_state());
			undo_redo->add_undo_method(ci, "_edit_set_state", se->undo_state);
			if (commit_bones) {
				for (const Dictionary &F : se->pre_drag_bones_undo_state) {
					ci = Object::cast_to<CanvasItem>(ci->get_parent());
					undo_redo->add_do_method(ci, "_edit_set_state", ci->_edit_get_state());
					undo_redo->add_undo_method(ci, "_edit_set_state", F);
				}
			}
		}
	}
	undo_redo->add_do_method(_get_viewport(), "queue_redraw");
	undo_redo->add_undo_method(_get_viewport(), "queue_redraw");
	undo_redo->commit_action();
}

void CanvasItemEditor::_snap_changed() {
	static_cast<SnapDialog *>(snap_dialog)->get_fields(grid_offset, grid_step, primary_grid_step, snap_rotation_offset, snap_rotation_step, snap_scale_step);

	EditorSettings::get_singleton()->set_project_metadata("2d_editor", "grid_offset", grid_offset);
	EditorSettings::get_singleton()->set_project_metadata("2d_editor", "grid_step", grid_step);
	EditorSettings::get_singleton()->set_project_metadata("2d_editor", "primary_grid_step", primary_grid_step);
	EditorSettings::get_singleton()->set_project_metadata("2d_editor", "snap_rotation_offset", snap_rotation_offset);
	EditorSettings::get_singleton()->set_project_metadata("2d_editor", "snap_rotation_step", snap_rotation_step);
	EditorSettings::get_singleton()->set_project_metadata("2d_editor", "snap_scale_step", snap_scale_step);

	grid_step_multiplier = 0;
	_get_viewport()->queue_redraw();
}

void CanvasItemEditor::_selection_result_pressed(int p_result) {
	if (view_state.selection_results_menu.size() <= p_result) {
		return;
	}

	CanvasItem *item = view_state.selection_results_menu[p_result].item;

	if (item) {
		_select_click_on_item(item, Point2(), selection_menu_additive_selection);
	}
	view_state.selection_results_menu.clear();
}

void CanvasItemEditor::_selection_menu_hide() {
	view_state.selection_results.clear();
	selection_menu->clear();
	selection_menu->reset_size();
}

void CanvasItemEditor::_add_node_pressed(int p_result) {
	List<Node *> nodes_to_move;

	switch (p_result) {
		case ADD_NODE: {
			SceneTreeDock::get_singleton()->open_add_child_dialog();
		} break;
		case ADD_INSTANCE: {
			SceneTreeDock::get_singleton()->open_instance_child_dialog();
		} break;
		case ADD_PASTE: {
			nodes_to_move = SceneTreeDock::get_singleton()->paste_nodes();
			[[fallthrough]];
		}
		case ADD_MOVE: {
			nodes_to_move = EditorNode::get_singleton()->get_editor_selection()->get_top_selected_node_list();
			if (nodes_to_move.is_empty()) {
				return;
			}

			EditorUndoRedoManager *undo_redo = EditorUndoRedoManager::get_singleton();
			undo_redo->create_action(TTR("Move Node(s) to Position"));
			for (Node *node : nodes_to_move) {
				CanvasItem *ci = Object::cast_to<CanvasItem>(node);
				if (ci) {
					Transform2D xform = ci->get_global_transform_with_canvas().affine_inverse() * ci->get_transform();
					undo_redo->add_do_method(ci, "_edit_set_position", xform.xform(view_state.node_create_position));
					undo_redo->add_undo_method(ci, "_edit_set_position", ci->_edit_get_position());
				}
			}
			undo_redo->commit_action();
			_reset_create_position();
		} break;
		default: {
			if (p_result >= EditorContextMenuPlugin::BASE_ID) {
				TypedArray<Node> nodes;
				nodes.resize(view_state.selection_results.size());

				int i = 0;
				for (const _SelectResult &result : view_state.selection_results) {
					nodes[i] = result.item;
					i++;
				}
				EditorContextMenuPluginManager::get_singleton()->activate_custom_option(EditorContextMenuPlugin::CONTEXT_SLOT_2D_EDITOR, p_result, nodes);
			}
		}
	}
}

void CanvasItemEditor::_adjust_new_node_position(Node *p_node) {
	if (view_state.node_create_position == Point2()) {
		return;
	}

	CanvasItem *c = Object::cast_to<CanvasItem>(p_node);
	if (c) {
		Transform2D xform = c->get_global_transform_with_canvas().affine_inverse() * c->get_transform();
		c->_edit_set_position(xform.xform(view_state.node_create_position));
	}

	callable_mp(this, &CanvasItemEditor::_reset_create_position).call_deferred(); // Defer the call in case more than one node is added.
}

void CanvasItemEditor::_reset_create_position() {
	view_state.node_create_position = Point2();
}

bool CanvasItemEditor::_is_grid_visible() const {
	switch (grid_visibility) {
		case GRID_VISIBILITY_SHOW:
			return true;
		case GRID_VISIBILITY_SHOW_WHEN_SNAPPING:
			return grid_snap_active;
		case GRID_VISIBILITY_HIDE:
			return false;
	}
	ERR_FAIL_V_MSG(true, "Unexpected grid_visibility value");
}

void CanvasItemEditor::_prepare_grid_menu() {
	for (int i = GRID_VISIBILITY_SHOW; i <= GRID_VISIBILITY_HIDE; i++) {
		grid_menu->set_item_checked(i, i == grid_visibility);
	}
}

void CanvasItemEditor::_on_grid_menu_id_pressed(int p_id) {
	switch (p_id) {
		case GRID_VISIBILITY_SHOW:
		case GRID_VISIBILITY_SHOW_WHEN_SNAPPING:
		case GRID_VISIBILITY_HIDE:
			grid_visibility = (GridVisibility)p_id;
			_get_viewport()->queue_redraw();
			view_menu->get_popup()->hide();
			return;
	}

	// Toggle grid: go to the least restrictive option possible.
	if (grid_snap_active) {
		switch (grid_visibility) {
			case GRID_VISIBILITY_SHOW:
			case GRID_VISIBILITY_SHOW_WHEN_SNAPPING:
				grid_visibility = GRID_VISIBILITY_HIDE;
				break;
			case GRID_VISIBILITY_HIDE:
				grid_visibility = GRID_VISIBILITY_SHOW_WHEN_SNAPPING;
				break;
		}
	} else {
		switch (grid_visibility) {
			case GRID_VISIBILITY_SHOW:
				grid_visibility = GRID_VISIBILITY_SHOW_WHEN_SNAPPING;
				break;
			case GRID_VISIBILITY_SHOW_WHEN_SNAPPING:
			case GRID_VISIBILITY_HIDE:
				grid_visibility = GRID_VISIBILITY_SHOW;
				break;
		}
	}
	_get_viewport()->queue_redraw();
}

void CanvasItemEditor::_reset_transform(TransformType p_type) {
	List<Node *> selection = editor_selection->get_full_selected_node_list();
	if (selection.is_empty()) {
		return;
	}
	EditorUndoRedoManager *undo_redo = EditorUndoRedoManager::get_singleton();
	undo_redo->create_action(TTR("Reset Transform"));
	for (Node *node : selection) {
		Node2D *res_node = Object::cast_to<Node2D>(node);
		if (res_node) {
			switch (p_type) {
				case TransformType::POSITION:
					undo_redo->add_undo_method(res_node, "set_position", res_node->get_position());
					undo_redo->add_do_method(res_node, "set_position", Vector2());
					break;
				case TransformType::ROTATION:
					undo_redo->add_undo_method(res_node, "set_rotation", res_node->get_rotation());
					undo_redo->add_do_method(res_node, "set_rotation", 0);
					break;
				case TransformType::SCALE:
					undo_redo->add_undo_method(res_node, "set_scale", res_node->get_scale());
					undo_redo->add_do_method(res_node, "set_scale", Size2(1, 1));
					break;
			}
			continue;
		}
		Control *res_control = Object::cast_to<Control>(node);
		if (res_control) {
			switch (p_type) {
				case TransformType::POSITION:
					undo_redo->add_undo_method(res_control, "set_position", res_control->get_position());
					undo_redo->add_do_method(res_control, "set_position", Vector2());
					break;
				case TransformType::ROTATION:
					undo_redo->add_undo_method(res_control, "set_rotation", res_control->get_rotation());
					undo_redo->add_do_method(res_control, "set_rotation", 0);
					break;
				case TransformType::SCALE:
					undo_redo->add_undo_method(res_control, "set_scale", res_control->get_scale());
					undo_redo->add_do_method(res_control, "set_scale", Size2(1, 1));
					break;
			}
		}
	}
	undo_redo->commit_action();
}

void CanvasItemEditor::_switch_theme_preview(int p_mode) {
	view_menu->get_popup()->hide();

	if (theme_preview == p_mode) {
		return;
	}
	theme_preview = (ThemePreviewMode)p_mode;
	EditorSettings::get_singleton()->set_project_metadata("2d_editor", "theme_preview", theme_preview);

	for (int i = 0; i < THEME_PREVIEW_MAX; i++) {
		theme_menu->set_item_checked(i, i == theme_preview);
	}

	EditorNode::get_singleton()->update_preview_themes(theme_preview);
}

void CanvasItemEditor::_update_lock_and_group_button() {
	bool all_locked = true;
	bool all_group = true;
	bool has_canvas_item = false;
	const List<Node *> &selection = editor_selection->get_top_selected_node_list();
	if (selection.is_empty()) {
		all_locked = false;
		all_group = false;
	} else {
		for (Node *E : selection) {
			CanvasItem *item = Object::cast_to<CanvasItem>(E);
			if (item) {
				if (all_locked && !item->has_meta("_edit_lock_")) {
					all_locked = false;
				}
				if (all_group && !item->has_meta("_edit_group_")) {
					all_group = false;
				}
				has_canvas_item = true;
			}
			if (!all_locked && !all_group) {
				break;
			}
		}
	}

	all_locked = all_locked && has_canvas_item;
	all_group = all_group && has_canvas_item;

	lock_button->set_visible(!all_locked);
	lock_button->set_disabled(!has_canvas_item);
	unlock_button->set_visible(all_locked);
	unlock_button->set_disabled(!has_canvas_item);
	group_button->set_visible(!all_group);
	group_button->set_disabled(!has_canvas_item);
	ungroup_button->set_visible(all_group);
	ungroup_button->set_disabled(!has_canvas_item);
}

void CanvasItemEditor::set_current_tool(Tool p_tool) {
	_button_tool_select(p_tool);
}

void CanvasItemEditor::_update_editor_settings() {
	if (editor_view) {
		editor_view->update_center_button_icon(get_editor_theme_icon(SNAME("CenterView")));
	}
	select_button->set_button_icon(get_editor_theme_icon(SNAME("ToolSelect")));
	select_sb->set_texture(get_editor_theme_icon(SNAME("EditorRect2D")));
	list_select_button->set_button_icon(get_editor_theme_icon(SNAME("ListSelect")));
	move_button->set_button_icon(get_editor_theme_icon(SNAME("ToolMove")));
	scale_button->set_button_icon(get_editor_theme_icon(SNAME("ToolScale")));
	rotate_button->set_button_icon(get_editor_theme_icon(SNAME("ToolRotate")));
	local_space_button->set_button_icon(get_editor_theme_icon(SNAME("Object")));
	smart_snap_button->set_button_icon(get_editor_theme_icon(SNAME("Snap")));
	grid_snap_button->set_button_icon(get_editor_theme_icon(SNAME("SnapGrid")));
	snap_config_menu->set_button_icon(get_editor_theme_icon(SNAME("GuiTabMenuHl")));
	skeleton_menu->set_button_icon(get_editor_theme_icon(SNAME("Bone")));
	pan_button->set_button_icon(get_editor_theme_icon(SNAME("ToolPan")));
	ruler_button->set_button_icon(get_editor_theme_icon(SNAME("Ruler")));
	pivot_button->set_button_icon(get_editor_theme_icon(SNAME("EditPivot")));
	select_handle = get_editor_theme_icon(SNAME("EditorHandle"));
	anchor_handle = get_editor_theme_icon(SNAME("EditorControlAnchor"));
	lock_button->set_button_icon(get_editor_theme_icon(SNAME("Lock")));
	unlock_button->set_button_icon(get_editor_theme_icon(SNAME("Unlock")));
	group_button->set_button_icon(get_editor_theme_icon(SNAME("Group")));
	ungroup_button->set_button_icon(get_editor_theme_icon(SNAME("Ungroup")));
	key_loc_button->set_button_icon(get_editor_theme_icon(SNAME("KeyPosition")));
	key_rot_button->set_button_icon(get_editor_theme_icon(SNAME("KeyRotation")));
	key_scale_button->set_button_icon(get_editor_theme_icon(SNAME("KeyScale")));
	key_insert_button->set_button_icon(get_editor_theme_icon(SNAME("Key")));
	key_auto_insert_button->set_button_icon(get_editor_theme_icon(SNAME("AutoKey")));
	// Use a different color for the active autokey icon to make them easier
	// to distinguish from the other key icons at the top. On a light theme,
	// the icon will be dark, so we need to lighten it before blending it
	// with the red color.
	const Color key_auto_color = EditorThemeManager::is_dark_icon_and_font() ? Color(1, 1, 1) : Color(4.25, 4.25, 4.25);
	key_auto_insert_button->add_theme_color_override("icon_pressed_color", key_auto_color.lerp(Color(1, 0, 0), 0.55));
	animation_menu->set_button_icon(get_editor_theme_icon(SNAME("GuiTabMenuHl")));

	context_toolbar_panel->add_theme_style_override(SceneStringName(panel), get_theme_stylebox(SNAME("ContextualToolbar"), EditorStringName(EditorStyles)));

	simple_panning = EDITOR_GET("editors/panning/simple_panning");
	if (editor_view) {
		editor_view->update_panner_from_settings();
	}

	// Compute the ruler width here so we can reuse the result throughout the various draw functions.
	real_t ruler_width_unscaled = EDITOR_GET("editors/2d/ruler_width");
	ruler_font_size = MAX(get_theme_font_size(SNAME("rulers_size"), EditorStringName(EditorFonts)) * ruler_width_unscaled / 15.0, 8);
	ruler_width_scaled = MAX(ruler_width_unscaled * EDSCALE, ruler_font_size * 2.0);

	view_state.grab_distance = EDITOR_GET("editors/polygon_editor/point_grab_radius");

	resample_delay = EDITOR_GET("editors/2d/auto_resample_delay");
	resample_timer->set_wait_time(resample_delay);
}

void CanvasItemEditor::_project_settings_changed() {
	if (editor_view) {
		editor_view->push_viewport_state();
	}
}

void CanvasItemEditor::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_TRANSLATION_CHANGED: {
			select_button->set_tooltip_text(vformat(TTR("%s+Drag: Rotate selected node around pivot."), keycode_get_string((Key)KeyModifierMask::CMD_OR_CTRL)) + "\n" + TTR("Alt+Drag: Move selected node.") + "\n" + vformat(TTR("%s+Alt+Drag: Scale selected node."), keycode_get_string((Key)KeyModifierMask::CMD_OR_CTRL)) + "\n" + TTR("V: Set selected node's pivot position.") + "\n" + TTR("Alt+RMB: Show list of all nodes at position clicked, including locked.") + "\n" + TTR("(Available in all modes.)") + "\n" + TTR("RMB: Add node at position clicked."));
			pivot_button->set_tooltip_text(TTR("Click to change object's pivot.") + "\n" + TTR("Shift: Set temporary pivot.") + "\n" + TTR("Click this button while holding Shift to put the temporary pivot in the center of the selected nodes."));
		} break;

		case NOTIFICATION_READY: {
			_update_lock_and_group_button();

			ProjectSettings::get_singleton()->connect("settings_changed", callable_mp(this, &CanvasItemEditor::_project_settings_changed));
		} break;

		case NOTIFICATION_ACCESSIBILITY_UPDATE: {
			RID ae = get_accessibility_element();
			ERR_FAIL_COND(ae.is_null());

			//TODO
			DisplayServer::get_singleton()->accessibility_update_set_role(ae, DisplayServer::AccessibilityRole::ROLE_STATIC_TEXT);
			DisplayServer::get_singleton()->accessibility_update_set_value(ae, TTR(vformat("The %s is not accessible at this time.", "Canvas item editor")));
		} break;

		case NOTIFICATION_PROCESS: {
			// Update the viewport if the canvas_item changes
			List<CanvasItem *> selection = _get_edited_canvas_items(true);
			for (CanvasItem *ci : selection) {
				CanvasItemEditorSelectedItem *se = editor_selection->get_node_editor_data<CanvasItemEditorSelectedItem>(ci);

				Rect2 rect;
				if (ci->_edit_use_rect()) {
					rect = ci->_edit_get_rect();
				} else {
					rect = Rect2();
				}
				Transform2D xform = ci->get_global_transform();

				if (rect != se->prev_rect || xform != se->prev_xform) {
					_get_viewport()->queue_redraw();
					se->prev_rect = rect;
					se->prev_xform = xform;
				}

				Control *control = Object::cast_to<Control>(ci);
				if (control) {
					Vector2 pivot = control->get_pivot_offset();
					Vector2 pivot_ratio = control->get_pivot_offset_ratio();

					real_t anchors[4];
					anchors[SIDE_LEFT] = control->get_anchor(SIDE_LEFT);
					anchors[SIDE_RIGHT] = control->get_anchor(SIDE_RIGHT);
					anchors[SIDE_TOP] = control->get_anchor(SIDE_TOP);
					anchors[SIDE_BOTTOM] = control->get_anchor(SIDE_BOTTOM);

					if (pivot != se->prev_pivot || pivot_ratio != se->prev_pivot_ratio || anchors[SIDE_LEFT] != se->prev_anchors[SIDE_LEFT] || anchors[SIDE_RIGHT] != se->prev_anchors[SIDE_RIGHT] || anchors[SIDE_TOP] != se->prev_anchors[SIDE_TOP] || anchors[SIDE_BOTTOM] != se->prev_anchors[SIDE_BOTTOM]) {
						se->prev_pivot = pivot;
						se->prev_pivot_ratio = pivot_ratio;
						se->prev_anchors[SIDE_LEFT] = anchors[SIDE_LEFT];
						se->prev_anchors[SIDE_RIGHT] = anchors[SIDE_RIGHT];
						se->prev_anchors[SIDE_TOP] = anchors[SIDE_TOP];
						se->prev_anchors[SIDE_BOTTOM] = anchors[SIDE_BOTTOM];
						_get_viewport()->queue_redraw();
					}
				}
			}

			// Activate / Deactivate the pivot tool.
			pivot_button->set_disabled(selection.is_empty());

			// Update the viewport if bones changes
			for (KeyValue<BoneKey, BoneList> &E : bone_list) {
				Object *b = ObjectDB::get_instance(E.key.from);
				if (!b) {
					_get_viewport()->queue_redraw();
					break;
				}

				Node2D *b2 = Object::cast_to<Node2D>(b);
				if (!b2 || !b2->is_inside_tree()) {
					continue;
				}

				Transform2D global_xform = b2->get_global_transform();

				if (global_xform != E.value.xform) {
					E.value.xform = global_xform;
					_get_viewport()->queue_redraw();
				}

				Bone2D *bone = Object::cast_to<Bone2D>(b);
				if (bone && bone->get_length() != E.value.length) {
					E.value.length = bone->get_length();
					_get_viewport()->queue_redraw();
				}
			}
		} break;

		case NOTIFICATION_ENTER_TREE: {
			// The signal only exists once EditorNode's bindings are set up,
			// which happens after its constructor (and this editor) ran; a
			// context may also have activated before this connection existed.
			Callable active_context_cb = callable_mp(this, &CanvasItemEditor::_active_scene_context_changed);
			if (EditorNode::get_singleton() && !EditorNode::get_singleton()->is_connected("active_scene_context_changed", active_context_cb)) {
				EditorNode::get_singleton()->connect("active_scene_context_changed", active_context_cb);
				_active_scene_context_changed();
			}
			select_sb->set_texture(get_editor_theme_icon(SNAME("EditorRect2D")));
			select_sb->set_texture_margin_all(4);
			select_sb->set_content_margin_all(4);

			AnimationPlayerEditor *animation_player_editor = AnimationPlayerEditor::get_singleton();
			if (animation_player_editor) {
				Callable keying_cb = callable_mp(this, &CanvasItemEditor::_keying_changed);
				if (animation_player_editor->get_track_editor() && !animation_player_editor->get_track_editor()->is_connected("keying_changed", keying_cb)) {
					animation_player_editor->get_track_editor()->connect("keying_changed", keying_cb);
				}
				Callable animation_selected_cb = callable_mp(this, &CanvasItemEditor::_keying_changed).unbind(1);
				if (!animation_player_editor->is_connected("animation_selected", animation_selected_cb)) {
					animation_player_editor->connect("animation_selected", animation_selected_cb);
				}
			}
			_keying_changed();
			_update_editor_settings();

			Callable lock_cb = callable_mp(this, &CanvasItemEditor::_update_lock_and_group_button);
			if (!is_connected("item_lock_status_changed", lock_cb)) {
				connect("item_lock_status_changed", lock_cb);
			}
			if (!is_connected("item_group_status_changed", lock_cb)) {
				connect("item_group_status_changed", lock_cb);
			}
		} break;

		case EditorSettings::NOTIFICATION_EDITOR_SETTINGS_CHANGED: {
			if (EditorThemeManager::is_generated_theme_outdated() ||
					EditorSettings::get_singleton()->check_changed_settings_in_group("editors/panning") ||
					EditorSettings::get_singleton()->check_changed_settings_in_group("editors/2d") ||
					EditorSettings::get_singleton()->check_changed_settings_in_group("editors/polygon_editor")) {
				_update_editor_settings();
				update_viewport();
			}
		} break;

		case NOTIFICATION_APPLICATION_FOCUS_OUT:
		case NOTIFICATION_WM_WINDOW_FOCUS_OUT: {
			if (view_state.drag_type != CanvasItemEditorViewState::DRAG_NONE) {
				_commit_drag();
			}
		} break;
	}
}

void CanvasItemEditor::_selection_changed() {
	_update_lock_and_group_button();
	if (!selected_from_canvas) {
		_reset_drag();
	}
	selected_from_canvas = false;

	if (view_state.temp_pivot != Vector2(Math::INF, Math::INF)) {
		view_state.temp_pivot = Vector2(Math::INF, Math::INF);
		_get_viewport()->queue_redraw();
	}
}

void CanvasItemEditor::edit(CanvasItem *p_canvas_item) {
	if (!p_canvas_item) {
		return;
	}

	Array selection = editor_selection->get_selected_nodes();
	if (selection.size() != 1 || Object::cast_to<Node>(selection[0]) != p_canvas_item) {
		_reset_drag();
	}
}

void CanvasItemEditor::_button_toggle_local_space(bool p_status) {
	use_local_space = p_status;
	_get_viewport()->queue_redraw();
}

void CanvasItemEditor::_button_toggle_smart_snap(bool p_status) {
	smart_snap_active = p_status;
	_get_viewport()->queue_redraw();
}

void CanvasItemEditor::_button_toggle_grid_snap(bool p_status) {
	grid_snap_active = p_status;
	_get_viewport()->queue_redraw();
}

void CanvasItemEditor::_button_tool_select(int p_index) {
	if (view_state.drag_type != CanvasItemEditorViewState::DRAG_NONE) {
		_commit_drag();
	}

	Button *tb[TOOL_MAX] = { select_button, list_select_button, move_button, scale_button, rotate_button, pivot_button, pan_button, ruler_button };
	for (int i = 0; i < TOOL_MAX; i++) {
		tb[i]->set_pressed(i == p_index);
	}

	tool = (Tool)p_index;

	if (p_index == TOOL_EDIT_PIVOT && Input::get_singleton()->is_key_pressed(Key::SHIFT)) {
		// Special action that places temporary rotation pivot in the middle of the selection.
		List<CanvasItem *> selection = _get_edited_canvas_items();
		if (!selection.is_empty()) {
			Vector2 center;
			for (const CanvasItem *ci : selection) {
				center += ci->get_viewport()->get_popup_base_transform().xform(ci->_edit_get_position());
			}
			view_state.temp_pivot = center / selection.size();
		}
	}

	_get_viewport()->queue_redraw();
	if (editor_view) {
		editor_view->update_cursor();
	}
}

void CanvasItemEditor::_insert_animation_keys(bool p_location, bool p_rotation, bool p_scale, bool p_on_existing) {
	const HashMap<ObjectID, Object *> &selection = editor_selection->get_selection();

	AnimationTrackEditor *te = AnimationPlayerEditor::get_singleton()->get_track_editor();
	ERR_FAIL_COND_MSG(te->get_current_animation().is_null(), "Cannot insert animation key. No animation selected.");

	bool is_read_only = te->is_read_only();
	if (is_read_only) {
		te->popup_read_only_dialog();
		return;
	}
	te->make_insert_queue();
	for (const KeyValue<ObjectID, Object *> &E : selection) {
		CanvasItem *ci = ObjectDB::get_instance<CanvasItem>(E.key);
		if (!ci || !ci->is_visible_in_tree()) {
			continue;
		}

		if (Object::cast_to<Node2D>(ci)) {
			Node2D *n2d = Object::cast_to<Node2D>(ci);

			if (key_pos && p_location) {
				te->insert_node_value_key(n2d, "position", p_on_existing);
			}
			if (key_rot && p_rotation) {
				te->insert_node_value_key(n2d, "rotation", p_on_existing);
			}
			if (key_scale && p_scale) {
				te->insert_node_value_key(n2d, "scale", p_on_existing);
			}

			if (n2d->has_meta("_edit_bone_") && n2d->get_parent_item()) {
				//look for an IK chain
				List<Node2D *> ik_chain;

				Node2D *n = Object::cast_to<Node2D>(n2d->get_parent_item());
				bool has_chain = false;

				while (n) {
					ik_chain.push_back(n);
					if (n->has_meta("_edit_ik_")) {
						has_chain = true;
						break;
					}

					if (!n->get_parent_item()) {
						break;
					}
					n = Object::cast_to<Node2D>(n->get_parent_item());
				}

				if (has_chain && ik_chain.size()) {
					for (Node2D *&F : ik_chain) {
						if (key_pos) {
							te->insert_node_value_key(F, "position", p_on_existing);
						}
						if (key_rot) {
							te->insert_node_value_key(F, "rotation", p_on_existing);
						}
						if (key_scale) {
							te->insert_node_value_key(F, "scale", p_on_existing);
						}
					}
				}
			}

		} else if (Object::cast_to<Control>(ci)) {
			Control *ctrl = Object::cast_to<Control>(ci);

			if (key_pos) {
				te->insert_node_value_key(ctrl, "position", p_on_existing);
			}
			if (key_rot) {
				te->insert_node_value_key(ctrl, "rotation", p_on_existing);
			}
			if (key_scale) {
				te->insert_node_value_key(ctrl, "size", p_on_existing);
			}
		}
	}
	te->commit_insert_queue();
}

void CanvasItemEditor::_prepare_view_menu() {
	PopupMenu *popup = view_menu->get_popup();

	Node *root = EditorNode::get_singleton()->get_edited_scene();
	bool has_guides = root && (root->has_meta("_edit_horizontal_guides_") || root->has_meta("_edit_vertical_guides_"));
	popup->set_item_disabled(popup->get_item_index(CLEAR_GUIDES), !has_guides);
}

void CanvasItemEditor::_popup_callback(int p_op) {
	EditorUndoRedoManager *undo_redo = EditorUndoRedoManager::get_singleton();
	last_option = MenuOption(p_op);
	switch (p_op) {
		case SHOW_ORIGIN: {
			show_origin = !show_origin;
			int idx = view_menu->get_popup()->get_item_index(SHOW_ORIGIN);
			view_menu->get_popup()->set_item_checked(idx, show_origin);
			_get_viewport()->queue_redraw();
		} break;
		case SHOW_VIEWPORT: {
			show_viewport = !show_viewport;
			int idx = view_menu->get_popup()->get_item_index(SHOW_VIEWPORT);
			view_menu->get_popup()->set_item_checked(idx, show_viewport);
			_get_viewport()->queue_redraw();
		} break;
		case SHOW_POSITION_GIZMOS: {
			show_position_gizmos = !show_position_gizmos;
			int idx = gizmos_menu->get_item_index(SHOW_POSITION_GIZMOS);
			gizmos_menu->set_item_checked(idx, show_position_gizmos);
			_get_viewport()->queue_redraw();
		} break;
		case SHOW_LOCK_GIZMOS: {
			show_lock_gizmos = !show_lock_gizmos;
			int idx = gizmos_menu->get_item_index(SHOW_LOCK_GIZMOS);
			gizmos_menu->set_item_checked(idx, show_lock_gizmos);
			_get_viewport()->queue_redraw();
		} break;
		case SHOW_GROUP_GIZMOS: {
			show_group_gizmos = !show_group_gizmos;
			int idx = gizmos_menu->get_item_index(SHOW_GROUP_GIZMOS);
			gizmos_menu->set_item_checked(idx, show_group_gizmos);
			_get_viewport()->queue_redraw();
		} break;
		case SHOW_TRANSFORMATION_GIZMOS: {
			show_transformation_gizmos = !show_transformation_gizmos;
			int idx = gizmos_menu->get_item_index(SHOW_TRANSFORMATION_GIZMOS);
			gizmos_menu->set_item_checked(idx, show_transformation_gizmos);
			_get_viewport()->queue_redraw();
		} break;
		case SNAP_USE_NODE_PARENT: {
			snap_node_parent = !snap_node_parent;
			int idx = smartsnap_config_popup->get_item_index(SNAP_USE_NODE_PARENT);
			smartsnap_config_popup->set_item_checked(idx, snap_node_parent);
		} break;
		case SNAP_USE_NODE_ANCHORS: {
			snap_node_anchors = !snap_node_anchors;
			int idx = smartsnap_config_popup->get_item_index(SNAP_USE_NODE_ANCHORS);
			smartsnap_config_popup->set_item_checked(idx, snap_node_anchors);
		} break;
		case SNAP_USE_NODE_SIDES: {
			snap_node_sides = !snap_node_sides;
			int idx = smartsnap_config_popup->get_item_index(SNAP_USE_NODE_SIDES);
			smartsnap_config_popup->set_item_checked(idx, snap_node_sides);
		} break;
		case SNAP_USE_NODE_CENTER: {
			snap_node_center = !snap_node_center;
			int idx = smartsnap_config_popup->get_item_index(SNAP_USE_NODE_CENTER);
			smartsnap_config_popup->set_item_checked(idx, snap_node_center);
		} break;
		case SNAP_USE_OTHER_NODES: {
			snap_other_nodes = !snap_other_nodes;
			int idx = smartsnap_config_popup->get_item_index(SNAP_USE_OTHER_NODES);
			smartsnap_config_popup->set_item_checked(idx, snap_other_nodes);
		} break;
		case SNAP_USE_GUIDES: {
			snap_guides = !snap_guides;
			int idx = smartsnap_config_popup->get_item_index(SNAP_USE_GUIDES);
			smartsnap_config_popup->set_item_checked(idx, snap_guides);
		} break;
		case SNAP_USE_ROTATION: {
			snap_rotation = !snap_rotation;
			int idx = snap_config_menu->get_popup()->get_item_index(SNAP_USE_ROTATION);
			snap_config_menu->get_popup()->set_item_checked(idx, snap_rotation);
		} break;
		case SNAP_USE_SCALE: {
			snap_scale = !snap_scale;
			int idx = snap_config_menu->get_popup()->get_item_index(SNAP_USE_SCALE);
			snap_config_menu->get_popup()->set_item_checked(idx, snap_scale);
		} break;
		case SNAP_RELATIVE: {
			snap_relative = !snap_relative;
			int idx = snap_config_menu->get_popup()->get_item_index(SNAP_RELATIVE);
			snap_config_menu->get_popup()->set_item_checked(idx, snap_relative);
			_get_viewport()->queue_redraw();
		} break;
		case SNAP_USE_PIXEL: {
			snap_pixel = !snap_pixel;
			int idx = snap_config_menu->get_popup()->get_item_index(SNAP_USE_PIXEL);
			snap_config_menu->get_popup()->set_item_checked(idx, snap_pixel);
		} break;
		case SNAP_CONFIGURE: {
			static_cast<SnapDialog *>(snap_dialog)->set_fields(grid_offset, grid_step, primary_grid_step, snap_rotation_offset, snap_rotation_step, snap_scale_step);
			snap_dialog->popup_centered(Size2(320, 160) * EDSCALE);
		} break;
		case SKELETON_SHOW_BONES: {
			List<Node *> selection = editor_selection->get_top_selected_node_list();
			for (Node *E : selection) {
				// Add children nodes so they are processed
				for (int child = 0; child < E->get_child_count(); child++) {
					selection.push_back(E->get_child(child));
				}

				Bone2D *bone_2d = Object::cast_to<Bone2D>(E);
				if (!bone_2d || !bone_2d->is_inside_tree()) {
					continue;
				}
				bone_2d->_editor_set_show_bone_gizmo(!bone_2d->_editor_get_show_bone_gizmo());
			}
		} break;
		case SHOW_HELPERS: {
			show_helpers = !show_helpers;
			int idx = view_menu->get_popup()->get_item_index(SHOW_HELPERS);
			view_menu->get_popup()->set_item_checked(idx, show_helpers);
			_get_viewport()->queue_redraw();
		} break;
		case SHOW_RULERS: {
			show_rulers = !show_rulers;
			int idx = view_menu->get_popup()->get_item_index(SHOW_RULERS);
			view_menu->get_popup()->set_item_checked(idx, show_rulers);
			update_viewport();
		} break;
		case SHOW_GUIDES: {
			show_guides = !show_guides;
			int idx = view_menu->get_popup()->get_item_index(SHOW_GUIDES);
			view_menu->get_popup()->set_item_checked(idx, show_guides);
			_get_viewport()->queue_redraw();
		} break;
		case LOCK_SELECTED: {
			undo_redo->create_action(TTR("Lock Selected"));

			const List<Node *> &selection = editor_selection->get_top_selected_node_list();
			for (Node *E : selection) {
				CanvasItem *ci = Object::cast_to<CanvasItem>(E);
				if (!ci || !ci->is_inside_tree()) {
					continue;
				}

				undo_redo->add_do_method(ci, "set_meta", "_edit_lock_", true);
				undo_redo->add_undo_method(ci, "remove_meta", "_edit_lock_");
				undo_redo->add_do_method(this, "emit_signal", "item_lock_status_changed");
				undo_redo->add_undo_method(this, "emit_signal", "item_lock_status_changed");
			}
			undo_redo->add_do_method(_get_viewport(), "queue_redraw");
			undo_redo->add_undo_method(_get_viewport(), "queue_redraw");
			undo_redo->commit_action();
		} break;
		case UNLOCK_SELECTED: {
			undo_redo->create_action(TTR("Unlock Selected"));

			const List<Node *> &selection = editor_selection->get_top_selected_node_list();
			for (Node *E : selection) {
				CanvasItem *ci = Object::cast_to<CanvasItem>(E);
				if (!ci || !ci->is_inside_tree()) {
					continue;
				}

				undo_redo->add_do_method(ci, "remove_meta", "_edit_lock_");
				undo_redo->add_undo_method(ci, "set_meta", "_edit_lock_", true);
				undo_redo->add_do_method(this, "emit_signal", "item_lock_status_changed");
				undo_redo->add_undo_method(this, "emit_signal", "item_lock_status_changed");
			}
			undo_redo->add_do_method(_get_viewport(), "queue_redraw");
			undo_redo->add_undo_method(_get_viewport(), "queue_redraw");
			undo_redo->commit_action();
		} break;
		case GROUP_SELECTED: {
			undo_redo->create_action(TTR("Group Selected"));

			const List<Node *> &selection = editor_selection->get_top_selected_node_list();
			for (Node *E : selection) {
				CanvasItem *ci = Object::cast_to<CanvasItem>(E);
				if (!ci || !ci->is_inside_tree()) {
					continue;
				}

				undo_redo->add_do_method(ci, "set_meta", "_edit_group_", true);
				undo_redo->add_undo_method(ci, "remove_meta", "_edit_group_");
				undo_redo->add_do_method(this, "emit_signal", "item_group_status_changed");
				undo_redo->add_undo_method(this, "emit_signal", "item_group_status_changed");
			}
			undo_redo->add_do_method(_get_viewport(), "queue_redraw");
			undo_redo->add_undo_method(_get_viewport(), "queue_redraw");
			undo_redo->commit_action();
		} break;
		case UNGROUP_SELECTED: {
			undo_redo->create_action(TTR("Ungroup Selected"));

			const List<Node *> &selection = editor_selection->get_top_selected_node_list();
			for (Node *E : selection) {
				CanvasItem *ci = Object::cast_to<CanvasItem>(E);
				if (!ci || !ci->is_inside_tree()) {
					continue;
				}

				undo_redo->add_do_method(ci, "remove_meta", "_edit_group_");
				undo_redo->add_undo_method(ci, "set_meta", "_edit_group_", true);
				undo_redo->add_do_method(this, "emit_signal", "item_group_status_changed");
				undo_redo->add_undo_method(this, "emit_signal", "item_group_status_changed");
			}
			undo_redo->add_do_method(_get_viewport(), "queue_redraw");
			undo_redo->add_undo_method(_get_viewport(), "queue_redraw");
			undo_redo->commit_action();
		} break;

		case ANIM_INSERT_KEY:
		case ANIM_INSERT_KEY_EXISTING: {
			bool existing = p_op == ANIM_INSERT_KEY_EXISTING;

			_insert_animation_keys(true, true, true, existing);

		} break;
		case ANIM_INSERT_POS: {
			key_pos = key_loc_button->is_pressed();
		} break;
		case ANIM_INSERT_ROT: {
			key_rot = key_rot_button->is_pressed();
		} break;
		case ANIM_INSERT_SCALE: {
			key_scale = key_scale_button->is_pressed();
		} break;
		case ANIM_COPY_POSE: {
			pose_clipboard.clear();

			const HashMap<ObjectID, Object *> &selection = editor_selection->get_selection();

			for (const KeyValue<ObjectID, Object *> &E : selection) {
				CanvasItem *ci = ObjectDB::get_instance<CanvasItem>(E.key);
				if (!ci || !ci->is_visible_in_tree()) {
					continue;
				}

				if (Object::cast_to<Node2D>(ci)) {
					Node2D *n2d = Object::cast_to<Node2D>(ci);
					PoseClipboard pc;
					pc.pos = n2d->get_position();
					pc.rot = n2d->get_rotation();
					pc.scale = n2d->get_scale();
					pc.id = n2d->get_instance_id();
					pose_clipboard.push_back(pc);
				}
			}

		} break;
		case ANIM_PASTE_POSE: {
			if (!pose_clipboard.size()) {
				break;
			}

			undo_redo->create_action(TTR("Paste Pose"));
			for (const PoseClipboard &E : pose_clipboard) {
				Node2D *n2d = ObjectDB::get_instance<Node2D>(E.id);
				if (!n2d) {
					continue;
				}
				undo_redo->add_do_method(n2d, "set_position", E.pos);
				undo_redo->add_do_method(n2d, "set_rotation", E.rot);
				undo_redo->add_do_method(n2d, "set_scale", E.scale);
				undo_redo->add_undo_method(n2d, "set_position", n2d->get_position());
				undo_redo->add_undo_method(n2d, "set_rotation", n2d->get_rotation());
				undo_redo->add_undo_method(n2d, "set_scale", n2d->get_scale());
			}
			undo_redo->commit_action();

		} break;
		case ANIM_CLEAR_POSE: {
			HashMap<ObjectID, Object *> &selection = editor_selection->get_selection();

			for (const KeyValue<ObjectID, Object *> &E : selection) {
				CanvasItem *ci = ObjectDB::get_instance<CanvasItem>(E.key);
				if (!ci || !ci->is_visible_in_tree()) {
					continue;
				}

				if (Object::cast_to<Node2D>(ci)) {
					Node2D *n2d = Object::cast_to<Node2D>(ci);

					if (key_pos) {
						n2d->set_position(Vector2());
					}
					if (key_rot) {
						n2d->set_rotation(0);
					}
					if (key_scale) {
						n2d->set_scale(Vector2(1, 1));
					}
				} else if (Object::cast_to<Control>(ci)) {
					Control *ctrl = Object::cast_to<Control>(ci);

					if (key_pos) {
						ctrl->set_position(Point2());
					}
				}
			}

		} break;
		case CLEAR_GUIDES: {
			Node *const root = EditorNode::get_singleton()->get_edited_scene();

			if (root && (root->has_meta("_edit_horizontal_guides_") || root->has_meta("_edit_vertical_guides_"))) {
				undo_redo->create_action(TTR("Clear Guides"));
				if (root->has_meta("_edit_horizontal_guides_")) {
					Array hguides = root->get_meta("_edit_horizontal_guides_");

					undo_redo->add_do_method(root, "remove_meta", "_edit_horizontal_guides_");
					undo_redo->add_undo_method(root, "set_meta", "_edit_horizontal_guides_", hguides);
				}
				if (root->has_meta("_edit_vertical_guides_")) {
					Array vguides = root->get_meta("_edit_vertical_guides_");

					undo_redo->add_do_method(root, "remove_meta", "_edit_vertical_guides_");
					undo_redo->add_undo_method(root, "set_meta", "_edit_vertical_guides_", vguides);
				}
				undo_redo->add_do_method(_get_viewport(), "queue_redraw");
				undo_redo->add_undo_method(_get_viewport(), "queue_redraw");
				undo_redo->commit_action();
			}

		} break;
		case VIEW_CENTER_TO_SELECTION:
		case VIEW_FRAME_TO_SELECTION: {
			_focus_selection(p_op);

		} break;
		case PREVIEW_CANVAS_SCALE: {
			bool preview = view_menu->get_popup()->is_item_checked(view_menu->get_popup()->get_item_index(PREVIEW_CANVAS_SCALE));
			preview = !preview;
			RS::get_singleton()->canvas_set_disable_scale(!preview);
			view_menu->get_popup()->set_item_checked(view_menu->get_popup()->get_item_index(PREVIEW_CANVAS_SCALE), preview);

		} break;
		case SKELETON_MAKE_BONES: {
			HashMap<ObjectID, Object *> &selection = editor_selection->get_selection();
			Node *editor_root = get_tree()->get_edited_scene_root();

			if (!editor_root || selection.is_empty()) {
				return;
			}

			undo_redo->create_action(TTR("Create Custom Bone2D(s) from Node(s)"));
			for (const KeyValue<ObjectID, Object *> &E : selection) {
				Node2D *n2d = ObjectDB::get_instance<Node2D>(E.key);
				if (!n2d) {
					continue;
				}

				Bone2D *new_bone = memnew(Bone2D);
				String new_bone_name = n2d->get_name();
				new_bone_name += "Bone2D";
				new_bone->set_name(new_bone_name);
				new_bone->set_transform(n2d->get_transform());

				Node *n2d_parent = n2d->get_parent();
				if (!n2d_parent) {
					continue;
				}

				undo_redo->add_do_method(n2d_parent, "add_child", new_bone);
				undo_redo->add_do_method(n2d_parent, "remove_child", n2d);
				undo_redo->add_do_method(new_bone, "add_child", n2d);
				undo_redo->add_do_method(n2d, "set_transform", Transform2D());
				undo_redo->add_do_method(this, "_set_owner_for_node_and_children", new_bone, editor_root);
				undo_redo->add_do_reference(new_bone);

				undo_redo->add_undo_method(new_bone, "remove_child", n2d);
				undo_redo->add_undo_method(n2d_parent, "add_child", n2d);
				undo_redo->add_undo_method(n2d_parent, "remove_child", new_bone);
				undo_redo->add_undo_method(n2d, "set_transform", new_bone->get_transform());
				undo_redo->add_undo_method(this, "_set_owner_for_node_and_children", n2d, editor_root);
			}
			undo_redo->commit_action();

		} break;
		case AUTO_RESAMPLE_CANVAS_ITEMS: {
			auto_resampling_enabled = !auto_resampling_enabled;
			int idx = view_menu->get_popup()->get_item_index(AUTO_RESAMPLE_CANVAS_ITEMS);
			view_menu->get_popup()->set_item_checked(idx, auto_resampling_enabled);
			if (!resample_timer->is_stopped()) {
				resample_timer->stop();
			}
			_update_oversampling();
			EditorSettings::get_singleton()->set_project_metadata("2d_editor", "auto_resampling_enabled", auto_resampling_enabled);
		} break;
	}
}

void CanvasItemEditor::_set_owner_for_node_and_children(Node *p_node, Node *p_owner) {
	p_node->set_owner(p_owner);
	for (int i = 0; i < p_node->get_child_count(); i++) {
		_set_owner_for_node_and_children(p_node->get_child(i), p_owner);
	}
}

void CanvasItemEditor::_focus_selection(int p_op) {
	Rect2 rect;
	int count = 0;

	const HashMap<ObjectID, Object *> &selection = editor_selection->get_selection();
	for (const KeyValue<ObjectID, Object *> &E : selection) {
		CanvasItem *ci = ObjectDB::get_instance<CanvasItem>(E.key);
		if (!ci) {
			continue;
		}
		const Transform2D canvas_item_transform = ci->get_global_transform();
		if (!canvas_item_transform.is_finite()) {
			continue;
		}
		Rect2 item_rect;
		if (ci->_edit_use_rect()) {
			item_rect = ci->_edit_get_rect();
		} else {
			item_rect = Rect2();
		}
		Vector2 pos = canvas_item_transform.get_origin();
		const Vector2 scale = canvas_item_transform.get_scale();
		const real_t angle = canvas_item_transform.get_rotation();
		pos = ci->get_viewport()->get_popup_base_transform().xform(pos);

		Transform2D t(angle, Vector2(0.f, 0.f));
		item_rect = t.xform(item_rect);
		Rect2 canvas_item_rect(pos + scale * item_rect.position, scale * item_rect.size);
		if (count == 0) {
			rect = canvas_item_rect;
		} else {
			rect = rect.merge(canvas_item_rect);
		}
		count++;
	}

	if (p_op == VIEW_FRAME_TO_SELECTION && rect.size.x > CMP_EPSILON && rect.size.y > CMP_EPSILON) {
		real_t scale_x = _get_viewport()->get_size().x / rect.size.x;
		real_t scale_y = _get_viewport()->get_size().y / rect.size.y;
		view_state.zoom = scale_x < scale_y ? scale_x : scale_y;
		view_state.zoom *= 0.90;
		editor_view->get_zoom_widget()->set_zoom(view_state.zoom);
		_get_viewport()->queue_redraw(); // Redraw to update the global canvas view_state.transform after view_state.zoom changes.
		callable_mp(this, &CanvasItemEditor::center_at).call_deferred(rect.get_center()); // Defer because the updated view_state.transform is needed.
		if (auto_resampling_enabled) {
			resample_timer->start();
		}
	} else {
		center_at(rect.get_center());
	}
}

void CanvasItemEditor::_reset_drag() {
	view_state.reset_drag();
}

void CanvasItemEditor::_bind_methods() {
	ClassDB::bind_method("_get_editor_data", &CanvasItemEditor::_get_editor_data);

	ClassDB::bind_method(D_METHOD("update_viewport"), &CanvasItemEditor::update_viewport);
	ClassDB::bind_method(D_METHOD("center_at", "position"), &CanvasItemEditor::center_at);

	ClassDB::bind_method("_set_owner_for_node_and_children", &CanvasItemEditor::_set_owner_for_node_and_children);

	ADD_SIGNAL(MethodInfo("item_lock_status_changed"));
	ADD_SIGNAL(MethodInfo("item_group_status_changed"));
}

Dictionary CanvasItemEditor::get_state() const {
	// Per-scene geometry only (zoom/pan). Global snap/show/grid config lives on the
	// shared controller and is not round-tripped per scene (#929).
	Dictionary state = CanvasItemEditorSceneGeometryState::to_dict(view_state);
	if (const CanvasItemEditorView *view = get_focused_view()) {
		state["show_zoom_control"] = view->get_zoom_widget()->is_visible();
	}
	return state;
}

void CanvasItemEditor::set_state(const Dictionary &p_state) {
	// Per-scene geometry only. Legacy dictionaries may still carry global config keys
	// from older editors; those are ignored so scene switches cannot clobber toolbar state.
	bool update_scrollbars = false;
	CanvasItemEditorSceneGeometryState::apply(view_state, p_state);

	if (p_state.has("zoom")) {
		if (CanvasItemEditorView *view = get_focused_view()) {
			view->get_zoom_widget()->set_zoom(view_state.zoom);
		}
		if (auto_resampling_enabled) {
			resample_timer->start();
		}
	}

	if (p_state.has("ofs")) {
		update_scrollbars = true;
	}

	if (p_state.has("show_zoom_control")) {
		if (CanvasItemEditorView *view = get_focused_view()) {
			view->get_zoom_widget()->set_visible(p_state["show_zoom_control"]);
		}
	}

	if (update_scrollbars) {
		_update_scrollbars();
	}
	if (CanvasItemEditorViewport *viewport = _get_viewport()) {
		viewport->queue_redraw();
	}
}

void CanvasItemEditor::clear() {
	view_state.zoom = 1.0 / MAX(1, EDSCALE);
	editor_view->get_zoom_widget()->set_zoom(view_state.zoom);

	view_state.view_offset = Point2(-150 - ruler_width_scaled, -95 - ruler_width_scaled);
	view_state.previous_update_view_offset = view_state.view_offset; // Moves the view a little bit to the left so that (0,0) is visible. The values a relative to a 16/10 screen.
	_update_scrollbars();

	grid_offset = EditorSettings::get_singleton()->get_project_metadata("2d_editor", "grid_offset", Vector2());
	grid_step = EditorSettings::get_singleton()->get_project_metadata("2d_editor", "grid_step", Vector2(8, 8));
	primary_grid_step = EditorSettings::get_singleton()->get_project_metadata("2d_editor", "primary_grid_step", Vector2i(8, 8));
	snap_rotation_step = EditorSettings::get_singleton()->get_project_metadata("2d_editor", "snap_rotation_step", Math::deg_to_rad(15.0));
	snap_rotation_offset = EditorSettings::get_singleton()->get_project_metadata("2d_editor", "snap_rotation_offset", 0.0);
	snap_scale_step = EditorSettings::get_singleton()->get_project_metadata("2d_editor", "snap_scale_step", 0.1);

	if (auto_resampling_enabled) {
		if (resample_timer->is_inside_tree()) {
			resample_timer->start();
		} else {
			_update_oversampling();
		}
	}
}

void CanvasItemEditor::add_control_to_menu_panel(Control *p_control) {
	ERR_FAIL_NULL(p_control);
	ERR_FAIL_COND(p_control->get_parent());

	VSeparator *sep = memnew(VSeparator);
	context_toolbar_hbox->add_child(sep);
	context_toolbar_hbox->add_child(p_control);
	context_toolbar_separators[p_control] = sep;

	p_control->connect(SceneStringName(visibility_changed), callable_mp(this, &CanvasItemEditor::_update_context_toolbar));

	_update_context_toolbar();
}

void CanvasItemEditor::remove_control_from_menu_panel(Control *p_control) {
	ERR_FAIL_NULL(p_control);
	ERR_FAIL_COND(p_control->get_parent() != context_toolbar_hbox);

	p_control->disconnect(SceneStringName(visibility_changed), callable_mp(this, &CanvasItemEditor::_update_context_toolbar));

	VSeparator *sep = context_toolbar_separators[p_control];
	context_toolbar_hbox->remove_child(sep);
	context_toolbar_hbox->remove_child(p_control);
	context_toolbar_separators.erase(p_control);
	memdelete(sep);

	_update_context_toolbar();
}

void CanvasItemEditor::_update_context_toolbar() {
	bool has_visible = false;
	bool first_visible = false;

	for (int i = 0; i < context_toolbar_hbox->get_child_count(); i++) {
		Control *child = Object::cast_to<Control>(context_toolbar_hbox->get_child(i));
		if (!child || !context_toolbar_separators.has(child)) {
			continue;
		}
		if (child->is_visible()) {
			first_visible = !has_visible;
			has_visible = true;
		}

		VSeparator *sep = context_toolbar_separators[child];
		sep->set_visible(!first_visible && child->is_visible());
	}

	context_toolbar_panel->set_visible(has_visible);
}

void CanvasItemEditor::add_control_to_left_panel(Control *p_control) {
	left_panel_split->add_child(p_control);
	left_panel_split->move_child(p_control, 0);
}

void CanvasItemEditor::add_control_to_right_panel(Control *p_control) {
	right_panel_split->add_child(p_control);
	right_panel_split->move_child(p_control, 1);
}

void CanvasItemEditor::remove_control_from_left_panel(Control *p_control) {
	left_panel_split->remove_child(p_control);
}

void CanvasItemEditor::remove_control_from_right_panel(Control *p_control) {
	right_panel_split->remove_child(p_control);
}

VSplitContainer *CanvasItemEditor::get_bottom_split() {
	return bottom_split;
}

void CanvasItemEditor::focus_selection() {
	_focus_selection(VIEW_CENTER_TO_SELECTION);
}

CanvasItemEditor::CanvasItemEditor() {
	view_state.reset_snap_in_progress();

	editor_selection = EditorNode::get_singleton()->get_editor_selection();
	EditorNode::get_singleton()->add_editor_selection_plugin(this);
	EditorNode::get_singleton()->connect_editor_selection_changed(callable_mp((CanvasItem *)this, &CanvasItem::queue_redraw));
	EditorNode::get_singleton()->connect_editor_selection_changed(callable_mp(this, &CanvasItemEditor::_selection_changed));

	SceneTreeDock::get_singleton()->connect("node_created", callable_mp(this, &CanvasItemEditor::_adjust_new_node_position));
	SceneTreeDock::get_singleton()->connect("add_node_used", callable_mp(this, &CanvasItemEditor::_reset_create_position));

	MarginContainer *toolbar_margin = memnew(MarginContainer);
	toolbar_margin->set_theme_type_variation("MainToolBarMargin");
	add_child(toolbar_margin);

	// A fluid container for all toolbars.
	HFlowContainer *main_flow = memnew(HFlowContainer);
	toolbar_margin->add_child(main_flow);

	// Main toolbars.
	HBoxContainer *main_menu_hbox = memnew(HBoxContainer);
	main_menu_hbox->set_anchors_and_offsets_preset(Control::PRESET_FULL_RECT);
	main_flow->add_child(main_menu_hbox);

	bottom_split = memnew(VSplitContainer);
	add_child(bottom_split);
	bottom_split->set_v_size_flags(Control::SIZE_EXPAND_FILL);

	left_panel_split = memnew(HSplitContainer);
	bottom_split->add_child(left_panel_split);
	left_panel_split->set_v_size_flags(Control::SIZE_EXPAND_FILL);

	right_panel_split = memnew(HSplitContainer);
	left_panel_split->add_child(right_panel_split);
	right_panel_split->set_v_size_flags(Control::SIZE_EXPAND_FILL);

	ED_SHORTCUT("canvas_item_editor/cancel_transform", TTRC("Cancel Transformation"), Key::ESCAPE);

	// To ensure that scripts can parse the list of shortcuts correctly, we have to define
	// those shortcuts one by one. Define shortcut before using it (by EditorZoomWidget).
	ED_SHORTCUT_ARRAY("canvas_item_editor/zoom_3.125_percent", TTRC("Zoom to 3.125%"),
			{ int32_t(KeyModifierMask::SHIFT | Key::KEY_5), int32_t(KeyModifierMask::SHIFT | Key::KP_5) });

	ED_SHORTCUT_ARRAY("canvas_item_editor/zoom_6.25_percent", TTRC("Zoom to 6.25%"),
			{ int32_t(KeyModifierMask::SHIFT | Key::KEY_4), int32_t(KeyModifierMask::SHIFT | Key::KP_4) });

	ED_SHORTCUT_ARRAY("canvas_item_editor/zoom_12.5_percent", TTRC("Zoom to 12.5%"),
			{ int32_t(KeyModifierMask::SHIFT | Key::KEY_3), int32_t(KeyModifierMask::SHIFT | Key::KP_3) });

	ED_SHORTCUT_ARRAY("canvas_item_editor/zoom_25_percent", TTRC("Zoom to 25%"),
			{ int32_t(KeyModifierMask::SHIFT | Key::KEY_2), int32_t(KeyModifierMask::SHIFT | Key::KP_2) });

	ED_SHORTCUT_ARRAY("canvas_item_editor/zoom_50_percent", TTRC("Zoom to 50%"),
			{ int32_t(KeyModifierMask::SHIFT | Key::KEY_1), int32_t(KeyModifierMask::SHIFT | Key::KP_1) });

	ED_SHORTCUT_ARRAY("canvas_item_editor/zoom_100_percent", TTRC("Zoom to 100%"),
			{ int32_t(Key::KEY_1), int32_t(KeyModifierMask::CMD_OR_CTRL | Key::KEY_0), int32_t(Key::KP_1), int32_t(KeyModifierMask::CMD_OR_CTRL | Key::KP_0) });

	ED_SHORTCUT_ARRAY("canvas_item_editor/zoom_200_percent", TTRC("Zoom to 200%"),
			{ int32_t(Key::KEY_2), int32_t(Key::KP_2) });

	ED_SHORTCUT_ARRAY("canvas_item_editor/zoom_400_percent", TTRC("Zoom to 400%"),
			{ int32_t(Key::KEY_3), int32_t(Key::KP_3) });

	ED_SHORTCUT_ARRAY("canvas_item_editor/zoom_800_percent", TTRC("Zoom to 800%"),
			{ int32_t(Key::KEY_4), int32_t(Key::KP_4) });

	ED_SHORTCUT_ARRAY("canvas_item_editor/zoom_1600_percent", TTRC("Zoom to 1600%"),
			{ int32_t(Key::KEY_5), int32_t(Key::KP_5) });

	editor_view = memnew(CanvasItemEditorView(this, view_state));
	views.push_back(editor_view);
	editor_view->build_ui(right_panel_split);

	select_button = memnew(Button);
	select_button->set_tooltip_auto_translate_mode(AUTO_TRANSLATE_MODE_DISABLED);
	select_button->set_theme_type_variation(SceneStringName(FlatButton));
	main_menu_hbox->add_child(select_button);
	select_button->set_toggle_mode(true);
	select_button->connect(SceneStringName(pressed), callable_mp(this, &CanvasItemEditor::_button_tool_select).bind(TOOL_SELECT));
	select_button->set_pressed(true);
	select_button->set_shortcut(ED_SHORTCUT("canvas_item_editor/select_mode", TTRC("Select Mode"), Key::Q, true));
	select_button->set_shortcut_context(this);
	select_button->set_accessibility_name(TTRC("Select Mode"));

	main_menu_hbox->add_child(memnew(VSeparator));

	move_button = memnew(Button);
	move_button->set_theme_type_variation(SceneStringName(FlatButton));
	main_menu_hbox->add_child(move_button);
	move_button->set_toggle_mode(true);
	move_button->connect(SceneStringName(pressed), callable_mp(this, &CanvasItemEditor::_button_tool_select).bind(TOOL_MOVE));
	move_button->set_shortcut(ED_SHORTCUT("canvas_item_editor/move_mode", TTRC("Move Mode"), Key::W, true));
	move_button->set_shortcut_context(this);
	move_button->set_tooltip_text(TTRC("Move Mode"));

	rotate_button = memnew(Button);
	rotate_button->set_theme_type_variation(SceneStringName(FlatButton));
	main_menu_hbox->add_child(rotate_button);
	rotate_button->set_toggle_mode(true);
	rotate_button->connect(SceneStringName(pressed), callable_mp(this, &CanvasItemEditor::_button_tool_select).bind(TOOL_ROTATE));
	rotate_button->set_shortcut(ED_SHORTCUT("canvas_item_editor/rotate_mode", TTRC("Rotate Mode"), Key::E, true));
	rotate_button->set_shortcut_context(this);
	rotate_button->set_tooltip_text(TTRC("Rotate Mode"));

	scale_button = memnew(Button);
	scale_button->set_theme_type_variation(SceneStringName(FlatButton));
	main_menu_hbox->add_child(scale_button);
	scale_button->set_toggle_mode(true);
	scale_button->connect(SceneStringName(pressed), callable_mp(this, &CanvasItemEditor::_button_tool_select).bind(TOOL_SCALE));
	scale_button->set_shortcut(ED_SHORTCUT("canvas_item_editor/scale_mode", TTRC("Scale Mode"), Key::R, true));
	scale_button->set_shortcut_context(this);
	scale_button->set_tooltip_text(TTRC("Shift: Scale proportionally."));
	scale_button->set_accessibility_name(TTRC("Scale Mode"));

	main_menu_hbox->add_child(memnew(VSeparator));

	list_select_button = memnew(Button);
	list_select_button->set_theme_type_variation(SceneStringName(FlatButton));
	main_menu_hbox->add_child(list_select_button);
	list_select_button->set_toggle_mode(true);
	list_select_button->connect(SceneStringName(pressed), callable_mp(this, &CanvasItemEditor::_button_tool_select).bind(TOOL_LIST_SELECT));
	list_select_button->set_tooltip_text(TTRC("Show list of selectable nodes at position clicked."));

	pivot_button = memnew(Button);
	pivot_button->set_tooltip_auto_translate_mode(AUTO_TRANSLATE_MODE_DISABLED);
	pivot_button->set_theme_type_variation(SceneStringName(FlatButton));
	main_menu_hbox->add_child(pivot_button);
	pivot_button->set_toggle_mode(true);
	pivot_button->connect(SceneStringName(pressed), callable_mp(this, &CanvasItemEditor::_button_tool_select).bind(TOOL_EDIT_PIVOT));
	pivot_button->set_accessibility_name(TTRC("Change Pivot"));

	pan_button = memnew(Button);
	pan_button->set_theme_type_variation(SceneStringName(FlatButton));
	main_menu_hbox->add_child(pan_button);
	pan_button->set_toggle_mode(true);
	pan_button->connect(SceneStringName(pressed), callable_mp(this, &CanvasItemEditor::_button_tool_select).bind(TOOL_PAN));
	pan_button->set_shortcut(ED_SHORTCUT("canvas_item_editor/pan_mode", TTRC("Pan Mode"), Key::G));
	pan_button->set_shortcut_context(this);
	pan_button->set_tooltip_text(TTRC("You can also use Pan View shortcut (Space by default) to pan in any mode."));
	pan_button->set_accessibility_name(TTRC("Pan View"));

	ruler_button = memnew(Button);
	ruler_button->set_theme_type_variation(SceneStringName(FlatButton));
	main_menu_hbox->add_child(ruler_button);
	ruler_button->set_toggle_mode(true);
	ruler_button->connect(SceneStringName(pressed), callable_mp(this, &CanvasItemEditor::_button_tool_select).bind(TOOL_RULER));
	ruler_button->set_shortcut(ED_SHORTCUT("canvas_item_editor/ruler_mode", TTRC("Ruler Mode"), Key::M));
	ruler_button->set_shortcut_context(this);
	ruler_button->set_tooltip_text(TTRC("Ruler Mode"));

	main_menu_hbox->add_child(memnew(VSeparator));

	local_space_button = memnew(Button);
	local_space_button->set_theme_type_variation(SceneStringName(FlatButton));
	main_menu_hbox->add_child(local_space_button);
	local_space_button->set_toggle_mode(true);
	local_space_button->set_pressed_no_signal(true);
	local_space_button->connect(SceneStringName(toggled), callable_mp(this, &CanvasItemEditor::_button_toggle_local_space));
	local_space_button->set_shortcut(ED_SHORTCUT("canvas_item_editor/use_local_space", TTRC("Use Local Space"), Key::T));
	local_space_button->set_shortcut_context(this);
	local_space_button->set_accessibility_name(TTRC("Use Local Space"));

	smart_snap_button = memnew(Button);
	smart_snap_button->set_theme_type_variation(SceneStringName(FlatButton));
	main_menu_hbox->add_child(smart_snap_button);
	smart_snap_button->set_toggle_mode(true);
	smart_snap_button->connect(SceneStringName(toggled), callable_mp(this, &CanvasItemEditor::_button_toggle_smart_snap));
	smart_snap_button->set_tooltip_text(TTRC("Toggle smart snapping."));
	smart_snap_button->set_shortcut(ED_SHORTCUT("canvas_item_editor/use_smart_snap", TTRC("Use Smart Snap"), KeyModifierMask::SHIFT | Key::S));
	smart_snap_button->set_shortcut_context(this);

	grid_snap_button = memnew(Button);
	grid_snap_button->set_theme_type_variation(SceneStringName(FlatButton));
	main_menu_hbox->add_child(grid_snap_button);
	grid_snap_button->set_toggle_mode(true);
	grid_snap_button->connect(SceneStringName(toggled), callable_mp(this, &CanvasItemEditor::_button_toggle_grid_snap));
	grid_snap_button->set_tooltip_text(TTRC("Toggle grid snapping."));
	grid_snap_button->set_shortcut(ED_SHORTCUT("canvas_item_editor/use_grid_snap", TTRC("Use Grid Snap"), KeyModifierMask::SHIFT | Key::G));
	grid_snap_button->set_shortcut_context(this);

	snap_config_menu = memnew(MenuButton);
	snap_config_menu->set_flat(false);
	snap_config_menu->set_theme_type_variation("FlatMenuButton");
	snap_config_menu->set_shortcut_context(this);
	main_menu_hbox->add_child(snap_config_menu);
	snap_config_menu->set_h_size_flags(SIZE_SHRINK_END);
	snap_config_menu->set_tooltip_text(TTRC("Snapping Options"));
	snap_config_menu->set_switch_on_hover(true);

	PopupMenu *p = snap_config_menu->get_popup();
	p->connect(SceneStringName(id_pressed), callable_mp(this, &CanvasItemEditor::_popup_callback));
	p->set_hide_on_checkable_item_selection(false);
	p->add_check_shortcut(ED_SHORTCUT("canvas_item_editor/use_rotation_snap", TTRC("Use Rotation Snap")), SNAP_USE_ROTATION);
	p->add_check_shortcut(ED_SHORTCUT("canvas_item_editor/use_scale_snap", TTRC("Use Scale Snap")), SNAP_USE_SCALE);
	p->add_check_shortcut(ED_SHORTCUT("canvas_item_editor/snap_relative", TTRC("Snap Relative")), SNAP_RELATIVE);
	p->add_check_shortcut(ED_SHORTCUT("canvas_item_editor/use_pixel_snap", TTRC("Use Pixel Snap")), SNAP_USE_PIXEL);

	smartsnap_config_popup = memnew(PopupMenu);
	smartsnap_config_popup->connect(SceneStringName(id_pressed), callable_mp(this, &CanvasItemEditor::_popup_callback));
	smartsnap_config_popup->set_hide_on_checkable_item_selection(false);
	smartsnap_config_popup->add_check_shortcut(ED_SHORTCUT("canvas_item_editor/snap_node_parent", TTRC("Snap to Parent")), SNAP_USE_NODE_PARENT);
	smartsnap_config_popup->add_check_shortcut(ED_SHORTCUT("canvas_item_editor/snap_node_anchors", TTRC("Snap to Node Anchor")), SNAP_USE_NODE_ANCHORS);
	smartsnap_config_popup->add_check_shortcut(ED_SHORTCUT("canvas_item_editor/snap_node_sides", TTRC("Snap to Node Sides")), SNAP_USE_NODE_SIDES);
	smartsnap_config_popup->add_check_shortcut(ED_SHORTCUT("canvas_item_editor/snap_node_center", TTRC("Snap to Node Center")), SNAP_USE_NODE_CENTER);
	smartsnap_config_popup->add_check_shortcut(ED_SHORTCUT("canvas_item_editor/snap_other_nodes", TTRC("Snap to Other Nodes")), SNAP_USE_OTHER_NODES);
	smartsnap_config_popup->add_check_shortcut(ED_SHORTCUT("canvas_item_editor/snap_guides", TTRC("Snap to Guides")), SNAP_USE_GUIDES);
	p->add_submenu_node_item(TTRC("Smart Snapping"), smartsnap_config_popup);

	p->add_separator();
	p->add_shortcut(ED_SHORTCUT("canvas_item_editor/configure_snap", TTRC("Configure Snap...")), SNAP_CONFIGURE);

	main_menu_hbox->add_child(memnew(VSeparator));

	lock_button = memnew(Button);
	lock_button->set_theme_type_variation(SceneStringName(FlatButton));
	lock_button->set_accessibility_name(TTRC("Lock"));
	main_menu_hbox->add_child(lock_button);

	lock_button->connect(SceneStringName(pressed), callable_mp(this, &CanvasItemEditor::_popup_callback).bind(LOCK_SELECTED));
	lock_button->set_tooltip_text(TTRC("Lock selected node, preventing selection and movement."));
	// Define the shortcut globally (without a context) so that it works if the Scene tree dock is currently focused.
	lock_button->set_shortcut(ED_GET_SHORTCUT("editor/lock_selected_nodes"));

	unlock_button = memnew(Button);
	unlock_button->set_accessibility_name(TTRC("Unlock"));
	unlock_button->set_theme_type_variation(SceneStringName(FlatButton));
	main_menu_hbox->add_child(unlock_button);
	unlock_button->connect(SceneStringName(pressed), callable_mp(this, &CanvasItemEditor::_popup_callback).bind(UNLOCK_SELECTED));
	unlock_button->set_tooltip_text(TTRC("Unlock selected node, allowing selection and movement."));
	// Define the shortcut globally (without a context) so that it works if the Scene tree dock is currently focused.
	unlock_button->set_shortcut(ED_GET_SHORTCUT("editor/unlock_selected_nodes"));

	group_button = memnew(Button);
	group_button->set_accessibility_name(TTRC("Group"));
	group_button->set_theme_type_variation(SceneStringName(FlatButton));
	main_menu_hbox->add_child(group_button);
	group_button->connect(SceneStringName(pressed), callable_mp(this, &CanvasItemEditor::_popup_callback).bind(GROUP_SELECTED));
	group_button->set_tooltip_text(TTRC("Groups the selected node with its children. This causes the parent to be selected when any child node is clicked in 2D and 3D view."));
	// Define the shortcut globally (without a context) so that it works if the Scene tree dock is currently focused.
	group_button->set_shortcut(ED_GET_SHORTCUT("editor/group_selected_nodes"));

	ungroup_button = memnew(Button);
	ungroup_button->set_accessibility_name(TTRC("Ungroup"));
	ungroup_button->set_theme_type_variation(SceneStringName(FlatButton));
	main_menu_hbox->add_child(ungroup_button);
	ungroup_button->connect(SceneStringName(pressed), callable_mp(this, &CanvasItemEditor::_popup_callback).bind(UNGROUP_SELECTED));
	ungroup_button->set_tooltip_text(TTRC("Ungroups the selected node from its children. Child nodes will be individual items in 2D and 3D view."));
	// Define the shortcut globally (without a context) so that it works if the Scene tree dock is currently focused.
	ungroup_button->set_shortcut(ED_GET_SHORTCUT("editor/ungroup_selected_nodes"));

	main_menu_hbox->add_child(memnew(VSeparator));

	skeleton_menu = memnew(MenuButton);
	skeleton_menu->set_flat(false);
	skeleton_menu->set_theme_type_variation("FlatMenuButton");
	skeleton_menu->set_shortcut_context(this);
	main_menu_hbox->add_child(skeleton_menu);
	skeleton_menu->set_tooltip_text(TTRC("Skeleton Options"));
	skeleton_menu->set_switch_on_hover(true);

	p = skeleton_menu->get_popup();
	p->set_hide_on_checkable_item_selection(false);
	p->add_shortcut(ED_SHORTCUT("canvas_item_editor/skeleton_show_bones", TTRC("Show Bones")), SKELETON_SHOW_BONES);
	p->add_separator();
	p->add_shortcut(ED_SHORTCUT("canvas_item_editor/skeleton_make_bones", TTRC("Make Bone2D Node(s) from Node(s)"), KeyModifierMask::CMD_OR_CTRL | KeyModifierMask::SHIFT | Key::B), SKELETON_MAKE_BONES);
	p->connect(SceneStringName(id_pressed), callable_mp(this, &CanvasItemEditor::_popup_callback));

	main_menu_hbox->add_child(memnew(VSeparator));

	view_menu = memnew(MenuButton);
	view_menu->set_flat(false);
	view_menu->set_theme_type_variation("FlatMenuButton");
	// TRANSLATORS: Noun, name of the 2D/3D View menus.
	view_menu->set_text(TTRC("View"));
	view_menu->set_switch_on_hover(true);
	view_menu->set_shortcut_context(this);
	main_menu_hbox->add_child(view_menu);

	p = view_menu->get_popup();
	p->connect(SceneStringName(id_pressed), callable_mp(this, &CanvasItemEditor::_popup_callback));
	p->connect("about_to_popup", callable_mp(this, &CanvasItemEditor::_prepare_view_menu));
	p->set_hide_on_checkable_item_selection(false);

	grid_menu = memnew(PopupMenu);
	grid_menu->connect("about_to_popup", callable_mp(this, &CanvasItemEditor::_prepare_grid_menu));
	grid_menu->connect(SceneStringName(id_pressed), callable_mp(this, &CanvasItemEditor::_on_grid_menu_id_pressed));
	grid_menu->add_radio_check_item(TTRC("Show"), GRID_VISIBILITY_SHOW);
	grid_menu->add_radio_check_item(TTRC("Show When Snapping"), GRID_VISIBILITY_SHOW_WHEN_SNAPPING);
	grid_menu->add_radio_check_item(TTRC("Hide"), GRID_VISIBILITY_HIDE);
	grid_menu->add_separator();
	grid_menu->add_shortcut(ED_SHORTCUT("canvas_item_editor/toggle_grid", TTRC("Toggle Grid"), KeyModifierMask::CMD_OR_CTRL | Key::APOSTROPHE));
	p->add_submenu_node_item(TTRC("Grid"), grid_menu);

	p->add_check_shortcut(ED_SHORTCUT("canvas_item_editor/show_helpers", TTRC("Show Helpers"), Key::H), SHOW_HELPERS);
	p->add_check_shortcut(ED_SHORTCUT("canvas_item_editor/show_rulers", TTRC("Show Rulers")), SHOW_RULERS);
	p->add_check_shortcut(ED_SHORTCUT("canvas_item_editor/show_guides", TTRC("Show Guides"), Key::Y), SHOW_GUIDES);
	p->add_check_shortcut(ED_SHORTCUT("canvas_item_editor/show_origin", TTRC("Show Origin")), SHOW_ORIGIN);
	p->add_check_shortcut(ED_SHORTCUT("canvas_item_editor/show_viewport", TTRC("Show Viewport")), SHOW_VIEWPORT);
	p->add_separator();

	gizmos_menu = memnew(PopupMenu);
	gizmos_menu->set_name("GizmosMenu");
	gizmos_menu->connect(SceneStringName(id_pressed), callable_mp(this, &CanvasItemEditor::_popup_callback));
	gizmos_menu->set_hide_on_checkable_item_selection(false);
	gizmos_menu->add_check_shortcut(ED_SHORTCUT("canvas_item_editor/show_position_gizmos", TTRC("Position")), SHOW_POSITION_GIZMOS);
	gizmos_menu->add_check_shortcut(ED_SHORTCUT("canvas_item_editor/show_lock_gizmos", TTRC("Lock")), SHOW_LOCK_GIZMOS);
	gizmos_menu->add_check_shortcut(ED_SHORTCUT("canvas_item_editor/show_group_gizmos", TTRC("Group")), SHOW_GROUP_GIZMOS);
	gizmos_menu->add_check_shortcut(ED_SHORTCUT("canvas_item_editor/show_transformation_gizmos", TTRC("Transformation")), SHOW_TRANSFORMATION_GIZMOS);
	p->add_child(gizmos_menu);
	p->add_submenu_item(TTRC("Gizmos"), "GizmosMenu");

	p->add_separator();
	p->add_shortcut(ED_SHORTCUT("canvas_item_editor/center_selection", TTRC("Center Selection"), Key::F), VIEW_CENTER_TO_SELECTION);
	p->add_shortcut(ED_SHORTCUT("canvas_item_editor/frame_selection", TTRC("Frame Selection"), KeyModifierMask::SHIFT | Key::F), VIEW_FRAME_TO_SELECTION);
	p->add_shortcut(ED_SHORTCUT("canvas_item_editor/clear_guides", TTRC("Clear Guides")), CLEAR_GUIDES);

	p->add_separator();
	auto_resampling_enabled = EditorSettings::get_singleton()->get_project_metadata("2d_editor", "auto_resampling_enabled", true);
	p->add_check_shortcut(ED_SHORTCUT("canvas_item_editor/auto_resample_canvas_items", TTRC("Auto Resample CanvasItems")), AUTO_RESAMPLE_CANVAS_ITEMS);
	p->set_item_checked(p->get_item_index(AUTO_RESAMPLE_CANVAS_ITEMS), auto_resampling_enabled);
	p->add_check_shortcut(ED_SHORTCUT("canvas_item_editor/preview_canvas_scale", TTRC("Preview Canvas Scale")), PREVIEW_CANVAS_SCALE);

	theme_menu = memnew(PopupMenu);
	theme_menu->connect(SceneStringName(id_pressed), callable_mp(this, &CanvasItemEditor::_switch_theme_preview));
	theme_menu->add_radio_check_item(TTRC("Project theme"), THEME_PREVIEW_PROJECT);
	theme_menu->add_radio_check_item(TTRC("Editor theme"), THEME_PREVIEW_EDITOR);
	theme_menu->add_radio_check_item(TTRC("Default theme"), THEME_PREVIEW_DEFAULT);
	p->add_submenu_node_item(TTRC("Preview Theme"), theme_menu);

	theme_preview = (ThemePreviewMode)(int)EditorSettings::get_singleton()->get_project_metadata("2d_editor", "theme_preview", THEME_PREVIEW_PROJECT);
	for (int i = 0; i < THEME_PREVIEW_MAX; i++) {
		theme_menu->set_item_checked(i, i == theme_preview);
	}

	p->add_submenu_node_item(TTRC("Preview Translation"), memnew(EditorTranslationPreviewMenu));

	main_menu_hbox->add_child(memnew(VSeparator));

	// Contextual toolbars.
	context_toolbar_panel = memnew(PanelContainer);
	context_toolbar_hbox = memnew(HBoxContainer);
	context_toolbar_panel->add_child(context_toolbar_hbox);
	main_flow->add_child(context_toolbar_panel);

	// Animation controls.
	animation_hb = memnew(HBoxContainer);
	add_control_to_menu_panel(animation_hb);
	animation_hb->hide();

	key_loc_button = memnew(Button);
	key_loc_button->set_theme_type_variation(SceneStringName(FlatButton));
	key_loc_button->set_toggle_mode(true);
	key_loc_button->set_pressed(true);
	key_loc_button->set_focus_mode(FOCUS_ACCESSIBILITY);
	key_loc_button->connect(SceneStringName(pressed), callable_mp(this, &CanvasItemEditor::_popup_callback).bind(ANIM_INSERT_POS));
	key_loc_button->set_tooltip_text(TTRC("Translation mask for inserting keys."));
	animation_hb->add_child(key_loc_button);

	key_rot_button = memnew(Button);
	key_rot_button->set_theme_type_variation(SceneStringName(FlatButton));
	key_rot_button->set_toggle_mode(true);
	key_rot_button->set_pressed(true);
	key_rot_button->set_focus_mode(FOCUS_ACCESSIBILITY);
	key_rot_button->connect(SceneStringName(pressed), callable_mp(this, &CanvasItemEditor::_popup_callback).bind(ANIM_INSERT_ROT));
	key_rot_button->set_tooltip_text(TTRC("Rotation mask for inserting keys."));
	animation_hb->add_child(key_rot_button);

	key_scale_button = memnew(Button);
	key_scale_button->set_theme_type_variation(SceneStringName(FlatButton));
	key_scale_button->set_toggle_mode(true);
	key_scale_button->set_focus_mode(FOCUS_ACCESSIBILITY);
	key_scale_button->connect(SceneStringName(pressed), callable_mp(this, &CanvasItemEditor::_popup_callback).bind(ANIM_INSERT_SCALE));
	key_scale_button->set_tooltip_text(TTRC("Scale mask for inserting keys."));
	animation_hb->add_child(key_scale_button);

	key_insert_button = memnew(Button);
	key_insert_button->set_theme_type_variation(SceneStringName(FlatButton));
	key_insert_button->set_focus_mode(FOCUS_ACCESSIBILITY);
	key_insert_button->connect(SceneStringName(pressed), callable_mp(this, &CanvasItemEditor::_popup_callback).bind(ANIM_INSERT_KEY));
	key_insert_button->set_tooltip_text(TTRC("Insert keys (based on mask)."));
	key_insert_button->set_shortcut(ED_SHORTCUT("canvas_item_editor/anim_insert_key", TTRC("Insert Key"), Key::INSERT));
	key_insert_button->set_shortcut_context(this);
	animation_hb->add_child(key_insert_button);

	key_auto_insert_button = memnew(Button);
	key_auto_insert_button->set_theme_type_variation(SceneStringName(FlatButton));
	key_auto_insert_button->set_toggle_mode(true);
	key_auto_insert_button->set_focus_mode(FOCUS_ACCESSIBILITY);
	key_auto_insert_button->set_tooltip_text(TTRC("Auto insert keys when objects are translated, rotated or scaled (based on mask).\nKeys are only added to existing tracks, no new tracks will be created.\nKeys must be inserted manually for the first time."));
	key_auto_insert_button->set_shortcut(ED_SHORTCUT("canvas_item_editor/anim_auto_insert_key", TTRC("Auto Insert Key")));
	key_auto_insert_button->set_accessibility_name(TTRC("Auto Insert Key"));
	key_auto_insert_button->set_shortcut_context(this);
	animation_hb->add_child(key_auto_insert_button);

	animation_menu = memnew(MenuButton);
	animation_menu->set_flat(false);
	animation_menu->set_theme_type_variation("FlatMenuButton");
	animation_menu->set_shortcut_context(this);
	animation_menu->set_tooltip_text(TTRC("Animation Key and Pose Options"));
	animation_hb->add_child(animation_menu);
	animation_menu->get_popup()->connect(SceneStringName(id_pressed), callable_mp(this, &CanvasItemEditor::_popup_callback));
	animation_menu->set_switch_on_hover(true);

	p = animation_menu->get_popup();

	p->add_shortcut(ED_GET_SHORTCUT("canvas_item_editor/anim_insert_key"), ANIM_INSERT_KEY);
	p->add_shortcut(ED_SHORTCUT("canvas_item_editor/anim_insert_key_existing_tracks", TTRC("Insert Key (Existing Tracks)"), KeyModifierMask::CMD_OR_CTRL + Key::INSERT), ANIM_INSERT_KEY_EXISTING);
	p->add_separator();
	p->add_shortcut(ED_SHORTCUT("canvas_item_editor/anim_copy_pose", TTRC("Copy Pose")), ANIM_COPY_POSE);
	p->add_shortcut(ED_SHORTCUT("canvas_item_editor/anim_paste_pose", TTRC("Paste Pose")), ANIM_PASTE_POSE);
	p->add_shortcut(ED_SHORTCUT("canvas_item_editor/anim_clear_pose", TTRC("Clear Pose"), KeyModifierMask::SHIFT | Key::K), ANIM_CLEAR_POSE);

	snap_dialog = memnew(SnapDialog);
	snap_dialog->connect(SceneStringName(confirmed), callable_mp(this, &CanvasItemEditor::_snap_changed));
	add_child(snap_dialog);

	select_sb.instantiate();

	selection_menu = memnew(PopupMenu);
	add_child(selection_menu);
	selection_menu->set_min_size(Vector2(100, 0));
	selection_menu->set_auto_translate_mode(AUTO_TRANSLATE_MODE_DISABLED);
	selection_menu->connect(SceneStringName(id_pressed), callable_mp(this, &CanvasItemEditor::_selection_result_pressed));
	selection_menu->connect("popup_hide", callable_mp(this, &CanvasItemEditor::_selection_menu_hide), CONNECT_DEFERRED);

	add_node_menu = memnew(PopupMenu);
	add_child(add_node_menu);
	add_node_menu->connect(SceneStringName(id_pressed), callable_mp(this, &CanvasItemEditor::_add_node_pressed));

	resample_timer = memnew(Timer);
	resample_timer->set_wait_time(resample_delay);
	resample_timer->set_one_shot(true);
	add_child(resample_timer);
	resample_timer->connect("timeout", callable_mp(this, &CanvasItemEditor::_update_oversampling));

	multiply_grid_step_shortcut = ED_SHORTCUT("canvas_item_editor/multiply_grid_step", TTRC("Multiply grid step by 2"), Key::KP_MULTIPLY);
	divide_grid_step_shortcut = ED_SHORTCUT("canvas_item_editor/divide_grid_step", TTRC("Divide grid step by 2"), Key::KP_DIVIDE);
	reset_transform_position_shortcut = ED_SHORTCUT("canvas_item_editor/reset_transform_position", TTRC("Reset Position"), KeyModifierMask::ALT + Key::W);
	reset_transform_rotation_shortcut = ED_SHORTCUT("canvas_item_editor/reset_transform_rotation", TTRC("Reset Rotation"), KeyModifierMask::ALT + Key::E);
	reset_transform_scale_shortcut = ED_SHORTCUT("canvas_item_editor/reset_transform_scale", TTRC("Reset Scale"), KeyModifierMask::ALT + Key::R);

	skeleton_menu->get_popup()->set_item_checked(skeleton_menu->get_popup()->get_item_index(SKELETON_SHOW_BONES), true);

	// Store the singleton instance.
	singleton = this;

	set_process_shortcut_input(true);
	clear(); // Make sure values are initialized.

	// Update the menus' checkboxes.
	callable_mp(this, &CanvasItemEditor::set_state).call_deferred(get_state());
}

CanvasItemEditor *CanvasItemEditor::singleton = nullptr;

void CanvasItemEditorPlugin::edit(Object *p_object) {
	canvas_item_editor->edit(Object::cast_to<CanvasItem>(p_object));
}

bool CanvasItemEditorPlugin::handles(Object *p_object) const {
	return p_object->is_class("CanvasItem");
}

void CanvasItemEditorPlugin::make_visible(bool p_visible) {
	if (p_visible) {
		canvas_item_editor->show();
		canvas_item_editor->set_process(true);
	} else {
		canvas_item_editor->hide();
		canvas_item_editor->set_process(false);
	}
	// Stored by EditorNode and re-applied whenever another scene context's
	// viewport becomes the displayed one.
	EditorNode::get_singleton()->set_scene_viewport_2d_disabled(!p_visible);
}

Dictionary CanvasItemEditorPlugin::get_state() const {
	return canvas_item_editor->get_state();
}

void CanvasItemEditorPlugin::set_state(const Dictionary &p_state) {
	canvas_item_editor->set_state(p_state);
}

void CanvasItemEditorPlugin::clear() {
	canvas_item_editor->clear();
}

void CanvasItemEditorPlugin::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_ENTER_TREE: {
			connect("scene_changed", callable_mp((CanvasItem *)canvas_item_editor->get_viewport_control(), &CanvasItem::queue_redraw).unbind(1));
			connect("scene_closed", callable_mp((CanvasItem *)canvas_item_editor->get_viewport_control(), &CanvasItem::queue_redraw).unbind(1));
		} break;
	}
}

CanvasItemEditorPlugin::CanvasItemEditorPlugin() {
	canvas_item_editor = memnew(CanvasItemEditor);
	canvas_item_editor->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	EditorNode::get_singleton()->get_editor_main_screen()->get_control()->add_child(canvas_item_editor);
	canvas_item_editor->set_anchors_and_offsets_preset(Control::PRESET_FULL_RECT);
	canvas_item_editor->hide();
}

void CanvasItemEditorViewport::_on_mouse_exit() {
	if (!texture_node_type_selector->is_visible()) {
		_remove_preview();
	}
}

void CanvasItemEditorViewport::_on_select_texture_node_type(Object *selected) {
	CheckBox *check = Object::cast_to<CheckBox>(selected);
	String type = check->get_text();
	texture_node_type_selector->set_title(vformat(TTR("Add %s"), type));
	label->set_text(vformat(TTR("Adding %s..."), type));
}

void CanvasItemEditorViewport::_on_change_type_confirmed() {
	if (!button_group->get_pressed_button()) {
		return;
	}

	CheckBox *check = Object::cast_to<CheckBox>(button_group->get_pressed_button());
	default_texture_node_type = check->get_text();
	_perform_drop_data();
	texture_node_type_selector->hide();
}

void CanvasItemEditorViewport::_on_change_type_closed() {
	_remove_preview();
}

void CanvasItemEditorViewport::_create_preview(const Vector<String> &files) const {
	bool add_preview = false;
	for (int i = 0; i < files.size(); i++) {
		Ref<Resource> res = ResourceLoader::load(files[i]);
		ERR_CONTINUE(res.is_null());

		Ref<Texture2D> texture = res;
		if (texture.is_valid()) {
			Sprite2D *sprite = memnew(Sprite2D);
			sprite->set_texture(texture);
			sprite->set_modulate(Color(1, 1, 1, 0.7f));
			preview_node->add_child(sprite);
			add_preview = true;
		}

		Ref<PackedScene> scene = res;
		if (scene.is_valid()) {
			Node *instance = scene->instantiate();
			if (instance) {
				preview_node->add_child(instance);
			}
			add_preview = true;
		}

		Ref<AudioStream> audio = res;
		if (audio.is_valid()) {
			Sprite2D *sprite = memnew(Sprite2D);
			sprite->set_texture(get_editor_theme_icon(SNAME("AudioStreamPlayer2D")));
			sprite->set_modulate(Color(1, 1, 1, 0.7f));
			sprite->set_position(Vector2(0, -sprite->get_texture()->get_size().height) * EDSCALE);
			preview_node->add_child(sprite);
			add_preview = true;
		}
	}

	if (add_preview) {
		EditorNode::get_singleton()->get_scene_root()->add_child(preview_node);
	}
}

void CanvasItemEditorViewport::_remove_preview() {
	if (!canvas_item_editor->view_state.message.is_empty()) {
		canvas_item_editor->view_state.message = "";
		canvas_item_editor->update_viewport();
	}
	if (preview_node->get_parent()) {
		for (int i = preview_node->get_child_count() - 1; i >= 0; i--) {
			Node *node = preview_node->get_child(i);
			node->queue_free();
			preview_node->remove_child(node);
		}
		// The displayed viewport may have changed since the preview was added,
		// so remove the preview from its actual parent.
		preview_node->get_parent()->remove_child(preview_node);

		label->hide();
		label_desc->hide();
	}
}

bool CanvasItemEditorViewport::_cyclical_dependency_exists(const String &p_target_scene_path, Node *p_desired_node) const {
	if (p_desired_node->get_scene_file_path() == p_target_scene_path) {
		return true;
	}

	int childCount = p_desired_node->get_child_count();
	for (int i = 0; i < childCount; i++) {
		Node *child = p_desired_node->get_child(i);
		if (_cyclical_dependency_exists(p_target_scene_path, child)) {
			return true;
		}
	}
	return false;
}

void CanvasItemEditorViewport::_create_texture_node(Node *p_parent, Node *p_child, const String &p_path, const Point2 &p_point) {
	// Adjust casing according to project setting. The file name is expected to be in snake_case, but will work for others.
	const String &node_name = Node::adjust_name_casing(p_path.get_file().get_basename());
	if (!node_name.is_empty()) {
		p_child->set_name(node_name);
	}

	EditorUndoRedoManager *undo_redo = EditorUndoRedoManager::get_singleton();
	Ref<Texture2D> texture = ResourceCache::get_ref(p_path);

	if (p_parent) {
		undo_redo->add_do_method(p_parent, "add_child", p_child, true);
		undo_redo->add_do_method(p_child, "set_owner", EditorNode::get_singleton()->get_edited_scene());
		undo_redo->add_do_reference(p_child);
		undo_redo->add_undo_method(p_parent, "remove_child", p_child);
	} else { // If no parent is selected, set as root node of the scene.
		undo_redo->add_do_method(EditorNode::get_singleton(), "set_edited_scene", p_child);
		undo_redo->add_do_method(p_child, "set_owner", EditorNode::get_singleton()->get_edited_scene());
		undo_redo->add_do_reference(p_child);
		undo_redo->add_undo_method(EditorNode::get_singleton(), "set_edited_scene", (Object *)nullptr);
	}

	if (p_parent) {
		String new_name = p_parent->validate_child_name(p_child);
		EditorDebuggerNode *ed = EditorDebuggerNode::get_singleton();
		undo_redo->add_do_method(ed, "live_debug_create_node", EditorNode::get_singleton()->get_edited_scene()->get_path_to(p_parent), p_child->get_class(), new_name);
		undo_redo->add_undo_method(ed, "live_debug_remove_node", NodePath(String(EditorNode::get_singleton()->get_edited_scene()->get_path_to(p_parent)) + "/" + new_name));
	}

	if (Object::cast_to<TouchScreenButton>(p_child) || Object::cast_to<TextureButton>(p_child)) {
		undo_redo->add_do_property(p_child, "texture_normal", texture);
	} else {
		undo_redo->add_do_property(p_child, "texture", texture);
	}

	// make visible for certain node type
	if (Object::cast_to<Control>(p_child)) {
		Size2 texture_size = texture->get_size();
		undo_redo->add_do_property(p_child, "size", texture_size);
	} else if (Object::cast_to<Polygon2D>(p_child)) {
		Size2 texture_size = texture->get_size();
		Vector<Vector2> list = {
			Vector2(0, 0),
			Vector2(texture_size.width, 0),
			Vector2(texture_size.width, texture_size.height),
			Vector2(0, texture_size.height)
		};
		undo_redo->add_do_property(p_child, "polygon", list);
	}

	// Compute the global position
	Transform2D xform = canvas_item_editor->get_canvas_transform();
	Point2 target_position = xform.affine_inverse().xform(p_point);

	// Adjust position for Control and TouchScreenButton
	if (Object::cast_to<Control>(p_child) || Object::cast_to<TouchScreenButton>(p_child)) {
		target_position -= texture->get_size() / 2;
	}

	// There's nothing to be used as source position, so snapping will work as absolute if enabled.
	target_position = canvas_item_editor->snap_point(target_position);

	CanvasItem *parent_ci = Object::cast_to<CanvasItem>(p_parent);
	Point2 local_target_pos = parent_ci ? parent_ci->get_global_transform().affine_inverse().xform(target_position) : target_position;

	undo_redo->add_do_method(p_child, "set_position", local_target_pos);
}

void CanvasItemEditorViewport::_create_audio_node(Node *p_parent, const String &p_path, const Point2 &p_point) {
	AudioStreamPlayer2D *child = memnew(AudioStreamPlayer2D);
	child->set_stream(ResourceCache::get_ref(p_path));

	// Adjust casing according to project setting. The file name is expected to be in snake_case, but will work for others.
	const String &node_name = Node::adjust_name_casing(p_path.get_file().get_basename());
	if (!node_name.is_empty()) {
		child->set_name(node_name);
	}

	EditorUndoRedoManager *undo_redo = EditorUndoRedoManager::get_singleton();

	if (p_parent) {
		undo_redo->add_do_method(p_parent, "add_child", child, true);
		undo_redo->add_do_method(child, "set_owner", EditorNode::get_singleton()->get_edited_scene());
		undo_redo->add_do_reference(child);
		undo_redo->add_undo_method(p_parent, "remove_child", child);
	} else { // If no parent is selected, set as root node of the scene.
		undo_redo->add_do_method(EditorNode::get_singleton(), "set_edited_scene", child);
		undo_redo->add_do_method(child, "set_owner", EditorNode::get_singleton()->get_edited_scene());
		undo_redo->add_do_reference(child);
		undo_redo->add_undo_method(EditorNode::get_singleton(), "set_edited_scene", (Object *)nullptr);
	}

	if (p_parent) {
		String new_name = p_parent->validate_child_name(child);
		EditorDebuggerNode *ed = EditorDebuggerNode::get_singleton();
		undo_redo->add_do_method(ed, "live_debug_create_node", EditorNode::get_singleton()->get_edited_scene()->get_path_to(p_parent), child->get_class(), new_name);
		undo_redo->add_undo_method(ed, "live_debug_remove_node", NodePath(String(EditorNode::get_singleton()->get_edited_scene()->get_path_to(p_parent)) + "/" + new_name));
	}

	// Compute the global position
	Transform2D xform = canvas_item_editor->get_canvas_transform();
	Point2 target_position = xform.affine_inverse().xform(p_point);

	// There's nothing to be used as source position, so snapping will work as absolute if enabled.
	target_position = canvas_item_editor->snap_point(target_position);

	CanvasItem *parent_ci = Object::cast_to<CanvasItem>(p_parent);
	Point2 local_target_pos = parent_ci ? parent_ci->get_global_transform().affine_inverse().xform(target_position) : target_position;

	undo_redo->add_do_method(child, "set_position", local_target_pos);

	EditorSelection *editor_selection = EditorNode::get_singleton()->get_editor_selection();
	undo_redo->add_do_method(editor_selection, "add_node", child);
}

bool CanvasItemEditorViewport::_create_instance(Node *p_parent, const String &p_path, const Point2 &p_point) {
	Ref<PackedScene> sdata = ResourceLoader::load(p_path);
	if (sdata.is_null()) { // invalid scene
		return false;
	}

	Node *instantiated_scene = sdata->instantiate(PackedScene::GEN_EDIT_STATE_INSTANCE);
	if (!instantiated_scene) { // Error on instantiation.
		return false;
	}

	Node *edited_scene = EditorNode::get_singleton()->get_edited_scene();

	if (!edited_scene->get_scene_file_path().is_empty()) { // Cyclic instantiation.
		if (_cyclical_dependency_exists(edited_scene->get_scene_file_path(), instantiated_scene)) {
			memdelete(instantiated_scene);
			return false;
		}
	}

	instantiated_scene->set_scene_file_path(ProjectSettings::get_singleton()->localize_path(p_path));

	EditorUndoRedoManager *undo_redo = EditorUndoRedoManager::get_singleton();
	EditorSelection *editor_selection = EditorNode::get_singleton()->get_editor_selection();
	undo_redo->add_do_method(p_parent, "add_child", instantiated_scene, true);
	undo_redo->add_do_method(instantiated_scene, "set_owner", edited_scene);
	undo_redo->add_do_reference(instantiated_scene);
	undo_redo->add_undo_method(p_parent, "remove_child", instantiated_scene);
	undo_redo->add_do_method(editor_selection, "add_node", instantiated_scene);

	String new_name = p_parent->validate_child_name(instantiated_scene);
	EditorDebuggerNode *ed = EditorDebuggerNode::get_singleton();
	undo_redo->add_do_method(ed, "live_debug_instantiate_node", edited_scene->get_path_to(p_parent), p_path, new_name);
	undo_redo->add_undo_method(ed, "live_debug_remove_node", NodePath(String(edited_scene->get_path_to(p_parent)) + "/" + new_name));

	CanvasItem *instance_ci = Object::cast_to<CanvasItem>(instantiated_scene);
	if (instance_ci) {
		Vector2 target_pos = canvas_item_editor->get_canvas_transform().affine_inverse().xform(p_point);
		target_pos = canvas_item_editor->snap_point(target_pos);

		CanvasItem *parent_ci = Object::cast_to<CanvasItem>(p_parent);
		if (parent_ci) {
			target_pos = parent_ci->get_global_transform_with_canvas().affine_inverse().xform(target_pos);
		}
		// Preserve instance position of the original scene.
		target_pos += instance_ci->_edit_get_position();

		undo_redo->add_do_method(instantiated_scene, "set_position", target_pos);
	}

	return true;
}

void CanvasItemEditorViewport::_perform_drop_data() {
	ERR_FAIL_COND(selected_files.is_empty());

	_remove_preview();

	if (!target_node) {
		// Should already be handled by `can_drop_data`.
		ERR_FAIL_COND_MSG(selected_files.size() > 1, "Can't instantiate multiple nodes without root.");

		const String &path = selected_files[0];
		Ref<Resource> res = ResourceLoader::load(path);
		if (res.is_null()) {
			return;
		}

		Ref<PackedScene> scene = res;
		if (scene.is_valid()) {
			// Without root node act the same as "Load Inherited Scene".
			Error err = EditorNode::get_singleton()->load_scene(path, false, true);
			if (err != OK) {
				accept->set_text(vformat(TTR("Error instantiating scene from %s."), path.get_file()));
				accept->popup_centered();
			}
			return;
		}
	}

	PackedStringArray error_files;

	EditorUndoRedoManager *undo_redo = EditorUndoRedoManager::get_singleton();
	undo_redo->create_action_for_history(TTR("Create Node"), EditorNode::get_editor_data().get_current_edited_scene_history_id());
	EditorSelection *editor_selection = EditorNode::get_singleton()->get_editor_selection();
	undo_redo->add_do_method(editor_selection, "clear");

	for (int i = 0; i < selected_files.size(); i++) {
		String path = selected_files[i];
		Ref<Resource> res = ResourceLoader::load(path);
		if (res.is_null()) {
			continue;
		}

		Ref<PackedScene> scene = res;
		if (scene.is_valid()) {
			bool success = _create_instance(target_node, path, drop_pos);
			if (!success) {
				error_files.push_back(path.get_file());
			}
			continue;
		}

		Ref<Texture2D> texture = res;
		if (texture.is_valid()) {
			Node *child = Object::cast_to<Node>(ClassDB::instantiate(default_texture_node_type));
			_create_texture_node(target_node, child, path, drop_pos);
			undo_redo->add_do_method(editor_selection, "add_node", child);
		}

		Ref<AudioStream> audio = res;
		if (audio.is_valid()) {
			_create_audio_node(target_node, path, drop_pos);
		}
	}

	undo_redo->commit_action();

	if (error_files.size() > 0) {
		accept->set_text(vformat(TTR("Error instantiating scene from %s."), String(", ").join(error_files)));
		accept->popup_centered();
	}
}

bool CanvasItemEditorViewport::can_drop_data(const Point2 &p_point, const Variant &p_data) const {
	if (p_point == Vector2(Math::INF, Math::INF)) {
		return false;
	}
	Dictionary d = p_data;
	if (!d.has("type") || (String(d["type"]) != "files")) {
		label->hide();
		return false;
	}

	Vector<String> files = d["files"];

	const Node *edited_scene = EditorNode::get_singleton()->get_edited_scene();
	if (!edited_scene && files.size() > 1) {
		canvas_item_editor->view_state.message = TTR("Can't instantiate multiple nodes without root.");
		canvas_item_editor->update_viewport();
		return false;
	}

	enum {
		SCENE = 1 << 0,
		TEXTURE = 1 << 1,
		AUDIO = 1 << 2,
	};
	int instantiate_type = 0;

	for (const String &path : files) {
		const String &res_type = ResourceLoader::get_resource_type(path);
		String error_message;

		if (ClassDB::is_parent_class(res_type, "PackedScene")) {
			Ref<PackedScene> scn = ResourceLoader::load(path);
			ERR_CONTINUE(scn.is_null());

			Node *instantiated_scene = scn->instantiate(PackedScene::GEN_EDIT_STATE_INSTANCE);
			if (!instantiated_scene) {
				continue;
			}
			if (edited_scene && !edited_scene->get_scene_file_path().is_empty() && _cyclical_dependency_exists(edited_scene->get_scene_file_path(), instantiated_scene)) {
				error_message = vformat(TTR("Circular dependency found at %s."), path.get_file());
			}
			memdelete(instantiated_scene);
			instantiate_type |= SCENE;
		}
		if (ClassDB::is_parent_class(res_type, "Texture2D")) {
			instantiate_type |= TEXTURE;
		}
		if (ClassDB::is_parent_class(res_type, "AudioStream")) {
			instantiate_type |= AUDIO;
		}

		if (!error_message.is_empty()) {
			// TRANSLATORS: The placeholder is the error message.
			canvas_item_editor->view_state.message = vformat(TTR("Can't instantiate: %s"), error_message);
			canvas_item_editor->update_viewport();
			return false;
		}
	}
	if (instantiate_type == 0) {
		return false;
	}

	if (!preview_node->get_parent()) { // create preview only once
		_create_preview(files);
	}
	ERR_FAIL_COND_V(preview_node->get_child_count() == 0, false);

	const Transform2D trans = canvas_item_editor->get_canvas_transform();
	preview_node->set_position((p_point - trans.get_origin()) / trans.get_scale().x);

	if (!edited_scene && instantiate_type & SCENE) {
		String scene_file_path = preview_node->get_child(0)->get_scene_file_path();
		// TRANSLATORS: The placeholder is the file path of the scene being instantiated.
		canvas_item_editor->view_state.message = vformat(TTR("Creating inherited scene from: %s"), scene_file_path);
	} else {
		double snap = EDITOR_GET("interface/inspector/default_float_step");
		int snap_step_decimals = Math::range_step_decimals(snap);
		const String &lang = _get_locale();
#define FORMAT(value) (TranslationServer::get_singleton()->format_number(String::num(value, snap_step_decimals), lang))
		Vector2 preview_node_pos = preview_node->get_global_position();
		canvas_item_editor->view_state.message = TTR("Instantiating: ") + "(" + FORMAT(preview_node_pos.x) + ", " + FORMAT(preview_node_pos.y) + ") px";
	}
	canvas_item_editor->update_viewport();

	if (instantiate_type & TEXTURE && instantiate_type & AUDIO) {
		// TRANSLATORS: The placeholders are the types of nodes being instantiated.
		label->set_text(vformat(TTR("Adding %s and %s..."), default_texture_node_type, "AudioStreamPlayer2D"));
	} else {
		String node_type;
		if (instantiate_type & TEXTURE) {
			node_type = default_texture_node_type;
		} else if (instantiate_type & AUDIO) {
			node_type = "AudioStreamPlayer2D";
		}
		if (!node_type.is_empty()) {
			// TRANSLATORS: The placeholder is the type of node being instantiated.
			label->set_text(vformat(TTR("Adding %s..."), node_type));
		}
	}
	label->set_visible(instantiate_type & ~SCENE);

	String desc = TTR("Drag and drop to add as sibling of selected node (except when root is selected).") +
			"\n" + TTR("Hold Shift when dropping to add as child of selected node.") +
			"\n" + TTR("Hold Alt when dropping to add as child of root node.");
	if (instantiate_type & TEXTURE) {
		desc += "\n" + TTR("Hold Alt + Shift when dropping to add as different node type.");
	}
	label_desc->set_text(desc);
	label_desc->show();

	return true;
}

void CanvasItemEditorViewport::_show_texture_node_type_selector() {
	_remove_preview();
	List<BaseButton *> btn_list;
	button_group->get_buttons(&btn_list);

	for (BaseButton *btn : btn_list) {
		CheckBox *check = Object::cast_to<CheckBox>(btn);
		check->set_pressed(check->get_text() == default_texture_node_type);
	}
	texture_node_type_selector->set_title(vformat(TTR("Add %s"), default_texture_node_type));
	texture_node_type_selector->popup_centered();
}

bool CanvasItemEditorViewport::_is_any_texture_selected() const {
	for (int i = 0; i < selected_files.size(); ++i) {
		if (ClassDB::is_parent_class(ResourceLoader::get_resource_type(selected_files[i]), "Texture2D")) {
			return true;
		}
	}
	return false;
}

void CanvasItemEditorViewport::drop_data(const Point2 &p_point, const Variant &p_data) {
	if (p_point == Vector2(Math::INF, Math::INF)) {
		return;
	}
	bool is_shift = Input::get_singleton()->is_key_pressed(Key::SHIFT);
	bool is_alt = Input::get_singleton()->is_key_pressed(Key::ALT);

	selected_files.clear();
	Dictionary d = p_data;
	if (d.has("type") && String(d["type"]) == "files") {
		selected_files = d["files"];
	}
	if (selected_files.is_empty()) {
		return;
	}

	const List<Node *> &selected_nodes = EditorNode::get_singleton()->get_editor_selection()->get_top_selected_node_list();
	Node *root_node = EditorNode::get_singleton()->get_edited_scene();
	if (selected_nodes.size() > 0) {
		Node *selected_node = selected_nodes.front()->get();
		if (is_alt) {
			target_node = root_node;
		} else if (is_shift) {
			target_node = selected_node;
		} else { // Default behavior.
			target_node = (selected_node != root_node) ? selected_node->get_parent() : root_node;
		}
	} else {
		if (root_node) {
			target_node = root_node;
		} else {
			target_node = nullptr;
		}
	}

	drop_pos = p_point;

	if (is_alt && is_shift && _is_any_texture_selected()) {
		_show_texture_node_type_selector();
	} else {
		_perform_drop_data();
	}
}

void CanvasItemEditorViewport::_update_theme() {
	List<BaseButton *> btn_list;
	button_group->get_buttons(&btn_list);

	for (BaseButton *btn : btn_list) {
		CheckBox *check = Object::cast_to<CheckBox>(btn);
		check->set_button_icon(get_editor_theme_icon(check->get_text()));
	}

	label->add_theme_color_override(SceneStringName(font_color), get_theme_color(SNAME("warning_color"), EditorStringName(Editor)));
}

void CanvasItemEditorViewport::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_THEME_CHANGED: {
			_update_theme();
		} break;

		case NOTIFICATION_ENTER_TREE: {
			_update_theme();
			connect(SceneStringName(mouse_exited), callable_mp(this, &CanvasItemEditorViewport::_on_mouse_exit));
		} break;

		case NOTIFICATION_EXIT_TREE: {
			disconnect(SceneStringName(mouse_exited), callable_mp(this, &CanvasItemEditorViewport::_on_mouse_exit));
		} break;

		case NOTIFICATION_DRAG_END: {
			_remove_preview();
		} break;
	}
}

CanvasItemEditorViewport::CanvasItemEditorViewport(CanvasItemEditor *p_canvas_item_editor, CanvasItemEditorView *p_canvas_item_editor_view) {
	default_texture_node_type = "Sprite2D";
	// Node2D
	texture_node_types.push_back("Sprite2D");
	texture_node_types.push_back("PointLight2D");
	texture_node_types.push_back("CPUParticles2D");
	texture_node_types.push_back("GPUParticles2D");
	texture_node_types.push_back("Polygon2D");
	texture_node_types.push_back("TouchScreenButton");
	// Control
	texture_node_types.push_back("TextureRect");
	texture_node_types.push_back("TextureButton");
	texture_node_types.push_back("NinePatchRect");

	target_node = nullptr;
	canvas_item_editor = p_canvas_item_editor;
	canvas_item_editor_view = p_canvas_item_editor_view;
	preview_node = memnew(Control);

	accept = memnew(AcceptDialog);
	EditorNode::get_singleton()->get_gui_base()->add_child(accept);

	texture_node_type_selector = memnew(AcceptDialog);
	EditorNode::get_singleton()->get_gui_base()->add_child(texture_node_type_selector);
	texture_node_type_selector->connect(SceneStringName(confirmed), callable_mp(this, &CanvasItemEditorViewport::_on_change_type_confirmed));
	texture_node_type_selector->connect("canceled", callable_mp(this, &CanvasItemEditorViewport::_on_change_type_closed));

	VBoxContainer *vbc = memnew(VBoxContainer);
	texture_node_type_selector->add_child(vbc);
	vbc->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	vbc->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	vbc->set_custom_minimum_size(Size2(240, 260) * EDSCALE);

	VBoxContainer *btn_group = memnew(VBoxContainer);
	vbc->add_child(btn_group);
	btn_group->set_h_size_flags(SIZE_EXPAND_FILL);

	button_group.instantiate();
	for (int i = 0; i < texture_node_types.size(); i++) {
		CheckBox *check = memnew(CheckBox);
		check->set_text(texture_node_types[i]);
		check->set_auto_translate_mode(AUTO_TRANSLATE_MODE_DISABLED);
		check->set_button_group(button_group);
		btn_group->add_child(check);
		check->connect("button_down", callable_mp(this, &CanvasItemEditorViewport::_on_select_texture_node_type).bind(check));
	}

	label = memnew(Label);
	label->add_theme_color_override("font_shadow_color", Color(0, 0, 0, 1));
	label->add_theme_constant_override("shadow_outline_size", 1 * EDSCALE);
	label->hide();
	canvas_item_editor_view->get_controls_container()->add_child(label);

	label_desc = memnew(Label);
	label_desc->set_focus_mode(FOCUS_ACCESSIBILITY);
	label_desc->add_theme_color_override(SceneStringName(font_color), Color(0.6f, 0.6f, 0.6f, 1));
	label_desc->add_theme_color_override("font_shadow_color", Color(0.2f, 0.2f, 0.2f, 1));
	label_desc->add_theme_constant_override("shadow_outline_size", 1 * EDSCALE);
	label_desc->add_theme_constant_override("line_spacing", 0);
	label_desc->hide();
	canvas_item_editor_view->get_controls_container()->add_child(label_desc);

	RS::get_singleton()->canvas_set_disable_scale(true);
}

CanvasItemEditorViewport::~CanvasItemEditorViewport() {
	memdelete(preview_node);
}
