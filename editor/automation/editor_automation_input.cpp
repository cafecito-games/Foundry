/**************************************************************************/
/*  editor_automation_input.cpp                                           */
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

#include "editor_automation_input.h"

#include "core/math/math_funcs.h"
#include "core/input/input.h"
#include "core/input/input_map.h"
#include "core/object/message_queue.h"
#include "core/os/keyboard.h"
#include "scene/gui/control.h"
#include "scene/gui/subviewport_container.h"
#include "scene/main/viewport.h"
#include "scene/main/window.h"

namespace {

Vector2 editor_automation_last_mouse_position;

void _dispatch_input_event(const Ref<InputEvent> &p_event) {
	Input::get_singleton()->parse_input_event(p_event);
	MessageQueue::get_singleton()->flush();
}

} // namespace

EditorAutomationInputModifiers EditorAutomationInput::parse_modifiers(const Variant &p_modifiers) {
	EditorAutomationInputModifiers modifiers;
	if (p_modifiers.get_type() != Variant::ARRAY) {
		return modifiers;
	}
	const Array mods = p_modifiers;
	for (int i = 0; i < mods.size(); i++) {
		const String mod = String(mods[i]).to_lower();
		if (mod == "shift") {
			modifiers.shift = true;
		} else if (mod == "ctrl" || mod == "control") {
			modifiers.ctrl = true;
		} else if (mod == "alt" || mod == "option") {
			modifiers.alt = true;
		} else if (mod == "meta" || mod == "cmd" || mod == "command") {
			modifiers.meta = true;
		}
	}
	return modifiers;
}

void EditorAutomationInput::apply_modifiers(Ref<InputEventWithModifiers> p_event, const EditorAutomationInputModifiers &p_modifiers) {
	ERR_FAIL_COND(p_event.is_null());
	p_event->set_shift_pressed(p_modifiers.shift);
	p_event->set_ctrl_pressed(p_modifiers.ctrl);
	p_event->set_alt_pressed(p_modifiers.alt);
	p_event->set_meta_pressed(p_modifiers.meta);
}

MouseButton EditorAutomationInput::parse_mouse_button(const String &p_button_name, MouseButton p_default) {
	const String button = p_button_name.to_lower();
	if (button.is_empty() || button == "left") {
		return MouseButton::LEFT;
	}
	if (button == "right") {
		return MouseButton::RIGHT;
	}
	if (button == "middle") {
		return MouseButton::MIDDLE;
	}
	return p_default;
}

MouseButtonMask EditorAutomationInput::mouse_button_to_mask(MouseButton p_button) {
	switch (p_button) {
		case MouseButton::LEFT:
			return MouseButtonMask::LEFT;
		case MouseButton::RIGHT:
			return MouseButtonMask::RIGHT;
		case MouseButton::MIDDLE:
			return MouseButtonMask::MIDDLE;
		default:
			return MouseButtonMask::LEFT;
	}
}

bool EditorAutomationInput::push_key_event(
		Viewport *p_viewport,
		Key p_key,
		bool p_pressed,
		char32_t p_unicode,
		const EditorAutomationInputModifiers &p_modifiers,
		PackedStringArray &r_events) {
	ERR_FAIL_NULL_V(p_viewport, false);

	Ref<InputEventKey> event;
	event.instantiate();
	event->set_keycode(p_key);
	event->set_physical_keycode(p_key);
	event->set_pressed(p_pressed);
	if (p_unicode != 0) {
		event->set_unicode(p_unicode);
	}
	apply_modifiers(event, p_modifiers);
	_dispatch_input_event(event);

	if (p_pressed) {
		r_events.push_back("key_pressed");
	} else {
		r_events.push_back("key_released");
	}
	return true;
}

bool EditorAutomationInput::push_input_action(
		Viewport *p_viewport,
		const StringName &p_action,
		bool p_pressed,
		PackedStringArray &r_events) {
	ERR_FAIL_NULL_V(p_viewport, false);

	const List<Ref<InputEvent>> *mapped_events = InputMap::get_singleton()->action_get_events(p_action);
	if (mapped_events == nullptr || mapped_events->is_empty()) {
		Ref<InputEventAction> action_event;
		action_event.instantiate();
		action_event->set_action(p_action);
		action_event->set_pressed(p_pressed);
		p_viewport->push_input(action_event);
		MessageQueue::get_singleton()->flush();
	} else {
		const List<Ref<InputEvent>>::Element *first_event = mapped_events->front();
		if (first_event != nullptr) {
			Ref<InputEvent> duplicated = first_event->get()->duplicate();
			Ref<InputEventKey> key_event = duplicated;
			if (key_event.is_valid()) {
				key_event->set_pressed(p_pressed);
				if (p_pressed) {
					_dispatch_input_event(key_event);
				}
			}
		}
	}

	if (p_pressed) {
		r_events.push_back(vformat("action_pressed:%s", String(p_action)));
	} else {
		r_events.push_back(vformat("action_released:%s", String(p_action)));
	}
	return true;
}

