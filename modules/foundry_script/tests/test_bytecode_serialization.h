/**************************************************************************/
/*  test_bytecode_serialization.h                                         */
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

#ifdef TOOLS_ENABLED

#include "modules/foundry_script/foundry_script.h"
#include "modules/foundry_script/fs_analyzer.h"
#include "modules/foundry_script/fs_byte_codegen.h"
#include "modules/foundry_script/fs_bytecode_export.h"
#include "modules/foundry_script/fs_bytecode_format.h"
#include "modules/foundry_script/fs_bytecode_loader.h"
#include "modules/foundry_script/fs_cache.h"
#include "modules/foundry_script/fs_compiler.h"
#include "modules/foundry_script/fs_function.h"
#include "modules/foundry_script/fs_parser.h"
#include "modules/foundry_script/fs_reflection.h"
#include "modules/foundry_script/fs_utility_callable.h"

#include "core/config/engine.h"
#include "core/config/project_settings.h"
#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/io/stream_peer.h"
#include "core/templates/hash_set.h"
#include "core/templates/pair.h"

#include "tests/test_macros.h"
#include "tests/test_utils.h"

namespace FSTests {

class TestFSLanguageGlobalsAccessor {
public:
	static void remove_global(const StringName &p_name) {
		FSLanguage::get_singleton()->_remove_global(p_name);
	}
};

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
	if (!FSLanguage::get_singleton()->get_reflection_singleton().is_valid()) {
		FSLanguage::get_singleton()->init();
	}

	static int bytecode_test_script_index = 0;
	const String path = TestUtils::get_temp_path(vformat("test_bytecode_serialization_%d.fs", bytecode_test_script_index++));
	{
		Ref<FileAccess> fixture_file = FileAccess::open(path, FileAccess::WRITE);
		REQUIRE(fixture_file.is_valid());
		fixture_file->store_string(p_source);
	}

	// Global warning levels can be elevated to errors by earlier language initialization; these
	// fixtures test serialization, not diagnostics, so warnings are ignored for the whole
	// parse-analyze-compile pipeline and the previous state is restored afterwards.
	const bool previous_ignore_warnings = FSParser::is_ignoring_warnings();
	FSParser::set_ignoring_warnings(true);

	// Obtain the script through FSCache, mirroring how production loading publishes a script before
	// compiling it: analyzer reductions that resolve this path through the cache (e.g. `Inner.new()`
	// folding to the cached script's class) must land on the same script object compiled here.
	Error error = OK;
	Ref<FoundryScript> script = FSCache::get_shallow_script(path, error);
	if (script.is_null()) {
		FSParser::set_ignoring_warnings(previous_ignore_warnings);
	}
	REQUIRE(error == OK);
	REQUIRE(script.is_valid());

	FSParser parser;
	error = parser.parse(p_source, path, false);
	if (error == OK) {
		FSAnalyzer analyzer(&parser);
		error = analyzer.analyze();
		if (error == OK) {
			FSCompiler compiler;
			error = compiler.compile(&parser, script.ptr(), false);
		}
	}
	FSParser::set_ignoring_warnings(previous_ignore_warnings);
	if (error != OK) {
		for (const FSParser::ParserError &parser_error : parser.get_errors()) {
			MESSAGE(vformat("Fixture error at line %d: %s", parser_error.line, parser_error.message));
		}
	}
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
	FSLanguage *language = FSLanguage::get_singleton();
	if (!language->has_any_global_constant(SNAME("RefCounted"))) {
		language->init();
	}

	const Ref<FSNativeClass> native_class = Ref<FSNativeClass>(memnew(FSNativeClass(StringName("Node"))));
	const Variant decoded_native = bytecode_round_trip_variant(native_class);
	const Ref<FSNativeClass> decoded_native_class = decoded_native;
	REQUIRE(decoded_native_class.is_valid());
	CHECK(decoded_native_class->get_name() == StringName("Node"));
	// Class-handle equality is object identity, so the decoded handle must be the language's
	// canonical global handle, not merely a same-named copy.
	REQUIRE(language->has_any_global_constant(SNAME("Node")));
	CHECK(decoded_native.operator Object *() == language->get_any_global_constant(SNAME("Node")).operator Object *());

	Object *project_settings = Engine::get_singleton()->get_singleton_object(SNAME("ProjectSettings"));
	REQUIRE(project_settings != nullptr);
	const Variant decoded_singleton = bytecode_round_trip_variant(project_settings);
	CHECK(decoded_singleton.operator Object *() == project_settings);
}

TEST_CASE("[FoundryScript][BytecodeCodec] Reflection singletons round-trip by identity") {
	FSLanguage *language = FSLanguage::get_singleton();
	// The reflection singletons only exist between init() and finish(); a preceding suite may have
	// finished the language while leaving its globals populated, so guard on the singleton itself.
	if (language->get_reflection_singleton().is_null()) {
		language->init();
	}

	const Ref<FSReflection> reflection = language->get_reflection_singleton();
	REQUIRE(reflection.is_valid());
	const Variant decoded_reflection = bytecode_round_trip_variant(reflection);
	CHECK(decoded_reflection.operator Object *() == reflection.ptr());

	const Ref<FSNamespace> namespace_singleton = language->get_namespace_singleton();
	REQUIRE(namespace_singleton.is_valid());
	const Variant decoded_namespace = bytecode_round_trip_variant(namespace_singleton);
	CHECK(decoded_namespace.operator Object *() == namespace_singleton.ptr());
}

TEST_CASE("[FoundryScript][BytecodeCodec] Process-bound Variants are rejected") {
	FSBytecodeExporter exporter;
	Vector<uint8_t> payload;

	Object *bare_object = memnew(Object);

	ERR_PRINT_OFF;
	CHECK(bytecode_encode_variant(exporter, Callable(bare_object, "method"), payload) == ERR_INVALID_PARAMETER);
	CHECK(bytecode_encode_variant(exporter, ::RID::from_uint64(1), payload) == ERR_INVALID_PARAMETER);
	CHECK(bytecode_encode_variant(exporter, Signal(bare_object, "changed"), payload) == ERR_INVALID_PARAMETER);

	CHECK(bytecode_encode_variant(exporter, bare_object, payload) == ERR_INVALID_PARAMETER);

	// The interception also applies inside containers.
	Array array_with_callable;
	array_with_callable.push_back(Callable(bare_object, "method"));
	CHECK(bytecode_encode_variant(exporter, array_with_callable, payload) == ERR_INVALID_PARAMETER);

	Dictionary dictionary_with_signal;
	dictionary_with_signal["on_hit"] = Signal(bare_object, "changed");
	CHECK(bytecode_encode_variant(exporter, dictionary_with_signal, payload) == ERR_INVALID_PARAMETER);

	// A resource without a path cannot be externalized.
	Ref<Resource> pathless_resource;
	pathless_resource.instantiate();
	CHECK(bytecode_encode_variant(exporter, pathless_resource, payload) == ERR_INVALID_PARAMETER);
	ERR_PRINT_ON;

	memdelete(bare_object);
}

