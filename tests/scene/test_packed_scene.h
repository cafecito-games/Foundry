/**************************************************************************/
/*  test_packed_scene.h                                                   */
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

#include "core/io/file_access.h"
#include "core/io/resource_loader.h"
#include "core/io/resource_saver.h"
#include "core/object/class_db.h"
#include "core/object/script_language.h"
#include "scene/main/missing_node.h"
#include "scene/resources/packed_scene.h"

#include "tests/test_macros.h"
#include "tests/test_utils.h"

#ifndef _3D_DISABLED
#include "scene/3d/mesh_instance_3d.h"
#endif // _3D_DISABLED

// Declared in the global namespace because of the `FOUNDRY_CLASS` friend-declaration warning on
// Windows when the macro is expanded inside a namespace.
class _PackedSceneNamespacedNode : public Node {
	FOUNDRY_CLASS(_PackedSceneNamespacedNode, Node);
};

class _PackedSceneOtherNamespacedNode : public Node {
	FOUNDRY_CLASS(_PackedSceneOtherNamespacedNode, Node);
};

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

#ifndef _3D_DISABLED
TEST_CASE("[SceneTree][PackedScene] Unsigned native property round-trips through a binary scene") {
	// `VisualInstance3D::get_layer_mask()` returns `uint32_t`, so this is the shortest path from an
	// unsigned C++ result to binary resource persistence and back through `PackedScene::instantiate()`.
	constexpr uint32_t layer_mask = 0xF0F0F0F0;

	MeshInstance3D *source = memnew(MeshInstance3D);
	source->set_name("UnsignedCarrier");
	source->set_layer_mask(layer_mask);
	CHECK(source->get("layers").get_type() == Variant::UINT);

	Ref<PackedScene> packed;
	packed.instantiate();
	REQUIRE(packed->pack(source) == OK);

	const String scene_path = TestUtils::get_temp_path("packed_scene_unsigned_carrier.scn");
	REQUIRE(ResourceSaver::save(packed, scene_path) == OK);

	Error error = FAILED;
	Ref<PackedScene> loaded = ResourceLoader::load(scene_path, "", ResourceFormatLoader::CACHE_MODE_IGNORE, &error);
	REQUIRE(error == OK);
	REQUIRE(loaded.is_valid());

	Node *instance = loaded->instantiate();
	REQUIRE(instance != nullptr);
	MeshInstance3D *restored = Object::cast_to<MeshInstance3D>(instance);
	REQUIRE(restored != nullptr);
	CHECK(restored->get_layer_mask() == layer_mask);
	CHECK(restored->get("layers").get_type() == Variant::UINT);
	CHECK(restored->get("layers").operator uint64_t() == uint64_t(layer_mask));

	memdelete(instance);
	memdelete(source);
}
#endif // _3D_DISABLED

// Saves a scene holding unsigned and signed node metadata, reloads it from disk, and checks that
// both carriers and both exact values survived the chosen scene format.
static void check_scene_metadata_round_trip(const String &p_extension) {
	Node *source = memnew(Node);
	source->set_name("UnsignedMetadata");
	source->set_meta("unsigned_zero", Variant(uint64_t(0)));
	source->set_meta("unsigned_above_signed_max", Variant(uint64_t(INT64_MAX) + 1));
	source->set_meta("unsigned_maximum", Variant(UINT64_MAX));
	source->set_meta("signed_minimum", Variant(int64_t(INT64_MIN)));

	Ref<PackedScene> packed;
	packed.instantiate();
	REQUIRE_EQ(packed->pack(source), OK);

	const String scene_path = TestUtils::get_temp_path("packed_scene_unsigned_metadata." + p_extension);
	REQUIRE_EQ(ResourceSaver::save(packed, scene_path), OK);

	Error error = FAILED;
	Ref<PackedScene> loaded = ResourceLoader::load(scene_path, "", ResourceFormatLoader::CACHE_MODE_IGNORE, &error);
	REQUIRE_EQ(error, OK);
	REQUIRE(loaded.is_valid());

	Node *instance = loaded->instantiate();
	REQUIRE_NE(instance, nullptr);

	struct ExpectedUnsigned {
		const char *key;
		uint64_t value;
	};
	const ExpectedUnsigned expectations[] = {
		{ "unsigned_zero", 0 },
		{ "unsigned_above_signed_max", uint64_t(INT64_MAX) + 1 },
		{ "unsigned_maximum", UINT64_MAX },
	};
	for (const ExpectedUnsigned &expectation : expectations) {
		const Variant restored = instance->get_meta(expectation.key);
		CHECK_MESSAGE(restored.get_type() == Variant::UINT, expectation.key);
		CHECK_MESSAGE(restored.operator uint64_t() == expectation.value, expectation.key);
	}

	const Variant restored_signed = instance->get_meta("signed_minimum");
	CHECK_EQ(restored_signed.get_type(), Variant::INT);
	CHECK_EQ(restored_signed.operator int64_t(), INT64_MIN);

	memdelete(instance);
	memdelete(source);
}

