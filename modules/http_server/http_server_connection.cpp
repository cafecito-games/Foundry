/**************************************************************************/
/*  http_server_connection.cpp                                            */
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

#include "http_server_connection.h"

#include "picohttpparser.h"

namespace {

// The reason phrase is advisory for clients but part of a well-formed status line, so the common
// codes get their registered text and anything else falls back to a class-wide phrase.
const char *status_reason(int p_status) {
	switch (p_status) {
		case 200:
			return "OK";
		case 201:
			return "Created";
		case 202:
			return "Accepted";
		case 204:
			return "No Content";
		case 301:
			return "Moved Permanently";
		case 302:
			return "Found";
		case 303:
			return "See Other";
		case 304:
			return "Not Modified";
		case 307:
			return "Temporary Redirect";
		case 308:
			return "Permanent Redirect";
		case 400:
			return "Bad Request";
		case 401:
			return "Unauthorized";
		case 403:
			return "Forbidden";
		case 404:
			return "Not Found";
		case 405:
			return "Method Not Allowed";
		case 500:
			return "Internal Server Error";
		case 501:
			return "Not Implemented";
		default:
			break;
	}

	switch (p_status / 100) {
		case 1:
			return "Informational";
		case 2:
			return "Success";
		case 3:
			return "Redirection";
		case 4:
			return "Client Error";
		default:
			return "Server Error";
	}
}

} // namespace

void HTTPServerConnection::accept(const Ref<StreamPeerTCP> &p_stream) {
	stream = p_stream;
	state = stream.is_valid() ? STATE_READING : STATE_CLOSED;
}

void HTTPServerConnection::poll() {
	switch (state) {
		case STATE_READING: {
			if (_read_available()) {
				_parse();
			}
		} break;
		case STATE_WRITING: {
			_flush_write();
		} break;
		default:
			break;
	}
}

bool HTTPServerConnection::_read_available() {
	if (stream.is_null()) {
		close();
		return false;
	}

	stream->poll();
	if (stream->get_status() != StreamPeerTCP::STATUS_CONNECTED) {
		close();
		return false;
	}

	int available = stream->get_available_bytes();
	while (available > 0) {
		const int previous_size = read_buffer.size();
		if (previous_size + available > MAX_HEADER_BYTES) {
			close();
			return false;
		}

		read_buffer.resize_uninitialized(previous_size + available);
		int received = 0;
		const Error err = stream->get_partial_data(read_buffer.ptrw() + previous_size, available, received);
		read_buffer.resize(previous_size + received);
		if (err != OK) {
			close();
			return false;
		}
		if (received == 0) {
			break;
		}

		available = stream->get_available_bytes();
	}

	return true;
}

void HTTPServerConnection::_parse() {
	if (read_buffer.is_empty()) {
		return;
	}

	const char *method = nullptr;
	size_t method_length = 0;
	const char *raw_path = nullptr;
	size_t raw_path_length = 0;
	int minor_version = 0;
	phr_header header_fields[MAX_HEADER_FIELDS];
	size_t header_field_count = MAX_HEADER_FIELDS;

	const int parsed = phr_parse_request(reinterpret_cast<const char *>(read_buffer.ptr()), read_buffer.size(),
			&method, &method_length, &raw_path, &raw_path_length, &minor_version,
			header_fields, &header_field_count, scanned_length);

	if (parsed == -2) {
		// Incomplete: remember how far the parser got so the next pass does not rescan the prefix.
		scanned_length = read_buffer.size();
		return;
	}
	if (parsed < 0) {
		// Malformed request line or header block. Dropping the connection is the whole error
		// handling this layer does; status-coded rejections belong with the request-limit work.
		close();
		return;
	}

	request.instantiate();
	request->set_method(String::utf8(method, method_length));
	request->set_raw_path(String::utf8(raw_path, raw_path_length));
	for (size_t i = 0; i < header_field_count; i++) {
		request->add_header(String::utf8(header_fields[i].name, header_fields[i].name_len),
				String::utf8(header_fields[i].value, header_fields[i].value_len));
	}
	request->set_peer(String(stream->get_connected_host()) + ":" + itos(stream->get_connected_port()));

	state = STATE_READY;
}

void HTTPServerConnection::begin_response(const Ref<HTTPResponse> &p_response) {
	ERR_FAIL_COND(p_response.is_null());
	ERR_FAIL_COND(state != STATE_READY);

	int status = p_response->get_status();
	PackedByteArray body;
	if (p_response->get_body_source() == HTTPResponse::BODY_SOURCE_FILE) {
		// File-backed bodies have no writer yet, so a handler that asks for one gets a server
		// error rather than an empty `200`.
		status = 500;
	} else {
		body = p_response->get_body();
	}

	String head = "HTTP/1.1 " + itos(status) + " " + String(status_reason(status)) + "\r\n";
	const Dictionary headers = p_response->get_headers();
	const Array header_names = headers.keys();
	for (int i = 0; i < header_names.size(); i++) {
		const String name = header_names[i];
		const String lowercase_name = name.to_lower();
		// The framing fields are decided here, not by the handler.
		if (lowercase_name == "content-length" || lowercase_name == "connection") {
			continue;
		}
		head += name + ": " + String(headers[header_names[i]]) + "\r\n";
	}
	head += "Content-Length: " + itos(body.size()) + "\r\n";
	// One request per connection; persistent connections are a separate concern.
	head += "Connection: close\r\n\r\n";

	const CharString head_bytes = head.utf8();
	write_buffer.resize_uninitialized(head_bytes.length() + body.size());
	memcpy(write_buffer.ptrw(), head_bytes.get_data(), head_bytes.length());
	if (!body.is_empty()) {
		memcpy(write_buffer.ptrw() + head_bytes.length(), body.ptr(), body.size());
	}
	write_offset = 0;
	state = STATE_WRITING;

	_flush_write();
}

void HTTPServerConnection::_flush_write() {
	if (stream.is_null()) {
		close();
		return;
	}

	while (write_offset < write_buffer.size()) {
		int sent = 0;
		const Error err = stream->put_partial_data(write_buffer.ptr() + write_offset, write_buffer.size() - write_offset, sent);
		if (err != OK) {
			close();
			return;
		}
		if (sent == 0) {
			// The send buffer is full; the rest drains on a later poll.
			return;
		}
		write_offset += sent;
	}

	close();
}

void HTTPServerConnection::close() {
	if (stream.is_valid()) {
		stream->disconnect_from_host();
	}
	state = STATE_CLOSED;
}
