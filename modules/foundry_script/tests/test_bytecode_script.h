/**************************************************************************/
/*  test_bytecode_script.h                                                */
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

// Shared fixtures and helpers (test resolver, in-process compile, buffer scanning). Every module
// test header is compiled into the single generated test translation unit, so this include does not
// duplicate test registrations.
#include "test_bytecode_serialization.h"

// TestFSCacheAccessor, for asserting FSCache state after resource-pipeline loads.
#include "fs_test_runner_suite.h"

#include "modules/foundry_script/fs_builtin_sources.h"
#include "modules/foundry_script/fs_conformance_registry.h"
#include "modules/foundry_script/fs_name_mangler_analysis.h"

#include "core/io/resource_loader.h"
#include "core/io/resource_saver.h"
#include "scene/main/node.h"

namespace FSTests {

// Friend accessor exposing the private FoundryScript state the whole-script round-trip assertions
// need; binding kinds are surfaced as plain integers matching FoundryScript::TypeArgumentBinding
// (0 = NONE, 1 = FIXED, 2 = OPEN) because the nested type is private.
class TestFSBytecodeScriptAccessor {
public:
	static uint8_t get_self_reflection_kinds(const FSFunction *p_function) {
		return p_function != nullptr ? p_function->self_reflection_kinds : uint8_t(FSFunction::REFLECTION_NONE);
	}

	static uint8_t get_unresolved_reflection_kinds(
			const FSFunction *p_function) {
		return p_function != nullptr ? p_function->unresolved_reflection_kinds : uint8_t(FSFunction::REFLECTION_NONE);
	}

	static Variant get_static_variable(const Ref<FoundryScript> &p_script, const StringName &p_name) {
		const FoundryScript::MemberInfo *member_info = p_script->static_variables_indices.getptr(p_name);
		if (member_info == nullptr || member_info->index < 0 || member_info->index >= p_script->static_variables.size()) {
			return Variant();
		}
		return p_script->static_variables[member_info->index];
	}

	static bool has_ancestor_binding(const Ref<FoundryScript> &p_script, FoundryScript *p_ancestor) {
		return p_script->type_parameter_bindings_by_ancestor.has(p_ancestor);
	}

	static int get_ancestor_binding_kind(const Ref<FoundryScript> &p_script, FoundryScript *p_ancestor, int p_binding_index) {
		const Vector<FoundryScript::TypeArgumentBinding> *bindings = p_script->type_parameter_bindings_by_ancestor.getptr(p_ancestor);
		if (bindings == nullptr || p_binding_index < 0 || p_binding_index >= bindings->size()) {
			return -1;
		}
		return (int)(*bindings)[p_binding_index].kind;
	}

	static int get_member_binding_kind(const Ref<FoundryScript> &p_script, const StringName &p_member) {
		const FoundryScript::MemberInfo *member_info = p_script->member_indices.getptr(p_member);
		return member_info != nullptr ? (int)member_info->type_argument_binding.kind : -1;
	}

	static Vector<FSFunction *> get_witness_functions(const Ref<FoundryScript> &p_script) {
		return p_script->witness_functions;
	}

	static String get_registered_conformance_source(const Ref<FoundryScript> &p_script) {
		return p_script->registered_conformance_source;
	}

	static bool retains_witness_target(const Ref<FoundryScript> &p_script, const Ref<Script> &p_target) {
		return p_script->witness_target_scripts.has(p_target);
	}

	static void add_namespace_conformance_script(const Ref<FoundryScript> &p_script, const Ref<Script> &p_conformance) {
		p_script->namespace_conformance_scripts.push_back(p_conformance);
	}

	static bool retains_namespace_conformance_script(const Ref<FoundryScript> &p_script, const Ref<Script> &p_conformance) {
		return p_script->namespace_conformance_scripts.has(p_conformance);
	}

	static FSFunction *get_initializer(const Ref<FoundryScript> &p_script) {
		return p_script->initializer;
	}

	// Compares the full MemberInfo tables of two scripts (index, accessors, data type, property
	// info). Lives on the accessor because MemberInfo is private to FoundryScript.
	static void check_member_tables_match(const Ref<FoundryScript> &p_expected, const Ref<FoundryScript> &p_actual) {
		const HashMap<StringName, FoundryScript::MemberInfo> &expected_members = p_expected->member_indices;
		const HashMap<StringName, FoundryScript::MemberInfo> &actual_members = p_actual->member_indices;
		CHECK(actual_members.size() == expected_members.size());
		for (const KeyValue<StringName, FoundryScript::MemberInfo> &member : expected_members) {
			CAPTURE(String(member.key));
			REQUIRE(actual_members.has(member.key));
			const FoundryScript::MemberInfo &actual_member = actual_members[member.key];
			CHECK(actual_member.index == member.value.index);
			CHECK(actual_member.setter == member.value.setter);
			CHECK(actual_member.getter == member.value.getter);
			CHECK(actual_member.data_type == member.value.data_type);
			CHECK(actual_member.property_info.type == member.value.property_info.type);
			CHECK(actual_member.property_info.name == member.value.property_info.name);
			CHECK(actual_member.property_info.hint == member.value.property_info.hint);
			CHECK(actual_member.property_info.hint_string == member.value.property_info.hint_string);
			CHECK(actual_member.property_info.usage == member.value.property_info.usage);
		}
	}
};

class BytecodeConformanceRegistryRestore {
	String source;
	Vector<FSConformanceRegistry::Conformance> parse_entries;
	Vector<FSConformanceRegistry::RuntimeConformance> runtime_entries;

public:
	explicit BytecodeConformanceRegistryRestore(const String &p_source) :
			source(p_source),
			parse_entries(FSConformanceRegistry::get_singleton()
							->get_file_conformances(p_source)),
			runtime_entries(FSConformanceRegistry::get_singleton()
							->get_runtime_witnesses(p_source)) {}

