/**************************************************************************/
/*  test_fs_json_marshal.h                                                */
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

#include "../fs_analyzer.h"
#include "../fs_conformance_registry.h"
#include "../fs_json_marshal.h"
#include "../fs_parser.h"
#include "../fs_script_extensible_native_hooks.h"
#include "../fs_warning.h"
#include "fs_test_runner.h"
#include "test_analyzer_finalization.h"

#include "core/io/image.h"
#include "core/io/json.h"
#include "core/io/resource_loader.h"
#include "core/object/class_db.h"
#include "core/object/ref_counted.h"
#include "core/object/script_language.h"
#include "tests/core/config/test_project_settings.h"
#include "tests/test_macros.h"

// Declared in the global namespace because of the FOUNDRY_CLASS macro warning on Windows:
// "Unqualified friend declaration referring to type outside of the nearest enclosing namespace
// is a Microsoft extension; add a nested name specifier".
//
// A native class that binds `to_json` is what makes the hook exemption observable: without one,
// no script declaration of `to_json` has a native parent signature to be warned about.
class FSJsonMarshalTestHost : public RefCounted {
	FOUNDRY_CLASS(FSJsonMarshalTestHost, RefCounted);

protected:
	static void _bind_methods() {
		ClassDB::bind_method(D_METHOD("to_json"), &FSJsonMarshalTestHost::to_json);
	}

public:
	String to_json() const { return "from native"; }
};

namespace FSTests {

static const char *json_marshal_scripts_directory = "modules/foundry_script/tests/scripts";

struct JsonMarshalProjectFixture {
	TestProjectSettingsRestoreScope project_settings;

