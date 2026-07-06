/**************************************************************************/
/*  test_script_leaf_node_drop.h                                          */
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

#include "core/object/script_language.h"
#include "editor/editor_scene_context.h"
#include "editor/editor_scene_workspace.h"
#include "editor/editor_script_leaf.h"
#include "editor/editor_script_node_drop.h"

#include "scene/2d/node_2d.h"
#include "scene/gui/control.h"

#include "tests/editor/test_scene_workspace.h"
#include "tests/test_macros.h"

namespace TestScriptLeafNodeDrop {

class TestNodeDropScript : public Script {
	FOUNDRY_CLASS(TestNodeDropScript, Script);

protected:
	static void _bind_methods() {}

public:
	bool can_instantiate() const override { return false; }
	Ref<Script> get_base_script() const override { return Ref<Script>(); }
	StringName get_global_name() const override { return StringName(); }
	bool inherits_script(const Ref<Script> &p_script) const override { return false; }
	StringName get_instance_base_type() const override { return StringName("Node"); }
	ScriptInstance *instance_create(Object *p_this) override { return nullptr; }
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

static Ref<Script> make_node_test_script() {
	Ref<TestNodeDropScript> script;
	script.instantiate();
	return script;
}

static Dictionary make_nodes_drag_data(Node *p_scene_root, const Node *p_node) {
	Dictionary drag_data;
	drag_data["type"] = "nodes";
	Array nodes;
	nodes.push_back(p_node->get_path());
	drag_data["nodes"] = nodes;
	drag_data["scene_root"] = p_scene_root;
	return drag_data;
}

TEST_CASE("[SceneTree][Editor] node-to-script-drag") {
	Node2D *scene_root = memnew(Node2D);
	scene_root->set_name("SceneRoot");
	Node2D *player = memnew(Node2D);
	player->set_name("Player");
	Node2D *child = memnew(Node2D);
	child->set_name("Sprite");
	scene_root->add_child(player);
	player->add_child(child);
	SceneTree::get_singleton()->get_root()->add_child(scene_root);

	Ref<Script> script = make_node_test_script();
	scene_root->set_script(script);

	const Dictionary drag_data = make_nodes_drag_data(scene_root, child);
	EditorScriptNodeDrop::DropValidation validation = EditorScriptNodeDrop::validate_nodes_drop(drag_data, scene_root, script, true);
	REQUIRE(validation.reason == EditorScriptNodeDrop::DROP_OK);

	const String inserted = EditorScriptNodeDrop::build_nodes_drop_text(drag_data, validation, false, false);
	CHECK(inserted == "$Player/Sprite");

	scene_root->queue_free();
}

TEST_CASE("[SceneTree][Editor] drop-focuses-target") {
	TestSceneWorkspace::WorkspaceHarness h;
	h.mount();
	h.pump();

	WorkspaceLeafNode *scene_leaf = h.workspace->get_focused_leaf();
	REQUIRE(scene_leaf != nullptr);
	WorkspaceLeafNode *script_leaf_node = h.workspace->split(scene_leaf, false, EditorSceneWorkspace::SPLIT_SIDE_SECOND);
	h.pump();
	REQUIRE(script_leaf_node != nullptr);

	TestSceneWorkspace::replace_leaf_with_script(script_leaf_node, "ScriptPane");
	ScriptLeaf *script_leaf = TestSceneWorkspace::get_leaf_script(script_leaf_node);
	REQUIRE(script_leaf != nullptr);

	const int scene_leaf_id = scene_leaf->get_leaf_id();
	const int script_leaf_id = script_leaf_node->get_leaf_id();
	h.workspace->set_focused_leaf(scene_leaf_id);
	h.pump();
	CHECK(h.workspace->get_focused_leaf_id() == scene_leaf_id);

	if (!h.workspace->is_connected(SNAME("leaf_focus_requested"), callable_mp(h.workspace, &EditorSceneWorkspace::set_focused_leaf))) {
		h.workspace->connect(SNAME("leaf_focus_requested"), callable_mp(h.workspace, &EditorSceneWorkspace::set_focused_leaf));
	}
	script_leaf->request_workspace_focus();
	h.pump();
	CHECK(h.workspace->get_focused_leaf_id() == script_leaf_id);

	h.unmount();
}

TEST_CASE("[SceneTree][Editor] cross-scene-node-drop") {
	Node2D *scene_a = memnew(Node2D);
	scene_a->set_name("SceneA");
	Node2D *node_a = memnew(Node2D);
	node_a->set_name("Enemy");
	scene_a->add_child(node_a);

	Node2D *scene_b = memnew(Node2D);
	scene_b->set_name("SceneB");
	Node2D *script_owner = memnew(Node2D);
	script_owner->set_name("Owner");
	scene_b->add_child(script_owner);

	SceneTree::get_singleton()->get_root()->add_child(scene_a);
	SceneTree::get_singleton()->get_root()->add_child(scene_b);

	Ref<Script> script = make_node_test_script();
	script_owner->set_script(script);

	const Dictionary drag_data = make_nodes_drag_data(scene_a, node_a);
	EditorScriptNodeDrop::DropValidation validation = EditorScriptNodeDrop::validate_nodes_drop(drag_data, scene_b, script, true);
	CHECK(validation.reason == EditorScriptNodeDrop::DROP_REJECT_CROSS_SCENE);

	scene_a->queue_free();
	scene_b->queue_free();
}

} // namespace TestScriptLeafNodeDrop
