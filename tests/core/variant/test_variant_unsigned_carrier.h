/**************************************************************************/
/*  test_variant_unsigned_carrier.h                                       */
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

#include "core/object/object.h"
#include "core/os/time.h"
#include "core/variant/variant.h"

#include "tests/test_macros.h"

namespace TestVariantUnsignedCarrier {

TEST_CASE("[Variant] Unsigned C++ integers construct the unsigned carrier") {
	CHECK(Variant(uint8_t(7)).get_type() == Variant::UINT);
	CHECK(Variant(uint16_t(7)).get_type() == Variant::UINT);
	CHECK(Variant(uint32_t(7)).get_type() == Variant::UINT);
	CHECK(Variant(uint64_t(7)).get_type() == Variant::UINT);
	CHECK(Variant(Math::uint_alt_t(7)).get_type() == Variant::UINT);

	const Variant largest = uint64_t(UINT64_MAX);
	CHECK(largest.get_type() == Variant::UINT);
	CHECK(uint64_t(largest) == UINT64_MAX);
	CHECK(largest.stringify() == "18446744073709551615");
}

TEST_CASE("[Variant] Signed and boolean C++ integers keep their carrier") {
	CHECK(Variant(int8_t(-7)).get_type() == Variant::INT);
	CHECK(Variant(int16_t(-7)).get_type() == Variant::INT);
	CHECK(Variant(int32_t(-7)).get_type() == Variant::INT);
	CHECK(Variant(int64_t(-7)).get_type() == Variant::INT);
	CHECK(Variant(Math::int_alt_t(-7)).get_type() == Variant::INT);
	CHECK(Variant(true).get_type() == Variant::BOOL);
}

TEST_CASE("[Variant] Nominal unsigned wrappers keep the signed carrier") {
	Object *object = memnew(Object);
	const Variant instance_id = object->get_instance_id();
	CHECK(instance_id.get_type() == Variant::INT);
	CHECK(ObjectID(uint64_t(instance_id.operator int64_t())) == object->get_instance_id());
	memdelete(object);

	const Variant bit_field = BitField<PropertyUsageFlags>(PROPERTY_USAGE_STORAGE);
	CHECK(bit_field.get_type() == Variant::INT);
	CHECK(bit_field.operator int64_t() == int64_t(PROPERTY_USAGE_STORAGE));
}

TEST_CASE("[Variant] Bound results with unsigned C++ types keep the signed carrier") {
	// `String::hash()` returns `uint32_t`, but the binding declares `Variant::INT`.
	Variant text = String("foundry");
	Callable::CallError error;
	Variant hashed;
	text.callp("hash", nullptr, 0, hashed, error);
	CHECK(error.error == Callable::CallError::CALL_OK);
	CHECK(hashed.get_type() == Variant::INT);

	// `Time::get_ticks_msec()` returns `uint64_t` through an object method bind.
	const Variant ticks = Time::get_singleton()->call("get_ticks_msec");
	CHECK(ticks.get_type() == Variant::INT);
}

TEST_CASE("[Variant] Native reflection masks compose with int operands") {
	Object *object = memnew(Object);

	List<PropertyInfo> properties;
	object->get_property_list(&properties);
	REQUIRE(!properties.is_empty());
	const Dictionary property = Dictionary(properties.front()->get());
	CHECK(property["usage"].get_type() == Variant::INT);

	bool valid = false;
	Variant masked;
	Variant::evaluate(Variant::OP_BIT_AND, property["usage"], Variant(int64_t(PROPERTY_USAGE_STORAGE)), masked, valid);
	CHECK(valid);
	CHECK(masked.get_type() == Variant::INT);

	List<MethodInfo> methods;
	object->get_method_list(&methods);
	REQUIRE(!methods.is_empty());
	const Dictionary method = Dictionary(methods.front()->get());
	CHECK(method["flags"].get_type() == Variant::INT);

	memdelete(object);
}

} // namespace TestVariantUnsignedCarrier
