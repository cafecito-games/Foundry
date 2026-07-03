/**************************************************************************/
/*  editor_automation_wait.cpp                                            */
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

#include "editor_automation_wait.h"

#include "editor/automation/editor_automation_selector.h"
#include "editor/automation/editor_automation_state.h"
#include "editor/automation/editor_automation_trace.h"
#include "editor/editor_node.h"
#include "editor/file_system/editor_file_system.h"
#include "core/object/message_queue.h"
#include "scene/main/scene_tree.h"

namespace {

enum class WaitConditionKind {
	NEXT_FRAME,
	EDITOR_IDLE,
	SELECTOR_APPEARS,
	SELECTOR_DISAPPEARS,
	SELECTOR_MATCHES,
	FOCUS_MATCHES,
	MODAL_STACK_CHANGED,
	MODAL_STACK_SETTLED,
	FILESYSTEM_IDLE,
	IMPORT_RELOAD_IDLE,
	SCRIPT_ANALYSIS_IDLE,
	LOG_CONTAINS,
	NO_NEW_ERRORS,
	UNSUPPORTED,
};

String _read_string(const Dictionary &p_dict, const char *p_key) {
	if (!p_dict.has(p_key)) {
		return String();
	}
	return p_dict.get(p_key, Variant());
}

Dictionary _read_dictionary(const Dictionary &p_dict, const char *p_key) {
	if (!p_dict.has(p_key)) {
		return Dictionary();
	}
	return p_dict.get(p_key, Dictionary());
}

PackedStringArray _read_string_array(const Dictionary &p_dict, const char *p_key) {
	PackedStringArray values;
	if (!p_dict.has(p_key)) {
		return values;
	}
	const Variant raw = p_dict.get(p_key, Variant());
	if (raw.get_type() == Variant::PACKED_STRING_ARRAY) {
		return raw;
	}
	if (raw.get_type() == Variant::ARRAY) {
		const Array array = raw;
		for (int i = 0; i < array.size(); i++) {
			values.push_back(array[i]);
		}
	}
	return values;
}

WaitConditionKind _parse_condition_kind(const Dictionary &p_condition) {
	const String type = _read_string(p_condition, "type").to_lower();
	if (type == "next_frame") {
		return WaitConditionKind::NEXT_FRAME;
	}
	if (type == "editor_idle") {
		return WaitConditionKind::EDITOR_IDLE;
	}
	if (type == "selector_appears") {
		return WaitConditionKind::SELECTOR_APPEARS;
	}
	if (type == "selector_disappears") {
		return WaitConditionKind::SELECTOR_DISAPPEARS;
	}
	if (type == "selector_matches") {
		return WaitConditionKind::SELECTOR_MATCHES;
	}
	if (type == "focus_matches") {
		return WaitConditionKind::FOCUS_MATCHES;
	}
	if (type == "modal_stack_changed") {
		return WaitConditionKind::MODAL_STACK_CHANGED;
	}
	if (type == "modal_stack_settled") {
		return WaitConditionKind::MODAL_STACK_SETTLED;
	}
	if (type == "filesystem_idle") {
		return WaitConditionKind::FILESYSTEM_IDLE;
	}
	if (type == "import_reload_idle") {
		return WaitConditionKind::IMPORT_RELOAD_IDLE;
	}
	if (type == "script_analysis_idle") {
		return WaitConditionKind::SCRIPT_ANALYSIS_IDLE;
	}
	if (type == "log_contains") {
		return WaitConditionKind::LOG_CONTAINS;
	}
	if (type == "no_new_errors") {
		return WaitConditionKind::NO_NEW_ERRORS;
	}
	return WaitConditionKind::UNSUPPORTED;
}

EditorAutomationSnapshot _capture_snapshot(const EditorAutomationWaitContext &p_context) {
	if (p_context.snapshot_root != nullptr) {
		return EditorAutomationSnapshot::capture_from_node(p_context.snapshot_root);
	}
	return EditorAutomationSnapshot::capture_from_editor();
}

bool _element_matches_fields(const EditorAutomationElement &p_element, const Dictionary &p_fields) {
	Array keys = p_fields.keys();
	for (int i = 0; i < keys.size(); i++) {
		const String key = keys[i];
		const Variant expected = p_fields.get(key, Variant());
		if (key == "visible" && expected.get_type() == Variant::BOOL && p_element.visible != bool(expected)) {
			return false;
		}
		if (key == "enabled" && expected.get_type() == Variant::BOOL && p_element.enabled != bool(expected)) {
			return false;
		}
		if (key == "focused" && expected.get_type() == Variant::BOOL && p_element.focused != bool(expected)) {
			return false;
		}
		if (key == "pressed" && expected.get_type() == Variant::BOOL && p_element.pressed != bool(expected)) {
			return false;
		}
		if (key == "selected" && expected.get_type() == Variant::BOOL && p_element.selected != bool(expected)) {
			return false;
		}
		if (key == "role" && String(expected) != p_element.role) {
			return false;
		}
		if (key == "name" && String(expected) != p_element.name) {
			return false;
		}
		if (key == "text" && String(expected) != p_element.text) {
			return false;
		}
		if (key == "class" && String(expected) != p_element.class_name) {
			return false;
		}
	}
	return true;
}

bool _selector_has_matches(const EditorAutomationSnapshot &p_snapshot, const Dictionary &p_selector) {
	const EditorAutomationSelectorResult result = EditorAutomationSelector::resolve(p_snapshot, p_selector);
	return result.status == EditorAutomationSelectorStatus::OK && !result.match_indices.is_empty();
}

bool _focus_matches_selector(const EditorAutomationSnapshot &p_snapshot, const Dictionary &p_selector) {
	const String &focused_id = p_snapshot.get_focused_element_id();
	if (focused_id.is_empty()) {
		return false;
	}
	const EditorAutomationSelectorResult result = EditorAutomationSelector::resolve(p_snapshot, p_selector);
	if (result.status != EditorAutomationSelectorStatus::OK || result.match_indices.size() != 1) {
		return false;
	}
	const EditorAutomationElement &element = p_snapshot.get_element(result.match_indices[0]);
	return element.id == focused_id;
}

bool _modal_stacks_equal(const Array &p_left, const Array &p_right) {
	if (p_left.size() != p_right.size()) {
		return false;
	}
	for (int i = 0; i < p_left.size(); i++) {
		const Dictionary left = p_left[i];
		const Dictionary right = p_right[i];
		if (left.get("title", String()) != right.get("title", String())) {
			return false;
		}
		if (left.get("class", String()) != right.get("class", String())) {
			return false;
		}
	}
	return true;
}

bool _is_filesystem_idle() {
	EditorFileSystem *filesystem = EditorFileSystem::get_singleton();
	if (filesystem == nullptr) {
		return true;
	}
	return !filesystem->is_scanning();
}

bool _is_import_reload_idle() {
	EditorFileSystem *filesystem = EditorFileSystem::get_singleton();
	if (filesystem == nullptr) {
		return true;
	}
	// Covers active filesystem scans and import/reimport work tracked by EditorFileSystem.
	return !filesystem->is_scanning() && !filesystem->is_importing();
}

void _advance_one_frame(double p_delta) {
	SceneTree *tree = SceneTree::get_singleton();
	if (tree == nullptr) {
		return;
	}
	tree->process(p_delta);
	if (MessageQueue::get_singleton() != nullptr) {
		MessageQueue::get_singleton()->flush();
	}
}

} // namespace

