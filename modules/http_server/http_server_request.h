/**************************************************************************/
/*  http_server_request.h                                                 */
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

// One inbound HTTP request, exposed to scripts as `foundry.http.server.HTTPRequest`. The C++ token
// stays `HTTPServerRequest` because the global-scope client node already owns `HTTPRequest`; the
// exposed simple name is supplied at namespace registration time.
//
// Scripts see a read-only view: the mutators below are for the native code that parses the wire
// format and are deliberately not bound.
class HTTPServerRequest : public RefCounted {
	FOUNDRY_CLASS(HTTPServerRequest, RefCounted);

	String method;
	String raw_path;
	String path;
	Dictionary query;
	Dictionary headers;
	// Lower-cased field name -> the key it is stored under in `headers`, so lookup is
	// case-insensitive as HTTP requires while the dictionary keeps the casing the client sent.
	HashMap<String, String> header_index;
	PackedByteArray body;
	String peer;

protected:
	static void _bind_methods();

public:
	void set_method(const String &p_method);
	String get_method() const;

	// Sets the request target as received on the wire; splits off and parses the query string and
	// percent-decodes the path component. A repeated query key keeps its last value.
	void set_raw_path(const String &p_raw_path);
	String get_raw_path() const;
	String get_path() const;
	// Returns a copy, so a caller cannot mutate the parsed request through the returned handle.
	Dictionary get_query() const;

	// Appends a header field. A repeated field name is folded into a comma-separated value, per the
	// HTTP field-order rules.
	void add_header(const String &p_name, const String &p_value);
	String get_header(const String &p_name) const;
	bool has_header(const String &p_name) const;
	// Returns a copy, for the same reason as `get_query()`.
	Dictionary get_headers() const;

	void set_body(const PackedByteArray &p_body);
	PackedByteArray get_body() const;
	String get_body_string() const;

	void set_peer(const String &p_peer);
	String get_peer() const;
};
