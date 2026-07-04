/**************************************************************************/
/*  editor_automation_workflow.cpp                                        */
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

#include "editor_automation_workflow.h"

#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/io/resource_loader.h"
#include "editor/docks/editor_dock.h"
#include "editor/docks/filesystem_dock.h"
#include "editor/editor_log.h"
#include "editor/inspector/editor_inspector.h"
#include "editor/run/editor_run_bar.h"
#include "editor/scene/scene_tree_editor.h"
#include "editor/script/script_editor_plugin.h"
#include "scene/gui/item_list.h"
#include "scene/gui/tree.h"

namespace {

enum class TreeContextKind {
	GENERIC,
	SCENE_TREE,
	FILESYSTEM_TREE,
};

enum class ItemListContextKind {
	GENERIC,
	FILESYSTEM_LIST,
};

TreeContextKind _tree_context_kind(const Tree *p_tree) {
	Node *parent = p_tree->get_parent();
	while (parent != nullptr) {
		if (Object::cast_to<const SceneTreeEditor>(parent) != nullptr) {
			return TreeContextKind::SCENE_TREE;
		}
		if (Object::cast_to<const FileSystemDock>(parent) != nullptr) {
			return TreeContextKind::FILESYSTEM_TREE;
		}
		parent = parent->get_parent();
	}
	return TreeContextKind::GENERIC;
}

ItemListContextKind _item_list_context_kind(const ItemList *p_list) {
	Node *parent = p_list->get_parent();
	while (parent != nullptr) {
		if (Object::cast_to<const FileSystemDock>(parent) != nullptr) {
			return ItemListContextKind::FILESYSTEM_LIST;
		}
		parent = parent->get_parent();
	}
	return ItemListContextKind::GENERIC;
}

String _variant_summary(const Variant &p_value) {
	switch (p_value.get_type()) {
		case Variant::STRING:
			return p_value;
		case Variant::BOOL:
			return String(p_value);
		case Variant::INT:
		case Variant::FLOAT:
			return String::num(p_value);
		case Variant::VECTOR2:
		case Variant::VECTOR2I:
		case Variant::VECTOR3:
		case Variant::VECTOR3I:
		case Variant::COLOR:
			return String(p_value);
		case Variant::OBJECT: {
			Object *object = p_value;
			if (object == nullptr) {
				return "null";
			}
			return object->get_class();
		}
		default:
			if (p_value.get_type() == Variant::NIL) {
				return "null";
			}
			return Variant::get_type_name(p_value.get_type());
	}
}

} // namespace

String EditorAutomationWorkflow::tree_item_stable_path(TreeItem *p_item) {
	PackedStringArray parts;
	while (p_item && p_item->get_parent()) {
		parts.push_back(String::num_int64(p_item->get_index()));
		p_item = p_item->get_parent();
	}
	parts.reverse();
	return String("/").join(parts);
}

namespace {

PackedStringArray _tree_item_supported_actions(TreeItem *p_item) {
	PackedStringArray actions;
	actions.push_back("select");
	actions.push_back("activate");
	if (p_item != nullptr && p_item->get_first_child() != nullptr) {
		actions.push_back("expand");
		actions.push_back("collapse");
	}
	return actions;
}

PackedStringArray _list_item_supported_actions() {
	PackedStringArray actions;
	actions.push_back("select");
	actions.push_back("activate");
	return actions;
}

void _append_common_tree_item_metadata(TreeItem *p_item, Dictionary &r_metadata) {
	ERR_FAIL_NULL(p_item);
	r_metadata["label"] = p_item->get_text(0);
	r_metadata["index"] = p_item->get_index();
	r_metadata["tree_item_path"] = EditorAutomationWorkflow::tree_item_stable_path(p_item);
	r_metadata["selected"] = p_item->is_selected(0);
	r_metadata["expanded"] = !p_item->is_collapsed();
	r_metadata["has_children"] = p_item->get_first_child() != nullptr;

	int depth = 0;
	TreeItem *parent = p_item->get_parent();
	while (parent && parent->get_parent()) {
		depth++;
		parent = parent->get_parent();
	}
	r_metadata["depth"] = depth;
	r_metadata["supported_actions"] = _tree_item_supported_actions(p_item);
}

void _append_common_list_item_metadata(const ItemList *p_list, int p_index, Dictionary &r_metadata) {
	ERR_FAIL_NULL(p_list);
	ERR_FAIL_INDEX(p_index, p_list->get_item_count());
	r_metadata["label"] = p_list->get_item_text(p_index);
	r_metadata["index"] = p_index;
	r_metadata["selected"] = p_list->is_selected(p_index);
	r_metadata["supported_actions"] = _list_item_supported_actions();

	const Variant item_metadata = p_list->get_item_metadata(p_index);
	if (item_metadata.get_type() != Variant::NIL) {
		r_metadata["item_metadata"] = item_metadata;
	}
}

} // namespace

String EditorAutomationWorkflow::role_for_node(const Node *p_node) {
	if (Object::cast_to<const EditorDock>(p_node)) {
		return "dock";
	}
	if (Object::cast_to<const EditorProperty>(p_node)) {
		return "property_row";
	}
	if (Object::cast_to<const EditorInspectorCategory>(p_node)) {
		return "inspector_section";
	}
	if (Object::cast_to<const EditorInspectorSection>(p_node)) {
		return "inspector_section";
	}
	if (Object::cast_to<const ScriptEditor>(p_node)) {
		return "script_editor";
	}
	if (Object::cast_to<const EditorRunBar>(p_node)) {
		return "run_bar";
	}
	if (Object::cast_to<const EditorLog>(p_node)) {
		return "editor_log";
	}
	return String();
}

