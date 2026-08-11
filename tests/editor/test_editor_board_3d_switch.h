/**************************************************************************/
/*  test_editor_board_3d_switch.h                                         */
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

#include "core/io/json.h"

#include "tests/editor/editor_workflow_test_fixtures.h"
#include "tests/test_macros.h"

namespace TestEditorBoard3DSwitch {

static bool parse_workflow_payload(const String &p_output, Dictionary &r_payload) {
	const int marker = p_output.find("FOUNDRY_AUTOMATION_WORKFLOW");
	if (marker < 0) {
		return false;
	}
	const int line_start = marker + String("FOUNDRY_AUTOMATION_WORKFLOW ").length();
	const int line_end = p_output.find_char('\n', line_start);
	const String json_text = line_end >= 0 ? p_output.substr(line_start, line_end - line_start) : p_output.substr(line_start);

	JSON json;
	if (json.parse(json_text.strip_edges()) != OK) {
		return false;
	}
	r_payload = json.get_data();
	return true;
}

// Switching boards while a 3D scene is open used to abort the editor: the demoted tile
// asks for a secondary 3D viewport, and building one dereferenced the view overlay that
// a chrome-less secondary viewport never creates.
//
// The board tests that existed when this shipped all mounted an EditorBoardStrip
// directly, where no 3D editor plugin exists and no secondary viewport is ever built,
// so none of them could observe the abort. This drives a real editor process instead:
// the crash lives in EditorNode's display-attachment update, which only runs there.
//
// The subprocess is launched with --headless and needs no GUI display, so this test is
// not gated on one.
TEST_CASE("[Editor][Boards] Switching boards with a 3D scene open does not crash the editor") {
	const String project_path = EditorWorkflowTestFixtures::prepare_disposable_project();
	REQUIRE_MESSAGE(!project_path.is_empty(), "Failed to prepare a temporary board-switch project copy.");

	List<String> arguments;
	arguments.push_back("editor");
	arguments.push_back("open");
	arguments.push_back("--headless");
	arguments.push_back("--project");
	arguments.push_back(project_path);
	arguments.push_back("--automation");
	arguments.push_back("--automation-run-workflow=board_switch_3d_scene");

	int exit_code = -1;
	const String output = EditorWorkflowTestFixtures::workflow_run_subprocess(arguments, exit_code);
	INFO("Subprocess output:\n", output);

	Dictionary payload;
	// REQUIRE does not abort in this build, so bail out explicitly rather than reading
	// fields off an empty payload below.
	const bool parsed = parse_workflow_payload(output, payload);
	REQUIRE_MESSAGE(parsed, "Workflow result line was not printed; the editor most likely crashed during the board switch.");
	if (!parsed) {
		return;
	}

	CHECK(String(payload.get("workflow", String())) == "board_switch_3d_scene");
	CHECK_MESSAGE((bool)payload.get("ok", false), String(payload.get("message", String())));
	CHECK(exit_code == 0);
}

// Closing a demoted 3D board destroys its EditorSceneContext viewport immediately while the
// board, ScenePaneTile, and secondary Node3DEditorViewport survive until a deferred callback.
// Preview furniture must therefore be held by ObjectID and same-world rebinds must not inflate
// live_view_count (#2082).
TEST_CASE("[Editor][Boards] 3D preview context lifetime survives deferred board close") {
	const String project_path = EditorWorkflowTestFixtures::prepare_disposable_project();
	REQUIRE_MESSAGE(!project_path.is_empty(), "Failed to prepare a temporary board-close lifetime project copy.");

	List<String> arguments;
	arguments.push_back("editor");
	arguments.push_back("open");
	arguments.push_back("--headless");
	arguments.push_back("--project");
	arguments.push_back(project_path);
	arguments.push_back("--automation");
	arguments.push_back("--automation-run-workflow=board_close_3d_context_lifetime");

	int exit_code = -1;
	const String output = EditorWorkflowTestFixtures::workflow_run_subprocess(arguments, exit_code);
	INFO("Subprocess output:\n", output);

	Dictionary payload;
	const bool parsed = parse_workflow_payload(output, payload);
	REQUIRE_MESSAGE(parsed, "Workflow result line was not printed; the editor most likely crashed during deferred board close.");
	if (!parsed) {
		return;
	}

	CHECK(String(payload.get("workflow", String())) == "board_close_3d_context_lifetime");
	CHECK_MESSAGE((bool)payload.get("ok", false), String(payload.get("message", String())));
	CHECK(exit_code == 0);
}

} // namespace TestEditorBoard3DSwitch
