/**************************************************************************/
/*  test_container_type_handle_transport.h                                */
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
#include "core/io/marshalls.h"
#include "core/io/resource_loader.h"
#include "core/io/resource_saver.h"
#include "core/object/class_handle.h"
#include "core/object/ref_counted.h"
#include "core/object/script_language.h"
#include "core/variant/array.h"
#include "core/variant/container_type_validate.h"
#include "core/variant/dictionary.h"
#include "core/variant/variant_parser.h"

#include "tests/test_macros.h"
#include "tests/test_utils.h"

namespace TestContainerTypeHandleTransport {

// A language-agnostic class handle: a value that denotes a class rather than instances of it.
class TransportTestClassHandle : public ClassHandle {
	FOUNDRY_CLASS(TransportTestClassHandle, ClassHandle);

	StringName represented_class;
	Ref<Script> represented_script;
	Vector<ContainerType> represented_type_arguments;

public:
	virtual StringName get_represented_native_class() const override { return represented_class; }
	virtual Ref<Script> get_represented_script() const override { return represented_script; }
	virtual void get_represented_type_arguments(Vector<ContainerType> &r_arguments) const override { r_arguments = represented_type_arguments; }

	void set_represented_native_class(const StringName &p_class) { represented_class = p_class; }
	void set_represented_script(const Ref<Script> &p_script) { represented_script = p_script; }
	void set_represented_type_arguments(const Vector<ContainerType> &p_arguments) { represented_type_arguments = p_arguments; }
};

// The minimum a `Script` needs to be usable as a container type descriptor payload in these tests.
class TransportTestScript : public Script {
	FOUNDRY_CLASS(TransportTestScript, Script);

protected:
	static void _bind_methods() {}

public:
	bool can_instantiate() const override { return false; }
	Ref<Script> get_base_script() const override { return Ref<Script>(); }
	StringName get_global_name() const override { return StringName(); }
	bool inherits_script(const Ref<Script> &p_script) const override { return p_script == Ref<Script>(this); }
	StringName get_instance_base_type() const override { return StringName("RefCounted"); }
	ScriptInstance *instance_create(Object *p_this) override { return nullptr; }
	bool instance_has(const Object *p_this) const override { return false; }
	bool has_source_code() const override { return false; }
	String get_source_code() const override { return String(); }
	void set_source_code(const String &p_code) override {}
	Error reload(bool p_keep_state = false) override { return OK; }
#ifdef TOOLS_ENABLED
	StringName get_doc_class_name() const override { return StringName(); }
	Vector<DocData::ClassDoc> get_documentation() const override { return Vector<DocData::ClassDoc>(); }
	String get_class_icon_path() const override { return String(); }
#endif // TOOLS_ENABLED
	bool has_method(const StringName &p_method) const override { return false; }
	MethodInfo get_method_info(const StringName &p_method) const override { return MethodInfo(); }
	bool is_tool() const override { return false; }
	bool is_valid() const override { return true; }
	bool is_abstract() const override { return false; }
	ScriptLanguage *get_language() const override { return nullptr; }
	bool has_script_signal(const StringName &p_signal) const override { return false; }
	void get_script_signal_list(List<MethodInfo> *r_signals) const override {}
	bool get_property_default_value(const StringName &p_property, Variant &r_value) const override { return false; }
	void get_script_method_list(List<MethodInfo> *p_list) const override {}
	void get_script_property_list(List<PropertyInfo> *p_list) const override {}
	const Variant get_rpc_config() const override { return Variant(); }
	bool project_type_arguments_onto_base(const Ref<Script> &p_base, const Vector<ContainerType> &p_leaf_type_arguments, Vector<ContainerType> &r_type_arguments, Vector<bool> &r_argument_bound) const override {
		if (p_base != Ref<Script>(this)) {
			return false;
		}
		r_type_arguments = p_leaf_type_arguments;
		r_argument_bound.resize(r_type_arguments.size());
		for (int i = 0; i < r_argument_bound.size(); i++) {
			r_argument_bound.write[i] = true;
		}
		return true;
	}
};

static Ref<TransportTestClassHandle> make_handle(const StringName &p_class) {
	Ref<TransportTestClassHandle> handle;
	handle.instantiate();
	handle->set_represented_native_class(p_class);
	return handle;
}

static Ref<TransportTestClassHandle> make_script_handle(const Ref<Script> &p_script, const Vector<ContainerType> &p_type_arguments) {
	Ref<TransportTestClassHandle> handle;
	handle.instantiate();
	handle->set_represented_script(p_script);
	handle->set_represented_type_arguments(p_type_arguments);
	return handle;
}

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

static ContainerType make_dictionary_of(const ContainerType &p_key, const ContainerType &p_value) {
	ContainerType type;
	type.builtin_type = Variant::DICTIONARY;
	type.element_types.push_back(p_key);
	type.element_types.push_back(p_value);
	return type;
}

static Variant encode_and_decode_binary(const Variant &p_source, bool p_full_objects = true) {
	int encoded_len = 0;
	REQUIRE_EQ(encode_variant(p_source, nullptr, encoded_len, p_full_objects), OK);
	PackedByteArray buffer;
	buffer.resize(encoded_len);
	REQUIRE_EQ(encode_variant(p_source, buffer.ptrw(), encoded_len, p_full_objects), OK);

	Variant decoded;
	int decoded_len = 0;
	REQUIRE_EQ(decode_variant(decoded, buffer.ptr(), buffer.size(), &decoded_len, p_full_objects), OK);
	CHECK_EQ(decoded_len, buffer.size());
	return decoded;
}

static Variant parse_text(const String &p_text) {
	VariantParser::StreamString stream;
	stream.s = p_text;
	String error_string;
	int line = 0;
	Variant parsed;
	const Error err = VariantParser::parse(&stream, parsed, error_string, line);
	INFO(error_string);
	REQUIRE_EQ(err, OK);
	return parsed;
}

TEST_CASE("[ContainerType] Descriptor round-trips the class handle flag at every nesting depth") {
	const ContainerType source = make_array_of(make_dictionary_of(make_handle_type(SNAME("Node")), make_handle_type(SNAME("Resource"))));

	ContainerType decoded;
	String error;
	REQUIRE(ContainerTypeDescriptor::from_variant(ContainerTypeDescriptor::to_variant(source), decoded, &error));
	CHECK_EQ(decoded, source);
	REQUIRE_EQ(decoded.element_types.size(), 1);
	REQUIRE_EQ(decoded.element_types[0].element_types.size(), 2);
	CHECK(decoded.element_types[0].element_types[0].is_type_handle);
	CHECK(decoded.element_types[0].element_types[1].is_type_handle);
	CHECK_FALSE(decoded.is_type_handle);

	// A specialized handle keeps its arguments and its flag together.
	ContainerType specialized = make_handle_type(SNAME("RefCounted"));
	specialized.type_arguments.push_back(make_builtin(Variant::INT));
	REQUIRE(ContainerTypeDescriptor::from_variant(ContainerTypeDescriptor::to_variant(make_array_of(specialized)), decoded, &error));
	CHECK_EQ(decoded, make_array_of(specialized));
	CHECK_EQ(decoded.element_types[0].get_type_name(), "Type[RefCounted[int]]");
}

TEST_CASE("[ContainerType] Descriptor omits the class handle flag for instance types") {
	const Dictionary descriptor = ContainerTypeDescriptor::to_variant(make_array_of(make_instance_type(SNAME("Node"))));
	CHECK_FALSE(descriptor.has("is_type_handle"));

	const Array element_types = descriptor["element_types"];
	const Dictionary element_descriptor = element_types[0];
	CHECK_FALSE(element_descriptor.has("is_type_handle"));

	// The flag appears only where it is set.
	const Dictionary handle_descriptor = ContainerTypeDescriptor::to_variant(make_handle_type(SNAME("Node")));
	REQUIRE(handle_descriptor.has("is_type_handle"));
	CHECK_EQ(handle_descriptor["is_type_handle"], Variant(true));
}

TEST_CASE("[ContainerType] Descriptor decoding rejects a class handle on a non-object type at depth") {
	Dictionary element_descriptor;
	element_descriptor["type"] = Variant::INT;
	element_descriptor["is_type_handle"] = true;

	Array element_types;
	element_types.push_back(element_descriptor);

	Dictionary descriptor;
	descriptor["type"] = Variant::ARRAY;
	descriptor["element_types"] = element_types;

	ContainerType decoded;
	String error;
	CHECK_FALSE(ContainerTypeDescriptor::from_variant(descriptor, decoded, &error));
	CHECK_EQ(error, R"(Container type descriptor "is_type_handle" is only valid for Object types.)");
}

TEST_CASE("[ContainerType] Variant writer and parser round-trip a class handle element type") {
	Array array;
	array.set_typed(make_handle_type(SNAME("Node")));

	String array_str;
	VariantWriter::write_to_string(array, array_str);
	CHECK_EQ(array_str, "Array[Type[Node]]([])");

	const Array parsed = parse_text(array_str);
	CHECK_EQ(parsed.get_element_type(), make_handle_type(SNAME("Node")));

	// The handle values the type admits are unchanged by the round trip.
	Array reconstructed = parsed;
	reconstructed.push_back(make_handle(SNAME("Button")));
	CHECK_EQ(reconstructed.size(), 1);
}

TEST_CASE("[ContainerType] Variant writer and parser round-trip class handles in Dictionary key and value positions") {
	Dictionary key_handle_dictionary;
	key_handle_dictionary.set_typed(make_handle_type(SNAME("Node")), make_builtin(Variant::INT));

	String dictionary_str;
	VariantWriter::write_to_string(key_handle_dictionary, dictionary_str);
	CHECK(dictionary_str.begins_with("Dictionary[Type[Node], int]("));

	const Dictionary parsed_key_handle = parse_text(dictionary_str);
	CHECK_EQ(parsed_key_handle.get_key_type(), make_handle_type(SNAME("Node")));
	CHECK_EQ(parsed_key_handle.get_value_type(), make_builtin(Variant::INT));

	Dictionary value_handle_dictionary;
	value_handle_dictionary.set_typed(make_builtin(Variant::STRING), make_handle_type(SNAME("Resource")));

	VariantWriter::write_to_string(value_handle_dictionary, dictionary_str);
	CHECK(dictionary_str.begins_with("Dictionary[String, Type[Resource]]("));

	const Dictionary parsed_value_handle = parse_text(dictionary_str);
	CHECK_EQ(parsed_value_handle.get_key_type(), make_builtin(Variant::STRING));
	CHECK_EQ(parsed_value_handle.get_value_type(), make_handle_type(SNAME("Resource")));
}

TEST_CASE("[ContainerType] Variant text for instance-typed containers is unchanged") {
	Array instance_array;
	instance_array.set_typed(make_instance_type(SNAME("Node")));

	String array_str;
	VariantWriter::write_to_string(instance_array, array_str);
	CHECK_EQ(array_str, "Array[Node]([])");

	Array nested;
	nested.set_typed(make_array_of(make_instance_type(SNAME("Node"))));
	VariantWriter::write_to_string(nested, array_str);
	CHECK_EQ(array_str, "Array[Array[Node]]([])");

	Dictionary dictionary;
	dictionary.set_typed(make_builtin(Variant::STRING), make_instance_type(SNAME("Resource")));
	String dictionary_str;
	VariantWriter::write_to_string(dictionary, dictionary_str);
	CHECK_EQ(dictionary_str, "Dictionary[String, Resource]({})");
}

TEST_CASE("[ContainerType] Variant writer and parser round-trip a nested and specialized class handle") {
	ContainerType specialized = make_handle_type(SNAME("RefCounted"));
	specialized.type_arguments.push_back(make_builtin(Variant::INT));

	Array array;
	array.set_typed(make_array_of(specialized));

	String array_str;
	VariantWriter::write_to_string(array, array_str);
	CHECK_EQ(array_str, "Array[Array[Type[RefCounted[int]]]]([])");

	const Array parsed = parse_text(array_str);
	CHECK_EQ(parsed.get_element_type(), make_array_of(specialized));
}

TEST_CASE("[ContainerType] Variant parser rejects a class handle on a non-object type") {
	VariantParser::StreamString stream;
	stream.s = "Array[Type[int]]([])";
	String error_string;
	int line = 0;
	Variant parsed;
	CHECK_NE(VariantParser::parse(&stream, parsed, error_string, line), OK);
	CHECK(error_string.contains("Object"));
}

TEST_CASE("[ContainerType] Binary Variant encoding round-trips the class handle flag") {
	Array source;
	source.set_typed(make_array_of(make_handle_type(SNAME("Node"))));

	const Array decoded = encode_and_decode_binary(source);
	CHECK_EQ(decoded.get_element_type(), make_array_of(make_handle_type(SNAME("Node"))));
}

TEST_CASE("[ContainerType] Binary Variant encoding keeps differing key and value class handles apart") {
	Dictionary source;
	source.set_typed(make_handle_type(SNAME("Node")), make_handle_type(SNAME("Resource")));

	const Dictionary decoded = encode_and_decode_binary(source);
	CHECK_EQ(decoded.get_key_type(), make_handle_type(SNAME("Node")));
	CHECK_EQ(decoded.get_value_type(), make_handle_type(SNAME("Resource")));
	CHECK(decoded.get_key_type() != decoded.get_value_type());

	// An instance-typed value position stays an instance type next to a handle key.
	Dictionary mixed;
	mixed.set_typed(make_handle_type(SNAME("Node")), make_instance_type(SNAME("Resource")));
	const Dictionary decoded_mixed = encode_and_decode_binary(mixed);
	CHECK(decoded_mixed.get_key_type().is_type_handle);
	CHECK_FALSE(decoded_mixed.get_value_type().is_type_handle);
}

TEST_CASE("[ContainerType] Binary Variant encoding is unchanged for instance-typed containers") {
	// Payloads captured before class handles existed. They must still decode as instance-typed
	// containers, and the current encoder must still produce exactly these bytes.
	static const uint8_t typed_object_array[] = {
		0x1c, 0x00, 0x02, 0x00, // Header: ARRAY with a class-name element type.
		0x04, 0x00, 0x00, 0x00, // Class name length.
		0x4e, 0x6f, 0x64, 0x65, // "Node".
		0x00, 0x00, 0x00, 0x00 // Array length.
	};
	static const uint8_t typed_builtin_array[] = {
		0x1c, 0x00, 0x01, 0x00, // Header: ARRAY with a builtin element type.
		0x02, 0x00, 0x00, 0x00, // `Variant::INT`.
		0x00, 0x00, 0x00, 0x00 // Array length.
	};

	Variant decoded;
	int decoded_len = 0;
	REQUIRE_EQ(decode_variant(decoded, typed_object_array, sizeof(typed_object_array), &decoded_len, true), OK);
	CHECK_EQ(decoded_len, (int)sizeof(typed_object_array));
	const Array decoded_object_array = decoded;
	CHECK_EQ(decoded_object_array.get_element_type(), make_instance_type(SNAME("Node")));
	CHECK_FALSE(decoded_object_array.get_element_type().is_type_handle);

	REQUIRE_EQ(decode_variant(decoded, typed_builtin_array, sizeof(typed_builtin_array), &decoded_len, true), OK);
	const Array decoded_builtin_array = decoded;
	CHECK_EQ(decoded_builtin_array.get_element_type(), make_builtin(Variant::INT));

	Array source;
	source.set_typed(make_instance_type(SNAME("Node")));
	int encoded_len = 0;
	REQUIRE_EQ(encode_variant(source, nullptr, encoded_len, true), OK);
	PackedByteArray buffer;
	buffer.resize(encoded_len);
	REQUIRE_EQ(encode_variant(source, buffer.ptrw(), encoded_len, true), OK);
	REQUIRE_EQ(buffer.size(), (int)sizeof(typed_object_array));
	CHECK_EQ(memcmp(buffer.ptr(), typed_object_array, sizeof(typed_object_array)), 0);
}

TEST_CASE("[ContainerType] Binary Variant decoding rejects a class handle on a non-object type") {
	// An extended container type payload whose element node claims to be a handle for `int`.
	// Word layout: header, element kind, element builtin type, element handle flag, element child
	// count, element type argument count, array length.
	PackedByteArray buffer;
	buffer.resize(7 * 4);
	uint8_t *write = buffer.ptrw();
	encode_uint32(Variant::ARRAY | (1 << 8), write + 0); // Extended container type flag.
	encode_uint32(1, write + 4); // Builtin element type kind.
	encode_uint32(Variant::INT, write + 8);
	encode_uint32(1, write + 12); // `is_type_handle`.
	encode_uint32(0, write + 16);
	encode_uint32(0, write + 20);
	encode_uint32(0, write + 24);

	Variant decoded;
	int decoded_len = 0;
	ERR_PRINT_OFF;
	CHECK_NE(decode_variant(decoded, buffer.ptr(), buffer.size(), &decoded_len, true), OK);
	ERR_PRINT_ON;

	// The same payload without the flag decodes normally, so the rejection is the flag alone.
	encode_uint32(0, write + 12);
	REQUIRE_EQ(decode_variant(decoded, buffer.ptr(), buffer.size(), &decoded_len, true), OK);
	const Array decoded_array = decoded;
	CHECK_EQ(decoded_array.get_element_type(), make_builtin(Variant::INT));
}

TEST_CASE("[ContainerType] Native JSON conversion round-trips the class handle flag") {
	Array source;
	source.set_typed(make_dictionary_of(make_builtin(Variant::STRING), make_handle_type(SNAME("Node"))));

	const Variant json = JSON::from_native(source, true);
	CHECK_EQ(JSON::stringify(json), R"({"args":[],"elem_type":{"key_type":"String","type":"Dictionary","value_type":{"type":"Node","type_handle":true}},"type":"Array"})");

	const Variant native = JSON::to_native(json, true);
	REQUIRE_EQ(native.get_type(), Variant::ARRAY);
	const Array decoded = native;
	CHECK_EQ(decoded.get_element_type(), make_dictionary_of(make_builtin(Variant::STRING), make_handle_type(SNAME("Node"))));
}

TEST_CASE("[ContainerType] Native JSON conversion leaves instance-typed containers unchanged") {
	Array source;
	source.set_typed(make_instance_type(SNAME("Node")));

	const Variant json = JSON::from_native(source, true);
	CHECK_EQ(JSON::stringify(json), R"({"args":[],"elem_type":"Node","type":"Array"})");

	const Array decoded = JSON::to_native(json, true);
	CHECK_EQ(decoded.get_element_type(), make_instance_type(SNAME("Node")));
	CHECK_FALSE(decoded.get_element_type().is_type_handle);
}

TEST_CASE("[ContainerType] Native JSON decoding rejects a class handle on a non-object type") {
	Dictionary element_type;
	element_type["type"] = "int";
	element_type["type_handle"] = true;

	Dictionary json;
	json["type"] = "Array";
	json["elem_type"] = element_type;
	json["args"] = Array();

	ERR_PRINT_OFF;
	const Variant native = JSON::to_native(json, true);
	ERR_PRINT_ON;
	CHECK_EQ(native.get_type(), Variant::NIL);
}

TEST_CASE("[ContainerType] Text resource save and load preserve a nested class handle") {
	const ContainerType element_type = make_dictionary_of(make_builtin(Variant::STRING), make_handle_type(SNAME("RefCounted")));

	Array values;
	values.set_typed(element_type);

	Ref<Resource> resource;
	resource.instantiate();
	resource->set_meta("values", values);

	const String save_path = TestUtils::get_temp_path("container_type_handle_transport.tres");
	REQUIRE_EQ(ResourceSaver::save(resource, save_path), OK);

	Ref<Resource> loaded = ResourceLoader::load(save_path, "", ResourceFormatLoader::CACHE_MODE_IGNORE);
	REQUIRE(loaded.is_valid());

	const Variant loaded_values = loaded->get_meta("values");
	REQUIRE_EQ(loaded_values.get_type(), Variant::ARRAY);
	const Array loaded_array = loaded_values;
	CHECK_EQ(loaded_array.get_element_type(), element_type);

	// The loaded container enforces the same rule as the original: a handle is accepted, an instance
	// of the represented class is not, and an incompatible handle is not.
	Dictionary entry;
	entry.set_typed(make_builtin(Variant::STRING), loaded_array.get_element_type().element_types[1]);
	CHECK(entry.set("accepted", make_handle(SNAME("Resource"))));

	ERR_PRINT_OFF;
	Ref<RefCounted> instance;
	instance.instantiate();
	CHECK_FALSE(entry.set("instance", instance));
	CHECK_FALSE(entry.set("incompatible", make_handle(SNAME("Object"))));
	ERR_PRINT_ON;
	CHECK_EQ(entry.size(), 1);
}

TEST_CASE("[ContainerType] A script reachable only through a class handle is saved as a dependency") {
	Ref<TransportTestScript> script;
	script.instantiate();
	script->set_path("res://test_container_type_handle_script.tres");

	ContainerType handle_type;
	handle_type.builtin_type = Variant::OBJECT;
	handle_type.class_name = script->get_instance_base_type();
	handle_type.script = script;
	handle_type.is_type_handle = true;

	Array values;
	values.set_typed(make_dictionary_of(make_builtin(Variant::STRING), handle_type));

	Ref<Resource> resource;
	resource.instantiate();
	resource->set_meta("values", values);

	const String save_path = TestUtils::get_temp_path("container_type_handle_dependency.res");
	REQUIRE_EQ(ResourceSaver::save(resource, save_path), OK);

	List<String> dependencies;
	ResourceLoader::get_dependencies(save_path, &dependencies);

	bool found = false;
	for (const String &dependency : dependencies) {
		if (dependency.contains("res://test_container_type_handle_script.tres")) {
			found = true;
		}
	}
	CHECK(found);
}

TEST_CASE("[ContainerType] A transported class handle validates values identically to the original") {
	const ContainerType handle_type = make_handle_type(SNAME("RefCounted"));

	Array original;
	original.set_typed(handle_type);

	ContainerType decoded_descriptor;
	String error;
	REQUIRE(ContainerTypeDescriptor::from_variant(ContainerTypeDescriptor::to_variant(handle_type), decoded_descriptor, &error));

	Array round_tripped;
	round_tripped.set_typed(decoded_descriptor);

	Array binary_round_tripped = encode_and_decode_binary(original);

	Ref<RefCounted> instance;
	instance.instantiate();
	for (Array *array : { &original, &round_tripped, &binary_round_tripped }) {
		array->push_back(make_handle(SNAME("Resource")));
		ERR_PRINT_OFF;
		array->push_back(instance);
		array->push_back(make_handle(SNAME("Object")));
		ERR_PRINT_ON;
		CHECK_EQ(array->size(), 1);
	}
}

TEST_CASE("[ContainerType] A specialized class handle survives every transport and still rejects a mismatch") {
	ContainerType specialized = make_handle_type(SNAME("RefCounted"));
	specialized.type_arguments.push_back(make_builtin(Variant::INT));
	const ContainerType element_type = make_array_of(specialized);

	Array source;
	source.set_typed(element_type);

	// Descriptor.
	ContainerType decoded_descriptor;
	String error;
	REQUIRE(ContainerTypeDescriptor::from_variant(ContainerTypeDescriptor::to_variant(element_type), decoded_descriptor, &error));
	CHECK_EQ(decoded_descriptor, element_type);

	// Text.
	String text;
	VariantWriter::write_to_string(source, text);
	const Array parsed = parse_text(text);
	CHECK_EQ(parsed.get_element_type(), element_type);

	// Binary.
	const Array binary_decoded = encode_and_decode_binary(source);
	CHECK_EQ(binary_decoded.get_element_type(), element_type);

	// Native JSON.
	const Array json_decoded = JSON::to_native(JSON::from_native(source, true), true);
	CHECK_EQ(json_decoded.get_element_type(), element_type);
}

TEST_CASE("[ContainerType] A transported specialized class handle still rejects a mismatched specialization") {
	// Specialization comparison needs a script-backed slot, so this uses the descriptor transport, which
	// carries the script reference itself rather than a path.
	Ref<TransportTestScript> script;
	script.instantiate();

	ContainerType expected;
	expected.builtin_type = Variant::OBJECT;
	expected.class_name = script->get_instance_base_type();
	expected.script = script;
	expected.type_arguments.push_back(make_builtin(Variant::INT));
	expected.is_type_handle = true;

	ContainerType decoded;
	String error;
	REQUIRE(ContainerTypeDescriptor::from_variant(ContainerTypeDescriptor::to_variant(make_array_of(expected)), decoded, &error));
	CHECK_EQ(decoded, make_array_of(expected));

	Array round_tripped;
	round_tripped.set_typed(decoded.element_types[0]);

	// `Type[Box[String]]` for a `Type[Box[int]]` slot.
	ERR_PRINT_OFF;
	round_tripped.push_back(make_script_handle(script, { make_builtin(Variant::STRING) }));
	ERR_PRINT_ON;
	CHECK_EQ(round_tripped.size(), 0);

	// The matching specialization is still accepted.
	round_tripped.push_back(make_script_handle(script, { make_builtin(Variant::INT) }));
	CHECK_EQ(round_tripped.size(), 1);
}

} // namespace TestContainerTypeHandleTransport
