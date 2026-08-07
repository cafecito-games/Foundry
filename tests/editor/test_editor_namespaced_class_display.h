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
#include "scene/gui/tree.h"
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
	// A dialog entry is labeled with the simple name but carries the canonical registry key, and
	// that key is what the scene dock hands to `ClassDB`. Driving the dialog itself needs a live
	// editor window, so this asserts the contract on the seam instead: a key that a listed entry
	// carries is offered as instantiable, is labeled by its simple name, and produces a node whose
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

TEST_CASE("[Editor][ClassDBNamespace] A namespaced first-level type collapses like its ungrouped peer") {
	// The unfiltered create-node tree keeps an abstract type on the first level
	// expanded so the types a user can pick stay visible. A namespaced type sits
	// under a namespace group header instead of directly under the base type, so
	// the predicate must look through that header.
	Tree *tree = memnew(Tree);
	TreeItem *base_item = tree->create_item();
	base_item->set_text(0, "Node");
	base_item->set_meta(SNAME("__type_name"), String("Node"));

	TreeItem *flat_item = tree->create_item(base_item);
	flat_item->set_text(0, "AbstractPeer");
	flat_item->set_meta(SNAME("__type_name"), String("AbstractPeer"));

	TreeItem *group_item = tree->create_item(base_item);
	group_item->set_text(0, "http");
	group_item->set_meta(SNAME("__instantiable"), false);
	group_item->set_meta(SNAME("__namespace_group"), true);

	TreeItem *namespaced_item = tree->create_item(group_item);
	namespaced_item->set_text(0, "HTTPServer");
	namespaced_item->set_meta(SNAME("__type_name"), String("foundry.http.server.HTTPServer"));

	// An abstract first-level type stays expanded whether or not a group header
	// stands between it and the base type.
	CHECK_FALSE(CreateDialog::should_collapse_search_option(flat_item->get_parent(), "AbstractPeer", "Node", false));
	CHECK_FALSE(CreateDialog::should_collapse_search_option(namespaced_item->get_parent(), "foundry.http.server.HTTPServer", "Node", false));

	// An instantiable type still collapses on either path.
	CHECK(CreateDialog::should_collapse_search_option(flat_item->get_parent(), "AbstractPeer", "Node", true));
	CHECK(CreateDialog::should_collapse_search_option(namespaced_item->get_parent(), "foundry.http.server.HTTPServer", "Node", true));

	// Deeper levels collapse regardless, and the base type itself never does.
	CHECK(CreateDialog::should_collapse_search_option(namespaced_item, "Deeper", "Node", false));
	CHECK_FALSE(CreateDialog::should_collapse_search_option(nullptr, "Node", "Node", false));

	// The base type is matched by the identifier an item carries, not by the
	// display label, which drops the namespace for a qualified name.
	TreeItem *qualified_base = tree->create_item();
	qualified_base->set_text(0, "HTTPServer");
	qualified_base->set_meta(SNAME("__type_name"), String("foundry.http.server.HTTPServer"));
	CHECK_FALSE(CreateDialog::should_collapse_search_option(qualified_base, "Child", "foundry.http.server.HTTPServer", false));

	memdelete(tree);
}

} // namespace TestEditorNamespacedClassDisplay
