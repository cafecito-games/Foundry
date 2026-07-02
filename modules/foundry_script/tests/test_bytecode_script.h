/**************************************************************************/
/*  test_bytecode_script.h                                                */
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

// Shared fixtures and helpers (test resolver, in-process compile, buffer scanning). Every module
// test header is compiled into the single generated test translation unit, so this include does not
// duplicate test registrations.
#include "test_bytecode_serialization.h"

#include "modules/foundry_script/fs_conformance_registry.h"

#include "core/io/resource_saver.h"
#include "scene/main/node.h"

namespace FSTests {

// Friend accessor exposing the private FoundryScript state the whole-script round-trip assertions
// need; binding kinds are surfaced as plain integers matching FoundryScript::TypeArgumentBinding
// (0 = NONE, 1 = FIXED, 2 = OPEN) because the nested type is private.
class TestFSBytecodeScriptAccessor {
public:
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
			"signal health_changed(amount: int)\n"
			"signal damaged\n"
			"\n"
			"enum Tint { RED, GREEN = 5 }\n"
			"\n"
			"const GREETING = \"hello\"\n"
			"\n"
			"@export var speed: float = 2.5\n"
			"var health: int = 10\n"
			"\n"
			"func _init() -> void:\n"
			"\thealth = 12\n"
			"\n"
			"func take_damage(amount: int) -> int:\n"
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
	CHECK(restored->get_variable_annotations().size() == original->get_variable_annotations().size());
	CHECK(restored->get_method_annotations().size() == original->get_method_annotations().size());
	REQUIRE(restored->get_variable_annotations().has(SNAME("speed")));
	const Vector<FoundryScript::AnnotationUsage> &speed_annotations = restored->get_variable_annotations()[SNAME("speed")];
	REQUIRE(speed_annotations.size() == original->get_variable_annotations()[SNAME("speed")].size());
	CHECK(speed_annotations[0].name == SNAME("export"));
	CHECK(speed_annotations[0].is_builtin);

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
	// outlive the language singleton in the test binary.
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

TEST_CASE("[FoundryScript][BytecodeScript] Conformance witnesses re-register with the registry") {
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
			"\tvar gadget := Gadget.new()\n"
			"\tvar pingable: Pingable = gadget\n"
			"\treturn pingable.ping()\n");
	const String script_path = original->get_script_path();
	const Ref<FoundryScript> original_gadget = original->get_subclasses().find(SNAME("Gadget"))->value;
	const String gadget_key = original_gadget->get_fully_qualified_name();

	// Sanity: the compiled fixture dispatches through the registry.
	{
		const Variant instance_variant = bytecode_new_instance(original);
		Object *instance = instance_variant;
		CHECK((int64_t)bytecode_instance_call(instance, SNAME("run"), {}) == 42);
	}

	FSBytecodeExporter exporter;
	Vector<uint8_t> buffer;
	REQUIRE(exporter.serialize(original, buffer) == OK);

	// Drop the compile-time registration so the assertion below can only be satisfied by the
	// loader's own re-registration.
	FSConformanceRegistry::get_singleton()->clear_runtime_witnesses(script_path);
	CHECK(FSConformanceRegistry::get_singleton()->find_witness_function(gadget_key, SNAME("ping")) == nullptr);

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

	// The trait identity survived on the loaded trait class, and runtime dispatch through the
	// restored graph reaches the witness.
	REQUIRE(restored->get_subclasses().has(SNAME("Pingable")));
	CHECK(restored->get_subclasses().find(SNAME("Pingable"))->value->is_trait_type());
	const Variant instance_variant = bytecode_new_instance(restored);
	Object *instance = instance_variant;
	CHECK((int64_t)bytecode_instance_call(instance, SNAME("run"), {}) == 42);
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

} // namespace FSTests

#endif // TOOLS_ENABLED
