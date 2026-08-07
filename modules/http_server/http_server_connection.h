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
#include "core/io/stream_peer_tls.h"
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
		// Completing the TLS handshake on an HTTPS connection before any request byte is read. A
		// plaintext connection is never in this state.
		STATE_HANDSHAKING,
		// Reading bytes and feeding them to the parser; the request is not complete yet.
		STATE_READING,
		// A complete request header block was parsed and is waiting for the server to dispatch it.
		STATE_READY,
		// A serialized response is draining to the peer.
		STATE_WRITING,
		// The socket is closed; the server drops the connection on its next prune.
		STATE_CLOSED,
	};

	// The parse table is a stack array, so a connection can never be asked to hold more header
	// fields than one pass can address.
	static constexpr int MAX_HEADER_FIELD_CEILING = 255;

	// The bounds one connection is held to. The server owns the values and copies them in at accept
	// time, so a limit changed later applies to connections accepted after the change.
	struct Limits {
		// The largest body, in bytes, that is read into memory before dispatch.
		int max_request_body_bytes = 1024 * 1024;
		// The largest number of header fields one request may carry.
		int max_header_count = 100;
		// The largest single header field line, counting its name, its colon, its value and the
		// terminating CRLF.
		int max_header_line_bytes = 8 * 1024;
		// The largest header block, request line and every field together. A block that has not
		// ended by this point never will, because nothing more is read into it.
		int max_header_block_bytes = 32 * 1024;
		// Seconds without progress — bytes read while a request arrives, bytes drained while a
		// response leaves, or no request at all on a reused socket — after which the connection is
		// dropped. Zero disables the drop.
		double timeout_seconds = 30.0;
	};

private:
	// How many bytes are read and discarded before closing a connection that was refused, so the
	// operating system does not answer the peer's unread bytes with a reset that would destroy the
	// status this layer just wrote.
	static constexpr int MAX_LINGER_BYTES = 64 * 1024;

	Limits limits;
	// When the connection last moved a byte in either direction. The timeout is measured against
	// progress rather than age, so a long download is never cut off for taking a long time.
	uint64_t last_progress_usec = 0;
	// Whether the socket carries another request once the current response has drained. Decided
	// from the request's HTTP version and its `Connection` field, and cleared by anything that
	// makes the framing of what follows unknowable.
	bool keep_alive = false;

	// The accepted socket. It carries the bytes on the wire and is the source of the peer address
	// whether or not the connection is encrypted.
	Ref<StreamPeerTCP> tcp_stream;
	// The TLS layer wrapping `tcp_stream` on an HTTPS connection, or null for a plaintext one. When
	// set, every request and response byte moves through it and the raw socket is never read or
	// written directly.
	Ref<StreamPeerTLS> tls_stream;
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

	// Appends what the socket has available, never reading past the end of the request being
	// assembled. Returns false once the connection has stopped reading.
	bool _read_available();
	void _parse();
	// Parses the request line and the header fields once, and records how the body is framed.
	// Returns false while the header block is still incomplete, or once the request was refused.
	bool _parse_header_block();
	// Answers a request this layer refused with a bare status, and closes once it has drained. Every
	// refusal goes through here, so no rejection leaves the peer guessing at a silent close.
	void _reject(int p_status);
	// Clears every per-request field so the socket can carry the next request.
	void _begin_next_request();
	// The stream every read and write goes through: the TLS layer when the connection is encrypted,
	// the raw socket otherwise. The peer address is never taken from here; it comes from `tcp_stream`.
	Ref<StreamPeer> _io_stream() const;
	// Drives the TLS handshake forward one poll. Moves to `STATE_READING` once it completes and closes
	// the connection if it fails. Never called on a plaintext connection.
	void _drive_handshake();
	void _note_progress();
	bool _has_stalled() const;
	// Reads and discards what the peer already sent, bounded, before the socket is closed.
	void _linger();
	void _flush_write();
	// Refills the drained write buffer with the next piece of a file body. Returns false once the
	// body is complete, or when the file stops delivering the bytes it promised.
	bool _refill_from_file();

public:
	// Takes ownership of an accepted socket. With server-side `p_tls_options` the connection wraps the
	// socket in a `StreamPeerTLS` and begins the handshake; with none it reads plaintext HTTP.
	void accept(const Ref<StreamPeerTCP> &p_stream, const Limits &p_limits, const Ref<TLSOptions> &p_tls_options = Ref<TLSOptions>());

	void poll();

	State get_state() const { return state; }
	bool is_closed() const { return state == STATE_CLOSED; }

	// Valid only while the connection is in `STATE_READY`.
	Ref<HTTPServerRequest> get_request() const { return request; }

	// Serializes a committed response and moves to `STATE_WRITING`. The bytes drain over later
	// polls; once the last one is written the socket either carries the next request or closes,
	// depending on what the request asked for.
	void begin_response(const Ref<HTTPResponse> &p_response);

	void close();
};
