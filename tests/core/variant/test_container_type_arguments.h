/**************************************************************************/
/*  test_container_type_arguments.h                                       */
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
#include "core/object/ref_counted.h"
#include "core/object/script_instance.h"
#include "core/object/script_language.h"
#include "core/variant/array.h"
#include "core/variant/container_type_validate.h"
#include "core/variant/dictionary.h"
#include "core/variant/variant_parser.h"

#include "tests/test_macros.h"
#include "tests/test_utils.h"

namespace TestContainerTypeArguments {

class SpecializedTestScript;

// Reports the reified arguments its class was instantiated with, which is the evidence
// `ContainerTypeValidate` compares against an expected specialization.
class SpecializedTestScriptInstance : public ScriptInstance {
	Object *owner = nullptr;
	Ref<Script> script;
	Vector<ContainerType> reified_type_arguments;

public:
	SpecializedTestScriptInstance(Object *p_owner, const Ref<Script> &p_script, const Vector<ContainerType> &p_reified_type_arguments) :
			owner(p_owner), script(p_script), reified_type_arguments(p_reified_type_arguments) {}

	bool set(const StringName &p_name, const Variant &p_value) override { return false; }
	bool get(const StringName &p_name, Variant &r_ret) const override { return false; }
	void get_property_list(List<PropertyInfo> *p_properties) const override {}
	Variant::Type get_property_type(const StringName &p_name, bool *r_is_valid = nullptr) const override {
		if (r_is_valid) {
			*r_is_valid = false;
		}
		return Variant::NIL;
	}
	void validate_property(PropertyInfo &p_property) const override {}
	bool property_can_revert(const StringName &p_name) const override { return false; }
	bool property_get_revert(const StringName &p_name, Variant &r_ret) const override { return false; }
	Object *get_owner() override { return owner; }
	void get_method_list(List<MethodInfo> *p_list) const override {}
	bool has_method(const StringName &p_method) const override { return false; }
	Variant callp(const StringName &p_method, const Variant **p_args, int p_argcount, Callable::CallError &r_error) override {
		return Variant();
	}
	void notification(int p_notification, bool p_reversed = false) override {}
	Ref<Script> get_script() const override { return script; }
	const Variant get_rpc_config() const override { return Variant(); }
	ScriptLanguage *get_language() override { return nullptr; }
	void get_reified_type_arguments(Vector<ContainerType> &r_type_arguments) const override {
		r_type_arguments = reified_type_arguments;
	}
};

// A minimal generic script class: it declares one type parameter and instances carry the argument
// they were created with.
class SpecializedTestScript : public Script {
	FOUNDRY_CLASS(SpecializedTestScript, Script);

	Vector<ContainerType> instance_type_arguments;

protected:
	static void _bind_methods() {}

public:
	void set_instance_type_arguments(const Vector<ContainerType> &p_type_arguments) { instance_type_arguments = p_type_arguments; }

