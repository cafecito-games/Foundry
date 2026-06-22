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

inline RefactorLocation selection(int p_start_line, int p_start_column, int p_end_line, int p_end_column) {
	RefactorLocation loc;
	loc.start_line = p_start_line;
	loc.start_column = p_start_column;
	loc.end_line = p_end_line;
	loc.end_column = p_end_column;
	return loc;
}

inline RefactorResult run_type_annotation(const String &p_source, int p_line, int p_column, String &r_out) {
	RefactorContext ctx;
	ctx.path = "user://type_annotation_refactor.gd";
	ctx.source = p_source;
	RefactorParams params;
	RefactorResult r = GDScriptRefactoring::prepare(ctx, caret(p_line, p_column), RefactorKind::ADD_TYPE_ANNOTATION, params);
	if (r.ok) {
		GDScriptRefactorEdits::apply(ctx.source, r.edits, r_out);
	}
	return r;
}

inline RefactorResult run_extract_variable(const String &p_source, const RefactorLocation &p_location, String &r_out) {
	RefactorContext ctx;
	ctx.path = "user://extract_variable_refactor.gd";
	ctx.source = p_source;
	RefactorParams params;
	RefactorResult r = GDScriptRefactoring::prepare(ctx, p_location, RefactorKind::EXTRACT_VARIABLE, params);
	if (r.ok) {
		GDScriptRefactorEdits::apply(ctx.source, r.edits, r_out);
	}
	return r;
}

inline RefactorResult run_extract_method(const String &p_source, const RefactorLocation &p_location, String &r_out) {
	RefactorContext ctx;
	ctx.path = "user://extract_method_refactor.gd";
	ctx.source = p_source;
	RefactorParams params;
	RefactorResult r = GDScriptRefactoring::prepare(ctx, p_location, RefactorKind::EXTRACT_METHOD, params);
	if (r.ok) {
		GDScriptRefactorEdits::apply(ctx.source, r.edits, r_out);
	}
	return r;
}

inline RefactorResult run_inline_variable(const String &p_source, int p_line, int p_column, String &r_out) {
	RefactorContext ctx;
	ctx.path = "user://inline_variable_refactor.gd";
	ctx.source = p_source;
	RefactorParams params;
	RefactorResult r = GDScriptRefactoring::prepare(ctx, caret(p_line, p_column), RefactorKind::INLINE_VARIABLE, params);
	if (r.ok) {
		GDScriptRefactorEdits::apply(ctx.source, r.edits, r_out);
	}
	return r;
}

inline const RefactorFileEdit *find_file_edit(const RefactorResult &p_result, const String &p_path) {
	for (const RefactorFileEdit &file_edit : p_result.file_edits) {
		if (file_edit.path == p_path) {
			return &file_edit;
		}
	}
	return nullptr;
}

