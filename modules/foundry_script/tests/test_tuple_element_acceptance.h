/**************************************************************************/
/*  test_tuple_element_acceptance.h                                       */
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

#include "modules/foundry_script/fs_function.h"

#include "core/object/ref_counted.h"
#include "core/variant/array.h"
#include "core/variant/container_type_validate.h"

#include "tests/test_macros.h"

// The tuple acceptance rule has two consumers: `FSDataType::is_type()`'s `TUPLE` branch, which decides
// whether a store or an `is` test succeeds, and the runtime's rejected-store diagnostic, which hunts
// for the single element that failed so it can name it. A diagnostic deriving its own copy of the rule
// can confidently blame an element the real test never rejected, so both now ask the same two
// questions -- `tuple_carrier_matches()` and `accepts_as_tuple_element()`. These cases pin those
// questions and the invariant that composing them reproduces `is_type()` exactly.

namespace FSTests {

static FSDataType tuple_element_builtin(Variant::Type p_carrier) {
	FSDataType type;
	type.kind = FSDataType::BUILTIN;
	type.builtin_type = p_carrier;
	return type;
}

static FSDataType tuple_element_typed_array(Variant::Type p_element_carrier) {
	FSDataType type = tuple_element_builtin(Variant::ARRAY);
	type.container_element_types.push_back(tuple_element_builtin(p_element_carrier));
	return type;
}

static FSDataType tuple_element_native(const StringName &p_native_type, bool p_nullable) {
	FSDataType type;
	type.kind = FSDataType::NATIVE;
	type.builtin_type = Variant::OBJECT;
	type.native_type = p_native_type;
	type.is_nullable = p_nullable;
	return type;
}

static FSDataType tuple_of(const Vector<FSDataType> &p_elements) {
	FSDataType type;
	type.kind = FSDataType::TUPLE;
	type.builtin_type = Variant::ARRAY;
	type.container_element_types = p_elements;
	return type;
}

static ContainerType builtin_container_type(Variant::Type p_carrier) {
	ContainerType container_type;
	container_type.builtin_type = p_carrier;
	return container_type;
}

static Array typed_int_array(int p_value) {
	Array array;
	array.set_typed(builtin_container_type(Variant::INT));
	array.push_back(p_value);
	return array;
}

// `is_type()` recomposed from the two shared questions. Every case below asserts the tuple test agrees
// with this, so an element the diagnostic walk would blame is always an element the test rejected.
static bool composed_tuple_acceptance(const FSDataType &p_tuple_type, const Variant &p_value) {
	if (!p_tuple_type.tuple_carrier_matches(p_value)) {
		return false;
	}
	const Array array = p_value;
	for (int i = 0; i < p_tuple_type.container_element_types.size(); i++) {
		if (!p_tuple_type.container_element_types[i].accepts_as_tuple_element(array[i])) {
			return false;
		}
	}
	return true;
}

static void check_tuple_acceptance(const FSDataType &p_tuple_type, const Variant &p_value, bool p_expected) {
	CHECK_EQ(p_tuple_type.is_type(p_value), p_expected);
	CHECK_EQ(composed_tuple_acceptance(p_tuple_type, p_value), p_expected);
}

TEST_CASE("[Modules][FoundryScript][Tuple] The carrier question answers shape only") {
	Vector<FSDataType> elements;
	elements.push_back(tuple_element_builtin(Variant::INT));
	elements.push_back(tuple_element_builtin(Variant::STRING));
	const FSDataType tuple_type = tuple_of(elements);

	CHECK_FALSE(tuple_type.tuple_carrier_matches(Variant(7)));
	CHECK_FALSE(tuple_type.tuple_carrier_matches(Variant()));

	Array short_array;
	short_array.push_back(7);
	CHECK_FALSE(tuple_type.tuple_carrier_matches(short_array));

	Array long_array;
	long_array.push_back(7);
	long_array.push_back("seven");
	long_array.push_back(true);
	CHECK_FALSE(tuple_type.tuple_carrier_matches(long_array));

	// Shape alone, so an Array of the right arity passes the carrier question even when its elements
	// disagree; element typing is the other question's job.
	Array wrong_elements;
	wrong_elements.push_back("seven");
	wrong_elements.push_back(7);
	CHECK(tuple_type.tuple_carrier_matches(wrong_elements));

	check_tuple_acceptance(tuple_type, Variant(7), false);
	check_tuple_acceptance(tuple_type, short_array, false);
	check_tuple_acceptance(tuple_type, long_array, false);
	check_tuple_acceptance(tuple_type, wrong_elements, false);

	Array matching;
	matching.push_back(7);
	matching.push_back("seven");
	check_tuple_acceptance(tuple_type, matching, true);
}

TEST_CASE("[Modules][FoundryScript][Tuple] A non-nullable object element rejects null") {
	Vector<FSDataType> elements;
	elements.push_back(tuple_element_native(StringName("RefCounted"), false));
	const FSDataType tuple_type = tuple_of(elements);

	// The asymmetry the element question exists for: a bare `NATIVE` slot accepts null so an
	// assignment stays compatible, while a tuple element must not, or a rejected store's diagnostic
	// counts a null element as passing and blames the wrong position.
	const FSDataType &element_type = tuple_type.container_element_types[0];
	CHECK(element_type.is_type(Variant()));
	CHECK_FALSE(element_type.accepts_as_tuple_element(Variant()));

	Array null_element;
	null_element.push_back(Variant());
	check_tuple_acceptance(tuple_type, null_element, false);

	Ref<RefCounted> value;
	value.instantiate();
	Array object_element;
	object_element.push_back(value);
	check_tuple_acceptance(tuple_type, object_element, true);

	// A nullable element admits the same null, through the shared question rather than a second rule.
	Vector<FSDataType> nullable_elements;
	nullable_elements.push_back(tuple_element_native(StringName("RefCounted"), true));
	const FSDataType nullable_tuple = tuple_of(nullable_elements);
	CHECK(nullable_tuple.container_element_types[0].accepts_as_tuple_element(Variant()));
	check_tuple_acceptance(nullable_tuple, null_element, true);
}

TEST_CASE("[Modules][FoundryScript][Tuple] A container element is accepted on its own element typing") {
	Vector<FSDataType> elements;
	elements.push_back(tuple_element_typed_array(Variant::INT));
	const FSDataType tuple_type = tuple_of(elements);

	// The failure the rejected-store hint is built to name: the carrier already agrees and only the
	// value's own element typing disagrees.
	Array untyped;
	untyped.push_back(1);
	Array untyped_element;
	untyped_element.push_back(untyped);
	check_tuple_acceptance(tuple_type, untyped_element, false);

	Array wrongly_typed;
	wrongly_typed.set_typed(builtin_container_type(Variant::STRING));
	Array wrongly_typed_element;
	wrongly_typed_element.push_back(wrongly_typed);
	check_tuple_acceptance(tuple_type, wrongly_typed_element, false);

	Array typed_element;
	typed_element.push_back(typed_int_array(1));
	check_tuple_acceptance(tuple_type, typed_element, true);
}

TEST_CASE("[Modules][FoundryScript][Tuple] Element acceptance recurses into a nested tuple") {
	Vector<FSDataType> inner_elements;
	inner_elements.push_back(tuple_element_builtin(Variant::INT));
	inner_elements.push_back(tuple_element_typed_array(Variant::INT));
	const FSDataType inner_tuple = tuple_of(inner_elements);

	Vector<FSDataType> outer_elements;
	outer_elements.push_back(tuple_element_builtin(Variant::INT));
	outer_elements.push_back(inner_tuple);
	const FSDataType outer_tuple = tuple_of(outer_elements);

	Array good_inner;
	good_inner.push_back(2);
	good_inner.push_back(typed_int_array(3));
	Array good_outer;
	good_outer.push_back(1);
	good_outer.push_back(good_inner);
	check_tuple_acceptance(outer_tuple, good_outer, true);

	// A nested element that fails is a failure of the outer element too, which is what lets the
	// diagnostic walk descend to it.
	Array untyped;
	untyped.push_back(3);
	Array bad_inner;
	bad_inner.push_back(2);
	bad_inner.push_back(untyped);
	CHECK_FALSE(inner_tuple.accepts_as_tuple_element(bad_inner));

	Array bad_outer;
	bad_outer.push_back(1);
	bad_outer.push_back(bad_inner);
	check_tuple_acceptance(outer_tuple, bad_outer, false);

	// A nested value of the wrong arity fails the nested carrier question, not an element one.
	Array short_inner;
	short_inner.push_back(2);
	CHECK_FALSE(inner_tuple.tuple_carrier_matches(short_inner));
	Array short_outer;
	short_outer.push_back(1);
	short_outer.push_back(short_inner);
	check_tuple_acceptance(outer_tuple, short_outer, false);
}

} // namespace FSTests
