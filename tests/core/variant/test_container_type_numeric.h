/**************************************************************************/
/*  test_container_type_numeric.h                                         */
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

#include "core/variant/array.h"
#include "core/variant/container_type_validate.h"
#include "core/variant/dictionary.h"
#include "core/variant/numeric_type.h"
#include "core/variant/variant_internal.h"

#include "tests/test_macros.h"

namespace TestContainerTypeNumeric {

// The unsigned carrier has no nominal C++ type, so `Variant(uint64_t)` still selects `INT`. Genuine
// `UINT` values are built through the same storage entry point the engine uses.
static Variant make_unsigned_variant(uint64_t p_value) {
	Variant value;
	VariantInternal::initialize(&value, Variant::UINT);
	*VariantInternal::get_uint(&value) = p_value;
	return value;
}

static ContainerType make_numeric_type(Variant::Type p_carrier, NumericType p_numeric_type) {
	ContainerType type;
	type.builtin_type = p_carrier;
	type.numeric_type = p_numeric_type;
	return type;
}

static ContainerType make_array_of(const ContainerType &p_element_type) {
	ContainerType type;
	type.builtin_type = Variant::ARRAY;
	type.element_types.push_back(p_element_type);
	return type;
}

static ContainerType make_dictionary_of(const ContainerType &p_key_type, const ContainerType &p_value_type) {
	ContainerType type;
	type.builtin_type = Variant::DICTIONARY;
	type.element_types.push_back(p_key_type);
	type.element_types.push_back(p_value_type);
	return type;
}

// Captures the last engine error message so rejection reasons can be asserted on directly.
struct NumericErrorRecorder {
	NumericErrorRecorder() {
		handler.errfunc = _record;
		handler.userdata = this;
		add_error_handler(&handler);
	}

	~NumericErrorRecorder() {
		remove_error_handler(&handler);
	}

	static void _record(void *p_self, const char *p_function, const char *p_file, int p_line, const char *p_error, const char *p_explanation, bool p_editor_notify, ErrorHandlerType p_type) {
		NumericErrorRecorder *self = static_cast<NumericErrorRecorder *>(p_self);
		self->last_message = String::utf8(p_explanation != nullptr && p_explanation[0] != '\0' ? p_explanation : p_error);
	}

