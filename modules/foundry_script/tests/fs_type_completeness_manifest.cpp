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

#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/io/json.h"
#include "core/math/math_funcs.h"
#include "core/variant/array.h"

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

// Core JSON parsing stores objects in Dictionary and therefore cannot retain duplicate member
// occurrences. This small recursive scanner runs only after the core parser has accepted the
// document. It follows the JSON structure closely enough to retain object membership and delegates
// string-literal decoding back to JSON, so escaped spellings such as `"a"` and `"\u0061"` compare
// as the same member without maintaining a second escape implementation here.
class JSONDuplicateMemberDetector {
	const String &source;
	Vector<String> &errors;
	int index = 0;

	void skip_whitespace() {
		while (index < source.length() && source[index] <= 32) {
			index++;
		}
	}

	bool parse_string(String &r_decoded) {
		skip_whitespace();
		if (index >= source.length() || source[index] != '"') {
			return false;
		}
		const int start = index++;
		while (index < source.length()) {
			const char32_t character = source[index++];
			if (character == '\\') {
				if (index >= source.length()) {
					return false;
				}
				index++;
				continue;
			}
			if (character == '"') {
				const Variant decoded = JSON::parse_string(source.substr(start, index - start));
				if (decoded.get_type() != Variant::STRING) {
					return false;
				}
				r_decoded = decoded;
				return true;
			}
		}
		return false;
	}

	bool parse_value(const String &p_path) {
		skip_whitespace();
		if (index >= source.length()) {
			return false;
		}
		switch (source[index]) {
			case '{':
				return parse_object(p_path);
			case '[':
				return parse_array(p_path);
			case '"': {
				String ignored;
				return parse_string(ignored);
			}
			default:
				while (index < source.length() && source[index] != ',' && source[index] != ']' && source[index] != '}') {
					index++;
				}
				return true;
		}
	}

	bool parse_object(const String &p_path) {
		if (source[index++] != '{') {
			return false;
		}
		HashSet<String> members;
		skip_whitespace();
		if (index < source.length() && source[index] == '}') {
			index++;
			return true;
		}

		while (index < source.length()) {
			String member;
			if (!parse_string(member)) {
				return false;
			}
			skip_whitespace();
			if (index >= source.length() || source[index++] != ':') {
				return false;
			}

			const String member_path = append_json_path_member(p_path, member);
			if (members.has(member)) {
				append_error(errors, member_path, vformat("duplicate object member '%s'", member));
			} else {
				members.insert(member);
			}
			if (!parse_value(member_path)) {
				return false;
			}

			skip_whitespace();
			if (index < source.length() && source[index] == ',') {
				index++;
				continue;
			}
			if (index < source.length() && source[index] == '}') {
				index++;
				return true;
			}
			return false;
		}
		return false;
	}

	bool parse_array(const String &p_path) {
		if (source[index++] != '[') {
			return false;
		}
		skip_whitespace();
		if (index < source.length() && source[index] == ']') {
			index++;
			return true;
		}

		int element = 0;
		while (index < source.length()) {
			if (!parse_value(vformat("%s[%d]", p_path, element++))) {
				return false;
			}
			skip_whitespace();
			if (index < source.length() && source[index] == ',') {
				index++;
				continue;
			}
			if (index < source.length() && source[index] == ']') {
				index++;
				return true;
			}
			return false;
		}
		return false;
	}

public:
	JSONDuplicateMemberDetector(const String &p_source, Vector<String> &r_errors) :
			source(p_source), errors(r_errors) {}

	void detect() {
		if (!parse_value("$")) {
			append_error(errors, "$", "could not verify unique object members");
		}
	}
};

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

	JSON json;
	if (json.parse(source) != OK) {
		append_error(r_errors, "$", vformat("invalid JSON at line %d: %s", json.get_error_line(), json.get_error_message()));
		return ERR_INVALID_DATA;
	}
	JSONDuplicateMemberDetector(source, r_errors).detect();
	if (json.get_data().get_type() != Variant::DICTIONARY) {
		append_error(r_errors, "$", "expected an object");
		return ERR_INVALID_DATA;
	}

	const Dictionary root = json.get_data();
	FSCompletenessManifest parsed;
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

	JSON json;
	if (json.parse(source) != OK) {
		append_error(r_errors, "$", vformat("invalid JSON at line %d: %s", json.get_error_line(), json.get_error_message()));
		return false;
	}
	JSONDuplicateMemberDetector(source, r_errors).detect();
	if (json.get_data().get_type() != Variant::DICTIONARY) {
		append_error(r_errors, "$", "expected an object");
		return false;
	}
	r_root = json.get_data();
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
		if (!directory->current_is_dir() && !entry.begins_with(".") && entry.get_extension() == "json") {
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
		const HashMap<String, FSCompletenessPartition> &p_partitions, Vector<String> &r_errors) {
	for (const String &axis : sorted_dictionary_keys(p_selector)) {
		const FSCompletenessPartition *partition = p_partitions.getptr(axis);
		if (partition == nullptr) {
			r_errors.push_back(vformat("%s selector uses unknown axis '%s'", p_context, axis));
			continue;
		}

		const Variant value = p_selector[axis];
		if (value.get_type() == Variant::STRING) {
			const String leaf = value;
			if (!partition->leaves.has(leaf)) {
				r_errors.push_back(vformat("%s selector for axis '%s' uses unknown leaf '%s'", p_context, axis, leaf));
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
				p_catalog.partitions, r_errors);
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
		validate_selector(relation.from, record + " from", p_catalog.partitions, r_errors);
		validate_coordinate_patch(relation.to, record + " to", p_catalog.partitions, r_errors);
		validate_dimension_outcomes(relation.derive, record, "derives", true, p_catalog.dimensions, r_errors);
	}

	for (const FSCompletenessException &exception : p_manifest.exceptions) {
		const String record = vformat("exception '%s'", exception.id);
		validate_selector(exception.when, record + " when", p_catalog.partitions, r_errors);
		validate_dimension_outcomes(exception.derive, record, "derives", true, p_catalog.dimensions, r_errors);
	}

	return r_errors.is_empty() ? OK : ERR_INVALID_DATA;
}

} // namespace FSTests
