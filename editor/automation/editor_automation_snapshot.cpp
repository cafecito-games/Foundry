/**************************************************************************/
/*  editor_automation_snapshot.cpp                                        */
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

#include "editor_automation_snapshot.h"

#include "core/object/object.h"
#include "editor/editor_node.h"
#include "scene/gui/base_button.h"
#include "scene/gui/button.h"
#include "scene/gui/check_box.h"
#include "scene/gui/check_button.h"
#include "scene/gui/code_edit.h"
#include "scene/gui/dialogs.h"
#include "scene/gui/item_list.h"
#include "scene/gui/line_edit.h"
#include "scene/gui/option_button.h"
#include "scene/gui/popup_menu.h"
#include "scene/gui/range.h"
#include "scene/gui/slider.h"
#include "scene/gui/spin_box.h"
#include "scene/gui/subviewport_container.h"
#include "scene/main/sub_viewport.h"
#include "scene/gui/tab_bar.h"
#include "scene/gui/tab_container.h"
#include "scene/gui/text_edit.h"
#include "scene/gui/tree.h"
#include "scene/main/node.h"
#include "scene/main/window.h"

namespace {

uint64_t next_snapshot_generation = 1;

class EditorAutomationSnapshotBuilder {
	EditorAutomationSnapshotData &data;
	Node *path_root = nullptr;
	Control *focused_control = nullptr;

	static Rect2i _control_bounds_global(const Control *p_control) {
		// Bounds are expressed in global/screen coordinates for automation clients.
		const Rect2 rect = p_control->get_global_rect();
		return Rect2i(rect.position.floor(), rect.size.floor());
	}

	static bool _control_is_enabled(const Control *p_control) {
		if (const BaseButton *button = Object::cast_to<const BaseButton>(p_control)) {
			return !button->is_disabled();
		}
		if (const LineEdit *line_edit = Object::cast_to<const LineEdit>(p_control)) {
			return line_edit->is_editable();
		}
		if (const TextEdit *text_edit = Object::cast_to<const TextEdit>(p_control)) {
			return text_edit->is_editable();
		}
		return true;
	}

	static String _control_name(const Control *p_control) {
		const String accessibility_name = p_control->get_accessibility_name().strip_edges();
		if (!accessibility_name.is_empty()) {
			return accessibility_name;
		}
		if (const Button *button = Object::cast_to<const Button>(p_control)) {
			return button->get_text();
		}
		if (const Window *window = Object::cast_to<const Window>(p_control)) {
			return window->get_title();
		}
		if (const OptionButton *option_button = Object::cast_to<const OptionButton>(p_control)) {
			const int selected = option_button->get_selected();
			if (selected >= 0) {
				return option_button->get_item_text(selected);
			}
		}
		return p_control->get_name();
	}

	static String _control_text(const Control *p_control) {
		if (const LineEdit *line_edit = Object::cast_to<const LineEdit>(p_control)) {
			return line_edit->get_text();
		}
		if (const TextEdit *text_edit = Object::cast_to<const TextEdit>(p_control)) {
			return text_edit->get_text();
		}
		if (const Button *button = Object::cast_to<const Button>(p_control)) {
			return button->get_text();
		}
		if (const Range *range = Object::cast_to<const Range>(p_control)) {
			return String::num(range->get_value());
		}
		if (const OptionButton *option_button = Object::cast_to<const OptionButton>(p_control)) {
			const int selected = option_button->get_selected();
			if (selected >= 0) {
				return option_button->get_item_text(selected);
			}
		}
		return String();
	}

