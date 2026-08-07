/**************************************************************************/
/*  test_editor_builtin_resource_setup.h                                  */
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

#include "core/io/missing_resource.h"
#include "core/io/resource.h"
#include "core/object/class_db.h"
#include "editor/editor_node.h"

#include "tests/test_macros.h"

// Declared in the global namespace because of the `FOUNDRY_CLASS` friend-declaration warning on
// Windows when the macro is expanded inside a namespace.
class _EditorBuiltInSetupNamespacedResource : public Resource {
	FOUNDRY_CLASS(_EditorBuiltInSetupNamespacedResource, Resource);
};

namespace TestEditorBuiltInResourceSetup {

constexpr const char *NAMESPACED_RESOURCE = "foundry.test.editor.builtin._EditorBuiltInSetupNamespacedResource";

// `ClassDB` cannot undo a namespace rekey, so this file's own test class gets its permanent registry
// shape once and every case below only reads it.
static void ensure_builtin_setup_registration() {
	static bool registered = false;
	if (registered) {
		return;
	}
	registered = true;

	FOUNDRY_REGISTER_CLASS(_EditorBuiltInSetupNamespacedResource);
	FOUNDRY_REGISTER_NAMESPACE(_EditorBuiltInSetupNamespacedResource, "foundry.test.editor.builtin");
}

// The id the editor stamps on a built-in resource must be a valid identifier derived from the simple
// class name, and the path it stamps must embed exactly that id.
static void check_id_and_path_agree(const Ref<Resource> &p_resource, const String &p_simple_class, const String &p_owner_path) {
	const String id = p_resource->get_scene_unique_id();
	CHECK(id.begins_with(p_simple_class + "_"));
	CHECK_FALSE(id.contains("."));
	CHECK_EQ(p_resource->get_path(), p_owner_path + "::" + id);
}

TEST_CASE("[BuiltInResource] A namespaced resource gets an identifier id matching its path") {
	ensure_builtin_setup_registration();

	Ref<Resource> resource = Ref<Resource>(Object::cast_to<Resource>(ClassDB::instantiate(NAMESPACED_RESOURCE)));
	REQUIRE(resource.is_valid());
	REQUIRE_EQ(resource->get_class(), String(NAMESPACED_RESOURCE));

	EditorNode::setup_built_in_resource(resource, "res://owner_namespaced.tscn");

	check_id_and_path_agree(resource, "_EditorBuiltInSetupNamespacedResource", "res://owner_namespaced.tscn");
}

TEST_CASE("[BuiltInResource] A MissingResource derives its id from the original class simple name") {
	Ref<MissingResource> resource;
	resource.instantiate();
	resource->set_original_class("foundry.test.missing.Widget");

	EditorNode::setup_built_in_resource(resource, "res://owner_missing.tscn");

	check_id_and_path_agree(resource, "Widget", "res://owner_missing.tscn");
}

TEST_CASE("[BuiltInResource] A flat class keeps its whole name as the id prefix") {
	Ref<Resource> resource;
	resource.instantiate();

	EditorNode::setup_built_in_resource(resource, "res://owner_flat.tscn");

	check_id_and_path_agree(resource, "Resource", "res://owner_flat.tscn");
}

} // namespace TestEditorBuiltInResourceSetup
