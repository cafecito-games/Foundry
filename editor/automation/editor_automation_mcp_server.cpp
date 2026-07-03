/**************************************************************************/
/*  editor_automation_mcp_server.cpp                                      */
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

#include "editor_automation_mcp_server.h"

#include "core/io/json.h"
#include "core/os/os.h"

EditorAutomationMCPServer::EditorAutomationMCPServer() {
	server.instantiate();
}

EditorAutomationMCPServer::~EditorAutomationMCPServer() {
	stop();
}

bool EditorAutomationMCPServer::_is_local_origin(const String &p_origin) {
	if (p_origin.is_empty()) {
		// Absence of Origin is handled by the caller; this helper only judges a
		// present value.
		return true;
	}
	// Sandboxed/opaque origins report "null" and must be rejected.
	if (p_origin == "null") {
		return false;
	}

	String rest = p_origin;
	const int scheme_pos = rest.find("://");
	if (scheme_pos < 0) {
		return false;
	}
	const String scheme = rest.substr(0, scheme_pos).to_lower();
	if (scheme != "http" && scheme != "https") {
		return false;
	}
	rest = rest.substr(scheme_pos + 3);

	// Strip any path component.
	const int slash_pos = rest.find_char('/');
	if (slash_pos >= 0) {
		rest = rest.substr(0, slash_pos);
	}

	String host = rest;
	if (host.begins_with("[")) {
		// IPv6 literal, e.g. [::1]:8080.
		const int close = host.find_char(']');
		if (close < 0) {
			return false;
		}
		host = host.substr(1, close - 1);
	} else {
		const int colon_pos = host.find_char(':');
		if (colon_pos >= 0) {
			host = host.substr(0, colon_pos);
		}
	}
	host = host.to_lower();

	return host == "localhost" || host == "127.0.0.1" || host == "::1";
}

EditorAutomationMCPServer::HTTPResponse EditorAutomationMCPServer::_make_json_response(int p_status, const String &p_reason, const String &p_json_body) {
	HTTPResponse response;
	response.status = p_status;
	response.reason = p_reason;
	response.content_type = "application/json";
	response.body = p_json_body;
	return response;
}

EditorAutomationMCPServer::HTTPResponse EditorAutomationMCPServer::process_http_request(const HTTPRequest &p_request) {
	// 1. Origin validation to prevent DNS rebinding.
	if (p_request.has_header("origin")) {
		const String origin = p_request.get_header("origin");
		if (!_is_local_origin(origin)) {
			Dictionary error;
			error["error"] = "forbidden_origin";
			error["message"] = "Origin is not allowed.";
			return _make_json_response(403, "Forbidden", JSON::stringify(error, "", false));
		}
	}

	// 2. Only POST carries JSON-RPC payloads. GET (SSE stream) is unsupported.
	if (p_request.method != "POST") {
		Dictionary error;
		error["error"] = "method_not_allowed";
		error["message"] = "Only POST is supported.";
		return _make_json_response(405, "Method Not Allowed", JSON::stringify(error, "", false));
	}

	// 3. Authentication: bearer token on every request.
	const String authorization = p_request.get_header("authorization");
	const String expected = "Bearer " + token;
	if (token.is_empty() || authorization != expected) {
		Dictionary error;
		error["error"] = "unauthorized";
		error["message"] = "Missing or invalid bearer token.";
		return _make_json_response(401, "Unauthorized", JSON::stringify(error, "", false));
	}

	// 4. Parse the JSON-RPC payload.
	JSON json;
	const Error parse_error = json.parse(p_request.body);
	if (parse_error != OK) {
		Dictionary error;
		error["code"] = EditorAutomationMCPDispatcher::PARSE_ERROR;
		error["message"] = "Parse error.";
		Dictionary response;
		response["jsonrpc"] = "2.0";
		response["id"] = Variant();
		response["error"] = error;
		return _make_json_response(200, "OK", JSON::stringify(response, "", false));
	}

	const Variant data = json.get_data();

	// 5a. Batch requests.
	if (data.get_type() == Variant::ARRAY) {
		const Array batch = data;
		Array responses;
		for (int i = 0; i < batch.size(); i++) {
			if (batch[i].get_type() != Variant::DICTIONARY) {
				Dictionary error;
				error["code"] = EditorAutomationMCPDispatcher::INVALID_REQUEST;
				error["message"] = "Batch entries must be objects.";
				Dictionary response;
				response["jsonrpc"] = "2.0";
				response["id"] = Variant();
				response["error"] = error;
				responses.push_back(response);
				continue;
			}
			bool has_response = false;
			const Dictionary response = dispatcher.handle_message(batch[i], has_response);
			if (has_response) {
				responses.push_back(response);
			}
		}
		if (responses.is_empty()) {
			return _make_json_response(202, "Accepted", String());
		}
		return _make_json_response(200, "OK", JSON::stringify(responses, "", false));
	}

	// 5b. Single request or notification.
	if (data.get_type() == Variant::DICTIONARY) {
		bool has_response = false;
		const Dictionary response = dispatcher.handle_message(data, has_response);
		if (!has_response) {
			return _make_json_response(202, "Accepted", String());
		}
		return _make_json_response(200, "OK", JSON::stringify(response, "", false));
	}

	Dictionary error;
	error["code"] = EditorAutomationMCPDispatcher::INVALID_REQUEST;
	error["message"] = "Invalid JSON-RPC request.";
	Dictionary response;
	response["jsonrpc"] = "2.0";
	response["id"] = Variant();
	response["error"] = error;
	return _make_json_response(200, "OK", JSON::stringify(response, "", false));
}