TEST_CASE("[FoundryScript][BytecodeCodec] Portable process-bound values round-trip") {
	// The default-constructed values of the process-bound types carry no process state, and the
	// analyzer folds them into constant pools (e.g. `Callable()` arguments and `RID()` literals).
	CHECK(Callable(bytecode_round_trip_variant(Callable())).is_null());
	CHECK(Signal(bytecode_round_trip_variant(Signal())).is_null());
	CHECK(!::RID(bytecode_round_trip_variant(::RID())).is_valid());

	// A utility-function callable is a pure name (the analyzer folds `absf` used as a value into
	// one) and rebuilds by name.
	const Callable utility_callable = Callable(memnew(FSUtilityCallable(SNAME("absf"))));
	const Variant decoded = bytecode_round_trip_variant(utility_callable);
	REQUIRE(decoded.get_type() == Variant::CALLABLE);
	const Callable decoded_callable = decoded;
	CHECK(decoded_callable.is_custom());
	CHECK(decoded_callable.get_method() == StringName("absf"));
	const Variant minus_two = -2.0;
	const Variant *utility_arguments[1] = { &minus_two };
	Variant utility_result;
	Callable::CallError utility_error;
	decoded_callable.callp(utility_arguments, 1, utility_result, utility_error);
	CHECK(utility_error.error == Callable::CallError::CALL_OK);
	CHECK((double)utility_result == 2.0);

	// Read-only container flags survive: constants are baked deeply read-only, and a loaded
	// constant must reject mutation exactly like the compiled one.
	Array read_only_array;
	read_only_array.push_back(1);
	read_only_array.make_read_only();
	CHECK(Array(bytecode_round_trip_variant(read_only_array)).is_read_only());
	Dictionary read_only_dictionary;
	read_only_dictionary["key"] = 2;
	read_only_dictionary.make_read_only();
	CHECK(Dictionary(bytecode_round_trip_variant(read_only_dictionary)).is_read_only());

	// A tampered utility name fails the decode eagerly with ERR_CANT_RESOLVE instead of producing
	// a callable that only breaks at call time.
	{
		FSBytecodeExporter tamper_exporter;
		Vector<uint8_t> tampered_payload;
		REQUIRE(bytecode_encode_variant(tamper_exporter, Callable(memnew(FSUtilityCallable(SNAME("absf")))), tampered_payload) == OK);

		Ref<StreamPeerBuffer> table_stream;
		table_stream.instantiate();
		tamper_exporter.get_string_table().write(table_stream.ptr());
		Vector<uint8_t> table_bytes = table_stream->get_data_array();
		const CharString marker = String("absf").utf8();
		const CharString replacement = String("zzzz").utf8();
		bool patched = false;
		for (int i = 0; i + marker.length() <= table_bytes.size(); i++) {
			if (memcmp(&table_bytes[i], marker.get_data(), marker.length()) == 0) {
				memcpy(&table_bytes.write[i], replacement.get_data(), replacement.length());
				patched = true;
				break;
			}
		}
		REQUIRE(patched);

		Ref<StreamPeerBuffer> tampered_table_stream;
		tampered_table_stream.instantiate();
		tampered_table_stream->set_data_array(table_bytes);
		BytecodeTestResolver tamper_resolver;
		FSBytecodeLoader tampered_loader;
		tampered_loader.set_resolver(&tamper_resolver);
		REQUIRE(tampered_loader.read_string_table(tampered_table_stream.ptr()) == OK);

		Ref<StreamPeerBuffer> tampered_payload_stream;
		tampered_payload_stream.instantiate();
		tampered_payload_stream->set_data_array(tampered_payload);
		Variant tampered_value;
		ERR_PRINT_OFF;
		CHECK(tampered_loader.decode_variant_tagged(tampered_payload_stream.ptr(), tampered_value) == ERR_CANT_RESOLVE);
		ERR_PRINT_ON;
	}
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

	FSDataType tuple_type;
	tuple_type.kind = FSDataType::TUPLE;
	tuple_type.builtin_type = Variant::ARRAY;
	tuple_type.container_element_types.push_back(integer_type);
	tuple_type.container_element_types.push_back(nullable_float);
	data_types.push_back(tuple_type);

	FSDataType nested_tuple_type;
	nested_tuple_type.kind = FSDataType::TUPLE;
	nested_tuple_type.builtin_type = Variant::ARRAY;
	nested_tuple_type.container_element_types.push_back(tuple_type);
	nested_tuple_type.container_element_types.push_back(string_type);
	data_types.push_back(nested_tuple_type);

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

TEST_CASE("[FoundryScript][BytecodeCodec] Codegen records export fixups for every pointer table") {
	const Ref<FoundryScript> script = compile_bytecode_test_source(
			"func run() -> Array:\n"
			"\tvar base := 1.5\n"
			"\tvar vector := Vector2(base, base + 1.0)\n"
			"\tvector.x = 3.0\n"
			"\tvar vertical := vector.y\n"
			"\tvar total := vector.x + vertical\n"
			"\tvar negated := -total\n"
			"\tvar packed := PackedFloat64Array()\n"
			"\tpacked.resize(2)\n"
			"\tpacked[0] = total\n"
			"\tvar first := packed[0]\n"
			"\tvar data := {}\n"
			"\tdata[\"value\"] = negated\n"
			"\tvar stored = data[\"value\"]\n"
			"\tvar text := \"fixup\"\n"
			"\tvar text_length := text.length()\n"
			"\tvar absolute := absf(total + first)\n"
			"\tvar count := len(text)\n"
			"\tvar reference := RefCounted.new()\n"
			"\tvar identifier := reference.get_instance_id()\n"
			"\tvar exists := FileAccess.file_exists(\"user://bytecode_fixup_probe\")\n"
			"\treturn [stored, absolute, count, text_length, identifier, exists]\n");

	const HashMap<StringName, FSFunction *> &member_functions = script->get_member_functions();
	REQUIRE(member_functions.has(SNAME("run")));
	const FSFunction *function = member_functions[SNAME("run")];
	const FSFunction::ExportFixups &fixups = function->export_fixups;

	// Every pointer table must have a symbolic descriptor for each entry, at the same index.
	CHECK(fixups.operators.size() == function->get_operator_funcs_count());
	CHECK(fixups.setters.size() == function->get_setters_count());
	CHECK(fixups.getters.size() == function->get_getters_count());
	CHECK(fixups.keyed_setters.size() == function->get_keyed_setters_count());
	CHECK(fixups.keyed_getters.size() == function->get_keyed_getters_count());
	CHECK(fixups.indexed_setters.size() == function->get_indexed_setters_count());
	CHECK(fixups.indexed_getters.size() == function->get_indexed_getters_count());
	CHECK(fixups.builtin_methods.size() == function->get_builtin_methods_count());
	CHECK(fixups.constructors.size() == function->get_constructors_count());
	CHECK(fixups.utilities.size() == function->get_utilities_count());
	CHECK(fixups.gds_utilities.size() == function->get_gds_utilities_count());
	CHECK(fixups.method_binds.size() == function->get_methods_count());

	// The script must actually exercise every table.
	CHECK(function->get_operator_funcs_count() > 0);
	CHECK(function->get_setters_count() > 0);
	CHECK(function->get_getters_count() > 0);
	CHECK(function->get_keyed_setters_count() > 0);
	CHECK(function->get_keyed_getters_count() > 0);
	CHECK(function->get_indexed_setters_count() > 0);
	CHECK(function->get_indexed_getters_count() > 0);
	CHECK(function->get_builtin_methods_count() > 0);
	CHECK(function->get_constructors_count() > 0);
	CHECK(function->get_utilities_count() > 0);
	CHECK(function->get_gds_utilities_count() > 0);
	CHECK(function->get_methods_count() > 0);

	bool has_float_addition = false;
	bool has_float_negation = false;
	for (const FSFunction::ExportFixups::OperatorKey &key : fixups.operators) {
		if (key.op == Variant::OP_ADD && key.left_type == Variant::FLOAT && key.right_type == Variant::FLOAT) {
			has_float_addition = true;
		}
		if (key.op == Variant::OP_NEGATE && key.left_type == Variant::FLOAT && key.right_type == Variant::NIL) {
			has_float_negation = true;
		}
	}
	CHECK(has_float_addition);
	CHECK(has_float_negation);

	bool has_vector_x_setter = false;
	for (const FSFunction::ExportFixups::TypedNameKey &key : fixups.setters) {
		if (key.type == Variant::VECTOR2 && key.name == SNAME("x")) {
			has_vector_x_setter = true;
		}
	}
	CHECK(has_vector_x_setter);

	bool has_vector_y_getter = false;
	for (const FSFunction::ExportFixups::TypedNameKey &key : fixups.getters) {
		if (key.type == Variant::VECTOR2 && key.name == SNAME("y")) {
			has_vector_y_getter = true;
		}
	}
	CHECK(has_vector_y_getter);

	CHECK(fixups.keyed_setters.has(Variant::DICTIONARY));
	CHECK(fixups.keyed_getters.has(Variant::DICTIONARY));
	CHECK(fixups.indexed_setters.has(Variant::PACKED_FLOAT64_ARRAY));
	CHECK(fixups.indexed_getters.has(Variant::PACKED_FLOAT64_ARRAY));

	bool has_string_length = false;
	for (const FSFunction::ExportFixups::TypedNameKey &key : fixups.builtin_methods) {
		if (key.type == Variant::STRING && key.name == SNAME("length")) {
			has_string_length = true;
		}
	}
	CHECK(has_string_length);

	bool has_vector_constructor = false;
	for (const FSFunction::ExportFixups::ConstructorKey &key : fixups.constructors) {
		if (key.type == Variant::VECTOR2) {
			CHECK(key.constructor_index >= 0);
			CHECK(key.constructor_index < Variant::get_constructor_count(Variant::VECTOR2));
			has_vector_constructor = true;
		}
	}
	CHECK(has_vector_constructor);

	CHECK(fixups.utilities.has(SNAME("absf")));
	CHECK(fixups.gds_utilities.has(SNAME("len")));

	bool has_get_instance_id = false;
	for (const FSFunction::ExportFixups::MethodBindKey &key : fixups.method_binds) {
		if (key.method_name == SNAME("get_instance_id")) {
			CHECK(key.class_name != StringName());
			has_get_instance_id = true;
		}
	}
	CHECK(has_get_instance_id);
}

TEST_CASE("[FoundryScript][BytecodeCodec] Codegen records store-global operands and named globals") {
	// A singleton autoload reference is the one construct the compiler lowers to
	// OPCODE_STORE_GLOBAL, baking the process-specific global-array index into the code.
	const String scene_path = TestUtils::get_temp_path("bytecode_fixup_autoload.tscn");
	{
		Ref<FileAccess> scene_file = FileAccess::open(scene_path, FileAccess::WRITE);
		REQUIRE(scene_file.is_valid());
		scene_file->store_string("[gd_scene format=3]\n\n[node name=\"Root\" type=\"Node\"]\n");
	}
	const StringName autoload_name = "BytecodeFixupAutoload";
	ProjectSettings::AutoloadInfo autoload;
	autoload.name = autoload_name;
	autoload.path = scene_path;
	autoload.is_singleton = true;
	ProjectSettings::get_singleton()->add_autoload(autoload);
	if (!FSLanguage::get_singleton()->has_any_global_constant(SNAME("RefCounted"))) {
		FSLanguage::get_singleton()->init();
	}
	FSLanguage::get_singleton()->add_global_constant(autoload_name, Variant());

	const Ref<FoundryScript> script = compile_bytecode_test_source(
			"func run() -> Array:\n"
			"\tvar first_reference = BytecodeFixupAutoload\n"
			"\tvar second_reference = BytecodeFixupAutoload\n"
			"\treturn [first_reference, second_reference]\n");

	ProjectSettings::get_singleton()->remove_autoload(autoload_name);
	DirAccess::remove_absolute(scene_path);

	const HashMap<StringName, FSFunction *> &member_functions = script->get_member_functions();
	REQUIRE(member_functions.has(SNAME("run")));
	const FSFunction *function = member_functions[SNAME("run")];
	const FSFunction::ExportFixups &fixups = function->export_fixups;

	REQUIRE(FSLanguage::get_singleton()->get_global_map().has(autoload_name));
	const int global_index = FSLanguage::get_singleton()->get_global_map()[autoload_name];
	const Vector<int> &code = function->get_code();
	REQUIRE(fixups.global_stores.size() == 2);
	for (const FSFunction::ExportFixups::GlobalStore &global_store : fixups.global_stores) {
		CHECK(global_store.global_name == autoload_name);
		REQUIRE(global_store.code_offset >= 2);
		REQUIRE(global_store.code_offset < code.size());
		CHECK(code[global_store.code_offset] == global_index);
		CHECK(code[global_store.code_offset - 2] == FSFunction::OPCODE_STORE_GLOBAL);
	}

	// Named globals are constant-folded on the normal compile path, so drive the generator
	// directly to prove both store-global recording and named-global staging end-to-end.
	FSByteCodeGenerator generator;
	generator.write_start(script.ptr(), "direct_store_global", false, Variant(), FSDataType());
	const uint32_t temporary_index = generator.add_temporary(FSDataType());
	const FSCodeGenerator::Address destination(FSCodeGenerator::Address::TEMPORARY, temporary_index);
	generator.write_store_global(destination, 7, "DirectGlobal");
	generator.write_store_named_global(destination, "DirectNamedGlobal");
	generator.write_store_named_global(destination, "DirectNamedGlobal");
	generator.pop_temporary();
	FSFunction *direct_function = generator.write_end();
	REQUIRE(direct_function != nullptr);

	const FSFunction::ExportFixups &direct_fixups = direct_function->export_fixups;
	const Vector<int> &direct_code = direct_function->get_code();
	REQUIRE(direct_fixups.global_stores.size() == 1);
	CHECK(direct_fixups.global_stores[0].global_name == SNAME("DirectGlobal"));
	REQUIRE(direct_fixups.global_stores[0].code_offset >= 2);
	REQUIRE(direct_fixups.global_stores[0].code_offset < direct_code.size());
	CHECK(direct_code[direct_fixups.global_stores[0].code_offset] == 7);
	CHECK(direct_code[direct_fixups.global_stores[0].code_offset - 2] == FSFunction::OPCODE_STORE_GLOBAL);
	REQUIRE(direct_fixups.named_globals.size() == 1);
	CHECK(direct_fixups.named_globals[0] == SNAME("DirectNamedGlobal"));

	memdelete(direct_function);

	// Drop the registered autoload global so later fixtures see a pristine global map.
	TestFSLanguageGlobalsAccessor::remove_global(autoload_name);
	CHECK(!FSLanguage::get_singleton()->get_global_map().has(autoload_name));
}

static Vector<uint8_t> bytecode_serialize_function_payload(FSBytecodeExporter &r_exporter, const FSFunction *p_function) {
	Ref<StreamPeerBuffer> stream;
	stream.instantiate();
	REQUIRE(r_exporter.serialize_function(stream.ptr(), p_function) == OK);
	return stream->get_data_array();
}

static FSFunction *bytecode_deserialize_function(FSBytecodeExporter &r_exporter, const Vector<uint8_t> &p_payload,
		const Ref<FoundryScript> &p_script, Vector<FSBytecodeLoader::LoadedLambdaInfo> *r_lambda_info = nullptr) {
	BytecodeTestResolver resolver;
	FSBytecodeLoader loader = bytecode_loader_for(r_exporter, &resolver);
	Ref<StreamPeerBuffer> payload_stream;
	payload_stream.instantiate();
	payload_stream->set_data_array(p_payload);
	FSFunction *function = nullptr;
	REQUIRE(loader.read_function(payload_stream.ptr(), p_script.ptr(), function, r_lambda_info) == OK);
	REQUIRE(function != nullptr);
	// The reader must consume exactly the bytes the writer produced.
	CHECK(payload_stream->get_available_bytes() == 0);
	return function;
}

// Releases a deserialized function copy that was never registered on the script. The FSFunction
// destructor's unregistration is identity-checked, so it cannot disturb the original compiled
// function that owns the same name in the script's member-function map; this asserts exactly that.
static void bytecode_destroy_restored_function(const Ref<FoundryScript> &p_script, FSFunction *p_restored) {
	const StringName function_name = p_restored->get_name();
	FSFunction *const *original_entry = p_script->get_member_functions().getptr(function_name);
	const FSFunction *original_function = original_entry != nullptr ? *original_entry : nullptr;
	memdelete(p_restored);
	FSFunction *const *surviving_entry = p_script->get_member_functions().getptr(function_name);
	CHECK((surviving_entry != nullptr ? *surviving_entry : nullptr) == original_function);
}

static void bytecode_check_function_matches(const FSFunction *p_original, const FSFunction *p_restored) {
	CHECK(p_restored->get_name() == p_original->get_name());
	CHECK(p_restored->is_static() == p_original->is_static());
	CHECK(p_restored->get_argument_count() == p_original->get_argument_count());
	CHECK(p_restored->is_vararg() == p_original->is_vararg());
	CHECK(p_restored->get_max_stack_size() == p_original->get_max_stack_size());
	CHECK(p_restored->get_instruction_args_size() == p_original->get_instruction_args_size());
	CHECK(p_restored->get_return_type() == p_original->get_return_type());
	CHECK(p_restored->get_method_info().name == p_original->get_method_info().name);
	CHECK(p_restored->get_method_info().flags == p_original->get_method_info().flags);
	CHECK(p_restored->get_method_info().arguments.size() == p_original->get_method_info().arguments.size());
	CHECK(p_restored->get_rpc_config().hash_compare(p_original->get_rpc_config()));
	CHECK(p_restored->get_code() == p_original->get_code());
	CHECK(p_restored->get_default_argument_offsets() == p_original->get_default_argument_offsets());
	CHECK(p_restored->get_constants_count() == p_original->get_constants_count());
	CHECK(p_restored->get_global_names_count() == p_original->get_global_names_count());
	CHECK(p_restored->get_operator_funcs_count() == p_original->get_operator_funcs_count());
	CHECK(p_restored->get_setters_count() == p_original->get_setters_count());
	CHECK(p_restored->get_getters_count() == p_original->get_getters_count());
	CHECK(p_restored->get_keyed_setters_count() == p_original->get_keyed_setters_count());
	CHECK(p_restored->get_keyed_getters_count() == p_original->get_keyed_getters_count());
	CHECK(p_restored->get_indexed_setters_count() == p_original->get_indexed_setters_count());
	CHECK(p_restored->get_indexed_getters_count() == p_original->get_indexed_getters_count());
	CHECK(p_restored->get_builtin_methods_count() == p_original->get_builtin_methods_count());
	CHECK(p_restored->get_constructors_count() == p_original->get_constructors_count());
	CHECK(p_restored->get_utilities_count() == p_original->get_utilities_count());
	CHECK(p_restored->get_gds_utilities_count() == p_original->get_gds_utilities_count());
	CHECK(p_restored->get_methods_count() == p_original->get_methods_count());
	REQUIRE(p_restored->get_lambdas().size() == p_original->get_lambdas().size());
	for (int i = 0; i < p_original->get_lambdas().size(); i++) {
		bytecode_check_function_matches(p_original->get_lambdas()[i], p_restored->get_lambdas()[i]);
	}
}

// Serializes the named member function, deserializes it into a fresh FSFunction attached to the
// same script (the simplest valid ownership story for a single-function round-trip; whole-script
// loading owns the full graph), verifies structural equality, and proves the restored function
// re-serializes to the exact same bytes. The caller owns the returned function and must release it
// through `bytecode_destroy_restored_function`.
static FSFunction *bytecode_round_trip_member_function(const Ref<FoundryScript> &p_script, const StringName &p_function_name,
		Vector<FSBytecodeLoader::LoadedLambdaInfo> *r_lambda_info = nullptr) {
	const HashMap<StringName, FSFunction *>::ConstIterator original_element = p_script->get_member_functions().find(p_function_name);
	REQUIRE(original_element);
	const FSFunction *original = original_element->value;

	FSBytecodeExporter exporter;
	const Vector<uint8_t> payload = bytecode_serialize_function_payload(exporter, original);

	Vector<FSBytecodeLoader::LoadedLambdaInfo> local_lambda_info;
	Vector<FSBytecodeLoader::LoadedLambdaInfo> *loaded_lambda_info = r_lambda_info != nullptr ? r_lambda_info : &local_lambda_info;
	FSFunction *restored = bytecode_deserialize_function(exporter, payload, p_script, loaded_lambda_info);
	bytecode_check_function_matches(original, restored);

	// Re-serializing the restored function must reproduce the payload byte-for-byte. The exporter
	// reads lambda metadata from the owning script, so stage the loaded entries the way the
	// whole-script loader will, and drop them again right after.
	HashMap<FSFunction *, FoundryScript::LambdaInfo> &script_lambda_info =
			const_cast<HashMap<FSFunction *, FoundryScript::LambdaInfo> &>(p_script->get_lambda_info());
	for (const FSBytecodeLoader::LoadedLambdaInfo &lambda_info : *loaded_lambda_info) {
		script_lambda_info.insert(lambda_info.function, { lambda_info.capture_count, lambda_info.use_self });
	}
	FSBytecodeExporter reserialize_exporter;
	const Vector<uint8_t> reserialized_payload = bytecode_serialize_function_payload(reserialize_exporter, restored);
	CHECK(reserialized_payload == payload);
	for (const FSBytecodeLoader::LoadedLambdaInfo &lambda_info : *loaded_lambda_info) {
		script_lambda_info.erase(lambda_info.function);
	}

	return restored;
}

static Variant bytecode_call_function(FSFunction *p_function, const Vector<Variant> &p_arguments) {
	constexpr int MAX_TEST_ARGUMENTS = 8;
	REQUIRE(p_arguments.size() <= MAX_TEST_ARGUMENTS);
	const Variant *argument_pointers[MAX_TEST_ARGUMENTS] = {};
	for (int i = 0; i < p_arguments.size(); i++) {
		argument_pointers[i] = &p_arguments[i];
	}
	Callable::CallError call_error;
	const Variant result = p_function->call(nullptr, argument_pointers, p_arguments.size(), call_error);
	CHECK(call_error.error == Callable::CallError::CALL_OK);
	return result;
}

static void bytecode_check_call_parity(const Ref<FoundryScript> &p_script, const StringName &p_function_name,
		FSFunction *p_restored, const Vector<Variant> &p_arguments) {
	const HashMap<StringName, FSFunction *>::ConstIterator original_element = p_script->get_member_functions().find(p_function_name);
	REQUIRE(original_element);
	const Variant original_result = bytecode_call_function(original_element->value, p_arguments);
	const Variant restored_result = bytecode_call_function(p_restored, p_arguments);
	CHECK(restored_result.get_type() == original_result.get_type());
	CHECK(restored_result.hash_compare(original_result));
}

TEST_CASE("[FoundryScript][BytecodeFunction] Compiled functions round-trip and execute identically") {
	const Ref<FoundryScript> script = compile_bytecode_test_source(
			"static func arithmetic(a: int, b: float) -> float:\n"
			"\tvar total := float(a) * 2.0 + b\n"
			"\tvar negated := -total\n"
			"\treturn total - negated / 4.0\n"
			"\n"
			"static func string_operations(text: String) -> Array:\n"
			"\treturn [text.length(), text.to_upper(), text.substr(1, 3)]\n"
			"\n"
			"static func construct_vector(x: float, y: float) -> Vector2:\n"
			"\tvar vector := Vector2(x, y)\n"
			"\tvector.x += 1.0\n"
			"\treturn vector\n"
			"\n"
			"static func utility_calls(value: float) -> Array:\n"
			"\treturn [absf(value), clampf(value, 0.0, 10.0), len(str(value))]\n"
			"\n"
			"static func native_method_call() -> String:\n"
			"\tvar reference := RefCounted.new()\n"
			"\treturn reference.get_class()\n"
			"\n"
			"static func lambda_sum(base: int) -> int:\n"
			"\tvar doubler := func(value: int) -> int:\n"
			"\t\tvar inner := func(amount: int) -> int:\n"
			"\t\t\treturn amount + base\n"
			"\t\treturn inner.call(value) * 2\n"
			"\treturn doubler.call(base + 1)\n"
			"\n"
			"static func typed_assignment(value: float) -> Array:\n"
			"\tvar count: int = int(value)\n"
			"\tvar numbers: Array[int] = [count, count + 1]\n"
			"\tvar mapping := {\"count\": count}\n"
			"\treturn [count, numbers, mapping[\"count\"]]\n"
			"\n"
			"static func iterate(values: Array) -> int:\n"
			"\tvar total := 0\n"
			"\tfor value in values:\n"
			"\t\ttotal += int(value)\n"
			"\tfor index in range(3):\n"
			"\t\ttotal += index\n"
			"\tvar letters := 0\n"
			"\tfor character in \"abc\":\n"
			"\t\tletters += 1\n"
			"\treturn total + letters\n"
			"\n"
			"static func with_defaults(base: int, multiplier: int = 3, suffix: String = \"end\") -> String:\n"
			"\treturn str(base * multiplier) + suffix\n");

	struct ParityFixture {
		StringName function_name;
		Vector<Vector<Variant>> argument_sets;
	};
	Vector<ParityFixture> fixtures;
	fixtures.push_back({ "arithmetic", { { 5, 2.5 } } });
	fixtures.push_back({ "string_operations", { { String("foundry") } } });
	fixtures.push_back({ "construct_vector", { { 1.5, -2.0 } } });
	fixtures.push_back({ "utility_calls", { { -3.5 } } });
	fixtures.push_back({ "native_method_call", { {} } });
	fixtures.push_back({ "typed_assignment", { { 6.9 } } });
	Array iteration_values;
	iteration_values.push_back(1);
	iteration_values.push_back(2);
	iteration_values.push_back(3);
	fixtures.push_back({ "iterate", { { iteration_values } } });
	// Default arguments: call with none, some, and all optional arguments provided.
	fixtures.push_back({ "with_defaults", { { 2 }, { 2, 5 }, { 2, 5, String("!") } } });

	for (const ParityFixture &fixture : fixtures) {
		const String fixture_name = fixture.function_name;
		CAPTURE(fixture_name);
		FSFunction *restored = bytecode_round_trip_member_function(script, fixture.function_name);
		for (const Vector<Variant> &arguments : fixture.argument_sets) {
			bytecode_check_call_parity(script, fixture.function_name, restored, arguments);
		}
		bytecode_destroy_restored_function(script, restored);
	}

	// Lambdas (including a nested one) round-trip with their capture metadata and execute.
	Vector<FSBytecodeLoader::LoadedLambdaInfo> loaded_lambda_info;
	FSFunction *restored_lambda_function = bytecode_round_trip_member_function(script, "lambda_sum", &loaded_lambda_info);
	REQUIRE(loaded_lambda_info.size() == 2);
	HashMap<FSFunction *, FSBytecodeLoader::LoadedLambdaInfo> loaded_lambda_info_by_function;
	for (const FSBytecodeLoader::LoadedLambdaInfo &lambda_info : loaded_lambda_info) {
		REQUIRE(lambda_info.function != nullptr);
		loaded_lambda_info_by_function.insert(lambda_info.function, lambda_info);
	}
	// Original and restored lambda trees are index-aligned; compare each pair's capture metadata.
	const HashMap<StringName, FSFunction *>::ConstIterator original_lambda_element = script->get_member_functions().find(SNAME("lambda_sum"));
	REQUIRE(original_lambda_element);
	REQUIRE(original_lambda_element->value->get_lambdas().size() == 1);
	const FSFunction *original_outer = original_lambda_element->value->get_lambdas()[0];
	FSFunction *restored_outer = restored_lambda_function->get_lambdas()[0];
	REQUIRE(original_outer->get_lambdas().size() == 1);
	const FSFunction *original_inner = original_outer->get_lambdas()[0];
	FSFunction *restored_inner = restored_outer->get_lambdas()[0];
	const Pair<const FSFunction *, FSFunction *> lambda_pairs[2] = { { original_outer, restored_outer }, { original_inner, restored_inner } };
	for (const Pair<const FSFunction *, FSFunction *> &lambda_pair : lambda_pairs) {
		const FoundryScript::LambdaInfo *original_info = script->get_lambda_info().getptr(const_cast<FSFunction *>(lambda_pair.first));
		REQUIRE(original_info != nullptr);
		const FSBytecodeLoader::LoadedLambdaInfo *restored_info = loaded_lambda_info_by_function.getptr(lambda_pair.second);
		REQUIRE(restored_info != nullptr);
		CHECK(restored_info->capture_count == original_info->capture_count);
		CHECK(restored_info->use_self == original_info->use_self);
	}
	bytecode_check_call_parity(script, "lambda_sum", restored_lambda_function, { 10 });
	bytecode_destroy_restored_function(script, restored_lambda_function);
}

TEST_CASE("[FoundryScript][BytecodeFunction] Tuple-typed functions round-trip and execute identically") {
	// Exercises tuple construction, element access, destructuring, and `is`/`is not` shape tests
	// through a real compile -> export -> load -> call cycle. The parameter and return types are
	// tuple-kind `FSDataType`s, so this also drives `encode_data_type`/`decode_data_type` through the
	// `FSDataType::TUPLE` branch, not just the opcode stream.
	const Ref<FoundryScript> script = compile_bytecode_test_source(
			"static func swap_pair(pair: (int, int)) -> (int, int):\n"
			"\treturn (pair.1, pair.0)\n"
			"\n"
			"static func tuple_roundtrip(a: int, b: int, label: String) -> Array:\n"
			"\tvar pair := (a, b)\n"
			"\tvar (x, y) = pair\n"
			"\tvar swapped := (y, x)\n"
			"\tvar nested := (pair, label)\n"
			"\tvar erased: Variant = pair\n"
			"\tvar matches_int_int := erased is (int, int)\n"
			"\tvar matches_string := erased is (String, int)\n"
			"\tvar not_string := erased is not (String, int)\n"
			"\treturn [pair, x, y, swapped, nested, matches_int_int, matches_string, not_string]\n");

	struct ParityFixture {
		StringName function_name;
		Vector<Vector<Variant>> argument_sets;
	};
	Vector<ParityFixture> fixtures;
	Array first_pair;
	first_pair.push_back(3);
	first_pair.push_back(4);
	fixtures.push_back({ "swap_pair", { { first_pair } } });
	fixtures.push_back({ "tuple_roundtrip", { { 1, 2, String("tail") } } });

	for (const ParityFixture &fixture : fixtures) {
		const String fixture_name = fixture.function_name;
		CAPTURE(fixture_name);
		FSFunction *restored = bytecode_round_trip_member_function(script, fixture.function_name);
		for (const Vector<Variant> &arguments : fixture.argument_sets) {
			bytecode_check_call_parity(script, fixture.function_name, restored, arguments);
		}
		bytecode_destroy_restored_function(script, restored);
	}
}

TEST_CASE("[FoundryScript][BytecodeFunction] Await-containing functions deserialize without error") {
	const Ref<FoundryScript> script = compile_bytecode_test_source(
			"static func waits(value):\n"
			"\tvar waited = await value\n"
			"\treturn waited\n");

	FSFunction *restored = bytecode_round_trip_member_function(script, "waits");
	bytecode_destroy_restored_function(script, restored);
}

TEST_CASE("[FoundryScript][BytecodeFunction] Store-global operands are rebaked by name at link time") {
	if (!FSLanguage::get_singleton()->has_any_global_constant(SNAME("RefCounted"))) {
		FSLanguage::get_singleton()->init();
	}
	const StringName global_name = "BytecodeFunctionGlobal";
	FSLanguage::get_singleton()->add_global_constant(global_name, Variant());
	const Ref<FoundryScript> script = compile_bytecode_test_source("var placeholder = 0\n");

	// Drive the generator directly so the baked operand (7) is knowably wrong for this process,
	// proving the loader rebakes it from the global name rather than trusting the serialized value.
	FSByteCodeGenerator generator;
	generator.write_start(script.ptr(), "direct_store_global", false, Variant(), FSDataType());
	const uint32_t temporary_index = generator.add_temporary(FSDataType());
	const FSCodeGenerator::Address destination(FSCodeGenerator::Address::TEMPORARY, temporary_index);
	generator.write_store_global(destination, 7, global_name);
	generator.pop_temporary();
	FSFunction *original = generator.write_end();
	REQUIRE(original != nullptr);
	REQUIRE(original->export_fixups.global_stores.size() == 1);
	const int code_offset = original->export_fixups.global_stores[0].code_offset;

	FSBytecodeExporter exporter;
	const Vector<uint8_t> payload = bytecode_serialize_function_payload(exporter, original);
	FSFunction *restored = bytecode_deserialize_function(exporter, payload, script);

	REQUIRE(FSLanguage::get_singleton()->get_global_map().has(global_name));
	const int global_index = FSLanguage::get_singleton()->get_global_map()[global_name];
	const Vector<int> &original_code = original->get_code();
	const Vector<int> &restored_code = restored->get_code();
	REQUIRE(restored_code.size() == original_code.size());
	REQUIRE(code_offset >= 0);
	REQUIRE(code_offset < restored_code.size());
	// The rewritten slot holds this process's current global-map index for the name; every other
	// slot is byte-identical to the original.
	CHECK(restored_code[code_offset] == global_index);
	CHECK(restored_code[code_offset] != 7);
	for (int i = 0; i < original_code.size(); i++) {
		if (i == code_offset) {
			continue;
		}
		CHECK(original_code[i] == restored_code[i]);
	}

	// A global name missing from the runtime map is a hard link error.
	TestFSLanguageGlobalsAccessor::remove_global(global_name);
	CHECK(!FSLanguage::get_singleton()->get_global_map().has(global_name));
	{
		BytecodeTestResolver resolver;
		FSBytecodeLoader loader = bytecode_loader_for(exporter, &resolver);
		Ref<StreamPeerBuffer> payload_stream;
		payload_stream.instantiate();
		payload_stream->set_data_array(payload);
		FSFunction *unresolved = nullptr;
		ERR_PRINT_OFF;
		CHECK(loader.read_function(payload_stream.ptr(), script.ptr(), unresolved) == ERR_CANT_RESOLVE);
		ERR_PRINT_ON;
		CHECK(unresolved == nullptr);
	}

	memdelete(restored);
	memdelete(original);
}

TEST_CASE("[FoundryScript][BytecodeFunction] Tampered fixup keys fail the load without crashing") {
	const Ref<FoundryScript> script = compile_bytecode_test_source(
			"static func measure(text: String) -> int:\n"
			"\treturn text.length()\n");
	const HashMap<StringName, FSFunction *>::ConstIterator original_element = script->get_member_functions().find(SNAME("measure"));
	REQUIRE(original_element);

	FSBytecodeExporter exporter;
	const Vector<uint8_t> payload = bytecode_serialize_function_payload(exporter, original_element->value);

	Ref<StreamPeerBuffer> table_stream;
	table_stream.instantiate();
	exporter.get_string_table().write(table_stream.ptr());
	Vector<uint8_t> table_bytes = table_stream->get_data_array();

	// Rename the recorded builtin method to a same-size name that does not exist.
	const CharString marker = String("length").utf8();
	bool patched = false;
	for (int i = 0; i + marker.length() <= table_bytes.size(); i++) {
		if (memcmp(&table_bytes[i], marker.get_data(), marker.length()) == 0) {
			table_bytes.write[i] = 'x';
			patched = true;
			break;
		}
	}
	REQUIRE(patched);

	BytecodeTestResolver resolver;
	FSBytecodeLoader loader;
	loader.set_resolver(&resolver);
	Ref<StreamPeerBuffer> tampered_table_stream;
	tampered_table_stream.instantiate();
	tampered_table_stream->set_data_array(table_bytes);
	REQUIRE(loader.read_string_table(tampered_table_stream.ptr()) == OK);

	Ref<StreamPeerBuffer> payload_stream;
	payload_stream.instantiate();
	payload_stream->set_data_array(payload);
	FSFunction *restored = nullptr;
	ERR_PRINT_OFF;
	CHECK(loader.read_function(payload_stream.ptr(), script.ptr(), restored) == ERR_CANT_RESOLVE);
	ERR_PRINT_ON;
	CHECK(restored == nullptr);
}

TEST_CASE("[FoundryScript][BytecodeFunction] Corrupted argument counts fail the load") {
	const Ref<FoundryScript> script = compile_bytecode_test_source(
			"static func no_arguments() -> int:\n"
			"\treturn 7\n");
	const HashMap<StringName, FSFunction *>::ConstIterator original_element = script->get_member_functions().find(SNAME("no_arguments"));
	REQUIRE(original_element);

	FSBytecodeExporter exporter;
	const Vector<uint8_t> payload = bytecode_serialize_function_payload(exporter, original_element->value);

	// An argument count larger than the argument-type table would make the VM index the table out
	// of bounds at call time. Field layout: u32 name index, u8 flags, i32 initial line, then the
	// i32 argument count at byte offset 9 (little-endian).
	Vector<uint8_t> corrupted = payload;
	REQUIRE(corrupted.size() > 13);
	corrupted.write[9] = 3;

	BytecodeTestResolver resolver;
	FSBytecodeLoader loader = bytecode_loader_for(exporter, &resolver);
	Ref<StreamPeerBuffer> payload_stream;
	payload_stream.instantiate();
	payload_stream->set_data_array(corrupted);
	FSFunction *restored = nullptr;
	ERR_PRINT_OFF;
	CHECK(loader.read_function(payload_stream.ptr(), script.ptr(), restored) == ERR_INVALID_DATA);
	ERR_PRINT_ON;
	CHECK(restored == nullptr);
}

TEST_CASE("[FoundryScript][BytecodeFunction] Failed loads roll back the lambda metadata output") {
	const Ref<FoundryScript> script = compile_bytecode_test_source(
			"static func two_lambdas(base: int) -> int:\n"
			"\tvar first := func(value: int) -> int:\n"
			"\t\treturn value + base\n"
			"\tvar second := func(text: String) -> int:\n"
			"\t\treturn text.length() * base\n"
			"\treturn first.call(1) + second.call(\"ab\")\n");
	const HashMap<StringName, FSFunction *>::ConstIterator original_element = script->get_member_functions().find(SNAME("two_lambdas"));
	REQUIRE(original_element);
	REQUIRE(original_element->value->get_lambdas().size() == 2);

	FSBytecodeExporter exporter;
	const Vector<uint8_t> payload = bytecode_serialize_function_payload(exporter, original_element->value);

	// Rename the builtin method only the second lambda uses, so the load fails after the first
	// lambda's metadata entry was already appended.
	Ref<StreamPeerBuffer> table_stream;
	table_stream.instantiate();
	exporter.get_string_table().write(table_stream.ptr());
	Vector<uint8_t> table_bytes = table_stream->get_data_array();
	const CharString marker = String("length").utf8();
	bool patched = false;
	for (int i = 0; i + marker.length() <= table_bytes.size(); i++) {
		if (memcmp(&table_bytes[i], marker.get_data(), marker.length()) == 0) {
			table_bytes.write[i] = 'x';
			patched = true;
			break;
		}
	}
	REQUIRE(patched);

	BytecodeTestResolver resolver;
	FSBytecodeLoader loader;
	loader.set_resolver(&resolver);
	Ref<StreamPeerBuffer> tampered_table_stream;
	tampered_table_stream.instantiate();
	tampered_table_stream->set_data_array(table_bytes);
	REQUIRE(loader.read_string_table(tampered_table_stream.ptr()) == OK);

	// Entries that predate the call must survive; entries appended by the failed call must not.
	Vector<FSBytecodeLoader::LoadedLambdaInfo> lambda_info;
	FSBytecodeLoader::LoadedLambdaInfo sentinel;
	sentinel.capture_count = 99;
	lambda_info.push_back(sentinel);

	Ref<StreamPeerBuffer> payload_stream;
	payload_stream.instantiate();
	payload_stream->set_data_array(payload);
	FSFunction *restored = nullptr;
	ERR_PRINT_OFF;
	CHECK(loader.read_function(payload_stream.ptr(), script.ptr(), restored, &lambda_info) == ERR_CANT_RESOLVE);
	ERR_PRINT_ON;
	CHECK(restored == nullptr);
	REQUIRE(lambda_info.size() == 1);
	CHECK(lambda_info[0].capture_count == 99);
	CHECK(lambda_info[0].function == nullptr);
}

TEST_CASE("[FoundryScript][BytecodeFunction] Serialized functions carry no source text or local identifiers") {
	const String source =
			"static func secret() -> int:\n"
			"\tvar distinctive_local_variable_name := 41\n"
			"\treturn distinctive_local_variable_name + 1\n";
	const Ref<FoundryScript> script = compile_bytecode_test_source(source);
	const HashMap<StringName, FSFunction *>::ConstIterator original_element = script->get_member_functions().find(SNAME("secret"));
	REQUIRE(original_element);

	FSBytecodeExporter exporter;
	const Vector<uint8_t> payload = bytecode_serialize_function_payload(exporter, original_element->value);
	Ref<StreamPeerBuffer> table_stream;
	table_stream.instantiate();
	exporter.get_string_table().write(table_stream.ptr());
	const Vector<uint8_t> table_bytes = table_stream->get_data_array();

	CHECK(!bytecode_buffer_contains(payload, "distinctive_local_variable_name"));
	CHECK(!bytecode_buffer_contains(table_bytes, "distinctive_local_variable_name"));
	CHECK(!bytecode_buffer_contains(payload, source));
	CHECK(!bytecode_buffer_contains(table_bytes, source));

	FSFunction *restored = bytecode_deserialize_function(exporter, payload, script);
	bytecode_check_call_parity(script, "secret", restored, {});
	bytecode_destroy_restored_function(script, restored);
}

// Cross-suite hygiene for the named-globals test: the autoload registration, the named global,
// the temp scene file, and the export-compile flag are global state, so they must be undone even
// when an assertion aborts the test case mid-way.
struct BytecodeEditorOnlyGlobalGuard {
	StringName name;
	String scene_path;

	~BytecodeEditorOnlyGlobalGuard() {
		FSLanguage::get_singleton()->set_compiling_for_export(false);
		if (FSLanguage::get_singleton()->get_named_globals_map().has(name)) {
			FSLanguage::get_singleton()->remove_named_global_constant(name);
		}
		if (ProjectSettings::get_singleton()->has_autoload(name)) {
			ProjectSettings::get_singleton()->remove_autoload(name);
		}
		DirAccess::remove_absolute(scene_path);
	}
};

TEST_CASE("[FoundryScript][BytecodeCodec] Editor-only named globals are collected for export validation") {
	if (!FSLanguage::get_singleton()->has_any_global_constant(SNAME("RefCounted"))) {
		FSLanguage::get_singleton()->init();
	}

	// Mirrors an editor session, where an autoload singleton is registered only as a named global
	// (not in the global array): the compiler reaches it through the TOOLS-only
	// STORE_NAMED_GLOBAL fallback, which an exported template runtime cannot resolve.
	const String scene_path = TestUtils::get_temp_path("bytecode_editor_only_autoload.tscn");
	{
		Ref<FileAccess> scene_file = FileAccess::open(scene_path, FileAccess::WRITE);
		REQUIRE(scene_file.is_valid());
		scene_file->store_string("[gd_scene format=3]\n\n[node name=\"Root\" type=\"Node\"]\n");
	}
	const StringName editor_only_name = "BytecodeEditorOnlyGlobal";
	BytecodeEditorOnlyGlobalGuard cleanup_guard{ editor_only_name, scene_path };
	ProjectSettings::AutoloadInfo autoload;
	autoload.name = editor_only_name;
	autoload.path = scene_path;
	autoload.is_singleton = true;
	ProjectSettings::get_singleton()->add_autoload(autoload);
	FSLanguage::get_singleton()->add_named_global_constant(editor_only_name, Variant());

	const Ref<FoundryScript> script = compile_bytecode_test_source(
			"func run():\n"
			"\tvar direct = BytecodeEditorOnlyGlobal\n"
			"\tvar through_lambda = func():\n"
			"\t\treturn BytecodeEditorOnlyGlobal\n"
			"\treturn [direct, through_lambda.call()]\n");

	const Vector<StringName> unsupported = FSBytecodeExporter::collect_unsupported_named_globals(script);

	// Enum functions live outside `member_functions`, but they and their lambdas are serialized just
	// like member functions. A named global referenced only from that table must therefore be caught
	// before an export template is built.
	const Ref<FoundryScript> enum_script = compile_bytecode_test_source(
			"enum Status:\n"
			"\tREADY = 1\n"
			"\n"
			"\tfunc editor_value():\n"
			"\t\tvar through_lambda = func():\n"
			"\t\t\treturn BytecodeEditorOnlyGlobal\n"
			"\t\treturn [BytecodeEditorOnlyGlobal, through_lambda.call()]\n");
	const Vector<StringName> enum_unsupported =
			FSBytecodeExporter::collect_unsupported_named_globals(enum_script);

	// Under the export-compile flag the same autoload reference compiles to STORE_GLOBAL with a
	// masked operand rebaked by name at .fsb load, so the validator has nothing left to flag.
	FSLanguage::get_singleton()->set_compiling_for_export(true);
	const Ref<FoundryScript> export_script = compile_bytecode_test_source(
			"func run():\n"
			"\tvar direct = BytecodeEditorOnlyGlobal\n"
			"\tvar through_lambda = func():\n"
			"\t\treturn BytecodeEditorOnlyGlobal\n"
			"\treturn [direct, through_lambda.call()]\n");
	FSLanguage::get_singleton()->set_compiling_for_export(false);

	const Vector<StringName> export_unsupported = FSBytecodeExporter::collect_unsupported_named_globals(export_script);
	const HashMap<StringName, FSFunction *> &export_member_functions = export_script->get_member_functions();

	REQUIRE(unsupported.size() == 1);
	CHECK(unsupported[0] == editor_only_name);
	REQUIRE(enum_unsupported.size() == 1);
	if (enum_unsupported.size() != 1) {
		return;
	}
	CHECK(enum_unsupported[0] == editor_only_name);

	CHECK(export_unsupported.is_empty());
	REQUIRE(export_member_functions.has(SNAME("run")));
	const FSFunction::ExportFixups &export_function_fixups = export_member_functions[SNAME("run")]->export_fixups;
	CHECK(export_function_fixups.named_globals.is_empty());
	bool recorded_store_global_for_autoload = false;
	for (const FSFunction::ExportFixups::GlobalStore &global_store : export_function_fixups.global_stores) {
		if (global_store.global_name == editor_only_name) {
			recorded_store_global_for_autoload = true;
		}
	}
	CHECK(recorded_store_global_for_autoload);

	// A script that never touches a named global reports nothing to validate.
	const Ref<FoundryScript> clean_script = compile_bytecode_test_source(
			"func run() -> int:\n"
			"\treturn 42\n");
	CHECK(FSBytecodeExporter::collect_unsupported_named_globals(clean_script).is_empty());
}

TEST_CASE("[FoundryScript][BytecodeCodec] Per-file export compile scope restores editor bytecode") {
	if (!FSLanguage::get_singleton()->has_any_global_constant(SNAME("RefCounted"))) {
		FSLanguage::get_singleton()->init();
	}

	const String scene_path = TestUtils::get_temp_path("bytecode_per_file_export_autoload.tscn");
	{
		Ref<FileAccess> scene_file = FileAccess::open(scene_path, FileAccess::WRITE);
		REQUIRE(scene_file.is_valid());
		scene_file->store_string("[gd_scene format=3]\n\n[node name=\"Root\" type=\"Node\"]\n");
	}
	const StringName autoload_name = "BytecodePerFileExportAutoload";
	const String script_path = TestUtils::get_temp_path("bytecode_per_file_export_autoload.fs");
	{
		Ref<FileAccess> script_file = FileAccess::open(script_path, FileAccess::WRITE);
		REQUIRE(script_file.is_valid());
		script_file->store_string(
				"func run():\n"
				"\treturn BytecodePerFileExportAutoload\n");
	}
	BytecodeEditorOnlyGlobalGuard cleanup_guard{ autoload_name, scene_path };
	ProjectSettings::AutoloadInfo autoload;
	autoload.name = autoload_name;
	autoload.path = scene_path;
	autoload.is_singleton = true;
	ProjectSettings::get_singleton()->add_autoload(autoload);
	FSLanguage::get_singleton()->add_named_global_constant(autoload_name, Variant());

	auto get_run_export_fixups = [&]() -> const FSFunction::ExportFixups & {
		Ref<FoundryScript> script = FSCache::get_cached_script(script_path);
		REQUIRE(script.is_valid());
		REQUIRE(script->get_member_functions().has(SNAME("run")));
		return script->get_member_functions()[SNAME("run")]->export_fixups;
	};

	Error error = OK;
	Ref<FoundryScript> editor_script = FSCache::get_full_script(script_path, error, String(), true);
	REQUIRE(error == OK);
	CHECK(editor_script->get_member_functions()[SNAME("run")]->export_fixups.global_stores.is_empty());

	// Mirrors EditorExportFoundryScript::CompiledBytecodeExportScope: export-only flags apply only
	// for the compile/serialize of one .fs, then every reloaded script is recompiled for the editor.
	{
		FSLanguage::get_singleton()->set_compiling_for_export(true);
		FSCache::begin_script_reload_recording();
		Ref<FoundryScript> export_script = FSCache::get_full_script(script_path, error, String(), true);
		REQUIRE(error == OK);
		bool recorded_store_global_for_autoload = false;
		for (const FSFunction::ExportFixups::GlobalStore &global_store : export_script->get_member_functions()[SNAME("run")]->export_fixups.global_stores) {
			if (global_store.global_name == autoload_name) {
				recorded_store_global_for_autoload = true;
			}
		}
		CHECK(recorded_store_global_for_autoload);

		FSLanguage::get_singleton()->set_compiling_for_export(false);
		for (const String &path : FSCache::end_script_reload_recording()) {
			FSCache::get_full_script(path, error, String(), true);
			REQUIRE(error == OK);
		}
	}

	CHECK_FALSE(FSLanguage::get_singleton()->is_compiling_for_export());
	CHECK(get_run_export_fixups().global_stores.is_empty());

	FSCache::remove_script(script_path);
	DirAccess::remove_absolute(script_path);
}

TEST_CASE("[FoundryScript][BytecodeCodec] Call-stack tracking gates OPCODE_LINE emission") {
	if (!FSLanguage::get_singleton()->has_any_global_constant(SNAME("RefCounted"))) {
		FSLanguage::get_singleton()->init();
	}
	const Ref<FoundryScript> script = compile_bytecode_test_source(
			"func run() -> int:\n"
			"\treturn 1\n");

	// The editor keeps call-stack tracking on; a release-profile compiled-bytecode export flips
	// it off around the export compile so the serialized functions carry no OPCODE_LINE
	// instructions. This pins the codegen gate the export toggle relies on.
	FSLanguage *language = FSLanguage::get_singleton();
	const bool previous_track_call_stack = language->should_track_call_stack();

	language->set_track_call_stack(true);
	FSByteCodeGenerator tracked_generator;
	tracked_generator.write_start(script.ptr(), "tracked_newline", false, Variant(), FSDataType());
	tracked_generator.write_newline(1);
	FSFunction *tracked_function = tracked_generator.write_end();

	language->set_track_call_stack(false);
	FSByteCodeGenerator untracked_generator;
	untracked_generator.write_start(script.ptr(), "untracked_newline", false, Variant(), FSDataType());
	untracked_generator.write_newline(1);
	FSFunction *untracked_function = untracked_generator.write_end();

	language->set_track_call_stack(previous_track_call_stack);

	REQUIRE(tracked_function != nullptr);
	REQUIRE(untracked_function != nullptr);
	const Vector<int> &tracked_code = tracked_function->get_code();
	const Vector<int> &untracked_code = untracked_function->get_code();
	CHECK(tracked_code.size() == untracked_code.size() + 2);
	REQUIRE(tracked_code.size() >= 2);
	CHECK(tracked_code[0] == FSFunction::OPCODE_LINE);
	CHECK(tracked_code[1] == 1);
	if (!untracked_code.is_empty()) {
		CHECK(untracked_code[0] != FSFunction::OPCODE_LINE);
	}

	memdelete(tracked_function);
	memdelete(untracked_function);
}

} // namespace FSTests

#endif // TOOLS_ENABLED
