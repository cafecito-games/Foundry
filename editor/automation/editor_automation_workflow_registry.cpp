/**************************************************************************/
/*  editor_automation_workflow_registry.cpp                               */
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

#include "editor_automation_workflow_registry.h"

#include "editor/automation/editor_workflow_test_driver.h"

#include "core/string/ustring.h"

namespace {

EditorAutomationAcceptanceWorkflow::Result _run_basic_scene_editing(EditorWorkflowTestDriver &p_driver) {
	return EditorAutomationAcceptanceWorkflow::run_basic_scene_editing(p_driver);
}

EditorAutomationAcceptanceWorkflow::Result _run_mixed_workspace_editing(EditorWorkflowTestDriver &p_driver) {
	return EditorAutomationAcceptanceWorkflow::run_mixed_workspace_editing(p_driver);
}

EditorAutomationAcceptanceWorkflow::Result _run_mixed_workspace_seed(EditorWorkflowTestDriver &p_driver) {
	return EditorAutomationAcceptanceWorkflow::run_mixed_workspace_seed(p_driver);
}

EditorAutomationAcceptanceWorkflow::Result _run_mixed_workspace_restore(EditorWorkflowTestDriver &p_driver) {
	return EditorAutomationAcceptanceWorkflow::run_mixed_workspace_restore(p_driver);
}

} // namespace

HashMap<String, EditorAutomationWorkflowRegistry::WorkflowEntry> EditorAutomationWorkflowRegistry::workflows;
HashMap<String, String> EditorAutomationWorkflowRegistry::alias_to_canonical;
bool EditorAutomationWorkflowRegistry::initialized = false;

void EditorAutomationWorkflowRegistry::_register_workflow(const String &p_canonical_name, WorkflowFn p_run, const PackedStringArray &p_aliases) {
	WorkflowEntry entry;
	entry.canonical_name = p_canonical_name;
	entry.run = p_run;
	entry.aliases = p_aliases;
	workflows[p_canonical_name] = entry;
	alias_to_canonical[p_canonical_name] = p_canonical_name;
	for (int i = 0; i < p_aliases.size(); i++) {
		alias_to_canonical[p_aliases[i]] = p_canonical_name;
	}
}

void EditorAutomationWorkflowRegistry::_ensure_initialized() {
	if (initialized) {
		return;
	}
	register_builtin_workflows();
	initialized = true;
}

void EditorAutomationWorkflowRegistry::register_builtin_workflows() {
	workflows.clear();
	alias_to_canonical.clear();

	PackedStringArray basic_scene_aliases;
	basic_scene_aliases.push_back("mvp");
	_register_workflow("basic_scene_editing", &_run_basic_scene_editing, basic_scene_aliases);
	_register_workflow("mixed_workspace_editing", &_run_mixed_workspace_editing);
	_register_workflow("mixed_workspace_seed", &_run_mixed_workspace_seed);
	_register_workflow("mixed_workspace_restore", &_run_mixed_workspace_restore);
}

String EditorAutomationWorkflowRegistry::resolve_canonical_name(const String &p_name) {
	_ensure_initialized();
	const String *canonical = alias_to_canonical.getptr(p_name);
	if (canonical != nullptr) {
		return *canonical;
	}
	return String();
}

bool EditorAutomationWorkflowRegistry::has_workflow(const String &p_name) {
	return !resolve_canonical_name(p_name).is_empty();
}

EditorAutomationAcceptanceWorkflow::Result EditorAutomationWorkflowRegistry::run(const String &p_name, EditorWorkflowTestDriver &p_driver) {
	_ensure_initialized();
	const String canonical = resolve_canonical_name(p_name);
	if (canonical.is_empty()) {
		EditorAutomationAcceptanceWorkflow::Result result;
		result.ok = false;
		result.workflow = p_name;
		result.message = format_unknown_workflow_message(p_name);
		return result;
	}

	const WorkflowEntry *entry = workflows.getptr(canonical);
	ERR_FAIL_NULL_V(entry, EditorAutomationAcceptanceWorkflow::Result());
	ERR_FAIL_NULL_V(entry->run, EditorAutomationAcceptanceWorkflow::Result());
	return entry->run(p_driver);
}

PackedStringArray EditorAutomationWorkflowRegistry::list_workflow_names() {
	_ensure_initialized();
	PackedStringArray names;
	for (const KeyValue<String, WorkflowEntry> &pair : workflows) {
		names.push_back(pair.key);
	}
	names.sort();
	return names;
}

String EditorAutomationWorkflowRegistry::format_unknown_workflow_message(const String &p_name) {
	_ensure_initialized();
	const PackedStringArray names = list_workflow_names();
	String message = vformat("Unknown automation workflow '%s'.", p_name);
	if (!names.is_empty()) {
		message += " Known workflows: " + String(", ").join(names) + ".";
	}
	return message;
}
