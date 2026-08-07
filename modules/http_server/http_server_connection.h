/**************************************************************************/
/*  http_server_connection.h                                              */
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

#include "http_response.h"
#include "http_server_request.h"

#include "core/io/file_access.h"
#include "core/io/stream_peer_tcp.h"
#include "core/object/ref_counted.h"
#include "core/templates/vector.h"

// One accepted TCP connection, from the first byte read to the last byte written. The connection
// owns the wire format only: it never resolves a route and never invokes script code. The server
// polls it, and once it reports `STATE_READY` the server dispatches the parsed request and hands
// back a committed `HTTPResponse` through `begin_response()`.
//
// This is an internal type; it is not registered with `ClassDB` and is not reachable from scripts.
class HTTPServerConnection : public RefCounted {
	FOUNDRY_SOFTCLASS(HTTPServerConnection, RefCounted);

public:
	enum State {
		// Reading bytes and feeding them to the parser; the request is not complete yet.
		STATE_READING,
		// A complete request header block was parsed and is waiting for the server to dispatch it.
		STATE_READY,
		// A serialized response is draining to the peer.
		STATE_WRITING,
		// The socket is closed; the server drops the connection on its next prune.
		STATE_CLOSED,
	};

private:
	// A request larger than this, header block and body together, is dropped rather than buffered
	// without bound. Configurable request limits are a separate concern and are not exposed here.
	static constexpr int MAX_REQUEST_BYTES = 32 * 1024;
	// picohttpparser writes one entry per header field, and reports a request carrying more fields
	// than this as malformed.
	static constexpr int MAX_HEADER_FIELDS = 64;

	Ref<StreamPeerTCP> stream;
	Vector<uint8_t> read_buffer;
	// How many buffered bytes the parser has already scanned without finding the end of the header
	// block. picohttpparser takes this as its `last_len` so a re-parse skips that prefix.
	size_t scanned_length = 0;
	// How many bytes the header block occupies, and how many body bytes are expected after it. Both
	// are meaningful only once `header_parsed` is set; the header block is parsed exactly once.
	bool header_parsed = false;
	int header_length = 0;
	int body_length = 0;
	// How much of a file body is copied into the write buffer at a time. A file body is streamed in
	// pieces this size rather than read into memory, so serving a large file costs a fixed amount
	// of memory per connection.
	static constexpr int FILE_CHUNK_BYTES = 64 * 1024;

	Vector<uint8_t> write_buffer;
	int write_offset = 0;
	// The open file a file-backed body is streaming from, and how many of its bytes are still to
	// be written. Null for every other kind of body.
	Ref<FileAccess> body_file;
	uint64_t body_file_remaining = 0;
	State state = STATE_READING;
	Ref<HTTPServerRequest> request;

	// Appends everything the socket has available. Returns false once the connection is closed.
	bool _read_available();
	void _parse();
	// Parses the request line and the header fields once, and records how the body is framed.
	// Returns false while the header block is still incomplete, or once the connection is closed.
	bool _parse_header_block();
	void _flush_write();
	// Refills the drained write buffer with the next piece of a file body. Returns false once the
	// body is complete, or when the file stops delivering the bytes it promised.
	bool _refill_from_file();

public:
	void accept(const Ref<StreamPeerTCP> &p_stream);

	void poll();

	State get_state() const { return state; }
	bool is_closed() const { return state == STATE_CLOSED; }

	// Valid only while the connection is in `STATE_READY`.
	Ref<HTTPServerRequest> get_request() const { return request; }

	// Serializes a committed response and moves to `STATE_WRITING`. The bytes drain over later
	// polls, and the connection closes once the last byte is written.
	void begin_response(const Ref<HTTPResponse> &p_response);

	void close();
};
