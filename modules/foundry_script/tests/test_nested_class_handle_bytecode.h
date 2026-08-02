/**************************************************************************/
/*  test_nested_class_handle_bytecode.h                                   */
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

// Shared bytecode fixtures (in-process compile, test resolver) and the error recorder that lets a
// rejection be asserted on by reason. Every module test header is compiled into one generated test
// translation unit, so these includes do not duplicate test registrations.
#include "test_bytecode_serialization.h"
#include "test_nested_class_handle_container.h"

#include "core/io/dir_access.h"
#include "core/variant/container_type_validate.h"
#include "scene/main/node.h"

#include "tests/test_macros.h"

// End-to-end coverage for nested `Type[T]` under the condition a shipped game actually runs in: the
// script comes from a `.fsb` and no parser or analyzer state is available for it. Every assertion
// below is on executed behavior — a call returning, a write being accepted, a write being rejected
// for a stated reason — rather than on the encoded bytes, because a descriptor silently downgraded
// to its instance-typed form still accepts every valid case and only misbehaves on rejection.

namespace FSTests {

// Compiles the source, serializes it, then removes the on-disk script so nothing can re-parse it,
// and rebuilds the script from the bytes alone through the production skeleton/full load pair.
static Ref<FoundryScript> load_nested_handle_bytecode(const String &p_source,
		FSBytecodeExternalResolver *p_resolver, Ref<FoundryScript> *r_original = nullptr) {
	const Ref<FoundryScript> original = compile_bytecode_test_source(p_source);

	FSBytecodeExporter exporter;
	Vector<uint8_t> buffer;
	REQUIRE(exporter.serialize(original, buffer) == OK);

	const String script_path = original->get_script_path();
	if (r_original != nullptr) {
		*r_original = original;
	}

	// The front end is unavailable from here on: the analyzer and parser that produced the script
	// are already destroyed, and the source they read no longer exists.
	REQUIRE(DirAccess::remove_absolute(script_path) == OK);
	REQUIRE_FALSE(FileAccess::exists(script_path));

	Ref<FoundryScript> restored;
	restored.instantiate();
	restored->set_path_cache(script_path);

	FSBytecodeLoader loader;
	loader.set_resolver(p_resolver);
	REQUIRE(loader.load_skeleton(buffer, restored) == OK);
	REQUIRE(loader.load_full(buffer, restored) == OK);
	REQUIRE(restored->is_valid());
	CHECK(restored->is_compiled_binary());
	return restored;
}

static Object *instantiate_nested_handle_script(const Ref<FoundryScript> &p_script, Variant &r_owner) {
	Callable::CallError call_error;
	r_owner = p_script->_new(nullptr, -1, call_error);
	REQUIRE(call_error.error == Callable::CallError::CALL_OK);
	Object *instance = r_owner;
	REQUIRE(instance != nullptr);
	return instance;
}

// A freed object exercises the "invalid or freed" branch without depending on any handle type.
struct FreedHandle {
	Variant value;

