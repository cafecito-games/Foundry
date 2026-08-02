/**************************************************************************/
/*  test_resource_format_text_nested_dependencies.h                       */
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

#include "core/io/file_access.h"
#include "core/io/resource_loader.h"
#include "core/io/resource_saver.h"
#include "core/object/ref_counted.h"
#include "core/object/script_instance.h"
#include "core/object/script_language.h"
#include "core/variant/array.h"
#include "core/variant/container_type_validate.h"
#include "core/variant/dictionary.h"

#include "tests/test_macros.h"
#include "tests/test_utils.h"

namespace TestResourceFormatTextNestedDependencies {

// A minimal loadable-in-memory script double. It only needs to behave like a `Resource` with a
// path for `_find_resources`/`_find_resources_in_container_type` to register it as a dependency;
// it is never instantiated.
class NestedDependencyTestScript : public Script {
	FOUNDRY_CLASS(NestedDependencyTestScript, Script);

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
};

static ContainerType make_scripted_class(const StringName &p_class_name, const Ref<Script> &p_script) {
	ContainerType type;
	type.builtin_type = Variant::OBJECT;
	type.class_name = p_class_name;
	type.script = p_script;
	return type;
}

static ContainerType make_array_of(const ContainerType &p_element) {
	ContainerType type;
	type.builtin_type = Variant::ARRAY;
	type.element_types.push_back(p_element);
	return type;
}

// `Box[Wrapped]`: an unspecialized class standing in for a generic script, holding `p_argument`
// as a reified type argument the way `Array[Box[Wrapped]]` would.
static ContainerType make_class_with_type_argument(const StringName &p_class_name, const ContainerType &p_argument) {
	ContainerType type;
	type.builtin_type = Variant::OBJECT;
	type.class_name = p_class_name;
	type.type_arguments.push_back(p_argument);
	return type;
}

struct SavedDependencies {
	List<String> text;
	List<String> binary;
};

static SavedDependencies save_and_collect_dependencies(const Ref<Resource> &p_resource, const String &p_stem) {
	SavedDependencies result;

	const String text_path = TestUtils::get_temp_path(p_stem + ".tres");
	const String binary_path = TestUtils::get_temp_path(p_stem + ".res");

	REQUIRE_EQ(ResourceSaver::save(p_resource, text_path), OK);
	REQUIRE_EQ(ResourceSaver::save(p_resource, binary_path), OK);

	ResourceLoader::get_dependencies(text_path, &result.text);
	ResourceLoader::get_dependencies(binary_path, &result.binary);

	return result;
}

static bool dependencies_contain(const List<String> &p_dependencies, const String &p_needle) {
	for (const String &dependency : p_dependencies) {
		if (dependency.contains(p_needle)) {
			return true;
		}
	}
	return false;
}

TEST_CASE("[ResourceFormatTextNestedDependencies] Script reachable only through a type argument is saved as a dependency") {
	Ref<NestedDependencyTestScript> script;
	script.instantiate();
	script->set_path("res://nested_dependency_type_argument_script.tres");

	// `Array[Wrapper[Box]]`: the script is not the array's own element script, only a reified
	// argument nested one level inside the element type.
	const ContainerType script_argument = make_scripted_class(script->get_instance_base_type(), script);
	const ContainerType element_type = make_class_with_type_argument(SNAME("RefCounted"), script_argument);

	Array values;
	values.set_typed(element_type);

	Ref<Resource> resource = memnew(Resource);
	resource->set_meta("values", values);

	const SavedDependencies dependencies = save_and_collect_dependencies(resource, "nested_dependency_type_argument");

	CHECK(dependencies_contain(dependencies.text, "res://nested_dependency_type_argument_script.tres"));
	CHECK(dependencies_contain(dependencies.binary, "res://nested_dependency_type_argument_script.tres"));
}

TEST_CASE("[ResourceFormatTextNestedDependencies] Script reachable only through a nested element_types slot is saved as a dependency") {
	Ref<NestedDependencyTestScript> script;
	script.instantiate();
	script->set_path("res://nested_dependency_element_type_script.tres");

	// `Array[Array[Box]]`: the script sits two `element_types` hops below the outer array.
	const ContainerType scripted_element = make_scripted_class(script->get_instance_base_type(), script);
	const ContainerType inner_array_type = make_array_of(scripted_element);

	Array values;
	values.set_typed(inner_array_type);

	Ref<Resource> resource = memnew(Resource);
	resource->set_meta("values", values);

	const SavedDependencies dependencies = save_and_collect_dependencies(resource, "nested_dependency_element_type");

	CHECK(dependencies_contain(dependencies.text, "res://nested_dependency_element_type_script.tres"));
	CHECK(dependencies_contain(dependencies.binary, "res://nested_dependency_element_type_script.tres"));
}

TEST_CASE("[ResourceFormatTextNestedDependencies] Text and binary saves agree on the dependency set at nesting depth two") {
	Ref<NestedDependencyTestScript> script;
	script.instantiate();
	script->set_path("res://nested_dependency_depth_two_script.tres");

	// `Array[Wrapper[Box[Wrapper2[BoxScript]]]]`: the script is two `type_arguments` hops deep.
	const ContainerType script_argument = make_scripted_class(script->get_instance_base_type(), script);
	const ContainerType depth_one = make_class_with_type_argument(SNAME("RefCounted"), script_argument);
	const ContainerType depth_two = make_class_with_type_argument(SNAME("Resource"), depth_one);

	Array values;
	values.set_typed(depth_two);

	Ref<Resource> resource = memnew(Resource);
	resource->set_meta("values", values);

	const SavedDependencies dependencies = save_and_collect_dependencies(resource, "nested_dependency_depth_two");

	CHECK(dependencies_contain(dependencies.text, "res://nested_dependency_depth_two_script.tres"));
	CHECK(dependencies_contain(dependencies.binary, "res://nested_dependency_depth_two_script.tres"));

	// The two savers must not silently drift from each other: same dependency count, same paths.
	REQUIRE_EQ(dependencies.text.size(), dependencies.binary.size());
	for (const String &text_dependency : dependencies.text) {
		CHECK(dependencies_contain(dependencies.binary, text_dependency));
	}
}

} // namespace TestResourceFormatTextNestedDependencies
