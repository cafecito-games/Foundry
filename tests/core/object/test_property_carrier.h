/**************************************************************************/
/*  test_property_carrier.h                                               */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             FOUNDRY ENGINE                             */
/*          A fork of the Godot Engine (https://godotengine.org)          */
/*                       https://www.cafecito.games                       */
/**************************************************************************/
/* Copyright (c) 2026-present Cafecito Games LLC.                         */
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

#include "core/object/class_db.h"
#include "core/object/method_bind.h"

#include "tests/test_macros.h"

namespace TestPropertyCarrier {

static bool is_integer_carrier(Variant::Type p_type) {
	return p_type == Variant::INT || p_type == Variant::UINT;
}

struct AccessorCarriers {
	// `Variant::NIL` when the corresponding accessor is missing or exchanges a non-integer value.
	Variant::Type getter = Variant::NIL;
	Variant::Type setter = Variant::NIL;
};

static AccessorCarriers integer_accessor_carriers(const StringName &p_class, const StringName &p_property) {
	AccessorCarriers result;

	const StringName getter_name = ClassDB::get_property_getter(p_class, p_property);
	MethodBind *getter = getter_name == StringName() ? nullptr : ClassDB::get_method(p_class, getter_name);
	if (getter != nullptr && is_integer_carrier(getter->get_return_info().type)) {
		result.getter = getter->get_return_info().type;
	}

	const StringName setter_name = ClassDB::get_property_setter(p_class, p_property);
	MethodBind *setter = setter_name == StringName() ? nullptr : ClassDB::get_method(p_class, setter_name);
	if (setter != nullptr && setter->get_argument_count() >= 1) {
		// An indexed property's setter takes the index first, so the value is always its last argument.
		const Variant::Type carrier = setter->get_argument_info(setter->get_argument_count() - 1).type;
		if (is_integer_carrier(carrier)) {
			result.setter = carrier;
		}
	}

	return result;
}

static bool is_layout_only(const PropertyInfo &p_property) {
	return (p_property.usage & (PROPERTY_USAGE_GROUP | PROPERTY_USAGE_SUBGROUP | PROPERTY_USAGE_CATEGORY)) != 0;
}

// A property registration transports its own Variant carrier, but the analyzer, the doc tool, and
// every runtime read of the property go through the accessors instead. When the two disagree,
// `get_property_list()` advertises a type that `get()` never produces.
TEST_CASE("[PropertyCarrier] Registered carrier agrees with the accessor carrier") {
	LocalVector<StringName> classes;
	ClassDB::get_class_list(classes);

	Vector<String> mismatches;
	for (const StringName &class_name : classes) {
		List<PropertyInfo> properties;
		ClassDB::get_property_list(class_name, &properties, true);
		for (const PropertyInfo &property : properties) {
			if (is_layout_only(property) || !is_integer_carrier(property.type)) {
				continue;
			}
			const AccessorCarriers carriers = integer_accessor_carriers(class_name, property.name);
			if (carriers.getter == Variant::NIL || carriers.getter != carriers.setter) {
				continue;
			}
			if (carriers.getter == property.type) {
				continue;
			}
			mismatches.push_back(vformat("%s.%s registered as %s but its accessors exchange %s",
					class_name, property.name, Variant::get_type_name(property.type),
					Variant::get_type_name(carriers.getter)));
		}
	}

	INFO(String("\n").join(mismatches));
	CHECK(mismatches.is_empty());
}

// A property whose getter and setter disagree on the integer carrier cannot round-trip its own
// value, and leaves the analyzer with no width to pin, so no registration could reconcile the two.
TEST_CASE("[PropertyCarrier] Property accessors agree with each other") {
	LocalVector<StringName> classes;
	ClassDB::get_class_list(classes);

	Vector<String> disagreements;
	for (const StringName &class_name : classes) {
		List<PropertyInfo> properties;
		ClassDB::get_property_list(class_name, &properties, true);
		for (const PropertyInfo &property : properties) {
			if (is_layout_only(property)) {
				continue;
			}
			const AccessorCarriers carriers = integer_accessor_carriers(class_name, property.name);
			if (carriers.getter == Variant::NIL || carriers.setter == Variant::NIL) {
				continue;
			}
			if (carriers.getter == carriers.setter) {
				continue;
			}
			disagreements.push_back(vformat("%s.%s reads %s but writes %s", class_name, property.name,
					Variant::get_type_name(carriers.getter), Variant::get_type_name(carriers.setter)));
		}
	}

	INFO(String("\n").join(disagreements));
	CHECK(disagreements.is_empty());
}

} // namespace TestPropertyCarrier
