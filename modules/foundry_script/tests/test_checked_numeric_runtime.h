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
#include "fs_test_language_lifecycle.h"

#include "core/error/error_macros.h"

#include "tests/test_macros.h"

// Runtime coverage for the checked numeric opcodes. The value tables themselves live in the script
// fixture `runtime/features/checked_integer_operations.fs`, which the integration runner executes
// both from source and from exported bytecode against one expected output. What a fixture cannot
// observe is state *after* a runtime error, because the error ends the call: these tests read the
// destination back from the instance to prove a failed operation committed nothing.

namespace FSTests {

struct ScopedCheckedNumericLanguage {
	ScopedCheckedNumericLanguage() {
		FSTests::ensure_fs_language_initialized();
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

// Captures every engine error raised during a call so a runtime error can be asserted on by the
// range it names rather than by the mere fact that the call failed.
struct FlowNarrowedWidthErrorRecorder {
	FlowNarrowedWidthErrorRecorder() {
		handler.errfunc = _record;
		handler.userdata = this;
		add_error_handler(&handler);
	}

	~FlowNarrowedWidthErrorRecorder() {
		remove_error_handler(&handler);
	}

	static void _record(void *p_self, const char *p_function, const char *p_file, int p_line, const char *p_error, const char *p_explanation, bool p_editor_notify, ErrorHandlerType p_type) {
		FlowNarrowedWidthErrorRecorder *self = static_cast<FlowNarrowedWidthErrorRecorder *>(p_self);
		self->messages += String::utf8(p_explanation != nullptr && p_explanation[0] != '\0' ? p_explanation : p_error) + "\n";
	}

	ErrorHandlerList handler;
	String messages;
};

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

// `int(source)` compiles to `Variant::construct()` against `VariantConstructorIntFromUInt`, whose
// range precondition the compiler cannot prove from the `ulong` carrier alone: the value is only
// known at run time. That constructor's validated fast path only asserts the precondition instead of
// checking it, so an out-of-range value used to trip a `DEV_ASSERT` in dev builds and silently keep
// whatever bit pattern the carrier held in release. The constructor call is routed through the same
// checked-numeric machinery `as` casts use, so the failure is a stable runtime error in every build.
//
// `int(...)` also declares the narrower 32-bit width, not its 64-bit `INT` carrier's full range (see
// the builtin-constructor typing in `FSAnalyzer::reduce_call`), so a value above `INT32_MAX` and
// at or below `INT64_MAX` must still be refused even though it fits the carrier: checking only the
// carrier's widest range here would silently accept a value `int` cannot hold, exactly the failure
// mode a plain `as int` cast on the same value already rejects.
TEST_CASE("[Modules][FoundryScript][CheckedNumeric] A signedness-crossing constructor call is checked") {
	ScopedCheckedNumericLanguage language;

	const Ref<FoundryScript> script = compile_checked_numeric_source(
			"func cast_to_int(source: ulong):\n"
			"\treturn int(source)\n");

	const Variant instance = instantiate_checked_numeric_script(script);
	Object *object = instance;
	REQUIRE(object != nullptr);

	const Variant out_of_range = uint64_t(UINT64_MAX);
	const Variant *out_of_range_arguments[] = { &out_of_range };
	Callable::CallError error;
	ERR_PRINT_OFF;
	const Variant failed_result = object->callp(SNAME("cast_to_int"), out_of_range_arguments, 1, error);
	ERR_PRINT_ON;
	REQUIRE(error.error == Callable::CallError::CALL_OK);
	CHECK(failed_result.get_type() == Variant::NIL);

	// This value fits `int`'s 64-bit carrier but not its declared 32-bit width, and used to be
	// accepted when the checked cast validated at the carrier's widest range instead of `int`'s
	// declared width.
	const Variant beyond_declared_width = uint64_t(int64_t(INT32_MAX) + 1);
	const Variant *beyond_declared_width_arguments[] = { &beyond_declared_width };
	ERR_PRINT_OFF;
	const Variant beyond_declared_width_result = object->callp(SNAME("cast_to_int"), beyond_declared_width_arguments, 1, error);
	ERR_PRINT_ON;
	REQUIRE(error.error == Callable::CallError::CALL_OK);
	CHECK(beyond_declared_width_result.get_type() == Variant::NIL);

	const Variant in_range = uint64_t(9);
	const Variant *in_range_arguments[] = { &in_range };
	const Variant succeeded_result = object->callp(SNAME("cast_to_int"), in_range_arguments, 1, error);
	REQUIRE(error.error == Callable::CallError::CALL_OK);
	CHECK(succeeded_result == Variant(int64_t(9)));
}

TEST_CASE("[Modules][FoundryScript][CheckedNumeric] A uint value widens to long at runtime (#1771)") {
	ScopedCheckedNumericLanguage language;

	// Design section 6.1 lists `uint -> long` as value-preserving: assigning a `uint` local into a
	// `long` local, and mixing `uint` and `long` operands in an arithmetic operator, both need no
	// explicit conversion and must compute the real value rather than trip a runtime "invalid operands"
	// error.
	const Ref<FoundryScript> script = compile_checked_numeric_source(
			"func widen_assign(source: uint) -> long:\n"
			"\tvar l: long = source\n"
			"\treturn l\n"
			"\n"
			"func widen_return(source: uint) -> long:\n"
			"\treturn source\n"
			"\n"
			"func add_uint_and_long(u: uint, l: long) -> long:\n"
			"\treturn u + l\n"
			"\n"
			"func add_long_and_uint(l: long, u: uint) -> long:\n"
			"\treturn l + u\n"
			"\n"
			"func add_int_and_uint(i: int, u: uint) -> long:\n"
			"\treturn i + u\n");

	const Variant instance = instantiate_checked_numeric_script(script);
	Object *object = instance;
	REQUIRE(object != nullptr);

	// A value above `INT32_MAX` proves the widen reads the full unsigned magnitude rather than
	// reinterpreting the 32-bit carrier's bit pattern as a (possibly negative) `int`.
	const Variant large_uint = uint64_t(UINT32_MAX);
	const Variant *widen_arguments[] = { &large_uint };
	Callable::CallError error;

	const Variant widen_assign_result = object->callp(SNAME("widen_assign"), widen_arguments, 1, error);
	REQUIRE(error.error == Callable::CallError::CALL_OK);
	CHECK(widen_assign_result.get_type() == Variant::INT);
	CHECK(widen_assign_result == Variant(int64_t(UINT32_MAX)));

	const Variant widen_return_result = object->callp(SNAME("widen_return"), widen_arguments, 1, error);
	REQUIRE(error.error == Callable::CallError::CALL_OK);
	CHECK(widen_return_result.get_type() == Variant::INT);
	CHECK(widen_return_result == Variant(int64_t(UINT32_MAX)));

	const Variant uint_operand = uint64_t(UINT32_MAX);
	// Well beyond `INT32_MAX` but nowhere near `INT64_MAX`, so the sum with `uint_operand` cannot
	// overflow `int64_t` and the test proves the widen computes the real value rather than merely
	// avoiding a compile-time-detectable overflow.
	const Variant long_operand = int64_t(5000000000);
	const int64_t expected_sum = int64_t(uint64_t(UINT32_MAX)) + int64_t(5000000000);

	const Variant *add_uint_long_arguments[] = { &uint_operand, &long_operand };
	const Variant add_uint_long_result = object->callp(SNAME("add_uint_and_long"), add_uint_long_arguments, 2, error);
	REQUIRE(error.error == Callable::CallError::CALL_OK);
	CHECK(add_uint_long_result.get_type() == Variant::INT);
	CHECK(add_uint_long_result == Variant(expected_sum));

	const Variant *add_long_uint_arguments[] = { &long_operand, &uint_operand };
	const Variant add_long_uint_result = object->callp(SNAME("add_long_and_uint"), add_long_uint_arguments, 2, error);
	REQUIRE(error.error == Callable::CallError::CALL_OK);
	CHECK(add_long_uint_result.get_type() == Variant::INT);
	CHECK(add_long_uint_result == Variant(expected_sum));

	const Variant int_operand = int64_t(5);
	const Variant *add_int_uint_arguments[] = { &int_operand, &uint_operand };
	const Variant add_int_uint_result = object->callp(SNAME("add_int_and_uint"), add_int_uint_arguments, 2, error);
	REQUIRE(error.error == Callable::CallError::CALL_OK);
	CHECK(add_int_uint_result.get_type() == Variant::INT);
	CHECK(add_int_uint_result == Variant(int64_t(5) + int64_t(uint64_t(UINT32_MAX))));
}

TEST_CASE("[Modules][FoundryScript][CheckedNumeric] A dynamic member write widens uint to long") {
	ScopedCheckedNumericLanguage language;

	// The dynamic analogue of a typed store: `object->set()` routes through
	// `FoundryScript::_coerce_member_write`, which must apply the same design-6.1 widen-then-width
	// rule as OPCODE_ASSIGN_TYPED_BUILTIN. One coercion point covers instance members, static
	// members, and setter-backed members, so all four shapes are pinned here.
	const Ref<FoundryScript> script = compile_checked_numeric_source(
			"var m: long\n"
			"var n: int\n"
			"static var s: long\n"
			"var observed: long = 0:\n"
			"\tset(value):\n"
			"\t\tobserved = value + 1\n");

	const Variant instance = instantiate_checked_numeric_script(script);
	Object *object = instance;
	REQUIRE(object != nullptr);

	const auto set_member = [&](const StringName &p_name, const Variant &p_value) {
		bool valid = false;
		object->set(p_name, p_value, &valid);
		return valid;
	};

	// An in-`uint`-range value above `int`'s range widens onto the `INT` carrier for a `long` member.
	CHECK(set_member(SNAME("m"), Variant(uint64_t(4000000000))));
	Variant member_value = object->get(SNAME("m"));
	CHECK(member_value.get_type() == Variant::INT);
	CHECK(member_value == Variant(int64_t(4000000000)));

	// A `ulong`-magnitude value stays rejected even though it would fit `long`; the slot is unchanged.
	CHECK_FALSE(set_member(SNAME("m"), Variant(uint64_t(5000000000))));
	CHECK(object->get(SNAME("m")) == Variant(int64_t(4000000000)));

	// The declared width is asked about the widened value: `int` admits a small `uint` value and
	// refuses a `uint`-range value above its own range.
	CHECK(set_member(SNAME("n"), Variant(uint64_t(5))));
	member_value = object->get(SNAME("n"));
	CHECK(member_value.get_type() == Variant::INT);
	CHECK(member_value == Variant(5));
	CHECK_FALSE(set_member(SNAME("n"), Variant(uint64_t(3000000000))));
	CHECK(object->get(SNAME("n")) == Variant(5));

	// The static-member path funnels through the same coercion point.
	CHECK(set_member(SNAME("s"), Variant(uint64_t(4000000000))));
	member_value = object->get(SNAME("s"));
	CHECK(member_value.get_type() == Variant::INT);
	CHECK(member_value == Variant(int64_t(4000000000)));

	// A setter-backed member receives the already-widened `INT`-carrier value: the setter's `+ 1`
	// computes on the real magnitude rather than tripping an invalid-operands error.
	CHECK(set_member(SNAME("observed"), Variant(uint64_t(4000000000))));
	member_value = object->get(SNAME("observed"));
	CHECK(member_value.get_type() == Variant::INT);
	CHECK(member_value == Variant(int64_t(4000000001)));
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

// A nullable slot narrowed by a null guard keeps its declared width, so the addition below is checked
// at `uint` and not at the wide carrier the value travels in. Narrowing a `Variant` through a type
// test is checked at that same narrowed width, which is pinned by the script fixture
// `runtime/errors/fixed_width_integer_flow_narrowed_type_test.fs`.
TEST_CASE("[Modules][FoundryScript][CheckedNumeric] A null-guarded nullable keeps its declared width") {
	ScopedCheckedNumericLanguage language;

	const Ref<FoundryScript> script = compile_checked_numeric_source(
			"func add_after_null_guard(value: uint?):\n"
			"\tif value == null:\n"
			"\t\treturn null\n"
			"\treturn value + 1U\n");

	const Variant instance = instantiate_checked_numeric_script(script);
	Object *object = instance;
	REQUIRE(object != nullptr);

	const Variant maximum = uint64_t(UINT32_MAX);
	const Variant *arguments[] = { &maximum };
	Callable::CallError error;
	ERR_PRINT_OFF;
	const Variant result = object->callp(SNAME("add_after_null_guard"), arguments, 1, error);
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

// A value narrowed by a type test presents its narrowed width to every checked-op shape that reads
// it through an ordinary expression, not just a plain binary addition. `uint` is the width whose
// narrowed range is strictly inside the carrier's widest range (`ulong` already is the carrier's
// widest, and a negative `long` unary result already overflows at the carrier's widest range too), so
// the `uint` cases below are the ones that only pass once the narrowed width — not the carrier's
// widest range — reaches the checked op: `unary_uint` complements at 32 bits and would produce a
// different, wrong value if it complemented at the 64-bit carrier instead. `binary_int`/`binary_long`/
// `binary_ulong` confirm the same mechanism handles every declared width uniformly, even though
// `long`/`ulong` already coincided with the carrier's widest range before this fix. `binary_int` reads
// its addend from an explicitly `: int`-typed local rather than a bare literal like the other three
// cases: an unsuffixed integer literal keeps an unconstrained width so it can enter any integer slot,
// so pairing the narrowed `int` operand with such a literal would promote the operation to the
// carrier's widest range and never exercise the narrowed width at all.
//
// A compound assignment's implicit read of the old value (`value += x`) is covered separately by
// "A compound assignment on a flow-narrowed value is checked at its narrowed width" below: that read
// goes through the assignee node, which `FSAnalyzer::reduce_assignment()` retypes for a plain read/write
// no differently than the ordinary reads exercised here.
TEST_CASE("[Modules][FoundryScript][CheckedNumeric] A flow-narrowed operand is checked at its narrowed width") {
	ScopedCheckedNumericLanguage language;

	const Ref<FoundryScript> script = compile_checked_numeric_source(
			"func binary_uint(value):\n"
			"\tif value is uint:\n"
			"\t\treturn value + 4294967295U\n"
			"\treturn null\n"
			"\n"
			"func unary_uint(value):\n"
			"\tif value is uint:\n"
			"\t\treturn ~value\n"
			"\treturn null\n"
			"\n"
			"func binary_int(value):\n"
			"\tvar limit: int = 2147483647\n"
			"\tif value is int:\n"
			"\t\treturn value + limit\n"
			"\treturn null\n"
			"\n"
			"func binary_long(value):\n"
			"\tif value is long:\n"
			"\t\treturn value + 9223372036854775807L\n"
			"\treturn null\n"
			"\n"
			"func binary_ulong(value):\n"
			"\tif value is ulong:\n"
			"\t\treturn value + 18446744073709551615UL\n"
			"\treturn null\n");

	const Variant instance = instantiate_checked_numeric_script(script);
	Object *object = instance;
	REQUIRE(object != nullptr);

	{
		FlowNarrowedWidthErrorRecorder recorder;
		const Variant argument = uint64_t(2);
		const Variant *arguments[] = { &argument };
		Callable::CallError error;
		ERR_PRINT_OFF;
		object->callp(SNAME("binary_uint"), arguments, 1, error);
		ERR_PRINT_ON;
		REQUIRE(error.error == Callable::CallError::CALL_OK);
		CHECK(recorder.messages.contains("overflows \"uint\""));
	}

	{
		// The complement itself does not error, so this asserts the *value* the narrowed width
		// produces rather than a runtime error: flipping only the 32 declared bits of `2U` yields
		// `4294967293`, not the 64-bit carrier's complement of the same value.
		const Variant argument = uint64_t(2);
		const Variant *arguments[] = { &argument };
		Callable::CallError error;
		const Variant result = object->callp(SNAME("unary_uint"), arguments, 1, error);
		REQUIRE(error.error == Callable::CallError::CALL_OK);
		CHECK(result == Variant(uint64_t(4294967293ULL)));
	}

	{
		FlowNarrowedWidthErrorRecorder recorder;
		const Variant argument = int64_t(2);
		const Variant *arguments[] = { &argument };
		Callable::CallError error;
		ERR_PRINT_OFF;
		object->callp(SNAME("binary_int"), arguments, 1, error);
		ERR_PRINT_ON;
		REQUIRE(error.error == Callable::CallError::CALL_OK);
		CHECK(recorder.messages.contains("overflows \"int\""));
	}

	{
		FlowNarrowedWidthErrorRecorder recorder;
		const Variant argument = int64_t(1);
		const Variant *arguments[] = { &argument };
		Callable::CallError error;
		ERR_PRINT_OFF;
		object->callp(SNAME("binary_long"), arguments, 1, error);
		ERR_PRINT_ON;
		REQUIRE(error.error == Callable::CallError::CALL_OK);
		CHECK(recorder.messages.contains("overflows \"long\""));
	}

	{
		FlowNarrowedWidthErrorRecorder recorder;
		const Variant argument = uint64_t(1);
		const Variant *arguments[] = { &argument };
		Callable::CallError error;
		ERR_PRINT_OFF;
		object->callp(SNAME("binary_ulong"), arguments, 1, error);
		ERR_PRINT_ON;
		REQUIRE(error.error == Callable::CallError::CALL_OK);
		CHECK(recorder.messages.contains("overflows \"ulong\""));
	}
}

// `FSAnalyzer::reduce_assignment()` used to clear the assignee's flow-narrowed type before typing the
// assignee node, so a compound assignment's implicit old-value read (`value += x` reading `value`)
// always saw the declared width instead of a prior type test's narrower one. `uint` is the width whose
// narrowed range is strictly inside its carrier's widest range, so `binary_uint` is the case that only
// passes once the narrowed width reaches the compound op. `binary_int`/`binary_long`/`binary_ulong`
// confirm the same mechanism handles every declared width uniformly, even though `long`/`ulong` already
// coincide with their carrier's widest range. `binary_int` reads its addend from an explicitly
// `: int`-typed local rather than a bare literal for the same reason the ordinary-read coverage above
// does: an unsuffixed literal keeps an unconstrained width and would promote the operation to the
// carrier's widest range instead of the narrowed one.
TEST_CASE("[Modules][FoundryScript][CheckedNumeric] A compound assignment on a flow-narrowed value is checked at its narrowed width") {
	ScopedCheckedNumericLanguage language;

	const Ref<FoundryScript> script = compile_checked_numeric_source(
			"func binary_uint(value):\n"
			"\tif value is uint:\n"
			"\t\tvalue += 4294967295U\n"
			"\treturn null\n"
			"\n"
			"func binary_int(value):\n"
			"\tvar limit: int = 2147483647\n"
			"\tif value is int:\n"
			"\t\tvalue += limit\n"
			"\treturn null\n"
			"\n"
			"func binary_long(value):\n"
			"\tif value is long:\n"
			"\t\tvalue += 9223372036854775807L\n"
			"\treturn null\n"
			"\n"
			"func binary_ulong(value):\n"
			"\tif value is ulong:\n"
			"\t\tvalue += 18446744073709551615UL\n"
			"\treturn null\n");

	const Variant instance = instantiate_checked_numeric_script(script);
	Object *object = instance;
	REQUIRE(object != nullptr);

	{
		FlowNarrowedWidthErrorRecorder recorder;
		const Variant argument = uint64_t(1);
		const Variant *arguments[] = { &argument };
		Callable::CallError error;
		ERR_PRINT_OFF;
		object->callp(SNAME("binary_uint"), arguments, 1, error);
		ERR_PRINT_ON;
		REQUIRE(error.error == Callable::CallError::CALL_OK);
		CHECK(recorder.messages.contains("overflows \"uint\""));
	}

	{
		FlowNarrowedWidthErrorRecorder recorder;
		const Variant argument = int64_t(1);
		const Variant *arguments[] = { &argument };
		Callable::CallError error;
		ERR_PRINT_OFF;
		object->callp(SNAME("binary_int"), arguments, 1, error);
		ERR_PRINT_ON;
		REQUIRE(error.error == Callable::CallError::CALL_OK);
		CHECK(recorder.messages.contains("overflows \"int\""));
	}

	{
		FlowNarrowedWidthErrorRecorder recorder;
		const Variant argument = int64_t(1);
		const Variant *arguments[] = { &argument };
		Callable::CallError error;
		ERR_PRINT_OFF;
		object->callp(SNAME("binary_long"), arguments, 1, error);
		ERR_PRINT_ON;
		REQUIRE(error.error == Callable::CallError::CALL_OK);
		CHECK(recorder.messages.contains("overflows \"long\""));
	}

	{
		FlowNarrowedWidthErrorRecorder recorder;
		const Variant argument = uint64_t(1);
		const Variant *arguments[] = { &argument };
		Callable::CallError error;
		ERR_PRINT_OFF;
		object->callp(SNAME("binary_ulong"), arguments, 1, error);
		ERR_PRINT_ON;
		REQUIRE(error.error == Callable::CallError::CALL_OK);
		CHECK(recorder.messages.contains("overflows \"ulong\""));
	}
}

// A plain assignment's destination must keep being checked against the variable's declared type, not
// whatever it was momentarily narrowed to: overwriting a `Variant` that was narrowed to `uint` with an
// incompatible value must still succeed, and a subsequent read must not still observe the stale
// narrowing. This is the control proving the fix scopes the preserved narrowing to the compound
// assignment's implicit read only, never to the assignment's destination or later statements.
TEST_CASE("[Modules][FoundryScript][CheckedNumeric] A simple assignment through a flow-narrowed variable clears the narrowing") {
	ScopedCheckedNumericLanguage language;

	const Ref<FoundryScript> script = compile_checked_numeric_source(
			"func reassign(value):\n"
			"\tif value is uint:\n"
			"\t\tvalue = \"not a uint anymore\"\n"
			"\treturn value\n");

	const Variant instance = instantiate_checked_numeric_script(script);
	Object *object = instance;
	REQUIRE(object != nullptr);

	const Variant argument = uint64_t(2);
	const Variant *arguments[] = { &argument };
	Callable::CallError error;
	const Variant result = object->callp(SNAME("reassign"), arguments, 1, error);
	REQUIRE(error.error == Callable::CallError::CALL_OK);
	CHECK(result == Variant("not a uint anymore"));
}

} // namespace FSTests
