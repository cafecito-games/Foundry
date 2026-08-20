/**************************************************************************/
/*  test_integer_promotion.h                                              */
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

#include "../fs_analyzer.h"
#include "../fs_type.h"

#include "tests/test_macros.h"

namespace TestIntegerPromotion {

// A parsed-and-analyzed snippet, so a promoted expression can be read back as the type the analyzer
// actually recorded rather than inferred from a printed value.
class AnalyzedSnippet {
public:
	FSParser parser;
	FSAnalyzer analyzer;
	Error parse_error = ERR_PARSE_ERROR;
	Error analyze_error = ERR_PARSE_ERROR;

	explicit AnalyzedSnippet(const String &p_source) :
			analyzer(&parser) {
		parse_error = parser.parse(p_source, "user://integer_promotion_test.fs", false);
		if (parse_error == OK) {
			analyze_error = analyzer.analyze();
		}
	}

	// The declared type of a local in `test()`, which is the destination the promotion had to satisfy.
	const FSParser::VariableNode *local(const StringName &p_name) const {
		const FSParser::ClassNode *tree = parser.get_tree();
		if (tree == nullptr || !tree->has_function(SNAME("test"))) {
			return nullptr;
		}
		const FSParser::FunctionNode *function = tree->get_member(SNAME("test")).function;
		if (function == nullptr || function->body == nullptr) {
			return nullptr;
		}
		for (const FSParser::Node *statement : function->body->statements) {
			if (statement == nullptr || statement->type != FSParser::Node::VARIABLE) {
				continue;
			}
			const FSParser::VariableNode *variable = static_cast<const FSParser::VariableNode *>(statement);
			if (variable->identifier != nullptr && variable->identifier->name == p_name) {
				return variable;
			}
		}
		return nullptr;
	}

	String first_error() const {
		const List<FSParser::ParserError> &errors = parser.get_errors();
		return errors.is_empty() ? String() : errors.front()->get().message;
	}

	// One rejected constant is one mistake, so the count is part of what these cases assert.
	int error_count() const {
		return parser.get_errors().size();
	}
};

// The width and carrier the analyzer recorded for `p_name`'s initializer. For an inferred local this
// is also the local's own type; for an explicitly typed local whose initializer is a plain value (not
// a constant `update_const_expression_builtin_type()` rewrites) the initializer keeps its own natural
// type, so an implicit-widen crossing is asserted with `check_declared_type()` below instead.
static void check_initializer_type(const AnalyzedSnippet &p_snippet, const StringName &p_name, Variant::Type p_carrier, NumericType p_numeric_type) {
	const FSParser::VariableNode *variable = p_snippet.local(p_name);
	REQUIRE_MESSAGE(variable != nullptr, vformat("local \"%s\" must exist", String(p_name)));
	REQUIRE(variable->initializer != nullptr);
	const FSParser::DataType initializer_type = variable->initializer->get_datatype();
	CHECK_MESSAGE(initializer_type.builtin_type == p_carrier,
			vformat("\"%s\" carrier was %s", String(p_name), Variant::get_type_name(initializer_type.builtin_type)));
	// `DataType::operator==` compares widths permissively, so the descriptor is asserted directly.
	CHECK_MESSAGE(initializer_type.numeric_type == p_numeric_type,
			vformat("\"%s\" width was %s, expected %s", String(p_name),
					numeric_type_name(initializer_type.numeric_type), numeric_type_name(p_numeric_type)));
}

// The width and carrier the analyzer recorded for `p_name` itself -- its declared type, not its
// initializer expression's type. An implicit-widen crossing (`var l: long = some_uint_variable`)
// converts the value at the assignment boundary without retyping the source expression, so the local's
// own type is the destination the promotion had to satisfy.
static void check_declared_type(const AnalyzedSnippet &p_snippet, const StringName &p_name, Variant::Type p_carrier, NumericType p_numeric_type) {
	const FSParser::VariableNode *variable = p_snippet.local(p_name);
	REQUIRE_MESSAGE(variable != nullptr, vformat("local \"%s\" must exist", String(p_name)));
	const FSParser::DataType declared_type = variable->get_datatype();
	CHECK_MESSAGE(declared_type.builtin_type == p_carrier,
			vformat("\"%s\" carrier was %s", String(p_name), Variant::get_type_name(declared_type.builtin_type)));
	CHECK_MESSAGE(declared_type.numeric_type == p_numeric_type,
			vformat("\"%s\" width was %s, expected %s", String(p_name),
					numeric_type_name(declared_type.numeric_type), numeric_type_name(p_numeric_type)));
}

using Conversion = FSNumericConversion::Conversion;

// The four source-spellable integer types, in the order the promotion matrix is written.
static constexpr NumericType PUBLIC_TYPES[] = {
	NumericType::INT32,
	NumericType::UINT32,
	NumericType::INT64,
	NumericType::UINT64,
};

// The promotion matrix from the language design, as a full 4x4 table indexed by `PUBLIC_TYPES`.
// `NumericType::MAX` marks a pair with no value-preserving common integer type. The table is
// written out rather than derived so that a change to the rules has to change this table too.
static constexpr NumericType PROMOTION_MATRIX[4][4] = {
	//              int                   uint                  long                  ulong
	/* int   */ { NumericType::INT32, NumericType::INT64, NumericType::INT64, NumericType::MAX },
	/* uint  */ { NumericType::INT64, NumericType::UINT32, NumericType::INT64, NumericType::UINT64 },
	/* long  */ { NumericType::INT64, NumericType::INT64, NumericType::INT64, NumericType::MAX },
	/* ulong */ { NumericType::MAX, NumericType::UINT64, NumericType::MAX, NumericType::UINT64 },
};

static FSParser::DataType make_numeric_type(Variant::Type p_builtin_type, NumericType p_numeric_type) {
	FSParser::DataType type;
	type.kind = FSParser::DataType::BUILTIN;
	type.type_source = FSParser::DataType::ANNOTATED_EXPLICIT;
	type.builtin_type = p_builtin_type;
	type.numeric_type = p_numeric_type;
	return type;
}

static FSParser::DataType make_integer_type(NumericType p_numeric_type) {
	return make_numeric_type(numeric_type_carrier(p_numeric_type), p_numeric_type);
}

static FSParser::DataType make_float_type() {
	return make_numeric_type(Variant::FLOAT, NumericType::NONE);
}

static Conversion classify(const FSParser::DataType &p_target, const FSParser::DataType &p_source) {
	return FSNumericConversion::classify(p_target, p_source, nullptr);
}

static Conversion classify_constant(const FSParser::DataType &p_target, const FSParser::DataType &p_source, const Variant &p_value) {
	return FSNumericConversion::classify(p_target, p_source, &p_value);
}

} // namespace TestIntegerPromotion

