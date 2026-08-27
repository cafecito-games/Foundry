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

#include "core/error/error_macros.h"
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
	if (probe_error != OK || exit_code != 0) {
		// The witness exists at a canonical path inside the repository; only its tracked status is
		// unverifiable here. Environments without a usable git - a source tree without a repository, a
		// sandbox that refuses to launch it - would otherwise turn every fixture witness into an
		// unresolved one, which is a verdict about the environment rather than about the census. The
		// refusal is reported once per process so a run that verified nothing is still visible.
		static bool reported_unavailable_probe = false;
		if (!reported_unavailable_probe) {
			reported_unavailable_probe = true;
			WARN_PRINT(vformat("Git tracked-file verification is unavailable (error %d, exit %d); census "
							   "fixture witnesses are accepted on canonical existence alone.",
					probe_error, exit_code));
		}
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
	"build_configuration",
};

constexpr int WITNESS_MEMBER_COUNT = 5;

// Ascending by policy-pair key, which is the order every census report is published in.
struct CoverageEntryKeyLess {
	bool operator()(const FSCompletenessCoverageEntry &p_left, const FSCompletenessCoverageEntry &p_right) const {
		return p_left.key() < p_right.key();
	}
};

// Every vocabulary the census uses is closed by the code that implements it, not by the document that
// declares it. Each enumeration below is the single source of truth: `..._id` has no default arm, so a
// new enumerator fails to compile until it is named, and `check_vocabulary_closure` refuses a schema
// that declares a term nothing implements or omits a term the code does implement. Accepting whatever
// the schema listed would let a term added there reach code that has no branch for it - a status with
// no rules, a witness kind with no resolution - and be counted as if it had been validated.
enum class CoverageStatus {
	COVERED,
	UNCOVERED,
	UNSUPPORTED,
	QUALITY_DEFERRED,
	MAX,
};

enum class WitnessKind {
	DOCTEST_CASE,
	FIXTURE,
	// A family case observes a resolved matrix cell. It is a coverage-document kind only: unsupported
	// configurations are witnessed by executable negative evidence, never by a matrix cell, so the
	// shared `witness_kinds` vocabulary must not declare it.
	FAMILY_CASE,
	MAX,
};

enum class WitnessStatus {
	PRESENT,
	DEFERRED,
	MAX,
};

// The build configurations a witness can restrict itself to. Only a configuration that compiles more
// than a template does needs naming: a witness with no declaration must bind in every build.
enum class WitnessConfiguration {
	EDITOR,
	MAX,
};

enum class PolicyKind {
	TRAVERSE,
	PRESERVE,
	SUBSTITUTE,
	PROJECT,
	ERASE,
	NOT_APPLICABLE,
	MAX,
};

enum class TransitionKind {
	PROJECT,
	ERASE,
	SUBSTITUTE,
	PRESERVE,
	REIFY,
	MAX,
};

String coverage_status_id(CoverageStatus p_status) {
	switch (p_status) {
		case CoverageStatus::COVERED:
			return "covered";
		case CoverageStatus::UNCOVERED:
			return "uncovered";
		case CoverageStatus::UNSUPPORTED:
			return "unsupported";
		case CoverageStatus::QUALITY_DEFERRED:
			return "quality_deferred";
		case CoverageStatus::MAX:
			break;
	}
	return String();
}

String witness_kind_id(WitnessKind p_kind) {
	switch (p_kind) {
		case WitnessKind::DOCTEST_CASE:
			return "doctest_case";
		case WitnessKind::FIXTURE:
			return "fixture";
		case WitnessKind::FAMILY_CASE:
			return "family_case";
		case WitnessKind::MAX:
			break;
	}
	return String();
}

String witness_configuration_id(WitnessConfiguration p_configuration) {
	switch (p_configuration) {
		case WitnessConfiguration::EDITOR:
			return "editor";
		case WitnessConfiguration::MAX:
			break;
	}
	return String();
}

String witness_status_id(WitnessStatus p_status) {
	switch (p_status) {
		case WitnessStatus::PRESENT:
			return "present";
		case WitnessStatus::DEFERRED:
			return "deferred";
		case WitnessStatus::MAX:
			break;
	}
	return String();
}

