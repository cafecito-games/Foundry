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
			EditorAutomationWaitResult &r_failure);
};