	~BytecodeConformanceRegistryRestore() {
		FSConformanceRegistry::get_singleton()->register_file_conformances(
				source, parse_entries);
		FSConformanceRegistry::get_singleton()->register_runtime_witnesses(
				source, runtime_entries);
	}
};

// Serializes the compiled script, then rebuilds it from the bytes onto a fresh FoundryScript with
// the same path (the production loader keeps the original resource path when a `.fs` is remapped to
// its `.fsb`) through load_skeleton followed by load_full.
static Ref<FoundryScript> bytecode_round_trip_script(const Ref<FoundryScript> &p_original,
		FSBytecodeExternalResolver *p_resolver, Vector<uint8_t> *r_buffer = nullptr) {
	FSBytecodeExporter exporter;
	Vector<uint8_t> buffer;
	REQUIRE(exporter.serialize(p_original, buffer) == OK);
	if (r_buffer != nullptr) {
		*r_buffer = buffer;
	}

	Ref<FoundryScript> restored;
	restored.instantiate();
	restored->set_path_cache(p_original->get_script_path());

	FSBytecodeLoader loader;
	loader.set_resolver(p_resolver);
	REQUIRE(loader.load_skeleton(buffer, restored) == OK);
	CHECK(!restored->is_valid());
	REQUIRE(loader.load_full(buffer, restored) == OK);
	CHECK(restored->is_valid());
	CHECK(restored->is_compiled_binary());
	return restored;
}

// A script whose signatures mention a builtin type (`JsonResult`, `JsonNode`, ...) encodes those
// references by (path, fully qualified name); production loading resolves them through the cache, so
// tests that round-trip such a script hand the resolver the same scripts.
static void register_builtin_scripts_for_bytecode_resolver(BytecodeTestResolver &r_resolver) {
	List<String> builtin_paths;
	FSBuiltinSources::get_registered_paths(&builtin_paths);
	for (const String &builtin_path : builtin_paths) {
		Error builtin_error = OK;
		const Ref<FoundryScript> builtin_script = FSCache::get_full_script(builtin_path, builtin_error);
		if (builtin_script.is_valid()) {
			r_resolver.scripts[builtin_path + "::" + builtin_script->get_fully_qualified_name()] = builtin_script;
		}
	}
}

static Variant bytecode_instance_call(Object *p_object, const StringName &p_method, const Vector<Variant> &p_arguments) {
	constexpr int MAX_TEST_ARGUMENTS = 8;
	REQUIRE(p_arguments.size() <= MAX_TEST_ARGUMENTS);
	const Variant *argument_pointers[MAX_TEST_ARGUMENTS] = {};
	for (int i = 0; i < p_arguments.size(); i++) {
		argument_pointers[i] = &p_arguments[i];
	}
	Callable::CallError call_error;
	const Variant result = p_object->callp(p_method, argument_pointers, p_arguments.size(), call_error);
	CHECK(call_error.error == Callable::CallError::CALL_OK);
	return result;
}

static Variant bytecode_new_instance(const Ref<FoundryScript> &p_script) {
	Callable::CallError call_error;
	const Variant instance = p_script->_new(nullptr, 0, call_error);
	REQUIRE(call_error.error == Callable::CallError::CALL_OK);
	REQUIRE(instance.get_type() == Variant::OBJECT);
	return instance;
}

TEST_CASE("[FoundryScript][BytecodeScript] Members, signals, constants, annotations, and rpc round-trip") {
	const Ref<FoundryScript> original = compile_bytecode_test_source(
			"@keep_name\n"
			"extends RefCounted\n"
			"\n"
			"signal health_changed(amount: int)\n"
			"@keep_name signal damaged\n"
			"\n"
			"@keep_name enum Tint:\n"
			"\tRED = 0\n"
			"\tGREEN = 5\n"
			"\t@keep_name func kept_enum_instance() -> void:\n"
			"\t\tpass\n"
			"\t@keep_name func shared_enum_escape() -> void:\n"
			"\t\tpass\n"
			"\n"
			"@keep_name enum Shade:\n"
			"\tLIGHT = 0\n"
			"\tDARK = 1\n"
			"\t@keep_name static func kept_enum_static() -> void:\n"
			"\t\tpass\n"
			"\t@keep_name static func shared_enum_escape() -> void:\n"
			"\t\tpass\n"
			"\n"
			"@keep_name const GREETING = \"hello\"\n"
			"\n"
			"@export var speed: float = 2.5\n"
			"@keep_name var health: int = 10\n"
			"\n"
			"func _init() -> void:\n"
			"\thealth = 12\n"
			"\n"
			"@keep_name func take_damage(amount: int) -> int:\n"
			"\thealth -= amount\n"
			"\thealth_changed.emit(health)\n"
			"\tdamaged.emit()\n"
			"\treturn health\n"
			"\n"
			"func describe() -> String:\n"
			"\treturn GREETING + \":\" + str(speed)\n");

	BytecodeTestResolver resolver;
	const Ref<FoundryScript> restored = bytecode_round_trip_script(original, &resolver);

	// Full MemberInfo equality across the member table.
	TestFSBytecodeScriptAccessor::check_member_tables_match(original, restored);
	CHECK(restored->get_members().size() == original->get_members().size());
	for (const StringName &member_name : original->get_members()) {
		CHECK(restored->get_members().has(member_name));
	}

	// Signals.
	CHECK(restored->has_script_signal(SNAME("health_changed")));
	CHECK(restored->has_script_signal(SNAME("damaged")));
	CHECK(restored->get_signals().size() == original->get_signals().size());
	const MethodInfo &restored_signal = restored->get_signals()[SNAME("health_changed")];
	REQUIRE(restored_signal.arguments.size() == 1);
	CHECK(restored_signal.arguments[0].name == "amount");
	CHECK(restored_signal.arguments[0].type == Variant::INT);

	// Constants, including the enum dictionary.
	const HashMap<StringName, Variant> &restored_constants = restored->get_constants();
	CHECK(restored_constants.size() == original->get_constants().size());
	REQUIRE(restored_constants.has(SNAME("GREETING")));
	CHECK(String(restored_constants[SNAME("GREETING")]) == "hello");
	REQUIRE(restored_constants.has(SNAME("Tint")));
	const Dictionary tint_dictionary = restored_constants[SNAME("Tint")];
	CHECK((int64_t)tint_dictionary[StringName("RED")] == 0);
	CHECK((int64_t)tint_dictionary[StringName("GREEN")] == 5);

	// Annotation usage maps.
	CHECK(restored->get_class_annotations().size() == original->get_class_annotations().size());
	CHECK(restored->get_variable_annotations().size() == original->get_variable_annotations().size());
	CHECK(restored->get_method_annotations().size() == original->get_method_annotations().size());
	CHECK(restored->get_signal_annotations().size() == original->get_signal_annotations().size());
	CHECK(restored->get_constant_annotations().size() == original->get_constant_annotations().size());
	REQUIRE(restored->get_variable_annotations().has(SNAME("speed")));
	const Vector<FoundryScript::AnnotationUsage> &speed_annotations = restored->get_variable_annotations()[SNAME("speed")];
	REQUIRE(speed_annotations.size() == original->get_variable_annotations()[SNAME("speed")].size());
	CHECK(speed_annotations[0].name == SNAME("export"));
	CHECK(speed_annotations[0].is_builtin);
	const auto check_restored_keep_name = [](const Vector<FoundryScript::AnnotationUsage> *p_usages) {
		REQUIRE(p_usages != nullptr);
		REQUIRE_EQ(p_usages->size(), 1);
		CHECK_EQ((*p_usages)[0].name, SNAME("keep_name"));
		CHECK((*p_usages)[0].is_builtin);
	};
	check_restored_keep_name(&restored->get_class_annotations());
	check_restored_keep_name(restored->get_variable_annotations().getptr(SNAME("health")));
	check_restored_keep_name(restored->get_method_annotations().getptr(SNAME("take_damage")));
	check_restored_keep_name(restored->get_signal_annotations().getptr(SNAME("damaged")));
	check_restored_keep_name(restored->get_constant_annotations().getptr(SNAME("GREETING")));
	check_restored_keep_name(restored->get_constant_annotations().getptr(SNAME("Tint")));
	check_restored_keep_name(restored->get_constant_annotations().getptr(SNAME("Shade")));
	check_restored_keep_name(restored->get_method_annotations().getptr(SNAME("kept_enum_instance")));
	check_restored_keep_name(restored->get_method_annotations().getptr(SNAME("kept_enum_static")));
	check_restored_keep_name(restored->get_method_annotations().getptr(SNAME("shared_enum_escape")));

	// Method reflection.
	CHECK(restored->has_method(SNAME("take_damage")));
	CHECK(restored->has_method(SNAME("describe")));
	CHECK(!restored->has_method(SNAME("missing_method")));
	List<MethodInfo> method_list;
	restored->get_script_method_list(&method_list);
	bool method_list_has_take_damage = false;
	for (const MethodInfo &method : method_list) {
		if (method.name == "take_damage") {
			method_list_has_take_damage = true;
		}
	}
	CHECK(method_list_has_take_damage);

	// The flagged initializer is the registered `_init` member function.
	REQUIRE(restored->get_member_functions().has(SNAME("_init")));
	CHECK(TestFSBytecodeScriptAccessor::get_initializer(restored) == restored->get_member_functions()[SNAME("_init")]);

	// Instance behavior: defaults, constructor, method calls, signal emission, property set/get.
	const Variant instance_variant = bytecode_new_instance(restored);
	Object *instance = instance_variant;
	CHECK((int64_t)instance->get(SNAME("health")) == 12);
	CHECK((double)instance->get(SNAME("speed")) == 2.5);
	// The signal watcher derives its callback arity from ClassDB, so it can only observe a script
	// signal without arguments; the zero-argument `damaged` covers connect/emit end to end.
	SIGNAL_WATCH(instance, SNAME("damaged"));
	const Variant damage_result = bytecode_instance_call(instance, SNAME("take_damage"), { 3 });
	CHECK((int64_t)damage_result == 9);
	Array emissions;
	emissions.push_back(Array());
	SIGNAL_CHECK(SNAME("damaged"), emissions);
	SIGNAL_UNWATCH(instance, SNAME("damaged"));
	CHECK(String(bytecode_instance_call(instance, SNAME("describe"), {})) == "hello:2.5");
	instance->set(SNAME("health"), 55);
	CHECK((int64_t)instance->get(SNAME("health")) == 55);
}

TEST_CASE("[FoundryScript][BytecodeScript] Inner classes round-trip as intra-file class references") {
	const Ref<FoundryScript> original = compile_bytecode_test_source(
			"class Inner:\n"
			"\tvar value: int = 3\n"
			"\tfunc double() -> int:\n"
			"\t\treturn value * 2\n"
			"\n"
			"class Wrapper:\n"
			"\tclass Deep:\n"
			"\t\tfunc word() -> String:\n"
			"\t\t\treturn \"deep\"\n"
			"\n"
			"func make_inner() -> Inner:\n"
			"\treturn Inner.new()\n"
			"\n"
			"func inner_value() -> int:\n"
			"\treturn Inner.new().double()\n"
			"\n"
			"func deep_word() -> String:\n"
			"\treturn Wrapper.Deep.new().word()\n");

	FSBytecodeExporter exporter;
	Vector<uint8_t> buffer;
	REQUIRE(exporter.serialize(original, buffer) == OK);
	Vector<uint8_t> repeated_buffer;
	REQUIRE(exporter.serialize(original, repeated_buffer) == OK);
	CHECK_EQ(repeated_buffer, buffer);

	// The skeleton alone reproduces the class tree and identities without a resolver and without
	// making the script valid.
	{
		Ref<FoundryScript> skeleton;
		skeleton.instantiate();
		skeleton->set_path_cache(original->get_script_path());
		FSBytecodeLoader skeleton_loader;
		REQUIRE(skeleton_loader.load_skeleton(buffer, skeleton) == OK);
		CHECK(!skeleton->is_valid());
		CHECK(skeleton->is_compiled_binary());
		REQUIRE(skeleton->get_subclasses().has(SNAME("Inner")));
		REQUIRE(skeleton->get_subclasses().has(SNAME("Wrapper")));
		const Ref<FoundryScript> skeleton_inner = skeleton->get_subclasses().find(SNAME("Inner"))->value;
		const Ref<FoundryScript> original_inner = original->get_subclasses().find(SNAME("Inner"))->value;
		CHECK(skeleton_inner->get_fully_qualified_name() == original_inner->get_fully_qualified_name());
		CHECK(skeleton_inner->get_local_name() == original_inner->get_local_name());
		const Ref<FoundryScript> skeleton_wrapper = skeleton->get_subclasses().find(SNAME("Wrapper"))->value;
		CHECK(skeleton_wrapper->get_subclasses().has(SNAME("Deep")));
	}

	BytecodeTestResolver resolver;
	const Ref<FoundryScript> restored = bytecode_round_trip_script(original, &resolver);

	// Every class reference in this file is intra-file, so the resolver is never consulted.
	CHECK(resolver.script_requests.is_empty());
	CHECK(resolver.resource_requests.is_empty());

	// The subclass constants are the loaded file's own class objects.
	REQUIRE(restored->get_subclasses().has(SNAME("Inner")));
	const Ref<FoundryScript> restored_inner = restored->get_subclasses().find(SNAME("Inner"))->value;
	CHECK(restored_inner->is_valid());
	const HashMap<StringName, Variant> &restored_constants = restored->get_constants();
	REQUIRE(restored_constants.has(SNAME("Inner")));
	const Ref<FoundryScript> inner_constant = restored_constants[SNAME("Inner")];
	CHECK(inner_constant.ptr() == restored_inner.ptr());
	const Ref<FoundryScript> original_inner = original->get_subclasses().find(SNAME("Inner"))->value;
	CHECK(inner_constant.ptr() != original_inner.ptr());

	// `Outer.Inner` class constants resolve from restored methods.
	const Variant instance_variant = bytecode_new_instance(restored);
	Object *instance = instance_variant;
	CHECK((int64_t)bytecode_instance_call(instance, SNAME("inner_value"), {}) == 6);
	CHECK(String(bytecode_instance_call(instance, SNAME("deep_word"), {})) == "deep");
	const Variant made_inner = bytecode_instance_call(instance, SNAME("make_inner"), {});
	Object *made_inner_object = made_inner;
	REQUIRE(made_inner_object != nullptr);
	CHECK(Ref<FoundryScript>(made_inner_object->get_script()).ptr() == restored_inner.ptr());
}

TEST_CASE("[FoundryScript][BytecodeScript] External bases and preload constants resolve through the resolver") {
	const String base_path = TestUtils::get_temp_path("bytecode_script_base.fs");
	{
		Ref<FileAccess> base_file = FileAccess::open(base_path, FileAccess::WRITE);
		REQUIRE(base_file.is_valid());
		base_file->store_string(
				"var base_member: int = 4\n"
				"func base_value() -> int:\n"
				"\treturn 11\n"
				"func overridden() -> String:\n"
				"\treturn \"base\"\n");
	}
	const String resource_path = TestUtils::get_temp_path("bytecode_script_data.tres");
	{
		Ref<Resource> preloaded_resource;
		preloaded_resource.instantiate();
		REQUIRE(ResourceSaver::save(preloaded_resource, resource_path) == OK);
	}

	const Ref<FoundryScript> original = compile_bytecode_test_source(vformat(
			"extends \"%s\"\n"
			"\n"
			"const DATA = preload(\"%s\")\n"
			"\n"
			"func overridden() -> String:\n"
			"\treturn \"child\"\n"
			"\n"
			"func child_total() -> int:\n"
			"\treturn base_value() + 1\n",
			base_path, resource_path));

	const Ref<FoundryScript> base_script = original->get_base();
	REQUIRE(base_script.is_valid());
	REQUIRE(base_script->is_valid());

	BytecodeTestResolver resolver;
	resolver.scripts.insert(base_script->get_script_path() + "::" + base_script->get_fully_qualified_name(), base_script);
	Ref<Resource> replacement_resource;
	replacement_resource.instantiate();
	replacement_resource->set_path_cache(resource_path);
	resolver.resources.insert(resource_path, replacement_resource);

	Vector<uint8_t> buffer;
	const Ref<FoundryScript> restored = bytecode_round_trip_script(original, &resolver, &buffer);

	// The base resolved through the resolver interface, and the dependency section names both
	// external paths without touching class data.
	CHECK(restored->get_base() == base_script);
	CHECK(restored->inherits_script(base_script));
	Vector<String> dependencies;
	FSBytecodeLoader dependency_loader;
	REQUIRE(dependency_loader.read_dependencies(buffer, dependencies) == OK);
	CHECK(dependencies.has(base_path));
	CHECK(dependencies.has(resource_path));

	// The preload constant is the resolver-provided resource, by identity.
	const Ref<Resource> data_constant = restored->get_constants()[SNAME("DATA")];
	CHECK(data_constant == replacement_resource);

	// Inherited members and methods work through real instances of the restored child.
	CHECK(restored->debug_get_member_indices().has(SNAME("base_member")));
	const Variant instance_variant = bytecode_new_instance(restored);
	Object *instance = instance_variant;
	CHECK(String(bytecode_instance_call(instance, SNAME("overridden"), {})) == "child");
	CHECK((int64_t)bytecode_instance_call(instance, SNAME("base_value"), {}) == 11);
	CHECK((int64_t)bytecode_instance_call(instance, SNAME("child_total"), {}) == 12);
	instance->set(SNAME("base_member"), 9);
	CHECK((int64_t)instance->get(SNAME("base_member")) == 9);

	// An unresolvable base is a clean hard error naming the script.
	{
		Ref<FoundryScript> unresolved;
		unresolved.instantiate();
		unresolved->set_path_cache(original->get_script_path());
		BytecodeTestResolver empty_resolver;
		FSBytecodeLoader failing_loader;
		failing_loader.set_resolver(&empty_resolver);
		ERR_PRINT_OFF;
		CHECK(failing_loader.load_full(buffer, unresolved) == ERR_CANT_RESOLVE);
		ERR_PRINT_ON;
		CHECK(!unresolved->is_valid());
	}

	// Re-entering the loader that is mid-load (a resolver reusing its invoking loader instead of a
	// fresh one) is rejected with ERR_BUSY without corrupting the outer load.
	{
		struct ReentrantBytecodeResolver : public BytecodeTestResolver {
			FSBytecodeLoader *active_loader = nullptr;
			const Vector<uint8_t> *reentrant_buffer = nullptr;
			Ref<FoundryScript> nested_target;
			Error nested_result = OK;
			bool attempted = false;

			virtual Ref<Script> resolve_script(const String &p_path, const String &p_fully_qualified_name,
					bool &r_is_local_class) override {
				if (!attempted && active_loader != nullptr) {
					attempted = true;
					ERR_PRINT_OFF;
					nested_result = active_loader->load_full(*reentrant_buffer, nested_target);
					ERR_PRINT_ON;
				}
				return BytecodeTestResolver::resolve_script(p_path, p_fully_qualified_name, r_is_local_class);
			}
		};

		ReentrantBytecodeResolver reentrant_resolver;
		reentrant_resolver.scripts = resolver.scripts;
		reentrant_resolver.resources = resolver.resources;
		reentrant_resolver.nested_target.instantiate();
		reentrant_resolver.nested_target->set_path_cache(original->get_script_path());
		Ref<FoundryScript> outer;
		outer.instantiate();
		outer->set_path_cache(original->get_script_path());
		FSBytecodeLoader reentrant_loader;
		reentrant_loader.set_resolver(&reentrant_resolver);
		reentrant_resolver.active_loader = &reentrant_loader;
		reentrant_resolver.reentrant_buffer = &buffer;
		REQUIRE(reentrant_loader.load_full(buffer, outer) == OK);
		CHECK(reentrant_resolver.attempted);
		CHECK(reentrant_resolver.nested_result == ERR_BUSY);
		CHECK(!reentrant_resolver.nested_target->is_valid());
		CHECK(outer->is_valid());
		const Variant outer_instance_variant = bytecode_new_instance(outer);
		Object *outer_instance = outer_instance_variant;
		CHECK((int64_t)bytecode_instance_call(outer_instance, SNAME("child_total"), {}) == 12);
	}

	DirAccess::remove_absolute(base_path);
	DirAccess::remove_absolute(resource_path);
}

TEST_CASE("[FoundryScript][BytecodeScript] Static variables, implicit initializers, and onready round-trip") {
	const Ref<FoundryScript> original = compile_bytecode_test_source(
			"extends Node\n"
			"\n"
			"static var counter: int = 7\n"
			"\n"
			"@onready var late := 1\n"
			"var plain: int = 3\n"
			"\n"
			"static func bump() -> int:\n"
			"\tcounter += 1\n"
			"\treturn counter\n"
			"\n"
			"@rpc(\"any_peer\")\n"
			"func sync_state(value: int) -> void:\n"
			"\tplain = value\n");

	// The script flags carry the static-data facts for the load-path integration.
	{
		FSBytecodeExporter flag_exporter;
		Vector<uint8_t> flag_buffer;
		REQUIRE(flag_exporter.serialize(original, flag_buffer, true) == OK);
		FSBytecodeLoader flag_loader;
		Vector<String> dependencies;
		REQUIRE(flag_loader.read_dependencies(flag_buffer, dependencies) == OK);
		CHECK(flag_loader.get_has_static_data());
		CHECK(flag_loader.get_annotated_static_unload());
	}

	BytecodeTestResolver resolver;
	const Ref<FoundryScript> restored = bytecode_round_trip_script(original, &resolver);

	CHECK(restored->get_implicit_initializer() != nullptr);
	CHECK(restored->get_implicit_ready() != nullptr);
	CHECK(restored->get_static_initializer() != nullptr);

	// The merged rpc configuration is taken verbatim.
	CHECK(Variant(restored->get_rpc_config()).hash_compare(Variant(original->get_rpc_config())));
	const Dictionary restored_rpc = restored->get_rpc_config();
	CHECK(restored_rpc.has(StringName("sync_state")));
	REQUIRE(restored->get_method_annotations().has(SNAME("sync_state")));
	CHECK(restored->get_method_annotations()[SNAME("sync_state")][0].name == SNAME("rpc"));

	// Load ran the reload() tail: static defaults, then valid, then the static initializer — so the
	// restored script's static variable already holds its initializer value.
	CHECK((int64_t)TestFSBytecodeScriptAccessor::get_static_variable(restored, SNAME("counter")) == 7);

	// Static methods dispatch on the restored script and mutate its own static storage.
	const Variant bumped = bytecode_instance_call(restored.ptr(), SNAME("bump"), {});
	CHECK((int64_t)bumped == 8);
	CHECK((int64_t)TestFSBytecodeScriptAccessor::get_static_variable(restored, SNAME("counter")) == 8);

	// Instantiation applies member defaults through the implicit initializer.
	Node *node = memnew(Node);
	node->set_script(restored);
	CHECK((int64_t)node->get(SNAME("plain")) == 3);
	memdelete(node);

	// Static-variable opcodes bake a self-reference into the constant pools (the class operand), a
	// cycle production breaks in FSLanguage::finish(); break it here so the fixture scripts do not
	// outlive other suites when this case runs before the harness teardown.
	restored->clear();
	original->clear();
}

TEST_CASE("[FoundryScript][BytecodeScript] Generic type parameter bindings re-key onto the loaded scripts") {
	const Ref<FoundryScript> original = compile_bytecode_test_source(
			"class Box[T]:\n"
			"\tvar value: T\n"
			"\n"
			"class IntBox extends Box[int]:\n"
			"\tpass\n");

	BytecodeTestResolver resolver;
	const Ref<FoundryScript> restored = bytecode_round_trip_script(original, &resolver);

	REQUIRE(restored->get_subclasses().has(SNAME("Box")));
	REQUIRE(restored->get_subclasses().has(SNAME("IntBox")));
	const Ref<FoundryScript> restored_box = restored->get_subclasses().find(SNAME("Box"))->value;
	const Ref<FoundryScript> restored_int_box = restored->get_subclasses().find(SNAME("IntBox"))->value;
	const Ref<FoundryScript> original_box = original->get_subclasses().find(SNAME("Box"))->value;

	CHECK(restored_box->is_generic());
	REQUIRE(restored_box->get_type_parameters().size() == 1);
	CHECK(restored_box->get_type_parameters()[0].name == SNAME("T"));
	CHECK(restored_box->get_type_parameters()[0].index == 0);

	// The per-ancestor binding tables are keyed by the loaded scripts, not the serialized ones.
	CHECK(TestFSBytecodeScriptAccessor::has_ancestor_binding(restored_box, restored_box.ptr()));
	CHECK(TestFSBytecodeScriptAccessor::has_ancestor_binding(restored_int_box, restored_box.ptr()));
	CHECK(!TestFSBytecodeScriptAccessor::has_ancestor_binding(restored_int_box, original_box.ptr()));

	// Box's own parameter stays OPEN at ordinal 0; IntBox's `extends Box[int]` fixes it.
	CHECK(TestFSBytecodeScriptAccessor::get_ancestor_binding_kind(restored_box, restored_box.ptr(), 0) == 2);
	CHECK(TestFSBytecodeScriptAccessor::get_ancestor_binding_kind(restored_int_box, restored_box.ptr(), 0) == 1);

	// The `value: T` member slot binding follows the same specialization.
	CHECK(TestFSBytecodeScriptAccessor::get_member_binding_kind(restored_box, SNAME("value")) == 2);
	CHECK(TestFSBytecodeScriptAccessor::get_member_binding_kind(restored_int_box, SNAME("value")) == 1);
}

TEST_CASE("[FoundryScript][BytecodeScript] Namespace conformance load edges survive serialization") {
	// A conformance file reached through a namespace is the one script a compiled file references
	// nowhere: no constant, no type, no base. Only the declaring script's own compilation registers
	// its witnesses, so if the edge did not survive serialization an exported game would type-check
	// against a conformance whose witnesses never get registered.
	const Ref<FoundryScript> conformance_library = compile_bytecode_test_source(
			"class Marker:\n"
			"\tpass\n");
	// The consumer declares a conformance of its own too, so the failure path below can show that a
	// script which fails to load stops supplying witnesses to the rest of the process.
	const Ref<FoundryScript> original = compile_bytecode_test_source(
			"trait Pingable:\n"
			"\tabstract func ping() -> int\n"
			"\n"
			"class Gadget:\n"
			"\tvar power: int = 21\n"
			"\n"
			"extend Gadget uses Pingable:\n"
			"\tfunc ping() -> int:\n"
			"\t\treturn power * 2\n"
			"\n"
			"func run() -> int:\n"
			"\treturn 1\n");
	const String consumer_path = original->get_script_path();
	const String gadget_key = original->get_subclasses().find(SNAME("Gadget"))->value->get_fully_qualified_name();
	BytecodeConformanceRegistryRestore registry_restore(consumer_path);
	TestFSBytecodeScriptAccessor::add_namespace_conformance_script(original, conformance_library);

	FSBytecodeExporter exporter;
	Vector<uint8_t> buffer;
	REQUIRE(exporter.serialize(original, buffer) == OK);

	// The library must also be packaged, or the exported game would ship a dangling path.
	Vector<String> dependencies;
	FSBytecodeLoader dependency_reader;
	REQUIRE(dependency_reader.read_dependencies(buffer, dependencies) == OK);
	CHECK(dependencies.has(conformance_library->get_path()));

	Ref<FoundryScript> restored;
	restored.instantiate();
	restored->set_path_cache(original->get_script_path());
	BytecodeTestResolver resolver;
	resolver.scripts[conformance_library->get_path() + "::"] = conformance_library;
	FSBytecodeLoader loader;
	loader.set_resolver(&resolver);
	REQUIRE(loader.load_full(buffer, restored) == OK);

	CHECK(TestFSBytecodeScriptAccessor::retains_namespace_conformance_script(restored, conformance_library));

	// A library that cannot be resolved fails the load rather than producing a consumer whose
	// conformance witnesses are never registered.
	Ref<FoundryScript> restored_without_library;
	restored_without_library.instantiate();
	restored_without_library->set_path_cache(original->get_script_path());
	BytecodeTestResolver empty_resolver;
	FSBytecodeLoader failing_loader;
	failing_loader.set_resolver(&empty_resolver);
	ERR_PRINT_OFF;
	CHECK(failing_loader.load_full(buffer, restored_without_library) != OK);
	ERR_PRINT_ON;
	// A caller that only checks `is_valid()` — the resource loader among them — must not be handed
	// this script despite the error, nor may the witnesses decoded earlier in that same load stay
	// registered for the rest of the process to dispatch through.
	CHECK_FALSE(restored_without_library->is_valid());
	CHECK(FSConformanceRegistry::get_singleton()->find_witness_function(gadget_key, SNAME("ping")) == nullptr);
}

TEST_CASE("[FoundryScript][BytecodeScript] Conformance witnesses re-register with the registry") {
	const Ref<FoundryScript> original = compile_bytecode_test_source(
			"trait Pingable:\n"
			"\tabstract func ping() -> int\n"
			"\n"
			"trait Trackable:\n"
			"\tabstract func ping() -> int\n"
			"\n"
			"class Gadget:\n"
			"\tvar power: int = 21\n"
			"\n"
			"extend Gadget uses Pingable, Trackable:\n"
			"\tfunc ping() -> int:\n"
			"\t\treturn power * 2\n"
			"\n"
			"class Speaker uses Pingable:\n"
			"\tfunc ping() -> int:\n"
			"\t\treturn 5\n"
			"\n"
			"func run() -> int:\n"
			"\tvar gadget := Gadget.new()\n"
			"\tvar pingable: Pingable = gadget\n"
			"\tvar widened: Object = gadget\n"
			"\tif not widened is Pingable or not widened is Trackable:\n"
			"\t\treturn -1\n"
			"\tvar trackable := widened as Trackable\n"
			"\treturn pingable.ping() + trackable.ping() + "
			"Speaker.new().ping()\n");
	const String script_path = original->get_script_path();
	const Ref<FoundryScript> original_gadget = original->get_subclasses().find(SNAME("Gadget"))->value;
	const String gadget_key = original_gadget->get_fully_qualified_name();
	const Ref<FoundryScript> original_pingable =
			original->get_subclasses().find(SNAME("Pingable"))->value;
	const Ref<FoundryScript> original_trackable =
			original->get_subclasses().find(SNAME("Trackable"))->value;
	const StringName pingable_trait =
			original_pingable->get_trait_type_name();
	const StringName trackable_trait =
			original_trackable->get_trait_type_name();
	BytecodeConformanceRegistryRestore registry_restore(script_path);
	const Vector<FSConformanceRegistry::RuntimeConformance>
			compiled_conformances =
					FSConformanceRegistry::get_singleton()
							->get_runtime_witnesses(script_path);
	REQUIRE_EQ(compiled_conformances.size(), 2);
	CHECK_EQ(compiled_conformances[0].trait_name, pingable_trait);
	CHECK_EQ(compiled_conformances[1].trait_name, trackable_trait);
	CHECK_EQ(compiled_conformances[0].target_script, original_gadget.ptr());
	CHECK_EQ(compiled_conformances[1].target_script, original_gadget.ptr());

	// Sanity: the compiled fixture dispatches through the registry.
	{
		const Variant instance_variant = bytecode_new_instance(original);
		Object *instance = instance_variant;
		CHECK((int64_t)bytecode_instance_call(instance, SNAME("run"), {}) == 89);
	}

	FSBytecodeExporter exporter;
	Vector<uint8_t> buffer;
	REQUIRE(exporter.serialize(original, buffer) == OK);

	// Drop both compile-time registrations so membership and dispatch below can only come from the
	// ordinary, non-Transaction bytecode buffer and the loader's own re-registration.
	FSConformanceRegistry::get_singleton()->clear_file(script_path);
	FSConformanceRegistry::get_singleton()->clear_runtime_witnesses(script_path);
	CHECK(FSConformanceRegistry::get_singleton()->find_witness_function(gadget_key, SNAME("ping")) == nullptr);
	CHECK_FALSE(FSConformanceRegistry::get_singleton()->has_conformance(
			gadget_key, pingable_trait, true));
	CHECK_FALSE(FSConformanceRegistry::get_singleton()->has_conformance(
			gadget_key, trackable_trait, true));

	Ref<FoundryScript> restored;
	restored.instantiate();
	restored->set_path_cache(script_path);
	BytecodeTestResolver resolver;
	FSBytecodeLoader loader;
	loader.set_resolver(&resolver);
	REQUIRE(loader.load_full(buffer, restored) == OK);

	// The loader registered the restored script's own witnesses under its path.
	CHECK(TestFSBytecodeScriptAccessor::get_registered_conformance_source(restored) == script_path);
	FSFunction *registered_witness = FSConformanceRegistry::get_singleton()->find_witness_function(gadget_key, SNAME("ping"));
	REQUIRE(registered_witness != nullptr);
	CHECK(TestFSBytecodeScriptAccessor::get_witness_functions(restored).has(registered_witness));
	CHECK(!TestFSBytecodeScriptAccessor::get_witness_functions(original).has(registered_witness));

	// The trait identities survived on the loaded trait classes, the direct `uses` trait list
	// survived on the implementer, and runtime membership plus dispatch through the restored graph
	// come solely from the loaded witness section.
	REQUIRE(restored->get_subclasses().has(SNAME("Pingable")));
	CHECK(restored->get_subclasses().find(SNAME("Pingable"))->value->is_trait_type());
	REQUIRE(restored->get_subclasses().has(SNAME("Trackable")));
	CHECK(restored->get_subclasses().find(SNAME("Trackable"))->value->is_trait_type());
	REQUIRE(restored->get_subclasses().has(SNAME("Gadget")));
	const Ref<FoundryScript> restored_gadget =
			restored->get_subclasses().find(SNAME("Gadget"))->value;
	const Vector<FSConformanceRegistry::RuntimeConformance>
			loaded_conformances =
					FSConformanceRegistry::get_singleton()
							->get_runtime_witnesses(script_path);
	REQUIRE_EQ(loaded_conformances.size(), 2);
	CHECK_EQ(loaded_conformances[0].target_script, restored_gadget.ptr());
	CHECK_EQ(loaded_conformances[1].target_script, restored_gadget.ptr());
	CHECK_FALSE(restored_gadget->has_script_trait_parse(pingable_trait));
	CHECK_FALSE(restored_gadget->has_script_trait_parse(trackable_trait));
	CHECK(restored_gadget->has_script_trait(pingable_trait));
	CHECK(restored_gadget->has_script_trait(trackable_trait));
	REQUIRE(restored->get_subclasses().has(SNAME("Speaker")));
	const Ref<FoundryScript> original_speaker = original->get_subclasses().find(SNAME("Speaker"))->value;
	const Ref<FoundryScript> restored_speaker = restored->get_subclasses().find(SNAME("Speaker"))->value;
	List<StringName> original_speaker_traits;
	original_speaker->get_script_trait_list(&original_speaker_traits);
	REQUIRE(!original_speaker_traits.is_empty());
	for (const StringName &trait_name : original_speaker_traits) {
		CHECK(restored_speaker->has_script_trait(trait_name));
	}
	const Variant instance_variant = bytecode_new_instance(restored);
	Object *instance = instance_variant;
	CHECK((int64_t)bytecode_instance_call(instance, SNAME("run"), {}) == 89);
}

TEST_CASE("[FoundryScript][BytecodeScript] Marker conformances survive compiled-bytecode loading") {
	const Ref<FoundryScript> original = compile_bytecode_test_source(
			"trait Marker:\n"
			"\tpass\n"
			"\n"
			"class Target:\n"
			"\tpass\n"
			"\n"
			"extend Target uses Marker:\n"
			"\tpass\n"
			"\n"
			"func run() -> int:\n"
			"\tvar value: Variant = Target.new()\n"
			"\tif not value is Marker:\n"
			"\t\treturn 0\n"
			"\tvar typed := value as Marker\n"
			"\treturn 42 if typed != null else 1\n");
	const String script_path = original->get_script_path();
	const Ref<FoundryScript> original_target =
			original->get_subclasses().find(SNAME("Target"))->value;
	const Ref<FoundryScript> original_marker =
			original->get_subclasses().find(SNAME("Marker"))->value;
	const String target_key = original_target->get_fully_qualified_name();
	const StringName marker_trait = original_marker->get_trait_type_name();
	BytecodeConformanceRegistryRestore registry_restore(script_path);

	const Vector<FSConformanceRegistry::RuntimeConformance>
			compiled_conformances =
					FSConformanceRegistry::get_singleton()
							->get_runtime_witnesses(script_path);
	REQUIRE_EQ(compiled_conformances.size(), 1);
	CHECK_EQ(compiled_conformances[0].target_script, original_target.ptr());
	CHECK(compiled_conformances[0].target_keys.has(target_key));
	CHECK_EQ(compiled_conformances[0].trait_name, marker_trait);
	CHECK(compiled_conformances[0].functions.is_empty());

	const Variant original_instance_variant = bytecode_new_instance(original);
	Object *original_instance = original_instance_variant;
	CHECK((int64_t)bytecode_instance_call(original_instance, SNAME("run"), {}) == 42);

	FSBytecodeExporter exporter;
	Vector<uint8_t> buffer;
	REQUIRE(exporter.serialize(original, buffer) == OK);

	FSConformanceRegistry::get_singleton()->clear_file(script_path);
	FSConformanceRegistry::get_singleton()->clear_runtime_witnesses(script_path);
	CHECK_FALSE(FSConformanceRegistry::get_singleton()->has_conformance(
			target_key, marker_trait, true));

	Ref<FoundryScript> restored;
	restored.instantiate();
	restored->set_path_cache(script_path);
	BytecodeTestResolver resolver;
	FSBytecodeLoader loader;
	loader.set_resolver(&resolver);
	REQUIRE(loader.load_full(buffer, restored) == OK);

	REQUIRE(restored->get_subclasses().has(SNAME("Target")));
	REQUIRE(restored->get_subclasses().has(SNAME("Marker")));
	const Ref<FoundryScript> restored_target =
			restored->get_subclasses().find(SNAME("Target"))->value;
	const Ref<FoundryScript> restored_marker =
			restored->get_subclasses().find(SNAME("Marker"))->value;
	const Vector<FSConformanceRegistry::RuntimeConformance>
			loaded_conformances =
					FSConformanceRegistry::get_singleton()
							->get_runtime_witnesses(script_path);
	REQUIRE_EQ(loaded_conformances.size(), 1);
	CHECK_EQ(loaded_conformances[0].target_script, restored_target.ptr());
	CHECK(loaded_conformances[0].functions.is_empty());
	CHECK(FSConformanceRegistry::get_singleton()->has_conformance(
			restored_target->get_fully_qualified_name(),
			restored_marker->get_trait_type_name(), true));

	Vector<uint8_t> restored_buffer;
	REQUIRE(exporter.serialize(restored, restored_buffer) == OK);
	CHECK_EQ(restored_buffer, buffer);
	Vector<uint8_t> repeated_restored_buffer;
	REQUIRE(exporter.serialize(restored, repeated_restored_buffer) == OK);
	CHECK_EQ(repeated_restored_buffer, restored_buffer);

	const Variant restored_instance_variant = bytecode_new_instance(restored);
	Object *restored_instance = restored_instance_variant;
	CHECK((int64_t)bytecode_instance_call(restored_instance, SNAME("run"), {}) == 42);
	Vector<uint8_t> executed_restored_buffer;
	REQUIRE(exporter.serialize(restored, executed_restored_buffer) == OK);
	CHECK_EQ(executed_restored_buffer, buffer);
}

TEST_CASE("[FoundryScript][BytecodeScript] Marker conformances retain external targets") {
	const Ref<FoundryScript> external_target = compile_bytecode_test_source(
			"extends RefCounted\n");
	const String external_path = external_target->get_script_path();
	const Ref<FoundryScript> declaring = compile_bytecode_test_source(vformat(
			"const ExternalTarget = preload(\"%s\")\n"
			"\n"
			"trait ExternalMarker:\n"
			"\tpass\n"
			"\n"
			"extend ExternalTarget uses ExternalMarker:\n"
			"\tpass\n",
			external_path));
	const String declaring_path = declaring->get_script_path();
	BytecodeConformanceRegistryRestore registry_restore(declaring_path);

	const Vector<FSConformanceRegistry::RuntimeConformance>
			compiled_conformances =
					FSConformanceRegistry::get_singleton()
							->get_runtime_witnesses(declaring_path);
	REQUIRE_EQ(compiled_conformances.size(), 1);
	CHECK_EQ(compiled_conformances[0].target_script, external_target.ptr());
	CHECK(compiled_conformances[0].functions.is_empty());
	CHECK(TestFSBytecodeScriptAccessor::retains_witness_target(
			declaring, Ref<Script>(external_target)));

	FSBytecodeExporter exporter;
	Vector<uint8_t> buffer;
	REQUIRE(exporter.serialize(declaring, buffer) == OK);

	FSConformanceRegistry::get_singleton()->clear_file(declaring_path);
	FSConformanceRegistry::get_singleton()->clear_runtime_witnesses(declaring_path);
	Ref<FoundryScript> restored;
	restored.instantiate();
	restored->set_path_cache(declaring_path);
	BytecodeTestResolver resolver;
	resolver.scripts.insert(
			external_path + "::" + external_target->get_fully_qualified_name(),
			external_target);
	FSBytecodeLoader loader;
	loader.set_resolver(&resolver);
	REQUIRE(loader.load_full(buffer, restored) == OK);

	const Vector<FSConformanceRegistry::RuntimeConformance>
			loaded_conformances =
					FSConformanceRegistry::get_singleton()
							->get_runtime_witnesses(declaring_path);
	REQUIRE_EQ(loaded_conformances.size(), 1);
	CHECK_EQ(loaded_conformances[0].target_script, external_target.ptr());
	CHECK(loaded_conformances[0].functions.is_empty());
	CHECK(TestFSBytecodeScriptAccessor::retains_witness_target(
			restored, Ref<Script>(external_target)));
}

TEST_CASE("[FoundryScript][BytecodeScript] Native and builtin conformance stand-ins survive loading") {
	const Ref<FoundryScript> original = compile_bytecode_test_source(
			"trait Pingable:\n"
			"\tabstract func ping() -> int\n"
			"\n"
			"trait NativePingable:\n"
			"\tabstract func native_ping() -> int\n"
			"\n"
			"extend int uses Pingable:\n"
			"\tfunc ping() -> int:\n"
			"\t\treturn self + 1\n"
			"\n"
			"extend RefCounted uses NativePingable:\n"
			"\tfunc native_ping() -> int:\n"
			"\t\treturn 20\n"
			"\n"
			"func run() -> int:\n"
			"\tvar value: Pingable = 21\n"
			"\tvar native_value: NativePingable = RefCounted.new()\n"
			"\treturn value.ping() + native_value.native_ping()\n");
	const String script_path = original->get_script_path();
	const Ref<FoundryScript> original_pingable =
			original->get_subclasses().find(SNAME("Pingable"))->value;
	const Ref<FoundryScript> original_native_pingable =
			original->get_subclasses()
					.find(SNAME("NativePingable"))
					->value;
	const StringName pingable_trait =
			original_pingable->get_trait_type_name();
	const StringName native_pingable_trait =
			original_native_pingable->get_trait_type_name();
	BytecodeConformanceRegistryRestore registry_restore(script_path);
	const Vector<FSConformanceRegistry::RuntimeConformance>
			compiled_conformances =
					FSConformanceRegistry::get_singleton()
							->get_runtime_witnesses(script_path);
	REQUIRE_EQ(compiled_conformances.size(), 2);
	for (const FSConformanceRegistry::RuntimeConformance &conformance :
			compiled_conformances) {
		CHECK_EQ(conformance.target_script, original.ptr());
		for (const KeyValue<StringName, FSFunction *> &witness :
				conformance.functions) {
			REQUIRE(witness.value != nullptr);
			CHECK_EQ(witness.value->get_script(), original.ptr());
		}
	}

	{
		const Variant instance_variant = bytecode_new_instance(original);
		Object *instance = instance_variant;
		CHECK((int64_t)bytecode_instance_call(instance, SNAME("run"), {}) == 42);
	}

	FSBytecodeExporter exporter;
	Vector<uint8_t> buffer;
	REQUIRE(exporter.serialize(original, buffer) == OK);

	FSConformanceRegistry::get_singleton()->clear_file(script_path);
	FSConformanceRegistry::get_singleton()->clear_runtime_witnesses(script_path);
	CHECK(FSConformanceRegistry::get_singleton()->find_builtin_witness_function(Variant::INT, SNAME("ping")) == nullptr);
	CHECK(FSConformanceRegistry::get_singleton()
					->find_native_witness_function(
							SNAME("RefCounted"), SNAME("native_ping")) ==
			nullptr);
	CHECK_FALSE(
			FSConformanceRegistry::get_singleton()->builtin_type_conforms(
					Variant::INT, pingable_trait, true));
	CHECK_FALSE(
			FSConformanceRegistry::get_singleton()->native_class_conforms(
					SNAME("RefCounted"), native_pingable_trait, true));

	Ref<FoundryScript> restored;
	restored.instantiate();
	restored->set_path_cache(script_path);
	BytecodeTestResolver resolver;
	FSBytecodeLoader loader;
	loader.set_resolver(&resolver);
	REQUIRE(loader.load_full(buffer, restored) == OK);

	FSFunction *registered_witness = FSConformanceRegistry::get_singleton()->find_builtin_witness_function(Variant::INT, SNAME("ping"));
	REQUIRE(registered_witness != nullptr);
	CHECK(TestFSBytecodeScriptAccessor::get_witness_functions(restored).has(registered_witness));
	FSFunction *registered_native_witness =
			FSConformanceRegistry::get_singleton()
					->find_native_witness_function(
							SNAME("RefCounted"),
							SNAME("native_ping"));
	REQUIRE(registered_native_witness != nullptr);
	CHECK(TestFSBytecodeScriptAccessor::get_witness_functions(restored).has(
			registered_native_witness));
	const Vector<FSConformanceRegistry::RuntimeConformance>
			loaded_conformances =
					FSConformanceRegistry::get_singleton()
							->get_runtime_witnesses(script_path);
	REQUIRE_EQ(loaded_conformances.size(), 2);
	for (const FSConformanceRegistry::RuntimeConformance &conformance :
			loaded_conformances) {
		CHECK_EQ(conformance.target_script, restored.ptr());
		for (const KeyValue<StringName, FSFunction *> &witness :
				conformance.functions) {
			REQUIRE(witness.value != nullptr);
			CHECK_EQ(witness.value->get_script(), restored.ptr());
		}
	}
	CHECK_FALSE(
			FSConformanceRegistry::get_singleton()->builtin_type_conforms(
					Variant::INT, pingable_trait));
	CHECK(FSConformanceRegistry::get_singleton()->builtin_type_conforms(
			Variant::INT, pingable_trait, true));
	CHECK_FALSE(
			FSConformanceRegistry::get_singleton()->native_class_conforms(
					SNAME("RefCounted"), native_pingable_trait));
	CHECK(FSConformanceRegistry::get_singleton()->native_class_conforms(
			SNAME("RefCounted"), native_pingable_trait, true));

	const Variant instance_variant = bytecode_new_instance(restored);
	Object *instance = instance_variant;
	CHECK((int64_t)bytecode_instance_call(instance, SNAME("run"), {}) == 42);
}

TEST_CASE("[FoundryScript][BytecodeScript] A native witness returning a generic over Self exports") {
	// The stand-in ClassNode a native conformance analyzes through is never registered as a real
	// class, so it has no serializable Foundry Script identity. Lowered as an ordinary class it would
	// put a pathless script inside `JsonResult[Self]`, which compiled-bytecode export must reject;
	// lowered with the native target's semantics it travels as a plain native type argument.
	const Ref<FoundryScript> original = compile_bytecode_test_source(
			"trait Wrapping:\n"
			"\tabstract static func wrapped() -> JsonResult[Self]\n"
			"\n"
			"extend RefCounted uses Wrapping:\n"
			"\tstatic func wrapped() -> JsonResult[Self]:\n"
			"\t\treturn JsonResult[Self].fail(\"nope\", \"$\")\n"
			"\n"
			"func run() -> String:\n"
			"\treturn RefCounted.wrapped().error.message\n");
	const String script_path = original->get_script_path();
	const Ref<FoundryScript> original_wrapping =
			original->get_subclasses().find(SNAME("Wrapping"))->value;
	const StringName wrapping_trait = original_wrapping->get_trait_type_name();
	BytecodeConformanceRegistryRestore registry_restore(script_path);

	// Asserted on both the compiled and the restored witness: the `Self` argument is a native type
	// naming the conformance target, and carries no script reference to encode.
	const auto check_self_type_argument = [](const FSDataType &p_return_type) {
		REQUIRE_EQ(p_return_type.type_arguments.size(), 1);
		const FSDataType &self_argument = p_return_type.type_arguments[0];
		CHECK(self_argument.kind == FSDataType::NATIVE);
		CHECK(self_argument.builtin_type == Variant::OBJECT);
		CHECK(self_argument.native_type == SNAME("RefCounted"));
		CHECK(self_argument.is_self_type);
		CHECK(self_argument.script_type == nullptr);
		CHECK(self_argument.script_type_ref.is_null());
	};

	FSFunction *compiled_witness = nullptr;
	for (FSFunction *witness_function : TestFSBytecodeScriptAccessor::get_witness_functions(original)) {
		if (witness_function != nullptr && witness_function->get_name() == SNAME("wrapped")) {
			compiled_witness = witness_function;
		}
	}
	REQUIRE(compiled_witness != nullptr);
	check_self_type_argument(compiled_witness->get_return_type());

	{
		const Variant instance_variant = bytecode_new_instance(original);
		Object *instance = instance_variant;
		CHECK(String(bytecode_instance_call(instance, SNAME("run"), {})) == "nope");
	}

	FSBytecodeExporter exporter;
	Vector<uint8_t> buffer;
	REQUIRE(exporter.serialize(original, buffer) == OK);

	FSConformanceRegistry::get_singleton()->clear_file(script_path);
	FSConformanceRegistry::get_singleton()->clear_runtime_witnesses(script_path);
	CHECK(FSConformanceRegistry::get_singleton()
					->find_native_witness_function(SNAME("RefCounted"), SNAME("wrapped")) == nullptr);

	Ref<FoundryScript> restored;
	restored.instantiate();
	restored->set_path_cache(script_path);
	// The witness signature names the builtin `JsonResult`, so loading has to resolve it the way the
	// runtime resolver does.
	BytecodeTestResolver resolver;
	register_builtin_scripts_for_bytecode_resolver(resolver);
	FSBytecodeLoader loader;
	loader.set_resolver(&resolver);
	REQUIRE(loader.load_full(buffer, restored) == OK);

	FSFunction *restored_witness =
			FSConformanceRegistry::get_singleton()
					->find_native_witness_function(SNAME("RefCounted"), SNAME("wrapped"));
	REQUIRE(restored_witness != nullptr);
	CHECK(TestFSBytecodeScriptAccessor::get_witness_functions(restored).has(restored_witness));
	check_self_type_argument(restored_witness->get_return_type());
	CHECK(FSConformanceRegistry::get_singleton()->native_class_conforms(
			SNAME("RefCounted"), wrapping_trait, true));

	const Variant restored_instance_variant = bytecode_new_instance(restored);
	Object *restored_instance = restored_instance_variant;
	CHECK(String(bytecode_instance_call(restored_instance, SNAME("run"), {})) == "nope");
}

TEST_CASE("[FoundryScript][BytecodeScript] Script-level lambda metadata rebuilds") {
	const Ref<FoundryScript> original = compile_bytecode_test_source(
			"func lambda_total(base: int) -> int:\n"
			"\tvar adder := func(value: int) -> int:\n"
			"\t\treturn value + base\n"
			"\treturn int(adder.call(4))\n");
	BytecodeTestResolver resolver;
	const Ref<FoundryScript> restored = bytecode_round_trip_script(original, &resolver);

	// The restored script's lambda_info map is keyed by the restored lambda functions, with the
	// serialized capture metadata.
	REQUIRE(original->get_lambda_info().size() == 1);
	REQUIRE(restored->get_lambda_info().size() == 1);
	REQUIRE(original->get_member_functions().has(SNAME("lambda_total")));
	REQUIRE(restored->get_member_functions().has(SNAME("lambda_total")));
	const FSFunction *original_function = original->get_member_functions()[SNAME("lambda_total")];
	const FSFunction *restored_function = restored->get_member_functions()[SNAME("lambda_total")];
	REQUIRE(original_function->get_lambdas().size() == 1);
	REQUIRE(restored_function->get_lambdas().size() == 1);
	const FoundryScript::LambdaInfo *original_info =
			original->get_lambda_info().getptr(original_function->get_lambdas()[0]);
	const FoundryScript::LambdaInfo *restored_info =
			restored->get_lambda_info().getptr(restored_function->get_lambdas()[0]);
	REQUIRE(original_info != nullptr);
	REQUIRE(restored_info != nullptr);
	CHECK(restored_info->capture_count == original_info->capture_count);
	CHECK(restored_info->use_self == original_info->use_self);

	const Variant instance_variant = bytecode_new_instance(restored);
	Object *instance = instance_variant;
	CHECK((int64_t)bytecode_instance_call(instance, SNAME("lambda_total"), { 10 }) == 14);
}

TEST_CASE("[FoundryScript][NameMangler][Reflection][Bytecode] Receiver scope survives serialization") {
	const StringName owner_method =
			SNAME("reflection_bytecode_owner_method_1237");
	const StringName owner_property =
			SNAME("reflection_bytecode_owner_property_1237");
	const StringName owner_signal =
			SNAME("reflection_bytecode_owner_signal_1237");
	const StringName unrelated_method =
			SNAME("reflection_bytecode_unrelated_method_1237");
	const StringName unrelated_property =
			SNAME("reflection_bytecode_unrelated_property_1237");
	const StringName unrelated_signal =
			SNAME("reflection_bytecode_unrelated_signal_1237");
	const Ref<FoundryScript> original = compile_bytecode_test_source(
			"extends RefCounted\n"
			"var reflection_bytecode_owner_property_1237: int\n"
			"signal reflection_bytecode_owner_signal_1237\n"
			"func reflection_bytecode_owner_method_1237() -> void:\n"
			"\tpass\n"
			"func inspect_reflection_bytecode_1237() -> void:\n"
			"\tget_method_list()\n"
			"\tget_property_list()\n"
			"\tget_signal_list()\n");
	BytecodeTestResolver resolver;
	const Ref<FoundryScript> restored =
			bytecode_round_trip_script(original, &resolver);
	const Ref<FoundryScript> unrelated = compile_bytecode_test_source(
			"extends RefCounted\n"
			"var reflection_bytecode_unrelated_property_1237: int\n"
			"signal reflection_bytecode_unrelated_signal_1237\n"
			"func reflection_bytecode_unrelated_method_1237() -> void:\n"
			"\tpass\n");

	FSNameManglerAnalysis::Input source_input;
	source_input.scripts.push_back(original);
	source_input.scripts.push_back(unrelated);
	const FSNameManglerAnalysis::Result source_analysis =
			FSNameManglerAnalysis::analyze(source_input);
	FSNameManglerAnalysis::Input restored_input;
	restored_input.scripts.push_back(restored);
	restored_input.scripts.push_back(unrelated);
	const FSNameManglerAnalysis::Result restored_analysis =
			FSNameManglerAnalysis::analyze(restored_input);
	REQUIRE_EQ(source_analysis.error, OK);
	REQUIRE_EQ(restored_analysis.error, OK);
	REQUIRE_EQ(
			source_analysis.rename_map.size(),
			restored_analysis.rename_map.size());
	for (const KeyValue<StringName, StringName> &rename :
			source_analysis.rename_map) {
		REQUIRE(restored_analysis.rename_map.has(rename.key));
		CHECK_EQ(restored_analysis.rename_map[rename.key], rename.value);
	}

	const StringName owner_names[] = {
		owner_method,
		owner_property,
		owner_signal,
	};
	const StringName unrelated_names[] = {
		unrelated_method,
		unrelated_property,
		unrelated_signal,
	};
	for (int i = 0; i < 3; i++) {
		CAPTURE(owner_names[i]);
		CHECK_FALSE(source_analysis.rename_map.has(owner_names[i]));
		CHECK_FALSE(restored_analysis.rename_map.has(owner_names[i]));
		CAPTURE(unrelated_names[i]);
		CHECK(source_analysis.rename_map.has(unrelated_names[i]));
		CHECK(restored_analysis.rename_map.has(unrelated_names[i]));
	}
}

TEST_CASE("[FoundryScript][NameMangler][Reflection][Bytecode] Unresolved receiver scope survives serialization") {
	const StringName inspected_method =
			SNAME("reflection_unresolved_method_1237");
	const StringName inspected_property =
			SNAME("reflection_unresolved_property_1237");
	const StringName inspected_signal =
			SNAME("reflection_unresolved_signal_1237");
	const StringName inspect_function =
			SNAME("inspect_unresolved_reflection_1237");
	const Ref<FoundryScript> original = compile_bytecode_test_source(
			"extends RefCounted\n"
			"func inspect_unresolved_reflection_1237(target: Object) -> void:\n"
			"\ttarget.get_method_list()\n"
			"\ttarget.get_property_list()\n"
			"\ttarget.get_signal_list()\n");
	BytecodeTestResolver resolver;
	const Ref<FoundryScript> restored =
			bytecode_round_trip_script(original, &resolver);
	REQUIRE(original->get_member_functions().has(inspect_function));
	REQUIRE(restored->get_member_functions().has(inspect_function));
	const FSFunction *source_function =
			original->get_member_functions()[inspect_function];
	const FSFunction *restored_function =
			restored->get_member_functions()[inspect_function];
	const uint8_t expected_unresolved =
			FSFunction::REFLECTION_METHODS |
			FSFunction::REFLECTION_PROPERTIES |
			FSFunction::REFLECTION_SIGNALS;
	CHECK_EQ(
			TestFSBytecodeScriptAccessor::get_self_reflection_kinds(
					source_function),
			FSFunction::REFLECTION_NONE);
	CHECK_EQ(
			TestFSBytecodeScriptAccessor::get_self_reflection_kinds(
					restored_function),
			FSFunction::REFLECTION_NONE);
	CHECK_EQ(
			TestFSBytecodeScriptAccessor::get_unresolved_reflection_kinds(
					source_function),
			expected_unresolved);
	CHECK_EQ(
			TestFSBytecodeScriptAccessor::get_unresolved_reflection_kinds(
					restored_function),
			expected_unresolved);

	const Ref<FoundryScript> declarations = compile_bytecode_test_source(
			"extends RefCounted\n"
			"var reflection_unresolved_property_1237: int\n"
			"signal reflection_unresolved_signal_1237\n"
			"func reflection_unresolved_method_1237() -> void:\n"
			"\tpass\n");
	FSNameManglerAnalysis::Input source_input;
	source_input.scripts.push_back(original);
	source_input.scripts.push_back(declarations);
	const FSNameManglerAnalysis::Result source_analysis =
			FSNameManglerAnalysis::analyze(source_input);
	FSNameManglerAnalysis::Input restored_input;
	restored_input.scripts.push_back(restored);
	restored_input.scripts.push_back(declarations);
	const FSNameManglerAnalysis::Result restored_analysis =
			FSNameManglerAnalysis::analyze(restored_input);
	REQUIRE_EQ(source_analysis.error, OK);
	REQUIRE_EQ(restored_analysis.error, OK);
	REQUIRE_EQ(
			source_analysis.rename_map.size(),
			restored_analysis.rename_map.size());
	for (const KeyValue<StringName, StringName> &rename :
			source_analysis.rename_map) {
		REQUIRE(restored_analysis.rename_map.has(rename.key));
		CHECK_EQ(restored_analysis.rename_map[rename.key], rename.value);
	}
	const auto has_reflection_reason =
			[](const FSNameManglerAnalysis::Result &p_result,
					const StringName &p_name) {
				const FSNameManglerAnalysis::Classification *classification =
						p_result.find(p_name);
				if (classification == nullptr) {
					return false;
				}
				for (const FSNameManglerAnalysis::KeepEvidence &evidence :
						classification->keep_evidence) {
					if (evidence.reason ==
							FSNameManglerAnalysis::KEEP_REFLECTION) {
						return true;
					}
				}
				return false;
			};
	const StringName inspected_names[] = {
		inspected_method,
		inspected_property,
		inspected_signal,
	};
	for (const StringName &name : inspected_names) {
		CAPTURE(name);
		CHECK_FALSE(source_analysis.rename_map.has(name));
		CHECK_FALSE(restored_analysis.rename_map.has(name));
		CHECK(has_reflection_reason(source_analysis, name));
		CHECK(has_reflection_reason(restored_analysis, name));
	}
}

TEST_CASE("[FoundryScript][NameMangler][Reflection][Bytecode] Base-owned self scope includes dynamic descendants") {
	const StringName derived_method =
			SNAME("reflection_dynamic_derived_method_1237");
	const StringName derived_property =
			SNAME("reflection_dynamic_derived_property_1237");
	const StringName derived_signal =
			SNAME("reflection_dynamic_derived_signal_1237");
	const StringName unrelated_method =
			SNAME("reflection_dynamic_unrelated_method_1237");
	const StringName unrelated_property =
			SNAME("reflection_dynamic_unrelated_property_1237");
	const StringName unrelated_signal =
			SNAME("reflection_dynamic_unrelated_signal_1237");
	const Ref<FoundryScript> source_base = compile_bytecode_test_source(
			"extends RefCounted\n"
			"var reflection_dynamic_base_property_1237: int\n"
			"signal reflection_dynamic_base_signal_1237\n"
			"func reflection_dynamic_base_method_1237() -> void:\n"
			"\tpass\n"
			"func reflection_dynamic_has_method_1237(name: String) -> bool:\n"
			"\tfor descriptor in get_method_list():\n"
			"\t\tif str(descriptor.name) == name:\n"
			"\t\t\treturn true\n"
			"\treturn false\n"
			"func reflection_dynamic_has_property_1237(name: String) -> bool:\n"
			"\tfor descriptor in get_property_list():\n"
			"\t\tif str(descriptor.name) == name:\n"
			"\t\t\treturn true\n"
			"\treturn false\n"
			"func reflection_dynamic_has_signal_1237(name: String) -> bool:\n"
			"\tfor descriptor in get_signal_list():\n"
			"\t\tif str(descriptor.name) == name:\n"
			"\t\t\treturn true\n"
			"\treturn false\n");
	const Ref<FoundryScript> source_derived =
			compile_bytecode_test_source(vformat(
					"extends \"%s\"\n"
					"var reflection_dynamic_derived_property_1237: int\n"
					"signal reflection_dynamic_derived_signal_1237\n"
					"func reflection_dynamic_derived_method_1237() -> void:\n"
					"\tpass\n",
					source_base->get_script_path()));
	const Ref<FoundryScript> unrelated = compile_bytecode_test_source(
			"extends RefCounted\n"
			"var reflection_dynamic_unrelated_property_1237: int\n"
			"signal reflection_dynamic_unrelated_signal_1237\n"
			"func reflection_dynamic_unrelated_method_1237() -> void:\n"
			"\tpass\n");

	BytecodeTestResolver base_resolver;
	const Ref<FoundryScript> restored_base =
			bytecode_round_trip_script(source_base, &base_resolver);
	BytecodeTestResolver derived_resolver;
	derived_resolver.scripts.insert(
			restored_base->get_script_path() + "::" +
					restored_base->get_fully_qualified_name(),
			restored_base);
	const Ref<FoundryScript> restored_derived =
			bytecode_round_trip_script(source_derived, &derived_resolver);
	REQUIRE(restored_derived->get_base() == restored_base);

	const Ref<FoundryScript> runtime_scripts[] = {
		source_derived,
		restored_derived,
	};
	for (const Ref<FoundryScript> &runtime_script : runtime_scripts) {
		const Variant instance_variant = bytecode_new_instance(runtime_script);
		Object *instance = instance_variant;
		CAPTURE(runtime_script->is_compiled_binary());
		CHECK((bool)bytecode_instance_call(
				instance, SNAME("reflection_dynamic_has_method_1237"),
				{ String(derived_method) }));
		CHECK((bool)bytecode_instance_call(
				instance, SNAME("reflection_dynamic_has_property_1237"),
				{ String(derived_property) }));
		CHECK((bool)bytecode_instance_call(
				instance, SNAME("reflection_dynamic_has_signal_1237"),
				{ String(derived_signal) }));
	}

	FSNameManglerAnalysis::Input source_input;
	source_input.scripts.push_back(source_base);
	source_input.scripts.push_back(source_derived);
	source_input.scripts.push_back(unrelated);
	const FSNameManglerAnalysis::Result source_analysis =
			FSNameManglerAnalysis::analyze(source_input);
	FSNameManglerAnalysis::Input restored_input;
	restored_input.scripts.push_back(restored_base);
	restored_input.scripts.push_back(restored_derived);
	restored_input.scripts.push_back(unrelated);
	const FSNameManglerAnalysis::Result restored_analysis =
			FSNameManglerAnalysis::analyze(restored_input);
	REQUIRE_EQ(source_analysis.error, OK);
	REQUIRE_EQ(restored_analysis.error, OK);
	REQUIRE_EQ(
			source_analysis.rename_map.size(),
			restored_analysis.rename_map.size());
	for (const KeyValue<StringName, StringName> &rename :
			source_analysis.rename_map) {
		REQUIRE(restored_analysis.rename_map.has(rename.key));
		CHECK_EQ(restored_analysis.rename_map[rename.key], rename.value);
	}

	const StringName derived_names[] = {
		derived_method,
		derived_property,
		derived_signal,
	};
	const StringName unrelated_names[] = {
		unrelated_method,
		unrelated_property,
		unrelated_signal,
	};
	for (int i = 0; i < 3; i++) {
		CAPTURE(derived_names[i]);
		CHECK_FALSE(source_analysis.rename_map.has(derived_names[i]));
		CHECK_FALSE(restored_analysis.rename_map.has(derived_names[i]));
		CAPTURE(unrelated_names[i]);
		CHECK(source_analysis.rename_map.has(unrelated_names[i]));
		CHECK(restored_analysis.rename_map.has(unrelated_names[i]));
	}
}

TEST_CASE("[FoundryScript][BytecodeScript] Enum functions and their exact owners round-trip") {
	const Ref<FoundryScript> original = compile_bytecode_test_source(
			"enum Status:\n"
			"\tUNKNOWN = 0\n"
			"\tREADY = 7\n"
			"\n"
			"\tfunc render(prefix: String) -> String:\n"
			"\t\tvar render_value := func(value: int) -> String:\n"
			"\t\t\treturn prefix + str(value)\n"
			"\t\treturn String(render_value.call(self))\n"
			"\n"
			"\tstatic func parse(value: int) -> Self:\n"
			"\t\treturn READY if value > 0 else UNKNOWN\n"
			"\n"
			"class Nested:\n"
			"\tenum Mode:\n"
			"\t\tOFF = 0\n"
			"\t\tON = 5\n"
			"\n"
			"\t\tfunc add(delta: int) -> int:\n"
			"\t\t\treturn self + delta\n"
			"\n"
			"\t\tstatic func initial() -> Self:\n"
			"\t\t\treturn ON\n"
			"\n"
			"func run() -> Array:\n"
			"\tvar status: Status = Status.parse(1)\n"
			"\tvar mode: Nested.Mode = Nested.Mode.initial()\n"
			"\treturn [status.render(\"status:\"), mode.add(3)]\n");

	FSFunction *original_render = original->get_enum_function(SNAME("Status"), SNAME("render"), false);
	FSFunction *original_parse = original->get_enum_function(SNAME("Status"), SNAME("parse"), true);
	REQUIRE(original_render != nullptr);
	REQUIRE(original_parse != nullptr);
	CHECK(original_render->get_script() == original.ptr());
	CHECK(original_parse->get_script() == original.ptr());
	REQUIRE(original_render->get_lambdas().size() == 1);

	BytecodeTestResolver resolver;
	const Ref<FoundryScript> restored = bytecode_round_trip_script(original, &resolver);
	FSFunction *restored_render = restored->get_enum_function(SNAME("Status"), SNAME("render"), false);
	FSFunction *restored_parse = restored->get_enum_function(SNAME("Status"), SNAME("parse"), true);
	REQUIRE(restored_render != nullptr);
	REQUIRE(restored_parse != nullptr);
	if (restored_render == nullptr || restored_parse == nullptr) {
		return;
	}
	CHECK(restored_render->get_script() == restored.ptr());
	CHECK(restored_parse->get_script() == restored.ptr());
	CHECK_FALSE(restored->get_member_functions().has(SNAME("render")));
	CHECK_FALSE(restored->get_member_functions().has(SNAME("parse")));
	REQUIRE(restored_render->get_lambdas().size() == 1);
	const FoundryScript::LambdaInfo *restored_lambda_info =
			restored->get_lambda_info().getptr(restored_render->get_lambdas()[0]);
	REQUIRE(restored_lambda_info != nullptr);
	CHECK(restored_lambda_info->capture_count == 1);

	REQUIRE(original->get_subclasses().has(SNAME("Nested")));
	REQUIRE(restored->get_subclasses().has(SNAME("Nested")));
	const Ref<FoundryScript> original_nested = original->get_subclasses().find(SNAME("Nested"))->value;
	const Ref<FoundryScript> restored_nested = restored->get_subclasses().find(SNAME("Nested"))->value;
	FSFunction *original_add = original_nested->get_enum_function(SNAME("Mode"), SNAME("add"), false);
	FSFunction *original_initial = original_nested->get_enum_function(SNAME("Mode"), SNAME("initial"), true);
	FSFunction *restored_add = restored_nested->get_enum_function(SNAME("Mode"), SNAME("add"), false);
	FSFunction *restored_initial = restored_nested->get_enum_function(SNAME("Mode"), SNAME("initial"), true);
	REQUIRE(original_add != nullptr);
	REQUIRE(original_initial != nullptr);
	REQUIRE(restored_add != nullptr);
	REQUIRE(restored_initial != nullptr);
	if (original_add == nullptr || original_initial == nullptr || restored_add == nullptr || restored_initial == nullptr) {
		return;
	}
	CHECK(original_add->get_script() == original_nested.ptr());
	CHECK(original_initial->get_script() == original_nested.ptr());
	CHECK(restored_add->get_script() == restored_nested.ptr());
	CHECK(restored_initial->get_script() == restored_nested.ptr());
	CHECK_FALSE(restored_nested->get_member_functions().has(SNAME("add")));
	CHECK_FALSE(restored_nested->get_member_functions().has(SNAME("initial")));

	const Variant instance_variant = bytecode_new_instance(restored);
	Object *instance = instance_variant;
	const Array result = bytecode_instance_call(instance, SNAME("run"), {});
	REQUIRE(result.size() == 2);
	CHECK((String)result[0] == "status:7");
	CHECK((int64_t)result[1] == 8);
}

TEST_CASE("[FoundryScript][BytecodeScript] Named lambdas may shadow member function names") {
	// A named lambda (grammar: `func`, [ identifier ], ...) may legally share its name with a
	// member function; the loader must accept it, and its eventual destruction must not unregister
	// the member (FSFunction unregistration is identity-checked).
	const Ref<FoundryScript> original = compile_bytecode_test_source(
			"func helper() -> int:\n"
			"\treturn 1\n"
			"\n"
			"func run() -> int:\n"
			"\tvar f := func helper() -> int:\n"
			"\t\treturn 2\n"
			"\treturn int(f.call()) + helper()\n");
	{
		const Variant original_instance_variant = bytecode_new_instance(original);
		Object *original_instance = original_instance_variant;
		CHECK((int64_t)bytecode_instance_call(original_instance, SNAME("run"), {}) == 3);
	}

	BytecodeTestResolver resolver;
	const Ref<FoundryScript> restored = bytecode_round_trip_script(original, &resolver);
	CHECK(restored->has_method(SNAME("helper")));
	const Variant instance_variant = bytecode_new_instance(restored);
	Object *instance = instance_variant;
	CHECK((int64_t)bytecode_instance_call(instance, SNAME("run"), {}) == 3);
	CHECK((int64_t)bytecode_instance_call(instance, SNAME("helper"), {}) == 1);
}

TEST_CASE("[FoundryScript][BytecodeScript] Duplicate function names in corrupt buffers fail cleanly") {
	const Ref<FoundryScript> original = compile_bytecode_test_source(
			"func alpha() -> int:\n"
			"\treturn 1\n"
			"\n"
			"func bravo() -> int:\n"
			"\treturn 2\n");

	FSBytecodeExporter exporter;
	Vector<uint8_t> buffer;
	REQUIRE(exporter.serialize(original, buffer) == OK);

	// Rename the second function to the first one's name in the string table (same length), so the
	// loader reads two top-level functions both named "alpha".
	const CharString marker = String("bravo").utf8();
	const CharString replacement = String("alpha").utf8();
	bool patched = false;
	for (int i = 0; i + marker.length() <= buffer.size(); i++) {
		if (memcmp(&buffer[i], marker.get_data(), marker.length()) == 0) {
			memcpy(&buffer.write[i], replacement.get_data(), replacement.length());
			patched = true;
			break;
		}
	}
	REQUIRE(patched);
	if (!patched) {
		return;
	}

	Ref<FoundryScript> target;
	target.instantiate();
	target->set_path_cache(original->get_script_path());
	BytecodeTestResolver resolver;
	FSBytecodeLoader loader;
	loader.set_resolver(&resolver);
	ERR_PRINT_OFF;
	CHECK(loader.load_full(buffer, target) == ERR_INVALID_DATA);
	ERR_PRINT_ON;
	CHECK(!target->is_valid());
	// The surviving first function stays registered and owned by the discarded script (its
	// destructor frees it exactly once); the duplicate's deletion must not have unregistered it.
	CHECK(target->get_member_functions().has(SNAME("alpha")));
}

TEST_CASE("[FoundryScript][BytecodeHardening] Duplicate enum function names in corrupt buffers fail cleanly") {
	const Ref<FoundryScript> original = compile_bytecode_test_source(
			"enum Status:\n"
			"\tREADY = 1\n"
			"\n"
			"\tfunc alpha() -> int:\n"
			"\t\treturn self\n"
			"\n"
			"\tstatic func bravo() -> Self:\n"
			"\t\treturn READY\n");

	FSBytecodeExporter exporter;
	Vector<uint8_t> buffer;
	REQUIRE(exporter.serialize(original, buffer) == OK);

	// Rename the static function to the instance function's equal-length name. A valid enum cannot
	// declare the same name twice across call kinds, so the reconstructed table must reject it.
	const CharString marker = String("bravo").utf8();
	const CharString replacement = String("alpha").utf8();
	bool patched = false;
	for (int i = 0; i + marker.length() <= buffer.size(); i++) {
		if (memcmp(&buffer[i], marker.get_data(), marker.length()) == 0) {
			memcpy(&buffer.write[i], replacement.get_data(), replacement.length());
			patched = true;
			break;
		}
	}
	REQUIRE(patched);
	if (!patched) {
		return;
	}

	Ref<FoundryScript> target;
	target.instantiate();
	target->set_path_cache(original->get_script_path());
	BytecodeTestResolver resolver;
	FSBytecodeLoader loader;
	loader.set_resolver(&resolver);
	ERR_PRINT_OFF;
	CHECK(loader.load_full(buffer, target) == ERR_INVALID_DATA);
	ERR_PRINT_ON;
	CHECK(!target->is_valid());
}

TEST_CASE("[FoundryScript][BytecodeHardening] Duplicate enum table references in corrupt buffers fail cleanly") {
	const Ref<FoundryScript> original = compile_bytecode_test_source(
			"enum Alpha:\n"
			"\tVALUE = 1\n"
			"\n"
			"\tfunc first() -> int:\n"
			"\t\treturn self\n"
			"\n"
			"enum Bravo:\n"
			"\tVALUE = 2\n"
			"\n"
			"\tfunc second() -> int:\n"
			"\t\treturn self\n");

	FSBytecodeExporter exporter;
	Vector<uint8_t> buffer;
	REQUIRE(exporter.serialize(original, buffer) == OK);

	// The string table deduplicates enum identities. Changing Bravo to Alpha therefore makes both
	// serialized enum table entries refer to the same owner-local enum.
	const CharString marker = String("Bravo").utf8();
	const CharString replacement = String("Alpha").utf8();
	bool patched = false;
	for (int i = 0; i + marker.length() <= buffer.size(); i++) {
		if (memcmp(&buffer[i], marker.get_data(), marker.length()) == 0) {
			memcpy(&buffer.write[i], replacement.get_data(), replacement.length());
			patched = true;
			break;
		}
	}
	REQUIRE(patched);

	Ref<FoundryScript> target;
	target.instantiate();
	target->set_path_cache(original->get_script_path());
	BytecodeTestResolver resolver;
	FSBytecodeLoader loader;
	loader.set_resolver(&resolver);
	ERR_PRINT_OFF;
	CHECK(loader.load_full(buffer, target) == ERR_INVALID_DATA);
	ERR_PRINT_ON;
	CHECK(!target->is_valid());
}

TEST_CASE("[FoundryScript][BytecodeHardening] Duplicate static-variable names in corrupt buffers fail cleanly") {
	// Two builtin-typed static variables of equal-length names so the string-table entry can be byte-patched to
	// collide. Both are `int`, so `_static_default_init` writes `static_variables.write[index]` for each.
	const Ref<FoundryScript> original = compile_bytecode_test_source(
			"static var alpha: int = 11\n"
			"static var bravo: int = 22\n");

	FSBytecodeExporter exporter;
	Vector<uint8_t> buffer;
	REQUIRE(exporter.serialize(original, buffer) == OK);

	// The pristine buffer round-trips: two distinct static variables, each default-initialized to its value, so
	// the added bounds are not over-tight and the dense static table is sized to hold both slots.
	{
		BytecodeTestResolver resolver;
		Ref<FoundryScript> accepted;
		accepted.instantiate();
		accepted->set_path_cache(original->get_script_path());
		FSBytecodeLoader loader;
		loader.set_resolver(&resolver);
		REQUIRE(loader.load_full(buffer, accepted) == OK);
		CHECK(accepted->is_valid());
		CHECK((int64_t)TestFSBytecodeScriptAccessor::get_static_variable(accepted, SNAME("alpha")) == 11);
		CHECK((int64_t)TestFSBytecodeScriptAccessor::get_static_variable(accepted, SNAME("bravo")) == 22);
		accepted->clear();
	}

	// Rename the second static to the first one's name in the string table (same length), so the loader
	// reads two static-variable entries both named "alpha". The second entry keeps its own index of 1,
	// which is `< static_variable_count` (2) but past the deduplicated table (size 1): without the
	// duplicate-name rejection this is the out-of-bounds `static_variables.write[1]` in
	// `_static_default_init`.
	const CharString marker = String("bravo").utf8();
	const CharString replacement = String("alpha").utf8();
	bool patched = false;
	for (int i = 0; i + marker.length() <= buffer.size(); i++) {
		if (memcmp(&buffer[i], marker.get_data(), marker.length()) == 0) {
			memcpy(&buffer.write[i], replacement.get_data(), replacement.length());
			patched = true;
			break;
		}
	}
	REQUIRE(patched);

	Ref<FoundryScript> target;
	target.instantiate();
	target->set_path_cache(original->get_script_path());
	BytecodeTestResolver resolver;
	FSBytecodeLoader loader;
	loader.set_resolver(&resolver);
	ERR_PRINT_OFF;
	CHECK(loader.load_full(buffer, target) == ERR_INVALID_DATA);
	ERR_PRINT_ON;
	CHECK(!target->is_valid());

	original->clear();
}

TEST_CASE("[FoundryScript][BytecodeScript] Exporter reuse does not leak strings across scripts") {
	const Ref<FoundryScript> first = compile_bytecode_test_source(
			"var first_script_unique_marker: int = 1\n");
	const Ref<FoundryScript> second = compile_bytecode_test_source(
			"var second_script_unique_marker: int = 2\n");

	FSBytecodeExporter exporter;
	Vector<uint8_t> first_buffer;
	REQUIRE(exporter.serialize(first, first_buffer) == OK);
	Vector<uint8_t> second_buffer;
	REQUIRE(exporter.serialize(second, second_buffer) == OK);

	CHECK(bytecode_buffer_contains(first_buffer, "first_script_unique_marker"));
	CHECK(bytecode_buffer_contains(second_buffer, "second_script_unique_marker"));
	// A reused exporter must reset its string table per script; otherwise every buffer embeds all
	// previously serialized scripts' identifiers.
	CHECK(!bytecode_buffer_contains(second_buffer, "first_script_unique_marker"));

	// The second buffer is self-consistent after the reset.
	BytecodeTestResolver resolver;
	Ref<FoundryScript> restored;
	restored.instantiate();
	restored->set_path_cache(second->get_script_path());
	FSBytecodeLoader loader;
	loader.set_resolver(&resolver);
	REQUIRE(loader.load_full(second_buffer, restored) == OK);
	CHECK(restored->debug_get_member_indices().has(SNAME("second_script_unique_marker")));
}

TEST_CASE("[FoundryScript][BytecodeScript] Serialized scripts carry no source text") {
	const String source =
			"var member_marker: int = 1\n"
			"\n"
			"func compute() -> int:\n"
			"\tvar distinctive_local_marker := 41\n"
			"\treturn distinctive_local_marker + member_marker\n";
	const Ref<FoundryScript> original = compile_bytecode_test_source(source);

	FSBytecodeExporter exporter;
	Vector<uint8_t> buffer;
	REQUIRE(exporter.serialize(original, buffer) == OK);

	// Local identifiers and the source text never reach the buffer; member names are accepted
	// residual information (the VM dispatches by name).
	CHECK(!bytecode_buffer_contains(buffer, "distinctive_local_marker"));
	CHECK(!bytecode_buffer_contains(buffer, source));
	CHECK(bytecode_buffer_contains(buffer, "member_marker"));

	BytecodeTestResolver resolver;
	const Ref<FoundryScript> restored = bytecode_round_trip_script(original, &resolver);
	CHECK(!restored->has_source_code());
	const Variant instance_variant = bytecode_new_instance(restored);
	Object *instance = instance_variant;
	CHECK((int64_t)bytecode_instance_call(instance, SNAME("compute"), {}) == 42);
}

TEST_CASE("[FoundryScript][BytecodeScript] Corrupted script buffers fail cleanly") {
	const Ref<FoundryScript> original = compile_bytecode_test_source(
			"class Inner:\n"
			"\tvar value: int = 1\n"
			"\n"
			"func read() -> int:\n"
			"\treturn Inner.new().value\n");

	FSBytecodeExporter exporter;
	Vector<uint8_t> buffer;
	REQUIRE(exporter.serialize(original, buffer) == OK);
	int header_size = 0;
	REQUIRE(FSBytecodeLoader::check_header(buffer, &header_size) == OK);

	BytecodeTestResolver resolver;

	// A corrupted section marker (the string-table marker sits right after the script flags).
	{
		Vector<uint8_t> corrupted = buffer;
		for (int i = 0; i < 4; i++) {
			corrupted.write[header_size + 4 + i] = 0xFF;
		}
		Ref<FoundryScript> target;
		target.instantiate();
		target->set_path_cache(original->get_script_path());
		FSBytecodeLoader loader;
		loader.set_resolver(&resolver);
		ERR_PRINT_OFF;
		CHECK(loader.load_skeleton(corrupted, target) == ERR_INVALID_DATA);
		CHECK(loader.load_full(corrupted, target) == ERR_INVALID_DATA);
		ERR_PRINT_ON;
		CHECK(!target->is_valid());
	}

	// A buffer truncated inside the sections.
	{
		Vector<uint8_t> truncated = buffer;
		truncated.resize(header_size + 6);
		Ref<FoundryScript> target;
		target.instantiate();
		target->set_path_cache(original->get_script_path());
		FSBytecodeLoader loader;
		loader.set_resolver(&resolver);
		ERR_PRINT_OFF;
		CHECK(loader.load_skeleton(truncated, target) != OK);
		CHECK(loader.load_full(truncated, target) != OK);
		ERR_PRINT_ON;
		CHECK(!target->is_valid());
	}
}

static void bytecode_write_file(const String &p_path, const Vector<uint8_t> &p_buffer) {
	Ref<FileAccess> file = FileAccess::open(p_path, FileAccess::WRITE);
	REQUIRE(file.is_valid());
	file->store_buffer(p_buffer.ptr(), p_buffer.size());
}

// Writes the `.remap` sidecar `ResourceLoader::path_remap` reads, redirecting `p_source_path` to
// `p_target_path` exactly like an export does when it replaces a text script with its `.fsb`.
static void bytecode_write_remap_file(const String &p_source_path, const String &p_target_path) {
	Ref<FileAccess> file = FileAccess::open(p_source_path + ".remap", FileAccess::WRITE);
	REQUIRE(file.is_valid());
	file->store_string(vformat("[remap]\n\npath=\"%s\"\n", p_target_path));
}

TEST_CASE("[FoundryScript][BytecodeScript] Partial link failure is recovered on reload retry") {
	const Ref<FoundryScript> original = compile_bytecode_test_source(
			"func alpha() -> int:\n"
			"\treturn 1\n"
			"\n"
			"func bravo() -> int:\n"
			"\treturn 2\n");

	const String source_path = original->get_script_path();
	FSBytecodeExporter exporter;
	Vector<uint8_t> good_buffer;
	REQUIRE(exporter.serialize(original, good_buffer) == OK);

	Vector<uint8_t> corrupt_buffer = good_buffer;
	const CharString marker = String("bravo").utf8();
	const CharString replacement = String("alpha").utf8();
	bool patched = false;
	for (int i = 0; i + marker.length() <= corrupt_buffer.size(); i++) {
		if (memcmp(&corrupt_buffer[i], marker.get_data(), marker.length()) == 0) {
			memcpy(&corrupt_buffer.write[i], replacement.get_data(), replacement.length());
			patched = true;
			break;
		}
	}
	REQUIRE(patched);

	const String binary_path = source_path.get_basename() + ".fsb";
	bytecode_write_file(binary_path, corrupt_buffer);
	bytecode_write_remap_file(source_path, binary_path);

	Ref<FoundryScript> target;
	target.instantiate();
	target->set_path_cache(source_path);

	BytecodeTestResolver resolver;
	FSBytecodeLoader loader;
	loader.set_resolver(&resolver);
	ERR_PRINT_OFF;
	CHECK(loader.load_full(corrupt_buffer, target) == ERR_INVALID_DATA);
	ERR_PRINT_ON;
	CHECK(!target->is_valid());
	CHECK(target->get_member_functions().has(SNAME("alpha")));

	bytecode_write_file(binary_path, good_buffer);
	ERR_PRINT_OFF;
	CHECK(target->reload() == OK);
	ERR_PRINT_ON;
	CHECK(target->is_valid());
	CHECK(target->get_member_functions().has(SNAME("alpha")));
	CHECK(target->get_member_functions().has(SNAME("bravo")));

	DirAccess::remove_absolute(source_path + ".remap");
	DirAccess::remove_absolute(binary_path);
}

TEST_CASE("[FoundryScript][BytecodeCache] ResourceLoader loads a .fsb end-to-end with dependencies and static variables") {
	const String helper_path = TestUtils::get_temp_path("test_bytecode_cache_helper.fs");
	{
		Ref<FileAccess> helper_file = FileAccess::open(helper_path, FileAccess::WRITE);
		REQUIRE(helper_file.is_valid());
		helper_file->store_string("const NAME = \"helper\"\n");
	}

	const Ref<FoundryScript> original = compile_bytecode_test_source(vformat(R"(
const Helper = preload("%s")
static var counter := 3

func use_helper() -> String:
	return Helper.NAME + str(counter)
)",
			helper_path));

