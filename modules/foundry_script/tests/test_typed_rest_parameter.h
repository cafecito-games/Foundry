/**************************************************************************/
/*  test_typed_rest_parameter.h                                           */
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

// Runtime behavior of typed rest ("...") parameters. The shared bytecode fixture helper compiles a
// script in-process, which is the shortest path to calling a compiled variadic function directly
// through `Object::callp` and observing what the call prologue built.
#include "test_bytecode_serialization.h"

#include "tests/test_macros.h"

namespace FSTests {

TEST_CASE("[Modules][FoundryScript][TypedRestParameter] A rejected rest argument releases the fixed arguments") {
	// The call prologue constructs the whole stack before it validates the rest tail, so a rejected
	// surplus argument must unwind it. Otherwise every reference-counted value already placed in a
	// fixed argument slot is retained for the lifetime of the process.
	const Ref<FoundryScript> script = compile_bytecode_test_source(
			"func take(first: RefCounted, ...values: Array[int]) -> int:\n"
			"\tprint(first)\n"
			"\treturn values.size()\n");

	Callable::CallError call_error;
	Variant owner = script->_new(nullptr, -1, call_error);
	REQUIRE(call_error.error == Callable::CallError::CALL_OK);
	Object *instance = owner;
	REQUIRE(instance != nullptr);

	Ref<RefCounted> tracked;
	tracked.instantiate();

	// The baseline is taken with the argument Variant already holding its reference, so the only
	// change the assertions can observe is one the call itself failed to release.
	const Variant first = tracked;
	const int reference_count_before = tracked->get_reference_count();

	const Variant rejected = "bad";
	const Variant *arguments[2] = { &first, &rejected };
	instance->callp(SNAME("take"), arguments, 2, call_error);
	CHECK(call_error.error == Callable::CallError::CALL_ERROR_INVALID_ARGUMENT);
	CHECK(call_error.argument == 1);
	CHECK(tracked->get_reference_count() == reference_count_before);

	// The same call with a valid tail still runs, so the unwinding did not break the accepted path.
	const Variant accepted = 7;
	const Variant *valid_arguments[2] = { &first, &accepted };
	const Variant result = instance->callp(SNAME("take"), valid_arguments, 2, call_error);
	CHECK(call_error.error == Callable::CallError::CALL_OK);
	CHECK(int(result) == 1);
	CHECK(tracked->get_reference_count() == reference_count_before);
}

} // namespace FSTests

#endif // TOOLS_ENABLED
