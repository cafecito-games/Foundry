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

#include "core/os/os.h"

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
		case 413:
			return "Content Too Large";
		case 431:
			return "Request Header Fields Too Large";
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

enum ContentLengthResult {
	CONTENT_LENGTH_OK,
	// Not a frame this layer can act on: a signed value, a non-numeric one, or the "5, 5" a repeated
	// field folds into, which is how a smuggled request tries to make two parties disagree on where
	// the body ends.
	CONTENT_LENGTH_MALFORMED,
	CONTENT_LENGTH_TOO_LARGE,
};

// A body is framed by `Content-Length`. An absent field means no body.
ContentLengthResult parse_content_length(const String &p_value, int p_limit, int &r_length) {
	const String value = p_value.strip_edges();
	if (value.is_empty()) {
		r_length = 0;
		return CONTENT_LENGTH_OK;
	}
	for (int i = 0; i < value.length(); i++) {
		if (!is_digit(value[i])) {
			return CONTENT_LENGTH_MALFORMED;
		}
	}
	// Bounded before the conversion, so a length far past any cap cannot wrap on its way to a small
	// number. Ten digits already exceed every allowed cap, so the value is only ever too large.
	if (value.length() > 9) {
		return CONTENT_LENGTH_TOO_LARGE;
	}

	const int length = static_cast<int>(value.to_int());
	if (length > p_limit) {
		return CONTENT_LENGTH_TOO_LARGE;
	}
	r_length = length;
	return CONTENT_LENGTH_OK;
}

// A connection is persistent by default from HTTP/1.1 on, and only on request before that. Either
// way `Connection: close` ends it.
bool wants_keep_alive(const String &p_connection_field, int p_minor_version) {
	const String field = p_connection_field.to_lower();
	if (field.contains("close")) {
		return false;
	}
	if (p_minor_version >= 1) {
		return true;
	}
	return field.contains("keep-alive");
}

} // namespace

void HTTPServerConnection::accept(const Ref<StreamPeerTCP> &p_stream, const Limits &p_limits) {
	stream = p_stream;
	limits = p_limits;
	limits.max_header_count = CLAMP(limits.max_header_count, 1, MAX_HEADER_FIELD_CEILING);
	state = stream.is_valid() ? STATE_READING : STATE_CLOSED;
	_note_progress();
}

void HTTPServerConnection::poll() {
	if (state == STATE_CLOSED) {
		return;
	}

	if (_has_stalled()) {
		// A connection that stops moving bytes — a request that never finishes arriving, a peer that
		// stopped reading, or a reused socket nobody sent anything on — is dropped so its slot comes
		// back. Nothing is written first: the peer is by definition not reading.
		close();
		return;
	}

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

void HTTPServerConnection::_note_progress() {
	last_progress_usec = OS::get_singleton()->get_ticks_usec();
}

bool HTTPServerConnection::_has_stalled() const {
	if (limits.timeout_seconds <= 0.0) {
		return false;
	}
	const uint64_t budget_usec = uint64_t(limits.timeout_seconds * 1000000.0);
	return OS::get_singleton()->get_ticks_usec() - last_progress_usec > budget_usec;
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

		// Once the framing is known, never read past the end of the request being assembled, so a
		// pipelined request behind it waits in the socket rather than growing this buffer. Before
		// that the end is not known yet, and the header block may grow only up to its own bound;
		// whatever lands past the frame in that window is carried forward at the request boundary.
		int wanted = available;
		if (header_parsed) {
			const int needed = header_length + body_length - previous_size;
			if (needed <= 0) {
				break;
			}
			wanted = MIN(available, needed);
		} else {
			wanted = MIN(available, limits.max_header_block_bytes - previous_size);
			if (wanted <= 0) {
				// The block already fills its bound without ending, so it never will.
				_reject(431);
				return false;
			}
		}

		read_buffer.resize_uninitialized(previous_size + wanted);
		int received = 0;
		const Error err = stream->get_partial_data(read_buffer.ptrw() + previous_size, wanted, received);
		read_buffer.resize(previous_size + received);
		if (err != OK) {
			close();
			return false;
		}
		if (received == 0) {
			break;
		}
		_note_progress();

		available = stream->get_available_bytes();
	}

	return true;
}