	FSBytecodeExporter exporter;
	Vector<uint8_t> buffer;
	REQUIRE(exporter.serialize(original, buffer) == OK);

	const String binary_path = TestUtils::get_temp_path("test_bytecode_cache_direct.fsb");
	bytecode_write_file(binary_path, buffer);

	// The dependency list comes from the header section; the text path would UTF-8 parse the file
	// and read garbage on binary data.
	List<String> dependencies;
	ResourceLoader::get_dependencies(binary_path, &dependencies);
	bool helper_dependency_found = false;
	for (const String &dependency : dependencies) {
		helper_dependency_found = helper_dependency_found || dependency.contains(helper_path);
	}
	CHECK(helper_dependency_found);

	const Ref<FoundryScript> loaded = ResourceLoader::load(binary_path);
	REQUIRE(loaded.is_valid());
	CHECK(loaded->is_valid());
	CHECK(loaded->is_compiled_binary());
	CHECK(loaded != original);
	CHECK(ResourceCache::has(binary_path));
	CHECK(TestFSCacheAccessor::has_full(binary_path));

	// The serialized static-data flags route the loaded script into the static cache, mirroring
	// what the compiler does on the text path.
	CHECK(TestFSCacheAccessor::get_static(loaded->get_fully_qualified_name()) == loaded);
	CHECK(TestFSBytecodeScriptAccessor::get_static_variable(loaded, "counter") == Variant(3));