	ErrorHandlerList handler;
	String last_message;
};

TEST_CASE("[ContainerType][NumericType] Descriptors of the same carrier compare unequal") {
	const ContainerType narrow = make_numeric_type(Variant::INT, NumericType::INT32);
	const ContainerType wide = make_numeric_type(Variant::INT, NumericType::INT64);
	const ContainerType narrow_unsigned = make_numeric_type(Variant::UINT, NumericType::UINT32);
	const ContainerType wide_unsigned = make_numeric_type(Variant::UINT, NumericType::UINT64);

	CHECK(narrow != wide);
	CHECK(narrow_unsigned != wide_unsigned);
	CHECK(narrow == make_numeric_type(Variant::INT, NumericType::INT32));
	CHECK(narrow != make_numeric_type(Variant::INT, NumericType::NONE));

	CHECK(make_array_of(narrow) != make_array_of(wide));
	CHECK(make_array_of(narrow_unsigned) != make_array_of(wide_unsigned));
	CHECK(make_array_of(narrow) == make_array_of(make_numeric_type(Variant::INT, NumericType::INT32)));

	// Key and value positions are independently significant.
	CHECK(make_dictionary_of(narrow, wide) != make_dictionary_of(wide, narrow));
	CHECK(make_dictionary_of(narrow, wide) != make_dictionary_of(narrow, narrow));
	CHECK(make_dictionary_of(narrow, wide) == make_dictionary_of(narrow, wide));

	CHECK(ContainerTypeValidate(narrow) != ContainerTypeValidate(wide));
	CHECK(ContainerTypeValidate(narrow) == ContainerTypeValidate(make_numeric_type(Variant::INT, NumericType::INT32)));
}

TEST_CASE("[ContainerType][NumericType] Descriptors round-trip through the validator") {
	const ContainerType nested = make_array_of(make_dictionary_of(
			make_numeric_type(Variant::UINT, NumericType::UINT32),
			make_array_of(make_numeric_type(Variant::INT, NumericType::INT64))));

	CHECK(ContainerTypeValidate(nested).get_container_type() == nested);
}

TEST_CASE("[ContainerType][NumericType] Type names render the declared numeric spelling") {
	CHECK_EQ(make_numeric_type(Variant::INT, NumericType::INT32).get_type_name(), "int");
	CHECK_EQ(make_numeric_type(Variant::INT, NumericType::INT64).get_type_name(), "long");
	CHECK_EQ(make_numeric_type(Variant::UINT, NumericType::UINT32).get_type_name(), "uint");
	CHECK_EQ(make_numeric_type(Variant::UINT, NumericType::UINT64).get_type_name(), "ulong");
	// A descriptor without a source spelling still renders a stable diagnostic name.
	CHECK_EQ(make_numeric_type(Variant::INT, NumericType::INT8).get_type_name(), "int8");
	// An unconstrained container keeps naming its carrier.
	CHECK_EQ(make_numeric_type(Variant::INT, NumericType::NONE).get_type_name(), "int");
	CHECK_EQ(make_numeric_type(Variant::UINT, NumericType::NONE).get_type_name(), "uint");

	CHECK_EQ(make_array_of(make_numeric_type(Variant::INT, NumericType::INT64)).get_type_name(), "Array[long]");
	CHECK_EQ(make_dictionary_of(make_numeric_type(Variant::UINT, NumericType::UINT32),
					 make_numeric_type(Variant::INT, NumericType::INT32))
					 .get_type_name(),
			"Dictionary[uint, int]");
}

TEST_CASE("[ContainerType][NumericType] Signed descriptors accept endpoints and reject one past") {
	const ContainerTypeValidate int32_validator(make_numeric_type(Variant::INT, NumericType::INT32));
	CHECK(int32_validator.test_validate(Variant(int64_t(INT32_MIN))));
	CHECK(int32_validator.test_validate(Variant(int64_t(INT32_MAX))));
	CHECK(int32_validator.test_validate(Variant(int64_t(0))));
	CHECK_FALSE(int32_validator.test_validate(Variant(int64_t(INT32_MIN) - 1)));
	CHECK_FALSE(int32_validator.test_validate(Variant(int64_t(INT32_MAX) + 1)));
	CHECK_FALSE(int32_validator.test_validate(Variant(int64_t(INT64_MIN))));
	CHECK_FALSE(int32_validator.test_validate(Variant(int64_t(INT64_MAX))));

	const ContainerTypeValidate int64_validator(make_numeric_type(Variant::INT, NumericType::INT64));
	CHECK(int64_validator.test_validate(Variant(int64_t(INT64_MIN))));
	CHECK(int64_validator.test_validate(Variant(int64_t(INT64_MAX))));
	CHECK(int64_validator.test_validate(Variant(int64_t(-1))));
}

TEST_CASE("[ContainerType][NumericType] Unsigned descriptors reject negatives and overflow") {
	const ContainerTypeValidate uint32_validator(make_numeric_type(Variant::UINT, NumericType::UINT32));
	CHECK(uint32_validator.test_validate(make_unsigned_variant(0)));
	CHECK(uint32_validator.test_validate(make_unsigned_variant(UINT32_MAX)));
	CHECK_FALSE(uint32_validator.test_validate(make_unsigned_variant(uint64_t(UINT32_MAX) + 1)));
	CHECK_FALSE(uint32_validator.test_validate(make_unsigned_variant(UINT64_MAX)));
	// A negative value has no unsigned representation, and it is not converted into one.
	CHECK_FALSE(uint32_validator.test_validate(Variant(int64_t(-1))));

	const ContainerTypeValidate uint64_validator(make_numeric_type(Variant::UINT, NumericType::UINT64));
	CHECK(uint64_validator.test_validate(make_unsigned_variant(UINT64_MAX)));
	// Magnitudes above `INT64_MAX` are exactly what the unsigned carrier exists for.
	CHECK(uint64_validator.test_validate(make_unsigned_variant(uint64_t(INT64_MAX) + 1)));
	CHECK_FALSE(uint64_validator.test_validate(Variant(int64_t(-1))));
}

TEST_CASE("[ContainerType][NumericType] Validation never crosses signedness") {
	const ContainerTypeValidate uint32_validator(make_numeric_type(Variant::UINT, NumericType::UINT32));
	const ContainerTypeValidate int32_validator(make_numeric_type(Variant::INT, NumericType::INT32));

	// In range for the magnitude, but on the wrong carrier in both directions.
	CHECK_FALSE(uint32_validator.test_validate(Variant(int64_t(1))));
	CHECK_FALSE(int32_validator.test_validate(make_unsigned_variant(1)));

	// The same holds without a descriptor: the carriers are distinct types.
	const ContainerTypeValidate unsigned_carrier(make_numeric_type(Variant::UINT, NumericType::NONE));
	const ContainerTypeValidate signed_carrier(make_numeric_type(Variant::INT, NumericType::NONE));
	CHECK_FALSE(unsigned_carrier.test_validate(Variant(int64_t(1))));
	CHECK_FALSE(signed_carrier.test_validate(make_unsigned_variant(1)));

	// A value that fits `uint64` but not `int64` is not smuggled into a signed slot.
	const ContainerTypeValidate int64_validator(make_numeric_type(Variant::INT, NumericType::INT64));
	CHECK_FALSE(int64_validator.test_validate(make_unsigned_variant(uint64_t(INT64_MAX) + 1)));
}

TEST_CASE("[ContainerType][NumericType] An unconstrained descriptor keeps carrier-only behavior") {
	const ContainerTypeValidate signed_carrier(make_numeric_type(Variant::INT, NumericType::NONE));
	CHECK(signed_carrier.test_validate(Variant(int64_t(INT64_MIN))));
	CHECK(signed_carrier.test_validate(Variant(int64_t(INT64_MAX))));
	// Strict conversions into the carrier are unaffected by the absence of a descriptor.
	CHECK(signed_carrier.test_validate(Variant(true)));

	const ContainerTypeValidate unsigned_carrier(make_numeric_type(Variant::UINT, NumericType::NONE));
	CHECK(unsigned_carrier.test_validate(make_unsigned_variant(UINT64_MAX)));
}

TEST_CASE("[ContainerType][NumericType] Range checking runs after carrier conversion") {
	const ContainerTypeValidate int32_validator(make_numeric_type(Variant::INT, NumericType::INT32));
	// `bool` and `float` still convert strictly into the signed carrier, then face the range.
	CHECK(int32_validator.test_validate(Variant(true)));
	CHECK(int32_validator.test_validate(Variant(3.9)));
	CHECK_FALSE(int32_validator.test_validate(Variant(1.0e18)));

	Variant converted = Variant(3.9);
	CHECK(int32_validator.validate(converted, "assign"));
	CHECK_EQ(converted.get_type(), Variant::INT);
	CHECK_EQ(int64_t(converted), 3);
}

TEST_CASE("[ContainerType][NumericType] Rejections name the declared type and its range") {
	NumericErrorRecorder recorder;

	const ContainerTypeValidate int32_validator(make_numeric_type(Variant::INT, NumericType::INT32));
	Variant too_large = Variant(int64_t(INT32_MAX) + 1);
	ERR_PRINT_OFF;
	CHECK_FALSE(int32_validator.validate(too_large, "assign"));
	ERR_PRINT_ON;
	CHECK(recorder.last_message.contains("'int'"));
	CHECK(recorder.last_message.contains("-2147483648"));
	CHECK(recorder.last_message.contains("2147483647"));

	const ContainerTypeValidate uint32_validator(make_numeric_type(Variant::UINT, NumericType::UINT32));
	Variant unsigned_too_large = make_unsigned_variant(uint64_t(UINT32_MAX) + 1);
	ERR_PRINT_OFF;
	CHECK_FALSE(uint32_validator.validate(unsigned_too_large, "assign"));
	ERR_PRINT_ON;
	CHECK(recorder.last_message.contains("'uint'"));
	CHECK(recorder.last_message.contains("4294967295"));

	// A carrier mismatch is reported before any range talk, and still names the declared type.
	Variant signed_value = Variant(int64_t(1));
	ERR_PRINT_OFF;
	CHECK_FALSE(uint32_validator.validate(signed_value, "assign"));
	ERR_PRINT_ON;
	CHECK(recorder.last_message.contains("'uint'"));
	CHECK_EQ(signed_value.get_type(), Variant::INT);
}

TEST_CASE("[ContainerType][NumericType] Typed Array preserves and enforces the descriptor") {
	Array values;
	values.set_typed(make_numeric_type(Variant::INT, NumericType::INT32));
	CHECK(values.get_element_type().numeric_type == NumericType::INT32);
	CHECK_EQ(values.get_element_type(), make_numeric_type(Variant::INT, NumericType::INT32));

	values.push_back(Variant(int64_t(INT32_MAX)));
	CHECK_EQ(values.size(), 1);
	ERR_PRINT_OFF;
	values.push_back(Variant(int64_t(INT32_MAX) + 1));
	ERR_PRINT_ON;
	CHECK_EQ(values.size(), 1);

	Array unsigned_values;
	unsigned_values.set_typed(make_numeric_type(Variant::UINT, NumericType::UINT32));
	unsigned_values.push_back(make_unsigned_variant(UINT32_MAX));
	CHECK_EQ(unsigned_values.size(), 1);
	ERR_PRINT_OFF;
	unsigned_values.push_back(Variant(int64_t(1)));
	unsigned_values.push_back(make_unsigned_variant(uint64_t(UINT32_MAX) + 1));
	ERR_PRINT_ON;
	CHECK_EQ(unsigned_values.size(), 1);

	// A carrier-inconsistent descriptor is refused rather than silently accepted.
	Array inconsistent;
	ERR_PRINT_OFF;
	inconsistent.set_typed(make_numeric_type(Variant::STRING, NumericType::INT32));
	ERR_PRINT_ON;
	CHECK_FALSE(inconsistent.is_typed());
}

TEST_CASE("[ContainerType][NumericType] Typed Dictionary preserves and enforces the descriptor") {
	Dictionary values;
	values.set_typed(make_numeric_type(Variant::UINT, NumericType::UINT32),
			make_numeric_type(Variant::INT, NumericType::INT32));
	CHECK(values.get_key_type().numeric_type == NumericType::UINT32);
	CHECK(values.get_value_type().numeric_type == NumericType::INT32);

	CHECK(values.set(make_unsigned_variant(7), Variant(int64_t(INT32_MIN))));
	CHECK_EQ(values.size(), 1);

	ERR_PRINT_OFF;
	// A signed key never crosses into the unsigned key slot.
	CHECK_FALSE(values.set(Variant(int64_t(7)), Variant(int64_t(0))));
	// An out-of-range key or value is rejected on its own merits.
	CHECK_FALSE(values.set(make_unsigned_variant(uint64_t(UINT32_MAX) + 1), Variant(int64_t(0))));
	CHECK_FALSE(values.set(make_unsigned_variant(8), Variant(int64_t(INT32_MAX) + 1)));
	ERR_PRINT_ON;
	CHECK_EQ(values.size(), 1);
}

TEST_CASE("[ContainerType][NumericType] Nested containers carry the descriptor through typed values") {
	Array outer;
	outer.set_typed(make_array_of(make_numeric_type(Variant::INT, NumericType::INT32)));

	Array inner;
	inner.set_typed(make_numeric_type(Variant::INT, NumericType::INT32));
	inner.push_back(Variant(int64_t(5)));
	outer.push_back(inner);
	CHECK_EQ(outer.size(), 1);
	const Array stored = outer[0];
	CHECK_EQ(stored.get_element_type(), make_numeric_type(Variant::INT, NumericType::INT32));

	// An untyped inner array is converted element-by-element against the declared descriptor.
	Array raw;
	raw.push_back(Variant(int64_t(1)));
	outer.push_back(raw);
	CHECK_EQ(outer.size(), 2);
	const Array converted = outer[1];
	CHECK_EQ(converted.get_element_type(), make_numeric_type(Variant::INT, NumericType::INT32));

	Array out_of_range;
	out_of_range.push_back(Variant(int64_t(INT32_MAX) + 1));
	ERR_PRINT_OFF;
	outer.push_back(out_of_range);
	ERR_PRINT_ON;
	CHECK_EQ(outer.size(), 2);
}

TEST_CASE("[ContainerType][NumericType] Width is invariant when referencing containers") {
	const ContainerTypeValidate narrow(make_array_of(make_numeric_type(Variant::INT, NumericType::INT32)));
	const ContainerTypeValidate wide(make_array_of(make_numeric_type(Variant::INT, NumericType::INT64)));
	const ContainerTypeValidate unconstrained(make_array_of(make_numeric_type(Variant::INT, NumericType::NONE)));

	CHECK_FALSE(narrow.can_reference(wide));
	CHECK_FALSE(wide.can_reference(narrow));
	CHECK(narrow.can_reference(ContainerTypeValidate(make_array_of(make_numeric_type(Variant::INT, NumericType::INT32)))));

	// An unconstrained slot accepts every value its carrier can hold, so it may alias a narrower one.
	CHECK(unconstrained.can_reference(narrow));
	CHECK_FALSE(narrow.can_reference(unconstrained));

	// An unconstrained slot aliases the source array in place, keeping its narrower element type.
	Array narrow_values;
	narrow_values.set_typed(make_numeric_type(Variant::INT, NumericType::INT32));
	Variant aliased = narrow_values;
	CHECK(unconstrained.validate(aliased, "assign"));
	CHECK_EQ(Array(aliased).get_element_type(), make_numeric_type(Variant::INT, NumericType::INT32));
	CHECK(Array(aliased).id() == narrow_values.id());

	// A differently-typed slot cannot alias it, so validation produces a converted copy instead.
	Variant reconverted = narrow_values;
	CHECK(wide.validate(reconverted, "assign"));
	CHECK_EQ(Array(reconverted).get_element_type(), make_numeric_type(Variant::INT, NumericType::INT64));
	CHECK(Array(reconverted).id() != narrow_values.id());
}

TEST_CASE("[ContainerType][NumericType] Container descriptors round-trip through variants") {
	ContainerType type_argument = make_numeric_type(Variant::UINT, NumericType::UINT64);
	ContainerType specialized;
	specialized.builtin_type = Variant::OBJECT;
	specialized.class_name = SNAME("RefCounted");
	specialized.type_arguments.push_back(type_argument);

	const ContainerType nested = make_array_of(make_dictionary_of(
			make_numeric_type(Variant::INT, NumericType::INT32),
			make_array_of(specialized)));

	ContainerType decoded;
	String error;
	CHECK(ContainerTypeDescriptor::from_variant(ContainerTypeDescriptor::to_variant(nested), decoded, &error));
	CHECK_EQ(error, String());
	CHECK_EQ(decoded, nested);
	CHECK(decoded.element_types[0].element_types[0].numeric_type == NumericType::INT32);
	CHECK(decoded.element_types[0].element_types[1].element_types[0].type_arguments[0].numeric_type == NumericType::UINT64);

	// An unconstrained descriptor is spelled exactly as it was before numeric types existed.
	const Dictionary plain = ContainerTypeDescriptor::to_variant(make_numeric_type(Variant::INT, NumericType::NONE));
	CHECK_FALSE(plain.has("numeric_type"));
}

TEST_CASE("[ContainerType][NumericType] Invalid descriptor payloads are rejected") {
	ContainerType decoded;
	String error;

	Dictionary wrong_kind;
	wrong_kind["type"] = Variant::INT;
	wrong_kind["numeric_type"] = "int32";
	CHECK_FALSE(ContainerTypeDescriptor::from_variant(wrong_kind, decoded, &error));
	CHECK_FALSE(error.is_empty());

	Dictionary out_of_range;
	out_of_range["type"] = Variant::INT;
	out_of_range["numeric_type"] = int64_t(NumericType::MAX);
	CHECK_FALSE(ContainerTypeDescriptor::from_variant(out_of_range, decoded, &error));
	CHECK_FALSE(error.is_empty());

	Dictionary negative;
	negative["type"] = Variant::INT;
	negative["numeric_type"] = -1;
	CHECK_FALSE(ContainerTypeDescriptor::from_variant(negative, decoded, &error));
	CHECK_FALSE(error.is_empty());

	Dictionary carrier_mismatch;
	carrier_mismatch["type"] = Variant::INT;
	carrier_mismatch["numeric_type"] = int64_t(NumericType::UINT32);
	CHECK_FALSE(ContainerTypeDescriptor::from_variant(carrier_mismatch, decoded, &error));
	CHECK_FALSE(error.is_empty());

	Dictionary non_numeric_carrier;
	non_numeric_carrier["type"] = Variant::STRING;
	non_numeric_carrier["numeric_type"] = int64_t(NumericType::INT32);
	CHECK_FALSE(ContainerTypeDescriptor::from_variant(non_numeric_carrier, decoded, &error));
	CHECK_FALSE(error.is_empty());

	// `NONE` is the absence of a constraint, so it stays legal on any carrier.
	Dictionary unconstrained;
	unconstrained["type"] = Variant::STRING;
	unconstrained["numeric_type"] = int64_t(NumericType::NONE);
	CHECK(ContainerTypeDescriptor::from_variant(unconstrained, decoded, &error));
	CHECK(decoded.numeric_type == NumericType::NONE);
}

} // namespace TestContainerTypeNumeric
