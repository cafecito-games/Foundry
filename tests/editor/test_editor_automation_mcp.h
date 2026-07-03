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

TEST_CASE("[Editor][Automation][MCP] tools/list includes the seven expected tools") {
	EditorAutomationMCPDispatcher dispatcher;
	const Dictionary response = dispatcher.handle_message(make_request(2, "tools/list"));
	CHECK(response.has("result"));
	const Dictionary result = response["result"];
	const Array tools = result["tools"];

	CHECK(tools.size() == 7);
	CHECK(tools_contains(tools, "observe_ui"));
	CHECK(tools_contains(tools, "find_elements"));
	CHECK(tools_contains(tools, "act"));
	CHECK(tools_contains(tools, "wait_for"));
	CHECK(tools_contains(tools, "read_editor_state"));
	CHECK(tools_contains(tools, "read_editor_log"));
	CHECK(tools_contains(tools, "run_command"));

	// Every tool must expose an inputSchema.
	for (int i = 0; i < tools.size(); i++) {
		const Dictionary tool = tools[i];
		CHECK(tool.has("inputSchema"));
	}
}

TEST_CASE("[Editor][Automation][MCP] resources/list includes the four expected resources") {
	EditorAutomationMCPDispatcher dispatcher;
	const Dictionary response = dispatcher.handle_message(make_request(3, "resources/list"));
	CHECK(response.has("result"));
	const Dictionary result = response["result"];
	const Array resources = result["resources"];
	CHECK(resources.size() == 4);

	PackedStringArray uris;
	for (int i = 0; i < resources.size(); i++) {
		const Dictionary resource = resources[i];
		uris.push_back(resource.get("uri", String()));
	}
	CHECK(uris.has("foundry://ui/tree"));
	CHECK(uris.has("foundry://editor/state"));
	CHECK(uris.has("foundry://editor/log"));
	CHECK(uris.has("foundry://scene/active"));
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

} // namespace TestEditorAutomationMCP
