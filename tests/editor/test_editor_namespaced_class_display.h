/**************************************************************************/
/*  test_editor_namespaced_class_display.h                                */
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

#include "editor/editor_node.h"
#include "editor/gui/create_dialog.h"

#include "core/object/class_db.h"
#include "scene/resources/packed_scene.h"

#include "tests/test_macros.h"

namespace TestEditorNamespacedClassDisplay {

TEST_CASE("[Editor][ClassDBNamespace] A namespaced class is listed under its simple name") {
	CHECK(CreateDialog::get_class_display_name("foundry.http.server.HTTPServer") == "HTTPServer");

	// A class without a namespace is listed under the only name it has.
	CHECK(CreateDialog::get_class_display_name("Node") == "Node");

	// Names that are not registry keys are shown verbatim: the bare name of a namespaced class is
	// not a canonical key, and neither is a global script class or a resource path.
	CHECK(CreateDialog::get_class_display_name("HTTPServer") == "HTTPServer");
	CHECK(CreateDialog::get_class_display_name("res://player.fs") == "res://player.fs");
}

TEST_CASE("[Editor][ClassDBNamespace] Theme icons for a namespaced class resolve by simple name") {
	CHECK(EditorNode::get_class_theme_icon_name("foundry.http.server.HTTPServer") == StringName("HTTPServer"));
	CHECK(EditorNode::get_class_theme_icon_name("Node") == StringName("Node"));
	CHECK(EditorNode::get_class_theme_icon_name("res://player.fs") == StringName("res://player.fs"));
}

TEST_CASE("[Editor][ClassDBNamespace] A registered class name never resolves to a script path") {
	// The last dotted segment of a qualified class name parses as a file extension, so without the
	// registry check first this name would be treated as a candidate resource path.
	CHECK(String("foundry.http.server.HTTPServer").get_extension() == "HTTPServer");
	CHECK(EditorNode::get_class_icon_script_path("foundry.http.server.HTTPServer").is_empty());

	CHECK(EditorNode::get_class_icon_script_path("Node").is_empty());
	// An unregistered name that is not an existing resource yields no script path either.
	CHECK(EditorNode::get_class_icon_script_path("res://does_not_exist.fs").is_empty());
}

TEST_CASE("[Editor][ClassDBNamespace] The canonical key a create-node entry carries reaches the scene as a qualified type") {
	// A dialog entry is labelled with the simple name but carries the canonical registry key, and
	// that key is what the scene dock hands to `ClassDB`. Driving the dialog itself needs a live
	// editor window, so this asserts the contract on the seam instead: a key that a listed entry
	// carries is offered as instantiable, is labelled by its simple name, and produces a node whose
	// serialized type is the qualified name.
	const StringName carried_type = "foundry.http.server.HTTPServer";

	REQUIRE(ClassDB::class_exists(carried_type));
	// `CreateDialog` marks an entry instantiable from exactly these two answers.
	CHECK(ClassDB::can_instantiate(carried_type));
	CHECK_FALSE(ClassDB::is_virtual(carried_type));
	CHECK(CreateDialog::get_class_display_name(carried_type) == "HTTPServer");

	Node *created = Object::cast_to<Node>(ClassDB::instantiate(carried_type));
	REQUIRE_NE(created, nullptr);
	created->set_name("Server");

	Ref<PackedScene> packed;
	packed.instantiate();
	REQUIRE_EQ(packed->pack(created), OK);
	memdelete(created);

	Ref<SceneState> state = packed->get_state();
	REQUIRE(state.is_valid());
	REQUIRE_GE(state->get_node_count(), 1);
	CHECK_EQ(state->get_node_type(0), carried_type);
}

} // namespace TestEditorNamespacedClassDisplay
