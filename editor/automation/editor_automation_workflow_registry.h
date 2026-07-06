/**************************************************************************/
/*  editor_automation_workflow_registry.h                                 */
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

#include "editor/automation/editor_automation_acceptance_workflow.h"

#include "core/string/ustring.h"
#include "core/templates/hash_map.h"
#include "core/templates/vector.h"
#include "core/variant/variant.h"

class EditorWorkflowTestDriver;

class EditorAutomationWorkflowRegistry {
public:
	using WorkflowFn = EditorAutomationAcceptanceWorkflow::Result (*)(EditorWorkflowTestDriver &);

	static void register_builtin_workflows();
	static String resolve_canonical_name(const String &p_name);
	static bool has_workflow(const String &p_name);
	static EditorAutomationAcceptanceWorkflow::Result run(const String &p_name, EditorWorkflowTestDriver &p_driver);
	static PackedStringArray list_workflow_names();
	static String format_unknown_workflow_message(const String &p_name);

private:
	struct WorkflowEntry {
		String canonical_name;
		WorkflowFn run = nullptr;
		PackedStringArray aliases;
	};

	static HashMap<String, WorkflowEntry> workflows;
	static HashMap<String, String> alias_to_canonical;
	static bool initialized;

	static void _register_workflow(const String &p_canonical_name, WorkflowFn p_run, const PackedStringArray &p_aliases = PackedStringArray());
	static void _ensure_initialized();
};