TEST_CASE("[Modules][FoundryScript][NumericTypes] The promotion matrix answers every ordered pair") {
	using namespace TestIntegerPromotion;

	for (int left = 0; left < 4; left++) {
		for (int right = 0; right < 4; right++) {
			const NumericType expected = PROMOTION_MATRIX[left][right];
			NumericType promoted = NumericType::MAX;
			const bool promoted_ok = FSNumericConversion::promote_integer_pair(PUBLIC_TYPES[left], PUBLIC_TYPES[right], promoted);

			const String pair = vformat("%s, %s",
					numeric_type_public_name(PUBLIC_TYPES[left]),
					numeric_type_public_name(PUBLIC_TYPES[right]));

			if (expected == NumericType::MAX) {
				CHECK_MESSAGE(!promoted_ok, vformat("%s must have no common integer type", pair));
				continue;
			}
			CHECK_MESSAGE(promoted_ok, vformat("%s must promote", pair));
			CHECK_MESSAGE(promoted == expected,
					vformat("%s promoted to %s, expected %s", pair, numeric_type_name(promoted), numeric_type_name(expected)));
		}
	}
}

TEST_CASE("[Modules][FoundryScript][NumericTypes] Promotion is symmetric") {
	using namespace TestIntegerPromotion;

	for (int left = 0; left < 4; left++) {
		for (int right = 0; right < 4; right++) {
			NumericType forward = NumericType::MAX;
			NumericType backward = NumericType::MAX;
			const bool forward_ok = FSNumericConversion::promote_integer_pair(PUBLIC_TYPES[left], PUBLIC_TYPES[right], forward);
			const bool backward_ok = FSNumericConversion::promote_integer_pair(PUBLIC_TYPES[right], PUBLIC_TYPES[left], backward);

			CHECK(forward_ok == backward_ok);
			if (forward_ok) {
				CHECK(forward == backward);
			}
		}
	}
}

TEST_CASE("[Modules][FoundryScript][NumericTypes] Unsigned 64-bit mixes have no common integer type") {
	using namespace TestIntegerPromotion;

	NumericType promoted = NumericType::MAX;
	CHECK_FALSE(FSNumericConversion::promote_integer_pair(NumericType::INT32, NumericType::UINT64, promoted));
	CHECK_FALSE(FSNumericConversion::promote_integer_pair(NumericType::UINT64, NumericType::INT32, promoted));
	CHECK_FALSE(FSNumericConversion::promote_integer_pair(NumericType::INT64, NumericType::UINT64, promoted));
	CHECK_FALSE(FSNumericConversion::promote_integer_pair(NumericType::UINT64, NumericType::INT64, promoted));
}

TEST_CASE("[Modules][FoundryScript][NumericTypes] An unconstrained width promotes to no constraint") {
	using namespace TestIntegerPromotion;

	for (const NumericType numeric_type : PUBLIC_TYPES) {
		NumericType promoted = NumericType::MAX;
		CHECK(FSNumericConversion::promote_integer_pair(NumericType::NONE, numeric_type, promoted));
		CHECK(promoted == NumericType::NONE);

		promoted = NumericType::MAX;
		CHECK(FSNumericConversion::promote_integer_pair(numeric_type, NumericType::NONE, promoted));
		CHECK(promoted == NumericType::NONE);
	}

	NumericType promoted = NumericType::MAX;
	CHECK(FSNumericConversion::promote_integer_pair(NumericType::NONE, NumericType::NONE, promoted));
	CHECK(promoted == NumericType::NONE);
}

TEST_CASE("[Modules][FoundryScript][NumericTypes] Native-only small widths promote as their 32-bit type") {
	using namespace TestIntegerPromotion;

	NumericType promoted = NumericType::MAX;
	CHECK(FSNumericConversion::promote_integer_pair(NumericType::INT8, NumericType::INT16, promoted));
	CHECK(promoted == NumericType::INT32);

	promoted = NumericType::MAX;
	CHECK(FSNumericConversion::promote_integer_pair(NumericType::UINT8, NumericType::INT64, promoted));
	CHECK(promoted == NumericType::INT64);

	promoted = NumericType::MAX;
	CHECK_FALSE(FSNumericConversion::promote_integer_pair(NumericType::INT8, NumericType::UINT64, promoted));
}

