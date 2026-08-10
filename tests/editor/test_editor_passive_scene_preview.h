/**************************************************************************/
/*  test_editor_passive_scene_preview.h                                   */
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

namespace TestEditorPassiveScenePreview {

// A non-focused scene preview is a passive rendering surface, not an editor. It must
// never take focus, process editor input, mutate view state or scene content, or accept
// a drop; a primary-button gesture on it only promotes its owning tile.
//
// This has to run in a real editor process. The demotion that builds a preview lives in
// EditorNode's display-attachment update, and the property under test is that real
// pointer and key events dispatched through Viewport::push_input reach nothing. A test
// that called a private input handler would prove the opposite of what is wanted: it
// would bypass exactly the wiring whose absence is the feature.
TEST_CASE("[Editor][Boards] Passive scene previews reject editor input and promote on click") {
	if (!EditorWorkflowTestFixtures::workflow_has_display()) {
		MESSAGE("Requires a GUI display. Re-run with DISPLAY set so the editor subprocess starts.");
		return;
	}

	const String project_path = EditorWorkflowTestFixtures::prepare_disposable_project();
	if (project_path.is_empty()) {
		FAIL("Failed to prepare a temporary passive-preview project copy.");
		return;
	}

	// Point the optional gallery dump at scratch space. Under --headless the
	// dummy renderer cannot capture, but requesting dumps must not trip
	// assert_no_new_errors (regression for FOUNDRY_CAPTURE_DIR + headless).
	const String capture_dir = TestUtils::get_temp_path(
			"passive_preview_capture_" + String::num_uint64(OS::get_singleton()->get_ticks_usec()));
	OS::get_singleton()->set_environment("FOUNDRY_CAPTURE_DIR", capture_dir);

	List<String> arguments;
	arguments.push_back("editor");
	arguments.push_back("open");
	arguments.push_back("--headless");
	arguments.push_back("--project");
	arguments.push_back(project_path);
	arguments.push_back("--automation");
	arguments.push_back("--automation-run-workflow=passive_preview_input_policy");

	int exit_code = -1;
	const String output = EditorWorkflowTestFixtures::workflow_run_subprocess(arguments, exit_code);
	OS::get_singleton()->unset_environment("FOUNDRY_CAPTURE_DIR");
	INFO("Subprocess output:\n", output);

	const int marker = output.find("FOUNDRY_AUTOMATION_WORKFLOW");
	// REQUIRE does not abort in this build, so bail out explicitly rather than parsing
	// fields out of output that was never produced.
	CHECK_MESSAGE(marker >= 0, "Workflow result line was not printed; the editor most likely crashed.");
	if (marker < 0) {
		return;
	}

	const int line_start = marker + String("FOUNDRY_AUTOMATION_WORKFLOW ").length();
	const int line_end = output.find_char('\n', line_start);
	const String json_text = line_end >= 0 ? output.substr(line_start, line_end - line_start) : output.substr(line_start);
	JSON json;
	if (json.parse(json_text.strip_edges()) != OK) {
		FAIL("Workflow result line was not valid JSON: ", json_text);
		return;
	}
	const Dictionary payload = json.get_data();

	CHECK(String(payload.get("workflow", String())) == "passive_preview_input_policy");
	CHECK_MESSAGE((bool)payload.get("ok", false), String(payload.get("message", String())));
	CHECK(exit_code == 0);
	CHECK_FALSE(output.contains("Parameter \"t\" is null"));
}

} // namespace TestEditorPassiveScenePreview
