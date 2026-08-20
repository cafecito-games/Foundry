/**************************************************************************/
/*  test_union_membership.h                                               */
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

// Shared in-process compile and disassembly helpers. Every module test header is compiled into the
// same generated test translation unit, so this include does not duplicate test registrations.
#include "test_tuple_lowering.h"

#include "modules/foundry_script/fs_function.h"

#include "tests/test_macros.h"

namespace FSTests {

TEST_CASE("[FoundryScript][UnionMembership] An unproven union store carries the alternative set") {
	// A union slot has no carrier of its own, so the only place the alternatives survive compilation is
	// this instruction's descriptor operand. A store whose membership the static types never proved
	// gets one; a store from a value that already satisfies an alternative keeps the plain store.
	Ref<FoundryScript> script = compile_bytecode_test_source(
			"func unproven(value) -> void:\n"
			"\tvar kept: int | String = value\n"
			"\tprint(kept)\n"
			"\n"
			"func proven(value: int) -> void:\n"
			"\tvar kept: int | String = value\n"
			"\tprint(kept)\n"
			"\n"
			"func returned(value) -> int | String:\n"
			"\treturn value\n");

	SUBCASE("A gradual initializer is checked against the alternatives") {
		const Vector<String> lines = disassemble_test_function(script, SNAME("unproven"));
		const Vector<String> store_lines = filter_disassembly_lines(lines, "assign typed union");
		REQUIRE(store_lines.size() == 1);
		CAPTURE(store_lines[0]);
		CHECK(store_lines[0].contains("\"is_union\": true"));
	}

	SUBCASE("A proven initializer keeps the ordinary store") {
		const Vector<String> lines = disassemble_test_function(script, SNAME("proven"));
		CHECK(filter_disassembly_lines(lines, "assign typed union").is_empty());
	}

	SUBCASE("A union return slot checks the value it hands back") {
		const Vector<String> lines = disassemble_test_function(script, SNAME("returned"));
		const Vector<String> store_lines = filter_disassembly_lines(lines, "assign typed union");
		REQUIRE(store_lines.size() == 1);
		CAPTURE(store_lines[0]);
		CHECK(store_lines[0].contains("\"is_union\": true"));
	}
}

static FSDataType first_parameter_type(const Ref<FoundryScript> &p_script, const StringName &p_function_name) {
	const HashMap<StringName, FSFunction *> &functions = p_script->get_member_functions();
	const HashMap<StringName, FSFunction *>::ConstIterator function = functions.find(p_function_name);
	REQUIRE(function != functions.end());
	REQUIRE(function->value != nullptr);
	REQUIRE(function->value->get_argument_count() > 0);
	return function->value->get_argument_type(0);
}

TEST_CASE("[FoundryScript][UnionMembership] A union parameter is checked like any other typed parameter") {
	Ref<FoundryScript> script = compile_bytecode_test_source(
			"func take(value: int | String) -> String:\n"
			"\treturn str(value)\n");

	const FSDataType parameter_type = first_parameter_type(script, SNAME("take"));
	CHECK(parameter_type.kind == FSDataType::UNION);
	CHECK(parameter_type.has_type());
	REQUIRE(parameter_type.union_alternatives.size() == 2);
	// The alternatives reach the runtime in the analyzer's canonical order, which is also the order the
	// diagnostic names them in.
	CHECK(parameter_type.get_source_type_name() == "String | int");

	CHECK(parameter_type.is_type(Variant(5)));
	CHECK(parameter_type.is_type(Variant("five")));
	CHECK_FALSE(parameter_type.is_type(Variant(5.0)));
	CHECK_FALSE(parameter_type.is_type(Variant(Array())));
}

TEST_CASE("[FoundryScript][UnionMembership] A union is an unconstrained node outside its own check") {
	// The alternatives are not the parts of one value, so they must not reach the consumers that read
	// container elements as such: specialization evidence, binding projection, and typed-container
	// metadata all treat an element list as the contents of one value. A union that carried its
	// alternatives there described a typed container nothing could satisfy, and rejected a
	// trait-conformant value whose conformance named the very same union.
	Ref<FoundryScript> script = compile_bytecode_test_source(
			"func take(value: Array[int] | Array[String]) -> void:\n"
			"\tprint(value)\n");

	const FSDataType parameter_type = first_parameter_type(script, SNAME("take"));
	REQUIRE(parameter_type.kind == FSDataType::UNION);
	CHECK(parameter_type.union_alternatives.size() == 2);
	CHECK(parameter_type.container_element_types.is_empty());

	const ContainerType projected = parameter_type.to_container_type();
	CHECK(projected.builtin_type == Variant::NIL);
	CHECK(projected.element_types.is_empty());
}

TEST_CASE("[FoundryScript][UnionMembership] A union accepts the untyped container a plain typed slot accepts") {
	// A non-union `Array[int]` parameter retypes an untyped literal at its binding, so a union holding
	// that same alternative has to accept the identical value or the union slot is stricter than the
	// alternative standing alone.
	Ref<FoundryScript> script = compile_bytecode_test_source(
			"func take(value: Array[int] | Array[String]) -> void:\n"
			"\tprint(value)\n");

	const FSDataType parameter_type = first_parameter_type(script, SNAME("take"));

	Array untyped_integers;
	untyped_integers.push_back(1);
	Variant accepted;
	REQUIRE(fs_union_accepts(parameter_type, untyped_integers, accepted));
	CHECK(Array(accepted).is_typed());
	CHECK(parameter_type.is_type(accepted));

	Array untyped_strings;
	untyped_strings.push_back("one");
	REQUIRE(fs_union_accepts(parameter_type, untyped_strings, accepted));
	CHECK(parameter_type.is_type(accepted));

	// Contents no alternative describes stay rejected, and nothing is built on the way there.
	Array untyped_floats;
	untyped_floats.push_back(1.5);
	CHECK_FALSE(fs_union_accepts(parameter_type, untyped_floats, accepted));
}

} // namespace FSTests

#endif // TOOLS_ENABLED && DEBUG_ENABLED
