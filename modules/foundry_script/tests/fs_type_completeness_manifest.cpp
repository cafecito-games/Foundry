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

#include "core/io/file_access.h"
#include "core/io/json.h"
#include "core/math/math_funcs.h"
#include "core/variant/array.h"

namespace FSTests {

static void append_error(Vector<String> &r_errors, const String &p_path, const String &p_reason) {
	r_errors.push_back(vformat("%s: %s", p_path, p_reason));
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
		const String axis_path = vformat("$.domain.%s", axis);
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

} // namespace FSTests
