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

#include "editor/automation/editor_automation_events.h"
#include "editor/automation/editor_automation_log.h"
#include "editor/automation/editor_automation_mcp_contracts.h"
#include "editor/automation/editor_automation_mcp_dispatcher.h"
#include "editor/automation/editor_automation_mcp_server.h"
#include "editor/automation/editor_automation_wait.h"

#include "core/io/json.h"
#include "core/object/message_queue.h"

#ifdef TOOLS_ENABLED
#include "editor/settings/editor_command_palette.h"
#include "editor/settings/editor_settings.h"
#endif

#include "scene/gui/button.h"
#include "scene/gui/line_edit.h"
#include "scene/gui/panel_container.h"
#include "scene/gui/spin_box.h"
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

static Dictionary tool_named(const Array &p_tools, const String &p_name) {
	for (int i = 0; i < p_tools.size(); i++) {
		const Dictionary tool = p_tools[i];
		if (String(tool.get("name", String())) == p_name) {
			return tool;
		}
	}
	return Dictionary();
}

static void check_direct_schema_property_descriptions(const Dictionary &p_schema) {
	if (!p_schema.has("properties")) {
		return;
	}
	const Dictionary properties = p_schema["properties"];
	const Array keys = properties.keys();
	for (int i = 0; i < keys.size(); i++) {
		const Dictionary property_schema = properties[keys[i]];
		CHECK_FALSE(String(property_schema.get("description", String())).is_empty());
	}
}

