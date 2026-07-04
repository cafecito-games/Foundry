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

#include "editor/automation/editor_automation_workflow.h"

#include "editor/automation/editor_automation_input.h"

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
#include "scene/gui/tab_bar.h"
#include "scene/gui/tab_container.h"
#include "scene/gui/text_edit.h"
#include "scene/gui/tree.h"
#include "scene/main/canvas_item.h"
#include "scene/main/viewport.h"
#include "scene/main/window.h"

namespace {

uint64_t next_snapshot_generation = 1;

class EditorAutomationSnapshotBuilder {
	EditorAutomationSnapshotData &data;
	Node *path_root = nullptr;
	Control *focused_control = nullptr;

	static Rect2i _node_bounds_global(const Node *p_node) {
		// Bounds are expressed in global/screen coordinates for automation clients.
		if (const Control *control = Object::cast_to<const Control>(p_node)) {
			const Rect2 rect = control->get_global_rect();
			return Rect2i(rect.position.floor(), rect.size.floor());
		}
		if (const Window *window = Object::cast_to<const Window>(p_node)) {
			return Rect2i(window->get_position(), window->get_size());
		}
		return Rect2i();
	}

	static bool _node_is_visible(const Node *p_node) {
		if (const CanvasItem *canvas_item = Object::cast_to<const CanvasItem>(p_node)) {
			return canvas_item->is_visible_in_tree();
		}
		if (const Window *window = Object::cast_to<const Window>(p_node)) {
			return window->is_visible();
		}
		return false;
	}

	static bool _node_is_enabled(const Node *p_node) {
		if (const BaseButton *button = Object::cast_to<const BaseButton>(p_node)) {
			return !button->is_disabled();
		}
		if (const LineEdit *line_edit = Object::cast_to<const LineEdit>(p_node)) {
			return line_edit->is_editable();
		}
		if (const TextEdit *text_edit = Object::cast_to<const TextEdit>(p_node)) {
			return text_edit->is_editable();
		}
		return true;
	}

	static String _node_name(const Node *p_node) {
		if (const Control *control = Object::cast_to<const Control>(p_node)) {
			const String accessibility_name = control->get_accessibility_name().strip_edges();
			if (!accessibility_name.is_empty()) {
				return accessibility_name;
			}
		}
		if (const Button *button = Object::cast_to<const Button>(p_node)) {
			return button->get_text();
		}
		if (const Window *window = Object::cast_to<const Window>(p_node)) {
			const String title = window->get_title();
			if (!title.is_empty()) {
				return title;
			}
		}
		if (const OptionButton *option_button = Object::cast_to<const OptionButton>(p_node)) {
			const int selected = option_button->get_selected();
			if (selected >= 0) {
				return option_button->get_item_text(selected);
			}
		}
		return p_node->get_name();
	}

	static String _node_text(const Node *p_node) {
		if (const LineEdit *line_edit = Object::cast_to<const LineEdit>(p_node)) {
			return line_edit->get_text();
		}
		if (const TextEdit *text_edit = Object::cast_to<const TextEdit>(p_node)) {
			return text_edit->get_text();
		}
		if (const Button *button = Object::cast_to<const Button>(p_node)) {
			return button->get_text();
		}
		if (const Range *range = Object::cast_to<const Range>(p_node)) {
			return String::num(range->get_value());
		}
		if (const OptionButton *option_button = Object::cast_to<const OptionButton>(p_node)) {
			const int selected = option_button->get_selected();
			if (selected >= 0) {
				return option_button->get_item_text(selected);
			}
		}
		if (const Window *window = Object::cast_to<const Window>(p_node)) {
			return window->get_title();
		}
		return String();
	}

	static String _node_role(const Node *p_node) {
		if (Object::cast_to<const SubViewportContainer>(p_node)) {
			return "viewport";
		}
		if (Object::cast_to<const AcceptDialog>(p_node)) {
			return "dialog";
		}
		if (Object::cast_to<const PopupMenu>(p_node)) {
			return "menu";
		}
		if (Object::cast_to<const Window>(p_node)) {
			return "window";
		}
		if (Object::cast_to<const CheckBox>(p_node) || Object::cast_to<const CheckButton>(p_node)) {
			return "checkbox";
		}
		if (Object::cast_to<const Button>(p_node)) {
			return "button";
		}
		if (Object::cast_to<const CodeEdit>(p_node)) {
			return "code_editor";
		}
		if (Object::cast_to<const TextEdit>(p_node)) {
			return "text_area";
		}
		if (Object::cast_to<const LineEdit>(p_node)) {
			return "text_field";
		}
		if (Object::cast_to<const Tree>(p_node)) {
			return "tree";
		}
		if (Object::cast_to<const ItemList>(p_node)) {
			return "list";
		}
		if (Object::cast_to<const OptionButton>(p_node)) {
			return "select";
		}
		if (Object::cast_to<const SpinBox>(p_node)) {
			return "spinbox";
		}
		if (Object::cast_to<const Slider>(p_node)) {
			return "slider";
		}
		if (Object::cast_to<const TabBar>(p_node)) {
			return "tab_list";
		}
		if (Object::cast_to<const TabContainer>(p_node)) {
			return "tab_list";
		}
		if (Object::cast_to<const Control>(p_node)) {
			return "control";
		}
		return "node";
	}