void HTTPServerConnection::_parse() {
	if (read_buffer.is_empty()) {
		return;
	}

	if (!_parse_header_block()) {
		return;
	}

	// The header block ends where the body begins; the request is only complete once every framed
	// body byte has arrived.
	if (read_buffer.size() - header_length < body_length) {
		return;
	}

	if (body_length > 0) {
		PackedByteArray body;
		body.resize(body_length);
		memcpy(body.ptrw(), read_buffer.ptr() + header_length, body_length);
		request->set_body(body);
	}

	state = STATE_READY;
}

bool HTTPServerConnection::_parse_header_block() {
	if (header_parsed) {
		return true;
	}

	const char *method = nullptr;
	size_t method_length = 0;
	const char *raw_path = nullptr;
	size_t raw_path_length = 0;
	int minor_version = 0;
	// One slot past the cap, so a request carrying exactly one field too many is still parsed and
	// can be refused for the reason it was actually refused for.
	const size_t field_capacity = size_t(limits.max_header_count) + 1;
	phr_header header_fields[MAX_HEADER_FIELD_CEILING + 1];
	size_t header_field_count = field_capacity;

	const int parsed = phr_parse_request(reinterpret_cast<const char *>(read_buffer.ptr()), read_buffer.size(),
			&method, &method_length, &raw_path, &raw_path_length, &minor_version,
			header_fields, &header_field_count, scanned_length);

	if (parsed == -2) {
		if (read_buffer.size() >= limits.max_header_block_bytes) {
			// Nothing more is read into a block this size, so it can never end.
			_reject(431);
			return false;
		}
		// Incomplete: remember how far the parser got so the next pass does not rescan the prefix.
		scanned_length = read_buffer.size();
		return false;
	}
	if (parsed < 0) {
		// A filled field table and a syntax error are reported the same way, and are told apart by
		// whether every slot was used.
		_reject(header_field_count >= field_capacity ? 431 : 400);
		return false;
	}
	if (header_field_count > size_t(limits.max_header_count)) {
		_reject(431);
		return false;
	}
	for (size_t i = 0; i < header_field_count; i++) {
		if (header_fields[i].name == nullptr) {
			// A line folded onto the previous field, which RFC 9110 deprecates and allows a recipient
			// to refuse. Refusing is the only safe reading: dropping the continuation, or keeping only
			// the part before it, would give this server a different value than a proxy that honors
			// the fold, and a `Content-Length` those two disagree on is a smuggled request.
			_reject(400);
			return false;
		}
		// The colon, the separating space and the CRLF are all part of the line the client sent, so
		// they count against its bound.
		if (header_fields[i].name_len + header_fields[i].value_len + 4 > size_t(limits.max_header_line_bytes)) {
			_reject(431);
			return false;
		}
	}

	request.instantiate();
	request->set_method(String::utf8(method, method_length));
	request->set_raw_path(String::utf8(raw_path, raw_path_length));
	for (size_t i = 0; i < header_field_count; i++) {
		request->add_header(String::utf8(header_fields[i].name, header_fields[i].name_len),
				String::utf8(header_fields[i].value, header_fields[i].value_len));
	}
	request->set_peer(String(stream->get_connected_host()) + ":" + itos(stream->get_connected_port()));

	const bool has_content_length = request->has_header("Content-Length");
	const bool has_transfer_encoding = request->has_header("Transfer-Encoding");
	const String content_length_field = request->get_header("Content-Length");
	const String connection_field = request->get_header("Connection");

	if (has_content_length && has_transfer_encoding) {
		// Ambiguous framing: the two fields disagree about where the body ends. Guessing between them
		// is exactly what request smuggling relies on, so the request is refused and the socket is
		// not reused for whatever follows it.
		_reject(400);
		return false;
	}
	if (has_transfer_encoding) {
		// A transfer coding would have to be decoded before a handler could read the body, and this
		// layer decodes none.
		_reject(501);
		return false;
	}

	int content_length = 0;
	switch (parse_content_length(content_length_field, limits.max_request_body_bytes, content_length)) {
		case CONTENT_LENGTH_MALFORMED: {
			_reject(400);
			return false;
		}
		case CONTENT_LENGTH_TOO_LARGE: {
			_reject(413);
			return false;
		}
		case CONTENT_LENGTH_OK:
			break;
	}

	body_length = content_length;
	header_length = parsed;
	header_parsed = true;
	keep_alive = wants_keep_alive(connection_field, minor_version);
	return true;
}