TEST_CASE("[Modules][FoundryScript][NumericTypes] Widening within a carrier is implicit and narrowing is not") {
	using namespace TestIntegerPromotion;

	CHECK(classify(make_integer_type(NumericType::INT64), make_integer_type(NumericType::INT64)) == Conversion::IDENTITY);
	CHECK(classify(make_integer_type(NumericType::UINT32), make_integer_type(NumericType::UINT32)) == Conversion::IDENTITY);

	CHECK(classify(make_integer_type(NumericType::INT64), make_integer_type(NumericType::INT32)) == Conversion::IMPLICIT_WIDEN);
	CHECK(classify(make_integer_type(NumericType::UINT64), make_integer_type(NumericType::UINT32)) == Conversion::IMPLICIT_WIDEN);

	CHECK(classify(make_integer_type(NumericType::INT32), make_integer_type(NumericType::INT64)) == Conversion::EXPLICIT_REQUIRED);
	CHECK(classify(make_integer_type(NumericType::UINT32), make_integer_type(NumericType::UINT64)) == Conversion::EXPLICIT_REQUIRED);
}

TEST_CASE("[Modules][FoundryScript][NumericTypes] A uint value widens to long unconditionally (#1771)") {
	using namespace TestIntegerPromotion;

	// Design section 6.1 lists `uint -> long` as value-preserving: every `uint` value is representable
	// as a `long`, so a `uint` *value* -- not just a constant whose exact magnitude is known -- may
	// cross into a `long` slot without proof, unlike the general signedness crossings `classify()`
	// otherwise only grants a representable constant.
	CHECK(classify(make_integer_type(NumericType::INT64), make_integer_type(NumericType::UINT32)) == Conversion::IMPLICIT_WIDEN);

	// The reverse direction is not value-preserving (a negative `long` has no `uint` representation) and
	// stays an explicit conversion.
	CHECK(classify(make_integer_type(NumericType::UINT32), make_integer_type(NumericType::INT64)) == Conversion::EXPLICIT_REQUIRED);

	// `uint` -> `int` (32-bit) is a genuine narrowing as well as a crossing and is unaffected.
	CHECK(classify(make_integer_type(NumericType::INT32), make_integer_type(NumericType::UINT32)) == Conversion::EXPLICIT_REQUIRED);

	// `ulong` -> `long` is a same-width signedness crossing, not the widening this fix grants.
	CHECK(classify(make_integer_type(NumericType::INT64), make_integer_type(NumericType::UINT64)) == Conversion::EXPLICIT_REQUIRED);
}

TEST_CASE("[Modules][FoundryScript][NumericTypes] A uint variable assigns into a long slot without an explicit cast (#1771)") {
	using namespace TestIntegerPromotion;

	const AnalyzedSnippet snippet(
			"func test():\n"
			"\tvar u: uint = 1U\n"
			"\tvar l: long = u\n"
			"\tprint(l)\n");
	REQUIRE(snippet.parse_error == OK);
	CHECK_MESSAGE(snippet.first_error().is_empty(), snippet.first_error());
	check_declared_type(snippet, "l", Variant::INT, NumericType::INT64);

	// The reverse direction still requires an explicit cast: not every `long` value is a `uint`.
	const AnalyzedSnippet reversed(
			"func test():\n"
			"\tvar l: long = 1L\n"
			"\tvar u: uint = l\n"
			"\tprint(u)\n");
	CHECK(reversed.parse_error == OK);
	CHECK(reversed.first_error().contains(R"(Cannot assign a value of type)"));
}

TEST_CASE("[Modules][FoundryScript][NumericTypes] An exactly representable constant narrows where a value may not") {
	using namespace TestIntegerPromotion;

	const FSParser::DataType uint32_type = make_integer_type(NumericType::UINT32);
	const FSParser::DataType uint64_type = make_integer_type(NumericType::UINT64);

	// The same nominal conversion diverges purely on whether the exact value is known.
	CHECK(classify(uint32_type, uint64_type) == Conversion::EXPLICIT_REQUIRED);
	CHECK(classify_constant(uint32_type, uint64_type, uint64_t(7)) == Conversion::CONSTANT_CHECKED);
	CHECK(classify_constant(uint32_type, uint64_type, uint64_t(UINT32_MAX)) == Conversion::CONSTANT_CHECKED);
	CHECK(classify_constant(uint32_type, uint64_type, uint64_t(UINT32_MAX) + 1) == Conversion::EXPLICIT_REQUIRED);

	const FSParser::DataType int32_type = make_integer_type(NumericType::INT32);
	const FSParser::DataType int64_type = make_integer_type(NumericType::INT64);
	CHECK(classify_constant(int32_type, int64_type, int64_t(INT32_MIN)) == Conversion::CONSTANT_CHECKED);
	CHECK(classify_constant(int32_type, int64_type, int64_t(INT32_MAX)) == Conversion::CONSTANT_CHECKED);
	CHECK(classify_constant(int32_type, int64_type, int64_t(INT32_MAX) + 1) == Conversion::EXPLICIT_REQUIRED);
	CHECK(classify_constant(int32_type, int64_type, int64_t(INT32_MIN) - 1) == Conversion::EXPLICIT_REQUIRED);

	// A constant on the other carrier still proves its exact value, so a representable one may cross
	// signedness: design section 6.1 permits an unsuffixed positive constant to enter an unsigned slot.
	CHECK(classify_constant(uint32_type, int64_type, int64_t(7)) == Conversion::CONSTANT_CHECKED);
	CHECK(classify_constant(uint32_type, int64_type, int64_t(UINT32_MAX)) == Conversion::CONSTANT_CHECKED);
	// A negative constant has no unsigned representation, so the crossing is still rejected.
	CHECK(classify_constant(uint32_type, int64_type, int64_t(-1)) == Conversion::EXPLICIT_REQUIRED);
	// A magnitude the destination cannot hold is rejected even though the sign would cross fine.
	CHECK(classify_constant(uint32_type, int64_type, int64_t(UINT32_MAX) + 1) == Conversion::EXPLICIT_REQUIRED);

	// The crossing is symmetric: a `uint` constant within the signed range may enter a signed slot.
	CHECK(classify_constant(int32_type, uint32_type, uint64_t(7)) == Conversion::CONSTANT_CHECKED);
	CHECK(classify_constant(int32_type, uint32_type, uint64_t(INT32_MAX)) == Conversion::CONSTANT_CHECKED);
	CHECK(classify_constant(int32_type, uint32_type, uint64_t(INT32_MAX) + 1) == Conversion::EXPLICIT_REQUIRED);
}

