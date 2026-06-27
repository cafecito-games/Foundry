/**************************************************************************/
/*  test_format.h                                                         */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
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

#ifdef TOOLS_ENABLED

#include "../gdscript_format.h"

#include "tests/test_macros.h"

namespace GDScriptTests {

static String format_or_fail(const String &p_source) {
	GDScriptFormatter formatter;
	GDScriptFormatter::Result result;
	Error err = formatter.format(p_source, "test.gd", result);
	CHECK_MESSAGE(err == OK, "Source must format without parse errors.");
	return result.formatted;
}

TEST_SUITE("[Modules][GDScript][Format]") {
	TEST_CASE("[Format] Reindents structurally with tabs") {
		String source = "func f():\n        return     1+2\n";
		String expected = "func f():\n\treturn 1 + 2\n";
		CHECK_EQ(format_or_fail(source), expected);
	}

	TEST_CASE("[Format] Refuses to format on parse error") {
		GDScriptFormatter formatter;
		GDScriptFormatter::Result result;
		Error err = formatter.format("func (:\n", "bad.gd", result);
		CHECK(err != OK);
		CHECK(result.formatted.is_empty());
		CHECK(result.error_line > 0);
	}

	TEST_CASE("[Format] Preserves string contents and normalizes to double quotes") {
		// Quote normalization itself lands in Task 3; here we assert the
		// literal text round-trips via the token index rather than the Variant.
		CHECK_EQ(format_or_fail("var s = \"a\\tb\"\n"), "var s = \"a\\tb\"\n");
	}

	TEST_CASE("[Format] Re-quotes node paths that need quoting") {
		// A quoted node name must stay quoted so the token stream is preserved.
		CHECK_EQ(format_or_fail("var n = $\"My Node\"\n"), "var n = $\"My Node\"\n");
	}

	TEST_CASE("[Format] Keeps bare node paths unquoted") {
		CHECK_EQ(format_or_fail("var n = $Player/Sprite\n"), "var n = $Player/Sprite\n");
	}
}

} // namespace GDScriptTests

#endif // TOOLS_ENABLED
