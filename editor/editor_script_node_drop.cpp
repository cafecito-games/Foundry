/**************************************************************************/
/*  editor_script_node_drop.cpp                                           */
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

#include "editor_script_node_drop.h"

#include "core/object/class_db.h"
#include "core/object/script_language.h"
#include "scene/main/node.h"

static Node *_find_script_node(Node *p_current_node, const Ref<Script> &p_script) {
	if (!p_current_node || p_script.is_null()) {
		return nullptr;
	}
	if (p_current_node->get_script() == p_script) {
		return p_current_node;
	}

	for (int i = 0; i < p_current_node->get_child_count(); i++) {
		Node *n = _find_script_node(p_current_node->get_child(i), p_script);
		if (n) {
			return n;
		}
	}

	return nullptr;
}

EditorScriptNodeDrop::DropValidation EditorScriptNodeDrop::validate_nodes_drop(const Dictionary &p_drag_data, Node *p_script_associated_scene, const Ref<Script> &p_script, bool p_require_associated_scene) {
	DropValidation result;
	ERR_FAIL_COND_V(p_script.is_null(), result);

	if (p_require_associated_scene && !p_script_associated_scene) {
		result.reason = DROP_REJECT_NO_SCRIPT_SCENE;
		return result;
	}

	if (!p_script_associated_scene) {
		result.reason = DROP_REJECT_NO_SCRIPT_SCENE;
		return result;
	}

	if (!ClassDB::is_parent_class(p_script->get_instance_base_type(), "Node")) {
		result.reason = DROP_REJECT_NOT_NODE_SCRIPT;
		return result;
	}

	Object *drag_root_obj = p_drag_data.get("scene_root", (Object *)nullptr);
	result.drag_scene_root = Object::cast_to<Node>(drag_root_obj);
	if (!result.drag_scene_root) {
		result.reason = DROP_REJECT_NO_SCRIPT_SCENE;
		return result;
	}

	if (result.drag_scene_root != p_script_associated_scene) {
		result.reason = DROP_REJECT_CROSS_SCENE;
		return result;
	}

	result.script_scene_root = p_script_associated_scene;
	result.script_anchor_node = _find_script_node(result.script_scene_root, p_script);
	if (!result.script_anchor_node) {
		result.script_anchor_node = result.script_scene_root;
	}

	return result;
}

String EditorScriptNodeDrop::format_node_reference(Node *p_anchor_node, Node *p_node) {
	ERR_FAIL_NULL_V(p_anchor_node, String());
	ERR_FAIL_NULL_V(p_node, String());

	const bool is_unique = p_node->is_unique_name_in_owner() && (p_node->get_owner() == p_anchor_node || p_node->get_owner() == p_anchor_node->get_owner());
	String path = is_unique ? String(p_node->get_name()) : String(p_anchor_node->get_path_to(p_node));
	for (const String &segment : path.split("/")) {
		if (!segment.is_valid_ascii_identifier()) {
			const bool using_single_quotes = false;
			path = path.c_escape().quote(using_single_quotes ? "'" : "\"");
			break;
		}
	}
	return (is_unique ? "%" : "$") + path;
}

String EditorScriptNodeDrop::build_nodes_drop_text(const Dictionary &p_drag_data, const DropValidation &p_validation, bool p_member_drop, bool p_use_type_hints) {
	ERR_FAIL_COND_V(p_validation.reason != DROP_OK, String());
	ERR_FAIL_NULL_V(p_validation.script_anchor_node, String());

	Array nodes = p_drag_data["nodes"];
	String text_to_drop;

	if (p_member_drop) {
		for (int i = 0; i < nodes.size(); i++) {
			NodePath np = nodes[i];
			Node *node = p_validation.drag_scene_root->get_node(np);
			if (!node) {
				continue;
			}

			const bool is_unique = node->is_unique_name_in_owner() && (node->get_owner() == p_validation.script_anchor_node || node->get_owner() == p_validation.script_anchor_node->get_owner());
			String path = is_unique ? String(node->get_name()) : String(p_validation.script_anchor_node->get_path_to(node));
			for (const String &segment : path.split("/")) {
				if (!segment.is_valid_unicode_identifier()) {
					const bool using_single_quotes = false;
					path = path.c_escape().quote(using_single_quotes ? "'" : "\"");
					break;
				}
			}

			String variable_name = String(node->get_name()).to_snake_case().validate_unicode_identifier();
			if (p_use_type_hints) {
				StringName class_name = node->get_class_name();
				Ref<Script> node_script = node->get_script();
				if (node_script.is_valid()) {
					StringName global_node_script_name = node_script->get_global_name();
					if (!global_node_script_name.is_empty()) {
						class_name = global_node_script_name;
					}
				}
				text_to_drop += vformat("@onready var %s: %s = %c%s", variable_name, class_name, is_unique ? '%' : '$', path);
			} else {
				text_to_drop += vformat("@onready var %s = %c%s", variable_name, is_unique ? '%' : '$', path);
			}
			if (i < nodes.size() - 1) {
				text_to_drop += "\n";
			}
		}
	} else {
		for (int i = 0; i < nodes.size(); i++) {
			if (i > 0) {
				text_to_drop += ", ";
			}

			NodePath np = nodes[i];
			Node *node = p_validation.drag_scene_root->get_node(np);
			if (!node) {
				continue;
			}

			text_to_drop += format_node_reference(p_validation.script_anchor_node, node);
		}
	}

	return text_to_drop;
}
