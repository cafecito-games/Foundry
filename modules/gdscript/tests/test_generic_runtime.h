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

} // namespace GDScriptTests
