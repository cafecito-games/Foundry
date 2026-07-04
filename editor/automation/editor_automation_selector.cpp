/**************************************************************************/
/*  editor_automation_selector.cpp                                        */
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

#include "editor_automation_selector.h"

#include "core/object/object.h"
#include "core/variant/variant.h"
#include "scene/main/node.h"

namespace {

bool _selector_has_key(const Dictionary &p_selector, const char *p_key) {
	if (!p_selector.has(p_key)) {
		return false;
	}
	return p_selector.get(p_key, Variant()).get_type() != Variant::NIL;
}

bool _read_optional_bool(const Dictionary &p_selector, const char *p_key, bool &r_value) {
	if (!_selector_has_key(p_selector, p_key)) {
		return false;
	}
	const Variant value = p_selector.get(p_key, Variant());
	if (value.get_type() != Variant::BOOL) {
		return false;
	}
	r_value = value;
	return true;
}

bool _element_matches_selector(const EditorAutomationElement &p_element, const Dictionary &p_selector) {
	if (_selector_has_key(p_selector, "handle")) {
		if (p_element.handle != String(p_selector.get("handle", Variant()))) {
			return false;
		}
	}

	if (_selector_has_key(p_selector, "id")) {
		if (p_element.id != String(p_selector.get("id", Variant()))) {
			return false;
		}
	}

	if (_selector_has_key(p_selector, "role")) {
		if (p_element.role != String(p_selector.get("role", Variant()))) {
			return false;
		}
	}

	if (_selector_has_key(p_selector, "name")) {
		if (p_element.name != String(p_selector.get("name", Variant()))) {
			return false;
		}
	}

	if (_selector_has_key(p_selector, "text")) {
		if (p_element.text != String(p_selector.get("text", Variant()))) {
			return false;
		}
	}

	if (_selector_has_key(p_selector, "class")) {
		if (p_element.class_name != String(p_selector.get("class", Variant()))) {
			return false;
		}
	}

	if (_selector_has_key(p_selector, "path")) {
		if (p_element.path != String(p_selector.get("path", Variant()))) {
			return false;
		}
	}

	bool bool_value = false;
	if (_read_optional_bool(p_selector, "visible", bool_value) && p_element.visible != bool_value) {
		return false;
	}
	if (_read_optional_bool(p_selector, "enabled", bool_value) && p_element.enabled != bool_value) {
		return false;
	}
	if (_read_optional_bool(p_selector, "focused", bool_value) && p_element.focused != bool_value) {
		return false;
	}

	return true;
}

void _collect_descendants(const Vector<EditorAutomationElement> &p_elements, int p_parent_index, LocalVector<int> &r_descendants) {
	for (int child_index : p_elements[p_parent_index].children) {
		r_descendants.push_back(child_index);
		_collect_descendants(p_elements, child_index, r_descendants);
	}
}

Dictionary _candidate_from_element(const EditorAutomationElement &p_element) {
	Dictionary candidate;
	candidate["id"] = p_element.id;
	candidate["role"] = p_element.role;
	candidate["name"] = p_element.name;
	candidate["text"] = p_element.text;
	candidate["class"] = p_element.class_name;
	candidate["path"] = p_element.path;
	return candidate;
}

EditorAutomationSelectorResult _make_result(EditorAutomationSelectorStatus p_status, const String &p_kind = String(), const String &p_message = String()) {
	EditorAutomationSelectorResult result;
	result.status = p_status;
	result.error_kind = p_kind;
	result.message = p_message;
	return result;
}

EditorAutomationSelectorResult _resolve_internal(const EditorAutomationSnapshot &p_snapshot, const Dictionary &p_selector, bool p_allow_within) {
	const EditorAutomationSnapshotData &data = p_snapshot.get_data();
	LocalVector<int> search_indices;

	if (p_allow_within && _selector_has_key(p_selector, "within")) {
		const Dictionary within_selector = p_selector.get("within", Dictionary());
		const EditorAutomationSelectorResult within_result = _resolve_internal(p_snapshot, within_selector, false);
		if (within_result.status != EditorAutomationSelectorStatus::OK) {
			EditorAutomationSelectorResult result = within_result;
			result.error_kind = "within_" + result.error_kind;
			return result;
		}
		ERR_FAIL_COND_V(within_result.match_indices.size() != 1, _make_result(EditorAutomationSelectorStatus::INVALID_SELECTOR, "invalid_within", "The `within` selector must resolve to exactly one container."));
		_collect_descendants(data.elements, within_result.match_indices[0], search_indices);
	} else {
		for (int i = 0; i < data.elements.size(); i++) {
			search_indices.push_back(i);
		}
	}

	Dictionary selector_without_within = p_selector;
	if (selector_without_within.has("within")) {
		selector_without_within.erase("within");
	}

	Vector<int> matches;
	for (int element_index : search_indices) {
		if (_element_matches_selector(data.elements[element_index], selector_without_within)) {
			matches.push_back(element_index);
		}
	}

	if (matches.is_empty()) {
		return _make_result(EditorAutomationSelectorStatus::NO_MATCH, "no_match", "No elements matched the selector.");
	}
	if (matches.size() > 1) {
		EditorAutomationSelectorResult result = _make_result(EditorAutomationSelectorStatus::AMBIGUOUS, "ambiguous_selector", "Multiple elements matched the selector.");
		for (int match_index : matches) {
			result.candidates.push_back(_candidate_from_element(data.elements[match_index]));
		}
		result.match_indices = matches;
		return result;
	}

	EditorAutomationSelectorResult result;
	result.status = EditorAutomationSelectorStatus::OK;
	result.match_indices = matches;
	return result;
}

EditorAutomationSelectorResult _make_ok_result(const EditorAutomationSnapshot &p_snapshot, int p_index, const String &p_requested_reference = String(), bool p_reconciled = false) {
	EditorAutomationSelectorResult result;
	result.status = EditorAutomationSelectorStatus::OK;
	result.match_indices.push_back(p_index);
	result.snapshot_generation = p_snapshot.get_generation();
	const EditorAutomationElement &element = p_snapshot.get_element(p_index);
	result.current_element_id = element.id;
	if (p_reconciled) {
		result.reconciled = true;
		result.requested_reference = p_requested_reference;
	}
	return result;
}

EditorAutomationSelectorResult _make_stale_result(
		const EditorAutomationSnapshot &p_snapshot,
		const String &p_error_kind,
		const String &p_message,
		const String &p_requested_reference,
		const Dictionary &p_diagnostic = Dictionary()) {
	EditorAutomationSelectorResult result = _make_result(EditorAutomationSelectorStatus::STALE_ID, p_error_kind, p_message);
	result.snapshot_generation = p_snapshot.get_generation();
	result.requested_reference = p_requested_reference;
	Dictionary diagnostic = p_diagnostic;
	if (diagnostic.is_empty()) {
		diagnostic["requested_reference"] = p_requested_reference;
		diagnostic["current_generation"] = p_snapshot.get_generation();
	}
	result.candidates.push_back(diagnostic);
	return result;
}

Node *_resolve_node_from_object_id(uint64_t p_object_id) {
	if (p_object_id == 0) {
		return nullptr;
	}
	return Object::cast_to<Node>(ObjectDB::get_instance(ObjectID(p_object_id)));
}

uint64_t _parent_object_id_from_virtual_key(const String &p_key) {
	const int colon_pos = p_key.find_char(':');
	if (colon_pos < 0) {
		return 0;
	}
	return static_cast<uint64_t>(p_key.substr(0, colon_pos).to_int());
}

EditorAutomationSelectorResult _reconcile_by_kind_and_key(
		const EditorAutomationSnapshot &p_snapshot,
		const String &p_kind,
		const String &p_key,
		const String &p_requested_reference) {
	if (p_kind == "object") {
		const uint64_t object_id = static_cast<uint64_t>(p_key.to_int());
		Node *node = _resolve_node_from_object_id(object_id);
		if (node == nullptr) {
			Dictionary diagnostic;
			diagnostic["object_id"] = String::num_uint64(object_id);
			return _make_stale_result(
					p_snapshot,
					"freed_object",
					"The element refers to a freed object that is no longer part of the editor UI.",
					p_requested_reference,
					diagnostic);
		}

		const EditorAutomationElement *element = p_snapshot.find_by_object_id(object_id);
		if (element == nullptr) {
			Dictionary diagnostic;
			diagnostic["object_id"] = String::num_uint64(object_id);
			if (node->is_inside_tree()) {
				diagnostic["node_path"] = String(node->get_path());
			}
			diagnostic["node_class"] = node->get_class();
			return _make_stale_result(
					p_snapshot,
					"element_not_visible",
					"The object still exists but is not visible in the current UI snapshot.",
					p_requested_reference,
					diagnostic);
		}

		const int *index = p_snapshot.get_data().object_id_to_index.getptr(object_id);
		ERR_FAIL_NULL_V(index, _make_result(EditorAutomationSelectorStatus::STALE_ID, "invalid_snapshot_handle", "The reconciled object id is not indexed in the current snapshot."));
		return _make_ok_result(p_snapshot, *index, p_requested_reference, true);
	}

	if (!EditorAutomationSnapshot::is_virtual_durable_kind(p_kind)) {
		return _make_result(EditorAutomationSelectorStatus::INVALID_SELECTOR, "invalid_id", "The element reference is malformed.");
	}

	const EditorAutomationElement *element = p_snapshot.find_by_durable_key(p_kind, p_key);
	if (element != nullptr) {
		const int *index = p_snapshot.get_data().handle_to_index.getptr(element->handle);
		ERR_FAIL_NULL_V(index, _make_result(EditorAutomationSelectorStatus::STALE_ID, "invalid_snapshot_handle", "The reconciled virtual element is not indexed in the current snapshot."));
		return _make_ok_result(p_snapshot, *index, p_requested_reference, true);
	}

	const uint64_t parent_object_id = _parent_object_id_from_virtual_key(p_key);
	Node *parent_node = _resolve_node_from_object_id(parent_object_id);
	if (parent_node == nullptr) {
		Dictionary diagnostic;
		diagnostic["parent_object_id"] = String::num_uint64(parent_object_id);
		diagnostic["virtual_kind"] = p_kind;
		diagnostic["virtual_key"] = p_key;
		return _make_stale_result(
				p_snapshot,
				"freed_object",
				"The virtual element's parent container was freed.",
				p_requested_reference,
				diagnostic);
	}

	Dictionary diagnostic;
	diagnostic["virtual_kind"] = p_kind;
	diagnostic["virtual_key"] = p_key;
	diagnostic["parent_object_id"] = String::num_uint64(parent_object_id);
	diagnostic["durable_key_strategy"] = "Virtual elements reconcile by parent object id plus index or tree path encoded in the durable key.";
	return _make_stale_result(
			p_snapshot,
			"virtual_element_unavailable",
			"The virtual element could not be reconciled in the current UI snapshot. Its parent container is still alive, but the item index or tree path no longer matches.",
			p_requested_reference,
			diagnostic);
}

} // namespace

