/**************************************************************************/
/*  test_type_completeness_census.h                                       */
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

#pragma once

#include "fs_type_completeness_json.h"

#include "modules/foundry_script/fs_function.h"
#include "modules/foundry_script/fs_parser.h"

#include "core/io/file_access.h"
#include "core/templates/hash_map.h"
#include "core/templates/hash_set.h"
#include "core/variant/container_type_validate.h"
#include "core/variant/numeric_type.h"
#include "tests/test_macros.h"

namespace FSTests {

static const String type_census_root = "modules/foundry_script/tests/type_completeness";
static const String type_census_directory = type_census_root.path_join("census");

// The kind-id switches below cover every enumerator of their enum and carry no default arm, so a
// renumbered or appended enumerator fails to compile this file (dev_mode builds treat the switch
// warning as an error) and forces the census data to be updated in the same change.
static String parser_data_type_kind_id(FSParser::DataType::Kind p_kind) {
	switch (p_kind) {
		case FSParser::DataType::BUILTIN:
			return "builtin";
		case FSParser::DataType::NATIVE:
			return "native";
		case FSParser::DataType::SCRIPT:
			return "script";
		case FSParser::DataType::CLASS:
			return "class";
		case FSParser::DataType::ENUM:
			return "enum";
		case FSParser::DataType::TUPLE:
			return "tuple";
		case FSParser::DataType::UNION:
			return "union";
		case FSParser::DataType::TYPE_PARAMETER:
			return "type_parameter";
		case FSParser::DataType::VARIANT:
			return "variant";
		case FSParser::DataType::RESOLVING:
			return "resolving";
		case FSParser::DataType::UNRESOLVED:
			return "unresolved";
	}
	return String();
}

static String parser_type_parameter_scope_id(FSParser::DataType::TypeParameterScope p_scope) {
	switch (p_scope) {
		case FSParser::DataType::TYPE_PARAMETER_NONE:
			return "none";
		case FSParser::DataType::TYPE_PARAMETER_CLASS:
			return "class";
		case FSParser::DataType::TYPE_PARAMETER_ENUM:
			return "enum";
		case FSParser::DataType::TYPE_PARAMETER_METHOD:
			return "method";
	}
	return String();
}

static String parser_type_source_id(FSParser::DataType::TypeSource p_source) {
	switch (p_source) {
		case FSParser::DataType::UNDETECTED:
			return "undetected";
		case FSParser::DataType::INFERRED:
			return "inferred";
		case FSParser::DataType::ANNOTATED_EXPLICIT:
			return "annotated_explicit";
		case FSParser::DataType::ANNOTATED_INFERRED:
			return "annotated_inferred";
	}
	return String();
}

static String runtime_data_type_kind_id(FSDataType::Kind p_kind) {
	switch (p_kind) {
		case FSDataType::VARIANT:
			return "variant";
		case FSDataType::BUILTIN:
			return "builtin";
		case FSDataType::NATIVE:
			return "native";
		case FSDataType::SCRIPT:
			return "script";
		case FSDataType::FOUNDRY_SCRIPT:
			return "foundry_script";
		case FSDataType::TUPLE:
			return "tuple";
		case FSDataType::TYPE_PARAMETER:
			return "type_parameter";
		case FSDataType::UNION:
			return "union";
	}
	return String();
}

static String runtime_type_parameter_scope_id(FSDataType::TypeParameterScope p_scope) {
	switch (p_scope) {
		case FSDataType::TYPE_PARAMETER_NONE:
			return "none";
		case FSDataType::TYPE_PARAMETER_CLASS:
			return "class";
		case FSDataType::TYPE_PARAMETER_METHOD:
			return "method";
	}
	return String();
}

static String numeric_type_id(NumericType p_numeric_type) {
	switch (p_numeric_type) {
		case NumericType::NONE:
			return "none";
		case NumericType::INT8:
			return "int8";
		case NumericType::UINT8:
			return "uint8";
		case NumericType::INT16:
			return "int16";
		case NumericType::UINT16:
			return "uint16";
		case NumericType::INT32:
			return "int32";
		case NumericType::UINT32:
			return "uint32";
		case NumericType::INT64:
			return "int64";
		case NumericType::UINT64:
			return "uint64";
		case NumericType::MAX:
			return String();
	}
	return String();
}

struct CensusKindRow {
	int value = 0;
	String id;
};

struct CensusLiveEnum {
	String live_enum;
	Vector<CensusKindRow> rows;
};

static void census_add_enum(Vector<CensusLiveEnum> &r_enums, const String &p_live_enum,
		int p_first_value, int p_last_value, String (*p_id_of)(int)) {
	CensusLiveEnum entry;
	entry.live_enum = p_live_enum;
	for (int value = p_first_value; value <= p_last_value; value++) {
		const String id = p_id_of(value);
		if (id.is_empty()) {
			continue;
		}
		CensusKindRow row;
		row.value = value;
		row.id = id;
		entry.rows.push_back(row);
	}
	r_enums.push_back(entry);
}

static String parser_data_type_kind_id_of(int p_value) {
	return parser_data_type_kind_id((FSParser::DataType::Kind)p_value);
}

static String parser_type_parameter_scope_id_of(int p_value) {
	return parser_type_parameter_scope_id((FSParser::DataType::TypeParameterScope)p_value);
}

static String parser_type_source_id_of(int p_value) {
	return parser_type_source_id((FSParser::DataType::TypeSource)p_value);
}

static String runtime_data_type_kind_id_of(int p_value) {
	return runtime_data_type_kind_id((FSDataType::Kind)p_value);
}

static String runtime_type_parameter_scope_id_of(int p_value) {
	return runtime_type_parameter_scope_id((FSDataType::TypeParameterScope)p_value);
}

static String numeric_type_id_of(int p_value) {
	return numeric_type_id((NumericType)p_value);
}

static Vector<CensusLiveEnum> census_live_enums() {
	Vector<CensusLiveEnum> enums;
	census_add_enum(enums, "FSParser::DataType::Kind",
			int(FSParser::DataType::BUILTIN), int(FSParser::DataType::UNRESOLVED), parser_data_type_kind_id_of);
	census_add_enum(enums, "FSParser::DataType::TypeParameterScope",
			0, int(FSParser::DataType::TYPE_PARAMETER_METHOD), parser_type_parameter_scope_id_of);
	census_add_enum(enums, "FSParser::DataType::TypeSource",
			0, int(FSParser::DataType::ANNOTATED_INFERRED), parser_type_source_id_of);
	census_add_enum(enums, "NumericType",
			0, int(NumericType::MAX) - 1, numeric_type_id_of);
	census_add_enum(enums, "FSDataType::Kind",
			int(FSDataType::VARIANT), int(FSDataType::UNION), runtime_data_type_kind_id_of);
	census_add_enum(enums, "FSDataType::TypeParameterScope",
			0, int(FSDataType::TYPE_PARAMETER_METHOD), runtime_type_parameter_scope_id_of);
	return enums;
}

static bool census_read_json(const String &p_file_name, Variant &r_data, Vector<String> &r_errors) {
	const String path = type_census_directory.path_join(p_file_name);
	Error read_error = OK;
	const String source = FileAccess::get_file_as_string(path, &read_error);
	if (read_error != OK) {
		r_errors.push_back(vformat("%s: could not read file (error %d)", path, read_error));
		return false;
	}
	const Error parse_error = parse_type_completeness_json(source, String(), r_data, r_errors);
	if (parse_error != OK || r_data.get_type() != Variant::DICTIONARY) {
		r_errors.push_back(vformat("%s: expected a JSON object", path));
		return false;
	}
	return true;
}

static Vector<Dictionary> census_dictionary_array(const Dictionary &p_root, const String &p_key) {
	Vector<Dictionary> result;
	const Variant &value = p_root.get(p_key, Variant());
	if (value.get_type() != Variant::ARRAY) {
		return result;
	}
	for (int index = 0; index < Array(value).size(); index++) {
		const Variant &element = Array(value)[index];
		if (element.get_type() == Variant::DICTIONARY) {
			result.push_back(element);
		}
	}
	return result;
}

static Vector<String> census_string_array(const Dictionary &p_dictionary, const String &p_key) {
	Vector<String> result;
	const Variant &value = p_dictionary.get(p_key, Variant());
	if (value.get_type() != Variant::ARRAY) {
		return result;
	}
	for (int index = 0; index < Array(value).size(); index++) {
		const Variant &element = Array(value)[index];
		if (element.get_type() == Variant::STRING) {
			result.push_back(element);
		}
	}
	return result;
}

static String census_string(const Dictionary &p_dictionary, const String &p_key) {
	const Variant &value = p_dictionary.get(p_key, Variant());
	return value.get_type() == Variant::STRING ? String(value) : String();
}

static HashSet<String> census_string_set(const Vector<String> &p_values) {
	HashSet<String> set;
	for (const String &value : p_values) {
		set.insert(value);
	}
	return set;
}

static void census_collect_representation_slots(const Vector<Dictionary> &p_representations,
		HashMap<String, HashSet<String>> &r_slots_by_representation, Vector<String> &r_errors) {
	for (const Dictionary &representation : p_representations) {
		const String representation_id = census_string(representation, "id");
		if (representation_id.is_empty()) {
			r_errors.push_back("representations: an entry has no id");
			continue;
		}
		HashSet<String> slots;
		for (const Dictionary &slot : census_dictionary_array(representation, "child_slots")) {
			const String slot_id = census_string(slot, "id");
			if (slot_id.is_empty()) {
				r_errors.push_back(vformat("%s: a child slot has no id", representation_id));
				continue;
			}
			slots.insert(slot_id);
		}
		r_slots_by_representation.insert(representation_id, slots);
	}
}

TEST_SUITE("[Modules][FoundryScript][TypeCompleteness][Census]") {
	TEST_CASE("TypeCompleteness Census kind inventories match the live engine enums") {
		Vector<String> errors;
		Variant representations_data;
		REQUIRE_MESSAGE(census_read_json("representations.json", representations_data, errors),
				String(" | ").join(errors));

		const Vector<Dictionary> representations = census_dictionary_array(representations_data, "representations");
		REQUIRE_MESSAGE(representations.size() > 0, "representations.json lists no representations");

		const Vector<CensusLiveEnum> live_enums = census_live_enums();
		HashSet<String> enums_cross_checked;
		for (const Dictionary &representation : representations) {
			for (const Dictionary &kind_enum : census_dictionary_array(representation, "kind_enums")) {
				const String live_enum_name = census_string(kind_enum, "live_enum");
				const Vector<Dictionary> census_kinds = census_dictionary_array(kind_enum, "kinds");

				const CensusLiveEnum *live_enum = nullptr;
				for (const CensusLiveEnum &candidate : live_enums) {
					if (candidate.live_enum == live_enum_name) {
						live_enum = &candidate;
						break;
					}
				}
				if (live_enum == nullptr) {
					// The member-binding kind enum is a private nested type; its integers are pinned
					// instead by the bytecode-script accessor suite, so the census check for it is
					// structural. Any other unregistered enum name is a census error.
					REQUIRE_MESSAGE(live_enum_name == "FoundryScript::TypeArgumentBinding::Kind",
							vformat("%s: no live enum registry entry for %s", census_string(representation, "id"), live_enum_name));
					Vector<int> values;
					for (const Dictionary &kind : census_kinds) {
						values.push_back(int(kind.get("value", -1)));
					}
					values.sort();
					for (int index = 0; index < values.size(); index++) {
						if (values[index] != index) {
							errors.push_back(vformat("%s: %s values are not contiguous from zero",
									census_string(representation, "id"), live_enum_name));
							break;
						}
					}
					continue;
				}
				enums_cross_checked.insert(live_enum_name);

				Vector<String> census_ids;
				HashSet<int> census_values;
				for (const Dictionary &kind : census_kinds) {
					const String id = census_string(kind, "id");
					const int value = int(kind.get("value", -1));
					if (id.is_empty() || value < 0) {
						errors.push_back(vformat("%s: a %s kind has no id or value", census_string(representation, "id"), live_enum_name));
						continue;
					}
					census_ids.push_back(id);
					census_values.insert(value);
				}
				REQUIRE_MESSAGE(census_values.size() == census_ids.size(),
						vformat("%s: %s records duplicate values", census_string(representation, "id"), live_enum_name));

				Vector<String> live_ids;
				for (const CensusKindRow &row : live_enum->rows) {
					live_ids.push_back(row.id);
					if (!census_values.has(row.value)) {
						errors.push_back(vformat("%s: %s value %d (%s) is missing from the census",
								census_string(representation, "id"), live_enum_name, row.value, row.id));
					}
				}
				census_ids.sort();
				live_ids.sort();
				CHECK_MESSAGE(census_ids == live_ids,
						vformat("%s: census %s ids do not match the live enum", census_string(representation, "id"), live_enum_name));
			}
		}
		// Bidirectional coverage: every live enum must be recorded by at least one representation's
		// kind inventory, so deleting a kind_enum record fails instead of shrinking coverage silently.
		for (const CensusLiveEnum &live_enum : live_enums) {
			if (!enums_cross_checked.has(live_enum.live_enum)) {
				errors.push_back(vformat("no census representation records live enum %s", live_enum.live_enum));
			}
		}
		CHECK_MESSAGE(errors.is_empty(), String(" | ").join(errors));
	}

	TEST_CASE("TypeCompleteness Census policy matrix is exactly complete") {
		Vector<String> errors;
		Variant schema_data;
		Variant representations_data;
		Variant policies_data;
		REQUIRE_MESSAGE(census_read_json("schema.json", schema_data, errors), String(" | ").join(errors));
		REQUIRE_MESSAGE(census_read_json("representations.json", representations_data, errors), String(" | ").join(errors));
		REQUIRE_MESSAGE(census_read_json("policies.json", policies_data, errors), String(" | ").join(errors));

		const Dictionary schema = schema_data;
		const HashSet<String> policies = census_string_set(census_string_array(schema, "policies"));
		const Vector<String> surfaces = census_string_array(schema, "surfaces");
		const HashSet<String> surface_set = census_string_set(surfaces);
		REQUIRE_MESSAGE(policies.size() == 6, "schema.json must declare the six handling policies");
		REQUIRE_MESSAGE(surfaces.size() == 6, "schema.json must declare the six surfaces");

		const Vector<Dictionary> representations = census_dictionary_array(representations_data, "representations");
		HashMap<String, HashSet<String>> slots_by_representation;
		census_collect_representation_slots(representations, slots_by_representation, errors);

		HashMap<String, int> entry_counts;
		for (const Dictionary &entry : census_dictionary_array(policies_data, "entries")) {
			const String representation = census_string(entry, "representation");
			const String child_slot = census_string(entry, "child_slot");
			const String surface = census_string(entry, "surface");
			const String policy = census_string(entry, "policy");
			const String rationale = census_string(entry, "rationale");

			const HashSet<String> *slots = slots_by_representation.getptr(representation);
			if (slots == nullptr) {
				errors.push_back(vformat("policies: representation '%s' is not inventoried", representation));
				continue;
			}
			if (!slots->has(child_slot)) {
				errors.push_back(vformat("policies: %s has no child slot '%s'", representation, child_slot));
				continue;
			}
			if (!surface_set.has(surface)) {
				errors.push_back(vformat("policies: unknown surface '%s'", surface));
				continue;
			}
			if (!policies.has(policy)) {
				errors.push_back(vformat("policies: unknown policy '%s'", policy));
				continue;
			}
			if (rationale.is_empty()) {
				errors.push_back(vformat("policies: %s.%s on %s has no rationale", representation, child_slot, surface));
				continue;
			}
			entry_counts[representation + "::" + child_slot + "::" + surface] += 1;
		}

		int expected_entries = 0;
		for (const KeyValue<String, HashSet<String>> &representation : slots_by_representation) {
			for (const String &child_slot : representation.value) {
				for (const String &surface : surfaces) {
					expected_entries += 1;
					const String key = representation.key + "::" + child_slot + "::" + surface;
					const int *count = entry_counts.getptr(key);
					if (count == nullptr) {
						errors.push_back(vformat("policies: no policy for %s.%s on %s (uncovered child)",
								representation.key, child_slot, surface));
					} else if (*count > 1) {
						errors.push_back(vformat("policies: %d policies for %s.%s on %s (ambiguous)",
								*count, representation.key, child_slot, surface));
					}
				}
			}
		}
		const int actual_entries = census_dictionary_array(policies_data, "entries").size();
		CHECK_MESSAGE(actual_entries == expected_entries,
				vformat("policies.json has %d entries but the inventory expects exactly %d", actual_entries, expected_entries));
		CHECK_MESSAGE(errors.is_empty(), String(" | ").join(errors));
	}

	TEST_CASE("TypeCompleteness Census vocabularies and references are closed") {
		Vector<String> errors;
		Variant schema_data;
		Variant representations_data;
		REQUIRE_MESSAGE(census_read_json("schema.json", schema_data, errors), String(" | ").join(errors));
		REQUIRE_MESSAGE(census_read_json("representations.json", representations_data, errors), String(" | ").join(errors));

		const Dictionary schema = schema_data;
		const HashSet<String> axes = census_string_set(census_string_array(schema, "axes"));
		const HashSet<String> owners = census_string_set(census_string_array(schema, "owners"));
		REQUIRE_MESSAGE(axes.size() > 0, "schema.json declares no axes vocabulary");
		REQUIRE_MESSAGE(owners.size() > 0, "schema.json declares no owners vocabulary");

		HashSet<String> representation_ids;
		const Vector<Dictionary> representations = census_dictionary_array(representations_data, "representations");
		for (const Dictionary &representation : representations) {
			representation_ids.insert(census_string(representation, "id"));
		}

		for (const Dictionary &representation : representations) {
			const String id = census_string(representation, "id");
			if (!owners.has(census_string(representation, "owner"))) {
				errors.push_back(vformat("%s: owner '%s' is outside the owners vocabulary", id, census_string(representation, "owner")));
			}
			for (const Dictionary &slot : census_dictionary_array(representation, "child_slots")) {
				const String slot_id = census_string(slot, "id");
				for (const String &axis : census_string_array(slot, "axes")) {
					if (!axes.has(axis)) {
						errors.push_back(vformat("%s.%s: axis '%s' is outside the axes vocabulary", id, slot_id, axis));
					}
				}
				const String child_representation = census_string(slot, "child_representation");
				if (!representation_ids.has(child_representation) && child_representation != "none") {
					errors.push_back(vformat("%s.%s: references uninventoried child representation '%s'", id, slot_id, child_representation));
				}
			}
		}
		CHECK_MESSAGE(errors.is_empty(), String(" | ").join(errors));
	}

	TEST_CASE("TypeCompleteness Census transitions resolve between inventoried representations") {
		Vector<String> errors;
		Variant schema_data;
		Variant representations_data;
		Variant transitions_data;
		REQUIRE_MESSAGE(census_read_json("schema.json", schema_data, errors), String(" | ").join(errors));
		REQUIRE_MESSAGE(census_read_json("representations.json", representations_data, errors), String(" | ").join(errors));
		REQUIRE_MESSAGE(census_read_json("transitions.json", transitions_data, errors), String(" | ").join(errors));

		const HashSet<String> transition_kinds = census_string_set(census_string_array(schema_data, "transition_kinds"));

		HashSet<String> representation_ids;
		HashSet<String> referenced_families;
		for (const Dictionary &representation : census_dictionary_array(representations_data, "representations")) {
			representation_ids.insert(census_string(representation, "id"));
			for (const Dictionary &slot : census_dictionary_array(representation, "child_slots")) {
				for (const String &family : census_string_array(slot, "families")) {
					referenced_families.insert(family);
				}
			}
		}

		for (const Dictionary &transition : census_dictionary_array(transitions_data, "transitions")) {
			const String id = census_string(transition, "id");
			if (!representation_ids.has(census_string(transition, "from"))) {
				errors.push_back(vformat("transitions: %s starts from an uninventoried representation", id));
			}
			if (!representation_ids.has(census_string(transition, "to"))) {
				errors.push_back(vformat("transitions: %s targets an uninventoried representation", id));
			}
			if (!transition_kinds.has(census_string(transition, "kind"))) {
				errors.push_back(vformat("transitions: %s has an unknown kind", id));
			}
			if (census_string(transition, "anchor").is_empty()) {
				errors.push_back(vformat("transitions: %s has no production anchor", id));
			}
			for (const String &family : census_string_array(transition, "families")) {
				referenced_families.insert(family);
			}
		}

		for (const String &family : referenced_families) {
			if (!FileAccess::exists(type_census_root.path_join("rules").path_join(family + ".json"))) {
				errors.push_back(vformat("census references rule family '%s' with no rules/ file", family));
			}
		}
		CHECK_MESSAGE(referenced_families.size() > 0, "the census references no rule family at all");
		CHECK_MESSAGE(errors.is_empty(), String(" | ").join(errors));
	}

	TEST_CASE("TypeCompleteness Census unsupported entries carry rationale and witnesses") {
		Vector<String> errors;
		Variant schema_data;
		Variant representations_data;
		Variant unsupported_data;
		REQUIRE_MESSAGE(census_read_json("schema.json", schema_data, errors), String(" | ").join(errors));
		REQUIRE_MESSAGE(census_read_json("representations.json", representations_data, errors), String(" | ").join(errors));
		REQUIRE_MESSAGE(census_read_json("unsupported.json", unsupported_data, errors), String(" | ").join(errors));

		const HashSet<String> witness_kinds = census_string_set(census_string_array(schema_data, "witness_kinds"));
		const HashSet<String> witness_statuses = census_string_set(census_string_array(schema_data, "witness_statuses"));

		HashSet<String> representation_ids;
		for (const Dictionary &representation : census_dictionary_array(representations_data, "representations")) {
			representation_ids.insert(census_string(representation, "id"));
		}

		HashSet<String> entry_ids;
		int present_witnesses = 0;
		int deferred_witnesses = 0;
		for (const Dictionary &entry : census_dictionary_array(unsupported_data, "entries")) {
			const String id = census_string(entry, "id");
			if (entry_ids.has(id)) {
				errors.push_back(vformat("unsupported: duplicate entry id '%s'", id));
			}
			entry_ids.insert(id);
			if (!representation_ids.has(census_string(entry, "representation"))) {
				errors.push_back(vformat("unsupported: %s names an uninventoried representation", id));
			}
			if (census_string(entry, "rationale").is_empty()) {
				errors.push_back(vformat("unsupported: %s has no rationale", id));
			}
			const Vector<Dictionary> witnesses = census_dictionary_array(entry, "witnesses");
			if (witnesses.is_empty()) {
				errors.push_back(vformat("unsupported: %s has no witness", id));
			}
			for (const Dictionary &witness : witnesses) {
				const String kind = census_string(witness, "kind");
				const String status = census_string(witness, "status");
				const String reference = census_string(witness, "reference");
				if (!witness_kinds.has(kind)) {
					errors.push_back(vformat("unsupported: %s has a witness of unknown kind '%s'", id, kind));
					continue;
				}
				if (!witness_statuses.has(status)) {
					errors.push_back(vformat("unsupported: %s has a witness of unknown status '%s'", id, status));
					continue;
				}
				if (reference.is_empty()) {
					errors.push_back(vformat("unsupported: %s has an empty witness reference", id));
					continue;
				}
				if (status == "present") {
					present_witnesses += 1;
					if (kind == "fixture" && !FileAccess::exists(reference)) {
						errors.push_back(vformat("unsupported: %s references fixture '%s' that does not exist", id, reference));
					}
					if (kind == "doctest_case" && (!reference.begins_with("[") || !reference.contains("] "))) {
						errors.push_back(vformat("unsupported: %s references doctest case '%s' with no suite-qualified name", id, reference));
					}
				} else {
					deferred_witnesses += 1;
					if (census_string(witness, "deferred_reason").is_empty()) {
						errors.push_back(vformat("unsupported: %s defers a witness without a reason", id));
					}
				}
			}
		}
		CHECK_MESSAGE(present_witnesses > 0, "no unsupported entry has a present witness");
		CHECK_MESSAGE(deferred_witnesses > 0, "deferred witnesses are expected until the #2477 adapters land");
		CHECK_MESSAGE(errors.is_empty(), String(" | ").join(errors));
	}

	TEST_CASE("TypeCompleteness Census production paths reconcile with the capability map") {
		Vector<String> errors;
		Variant representations_data;
		Variant reconciliation_data;
		Variant capabilities_data;
		REQUIRE_MESSAGE(census_read_json("representations.json", representations_data, errors), String(" | ").join(errors));
		REQUIRE_MESSAGE(census_read_json("capability_reconciliation.json", reconciliation_data, errors), String(" | ").join(errors));
		REQUIRE(census_read_json("../capabilities.json", capabilities_data, errors));
		REQUIRE_MESSAGE(errors.is_empty(), String(" | ").join(errors));

		HashSet<String> capability_paths;
		for (const Dictionary &record : census_dictionary_array(capabilities_data, "production")) {
			for (const String &path : census_string_array(record, "paths")) {
				capability_paths.insert(path);
			}
		}
		REQUIRE_MESSAGE(capability_paths.size() > 0, "capabilities.json lists no production paths");

		HashSet<String> census_paths;
		for (const Dictionary &representation : census_dictionary_array(representations_data, "representations")) {
			for (const String &path : census_string_array(representation, "production_paths")) {
				census_paths.insert(path);
			}
		}

		const Vector<String> mapped_paths = census_string_array(reconciliation_data, "mapped_production_paths");
		Vector<Dictionary> unmapped_records = census_dictionary_array(reconciliation_data, "unmapped_production_paths");
		HashSet<String> declared_paths;
		for (const String &path : mapped_paths) {
			declared_paths.insert(path);
			if (!capability_paths.has(path)) {
				errors.push_back(vformat("reconciliation: '%s' is declared mapped but the capability map does not list it", path));
			}
		}
		for (const Dictionary &record : unmapped_records) {
			const String path = census_string(record, "path");
			declared_paths.insert(path);
			if (capability_paths.has(path)) {
				errors.push_back(vformat("reconciliation: '%s' is declared unmapped but the capability map lists it", path));
			}
			if (census_string(record, "rationale").is_empty()) {
				errors.push_back(vformat("reconciliation: unmapped path '%s' has no rationale", path));
			}
			if (!record.has("owning_issue")) {
				errors.push_back(vformat("reconciliation: unmapped path '%s' has no owning issue", path));
			}
		}

		for (const String &path : census_paths) {
			if (!declared_paths.has(path)) {
				errors.push_back(vformat("reconciliation: census production path '%s' is neither mapped nor declared unmapped", path));
			}
		}
		// Reverse direction: a path the capability map lists must be visible to the census (a
		// production path of some representation) or explicitly reconciled, so a map addition the
		// census never absorbed is noisy instead of silently ignored.
		for (const String &path : capability_paths) {
			if (!census_paths.has(path) && !declared_paths.has(path)) {
				errors.push_back(vformat("reconciliation: capability-map path '%s' appears in neither the census nor the reconciliation", path));
			}
		}
		CHECK_MESSAGE(errors.is_empty(), String(" | ").join(errors));
	}

	TEST_CASE("TypeCompleteness Census distinguishes surface applicability per representation") {
		Vector<String> errors;
		Variant schema_data;
		Variant representations_data;
		Variant policies_data;
		REQUIRE_MESSAGE(census_read_json("schema.json", schema_data, errors), String(" | ").join(errors));
		REQUIRE_MESSAGE(census_read_json("representations.json", representations_data, errors), String(" | ").join(errors));
		REQUIRE_MESSAGE(census_read_json("policies.json", policies_data, errors), String(" | ").join(errors));

		const Vector<String> surfaces = census_string_array(schema_data, "surfaces");

		// Convention: a surface a representation is not applicable for is a surface its records never
		// reach, so every policy for that (representation, surface) pair must be not_applicable. A
		// project/erase cell on an inapplicable surface would describe a crossing that cannot happen.
		HashMap<String, String> policy_by_representation_surface;
		for (const Dictionary &entry : census_dictionary_array(policies_data, "entries")) {
			policy_by_representation_surface[census_string(entry, "representation") + "::" + census_string(entry, "child_slot") + "::" + census_string(entry, "surface")] =
					census_string(entry, "policy");
		}

		bool any_bytecode_only = false;
		for (const Dictionary &representation : census_dictionary_array(representations_data, "representations")) {
			const String id = census_string(representation, "id");
			const Variant &applicability = representation.get("surface_applicability", Variant());
			if (applicability.get_type() != Variant::DICTIONARY) {
				errors.push_back(vformat("%s: no surface_applicability record", id));
				continue;
			}
			const Dictionary applicability_dictionary = applicability;
			for (const String &surface : surfaces) {
				const Variant &value = applicability_dictionary.get(surface, Variant());
				if (value.get_type() != Variant::BOOL) {
					errors.push_back(vformat("%s: surface_applicability.%s is not a boolean", id, surface));
					continue;
				}
				if (!bool(value)) {
					for (const Dictionary &slot : census_dictionary_array(representation, "child_slots")) {
						const String key = id + "::" + census_string(slot, "id") + "::" + surface;
						const String *policy = policy_by_representation_surface.getptr(key);
						if (policy != nullptr && *policy != "not_applicable") {
							errors.push_back(vformat("%s: %s is not applicable on %s but slot '%s' has policy '%s'",
									id, id, surface, census_string(slot, "id"), *policy));
						}
					}
				}
			}
			if (applicability_dictionary.size() != surfaces.size()) {
				errors.push_back(vformat("%s: surface_applicability does not cover exactly the surface vocabulary", id));
			}
			const bool bytecode_only = bool(applicability_dictionary.get("runtime_bytecode", false)) &&
					!bool(applicability_dictionary.get("runtime_text", false));
			any_bytecode_only = any_bytecode_only || bytecode_only;
		}
		CHECK_MESSAGE(any_bytecode_only, "no representation is bytecode-reload-specific; the serialization surface is not distinguished");
		CHECK_MESSAGE(errors.is_empty(), String(" | ").join(errors));
	}
}

} // namespace FSTests