void HTTPServerConnection::_reject(int p_status) {
	// Whatever was parsed so far describes a request no handler may see, so none of it survives into
	// the response and none of it survives the socket either: a refusal always ends the connection.
	request.unref();
	read_buffer.clear();
	scanned_length = 0;
	header_parsed = false;
	header_length = 0;
	body_length = 0;
	body_file.unref();
	body_file_remaining = 0;
	keep_alive = false;

	const String reason = String(status_reason(p_status));
	const CharString body_bytes = reason.utf8();
	String head = "HTTP/1.1 " + itos(p_status) + " " + reason + "\r\n";
	head += "Content-Type: text/plain; charset=utf-8\r\n";
	head += "Content-Length: " + itos(body_bytes.length()) + "\r\n";
	head += "Connection: close\r\n\r\n";

	const CharString head_bytes = head.utf8();
	write_buffer.resize_uninitialized(head_bytes.length() + body_bytes.length());
	memcpy(write_buffer.ptrw(), head_bytes.get_data(), head_bytes.length());
	memcpy(write_buffer.ptrw() + head_bytes.length(), body_bytes.get_data(), body_bytes.length());
	write_offset = 0;
	state = STATE_WRITING;
	_note_progress();

	_flush_write();
}

void HTTPServerConnection::_begin_next_request() {
	// The socket is reused, so everything the finished exchange left behind has to go: a field, a
	// body byte or a parse offset that survived would be read as part of the next request.
	//
	// The one exception is what a client pipelined behind the finished request. Those bytes are past
	// its frame, so they are not part of it and are never mistaken for it, but they are the start of
	// the next request and dropping them would lose it.
	const int consumed = header_length + body_length;
	if (consumed > 0 && read_buffer.size() > consumed) {
		read_buffer = read_buffer.slice(consumed);
	} else {
		read_buffer.clear();
	}
	request.unref();
	scanned_length = 0;
	header_parsed = false;
	header_length = 0;
	body_length = 0;
	write_buffer.clear();
	write_offset = 0;
	body_file.unref();
	body_file_remaining = 0;
	keep_alive = false;
	state = STATE_READING;
	_note_progress();
}

