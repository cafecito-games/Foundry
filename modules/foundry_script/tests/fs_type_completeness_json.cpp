/**************************************************************************/
/*  fs_type_completeness_json.cpp                                         */
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

#include "fs_type_completeness_json.h"

#include "core/io/json.h"
#include "core/templates/hash_set.h"

namespace FSTests {

namespace {

static bool is_json_path_identifier(const String &p_member) {
	if (p_member.is_empty()) {
		return false;
	}
	for (int i = 0; i < p_member.length(); i++) {
		const char32_t character = p_member[i];
		const bool valid = (character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z') ||
				character == '_' || (i > 0 && character >= '0' && character <= '9');
		if (!valid) {
			return false;
		}
	}
	return true;
}

static String append_json_path_member(const String &p_path, const String &p_member) {
	if (is_json_path_identifier(p_member)) {
		return p_path + "." + p_member;
	}
	return vformat("%s[%s]", p_path, JSON::stringify(p_member));
}

static void append_json_error(Vector<String> &r_errors, const String &p_prefix, const String &p_path,
		const String &p_reason) {
	const String diagnostic = vformat("%s: %s", p_path, p_reason);
	r_errors.push_back(p_prefix.is_empty() ? diagnostic : p_prefix + ": " + diagnostic);
}

// Core JSON parsing stores objects in Dictionary and therefore cannot retain duplicate member
// occurrences. This recursive scanner runs only after the core parser has accepted the document.
// It delegates string-literal decoding to JSON so escaped spellings compare as the same member.
class JSONDuplicateMemberDetector {
	const String &source;
	const String &diagnostic_prefix;
	Vector<String> &errors;
	int index = 0;

	void skip_whitespace() {
		while (index < source.length() && source[index] <= 32) {
			index++;
		}
	}

	bool parse_string(String &r_decoded) {
		skip_whitespace();
		if (index >= source.length() || source[index] != '"') {
			return false;
		}
		const int start = index++;
		while (index < source.length()) {
			const char32_t character = source[index++];
			if (character == '\\') {
				if (index >= source.length()) {
					return false;
				}
				index++;
				continue;
			}
			if (character == '"') {
				const Variant decoded = JSON::parse_string(source.substr(start, index - start));
				if (decoded.get_type() != Variant::STRING) {
					return false;
				}
				r_decoded = decoded;
				return true;
			}
		}
		return false;
	}

	bool parse_value(const String &p_path) {
		skip_whitespace();
		if (index >= source.length()) {
			return false;
		}
		switch (source[index]) {
			case '{':
				return parse_object(p_path);
			case '[':
				return parse_array(p_path);
			case '"': {
				String ignored;
				return parse_string(ignored);
			}
			default:
				while (index < source.length() && source[index] != ',' && source[index] != ']' && source[index] != '}') {
					index++;
				}
				return true;
		}
	}

	bool parse_object(const String &p_path) {
		if (source[index++] != '{') {
			return false;
		}
		HashSet<String> members;
		skip_whitespace();
		if (index < source.length() && source[index] == '}') {
			index++;
			return true;
		}

		while (index < source.length()) {
			String member;
			if (!parse_string(member)) {
				return false;
			}
			skip_whitespace();
			if (index >= source.length() || source[index++] != ':') {
				return false;
			}

			const String member_path = append_json_path_member(p_path, member);
			if (members.has(member)) {
				append_json_error(errors, diagnostic_prefix, member_path,
						vformat("duplicate object member '%s'", member));
			} else {
				members.insert(member);
			}
			if (!parse_value(member_path)) {
				return false;
			}

			skip_whitespace();
			if (index < source.length() && source[index] == ',') {
				index++;
				continue;
			}
			if (index < source.length() && source[index] == '}') {
				index++;
				return true;
			}
			return false;
		}
		return false;
	}

	bool parse_array(const String &p_path) {
		if (source[index++] != '[') {
			return false;
		}
		skip_whitespace();
		if (index < source.length() && source[index] == ']') {
			index++;
			return true;
		}

		int element = 0;
		while (index < source.length()) {
			if (!parse_value(vformat("%s[%d]", p_path, element++))) {
				return false;
			}
			skip_whitespace();
			if (index < source.length() && source[index] == ',') {
				index++;
				continue;
			}
			if (index < source.length() && source[index] == ']') {
				index++;
				return true;
			}
			return false;
		}
		return false;
	}

public:
	JSONDuplicateMemberDetector(const String &p_source, const String &p_diagnostic_prefix, Vector<String> &r_errors) :
			source(p_source), diagnostic_prefix(p_diagnostic_prefix), errors(r_errors) {}

	bool detect() {
		return parse_value("$");
	}
};

} // namespace

Error parse_type_completeness_json(const String &p_source, const String &p_diagnostic_prefix,
		Variant &r_data, Vector<String> &r_errors) {
	r_data = Variant();
	JSON json;
	if (json.parse(p_source) != OK) {
		append_json_error(r_errors, p_diagnostic_prefix, "$",
				vformat("invalid JSON at line %d: %s", json.get_error_line(), json.get_error_message()));
		return ERR_PARSE_ERROR;
	}
	r_data = json.get_data();
	const int errors_before_detection = r_errors.size();
	if (!JSONDuplicateMemberDetector(p_source, p_diagnostic_prefix, r_errors).detect()) {
		append_json_error(r_errors, p_diagnostic_prefix, "$", "could not verify unique object members");
	}
	return r_errors.size() == errors_before_detection ? OK : ERR_INVALID_DATA;
}

} // namespace FSTests
