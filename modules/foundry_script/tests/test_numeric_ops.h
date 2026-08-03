/**************************************************************************/
/*  test_numeric_ops.h                                                    */
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

#include "../fs_numeric_ops.h"

#include "core/math/math_defs.h"

#include "tests/test_macros.h"

namespace TestNumericOps {

// The four source-nameable descriptors, in the order every table below walks them. A boundary case is
// written once per descriptor rather than once per width, so a descriptor that is added later fails
// the tables until it is given its own row.
struct Descriptor {
	NumericType type;
	const char *name;
	bool is_unsigned;
	int64_t minimum;
	uint64_t maximum;
};

inline constexpr Descriptor DESCRIPTORS[] = {
	{ NumericType::INT32, "int", false, INT32_MIN, uint64_t(INT32_MAX) },
	{ NumericType::UINT32, "uint", true, 0, uint64_t(UINT32_MAX) },
	{ NumericType::INT64, "long", false, INT64_MIN, uint64_t(INT64_MAX) },
	{ NumericType::UINT64, "ulong", true, 0, UINT64_MAX },
};

// A value on the descriptor's own carrier, so a helper never has to guess which storage it is in.
inline Variant carried(const Descriptor &p_descriptor, uint64_t p_magnitude) {
	return p_descriptor.is_unsigned ? Variant(p_magnitude) : Variant(int64_t(p_magnitude));
}

inline Variant maximum_of(const Descriptor &p_descriptor) {
	return p_descriptor.is_unsigned ? Variant(p_descriptor.maximum) : Variant(int64_t(p_descriptor.maximum));
}

inline Variant minimum_of(const Descriptor &p_descriptor) {
	return p_descriptor.is_unsigned ? Variant(uint64_t(0)) : Variant(p_descriptor.minimum);
}

inline Variant one_of(const Descriptor &p_descriptor) {
	return carried(p_descriptor, 1);
}

inline Variant zero_of(const Descriptor &p_descriptor) {
	return carried(p_descriptor, 0);
}

// The evaluated result, or `NONE` plus a failure reason. Bundled so a table row can state one
// expectation instead of threading three out-parameters through every check.
struct Outcome {
	bool succeeded = false;
	Variant value;
	FSNumericError error = FSNumericError::NONE;
};

inline Outcome evaluate(Variant::Operator p_operation, NumericType p_type, const Variant &p_left, const Variant &p_right) {
	Outcome outcome;
	outcome.succeeded = FSNumericOps::binary(p_operation, p_type, p_left, p_right, outcome.value, outcome.error);
	return outcome;
}

inline Outcome evaluate_unary(Variant::Operator p_operation, NumericType p_type, const Variant &p_value) {
	Outcome outcome;
	outcome.succeeded = FSNumericOps::unary(p_operation, p_type, p_value, outcome.value, outcome.error);
	return outcome;
}

inline Outcome convert(NumericType p_target, const Variant &p_value) {
	Outcome outcome;
	outcome.succeeded = FSNumericOps::convert(p_target, p_value, outcome.value, outcome.error);
	return outcome;
}

// Exact identity, so an `int` result is never accepted for a `uint` expectation. `Variant::operator==`
// compares integers across carriers, which is right for the language and wrong for this assertion.
inline bool is_exactly(const Variant &p_value, const Variant &p_expected) {
	return p_value.get_type() == p_expected.get_type() && p_value == p_expected;
}

TEST_CASE("[FoundryScript][NumericOps] Addition overflows at every descriptor maximum") {
	for (const Descriptor &descriptor : DESCRIPTORS) {
		INFO(descriptor.name);
		const Outcome overflowing = evaluate(Variant::OP_ADD, descriptor.type, maximum_of(descriptor), one_of(descriptor));
		CHECK_FALSE(overflowing.succeeded);
		CHECK(overflowing.error == FSNumericError::OVERFLOWED);

		const Outcome largest = evaluate(Variant::OP_ADD, descriptor.type, carried(descriptor, descriptor.maximum - 1), one_of(descriptor));
		CHECK(largest.succeeded);
		CHECK(is_exactly(largest.value, maximum_of(descriptor)));
	}
}

TEST_CASE("[FoundryScript][NumericOps] Subtraction overflows at every descriptor minimum") {
	for (const Descriptor &descriptor : DESCRIPTORS) {
		INFO(descriptor.name);
		const Outcome overflowing = evaluate(Variant::OP_SUBTRACT, descriptor.type, minimum_of(descriptor), one_of(descriptor));
		CHECK_FALSE(overflowing.succeeded);
		CHECK(overflowing.error == FSNumericError::OVERFLOWED);

		const Outcome smallest = evaluate(Variant::OP_SUBTRACT, descriptor.type, one_of(descriptor), one_of(descriptor));
		CHECK(smallest.succeeded);
		CHECK(is_exactly(smallest.value, zero_of(descriptor)));
	}
}

TEST_CASE("[FoundryScript][NumericOps] Multiplication overflows past every descriptor maximum") {
	for (const Descriptor &descriptor : DESCRIPTORS) {
		INFO(descriptor.name);
		const Outcome overflowing = evaluate(Variant::OP_MULTIPLY, descriptor.type, maximum_of(descriptor), carried(descriptor, 2));
		CHECK_FALSE(overflowing.succeeded);
		CHECK(overflowing.error == FSNumericError::OVERFLOWED);

		const Outcome exact = evaluate(Variant::OP_MULTIPLY, descriptor.type, carried(descriptor, descriptor.maximum / 2), carried(descriptor, 2));
		CHECK(exact.succeeded);
		CHECK(is_exactly(exact.value, carried(descriptor, (descriptor.maximum / 2) * 2)));
	}
}

TEST_CASE("[FoundryScript][NumericOps] Division and remainder refuse a zero divisor") {
	for (const Descriptor &descriptor : DESCRIPTORS) {
		INFO(descriptor.name);
		for (Variant::Operator operation : { Variant::OP_DIVIDE, Variant::OP_MODULE }) {
			const Outcome outcome = evaluate(operation, descriptor.type, one_of(descriptor), zero_of(descriptor));
			CHECK_FALSE(outcome.succeeded);
			CHECK(outcome.error == FSNumericError::DIVISION_BY_ZERO);
		}

		const Outcome quotient = evaluate(Variant::OP_DIVIDE, descriptor.type, carried(descriptor, 9), carried(descriptor, 2));
		CHECK(quotient.succeeded);
		CHECK(is_exactly(quotient.value, carried(descriptor, 4)));

		const Outcome remainder = evaluate(Variant::OP_MODULE, descriptor.type, carried(descriptor, 9), carried(descriptor, 2));
		CHECK(remainder.succeeded);
		CHECK(is_exactly(remainder.value, carried(descriptor, 1)));
	}
}

TEST_CASE("[FoundryScript][NumericOps] A signed minimum divided by minus one overflows") {
	for (const Descriptor &descriptor : DESCRIPTORS) {
		if (descriptor.is_unsigned) {
			continue;
		}
		INFO(descriptor.name);
		const Variant minus_one = Variant(int64_t(-1));
		const Outcome quotient = evaluate(Variant::OP_DIVIDE, descriptor.type, minimum_of(descriptor), minus_one);
		CHECK_FALSE(quotient.succeeded);
		CHECK(quotient.error == FSNumericError::OVERFLOWED);

		// The remainder is mathematically zero even though the quotient has no representation.
		const Outcome remainder = evaluate(Variant::OP_MODULE, descriptor.type, minimum_of(descriptor), minus_one);
		CHECK(remainder.succeeded);
		CHECK(is_exactly(remainder.value, Variant(int64_t(0))));
	}
}

TEST_CASE("[FoundryScript][NumericOps] Exponentiation is exact and checked at every descriptor") {
	for (const Descriptor &descriptor : DESCRIPTORS) {
		INFO(descriptor.name);
		const Outcome cube = evaluate(Variant::OP_POWER, descriptor.type, carried(descriptor, 3), carried(descriptor, 3));
		CHECK(cube.succeeded);
		CHECK(is_exactly(cube.value, carried(descriptor, 27)));

		// Two raised to the descriptor's width always leaves it, one bit past the top of its range.
		const uint64_t width = uint64_t(numeric_type_bit_width(descriptor.type));
		const Outcome overflowing = evaluate(Variant::OP_POWER, descriptor.type, carried(descriptor, 2), carried(descriptor, width));
		CHECK_FALSE(overflowing.succeeded);
		CHECK(overflowing.error == FSNumericError::OVERFLOWED);

		// The largest representable power of two still folds exactly, which a double-based
		// exponentiation cannot promise above the 53-bit mantissa.
		const uint64_t highest = descriptor.is_unsigned ? width - 1 : width - 2;
		const Outcome largest = evaluate(Variant::OP_POWER, descriptor.type, carried(descriptor, 2), carried(descriptor, highest));
		CHECK(largest.succeeded);
		CHECK(is_exactly(largest.value, carried(descriptor, uint64_t(1) << highest)));
	}
}

TEST_CASE("[FoundryScript][NumericOps] Bitwise operations stay inside the declared width") {
	for (const Descriptor &descriptor : DESCRIPTORS) {
		INFO(descriptor.name);
		const Variant all_bits = maximum_of(descriptor);

		const Outcome conjunction = evaluate(Variant::OP_BIT_AND, descriptor.type, all_bits, carried(descriptor, 12));
		CHECK(conjunction.succeeded);
		CHECK(is_exactly(conjunction.value, carried(descriptor, 12)));

		const Outcome disjunction = evaluate(Variant::OP_BIT_OR, descriptor.type, carried(descriptor, 8), carried(descriptor, 4));
		CHECK(disjunction.succeeded);
		CHECK(is_exactly(disjunction.value, carried(descriptor, 12)));

		const Outcome exclusive = evaluate(Variant::OP_BIT_XOR, descriptor.type, carried(descriptor, 12), carried(descriptor, 10));
		CHECK(exclusive.succeeded);
		CHECK(is_exactly(exclusive.value, carried(descriptor, 6)));

		// Complement flips exactly the bits the descriptor has, so the complement of zero is the
		// descriptor's maximum when unsigned and minus one when signed.
		const Outcome complement = evaluate_unary(Variant::OP_BIT_NEGATE, descriptor.type, zero_of(descriptor));
		CHECK(complement.succeeded);
		CHECK(is_exactly(complement.value, descriptor.is_unsigned ? maximum_of(descriptor) : Variant(int64_t(-1))));
	}
}

TEST_CASE("[FoundryScript][NumericOps] Shifts validate the count against the declared width") {
	for (const Descriptor &descriptor : DESCRIPTORS) {
		INFO(descriptor.name);
		const uint64_t width = uint64_t(numeric_type_bit_width(descriptor.type));

		const Outcome too_wide = evaluate(Variant::OP_SHIFT_LEFT, descriptor.type, one_of(descriptor), carried(descriptor, width));
		CHECK_FALSE(too_wide.succeeded);
		CHECK(too_wide.error == FSNumericError::INVALID_SHIFT);

		if (!descriptor.is_unsigned) {
			const Outcome negative_count = evaluate(Variant::OP_SHIFT_LEFT, descriptor.type, one_of(descriptor), Variant(int64_t(-1)));
			CHECK_FALSE(negative_count.succeeded);
			CHECK(negative_count.error == FSNumericError::INVALID_SHIFT);
		}

		// The highest count that still fits: one bit below the sign bit when signed, the top bit when
		// unsigned.
		const uint64_t highest = descriptor.is_unsigned ? width - 1 : width - 2;
		const Outcome highest_bit = evaluate(Variant::OP_SHIFT_LEFT, descriptor.type, one_of(descriptor), carried(descriptor, highest));
		CHECK(highest_bit.succeeded);
		CHECK(is_exactly(highest_bit.value, carried(descriptor, uint64_t(1) << highest)));

		// A count the width accepts can still shift a bit out of the range, which is an overflow and
		// not an invalid count. A signed type loses its top bit to the sign; an unsigned one needs a
		// second bit to have anything to lose.
		const Outcome shifted_out = evaluate(Variant::OP_SHIFT_LEFT, descriptor.type,
				carried(descriptor, descriptor.is_unsigned ? 3 : 1), carried(descriptor, width - 1));
		CHECK_FALSE(shifted_out.succeeded);
		CHECK(shifted_out.error == FSNumericError::OVERFLOWED);

		const Outcome halved = evaluate(Variant::OP_SHIFT_RIGHT, descriptor.type, carried(descriptor, 8), carried(descriptor, 2));
		CHECK(halved.succeeded);
		CHECK(is_exactly(halved.value, carried(descriptor, 2)));
	}
}

TEST_CASE("[FoundryScript][NumericOps] A signed right shift keeps the sign") {
	for (const Descriptor &descriptor : DESCRIPTORS) {
		if (descriptor.is_unsigned) {
			continue;
		}
		INFO(descriptor.name);
		const Outcome shifted = evaluate(Variant::OP_SHIFT_RIGHT, descriptor.type, Variant(int64_t(-8)), Variant(int64_t(2)));
		CHECK(shifted.succeeded);
		CHECK(is_exactly(shifted.value, Variant(int64_t(-2))));
	}
}

TEST_CASE("[FoundryScript][NumericOps] Negation is checked for signed and undefined for unsigned") {
	for (const Descriptor &descriptor : DESCRIPTORS) {
		INFO(descriptor.name);
		const Outcome negated = evaluate_unary(Variant::OP_NEGATE, descriptor.type, one_of(descriptor));
		if (descriptor.is_unsigned) {
			CHECK_FALSE(negated.succeeded);
			CHECK(negated.error == FSNumericError::UNSUPPORTED);
			continue;
		}
		CHECK(negated.succeeded);
		CHECK(is_exactly(negated.value, Variant(int64_t(-1))));

		const Outcome minimum = evaluate_unary(Variant::OP_NEGATE, descriptor.type, minimum_of(descriptor));
		CHECK_FALSE(minimum.succeeded);
		CHECK(minimum.error == FSNumericError::OVERFLOWED);
	}
}

TEST_CASE("[FoundryScript][NumericOps] Absolute value overflows only at a signed minimum") {
	for (const Descriptor &descriptor : DESCRIPTORS) {
		INFO(descriptor.name);
		Variant result;
		FSNumericError error = FSNumericError::NONE;

		CHECK(FSNumericOps::absolute(descriptor.type, maximum_of(descriptor), result, error));
		CHECK(is_exactly(result, maximum_of(descriptor)));

		if (descriptor.is_unsigned) {
			continue;
		}
		CHECK(FSNumericOps::absolute(descriptor.type, Variant(int64_t(-7)), result, error));
		CHECK(is_exactly(result, Variant(int64_t(7))));

		CHECK_FALSE(FSNumericOps::absolute(descriptor.type, minimum_of(descriptor), result, error));
		CHECK(error == FSNumericError::OVERFLOWED);
	}
}

TEST_CASE("[FoundryScript][NumericOps] Conversion accepts exactly the representable integers") {
	for (const Descriptor &descriptor : DESCRIPTORS) {
		INFO(descriptor.name);

		const Outcome at_maximum = convert(descriptor.type, maximum_of(descriptor));
		CHECK(at_maximum.succeeded);
		CHECK(is_exactly(at_maximum.value, maximum_of(descriptor)));

		// One past the maximum, expressed on whichever carrier can still hold it.
		if (descriptor.maximum < UINT64_MAX) {
			const Outcome past_maximum = convert(descriptor.type, Variant(descriptor.maximum + 1));
			CHECK_FALSE(past_maximum.succeeded);
			CHECK(past_maximum.error == FSNumericError::OUT_OF_RANGE);
		}

		const Outcome negative = convert(descriptor.type, Variant(int64_t(-1)));
		if (descriptor.is_unsigned) {
			CHECK_FALSE(negative.succeeded);
			CHECK(negative.error == FSNumericError::OUT_OF_RANGE);
		} else {
			CHECK(negative.succeeded);
			CHECK(is_exactly(negative.value, Variant(int64_t(-1))));
		}

		// A value crossing carriers keeps its magnitude and lands on the destination's carrier.
		const Outcome crossed = convert(descriptor.type, descriptor.is_unsigned ? Variant(int64_t(7)) : Variant(uint64_t(7)));
		CHECK(crossed.succeeded);
		CHECK(is_exactly(crossed.value, carried(descriptor, 7)));
	}
}

TEST_CASE("[FoundryScript][NumericOps] Float conversion truncates toward zero and refuses the rest") {
	for (const Descriptor &descriptor : DESCRIPTORS) {
		INFO(descriptor.name);

		const Outcome truncated = convert(descriptor.type, Variant(3.9));
		CHECK(truncated.succeeded);
		CHECK(is_exactly(truncated.value, carried(descriptor, 3)));

		if (!descriptor.is_unsigned) {
			const Outcome toward_zero = convert(descriptor.type, Variant(-3.9));
			CHECK(toward_zero.succeeded);
			CHECK(is_exactly(toward_zero.value, Variant(int64_t(-3))));
		}

		for (double value : { Math::NaN, Math::INF, -Math::INF }) {
			const Outcome outcome = convert(descriptor.type, Variant(value));
			CHECK_FALSE(outcome.succeeded);
			CHECK(outcome.error == FSNumericError::NON_FINITE);
		}

		// Two raised to the descriptor's width is exactly representable as a double and is exactly one
		// past the top of an unsigned range, so no rounding decides this boundary.
		const uint32_t width = numeric_type_bit_width(descriptor.type);
		double limit = 1.0;
		for (uint32_t bit = 0; bit < (descriptor.is_unsigned ? width : width - 1); bit++) {
			limit *= 2.0;
		}
		const Outcome past_limit = convert(descriptor.type, Variant(limit));
		CHECK_FALSE(past_limit.succeeded);
		CHECK(past_limit.error == FSNumericError::OUT_OF_RANGE);

		// Half the limit is exactly representable at every width, so this stays a boundary statement
		// rather than one the double's spacing near the limit could decide.
		const Outcome inside_limit = convert(descriptor.type, Variant(limit / 2.0));
		CHECK(inside_limit.succeeded);
	}
}

TEST_CASE("[FoundryScript][NumericOps] A type test checks the carrier and the declared range") {
	// This is the `is int` / `is uint` / `is long` / `is ulong` rule: the narrow descriptors refine the
	// carrier by range, and the wide ones are satisfied by the carrier alone.
	CHECK(numeric_type_contains(NumericType::INT32, Variant(int64_t(INT32_MAX))));
	CHECK_FALSE(numeric_type_contains(NumericType::INT32, Variant(int64_t(INT32_MAX) + 1)));
	CHECK(numeric_type_contains(NumericType::INT32, Variant(int64_t(INT32_MIN))));
	CHECK_FALSE(numeric_type_contains(NumericType::INT32, Variant(int64_t(INT32_MIN) - 1)));
	CHECK_FALSE(numeric_type_contains(NumericType::INT32, Variant(uint64_t(1))));

	CHECK(numeric_type_contains(NumericType::UINT32, Variant(uint64_t(UINT32_MAX))));
	CHECK_FALSE(numeric_type_contains(NumericType::UINT32, Variant(uint64_t(UINT32_MAX) + 1)));
	CHECK_FALSE(numeric_type_contains(NumericType::UINT32, Variant(int64_t(1))));

	CHECK(numeric_type_contains(NumericType::INT64, Variant(int64_t(INT64_MIN))));
	CHECK(numeric_type_contains(NumericType::INT64, Variant(int64_t(INT64_MAX))));
	CHECK_FALSE(numeric_type_contains(NumericType::INT64, Variant(uint64_t(0))));

	CHECK(numeric_type_contains(NumericType::UINT64, Variant(UINT64_MAX)));
	CHECK_FALSE(numeric_type_contains(NumericType::UINT64, Variant(int64_t(0))));
}

TEST_CASE("[FoundryScript][NumericOps] An unconstrained slot is checked at its carrier's full range") {
	CHECK(FSNumericOps::operation_type(NumericType::NONE, Variant::INT) == NumericType::INT64);
	CHECK(FSNumericOps::operation_type(NumericType::NONE, Variant::UINT) == NumericType::UINT64);
	CHECK(FSNumericOps::operation_type(NumericType::UINT32, Variant::UINT) == NumericType::UINT32);
	// A declared width on the wrong carrier cannot constrain the values, so it degrades to the wide
	// answer instead of range-checking against the wrong signedness.
	CHECK(FSNumericOps::operation_type(NumericType::UINT32, Variant::INT) == NumericType::INT64);
	CHECK(FSNumericOps::operation_type(NumericType::NONE, Variant::FLOAT) == NumericType::NONE);
}

TEST_CASE("[FoundryScript][NumericOps] Comparisons stay with the generic evaluator") {
	CHECK_FALSE(FSNumericOps::handles_operation(Variant::OP_EQUAL));
	CHECK_FALSE(FSNumericOps::handles_operation(Variant::OP_LESS));
	CHECK_FALSE(FSNumericOps::handles_operation(Variant::OP_AND));
	CHECK(FSNumericOps::handles_operation(Variant::OP_ADD));
	CHECK(FSNumericOps::handles_operation(Variant::OP_SHIFT_LEFT));
}

TEST_CASE("[FoundryScript][NumericOps] A failure names the operation, the type, and the range") {
	const String overflow = FSNumericOps::describe_operation_error(FSNumericError::OVERFLOWED, Variant::OP_ADD, NumericType::UINT32);
	CHECK(overflow.contains("+"));
	CHECK(overflow.contains("uint"));
	CHECK(overflow.contains("0 to 4294967295"));

	const String shift = FSNumericOps::describe_operation_error(FSNumericError::INVALID_SHIFT, Variant::OP_SHIFT_LEFT, NumericType::UINT32);
	CHECK(shift.contains("0 to 31"));

	const String conversion = FSNumericOps::describe_conversion_error(FSNumericError::OUT_OF_RANGE, NumericType::INT32, Variant(int64_t(2147483648)), "int");
	CHECK(conversion.contains("2147483648"));
	CHECK(conversion.contains("int"));
	CHECK(conversion.contains("-2147483648 to 2147483647"));

	// An empty name falls back to the descriptor's own spelling, so a caller with no source type to
	// quote still produces a complete message.
	CHECK(FSNumericOps::describe_conversion_error(FSNumericError::NON_FINITE, NumericType::UINT32, Variant(Math::NaN), String()).contains("uint"));

	CHECK(FSNumericOps::describe_range(NumericType::UINT64) == "0 to 18446744073709551615");
	CHECK(FSNumericOps::describe_operation_error(FSNumericError::UNSUPPORTED, Variant::OP_ADD, NumericType::INT32).is_empty());
}

} // namespace TestNumericOps