void HTTPServerConnection::begin_response(const Ref<HTTPResponse> &p_response) {
	ERR_FAIL_COND(p_response.is_null());
	ERR_FAIL_COND(state != STATE_READY);

	int status = p_response->get_status();
	PackedByteArray body;
	if (p_response->get_body_source() == HTTPResponse::BODY_SOURCE_FILE) {
		body_file = FileAccess::open(p_response->get_file_path(), FileAccess::READ);
		if (body_file.is_null()) {
			// The file was readable when the response was built, so losing it in between is a
			// server-side failure rather than anything the client did wrong.
			status = 500;
		} else {
			const uint64_t length = body_file->get_length();
			const uint64_t offset = MIN(p_response->get_file_offset(), length);
			body_file_remaining = MIN(p_response->get_file_length(), length - offset);
			body_file->seek(offset);
		}
	} else {
		body = p_response->get_body();
	}

	// RFC 9110 forbids a content length on these statuses, and neither carries a body, so whatever a
	// handler committed is dropped rather than written without a frame the client can read.
	const bool body_forbidden = status == 204 || status == 304;
	// A `HEAD` asks for the header block a `GET` would produce, so the framing is written and the
	// body bytes are not.
	const bool head_request = request.is_valid() && request->get_method() == "HEAD";
	const uint64_t content_length = body_file.is_valid() ? body_file_remaining : uint64_t(body.size());
	if (body_forbidden || head_request) {
		body = PackedByteArray();
		body_file.unref();
		body_file_remaining = 0;
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
	if (!body_forbidden) {
		head += "Content-Length: " + String::num_uint64(content_length) + "\r\n";
	}
	// Every response written here is framed by a length the client can count, so the socket is safe
	// to reuse whenever the request asked for it.
	head += keep_alive ? "Connection: keep-alive\r\n\r\n" : "Connection: close\r\n\r\n";

	const CharString head_bytes = head.utf8();
	write_buffer.resize_uninitialized(head_bytes.length() + body.size());
	memcpy(write_buffer.ptrw(), head_bytes.get_data(), head_bytes.length());
	if (!body.is_empty()) {
		memcpy(write_buffer.ptrw() + head_bytes.length(), body.ptr(), body.size());
	}
	write_offset = 0;
	state = STATE_WRITING;
	_note_progress();

	_flush_write();
}

void HTTPServerConnection::_flush_write() {
	if (stream.is_null()) {
		close();
		return;
	}

	do {
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
			// Progress is counted as bytes leave, not against the age of the response, so a body of
			// any size is never cut off for taking many polls to deliver.
			_note_progress();
		}
	} while (_refill_from_file());

	if (keep_alive) {
		_begin_next_request();
		return;
	}
	close();
}

bool HTTPServerConnection::_refill_from_file() {
	if (body_file.is_null() || body_file_remaining == 0) {
		return false;
	}

	const int wanted = int(MIN(body_file_remaining, uint64_t(FILE_CHUNK_BYTES)));
	write_buffer.resize_uninitialized(wanted);
	const uint64_t read = body_file->get_buffer(write_buffer.ptrw(), wanted);
	write_offset = 0;
	if (read == 0) {
		// The file promised more bytes than it delivered, so the framing is already wrong and the
		// only honest thing left is to close instead of writing a short body forever. The socket
		// cannot be reused either: the client is still counting bytes that will never arrive.
		write_buffer.clear();
		body_file.unref();
		body_file_remaining = 0;
		keep_alive = false;
		return false;
	}

	write_buffer.resize(int(read));
	body_file_remaining -= read;
	return true;
}

void HTTPServerConnection::_linger() {
	if (stream.is_null()) {
		return;
	}

	uint8_t discard[4096];
	int drained = 0;
	while (drained < MAX_LINGER_BYTES) {
		if (stream->get_status() != StreamPeerTCP::STATUS_CONNECTED) {
			return;
		}
		const int available = stream->get_available_bytes();
		if (available <= 0) {
			return;
		}
		int received = 0;
		if (stream->get_partial_data(discard, MIN(available, int(sizeof(discard))), received) != OK || received == 0) {
			return;
		}
		drained += received;
	}
}

void HTTPServerConnection::close() {
	if (stream.is_valid()) {
		// Closing a socket that still holds unread bytes makes the operating system answer the peer
		// with a reset, which would destroy a status this layer just wrote. Reading them first,
		// bounded, is what lets a refused client actually see why it was refused.
		_linger();
		stream->disconnect_from_host();
	}
	request.unref();
	read_buffer.clear();
	write_buffer.clear();
	body_file.unref();
	body_file_remaining = 0;
	keep_alive = false;
	state = STATE_CLOSED;
}