	{
		const Variant instance_variant = bytecode_new_instance(loaded);
		Object *instance = instance_variant;
		CHECK(String(bytecode_instance_call(instance, SNAME("use_helper"), {})) == "helper3");
	}

	// reload() on a linked bytecode script never parses; it is a no-op that keeps the script valid.
	CHECK(loaded->reload(true) == OK);
	CHECK(loaded->is_valid());

	// Scripts with static state keep reference cycles through themselves; clear the fixtures
	// explicitly so their compiled functions do not outlive other suites in this run.
	FSCache::remove_static_script(loaded->get_fully_qualified_name());
	FSCache::remove_script(binary_path);
	FSCache::remove_script(original->get_script_path());
	FSCache::remove_script(helper_path);
	loaded->clear();
	original->clear();
	DirAccess::remove_absolute(binary_path);
}

TEST_CASE("[FoundryScript][BytecodeCache] A remapped .fs path loads its .fsb and refuses the parser") {
	const Ref<FoundryScript> original = compile_bytecode_test_source(R"(
func ping() -> String:
	return "pong"
)");
	const String source_path = original->get_script_path();

	FSBytecodeExporter exporter;
	Vector<uint8_t> buffer;
	REQUIRE(exporter.serialize(original, buffer) == OK);

	const String binary_path = source_path.get_basename() + ".fsb";
	bytecode_write_file(binary_path, buffer);
	bytecode_write_remap_file(source_path, binary_path);

	// Drop the text compilation so the load below starts from a cold cache, the way an exported
	// game (which never had the text script) would.
	FSCache::remove_script(source_path);
	CHECK(ResourceLoader::path_remap(source_path) == binary_path);

	const Ref<FoundryScript> loaded = ResourceLoader::load(source_path);
	REQUIRE(loaded.is_valid());
	CHECK(loaded->is_valid());
	CHECK(loaded->is_compiled_binary());
	CHECK(loaded != original);
	const Variant instance_variant = bytecode_new_instance(loaded);
	Object *instance = instance_variant;
	CHECK(String(bytecode_instance_call(instance, SNAME("ping"), {})) == "pong");

	// The parser pipeline must never touch a bytecode-backed path.
	Error parser_error = OK;
	ERR_PRINT_OFF;
	Ref<FSParserRef> parser_ref = FSCache::get_parser(source_path, FSParserRef::PARSED, parser_error);
	ERR_PRINT_ON;
	CHECK(parser_error == ERR_UNAVAILABLE);

	// An update-from-disk load of an already linked binary is a no-op that returns the same script.
	Error update_error = OK;
	const Ref<FoundryScript> updated = FSCache::get_full_script(source_path, update_error, "", true);
	CHECK(update_error == OK);
	CHECK(updated == loaded);
	CHECK(loaded->is_valid());

	// Evict the loaded script and the ERR_UNAVAILABLE parser entry, and drop the fixture files, so
	// no cache or disk state leaks into other suites.
	FSCache::remove_script(source_path);
	DirAccess::remove_absolute(source_path + ".remap");
	DirAccess::remove_absolute(binary_path);
}

