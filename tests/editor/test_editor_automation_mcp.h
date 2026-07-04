/**************************************************************************/
/*  test_editor_automation_mcp.h                                          */
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

#include "editor/automation/editor_automation_mcp_dispatcher.h"
#include "editor/automation/editor_automation_mcp_server.h"

#include "core/io/json.h"
#include "core/object/message_queue.h"

#ifdef TOOLS_ENABLED
#include "editor/settings/editor_command_palette.h"
#include "editor/settings/editor_settings.h"
#endif

#include "scene/gui/button.h"
#include "scene/gui/panel_container.h"
#include "scene/main/scene_tree.h"

#include "tests/test_macros.h"

namespace TestEditorAutomationMCP {

static void mcp_flush_frames(int p_count = 1) {
	for (int i = 0; i < p_count; i++) {
		SceneTree::get_singleton()->process(1.0 / 60.0);
		MessageQueue::get_singleton()->flush();
	}
}

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

static bool tools_contains(const Array &p_tools, const String &p_name) {
	for (int i = 0; i < p_tools.size(); i++) {
		const Dictionary tool = p_tools[i];
		if (String(tool.get("name", String())) == p_name) {
			return true;
		}
	}
	return false;
}

TEST_CASE("[Editor][Automation][MCP] initialize handshake succeeds") {
	EditorAutomationMCPDispatcher dispatcher;

	Dictionary params;
	params["protocolVersion"] = EditorAutomationMCPDispatcher::PROTOCOL_VERSION;
	const Dictionary response = dispatcher.handle_message(make_request(1, "initialize", params));

	CHECK(response.has("result"));
	const Dictionary result = response["result"];
	CHECK(String(result["protocolVersion"]) == EditorAutomationMCPDispatcher::PROTOCOL_VERSION);
	CHECK(result.has("capabilities"));
	CHECK(result.has("serverInfo"));

	const Dictionary capabilities = result["capabilities"];
	CHECK(capabilities.has("tools"));
	CHECK(capabilities.has("resources"));
}

TEST_CASE("[Editor][Automation][MCP] initialize without protocol version is rejected") {
	EditorAutomationMCPDispatcher dispatcher;
	Dictionary params;
	params["protocolVersion"] = "";
	const Dictionary response = dispatcher.handle_message(make_request(1, "initialize", params));
	CHECK(response.has("error"));
}

TEST_CASE("[Editor][Automation][MCP] notifications/initialized has no response and marks initialized") {
	EditorAutomationMCPDispatcher dispatcher;
	Dictionary notification;
	notification["jsonrpc"] = "2.0";
	notification["method"] = "notifications/initialized";

	bool has_response = true;
	const Dictionary response = dispatcher.handle_message(notification, has_response);
	CHECK_FALSE(has_response);
	CHECK(response.is_empty());
	CHECK(dispatcher.is_initialized());
}

TEST_CASE("[Editor][Automation][MCP] tools/list includes the eight expected tools") {
	EditorAutomationMCPDispatcher dispatcher;
	const Dictionary response = dispatcher.handle_message(make_request(2, "tools/list"));
	CHECK(response.has("result"));
	const Dictionary result = response["result"];
	const Array tools = result["tools"];

	CHECK(tools.size() == 8);
	CHECK(tools_contains(tools, "observe_ui"));
	CHECK(tools_contains(tools, "find_elements"));
	CHECK(tools_contains(tools, "act"));
	CHECK(tools_contains(tools, "wait_for"));
	CHECK(tools_contains(tools, "read_editor_state"));
	CHECK(tools_contains(tools, "read_editor_log"));
	CHECK(tools_contains(tools, "run_command"));
	CHECK(tools_contains(tools, "list_commands"));

	// Every tool must expose an inputSchema.
	for (int i = 0; i < tools.size(); i++) {
		const Dictionary tool = tools[i];
		CHECK(tool.has("inputSchema"));
	}
}

TEST_CASE("[Editor][Automation][MCP] resources/list includes the five expected resources") {
	EditorAutomationMCPDispatcher dispatcher;
	const Dictionary response = dispatcher.handle_message(make_request(3, "resources/list"));
	CHECK(response.has("result"));
	const Dictionary result = response["result"];
	const Array resources = result["resources"];
	CHECK(resources.size() == 5);

	PackedStringArray uris;
	for (int i = 0; i < resources.size(); i++) {
		const Dictionary resource = resources[i];
		uris.push_back(resource.get("uri", String()));
	}
	CHECK(uris.has("foundry://ui/tree"));
	CHECK(uris.has("foundry://editor/state"));
	CHECK(uris.has("foundry://editor/log"));
	CHECK(uris.has("foundry://scene/active"));
	CHECK(uris.has("foundry://commands"));
}

TEST_CASE("[Editor][Automation][MCP] unknown method returns method_not_found") {
	EditorAutomationMCPDispatcher dispatcher;
	const Dictionary response = dispatcher.handle_message(make_request(4, "does/not/exist"));
	CHECK(response.has("error"));
	const Dictionary error = response["error"];
	CHECK((int)error["code"] == EditorAutomationMCPDispatcher::METHOD_NOT_FOUND);
}

TEST_CASE("[Editor][Automation][MCP] tools/call unknown tool returns structured error") {
	EditorAutomationMCPDispatcher dispatcher;
	Dictionary params;
	params["name"] = "not_a_tool";
	params["arguments"] = Dictionary();
	const Dictionary response = dispatcher.handle_message(make_request(5, "tools/call", params));
	CHECK(response.has("error"));
	const Dictionary error = response["error"];
	CHECK((int)error["code"] == EditorAutomationMCPDispatcher::METHOD_NOT_FOUND);
}

TEST_CASE("[Editor][Automation][MCP] observe_ui delegates to the snapshot core") {
	PanelContainer *root = memnew(PanelContainer);
	root->set_size(Size2(400, 300));
	SceneTree::get_singleton()->get_root()->add_child(root);

	Button *button = memnew(Button);
	button->set_text("Add Child Node");
	button->set_anchors_and_offsets_preset(Control::PRESET_TOP_LEFT);
	button->set_size(Size2(120, 32));
	root->add_child(button);
	mcp_flush_frames();

	EditorAutomationMCPDispatcher dispatcher;
	EditorAutomationMCPDispatcher::Options options;
	options.snapshot_root = root;
	dispatcher.set_options(options);

	Dictionary params;
	params["name"] = "observe_ui";
	params["arguments"] = Dictionary();
	const Dictionary response = dispatcher.handle_message(make_request(6, "tools/call", params));
	CHECK(response.has("result"));

	const Dictionary result = response["result"];
	CHECK_FALSE((bool)result["isError"]);
	const Dictionary structured = result["structuredContent"];
	CHECK((int)structured["element_count"] >= 2);

	const Array tree = structured["tree"];
	CHECK(tree.size() == 1);

	// The compact JSON text content must also mention the button name.
	const Array content = result["content"];
	const Dictionary first = content[0];
	CHECK(String(first["text"]).contains("Add Child Node"));

	memdelete(root);
}

TEST_CASE("[Editor][Automation][MCP] find_elements delegates to the selector core") {
	PanelContainer *root = memnew(PanelContainer);
	root->set_size(Size2(400, 300));
	SceneTree::get_singleton()->get_root()->add_child(root);

	Button *button = memnew(Button);
	button->set_text("Save Scene");
	button->set_anchors_and_offsets_preset(Control::PRESET_TOP_LEFT);
	button->set_size(Size2(120, 32));
	root->add_child(button);

	Button *dup_a = memnew(Button);
	dup_a->set_text("Duplicate");
	dup_a->set_anchors_and_offsets_preset(Control::PRESET_TOP_LEFT);
	dup_a->set_size(Size2(120, 32));
	root->add_child(dup_a);

	Button *dup_b = memnew(Button);
	dup_b->set_text("Duplicate");
	dup_b->set_anchors_and_offsets_preset(Control::PRESET_TOP_LEFT);
	dup_b->set_size(Size2(120, 32));
	root->add_child(dup_b);
	mcp_flush_frames();

	EditorAutomationMCPDispatcher dispatcher;
	EditorAutomationMCPDispatcher::Options options;
	options.snapshot_root = root;
	dispatcher.set_options(options);

	SUBCASE("match") {
		Dictionary selector;
		selector["role"] = "button";
		selector["name"] = "Save Scene";
		Dictionary arguments;
		arguments["selector"] = selector;
		Dictionary params;
		params["name"] = "find_elements";
		params["arguments"] = arguments;

		const Dictionary response = dispatcher.handle_message(make_request(7, "tools/call", params));
		const Dictionary result = response["result"];
		CHECK_FALSE((bool)result["isError"]);
		const Dictionary structured = result["structuredContent"];
		CHECK((bool)structured["ok"]);
		CHECK((int)structured["match_count"] == 1);
	}

	SUBCASE("no match") {
		Dictionary selector;
		selector["role"] = "button";
		selector["name"] = "Does Not Exist";
		Dictionary arguments;
		arguments["selector"] = selector;
		Dictionary params;
		params["name"] = "find_elements";
		params["arguments"] = arguments;

		const Dictionary response = dispatcher.handle_message(make_request(8, "tools/call", params));
		const Dictionary result = response["result"];
		CHECK((bool)result["isError"]);
		const Dictionary structured = result["structuredContent"];
		CHECK_FALSE((bool)structured["ok"]);
	}

	SUBCASE("multiple matches are returned, not treated as an error") {
		Dictionary selector;
		selector["role"] = "button";
		selector["name"] = "Duplicate";
		Dictionary arguments;
		arguments["selector"] = selector;
		Dictionary params;
		params["name"] = "find_elements";
		params["arguments"] = arguments;

		const Dictionary response = dispatcher.handle_message(make_request(10, "tools/call", params));
		const Dictionary result = response["result"];
		CHECK_FALSE((bool)result["isError"]);
		const Dictionary structured = result["structuredContent"];
		CHECK((bool)structured["ok"]);
		CHECK((bool)structured["ambiguous"]);
		CHECK((int)structured["match_count"] == 2);
	}

	memdelete(root);
}

TEST_CASE("[Editor][Automation][MCP] act delegates to the action driver") {
	PanelContainer *root = memnew(PanelContainer);
	root->set_size(Size2(400, 300));
	SceneTree::get_singleton()->get_root()->add_child(root);

	Button *button = memnew(Button);
	button->set_text("Click Me");
	button->set_anchors_and_offsets_preset(Control::PRESET_TOP_LEFT);
	button->set_size(Size2(120, 32));
	root->add_child(button);
	mcp_flush_frames();

	EditorAutomationMCPDispatcher dispatcher;
	EditorAutomationMCPDispatcher::Options options;
	options.snapshot_root = root;
	dispatcher.set_options(options);

	Dictionary selector;
	selector["role"] = "button";
	selector["name"] = "Click Me";
	Dictionary arguments;
	arguments["selector"] = selector;
	arguments["action"] = "click";
	Dictionary params;
	params["name"] = "act";
	params["arguments"] = arguments;

	const Dictionary response = dispatcher.handle_message(make_request(9, "tools/call", params));
	const Dictionary result = response["result"];
	CHECK_FALSE((bool)result["isError"]);
	const Dictionary structured = result["structuredContent"];
	CHECK((bool)structured["ok"]);
	CHECK(String(structured["route"]) == "semantic_click");

	memdelete(root);
}

TEST_CASE("[Editor][Automation][MCP] act consumes handle returned by observe_ui across snapshots") {
	PanelContainer *root = memnew(PanelContainer);
	root->set_size(Size2(400, 300));
	SceneTree::get_singleton()->get_root()->add_child(root);

	Button *button = memnew(Button);
	button->set_text("Observe Then Act");
	button->set_anchors_and_offsets_preset(Control::PRESET_TOP_LEFT);
	button->set_size(Size2(160, 32));
	root->add_child(button);
	mcp_flush_frames();

	EditorAutomationMCPDispatcher dispatcher;
	EditorAutomationMCPDispatcher::Options options;
	options.snapshot_root = root;
	dispatcher.set_options(options);

	Dictionary observe_params;
	observe_params["name"] = "observe_ui";
	observe_params["arguments"] = Dictionary();
	const Dictionary observe_response = dispatcher.handle_message(make_request(11, "tools/call", observe_params));
	CHECK(observe_response.has("result"));
	const Dictionary observe_result = observe_response["result"];
	CHECK_FALSE((bool)observe_result["isError"]);
	const Dictionary observe_structured = observe_result["structuredContent"];
	const Array tree = observe_structured["tree"];
	REQUIRE(tree.size() == 1);

	const Dictionary root_element = tree[0];
	const Array children = root_element["children"];
	REQUIRE(children.size() >= 1);
	const Dictionary button_element = children[0];
	const String observed_id = button_element["id"];
	const String observed_handle = button_element["handle"];
	CHECK_FALSE(observed_id.is_empty());
	CHECK_FALSE(observed_handle.is_empty());

	mcp_flush_frames();

	Dictionary selector;
	selector["id"] = observed_id;
	Dictionary arguments;
	arguments["selector"] = selector;
	arguments["action"] = "click";
	Dictionary act_params;
	act_params["name"] = "act";
	act_params["arguments"] = arguments;
	const Dictionary act_response = dispatcher.handle_message(make_request(12, "tools/call", act_params));
	CHECK(act_response.has("result"));
	const Dictionary act_result = act_response["result"];
	CHECK_FALSE((bool)act_result["isError"]);
	const Dictionary act_structured = act_result["structuredContent"];
	CHECK((bool)act_structured["ok"]);
	const Dictionary act_details = act_structured["details"];
	CHECK((bool)act_details["reconciled"]);
	CHECK((uint64_t)act_details["snapshot_generation"] > (uint64_t)observe_structured["generation"]);

	memdelete(root);
}

// --- HTTP transport (socket-free) tests. ---

static EditorAutomationMCPServer::HTTPRequest make_http_request(const String &p_method, const String &p_body, const String &p_token, const String &p_origin = String()) {
	EditorAutomationMCPServer::HTTPRequest request;
	request.method = p_method;
	request.path = "/mcp";
	if (!p_token.is_empty()) {
		request.headers.insert("authorization", "Bearer " + p_token);
	}
	if (!p_origin.is_empty()) {
		request.headers.insert("origin", p_origin);
	}
	request.headers.insert("content-type", "application/json");
	request.body = p_body;
	return request;
}

TEST_CASE("[Editor][Automation][MCP] request without token is rejected") {
	EditorAutomationMCPServer server;
	server.set_token("secret-token");

	const Dictionary request = make_request(1, "initialize");
	const String body = JSON::stringify(request, "", false);

	// No Authorization header.
	EditorAutomationMCPServer::HTTPRequest http_request = make_http_request("POST", body, String());
	const EditorAutomationMCPServer::HTTPResponse response = server.process_http_request(http_request);
	CHECK(response.status == 401);

	// Wrong token.
	EditorAutomationMCPServer::HTTPRequest wrong = make_http_request("POST", body, "wrong-token");
	const EditorAutomationMCPServer::HTTPResponse wrong_response = server.process_http_request(wrong);
	CHECK(wrong_response.status == 401);
}

TEST_CASE("[Editor][Automation][MCP] invalid origin is rejected") {
	EditorAutomationMCPServer server;
	server.set_token("secret-token");

	const String body = JSON::stringify(make_request(1, "initialize"), "", false);

	EditorAutomationMCPServer::HTTPRequest evil = make_http_request("POST", body, "secret-token", "http://evil.example.com");
	const EditorAutomationMCPServer::HTTPResponse evil_response = server.process_http_request(evil);
	CHECK(evil_response.status == 403);

	EditorAutomationMCPServer::HTTPRequest null_origin = make_http_request("POST", body, "secret-token", "null");
	const EditorAutomationMCPServer::HTTPResponse null_response = server.process_http_request(null_origin);
	CHECK(null_response.status == 403);
}

TEST_CASE("[Editor][Automation][MCP] valid token and local origin dispatches") {
	EditorAutomationMCPServer server;
	server.set_token("secret-token");

	Dictionary params;
	params["protocolVersion"] = EditorAutomationMCPDispatcher::PROTOCOL_VERSION;
	const String body = JSON::stringify(make_request(1, "initialize", params), "", false);

	EditorAutomationMCPServer::HTTPRequest ok = make_http_request("POST", body, "secret-token", "http://127.0.0.1:9000");
	const EditorAutomationMCPServer::HTTPResponse response = server.process_http_request(ok);
	CHECK(response.status == 200);

	JSON json;
	REQUIRE(json.parse(response.body) == OK);
	const Dictionary parsed = json.get_data();
	CHECK(parsed.has("result"));

	// localhost origin variant is also allowed.
	EditorAutomationMCPServer::HTTPRequest localhost = make_http_request("POST", body, "secret-token", "http://localhost");
	CHECK(server.process_http_request(localhost).status == 200);
}

TEST_CASE("[Editor][Automation][MCP] GET method is not allowed") {
	EditorAutomationMCPServer server;
	server.set_token("secret-token");
	EditorAutomationMCPServer::HTTPRequest get = make_http_request("GET", String(), "secret-token");
	CHECK(server.process_http_request(get).status == 405);
}

TEST_CASE("[Editor][Automation][MCP] socket listen, auth handshake, and shutdown") {
	EditorAutomationMCPServer server;
	server.set_token("smoke-token");
	REQUIRE(server.listen(0, IPAddress("127.0.0.1")) == OK);
	REQUIRE(server.is_listening());
	const int port = server.get_port();
	CHECK(port > 0);

	Ref<StreamPeerTCP> client;
	client.instantiate();
	REQUIRE(client->connect_to_host(IPAddress("127.0.0.1"), port) == OK);

	const uint64_t deadline = OS::get_singleton()->get_ticks_usec() + 2000000;
	while (client->poll() == OK && client->get_status() == StreamPeerTCP::STATUS_CONNECTING && OS::get_singleton()->get_ticks_usec() < deadline) {
		OS::get_singleton()->delay_usec(1000);
	}
	REQUIRE(client->get_status() == StreamPeerTCP::STATUS_CONNECTED);

	Dictionary params;
	params["protocolVersion"] = EditorAutomationMCPDispatcher::PROTOCOL_VERSION;
	const String json_body = JSON::stringify(make_request(1, "initialize", params), "", false);
	const CharString body_utf8 = json_body.utf8();

	String request_text = "POST /mcp HTTP/1.1\r\n";
	request_text += "Host: 127.0.0.1\r\n";
	request_text += "Authorization: Bearer smoke-token\r\n";
	request_text += "Content-Type: application/json\r\n";
	request_text += vformat("Content-Length: %d\r\n", body_utf8.length());
	request_text += "\r\n";
	request_text += json_body;
	const CharString request_utf8 = request_text.utf8();
	REQUIRE(client->put_data((const uint8_t *)request_utf8.get_data(), request_utf8.length()) == OK);

	String response_text;
	const uint64_t response_deadline = OS::get_singleton()->get_ticks_usec() + 3000000;
	while (OS::get_singleton()->get_ticks_usec() < response_deadline) {
		server.poll();
		client->poll();
		int available = client->get_available_bytes();
		if (available > 0) {
			Vector<uint8_t> chunk;
			chunk.resize(available);
			int received = 0;
			if (client->get_partial_data(chunk.ptrw(), available, received) == OK && received > 0) {
				response_text += String::utf8((const char *)chunk.ptr(), received);
			}
		}
		if (response_text.contains("\r\n\r\n") && response_text.contains("protocolVersion")) {
			break;
		}
		OS::get_singleton()->delay_usec(2000);
	}

	CHECK(response_text.contains("HTTP/1.1 200"));
	CHECK(response_text.contains("protocolVersion"));

	client->disconnect_from_host();
	server.stop();
	CHECK_FALSE(server.is_listening());

	// Port must be released: a fresh server can bind again.
	EditorAutomationMCPServer server_again;
	server_again.set_token("smoke-token-2");
	CHECK(server_again.listen(port, IPAddress("127.0.0.1")) == OK);
	server_again.stop();
}

#ifdef TOOLS_ENABLED

class AutomationCommandProbe : public Object {
	FOUNDRY_CLASS(AutomationCommandProbe, Object);

public:
	bool executed = false;

