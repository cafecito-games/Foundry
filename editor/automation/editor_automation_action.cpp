/**************************************************************************/
/*  editor_automation_action.cpp                                          */
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

#include "editor_automation_action.h"

EditorAutomationActionResult EditorAutomationActionResult::success(const String &p_route, const String &p_element_id) {
	EditorAutomationActionResult result;
	result.ok = true;
	result.route = p_route;
	result.element_id = p_element_id;
	return result;
}

EditorAutomationActionResult EditorAutomationActionResult::failure(const String &p_kind, const String &p_message, const Array &p_candidates) {
	EditorAutomationActionResult result;
	result.ok = false;
	result.kind = p_kind;
	result.message = p_message;
	result.candidates = p_candidates;
	return result;
}

Dictionary EditorAutomationActionResult::to_dictionary() const {
	Dictionary dict;
	dict["ok"] = ok;
	if (!route.is_empty()) {
		dict["route"] = route;
	}
	if (!element_id.is_empty()) {
		dict["element_id"] = element_id;
	}
	if (!kind.is_empty()) {
		dict["kind"] = kind;
	}
	if (!message.is_empty()) {
		dict["message"] = message;
	}
	if (!events.is_empty()) {
		dict["events"] = events;
	}
	if (!focus.is_empty()) {
		dict["focus"] = focus;
	}
	if (!candidates.is_empty()) {
		dict["candidates"] = candidates;
	}
	if (!details.is_empty()) {
		dict["details"] = details;
	}
	return dict;
}

EditorAutomationActionKind editor_automation_action_kind_from_string(const String &p_action) {
	const String action = p_action.to_lower();
	if (action == "focus") {
		return EditorAutomationActionKind::FOCUS;
	}
	if (action == "click") {
		return EditorAutomationActionKind::CLICK;
	}
	if (action == "type_text") {
		return EditorAutomationActionKind::TYPE_TEXT;
	}
	if (action == "set_text") {
		return EditorAutomationActionKind::SET_TEXT;
	}
	if (action == "press_key") {
		return EditorAutomationActionKind::PRESS_KEY;
	}
	if (action == "select") {
		return EditorAutomationActionKind::SELECT;
	}
	if (action == "activate") {
		return EditorAutomationActionKind::ACTIVATE;
	}
	if (action == "submit") {
		return EditorAutomationActionKind::SUBMIT;
	}
	if (action == "scroll") {
		return EditorAutomationActionKind::SCROLL;
	}
	if (action == "expand") {
		return EditorAutomationActionKind::EXPAND;
	}
	if (action == "collapse") {
		return EditorAutomationActionKind::COLLAPSE;
	}
	if (action == "choose_menu_item") {
		return EditorAutomationActionKind::CHOOSE_MENU_ITEM;
	}
	if (action == "set_value") {
		return EditorAutomationActionKind::SET_VALUE;
	}
	if (action == "increment") {
		return EditorAutomationActionKind::INCREMENT;
	}
	if (action == "decrement") {
		return EditorAutomationActionKind::DECREMENT;
	}
	if (action == "drag") {
		return EditorAutomationActionKind::DRAG;
	}
	return EditorAutomationActionKind::UNKNOWN;
}

EditorAutomationRoutePreference editor_automation_route_preference_from_string(const String &p_route) {
	const String route = p_route.to_lower();
	if (route == "semantic") {
		return EditorAutomationRoutePreference::SEMANTIC;
	}
	if (route == "input") {
		return EditorAutomationRoutePreference::INPUT;
	}
	return EditorAutomationRoutePreference::AUTO;
}
