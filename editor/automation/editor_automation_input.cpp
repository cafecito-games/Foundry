/**************************************************************************/
/*  editor_automation_input.cpp                                           */
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

#include "editor_automation_input.h"

#include "core/input/input.h"
#include "core/input/input_map.h"
#include "core/math/math_funcs.h"
#include "core/object/message_queue.h"
#include "core/object/object.h"
#include "core/os/keyboard.h"
#include "scene/gui/control.h"
#include "scene/gui/subviewport_container.h"
#include "scene/main/viewport.h"
#include "scene/main/window.h"
#include "servers/display/display_server.h"

namespace {

Vector2 editor_automation_last_mouse_position;
MouseButton editor_automation_held_button = MouseButton::NONE;

// The viewport a still-held gesture was started on, so an abandoned gesture can
// be released on the same viewport that received its press.
ObjectID editor_automation_gesture_viewport;

// Where the system pointer was, in the coordinates of the viewport that owned
// the gesture, before automation first warped it. Warping is what makes a
// synthesized drop land where it aims, but it moves the machine's real cursor,
// so a gesture has to put it back when it ends or is abandoned.
ObjectID editor_automation_pointer_restore_viewport;
Vector2 editor_automation_pointer_restore_position;
bool editor_automation_pointer_has_restore_position = false;

bool _display_server_can_warp() {
	DisplayServer *display_server = DisplayServer::get_singleton();
	return display_server != nullptr && display_server->has_feature(DisplayServer::FEATURE_MOUSE_WARP);
}

void _remember_system_pointer(Viewport *p_viewport) {
	if (editor_automation_pointer_has_restore_position || p_viewport == nullptr) {
		return;
	}
	editor_automation_pointer_restore_position = p_viewport->get_mouse_position();
	editor_automation_pointer_restore_viewport = p_viewport->get_instance_id();
	editor_automation_pointer_has_restore_position = true;
}

void _restore_system_pointer() {
	if (!editor_automation_pointer_has_restore_position) {
		return;
	}
	const ObjectID viewport_id = editor_automation_pointer_restore_viewport;
	const Vector2 position = editor_automation_pointer_restore_position;
	editor_automation_pointer_has_restore_position = false;
	editor_automation_pointer_restore_viewport = ObjectID();
	editor_automation_pointer_restore_position = Vector2();

	if (!_display_server_can_warp()) {
		return;
	}
	Viewport *viewport = ObjectDB::get_instance<Viewport>(viewport_id);
	if (viewport == nullptr || !viewport->is_inside_tree()) {
		return;
	}
	// Symmetric with the warp in sync_window_pointer: both sides go through the
	// same viewport transform, so the pointer lands exactly where it started.
	viewport->warp_mouse(position);
}

// Releases a still-held gesture button on the viewport that received its
// press, so that viewport's own drag bookkeeping (Viewport::gui_is_dragging())
// is unwound instead of being left mid-drag. Shared by every abandonment path
// -- the public teardown entry point and the automatic scope destructor --
// so they cannot drift into different contracts.
//
// This routes through EditorAutomationInput::end_mouse_gesture, which declares
// its own AutomationGestureScope. That inner scope cannot re-enter this
// function: end_mouse_gesture clears editor_automation_held_button before this
// call returns, so by the time the inner scope's destructor runs (on that
// call's own return), the held-button check below is already false and it is
// a no-op.
void _unwind_held_gesture() {
	if (editor_automation_held_button != MouseButton::NONE) {
		Viewport *viewport = ObjectDB::get_instance<Viewport>(editor_automation_gesture_viewport);
		if (viewport != nullptr && viewport->is_inside_tree()) {
			PackedStringArray discarded_events;
			const EditorAutomationInputModifiers modifiers;
			EditorAutomationInput::end_mouse_gesture(viewport, editor_automation_last_mouse_position, modifiers, discarded_events);
		}
	}
	editor_automation_held_button = MouseButton::NONE;
	editor_automation_gesture_viewport = ObjectID();
}

// A gesture owns the held mouse button and the system pointer from its first
// warp until its release. Any path that leaves this scope without the gesture
// continuing -- an early error return as much as a normal end -- abandons it:
// the held button is released on the viewport that started the drag (so that
// viewport is never left mid-drag) and the pointer goes back where the user
// left it, so a failed gesture cannot poison the next one.
struct AutomationGestureScope {
	bool in_flight = false;

	void keep_in_flight() { in_flight = true; }