Dictionary EditorAutomationWorkflow::metadata_for_node(const Node *p_node) {
	Dictionary metadata;

	if (const EditorDock *dock = Object::cast_to<const EditorDock>(p_node)) {
		metadata["dock_title"] = dock->get_display_title();
		metadata["layout_key"] = dock->get_effective_layout_key();
		return metadata;
	}

	if (const EditorProperty *property = Object::cast_to<const EditorProperty>(p_node)) {
		metadata["property_path"] = property->get_property_path();
		metadata["label"] = property->get_label();
		metadata["value"] = _variant_summary(property->get_edited_property_display_value());
		metadata["enabled"] = !property->is_read_only();
		metadata["editable"] = !property->is_read_only();
		if (Object *edited_object = const_cast<EditorProperty *>(property)->get_edited_object()) {
			metadata["object_class"] = edited_object->get_class();
		}
		return metadata;
	}

	if (const EditorInspectorCategory *category = Object::cast_to<const EditorInspectorCategory>(p_node)) {
		metadata["section"] = category->get_label();
		return metadata;
	}

	if (const EditorInspectorSection *section = Object::cast_to<const EditorInspectorSection>(p_node)) {
		metadata["section"] = section->get_label();
		return metadata;
	}

	return metadata;
}

Dictionary EditorAutomationWorkflow::metadata_for_tree_item(const Tree *p_tree, TreeItem *p_item) {
	Dictionary metadata;
	ERR_FAIL_NULL_V(p_tree, metadata);
	ERR_FAIL_NULL_V(p_item, metadata);

	_append_common_tree_item_metadata(p_item, metadata);

	const TreeContextKind context = _tree_context_kind(p_tree);
	const Variant item_metadata = p_item->get_metadata(0);

	if (context == TreeContextKind::SCENE_TREE) {
		NodePath node_path;
		if (item_metadata.get_type() == Variant::NODE_PATH) {
			node_path = item_metadata;
		}
		metadata["node_path"] = node_path;
		metadata["node_name"] = p_item->get_text(0);
		metadata["visible_in_tree"] = p_item->is_visible_in_tree();

		const SceneTreeEditor *scene_tree_editor = nullptr;
		Node *parent = p_tree->get_parent();
		while (parent != nullptr) {
			if (const SceneTreeEditor *candidate = Object::cast_to<const SceneTreeEditor>(parent)) {
				scene_tree_editor = candidate;
				break;
			}
			parent = parent->get_parent();
		}
		if (scene_tree_editor != nullptr && !node_path.is_empty()) {
			if (Node *scene_node = scene_tree_editor->get_node_or_null(node_path)) {
				metadata["node_class"] = scene_node->get_class();
			}
		}
		return metadata;
	}

	if (context == TreeContextKind::FILESYSTEM_TREE) {
		const String path = item_metadata;
		metadata["path"] = path;
		if (!path.is_empty()) {
			if (path == "Favorites") {
				metadata["type"] = "favorites_root";
			} else if (path.ends_with("/")) {
				metadata["type"] = "folder";
			} else if (DirAccess::dir_exists_absolute(path)) {
				metadata["type"] = "folder";
			} else {
				metadata["type"] = "file";
				if (FileAccess::exists(path)) {
					metadata["resource_type"] = ResourceLoader::get_resource_type(path);
				}
			}
		}
		return metadata;
	}

	return metadata;
}

Dictionary EditorAutomationWorkflow::metadata_for_list_item(const ItemList *p_list, int p_index) {
	Dictionary metadata;
	ERR_FAIL_NULL_V(p_list, metadata);
	ERR_FAIL_INDEX_V(p_index, p_list->get_item_count(), metadata);

	_append_common_list_item_metadata(p_list, p_index, metadata);

	if (_item_list_context_kind(p_list) == ItemListContextKind::FILESYSTEM_LIST) {
		const String path = p_list->get_item_metadata(p_index);
		metadata["path"] = path;
		if (!path.is_empty()) {
			if (path.ends_with("/")) {
				metadata["type"] = "folder";
			} else {
				metadata["type"] = "file";
				if (FileAccess::exists(path)) {
					metadata["resource_type"] = ResourceLoader::get_resource_type(path);
				}
			}
		}
	}
	return metadata;
}

bool EditorAutomationWorkflow::select_tree_item_ui(Tree *p_tree, TreeItem *p_item) {
	ERR_FAIL_NULL_V(p_tree, false);
	ERR_FAIL_NULL_V(p_item, false);

	// Route through the same TreeItem selection API the UI uses so editor
	// controllers (scene tree dock, filesystem dock) observe the change.
	p_item->select(0);
	p_item->set_as_cursor(0);
	p_tree->ensure_cursor_is_visible();
	return true;
}

bool EditorAutomationWorkflow::activate_tree_item_ui(Tree *p_tree, TreeItem *p_item) {
	ERR_FAIL_COND_V(!select_tree_item_ui(p_tree, p_item), false);
	p_tree->emit_signal(SNAME("item_activated"));
	return true;
}

bool EditorAutomationWorkflow::activate_list_item_ui(ItemList *p_list, int p_index) {
	ERR_FAIL_NULL_V(p_list, false);
	ERR_FAIL_INDEX_V(p_index, p_list->get_item_count(), false);
	p_list->select(p_index);
	p_list->emit_signal(SNAME("item_activated"), p_index);
	return true;
}
