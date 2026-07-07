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

#include "core/input/input_event.h"
#include "core/object/object.h"
#include "core/os/keyboard.h"
#include "editor/automation/editor_automation_diagnostics.h"
#include "editor/automation/editor_automation_input.h"
#include "editor/automation/editor_automation_log.h"
#include "editor/automation/editor_automation_selector.h"
#include "editor/automation/editor_automation_state.h"
#include "editor/automation/editor_automation_trace.h"
#include "editor/automation/editor_automation_workflow.h"
#include "editor/automation/editor_automation_workspace.h"
#include "editor/editor_node.h"
#include "editor/editor_scene_pane_tile.h"
#include "editor/inspector/editor_inspector.h"
#include "editor/inspector/editor_properties.h"
#include "scene/gui/base_button.h"
#include "scene/gui/check_box.h"
#include "scene/gui/code_edit.h"
#include "scene/gui/control.h"
#include "scene/gui/item_list.h"
#include "scene/gui/line_edit.h"
#include "scene/gui/menu_button.h"
#include "scene/gui/option_button.h"
#include "scene/gui/popup_menu.h"
#include "scene/gui/range.h"
#include "scene/gui/scroll_bar.h"
#include "scene/gui/scroll_container.h"
#include "scene/gui/spin_box.h"
#include "scene/gui/tab_bar.h"
#include "scene/gui/tab_container.h"
#include "scene/gui/text_edit.h"
#include "scene/gui/tree.h"
#include "scene/gui/subviewport_container.h"
#include "scene/main/viewport.h"
#include "scene/main/window.h"

