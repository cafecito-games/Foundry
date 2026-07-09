/**************************************************************************/
/*  editor_automation_acceptance_workflow.h                               */
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

#pragma once

#include "core/string/ustring.h"
#include "core/variant/dictionary.h"

class EditorWorkflowTestDriver;

class EditorAutomationAcceptanceWorkflow {
public:
	struct Result {
		bool ok = false;
		String workflow;
		String message;
		Dictionary details;
	};

	// Basic scene-editing smoke workflow exercising scene tree, create dialog,
	// inspector, save, run/stop, and editor-log assertions through EditorWorkflowTestDriver.
	static Result run_basic_scene_editing(EditorWorkflowTestDriver &p_driver, const String &p_scene_path = "res://scenes/main.tscn");
	static Result run_close_last_scene_empty_pane(EditorWorkflowTestDriver &p_driver);
	static Result run_mixed_workspace_editing(EditorWorkflowTestDriver &p_driver);
	static Result run_mixed_workspace_seed(EditorWorkflowTestDriver &p_driver);
	static Result run_mixed_workspace_restore(EditorWorkflowTestDriver &p_driver);
	static Result run_projectless_shell_smoke(EditorWorkflowTestDriver &p_driver);
	static Result run_projectless_shell_dismiss_quits(EditorWorkflowTestDriver &p_driver);
	static Result run_projectless_shell_open_in_process(EditorWorkflowTestDriver &p_driver);
	static Result run_projectless_shell_open_in_process_fallback(EditorWorkflowTestDriver &p_driver);
	static Result run_startup_dialog_projects_tab(EditorWorkflowTestDriver &p_driver);
	static Result run_startup_dialog_manage_tab(EditorWorkflowTestDriver &p_driver);
	static Result run_startup_dialog_about_tab(EditorWorkflowTestDriver &p_driver);

	// Deprecated alias kept for backward compatibility with older CLI/tests.
	static Result run_mvp(EditorWorkflowTestDriver &p_driver, const String &p_scene_path = "res://scenes/main.tscn") {
		return run_basic_scene_editing(p_driver, p_scene_path);
	}

	static void print_result(const Result &p_result);
};
