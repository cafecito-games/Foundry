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

// Coverage for the declaring-class branch (issue #2414): two classes of one script can declare
// same-named tuples, which render as the declared name alone; the clause names the declaring class
// on each side, and stays silent whenever both sides name the same one.

TEST_CASE("[Modules][FoundryScript][DataType] same_rendered_name_clause names both declaring classes across same-shaped tuples from different declarations") {
	// Two classes can declare same-named, same-shaped tuples; their nominal identities
	// (class-qualified native_type) differ, and that difference — not a Self binding — is the
	// actual incompatibility.
	FSParser::DataType expected = named_tuple_data_type(StringName("Pair"), self_type_parameter());
	expected.native_type = StringName("Left.Pair");
	FSParser::DataType actual = named_tuple_data_type(StringName("Pair"), native_data_type(StringName("Node")));
	actual.native_type = StringName("Right.Pair");
	CHECK_EQ(expected.to_string_diagnostic(), actual.to_string_diagnostic());
	CHECK_EQ(FSParser::DataType::same_rendered_name_clause(expected, "parameter", actual, "argument"),
			String(R"( The parameter is declared by class "Left"; the argument is declared by class "Right".)"));
}

TEST_CASE("[Modules][FoundryScript][DataType] same_rendered_name_clause names both declaring classes across same-named tuples with different layouts") {
	// Two distinct named tuples can share a displayed name while declaring different fields; the
	// declaring class is what tells them apart, and their slots must not be paired positionally.
	FSParser::DataType expected = named_tuple_data_type(StringName("Pair"), self_type_parameter());
	expected.native_type = StringName("Left.Pair");
	FSParser::DataType actual;
	actual.kind = FSParser::DataType::TUPLE;
	actual.type_source = FSParser::DataType::ANNOTATED_EXPLICIT;
	actual.tuple_name = StringName("Pair");
	actual.native_type = StringName("Right.Pair");
	actual.container_element_types.push_back(int_data_type());
	actual.container_element_types.push_back(native_data_type(StringName("Node")));
	actual.tuple_field_names.push_back(StringName("count"));
	actual.tuple_field_names.push_back(StringName("target"));
	CHECK_EQ(expected.to_string_diagnostic(), actual.to_string_diagnostic());
	CHECK_EQ(FSParser::DataType::same_rendered_name_clause(expected, "parameter", actual, "argument"),
			String(R"( The parameter is declared by class "Left"; the argument is declared by class "Right".)"));
}

TEST_CASE("[Modules][FoundryScript][DataType] same_rendered_name_clause names both declaring classes for same-named nominal tuples of one script") {
	FSParser::DataType expected = named_tuple_data_type(StringName("Point"), int_data_type());
	expected.native_type = StringName("res://x.fs::Left.Point");
	expected.script_path = "res://x.fs";
	FSParser::DataType actual = named_tuple_data_type(StringName("Point"), int_data_type());
	actual.native_type = StringName("res://x.fs::Right.Point");
	actual.script_path = "res://x.fs";
	CHECK_EQ(expected.to_string_diagnostic(), actual.to_string_diagnostic());
	CHECK_EQ(FSParser::DataType::same_rendered_name_clause(expected, "value", actual, "specified type"),
			String(R"( The value is declared by class "Left"; the specified type is declared by class "Right".)"));
}

TEST_CASE("[Modules][FoundryScript][DataType] same_rendered_name_clause names the script's top level for a head-class tuple") {
	// A head class's fqcn is its own declaring path, so its tuples have no class name to report; the
	// nested class on the other side does.
	FSParser::DataType expected = named_tuple_data_type(StringName("Point"), int_data_type());
	expected.native_type = StringName("res://x.fs.Point");
	expected.script_path = "res://x.fs";
	FSParser::DataType actual = named_tuple_data_type(StringName("Point"), int_data_type());
	actual.native_type = StringName("res://x.fs::Outer::Inner.Point");
	actual.script_path = "res://x.fs";
	CHECK_EQ(expected.to_string_diagnostic(), actual.to_string_diagnostic());
	CHECK_EQ(FSParser::DataType::same_rendered_name_clause(expected, "value", actual, "specified type"),
			String(R"( The value is declared at the script's top level; the specified type is declared by class "Outer.Inner".)"));
}

TEST_CASE("[Modules][FoundryScript][DataType] same_rendered_name_clause is silent for same-named tuples whose declaring class descriptors also collide") {
	// Two same-basename scripts outside every resource root collapse to one file reference, and both
	// tuples are declared by a class of the same name, so neither clause can disambiguate.
	FSParser::DataType expected = named_tuple_data_type(StringName("Point"), int_data_type());
	expected.native_type = StringName("/x/a/helper.fs::Owner.Point");
	expected.script_path = "/x/a/helper.fs";
	FSParser::DataType actual = named_tuple_data_type(StringName("Point"), int_data_type());
	actual.native_type = StringName("/y/b/helper.fs::Owner.Point");
	actual.script_path = "/y/b/helper.fs";
	CHECK_EQ(expected.to_string_diagnostic(), actual.to_string_diagnostic());
	CHECK_EQ(FSParser::DataType::same_rendered_name_clause(expected, "value", actual, "specified type"), String());
}

