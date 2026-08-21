/**************************************************************************/
/*  fs_type_completeness_manifest.cpp                                     */
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

#include "fs_type_completeness_manifest.h"
#include "fs_type_completeness_json.h"

#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/io/json.h"
#include "core/math/math_funcs.h"
#include "core/variant/array.h"

#ifdef UNIX_ENABLED
#include <cstdlib>
#endif

#ifdef WINDOWS_ENABLED
#include <windows.h>
#endif

namespace FSTests {

static void append_error(Vector<String> &r_errors, const String &p_path, const String &p_reason) {
	r_errors.push_back(vformat("%s: %s", p_path, p_reason));
}

static bool is_json_path_identifier(const String &p_member) {
	if (p_member.is_empty()) {
		return false;
	}
	for (int i = 0; i < p_member.length(); i++) {
		const char32_t character = p_member[i];
		const bool valid = (character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z') ||
				character == '_' || (i > 0 && character >= '0' && character <= '9');
		if (!valid) {
			return false;
		}
	}
	return true;
}

static String append_json_path_member(const String &p_path, const String &p_member, bool p_force_quoted = false) {
	if (!p_force_quoted && is_json_path_identifier(p_member)) {
		return p_path + "." + p_member;
	}
	return vformat("%s[%s]", p_path, JSON::stringify(p_member));
}

static bool require_string(const Dictionary &p_object, const StringName &p_field, const String &p_path, String &r_value,
		Vector<String> &r_errors) {
	if (!p_object.has(p_field)) {
		append_error(r_errors, p_path, "required field is missing");
		return false;
	}
	const Variant value = p_object[p_field];
	if (value.get_type() != Variant::STRING) {
		append_error(r_errors, p_path, "expected a string");
		return false;
	}
	r_value = value;
	return true;
}

static bool require_dictionary(const Variant &p_value, const String &p_path, Dictionary &r_value, Vector<String> &r_errors) {
	if (p_value.get_type() != Variant::DICTIONARY) {
		append_error(r_errors, p_path, "expected an object");
		return false;
	}
	r_value = p_value;
	return true;
}

static bool require_dictionary(const Dictionary &p_object, const StringName &p_field, const String &p_path,
		Dictionary &r_value, Vector<String> &r_errors) {
	if (!p_object.has(p_field)) {
		append_error(r_errors, p_path, "required field is missing");
		return false;
	}
	return require_dictionary(p_object[p_field], p_path, r_value, r_errors);
}

static bool require_array(const Dictionary &p_object, const StringName &p_field, const String &p_path, Array &r_value,
		Vector<String> &r_errors) {
	if (!p_object.has(p_field)) {
		append_error(r_errors, p_path, "required field is missing");
		return false;
	}
	const Variant value = p_object[p_field];
	if (value.get_type() != Variant::ARRAY) {
		append_error(r_errors, p_path, "expected an array");
		return false;
	}
	r_value = value;
	return true;
}

static bool append_unique_id(const String &p_id, const String &p_path, HashSet<String> &r_ids, Vector<String> &r_errors) {
	if (p_id.is_empty()) {
		append_error(r_errors, p_path, "must be a non-empty string");
		return false;
	}
	if (r_ids.has(p_id)) {
		append_error(r_errors, p_path, vformat("duplicate id '%s'", p_id));
		return false;
	}
	r_ids.insert(p_id);
	return true;
}

static void validate_allowed_fields(const Dictionary &p_object, const Vector<String> &p_allowed_fields,
		const String &p_path, Vector<String> &r_errors);

static bool require_json_integer(const Variant &p_value, const String &p_path, int &r_value, Vector<String> &r_errors) {
	if (p_value.get_type() != Variant::FLOAT && p_value.get_type() != Variant::INT) {
		append_error(r_errors, p_path, "expected an integer");
		return false;
	}
	const double number = p_value;
	if (!Math::is_finite(number) || number != Math::floor(number) || number < INT32_MIN || number > INT32_MAX) {
		append_error(r_errors, p_path, "expected an integer");
		return false;
	}
	r_value = (int)number;
	return true;
}

static bool parse_string_array(const Array &p_array, const String &p_path, Vector<String> &r_values,
		Vector<String> &r_errors, bool p_require_non_empty) {
	if (p_require_non_empty && p_array.is_empty()) {
		append_error(r_errors, p_path, "must contain at least one string");
	}
	HashSet<String> seen;
	bool valid = !p_require_non_empty || !p_array.is_empty();
	for (int i = 0; i < p_array.size(); i++) {
		const String item_path = vformat("%s[%d]", p_path, i);
		const Variant value = p_array[i];
		if (value.get_type() != Variant::STRING) {
			append_error(r_errors, item_path, "expected a string");
			valid = false;
			continue;
		}
		const String string_value = value;
		if (string_value.is_empty()) {
			append_error(r_errors, item_path, "must be non-empty");
			valid = false;
			continue;
		}
		if (seen.has(string_value)) {
			append_error(r_errors, item_path, vformat("duplicate value '%s'", string_value));
			valid = false;
			continue;
		}
		seen.insert(string_value);
		r_values.push_back(string_value);
	}
	return valid;
}

static void parse_domain(const Dictionary &p_domain, FSCompletenessManifest &r_manifest, Vector<String> &r_errors) {
	if (p_domain.is_empty()) {
		append_error(r_errors, "$.domain", "must contain at least one axis");
		return;
	}
	for (const Variant &axis_variant : p_domain.keys()) {
		const String axis = axis_variant;
		const String axis_path = append_json_path_member("$.domain", axis, true);
		if (axis.is_empty()) {
			append_error(r_errors, "$.domain", "axis names must be non-empty");
			continue;
		}
		r_manifest.domain_axis_order.push_back(axis);
		const Variant values_variant = p_domain[axis_variant];
		if (values_variant.get_type() != Variant::ARRAY) {
			append_error(r_errors, axis_path, "expected an array");
			continue;
		}
		Vector<String> values;
		parse_string_array(values_variant, axis_path, values, r_errors, true);
		r_manifest.domain.insert(axis, values);
	}
}

static void parse_required_dimensions(const Array &p_records, FSCompletenessManifest &r_manifest,
		Vector<String> &r_errors) {
	Vector<FSCompletenessRequiredDimension> seen;
	for (int i = 0; i < p_records.size(); i++) {
		const String record_path = vformat("$.required_dimensions[%d]", i);
		Dictionary object;
		if (!require_dictionary(p_records[i], record_path, object, r_errors)) {
			continue;
		}
		validate_allowed_fields(object, Vector<String>({ "dimension", "when" }), record_path, r_errors);
		FSCompletenessRequiredDimension record;
		const bool has_dimension = require_string(object, SNAME("dimension"), record_path + ".dimension", record.dimension, r_errors);
		if (has_dimension && record.dimension.is_empty()) {
			append_error(r_errors, record_path + ".dimension", "must be non-empty");
		}
		require_dictionary(object, SNAME("when"), record_path + ".when", record.when, r_errors);

		bool duplicate = false;
		if (has_dimension && !record.dimension.is_empty()) {
			for (const FSCompletenessRequiredDimension &prior : seen) {
				if (prior.dimension == record.dimension && prior.when == record.when) {
					duplicate = true;
					break;
				}
			}
		}
		if (duplicate) {
			append_error(r_errors, record_path, "duplicates an earlier (dimension, when) record");
		} else {
			seen.push_back(record);
		}
		r_manifest.required_dimensions.push_back(record);
	}
}

static void parse_anchors(const Array &p_records, FSCompletenessManifest &r_manifest, HashSet<String> &r_ids,
		Vector<String> &r_errors) {
	for (int i = 0; i < p_records.size(); i++) {
		const String record_path = vformat("$.anchors[%d]", i);
		Dictionary object;
		if (!require_dictionary(p_records[i], record_path, object, r_errors)) {
			continue;
		}
		validate_allowed_fields(object, Vector<String>({ "id", "coordinates", "expect", "surfaces" }),
				record_path, r_errors);
		FSCompletenessAnchor record;
		if (require_string(object, SNAME("id"), record_path + ".id", record.id, r_errors)) {
			append_unique_id(record.id, record_path + ".id", r_ids, r_errors);
		}
		if (require_dictionary(object, SNAME("coordinates"), record_path + ".coordinates", record.coordinates, r_errors) &&
				record.coordinates.is_empty()) {
			append_error(r_errors, record_path + ".coordinates", "must be non-empty");
		}
		if (require_dictionary(object, SNAME("expect"), record_path + ".expect", record.expect, r_errors) && record.expect.is_empty()) {
			append_error(r_errors, record_path + ".expect", "must be non-empty");
		}
		if (object.has(SNAME("surfaces"))) {
			Array surfaces;
			if (require_array(object, SNAME("surfaces"), record_path + ".surfaces", surfaces, r_errors)) {
				parse_string_array(surfaces, record_path + ".surfaces", record.surfaces, r_errors, false);
			}
		}
		r_manifest.anchors.push_back(record);
	}
}

static void parse_relations(const Array &p_records, FSCompletenessManifest &r_manifest, HashSet<String> &r_ids,
		HashSet<String> &r_relation_ids, Vector<String> &r_errors) {
	for (int i = 0; i < p_records.size(); i++) {
		const String record_path = vformat("$.relations[%d]", i);
		Dictionary object;
		if (!require_dictionary(p_records[i], record_path, object, r_errors)) {
			continue;
		}
		validate_allowed_fields(object, Vector<String>({ "id", "from", "to", "derive" }), record_path, r_errors);
		FSCompletenessRelation record;
		if (require_string(object, SNAME("id"), record_path + ".id", record.id, r_errors)) {
			append_unique_id(record.id, record_path + ".id", r_ids, r_errors);
			if (!record.id.is_empty()) {
				r_relation_ids.insert(record.id);
			}
		}
		if (require_dictionary(object, SNAME("from"), record_path + ".from", record.from, r_errors) && record.from.is_empty()) {
			append_error(r_errors, record_path + ".from", "must be non-empty");
		}
		if (require_dictionary(object, SNAME("to"), record_path + ".to", record.to, r_errors) && record.to.is_empty()) {
			append_error(r_errors, record_path + ".to", "must be non-empty");
		}
		if (require_dictionary(object, SNAME("derive"), record_path + ".derive", record.derive, r_errors) && record.derive.is_empty()) {
			append_error(r_errors, record_path + ".derive", "must be non-empty");
		}
		r_manifest.relations.push_back(record);
	}
}

static void parse_exceptions(const Array &p_records, FSCompletenessManifest &r_manifest, HashSet<String> &r_ids,
		const HashSet<String> &p_relation_ids, Vector<String> &r_errors) {
	for (int i = 0; i < p_records.size(); i++) {
		const String record_path = vformat("$.exceptions[%d]", i);
		Dictionary object;
		if (!require_dictionary(p_records[i], record_path, object, r_errors)) {
			continue;
		}
		validate_allowed_fields(object,
				Vector<String>({ "id", "parent", "when", "derive", "rationale", "positive_witnesses",
						"boundary_witnesses" }),
				record_path, r_errors);
		FSCompletenessException record;
		if (require_string(object, SNAME("id"), record_path + ".id", record.id, r_errors)) {
			append_unique_id(record.id, record_path + ".id", r_ids, r_errors);
		}
		if (require_string(object, SNAME("parent"), record_path + ".parent", record.parent, r_errors)) {
			if (record.parent.is_empty()) {
				append_error(r_errors, record_path + ".parent", "must be non-empty");
			} else if (!p_relation_ids.has(record.parent)) {
				append_error(r_errors, record_path + ".parent", vformat("unknown relation '%s'", record.parent));
			}
		}
		require_dictionary(object, SNAME("when"), record_path + ".when", record.when, r_errors);
		if (require_dictionary(object, SNAME("derive"), record_path + ".derive", record.derive, r_errors) && record.derive.is_empty()) {
			append_error(r_errors, record_path + ".derive", "must be non-empty");
		}
		if (require_string(object, SNAME("rationale"), record_path + ".rationale", record.rationale, r_errors) &&
				record.rationale.is_empty()) {
			append_error(r_errors, record_path + ".rationale", "must be non-empty");
		}
		Array positive_witnesses;
		if (require_array(object, SNAME("positive_witnesses"), record_path + ".positive_witnesses", positive_witnesses, r_errors)) {
			parse_string_array(positive_witnesses, record_path + ".positive_witnesses", record.positive_witnesses, r_errors, true);
		}
		Array boundary_witnesses;
		if (require_array(object, SNAME("boundary_witnesses"), record_path + ".boundary_witnesses", boundary_witnesses, r_errors)) {
			parse_string_array(boundary_witnesses, record_path + ".boundary_witnesses", record.boundary_witnesses, r_errors, true);
		}
		r_manifest.exceptions.push_back(record);
	}
}

Error FSCompletenessManifest::load(const String &p_path, FSCompletenessManifest &r_manifest, Vector<String> &r_errors) {
	r_manifest = FSCompletenessManifest();
	r_errors.clear();

	Error read_error = OK;
	const String source = FileAccess::get_file_as_string(p_path, &read_error);
	if (read_error != OK) {
		append_error(r_errors, p_path, vformat("could not read file (error %d)", read_error));
		return ERR_INVALID_DATA;
	}

	Variant json_data;
	if (parse_type_completeness_json(source, String(), json_data, r_errors) == ERR_PARSE_ERROR) {
		return ERR_INVALID_DATA;
	}
	if (json_data.get_type() != Variant::DICTIONARY) {
		append_error(r_errors, "$", "expected an object");
		return ERR_INVALID_DATA;
	}

	const Dictionary root = json_data;
	FSCompletenessManifest parsed;
	validate_allowed_fields(root,
			Vector<String>({ "schema_version", "family", "domain", "required_dimensions", "anchors", "relations",
					"exceptions", "max_chain_length" }),
			"$", r_errors);
	if (!root.has(SNAME("schema_version"))) {
		append_error(r_errors, "$.schema_version", "required field is missing");
	} else if (require_json_integer(root[SNAME("schema_version")], "$.schema_version", parsed.schema_version, r_errors)) {
		if (parsed.schema_version != 1) {
			append_error(r_errors, "$.schema_version", "must equal 1");
		}
	}
	if (require_string(root, SNAME("family"), "$.family", parsed.family, r_errors) && parsed.family.is_empty()) {
		append_error(r_errors, "$.family", "must be non-empty");
	}

	Dictionary domain;
	if (require_dictionary(root, SNAME("domain"), "$.domain", domain, r_errors)) {
		parse_domain(domain, parsed, r_errors);
	}
	Array required_dimensions;
	if (require_array(root, SNAME("required_dimensions"), "$.required_dimensions", required_dimensions, r_errors)) {
		parse_required_dimensions(required_dimensions, parsed, r_errors);
	}

	HashSet<String> ids;
	Array anchors;
	if (require_array(root, SNAME("anchors"), "$.anchors", anchors, r_errors)) {
		parse_anchors(anchors, parsed, ids, r_errors);
	}
	HashSet<String> relation_ids;
	Array relations;
	if (require_array(root, SNAME("relations"), "$.relations", relations, r_errors)) {
		parse_relations(relations, parsed, ids, relation_ids, r_errors);
	}
	Array exceptions;
	if (require_array(root, SNAME("exceptions"), "$.exceptions", exceptions, r_errors)) {
		parse_exceptions(exceptions, parsed, ids, relation_ids, r_errors);
	}

	if (root.has(SNAME("max_chain_length"))) {
		const Variant value = root[SNAME("max_chain_length")];
		if (require_json_integer(value, "$.max_chain_length", parsed.max_chain_length, r_errors)) {
			if (parsed.max_chain_length < 1 || parsed.max_chain_length > 3) {
				append_error(r_errors, "$.max_chain_length", "must be between 1 and 3");
			}
		}
	}

	if (!r_errors.is_empty()) {
		r_manifest = FSCompletenessManifest();
		return ERR_INVALID_DATA;
	}
	r_manifest = parsed;
	return OK;
}

static void append_catalog_errors(Vector<String> &r_errors, const String &p_file, const Vector<String> &p_file_errors) {
	for (const String &error : p_file_errors) {
		r_errors.push_back(vformat("%s: %s", p_file, error));
	}
}

static bool load_catalog_object(const String &p_path, Dictionary &r_root, Vector<String> &r_errors) {
	Error read_error = OK;
	const String source = FileAccess::get_file_as_string(p_path, &read_error);
	if (read_error != OK) {
		append_error(r_errors, "$", vformat("could not read file (error %d)", read_error));
		return false;
	}

	Variant json_data;
	if (parse_type_completeness_json(source, String(), json_data, r_errors) == ERR_PARSE_ERROR) {
		return false;
	}
	if (json_data.get_type() != Variant::DICTIONARY) {
		append_error(r_errors, "$", "expected an object");
		return false;
	}
	r_root = json_data;
	return true;
}

static Vector<String> sorted_dictionary_keys(const Dictionary &p_dictionary) {
	Vector<String> keys;
	for (const Variant &key : p_dictionary.keys()) {
		keys.push_back(key);
	}
	keys.sort();
	return keys;
}

static void validate_allowed_fields(const Dictionary &p_object, const Vector<String> &p_allowed_fields,
		const String &p_path, Vector<String> &r_errors) {
	for (const String &field : sorted_dictionary_keys(p_object)) {
		if (!p_allowed_fields.has(field)) {
			append_error(r_errors, append_json_path_member(p_path, field), vformat("unknown field '%s'", field));
		}
	}
}

static bool is_normalized_repository_path(const String &p_path) {
	if (p_path.is_empty() || p_path != p_path.strip_edges() || p_path.is_absolute_path() ||
			p_path.contains(":") || p_path.contains("\\")) {
		return false;
	}
	const bool is_directory_prefix = p_path.ends_with("/");
	const String path_without_trailing_slash = is_directory_prefix ? p_path.trim_suffix("/") : p_path;
	if (path_without_trailing_slash.is_empty() || path_without_trailing_slash.simplify_path() != path_without_trailing_slash) {
		return false;
	}
	const PackedStringArray components = path_without_trailing_slash.split("/", true);
	for (const String &component : components) {
		if (component.is_empty() || component == "." || component == "..") {
			return false;
		}
	}
	return true;
}

static bool is_normalized_repository_file_path(const String &p_path) {
	return !p_path.ends_with("/") && is_normalized_repository_path(p_path);
}

static bool capability_path_matches(const String &p_rule, const String &p_path) {
	if (p_rule.ends_with("/")) {
		return p_path.length() > p_rule.length() && p_path.begins_with(p_rule);
	}
	return p_path == p_rule;
}

static bool capability_rules_overlap(const String &p_left, const String &p_right) {
	return p_left == p_right || capability_path_matches(p_left, p_right) || capability_path_matches(p_right, p_left);
}

static bool is_foundry_script_production_path(const String &p_path) {
	static const String production_root = "modules/foundry_script/";
	return p_path.length() > production_root.length() && p_path.begins_with(production_root);
}

static bool is_foundry_script_production_rule(const String &p_path) {
	static const String production_root = "modules/foundry_script/";
	return p_path == production_root || is_foundry_script_production_path(p_path);
}

static bool is_permitted_nonproduction_rule(const String &p_path) {
	static const String production_root = "modules/foundry_script/";
	static const String production_root_without_slash = "modules/foundry_script";
	static const String tests_root = "modules/foundry_script/tests/";
	if (p_path == tests_root || p_path.begins_with(tests_root)) {
		return true;
	}
	if (p_path == production_root_without_slash || p_path == production_root ||
			p_path.begins_with(production_root)) {
		return false;
	}
	if (p_path.ends_with("/") && production_root.begins_with(p_path)) {
		return false;
	}
	return true;
}

static bool is_intentional_test_production_override(const String &p_production, const String &p_nonproduction) {
	static const String tests_root = "modules/foundry_script/tests/";
	return p_nonproduction.ends_with("/") &&
			(p_nonproduction == tests_root || p_nonproduction.begins_with(tests_root)) &&
			p_production.length() > p_nonproduction.length() && p_production.begins_with(p_nonproduction);
}

static bool parse_capability_string_array(const Dictionary &p_object, const StringName &p_field,
		const String &p_path, Vector<String> &r_values, Vector<String> &r_errors) {
	Array values;
	if (!require_array(p_object, p_field, p_path, values, r_errors)) {
		return false;
	}
	return parse_string_array(values, p_path, r_values, r_errors, true);
}

static bool is_safe_capability_family(const String &p_family) {
	if (p_family.is_empty() || p_family[0] < 'a' || p_family[0] > 'z') {
		return false;
	}
	for (int i = 1; i < p_family.length(); i++) {
		const char32_t character = p_family[i];
		if (!((character >= 'a' && character <= 'z') ||
					(character >= '0' && character <= '9') || character == '_')) {
			return false;
		}
	}
	const String upper_family = p_family.to_upper();
	if (upper_family == "CON" || upper_family == "PRN" || upper_family == "AUX" || upper_family == "NUL") {
		return false;
	}
	if (upper_family.length() == 4 &&
			(upper_family.begins_with("COM") || upper_family.begins_with("LPT")) &&
			upper_family[3] >= '1' && upper_family[3] <= '9') {
		return false;
	}
	return true;
}

static void sort_and_deduplicate_errors(Vector<String> &r_errors) {
	r_errors.sort();
	Vector<String> unique_errors;
	for (const String &error : r_errors) {
		if (unique_errors.is_empty() || unique_errors[unique_errors.size() - 1] != error) {
			unique_errors.push_back(error);
		}
	}
	r_errors = unique_errors;
}

static String canonicalize_existing_capability_path(const String &p_path) {
#ifdef UNIX_ENABLED
	char *resolved = ::realpath(p_path.utf8().get_data(), nullptr);
	if (resolved == nullptr) {
		return String();
	}
	String canonical;
	const Error parse_error = canonical.append_utf8(resolved);
	::free(resolved);
	return parse_error == OK ? canonical.simplify_path() : String();
#elif defined(WINDOWS_ENABLED)
	HANDLE handle = ::CreateFileW((LPCWSTR)(p_path.utf16().get_data()), FILE_READ_ATTRIBUTES,
			FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
			FILE_FLAG_BACKUP_SEMANTICS, nullptr);
	if (handle == INVALID_HANDLE_VALUE) {
		return String();
	}
	WCHAR buffer[4096];
	const DWORD length = ::GetFinalPathNameByHandleW(
			handle, buffer, 4095, FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
	::CloseHandle(handle);
	if (length == 0 || length > 4095) {
		return String();
	}
	buffer[length] = 0;
	return String::utf16((const char16_t *)buffer, (int)length)
			.trim_prefix("\\\\?\\")
			.replace("\\", "/")
			.simplify_path();
#else
	return p_path.simplify_path();
#endif
}

Error FSCompletenessCapabilityMap::load(const String &p_path, Vector<String> &r_errors) {
	production_prefixes.clear();
	nonproduction_prefixes.clear();
	broad_core_families.clear();
	loaded = false;
	r_errors.clear();

	Error read_error = OK;
	const String source = FileAccess::get_file_as_string(p_path, &read_error);
	if (read_error != OK) {
		append_error(r_errors, p_path, vformat("could not read file (error %d)", read_error));
		return ERR_INVALID_DATA;
	}

	Variant json_data;
	if (parse_type_completeness_json(source, String(), json_data, r_errors) == ERR_PARSE_ERROR) {
		return ERR_INVALID_DATA;
	}
	if (json_data.get_type() != Variant::DICTIONARY) {
		append_error(r_errors, "$", "expected an object");
		return ERR_INVALID_DATA;
	}

	const Dictionary root = json_data;
	validate_allowed_fields(root,
			Vector<String>({ "schema_version", "production", "nonproduction_prefixes", "broad_core_families" }),
			"$", r_errors);
	int schema_version = 0;
	if (!root.has(SNAME("schema_version"))) {
		append_error(r_errors, "$.schema_version", "required field is missing");
	} else if (require_json_integer(root[SNAME("schema_version")], "$.schema_version", schema_version, r_errors) &&
			schema_version != 1) {
		append_error(r_errors, "$.schema_version", "must equal 1");
	}

	FSCompletenessCapabilityMap parsed;
	Vector<String> ownership_entries;
	Array production;
	if (require_array(root, SNAME("production"), "$.production", production, r_errors)) {
		if (production.is_empty()) {
			append_error(r_errors, "$.production", "must contain at least one ownership record");
		}
		for (int record_index = 0; record_index < production.size(); record_index++) {
			const String record_path = vformat("$.production[%d]", record_index);
			Dictionary record;
			if (!require_dictionary(production[record_index], record_path, record, r_errors)) {
				continue;
			}
			validate_allowed_fields(record, Vector<String>({ "paths", "families" }), record_path, r_errors);
			Vector<String> paths;
			Vector<String> families;
			parse_capability_string_array(record, SNAME("paths"), record_path + ".paths", paths, r_errors);
			parse_capability_string_array(record, SNAME("families"), record_path + ".families", families, r_errors);

			HashSet<String> family_set;
			for (int family_index = 0; family_index < families.size(); family_index++) {
				const String &family = families[family_index];
				if (family.is_empty()) {
					continue;
				}
				if (!is_safe_capability_family(family)) {
					append_error(r_errors, vformat("%s.families[%d]", record_path, family_index),
							"must be a lowercase portable filename-stem family id");
					continue;
				}
				family_set.insert(family);
			}
			for (int path_index = 0; path_index < paths.size(); path_index++) {
				const String &path = paths[path_index];
				if (!is_normalized_repository_path(path)) {
					append_error(r_errors, vformat("%s.paths[%d]", record_path, path_index),
							"must be a normalized repository-relative path");
					continue;
				}
				if (!is_foundry_script_production_rule(path)) {
					append_error(r_errors, vformat("%s.paths[%d]", record_path, path_index),
							"must identify a path within modules/foundry_script/");
					continue;
				}
				parsed.production_prefixes.push_back(Pair<String, HashSet<String>>(path, family_set));
				ownership_entries.push_back(path);
			}
		}
	}

	for (int left = 0; left < ownership_entries.size(); left++) {
		for (int right = left + 1; right < ownership_entries.size(); right++) {
			if (capability_rules_overlap(ownership_entries[left], ownership_entries[right])) {
				append_error(r_errors, "$.production",
						vformat("ambiguous production ownership between '%s' and '%s'",
								ownership_entries[left], ownership_entries[right]));
			}
		}
	}

	Vector<String> nonproduction;
	if (parse_capability_string_array(root, SNAME("nonproduction_prefixes"), "$.nonproduction_prefixes",
				nonproduction, r_errors)) {
		for (int i = 0; i < nonproduction.size(); i++) {
			if (!is_normalized_repository_path(nonproduction[i])) {
				append_error(r_errors, vformat("$.nonproduction_prefixes[%d]", i),
						"must be a normalized repository-relative path");
				continue;
			}
			if (!is_permitted_nonproduction_rule(nonproduction[i])) {
				append_error(r_errors, vformat("$.nonproduction_prefixes[%d]", i),
						"may exempt Foundry Script production core; only the tests subtree is permitted");
				continue;
			}
			parsed.nonproduction_prefixes.push_back(nonproduction[i]);
		}
	}

	Vector<String> broad_families;
	if (parse_capability_string_array(root, SNAME("broad_core_families"), "$.broad_core_families",
				broad_families, r_errors)) {
		for (int family_index = 0; family_index < broad_families.size(); family_index++) {
			const String &family = broad_families[family_index];
			if (!family.is_empty()) {
				if (!is_safe_capability_family(family)) {
					append_error(r_errors, vformat("$.broad_core_families[%d]", family_index),
							"must be a lowercase portable filename-stem family id");
				} else {
					parsed.broad_core_families.insert(family);
				}
			}
		}
	}

	for (int left = 0; left < parsed.nonproduction_prefixes.size(); left++) {
		for (int right = left + 1; right < parsed.nonproduction_prefixes.size(); right++) {
			if (capability_rules_overlap(
						parsed.nonproduction_prefixes[left], parsed.nonproduction_prefixes[right])) {
				append_error(r_errors, "$.nonproduction_prefixes",
						vformat("overlapping entries '%s' and '%s'",
								parsed.nonproduction_prefixes[left], parsed.nonproduction_prefixes[right]));
			}
		}
	}
	for (const Pair<String, HashSet<String>> &production_entry : parsed.production_prefixes) {
		for (const String &nonproduction_entry : parsed.nonproduction_prefixes) {
			if (capability_rules_overlap(production_entry.first, nonproduction_entry) &&
					!is_intentional_test_production_override(production_entry.first, nonproduction_entry)) {
				append_error(r_errors, "$",
						vformat("production path '%s' overlaps nonproduction path '%s'",
								production_entry.first, nonproduction_entry));
			}
		}
	}

	if (!r_errors.is_empty()) {
		sort_and_deduplicate_errors(r_errors);
		return ERR_INVALID_DATA;
	}
	for (int left = 0; left < parsed.production_prefixes.size(); left++) {
		for (int right = left + 1; right < parsed.production_prefixes.size(); right++) {
			if (parsed.production_prefixes[right].first < parsed.production_prefixes[left].first) {
				SWAP(parsed.production_prefixes.write[left], parsed.production_prefixes.write[right]);
			}
		}
	}
	parsed.nonproduction_prefixes.sort();
	production_prefixes = parsed.production_prefixes;
	nonproduction_prefixes = parsed.nonproduction_prefixes;
	broad_core_families = parsed.broad_core_families;
	loaded = true;
	return OK;
}

FSCompletenessSelection FSCompletenessCapabilityMap::select(const Vector<String> &p_changed_paths) const {
	FSCompletenessSelection selection;
	if (!loaded) {
		selection.validation_errors.push_back("capability map is not loaded");
		return selection;
	}
	Vector<String> changed_paths = p_changed_paths;
	changed_paths.sort();
	String previous_path;
	bool has_previous_path = false;
	for (const String &path : changed_paths) {
		if (has_previous_path && path == previous_path) {
			continue;
		}
		previous_path = path;
		has_previous_path = true;

		if (!is_normalized_repository_file_path(path)) {
			selection.validation_errors.push_back(vformat("invalid changed path: %s", path));
			selection.used_broad_core_fallback = true;
			for (const String &family : broad_core_families) {
				selection.families.insert(family);
			}
			continue;
		}

		bool mapped = false;
		for (const Pair<String, HashSet<String>> &entry : production_prefixes) {
			if (!capability_path_matches(entry.first, path)) {
				continue;
			}
			mapped = true;
			for (const String &family : entry.second) {
				selection.families.insert(family);
			}
		}
		if (mapped) {
			continue;
		}

		bool nonproduction = false;
		for (const String &prefix : nonproduction_prefixes) {
			if (capability_path_matches(prefix, path)) {
				nonproduction = true;
				break;
			}
		}
		if (nonproduction) {
			continue;
		}
		if (!mapped && is_foundry_script_production_path(path)) {
			selection.used_broad_core_fallback = true;
			selection.validation_errors.push_back(vformat("unmapped production path: %s", path));
			for (const String &family : broad_core_families) {
				selection.families.insert(family);
			}
		}
	}
	sort_and_deduplicate_errors(selection.validation_errors);
	return selection;
}

Error FSCompletenessCapabilityMap::validate_against_rule_directory(
		const String &p_directory, Vector<String> &r_errors) const {
	r_errors.clear();
	Error open_error = OK;
	Ref<DirAccess> directory = DirAccess::open(p_directory, &open_error);
	if (directory.is_null()) {
		r_errors.push_back(vformat("%s: could not open rule directory (error %d)", p_directory, open_error));
		return ERR_INVALID_DATA;
	}
	Ref<DirAccess> filesystem = DirAccess::create(DirAccess::ACCESS_FILESYSTEM);
	if (filesystem.is_null()) {
		r_errors.push_back(vformat("%s: filesystem access is unavailable", p_directory));
		return ERR_INVALID_DATA;
	}
	if (filesystem->is_link(p_directory)) {
		r_errors.push_back(vformat("%s: linked rule directory is not allowed", p_directory));
		return ERR_INVALID_DATA;
	}
	const String directory_path = directory->get_current_dir().replace("\\", "/").simplify_path();
	const String canonical_directory_path = canonicalize_existing_capability_path(directory_path);
	if (canonical_directory_path.is_empty() || canonical_directory_path != directory_path) {
		r_errors.push_back(vformat("%s: rule directory must be canonical", p_directory));
		return ERR_INVALID_DATA;
	}
	if (directory->list_dir_begin() != OK) {
		r_errors.push_back(vformat("%s: could not list rule directory", p_directory));
		return ERR_INVALID_DATA;
	}

	Vector<String> json_files;
	for (String entry = directory->get_next(); !entry.is_empty(); entry = directory->get_next()) {
		if (entry.begins_with(".")) {
			continue;
		}
		const String entry_path = directory_path.path_join(entry).simplify_path();
		if (entry_path.get_base_dir() != directory_path) {
			r_errors.push_back(vformat("%s: non-contained rule entry '%s'", p_directory, entry));
			continue;
		}
		if (directory->is_link(entry) || filesystem->is_link(entry_path)) {
			r_errors.push_back(vformat("%s: linked rule entry '%s' is not allowed", p_directory, entry));
			continue;
		}
		if (directory->current_is_dir()) {
			r_errors.push_back(vformat("%s: unexpected directory entry '%s'", p_directory, entry));
			continue;
		}
		if (entry.get_extension() != "json") {
			r_errors.push_back(vformat("%s: unexpected non-JSON entry '%s'", p_directory, entry));
			continue;
		}
		json_files.push_back(entry_path);
	}
	directory->list_dir_end();
	json_files.sort();
	if (json_files.is_empty()) {
		r_errors.push_back(vformat("%s: must contain at least one JSON rule manifest", p_directory));
	}

	HashMap<String, String> family_sources;
	for (const String &file : json_files) {
		const String canonical_file = canonicalize_existing_capability_path(file);
		if (canonical_file.is_empty() || canonical_file != file || canonical_file.get_base_dir() != canonical_directory_path) {
			r_errors.push_back(vformat("%s: rule manifest must be a canonical direct child", file));
			continue;
		}
		const String filename_stem = file.get_file().get_basename();
		if (!is_safe_capability_family(filename_stem)) {
			r_errors.push_back(vformat("%s: filename stem '%s' is not a lowercase portable family id",
					file, filename_stem));
		}
		FSCompletenessManifest manifest;
		Vector<String> file_errors;
		if (FSCompletenessManifest::load(file, manifest, file_errors) != OK) {
			append_catalog_errors(r_errors, file, file_errors);
			continue;
		}
		if (filename_stem != manifest.family) {
			r_errors.push_back(vformat("%s: filename stem must equal rule family '%s'", file, manifest.family));
		}
		if (!is_safe_capability_family(manifest.family)) {
			r_errors.push_back(vformat("%s: rule family '%s' is not a lowercase portable filename stem",
					file, manifest.family));
			continue;
		}
		const String *prior_source = family_sources.getptr(manifest.family);
		if (prior_source != nullptr) {
			r_errors.push_back(vformat("%s: duplicate rule family '%s' (already declared by %s)",
					file, manifest.family, *prior_source));
			continue;
		}
		family_sources.insert(manifest.family, file);
	}

	HashSet<String> explicitly_mapped_families;
	for (const Pair<String, HashSet<String>> &entry : production_prefixes) {
		for (const String &family : entry.second) {
			explicitly_mapped_families.insert(family);
		}
	}
	HashSet<String> referenced_families = explicitly_mapped_families;
	for (const String &family : broad_core_families) {
		referenced_families.insert(family);
	}
	Vector<String> sorted_referenced;
	for (const String &family : referenced_families) {
		sorted_referenced.push_back(family);
	}
	sorted_referenced.sort();
	for (const String &family : sorted_referenced) {
		if (!family_sources.has(family)) {
			r_errors.push_back(vformat("mapped family '%s' has no rule manifest", family));
		}
	}

	Vector<String> sorted_rule_families;
	for (const KeyValue<String, String> &entry : family_sources) {
		sorted_rule_families.push_back(entry.key);
	}
	sorted_rule_families.sort();
	for (const String &family : sorted_rule_families) {
		if (!explicitly_mapped_families.has(family)) {
			r_errors.push_back(vformat("rule family '%s' is unreachable from the capability map", family));
		}
	}

	sort_and_deduplicate_errors(r_errors);
	return r_errors.is_empty() ? OK : ERR_INVALID_DATA;
}

static bool collect_catalog_files(const String &p_directory, Vector<String> &r_files, Vector<String> &r_errors) {
	Error open_error = OK;
	Ref<DirAccess> directory = DirAccess::open(p_directory, &open_error);
	if (directory.is_null()) {
		r_errors.push_back(vformat("%s: could not open directory (error %d)", p_directory, open_error));
		return false;
	}
	const Error list_error = directory->list_dir_begin();
	if (list_error != OK) {
		r_errors.push_back(vformat("%s: could not list directory (error %d)", p_directory, list_error));
		return false;
	}

	String entry = directory->get_next();
	while (!entry.is_empty()) {
		if (entry.begins_with(".")) {
			entry = directory->get_next();
			continue;
		}
		if (directory->current_is_dir()) {
			r_errors.push_back(vformat("%s: unexpected directory entry '%s'", p_directory, entry));
		} else if (entry.get_extension() != "json") {
			r_errors.push_back(vformat("%s: unexpected non-JSON entry '%s'", p_directory, entry));
		} else {
			r_files.push_back(p_directory.path_join(entry));
		}
		entry = directory->get_next();
	}
	directory->list_dir_end();
	r_files.sort();
	if (r_files.is_empty()) {
		r_errors.push_back(vformat("%s: must contain at least one JSON file", p_directory));
		return false;
	}
	return true;
}

static bool parse_catalog_schema_version(const Dictionary &p_root, int &r_schema_version, Vector<String> &r_errors) {
	if (!p_root.has(SNAME("schema_version"))) {
		append_error(r_errors, "$.schema_version", "required field is missing");
		return false;
	}
	if (!require_json_integer(p_root[SNAME("schema_version")], "$.schema_version", r_schema_version, r_errors)) {
		return false;
	}
	if (r_schema_version != 1) {
		append_error(r_errors, "$.schema_version", "must equal 1");
		return false;
	}
	return true;
}

static void parse_partition_file(const String &p_path, HashSet<String> &r_seen_axes,
		HashMap<String, FSCompletenessPartition> &r_partitions, Vector<String> &r_errors) {
	Vector<String> file_errors;
	Dictionary root;
	if (!load_catalog_object(p_path, root, file_errors)) {
		append_catalog_errors(r_errors, p_path, file_errors);
		return;
	}
	validate_allowed_fields(root, Vector<String>({ "schema_version", "axis", "leaves", "classes" }), "$", file_errors);

	FSCompletenessPartition partition;
	parse_catalog_schema_version(root, partition.schema_version, file_errors);
	const bool has_axis = require_string(root, SNAME("axis"), "$.axis", partition.axis, file_errors);
	if (has_axis) {
		if (partition.axis.is_empty()) {
			append_error(file_errors, "$.axis", "must be non-empty");
		} else if (r_seen_axes.has(partition.axis)) {
			append_error(file_errors, "$.axis", vformat("duplicate axis '%s'", partition.axis));
		} else {
			r_seen_axes.insert(partition.axis);
		}
	}

	Array leaves;
	if (require_array(root, SNAME("leaves"), "$.leaves", leaves, file_errors)) {
		parse_string_array(leaves, "$.leaves", partition.leaves, file_errors, true);
	}
	HashSet<String> declared_leaves;
	for (const String &leaf : partition.leaves) {
		declared_leaves.insert(leaf);
	}

	Dictionary classes;
	if (require_dictionary(root, SNAME("classes"), "$.classes", classes, file_errors)) {
		for (const String &class_name : sorted_dictionary_keys(classes)) {
			const String class_path = append_json_path_member("$.classes", class_name);
			if (class_name.is_empty()) {
				append_error(file_errors, "$.classes", "class names must be non-empty");
				continue;
			}
			const Variant members_variant = classes[class_name];
			if (members_variant.get_type() != Variant::ARRAY) {
				append_error(file_errors, class_path, "expected an array");
				continue;
			}
			Vector<String> members;
			parse_string_array(members_variant, class_path, members, file_errors, true);
			HashSet<String> member_set;
			for (int i = 0; i < members.size(); i++) {
				const String &member = members[i];
				member_set.insert(member);
				if (!declared_leaves.has(member)) {
					append_error(file_errors, vformat("%s[%d]", class_path, i),
							vformat("class member '%s' is not a declared leaf", member));
				}
			}
			partition.classes.insert(class_name, member_set);
		}
	}

	if (file_errors.is_empty()) {
		r_partitions.insert(partition.axis, partition);
	}
	append_catalog_errors(r_errors, p_path, file_errors);
}

static void parse_dimension_file(const String &p_path, HashSet<String> &r_seen_dimensions,
		HashMap<String, FSCompletenessDimension> &r_dimensions, Vector<String> &r_errors) {
	Vector<String> file_errors;
	Dictionary root;
	if (!load_catalog_object(p_path, root, file_errors)) {
		append_catalog_errors(r_errors, p_path, file_errors);
		return;
	}
	validate_allowed_fields(root, Vector<String>({ "schema_version", "adapter", "dimensions" }), "$", file_errors);

	int schema_version = 0;
	parse_catalog_schema_version(root, schema_version, file_errors);
	String adapter;
	if (require_string(root, SNAME("adapter"), "$.adapter", adapter, file_errors) && adapter.is_empty()) {
		append_error(file_errors, "$.adapter", "must be non-empty");
	}

	Array records;
	if (require_array(root, SNAME("dimensions"), "$.dimensions", records, file_errors)) {
		if (records.is_empty()) {
			append_error(file_errors, "$.dimensions", "must contain at least one dimension");
		}
		for (int i = 0; i < records.size(); i++) {
			const String record_path = vformat("$.dimensions[%d]", i);
			Dictionary record_object;
			if (!require_dictionary(records[i], record_path, record_object, file_errors)) {
				continue;
			}
			validate_allowed_fields(record_object, Vector<String>({ "id", "outcomes" }), record_path, file_errors);
			FSCompletenessDimension dimension;
			const bool has_id = require_string(record_object, SNAME("id"), record_path + ".id", dimension.id, file_errors);
			if (has_id) {
				if (dimension.id.is_empty()) {
					append_error(file_errors, record_path + ".id", "must be non-empty");
				} else if (r_seen_dimensions.has(dimension.id)) {
					append_error(file_errors, record_path + ".id", vformat("duplicate dimension id '%s'", dimension.id));
				} else {
					r_seen_dimensions.insert(dimension.id);
				}
			}

			Array outcomes;
			if (require_array(record_object, SNAME("outcomes"), record_path + ".outcomes", outcomes, file_errors)) {
				Vector<String> parsed_outcomes;
				parse_string_array(outcomes, record_path + ".outcomes", parsed_outcomes, file_errors, true);
				for (const String &outcome : parsed_outcomes) {
					dimension.outcomes.insert(outcome);
				}
			}
			if (has_id && !dimension.id.is_empty()) {
				r_dimensions.insert(dimension.id, dimension);
			}
		}
	}
	append_catalog_errors(r_errors, p_path, file_errors);
}

Error FSCompletenessCatalog::load(const String &p_root, Vector<String> &r_errors) {
	partitions.clear();
	dimensions.clear();
	r_errors.clear();

	Vector<String> partition_files;
	Vector<String> dimension_files;
	collect_catalog_files(p_root.path_join("partitions"), partition_files, r_errors);
	collect_catalog_files(p_root.path_join("dimensions"), dimension_files, r_errors);

	HashMap<String, FSCompletenessPartition> parsed_partitions;
	HashMap<String, FSCompletenessDimension> parsed_dimensions;
	HashSet<String> seen_axes;
	for (const String &path : partition_files) {
		parse_partition_file(path, seen_axes, parsed_partitions, r_errors);
	}
	HashSet<String> seen_dimensions;
	for (const String &path : dimension_files) {
		parse_dimension_file(path, seen_dimensions, parsed_dimensions, r_errors);
	}

	if (!r_errors.is_empty()) {
		return ERR_INVALID_DATA;
	}
	partitions = parsed_partitions;
	dimensions = parsed_dimensions;
	return OK;
}

bool FSCompletenessCatalog::axis_has_leaf(const String &p_axis, const String &p_leaf) const {
	const FSCompletenessPartition *partition = partitions.getptr(p_axis);
	if (partition == nullptr) {
		return false;
	}
	return partition->leaves.has(p_leaf);
}

bool FSCompletenessCatalog::class_contains(const String &p_axis, const String &p_class, const String &p_leaf) const {
	const FSCompletenessPartition *partition = partitions.getptr(p_axis);
	if (partition == nullptr) {
		return false;
	}
	const HashSet<String> *members = partition->classes.getptr(p_class);
	return members != nullptr && members->has(p_leaf);
}

bool FSCompletenessCatalog::dimension_has_outcome(const String &p_dimension, const String &p_outcome) const {
	const FSCompletenessDimension *dimension = dimensions.getptr(p_dimension);
	return dimension != nullptr && dimension->outcomes.has(p_outcome);
}

static Vector<String> sorted_manifest_axes(const HashMap<String, Vector<String>> &p_domain) {
	Vector<String> axes;
	for (const KeyValue<String, Vector<String>> &entry : p_domain) {
		axes.push_back(entry.key);
	}
	axes.sort();
	return axes;
}

static void validate_selector(const Dictionary &p_selector, const String &p_context,
		const HashMap<String, FSCompletenessPartition> &p_partitions,
		const HashMap<String, Vector<String>> *p_domain, Vector<String> &r_errors) {
	for (const String &axis : sorted_dictionary_keys(p_selector)) {
		const FSCompletenessPartition *partition = p_partitions.getptr(axis);
		if (partition == nullptr) {
			r_errors.push_back(vformat("%s selector uses unknown axis '%s'", p_context, axis));
			continue;
		}
		const Vector<String> *domain_leaves = p_domain == nullptr ? nullptr : p_domain->getptr(axis);

		const Variant value = p_selector[axis];
		if (value.get_type() == Variant::STRING) {
			const String leaf = value;
			if (!partition->leaves.has(leaf)) {
				r_errors.push_back(vformat("%s selector for axis '%s' uses unknown leaf '%s'", p_context, axis, leaf));
			} else if (p_domain != nullptr && domain_leaves == nullptr) {
				r_errors.push_back(vformat("%s selector uses axis '%s' not declared in manifest domain", p_context, axis));
			} else if (domain_leaves != nullptr && !domain_leaves->has(leaf)) {
				r_errors.push_back(vformat(
						"%s selector for axis '%s' uses leaf '%s' not declared in manifest domain", p_context, axis, leaf));
			}
			continue;
		}
		if (value.get_type() == Variant::DICTIONARY) {
			const Dictionary predicate = value;
			if (predicate.size() != 1 || !predicate.has(SNAME("class")) ||
					predicate[SNAME("class")].get_type() != Variant::STRING || String(predicate[SNAME("class")]).is_empty()) {
				r_errors.push_back(vformat(
						"%s selector for axis '%s' must be a string leaf or an object containing only 'class'", p_context, axis));
				continue;
			}
			const String class_name = predicate[SNAME("class")];
			if (!partition->classes.has(class_name)) {
				r_errors.push_back(vformat("%s selector for axis '%s' references unknown class '%s'",
						p_context, axis, class_name));
			} else if (p_domain != nullptr && domain_leaves == nullptr) {
				r_errors.push_back(vformat("%s selector uses axis '%s' not declared in manifest domain", p_context, axis));
			} else if (domain_leaves != nullptr) {
				bool matches_domain = false;
				const HashSet<String> &class_members = partition->classes[class_name];
				for (const String &leaf : *domain_leaves) {
					if (class_members.has(leaf)) {
						matches_domain = true;
						break;
					}
				}
				if (!matches_domain) {
					r_errors.push_back(vformat(
							"%s selector for axis '%s' class '%s' matches no leaves declared in manifest domain",
							p_context, axis, class_name));
				}
			}
			continue;
		}
		r_errors.push_back(vformat(
				"%s selector for axis '%s' must be a string leaf or an object containing only 'class'", p_context, axis));
	}
}

static void validate_coordinate_patch(const Dictionary &p_patch, const String &p_context,
		const HashMap<String, FSCompletenessPartition> &p_partitions, Vector<String> &r_errors) {
	for (const String &axis : sorted_dictionary_keys(p_patch)) {
		const FSCompletenessPartition *partition = p_partitions.getptr(axis);
		if (partition == nullptr) {
			r_errors.push_back(vformat("%s coordinate uses unknown axis '%s'", p_context, axis));
			continue;
		}

		const Variant value = p_patch[axis];
		if (value.get_type() != Variant::STRING || String(value).is_empty()) {
			r_errors.push_back(vformat("%s coordinate for axis '%s' must be a non-empty string leaf", p_context, axis));
			continue;
		}
		const String leaf = value;
		if (!partition->leaves.has(leaf)) {
			r_errors.push_back(vformat("%s coordinate for axis '%s' uses unknown leaf '%s'", p_context, axis, leaf));
		}
	}
}

static void validate_dimension_outcomes(const Dictionary &p_outcomes, const String &p_record,
		const String &p_verb, bool p_allow_same, const HashMap<String, FSCompletenessDimension> &p_dimensions,
		Vector<String> &r_errors) {
	for (const String &dimension_name : sorted_dictionary_keys(p_outcomes)) {
		const FSCompletenessDimension *dimension = p_dimensions.getptr(dimension_name);
		if (dimension == nullptr) {
			r_errors.push_back(vformat("%s %s unknown dimension '%s'", p_record, p_verb, dimension_name));
			continue;
		}
		const Variant value = p_outcomes[dimension_name];
		if (value.get_type() != Variant::STRING) {
			r_errors.push_back(vformat("%s %s non-string outcome for dimension '%s'", p_record, p_verb, dimension_name));
			continue;
		}
		const String outcome = value;
		if (p_allow_same && outcome == "same") {
			continue;
		}
		if (!dimension->outcomes.has(outcome)) {
			r_errors.push_back(vformat("%s %s unknown %s outcome '%s'", p_record, p_verb, dimension_name, outcome));
		}
	}
}

Error validate_manifest_vocabulary(const FSCompletenessManifest &p_manifest,
		const FSCompletenessCatalog &p_catalog, Vector<String> &r_errors) {
	r_errors.clear();

	for (const String &axis : sorted_manifest_axes(p_manifest.domain)) {
		const FSCompletenessPartition *partition = p_catalog.partitions.getptr(axis);
		if (partition == nullptr) {
			r_errors.push_back(vformat("domain uses unknown axis '%s'", axis));
			continue;
		}
		const Vector<String> &leaves = p_manifest.domain[axis];
		for (const String &leaf : leaves) {
			if (!p_catalog.axis_has_leaf(axis, leaf)) {
				r_errors.push_back(vformat("domain axis '%s' uses unknown leaf '%s'", axis, leaf));
			}
		}
	}

	for (const FSCompletenessRequiredDimension &required : p_manifest.required_dimensions) {
		if (!p_catalog.dimensions.has(required.dimension)) {
			r_errors.push_back(vformat("required dimension references unknown dimension '%s'", required.dimension));
		}
		validate_selector(required.when, vformat("required dimension '%s' when", required.dimension),
				p_catalog.partitions, &p_manifest.domain, r_errors);
	}

	for (const FSCompletenessAnchor &anchor : p_manifest.anchors) {
		const String record = vformat("anchor '%s'", anchor.id);
		for (const String &axis : sorted_dictionary_keys(anchor.coordinates)) {
			if (!p_catalog.partitions.has(axis)) {
				r_errors.push_back(vformat("%s uses unknown axis '%s'", record, axis));
				continue;
			}
			const Variant value = anchor.coordinates[axis];
			if (value.get_type() != Variant::STRING) {
				r_errors.push_back(vformat("%s uses non-string coordinate for axis '%s'", record, axis));
				continue;
			}
			const String leaf = value;
			if (!p_catalog.axis_has_leaf(axis, leaf)) {
				r_errors.push_back(vformat("%s uses unknown %s leaf '%s'", record, axis, leaf));
			}
		}
		for (const String &surface : anchor.surfaces) {
			if (!p_catalog.axis_has_leaf("surface", surface)) {
				r_errors.push_back(vformat("%s uses unknown surface leaf '%s'", record, surface));
			}
		}
		validate_dimension_outcomes(anchor.expect, record, "expects", false, p_catalog.dimensions, r_errors);
	}

	for (const FSCompletenessRelation &relation : p_manifest.relations) {
		const String record = vformat("relation '%s'", relation.id);
		validate_selector(relation.from, record + " from", p_catalog.partitions, &p_manifest.domain, r_errors);
		validate_coordinate_patch(relation.to, record + " to", p_catalog.partitions, r_errors);
		validate_dimension_outcomes(relation.derive, record, "derives", true, p_catalog.dimensions, r_errors);
	}

	for (const FSCompletenessException &exception : p_manifest.exceptions) {
		const String record = vformat("exception '%s'", exception.id);
		validate_selector(exception.when, record + " when", p_catalog.partitions, nullptr, r_errors);
		validate_dimension_outcomes(exception.derive, record, "derives", true, p_catalog.dimensions, r_errors);
	}

	return r_errors.is_empty() ? OK : ERR_INVALID_DATA;
}

} // namespace FSTests