TEST_CASE("[Modules][FoundryScript][NumericTypes] A slot that declares no width keeps its prior behavior") {
	using namespace TestIntegerPromotion;

	const FSParser::DataType unconstrained = make_numeric_type(Variant::INT, NumericType::NONE);
	CHECK(classify(unconstrained, make_integer_type(NumericType::INT64)) == Conversion::IDENTITY);
	CHECK(classify(make_integer_type(NumericType::INT64), unconstrained) == Conversion::IDENTITY);
	CHECK(classify(make_float_type(), unconstrained) == Conversion::IDENTITY);
}

TEST_CASE("[Modules][FoundryScript][NumericTypes] 32-bit integers promote to float and 64-bit ones need proof") {
	using namespace TestIntegerPromotion;

	const FSParser::DataType float_type = make_float_type();

	CHECK(classify(float_type, make_integer_type(NumericType::INT32)) == Conversion::IMPLICIT_WIDEN);
	CHECK(classify(float_type, make_integer_type(NumericType::UINT32)) == Conversion::IMPLICIT_WIDEN);

	CHECK(classify(float_type, make_integer_type(NumericType::INT64)) == Conversion::EXPLICIT_REQUIRED);
	CHECK(classify(float_type, make_integer_type(NumericType::UINT64)) == Conversion::EXPLICIT_REQUIRED);

	// 2^53 is the last integer the double represents exactly along with its predecessor.
	CHECK(classify_constant(float_type, make_integer_type(NumericType::INT64), int64_t(1) << 53) == Conversion::CONSTANT_CHECKED);
	CHECK(classify_constant(float_type, make_integer_type(NumericType::INT64), ((int64_t(1) << 53) + 1)) == Conversion::EXPLICIT_REQUIRED);
	CHECK(classify_constant(float_type, make_integer_type(NumericType::UINT64), uint64_t(1) << 53) == Conversion::CONSTANT_CHECKED);
	CHECK(classify_constant(float_type, make_integer_type(NumericType::UINT64), ((uint64_t(1) << 53) + 1)) == Conversion::EXPLICIT_REQUIRED);

	CHECK(classify(float_type, float_type) == Conversion::IDENTITY);
}

TEST_CASE("[Modules][FoundryScript][NumericTypes] Non-numeric types are outside the classifier") {
	using namespace TestIntegerPromotion;

	FSParser::DataType string_type;
	string_type.kind = FSParser::DataType::BUILTIN;
	string_type.type_source = FSParser::DataType::ANNOTATED_EXPLICIT;
	string_type.builtin_type = Variant::STRING;

	CHECK(classify(string_type, make_integer_type(NumericType::INT32)) == Conversion::INVALID);
	CHECK(classify(make_integer_type(NumericType::INT32), string_type) == Conversion::INVALID);
	CHECK_FALSE(FSNumericConversion::is_numeric_builtin(string_type));
	CHECK(FSNumericConversion::is_numeric_builtin(make_float_type()));
}

TEST_CASE("[Modules][FoundryScript][NumericTypes] An operator's result carries the promoted width") {
	using namespace TestIntegerPromotion;

	const AnalyzedSnippet snippet(
			"func test():\n"
			"\tvar i: int = 1\n"
			"\tvar u: uint = 2U\n"
			"\tvar ul: ulong = 4UL\n"
			"\tvar l: long = 3L\n"
			"\tvar same_int = i + i\n"
			"\tvar same_uint = u + u\n"
			"\tvar same_long = l + l\n"
			"\tvar same_ulong = ul + ul\n"
			"\tvar uint_ulong = u + ul\n"
			"\tvar ulong_uint = ul + u\n"
			"\tvar folded_long = 1L + 2L\n"
			"\tvar negated_long = -l\n"
			"\tprint(same_int, same_uint, same_long, same_ulong, uint_ulong, ulong_uint, folded_long, negated_long)\n");

	REQUIRE(snippet.parse_error == OK);
	CHECK_MESSAGE(snippet.first_error().is_empty(), snippet.first_error());

	check_initializer_type(snippet, "same_int", Variant::INT, NumericType::INT32);
	check_initializer_type(snippet, "same_uint", Variant::UINT, NumericType::UINT32);
	check_initializer_type(snippet, "same_long", Variant::INT, NumericType::INT64);
	check_initializer_type(snippet, "same_ulong", Variant::UINT, NumericType::UINT64);
	check_initializer_type(snippet, "uint_ulong", Variant::UINT, NumericType::UINT64);
	check_initializer_type(snippet, "ulong_uint", Variant::UINT, NumericType::UINT64);
	check_initializer_type(snippet, "folded_long", Variant::INT, NumericType::INT64);
	check_initializer_type(snippet, "negated_long", Variant::INT, NumericType::INT64);
}

