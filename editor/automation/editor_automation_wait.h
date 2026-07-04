/**************************************************************************/
/*  editor_automation_wait.h                                              */
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

#include "editor/automation/editor_automation_log.h"
#include "editor/automation/editor_automation_snapshot.h"

#include "core/variant/variant.h"

class Node;

struct EditorAutomationWaitContext {
	Node *snapshot_root = nullptr;
	double poll_interval_sec = 1.0 / 60.0;
};

struct EditorAutomationWaitResult {
	bool ok = false;
	String kind;
	String message;
	Dictionary details;

	static EditorAutomationWaitResult success(const Dictionary &p_details = Dictionary());
	static EditorAutomationWaitResult failure(const String &p_kind, const String &p_message, const Dictionary &p_details = Dictionary());

	Dictionary to_dictionary() const;
};

struct EditorAutomationActWaitContext {
	bool active = false;
	String action;
	Dictionary selector;
	Dictionary action_result;
	EditorAutomationLogMarker log_marker;
	bool attach_screenshot_on_failure = false;
	int max_screenshot_bytes = 0;
};

enum class EditorAutomationCooperativeWaitStatus {
	PENDING,
	COMPLETE,
	CANCELLED,
};

struct EditorAutomationCooperativeWaitHandle {
	String wait_id;
	EditorAutomationCooperativeWaitStatus status = EditorAutomationCooperativeWaitStatus::PENDING;
	EditorAutomationWaitResult result;
	Dictionary condition;
	double elapsed_sec = 0.0;
	EditorAutomationActWaitContext act_context;
};

class EditorAutomationWait {
public:
	static EditorAutomationWaitResult wait_for(
			const Dictionary &p_condition,
			double p_timeout_sec = 5.0,
			const EditorAutomationWaitContext &p_context = EditorAutomationWaitContext());

	static bool evaluate_condition_once(
			const Dictionary &p_condition,
			const EditorAutomationSnapshot &p_snapshot,
			const EditorAutomationWaitContext &p_context,
			EditorAutomationWaitResult &r_failure,
			int p_processed_frames = 0,
			const Array &p_previous_modal_stack = Array(),
			bool p_has_previous_modal_stack = false,
			int p_settled_frames = 0);

	static String begin_cooperative(
			const Dictionary &p_condition,
			double p_timeout_sec,
			const EditorAutomationWaitContext &p_context,
			const EditorAutomationActWaitContext &p_act_context = EditorAutomationActWaitContext());

	static bool poll_cooperative(const String &p_wait_id, EditorAutomationCooperativeWaitHandle &r_handle);
	static bool cancel_cooperative(const String &p_wait_id, EditorAutomationCooperativeWaitHandle &r_handle);
	static int poll_all_cooperative(int p_max_steps = 1);
	static void clear_all_cooperative();
	static bool has_pending_cooperative();
};