	static String _control_role(const Control *p_control) {
		if (Object::cast_to<const SubViewportContainer>(p_control)) {
			return "viewport";
		}
		if (Object::cast_to<const AcceptDialog>(p_control)) {
			return "dialog";
		}
		if (Object::cast_to<const Window>(p_control)) {
			return "window";
		}
		if (Object::cast_to<const CheckBox>(p_control) || Object::cast_to<const CheckButton>(p_control)) {
			return "checkbox";
		}
		if (Object::cast_to<const Button>(p_control)) {
			return "button";
		}
		if (Object::cast_to<const CodeEdit>(p_control)) {
			return "code_editor";
		}
		if (Object::cast_to<const TextEdit>(p_control)) {
			return "text_area";
		}
		if (Object::cast_to<const LineEdit>(p_control)) {
			return "text_field";
		}
		if (Object::cast_to<const Tree>(p_control)) {
			return "tree";
		}
		if (Object::cast_to<const ItemList>(p_control)) {
			return "list";
		}
		if (Object::cast_to<const PopupMenu>(p_control)) {
			return "menu";
		}
		if (Object::cast_to<const OptionButton>(p_control)) {
			return "select";
		}
		if (Object::cast_to<const SpinBox>(p_control)) {
			return "spinbox";
		}
		if (Object::cast_to<const Slider>(p_control)) {
			return "slider";
		}
		if (Object::cast_to<const TabBar>(p_control)) {
			return "tab_list";
		}
		if (Object::cast_to<const TabContainer>(p_control)) {
			return "tab_list";
		}
		return "control";
	}

	static void _append_actions(const Control *p_control, const String &p_role, PackedStringArray &r_actions) {
		auto add_unique = [&](const String &p_action) {
			if (!r_actions.has(p_action)) {
				r_actions.push_back(p_action);
			}
		};

		if (p_control->has_focus() || p_control->get_focus_mode_with_override() != Control::FOCUS_NONE) {
			add_unique("focus");
		}

		if (p_role == "button" || p_role == "checkbox") {
			add_unique("click");
		} else if (p_role == "text_field" || p_role == "text_area" || p_role == "code_editor") {
			add_unique("set_text");
			add_unique("type_text");
		} else if (p_role == "spinbox" || p_role == "slider") {
			add_unique("set_value");
			add_unique("increment");
			add_unique("decrement");
		}
	}

	String _node_path(const Node *p_node) const {
		if (path_root == nullptr) {
			return p_node->get_path();
		}
		return path_root->get_path_to(p_node);
	}

	int _add_virtual_element(int p_parent_index, const String &p_kind, const String &p_key, const String &p_role, const String &p_name, const String &p_text, bool p_selected = false) {
		EditorAutomationElement element;
		element.id = EditorAutomationSnapshot::make_virtual_element_id(data.generation, p_kind, p_key);
		element.role = p_role;
		element.name = p_name;
		element.text = p_text;
		element.class_name = p_kind;
		element.visible = true;
		element.enabled = true;
		element.selected = p_selected;
		element.parent_index = p_parent_index;
		if (p_parent_index >= 0) {
			data.elements[p_parent_index].children.push_back(data.elements.size());
		} else {
			data.root_indices.push_back(data.elements.size());
		}
		data.id_to_index.insert(element.id, data.elements.size());
		data.elements.push_back(element);
		return data.elements.size() - 1;
	}

	String _tree_item_path(TreeItem *p_item) {
		PackedStringArray parts;
		while (p_item && p_item->get_parent()) {
			parts.push_back(String::num_int64(p_item->get_index()));
			p_item = p_item->get_parent();
		}
		parts.reverse();
		return String("/").join(parts);
	}

	void _add_tree_items(const Tree *p_tree, int p_parent_index) {
		TreeItem *item = p_tree->get_root();
		if (item == nullptr) {
			return;
		}
		const uint64_t tree_id = p_tree->get_instance_id();
		item = item->get_first_child();
		while (item) {
			if (item->is_visible_in_tree()) {
				const String key = vformat("%llu:%s", tree_id, _tree_item_path(item));
				const String item_text = item->get_text(0);
				_add_virtual_element(p_parent_index, "tree_item", key, "tree_item", item_text, item_text, item->is_selected(0));
			}
			item = item->get_next_in_tree();
		}
	}

