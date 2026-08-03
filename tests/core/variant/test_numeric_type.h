/**************************************************************************/
/*  test_numeric_type.h                                                   */
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
#include "core/variant/variant_internal.h"

#include "tests/test_macros.h"

namespace TestNumericType {

// The unsigned carrier has no nominal C++ type, so a `Variant(uint64_t)` still selects `INT`.
// Tests build genuine `UINT` values through the same storage entry point the engine uses.
static Variant make_unsigned_variant(uint64_t p_value) {
	Variant value;
	VariantInternal::initialize(&value, Variant::UINT);
	*VariantInternal::get_uint(&value) = p_value;
	return value;
}

struct DescriptorExpectation {
	NumericType numeric_type;
	uint8_t encoding;
	Variant::Type carrier;
	bool is_signed;
	bool is_unsigned;
	uint32_t bit_width;
	int64_t minimum;
	uint64_t maximum;
	const char *name;
	const char *public_name;
};

static const DescriptorExpectation DESCRIPTOR_EXPECTATIONS[] = {
	{ NumericType::NONE, 0, Variant::NIL, false, false, 0, INT64_MIN, UINT64_MAX, "none", "" },
	{ NumericType::INT8, 1, Variant::INT, true, false, 8, INT8_MIN, uint64_t(INT8_MAX), "int8", "" },
	{ NumericType::UINT8, 2, Variant::UINT, false, true, 8, 0, uint64_t(UINT8_MAX), "uint8", "" },
	{ NumericType::INT16, 3, Variant::INT, true, false, 16, INT16_MIN, uint64_t(INT16_MAX), "int16", "" },
	{ NumericType::UINT16, 4, Variant::UINT, false, true, 16, 0, uint64_t(UINT16_MAX), "uint16", "" },
	{ NumericType::INT32, 5, Variant::INT, true, false, 32, INT32_MIN, uint64_t(INT32_MAX), "int32", "int" },
	{ NumericType::UINT32, 6, Variant::UINT, false, true, 32, 0, uint64_t(UINT32_MAX), "uint32", "uint" },
	{ NumericType::INT64, 7, Variant::INT, true, false, 64, INT64_MIN, uint64_t(INT64_MAX), "int64", "long" },
	{ NumericType::UINT64, 8, Variant::UINT, false, true, 64, 0, UINT64_MAX, "uint64", "ulong" },
};

TEST_CASE("[Variant][NumericType] The descriptor encoding is an explicit wire contract") {
	// Serialized descriptors travel through bytecode and metadata, so these values must not drift.
	for (const DescriptorExpectation &expectation : DESCRIPTOR_EXPECTATIONS) {
		CHECK_EQ(uint8_t(expectation.numeric_type), expectation.encoding);
	}
	CHECK_EQ(uint8_t(NumericType::MAX), 9);
	CHECK_EQ(sizeof(NumericType), sizeof(uint8_t));
	CHECK_EQ(sizeof(DESCRIPTOR_EXPECTATIONS) / sizeof(DESCRIPTOR_EXPECTATIONS[0]), size_t(NumericType::MAX));
}

TEST_CASE("[Variant][NumericType] Every descriptor exposes its carrier, signedness, width, and names") {
	for (const DescriptorExpectation &expectation : DESCRIPTOR_EXPECTATIONS) {
		CAPTURE(expectation.name);
		CHECK(numeric_type_is_valid(expectation.numeric_type));
		CHECK_EQ(numeric_type_carrier(expectation.numeric_type), expectation.carrier);
		CHECK_EQ(numeric_type_is_signed(expectation.numeric_type), expectation.is_signed);
		CHECK_EQ(numeric_type_is_unsigned(expectation.numeric_type), expectation.is_unsigned);
		CHECK_EQ(numeric_type_bit_width(expectation.numeric_type), expectation.bit_width);
		CHECK_EQ(numeric_type_minimum(expectation.numeric_type), expectation.minimum);
		CHECK_EQ(numeric_type_maximum(expectation.numeric_type), expectation.maximum);
		CHECK_EQ(numeric_type_name(expectation.numeric_type), String(expectation.name));
		CHECK_EQ(numeric_type_public_name(expectation.numeric_type), String(expectation.public_name));
		CHECK_EQ(numeric_type_has_public_name(expectation.numeric_type), String(expectation.public_name) != String());
	}
}

TEST_CASE("[Variant][NumericType] The terminal MAX sentinel is not a usable descriptor") {
	ERR_PRINT_OFF;
	CHECK_FALSE(numeric_type_is_valid(NumericType::MAX));
	CHECK_EQ(numeric_type_carrier(NumericType::MAX), Variant::NIL);
	CHECK_FALSE(numeric_type_is_signed(NumericType::MAX));
	CHECK_FALSE(numeric_type_is_unsigned(NumericType::MAX));
	CHECK_EQ(numeric_type_bit_width(NumericType::MAX), 0);
	CHECK_EQ(numeric_type_name(NumericType::MAX), String());
	CHECK_FALSE(numeric_type_has_public_name(NumericType::MAX));
	CHECK_FALSE(numeric_type_contains(NumericType::MAX, Variant(int64_t(0))));
	CHECK_FALSE(numeric_type_is_carrier_consistent(NumericType::MAX, Variant::INT));
	CHECK_FALSE(numeric_type_is_carrier_consistent(NumericType::MAX, Variant::UINT));
	ERR_PRINT_ON;
}

TEST_CASE("[Variant][NumericType] Only 32- and 64-bit descriptors have public source names") {
	CHECK_EQ(numeric_type_public_name(NumericType::INT32), String("int"));
	CHECK_EQ(numeric_type_public_name(NumericType::UINT32), String("uint"));
	CHECK_EQ(numeric_type_public_name(NumericType::INT64), String("long"));
	CHECK_EQ(numeric_type_public_name(NumericType::UINT64), String("ulong"));

	// The narrow descriptors exist for native signature constraints only.
	const NumericType narrow[] = { NumericType::INT8, NumericType::UINT8, NumericType::INT16, NumericType::UINT16 };
	for (NumericType numeric_type : narrow) {
		CHECK_FALSE(numeric_type_has_public_name(numeric_type));
		CHECK_EQ(numeric_type_public_name(numeric_type), String());
		CHECK_FALSE(numeric_type_name(numeric_type).is_empty());
	}
	CHECK_FALSE(numeric_type_has_public_name(NumericType::NONE));
}

TEST_CASE("[Variant][NumericType] One shared validator rejects inconsistent descriptor/carrier pairs") {
	for (const DescriptorExpectation &expectation : DESCRIPTOR_EXPECTATIONS) {
		CAPTURE(expectation.name);
		if (expectation.numeric_type == NumericType::NONE) {
			// A carrier-neutral descriptor constrains nothing.
			CHECK(numeric_type_is_carrier_consistent(NumericType::NONE, Variant::INT));
			CHECK(numeric_type_is_carrier_consistent(NumericType::NONE, Variant::UINT));
			CHECK(numeric_type_is_carrier_consistent(NumericType::NONE, Variant::STRING));
			continue;
		}
		CHECK(numeric_type_is_carrier_consistent(expectation.numeric_type, expectation.carrier));
		const Variant::Type opposite = expectation.is_signed ? Variant::UINT : Variant::INT;
		CHECK_FALSE(numeric_type_is_carrier_consistent(expectation.numeric_type, opposite));
		CHECK_FALSE(numeric_type_is_carrier_consistent(expectation.numeric_type, Variant::FLOAT));
		CHECK_FALSE(numeric_type_is_carrier_consistent(expectation.numeric_type, Variant::NIL));
	}
}

TEST_CASE("[Variant][NumericType] Containment accepts each descriptor's exact boundaries") {
	for (const DescriptorExpectation &expectation : DESCRIPTOR_EXPECTATIONS) {
		CAPTURE(expectation.name);
		if (expectation.is_unsigned) {
			CHECK(numeric_type_contains(expectation.numeric_type, make_unsigned_variant(0)));
			CHECK(numeric_type_contains(expectation.numeric_type, make_unsigned_variant(expectation.maximum)));
			if (expectation.maximum != UINT64_MAX) {
				CHECK_FALSE(numeric_type_contains(expectation.numeric_type, make_unsigned_variant(expectation.maximum + 1)));
			}
		} else if (expectation.is_signed) {
			CHECK(numeric_type_contains(expectation.numeric_type, Variant(expectation.minimum)));
			CHECK(numeric_type_contains(expectation.numeric_type, Variant(int64_t(expectation.maximum))));
			CHECK(numeric_type_contains(expectation.numeric_type, Variant(int64_t(0))));
			if (expectation.minimum != INT64_MIN) {
				CHECK_FALSE(numeric_type_contains(expectation.numeric_type, Variant(expectation.minimum - 1)));
			}
			if (expectation.maximum != uint64_t(INT64_MAX)) {
				CHECK_FALSE(numeric_type_contains(expectation.numeric_type, Variant(int64_t(expectation.maximum) + 1)));
			}
		}
	}
}

TEST_CASE("[Variant][NumericType] Unsigned descriptors reject negative and signed-carrier values") {
	const NumericType unsigned_types[] = { NumericType::UINT8, NumericType::UINT16, NumericType::UINT32, NumericType::UINT64 };
	for (NumericType numeric_type : unsigned_types) {
		CAPTURE(numeric_type_name(numeric_type).utf8().get_data());
		CHECK_FALSE(numeric_type_contains(numeric_type, Variant(int64_t(-1))));
		CHECK_FALSE(numeric_type_contains(numeric_type, Variant(INT64_MIN)));
		// Even an in-range magnitude is rejected when it arrives on the signed carrier.
		CHECK_FALSE(numeric_type_contains(numeric_type, Variant(int64_t(1))));
		CHECK(numeric_type_contains(numeric_type, make_unsigned_variant(1)));
	}
}

TEST_CASE("[Variant][NumericType] Signed descriptors reject unsigned-carrier values") {
	const NumericType signed_types[] = { NumericType::INT8, NumericType::INT16, NumericType::INT32, NumericType::INT64 };
	for (NumericType numeric_type : signed_types) {
		CAPTURE(numeric_type_name(numeric_type).utf8().get_data());
		CHECK_FALSE(numeric_type_contains(numeric_type, make_unsigned_variant(1)));
		CHECK_FALSE(numeric_type_contains(numeric_type, make_unsigned_variant(UINT64_MAX)));
	}
}

TEST_CASE("[Variant][NumericType] Values above the signed range still fit the wide unsigned descriptor") {
	// Magnitudes that fit `uint64` but not `int64` are exactly what the unsigned carrier exists for.
	const uint64_t above_signed_range = uint64_t(INT64_MAX) + 1;
	CHECK(numeric_type_contains(NumericType::UINT64, make_unsigned_variant(above_signed_range)));
	CHECK(numeric_type_contains(NumericType::UINT64, make_unsigned_variant(UINT64_MAX)));
	CHECK_FALSE(numeric_type_contains(NumericType::UINT32, make_unsigned_variant(above_signed_range)));
	CHECK(numeric_type_contains(NumericType::INT64, Variant(INT64_MAX)));
	CHECK(numeric_type_contains(NumericType::INT64, Variant(INT64_MIN)));
}

TEST_CASE("[Variant][NumericType] The carrier-neutral descriptor imposes no width constraint") {
	CHECK(numeric_type_contains(NumericType::NONE, Variant(INT64_MIN)));
	CHECK(numeric_type_contains(NumericType::NONE, Variant(INT64_MAX)));
	CHECK(numeric_type_contains(NumericType::NONE, Variant(int64_t(-1))));
	CHECK(numeric_type_contains(NumericType::NONE, make_unsigned_variant(0)));
	CHECK(numeric_type_contains(NumericType::NONE, make_unsigned_variant(UINT64_MAX)));
}

TEST_CASE("[Variant][NumericType] Containment rejects values that are not integers") {
	const NumericType all_types[] = { NumericType::NONE, NumericType::INT8, NumericType::UINT8,
		NumericType::INT16, NumericType::UINT16, NumericType::INT32, NumericType::UINT32,
		NumericType::INT64, NumericType::UINT64 };
	for (NumericType numeric_type : all_types) {
		CAPTURE(numeric_type_name(numeric_type).utf8().get_data());
		CHECK_FALSE(numeric_type_contains(numeric_type, Variant()));
		CHECK_FALSE(numeric_type_contains(numeric_type, Variant(true)));
		CHECK_FALSE(numeric_type_contains(numeric_type, Variant(1.0)));
		CHECK_FALSE(numeric_type_contains(numeric_type, Variant("1")));
	}
}

} // namespace TestNumericType