	~AutomationGestureScope() {
		if (in_flight) {
			return;
		}
		_unwind_held_gesture();
		_restore_system_pointer();
	}
};

#ifdef TESTS_ENABLED
struct AutomationMouseTrace {
	static bool enabled;
	static Vector2 press_global;
	static Vector2 release_global;
};

bool AutomationMouseTrace::enabled = false;
Vector2 AutomationMouseTrace::press_global;
Vector2 AutomationMouseTrace::release_global;
#endif // TESTS_ENABLED

void _copy_option_if_present(const Dictionary &p_from, Dictionary &r_to, const char *p_key) {
	if (p_from.has(p_key)) {
		r_to[p_key] = p_from.get(p_key, Variant());
	}
}

void _merge_position_spec(const Dictionary &p_from, Dictionary &r_to) {
	_copy_option_if_present(p_from, r_to, "position");
	_copy_option_if_present(p_from, r_to, "anchor");
	_copy_option_if_present(p_from, r_to, "x");
	_copy_option_if_present(p_from, r_to, "y");
}

void _copy_prefixed_position_option(const Dictionary &p_from, Dictionary &r_to, const char *p_prefix, const char *p_key) {
	const String prefixed = String(p_prefix) + String(p_key);
	if (p_from.has(prefixed)) {
		r_to[p_key] = p_from.get(prefixed, Variant());
	}
}

// Input keeps parsed events in an accumulation buffer that the main loop drains
// once per frame, and consecutive mouse motions collapse into a single event
// while they sit there. A synthesized gesture therefore has to drain the buffer
// itself: without this, press/motion/release issued inside one action call are
// never delivered while the action runs, and the intermediate motions that
// drive drag detection and drop-target tracking are merged away.
void _dispatch_input_event(const Ref<InputEvent> &p_event) {
	Input::get_singleton()->parse_input_event(p_event);
	Input::get_singleton()->flush_buffered_events();
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
	// Text entry must be routed through the target viewport so popup and modal
	// LineEdits receive unicode keys. Global parse_input_event only reaches the
	// root window focus chain and misses exclusive child windows.
	if (p_unicode != 0) {
		p_viewport->push_input(event);
		MessageQueue::get_singleton()->flush();
	} else {
		_dispatch_input_event(event);
	}

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
					p_viewport->push_input(key_event);
					MessageQueue::get_singleton()->flush();
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
#ifdef TESTS_ENABLED
		if (AutomationMouseTrace::enabled && !p_local_coords) {
			AutomationMouseTrace::press_global = p_position;
		}
#endif // TESTS_ENABLED
	} else {
		r_events.push_back("mouse_released");
#ifdef TESTS_ENABLED
		if (AutomationMouseTrace::enabled && !p_local_coords) {
			AutomationMouseTrace::release_global = p_position;
		}
#endif // TESTS_ENABLED
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

MouseButton EditorAutomationInput::get_held_mouse_button() {
	return editor_automation_held_button;
}

Vector2 EditorAutomationInput::get_last_mouse_position() {
	return editor_automation_last_mouse_position;
}

void EditorAutomationInput::reset_pointer_state() {
	_restore_system_pointer();
	editor_automation_held_button = MouseButton::NONE;
	editor_automation_gesture_viewport = ObjectID();
	editor_automation_last_mouse_position = Vector2();
}

void EditorAutomationInput::abandon_gesture() {
	_unwind_held_gesture();
	_restore_system_pointer();
}

void EditorAutomationInput::sync_window_pointer(Viewport *p_viewport, const Vector2 &p_global) {
	if (p_viewport == nullptr) {
		return;
	}
	if (!_display_server_can_warp()) {
		return;
	}
	_remember_system_pointer(p_viewport);
	// Drop handling reads the pointer from the display server, not from the
	// synthesized event: Viewport::get_mouse_position() (and therefore the point
	// handed to can_drop_data/drop_data) comes from DisplayServer::mouse_get_position(),
	// and a native window only tracks a drop target at all while the window
	// manager reports the pointer as being inside it. Moving the system pointer
	// along with the gesture is what makes a synthesized drag land where it aims.
	p_viewport->warp_mouse(p_global);
}

bool EditorAutomationInput::begin_mouse_gesture(
		Viewport *p_viewport,
		const Vector2 &p_global,
		MouseButton p_button,
		const EditorAutomationInputModifiers &p_modifiers,
		PackedStringArray &r_events) {
	// Declared before the argument check so that every return from here on --
	// including the failed precondition -- runs the pointer restore.
	AutomationGestureScope gesture;
	ERR_FAIL_NULL_V(p_viewport, false);

	// A press while a button is still held means the previous gesture was never
	// finished. Release it on its own terms first, so held state and the drag it
	// left in the viewport cannot accumulate across requests.
	if (editor_automation_held_button != MouseButton::NONE) {
		Viewport *previous_viewport = ObjectDB::get_instance<Viewport>(editor_automation_gesture_viewport);
		if (previous_viewport == nullptr || !previous_viewport->is_inside_tree()) {
			previous_viewport = p_viewport;
		}
		end_mouse_gesture(previous_viewport, editor_automation_last_mouse_position, p_modifiers, r_events);
	}

	sync_window_pointer(p_viewport, p_global);

	// Hover the origin before pressing so the viewport's mouse-over bookkeeping
	// matches the press position even when the gesture starts far from wherever
	// the previous action left the pointer.
	if (!push_mouse_motion(p_viewport, p_global, Vector2(), MouseButtonMask::NONE, p_modifiers, r_events)) {
		return false;
	}

	const MouseButtonMask button_mask = mouse_button_to_mask(p_button);
	if (!push_mouse_button(p_viewport, p_global, p_button, true, button_mask, p_modifiers, r_events)) {
		return false;
	}
	editor_automation_held_button = p_button;
	editor_automation_gesture_viewport = p_viewport->get_instance_id();
	gesture.keep_in_flight();
	return true;
}

bool EditorAutomationInput::move_mouse_gesture(
		Viewport *p_viewport,
		const Vector2 &p_to_global,
		const Vector<Vector2> &p_waypoints,
		const EditorAutomationInputModifiers &p_modifiers,
		PackedStringArray &r_events) {
	AutomationGestureScope gesture;
	ERR_FAIL_NULL_V(p_viewport, false);

	const MouseButtonMask button_mask = editor_automation_held_button == MouseButton::NONE
			? MouseButtonMask::NONE
			: mouse_button_to_mask(editor_automation_held_button);

	Vector<Vector2> path;
	path.push_back(editor_automation_last_mouse_position);
	for (const Vector2 &waypoint : p_waypoints) {
		path.push_back(waypoint);
	}
	path.push_back(p_to_global);

	// Step in increments below the viewport's drag threshold so accumulation
	// crosses the threshold on a motion event rather than in one jump, matching
	// how a real pointer produces a drag.
	const float step_distance = MAX(1.0f, float(p_viewport->get_drag_threshold()) * 0.5f);
	Vector2 current = editor_automation_last_mouse_position;
	for (int segment_index = 1; segment_index < path.size(); segment_index++) {
		const Vector2 target = path[segment_index];
		const float distance = (target - current).length();
		const int steps = MAX(1, int(Math::ceil(distance / step_distance)));
		for (int step = 1; step <= steps; step++) {
			const Vector2 next = current.lerp(target, float(step) / float(steps));
			const Vector2 relative = next - current;
			current = next;
			sync_window_pointer(p_viewport, current);
			if (!push_mouse_motion(p_viewport, current, relative, button_mask, p_modifiers, r_events)) {
				return false;
			}
		}
	}

	// A move with a button down leaves the gesture in flight and the pointer at
	// the drag position, which is what drop tracking reads. A move with no
	// button held is a hover: it has no continuation, so the pointer goes back.
	if (editor_automation_held_button != MouseButton::NONE) {
		gesture.keep_in_flight();
	}
	return true;
}

bool EditorAutomationInput::end_mouse_gesture(
		Viewport *p_viewport,
		const Vector2 &p_global,
		const EditorAutomationInputModifiers &p_modifiers,
		PackedStringArray &r_events) {
	// The scope is never kept in flight here: the release ends the gesture, so
	// the system pointer is handed back to the user once the drop is delivered.
	AutomationGestureScope gesture;
	ERR_FAIL_NULL_V(p_viewport, false);

	const MouseButton button = editor_automation_held_button == MouseButton::NONE
			? MouseButton::LEFT
			: editor_automation_held_button;
	editor_automation_held_button = MouseButton::NONE;
	editor_automation_gesture_viewport = ObjectID();
	sync_window_pointer(p_viewport, p_global);
	return push_mouse_button(p_viewport, p_global, button, false, MouseButtonMask::NONE, p_modifiers, r_events);
}

bool EditorAutomationInput::push_mouse_drag(
		Viewport *p_viewport,
		const Vector2 &p_from_global,
		const Vector2 &p_to_global,
		const Vector<Vector2> &p_waypoints,
		MouseButton p_button,
		const EditorAutomationInputModifiers &p_modifiers,
		PackedStringArray &r_events,
		bool p_release) {
	ERR_FAIL_NULL_V(p_viewport, false);

	if (!begin_mouse_gesture(p_viewport, p_from_global, p_button, p_modifiers, r_events)) {
		return false;
	}
	if (!move_mouse_gesture(p_viewport, p_to_global, p_waypoints, p_modifiers, r_events)) {
		return false;
	}
	if (!p_release) {
		return true;
	}
	return end_mouse_gesture(p_viewport, p_to_global, p_modifiers, r_events);
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

	if (p_options.has("position")) {
		const Variant position_value = p_options.get("position", Variant());
		if (position_value.get_type() == Variant::VECTOR2) {
			return rect.position + Vector2(position_value);
		}
		if (position_value.get_type() == Variant::ARRAY) {
			const Array position = position_value;
			if (position.size() >= 2) {
				return rect.position + Vector2(position[0], position[1]);
			}
		}
	}

	if (p_options.has("x") && p_options.has("y")) {
		const Variant x_value = p_options.get("x", Variant());
		const Variant y_value = p_options.get("y", Variant());
		Vector2 position = rect.position;
		if (x_value.is_num()) {
			const double x = x_value;
			if (x >= 0.0 && x <= 1.0 && rect.size.x > 0) {
				position.x += float(x) * rect.size.x;
			} else {
				position.x += float(x);
			}
		}
		if (y_value.is_num()) {
			const double y = y_value;
			if (y >= 0.0 && y <= 1.0 && rect.size.y > 0) {
				position.y += float(y) * rect.size.y;
			} else {
				position.y += float(y);
			}
		}
		return position;
	}

	return rect.get_center();
}

Dictionary EditorAutomationInput::position_options_for_source(const Dictionary &p_options) {
	Dictionary result;
	if (p_options.has("source")) {
		const Variant source_value = p_options.get("source", Variant());
		if (source_value.get_type() == Variant::DICTIONARY) {
			_merge_position_spec(source_value, result);
		}
	}
	_merge_position_spec(p_options, result);
	_copy_prefixed_position_option(p_options, result, "source_", "position");
	_copy_prefixed_position_option(p_options, result, "source_", "anchor");
	_copy_prefixed_position_option(p_options, result, "source_", "x");
	_copy_prefixed_position_option(p_options, result, "source_", "y");
	return result;
}

Dictionary EditorAutomationInput::position_options_for_target_element(const Dictionary &p_options) {
	Dictionary result;
	if (p_options.has("target_position")) {
		const Variant target_position_value = p_options.get("target_position", Variant());
		if (target_position_value.get_type() == Variant::DICTIONARY) {
			_merge_position_spec(target_position_value, result);
		}
	}
	_copy_prefixed_position_option(p_options, result, "target_", "anchor");
	_copy_prefixed_position_option(p_options, result, "target_", "x");
	_copy_prefixed_position_option(p_options, result, "target_", "y");
	return result;
}

Vector2 EditorAutomationInput::resolve_target_point(const Dictionary &p_options) {
	if (!p_options.has("target_point")) {
		return Vector2();
	}
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
	return Vector2();
}

#ifdef TESTS_ENABLED
void EditorAutomationInput::set_mouse_trace_enabled(bool p_enabled) {
	AutomationMouseTrace::enabled = p_enabled;
	if (!p_enabled) {
		AutomationMouseTrace::press_global = Vector2();
		AutomationMouseTrace::release_global = Vector2();
	}
}

Vector2 EditorAutomationInput::get_mouse_trace_press_global() {
	return AutomationMouseTrace::press_global;
}

Vector2 EditorAutomationInput::get_mouse_trace_release_global() {
	return AutomationMouseTrace::release_global;
}
#endif // TESTS_ENABLED

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

	Window *root_window = p_node->get_window();
	if (root_window != nullptr) {
		Window *exclusive = root_window->get_exclusive_child();
		while (exclusive != nullptr) {
			if (exclusive == result.window || exclusive->is_ancestor_of(result.window)) {
				result.ok = true;
				return result;
			}
			exclusive = exclusive->get_exclusive_child();
		}
	}

	if (!result.window->is_visible()) {
		result.message = vformat("Window '%s' is not visible.", result.window->get_title());
		return result;
	}

	if (Control *control = Object::cast_to<Control>(p_node)) {
		control->grab_focus();
		MessageQueue::get_singleton()->flush();
		if (control->has_focus()) {
			result.ok = true;
			return result;
		}
	}

	result.window->grab_focus();
	MessageQueue::get_singleton()->flush();

	if (result.window->has_focus()) {
		result.ok = true;
		return result;
	}

	if (root_window != nullptr && root_window->has_focus_or_active_popup() && result.window == root_window) {
		result.ok = true;
		return result;
	}

	result.message = vformat("Failed to focus window '%s'. Another window may own focus.", result.window->get_title());
	return result;
}