EditorAutomationSelectorResult EditorAutomationSelector::resolve_by_id(const EditorAutomationSnapshot &p_snapshot, const String &p_id) {
	uint64_t generation = 0;
	String kind;
	String key;
	if (!EditorAutomationSnapshot::parse_element_id(p_id, generation, kind, key)) {
		if (!EditorAutomationSnapshot::parse_durable_handle(p_id, kind, key)) {
			return _make_result(EditorAutomationSelectorStatus::INVALID_SELECTOR, "invalid_id", "The element id is malformed.");
		}
		return _reconcile_by_kind_and_key(p_snapshot, kind, key, p_id);
	}

	if (generation == p_snapshot.get_generation()) {
		const EditorAutomationElement *element = p_snapshot.find_by_id(p_id);
		if (element == nullptr) {
			return _make_stale_result(
					p_snapshot,
					"invalid_snapshot_handle",
					"The element id is no longer valid for the current UI snapshot.",
					p_id);
		}

		const int *index = p_snapshot.get_data().id_to_index.getptr(p_id);
		ERR_FAIL_NULL_V(index, _make_result(EditorAutomationSelectorStatus::STALE_ID, "invalid_snapshot_handle", "The element id is no longer valid for the current UI snapshot."));
		return _make_ok_result(p_snapshot, *index);
	}

	return _reconcile_by_kind_and_key(p_snapshot, kind, key, p_id);
}

