/**************************************************************************/
/*  test_checked_numeric_runtime.h                                        */
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

#include "../foundry_script.h"
#include "../fs_analyzer.h"
#include "../fs_compiler.h"
#include "../fs_parser.h"

#include "tests/test_macros.h"

// Runtime coverage for the checked numeric opcodes. The value tables themselves live in the script
// fixture `runtime/features/checked_integer_operations.fs`, which the integration runner executes
// both from source and from exported bytecode against one expected output. What a fixture cannot
// observe is state *after* a runtime error, because the error ends the call: these tests read the
// destination back from the instance to prove a failed operation committed nothing.

namespace FSTests {

struct ScopedCheckedNumericLanguage {
	ScopedCheckedNumericLanguage() {
		if (!FSLanguage::get_singleton()->has_any_global_constant(SNAME("RefCounted"))) {
			FSLanguage::get_singleton()->init();
		}
	}
};

static Ref<FoundryScript> compile_checked_numeric_source(const String &p_source) {
	static int unique_index = 0;
	const String path = vformat("user://test_checked_numeric_runtime_%d.fs", unique_index++);

	Ref<FoundryScript> script;
	script.instantiate();
	script->set_path(path);
	script->set_source_code(p_source);

	FSParser parser;
	Error error = parser.parse(p_source, script->get_path(), false);
	REQUIRE(error == OK);

	FSAnalyzer analyzer(&parser);
	error = analyzer.analyze();
	REQUIRE(error == OK);

	FSCompiler compiler;
	error = compiler.compile(&parser, script.ptr(), false);
	REQUIRE(error == OK);

	error = script->reload();
	REQUIRE(error == OK);

	return script;
}

static Variant instantiate_checked_numeric_script(const Ref<FoundryScript> &p_script) {
	Callable::CallError error;
	const Variant instance = const_cast<FoundryScript *>(p_script.ptr())->_new(nullptr, 0, error);
	REQUIRE(error.error == Callable::CallError::CALL_OK);
	return instance;
}

// Calls a method that is expected to end in a runtime error, with the error diagnostic silenced.
static void call_expecting_runtime_error(Object *p_object, const StringName &p_method) {
	Callable::CallError error;
	ERR_PRINT_OFF;
	p_object->callp(p_method, nullptr, 0, error);
	ERR_PRINT_ON;
	CHECK(error.error == Callable::CallError::CALL_OK);
}

TEST_CASE("[Modules][FoundryScript][CheckedNumeric] A failed compound assignment leaves its destination alone") {
	ScopedCheckedNumericLanguage language;

	const Ref<FoundryScript> script = compile_checked_numeric_source(
			"var signed_value: long = 9223372036854775807\n"
			"var unsigned_value: uint = 4294967295U\n"
			"var signed_floor: long = -9223372036854775808\n"
			"\n"
			"func overflow_signed() -> void:\n"
			"\tsigned_value += 1\n"
			"\n"
			"func overflow_unsigned() -> void:\n"
			"\tunsigned_value += 1U\n"
			"\n"
			"func underflow_signed() -> void:\n"
			"\tsigned_floor -= 1\n"
			"\n"
			"func succeed_unsigned() -> void:\n"
			"\tunsigned_value -= 1U\n");

	const Variant instance = instantiate_checked_numeric_script(script);
	Object *object = instance;
	REQUIRE(object != nullptr);

	// The operation computes into a temporary, so the store never happens and the member still holds
	// the value it had before the call.
	call_expecting_runtime_error(object, SNAME("overflow_signed"));
	CHECK(object->get("signed_value") == Variant(int64_t(INT64_MAX)));

	call_expecting_runtime_error(object, SNAME("underflow_signed"));
	CHECK(object->get("signed_floor") == Variant(int64_t(INT64_MIN)));

	call_expecting_runtime_error(object, SNAME("overflow_unsigned"));
	const Variant unsigned_after_failure = object->get("unsigned_value");
	CHECK(unsigned_after_failure.get_type() == Variant::UINT);
	CHECK(unsigned_after_failure == Variant(uint64_t(UINT32_MAX)));

	// The same destination still accepts a result that does fit, so the failure did not leave the
	// member in an unusable state.
	Callable::CallError error;
	object->callp(SNAME("succeed_unsigned"), nullptr, 0, error);
	REQUIRE(error.error == Callable::CallError::CALL_OK);
	const Variant unsigned_after_success = object->get("unsigned_value");
	CHECK(unsigned_after_success.get_type() == Variant::UINT);
	CHECK(unsigned_after_success == Variant(uint64_t(UINT32_MAX) - 1));
}

TEST_CASE("[Modules][FoundryScript][CheckedNumeric] An unsigned result reaches its destination") {
	ScopedCheckedNumericLanguage language;

	// A `uint` destination used to receive nothing at all: the type-adjust step had no unsigned case,
	// so a program that analyzed cleanly stored null.
	const Ref<FoundryScript> script = compile_checked_numeric_source(
			"var total: uint = 0U\n"
			"var product: ulong = 0UL\n"
			"var complement: uint = 0U\n"
			"\n"
			"func compute() -> void:\n"
			"\tvar left: uint = 2000000000U\n"
			"\tvar right: uint = 294967295U\n"
			"\ttotal = left + right\n"
			"\tvar wide: ulong = 4294967296UL\n"
			"\tproduct = wide * 2UL\n"
			"\tcomplement = ~left\n");

	const Variant instance = instantiate_checked_numeric_script(script);
	Object *object = instance;
	REQUIRE(object != nullptr);

	Callable::CallError error;
	object->callp(SNAME("compute"), nullptr, 0, error);
	REQUIRE(error.error == Callable::CallError::CALL_OK);

	const Variant total = object->get("total");
	CHECK(total.get_type() == Variant::UINT);
	CHECK(total == Variant(uint64_t(2294967295)));

	const Variant product = object->get("product");
	CHECK(product.get_type() == Variant::UINT);
	CHECK(product == Variant(uint64_t(8589934592ULL)));

	// Complement is taken at the declared 32-bit width rather than at the carrier's 64 bits.
	const Variant complement = object->get("complement");
	CHECK(complement.get_type() == Variant::UINT);
	CHECK(complement == Variant(uint64_t(~uint32_t(2000000000))));
}

TEST_CASE("[Modules][FoundryScript][CheckedNumeric] A failed checked cast leaves its destination alone") {
	ScopedCheckedNumericLanguage language;

	const Ref<FoundryScript> script = compile_checked_numeric_source(
			"var converted: uint = 7U\n"
			"\n"
			"func convert_negative() -> void:\n"
			"\tvar source: long = -1\n"
			"\tconverted = source as uint\n"
			"\n"
			"func convert_valid() -> void:\n"
			"\tvar source: long = 9\n"
			"\tconverted = source as uint\n");

	const Variant instance = instantiate_checked_numeric_script(script);
	Object *object = instance;
	REQUIRE(object != nullptr);

	call_expecting_runtime_error(object, SNAME("convert_negative"));
	const Variant after_failure = object->get("converted");
	CHECK(after_failure.get_type() == Variant::UINT);
	CHECK(after_failure == Variant(uint64_t(7)));

	Callable::CallError error;
	object->callp(SNAME("convert_valid"), nullptr, 0, error);
	REQUIRE(error.error == Callable::CallError::CALL_OK);
	CHECK(object->get("converted") == Variant(uint64_t(9)));
}

TEST_CASE("[Modules][FoundryScript][CheckedNumeric] Nullable integer arithmetic keeps its declared width") {
	ScopedCheckedNumericLanguage language;

	const Ref<FoundryScript> script = compile_checked_numeric_source(
			"func overflow_nullable():\n"
			"\tvar left: uint? = 4294967295U\n"
			"\tvar right: uint? = 1U\n"
			"\treturn left + right\n");

	const Variant instance = instantiate_checked_numeric_script(script);
	Object *object = instance;
	REQUIRE(object != nullptr);

	Callable::CallError error;
	ERR_PRINT_OFF;
	const Variant result = object->callp(SNAME("overflow_nullable"), nullptr, 0, error);
	ERR_PRINT_ON;
	REQUIRE(error.error == Callable::CallError::CALL_OK);
	CHECK(result.get_type() == Variant::NIL);
}

TEST_CASE("[Modules][FoundryScript][CheckedNumeric] Dynamic shifts require matching integer carriers") {
	ScopedCheckedNumericLanguage language;

	const Ref<FoundryScript> script = compile_checked_numeric_source(
			"func shift(left, right):\n"
			"\treturn left << right\n");

	const Variant instance = instantiate_checked_numeric_script(script);
	Object *object = instance;
	REQUIRE(object != nullptr);

	const Variant unsigned_one = uint64_t(1);
	const Variant signed_one = int64_t(1);
	const Variant *mixed_arguments[] = { &unsigned_one, &signed_one };
	Callable::CallError error;
	ERR_PRINT_OFF;
	const Variant mixed_result = object->callp(SNAME("shift"), mixed_arguments, 2, error);
	ERR_PRINT_ON;
	REQUIRE(error.error == Callable::CallError::CALL_OK);
	CHECK(mixed_result.get_type() == Variant::NIL);

	const Variant *matching_arguments[] = { &unsigned_one, &unsigned_one };
	const Variant matching_result = object->callp(SNAME("shift"), matching_arguments, 2, error);
	REQUIRE(error.error == Callable::CallError::CALL_OK);
	CHECK(matching_result == Variant(uint64_t(2)));
}

} // namespace FSTests
