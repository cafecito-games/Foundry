/**************************************************************************/
/*  editor_automation_driver.cpp                                          */
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

#include "editor_automation_driver.h"

#include "editor/automation/editor_automation_selector.h"
#include "core/input/input_event.h"
#include "core/object/object.h"
#include "core/os/keyboard.h"
#include "scene/gui/base_button.h"
#include "scene/gui/code_edit.h"
#include "scene/gui/control.h"
#include "scene/gui/item_list.h"
#include "scene/gui/line_edit.h"
#include "scene/gui/option_button.h"
#include "scene/gui/popup_menu.h"
#include "scene/gui/range.h"
#include "scene/gui/spin_box.h"
#include "scene/gui/tab_bar.h"
#include "scene/gui/tab_container.h"
#include "scene/gui/text_edit.h"
#include "scene/gui/tree.h"
#include "scene/main/viewport.h"
#include "scene/main/window.h"

namespace {

EditorAutomationActionResult _selector_failure(const EditorAutomationSelectorResult &p_selector_result) {
	String kind = p_selector_result.error_kind;
	if (p_selector_result.status == EditorAutomationSelectorStatus::STALE_ID) {
		if (kind == "invalid_snapshot_handle") {
			kind = "invalid_element";
		} else {
			kind = "stale_element";
		}
	} else if (p_selector_result.status == EditorAutomationSelectorStatus::AMBIGUOUS) {
		kind = "ambiguous_selector";
	} else if (p_selector_result.status == EditorAutomationSelectorStatus::NO_MATCH) {
		kind = "no_match";
	} else if (kind.is_empty()) {
		kind = "invalid_selector";
	}

	EditorAutomationActionResult result = EditorAutomationActionResult::failure(kind, p_selector_result.message, p_selector_result.candidates);
	return result;
}

EditorAutomationSelectorResult _resolve_target(const EditorAutomationSnapshot &p_snapshot, const Dictionary &p_target) {
	if (p_target.is_empty()) {
		EditorAutomationSelectorResult result;
		result.status = EditorAutomationSelectorStatus::INVALID_SELECTOR;
		result.error_kind = "invalid_target";
		result.message = "Action target is empty.";
		return result;
	}
	return EditorAutomationSelector::resolve(p_snapshot, p_target);
}

Node *_resolve_node_from_object_id(uint64_t p_object_id) {
	if (p_object_id == 0) {
		return nullptr;
	}
	return Object::cast_to<Node>(ObjectDB::get_instance(ObjectID(p_object_id)));
}

bool _parse_virtual_key(const EditorAutomationElement &p_element, String &r_kind, String &r_key) {
	uint64_t generation = 0;
	return EditorAutomationSnapshot::parse_element_id(p_element.id, generation, r_kind, r_key);
}

TreeItem *_resolve_tree_item(Tree *p_tree, const String &p_path) {
	ERR_FAIL_NULL_V(p_tree, nullptr);
	TreeItem *item = p_tree->get_root();
	ERR_FAIL_NULL_V(item, nullptr);
	if (p_path.is_empty() || p_path == "/") {
		return item;
	}
	PackedStringArray parts = p_path.split("/", false);
	for (const String &part : parts) {
		item = item->get_child(part.to_int());
		ERR_FAIL_NULL_V(item, nullptr);
	}
	return item;
}

Viewport *_viewport_for_node(Node *p_node) {
	if (p_node == nullptr || !p_node->is_inside_tree()) {
		return nullptr;
	}
	return p_node->get_viewport();
}

String _focused_element_id(const EditorAutomationSnapshot &p_snapshot) {
	Viewport *viewport = nullptr;
	for (int i = 0; i < p_snapshot.get_element_count(); i++) {
		const EditorAutomationElement &element = p_snapshot.get_element(i);
		Node *node = _resolve_node_from_object_id(element.object_id);
		Viewport *candidate = _viewport_for_node(node);
		if (candidate != nullptr) {
			viewport = candidate;
			break;
		}
	}
	if (viewport == nullptr) {
		return p_snapshot.get_focused_element_id();
	}

	Control *focus_owner = viewport->gui_get_focus_owner();
	if (focus_owner != nullptr) {
		return EditorAutomationSnapshot::make_control_element_id(p_snapshot.get_generation(), focus_owner->get_instance_id());
	}
	return String();
}

bool _semantic_click_available(Node *p_node) {
	return Object::cast_to<BaseButton>(p_node) != nullptr;
}

bool _semantic_set_text_available(Node *p_node) {
	return Object::cast_to<LineEdit>(p_node) != nullptr ||
			Object::cast_to<TextEdit>(p_node) != nullptr ||
			Object::cast_to<CodeEdit>(p_node) != nullptr;
}

bool _perform_semantic_click(BaseButton *p_button, PackedStringArray &r_events) {
	if (p_button->is_toggle_mode()) {
		p_button->set_pressed(!p_button->is_pressed());
	}
	p_button->emit_signal(SceneStringName(pressed));
	r_events.push_back("pressed");
	return true;
}

bool _push_mouse_click(Control *p_control, const Rect2i &p_bounds, PackedStringArray &r_events) {
	Viewport *viewport = _viewport_for_node(p_control);
	ERR_FAIL_NULL_V(viewport, false);

	const Vector2 global_position = Rect2(p_bounds.position, p_bounds.size).get_center();
	Vector2 local_position = global_position;
	if (p_control->is_inside_tree()) {
		local_position = p_control->get_global_transform_with_canvas().affine_inverse().xform(global_position);
	}

	Ref<InputEventMouseButton> press;
	press.instantiate();
	press->set_button_index(MouseButton::LEFT);
	press->set_pressed(true);
	press->set_position(local_position);
	press->set_global_position(global_position);
	viewport->push_input(press);

	Ref<InputEventMouseButton> release;
	release.instantiate();
	release->set_button_index(MouseButton::LEFT);
	release->set_pressed(false);
	release->set_position(local_position);
	release->set_global_position(global_position);
	viewport->push_input(release);

	r_events.push_back("mouse_pressed");
	r_events.push_back("mouse_released");
	return true;
}

bool _push_key_event(Viewport *p_viewport, Key p_key, bool p_pressed, char32_t p_unicode, PackedStringArray &r_events) {
	ERR_FAIL_NULL_V(p_viewport, false);

	Ref<InputEventKey> event;
	event.instantiate();
	event->set_keycode(p_key);
	event->set_physical_keycode(p_key);
	event->set_pressed(p_pressed);
	if (p_unicode != 0) {
		event->set_unicode(p_unicode);
	}
	p_viewport->push_input(event);

	if (p_pressed) {
		r_events.push_back("key_pressed");
	} else {
		r_events.push_back("key_released");
	}
	return true;
}

Viewport *_focused_viewport(const EditorAutomationSnapshot &p_snapshot) {
	for (int i = 0; i < p_snapshot.get_element_count(); i++) {
		const EditorAutomationElement &element = p_snapshot.get_element(i);
		Node *node = _resolve_node_from_object_id(element.object_id);
		Viewport *viewport = _viewport_for_node(node);
		if (viewport != nullptr) {
			return viewport;
		}
	}
	return nullptr;
}

EditorAutomationActionResult _action_focus(
		const EditorAutomationSnapshot &p_snapshot,
		const EditorAutomationElement &p_element,
		EditorAutomationRoutePreference p_route_preference) {
	(void)p_route_preference;

	Node *node = _resolve_node_from_object_id(p_element.object_id);
	if (Control *control = Object::cast_to<Control>(node)) {
		control->grab_focus();
		EditorAutomationActionResult result = EditorAutomationActionResult::success(EditorAutomationActionRouteNames::SEMANTIC_FOCUS, p_element.id);
		result.focus = _focused_element_id(p_snapshot);
		result.events.push_back("focused");
		return result;
	}
	if (Window *window = Object::cast_to<Window>(node)) {
		window->grab_focus();
		EditorAutomationActionResult result = EditorAutomationActionResult::success(EditorAutomationActionRouteNames::SEMANTIC_FOCUS, p_element.id);
		result.focus = _focused_element_id(p_snapshot);
		result.events.push_back("focused");
		return result;
	}
	return EditorAutomationActionResult::failure("unsupported_action", "Focus is only supported on focusable controls and windows.");
}

EditorAutomationActionResult _action_click(
		const EditorAutomationSnapshot &p_snapshot,
		const EditorAutomationElement &p_element,
		EditorAutomationRoutePreference p_route_preference) {
	Node *node = _resolve_node_from_object_id(p_element.object_id);
	Control *control = Object::cast_to<Control>(node);
	ERR_FAIL_NULL_V(control, EditorAutomationActionResult::failure("invalid_element", "The selected element is not a control."));

	const bool semantic_available = _semantic_click_available(node);
	const bool try_semantic = p_route_preference != EditorAutomationRoutePreference::INPUT && semantic_available;
	const bool try_input = p_route_preference != EditorAutomationRoutePreference::SEMANTIC;

	if (try_semantic) {
		BaseButton *button = Object::cast_to<BaseButton>(node);
		PackedStringArray events;
		if (button != nullptr && _perform_semantic_click(button, events)) {
			EditorAutomationActionResult result = EditorAutomationActionResult::success(EditorAutomationActionRouteNames::SEMANTIC_CLICK, p_element.id);
			result.events = events;
			result.focus = _focused_element_id(p_snapshot);
			return result;
		}
		if (p_route_preference == EditorAutomationRoutePreference::SEMANTIC) {
			return EditorAutomationActionResult::failure("unsupported_route", "Semantic click is unavailable for the selected control.");
		}
	}

	if (try_input) {
		PackedStringArray events;
		if (_push_mouse_click(control, p_element.bounds, events)) {
			EditorAutomationActionResult result = EditorAutomationActionResult::success(EditorAutomationActionRouteNames::INPUT_MOUSE_CLICK, p_element.id);
			result.events = events;
			result.focus = _focused_element_id(p_snapshot);
			return result;
		}
		if (p_route_preference == EditorAutomationRoutePreference::INPUT) {
			return EditorAutomationActionResult::failure("unsupported_route", "Input click is unavailable for the selected control.");
		}
	}

	return EditorAutomationActionResult::failure("unsupported_action", "Click is unavailable for the selected control.");
}

EditorAutomationActionResult _action_set_text(
		const EditorAutomationSnapshot &p_snapshot,
		const EditorAutomationElement &p_element,
		const String &p_text,
		EditorAutomationRoutePreference p_route_preference) {
	(void)p_snapshot;
	Node *node = _resolve_node_from_object_id(p_element.object_id);

	const bool semantic_available = _semantic_set_text_available(node);
	if (p_route_preference == EditorAutomationRoutePreference::INPUT) {
		return EditorAutomationActionResult::failure("unsupported_route", "set_text requires semantic control APIs. Use type_text with route=input for synthesized typing.");
	}
	if (p_route_preference == EditorAutomationRoutePreference::SEMANTIC && !semantic_available) {
		return EditorAutomationActionResult::failure("unsupported_route", "Semantic text setting is unavailable for the selected control.");
	}
	if (!semantic_available && p_route_preference == EditorAutomationRoutePreference::AUTO) {
		return EditorAutomationActionResult::failure("unsupported_action", "Text setting is unavailable for the selected control.");
	}

	if (LineEdit *line_edit = Object::cast_to<LineEdit>(node)) {
		line_edit->set_text(p_text);
		EditorAutomationActionResult result = EditorAutomationActionResult::success(EditorAutomationActionRouteNames::SEMANTIC_SET_TEXT, p_element.id);
		result.events.push_back("text_changed");
		return result;
	}
	if (TextEdit *text_edit = Object::cast_to<TextEdit>(node)) {
		text_edit->set_text(p_text);
		EditorAutomationActionResult result = EditorAutomationActionResult::success(EditorAutomationActionRouteNames::SEMANTIC_SET_TEXT, p_element.id);
		result.events.push_back("text_changed");
		return result;
	}
	if (CodeEdit *code_edit = Object::cast_to<CodeEdit>(node)) {
		code_edit->set_text(p_text);
		EditorAutomationActionResult result = EditorAutomationActionResult::success(EditorAutomationActionRouteNames::SEMANTIC_SET_TEXT, p_element.id);
		result.events.push_back("text_changed");
		return result;
	}

	return EditorAutomationActionResult::failure("unsupported_action", "Text setting is unavailable for the selected control.");
}

EditorAutomationActionResult _action_type_text(
		const EditorAutomationSnapshot &p_snapshot,
		const EditorAutomationElement &p_element,
		const String &p_text,
		EditorAutomationRoutePreference p_route_preference) {
	if (p_route_preference == EditorAutomationRoutePreference::SEMANTIC) {
		return EditorAutomationActionResult::failure("unsupported_route", "type_text requires synthesized input events. Use set_text with route=semantic instead.");
	}

	Node *node = _resolve_node_from_object_id(p_element.object_id);
	Control *control = Object::cast_to<Control>(node);
	ERR_FAIL_NULL_V(control, EditorAutomationActionResult::failure("invalid_element", "The selected element is not a text control."));

	control->grab_focus();
	Viewport *viewport = _viewport_for_node(control);
	ERR_FAIL_NULL_V(viewport, EditorAutomationActionResult::failure("unsupported_route", "No viewport is available for text input."));

	PackedStringArray events;
	for (int i = 0; i < p_text.length(); i++) {
		const char32_t codepoint = p_text[i];
		const Key key = Key(codepoint);
		_push_key_event(viewport, key, true, codepoint, events);
		_push_key_event(viewport, key, false, codepoint, events);
	}

	EditorAutomationActionResult result = EditorAutomationActionResult::success(EditorAutomationActionRouteNames::INPUT_TEXT, p_element.id);
	result.events = events;
	result.focus = _focused_element_id(p_snapshot);
	return result;
}

EditorAutomationActionResult _action_press_key(
		const EditorAutomationSnapshot &p_snapshot,
		const String &p_key_name,
		EditorAutomationRoutePreference p_route_preference) {
	if (p_route_preference == EditorAutomationRoutePreference::SEMANTIC) {
		return EditorAutomationActionResult::failure("unsupported_route", "press_key requires synthesized input events.");
	}

	const Key key = find_keycode(p_key_name);
	if (key == Key::NONE) {
		return EditorAutomationActionResult::failure("invalid_parameter", vformat("Unknown key '%s'.", p_key_name));
	}

	Viewport *viewport = _focused_viewport(p_snapshot);
	ERR_FAIL_NULL_V(viewport, EditorAutomationActionResult::failure("unsupported_route", "No focused viewport is available for key input."));

	PackedStringArray events;
	_push_key_event(viewport, key, true, 0, events);
	_push_key_event(viewport, key, false, 0, events);

	EditorAutomationActionResult result = EditorAutomationActionResult::success(EditorAutomationActionRouteNames::INPUT_KEY, String());
	result.events = events;
	result.focus = _focused_element_id(p_snapshot);
	return result;
}

EditorAutomationActionResult _action_select_virtual(
		const EditorAutomationSnapshot &p_snapshot,
		const EditorAutomationElement &p_element,
		const String &p_kind,
		const String &p_key) {
	if (p_kind == "tree_item") {
		const int separator = p_key.find_char(':');
		ERR_FAIL_COND_V(separator < 0, EditorAutomationActionResult::failure("invalid_element", "Malformed tree item id."));
		const uint64_t tree_id = p_key.substr(0, separator).to_int();
		const String path = p_key.substr(separator + 1);
		Tree *tree = Object::cast_to<Tree>(ObjectDB::get_instance(ObjectID(tree_id)));
		ERR_FAIL_NULL_V(tree, EditorAutomationActionResult::failure("invalid_element", "Tree item parent is no longer available."));
		TreeItem *item = _resolve_tree_item(tree, path);
		ERR_FAIL_NULL_V(item, EditorAutomationActionResult::failure("invalid_element", "Tree item is no longer available."));
		tree->set_selected(item, 0);
		EditorAutomationActionResult result = EditorAutomationActionResult::success(EditorAutomationActionRouteNames::SEMANTIC_SELECT, p_element.id);
		result.events.push_back("selected");
		result.focus = _focused_element_id(p_snapshot);
		return result;
	}
	if (p_kind == "list_item") {
		const int separator = p_key.find_char(':');
		ERR_FAIL_COND_V(separator < 0, EditorAutomationActionResult::failure("invalid_element", "Malformed list item id."));
		const uint64_t list_id = p_key.substr(0, separator).to_int();
		const int index = p_key.substr(separator + 1).to_int();
		ItemList *item_list = Object::cast_to<ItemList>(ObjectDB::get_instance(ObjectID(list_id)));
		ERR_FAIL_NULL_V(item_list, EditorAutomationActionResult::failure("invalid_element", "List item parent is no longer available."));
		ERR_FAIL_INDEX_V(index, item_list->get_item_count(), EditorAutomationActionResult::failure("invalid_element", "List item index is out of range."));
		item_list->select(index);
		EditorAutomationActionResult result = EditorAutomationActionResult::success(EditorAutomationActionRouteNames::SEMANTIC_SELECT, p_element.id);
		result.events.push_back("selected");
		result.focus = _focused_element_id(p_snapshot);
		return result;
	}
	if (p_kind == "menu_item") {
		const int separator = p_key.find_char(':');
		ERR_FAIL_COND_V(separator < 0, EditorAutomationActionResult::failure("invalid_element", "Malformed menu item id."));
		const uint64_t menu_id = p_key.substr(0, separator).to_int();
		const int index = p_key.substr(separator + 1).to_int();
		PopupMenu *popup_menu = Object::cast_to<PopupMenu>(ObjectDB::get_instance(ObjectID(menu_id)));
		ERR_FAIL_NULL_V(popup_menu, EditorAutomationActionResult::failure("invalid_element", "Menu item parent is no longer available."));
		ERR_FAIL_INDEX_V(index, popup_menu->get_item_count(), EditorAutomationActionResult::failure("invalid_element", "Menu item index is out of range."));
		popup_menu->activate_item(index);
		EditorAutomationActionResult result = EditorAutomationActionResult::success(EditorAutomationActionRouteNames::SEMANTIC_SELECT, p_element.id);
		result.events.push_back("activated");
		result.focus = _focused_element_id(p_snapshot);
		return result;
	}
	if (p_kind == "tab") {
		const int separator = p_key.find_char(':');
		ERR_FAIL_COND_V(separator < 0, EditorAutomationActionResult::failure("invalid_element", "Malformed tab id."));
		const uint64_t tab_owner_id = p_key.substr(0, separator).to_int();
		const int index = p_key.substr(separator + 1).to_int();
		Node *tab_owner = Object::cast_to<Node>(ObjectDB::get_instance(ObjectID(tab_owner_id)));
		if (TabBar *tab_bar = Object::cast_to<TabBar>(tab_owner)) {
			tab_bar->set_current_tab(index);
			EditorAutomationActionResult result = EditorAutomationActionResult::success(EditorAutomationActionRouteNames::SEMANTIC_SELECT, p_element.id);
			result.events.push_back("selected");
			result.focus = _focused_element_id(p_snapshot);
			return result;
		}
		if (TabContainer *tab_container = Object::cast_to<TabContainer>(tab_owner)) {
			tab_container->set_current_tab(index);
			EditorAutomationActionResult result = EditorAutomationActionResult::success(EditorAutomationActionRouteNames::SEMANTIC_SELECT, p_element.id);
			result.events.push_back("selected");
			result.focus = _focused_element_id(p_snapshot);
			return result;
		}
		return EditorAutomationActionResult::failure("invalid_element", "Tab parent is no longer available.");
	}
	return EditorAutomationActionResult::failure("unsupported_action", vformat("Selection is unavailable for virtual element kind '%s'.", p_kind));
}

EditorAutomationActionResult _action_select(
		const EditorAutomationSnapshot &p_snapshot,
		const EditorAutomationElement &p_element,
		const Variant &p_value) {
	if (p_element.object_id == 0) {
		String kind;
		String key;
		ERR_FAIL_COND_V(!_parse_virtual_key(p_element, kind, key), EditorAutomationActionResult::failure("invalid_element", "Malformed virtual element id."));
		return _action_select_virtual(p_snapshot, p_element, kind, key);
	}

	Node *node = _resolve_node_from_object_id(p_element.object_id);
	if (OptionButton *option_button = Object::cast_to<OptionButton>(node)) {
		int index = option_button->get_selected();
		if (p_value.get_type() != Variant::NIL) {
			if (p_value.get_type() == Variant::INT) {
				index = p_value;
			} else {
				const String text = p_value;
				index = -1;
				for (int i = 0; i < option_button->get_item_count(); i++) {
					if (option_button->get_item_text(i) == text) {
						index = i;
						break;
					}
				}
				ERR_FAIL_COND_V(index < 0, EditorAutomationActionResult::failure("invalid_parameter", vformat("Option '%s' was not found.", text)));
			}
		}
		option_button->select(index);
		EditorAutomationActionResult result = EditorAutomationActionResult::success(EditorAutomationActionRouteNames::SEMANTIC_SELECT, p_element.id);
		result.events.push_back("selected");
		result.focus = _focused_element_id(p_snapshot);
		return result;
	}

	return EditorAutomationActionResult::failure("unsupported_action", "Selection is unavailable for the selected control.");
}

EditorAutomationActionResult _action_tree_item_state(
		const EditorAutomationSnapshot &p_snapshot,
		const EditorAutomationElement &p_element,
		bool p_expand) {
	String kind;
	String key;
	ERR_FAIL_COND_V(!_parse_virtual_key(p_element, kind, key), EditorAutomationActionResult::failure("invalid_element", "Malformed virtual element id."));
	ERR_FAIL_COND_V(kind != "tree_item", EditorAutomationActionResult::failure("unsupported_action", "Expand/collapse is only supported for tree items."));

	const int separator = key.find_char(':');
	ERR_FAIL_COND_V(separator < 0, EditorAutomationActionResult::failure("invalid_element", "Malformed tree item id."));
	const uint64_t tree_id = key.substr(0, separator).to_int();
	const String path = key.substr(separator + 1);
	Tree *tree = Object::cast_to<Tree>(ObjectDB::get_instance(ObjectID(tree_id)));
	ERR_FAIL_NULL_V(tree, EditorAutomationActionResult::failure("invalid_element", "Tree item parent is no longer available."));
	TreeItem *item = _resolve_tree_item(tree, path);
	ERR_FAIL_NULL_V(item, EditorAutomationActionResult::failure("invalid_element", "Tree item is no longer available."));
	item->set_collapsed(!p_expand);

	EditorAutomationActionResult result = EditorAutomationActionResult::success(EditorAutomationActionRouteNames::SEMANTIC_SELECT, p_element.id);
	result.events.push_back(p_expand ? "expanded" : "collapsed");
	result.focus = _focused_element_id(p_snapshot);
	return result;
}

EditorAutomationActionResult _action_set_value(
		const EditorAutomationSnapshot &p_snapshot,
		const EditorAutomationElement &p_element,
		const Variant &p_value,
		EditorAutomationRoutePreference p_route_preference) {
	(void)p_snapshot;
	if (p_route_preference == EditorAutomationRoutePreference::INPUT) {
		return EditorAutomationActionResult::failure("unsupported_route", "set_value requires semantic control APIs.");
	}

	Node *node = _resolve_node_from_object_id(p_element.object_id);
	Range *range = Object::cast_to<Range>(node);
	ERR_FAIL_NULL_V(range, EditorAutomationActionResult::failure("unsupported_action", "Value setting is only supported on Range controls such as SpinBox."));

	double value = range->get_value();
	if (p_value.get_type() == Variant::INT || p_value.get_type() == Variant::FLOAT) {
		value = p_value;
	} else if (p_value.get_type() == Variant::STRING) {
		value = String(p_value).to_float();
	} else {
		return EditorAutomationActionResult::failure("invalid_parameter", "set_value requires a numeric value.");
	}
	range->set_value(value);

	EditorAutomationActionResult result = EditorAutomationActionResult::success(EditorAutomationActionRouteNames::SEMANTIC_SET_VALUE, p_element.id);
	result.events.push_back("value_changed");
	return result;
}

EditorAutomationActionResult _action_adjust_value(
		const EditorAutomationSnapshot &p_snapshot,
		const EditorAutomationElement &p_element,
		bool p_increment,
		EditorAutomationRoutePreference p_route_preference) {
	(void)p_snapshot;
	if (p_route_preference == EditorAutomationRoutePreference::INPUT) {
		return EditorAutomationActionResult::failure("unsupported_route", "increment/decrement require semantic control APIs.");
	}

	Node *node = _resolve_node_from_object_id(p_element.object_id);
	Range *range = Object::cast_to<Range>(node);
	ERR_FAIL_NULL_V(range, EditorAutomationActionResult::failure("unsupported_action", "Value adjustment is only supported on Range controls such as SpinBox."));

	const double step = range->get_step();
	range->set_value(range->get_value() + (p_increment ? step : -step));

	EditorAutomationActionResult result = EditorAutomationActionResult::success(EditorAutomationActionRouteNames::SEMANTIC_SET_VALUE, p_element.id);
	result.events.push_back(p_increment ? "incremented" : "decremented");
	return result;
}

String _read_string_option(const Dictionary &p_options, const char *p_key) {
	if (!p_options.has(p_key)) {
		return String();
	}
	return p_options.get(p_key, Variant());
}

Variant _read_variant_option(const Dictionary &p_options, const char *p_key) {
	if (!p_options.has(p_key)) {
		return Variant();
	}
	return p_options.get(p_key, Variant());
}

} // namespace

