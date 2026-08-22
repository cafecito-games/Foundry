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

#include "fs_temporary_project_tree.h"
#include "fs_type_completeness_cache.h"
#include "fs_type_completeness_census.h"

#include "modules/foundry_script/fs_function.h"
#include "modules/foundry_script/fs_parser.h"

#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/io/json.h"
#include "core/os/os.h"
#include "core/templates/hash_map.h"
#include "core/templates/hash_set.h"
#include "core/variant/container_type_validate.h"
#include "core/variant/numeric_type.h"
#include "tests/test_macros.h"

namespace FSTests {

// The census documents belong to the tracked catalog, so its root is read from the one place that
// names it rather than spelled again here.
static String type_census_root() {
	return tracked_catalog_root();
}

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
	return read_completeness_json(
			FSCompletenessCensus::census_directory(type_census_root()).path_join(p_file_name), r_data, r_errors);
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

// A writable copy of the tracked catalog, so a negative case can mutate one census document without
// touching the repository. The whole catalog is copied because a coverage witness resolves through the
// rule manifests and the catalog next to it.
static String stage_census_catalog(TemporaryProjectTree &p_tree) {
	const String staged_root = p_tree.root.path_join("catalog");
	Ref<DirAccess> filesystem = DirAccess::create(DirAccess::ACCESS_FILESYSTEM);
	REQUIRE(filesystem.is_valid());
	REQUIRE_EQ(filesystem->copy_dir(type_census_root(), staged_root), OK);
	return staged_root;
}

static Dictionary tracked_coverage_document() {
	Vector<String> errors;
	Variant data;
	REQUIRE_MESSAGE(census_read_json("coverage.json", data, errors), String(" | ").join(errors));
	return Dictionary(data).duplicate(true);
}

static void write_staged_coverage(TemporaryProjectTree &p_tree, const Dictionary &p_document) {
	p_tree.write_file("catalog/census/coverage.json", JSON::stringify(p_document, "\t") + "\n");
}

// The first entry of p_kind whose witness this build can bind. A witness declared for another build
// configuration resolves to nothing here whatever is done to it, so a negative case that broke one
// would be asserting on the configuration instead of on the breakage.
static int first_coverage_entry_with_witness_kind(const Array &p_entries, const String &p_kind) {
	for (int index = 0; index < p_entries.size(); index++) {
		const Dictionary entry = p_entries[index];
		const Variant &witness = entry.get("witness", Variant());
		if (witness.get_type() != Variant::DICTIONARY || census_string(witness, "kind") != p_kind) {
			continue;
		}
		FSCompletenessCoverageWitness declared_witness;
		declared_witness.kind = p_kind;
		declared_witness.build_configuration = census_string(witness, "build_configuration");
		if (FSCompletenessCensus::witness_needs_other_configuration(declared_witness)) {
			continue;
		}
		return index;
	}
	return -1;
}

// Loads p_staged_root's census and reports the refusal messages, so a negative case asserts on the
// message a maintainer would read rather than only on an error code.
static Error load_staged_census(const String &p_staged_root, FSCompletenessCensusSummary &r_summary,
		Vector<String> &r_errors) {
	return FSCompletenessCensus::load(p_staged_root, r_summary, r_errors);
}

static bool census_errors_mention(const Vector<String> &p_errors, const String &p_fragment) {
	for (const String &error : p_errors) {
		if (error.contains(p_fragment)) {
			return true;
		}
	}
	return false;
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
		// Completeness of the matrix is decided by FSCompletenessCensus::load, because a run has to
		// decide it too: a matrix that shrank together with its coverage entries would otherwise load
		// clean inside the runner while only this test noticed.
		Vector<String> errors;
		FSCompletenessCensusSummary summary;
		CHECK_EQ(FSCompletenessCensus::load(type_census_root(), summary, errors), OK);
		CHECK_MESSAGE(errors.is_empty(), String(" | ").join(errors));

		Variant schema_data;
		Variant representations_data;
		Variant policies_data;
		REQUIRE_MESSAGE(census_read_json("schema.json", schema_data, errors), String(" | ").join(errors));
		REQUIRE_MESSAGE(census_read_json("representations.json", representations_data, errors), String(" | ").join(errors));
		REQUIRE_MESSAGE(census_read_json("policies.json", policies_data, errors), String(" | ").join(errors));
		const Vector<String> surfaces = census_string_array(schema_data, "surfaces");
		REQUIRE_MESSAGE(census_string_array(schema_data, "policies").size() == 6,
				"schema.json must declare the six handling policies");
		REQUIRE_MESSAGE(surfaces.size() == 6, "schema.json must declare the six surfaces");

		HashMap<String, HashSet<String>> slots_by_representation;
		census_collect_representation_slots(
				census_dictionary_array(representations_data, "representations"), slots_by_representation, errors);
		int expected_entries = 0;
		for (const KeyValue<String, HashSet<String>> &representation : slots_by_representation) {
			expected_entries += representation.value.size() * surfaces.size();
		}
		CHECK_EQ(census_dictionary_array(policies_data, "entries").size(), expected_entries);
		CHECK_MESSAGE(errors.is_empty(), String(" | ").join(errors));
	}