	FreedHandle() {
		Object *object = memnew(Object);
		value = object;
		memdelete(object);
	}
};

// Compares two descriptors node by node, so a handle flag lost anywhere below the root is reported
// at the node that lost it rather than as one opaque inequality.
static void check_container_types_match(const ContainerType &p_expected, const ContainerType &p_actual, const String &p_where) {
	CAPTURE(p_where);
	CHECK(p_actual.builtin_type == p_expected.builtin_type);
	CHECK(p_actual.class_name == p_expected.class_name);
	CHECK(p_actual.is_type_handle == p_expected.is_type_handle);
	CHECK(p_actual.script.is_valid() == p_expected.script.is_valid());
	REQUIRE(p_actual.element_types.size() == p_expected.element_types.size());
	for (int i = 0; i < p_expected.element_types.size(); i++) {
		check_container_types_match(p_expected.element_types[i], p_actual.element_types[i],
				vformat("%s.element_types[%d]", p_where, i));
	}
	REQUIRE(p_actual.type_arguments.size() == p_expected.type_arguments.size());
	for (int i = 0; i < p_expected.type_arguments.size(); i++) {
		check_container_types_match(p_expected.type_arguments[i], p_actual.type_arguments[i],
				vformat("%s.type_arguments[%d]", p_where, i));
	}
}

TEST_CASE("[FoundryScript][Bytecode][NestedClassHandle] Container members keep their handle descriptors without a front end") {
	BytecodeTestResolver resolver;
	Ref<FoundryScript> original;
	const Ref<FoundryScript> restored = load_nested_handle_bytecode(
			"extends RefCounted\n"
			"\n"
			"class Other extends RefCounted:\n"
			"\tpass\n"
			"\n"
			"var nodes: Array[Type[Node]] = []\n"
			"var instances: Array[Node] = []\n"
			"var registry: Dictionary[String, Type[Other]] = {}\n"
			"var grouped: Array[Dictionary[String, Type[Other]]] = []\n"
			"\n"
			"func node_handle() -> Type[Node]:\n"
			"\treturn Node\n"
			"\n"
			"func other_handle() -> Type[Other]:\n"
			"\treturn Other\n"
			"\n"
			"func empty_group() -> Dictionary[String, Type[Other]]:\n"
			"\treturn {}\n",
			&resolver, &original);

	Variant original_owner;
	Object *original_instance = instantiate_nested_handle_script(original, original_owner);
	Variant restored_owner;
	Object *restored_instance = instantiate_nested_handle_script(restored, restored_owner);

	// The restored descriptors are the analyzer's descriptors, at every recursive node.
	const Array original_nodes = original_instance->get(SNAME("nodes"));
	Array nodes = restored_instance->get(SNAME("nodes"));
	check_container_types_match(original_nodes.get_element_type(), nodes.get_element_type(), "nodes");
	CHECK(nodes.get_element_type().is_type_handle);
	CHECK(nodes.get_element_type().get_type_name() == "Type[Node]");

	// An instance-typed container in the same script is unaffected, so the flag is not restored
	// indiscriminately for every object element.
	const Array instances = restored_instance->get(SNAME("instances"));
	CHECK_FALSE(instances.get_element_type().is_type_handle);

	const Dictionary original_registry = original_instance->get(SNAME("registry"));
	Dictionary registry = restored_instance->get(SNAME("registry"));
	check_container_types_match(original_registry.get_key_type(), registry.get_key_type(), "registry.key");
	check_container_types_match(original_registry.get_value_type(), registry.get_value_type(), "registry.value");
	CHECK_FALSE(registry.get_key_type().is_type_handle);
	CHECK(registry.get_value_type().is_type_handle);

	const Array original_grouped = original_instance->get(SNAME("grouped"));
	Array grouped = restored_instance->get(SNAME("grouped"));
	check_container_types_match(original_grouped.get_element_type(), grouped.get_element_type(), "grouped");
	const ContainerType grouped_element = grouped.get_element_type();
	REQUIRE(grouped_element.element_types.size() == 2);
	CHECK_FALSE(grouped_element.element_types[0].is_type_handle);
	CHECK(grouped_element.element_types[1].is_type_handle);
	// The leaf handle denotes the loaded file's own class object, not the one the exporter held.
	REQUIRE(restored->get_subclasses().has(SNAME("Other")));
	CHECK(grouped_element.element_types[1].script == restored->get_subclasses().find(SNAME("Other"))->value);

	const Variant node_handle = restored_instance->call(SNAME("node_handle"));
	const Variant other_handle = restored_instance->call(SNAME("other_handle"));

	// Acceptance, through a native mutation method rather than compiled script code, so the
	// restored descriptor is what does the validating.
	nodes.push_back(node_handle);
	CHECK(nodes.size() == 1);
	CHECK(registry.set("other", other_handle));
	CHECK(registry.size() == 1);

	NestedHandleErrorRecorder recorder;
	ERR_PRINT_OFF;

	// An instance of the represented type is not a handle for it.
	Node *node = memnew(Node);
	nodes.push_back(Variant(node));
	CHECK(nodes.size() == 1);
	CHECK(recorder.messages.contains("requires a class handle"));

	// A handle for an unrelated class is reported as an incompatible represented type, not as a
	// failure of the handle object's own class.
	recorder.clear();
	nodes.push_back(other_handle);
	CHECK(nodes.size() == 1);
	CHECK(recorder.messages.contains("represented type is not compatible"));
	CHECK_FALSE(recorder.messages.contains("does not inherit"));

	recorder.clear();
	FreedHandle freed;
	nodes.push_back(freed.value);
	CHECK(nodes.size() == 1);
	CHECK(recorder.messages.contains("previously freed"));

	// A Variant-typed write into the loaded dictionary is validated by the same restored descriptor.
	recorder.clear();
	const Variant dynamic_instance = Variant(node);
	CHECK_FALSE(registry.set("bad", dynamic_instance));
	CHECK(registry.size() == 1);
	CHECK(recorder.messages.contains("requires a class handle"));

	// Deep nesting validates at the leaf, not only at the outermost node.
	recorder.clear();
	Dictionary accepted_group = restored_instance->call(SNAME("empty_group"));
	CHECK(accepted_group.set("other", other_handle));
	grouped.push_back(accepted_group);
	CHECK(grouped.size() == 1);
	CHECK_FALSE(accepted_group.set("bad", Variant(node)));
	CHECK(recorder.messages.contains("requires a class handle"));

	// The four rejection messages are the ones the same writes produce with the front end present,
	// so a restored descriptor is not merely rejecting, it is rejecting for the same reasons.
	Array original_nodes_writable = original_instance->get(SNAME("nodes"));
	Dictionary original_registry_writable = original_instance->get(SNAME("registry"));
	const auto collect_rejections = [&](Array &r_array, Dictionary &r_dictionary, const Variant &p_other_handle) {
		NestedHandleErrorRecorder rejection_recorder;
		r_array.push_back(Variant(node));
		r_array.push_back(p_other_handle);
		r_array.push_back(freed.value);
		r_dictionary.set("bad", Variant(node));
		return rejection_recorder.messages;
	};
	const String restored_rejections = collect_rejections(nodes, registry, other_handle);
	const Variant original_other_handle = original_instance->call(SNAME("other_handle"));
	const String original_rejections = collect_rejections(original_nodes_writable, original_registry_writable, original_other_handle);
	CHECK_FALSE(restored_rejections.is_empty());
	CHECK(restored_rejections == original_rejections);

	ERR_PRINT_ON;
	memdelete(node);
}

TEST_CASE("[FoundryScript][Bytecode][NestedClassHandle] The trait registry drives a concrete product from bytecode") {
	BytecodeTestResolver resolver;
	const Ref<FoundryScript> restored = load_nested_handle_bytecode(
			"extends RefCounted\n"
			"\n"
			"trait Factory extends RefCounted:\n"
			"\tabstract static func create() -> Self\n"
			"\n"
			"class User extends RefCounted:\n"
			"\tuses Factory\n"
			"\n"
			"\tstatic func create() -> User:\n"
			"\t\treturn User.new()\n"
			"\n"
			"var types: Dictionary[String, Type[Factory]] = {}\n"
			"\n"
			"func seed() -> void:\n"
			"\ttypes[\"user\"] = User\n"
			"\n"
			"func register(name: String, factory: Type[Factory]) -> Type[Factory]:\n"
			"\ttypes[name] = factory\n"
			"\treturn factory\n"
			"\n"
			"func build(name: String) -> Factory:\n"
			"\tvar factory: Type[Factory] = types[name]\n"
			"\treturn factory.create()\n"
			"\n"
			"func built_is_user(name: String) -> bool:\n"
			"\treturn build(name) is User\n"
			"\n"
			"func user_handle() -> Type[Factory]:\n"
			"\treturn User\n"
			"\n"
			"func node_handle() -> Type[Node]:\n"
			"\treturn Node\n",
			&resolver);

	Variant owner;
	Object *instance = instantiate_nested_handle_script(restored, owner);

	// The motivating example: store a conforming handle, retrieve it, call the static factory
	// requirement promised by the trait, and receive the concrete product.
	instance->call(SNAME("seed"));
	CHECK(bool(instance->call(SNAME("built_is_user"), "user")));

	// An argument slot and a return slot carry the handle through a compiled call.
	const Variant user_handle = instance->call(SNAME("user_handle"));
	const Variant registered = instance->call(SNAME("register"), "second", user_handle);
	CHECK(registered == user_handle);
	CHECK(bool(instance->call(SNAME("built_is_user"), "second")));

	Dictionary types = instance->get(SNAME("types"));
	CHECK(types.size() == 2);
	CHECK(types.get_value_type().is_type_handle);

	NestedHandleErrorRecorder recorder;
	ERR_PRINT_OFF;

	Ref<RefCounted> product = instance->call(SNAME("build"), "user");
	REQUIRE(product.is_valid());
	CHECK_FALSE(types.set("instance", Variant(product)));
	CHECK(recorder.messages.contains("requires a class handle"));

	// A native class handle never satisfies a trait-typed slot, because retroactive conformance is
	// a property of scripts.
	recorder.clear();
	const Variant node_handle = instance->call(SNAME("node_handle"));
	CHECK_FALSE(types.set("node", node_handle));
	CHECK(recorder.messages.contains("represented type is not compatible"));

	recorder.clear();
	FreedHandle freed;
	CHECK_FALSE(types.set("freed", freed.value));
	CHECK(recorder.messages.contains("previously freed"));

	CHECK(types.size() == 2);
	ERR_PRINT_ON;
}

TEST_CASE("[FoundryScript][Bytecode][NestedClassHandle] A nested generic binding stays distinct from its instance form") {
	BytecodeTestResolver resolver;
	const Ref<FoundryScript> restored = load_nested_handle_bytecode(
			"extends RefCounted\n"
			"\n"
			"class User extends RefCounted:\n"
			"\tpass\n"
			"\n"
			"class Slot[T] extends RefCounted:\n"
			"\tvar value: T\n"
			"\n"
			"class HandleSlot extends Slot[Type[User]]:\n"
			"\tpass\n"
			"\n"
			"class InstanceSlot extends Slot[User]:\n"
			"\tpass\n"
			"\n"
			"func handle_slot() -> HandleSlot:\n"
			"\treturn HandleSlot.new()\n"
			"\n"
			"func instance_slot() -> InstanceSlot:\n"
			"\treturn InstanceSlot.new()\n"
			"\n"
			"func user_handle() -> Type[User]:\n"
			"\treturn User\n"
			"\n"
			"func user_instance() -> User:\n"
			"\treturn User.new()\n",
			&resolver);

	Variant owner;
	Object *instance = instantiate_nested_handle_script(restored, owner);

	const Variant handle_slot_value = instance->call(SNAME("handle_slot"));
	Object *handle_slot = handle_slot_value;
	REQUIRE(handle_slot != nullptr);
	const Variant instance_slot_value = instance->call(SNAME("instance_slot"));
	Object *instance_slot = instance_slot_value;
	REQUIRE(instance_slot != nullptr);

	const Variant user_handle = instance->call(SNAME("user_handle"));
	const Variant user_instance = instance->call(SNAME("user_instance"));

	handle_slot->set(SNAME("value"), user_handle);
	CHECK(handle_slot->get(SNAME("value")) == user_handle);
	instance_slot->set(SNAME("value"), user_instance);
	CHECK(instance_slot->get(SNAME("value")) == user_instance);

	NestedHandleErrorRecorder recorder;
	ERR_PRINT_OFF;

	// `Slot[Type[User]]` is not `Slot[User]`: neither binding accepts the other's values.
	handle_slot->set(SNAME("value"), user_instance);
	CHECK(handle_slot->get(SNAME("value")) == user_handle);
	CHECK(recorder.messages.contains("requires a class handle"));

	recorder.clear();
	instance_slot->set(SNAME("value"), user_handle);
	CHECK(instance_slot->get(SNAME("value")) == user_instance);
	CHECK_FALSE(recorder.messages.is_empty());

	recorder.clear();
	FreedHandle freed;
	handle_slot->set(SNAME("value"), freed.value);
	CHECK(handle_slot->get(SNAME("value")) == user_handle);
	CHECK(recorder.messages.contains("previously freed"));

	ERR_PRINT_ON;
}

TEST_CASE("[FoundryScript][Bytecode][NestedClassHandle] A specialized handle element keeps its specialization") {
	BytecodeTestResolver resolver;
	const Ref<FoundryScript> restored = load_nested_handle_bytecode(
			"extends RefCounted\n"
			"\n"
			"class Box[T] extends RefCounted:\n"
			"\tvar value: T\n"
			"\n"
			"var boxes: Array[Type[Box[int]]] = []\n"
			"\n"
			"func int_box() -> Type[Box[int]]:\n"
			"\treturn Box[int]\n"
			"\n"
			"func string_box() -> Type[Box[String]]:\n"
			"\treturn Box[String]\n"
			"\n"
			"func int_box_instance() -> Box[int]:\n"
			"\treturn Box[int].new()\n",
			&resolver);

	Variant owner;
	Object *instance = instantiate_nested_handle_script(restored, owner);

	Array boxes = instance->get(SNAME("boxes"));
	CHECK(boxes.get_element_type().is_type_handle);

	const Variant int_box = instance->call(SNAME("int_box"));
	boxes.push_back(int_box);
	CHECK(boxes.size() == 1);

	NestedHandleErrorRecorder recorder;
	ERR_PRINT_OFF;

	// The specialization is what distinguishes the two handles; losing the type arguments on the way
	// through `.fsb` would make this write succeed.
	const Variant string_box = instance->call(SNAME("string_box"));
	boxes.push_back(string_box);
	CHECK(boxes.size() == 1);
	CHECK(recorder.messages.contains("specialized as"));

	recorder.clear();
	const Variant int_box_instance = instance->call(SNAME("int_box_instance"));
	boxes.push_back(int_box_instance);
	CHECK(boxes.size() == 1);
	CHECK(recorder.messages.contains("requires a class handle"));

	recorder.clear();
	FreedHandle freed;
	boxes.push_back(freed.value);
	CHECK(boxes.size() == 1);
	CHECK(recorder.messages.contains("previously freed"));

	ERR_PRINT_ON;
}

TEST_CASE("[FoundryScript][Bytecode][NestedClassHandle] Callable and Signal signatures carry handles through bytecode") {
	BytecodeTestResolver resolver;
	const Ref<FoundryScript> restored = load_nested_handle_bytecode(
			"extends RefCounted\n"
			"\n"
			"signal registered(factory: Type[RefCounted])\n"
			"\n"
			"var construct: Callable[[Type[RefCounted]], RefCounted]\n"
			"var last_received: Type[RefCounted] = RefCounted\n"
			"\n"
			"func _on_registered(factory: Type[RefCounted]) -> void:\n"
			"\tlast_received = factory\n"
			"\n"
			"func wire_signal() -> void:\n"
			"\tregistered.connect(_on_registered)\n"
			"\n"
			"func fire(factory: Type[RefCounted]) -> void:\n"
			"\tregistered.emit(factory)\n"
			"\n"
			"func make(factory: Type[RefCounted]) -> RefCounted:\n"
			"\treturn factory.new()\n"
			"\n"
			"func wire() -> void:\n"
			"\tconstruct = make\n"
			"\n"
			"func build() -> RefCounted:\n"
			"\treturn construct.call(RefCounted)\n"
			"\n"
			"func assign(value: Variant) -> void:\n"
			"\tlast_received = value\n"
			"\n"
			"func refcounted_handle() -> Type[RefCounted]:\n"
			"\treturn RefCounted\n"
			"\n"
			"func refcounted_instance() -> RefCounted:\n"
			"\treturn RefCounted.new()\n"
			"\n"
			"func node_handle() -> Type[Node]:\n"
			"\treturn Node\n",
			&resolver);

	Variant owner;
	Object *instance = instantiate_nested_handle_script(restored, owner);

	// A Callable slot spells its handle parameter out, so a handle slot cannot decode as an
	// instance slot.
	List<PropertyInfo> properties;
	restored->get_script_property_list(&properties);
	bool found_construct = false;
	for (const PropertyInfo &property : properties) {
		if (property.name == SNAME("construct")) {
			found_construct = true;
			CHECK(property.hint == PROPERTY_HINT_CALLABLE_TYPE);
			CHECK(property.hint_string == "[[Type[RefCounted]], RefCounted]");
		}
	}
	CHECK(found_construct);

	// The wired callable still constructs through the handle after loading.
	instance->call(SNAME("wire"));
	const Variant built = instance->call(SNAME("build"));
	Object *built_object = built;
	CHECK(built_object != nullptr);

	REQUIRE(restored->get_signals().has(SNAME("registered")));
	const MethodInfo &signal_info = restored->get_signals()[SNAME("registered")];
	REQUIRE(signal_info.arguments.size() == 1);
	CHECK(signal_info.arguments[0].class_name == StringName("Type[RefCounted]"));

	// Connect and emit carry the handle end to end.
	const Variant emitted_handle = instance->call(SNAME("refcounted_handle"));
	instance->call(SNAME("wire_signal"));
	instance->call(SNAME("fire"), emitted_handle);
	CHECK(instance->get(SNAME("last_received")) == emitted_handle);

	// The receiving slot keeps a handle descriptor, so a dynamic write of anything but a compatible
	// handle is rejected, naming the handle type rather than the erased instance type.
	NestedHandleErrorRecorder recorder;
	ERR_PRINT_OFF;

	const Variant instance_value = instance->call(SNAME("refcounted_instance"));
	instance->call(SNAME("assign"), instance_value);
	CHECK(recorder.messages.contains("Trying to assign value of type 'RefCounted'"));
	CHECK(recorder.messages.contains("Type[RefCounted]"));

	recorder.clear();
	const Variant node_handle = instance->call(SNAME("node_handle"));
	instance->call(SNAME("assign"), node_handle);
	CHECK(recorder.messages.contains("Trying to assign value of type 'Node'"));
	CHECK(recorder.messages.contains("Type[RefCounted]"));

	recorder.clear();
	FreedHandle freed;
	instance->call(SNAME("assign"), freed.value);
	CHECK_FALSE(recorder.messages.is_empty());

	CHECK(instance->get(SNAME("last_received")) == emitted_handle);
	ERR_PRINT_ON;
}

TEST_CASE("[FoundryScript][Bytecode][NestedClassHandle] A constant carrying a nested handle validates after loading") {
	// The represented class lives in its own file so the constant's class reference travels as an
	// external identity; a class local to the same file cannot appear inside a container constant
	// today for reasons unrelated to class handles.
	const String user_path = TestUtils::get_temp_path("nested_class_handle_user.fs");
	{
		Ref<FileAccess> user_file = FileAccess::open(user_path, FileAccess::WRITE);
		REQUIRE(user_file.is_valid());
		user_file->store_string(
				"extends RefCounted\n"
				"\n"
				"static func create() -> RefCounted:\n"
				"\treturn RefCounted.new()\n");
	}

	const String source = vformat(
			"extends RefCounted\n"
			"\n"
			"const UserClass = preload(\"%s\")\n"
			"\n"
			"const DEFAULT_TYPES: Dictionary[String, Type[UserClass]] = { \"user\": UserClass }\n"
			"\n"
			"func defaults() -> Dictionary[String, Type[UserClass]]:\n"
			"\treturn DEFAULT_TYPES.duplicate()\n"
			"\n"
			"func user_handle() -> Type[UserClass]:\n"
			"\treturn UserClass\n"
			"\n"
			"func user_instance() -> UserClass:\n"
			"\treturn UserClass.new()\n"
			"\n"
			"func node_handle() -> Type[Node]:\n"
			"\treturn Node\n",
			user_path);

	// The external class has to be resolvable at load time; the compiled file only names it.
	Ref<FoundryScript> probe_original;
	{
		const Ref<FoundryScript> probe = compile_bytecode_test_source(source);
		probe_original = probe;
	}
	const Ref<FoundryScript> user_script = probe_original->get_constants()[SNAME("UserClass")];
	REQUIRE(user_script.is_valid());

	BytecodeTestResolver resolver;
	resolver.scripts.insert(user_script->get_script_path() + "::" + user_script->get_fully_qualified_name(), user_script);
	const Ref<FoundryScript> restored = load_nested_handle_bytecode(source, &resolver);

	Variant owner;
	Object *instance = instantiate_nested_handle_script(restored, owner);

	Dictionary defaults = instance->call(SNAME("defaults"));
	CHECK(defaults.get_value_type().is_type_handle);
	CHECK(defaults.size() == 1);

	const Variant user_handle = instance->call(SNAME("user_handle"));
	CHECK(defaults.set("second", user_handle));
	CHECK(defaults.size() == 2);

	NestedHandleErrorRecorder recorder;
	ERR_PRINT_OFF;

	const Variant user_instance = instance->call(SNAME("user_instance"));
	CHECK_FALSE(defaults.set("bad", user_instance));
	CHECK(recorder.messages.contains("requires a class handle"));

	recorder.clear();
	const Variant node_handle = instance->call(SNAME("node_handle"));
	CHECK_FALSE(defaults.set("node", node_handle));
	CHECK(recorder.messages.contains("represented type is not compatible"));

	recorder.clear();
	FreedHandle freed;
	CHECK_FALSE(defaults.set("freed", freed.value));
	CHECK(recorder.messages.contains("previously freed"));

	CHECK(defaults.size() == 2);
	ERR_PRINT_ON;
}

} // namespace FSTests

#endif // TOOLS_ENABLED
