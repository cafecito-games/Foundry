/**************************************************************************/
/*  editor_automation_mcp_dispatcher.h                                    */
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

#include "core/variant/variant.h"

class Node;

// Socket-free MCP request dispatcher.
//
// This class implements the Model Context Protocol JSON-RPC surface
// (initialize, notifications/initialized, tools/list, tools/call,
// resources/list, resources/read) and routes every tool/resource call to the
// shared editor automation core (snapshot, selector, driver, wait, state, log).
//
// It deliberately owns no transport, no socket, and no editor-workflow logic so
// it can be unit-tested without opening a port. The transport
// (EditorAutomationMCPServer) is responsible for HTTP framing, authentication,
// and origin validation before handing a parsed JSON-RPC message here.
class EditorAutomationMCPDispatcher {
public:
	// JSON-RPC error codes (superset of the standard codes used by MCP).
	enum ErrorCode {
		PARSE_ERROR = -32700,
		INVALID_REQUEST = -32600,
		METHOD_NOT_FOUND = -32601,
		INVALID_PARAMS = -32602,
		INTERNAL_ERROR = -32603,
	};

	struct Options {
		// When set, snapshot/selector/action/wait tools operate on this subtree
		// instead of the live editor. Used by unit tests to inject a synthetic
		// UI without a full editor instance.
		Node *snapshot_root = nullptr;
		double default_wait_timeout_sec = 5.0;
		int max_tree_depth = 8;
	};

private:
	Options options;
	bool initialized = false;
	String negotiated_protocol_version;

	Node *_snapshot_root() const { return options.snapshot_root; }

	// MCP method handlers. Each returns the JSON-RPC "result" value.
	Dictionary _handle_initialize(const Dictionary &p_params, bool &r_ok, Dictionary &r_error);
	Dictionary _handle_tools_call(const Dictionary &p_params, bool &r_ok, Dictionary &r_error);
	Dictionary _handle_resources_read(const Dictionary &p_params, bool &r_ok, Dictionary &r_error);

	// Tool implementations. They return a structured JSON object that becomes
	// the tool's structuredContent; r_is_error flags automation-level failures.
	Dictionary _tool_observe_ui(const Dictionary &p_args, bool &r_is_error);
	Dictionary _tool_find_elements(const Dictionary &p_args, bool &r_is_error);
	Dictionary _tool_act(const Dictionary &p_args, bool &r_is_error);
	Dictionary _tool_wait_for(const Dictionary &p_args, bool &r_is_error);
	Dictionary _tool_read_editor_state(const Dictionary &p_args, bool &r_is_error);

	Dictionary _build_condition_from_args(const Dictionary &p_args);
	Dictionary _wait_context_from_handle(const EditorAutomationCooperativeWaitHandle &p_handle);
	Dictionary _cooperative_wait_response(const EditorAutomationCooperativeWaitHandle &p_handle, bool &r_is_error);
	Dictionary _compose_act_wait_result(
			const Dictionary &p_action_result,
			const EditorAutomationCooperativeWaitHandle &p_handle,
			const Dictionary &p_condition,
			const Dictionary &p_selector,
			const String &p_action,
			const EditorAutomationLogMarker &p_log_marker,
			bool &r_is_error);
	Dictionary _tool_read_editor_log(const Dictionary &p_args, bool &r_is_error);
	Dictionary _tool_run_command(const Dictionary &p_args, bool &r_is_error);
	Dictionary _tool_list_commands(const Dictionary &p_args, bool &r_is_error);

	Dictionary _resource_payload(const String &p_uri, bool &r_ok);

	static Dictionary _make_result(const Variant &p_id, const Variant &p_result);
	static Dictionary _make_error(const Variant &p_id, int p_code, const String &p_message, const Variant &p_data = Variant());
	static Dictionary _tool_result_from_structured(const Dictionary &p_structured, bool p_is_error);

public:
	static const char *PROTOCOL_VERSION;

	void set_options(const Options &p_options) { options = p_options; }
	const Options &get_options() const { return options; }
	bool is_initialized() const { return initialized; }
	void reset();

	// Handles a single parsed JSON-RPC message. Returns the response Dictionary,
	// or an empty Dictionary when the message is a notification (no response).
	// r_has_response is false for notifications.
	Dictionary handle_message(const Dictionary &p_message, bool &r_has_response);

	// Convenience wrapper that always returns a Dictionary (empty for
	// notifications). Useful for tests.
	Dictionary handle_message(const Dictionary &p_message);

	static Array build_tools_list();
	static Array build_resources_list();
};
