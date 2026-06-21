/**************************************************************************/
/*  test_refactor.h                                                       */
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

#include "tests/test_macros.h"

#include "../editor/gdscript_refactoring.h"
#include "../editor/gdscript_refactoring_edits.h"
#include "../editor/gdscript_refactoring_names.h"
#include "../editor/gdscript_refactoring_types.h"

#include "core/io/file_access.h"

namespace GDScriptTests {

inline RefactorContext make_context(const String &p_path) {
	Error err = OK;
	String source = FileAccess::get_file_as_string(p_path, &err);
	REQUIRE_MESSAGE(err == OK, vformat("Cannot read '%s'", p_path));
	RefactorContext ctx;
	ctx.path = p_path;
	ctx.source = source;
	return ctx;
}

inline RefactorLocation caret(int p_line, int p_column) {
	RefactorLocation loc;
	loc.start_line = loc.end_line = p_line;
	loc.start_column = loc.end_column = p_column;
	return loc;
}

TEST_SUITE("[Modules][GDScript][Refactor]") {
	TEST_CASE("Engine returns no refactors for a trivial location") {
		RefactorContext ctx = make_context("modules/gdscript/tests/scripts/refactor/empty.gd");
		Vector<RefactorAvailability> available = GDScriptRefactoring::get_available_refactors(ctx, caret(0, 0));
		CHECK(available.is_empty());
	}

	TEST_CASE("Edit application") {
		auto edit = [](int sl, int sc, int el, int ec, const String &t) {
			RefactorTextEdit e;
			e.start_line = sl; e.start_column = sc; e.end_line = el; e.end_column = ec; e.new_text = t;
			return e;
		};

		SUBCASE("single in-line replacement") {
			String src = "var foo = 1\n";
			Vector<RefactorTextEdit> edits;
			edits.push_back(edit(0, 4, 0, 7, "bar")); // "foo" -> "bar"
			String out;
			CHECK(GDScriptRefactorEdits::apply(src, edits, out));
			CHECK_EQ(out, "var bar = 1\n");
		}

		SUBCASE("order independence") {
			String src = "ab\ncd\n";
			Vector<RefactorTextEdit> edits;
			edits.push_back(edit(1, 0, 1, 1, "X")); // later position, added first
			edits.push_back(edit(0, 0, 0, 1, "Y")); // earlier position, added second
			String out;
			CHECK(GDScriptRefactorEdits::apply(src, edits, out));
			CHECK_EQ(out, "Yb\nXd\n");
		}

		SUBCASE("overlapping edits rejected") {
			String src = "abcdef";
			Vector<RefactorTextEdit> edits;
			edits.push_back(edit(0, 0, 0, 3, "X"));
			edits.push_back(edit(0, 2, 0, 5, "Y"));
			String out = "untouched";
			CHECK_FALSE(GDScriptRefactorEdits::apply(src, edits, out));
			CHECK_EQ(out, "untouched");
		}

		SUBCASE("multi-line range") {
			String src = "one\ntwo\nthree\n";
			Vector<RefactorTextEdit> edits;
			edits.push_back(edit(0, 1, 2, 2, "X")); // 'n'(line0,col1) .. before 'r'(line2,col2)
			String out;
			CHECK(GDScriptRefactorEdits::apply(src, edits, out));
			CHECK_EQ(out, "oXree\n");
		}
	}

	TEST_CASE("Identifier validation") {
		String reason;
		SUBCASE("accepts valid identifiers") {
			CHECK(GDScriptRefactorNames::validate_identifier("foo", reason));
			CHECK(GDScriptRefactorNames::validate_identifier("_bar", reason));
			CHECK(GDScriptRefactorNames::validate_identifier("baz2", reason));
			CHECK(GDScriptRefactorNames::validate_identifier("foo", reason));
			CHECK(reason.is_empty());
		}
		SUBCASE("rejects empty") {
			CHECK_FALSE(GDScriptRefactorNames::validate_identifier("", reason));
			CHECK_FALSE(reason.is_empty());
		}
		SUBCASE("rejects leading digit") {
			CHECK_FALSE(GDScriptRefactorNames::validate_identifier("2foo", reason));
		}
		SUBCASE("rejects whitespace") {
			CHECK_FALSE(GDScriptRefactorNames::validate_identifier("foo bar", reason));
		}
		SUBCASE("rejects keywords") {
			CHECK_FALSE(GDScriptRefactorNames::validate_identifier("var", reason));
			CHECK_FALSE(GDScriptRefactorNames::validate_identifier("func", reason));
			CHECK_FALSE(GDScriptRefactorNames::validate_identifier("return", reason));
		}
	}

	TEST_CASE("Type annotation rendering") {
		SUBCASE("concrete builtin is annotatable") {
			GDScriptParser::DataType dt;
			dt.kind = GDScriptParser::DataType::BUILTIN;
			dt.builtin_type = Variant::INT;
			dt.type_source = GDScriptParser::DataType::ANNOTATED_EXPLICIT;
			String rendered;
			CHECK(GDScriptRefactorTypes::render_annotatable_type(dt, rendered));
			CHECK_EQ(rendered, "int");
		}
		SUBCASE("variant is not annotatable") {
			GDScriptParser::DataType dt;
			dt.kind = GDScriptParser::DataType::VARIANT;
			String rendered;
			CHECK_FALSE(GDScriptRefactorTypes::render_annotatable_type(dt, rendered));
		}
	}
}

} // namespace GDScriptTests

#endif // TOOLS_ENABLED
