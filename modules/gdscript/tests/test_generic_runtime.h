/**************************************************************************/
/*  test_generic_runtime.h                                                */
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

#include "../gdscript.h"
#include "../gdscript_analyzer.h"
#include "../gdscript_compiler.h"
#include "../gdscript_parser.h"

#include "core/variant/container_type_validate.h"

#include "tests/test_macros.h"

// Runtime coverage for reified generic instance bindings (issue #135). The full
// `Box[int].new()` analyzer + codegen path is exercised by the integration runner fixture
// `runtime/features/generic_reified_construction.gd`; these tests pin the C++ pieces that the
// fixture cannot observe: the reified type arguments stored on a `GDScriptInstance` and the
// `ContainerType` serialization that carries them.

namespace GDScriptTests {

struct ScopedGenericRuntimeLanguage {
	ScopedGenericRuntimeLanguage() {
		if (!GDScriptLanguage::get_singleton()->has_any_global_constant(SNAME("RefCounted"))) {
			GDScriptLanguage::get_singleton()->init();
		}
	}
};

static Ref<GDScript> compile_generic_runtime_source(const String &p_source) {
	static int unique_index = 0;
	const String path = vformat("user://test_generic_runtime_%d.gd", unique_index++);

	Ref<GDScript> script;
	script.instantiate();
	script->set_path(path);
	script->set_source_code(p_source);

	GDScriptParser parser;
	Error error = parser.parse(p_source, script->get_path(), false);
	REQUIRE(error == OK);

	GDScriptAnalyzer analyzer(&parser);
	error = analyzer.analyze();
	REQUIRE(error == OK);

	GDScriptCompiler compiler;
	error = compiler.compile(&parser, script.ptr(), false);
	REQUIRE(error == OK);

	error = script->reload();
	REQUIRE(error == OK);

	return script;
}

static Ref<GDScript> get_generic_subclass(const Ref<GDScript> &p_script, const StringName &p_name) {
	const HashMap<StringName, Ref<GDScript>>::ConstIterator element = p_script->get_subclasses().find(p_name);
	return element ? element->value : Ref<GDScript>();
}

static GDScriptInstance *gdscript_instance_of(const Variant &p_value) {
	Object *object = p_value;
	if (object == nullptr) {
		return nullptr;
	}
	ScriptInstance *instance = object->get_script_instance();
	if (instance == nullptr || instance->is_synthetic()) {
		return nullptr;
	}
	return static_cast<GDScriptInstance *>(instance);
}

TEST_CASE("[Modules][GDScript][Generics] Specialized construction binds reified type arguments") {
	ScopedGenericRuntimeLanguage language;

	const char *source =
			"class Box[T]:\n"
			"\tvar value\n"
			"\tfunc _init(v = null):\n"
			"\t\tvalue = v\n";

	Ref<GDScript> script = compile_generic_runtime_source(source);
	Ref<GDScript> box = get_generic_subclass(script, "Box");
	REQUIRE(box.is_valid());

	// A scalar reified argument (`Box[int]`) lands on the constructed instance, and the
	// constructor still runs (the value passes through).
	{
		ContainerType int_type;
		int_type.builtin_type = Variant::INT;
		Vector<ContainerType> type_arguments;
		type_arguments.push_back(int_type);

		Variant argument = 5;
		const Variant *args[1] = { &argument };
		Callable::CallError error;
		Variant box_instance = box->_new_specialized(args, 1, type_arguments, error);
		REQUIRE(error.error == Callable::CallError::CALL_OK);

		GDScriptInstance *instance = gdscript_instance_of(box_instance);
		REQUIRE(instance != nullptr);

		const Vector<ContainerType> &bound = instance->get_type_arguments();
		REQUIRE(bound.size() == 1);
		CHECK(bound[0].builtin_type == Variant::INT);

		Object *box_object = box_instance;
		CHECK(box_object->get("value") == Variant(5));
	}

	// A nested container argument (`Box[Array[int]]`) preserves its element type.
	{
		ContainerType element_type;
		element_type.builtin_type = Variant::INT;
		ContainerType array_type;
		array_type.builtin_type = Variant::ARRAY;
		array_type.element_types.push_back(element_type);
		Vector<ContainerType> type_arguments;
		type_arguments.push_back(array_type);

		Callable::CallError error;
		Variant box_instance = box->_new_specialized(nullptr, 0, type_arguments, error);
		REQUIRE(error.error == Callable::CallError::CALL_OK);

		GDScriptInstance *instance = gdscript_instance_of(box_instance);
		REQUIRE(instance != nullptr);

		const Vector<ContainerType> &bound = instance->get_type_arguments();
		REQUIRE(bound.size() == 1);
		CHECK(bound[0].builtin_type == Variant::ARRAY);
		REQUIRE(bound[0].element_types.size() == 1);
		CHECK(bound[0].element_types[0].builtin_type == Variant::INT);
	}

	// Plain construction (the existing `new`) leaves the instance unspecialized.
	{
		Callable::CallError error;
		Variant box_instance = box->_new(nullptr, -1, error);
		REQUIRE(error.error == Callable::CallError::CALL_OK);

		GDScriptInstance *instance = gdscript_instance_of(box_instance);
		REQUIRE(instance != nullptr);
		CHECK(instance->get_type_arguments().is_empty());
	}
}

TEST_CASE("[Modules][GDScript][Generics] Reified member writes validate against the bound argument") {
	ScopedGenericRuntimeLanguage language;

	const char *source =
			"class Box[T]:\n"
			"\tvar value: T\n"
			"\tfunc _init(initial = null):\n"
			"\t\tvalue = initial\n";

	Ref<GDScript> script = compile_generic_runtime_source(source);
	Ref<GDScript> box = get_generic_subclass(script, "Box");
	REQUIRE(box.is_valid());

	ContainerType int_type;
	int_type.builtin_type = Variant::INT;
	Vector<ContainerType> type_arguments;
	type_arguments.push_back(int_type);

	Variant initial = 1;
	const Variant *args[1] = { &initial };
	Callable::CallError error;
	Variant box_instance = box->_new_specialized(args, 1, type_arguments, error);
	REQUIRE(error.error == Callable::CallError::CALL_OK);

	GDScriptInstance *instance = gdscript_instance_of(box_instance);
	REQUIRE(instance != nullptr);

	Variant stored;

	// A value of the reified type is accepted.
	CHECK(instance->set(SNAME("value"), Variant(42)));
	REQUIRE(instance->get(SNAME("value"), stored));
	CHECK(stored == Variant(42));

	// A convertible value is coerced to the bound type.
	CHECK(instance->set(SNAME("value"), Variant(7.0)));
	REQUIRE(instance->get(SNAME("value"), stored));
	CHECK(stored == Variant(7));

	// A value that cannot be the bound type is rejected and leaves the slot unchanged.
	ERR_PRINT_OFF;
	CHECK_FALSE(instance->set(SNAME("value"), Variant("not an int")));
	ERR_PRINT_ON;
	REQUIRE(instance->get(SNAME("value"), stored));
	CHECK(stored == Variant(7));

	// Without reified arguments the slot stays untyped and accepts anything.
	Callable::CallError plain_error;
	Variant plain_instance_value = box->_new(nullptr, -1, plain_error);
	REQUIRE(plain_error.error == Callable::CallError::CALL_OK);
	GDScriptInstance *plain_instance = gdscript_instance_of(plain_instance_value);
	REQUIRE(plain_instance != nullptr);
	REQUIRE(plain_instance->get_type_arguments().is_empty());
	CHECK(plain_instance->set(SNAME("value"), Variant("anything")));
	REQUIRE(plain_instance->get(SNAME("value"), stored));
	CHECK(stored == Variant("anything"));
}

TEST_CASE("[Modules][GDScript][Generics] ContainerType carries and serializes type arguments") {
	// A specialized script handle described as a ContainerType round-trips through the public
	// descriptor format, type arguments included.
	ContainerType inner;
	inner.builtin_type = Variant::INT;

	ContainerType specialized;
	specialized.builtin_type = Variant::OBJECT;
	specialized.class_name = SNAME("RefCounted");
	specialized.type_arguments.push_back(inner);

	const Variant descriptor = ContainerTypeDescriptor::to_variant(specialized);
	ContainerType restored;
	String error;
	REQUIRE(ContainerTypeDescriptor::from_variant(descriptor, restored, &error));
	CHECK(restored == specialized);
	REQUIRE(restored.type_arguments.size() == 1);
	CHECK(restored.type_arguments[0].builtin_type == Variant::INT);

	// The type name reflects the specialization.
	CHECK(specialized.get_type_name() == String("RefCounted[int]"));

	// Type arguments participate in equality.
	ContainerType unspecialized = specialized;
	unspecialized.type_arguments.clear();
	CHECK(unspecialized != specialized);
}

TEST_CASE("[Modules][GDScript][Generics] Reified type arguments round-trip through instance storage") {
	ScopedGenericRuntimeLanguage language;

	const char *source =
			"class Pair[K, V]:\n"
			"\tvar first: K\n"
			"\tvar second: V\n";

	Ref<GDScript> script = compile_generic_runtime_source(source);
	Ref<GDScript> pair = get_generic_subclass(script, "Pair");
	REQUIRE(pair.is_valid());

	// Stable serialized name of the hidden storage property persisted into `.tres`/scene files.
	const StringName type_arguments_property = SNAME("__gdscript_type_arguments__");

	// A nested container argument (`Array[int]`) and a script-typed argument (`Pair`) cover both
	// descriptor branches: recursive element metadata and a Script resource reference.
	ContainerType int_element;
	int_element.builtin_type = Variant::INT;
	ContainerType array_argument;
	array_argument.builtin_type = Variant::ARRAY;
	array_argument.element_types.push_back(int_element);

	ContainerType script_argument;
	script_argument.builtin_type = Variant::OBJECT;
	script_argument.class_name = pair->get_instance_base_type();
	script_argument.script = pair;

	Vector<ContainerType> type_arguments;
	type_arguments.push_back(array_argument);
	type_arguments.push_back(script_argument);

	Callable::CallError error;
	Variant source_value = pair->_new_specialized(nullptr, 0, type_arguments, error);
	REQUIRE(error.error == Callable::CallError::CALL_OK);
	GDScriptInstance *source_instance = gdscript_instance_of(source_value);
	REQUIRE(source_instance != nullptr);

	// The hidden storage property is advertised (with STORAGE usage) only for specialized instances.
	{
		List<PropertyInfo> properties;
		source_instance->get_property_list(&properties);
		bool found = false;
		for (const PropertyInfo &info : properties) {
			if (info.name == type_arguments_property) {
				found = true;
				CHECK(info.type == Variant::ARRAY);
				CHECK((info.usage & PROPERTY_USAGE_STORAGE) != 0);
			}
		}
		CHECK(found);
	}

	// Serialize, then restore onto a fresh, plain instance — the same get/set storage path that
	// duplication and `.tres`/scene save-load travel.
	Variant serialized;
	REQUIRE(source_instance->get(type_arguments_property, serialized));
	REQUIRE(serialized.get_type() == Variant::ARRAY);

	Callable::CallError plain_error;
	Variant restored_value = pair->_new(nullptr, -1, plain_error);
	REQUIRE(plain_error.error == Callable::CallError::CALL_OK);
	GDScriptInstance *restored_instance = gdscript_instance_of(restored_value);
	REQUIRE(restored_instance != nullptr);
	REQUIRE(restored_instance->get_type_arguments().is_empty());

	CHECK(restored_instance->set(type_arguments_property, serialized));

	const Vector<ContainerType> &restored = restored_instance->get_type_arguments();
	REQUIRE(restored.size() == 2);
	CHECK(restored[0] == array_argument);
	CHECK(restored[1] == script_argument);
	REQUIRE(restored[0].element_types.size() == 1);
	CHECK(restored[0].element_types[0].builtin_type == Variant::INT);
	CHECK(restored[1].script == pair);

	// A payload whose arity does not match the class's type parameters (here one argument for a
	// two-parameter `Pair`) is rejected, leaving the previously restored vector untouched.
	{
		Array malformed;
		malformed.push_back(ContainerTypeDescriptor::to_variant(array_argument));
		ERR_PRINT_OFF;
		CHECK(restored_instance->set(type_arguments_property, malformed));
		ERR_PRINT_ON;
		REQUIRE(restored_instance->get_type_arguments().size() == 2);
	}

	// An unspecialized instance carries no arguments and advertises no storage property, so
	// non-generic resources serialize exactly as before.
	Callable::CallError raw_error;
	Variant raw_value = pair->_new(nullptr, -1, raw_error);
	REQUIRE(raw_error.error == Callable::CallError::CALL_OK);
	GDScriptInstance *raw_instance = gdscript_instance_of(raw_value);
	REQUIRE(raw_instance != nullptr);
	List<PropertyInfo> raw_properties;
	raw_instance->get_property_list(&raw_properties);
	for (const PropertyInfo &info : raw_properties) {
		CHECK(info.name != type_arguments_property);
	}
}

TEST_CASE("[Modules][GDScript][Generics] GDScriptDataType lowers type arguments to ContainerType") {
	GDScriptDataType argument;
	argument.kind = GDScriptDataType::BUILTIN;
	argument.builtin_type = Variant::STRING;

	GDScriptDataType handle;
	handle.kind = GDScriptDataType::GDSCRIPT;
	handle.builtin_type = Variant::OBJECT;
	handle.type_arguments.push_back(argument);

	const ContainerType lowered = handle.to_container_type();
	REQUIRE(lowered.type_arguments.size() == 1);
	CHECK(lowered.type_arguments[0].builtin_type == Variant::STRING);
}

TEST_CASE("[Modules][GDScript][Generics] Type handle ContainerType restores type arguments") {
	ContainerType argument;
	argument.builtin_type = Variant::INT;

	ContainerType expected;
	expected.builtin_type = Variant::OBJECT;
	expected.class_name = SNAME("RefCounted");
	expected.type_arguments.push_back(argument);

	const GDScriptDataType handle = GDScriptDataType::from_type_handle_container_type(expected);

	CHECK(handle.is_type_handle);
	CHECK(handle.kind == GDScriptDataType::NATIVE);
	CHECK(handle.builtin_type == Variant::OBJECT);
	CHECK(handle.native_type == SNAME("RefCounted"));
	REQUIRE(handle.type_arguments.size() == 1);
	if (handle.type_arguments.size() != 1) {
		return;
	}
	CHECK(handle.type_arguments[0].kind == GDScriptDataType::BUILTIN);
	CHECK(handle.type_arguments[0].builtin_type == Variant::INT);
	CHECK_FALSE(handle.type_arguments[0].is_type_handle);
}

} // namespace GDScriptTests