Error EditorAutomationMCPServer::listen(int p_port, const IPAddress &p_bind_ip) {
	ERR_FAIL_COND_V(server.is_null(), ERR_UNCONFIGURED);
	if (listening) {
		return ERR_ALREADY_IN_USE;
	}
	const Error err = server->listen(p_port, p_bind_ip);
	if (err != OK) {
		return err;
	}
	bound_port = server->get_local_port();
	listening = true;
	return OK;
}

void EditorAutomationMCPServer::_reset_connection() {
	connection = Ref<StreamPeerTCP>();
	req_buf.clear();
	header_end = -1;
	content_length = -1;
	connection_time = 0;
}

void EditorAutomationMCPServer::_accept_connection() {
	if (!server->is_connection_available()) {
		return;
	}
	connection = server->take_connection();
	req_buf.clear();
	header_end = -1;
	content_length = -1;
	connection_time = OS::get_singleton()->get_ticks_usec();
}

bool EditorAutomationMCPServer::_parse_headers(HTTPRequest &r_request) {
	// header_end points at the byte after the terminating \r\n\r\n.
	const String header_text = String::utf8((const char *)req_buf.ptr(), header_end);
	Vector<String> lines = header_text.split("\r\n");
	if (lines.is_empty()) {
		return false;
	}

	Vector<String> request_line = lines[0].split(" ", false);
	if (request_line.size() < 2) {
		return false;
	}
	r_request.method = request_line[0].to_upper();
	String target = request_line[1];
	const int query_pos = target.find_char('?');
	r_request.path = query_pos >= 0 ? target.substr(0, query_pos) : target;

	for (int i = 1; i < lines.size(); i++) {
		const String &line = lines[i];
		if (line.is_empty()) {
			continue;
		}
		const int colon = line.find_char(':');
		if (colon < 0) {
			continue;
		}
		const String key = line.substr(0, colon).strip_edges().to_lower();
		const String value = line.substr(colon + 1).strip_edges();
		r_request.headers.insert(key, value);
	}
	return true;
}

