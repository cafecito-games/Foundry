/**************************************************************************/
/*  test_enum_payload_reification.h                                       */
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

// Shared in-process compile helper and the disassembly helpers used to read an emitted store's type
// operand. Every module test header is compiled into the same generated test translation unit, so
// these includes do not duplicate test registrations.
#include "test_tuple_lowering.h"

#include "tests/test_macros.h"

namespace FSTests {

// The three enum-case construction spellings whose payload lowering the reified check distinguishes:
// a `Type[T]` handle, the concrete class, and a payload field typed by a method parameter the handle
// does not reify.
static Ref<FoundryScript> compile_enum_payload_reification_source() {
	return compile_bytecode_test_source(
			"class Receiver:\n"
			"\tenum Message:\n"
			"\t\tDetach\n"
			"\t\tAttach(index: int, owner: Self)\n"
			"\tenum Box[E]:\n"
			"\t\tEmpty\n"
			"\t\tFull(value: E)\n"
			"\n"
			"class Sub:\n"
			"\textends Receiver\n"
			"\n"
			"func via_handle[T: Receiver](handle: Type[T], value: Variant) -> Variant:\n"
			"\treturn handle.Message.Attach(1, value)\n"
			"\n"
			"func via_concrete(value: Variant) -> Variant:\n"
			"\treturn Sub.Message.Attach(1, value)\n"
			"\n"
			"func via_unrelated_parameter[T: Receiver, U](handle: Type[T], value: U) -> Variant:\n"
			"\treturn handle.Box[U].Full(value)\n");
}

TEST_CASE("[Modules][FoundryScript][EnumPayload] A handle-spelled payload field checks against the handle register") {
	const Ref<FoundryScript> script = compile_enum_payload_reification_source();

	const Vector<String> handle_stores = filter_disassembly_lines(
			disassemble_test_function(script, SNAME("via_handle")), "assign typed script");
	REQUIRE(handle_stores.size() == 1);
	// The represented type is a method type parameter, which erases to Variant and so is nameable by
	// no constant: the check reads the class off the handle the frame was given.
	CHECK(handle_stores[0].contains("assign typed script (stack("));

	const Vector<String> concrete_stores = filter_disassembly_lines(
			disassemble_test_function(script, SNAME("via_concrete")), "assign typed script");
	REQUIRE(concrete_stores.size() == 1);
	// The concrete spelling names a class at compile time, so its operand still renders as that class.
	CHECK(concrete_stores[0].contains("assign typed script (Sub)"));
}

TEST_CASE("[Modules][FoundryScript][EnumPayload] A payload field typed by an unreified parameter stays erased") {
	const Ref<FoundryScript> script = compile_enum_payload_reification_source();

	// `U` is not what the receiver handle reifies, so the frame holds no evidence about it and the
	// argument keeps the erasure every other method-generic slot has.
	CHECK(filter_disassembly_lines(
			disassemble_test_function(script, SNAME("via_unrelated_parameter")), "assign typed script")
					.is_empty());
}

} // namespace FSTests

#endif // TOOLS_ENABLED && DEBUG_ENABLED
