/**************************************************************************/
/*  test_typed_store_width.h                                              */
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

// Shared in-process compile helper, and the disassembly helpers used to read back the width operand
// the typed store and typed return carry. Every module test header is compiled into the same
// generated test translation unit, so these includes do not duplicate test registrations.
#include "test_bytecode_serialization.h"
#include "test_tuple_lowering.h"

#include "modules/foundry_script/fs_function.h"

#include "tests/test_macros.h"

namespace FSTests {

TEST_CASE("[FoundryScript][TypedStoreWidth] A gradual store and return carry the declared width") {
	// The width operand is the last on both instructions, so a decoder that miscounts its length
	// desynchronizes the rest of the stream. Disassembling the whole function is the cheapest way to
	// observe that: every instruction after the store still decodes, and the walk reaches the closing
	// marker instead of trailing off into misread operands.
	Ref<FoundryScript> script = compile_bytecode_test_source(
			"func store(value) -> void:\n"
			"\tvar narrow: int = value\n"
			"\tvar text: String = value\n"
			"\tprint(narrow, text)\n"
			"\n"
			"func produce(value) -> uint:\n"
			"\treturn value\n");

	SUBCASE("A width-constrained destination shows its width") {
		const Vector<String> lines = disassemble_test_function(script, SNAME("store"));
		const Vector<String> store_lines = filter_disassembly_lines(lines, "assign typed builtin");
		REQUIRE(store_lines.size() == 2);
		CHECK(store_lines[0].contains("assign typed builtin (int int32)"));
		// A non-integer destination has no width to constrain, so nothing is appended.
		CHECK(store_lines[1].contains("assign typed builtin (String)"));

		REQUIRE(!lines.is_empty());
		CHECK(lines[lines.size() - 1].contains("== END =="));
	}

	SUBCASE("A width-constrained return shows its width") {
		const Vector<String> lines = disassemble_test_function(script, SNAME("produce"));
		const Vector<String> return_lines = filter_disassembly_lines(lines, "return typed builtin");
		REQUIRE(return_lines.size() == 1);
		CHECK(return_lines[0].contains("return typed builtin (uint uint32)"));

		REQUIRE(!lines.is_empty());
		CHECK(lines[lines.size() - 1].contains("== END =="));
	}
}

} // namespace FSTests

#endif // TOOLS_ENABLED && DEBUG_ENABLED
