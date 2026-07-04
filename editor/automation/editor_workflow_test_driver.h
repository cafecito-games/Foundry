/**************************************************************************/
/*  editor_workflow_test_driver.h                                         */
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

#pragma once

#include "editor/automation/editor_automation_diagnostics.h"
#include "editor/automation/editor_automation_log.h"

#include "core/variant/variant.h"

class Node;
class EditorAutomationSnapshot;

// Direct test-facing client for editor workflow automation.
//
// Wraps the same snapshot, selector, driver, wait, state, log, and diagnostics
// stack used by EditorAutomationMCPDispatcher without going through MCP.
class EditorWorkflowTestDriver {
public:
	struct Options {
		// When set, observe/find/act/wait operate on this subtree instead of the
		// live editor. Unit tests inject synthetic UI here.
		Node *snapshot_root = nullptr;
		int default_wait_timeout_ms = 30000;
		int max_tree_depth = 8;
		bool attach_screenshot_on_failure = false;
		int max_screenshot_bytes = 512 * 1024;
	};

	struct Failure {
		bool failed = false;
		String step;
		String message;
		String action;
		String route;
		Dictionary selector;
		Dictionary diagnostics;
	};

private:
	Options options;
	EditorAutomationLogMarker log_marker;
	String current_step;
	String last_action;
	String last_route;
	Dictionary last_selector;
	Failure failure;

	EditorAutomationSnapshot _capture_snapshot() const;
	EditorAutomationFailureAttachmentOptions _failure_attachment_options() const;
	void _record_failure(const String &p_kind, const String &p_message, const Dictionary &p_selector, const EditorAutomationSnapshot &p_snapshot, const Array &p_candidates = Array());
	void _record_wait_failure(
			const String &p_kind,
			const String &p_message,
			const Dictionary &p_condition,
			const Dictionary &p_action,
			const Dictionary &p_selector,
			const EditorAutomationSnapshot &p_snapshot);
	bool _check_result(const Dictionary &p_result, const String &p_context, const Dictionary &p_selector, const EditorAutomationSnapshot &p_snapshot);

public:
	void configure(const Options &p_options);
	const Options &get_options() const { return options; }

	void begin_workflow();
	void set_step(const String &p_step);
	const String &get_current_step() const { return current_step; }

	const EditorAutomationLogMarker &get_log_marker() const { return log_marker; }
	const Failure &get_failure() const { return failure; }
	bool has_failed() const { return failure.failed; }

	Dictionary observe_ui(int p_max_depth = -1, bool p_include_hidden = false);
	Dictionary find(const Dictionary &p_selector, int p_max_results = 20);
	Dictionary act(const Dictionary &p_selector, const String &p_action, const Dictionary &p_args = Dictionary(), const String &p_route = String());
	Dictionary wait_for(const Dictionary &p_condition, int p_timeout_ms = -1);
	Dictionary read_editor_state();
	Dictionary read_editor_log(const EditorAutomationLogMarker *p_since = nullptr, const String &p_severity = String(), int p_limit = 64);
	Dictionary run_command(const String &p_command);

	bool assert_no_new_errors(const PackedStringArray &p_severities = PackedStringArray());
	bool require_ok(const Dictionary &p_result, const String &p_context = String());

	String format_failure_report() const;
};
