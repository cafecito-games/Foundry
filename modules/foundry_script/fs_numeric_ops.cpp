/**************************************************************************/
/*  fs_numeric_ops.cpp                                                    */
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

#include "fs_numeric_ops.h"

#include "core/math/math_funcs.h"

namespace {

bool is_integer_carrier(Variant::Type p_carrier) {
	return p_carrier == Variant::INT || p_carrier == Variant::UINT;
}

// Two to the power of an exponent, computed exactly. Every power a 64-bit descriptor needs is far
// below the double exponent limit, so repeated doubling is exact and needs no library rounding mode.
double two_to_the(uint32_t p_exponent) {
	double result = 1.0;
	for (uint32_t index = 0; index < p_exponent; index++) {
		result *= 2.0;
	}
	return result;
}

// Every check below is written so the overflowing expression is never evaluated. `INT64_MAX - right`
// and `INT64_MIN - right` are themselves in range for the sign of `right` each branch tests, so the
// guard cannot overflow while proving the operation would.
bool signed_add(int64_t p_left, int64_t p_right, int64_t &r_result) {
	if (p_right > 0 && p_left > INT64_MAX - p_right) {
		return false;
	}
	if (p_right < 0 && p_left < INT64_MIN - p_right) {
		return false;
	}
	r_result = p_left + p_right;
	return true;
}

bool signed_subtract(int64_t p_left, int64_t p_right, int64_t &r_result) {
	if (p_right < 0 && p_left > INT64_MAX + p_right) {
		return false;
	}
	if (p_right > 0 && p_left < INT64_MIN + p_right) {
		return false;
	}
	r_result = p_left - p_right;
	return true;
}

bool signed_multiply(int64_t p_left, int64_t p_right, int64_t &r_result) {
	if (p_left == 0 || p_right == 0) {
		r_result = 0;
		return true;
	}
	// Negating the signed minimum is the one case division cannot express, because `INT64_MIN / -1`
	// overflows in exactly the same way, so both `-1` operands are answered before dividing.
	if (p_left == -1) {
		if (p_right == INT64_MIN) {
			return false;
		}
		r_result = -p_right;
		return true;
	}
	if (p_right == -1) {
		if (p_left == INT64_MIN) {
			return false;
		}
		r_result = -p_left;
		return true;
	}
	if (p_left > 0) {
		if (p_right > 0 ? p_left > INT64_MAX / p_right : p_right < INT64_MIN / p_left) {
			return false;
		}
	} else {
		if (p_right > 0 ? p_left < INT64_MIN / p_right : p_left < INT64_MAX / p_right) {
			return false;
		}
	}
	r_result = p_left * p_right;
	return true;
}

bool unsigned_add(uint64_t p_left, uint64_t p_right, uint64_t &r_result) {
	if (p_left > UINT64_MAX - p_right) {
		return false;
	}
	r_result = p_left + p_right;
	return true;
}

bool unsigned_subtract(uint64_t p_left, uint64_t p_right, uint64_t &r_result) {
	if (p_left < p_right) {
		return false;
	}
	r_result = p_left - p_right;
	return true;
}

bool unsigned_multiply(uint64_t p_left, uint64_t p_right, uint64_t &r_result) {
	if (p_left != 0 && p_right > UINT64_MAX / p_left) {
		return false;
	}
	r_result = p_left * p_right;
	return true;
}

// Exponentiation by squaring, refusing any intermediate that cannot be represented. Squaring happens
// only while a higher exponent bit is still pending, so a squaring that overflows always belongs to a
// factor the final result would have contained; a base of magnitude at most one never overflows at
// all. The check is therefore exact rather than conservative.
template <typename Value, typename Multiply>
bool checked_power(Value p_base, uint64_t p_exponent, Multiply p_multiply, Value &r_result) {
	Value result = 1;
	Value base = p_base;
	uint64_t exponent = p_exponent;
	while (exponent > 0) {
		if ((exponent & 1) != 0 && !p_multiply(result, base, result)) {
			return false;
		}
		exponent >>= 1;
		if (exponent > 0 && !p_multiply(base, base, base)) {
			return false;
		}
	}
	r_result = result;
	return true;
}

bool signed_fits(NumericType p_type, int64_t p_value) {
	const int64_t minimum = numeric_type_minimum(p_type);
	const uint64_t maximum = numeric_type_maximum(p_type);
	if (p_value < minimum) {
		return false;
	}
	return p_value < 0 || uint64_t(p_value) <= maximum;
}

bool unsigned_fits(NumericType p_type, uint64_t p_value) {
	return p_value <= numeric_type_maximum(p_type);
}

// The shift count, or a negative marker when the count is not a usable non-negative integer.
int64_t shift_count(const Variant &p_value) {
	const Variant::Type carrier = p_value.get_type();
	if (carrier == Variant::UINT) {
		const uint64_t count = p_value.operator uint64_t();
		return count > 64 ? 65 : int64_t(count);
	}
	if (carrier == Variant::INT) {
		return p_value.operator int64_t();
	}
	return -1;
}

String type_display_name(NumericType p_type) {
	return numeric_type_has_public_name(p_type) ? numeric_type_public_name(p_type) : numeric_type_name(p_type);
}

} // namespace

