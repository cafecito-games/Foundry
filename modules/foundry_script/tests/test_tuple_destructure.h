/**************************************************************************/
/*  test_tuple_destructure.h                                              */
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

#include "../fs_analyzer.h"
#include "../fs_parser.h"

#include "tests/test_macros.h"

namespace FSTests {

static bool destructure_analysis_has_error(const String &p_source, bool p_strict_null_checks, const String &p_expected_fragment) {
	FSParser parser;
	REQUIRE_EQ(parser.parse(p_source, "user://tuple_destructure.fs", false), OK);

	FSAnalyzer analyzer(&parser);
	analyzer.set_strict_null_checks(p_strict_null_checks);
	const Error analyze_error = analyzer.analyze();

	bool found = false;
	for (const FSParser::ParserError &parser_error : parser.get_errors()) {
		if (parser_error.message.contains(p_expected_fragment)) {
			found = true;
			break;
		}
	}
	return analyze_error != OK && found;
}

TEST_CASE("[Modules][FoundryScript] Destructuring a nullable tuple depends on strict null checks") {
	const String source =
			"func maybe() -> (int, int)?:\n"
			"\treturn null\n"
			"func run() -> int:\n"
			"\tvar (first, second) = maybe()\n"
			"\treturn first + second\n";

	CHECK(destructure_analysis_has_error(source, true, "check for null first"));

	FSParser parser;
	REQUIRE_EQ(parser.parse(source, "user://tuple_destructure.fs", false), OK);
	FSAnalyzer analyzer(&parser);
	// Without strict null checks the nullable tuple destructures like any other tuple value.
	CHECK_EQ(analyzer.analyze(), OK);
}

} // namespace FSTests
