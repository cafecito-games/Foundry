/**************************************************************************/
/*  editor_automation_diagnostics.h                                       */
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

struct EditorAutomationDiagnostics {
	String kind;
	String message;
	Dictionary details;

	Dictionary to_dictionary() const;
};

class EditorAutomationDiagnosticsBuilder {
public:
	static Dictionary element_summary(const EditorAutomationElement &p_element);
	static Dictionary element_subtree(const EditorAutomationSnapshot &p_snapshot, const String &p_element_id, int p_depth = 2);
	static Dictionary element_subtree_for_selector(const EditorAutomationSnapshot &p_snapshot, const Dictionary &p_selector, int p_depth = 2);

	static EditorAutomationDiagnostics build_for_action_failure(
			const String &p_kind,
			const String &p_message,
			const Dictionary &p_selector,
			const Array &p_candidates,
			const EditorAutomationSnapshot &p_snapshot,
			const EditorAutomationLogMarker &p_log_marker,
			int p_trace_count = 16);
};