bool EditorAutomationInput::push_mouse_button(
		Viewport *p_viewport,
		const Vector2 &p_position,
		MouseButton p_button,
		bool p_pressed,
		MouseButtonMask p_button_mask,
		const EditorAutomationInputModifiers &p_modifiers,
		PackedStringArray &r_events,
		bool p_local_coords) {
	ERR_FAIL_NULL_V(p_viewport, false);

	Ref<InputEventMouseButton> event;
	event.instantiate();
	event->set_button_index(p_button);
	event->set_pressed(p_pressed);
	event->set_position(p_position);
	if (!p_local_coords) {
		event->set_global_position(p_position);
	}
	event->set_button_mask(p_pressed ? p_button_mask : MouseButtonMask::NONE);
	apply_modifiers(event, p_modifiers);
	if (p_local_coords) {
		p_viewport->push_input(event, true);
		MessageQueue::get_singleton()->flush();
	} else {
		editor_automation_last_mouse_position = p_position;
		_dispatch_input_event(event);
	}

	if (p_pressed) {
		r_events.push_back("mouse_pressed");
	} else {
		r_events.push_back("mouse_released");
	}
	return true;
}

bool EditorAutomationInput::push_mouse_motion(
		Viewport *p_viewport,
		const Vector2 &p_position,
		const Vector2 &p_relative,
		MouseButtonMask p_button_mask,
		const EditorAutomationInputModifiers &p_modifiers,
		PackedStringArray &r_events,
		bool p_local_coords) {
	ERR_FAIL_NULL_V(p_viewport, false);

	Ref<InputEventMouseMotion> event;
	event.instantiate();
	if (!p_local_coords) {
		const Vector2 relative = p_relative.is_zero_approx() ? p_position - editor_automation_last_mouse_position : p_relative;
		event->set_relative(relative);
		editor_automation_last_mouse_position = p_position;
	} else {
		event->set_relative(p_relative);
	}
	event->set_position(p_position);
	if (!p_local_coords) {
		event->set_global_position(p_position);
	}
	event->set_button_mask(p_button_mask);
	apply_modifiers(event, p_modifiers);
	if (p_local_coords) {
		ERR_FAIL_NULL_V(p_viewport, false);
		p_viewport->push_input(event, true);
		MessageQueue::get_singleton()->flush();
	} else {
		_dispatch_input_event(event);
	}
	r_events.push_back("mouse_motion");
	return true;
}

bool EditorAutomationInput::push_mouse_drag(
		Viewport *p_viewport,
		const Vector2 &p_from_global,
		const Vector2 &p_to_global,
		const Vector<Vector2> &p_waypoints,
		MouseButton p_button,
		const EditorAutomationInputModifiers &p_modifiers,
		PackedStringArray &r_events) {
	ERR_FAIL_NULL_V(p_viewport, false);

	const MouseButtonMask button_mask = mouse_button_to_mask(p_button);
	if (!push_mouse_button(p_viewport, p_from_global, p_button, true, button_mask, p_modifiers, r_events)) {
		return false;
	}

	Vector<Vector2> path;
	path.push_back(p_from_global);
	for (const Vector2 &waypoint : p_waypoints) {
		path.push_back(waypoint);
	}
	path.push_back(p_to_global);

	const float step_distance = MAX(1.0f, float(p_viewport->get_drag_threshold()) * 0.5f);
	Vector2 current = p_from_global;
	for (int segment_index = 1; segment_index < path.size(); segment_index++) {
		const Vector2 target = path[segment_index];
		const Vector2 delta = target - current;
		const float distance = delta.length();
		const int steps = MAX(1, int(Math::ceil(distance / step_distance)));
		for (int step = 1; step <= steps; step++) {
			const Vector2 next = current.lerp(target, float(step) / float(steps));
			const Vector2 relative = next - current;
			current = next;
			if (!push_mouse_motion(p_viewport, current, relative, button_mask, p_modifiers, r_events)) {
				return false;
			}
		}
	}

	return push_mouse_button(p_viewport, p_to_global, p_button, false, MouseButtonMask::NONE, p_modifiers, r_events);
}