namespace {

String _read_string_option(const Dictionary &p_options, const char *p_key);
Variant _read_variant_option(const Dictionary &p_options, const char *p_key);

EditorAutomationActionResult _selector_failure(const EditorAutomationSelectorResult &p_selector_result) {
	String kind = p_selector_result.error_kind;
	if (p_selector_result.status == EditorAutomationSelectorStatus::STALE_ID) {
		if (kind == "invalid_snapshot_handle") {
			kind = "invalid_element";
		} else if (kind == "freed_object") {
			kind = "freed_element";
		} else if (kind == "element_not_visible") {
			kind = "element_not_visible";
		} else if (kind == "virtual_element_unavailable") {
			kind = "virtual_element_unavailable";
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
	if (p_selector_result.snapshot_generation > 0) {
		result.details["snapshot_generation"] = p_selector_result.snapshot_generation;
	}
	if (!p_selector_result.requested_reference.is_empty()) {
		result.details["requested_reference"] = p_selector_result.requested_reference;
	}
	return result;
}

EditorAutomationSelectorResult _resolve_target(const EditorAutomationSnapshot &p_snapshot, const Dictionary &p_target) {
	if (p_target.is_empty()) {
		EditorAutomationSelectorResult result;
		result.status = EditorAutomationSelectorStatus::INVALID_SELECTOR;
		result.error_kind = "invalid_target";
		result.message = "act requires a 'selector' (use handle/id/role/name); 'target' is only the drag destination.";
		return result;
	}
	return EditorAutomationSelector::resolve(p_snapshot, p_target);
}

void _enrich_action_result(
		EditorAutomationActionResult &r_result,
		const EditorAutomationSnapshot &p_snapshot,
		const EditorAutomationSelectorResult &p_selector_result,
		const EditorAutomationElement &p_element) {
	r_result.element_id = p_element.id;
	r_result.details["snapshot_generation"] = p_snapshot.get_generation();
	if (p_selector_result.reconciled) {
		r_result.details["reconciled"] = true;
		r_result.details["requested_reference"] = p_selector_result.requested_reference;
		r_result.details["current_element_id"] = p_element.id;
	}
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

// A MenuButton keeps its popup as a hidden child until shown; while hidden the
// popup's parent is the MenuButton. Automation exposes those hidden items, so
// activating one must replicate what opening the menu would do. A visible popup
// (an already-open menu) is captured directly and needs no such handling.
MenuButton *_hidden_menu_button_owner(PopupMenu *p_popup_menu) {
	if (p_popup_menu == nullptr || p_popup_menu->is_visible()) {
		return nullptr;
	}
	return Object::cast_to<MenuButton>(p_popup_menu->get_parent());
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
	return EditorAutomationInput::viewport_for_node(p_node);
}

Dictionary _element_summary_dict(const EditorAutomationSnapshot &p_snapshot, const EditorAutomationElement &p_element) {
	return EditorAutomationDiagnosticsBuilder::element_summary(p_element);
}

Dictionary _drag_failure_details(
		const EditorAutomationSnapshot &p_snapshot,
		const EditorAutomationElement *p_source,
		const EditorAutomationElement *p_target,
		const String &p_message) {
	Dictionary details;
	if (p_source != nullptr) {
		details["source_element"] = _element_summary_dict(p_snapshot, *p_source);
	}
	if (p_target != nullptr) {
		details["target_element"] = _element_summary_dict(p_snapshot, *p_target);
	}
	details["focused_element_id"] = p_snapshot.get_focused_element_id();
	details["modal_stack"] = EditorAutomationState::capture_modal_stack();
	if (!p_message.is_empty()) {
		details["reason"] = p_message;
	}
	return details;
}

EditorAutomationActionResult _drag_failure(
		const String &p_kind,
		const String &p_message,
		const EditorAutomationSnapshot &p_snapshot,
		const EditorAutomationElement *p_source,
		const EditorAutomationElement *p_target) {
	EditorAutomationActionResult result = EditorAutomationActionResult::failure(p_kind, p_message);
	result.details = _drag_failure_details(p_snapshot, p_source, p_target, p_message);
	return result;
}

EditorAutomationActionResult _prepare_element_for_input(
		const EditorAutomationSnapshot &p_snapshot,
		const EditorAutomationElement &p_element,
		Node *&r_node) {
	r_node = _resolve_node_from_object_id(p_element.object_id);
	if (r_node == nullptr) {
		return EditorAutomationActionResult::failure("invalid_element", "The selected element is no longer available.");
	}

	const EditorAutomationWindowFocusResult focus_result = EditorAutomationInput::ensure_window_focus(r_node);
	if (!focus_result.ok) {
		EditorAutomationActionResult result = EditorAutomationActionResult::failure("window_focus_failed", focus_result.message);
		result.details = _drag_failure_details(p_snapshot, &p_element, nullptr, focus_result.message);
		if (focus_result.window != nullptr) {
			result.details["target_window_object_id"] = String::num_uint64(focus_result.window->get_instance_id());
		}
		return result;
	}
	return EditorAutomationActionResult::success(EditorAutomationActionRouteNames::SEMANTIC_FOCUS, p_element.id);
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

bool _perform_semantic_property_check_click(EditorPropertyCheck *p_property, PackedStringArray &r_events) {
	CheckBox *checkbox = nullptr;
	for (int i = 0; i < p_property->get_child_count(false); i++) {
		Node *child = p_property->get_child(i, false);
		checkbox = Object::cast_to<CheckBox>(child);
		if (checkbox != nullptr) {
			break;
		}
	}
	ERR_FAIL_NULL_V(checkbox, false);

	checkbox->set_pressed(!checkbox->is_pressed());
	checkbox->emit_signal(SceneStringName(pressed));
	r_events.push_back("pressed");
	return true;
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

bool _push_mouse_click(Control *p_control, const Rect2i &p_bounds, const Dictionary &p_options, bool p_viewport_target, PackedStringArray &r_events) {
	Node *node = p_control;
	const EditorAutomationWindowFocusResult focus_result = EditorAutomationInput::ensure_window_focus(node);
	ERR_FAIL_COND_V(!focus_result.ok, false);

	const Vector2 global_position = EditorAutomationInput::resolve_position_in_bounds(
			p_bounds, EditorAutomationInput::position_options_for_source(p_options));
	Vector2 input_position = global_position;
	Viewport *viewport = EditorAutomationInput::input_viewport_for_control(p_control, input_position, global_position);
	ERR_FAIL_NULL_V(viewport, false);

	const EditorAutomationInputModifiers modifiers = EditorAutomationInput::parse_modifiers(p_options.get("modifiers", Variant()));
	const MouseButton button = EditorAutomationInput::parse_mouse_button(String(p_options.get("button", Variant())));
	const MouseButtonMask button_mask = EditorAutomationInput::mouse_button_to_mask(button);
	const bool local_coords = p_viewport_target || Object::cast_to<SubViewport>(viewport) != nullptr;

	if (!EditorAutomationInput::push_mouse_button(viewport, input_position, button, true, button_mask, modifiers, r_events, local_coords)) {
		return false;
	}
	return EditorAutomationInput::push_mouse_button(viewport, input_position, button, false, MouseButtonMask::NONE, modifiers, r_events, local_coords);
}

bool _push_key_event(Viewport *p_viewport, Key p_key, bool p_pressed, char32_t p_unicode, const EditorAutomationInputModifiers &p_modifiers, PackedStringArray &r_events) {
	return EditorAutomationInput::push_key_event(p_viewport, p_key, p_pressed, p_unicode, p_modifiers, r_events);
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

	Node *node = nullptr;
	const EditorAutomationActionResult prepare_result = _prepare_element_for_input(p_snapshot, p_element, node);
	if (!prepare_result.ok) {
		return prepare_result;
	}

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
		const Dictionary &p_options,
		EditorAutomationRoutePreference p_route_preference) {
	Node *node = nullptr;
	const EditorAutomationActionResult prepare_result = _prepare_element_for_input(p_snapshot, p_element, node);
	if (!prepare_result.ok && prepare_result.kind == "window_focus_failed") {
		return prepare_result;
	}

	Control *control = Object::cast_to<Control>(node);
	ERR_FAIL_NULL_V(control, EditorAutomationActionResult::failure("invalid_element", "The selected element is not a control."));

	const bool is_viewport = p_element.role == "viewport";
	const bool try_input = p_route_preference != EditorAutomationRoutePreference::SEMANTIC || is_viewport;
	const bool try_semantic = !is_viewport && p_route_preference != EditorAutomationRoutePreference::INPUT;

	if (try_semantic) {
		PackedStringArray events;
		if (BaseButton *button = Object::cast_to<BaseButton>(node)) {
			if (_perform_semantic_click(button, events)) {
				EditorAutomationActionResult result = EditorAutomationActionResult::success(EditorAutomationActionRouteNames::SEMANTIC_CLICK, p_element.id);
				result.events = events;
				result.focus = _focused_element_id(p_snapshot);
				return result;
			}
		} else if (EditorPropertyCheck *check_property = Object::cast_to<EditorPropertyCheck>(node)) {
			if (_perform_semantic_property_check_click(check_property, events)) {
				EditorAutomationActionResult result = EditorAutomationActionResult::success(EditorAutomationActionRouteNames::SEMANTIC_CLICK, p_element.id);
				result.events = events;
				result.focus = _focused_element_id(p_snapshot);
				return result;
			}
		}
		if (p_route_preference == EditorAutomationRoutePreference::SEMANTIC) {
			return EditorAutomationActionResult::failure("unsupported_route", "Semantic click is unavailable for the selected control.");
		}
	}

	if (try_input) {
		PackedStringArray events;
		if (_push_mouse_click(control, p_element.bounds, p_options, is_viewport, events)) {
			const char *route = is_viewport ? EditorAutomationActionRouteNames::INPUT_VIEWPORT_CLICK : EditorAutomationActionRouteNames::INPUT_MOUSE_CLICK;
			EditorAutomationActionResult result = EditorAutomationActionResult::success(route, p_element.id);
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

EditorAutomationActionResult _action_open_context_menu(
		const EditorAutomationSnapshot &p_snapshot,
		const EditorAutomationElement &p_element,
		const Dictionary &p_options,
		EditorAutomationRoutePreference p_route_preference) {
	if (p_route_preference == EditorAutomationRoutePreference::SEMANTIC) {
		return EditorAutomationActionResult::failure("unsupported_route", "open_context_menu synthesizes a right-click input event; use route=input or auto.");
	}

	Node *node = nullptr;
	const EditorAutomationActionResult prepare_result = _prepare_element_for_input(p_snapshot, p_element, node);
	if (!prepare_result.ok && prepare_result.kind == "window_focus_failed") {
		return prepare_result;
	}

	Control *control = Object::cast_to<Control>(node);
	ERR_FAIL_NULL_V(control, EditorAutomationActionResult::failure("invalid_element", "The selected element is not a control."));

	// Force the right mouse button regardless of any caller-provided `button` so
	// the control's context-menu handler (e.g. EditorInspectorCategory::_gui_input
	// -> _popup_context_menu) fires. Positioning options (position/anchor) are
	// still honored via the shared options dictionary.
	Dictionary options = p_options.duplicate();
	options["button"] = "right";

	PackedStringArray events;
	if (_push_mouse_click(control, p_element.bounds, options, false, events)) {
		EditorAutomationActionResult result = EditorAutomationActionResult::success(EditorAutomationActionRouteNames::INPUT_CONTEXT_MENU, p_element.id);
		result.events = events;
		result.focus = _focused_element_id(p_snapshot);
		return result;
	}
	return EditorAutomationActionResult::failure("unsupported_action", "Opening a context menu is unavailable for the selected control.");
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
		// LineEdit::set_text() intentionally does not emit text_changed, but a
		// real user typing does. Emit it so reactive UIs (incremental search,
		// live validation, filters) respond the same way they would to input.
		line_edit->emit_signal(SceneStringName(text_changed), p_text);
		EditorAutomationActionResult result = EditorAutomationActionResult::success(EditorAutomationActionRouteNames::SEMANTIC_SET_TEXT, p_element.id);
		result.events.push_back("text_changed");
		return result;
	}
	if (TextEdit *text_edit = Object::cast_to<TextEdit>(node)) {
		text_edit->set_text(p_text);
		text_edit->emit_signal(SceneStringName(text_changed));
		EditorAutomationActionResult result = EditorAutomationActionResult::success(EditorAutomationActionRouteNames::SEMANTIC_SET_TEXT, p_element.id);
		result.events.push_back("text_changed");
		return result;
	}
	if (CodeEdit *code_edit = Object::cast_to<CodeEdit>(node)) {
		code_edit->set_text(p_text);
		code_edit->emit_signal(SceneStringName(text_changed));
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
		const Dictionary &p_options,
		EditorAutomationRoutePreference p_route_preference) {
	if (p_route_preference == EditorAutomationRoutePreference::SEMANTIC) {
		return EditorAutomationActionResult::failure("unsupported_route", "type_text requires synthesized input events. Use set_text with route=semantic instead.");
	}

	Node *node = nullptr;
	const EditorAutomationActionResult prepare_result = _prepare_element_for_input(p_snapshot, p_element, node);
	if (!prepare_result.ok) {
		return prepare_result;
	}

	Control *control = Object::cast_to<Control>(node);
	ERR_FAIL_NULL_V(control, EditorAutomationActionResult::failure("invalid_element", "The selected element is not a text control."));

	const bool had_focus = control->has_focus();
	control->grab_focus();
	if (!had_focus) {
		if (LineEdit *line_edit = Object::cast_to<LineEdit>(control)) {
			if (!line_edit->has_selection()) {
				line_edit->set_caret_column(line_edit->get_text().length());
			}
		} else if (TextEdit *text_edit = Object::cast_to<TextEdit>(control)) {
			if (!text_edit->has_selection()) {
				const int last_line = text_edit->get_line_count() - 1;
				text_edit->set_caret_line(last_line);
				text_edit->set_caret_column(text_edit->get_line(last_line).length());
			}
		} else if (CodeEdit *code_edit = Object::cast_to<CodeEdit>(control)) {
			if (!code_edit->has_selection()) {
				const int last_line = code_edit->get_line_count() - 1;
				code_edit->set_caret_line(last_line);
				code_edit->set_caret_column(code_edit->get_line(last_line).length());
			}
		}
	}
	Viewport *viewport = _viewport_for_node(control);
	ERR_FAIL_NULL_V(viewport, EditorAutomationActionResult::failure("unsupported_route", "No viewport is available for text input."));

	const EditorAutomationInputModifiers modifiers = EditorAutomationInput::parse_modifiers(p_options.get("modifiers", Variant()));
	PackedStringArray events;

	auto type_codepoint = [&](char32_t p_codepoint) {
		const Key key = Key(p_codepoint);
		_push_key_event(viewport, key, true, p_codepoint, modifiers, events);
		_push_key_event(viewport, key, false, p_codepoint, modifiers, events);
	};

	for (int i = 0; i < p_text.length(); i++) {
		const char32_t codepoint = p_text[i];
		if (codepoint == '\n') {
			EditorAutomationInput::push_input_action(viewport, SNAME("ui_text_newline"), true, events);
		} else if (codepoint == '\b') {
			EditorAutomationInput::push_input_action(viewport, SNAME("ui_text_backspace"), true, events);
		} else if (codepoint >= 32 && codepoint < 127) {
			type_codepoint(codepoint);
		} else {
			// Non-ASCII/IME composition is not synthesized here. Callers must use
			// semantic set_text or platform-specific composition hooks when added.
			return EditorAutomationActionResult::failure(
					"unsupported_text",
					vformat("type_text only synthesizes plain ASCII input events. Unsupported codepoint U+%04X.", (unsigned int)codepoint));
		}
	}

	EditorAutomationActionResult result = EditorAutomationActionResult::success(EditorAutomationActionRouteNames::INPUT_TEXT, p_element.id);
	result.events = events;
	result.focus = _focused_element_id(p_snapshot);
	return result;
}

EditorAutomationActionResult _action_press_key(
		const EditorAutomationSnapshot &p_snapshot,
		const String &p_key_name,
		const Dictionary &p_options,
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

	const EditorAutomationInputModifiers modifiers = EditorAutomationInput::parse_modifiers(p_options.get("modifiers", Variant()));
	PackedStringArray events;
	_push_key_event(viewport, key, true, 0, modifiers, events);
	_push_key_event(viewport, key, false, 0, modifiers, events);

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
		ERR_FAIL_COND_V(!EditorAutomationWorkflow::select_tree_item_ui(tree, item), EditorAutomationActionResult::failure("unsupported_action", "Tree item selection failed."));
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
		MenuButton *menu_owner = _hidden_menu_button_owner(popup_menu);
		if (menu_owner != nullptr) {
			if (menu_owner->is_disabled()) {
				// A user cannot open a disabled MenuButton, so its commands are inert.
				return EditorAutomationActionResult::failure("element_disabled", "The menu is disabled and cannot be opened.");
			}
			// Open the menu exactly as a user would. show_popup() fires both the
			// MenuButton and PopupMenu about_to_popup signals (letting editor code
			// populate/refresh/disable entries), and the paired activate_item()/hide()
			// below closes it -- keeping the MenuButton's pressed/processing state
			// balanced instead of leaving it stuck open.
			menu_owner->show_popup();
		}
		if (index < 0 || index >= popup_menu->get_item_count()) {
			if (menu_owner != nullptr) {
				popup_menu->hide();
			}
			return EditorAutomationActionResult::failure("invalid_element", "Menu item index is out of range.");
		}
		if (menu_owner != nullptr && popup_menu->get_item_text(index) != p_element.text) {
			// A rebuild on about_to_popup reordered/renamed entries, so the snapshot
			// index no longer maps to the requested command; close and force a
			// re-observe rather than firing the wrong one.
			popup_menu->hide();
			return EditorAutomationActionResult::failure("stale_element", "The menu changed after opening; re-observe before selecting.");
		}
		if (popup_menu->is_item_disabled(index)) {
			// A disabled command is not selectable by a user, so automation must not
			// fire its id_pressed via activate_item() either.
			if (menu_owner != nullptr) {
				popup_menu->hide();
			}
			return EditorAutomationActionResult::failure("element_disabled", "The menu item is disabled and cannot be activated.");
		}
		if (!popup_menu->get_item_submenu(index).is_empty() || popup_menu->get_item_submenu_node(index) != nullptr) {
			// A submenu row opens a child menu rather than emitting an id;
			// activate_item() would not open it, so refuse instead of firing the
			// parent's id_pressed path. (Submenu contents are not yet reachable while
			// the menu is hidden -- tracked as a follow-up.)
			if (menu_owner != nullptr) {
				popup_menu->hide();
			}
			return EditorAutomationActionResult::failure("unsupported_action", "This menu item opens a submenu; its items are not directly selectable.");
		}
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

EditorAutomationActionResult _action_activate_virtual(
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
		ERR_FAIL_COND_V(!EditorAutomationWorkflow::activate_tree_item_ui(tree, item), EditorAutomationActionResult::failure("unsupported_action", "Tree item activation failed."));
		EditorAutomationActionResult result = EditorAutomationActionResult::success(EditorAutomationActionRouteNames::SEMANTIC_ACTIVATE, p_element.id);
		result.events.push_back("activated");
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
		ERR_FAIL_COND_V(!EditorAutomationWorkflow::activate_list_item_ui(item_list, index), EditorAutomationActionResult::failure("unsupported_action", "List item activation failed."));
		EditorAutomationActionResult result = EditorAutomationActionResult::success(EditorAutomationActionRouteNames::SEMANTIC_ACTIVATE, p_element.id);
		result.events.push_back("activated");
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
		MenuButton *menu_owner = _hidden_menu_button_owner(popup_menu);
		if (menu_owner != nullptr) {
			if (menu_owner->is_disabled()) {
				// A user cannot open a disabled MenuButton, so its commands are inert.
				return EditorAutomationActionResult::failure("element_disabled", "The menu is disabled and cannot be opened.");
			}
			// Open the menu exactly as a user would. show_popup() fires both the
			// MenuButton and PopupMenu about_to_popup signals (letting editor code
			// populate/refresh/disable entries), and the paired activate_item()/hide()
			// below closes it -- keeping the MenuButton's pressed/processing state
			// balanced instead of leaving it stuck open.
			menu_owner->show_popup();
		}
		if (index < 0 || index >= popup_menu->get_item_count()) {
			if (menu_owner != nullptr) {
				popup_menu->hide();
			}
			return EditorAutomationActionResult::failure("invalid_element", "Menu item index is out of range.");
		}
		if (menu_owner != nullptr && popup_menu->get_item_text(index) != p_element.text) {
			// A rebuild on about_to_popup reordered/renamed entries, so the snapshot
			// index no longer maps to the requested command; close and force a
			// re-observe rather than firing the wrong one.
			popup_menu->hide();
			return EditorAutomationActionResult::failure("stale_element", "The menu changed after opening; re-observe before selecting.");
		}
		if (popup_menu->is_item_disabled(index)) {
			// A disabled command is not selectable by a user, so automation must not
			// fire its id_pressed via activate_item() either.
			if (menu_owner != nullptr) {
				popup_menu->hide();
			}
			return EditorAutomationActionResult::failure("element_disabled", "The menu item is disabled and cannot be activated.");
		}
		if (!popup_menu->get_item_submenu(index).is_empty() || popup_menu->get_item_submenu_node(index) != nullptr) {
			// A submenu row opens a child menu rather than emitting an id;
			// activate_item() would not open it, so refuse instead of firing the
			// parent's id_pressed path. (Submenu contents are not yet reachable while
			// the menu is hidden -- tracked as a follow-up.)
			if (menu_owner != nullptr) {
				popup_menu->hide();
			}
			return EditorAutomationActionResult::failure("unsupported_action", "This menu item opens a submenu; its items are not directly selectable.");
		}
		popup_menu->activate_item(index);
		EditorAutomationActionResult result = EditorAutomationActionResult::success(EditorAutomationActionRouteNames::SEMANTIC_ACTIVATE, p_element.id);
		result.events.push_back("activated");
		result.focus = _focused_element_id(p_snapshot);
		return result;
	}
	return EditorAutomationActionResult::failure("unsupported_action", vformat("Activation is unavailable for virtual element kind '%s'.", p_kind));
}

EditorAutomationActionResult _action_activate(
		const EditorAutomationSnapshot &p_snapshot,
		const EditorAutomationElement &p_element,
		EditorAutomationRoutePreference p_route_preference) {
	if (p_element.object_id == 0) {
		String kind;
		String key;
		ERR_FAIL_COND_V(!_parse_virtual_key(p_element, kind, key), EditorAutomationActionResult::failure("invalid_element", "Malformed virtual element id."));
		return _action_activate_virtual(p_snapshot, p_element, kind, key);
	}

	if (p_route_preference == EditorAutomationRoutePreference::INPUT) {
		return EditorAutomationActionResult::failure("unsupported_route", "activate requires semantic control APIs for the selected element.");
	}

	Node *node = nullptr;
	const EditorAutomationActionResult prepare_result = _prepare_element_for_input(p_snapshot, p_element, node);
	if (!prepare_result.ok && prepare_result.kind == "window_focus_failed") {
		return prepare_result;
	}

	if (BaseButton *button = Object::cast_to<BaseButton>(node)) {
		PackedStringArray events;
		if (_perform_semantic_click(button, events)) {
			EditorAutomationActionResult result = EditorAutomationActionResult::success(EditorAutomationActionRouteNames::SEMANTIC_ACTIVATE, p_element.id);
			result.events = events;
			result.focus = _focused_element_id(p_snapshot);
			return result;
		}
	}

	return EditorAutomationActionResult::failure("unsupported_action", "Activation is unavailable for the selected control.");
}

EditorAutomationActionResult _action_submit(
		const EditorAutomationSnapshot &p_snapshot,
		const EditorAutomationElement &p_element,
		EditorAutomationRoutePreference p_route_preference) {
	(void)p_snapshot;
	if (p_route_preference == EditorAutomationRoutePreference::INPUT) {
		return EditorAutomationActionResult::failure("unsupported_route", "submit requires semantic control APIs.");
	}

	Node *node = _resolve_node_from_object_id(p_element.object_id);
	LineEdit *line_edit = Object::cast_to<LineEdit>(node);
	ERR_FAIL_NULL_V(line_edit, EditorAutomationActionResult::failure("unsupported_action", "Submit is only supported on LineEdit controls."));

	const String text = line_edit->get_text();
	line_edit->emit_signal(SceneStringName(text_submitted), text);

	EditorAutomationActionResult result = EditorAutomationActionResult::success(EditorAutomationActionRouteNames::SEMANTIC_SUBMIT, p_element.id);
	result.events.push_back("text_submitted");
	result.details["submitted_text"] = text;
	return result;
}

bool _scroll_scroll_bar(ScrollBar *p_bar, double p_delta) {
	if (p_bar == nullptr || !p_bar->is_visible()) {
		return false;
	}
	const double previous = p_bar->get_value();
	if (p_delta == 0.0) {
		return false;
	}
	p_bar->scroll(p_delta);
	return p_bar->get_value() != previous;
}

bool _scroll_scroll_container(ScrollContainer *p_container, bool p_horizontal, double p_delta) {
	ERR_FAIL_NULL_V(p_container, false);
	if (p_horizontal) {
		const int previous = p_container->get_h_scroll();
		p_container->set_h_scroll(previous + (int)p_delta);
		return p_container->get_h_scroll() != previous;
	}
	const int previous = p_container->get_v_scroll();
	p_container->set_v_scroll(previous + (int)p_delta);
	return p_container->get_v_scroll() != previous;
}

bool _parse_scroll_direction(const String &p_direction, bool &r_horizontal, double &r_sign) {
	const String direction = p_direction.to_lower();
	if (direction == "up") {
		r_horizontal = false;
		r_sign = -1.0;
		return true;
	}
	if (direction == "down") {
		r_horizontal = false;
		r_sign = 1.0;
		return true;
	}
	if (direction == "left") {
		r_horizontal = true;
		r_sign = -1.0;
		return true;
	}
	if (direction == "right") {
		r_horizontal = true;
		r_sign = 1.0;
		return true;
	}
	return false;
}

EditorAutomationActionResult _action_scroll(
		const EditorAutomationSnapshot &p_snapshot,
		const EditorAutomationElement &p_element,
		const Dictionary &p_options,
		EditorAutomationRoutePreference p_route_preference) {
	(void)p_snapshot;
	if (p_route_preference == EditorAutomationRoutePreference::INPUT) {
		return EditorAutomationActionResult::failure("unsupported_route", "scroll requires semantic control APIs.");
	}

	Node *node = _resolve_node_from_object_id(p_element.object_id);
	ERR_FAIL_NULL_V(node, EditorAutomationActionResult::failure("invalid_element", "The selected element is no longer available."));

	bool horizontal = false;
	double sign = 1.0;
	const String direction = _read_string_option(p_options, "direction");
	if (!direction.is_empty() && !_parse_scroll_direction(direction, horizontal, sign)) {
		return EditorAutomationActionResult::failure("invalid_parameter", vformat("Unknown scroll direction '%s'.", direction));
	}

	const bool page = p_options.has("page") && (bool)p_options.get("page", Variant());
	double amount = 0.0;
	if (p_options.has("amount")) {
		const Variant amount_value = p_options.get("amount", Variant());
		if (amount_value.get_type() == Variant::INT || amount_value.get_type() == Variant::FLOAT) {
			amount = amount_value;
		} else {
			return EditorAutomationActionResult::failure("invalid_parameter", "scroll `amount` must be numeric.");
		}
	}

	bool scrolled = false;
	ScrollBar *scroll_bar = nullptr;
	if (Tree *tree = Object::cast_to<Tree>(node)) {
		if (horizontal) {
			return EditorAutomationActionResult::failure("unsupported_action", "Horizontal scroll is not available for Tree controls.");
		}
		scroll_bar = tree->get_vscroll_bar();
	} else if (ItemList *item_list = Object::cast_to<ItemList>(node)) {
		scroll_bar = horizontal ? (ScrollBar *)item_list->get_h_scroll_bar() : item_list->get_v_scroll_bar();
	} else if (ScrollContainer *scroll_container = Object::cast_to<ScrollContainer>(node)) {
		if (amount > 0.0) {
			scrolled = _scroll_scroll_container(scroll_container, horizontal, sign * amount);
		} else if (page) {
			const int page_amount = horizontal ? scroll_container->get_size().x : scroll_container->get_size().y;
			scrolled = _scroll_scroll_container(scroll_container, horizontal, sign * MAX(page_amount, 1));
		} else {
			const int line_amount = MAX((horizontal ? scroll_container->get_size().x : scroll_container->get_size().y) / 8, 1);
			scrolled = _scroll_scroll_container(scroll_container, horizontal, sign * line_amount);
		}
	} else {
		return EditorAutomationActionResult::failure("unsupported_action", "Scroll is only supported on Tree, ItemList, and ScrollContainer controls.");
	}

	if (scroll_bar != nullptr) {
		double delta = 0.0;
		if (amount > 0.0) {
			delta = sign * amount;
		} else if (page) {
			delta = sign * scroll_bar->get_page();
		} else {
			delta = sign * scroll_bar->get_page() / ScrollBar::PAGE_DIVISOR;
		}
		scrolled = _scroll_scroll_bar(scroll_bar, delta);
	}

	EditorAutomationActionResult result = EditorAutomationActionResult::success(EditorAutomationActionRouteNames::SEMANTIC_SCROLL, p_element.id);
	result.details["scrolled"] = scrolled;
	if (scrolled) {
		result.events.push_back("scrolled");
	}
	return result;
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

EditorAutomationActionResult _action_inspector_section_state(
		const EditorAutomationSnapshot &p_snapshot,
		const EditorAutomationElement &p_element,
		bool p_expand) {
	Node *node = _resolve_node_from_object_id(p_element.object_id);
	EditorInspectorSection *section = Object::cast_to<EditorInspectorSection>(node);
	ERR_FAIL_NULL_V(section, EditorAutomationActionResult::failure("unsupported_action", "Expand/collapse is only supported for inspector sections."));

	if (p_expand) {
		section->unfold();
	} else {
		section->fold();
	}

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

Vector<Vector2> _read_waypoints(const Dictionary &p_options) {
	Vector<Vector2> waypoints;
	if (!p_options.has("waypoints") && !p_options.has("path")) {
		return waypoints;
	}
	const Variant raw = p_options.has("waypoints") ? p_options.get("waypoints", Variant()) : p_options.get("path", Variant());
	if (raw.get_type() != Variant::ARRAY) {
		return waypoints;
	}
	const Array points = raw;
	for (int i = 0; i < points.size(); i++) {
		const Variant point_value = points[i];
		if (point_value.get_type() == Variant::VECTOR2) {
			waypoints.push_back(point_value);
		} else if (point_value.get_type() == Variant::ARRAY) {
			const Array point = point_value;
			if (point.size() >= 2) {
				waypoints.push_back(Vector2(point[0], point[1]));
			}
		}
	}
	return waypoints;
}

EditorAutomationActionResult _action_drag(
		const EditorAutomationSnapshot &p_snapshot,
		const EditorAutomationElement &p_source_element,
		const Dictionary &p_options,
		EditorAutomationRoutePreference p_route_preference) {
	if (p_route_preference == EditorAutomationRoutePreference::SEMANTIC) {
		return EditorAutomationActionResult::failure("unsupported_route", "drag requires synthesized input events.");
	}

	Node *source_node = nullptr;
	const EditorAutomationActionResult prepare_result = _prepare_element_for_input(p_snapshot, p_source_element, source_node);
	if (!prepare_result.ok) {
		return _drag_failure(prepare_result.kind, prepare_result.message, p_snapshot, &p_source_element, nullptr);
	}

	Control *source_control = Object::cast_to<Control>(source_node);
	ERR_FAIL_NULL_V(source_control, _drag_failure("invalid_element", "Drag source is not a control.", p_snapshot, &p_source_element, nullptr));

	const Vector2 source_position = EditorAutomationInput::resolve_position_in_bounds(
			p_source_element.bounds, EditorAutomationInput::position_options_for_source(p_options));
	Vector2 target_position;
	const EditorAutomationElement *target_element = nullptr;

	if (p_options.has("target")) {
		const Dictionary target_selector = p_options.get("target", Variant());
		const EditorAutomationSelectorResult target_result = EditorAutomationSelector::resolve(p_snapshot, target_selector);
		if (target_result.status != EditorAutomationSelectorStatus::OK || target_result.match_indices.size() != 1) {
			const String message = target_result.message.is_empty() ? "Drag target selector did not resolve to exactly one element." : target_result.message;
			return _drag_failure("invalid_target", message, p_snapshot, &p_source_element, nullptr);
		}
		target_element = &p_snapshot.get_element(target_result.match_indices[0]);
		Node *target_node = nullptr;
		const EditorAutomationActionResult target_prepare = _prepare_element_for_input(p_snapshot, *target_element, target_node);
		if (!target_prepare.ok) {
			return _drag_failure(target_prepare.kind, target_prepare.message, p_snapshot, &p_source_element, target_element);
		}
		target_position = EditorAutomationInput::resolve_position_in_bounds(
				target_element->bounds, EditorAutomationInput::position_options_for_target_element(p_options));
	} else if (p_options.has("target_point")) {
		target_position = EditorAutomationInput::resolve_target_point(p_options);
	} else {
		return _drag_failure("invalid_parameter", "drag requires a `target` selector or `target_point`.", p_snapshot, &p_source_element, nullptr);
	}

	Viewport *viewport = _viewport_for_node(source_control);
	ERR_FAIL_NULL_V(viewport, _drag_failure("unsupported_route", "No viewport is available for drag input.", p_snapshot, &p_source_element, target_element));

	const EditorAutomationInputModifiers modifiers = EditorAutomationInput::parse_modifiers(p_options.get("modifiers", Variant()));
	const MouseButton button = EditorAutomationInput::parse_mouse_button(String(p_options.get("button", Variant())));
	const Vector<Vector2> waypoints = _read_waypoints(p_options);

	PackedStringArray events;
	if (!EditorAutomationInput::push_mouse_drag(viewport, source_position, target_position, waypoints, button, modifiers, events)) {
		return _drag_failure("unsupported_route", "Drag input could not be dispatched.", p_snapshot, &p_source_element, target_element);
	}

	EditorAutomationActionResult result = EditorAutomationActionResult::success(EditorAutomationActionRouteNames::INPUT_DRAG, p_source_element.id);
	result.events = events;
	result.focus = _focused_element_id(p_snapshot);
	return result;
}

EditorAutomationActionResult _action_dock(
		const EditorAutomationSnapshot &p_snapshot,
		const EditorAutomationElement &p_source_element,
		const Dictionary &p_options,
		EditorAutomationRoutePreference p_route_preference) {
	String region_name = _read_string_option(p_options, "region");
	if (region_name.is_empty()) {
		region_name = "center";
	}
	const EditorSceneWorkspace::TileDropRegion region = EditorAutomationWorkspace::parse_drop_region(region_name);
	const int target_tile_id = EditorAutomationWorkspace::resolve_target_tile_id(p_options, p_snapshot);
	if (target_tile_id < 0) {
		return _drag_failure(
				"invalid_parameter",
				"dock requires `target_tile_id` or a tile container `target_tile`/`target` selector.",
				p_snapshot,
				&p_source_element,
				nullptr);
	}

	int source_tile_id = -1;
	int source_tab = -1;
	if (EditorAutomationWorkspace::resolve_scene_tab_source(p_source_element, source_tile_id, source_tab)) {
		EditorNode *editor_node = EditorNode::get_singleton();
		EditorData *editor_data = editor_node != nullptr ? &EditorNode::get_editor_data() : nullptr;
		EditorSceneWorkspace *workspace = EditorNode::get_scene_workspace();
		if (editor_data != nullptr && workspace != nullptr &&
				EditorAutomationWorkspace::dock_scene_tab(editor_data, workspace, source_tile_id, source_tab, target_tile_id, region, editor_node)) {
			EditorAutomationActionResult result = EditorAutomationActionResult::success(EditorAutomationActionRouteNames::SEMANTIC_DOCK, p_source_element.id);
			result.details["target_tile_id"] = target_tile_id;
			result.details["region"] = EditorAutomationWorkspace::drop_region_name(region);
			result.details["source_tile_id"] = source_tile_id;
			result.details["source_tab"] = source_tab;
			return result;
		}
	}

	if (p_route_preference == EditorAutomationRoutePreference::SEMANTIC) {
		return EditorAutomationActionResult::failure("unsupported_route", "dock requires synthesized input events for non-tab sources.");
	}

	EditorSceneWorkspace *workspace = EditorNode::get_scene_workspace();
	if (workspace == nullptr) {
		return _drag_failure("unsupported_route", "No workspace is available for dock input.", p_snapshot, &p_source_element, nullptr);
	}
	ScenePaneTile *target_tile = workspace->get_tile_by_id(target_tile_id);
	ERR_FAIL_NULL_V(target_tile, _drag_failure("invalid_target", "Target tile was not found.", p_snapshot, &p_source_element, nullptr));

	Node *source_node = nullptr;
	const EditorAutomationActionResult prepare_result = _prepare_element_for_input(p_snapshot, p_source_element, source_node);
	if (!prepare_result.ok) {
		return _drag_failure(prepare_result.kind, prepare_result.message, p_snapshot, &p_source_element, nullptr);
	}
	Control *source_control = Object::cast_to<Control>(source_node);
	ERR_FAIL_NULL_V(source_control, _drag_failure("invalid_element", "Dock source is not a control.", p_snapshot, &p_source_element, nullptr));

	const Vector2 source_position = EditorAutomationInput::resolve_position_in_bounds(
			p_source_element.bounds, EditorAutomationInput::position_options_for_source(p_options));
	const Vector2 target_position = EditorAutomationWorkspace::global_drop_point(target_tile, region);
	Viewport *viewport = _viewport_for_node(source_control);
	ERR_FAIL_NULL_V(viewport, _drag_failure("unsupported_route", "No viewport is available for dock input.", p_snapshot, &p_source_element, nullptr));

	const EditorAutomationInputModifiers modifiers = EditorAutomationInput::parse_modifiers(p_options.get("modifiers", Variant()));
	const MouseButton button = EditorAutomationInput::parse_mouse_button(String(p_options.get("button", Variant())));
	const Vector<Vector2> waypoints = _read_waypoints(p_options);

	PackedStringArray events;
	if (!EditorAutomationInput::push_mouse_drag(viewport, source_position, target_position, waypoints, button, modifiers, events)) {
		return _drag_failure("unsupported_route", "Dock input could not be dispatched.", p_snapshot, &p_source_element, nullptr);
	}

	EditorAutomationActionResult result = EditorAutomationActionResult::success(EditorAutomationActionRouteNames::INPUT_DOCK, p_source_element.id);
	result.events = events;
	result.focus = _focused_element_id(p_snapshot);
	result.details["target_tile_id"] = target_tile_id;
	result.details["region"] = EditorAutomationWorkspace::drop_region_name(region);
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

String _element_summary_from_snapshot(const EditorAutomationSnapshot &p_snapshot, const EditorAutomationElement &p_element) {
	return vformat("%s:%s (%s)", p_element.role, p_element.name, p_element.id);
}

void _record_action_trace(
		const String &p_action,
		const Dictionary &p_target,
		const EditorAutomationSnapshot &p_snapshot,
		const EditorAutomationElement *p_element,
		const EditorAutomationActionResult &p_result,
		const EditorAutomationLogMarker &p_log_marker) {
	String summary;
	String element_id;
	if (p_element != nullptr) {
		element_id = p_element->id;
		summary = _element_summary_from_snapshot(p_snapshot, *p_element);
	}
	const String result_kind = p_result.ok ? String("success") : p_result.kind;
	EditorAutomationTrace::get_singleton().record_action(
			p_action,
			p_target,
			element_id,
			summary,
			p_result.route,
			result_kind,
			p_result.ok,
			p_log_marker.message_index,
			p_log_marker.message_index);
}

} // namespace

EditorAutomationActionResult EditorAutomationDriver::perform(
		const EditorAutomationSnapshot &p_snapshot,
		const String &p_action,
		const Dictionary &p_target,
		const Dictionary &p_options) {
	EditorAutomationTrace::get_singleton().begin_action();
	const EditorAutomationLogMarker log_marker = EditorAutomationLog::create_marker();

	const EditorAutomationActionKind action_kind = editor_automation_action_kind_from_string(p_action);
	if (action_kind == EditorAutomationActionKind::UNKNOWN) {
		EditorAutomationActionResult result = EditorAutomationActionResult::failure("unsupported_action", vformat("Unsupported action '%s'.", p_action));
		_record_action_trace(p_action, p_target, p_snapshot, nullptr, result, log_marker);
		EditorAutomationTrace::get_singleton().end_action();
		return result;
	}

	const EditorAutomationRoutePreference route_preference = editor_automation_route_preference_from_string(_read_string_option(p_options, "route"));

	if (action_kind == EditorAutomationActionKind::PRESS_KEY) {
		const String key_name = _read_string_option(p_options, "key");
		if (key_name.is_empty()) {
			EditorAutomationActionResult result = EditorAutomationActionResult::failure("invalid_parameter", "press_key requires a `key` option.");
			_record_action_trace(p_action, p_target, p_snapshot, nullptr, result, log_marker);
			EditorAutomationTrace::get_singleton().end_action();
			return result;
		}
		EditorAutomationActionResult result = _action_press_key(p_snapshot, key_name, p_options, route_preference);
		_record_action_trace(p_action, p_target, p_snapshot, nullptr, result, log_marker);
		EditorAutomationTrace::get_singleton().end_action();
		return result;
	}

	if (action_kind == EditorAutomationActionKind::DRAG) {
		const EditorAutomationSelectorResult selector_result = _resolve_target(p_snapshot, p_target);
		if (selector_result.status != EditorAutomationSelectorStatus::OK) {
			EditorAutomationActionResult result = _selector_failure(selector_result);
			_record_action_trace(p_action, p_target, p_snapshot, nullptr, result, log_marker);
			EditorAutomationTrace::get_singleton().end_action();
			return result;
		}
		ERR_FAIL_COND_V(selector_result.match_indices.size() != 1, EditorAutomationActionResult::failure("ambiguous_selector", "Selector matched multiple elements."));
		const EditorAutomationElement &source_element = p_snapshot.get_element(selector_result.match_indices[0]);
		EditorAutomationActionResult result = _action_drag(p_snapshot, source_element, p_options, route_preference);
		if (result.ok) {
			_enrich_action_result(result, p_snapshot, selector_result, source_element);
		}
		_record_action_trace(p_action, p_target, p_snapshot, &source_element, result, log_marker);
		EditorAutomationTrace::get_singleton().end_action();
		return result;
	}

	if (action_kind == EditorAutomationActionKind::DOCK) {
		const EditorAutomationSelectorResult selector_result = _resolve_target(p_snapshot, p_target);
		if (selector_result.status != EditorAutomationSelectorStatus::OK) {
			EditorAutomationActionResult result = _selector_failure(selector_result);
			_record_action_trace(p_action, p_target, p_snapshot, nullptr, result, log_marker);
			EditorAutomationTrace::get_singleton().end_action();
			return result;
		}
		ERR_FAIL_COND_V(selector_result.match_indices.size() != 1, EditorAutomationActionResult::failure("ambiguous_selector", "Selector matched multiple elements."));
		const EditorAutomationElement &source_element = p_snapshot.get_element(selector_result.match_indices[0]);
		EditorAutomationActionResult result = _action_dock(p_snapshot, source_element, p_options, route_preference);
		if (result.ok) {
			_enrich_action_result(result, p_snapshot, selector_result, source_element);
		}
		_record_action_trace(p_action, p_target, p_snapshot, &source_element, result, log_marker);
		EditorAutomationTrace::get_singleton().end_action();
		return result;
	}

	const EditorAutomationSelectorResult selector_result = _resolve_target(p_snapshot, p_target);
	if (selector_result.status != EditorAutomationSelectorStatus::OK) {
		EditorAutomationActionResult result = _selector_failure(selector_result);
		_record_action_trace(p_action, p_target, p_snapshot, nullptr, result, log_marker);
		EditorAutomationTrace::get_singleton().end_action();
		return result;
	}
	ERR_FAIL_COND_V(selector_result.match_indices.size() != 1, EditorAutomationActionResult::failure("ambiguous_selector", "Selector matched multiple elements."));
	const EditorAutomationElement &element = p_snapshot.get_element(selector_result.match_indices[0]);

	EditorAutomationActionResult result;
	switch (action_kind) {
		case EditorAutomationActionKind::FOCUS:
			result = _action_focus(p_snapshot, element, route_preference);
			break;
		case EditorAutomationActionKind::CLICK:
			result = _action_click(p_snapshot, element, p_options, route_preference);
			break;
		case EditorAutomationActionKind::SET_TEXT: {
			if (!p_options.has("text")) {
				result = EditorAutomationActionResult::failure("invalid_parameter", "set_text requires a `text` option.");
				break;
			}
			const String text = _read_string_option(p_options, "text");
			result = _action_set_text(p_snapshot, element, text, route_preference);
			break;
		}
		case EditorAutomationActionKind::TYPE_TEXT: {
			const String text = _read_string_option(p_options, "text");
			if (text.is_empty()) {
				result = EditorAutomationActionResult::failure("invalid_parameter", "type_text requires a `text` option.");
				break;
			}
			result = _action_type_text(p_snapshot, element, text, p_options, route_preference);
			break;
		}
		case EditorAutomationActionKind::SELECT:
			result = _action_select(p_snapshot, element, _read_variant_option(p_options, "value"));
			break;
		case EditorAutomationActionKind::ACTIVATE:
			result = _action_activate(p_snapshot, element, route_preference);
			break;
		case EditorAutomationActionKind::SUBMIT:
			result = _action_submit(p_snapshot, element, route_preference);
			break;
		case EditorAutomationActionKind::SCROLL:
			result = _action_scroll(p_snapshot, element, p_options, route_preference);
			break;
		case EditorAutomationActionKind::EXPAND:
			if (element.role == "inspector_section") {
				result = _action_inspector_section_state(p_snapshot, element, true);
			} else {
				result = _action_tree_item_state(p_snapshot, element, true);
			}
			break;
		case EditorAutomationActionKind::COLLAPSE:
			if (element.role == "inspector_section") {
				result = _action_inspector_section_state(p_snapshot, element, false);
			} else {
				result = _action_tree_item_state(p_snapshot, element, false);
			}
			break;
		case EditorAutomationActionKind::OPEN_CONTEXT_MENU:
			result = _action_open_context_menu(p_snapshot, element, p_options, route_preference);
			break;
		case EditorAutomationActionKind::CHOOSE_MENU_ITEM: {
			String kind;
			String key;
			if (!_parse_virtual_key(element, kind, key) || kind != "menu_item") {
				result = EditorAutomationActionResult::failure("unsupported_action", "choose_menu_item requires a menu item element.");
				break;
			}
			result = _action_select_virtual(p_snapshot, element, kind, key);
			break;
		}
		case EditorAutomationActionKind::SET_VALUE:
			result = _action_set_value(p_snapshot, element, _read_variant_option(p_options, "value"), route_preference);
			break;
		case EditorAutomationActionKind::INCREMENT:
			result = _action_adjust_value(p_snapshot, element, true, route_preference);
			break;
		case EditorAutomationActionKind::DECREMENT:
			result = _action_adjust_value(p_snapshot, element, false, route_preference);
			break;
		default:
			result = EditorAutomationActionResult::failure("unsupported_action", vformat("Unsupported action '%s'.", p_action));
			break;
	}

	if (result.ok) {
		_enrich_action_result(result, p_snapshot, selector_result, element);
	}

	_record_action_trace(p_action, p_target, p_snapshot, &element, result, log_marker);
	EditorAutomationTrace::get_singleton().end_action();
	return result;
}
