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

	TEST_CASE("[Format] Keeps a full-line comment above a statement at its indent") {
		String source = "func f():\n#hi\n\treturn 1\n";
		String expected = "func f():\n\t# hi\n\treturn 1\n";
		CHECK_EQ(format_or_fail(source), expected);
	}

	TEST_CASE("[Format] Keeps an inline comment trailing the statement") {
		String source = "var x = 1 #count\n";
		String expected = "var x = 1  # count\n";
		CHECK_EQ(format_or_fail(source), expected);
	}

	TEST_CASE("[Format] Collapses multiple blank lines inside a block to one") {
		String source = "func f():\n\tvar a = 1\n\n\n\tvar b = 2\n";
		String expected = "func f():\n\tvar a = 1\n\n\tvar b = 2\n";
		CHECK_EQ(format_or_fail(source), expected);
	}

	TEST_CASE("[Format] Two blank lines between top-level functions") {
		String source = "func a():\n\tpass\nfunc b():\n\tpass\n";
		String expected = "func a():\n\tpass\n\n\nfunc b():\n\tpass\n";
		CHECK_EQ(format_or_fail(source), expected);
	}

	TEST_CASE("[Format] One blank line between methods inside an inner class") {
		String source = "class Inner:\n\tfunc a():\n\t\tpass\n\tfunc b():\n\t\tpass\n";
		String expected = "class Inner:\n\tfunc a():\n\t\tpass\n\n\tfunc b():\n\t\tpass\n";
		CHECK_EQ(format_or_fail(source), expected);
	}

	TEST_CASE("[Format] Places a full-line comment above the correct statement") {
		// Guards line-index alignment: the comment sits between `b` and `d` and
		// must stay above `d`, never drift up to `a`/`b` (off-by-one regression).
		String source = "var a = 1\nvar b = 2\n# c\nvar d = 3\n";
		String expected = "var a = 1\nvar b = 2\n# c\nvar d = 3\n";
		CHECK_EQ(format_or_fail(source), expected);
	}

	TEST_CASE("[Format] Emits mixed inline and full-line comments exactly once") {
		String source = "func f():\n\tvar a = 1 #first\n\t#second\n\treturn a\n";
		String expected = "func f():\n\tvar a = 1  # first\n\t# second\n\treturn a\n";
		CHECK_EQ(format_or_fail(source), expected);
	}

	TEST_CASE("[Format] Preserves a comment-only file") {
		CHECK_EQ(format_or_fail("# just a comment\n"), "# just a comment\n");
	}

	TEST_CASE("[Format] Preserves a comment after the last statement") {
		String source = "var x = 1\n# trailing\n";
		String expected = "var x = 1\n# trailing\n";
		CHECK_EQ(format_or_fail(source), expected);
	}

	TEST_CASE("[Format] Keeps a trailing comment inside the function body") {
		String source = "func f():\n\tvar a = 1\n\t# trailing in f\nfunc g():\n\tpass\n";
		String expected = "func f():\n\tvar a = 1\n\t# trailing in f\n\n\nfunc g():\n\tpass\n";
		CHECK_EQ(format_or_fail(source), expected);
	}

	TEST_CASE("[Format] Keeps a trailing comment inside an inner-class method") {
		String source = "class Inner:\n\tfunc a():\n\t\tvar x = 1\n\t\t# trailing in a\n\tfunc b():\n\t\tpass\n";
		String expected = "class Inner:\n\tfunc a():\n\t\tvar x = 1\n\t\t# trailing in a\n\n\tfunc b():\n\t\tpass\n";
		CHECK_EQ(format_or_fail(source), expected);
	}

	TEST_CASE("[Format] Enforces one space after the hash") {
		CHECK_EQ(format_or_fail("#foo\n"), "# foo\n");
	}

	TEST_CASE("[Format] Leaves doc comments and shebang lines untouched") {
		CHECK_EQ(format_or_fail("#!shebang\n##doc comment\nvar x = 1\n"),
				"#!shebang\n##doc comment\nvar x = 1\n");
	}

	TEST_CASE("[Format] Normalizes single quotes to double") {
		CHECK_EQ(format_or_fail("var s = 'hi'\n"), "var s = \"hi\"\n");
	}

	TEST_CASE("[Format] Keeps single quotes when content has a double quote") {
		CHECK_EQ(format_or_fail("var s = 'say \"hi\"'\n"), "var s = 'say \"hi\"'\n");
	}

	TEST_CASE("[Format] Leaves double-quoted strings untouched") {
		CHECK_EQ(format_or_fail("var s = \"hi\"\n"), "var s = \"hi\"\n");
	}

	TEST_CASE("[Format] Leaves triple-quoted strings untouched") {
		CHECK_EQ(format_or_fail("var s = '''hi'''\n"), "var s = '''hi'''\n");
	}

	TEST_CASE("[Format] Leaves raw strings untouched") {
		CHECK_EQ(format_or_fail("var s = r'hi'\n"), "var s = r'hi'\n");
	}

	TEST_CASE("[Format] Normalizes hex literal casing") {
		CHECK_EQ(format_or_fail("var n = 0Xff\n"), "var n = 0xFF\n");
	}

	TEST_CASE("[Format] Normalizes binary literal prefix casing") {
		CHECK_EQ(format_or_fail("var n = 0B1010\n"), "var n = 0b1010\n");
	}

	TEST_CASE("[Format] Normalizes exponent casing and preserves underscores") {
		CHECK_EQ(format_or_fail("var n = 1_000E3\n"), "var n = 1_000e3\n");
		CHECK_EQ(format_or_fail("var h = 0xDE_AD\n"), "var h = 0xDE_AD\n");
	}

	TEST_CASE("[Format] Single-line array has no trailing comma") {
		CHECK_EQ(format_or_fail("var a = [1, 2, 3]\n"), "var a = [1, 2, 3]\n");
	}

	TEST_CASE("[Format] Multi-line array gains a trailing comma") {
		CHECK_EQ(format_or_fail("var a = [\n\t1,\n\t2\n]\n"),
				"var a = [\n\t1,\n\t2,\n]\n");
	}

	TEST_CASE("[Format] Multi-line dictionary gains a trailing comma") {
		CHECK_EQ(format_or_fail("var d = {\n\t\"a\": 1,\n\t\"b\": 2\n}\n"),
				"var d = {\n\t\"a\": 1,\n\t\"b\": 2,\n}\n");
	}

	TEST_CASE("[Format] Multi-line call arguments gain a trailing comma") {
		CHECK_EQ(format_or_fail("func f():\n\tfoo(\n\t\t1,\n\t\t2\n\t)\n"),
				"func f():\n\tfoo(\n\t\t1,\n\t\t2,\n\t)\n");
	}

	TEST_CASE("[Format] Keeps parentheses required by precedence") {
		CHECK_EQ(format_or_fail("var n = (1 + 2) * 3\n"), "var n = (1 + 2) * 3\n");
	}

	TEST_CASE("[Format] Drops parentheses not required by precedence") {
		CHECK_EQ(format_or_fail("var n = 1 + (2 * 3)\n"), "var n = 1 + 2 * 3\n");
	}

	TEST_CASE("[Format] Keeps parentheses for left-associativity grouping") {
		CHECK_EQ(format_or_fail("var n = 1 - (2 - 3)\n"), "var n = 1 - (2 - 3)\n");
		CHECK_EQ(format_or_fail("var n = 12 / (3 / 2)\n"), "var n = 12 / (3 / 2)\n");
	}

	TEST_CASE("[Format] Drops parentheses on the left for left-associativity") {
		CHECK_EQ(format_or_fail("var n = (1 - 2) - 3\n"), "var n = 1 - 2 - 3\n");
	}

	TEST_CASE("[Format] Keeps parentheses across logical operator precedence") {
		CHECK_EQ(format_or_fail("var b = (a or b) and c\n"), "var b = (a or b) and c\n");
		CHECK_EQ(format_or_fail("var b = a or (b and c)\n"), "var b = a or b and c\n");
	}

	TEST_CASE("[Format] Parenthesizes a unary operand by precedence") {
		CHECK_EQ(format_or_fail("var n = -a * b\n"), "var n = -a * b\n");
		CHECK_EQ(format_or_fail("var n = -(a * b)\n"), "var n = -(a * b)\n");
	}

	TEST_CASE("[Format] Parenthesizes nested ternary on the value side") {
		CHECK_EQ(format_or_fail("var n = (a if b else c) if d else e\n"),
				"var n = (a if b else c) if d else e\n");
		CHECK_EQ(format_or_fail("var n = a if b else c if d else e\n"),
				"var n = a if b else c if d else e\n");
	}

	TEST_CASE("[Format] Parenthesizes an await operand by precedence") {
		CHECK_EQ(format_or_fail("func f():\n\tawait (a + b)\n"),
				"func f():\n\tawait (a + b)\n");
		CHECK_EQ(format_or_fail("func f():\n\tawait a + b\n"),
				"func f():\n\tawait a + b\n");
	}

	TEST_CASE("[Format] Parenthesizes a cast operand by precedence") {
		CHECK_EQ(format_or_fail("var n = (a + b) as int\n"), "var n = a + b as int\n");
		CHECK_EQ(format_or_fail("var n = a + (b as int)\n"), "var n = a + (b as int)\n");
	}

	TEST_CASE("[Format] Generic type parameters spaced canonically") {
		CHECK_EQ(format_or_fail("class Box[T,U]:\n\tpass\n"), "class Box[T, U]:\n\tpass\n");
	}

	TEST_CASE("[Format] AsyncCallable signature spacing") {
		CHECK_EQ(format_or_fail("var f: AsyncCallable[[int,String],bool]\n"),
				"var f: AsyncCallable[[int, String], bool]\n");
	}
}

} // namespace GDScriptTests

#endif // TOOLS_ENABLED
