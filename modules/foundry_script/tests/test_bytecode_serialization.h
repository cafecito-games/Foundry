/**************************************************************************/
/*  test_bytecode_serialization.h                                         */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
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

#ifdef TOOLS_ENABLED

#include "modules/foundry_script/foundry_script.h"
#include "modules/foundry_script/fs_analyzer.h"
#include "modules/foundry_script/fs_bytecode_export.h"
#include "modules/foundry_script/fs_bytecode_format.h"
#include "modules/foundry_script/fs_bytecode_loader.h"
#include "modules/foundry_script/fs_compiler.h"
#include "modules/foundry_script/fs_function.h"
#include "modules/foundry_script/fs_parser.h"

#include "core/config/engine.h"
#include "core/io/stream_peer.h"
#include "core/templates/hash_set.h"
#include "core/templates/pair.h"

#include "tests/test_macros.h"

namespace FSTests {

class BytecodeTestResolver : public FSBytecodeExternalResolver {
public:
	HashMap<String, Ref<Resource>> resources;
	HashMap<String, Ref<Script>> scripts; // Keyed by "path::fully_qualified_name".
	HashSet<String> local_classes; // Same keys as `scripts`.
	Vector<String> resource_requests;
	Vector<Pair<String, String>> script_requests;

	virtual Ref<Resource> resolve_resource(const String &p_path) override {
		resource_requests.push_back(p_path);
		const HashMap<String, Ref<Resource>>::ConstIterator found = resources.find(p_path);
		return found ? found->value : Ref<Resource>();
	}

