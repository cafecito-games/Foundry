/**************************************************************************/
/*  numeric_type.h                                                        */
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

#include "core/error/error_macros.h"
#include "core/string/ustring.h"
#include "core/variant/variant.h"

// Exact integer width and signedness, tracked independently from the runtime Variant carrier.
//
// A carrier only distinguishes signed (`Variant::INT`) from unsigned (`Variant::UINT`) storage; it
// cannot tell a 32-bit constraint from a 64-bit one. This descriptor is the seam that typed
// containers, reflection metadata, and compiled type records use to keep that information.
//
// The values are an explicit wire contract: they are serialized into compiled metadata, so they must
// never be renumbered. Append new descriptors before `MAX` and give them the next free value.
enum class NumericType : uint8_t {
	NONE = 0,
	INT8 = 1,
	UINT8 = 2,
	INT16 = 3,
	UINT16 = 4,
	INT32 = 5,
	UINT32 = 6,
	INT64 = 7,
	UINT64 = 8,
	MAX = 9,
};

namespace NumericTypeInternal {

struct Row {
	Variant::Type carrier;
	bool is_signed;
	bool is_unsigned;
	uint32_t bit_width;
	int64_t minimum;
	uint64_t maximum;
	const char *name;
	// Empty when the descriptor exists only as a native constraint and has no source spelling.
	const char *public_name;
};

// One row per descriptor, in declaration order. `MAX` bounds the table, so adding a descriptor
// without a row is a compile error rather than a silently missing constraint.
inline constexpr Row ROWS[] = {
	// A carrier-neutral descriptor constrains nothing, so its range is the union of both carriers.
	{ Variant::NIL, false, false, 0, INT64_MIN, UINT64_MAX, "none", "" },
	{ Variant::INT, true, false, 8, INT8_MIN, uint64_t(INT8_MAX), "int8", "" },
	{ Variant::UINT, false, true, 8, 0, uint64_t(UINT8_MAX), "uint8", "" },
	{ Variant::INT, true, false, 16, INT16_MIN, uint64_t(INT16_MAX), "int16", "" },
	{ Variant::UINT, false, true, 16, 0, uint64_t(UINT16_MAX), "uint16", "" },
	{ Variant::INT, true, false, 32, INT32_MIN, uint64_t(INT32_MAX), "int32", "int" },
	{ Variant::UINT, false, true, 32, 0, uint64_t(UINT32_MAX), "uint32", "uint" },
	{ Variant::INT, true, false, 64, INT64_MIN, uint64_t(INT64_MAX), "int64", "long" },
	{ Variant::UINT, false, true, 64, 0, UINT64_MAX, "uint64", "ulong" },
};

static_assert(sizeof(ROWS) / sizeof(ROWS[0]) == size_t(NumericType::MAX),
		"Every NumericType descriptor needs a row in the descriptor table.");

} // namespace NumericTypeInternal

// True for every real descriptor, including the carrier-neutral `NONE`, and false for the terminal
// `MAX` sentinel or any out-of-range byte decoded from external data.
_FORCE_INLINE_ bool numeric_type_is_valid(NumericType p_numeric_type) {
	return uint8_t(p_numeric_type) < uint8_t(NumericType::MAX);
}

#define _NUMERIC_TYPE_ROW_V(m_numeric_type, m_fallback)                                                     \
	ERR_FAIL_COND_V_MSG(!numeric_type_is_valid(m_numeric_type), m_fallback, "Invalid numeric type value."); \
	const NumericTypeInternal::Row &row = NumericTypeInternal::ROWS[uint8_t(m_numeric_type)]

// The Variant storage a value of this descriptor travels in. `NONE` is carrier-neutral and maps to
// `Variant::NIL` because it does not pin storage to either integer carrier.
_FORCE_INLINE_ Variant::Type numeric_type_carrier(NumericType p_numeric_type) {
	_NUMERIC_TYPE_ROW_V(p_numeric_type, Variant::NIL);
	return row.carrier;
}

// The single validator for descriptor/carrier agreement. `NONE` accepts any carrier; every other
// descriptor accepts only its own.
_FORCE_INLINE_ bool numeric_type_is_carrier_consistent(NumericType p_numeric_type, Variant::Type p_carrier) {
	if (!numeric_type_is_valid(p_numeric_type)) {
		return false;
	}
	if (p_numeric_type == NumericType::NONE) {
		return true;
	}
	return NumericTypeInternal::ROWS[uint8_t(p_numeric_type)].carrier == p_carrier;
}

// `NONE` is neither signed nor unsigned: its signedness is simply unspecified.
_FORCE_INLINE_ bool numeric_type_is_signed(NumericType p_numeric_type) {
	_NUMERIC_TYPE_ROW_V(p_numeric_type, false);
	return row.is_signed;
}

_FORCE_INLINE_ bool numeric_type_is_unsigned(NumericType p_numeric_type) {
	_NUMERIC_TYPE_ROW_V(p_numeric_type, false);
	return row.is_unsigned;
}