EditorAutomationSelectorResult EditorAutomationSelector::resolve_by_handle(const EditorAutomationSnapshot &p_snapshot, const String &p_handle) {
	String kind;
	String key;
	if (!EditorAutomationSnapshot::parse_durable_handle(p_handle, kind, key)) {
		return _make_result(EditorAutomationSelectorStatus::INVALID_SELECTOR, "invalid_handle", "The durable element handle is malformed.");
	}

	const EditorAutomationElement *element = p_snapshot.find_by_handle(EditorAutomationSnapshot::make_durable_handle(kind, key));
	if (element != nullptr) {
		const int *index = p_snapshot.get_data().handle_to_index.getptr(element->handle);
		ERR_FAIL_NULL_V(index, _make_result(EditorAutomationSelectorStatus::STALE_ID, "invalid_snapshot_handle", "The durable handle is not indexed in the current snapshot."));
		return _make_ok_result(p_snapshot, *index);
	}

	return _reconcile_by_kind_and_key(p_snapshot, kind, key, p_handle);
}

EditorAutomationSelectorResult EditorAutomationSelector::resolve(const EditorAutomationSnapshot &p_snapshot, const Dictionary &p_selector) {
	if (_selector_has_key(p_selector, "handle")) {
		return resolve_by_handle(p_snapshot, p_selector.get("handle", Variant()));
	}
	if (_selector_has_key(p_selector, "id")) {
		return resolve_by_id(p_snapshot, p_selector.get("id", Variant()));
	}
	return _resolve_internal(p_snapshot, p_selector, true);
}