bool FSNumericOps::handles_operation(Variant::Operator p_operation) {
	switch (p_operation) {
		case Variant::OP_ADD:
		case Variant::OP_SUBTRACT:
		case Variant::OP_MULTIPLY:
		case Variant::OP_DIVIDE:
		case Variant::OP_MODULE:
		case Variant::OP_POWER:
		case Variant::OP_NEGATE:
		case Variant::OP_POSITIVE:
		case Variant::OP_SHIFT_LEFT:
		case Variant::OP_SHIFT_RIGHT:
		case Variant::OP_BIT_AND:
		case Variant::OP_BIT_OR:
		case Variant::OP_BIT_XOR:
		case Variant::OP_BIT_NEGATE:
			return true;
		default:
			return false;
	}
}

NumericType FSNumericOps::operation_type(NumericType p_declared, Variant::Type p_carrier) {
	if (!is_integer_carrier(p_carrier)) {
		return NumericType::NONE;
	}
	if (p_declared == NumericType::NONE || !numeric_type_is_carrier_consistent(p_declared, p_carrier)) {
		return numeric_type_wide_for_carrier(p_carrier);
	}
	return p_declared;
}

Variant FSNumericOps::zero(NumericType p_type) {
	return numeric_type_is_unsigned(p_type) ? Variant(uint64_t(0)) : Variant(int64_t(0));
}