TEST_CASE("[FoundryScript][BytecodeCache] Cyclic .fsb preloads publish before linking and both load") {
	if (!FSLanguage::get_singleton()->get_reflection_singleton().is_valid()) {
		FSLanguage::get_singleton()->init();
	}

	const String path_a = TestUtils::get_temp_path("test_bytecode_cache_cycle_a.fs");
	const String path_b = TestUtils::get_temp_path("test_bytecode_cache_cycle_b.fs");
	{
		Ref<FileAccess> file_a = FileAccess::open(path_a, FileAccess::WRITE);
		REQUIRE(file_a.is_valid());
		file_a->store_string(vformat(R"(
const Other = preload("%s")

static func ping() -> String:
	return "ping-a"

func chain() -> String:
	return Other.pong()
)",
				path_b));
		Ref<FileAccess> file_b = FileAccess::open(path_b, FileAccess::WRITE);
		REQUIRE(file_b.is_valid());
		file_b->store_string(vformat(R"(
const Other = preload("%s")

static func pong() -> String:
	return "pong-" + Other.ping()
)",
				path_a));
	}

	const bool previous_ignore_warnings = FSParser::is_ignoring_warnings();
	FSParser::set_ignoring_warnings(true);

	Vector<uint8_t> buffer_a;
	Vector<uint8_t> buffer_b;
	{
		const Ref<FoundryScript> text_a = ResourceLoader::load(path_a);
		REQUIRE(text_a.is_valid());
		REQUIRE(text_a->is_valid());
		Error text_error = OK;
		const Ref<FoundryScript> text_b = FSCache::get_full_script(path_b, text_error);
		REQUIRE(text_error == OK);
		REQUIRE(text_b.is_valid());
		REQUIRE(text_b->is_valid());

		FSBytecodeExporter exporter_a;
		REQUIRE(exporter_a.serialize(text_a, buffer_a) == OK);
		FSBytecodeExporter exporter_b;
		REQUIRE(exporter_b.serialize(text_b, buffer_b) == OK);

		// Drop the text compilations and break their preload reference cycle so both scripts
		// actually free and leave ResourceCache; an exported game never had them.
		FSCache::remove_script(path_a);
		FSCache::remove_script(path_b);
		text_a->clear();
		text_b->clear();
	}
	FSParser::set_ignoring_warnings(previous_ignore_warnings);
	CHECK(!ResourceCache::has(path_a));
	CHECK(!ResourceCache::has(path_b));

	bytecode_write_file(path_a.get_basename() + ".fsb", buffer_a);
	bytecode_write_file(path_b.get_basename() + ".fsb", buffer_b);
	bytecode_write_remap_file(path_a, path_a.get_basename() + ".fsb");
	bytecode_write_remap_file(path_b, path_b.get_basename() + ".fsb");

	const Ref<FoundryScript> loaded_a = ResourceLoader::load(path_a);
	REQUIRE(loaded_a.is_valid());
	CHECK(loaded_a->is_valid());
	CHECK(loaded_a->is_compiled_binary());

	// B was published and fully linked while A was mid-link; loading it now hits the cache.
	const Ref<FoundryScript> loaded_b = ResourceLoader::load(path_b);
	REQUIRE(loaded_b.is_valid());
	CHECK(loaded_b->is_valid());
	CHECK(loaded_b->is_compiled_binary());

	// The cross references are identity-correct: B linked against A's published shell, which is
	// the same object that finished loading afterwards.
	REQUIRE(loaded_a->get_constants().has(SNAME("Other")));
	REQUIRE(loaded_b->get_constants().has(SNAME("Other")));
	CHECK(Ref<FoundryScript>(loaded_a->get_constants()[SNAME("Other")]) == loaded_b);
	CHECK(Ref<FoundryScript>(loaded_b->get_constants()[SNAME("Other")]) == loaded_a);

	{
		const Variant instance_variant = bytecode_new_instance(loaded_a);
		Object *instance = instance_variant;
		CHECK(String(bytecode_instance_call(instance, SNAME("chain"), {})) == "pong-ping-a");
	}

	// Break the loaded scripts' preload reference cycle here so their compiled functions do not
	// outlive other suites when this case runs before the harness teardown.
	FSCache::remove_script(path_a);
	FSCache::remove_script(path_b);
	loaded_a->clear();
	loaded_b->clear();

	DirAccess::remove_absolute(path_a + ".remap");
	DirAccess::remove_absolute(path_b + ".remap");
}

