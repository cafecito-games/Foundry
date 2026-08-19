/**************************************************************************/
/*  test_tuple_slot_predecode.h                                           */
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

#if defined(TOOLS_ENABLED) && defined(DEBUG_ENABLED)

// Shared in-process compile helper, and the disassembly helpers used to count the store instructions
// that share one predecoded shape. Every module test header is compiled into the same generated test
// translation unit, so these includes do not duplicate test registrations.
#include "test_bytecode_serialization.h"
#include "test_tuple_lowering.h"

#include "modules/foundry_script/fs_bytecode_loader.h"
#include "modules/foundry_script/fs_function.h"

#include "core/io/dir_access.h"

#include "tests/test_macros.h"

namespace FSTests {

static const FSFunction *predecode_test_function(const Ref<FoundryScript> &p_script, const StringName &p_function_name) {
	FSFunction *const *entry = p_script->get_member_functions().getptr(p_function_name);
	REQUIRE(entry != nullptr);
	REQUIRE(*entry != nullptr);
	return *entry;
}

// The one shape a function predecoded, found by the only key the table has: the descriptor's constant
// index.
static const FSDataType *only_predecoded_tuple_shape(const FSFunction *p_function) {
	const FSDataType *found = nullptr;
	for (int constant_index = 0; constant_index < p_function->get_constants_count(); constant_index++) {
		const FSDataType *shape = p_function->get_predecoded_tuple_shape_for_constant(constant_index);
		if (shape != nullptr) {
			CHECK(found == nullptr);
			found = shape;
		}
	}
	return found;
}

static FSInstance *tuple_slot_test_instance(const Variant &p_value) {
	Object *object = p_value;
	REQUIRE(object != nullptr);
	ScriptInstance *instance = object->get_script_instance();
	REQUIRE(instance != nullptr);
	return static_cast<FSInstance *>(instance);
}

static ContainerType tuple_slot_builtin_type(Variant::Type p_builtin_type) {
	ContainerType type;
	type.builtin_type = p_builtin_type;
	return type;
}

static Vector<ContainerType> tuple_slot_one_argument(Variant::Type p_builtin_type) {
	Vector<ContainerType> arguments;
	arguments.push_back(tuple_slot_builtin_type(p_builtin_type));
	return arguments;
}

static const FSDataType *only_specialized_tuple_shape(const FSFunction *p_function, const FSTupleSlotSpecialization *p_specialization) {
	const FSDataType *found = nullptr;
	for (int constant_index = 0; constant_index < p_function->get_constants_count(); constant_index++) {
		const FSDataType *shape = p_function->get_specialized_tuple_shape_for_constant(p_specialization, constant_index);
		if (shape != nullptr) {
			CHECK(found == nullptr);
			found = shape;
		}
	}
	return found;
}

static Variant construct_specialized_crate(const Ref<FoundryScript> &p_crate, const Vector<ContainerType> &p_type_arguments) {
	Callable::CallError call_error;
	const Variant constructed = p_crate->_new_specialized(nullptr, -1, p_type_arguments, call_error);
	REQUIRE(call_error.error == Callable::CallError::CALL_OK);
	return constructed;
}

TEST_CASE("[FoundryScript][TupleStore] A receiver-independent slot shape is decoded once for the function") {
	// Both stores in `keep` -- the local's initializer and the return -- are compiled against the same
	// `(int, String)` descriptor constant, so the function decodes exactly one shape and both
	// instructions read it.
	Ref<FoundryScript> script = compile_bytecode_test_source(
			"func keep(value) -> (int, String):\n"
			"\tvar kept: (int, String) = value\n"
			"\treturn kept\n");

	const FSFunction *keep = predecode_test_function(script, SNAME("keep"));
	CHECK(keep->get_predecoded_tuple_shape_count() == 1);

	const Vector<String> store_lines = filter_disassembly_lines(
			disassemble_test_function(script, SNAME("keep")), "assign typed tuple");
	CHECK(store_lines.size() == 2);

	const FSDataType *shape = only_predecoded_tuple_shape(keep);
	REQUIRE(shape != nullptr);
	CHECK(shape->kind == FSDataType::TUPLE);
	CHECK(shape->builtin_type == Variant::ARRAY);
	REQUIRE(shape->container_element_types.size() == 2);
	CHECK(shape->container_element_types[0].builtin_type == Variant::INT);
	CHECK(shape->container_element_types[1].builtin_type == Variant::STRING);

	// The decoded shape is what the store enforces, so the accepted and rejected values are unchanged.
	Callable::CallError call_error;
	Variant owner = script->_new(nullptr, -1, call_error);
	REQUIRE(call_error.error == Callable::CallError::CALL_OK);
	Object *instance = owner;
	REQUIRE(instance != nullptr);

	Array source;
	source.push_back(7);
	source.push_back("seven");
	const Variant accepted_argument = source;
	const Variant *accepted_arguments[1] = { &accepted_argument };
	const Variant accepted = instance->callp(SNAME("keep"), accepted_arguments, 1, call_error);
	REQUIRE(call_error.error == Callable::CallError::CALL_OK);
	const Array stored = accepted;
	REQUIRE(stored.size() == 2);
	CHECK(int(stored[0]) == 7);
	CHECK(String(stored[1]) == "seven");
	// The canonical carrier the store normalizes to is unchanged by predecoding.
	CHECK(stored.is_read_only());
	CHECK_FALSE(stored.is_typed());
}

TEST_CASE("[FoundryScript][TupleStore] A shape that names the receiver keeps the per-execution decode") {
	// A type parameter resolves against what the receiver reified and `Self` against the frame's
	// receiver, so neither has a single frame-independent answer -- at the slot's root, in a nested
	// tuple element, or under a type argument.
	Ref<FoundryScript> script = compile_bytecode_test_source(
			"class Crate[T]:\n"
			"\tfunc direct_parameter(value) -> void:\n"
			"\t\tvar kept: (int, T) = value\n"
			"\n"
			"\tfunc nested_parameter(value) -> void:\n"
			"\t\tvar kept: (int, (String, T)) = value\n"
			"\n"
			"\tfunc concrete(value) -> void:\n"
			"\t\tvar kept: (int, String) = value\n"
			"\n"
			"class Anchor:\n"
			"\tfunc direct_self(value: (int, Self)) -> void:\n"
			"\t\tvar kept: (int, Self) = value\n"
			"\n"
			"\tfunc nested_self(value: (int, (String, Self))) -> void:\n"
			"\t\tvar kept: (int, (String, Self)) = value\n");

	const HashMap<StringName, Ref<FoundryScript>>::ConstIterator crate = script->get_subclasses().find(SNAME("Crate"));
	REQUIRE(crate != script->get_subclasses().end());
	const HashMap<StringName, Ref<FoundryScript>>::ConstIterator anchor = script->get_subclasses().find(SNAME("Anchor"));
	REQUIRE(anchor != script->get_subclasses().end());

	// Each of these does store through a tuple descriptor -- otherwise "nothing was predecoded" would
	// be true for the uninteresting reason that there is nothing to predecode. The independent table
	// stays empty; the function records the descriptor as specialization-dependent instead.
	auto check_dependent = [](const Ref<FoundryScript> &p_owner, const StringName &p_function_name) {
		CAPTURE(String(p_function_name));
		CHECK_FALSE(filter_disassembly_lines(
				disassemble_test_function(p_owner, p_function_name), "assign typed tuple")
						.is_empty());
		const FSFunction *function = predecode_test_function(p_owner, p_function_name);
		CHECK(function->get_predecoded_tuple_shape_count() == 0);
		CHECK(function->get_dependent_tuple_descriptor_count() == 1);
	};
	check_dependent(crate->value, SNAME("direct_parameter"));
	check_dependent(crate->value, SNAME("nested_parameter"));
	check_dependent(anchor->value, SNAME("direct_self"));
	check_dependent(anchor->value, SNAME("nested_self"));

	// A concrete slot in the same generic class is still independent: the exclusion is per descriptor,
	// not per declaring class.
	CHECK(predecode_test_function(crate->value, SNAME("concrete"))->get_predecoded_tuple_shape_count() == 1);
}

TEST_CASE("[FoundryScript][TupleStore] The dependent-store benchmark uses the specialization cache") {
	// `tuple_store_dependent/feature.fs` measures a receiver-dependent store. The independent table
	// must stay empty (the slot names `T`), and constructing `Crate[String]` must intern the
	// specialized shape the loop then reads. If that script ever stopped emitting a typed-tuple
	// assign, or started predecoding `(int, T)` as frame-independent, the numbers would no longer
	// answer the question the case was added for.
	if (!FSLanguage::get_singleton()->get_reflection_singleton().is_valid()) {
		FSLanguage::get_singleton()->init();
	}

	Ref<FoundryScript> script;
	script.instantiate();
	const String path = "modules/foundry_script/tests/benchmarks/tuple_store_dependent/feature.fs";
	REQUIRE(script->load_source_code(path) == OK);
	script->set_path(path);
	REQUIRE(script->reload() == OK);

	const HashMap<StringName, Ref<FoundryScript>>::ConstIterator crate = script->get_subclasses().find(SNAME("Crate"));
	REQUIRE(crate != script->get_subclasses().end());
	const FSFunction *store_loop = predecode_test_function(crate->value, SNAME("store_loop"));
	CHECK_FALSE(filter_disassembly_lines(
			disassemble_test_function(crate->value, SNAME("store_loop")), "assign typed tuple")
					.is_empty());
	CHECK(store_loop->get_predecoded_tuple_shape_count() == 0);
	CHECK(store_loop->get_dependent_tuple_descriptor_count() == 1);

	const Vector<ContainerType> string_arguments = tuple_slot_one_argument(Variant::STRING);
	construct_specialized_crate(crate->value, string_arguments);
	const FSDataType *shape = only_specialized_tuple_shape(
			store_loop, crate->value->find_tuple_slot_specialization(string_arguments).ptr());
	REQUIRE(shape != nullptr);
	CHECK(shape->container_element_types[1].builtin_type == Variant::STRING);
}

TEST_CASE("[FoundryScript][TupleStore] A key-typed Dictionary constant is never read as a descriptor") {
	// Descriptors share the constant table with ordinary script data. A Dictionary keyed by anything but
	// String reports a failure for every String-key lookup made on it, so such a constant has to be
	// recognized as data before its keys are inspected.
	BytecodeErrorRecorder recorder;
	Ref<FoundryScript> script = compile_bytecode_test_source(
			"const KEYED = Dictionary({ 1: 1 }, TYPE_INT, &\"\", null, TYPE_INT, &\"\", null)\n"
			"\n"
			"func keep(value) -> void:\n"
			"\tvar kept: (int, String) = value\n"
			"\tprint(KEYED.size())\n");
	CHECK_FALSE(recorder.messages.contains("TypedDictionary"));

	// The tuple slot in the same function is still predecoded.
	CHECK(predecode_test_function(script, SNAME("keep"))->get_predecoded_tuple_shape_count() == 1);
}

TEST_CASE("[FoundryScript][TupleStore] A parameter erased below a container leaves nothing to resolve") {
	// `Array[T]` as a tuple element lowers to a bare Array: a tuple under a typed container carries no
	// parameter node into the descriptor, so the shape a frame would rebuild is the erased one, and the
	// decoded shape is that same erased Array. The exclusion is about what the descriptor says, not
	// about what the declaration mentioned.
	Ref<FoundryScript> script = compile_bytecode_test_source(
			"class Crate[T]:\n"
			"\tfunc parameter_under_argument(value) -> void:\n"
			"\t\tvar kept: (int, Array[T]) = value\n");

	const HashMap<StringName, Ref<FoundryScript>>::ConstIterator crate = script->get_subclasses().find(SNAME("Crate"));
	REQUIRE(crate != script->get_subclasses().end());
	const FSFunction *store = predecode_test_function(crate->value, SNAME("parameter_under_argument"));
	CHECK(store->get_predecoded_tuple_shape_count() == 1);

	const FSDataType *shape = only_predecoded_tuple_shape(store);
	REQUIRE(shape != nullptr);
	REQUIRE(shape->container_element_types.size() == 2);
	CHECK(shape->container_element_types[1].builtin_type == Variant::ARRAY);
	CHECK(shape->container_element_types[1].container_element_types.is_empty());
}

TEST_CASE("[FoundryScript][TupleStore] A slot shape loaded from compiled bytecode is decoded once too") {
	// The table is derived from the constants, never serialized, so the loader has to produce it from
	// the same finalization step the byte code generator uses.
	const Ref<FoundryScript> original = compile_bytecode_test_source(
			"func keep(value) -> (int, String):\n"
			"\tvar kept: (int, String) = value\n"
			"\treturn kept\n");
	CHECK(predecode_test_function(original, SNAME("keep"))->get_predecoded_tuple_shape_count() == 1);

	FSBytecodeExporter exporter;
	Vector<uint8_t> buffer;
	REQUIRE(exporter.serialize(original, buffer) == OK);

	// A shipped game runs from bytecode alone, so remove the source before rebuilding the script.
	const String script_path = original->get_script_path();
	REQUIRE(DirAccess::remove_absolute(script_path) == OK);

	BytecodeTestResolver resolver;
	Ref<FoundryScript> restored;
	restored.instantiate();
	restored->set_path_cache(script_path);
	FSBytecodeLoader loader;
	loader.set_resolver(&resolver);
	REQUIRE(loader.load_skeleton(buffer, restored) == OK);
	REQUIRE(loader.load_full(buffer, restored) == OK);
	REQUIRE(restored->is_valid());

	const FSFunction *keep = predecode_test_function(restored, SNAME("keep"));
	CHECK(keep->get_predecoded_tuple_shape_count() == 1);
	const FSDataType *shape = only_predecoded_tuple_shape(keep);
	REQUIRE(shape != nullptr);
	CHECK(shape->kind == FSDataType::TUPLE);
	REQUIRE(shape->container_element_types.size() == 2);
	CHECK(shape->container_element_types[0].builtin_type == Variant::INT);
	CHECK(shape->container_element_types[1].builtin_type == Variant::STRING);

	Callable::CallError call_error;
	Variant owner = restored->_new(nullptr, -1, call_error);
	REQUIRE(call_error.error == Callable::CallError::CALL_OK);
	Object *instance = owner;
	REQUIRE(instance != nullptr);

	Array source;
	source.push_back(1);
	source.push_back("one");
	const Variant argument = source;
	const Variant *arguments[1] = { &argument };
	const Array stored = instance->callp(SNAME("keep"), arguments, 1, call_error);
	REQUIRE(call_error.error == Callable::CallError::CALL_OK);
	REQUIRE(stored.size() == 2);
	CHECK(int(stored[0]) == 1);
	CHECK(String(stored[1]) == "one");
	CHECK(stored.is_read_only());
}

TEST_CASE("[FoundryScript][TupleStore] A specialized dependent slot is answered from the specialization cache") {
	Ref<FoundryScript> script = compile_bytecode_test_source(
			"class Crate[T]:\n"
			"\tfunc keep(value) -> (int, T):\n"
			"\t\tvar kept: (int, T) = value\n"
			"\t\treturn kept\n");

	const HashMap<StringName, Ref<FoundryScript>>::ConstIterator crate = script->get_subclasses().find(SNAME("Crate"));
	REQUIRE(crate != script->get_subclasses().end());
	const FSFunction *keep = predecode_test_function(crate->value, SNAME("keep"));
	CHECK(keep->get_predecoded_tuple_shape_count() == 0);
	CHECK(keep->get_dependent_tuple_descriptor_count() == 1);

	const Vector<ContainerType> string_arguments = tuple_slot_one_argument(Variant::STRING);
	const Variant string_crate = construct_specialized_crate(crate->value, string_arguments);
	const FSTupleSlotSpecialization *string_specialization =
			crate->value->find_tuple_slot_specialization(string_arguments).ptr();
	REQUIRE(string_specialization != nullptr);
	CHECK(tuple_slot_test_instance(string_crate)->get_type_arguments() == string_arguments);

	const FSDataType *shape = only_specialized_tuple_shape(keep, string_specialization);
	REQUIRE(shape != nullptr);
	REQUIRE(shape->container_element_types.size() == 2);
	CHECK(shape->container_element_types[0].builtin_type == Variant::INT);
	CHECK(shape->container_element_types[1].builtin_type == Variant::STRING);

	Array source;
	source.push_back(7);
	source.push_back("seven");
	const Variant accepted_argument = source;
	const Variant *accepted_arguments[1] = { &accepted_argument };
	Callable::CallError call_error;
	Object *instance = string_crate;
	const Array stored = instance->callp(SNAME("keep"), accepted_arguments, 1, call_error);
	REQUIRE(call_error.error == Callable::CallError::CALL_OK);
	REQUIRE(stored.size() == 2);
	CHECK(int(stored[0]) == 7);
	CHECK(String(stored[1]) == "seven");
	CHECK(stored.is_read_only());
	CHECK_FALSE(stored.is_typed());
}

TEST_CASE("[FoundryScript][TupleStore] Two specializations of one function do not share a shape") {
	Ref<FoundryScript> script = compile_bytecode_test_source(
			"class Crate[T]:\n"
			"\tfunc keep(value) -> void:\n"
			"\t\tvar kept: (int, T) = value\n"
			"\n"
			"\tfunc nested(value) -> void:\n"
			"\t\tvar kept: (int, (String, T)) = value\n");

	const HashMap<StringName, Ref<FoundryScript>>::ConstIterator crate = script->get_subclasses().find(SNAME("Crate"));
	REQUIRE(crate != script->get_subclasses().end());
	const FSFunction *keep = predecode_test_function(crate->value, SNAME("keep"));
	const FSFunction *nested = predecode_test_function(crate->value, SNAME("nested"));

	const Vector<ContainerType> string_arguments = tuple_slot_one_argument(Variant::STRING);
	const Vector<ContainerType> int_arguments = tuple_slot_one_argument(Variant::INT);
	construct_specialized_crate(crate->value, string_arguments);
	construct_specialized_crate(crate->value, int_arguments);

	const FSTupleSlotSpecialization *string_specialization =
			crate->value->find_tuple_slot_specialization(string_arguments).ptr();
	const FSTupleSlotSpecialization *int_specialization =
			crate->value->find_tuple_slot_specialization(int_arguments).ptr();
	REQUIRE(string_specialization != nullptr);
	REQUIRE(int_specialization != nullptr);
	CHECK(string_specialization != int_specialization);

	const FSDataType *string_shape = only_specialized_tuple_shape(keep, string_specialization);
	const FSDataType *int_shape = only_specialized_tuple_shape(keep, int_specialization);
	REQUIRE(string_shape != nullptr);
	REQUIRE(int_shape != nullptr);
	CHECK(string_shape != int_shape);
	CHECK(string_shape->container_element_types[1].builtin_type == Variant::STRING);
	CHECK(int_shape->container_element_types[1].builtin_type == Variant::INT);

	const FSDataType *nested_string = only_specialized_tuple_shape(nested, string_specialization);
	REQUIRE(nested_string != nullptr);
	REQUIRE(nested_string->container_element_types.size() == 2);
	REQUIRE(nested_string->container_element_types[1].container_element_types.size() == 2);
	CHECK(nested_string->container_element_types[1].container_element_types[1].builtin_type == Variant::STRING);
}

TEST_CASE("[FoundryScript][TupleStore] An unspecialized receiver stays on the per-execution path") {
	Ref<FoundryScript> script = compile_bytecode_test_source(
			"class Crate[T]:\n"
			"\tfunc keep(value) -> (int, T):\n"
			"\t\tvar kept: (int, T) = value\n"
			"\t\treturn kept\n");

	const HashMap<StringName, Ref<FoundryScript>>::ConstIterator crate = script->get_subclasses().find(SNAME("Crate"));
	REQUIRE(crate != script->get_subclasses().end());
	const FSFunction *keep = predecode_test_function(crate->value, SNAME("keep"));
	CHECK(keep->get_dependent_tuple_descriptor_count() == 1);
	CHECK(crate->value->find_tuple_slot_specialization(Vector<ContainerType>()).is_null());

	Callable::CallError call_error;
	const Variant raw = crate->value->_new(nullptr, -1, call_error);
	REQUIRE(call_error.error == Callable::CallError::CALL_OK);
	CHECK(crate->value->find_tuple_slot_specialization(Vector<ContainerType>()).is_null());
	CHECK(only_specialized_tuple_shape(keep, crate->value->find_tuple_slot_specialization(Vector<ContainerType>()).ptr()) == nullptr);

	Array source;
	source.push_back(1);
	source.push_back("anything");
	const Variant argument = source;
	const Variant *arguments[1] = { &argument };
	Object *instance = raw;
	const Array stored = instance->callp(SNAME("keep"), arguments, 1, call_error);
	REQUIRE(call_error.error == Callable::CallError::CALL_OK);
	REQUIRE(stored.size() == 2);
	CHECK(int(stored[0]) == 1);
	CHECK(String(stored[1]) == "anything");
}

TEST_CASE("[FoundryScript][TupleStore] A Self-dependent slot is cached per specialization") {
	Ref<FoundryScript> script = compile_bytecode_test_source(
			"class Anchor:\n"
			"\tfunc keep(value: (int, Self)) -> (int, Self):\n"
			"\t\tvar kept: (int, Self) = value\n"
			"\t\treturn kept\n"
			"\n"
			"\tfunc nested(value: (int, (String, Self))) -> void:\n"
			"\t\tvar kept: (int, (String, Self)) = value\n"
			"\n"
			"class Derived extends Anchor:\n"
			"\tpass\n");

	const HashMap<StringName, Ref<FoundryScript>>::ConstIterator anchor = script->get_subclasses().find(SNAME("Anchor"));
	const HashMap<StringName, Ref<FoundryScript>>::ConstIterator derived = script->get_subclasses().find(SNAME("Derived"));
	REQUIRE(anchor != script->get_subclasses().end());
	REQUIRE(derived != script->get_subclasses().end());

	const FSFunction *keep = predecode_test_function(anchor->value, SNAME("keep"));
	CHECK(keep->get_predecoded_tuple_shape_count() == 0);
	CHECK(keep->get_dependent_tuple_descriptor_count() == 1);

	const FSTupleSlotSpecialization *anchor_specialization =
			anchor->value->find_tuple_slot_specialization(Vector<ContainerType>()).ptr();
	const FSTupleSlotSpecialization *derived_specialization =
			derived->value->find_tuple_slot_specialization(Vector<ContainerType>()).ptr();
	REQUIRE(anchor_specialization != nullptr);
	REQUIRE(derived_specialization != nullptr);
	CHECK(anchor_specialization != derived_specialization);

	const FSDataType *anchor_shape = only_specialized_tuple_shape(keep, anchor_specialization);
	const FSDataType *derived_shape = only_specialized_tuple_shape(keep, derived_specialization);
	REQUIRE(anchor_shape != nullptr);
	REQUIRE(derived_shape != nullptr);
	CHECK(anchor_shape != derived_shape);
	CHECK(anchor_shape->container_element_types[1].to_container_type().script == Ref<Script>(anchor->value));
	CHECK(derived_shape->container_element_types[1].to_container_type().script == Ref<Script>(derived->value));

	const FSFunction *nested = predecode_test_function(anchor->value, SNAME("nested"));
	const FSDataType *nested_shape = only_specialized_tuple_shape(nested, anchor_specialization);
	REQUIRE(nested_shape != nullptr);
	REQUIRE(nested_shape->container_element_types[1].container_element_types.size() == 2);
	CHECK(nested_shape->container_element_types[1].container_element_types[1].to_container_type().script == Ref<Script>(anchor->value));
}

TEST_CASE("[FoundryScript][TupleStore] A static Self-dependent slot is answered from the specialization cache") {
	// A static frame has no `FSInstance`, so the store must read the interned table from the
	// receiver context. `Self` is the descriptor a static method can name; a class type
	// parameter is erased in a static body and is refused at analysis.
	Ref<FoundryScript> script = compile_bytecode_test_source(
			"class Anchor:\n"
			"\tstatic func keep(value: (int, Self)) -> (int, Self):\n"
			"\t\tvar kept: (int, Self) = value\n"
			"\t\treturn kept\n");

	const HashMap<StringName, Ref<FoundryScript>>::ConstIterator anchor = script->get_subclasses().find(SNAME("Anchor"));
	REQUIRE(anchor != script->get_subclasses().end());
	const FSFunction *keep = predecode_test_function(anchor->value, SNAME("keep"));
	CHECK(keep->get_predecoded_tuple_shape_count() == 0);
	CHECK(keep->get_dependent_tuple_descriptor_count() == 1);

	const FSTupleSlotSpecialization *specialization =
			anchor->value->find_tuple_slot_specialization(Vector<ContainerType>()).ptr();
	REQUIRE(specialization != nullptr);

	const FSDataType *shape = only_specialized_tuple_shape(keep, specialization);
	REQUIRE(shape != nullptr);
	REQUIRE(shape->container_element_types.size() == 2);
	CHECK(shape->container_element_types[1].to_container_type().script == Ref<Script>(anchor->value));

	Callable::CallError call_error;
	const Variant receiver = anchor->value->_new(nullptr, -1, call_error);
	REQUIRE(call_error.error == Callable::CallError::CALL_OK);

	Array source;
	source.push_back(7);
	source.push_back(receiver);
	const Variant accepted_argument = source;
	const Variant *accepted_arguments[1] = { &accepted_argument };
	Object *static_receiver = anchor->value.ptr();
	const Array stored = static_receiver->callp(SNAME("keep"), accepted_arguments, 1, call_error);
	REQUIRE(call_error.error == Callable::CallError::CALL_OK);
	REQUIRE(stored.size() == 2);
	CHECK(int(stored[0]) == 7);
	CHECK(stored[1] == receiver);
	CHECK(stored.is_read_only());
}

TEST_CASE("[FoundryScript][TupleStore] A freed specialization table is not used by a retained receiver") {
	Ref<FoundryScript> script = compile_bytecode_test_source(
			"class Anchor:\n"
			"\tfunc keep(value: (int, Self)) -> void:\n"
			"\t\tvar kept: (int, Self) = value\n");

	const HashMap<StringName, Ref<FoundryScript>>::ConstIterator anchor = script->get_subclasses().find(SNAME("Anchor"));
	REQUIRE(anchor != script->get_subclasses().end());

	REQUIRE(anchor->value->find_tuple_slot_specialization(Vector<ContainerType>()).is_valid());

	anchor->value->clear();
	CHECK(anchor->value->find_tuple_slot_specialization(Vector<ContainerType>()).is_null());
}

TEST_CASE("[FoundryScript][TupleStore] A specialized dependent slot loaded from compiled bytecode is cached too") {
	const Ref<FoundryScript> original = compile_bytecode_test_source(
			"class Crate[T]:\n"
			"\tfunc keep(value) -> (int, T):\n"
			"\t\tvar kept: (int, T) = value\n"
			"\t\treturn kept\n");
	const HashMap<StringName, Ref<FoundryScript>>::ConstIterator original_crate = original->get_subclasses().find(SNAME("Crate"));
	REQUIRE(original_crate != original->get_subclasses().end());
	CHECK(predecode_test_function(original_crate->value, SNAME("keep"))->get_dependent_tuple_descriptor_count() == 1);

	FSBytecodeExporter exporter;
	Vector<uint8_t> buffer;
	REQUIRE(exporter.serialize(original, buffer) == OK);

	const String script_path = original->get_script_path();
	REQUIRE(DirAccess::remove_absolute(script_path) == OK);

	BytecodeTestResolver resolver;
	Ref<FoundryScript> restored;
	restored.instantiate();
	restored->set_path_cache(script_path);
	FSBytecodeLoader loader;
	loader.set_resolver(&resolver);
	REQUIRE(loader.load_skeleton(buffer, restored) == OK);
	REQUIRE(loader.load_full(buffer, restored) == OK);
	REQUIRE(restored->is_valid());

	const HashMap<StringName, Ref<FoundryScript>>::ConstIterator crate = restored->get_subclasses().find(SNAME("Crate"));
	REQUIRE(crate != restored->get_subclasses().end());
	const FSFunction *keep = predecode_test_function(crate->value, SNAME("keep"));
	CHECK(keep->get_predecoded_tuple_shape_count() == 0);
	CHECK(keep->get_dependent_tuple_descriptor_count() == 1);

	const Vector<ContainerType> string_arguments = tuple_slot_one_argument(Variant::STRING);
	const Variant string_crate = construct_specialized_crate(crate->value, string_arguments);
	const FSDataType *shape = only_specialized_tuple_shape(keep, crate->value->find_tuple_slot_specialization(string_arguments).ptr());
	REQUIRE(shape != nullptr);
	CHECK(shape->container_element_types[1].builtin_type == Variant::STRING);

	Array source;
	source.push_back(1);
	source.push_back("one");
	const Variant argument = source;
	const Variant *arguments[1] = { &argument };
	Callable::CallError call_error;
	Object *instance = string_crate;
	const Array stored = instance->callp(SNAME("keep"), arguments, 1, call_error);
	REQUIRE(call_error.error == Callable::CallError::CALL_OK);
	REQUIRE(stored.size() == 2);
	CHECK(int(stored[0]) == 1);
	CHECK(String(stored[1]) == "one");
	CHECK(stored.is_read_only());
}

TEST_CASE("[FoundryScript][TupleStore] A specialized store rejects a value an unspecialized receiver accepts") {
	// The per-execution path and the cache agree on a conforming value. They disagree on a
	// mismatch once `T` is reified: `Crate[String]` rejects a second `int`, while an
	// unspecialized `Crate` degrades that element and accepts it.
	Ref<FoundryScript> script = compile_bytecode_test_source(
			"class Crate[T]:\n"
			"\tfunc keep(value) -> Variant:\n"
			"\t\tvar kept: (int, T) = value\n"
			"\t\treturn kept\n");

	const HashMap<StringName, Ref<FoundryScript>>::ConstIterator crate = script->get_subclasses().find(SNAME("Crate"));
	REQUIRE(crate != script->get_subclasses().end());

	Array mismatched;
	mismatched.push_back(7);
	mismatched.push_back(7);
	const Variant argument = mismatched;
	const Variant *arguments[1] = { &argument };
	Callable::CallError call_error;

	const Variant specialized = construct_specialized_crate(crate->value, tuple_slot_one_argument(Variant::STRING));
	Object *specialized_instance = specialized;
	ERR_PRINT_OFF;
	const Variant rejected = specialized_instance->callp(SNAME("keep"), arguments, 1, call_error);
	ERR_PRINT_ON;
	CHECK(rejected.get_type() == Variant::NIL);

	const Variant raw = crate->value->_new(nullptr, -1, call_error);
	REQUIRE(call_error.error == Callable::CallError::CALL_OK);
	Object *unspecialized_instance = raw;
	const Array accepted = unspecialized_instance->callp(SNAME("keep"), arguments, 1, call_error);
	REQUIRE(call_error.error == Callable::CallError::CALL_OK);
	REQUIRE(accepted.size() == 2);
	CHECK(int(accepted[0]) == 7);
	CHECK(int(accepted[1]) == 7);
}

TEST_CASE("[FoundryScript][TupleStore] Reloading a script republishes shapes against the new functions") {
	Ref<FoundryScript> script = compile_bytecode_test_source(
			"class Crate[T]:\n"
			"\tfunc keep(value) -> void:\n"
			"\t\tvar kept: (int, T) = value\n");

	const HashMap<StringName, Ref<FoundryScript>>::ConstIterator crate = script->get_subclasses().find(SNAME("Crate"));
	REQUIRE(crate != script->get_subclasses().end());
	const FSFunction *keep_before = predecode_test_function(crate->value, SNAME("keep"));
	const Vector<ContainerType> string_arguments = tuple_slot_one_argument(Variant::STRING);
	crate->value->intern_tuple_slot_specialization(string_arguments);
	const FSDataType *shape_before = only_specialized_tuple_shape(
			keep_before, crate->value->find_tuple_slot_specialization(string_arguments).ptr());
	REQUIRE(shape_before != nullptr);
	CHECK(shape_before->container_element_types[0].builtin_type == Variant::INT);

	// Reload with a different leading slot so a surviving pre-reload table cannot satisfy the
	// assertion. The function pointer itself is not compared: the allocator commonly reuses it.
	script->set_source_code(
			"class Crate[T]:\n"
			"\tfunc keep(value) -> void:\n"
			"\t\tvar kept: (String, T) = value\n");
	REQUIRE(script->reload() == OK);

	const HashMap<StringName, Ref<FoundryScript>>::ConstIterator reloaded = script->get_subclasses().find(SNAME("Crate"));
	REQUIRE(reloaded != script->get_subclasses().end());
	const FSFunction *keep_after = predecode_test_function(reloaded->value, SNAME("keep"));
	reloaded->value->intern_tuple_slot_specialization(string_arguments);
	const FSDataType *shape = only_specialized_tuple_shape(
			keep_after, reloaded->value->find_tuple_slot_specialization(string_arguments).ptr());
	REQUIRE(shape != nullptr);
	CHECK(shape->container_element_types[0].builtin_type == Variant::STRING);
	CHECK(shape->container_element_types[1].builtin_type == Variant::STRING);
}

TEST_CASE("[FoundryScript][TupleStore] A derived class declared before its generic base still caches the base slot") {
	Ref<FoundryScript> script = compile_bytecode_test_source(
			"class Derived extends Crate[int]:\n"
			"\tpass\n"
			"\n"
			"class Crate[T]:\n"
			"\tfunc keep(value) -> void:\n"
			"\t\tvar kept: (int, T) = value\n");

	const HashMap<StringName, Ref<FoundryScript>>::ConstIterator crate = script->get_subclasses().find(SNAME("Crate"));
	const HashMap<StringName, Ref<FoundryScript>>::ConstIterator derived = script->get_subclasses().find(SNAME("Derived"));
	REQUIRE(crate != script->get_subclasses().end());
	REQUIRE(derived != script->get_subclasses().end());

	const FSFunction *keep = predecode_test_function(crate->value, SNAME("keep"));
	const FSDataType *shape = only_specialized_tuple_shape(
			keep, derived->value->find_tuple_slot_specialization(Vector<ContainerType>()).ptr());
	REQUIRE(shape != nullptr);
	CHECK(shape->container_element_types[1].builtin_type == Variant::INT);
}

TEST_CASE("[FoundryScript][TupleStore] A folded specialized handle still publishes the cache") {
	// `_prepare_compilation` stores `CRATE` before any function exists. Interning then must not
	// record a sticky "no dependent descriptors" answer, or the end-of-unit intern publishes nothing.
	Ref<FoundryScript> script = compile_bytecode_test_source(
			"class Crate[T]:\n"
			"\tfunc keep(value) -> void:\n"
			"\t\tvar kept: (int, T) = value\n"
			"\n"
			"class Holder:\n"
			"\tconst CRATE := Crate[int]\n");

	const HashMap<StringName, Ref<FoundryScript>>::ConstIterator crate = script->get_subclasses().find(SNAME("Crate"));
	REQUIRE(crate != script->get_subclasses().end());
	const FSFunction *keep = predecode_test_function(crate->value, SNAME("keep"));
	const Vector<ContainerType> int_arguments = tuple_slot_one_argument(Variant::INT);
	const FSDataType *shape = only_specialized_tuple_shape(
			keep, crate->value->find_tuple_slot_specialization(int_arguments).ptr());
	REQUIRE(shape != nullptr);
	CHECK(shape->container_element_types[1].builtin_type == Variant::INT);
}

TEST_CASE("[FoundryScript][TupleStore] A folded specialized handle loaded from compiled bytecode is cached too") {
	const Ref<FoundryScript> original = compile_bytecode_test_source(
			"class Crate[T]:\n"
			"\tfunc keep(value) -> void:\n"
			"\t\tvar kept: (int, T) = value\n"
			"\n"
			"class Holder:\n"
			"\tconst CRATE := Crate[int]\n");

	FSBytecodeExporter exporter;
	Vector<uint8_t> buffer;
	REQUIRE(exporter.serialize(original, buffer) == OK);

	const String script_path = original->get_script_path();
	REQUIRE(DirAccess::remove_absolute(script_path) == OK);

	BytecodeTestResolver resolver;
	Ref<FoundryScript> restored;
	restored.instantiate();
	restored->set_path_cache(script_path);
	FSBytecodeLoader loader;
	loader.set_resolver(&resolver);
	REQUIRE(loader.load_skeleton(buffer, restored) == OK);
	REQUIRE(loader.load_full(buffer, restored) == OK);
	REQUIRE(restored->is_valid());

	const HashMap<StringName, Ref<FoundryScript>>::ConstIterator crate = restored->get_subclasses().find(SNAME("Crate"));
	REQUIRE(crate != restored->get_subclasses().end());
	const FSFunction *keep = predecode_test_function(crate->value, SNAME("keep"));
	const Vector<ContainerType> int_arguments = tuple_slot_one_argument(Variant::INT);
	const FSDataType *shape = only_specialized_tuple_shape(
			keep, crate->value->find_tuple_slot_specialization(int_arguments).ptr());
	REQUIRE(shape != nullptr);
	CHECK(shape->container_element_types[1].builtin_type == Variant::INT);
}

TEST_CASE("[FoundryScript][TupleStore] Reloading only a generic base does not serve a stale derived shape") {
	static int unique_index = 0;
	const String class_name = vformat("TupleSlotReloadBase_%d", unique_index++);
	Ref<FoundryScript> base = compile_bytecode_test_source(vformat(
			"class_name %s\n"
			"class Crate[T]:\n"
			"\tfunc keep(value) -> Variant:\n"
			"\t\tvar kept: (int, T) = value\n"
			"\t\treturn kept\n",
			class_name));

	const HashMap<StringName, Ref<FoundryScript>>::ConstIterator crate = base->get_subclasses().find(SNAME("Crate"));
	REQUIRE(crate != base->get_subclasses().end());

	Ref<FoundryScript> derived_script = compile_bytecode_test_source(vformat(
			"class Derived extends %s.Crate[int]:\n"
			"\tpass\n",
			class_name));
	const HashMap<StringName, Ref<FoundryScript>>::ConstIterator derived = derived_script->get_subclasses().find(SNAME("Derived"));
	REQUIRE(derived != derived_script->get_subclasses().end());

	const FSFunction *keep_before = predecode_test_function(crate->value, SNAME("keep"));
	const FSDataType *shape_before = only_specialized_tuple_shape(
			keep_before, derived->value->find_tuple_slot_specialization(Vector<ContainerType>()).ptr());
	REQUIRE(shape_before != nullptr);
	CHECK(shape_before->container_element_types[0].builtin_type == Variant::INT);
	CHECK(shape_before->container_element_types[1].builtin_type == Variant::INT);

	Callable::CallError call_error;
	const Variant derived_owner = derived->value->_new(nullptr, -1, call_error);
	REQUIRE(call_error.error == Callable::CallError::CALL_OK);
	Object *instance = derived_owner;
	REQUIRE(instance != nullptr);

	Array accepted_before;
	accepted_before.push_back(7);
	accepted_before.push_back(2);
	const Variant accepted_argument = accepted_before;
	const Variant *accepted_arguments[1] = { &accepted_argument };
	const Array stored_before = instance->callp(SNAME("keep"), accepted_arguments, 1, call_error);
	REQUIRE(call_error.error == Callable::CallError::CALL_OK);
	REQUIRE(stored_before.size() == 2);

	base->set_source_code(vformat(
			"class_name %s\n"
			"class Crate[T]:\n"
			"\tfunc keep(value) -> Variant:\n"
			"\t\tvar kept: (String, T) = value\n"
			"\t\treturn kept\n",
			class_name));
	REQUIRE(base->reload() == OK);

	const HashMap<StringName, Ref<FoundryScript>>::ConstIterator reloaded_crate = base->get_subclasses().find(SNAME("Crate"));
	REQUIRE(reloaded_crate != base->get_subclasses().end());
	const FSFunction *keep_after = predecode_test_function(reloaded_crate->value, SNAME("keep"));
	// The derived table is still the one interned against the old functions. Address reuse must
	// not make it answer for the new `keep`; a generation miss falls through to per-execution decode.
	CHECK(only_specialized_tuple_shape(
			keep_after, derived->value->find_tuple_slot_specialization(Vector<ContainerType>()).ptr()) == nullptr);

	ERR_PRINT_OFF;
	const Variant rejected = instance->callp(SNAME("keep"), accepted_arguments, 1, call_error);
	ERR_PRINT_ON;
	CHECK(rejected.get_type() == Variant::NIL);

	Array accepted_after;
	accepted_after.push_back("seven");
	accepted_after.push_back(2);
	const Variant after_argument = accepted_after;
	const Variant *after_arguments[1] = { &after_argument };
	const Array stored_after = instance->callp(SNAME("keep"), after_arguments, 1, call_error);
	REQUIRE(call_error.error == Callable::CallError::CALL_OK);
	REQUIRE(stored_after.size() == 2);
	CHECK(String(stored_after[0]) == "seven");
	CHECK(int(stored_after[1]) == 2);
}

} // namespace FSTests

#endif // TOOLS_ENABLED && DEBUG_ENABLED
