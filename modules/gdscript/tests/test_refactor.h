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

#include "core/config/project_settings.h"
#include "core/io/dir_access.h"
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

inline RefactorResult run_extract_method_named(
		const String &p_source,
		const RefactorLocation &p_location,
		const String &p_new_name,
		String &r_out) {
	RefactorContext ctx;
	ctx.path = "user://extract_method_refactor.gd";
	ctx.source = p_source;
	RefactorParams params;
	params.new_name = p_new_name;
	RefactorResult r = GDScriptRefactoring::prepare(ctx, p_location, RefactorKind::EXTRACT_METHOD, params);
	if (r.ok) {
		GDScriptRefactorEdits::apply(ctx.source, r.edits, r_out);
	}
	return r;
}

inline RefactorResult run_extract_method(const String &p_source, const RefactorLocation &p_location, String &r_out) {
	return run_extract_method_named(p_source, p_location, String(), r_out);
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

inline const RefactorAvailability *find_availability(const Vector<RefactorAvailability> &p_available, RefactorKind p_kind) {
	for (const RefactorAvailability &a : p_available) {
		if (a.kind == p_kind) {
			return &a;
		}
	}
	return nullptr;
}

inline bool implement_abstract_enabled(const String &p_source, int p_line, int p_column) {
	RefactorContext ctx;
	ctx.path = "user://implement_abstract_refactor.gd";
	ctx.source = p_source;
	const Vector<RefactorAvailability> available = GDScriptRefactoring::get_available_refactors(ctx, caret(p_line, p_column));
	const RefactorAvailability *entry = find_availability(available, RefactorKind::IMPLEMENT_ABSTRACT_METHODS);
	return entry != nullptr && entry->enabled;
}

inline String implement_abstract_reason(const String &p_source, int p_line, int p_column) {
	RefactorContext ctx;
	ctx.path = "user://implement_abstract_refactor.gd";
	ctx.source = p_source;
	const Vector<RefactorAvailability> available = GDScriptRefactoring::get_available_refactors(ctx, caret(p_line, p_column));
	const RefactorAvailability *entry = find_availability(available, RefactorKind::IMPLEMENT_ABSTRACT_METHODS);
	return entry != nullptr ? entry->disabled_reason : String();
}

inline RefactorResult run_implement_abstract(const String &p_source, int p_line, int p_column, String &r_out) {
	RefactorContext ctx;
	ctx.path = "user://implement_abstract_refactor.gd";
	ctx.source = p_source;
	RefactorParams params;
	RefactorResult r = GDScriptRefactoring::prepare(ctx, caret(p_line, p_column), RefactorKind::IMPLEMENT_ABSTRACT_METHODS, params);
	if (r.ok) {
		GDScriptRefactorEdits::apply(ctx.source, r.edits, r_out);
	}
	return r;
}

#ifndef GDSCRIPT_NO_LSP
struct TemporaryScriptFile {
	String path;

	TemporaryScriptFile(const String &p_path, const String &p_source) {
		path = p_path;
		Ref<FileAccess> file = FileAccess::open(path, FileAccess::WRITE);
		REQUIRE_MESSAGE(file.is_valid(), vformat("Cannot write '%s'", path));
		file->store_string(p_source);
	}

	~TemporaryScriptFile() {
		DirAccess::remove_absolute(ProjectSettings::get_singleton()->globalize_path(path));
	}
};
#endif // GDSCRIPT_NO_LSP

TEST_SUITE("[Modules][GDScript][Refactor]") {
	TEST_CASE("Rename is reported but disabled at a trivial location") {
		RefactorContext ctx = make_context("modules/gdscript/tests/scripts/refactor/empty.gd");
		Vector<RefactorAvailability> available = GDScriptRefactoring::get_available_refactors(ctx, caret(0, 0));
		CHECK_EQ(available.size(), 6);
		if (available.size() < 6) {
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

	TEST_CASE("Implement abstract methods is listed and disabled with no abstract base") {
		RefactorContext ctx = make_context("modules/gdscript/tests/scripts/refactor/empty.gd");
		Vector<RefactorAvailability> available = GDScriptRefactoring::get_available_refactors(ctx, caret(0, 0));
		const RefactorAvailability *entry = nullptr;
		for (const RefactorAvailability &a : available) {
			if (a.kind == RefactorKind::IMPLEMENT_ABSTRACT_METHODS) {
				entry = &a;
				break;
			}
		}
		REQUIRE(entry != nullptr);
		CHECK_EQ(entry->title, String("Implement Abstract Methods"));
		CHECK_FALSE(entry->enabled);
		CHECK_FALSE(entry->disabled_reason.is_empty());
	}

	TEST_CASE("Implement abstract: enabled when a derived class owes an abstract method") {
		const String source =
				"@abstract class Base:\n"
				"\t@abstract func area() -> float\n"
				"class Circle extends Base:\n"
				"\tvar radius := 1.0\n";
		CHECK(GDScriptTests::implement_abstract_enabled(source, 3, 1));
	}

	TEST_CASE("Implement abstract: disabled when the method is already overridden") {
		const String source =
				"@abstract class Base:\n"
				"\t@abstract func area() -> float\n"
				"class Circle extends Base:\n"
				"\tfunc area() -> float:\n"
				"\t\treturn 3.14\n";
		CHECK_FALSE(GDScriptTests::implement_abstract_enabled(source, 2, 1));
		CHECK_EQ(GDScriptTests::implement_abstract_reason(source, 2, 1), String("No unimplemented abstract methods."));
	}

	TEST_CASE("Implement abstract: disabled for an abstract derived class") {
		const String source =
				"@abstract class Base:\n"
				"\t@abstract func area() -> float\n"
				"@abstract class Shape extends Base:\n"
				"\tvar name := \"\"\n";
		CHECK_FALSE(GDScriptTests::implement_abstract_enabled(source, 3, 1));
		CHECK_EQ(GDScriptTests::implement_abstract_reason(source, 3, 1), String("Abstract classes don't need to implement abstract methods."));
	}

	TEST_CASE("Implement abstract: disabled when the script cannot be parsed") {
		const String source =
				"class Circle extends:\n" // Malformed extends -> parse error.
				"\tfunc\n";
		RefactorContext ctx;
		ctx.path = "user://implement_abstract_refactor.gd";
		ctx.source = source;
		const Vector<RefactorAvailability> available = GDScriptRefactoring::get_available_refactors(ctx, GDScriptTests::caret(0, 0));
		const RefactorAvailability *entry = GDScriptTests::find_availability(available, RefactorKind::IMPLEMENT_ABSTRACT_METHODS);
		REQUIRE(entry != nullptr);
		CHECK_FALSE(entry->enabled);
		CHECK_EQ(entry->disabled_reason, String("Cannot analyze this script."));
	}

	TEST_CASE("Implement abstract: intermediate concrete override satisfies the contract") {
		const String source =
				"@abstract class Base:\n"
				"\t@abstract func area() -> float\n"
				"\t@abstract func name() -> String\n"
				"@abstract class Mid extends Base:\n"
				"\tfunc area() -> float:\n"
				"\t\treturn 0.0\n"
				"class Leaf extends Mid:\n"
				"\tvar tag := 1\n";
		// Leaf still owes name() but not area().
		CHECK(GDScriptTests::implement_abstract_enabled(source, 7, 1));
	}

	TEST_CASE("Implement abstract: renders typed stub with push_error and default return") {
		const String source =
				"@abstract class Base:\n"
				"\t@abstract func area() -> float\n"
				"class Circle extends Base:\n"
				"\tvar radius := 1.0\n";
		String out;
		RefactorResult r = GDScriptTests::run_implement_abstract(source, 3, 1, out);
		REQUIRE(r.ok);
		CHECK(out.contains("func area() -> float:"));
		CHECK(out.contains("push_error(\"Not implemented: area\")"));
		CHECK(out.contains("return 0.0"));
	}

	TEST_CASE("Implement abstract: void method has no return") {
		const String source =
				"@abstract class Base:\n"
				"\t@abstract func tick() -> void\n"
				"class Clock extends Base:\n"
				"\tvar t := 0\n";
		String out;
		RefactorResult r = GDScriptTests::run_implement_abstract(source, 3, 1, out);
		REQUIRE(r.ok);
		CHECK(out.contains("func tick() -> void:"));
		CHECK(out.contains("push_error(\"Not implemented: tick\")"));
		CHECK_FALSE(out.contains("return"));
	}

	TEST_CASE("Implement abstract: object return type uses pass") {
		const String source =
				"@abstract class Base:\n"
				"\t@abstract func make() -> Node\n"
				"class Factory extends Base:\n"
				"\tvar count := 0\n";
		String out;
		RefactorResult r = GDScriptTests::run_implement_abstract(source, 3, 1, out);
		REQUIRE(r.ok);
		CHECK(out.contains("func make() -> Node:"));
		CHECK(out.contains("push_error(\"Not implemented: make\")"));
		CHECK(out.contains("pass"));
		CHECK_FALSE(out.contains("return"));
	}

	TEST_CASE("Implement abstract: preserves params and annotated types") {
		const String source =
				"@abstract class Base:\n"
				"\t@abstract func combine(a: int, b: int) -> int\n"
				"class Math extends Base:\n"
				"\tvar seed := 0\n";
		String out;
		RefactorResult r = GDScriptTests::run_implement_abstract(source, 3, 1, out);
		REQUIRE(r.ok);
		CHECK(out.contains("func combine(a: int, b: int) -> int:"));
		CHECK(out.contains("return 0"));
	}

	TEST_CASE("Implement abstract: generates all owed methods at once") {
		const String source =
				"@abstract class Base:\n"
				"\t@abstract func area() -> float\n"
				"\t@abstract func name() -> String\n"
				"class Circle extends Base:\n"
				"\tvar radius := 1.0\n";
		String out;
		RefactorResult r = GDScriptTests::run_implement_abstract(source, 4, 1, out);
		REQUIRE(r.ok);
		CHECK(out.contains("func area() -> float:"));
		CHECK(out.contains("func name() -> String:"));
		CHECK(out.contains("return \"\""));
	}

	TEST_CASE("Implement abstract: inner class stub is indented one level deeper") {
		const String source =
				"@abstract class Base:\n"
				"\t@abstract func area() -> float\n"
				"class Circle extends Base:\n"
				"\tvar radius := 1.0\n";
		String out;
		RefactorResult r = GDScriptTests::run_implement_abstract(source, 3, 1, out);
		REQUIRE(r.ok);
		CHECK(out.contains("\tfunc area() -> float:\n"));
		CHECK(out.contains("\t\tpush_error(\"Not implemented: area\")\n"));
		CHECK(out.contains("\t\treturn 0.0\n"));
	}

	TEST_CASE("Implement abstract: reproduces parameter default values") {
		const String source =
				"@abstract class Base:\n"
				"\t@abstract func scaled(factor: float = 1.0) -> float\n"
				"class Circle extends Base:\n"
				"\tvar radius := 1.0\n";
		String out;
		RefactorResult r = GDScriptTests::run_implement_abstract(source, 3, 1, out);
		REQUIRE(r.ok);
		CHECK(out.contains("func scaled(factor: float = 1.0) -> float:"));
	}

	TEST_CASE("Implement abstract: Array return defaults to empty array") {
		const String source =
				"@abstract class Base:\n"
				"\t@abstract func items() -> Array\n"
				"class Bag extends Base:\n"
				"\tvar count := 0\n";
		String out;
		RefactorResult r = GDScriptTests::run_implement_abstract(source, 3, 1, out);
		REQUIRE(r.ok);
		CHECK(out.contains("func items() -> Array:"));
		CHECK(out.contains("return []"));
	}

	TEST_CASE("Implement abstract: Dictionary return defaults to empty dictionary") {
		const String source =
				"@abstract class Base:\n"
				"\t@abstract func lookup() -> Dictionary\n"
				"class Store extends Base:\n"
				"\tvar count := 0\n";
		String out;
		RefactorResult r = GDScriptTests::run_implement_abstract(source, 3, 1, out);
		REQUIRE(r.ok);
		CHECK(out.contains("func lookup() -> Dictionary:"));
		CHECK(out.contains("return {}"));
	}

	TEST_CASE("Implement abstract: StringName return defaults to empty string name") {
		const String source =
				"@abstract class Base:\n"
				"\t@abstract func tag() -> StringName\n"
				"class Label extends Base:\n"
				"\tvar count := 0\n";
		String out;
		RefactorResult r = GDScriptTests::run_implement_abstract(source, 3, 1, out);
		REQUIRE(r.ok);
		CHECK(out.contains("func tag() -> StringName:"));
		CHECK(out.contains("return &\"\""));
	}

	TEST_CASE("Implement abstract: top-level class stub has no indentation") {
		const String source =
				"extends Base\n"
				"@abstract class Base:\n"
				"\t@abstract func area() -> float\n"
				"var radius := 1.0\n";
		String out;
		RefactorResult r = GDScriptTests::run_implement_abstract(source, 0, 0, out);
		REQUIRE(r.ok);
		CHECK(out.contains("\nfunc area() -> float:\n"));
		CHECK(out.contains("\n\tpush_error(\"Not implemented: area\")\n"));
		CHECK(out.contains("\n\treturn 0.0\n"));
	}

	TEST_CASE("Implement abstract: top-level class with no members has no indentation") {
		const String source =
				"extends Base\n"
				"@abstract class Base:\n"
				"\t@abstract func area() -> float\n";
		String out;
		RefactorResult r = GDScriptTests::run_implement_abstract(source, 0, 0, out);
		REQUIRE(r.ok);
		CHECK(out.contains("\nfunc area() -> float:\n"));
		CHECK(out.contains("\n\treturn 0.0\n"));
	}

	TEST_CASE("Implement abstract: preserves the async modifier") {
		const String source =
				"@abstract class Base:\n"
				"\t@abstract async func area() -> int\n"
				"class Circle extends Base:\n"
				"\tvar radius := 1.0\n";
		String out;
		RefactorResult r = GDScriptTests::run_implement_abstract(source, 3, 1, out);
		REQUIRE(r.ok);
		CHECK(out.contains("async func area() -> int:"));
	}

	TEST_CASE("Implement abstract: preserves the rest (vararg) parameter") {
		const String source =
				"@abstract class Base:\n"
				"\t@abstract func record(...args)\n"
				"class Circle extends Base:\n"
				"\tvar radius := 1.0\n";
		String out;
		RefactorResult r = GDScriptTests::run_implement_abstract(source, 3, 1, out);
		REQUIRE(r.ok);
		// An untyped rest parameter is inferred as Array, which the stub renders faithfully.
		CHECK(out.contains("func record(...args: Array):"));
	}

	TEST_CASE("Implement abstract: rest parameter follows fixed parameters") {
		const String source =
				"@abstract class Base:\n"
				"\t@abstract func record(prefix: String, ...args)\n"
				"class Circle extends Base:\n"
				"\tvar radius := 1.0\n";
		String out;
		RefactorResult r = GDScriptTests::run_implement_abstract(source, 3, 1, out);
		REQUIRE(r.ok);
		CHECK(out.contains("func record(prefix: String, ...args: Array):"));
	}

	TEST_CASE("Implement abstract: reproduces a multi-line default value") {
		const String source =
				"@abstract class Base:\n"
				"\t@abstract func build(options := {\n"
				"\t\t\t\"a\": 1,\n"
				"\t\t\t\"b\": 2,\n"
				"\t\t}) -> int\n"
				"class Maker extends Base:\n"
				"\tvar seed := 0\n";
		String out;
		RefactorResult r = GDScriptTests::run_implement_abstract(source, 6, 1, out);
		REQUIRE(r.ok);
		CHECK(out.contains("\"a\": 1,"));
		CHECK(out.contains("\"b\": 2,"));
		CHECK(out.contains("options"));
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

		SUBCASE("finds edit touched by caret or selection") {
			Vector<RefactorTextEdit> edits;
			edits.push_back(edit(1, 4, 1, 9, "first"));
			edits.push_back(edit(3, 8, 3, 13, "second"));

			RefactorLocation caret_location;
			caret_location.start_line = 3;
			caret_location.end_line = 3;
			caret_location.start_column = 10;
			caret_location.end_column = 10;
			CHECK_EQ(GDScriptRefactorEdits::find_edit_at_location(edits, caret_location), 1);

			RefactorLocation left_anchored_selection;
			left_anchored_selection.start_line = 3;
			left_anchored_selection.end_line = 3;
			left_anchored_selection.start_column = 7;
			left_anchored_selection.end_column = 13;
			CHECK_EQ(GDScriptRefactorEdits::find_edit_at_location(edits, left_anchored_selection), 1);

			RefactorLocation outside_location;
			outside_location.start_line = 2;
			outside_location.end_line = 2;
			outside_location.start_column = 1;
			outside_location.end_column = 1;
			CHECK_EQ(GDScriptRefactorEdits::find_edit_at_location(edits, outside_location), -1);
		}

		SUBCASE("finds adjacent edits using half-open ranges") {
			Vector<RefactorTextEdit> edits;
			edits.push_back(edit(0, 0, 0, 3, "first"));
			edits.push_back(edit(0, 3, 0, 6, "second"));

			RefactorLocation boundary_location;
			boundary_location.start_line = 0;
			boundary_location.end_line = 0;
			boundary_location.start_column = 3;
			boundary_location.end_column = 3;
			CHECK_EQ(GDScriptRefactorEdits::find_edit_at_location(edits, boundary_location), 1);

			RefactorLocation after_last_location;
			after_last_location.start_line = 0;
			after_last_location.end_line = 0;
			after_last_location.start_column = 6;
			after_last_location.end_column = 6;
			CHECK_EQ(GDScriptRefactorEdits::find_edit_at_location(edits, after_last_location), 1);
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
		SUBCASE("identifier at caret column") {
			const String line = String::utf8("\tvar número := número + 1");
			CHECK_EQ(GDScriptRefactorNames::identifier_at_column("name := 1", 0), "name");
			CHECK_EQ(GDScriptRefactorNames::identifier_at_column("name := 1", 1), "name");
			CHECK_EQ(GDScriptRefactorNames::identifier_at_column(line, 8), String::utf8("número"));
			CHECK_EQ(GDScriptRefactorNames::identifier_at_column(line, 11), String::utf8("número"));
			CHECK_EQ(GDScriptRefactorNames::identifier_at_column(line, 21), String::utf8("número"));
			CHECK(GDScriptRefactorNames::identifier_at_column(line, 12).is_empty());
			CHECK(GDScriptRefactorNames::identifier_at_column("", 0).is_empty());
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

	TEST_CASE("Add type annotation return type skips trailing comment colon") {
		SUBCASE("caret-driven path") {
			const String source = "func make_score(): # returns: score\n\treturn 1\n";
			String out;
			RefactorResult r = run_type_annotation(source, 0, 5, out);
			REQUIRE(r.ok);
			CHECK_EQ(out, "func make_score() -> int: # returns: score\n\treturn 1\n");
		}
		SUBCASE("typed parameter with trailing comment colon") {
			const String source = "func scale(amount: float): # ratio: keep\n\treturn amount\n";
			String out;
			RefactorResult r = run_type_annotation(source, 0, 5, out);
			REQUIRE(r.ok);
			CHECK_EQ(out, "func scale(amount: float) -> float: # ratio: keep\n\treturn amount\n");
		}
	}

	TEST_CASE("find_candidates return-type edit skips trailing comment colon") {
		RefactorContext ctx;
		ctx.path = "user://type_annotation_comment.gd";
		ctx.source = "func make_score(): # returns: score\n\treturn 1\n";
		RefactorCandidatesResult result = GDScriptRefactoring::find_candidates(ctx, RefactorKind::ADD_TYPE_ANNOTATION);
		REQUIRE(result.ok);

		const RefactorCandidate *return_candidate = nullptr;
		for (const RefactorCandidate &candidate : result.candidates) {
			if (candidate.enabled && candidate.line == 0) {
				return_candidate = &candidate;
				break;
			}
		}
		REQUIRE(return_candidate != nullptr);

		String out;
		REQUIRE(GDScriptRefactorEdits::apply(ctx.source, return_candidate->edits, out));
		CHECK_EQ(out, "func make_score() -> int: # returns: score\n\treturn 1\n");
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

	TEST_CASE("find_candidates collects all type annotations without a caret") {
		RefactorContext ctx = make_context("modules/gdscript/tests/scripts/refactor/type_annotation_candidates.gd");
		RefactorCandidatesResult result = GDScriptRefactoring::find_candidates(ctx, RefactorKind::ADD_TYPE_ANNOTATION);
		REQUIRE(result.ok);

		// The fixture's annotatable declarations are: member_score (line 0, enabled),
		// member_typed (line 1, disabled: already typed), the amount parameter (line 3,
		// enabled), make_total's inferred return type (line 3, disabled: the return-less
		// body yields no concrete annotation), local_name (line 4, enabled), and
		// nested_flag (line 6, enabled).
		int enabled_count = 0;
		int disabled_count = 0;
		bool saw_member_score = false;
		bool saw_nested_flag = false;
		bool saw_disabled_at_line_1 = false;
		for (const RefactorCandidate &candidate : result.candidates) {
			CHECK_EQ(candidate.kind, RefactorKind::ADD_TYPE_ANNOTATION);
			if (candidate.enabled) {
				enabled_count++;
				REQUIRE_FALSE(candidate.edits.is_empty());
				if (candidate.line == 0) {
					saw_member_score = true;
					CHECK_EQ(candidate.edits[0].new_text, ": int = ");
				}
				if (candidate.line == 6) {
					saw_nested_flag = true;
				}
			} else {
				disabled_count++;
				CHECK_FALSE(candidate.disabled_reason.is_empty());
				if (candidate.line == 1) {
					saw_disabled_at_line_1 = true; // member_typed is already typed.
				}
			}
		}
		CHECK(saw_member_score);
		CHECK(saw_nested_flag);
		CHECK(saw_disabled_at_line_1);
		CHECK_EQ(enabled_count, 4);
		CHECK_EQ(disabled_count, 2);
		CHECK_EQ(result.candidates.size(), 6);
	}

	TEST_CASE("Add type annotation handles multi-line function signatures") {
		SUBCASE("wrapped parameter list") {
			const String source =
					"func make_score(\n"
					"\t\tbase: int\n"
					"):\n"
					"\treturn base\n";
			String out;
			RefactorResult r = run_type_annotation(source, 0, 5, out);
			REQUIRE(r.ok);
			CHECK_EQ(out,
					"func make_score(\n"
					"\t\tbase: int\n"
					") -> int:\n"
					"\treturn base\n");
		}
		SUBCASE("parameter type colon is not mistaken for the body colon") {
			const String source =
					"func combine(\n"
					"\t\tfirst: int,\n"
					"\t\tsecond: int):\n"
					"\treturn first + second\n";
			String out;
			RefactorResult r = run_type_annotation(source, 0, 5, out);
			REQUIRE(r.ok);
			CHECK_EQ(out,
					"func combine(\n"
					"\t\tfirst: int,\n"
					"\t\tsecond: int) -> int:\n"
					"\treturn first + second\n");
		}
		SUBCASE("colon inside a multi-line string default is not mistaken for the body colon") {
			const String source =
					"func describe(text = \"\"\"\n"
					")  :\n"
					"\"\"\"):\n"
					"\treturn 1\n";
			String out;
			RefactorResult r = run_type_annotation(source, 0, 5, out);
			REQUIRE(r.ok);
			CHECK_EQ(out,
					"func describe(text = \"\"\"\n"
					")  :\n"
					"\"\"\") -> int:\n"
					"\treturn 1\n");
		}
		SUBCASE("caret on the closing colon line selects the wrapped return type") {
			const String source =
					"func make_score(\n"
					"\t\tbase: int\n"
					"):\n"
					"\treturn base\n";
			String out;
			RefactorResult r = run_type_annotation(source, 2, 0, out);
			REQUIRE(r.ok);
			CHECK_EQ(out,
					"func make_score(\n"
					"\t\tbase: int\n"
					") -> int:\n"
					"\treturn base\n");
		}
	}

	TEST_CASE("Add type annotation preserves the async modifier") {
		SUBCASE("single-line async function") {
			const String source = "async func fetch():\n\treturn 1\n";
			String out;
			RefactorResult r = run_type_annotation(source, 0, 12, out);
			REQUIRE(r.ok);
			CHECK_EQ(out, "async func fetch() -> int:\n\treturn 1\n");
		}
		SUBCASE("multi-line async function") {
			const String source =
					"async func fetch(\n"
					"\t\tbase: int\n"
					"):\n"
					"\treturn base\n";
			String out;
			RefactorResult r = run_type_annotation(source, 0, 12, out);
			REQUIRE(r.ok);
			CHECK_EQ(out,
					"async func fetch(\n"
					"\t\tbase: int\n"
					") -> int:\n"
					"\treturn base\n");
		}
	}

	TEST_CASE("find_candidates reports a wrapped declaration as a counted skip") {
		RefactorContext ctx;
		ctx.path = "user://type_annotation_wrapped_declaration.gd";
		ctx.source =
				"var total \\\n"
				"\t= 1 + 2\n";
		RefactorCandidatesResult result = GDScriptRefactoring::find_candidates(ctx, RefactorKind::ADD_TYPE_ANNOTATION);
		REQUIRE(result.ok);
		REQUIRE_EQ(result.candidates.size(), 1);
		CHECK_FALSE(result.candidates[0].enabled);
		CHECK(result.candidates[0].disabled_reason.contains("spans multiple lines"));
	}

	TEST_CASE("find_candidates does not scan a bodyless abstract signature into a following member") {
		RefactorContext ctx;
		ctx.path = "user://type_annotation_abstract.gd";
		ctx.source =
				"@abstract class Base:\n"
				"\t@abstract func area() -> float\n"
				"\tvar radius := 1.0\n";
		RefactorCandidatesResult result = GDScriptRefactoring::find_candidates(ctx, RefactorKind::ADD_TYPE_ANNOTATION);
		REQUIRE(result.ok);

		bool saw_radius = false;
		for (const RefactorCandidate &candidate : result.candidates) {
			// The bodyless abstract signature on line 1 has no body colon; the scan
			// must not borrow the `:=` colon from the following member declaration.
			CHECK_NE(candidate.line, 1);
			if (candidate.line == 2) {
				saw_radius = true;
				CHECK(candidate.enabled);
			}
		}
		CHECK(saw_radius);
	}

	TEST_CASE("find_candidates rejects unsupported refactor kinds") {
		RefactorContext ctx = make_context("modules/gdscript/tests/scripts/refactor/type_annotation_candidates.gd");
		RefactorCandidatesResult result = GDScriptRefactoring::find_candidates(ctx, RefactorKind::RENAME);
		CHECK_FALSE(result.ok);
		CHECK_EQ(result.error_message, "Headless candidate collection is not implemented for this refactor.");
	}

	TEST_CASE("find_candidates edit matches the caret-driven path") {
		RefactorContext ctx = make_context("modules/gdscript/tests/scripts/refactor/type_annotation_candidates.gd");
		RefactorCandidatesResult result = GDScriptRefactoring::find_candidates(ctx, RefactorKind::ADD_TYPE_ANNOTATION);
		REQUIRE(result.ok);

		const RefactorCandidate *member_score = nullptr;
		for (const RefactorCandidate &candidate : result.candidates) {
			if (candidate.enabled && candidate.line == 0) {
				member_score = &candidate;
				break;
			}
		}
		REQUIRE(member_score != nullptr);

		RefactorParams params;
		RefactorResult caret_result = GDScriptRefactoring::prepare(ctx, caret(0, 5), RefactorKind::ADD_TYPE_ANNOTATION, params);
		REQUIRE(caret_result.ok);
		REQUIRE_EQ(caret_result.edits.size(), 1);
		CHECK_EQ(member_score->edits[0].new_text, caret_result.edits[0].new_text);
		CHECK_EQ(member_score->edits[0].start_line, caret_result.edits[0].start_line);
		CHECK_EQ(member_score->edits[0].start_column, caret_result.edits[0].start_column);
		CHECK_EQ(member_score->edits[0].end_line, caret_result.edits[0].end_line);
		CHECK_EQ(member_score->edits[0].end_column, caret_result.edits[0].end_column);

		// The headless disabled_reason for an already-typed declaration must match the
		// caret path's error_message for the same declaration.
		const RefactorCandidate *member_typed = nullptr;
		for (const RefactorCandidate &candidate : result.candidates) {
			if (!candidate.enabled && candidate.line == 1) {
				member_typed = &candidate;
				break;
			}
		}
		REQUIRE(member_typed != nullptr);
		RefactorResult typed_caret = GDScriptRefactoring::prepare(ctx, caret(1, 5), RefactorKind::ADD_TYPE_ANNOTATION, params);
		CHECK_FALSE(typed_caret.ok);
		CHECK_EQ(typed_caret.error_message, member_typed->disabled_reason);
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
		REQUIRE_EQ(available.size(), 6);
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
		SUBCASE("export group labels do not invalidate suggested prompt names") {
			const String source =
					"@export_group(\"_extracted_method\")\n"
					"@export var value := 0\n"
					"\n"
					"func run() -> void:\n"
					"\tprint(\"ready\")\n"
					"\tprint(\"done\")\n";
			String out;
			RefactorResult r = run_extract_method(source, selection(4, 0, 5, 0), out);
			REQUIRE(r.ok);
			CHECK_EQ(r.suggested_name, "_extracted_method");
			String reason;
			CHECK(GDScriptRefactoring::validate_extract_method_name(r.extract_method_member_names, r.suggested_name, reason));
			CHECK(reason.is_empty());
		}
		SUBCASE("uses requested method name") {
			const String source =
					"func run() -> void:\n"
					"\tprint(\"ready\")\n"
					"\tprint(\"done\")\n";
			String out;
			RefactorResult r = run_extract_method_named(source, selection(1, 0, 2, 0), "_print_ready", out);
			REQUIRE(r.ok);
			CHECK_EQ(r.suggested_name, "_print_ready");
			CHECK_EQ(out,
					"func run() -> void:\n"
					"\t_print_ready()\n"
					"\tprint(\"done\")\n"
					"\n"
					"func _print_ready() -> void:\n"
					"\tprint(\"ready\")\n");
		}
		SUBCASE("accepts full statement text selection without newline selection") {
			const String source =
					"func run(player_name: String, score: int) -> void:\n"
					"\tvar message := \"Player %s scored %d points\" % [player_name, score]\n"
					"\tprint(message)\n"
					"\tprint(\"Score length: \", str(score).length())\n"
					"\tprint(\"Done\")\n";
			String out;
			const Vector<String> lines = source.split("\n");
			RefactorResult r = run_extract_method(source, selection(1, 1, 3, lines[3].length()), out);
			REQUIRE(r.ok);
			CHECK_EQ(out,
					"func run(player_name: String, score: int) -> void:\n"
					"\t_extracted_method(player_name, score)\n"
					"\tprint(\"Done\")\n"
					"\n"
					"func _extracted_method(player_name: String, score: int) -> void:\n"
					"\tvar message := \"Player %s scored %d points\" % [player_name, score]\n"
					"\tprint(message)\n"
					"\tprint(\"Score length: \", str(score).length())\n");
		}
		SUBCASE("accepts single-line full statement text selection") {
			const String source =
					"func run() -> void:\n"
					"\tprint(\"ready\")\n"
					"\tprint(\"done\")\n";
			String out;
			const Vector<String> lines = source.split("\n");
			RefactorResult r = run_extract_method(source, selection(1, 1, 1, lines[1].length()), out);
			REQUIRE(r.ok);
			CHECK_EQ(out,
					"func run() -> void:\n"
					"\t_extracted_method()\n"
					"\tprint(\"done\")\n"
					"\n"
					"func _extracted_method() -> void:\n"
					"\tprint(\"ready\")\n");
		}
		SUBCASE("allows requested method name matching export group label") {
			const String source =
					"@export_group(\"helper\")\n"
					"@export var value := 0\n"
					"\n"
					"func run() -> void:\n"
					"\tprint(\"ready\")\n"
					"\tprint(\"done\")\n";
			String out;
			RefactorResult r = run_extract_method_named(source, selection(4, 0, 5, 0), "helper", out);
			REQUIRE(r.ok);
			CHECK_EQ(r.suggested_name, "helper");
			CHECK(out.contains("\thelper()\n"));
			CHECK(out.contains("func helper() -> void:\n"));
		}
		SUBCASE("rejects invalid requested method name") {
			const String source =
					"func run() -> void:\n"
					"\tprint(\"ready\")\n"
					"\tprint(\"done\")\n";
			String out;
			RefactorResult r = run_extract_method_named(source, selection(1, 0, 2, 0), "1bad", out);
			CHECK_FALSE(r.ok);
			CHECK(r.error_message.contains("valid identifier"));
			CHECK(out.is_empty());
		}
		SUBCASE("rejects reserved requested method name") {
			const String source =
					"func run() -> void:\n"
					"\tprint(\"ready\")\n"
					"\tprint(\"done\")\n";
			String out;
			RefactorResult r = run_extract_method_named(source, selection(1, 0, 2, 0), "class", out);
			CHECK_FALSE(r.ok);
			CHECK(r.error_message.contains("reserved keyword"));
			CHECK(out.is_empty());
		}
		SUBCASE("rejects requested method name collision") {
			const String source =
					"func run() -> void:\n"
					"\tprint(\"ready\")\n"
					"\tprint(\"done\")\n"
					"\n"
					"func existing() -> void:\n"
					"\tpass\n";
			String out;
			RefactorResult r = run_extract_method_named(source, selection(1, 0, 2, 0), "existing", out);
			CHECK_FALSE(r.ok);
			CHECK(r.error_message.contains("already exists"));
			CHECK(out.is_empty());
		}
		SUBCASE("rejects requested method name collision inside target inner class") {
			const String source =
					"class Inner:\n"
					"\tfunc run() -> void:\n"
					"\t\tprint(\"ready\")\n"
					"\t\tprint(\"done\")\n"
					"\n"
					"\tfunc existing() -> void:\n"
					"\t\tpass\n"
					"\n"
					"func existing() -> void:\n"
					"\tpass\n";
			String out;
			RefactorResult r = run_extract_method_named(source, selection(2, 0, 3, 0), "existing", out);
			CHECK_FALSE(r.ok);
			CHECK(r.error_message.contains("already exists"));
			CHECK(out.is_empty());
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
		REQUIRE_EQ(available.size(), 6);
		CHECK_EQ(available[2].kind, RefactorKind::EXTRACT_METHOD);
		CHECK(available[2].enabled);
		CHECK(available[2].disabled_reason.is_empty());

		const Vector<String> lines = source.split("\n");
		Vector<RefactorAvailability> text_selection_available = GDScriptRefactoring::get_available_refactors(
				ctx,
				selection(1, 1, 1, lines[1].length()));
		REQUIRE_EQ(text_selection_available.size(), 6);
		CHECK_EQ(text_selection_available[2].kind, RefactorKind::EXTRACT_METHOD);
		CHECK(text_selection_available[2].enabled);
		CHECK(text_selection_available[2].disabled_reason.is_empty());
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
		SUBCASE("rejects text selection starting inside a statement") {
			const String source =
					"func run() -> void:\n"
					"\tprint(\"ready\")\n"
					"\tprint(\"done\")\n";
			String out;
			const Vector<String> lines = source.split("\n");
			RefactorResult r = run_extract_method(source, selection(1, 2, 1, lines[1].length()), out);
			CHECK_FALSE(r.ok);
			CHECK_FALSE(r.error_message.is_empty());
			CHECK(out.is_empty());
		}
		SUBCASE("rejects text selection ending before statement text ends") {
			const String source =
					"func run() -> void:\n"
					"\tprint(\"ready\")\n"
					"\tprint(\"done\")\n";
			String out;
			RefactorResult r = run_extract_method(source, selection(1, 1, 1, 6), out);
			CHECK_FALSE(r.ok);
			CHECK_FALSE(r.error_message.is_empty());
			CHECK(out.is_empty());
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
		REQUIRE_EQ(available.size(), 6);
		if (available.size() >= 6) {
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
		SUBCASE("local variable exposes current-file occurrence ranges for inline rename") {
			String out;
			RefactorResult r = run_rename("res://refactor/rename_local.gd", 3, 5, "total", out); // caret on `total`
			REQUIRE(r.ok);
			REQUIRE_EQ(r.rename_occurrences.size(), 3);

			CHECK_EQ(r.rename_occurrences[0].start_line, 3);
			CHECK_EQ(r.rename_occurrences[0].start_column, 5);
			CHECK_EQ(r.rename_occurrences[0].end_line, 3);
			CHECK_EQ(r.rename_occurrences[0].end_column, 10);
			CHECK_EQ(r.rename_occurrences[0].expected_text, "total");

			CHECK_EQ(r.rename_occurrences[1].start_line, 4);
			CHECK_EQ(r.rename_occurrences[1].start_column, 1);
			CHECK_EQ(r.rename_occurrences[1].end_line, 4);
			CHECK_EQ(r.rename_occurrences[1].end_column, 6);

			CHECK_EQ(r.rename_occurrences[2].start_line, 5);
			CHECK_EQ(r.rename_occurrences[2].start_column, 8);
			CHECK_EQ(r.rename_occurrences[2].end_line, 5);
			CHECK_EQ(r.rename_occurrences[2].end_column, 13);
		}
		SUBCASE("inline rename occurrence ranges ignore strings and comments") {
			String out;
			RefactorResult r = run_rename("res://refactor/rename_strings_comments.gd", 3, 5, "total", out); // caret on `total`
			REQUIRE(r.ok);
			REQUIRE_EQ(r.rename_occurrences.size(), 2);
			CHECK_EQ(r.rename_occurrences[0].start_line, 3);
			CHECK_EQ(r.rename_occurrences[1].start_line, 6);
			for (const RefactorTextEdit &occurrence : r.rename_occurrences) {
				CHECK_EQ(occurrence.expected_text, "total");
				CHECK(occurrence.start_line != 4); // Comment line.
				CHECK(occurrence.start_line != 5); // String line.
			}
		}
		SUBCASE("inline rename exposes same-line occurrence ranges") {
			String out;
			RefactorResult r = run_rename("res://refactor/rename_same_line.gd", 3, 5, "sum", out); // caret on `total`
			REQUIRE(r.ok);
			REQUIRE_EQ(r.rename_occurrences.size(), 5);

			CHECK_EQ(r.rename_occurrences[0].start_line, 3);
			CHECK_EQ(r.rename_occurrences[0].start_column, 5);
			CHECK_EQ(r.rename_occurrences[0].end_column, 10);

			CHECK_EQ(r.rename_occurrences[1].start_line, 4);
			CHECK_EQ(r.rename_occurrences[1].start_column, 1);
			CHECK_EQ(r.rename_occurrences[1].end_column, 6);

			CHECK_EQ(r.rename_occurrences[2].start_line, 4);
			CHECK_EQ(r.rename_occurrences[2].start_column, 9);
			CHECK_EQ(r.rename_occurrences[2].end_column, 14);

			CHECK_EQ(r.rename_occurrences[3].start_line, 4);
			CHECK_EQ(r.rename_occurrences[3].start_column, 17);
			CHECK_EQ(r.rename_occurrences[3].end_column, 22);

			CHECK_EQ(r.rename_occurrences[4].start_line, 5);
			CHECK_EQ(r.rename_occurrences[4].start_column, 8);
			CHECK_EQ(r.rename_occurrences[4].end_column, 13);
		}
		SUBCASE("availability reports rename enabled on a symbol") {
			GDScriptTests::assert_no_errors_in("res://refactor/rename_local.gd");
			RefactorContext ctx;
			ctx.path = "res://refactor/rename_local.gd";
			ctx.source = FileAccess::get_file_as_string(ctx.path);
			Vector<RefactorAvailability> available = GDScriptRefactoring::get_available_refactors(ctx, caret(3, 5));
			REQUIRE_EQ(available.size(), 6);
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
			REQUIRE_EQ(available.size(), 6);
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
			CHECK(r.warning.is_empty());
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
		SUBCASE("exported member rename reports cross-file edits with advisory") {
			GDScriptTests::assert_no_errors_in("res://refactor/rename_cross_file_exported_user.gd");

			String out;
			RefactorResult r = run_rename(
					"res://refactor/rename_cross_file_exported_target.gd", 2, 13, "renamed_exported_count", out); // caret on `shared_exported_count`
			REQUIRE(r.ok);
			CHECK_FALSE(r.warning.is_empty());
			CHECK(r.warning.to_lower().contains("exported"));
			CHECK(r.unresolved_references.is_empty());
			REQUIRE_EQ(r.file_edits.size(), 2);

			const RefactorFileEdit *target_edits = find_file_edit(r, "res://refactor/rename_cross_file_exported_target.gd");
			REQUIRE(target_edits);
			CHECK_EQ(target_edits->edits.size(), 3);

			const RefactorFileEdit *user_edits = find_file_edit(r, "res://refactor/rename_cross_file_exported_user.gd");
			REQUIRE(user_edits);
			CHECK_EQ(user_edits->edits.size(), 2);

			const String scene_path = "res://refactor/rename_cross_file_exported_scene.tscn";
			CHECK(find_file_edit(r, scene_path) == nullptr);
			const String scene_source = FileAccess::get_file_as_string(scene_path);
			CHECK(scene_source.contains("shared_exported_count = 7"));
			CHECK_FALSE(scene_source.contains("renamed_exported_count = 7"));

			String target_out;
			REQUIRE(GDScriptRefactorEdits::apply(
					FileAccess::get_file_as_string(target_edits->path), target_edits->edits, target_out));
			CHECK(target_out.contains("@export var renamed_exported_count := 0"));
			CHECK(target_out.contains("renamed_exported_count += 1"));
			CHECK(target_out.contains("return renamed_exported_count"));
			CHECK_FALSE(target_out.contains("shared_exported_count"));

			String user_out;
			REQUIRE(GDScriptRefactorEdits::apply(FileAccess::get_file_as_string(user_edits->path), user_edits->edits, user_out));
			CHECK(user_out.contains("target.renamed_exported_count += 1"));
			CHECK(user_out.contains("return target.renamed_exported_count"));
			CHECK_FALSE(user_out.contains("shared_exported_count"));
		}
		SUBCASE("cross-file rename reports parse-failed textual references") {
			const String broken_path = "res://refactor/rename_cross_file_broken_user.gd";
			TemporaryScriptFile broken_script(
					broken_path,
					"extends Node\n"
					"\n"
					"const Target = preload(\"res://refactor/rename_cross_file_target.gd\")\n"
					"\n"
					"func use_target() -> int:\n"
					"\tvar target := Target.new()\n"
					"\ttarget.shared_count +=\n");

			String out;
			RefactorResult r = run_rename(
					"res://refactor/rename_cross_file_target.gd", 2, 5, "renamed_count", out); // caret on `shared_count`
			REQUIRE(r.ok);
			CHECK_FALSE(r.warning.is_empty());
			CHECK(r.warning.to_lower().contains("parse"));

			bool found_unresolved = false;
			for (const RefactorUnresolvedReference &unresolved : r.unresolved_references) {
				if (unresolved.path == broken_path && unresolved.line == 6 && unresolved.column == 8) {
					found_unresolved = true;
					CHECK(unresolved.message.to_lower().contains("parse"));
				}
			}
			CHECK(found_unresolved);
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

	TEST_CASE("Rename uses caller source over stale protocol cache") {
		EditorFileSystem *editor_file_system = memnew(EditorFileSystem);
		GDScriptLanguageProtocol *protocol = GDScriptTests::initialize(GDScriptTests::root);
		REQUIRE(protocol);

		GDScriptTests::assert_no_errors_in("res://refactor/rename_local.gd");

		RefactorContext ctx;
		ctx.path = "res://refactor/rename_local.gd";
		ctx.source = "extends Node\n"
					 "\n"
					 "func compute() -> int:\n"
					 "\tvar current_total := 1\n"
					 "\tcurrent_total += 2\n"
					 "\treturn current_total\n";

		RefactorParams params;
		params.new_name = "renamed_total";
		RefactorResult r = GDScriptRefactoring::prepare(ctx, caret(3, 5), RefactorKind::RENAME, params);
		REQUIRE(r.ok);
		CHECK_EQ(r.suggested_name, "current_total");

		String out;
		REQUIRE(GDScriptRefactorEdits::apply(ctx.source, r.edits, out));
		CHECK(out.contains("var renamed_total := 1"));
		CHECK(out.contains("renamed_total += 2"));
		CHECK(out.contains("return renamed_total"));
		CHECK_FALSE(out.contains("current_total"));

		memdelete(protocol);
		memdelete(editor_file_system);
		GDScriptTests::finish_language();
	}

	TEST_CASE("Add type annotation infers parameters from resolved call sites") {
		EditorFileSystem *editor_file_system = memnew(EditorFileSystem);
		GDScriptLanguageProtocol *protocol = GDScriptTests::initialize(GDScriptTests::root);
		REQUIRE(protocol);

		auto run_callsite_type_annotation = [](const String &p_res_path, int p_line, int p_column, String &r_out) -> RefactorResult {
			GDScriptTests::assert_no_errors_in(p_res_path);

			RefactorContext ctx;
			ctx.path = p_res_path;
			ctx.source = FileAccess::get_file_as_string(p_res_path);
			RefactorParams params;
			RefactorResult r = GDScriptRefactoring::prepare(ctx, caret(p_line, p_column), RefactorKind::ADD_TYPE_ANNOTATION, params);
			if (r.ok) {
				GDScriptRefactorEdits::apply(ctx.source, r.edits, r_out);
			}
			return r;
		};

		SUBCASE("agreeing call sites produce a parameter annotation") {
			GDScriptTests::assert_no_errors_in("res://refactor/callsite_parameter_agree_user.gd");

			String out;
			RefactorResult r = run_callsite_type_annotation(
					"res://refactor/callsite_parameter_agree_target.gd", 2, 19, out);
			REQUIRE(r.ok);
			CHECK(out.contains("func accept_score(score: int):"));
		}

		SUBCASE("default-less parameter before defaulted parameter keeps its insertion point") {
			GDScriptTests::assert_no_errors_in("res://refactor/callsite_parameter_before_default_user.gd");

			String out;
			RefactorResult r = run_callsite_type_annotation(
					"res://refactor/callsite_parameter_before_default_target.gd", 2, 19, out);
			REQUIRE(r.ok);
			CHECK(out.contains("func accept_value(value: String, count = 5):"));
		}

		SUBCASE("non-first parameter uses its own argument index") {
			GDScriptTests::assert_no_errors_in("res://refactor/callsite_parameter_second_arg_user.gd");

			String out;
			RefactorResult r = run_callsite_type_annotation(
					"res://refactor/callsite_parameter_second_arg_target.gd", 2, 29, out);
			REQUIRE(r.ok);
			CHECK(out.contains("func accept_second(prefix, score: int):"));
		}

		SUBCASE("disagreeing call sites are skipped") {
			GDScriptTests::assert_no_errors_in("res://refactor/callsite_parameter_disagree_user.gd");

			String out;
			RefactorResult r = run_callsite_type_annotation(
					"res://refactor/callsite_parameter_disagree_target.gd", 2, 19, out);
			CHECK_FALSE(r.ok);
			CHECK_FALSE(r.error_message.is_empty());
		}

		SUBCASE("string-based dynamic dispatch keeps direct calls skipped") {
			GDScriptTests::assert_no_errors_in("res://refactor/callsite_parameter_dynamic_user.gd");

			String out;
			RefactorResult r = run_callsite_type_annotation(
					"res://refactor/callsite_parameter_dynamic_target.gd", 2, 21, out);
			CHECK_FALSE(r.ok);
			CHECK_FALSE(r.error_message.is_empty());
		}

		SUBCASE("overridden methods are skipped") {
			GDScriptTests::assert_no_errors_in("res://refactor/callsite_parameter_override_user.gd");

			String out;
			RefactorResult r = run_callsite_type_annotation(
					"res://refactor/callsite_parameter_override_target.gd", 2, 22, out);
			CHECK_FALSE(r.ok);
			CHECK_FALSE(r.error_message.is_empty());
		}

		SUBCASE("inline property accessor call sites are included") {
			GDScriptTests::assert_no_errors_in("res://refactor/callsite_parameter_property_user.gd");

			String out;
			RefactorResult r = run_callsite_type_annotation(
					"res://refactor/callsite_parameter_property_target.gd", 2, 21, out);
			CHECK_FALSE(r.ok);
			CHECK_FALSE(r.error_message.is_empty());
		}

		memdelete(protocol);
		memdelete(editor_file_system);
		GDScriptTests::finish_language();
	}

	TEST_CASE("Implement abstract methods resolves a base defined in another file") {
		EditorFileSystem *editor_file_system = memnew(EditorFileSystem);
		GDScriptLanguageProtocol *protocol = GDScriptTests::initialize(GDScriptTests::root);
		REQUIRE(protocol);

		// Prime the workspace so the abstract base resolves. The derived script is
		// intentionally left with unimplemented abstract methods, which the analyzer
		// reports as an error; the refactor still operates on its parse tree.
		GDScriptTests::assert_no_errors_in("res://refactor/implement_abstract_base.gd");

		const String derived_path = "res://refactor/implement_abstract_derived.gd";
		RefactorContext ctx;
		ctx.path = derived_path;
		ctx.source = FileAccess::get_file_as_string(derived_path);

		SUBCASE("availability reports the refactor as enabled") {
			const Vector<RefactorAvailability> available = GDScriptRefactoring::get_available_refactors(ctx, caret(2, 0));
			const RefactorAvailability *entry = GDScriptTests::find_availability(available, RefactorKind::IMPLEMENT_ABSTRACT_METHODS);
			REQUIRE(entry != nullptr);
			CHECK(entry->enabled);
		}

		SUBCASE("prepare stubs the inherited abstract methods") {
			RefactorParams params;
			RefactorResult r = GDScriptRefactoring::prepare(ctx, caret(2, 0), RefactorKind::IMPLEMENT_ABSTRACT_METHODS, params);
			REQUIRE(r.ok);
			REQUIRE_FALSE(r.edits.is_empty());

			String out;
			REQUIRE(GDScriptRefactorEdits::apply(ctx.source, r.edits, out));
			CHECK(out.contains("func area() -> float:"));
			CHECK(out.contains("push_error(\"Not implemented: area\")"));
			CHECK(out.contains("return 0.0"));
			CHECK(out.contains("func describe() -> String:"));
			CHECK(out.contains("return \"\""));
		}

		memdelete(protocol);
		memdelete(editor_file_system);
		GDScriptTests::finish_language();
	}

	TEST_CASE("Implement abstract methods resolves a multi-level cross-file base chain") {
		EditorFileSystem *editor_file_system = memnew(EditorFileSystem);
		GDScriptLanguageProtocol *protocol = GDScriptTests::initialize(GDScriptTests::root);
		REQUIRE(protocol);

		// Prime the abstract base and the abstract intermediate so the leaf's base chain
		// resolves through separate script files. The leaf still owes ping(), inherited
		// from the base two levels up; the intermediate does not override it.
		GDScriptTests::assert_no_errors_in("res://refactor/implement_abstract_chain_base.gd");

		const String leaf_path = "res://refactor/implement_abstract_chain_leaf.gd";
		RefactorContext ctx;
		ctx.path = leaf_path;
		ctx.source = FileAccess::get_file_as_string(leaf_path);

		const Vector<RefactorAvailability> available = GDScriptRefactoring::get_available_refactors(ctx, caret(2, 0));
		const RefactorAvailability *entry = GDScriptTests::find_availability(available, RefactorKind::IMPLEMENT_ABSTRACT_METHODS);
		REQUIRE(entry != nullptr);
		CHECK(entry->enabled);

		RefactorParams params;
		RefactorResult r = GDScriptRefactoring::prepare(ctx, caret(2, 0), RefactorKind::IMPLEMENT_ABSTRACT_METHODS, params);
		REQUIRE(r.ok);
		REQUIRE_FALSE(r.edits.is_empty());

		String out;
		REQUIRE(GDScriptRefactorEdits::apply(ctx.source, r.edits, out));
		CHECK(out.contains("func ping() -> int:"));
		CHECK(out.contains("return 0"));

		memdelete(protocol);
		memdelete(editor_file_system);
		GDScriptTests::finish_language();
	}

	TEST_CASE("Implement abstract: root class with only header lines appends at end of file") {
		EditorFileSystem *editor_file_system = memnew(EditorFileSystem);
		GDScriptLanguageProtocol *protocol = GDScriptTests::initialize(GDScriptTests::root);
		REQUIRE(protocol);

		GDScriptTests::assert_no_errors_in("res://refactor/implement_abstract_base.gd");

		// A root class whose body is only header lines (@tool / class_name / extends) and
		// that owes inherited abstract methods. The stubs must be appended after the
		// extends line, not inserted mid-header, to keep the file valid.
		const String derived_path = "res://refactor/implement_abstract_header_only.gd";
		RefactorContext ctx;
		ctx.path = derived_path;
		ctx.source = FileAccess::get_file_as_string(derived_path);

		RefactorParams params;
		RefactorResult r = GDScriptRefactoring::prepare(ctx, caret(2, 0), RefactorKind::IMPLEMENT_ABSTRACT_METHODS, params);
		REQUIRE(r.ok);
		REQUIRE_FALSE(r.edits.is_empty());

		String out;
		REQUIRE(GDScriptRefactorEdits::apply(ctx.source, r.edits, out));
		const int class_name_index = out.find("class_name RefactorAbstractHeaderOnly");
		const int extends_index = out.find("extends \"res://refactor/implement_abstract_base.gd\"");
		const int func_index = out.find("func area() -> float:");
		CHECK(class_name_index >= 0);
		CHECK(extends_index >= 0);
		CHECK(func_index >= 0);
		CHECK(func_index > extends_index);
		CHECK(func_index > class_name_index);

		memdelete(protocol);
		memdelete(editor_file_system);
		GDScriptTests::finish_language();
	}
#endif // GDSCRIPT_NO_LSP
}

} // namespace GDScriptTests

#endif // TOOLS_ENABLED
