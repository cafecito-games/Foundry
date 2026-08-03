/**************************************************************************/
/*  fs_numeric_ops.h                                                      */
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

#include "core/variant/numeric_type.h"
#include "core/variant/variant.h"

// Why a checked numeric operation or conversion refused to produce a value.
//
// The categories are deliberately distinct rather than one "invalid" outcome: each one has a
// different explanation for the author, and constant folding, the runtime, and typed boundaries all
// have to give the same explanation for the same cause.
enum class FSNumericError : uint8_t {
	// The operation succeeded. Also the value a caller should see whenever a helper returns `true`.
	NONE,
	// The mathematical result exists but does not fit the operation's result type.
	OVERFLOWED,
	// A division or remainder with a zero divisor.
	DIVISION_BY_ZERO,
	// A shift count that is negative or at least as large as the result type's width.
	INVALID_SHIFT,
	// A conversion whose source value lies outside the destination type's range.
	OUT_OF_RANGE,
	// A floating source that is NaN or infinite, so no integer value exists at all.
	NON_FINITE,
	// The operator/type combination is not part of the checked integer model. The caller's own type
	// rules decide what happens; this is not by itself a diagnosable error.
	UNSUPPORTED,
};

// The one implementation of what an integer operation or conversion means in FoundryScript.
//
// Constant folding, explicit casts, typed boundaries, and (later) the virtual machine all have to
// answer identically, so they all call these functions instead of evaluating C++ arithmetic
// themselves. Every operation is checked: an overflowing result is refused, never wrapped and never
// widened after the fact. Signed overflow is detected from the operands, before the overflowing
// expression is evaluated, because evaluating it first is already undefined behavior. Unsigned
// overflow is detected the same way, because C++ would otherwise wrap it silently and legally.
class FSNumericOps {
public:
	// Whether an operator is part of the checked integer model. Comparisons, logical operators, and
	// everything non-numeric are left to the generic evaluator, which already answers them exactly.
	static bool handles_operation(Variant::Operator p_operation);

	// The descriptor an operation is actually checked at.
	//
	// `NumericType::NONE` is the absence of a width constraint, not a fifth type, so an operation on
	// values that declare no width is checked at the widest range its carrier can hold. That is the
	// same rule the dynamic path uses, which is what keeps a width-erased value behaving as it always
	// has while still refusing a result the carrier itself cannot represent. A declared width that
	// disagrees with the carrier the values actually travel in cannot constrain them, so it degrades
	// to the same wide answer instead of range-checking against the wrong signedness.
	static NumericType operation_type(NumericType p_declared, Variant::Type p_carrier);

	// The zero of a descriptor, on that descriptor's carrier.
	static Variant zero(NumericType p_type);

	// Evaluates `p_left <operation> p_right` at `p_type`. Returns false and sets `r_error` when no
	// representable result exists; `r_result` is left untouched in that case.
	static bool binary(Variant::Operator p_operation, NumericType p_type, const Variant &p_left,
			const Variant &p_right, Variant &r_result, FSNumericError &r_error);

	// Evaluates a unary operator at `p_type`. Negation of an unsigned value is `UNSUPPORTED` rather
	// than an overflow: the unsigned carrier has no negative values at all, so there is nothing to
	// check.
	static bool unary(Variant::Operator p_operation, NumericType p_type, const Variant &p_value,
			Variant &r_result, FSNumericError &r_error);

	// Absolute value at `p_type`. A signed minimum has no positive counterpart, so it overflows
	// instead of negating into itself.
	static bool absolute(NumericType p_type, const Variant &p_value, Variant &r_result, FSNumericError &r_error);

	// Converts a value into `p_target`.
	//
	// An integer source must be mathematically representable in the destination; signedness crossings
	// are checked by value, not reinterpreted. A floating source truncates toward zero, and is refused
	// when it is not finite or when the truncated value lies outside the destination's range.
	static bool convert(NumericType p_target, const Variant &p_value, Variant &r_result, FSNumericError &r_error);

	// The inclusive range of a descriptor, rendered for a diagnostic.
	static String describe_range(NumericType p_type);

	// A diagnostic for a failed operation, naming the operator, the type it was checked at, and that
	// type's valid range. Empty for `NONE` and `UNSUPPORTED`, which are not diagnosable here.
	static String describe_operation_error(FSNumericError p_error, Variant::Operator p_operation, NumericType p_type);

	// A diagnostic for a failed conversion, naming the offending value, the destination, and the range
	// the destination was checked against. The destination is named by the caller rather than by the
	// descriptor, so a message quotes the type the author actually wrote. Empty for `NONE` and
	// `UNSUPPORTED`.
	static String describe_conversion_error(FSNumericError p_error, NumericType p_target, const Variant &p_value,
			const String &p_target_name);
};