TEST_CASE("[SceneTree][PackedScene][UInt] Text scenes round-trip unsigned node metadata") {
	check_scene_metadata_round_trip("tscn");
}

TEST_CASE("[SceneTree][PackedScene][UInt] Binary scenes round-trip unsigned node metadata") {
	check_scene_metadata_round_trip("scn");
}

// Writes a one-node text scene whose root carries `p_root_type` verbatim, loads it back through the
// resource loader and returns the instantiated root. The caller owns the returned node.
static Node *instantiate_text_scene_with_root_type(const String &p_root_type, const String &p_file_name) {
	const String scene_path = TestUtils::get_temp_path(p_file_name);
	{
		Ref<FileAccess> file = FileAccess::open(scene_path, FileAccess::WRITE);
		REQUIRE(file.is_valid());
		file->store_string("[gd_scene format=3]\n\n[node name=\"Root\" type=\"" + p_root_type + "\"]\n");
	}

	Error error = FAILED;
	Ref<PackedScene> loaded = ResourceLoader::load(scene_path, "", ResourceFormatLoader::CACHE_MODE_IGNORE, &error);
	REQUIRE_EQ(error, OK);
	REQUIRE(loaded.is_valid());

	return loaded->instantiate();
}

// Repacks `p_root` and reports the type string that would be written back to disk for it.
static StringName repacked_root_type(Node *p_root) {
	Ref<PackedScene> packed;
	packed.instantiate();
	REQUIRE_EQ(packed->pack(p_root), OK);
	Ref<SceneState> state = packed->get_state();
	REQUIRE(state.is_valid());
	REQUIRE_GE(state->get_node_count(), 1);
	return state->get_node_type(0);
}

TEST_CASE("[SceneTree][PackedScene][ClassDBNamespace] A text scene loads a qualified namespaced native type") {
	const String qualified_name = "foundry.http.server.HTTPServer";

	Node *root = instantiate_text_scene_with_root_type(qualified_name, "packed_scene_namespaced_root.tscn");
	REQUIRE_NE(root, nullptr);

	CHECK_EQ(Object::cast_to<MissingNode>(root), nullptr);
	CHECK_EQ(root->get_class(), qualified_name);
	CHECK(root->is_class("Node"));
	// Re-saving writes the same qualified string back.
	CHECK_EQ(repacked_root_type(root), StringName(qualified_name));

	memdelete(root);
}

