/**************************************************************************/
/*  editor_automation_events.cpp                                          */
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

#include "editor_automation_events.h"

#include "editor/automation/editor_automation_log.h"

namespace {

struct EditorAutomationEventEntry {
	String kind;
	Dictionary payload;
};

LocalVector<EditorAutomationEventEntry> events;
int last_polled_log_index = 0;

bool _kind_matches(const String &p_kind, const PackedStringArray &p_kinds) {
	if (p_kinds.is_empty()) {
		return true;
	}
	for (int i = 0; i < p_kinds.size(); i++) {
		if (p_kind == p_kinds[i]) {
			return true;
		}
	}
	return false;
}

void _push_log_event(const String &p_kind, const Dictionary &p_message) {
	EditorAutomationEventEntry entry;
	entry.kind = p_kind;
	entry.payload = p_message;
	events.push_back(entry);
}

} // namespace

void EditorAutomationEvents::reset() {
	events.clear();
	last_polled_log_index = EditorAutomationLog::get_message_count();
}

void EditorAutomationEvents::poll_sources() {
	const int total = EditorAutomationLog::get_message_count();
	for (int i = last_polled_log_index; i < total; i++) {
		const Dictionary message = EditorAutomationLog::read_message_at(i);
		const String severity = message.get("severity", String());
		if (severity == "error" || severity == "warning") {
			Dictionary payload = message;
			payload["message_index"] = i;
			_push_log_event(vformat("editor_log_%s", severity), payload);
		}
	}
	last_polled_log_index = total;
}

void EditorAutomationEvents::push_automation_event(const String &p_kind, const Dictionary &p_payload) {
	EditorAutomationEventEntry entry;
	entry.kind = p_kind;
	entry.payload = p_payload;
	events.push_back(entry);
}

EditorAutomationEventMarker EditorAutomationEvents::create_marker() {
	EditorAutomationEventMarker marker;
	marker.event_index = get_event_count();
	return marker;
}

int EditorAutomationEvents::get_event_count() {
	return events.size();
}

Array EditorAutomationEvents::read_since(const EditorAutomationEventMarker &p_marker, const PackedStringArray &p_kinds, int p_limit) {
	Array result;
	if (p_limit <= 0) {
		p_limit = 64;
	}
	const int total = events.size();
	for (int i = p_marker.event_index; i < total; i++) {
		if (result.size() >= p_limit) {
			break;
		}
		const EditorAutomationEventEntry &entry = events[i];
		if (!_kind_matches(entry.kind, p_kinds)) {
			continue;
		}
		Dictionary event;
		event["index"] = i;
		event["kind"] = entry.kind;
		event["payload"] = entry.payload;
		result.push_back(event);
	}
	return result;
}

void EditorAutomationEvents::clear_test_events() {
	events.clear();
	last_polled_log_index = 0;
}