void EditorAutomationMCPServer::_finish_request() {
	HTTPRequest request;
	if (!_parse_headers(request)) {
		HTTPResponse bad;
		bad.status = 400;
		bad.reason = "Bad Request";
		bad.body = "{\"error\":\"bad_request\"}";
		_send_response(bad);
		return;
	}

	// Body is everything after the header terminator.
	const int body_size = req_buf.size() - header_end;
	if (body_size > 0) {
		request.body = String::utf8((const char *)(req_buf.ptr() + header_end), body_size);
	}

	const HTTPResponse response = process_http_request(request);
	_send_response(response);
}

void EditorAutomationMCPServer::_send_response(const HTTPResponse &p_response) {
	if (connection.is_null()) {
		return;
	}
	const CharString body_utf8 = p_response.body.utf8();
	const int body_length = body_utf8.length();

	String head = vformat("HTTP/1.1 %d %s\r\n", p_response.status, p_response.reason);
	head += "Content-Type: " + p_response.content_type + "\r\n";
	head += vformat("Content-Length: %d\r\n", body_length);
	head += "Connection: close\r\n";
	head += "\r\n";

	const CharString head_utf8 = head.utf8();
	connection->put_data((const uint8_t *)head_utf8.get_data(), head_utf8.length());
	if (body_length > 0) {
		connection->put_data((const uint8_t *)body_utf8.get_data(), body_length);
	}
}

void EditorAutomationMCPServer::poll() {
	if (!listening || server.is_null()) {
		return;
	}

	if (connection.is_null()) {
		_accept_connection();
		if (connection.is_null()) {
			return;
		}
	}

	connection->poll();
	const StreamPeerTCP::Status status = connection->get_status();
	if (status == StreamPeerTCP::STATUS_ERROR || status == StreamPeerTCP::STATUS_NONE) {
		_reset_connection();
		return;
	}
	if (status != StreamPeerTCP::STATUS_CONNECTED) {
		return;
	}

	if (OS::get_singleton()->get_ticks_usec() - connection_time > MCP_REQUEST_TIMEOUT_USEC) {
		_reset_connection();
		return;
	}

	// Drain available bytes into the request buffer.
	int available = connection->get_available_bytes();
	while (available > 0) {
		const int previous_size = req_buf.size();
		if (previous_size + available > MCP_MAX_REQUEST_SIZE) {
			HTTPResponse too_large;
			too_large.status = 413;
			too_large.reason = "Payload Too Large";
			too_large.body = "{\"error\":\"payload_too_large\"}";
			_send_response(too_large);
			_reset_connection();
			return;
		}
		req_buf.resize(previous_size + available);
		int received = 0;
		const Error err = connection->get_partial_data(req_buf.ptrw() + previous_size, available, received);
		if (err != OK) {
			_reset_connection();
			return;
		}
		if (received < available) {
			req_buf.resize(previous_size + received);
		}
		available = connection->get_available_bytes();
	}

	// Locate the end of the headers.
	if (header_end < 0) {
		for (int i = 3; i < req_buf.size(); i++) {
			if (req_buf[i - 3] == '\r' && req_buf[i - 2] == '\n' && req_buf[i - 1] == '\r' && req_buf[i] == '\n') {
				header_end = i + 1;
				break;
			}
		}
		if (header_end < 0) {
			return; // Await more header bytes.
		}

		// Parse Content-Length once headers are complete.
		const String header_text = String::utf8((const char *)req_buf.ptr(), header_end);
		Vector<String> lines = header_text.split("\r\n");
		content_length = 0;
		for (const String &line : lines) {
			if (line.to_lower().begins_with("content-length:")) {
				content_length = line.substr(line.find_char(':') + 1).strip_edges().to_int();
				break;
			}
		}
	}

	// Wait until the full body has arrived.
	const int body_received = req_buf.size() - header_end;
	if (body_received < content_length) {
		return;
	}

	_finish_request();
	_reset_connection();
}

void EditorAutomationMCPServer::stop() {
	if (connection.is_valid()) {
		connection->disconnect_from_host();
	}
	_reset_connection();
	if (server.is_valid() && server->is_listening()) {
		server->stop();
	}
	listening = false;
	bound_port = 0;
}
