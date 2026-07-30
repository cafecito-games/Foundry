/**************************************************************************/
/*  editor_automation_mcp_server.h                                        */
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

#include "editor/automation/editor_automation_mcp_dispatcher.h"

#include "core/io/stream_peer_tcp.h"
#include "core/io/tcp_server.h"
#include "core/templates/hash_map.h"

// Local/test-only MCP Streamable HTTP transport for the running editor.
//
// This class owns the socket, HTTP framing, authentication, and origin
// validation. All protocol/tool logic lives in EditorAutomationMCPDispatcher,
// which this class calls for every request. The dispatcher and the HTTP request
// handler (process_http_request) are both socket-free so they can be unit-tested
// without opening a port.
class EditorAutomationMCPServer {
public:
	static constexpr int MCP_MAX_REQUEST_SIZE = 8 * 1024 * 1024;
	static constexpr uint64_t MCP_REQUEST_TIMEOUT_USEC = 30 * 1000 * 1000;

	// Socket-free HTTP request/response abstraction so auth, origin checks, and
	// dispatch can be tested without a socket.
	struct HTTPRequest {
		String method;
		String path;
		HashMap<String, String> headers; // keys lowercased.
		String body;

		bool has_header(const String &p_key) const { return headers.has(p_key.to_lower()); }
		String get_header(const String &p_key) const {
			const String *value = headers.getptr(p_key.to_lower());
			return value != nullptr ? *value : String();
		}
	};

	struct HTTPResponse {
		int status = 200;
		String reason = "OK";
		String content_type = "application/json";
		String body;
	};

private:
	Ref<TCPServer> server;
	Ref<StreamPeerTCP> connection;

	String token;
	int bound_port = 0;
	bool listening = false;

	EditorAutomationMCPDispatcher dispatcher;

	// Per-connection read state.
	Vector<uint8_t> req_buf;
	int header_end = -1;
	int content_length = -1;
	uint64_t connection_time = 0;
	// True while _finish_request is dispatching a JSON-RPC payload. Frame pumps
	// triggered by synchronous tools/call handlers (for example cooperative=false
	// wait_for) re-enter poll(); without this guard the buffered request would be
	// processed again and the connection reset before the outer response is sent.
	bool request_in_progress = false;

	void _reset_connection();
	void _accept_connection();
	bool _parse_headers(HTTPRequest &r_request);
	void _finish_request();
	void _send_response(const HTTPResponse &p_response);

	static bool _is_local_origin(const String &p_origin);
	static HTTPResponse _make_json_response(int p_status, const String &p_reason, const String &p_json_body);

public:
	EditorAutomationMCPServer();
	~EditorAutomationMCPServer();

	void set_token(const String &p_token) { token = p_token; }
	const String &get_token() const { return token; }
	void set_dispatcher_options(const EditorAutomationMCPDispatcher::Options &p_options) { dispatcher.set_options(p_options); }

	Error listen(int p_port, const IPAddress &p_bind_ip = IPAddress("127.0.0.1"), bool p_reuse_address = true);
	void poll();
	void stop();

	bool is_listening() const { return listening; }
	int get_port() const { return bound_port; }

	// Socket-free entry point: validates origin/auth and dispatches the JSON-RPC
	// payload. Exposed for unit tests.
	HTTPResponse process_http_request(const HTTPRequest &p_request);

	EditorAutomationMCPDispatcher &get_dispatcher() { return dispatcher; }
};
