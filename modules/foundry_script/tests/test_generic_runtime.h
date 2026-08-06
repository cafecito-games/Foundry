/**************************************************************************/
/*  test_generic_runtime.h                                                */
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

#include "../foundry_script.h"
#include "../fs_analyzer.h"
#include "../fs_compiler.h"
#include "../fs_conformance_registry.h"
#include "../fs_parser.h"

#include "core/variant/container_type_validate.h"

#include "tests/test_macros.h"

// Shared bytecode round-trip fixtures (in-process compile through the cache, test resolver). The
// header is tools-only and self-guarded; every module test header is compiled into the one generated
// test translation unit, so this include duplicates no test registrations.
#include "test_bytecode_script.h"

// Runtime coverage for reified generic instance bindings (issue #135). The full
// `Box[int].new()` analyzer + codegen path is exercised by the integration runner fixture
// `runtime/features/generic_reified_construction.fs`; these tests pin the C++ pieces that the
// fixture cannot observe: the reified type arguments stored on a `FSInstance` and the
// `ContainerType` serialization that carries them.

namespace FSTests {

struct ScopedGenericRuntimeLanguage {
	ScopedGenericRuntimeLanguage() {
		if (!FSLanguage::get_singleton()->has_any_global_constant(SNAME("RefCounted"))) {
			FSLanguage::get_singleton()->init();
		}
	}
};

static Ref<FoundryScript> compile_generic_runtime_source(const String &p_source) {
	static int unique_index = 0;
	const String path = vformat("user://test_generic_runtime_%d.fs", unique_index++);

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

	error = script->reload();
	REQUIRE(error == OK);

	return script;
}

static Ref<FoundryScript> get_generic_subclass(const Ref<FoundryScript> &p_script, const StringName &p_name) {
	const HashMap<StringName, Ref<FoundryScript>>::ConstIterator element = p_script->get_subclasses().find(p_name);
	return element ? element->value : Ref<FoundryScript>();
}

static FSInstance *fs_instance_of(const Variant &p_value) {
	Object *object = p_value;
	if (object == nullptr) {
		return nullptr;
	}
	ScriptInstance *instance = object->get_script_instance();
	if (instance == nullptr || instance->is_synthetic()) {
		return nullptr;
	}
	return static_cast<FSInstance *>(instance);
}

TEST_CASE("[Modules][FoundryScript][Generics] Specialized construction binds reified type arguments") {
	ScopedGenericRuntimeLanguage language;

	const char *source =
			"class Box[T]:\n"
			"\tvar value\n"
			"\tfunc _init(v = null):\n"
			"\t\tvalue = v\n";

	Ref<FoundryScript> script = compile_generic_runtime_source(source);
	Ref<FoundryScript> box = get_generic_subclass(script, "Box");
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

		FSInstance *instance = fs_instance_of(box_instance);
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

		FSInstance *instance = fs_instance_of(box_instance);
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

		FSInstance *instance = fs_instance_of(box_instance);
		REQUIRE(instance != nullptr);
		CHECK(instance->get_type_arguments().is_empty());
	}
}

TEST_CASE("[Modules][FoundryScript][Generics] Reified member writes validate against the bound argument") {
	ScopedGenericRuntimeLanguage language;

	const char *source =
			"class Box[T]:\n"
			"\tvar value: T\n"
			"\tfunc _init(initial = null):\n"
			"\t\tvalue = initial\n";

	Ref<FoundryScript> script = compile_generic_runtime_source(source);
	Ref<FoundryScript> box = get_generic_subclass(script, "Box");
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

	FSInstance *instance = fs_instance_of(box_instance);
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
	FSInstance *plain_instance = fs_instance_of(plain_instance_value);
	REQUIRE(plain_instance != nullptr);
	REQUIRE(plain_instance->get_type_arguments().is_empty());
	CHECK(plain_instance->set(SNAME("value"), Variant("anything")));
	REQUIRE(plain_instance->get(SNAME("value"), stored));
	CHECK(stored == Variant("anything"));
}

