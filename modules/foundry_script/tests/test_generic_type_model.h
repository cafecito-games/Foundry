/**************************************************************************/
/*  test_generic_type_model.h                                             */
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

#include <utility>

namespace FSTests {

// These tests exercise the generic type-system model (TYPE_PARAMETER kind,
// type_arguments handles, and DataType::substitute) directly against the
// in-memory representation, independent of any parsing or execution.

using DataType = FSParser::DataType;

static DataType make_builtin(Variant::Type p_type) {
	DataType type;
	type.kind = DataType::BUILTIN;
	type.builtin_type = p_type;
	type.type_source = DataType::ANNOTATED_EXPLICIT;
	return type;
}

static DataType make_native(const StringName &p_name) {
	DataType type;
	type.kind = DataType::NATIVE;
	type.native_type = p_name;
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

TEST_CASE("[Modules][FoundryScript][GenericTypeModel] substitute replaces a bare type parameter") {
	HashMap<StringName, DataType> bindings;
	bindings.insert("T", make_builtin(Variant::INT));

	const DataType result = DataType::substitute(make_type_parameter("T"), bindings);

	CHECK(result.kind == DataType::BUILTIN);
	CHECK(result.builtin_type == Variant::INT);
}

TEST_CASE("[Modules][FoundryScript][GenericTypeModel] substitute leaves an unbound parameter intact") {
	HashMap<StringName, DataType> bindings;
	bindings.insert("T", make_builtin(Variant::INT));

	// "U" has no binding and must survive unchanged so an outer scope can resolve it.
	const DataType result = DataType::substitute(make_type_parameter("U", 1), bindings);

	CHECK(result.kind == DataType::TYPE_PARAMETER);
	CHECK(result.type_parameter_name == StringName("U"));
	CHECK(result.type_parameter_index == 1);
}

TEST_CASE("[Modules][FoundryScript][GenericTypeModel] substitute recurses into container element types") {
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

TEST_CASE("[Modules][FoundryScript][GenericTypeModel] substitute preserves Type wrapper on type parameters") {
	DataType type_of_t = make_type_parameter("T");
	type_of_t.is_meta_type = true;
	type_of_t.is_type_handle_annotation = true;
	type_of_t.is_nullable = true;

	HashMap<StringName, DataType> bindings;
	bindings.insert("T", make_native("Node"));

	const DataType result = DataType::substitute(type_of_t, bindings);

	CHECK(result.kind == DataType::NATIVE);
	CHECK(result.native_type == StringName("Node"));
	CHECK(result.is_meta_type);
	CHECK(result.is_type_handle_annotation);
	CHECK(result.is_nullable);
	CHECK(result.to_string() == "Type[Node]?");
}

TEST_CASE("[Modules][FoundryScript][GenericTypeModel] substitute keeps Type handle nullability on wrapper") {
	DataType type_of_t = make_type_parameter("T");
	type_of_t.is_meta_type = true;
	type_of_t.is_type_handle_annotation = true;

	DataType nullable_node = make_native("Node");
	nullable_node.is_nullable = true;

	HashMap<StringName, DataType> bindings;
	bindings.insert("T", nullable_node);

	const DataType result = DataType::substitute(type_of_t, bindings);

	CHECK(result.kind == DataType::NATIVE);
	CHECK(result.native_type == StringName("Node"));
	CHECK(result.is_meta_type);
	CHECK(result.is_type_handle_annotation);
	CHECK(!result.is_nullable);
	CHECK(result.to_string() == "Type[Node]");
}

TEST_CASE("[Modules][FoundryScript][GenericTypeModel] substitute recurses into type arguments") {
	// Models Box[T] as a native handle carrying a single type argument.
	DataType box_of_t;
	box_of_t.kind = DataType::NATIVE;
	box_of_t.native_type = "Box";
	box_of_t.type_source = DataType::ANNOTATED_EXPLICIT;
	box_of_t.add_type_argument(make_type_parameter("T"));

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

TEST_CASE("[Modules][FoundryScript][GenericTypeModel] substitute leaves a concrete type unchanged") {
	HashMap<StringName, DataType> bindings;
	bindings.insert("T", make_builtin(Variant::STRING));

	const DataType result = DataType::substitute(make_builtin(Variant::INT), bindings);

	CHECK(result.kind == DataType::BUILTIN);
	CHECK(result.builtin_type == Variant::INT);
}

TEST_CASE("[Modules][FoundryScript][GenericTypeModel] specialized handles differ by type argument") {
	DataType box_of_int;
	box_of_int.kind = DataType::NATIVE;
	box_of_int.native_type = "Box";
	box_of_int.type_source = DataType::ANNOTATED_EXPLICIT;
	box_of_int.add_type_argument(make_builtin(Variant::INT));

	DataType box_of_string = box_of_int;
	box_of_string.set_type_argument(0, make_builtin(Variant::STRING));

	CHECK(box_of_int != box_of_string);

	DataType another_box_of_int = box_of_int;
	CHECK(box_of_int == another_box_of_int);
}

TEST_CASE("[Modules][FoundryScript][GenericTypeModel] type parameters compare by name, scope, index, and bound") {
	const DataType t_class_zero = make_type_parameter("T", 0, DataType::TYPE_PARAMETER_CLASS);

	CHECK(t_class_zero == make_type_parameter("T", 0, DataType::TYPE_PARAMETER_CLASS));
	// A different ordinal position is a different parameter.
	CHECK(t_class_zero != make_type_parameter("T", 1, DataType::TYPE_PARAMETER_CLASS));
	// A class-level T and a method-level T are distinct.
	CHECK(t_class_zero != make_type_parameter("T", 0, DataType::TYPE_PARAMETER_METHOD));

	// A bound participates in identity, e.g. unconstrained `T` differs from `T: String`.
	DataType bounded = make_type_parameter("T", 0, DataType::TYPE_PARAMETER_CLASS);
	bounded.type_parameter_bound.push_back(make_builtin(Variant::STRING));
	CHECK(t_class_zero != bounded);
}

TEST_CASE("[Modules][FoundryScript][GenericTypeModel] to_string renders parameters and specializations") {
	CHECK(make_type_parameter("T").to_string() == "T");

	DataType box_of_int;
	box_of_int.kind = DataType::NATIVE;
	box_of_int.native_type = "Box";
	box_of_int.type_source = DataType::ANNOTATED_EXPLICIT;
	box_of_int.add_type_argument(make_builtin(Variant::INT));
	CHECK(box_of_int.to_string() == "Box[int]");
}

TEST_CASE("[Modules][FoundryScript][GenericTypeModel] Copy and assignment preserve every provenance and identity field") {
	DataType original = make_builtin(Variant::INT);
	original.is_constant = true;
	original.is_read_only = true;
	original.is_meta_type = true;
	original.is_type_handle_annotation = true;
	original.is_pseudo_type = true;
	original.is_coroutine = true;
	original.is_nullable = true;
	original.numeric_type_is_carrier_erased = true;
	original.is_substituted_self = true;
	original.is_receiver_self_contract = true;
	original.has_method_signature = true;
	original.has_explicit_method_signature = true;
	original.signature_is_async = true;
	original.method_return_is_erased_container = true;
	original.callable_is_over_bound = true;
	original.is_tagged_union = true;

	original.container_element_types.push_back(make_builtin(Variant::STRING));
	original.add_type_argument(make_builtin(Variant::FLOAT));
	original.type_parameter_bound.push_back(make_builtin(Variant::STRING));

	DataType::EnumCasePayload payload;
	payload.field_names.push_back("value");
	payload.field_types.push_back(make_builtin(Variant::INT));
	original.enum_case_payloads.insert("Case", payload);

	original.tuple_field_names.push_back("field");

	const DataType copy_constructed(original);
	DataType assigned;
	assigned = original;

	const DataType *copies[] = { &copy_constructed, &assigned };
	for (const DataType *copy : copies) {
		CHECK(copy->is_constant == original.is_constant);
		CHECK(copy->is_read_only == original.is_read_only);
		CHECK(copy->is_meta_type == original.is_meta_type);
		CHECK(copy->is_type_handle_annotation == original.is_type_handle_annotation);
		CHECK(copy->is_pseudo_type == original.is_pseudo_type);
		CHECK(copy->is_coroutine == original.is_coroutine);
		CHECK(copy->is_nullable == original.is_nullable);
		CHECK(copy->numeric_type_is_carrier_erased == original.numeric_type_is_carrier_erased);
		CHECK(copy->is_substituted_self == original.is_substituted_self);
		CHECK(copy->is_receiver_self_contract == original.is_receiver_self_contract);
		CHECK(copy->has_method_signature == original.has_method_signature);
		CHECK(copy->has_explicit_method_signature == original.has_explicit_method_signature);
		CHECK(copy->signature_is_async == original.signature_is_async);
		CHECK(copy->method_return_is_erased_container == original.method_return_is_erased_container);
		CHECK(copy->callable_is_over_bound == original.callable_is_over_bound);
		CHECK(copy->is_tagged_union == original.is_tagged_union);

		REQUIRE(copy->container_element_types.size() == 1);
		CHECK(copy->container_element_types[0].builtin_type == Variant::STRING);

		REQUIRE(copy->type_arguments.size() == 1);
		CHECK(copy->type_arguments[0].builtin_type == Variant::FLOAT);

		REQUIRE(copy->type_parameter_bound.size() == 1);
		CHECK(copy->type_parameter_bound[0].builtin_type == Variant::STRING);

		REQUIRE(copy->enum_case_payloads.has("Case"));
		REQUIRE(copy->enum_case_payloads["Case"].field_names.size() == 1);
		CHECK(copy->enum_case_payloads["Case"].field_names[0] == StringName("value"));

		REQUIRE(copy->tuple_field_names.size() == 1);
		CHECK(copy->tuple_field_names[0] == StringName("field"));

		CHECK(*copy == original);
	}

	CHECK(copy_constructed == assigned);
}

TEST_CASE("[Modules][FoundryScript][GenericTypeModel] A moved DataType leaves an empty source and an intact destination") {
	DataType source = make_native("Box");
	source.add_type_argument(make_builtin(Variant::INT));
	source.container_element_types.push_back(make_builtin(Variant::STRING));

	DataType destination = std::move(source);

	CHECK(destination.kind == DataType::NATIVE);
	CHECK(destination.native_type == StringName("Box"));
	REQUIRE(destination.type_arguments.size() == 1);
	CHECK(destination.type_arguments[0].builtin_type == Variant::INT);
	REQUIRE(destination.container_element_types.size() == 1);
	CHECK(destination.container_element_types[0].builtin_type == Variant::STRING);

	// A moved-from Vector<DataType> member is left empty, since Vector's own move constructor
	// takes ownership of the source's storage rather than copying it.
	CHECK(source.type_arguments.is_empty());
	CHECK(source.container_element_types.is_empty());
}

TEST_CASE("[Modules][FoundryScript][GenericTypeModel] A type argument slot drops a carrier-erased width") {
	DataType erased_int = make_builtin(Variant::INT);
	erased_int.numeric_type = NumericType::INT32;
	erased_int.numeric_type_is_carrier_erased = true;

	DataType via_type_argument;
	via_type_argument.kind = DataType::NATIVE;
	via_type_argument.native_type = "Box";
	via_type_argument.add_type_argument(erased_int);

	DataType via_container_element = make_builtin(Variant::ARRAY);
	via_container_element.set_container_element_type(0, erased_int);

	REQUIRE(via_type_argument.type_arguments.size() == 1);
	const DataType &argument_slot = via_type_argument.type_arguments[0];
	CHECK(argument_slot.numeric_type == NumericType::NONE);
	CHECK(argument_slot.numeric_type_is_carrier_erased == false);

	const DataType element_slot = via_container_element.get_container_element_type(0);
	CHECK(argument_slot.numeric_type == element_slot.numeric_type);
	CHECK(argument_slot.numeric_type_is_carrier_erased == element_slot.numeric_type_is_carrier_erased);
	CHECK(argument_slot.builtin_type == element_slot.builtin_type);
}

TEST_CASE("[Modules][FoundryScript][GenericTypeModel] set_type_argument grows the argument list with Variant slots") {
	DataType handle;
	handle.kind = DataType::NATIVE;
	handle.native_type = "Box";

	handle.set_type_argument(2, make_builtin(Variant::INT));

	REQUIRE(handle.type_arguments.size() == 3);
	CHECK(handle.type_arguments[0].kind == DataType::VARIANT);
	CHECK(handle.type_arguments[1].kind == DataType::VARIANT);
	CHECK(handle.type_arguments[2].kind == DataType::BUILTIN);
	CHECK(handle.type_arguments[2].builtin_type == Variant::INT);
}

} // namespace FSTests
