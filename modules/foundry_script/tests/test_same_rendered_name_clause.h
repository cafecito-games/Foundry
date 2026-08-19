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

static FSParser::DataType int_data_type() {
	FSParser::DataType type;
	type.kind = FSParser::DataType::BUILTIN;
	type.type_source = FSParser::DataType::ANNOTATED_EXPLICIT;
	type.builtin_type = Variant::INT;
	return type;
}

static FSParser::DataType self_type_parameter() {
	FSParser::DataType type;
	type.kind = FSParser::DataType::TYPE_PARAMETER;
	type.type_source = FSParser::DataType::ANNOTATED_EXPLICIT;
	type.type_parameter_name = StringName("@Self");
	return type;
}

static FSParser::DataType native_data_type(const StringName &p_native_type) {
	FSParser::DataType type;
	type.kind = FSParser::DataType::NATIVE;
	type.type_source = FSParser::DataType::ANNOTATED_EXPLICIT;
	type.native_type = p_native_type;
	return type;
}

static FSParser::DataType named_tuple_data_type(const StringName &p_name, const FSParser::DataType &p_owner_field_type) {
	FSParser::DataType type;
	type.kind = FSParser::DataType::TUPLE;
	type.type_source = FSParser::DataType::ANNOTATED_EXPLICIT;
	type.tuple_name = p_name;
	type.container_element_types.push_back(int_data_type());
	type.container_element_types.push_back(p_owner_field_type);
	type.tuple_field_names.push_back(StringName("index"));
	type.tuple_field_names.push_back(StringName("owner"));
	return type;
}

// Coverage for the Self-binding branch (issue #2408): a named tuple with a `Self` field renders only
// its declared name, so a receiver-identity rejection contrasts two identical spellings; the clause
// names the side that keeps `Self` and the concrete type the other side carries in its place.

TEST_CASE("[Modules][FoundryScript][DataType] same_rendered_name_clause names a divergent Self binding of a named tuple field") {
	const FSParser::DataType expected = named_tuple_data_type(StringName("Pair"), self_type_parameter());
	const FSParser::DataType actual = named_tuple_data_type(StringName("Pair"), native_data_type(StringName("Node")));
	CHECK_EQ(expected.to_string_diagnostic(), actual.to_string_diagnostic());
	CHECK_EQ(FSParser::DataType::same_rendered_name_clause(expected, "parameter", actual, "argument"),
			String(R"( The parameter's "Self" stands for the exact receiver at this use; the argument has "Node" as field "owner".)"));
}

TEST_CASE("[Modules][FoundryScript][DataType] same_rendered_name_clause names the Self side regardless of argument order") {
	const FSParser::DataType expected = named_tuple_data_type(StringName("Pair"), self_type_parameter());
	const FSParser::DataType actual = named_tuple_data_type(StringName("Pair"), native_data_type(StringName("Node")));
	// The assignment sites pass the value first and the declared type second; the clause still
	// attributes `Self` to the side that actually reads it.
	CHECK_EQ(FSParser::DataType::same_rendered_name_clause(actual, "value", expected, "specified type"),
			String(R"( The specified type's "Self" stands for the exact receiver at this use; the value has "Node" as field "owner".)"));
}

TEST_CASE("[Modules][FoundryScript][DataType] same_rendered_name_clause is silent when both sides keep Self") {
	const FSParser::DataType first = named_tuple_data_type(StringName("Pair"), self_type_parameter());
	const FSParser::DataType second = named_tuple_data_type(StringName("Pair"), self_type_parameter());
	CHECK_EQ(FSParser::DataType::same_rendered_name_clause(first, "parameter", second, "argument"), String());
}

TEST_CASE("[Modules][FoundryScript][DataType] same_rendered_name_clause is silent when neither side mentions Self") {
	const FSParser::DataType first = named_tuple_data_type(StringName("Pair"), native_data_type(StringName("Node")));
	const FSParser::DataType second = named_tuple_data_type(StringName("Pair"), native_data_type(StringName("Resource")));
	CHECK_EQ(FSParser::DataType::same_rendered_name_clause(first, "parameter", second, "argument"), String());
}

TEST_CASE("[Modules][FoundryScript][DataType] same_rendered_name_clause is silent across same-named tuples with different layouts") {
	// Two distinct named tuples can share a displayed name while declaring different fields; pairing
	// their slots positionally would describe one declaration's field with the other's type.
	FSParser::DataType expected = named_tuple_data_type(StringName("Pair"), self_type_parameter());
	FSParser::DataType actual;
	actual.kind = FSParser::DataType::TUPLE;
	actual.type_source = FSParser::DataType::ANNOTATED_EXPLICIT;
	actual.tuple_name = StringName("Pair");
	actual.container_element_types.push_back(int_data_type());
	actual.container_element_types.push_back(native_data_type(StringName("Node")));
	actual.tuple_field_names.push_back(StringName("count"));
	actual.tuple_field_names.push_back(StringName("target"));
	CHECK_EQ(expected.to_string_diagnostic(), actual.to_string_diagnostic());
	CHECK_EQ(FSParser::DataType::same_rendered_name_clause(expected, "parameter", actual, "argument"), String());
}

TEST_CASE("[Modules][FoundryScript][DataType] same_rendered_name_clause skips lists whose sizes differ") {
	FSParser::DataType expected = named_tuple_data_type(StringName("Pair"), self_type_parameter());
	FSParser::DataType actual = named_tuple_data_type(StringName("Pair"), native_data_type(StringName("Node")));
	actual.container_element_types.push_back(int_data_type());
	CHECK_EQ(FSParser::DataType::same_rendered_name_clause(expected, "parameter", actual, "argument"), String());
}

TEST_CASE("[Modules][FoundryScript][DataType] same_rendered_name_clause omits the field label for a positional slot") {
	FSParser::DataType expected = named_tuple_data_type(StringName("Pair"), self_type_parameter());
	FSParser::DataType actual = named_tuple_data_type(StringName("Pair"), native_data_type(StringName("Node")));
	expected.tuple_field_names.clear();
	actual.tuple_field_names.clear();
	CHECK_EQ(FSParser::DataType::same_rendered_name_clause(expected, "parameter", actual, "argument"),
			String(R"( The parameter's "Self" stands for the exact receiver at this use; the argument has "Node" in its place.)"));
}

TEST_CASE("[Modules][FoundryScript][DataType] same_rendered_name_clause finds a Self binding inside a type argument") {
	FSParser::DataType expected = native_data_type(StringName("Box"));
	expected.add_type_argument(self_type_parameter());
	FSParser::DataType actual = native_data_type(StringName("Box"));
	actual.add_type_argument(native_data_type(StringName("Box")));
	// `Box[Self]` and `Box[Box]` render differently, so the clause's rendered-name gate keeps it
	// silent; the walk itself must still terminate cleanly on nested type arguments.
	CHECK_EQ(FSParser::DataType::same_rendered_name_clause(expected, "parameter", actual, "argument"), String());
}

} // namespace FSTests
