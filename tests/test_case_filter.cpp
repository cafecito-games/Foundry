/**************************************************************************/
/*  test_case_filter.cpp                                                  */
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

#include "test_case_filter.h"

#include "thirdparty/doctest/doctest.h"

#include <cctype>
#include <set>

namespace doctest {
namespace detail {
// doctest declares its registry only inside its own implementation translation unit, but
// the definition has external linkage. Redeclaring it here lets the selection run before a
// doctest context without patching the vendored header.
std::set<TestCase> &getRegisteredTests();
} // namespace detail
} // namespace doctest

namespace {

Vector<String> configured_case_patterns;
Vector<String> configured_suite_patterns;

char lowered(char p_character) {
	return (char)std::tolower((unsigned char)p_character);
}

} // namespace

bool FoundryTestCaseFilter::is_no_skip_argument(const String &p_argument) {
	if (!p_argument.begins_with("-")) {
		return false;
	}
	int start = 0;
	while (start < p_argument.length() && p_argument[start] == '-') {
		start++;
	}
	String body = p_argument.substr(start);
	if (body.begins_with("dt-")) {
		body = body.substr(3);
	}
	if (body == "no-skip" || body == "ns") {
		return true;
	}
	const int separator = body.find_char('=');
	if (separator < 0) {
		return false;
	}
	const String name = body.substr(0, separator);
	if (name != "no-skip" && name != "ns") {
		return false;
	}
	// The values doctest reads as true for a boolean option.
	const String value = body.substr(separator + 1).to_lower();
	return value == "1" || value == "true" || value == "on" || value == "yes";
}

Vector<String> FoundryTestCaseFilter::split_patterns(const String &p_value) {
	Vector<String> patterns;
	String current;
	bool seen_backslash = false;
	for (int i = 0; i < p_value.length(); i++) {
		const char32_t character = p_value[i];
		if (seen_backslash) {
			seen_backslash = false;
			if (character == ',' || character == '\\') {
				current += character;
				continue;
			}
			current += '\\';
		}
		if (character == '\\') {
			seen_backslash = true;
		} else if (character == ',') {
			if (!current.is_empty()) {
				patterns.push_back(current);
			}
			current = String();
		} else {
			current += character;
		}
	}
	if (seen_backslash) {
		current += '\\';
	}
	if (!current.is_empty()) {
		patterns.push_back(current);
	}
	return patterns;
}

bool FoundryTestCaseFilter::matches_pattern(const String &p_text, const String &p_pattern) {
	// Byte-wise glob over the UTF-8 encodings, matching doctest's own `wildcmp()` so a
	// pattern selects exactly the cases it selected when it was forwarded to doctest.
	const CharString text_utf8 = p_text.utf8();
	const CharString pattern_utf8 = p_pattern.utf8();
	const char *text = text_utf8.get_data();
	const char *pattern = pattern_utf8.get_data();
	const char *text_restart = text;
	const char *pattern_restart = pattern;

	while (*text && *pattern != '*') {
		if (lowered(*pattern) != lowered(*text) && *pattern != '?') {
			return false;
		}
		pattern++;
		text++;
	}

	while (*text) {
		if (*pattern == '*') {
			pattern++;
			if (!*pattern) {
				return true;
			}
			pattern_restart = pattern;
			text_restart = text + 1;
		} else if (lowered(*pattern) == lowered(*text) || *pattern == '?') {
			pattern++;
			text++;
		} else {
			pattern = pattern_restart;
			text = text_restart;
			text_restart++;
		}
	}

	while (*pattern == '*') {
		pattern++;
	}
	return *pattern == '\0';
}

bool FoundryTestCaseFilter::selects(const Vector<String> &p_case_patterns, const Vector<String> &p_suite_patterns,
		const String &p_test_suite, const String &p_name) {
	if (p_case_patterns.is_empty() && p_suite_patterns.is_empty()) {
		return true;
	}
	for (const String &pattern : p_case_patterns) {
		if (matches_pattern(p_name, pattern)) {
			return true;
		}
	}
	for (const String &pattern : p_suite_patterns) {
		if (matches_pattern(p_test_suite, pattern)) {
			return true;
		}
	}
	return false;
}

void FoundryTestCaseFilter::configure(const Vector<String> &p_case_values, const Vector<String> &p_suite_values) {
	configured_case_patterns.clear();
	configured_suite_patterns.clear();
	for (const String &value : p_case_values) {
		configured_case_patterns.append_array(split_patterns(value));
	}
	for (const String &value : p_suite_values) {
		configured_suite_patterns.append_array(split_patterns(value));
	}
}

bool FoundryTestCaseFilter::is_active() {
	return !configured_case_patterns.is_empty() || !configured_suite_patterns.is_empty();
}

int FoundryTestCaseFilter::apply_to_registry() {
	int selected_count = 0;
	for (const doctest::detail::TestCase &test_case : doctest::detail::getRegisteredTests()) {
		const String test_suite = String::utf8(test_case.m_test_suite != nullptr ? test_case.m_test_suite : "");
		const String name = String::utf8(test_case.m_name != nullptr ? test_case.m_name : "");
		if (selects(configured_case_patterns, configured_suite_patterns, test_suite, name)) {
			selected_count++;
			continue;
		}
		// `m_skip` takes no part in the registry's ordering, so mutating it in place cannot
		// invalidate the set doctest iterates.
		const_cast<doctest::detail::TestCase &>(test_case).m_skip = true;
	}
	return selected_count;
}