EditorAutomationActionResult EditorAutomationDriver::perform(
		const EditorAutomationSnapshot &p_snapshot,
		const String &p_action,
		const Dictionary &p_target,
		const Dictionary &p_options) {
	const EditorAutomationActionKind action_kind = editor_automation_action_kind_from_string(p_action);
	if (action_kind == EditorAutomationActionKind::UNKNOWN) {
		return EditorAutomationActionResult::failure("unsupported_action", vformat("Unsupported action '%s'.", p_action));
	}

	const EditorAutomationRoutePreference route_preference = editor_automation_route_preference_from_string(_read_string_option(p_options, "route"));

	if (action_kind == EditorAutomationActionKind::PRESS_KEY) {
		const String key_name = _read_string_option(p_options, "key");
		if (key_name.is_empty()) {
			return EditorAutomationActionResult::failure("invalid_parameter", "press_key requires a `key` option.");
		}
		return _action_press_key(p_snapshot, key_name, route_preference);
	}

	const EditorAutomationSelectorResult selector_result = _resolve_target(p_snapshot, p_target);
	if (selector_result.status != EditorAutomationSelectorStatus::OK) {
		return _selector_failure(selector_result);
	}
	ERR_FAIL_COND_V(selector_result.match_indices.size() != 1, EditorAutomationActionResult::failure("ambiguous_selector", "Selector matched multiple elements."));
	const EditorAutomationElement &element = p_snapshot.get_element(selector_result.match_indices[0]);

	switch (action_kind) {
		case EditorAutomationActionKind::FOCUS:
			return _action_focus(p_snapshot, element, route_preference);
		case EditorAutomationActionKind::CLICK:
			return _action_click(p_snapshot, element, route_preference);
		case EditorAutomationActionKind::SET_TEXT: {
			if (!p_options.has("text")) {
				return EditorAutomationActionResult::failure("invalid_parameter", "set_text requires a `text` option.");
			}
			const String text = _read_string_option(p_options, "text");
			return _action_set_text(p_snapshot, element, text, route_preference);
		}
		case EditorAutomationActionKind::TYPE_TEXT: {
			const String text = _read_string_option(p_options, "text");
			if (text.is_empty()) {
				return EditorAutomationActionResult::failure("invalid_parameter", "type_text requires a `text` option.");
			}
			return _action_type_text(p_snapshot, element, text, route_preference);
		}
		case EditorAutomationActionKind::SELECT:
			return _action_select(p_snapshot, element, _read_variant_option(p_options, "value"));
		case EditorAutomationActionKind::EXPAND:
			return _action_tree_item_state(p_snapshot, element, true);
		case EditorAutomationActionKind::COLLAPSE:
			return _action_tree_item_state(p_snapshot, element, false);
		case EditorAutomationActionKind::CHOOSE_MENU_ITEM: {
			String kind;
			String key;
			if (!_parse_virtual_key(element, kind, key) || kind != "menu_item") {
				return EditorAutomationActionResult::failure("unsupported_action", "choose_menu_item requires a menu item element.");
			}
			return _action_select_virtual(p_snapshot, element, kind, key);
		}
		case EditorAutomationActionKind::SET_VALUE:
			return _action_set_value(p_snapshot, element, _read_variant_option(p_options, "value"), route_preference);
		case EditorAutomationActionKind::INCREMENT:
			return _action_adjust_value(p_snapshot, element, true, route_preference);
		case EditorAutomationActionKind::DECREMENT:
			return _action_adjust_value(p_snapshot, element, false, route_preference);
		default:
			return EditorAutomationActionResult::failure("unsupported_action", vformat("Unsupported action '%s'.", p_action));
	}
}
