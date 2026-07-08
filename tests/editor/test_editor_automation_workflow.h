/**************************************************************************/
/*  test_editor_automation_workflow.h                                     */
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
#include "editor/automation/editor_automation_mcp_dispatcher.h"
#include "editor/automation/editor_automation_mcp_server.h"
#include "editor/automation/editor_automation_selector.h"
#include "editor/automation/editor_automation_snapshot.h"
#include "editor/automation/editor_automation_state.h"
#include "editor/automation/editor_automation_trace.h"
#include "editor/automation/editor_automation_workflow.h"
#include "editor/automation/editor_automation_workflow_registry.h"
#include "editor/automation/editor_workflow_test_driver.h"
#include "editor/docks/editor_dock.h"
#include "editor/scene/scene_tree_editor.h"

#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/io/json.h"
#include "core/io/stream_peer_tcp.h"
#include "core/object/message_queue.h"
#include "core/os/os.h"
#include "scene/gui/button.h"
#include "scene/gui/line_edit.h"
#include "scene/gui/panel_container.h"
#include "scene/gui/tree.h"
#include "scene/main/window.h"

#include "tests/editor/editor_workflow_test_fixtures.h"
#include "tests/test_macros.h"
#include "tests/test_utils.h"

namespace TestEditorAutomationWorkflow {

static Dictionary make_request(const Variant &p_id, const String &p_method, const Dictionary &p_params = Dictionary()) {
	Dictionary request;
	request["jsonrpc"] = "2.0";
	request["id"] = p_id;
	request["method"] = p_method;
	if (!p_params.is_empty()) {
		request["params"] = p_params;
	}
	return request;
}

static const EditorAutomationElement *find_element_by_role_and_name(const EditorAutomationSnapshot &p_snapshot, const String &p_role, const String &p_name) {
	for (int i = 0; i < p_snapshot.get_element_count(); i++) {
		const EditorAutomationElement &element = p_snapshot.get_element(i);
		if (element.role == p_role && element.name == p_name) {
			return &element;
		}
	}
	return nullptr;
}

TEST_CASE("[Editor][Automation] named dock roots appear in semantic snapshots") {
	Window *window = memnew(Window);
	SceneTree::get_singleton()->get_root()->add_child(window);

	EditorDock *scene_dock = memnew(EditorDock);
	scene_dock->set_name("Scene");
	EditorDock *inspector_dock = memnew(EditorDock);
	inspector_dock->set_name("Inspector");
	EditorDock *filesystem_dock = memnew(EditorDock);
	filesystem_dock->set_name("FileSystem");

	Button *run_button = memnew(Button);
	run_button->set_accessibility_name("Run Project");

	window->add_child(scene_dock);
	window->add_child(inspector_dock);
	window->add_child(filesystem_dock);
	window->add_child(run_button);

	const EditorAutomationSnapshot snapshot = EditorAutomationSnapshot::capture_from_node(window);

	const EditorAutomationElement *scene = find_element_by_role_and_name(snapshot, "dock", "Scene");
	REQUIRE(scene != nullptr);
	CHECK(String(scene->metadata.get("dock_title", String())) == "Scene");

	const EditorAutomationElement *inspector = find_element_by_role_and_name(snapshot, "dock", "Inspector");
	REQUIRE(inspector != nullptr);

	const EditorAutomationElement *filesystem = find_element_by_role_and_name(snapshot, "dock", "FileSystem");
	REQUIRE(filesystem != nullptr);

	const EditorAutomationElement *run_project = find_element_by_role_and_name(snapshot, "button", "Run Project");
	REQUIRE(run_project != nullptr);

	window->queue_free();
}

TEST_CASE("[Editor][Automation] scene tree item metadata includes node path and class") {
	Window *window = memnew(Window);
	SceneTree::get_singleton()->get_root()->add_child(window);

	SceneTreeEditor *scene_tree_editor = memnew(SceneTreeEditor(false, false, false));
	window->add_child(scene_tree_editor);

	Node *child = memnew(Node);
	child->set_name("ChildNode");
	scene_tree_editor->add_child(child);

	Tree *tree = scene_tree_editor->get_scene_tree();
	TreeItem *root_item = tree->create_item();
	TreeItem *child_item = tree->create_item(root_item);
	child_item->set_text(0, "ChildNode");
	child_item->set_metadata(0, NodePath("ChildNode"));
	MessageQueue::get_singleton()->flush();

	const Dictionary metadata = EditorAutomationWorkflow::metadata_for_tree_item(tree, child_item);
	CHECK(String(metadata.get("node_name", String())) == "ChildNode");
	CHECK(String(metadata.get("node_class", String())) == "Node");
	CHECK(NodePath(metadata.get("node_path", NodePath())) == NodePath("ChildNode"));
	CHECK(String(metadata.get("label", String())) == "ChildNode");
	CHECK((bool)metadata.get("selected", true) == false);
	CHECK(metadata.has("supported_actions"));
	CHECK(PackedStringArray(metadata.get("supported_actions", PackedStringArray())).has("activate"));

	const EditorAutomationSnapshot snapshot = EditorAutomationSnapshot::capture_from_node(window);
	const EditorAutomationElement *child_element = nullptr;
	for (int i = 0; i < snapshot.get_element_count(); i++) {
		const EditorAutomationElement &element = snapshot.get_element(i);
		if (element.role == "tree_item" && element.name == "ChildNode") {
			child_element = &element;
			break;
		}
	}
	REQUIRE(child_element != nullptr);
	CHECK(NodePath(child_element->metadata.get("node_path", NodePath())) == NodePath("ChildNode"));
	CHECK(child_element->actions.has("activate"));
	CHECK(child_element->actions.has("select"));

	Dictionary selector;
	selector["role"] = "tree_item";
	Dictionary metadata_selector;
	metadata_selector["node_path"] = NodePath("ChildNode");
	selector["metadata"] = metadata_selector;
	Dictionary within;
	within["role"] = "tree";
	selector["within"] = within;
	const EditorAutomationSelectorResult selector_result = EditorAutomationSelector::resolve(snapshot, selector);
	CHECK(selector_result.status == EditorAutomationSelectorStatus::OK);

	window->queue_free();
}

TEST_CASE("[Editor][Automation] editor state readback includes filesystem keys") {
	const Dictionary state = EditorAutomationState::read_editor_state();
	CHECK(state.has("filesystem"));
	const Dictionary filesystem = state.get("filesystem", Dictionary());
	CHECK(filesystem.has("supported"));
}

TEST_CASE("[Editor][Automation][MCP] run_command rejects unknown command") {
	EditorAutomationMCPDispatcher dispatcher;
	Dictionary params;
	params["name"] = "run_command";
	Dictionary arguments;
	arguments["command"] = "editor/nonexistent_test_command";
	params["arguments"] = arguments;

	const Dictionary response = dispatcher.handle_message(make_request(99, "tools/call", params));
	CHECK(response.has("result"));

	const Dictionary result = response["result"];
	CHECK((bool)result["isError"]);
	const Dictionary structured = result["structuredContent"];
	CHECK((bool)structured.get("ok", true) == false);
	CHECK(String(structured.get("kind", String())) == "unknown_command");
	CHECK(structured.has("suggestions"));
	CHECK(structured.has("candidates"));
}

static void workflow_flush_frames(int p_count = 1) {
	for (int i = 0; i < p_count; i++) {
		SceneTree::get_singleton()->process(1.0 / 60.0);
		MessageQueue::get_singleton()->flush();
	}
}

static String workflow_fixture_project_path() {
	return EditorWorkflowTestFixtures::fixture_project_path("editor_automation_mvp");
}

static String workflow_prepare_temp_project() {
	return EditorWorkflowTestFixtures::prepare_basic_scene_project();
}

static String workflow_prepare_disposable_project() {
	return EditorWorkflowTestFixtures::prepare_disposable_project();
}

static bool workflow_has_display() {
	return EditorWorkflowTestFixtures::workflow_has_display();
}

static String workflow_run_subprocess(const List<String> &p_arguments, int &r_exit_code) {
	return EditorWorkflowTestFixtures::workflow_run_subprocess(p_arguments, r_exit_code);
}

static bool workflow_parse_result_payload(const String &p_output, Dictionary &r_payload) {
	if (!p_output.contains("FOUNDRY_AUTOMATION_WORKFLOW")) {
		return false;
	}
	const int line_start = p_output.find("FOUNDRY_AUTOMATION_WORKFLOW") + String("FOUNDRY_AUTOMATION_WORKFLOW ").length();
	const int line_end = p_output.find_char('\n', line_start);
	const String json_text = line_end >= 0 ? p_output.substr(line_start, line_end - line_start) : p_output.substr(line_start);

	JSON json;
	if (json.parse(json_text.strip_edges()) != OK) {
		return false;
	}
	r_payload = json.get_data();
	return true;
}

TEST_CASE("[Editor][Automation] workflow registry resolves canonical names and aliases") {
	EditorAutomationWorkflowRegistry::register_builtin_workflows();
	CHECK(EditorAutomationWorkflowRegistry::has_workflow("basic_scene_editing"));
	CHECK(EditorAutomationWorkflowRegistry::has_workflow("mvp"));
	CHECK(EditorAutomationWorkflowRegistry::has_workflow("close_last_scene_empty_pane"));
	CHECK(EditorAutomationWorkflowRegistry::resolve_canonical_name("mvp") == "basic_scene_editing");
	CHECK(EditorAutomationWorkflowRegistry::resolve_canonical_name("basic_scene_editing") == "basic_scene_editing");
	CHECK_FALSE(EditorAutomationWorkflowRegistry::has_workflow("does_not_exist"));

	const PackedStringArray names = EditorAutomationWorkflowRegistry::list_workflow_names();
	CHECK(names.has("basic_scene_editing"));
	CHECK(names.has("close_last_scene_empty_pane"));
	CHECK(EditorAutomationWorkflowRegistry::format_unknown_workflow_message("missing").contains("basic_scene_editing"));
}

TEST_CASE("[Editor][EditorAutomation] mixed-workspace workflow registry") {
	EditorAutomationWorkflowRegistry::register_builtin_workflows();
	CHECK(EditorAutomationWorkflowRegistry::has_workflow("mixed_workspace_editing"));
	CHECK(EditorAutomationWorkflowRegistry::has_workflow("mixed_workspace_seed"));
	CHECK(EditorAutomationWorkflowRegistry::has_workflow("mixed_workspace_restore"));

	const PackedStringArray names = EditorAutomationWorkflowRegistry::list_workflow_names();
	CHECK(names.has("mixed_workspace_editing"));
	CHECK(names.has("mixed_workspace_seed"));
	CHECK(names.has("mixed_workspace_restore"));
	CHECK(EditorAutomationWorkflowRegistry::format_unknown_workflow_message("missing").contains("mixed_workspace_editing"));
}

TEST_CASE("[Editor][Automation] disposable workflow project helper copies multi-scene assets") {
	const String project_path = EditorWorkflowTestFixtures::prepare_disposable_project();
	REQUIRE_FALSE(project_path.is_empty());
	CHECK(FileAccess::exists(project_path.path_join("project.foundry")));
	CHECK(FileAccess::exists(project_path.path_join("scenes/main.tscn")));
	CHECK(FileAccess::exists(project_path.path_join("scenes/secondary.tscn")));
	CHECK(FileAccess::exists(project_path.path_join("scripts/player.fs")));
	CHECK(FileAccess::exists(project_path.path_join("layout/editor_layout.cfg")));
}

TEST_CASE("[Editor][EditorAutomation] workflow test driver wraps the shared automation core") {
	EditorAutomationTrace::get_singleton().clear();
	EditorAutomationLog::clear_test_messages();

	PanelContainer *root = memnew(PanelContainer);
	root->set_size(Size2(400, 300));
	SceneTree::get_singleton()->get_root()->add_child(root);

	Button *button = memnew(Button);
	button->set_text("Workflow Click");
	button->set_anchors_and_offsets_preset(Control::PRESET_TOP_LEFT);
	button->set_size(Size2(120, 32));
	root->add_child(button);
	workflow_flush_frames();

	EditorWorkflowTestDriver driver;
	EditorWorkflowTestDriver::Options options;
	options.snapshot_root = root;
	driver.configure(options);
	driver.begin_workflow();
	driver.set_step("click_button");

	Dictionary selector;
	selector["role"] = "button";
	selector["name"] = "Workflow Click";

	const Dictionary observe = driver.observe_ui();
	CHECK((bool)observe["ok"]);
	CHECK((int)observe["element_count"] >= 2);

	const Dictionary found = driver.find(selector);
	CHECK((bool)found["ok"]);
	CHECK((int)found["match_count"] == 1);

	const Dictionary acted = driver.act(selector, "click");
	CHECK(driver.require_ok(acted, "click"));
	CHECK((bool)acted["ok"]);
	CHECK(String(acted["route"]) == "semantic_click");

	const Dictionary state = driver.read_editor_state();
	CHECK(state.has("supported"));
	CHECK(state.has("inspector"));
	CHECK(state.has("undo_redo"));
	CHECK(state.has("view_2d"));
	CHECK(state.has("view_3d"));

	const Dictionary log = driver.read_editor_log();
	CHECK((bool)log["ok"]);
	CHECK(driver.assert_no_new_errors());

	memdelete(root);
	EditorAutomationTrace::get_singleton().clear();
	EditorAutomationLog::clear_test_messages();
}

TEST_CASE("[Editor][EditorAutomation] workflow test driver records structured diagnostics on failure") {
	PanelContainer *root = memnew(PanelContainer);
	root->set_size(Size2(200, 120));
	SceneTree::get_singleton()->get_root()->add_child(root);
	workflow_flush_frames();

	EditorWorkflowTestDriver driver;
	EditorWorkflowTestDriver::Options options;
	options.snapshot_root = root;
	driver.configure(options);
	driver.begin_workflow();
	driver.set_step("missing_button");

	Dictionary selector;
	selector["role"] = "button";
	selector["name"] = "Does Not Exist";
	const Dictionary acted = driver.act(selector, "click");
	CHECK_FALSE((bool)acted["ok"]);
	CHECK(driver.has_failed());
	CHECK(driver.get_failure().step == "missing_button");
	CHECK_FALSE(driver.format_failure_report().is_empty());
	const Dictionary diagnostics = driver.get_failure().diagnostics;
	CHECK(diagnostics.has("kind"));
	CHECK(diagnostics.has("details"));

	memdelete(root);
}

TEST_CASE("[Editor][EditorAutomation] basic scene-editing workflow subprocess") {
	if (!workflow_has_display()) {
		MESSAGE("Requires a GUI display. Run with DISPLAY=:1 ./bin/foundry.linuxbsd.editor.dev.x86_64 editor open --project <project> --automation --automation-run-workflow=basic_scene_editing");
		return;
	}

	const String fixture_path = workflow_fixture_project_path();
	CHECK_MESSAGE(DirAccess::exists(fixture_path), "Fixture project missing at ", fixture_path);
	CHECK_MESSAGE(FileAccess::exists(fixture_path.path_join("project.foundry")), "Fixture is missing project.foundry");

	const String project_path = workflow_prepare_temp_project();
	REQUIRE_MESSAGE(!project_path.is_empty(), "Failed to prepare a temporary workflow project copy.");

	List<String> arguments;
	arguments.push_back("editor");
	arguments.push_back("open");
	arguments.push_back("--headless");
	arguments.push_back("--project");
	arguments.push_back(project_path);
	arguments.push_back("--automation");
	arguments.push_back("--automation-run-workflow=basic_scene_editing");

	int exit_code = -1;
	const String output = workflow_run_subprocess(arguments, exit_code);
	INFO("Subprocess output:\n", output);
	CHECK_MESSAGE(output.contains("FOUNDRY_AUTOMATION_WORKFLOW"), "Workflow result line was not printed.");
	CHECK(output.contains("FOUNDRY_AUTOMATION"));
	CHECK(output.contains("\"transport\":\"none\""));

	JSON json;
	if (output.contains("FOUNDRY_AUTOMATION_WORKFLOW")) {
		const int line_start = output.find("FOUNDRY_AUTOMATION_WORKFLOW") + String("FOUNDRY_AUTOMATION_WORKFLOW ").length();
		const int line_end = output.find_char('\n', line_start);
		const String json_text = line_end >= 0 ? output.substr(line_start, line_end - line_start) : output.substr(line_start);
		REQUIRE(json.parse(json_text.strip_edges()) == OK);
		const Dictionary payload = json.get_data();
		CHECK(String(payload.get("workflow", String())) == "basic_scene_editing");
		CHECK((bool)payload.get("ok", false));
	}

	CHECK(exit_code == 0);
}

TEST_CASE("[Editor][EditorAutomation] mvp workflow alias subprocess") {
	if (!workflow_has_display()) {
		MESSAGE("Requires a GUI display. Run with DISPLAY=:1 ./bin/foundry.linuxbsd.editor.dev.x86_64 editor open --project <project> --automation --automation-run-workflow=mvp");
		return;
	}

	const String project_path = workflow_prepare_temp_project();
	REQUIRE_MESSAGE(!project_path.is_empty(), "Failed to prepare a temporary workflow project copy.");

	List<String> arguments;
	arguments.push_back("editor");
	arguments.push_back("open");
	arguments.push_back("--headless");
	arguments.push_back("--project");
	arguments.push_back(project_path);
	arguments.push_back("--automation");
	arguments.push_back("--automation-run-workflow=mvp");

	int exit_code = -1;
	const String output = workflow_run_subprocess(arguments, exit_code);
	INFO("Subprocess output:\n", output);
	CHECK(output.contains("FOUNDRY_AUTOMATION_WORKFLOW"));

	JSON json;
	if (output.contains("FOUNDRY_AUTOMATION_WORKFLOW")) {
		const int line_start = output.find("FOUNDRY_AUTOMATION_WORKFLOW") + String("FOUNDRY_AUTOMATION_WORKFLOW ").length();
		const int line_end = output.find_char('\n', line_start);
		const String json_text = line_end >= 0 ? output.substr(line_start, line_end - line_start) : output.substr(line_start);
		REQUIRE(json.parse(json_text.strip_edges()) == OK);
		const Dictionary payload = json.get_data();
		CHECK(String(payload.get("workflow", String())) == "basic_scene_editing");
		CHECK((bool)payload.get("ok", false));
	}

	CHECK(exit_code == 0);
}

TEST_CASE("[Editor][EditorAutomation] close-last-scene-empty-pane workflow subprocess") {
	if (!workflow_has_display()) {
		MESSAGE("Requires a GUI display. Run with DISPLAY=:1 ./bin/foundry.linuxbsd.editor.dev.x86_64 editor open --project <project> res://scenes/main.tscn --automation --automation-run-workflow=close_last_scene_empty_pane");
		return;
	}

	const String project_path = workflow_prepare_temp_project();
	REQUIRE_MESSAGE(!project_path.is_empty(), "Failed to prepare a temporary workflow project copy.");

	List<String> arguments;
	arguments.push_back("editor");
	arguments.push_back("open");
	arguments.push_back("--headless");
	arguments.push_back("--project");
	arguments.push_back(project_path);
	arguments.push_back("res://scenes/main.tscn");
	arguments.push_back("--automation");
	arguments.push_back("--automation-run-workflow=close_last_scene_empty_pane");

	int exit_code = -1;
	const String output = workflow_run_subprocess(arguments, exit_code);
	INFO("Subprocess output:\n", output);
	CHECK_MESSAGE(output.contains("FOUNDRY_AUTOMATION_WORKFLOW"), "Workflow result line was not printed.");
	CHECK(output.contains("FOUNDRY_AUTOMATION"));
	CHECK(output.contains("\"transport\":\"none\""));

	Dictionary payload;
	REQUIRE(workflow_parse_result_payload(output, payload));
	CHECK(String(payload.get("workflow", String())) == "close_last_scene_empty_pane");
	CHECK((bool)payload.get("ok", false));
	CHECK(exit_code == 0);
}

TEST_CASE("[Editor][EditorAutomation] mixed-workspace-editing workflow subprocess") {
	if (!workflow_has_display()) {
		MESSAGE("Requires a GUI display. Run with DISPLAY=:1 ./bin/foundry.linuxbsd.editor.dev.x86_64 editor open --project <project> --automation --automation-run-workflow=mixed_workspace_editing");
		return;
	}

	const String project_path = workflow_prepare_disposable_project();
	REQUIRE_MESSAGE(!project_path.is_empty(), "Failed to prepare a temporary mixed-workspace project copy.");

	List<String> arguments;
	arguments.push_back("editor");
	arguments.push_back("open");
	arguments.push_back("--headless");
	arguments.push_back("--project");
	arguments.push_back(project_path);
	arguments.push_back("--automation");
	arguments.push_back("--automation-run-workflow=mixed_workspace_editing");

	int exit_code = -1;
	const String output = workflow_run_subprocess(arguments, exit_code);
	INFO("Subprocess output:\n", output);
	CHECK_MESSAGE(output.contains("FOUNDRY_AUTOMATION_WORKFLOW"), "Workflow result line was not printed.");
	CHECK(output.contains("FOUNDRY_AUTOMATION"));
	CHECK(output.contains("\"transport\":\"none\""));

	Dictionary payload;
	REQUIRE(workflow_parse_result_payload(output, payload));
	CHECK(String(payload.get("workflow", String())) == "mixed_workspace_editing");
	CHECK((bool)payload.get("ok", false));
	CHECK(exit_code == 0);
}

TEST_CASE("[Editor][EditorAutomation] mixed-workspace-restart-restore subprocess") {
	if (!workflow_has_display()) {
		MESSAGE("Requires a GUI display. Run with DISPLAY=:1 ./bin/foundry.linuxbsd.editor.dev.x86_64 editor open --project <project> --automation --automation-run-workflow=mixed_workspace_seed");
		return;
	}

	const String project_path = workflow_prepare_disposable_project();
	REQUIRE_MESSAGE(!project_path.is_empty(), "Failed to prepare a temporary mixed-workspace project copy.");

	List<String> seed_arguments;
	seed_arguments.push_back("editor");
	seed_arguments.push_back("open");
	seed_arguments.push_back("--headless");
	seed_arguments.push_back("--project");
	seed_arguments.push_back(project_path);
	seed_arguments.push_back("--automation");
	seed_arguments.push_back("--automation-run-workflow=mixed_workspace_seed");

	int seed_exit_code = -1;
	const String seed_output = workflow_run_subprocess(seed_arguments, seed_exit_code);
	INFO("Seed subprocess output:\n", seed_output);
	CHECK(seed_output.contains("FOUNDRY_AUTOMATION_WORKFLOW"));
	CHECK(seed_output.contains("FOUNDRY_AUTOMATION"));
	CHECK(seed_output.contains("\"transport\":\"none\""));

	Dictionary seed_payload;
	REQUIRE(workflow_parse_result_payload(seed_output, seed_payload));
	CHECK(String(seed_payload.get("workflow", String())) == "mixed_workspace_seed");
	CHECK((bool)seed_payload.get("ok", false));
	REQUIRE(seed_exit_code == 0);

	List<String> restore_arguments;
	restore_arguments.push_back("editor");
	restore_arguments.push_back("open");
	restore_arguments.push_back("--headless");
	restore_arguments.push_back("--project");
	restore_arguments.push_back(project_path);
	restore_arguments.push_back("--automation");
	restore_arguments.push_back("--automation-run-workflow=mixed_workspace_restore");

	int restore_exit_code = -1;
	const String restore_output = workflow_run_subprocess(restore_arguments, restore_exit_code);
	INFO("Restore subprocess output:\n", restore_output);
	CHECK(restore_output.contains("FOUNDRY_AUTOMATION_WORKFLOW"));
	CHECK(restore_output.contains("FOUNDRY_AUTOMATION"));
	CHECK(restore_output.contains("\"transport\":\"none\""));

	Dictionary restore_payload;
	REQUIRE(workflow_parse_result_payload(restore_output, restore_payload));
	CHECK(String(restore_payload.get("workflow", String())) == "mixed_workspace_restore");
	CHECK((bool)restore_payload.get("ok", false));
	CHECK(restore_exit_code == 0);
}

TEST_CASE("[Editor][EditorAutomation] unknown workflow subprocess exits with guidance") {
	if (!workflow_has_display()) {
		return;
	}

	const String project_path = workflow_prepare_temp_project();
	REQUIRE_MESSAGE(!project_path.is_empty(), "Failed to prepare a temporary workflow project copy.");

	List<String> arguments;
	arguments.push_back("editor");
	arguments.push_back("open");
	arguments.push_back("--headless");
	arguments.push_back("--project");
	arguments.push_back(project_path);
	arguments.push_back("--automation");
	arguments.push_back("--automation-run-workflow=not_a_real_workflow");

	int exit_code = -1;
	const String output = workflow_run_subprocess(arguments, exit_code);
	INFO("Subprocess output:\n", output);
	CHECK(output.contains("FOUNDRY_AUTOMATION_WORKFLOW"));
	CHECK(output.contains("Unknown automation workflow"));
	CHECK(output.contains("basic_scene_editing"));
	CHECK(exit_code != 0);
}

static int workflow_reserve_local_port() {
	EditorAutomationMCPServer blocker;
	blocker.set_token("port-blocker");
	if (blocker.listen(0, IPAddress("127.0.0.1")) != OK) {
		return -1;
	}
	const int port = blocker.get_port();
	blocker.stop();
	return port;
}

static bool workflow_wait_for_output_line(const Ref<FileAccess> &p_stdout_pipe, const String &p_marker, String &r_output, const OS::ProcessID p_pid, uint64_t p_timeout_msec) {
	const uint64_t deadline = OS::get_singleton()->get_ticks_msec() + p_timeout_msec;
	while (OS::get_singleton()->get_ticks_msec() < deadline) {
		if (p_stdout_pipe.is_valid() && p_stdout_pipe->is_open()) {
			const uint64_t available = p_stdout_pipe->get_length();
			if (available > 0) {
				Vector<uint8_t> chunk;
				chunk.resize(available);
				const uint64_t read = p_stdout_pipe->get_buffer(chunk.ptrw(), available);
				if (read > 0) {
					r_output += String::utf8((const char *)chunk.ptr(), read);
				}
			}
		}
		if (r_output.contains(p_marker)) {
			return true;
		}
		if (!OS::get_singleton()->is_process_running(p_pid)) {
			return r_output.contains(p_marker);
		}
		OS::get_singleton()->delay_usec(20000);
	}
	return r_output.contains(p_marker);
}

TEST_CASE("[Editor][EditorAutomation] explicit automation port conflict subprocess") {
	if (!workflow_has_display()) {
		MESSAGE("Requires a GUI display. Run an editor against an occupied --automation-port to verify the conflict error.");
		return;
	}

	const int reserved_port = workflow_reserve_local_port();
	REQUIRE_MESSAGE(reserved_port > 0, "Failed to reserve a local automation port.");

	EditorAutomationMCPServer blocker;
	blocker.set_token("port-blocker");
	REQUIRE(blocker.listen(reserved_port, IPAddress("127.0.0.1"), false) == OK);

	const String project_path = workflow_prepare_temp_project();
	REQUIRE_MESSAGE(!project_path.is_empty(), "Failed to prepare a temporary MVP project copy.");

	List<String> arguments;
	arguments.push_back("editor");
	arguments.push_back("open");
	arguments.push_back("--headless");
	arguments.push_back("--project");
	arguments.push_back(project_path);
	arguments.push_back("--automation");
	arguments.push_back("--automation-port");
	arguments.push_back(String::num_int64(reserved_port));
	arguments.push_back("--automation-token");
	arguments.push_back("conflict-token");

	int exit_code = -1;
	const String output = workflow_run_subprocess(arguments, exit_code);
	blocker.stop();

	INFO("Editor output:\n", output);
	CHECK(output.contains("FOUNDRY_AUTOMATION_ERROR"));
	CHECK(output.contains("already in use"));
	CHECK(exit_code != 0);
}

TEST_CASE("[Editor][EditorAutomation] rapid relaunch reuses released automation port") {
	if (!workflow_has_display()) {
		MESSAGE("Requires a GUI display. Relaunch the editor on the same automation port after shutdown.");
		return;
	}

	const int reserved_port = workflow_reserve_local_port();
	REQUIRE_MESSAGE(reserved_port > 0, "Failed to reserve a local automation port.");

	const String project_path = workflow_prepare_temp_project();
	REQUIRE_MESSAGE(!project_path.is_empty(), "Failed to prepare a temporary MVP project copy.");

	List<String> arguments;
	arguments.push_back("editor");
	arguments.push_back("open");
	arguments.push_back("--headless");
	arguments.push_back("--project");
	arguments.push_back(project_path);
	arguments.push_back("--automation");
	arguments.push_back("--automation-port");
	arguments.push_back(String::num_int64(reserved_port));
	arguments.push_back("--automation-token");
	arguments.push_back("relaunch-a");

	Dictionary environment;
	environment["DISPLAY"] = OS::get_singleton()->get_environment("DISPLAY");
	Dictionary first_pipe = OS::get_singleton()->execute_with_pipe(
			OS::get_singleton()->get_executable_path(), arguments, false, String(), environment, false);
	REQUIRE_FALSE(first_pipe.is_empty());

	Ref<FileAccess> first_stdout = first_pipe["stdio"];
	const OS::ProcessID first_pid = first_pipe["pid"];
	String first_output;
	REQUIRE(workflow_wait_for_output_line(first_stdout, "FOUNDRY_AUTOMATION", first_output, first_pid, 120000));

	if (first_stdout.is_valid()) {
		first_stdout->close();
	}
	OS::get_singleton()->kill(first_pid);
	OS::get_singleton()->delay_usec(250000);

	arguments.clear();
	arguments.push_back("editor");
	arguments.push_back("open");
	arguments.push_back("--headless");
	arguments.push_back("--project");
	arguments.push_back(project_path);
	arguments.push_back("--automation");
	arguments.push_back("--automation-port");
	arguments.push_back(String::num_int64(reserved_port));
	arguments.push_back("--automation-token");
	arguments.push_back("relaunch-b");

	Dictionary second_pipe = OS::get_singleton()->execute_with_pipe(
			OS::get_singleton()->get_executable_path(), arguments, false, String(), environment, false);
	REQUIRE_FALSE(second_pipe.is_empty());

	Ref<FileAccess> second_stdout = second_pipe["stdio"];
	const OS::ProcessID second_pid = second_pipe["pid"];
	String second_output;
	REQUIRE(workflow_wait_for_output_line(second_stdout, "FOUNDRY_AUTOMATION", second_output, second_pid, 120000));
	INFO("Second launch output:\n", second_output);
	CHECK_FALSE(second_output.contains("FOUNDRY_AUTOMATION_ERROR"));

	if (second_stdout.is_valid()) {
		second_stdout->close();
	}
	OS::get_singleton()->kill(second_pid);
}

TEST_CASE("[Editor][EditorAutomation][MCP] launched editor smoke handshake") {
	if (!workflow_has_display()) {
		MESSAGE("Requires a GUI display. Run with DISPLAY=:1 ./bin/foundry.linuxbsd.editor.dev.x86_64 editor open --project <project> --automation --automation-transport=mcp --automation-port 0");
		return;
	}

	const String fixture_path = workflow_fixture_project_path();
	CHECK_MESSAGE(DirAccess::exists(fixture_path), "Fixture project missing at ", fixture_path);

	// Use a disposable copy so opening the project does not leave editor caches
	// inside the committed fixture directory.
	const String project_path = workflow_prepare_temp_project();
	REQUIRE_MESSAGE(!project_path.is_empty(), "Failed to prepare a temporary MVP project copy.");

	List<String> arguments;
	arguments.push_back("editor");
	arguments.push_back("open");
	arguments.push_back("--headless");
	arguments.push_back("--project");
	arguments.push_back(project_path);
	arguments.push_back("--automation");
	arguments.push_back("--automation-transport=mcp");
	arguments.push_back("--automation-port");
	arguments.push_back("0");
	arguments.push_back("--automation-token");
	arguments.push_back("smoke-token");

	Vector<uint8_t> stdout_bytes;
	Dictionary environment;
	environment["DISPLAY"] = OS::get_singleton()->get_environment("DISPLAY");
	Dictionary pipe_info = OS::get_singleton()->execute_with_pipe(
			OS::get_singleton()->get_executable_path(), arguments, false, String(), environment, false);
	REQUIRE_FALSE(pipe_info.is_empty());

	Ref<FileAccess> stdout_pipe = pipe_info["stdio"];
	const OS::ProcessID pid = pipe_info["pid"];

	String boot_output;
	const uint64_t boot_deadline = OS::get_singleton()->get_ticks_msec() + 120000;
	while (OS::get_singleton()->get_ticks_msec() < boot_deadline) {
		if (stdout_pipe.is_valid() && stdout_pipe->is_open()) {
			const uint64_t available = stdout_pipe->get_length();
			if (available > 0) {
				Vector<uint8_t> chunk;
				chunk.resize(available);
				const uint64_t read = stdout_pipe->get_buffer(chunk.ptrw(), available);
				if (read > 0) {
					boot_output += String::utf8((const char *)chunk.ptr(), read);
				}
			}
		}
		if (boot_output.contains("FOUNDRY_AUTOMATION")) {
			break;
		}
		if (!OS::get_singleton()->is_process_running(pid)) {
			break;
		}
		OS::get_singleton()->delay_usec(20000);
	}

	INFO("Editor boot output:\n", boot_output);
	if (!boot_output.contains("FOUNDRY_AUTOMATION")) {
		if (stdout_pipe.is_valid()) {
			stdout_pipe->close();
		}
		if (OS::get_singleton()->is_process_running(pid)) {
			OS::get_singleton()->kill(pid);
		}
		FAIL("Editor subprocess did not print FOUNDRY_AUTOMATION before the boot deadline.");
		return;
	}

	const int automation_start = boot_output.find("FOUNDRY_AUTOMATION") + String("FOUNDRY_AUTOMATION ").length();
	const int automation_end = boot_output.find_char('\n', automation_start);
	const String automation_json = automation_end >= 0
			? boot_output.substr(automation_start, automation_end - automation_start)
			: boot_output.substr(automation_start);

	JSON automation_parser;
	REQUIRE(automation_parser.parse(automation_json.strip_edges()) == OK);
	const Dictionary automation_line = automation_parser.get_data();
	const String endpoint = automation_line.get("endpoint", String());
	const String token = automation_line.get("token", String());
	const int reported_port = automation_line.get("port", 0);
	REQUIRE_FALSE(endpoint.is_empty());
	REQUIRE(token == "smoke-token");

	const int port = endpoint.get_slicec(':', 2).get_slicec('/', 0).to_int();
	REQUIRE(port > 0);
	CHECK(reported_port == port);

	auto mcp_request = [&](const Dictionary &p_request) -> String {
		Ref<StreamPeerTCP> client;
		client.instantiate();
		REQUIRE(client->connect_to_host(IPAddress("127.0.0.1"), port) == OK);

		const uint64_t connect_deadline = OS::get_singleton()->get_ticks_usec() + 5000000;
		while (client->poll() == OK && client->get_status() == StreamPeerTCP::STATUS_CONNECTING && OS::get_singleton()->get_ticks_usec() < connect_deadline) {
			OS::get_singleton()->delay_usec(1000);
		}
		REQUIRE(client->get_status() == StreamPeerTCP::STATUS_CONNECTED);

		const String body = JSON::stringify(p_request, "", false);
		const CharString body_utf8 = body.utf8();
		String request_text = "POST /mcp HTTP/1.1\r\n";
		request_text += "Host: 127.0.0.1\r\n";
		request_text += "Authorization: Bearer " + token + "\r\n";
		request_text += "Content-Type: application/json\r\n";
		request_text += "Connection: close\r\n";
		request_text += vformat("Content-Length: %d\r\n", body_utf8.length());
		request_text += "\r\n";
		request_text += body;
		const CharString request_utf8 = request_text.utf8();
		REQUIRE(client->put_data((const uint8_t *)request_utf8.get_data(), request_utf8.length()) == OK);

		String response_text;
		const uint64_t response_deadline = OS::get_singleton()->get_ticks_usec() + 30000000;
		while (OS::get_singleton()->get_ticks_usec() < response_deadline) {
			client->poll();
			const int available = client->get_available_bytes();
			if (available > 0) {
				Vector<uint8_t> chunk;
				chunk.resize(available);
				int received = 0;
				if (client->get_partial_data(chunk.ptrw(), available, received) == OK && received > 0) {
					response_text += String::utf8((const char *)chunk.ptr(), received);
				}
			}

			if (response_text.contains("\r\n\r\n")) {
				int content_length = -1;
				const PackedStringArray header_lines = response_text.substr(0, response_text.find("\r\n\r\n")).split("\r\n");
				for (const String &header_line : header_lines) {
					if (header_line.to_lower().begins_with("content-length:")) {
						content_length = header_line.get_slice(":", 1).strip_edges().to_int();
						break;
					}
				}
				const int body_start = response_text.find("\r\n\r\n") + 4;
				if (content_length >= 0 && response_text.length() - body_start >= content_length) {
					break;
				}
			}

			if (client->get_status() != StreamPeerTCP::STATUS_CONNECTED && available == 0) {
				break;
			}
			OS::get_singleton()->delay_usec(2000);
		}

		client->disconnect_from_host();
		return response_text;
	};

	Dictionary init_params;
	init_params["protocolVersion"] = EditorAutomationMCPDispatcher::PROTOCOL_VERSION;
	Dictionary init_request;
	init_request["jsonrpc"] = "2.0";
	init_request["id"] = 1;
	init_request["method"] = "initialize";
	init_request["params"] = init_params;
	const String init_response = mcp_request(init_request);
	CHECK(init_response.contains("HTTP/1.1 200"));
	CHECK(init_response.contains("protocolVersion"));

	Dictionary initialized_notification;
	initialized_notification["jsonrpc"] = "2.0";
	initialized_notification["method"] = "notifications/initialized";
	initialized_notification["params"] = Dictionary();
	(void)mcp_request(initialized_notification);

	Dictionary tools_request;
	tools_request["jsonrpc"] = "2.0";
	tools_request["id"] = 2;
	tools_request["method"] = "tools/list";
	const String tools_response = mcp_request(tools_request);
	CHECK(tools_response.contains("observe_ui"));
	CHECK(tools_response.contains("read_editor_state"));

	Dictionary observe_params;
	observe_params["name"] = "observe_ui";
	observe_params["arguments"] = Dictionary();
	Dictionary observe_request;
	observe_request["jsonrpc"] = "2.0";
	observe_request["id"] = 3;
	observe_request["method"] = "tools/call";
	observe_request["params"] = observe_params;
	const String observe_response = mcp_request(observe_request);
	CHECK(observe_response.contains("element_count"));

	Dictionary state_params;
	state_params["name"] = "read_editor_state";
	state_params["arguments"] = Dictionary();
	Dictionary state_request;
	state_request["jsonrpc"] = "2.0";
	state_request["id"] = 4;
	state_request["method"] = "tools/call";
	state_request["params"] = state_params;
	const String state_response = mcp_request(state_request);
	CHECK(state_response.contains("supported"));

	if (stdout_pipe.is_valid()) {
		stdout_pipe->close();
	}
	OS::get_singleton()->kill(pid);
}

} // namespace TestEditorAutomationWorkflow