TEST_CASE("[SceneTree][PackedScene][ClassDBNamespace] Unresolvable types become a MissingNode preserving the on-disk string") {
	const bool previous = ResourceLoader::is_creating_missing_resources_if_class_unavailable_enabled();
	ResourceLoader::set_create_missing_resources_if_class_unavailable(true);

	SUBCASE("An unknown qualified type keeps its namespace") {
		const String unknown_qualified_name = "foundry.test.absent.DoesNotExist";

		ERR_PRINT_OFF;
		Node *root = instantiate_text_scene_with_root_type(unknown_qualified_name, "packed_scene_unknown_qualified.tscn");
		ERR_PRINT_ON;
		REQUIRE_NE(root, nullptr);

		MissingNode *missing = Object::cast_to<MissingNode>(root);
		REQUIRE_NE(missing, nullptr);
		CHECK_EQ(missing->get_original_class(), unknown_qualified_name);
		CHECK_EQ(repacked_root_type(root), StringName(unknown_qualified_name));

		memdelete(root);
	}

	SUBCASE("The bare name of a namespaced class does not resolve without an alias") {
		// `HTTPServer` lives in `foundry.http.server` and registers no global alias, so a scene saved
		// with the bare name must not silently pick the namespaced class up.
		REQUIRE_EQ(ClassDB::resolve_type_name("HTTPServer"), StringName());

		ERR_PRINT_OFF;
		Node *root = instantiate_text_scene_with_root_type("HTTPServer", "packed_scene_bare_namespaced_root.tscn");
		ERR_PRINT_ON;
		REQUIRE_NE(root, nullptr);

		MissingNode *missing = Object::cast_to<MissingNode>(root);
		REQUIRE_NE(missing, nullptr);
		CHECK_EQ(missing->get_original_class(), String("HTTPServer"));
		CHECK_EQ(repacked_root_type(root), StringName("HTTPServer"));

		memdelete(root);
	}

	ResourceLoader::set_create_missing_resources_if_class_unavailable(previous);
}

TEST_CASE("[SceneTree][PackedScene][ClassDBNamespace] Scene types resolve through global aliases") {
	const StringName qualified_name = "foundry.test.scene._PackedSceneNamespacedNode";
	const StringName other_qualified_name = "foundry.test.scene.other._PackedSceneOtherNamespacedNode";
	const StringName alias = "_PackedSceneAliasedNode";

	// Namespacing rekeys the registry once, so the shared setup must not repeat per subcase.
	static bool namespaces_registered = false;
	if (!namespaces_registered) {
		namespaces_registered = true;
		FOUNDRY_REGISTER_CLASS(_PackedSceneNamespacedNode);
		FOUNDRY_REGISTER_CLASS(_PackedSceneOtherNamespacedNode);
		FOUNDRY_REGISTER_NAMESPACE(_PackedSceneNamespacedNode, "foundry.test.scene");
		FOUNDRY_REGISTER_NAMESPACE(_PackedSceneOtherNamespacedNode, "foundry.test.scene.other");
		ClassDB::class_register_global_alias(qualified_name, alias);
	}

	const bool previous = ResourceLoader::is_creating_missing_resources_if_class_unavailable_enabled();
	ResourceLoader::set_create_missing_resources_if_class_unavailable(true);

	SUBCASE("A unique alias instantiates the qualified class and re-saves qualified") {
		REQUIRE_EQ(ClassDB::resolve_type_name(alias), qualified_name);

		Node *root = instantiate_text_scene_with_root_type(alias, "packed_scene_aliased_root.tscn");
		REQUIRE_NE(root, nullptr);

		CHECK_EQ(Object::cast_to<MissingNode>(root), nullptr);
		CHECK_EQ(root->get_class(), String(qualified_name));
		// The alias is a load-time re-export only; saving normalizes to the canonical key.
		CHECK_EQ(repacked_root_type(root), qualified_name);

		memdelete(root);
	}

	SUBCASE("An alias claimed by two classes is ambiguous and yields a MissingNode") {
		ClassDB::class_register_global_alias(other_qualified_name, alias);
		REQUIRE_EQ(ClassDB::resolve_type_name(alias), StringName());

		ERR_PRINT_OFF;
		Node *root = instantiate_text_scene_with_root_type(alias, "packed_scene_ambiguous_alias_root.tscn");
		ERR_PRINT_ON;
		REQUIRE_NE(root, nullptr);

		MissingNode *missing = Object::cast_to<MissingNode>(root);
		REQUIRE_NE(missing, nullptr);
		CHECK_EQ(missing->get_original_class(), String(alias));

		memdelete(root);
	}

	ResourceLoader::set_create_missing_resources_if_class_unavailable(previous);
}

} // namespace TestPackedScene
