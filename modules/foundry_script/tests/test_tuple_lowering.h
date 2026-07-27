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

} // namespace FSTests

#endif // defined(TOOLS_ENABLED) && defined(DEBUG_ENABLED)