String policy_kind_id(PolicyKind p_policy) {
	switch (p_policy) {
		case PolicyKind::TRAVERSE:
			return "traverse";
		case PolicyKind::PRESERVE:
			return "preserve";
		case PolicyKind::SUBSTITUTE:
			return "substitute";
		case PolicyKind::PROJECT:
			return "project";
		case PolicyKind::ERASE:
			return "erase";
		case PolicyKind::NOT_APPLICABLE:
			return "not_applicable";
		case PolicyKind::MAX:
			break;
	}
	return String();
}

String transition_kind_id(TransitionKind p_kind) {
	switch (p_kind) {
		case TransitionKind::PROJECT:
			return "project";
		case TransitionKind::ERASE:
			return "erase";
		case TransitionKind::SUBSTITUTE:
			return "substitute";
		case TransitionKind::PRESERVE:
			return "preserve";
		case TransitionKind::REIFY:
			return "reify";
		case TransitionKind::MAX:
			break;
	}
	return String();
}

// The implemented terms of one enumeration, in enum order.
template <typename Enumeration>
Vector<String> implemented_vocabulary(String (*p_id_of)(Enumeration)) {
	Vector<String> terms;
	for (int value = 0; value < int(Enumeration::MAX); value++) {
		const String id = p_id_of(Enumeration(value));
		if (!id.is_empty()) {
			terms.push_back(id);
		}
	}
	return terms;
}

template <typename Enumeration>
bool parse_vocabulary_term(const String &p_term, String (*p_id_of)(Enumeration), Enumeration &r_value) {
	for (int value = 0; value < int(Enumeration::MAX); value++) {
		if (p_id_of(Enumeration(value)) == p_term) {
			r_value = Enumeration(value);
			return true;
		}
	}
	return false;
}

// Both directions, so the schema and the code cannot drift apart: a declared term nothing implements
// would be accepted by no branch, and an implemented term the schema omits could never appear in a
// document that validates against it.
void check_vocabulary_closure(const Dictionary &p_schema, const String &p_member,
		const Vector<String> &p_implemented, const String &p_schema_path, Vector<String> &r_errors) {
	const HashSet<String> declared = census_string_set(census_string_array(p_schema, p_member));
	HashSet<String> implemented;
	for (const String &term : p_implemented) {
		implemented.insert(term);
		if (!declared.has(term)) {
			r_errors.push_back(vformat("%s:$.%s: does not declare implemented term '%s'",
					p_schema_path, p_member, term));
		}
	}
	Vector<String> undeclared;
	for (const String &term : declared) {
		if (!implemented.has(term)) {
			undeclared.push_back(term);
		}
	}
	undeclared.sort();
	for (const String &term : undeclared) {
		r_errors.push_back(vformat("%s:$.%s: declares term '%s', which the census loader does not implement",
				p_schema_path, p_member, term));
	}
}

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