	static void _append_actions(const Node *p_node, const String &p_role, PackedStringArray &r_actions) {
		auto add_unique = [&](const String &p_action) {
			if (!r_actions.has(p_action)) {
				r_actions.push_back(p_action);
			}
		};

		if (const Control *control = Object::cast_to<const Control>(p_node)) {
			if (control->has_focus() || control->get_focus_mode_with_override() != Control::FOCUS_NONE) {
				add_unique("focus");
			}
		} else if (Object::cast_to<const Window>(p_node)) {
			add_unique("focus");
		}

		if (p_role == "button" || p_role == "checkbox") {
			add_unique("click");
		} else if (p_role == "viewport") {
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
			return String(p_node->get_path());
		}
		return String(path_root->get_path_to(p_node));
	}

	int _add_virtual_element(int p_parent_index, const String &p_kind, const String &p_key, const String &p_role, const String &p_name, const String &p_text, bool p_selected = false, const Dictionary &p_metadata = Dictionary()) {
		EditorAutomationElement element;
		element.id = EditorAutomationSnapshot::make_virtual_element_id(data.generation, p_kind, p_key);
		element.role = p_role;
		element.name = p_name;
		element.text = p_text;
		element.class_name = p_kind;
		element.visible = true;
		element.enabled = true;
		element.selected = p_selected;
		element.metadata = p_metadata;
		element.parent_index = p_parent_index;
		if (p_parent_index >= 0) {
			data.elements.ptrw()[p_parent_index].children.push_back(data.elements.size());
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
				const String key = vformat("%s:%s", String::num_uint64(tree_id), _tree_item_path(item));
				const String item_text = item->get_text(0);
				const Dictionary metadata = EditorAutomationWorkflow::metadata_for_tree_item(p_tree, item);
				_add_virtual_element(p_parent_index, "tree_item", key, "tree_item", item_text, item_text, item->is_selected(0), metadata);
			}
			item = item->get_next_in_tree();
		}
	}

	void _add_item_list_items(const ItemList *p_item_list, int p_parent_index) {
		const uint64_t list_id = p_item_list->get_instance_id();
		for (int i = 0; i < p_item_list->get_item_count(); i++) {
			const String item_text = p_item_list->get_item_text(i);
			const String key = vformat("%s:%d", String::num_uint64(list_id), i);
			const Dictionary metadata = EditorAutomationWorkflow::metadata_for_list_item(p_item_list, i);
			_add_virtual_element(p_parent_index, "list_item", key, "list_item", item_text, item_text, p_item_list->is_selected(i), metadata);
		}
	}

	void _add_popup_menu_items(const PopupMenu *p_popup_menu, int p_parent_index) {
		const uint64_t menu_id = p_popup_menu->get_instance_id();
		for (int i = 0; i < p_popup_menu->get_item_count(); i++) {
			if (p_popup_menu->is_item_separator(i)) {
				continue;
			}
			const String item_text = p_popup_menu->get_item_text(i);
			const String key = vformat("%s:%d", String::num_uint64(menu_id), i);
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
			const String key = vformat("%s:%d", String::num_uint64(tab_bar_id), i);
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
			const String key = vformat("%s:%d", String::num_uint64(tab_container_id), i);
			_add_virtual_element(p_parent_index, "tab", key, "tab", tab_title, tab_title, p_tab_container->get_current_tab() == i);
		}
	}

	void _add_virtual_children(const Node *p_node, int p_parent_index) {
		if (const Tree *tree = Object::cast_to<const Tree>(p_node)) {
			_add_tree_items(tree, p_parent_index);
		} else if (const ItemList *item_list = Object::cast_to<const ItemList>(p_node)) {
			_add_item_list_items(item_list, p_parent_index);
		} else if (const PopupMenu *popup_menu = Object::cast_to<const PopupMenu>(p_node)) {
			_add_popup_menu_items(popup_menu, p_parent_index);
		} else if (const TabBar *tab_bar = Object::cast_to<const TabBar>(p_node)) {
			_add_tab_bar_tabs(tab_bar, p_parent_index);
		} else if (const TabContainer *tab_container = Object::cast_to<const TabContainer>(p_node)) {
			_add_tab_container_tabs(tab_container, p_parent_index);
		}
	}