EditorAutomationWaitResult EditorAutomationWaitResult::success(const Dictionary &p_details) {
	EditorAutomationWaitResult result;
	result.ok = true;
	result.details = p_details;
	return result;
}

EditorAutomationWaitResult EditorAutomationWaitResult::failure(const String &p_kind, const String &p_message, const Dictionary &p_details) {
	EditorAutomationWaitResult result;
	result.ok = false;
	result.kind = p_kind;
	result.message = p_message;
	result.details = p_details;
	return result;
}

Dictionary EditorAutomationWaitResult::to_dictionary() const {
	Dictionary dict;
	dict["ok"] = ok;
	if (!kind.is_empty()) {
		dict["kind"] = kind;
	}
	if (!message.is_empty()) {
		dict["message"] = message;
	}
	if (!details.is_empty()) {
		dict["details"] = details;
	}
	return dict;
}

bool EditorAutomationWait::evaluate_condition_once(
		const Dictionary &p_condition,
		const EditorAutomationSnapshot &p_snapshot,
		const EditorAutomationWaitContext &p_context,
		EditorAutomationWaitResult &r_failure) {
	(void)p_context;
	const WaitConditionKind kind = _parse_condition_kind(p_condition);

	switch (kind) {
		case WaitConditionKind::NEXT_FRAME:
			return false;
		case WaitConditionKind::EDITOR_IDLE:
			return !EditorAutomationTrace::get_singleton().has_pending_actions();
		case WaitConditionKind::SELECTOR_APPEARS:
			return _selector_has_matches(p_snapshot, _read_dictionary(p_condition, "selector"));
		case WaitConditionKind::SELECTOR_DISAPPEARS:
			return !_selector_has_matches(p_snapshot, _read_dictionary(p_condition, "selector"));
		case WaitConditionKind::SELECTOR_MATCHES: {
			const Dictionary selector = _read_dictionary(p_condition, "selector");
			const Dictionary fields = _read_dictionary(p_condition, "fields");
			const EditorAutomationSelectorResult result = EditorAutomationSelector::resolve(p_snapshot, selector);
			if (result.status != EditorAutomationSelectorStatus::OK || result.match_indices.is_empty()) {
				return false;
			}
			for (int match_index : result.match_indices) {
				if (_element_matches_fields(p_snapshot.get_element(match_index), fields)) {
					return true;
				}
			}
			return false;
		}
		case WaitConditionKind::FOCUS_MATCHES:
			return _focus_matches_selector(p_snapshot, _read_dictionary(p_condition, "selector"));
		case WaitConditionKind::MODAL_STACK_CHANGED: {
			const Array baseline = p_condition.has("baseline") ? (Array)p_condition.get("baseline", Array()) : Array();
			const Array current = EditorAutomationState::capture_modal_stack(p_context.snapshot_root);
			return !_modal_stacks_equal(baseline, current);
		}
		case WaitConditionKind::MODAL_STACK_SETTLED:
			return true;
		case WaitConditionKind::FILESYSTEM_IDLE:
			return _is_filesystem_idle();
		case WaitConditionKind::IMPORT_RELOAD_IDLE:
			return _is_import_reload_idle();
		case WaitConditionKind::SCRIPT_ANALYSIS_IDLE:
			r_failure = EditorAutomationWaitResult::failure(
					"unsupported_condition",
					"script_analysis_idle is not observable yet. No central Foundry Script analysis idle API is available.");
			return false;
		case WaitConditionKind::LOG_CONTAINS: {
			const String text = _read_string(p_condition, "text");
			const String severity = _read_string(p_condition, "severity");
			return EditorAutomationLog::contains_text(text, severity);
		}
		case WaitConditionKind::NO_NEW_ERRORS: {
			EditorAutomationLogMarker marker;
			if (p_condition.has("marker")) {
				const Dictionary marker_dict = p_condition.get("marker", Dictionary());
				marker.message_index = int(marker_dict.get("message_index", 0));
			} else {
				marker = EditorAutomationLog::create_marker();
			}
			PackedStringArray severities = _read_string_array(p_condition, "severities");
			if (severities.is_empty()) {
				severities.push_back("error");
				severities.push_back("warning");
			}
			return !EditorAutomationLog::has_new_messages_since(marker, severities);
		}
		case WaitConditionKind::UNSUPPORTED:
			r_failure = EditorAutomationWaitResult::failure(
					"unsupported_condition",
					vformat("Unsupported wait condition '%s'.", _read_string(p_condition, "type")));
			return false;
	}
	return false;
}