static PanelContainer *mcp_make_large_button_tree(int p_count) {
	PanelContainer *root = memnew(PanelContainer);
	root->set_size(Size2(400, 300));
	SceneTree::get_singleton()->get_root()->add_child(root);
	for (int i = 0; i < p_count; i++) {
		Button *button = memnew(Button);
		button->set_text(vformat("Bulk %d", i));
		button->set_anchors_and_offsets_preset(Control::PRESET_TOP_LEFT);
		button->set_size(Size2(120, 32));
		root->add_child(button);
	}
	mcp_flush_frames();
	return root;
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

TEST_CASE("[Editor][Automation][MCP] tools/list includes the nine expected tools") {
	EditorAutomationMCPDispatcher dispatcher;
	const Dictionary response = dispatcher.handle_message(make_request(2, "tools/list"));
	CHECK(response.has("result"));
	const Dictionary result = response["result"];
	const Array tools = result["tools"];

	CHECK(tools.size() == 9);
	CHECK(!tool_named(tools, "observe_ui").is_empty());
	CHECK(!tool_named(tools, "find_elements").is_empty());
	CHECK(!tool_named(tools, "act").is_empty());
	CHECK(!tool_named(tools, "wait_for").is_empty());
	CHECK(!tool_named(tools, "read_editor_state").is_empty());
	CHECK(!tool_named(tools, "read_editor_log").is_empty());
	CHECK(!tool_named(tools, "run_command").is_empty());
	CHECK(!tool_named(tools, "list_commands").is_empty());
	CHECK(!tool_named(tools, "poll_events").is_empty());

	for (int i = 0; i < tools.size(); i++) {
		const Dictionary tool = tools[i];
		CHECK(tool.has("inputSchema"));
		CHECK(tool.has("outputSchema"));
	}
}

TEST_CASE("[Editor][Automation][MCP] resources/list includes the six expected resources") {
	EditorAutomationMCPDispatcher dispatcher;
	const Dictionary response = dispatcher.handle_message(make_request(3, "resources/list"));
	CHECK(response.has("result"));
	const Dictionary result = response["result"];
	const Array resources = result["resources"];
	CHECK(resources.size() == 6);

	PackedStringArray uris;
	for (int i = 0; i < resources.size(); i++) {
		const Dictionary resource = resources[i];
		uris.push_back(resource.get("uri", String()));
	}
	CHECK(uris.has("foundry://ui/tree"));
	CHECK(uris.has("foundry://editor/state"));
	CHECK(uris.has("foundry://editor/log"));
	CHECK(uris.has("foundry://scene/active"));
	CHECK(uris.has("foundry://scene/tree"));
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

TEST_CASE("[Editor][Automation][MCP] cooperative wait_for does not block read_editor_state and can be cancelled") {
	EditorAutomationWait::clear_all_cooperative();

	EditorAutomationMCPDispatcher dispatcher;

	Dictionary wait_args;
	wait_args["condition"] = "selector_appears";
	Dictionary selector;
	selector["role"] = "button";
	selector["name"] = "Never Appears";
	wait_args["selector"] = selector;
	wait_args["timeout_ms"] = 60000;
	wait_args["cooperative"] = true;

	Dictionary wait_params;
	wait_params["name"] = "wait_for";
	wait_params["arguments"] = wait_args;
	const Dictionary wait_response = dispatcher.handle_message(make_request(30, "tools/call", wait_params));
	CHECK(wait_response.has("result"));
	const Dictionary wait_response_result = wait_response["result"];
	CHECK_FALSE((bool)wait_response_result["isError"]);
	const Dictionary wait_structured = wait_response_result["structuredContent"];
	CHECK(wait_structured["status"] == "pending");
	CHECK(wait_structured.has("wait_id"));
	const String wait_id = wait_structured["wait_id"];

	Dictionary state_params;
	state_params["name"] = "read_editor_state";
	state_params["arguments"] = Dictionary();
	const Dictionary state_response = dispatcher.handle_message(make_request(31, "tools/call", state_params));
	CHECK(state_response.has("result"));
	const Dictionary state_response_result = state_response["result"];
	CHECK_FALSE((bool)state_response_result["isError"]);

	Dictionary cancel_args;
	cancel_args["wait_id"] = wait_id;
	cancel_args["cancel"] = true;
	Dictionary cancel_params;
	cancel_params["name"] = "wait_for";
	cancel_params["arguments"] = cancel_args;
	const Dictionary cancel_response = dispatcher.handle_message(make_request(32, "tools/call", cancel_params));
	const Dictionary cancel_response_result = cancel_response["result"];
	const Dictionary cancel_structured = cancel_response_result["structuredContent"];
	CHECK(cancel_structured["status"] == "cancelled");

	EditorAutomationWait::clear_all_cooperative();
}

TEST_CASE("[Editor][Automation][MCP] act with wait clause succeeds when condition is met") {
	EditorAutomationWait::clear_all_cooperative();

	PanelContainer *root = memnew(PanelContainer);
	root->set_size(Size2(400, 300));
	SceneTree::get_singleton()->get_root()->add_child(root);

	LineEdit *first = memnew(LineEdit);
	first->set_name("FirstField");
	first->set_anchors_and_offsets_preset(Control::PRESET_TOP_LEFT);
	first->set_size(Size2(120, 32));
	root->add_child(first);

	LineEdit *second = memnew(LineEdit);
	second->set_name("SecondField");
	second->set_anchors_and_offsets_preset(Control::PRESET_TOP_LEFT);
	second->set_size(Size2(120, 32));
	root->add_child(second);
	mcp_flush_frames();

	EditorAutomationMCPDispatcher dispatcher;
	EditorAutomationMCPDispatcher::Options options;
	options.snapshot_root = root;
	dispatcher.set_options(options);

	Dictionary target;
	target["role"] = "text_field";
	target["name"] = "SecondField";

	Dictionary wait;
	wait["type"] = "focus_matches";
	wait["selector"] = target;

	Dictionary arguments;
	arguments["action"] = "focus";
	arguments["selector"] = target;
	arguments["wait"] = wait;
	arguments["wait_timeout_ms"] = 1000;

	Dictionary params;
	params["name"] = "act";
	params["arguments"] = arguments;

	const Dictionary response = dispatcher.handle_message(make_request(33, "tools/call", params));
	const Dictionary response_result = response["result"];
	CHECK_FALSE((bool)response_result["isError"]);
	const Dictionary structured = response_result["structuredContent"];
	CHECK((bool)structured["ok"]);
	CHECK(structured.has("action"));
	CHECK(structured.has("wait"));
	CHECK((bool)((Dictionary)structured["action"])["ok"]);
	CHECK((bool)((Dictionary)structured["wait"])["ok"]);

	EditorAutomationWait::clear_all_cooperative();
	memdelete(root);
}

TEST_CASE("[Editor][Automation][MCP] act success with wait timeout reports partial success shape") {
	EditorAutomationWait::clear_all_cooperative();

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

	Dictionary wait;
	wait["type"] = "selector_appears";
	Dictionary missing;
	missing["role"] = "button";
	missing["name"] = "Missing Dialog";
	wait["selector"] = missing;

	Dictionary arguments;
	arguments["action"] = "click";
	arguments["selector"] = selector;
	arguments["wait"] = wait;
	arguments["wait_timeout_ms"] = 50;

	Dictionary params;
	params["name"] = "act";
	params["arguments"] = arguments;

	Dictionary response = dispatcher.handle_message(make_request(34, "tools/call", params));
	Dictionary response_result = response["result"];
	Dictionary structured = response_result["structuredContent"];
	String wait_id = structured.get("wait_id", String());

	while (structured.get("status", String()) == "pending" && !wait_id.is_empty()) {
		mcp_flush_frames(2);
		Dictionary poll_args;
		poll_args["wait_id"] = wait_id;
		Dictionary poll_params;
		poll_params["name"] = "act";
		poll_params["arguments"] = poll_args;
		response = dispatcher.handle_message(make_request(35, "tools/call", poll_params));
		response_result = response["result"];
		structured = response_result["structuredContent"];
		wait_id = structured.get("wait_id", wait_id);
	}

	CHECK_FALSE((bool)structured["ok"]);
	CHECK(structured["kind"] == "wait_timeout");
	CHECK((bool)((Dictionary)structured["action"])["ok"]);
	CHECK(structured.has("details"));
	CHECK(((Dictionary)structured["details"]).has("condition"));
	CHECK(((Dictionary)structured["details"]).has("action_trace"));
	CHECK(((Dictionary)structured["details"]).has("modal_stack"));

	EditorAutomationWait::clear_all_cooperative();
	memdelete(root);
}

TEST_CASE("[Editor][Automation][MCP] act failure before wait reports action_failed shape") {
	EditorAutomationWait::clear_all_cooperative();

	PanelContainer *root = memnew(PanelContainer);
	root->set_size(Size2(400, 300));
	SceneTree::get_singleton()->get_root()->add_child(root);

	Button *first = memnew(Button);
	first->set_text("Duplicate");
	first->set_anchors_and_offsets_preset(Control::PRESET_TOP_LEFT);
	first->set_size(Size2(120, 32));
	root->add_child(first);

	Button *second = memnew(Button);
	second->set_text("Duplicate");
	second->set_anchors_and_offsets_preset(Control::PRESET_TOP_LEFT);
	second->set_size(Size2(120, 32));
	root->add_child(second);
	mcp_flush_frames();

	EditorAutomationMCPDispatcher dispatcher;
	EditorAutomationMCPDispatcher::Options options;
	options.snapshot_root = root;
	dispatcher.set_options(options);

	Dictionary selector;
	selector["role"] = "button";
	selector["name"] = "Duplicate";

	Dictionary wait;
	wait["type"] = "editor_idle";

	Dictionary arguments;
	arguments["action"] = "click";
	arguments["selector"] = selector;
	arguments["wait"] = wait;

	Dictionary params;
	params["name"] = "act";
	params["arguments"] = arguments;

	const Dictionary response = dispatcher.handle_message(make_request(36, "tools/call", params));
	const Dictionary response_result = response["result"];
	const Dictionary structured = response_result["structuredContent"];
	CHECK((bool)response_result["isError"]);
	CHECK(structured["kind"] == "action_failed");
	CHECK(structured.has("action"));
	CHECK_FALSE(structured.has("wait"));
	CHECK_FALSE(EditorAutomationWait::has_pending_cooperative());

	EditorAutomationWait::clear_all_cooperative();
	memdelete(root);
}

TEST_CASE("[Editor][Automation][MCP] synchronous wait_for regression via cooperative=false") {
	EditorAutomationWait::clear_all_cooperative();

	PanelContainer *root = memnew(PanelContainer);
	root->set_size(Size2(400, 300));
	SceneTree::get_singleton()->get_root()->add_child(root);

	Button *button = memnew(Button);
	button->set_text("Appear Later");
	button->set_visible(false);
	button->set_anchors_and_offsets_preset(Control::PRESET_TOP_LEFT);
	button->set_size(Size2(120, 32));
	root->add_child(button);
	mcp_flush_frames();

	EditorAutomationMCPDispatcher dispatcher;
	EditorAutomationMCPDispatcher::Options options;
	options.snapshot_root = root;
	dispatcher.set_options(options);

	Dictionary wait_args;
	wait_args["condition"] = "selector_appears";
	Dictionary selector;
	selector["role"] = "button";
	selector["name"] = "Appear Later";
	selector["visible"] = true;
	wait_args["selector"] = selector;
	wait_args["timeout_ms"] = 1000;
	wait_args["cooperative"] = false;

	button->set_visible(true);
	mcp_flush_frames();

	Dictionary wait_params;
	wait_params["name"] = "wait_for";
	wait_params["arguments"] = wait_args;
	const Dictionary response = dispatcher.handle_message(make_request(37, "tools/call", wait_params));
	const Dictionary response_result = response["result"];
	const Dictionary structured = response_result["structuredContent"];
	CHECK((bool)structured["ok"]);
	CHECK_FALSE(structured.has("status"));

	EditorAutomationWait::clear_all_cooperative();
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

TEST_CASE("[Editor][Automation][MCP] synchronous wait_for over HTTP survives nested poll") {
	EditorAutomationMCPServer server;
	server.set_token("reentrancy-token");
	REQUIRE(server.listen(0, IPAddress("127.0.0.1")) == OK);
	const int port = server.get_port();

	Ref<StreamPeerTCP> client;
	client.instantiate();
	REQUIRE(client->connect_to_host(IPAddress("127.0.0.1"), port) == OK);

	const uint64_t connect_deadline = OS::get_singleton()->get_ticks_usec() + 2000000;
	while (client->poll() == OK && client->get_status() == StreamPeerTCP::STATUS_CONNECTING && OS::get_singleton()->get_ticks_usec() < connect_deadline) {
		OS::get_singleton()->delay_usec(1000);
	}
	REQUIRE(client->get_status() == StreamPeerTCP::STATUS_CONNECTED);

	Dictionary wait_args;
	wait_args["condition"] = "next_frame";
	wait_args["timeout_ms"] = 1000;
	wait_args["cooperative"] = false;
	Dictionary call_params;
	call_params["name"] = "wait_for";
	call_params["arguments"] = wait_args;
	const String json_body = JSON::stringify(make_request(39, "tools/call", call_params), "", false);
	const CharString body_utf8 = json_body.utf8();

	String request_text = "POST /mcp HTTP/1.1\r\n";
	request_text += "Host: 127.0.0.1\r\n";
	request_text += "Authorization: Bearer reentrancy-token\r\n";
	request_text += "Content-Type: application/json\r\n";
	request_text += vformat("Content-Length: %d\r\n", body_utf8.length());
	request_text += "\r\n";
	request_text += json_body;
	const CharString request_utf8 = request_text.utf8();
	REQUIRE(client->put_data((const uint8_t *)request_utf8.get_data(), request_utf8.length()) == OK);

	String response_text;
	const uint64_t response_deadline = OS::get_singleton()->get_ticks_usec() + 5000000;
	while (OS::get_singleton()->get_ticks_usec() < response_deadline) {
		// Frame pumps during synchronous wait_for re-enter poll(); the guard must
		// keep the in-flight request from being processed twice.
		server.poll();
		server.poll();
		if (SceneTree::get_singleton() != nullptr) {
			SceneTree::get_singleton()->process(1.0 / 60.0);
			MessageQueue::get_singleton()->flush();
		}
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
		if (response_text.contains("\r\n\r\n") && response_text.contains("\"ok\":true")) {
			break;
		}
		OS::get_singleton()->delay_usec(2000);
	}

	CHECK(response_text.contains("HTTP/1.1 200"));
	CHECK(response_text.contains("\"ok\":true"));

	client->disconnect_from_host();
	server.stop();
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

TEST_CASE("[Editor][Automation][MCP] palette command without a key binding is still runnable") {
	// Regression for the MVP acceptance workflow: shortcut-backed palette
	// commands such as docks/open_inspector are registered via
	// ED_SHORTCUT_AND_COMMAND with no default key. EditorCommandPalette runs
	// their callable directly (a push of InputEventShortcut resolved by
	// shortcut identity), so run_command must execute them instead of
	// rejecting them as "disabled" just because the shortcut has no key.
	EditorCommandPalette *palette = EditorCommandPalette::get_singleton();
	REQUIRE(palette != nullptr);

	AutomationCommandProbe probe;
	Ref<Shortcut> unbound_shortcut;
	unbound_shortcut.instantiate();
	unbound_shortcut->set_name("Unbound Automation Shortcut");
	palette->add_command("Unbound Automation Command", "automation/unbound_command", callable_mp(&probe, &AutomationCommandProbe::mark), varray(), unbound_shortcut);

	EditorAutomationMCPDispatcher dispatcher;

	Dictionary list_params;
	list_params["name"] = "list_commands";
	list_params["arguments"] = Dictionary();
	const Dictionary list_response = dispatcher.handle_message(make_request(25, "tools/call", list_params));
	const Dictionary list_result = list_response.get("result", Dictionary());
	const Dictionary list_structured = list_result.get("structuredContent", Dictionary());
	const Dictionary entry = command_entry_for_key(list_structured.get("commands", Array()), "automation/unbound_command");
	CHECK((bool)entry.get("enabled", false) == true);
	CHECK((bool)entry.get("runnable_by_run_command", false) == true);
	// The missing key binding is reported as metadata, not as a blocker.
	CHECK((bool)entry.get("has_shortcut_binding", true) == false);

	Dictionary run_params;
	run_params["name"] = "run_command";
	Dictionary run_args;
	run_args["command"] = "automation/unbound_command";
	run_params["arguments"] = run_args;
	const Dictionary run_response = dispatcher.handle_message(make_request(26, "tools/call", run_params));
	const Dictionary run_result = run_response.get("result", Dictionary());
	CHECK_FALSE((bool)run_result.get("isError", true));
	const Dictionary run_structured = run_result.get("structuredContent", Dictionary());
	CHECK((bool)run_structured.get("ok", false) == true);
	CHECK(String(run_structured.get("route", String())) == "command_palette");
	CHECK(probe.executed);

	palette->remove_command("automation/unbound_command");
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

TEST_CASE("[Editor][Automation][MCP] tool schemas expose typed contracts") {
	const Array tools = EditorAutomationMCPDispatcher::build_tools_list();
	REQUIRE(tools.size() == 9);

	const Dictionary act = tool_named(tools, "act");
	REQUIRE_FALSE(act.is_empty());
	const Dictionary act_input = act["inputSchema"];
	const Dictionary act_props = act_input["properties"];
	const Dictionary action_schema = act_props["action"];
	CHECK(action_schema.has("enum"));
	const Dictionary route_schema = act_props["route"];
	CHECK(route_schema.has("enum"));

	const Dictionary wait_for = tool_named(tools, "wait_for");
	const Dictionary wait_props = ((Dictionary)wait_for["inputSchema"])["properties"];
	CHECK(wait_props.has("condition"));
	CHECK(wait_props.has("selector"));

	const Dictionary observe = tool_named(tools, "observe_ui");
	const Dictionary observe_output = observe["outputSchema"];
	CHECK(observe_output.has("properties"));
}

TEST_CASE("[Editor][Automation][MCP] typed contract schemas expose stable fields") {
	const Array tools = EditorAutomationMCPContracts::build_tools_list();
	REQUIRE(tools.size() == 9);

	const Dictionary act = tool_named(tools, "act");
	REQUIRE_FALSE(act.is_empty());
	const Dictionary input = act["inputSchema"];
	const Dictionary props = input["properties"];
	CHECK(props.has("selector"));
	CHECK(props.has("action"));
	CHECK(props.has("args"));
	CHECK(props.has("wait"));
	const Dictionary action_schema = props["action"];
	CHECK(action_schema.has("enum"));
}

TEST_CASE("[Editor][Automation][MCP] tool schemas include agent-facing descriptions") {
	const Array tools = EditorAutomationMCPContracts::build_tools_list();
	REQUIRE(tools.size() == 9);

	for (int i = 0; i < tools.size(); i++) {
		const Dictionary tool = tools[i];
		CHECK_FALSE(String(tool.get("description", String())).is_empty());

		const Dictionary input_schema = tool.get("inputSchema", Dictionary());
		const Dictionary output_schema = tool.get("outputSchema", Dictionary());
		CHECK_FALSE(String(input_schema.get("description", String())).is_empty());
		CHECK_FALSE(String(output_schema.get("description", String())).is_empty());
		check_direct_schema_property_descriptions(input_schema);
		check_direct_schema_property_descriptions(output_schema);
	}

	const Dictionary act = tool_named(tools, "act");
	REQUIRE_FALSE(act.is_empty());
	const Dictionary act_input = act["inputSchema"];
	CHECK(String(act_input.get("description", String())).contains("wait_id"));
	if (act_input.has("required")) {
		const Array act_required = act_input["required"];
		CHECK_FALSE(act_required.has("action"));
	}

	const Dictionary observe = tool_named(tools, "observe_ui");
	REQUIRE_FALSE(observe.is_empty());
	const Dictionary observe_input = observe["inputSchema"];
	CHECK(String(observe_input.get("description", String())).contains("subtree_cursor"));

	const Array resources = EditorAutomationMCPContracts::build_resources_list();
	REQUIRE(resources.size() == 6);
	for (int i = 0; i < resources.size(); i++) {
		const Dictionary resource = resources[i];
		CHECK_FALSE(String(resource.get("description", String())).is_empty());
	}

	const Array templates = EditorAutomationMCPContracts::build_resource_templates_list();
	REQUIRE(templates.size() == 4);
	for (int i = 0; i < templates.size(); i++) {
		const Dictionary resource_template = templates[i];
		CHECK_FALSE(String(resource_template.get("description", String())).is_empty());
	}
}

TEST_CASE("[Editor][Automation][MCP] typed tool inputs reject wrong argument types") {
	Dictionary bad_find;
	bad_find["selector"] = "button";
	const EditorAutomationMCPParseResult<EditorAutomationMCPFindElementsInput> find_result =
			EditorAutomationMCPFindElementsInput::parse(bad_find);
	CHECK_FALSE(find_result.ok);
	CHECK(find_result.error.field == "selector");

	Dictionary bad_act;
	bad_act["action"] = 42;
	const EditorAutomationMCPParseResult<EditorAutomationMCPActInput> act_result =
			EditorAutomationMCPActInput::parse(bad_act);
	CHECK_FALSE(act_result.ok);
	CHECK(act_result.error.field == "action");
}

TEST_CASE("[Editor][Automation][MCP] typed tool input parsers cover all tools") {
	Dictionary bad_observe;
	bad_observe["max_depth"] = "deep";
	const EditorAutomationMCPParseResult<EditorAutomationMCPObserveUIInput> observe_result =
			EditorAutomationMCPObserveUIInput::parse(bad_observe);
	CHECK_FALSE(observe_result.ok);
	CHECK(observe_result.error.field == "max_depth");

	const EditorAutomationMCPParseResult<EditorAutomationMCPWaitForInput> wait_result =
			EditorAutomationMCPWaitForInput::parse(Dictionary());
	CHECK_FALSE(wait_result.ok);
	CHECK(wait_result.error.field == "condition");

	Dictionary bad_log;
	bad_log["since"] = 7;
	const EditorAutomationMCPParseResult<EditorAutomationMCPReadEditorLogInput> log_result =
			EditorAutomationMCPReadEditorLogInput::parse(bad_log);
	CHECK_FALSE(log_result.ok);
	CHECK(log_result.error.field == "since");

	const EditorAutomationMCPParseResult<EditorAutomationMCPRunCommandInput> run_result =
			EditorAutomationMCPRunCommandInput::parse(Dictionary());
	CHECK_FALSE(run_result.ok);
	CHECK(run_result.error.field == "command");

	Dictionary bad_list;
	bad_list["limit"] = "many";
	const EditorAutomationMCPParseResult<EditorAutomationMCPListCommandsInput> list_result =
			EditorAutomationMCPListCommandsInput::parse(bad_list);
	CHECK_FALSE(list_result.ok);
	CHECK(list_result.error.field == "limit");

	Dictionary bad_poll;
	bad_poll["kinds"] = 12;
	const EditorAutomationMCPParseResult<EditorAutomationMCPPollEventsInput> poll_result =
			EditorAutomationMCPPollEventsInput::parse(bad_poll);
	CHECK_FALSE(poll_result.ok);
	CHECK(poll_result.error.field == "kinds");

	const EditorAutomationMCPParseResult<EditorAutomationMCPReadEditorStateInput> state_result =
			EditorAutomationMCPReadEditorStateInput::parse(Dictionary());
	CHECK(state_result.ok);
}

TEST_CASE("[Editor][Automation][MCP] typed element output serializes stable fields") {
	EditorAutomationElement element;
	element.id = "snapshot:1/object:2";
	element.handle = "object:2";
	element.role = "button";
	element.name = "Run";
	element.text = "Run";
	element.class_name = "Button";
	element.path = "/root/Button";
	element.visible = true;
	element.enabled = true;
	element.focused = false;
	element.pressed = false;
	element.selected = false;
	element.bounds = Rect2(10, 20, 30, 40);
	element.actions.push_back("click");

	const EditorAutomationMCPElementNode node = EditorAutomationMCPElementNode::from_element(element);
	const Dictionary serialized = node.to_dictionary();
	CHECK(String(serialized["id"]) == "snapshot:1/object:2");
	CHECK(String(serialized["class"]) == "Button");
	const Array bounds = serialized["bounds"];
	REQUIRE(bounds.size() == 4);
	CHECK((int)bounds[0] == 10);
	CHECK((int)bounds[3] == 40);
	const Array actions = serialized["actions"];
	REQUIRE(actions.size() == 1);
	CHECK(String(actions[0]) == "click");
}

TEST_CASE("[Editor][Automation][MCP] tools/call reports invalid params for wrong argument types") {
	EditorAutomationMCPDispatcher dispatcher;

	Dictionary arguments;
	arguments["selector"] = "button";
	Dictionary params;
	params["name"] = "find_elements";
	params["arguments"] = arguments;

	const Dictionary response = dispatcher.handle_message(make_request(50, "tools/call", params));
	CHECK(response.has("error"));
	const Dictionary error = response["error"];
	CHECK((int)error["code"] == EditorAutomationMCPDispatcher::INVALID_PARAMS);
	CHECK(String(error["message"]).contains("selector"));
}

TEST_CASE("[Editor][Automation][MCP] resources/templates/list exposes targeted URI templates") {
	EditorAutomationMCPDispatcher dispatcher;
	const Dictionary response = dispatcher.handle_message(make_request(40, "resources/templates/list"));
	CHECK(response.has("result"));
	const Dictionary templates_result = response["result"];
	const Array templates = templates_result["resourceTemplates"];
	CHECK(templates.size() == 4);
	PackedStringArray patterns;
	for (int i = 0; i < templates.size(); i++) {
		const Dictionary entry = templates[i];
		patterns.push_back(entry.get("uriTemplate", String()));
	}
	CHECK(patterns.has("foundry://element/{id}"));
	CHECK(patterns.has("foundry://ui/subtree/{id}"));
	CHECK(patterns.has("foundry://scene/tree"));
}

TEST_CASE("[Editor][Automation][MCP] observe_ui paginates large child lists via subtree cursors") {
	PanelContainer *root = mcp_make_large_button_tree(40);

	EditorAutomationMCPDispatcher dispatcher;
	EditorAutomationMCPDispatcher::Options options;
	options.snapshot_root = root;
	dispatcher.set_options(options);

	Dictionary arguments;
	arguments["max_children"] = 5;
	arguments["max_depth"] = 2;
	Dictionary params;
	params["name"] = "observe_ui";
	params["arguments"] = arguments;
	const Dictionary response = dispatcher.handle_message(make_request(41, "tools/call", params));
	const Dictionary result = response["result"];
	const Dictionary structured = result["structuredContent"];
	const Array tree = structured["tree"];
	REQUIRE(tree.size() == 1);
	const Dictionary root_element = tree[0];
	CHECK((bool)root_element["children_truncated"]);
	CHECK(root_element.has("children_next_cursor"));

	String cursor = root_element["children_next_cursor"];
	int pages = 1;
	while (!cursor.is_empty() && pages < 20) {
		Dictionary page_args;
		page_args["subtree_cursor"] = cursor;
		Dictionary page_params;
		page_params["name"] = "observe_ui";
		page_params["arguments"] = page_args;
		const Dictionary page_response = dispatcher.handle_message(make_request(41 + pages, "tools/call", page_params));
		const Dictionary page_result = page_response["result"];
		const Dictionary page_structured = page_result["structuredContent"];
		CHECK(page_structured.has("subtree"));
		const Dictionary subtree = page_structured["subtree"];
		const Array children = subtree["children"];
		CHECK(children.size() > 0);
		cursor = subtree.get("children_next_cursor", String());
		pages++;
	}
	CHECK(pages >= 8);

	memdelete(root);
}

static Dictionary mcp_find_tree_element_by_class(const Dictionary &p_element, const String &p_class) {
	if (String(p_element.get("class", String())) == p_class) {
		return p_element;
	}
	const Array children = p_element.get("children", Array());
	for (int i = 0; i < children.size(); i++) {
		const Dictionary found = mcp_find_tree_element_by_class(children[i], p_class);
		if (!found.is_empty()) {
			return found;
		}
	}
	return Dictionary();
}

TEST_CASE("[Editor][Automation][MCP] observe_ui include_internal argument exposes flagged internals") {
	PanelContainer *root = memnew(PanelContainer);
	root->set_size(Size2(400, 300));
	SceneTree::get_singleton()->get_root()->add_child(root);

	SpinBox *spin_box = memnew(SpinBox);
	spin_box->set_name("Amount");
	spin_box->set_anchors_and_offsets_preset(Control::PRESET_TOP_LEFT);
	spin_box->set_size(Size2(120, 32));
	root->add_child(spin_box);
	mcp_flush_frames();

	EditorAutomationMCPDispatcher dispatcher;
	EditorAutomationMCPDispatcher::Options options;
	options.snapshot_root = root;
	dispatcher.set_options(options);

	// The tool schema advertises the new argument.
	const Dictionary tools_response = dispatcher.handle_message(make_request(60, "tools/list"));
	const Dictionary tools_result = tools_response["result"];
	const Dictionary observe_tool = tool_named(tools_result["tools"], "observe_ui");
	REQUIRE_FALSE(observe_tool.is_empty());
	const Dictionary observe_schema = observe_tool["inputSchema"];
	const Dictionary observe_props = observe_schema["properties"];
	CHECK(observe_props.has("include_internal"));

	SUBCASE("default snapshot hides the SpinBox internal LineEdit") {
		Dictionary params;
		params["name"] = "observe_ui";
		params["arguments"] = Dictionary();
		const Dictionary response = dispatcher.handle_message(make_request(61, "tools/call", params));
		const Dictionary result = response["result"];
		CHECK_FALSE((bool)result["isError"]);
		const Dictionary structured = result["structuredContent"];
		const Dictionary limits = structured["limits"];
		CHECK_FALSE((bool)limits["include_internal"]);
		const Array tree = structured["tree"];
		REQUIRE(tree.size() == 1);
		CHECK(mcp_find_tree_element_by_class(tree[0], "SpinBox").has("id"));
		CHECK(mcp_find_tree_element_by_class(tree[0], "SpinBoxLineEdit").is_empty());
		// The serialized JSON never mentions the internal flag by default.
		const Array content = result["content"];
		const Dictionary first = content[0];
		CHECK_FALSE(String(first["text"]).contains("\"internal\":true"));
	}

	SUBCASE("include_internal exposes the LineEdit flagged internal") {
		Dictionary arguments;
		arguments["include_internal"] = true;
		Dictionary params;
		params["name"] = "observe_ui";
		params["arguments"] = arguments;
		const Dictionary response = dispatcher.handle_message(make_request(62, "tools/call", params));
		const Dictionary result = response["result"];
		CHECK_FALSE((bool)result["isError"]);
		const Dictionary structured = result["structuredContent"];
		const Dictionary limits = structured["limits"];
		CHECK((bool)limits["include_internal"]);
		const Array tree = structured["tree"];
		REQUIRE(tree.size() == 1);
		const Dictionary line_edit = mcp_find_tree_element_by_class(tree[0], "SpinBoxLineEdit");
		REQUIRE(line_edit.has("id"));
		CHECK((bool)line_edit["internal"]);
		CHECK(String(line_edit["role"]) == "text_field");
	}

	memdelete(root);
}

TEST_CASE("[Editor][Automation][MCP] subtree cursors preserve include_internal across pages") {
	PanelContainer *root = memnew(PanelContainer);
	root->set_size(Size2(400, 300));
	SceneTree::get_singleton()->get_root()->add_child(root);
	for (int i = 0; i < 3; i++) {
		Button *button = memnew(Button);
		button->set_text(vformat("Bulk %d", i));
		button->set_anchors_and_offsets_preset(Control::PRESET_TOP_LEFT);
		button->set_size(Size2(120, 32));
		root->add_child(button);
	}
	SpinBox *spin_box = memnew(SpinBox);
	spin_box->set_name("Amount");
	spin_box->set_anchors_and_offsets_preset(Control::PRESET_TOP_LEFT);
	spin_box->set_size(Size2(120, 32));
	root->add_child(spin_box);
	mcp_flush_frames();

	EditorAutomationMCPDispatcher dispatcher;
	EditorAutomationMCPDispatcher::Options options;
	options.snapshot_root = root;
	dispatcher.set_options(options);

	Dictionary arguments;
	arguments["include_internal"] = true;
	arguments["max_children"] = 2;
	arguments["max_depth"] = 4;
	Dictionary params;
	params["name"] = "observe_ui";
	params["arguments"] = arguments;
	const Dictionary response = dispatcher.handle_message(make_request(63, "tools/call", params));
	const Dictionary result = response["result"];
	const Dictionary structured = result["structuredContent"];
	const Array tree = structured["tree"];
	REQUIRE(tree.size() == 1);
	const Dictionary root_element = tree[0];
	CHECK((bool)root_element["children_truncated"]);
	REQUIRE(root_element.has("children_next_cursor"));

	// The SpinBox (and its internal LineEdit) live on a later page; following
	// the cursor must keep the internal-child expansion from the first call.
	String cursor = root_element["children_next_cursor"];
	bool found_internal_line_edit = false;
	int pages = 0;
	while (!cursor.is_empty() && pages < 10) {
		Dictionary page_args;
		page_args["subtree_cursor"] = cursor;
		Dictionary page_params;
		page_params["name"] = "observe_ui";
		page_params["arguments"] = page_args;
		const Dictionary page_response = dispatcher.handle_message(make_request(64 + pages, "tools/call", page_params));
		const Dictionary page_result = page_response["result"];
		CHECK_FALSE((bool)page_result["isError"]);
		const Dictionary page_structured = page_result["structuredContent"];
		const Dictionary page_limits = page_structured["limits"];
		CHECK((bool)page_limits["include_internal"]);
		const Dictionary subtree = page_structured["subtree"];
		const Dictionary line_edit = mcp_find_tree_element_by_class(subtree, "SpinBoxLineEdit");
		if (line_edit.has("id") && (bool)line_edit.get("internal", false)) {
			found_internal_line_edit = true;
		}
		cursor = subtree.get("children_next_cursor", String());
		pages++;
	}
	CHECK(found_internal_line_edit);

	memdelete(root);
}

TEST_CASE("[Editor][Automation][MCP] find_elements paginates large match sets") {
	PanelContainer *root = mcp_make_large_button_tree(25);

	EditorAutomationMCPDispatcher dispatcher;
	EditorAutomationMCPDispatcher::Options options;
	options.snapshot_root = root;
	dispatcher.set_options(options);

	Dictionary selector;
	selector["role"] = "button";
	selector["name_contains"] = "Bulk";
	Dictionary arguments;
	arguments["selector"] = selector;
	arguments["max_results"] = 10;
	Dictionary params;
	params["name"] = "find_elements";
	params["arguments"] = arguments;

	const Dictionary first_response = dispatcher.handle_message(make_request(60, "tools/call", params));
	const Dictionary first_result = first_response["result"];
	const Dictionary first_structured = first_result["structuredContent"];
	CHECK((bool)first_structured["truncated"]);
	CHECK((int)first_structured["match_count"] == 25);
	CHECK(((Array)first_structured["elements"]).size() == 10);
	const String next_cursor = first_structured["next_cursor"];
	CHECK_FALSE(next_cursor.is_empty());

	Dictionary page_two_args = arguments;
	page_two_args["cursor"] = next_cursor;
	Dictionary page_two_params;
	page_two_params["name"] = "find_elements";
	page_two_params["arguments"] = page_two_args;
	const Dictionary second_response = dispatcher.handle_message(make_request(61, "tools/call", page_two_params));
	const Dictionary second_result = second_response["result"];
	const Dictionary second_structured = second_result["structuredContent"];
	CHECK((int)second_structured["match_count"] == 25);
	CHECK(((Array)second_structured["elements"]).size() == 10);

	memdelete(root);
}

TEST_CASE("[Editor][Automation][MCP] targeted element and subtree resources avoid full observe_ui") {
	PanelContainer *root = memnew(PanelContainer);
	root->set_size(Size2(400, 300));
	SceneTree::get_singleton()->get_root()->add_child(root);
	Button *button = memnew(Button);
	button->set_text("Resource Target");
	root->add_child(button);
	mcp_flush_frames();

	EditorAutomationMCPDispatcher dispatcher;
	EditorAutomationMCPDispatcher::Options options;
	options.snapshot_root = root;
	dispatcher.set_options(options);

	Dictionary observe_params;
	observe_params["name"] = "observe_ui";
	observe_params["arguments"] = Dictionary();
	const Dictionary observe_response = dispatcher.handle_message(make_request(70, "tools/call", observe_params));
	const Dictionary observe_result = observe_response["result"];
	const Dictionary observe_structured = observe_result["structuredContent"];
	const Array observe_tree = observe_structured["tree"];
	const Dictionary root_element = observe_tree[0];
	const Array children = root_element["children"];
	REQUIRE(children.size() >= 1);
	const Dictionary button_element = children[0];
	const String element_handle = button_element["handle"];
	CHECK_FALSE(element_handle.is_empty());

	Dictionary element_read;
	element_read["uri"] = vformat("foundry://element/%s", element_handle);
	const Dictionary element_response = dispatcher.handle_message(make_request(71, "resources/read", element_read));
	CHECK(element_response.has("result"));
	const Dictionary element_result = element_response["result"];
	const Array element_contents = element_result["contents"];
	const Dictionary element_content = element_contents[0];
	const String element_text = element_content["text"];
	CHECK(element_text.contains("Resource Target"));

	Dictionary subtree_read;
	subtree_read["uri"] = vformat("foundry://ui/subtree/%s/depth/2", element_handle);
	const Dictionary subtree_response = dispatcher.handle_message(make_request(72, "resources/read", subtree_read));
	CHECK(subtree_response.has("result"));
	const Dictionary subtree_result = subtree_response["result"];
	const Array subtree_contents = subtree_result["contents"];
	const Dictionary subtree_content = subtree_contents[0];
	const String subtree_text = subtree_content["text"];
	CHECK(subtree_text.contains("subtree"));

	Dictionary scene_read;
	scene_read["uri"] = "foundry://scene/tree";
	const Dictionary scene_response = dispatcher.handle_message(make_request(73, "resources/read", scene_read));
	CHECK(scene_response.has("result"));

	memdelete(root);
}

TEST_CASE("[Editor][Automation][MCP] poll_events exposes queued editor log notifications") {
	EditorAutomationLog::clear_test_messages();
	EditorAutomationEvents::clear_test_events();
	EditorAutomationEvents::reset();

	EditorAutomationLog::push_test_message("Synthetic warning", "warning");
	EditorAutomationLog::push_test_message("Synthetic error", "error");

	EditorAutomationMCPDispatcher dispatcher;
	Dictionary params;
	params["name"] = "poll_events";
	params["arguments"] = Dictionary();
	const Dictionary response = dispatcher.handle_message(make_request(80, "tools/call", params));
	const Dictionary result = response["result"];
	const Dictionary structured = result["structuredContent"];
	const Array events = structured["events"];
	CHECK(events.size() >= 2);
	CHECK(structured.has("transport"));
	CHECK_FALSE((bool)((Dictionary)structured["transport"])["server_push"]);

	EditorAutomationLog::clear_test_messages();
	EditorAutomationEvents::clear_test_events();
}

TEST_CASE("[Editor][Automation][MCP] initialize/list/call/read compatibility regression") {
	EditorAutomationMCPDispatcher dispatcher;

	Dictionary init_params;
	init_params["protocolVersion"] = EditorAutomationMCPDispatcher::PROTOCOL_VERSION;
	CHECK(dispatcher.handle_message(make_request(90, "initialize", init_params)).has("result"));
	CHECK(dispatcher.handle_message(make_request(91, "tools/list")).has("result"));
	CHECK(dispatcher.handle_message(make_request(92, "resources/list")).has("result"));
	CHECK(dispatcher.handle_message(make_request(93, "resources/templates/list")).has("result"));

	Dictionary read_params;
	read_params["uri"] = "foundry://editor/state";
	CHECK(dispatcher.handle_message(make_request(94, "resources/read", read_params)).has("result"));

	Dictionary state_call;
	state_call["name"] = "read_editor_state";
	state_call["arguments"] = Dictionary();
	CHECK(dispatcher.handle_message(make_request(95, "tools/call", state_call)).has("result"));
}

} // namespace TestEditorAutomationMCP