// Binds r_entries to p_document's p_key member when it is an array of objects. A member of another
// type, or an element that is not an object, is a defect of the document rather than a shorter list:
// skipping it would quietly relax whatever rule the list decides.
bool require_object_array(const Dictionary &p_document, const String &p_key, const String &p_member_path,
		Array &r_entries, Vector<String> &r_errors) {
	const Variant &value = p_document.get(p_key, Variant());
	if (value.get_type() != Variant::ARRAY) {
		r_errors.push_back(vformat("%s: must be an array", p_member_path));
		return false;
	}
	const Array entries = value;
	for (int index = 0; index < entries.size(); index++) {
		if (entries[index].get_type() != Variant::DICTIONARY) {
			r_errors.push_back(vformat("%s[%d]: must be an object", p_member_path, index));
			return false;
		}
	}
	r_entries = entries;
	return true;
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
		FSCompletenessCoverageWitness &r_witness, Vector<String> &r_errors) {
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
	WitnessKind witness_kind = WitnessKind::MAX;
	if (!parse_vocabulary_term(kind, witness_kind_id, witness_kind)) {
		r_errors.push_back(vformat("%s.kind: '%s' is outside the witness-kind vocabulary", witness_path, kind));
		return;
	}
	r_witness.kind = kind;
	const String build_configuration = census_string(witness, "build_configuration");
	if (!build_configuration.is_empty()) {
		WitnessConfiguration declared_configuration = WitnessConfiguration::MAX;
		if (!parse_vocabulary_term(build_configuration, witness_configuration_id, declared_configuration)) {
			r_errors.push_back(vformat(
					"%s.build_configuration: '%s' is outside the build-configuration vocabulary",
					witness_path, build_configuration));
		} else if (witness_kind != WitnessKind::DOCTEST_CASE) {
			// A tracked fixture path and a resolved matrix cell are catalog data every build reads the
			// same way. Only a compiled test case can be absent from a build that is otherwise correct.
			r_errors.push_back(
					vformat("%s.build_configuration: only a doctest_case witness is configuration-dependent",
							witness_path));
		} else {
			r_witness.build_configuration = build_configuration;
		}
	}
	if (witness_kind == WitnessKind::FAMILY_CASE) {
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
	// The declaration travels with the claim: a reader of the document has to be able to tell a witness
	// every build compiles from one only some builds do, without the census beside it.
	report["build_configuration"] = p_witness.build_configuration;
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
	// The witness goes through the same load-validate-resolve the runner performs, so a coverage claim
	// can only stand on a family that would actually run: one whose manifest parses, whose vocabulary
	// holds against the catalog, and whose derivation graph leaves every cell with a reachable
	// disposition. The cache makes that one resolution per family per process rather than one per
	// witness, which is what keeps a census check affordable without deciding less.
	const FSCompletenessCatalogRecord *record = nullptr;
	Vector<String> errors;
	FSCompletenessCatalogCache::get(catalog_root, p_witness.family, record, errors);
	if (record == nullptr || record->resolution_error != OK) {
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
	Dictionary representations;
	Dictionary policies;
	Dictionary unsupported;
	Dictionary transitions;
	Dictionary coverage;
	if (!read_census_document(p_root, "schema.json", schema, r_errors) ||
			!read_census_document(p_root, "representations.json", representations, r_errors) ||
			!read_census_document(p_root, "policies.json", policies, r_errors) ||
			!read_census_document(p_root, "unsupported.json", unsupported, r_errors) ||
			!read_census_document(p_root, "transitions.json", transitions, r_errors) ||
			!read_census_document(p_root, "coverage.json", coverage, r_errors)) {
		return ERR_INVALID_DATA;
	}

	const String coverage_path = census_directory(p_root).path_join("coverage.json");
	int schema_version = 0;
	if (!parse_json_integer(coverage.get("schema_version", Variant()), schema_version) ||
			schema_version != CENSUS_SCHEMA_VERSION) {
		r_errors.push_back(vformat("%s:$.schema_version: expected %d", coverage_path, CENSUS_SCHEMA_VERSION));
	}

	// Every closed vocabulary is checked against what this loader implements before a single document
	// is read against it. A schema that declares a term the code has no branch for, or omits one the
	// code does implement, is a defect of the pair rather than of either file alone.
	const String schema_path = census_directory(p_root).path_join("schema.json");
	check_vocabulary_closure(schema, "coverage_statuses",
			implemented_vocabulary<CoverageStatus>(coverage_status_id), schema_path, r_errors);
	Vector<String> declarable_witness_kinds;
	for (const String &kind : implemented_vocabulary<WitnessKind>(witness_kind_id)) {
		if (kind != FAMILY_CASE_WITNESS_KIND) {
			declarable_witness_kinds.push_back(kind);
		}
	}
	check_vocabulary_closure(schema, "witness_kinds", declarable_witness_kinds, schema_path, r_errors);
	check_vocabulary_closure(schema, "witness_statuses",
			implemented_vocabulary<WitnessStatus>(witness_status_id), schema_path, r_errors);
	check_vocabulary_closure(schema, "witness_build_configurations",
			implemented_vocabulary<WitnessConfiguration>(witness_configuration_id), schema_path, r_errors);
	check_vocabulary_closure(schema, "policies",
			implemented_vocabulary<PolicyKind>(policy_kind_id), schema_path, r_errors);
	check_vocabulary_closure(schema, "transition_kinds",
			implemented_vocabulary<TransitionKind>(transition_kind_id), schema_path, r_errors);

	const Vector<String> surfaces = census_string_array(schema, "surfaces");
	if (surfaces.is_empty()) {
		r_errors.push_back(vformat("%s:$.surfaces: no surface vocabulary is declared", schema_path));
		return ERR_INVALID_DATA;
	}

	// The child slots the representations are inventoried with, crossed with the surfaces, are what the
	// policy matrix must cover exactly. Deriving the matrix from policies.json alone would let a deleted
	// policy and its coverage entry shrink the census together and still load.
	HashMap<String, HashSet<String>> slots_by_representation;
	Array representation_entries;
	if (!require_object_array(representations, "representations",
				census_directory(p_root).path_join("representations.json") + ":$.representations",
				representation_entries, r_errors)) {
		return ERR_INVALID_DATA;
	}
	const String representations_path = census_directory(p_root).path_join("representations.json");
	for (int index = 0; index < representation_entries.size(); index++) {
		const Dictionary representation = representation_entries[index];
		const String id = census_string(representation, "id");
		if (id.is_empty()) {
			r_errors.push_back(vformat("%s:$.representations[%d]: names no id", representations_path, index));
			continue;
		}
		// An identity table keyed by a repeated id silently loses whichever entry came first, and with
		// it every cell that entry's child slots required.
		if (slots_by_representation.has(id)) {
			r_errors.push_back(vformat("%s:$.representations[%d]: duplicate representation id '%s'",
					representations_path, index, id));
			continue;
		}
		Array child_slots;
		if (!require_object_array(representation, "child_slots",
					vformat("%s:$.representations[%d].child_slots", representations_path, index),
					child_slots, r_errors)) {
			continue;
		}
		HashSet<String> slots;
		for (int slot_index = 0; slot_index < child_slots.size(); slot_index++) {
			const Dictionary slot = child_slots[slot_index];
			const String slot_id = census_string(slot, "id");
			if (slot_id.is_empty()) {
				r_errors.push_back(vformat("%s:$.representations[%d].child_slots[%d]: names no id",
						representations_path, index, slot_index));
				continue;
			}
			if (slots.has(slot_id)) {
				r_errors.push_back(vformat("%s:$.representations[%d].child_slots[%d]: duplicate child slot '%s'",
						representations_path, index, slot_index, slot_id));
				continue;
			}
			slots.insert(slot_id);
		}
		// A representation with no recursive children is a leaf of the census, not a defect: it
		// contributes no policy cell at all.
		slots_by_representation.insert(id, slots);
	}

	// The pair matrix and the exemption list decide what coverage.json must contain and what it may
	// exempt, so a malformed one of either is refused here rather than read as a shorter matrix or as
	// an absent exemption. Skipping a malformed entry would silently relax both rules at once.
	const String policies_path = census_directory(p_root).path_join("policies.json");
	HashSet<String> required_pairs;
	HashMap<String, int> policy_counts;
	Array policy_entries;
	if (!require_object_array(policies, "entries", policies_path + ":$.entries", policy_entries, r_errors)) {
		return ERR_INVALID_DATA;
	}
	for (int index = 0; index < policy_entries.size(); index++) {
		const Dictionary entry = policy_entries[index];
		const String entry_path = vformat("%s:$.entries[%d]", policies_path, index);
		const String representation = census_string(entry, "representation");
		const String child_slot = census_string(entry, "child_slot");
		const String surface = census_string(entry, "surface");
		const String policy = census_string(entry, "policy");
		const HashSet<String> *slots = slots_by_representation.getptr(representation);
		if (slots == nullptr) {
			r_errors.push_back(vformat("%s: representation '%s' is not inventoried", entry_path, representation));
			continue;
		}
		if (!slots->has(child_slot)) {
			r_errors.push_back(vformat("%s: %s has no child slot '%s'", entry_path, representation, child_slot));
			continue;
		}
		if (!surfaces.has(surface)) {
			r_errors.push_back(vformat("%s.surface: '%s' is outside the surface vocabulary", entry_path, surface));
			continue;
		}
		PolicyKind policy_kind = PolicyKind::MAX;
		if (!parse_vocabulary_term(policy, policy_kind_id, policy_kind)) {
			r_errors.push_back(vformat("%s.policy: '%s' is outside the policy vocabulary", entry_path, policy));
			continue;
		}
		if (census_string(entry, "rationale").is_empty()) {
			r_errors.push_back(vformat("%s: declares a policy with no rationale", entry_path));
			continue;
		}
		const String key = policy_pair_key(representation, child_slot, surface);
		if (policy_counts.getptr(key) != nullptr) {
			r_errors.push_back(vformat("%s: duplicate policy for %s", entry_path, key));
			continue;
		}
		policy_counts[key] += 1;
		switch (policy_kind) {
			case PolicyKind::TRAVERSE:
			case PolicyKind::PRESERVE:
			case PolicyKind::SUBSTITUTE:
			case PolicyKind::PROJECT:
			case PolicyKind::ERASE:
				// Every operative policy describes a crossing a witness has to observe.
				required_pairs.insert(key);
				break;
			case PolicyKind::NOT_APPLICABLE:
			case PolicyKind::MAX:
				break;
		}
	}

	Vector<String> policy_matrix_errors;
	for (const KeyValue<String, HashSet<String>> &representation : slots_by_representation) {
		for (const String &child_slot : representation.value) {
			for (const String &surface : surfaces) {
				const String key = policy_pair_key(representation.key, child_slot, surface);
				const int *count = policy_counts.getptr(key);
				if (count == nullptr) {
					policy_matrix_errors.push_back(
							vformat("%s: no policy for %s (uncovered representation child)", policies_path, key));
				} else if (*count > 1) {
					policy_matrix_errors.push_back(
							vformat("%s: %d policies for %s (ambiguous)", policies_path, *count, key));
				}
			}
		}
	}
	policy_matrix_errors.sort();
	for (const String &matrix_error : policy_matrix_errors) {
		r_errors.push_back(matrix_error);
	}
	if (required_pairs.is_empty()) {
		r_errors.push_back(vformat("%s:$.entries: no operative policy pair is declared", policies_path));
	}

	const String unsupported_path = census_directory(p_root).path_join("unsupported.json");
	HashSet<String> witnessed_unsupported_representations;
	Vector<FSCompletenessUnsupportedWitness> unsupported_witnesses;
	Array unsupported_entries;
	if (!require_object_array(
				unsupported, "entries", unsupported_path + ":$.entries", unsupported_entries, r_errors)) {
		return ERR_INVALID_DATA;
	}
	HashSet<String> unsupported_ids;
	for (int index = 0; index < unsupported_entries.size(); index++) {
		const Dictionary entry = unsupported_entries[index];
		const String entry_path = vformat("%s:$.entries[%d]", unsupported_path, index);
		const String entry_id = census_string(entry, "id");
		if (entry_id.is_empty()) {
			r_errors.push_back(vformat("%s: names no id", entry_path));
			continue;
		}
		if (unsupported_ids.has(entry_id)) {
			r_errors.push_back(vformat("%s: duplicate unsupported entry id '%s'", entry_path, entry_id));
			continue;
		}
		unsupported_ids.insert(entry_id);
		const String representation = census_string(entry, "representation");
		if (representation.is_empty()) {
			r_errors.push_back(vformat("%s: names no representation", entry_path));
			continue;
		}
		Array witnesses;
		if (!require_object_array(entry, "witnesses", entry_path + ".witnesses", witnesses, r_errors)) {
			continue;
		}
		if (witnesses.is_empty()) {
			r_errors.push_back(vformat("%s.witnesses: an unsupported configuration carries no witness", entry_path));
			continue;
		}
		for (int witness_index = 0; witness_index < witnesses.size(); witness_index++) {
			const Dictionary witness = witnesses[witness_index];
			const String witness_path = vformat("%s.witnesses[%d]", entry_path, witness_index);
			const String kind = census_string(witness, "kind");
			WitnessKind witness_kind = WitnessKind::MAX;
			if (!parse_vocabulary_term(kind, witness_kind_id, witness_kind) ||
					witness_kind == WitnessKind::FAMILY_CASE) {
				// A family case observes a matrix cell, which is not an executable negative witness.
				r_errors.push_back(vformat("%s.kind: '%s' is not an executable negative-witness kind",
						witness_path, kind));
				continue;
			}
			const String status = census_string(witness, "status");
			WitnessStatus witness_status = WitnessStatus::MAX;
			if (!parse_vocabulary_term(status, witness_status_id, witness_status)) {
				r_errors.push_back(vformat("%s.status: '%s' is outside the witness-status vocabulary",
						witness_path, status));
				continue;
			}
			if (census_string(witness, "reference").is_empty()) {
				r_errors.push_back(vformat("%s.reference: a witness carries no reference", witness_path));
				continue;
			}
			const String build_configuration = census_string(witness, "build_configuration");
			if (!build_configuration.is_empty()) {
				WitnessConfiguration declared_configuration = WitnessConfiguration::MAX;
				if (!parse_vocabulary_term(
							build_configuration, witness_configuration_id, declared_configuration)) {
					r_errors.push_back(vformat(
							"%s.build_configuration: '%s' is outside the build-configuration vocabulary",
							witness_path, build_configuration));
					continue;
				}
				if (witness_kind != WitnessKind::DOCTEST_CASE) {
					r_errors.push_back(vformat(
							"%s.build_configuration: only a doctest_case witness is configuration-dependent",
							witness_path));
					continue;
				}
			}
			switch (witness_status) {
				case WitnessStatus::PRESENT: {
					witnessed_unsupported_representations.insert(representation);
					FSCompletenessUnsupportedWitness present_witness;
					present_witness.entry_id = entry_id;
					present_witness.representation = representation;
					present_witness.witness.kind = kind;
					present_witness.witness.reference = census_string(witness, "reference");
					present_witness.witness.build_configuration = build_configuration;
					unsupported_witnesses.push_back(present_witness);
				} break;
				case WitnessStatus::DEFERRED:
					// A deferred witness observes nothing, so it authorizes no exemption. The census
					// test refuses one outright; the loader simply never counts it as evidence.
					break;
				case WitnessStatus::MAX:
					break;
			}
		}
	}

	// The transition table is identity-keyed like every other census document, and a repeated id is the
	// same defect there: one crossing silently replaces another.
	const String transitions_path = census_directory(p_root).path_join("transitions.json");
	Array transition_entries;
	if (!require_object_array(
				transitions, "transitions", transitions_path + ":$.transitions", transition_entries, r_errors)) {
		return ERR_INVALID_DATA;
	}
	HashSet<String> transition_ids;
	for (int index = 0; index < transition_entries.size(); index++) {
		const Dictionary transition = transition_entries[index];
		const String transition_id = census_string(transition, "id");
		const String transition_path = vformat("%s:$.transitions[%d]", transitions_path, index);
		if (transition_id.is_empty()) {
			r_errors.push_back(vformat("%s: names no id", transition_path));
			continue;
		}
		if (transition_ids.has(transition_id)) {
			r_errors.push_back(vformat("%s: duplicate transition id '%s'", transition_path, transition_id));
			continue;
		}
		transition_ids.insert(transition_id);
		const String transition_kind = census_string(transition, "kind");
		TransitionKind parsed_kind = TransitionKind::MAX;
		if (!parse_vocabulary_term(transition_kind, transition_kind_id, parsed_kind)) {
			r_errors.push_back(vformat("%s.kind: '%s' is outside the transition-kind vocabulary",
					transition_path, transition_kind));
			continue;
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
		if (policy_counts.getptr(key) == nullptr) {
			r_errors.push_back(vformat("%s: %s is not a policy pair", json_path, key));
			continue;
		}
		if (!required_pairs.has(key)) {
			r_errors.push_back(vformat("%s: %s is declared not_applicable and takes no coverage entry",
					json_path, key));
			continue;
		}
		CoverageStatus status = CoverageStatus::MAX;
		if (!parse_vocabulary_term(entry.status, coverage_status_id, status)) {
			r_errors.push_back(vformat("%s.status: '%s' is outside the coverage-status vocabulary",
					json_path, entry.status));
			continue;
		}
		parse_witness(raw_entry, json_path, entry.witness, r_errors);
		// What each status obliges the entry to carry, decided in one place with no default arm, so a
		// new status cannot be added without deciding what it must prove.
		switch (status) {
			case CoverageStatus::COVERED:
				// The witness itself is resolved by unresolved_witnesses(); an entry that declares none
				// is reported there rather than here, so the message names what failed to bind.
				break;
			case CoverageStatus::UNSUPPORTED:
				if (!witnessed_unsupported_representations.has(entry.representation)) {
					r_errors.push_back(
							vformat("%s: no unsupported.json entry for representation '%s' carries a present witness",
									json_path, entry.representation));
				}
				break;
			case CoverageStatus::UNCOVERED:
			case CoverageStatus::QUALITY_DEFERRED:
				if (entry.issue_url.is_empty()) {
					r_errors.push_back(
							vformat("%s.issue_url: a %s entry names no owning issue", json_path, entry.status));
				}
				break;
			case CoverageStatus::MAX:
				break;
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
		CoverageStatus status = CoverageStatus::MAX;
		// Every entry parsed above, so this cannot fail; counting is still a switch with no default arm
		// so a new status is counted deliberately rather than folded into whichever branch came last.
		ERR_CONTINUE(!parse_vocabulary_term(entry.status, coverage_status_id, status));
		switch (status) {
			case CoverageStatus::COVERED:
				r_summary.covered++;
				break;
			case CoverageStatus::UNCOVERED:
				r_summary.uncovered++;
				break;
			case CoverageStatus::UNSUPPORTED:
				r_summary.unsupported++;
				break;
			case CoverageStatus::QUALITY_DEFERRED:
				r_summary.quality_deferred++;
				break;
			case CoverageStatus::MAX:
				break;
		}
	}
	r_summary.entries = parsed_entries;
	r_summary.unsupported_witnesses = unsupported_witnesses;
	return OK;
}

Vector<String> FSCompletenessCensus::summary_count_members() {
	return implemented_vocabulary<CoverageStatus>(coverage_status_id);
}

FSCompletenessCensusScope FSCompletenessCensusScope::everything() {
	return FSCompletenessCensusScope();
}

FSCompletenessCensusScope FSCompletenessCensusScope::for_family(const String &p_family) {
	FSCompletenessCensusScope scope;
	scope.exhaustive = false;
	if (!p_family.is_empty()) {
		scope.families.insert(p_family);
	}
	return scope;
}

bool FSCompletenessCensusScope::includes(const FSCompletenessCoverageWitness &p_witness) const {
	if (exhaustive) {
		return true;
	}
	// A witness that names no family belongs to no family's run, so every scope answers for it.
	return p_witness.family.is_empty() || families.has(p_witness.family);
}

Vector<String> FSCompletenessCensus::claimed_families(const FSCompletenessCensusSummary &p_summary) {
	HashSet<String> seen;
	Vector<String> families;
	auto record = [&](const FSCompletenessCoverageWitness &p_witness) {
		if (p_witness.family.is_empty() || seen.has(p_witness.family)) {
			return;
		}
		seen.insert(p_witness.family);
		families.push_back(p_witness.family);
	};
	for (const FSCompletenessCoverageEntry &entry : p_summary.entries) {
		if (entry.status == coverage_status_id(CoverageStatus::COVERED)) {
			record(entry.witness);
		}
	}
	for (const FSCompletenessUnsupportedWitness &unsupported : p_summary.unsupported_witnesses) {
		record(unsupported.witness);
	}
	families.sort();
	return families;
}

Dictionary FSCompletenessCensus::summary_report(
		const FSCompletenessCensusSummary &p_summary, const FSCompletenessCensusScope &p_scope) {
	Dictionary report;
	for (int value = 0; value < int(CoverageStatus::MAX); value++) {
		const CoverageStatus status = CoverageStatus(value);
		int count = 0;
		switch (status) {
			case CoverageStatus::COVERED:
				count = p_summary.covered;
				break;
			case CoverageStatus::UNCOVERED:
				count = p_summary.uncovered;
				break;
			case CoverageStatus::UNSUPPORTED:
				count = p_summary.unsupported;
				break;
			case CoverageStatus::QUALITY_DEFERRED:
				count = p_summary.quality_deferred;
				break;
			case CoverageStatus::MAX:
				continue;
		}
		report[coverage_status_id(status)] = double(count);
	}
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
	const Vector<String> families = claimed_families(p_summary);
	int validated = 0;
	for (const String &family : families) {
		FSCompletenessCoverageWitness probe;
		probe.family = family;
		if (p_scope.includes(probe)) {
			validated++;
		}
	}
	report["declared_families"] = double(families.size());
	report["validated_families"] = double(validated);
	return report;
}

bool FSCompletenessCensus::resolve_witness(const String &p_root, const FSCompletenessCoverageEntry &p_entry,
		String &r_detail) {
	r_detail = String();
	if (!p_entry.witness.is_declared()) {
		r_detail = "declares no witness";
		return false;
	}
	WitnessKind kind = WitnessKind::MAX;
	if (!parse_vocabulary_term(p_entry.witness.kind, witness_kind_id, kind)) {
		r_detail = vformat("witness kind '%s' has no resolution", p_entry.witness.kind);
		return false;
	}
	switch (kind) {
		case WitnessKind::DOCTEST_CASE:
			return resolve_doctest_case_reference(p_entry.witness.reference, r_detail);
		case WitnessKind::FIXTURE:
			return resolve_tracked_fixture_reference(p_root, p_entry.witness.reference, r_detail);
		case WitnessKind::FAMILY_CASE:
			return resolve_family_case_witness(p_root, p_entry.witness, r_detail);
		case WitnessKind::MAX:
			break;
	}
	r_detail = vformat("witness kind '%s' has no resolution", p_entry.witness.kind);
	return false;
}

bool FSCompletenessCensus::witness_needs_other_configuration(
		const FSCompletenessCoverageWitness &p_witness) {
	if (p_witness.build_configuration.is_empty()) {
		return false;
	}
#ifdef TOOLS_ENABLED
	return p_witness.build_configuration != witness_configuration_id(WitnessConfiguration::EDITOR);
#else
	return p_witness.build_configuration == witness_configuration_id(WitnessConfiguration::EDITOR);
#endif
}

// Both verdicts read the same claims in the same order, so a witness is either resolved, unresolved,
// or unconfirmable here, and never two of the three.
static Vector<String> claimed_witnesses(const String &p_root, const FSCompletenessCensusSummary &p_summary,
		const FSCompletenessCensusScope &p_scope, bool p_collect_unconfirmable) {
	Vector<String> messages;
	auto record = [&](const String &p_label, const FSCompletenessCoverageEntry &p_entry) {
		// A claim outside the scope is not this consumer's to answer: it is neither bound nor broken
		// here, and the report says how much of the census was asked about.
		if (!p_scope.includes(p_entry.witness)) {
			return;
		}
		const bool needs_other_configuration =
				FSCompletenessCensus::witness_needs_other_configuration(p_entry.witness);
		if (needs_other_configuration != p_collect_unconfirmable) {
			return;
		}
		if (needs_other_configuration) {
			messages.push_back(vformat("%s: witness '%s' is only compiled in the %s configuration", p_label,
					p_entry.witness.reference, p_entry.witness.build_configuration));
			return;
		}
		String detail;
		if (!FSCompletenessCensus::resolve_witness(p_root, p_entry, detail)) {
			messages.push_back(vformat("%s: %s", p_label, detail));
		}
	};
	for (const FSCompletenessCoverageEntry &entry : p_summary.entries) {
		if (entry.status != coverage_status_id(CoverageStatus::COVERED)) {
			continue;
		}
		record(entry.key(), entry);
	}
	// An exemption is only an exemption while the negative witness it stands on still observes the
	// rejection. A reference nothing binds authorizes nothing, whatever its declared status says.
	for (const FSCompletenessUnsupportedWitness &unsupported : p_summary.unsupported_witnesses) {
		FSCompletenessCoverageEntry entry;
		entry.representation = unsupported.representation;
		entry.status = coverage_status_id(CoverageStatus::UNSUPPORTED);
		entry.witness = unsupported.witness;
		record(vformat("unsupported '%s'", unsupported.entry_id), entry);
	}
	return messages;
}

Vector<String> FSCompletenessCensus::unresolved_witnesses(const String &p_root,
		const FSCompletenessCensusSummary &p_summary, const FSCompletenessCensusScope &p_scope) {
	return claimed_witnesses(p_root, p_summary, p_scope, false);
}

Vector<String> FSCompletenessCensus::unconfirmable_witnesses(const String &p_root,
		const FSCompletenessCensusSummary &p_summary, const FSCompletenessCensusScope &p_scope) {
	return claimed_witnesses(p_root, p_summary, p_scope, true);
}

} // namespace FSTests
