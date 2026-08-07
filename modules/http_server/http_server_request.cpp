/**************************************************************************/
/*  http_server_request.cpp                                               */
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

#include "http_server_request.h"

namespace {

// `application/x-www-form-urlencoded` decoding: `+` stands for a space, everything else is plain
// percent-encoding.
String decode_form_component(const String &p_value) {
	return p_value.replace("+", " ").uri_decode();
}

} // namespace

void HTTPServerRequest::_bind_methods() {
	ClassDB::bind_method(D_METHOD("get_method"), &HTTPServerRequest::get_method);
	ClassDB::bind_method(D_METHOD("get_path"), &HTTPServerRequest::get_path);
	ClassDB::bind_method(D_METHOD("get_raw_path"), &HTTPServerRequest::get_raw_path);
	ClassDB::bind_method(D_METHOD("get_query"), &HTTPServerRequest::get_query);
	ClassDB::bind_method(D_METHOD("get_header", "name"), &HTTPServerRequest::get_header);
	ClassDB::bind_method(D_METHOD("has_header", "name"), &HTTPServerRequest::has_header);
	ClassDB::bind_method(D_METHOD("get_headers"), &HTTPServerRequest::get_headers);
	ClassDB::bind_method(D_METHOD("get_body"), &HTTPServerRequest::get_body);
	ClassDB::bind_method(D_METHOD("get_body_string"), &HTTPServerRequest::get_body_string);
	ClassDB::bind_method(D_METHOD("get_peer"), &HTTPServerRequest::get_peer);
}

void HTTPServerRequest::set_method(const String &p_method) {
	method = p_method.to_upper();
}

String HTTPServerRequest::get_method() const {
	return method;
}

void HTTPServerRequest::set_raw_path(const String &p_raw_path) {
	raw_path = p_raw_path;
	query.clear();

	const int query_start = p_raw_path.find_char('?');
	if (query_start < 0) {
		path = p_raw_path.uri_decode();
		return;
	}

	path = p_raw_path.substr(0, query_start).uri_decode();

	const String query_string = p_raw_path.substr(query_start + 1);
	for (const String &pair : query_string.split("&", false)) {
		const int separator = pair.find_char('=');
		if (separator < 0) {
			// A bare key with no `=` is present with an empty value.
			query[decode_form_component(pair)] = String();
		} else {
			query[decode_form_component(pair.substr(0, separator))] = decode_form_component(pair.substr(separator + 1));
		}
	}
}

String HTTPServerRequest::get_raw_path() const {
	return raw_path;
}

String HTTPServerRequest::get_path() const {
	return path;
}

Dictionary HTTPServerRequest::get_query() const {
	return query.duplicate();
}

void HTTPServerRequest::add_header(const String &p_name, const String &p_value) {
	ERR_FAIL_COND_MSG(p_name.is_empty(), "Header name cannot be empty.");

	const String lower_name = p_name.to_lower();
	HashMap<String, String>::ConstIterator existing = header_index.find(lower_name);
	if (existing) {
		// A repeat folds into the value stored under the name that was seen first.
		headers[existing->value] = String(headers[existing->value]) + ", " + p_value;
		return;
	}
	header_index.insert(lower_name, p_name);
	headers[p_name] = p_value;
}

String HTTPServerRequest::get_header(const String &p_name) const {
	HashMap<String, String>::ConstIterator found = header_index.find(p_name.to_lower());
	return found ? String(headers.get(found->value, String())) : String();
}

bool HTTPServerRequest::has_header(const String &p_name) const {
	return header_index.has(p_name.to_lower());
}

Dictionary HTTPServerRequest::get_headers() const {
	return headers.duplicate();
}

void HTTPServerRequest::set_body(const PackedByteArray &p_body) {
	body = p_body;
}

PackedByteArray HTTPServerRequest::get_body() const {
	return body;
}

String HTTPServerRequest::get_body_string() const {
	if (body.is_empty()) {
		return String();
	}
	return String::utf8((const char *)body.ptr(), body.size());
}

void HTTPServerRequest::set_peer(const String &p_peer) {
	peer = p_peer;
}

String HTTPServerRequest::get_peer() const {
	return peer;
}