	bool _should_skip_child(const Node *p_parent, Node *p_child) const {
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

	bool _node_is_focused(const Node *p_node) const {
		if (const Control *control = Object::cast_to<const Control>(p_node)) {
			return control->has_focus();
		}
		if (const Window *window = Object::cast_to<const Window>(p_node)) {
			return window->has_focus();
		}
		return false;
	}

	int _add_node(Node *p_node, int p_parent_index, bool p_is_root) {
		if (!_node_is_visible(p_node)) {
			return -1;
		}

		EditorAutomationElement element;
		element.object_id = p_node->get_instance_id();
		element.id = EditorAutomationSnapshot::make_control_element_id(data.generation, element.object_id);
		element.role = _node_role(p_node);
		const String workflow_role = EditorAutomationWorkflow::role_for_node(p_node);
		if (!workflow_role.is_empty()) {
			element.role = workflow_role;
		}
		element.name = _node_name(p_node);
		element.text = _node_text(p_node);
		element.class_name = p_node->get_class();
		element.path = _node_path(p_node);
		element.visible = _node_is_visible(p_node);
		element.enabled = _node_is_enabled(p_node);
		element.focused = _node_is_focused(p_node);
		element.bounds = _node_bounds_global(p_node);
		element.parent_index = p_parent_index;
		element.metadata = EditorAutomationWorkflow::metadata_for_node(p_node);
		if (Window *owner_window = EditorAutomationInput::window_for_node(p_node)) {
			element.metadata["window_object_id"] = String::num_uint64(owner_window->get_instance_id());
			element.metadata["window_title"] = owner_window->get_title();
			element.metadata["window_focused"] = owner_window->has_focus();
		}
		_append_actions(p_node, element.role, element.actions);

		if (const BaseButton *button = Object::cast_to<const BaseButton>(p_node)) {
			element.pressed = button->is_pressed();
		}
		if (const Window *window = Object::cast_to<const Window>(p_node)) {
			element.selected = window->has_focus();
		}

		if (p_parent_index >= 0) {
			data.elements.ptrw()[p_parent_index].children.push_back(data.elements.size());
		} else if (p_is_root) {
			data.root_indices.push_back(data.elements.size());
		}

		data.id_to_index.insert(element.id, data.elements.size());
		data.object_id_to_index.insert(element.object_id, data.elements.size());
		data.elements.push_back(element);
		const int element_index = data.elements.size() - 1;

		if (focused_control && p_node == focused_control) {
			data.focused_element_id = element.id;
		}

		_add_virtual_children(p_node, element_index);

		if (Object::cast_to<const SubViewportContainer>(p_node)) {
			return element_index;
		}

		// Descend into internal children only for Window nodes. Dialogs
		// (AcceptDialog/ConfirmationDialog and subclasses like CreateDialog) add
		// their action buttons (OK/Cancel/custom) via an internal buttons HBox, so
		// without this those buttons are invisible to automation and dialogs can
		// never be confirmed. Regular Controls keep hiding their internal parts
		// (e.g. SpinBox line edit, Tree/ItemList scrollbars) to avoid noise.
		const bool include_internal = Object::cast_to<Window>(p_node) != nullptr;
		for (int i = 0; i < p_node->get_child_count(include_internal); i++) {
			Node *child = p_node->get_child(i, include_internal);
			if (_should_skip_child(p_node, child)) {
				continue;
			}
			if (Control *child_control = Object::cast_to<Control>(child)) {
				_add_node(child_control, element_index, false);
			} else if (Window *child_window = Object::cast_to<Window>(child)) {
				_add_node(child_window, element_index, false);
			}
		}

		return element_index;
	}

	void _walk_root(Node *p_root) {
		_add_node(p_root, -1, true);
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
	if (!p_element.metadata.is_empty()) {
		dict["metadata"] = p_element.metadata;
	}

	Array children;
	for (int child_index : p_element.children) {
		children.push_back(_element_to_dictionary(p_elements[child_index], p_elements));
	}
	dict["children"] = children;
	return dict;
}

} // namespace

String EditorAutomationSnapshot::make_control_element_id(uint64_t p_generation, uint64_t p_object_id) {
	return vformat("snapshot:%s/object:%s", String::num_uint64(p_generation), String::num_uint64(p_object_id));
}

String EditorAutomationSnapshot::make_virtual_element_id(uint64_t p_generation, const String &p_kind, const String &p_key) {
	return vformat("snapshot:%s/%s:%s", String::num_uint64(p_generation), p_kind, p_key);
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
	r_generation = static_cast<uint64_t>(generation_text.to_int());
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
	if (editor_node != nullptr && editor_node->is_editor_ready() && editor_node->is_inside_tree()) {
		// EditorNode is a plain Node, so it is not itself a visible element and
		// the snapshot walk does not descend through non-Control/non-Window
		// nodes. Capture from the editor's GUI base Control, which is the root of
		// the visible editor UI (docks, toolbars, dialogs, popups).
		Control *gui_base = editor_node->get_gui_base();
		if (gui_base != nullptr) {
			roots.push_back(gui_base);
		} else {
			Window *root_window = editor_node->get_window();
			if (root_window != nullptr) {
				roots.push_back(root_window);
			}
		}
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
