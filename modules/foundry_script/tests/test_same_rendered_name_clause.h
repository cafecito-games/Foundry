/**************************************************************************/
/*  test_same_rendered_name_clause.h                                      */
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

#include "modules/foundry_script/fs_parser.h"

#include "tests/test_macros.h"

// Coverage for the same-rendered-name disambiguation clause (issue #2400): two unnamed script
// types declared in same-named files render identically in a type position, so a diagnostic that
// contrasts them appends a clause naming both declaring files. The clause must stay silent
// whenever it cannot actually disambiguate.

namespace FSTests {

static FSParser::DataType data_type_for_script_path(const String &p_script_path) {
	FSParser::DataType type;
	type.kind = FSParser::DataType::SCRIPT;
	type.type_source = FSParser::DataType::ANNOTATED_EXPLICIT;
	type.script_path = p_script_path;
	return type;
}

TEST_CASE("[Modules][FoundryScript][DataType] same_rendered_name_clause names both declaring files on a rendered-name collision") {
	const FSParser::DataType first = data_type_for_script_path("res://a/helper.fs");
	const FSParser::DataType second = data_type_for_script_path("res://b/helper.fs");
	// Both sides render identically, which is the collision the clause exists for.
	CHECK_EQ(first.to_string_diagnostic(), second.to_string_diagnostic());
	CHECK_EQ(FSParser::DataType::same_rendered_name_clause(first, "value", second, "specified type"),
			String(R"( The value is declared in "res://a/helper.fs"; the specified type is declared in "res://b/helper.fs".)"));
}

TEST_CASE("[Modules][FoundryScript][DataType] same_rendered_name_clause is silent when the rendered names differ") {
	const FSParser::DataType first = data_type_for_script_path("res://a/helper.fs");
	const FSParser::DataType second = data_type_for_script_path("res://b/other.fs");
	CHECK_EQ(FSParser::DataType::same_rendered_name_clause(first, "value", second, "specified type"), String());
}

TEST_CASE("[Modules][FoundryScript][DataType] same_rendered_name_clause is silent when both sides are the same file") {
	const FSParser::DataType first = data_type_for_script_path("res://a/helper.fs");
	const FSParser::DataType second = data_type_for_script_path("res://a/helper.fs");
	CHECK_EQ(FSParser::DataType::same_rendered_name_clause(first, "value", second, "specified type"), String());
}

TEST_CASE("[Modules][FoundryScript][DataType] same_rendered_name_clause is silent when both file references collapse to the same basename") {
	// Two distinct absolute paths outside every resource root fall back to their basename, so the
	// clause would repeat the colliding name twice and disambiguate nothing.
	const FSParser::DataType first = data_type_for_script_path("/x/a/helper.fs");
	const FSParser::DataType second = data_type_for_script_path("/y/b/helper.fs");
	CHECK_EQ(first.to_string_diagnostic(), second.to_string_diagnostic());
	CHECK_EQ(FSParser::DataType::same_rendered_name_clause(first, "value", second, "specified type"), String());
}

TEST_CASE("[Modules][FoundryScript][DataType] same_rendered_name_clause is silent when either side has no declaring script path") {
	FSParser::DataType builtin;
	builtin.kind = FSParser::DataType::BUILTIN;
	builtin.type_source = FSParser::DataType::ANNOTATED_EXPLICIT;
	builtin.builtin_type = Variant::INT;
	const FSParser::DataType script = data_type_for_script_path("res://a/helper.fs");
	CHECK_EQ(FSParser::DataType::same_rendered_name_clause(builtin, "value", script, "specified type"), String());
	CHECK_EQ(FSParser::DataType::same_rendered_name_clause(script, "value", builtin, "specified type"), String());
}

} // namespace FSTests