bool FSNumericOps::binary(Variant::Operator p_operation, NumericType p_type, const Variant &p_left,
		const Variant &p_right, Variant &r_result, FSNumericError &r_error) {
	r_error = FSNumericError::UNSUPPORTED;
	if (!handles_operation(p_operation) || !numeric_type_is_valid(p_type) || p_type == NumericType::NONE) {
		return false;
	}
	if (!is_integer_carrier(p_left.get_type()) || !is_integer_carrier(p_right.get_type())) {
		return false;
	}
	if (!numeric_type_is_carrier_consistent(p_type, p_left.get_type())) {
		return false;
	}

	const bool is_unsigned = numeric_type_is_unsigned(p_type);
	const uint32_t width = numeric_type_bit_width(p_type);

	if (p_operation == Variant::OP_SHIFT_LEFT || p_operation == Variant::OP_SHIFT_RIGHT) {
		const int64_t count = shift_count(p_right);
		if (count < 0 || count >= int64_t(width)) {
			r_error = FSNumericError::INVALID_SHIFT;
			return false;
		}
		if (is_unsigned) {
			const uint64_t value = p_left.operator uint64_t();
			uint64_t result = 0;
			if (p_operation == Variant::OP_SHIFT_RIGHT) {
				result = value >> count;
			} else {
				result = value << count;
				if ((result >> count) != value) {
					r_error = FSNumericError::OVERFLOWED;
					return false;
				}
			}
			if (!unsigned_fits(p_type, result)) {
				r_error = FSNumericError::OVERFLOWED;
				return false;
			}
			r_error = FSNumericError::NONE;
			r_result = result;
			return true;
		}

		const int64_t value = p_left.operator int64_t();
		int64_t result = 0;
		if (p_operation == Variant::OP_SHIFT_RIGHT) {
			// An arithmetic shift never leaves the operand's range, so it needs no overflow check.
			result = value >> count;
		} else {
			// A left shift is a multiplication by a power of two, so it is checked as one rather than
			// shifting a signed value and inspecting the result afterwards.
			if (count >= 63) {
				if (value == 0) {
					result = 0;
				} else if (value == -1 && count == 63) {
					result = INT64_MIN;
				} else {
					r_error = FSNumericError::OVERFLOWED;
					return false;
				}
			} else if (!signed_multiply(value, int64_t(1) << count, result)) {
				r_error = FSNumericError::OVERFLOWED;
				return false;
			}
		}
		if (!signed_fits(p_type, result)) {
			r_error = FSNumericError::OVERFLOWED;
			return false;
		}
		r_error = FSNumericError::NONE;
		r_result = result;
		return true;
	}

	if (p_left.get_type() != p_right.get_type()) {
		// Only a shift takes an operand of a different kind. Everything else needs both operands on the
		// carrier the result is checked at; a mixed pair has no common integer type at all.
		return false;
	}

	if (is_unsigned) {
		const uint64_t left = p_left.operator uint64_t();
		const uint64_t right = p_right.operator uint64_t();
		uint64_t result = 0;
		bool representable = true;
		switch (p_operation) {
			case Variant::OP_ADD:
				representable = unsigned_add(left, right, result);
				break;
			case Variant::OP_SUBTRACT:
				representable = unsigned_subtract(left, right, result);
				break;
			case Variant::OP_MULTIPLY:
				representable = unsigned_multiply(left, right, result);
				break;
			case Variant::OP_DIVIDE:
			case Variant::OP_MODULE:
				if (right == 0) {
					r_error = FSNumericError::DIVISION_BY_ZERO;
					return false;
				}
				result = p_operation == Variant::OP_DIVIDE ? left / right : left % right;
				break;
			case Variant::OP_POWER:
				representable = checked_power<uint64_t>(left, right, unsigned_multiply, result);
				break;
			case Variant::OP_BIT_AND:
				result = left & right;
				break;
			case Variant::OP_BIT_OR:
				result = left | right;
				break;
			case Variant::OP_BIT_XOR:
				result = left ^ right;
				break;
			default:
				return false;
		}
		if (!representable || !unsigned_fits(p_type, result)) {
			r_error = FSNumericError::OVERFLOWED;
			return false;
		}
		r_error = FSNumericError::NONE;
		r_result = result;
		return true;
	}

	const int64_t left = p_left.operator int64_t();
	const int64_t right = p_right.operator int64_t();
	int64_t result = 0;
	bool representable = true;
	switch (p_operation) {
		case Variant::OP_ADD:
			representable = signed_add(left, right, result);
			break;
		case Variant::OP_SUBTRACT:
			representable = signed_subtract(left, right, result);
			break;
		case Variant::OP_MULTIPLY:
			representable = signed_multiply(left, right, result);
			break;
		case Variant::OP_DIVIDE:
		case Variant::OP_MODULE:
			if (right == 0) {
				r_error = FSNumericError::DIVISION_BY_ZERO;
				return false;
			}
			if (left == INT64_MIN && right == -1) {
				// The quotient has no representation and the remainder is zero, but the hardware
				// instruction traps on both, so neither is evaluated.
				if (p_operation == Variant::OP_DIVIDE) {
					r_error = FSNumericError::OVERFLOWED;
					return false;
				}
				result = 0;
				break;
			}
			result = p_operation == Variant::OP_DIVIDE ? left / right : left % right;
			break;
		case Variant::OP_POWER:
			if (right < 0) {
				// A negative exponent has an integer result only where the reciprocal is one: every
				// other magnitude truncates toward zero, and a zero base has no reciprocal at all.
				if (left == 0) {
					r_error = FSNumericError::DIVISION_BY_ZERO;
					return false;
				}
				result = left == 1 ? 1 : (left == -1 ? ((right & 1) != 0 ? -1 : 1) : 0);
				break;
			}
			representable = checked_power<int64_t>(left, uint64_t(right), signed_multiply, result);
			break;
		case Variant::OP_BIT_AND:
			result = left & right;
			break;
		case Variant::OP_BIT_OR:
			result = left | right;
			break;
		case Variant::OP_BIT_XOR:
			result = left ^ right;
			break;
		default:
			return false;
	}
	if (!representable || !signed_fits(p_type, result)) {
		r_error = FSNumericError::OVERFLOWED;
		return false;
	}
	r_error = FSNumericError::NONE;
	r_result = result;
	return true;
}