	bool can_instantiate() const override { return true; }
	Ref<Script> get_base_script() const override { return Ref<Script>(); }
	StringName get_global_name() const override { return StringName(); }
	bool inherits_script(const Ref<Script> &p_script) const override { return p_script == Ref<Script>(this); }
	StringName get_instance_base_type() const override { return StringName("RefCounted"); }
	ScriptInstance *instance_create(Object *p_this) override {
		return memnew(SpecializedTestScriptInstance(p_this, Ref<Script>(this), instance_type_arguments));
	}
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

static ContainerType make_builtin(Variant::Type p_type) {
	ContainerType type;
	type.builtin_type = p_type;
	return type;
}

// `RefCounted[int]`: an engine class standing in for a specialized script class, so the transport
// surfaces can be exercised without a loadable script file.
static ContainerType make_specialized_class(const StringName &p_class_name, const Vector<ContainerType> &p_type_arguments) {
	ContainerType type;
	type.builtin_type = Variant::OBJECT;
	type.class_name = p_class_name;
	type.type_arguments = p_type_arguments;
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

TEST_CASE("[ContainerType] Descriptor round-trips type arguments at every nesting depth") {
	const ContainerType inner = make_specialized_class(SNAME("RefCounted"), { make_builtin(Variant::INT) });
	const ContainerType source = make_specialized_class(SNAME("Node"), { make_array_of(inner), make_builtin(Variant::STRING) });

	ContainerType decoded;
	String error;
	REQUIRE(ContainerTypeDescriptor::from_variant(ContainerTypeDescriptor::to_variant(source), decoded, &error));
	CHECK_EQ(decoded, source);
	REQUIRE_EQ(decoded.type_arguments.size(), 2);
	REQUIRE_EQ(decoded.type_arguments[0].element_types.size(), 1);
	CHECK_EQ(decoded.type_arguments[0].element_types[0].type_arguments.size(), 1);
}

TEST_CASE("[ContainerType] Descriptor rejects a malformed type-argument payload") {
	Dictionary descriptor;
	descriptor["type"] = Variant::OBJECT;
	descriptor["class_name"] = "RefCounted";
	descriptor["type_arguments"] = "int";

	ContainerType decoded;
	String error;
	CHECK_FALSE(ContainerTypeDescriptor::from_variant(descriptor, decoded, &error));
	CHECK(error.contains("type_arguments"));

	Array malformed_arguments;
	malformed_arguments.push_back(42);
	descriptor["type_arguments"] = malformed_arguments;
	CHECK_FALSE(ContainerTypeDescriptor::from_variant(descriptor, decoded, &error));
}

TEST_CASE("[ContainerType] Descriptor decodes an absent type-argument payload as unspecialized") {
	Dictionary descriptor;
	descriptor["type"] = Variant::OBJECT;
	descriptor["class_name"] = "RefCounted";

	ContainerType decoded;
	String error;
	REQUIRE(ContainerTypeDescriptor::from_variant(descriptor, decoded, &error));
	CHECK(decoded.type_arguments.is_empty());
	CHECK_EQ(decoded, make_specialized_class(SNAME("RefCounted"), {}));
}

TEST_CASE("[ContainerType] Variant writer and parser round-trip type arguments") {
	const ContainerType element_type = make_specialized_class(SNAME("RefCounted"), { make_builtin(Variant::INT) });

	Array array;
	array.set_typed(element_type);

	String array_str;
	VariantWriter::write_to_string(array, array_str);
	CHECK_EQ(array_str, "Array[RefCounted[int]]([])");

	VariantParser::StreamString stream;
	stream.s = array_str;
	String error_string;
	int line = 0;
	Variant parsed;
	REQUIRE_EQ(VariantParser::parse(&stream, parsed, error_string, line), OK);

	const Array parsed_array = parsed;
	CHECK_EQ(parsed_array.get_element_type(), element_type);
}

TEST_CASE("[ContainerType] Variant writer and parser round-trip nested type arguments") {
	const ContainerType leaf_type = make_specialized_class(SNAME("RefCounted"), { make_builtin(Variant::INT) });
	const ContainerType element_type = make_dictionary_of(make_builtin(Variant::STRING), leaf_type);

	Array array;
	array.set_typed(element_type);

	String array_str;
	VariantWriter::write_to_string(array, array_str);
	CHECK_EQ(array_str, "Array[Dictionary[String, RefCounted[int]]]([])");

	VariantParser::StreamString stream;
	stream.s = array_str;
	String error_string;
	int line = 0;
	Variant parsed;
	REQUIRE_EQ(VariantParser::parse(&stream, parsed, error_string, line), OK);

	const Array parsed_array = parsed;
	CHECK_EQ(parsed_array.get_element_type(), element_type);
}

TEST_CASE("[ContainerType] Variant writer and parser round-trip multiple type arguments") {
	const ContainerType element_type = make_specialized_class(SNAME("RefCounted"), { make_builtin(Variant::INT), make_builtin(Variant::STRING) });

	Array array;
	array.set_typed(element_type);

	String array_str;
	VariantWriter::write_to_string(array, array_str);
	CHECK_EQ(array_str, "Array[RefCounted[int, String]]([])");

	VariantParser::StreamString stream;
	stream.s = array_str;
	String error_string;
	int line = 0;
	Variant parsed;
	REQUIRE_EQ(VariantParser::parse(&stream, parsed, error_string, line), OK);

	const Array parsed_array = parsed;
	CHECK_EQ(parsed_array.get_element_type(), element_type);
}

TEST_CASE("[ContainerType] Variant writer and parser round-trip a specialized Resource type") {
	// `Resource` also introduces a resource-reference constructor in this text format, so a
	// specialization of the class itself must not be mistaken for one.
	const ContainerType element_type = make_specialized_class(SNAME("Resource"), { make_builtin(Variant::INT) });

	Array array;
	array.set_typed(element_type);

	String array_str;
	VariantWriter::write_to_string(array, array_str);
	CHECK_EQ(array_str, "Array[Resource[int]]([])");

	VariantParser::StreamString stream;
	stream.s = array_str;
	String error_string;
	int line = 0;
	Variant parsed;
	REQUIRE_EQ(VariantParser::parse(&stream, parsed, error_string, line), OK);

	const Array parsed_array = parsed;
	CHECK_EQ(parsed_array.get_element_type(), element_type);
}

TEST_CASE("[ContainerType] Binary Variant encoding round-trips type arguments") {
	const ContainerType element_type = make_specialized_class(SNAME("RefCounted"), { make_builtin(Variant::INT) });

	Array source;
	source.set_typed(element_type);

	int encoded_len = 0;
	REQUIRE_EQ(encode_variant(source, nullptr, encoded_len, true), OK);
	PackedByteArray buffer;
	buffer.resize(encoded_len);
	REQUIRE_EQ(encode_variant(source, buffer.ptrw(), encoded_len, true), OK);

	Variant decoded_variant;
	int decoded_len = 0;
	REQUIRE_EQ(decode_variant(decoded_variant, buffer.ptr(), buffer.size(), &decoded_len, true), OK);
	CHECK_EQ(decoded_len, buffer.size());

	const Array decoded_array = decoded_variant;
	CHECK_EQ(decoded_array.get_element_type(), element_type);
}

TEST_CASE("[ContainerType] Binary Variant encoding keeps differing key and value specializations apart") {
	const ContainerType key_type = make_specialized_class(SNAME("RefCounted"), { make_builtin(Variant::INT) });
	const ContainerType value_type = make_specialized_class(SNAME("RefCounted"), { make_builtin(Variant::STRING) });

	Dictionary source;
	source.set_typed(key_type, value_type);

	int encoded_len = 0;
	REQUIRE_EQ(encode_variant(source, nullptr, encoded_len, true), OK);
	PackedByteArray buffer;
	buffer.resize(encoded_len);
	REQUIRE_EQ(encode_variant(source, buffer.ptrw(), encoded_len, true), OK);

	Variant decoded_variant;
	int decoded_len = 0;
	REQUIRE_EQ(decode_variant(decoded_variant, buffer.ptr(), buffer.size(), &decoded_len, true), OK);
	CHECK_EQ(decoded_len, buffer.size());

	const Dictionary decoded_dictionary = decoded_variant;
	CHECK_EQ(decoded_dictionary.get_key_type(), key_type);
	CHECK_EQ(decoded_dictionary.get_value_type(), value_type);
	CHECK(decoded_dictionary.get_key_type() != decoded_dictionary.get_value_type());
}

TEST_CASE("[ContainerType] Native JSON conversion round-trips type arguments") {
	const ContainerType element_type = make_specialized_class(SNAME("RefCounted"), { make_array_of(make_builtin(Variant::INT)) });

	Array source;
	source.set_typed(element_type);

	const Variant json = JSON::from_native(source, true);
	CHECK_EQ(JSON::stringify(json), R"({"args":[],"elem_type":{"type":"RefCounted","type_args":[{"elem_type":"int","type":"Array"}]},"type":"Array"})");

	const Variant native = JSON::to_native(json, true);
	REQUIRE_EQ(native.get_type(), Variant::ARRAY);
	const Array decoded_array = native;
	CHECK_EQ(decoded_array.get_element_type(), element_type);
}

TEST_CASE("[ContainerType] Native JSON conversion leaves unspecialized types unchanged") {
	Array source;
	source.set_typed(make_builtin(Variant::INT));
	source.push_back(1);

	const Variant json = JSON::from_native(source, true);
	CHECK_EQ(JSON::stringify(json), R"({"args":["i:1"],"elem_type":"int","type":"Array"})");

	const Variant native = JSON::to_native(json, true);
	const Array decoded_array = native;
	CHECK_EQ(decoded_array.get_element_type(), make_builtin(Variant::INT));
}

TEST_CASE("[ContainerType] A script reachable only through a type argument is saved as a dependency") {
	Ref<SpecializedTestScript> script;
	script.instantiate();
	script->set_path("res://test_container_type_argument_script.tres");

	ContainerType script_argument;
	script_argument.builtin_type = Variant::OBJECT;
	script_argument.class_name = script->get_instance_base_type();
	script_argument.script = script;

	Array values;
	values.set_typed(make_specialized_class(SNAME("RefCounted"), { script_argument }));

	Ref<Resource> resource = memnew(Resource);
	resource->set_meta("values", values);

	const String save_path = TestUtils::get_temp_path("container_type_arguments.res");
	REQUIRE_EQ(ResourceSaver::save(resource, save_path), OK);

	List<String> dependencies;
	ResourceLoader::get_dependencies(save_path, &dependencies);

	bool found = false;
	for (const String &dependency : dependencies) {
		if (dependency.contains("res://test_container_type_argument_script.tres")) {
			found = true;
		}
	}
	CHECK(found);
}

TEST_CASE("[ContainerType] A round-tripped specialization still rejects a mismatched value") {
	Ref<SpecializedTestScript> script;
	script.instantiate();

	ContainerType expected_element;
	expected_element.builtin_type = Variant::OBJECT;
	expected_element.class_name = script->get_instance_base_type();
	expected_element.script = script;
	expected_element.type_arguments.push_back(make_builtin(Variant::INT));

	// A value of the same class specialized differently: `Box[String]` for an `Array[Box[int]]`.
	script->set_instance_type_arguments({ make_builtin(Variant::STRING) });
	Ref<RefCounted> mismatched;
	mismatched.instantiate();
	mismatched->set_script(script);

	Array original;
	original.set_typed(expected_element);
	ERR_PRINT_OFF;
	original.push_back(mismatched);
	ERR_PRINT_ON;
	CHECK_EQ(original.size(), 0);

	ContainerType decoded;
	String error;
	REQUIRE(ContainerTypeDescriptor::from_variant(ContainerTypeDescriptor::to_variant(expected_element), decoded, &error));

	Array round_tripped;
	round_tripped.set_typed(decoded);
	ERR_PRINT_OFF;
	round_tripped.push_back(mismatched);
	ERR_PRINT_ON;
	CHECK_EQ(round_tripped.size(), 0);

	// A matching specialization is still accepted after the round trip.
	script->set_instance_type_arguments({ make_builtin(Variant::INT) });
	Ref<RefCounted> matching;
	matching.instantiate();
	matching->set_script(script);
	round_tripped.push_back(matching);
	CHECK_EQ(round_tripped.size(), 1);
}

} // namespace TestContainerTypeArguments
