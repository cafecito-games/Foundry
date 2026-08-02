/**************************************************************************/
/*  test_resource_format_binary_container_types.h                         */
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

#include "core/io/resource_loader.h"
#include "core/io/resource_saver.h"
#include "core/object/ref_counted.h"
#include "core/variant/array.h"
#include "core/variant/container_type_validate.h"
#include "core/variant/dictionary.h"

#include "tests/test_macros.h"
#include "tests/test_utils.h"

namespace TestResourceFormatBinaryContainerTypes {

static ContainerType make_builtin(Variant::Type p_type) {
	ContainerType type;
	type.builtin_type = p_type;
	return type;
}

static ContainerType make_instance_type(const StringName &p_class) {
	ContainerType type;
	type.builtin_type = Variant::OBJECT;
	type.class_name = p_class;
	return type;
}

static ContainerType make_handle_type(const StringName &p_class) {
	ContainerType type = make_instance_type(p_class);
	type.is_type_handle = true;
	return type;
}

static ContainerType make_array_of(const ContainerType &p_element) {
	ContainerType type;
	type.builtin_type = Variant::ARRAY;
	type.element_types.push_back(p_element);
	return type;
}

// `Box[int]`: an engine class standing in for a specialized script type, carrying its reified
// generic arguments the way `Array[Box[int]]` would.
static ContainerType make_specialized_type(const StringName &p_class, const ContainerType &p_argument) {
	ContainerType type = make_instance_type(p_class);
	type.type_arguments.push_back(p_argument);
	return type;
}

// Saves `p_value` as resource metadata, reloads the file, and hands back the reconstructed value.
static Variant round_trip(const Variant &p_value, const String &p_stem, const String &p_extension) {
	Ref<Resource> resource = memnew(Resource);
	resource->set_meta("values", p_value);

	const String path = TestUtils::get_temp_path(p_stem + p_extension);
	REQUIRE_EQ(ResourceSaver::save(resource, path), OK);

	Ref<Resource> loaded = ResourceLoader::load(path, "", ResourceFormatLoader::CACHE_MODE_IGNORE);
	REQUIRE(loaded.is_valid());
	return loaded->get_meta("values");
}

static Array round_trip_array(const ContainerType &p_element_type, const String &p_stem, const String &p_extension = ".res") {
	Array values;
	values.set_typed(p_element_type);
	const Variant loaded = round_trip(values, p_stem, p_extension);
	REQUIRE_EQ(loaded.get_type(), Variant::ARRAY);
	return loaded;
}

TEST_CASE("[ResourceFormatBinary] Typed array keeps its element type across a binary round trip") {
	Array values;
	values.set_typed(make_builtin(Variant::INT));
	values.push_back(7);

	const Variant loaded = round_trip(values, "binary_container_typed_array", ".res");
	REQUIRE_EQ(loaded.get_type(), Variant::ARRAY);

	const Array loaded_array = loaded;
	CHECK(loaded_array.is_typed());
	CHECK_EQ(loaded_array.get_element_type(), make_builtin(Variant::INT));
	REQUIRE_EQ(loaded_array.size(), 1);
	CHECK_EQ(int(loaded_array[0]), 7);

	// The reloaded array must still enforce what its element type promised.
	ERR_PRINT_OFF;
	Array mutable_array = loaded_array;
	mutable_array.push_back("not an int");
	ERR_PRINT_ON;
	CHECK_EQ(mutable_array.size(), 1);
}

TEST_CASE("[ResourceFormatBinary] Typed dictionary keeps its key and value types across a binary round trip") {
	Dictionary values;
	values.set_typed(make_builtin(Variant::STRING), make_builtin(Variant::INT));
	values["answer"] = 42;

	const Variant loaded = round_trip(values, "binary_container_typed_dictionary", ".res");
	REQUIRE_EQ(loaded.get_type(), Variant::DICTIONARY);

	Dictionary loaded_dictionary = loaded;
	CHECK(loaded_dictionary.is_typed_key());
	CHECK(loaded_dictionary.is_typed_value());
	CHECK_EQ(loaded_dictionary.get_key_type(), make_builtin(Variant::STRING));
	CHECK_EQ(loaded_dictionary.get_value_type(), make_builtin(Variant::INT));
	REQUIRE_EQ(loaded_dictionary.size(), 1);
	CHECK_EQ(int(loaded_dictionary["answer"]), 42);

	// The reloaded dictionary must still enforce what its key and value types promised.
	ERR_PRINT_OFF;
	CHECK_FALSE(loaded_dictionary.set(Vector2(), 1));
	CHECK_FALSE(loaded_dictionary.set("rejected", Vector2()));
	ERR_PRINT_ON;
	CHECK_EQ(loaded_dictionary.size(), 1);
}

TEST_CASE("[ResourceFormatBinary] Specialized element type keeps its type arguments across a binary round trip") {
	const ContainerType element_type = make_specialized_type(SNAME("Resource"), make_builtin(Variant::INT));
	const Array loaded_array = round_trip_array(element_type, "binary_container_specialized_element");

	CHECK_EQ(loaded_array.get_element_type(), element_type);
	REQUIRE_EQ(loaded_array.get_element_type().type_arguments.size(), 1);
	CHECK_EQ(loaded_array.get_element_type().type_arguments[0].builtin_type, Variant::INT);
}

TEST_CASE("[ResourceFormatBinary] Container element metadata survives at nesting depth two") {
	const ContainerType element_type = make_array_of(make_array_of(make_builtin(Variant::FLOAT)));
	const Array loaded_array = round_trip_array(element_type, "binary_container_nesting_depth_two");

	CHECK_EQ(loaded_array.get_element_type(), element_type);
}

TEST_CASE("[ResourceFormatBinary] Class-handle element type survives a binary round trip and still rejects an instance") {
	const ContainerType element_type = make_handle_type(SNAME("Resource"));
	Array loaded_array = round_trip_array(element_type, "binary_container_class_handle");

	CHECK(loaded_array.get_element_type().is_type_handle);
	CHECK_EQ(loaded_array.get_element_type(), element_type);

	Ref<Resource> instance = memnew(Resource);
	ERR_PRINT_OFF;
	loaded_array.push_back(instance);
	ERR_PRINT_ON;
	CHECK_EQ(loaded_array.size(), 0);
}

TEST_CASE("[ResourceFormatBinary] Text and binary formats agree on the reconstructed container type") {
	const ContainerType element_types[] = {
		make_builtin(Variant::INT),
		make_instance_type(SNAME("Resource")),
		make_handle_type(SNAME("Resource")),
		make_specialized_type(SNAME("Resource"), make_builtin(Variant::STRING)),
		make_array_of(make_builtin(Variant::INT)),
	};

	int index = 0;
	for (const ContainerType &element_type : element_types) {
		const String stem = "binary_container_format_agreement_" + itos(index++);
		const Array from_binary = round_trip_array(element_type, stem, ".res");
		const Array from_text = round_trip_array(element_type, stem, ".tres");

		CHECK_EQ(from_binary.get_element_type(), from_text.get_element_type());
		CHECK_EQ(from_binary.get_element_type(), element_type);
	}
}

TEST_CASE("[ResourceFormatBinary] A payload written before container types were encoded loads as untyped") {
	// Written by the binary saver as it stood before typed containers were encoded: the array
	// carried `Array[int]` in memory but the payload holds only a length and its elements.
	const String path = TestUtils::get_data_path("resource_format_binary_untyped_container_legacy.res");

	Ref<Resource> loaded = ResourceLoader::load(path, "", ResourceFormatLoader::CACHE_MODE_IGNORE);
	REQUIRE(loaded.is_valid());

	const Variant values = loaded->get_meta("values");
	REQUIRE_EQ(values.get_type(), Variant::ARRAY);

	Array loaded_array = values;
	CHECK_FALSE(loaded_array.is_typed());
	REQUIRE_EQ(loaded_array.size(), 1);
	CHECK_EQ(int(loaded_array[0]), 7);

	// An untyped array accepts anything, which is exactly how a payload without element metadata
	// must behave.
	loaded_array.push_back("anything goes");
	CHECK_EQ(loaded_array.size(), 2);
}

} // namespace TestResourceFormatBinaryContainerTypes