TEST_CASE("[Modules][FoundryScript][NumericTypes] A declared int contributes its 32-bit width to a promotion") {
	using namespace TestIntegerPromotion;

	// `int` carries the same `INT32` width as every other 32-bit-declaring slot, so it promotes and
	// narrows exactly like `uint`, `long`, and `ulong` already did: mixing it with `long` promotes to
	// `long`, and a value outside the 32-bit signed range is refused at an `int` destination.
	const AnalyzedSnippet widened(
			"func test():\n"
			"\tvar i: int = 1\n"
			"\tvar l: long = 2L\n"
			"\tvar sum = i + l\n"
			"\tprint(sum)\n");
	REQUIRE(widened.parse_error == OK);
	CHECK_MESSAGE(widened.first_error().is_empty(), widened.first_error());
	check_initializer_type(widened, "sum", Variant::INT, NumericType::INT64);

	const AnalyzedSnippet out_of_range(
			"func test():\n"
			"\tvar source: long = 3000000000L\n"
			"\tvar overflowed: int = source\n"
			"\tprint(overflowed)\n");
	CHECK(out_of_range.parse_error == OK);
	CHECK(out_of_range.first_error().contains("Cannot assign a value of type long to variable \"overflowed\" with specified type int."));

	const FSParser::DataType int32_type = make_integer_type(NumericType::INT32);
	const FSParser::DataType int64_type = make_integer_type(NumericType::INT64);
	CHECK(classify_constant(int32_type, int64_type, int64_t(INT32_MAX)) == Conversion::CONSTANT_CHECKED);
	CHECK(classify_constant(int32_type, int64_type, int64_t(INT32_MAX) + 1) == Conversion::EXPLICIT_REQUIRED);
}

TEST_CASE("[Modules][FoundryScript][NumericTypes] A mixed signed/unsigned operand pair is rejected in both orders") {
	using namespace TestIntegerPromotion;

	const AnalyzedSnippet no_common_type(
			"func test():\n"
			"\tvar l: long = 3L\n"
			"\tvar ul: ulong = 4UL\n"
			"\tprint(l + ul)\n");
	CHECK(no_common_type.parse_error == OK);
	CHECK(no_common_type.first_error().contains("No integer type holds every value of both"));

	const AnalyzedSnippet no_common_type_reversed(
			"func test():\n"
			"\tvar l: long = 3L\n"
			"\tvar ul: ulong = 4UL\n"
			"\tprint(ul + l)\n");
	CHECK(no_common_type_reversed.parse_error == OK);
	CHECK(no_common_type_reversed.first_error().contains("No integer type holds every value of both"));

	// `uint`/`long` is design section 6.1's one value-preserving carrier crossing: every `uint` value is
	// representable as a `long`, so the pair the matrix promotes to `long` needs no conversion spelled
	// out, in either operand order (#1771).
	const AnalyzedSnippet mixed_carriers(
			"func test():\n"
			"\tvar u: uint = 2U\n"
			"\tvar l: long = 3L\n"
			"\tvar sum = u + l\n"
			"\tprint(sum)\n");
	REQUIRE(mixed_carriers.parse_error == OK);
	CHECK_MESSAGE(mixed_carriers.first_error().is_empty(), mixed_carriers.first_error());
	check_initializer_type(mixed_carriers, "sum", Variant::INT, NumericType::INT64);

	const AnalyzedSnippet mixed_carriers_reversed(
			"func test():\n"
			"\tvar u: uint = 2U\n"
			"\tvar l: long = 3L\n"
			"\tvar sum = l + u\n"
			"\tprint(sum)\n");
	REQUIRE(mixed_carriers_reversed.parse_error == OK);
	CHECK_MESSAGE(mixed_carriers_reversed.first_error().is_empty(), mixed_carriers_reversed.first_error());
	check_initializer_type(mixed_carriers_reversed, "sum", Variant::INT, NumericType::INT64);

	// `int`/`uint` still has no unconditional crossing of its own, but both operands individually widen
	// to `long` (`int` -> `long` and `uint` -> `long` are each value-preserving), so the pair the matrix
	// promotes to `long` also needs no conversion spelled out.
	const AnalyzedSnippet int_uint_mixed(
			"func test():\n"
			"\tvar i: int = 2\n"
			"\tvar u: uint = 3U\n"
			"\tvar sum = i + u\n"
			"\tprint(sum)\n");
	REQUIRE(int_uint_mixed.parse_error == OK);
	CHECK_MESSAGE(int_uint_mixed.first_error().is_empty(), int_uint_mixed.first_error());
	check_initializer_type(int_uint_mixed, "sum", Variant::INT, NumericType::INT64);

	// A shift is not a promotion: its result keeps the left operand's own width and carrier rather than
	// a common one (see `_checked_binary_type()` in fs_byte_codegen.cpp), so a mixed `int`/`uint` shift
	// still needs an explicit conversion even though `+` no longer does (#1771 review finding). Admitting
	// it here would give the expression a `long` result code generation never checks the shift at.
	const AnalyzedSnippet shift_mixed_carriers(
			"func test():\n"
			"\tvar i: int = 1\n"
			"\tvar u: uint = 31U\n"
			"\tprint(i << u)\n");
	CHECK(shift_mixed_carriers.parse_error == OK);
	CHECK(shift_mixed_carriers.first_error().contains("Convert both to \"long\" explicitly."));

	const AnalyzedSnippet shift_mixed_carriers_reversed(
			"func test():\n"
			"\tvar u: uint = 1U\n"
			"\tvar l: long = 40L\n"
			"\tprint(u << l)\n");
	CHECK(shift_mixed_carriers_reversed.parse_error == OK);
	CHECK(shift_mixed_carriers_reversed.first_error().contains("Convert both to \"long\" explicitly."));
}

