/**************************************************************************/
/*  test_namespaced_native_integration.h                                  */
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
#include "scene/main/http_request.h"
#include "scene/resources/packed_scene.h"

#include "tests/test_macros.h"
#include "tests/test_utils.h"

#ifdef MODULE_HTTP_SERVER_ENABLED
#include "modules/http_server/http_server.h"

// End-to-end coverage for namespaced native classes: the `ClassDB` identity stamp, the scene
// serializer, and the scene loader have to agree on one canonical qualified name from the moment a
// node is created until it comes back off disk. Each layer is unit tested on its own; these cases
// exercise the seam between them with the `foundry.http.server.HTTPServer` pilot.
namespace TestNamespacedNativeIntegration {

static const char *QUALIFIED_NAME = "foundry.http.server.HTTPServer";

// Packs `p_root` and returns the scene it would serialize.
static Ref<PackedScene> pack_scene(Node *p_root) {
	Ref<PackedScene> packed;
	packed.instantiate();
	REQUIRE_EQ(packed->pack(p_root), OK);
	return packed;
}

// Reports the type string a packed scene stores for the node at `p_node_index`.
static StringName stored_node_type(const Ref<PackedScene> &p_scene, int p_node_index) {
	Ref<SceneState> state = p_scene->get_state();
	REQUIRE(state.is_valid());
	REQUIRE_GT(state->get_node_count(), p_node_index);
	return state->get_node_type(p_node_index);
}

// Saves `p_scene` under the test temp directory and loads it back, bypassing the resource cache so
// the parser really re-reads the stored type string.
static Ref<PackedScene> save_and_reload(const Ref<PackedScene> &p_scene, const String &p_file_name) {
	const String scene_path = TestUtils::get_temp_path(p_file_name);
	REQUIRE_EQ(ResourceSaver::save(p_scene, scene_path), OK);

	Error error = FAILED;
	Ref<PackedScene> loaded = ResourceLoader::load(scene_path, "", ResourceFormatLoader::CACHE_MODE_IGNORE, &error);
	REQUIRE_EQ(error, OK);
	REQUIRE(loaded.is_valid());
	return loaded;
}

// Asserts the identity every layer has to agree on for an instantiated pilot node.
static void check_pilot_identity(Node *p_node, int p_expected_port) {
	REQUIRE_NE(p_node, nullptr);
	CHECK_EQ(p_node->get_class(), String(QUALIFIED_NAME));
	CHECK(p_node->is_class("Node"));
	CHECK(p_node->is_class("Object"));
	// The bare name is not an identity, because the pilot registers no global alias.
	CHECK_FALSE(p_node->is_class("HTTPServer"));
	CHECK_EQ(int(p_node->get("port")), p_expected_port);
}

TEST_CASE("[SceneTree][ClassDBNamespace] Packing a namespaced node stores the qualified type and restores its state") {
	HTTPServer *server = memnew(HTTPServer);
	server->set_name("Server");
	server->set_port(9000);

	Ref<PackedScene> packed = pack_scene(server);
	memdelete(server);

	// `PackedScene::pack()` stores `Object::get_class()`, which is the qualified registry key.
	CHECK_EQ(stored_node_type(packed, 0), StringName(QUALIFIED_NAME));

	Node *instance = packed->instantiate();
	check_pilot_identity(instance, 9000);
	CHECK_EQ(instance->get_name(), StringName("Server"));
	memdelete(instance);
}

TEST_CASE("[SceneTree][ClassDBNamespace] A saved text scene carries the qualified type through the parser") {
	HTTPServer *server = memnew(HTTPServer);
	server->set_name("Server");
	server->set_port(9000);

	Ref<PackedScene> packed = pack_scene(server);
	memdelete(server);

	const String scene_path = TestUtils::get_temp_path("namespaced_native_root.tscn");
	REQUIRE_EQ(ResourceSaver::save(packed, scene_path), OK);

	// The on-disk contract other tools re-implement: the qualified name verbatim, never the bare one.
	const String scene_text = FileAccess::get_file_as_string(scene_path);
	CHECK(scene_text.contains("type=\"foundry.http.server.HTTPServer\""));
	CHECK_FALSE(scene_text.contains("type=\"HTTPServer\""));

	Error error = FAILED;
	Ref<PackedScene> loaded = ResourceLoader::load(scene_path, "", ResourceFormatLoader::CACHE_MODE_IGNORE, &error);
	REQUIRE_EQ(error, OK);
	REQUIRE(loaded.is_valid());
	CHECK_EQ(stored_node_type(loaded, 0), StringName(QUALIFIED_NAME));

	Node *instance = loaded->instantiate();
	check_pilot_identity(instance, 9000);
	memdelete(instance);
}

TEST_CASE("[SceneTree][ClassDBNamespace] A saved binary scene carries the qualified type") {
	HTTPServer *server = memnew(HTTPServer);
	server->set_name("Server");
	server->set_port(9100);

	Ref<PackedScene> packed = pack_scene(server);
	memdelete(server);

	Ref<PackedScene> loaded = save_and_reload(packed, "namespaced_native_root.scn");
	CHECK_EQ(stored_node_type(loaded, 0), StringName(QUALIFIED_NAME));

	Node *instance = loaded->instantiate();
	check_pilot_identity(instance, 9100);
	memdelete(instance);
}

TEST_CASE("[SceneTree][ClassDBNamespace] A namespaced node nested under a plain root round-trips by path") {
	// Non-root nodes take a different serialization path than the root type, so the child has to be
	// covered separately from the cases above.
	Node *root = memnew(Node);
	root->set_name("Root");

	HTTPServer *server = memnew(HTTPServer);
	server->set_name("Server");
	server->set_port(9200);
	root->add_child(server);
	server->set_owner(root);

	Ref<PackedScene> packed = pack_scene(root);
	memdelete(root);

	Ref<PackedScene> loaded = save_and_reload(packed, "namespaced_native_child.tscn");
	CHECK_EQ(stored_node_type(loaded, 0), StringName("Node"));
	CHECK_EQ(stored_node_type(loaded, 1), StringName(QUALIFIED_NAME));

	Node *instance = loaded->instantiate();
	REQUIRE_NE(instance, nullptr);
	check_pilot_identity(instance->get_node_or_null(NodePath("Server")), 9200);
	memdelete(instance);
}

TEST_CASE("[SceneTree][ClassDBNamespace] An instantiated scene is reusable as a nested branch") {
	// A scene whose root is namespaced is also usable as an instance inside another scene, which
	// stores it by path rather than by type; the identity still has to survive the second hop.
	HTTPServer *server = memnew(HTTPServer);
	server->set_name("Server");
	server->set_port(9300);

	Ref<PackedScene> inner = pack_scene(server);
	memdelete(server);

	const String inner_path = TestUtils::get_temp_path("namespaced_native_inner.tscn");
	REQUIRE_EQ(ResourceSaver::save(inner, inner_path), OK);

	Node *outer_root = memnew(Node);
	outer_root->set_name("Outer");

	Error error = FAILED;
	Ref<PackedScene> inner_loaded = ResourceLoader::load(inner_path, "", ResourceFormatLoader::CACHE_MODE_IGNORE, &error);
	REQUIRE_EQ(error, OK);
	Node *inner_instance = inner_loaded->instantiate();
	REQUIRE_NE(inner_instance, nullptr);
	outer_root->add_child(inner_instance);
	inner_instance->set_owner(outer_root);

	Ref<PackedScene> outer = pack_scene(outer_root);
	memdelete(outer_root);

	Ref<PackedScene> outer_loaded = save_and_reload(outer, "namespaced_native_outer.tscn");
	Node *outer_instance = outer_loaded->instantiate();
	REQUIRE_NE(outer_instance, nullptr);
	check_pilot_identity(outer_instance->get_node_or_null(NodePath("Server")), 9300);
	memdelete(outer_instance);
}

TEST_CASE("[SceneTree][ClassDBNamespace] A saved text scene stores the namespaced HTTPRequest client type") {
	HTTPRequest *client = memnew(HTTPRequest);
	client->set_name("Client");

	Ref<PackedScene> packed = pack_scene(client);
	memdelete(client);

	CHECK_EQ(stored_node_type(packed, 0), StringName("foundry.http.client.HTTPRequest"));

	const String scene_path = TestUtils::get_temp_path("namespaced_native_http_request.tscn");
	REQUIRE_EQ(ResourceSaver::save(packed, scene_path), OK);

	// The on-disk contract other tools re-implement: the qualified name verbatim, never the bare one.
	const String scene_text = FileAccess::get_file_as_string(scene_path);
	CHECK(scene_text.contains("type=\"foundry.http.client.HTTPRequest\""));
	CHECK_FALSE(scene_text.contains("type=\"HTTPRequest\""));

	Error error = FAILED;
	Ref<PackedScene> loaded = ResourceLoader::load(scene_path, "", ResourceFormatLoader::CACHE_MODE_IGNORE, &error);
	REQUIRE_EQ(error, OK);
	REQUIRE(loaded.is_valid());
	CHECK_EQ(stored_node_type(loaded, 0), StringName("foundry.http.client.HTTPRequest"));

	Node *instance = loaded->instantiate();
	REQUIRE_NE(instance, nullptr);
	CHECK_EQ(instance->get_class(), String("foundry.http.client.HTTPRequest"));
	CHECK(instance->is_class("Node"));
	CHECK_FALSE(instance->is_class("HTTPRequest"));
	memdelete(instance);
}

} // namespace TestNamespacedNativeIntegration
#endif // MODULE_HTTP_SERVER_ENABLED
