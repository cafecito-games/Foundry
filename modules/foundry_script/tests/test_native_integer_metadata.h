/**************************************************************************/
/*  test_native_integer_metadata.h                                        */
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

#include "core/object/class_db.h"
#include "core/variant/numeric_type.h"
#include "core/variant/type_info.h"

#include "tests/test_macros.h"

// Coverage for the native integer boundary: a `PropertyInfo` transports only the Variant carrier, so
// a bound method's declared width lives in its `MethodBind` metadata. These tests assert on what the
// mapping produces -- the descriptor a native slot decodes to, whether a call type-checks, and the
// carrier and magnitude a native result actually arrives with -- rather than on any source text.

namespace FSTests {

struct ScopedNativeIntegerMetadataLanguage {
	ScopedNativeIntegerMetadataLanguage() {
		if (!FSLanguage::get_singleton()->has_any_global_constant(SNAME("RefCounted"))) {
			FSLanguage::get_singleton()->init();
		}
	}
};

static Error analyze_native_integer_source(FSParser &p_parser, const String &p_source) {
	static int unique_index = 0;
	const String path = vformat("user://test_native_integer_metadata_%d.fs", unique_index++);

	const Error parse_error = p_parser.parse(p_source, path, false);
	if (parse_error != OK) {
		return parse_error;
	}
	FSAnalyzer analyzer(&p_parser);
	return analyzer.analyze();
}

// Analyzes `p_source` and returns its diagnostics, so a failing expectation reports what the
// analyzer actually said instead of only that it disagreed.
static String native_integer_analysis_errors(const String &p_source) {
	FSParser parser;
	ERR_PRINT_OFF;
	const Error error = analyze_native_integer_source(parser, p_source);
	ERR_PRINT_ON;

	String messages;
	for (const FSParser::ParserError &parser_error : parser.get_errors()) {
		if (!messages.is_empty()) {
			messages += "\n";
		}
		messages += parser_error.message;
	}
	if (error != OK && messages.is_empty()) {
		messages = "analysis failed without a diagnostic";
	}
	return messages;
}

#define CHECK_NATIVE_INTEGER_ANALYSIS_SUCCEEDS(m_source)                          \
	do {                                                                          \
		const String _analysis_errors = native_integer_analysis_errors(m_source); \
		CHECK_MESSAGE(_analysis_errors.is_empty(), _analysis_errors);             \
	} while (false)

#define CHECK_NATIVE_INTEGER_ANALYSIS_FAILS(m_source) \
	CHECK_FALSE(native_integer_analysis_errors(m_source).is_empty())

static Ref<FoundryScript> compile_native_integer_source(const String &p_source) {
	static int unique_index = 0;
	const String path = vformat("user://test_native_integer_metadata_runtime_%d.fs", unique_index++);

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

static Variant call_native_integer_method(const Ref<FoundryScript> &p_script, const StringName &p_method) {
	Callable::CallError error;
	Variant instance = p_script->_new(nullptr, 0, error);
	REQUIRE(error.error == Callable::CallError::CALL_OK);

	Variant result;
	instance.callp(p_method, nullptr, 0, result, error);
	REQUIRE(error.error == Callable::CallError::CALL_OK);
	return result;
}

TEST_CASE("[Modules][FoundryScript][NativeIntegerMetadata] Every native integer width maps to one descriptor") {
	struct Expectation {
		Variant::Type carrier;
		FoundryTypeInfo::Metadata metadata;
		NumericType numeric_type;
	};

	const Expectation expectations[] = {
		{ Variant::INT, FoundryTypeInfo::METADATA_INT_IS_INT8, NumericType::INT8 },
		{ Variant::INT, FoundryTypeInfo::METADATA_INT_IS_INT16, NumericType::INT16 },
		{ Variant::INT, FoundryTypeInfo::METADATA_INT_IS_INT32, NumericType::INT32 },
		{ Variant::INT, FoundryTypeInfo::METADATA_INT_IS_INT64, NumericType::INT64 },
		{ Variant::UINT, FoundryTypeInfo::METADATA_INT_IS_UINT8, NumericType::UINT8 },
		{ Variant::UINT, FoundryTypeInfo::METADATA_INT_IS_UINT16, NumericType::UINT16 },
		{ Variant::UINT, FoundryTypeInfo::METADATA_INT_IS_UINT32, NumericType::UINT32 },
		{ Variant::UINT, FoundryTypeInfo::METADATA_INT_IS_UINT64, NumericType::UINT64 },
		// A declaration that states no width pins none, so its carrier's wide default applies.
		{ Variant::INT, FoundryTypeInfo::METADATA_NONE, NumericType::NONE },
		{ Variant::UINT, FoundryTypeInfo::METADATA_NONE, NumericType::NONE },
		// Character metadata describes C++ storage, not an integer range.
		{ Variant::INT, FoundryTypeInfo::METADATA_INT_IS_CHAR32, NumericType::NONE },
		// A nominal unsigned wrapper such as `ObjectID` keeps the signed carrier it declares, so its
		// unsigned metadata does not constrain the slot.
		{ Variant::INT, FoundryTypeInfo::METADATA_INT_IS_UINT64, NumericType::NONE },
		{ Variant::UINT, FoundryTypeInfo::METADATA_INT_IS_INT32, NumericType::NONE },
	};

	for (const Expectation &expectation : expectations) {
		const NumericType mapped = numeric_type_from_native_metadata(expectation.carrier, expectation.metadata);
		CHECK_MESSAGE(mapped == expectation.numeric_type,
				vformat("carrier %s metadata %d mapped to %s",
						Variant::get_type_name(expectation.carrier), int(expectation.metadata), numeric_type_name(mapped)));
	}
}

TEST_CASE("[Modules][FoundryScript][NativeIntegerMetadata] A bound method reports the width its C++ signature declares") {
	const MethodBind *set_seed = ClassDB::get_method(SNAME("RandomNumberGenerator"), SNAME("set_seed"));
	REQUIRE(set_seed != nullptr);
	CHECK(set_seed->get_argument_meta(0) == FoundryTypeInfo::METADATA_INT_IS_UINT64);
	CHECK(set_seed->get_argument_info(0).type == Variant::UINT);

	const MethodBind *randi = ClassDB::get_method(SNAME("RandomNumberGenerator"), SNAME("randi"));
	REQUIRE(randi != nullptr);
	CHECK(randi->get_argument_meta(-1) == FoundryTypeInfo::METADATA_INT_IS_UINT32);
	CHECK(randi->get_return_info().type == Variant::UINT);

	const MethodBind *randi_range = ClassDB::get_method(SNAME("RandomNumberGenerator"), SNAME("randi_range"));
	REQUIRE(randi_range != nullptr);
	CHECK(randi_range->get_argument_meta(0) == FoundryTypeInfo::METADATA_INT_IS_INT32);
	CHECK(randi_range->get_argument_info(0).type == Variant::INT);

	// An `ObjectID` declares the signed carrier explicitly, and that binding is unchanged.
	const MethodBind *get_instance_id = ClassDB::get_method(SNAME("Object"), SNAME("get_instance_id"));
	REQUIRE(get_instance_id != nullptr);
	CHECK(get_instance_id->get_return_info().type == Variant::INT);
}

TEST_CASE("[Modules][FoundryScript][NativeIntegerMetadata] A default argument advertises its parameter's carrier") {
	// `Animation::compress()` declares `uint32_t` parameters and gives them plain signed C++
	// defaults, which is the ordinary way a binding is written. The advertised default has to agree
	// with the advertised parameter, or a caller that fills the parameter from the default would
	// hand the call a value of the other carrier.
	const MethodBind *compress = ClassDB::get_method(SNAME("Animation"), SNAME("compress"));
	REQUIRE(compress != nullptr);
	REQUIRE(compress->get_default_argument_count() == 3);

	const Variant page_size = compress->get_default_argument(0);
	CHECK(page_size.get_type() == Variant::UINT);
	CHECK(page_size.operator uint64_t() == 8192);

	const Variant fps = compress->get_default_argument(1);
	CHECK(fps.get_type() == Variant::UINT);
	CHECK(fps.operator uint64_t() == 120);

	// A signed parameter keeps a signed default, and a non-integer default is untouched.
	const MethodBind *randfn = ClassDB::get_method(SNAME("RandomNumberGenerator"), SNAME("randfn"));
	REQUIRE(randfn != nullptr);
	CHECK(randfn->get_default_argument(0).get_type() == Variant::FLOAT);

	const MethodBind *seek = ClassDB::get_method(SNAME("FileAccess"), SNAME("seek_end"));
	REQUIRE(seek != nullptr);
	CHECK(seek->get_default_argument(0).get_type() == Variant::INT);
}

TEST_CASE("[Modules][FoundryScript][NativeIntegerMetadata] Native signatures type-check at their declared width") {
	ScopedNativeIntegerMetadataLanguage language;

	// `uint64_t` in, `uint64_t` out: both sides are `ulong`.
	CHECK_NATIVE_INTEGER_ANALYSIS_SUCCEEDS(
			"func run() -> ulong:\n"
			"\tvar rng := RandomNumberGenerator.new()\n"
			"\trng.set_seed(7UL)\n"
			"\treturn rng.get_seed()\n");

	// `uint32_t` out is `uint`, which widens into `ulong` but is not itself a `ulong`.
	CHECK_NATIVE_INTEGER_ANALYSIS_SUCCEEDS(
			"func run() -> uint:\n"
			"\tvar rng := RandomNumberGenerator.new()\n"
			"\treturn rng.randi()\n");
	CHECK_NATIVE_INTEGER_ANALYSIS_SUCCEEDS(
			"func run() -> ulong:\n"
			"\tvar rng := RandomNumberGenerator.new()\n"
			"\treturn rng.randi()\n");

	// A `uint64_t` result does not fit the signed 64-bit range, so it needs an explicit conversion.
	CHECK_NATIVE_INTEGER_ANALYSIS_FAILS(
			"func run() -> long:\n"
			"\tvar rng := RandomNumberGenerator.new()\n"
			"\treturn rng.get_seed()\n");
	CHECK_NATIVE_INTEGER_ANALYSIS_SUCCEEDS(
			"func run() -> long:\n"
			"\tvar rng := RandomNumberGenerator.new()\n"
			"\treturn rng.get_seed() as long\n");

	// A native property decodes through its accessors, so `seed` is the accessors' `ulong`.
	CHECK_NATIVE_INTEGER_ANALYSIS_SUCCEEDS(
			"func run() -> ulong:\n"
			"\tvar rng := RandomNumberGenerator.new()\n"
			"\trng.seed = 9UL\n"
			"\treturn rng.seed\n");

	// An `ObjectID` keeps its explicit signed binding, so it stays a `long`.
	CHECK_NATIVE_INTEGER_ANALYSIS_SUCCEEDS(
			"func run(value: Object) -> long:\n"
			"\treturn value.get_instance_id()\n");
	CHECK_NATIVE_INTEGER_ANALYSIS_FAILS(
			"func run(value: Object) -> ulong:\n"
			"\treturn value.get_instance_id()\n");
}

TEST_CASE("[Modules][FoundryScript][NativeIntegerMetadata] A constant argument is checked against the native range") {
	ScopedNativeIntegerMetadataLanguage language;

	// `int` parameters are 32 bits wide natively, so a constant outside that range fails at the
	// argument instead of being narrowed on the way in.
	CHECK_NATIVE_INTEGER_ANALYSIS_SUCCEEDS(
			"func run() -> int:\n"
			"\tvar rng := RandomNumberGenerator.new()\n"
			"\treturn rng.randi_range(0, 2147483647)\n");
	CHECK_NATIVE_INTEGER_ANALYSIS_FAILS(
			"func run() -> int:\n"
			"\tvar rng := RandomNumberGenerator.new()\n"
			"\treturn rng.randi_range(0, 2147483648)\n");

	// An unsigned parameter takes an unsigned constant, including one in the upper half of the
	// 64-bit range that has no signed spelling at all.
	CHECK_NATIVE_INTEGER_ANALYSIS_SUCCEEDS(
			"func run() -> void:\n"
			"\tvar rng := RandomNumberGenerator.new()\n"
			"\trng.set_seed(18446744073709551615UL)\n");
	// An unsuffixed positive constant is representable in the unsigned carrier, so it crosses on its
	// own, exactly as it does in an assignment. A negative one still has no unsigned value at all.
	CHECK_NATIVE_INTEGER_ANALYSIS_SUCCEEDS(
			"func run() -> void:\n"
			"\tvar rng := RandomNumberGenerator.new()\n"
			"\trng.set_seed(12)\n");
	CHECK_NATIVE_INTEGER_ANALYSIS_FAILS(
			"func run() -> void:\n"
			"\tvar rng := RandomNumberGenerator.new()\n"
			"\trng.set_seed(-1)\n");

	// A dynamic value of a broader type needs the conversion spelled out; the conversion itself is
	// checked at run time.
	CHECK_NATIVE_INTEGER_ANALYSIS_FAILS(
			"func run(value: long) -> void:\n"
			"\tvar rng := RandomNumberGenerator.new()\n"
			"\trng.set_seed(value)\n");
	CHECK_NATIVE_INTEGER_ANALYSIS_SUCCEEDS(
			"func run(value: long) -> void:\n"
			"\tvar rng := RandomNumberGenerator.new()\n"
			"\trng.set_seed(value as ulong)\n");
}

TEST_CASE("[Modules][FoundryScript][NativeIntegerMetadata] A native result keeps its carrier and its full range") {
	ScopedNativeIntegerMetadataLanguage language;

	const Ref<FoundryScript> script = compile_native_integer_source(
			"func upper_half_seed() -> ulong:\n"
			"\tvar rng := RandomNumberGenerator.new()\n"
			"\trng.set_seed(18446744073709551615UL)\n"
			"\treturn rng.get_seed()\n"
			"\n"
			"func randi_result() -> uint:\n"
			"\tvar rng := RandomNumberGenerator.new()\n"
			"\trng.set_seed(1UL)\n"
			"\treturn rng.randi()\n"
			"\n"
			"func range_result() -> int:\n"
			"\tvar rng := RandomNumberGenerator.new()\n"
			"\trng.set_seed(1UL)\n"
			"\treturn rng.randi_range(-5, 5)\n");

	// A value above `INT64_MAX` survives the boundary intact rather than arriving as a negative
	// signed number.
	const Variant seed = call_native_integer_method(script, SNAME("upper_half_seed"));
	CHECK(seed.get_type() == Variant::UINT);
	CHECK(seed == Variant(uint64_t(UINT64_MAX)));

	const Variant random_value = call_native_integer_method(script, SNAME("randi_result"));
	CHECK(random_value.get_type() == Variant::UINT);
	CHECK(random_value.operator uint64_t() <= uint64_t(UINT32_MAX));

	const Variant range_value = call_native_integer_method(script, SNAME("range_result"));
	CHECK(range_value.get_type() == Variant::INT);
	CHECK(range_value.operator int64_t() >= -5);
	CHECK(range_value.operator int64_t() <= 5);
}

TEST_CASE("[Modules][FoundryScript][NativeIntegerMetadata] A failed boundary conversion never reaches the native method") {
	ScopedNativeIntegerMetadataLanguage language;

	const Ref<FoundryScript> script = compile_native_integer_source(
			"var rng := RandomNumberGenerator.new()\n"
			"\n"
			"func prepare() -> void:\n"
			"\trng.set_seed(3UL)\n"
			"\n"
			"func assign_negative_seed(value: long) -> void:\n"
			"\trng.set_seed(value as ulong)\n"
			"\n"
			"func current_seed() -> ulong:\n"
			"\treturn rng.get_seed()\n");

	Callable::CallError error;
	Variant instance = script->_new(nullptr, 0, error);
	REQUIRE(error.error == Callable::CallError::CALL_OK);
	Object *object = instance;
	REQUIRE(object != nullptr);

	Variant result;
	instance.callp(SNAME("prepare"), nullptr, 0, result, error);
	REQUIRE(error.error == Callable::CallError::CALL_OK);

	const Variant negative = int64_t(-1);
	const Variant *arguments[1] = { &negative };
	ERR_PRINT_OFF;
	instance.callp(SNAME("assign_negative_seed"), arguments, 1, result, error);
	ERR_PRINT_ON;
	CHECK(error.error == Callable::CallError::CALL_OK);

	// The conversion failed before `set_seed()` ran, so the generator still holds what it had.
	Variant seed;
	instance.callp(SNAME("current_seed"), nullptr, 0, seed, error);
	REQUIRE(error.error == Callable::CallError::CALL_OK);
	CHECK(seed.get_type() == Variant::UINT);
	CHECK(seed == Variant(uint64_t(3)));
}

TEST_CASE("[Modules][FoundryScript][NativeIntegerMetadata] A builtin method reports the width its C++ signature declares") {
	// `Color::hex(uint32_t)` is a static builtin method; its lone parameter is `uint32_t`, not the wide
	// `uint64_t` a caller gets when no metadata is available at all.
	CHECK(Variant::get_builtin_method_argument_metadata(Variant::COLOR, SNAME("hex"), 0) == FoundryTypeInfo::METADATA_INT_IS_UINT32);
	CHECK(Variant::get_builtin_method_argument_type(Variant::COLOR, SNAME("hex"), 0) == Variant::UINT);

	// `Color::hex64(uint64_t)` is genuinely 64-bit, and stays that way.
	CHECK(Variant::get_builtin_method_argument_metadata(Variant::COLOR, SNAME("hex64"), 0) == FoundryTypeInfo::METADATA_INT_IS_UINT64);
	CHECK(Variant::get_builtin_method_argument_type(Variant::COLOR, SNAME("hex64"), 0) == Variant::UINT);

	// `Signal::connect(const Callable &, uint32_t p_flags = 0)`: the second argument is the `uint32_t`
	// flags word, not a `uint64_t`.
	CHECK(Variant::get_builtin_method_argument_metadata(Variant::SIGNAL, SNAME("connect"), 1) == FoundryTypeInfo::METADATA_INT_IS_UINT32);
	CHECK(Variant::get_builtin_method_argument_type(Variant::SIGNAL, SNAME("connect"), 1) == Variant::UINT);
	// Its first argument, the callable, carries no integer metadata at all.
	CHECK(Variant::get_builtin_method_argument_metadata(Variant::SIGNAL, SNAME("connect"), 0) == FoundryTypeInfo::METADATA_NONE);

	// `String::num_uint64(uint64_t, int base = 10, bool capitalize_hex = false)`.
	CHECK(Variant::get_builtin_method_argument_metadata(Variant::STRING, SNAME("num_uint64"), 0) == FoundryTypeInfo::METADATA_INT_IS_UINT64);
	CHECK(Variant::get_builtin_method_argument_type(Variant::STRING, SNAME("num_uint64"), 0) == Variant::UINT);

	// `Signal::get_object_id()` returns an `ObjectID`, a nominal unsigned wrapper that keeps the signed
	// `INT` carrier while declaring `METADATA_INT_IS_UINT64`; the return metadata surfaces that exactly
	// as `MethodBind::get_argument_meta(-1)` would for the equivalent class-bound method.
	CHECK(Variant::get_builtin_method_return_metadata(Variant::SIGNAL, SNAME("get_object_id")) == FoundryTypeInfo::METADATA_INT_IS_UINT64);
	CHECK(Variant::get_builtin_method_return_type(Variant::SIGNAL, SNAME("get_object_id")) == Variant::INT);

	// A method with no declared width at all (a `bool` return, `Callable` argument) reports
	// `METADATA_NONE` rather than fabricating a width.
	CHECK(Variant::get_builtin_method_return_metadata(Variant::SIGNAL, SNAME("is_connected")) == FoundryTypeInfo::METADATA_NONE);
}

TEST_CASE("[Modules][FoundryScript][NativeIntegerMetadata] A builtin method's exact argument width participates in call validation") {
	ScopedNativeIntegerMetadataLanguage language;

	// `Color.hex()` declares `uint32_t`, so a `uint` literal at the exact width type-checks.
	CHECK_NATIVE_INTEGER_ANALYSIS_SUCCEEDS(
			"func run() -> Color:\n"
			"\treturn Color.hex(4294967295U)\n");

	// `Color.hex64()` declares `uint64_t`, so a `ulong` literal at that width type-checks.
	CHECK_NATIVE_INTEGER_ANALYSIS_SUCCEEDS(
			"func run() -> Color:\n"
			"\treturn Color.hex64(4294967295UL)\n");

	// A dynamic `ulong` value is wider than the declared `uint32_t` parameter and needs an explicit
	// narrowing conversion spelled out, exactly as it does for a class-bound native method (see
	// `RandomNumberGenerator.set_seed()` above). If the parameter still decoded as the wide `ulong`
	// carrier (the pre-fix behavior, from missing metadata), a `ulong` argument would incorrectly be
	// treated as an exact match and this would succeed without the cast.
	CHECK_NATIVE_INTEGER_ANALYSIS_FAILS(
			"func run(value: ulong) -> Color:\n"
			"\treturn Color.hex(value)\n");
	CHECK_NATIVE_INTEGER_ANALYSIS_SUCCEEDS(
			"func run(value: ulong) -> Color:\n"
			"\treturn Color.hex(value as uint)\n");
}

TEST_CASE("[Modules][FoundryScript][NativeIntegerMetadata] A builtin method's exact-width mismatch names the exact declared type") {
	ScopedNativeIntegerMetadataLanguage language;

	// `Color.hex()`'s parameter is `uint32_t`; a wrong-typed argument's diagnostic must name "uint",
	// not the wide "ulong" carrier a missing-metadata decode would fall back to.
	const String errors = native_integer_analysis_errors(
			"func run() -> void:\n"
			"\tColor.hex(true)\n");
	CHECK(errors.contains("should be \"uint\""));
	CHECK_FALSE(errors.contains("should be \"ulong\""));
}

} // namespace FSTests
