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
			disassemble_tuple_test_function(script, SNAME("keep")), "assign typed tuple");
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
	// be true for the uninteresting reason that there is nothing to predecode.
	auto check_dependent = [](const Ref<FoundryScript> &p_owner, const StringName &p_function_name) {
		CAPTURE(String(p_function_name));
		CHECK_FALSE(filter_disassembly_lines(
				disassemble_tuple_test_function(p_owner, p_function_name), "assign typed tuple")
						.is_empty());
		CHECK(predecode_test_function(p_owner, p_function_name)->get_predecoded_tuple_shape_count() == 0);
	};
	check_dependent(crate->value, SNAME("direct_parameter"));
	check_dependent(crate->value, SNAME("nested_parameter"));
	check_dependent(anchor->value, SNAME("direct_self"));
	check_dependent(anchor->value, SNAME("nested_self"));

	// A concrete slot in the same generic class is still independent: the exclusion is per descriptor,
	// not per declaring class.
	CHECK(predecode_test_function(crate->value, SNAME("concrete"))->get_predecoded_tuple_shape_count() == 1);
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

} // namespace FSTests

#endif // TOOLS_ENABLED && DEBUG_ENABLED
