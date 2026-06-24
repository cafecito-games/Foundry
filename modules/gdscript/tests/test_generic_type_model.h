/**************************************************************************/
/*  test_generic_type_model.h                                             */
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

#include "modules/gdscript/gdscript_parser.h"

#include "tests/test_macros.h"

namespace GDScriptTests {

// These tests exercise the generic type-system model (TYPE_PARAMETER kind,
// type_arguments handles, and DataType::substitute) directly against the
// in-memory representation, independent of any parsing or execution.

using DataType = GDScriptParser::DataType;

static DataType make_builtin(Variant::Type p_type) {
	DataType type;
	type.kind = DataType::BUILTIN;
	type.builtin_type = p_type;
	type.type_source = DataType::ANNOTATED_EXPLICIT;
	return type;
}

static DataType make_type_parameter(const StringName &p_name, int p_index = 0, DataType::TypeParameterScope p_scope = DataType::TYPE_PARAMETER_CLASS) {
	DataType type;
	type.kind = DataType::TYPE_PARAMETER;
	type.type_parameter_name = p_name;
	type.type_parameter_index = p_index;
	type.type_parameter_scope = p_scope;
	type.type_source = DataType::ANNOTATED_EXPLICIT;
	return type;
}

TEST_CASE("[Modules][GDScript][GenericTypeModel] substitute replaces a bare type parameter") {
	HashMap<StringName, DataType> bindings;
	bindings.insert("T", make_builtin(Variant::INT));

	const DataType result = DataType::substitute(make_type_parameter("T"), bindings);

	CHECK(result.kind == DataType::BUILTIN);
	CHECK(result.builtin_type == Variant::INT);
}

TEST_CASE("[Modules][GDScript][GenericTypeModel] substitute leaves an unbound parameter intact") {
	HashMap<StringName, DataType> bindings;
	bindings.insert("T", make_builtin(Variant::INT));

	// "U" has no binding and must survive unchanged so an outer scope can resolve it.
	const DataType result = DataType::substitute(make_type_parameter("U", 1), bindings);

	CHECK(result.kind == DataType::TYPE_PARAMETER);
	CHECK(result.type_parameter_name == StringName("U"));
	CHECK(result.type_parameter_index == 1);
}

TEST_CASE("[Modules][GDScript][GenericTypeModel] substitute recurses into container element types") {
	DataType array_of_t = make_builtin(Variant::ARRAY);
	array_of_t.set_container_element_type(0, make_type_parameter("T"));

	HashMap<StringName, DataType> bindings;
	bindings.insert("T", make_builtin(Variant::STRING));

	const DataType result = DataType::substitute(array_of_t, bindings);

	CHECK(result.kind == DataType::BUILTIN);
	CHECK(result.builtin_type == Variant::ARRAY);
	REQUIRE(result.has_container_element_type(0));
	CHECK(result.get_container_element_type(0).kind == DataType::BUILTIN);
	CHECK(result.get_container_element_type(0).builtin_type == Variant::STRING);
}

TEST_CASE("[Modules][GDScript][GenericTypeModel] substitute recurses into type arguments") {
	// Models Box[T] as a native handle carrying a single type argument.
	DataType box_of_t;
	box_of_t.kind = DataType::NATIVE;
	box_of_t.native_type = "Box";
	box_of_t.type_source = DataType::ANNOTATED_EXPLICIT;
	box_of_t.type_arguments.push_back(make_type_parameter("T"));

	HashMap<StringName, DataType> bindings;
	bindings.insert("T", make_builtin(Variant::FLOAT));

	const DataType result = DataType::substitute(box_of_t, bindings);

	CHECK(result.kind == DataType::NATIVE);
	CHECK(result.native_type == StringName("Box"));
	REQUIRE(result.has_type_arguments());
	REQUIRE(result.type_arguments.size() == 1);
	CHECK(result.type_arguments[0].kind == DataType::BUILTIN);
	CHECK(result.type_arguments[0].builtin_type == Variant::FLOAT);
}

TEST_CASE("[Modules][GDScript][GenericTypeModel] substitute leaves a concrete type unchanged") {
	HashMap<StringName, DataType> bindings;
	bindings.insert("T", make_builtin(Variant::STRING));

	const DataType result = DataType::substitute(make_builtin(Variant::INT), bindings);

	CHECK(result.kind == DataType::BUILTIN);
	CHECK(result.builtin_type == Variant::INT);
}

TEST_CASE("[Modules][GDScript][GenericTypeModel] specialized handles differ by type argument") {
	DataType box_of_int;
	box_of_int.kind = DataType::NATIVE;
	box_of_int.native_type = "Box";
	box_of_int.type_source = DataType::ANNOTATED_EXPLICIT;
	box_of_int.type_arguments.push_back(make_builtin(Variant::INT));

	DataType box_of_string = box_of_int;
	box_of_string.type_arguments.write[0] = make_builtin(Variant::STRING);

	CHECK(box_of_int != box_of_string);

	DataType another_box_of_int = box_of_int;
	CHECK(box_of_int == another_box_of_int);
}

TEST_CASE("[Modules][GDScript][GenericTypeModel] to_string renders parameters and specializations") {
	CHECK(make_type_parameter("T").to_string() == "T");

	DataType box_of_int;
	box_of_int.kind = DataType::NATIVE;
	box_of_int.native_type = "Box";
	box_of_int.type_source = DataType::ANNOTATED_EXPLICIT;
	box_of_int.type_arguments.push_back(make_builtin(Variant::INT));
	CHECK(box_of_int.to_string() == "Box[int]");
}

} // namespace GDScriptTests
