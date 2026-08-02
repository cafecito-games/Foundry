/**************************************************************************/
/*  test_json_container_type.h                                            */
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

#include "core/io/json.h"

#include "core/variant/array.h"
#include "core/variant/container_type_validate.h"
#include "core/variant/dictionary.h"

#include "tests/test_macros.h"

namespace TestJSONContainerType {

static ContainerType make_builtin(Variant::Type p_type) {
	ContainerType type;
	type.builtin_type = p_type;
	return type;
}

// The descriptor for an explicitly `Variant`-typed slot: a real typing decision spelled `Variant`,
// as opposed to a slot that carries no element typing at all.
static ContainerType make_variant() {
	return ContainerType();
}

static ContainerType make_array_of(const ContainerType &p_element) {
	ContainerType type;
	type.builtin_type = Variant::ARRAY;
	type.element_types.push_back(p_element);
	return type;
}

static ContainerType make_dictionary_of(const ContainerType &p_key, const ContainerType &p_value) {
	ContainerType type;
	type.builtin_type = Variant::DICTIONARY;
	type.element_types.push_back(p_key);
	type.element_types.push_back(p_value);
	return type;
}

static ContainerType make_specialized_class(const StringName &p_class_name, const Vector<ContainerType> &p_type_arguments) {
	ContainerType type;
	type.builtin_type = Variant::OBJECT;
	type.class_name = p_class_name;
	type.type_arguments = p_type_arguments;
	return type;
}

// Carries `p_element_type` through `JSON.from_native` / `JSON.to_native` as the element type of an
// array and returns what came back, so a descriptor can be compared with the one that went in.
static ContainerType json_round_trip(const ContainerType &p_element_type) {
	Array source;
	source.set_typed(p_element_type);

	const Variant native = JSON::to_native(JSON::from_native(source, true), true);
	REQUIRE_EQ(native.get_type(), Variant::ARRAY);
	const Array decoded = native;
	return decoded.get_element_type();
}

// Decodes a JSON descriptor written by hand, so the "explicitly Variant" and "absent" spellings can
// be compared directly rather than only through what the encoder happens to emit.
static ContainerType decode_element_type(const String &p_json) {
	const Variant native = JSON::to_native(JSON::parse_string(p_json), true);
	REQUIRE_EQ(native.get_type(), Variant::ARRAY);
	const Array decoded = native;
	return decoded.get_element_type();
}

TEST_CASE("[JSON][Native] An explicitly Variant-typed array element round-trips") {
	const ContainerType source = make_array_of(make_variant());

	Array array;
	array.set_typed(source);
	CHECK_EQ(JSON::stringify(JSON::from_native(array, true)), R"({"args":[],"elem_type":{"elem_type":"Variant","type":"Array"},"type":"Array"})");

	CHECK_EQ(json_round_trip(source), source);
}

TEST_CASE("[JSON][Native] An explicitly Variant-typed dictionary key or value round-trips") {
	const ContainerType variant_to_variant = make_dictionary_of(make_variant(), make_variant());
	const ContainerType variant_to_int = make_dictionary_of(make_variant(), make_builtin(Variant::INT));
	const ContainerType string_to_variant = make_dictionary_of(make_builtin(Variant::STRING), make_variant());

	CHECK_EQ(json_round_trip(variant_to_variant), variant_to_variant);
	CHECK_EQ(json_round_trip(variant_to_int), variant_to_int);
	CHECK_EQ(json_round_trip(string_to_variant), string_to_variant);
}

TEST_CASE("[JSON][Native] A Variant-typed node reached through type arguments round-trips") {
	// `RefCounted[Node[Array[Variant]]]`: the `Variant` sits two type-argument hops down.
	const ContainerType inner = make_specialized_class(SNAME("Node"), { make_array_of(make_variant()) });
	const ContainerType source = make_specialized_class(SNAME("RefCounted"), { inner });

	const ContainerType decoded = json_round_trip(source);
	CHECK_EQ(decoded, source);
	CHECK_EQ(decoded.get_type_name(), "RefCounted[Node[Array[Variant]]]");
}

TEST_CASE("[JSON][Native] An unspecialized container element stays unspecialized") {
	const ContainerType bare_array = make_builtin(Variant::ARRAY);
	const ContainerType bare_dictionary = make_builtin(Variant::DICTIONARY);

	CHECK_EQ(json_round_trip(bare_array), bare_array);
	CHECK_EQ(json_round_trip(bare_dictionary), bare_dictionary);
	CHECK(json_round_trip(bare_array).element_types.is_empty());
	CHECK(json_round_trip(bare_dictionary).element_types.is_empty());

	// An unspecialized element stays distinguishable from an explicitly `Variant`-typed one.
	CHECK(json_round_trip(make_array_of(make_variant())) != json_round_trip(bare_array));
	CHECK_EQ(json_round_trip(make_array_of(make_variant())).get_type_name(), "Array[Variant]");
	CHECK_EQ(json_round_trip(bare_array).get_type_name(), "Array");
}

TEST_CASE("[JSON][Native] Absent and explicitly Variant child descriptors decode differently") {
	ContainerType specialized_array = make_builtin(Variant::ARRAY);
	specialized_array.type_arguments.push_back(make_builtin(Variant::INT));

	ContainerType specialized_dictionary = make_builtin(Variant::DICTIONARY);
	specialized_dictionary.type_arguments.push_back(make_builtin(Variant::INT));

	CHECK_EQ(decode_element_type(R"({"type":"Array","args":[],"elem_type":{"type":"Array","elem_type":"Variant"}})"),
			make_array_of(make_variant()));
	CHECK_EQ(decode_element_type(R"({"type":"Array","args":[],"elem_type":{"type":"Array","type_args":["int"]}})"),
			specialized_array);

	CHECK_EQ(decode_element_type(R"({"type":"Array","args":[],"elem_type":{"type":"Dictionary","key_type":"Variant","value_type":"Variant"}})"),
			make_dictionary_of(make_variant(), make_variant()));
	CHECK_EQ(decode_element_type(R"({"type":"Array","args":[],"elem_type":{"type":"Dictionary","type_args":["int"]}})"),
			specialized_dictionary);
}

TEST_CASE("[JSON][Native] Ordinary JSON documents are unaffected") {
	const String document = R"({"count":3.0,"items":["a","b"],"nested":{"flag":true}})";

	const Variant parsed = JSON::parse_string(document);
	REQUIRE_EQ(parsed.get_type(), Variant::DICTIONARY);
	CHECK_EQ(JSON::stringify(parsed), document);

	const Variant native = JSON::to_native(JSON::from_native(parsed, true), true);
	REQUIRE_EQ(native.get_type(), Variant::DICTIONARY);
	const Dictionary decoded = native;
	CHECK_EQ(int(decoded["count"]), 3);
	CHECK_EQ(decoded.get_key_type(), ContainerType());
	CHECK_EQ(decoded.get_value_type(), ContainerType());
}

} // namespace TestJSONContainerType