TEST_CASE("[FoundryScript][BytecodeHardening] A dependent .fsb refuses to link against a dependency that previously failed to load") {
	if (!FSLanguage::get_singleton()->get_reflection_singleton().is_valid()) {
		FSLanguage::get_singleton()->init();
	}

	const String base_path = TestUtils::get_temp_path("test_bytecode_stale_base.fs");
	const String dependent_path = TestUtils::get_temp_path("test_bytecode_stale_dependent.fs");
	{
		Ref<FileAccess> base_file = FileAccess::open(base_path, FileAccess::WRITE);
		REQUIRE(base_file.is_valid());
		base_file->store_string(
				"static func base_value() -> int:\n"
				"\treturn 7\n");
		Ref<FileAccess> dependent_file = FileAccess::open(dependent_path, FileAccess::WRITE);
		REQUIRE(dependent_file.is_valid());
		dependent_file->store_string(vformat(
				"const Base = preload(\"%s\")\n"
				"\n"
				"func run() -> int:\n"
				"\treturn Base.base_value()\n",
				base_path));
	}

	const bool previous_ignore_warnings = FSParser::is_ignoring_warnings();
	FSParser::set_ignoring_warnings(true);

	Vector<uint8_t> buffer_base;
	Vector<uint8_t> buffer_dependent;
	{
		const Ref<FoundryScript> text_base = ResourceLoader::load(base_path);
		REQUIRE(text_base.is_valid());
		REQUIRE(text_base->is_valid());
		const Ref<FoundryScript> text_dependent = ResourceLoader::load(dependent_path);
		REQUIRE(text_dependent.is_valid());
		REQUIRE(text_dependent->is_valid());

		FSBytecodeExporter exporter_base;
		REQUIRE(exporter_base.serialize(text_base, buffer_base) == OK);
		FSBytecodeExporter exporter_dependent;
		REQUIRE(exporter_dependent.serialize(text_dependent, buffer_dependent) == OK);

		// Drop the text compilations and break their preload reference cycle so both scripts free and
		// leave ResourceCache; an exported game never had them.
		FSCache::remove_script(dependent_path);
		FSCache::remove_script(base_path);
		text_dependent->clear();
		text_base->clear();
	}
	FSParser::set_ignoring_warnings(previous_ignore_warnings);

	// Produce a base `.fsb` whose header and skeleton parse but whose class-body section is absent, so
	// the base loads far enough to be published to the cache yet fails to link. The shortest prefix
	// that still yields a valid skeleton ends exactly where the body section begins, so truncating
	// there drops the body while leaving the skeleton intact.
	int skeleton_end = buffer_base.size();
	for (int length = 0; length <= buffer_base.size(); length++) {
		Vector<uint8_t> candidate = buffer_base;
		candidate.resize(length);
		Ref<FoundryScript> probe;
		probe.instantiate();
		probe->set_path_cache(base_path);
		BytecodeTestResolver probe_resolver;
		FSBytecodeLoader probe_loader;
		probe_loader.set_resolver(&probe_resolver);
		ERR_PRINT_OFF;
		const Error skeleton_error = probe_loader.load_skeleton(candidate, probe);
		ERR_PRINT_ON;
		if (skeleton_error == OK) {
			skeleton_end = length;
			break;
		}
	}
	REQUIRE(skeleton_end < buffer_base.size());
	Vector<uint8_t> corrupted_base = buffer_base;
	corrupted_base.resize(skeleton_end);

	bytecode_write_file(base_path.get_basename() + ".fsb", corrupted_base);
	bytecode_write_file(dependent_path.get_basename() + ".fsb", buffer_dependent);
	bytecode_write_remap_file(base_path, base_path.get_basename() + ".fsb");
	bytecode_write_remap_file(dependent_path, dependent_path.get_basename() + ".fsb");

	// The base fails to link, but the failure is published to the full-script cache (text-path
	// parity): an invalid, error-free script that is no longer reloading.
	ERR_PRINT_OFF;
	const Ref<FoundryScript> loaded_base = ResourceLoader::load(base_path);
	ERR_PRINT_ON;
	if (loaded_base.is_valid()) {
		CHECK_FALSE(loaded_base->is_valid());
	}
	REQUIRE(TestFSCacheAccessor::has_full(base_path));
	const Ref<FoundryScript> cached_base = TestFSCacheAccessor::get_full(base_path);
	REQUIRE(cached_base.is_valid());
	CHECK_FALSE(cached_base->is_valid());
	CHECK_FALSE(cached_base->is_reloading());

	// Loading the dependent resolves the base through the cache resolver and gets that stale, invalid
	// script back on a cache hit. It must be rejected so the dependent fails deterministically instead
	// of silently linking against a dead dependency.
	ERR_PRINT_OFF;
	const Ref<FoundryScript> loaded_dependent = ResourceLoader::load(dependent_path);
	ERR_PRINT_ON;
	if (loaded_dependent.is_valid()) {
		CHECK_FALSE(loaded_dependent->is_valid());
	}

	FSCache::remove_script(dependent_path);
	FSCache::remove_script(base_path);
	if (loaded_dependent.is_valid()) {
		loaded_dependent->clear();
	}
	if (loaded_base.is_valid()) {
		loaded_base->clear();
	}
	DirAccess::remove_absolute(base_path + ".remap");
	DirAccess::remove_absolute(dependent_path + ".remap");
	DirAccess::remove_absolute(base_path.get_basename() + ".fsb");
	DirAccess::remove_absolute(dependent_path.get_basename() + ".fsb");
	DirAccess::remove_absolute(base_path);
	DirAccess::remove_absolute(dependent_path);
}

