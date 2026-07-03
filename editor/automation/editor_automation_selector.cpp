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

#include "core/variant/variant.h"

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

} // namespace

EditorAutomationSelectorResult EditorAutomationSelector::resolve_by_id(const EditorAutomationSnapshot &p_snapshot, const String &p_id) {
	uint64_t generation = 0;
	String kind;
	String key;
	if (!EditorAutomationSnapshot::parse_element_id(p_id, generation, kind, key)) {
		return _make_result(EditorAutomationSelectorStatus::INVALID_SELECTOR, "invalid_id", "The element id is malformed.");
	}

	if (generation != p_snapshot.get_generation()) {
		EditorAutomationSelectorResult result = _make_result(
				EditorAutomationSelectorStatus::STALE_ID,
				"stale_snapshot_id",
				"The element id belongs to a previous UI snapshot. Capture a fresh snapshot and resolve the element again.");
		Dictionary diagnostic;
		diagnostic["requested_id"] = p_id;
		diagnostic["current_generation"] = p_snapshot.get_generation();
		diagnostic["requested_generation"] = generation;
		result.candidates.push_back(diagnostic);
		return result;
	}

	const EditorAutomationElement *element = p_snapshot.find_by_id(p_id);
	if (element == nullptr) {
		EditorAutomationSelectorResult result = _make_result(
				EditorAutomationSelectorStatus::STALE_ID,
				"invalid_snapshot_handle",
				"The element id is no longer valid for the current UI snapshot. Capture a fresh snapshot and resolve the element again.");
		Dictionary diagnostic;
		diagnostic["requested_id"] = p_id;
		diagnostic["current_generation"] = p_snapshot.get_generation();
		result.candidates.push_back(diagnostic);
		return result;
	}

	EditorAutomationSelectorResult result;
	result.status = EditorAutomationSelectorStatus::OK;
	const int *index = p_snapshot.get_data().id_to_index.getptr(p_id);
	ERR_FAIL_NULL_V(index, result);
	result.match_indices.push_back(*index);
	return result;
}

EditorAutomationSelectorResult EditorAutomationSelector::resolve(const EditorAutomationSnapshot &p_snapshot, const Dictionary &p_selector) {
	if (_selector_has_key(p_selector, "id")) {
		return resolve_by_id(p_snapshot, p_selector.get("id", Variant()));
	}
	return _resolve_internal(p_snapshot, p_selector, true);
}