	TEST_CASE("TypeCompleteness Census refuses a policy matrix that no longer covers the inventory") {
		TemporaryProjectTree tree(vformat("type_completeness_census_matrix_%d", OS::get_singleton()->get_process_id()));
		REQUIRE(tree.is_valid());
		const String staged_root = stage_census_catalog(tree);

		Vector<String> read_errors;
		Variant policies_data;
		REQUIRE_MESSAGE(census_read_json("policies.json", policies_data, read_errors), String(" | ").join(read_errors));
		Variant coverage_data;
		REQUIRE_MESSAGE(census_read_json("coverage.json", coverage_data, read_errors), String(" | ").join(read_errors));

		SUBCASE("a deleted policy takes its coverage entry with it and is still refused") {
			// The pair disappears from both documents, which is exactly the shape a shrinking census
			// takes: without the inventory cross-check both documents agree and nothing observes the
			// loss.
			Dictionary policies = Dictionary(policies_data).duplicate(true);
			Array policy_entries = policies["entries"];
			const Dictionary removed = policy_entries[0];
			policy_entries.remove_at(0);
			policies["entries"] = policy_entries;
			tree.write_file("catalog/census/policies.json", JSON::stringify(policies, "\t") + "\n");

			Dictionary coverage = Dictionary(coverage_data).duplicate(true);
			Array coverage_entries = coverage["entries"];
			for (int index = 0; index < coverage_entries.size(); index++) {
				const Dictionary entry = coverage_entries[index];
				if (entry["representation"] == removed["representation"] &&
						entry["child_slot"] == removed["child_slot"] && entry["surface"] == removed["surface"]) {
					coverage_entries.remove_at(index);
					break;
				}
			}
			coverage["entries"] = coverage_entries;
			write_staged_coverage(tree, coverage);

			Vector<String> errors;
			FSCompletenessCensusSummary summary;
			CHECK_EQ(load_staged_census(staged_root, summary, errors), ERR_INVALID_DATA);
			CHECK_MESSAGE(census_errors_mention(errors,
								  vformat("no policy for %s::%s::%s", String(removed["representation"]),
										  String(removed["child_slot"]), String(removed["surface"]))),
					String(" | ").join(errors));
			CHECK(summary.entries.is_empty());
		}

		SUBCASE("a duplicated policy is refused rather than counted twice") {
			Dictionary policies = Dictionary(policies_data).duplicate(true);
			Array policy_entries = policies["entries"];
			policy_entries.push_back(Dictionary(policy_entries[0]).duplicate(true));
			policies["entries"] = policy_entries;
			tree.write_file("catalog/census/policies.json", JSON::stringify(policies, "\t") + "\n");

			Vector<String> errors;
			FSCompletenessCensusSummary summary;
			CHECK_EQ(load_staged_census(staged_root, summary, errors), ERR_INVALID_DATA);
			CHECK_MESSAGE(census_errors_mention(errors, "duplicate policy for"), String(" | ").join(errors));
		}

		SUBCASE("a malformed child-slot inventory is refused rather than read as fewer slots") {
			// The inventory decides how many cells the matrix must carry, so a slot list of the wrong
			// type may not read as a representation with no children.
			Vector<String> inventory_errors;
			Variant representations_data;
			REQUIRE_MESSAGE(census_read_json("representations.json", representations_data, inventory_errors),
					String(" | ").join(inventory_errors));
			Dictionary representations = Dictionary(representations_data).duplicate(true);
			Array inventory = representations["representations"];
			Dictionary representation = inventory[0];
			representation["child_slots"] = "not an array";
			inventory[0] = representation;
			representations["representations"] = inventory;
			tree.write_file("catalog/census/representations.json", JSON::stringify(representations, "\t") + "\n");

			Vector<String> errors;
			FSCompletenessCensusSummary summary;
			CHECK_EQ(load_staged_census(staged_root, summary, errors), ERR_INVALID_DATA);
			CHECK_MESSAGE(census_errors_mention(errors, "child_slots: must be an array"),
					String(" | ").join(errors));
		}

		SUBCASE("a policy for a slot the inventory does not declare is refused") {
			Dictionary policies = Dictionary(policies_data).duplicate(true);
			Array policy_entries = policies["entries"];
			Dictionary entry = Dictionary(policy_entries[0]).duplicate(true);
			entry["child_slot"] = "a_slot_no_representation_declares";
			policy_entries.push_back(entry);
			policies["entries"] = policy_entries;
			tree.write_file("catalog/census/policies.json", JSON::stringify(policies, "\t") + "\n");

			Vector<String> errors;
			FSCompletenessCensusSummary summary;
			CHECK_EQ(load_staged_census(staged_root, summary, errors), ERR_INVALID_DATA);
			CHECK_MESSAGE(census_errors_mention(errors, "has no child slot 'a_slot_no_representation_declares'"),
					String(" | ").join(errors));
		}
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
			if (!FileAccess::exists(type_census_root().path_join("rules").path_join(family + ".json"))) {
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
					FSCompletenessCoverageWitness declared_witness;
					declared_witness.kind = kind;
					declared_witness.reference = reference;
					declared_witness.build_configuration = census_string(witness, "build_configuration");
					if (FSCompletenessCensus::witness_needs_other_configuration(declared_witness)) {
						// The witness names a build this one is not, so no case was compiled to bind and
						// this build can neither confirm the claim nor refute it.
						continue;
					}
					// A present witness is bound to what actually observes it: a tracked fixture or a
					// registered doctest case. A reference that only looks well-formed observes nothing.
					String detail;
					if (kind == "fixture" &&
							!resolve_tracked_fixture_reference(type_census_root(), reference, detail)) {
						errors.push_back(vformat("unsupported: %s: %s", id, detail));
					}
					if (kind == "doctest_case" && !resolve_doctest_case_reference(reference, detail)) {
						errors.push_back(vformat("unsupported: %s: %s", id, detail));
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
		// A deferred witness observes nothing, so an unsupported configuration backed by one is an
		// uncovered census cell wearing an exemption. Every declared configuration carries an
		// executable negative witness, and a new one may not be filed without one.
		CHECK_MESSAGE(deferred_witnesses == 0,
				"an unsupported configuration must carry an executable negative witness, not a deferred one");
		CHECK_MESSAGE(errors.is_empty(), String(" | ").join(errors));
	}

	TEST_CASE("TypeCompleteness Census production paths reconcile with the capability map") {
		Vector<String> errors;
		Variant representations_data;
		Variant reconciliation_data;
		Variant capabilities_data;
		REQUIRE_MESSAGE(census_read_json("representations.json", representations_data, errors), String(" | ").join(errors));
		REQUIRE_MESSAGE(census_read_json("capability_reconciliation.json", reconciliation_data, errors), String(" | ").join(errors));
		REQUIRE(read_completeness_json(type_census_root().path_join("capabilities.json"), capabilities_data, errors));
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

		// Catalog data paths the capability map selects a family for are inputs of the matrix, not
		// product code, so they are recorded as catalog paths and are never census production paths.
		for (const Dictionary &record : census_dictionary_array(reconciliation_data, "mapped_catalog_paths")) {
			const String path = census_string(record, "path");
			declared_paths.insert(path);
			if (!capability_paths.has(path)) {
				errors.push_back(vformat("reconciliation: catalog path '%s' is declared mapped but the capability map does not list it", path));
			}
			if (census_paths.has(path)) {
				errors.push_back(vformat("reconciliation: '%s' is a census production path and may not be recorded as a catalog path", path));
			}
			if (census_string(record, "rationale").is_empty()) {
				errors.push_back(vformat("reconciliation: catalog path '%s' has no rationale", path));
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

	TEST_CASE("TypeCompleteness Census coverage is exactly one entry per operative policy pair") {
		Vector<String> errors;
		FSCompletenessCensusSummary summary;
		REQUIRE_EQ(FSCompletenessCensus::load(type_census_root(), summary, errors), OK);
		CHECK_MESSAGE(errors.is_empty(), String(" | ").join(errors));

		Variant policies_data;
		REQUIRE_MESSAGE(census_read_json("policies.json", policies_data, errors), String(" | ").join(errors));
		HashSet<String> operative_pairs;
		for (const Dictionary &entry : census_dictionary_array(policies_data, "entries")) {
			if (census_string(entry, "policy") == "not_applicable") {
				continue;
			}
			operative_pairs.insert(census_string(entry, "representation") + "::" +
					census_string(entry, "child_slot") + "::" + census_string(entry, "surface"));
		}
		REQUIRE(operative_pairs.size() > 0);

		// Both directions: no operative pair without an entry, and no entry without an operative pair.
		HashSet<String> covered_pairs;
		for (const FSCompletenessCoverageEntry &entry : summary.entries) {
			CHECK_MESSAGE(operative_pairs.has(entry.key()), vformat("%s is not an operative policy pair", entry.key()));
			covered_pairs.insert(entry.key());
		}
		for (const String &pair : operative_pairs) {
			CHECK_MESSAGE(covered_pairs.has(pair), vformat("%s has no coverage entry", pair));
		}
		CHECK_EQ(summary.entries.size(), operative_pairs.size());
		CHECK_EQ(summary.total(), summary.entries.size());

		for (const FSCompletenessCoverageEntry &entry : summary.entries) {
			CAPTURE(entry.key());
			if (entry.status == "uncovered" || entry.status == "quality_deferred") {
				CHECK_FALSE(entry.issue_url.is_empty());
			}
		}
	}

	TEST_CASE("TypeCompleteness Census every witness the census claims resolves") {
		Vector<String> errors;
		FSCompletenessCensusSummary summary;
		REQUIRE_EQ(FSCompletenessCensus::load(type_census_root(), summary, errors), OK);
		CHECK(summary.covered > 0);
		// An exemption stands on an executable negative witness, so those are claims too and resolve
		// here alongside the coverage witnesses.
		CHECK(summary.unsupported_witnesses.size() > 0);

		const Vector<String> unresolved =
				FSCompletenessCensus::unresolved_witnesses(type_census_root(), summary);
		CHECK_MESSAGE(unresolved.is_empty(), String(" | ").join(unresolved));

		// Every witness kind the census can express is exercised, so a resolution path cannot rot
		// unobserved behind a census that only ever names one kind.
		HashSet<String> witness_kinds;
		for (const FSCompletenessCoverageEntry &entry : summary.entries) {
			if (entry.status == "covered") {
				witness_kinds.insert(entry.witness.kind);
			}
		}
		CHECK(witness_kinds.has("doctest_case"));
		CHECK(witness_kinds.has("fixture"));
		CHECK(witness_kinds.has("family_case"));
	}

	TEST_CASE("TypeCompleteness Census reports a witness of another build configuration as unconfirmed") {
		Vector<String> errors;
		FSCompletenessCensusSummary summary;
		REQUIRE_EQ(FSCompletenessCensus::load(type_census_root(), summary, errors), OK);

		const Vector<String> unresolved =
				FSCompletenessCensus::unresolved_witnesses(type_census_root(), summary);
		const Vector<String> unconfirmable =
				FSCompletenessCensus::unconfirmable_witnesses(type_census_root(), summary);
		CHECK_MESSAGE(unresolved.is_empty(), String(" | ").join(unresolved));

		// Every claim the census makes lands in exactly one bucket, so a configuration can never both
		// count a witness as evidence and report it as unconfirmed.
		int declared_for_other_configuration = 0;
		for (const FSCompletenessCoverageEntry &entry : summary.entries) {
			if (entry.status == "covered" &&
					FSCompletenessCensus::witness_needs_other_configuration(entry.witness)) {
				declared_for_other_configuration++;
			}
		}
		for (const FSCompletenessUnsupportedWitness &witness : summary.unsupported_witnesses) {
			if (FSCompletenessCensus::witness_needs_other_configuration(witness.witness)) {
				declared_for_other_configuration++;
			}
		}
		CHECK_EQ(unconfirmable.size(), declared_for_other_configuration);
		for (const String &message : unconfirmable) {
			CHECK_MESSAGE(message.contains("is only compiled in the editor configuration"), message);
		}
#ifdef TOOLS_ENABLED
		// This is the editor configuration, so every witness the census declares is compiled here and
		// had to bind for the run above to report nothing unresolved.
		CHECK(unconfirmable.is_empty());
#else
		// The census declares editor-only witnesses, so a template build has to report them rather than
		// counting a case it never compiled as evidence.
		CHECK_FALSE(unconfirmable.is_empty());
#endif
	}

	TEST_CASE("TypeCompleteness Census refuses a witness build configuration it does not implement") {
		TemporaryProjectTree tree(vformat("type_completeness_census_configuration_%d",
				OS::get_singleton()->get_process_id()));
		REQUIRE(tree.is_valid());
		const String staged_root = stage_census_catalog(tree);

		SUBCASE("a configuration outside the vocabulary is a refusal") {
			Dictionary document = tracked_coverage_document();
			Array entries = document["entries"];
			const int index = first_coverage_entry_with_witness_kind(entries, "doctest_case");
			REQUIRE(index >= 0);
			Dictionary entry = entries[index];
			Dictionary witness = entry["witness"];
			witness["build_configuration"] = "headless";
			entry["witness"] = witness;
			entries[index] = entry;
			document["entries"] = entries;
			write_staged_coverage(tree, document);

			Vector<String> errors;
			FSCompletenessCensusSummary summary;
			CHECK_EQ(load_staged_census(staged_root, summary, errors), ERR_INVALID_DATA);
			CHECK_MESSAGE(census_errors_mention(errors, "is outside the build-configuration vocabulary"),
					String(" | ").join(errors));
		}

		SUBCASE("only a compiled case can be configuration-dependent") {
			Dictionary document = tracked_coverage_document();
			Array entries = document["entries"];
			const int index = first_coverage_entry_with_witness_kind(entries, "fixture");
			REQUIRE(index >= 0);
			Dictionary entry = entries[index];
			Dictionary witness = entry["witness"];
			witness["build_configuration"] = "editor";
			entry["witness"] = witness;
			entries[index] = entry;
			document["entries"] = entries;
			write_staged_coverage(tree, document);

			Vector<String> errors;
			FSCompletenessCensusSummary summary;
			CHECK_EQ(load_staged_census(staged_root, summary, errors), ERR_INVALID_DATA);
			CHECK_MESSAGE(
					census_errors_mention(errors, "only a doctest_case witness is configuration-dependent"),
					String(" | ").join(errors));
		}

		SUBCASE("a broken witness of another configuration is unconfirmed, never resolved") {
			Dictionary document = tracked_coverage_document();
			Array entries = document["entries"];
			int index = -1;
			for (int entry_index = 0; entry_index < entries.size(); entry_index++) {
				const Dictionary candidate = entries[entry_index];
				const Variant &candidate_witness = candidate.get("witness", Variant());
				if (candidate_witness.get_type() == Variant::DICTIONARY &&
						census_string(candidate_witness, "build_configuration") == "editor") {
					index = entry_index;
					break;
				}
			}
			REQUIRE_MESSAGE(index >= 0, "the census declares no editor-only witness");
			Dictionary entry = entries[index];
			Dictionary witness = entry["witness"];
			witness["reference"] = "[Modules][FoundryScript][TypeUnion] No case is named this";
			entry["witness"] = witness;
			entries[index] = entry;
			document["entries"] = entries;
			write_staged_coverage(tree, document);

			Vector<String> errors;
			FSCompletenessCensusSummary summary;
			REQUIRE_EQ(load_staged_census(staged_root, summary, errors), OK);
			const Vector<String> unresolved =
					FSCompletenessCensus::unresolved_witnesses(staged_root, summary);
			const Vector<String> unconfirmable =
					FSCompletenessCensus::unconfirmable_witnesses(staged_root, summary);
#ifdef TOOLS_ENABLED
			// The editor configuration compiles the case the witness names, so a reference nothing
			// registers is the defect it always was.
			REQUIRE_EQ(unresolved.size(), 1);
			CHECK_MESSAGE(unresolved[0].contains("no registered doctest case"), unresolved[0]);
			CHECK(unconfirmable.is_empty());
#else
			// A build that compiled no case cannot tell a renamed witness from an absent one, so it
			// reports the claim as unconfirmed instead of inventing a verdict.
			CHECK_MESSAGE(unresolved.is_empty(), String(" | ").join(unresolved));
			REQUIRE_FALSE(unconfirmable.is_empty());
#endif
		}
	}

	TEST_CASE("TypeCompleteness Census refuses an exemption whose negative witness observes nothing") {
		TemporaryProjectTree tree(vformat("type_completeness_census_exempt_%d", OS::get_singleton()->get_process_id()));
		REQUIRE(tree.is_valid());
		const String staged_root = stage_census_catalog(tree);

		Vector<String> read_errors;
		Variant unsupported_data;
		REQUIRE_MESSAGE(census_read_json("unsupported.json", unsupported_data, read_errors),
				String(" | ").join(read_errors));
		Dictionary document = Dictionary(unsupported_data).duplicate(true);
		Array entries = document["entries"];
		Dictionary entry = entries[0];
		Array witnesses = entry["witnesses"];
		Dictionary witness = witnesses[0];
		witness["kind"] = "fixture";
		witness["reference"] = "modules/foundry_script/tests/scripts/analyzer/errors/not_a_fixture.fs";
		witnesses[0] = witness;
		entry["witnesses"] = witnesses;
		entries[0] = entry;
		document["entries"] = entries;
		tree.write_file("catalog/census/unsupported.json", JSON::stringify(document, "\t") + "\n");

		Vector<String> errors;
		FSCompletenessCensusSummary summary;
		REQUIRE_EQ(load_staged_census(staged_root, summary, errors), OK);
		const Vector<String> unresolved = FSCompletenessCensus::unresolved_witnesses(staged_root, summary);
		REQUIRE_EQ(unresolved.size(), 1);
		CHECK_MESSAGE(unresolved[0].begins_with(vformat("unsupported '%s'", String(entry["id"]))), unresolved[0]);
		CHECK_MESSAGE(unresolved[0].contains("does not exist"), unresolved[0]);
	}

	TEST_CASE("TypeCompleteness Census names a broken witness of every kind") {
		TemporaryProjectTree tree(vformat("type_completeness_census_witness_%d", OS::get_singleton()->get_process_id()));
		REQUIRE(tree.is_valid());
		const String staged_root = stage_census_catalog(tree);

		struct BrokenWitness {
			const char *witness_kind;
			const char *member;
			const char *value;
			const char *expected_fragment;
		};
		const BrokenWitness broken_witnesses[] = {
			{ "doctest_case", "reference", "[Modules][FoundryScript][TypeUnion] No case is named this",
					"no registered doctest case" },
			{ "fixture", "reference", "modules/foundry_script/tests/scripts/analyzer/features/not_a_fixture.fs",
					"does not exist" },
			{ "family_case", "family", "not_a_registered_family", "does not load" },
		};
		for (const BrokenWitness &broken : broken_witnesses) {
			CAPTURE(broken.witness_kind);
			Dictionary document = tracked_coverage_document();
			Array entries = document["entries"];
			const int index = first_coverage_entry_with_witness_kind(entries, broken.witness_kind);
			REQUIRE_MESSAGE(index >= 0, vformat("the census declares no %s witness", broken.witness_kind));
			Dictionary entry = entries[index];
			Dictionary witness = entry["witness"];
			witness[broken.member] = String(broken.value);
			entry["witness"] = witness;
			entries[index] = entry;
			document["entries"] = entries;
			write_staged_coverage(tree, document);

			Vector<String> errors;
			FSCompletenessCensusSummary summary;
			REQUIRE_EQ(load_staged_census(staged_root, summary, errors), OK);
			const Vector<String> unresolved =
					FSCompletenessCensus::unresolved_witnesses(staged_root, summary);
			REQUIRE_EQ(unresolved.size(), 1);
			CHECK(unresolved[0].begins_with(String(entry["representation"]) + "::"));
			CHECK_MESSAGE(unresolved[0].contains(broken.expected_fragment), unresolved[0]);
		}

		// Coordinates that name no cell of the family observe nothing, even though the family loads.
		{
			Dictionary document = tracked_coverage_document();
			Array entries = document["entries"];
			const int index = first_coverage_entry_with_witness_kind(entries, "family_case");
			REQUIRE(index >= 0);
			Dictionary entry = entries[index];
			Dictionary witness = entry["witness"];
			Dictionary coordinates = witness["coordinates"];
			coordinates["destination"] = "not_a_declared_leaf";
			witness["coordinates"] = coordinates;
			entry["witness"] = witness;
			entries[index] = entry;
			document["entries"] = entries;
			write_staged_coverage(tree, document);

			Vector<String> errors;
			FSCompletenessCensusSummary summary;
			REQUIRE_EQ(load_staged_census(staged_root, summary, errors), OK);
			const Vector<String> unresolved =
					FSCompletenessCensus::unresolved_witnesses(staged_root, summary);
			REQUIRE_EQ(unresolved.size(), 1);
			CHECK_MESSAGE(unresolved[0].contains("resolve to 0 cells"), unresolved[0]);
		}

		// A fixture reference that is not a repository-relative path is refused before the repository is
		// consulted, so a witness can never be resolved against a path outside the tree.
		{
			Dictionary document = tracked_coverage_document();
			Array entries = document["entries"];
			const int index = first_coverage_entry_with_witness_kind(entries, "fixture");
			REQUIRE(index >= 0);
			Dictionary entry = entries[index];
			Dictionary witness = entry["witness"];
			witness["reference"] = "modules/foundry_script/tests/../../../etc/hosts";
			entry["witness"] = witness;
			entries[index] = entry;
			document["entries"] = entries;
			write_staged_coverage(tree, document);

			Vector<String> errors;
			FSCompletenessCensusSummary summary;
			REQUIRE_EQ(load_staged_census(staged_root, summary, errors), OK);
			const Vector<String> unresolved =
					FSCompletenessCensus::unresolved_witnesses(staged_root, summary);
			REQUIRE_EQ(unresolved.size(), 1);
			CHECK_MESSAGE(unresolved[0].contains("is not a repository-relative path"), unresolved[0]);
		}

		// A cell that claims coverage and declares nothing at all is uncovered, not covered.
		Dictionary document = tracked_coverage_document();
		Array entries = document["entries"];
		const int index = first_coverage_entry_with_witness_kind(entries, "doctest_case");
		REQUIRE(index >= 0);
		Dictionary entry = entries[index];
		entry.erase("witness");
		entries[index] = entry;
		document["entries"] = entries;
		write_staged_coverage(tree, document);
		Vector<String> errors;
		FSCompletenessCensusSummary summary;
		REQUIRE_EQ(load_staged_census(staged_root, summary, errors), OK);
		const Vector<String> unresolved =
				FSCompletenessCensus::unresolved_witnesses(staged_root, summary);
		REQUIRE_EQ(unresolved.size(), 1);
		CHECK_MESSAGE(unresolved[0].contains("declares no witness"), unresolved[0]);
	}

	TEST_CASE("TypeCompleteness Census refuses a malformed coverage document") {
		TemporaryProjectTree tree(vformat("type_completeness_census_refusal_%d", OS::get_singleton()->get_process_id()));
		REQUIRE(tree.is_valid());
		const String staged_root = stage_census_catalog(tree);

		SUBCASE("an entry for a not_applicable pair is an orphan") {
			Dictionary document = tracked_coverage_document();
			Array entries = document["entries"];
			Dictionary orphan;
			orphan["representation"] = "parser_data_type";
			orphan["child_slot"] = "union_members";
			orphan["surface"] = "runtime_bytecode";
			orphan["status"] = "uncovered";
			orphan["issue_url"] = "https://example.invalid/issues/1";
			entries.push_back(orphan);
			document["entries"] = entries;
			write_staged_coverage(tree, document);

			Vector<String> errors;
			FSCompletenessCensusSummary summary;
			CHECK_EQ(load_staged_census(staged_root, summary, errors), ERR_INVALID_DATA);
			CHECK(summary.entries.is_empty());
			CHECK_MESSAGE(census_errors_mention(errors,
								  "parser_data_type::union_members::runtime_bytecode is declared not_applicable"),
					String(" | ").join(errors));
		}

		SUBCASE("a duplicate entry is refused before any witness is resolved") {
			Dictionary document = tracked_coverage_document();
			Array entries = document["entries"];
			entries.push_back(Dictionary(entries[0]).duplicate(true));
			document["entries"] = entries;
			write_staged_coverage(tree, document);

			Vector<String> errors;
			FSCompletenessCensusSummary summary;
			CHECK_EQ(load_staged_census(staged_root, summary, errors), ERR_INVALID_DATA);
			CHECK_MESSAGE(census_errors_mention(errors, "duplicate coverage entry"), String(" | ").join(errors));
		}

		SUBCASE("a missing operative pair is named") {
			Dictionary document = tracked_coverage_document();
			Array entries = document["entries"];
			const Dictionary removed = entries[0];
			entries.remove_at(0);
			document["entries"] = entries;
			write_staged_coverage(tree, document);

			Vector<String> errors;
			FSCompletenessCensusSummary summary;
			CHECK_EQ(load_staged_census(staged_root, summary, errors), ERR_INVALID_DATA);
			CHECK_MESSAGE(census_errors_mention(errors, String(removed["representation"]) + "::" + String(removed["child_slot"]) + "::" + String(removed["surface"])),
					String(" | ").join(errors));
		}

		SUBCASE("a status outside the vocabulary is named with its JSONPath") {
			Dictionary document = tracked_coverage_document();
			Array entries = document["entries"];
			Dictionary entry = entries[0];
			entry["status"] = "probably_fine";
			entries[0] = entry;
			document["entries"] = entries;
			write_staged_coverage(tree, document);

			Vector<String> errors;
			FSCompletenessCensusSummary summary;
			CHECK_EQ(load_staged_census(staged_root, summary, errors), ERR_INVALID_DATA);
			CHECK_MESSAGE(census_errors_mention(errors, "$.entries[0].status"), String(" | ").join(errors));
		}

		SUBCASE("an unknown member is a defect rather than an ignored declaration") {
			Dictionary document = tracked_coverage_document();
			Array entries = document["entries"];
			Dictionary entry = entries[0];
			entry["rationale"] = "not a member of the coverage schema";
			entries[0] = entry;
			document["entries"] = entries;
			write_staged_coverage(tree, document);

			Vector<String> errors;
			FSCompletenessCensusSummary summary;
			CHECK_EQ(load_staged_census(staged_root, summary, errors), ERR_INVALID_DATA);
			CHECK_MESSAGE(census_errors_mention(errors, "unknown member 'rationale'"), String(" | ").join(errors));
		}

		SUBCASE("an unsupported cell with no present negative witness is refused") {
			// runtime_data_type declares no unsupported configuration, so an exemption filed against it
			// has no executable negative witness to stand on.
			Dictionary document = tracked_coverage_document();
			Array entries = document["entries"];
			int unwitnessed_index = -1;
			for (int index = 0; index < entries.size(); index++) {
				if (String(Dictionary(entries[index]).get("representation", String())) == "runtime_data_type") {
					unwitnessed_index = index;
					break;
				}
			}
			REQUIRE(unwitnessed_index >= 0);
			Dictionary entry = entries[unwitnessed_index];
			entry["status"] = "unsupported";
			entry.erase("issue_url");
			entries[unwitnessed_index] = entry;
			document["entries"] = entries;
			write_staged_coverage(tree, document);

			Vector<String> errors;
			FSCompletenessCensusSummary summary;
			CHECK_EQ(load_staged_census(staged_root, summary, errors), ERR_INVALID_DATA);
			CHECK_MESSAGE(census_errors_mention(errors, "carries a present witness"), String(" | ").join(errors));
		}

		SUBCASE("an uncovered cell without an owning issue is refused") {
			Dictionary document = tracked_coverage_document();
			Array entries = document["entries"];
			int uncovered_index = -1;
			for (int index = 0; index < entries.size(); index++) {
				if (String(Dictionary(entries[index]).get("status", String())) == "uncovered") {
					uncovered_index = index;
					break;
				}
			}
			REQUIRE(uncovered_index >= 0);
			Dictionary entry = entries[uncovered_index];
			entry.erase("issue_url");
			entries[uncovered_index] = entry;
			document["entries"] = entries;
			write_staged_coverage(tree, document);

			Vector<String> errors;
			FSCompletenessCensusSummary summary;
			CHECK_EQ(load_staged_census(staged_root, summary, errors), ERR_INVALID_DATA);
			CHECK_MESSAGE(census_errors_mention(errors, "names no owning issue"), String(" | ").join(errors));
		}

		SUBCASE("a malformed exemption list is refused rather than read as no exemption") {
			// unsupported.json decides what coverage.json may exempt. Reading a malformed one as an
			// empty list would relax that rule silently on a census that exempts nothing today.
			tree.write_file("catalog/census/unsupported.json",
					"{ \"schema_version\": 1, \"entries\": \"not an array\" }\n");
			Vector<String> errors;
			FSCompletenessCensusSummary summary;
			CHECK_EQ(load_staged_census(staged_root, summary, errors), ERR_INVALID_DATA);
			CHECK_MESSAGE(census_errors_mention(errors, "unsupported.json:$.entries: must be an array"),
					String(" | ").join(errors));
			CHECK(summary.entries.is_empty());
		}

		SUBCASE("an exemption without an executable negative witness is refused") {
			Vector<String> read_errors;
			Variant unsupported_data;
			REQUIRE_MESSAGE(census_read_json("unsupported.json", unsupported_data, read_errors),
					String(" | ").join(read_errors));
			Dictionary document = Dictionary(unsupported_data).duplicate(true);
			Array entries = document["entries"];
			Dictionary entry = entries[0];
			entry["witnesses"] = Array();
			entries[0] = entry;
			document["entries"] = entries;
			tree.write_file("catalog/census/unsupported.json", JSON::stringify(document, "\t") + "\n");

			Vector<String> errors;
			FSCompletenessCensusSummary summary;
			CHECK_EQ(load_staged_census(staged_root, summary, errors), ERR_INVALID_DATA);
			CHECK_MESSAGE(census_errors_mention(errors, "carries no witness"), String(" | ").join(errors));
		}

		SUBCASE("a malformed policy matrix is refused rather than read as a shorter matrix") {
			// A skipped policy entry would shrink the set of pairs coverage.json has to carry, so the
			// document that decides completeness may not be read permissively.
			tree.write_file("catalog/census/policies.json",
					"{ \"schema_version\": 1, \"entries\": [ \"not an object\" ] }\n");
			Vector<String> errors;
			FSCompletenessCensusSummary summary;
			CHECK_EQ(load_staged_census(staged_root, summary, errors), ERR_INVALID_DATA);
			CHECK_MESSAGE(census_errors_mention(errors, "policies.json:$.entries[0]: must be an object"),
					String(" | ").join(errors));
		}

		SUBCASE("a malformed document is refused rather than read as an absent census") {
			tree.write_file("catalog/census/coverage.json", "{ \"schema_version\": 1, \"entries\": [ }\n");
			Vector<String> errors;
			FSCompletenessCensusSummary summary;
			CHECK_EQ(load_staged_census(staged_root, summary, errors), ERR_INVALID_DATA);
			CHECK_FALSE(errors.is_empty());
			CHECK(summary.entries.is_empty());
			CHECK_EQ(summary.total(), 0);
		}
	}

	TEST_CASE("TypeCompleteness Census closes every vocabulary against what the loader implements") {
		// A vocabulary is closed by the code that implements it. A term declared in schema.json that no
		// branch handles would be accepted and then validated by nothing - a status with no rules, a
		// witness kind with no resolution - so the schema and the loader are checked against each other
		// in both directions before any document is read against them.
		TemporaryProjectTree tree(vformat("type_completeness_census_vocabulary_%d", OS::get_singleton()->get_process_id()));
		REQUIRE(tree.is_valid());
		const String staged_root = stage_census_catalog(tree);

		const auto staged_schema = [&]() {
			Vector<String> read_errors;
			Variant data;
			REQUIRE_MESSAGE(census_read_json("schema.json", data, read_errors), String(" | ").join(read_errors));
			return Dictionary(data).duplicate(true);
		};
		const auto write_schema = [&](const Dictionary &p_document) {
			tree.write_file("catalog/census/schema.json", JSON::stringify(p_document, "\t") + "\n");
		};
		const auto refuses = [&](const String &p_fragment) {
			Vector<String> errors;
			FSCompletenessCensusSummary summary;
			CHECK_EQ(load_staged_census(staged_root, summary, errors), ERR_INVALID_DATA);
			CHECK_MESSAGE(census_errors_mention(errors, p_fragment), String(" | ").join(errors));
			CHECK(summary.entries.is_empty());
		};

		SUBCASE("a status the loader does not implement may not be declared") {
			Dictionary schema = staged_schema();
			Array statuses = schema["coverage_statuses"];
			statuses.push_back("provisionally_covered");
			schema["coverage_statuses"] = statuses;
			write_schema(schema);
			refuses("declares term 'provisionally_covered', which the census loader does not implement");
		}

		SUBCASE("an entry may not use a status the loader does not implement") {
			// Declaring the term is what a drifting schema would do first; the entry must be refused on
			// its own terms too, so the implemented set gates the document rather than the schema list.
			Dictionary schema = staged_schema();
			Array statuses = schema["coverage_statuses"];
			statuses.push_back("provisionally_covered");
			schema["coverage_statuses"] = statuses;
			write_schema(schema);

			Dictionary coverage = tracked_coverage_document();
			Array entries = coverage["entries"];
			Dictionary entry = entries[0];
			entry["status"] = "provisionally_covered";
			entries[0] = entry;
			coverage["entries"] = entries;
			write_staged_coverage(tree, coverage);

			Vector<String> errors;
			FSCompletenessCensusSummary summary;
			CHECK_EQ(load_staged_census(staged_root, summary, errors), ERR_INVALID_DATA);
			CHECK_MESSAGE(census_errors_mention(errors,
								  "$.entries[0].status: 'provisionally_covered' is outside the coverage-status vocabulary"),
					String(" | ").join(errors));
			CHECK(summary.entries.is_empty());
		}

		SUBCASE("a status the loader implements may not be dropped from the schema") {
			Dictionary schema = staged_schema();
			Array statuses = schema["coverage_statuses"];
			int quality_deferred_index = -1;
			for (int index = 0; index < statuses.size(); index++) {
				if (String(statuses[index]) == "quality_deferred") {
					quality_deferred_index = index;
					break;
				}
			}
			REQUIRE(quality_deferred_index >= 0);
			statuses.remove_at(quality_deferred_index);
			schema["coverage_statuses"] = statuses;
			write_schema(schema);
			refuses("does not declare implemented term 'quality_deferred'");
		}

		SUBCASE("a witness kind the loader cannot resolve may not be declared") {
			Dictionary schema = staged_schema();
			Array kinds = schema["witness_kinds"];
			kinds.push_back("screenshot");
			schema["witness_kinds"] = kinds;
			write_schema(schema);
			refuses("declares term 'screenshot', which the census loader does not implement");
		}

		SUBCASE("the coverage-only witness kind may not be declared as a negative-witness kind") {
			// `family_case` observes a matrix cell, which is never executable negative evidence, so the
			// shared vocabulary may not admit it even though the loader implements the kind.
			Dictionary schema = staged_schema();
			Array kinds = schema["witness_kinds"];
			kinds.push_back("family_case");
			schema["witness_kinds"] = kinds;
			write_schema(schema);
			refuses("declares term 'family_case', which the census loader does not implement");
		}

		SUBCASE("a policy the loader implements may not be dropped from the schema") {
			Dictionary schema = staged_schema();
			Array policies = schema["policies"];
			int erase_index = -1;
			for (int index = 0; index < policies.size(); index++) {
				if (String(policies[index]) == "erase") {
					erase_index = index;
					break;
				}
			}
			REQUIRE(erase_index >= 0);
			policies.remove_at(erase_index);
			schema["policies"] = policies;
			write_schema(schema);
			refuses("does not declare implemented term 'erase'");
		}

		SUBCASE("a transition kind the loader does not implement may not be declared") {
			Dictionary schema = staged_schema();
			Array kinds = schema["transition_kinds"];
			kinds.push_back("teleport");
			schema["transition_kinds"] = kinds;
			write_schema(schema);
			refuses("declares term 'teleport', which the census loader does not implement");
		}

		SUBCASE("a witness status the loader does not implement may not be declared") {
			Dictionary schema = staged_schema();
			Array statuses = schema["witness_statuses"];
			statuses.push_back("planned");
			schema["witness_statuses"] = statuses;
			write_schema(schema);
			refuses("declares term 'planned', which the census loader does not implement");
		}
	}

	TEST_CASE("TypeCompleteness Census refuses a duplicate identity in every census table") {
		// Every census document is an identity-keyed table, and every one of them is loaded into a map.
		// A repeated key replaces the entry that came before it, so a document can lose a whole
		// representation, slot, policy, exemption, crossing, or coverage cell and still load. Each table
		// is proven separately because each one is keyed and inserted separately.
		TemporaryProjectTree tree(vformat("type_completeness_census_duplicate_%d", OS::get_singleton()->get_process_id()));
		REQUIRE(tree.is_valid());
		const String staged_root = stage_census_catalog(tree);

		const auto staged_document = [&](const String &p_file_name) {
			Vector<String> read_errors;
			Variant data;
			REQUIRE_MESSAGE(census_read_json(p_file_name, data, read_errors), String(" | ").join(read_errors));
			return Dictionary(data).duplicate(true);
		};
		const auto write_staged = [&](const String &p_file_name, const Dictionary &p_document) {
			tree.write_file(String("catalog/census/").path_join(p_file_name), JSON::stringify(p_document, "\t") + "\n");
		};
		const auto refuses = [&](const String &p_fragment) {
			Vector<String> errors;
			FSCompletenessCensusSummary summary;
			CHECK_EQ(load_staged_census(staged_root, summary, errors), ERR_INVALID_DATA);
			CHECK_MESSAGE(census_errors_mention(errors, p_fragment), String(" | ").join(errors));
			CHECK(summary.entries.is_empty());
		};

		SUBCASE("a duplicate representation id may not replace an inventoried representation") {
			Dictionary document = staged_document("representations.json");
			Array inventory = document["representations"];
			Dictionary duplicate = Dictionary(inventory[1]).duplicate(true);
			duplicate["id"] = String(Dictionary(inventory[0])["id"]);
			inventory.push_back(duplicate);
			document["representations"] = inventory;
			write_staged("representations.json", document);
			refuses("duplicate representation id");
		}

		SUBCASE("a duplicate child slot may not replace a slot of the same representation") {
			Dictionary document = staged_document("representations.json");
			Array inventory = document["representations"];
			Dictionary representation = inventory[0];
			Array slots = representation["child_slots"];
			slots.push_back(Dictionary(slots[0]).duplicate(true));
			representation["child_slots"] = slots;
			inventory[0] = representation;
			document["representations"] = inventory;
			write_staged("representations.json", document);
			refuses("duplicate child slot");
		}

		SUBCASE("a duplicate policy pair is refused where it is declared") {
			Dictionary document = staged_document("policies.json");
			Array entries = document["entries"];
			entries.push_back(Dictionary(entries[0]).duplicate(true));
			document["entries"] = entries;
			write_staged("policies.json", document);
			refuses("duplicate policy for");
		}

		SUBCASE("a duplicate unsupported entry id is refused") {
			Dictionary document = staged_document("unsupported.json");
			Array entries = document["entries"];
			entries.push_back(Dictionary(entries[0]).duplicate(true));
			document["entries"] = entries;
			write_staged("unsupported.json", document);
			refuses("duplicate unsupported entry id");
		}

		SUBCASE("a duplicate transition id is refused") {
			Dictionary document = staged_document("transitions.json");
			Array transitions = document["transitions"];
			transitions.push_back(Dictionary(transitions[0]).duplicate(true));
			document["transitions"] = transitions;
			write_staged("transitions.json", document);
			refuses("duplicate transition id");
		}

		SUBCASE("a duplicate coverage pair is refused before any witness is resolved") {
			Dictionary document = staged_document("coverage.json");
			Array entries = document["entries"];
			entries.push_back(Dictionary(entries[0]).duplicate(true));
			document["entries"] = entries;
			write_staged("coverage.json", document);
			refuses("duplicate coverage entry for");
		}
	}

	TEST_CASE("TypeCompleteness Census summary report carries every entry as report numbers") {
		Vector<String> errors;
		FSCompletenessCensusSummary summary;
		REQUIRE_EQ(FSCompletenessCensus::load(type_census_root(), summary, errors), OK);

		const Dictionary report = FSCompletenessCensus::summary_report(summary);
		for (const String &member : FSCompletenessCensus::summary_count_members()) {
			CAPTURE(member);
			REQUIRE(report.has(member));
			CHECK_EQ(Variant(report[member]).get_type(), Variant::FLOAT);
		}
		CHECK_EQ(int(double(report["covered"])), summary.covered);
		CHECK_EQ(int(double(report["uncovered"])), summary.uncovered);
		CHECK_EQ(int(double(report["unsupported"])), summary.unsupported);
		CHECK_EQ(int(double(report["quality_deferred"])), summary.quality_deferred);
		const Array entries = report["entries"];
		REQUIRE_EQ(entries.size(), summary.entries.size());
		CHECK_EQ(int(double(report["covered"])) + int(double(report["uncovered"])) +
						int(double(report["unsupported"])) + int(double(report["quality_deferred"])),
				entries.size());

		// The report is published evidence, so a second read of the same census must produce the same
		// document; entries are ordered by their policy pair rather than by file order.
		FSCompletenessCensusSummary repeated_summary;
		Vector<String> repeated_errors;
		REQUIRE_EQ(FSCompletenessCensus::load(type_census_root(), repeated_summary, repeated_errors), OK);
		CHECK_EQ(FSCompletenessCensus::summary_report(repeated_summary), report);
		String previous_key;
		for (const FSCompletenessCoverageEntry &entry : summary.entries) {
			CHECK(previous_key < entry.key());
			previous_key = entry.key();
		}
	}

	TEST_CASE("TypeCompleteness Census a run refuses to publish a census it cannot confirm") {
		TemporaryProjectTree tree(vformat("type_completeness_census_run_%d", OS::get_singleton()->get_process_id()));
		REQUIRE(tree.is_valid());
		const String staged_root = stage_census_catalog(tree);
		const String scratch_root = tree.root.path_join("scratch");
		REQUIRE_EQ(DirAccess::make_dir_recursive_absolute(scratch_root), OK);

		FSCompletenessRunOptions options;
		options.catalog_root = staged_root;
		options.family = "union_destination_membership";
		options.scratch_root = scratch_root;
		options.report_path = scratch_root.path_join("report.json");

		SUBCASE("a covered cell whose witness does not resolve is a structural failure") {
			Dictionary document = tracked_coverage_document();
			Array entries = document["entries"];
			const int index = first_coverage_entry_with_witness_kind(entries, "doctest_case");
			REQUIRE(index >= 0);
			Dictionary entry = entries[index];
			Dictionary witness = entry["witness"];
			witness["reference"] = "[Modules][FoundryScript][TypeUnion] No case is named this";
			entry["witness"] = witness;
			entries[index] = entry;
			document["entries"] = entries;
			write_staged_coverage(tree, document);

			FSCompletenessRunResult result;
			CHECK_EQ(FSCompletenessRunner::run(options, result), ERR_INVALID_DATA);
			CHECK_FALSE(result.success);
			CHECK_EQ(result.outcome, "structural_failure");
			REQUIRE_EQ(result.structural_failures.size(), 1);
			CHECK_EQ(result.structural_failures[0].stage, FSCompletenessStructuralStage::CENSUS_WITNESS_UNRESOLVED);
			CHECK_MESSAGE(result.structural_failures[0].detail.contains("no registered doctest case"),
					result.structural_failures[0].detail);

			// The evidence stays in the document: the run publishes what the census says and refuses the
			// verdict, rather than publishing a report with no census in it.
			const Dictionary census = result.report["census"];
			CHECK_EQ(int(double(census["covered"])) + int(double(census["uncovered"])) +
							int(double(census["unsupported"])) + int(double(census["quality_deferred"])),
					Array(census["entries"]).size());
		}

		SUBCASE("a malformed census is a structural failure rather than no census") {
			tree.write_file("catalog/census/coverage.json", "not a census at all\n");

			FSCompletenessRunResult result;
			CHECK_EQ(FSCompletenessRunner::run(options, result), ERR_INVALID_DATA);
			CHECK_FALSE(result.success);
			CHECK_EQ(result.outcome, "structural_failure");
			REQUIRE_EQ(result.structural_failures.size(), 1);
			CHECK_EQ(result.structural_failures[0].stage, FSCompletenessStructuralStage::CENSUS_WITNESS_UNRESOLVED);
			CHECK_MESSAGE(result.structural_failures[0].detail.contains("could not be read"),
					result.structural_failures[0].detail);
		}
	}
}

} // namespace FSTests
