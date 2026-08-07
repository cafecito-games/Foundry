/**************************************************************************/
/*  http_response.cpp                                                     */
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

#include "http_response.h"

void HTTPResponse::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_status", "status"), &HTTPResponse::set_status);
	ClassDB::bind_method(D_METHOD("get_status"), &HTTPResponse::get_status);
	ClassDB::bind_method(D_METHOD("set_header", "name", "value"), &HTTPResponse::set_header);
	ClassDB::bind_method(D_METHOD("get_header", "name"), &HTTPResponse::get_header);
	ClassDB::bind_method(D_METHOD("has_header", "name"), &HTTPResponse::has_header);
	ClassDB::bind_method(D_METHOD("get_headers"), &HTTPResponse::get_headers);
	ClassDB::bind_method(D_METHOD("send", "body"), &HTTPResponse::send);
	ClassDB::bind_method(D_METHOD("send_string", "body"), &HTTPResponse::send_string);
	ClassDB::bind_method(D_METHOD("redirect", "location", "status"), &HTTPResponse::redirect, DEFVAL(302));
	ClassDB::bind_method(D_METHOD("send_file", "path"), &HTTPResponse::send_file);
	ClassDB::bind_method(D_METHOD("is_sent"), &HTTPResponse::is_sent);
	ClassDB::bind_method(D_METHOD("get_body_source"), &HTTPResponse::get_body_source);
	ClassDB::bind_method(D_METHOD("get_body"), &HTTPResponse::get_body);
	ClassDB::bind_method(D_METHOD("get_body_string"), &HTTPResponse::get_body_string);
	ClassDB::bind_method(D_METHOD("get_file_path"), &HTTPResponse::get_file_path);

	ADD_PROPERTY(PropertyInfo(Variant::INT, "status", PROPERTY_HINT_RANGE, "100,599,1"), "set_status", "get_status");

	BIND_ENUM_CONSTANT(BODY_SOURCE_NONE);
	BIND_ENUM_CONSTANT(BODY_SOURCE_BYTES);
	BIND_ENUM_CONSTANT(BODY_SOURCE_FILE);
}

bool HTTPResponse::_begin_send() {
	ERR_FAIL_COND_V_MSG(sent, false, "This response has already been sent.");
	sent = true;
	return true;
}

void HTTPResponse::_put_header(const String &p_name, const String &p_value) {
	const String lower_name = p_name.to_lower();
	HashMap<String, String>::ConstIterator existing = header_index.find(lower_name);
	if (existing) {
		headers[existing->value] = p_value;
		return;
	}
	header_index.insert(lower_name, p_name);
	headers[p_name] = p_value;
}

void HTTPResponse::set_status(int p_status) {
	ERR_FAIL_COND_MSG(sent, "This response has already been sent; its status can no longer change.");
	ERR_FAIL_COND_MSG(p_status < 100 || p_status > 599, "HTTP status must be between 100 and 599.");
	status = p_status;
}

int HTTPResponse::get_status() const {
	return status;
}

void HTTPResponse::set_header(const String &p_name, const String &p_value) {
	ERR_FAIL_COND_MSG(sent, "This response has already been sent; its header fields can no longer change.");
	ERR_FAIL_COND_MSG(p_name.is_empty(), "Header name cannot be empty.");
	_put_header(p_name, p_value);
}

String HTTPResponse::get_header(const String &p_name) const {
	HashMap<String, String>::ConstIterator found = header_index.find(p_name.to_lower());
	return found ? String(headers.get(found->value, String())) : String();
}

bool HTTPResponse::has_header(const String &p_name) const {
	return header_index.has(p_name.to_lower());
}

Dictionary HTTPResponse::get_headers() const {
	return headers.duplicate();
}

void HTTPResponse::send(const PackedByteArray &p_body) {
	if (!_begin_send()) {
		return;
	}
	body = p_body;
	body_source = BODY_SOURCE_BYTES;
}

void HTTPResponse::send_string(const String &p_body) {
	if (!_begin_send()) {
		return;
	}
	body = p_body.to_utf8_buffer();
	body_source = BODY_SOURCE_BYTES;
}

void HTTPResponse::redirect(const String &p_location, int p_status) {
	ERR_FAIL_COND_MSG(p_location.is_empty(), "Redirect location cannot be empty.");
	ERR_FAIL_COND_MSG(p_status < 300 || p_status > 399, "A redirect status must be between 300 and 399.");
	if (!_begin_send()) {
		return;
	}
	// The status and the field are part of committing here, so they bypass the gate that closes both
	// to callers once the response is sent.
	status = p_status;
	_put_header("Location", p_location);
	body = PackedByteArray();
	body_source = BODY_SOURCE_BYTES;
}

void HTTPResponse::send_file(const String &p_path) {
	ERR_FAIL_COND_MSG(p_path.is_empty(), "File path cannot be empty.");
	if (!_begin_send()) {
		return;
	}
	file_path = p_path;
	body = PackedByteArray();
	body_source = BODY_SOURCE_FILE;
}

bool HTTPResponse::is_sent() const {
	return sent;
}

HTTPResponse::BodySource HTTPResponse::get_body_source() const {
	return body_source;
}

PackedByteArray HTTPResponse::get_body() const {
	return body;
}

String HTTPResponse::get_body_string() const {
	if (body.is_empty()) {
		return String();
	}
	return String::utf8((const char *)body.ptr(), body.size());
}

String HTTPResponse::get_file_path() const {
	return file_path;
}