TEST_SUITE("[Modules][GDScript][Refactor]") {
	TEST_CASE("Rename is reported but disabled at a trivial location") {
		RefactorContext ctx = make_context("modules/gdscript/tests/scripts/refactor/empty.gd");
		Vector<RefactorAvailability> available = GDScriptRefactoring::get_available_refactors(ctx, caret(0, 0));
		CHECK_EQ(available.size(), 5);
		if (available.size() < 5) {
			return;
		}
		CHECK_EQ(available[0].kind, RefactorKind::RENAME);
		CHECK_FALSE(available[0].enabled);
		CHECK_FALSE(available[0].disabled_reason.is_empty());
		CHECK_EQ(available[1].kind, RefactorKind::EXTRACT_VARIABLE);
		CHECK_FALSE(available[1].enabled);
		CHECK_FALSE(available[1].disabled_reason.is_empty());
		CHECK_EQ(available[2].kind, RefactorKind::EXTRACT_METHOD);
		CHECK_FALSE(available[2].enabled);
		CHECK_FALSE(available[2].disabled_reason.is_empty());
		CHECK_EQ(available[3].kind, RefactorKind::ADD_TYPE_ANNOTATION);
		CHECK_FALSE(available[3].enabled);
		CHECK_FALSE(available[3].disabled_reason.is_empty());
		CHECK_EQ(available[4].kind, RefactorKind::INLINE_VARIABLE);
		CHECK_FALSE(available[4].enabled);
		CHECK_FALSE(available[4].disabled_reason.is_empty());
	}

	TEST_CASE("Edit application") {
		auto edit = [](int sl, int sc, int el, int ec, const String &t) {
			RefactorTextEdit e;
			e.start_line = sl;
			e.start_column = sc;
			e.end_line = el;
			e.end_column = ec;
			e.new_text = t;
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
		SUBCASE("accepts unicode identifiers") {
			CHECK(GDScriptRefactorNames::validate_identifier(String::utf8("café"), reason));
			CHECK(GDScriptRefactorNames::validate_identifier(String::utf8("número"), reason));
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
		SUBCASE("inferred concrete type is annotatable") {
			GDScriptParser::DataType dt;
			dt.kind = GDScriptParser::DataType::BUILTIN;
			dt.builtin_type = Variant::INT;
			dt.type_source = GDScriptParser::DataType::INFERRED;
			String rendered;
			CHECK(GDScriptRefactorTypes::render_annotatable_type(dt, rendered));
			CHECK_EQ(rendered, "int");
		}
		SUBCASE("unresolved type is not annotatable") {
			GDScriptParser::DataType dt;
			dt.kind = GDScriptParser::DataType::UNRESOLVED;
			dt.type_source = GDScriptParser::DataType::INFERRED;
			String rendered;
			CHECK_FALSE(GDScriptRefactorTypes::render_annotatable_type(dt, rendered));
		}
		SUBCASE("null type is not annotatable") {
			GDScriptParser::DataType dt;
			dt.kind = GDScriptParser::DataType::BUILTIN;
			dt.builtin_type = Variant::NIL;
			dt.type_source = GDScriptParser::DataType::INFERRED;
			String rendered;
			CHECK_FALSE(GDScriptRefactorTypes::render_annotatable_type(dt, rendered));
		}
	}

	TEST_CASE("Add type annotation inserts concrete inferred types") {
		SUBCASE("variable") {
			const String source = "var score = 1\n";
			String out;
			RefactorResult r = run_type_annotation(source, 0, 1, out);
			REQUIRE(r.ok);
			CHECK_EQ(out, "var score: int = 1\n");
		}
		SUBCASE("variable without assignment spacing") {
			const String source = "var score=1\n";
			String out;
			RefactorResult r = run_type_annotation(source, 0, 1, out);
			REQUIRE(r.ok);
			CHECK_EQ(out, "var score: int = 1\n");
		}
		SUBCASE("constant") {
			const String source = "const TITLE = \"Cafecito\"\n";
			String out;
			RefactorResult r = run_type_annotation(source, 0, 2, out);
			REQUIRE(r.ok);
			CHECK_EQ(out, "const TITLE: String = \"Cafecito\"\n");
		}
		SUBCASE("parameter") {
			const String source = "func scale(amount = 1.0) -> void:\n\tpass\n";
			String out;
			RefactorResult r = run_type_annotation(source, 0, 12, out);
			REQUIRE(r.ok);
			CHECK_EQ(out, "func scale(amount: float = 1.0) -> void:\n\tpass\n");
		}
		SUBCASE("function return") {
			const String source = "func make_score():\n\treturn 1\n";
			String out;
			RefactorResult r = run_type_annotation(source, 0, 5, out);
			REQUIRE(r.ok);
			CHECK_EQ(out, "func make_score() -> int:\n\treturn 1\n");
		}
		SUBCASE("function return when parameter repeats function name") {
			const String source = "func value(value = 1):\n\treturn 1\n";
			String out;
			RefactorResult r = run_type_annotation(source, 0, 5, out);
			REQUIRE(r.ok);
			CHECK_EQ(out, "func value(value = 1) -> int:\n\treturn 1\n");
		}
	}

	TEST_CASE("Add type annotation preserves strict type spelling") {
		const String source =
				"signal selected(name: String)\n"
				"func accept_score(score: int) -> bool:\n"
				"\treturn true\n"
				"func get_maybe_node() -> Node?:\n"
				"\treturn null\n"
				"func get_callback() -> Callable[[int], bool]:\n"
				"\treturn accept_score\n"
				"func get_selected() -> Signal[[String]]:\n"
				"\treturn selected\n"
				"func get_nested() -> Dictionary[String, Array[Callable[[int], bool]]]:\n"
				"\treturn {}\n"
				"var maybe_node = get_maybe_node()\n"
				"var callback = get_callback()\n"
				"var selected_signal = get_selected()\n"
				"var nested = get_nested()\n";

		SUBCASE("nullable") {
			String out;
			RefactorResult r = run_type_annotation(source, 11, 5, out);
			REQUIRE(r.ok);
			CHECK(out.contains("var maybe_node: Node? = get_maybe_node()"));
		}
		SUBCASE("callable signature") {
			String out;
			RefactorResult r = run_type_annotation(source, 12, 5, out);
			REQUIRE(r.ok);
			CHECK(out.contains("var callback: Callable[[int], bool] = get_callback()"));
		}
		SUBCASE("signal signature") {
			String out;
			RefactorResult r = run_type_annotation(source, 13, 5, out);
			REQUIRE(r.ok);
			CHECK(out.contains("var selected_signal: Signal[[String]] = get_selected()"));
		}
		SUBCASE("nested typed containers") {
			String out;
			RefactorResult r = run_type_annotation(source, 14, 5, out);
			REQUIRE(r.ok);
			CHECK(out.contains("var nested: Dictionary[String, Array[Callable[[int], bool]]] = get_nested()"));
		}
	}

	TEST_CASE("Add type annotation repairs inferred declaration syntax") {
		const String source =
				"func typed_value() -> int:\n"
				"\treturn 1\n"
				"func run(value := typed_value()) -> void:\n"
				"\tvar local := typed_value()\n";

		SUBCASE("variable colon-equals") {
			String out;
			RefactorResult r = run_type_annotation(source, 3, 6, out);
			REQUIRE(r.ok);
			CHECK(out.contains("\tvar local: int = typed_value()"));
			CHECK_FALSE(out.contains("local :="));
		}
		SUBCASE("parameter colon-equals") {
			String out;
			RefactorResult r = run_type_annotation(source, 2, 10, out);
			REQUIRE(r.ok);
			CHECK(out.contains("func run(value: int = typed_value()) -> void:"));
			CHECK_FALSE(out.contains("value :="));
		}
	}

	TEST_CASE("Add type annotation rejects non-annotatable inferred types") {
		SUBCASE("variant inferred variable") {
			const String source =
					"func dynamic_value() -> Variant:\n"
					"\treturn 1\n"
					"var value = dynamic_value()\n";
			String out;
			RefactorResult r = run_type_annotation(source, 2, 5, out);
			CHECK_FALSE(r.ok);
			CHECK_FALSE(r.error_message.is_empty());
		}
		SUBCASE("already explicit variable") {
			const String source = "var value: int = 1\n";
			String out;
			RefactorResult r = run_type_annotation(source, 0, 5, out);
			CHECK_FALSE(r.ok);
			CHECK_FALSE(r.error_message.is_empty());
		}
		SUBCASE("return-less function with variant return") {
			const String source =
					"func dynamic_value() -> Variant:\n"
					"\treturn 1\n"
					"func passthrough():\n"
					"\treturn dynamic_value()\n";
			String out;
			RefactorResult r = run_type_annotation(source, 2, 5, out);
			CHECK_FALSE(r.ok);
			CHECK_FALSE(r.error_message.is_empty());
		}
	}

	TEST_CASE("Extract variable inserts typed local and replaces selected expression") {
		SUBCASE("return expression") {
			const String source =
					"func calculate(base: int) -> int:\n"
					"\treturn base + 2\n";
			String out;
			RefactorResult r = run_extract_variable(source, selection(1, 8, 1, 16), out);
			REQUIRE(r.ok);
			CHECK_EQ(r.suggested_name, "value");
			CHECK_EQ(r.rename_anchor_line, 1);
			CHECK_EQ(r.rename_anchor_column, 5);
			CHECK_EQ(out,
					"func calculate(base: int) -> int:\n"
					"\tvar value: int = base + 2\n"
					"\treturn value\n");
		}
		SUBCASE("call expression name") {
			const String source =
					"func get_score() -> int:\n"
					"\treturn 1\n"
					"func run() -> int:\n"
					"\treturn get_score()\n";
			String out;
			RefactorResult r = run_extract_variable(source, selection(3, 8, 3, 19), out);
			REQUIRE(r.ok);
			CHECK_EQ(r.suggested_name, "score");
			CHECK(out.contains("\tvar score: int = get_score()\n"));
			CHECK(out.contains("\treturn score\n"));
		}
		SUBCASE("avoids local name collisions") {
			const String source =
					"func run() -> int:\n"
					"\tvar value: int = 0\n"
					"\treturn 1 + 2\n";
			String out;
			RefactorResult r = run_extract_variable(source, selection(2, 8, 2, 13), out);
			REQUIRE(r.ok);
			CHECK_EQ(r.suggested_name, "value_2");
			CHECK(out.contains("\tvar value_2: int = 1 + 2\n"));
			CHECK(out.contains("\treturn value_2\n"));
		}
		SUBCASE("compound assignment") {
			const String source =
					"func get_delta() -> int:\n"
					"\treturn 2\n"
					"func run() -> void:\n"
					"\tvar total := 1\n"
					"\ttotal += get_delta()\n";
			String out;
			RefactorResult r = run_extract_variable(source, selection(4, 10, 4, 21), out);
			REQUIRE(r.ok);
			CHECK(out.contains("\tvar delta: int = get_delta()\n"));
			CHECK(out.contains("\ttotal += delta\n"));
		}
		SUBCASE("member assignment") {
			const String source =
					"var total := 0\n"
					"func get_delta() -> int:\n"
					"\treturn 2\n"
					"func run() -> void:\n"
					"\tself.total = get_delta()\n";
			String out;
			RefactorResult r = run_extract_variable(source, selection(4, 14, 4, 25), out);
			REQUIRE(r.ok);
			CHECK(out.contains("\tvar delta: int = get_delta()\n"));
			CHECK(out.contains("\tself.total = delta\n"));
		}
		SUBCASE("if condition") {
			const String source =
					"func check_ready() -> bool:\n"
					"\treturn true\n"
					"func run() -> void:\n"
					"\tif check_ready():\n"
					"\t\tpass\n";
			String out;
			RefactorResult r = run_extract_variable(source, selection(3, 4, 3, 17), out);
			REQUIRE(r.ok);
			CHECK(out.contains("\tvar check_ready: bool = check_ready()\n"));
			CHECK(out.contains("\tif check_ready:\n"));
		}
		SUBCASE("for list") {
			const String source =
					"func get_items() -> Array[int]:\n"
					"\treturn [1]\n"
					"func run() -> void:\n"
					"\tfor item in get_items():\n"
					"\t\tpass\n";
			String out;
			RefactorResult r = run_extract_variable(source, selection(3, 13, 3, 24), out);
			REQUIRE(r.ok);
			CHECK(out.contains("\tvar items: Array[int] = get_items()\n"));
			CHECK(out.contains("\tfor item in items:\n"));
		}
		SUBCASE("match subject") {
			const String source =
					"func get_value() -> int:\n"
					"\treturn 1\n"
					"func run() -> void:\n"
					"\tmatch get_value():\n"
					"\t\t1:\n"
					"\t\t\tpass\n";
			String out;
			RefactorResult r = run_extract_variable(source, selection(3, 7, 3, 18), out);
			REQUIRE(r.ok);
			CHECK(out.contains("\tvar value: int = get_value()\n"));
			CHECK(out.contains("\tmatch value:\n"));
		}
	}

	TEST_CASE("Extract variable availability follows expression selection") {
		const String source =
				"func calculate(base: int) -> int:\n"
				"\treturn base + 2\n";
		RefactorContext ctx;
		ctx.path = "user://extract_variable_availability.gd";
		ctx.source = source;
		Vector<RefactorAvailability> available = GDScriptRefactoring::get_available_refactors(ctx, selection(1, 8, 1, 16));
		REQUIRE_EQ(available.size(), 5);
		if (available.size() >= 2) {
			CHECK_EQ(available[1].kind, RefactorKind::EXTRACT_VARIABLE);
			CHECK(available[1].enabled);
			CHECK(available[1].disabled_reason.is_empty());
		}
	}

	TEST_CASE("Extract variable rejects unsafe or unprovable selections") {
		SUBCASE("requires a selection") {
			const String source =
					"func calculate(base: int) -> int:\n"
					"\treturn base + 2\n";
			String out;
			RefactorResult r = run_extract_variable(source, caret(1, 8), out);
			CHECK_FALSE(r.ok);
			CHECK_FALSE(r.error_message.is_empty());
		}
		SUBCASE("rejects subexpressions inside larger expressions") {
			const String source =
					"func calculate(base: int) -> int:\n"
					"\treturn base + 2\n";
			String out;
			RefactorResult r = run_extract_variable(source, selection(1, 8, 1, 12), out);
			CHECK_FALSE(r.ok);
			CHECK_FALSE(r.error_message.is_empty());
		}
		SUBCASE("rejects non-annotatable Variant expressions") {
			const String source =
					"func dynamic_value() -> Variant:\n"
					"\treturn 1\n"
					"func run() -> Variant:\n"
					"\treturn dynamic_value()\n";
			String out;
			RefactorResult r = run_extract_variable(source, selection(3, 8, 3, 23), out);
			CHECK_FALSE(r.ok);
			CHECK_FALSE(r.error_message.is_empty());
		}
		SUBCASE("rejects while conditions") {
			const String source =
					"func should_continue() -> bool:\n"
					"\treturn true\n"
					"func run() -> void:\n"
					"\twhile should_continue():\n"
					"\t\tpass\n";
			String out;
			RefactorResult r = run_extract_variable(source, selection(3, 7, 3, 24), out);
			CHECK_FALSE(r.ok);
			CHECK(r.error_message.to_lower().contains("while"));
		}
		SUBCASE("rejects assert expressions") {
			const String source =
					"func check_ready() -> bool:\n"
					"\treturn true\n"
					"func run() -> void:\n"
					"\tassert(check_ready())\n";
			String out;
			RefactorResult r = run_extract_variable(source, selection(3, 8, 3, 21), out);
			CHECK_FALSE(r.ok);
			CHECK(r.error_message.to_lower().contains("assert"));
		}
	}

	TEST_CASE("Extract method inserts nearby helper and replaces selected statements") {
		SUBCASE("no parameters") {
			const String source =
					"func run() -> void:\n"
					"\tprint(\"ready\")\n"
					"\tprint(\"done\")\n";
			String out;
			RefactorResult r = run_extract_method(source, selection(1, 0, 2, 0), out);
			REQUIRE(r.ok);
			CHECK_EQ(r.suggested_name, "_extracted_method");
			CHECK_EQ(out,
					"func run() -> void:\n"
					"\t_extracted_method()\n"
					"\tprint(\"done\")\n"
					"\n"
					"func _extracted_method() -> void:\n"
					"\tprint(\"ready\")\n");
		}
		SUBCASE("multiple parameters") {
			const String source =
					"func run(a: int, b: int) -> void:\n"
					"\tprint(a + b)\n"
					"\tprint(\"done\")\n";
			String out;
			RefactorResult r = run_extract_method(source, selection(1, 0, 2, 0), out);
			REQUIRE(r.ok);
			CHECK_EQ(out,
					"func run(a: int, b: int) -> void:\n"
					"\t_extracted_method(a, b)\n"
					"\tprint(\"done\")\n"
					"\n"
					"func _extracted_method(a: int, b: int) -> void:\n"
					"\tprint(a + b)\n");
		}
		SUBCASE("single output local uses explicit call-site type") {
			const String source =
					"func run(a: int, b: int) -> void:\n"
					"\tvar total := a + b\n"
					"\tprint(total)\n";
			String out;
			RefactorResult r = run_extract_method(source, selection(1, 0, 2, 0), out);
			REQUIRE(r.ok);
			CHECK_EQ(out,
					"func run(a: int, b: int) -> void:\n"
					"\tvar total: int = _extracted_method(a, b)\n"
					"\tprint(total)\n"
					"\n"
					"func _extracted_method(a: int, b: int) -> int:\n"
					"\tvar total := a + b\n"
					"\treturn total\n");
		}
		SUBCASE("single output local inside nested suite") {
			const String source =
					"func run(flag: bool) -> void:\n"
					"\tif flag:\n"
					"\t\tvar total := 1\n"
					"\t\tprint(total)\n";
			String out;
			RefactorResult r = run_extract_method(source, selection(2, 0, 3, 0), out);
			REQUIRE(r.ok);
			CHECK_EQ(out,
					"func run(flag: bool) -> void:\n"
					"\tif flag:\n"
					"\t\tvar total: int = _extracted_method()\n"
					"\t\tprint(total)\n"
					"\n"
					"func _extracted_method() -> int:\n"
					"\tvar total := 1\n"
					"\treturn total\n");
		}
		SUBCASE("blank lines are preserved without trailing whitespace") {
			const String source =
					"func run() -> void:\n"
					"\tprint(\"ready\")\n"
					"\n"
					"\tprint(\"done\")\n"
					"\tprint(\"after\")\n";
			String out;
			RefactorResult r = run_extract_method(source, selection(1, 0, 4, 0), out);
			REQUIRE(r.ok);
			CHECK_EQ(out,
					"func run() -> void:\n"
					"\t_extracted_method()\n"
					"\tprint(\"after\")\n"
					"\n"
					"func _extracted_method() -> void:\n"
					"\tprint(\"ready\")\n"
					"\n"
					"\tprint(\"done\")\n");
		}
		SUBCASE("appends helper when source has no final newline") {
			const String source =
					"func run() -> void:\n"
					"\tprint(\"ready\")\n"
					"\tprint(\"done\")";
			String out;
			RefactorResult r = run_extract_method(source, selection(1, 0, 2, 0), out);
			REQUIRE(r.ok);
			CHECK_EQ(out,
					"func run() -> void:\n"
					"\t_extracted_method()\n"
					"\tprint(\"done\")\n"
					"\n"
					"func _extracted_method() -> void:\n"
					"\tprint(\"ready\")\n");
		}
		SUBCASE("preserves nested control-flow indentation") {
			const String source =
					"func run(flag: bool) -> void:\n"
					"\tif flag:\n"
					"\t\tprint(\"yes\")\n"
					"\tprint(\"done\")\n";
			String out;
			RefactorResult r = run_extract_method(source, selection(1, 0, 3, 0), out);
			REQUIRE(r.ok);
			CHECK_EQ(out,
					"func run(flag: bool) -> void:\n"
					"\t_extracted_method(flag)\n"
					"\tprint(\"done\")\n"
					"\n"
					"func _extracted_method(flag: bool) -> void:\n"
					"\tif flag:\n"
					"\t\tprint(\"yes\")\n");
		}
		SUBCASE("avoids method name collisions") {
			const String source =
					"func run() -> void:\n"
					"\tprint(\"ready\")\n"
					"\tprint(\"done\")\n"
					"\n"
					"func _extracted_method() -> void:\n"
					"\tpass\n";
			String out;
			RefactorResult r = run_extract_method(source, selection(1, 0, 2, 0), out);
			REQUIRE(r.ok);
			CHECK_EQ(r.suggested_name, "_extracted_method_2");
			CHECK(out.contains("\t_extracted_method_2()\n"));
			CHECK(out.contains("func _extracted_method_2() -> void:\n"));
		}
	}

	TEST_CASE("Extract method availability follows whole-statement selection") {
		const String source =
				"func run() -> void:\n"
				"\tprint(\"ready\")\n"
				"\tprint(\"done\")\n";
		RefactorContext ctx;
		ctx.path = "user://extract_method_availability.gd";
		ctx.source = source;
		Vector<RefactorAvailability> available = GDScriptRefactoring::get_available_refactors(ctx, selection(1, 0, 2, 0));
		REQUIRE_EQ(available.size(), 5);
		CHECK_EQ(available[2].kind, RefactorKind::EXTRACT_METHOD);
		CHECK(available[2].enabled);
		CHECK(available[2].disabled_reason.is_empty());
	}

	TEST_CASE("Extract method rejects unsafe or unprovable selections") {
		SUBCASE("requires whole statements") {
			const String source =
					"func run() -> void:\n"
					"\tprint(\"ready\")\n";
			String out;
			RefactorResult r = run_extract_method(source, selection(1, 1, 1, 6), out);
			CHECK_FALSE(r.ok);
			CHECK_FALSE(r.error_message.is_empty());
		}
		SUBCASE("rejects multiple output locals") {
			const String source =
					"func run(a: int, b: int) -> void:\n"
					"\tvar first := a + 1\n"
					"\tvar second := b + 1\n"
					"\tprint(first + second)\n";
			String out;
			RefactorResult r = run_extract_method(source, selection(1, 0, 3, 0), out);
			CHECK_FALSE(r.ok);
			CHECK(r.error_message.to_lower().contains("multiple"));
		}
		SUBCASE("rejects assignment to an existing output local") {
			const String source =
					"func run(a: int, b: int) -> void:\n"
					"\tvar total := 0\n"
					"\ttotal = a + b\n"
					"\tprint(total)\n";
			String out;
			RefactorResult r = run_extract_method(source, selection(2, 0, 3, 0), out);
			CHECK_FALSE(r.ok);
			CHECK(r.error_message.to_lower().contains("assignment"));
		}
		SUBCASE("rejects Variant parameters") {
			const String source =
					"func run(value: Variant) -> void:\n"
					"\tprint(value)\n";
			String out;
			RefactorResult r = run_extract_method(source, selection(1, 0, 2, 0), out);
			CHECK_FALSE(r.ok);
			CHECK(r.error_message.to_lower().contains("parameter"));
		}
		SUBCASE("rejects Variant output locals") {
			const String source =
					"func dynamic_value() -> Variant:\n"
					"\treturn 1\n"
					"func run() -> void:\n"
					"\tvar value := dynamic_value()\n"
					"\tprint(value)\n";
			String out;
			RefactorResult r = run_extract_method(source, selection(3, 0, 4, 0), out);
			CHECK_FALSE(r.ok);
			CHECK(r.error_message.to_lower().contains("return type"));
		}
		SUBCASE("rejects selections spanning functions") {
			const String source =
					"func first() -> void:\n"
					"\tprint(\"first\")\n"
					"func second() -> void:\n"
					"\tprint(\"second\")\n";
			String out;
			RefactorResult r = run_extract_method(source, selection(1, 0, 4, 0), out);
			CHECK_FALSE(r.ok);
			CHECK_FALSE(r.error_message.is_empty());
		}
		SUBCASE("rejects return") {
			const String source =
					"func run() -> int:\n"
					"\treturn 1\n";
			String out;
			RefactorResult r = run_extract_method(source, selection(1, 0, 2, 0), out);
			CHECK_FALSE(r.ok);
			CHECK(r.error_message.to_lower().contains("return"));
		}
		SUBCASE("rejects break") {
			const String source =
					"func run() -> void:\n"
					"\twhile true:\n"
					"\t\tbreak\n";
			String out;
			RefactorResult r = run_extract_method(source, selection(1, 0, 3, 0), out);
			CHECK_FALSE(r.ok);
			CHECK(r.error_message.to_lower().contains("break"));
		}
		SUBCASE("rejects continue") {
			const String source =
					"func run() -> void:\n"
					"\twhile true:\n"
					"\t\tcontinue\n";
			String out;
			RefactorResult r = run_extract_method(source, selection(1, 0, 3, 0), out);
			CHECK_FALSE(r.ok);
			CHECK(r.error_message.to_lower().contains("continue"));
		}
		SUBCASE("rejects await") {
			const String source =
					"signal finished\n"
					"func run() -> void:\n"
					"\tawait finished\n"
					"\tprint(\"done\")\n";
			String out;
			RefactorResult r = run_extract_method(source, selection(2, 0, 3, 0), out);
			CHECK_FALSE(r.ok);
			CHECK(r.error_message.to_lower().contains("await"));
		}
		SUBCASE("rejects lambda") {
			const String source =
					"func run(factor: int) -> void:\n"
					"\tvar unused := func(x: int) -> int:\n"
					"\t\treturn x * factor\n"
					"\tprint(\"done\")\n";
			String out;
			RefactorResult r = run_extract_method(source, selection(1, 0, 3, 0), out);
			CHECK_FALSE(r.ok);
			CHECK(r.error_message.to_lower().contains("lambda"));
		}
	}

	TEST_CASE("Inline variable removes local declaration and replaces reads") {
		SUBCASE("single use from declaration") {
			const String source =
					"func run() -> int:\n"
					"\tvar value = 2\n"
					"\treturn value\n";
			String out;
			RefactorResult r = run_inline_variable(source, 1, 6, out);
			REQUIRE(r.ok);
			CHECK_EQ(out,
					"func run() -> int:\n"
					"\treturn 2\n");
		}
		SUBCASE("single use from reference") {
			const String source =
					"func run() -> int:\n"
					"\tvar value = 2\n"
					"\treturn value\n";
			String out;
			RefactorResult r = run_inline_variable(source, 2, 9, out);
			REQUIRE(r.ok);
			CHECK_EQ(out,
					"func run() -> int:\n"
					"\treturn 2\n");
		}
		SUBCASE("reference caret chooses identifier under caret") {
			const String source =
					"func run() -> int:\n"
					"\tvar a = 1\n"
					"\tvar b = 2\n"
					"\treturn a + b\n";
			String out;
			RefactorResult r = run_inline_variable(source, 3, 12, out);
			REQUIRE(r.ok);
			CHECK_EQ(out,
					"func run() -> int:\n"
					"\tvar a = 1\n"
					"\treturn a + 2\n");
		}
		SUBCASE("multi-use pure initializer") {
			const String source =
					"func run(base: int) -> int:\n"
					"\tvar value = base + 1\n"
					"\tprint(value)\n"
					"\treturn value\n";
			String out;
			RefactorResult r = run_inline_variable(source, 1, 6, out);
			REQUIRE(r.ok);
			CHECK_EQ(out,
					"func run(base: int) -> int:\n"
					"\tprint(base + 1)\n"
					"\treturn base + 1\n");
		}
		SUBCASE("parenthesizes lower-precedence initializer") {
			const String source =
					"func run(x: int, a: int, b: int) -> int:\n"
					"\tvar value = a + b\n"
					"\treturn x * value\n";
			String out;
			RefactorResult r = run_inline_variable(source, 1, 6, out);
			REQUIRE(r.ok);
			CHECK_EQ(out,
					"func run(x: int, a: int, b: int) -> int:\n"
					"\treturn x * (a + b)\n");
		}
		SUBCASE("parenthesizes same-precedence right initializer") {
			const String source =
					"func run(x: int, a: int, b: int) -> int:\n"
					"\tvar value = a + b\n"
					"\treturn x + value\n";
			String out;
			RefactorResult r = run_inline_variable(source, 1, 6, out);
			REQUIRE(r.ok);
			CHECK_EQ(out,
					"func run(x: int, a: int, b: int) -> int:\n"
					"\treturn x + (a + b)\n");
		}
		SUBCASE("parenthesizes logical-not initializer in comparison") {
			const String source =
					"func run(a: bool, done: bool) -> bool:\n"
					"\tvar ok = not a\n"
					"\treturn ok == done\n";
			String out;
			RefactorResult r = run_inline_variable(source, 1, 6, out);
			REQUIRE(r.ok);
			CHECK_EQ(out,
					"func run(a: bool, done: bool) -> bool:\n"
					"\treturn (not a) == done\n");
		}
		SUBCASE("parenthesizes awaited initializer operand") {
			const String source =
					"func run(a, b) -> void:\n"
					"\tvar value = a + b\n"
					"\tawait value\n";
			String out;
			RefactorResult r = run_inline_variable(source, 1, 6, out);
			REQUIRE(r.ok);
			CHECK_EQ(out,
					"func run(a, b) -> void:\n"
					"\tawait (a + b)\n");
		}
		SUBCASE("parenthesizes power initializer as left power operand") {
			const String source =
					"func run(a: float, b: float, c: float) -> float:\n"
					"\tvar value = a ** b\n"
					"\treturn value ** c\n";
			String out;
			RefactorResult r = run_inline_variable(source, 1, 6, out);
			REQUIRE(r.ok);
			CHECK_EQ(out,
					"func run(a: float, b: float, c: float) -> float:\n"
					"\treturn (a ** b) ** c\n");
		}
		SUBCASE("single-use side-effecting initializer") {
			const String source =
					"func get_value() -> int:\n"
					"\treturn 1\n"
					"func run() -> int:\n"
					"\tvar value = get_value()\n"
					"\treturn value\n";
			String out;
			RefactorResult r = run_inline_variable(source, 3, 6, out);
			REQUIRE(r.ok);
			CHECK_EQ(out,
					"func get_value() -> int:\n"
					"\treturn 1\n"
					"func run() -> int:\n"
					"\treturn get_value()\n");
		}
		SUBCASE("same-name local in another function is not replaced") {
			const String source =
					"extends Node\n"
					"\n"
					"func other() -> void:\n"
					"\tvar value = 2\n"
					"\tprint(value)\n"
					"\n"
					"func run() -> int:\n"
					"\tvar value = 1\n"
					"\treturn value\n";
			String out;
			RefactorResult r = run_inline_variable(source, 8, 9, out);
			REQUIRE_MESSAGE(r.ok, r.error_message);
			CHECK_EQ(out,
					"extends Node\n"
					"\n"
					"func other() -> void:\n"
					"\tvar value = 2\n"
					"\tprint(value)\n"
					"\n"
					"func run() -> int:\n"
					"\treturn 1\n");
		}
	}

	TEST_CASE("Inline variable availability follows local variable caret") {
		const String source =
				"func run() -> int:\n"
				"\tvar value = 2\n"
				"\treturn value\n";
		RefactorContext ctx;
		ctx.path = "user://inline_variable_availability.gd";
		ctx.source = source;
		Vector<RefactorAvailability> available = GDScriptRefactoring::get_available_refactors(ctx, caret(1, 6));
		REQUIRE_EQ(available.size(), 5);
		if (available.size() >= 5) {
			CHECK_EQ(available[4].kind, RefactorKind::INLINE_VARIABLE);
			CHECK(available[4].enabled);
			CHECK(available[4].disabled_reason.is_empty());
		}
	}

	TEST_CASE("Inline variable rejects unsafe or unsupported targets") {
		SUBCASE("multi-use side-effecting initializer") {
			const String source =
					"func get_value() -> int:\n"
					"\treturn 1\n"
					"func run() -> int:\n"
					"\tvar value = get_value()\n"
					"\tprint(value)\n"
					"\treturn value\n";
			String out;
			RefactorResult r = run_inline_variable(source, 3, 6, out);
			CHECK_FALSE(r.ok);
			CHECK_MESSAGE(r.error_message.to_lower().contains("side-effect"), r.error_message);
		}
		SUBCASE("reassigned local") {
			const String source =
					"func run() -> int:\n"
					"\tvar value = 1\n"
					"\tvalue = 3\n"
					"\treturn value\n";
			String out;
			RefactorResult r = run_inline_variable(source, 1, 6, out);
			CHECK_FALSE(r.ok);
			CHECK_FALSE(r.error_message.is_empty());
		}
		SUBCASE("parameter") {
			const String source =
					"func run(value: int) -> int:\n"
					"\treturn value\n";
			String out;
			RefactorResult r = run_inline_variable(source, 0, 10, out);
			CHECK_FALSE(r.ok);
			CHECK_FALSE(r.error_message.is_empty());
		}
		SUBCASE("constant") {
			const String source =
					"func run() -> int:\n"
					"\tconst VALUE = 1\n"
					"\treturn VALUE\n";
			String out;
			RefactorResult r = run_inline_variable(source, 1, 8, out);
			CHECK_FALSE(r.ok);
			CHECK_FALSE(r.error_message.is_empty());
		}
		SUBCASE("member variable") {
			const String source =
					"var value = 1\n"
					"func run() -> int:\n"
					"\treturn value\n";
			String out;
			RefactorResult r = run_inline_variable(source, 0, 5, out);
			CHECK_FALSE(r.ok);
			CHECK_FALSE(r.error_message.is_empty());
		}
		SUBCASE("lambda capture") {
			const String source =
					"func run() -> Callable:\n"
					"\tvar value = 1\n"
					"\tvar callback = func():\n"
					"\t\treturn value\n"
					"\treturn callback\n";
			String out;
			RefactorResult r = run_inline_variable(source, 1, 6, out);
			CHECK_FALSE(r.ok);
			CHECK_MESSAGE(r.error_message.to_lower().contains("lambda"), r.error_message);
		}
		SUBCASE("no read usages") {
			const String source =
					"func run() -> void:\n"
					"\tvar value = 2\n"
					"\tpass\n";
			String out;
			RefactorResult r = run_inline_variable(source, 1, 6, out);
			CHECK_FALSE(r.ok);
			CHECK_MESSAGE(r.error_message.to_lower().contains("read"), r.error_message);
		}
		SUBCASE("multiline initializer") {
			const String source =
					"func run():\n"
					"\tvar value = [\n"
					"\t\t1,\n"
					"\t]\n"
					"\treturn value[0]\n";
			String out;
			RefactorResult r = run_inline_variable(source, 1, 6, out);
			CHECK_FALSE(r.ok);
			CHECK_MESSAGE(r.error_message.to_lower().contains("single-line"), r.error_message);
		}
		SUBCASE("trailing comment on declaration") {
			const String source =
					"func run() -> int:\n"
					"\tvar value = 2 # keep this\n"
					"\treturn value\n";
			String out;
			RefactorResult r = run_inline_variable(source, 1, 6, out);
			CHECK_FALSE(r.ok);
			CHECK_MESSAGE(r.error_message.to_lower().contains("trailing"), r.error_message);
		}
		SUBCASE("same-line declaration and use") {
			const String source =
					"func run() -> int:\n"
					"\tvar value = 2; return value\n";
			String out;
			RefactorResult r = run_inline_variable(source, 1, 6, out);
			CHECK_FALSE(r.ok);
			CHECK_MESSAGE(r.error_message.to_lower().contains("trailing"), r.error_message);
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
		SUBCASE("local variable ignores string-based dynamic member references") {
			String out;
			RefactorResult r = run_rename(
					"res://refactor/rename_local_dynamic_reference.gd", 3, 5, "local_total", out); // caret on `local_value`
			REQUIRE(r.ok);
			CHECK(r.warning.is_empty());
			CHECK(r.unresolved_references.is_empty());
			CHECK(out.contains("var local_total := 1"));
			CHECK(out.contains("get(\"local_value\")"));
			CHECK(out.contains("return local_total"));
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
			REQUIRE_EQ(available.size(), 5);
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
			REQUIRE_EQ(available.size(), 5);
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
		SUBCASE("member rename reports grouped cross-file edits") {
			GDScriptTests::assert_no_errors_in("res://refactor/rename_cross_file_user.gd");

			String out;
			RefactorResult r = run_rename(
					"res://refactor/rename_cross_file_target.gd", 2, 5, "renamed_count", out); // caret on `shared_count`
			REQUIRE(r.ok);
			CHECK_FALSE(r.warning.is_empty());
			CHECK(r.warning.to_lower().contains("other files"));
			REQUIRE_EQ(r.file_edits.size(), 2);

			const RefactorFileEdit *target_edits = find_file_edit(r, "res://refactor/rename_cross_file_target.gd");
			REQUIRE(target_edits);
			CHECK_EQ(target_edits->edits.size(), 3);

			const RefactorFileEdit *user_edits = find_file_edit(r, "res://refactor/rename_cross_file_user.gd");
			REQUIRE(user_edits);
			CHECK_EQ(user_edits->edits.size(), 2);

			String target_out;
			REQUIRE(GDScriptRefactorEdits::apply(
					FileAccess::get_file_as_string(target_edits->path), target_edits->edits, target_out));
			CHECK(target_out.contains("var renamed_count := 0"));
			CHECK(target_out.contains("renamed_count += 1"));
			CHECK(target_out.contains("return renamed_count"));
			CHECK_FALSE(target_out.contains("shared_count"));

			String user_out;
			REQUIRE(GDScriptRefactorEdits::apply(FileAccess::get_file_as_string(user_edits->path), user_edits->edits, user_out));
			CHECK(user_out.contains("target.renamed_count += 1"));
			CHECK(user_out.contains("return target.renamed_count"));
			CHECK_FALSE(user_out.contains("shared_count"));
		}
		SUBCASE("string-based dynamic references are reported") {
			String out;
			RefactorResult r = run_rename(
					"res://refactor/rename_dynamic_reference.gd", 2, 5, "renamed_value", out); // caret on `dynamic_value`
			REQUIRE(r.ok);
			CHECK_FALSE(r.warning.is_empty());
			CHECK(r.warning.to_lower().contains("dynamic"));
			REQUIRE_EQ(r.unresolved_references.size(), 1);
			CHECK_EQ(r.unresolved_references[0].path, "res://refactor/rename_dynamic_reference.gd");
			CHECK_EQ(r.unresolved_references[0].line, 5);
			CHECK(out.contains("\treturn get(\"dynamic_value\")"));
			CHECK_FALSE(out.contains("\treturn get(\"renamed_value\")"));
			CHECK(out.contains("\twidget(\"dynamic_value\")"));
			CHECK_FALSE(out.contains("\twidget(\"renamed_value\")"));
			CHECK(out.contains("\toffset(\"dynamic_value\")"));
			CHECK_FALSE(out.contains("\toffset(\"renamed_value\")"));
			CHECK(out.contains("\trecall(\"dynamic_value\")"));
			CHECK_FALSE(out.contains("\trecall(\"renamed_value\")"));
			CHECK(out.contains("\tvar label := \"dynamic_value\"; call(\"unrelated_method\")"));
			CHECK_FALSE(out.contains("\tvar label := \"renamed_value\"; call(\"unrelated_method\")"));
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