	JsonMarshalProjectFixture() {
		init_language(String(json_marshal_scripts_directory));
		REQUIRE_MESSAGE(is_fs_language_active(), "Failed to initialize the marshal test project.");
	}
};

// Installs the module's marshaller for the duration of a case and puts the previous registration
// back, so these cases do not depend on the order they run in relative to the core seam tests.
class JsonMarshallerScope {
	JSONObjectMarshaller *previous = JSON::get_object_marshaller();
	FSJsonObjectMarshaller marshaller;

public:
	JsonMarshallerScope() { JSON::set_object_marshaller(&marshaller); }
	~JsonMarshallerScope() { JSON::set_object_marshaller(previous); }
};

// Builds the `[tag, payload...]` read-only Array a tagged-union case erases to.
static Array make_json_node(int64_t p_tag) {
	Array node;
	node.push_back(p_tag);
	node.make_read_only();
	return node;
}

static Array make_json_node(int64_t p_tag, const Variant &p_payload) {
	Array node;
	node.push_back(p_tag);
	node.push_back(p_payload);
	node.make_read_only();
	return node;
}

static Ref<RefCounted> instantiate_script_host(const String &p_res_path) {
	const Ref<Script> host_script = ResourceLoader::load(p_res_path);
	REQUIRE_MESSAGE(host_script.is_valid(), vformat("Failed to load host script: %s", p_res_path));
	REQUIRE(host_script->is_valid());
	REQUIRE(host_script->can_instantiate());

	Ref<RefCounted> host(memnew(RefCounted));
	host->set_script(host_script);
	return host;
}

TEST_CASE("[Modules][FoundryScript][JsonMarshal] The to_json hook is registered as script-extensible") {
	// `to_json` is implemented in script, so declaring it must not be reported as an accidental
	// shadow of a native method.
	CHECK(FSScriptExtensibleNativeHooks::is_allowed_override(SNAME("Object"), SNAME("to_json")));

	// The exemption reaches native subclasses of the registered base, since any object may
	// conform to `JsonSerializable`.
	CHECK(FSScriptExtensibleNativeHooks::is_allowed_override(SNAME("RefCounted"), SNAME("to_json")));
	CHECK(FSScriptExtensibleNativeHooks::is_allowed_override(SNAME("Node"), SNAME("to_json")));

	// Unrelated native methods keep warning: the exemption is scoped to the hook name.
	CHECK_FALSE(FSScriptExtensibleNativeHooks::is_allowed_override(SNAME("Object"), SNAME("get")));
	CHECK_FALSE(FSScriptExtensibleNativeHooks::is_allowed_override(SNAME("RefCounted"), SNAME("reference")));
	CHECK_FALSE(FSScriptExtensibleNativeHooks::is_allowed_override(SNAME("Node"), SNAME("to_string")));

	// The hook is synchronous; an async override is not silently accepted.
	CHECK_FALSE(FSScriptExtensibleNativeHooks::allows_async_override_of_sync_hook(
			SNAME("Object"), SNAME("to_json")));
}

TEST_CASE("[Modules][FoundryScript][JsonMarshal] The hook is offered to a native base that binds it") {
	// `collect_allowed_overrides` feeds the editor's override-method list, which only offers a
	// hook the native base actually declares.
	List<StringName> hook_method_names;
	FSScriptExtensibleNativeHooks::collect_allowed_overrides(SNAME("RefCounted"), hook_method_names);
	CHECK(hook_method_names.find(SNAME("to_json")) != nullptr);
}

TEST_CASE("[Modules][FoundryScript][JsonMarshal] Calling the hook on a null object fails") {
	Variant node = "untouched";
	ERR_PRINT_OFF;
	CHECK_FALSE(FSJsonMarshal::call_to_json(nullptr, node));
	ERR_PRINT_ON;
	CHECK_EQ(String(node), "untouched");
	CHECK_FALSE(FSJsonMarshal::has_to_json(nullptr));
}

TEST_CASE("[Modules][FoundryScript][JsonMarshal] Native implementations dispatch by name") {
	// A native subclass of the extensible base still works: the helper resolves the bound
	// method through ClassDB rather than requiring a script.
	Ref<FSJsonMarshalTestHost> native_host(memnew(FSJsonMarshalTestHost));
	CHECK(FSJsonMarshal::has_to_json(native_host.ptr()));

	Variant node;
	CHECK(FSJsonMarshal::call_to_json(native_host.ptr(), node));
	CHECK_EQ(String(node), "from native");
}

TEST_CASE("[Modules][FoundryScript][JsonMarshal] An object without the hook reports failure") {
	Ref<RefCounted> bare_host(memnew(RefCounted));
	CHECK_FALSE(FSJsonMarshal::has_to_json(bare_host.ptr()));

	Variant node = "untouched";
	ERR_PRINT_OFF;
	CHECK_FALSE(FSJsonMarshal::call_to_json(bare_host.ptr(), node));
	ERR_PRINT_ON;
	CHECK_EQ(String(node), "untouched");
}

TEST_CASE("[Modules][FoundryScript][JsonMarshal] Native code dispatches to a script implementation") {
	JsonMarshalProjectFixture project;

	const Ref<RefCounted> host =
			instantiate_script_host("res://json_marshal_host/serializable_host.notest.fs");
	CHECK(FSJsonMarshal::has_to_json(host.ptr()));

	Variant node;
	REQUIRE(FSJsonMarshal::call_to_json(host.ptr(), node));

	// `JsonNode.Str("from script")` lowers to the tagged-union representation `[tag, payload]`,
	// where `Str` is case 4 in the wire contract pinned by the builtin type tests.
	REQUIRE_EQ(node.get_type(), Variant::ARRAY);
	const Array encoded = node;
	REQUIRE_EQ(encoded.size(), 2);
	CHECK_EQ(int(encoded[0]), 4);
	CHECK_EQ(String(encoded[1]), "from script");
}

TEST_CASE("[Modules][FoundryScript][JsonMarshal] A script without the hook reports failure") {
	JsonMarshalProjectFixture project;

	const Ref<RefCounted> host = instantiate_script_host("res://json_marshal_host/plain_host.notest.fs");
	CHECK_FALSE(FSJsonMarshal::has_to_json(host.ptr()));

	Variant node = "untouched";
	ERR_PRINT_OFF;
	CHECK_FALSE(FSJsonMarshal::call_to_json(host.ptr(), node));
	ERR_PRINT_ON;
	CHECK_EQ(String(node), "untouched");
}

TEST_CASE("[Modules][FoundryScript][JsonMarshal] Every JsonNode case lowers to a plain Variant") {
	Variant lowered;

	REQUIRE(FSJsonObjectMarshaller::lower_node(make_json_node(0), lowered, 0, "Fixture"));
	CHECK_EQ(lowered.get_type(), Variant::NIL);

	REQUIRE(FSJsonObjectMarshaller::lower_node(make_json_node(1, true), lowered, 0, "Fixture"));
	CHECK_EQ(lowered.get_type(), Variant::BOOL);
	CHECK(bool(lowered));

	REQUIRE(FSJsonObjectMarshaller::lower_node(make_json_node(2, 7), lowered, 0, "Fixture"));
	CHECK_EQ(lowered.get_type(), Variant::INT);
	CHECK_EQ(int64_t(lowered), 7);

	// An `int` in a `float` payload slot still lowers to a float, which is what keeps `Int` and
	// `Float` distinguishable in the encoded output.
	REQUIRE(FSJsonObjectMarshaller::lower_node(make_json_node(3, 2), lowered, 0, "Fixture"));
	CHECK_EQ(lowered.get_type(), Variant::FLOAT);
	CHECK_EQ(double(lowered), doctest::Approx(2.0));

	REQUIRE(FSJsonObjectMarshaller::lower_node(make_json_node(4, "text"), lowered, 0, "Fixture"));
	CHECK_EQ(String(lowered), "text");

	Array items;
	items.push_back(make_json_node(2, 1));
	items.push_back(make_json_node(0));
	REQUIRE(FSJsonObjectMarshaller::lower_node(make_json_node(5, items), lowered, 0, "Fixture"));
	REQUIRE_EQ(lowered.get_type(), Variant::ARRAY);
	const Array lowered_items = lowered;
	REQUIRE_EQ(lowered_items.size(), 2);
	CHECK_EQ(int64_t(lowered_items[0]), 1);
	CHECK_EQ(lowered_items[1].get_type(), Variant::NIL);

	Dictionary entries;
	entries["level"] = make_json_node(2, 3);
	REQUIRE(FSJsonObjectMarshaller::lower_node(make_json_node(6, entries), lowered, 0, "Fixture"));
	REQUIRE_EQ(lowered.get_type(), Variant::DICTIONARY);
	const Dictionary lowered_entries = lowered;
	CHECK_EQ(int64_t(lowered_entries["level"]), 3);
}

TEST_CASE("[Modules][FoundryScript][JsonMarshal] A malformed JsonNode is rejected") {
	Variant lowered;

	ERR_PRINT_OFF;
	// Not an Array at all.
	CHECK_FALSE(FSJsonObjectMarshaller::lower_node("not a node", lowered, 0, "Fixture"));
	// Empty, and a non-integer tag.
	CHECK_FALSE(FSJsonObjectMarshaller::lower_node(Array(), lowered, 0, "Fixture"));
	CHECK_FALSE(FSJsonObjectMarshaller::lower_node(make_json_node(0, 1), lowered, 0, "Fixture"));
	// A tag outside the wire contract.
	CHECK_FALSE(FSJsonObjectMarshaller::lower_node(make_json_node(7, 1), lowered, 0, "Fixture"));
	// Payload arity and type mismatches.
	CHECK_FALSE(FSJsonObjectMarshaller::lower_node(make_json_node(4), lowered, 0, "Fixture"));
	CHECK_FALSE(FSJsonObjectMarshaller::lower_node(make_json_node(4, 1), lowered, 0, "Fixture"));
	CHECK_FALSE(FSJsonObjectMarshaller::lower_node(make_json_node(5, "not an array"), lowered, 0, "Fixture"));
	CHECK_FALSE(FSJsonObjectMarshaller::lower_node(make_json_node(6, "not a dictionary"), lowered, 0, "Fixture"));
	// A malformed child fails the whole tree rather than being silently dropped.
	Array bad_items;
	bad_items.push_back("not a node");
	CHECK_FALSE(FSJsonObjectMarshaller::lower_node(make_json_node(5, bad_items), lowered, 0, "Fixture"));
	// A non-string object key fails too: coercing it could collapse two members into one.
	Dictionary bad_entries;
	bad_entries[1] = make_json_node(2, 1);
	CHECK_FALSE(FSJsonObjectMarshaller::lower_node(make_json_node(6, bad_entries), lowered, 0, "Fixture"));
	ERR_PRINT_ON;
}

TEST_CASE("[Modules][FoundryScript][JsonMarshal] Lowering is bounded by the recursion limit") {
	Variant deep_node = make_json_node(2, 1);
	for (int i = 0; i <= Variant::MAX_RECURSION_DEPTH; i++) {
		Array items;
		items.push_back(deep_node);
		deep_node = make_json_node(5, items);
	}

	Variant lowered;
	ERR_PRINT_OFF;
	CHECK_FALSE(FSJsonObjectMarshaller::lower_node(deep_node, lowered, 0, "Fixture"));
	ERR_PRINT_ON;
}

TEST_CASE("[Modules][FoundryScript][JsonMarshal] A non-conforming object is declined") {
	JsonMarshalProjectFixture project;
	JsonMarshallerScope marshaller_scope;

	const Ref<RefCounted> host = instantiate_script_host("res://json_marshal_host/plain_host.notest.fs");

	// Declining keeps the pre-existing quoted `to_string` representation.
	const String encoded = JSON::stringify(Variant(host.ptr()));
	CHECK(encoded.begins_with("\""));

	// An object with no script at all is declined too.
	Ref<RefCounted> bare_host(memnew(RefCounted));
	CHECK(JSON::stringify(Variant(bare_host.ptr())).begins_with("\""));
}

TEST_CASE("[Modules][FoundryScript][JsonMarshal] A conforming object is marshaled through to_json") {
	JsonMarshalProjectFixture project;
	JsonMarshallerScope marshaller_scope;

	const Ref<RefCounted> host =
			instantiate_script_host("res://json_marshal_host/serializable_host.notest.fs");

	CHECK_EQ(JSON::stringify(Variant(host.ptr())), "\"from script\"");

	// Nesting works at any depth because core recurses back into `_stringify` with the lowered tree.
	Array container;
	container.push_back(host.ptr());
	CHECK_EQ(JSON::stringify(container), "[\"from script\"]");

	Dictionary keyed;
	keyed["hero"] = host.ptr();
	CHECK_EQ(JSON::stringify(keyed), "{\"hero\":\"from script\"}");
}

TEST_CASE("[Modules][FoundryScript][JsonMarshal] A retroactive conformance on a native class is honored") {
	JsonMarshalProjectFixture project;
	JsonMarshallerScope marshaller_scope;

	// Loading the declaring file is what installs the conformance; the Ref is held for the whole case
	// so the compiled witnesses stay registered, and dropping it unregisters them again.
	const Ref<Script> conformance_script =
			ResourceLoader::load("res://json_marshal_host/native_image_ext.notest.fs");
	REQUIRE(conformance_script.is_valid());
	REQUIRE(conformance_script->is_valid());

	// The conformance target has no script, so nothing installs the hook on the instance.
	Ref<Image> image(memnew(Image));
	CHECK(FSJsonObjectMarshaller::conforms_to_serializable(image.ptr()));
	CHECK(FSJsonMarshal::has_to_json(image.ptr()));

	Variant node;
	REQUIRE(FSJsonMarshal::call_to_json(image.ptr(), node));

	// `JsonNode.Str(...)` lowers to `[tag, payload]`, where `Str` is case 4 in the wire contract.
	REQUIRE_EQ(node.get_type(), Variant::ARRAY);
	const Array encoded_node = node;
	REQUIRE_EQ(encoded_node.size(), 2);
	CHECK_EQ(int(encoded_node[0]), 4);
	CHECK_EQ(String(encoded_node[1]), "image:Image");

	CHECK_EQ(JSON::stringify(Variant(image.ptr())), "\"image:Image\"");

	Array container;
	container.push_back(image.ptr());
	CHECK_EQ(JSON::stringify(container), "[\"image:Image\"]");

	// The inheritance walk only goes up: a base class of the conformance target is not conformed by
	// it, and neither is a native class outside the hierarchy. Both keep the quoted `to_string`.
	Ref<Resource> base_instance(memnew(Resource));
	CHECK_FALSE(FSJsonObjectMarshaller::conforms_to_serializable(base_instance.ptr()));
	CHECK(JSON::stringify(Variant(base_instance.ptr())).begins_with("\""));

	Ref<RefCounted> unconformed(memnew(RefCounted));
	CHECK_FALSE(FSJsonObjectMarshaller::conforms_to_serializable(unconformed.ptr()));
	CHECK_FALSE(FSJsonMarshal::has_to_json(unconformed.ptr()));
	CHECK(JSON::stringify(Variant(unconformed.ptr())).begins_with("\""));
}

TEST_CASE("[Modules][FoundryScript][JsonMarshal] A witness of another trait does not opt a native class in") {
	JsonMarshalProjectFixture project;
	JsonMarshallerScope marshaller_scope;

	// Re-register a real compiled witness under an unrelated trait name, which is what a conformance
	// to some other trait that happens to require a method named `to_json` leaves in the registry.
	// The script Ref is held for the whole case, so the borrowed function stays owned and alive, and
	// its `JsonSerializable` registration is dropped so only the unrelated trait remains loaded.
	const Ref<Script> witness_owner =
			ResourceLoader::load("res://json_marshal_host/native_image_ext.notest.fs");
	REQUIRE(witness_owner.is_valid());
	REQUIRE(witness_owner->is_valid());

	const String owner_file = witness_owner->get_path();
	Vector<FSConformanceRegistry::RuntimeConformance> compiled =
			FSConformanceRegistry::get_singleton()->get_runtime_witnesses(owner_file);
	REQUIRE_FALSE(compiled.is_empty());
	FSConformanceRegistry::get_singleton()->clear_runtime_witnesses(owner_file);
	FSConformanceRegistry::get_singleton()->clear_file(owner_file);

	const String probe_file = "res://json_marshal_host/probe_only.notest.fs";
	for (FSConformanceRegistry::RuntimeConformance &conformance : compiled) {
		conformance.trait_name = SNAME("JsonMarshalProbe");
	}
	FSConformanceRegistry::get_singleton()->register_runtime_witnesses(probe_file, compiled);

	// ...combined with a `JsonSerializable` declaration on a base class that was analyzed but never
	// loaded. Neither half is a conformance that can be marshaled, and pairing the trait from one with
	// the witness from the other would invoke a method that never agreed to produce a `JsonNode`.
	const String analyzed_file = "res://json_marshal_host/analyzed_only.notest.fs";
	FSConformanceRegistry::Conformance analyzed;
	analyzed.target_keys.push_back("Resource");
	analyzed.target_fqcn = "Resource";
	analyzed.trait_name = FSJsonObjectMarshaller::serializable_trait_name();
	analyzed.source_file = analyzed_file;

	Vector<FSConformanceRegistry::Conformance> analyzed_conformances;
	analyzed_conformances.push_back(analyzed);
	FSConformanceRegistry::get_singleton()->register_file_conformances(analyzed_file, analyzed_conformances);

	Ref<Image> image(memnew(Image));
	REQUIRE(FSConformanceRegistry::get_singleton()->native_class_conforms(
			SNAME("Image"), FSJsonObjectMarshaller::serializable_trait_name(), true));
	REQUIRE(FSConformanceRegistry::get_singleton()->find_native_witness_function(
					SNAME("Image"), FSJsonMarshal::to_json_method_name()) != nullptr);
	CHECK(FSConformanceRegistry::get_singleton()->find_native_trait_witness_function(
				  SNAME("Image"), FSJsonObjectMarshaller::serializable_trait_name(),
				  FSJsonMarshal::to_json_method_name()) == nullptr);

	CHECK_FALSE(FSJsonObjectMarshaller::conforms_to_serializable(image.ptr()));
	CHECK(JSON::stringify(Variant(image.ptr())).begins_with("\""));

	FSConformanceRegistry::get_singleton()->clear_file(analyzed_file);
	FSConformanceRegistry::get_singleton()->clear_runtime_witnesses(probe_file);
}

TEST_CASE("[Modules][FoundryScript][JsonMarshal] An analyzed but unloaded native conformance is declined") {
	JsonMarshalProjectFixture project;
	JsonMarshallerScope marshaller_scope;

	// Analysis alone registers a conformance: the editor and the language server analyze files that
	// nothing loaded, which installs the declaration without ever compiling a witness. Claiming an
	// object on that basis would replace its quoted `to_string()` with `null`, so the declaration is
	// not enough on its own.
	const String declaring_file = "res://json_marshal_host/analyzed_only.notest.fs";
	FSConformanceRegistry::Conformance analyzed;
	analyzed.target_keys.push_back("Resource");
	analyzed.target_fqcn = "Resource";
	analyzed.trait_name = FSJsonObjectMarshaller::serializable_trait_name();
	analyzed.source_file = declaring_file;

	Vector<FSConformanceRegistry::Conformance> analyzed_conformances;
	analyzed_conformances.push_back(analyzed);
	FSConformanceRegistry::get_singleton()->register_file_conformances(declaring_file, analyzed_conformances);

	Ref<Resource> resource(memnew(Resource));
	REQUIRE(FSConformanceRegistry::get_singleton()->native_class_conforms(
			SNAME("Resource"), FSJsonObjectMarshaller::serializable_trait_name(), true));
	CHECK_FALSE(FSJsonObjectMarshaller::conforms_to_serializable(resource.ptr()));
	CHECK(JSON::stringify(Variant(resource.ptr())).begins_with("\""));

	FSConformanceRegistry::get_singleton()->clear_file(declaring_file);
}

TEST_CASE("[Modules][FoundryScript][JsonMarshal] A conforming object returning a bad node encodes as null") {
	JsonMarshalProjectFixture project;
	JsonMarshallerScope marshaller_scope;

	const Ref<RefCounted> host = instantiate_script_host("res://json_marshal_host/bad_node_host.notest.fs");

	// Declining here would hide a broken `to_json()` behind the quoted `to_string` fallback, so the
	// object is still claimed and written as `null` after the error is reported.
	ERR_PRINT_OFF;
	const String encoded = JSON::stringify(Variant(host.ptr()));
	ERR_PRINT_ON;
	CHECK_EQ(encoded, "null");
}

TEST_CASE("[Modules][FoundryScript][JsonMarshal] Only values JSON has a node for can be lifted") {
	Variant prepared;
	CHECK(FSJsonObjectMarshaller::prepare_parsed_value(Variant(), 0, prepared));
	CHECK(FSJsonObjectMarshaller::prepare_parsed_value(true, 0, prepared));
	CHECK(FSJsonObjectMarshaller::prepare_parsed_value(int64_t(3), 0, prepared));
	CHECK(FSJsonObjectMarshaller::prepare_parsed_value(1.5, 0, prepared));
	CHECK(FSJsonObjectMarshaller::prepare_parsed_value(String("text"), 0, prepared));

	Array items;
	items.push_back(1);
	items.push_back("two");
	CHECK(FSJsonObjectMarshaller::prepare_parsed_value(items, 0, prepared));

	Dictionary entries;
	entries["level"] = 3;
	CHECK(FSJsonObjectMarshaller::prepare_parsed_value(entries, 0, prepared));

	// A value the parser never produces has no JsonNode case.
	CHECK_FALSE(FSJsonObjectMarshaller::prepare_parsed_value(Vector2(1, 2), 0, prepared));
	Array nested_unsupported;
	nested_unsupported.push_back(Vector2(1, 2));
	CHECK_FALSE(FSJsonObjectMarshaller::prepare_parsed_value(nested_unsupported, 0, prepared));

	// A non-string key would collapse two members into one when coerced.
	Dictionary numeric_keys;
	numeric_keys[1] = 3;
	CHECK_FALSE(FSJsonObjectMarshaller::prepare_parsed_value(numeric_keys, 0, prepared));

	// Lifting is bounded the same way lowering is.
	Variant deep_value = Variant(1);
	for (int i = 0; i <= Variant::MAX_RECURSION_DEPTH; i++) {
		Array wrapper;
		wrapper.push_back(deep_value);
		deep_value = wrapper;
	}
	CHECK_FALSE(FSJsonObjectMarshaller::prepare_parsed_value(deep_value, 0, prepared));
}

TEST_CASE("[Modules][FoundryScript][JsonMarshal] A parsed whole number lifts as an Int") {
	// The parser reports every JSON number as a float, so without this the `Int` case could never
	// occur in a parsed document and an `int` field could not be decoded by matching it.
	Variant prepared;
	REQUIRE(FSJsonObjectMarshaller::prepare_parsed_value(3.0, 0, prepared));
	CHECK_EQ(prepared.get_type(), Variant::INT);
	CHECK_EQ(int64_t(prepared), 3);

	REQUIRE(FSJsonObjectMarshaller::prepare_parsed_value(-7.0, 0, prepared));
	CHECK_EQ(prepared.get_type(), Variant::INT);
	CHECK_EQ(int64_t(prepared), -7);

	// A fractional value stays a float.
	REQUIRE(FSJsonObjectMarshaller::prepare_parsed_value(1.5, 0, prepared));
	CHECK_EQ(prepared.get_type(), Variant::FLOAT);

	// So does a whole value no `int64_t` can hold exactly, which truncating would corrupt.
	REQUIRE(FSJsonObjectMarshaller::prepare_parsed_value(1.0e30, 0, prepared));
	CHECK_EQ(prepared.get_type(), Variant::FLOAT);

	// A literal beyond a double's exact range is already rounded by the parser, so the node carries
	// the rounded value rather than the digits in the text. Nothing here can recover them.
	const Variant parsed_beyond_exact_range = JSON::parse_string("9007199254740993");
	REQUIRE_EQ(parsed_beyond_exact_range.get_type(), Variant::FLOAT);
	REQUIRE(FSJsonObjectMarshaller::prepare_parsed_value(parsed_beyond_exact_range, 0, prepared));
	CHECK_EQ(prepared.get_type(), Variant::INT);
	CHECK_EQ(int64_t(prepared), 9007199254740992);

	// The rewrite reaches into containers.
	Array items;
	items.push_back(2.0);
	REQUIRE(FSJsonObjectMarshaller::prepare_parsed_value(items, 0, prepared));
	const Array prepared_items = prepared;
	CHECK_EQ(prepared_items[0].get_type(), Variant::INT);

	Dictionary entries;
	entries["level"] = 4.0;
	REQUIRE(FSJsonObjectMarshaller::prepare_parsed_value(entries, 0, prepared));
	const Dictionary prepared_entries = prepared;
	CHECK_EQ(prepared_entries["level"].get_type(), Variant::INT);
}

// `JsonResult` and `JsonDecodeError` are declared in script, so their fields are read the same way
// a script would read them.
static bool result_is_ok(const Variant &p_result) {
	Object *result_object = p_result.get_validated_object();
	REQUIRE(result_object != nullptr);
	Callable::CallError call_error;
	const Variant is_ok = result_object->callp(SNAME("is_ok"), nullptr, 0, call_error);
	REQUIRE(call_error.error == Callable::CallError::CALL_OK);
	return is_ok;
}

static Variant result_error_field(const Variant &p_result, const StringName &p_field) {
	Object *result_object = p_result.get_validated_object();
	REQUIRE(result_object != nullptr);
	const Variant decode_error = result_object->get(SNAME("error"));
	Object *error_object = decode_error.get_validated_object();
	REQUIRE(error_object != nullptr);
	return error_object->get(p_field);
}

TEST_CASE("[Modules][FoundryScript][JsonMarshal] A plain Variant tree lifts to the JsonNode wire contract") {
	JsonMarshalProjectFixture project;

	Dictionary parsed;
	parsed["level"] = 3;
	parsed["name"] = "Captain";

	Variant node;
	REQUIRE(FSJsonMarshal::make_json_node(parsed, node));

	// `Object` is case 6, and its payload is a Dictionary of nodes.
	REQUIRE_EQ(node.get_type(), Variant::ARRAY);
	const Array object_node = node;
	REQUIRE_EQ(object_node.size(), 2);
	CHECK_EQ(int(object_node[0]), 6);

	REQUIRE_EQ(object_node[1].get_type(), Variant::DICTIONARY);
	const Dictionary entries = object_node[1];

	const Array level_node = entries["level"];
	REQUIRE_EQ(level_node.size(), 2);
	CHECK_EQ(int(level_node[0]), 2);
	CHECK_EQ(int64_t(level_node[1]), 3);

	const Array name_node = entries["name"];
	REQUIRE_EQ(name_node.size(), 2);
	CHECK_EQ(int(name_node[0]), 4);
	CHECK_EQ(String(name_node[1]), "Captain");
}

TEST_CASE("[Modules][FoundryScript][JsonMarshal] An unrepresentable value is not lifted") {
	JsonMarshalProjectFixture project;

	Variant node = "untouched";
	CHECK_FALSE(FSJsonMarshal::make_json_node(Vector2(1, 2), node));
	CHECK_EQ(String(node), "untouched");
}

TEST_CASE("[Modules][FoundryScript][JsonMarshal] Parse results are built through the builtin script") {
	JsonMarshalProjectFixture project;

	Variant success;
	REQUIRE(FSJsonMarshal::make_result_ok(int64_t(7), success));
	CHECK(result_is_ok(success));

	Variant failure;
	REQUIRE(FSJsonMarshal::make_result_failure("expected an int", "$.level", failure));
	CHECK_FALSE(result_is_ok(failure));
	CHECK_EQ(String(result_error_field(failure, SNAME("message"))), "expected an int");
	CHECK_EQ(String(result_error_field(failure, SNAME("path"))), "$.level");
}

TEST_CASE("[Modules][FoundryScript][JsonMarshal] parse_to_node returns a JsonResult holding a JsonNode") {
	JsonMarshalProjectFixture project;
	JsonMarshallerScope marshaller_scope;

	const Variant parsed = JSON::parse_to_node("{\"level\":3,\"ratio\":0.5}");
	REQUIRE(result_is_ok(parsed));

	Object *result_object = parsed.get_validated_object();
	REQUIRE(result_object != nullptr);
	const Variant node = result_object->get(SNAME("value"));
	REQUIRE_EQ(node.get_type(), Variant::ARRAY);
	const Array object_node = node;
	CHECK_EQ(int(object_node[0]), 6);

	const Dictionary entries = object_node[1];
	const Array level_node = entries["level"];
	CHECK_EQ(int(level_node[0]), 2);
	const Array ratio_node = entries["ratio"];
	CHECK_EQ(int(ratio_node[0]), 3);
}

TEST_CASE("[Modules][FoundryScript][JsonMarshal] A syntax error becomes a failed JsonResult") {
	JsonMarshalProjectFixture project;
	JsonMarshallerScope marshaller_scope;

	const Variant parsed = JSON::parse_to_node("{not json");
	CHECK_FALSE(result_is_ok(parsed));

	// The parser's own message survives, and its line is folded into the message because
	// `JsonDecodeError` locates failures by document path rather than by line.
	const String message = result_error_field(parsed, SNAME("message"));
	CHECK_FALSE(message.is_empty());
	CHECK(message.contains("line"));
	CHECK_EQ(String(result_error_field(parsed, SNAME("path"))), "$");
}

TEST_CASE("[Modules][FoundryScript][JsonMarshal] An encoded value parses back to the same tree") {
	JsonMarshalProjectFixture project;
	JsonMarshallerScope marshaller_scope;

	const Ref<RefCounted> host =
			instantiate_script_host("res://json_marshal_host/serializable_host.notest.fs");

	const Variant parsed = JSON::parse_to_node(JSON::stringify(Variant(host.ptr())));
	REQUIRE(result_is_ok(parsed));

	Object *result_object = parsed.get_validated_object();
	REQUIRE(result_object != nullptr);
	Variant round_tripped;
	REQUIRE(FSJsonObjectMarshaller::lower_node(result_object->get(SNAME("value")), round_tripped, 0, "Fixture"));
	CHECK_EQ(String(round_tripped), "from script");
}

#if defined(DEBUG_ENABLED) && defined(TOOLS_ENABLED)
static void analyze_against_native_host(const char *p_source, const String &p_path, FSParser &r_parser) {
	// Registering the native base is what gives a script declaration of `to_json` a parent
	// signature to be compared against, and the analyzer only resolves `extends` against an
	// exposed class, so force full registration before analysis.
	FOUNDRY_REGISTER_CLASS(FSJsonMarshalTestHost);
	REQUIRE(ClassDB::is_class_exposed(SNAME("FSJsonMarshalTestHost")));

	REQUIRE_EQ(r_parser.parse(p_source, p_path, false), OK);
	FSAnalyzer analyzer(&r_parser);
	const Error analyze_result = analyzer.analyze();
	String reported_errors;
	for (const FSParser::ParserError &parse_error : r_parser.get_errors()) {
		reported_errors += "\n" + parse_error.message;
	}
	REQUIRE_MESSAGE(analyze_result == OK, reported_errors);
}

TEST_CASE("[Modules][FoundryScript][JsonMarshal] Implementing the hook is not a native-override warning") {
	AnalyzerWarningSettingsScope warning_settings;

	const char *source = R"(
extends FSJsonMarshalTestHost

func to_json() -> String:
	return "from script"

func test() -> void:
	pass
)";

	FSParser parser;
	analyze_against_native_host(source, "user://json_marshal_hook_override.fs", parser);
	CHECK_EQ(count_warnings_with_code(parser, FSWarning::NATIVE_METHOD_OVERRIDE), 0);
}

TEST_CASE("[Modules][FoundryScript][JsonMarshal] An unrelated native override still warns") {
	AnalyzerWarningSettingsScope warning_settings;

	const char *source = R"(
extends FSJsonMarshalTestHost

func get(_property: StringName) -> Variant:
	return null

func test() -> void:
	pass
)";

	FSParser parser;
	analyze_against_native_host(source, "user://json_marshal_unrelated_override.fs", parser);
	CHECK_EQ(count_warnings_with_code(parser, FSWarning::NATIVE_METHOD_OVERRIDE), 1);
}
#endif // DEBUG_ENABLED && TOOLS_ENABLED

} // namespace FSTests