bool FSNumericOps::unary(Variant::Operator p_operation, NumericType p_type, const Variant &p_value,
		Variant &r_result, FSNumericError &r_error) {
	r_error = FSNumericError::UNSUPPORTED;
	if (!numeric_type_is_valid(p_type) || p_type == NumericType::NONE) {
		return false;
	}
	if (!is_integer_carrier(p_value.get_type()) || !numeric_type_is_carrier_consistent(p_type, p_value.get_type())) {
		return false;
	}

	const bool is_unsigned = numeric_type_is_unsigned(p_type);
	const uint32_t width = numeric_type_bit_width(p_type);
	const uint64_t width_mask = width >= 64 ? UINT64_MAX : ((uint64_t(1) << width) - 1);

	switch (p_operation) {
		case Variant::OP_POSITIVE:
			r_error = FSNumericError::NONE;
			r_result = p_value;
			return true;
		case Variant::OP_NEGATE: {
			if (is_unsigned) {
				// The unsigned carrier holds no negative value, so negation is not defined for it at
				// all rather than being an operation that happens to overflow.
				return false;
			}
			const int64_t value = p_value.operator int64_t();
			if (value == numeric_type_minimum(p_type)) {
				r_error = FSNumericError::OVERFLOWED;
				return false;
			}
			r_error = FSNumericError::NONE;
			r_result = -value;
			return true;
		}
		case Variant::OP_BIT_NEGATE: {
			// Complement is taken at the declared width, so a narrow type flips only the bits it has.
			if (is_unsigned) {
				const uint64_t bits = (~p_value.operator uint64_t()) & width_mask;
				r_error = FSNumericError::NONE;
				r_result = bits;
				return true;
			}
			const uint64_t bits = (~uint64_t(p_value.operator int64_t())) & width_mask;
			int64_t result = 0;
			if (width < 64 && (bits & (uint64_t(1) << (width - 1))) != 0) {
				result = int64_t(bits | ~width_mask);
			} else {
				result = int64_t(bits);
			}
			r_error = FSNumericError::NONE;
			r_result = result;
			return true;
		}
		default:
			return false;
	}
}

bool FSNumericOps::absolute(NumericType p_type, const Variant &p_value, Variant &r_result, FSNumericError &r_error) {
	r_error = FSNumericError::UNSUPPORTED;
	if (!numeric_type_is_valid(p_type) || p_type == NumericType::NONE) {
		return false;
	}
	if (!is_integer_carrier(p_value.get_type()) || !numeric_type_is_carrier_consistent(p_type, p_value.get_type())) {
		return false;
	}
	if (numeric_type_is_unsigned(p_type)) {
		r_error = FSNumericError::NONE;
		r_result = p_value;
		return true;
	}
	const int64_t value = p_value.operator int64_t();
	if (value >= 0) {
		r_error = FSNumericError::NONE;
		r_result = value;
		return true;
	}
	if (value == numeric_type_minimum(p_type)) {
		// The signed minimum has a larger magnitude than the maximum, so its absolute value is exactly
		// the one integer the type cannot hold.
		r_error = FSNumericError::OVERFLOWED;
		return false;
	}
	r_error = FSNumericError::NONE;
	r_result = -value;
	return true;
}