TEST_CASE("[Modules][FoundryScript][NumericTypes] Ordering compares across carriers without a common type") {
	using namespace TestIntegerPromotion;

	// Comparison produces a boolean rather than an integer, so it needs no common carrier and uses the
	// exact cross-carrier comparison instead.
	const AnalyzedSnippet snippet(
			"func test():\n"
			"\tvar l: long = 3L\n"
			"\tvar ul: ulong = 4UL\n"
			"\tvar ordered = l < ul\n"
			"\tprint(ordered)\n");
	REQUIRE(snippet.parse_error == OK);
	CHECK_MESSAGE(snippet.first_error().is_empty(), snippet.first_error());
	check_initializer_type(snippet, "ordered", Variant::BOOL, NumericType::NONE);
}

TEST_CASE("[Modules][FoundryScript][NumericTypes] A folded constant keeps the width it was checked at") {
	using namespace TestIntegerPromotion;

	const AnalyzedSnippet snippet(
			"func test():\n"
			"\tvar in_range = 2000000000U + 100U\n"
			"\tprint(in_range)\n");
	REQUIRE(snippet.parse_error == OK);
	CHECK_MESSAGE(snippet.first_error().is_empty(), snippet.first_error());

	check_initializer_type(snippet, "in_range", Variant::UINT, NumericType::UINT32);
}

TEST_CASE("[Modules][FoundryScript][NumericTypes] An unsuffixed constant crosses signedness when representable") {
	using namespace TestIntegerPromotion;

	// Design section 6.1: a representable constant may cross signedness without a suffix or explicit
	// cast, so `var x: ulong = 12` must not force the caller to spell `12U`. The crossing is symmetric,
	// so a `uint` constant that fits the signed range enters `int`/`long` the same way.
	const AnalyzedSnippet snippet(
			"func test():\n"
			"\tvar as_uint: uint = 12\n"
			"\tvar as_ulong: ulong = 12\n"
			"\tvar as_int: int = 12U\n"
			"\tvar as_long: long = 12U\n"
			"\tprint(as_uint, as_ulong, as_int, as_long)\n");
	REQUIRE(snippet.parse_error == OK);
	CHECK_MESSAGE(snippet.first_error().is_empty(), snippet.first_error());

	check_initializer_type(snippet, "as_uint", Variant::UINT, NumericType::UINT32);
	check_initializer_type(snippet, "as_ulong", Variant::UINT, NumericType::UINT64);
	check_initializer_type(snippet, "as_int", Variant::INT, NumericType::INT32);
	check_initializer_type(snippet, "as_long", Variant::INT, NumericType::INT64);

	// A negative constant still has no unsigned representation.
	const AnalyzedSnippet negative(
			"func test():\n"
			"\tvar as_uint: uint = -1\n"
			"\tprint(as_uint)\n");
	CHECK(negative.parse_error == OK);
	CHECK(negative.first_error().contains(R"(Cannot assign a value of type int to variable "as_uint" with specified type uint.)"));
	CHECK(negative.error_count() == 1);

	// An out-of-range constant still needs an explicit conversion even though its sign would cross fine.
	const AnalyzedSnippet out_of_range(
			"func test():\n"
			"\tvar as_uint: uint = 4294967296\n"
			"\tprint(as_uint)\n");
	CHECK(out_of_range.parse_error == OK);
	CHECK(out_of_range.first_error().contains(R"(Cannot assign a value of type int to variable "as_uint" with specified type uint.)"));
	CHECK(out_of_range.error_count() == 1);
}

TEST_CASE("[Modules][FoundryScript][NumericTypes] A constant too large for its own carrier's width is refused by value") {
	using namespace TestIntegerPromotion;

	// `int` and the unsuffixed constant's own type render under the same name, so naming the two types
	// says nothing. The value and the destination's range are what describe the mistake, and they are
	// said once: the declaration does not add a report of its own.
	const AnalyzedSnippet declaration(
			"func test():\n"
			"\tvar v: int = 5000000000\n"
			"\tprint(v)\n");
	CHECK(declaration.parse_error == OK);
	CHECK(declaration.first_error().contains(R"(Cannot convert 5000000000 to "int": the value is outside its range)"));
	CHECK(declaration.error_count() == 1);

	const AnalyzedSnippet argument(
			"func take(v: int) -> int:\n"
			"\treturn v\n"
			"\n"
			"func test():\n"
			"\tprint(take(5000000000))\n");
	CHECK(argument.parse_error == OK);
	CHECK(argument.first_error().contains(R"(Cannot convert 5000000000 to "int": the value is outside its range)"));
	CHECK(argument.error_count() == 1);

	// A constant that fits keeps crossing, and takes the destination's width with it.
	const AnalyzedSnippet in_range(
			"func test():\n"
			"\tvar v: int = 5\n"
			"\tprint(v)\n");
	CHECK(in_range.parse_error == OK);
	CHECK_MESSAGE(in_range.first_error().is_empty(), in_range.first_error());
	check_initializer_type(in_range, "v", Variant::INT, NumericType::INT32);
}

