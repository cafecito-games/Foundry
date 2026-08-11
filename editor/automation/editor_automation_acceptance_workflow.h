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
	static Result run_canvas_2d_zoom_automation(EditorWorkflowTestDriver &p_driver, const String &p_scene_path = "res://scenes/main.tscn");
	static Result run_close_last_scene_empty_pane(EditorWorkflowTestDriver &p_driver);
	static Result run_split_scene_root_button_context(EditorWorkflowTestDriver &p_driver);
	// Real-pointer cross-board drag: grabs a scene tab on the active board and
	// drops it on the tile body of a non-active board shown in the overview.
	static Result run_cross_board_tile_body_drop(EditorWorkflowTestDriver &p_driver);
	// Real-pointer cross-board drag: grabs a scene tab on the active board and
	// drops it on the tab strip of a non-active board's already-populated pane,
	// shown in the overview.
	static Result run_cross_board_tab_strip_drop(EditorWorkflowTestDriver &p_driver);
	// Real-pointer drag that enters a neighboring tile in one uninterrupted
	// gesture, asserting the drop affordance is live before the release.
	static Result run_continuous_drag_arms_drop_overlay(EditorWorkflowTestDriver &p_driver);
	static Result run_mixed_workspace_editing(EditorWorkflowTestDriver &p_driver);
	static Result run_mixed_workspace_seed(EditorWorkflowTestDriver &p_driver);
	static Result run_mixed_workspace_restore(EditorWorkflowTestDriver &p_driver);
	static Result run_tile_local_scene_modes(EditorWorkflowTestDriver &p_driver);
	static Result run_tile_local_scene_modes_restore(EditorWorkflowTestDriver &p_driver);
	// Opens a 3D scene in a board tile and switches boards, which demotes that tile out
	// of focus and makes the editor build a secondary 3D viewport for its live preview.
	static Result run_board_switch_3d_scene(EditorWorkflowTestDriver &p_driver);
#ifdef TESTS_ENABLED
	// Closes a demoted 3D board while proving its EditorSceneContext viewport dies before
	// the deferred board/tile/secondary-viewport teardown, and that preview furniture is
	// held by ObjectID rather than raw address (#2082).
	static Result run_board_close_3d_context_lifetime(EditorWorkflowTestDriver &p_driver);
#endif
	// Splits the active board so a 3D tile is demoted next to a focused sibling, then
	// drives real pointer and key input at both the passive 3D and passive 2D previews.
	static Result run_passive_preview_input_policy(EditorWorkflowTestDriver &p_driver);
	static Result run_projectless_shell_smoke(EditorWorkflowTestDriver &p_driver);
	static Result run_projectless_shell_dismiss_quits(EditorWorkflowTestDriver &p_driver);
	static Result run_projectless_shell_open_in_process(EditorWorkflowTestDriver &p_driver);
	static Result run_projectless_shell_open_in_process_fallback(EditorWorkflowTestDriver &p_driver);
	static Result run_projectless_shell_open_in_process_autoload(EditorWorkflowTestDriver &p_driver);
	static Result run_projectless_shell_open_in_process_debug_options(EditorWorkflowTestDriver &p_driver);
	static Result run_startup_dialog_projects_tab(EditorWorkflowTestDriver &p_driver);
	static Result run_startup_dialog_manage_tab(EditorWorkflowTestDriver &p_driver);
	static Result run_startup_dialog_about_tab(EditorWorkflowTestDriver &p_driver);

	// Deprecated alias kept for backward compatibility with older CLI/tests.
	static Result run_mvp(EditorWorkflowTestDriver &p_driver, const String &p_scene_path = "res://scenes/main.tscn") {
		return run_basic_scene_editing(p_driver, p_scene_path);
	}

	static void print_result(const Result &p_result);
};
