/**************************************************************************/
/*  http_response.h                                                       */
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

#include "core/object/ref_counted.h"
#include "core/templates/hash_map.h"
#include "core/variant/dictionary.h"

// The outbound half of one HTTP exchange, exposed as `foundry.http.server.HTTPResponse`. Handlers
// build the status line and headers, then commit the body exactly once through one of the `send`
// methods. Committing does not write to a socket yet; it records what will be written, which the
// transport layer reads back.
class HTTPResponse : public RefCounted {
	FOUNDRY_CLASS(HTTPResponse, RefCounted);

public:
	enum BodySource {
		BODY_SOURCE_NONE,
		BODY_SOURCE_BYTES,
		BODY_SOURCE_FILE,
	};

	// A file body that runs to the end of the file, whatever its length turns out to be when the
	// transport opens it.
	static constexpr uint64_t FILE_LENGTH_TO_END = UINT64_MAX;

private:
	int status = 200;
	Dictionary headers;
	// Lower-cased field name -> the key it is stored under in `headers`, so a re-set replaces the
	// value rather than adding a second spelling of the same field.
	HashMap<String, String> header_index;
	PackedByteArray body;
	String file_path;
	uint64_t file_offset = 0;
	uint64_t file_length = FILE_LENGTH_TO_END;
	BodySource body_source = BODY_SOURCE_NONE;
	bool sent = false;

	bool _begin_send();
	// Writes a field without the commit gate, for the fields the commit itself contributes.
	void _put_header(const String &p_name, const String &p_value);

protected:
	static void _bind_methods();

public:
	// Committing closes the whole response: the status line and the header fields stop accepting
	// changes along with the body, so what a later caller observes is what the client will read.
	void set_status(int p_status);
	int get_status() const;

	void set_header(const String &p_name, const String &p_value);
	String get_header(const String &p_name) const;
	bool has_header(const String &p_name) const;
	// Returns a copy; `set_header()` is the only way to change the fields that will be written.
	Dictionary get_headers() const;

	// Each of these commits the response. A second commit is an error and leaves the first intact.
	void send(const PackedByteArray &p_body);
	void send_string(const String &p_body);
	void redirect(const String &p_location, int p_status);
	void send_file(const String &p_path);
	// Commits part of a file instead of all of it, which is what a range request is answered with.
	// Native only: a script asks for a file by path and lets the mount decide about ranges.
	void send_file_range(const String &p_path, uint64_t p_offset, uint64_t p_length);

	bool is_sent() const;
	BodySource get_body_source() const;
	PackedByteArray get_body() const;
	String get_body_string() const;
	String get_file_path() const;
	// Both are meaningful only for a file body, and the length may be `FILE_LENGTH_TO_END`.
	uint64_t get_file_offset() const;
	uint64_t get_file_length() const;
};

VARIANT_ENUM_CAST(HTTPResponse::BodySource);