TEST_CASE("[Modules][FoundryScript][NumericTypes] `uint` now reaches `float`, but a non-constant `ulong` still needs a cast") {
	using namespace TestIntegerPromotion;

	// Every `uint` value is exactly representable as a `double`, so it promotes to `float` exactly
	// like `int` already does (design section 6.1). `Variant::can_convert_strict()` now answers
	// `UINT` -> `FLOAT` unconditionally, the same way it already answers `INT` -> `FLOAT`, and
	// `Variant::construct()` has a matching unchecked `UINT` -> `FLOAT` conversion.
	const AnalyzedSnippet uint_widens(
			"func test():\n"
			"\tvar u: uint = 2U\n"
			"\tvar widened: float = u\n"
			"\tprint(widened)\n");
	CHECK(uint_widens.parse_error == OK);
	CHECK(uint_widens.first_error().is_empty());

	// `ulong` cannot make the same unconditional promise: above 2^53 the round trip through `double`
	// loses precision, so a non-constant `ulong` value still requires an explicit cast, mirroring the
	// existing `long` -> `float` treatment.
	const AnalyzedSnippet ulong_variable(
			"func test():\n"
			"\tvar ul: ulong = 4UL\n"
			"\tvar from_ulong: float = ul\n"
			"\tprint(from_ulong)\n");
	CHECK(ulong_variable.parse_error == OK);
	CHECK(ulong_variable.first_error().contains(R"(Cannot assign a value of type ulong to variable "from_ulong" with specified type float.)"));

	// A `ulong` constant is different: its exact value is known, so design section 6.1's constant
	// carve-out still applies once the value survives the round trip through `double`.
	const AnalyzedSnippet ulong_constant_representable(
			"func test():\n"
			"\tvar from_ulong: float = 5UL\n"
			"\tprint(from_ulong)\n");
	CHECK(ulong_constant_representable.parse_error == OK);
	CHECK(ulong_constant_representable.first_error().is_empty());

	const AnalyzedSnippet ulong_constant_too_large(
			"func test():\n"
			"\tvar from_ulong: float = 9007199254740993UL\n"
			"\tprint(from_ulong)\n");
	CHECK(ulong_constant_too_large.parse_error == OK);
	CHECK(ulong_constant_too_large.first_error().contains(R"(Cannot assign a value of type ulong to variable "from_ulong" with specified type float.)"));
	CHECK(ulong_constant_too_large.error_count() == 1);
}

TEST_CASE("[Modules][FoundryScript][NumericTypes] A sum that leaves the promoted range is refused") {
	using namespace TestIntegerPromotion;

	// The operands agree on `uint`, so the sum is a `uint` sum. It does not silently widen to the
	// carrier that happens to hold it, and it does not wrap the way unsigned C++ arithmetic would.
	const AnalyzedSnippet snippet(
			"func test():\n"
			"\tvar out_of_range = 4000000000U + 4000000000U\n"
			"\tprint(out_of_range)\n");
	REQUIRE(snippet.parse_error == OK);
	CHECK(snippet.first_error().contains(R"(The "+" operator overflows "uint")"));
	CHECK(snippet.first_error().contains("0 to 4294967295"));
}

TEST_CASE("[Modules][FoundryScript][NumericTypes] A width-less constant is still range-checked") {
	using namespace TestIntegerPromotion;

	const FSParser::DataType uint32_type = make_integer_type(NumericType::UINT32);
	const FSParser::DataType unconstrained_unsigned = make_numeric_type(Variant::UINT, NumericType::NONE);

	CHECK(classify(uint32_type, unconstrained_unsigned) == Conversion::IDENTITY);
	CHECK(classify_constant(uint32_type, unconstrained_unsigned, uint64_t(7)) == Conversion::CONSTANT_CHECKED);
	CHECK(classify_constant(uint32_type, unconstrained_unsigned, uint64_t(8000000000)) == Conversion::EXPLICIT_REQUIRED);
}

TEST_CASE("[Modules][FoundryScript][NumericTypes] A native narrow destination keeps its own range") {
	using namespace TestIntegerPromotion;

	// The 8- and 16-bit descriptors have no source spelling, so they only ever appear as a native
	// signature's constraint. Conversion still has to honor their exact range, unlike promotion, which
	// folds them into their 32-bit counterparts because neither can name a result type.
	const FSParser::DataType int8_type = make_integer_type(NumericType::INT8);
	const FSParser::DataType int32_type = make_integer_type(NumericType::INT32);

	CHECK(classify(int32_type, int8_type) == Conversion::IMPLICIT_WIDEN);
	CHECK(classify(int8_type, int32_type) == Conversion::EXPLICIT_REQUIRED);
	CHECK(classify_constant(int8_type, int32_type, int64_t(127)) == Conversion::CONSTANT_CHECKED);
	CHECK(classify_constant(int8_type, int32_type, int64_t(128)) == Conversion::EXPLICIT_REQUIRED);
	CHECK(classify_constant(int8_type, int32_type, int64_t(-129)) == Conversion::EXPLICIT_REQUIRED);

	const FSParser::DataType uint16_type = make_integer_type(NumericType::UINT16);
	CHECK(classify_constant(uint16_type, make_integer_type(NumericType::UINT32), uint64_t(65535)) == Conversion::CONSTANT_CHECKED);
	CHECK(classify_constant(uint16_type, make_integer_type(NumericType::UINT32), uint64_t(65536)) == Conversion::EXPLICIT_REQUIRED);

	// A narrow native source still fits the double exactly, so it promotes to float.
	CHECK(classify(make_float_type(), int8_type) == Conversion::IMPLICIT_WIDEN);
}

