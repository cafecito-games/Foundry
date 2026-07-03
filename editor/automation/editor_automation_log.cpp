/**************************************************************************/
/*  editor_automation_log.cpp                                             */
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

#include "editor_automation_log.h"

#include "editor/editor_log.h"
#include "editor/editor_node.h"

namespace {

struct EditorAutomationTestLogMessage {
	String text;
	String severity;
};

LocalVector<EditorAutomationTestLogMessage> test_messages;
bool use_test_log = false;

EditorLog *get_editor_log() {
	EditorNode *editor_node = EditorNode::get_singleton();
	if (editor_node == nullptr) {
		return nullptr;
	}
	return EditorNode::get_log();
}

Dictionary _message_to_dictionary(const String &p_text, const String &p_severity, int p_count = 1) {
	Dictionary dict;
	dict["text"] = p_text;
	dict["severity"] = p_severity;
	dict["count"] = p_count;
	return dict;
}

bool _severity_matches(const String &p_message_severity, const String &p_filter_severity) {
	if (p_filter_severity.is_empty()) {
		return true;
	}
	return p_message_severity == p_filter_severity;
}

bool _message_matches_severities(const String &p_message_severity, const PackedStringArray &p_severities) {
	if (p_severities.is_empty()) {
		return true;
	}
	for (int i = 0; i < p_severities.size(); i++) {
		if (p_message_severity == p_severities[i]) {
			return true;
		}
	}
	return false;
}

} // namespace

String EditorAutomationLog::message_type_to_string(int p_type) {
	switch (static_cast<EditorLog::MessageType>(p_type)) {
		case EditorLog::MSG_TYPE_ERROR:
			return "error";
		case EditorLog::MSG_TYPE_WARNING:
			return "warning";
		case EditorLog::MSG_TYPE_EDITOR:
			return "editor";
		case EditorLog::MSG_TYPE_STD_RICH:
			return "stdout_rich";
		case EditorLog::MSG_TYPE_STD:
		default:
			return "stdout";
	}
}

bool EditorAutomationLog::message_type_matches_severity(int p_type, const String &p_severity) {
	if (p_severity.is_empty()) {
		return true;
	}
	return message_type_to_string(p_type) == p_severity;
}

EditorAutomationLogMarker EditorAutomationLog::create_marker() {
	EditorAutomationLogMarker marker;
	marker.message_index = get_message_count();
	return marker;
}

int EditorAutomationLog::get_message_count() {
	if (use_test_log) {
		return test_messages.size();
	}
	EditorLog *log = get_editor_log();
	if (log == nullptr) {
		return 0;
	}
	return log->get_message_count();
}

Dictionary EditorAutomationLog::read_message_at(int p_index) {
	if (use_test_log) {
		ERR_FAIL_COND_V(p_index < 0 || (uint32_t)p_index >= (uint32_t)test_messages.size(), Dictionary());
		return _message_to_dictionary(test_messages[p_index].text, test_messages[p_index].severity);
	}
	EditorLog *log = get_editor_log();
	ERR_FAIL_NULL_V(log, Dictionary());
	ERR_FAIL_INDEX_V(p_index, log->get_message_count(), Dictionary());
	return log->get_message_snapshot(p_index);
}

Array EditorAutomationLog::read_since(const EditorAutomationLogMarker &p_marker, const PackedStringArray &p_severities) {
	Array messages;
	const int total = get_message_count();
	for (int i = p_marker.message_index; i < total; i++) {
		const Dictionary message = read_message_at(i);
		const String severity = message.get("severity", String());
		if (_message_matches_severities(severity, p_severities)) {
			messages.push_back(message);
		}
	}
	return messages;
}

bool EditorAutomationLog::contains_text(const String &p_text, const String &p_severity) {
	const int total = get_message_count();
	for (int i = 0; i < total; i++) {
		const Dictionary message = read_message_at(i);
		const String text = message.get("text", String());
		const String severity = message.get("severity", String());
		if (!_severity_matches(severity, p_severity)) {
			continue;
		}
		if (p_text.is_empty() || text.contains(p_text)) {
			return true;
		}
	}
	return false;
}

bool EditorAutomationLog::has_new_messages_since(const EditorAutomationLogMarker &p_marker, const PackedStringArray &p_severities) {
	return !read_since(p_marker, p_severities).is_empty();
}

Array EditorAutomationLog::read_recent(int p_count, const PackedStringArray &p_severities) {
	Array messages;
	const int total = get_message_count();
	const int start = MAX(0, total - p_count);
	for (int i = start; i < total; i++) {
		const Dictionary message = read_message_at(i);
		const String severity = message.get("severity", String());
		if (_message_matches_severities(severity, p_severities)) {
			messages.push_back(message);
		}
	}
	return messages;
}

void EditorAutomationLog::push_test_message(const String &p_text, const String &p_severity) {
	use_test_log = true;
	EditorAutomationTestLogMessage message;
	message.text = p_text;
	message.severity = p_severity;
	test_messages.push_back(message);
}

void EditorAutomationLog::clear_test_messages() {
	test_messages.clear();
	use_test_log = false;
}

bool EditorAutomationLog::is_using_test_log() {
	return use_test_log;
}
