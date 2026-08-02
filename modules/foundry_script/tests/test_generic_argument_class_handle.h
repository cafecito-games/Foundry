/**************************************************************************/
/*  test_generic_argument_class_handle.h                                  */
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

#ifdef TOOLS_ENABLED

// `compile_bytecode_test_source` publishes the script through FSCache the way production loading
// does, which is what lets an inner class construct itself (`Slot[Type[Node]].new()`) during
// analysis. Every module test header is compiled into one translation unit, so this include does not
// duplicate test registrations.
#include "test_bytecode_serialization.h"

#include "../foundry_script.h"

#include "core/object/script_instance.h"
#include "core/variant/container_type_validate.h"
#include "scene/main/node.h"

#include "tests/test_macros.h"

// Coverage for `Type[T]` used as a generic class type argument. The `.fs` fixtures under
// `modules/foundry_script/tests/scripts` cover the source-level acceptances and rejections; these
// cases assert on the reified descriptor an instance actually carries, which is what makes
// `Slot[Type[Factory]]` and `Slot[Factory]` non-interchangeable outside analyzed source.

namespace FSTests {

static Vector<ContainerType> reified_type_arguments_of(const Variant &p_instance) {
	Vector<ContainerType> type_arguments;
	Object *object = p_instance;
	if (object == nullptr) {
		return type_arguments;
	}
	ScriptInstance *script_instance = object->get_script_instance();
	if (script_instance == nullptr) {
		return type_arguments;
	}
	script_instance->get_reified_type_arguments(type_arguments);
	return type_arguments;
}

TEST_CASE("[Modules][FoundryScript][GenericArgumentHandle] A specialized instance reifies a class-handle argument") {
	const Ref<FoundryScript> script = compile_bytecode_test_source(
			"extends RefCounted\n"
			"\n"
			"class Slot[T]:\n"
			"\tvar value: T\n"
			"\n"
			"var handles := Slot[Type[Node]].new()\n"
			"var instances := Slot[Node].new()\n"
			"var nested := Slot[Array[Type[Node]]].new()\n");

	Callable::CallError call_error;
	const Variant instance = script->_new(nullptr, -1, call_error);
	REQUIRE(call_error.error == Callable::CallError::CALL_OK);
	Object *instance_object = instance;
	REQUIRE(instance_object != nullptr);

	const Vector<ContainerType> handle_arguments = reified_type_arguments_of(instance_object->get(SNAME("handles")));
	REQUIRE(handle_arguments.size() == 1);
	CHECK(handle_arguments[0].is_type_handle);
	CHECK(handle_arguments[0].get_type_name() == "Type[Node]");

	const Vector<ContainerType> instance_arguments = reified_type_arguments_of(instance_object->get(SNAME("instances")));
	REQUIRE(instance_arguments.size() == 1);
	CHECK_FALSE(instance_arguments[0].is_type_handle);

	// The whole point of the layer: the two specializations are never the same descriptor, so an
	// invariance comparison of reified arguments separates them.
	CHECK(handle_arguments[0] != instance_arguments[0]);

	// A container-shaped argument keeps the handle flag on the element, not on the container.
	const Vector<ContainerType> nested_arguments = reified_type_arguments_of(instance_object->get(SNAME("nested")));
	REQUIRE(nested_arguments.size() == 1);
	CHECK_FALSE(nested_arguments[0].is_type_handle);
	REQUIRE(nested_arguments[0].element_types.size() == 1);
	CHECK(nested_arguments[0].element_types[0].is_type_handle);
	CHECK(nested_arguments[0].get_type_name() == "Array[Type[Node]]");
}

TEST_CASE("[Modules][FoundryScript][GenericArgumentHandle] A reified handle argument validates handles, not instances") {
	const Ref<FoundryScript> script = compile_bytecode_test_source(
			"extends RefCounted\n"
			"\n"
			"class Slot[T]:\n"
			"\tvar value: T\n"
			"\n"
			"var handles := Slot[Type[Node]].new()\n");

	Callable::CallError call_error;
	const Variant instance = script->_new(nullptr, -1, call_error);
	REQUIRE(call_error.error == Callable::CallError::CALL_OK);
	Object *instance_object = instance;
	REQUIRE(instance_object != nullptr);

	const Vector<ContainerType> handle_arguments = reified_type_arguments_of(instance_object->get(SNAME("handles")));
	REQUIRE(handle_arguments.size() == 1);

	ContainerTypeValidate validator(handle_arguments[0]);
	validator.where = "member";

	ERR_PRINT_OFF;
	Node *node = memnew(Node);
	Variant node_value = node;
	CHECK_FALSE(validator.validate(node_value, "assign"));
	memdelete(node);

	// A native class handle for the represented type is what the slot accepts.
	Ref<FSNativeClass> node_class = memnew(FSNativeClass(SNAME("Node")));
	Variant native_handle = node_class;
	CHECK(validator.validate(native_handle, "assign"));

	Ref<FSNativeClass> ref_counted_class = memnew(FSNativeClass(SNAME("RefCounted")));
	Variant unrelated_handle = ref_counted_class;
	CHECK_FALSE(validator.validate(unrelated_handle, "assign"));
	ERR_PRINT_ON;
}

} // namespace FSTests

#endif // TOOLS_ENABLED
