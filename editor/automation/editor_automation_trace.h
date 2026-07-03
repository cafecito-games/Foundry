/**************************************************************************/
/*  editor_automation_trace.h                                             */
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

#include "core/string/ustring.h"
#include "core/variant/variant.h"

struct EditorAutomationTraceEntry {
	uint64_t timestamp_usec = 0;
	uint64_t frame = 0;
	String action;
	Dictionary selector;
	String element_id;
	String element_summary;
	String route;
	String result_kind;
	bool ok = false;
	int log_marker = 0;
	int error_marker = 0;
};

class EditorAutomationTrace {
	static constexpr int MAX_ENTRIES = 64;

	EditorAutomationTraceEntry entries[MAX_ENTRIES];
	int head = 0;
	int count = 0;
	int pending_actions = 0;

	void _push_entry(const EditorAutomationTraceEntry &p_entry);

public:
	static EditorAutomationTrace &get_singleton();

	void begin_action();
	void end_action();
	bool has_pending_actions() const { return pending_actions > 0; }

	void record_action(
			const String &p_action,
			const Dictionary &p_selector,
			const String &p_element_id,
			const String &p_element_summary,
			const String &p_route,
			const String &p_result_kind,
			bool p_ok,
			int p_log_marker,
			int p_error_marker);

	Array get_recent(int p_count = 16) const;
	void clear();

	static Dictionary entry_to_dictionary(const EditorAutomationTraceEntry &p_entry);
};
