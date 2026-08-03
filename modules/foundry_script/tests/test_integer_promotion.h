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
};

// The width and carrier the analyzer recorded for `p_name`'s initializer.
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

	// A constant on the other carrier proves nothing: the descriptor rejects the crossing outright.
	CHECK(classify_constant(uint32_type, int64_type, int64_t(7)) == Conversion::EXPLICIT_REQUIRED);
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

	// `int` deliberately declares no width yet (see `_applied_numeric_type()`), so its two matrix rows
	// are covered by the descriptor-level test above; the three spellings that do declare one are
	// checked here through the analyzer.
	const AnalyzedSnippet snippet(
			"func test():\n"
			"\tvar u: uint = 2U\n"
			"\tvar ul: ulong = 4UL\n"
			"\tvar l: long = 3L\n"
			"\tvar same_uint = u + u\n"
			"\tvar same_long = l + l\n"
			"\tvar same_ulong = ul + ul\n"
			"\tvar uint_ulong = u + ul\n"
			"\tvar ulong_uint = ul + u\n"
			"\tvar folded_long = 1L + 2L\n"
			"\tvar negated_long = -l\n"
			"\tprint(same_uint, same_long, same_ulong, uint_ulong, ulong_uint, folded_long, negated_long)\n");

	REQUIRE(snippet.parse_error == OK);
	CHECK_MESSAGE(snippet.first_error().is_empty(), snippet.first_error());

	check_initializer_type(snippet, "same_uint", Variant::UINT, NumericType::UINT32);
	check_initializer_type(snippet, "same_long", Variant::INT, NumericType::INT64);
	check_initializer_type(snippet, "same_ulong", Variant::UINT, NumericType::UINT64);
	check_initializer_type(snippet, "uint_ulong", Variant::UINT, NumericType::UINT64);
	check_initializer_type(snippet, "ulong_uint", Variant::UINT, NumericType::UINT64);
	check_initializer_type(snippet, "folded_long", Variant::INT, NumericType::INT64);
	check_initializer_type(snippet, "negated_long", Variant::INT, NumericType::INT64);
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

	// A pair the matrix does promote still needs the conversion spelled out, because the two carriers
	// cannot meet in one operation; the diagnostic names the promoted type.
	const AnalyzedSnippet mixed_carriers(
			"func test():\n"
			"\tvar u: uint = 2U\n"
			"\tvar l: long = 3L\n"
			"\tprint(u + l)\n");
	CHECK(mixed_carriers.parse_error == OK);
	CHECK(mixed_carriers.first_error().contains("Convert both to \"long\" explicitly."));

	const AnalyzedSnippet mixed_carriers_reversed(
			"func test():\n"
			"\tvar u: uint = 2U\n"
			"\tvar l: long = 3L\n"
			"\tprint(l + u)\n");
	CHECK(mixed_carriers_reversed.parse_error == OK);
	CHECK(mixed_carriers_reversed.first_error().contains("Convert both to \"long\" explicitly."));
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