TEST_CASE("[Modules][FoundryScript][NumericTypes] Set-wise arithmetic collapses when every combination agrees") {
	using namespace TestIntegerPromotion;

	// `int` with `long` and `long` with `long` both promote to `long`, so the union of the results is
	// a single type and the expression is an ordinary `long`.
	const AnalyzedSnippet snippet(
			"func test():\n"
			"\tvar narrow: int | long = 1\n"
			"\tvar wide: long = 2L\n"
			"\tvar total = narrow + wide\n"
			"\tprint(total)\n");
	REQUIRE(snippet.parse_error == OK);
	CHECK_MESSAGE(snippet.first_error().is_empty(), snippet.first_error());
	check_initializer_type(snippet, "total", Variant::INT, NumericType::INT64);
}

TEST_CASE("[Modules][FoundryScript][NumericTypes] Differing combination results become a union") {
	using namespace TestIntegerPromotion;

	// `int` with `int` stays `int` while every other combination reaches `long`, so the result is the
	// union of both rather than a forced join onto the wider one.
	const AnalyzedSnippet snippet(
			"func test():\n"
			"\tvar left: int | long = 1\n"
			"\tvar right: int | long = 2\n"
			"\tvar total = left + right\n"
			"\tprint(total)\n");
	REQUIRE(snippet.parse_error == OK);
	CHECK_MESSAGE(snippet.first_error().is_empty(), snippet.first_error());

	const FSParser::VariableNode *total = snippet.local("total");
	REQUIRE(total != nullptr);
	REQUIRE(total->initializer != nullptr);
	const FSParser::DataType total_type = total->initializer->get_datatype();
	CHECK(total_type.kind == FSParser::DataType::UNION);
	CHECK_MESSAGE(total_type.union_members.size() == 2, total_type.to_string());
	CHECK(total_type.union_members.has(make_integer_type(NumericType::INT32)));
	CHECK(total_type.union_members.has(make_integer_type(NumericType::INT64)));
}

TEST_CASE("[Modules][FoundryScript][NumericTypes] A combination with no result rejects the whole operation") {
	using namespace TestIntegerPromotion;

	// `ulong` with `int` has no common integer type, and one unsatisfiable combination is enough,
	// even though `int` with `int` would be fine on its own.
	const AnalyzedSnippet snippet(
			"func test():\n"
			"\tvar mixed: int | ulong = 1\n"
			"\tvar narrow: int = 2\n"
			"\tprint(mixed + narrow)\n");
	REQUIRE(snippet.parse_error == OK);
	const String error = snippet.first_error();
	CHECK(error.contains("int | ulong"));
	CHECK(error.contains("ulong"));
	CHECK(error.contains("Narrow both operands with a type test"));
}

TEST_CASE("[Modules][FoundryScript][NumericTypes] Two Number-bounded values cannot be added directly") {
	using namespace TestIntegerPromotion;

	// `Number` admits `long` with `ulong`, which the promotion matrix has no common type for, so a
	// direct addition under a full numeric bound is always rejected. Narrowing or an explicit
	// conversion is the supported way to write it.
	const AnalyzedSnippet snippet(
			"func add[X: Number, Y: Number](left: X, right: Y) -> long:\n"
			"\treturn left + right\n"
			"\n"
			"func test():\n"
			"\tprint(add(1, 2))\n");
	REQUIRE(snippet.parse_error == OK);
	const String error = snippet.first_error();
	CHECK(error.contains("ulong"));
	CHECK(error.contains("Narrow both operands with a type test"));
}

TEST_CASE("[Modules][FoundryScript][NumericTypes] A set-typed integer still reaches float without a gate") {
	using namespace TestIntegerPromotion;

	// `long + 1.5` is accepted and yields `float` even though `var f: float = some_long` is not, and
	// set-wise checking reproduces that asymmetry rather than diverging from it.
	const AnalyzedSnippet snippet(
			"func test():\n"
			"\tvar value: int | long = 1\n"
			"\tvar scaled = value + 1.5\n"
			"\tprint(scaled)\n");
	REQUIRE(snippet.parse_error == OK);
	CHECK_MESSAGE(snippet.first_error().is_empty(), snippet.first_error());
	check_initializer_type(snippet, "scaled", Variant::FLOAT, NumericType::NONE);
}

TEST_CASE("[Modules][FoundryScript][NumericTypes] Comparing set-typed operands still yields a boolean") {
	using namespace TestIntegerPromotion;

	// Ordering needs no common carrier, so every combination a wide unsigned union allows has a
	// result even though addition would not.
	const AnalyzedSnippet snippet(
			"func test():\n"
			"\tvar left: long | ulong = 1L\n"
			"\tvar right: long | ulong = 2L\n"
			"\tvar ordered = left < right\n"
			"\tprint(ordered)\n");
	REQUIRE(snippet.parse_error == OK);
	CHECK_MESSAGE(snippet.first_error().is_empty(), snippet.first_error());
	check_initializer_type(snippet, "ordered", Variant::BOOL, NumericType::NONE);
}

TEST_CASE("[Modules][FoundryScript][NumericTypes] A set operand cannot cross carriers, since erasure removes the widening") {
	using namespace TestIntegerPromotion;

	// `int` with `uint` has a common type, but only because code generation widens the `uint` operand
	// into the shared carrier first, which needs both carriers statically. A set-typed operand erases
	// to untyped, so the combination has no executable result and is rejected instead of failing with
	// invalid operands at run time.
	const AnalyzedSnippet snippet(
			"func test():\n"
			"\tvar left: int | long = 2\n"
			"\tvar right: uint = 3U\n"
			"\tprint(left + right)\n");
	REQUIRE(snippet.parse_error == OK);
	const String error = snippet.first_error();
	CHECK(error.contains("int | long"));
	CHECK(error.contains("cannot mix \"int\" and \"uint\" operands"));
}
