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
#include "editor/automation/editor_automation_snapshot.h"
#include "editor/automation/editor_automation_state.h"
#include "editor/automation/editor_automation_trace.h"
#include "editor/automation/editor_automation_workflow.h"
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

	const Dictionary metadata = EditorAutomationWorkflow::metadata_for_tree_item(tree, child_item);
	CHECK(String(metadata.get("node_name", String())) == "ChildNode");
	CHECK(String(metadata.get("node_class", String())) == "Node");
	CHECK(NodePath(metadata.get("node_path", NodePath())) == NodePath("ChildNode"));

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
}

static void workflow_flush_frames(int p_count = 1) {
	for (int i = 0; i < p_count; i++) {
		SceneTree::get_singleton()->process(1.0 / 60.0);
		MessageQueue::get_singleton()->flush();
	}
}

static String workflow_fixture_project_path() {
	return TestUtils::get_executable_dir().path_join("../tests/fixtures/editor_automation_mvp").simplify_path();
}

static bool workflow_has_display() {
	return OS::get_singleton()->has_environment("DISPLAY") && !OS::get_singleton()->get_environment("DISPLAY").is_empty();
}

static String workflow_run_subprocess(const List<String> &p_arguments, int &r_exit_code) {
	Vector<uint8_t> stdout_bytes;
	Vector<uint8_t> stderr_bytes;

	Dictionary environment;
	if (workflow_has_display()) {
		environment["DISPLAY"] = OS::get_singleton()->get_environment("DISPLAY");
	}

	Dictionary pipe_info = OS::get_singleton()->execute_with_pipe(
			OS::get_singleton()->get_executable_path(), p_arguments, false, String(), environment, false);
	if (pipe_info.is_empty()) {
		r_exit_code = -1;
		return String();
	}

	Ref<FileAccess> stdout_pipe = pipe_info["stdio"];
	Ref<FileAccess> stderr_pipe = pipe_info["stderr"];
	const OS::ProcessID pid = pipe_info["pid"];

	auto pump_pipe = [](const Ref<FileAccess> &p_pipe, Vector<uint8_t> &r_bytes) -> uint64_t {
		if (p_pipe.is_null() || !p_pipe->is_open()) {
			return 0;
		}
		const uint64_t available = p_pipe->get_length();
		if (available == 0) {
			return 0;
		}
		Vector<uint8_t> chunk;
		chunk.resize(available);
		const uint64_t read = p_pipe->get_buffer(chunk.ptrw(), available);
		if (read > 0) {
			const int offset = r_bytes.size();
			r_bytes.resize(offset + read);
			memcpy(r_bytes.ptrw() + offset, chunk.ptr(), read);
		}
		return read;
	};

	const uint64_t deadline = OS::get_singleton()->get_ticks_msec() + 180000;
	while (OS::get_singleton()->get_ticks_msec() < deadline) {
		pump_pipe(stdout_pipe, stdout_bytes);
		pump_pipe(stderr_pipe, stderr_bytes);
		if (!OS::get_singleton()->is_process_running(pid)) {
			pump_pipe(stdout_pipe, stdout_bytes);
			pump_pipe(stderr_pipe, stderr_bytes);
			break;
		}
		OS::get_singleton()->delay_usec(20000);
	}

	if (stdout_pipe.is_valid()) {
		stdout_pipe->close();
	}
	if (stderr_pipe.is_valid()) {
		stderr_pipe->close();
	}

	r_exit_code = OS::get_singleton()->get_process_exit_code(pid);
	String output = String::utf8((const char *)stdout_bytes.ptr(), stdout_bytes.size());
	output += String::utf8((const char *)stderr_bytes.ptr(), stderr_bytes.size());
	return output;
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

TEST_CASE("[Editor][EditorAutomation] MVP acceptance workflow subprocess") {
	if (!workflow_has_display()) {
		MESSAGE("Requires a GUI display. Run with DISPLAY=:1 ./bin/foundry.linuxbsd.editor.dev.x86_64 editor open --project <project> --automation --automation-run-workflow=mvp");
		return;
	}

	const String project_path = workflow_fixture_project_path();
	CHECK_MESSAGE(DirAccess::exists(project_path), "Fixture project missing at ", project_path);
	CHECK_MESSAGE(FileAccess::exists(project_path.path_join("project.foundry")), "Fixture is missing project.foundry");

	List<String> arguments;
	arguments.push_back("editor");
	arguments.push_back("open");
	arguments.push_back("--project");
	arguments.push_back(project_path);
	arguments.push_back("--automation");
	arguments.push_back("--automation-run-workflow=mvp");

	int exit_code = -1;
	const String output = workflow_run_subprocess(arguments, exit_code);
	INFO("Subprocess output:\n", output);
	CHECK_MESSAGE(output.contains("FOUNDRY_AUTOMATION_WORKFLOW"), "Workflow result line was not printed.");

	JSON json;
	if (output.contains("FOUNDRY_AUTOMATION_WORKFLOW")) {
		const int line_start = output.find("FOUNDRY_AUTOMATION_WORKFLOW") + String("FOUNDRY_AUTOMATION_WORKFLOW ").length();
		const int line_end = output.find_char('\n', line_start);
		const String json_text = line_end >= 0 ? output.substr(line_start, line_end - line_start) : output.substr(line_start);
		REQUIRE(json.parse(json_text.strip_edges()) == OK);
		const Dictionary payload = json.get_data();
		CHECK(String(payload.get("workflow", String())) == "mvp");
		CHECK((bool)payload.get("ok", false));
	}

	CHECK(exit_code == 0);
}

TEST_CASE("[Editor][EditorAutomation][MCP] launched editor smoke handshake") {
	if (!workflow_has_display()) {
		MESSAGE("Requires a GUI display. Run with DISPLAY=:1 ./bin/foundry.linuxbsd.editor.dev.x86_64 editor open --project <project> --automation --automation-transport=mcp --automation-port 0");
		return;
	}

	const String project_path = workflow_fixture_project_path();
	CHECK_MESSAGE(DirAccess::exists(project_path), "Fixture project missing at ", project_path);

	List<String> arguments;
	arguments.push_back("editor");
	arguments.push_back("open");
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
	REQUIRE(boot_output.contains("FOUNDRY_AUTOMATION"));

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
	REQUIRE_FALSE(endpoint.is_empty());
	REQUIRE(token == "smoke-token");

	const int port = endpoint.get_slicec(':', 2).get_slicec('/', 0).to_int();
	REQUIRE(port > 0);

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
		const uint64_t response_deadline = OS::get_singleton()->get_ticks_usec() + 10000000;
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
			if (response_text.contains("\r\n\r\n") && response_text.contains("jsonrpc")) {
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