bool FSNumericOps::convert(NumericType p_target, const Variant &p_value, Variant &r_result, FSNumericError &r_error) {
	r_error = FSNumericError::UNSUPPORTED;
	if (!numeric_type_is_valid(p_target) || p_target == NumericType::NONE) {
		return false;
	}

	const bool target_is_unsigned = numeric_type_is_unsigned(p_target);
	const Variant::Type carrier = p_value.get_type();

	if (carrier == Variant::FLOAT) {
		const double value = p_value.operator double();
		if (!Math::is_finite(value)) {
			r_error = FSNumericError::NON_FINITE;
			return false;
		}
		// Truncation toward zero is the existing rule and is kept; what changes is that a value the
		// destination cannot hold is refused instead of converted through undefined behavior.
		const double truncated = value < 0.0 ? Math::ceil(value) : Math::floor(value);
		const uint32_t width = numeric_type_bit_width(p_target);
		if (target_is_unsigned) {
			if (truncated < 0.0 || truncated >= two_to_the(width)) {
				r_error = FSNumericError::OUT_OF_RANGE;
				return false;
			}
			r_error = FSNumericError::NONE;
			r_result = uint64_t(truncated);
			return true;
		}
		const double limit = two_to_the(width - 1);
		if (truncated < -limit || truncated >= limit) {
			r_error = FSNumericError::OUT_OF_RANGE;
			return false;
		}
		r_error = FSNumericError::NONE;
		r_result = int64_t(truncated);
		return true;
	}

	if (!is_integer_carrier(carrier)) {
		return false;
	}

	if (carrier == Variant::UINT) {
		const uint64_t value = p_value.operator uint64_t();
		if (value > numeric_type_maximum(p_target)) {
			r_error = FSNumericError::OUT_OF_RANGE;
			return false;
		}
		r_error = FSNumericError::NONE;
		r_result = target_is_unsigned ? Variant(value) : Variant(int64_t(value));
		return true;
	}

	const int64_t value = p_value.operator int64_t();
	if (!signed_fits(p_target, value)) {
		r_error = FSNumericError::OUT_OF_RANGE;
		return false;
	}
	r_error = FSNumericError::NONE;
	r_result = target_is_unsigned ? Variant(uint64_t(value)) : Variant(value);
	return true;
}

String FSNumericOps::describe_range(NumericType p_type) {
	if (!numeric_type_is_valid(p_type) || p_type == NumericType::NONE) {
		return String();
	}
	return itos(numeric_type_minimum(p_type)) + " to " + String::num_uint64(numeric_type_maximum(p_type));
}

String FSNumericOps::describe_operation_error(FSNumericError p_error, Variant::Operator p_operation, NumericType p_type) {
	const String operation_name = Variant::get_operator_name(p_operation);
	const String type_name = type_display_name(p_type);
	switch (p_error) {
		case FSNumericError::OVERFLOWED:
			return vformat(R"(The "%s" operator overflows "%s": the result is outside its range %s.)",
					operation_name, type_name, describe_range(p_type));
		case FSNumericError::DIVISION_BY_ZERO:
			return vformat(R"(The "%s" operator on "%s" has a divisor of zero.)", operation_name, type_name);
		case FSNumericError::INVALID_SHIFT:
			return vformat(R"(Invalid shift count for the "%s" operator on "%s": it must be from 0 to %d.)",
					operation_name, type_name, int(numeric_type_bit_width(p_type)) - 1);
		case FSNumericError::OUT_OF_RANGE:
			return vformat(R"(The "%s" operator on "%s" produced a value outside its range %s.)",
					operation_name, type_name, describe_range(p_type));
		case FSNumericError::NON_FINITE:
			return vformat(R"(The "%s" operator on "%s" received a value that is not finite.)", operation_name, type_name);
		case FSNumericError::NONE:
		case FSNumericError::UNSUPPORTED:
			break;
	}
	return String();
}

String FSNumericOps::describe_conversion_error(FSNumericError p_error, NumericType p_target, const Variant &p_value,
		const String &p_target_name) {
	const String type_name = p_target_name.is_empty() ? type_display_name(p_target) : p_target_name;
	switch (p_error) {
		case FSNumericError::OUT_OF_RANGE:
			return vformat(R"(Cannot convert %s to "%s": the value is outside its range %s.)",
					p_value.stringify(), type_name, describe_range(p_target));
		case FSNumericError::NON_FINITE:
			return vformat(R"(Cannot convert %s to "%s": it is not a finite number.)", p_value.stringify(), type_name);
		case FSNumericError::OVERFLOWED:
			return vformat(R"(Cannot convert %s to "%s": the value is outside its range %s.)",
					p_value.stringify(), type_name, describe_range(p_target));
		case FSNumericError::DIVISION_BY_ZERO:
		case FSNumericError::INVALID_SHIFT:
		case FSNumericError::NONE:
		case FSNumericError::UNSUPPORTED:
			break;
	}
	return String();
}