	void _add_item_list_items(const ItemList *p_item_list, int p_parent_index) {
		const uint64_t list_id = p_item_list->get_instance_id();
		for (int i = 0; i < p_item_list->get_item_count(); i++) {
			const String item_text = p_item_list->get_item_text(i);
			const String key = vformat("%llu:%d", list_id, i);
			_add_virtual_element(p_parent_index, "list_item", key, "list_item", item_text, item_text, p_item_list->is_selected(i));
		}
	}

	void _add_popup_menu_items(const PopupMenu *p_popup_menu, int p_parent_index) {
		const uint64_t menu_id = p_popup_menu->get_instance_id();
		for (int i = 0; i < p_popup_menu->get_item_count(); i++) {
			if (p_popup_menu->is_item_separator(i)) {
				continue;
			}
			const String item_text = p_popup_menu->get_item_text(i);
			const String key = vformat("%llu:%d", menu_id, i);
			_add_virtual_element(p_parent_index, "menu_item", key, "menu_item", item_text, item_text, false);
		}
	}

	void _add_tab_bar_tabs(const TabBar *p_tab_bar, int p_parent_index) {
		const uint64_t tab_bar_id = p_tab_bar->get_instance_id();
		for (int i = 0; i < p_tab_bar->get_tab_count(); i++) {
			if (p_tab_bar->is_tab_hidden(i)) {
				continue;
			}
			const String tab_title = p_tab_bar->get_tab_title(i);
			const String key = vformat("%llu:%d", tab_bar_id, i);
			_add_virtual_element(p_parent_index, "tab", key, "tab", tab_title, tab_title, p_tab_bar->get_current_tab() == i);
		}
	}

	void _add_tab_container_tabs(const TabContainer *p_tab_container, int p_parent_index) {
		const uint64_t tab_container_id = p_tab_container->get_instance_id();
		for (int i = 0; i < p_tab_container->get_tab_count(); i++) {
			if (p_tab_container->is_tab_hidden(i)) {
				continue;
			}
			const String tab_title = p_tab_container->get_tab_title(i);
			const String key = vformat("%llu:%d", tab_container_id, i);
			_add_virtual_element(p_parent_index, "tab", key, "tab", tab_title, tab_title, p_tab_container->get_current_tab() == i);
		}
	}

	void _add_control_children(const Control *p_control, int p_parent_index) {
		if (Object::cast_to<const Tree>(p_control)) {
			_add_tree_items(static_cast<const Tree *>(p_control), p_parent_index);
		} else if (Object::cast_to<const ItemList>(p_control)) {
			_add_item_list_items(static_cast<const ItemList *>(p_control), p_parent_index);
		} else if (Object::cast_to<const PopupMenu>(p_control)) {
			_add_popup_menu_items(static_cast<const PopupMenu *>(p_control), p_parent_index);
		} else if (Object::cast_to<const TabBar>(p_control)) {
			_add_tab_bar_tabs(static_cast<const TabBar *>(p_control), p_parent_index);
		} else if (Object::cast_to<const TabContainer>(p_control)) {
			_add_tab_container_tabs(static_cast<const TabContainer *>(p_control), p_parent_index);
		}
	}

	bool _should_skip_child(const Control *p_parent, Node *p_child) const {
		if (Object::cast_to<const SubViewportContainer>(p_parent)) {
			return true;
		}
		if (SubViewport *sub_viewport = Object::cast_to<SubViewport>(p_child)) {
			if (sub_viewport->is_part_of_edited_scene()) {
				return true;
			}
		}
		return false;
	}

