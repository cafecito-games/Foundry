/**************************************************************************/
/*  editor_scene_context.cpp                                              */
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

#include "editor_scene_context.h"

#include "scene/main/viewport.h"

void EditorSceneContext::set_scene_root_node(Node *p_scene_root, bool p_attach_to_viewport) {
	if (scene_root_node == p_scene_root) {
		return;
	}
	if (scene_root_node && scene_root_node->get_parent() == viewport) {
		viewport->remove_child(scene_root_node);
	}
	scene_root_node = p_scene_root;
	if (p_attach_to_viewport && scene_root_node && scene_root_node->get_parent() == nullptr) {
		viewport->add_child(scene_root_node, true);
	}
}

void EditorSceneContext::activate(Node *p_display_parent) {
	ERR_FAIL_COND(active);
	ERR_FAIL_NULL(p_display_parent);

	if (viewport->get_parent() != p_display_parent) {
		if (viewport->get_parent()) {
			viewport->get_parent()->remove_child(viewport);
		}
		p_display_parent->add_child(viewport);
	}
	active = true;

	// Nodes are back in the tree, so the retained selection can become a live
	// selection again. Nodes freed while the context was inactive are skipped.
	for (const ObjectID &node_id : retained_selection_ids) {
		Node *node = ObjectDB::get_instance<Node>(node_id);
		if (node && node->is_inside_tree()) {
			selection->add_node(node);
		}
	}
	retained_selection_ids.clear();
}

void EditorSceneContext::deactivate() {
	ERR_FAIL_COND(!active);

	// Capture the selection by id before the scene leaves the tree; the live
	// selection drops nodes as they exit the tree.
	retained_selection_ids.clear();
	List<Node *> selected_nodes = selection->get_full_selected_node_list();
	for (Node *node : selected_nodes) {
		retained_selection_ids.push_back(node->get_instance_id());
	}

	if (viewport->get_parent()) {
		viewport->get_parent()->remove_child(viewport);
	}
	selection->clear();
	active = false;
}

Vector<ObjectID> EditorSceneContext::get_selected_node_ids() const {
	Vector<ObjectID> node_ids;
	if (active) {
		List<Node *> selected_nodes = selection->get_full_selected_node_list();
		for (Node *node : selected_nodes) {
			node_ids.push_back(node->get_instance_id());
		}
	} else {
		for (const ObjectID &node_id : retained_selection_ids) {
			if (ObjectDB::get_instance(node_id)) {
				node_ids.push_back(node_id);
			}
		}
	}
	return node_ids;
}

void EditorSceneContext::set_selected_node_ids(const Vector<ObjectID> &p_ids) {
	if (active) {
		selection->clear();
		for (const ObjectID &node_id : p_ids) {
			Node *node = ObjectDB::get_instance<Node>(node_id);
			if (node && node->is_inside_tree()) {
				selection->add_node(node);
			}
		}
	} else {
		retained_selection_ids = p_ids;
	}
}

EditorSceneContext::EditorSceneContext() {
	viewport = memnew(SubViewport);
	viewport->set_auto_translate_mode(Node::AUTO_TRANSLATE_MODE_ALWAYS);
	viewport->set_translation_domain(StringName());
	viewport->set_embedding_subwindows(true);
	viewport->set_disable_3d(true);
	viewport->set_disable_input(true);
	viewport->set_as_audio_listener_2d(true);

	selection = memnew(EditorSelection);
}

EditorSceneContext::~EditorSceneContext() {
	if (viewport) {
		if (viewport->get_parent()) {
			viewport->get_parent()->remove_child(viewport);
		}
		// The scene root (if any) is a child of the viewport and is freed
		// with it.
		memdelete(viewport);
	}
	if (selection) {
		memdelete(selection);
	}
}
