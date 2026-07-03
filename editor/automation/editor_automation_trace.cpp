/**************************************************************************/
/*  editor_automation_trace.cpp                                           */
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

#include "editor_automation_trace.h"

#include "core/config/engine.h"
#include "core/os/os.h"

EditorAutomationTrace &EditorAutomationTrace::get_singleton() {
	static EditorAutomationTrace trace;
	return trace;
}

void EditorAutomationTrace::_push_entry(const EditorAutomationTraceEntry &p_entry) {
	entries[head] = p_entry;
	head = (head + 1) % MAX_ENTRIES;
	if (count < MAX_ENTRIES) {
		count++;
	}
}

void EditorAutomationTrace::begin_action() {
	pending_actions++;
}

void EditorAutomationTrace::end_action() {
	ERR_FAIL_COND(pending_actions <= 0);
	pending_actions--;
}

void EditorAutomationTrace::record_action(
		const String &p_action,
		const Dictionary &p_selector,
		const String &p_element_id,
		const String &p_element_summary,
		const String &p_route,
		const String &p_result_kind,
		bool p_ok,
		int p_log_marker,
		int p_error_marker) {
	EditorAutomationTraceEntry entry;
	entry.timestamp_usec = OS::get_singleton()->get_ticks_usec();
	entry.frame = Engine::get_singleton() != nullptr ? Engine::get_singleton()->get_frames_drawn() : 0;
	entry.action = p_action;
	entry.selector = p_selector;
	entry.element_id = p_element_id;
	entry.element_summary = p_element_summary;
	entry.route = p_route;
	entry.result_kind = p_result_kind;
	entry.ok = p_ok;
	entry.log_marker = p_log_marker;
	entry.error_marker = p_error_marker;
	_push_entry(entry);
}

Dictionary EditorAutomationTrace::entry_to_dictionary(const EditorAutomationTraceEntry &p_entry) {
	Dictionary dict;
	dict["timestamp_usec"] = p_entry.timestamp_usec;
	dict["frame"] = p_entry.frame;
	if (!p_entry.action.is_empty()) {
		dict["action"] = p_entry.action;
	}
	if (!p_entry.selector.is_empty()) {
		dict["selector"] = p_entry.selector;
	}
	if (!p_entry.element_id.is_empty()) {
		dict["element_id"] = p_entry.element_id;
	}
	if (!p_entry.element_summary.is_empty()) {
		dict["element_summary"] = p_entry.element_summary;
	}
	if (!p_entry.route.is_empty()) {
		dict["route"] = p_entry.route;
	}
	if (!p_entry.result_kind.is_empty()) {
		dict["result_kind"] = p_entry.result_kind;
	}
	dict["ok"] = p_entry.ok;
	dict["log_marker"] = p_entry.log_marker;
	dict["error_marker"] = p_entry.error_marker;
	return dict;
}

Array EditorAutomationTrace::get_recent(int p_count) const {
	Array recent;
	const int limit = MIN(p_count, count);
	for (int i = 0; i < limit; i++) {
		const int index = (head - 1 - i + MAX_ENTRIES) % MAX_ENTRIES;
		recent.push_back(entry_to_dictionary(entries[index]));
	}
	return recent;
}

void EditorAutomationTrace::clear() {
	head = 0;
	count = 0;
	pending_actions = 0;
}
