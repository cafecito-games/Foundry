/**************************************************************************/
/*  editor_automation_input.h                                             */
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

#include "core/input/input_enums.h"
#include "core/input/input_event.h"
#include "core/math/rect2i.h"
#include "core/string/ustring.h"
#include "core/variant/variant.h"

class Control;
class Node;
class SubViewport;
class SubViewportContainer;
class Viewport;
class Window;

struct EditorAutomationInputModifiers {
	bool shift = false;
	bool ctrl = false;
	bool alt = false;
	bool meta = false;
};

struct EditorAutomationWindowFocusResult {
	bool ok = false;
	String message;
	Window *window = nullptr;
};

class EditorAutomationInput {
public:
	static EditorAutomationInputModifiers parse_modifiers(const Variant &p_modifiers);
	static void apply_modifiers(Ref<InputEventWithModifiers> p_event, const EditorAutomationInputModifiers &p_modifiers);

	static MouseButton parse_mouse_button(const String &p_button_name, MouseButton p_default = MouseButton::LEFT);
	static MouseButtonMask mouse_button_to_mask(MouseButton p_button);

	static bool push_key_event(
			Viewport *p_viewport,
			Key p_key,
			bool p_pressed,
			char32_t p_unicode,
			const EditorAutomationInputModifiers &p_modifiers,
			PackedStringArray &r_events);

	static bool push_input_action(
			Viewport *p_viewport,
			const StringName &p_action,
			bool p_pressed,
			PackedStringArray &r_events);

	static bool push_mouse_button(
			Viewport *p_viewport,
			const Vector2 &p_position,
			MouseButton p_button,
			bool p_pressed,
			MouseButtonMask p_button_mask,
			const EditorAutomationInputModifiers &p_modifiers,
			PackedStringArray &r_events,
			bool p_local_coords = false);

	static bool push_mouse_motion(
			Viewport *p_viewport,
			const Vector2 &p_position,
			const Vector2 &p_relative,
			MouseButtonMask p_button_mask,
			const EditorAutomationInputModifiers &p_modifiers,
			PackedStringArray &r_events,
			bool p_local_coords = false);

	static bool push_mouse_drag(
			Viewport *p_viewport,
			const Vector2 &p_from_global,
			const Vector2 &p_to_global,
			const Vector<Vector2> &p_waypoints,
			MouseButton p_button,
			const EditorAutomationInputModifiers &p_modifiers,
			PackedStringArray &r_events);

	static Vector2 resolve_position_in_bounds(const Rect2i &p_bounds, const Dictionary &p_options);
	static Dictionary position_options_for_source(const Dictionary &p_options);
	static Dictionary position_options_for_target_element(const Dictionary &p_options);
	static Vector2 resolve_target_point(const Dictionary &p_options);
	static Vector2 global_center_of_bounds(const Rect2i &p_bounds);

#ifdef TESTS_ENABLED
	static void set_mouse_trace_enabled(bool p_enabled);
	static Vector2 get_mouse_trace_press_global();
	static Vector2 get_mouse_trace_release_global();
#endif // TESTS_ENABLED

	static Viewport *viewport_for_node(Node *p_node);
	static Viewport *input_viewport_for_control(Control *p_control, Vector2 &r_local_position, const Vector2 &p_global_position);
	static Window *window_for_node(Node *p_node);
	static EditorAutomationWindowFocusResult ensure_window_focus(Node *p_node);
};
