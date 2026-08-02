/**************************************************************************/
/*  test_fs_data_type_container_roundtrip.h                               */
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

#include "modules/foundry_script/fs_function.h"

#include "core/variant/container_type_validate.h"

#include "tests/test_macros.h"

// Coverage for issue #1529: `FSDataType::to_container_type()` and the module's
// `ContainerType -> FSDataType` conversion must preserve `is_type_handle` at every
// recursive node, not only at the root. This is a pure model-conversion suite; no
// analyzer or compiler involvement is needed because the descriptors below are built
// directly in C++.

namespace FSTests {

static FSDataType data_type_native_handle(const StringName &p_native_type, bool p_is_type_handle) {
	FSDataType type;
	type.kind = FSDataType::NATIVE;
	type.builtin_type = Variant::OBJECT;
	type.native_type = p_native_type;
	type.is_type_handle = p_is_type_handle;
	return type;
}

static FSDataType data_type_builtin(Variant::Type p_builtin_type) {
	FSDataType type;
	type.kind = FSDataType::BUILTIN;
	type.builtin_type = p_builtin_type;
	return type;
}

static FSDataType data_type_array_of(const FSDataType &p_element_type) {
	FSDataType type = data_type_builtin(Variant::ARRAY);
	type.container_element_types.push_back(p_element_type);
	return type;
}

static FSDataType data_type_dictionary_of(const FSDataType &p_key_type, const FSDataType &p_value_type) {
	FSDataType type = data_type_builtin(Variant::DICTIONARY);
	type.container_element_types.push_back(p_key_type);
	type.container_element_types.push_back(p_value_type);
	return type;
}

TEST_CASE("[Modules][FoundryScript][DataType] to_container_type preserves is_type_handle at the root") {
	const FSDataType handle_type = data_type_native_handle(SNAME("Node"), true);
	const ContainerType container_type = handle_type.to_container_type();
	CHECK(container_type.is_type_handle);
	CHECK(container_type.class_name == SNAME("Node"));

	const FSDataType instance_type = data_type_native_handle(SNAME("Node"), false);
	CHECK_FALSE(instance_type.to_container_type().is_type_handle);
}

TEST_CASE("[Modules][FoundryScript][DataType] Nested is_type_handle survives FSDataType -> ContainerType -> FSDataType at two levels deep") {
	// Array[Dictionary[String, Type[Factory]]]
	const FSDataType value_type = data_type_native_handle(SNAME("Factory"), true);
	const FSDataType key_type = data_type_builtin(Variant::STRING);
	const FSDataType dictionary_type = data_type_dictionary_of(key_type, value_type);
	const FSDataType root_type = data_type_array_of(dictionary_type);

	const ContainerType container_root = root_type.to_container_type();
	CHECK_FALSE(container_root.is_type_handle);
	REQUIRE(container_root.element_types.size() == 1);
	const ContainerType &container_dictionary = container_root.element_types[0];
	CHECK_FALSE(container_dictionary.is_type_handle);
	REQUIRE(container_dictionary.element_types.size() == 2);
	CHECK_FALSE(container_dictionary.element_types[0].is_type_handle);
	CHECK(container_dictionary.element_types[1].is_type_handle);
	CHECK(container_dictionary.element_types[1].class_name == SNAME("Factory"));

	const FSDataType decoded_root = FSDataType::from_container_type(container_root);
	CHECK(decoded_root == root_type);
	REQUIRE(decoded_root.container_element_types.size() == 1);
	const FSDataType &decoded_dictionary = decoded_root.container_element_types[0];
	CHECK(decoded_dictionary == dictionary_type);
	REQUIRE(decoded_dictionary.container_element_types.size() == 2);
	CHECK_FALSE(decoded_dictionary.container_element_types[0].is_type_handle);
	CHECK(decoded_dictionary.container_element_types[1].is_type_handle);
}

TEST_CASE("[Modules][FoundryScript][DataType] is_type_handle survives a type_arguments child, distinguishing Slot[Type[Factory]] from Slot[Factory]") {
	FSDataType handle_argument = data_type_native_handle(SNAME("Factory"), true);
	FSDataType slot_with_handle_argument = data_type_native_handle(SNAME("Slot"), false);
	slot_with_handle_argument.type_arguments.push_back(handle_argument);

	FSDataType instance_argument = data_type_native_handle(SNAME("Factory"), false);
	FSDataType slot_with_instance_argument = data_type_native_handle(SNAME("Slot"), false);
	slot_with_instance_argument.type_arguments.push_back(instance_argument);

	const ContainerType container_handle_argument = slot_with_handle_argument.to_container_type();
	REQUIRE(container_handle_argument.type_arguments.size() == 1);
	CHECK(container_handle_argument.type_arguments[0].is_type_handle);

	const ContainerType container_instance_argument = slot_with_instance_argument.to_container_type();
	REQUIRE(container_instance_argument.type_arguments.size() == 1);
	CHECK_FALSE(container_instance_argument.type_arguments[0].is_type_handle);

	CHECK(container_handle_argument != container_instance_argument);

	const FSDataType decoded_handle_argument = FSDataType::from_container_type(container_handle_argument);
	const FSDataType decoded_instance_argument = FSDataType::from_container_type(container_instance_argument);
	CHECK(decoded_handle_argument == slot_with_handle_argument);
	CHECK(decoded_instance_argument == slot_with_instance_argument);
	CHECK(decoded_handle_argument != decoded_instance_argument);
	REQUIRE(decoded_handle_argument.type_arguments.size() == 1);
	CHECK(decoded_handle_argument.type_arguments[0].is_type_handle);
	REQUIRE(decoded_instance_argument.type_arguments.size() == 1);
	CHECK_FALSE(decoded_instance_argument.type_arguments[0].is_type_handle);
}

TEST_CASE("[Modules][FoundryScript][DataType] A descriptor mixing handle and instance nodes at the same depth round-trips both independently") {
	// Dictionary[Type[Node], Node]
	const FSDataType key_type = data_type_native_handle(SNAME("Node"), true);
	const FSDataType value_type = data_type_native_handle(SNAME("Node"), false);
	const FSDataType root_type = data_type_dictionary_of(key_type, value_type);

	const ContainerType container_root = root_type.to_container_type();
	REQUIRE(container_root.element_types.size() == 2);
	CHECK(container_root.element_types[0].is_type_handle);
	CHECK_FALSE(container_root.element_types[1].is_type_handle);

	const FSDataType decoded_root = FSDataType::from_container_type(container_root);
	CHECK(decoded_root == root_type);
	REQUIRE(decoded_root.container_element_types.size() == 2);
	CHECK(decoded_root.container_element_types[0].is_type_handle);
	CHECK_FALSE(decoded_root.container_element_types[1].is_type_handle);
	CHECK(decoded_root.container_element_types[0] != decoded_root.container_element_types[1]);
}

TEST_CASE("[Modules][FoundryScript][DataType] from_container_type and from_type_handle_container_type agree on every node but the root") {
	// A `Slot[Type[Factory]]`-shaped descriptor, itself passed as a root with `is_type_handle` unset:
	// `from_container_type()` must leave the root as an instance type while `from_type_handle_container_type()`
	// treats it as a handle. Both must agree that the nested `type_arguments` child stays a handle either way,
	// since only the root's flag is a parameter of the two entry points.
	const FSDataType child_handle = data_type_native_handle(SNAME("Factory"), true);
	FSDataType root_descriptor = data_type_native_handle(SNAME("Slot"), false);
	root_descriptor.type_arguments.push_back(child_handle);

	const ContainerType container_root = root_descriptor.to_container_type();
	CHECK_FALSE(container_root.is_type_handle);
	REQUIRE(container_root.type_arguments.size() == 1);
	CHECK(container_root.type_arguments[0].is_type_handle);

	const FSDataType via_from_container_type = FSDataType::from_container_type(container_root);
	CHECK_FALSE(via_from_container_type.is_type_handle);
	REQUIRE(via_from_container_type.type_arguments.size() == 1);
	CHECK(via_from_container_type.type_arguments[0].is_type_handle);

	const FSDataType via_from_type_handle_container_type = FSDataType::from_type_handle_container_type(container_root);
	CHECK(via_from_type_handle_container_type.is_type_handle);
	REQUIRE(via_from_type_handle_container_type.type_arguments.size() == 1);
	CHECK(via_from_type_handle_container_type.type_arguments[0].is_type_handle);

	// The two entry points agree everywhere but the root.
	CHECK(via_from_container_type.type_arguments[0] == via_from_type_handle_container_type.type_arguments[0]);
}

TEST_CASE("[Modules][FoundryScript][DataType] is_same_container_type distinguishes a handle descriptor from an otherwise identical instance descriptor") {
	const FSDataType handle_type = data_type_native_handle(SNAME("Node"), true);
	const FSDataType instance_type = data_type_native_handle(SNAME("Node"), false);

	CHECK(handle_type.is_same_container_type(handle_type.to_container_type()));
	CHECK_FALSE(handle_type.is_same_container_type(instance_type.to_container_type()));
	CHECK_FALSE(instance_type.is_same_container_type(handle_type.to_container_type()));
}

TEST_CASE("[Modules][FoundryScript][DataType] A nullable FSDataType converts to an untyped ContainerType and carries no handle flag") {
	FSDataType nullable_handle = data_type_native_handle(SNAME("Node"), true);
	nullable_handle.is_nullable = true;

	const ContainerType container_type = nullable_handle.to_container_type();
	CHECK(container_type.builtin_type == Variant::NIL);
	CHECK(container_type.class_name == StringName());
	CHECK_FALSE(container_type.is_type_handle);
}

} // namespace FSTests
