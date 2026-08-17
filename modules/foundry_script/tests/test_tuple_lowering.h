/**************************************************************************/
/*  test_tuple_lowering.h                                                 */
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

// Shared in-process compile helper. Every module test header is compiled into the same generated
// test translation unit, so this include does not duplicate test registrations.
#include "test_bytecode_serialization.h"

#include "modules/foundry_script/fs_function.h"

#include "tests/test_macros.h"

namespace FSTests {

// Collects the lines `FSFunction::disassemble()` prints so the emitted instruction text can be
// asserted on directly.
struct DisassemblyCapture {
	Vector<String> lines;
	PrintHandlerList handler;

	static void capture(void *p_userdata, const String &p_message, bool p_error, bool p_rich) {
		if (p_error) {
			return;
		}
		static_cast<DisassemblyCapture *>(p_userdata)->lines.push_back(p_message);
	}

	DisassemblyCapture() {
		handler.printfunc = capture;
		handler.userdata = this;
		add_print_handler(&handler);
	}

	~DisassemblyCapture() {
		remove_print_handler(&handler);
	}
};

static Vector<String> disassemble_tuple_test_function(const Ref<FoundryScript> &p_script, const StringName &p_function_name) {
	const HashMap<StringName, FSFunction *> &functions = p_script->get_member_functions();
	const HashMap<StringName, FSFunction *>::ConstIterator function = functions.find(p_function_name);
	REQUIRE(function != functions.end());
	REQUIRE(function->value != nullptr);

	DisassemblyCapture capture;
	function->value->disassemble(Vector<String>());
	return capture.lines;
}

static Vector<String> filter_disassembly_lines(const Vector<String> &p_lines, const String &p_instruction) {
	Vector<String> matches;
	for (const String &line : p_lines) {
		// Disassembly lines are formatted as " <ip>: <instruction> ...".
		if (line.get_slice(": ", 1).begins_with(p_instruction)) {
			matches.push_back(line);
		}
	}
	return matches;
}

TEST_CASE("[FoundryScript][TupleLowering] A tuple slot store disassembles with its declared shape") {
	// A tuple slot's address type is an erased, untyped Array, so the only place the declared shape
	// survives compilation is this instruction's descriptor operand. Both a local's initializer and the
	// return get one; a slot with no declared tuple type keeps the plain store.
	Ref<FoundryScript> script = compile_bytecode_test_source(
			"func keep(value) -> (int, String):\n"
			"\tvar kept: (int, String) = value\n"
			"\treturn kept\n"
			"\n"
			"func passthrough(value) -> Array:\n"
			"\tvar kept: Array = value\n"
			"\treturn kept\n");

	SUBCASE("Declared tuple slots carry the shape") {
		const Vector<String> lines = disassemble_tuple_test_function(script, SNAME("keep"));
		const Vector<String> store_lines = filter_disassembly_lines(lines, "assign typed tuple");
		// One for the local's initializer, one for the return.
		REQUIRE(store_lines.size() == 2);
		for (const String &store_line : store_lines) {
			CAPTURE(store_line);
			CHECK(store_line.contains("assign typed tuple of arity 2"));
			CHECK(store_line.contains("\"is_tuple\": true"));
		}
	}

	SUBCASE("A plain Array slot keeps the ordinary store") {
		const Vector<String> lines = disassemble_tuple_test_function(script, SNAME("passthrough"));
		CHECK(filter_disassembly_lines(lines, "assign typed tuple").is_empty());
	}
}

TEST_CASE("[FoundryScript][TupleLowering] Tuple construction disassembles as a dedicated instruction") {
	Ref<FoundryScript> script = compile_bytecode_test_source(
			"tuple Vec2(x: int, y: int)\n"
			"\n"
			"func build_unnamed(first: int, second: int) -> (int, int):\n"
			"\treturn (first, second)\n"
			"\n"
			"func build_named(first: int, second: int) -> Vec2:\n"
			"\treturn Vec2(first, second)\n"
			"\n"
			"func build_array(first: int, second: int) -> Array:\n"
			"\treturn [first, second]\n");

	SUBCASE("Unnamed tuple literal") {
		const Vector<String> lines = disassemble_tuple_test_function(script, SNAME("build_unnamed"));
		const Vector<String> tuple_lines = filter_disassembly_lines(lines, "make_tuple");
		REQUIRE(tuple_lines.size() == 1);
		CAPTURE(tuple_lines[0]);
		CHECK(tuple_lines[0].get_slice(": ", 1) == "make_tuple stack(5) = (stack(3), stack(4))");
		CHECK(filter_disassembly_lines(lines, "make_array").is_empty());
	}

	SUBCASE("Named tuple construction") {
		const Vector<String> lines = disassemble_tuple_test_function(script, SNAME("build_named"));
		const Vector<String> tuple_lines = filter_disassembly_lines(lines, "make_tuple");
		REQUIRE(tuple_lines.size() == 1);
		CAPTURE(tuple_lines[0]);
		// The arguments are the temporaries the declared field types are converted into.
		CHECK(tuple_lines[0].get_slice(": ", 1) == "make_tuple stack(5) = (stack(6), stack(7))");
		CHECK(filter_disassembly_lines(lines, "make_array").is_empty());
	}

	SUBCASE("Array literals keep the array instruction") {
		const Vector<String> lines = disassemble_tuple_test_function(script, SNAME("build_array"));
		CHECK(filter_disassembly_lines(lines, "make_tuple").is_empty());
		CHECK(filter_disassembly_lines(lines, "make_array").size() == 1);
	}
}

TEST_CASE("[FoundryScript][TupleLowering] A container element of a tuple literal is built typed") {
	// A tuple store never converts, so the element has to be constructed already typed. The declared
	// element type reaches the element's own construction instruction and nothing else: the tuple slot
	// itself still stores through the erased Array carrier.
	Ref<FoundryScript> script = compile_bytecode_test_source(
			"func declared() -> void:\n"
			"\tvar slot: (int, Array[int]) = (1, [])\n"
			"\tprint(slot)\n"
			"\n"
			"func undeclared() -> void:\n"
			"\tvar slot := (1, [])\n"
			"\tprint(slot)\n");

	SUBCASE("The declared element type selects the typed construction") {
		const Vector<String> lines = disassemble_tuple_test_function(script, SNAME("declared"));
		const Vector<String> typed_array_lines = filter_disassembly_lines(lines, "make_typed_array");
		REQUIRE(typed_array_lines.size() == 1);
		CAPTURE(typed_array_lines[0]);
		CHECK(typed_array_lines[0].contains("make_typed_array (int)"));
		CHECK(filter_disassembly_lines(lines, "make_array").is_empty());

		// The slot store keeps the erased carrier: this fix types the literal, it does not give the
		// tuple slot a typed-container form.
		const Vector<String> store_lines = filter_disassembly_lines(lines, "assign typed tuple");
		REQUIRE(store_lines.size() == 1);
		CHECK(store_lines[0].contains("assign typed tuple of arity 2"));
	}

	SUBCASE("An inferred declaration supplies no element type, so the literal is unchanged") {
		const Vector<String> lines = disassemble_tuple_test_function(script, SNAME("undeclared"));
		CHECK(filter_disassembly_lines(lines, "make_typed_array").is_empty());
		CHECK(filter_disassembly_lines(lines, "make_array").size() == 1);
	}
}

static FSFunction *tuple_test_function(const Ref<FoundryScript> &p_script, const StringName &p_function_name) {
	const HashMap<StringName, FSFunction *> &functions = p_script->get_member_functions();
	const HashMap<StringName, FSFunction *>::ConstIterator function = functions.find(p_function_name);
	REQUIRE(function != functions.end());
	REQUIRE(function->value != nullptr);
	return function->value;
}

TEST_CASE("[FoundryScript][TupleLowering] A tuple parameter compiles its shape into the call boundary") {
	// The shape has to reach `FSFunction::argument_types` and nowhere else: a parameter's compiled type
	// is also its codegen address type, and no `write_assign*` has a `TUPLE` case.
	Ref<FoundryScript> script = compile_bytecode_test_source(
			"func take(pair: (int, String), plain: Array) -> void:\n"
			"\tprint(pair, plain)\n"
			"\n"
			"func rest_of_tuples(...values: Array[(int, String)]) -> void:\n"
			"\tprint(values)\n"
			"\n"
			"func rest_of_ints(...values: Array[int]) -> void:\n"
			"\tprint(values)\n");

	SUBCASE("A fixed tuple parameter validates against the declared shape") {
		FSFunction *function = tuple_test_function(script, SNAME("take"));
		REQUIRE(function->get_argument_count() == 2);

		const FSDataType &tuple_argument = function->get_argument_type(0);
		CHECK(tuple_argument.kind == FSDataType::TUPLE);
		CHECK(tuple_argument.builtin_type == Variant::ARRAY);
		REQUIRE(tuple_argument.container_element_types.size() == 2);
		CHECK(tuple_argument.container_element_types[0].builtin_type == Variant::INT);
		CHECK(tuple_argument.container_element_types[1].builtin_type == Variant::STRING);

		// A neighboring non-tuple parameter is untouched.
		const FSDataType &array_argument = function->get_argument_type(1);
		CHECK(array_argument.kind == FSDataType::BUILTIN);
		CHECK(array_argument.builtin_type == Variant::ARRAY);
		CHECK(array_argument.container_element_types.is_empty());
	}

	SUBCASE("Binding a parameter emits no store, so the slot type stays erased") {
		// The shape is enforced by the boundary, not by an instruction: a `TUPLE`-kind address type
		// would change how the body is lowered, and this is what proves it was not handed one.
		const Vector<String> lines = disassemble_tuple_test_function(script, SNAME("take"));
		CHECK(filter_disassembly_lines(lines, "assign typed tuple").is_empty());
	}

	SUBCASE("A tuple rest element validates against the declared shape") {
		FSFunction *function = tuple_test_function(script, SNAME("rest_of_tuples"));
		const FSDataType &rest_type = function->get_rest_parameter_type();
		// The collected array itself stays a bare Array: a tuple has no typed-container form, so
		// typing the collection from this element would reject every legal element.
		CHECK(rest_type.kind == FSDataType::BUILTIN);
		CHECK(rest_type.builtin_type == Variant::ARRAY);
		REQUIRE(rest_type.container_element_types.size() == 1);
		CHECK(rest_type.container_element_types[0].kind == FSDataType::TUPLE);
		CHECK(rest_type.container_element_types[0].container_element_types.size() == 2);
	}

	SUBCASE("A non-tuple rest element keeps its typed-container element") {
		FSFunction *function = tuple_test_function(script, SNAME("rest_of_ints"));
		const FSDataType &rest_type = function->get_rest_parameter_type();
		REQUIRE(rest_type.container_element_types.size() == 1);
		CHECK(rest_type.container_element_types[0].kind == FSDataType::BUILTIN);
		CHECK(rest_type.container_element_types[0].builtin_type == Variant::INT);
	}
}

} // namespace FSTests

#endif // defined(TOOLS_ENABLED) && defined(DEBUG_ENABLED)