EditorAutomationWaitResult EditorAutomationWait::wait_for(
		const Dictionary &p_condition,
		double p_timeout_sec,
		const EditorAutomationWaitContext &p_context) {
	const WaitConditionKind kind = _parse_condition_kind(p_condition);
	if (kind == WaitConditionKind::UNSUPPORTED) {
		return EditorAutomationWaitResult::failure(
				"unsupported_condition",
				vformat("Unsupported wait condition '%s'.", _read_string(p_condition, "type")));
	}
	if (kind == WaitConditionKind::SCRIPT_ANALYSIS_IDLE) {
		return EditorAutomationWaitResult::failure(
				"unsupported_condition",
				"script_analysis_idle is not observable yet. No central Foundry Script analysis idle API is available.");
	}

	if (kind == WaitConditionKind::NEXT_FRAME) {
		_advance_one_frame(p_context.poll_interval_sec);
		Dictionary details;
		details["condition"] = p_condition;
		return EditorAutomationWaitResult::success(details);
	}

	double elapsed = 0.0;
	Array previous_modal_stack;
	bool has_previous_modal_stack = false;
	int settled_frames = 0;
	int processed_frames = 0;

	while (elapsed <= p_timeout_sec) {
		const EditorAutomationSnapshot snapshot = _capture_snapshot(p_context);
		EditorAutomationWaitResult immediate_failure;
		bool satisfied = false;

		switch (kind) {
			case WaitConditionKind::EDITOR_IDLE:
				satisfied = processed_frames >= 1 && !EditorAutomationTrace::get_singleton().has_pending_actions();
				break;
			case WaitConditionKind::MODAL_STACK_CHANGED: {
				const Array baseline = p_condition.has("baseline") ? (Array)p_condition.get("baseline", Array()) : Array();
				const Array current = EditorAutomationState::capture_modal_stack(p_context.snapshot_root);
				satisfied = !_modal_stacks_equal(baseline, current);
				break;
			}
			case WaitConditionKind::MODAL_STACK_SETTLED: {
				const Array current = EditorAutomationState::capture_modal_stack(p_context.snapshot_root);
				if (has_previous_modal_stack && _modal_stacks_equal(previous_modal_stack, current)) {
					settled_frames++;
				} else {
					settled_frames = 0;
				}
				previous_modal_stack = current;
				has_previous_modal_stack = true;
				satisfied = settled_frames >= 1;
				break;
			}
			default:
				satisfied = evaluate_condition_once(p_condition, snapshot, p_context, immediate_failure);
				if (!immediate_failure.kind.is_empty()) {
					return immediate_failure;
				}
				break;
		}

		if (satisfied) {
			Dictionary details;
			details["condition"] = p_condition;
			details["elapsed_sec"] = elapsed;
			if (kind == WaitConditionKind::MODAL_STACK_SETTLED || kind == WaitConditionKind::MODAL_STACK_CHANGED) {
				details["modal_stack"] = EditorAutomationState::capture_modal_stack(p_context.snapshot_root);
			}
			return EditorAutomationWaitResult::success(details);
		}

		_advance_one_frame(p_context.poll_interval_sec);
		processed_frames++;
		elapsed += p_context.poll_interval_sec;
	}

	Dictionary timeout_details;
	timeout_details["condition"] = p_condition;
	timeout_details["elapsed_sec"] = elapsed;
	timeout_details["timeout_sec"] = p_timeout_sec;
	return EditorAutomationWaitResult::failure("timeout", "Timed out waiting for condition.", timeout_details);
}