	int _add_control(Control *p_control, int p_parent_index, bool p_is_root) {
		if (!p_control->is_visible_in_tree()) {
			return -1;
		}

		EditorAutomationElement element;
		element.object_id = p_control->get_instance_id();
		element.id = EditorAutomationSnapshot::make_control_element_id(data.generation, element.object_id);
		element.role = _control_role(p_control);
		element.name = _control_name(p_control);
		element.text = _control_text(p_control);
		element.class_name = p_control->get_class();
		element.path = _node_path(p_control);
		element.visible = p_control->is_visible_in_tree();
		element.enabled = _control_is_enabled(p_control);
		element.focused = p_control->has_focus();
		element.bounds = _control_bounds_global(p_control);
		element.parent_index = p_parent_index;
		_append_actions(p_control, element.role, element.actions);

		if (const BaseButton *button = Object::cast_to<const BaseButton>(p_control)) {
			element.pressed = button->is_pressed();
		}
		if (const Window *window = Object::cast_to<const Window>(p_control)) {
			element.selected = window->has_focus();
		}

		if (p_parent_index >= 0) {
			data.elements[p_parent_index].children.push_back(data.elements.size());
		} else if (p_is_root) {
			data.root_indices.push_back(data.elements.size());
		}

		data.id_to_index.insert(element.id, data.elements.size());
		data.object_id_to_index.insert(element.object_id, data.elements.size());
		data.elements.push_back(element);
		const int element_index = data.elements.size() - 1;

		if (focused_control == p_control) {
			data.focused_element_id = element.id;
		}

		_add_control_children(p_control, element_index);

		if (Object::cast_to<const SubViewportContainer>(p_control)) {
			return element_index;
		}

		for (int i = 0; i < p_control->get_child_count(false); i++) {
			Node *child = p_control->get_child(i, false);
			if (_should_skip_child(p_control, child)) {
				continue;
			}
			if (Control *child_control = Object::cast_to<Control>(child)) {
				_add_control(child_control, element_index, false);
			} else if (Window *child_window = Object::cast_to<Window>(child)) {
				_add_control(child_window, element_index, false);
			}
		}

		return element_index;
	}

	void _walk_root(Node *p_root) {
		if (Window *window = Object::cast_to<Window>(p_root)) {
			if (!window->is_visible()) {
				return;
			}
			_add_control(window, -1, true);
			return;
		}
		if (Control *control = Object::cast_to<Control>(p_root)) {
			_add_control(control, -1, true);
		}
	}

public:
	explicit EditorAutomationSnapshotBuilder(EditorAutomationSnapshotData &p_data) :
			data(p_data) {
		data.generation = next_snapshot_generation++;
	}

	void set_path_root(Node *p_root) { path_root = p_root; }
	void set_focused_control(Control *p_focused_control) { focused_control = p_focused_control; }

	void build_from_roots(const LocalVector<Node *> &p_roots) {
		for (Node *root : p_roots) {
			ERR_CONTINUE(root == nullptr);
			set_path_root(root);
			if (root->is_inside_tree()) {
				Viewport *viewport = root->get_viewport();
				if (viewport != nullptr) {
					set_focused_control(viewport->gui_get_focus_owner());
				}
			}
			_walk_root(root);
		}
	}
};

Dictionary _element_to_dictionary(const EditorAutomationElement &p_element, const Vector<EditorAutomationElement> &p_elements) {
	Dictionary dict;
	dict["id"] = p_element.id;
	dict["role"] = p_element.role;
	dict["name"] = p_element.name;
	dict["text"] = p_element.text;
	dict["class"] = p_element.class_name;
	dict["path"] = p_element.path;
	dict["visible"] = p_element.visible;
	dict["enabled"] = p_element.enabled;
	dict["focused"] = p_element.focused;
	dict["pressed"] = p_element.pressed;
	dict["selected"] = p_element.selected;

	Array bounds;
	bounds.push_back(p_element.bounds.position.x);
	bounds.push_back(p_element.bounds.position.y);
	bounds.push_back(p_element.bounds.size.x);
	bounds.push_back(p_element.bounds.size.y);
	dict["bounds"] = bounds;
	dict["actions"] = p_element.actions;

	Array children;
	for (int child_index : p_element.children) {
		children.push_back(_element_to_dictionary(p_elements[child_index], p_elements));
	}
	dict["children"] = children;
	return dict;
}

} // namespace

