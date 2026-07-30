/**************************************************************************/
/*  editor_automation_action.h                                            */
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

#include "core/string/ustring.h"
#include "core/variant/variant.h"

struct EditorAutomationActionRouteNames {
	static inline const char *SEMANTIC_FOCUS = "semantic_focus";
	static inline const char *SEMANTIC_CLICK = "semantic_click";
	static inline const char *SEMANTIC_SET_TEXT = "semantic_set_text";
	static inline const char *SEMANTIC_SELECT = "semantic_select";
	static inline const char *SEMANTIC_ACTIVATE = "semantic_activate";
	static inline const char *SEMANTIC_SUBMIT = "semantic_submit";
	static inline const char *SEMANTIC_SCROLL = "semantic_scroll";
	static inline const char *SEMANTIC_SET_VALUE = "semantic_set_value";
	static inline const char *INPUT_MOUSE_CLICK = "input_mouse_click";
	static inline const char *INPUT_DOUBLE_CLICK = "input_double_click";
	static inline const char *INPUT_SCROLL = "input_scroll";
	static inline const char *INPUT_VIEWPORT_CLICK = "input_viewport_click";
	static inline const char *INPUT_KEY = "input_key";
	static inline const char *INPUT_TEXT = "input_text";
	static inline const char *INPUT_DRAG = "input_drag";
	static inline const char *INPUT_CONTEXT_MENU = "input_context_menu";
	static inline const char *SEMANTIC_DOCK = "semantic_dock";
	static inline const char *INPUT_DOCK = "input_dock";
	static inline const char *UNSUPPORTED = "unsupported";
};

enum class EditorAutomationRoutePreference {
	AUTO,
	SEMANTIC,
	INPUT,
};

enum class EditorAutomationActionKind {
	FOCUS,
	CLICK,
	TYPE_TEXT,
	SET_TEXT,
	PRESS_KEY,
	SELECT,
	ACTIVATE,
	SUBMIT,
	SCROLL,
	EXPAND,
	COLLAPSE,
	CHOOSE_MENU_ITEM,
	OPEN_CONTEXT_MENU,
	SET_VALUE,
	INCREMENT,
	DECREMENT,
	DRAG,
	DOCK,
	UNKNOWN,
};

struct EditorAutomationActionResult {
	bool ok = false;
	String route;
	String element_id;
	String kind;
	String message;
	PackedStringArray events;
	String focus;
	Array candidates;
	Dictionary details;

	static EditorAutomationActionResult success(const String &p_route, const String &p_element_id);
	static EditorAutomationActionResult failure(const String &p_kind, const String &p_message, const Array &p_candidates = Array());

	Dictionary to_dictionary() const;
};

EditorAutomationActionKind editor_automation_action_kind_from_string(const String &p_action);
EditorAutomationRoutePreference editor_automation_route_preference_from_string(const String &p_route);
