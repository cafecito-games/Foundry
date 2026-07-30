/**************************************************************************/
/*  editor_automation_selector.cpp                                        */
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

#include "editor_automation_selector.h"

#include "core/object/object.h"
#include "core/variant/variant.h"
#include "editor/automation/editor_automation_workspace.h"
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

bool _read_optional_int(const Dictionary &p_selector, const char *p_key, int64_t &r_value) {
	if (!_selector_has_key(p_selector, p_key)) {
		return false;
	}
	const Variant value = p_selector.get(p_key, Variant());
	if (value.get_type() != Variant::INT && value.get_type() != Variant::FLOAT) {
		return false;
	}
	r_value = value;
	return true;
}

// Reads the optional `nth`/`index` disambiguator. `nth` and `index` are
// synonyms; when both are present they must agree.
bool _read_disambiguator(const Dictionary &p_selector, int64_t &r_value, bool &r_valid) {
	r_valid = true;
	int64_t nth = 0;
	int64_t index = 0;
	const bool has_nth = _read_optional_int(p_selector, "nth", nth);
	const bool has_index = _read_optional_int(p_selector, "index", index);
	if (!has_nth && !has_index) {
		return false;
	}
	if (has_nth && has_index && nth != index) {
		r_valid = false;
		return true;
	}
	r_value = has_nth ? nth : index;
	return true;
}

bool _string_equals(const String &p_value, const String &p_expected, bool p_case_sensitive) {
	if (p_case_sensitive) {
		return p_value == p_expected;
	}
	return p_value.nocasecmp_to(p_expected) == 0;
}

bool _string_contains(const String &p_value, const String &p_needle, bool p_case_sensitive) {
	if (p_case_sensitive) {
		return p_value.contains(p_needle);
	}
	return p_value.containsn(p_needle);
}

// Matches an exact field (`p_exact_key`) and/or a substring field
// (`p_contains_key`) against `p_value`, honoring case sensitivity.
bool _match_text_field(const Dictionary &p_selector, const char *p_exact_key, const char *p_contains_key, const String &p_value, bool p_case_sensitive) {
	if (_selector_has_key(p_selector, p_exact_key)) {
		if (!_string_equals(p_value, String(p_selector.get(p_exact_key, Variant())), p_case_sensitive)) {
			return false;
		}
	}
	if (p_contains_key != nullptr && _selector_has_key(p_selector, p_contains_key)) {
		if (!_string_contains(p_value, String(p_selector.get(p_contains_key, Variant())), p_case_sensitive)) {
			return false;
		}
	}
	return true;
}

bool _variant_metadata_equals(const Variant &p_actual, const Variant &p_expected) {
	if (p_expected.get_type() == Variant::NODE_PATH && p_actual.get_type() == Variant::NODE_PATH) {
		return NodePath(p_expected) == NodePath(p_actual);
	}
	return p_actual == p_expected;
}

bool _metadata_matches(const Dictionary &p_element_metadata, const Dictionary &p_selector_metadata) {
	if (p_selector_metadata.is_empty()) {
		return true;
	}
	const Array keys = p_selector_metadata.keys();
	for (int i = 0; i < keys.size(); i++) {
		const String key = keys[i];
		if (!p_element_metadata.has(key)) {
			return false;
		}
		if (!_variant_metadata_equals(p_element_metadata.get(key, Variant()), p_selector_metadata.get(key, Variant()))) {
			return false;
		}
	}
	return true;
}