// Width in bits, or zero when the descriptor does not pin a width.
_FORCE_INLINE_ uint32_t numeric_type_bit_width(NumericType p_numeric_type) {
	_NUMERIC_TYPE_ROW_V(p_numeric_type, 0);
	return row.bit_width;
}

// Inclusive lower bound. Every descriptor's minimum fits `int64_t`, since unsigned ranges start at 0.
_FORCE_INLINE_ int64_t numeric_type_minimum(NumericType p_numeric_type) {
	_NUMERIC_TYPE_ROW_V(p_numeric_type, 0);
	return row.minimum;
}

// Inclusive upper bound. Every descriptor's maximum fits `uint64_t`, since signed maxima are positive.
_FORCE_INLINE_ uint64_t numeric_type_maximum(NumericType p_numeric_type) {
	_NUMERIC_TYPE_ROW_V(p_numeric_type, 0);
	return row.maximum;
}

// True when `p_value` is an integer whose carrier agrees with the descriptor and whose magnitude
// falls inside the descriptor's inclusive range. Signedness crossings are rejected rather than
// silently reinterpreted, so an in-range magnitude on the wrong carrier is still not contained.
_FORCE_INLINE_ bool numeric_type_contains(NumericType p_numeric_type, const Variant &p_value) {
	const Variant::Type value_carrier = p_value.get_type();
	if (value_carrier != Variant::INT && value_carrier != Variant::UINT) {
		return false;
	}
	if (!numeric_type_is_carrier_consistent(p_numeric_type, value_carrier)) {
		return false;
	}
	const NumericTypeInternal::Row &row = NumericTypeInternal::ROWS[uint8_t(p_numeric_type)];
	if (value_carrier == Variant::UINT) {
		return p_value.operator uint64_t() <= row.maximum;
	}
	const int64_t signed_value = p_value.operator int64_t();
	if (signed_value < row.minimum) {
		return false;
	}
	return signed_value < 0 || uint64_t(signed_value) <= row.maximum;
}

// Stable diagnostic name, distinct from the source spelling; empty for the `MAX` sentinel.
_FORCE_INLINE_ String numeric_type_name(NumericType p_numeric_type) {
	_NUMERIC_TYPE_ROW_V(p_numeric_type, String());
	return String(row.name);
}

// True only for descriptors that have a FoundryScript source spelling. The 8- and 16-bit
// descriptors exist for native signature constraints and are deliberately not source-nameable.
_FORCE_INLINE_ bool numeric_type_has_public_name(NumericType p_numeric_type) {
	if (!numeric_type_is_valid(p_numeric_type)) {
		return false;
	}
	return NumericTypeInternal::ROWS[uint8_t(p_numeric_type)].public_name[0] != '\0';
}

// The FoundryScript source spelling, or an empty string when the descriptor has none.
_FORCE_INLINE_ String numeric_type_public_name(NumericType p_numeric_type) {
	_NUMERIC_TYPE_ROW_V(p_numeric_type, String());
	return String(row.public_name);
}

// Symmetric agreement rule, for the places that compare two descriptions of the same slot: equality,
// invariant identity, and assignment compatibility.
//
// `NONE` is the absence of a width constraint, not an empty range, so it neither adds nor removes
// identity and agrees with every descriptor. Two declared widths agree only with themselves, which is
// what keeps `int` and `long` observably distinct while a slot that never declared a width keeps
// behaving exactly as it did before descriptors existed.
_FORCE_INLINE_ bool numeric_types_agree(NumericType p_left, NumericType p_right) {
	return p_left == NumericType::NONE || p_right == NumericType::NONE || p_left == p_right;
}

// Directional aliasing rule, for the places that hand out a reference to a slot rather than copy a
// value through it. Referencing skips per-value validation, so it must not be more permissive: an
// unconstrained slot accepts every value its carrier holds and can therefore alias any width on that
// carrier, but a slot that declared a width would let values it rejects in through an alias of any
// other descriptor -- including an unconstrained one.
_FORCE_INLINE_ bool numeric_type_can_alias(NumericType p_referencing, NumericType p_referenced) {
	return p_referencing == NumericType::NONE || p_referencing == p_referenced;
}

// The descriptor a genuinely width-erased value of `p_carrier` decodes to.
//
// A boundary that transports only the carrier (a plain `PropertyInfo`) loses the width, so the value
// that arrives may be anything the carrier can hold. Only the 64-bit descriptor covers that whole
// range, so decoding wide is the sole choice that cannot claim a constraint the value never had.
// Non-integer carriers pin no width at all.
_FORCE_INLINE_ NumericType numeric_type_wide_for_carrier(Variant::Type p_carrier) {
	if (p_carrier == Variant::INT) {
		return NumericType::INT64;
	}
	if (p_carrier == Variant::UINT) {
		return NumericType::UINT64;
	}
	return NumericType::NONE;
}

#undef _NUMERIC_TYPE_ROW_V