TEST_CASE("[Modules][FoundryScript][DataType] same_rendered_name_clause names both declaring files for same-named tuples in different files") {
	FSParser::DataType expected = named_tuple_data_type(StringName("Point"), int_data_type());
	expected.native_type = StringName("res://a/helper.fs.Point");
	expected.script_path = "res://a/helper.fs";
	FSParser::DataType actual = named_tuple_data_type(StringName("Point"), int_data_type());
	actual.native_type = StringName("res://b/helper.fs.Point");
	actual.script_path = "res://b/helper.fs";
	CHECK_EQ(expected.to_string_diagnostic(), actual.to_string_diagnostic());
	CHECK_EQ(FSParser::DataType::same_rendered_name_clause(expected, "value", actual, "specified type"),
			String(R"( The value is declared in "res://a/helper.fs"; the specified type is declared in "res://b/helper.fs".)"));
}

TEST_CASE("[Modules][FoundryScript][DataType] same_rendered_name_clause names both declaring files for same-rendered enums in same-named files") {
	// An enum renders only the basename of its qualified identity, so two same-named files declaring
	// the same enum name collide; the enum's declaring script is what tells them apart.
	FSParser::DataType expected;
	expected.kind = FSParser::DataType::ENUM;
	expected.type_source = FSParser::DataType::ANNOTATED_EXPLICIT;
	expected.native_type = StringName("res://a/helper.fs.Result");
	expected.script_path = "res://a/helper.fs";
	FSParser::DataType actual = expected;
	actual.native_type = StringName("res://b/helper.fs.Result");
	actual.script_path = "res://b/helper.fs";
	CHECK_EQ(expected.to_string_diagnostic(), actual.to_string_diagnostic());
	CHECK_EQ(FSParser::DataType::same_rendered_name_clause(expected, "value", actual, "specified type"),
			String(R"( The value is declared in "res://a/helper.fs"; the specified type is declared in "res://b/helper.fs".)"));
}

TEST_CASE("[Modules][FoundryScript][DataType] same_rendered_name_clause is silent for a native enum with no declaring script") {
	FSParser::DataType first;
	first.kind = FSParser::DataType::ENUM;
	first.type_source = FSParser::DataType::ANNOTATED_EXPLICIT;
	first.native_type = StringName("Node.ProcessMode");
	const FSParser::DataType second = first;
	CHECK_EQ(FSParser::DataType::same_rendered_name_clause(first, "value", second, "specified type"), String());
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

TEST_CASE("[Modules][FoundryScript][DataType] same_rendered_name_clause finds a Self binding in a callable rest slot") {
	// A variadic callable's rest slot is a supported `Self` position, so a named tuple field holding
	// such a callable must not retain the contradictory rendering.
	FSParser::DataType expected_callable;
	expected_callable.kind = FSParser::DataType::BUILTIN;
	expected_callable.type_source = FSParser::DataType::ANNOTATED_EXPLICIT;
	expected_callable.builtin_type = Variant::CALLABLE;
	FSParser::DataType actual_callable = expected_callable;
	expected_callable.set_method_rest_parameter_type(self_type_parameter());
	actual_callable.set_method_rest_parameter_type(native_data_type(StringName("Node")));
	FSParser::DataType expected = named_tuple_data_type(StringName("Pair"), expected_callable);
	FSParser::DataType actual = named_tuple_data_type(StringName("Pair"), actual_callable);
	CHECK_EQ(expected.to_string_diagnostic(), actual.to_string_diagnostic());
	CHECK_EQ(FSParser::DataType::same_rendered_name_clause(expected, "parameter", actual, "argument"),
			String(R"( The parameter's "Self" stands for the exact receiver at this use; the argument has "Node" as field "owner".)"));
}

TEST_CASE("[Modules][FoundryScript][DataType] same_rendered_name_clause does not descend across distinct wrapper declarations") {
	// Two same-basename scripts outside every resource root render identically and their file
	// references collapse, so neither the file clause nor the Self clause may fire: the wrapper
	// identities differ, and that — not any nested Self binding — is the incompatibility.
	FSParser::DataType expected = data_type_for_script_path("/x/a/helper.fs");
	expected.method_parameter_types.push_back(self_type_parameter());
	FSParser::DataType actual = data_type_for_script_path("/y/b/helper.fs");
	actual.method_parameter_types.push_back(native_data_type(StringName("Node")));
	CHECK_EQ(expected.to_string_diagnostic(), actual.to_string_diagnostic());
	CHECK_EQ(FSParser::DataType::same_rendered_name_clause(expected, "parameter", actual, "argument"), String());
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
