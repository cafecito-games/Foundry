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

#ifndef GDSCRIPT_NO_LSP
#include "test_lsp.h"

#include "editor/file_system/editor_file_system.h"
#endif

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
	TEST_CASE("Rename is reported but disabled at a trivial location") {
		RefactorContext ctx = make_context("modules/gdscript/tests/scripts/refactor/empty.gd");
		Vector<RefactorAvailability> available = GDScriptRefactoring::get_available_refactors(ctx, caret(0, 0));
		REQUIRE_EQ(available.size(), 1);
		CHECK_EQ(available[0].kind, RefactorKind::RENAME);
		CHECK_FALSE(available[0].enabled);
		CHECK_FALSE(available[0].disabled_reason.is_empty());
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
		SUBCASE("inferred concrete type is not annotatable") {
			GDScriptParser::DataType dt;
			dt.kind = GDScriptParser::DataType::BUILTIN;
			dt.builtin_type = Variant::INT;
			dt.type_source = GDScriptParser::DataType::INFERRED;
			String rendered;
			CHECK_FALSE(GDScriptRefactorTypes::render_annotatable_type(dt, rendered));
		}
	}

#ifndef GDSCRIPT_NO_LSP
	TEST_CASE("Rename file-local symbols") {
		EditorFileSystem *editor_file_system = memnew(EditorFileSystem);
		GDScriptLanguageProtocol *protocol = GDScriptTests::initialize(GDScriptTests::root);
		REQUIRE(protocol);

		auto run_rename = [](const String &p_res_path, int p_line, int p_column, const String &p_new_name, String &r_out) -> RefactorResult {
			// Prime the LSP workspace so the document can be resolved and parsed.
			GDScriptTests::assert_no_errors_in(p_res_path);

			RefactorContext ctx;
			ctx.path = p_res_path;
			ctx.source = FileAccess::get_file_as_string(p_res_path);
			RefactorParams params;
			params.new_name = p_new_name;
			RefactorResult r = GDScriptRefactoring::prepare(ctx, caret(p_line, p_column), RefactorKind::RENAME, params);
			if (r.ok) {
				GDScriptRefactorEdits::apply(ctx.source, r.edits, r_out);
			}
			return r;
		};

		SUBCASE("local variable") {
			String out;
			RefactorResult r = run_rename("res://refactor/rename_local.gd", 3, 5, "sum", out); // caret on `total`
			REQUIRE(r.ok);
			CHECK_EQ(r.suggested_name, "total");
			CHECK(out.contains("var sum := 1"));
			CHECK(out.contains("sum += 2"));
			CHECK(out.contains("return sum"));
			CHECK_FALSE(out.contains("total"));
		}
		SUBCASE("file-local member") {
			String out;
			RefactorResult r = run_rename("res://refactor/rename_member.gd", 2, 4, "tally", out); // caret on `counter`
			REQUIRE(r.ok);
			CHECK_EQ(r.suggested_name, "counter");
			CHECK(out.contains("var tally := 0"));
			CHECK(out.contains("tally += 1"));
			CHECK(out.contains("return tally"));
			CHECK_FALSE(out.contains("counter"));
		}
		SUBCASE("strings and comments untouched") {
			String out;
			RefactorResult r = run_rename("res://refactor/rename_strings_comments.gd", 3, 5, "sum", out); // caret on `total`
			REQUIRE(r.ok);
			CHECK(out.contains("var sum := 1"));
			CHECK(out.contains("print(sum)"));
			CHECK(out.contains("# total is a comment word")); // comment unchanged
			CHECK(out.contains("\"total in a string\"")); // string unchanged
		}
		SUBCASE("availability reports rename enabled on a symbol") {
			GDScriptTests::assert_no_errors_in("res://refactor/rename_local.gd");
			RefactorContext ctx;
			ctx.path = "res://refactor/rename_local.gd";
			ctx.source = FileAccess::get_file_as_string(ctx.path);
			Vector<RefactorAvailability> available = GDScriptRefactoring::get_available_refactors(ctx, caret(3, 5));
			REQUIRE_EQ(available.size(), 1);
			CHECK_EQ(available[0].kind, RefactorKind::RENAME);
			CHECK(available[0].enabled);
		}
		SUBCASE("function and all call sites") {
			String out;
			RefactorResult r = run_rename("res://refactor/rename_function.gd", 2, 5, "worker", out); // caret on `helper` decl
			REQUIRE(r.ok);
			CHECK(out.contains("func worker() -> int:"));
			CHECK(out.contains("return worker() + worker()"));
			CHECK_FALSE(out.contains("helper"));
		}
		SUBCASE("rejects invalid new name") {
			String out;
			RefactorResult bad_syntax = run_rename("res://refactor/rename_local.gd", 3, 5, "1bad", out); // caret on `total`
			CHECK_FALSE(bad_syntax.ok);
			CHECK_FALSE(bad_syntax.error_message.is_empty());

			RefactorResult keyword = run_rename("res://refactor/rename_local.gd", 3, 5, "class", out);
			CHECK_FALSE(keyword.ok);
			CHECK_FALSE(keyword.error_message.is_empty());
		}
		SUBCASE("availability disabled off a symbol") {
			GDScriptTests::assert_no_errors_in("res://refactor/rename_local.gd");
			RefactorContext ctx;
			ctx.path = "res://refactor/rename_local.gd";
			ctx.source = FileAccess::get_file_as_string(ctx.path);
			Vector<RefactorAvailability> available = GDScriptRefactoring::get_available_refactors(ctx, caret(1, 0)); // blank line
			REQUIRE_EQ(available.size(), 1);
			CHECK_FALSE(available[0].enabled);
			CHECK_FALSE(available[0].disabled_reason.is_empty());
		}
		SUBCASE("rejects in-scope collision") {
			String out;
			RefactorResult r = run_rename("res://refactor/rename_collision.gd", 3, 5, "b", out); // caret on `a` decl
			CHECK_FALSE(r.ok);
			CHECK_FALSE(r.error_message.is_empty());
			CHECK(r.error_message.to_lower().contains("scope"));
		}
		SUBCASE("warns when renaming an exported variable") {
			String out;
			RefactorResult r = run_rename("res://refactor/rename_exported.gd", 2, 12, "velocity", out); // caret on `speed`
			REQUIRE(r.ok);
			CHECK_FALSE(r.warning.is_empty());
			CHECK(r.warning.to_lower().contains("exported"));
			// The in-file edits still apply: declaration and use are both updated.
			CHECK(out.contains("@export var velocity := 1.0"));
			CHECK(out.contains("return velocity"));
			CHECK_FALSE(out.contains("speed"));
		}
		SUBCASE("member rename stays within its own file") {
			// rename_xfile_a.gd and rename_xfile_b.gd both declare an unrelated
			// `var shared_value`. Renaming A's member must only touch A.
			String out;
			RefactorResult r = run_rename("res://refactor/rename_xfile_a.gd", 2, 4, "renamed_value", out); // caret on A's `shared_value`
			REQUIRE(r.ok);
			CHECK(out.contains("var renamed_value := 0"));
			CHECK(out.contains("renamed_value += 1"));
			CHECK(out.contains("return renamed_value"));
			CHECK_FALSE(out.contains("shared_value"));

			// Every emitted edit targets a line that exists within A; the engine's
			// URI filter keeps edits single-file, so B's same-named symbol is untouched.
			const RefactorContext a_ctx = make_context("res://refactor/rename_xfile_a.gd");
			const int a_line_count = a_ctx.source.split("\n").size();
			for (const RefactorTextEdit &edit : r.edits) {
				CHECK(edit.start_line >= 0);
				CHECK(edit.start_line < a_line_count);
			}

			// B on disk is unaffected: it still declares the original symbol name.
			const String b_source = FileAccess::get_file_as_string("res://refactor/rename_xfile_b.gd");
			CHECK(b_source.contains("var shared_value := 0"));
			CHECK_FALSE(b_source.contains("renamed_value"));
		}
		SUBCASE("allows same name in a different scope") {
			String out;
			RefactorResult r = run_rename("res://refactor/rename_diff_scope.gd", 3, 5, "y", out); // caret on `x` in first()
			REQUIRE(r.ok); // `y` exists only in second(), not in first()'s scope
			CHECK(out.contains("var y := 1"));
			CHECK(out.contains("print(y)"));
		}
		SUBCASE("rejects collision with a parameter") {
			String out;
			RefactorResult r = run_rename("res://refactor/rename_param_collision.gd", 3, 5, "value", out); // caret on `temp`
			CHECK_FALSE(r.ok);
			CHECK(r.error_message.to_lower().contains("scope"));
		}

		memdelete(protocol);
		memdelete(editor_file_system);
		GDScriptTests::finish_language();
	}
#endif // GDSCRIPT_NO_LSP
}

} // namespace GDScriptTests

#endif // TOOLS_ENABLED
