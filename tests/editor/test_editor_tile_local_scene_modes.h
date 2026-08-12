/**************************************************************************/
/*  test_editor_tile_local_scene_modes.h                                  */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             FOUNDRY ENGINE                             */
/*          A fork of the Godot Engine (https://godotengine.org)          */
/*                       https://www.cafecito.games                       */
/**************************************************************************/
/* Copyright (c) 2026-present Cafecito Games LLC.                         */
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

namespace TestEditorTileLocalSceneModes {

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

TEST_CASE("[Editor][SceneModes] two tiles keep independent modes across focus, boards, and restart") {
	const String project_path = EditorWorkflowTestFixtures::prepare_disposable_project();
	REQUIRE_MESSAGE(!project_path.is_empty(), "Failed to prepare a temporary tile-local scene-mode project copy.");

	List<String> arguments;
	arguments.push_back("editor");
	arguments.push_back("open");
	arguments.push_back("--headless");
	arguments.push_back("--project");
	arguments.push_back(project_path);
	arguments.push_back("--automation");
	arguments.push_back("--automation-run-workflow=tile_local_scene_modes");

	int seed_exit_code = -1;
	const String seed_output = EditorWorkflowTestFixtures::workflow_run_subprocess(arguments, seed_exit_code);
	INFO("Seed subprocess output:\n", seed_output);

	Dictionary seed_payload;
	const bool seed_parsed = parse_workflow_payload(seed_output, seed_payload);
	REQUIRE_MESSAGE(seed_parsed, "Seed workflow result line was not printed.");
	if (!seed_parsed) {
		return;
	}
	const bool seed_workflow_matches = String(seed_payload.get("workflow", String())) == "tile_local_scene_modes";
	const bool seed_ok = (bool)seed_payload.get("ok", false);
	CHECK(seed_workflow_matches);
	CHECK(seed_output.contains("\"ok\":true"));
	CHECK_MESSAGE(seed_ok, String(seed_payload.get("message", String())));
	REQUIRE(seed_exit_code == 0);
	if (!seed_workflow_matches || !seed_ok || seed_exit_code != 0) {
		return;
	}

	arguments.back()->get() = "--automation-run-workflow=tile_local_scene_modes_restore";
	int restore_exit_code = -1;
	const String restore_output = EditorWorkflowTestFixtures::workflow_run_subprocess(arguments, restore_exit_code);
	INFO("Restore subprocess output:\n", restore_output);

	Dictionary restore_payload;
	const bool restore_parsed = parse_workflow_payload(restore_output, restore_payload);
	REQUIRE_MESSAGE(restore_parsed, "Restore workflow result line was not printed.");
	if (!restore_parsed) {
		return;
	}
	CHECK(String(restore_payload.get("workflow", String())) == "tile_local_scene_modes_restore");
	CHECK(restore_output.contains("\"ok\":true"));
	CHECK_MESSAGE((bool)restore_payload.get("ok", false), String(restore_payload.get("message", String())));
	CHECK(restore_exit_code == 0);
}

} // namespace TestEditorTileLocalSceneModes
