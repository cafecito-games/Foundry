/**************************************************************************/
/*  test_packed_scene.h                                                   */
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

#include "core/object/script_language.h"
#include "scene/resources/packed_scene.h"

#include "tests/test_macros.h"

namespace TestPackedScene {

class TestPackedSceneScript;

class TestPackedSceneScriptInstance : public ScriptInstance {
	Object *owner = nullptr;
	Ref<TestPackedSceneScript> script;

public:
	TestPackedSceneScriptInstance(Object *p_owner, const Ref<TestPackedSceneScript> &p_script) :
			owner(p_owner), script(p_script) {}

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
};

class TestPackedSceneScript : public Script {
	FOUNDRY_CLASS(TestPackedSceneScript, Script);

protected:
	static void _bind_methods() {}

public:
	bool can_instantiate() const override { return true; }
	Ref<Script> get_base_script() const override { return Ref<Script>(); }
	StringName get_global_name() const override { return StringName(); }
	bool inherits_script(const Ref<Script> &p_script) const override { return false; }
	StringName get_instance_base_type() const override { return StringName("Node"); }
	ScriptInstance *instance_create(Object *p_this) override {
		return memnew(TestPackedSceneScriptInstance(p_this, Ref<TestPackedSceneScript>(this)));
	}
	bool instance_has(const Object *p_this) const override { return false; }
	bool has_source_code() const override { return false; }
	String get_source_code() const override { return String(); }
	void set_source_code(const String &p_code) override {}
	Error reload(bool p_keep_state = false) override { return OK; }
	StringName get_doc_class_name() const override { return StringName(); }
	Vector<DocData::ClassDoc> get_documentation() const override { return Vector<DocData::ClassDoc>(); }
	String get_class_icon_path() const override { return String(); }
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

TEST_CASE("[PackedScene] Pack Scene and Retrieve State") {
	// Create a scene to pack.
	Node *scene = memnew(Node);
	scene->set_name("TestScene");

	// Pack the scene.
	PackedScene packed_scene;
	const Error err = packed_scene.pack(scene);
	CHECK(err == OK);

	// Retrieve the packed state.
	Ref<SceneState> state = packed_scene.get_state();
	CHECK(state.is_valid());
	CHECK(state->get_node_count() == 1);
	CHECK(state->get_node_name(0) == "TestScene");

	memdelete(scene);
}

TEST_CASE("[PackedScene] Signals Preserved when Packing Scene") {
	// Create main scene
	// root
	// `- sub_node (local)
	// `- sub_scene (instance of another scene)
	//    `- sub_scene_node (owned by sub_scene)
	Node *main_scene_root = memnew(Node);
	Node *sub_node = memnew(Node);
	Node *sub_scene_root = memnew(Node);
	Node *sub_scene_node = memnew(Node);

	main_scene_root->add_child(sub_node);
	sub_node->set_owner(main_scene_root);

	sub_scene_root->add_child(sub_scene_node);
	sub_scene_node->set_owner(sub_scene_root);

	main_scene_root->add_child(sub_scene_root);
	sub_scene_root->set_owner(main_scene_root);

	SUBCASE("Signals that should be saved") {
		int main_flags = Object::CONNECT_PERSIST;
		// sub node to a node in main scene
		sub_node->connect("ready", callable_mp(main_scene_root, &Node::is_ready), main_flags);
		// subscene root to a node in main scene
		sub_scene_root->connect("ready", callable_mp(main_scene_root, &Node::is_ready), main_flags);
		//subscene root to subscene root (connected within main scene)
		sub_scene_root->connect("ready", callable_mp(sub_scene_root, &Node::is_ready), main_flags);

		// Pack the scene.
		Ref<PackedScene> packed_scene;
		packed_scene.instantiate();
		const Error err = packed_scene->pack(main_scene_root);
		CHECK(err == OK);

		// Make sure the right connections are in packed scene.
		Ref<SceneState> state = packed_scene->get_state();
		CHECK_EQ(state->get_connection_count(), 3);
	}

	/*
	// FIXME: This subcase requires GH-48064 to be fixed.
	SUBCASE("Signals that should not be saved") {
		int subscene_flags = Object::CONNECT_PERSIST | Object::CONNECT_INHERITED;
		// subscene node to itself
		sub_scene_node->connect("ready", callable_mp(sub_scene_node, &Node::is_ready), subscene_flags);
		// subscene node to subscene root
		sub_scene_node->connect("ready", callable_mp(sub_scene_root, &Node::is_ready), subscene_flags);
		//subscene root to subscene root (connected within sub scene)
		sub_scene_root->connect("ready", callable_mp(sub_scene_root, &Node::is_ready), subscene_flags);

		// Pack the scene.
		Ref<PackedScene> packed_scene;
		packed_scene.instantiate();
		const Error err = packed_scene->pack(main_scene_root);
		CHECK(err == OK);

		// Make sure the right connections are in packed scene.
		Ref<SceneState> state = packed_scene->get_state();
		CHECK_EQ(state->get_connection_count(), 0);
	}
	*/

	memdelete(main_scene_root);
}

TEST_CASE("[PackedScene] Clear Packed Scene") {
	// Create a scene to pack.
	Node *scene = memnew(Node);
	scene->set_name("TestScene");

	// Pack the scene.
	PackedScene packed_scene;
	packed_scene.pack(scene);

	// Clear the packed scene.
	packed_scene.clear();

	// Check if it has been cleared.
	Ref<SceneState> state = packed_scene.get_state();
	CHECK_FALSE(state->get_node_count() == 1);

	memdelete(scene);
}

TEST_CASE("[PackedScene] Can Instantiate Packed Scene") {
	// Create a scene to pack.
	Node *scene = memnew(Node);
	scene->set_name("TestScene");

	// Pack the scene.
	PackedScene packed_scene;
	packed_scene.pack(scene);

	// Check if the packed scene can be instantiated.
	const bool can_instantiate = packed_scene.can_instantiate();
	CHECK(can_instantiate == true);

	memdelete(scene);
}

TEST_CASE("[PackedScene] Instantiate Packed Scene") {
	// Create a scene to pack.
	Node *scene = memnew(Node);
	scene->set_name("TestScene");

	// Pack the scene.
	PackedScene packed_scene;
	packed_scene.pack(scene);

	// Instantiate the packed scene.
	Node *instance = packed_scene.instantiate();
	CHECK(instance != nullptr);
	CHECK(instance->get_name() == "TestScene");

	memdelete(scene);
	memdelete(instance);
}

TEST_CASE("[PackedScene] Instantiate Packed Scene With Children") {
	// Create a scene to pack.
	Node *scene = memnew(Node);
	scene->set_name("TestScene");

	// Add persisting child nodes to the scene.
	Node *child1 = memnew(Node);
	child1->set_name("Child1");
	scene->add_child(child1);
	child1->set_owner(scene);

	Node *child2 = memnew(Node);
	child2->set_name("Child2");
	scene->add_child(child2);
	child2->set_owner(scene);

	// Add non persisting child node to the scene.
	Node *child3 = memnew(Node);
	child3->set_name("Child3");
	scene->add_child(child3);

	// Pack the scene.
	PackedScene packed_scene;
	packed_scene.pack(scene);

	// Instantiate the packed scene.
	Node *instance = packed_scene.instantiate();
	CHECK(instance != nullptr);
	CHECK(instance->get_name() == "TestScene");

	// Validate the child nodes of the instantiated scene.
	CHECK(instance->get_child_count() == 2);
	CHECK(instance->get_child(0)->get_name() == "Child1");
	CHECK(instance->get_child(1)->get_name() == "Child2");
	CHECK(instance->get_child(0)->get_owner() == instance);
	CHECK(instance->get_child(1)->get_owner() == instance);

	memdelete(scene);
	memdelete(instance);
}

TEST_CASE("[PackedScene] Set Path") {
	// Create a scene to pack.
	Node *scene = memnew(Node);
	scene->set_name("TestScene");

	// Pack the scene.
	PackedScene packed_scene;
	packed_scene.pack(scene);

	// Set a new path for the packed scene.
	const String new_path = "NewTestPath";
	packed_scene.set_path(new_path);

	// Check if the path has been set correctly.
	Ref<SceneState> state = packed_scene.get_state();
	CHECK(state.is_valid());
	CHECK(state->get_path() == new_path);

	memdelete(scene);
}

TEST_CASE("[PackedScene] Replace State") {
	// Create a scene to pack.
	Node *scene = memnew(Node);
	scene->set_name("TestScene");

	// Pack the scene.
	PackedScene packed_scene;
	packed_scene.pack(scene);

	// Create another scene state to replace with.
	Ref<SceneState> new_state = memnew(SceneState);
	new_state->set_path("NewPath");

	// Replace the state.
	packed_scene.replace_state(new_state);

	// Check if the state has been replaced.
	Ref<SceneState> state = packed_scene.get_state();
	CHECK(state.is_valid());
	CHECK(state == new_state);

	memdelete(scene);
}

TEST_CASE("[PackedScene] Built-in node script is not applied on instantiate") {
	Node *scene = memnew(Node);
	scene->set_name("TestScene");

	Ref<TestPackedSceneScript> built_in_script;
	built_in_script.instantiate();
	built_in_script->set_path_cache("res://test.tscn::Script1");
	REQUIRE(built_in_script->is_built_in());

	scene->set_script(built_in_script);

	PackedScene packed_scene;
	CHECK(packed_scene.pack(scene) == OK);

	Ref<SceneState> state = packed_scene.get_state();
	REQUIRE(state.is_valid());
	bool found = false;
	bool deferred = false;
	const Variant packed_script = state->get_property_value(0, CoreStringName(script), found, deferred);
	REQUIRE(found);
	const Ref<Script> packed_script_ref = packed_script;
	REQUIRE(packed_script_ref.is_valid());
	REQUIRE(packed_script_ref->is_built_in());

	Node *instance = packed_scene.instantiate();
	REQUIRE(instance != nullptr);
	CHECK(instance->get_script().is_null());

	memdelete(scene);
	memdelete(instance);
}

TEST_CASE("[PackedScene] Standalone file script is applied on instantiate") {
	Node *scene = memnew(Node);
	scene->set_name("TestScene");

	Ref<TestPackedSceneScript> file_script;
	file_script.instantiate();
	file_script->set_path_cache("res://test/foo.fs");
	REQUIRE_FALSE(file_script->is_built_in());

	scene->set_script(file_script);

	PackedScene packed_scene;
	CHECK(packed_scene.pack(scene) == OK);

	Ref<SceneState> state = packed_scene.get_state();
	REQUIRE(state.is_valid());
	bool found = false;
	bool deferred = false;
	const Variant packed_script = state->get_property_value(0, CoreStringName(script), found, deferred);
	REQUIRE(found);
	const Ref<Script> packed_script_ref = packed_script;
	REQUIRE(packed_script_ref.is_valid());
	REQUIRE_FALSE(packed_script_ref->is_built_in());

	Node *instance = packed_scene.instantiate();
	REQUIRE(instance != nullptr);
	const Ref<Script> instance_script = instance->get_script();
	CHECK(instance_script.is_valid());
	CHECK(instance_script->get_path() == "res://test/foo.fs");

	memdelete(scene);
	memdelete(instance);
}

TEST_CASE("[PackedScene] Recreate State") {
	// Create a scene to pack.
	Node *scene = memnew(Node);
	scene->set_name("TestScene");

	// Pack the scene.
	PackedScene packed_scene;
	packed_scene.pack(scene);

	// Recreate the state.
	packed_scene.recreate_state();

	// Check if the state has been recreated.
	Ref<SceneState> state = packed_scene.get_state();
	CHECK(state.is_valid());
	CHECK(state->get_node_count() == 0); // Since the state was recreated, it should be empty.

	memdelete(scene);
}

} // namespace TestPackedScene
