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
#include "editor/docks/inspector_dock.h"
#include "editor/docks/scene_tree_dock.h"
#include "editor/editor_node.h"
#include "editor/editor_scene_pane_tile.h"
#include "editor/scene/editor_scene_tabs.h"
#include "editor/inspector/editor_inspector.h"
#include "editor/workspace/workspace_pane.h"
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
#include "scene/gui/scroll_container.h"
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
	EditorAutomationSnapshotOptions options;
	Node *path_root = nullptr;
	Control *focused_control = nullptr;
	HashSet<Node *> relaxed_visibility_roots;

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
		// Prefer explicit accessibility names: they are stable, user-facing labels
		// intended for both assistive tech and automation selectors.
		if (const Control *control = Object::cast_to<const Control>(p_node)) {
			const String accessibility_name = control->get_accessibility_name().strip_edges();
			if (!accessibility_name.is_empty()) {
				return accessibility_name;
			}
		}
		if (const EditorProperty *property = Object::cast_to<const EditorProperty>(p_node)) {
			const String property_label = property->get_label().strip_edges();
			if (!property_label.is_empty()) {
				return property_label;
			}
		}
		// Text-specific sources come next. Only use them when they carry a real
		// label; an icon-only Button has empty text and must not shadow the
		// tooltip/node-name fallbacks below.
		if (const Button *button = Object::cast_to<const Button>(p_node)) {
			const String button_text = button->get_text();
			if (!button_text.is_empty()) {
				return button_text;
			}
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
		// Fall back to tooltip text so unnamed icon-only controls (toolbar and
		// dock buttons) remain addressable by a semantic, editor-facing label
		// instead of exposing a volatile node name like "@Button@4790".
		if (const Control *control = Object::cast_to<const Control>(p_node)) {
			const String tooltip = control->get_tooltip_text().strip_edges();
			if (!tooltip.is_empty()) {
				return tooltip;
			}
		}
		// Raw node name is the last resort only.
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

		if (p_role == "button" || p_role == "checkbox" || p_role == "property_row") {
			add_unique("click");
			add_unique("activate");
		} else if (p_role == "viewport") {
			add_unique("click");
		} else if (p_role == "text_field" || p_role == "text_area" || p_role == "code_editor") {
			add_unique("set_text");
			add_unique("type_text");
			add_unique("submit");
		} else if (p_role == "tree" || p_role == "list") {
			add_unique("scroll");
		} else if (p_role == "spinbox" || p_role == "slider") {
			add_unique("set_value");
			add_unique("increment");
			add_unique("decrement");
		}

		if (Object::cast_to<const ScrollContainer>(p_node)) {
			add_unique("scroll");
		}
	}

	static void _append_virtual_actions(const String &p_kind, const Dictionary &p_metadata, PackedStringArray &r_actions) {
		if (p_metadata.has("supported_actions")) {
			const Variant actions_value = p_metadata.get("supported_actions", Variant());
			if (actions_value.get_type() == Variant::PACKED_STRING_ARRAY) {
				for (const String &action : PackedStringArray(actions_value)) {
					if (!r_actions.has(action)) {
						r_actions.push_back(action);
					}
				}
			}
			return;
		}

		if (p_kind == "tree_item") {
			for (const char *action : { "select", "activate", "expand", "collapse" }) {
				if (!r_actions.has(action)) {
					r_actions.push_back(action);
				}
			}
		} else if (p_kind == "list_item") {
			for (const char *action : { "select", "activate" }) {
				if (!r_actions.has(action)) {
					r_actions.push_back(action);
				}
			}
		} else if (p_kind == "menu_item") {
			for (const char *action : { "select", "activate", "choose_menu_item" }) {
				if (!r_actions.has(action)) {
					r_actions.push_back(action);
				}
			}
		} else if (p_kind == "tab") {
			if (!r_actions.has("select")) {
				r_actions.push_back("select");
			}
		}
	}

	String _node_path(const Node *p_node) const {
		if (path_root == nullptr) {
			return String(p_node->get_path());
		}
		return String(path_root->get_path_to(p_node));
	}

	int _add_virtual_element(int p_parent_index, const String &p_kind, const String &p_key, const String &p_role, const String &p_name, const String &p_text, bool p_selected = false, const Dictionary &p_metadata = Dictionary(), const Rect2i &p_bounds = Rect2i()) {
		EditorAutomationElement element;
		element.id = EditorAutomationSnapshot::make_virtual_element_id(data.generation, p_kind, p_key);
		element.handle = EditorAutomationSnapshot::make_durable_handle(p_kind, p_key);
		element.role = p_role;
		element.name = p_name;
		element.text = p_text;
		element.class_name = p_kind;
		element.visible = true;
		element.enabled = true;
		element.selected = p_selected;
		element.metadata = p_metadata;
		element.bounds = p_bounds;
		element.parent_index = p_parent_index;
		_append_virtual_actions(p_kind, p_metadata, element.actions);
		if (p_parent_index >= 0) {
			data.elements.ptrw()[p_parent_index].children.push_back(data.elements.size());
		} else {
			data.root_indices.push_back(data.elements.size());
		}
		data.id_to_index.insert(element.id, data.elements.size());
		data.handle_to_index.insert(element.handle, data.elements.size());
		data.elements.push_back(element);
		return data.elements.size() - 1;
	}

	String _tree_item_path(TreeItem *p_item) {
		return EditorAutomationWorkflow::tree_item_stable_path(p_item);
	}

	// Converts a control-local item rect into global snapshot bounds so synthesized
	// tree/list rows report a real on-screen target instead of the [0,0,0,0]
	// default. The rect is clipped to the control's viewport, so a row scrolled
	// out of view reports no bounds (left unset) rather than advertising an
	// off-screen, non-hit-testable target; partially visible rows report only
	// their visible region. An empty local rect (item not laid out yet) is also
	// left unset.
	static Rect2i _virtual_item_bounds(const Control *p_control, const Rect2 &p_local_rect) {
		if (p_local_rect.size.x <= 0 && p_local_rect.size.y <= 0) {
			return Rect2i();
		}
		const Rect2 global_rect = Rect2(p_control->get_global_position() + p_local_rect.position, p_local_rect.size);
		const Rect2 visible_rect = p_control->get_global_rect().intersection(global_rect);
		if (visible_rect.size.x <= 0 || visible_rect.size.y <= 0) {
			return Rect2i();
		}
		return Rect2i(visible_rect.position.floor(), visible_rect.size.floor());
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
				// Column -1 yields the full-width row rect, which is the target an
				// agent clicks to select/activate the item.
				Rect2 item_rect = p_tree->get_item_rect(item, -1);
				// get_item_rect anchors the row's y to the scroll offset from the last
				// draw (theme_cache.offset), which lags a pending act(scroll) until the
				// next redraw. Re-anchor to the live scroll value so tree_item bounds
				// share the list_item path's reference frame and stay correct when
				// observe_ui runs before a redraw. Only y needs correcting: the
				// full-width row rect starts at local x=0 regardless of horizontal
				// scroll.
				item_rect.position.y += p_tree->get_drawn_scroll_offset().y - p_tree->get_scroll().y;
				const Rect2i bounds = _virtual_item_bounds(p_tree, item_rect);
				_add_virtual_element(p_parent_index, "tree_item", key, "tree_item", item_text, item_text, item->is_selected(0), metadata, bounds);
			}
			item = item->get_next_in_tree();
		}
	}

	void _add_item_list_items(const ItemList *p_item_list, int p_parent_index) {
		const uint64_t list_id = p_item_list->get_instance_id();
		// ItemList::get_item_rect returns content-space coordinates (rect_cache
		// plus the panel offset) and, unlike Tree::get_item_rect, does not apply
		// the scroll offset that drawing subtracts. Subtract the current scroll
		// values so scrolled rows report their true on-screen position. The
		// scroll-bar accessors are non-const only; the reads themselves are const.
		ItemList *mutable_list = const_cast<ItemList *>(p_item_list);
		const Vector2 scroll_offset = Vector2(mutable_list->get_h_scroll_bar()->get_value(), mutable_list->get_v_scroll_bar()->get_value());
		for (int i = 0; i < p_item_list->get_item_count(); i++) {
			const String item_text = p_item_list->get_item_text(i);
			const String key = vformat("%s:%d", String::num_uint64(list_id), i);
			const Dictionary metadata = EditorAutomationWorkflow::metadata_for_list_item(p_item_list, i);
			Rect2 local_rect = p_item_list->get_item_rect(i, true);
			local_rect.position -= scroll_offset;
			const Rect2i bounds = _virtual_item_bounds(p_item_list, local_rect);
			_add_virtual_element(p_parent_index, "list_item", key, "list_item", item_text, item_text, p_item_list->is_selected(i), metadata, bounds);
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

	void _add_tab_bar_tabs(const TabBar *p_tab_bar, int p_parent_index, int p_active_tile_id = -1) {
		const uint64_t tab_bar_id = p_tab_bar->get_instance_id();
		int tile_id = p_active_tile_id;
		const WorkspacePane *owner_pane = nullptr;
		for (Node *node = p_tab_bar->get_parent(); node != nullptr; node = node->get_parent()) {
			if (EditorSceneTabs *scene_tabs = Object::cast_to<EditorSceneTabs>(node)) {
				if (tile_id < 0) {
					tile_id = scene_tabs->get_tile_id();
				}
				break;
			}
			// The generic pane tab strip is a plain TabBar owned by a WorkspacePane
			// (the legacy EditorSceneTabs bar is hidden). Capture the owning pane so
			// its strip's tab elements carry the owning leaf id plus the tab's
			// type_id and resource_key, letting selectors distinguish e.g. a scene
			// tab from a script tab sharing one pane's strip. Only the pane's own
			// strip mirrors the pane tab model 1:1; other TabBars mounted inside the
			// pane chrome (e.g. a tab bar in the active scene/script surface) must
			// not inherit workspace tab metadata by index.
			if (WorkspacePane *pane = Object::cast_to<WorkspacePane>(node)) {
				if (pane->get_tab_strip() == p_tab_bar) {
					owner_pane = pane;
				}
				if (tile_id < 0) {
					tile_id = pane->get_leaf_id();
				}
				break;
			}
		}
		for (int i = 0; i < p_tab_bar->get_tab_count(); i++) {
			if (p_tab_bar->is_tab_hidden(i)) {
				continue;
			}
			const String tab_title = p_tab_bar->get_tab_title(i);
			const String key = vformat("%s:%d", String::num_uint64(tab_bar_id), i);
			Dictionary metadata;
			if (tile_id >= 0) {
				metadata["tile_id"] = tile_id;
			}
			metadata["tab_index"] = i;
			// The pane's tab model is 1:1 with its strip (see WorkspacePane::_sync_tab_strip),
			// so index i maps directly to the backing WorkspaceTab.
			if (owner_pane && i < owner_pane->get_tab_count()) {
				const WorkspaceTab &tab = owner_pane->get_tab(i);
				metadata["type_id"] = String(tab.get_type_id());
				metadata["resource_key"] = tab.get_resource_key();
			}
			_add_virtual_element(p_parent_index, "tab", key, "tab", tab_title, tab_title, p_tab_bar->get_current_tab() == i, metadata);
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

	void _add_virtual_children(const Node *p_node, int p_parent_index, int p_active_tile_id = -1) {
		if (const Tree *tree = Object::cast_to<const Tree>(p_node)) {
			_add_tree_items(tree, p_parent_index);
		} else if (const ItemList *item_list = Object::cast_to<const ItemList>(p_node)) {
			_add_item_list_items(item_list, p_parent_index);
		} else if (const PopupMenu *popup_menu = Object::cast_to<const PopupMenu>(p_node)) {
			_add_popup_menu_items(popup_menu, p_parent_index);
		} else if (const TabBar *tab_bar = Object::cast_to<const TabBar>(p_node)) {
			_add_tab_bar_tabs(tab_bar, p_parent_index, p_active_tile_id);
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

	int _add_node(Node *p_node, int p_parent_index, bool p_is_root, bool p_internal = false, bool p_relax_visibility = false, int p_active_tile_id = -1) {
		if (!p_relax_visibility && !_node_is_visible(p_node)) {
			return -1;
		}

		EditorAutomationElement element;
		element.internal = p_internal;
		element.object_id = p_node->get_instance_id();
		element.id = EditorAutomationSnapshot::make_control_element_id(data.generation, element.object_id);
		element.handle = EditorAutomationSnapshot::make_durable_handle("object", String::num_uint64(element.object_id));
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
		int active_tile_id = p_active_tile_id;
		if (const ScenePaneTile *tile = Object::cast_to<const ScenePaneTile>(p_node)) {
			active_tile_id = tile->get_tile_id();
		}
		if (active_tile_id >= 0) {
			element.metadata["tile_id"] = active_tile_id;
		}
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
		data.handle_to_index.insert(element.handle, data.elements.size());
		data.object_id_to_index.insert(element.object_id, data.elements.size());
		data.elements.push_back(element);
		const int element_index = data.elements.size() - 1;

		if (focused_control && p_node == focused_control) {
			data.focused_element_id = element.id;
		}

		_add_virtual_children(p_node, element_index, active_tile_id);

		if (Object::cast_to<const SubViewportContainer>(p_node)) {
			return element_index;
		}

		// Internal-child policy (see EditorAutomationSnapshotOptions): by default,
		// descend into internal children only for Window nodes. Dialogs
		// (AcceptDialog/ConfirmationDialog and subclasses like CreateDialog) add
		// their action buttons (OK/Cancel/custom) via an internal buttons HBox, so
		// without this those buttons are invisible to automation and dialogs can
		// never be confirmed. Regular Controls keep hiding their internal parts
		// (e.g. SpinBox line edit, Tree/ItemList scrollbars) to avoid noise unless
		// the caller opts in with include_internal; opted-in internal subtrees are
		// flagged `internal` so agents can avoid depending on them by default.
		const bool parent_is_window = Object::cast_to<Window>(p_node) != nullptr;
		const bool include_internal = parent_is_window || options.include_internal;
		for (int i = 0; i < p_node->get_child_count(include_internal); i++) {
			Node *child = p_node->get_child(i, include_internal);
			if (_should_skip_child(p_node, child)) {
				continue;
			}
			const bool child_internal = p_internal || (child->is_internal() && !parent_is_window);
			bool child_relax = p_relax_visibility;
			if (child_relax && !_node_is_visible(child) && Object::cast_to<Window>(child) != nullptr) {
				// Hidden embedded dialogs (resource pickers, confirmations, etc.) live
				// inside the inspector dock but must not pollute relaxed snapshots.
				continue;
			}
			if (Control *child_control = Object::cast_to<Control>(child)) {
				_add_node(child_control, element_index, false, child_internal, child_relax, active_tile_id);
			} else if (Window *child_window = Object::cast_to<Window>(child)) {
				_add_node(child_window, element_index, false, child_internal, child_relax, active_tile_id);
			}
		}

		return element_index;
	}

	void _walk_root(Node *p_root) {
		const bool relax_visibility = options.relaxed_visibility_roots && relaxed_visibility_roots.has(p_root);
		// A node can be reachable from more than one capture root (e.g. an open
		// dialog under gui_base is also pushed via the exclusive-window chain and
		// its owning dock). Skip re-walking a root already captured from an earlier
		// root so it is emitted once instead of tripping a false ambiguous-selector
		// error. Relaxed roots still re-walk to expose otherwise-hidden descendants.
		if (!relax_visibility && data.object_id_to_index.has(p_root->get_instance_id())) {
			return;
		}
		_add_node(p_root, -1, true, false, relax_visibility);
	}

public:
	EditorAutomationSnapshotBuilder(EditorAutomationSnapshotData &p_data, const EditorAutomationSnapshotOptions &p_options) :
			data(p_data), options(p_options) {
		data.generation = next_snapshot_generation++;
	}

	void set_path_root(Node *p_root) { path_root = p_root; }
	void set_focused_control(Control *p_focused_control) { focused_control = p_focused_control; }
	void add_relaxed_visibility_root(Node *p_root) {
		if (p_root != nullptr) {
			relaxed_visibility_roots.insert(p_root);
		}
	}

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
	dict["handle"] = p_element.handle;
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
	if (p_element.internal) {
		dict["internal"] = true;
	}

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

String EditorAutomationSnapshot::make_durable_handle(const String &p_kind, const String &p_key) {
	return vformat("%s:%s", p_kind, p_key);
}

bool EditorAutomationSnapshot::parse_durable_handle(const String &p_handle, String &r_kind, String &r_key) {
	if (p_handle.begins_with("snapshot:")) {
		uint64_t generation = 0;
		return parse_element_id(p_handle, generation, r_kind, r_key);
	}
	const int colon_pos = p_handle.find_char(':');
	if (colon_pos < 0) {
		return false;
	}
	r_kind = p_handle.substr(0, colon_pos);
	r_key = p_handle.substr(colon_pos + 1);
	return !r_kind.is_empty();
}

bool EditorAutomationSnapshot::is_virtual_durable_kind(const String &p_kind) {
	return p_kind == "tree_item" || p_kind == "list_item" || p_kind == "menu_item" || p_kind == "tab";
}

EditorAutomationSnapshot EditorAutomationSnapshot::capture_from_node(Node *p_root, const EditorAutomationSnapshotOptions &p_options) {
	LocalVector<Node *> roots;
	if (p_root != nullptr) {
		roots.push_back(p_root);
	}
	return capture_from_roots(roots, p_options);
}

EditorAutomationSnapshot EditorAutomationSnapshot::capture_from_roots(const LocalVector<Node *> &p_roots, const EditorAutomationSnapshotOptions &p_options) {
	EditorAutomationSnapshot snapshot;
	EditorAutomationSnapshotBuilder builder(snapshot.data, p_options);
	builder.build_from_roots(p_roots);
	return snapshot;
}

EditorAutomationSnapshot EditorAutomationSnapshot::capture_from_editor(const EditorAutomationSnapshotOptions &p_options) {
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

		// Exclusive modal windows (CreateDialog, AcceptDialog, etc.) are not
		// reachable from gui_base, but agents must be able to act inside them.
		if (Window *root_window = editor_node->get_window()) {
			for (Window *exclusive = root_window->get_exclusive_child(); exclusive != nullptr; exclusive = exclusive->get_exclusive_child()) {
				roots.push_back(exclusive);
			}
		}

		LocalVector<Node *> relaxed_roots;
		if (SceneTreeDock *scene_tree_dock = EditorNode::get_singleton()->get_focused_scene_tree_dock()) {
			if (scene_tree_dock->is_inside_tree()) {
				roots.push_back(scene_tree_dock);
			}
		}
		if (InspectorDock *inspector_dock = EditorNode::get_singleton()->get_focused_inspector_dock()) {
			if (inspector_dock->is_inside_tree()) {
				roots.push_back(inspector_dock);
				relaxed_roots.push_back(inspector_dock);
				if (EditorInspector *inspector = inspector_dock->get_inspector()) {
					if (inspector->is_inside_tree()) {
						roots.push_back(inspector);
						relaxed_roots.push_back(inspector);
					}
				}
			}
		}

		EditorAutomationSnapshotOptions options = p_options;
		options.relaxed_visibility_roots = !relaxed_roots.is_empty();
		EditorAutomationSnapshot snapshot;
		EditorAutomationSnapshotBuilder builder(snapshot.data, options);
		for (Node *root : relaxed_roots) {
			builder.add_relaxed_visibility_root(root);
		}
		builder.build_from_roots(roots);
		return snapshot;
	}
	return capture_from_roots(roots, p_options);
}

const EditorAutomationElement *EditorAutomationSnapshot::find_by_id(const String &p_id) const {
	const int *index = data.id_to_index.getptr(p_id);
	if (index == nullptr) {
		return nullptr;
	}
	return &data.elements[*index];
}

const EditorAutomationElement *EditorAutomationSnapshot::find_by_handle(const String &p_handle) const {
	const int *index = data.handle_to_index.getptr(p_handle);
	if (index == nullptr) {
		return nullptr;
	}
	return &data.elements[*index];
}

const EditorAutomationElement *EditorAutomationSnapshot::find_by_durable_key(const String &p_kind, const String &p_key) const {
	return find_by_handle(make_durable_handle(p_kind, p_key));
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
	if (snapshot_generation > 0) {
		dict["snapshot_generation"] = snapshot_generation;
	}
	if (reconciled) {
		dict["reconciled"] = true;
	}
	if (!requested_reference.is_empty()) {
		dict["requested_reference"] = requested_reference;
	}
	if (!current_element_id.is_empty()) {
		dict["current_element_id"] = current_element_id;
	}
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
