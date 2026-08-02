/**************************************************************************/
/*  test_class_handle_container.h                                         */
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
#include "../fs_parser.h"

#include "core/object/class_db.h"
#include "core/object/class_handle.h"
#include "core/variant/array.h"
#include "core/variant/container_type_validate.h"

#include "tests/test_macros.h"

// Coverage for nested `Type[T]` element typing (issue #1528): a typed container element described as a
// class handle is validated by core's single class-handle rule, which `FSDataType::is_type_handle_type()`
// delegates to. The cases here are the ones that need real Foundry Script types — script inheritance,
// traits, and generic specialization — which the core-only suite cannot construct.

namespace FSTests {

struct ScopedClassHandleContainerLanguage {
	ScopedClassHandleContainerLanguage() {
		if (!FSLanguage::get_singleton()->has_any_global_constant(SNAME("RefCounted"))) {
			FSLanguage::get_singleton()->init();
		}
	}
};

static Ref<FoundryScript> compile_class_handle_container_source(const String &p_source) {
	static int unique_index = 0;
	const String path = vformat("user://test_class_handle_container_%d.fs", unique_index++);

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

static Ref<FoundryScript> class_handle_container_subclass(const Ref<FoundryScript> &p_script, const StringName &p_name) {
	const HashMap<StringName, Ref<FoundryScript>>::ConstIterator element = p_script->get_subclasses().find(p_name);
	return element ? element->value : Ref<FoundryScript>();
}

static ContainerType class_handle_type_for(const Ref<FoundryScript> &p_script, const Vector<ContainerType> &p_type_arguments = Vector<ContainerType>()) {
	ContainerType type;
	type.builtin_type = Variant::OBJECT;
	type.class_name = p_script->get_instance_base_type();
	type.script = p_script;
	type.type_arguments = p_type_arguments;
	type.is_type_handle = true;
	return type;
}

// Both the core rule and the Foundry Script runtime predicate must answer identically for the same
// descriptor and value; the module predicate delegates, so a divergence means the delegation broke.
static bool check_class_handle_agreement(const ContainerType &p_expected, const Variant &p_value) {
	const bool core_answer = ContainerTypeValidate(p_expected).test_validate(p_value);
	const bool module_answer = FSDataType::from_type_handle_container_type(p_expected).is_type_handle_type(p_value);
	CHECK(core_answer == module_answer);
	return core_answer;
}

static int typed_array_accepted_count(const ContainerType &p_element_type, const Vector<Variant> &p_values) {
	Array array;
	array.set_typed(p_element_type);
	ERR_PRINT_OFF;
	for (const Variant &value : p_values) {
		array.push_back(value);
	}
	ERR_PRINT_ON;
	return array.size();
}

TEST_CASE("[Modules][FoundryScript][ClassHandle] Foundry Script class handles are core ClassHandles") {
	CHECK(ClassDB::class_exists(SNAME("ClassHandle")));
	CHECK(ClassDB::is_parent_class(SNAME("FSSpecializedClassHandle"), SNAME("ClassHandle")));

	Ref<FSNativeClass> native_class = memnew(FSNativeClass(SNAME("RefCounted")));
	CHECK(Object::cast_to<ClassHandle>(native_class.ptr()) != nullptr);
	CHECK(native_class->get_represented_native_class() == SNAME("RefCounted"));
	// The existing bound surface is unchanged by the rebase.
	CHECK(native_class->get_name() == SNAME("RefCounted"));
}

TEST_CASE("[Modules][FoundryScript][ClassHandle] A handle-typed Array accepts derived script handles") {
	ScopedClassHandleContainerLanguage language;

	const char *source =
			"class Base:\n"
			"\tpass\n"
			"\n"
			"class Derived extends Base:\n"
			"\tpass\n"
			"\n"
			"class Unrelated:\n"
			"\tpass\n";

	Ref<FoundryScript> script = compile_class_handle_container_source(source);
	Ref<FoundryScript> base = class_handle_container_subclass(script, "Base");
	Ref<FoundryScript> derived = class_handle_container_subclass(script, "Derived");
	Ref<FoundryScript> unrelated = class_handle_container_subclass(script, "Unrelated");
	REQUIRE(base.is_valid());
	REQUIRE(derived.is_valid());
	REQUIRE(unrelated.is_valid());

	const ContainerType expected = class_handle_type_for(base);

	CHECK(check_class_handle_agreement(expected, base));
	CHECK(check_class_handle_agreement(expected, derived));
	CHECK_FALSE(check_class_handle_agreement(expected, unrelated));

	// An instance of the represented class is not a handle for it.
	Callable::CallError call_error;
	Variant instance = base->_new(nullptr, -1, call_error);
	REQUIRE(call_error.error == Callable::CallError::CALL_OK);
	CHECK_FALSE(check_class_handle_agreement(expected, instance));

	Vector<Variant> values;
	values.push_back(base);
	values.push_back(derived);
	values.push_back(unrelated);
	values.push_back(instance);
	CHECK(typed_array_accepted_count(expected, values) == 2);
}

TEST_CASE("[Modules][FoundryScript][ClassHandle] A trait-typed handle slot rejects native class handles") {
	ScopedClassHandleContainerLanguage language;

	const char *source =
			"trait Factory:\n"
			"\tpass\n"
			"\n"
			"class Conforming:\n"
			"\tuses Factory\n"
			"\n"
			"class NonConforming:\n"
			"\tpass\n"
			"\n"
			"func accepts_factory(_value: Factory) -> void:\n"
			"\tpass\n";

	Ref<FoundryScript> script = compile_class_handle_container_source(source);
	Ref<FoundryScript> factory = class_handle_container_subclass(script, "Factory");
	Ref<FoundryScript> conforming = class_handle_container_subclass(script, "Conforming");
	Ref<FoundryScript> non_conforming = class_handle_container_subclass(script, "NonConforming");
	REQUIRE(factory.is_valid());
	REQUIRE(conforming.is_valid());
	REQUIRE(non_conforming.is_valid());
	REQUIRE(factory->is_trait_type());

	const ContainerType expected = class_handle_type_for(factory);

	CHECK(check_class_handle_agreement(expected, conforming));
	CHECK_FALSE(check_class_handle_agreement(expected, non_conforming));

	// A native class handle denotes no script, so it can never satisfy a trait-typed slot, matching
	// top-level `Type[Factory]`.
	Ref<FSNativeClass> native_class = memnew(FSNativeClass(SNAME("RefCounted")));
	CHECK_FALSE(check_class_handle_agreement(expected, native_class));

	Vector<Variant> values;
	values.push_back(conforming);
	values.push_back(non_conforming);
	values.push_back(native_class);
	CHECK(typed_array_accepted_count(expected, values) == 1);
}

TEST_CASE("[Modules][FoundryScript][ClassHandle] A specialized handle slot is invariant in its arguments") {
	ScopedClassHandleContainerLanguage language;

	const char *source =
			"class Box[T]:\n"
			"\tpass\n";

	Ref<FoundryScript> script = compile_class_handle_container_source(source);
	Ref<FoundryScript> box = class_handle_container_subclass(script, "Box");
	REQUIRE(box.is_valid());

	ContainerType int_argument;
	int_argument.builtin_type = Variant::INT;
	ContainerType string_argument;
	string_argument.builtin_type = Variant::STRING;

	Vector<ContainerType> int_arguments;
	int_arguments.push_back(int_argument);
	Vector<ContainerType> string_arguments;
	string_arguments.push_back(string_argument);

	const ContainerType expected = class_handle_type_for(box, int_arguments);
	// A local class has no global name, so the descriptor renders its engine base with the
	// specialization inside the handle wrapper.
	CHECK(expected.get_type_name() == "Type[RefCounted[int]]");

	Ref<FSSpecializedClassHandle> int_box = FSSpecializedClassHandle::create(box, int_arguments);
	Ref<FSSpecializedClassHandle> string_box = FSSpecializedClassHandle::create(box, string_arguments);
	REQUIRE(int_box.is_valid());
	REQUIRE(string_box.is_valid());

	CHECK(check_class_handle_agreement(expected, int_box));
	CHECK_FALSE(check_class_handle_agreement(expected, string_box));

	// An unspecialized script handle carries no argument evidence at all, so it does not satisfy a
	// specialized slot — the same answer top-level `Type[Box[int]]` gives today.
	CHECK_FALSE(check_class_handle_agreement(expected, box));

	// The unspecialized slot accepts every handle for the class.
	const ContainerType unspecialized = class_handle_type_for(box);
	CHECK(check_class_handle_agreement(unspecialized, int_box));
	CHECK(check_class_handle_agreement(unspecialized, string_box));
	CHECK(check_class_handle_agreement(unspecialized, box));

	Vector<Variant> values;
	values.push_back(int_box);
	values.push_back(string_box);
	values.push_back(box);
	CHECK(typed_array_accepted_count(expected, values) == 1);
}

TEST_CASE("[Modules][FoundryScript][ClassHandle] Null and non-handle values agree with the runtime predicate") {
	ScopedClassHandleContainerLanguage language;

	const char *source =
			"class Base:\n"
			"\tpass\n";

	Ref<FoundryScript> script = compile_class_handle_container_source(source);
	Ref<FoundryScript> base = class_handle_container_subclass(script, "Base");
	REQUIRE(base.is_valid());

	const ContainerType expected = class_handle_type_for(base);

	// Null is permissive, matching the `NATIVE` element branch.
	CHECK(check_class_handle_agreement(expected, Variant()));
	CHECK_FALSE(check_class_handle_agreement(expected, Variant(42)));

	Ref<RefCounted> plain;
	plain.instantiate();
	CHECK_FALSE(check_class_handle_agreement(expected, plain));

	// A native class handle for the script's engine base does not satisfy a script-typed slot, but does
	// satisfy a native-typed one.
	Ref<FSNativeClass> native_class = memnew(FSNativeClass(SNAME("RefCounted")));
	CHECK_FALSE(check_class_handle_agreement(expected, native_class));

	ContainerType native_expected;
	native_expected.builtin_type = Variant::OBJECT;
	native_expected.class_name = SNAME("RefCounted");
	native_expected.is_type_handle = true;
	CHECK(check_class_handle_agreement(native_expected, native_class));
	CHECK(check_class_handle_agreement(native_expected, base));
}

} // namespace FSTests