TEST_CASE("[FoundryScript][BytecodeCache] A .fsb from a different engine build refuses to load") {
	const Ref<FoundryScript> original = compile_bytecode_test_source(R"(
func value() -> int:
	return 1
)");
	FSBytecodeExporter exporter;
	Vector<uint8_t> buffer;
	REQUIRE(exporter.serialize(original, buffer) == OK);

	// Flip one byte inside the engine-guard version string region (magic + format version +
	// string length prefix put it at offset 12; see the header codec test).
	buffer.write[16] ^= 0xFF;
	const String binary_path = TestUtils::get_temp_path("test_bytecode_cache_bad_guard.fsb");
	bytecode_write_file(binary_path, buffer);

	Error load_error = OK;
	ERR_PRINT_OFF;
	const Ref<Resource> loaded = ResourceLoader::load(binary_path, "", ResourceFormatLoader::CACHE_MODE_REUSE, &load_error);
	ERR_PRINT_ON;
	CHECK(loaded.is_null());
	CHECK(load_error != OK);
	CHECK(!ResourceCache::has(binary_path));
	CHECK(!TestFSCacheAccessor::has_shallow(binary_path));
	CHECK(!TestFSCacheAccessor::has_full(binary_path));
}

} // namespace FSTests

#endif // TOOLS_ENABLED