TEST_CASE("[Modules][FoundryScript][Generics] ContainerType carries and serializes type arguments") {
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

TEST_CASE("[Modules][FoundryScript][Generics] Reified type arguments round-trip through instance storage") {
	ScopedGenericRuntimeLanguage language;

	const char *source =
			"class Pair[K, V]:\n"
			"\tvar first: K\n"
			"\tvar second: V\n";

	Ref<FoundryScript> script = compile_generic_runtime_source(source);
	Ref<FoundryScript> pair = get_generic_subclass(script, "Pair");
	REQUIRE(pair.is_valid());

	// Stable serialized name of the hidden storage property persisted into `.tres`/scene files.
	const StringName type_arguments_property = SNAME("__foundry_script_type_arguments__");

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
	FSInstance *source_instance = fs_instance_of(source_value);
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
	FSInstance *restored_instance = fs_instance_of(restored_value);
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
	FSInstance *raw_instance = fs_instance_of(raw_value);
	REQUIRE(raw_instance != nullptr);
	List<PropertyInfo> raw_properties;
	raw_instance->get_property_list(&raw_properties);
	for (const PropertyInfo &info : raw_properties) {
		CHECK(info.name != type_arguments_property);
	}
}

TEST_CASE("[Modules][FoundryScript][Generics] FSDataType lowers type arguments to ContainerType") {
	FSDataType argument;
	argument.kind = FSDataType::BUILTIN;
	argument.builtin_type = Variant::STRING;

	FSDataType handle;
	handle.kind = FSDataType::FOUNDRY_SCRIPT;
	handle.builtin_type = Variant::OBJECT;
	handle.type_arguments.push_back(argument);

	const ContainerType lowered = handle.to_container_type();
	REQUIRE(lowered.type_arguments.size() == 1);
	CHECK(lowered.type_arguments[0].builtin_type == Variant::STRING);
}

TEST_CASE("[Modules][FoundryScript][Generics] Type handle ContainerType restores type arguments") {
	ContainerType argument;
	argument.builtin_type = Variant::INT;

	ContainerType expected;
	expected.builtin_type = Variant::OBJECT;
	expected.class_name = SNAME("RefCounted");
	expected.type_arguments.push_back(argument);

	const FSDataType handle = FSDataType::from_type_handle_container_type(expected);

	CHECK(handle.is_type_handle);
	CHECK(handle.kind == FSDataType::NATIVE);
	CHECK(handle.builtin_type == Variant::OBJECT);
	CHECK(handle.native_type == SNAME("RefCounted"));
	REQUIRE(handle.type_arguments.size() == 1);
	if (handle.type_arguments.size() != 1) {
		return;
	}
	CHECK(handle.type_arguments[0].kind == FSDataType::BUILTIN);
	CHECK(handle.type_arguments[0].builtin_type == Variant::INT);
	CHECK_FALSE(handle.type_arguments[0].is_type_handle);
}

#ifdef TOOLS_ENABLED

static Variant call_generic_runtime_static(const Ref<FoundryScript> &p_script, const StringName &p_method,
		const Vector<Variant> &p_arguments) {
	LocalVector<const Variant *> argument_pointers;
	argument_pointers.resize(p_arguments.size());
	for (int i = 0; i < p_arguments.size(); i++) {
		argument_pointers[i] = &p_arguments[i];
	}
	Callable::CallError call_error;
	Object *script_object = p_script.ptr();
	const Variant result = script_object->callp(p_method, argument_pointers.ptr(), p_arguments.size(), call_error);
	CHECK(call_error.error == Callable::CallError::CALL_OK);
	return result;
}

static Variant new_generic_runtime_instance(const Ref<FoundryScript> &p_script,
		const Vector<ContainerType> &p_type_arguments) {
	Callable::CallError call_error;
	const Variant instance = p_type_arguments.is_empty()
			? p_script->_new(nullptr, 0, call_error)
			: p_script->_new_specialized(nullptr, 0, p_type_arguments, call_error);
	REQUIRE(call_error.error == Callable::CallError::CALL_OK);
	REQUIRE(instance.get_type() == Variant::OBJECT);
	return instance;
}

// Exercises the whole specialized predicate against real runtime values. Runs once against the
// compiled script and once against the same script rebuilt from bytecode, so source execution and
// compiled execution are held to the same answers.
static void check_specialized_generic_is_as(const Ref<FoundryScript> &p_script) {
	const Ref<FoundryScript> crate = get_generic_subclass(p_script, SNAME("Crate"));
	const Ref<FoundryScript> other = get_generic_subclass(p_script, SNAME("Other"));
	const Ref<FoundryScript> int_crate_class = get_generic_subclass(p_script, SNAME("IntCrate"));
	const Ref<FoundryScript> derived = get_generic_subclass(p_script, SNAME("Derived"));
	REQUIRE(crate.is_valid());
	REQUIRE(other.is_valid());
	REQUIRE(int_crate_class.is_valid());
	REQUIRE(derived.is_valid());

	ContainerType int_argument;
	int_argument.builtin_type = Variant::INT;
	int_argument.numeric_type = NumericType::INT32;
	ContainerType string_argument;
	string_argument.builtin_type = Variant::STRING;
	ContainerType int_array_argument;
	int_array_argument.builtin_type = Variant::ARRAY;
	int_array_argument.element_types.push_back(int_argument);

	Vector<ContainerType> int_arguments;
	int_arguments.push_back(int_argument);
	Vector<ContainerType> string_arguments;
	string_arguments.push_back(string_argument);
	Vector<ContainerType> int_array_arguments;
	int_array_arguments.push_back(int_array_argument);

	const Variant int_crate = new_generic_runtime_instance(crate, int_arguments);
	const Variant string_crate = new_generic_runtime_instance(crate, string_arguments);
	const Variant raw_crate = new_generic_runtime_instance(crate, Vector<ContainerType>());
	const Variant array_crate = new_generic_runtime_instance(crate, int_array_arguments);
	const Variant other_int = new_generic_runtime_instance(other, int_arguments);
	const Variant fixed_leaf = new_generic_runtime_instance(int_crate_class, Vector<ContainerType>());
	const Variant forwarded_leaf = new_generic_runtime_instance(derived, int_arguments);

	// The value's own leaf identity and reified vector are what the predicate reads, so they are
	// asserted directly before any test result is trusted.
	FSInstance *int_crate_instance = fs_instance_of(int_crate);
	REQUIRE(int_crate_instance != nullptr);
	CHECK(int_crate_instance->get_script() == crate);
	REQUIRE(int_crate_instance->get_type_arguments().size() == 1);
	CHECK(int_crate_instance->get_type_arguments()[0].builtin_type == Variant::INT);

	FSInstance *raw_crate_instance = fs_instance_of(raw_crate);
	REQUIRE(raw_crate_instance != nullptr);
	CHECK(raw_crate_instance->get_script() == crate);
	CHECK(raw_crate_instance->get_type_arguments().is_empty());

	// A non-generic leaf reifies nothing of its own; its evidence comes from the fixed base binding.
	FSInstance *fixed_leaf_instance = fs_instance_of(fixed_leaf);
	REQUIRE(fixed_leaf_instance != nullptr);
	CHECK(fixed_leaf_instance->get_script() == int_crate_class);
	CHECK(fixed_leaf_instance->get_type_arguments().is_empty());

	FSInstance *array_crate_instance = fs_instance_of(array_crate);
	REQUIRE(array_crate_instance != nullptr);
	REQUIRE(array_crate_instance->get_type_arguments().size() == 1);
	CHECK(array_crate_instance->get_type_arguments()[0].builtin_type == Variant::ARRAY);
	REQUIRE(array_crate_instance->get_type_arguments()[0].element_types.size() == 1);
	CHECK(array_crate_instance->get_type_arguments()[0].element_types[0].builtin_type == Variant::INT);

	// Raw targets stay nominal.
	CHECK(bool(call_generic_runtime_static(p_script, SNAME("is_crate"), { int_crate })));
	CHECK(bool(call_generic_runtime_static(p_script, SNAME("is_crate"), { raw_crate })));
	CHECK(bool(call_generic_runtime_static(p_script, SNAME("is_crate"), { fixed_leaf })));
	CHECK_FALSE(bool(call_generic_runtime_static(p_script, SNAME("is_crate"), { other_int })));

	// Same script, exact match and mismatch.
	CHECK(bool(call_generic_runtime_static(p_script, SNAME("is_int_crate"), { int_crate })));
	CHECK_FALSE(bool(call_generic_runtime_static(p_script, SNAME("is_string_crate"), { int_crate })));
	CHECK(bool(call_generic_runtime_static(p_script, SNAME("is_string_crate"), { string_crate })));
	CHECK_FALSE(bool(call_generic_runtime_static(p_script, SNAME("is_int_crate"), { string_crate })));

	// A raw instance carries no evidence for a specialized predicate.
	CHECK_FALSE(bool(call_generic_runtime_static(p_script, SNAME("is_int_crate"), { raw_crate })));

	// An unrelated generic class with the same argument is still unrelated.
	CHECK_FALSE(bool(call_generic_runtime_static(p_script, SNAME("is_int_crate"), { other_int })));

	// Fixed-base and forwarded-base projection.
	CHECK(bool(call_generic_runtime_static(p_script, SNAME("is_int_crate"), { fixed_leaf })));
	CHECK_FALSE(bool(call_generic_runtime_static(p_script, SNAME("is_string_crate"), { fixed_leaf })));
	CHECK(bool(call_generic_runtime_static(p_script, SNAME("is_int_crate"), { forwarded_leaf })));
	CHECK_FALSE(bool(call_generic_runtime_static(p_script, SNAME("is_string_crate"), { forwarded_leaf })));

	// Nested arguments compare recursively.
	CHECK(bool(call_generic_runtime_static(p_script, SNAME("is_int_array_crate"), { array_crate })));
	CHECK_FALSE(bool(call_generic_runtime_static(p_script, SNAME("is_string_array_crate"), { array_crate })));
	CHECK_FALSE(bool(call_generic_runtime_static(p_script, SNAME("is_int_array_crate"), { int_crate })));

	// Null is neither an instance nor a handle.
	CHECK_FALSE(bool(call_generic_runtime_static(p_script, SNAME("is_int_crate"), { Variant() })));
	CHECK_FALSE(bool(call_generic_runtime_static(p_script, SNAME("is_int_crate_handle"), { Variant() })));

	// `as` agrees with `is`, preserving identity on success and yielding null on failure.
	CHECK(call_generic_runtime_static(p_script, SNAME("as_int_crate"), { int_crate }) == int_crate);
	CHECK(call_generic_runtime_static(p_script, SNAME("as_int_crate"), { fixed_leaf }) == fixed_leaf);
	CHECK(call_generic_runtime_static(p_script, SNAME("as_int_crate"), { string_crate }).get_type() == Variant::NIL);
	CHECK(call_generic_runtime_static(p_script, SNAME("as_int_crate"), { raw_crate }).get_type() == Variant::NIL);

	// The class-handle form answers with the same relation over its own runtime values.
	const Variant int_handle = call_generic_runtime_static(p_script, SNAME("int_crate_handle"), {});
	const Variant raw_handle = call_generic_runtime_static(p_script, SNAME("raw_crate_handle"), {});
	CHECK(bool(call_generic_runtime_static(p_script, SNAME("is_int_crate_handle"), { int_handle })));
	CHECK_FALSE(bool(call_generic_runtime_static(p_script, SNAME("is_string_crate_handle"), { int_handle })));
	CHECK_FALSE(bool(call_generic_runtime_static(p_script, SNAME("is_int_crate_handle"), { raw_handle })));
	// The two forms stay distinct value kinds: an instance is not a class handle, and vice versa.
	CHECK_FALSE(bool(call_generic_runtime_static(p_script, SNAME("is_int_crate_handle"), { int_crate })));
	CHECK_FALSE(bool(call_generic_runtime_static(p_script, SNAME("is_int_crate"), { int_handle })));
}

TEST_CASE("[Modules][FoundryScript][GenericRuntime] Specialized generic is/as uses reified arguments") {
	const Ref<FoundryScript> script = compile_bytecode_test_source(
			"class Crate[T]:\n"
			"\tvar item: T\n"
			"\n"
			"class Other[T]:\n"
			"\tvar item: T\n"
			"\n"
			"class IntCrate extends Crate[int]:\n"
			"\tpass\n"
			"\n"
			"class Derived[U] extends Crate[U]:\n"
			"\tpass\n"
			"\n"
			"static func is_crate(value: Variant) -> bool:\n"
			"\treturn value is Crate\n"
			"\n"
			"static func is_int_crate(value: Variant) -> bool:\n"
			"\treturn value is Crate[int]\n"
			"\n"
			"static func is_string_crate(value: Variant) -> bool:\n"
			"\treturn value is Crate[String]\n"
			"\n"
			"static func is_int_array_crate(value: Variant) -> bool:\n"
			"\treturn value is Crate[Array[int]]\n"
			"\n"
			"static func is_string_array_crate(value: Variant) -> bool:\n"
			"\treturn value is Crate[Array[String]]\n"
			"\n"
			"static func as_int_crate(value: Variant) -> Variant:\n"
			"\treturn value as Crate[int]\n"
			"\n"
			"static func is_int_crate_handle(value: Variant) -> bool:\n"
			"\treturn value is Type[Crate[int]]\n"
			"\n"
			"static func is_string_crate_handle(value: Variant) -> bool:\n"
			"\treturn value is Type[Crate[String]]\n"
			"\n"
			"static func int_crate_handle() -> Variant:\n"
			"\treturn Crate[int]\n"
			"\n"
			"static func raw_crate_handle() -> Variant:\n"
			"\treturn Crate\n");

	check_specialized_generic_is_as(script);

	// The compiled form must answer identically: the descriptor operands survive export and load.
	BytecodeTestResolver resolver;
	const Ref<FoundryScript> restored = bytecode_round_trip_script(script, &resolver);
	check_specialized_generic_is_as(restored);

	restored->clear();
	script->clear();
}

// The trait counterpart of `check_specialized_generic_is_as`. A trait target's nominal answer is a
// conformance lookup rather than an inheritance walk, so this pins that the specialized answer still
// comes from the implementer's recorded trait argument bindings, through direct `uses`, an inherited
// one, a supertrait, and a forwarded class parameter.
static void check_specialized_generic_trait_is_as(const Ref<FoundryScript> &p_script) {
	const Ref<FoundryScript> holder_trait = get_generic_subclass(p_script, SNAME("Holder"));
	const Ref<FoundryScript> int_holder = get_generic_subclass(p_script, SNAME("IntHolder"));
	const Ref<FoundryScript> string_holder = get_generic_subclass(p_script, SNAME("StringHolder"));
	const Ref<FoundryScript> derived_holder = get_generic_subclass(p_script, SNAME("DerivedIntHolder"));
	const Ref<FoundryScript> relay_holder = get_generic_subclass(p_script, SNAME("RelayHolder"));
	const Ref<FoundryScript> forwarder = get_generic_subclass(p_script, SNAME("Forwarder"));
	const Ref<FoundryScript> plain = get_generic_subclass(p_script, SNAME("Plain"));
	REQUIRE(holder_trait.is_valid());
	REQUIRE(int_holder.is_valid());
	REQUIRE(string_holder.is_valid());
	REQUIRE(derived_holder.is_valid());
	REQUIRE(relay_holder.is_valid());
	REQUIRE(forwarder.is_valid());
	REQUIRE(plain.is_valid());

	ContainerType int_argument;
	int_argument.builtin_type = Variant::INT;
	int_argument.numeric_type = NumericType::INT32;
	ContainerType string_argument;
	string_argument.builtin_type = Variant::STRING;
	Vector<ContainerType> int_arguments;
	int_arguments.push_back(int_argument);
	Vector<ContainerType> string_arguments;
	string_arguments.push_back(string_argument);

	const Variant int_value = new_generic_runtime_instance(int_holder, Vector<ContainerType>());
	const Variant string_value = new_generic_runtime_instance(string_holder, Vector<ContainerType>());
	const Variant derived_value = new_generic_runtime_instance(derived_holder, Vector<ContainerType>());
	const Variant relay_value = new_generic_runtime_instance(relay_holder, Vector<ContainerType>());
	const Variant forwarded_value = new_generic_runtime_instance(forwarder, int_arguments);
	const Variant raw_forwarder_value = new_generic_runtime_instance(forwarder, Vector<ContainerType>());
	const Variant plain_value = new_generic_runtime_instance(plain, Vector<ContainerType>());

	// The evidence a conforming implementer carries is its own leaf script plus its own reified vector;
	// the trait argument lives in the leaf's per-ancestor binding table, not in the instance.
	FSInstance *int_instance = fs_instance_of(int_value);
	REQUIRE(int_instance != nullptr);
	CHECK(int_instance->get_script() == int_holder);
	CHECK(int_instance->get_type_arguments().is_empty());

	Vector<ProjectedContainerType> projected;
	REQUIRE(int_holder->project_type_arguments_onto_base(holder_trait, Vector<ContainerType>(), projected));
	REQUIRE(projected.size() == 1);
	CHECK(projected[0].state == ProjectedContainerType::EXACT);
	CHECK(projected[0].outer.builtin_type == Variant::INT);

	// Raw trait targets stay nominal, including for an implementer that supplied no argument at all.
	CHECK(bool(call_generic_runtime_static(p_script, SNAME("is_holder"), { int_value })));
	CHECK(bool(call_generic_runtime_static(p_script, SNAME("is_holder"), { string_value })));
	CHECK(bool(call_generic_runtime_static(p_script, SNAME("is_holder"), { raw_forwarder_value })));
	CHECK_FALSE(bool(call_generic_runtime_static(p_script, SNAME("is_holder"), { plain_value })));

	// A direct `uses Holder[arg]` answers invariantly.
	CHECK(bool(call_generic_runtime_static(p_script, SNAME("is_int_holder"), { int_value })));
	CHECK_FALSE(bool(call_generic_runtime_static(p_script, SNAME("is_string_holder"), { int_value })));
	CHECK(bool(call_generic_runtime_static(p_script, SNAME("is_string_holder"), { string_value })));
	CHECK_FALSE(bool(call_generic_runtime_static(p_script, SNAME("is_int_holder"), { string_value })));

	// A subclass inherits its base's conformance evidence.
	CHECK(bool(call_generic_runtime_static(p_script, SNAME("is_int_holder"), { derived_value })));
	CHECK_FALSE(bool(call_generic_runtime_static(p_script, SNAME("is_string_holder"), { derived_value })));

	// A supertrait's argument projects through the intermediate trait.
	CHECK(bool(call_generic_runtime_static(p_script, SNAME("is_int_relayed"), { relay_value })));
	CHECK_FALSE(bool(call_generic_runtime_static(p_script, SNAME("is_string_relayed"), { relay_value })));
	CHECK(bool(call_generic_runtime_static(p_script, SNAME("is_int_holder"), { relay_value })));
	CHECK_FALSE(bool(call_generic_runtime_static(p_script, SNAME("is_string_holder"), { relay_value })));

	// A generic implementer forwards its own reified argument to the trait.
	CHECK(bool(call_generic_runtime_static(p_script, SNAME("is_int_holder"), { forwarded_value })));
	CHECK_FALSE(bool(call_generic_runtime_static(p_script, SNAME("is_string_holder"), { forwarded_value })));

	// A raw generic implementer proves nothing, so the specialized target rejects it.
	CHECK_FALSE(bool(call_generic_runtime_static(p_script, SNAME("is_int_holder"), { raw_forwarder_value })));

	// A non-implementer and null fail both questions.
	CHECK_FALSE(bool(call_generic_runtime_static(p_script, SNAME("is_int_holder"), { plain_value })));
	CHECK_FALSE(bool(call_generic_runtime_static(p_script, SNAME("is_int_holder"), { Variant() })));

	// `as` agrees with `is` for trait targets too.
	CHECK(call_generic_runtime_static(p_script, SNAME("as_int_holder"), { int_value }) == int_value);
	CHECK(call_generic_runtime_static(p_script, SNAME("as_int_holder"), { derived_value }) == derived_value);
	CHECK(call_generic_runtime_static(p_script, SNAME("as_int_holder"), { string_value }).get_type() == Variant::NIL);
	CHECK(call_generic_runtime_static(p_script, SNAME("as_int_holder"), { raw_forwarder_value }).get_type() == Variant::NIL);
	CHECK(call_generic_runtime_static(p_script, SNAME("as_holder"), { string_value }) == string_value);
}

TEST_CASE("[Modules][FoundryScript][GenericRuntime] Specialized generic trait is/as uses conformance arguments") {
	const Ref<FoundryScript> script = compile_bytecode_test_source(
			"trait Holder[T]:\n"
			"\tvar item: T\n"
			"\n"
			"trait Relayed[U]:\n"
			"\tuses Holder[U]\n"
			"\n"
			"class IntHolder:\n"
			"\tuses Holder[int]\n"
			"\n"
			"class StringHolder:\n"
			"\tuses Holder[String]\n"
			"\n"
			"class DerivedIntHolder extends IntHolder:\n"
			"\tvar extra: int = 0\n"
			"\n"
			"class RelayHolder:\n"
			"\tuses Relayed[int]\n"
			"\n"
			"class Forwarder[W]:\n"
			"\tuses Holder[W]\n"
			"\n"
			"class Plain:\n"
			"\tvar value: int = 0\n"
			"\n"
			"static func is_holder(value: Variant) -> bool:\n"
			"\treturn value is Holder\n"
			"\n"
			"static func is_int_holder(value: Variant) -> bool:\n"
			"\treturn value is Holder[int]\n"
			"\n"
			"static func is_string_holder(value: Variant) -> bool:\n"
			"\treturn value is Holder[String]\n"
			"\n"
			"static func is_int_relayed(value: Variant) -> bool:\n"
			"\treturn value is Relayed[int]\n"
			"\n"
			"static func is_string_relayed(value: Variant) -> bool:\n"
			"\treturn value is Relayed[String]\n"
			"\n"
			"static func as_int_holder(value: Variant) -> Variant:\n"
			"\treturn value as Holder[int]\n"
			"\n"
			"static func as_holder(value: Variant) -> Variant:\n"
			"\treturn value as Holder\n");

	check_specialized_generic_trait_is_as(script);

	// The per-ancestor trait binding table must survive export and load, or the compiled form would
	// fall back to the nominal answer.
	BytecodeTestResolver resolver;
	const Ref<FoundryScript> restored = bytecode_round_trip_script(script, &resolver);
	check_specialized_generic_trait_is_as(restored);

	restored->clear();
	script->clear();
}

// The `int` argument every retroactive-conformance case below declares. `int` lowers to a 32-bit
// carrier, so a descriptor built without the width would never equal a recorded one.
static ContainerType retroactive_conformance_int_argument() {
	ContainerType argument;
	argument.builtin_type = Variant::INT;
	argument.numeric_type = NumericType::INT32;
	return argument;
}

TEST_CASE("[Modules][FoundryScript][GenericRuntime] Retroactive conformance arguments answer specialized is/as") {
	const Ref<FoundryScript> script = compile_bytecode_test_source(
			"trait ScriptStore[T]:\n"
			"\tabstract func fetch() -> T\n"
			"\n"
			"trait SubStore[T]:\n"
			"\tuses ScriptStore[T]\n"
			"\n"
			"\tabstract func stow(item: T) -> void\n"
			"\n"
			"trait NativeStore[T]:\n"
			"\tabstract func native_fetch() -> T\n"
			"\n"
			"trait BuiltinStore[T]:\n"
			"\tabstract func builtin_fetch() -> T\n"
			"\n"
			"class Target:\n"
			"\tvar kept: int = 0\n"
			"\n"
			"class DerivedTarget extends Target:\n"
			"\tpass\n"
			"\n"
			"extend Target uses SubStore[int]:\n"
			"\tfunc stow(item: int) -> void:\n"
			"\t\tkept = item\n"
			"\n"
			"\tfunc fetch() -> int:\n"
			"\t\treturn kept\n"
			"\n"
			"extend RefCounted uses NativeStore[int]:\n"
			"\tfunc native_fetch() -> int:\n"
			"\t\treturn 7\n"
			"\n"
			"extend int uses BuiltinStore[int]:\n"
			"\tfunc builtin_fetch() -> int:\n"
			"\t\treturn self\n"
			"\n"
			"static func is_sub_store(value: Variant) -> bool:\n"
			"\treturn value is SubStore\n"
			"\n"
			"static func is_int_sub_store(value: Variant) -> bool:\n"
			"\treturn value is SubStore[int]\n"
			"\n"
			"static func is_string_sub_store(value: Variant) -> bool:\n"
			"\treturn value is SubStore[String]\n"
			"\n"
			"static func is_int_script_store(value: Variant) -> bool:\n"
			"\treturn value is ScriptStore[int]\n"
			"\n"
			"static func is_string_script_store(value: Variant) -> bool:\n"
			"\treturn value is ScriptStore[String]\n"
			"\n"
			"static func is_int_native_store(value: Variant) -> bool:\n"
			"\treturn value is NativeStore[int]\n"
			"\n"
			"static func is_string_native_store(value: Variant) -> bool:\n"
			"\treturn value is NativeStore[String]\n"
			"\n"
			"static func is_int_builtin_store(value: Variant) -> bool:\n"
			"\treturn value is BuiltinStore[int]\n"
			"\n"
			"static func is_string_builtin_store(value: Variant) -> bool:\n"
			"\treturn value is BuiltinStore[String]\n"
			"\n"
			"static func as_int_sub_store(value: Variant) -> Variant:\n"
			"\treturn value as SubStore[int]\n"
			"\n"
			"static func as_string_sub_store(value: Variant) -> Variant:\n"
			"\treturn value as SubStore[String]\n");
	REQUIRE(script->is_valid());

	BytecodeConformanceRegistryRestore registry_restore(script->get_script_path());

	const Ref<FoundryScript> target = get_generic_subclass(script, SNAME("Target"));
	const Ref<FoundryScript> derived_target = get_generic_subclass(script, SNAME("DerivedTarget"));
	const Ref<FoundryScript> script_store = get_generic_subclass(script, SNAME("ScriptStore"));
	const Ref<FoundryScript> sub_store = get_generic_subclass(script, SNAME("SubStore"));
	const Ref<FoundryScript> native_store = get_generic_subclass(script, SNAME("NativeStore"));
	const Ref<FoundryScript> builtin_store = get_generic_subclass(script, SNAME("BuiltinStore"));
	REQUIRE(target.is_valid());
	REQUIRE(derived_target.is_valid());
	REQUIRE(script_store.is_valid());
	REQUIRE(sub_store.is_valid());
	REQUIRE(native_store.is_valid());
	REQUIRE(builtin_store.is_valid());

	const FSConformanceRegistry *registry = FSConformanceRegistry::get_singleton();
	const ContainerType int_argument = retroactive_conformance_int_argument();

	// The registry surface itself: the declared argument is recorded per trait identity, and the
	// supertrait identity carries the argument substituted into it.
	Vector<ContainerType> recorded;
	REQUIRE(registry->get_conformance_type_arguments(target->get_fully_qualified_name(),
			sub_store->get_trait_type_name(), recorded));
	REQUIRE_EQ(recorded.size(), 1);
	CHECK_EQ(recorded[0].builtin_type, Variant::INT);
	CHECK_EQ(recorded[0].numeric_type, NumericType::INT32);
	CHECK(recorded[0] == int_argument);

	REQUIRE(registry->get_conformance_type_arguments(target->get_fully_qualified_name(),
			script_store->get_trait_type_name(), recorded));
	REQUIRE_EQ(recorded.size(), 1);
	CHECK_EQ(recorded[0].builtin_type, Variant::INT);
	CHECK_EQ(recorded[0].numeric_type, NumericType::INT32);

	// A native conformance answers for the conforming class and, through the ancestor walk, for its
	// subclasses.
	REQUIRE(registry->get_native_conformance_type_arguments(SNAME("RefCounted"),
			native_store->get_trait_type_name(), recorded));
	REQUIRE_EQ(recorded.size(), 1);
	CHECK_EQ(recorded[0].numeric_type, NumericType::INT32);
	REQUIRE(registry->get_native_conformance_type_arguments(SNAME("Resource"),
			native_store->get_trait_type_name(), recorded));
	REQUIRE_EQ(recorded.size(), 1);
	CHECK_EQ(recorded[0].numeric_type, NumericType::INT32);

	REQUIRE(registry->get_builtin_conformance_type_arguments(Variant::INT,
			builtin_store->get_trait_type_name(), recorded));
	REQUIRE_EQ(recorded.size(), 1);
	CHECK_EQ(recorded[0].numeric_type, NumericType::INT32);

	// A trait nothing conformed to, and a target nothing conformed, report an absence of evidence
	// rather than an empty (wildcard-shaped) success.
	CHECK_FALSE(registry->get_conformance_type_arguments(target->get_fully_qualified_name(),
			SNAME("NoSuchTraitIdentity"), recorded));
	CHECK_FALSE(registry->get_builtin_conformance_type_arguments(Variant::STRING,
			builtin_store->get_trait_type_name(), recorded));

	// The VM answers `is`/`as` from those records, for the conformed script class, a subclass of it,
	// a subclass of the conformed engine class, and a conformed builtin value.
	const Variant target_value = new_generic_runtime_instance(target, Vector<ContainerType>());
	const Variant derived_value = new_generic_runtime_instance(derived_target, Vector<ContainerType>());
	Ref<Resource> native_resource;
	native_resource.instantiate();
	const Variant native_value = native_resource;
	const Variant builtin_value = 3;

	CHECK(bool(call_generic_runtime_static(script, SNAME("is_sub_store"), { target_value })));
	CHECK(bool(call_generic_runtime_static(script, SNAME("is_int_sub_store"), { target_value })));
	CHECK_FALSE(bool(call_generic_runtime_static(script, SNAME("is_string_sub_store"), { target_value })));
	CHECK(bool(call_generic_runtime_static(script, SNAME("is_int_sub_store"), { derived_value })));
	CHECK_FALSE(bool(call_generic_runtime_static(script, SNAME("is_string_sub_store"), { derived_value })));

	CHECK(bool(call_generic_runtime_static(script, SNAME("is_int_script_store"), { target_value })));
	CHECK_FALSE(bool(call_generic_runtime_static(script, SNAME("is_string_script_store"), { target_value })));

	CHECK(bool(call_generic_runtime_static(script, SNAME("is_int_native_store"), { native_value })));
	CHECK_FALSE(bool(call_generic_runtime_static(script, SNAME("is_string_native_store"), { native_value })));

	CHECK(bool(call_generic_runtime_static(script, SNAME("is_int_builtin_store"), { builtin_value })));
	CHECK_FALSE(bool(call_generic_runtime_static(script, SNAME("is_string_builtin_store"), { builtin_value })));

	// `as` agrees with `is` row for row.
	CHECK(call_generic_runtime_static(script, SNAME("as_int_sub_store"), { target_value }) == target_value);
	CHECK(call_generic_runtime_static(script, SNAME("as_string_sub_store"), { target_value }).get_type() == Variant::NIL);
	CHECK(call_generic_runtime_static(script, SNAME("as_int_sub_store"), { Variant() }).get_type() == Variant::NIL);

	script->clear();
}

TEST_CASE("[Modules][FoundryScript][GenericRuntime] A freed conformance type-argument script fails specialized is/as") {
	const Ref<FoundryScript> script = compile_bytecode_test_source(
			"trait Boxed[T]:\n"
			"\tabstract func unbox() -> T\n"
			"\n"
			"class Payload:\n"
			"\tpass\n"
			"\n"
			"class Target:\n"
			"\tpass\n"
			"\n"
			"extend Target uses Boxed[Payload]:\n"
			"\tfunc unbox() -> Payload:\n"
			"\t\treturn null\n"
			"\n"
			"static func is_boxed(value: Variant) -> bool:\n"
			"\treturn value is Boxed\n"
			"\n"
			"static func is_payload_boxed(value: Variant) -> bool:\n"
			"\treturn value is Boxed[Payload]\n");
	REQUIRE(script->is_valid());

	const String script_path = script->get_script_path();
	BytecodeConformanceRegistryRestore registry_restore(script_path);

	const Ref<FoundryScript> target = get_generic_subclass(script, SNAME("Target"));
	const Ref<FoundryScript> boxed = get_generic_subclass(script, SNAME("Boxed"));
	REQUIRE(target.is_valid());
	REQUIRE(boxed.is_valid());
	const StringName boxed_trait = boxed->get_trait_type_name();
	const String target_key = target->get_fully_qualified_name();

	FSConformanceRegistry *registry = FSConformanceRegistry::get_singleton();
	const Variant target_value = new_generic_runtime_instance(target, Vector<ContainerType>());

	// The live baseline: a script-typed argument is recorded and answers the specialized target.
	Vector<ContainerType> recorded;
	REQUIRE(registry->get_conformance_type_arguments(target_key, boxed_trait, recorded));
	REQUIRE_EQ(recorded.size(), 1);
	CHECK(recorded[0].script.is_valid());
	CHECK(bool(call_generic_runtime_static(script, SNAME("is_boxed"), { target_value })));
	CHECK(bool(call_generic_runtime_static(script, SNAME("is_payload_boxed"), { target_value })));

	// Re-record the same conformance against an argument script whose only strong reference is local.
	// A compiled argument is kept alive by the declaring script's own graph, so this is the only way
	// to observe an argument outliving nothing: the registry describes it weakly.
	Ref<FoundryScript> free_standing_argument;
	free_standing_argument.instantiate();
	{
		Vector<FSConformanceRegistry::RuntimeConformance> conformances =
				registry->get_runtime_witnesses(script_path);
		REQUIRE_FALSE(conformances.is_empty());
		ContainerType argument;
		argument.builtin_type = Variant::OBJECT;
		argument.script = free_standing_argument;
		for (FSConformanceRegistry::RuntimeConformance &conformance : conformances) {
			conformance.trait_type_arguments.clear();
			conformance.trait_type_arguments.push_back(FSWeakContainerType::from_container_type(argument));
		}
		registry->register_runtime_witnesses(script_path, conformances);
	}
	REQUIRE(registry->get_conformance_type_arguments(target_key, boxed_trait, recorded));
	REQUIRE_EQ(recorded.size(), 1);
	CHECK(recorded[0].script.ptr() == free_standing_argument.ptr());

	// Dropping the last strong reference frees the argument. The record still names it, so the query
	// must report an absence of evidence rather than materialize a null-script stand-in, and the
	// specialized target must fail while nominal membership is untouched.
	free_standing_argument = Ref<FoundryScript>();

	CHECK_FALSE(registry->get_conformance_type_arguments(target_key, boxed_trait, recorded));
	CHECK(recorded.is_empty());
	CHECK(bool(call_generic_runtime_static(script, SNAME("is_boxed"), { target_value })));
	CHECK_FALSE(bool(call_generic_runtime_static(script, SNAME("is_payload_boxed"), { target_value })));

	script->clear();
}

#endif // TOOLS_ENABLED

} // namespace FSTests
