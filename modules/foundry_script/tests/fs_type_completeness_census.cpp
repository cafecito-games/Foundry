/**************************************************************************/
/*  fs_type_completeness_census.cpp                                       */
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

#include "fs_type_completeness_census.h"

#include "fs_temporary_project_tree.h"
#include "fs_type_completeness_adapter.h"
#include "fs_type_completeness_cache.h"
#include "fs_type_completeness_common.h"
#include "fs_type_completeness_json.h"

#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/templates/hash_map.h"
#include "core/templates/hash_set.h"

#include "thirdparty/doctest/doctest.h"

#include <set>

namespace doctest {
namespace detail {
// doctest declares its registry only inside its own implementation translation unit, but the
// definition has external linkage. Redeclaring it binds a census witness to the case that actually
// registered, rather than to a name nothing checks.
std::set<TestCase> &getRegisteredTests();
} // namespace detail
} // namespace doctest

namespace FSTests {

using namespace Completeness;

bool resolve_doctest_case_reference(const String &p_reference, String &r_detail) {
	// Registration happens before any test runs and nothing removes a case afterwards, so the index is
	// built once; a census with many witnesses would otherwise walk the whole registry per witness.
	static HashSet<String> registered_case_names = []() {
		HashSet<String> names;
		for (const doctest::detail::TestCase &test_case : doctest::detail::getRegisteredTests()) {
			const String suite = String::utf8(test_case.m_test_suite != nullptr ? test_case.m_test_suite : "");
			const String name = String::utf8(test_case.m_name != nullptr ? test_case.m_name : "");
			names.insert(suite.is_empty() ? name : suite + " " + name);
		}
		return names;
	}();
	if (registered_case_names.has(p_reference)) {
		return true;
	}
	r_detail = vformat("no registered doctest case is named '%s'", p_reference);
	return false;
}

bool resolve_tracked_fixture_reference(const String &p_root, const String &p_reference, String &r_detail) {
	if (p_reference.is_absolute_path() || p_reference.simplify_path() != p_reference ||
			p_reference.contains("://") || p_reference.contains("..")) {
		r_detail = vformat("fixture reference '%s' is not a repository-relative path", p_reference);
		return false;
	}
	const String repository_root = find_repository_root(p_root);
	if (repository_root.is_empty()) {
		r_detail = vformat("no repository root could be resolved for '%s'", p_reference);
		return false;
	}
	const String absolute_path = repository_root.path_join(p_reference);
	if (!FileAccess::exists(absolute_path) || DirAccess::dir_exists_absolute(absolute_path)) {
		r_detail = vformat("fixture '%s' does not exist", p_reference);
		return false;
	}
	String probe_output;
	int exit_code = -1;
	const Error probe_error = default_tracked_file_probe(repository_root, p_reference, probe_output, exit_code);
	if (probe_error == OK && exit_code == 1) {
		r_detail = vformat("fixture '%s' is not tracked", p_reference);
		return false;
	}
	return true;
}

bool read_completeness_json(const String &p_path, Variant &r_data, Vector<String> &r_errors) {
	Error read_error = OK;
	const String source = FileAccess::get_file_as_string(p_path, &read_error);
	if (read_error != OK) {
		r_errors.push_back(vformat("%s: could not be read (error %d)", p_path, read_error));
		return false;
	}
	Vector<String> parse_errors;
	const Error parse_error = parse_type_completeness_json(source, p_path, r_data, parse_errors);
	for (const String &parse_message : parse_errors) {
		r_errors.push_back(parse_message);
	}
	if (parse_error != OK || r_data.get_type() != Variant::DICTIONARY) {
		r_errors.push_back(vformat("%s: expected a JSON object", p_path));
		return false;
	}
	return true;
}

Vector<Dictionary> census_dictionary_array(const Dictionary &p_document, const String &p_key) {
	Vector<Dictionary> result;
	const Variant &value = p_document.get(p_key, Variant());
	if (value.get_type() != Variant::ARRAY) {
		return result;
	}
	const Array array = value;
	for (int index = 0; index < array.size(); index++) {
		if (array[index].get_type() == Variant::DICTIONARY) {
			result.push_back(array[index]);
		}
	}
	return result;
}

Vector<String> census_string_array(const Dictionary &p_dictionary, const String &p_key) {
	Vector<String> result;
	const Variant &value = p_dictionary.get(p_key, Variant());
	if (value.get_type() != Variant::ARRAY) {
		return result;
	}
	const Array array = value;
	for (int index = 0; index < array.size(); index++) {
		if (array[index].get_type() == Variant::STRING) {
			result.push_back(array[index]);
		}
	}
	return result;
}

String census_string(const Dictionary &p_dictionary, const String &p_key) {
	const Variant &value = p_dictionary.get(p_key, Variant());
	return value.get_type() == Variant::STRING ? String(value) : String();
}

HashSet<String> census_string_set(const Vector<String> &p_values) {
	HashSet<String> result;
	for (const String &value : p_values) {
		result.insert(value);
	}
	return result;
}

namespace {

constexpr int CENSUS_SCHEMA_VERSION = 1;
constexpr const char *FAMILY_CASE_WITNESS_KIND = "family_case";

const char *COVERAGE_ENTRY_MEMBERS[] = {
	"representation",
	"child_slot",
	"surface",
	"status",
	"witness",
	"issue_url",
};

constexpr int COVERAGE_ENTRY_MEMBER_COUNT = 6;

const char *WITNESS_MEMBERS[] = {
	"kind",
	"reference",
	"family",
	"coordinates",
};

constexpr int WITNESS_MEMBER_COUNT = 4;

// Ascending by policy-pair key, which is the order every census report is published in.
struct CoverageEntryKeyLess {
	bool operator()(const FSCompletenessCoverageEntry &p_left, const FSCompletenessCoverageEntry &p_right) const {
		return p_left.key() < p_right.key();
	}
};

bool read_census_document(const String &p_root, const String &p_file_name, Dictionary &r_document,
		Vector<String> &r_errors) {
	Variant data;
	if (!read_completeness_json(
				FSCompletenessCensus::census_directory(p_root).path_join(p_file_name), data, r_errors)) {
		return false;
	}
	r_document = data;
	return true;
}

// The vocabulary a schema member declares, as a set.
HashSet<String> schema_vocabulary(const Dictionary &p_schema, const String &p_key) {
	return census_string_set(census_string_array(p_schema, p_key));
}

String policy_pair_key(const String &p_representation, const String &p_child_slot, const String &p_surface) {
	return p_representation + "::" + p_child_slot + "::" + p_surface;
}

// Reports a member the census does not define rather than ignoring it: an unknown member is either a
// typo in a member that matters or a declaration nothing enforces, and both read as coverage.
void check_unknown_members(const Dictionary &p_object, const char *const *p_known, int p_known_count,
		const String &p_json_path, Vector<String> &r_errors) {
	for (const String &member : sorted_dictionary_keys(p_object)) {
		bool known = false;
		for (int index = 0; index < p_known_count; index++) {
			if (member == p_known[index]) {
				known = true;
				break;
			}
		}
		if (!known) {
			r_errors.push_back(vformat("%s: unknown member '%s'", p_json_path, member));
		}
	}
}

void parse_witness(const Dictionary &p_entry, const String &p_json_path,
		const HashSet<String> &p_witness_kinds, FSCompletenessCoverageWitness &r_witness,
		Vector<String> &r_errors) {
	const Variant &raw_witness = p_entry.get("witness", Variant());
	if (raw_witness.get_type() == Variant::NIL) {
		return;
	}
	if (raw_witness.get_type() != Variant::DICTIONARY) {
		r_errors.push_back(vformat("%s.witness: must be an object", p_json_path));
		return;
	}
	const Dictionary witness = raw_witness;
	const String witness_path = p_json_path + ".witness";
	check_unknown_members(witness, WITNESS_MEMBERS, WITNESS_MEMBER_COUNT, witness_path, r_errors);

	const String kind = census_string(witness, "kind");
	if (!p_witness_kinds.has(kind)) {
		r_errors.push_back(vformat("%s.kind: '%s' is outside the witness-kind vocabulary", witness_path, kind));
		return;
	}
	r_witness.kind = kind;
	if (kind == FAMILY_CASE_WITNESS_KIND) {
		r_witness.family = census_string(witness, "family");
		if (r_witness.family.is_empty()) {
			r_errors.push_back(vformat("%s.family: a family_case witness names no rule family", witness_path));
		}
		const Variant &coordinates = witness.get("coordinates", Variant());
		if (coordinates.get_type() != Variant::DICTIONARY || Dictionary(coordinates).is_empty()) {
			r_errors.push_back(vformat("%s.coordinates: a family_case witness carries no coordinates", witness_path));
		} else {
			const Dictionary declared = coordinates;
			for (const String &axis : sorted_dictionary_keys(declared)) {
				if (declared[axis].get_type() != Variant::STRING) {
					r_errors.push_back(vformat("%s.coordinates.%s: must be a concrete leaf name", witness_path, axis));
				}
			}
			r_witness.coordinates = declared;
		}
		if (witness.has("reference")) {
			r_errors.push_back(vformat("%s.reference: a family_case witness is named by coordinates, not by a reference", witness_path));
		}
		return;
	}

	r_witness.reference = census_string(witness, "reference");
	if (r_witness.reference.is_empty()) {
		r_errors.push_back(vformat("%s.reference: a %s witness carries no reference", witness_path, kind));
	}
	if (witness.has("family") || witness.has("coordinates")) {
		r_errors.push_back(vformat("%s: only a family_case witness carries a family and coordinates", witness_path));
	}
}

Dictionary witness_report(const FSCompletenessCoverageWitness &p_witness) {
	Dictionary report;
	report["kind"] = p_witness.kind;
	report["reference"] = p_witness.reference;
	report["family"] = p_witness.family;
	Dictionary coordinates;
	for (const String &axis : sorted_dictionary_keys(p_witness.coordinates)) {
		coordinates[axis] = p_witness.coordinates[axis];
	}
	report["coordinates"] = coordinates;
	return report;
}

bool resolve_family_case_witness(const String &p_root, const FSCompletenessCoverageWitness &p_witness,
		String &r_detail) {
	const String canonical_root = TemporaryProjectTree::canonicalize_existing_path(p_root);
	const String catalog_root = canonical_root.is_empty() ? p_root : canonical_root;
	const FSCompletenessCatalogRecord *record = nullptr;
	Vector<String> errors;
	if (FSCompletenessCatalogCache::get(catalog_root, p_witness.family, record, errors) != OK || record == nullptr) {
		r_detail = vformat("rule family '%s' does not load: %s", p_witness.family,
				errors.is_empty() ? String("no rules file") : String(" | ").join(errors));
		return false;
	}
	if (FSCompletenessAdapterRegistry::find(record->manifest.adapter) == nullptr) {
		r_detail = vformat("rule family '%s' names adapter '%s', which is not registered", p_witness.family,
				record->manifest.adapter);
		return false;
	}
	int matches = 0;
	record->resolution.find_cell_by_coordinates(p_witness.coordinates, matches);
	if (matches != 1) {
		r_detail = vformat("coordinates %s resolve to %d cells of family '%s'",
				Variant(p_witness.coordinates).stringify(), matches, p_witness.family);
		return false;
	}
	return true;
}

} // namespace

String FSCompletenessCoverageEntry::key() const {
	return policy_pair_key(representation, child_slot, surface);
}

int FSCompletenessCensusSummary::total() const {
	return covered + uncovered + unsupported + quality_deferred;
}

String FSCompletenessCensus::census_directory(const String &p_root) {
	return p_root.path_join("census");
}

Error FSCompletenessCensus::load(const String &p_root, FSCompletenessCensusSummary &r_summary,
		Vector<String> &r_errors) {
	r_summary = FSCompletenessCensusSummary();
	const int error_count_on_entry = r_errors.size();

	Dictionary schema;
	Dictionary policies;
	Dictionary unsupported;
	Dictionary coverage;
	if (!read_census_document(p_root, "schema.json", schema, r_errors) ||
			!read_census_document(p_root, "policies.json", policies, r_errors) ||
			!read_census_document(p_root, "unsupported.json", unsupported, r_errors) ||
			!read_census_document(p_root, "coverage.json", coverage, r_errors)) {
		return ERR_INVALID_DATA;
	}

	const String coverage_path = census_directory(p_root).path_join("coverage.json");
	int schema_version = 0;
	if (!parse_json_integer(coverage.get("schema_version", Variant()), schema_version) ||
			schema_version != CENSUS_SCHEMA_VERSION) {
		r_errors.push_back(vformat("%s:$.schema_version: expected %d", coverage_path, CENSUS_SCHEMA_VERSION));
	}

	const HashSet<String> statuses = schema_vocabulary(schema, "coverage_statuses");
	if (statuses.is_empty()) {
		r_errors.push_back("census/schema.json:$.coverage_statuses: no coverage-status vocabulary is declared");
	}
	HashSet<String> witness_kinds = schema_vocabulary(schema, "witness_kinds");
	if (witness_kinds.is_empty()) {
		r_errors.push_back("census/schema.json:$.witness_kinds: no witness-kind vocabulary is declared");
	}
	// A family case is a witness kind of the coverage document alone: unsupported.json declares only
	// executable negative witnesses, so the shared vocabulary must not admit it there.
	witness_kinds.insert(FAMILY_CASE_WITNESS_KIND);

	HashSet<String> declared_pairs;
	HashSet<String> required_pairs;
	for (const Dictionary &entry : census_dictionary_array(policies, "entries")) {
		const String key = policy_pair_key(census_string(entry, "representation"),
				census_string(entry, "child_slot"), census_string(entry, "surface"));
		declared_pairs.insert(key);
		if (census_string(entry, "policy") != "not_applicable") {
			required_pairs.insert(key);
		}
	}
	if (required_pairs.is_empty()) {
		r_errors.push_back("census/policies.json:$.entries: no operative policy pair is declared");
	}

	HashSet<String> witnessed_unsupported_representations;
	for (const Dictionary &entry : census_dictionary_array(unsupported, "entries")) {
		for (const Dictionary &witness : census_dictionary_array(entry, "witnesses")) {
			if (census_string(witness, "status") == "present") {
				witnessed_unsupported_representations.insert(census_string(entry, "representation"));
			}
		}
	}

	const Variant &raw_entries = coverage.get("entries", Variant());
	if (raw_entries.get_type() != Variant::ARRAY) {
		r_errors.push_back(vformat("%s:$.entries: must be an array", coverage_path));
		return ERR_INVALID_DATA;
	}
	const Array entries = raw_entries;
	HashSet<String> seen_keys;
	Vector<FSCompletenessCoverageEntry> parsed_entries;
	for (int index = 0; index < entries.size(); index++) {
		const String json_path = vformat("%s:$.entries[%d]", coverage_path, index);
		if (entries[index].get_type() != Variant::DICTIONARY) {
			r_errors.push_back(vformat("%s: must be an object", json_path));
			continue;
		}
		const Dictionary raw_entry = entries[index];
		check_unknown_members(raw_entry, COVERAGE_ENTRY_MEMBERS, COVERAGE_ENTRY_MEMBER_COUNT,
				json_path, r_errors);

		FSCompletenessCoverageEntry entry;
		entry.representation = census_string(raw_entry, "representation");
		entry.child_slot = census_string(raw_entry, "child_slot");
		entry.surface = census_string(raw_entry, "surface");
		entry.status = census_string(raw_entry, "status");
		entry.issue_url = census_string(raw_entry, "issue_url");
		if (entry.representation.is_empty() || entry.child_slot.is_empty() || entry.surface.is_empty()) {
			r_errors.push_back(vformat("%s: names no representation, child slot, and surface", json_path));
			continue;
		}
		const String key = entry.key();
		if (seen_keys.has(key)) {
			r_errors.push_back(vformat("%s: duplicate coverage entry for %s", json_path, key));
			continue;
		}
		seen_keys.insert(key);
		if (!declared_pairs.has(key)) {
			r_errors.push_back(vformat("%s: %s is not a policy pair", json_path, key));
			continue;
		}
		if (!required_pairs.has(key)) {
			r_errors.push_back(vformat("%s: %s is declared not_applicable and takes no coverage entry",
					json_path, key));
			continue;
		}
		if (!statuses.has(entry.status)) {
			r_errors.push_back(vformat("%s.status: '%s' is outside the coverage-status vocabulary",
					json_path, entry.status));
			continue;
		}
		parse_witness(raw_entry, json_path, witness_kinds, entry.witness, r_errors);
		if (entry.status == "unsupported" && !witnessed_unsupported_representations.has(entry.representation)) {
			r_errors.push_back(vformat("%s: no unsupported.json entry for representation '%s' carries a present witness",
					json_path, entry.representation));
		}
		if ((entry.status == "uncovered" || entry.status == "quality_deferred") && entry.issue_url.is_empty()) {
			r_errors.push_back(vformat("%s.issue_url: a %s entry names no owning issue", json_path, entry.status));
		}
		parsed_entries.push_back(entry);
	}

	Vector<String> missing_pairs;
	for (const String &pair : required_pairs) {
		if (!seen_keys.has(pair)) {
			missing_pairs.push_back(pair);
		}
	}
	missing_pairs.sort();
	for (const String &pair : missing_pairs) {
		r_errors.push_back(vformat("%s: no coverage entry for %s (uncovered representation child)",
				coverage_path, pair));
	}

	if (r_errors.size() != error_count_on_entry) {
		r_summary = FSCompletenessCensusSummary();
		return ERR_INVALID_DATA;
	}

	parsed_entries.sort_custom<CoverageEntryKeyLess>();
	for (const FSCompletenessCoverageEntry &entry : parsed_entries) {
		if (entry.status == "covered") {
			r_summary.covered++;
		} else if (entry.status == "uncovered") {
			r_summary.uncovered++;
		} else if (entry.status == "unsupported") {
			r_summary.unsupported++;
		} else {
			r_summary.quality_deferred++;
		}
	}
	r_summary.entries = parsed_entries;
	return OK;
}

Dictionary FSCompletenessCensus::summary_report(const FSCompletenessCensusSummary &p_summary) {
	Dictionary report;
	report["covered"] = double(p_summary.covered);
	report["uncovered"] = double(p_summary.uncovered);
	report["unsupported"] = double(p_summary.unsupported);
	report["quality_deferred"] = double(p_summary.quality_deferred);
	Array entries;
	for (const FSCompletenessCoverageEntry &entry : p_summary.entries) {
		Dictionary record;
		record["representation"] = entry.representation;
		record["child_slot"] = entry.child_slot;
		record["surface"] = entry.surface;
		record["status"] = entry.status;
		record["witness"] = witness_report(entry.witness);
		record["issue_url"] = entry.issue_url;
		entries.push_back(record);
	}
	report["entries"] = entries;
	return report;
}

bool FSCompletenessCensus::resolve_witness(const String &p_root, const FSCompletenessCoverageEntry &p_entry,
		String &r_detail) {
	r_detail = String();
	if (!p_entry.witness.is_declared()) {
		r_detail = "declares no witness";
		return false;
	}
	if (p_entry.witness.kind == "doctest_case") {
		return resolve_doctest_case_reference(p_entry.witness.reference, r_detail);
	}
	if (p_entry.witness.kind == "fixture") {
		return resolve_tracked_fixture_reference(p_root, p_entry.witness.reference, r_detail);
	}
	if (p_entry.witness.kind == FAMILY_CASE_WITNESS_KIND) {
		return resolve_family_case_witness(p_root, p_entry.witness, r_detail);
	}
	r_detail = vformat("witness kind '%s' has no resolution", p_entry.witness.kind);
	return false;
}

Vector<String> FSCompletenessCensus::unresolved_covered_witnesses(
		const String &p_root, const FSCompletenessCensusSummary &p_summary) {
	Vector<String> unresolved;
	for (const FSCompletenessCoverageEntry &entry : p_summary.entries) {
		if (entry.status != "covered") {
			continue;
		}
		String detail;
		if (!resolve_witness(p_root, entry, detail)) {
			unresolved.push_back(vformat("%s: %s", entry.key(), detail));
		}
	}
	return unresolved;
}

} // namespace FSTests