	void mark() {
		executed = true;
	}
};

static bool command_entries_contain_key(const Array &p_commands, const String &p_key) {
	for (int i = 0; i < p_commands.size(); i++) {
		const Dictionary entry = p_commands[i];
		if (String(entry.get("key", String())) == p_key) {
			return true;
		}
	}
	return false;
}

static Dictionary command_entry_for_key(const Array &p_commands, const String &p_key) {
	for (int i = 0; i < p_commands.size(); i++) {
		const Dictionary entry = p_commands[i];
		if (String(entry.get("key", String())) == p_key) {
			return entry;
		}
	}
	return Dictionary();
}

TEST_CASE("[Editor][Automation][MCP] list_commands exposes command discovery schema") {
	EditorAutomationMCPDispatcher dispatcher;
	const Dictionary response = dispatcher.handle_message(make_request(20, "tools/list"));
	const Dictionary result = response.get("result", Dictionary());
	const Array tools = result.get("tools", Array());

	bool found = false;
	for (int i = 0; i < tools.size(); i++) {
		const Dictionary tool = tools[i];
		if (String(tool.get("name", String())) == "list_commands") {
			found = true;
			CHECK(tool.has("inputSchema"));
			const Dictionary schema = tool["inputSchema"];
			CHECK(schema.has("properties"));
			break;
		}
	}
	CHECK(found);
}

TEST_CASE("[Editor][Automation][MCP] list_commands and foundry://commands report registered commands") {
	EditorCommandPalette *palette = EditorCommandPalette::get_singleton();
	REQUIRE(palette != nullptr);

	AutomationCommandProbe probe;
	palette->add_command("Automation Test Command", "automation/test_palette_command", callable_mp(&probe, &AutomationCommandProbe::mark), varray(), Ref<Shortcut>());

	Ref<Shortcut> shortcut = ED_SHORTCUT("automation/test_shortcut_action", "Automation Test Shortcut", Key::F24);

	EditorAutomationMCPDispatcher dispatcher;

	Dictionary list_params;
	list_params["name"] = "list_commands";
	list_params["arguments"] = Dictionary();
	const Dictionary list_response = dispatcher.handle_message(make_request(21, "tools/call", list_params));
	CHECK(list_response.has("result"));
	const Dictionary list_result = list_response["result"];
	CHECK_FALSE((bool)list_result["isError"]);
	const Dictionary list_structured = list_result["structuredContent"];
	CHECK((bool)list_structured["ok"]);
	const Array commands = list_structured["commands"];
	CHECK(command_entries_contain_key(commands, "automation/test_palette_command"));
	CHECK(command_entries_contain_key(commands, "automation/test_shortcut_action"));

	const Dictionary palette_entry = command_entry_for_key(commands, "automation/test_palette_command");
	CHECK(String(palette_entry.get("source", String())) == "command_palette");
	CHECK((bool)palette_entry.get("runnable_by_run_command", false));

	const Dictionary shortcut_entry = command_entry_for_key(commands, "automation/test_shortcut_action");
	CHECK(String(shortcut_entry.get("source", String())) == "shortcut");
	CHECK(String(shortcut_entry.get("label", String())) == "Automation Test Shortcut");
	CHECK((bool)shortcut_entry.get("enabled", false));
	CHECK_FALSE((bool)shortcut_entry.get("runnable_by_run_command", true));
	CHECK(String(shortcut_entry.get("non_runnable_reason", String())).contains("viewport"));

	Dictionary read_params;
	read_params["uri"] = "foundry://commands";
	const Dictionary read_response = dispatcher.handle_message(make_request(22, "resources/read", read_params));
	CHECK(read_response.has("result"));
	const Dictionary read_result = read_response.get("result", Dictionary());
	const Array contents = read_result.get("contents", Array());
	REQUIRE(contents.size() >= 1);
	const Dictionary content = contents[0];
	CHECK(String(content.get("uri", String())) == "foundry://commands");

	JSON json;
	REQUIRE(json.parse(content.get("text", String())) == OK);
	const Dictionary payload = json.get_data();
	CHECK((bool)payload.get("ok", false));
	CHECK(command_entries_contain_key(payload.get("commands", Array()), "automation/test_palette_command"));

	palette->remove_command("automation/test_palette_command");
	EditorSettings::get_singleton()->remove_shortcut("automation/test_shortcut_action");
}

TEST_CASE("[Editor][Automation][MCP] run_command executes palette command and reports unknown suggestions") {
	EditorCommandPalette *palette = EditorCommandPalette::get_singleton();
	REQUIRE(palette != nullptr);

	AutomationCommandProbe probe;
	palette->add_command("Automation Execute Command", "automation/execute_palette_command", callable_mp(&probe, &AutomationCommandProbe::mark), varray(), Ref<Shortcut>());

	EditorAutomationMCPDispatcher dispatcher;

	Dictionary run_params;
	run_params["name"] = "run_command";
	Dictionary run_args;
	run_args["command"] = "automation/execute_palette_command";
	run_params["arguments"] = run_args;
	const Dictionary run_response = dispatcher.handle_message(make_request(23, "tools/call", run_params));
	CHECK(run_response.has("result"));
	const Dictionary run_result = run_response["result"];
	CHECK_FALSE((bool)run_result["isError"]);
	const Dictionary run_structured = run_result["structuredContent"];
	CHECK((bool)run_structured["ok"]);
	CHECK(String(run_structured.get("route", String())) == "command_palette");
	CHECK(probe.executed);

	Dictionary unknown_params;
	unknown_params["name"] = "run_command";
	Dictionary unknown_args;
	unknown_args["command"] = "automation/missing_command";
	unknown_params["arguments"] = unknown_args;
	const Dictionary unknown_response = dispatcher.handle_message(make_request(24, "tools/call", unknown_params));
	CHECK(unknown_response.has("result"));
	const Dictionary unknown_result = unknown_response["result"];
	CHECK((bool)unknown_result["isError"]);
	const Dictionary unknown_structured = unknown_result["structuredContent"];
	CHECK((bool)unknown_structured.get("ok", true) == false);
	CHECK(String(unknown_structured.get("kind", String())) == "unknown_command");
	CHECK(unknown_structured.has("suggestions"));
	CHECK(unknown_structured.has("candidates"));

	palette->remove_command("automation/execute_palette_command");
}

TEST_CASE("[Editor][Automation][MCP] disabled shortcut command is reported as non-runnable") {
	EditorCommandPalette *palette = EditorCommandPalette::get_singleton();
	REQUIRE(palette != nullptr);

	Ref<Shortcut> disabled_shortcut;
	disabled_shortcut.instantiate();
	disabled_shortcut->set_name("Disabled Automation Shortcut");
	palette->add_command("Disabled Automation Command", "automation/disabled_command", callable_mp(palette, &EditorCommandPalette::open_popup), varray(), disabled_shortcut);

	EditorAutomationMCPDispatcher dispatcher;

	Dictionary list_params;
	list_params["name"] = "list_commands";
	list_params["arguments"] = Dictionary();
	const Dictionary list_response = dispatcher.handle_message(make_request(25, "tools/call", list_params));
	const Dictionary list_result = list_response.get("result", Dictionary());
	const Dictionary list_structured = list_result.get("structuredContent", Dictionary());
	const Dictionary disabled_entry = command_entry_for_key(list_structured.get("commands", Array()), "automation/disabled_command");
	CHECK((bool)disabled_entry.get("enabled", true) == false);
	CHECK((bool)disabled_entry.get("runnable_by_run_command", true) == false);
	CHECK_FALSE(String(disabled_entry.get("non_runnable_reason", String())).is_empty());

	Dictionary run_params;
	run_params["name"] = "run_command";
	Dictionary run_args;
	run_args["command"] = "automation/disabled_command";
	run_params["arguments"] = run_args;
	const Dictionary run_response = dispatcher.handle_message(make_request(26, "tools/call", run_params));
	const Dictionary run_result = run_response.get("result", Dictionary());
	const Dictionary run_structured = run_result.get("structuredContent", Dictionary());
	CHECK((bool)run_structured.get("ok", true) == false);
	CHECK(String(run_structured.get("kind", String())) == "disabled_command");

	palette->remove_command("automation/disabled_command");
}

TEST_CASE("[Editor][Automation][MCP] scene_tree/add_child_node shortcut is discoverable") {
	Ref<Shortcut> shortcut = ED_SHORTCUT("scene_tree/add_child_node", "Add Child Node...", KeyModifierMask::CMD_OR_CTRL | Key::A);

	EditorAutomationMCPDispatcher dispatcher;
	Dictionary list_params;
	list_params["name"] = "list_commands";
	Dictionary list_args;
	list_args["query"] = "add_child";
	list_params["arguments"] = list_args;
	const Dictionary list_response = dispatcher.handle_message(make_request(27, "tools/call", list_params));
	const Dictionary list_result = list_response.get("result", Dictionary());
	const Dictionary list_structured = list_result.get("structuredContent", Dictionary());
	const Dictionary entry = command_entry_for_key(list_structured.get("commands", Array()), "scene_tree/add_child_node");
	CHECK(String(entry.get("key", String())) == "scene_tree/add_child_node");
	CHECK(String(entry.get("label", String())) == "Add Child Node...");
	CHECK(String(entry.get("source", String())) == "shortcut");
	CHECK((bool)entry.get("enabled", false));
	CHECK_FALSE((bool)entry.get("runnable_by_run_command", true));

	EditorSettings::get_singleton()->remove_shortcut("scene_tree/add_child_node");
}

#endif // TOOLS_ENABLED

} // namespace TestEditorAutomationMCP