bool _element_matches_selector(const EditorAutomationElement &p_element, const Dictionary &p_selector, bool p_case_sensitive) {
	// Opaque references are always matched case-sensitively; they are not
	// human-authored labels.
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

	if (!_match_text_field(p_selector, "role", "role_contains", p_element.role, p_case_sensitive)) {
		return false;
	}
	if (!_match_text_field(p_selector, "name", "name_contains", p_element.name, p_case_sensitive)) {
		return false;
	}
	if (!_match_text_field(p_selector, "text", "text_contains", p_element.text, p_case_sensitive)) {
		return false;
	}
	if (!_match_text_field(p_selector, "class", "class_contains", p_element.class_name, p_case_sensitive)) {
		return false;
	}
	if (!_match_text_field(p_selector, "path", "path_contains", p_element.path, p_case_sensitive)) {
		return false;
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

	// Agent-friendly filters. Unlike the exact-state selectors above, these
	// only restrict when set to true and are ignored when false.
	if (_read_optional_bool(p_selector, "visible_only", bool_value) && bool_value && !p_element.visible) {
		return false;
	}
	if (_read_optional_bool(p_selector, "enabled_only", bool_value) && bool_value && !p_element.enabled) {
		return false;
	}
	if (_read_optional_bool(p_selector, "selected", bool_value) && p_element.selected != bool_value) {
		return false;
	}

	if (_selector_has_key(p_selector, "metadata")) {
		const Variant metadata_value = p_selector.get("metadata", Variant());
		if (metadata_value.get_type() != Variant::DICTIONARY) {
			return false;
		}
		if (!_metadata_matches(p_element.metadata, Dictionary(metadata_value))) {
			return false;
		}
	}

	return true;
}

void _collect_descendants(const Vector<EditorAutomationElement> &p_elements, int p_parent_index, LocalVector<int> &r_descendants) {
	for (int child_index : p_elements[p_parent_index].children) {
		r_descendants.push_back(child_index);
		_collect_descendants(p_elements, child_index, r_descendants);
	}
}

// Closeness of a substring match: 1.0 for an exact-length hit, approaching 0
// as the containing text grows relative to the needle. Used only to rank
// otherwise-equal candidates so agents can prefer the tightest match.
double _substring_ratio(const String &p_needle, const String &p_haystack) {
	if (p_haystack.is_empty()) {
		return p_needle.is_empty() ? 1.0 : 0.0;
	}
	const double ratio = (double)p_needle.length() / (double)p_haystack.length();
	return ratio > 1.0 ? 1.0 : ratio;
}

double _candidate_score(const EditorAutomationElement &p_element, const Dictionary &p_selector, bool p_case_sensitive, String &r_reason) {
	double score = 1.0;
	Vector<String> reasons;
	if (_selector_has_key(p_selector, "role")) {
		reasons.push_back("role");
	}
	if (_selector_has_key(p_selector, "name")) {
		reasons.push_back("name");
	}
	if (_selector_has_key(p_selector, "text")) {
		reasons.push_back("text");
	}
	if (_selector_has_key(p_selector, "class")) {
		reasons.push_back("class");
	}
	if (_selector_has_key(p_selector, "path")) {
		reasons.push_back("path");
	}
	if (_selector_has_key(p_selector, "name_contains")) {
		score *= _substring_ratio(String(p_selector.get("name_contains", Variant())), p_element.name);
		reasons.push_back("name_contains");
	}
	if (_selector_has_key(p_selector, "text_contains")) {
		score *= _substring_ratio(String(p_selector.get("text_contains", Variant())), p_element.text);
		reasons.push_back("text_contains");
	}
	if (_selector_has_key(p_selector, "class_contains")) {
		score *= _substring_ratio(String(p_selector.get("class_contains", Variant())), p_element.class_name);
		reasons.push_back("class_contains");
	}
	if (_selector_has_key(p_selector, "path_contains")) {
		score *= _substring_ratio(String(p_selector.get("path_contains", Variant())), p_element.path);
		reasons.push_back("path_contains");
	}
	if (_selector_has_key(p_selector, "role_contains")) {
		reasons.push_back("role_contains");
	}
	if (reasons.is_empty()) {
		r_reason = "matched all elements (no field filters)";
	} else {
		r_reason = "matched " + String(", ").join(reasons);
	}
	return score;
}

Dictionary _candidate_from_element(const EditorAutomationElement &p_element, int p_nth, double p_score, const String &p_reason) {
	Dictionary candidate;
	candidate["id"] = p_element.id;
	candidate["handle"] = p_element.handle;
	candidate["role"] = p_element.role;
	candidate["name"] = p_element.name;
	candidate["text"] = p_element.text;
	candidate["class"] = p_element.class_name;
	candidate["path"] = p_element.path;
	candidate["visible"] = p_element.visible;
	candidate["enabled"] = p_element.enabled;
	candidate["focused"] = p_element.focused;
	// Ordinal within the ranked candidate list (stable snapshot order). Pass
	// this back as `nth`/`index` in the selector to pick this candidate.
	candidate["nth"] = p_nth;
	candidate["index"] = p_nth;
	candidate["score"] = p_score;
	candidate["reason"] = p_reason;
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
	if (EditorAutomationWorkspace::selector_is_tile_container(p_selector)) {
		Dictionary tile_only = p_selector;
		if (tile_only.has("within")) {
			return _make_result(EditorAutomationSelectorStatus::INVALID_SELECTOR, "invalid_tile_selector", "Tile container selectors cannot be combined with `within`.");
		}
		bool has_non_tile_field = false;
		const char *non_tile_keys[] = { "role", "name", "text", "class", "path", "handle", "id", "role_contains", "name_contains", "text_contains", "class_contains", "path_contains" };
		for (const char *key : non_tile_keys) {
			if (_selector_has_key(tile_only, key)) {
				has_non_tile_field = true;
				break;
			}
		}
		if (!has_non_tile_field) {
			return EditorAutomationWorkspace::resolve_tile_container(p_snapshot, tile_only);
		}
	}

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

	bool case_sensitive = true;
	_read_optional_bool(selector_without_within, "case_sensitive", case_sensitive);

	int64_t disambiguator = 0;
	bool disambiguator_valid = true;
	const bool has_disambiguator = _read_disambiguator(selector_without_within, disambiguator, disambiguator_valid);
	if (has_disambiguator && !disambiguator_valid) {
		return _make_result(EditorAutomationSelectorStatus::INVALID_SELECTOR, "invalid_disambiguator", "The `nth` and `index` disambiguators disagree.");
	}

	Vector<int> matches;
	for (int element_index : search_indices) {
		if (_element_matches_selector(data.elements[element_index], selector_without_within, case_sensitive)) {
			matches.push_back(element_index);
		}
	}

	if (matches.is_empty()) {
		return _make_result(EditorAutomationSelectorStatus::NO_MATCH, "no_match", "No elements matched the selector.");
	}

	// Deterministic disambiguation is applied after all filters, over the
	// candidate list in stable snapshot order. Negative indices count from the
	// end (nth=-1 is the last match).
	if (has_disambiguator) {
		int64_t resolved_index = disambiguator;
		if (resolved_index < 0) {
			resolved_index += matches.size();
		}
		if (resolved_index < 0 || resolved_index >= matches.size()) {
			EditorAutomationSelectorResult result = _make_result(EditorAutomationSelectorStatus::NO_MATCH, "index_out_of_range", vformat("The disambiguator %d is out of range for %d matches.", (int)disambiguator, matches.size()));
			for (int i = 0; i < matches.size(); i++) {
				String reason;
				const double score = _candidate_score(data.elements[matches[i]], selector_without_within, case_sensitive, reason);
				result.candidates.push_back(_candidate_from_element(data.elements[matches[i]], i, score, reason));
			}
			result.match_indices = matches;
			return result;
		}
		EditorAutomationSelectorResult result;
		result.status = EditorAutomationSelectorStatus::OK;
		result.match_indices.push_back(matches[resolved_index]);
		return result;
	}

	if (matches.size() > 1) {
		EditorAutomationSelectorResult result = _make_result(EditorAutomationSelectorStatus::AMBIGUOUS, "ambiguous_selector", "Multiple elements matched the selector. Add `nth`/`index` to disambiguate.");
		for (int i = 0; i < matches.size(); i++) {
			String reason;
			const double score = _candidate_score(data.elements[matches[i]], selector_without_within, case_sensitive, reason);
			result.candidates.push_back(_candidate_from_element(data.elements[matches[i]], i, score, reason));
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