String EditorAutomationSnapshot::make_control_element_id(uint64_t p_generation, uint64_t p_object_id) {
	return vformat("snapshot:%llu/object:%llu", p_generation, p_object_id);
}

String EditorAutomationSnapshot::make_virtual_element_id(uint64_t p_generation, const String &p_kind, const String &p_key) {
	return vformat("snapshot:%llu/%s:%s", p_generation, p_kind, p_key);
}

bool EditorAutomationSnapshot::parse_element_id(const String &p_id, uint64_t &r_generation, String &r_kind, String &r_key) {
	const int snapshot_pos = p_id.find("snapshot:");
	if (snapshot_pos != 0) {
		return false;
	}
	const int slash_pos = p_id.find_char('/');
	if (slash_pos < 0) {
		return false;
	}
	const String generation_text = p_id.substr(snapshot_pos + 9, slash_pos - (snapshot_pos + 9));
	r_generation = generation_text.to_uint64();
	const String remainder = p_id.substr(slash_pos + 1);
	const int colon_pos = remainder.find_char(':');
	if (colon_pos < 0) {
		return false;
	}
	r_kind = remainder.substr(0, colon_pos);
	r_key = remainder.substr(colon_pos + 1);
	return true;
}

EditorAutomationSnapshot EditorAutomationSnapshot::capture_from_node(Node *p_root) {
	LocalVector<Node *> roots;
	if (p_root != nullptr) {
		roots.push_back(p_root);
	}
	return capture_from_roots(roots);
}

EditorAutomationSnapshot EditorAutomationSnapshot::capture_from_roots(const LocalVector<Node *> &p_roots) {
	EditorAutomationSnapshot snapshot;
	EditorAutomationSnapshotBuilder builder(snapshot.data);
	builder.build_from_roots(p_roots);
	return snapshot;
}

EditorAutomationSnapshot EditorAutomationSnapshot::capture_from_editor() {
	LocalVector<Node *> roots;
	EditorNode *editor_node = EditorNode::get_singleton();
	if (editor_node != nullptr && editor_node->is_editor_ready()) {
		roots.push_back(editor_node);
	}
	return capture_from_roots(roots);
}

const EditorAutomationElement *EditorAutomationSnapshot::find_by_id(const String &p_id) const {
	const int *index = data.id_to_index.getptr(p_id);
	if (index == nullptr) {
		return nullptr;
	}
	return &data.elements[*index];
}

const EditorAutomationElement *EditorAutomationSnapshot::find_by_object_id(uint64_t p_object_id) const {
	const int *index = data.object_id_to_index.getptr(p_object_id);
	if (index == nullptr) {
		return nullptr;
	}
	return &data.elements[*index];
}

Array EditorAutomationSnapshot::get_root_elements() const {
	Array roots;
	for (int root_index : data.root_indices) {
		roots.push_back(_element_to_dictionary(data.elements[root_index], data.elements));
	}
	return roots;
}

Dictionary EditorAutomationSnapshot::to_dictionary() const {
	Dictionary dict;
	dict["generation"] = data.generation;
	dict["focused_element_id"] = data.focused_element_id;
	dict["roots"] = get_root_elements();
	return dict;
}

Dictionary EditorAutomationSelectorResult::to_dictionary() const {
	Dictionary dict;
	dict["ok"] = status == EditorAutomationSelectorStatus::OK;
	if (!error_kind.is_empty()) {
		dict["kind"] = error_kind;
	}
	if (!message.is_empty()) {
		dict["message"] = message;
	}
	if (!candidates.is_empty()) {
		dict["candidates"] = candidates;
	}
	Array matches;
	for (int match_index : match_indices) {
		matches.push_back(match_index);
	}
	dict["matches"] = matches;
	return dict;
}