	virtual Ref<Script> resolve_script(const String &p_path, const String &p_fully_qualified_name, bool &r_is_local_class) override {
		script_requests.push_back(Pair<String, String>(p_path, p_fully_qualified_name));
		const String key = p_path + "::" + p_fully_qualified_name;
		r_is_local_class = local_classes.has(key);
		const HashMap<String, Ref<Script>>::ConstIterator found = scripts.find(key);
		return found ? found->value : Ref<Script>();
	}
};

static Error bytecode_encode_variant(FSBytecodeExporter &r_exporter, const Variant &p_value, Vector<uint8_t> &r_payload) {
	Ref<StreamPeerBuffer> stream;
	stream.instantiate();
	const Error error = r_exporter.encode_variant_tagged(stream.ptr(), p_value);
	r_payload = stream->get_data_array();
	return error;
}

static FSBytecodeLoader bytecode_loader_for(FSBytecodeExporter &r_exporter, FSBytecodeExternalResolver *p_resolver) {
	Ref<StreamPeerBuffer> table_stream;
	table_stream.instantiate();
	r_exporter.get_string_table().write(table_stream.ptr());
	table_stream->seek(0);
	FSBytecodeLoader loader;
	loader.set_resolver(p_resolver);
	REQUIRE(loader.read_string_table(table_stream.ptr()) == OK);
	return loader;
}

static Error bytecode_decode_variant(FSBytecodeExporter &r_exporter, const Vector<uint8_t> &p_payload,
		FSBytecodeExternalResolver *p_resolver, Variant &r_value) {
	FSBytecodeLoader loader = bytecode_loader_for(r_exporter, p_resolver);
	Ref<StreamPeerBuffer> payload_stream;
	payload_stream.instantiate();
	payload_stream->set_data_array(p_payload);
	return loader.decode_variant_tagged(payload_stream.ptr(), r_value);
}

static Variant bytecode_round_trip_variant(const Variant &p_value, FSBytecodeExternalResolver *p_resolver = nullptr) {
	FSBytecodeExporter exporter;
	Vector<uint8_t> payload;
	REQUIRE(bytecode_encode_variant(exporter, p_value, payload) == OK);
	BytecodeTestResolver fallback_resolver;
	Variant decoded;
	REQUIRE(bytecode_decode_variant(exporter, payload, p_resolver != nullptr ? p_resolver : &fallback_resolver, decoded) == OK);
	return decoded;
}

static FSDataType bytecode_round_trip_data_type(const FSDataType &p_data_type, FSBytecodeExternalResolver *p_resolver = nullptr) {
	FSBytecodeExporter exporter;
	Ref<StreamPeerBuffer> payload_stream;
	payload_stream.instantiate();
	REQUIRE(exporter.encode_data_type(payload_stream.ptr(), p_data_type) == OK);
	BytecodeTestResolver fallback_resolver;
	FSBytecodeLoader loader = bytecode_loader_for(exporter, p_resolver != nullptr ? p_resolver : &fallback_resolver);
	payload_stream->seek(0);
	FSDataType decoded;
	REQUIRE(loader.decode_data_type(payload_stream.ptr(), decoded) == OK);
	return decoded;
}

static bool bytecode_buffer_contains(const Vector<uint8_t> &p_buffer, const String &p_marker) {
	const CharString marker = p_marker.utf8();
	const int marker_length = marker.length();
	if (marker_length == 0) {
		return false;
	}
	for (int i = 0; i + marker_length <= p_buffer.size(); i++) {
		if (memcmp(&p_buffer[i], marker.get_data(), marker_length) == 0) {
			return true;
		}
	}
	return false;
}

static Ref<FoundryScript> compile_bytecode_test_source(const String &p_source) {
	if (!FSLanguage::get_singleton()->has_any_global_constant(SNAME("RefCounted"))) {
		FSLanguage::get_singleton()->init();
	}

	static int bytecode_test_script_index = 0;
	const String path = vformat("user://test_bytecode_serialization_%d.fs", bytecode_test_script_index++);

	Ref<FoundryScript> script;
	script.instantiate();
	script->set_path(path);
	script->set_source_code(p_source);

	FSParser parser;
	Error error = parser.parse(p_source, script->get_path(), false);
	REQUIRE(error == OK);

	FSAnalyzer analyzer(&parser);
	error = analyzer.analyze();
	REQUIRE(error == OK);

	FSCompiler compiler;
	error = compiler.compile(&parser, script.ptr(), false);
	REQUIRE(error == OK);

	return script;
}

TEST_CASE("[FoundryScript][BytecodeCodec] Header round-trip and engine guard") {
	Vector<uint8_t> buffer = FSBytecodeExporter::write_header();
	int header_size = 0;
	CHECK(FSBytecodeLoader::check_header(buffer, &header_size) == OK);
	CHECK(header_size == buffer.size());

	// Flip one byte inside the engine-guard version string region.
	buffer.write[16] ^= 0xFF;
	ERR_PRINT_OFF;
	CHECK(FSBytecodeLoader::check_header(buffer) == ERR_INVALID_DATA);
	ERR_PRINT_ON;

	Vector<uint8_t> bad_magic = FSBytecodeExporter::write_header();
	bad_magic.write[0] ^= 0xFF;
	ERR_PRINT_OFF;
	CHECK(FSBytecodeLoader::check_header(bad_magic) == ERR_INVALID_DATA);
	ERR_PRINT_ON;

	Vector<uint8_t> truncated = FSBytecodeExporter::write_header();
	truncated.resize(10);
	ERR_PRINT_OFF;
	CHECK(FSBytecodeLoader::check_header(truncated) == ERR_INVALID_DATA);
	ERR_PRINT_ON;
}

TEST_CASE("[FoundryScript][BytecodeCodec] String table dedup and round-trip") {
	FSBytecodeExporter::StringTable table;
	const StringName member_name("player_speed");
	const uint32_t a = table.insert(member_name);
	const uint32_t b = table.insert(member_name);
	const uint32_t c = table.insert(String::utf8("ñandú_速度"));
	CHECK(a == b);
	CHECK(a != c);

	Ref<StreamPeerBuffer> stream;
	stream.instantiate();
	table.write(stream.ptr());
	stream->seek(0);

	FSBytecodeLoader loader;
	REQUIRE(loader.read_string_table(stream.ptr()) == OK);
	CHECK(loader.get_string(a) == "player_speed");
	CHECK(loader.get_string(c) == String::utf8("ñandú_速度"));

	ERR_PRINT_OFF;
	CHECK(loader.get_string(c + 1) == String());
	ERR_PRINT_ON;
}

TEST_CASE("[FoundryScript][BytecodeCodec] Tagged Variant round-trip") {
	Vector<Variant> values;
	values.push_back(Variant());
	values.push_back(true);
	values.push_back((int64_t)-42);
	values.push_back(3.5);
	values.push_back(String("hello world"));
	values.push_back(StringName("signal_name"));
	values.push_back(NodePath("Path/To/Node:property"));
	values.push_back(Vector2(1.5, -2.5));
	values.push_back(Vector2i(3, -4));
	values.push_back(Rect2(1, 2, 3, 4));
	values.push_back(Rect2i(1, 2, 3, 4));
	values.push_back(Vector3(1, 2, 3));
	values.push_back(Vector3i(4, 5, 6));
	values.push_back(Transform2D(0.5, Vector2(7, 8)));
	values.push_back(Vector4(1, 2, 3, 4));
	values.push_back(Vector4i(5, 6, 7, 8));
	values.push_back(Plane(Vector3(0, 1, 0), 2.0));
	values.push_back(Quaternion(0, 0, 0, 1));
	values.push_back(AABB(Vector3(1, 2, 3), Vector3(4, 5, 6)));
	values.push_back(Basis::from_scale(Vector3(2, 2, 2)));
	values.push_back(Transform3D(Basis(), Vector3(9, 8, 7)));
	values.push_back(Projection());
	values.push_back(Color(0.25, 0.5, 0.75, 1.0));
	values.push_back(PackedByteArray({ 1, 2, 3 }));
	values.push_back(PackedInt32Array({ -1, 0, 1 }));
	values.push_back(PackedInt64Array({ -10, 0, 10 }));
	values.push_back(PackedFloat32Array({ 0.5f, 1.5f }));
	values.push_back(PackedFloat64Array({ 0.25, 1.25 }));
	values.push_back(PackedStringArray({ "one", "two" }));
	values.push_back(PackedVector2Array({ Vector2(1, 2) }));
	values.push_back(PackedVector3Array({ Vector3(1, 2, 3) }));
	values.push_back(PackedColorArray({ Color(1, 0, 0) }));
	values.push_back(PackedVector4Array({ Vector4(1, 2, 3, 4) }));

	for (int i = 0; i < values.size(); i++) {
		CAPTURE(i);
		const Variant decoded = bytecode_round_trip_variant(values[i]);
		CHECK(decoded.get_type() == values[i].get_type());
		CHECK(decoded.hash_compare(values[i]));
	}

	// Nested untyped containers recurse element-wise.
	Array inner_array;
	inner_array.push_back((int64_t)1);
	inner_array.push_back(String("two"));
	Dictionary inner_dictionary;
	inner_dictionary["position"] = Vector2(3, 4);
	inner_dictionary["values"] = inner_array;
	Array outer_array;
	outer_array.push_back(inner_dictionary);
	outer_array.push_back(inner_array);
	const Variant decoded_nested = bytecode_round_trip_variant(outer_array);
	CHECK(decoded_nested.hash_compare(outer_array));

	// Typed array metadata survives.
	Array typed_array;
	typed_array.set_typed(Variant::INT, StringName(), Variant());
	typed_array.push_back((int64_t)1);
	typed_array.push_back((int64_t)2);
	const Variant decoded_typed = bytecode_round_trip_variant(typed_array);
	const Array decoded_typed_array = decoded_typed;
	CHECK(decoded_typed_array.is_typed());
	CHECK(decoded_typed_array.get_element_type() == typed_array.get_element_type());
	CHECK(decoded_typed.hash_compare(typed_array));

	// Nested typed containers: Array[Array[int]] keeps the inner element type.
	ContainerType integer_type;
	integer_type.builtin_type = Variant::INT;
	ContainerType integer_array_type;
	integer_array_type.builtin_type = Variant::ARRAY;
	integer_array_type.element_types.push_back(integer_type);
	Array nested_typed_array;
	nested_typed_array.set_typed(integer_array_type);
	Array nested_element;
	nested_element.set_typed(integer_type);
	nested_element.push_back((int64_t)5);
	nested_typed_array.push_back(nested_element);
	const Variant decoded_nested_typed = bytecode_round_trip_variant(nested_typed_array);
	const Array decoded_nested_typed_array = decoded_nested_typed;
	CHECK(decoded_nested_typed_array.get_element_type() == nested_typed_array.get_element_type());
	CHECK(decoded_nested_typed.hash_compare(nested_typed_array));

	// Typed dictionary metadata survives.
	Dictionary typed_dictionary;
	typed_dictionary.set_typed(Variant::STRING, StringName(), Variant(), Variant::INT, StringName(), Variant());
	typed_dictionary["score"] = (int64_t)10;
	const Variant decoded_dictionary = bytecode_round_trip_variant(typed_dictionary);
	const Dictionary decoded_typed_dictionary = decoded_dictionary;
	CHECK(decoded_typed_dictionary.is_typed());
	CHECK(decoded_typed_dictionary.get_key_type() == typed_dictionary.get_key_type());
	CHECK(decoded_typed_dictionary.get_value_type() == typed_dictionary.get_value_type());
	CHECK(decoded_dictionary.hash_compare(typed_dictionary));
}

TEST_CASE("[FoundryScript][BytecodeCodec] Script-typed containers route the script through tagging") {
	Ref<FoundryScript> element_script;
	element_script.instantiate();
	element_script->set_path_cache("res://element_type.fs");

	ContainerType script_element_type;
	script_element_type.builtin_type = Variant::OBJECT;
	script_element_type.class_name = "RefCounted";
	script_element_type.script = element_script;
	Array script_typed_array;
	script_typed_array.set_typed(script_element_type);

	FSBytecodeExporter exporter;
	Vector<uint8_t> payload;
	REQUIRE(bytecode_encode_variant(exporter, script_typed_array, payload) == OK);

	BytecodeTestResolver resolver;
	resolver.scripts.insert("res://element_type.fs::", element_script);
	Variant decoded;
	REQUIRE(bytecode_decode_variant(exporter, payload, &resolver, decoded) == OK);
	const Array decoded_array = decoded;
	CHECK(decoded_array.is_typed());
	CHECK(decoded_array.get_element_type() == script_typed_array.get_element_type());
	REQUIRE(resolver.script_requests.size() == 1);
	CHECK(resolver.script_requests[0].first == "res://element_type.fs");
}

TEST_CASE("[FoundryScript][BytecodeCodec] Resource constants become external references") {
	Ref<Resource> resource;
	resource.instantiate();
	resource->set_path_cache("res://icon.png");
	resource->set_meta("secret", "THIS_MUST_NOT_SERIALIZE");

	FSBytecodeExporter exporter;
	Vector<uint8_t> payload;
	REQUIRE(bytecode_encode_variant(exporter, resource, payload) == OK);

	// The payload is exactly one tag byte plus one u32 string-table index: no property data.
	CHECK(payload.size() == 5);
	CHECK(payload[0] == (uint8_t)FSBytecodeFormat::TAG_EXTERNAL_RESOURCE);
	CHECK(!bytecode_buffer_contains(payload, "THIS_MUST_NOT_SERIALIZE"));

	Ref<StreamPeerBuffer> table_stream;
	table_stream.instantiate();
	exporter.get_string_table().write(table_stream.ptr());
	const Vector<uint8_t> table_bytes = table_stream->get_data_array();
	CHECK(!bytecode_buffer_contains(table_bytes, "THIS_MUST_NOT_SERIALIZE"));
	CHECK(bytecode_buffer_contains(table_bytes, "res://icon.png"));

	BytecodeTestResolver resolver;
	Ref<Resource> replacement;
	replacement.instantiate();
	replacement->set_path_cache("res://icon.png");
	resolver.resources.insert("res://icon.png", replacement);

	Variant decoded;
	REQUIRE(bytecode_decode_variant(exporter, payload, &resolver, decoded) == OK);
	const Ref<Resource> decoded_resource = decoded;
	REQUIRE(decoded_resource.is_valid());
	CHECK(decoded_resource == replacement);
	REQUIRE(resolver.resource_requests.size() == 1);
	CHECK(resolver.resource_requests[0] == "res://icon.png");
}

TEST_CASE("[FoundryScript][BytecodeCodec] Native class and engine singleton constants round-trip") {
	const Ref<FSNativeClass> native_class = Ref<FSNativeClass>(memnew(FSNativeClass(StringName("Node"))));
	const Variant decoded_native = bytecode_round_trip_variant(native_class);
	const Ref<FSNativeClass> decoded_native_class = decoded_native;
	REQUIRE(decoded_native_class.is_valid());
	CHECK(decoded_native_class->get_name() == StringName("Node"));

	Object *project_settings = Engine::get_singleton()->get_singleton_object(SNAME("ProjectSettings"));
	REQUIRE(project_settings != nullptr);
	const Variant decoded_singleton = bytecode_round_trip_variant(project_settings);
	CHECK(decoded_singleton.operator Object *() == project_settings);
}

TEST_CASE("[FoundryScript][BytecodeCodec] Process-bound Variants are rejected") {
	FSBytecodeExporter exporter;
	Vector<uint8_t> payload;

	ERR_PRINT_OFF;
	CHECK(bytecode_encode_variant(exporter, Callable(), payload) == ERR_INVALID_PARAMETER);
	CHECK(bytecode_encode_variant(exporter, ::RID(), payload) == ERR_INVALID_PARAMETER);
	CHECK(bytecode_encode_variant(exporter, Signal(), payload) == ERR_INVALID_PARAMETER);

	Object *bare_object = memnew(Object);
	CHECK(bytecode_encode_variant(exporter, bare_object, payload) == ERR_INVALID_PARAMETER);
	memdelete(bare_object);

	// The interception also applies inside containers.
	Array array_with_callable;
	array_with_callable.push_back(Callable());
	CHECK(bytecode_encode_variant(exporter, array_with_callable, payload) == ERR_INVALID_PARAMETER);

	Dictionary dictionary_with_signal;
	dictionary_with_signal["on_hit"] = Signal();
	CHECK(bytecode_encode_variant(exporter, dictionary_with_signal, payload) == ERR_INVALID_PARAMETER);

	// A resource without a path cannot be externalized.
	Ref<Resource> pathless_resource;
	pathless_resource.instantiate();
	CHECK(bytecode_encode_variant(exporter, pathless_resource, payload) == ERR_INVALID_PARAMETER);
	ERR_PRINT_ON;
}

TEST_CASE("[FoundryScript][BytecodeCodec] FSDataType round-trip") {
	Ref<FoundryScript> referenced_script;
	referenced_script.instantiate();
	referenced_script->set_path_cache("res://referenced.fs");
	BytecodeTestResolver resolver;
	resolver.scripts.insert("res://referenced.fs::", referenced_script);

	Vector<FSDataType> data_types;

	FSDataType variant_type;
	data_types.push_back(variant_type);

	FSDataType nullable_float;
	nullable_float.kind = FSDataType::BUILTIN;
	nullable_float.builtin_type = Variant::FLOAT;
	nullable_float.is_nullable = true;
	data_types.push_back(nullable_float);

	FSDataType integer_type;
	integer_type.kind = FSDataType::BUILTIN;
	integer_type.builtin_type = Variant::INT;

	FSDataType integer_array;
	integer_array.kind = FSDataType::BUILTIN;
	integer_array.builtin_type = Variant::ARRAY;
	integer_array.set_container_element_type(0, integer_type);
	data_types.push_back(integer_array);

	FSDataType native_node;
	native_node.kind = FSDataType::NATIVE;
	native_node.builtin_type = Variant::OBJECT;
	native_node.native_type = "Node2D";

	FSDataType string_type;
	string_type.kind = FSDataType::BUILTIN;
	string_type.builtin_type = Variant::STRING;

	FSDataType typed_dictionary;
	typed_dictionary.kind = FSDataType::BUILTIN;
	typed_dictionary.builtin_type = Variant::DICTIONARY;
	typed_dictionary.set_container_element_type(0, string_type);
	typed_dictionary.set_container_element_type(1, native_node);
	data_types.push_back(typed_dictionary);
	data_types.push_back(native_node);

	FSDataType self_native;
	self_native.kind = FSDataType::NATIVE;
	self_native.builtin_type = Variant::OBJECT;
	self_native.native_type = "RefCounted";
	self_native.is_self_type = true;
	data_types.push_back(self_native);

	FSDataType script_type;
	script_type.kind = FSDataType::SCRIPT;
	script_type.builtin_type = Variant::OBJECT;
	script_type.native_type = "RefCounted";
	script_type.script_type_ref = referenced_script;
	script_type.script_type = referenced_script.ptr();
	data_types.push_back(script_type);

	FSDataType foundry_script_type;
	foundry_script_type.kind = FSDataType::FOUNDRY_SCRIPT;
	foundry_script_type.builtin_type = Variant::OBJECT;
	foundry_script_type.native_type = "RefCounted";
	foundry_script_type.script_type_ref = referenced_script;
	foundry_script_type.script_type = referenced_script.ptr();
	data_types.push_back(foundry_script_type);

	FSDataType trait_type;
	trait_type.kind = FSDataType::FOUNDRY_SCRIPT;
	trait_type.builtin_type = Variant::OBJECT;
	trait_type.native_type = "RefCounted";
	trait_type.is_script_trait = true;
	trait_type.script_trait = "Damageable";
	data_types.push_back(trait_type);

	FSDataType type_parameter;
	type_parameter.kind = FSDataType::TYPE_PARAMETER;
	type_parameter.type_parameter_name = "T";
	type_parameter.type_parameter_index = 1;
	type_parameter.type_parameter_scope = FSDataType::TYPE_PARAMETER_CLASS;
	data_types.push_back(type_parameter);

	FSDataType specialized_handle;
	specialized_handle.kind = FSDataType::FOUNDRY_SCRIPT;
	specialized_handle.builtin_type = Variant::OBJECT;
	specialized_handle.native_type = "RefCounted";
	specialized_handle.script_type_ref = referenced_script;
	specialized_handle.script_type = referenced_script.ptr();
	specialized_handle.is_type_handle = true;
	specialized_handle.type_arguments.push_back(integer_type);
	specialized_handle.type_arguments.push_back(type_parameter);
	data_types.push_back(specialized_handle);

	for (int i = 0; i < data_types.size(); i++) {
		CAPTURE(i);
		const FSDataType decoded = bytecode_round_trip_data_type(data_types[i], &resolver);
		CHECK(decoded == data_types[i]);
		CHECK(decoded.native_type == data_types[i].native_type);
		CHECK(decoded.is_nullable == data_types[i].is_nullable);
		CHECK(decoded.container_element_types.size() == data_types[i].container_element_types.size());
		CHECK(decoded.type_arguments.size() == data_types[i].type_arguments.size());
	}
}

TEST_CASE("[FoundryScript][BytecodeCodec] FSDataType script references serialize as path and fully qualified name") {
	const Ref<FoundryScript> script = compile_bytecode_test_source(
			"class Inner:\n"
			"\tvar value: int = 0\n");
	const HashMap<StringName, Ref<FoundryScript>>::ConstIterator inner_element = script->get_subclasses().find(SNAME("Inner"));
	REQUIRE(inner_element);
	const Ref<FoundryScript> inner_script = inner_element->value;
	REQUIRE(inner_script.is_valid());
	REQUIRE(!inner_script->get_fully_qualified_name().is_empty());

	FSDataType data_type;
	data_type.kind = FSDataType::FOUNDRY_SCRIPT;
	data_type.builtin_type = Variant::OBJECT;
	data_type.native_type = "RefCounted";
	data_type.script_type_ref = inner_script;
	data_type.script_type = inner_script.ptr();

	BytecodeTestResolver resolver;
	resolver.scripts.insert(inner_script->get_script_path() + "::" + inner_script->get_fully_qualified_name(), inner_script);

	const FSDataType decoded = bytecode_round_trip_data_type(data_type, &resolver);
	CHECK(decoded == data_type);
	CHECK(decoded.script_type_ref == inner_script);

	// The script identity traveled purely as (path, fully qualified name) strings through the
	// resolver; no pointer is stored in the payload.
	REQUIRE(resolver.script_requests.size() == 1);
	CHECK(resolver.script_requests[0].first == inner_script->get_script_path());
	CHECK(resolver.script_requests[0].second == inner_script->get_fully_qualified_name());
}

TEST_CASE("[FoundryScript][BytecodeCodec] Corrupted inline Variants fail the decode") {
	FSBytecodeExporter exporter;
	BytecodeTestResolver resolver;

	// A legitimate nil still round-trips through the bounded decode path.
	Vector<uint8_t> nil_payload;
	REQUIRE(bytecode_encode_variant(exporter, Variant(), nil_payload) == OK);
	Variant decoded = true;
	REQUIRE(bytecode_decode_variant(exporter, nil_payload, &resolver, decoded) == OK);
	CHECK(decoded.get_type() == Variant::NIL);

	// A corrupted 4-byte length prefix (after the tag byte) must fail cleanly instead of
	// attempting a huge allocation.
	Vector<uint8_t> corrupted_length = nil_payload;
	REQUIRE(corrupted_length.size() >= 5);
	corrupted_length.write[1] = 0xFF;
	corrupted_length.write[2] = 0xFF;
	corrupted_length.write[3] = 0xFF;
	corrupted_length.write[4] = 0x7F;
	ERR_PRINT_OFF;
	CHECK(bytecode_decode_variant(exporter, corrupted_length, &resolver, decoded) == ERR_INVALID_DATA);
	ERR_PRINT_ON;

	// A truncated payload must fail instead of silently decoding as nil.
	Vector<uint8_t> string_payload;
	REQUIRE(bytecode_encode_variant(exporter, String("truncate me"), string_payload) == OK);
	Vector<uint8_t> truncated = string_payload;
	truncated.resize(truncated.size() - 4);
	ERR_PRINT_OFF;
	CHECK(bytecode_decode_variant(exporter, truncated, &resolver, decoded) == ERR_INVALID_DATA);
	ERR_PRINT_ON;

	// Garbage bytes that pass the length check must still fail encode_variant's own decoding.
	Vector<uint8_t> garbage = nil_payload;
	for (int i = 5; i < garbage.size(); i++) {
		garbage.write[i] = 0xFF;
	}
	ERR_PRINT_OFF;
	CHECK(bytecode_decode_variant(exporter, garbage, &resolver, decoded) == ERR_INVALID_DATA);
	ERR_PRINT_ON;
}

TEST_CASE("[FoundryScript][BytecodeCodec] FSDataType local-class references link without a strong reference") {
	Ref<FoundryScript> referenced_script;
	referenced_script.instantiate();
	referenced_script->set_path_cache("res://linkage.fs");

	FSDataType data_type;
	data_type.kind = FSDataType::FOUNDRY_SCRIPT;
	data_type.builtin_type = Variant::OBJECT;
	data_type.native_type = "RefCounted";
	data_type.script_type_ref = referenced_script;
	data_type.script_type = referenced_script.ptr();

	// External references keep the strong reference.
	BytecodeTestResolver external_resolver;
	external_resolver.scripts.insert("res://linkage.fs::", referenced_script);
	const FSDataType external_decoded = bytecode_round_trip_data_type(data_type, &external_resolver);
	CHECK(external_decoded.script_type_ref == Ref<Script>(referenced_script));
	CHECK(external_decoded.script_type == referenced_script.ptr());

	// Classes local to the loaded file link as a raw pointer only, per the local-class
	// no-strong-ref rule (see FoundryScript::TypeArgumentBinding in foundry_script.h).
	BytecodeTestResolver local_resolver;
	local_resolver.scripts.insert("res://linkage.fs::", referenced_script);
	local_resolver.local_classes.insert("res://linkage.fs::");
	const FSDataType local_decoded = bytecode_round_trip_data_type(data_type, &local_resolver);
	CHECK(local_decoded.script_type == referenced_script.ptr());
	CHECK(local_decoded.script_type_ref.is_null());
}

TEST_CASE("[FoundryScript][BytecodeCodec] Specialized class handles round-trip") {
	const Ref<FoundryScript> script = compile_bytecode_test_source(
			"class Box[T]:\n"
			"\tvar value\n");
	const HashMap<StringName, Ref<FoundryScript>>::ConstIterator box_element = script->get_subclasses().find(SNAME("Box"));
	REQUIRE(box_element);
	const Ref<FoundryScript> box_script = box_element->value;
	REQUIRE(box_script.is_valid());

	ContainerType integer_argument;
	integer_argument.builtin_type = Variant::INT;
	Vector<ContainerType> type_arguments;
	type_arguments.push_back(integer_argument);
	const Ref<FSSpecializedClassHandle> handle = FSSpecializedClassHandle::create(box_script, type_arguments);
	REQUIRE(handle.is_valid());

	BytecodeTestResolver resolver;
	resolver.scripts.insert(box_script->get_script_path() + "::" + box_script->get_fully_qualified_name(), box_script);
	const Variant decoded = bytecode_round_trip_variant(handle, &resolver);
	const Ref<FSSpecializedClassHandle> decoded_handle = decoded;
	REQUIRE(decoded_handle.is_valid());
	CHECK(decoded_handle->get_specialized_script() == box_script);
	REQUIRE(decoded_handle->get_type_arguments().size() == 1);
	CHECK(decoded_handle->get_type_arguments()[0] == integer_argument);
}

TEST_CASE("[FoundryScript][BytecodeCodec] Unresolvable external references fail the decode") {
	Ref<Resource> resource;
	resource.instantiate();
	resource->set_path_cache("res://missing.png");

	FSBytecodeExporter exporter;
	Vector<uint8_t> payload;
	REQUIRE(bytecode_encode_variant(exporter, resource, payload) == OK);

	BytecodeTestResolver empty_resolver;
	Variant decoded;
	ERR_PRINT_OFF;
	CHECK(bytecode_decode_variant(exporter, payload, &empty_resolver, decoded) == ERR_CANT_RESOLVE);
	ERR_PRINT_ON;
}

} // namespace FSTests

#endif // TOOLS_ENABLED