Vector2 EditorAutomationInput::global_center_of_bounds(const Rect2i &p_bounds) {
	return Rect2(p_bounds.position, p_bounds.size).get_center();
}

Vector2 EditorAutomationInput::resolve_position_in_bounds(const Rect2i &p_bounds, const Dictionary &p_options) {
	const Rect2 rect(p_bounds.position, p_bounds.size);
	if (p_options.has("anchor")) {
		const String anchor = String(p_options.get("anchor", Variant())).to_lower();
		if (anchor == "top_left") {
			return rect.position + Vector2(4, 4);
		}
		if (anchor == "top_right") {
			return rect.position + Vector2(rect.size.x - 4, 4);
		}
		if (anchor == "bottom_left") {
			return rect.position + Vector2(4, rect.size.y - 4);
		}
		if (anchor == "bottom_right") {
			return rect.position + Vector2(rect.size.x - 4, rect.size.y - 4);
		}
		return rect.get_center();
	}

	if (p_options.has("x") && p_options.has("y")) {
		const Variant x_value = p_options.get("x", Variant());
		const Variant y_value = p_options.get("y", Variant());
		Vector2 position = rect.position;
		if (x_value.get_type() == Variant::FLOAT || x_value.get_type() == Variant::INT) {
			const double x = x_value;
			if (x >= 0.0 && x <= 1.0 && rect.size.x > 0) {
				position.x += float(x) * rect.size.x;
			} else {
				position.x += float(x);
			}
		}
		if (y_value.get_type() == Variant::FLOAT || y_value.get_type() == Variant::INT) {
			const double y = y_value;
			if (y >= 0.0 && y <= 1.0 && rect.size.y > 0) {
				position.y += float(y) * rect.size.y;
			} else {
				position.y += float(y);
			}
		}
		return position;
	}

	if (p_options.has("target_point")) {
		const Variant point_value = p_options.get("target_point", Variant());
		if (point_value.get_type() == Variant::VECTOR2) {
			return point_value;
		}
		if (point_value.get_type() == Variant::ARRAY) {
			const Array point = point_value;
			if (point.size() >= 2) {
				return Vector2(point[0], point[1]);
			}
		}
	}

	return rect.get_center();
}

Viewport *EditorAutomationInput::viewport_for_node(Node *p_node) {
	if (p_node == nullptr || !p_node->is_inside_tree()) {
		return nullptr;
	}
	return p_node->get_viewport();
}

Viewport *EditorAutomationInput::input_viewport_for_control(Control *p_control, Vector2 &r_local_position, const Vector2 &p_global_position) {
	ERR_FAIL_NULL_V(p_control, nullptr);
	if (SubViewportContainer *container = Object::cast_to<SubViewportContainer>(p_control)) {
		for (int i = 0; i < container->get_child_count(); i++) {
			if (SubViewport *sub_viewport = Object::cast_to<SubViewport>(container->get_child(i))) {
				r_local_position = container->get_global_transform_with_canvas().affine_inverse().xform(p_global_position);
				if (container->is_stretch_enabled() && container->get_stretch_shrink() > 1) {
					r_local_position /= container->get_stretch_shrink();
				}
				return sub_viewport;
			}
		}
	}
	r_local_position = p_global_position;
	return viewport_for_node(p_control);
}

Window *EditorAutomationInput::window_for_node(Node *p_node) {
	while (p_node != nullptr) {
		if (Window *window = Object::cast_to<Window>(p_node)) {
			return window;
		}
		p_node = p_node->get_parent();
	}
	return nullptr;
}

EditorAutomationWindowFocusResult EditorAutomationInput::ensure_window_focus(Node *p_node) {
	EditorAutomationWindowFocusResult result;
	result.window = window_for_node(p_node);
	if (result.window == nullptr) {
		result.ok = true;
		return result;
	}

	if (result.window->has_focus()) {
		result.ok = true;
		return result;
	}

	if (!result.window->is_visible()) {
		result.message = vformat("Window '%s' is not visible.", result.window->get_title());
		return result;
	}

	result.window->grab_focus();
	MessageQueue::get_singleton()->flush();

	if (!result.window->has_focus()) {
		result.message = vformat("Failed to focus window '%s'. Another window may own focus.", result.window->get_title());
		return result;
	}

	result.ok = true;
	return result;
}
