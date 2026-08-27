/**************************************************************************/
/*  fs_type_completeness_runner.cpp                                       */
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

#include "fs_type_completeness_runner.h"

#include "fs_temporary_project_tree.h"
#include "fs_type_completeness_cache.h"
#include "fs_type_completeness_case_id.h"
#include "fs_type_completeness_census.h"
#include "fs_type_completeness_common.h"
#include "fs_type_completeness_graph.h"
#include "fs_type_completeness_json.h"
#include "fs_type_completeness_manifest.h"
#include "fs_type_completeness_tooling_adapter.h"

#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/io/json.h"
#include "core/os/os.h"
#include "core/templates/hash_set.h"

namespace FSTests {

using namespace Completeness;

namespace {

static Vector<String> sorted_dimension_keys(
		const HashMap<String, FSCompletenessResolvedDimension> &p_dimensions) {
	Vector<String> keys;
	keys.reserve(p_dimensions.size());
	for (const KeyValue<String, FSCompletenessResolvedDimension> &dimension : p_dimensions) {
		keys.push_back(dimension.key);
	}
	keys.sort();
	return keys;
}

static Dictionary sorted_dictionary_copy(const Dictionary &p_dictionary) {
	Dictionary sorted;
	for (const String &key : sorted_dictionary_keys(p_dictionary)) {
		sorted[key] = p_dictionary[key];
	}
	return sorted;
}

static Dictionary provenance_step_report(const FSCompletenessProvenanceStep &p_step) {
	Dictionary report;
	report["relation_id"] = p_step.relation_id;
	report["exception_id"] = p_step.exception_id;
	report["source_coordinates"] = sorted_dictionary_copy(p_step.source_coordinates);
	report["target_coordinates"] = sorted_dictionary_copy(p_step.target_coordinates);
	report["input_dimensions"] = sorted_dictionary_copy(p_step.input_dimensions);
	report["output_dimensions"] = sorted_dictionary_copy(p_step.output_dimensions);
	return report;
}

static Array provenance_report(const Vector<FSCompletenessProvenanceStep> &p_provenance) {
	Array report;
	for (const FSCompletenessProvenanceStep &step : p_provenance) {
		report.push_back(provenance_step_report(step));
	}
	return report;
}

static Array agreeing_provenance_report(
		const Vector<Vector<FSCompletenessProvenanceStep>> &p_provenance) {
	Array report;
	for (const Vector<FSCompletenessProvenanceStep> &path : p_provenance) {
		report.push_back(provenance_report(path));
	}
	return report;
}

static bool provenance_has_exception_parent(const FSCompletenessResolvedDimension &p_dimension,
		const String &p_exception, const String &p_parent) {
	for (const Vector<FSCompletenessProvenanceStep> &path : p_dimension.agreeing_provenance) {
		for (const FSCompletenessProvenanceStep &step : path) {
			if (step.exception_id == p_exception && step.relation_id == p_parent) {
				return true;
			}
		}
	}
	return false;
}

static bool provenance_has_parent_without_exception(
		const FSCompletenessResolvedDimension &p_dimension, const String &p_parent) {
	for (const Vector<FSCompletenessProvenanceStep> &path : p_dimension.agreeing_provenance) {
		for (const FSCompletenessProvenanceStep &step : path) {
			if (step.relation_id == p_parent && step.exception_id.is_empty()) {
				return true;
			}
		}
	}
	return false;
}

static FSCompletenessStructuralFailure make_structural_failure(const String &p_stage,
		const String &p_detail, const String &p_case_id, const String &p_witness_id,
		const String &p_exception_id, Error p_error) {
	FSCompletenessStructuralFailure failure;
	failure.stage = p_stage;
	failure.detail = p_detail;
	failure.case_id = p_case_id;
	failure.witness_id = p_witness_id;
	failure.exception_id = p_exception_id;
	failure.error_code = p_error;
	return failure;
}

static String structural_failure_sort_key(const FSCompletenessStructuralFailure &p_failure) {
	return p_failure.stage + "|" + p_failure.exception_id + "|" + p_failure.witness_id + "|" +
			p_failure.case_id + "|" + p_failure.detail;
}

struct StructuralFailureSortsBefore {
	bool operator()(const FSCompletenessStructuralFailure &p_left,
			const FSCompletenessStructuralFailure &p_right) const {
		return structural_failure_sort_key(p_left) < structural_failure_sort_key(p_right);
	}
};

static void sort_structural_failures(Vector<FSCompletenessStructuralFailure> &r_failures) {
	r_failures.sort_custom<StructuralFailureSortsBefore>();
}

// True when p_coordinates names one string leaf of the manifest domain on every domain axis and on no
// other axis. Anything else could match several cells or none, so it is refused before it is resolved.
static bool witness_coordinates_are_resolvable(
		const FSCompletenessManifest &p_manifest, const Dictionary &p_coordinates) {
	if (uint32_t(p_coordinates.size()) != p_manifest.domain.size()) {
		return false;
	}
	for (const KeyValue<String, Vector<String>> &axis : p_manifest.domain) {
		const Variant value = p_coordinates.get(axis.key, Variant());
		if (value.get_type() != Variant::STRING || !axis.value.has(String(value))) {
			return false;
		}
	}
	return true;
}

static const FSCompletenessRuntimeResult *runtime_result_for(
		const FSCompletenessResolvedCell &p_cell, const FSCompletenessRuntimeBatch &p_batch) {
	const HashMap<String, FSCompletenessRuntimeResult> *results =
			p_batch.results_for_surface(p_cell.coordinates.get("surface", String()));
	return results == nullptr ? nullptr : results->getptr(p_cell.case_id);
}

static FSCompletenessRuntimeResult *runtime_result_for(
		const FSCompletenessResolvedCell &p_cell, FSCompletenessRuntimeBatch &r_batch) {
	HashMap<String, FSCompletenessRuntimeResult> *results =
			r_batch.results_for_surface(p_cell.coordinates.get("surface", String()));
	return results == nullptr ? nullptr : results->getptr(p_cell.case_id);
}

static bool runtime_result_identity_matches(
		const FSCompletenessResolvedCell &p_cell, const FSCompletenessRuntimeResult &p_result) {
	return p_result.case_id == p_cell.case_id &&
			p_result.surface == String(p_cell.coordinates.get("surface", String()));
}

static String make_finding_id(const String &p_case_id, const String &p_dimension) {
	return "fstcf-v1-" + (p_case_id + "|" + p_dimension).sha256_text().substr(0, 20);
}

static Dictionary parity_evidence(const Variant &p_text, const Variant &p_bytecode,
		const String &p_text_case_id, const String &p_bytecode_case_id) {
	Dictionary evidence;
	evidence["text"] = p_text;
	evidence["bytecode"] = p_bytecode;
	evidence["text_case_id"] = p_text_case_id;
	evidence["bytecode_case_id"] = p_bytecode_case_id;
	return evidence;
}

static Dictionary finding_report(const FSCompletenessFinding &p_finding) {
	Dictionary report;
	report["finding_id"] = p_finding.finding_id;
	report["case_id"] = p_finding.case_id;
	report["family"] = p_finding.family;
	report["dimension"] = p_finding.dimension;
	report["expected"] = p_finding.expected;
	report["actual"] = p_finding.actual;
	report["classification"] = p_finding.classification;
	report["artifact_path"] = p_finding.artifact_path;
	report["parity_evidence"] = p_finding.parity_evidence;
	report["issue_url"] = p_finding.issue_url;
	report["closure_packet_url"] = p_finding.closure_packet_url;
	Array permanent_test_paths;
	for (const String &path : p_finding.permanent_test_paths) {
		permanent_test_paths.push_back(path);
	}
	report["permanent_test_paths"] = permanent_test_paths;
	report["migrated_from"] = p_finding.migrated_from;
	Array resolved_case_ids;
	for (const String &case_id : p_finding.resolved_case_ids) {
		resolved_case_ids.push_back(case_id);
	}
	report["resolved_case_ids"] = resolved_case_ids;
	return report;
}

// Findings are ordered by their identity first so the report is stable across runs; the case and
// dimension keys only break ties between findings that share a finding ID.
struct FindingSortsBefore {
	bool operator()(const FSCompletenessFinding &p_left, const FSCompletenessFinding &p_right) const {
		if (p_left.finding_id != p_right.finding_id) {
			return p_left.finding_id < p_right.finding_id;
		}
		if (p_left.case_id != p_right.case_id) {
			return p_left.case_id < p_right.case_id;
		}
		return p_left.dimension < p_right.dimension;
	}
};

static void sort_findings(Vector<FSCompletenessFinding> &r_findings) {
	r_findings.sort_custom<FindingSortsBefore>();
}

// A bound witness must be observed by a runtime result that really is the witness cell's result.
// A missing or misidentified result is a harness defect: it would otherwise let a witness pass on
// evidence produced for a different case or surface.
static Error validate_witness_runtime_identity(const Vector<FSCompletenessWitnessBinding> &p_bindings,
		const FSCompletenessRuntimeBatch &p_batch, Vector<FSCompletenessStructuralFailure> &r_failures) {
	bool failed = false;
	for (const FSCompletenessWitnessBinding &binding : p_bindings) {
		const FSCompletenessRuntimeResult *actual = runtime_result_for(*binding.cell, p_batch);
		if (actual == nullptr) {
			r_failures.push_back(make_structural_failure(
					FSCompletenessStructuralStage::WITNESS_RUNTIME_RESULT_MISSING,
					"No runtime result was produced for the witness cell.", binding.cell->case_id,
					binding.witness_id, binding.exception_id, ERR_INVALID_DATA));
			failed = true;
			continue;
		}
		if (!runtime_result_identity_matches(*binding.cell, *actual)) {
			r_failures.push_back(make_structural_failure(
					FSCompletenessStructuralStage::WITNESS_RUNTIME_IDENTITY_MISMATCH,
					vformat("The runtime result reports case '%s' on surface '%s'.", actual->case_id,
							actual->surface),
					binding.cell->case_id, binding.witness_id, binding.exception_id, ERR_INVALID_DATA));
			failed = true;
		}
	}
	return failed ? ERR_INVALID_DATA : OK;
}

// Case-ID lookup over the programs rendered for one run. Built once, so resolving a cell's program
// stays constant-time however large the matrix grows.
class ProgramIndex {
	const Vector<FSCompletenessProgram> *programs = nullptr;
	HashMap<String, int> index_by_case_id;

public:
	void build(const Vector<FSCompletenessProgram> &p_programs) {
		programs = &p_programs;
		index_by_case_id.clear();
		for (int index = 0; index < p_programs.size(); index++) {
			index_by_case_id.insert(p_programs[index].case_id, index);
		}
	}

	const FSCompletenessProgram *find(const String &p_case_id) const {
		const int *index = index_by_case_id.getptr(p_case_id);
		return index == nullptr || programs == nullptr ? nullptr : &(*programs)[*index];
	}
};

static Dictionary runtime_status(bool p_passed, const String &p_status) {
	Dictionary status;
	status["passed"] = p_passed;
	status["status"] = p_status;
	return status;
}

static void append_finding(Vector<FSCompletenessFinding> &r_findings,
		const String &p_family, const String &p_case_id, const String &p_dimension,
		const Variant &p_expected, const Variant &p_actual, const String &p_artifact_path) {
	FSCompletenessFinding finding;
	finding.finding_id = make_finding_id(p_case_id, p_dimension);
	finding.case_id = p_case_id;
	finding.family = p_family;
	finding.dimension = p_dimension;
	finding.expected = p_expected;
	finding.actual = p_actual;
	finding.artifact_path = p_artifact_path;
	r_findings.push_back(finding);
}

static Dictionary aggregate_parity_evidence(const FSCompletenessResolvedCell &p_text_cell,
		const FSCompletenessResolvedCell &p_bytecode_cell,
		const FSCompletenessRuntimeResult &p_text, const FSCompletenessRuntimeResult &p_bytecode) {
	Dictionary evidence;
	evidence["text_case_id"] = p_text.case_id;
	evidence["bytecode_case_id"] = p_bytecode.case_id;
	// The surface-independent half of the comparison is the adapter's, so the report cannot disagree
	// with the pair-failure count the adapter derived from the same evidence.
	const FSCompletenessSurfaceEvidenceMismatch mismatch = compare_surface_evidence(p_text, p_bytecode);
	bool has_primary = false;
	if (mismatch.produced_output) {
		const Dictionary output = parity_evidence(
				p_text.produced_output, p_bytecode.produced_output, p_text.case_id, p_bytecode.case_id);
		evidence["output"] = output;
		evidence["text"] = p_text.produced_output;
		evidence["bytecode"] = p_bytecode.produced_output;
		has_primary = true;
	}
	if (mismatch.diagnostics) {
		evidence["diagnostics"] = parity_evidence(
				p_text.diagnostics, p_bytecode.diagnostics, p_text.case_id, p_bytecode.case_id);
	}
	if (mismatch.diagnostic_records) {
		evidence["diagnostic_records"] = parity_evidence(p_text.diagnostic_records,
				p_bytecode.diagnostic_records, p_text.case_id, p_bytecode.case_id);
	}
	if (mismatch.runtime_status) {
		evidence["runtime_status"] = parity_evidence(
				runtime_status(p_text.passed, p_text.status),
				runtime_status(p_bytecode.passed, p_bytecode.status), p_text.case_id, p_bytecode.case_id);
	}

	Vector<String> dimension_names = sorted_dimension_keys(p_text_cell.dimensions);
	for (const String &dimension_name : sorted_dimension_keys(p_bytecode_cell.dimensions)) {
		if (!dimension_names.has(dimension_name)) {
			dimension_names.push_back(dimension_name);
		}
	}
	dimension_names.sort();
	Dictionary dimensions;
	for (const String &dimension_name : dimension_names) {
		const Variant text_value = p_text.dimensions.get(dimension_name, Variant());
		const Variant bytecode_value = p_bytecode.dimensions.get(dimension_name, Variant());
		if (text_value == bytecode_value) {
			continue;
		}
		// Parity is agreement where the catalog says the surfaces must agree. A dimension whose expected
		// value differs between the two cells was declared surface-dependent by the manifest that
		// resolved them, and holding those two observations to each other would report the difference
		// the catalog asked for as a disagreement. The observations are still judged against their own
		// expectations, so a surface-dependent dimension is checked, just not against the other surface.
		const FSCompletenessResolvedDimension *text_expectation =
				p_text_cell.find_dimension(dimension_name);
		const FSCompletenessResolvedDimension *bytecode_expectation =
				p_bytecode_cell.find_dimension(dimension_name);
		if (text_expectation != nullptr && bytecode_expectation != nullptr &&
				text_expectation->expected != bytecode_expectation->expected) {
			continue;
		}
		dimensions[dimension_name] = parity_evidence(
				text_value, bytecode_value, p_text.case_id, p_bytecode.case_id);
		if (!has_primary) {
			evidence["text"] = text_value;
			evidence["bytecode"] = bytecode_value;
			has_primary = true;
		}
	}
	evidence["dimensions"] = dimensions;
	return evidence;
}

static bool classification_is_known(const String &p_classification) {
	return p_classification == "unclassified" || p_classification == "product_defect" ||
			p_classification == "specification_defect" || p_classification == "harness_defect" ||
			p_classification == "intentional_unsupported" || p_classification == "duplicate";
}

static bool object_has_exact_fields(const Dictionary &p_object, const Vector<String> &p_fields) {
	if (p_object.size() != p_fields.size()) {
		return false;
	}
	for (const String &field : p_fields) {
		if (!p_object.has(field)) {
			return false;
		}
	}
	return true;
}

static bool ledger_dimension_is_known(
		const FSCompletenessResolvedCell &p_cell, const String &p_dimension) {
	return p_cell.dimensions.has(p_dimension) ||
			FSCompletenessRunner::builtin_dimensions().has(p_dimension);
}

static bool is_permanent_test_path_form(const String &p_path) {
	const String filename = p_path.get_file();
	const String extension = filename.get_extension();
	if (p_path.begins_with("modules/foundry_script/tests/scripts/") &&
			(extension == "fs" || extension == "out")) {
		return true;
	}

	if (filename.begins_with("test_") && extension == "py") {
		if (p_path.begins_with("tests/python_build/") ||
				p_path.begins_with(".github/scripts/tests/") ||
				p_path.begins_with("scripts/tests/")) {
			return true;
		}
		const PackedStringArray components = p_path.split("/", false);
		if (components.size() >= 4 && components[0] == "tools" && components[2] == "tests") {
			return true;
		}
	}

	if (!filename.begins_with("test_") || (extension != "h" && extension != "cpp")) {
		return false;
	}
	const PackedStringArray components = p_path.split("/", false);
	for (int i = 0; i + 1 < components.size(); i++) {
		if (components[i] == "tests") {
			return true;
		}
	}
	return false;
}

static Error validate_permanent_test_path(const String &p_path, const String &p_repository_root,
		FSCompletenessTrackedFileProbe p_tracked_file_probe) {
	if (p_path.is_empty() || p_path.is_absolute_path() || p_path.simplify_path() != p_path ||
			p_path.contains("://") || p_path.contains("/../") ||
			p_path.ends_with("/..")) {
		return ERR_INVALID_DATA;
	}
	if (!is_permanent_test_path_form(p_path)) {
		return ERR_INVALID_DATA;
	}
	const String absolute_path = p_repository_root.path_join(p_path).simplify_path();
	if (!TemporaryProjectTree::is_strict_descendant(p_repository_root, absolute_path) ||
			!FileAccess::exists(absolute_path) || DirAccess::dir_exists_absolute(absolute_path)) {
		return ERR_INVALID_DATA;
	}
	Ref<DirAccess> filesystem = DirAccess::create(DirAccess::ACCESS_FILESYSTEM);
	if (filesystem.is_null()) {
		return ERR_UNAVAILABLE;
	}
	for (String current = absolute_path; current != p_repository_root; current = current.get_base_dir()) {
		if (current.is_empty() || filesystem->is_link(current)) {
			return ERR_UNAUTHORIZED;
		}
	}
	const String canonical_repository_root = TemporaryProjectTree::canonicalize_existing_path(p_repository_root);
	const String canonical_absolute_path = TemporaryProjectTree::canonicalize_existing_path(absolute_path);
	if (canonical_repository_root.is_empty() || canonical_absolute_path.is_empty() ||
			canonical_repository_root != p_repository_root || canonical_absolute_path != absolute_path ||
			!TemporaryProjectTree::is_strict_descendant(canonical_repository_root, canonical_absolute_path)) {
		return ERR_UNAUTHORIZED;
	}

	String git_output;
	int exit_code = -1;
	const FSCompletenessTrackedFileProbe tracked_file_probe =
			p_tracked_file_probe == nullptr ? Completeness::default_tracked_file_probe : p_tracked_file_probe;
	const Error probe_error = tracked_file_probe(p_repository_root, p_path, git_output, exit_code);
	if (probe_error != OK) {
		WARN_PRINT(vformat("Git tracked-file verification is unavailable for '%s' (error %d); "
						   "accepting canonical existing test path.",
				p_path, probe_error));
		return OK;
	}
	if (exit_code == 1) {
		return ERR_INVALID_DATA;
	}
	if (exit_code != 0) {
		WARN_PRINT(vformat("Git tracked-file verification is unavailable for '%s' (exit %d%s); "
						   "accepting canonical existing test path.",
				p_path, exit_code,
				git_output.strip_edges().is_empty() ? String() : ": " + git_output.strip_edges()));
	}
	return OK;
}

// The ledger's directory refusals are fatal, so each one is mapped to the error the run aborts with.
// A visible unexpected entry also prints, because an operator who put a file there needs to know why
// the whole run stopped.
static Error findings_directory_error(const String &p_directory, const Vector<JsonDirectoryError> &p_errors) {
	if (p_errors.is_empty()) {
		return ERR_INVALID_DATA;
	}
	const JsonDirectoryError &error = p_errors[0];
	switch (error.kind) {
		case JsonDirectoryErrorKind::DIRECTORY_UNOPENABLE:
		case JsonDirectoryErrorKind::DIRECTORY_UNLISTABLE:
			return error.error_code;
		case JsonDirectoryErrorKind::FILESYSTEM_UNAVAILABLE:
			return ERR_UNAVAILABLE;
		case JsonDirectoryErrorKind::SUBDIRECTORY_ENTRY:
			ERR_PRINT(vformat("%s: unexpected findings directory entry '%s'", p_directory, error.entry));
			return ERR_INVALID_DATA;
		case JsonDirectoryErrorKind::NON_JSON_ENTRY:
			ERR_PRINT(vformat("%s: unexpected non-JSON findings entry '%s'", p_directory, error.entry));
			return ERR_INVALID_DATA;
		default:
			break;
	}
	return ERR_UNAUTHORIZED;
}

static Error load_findings_ledger(const String &p_directory, const String &p_family,
		const FSCompletenessResolution &p_resolution, const HashSet<String> &p_current_ids,
		const FSCompletenessMigrations &p_migrations, const String &p_repository_root,
		FSCompletenessTrackedFileProbe p_tracked_file_probe,
		HashMap<String, FSCompletenessFinding> &r_findings) {
	r_findings.clear();
	Vector<String> files;
	Vector<JsonDirectoryError> directory_errors;
	JsonDirectoryPolicy policy;
	policy.require_canonical_directory = true;
	policy.require_canonical_children = true;
	policy.require_non_empty = false;
	// The ledger is an all-or-nothing input: the first refused entry aborts the run, so enumerating
	// the rest would only produce diagnostics for a run that is already over.
	policy.stop_at_first_error = true;
	if (enumerate_json_directory(p_directory, policy, files, directory_errors) != OK) {
		return findings_directory_error(p_directory, directory_errors);
	}

	const Vector<String> required_fields = {
		"schema_version",
		"finding_id",
		"case_id",
		"family",
		"dimension",
		"classification",
		"issue_url",
		"closure_packet_url",
		"permanent_test_paths",
	};
	HashSet<String> finding_ids;
	for (const String &file_path : files) {
		Error read_error = OK;
		const String source = FileAccess::get_file_as_string(file_path, &read_error);
		if (read_error != OK) {
			return read_error;
		}
		Variant data;
		Vector<String> parse_errors;
		const Error parse_error = parse_type_completeness_json(source, file_path, data, parse_errors);
		if (parse_error != OK) {
			return parse_error;
		}
		if (data.get_type() != Variant::DICTIONARY) {
			return ERR_INVALID_DATA;
		}
		const Dictionary record = data;
		int record_schema_version = 0;
		if (!object_has_exact_fields(record, required_fields) ||
				!parse_json_integer(record["schema_version"], record_schema_version) ||
				record_schema_version != 1) {
			return ERR_INVALID_DATA;
		}
		for (const String &field : { String("finding_id"), String("case_id"), String("family"),
					 String("dimension"), String("classification"), String("issue_url"),
					 String("closure_packet_url") }) {
			if (record[field].get_type() != Variant::STRING || String(record[field]).is_empty()) {
				return ERR_INVALID_DATA;
			}
		}

		FSCompletenessFinding finding;
		finding.finding_id = record["finding_id"];
		finding.case_id = record["case_id"];
		finding.family = record["family"];
		finding.dimension = record["dimension"];
		finding.classification = record["classification"];
		finding.issue_url = record["issue_url"];
		finding.closure_packet_url = record["closure_packet_url"];
		if (file_path.get_file().get_basename() != finding.finding_id ||
				finding_ids.has(finding.finding_id) || finding.family != p_family ||
				!classification_is_known(finding.classification) ||
				(!finding.issue_url.begins_with("https://") && !finding.issue_url.begins_with("http://")) ||
				(!finding.closure_packet_url.begins_with("https://") &&
						!finding.closure_packet_url.begins_with("http://"))) {
			return ERR_INVALID_DATA;
		}
		finding_ids.insert(finding.finding_id);

		if (record["permanent_test_paths"].get_type() != Variant::ARRAY) {
			return ERR_INVALID_DATA;
		}
		const Array paths = record["permanent_test_paths"];
		if (paths.is_empty()) {
			return ERR_INVALID_DATA;
		}
		HashSet<String> observed_paths;
		for (int path_index = 0; path_index < paths.size(); path_index++) {
			if (paths[path_index].get_type() != Variant::STRING || String(paths[path_index]).is_empty() ||
					observed_paths.has(paths[path_index])) {
				return ERR_INVALID_DATA;
			}
			const String path = paths[path_index];
			if (validate_permanent_test_path(path, p_repository_root, p_tracked_file_probe) != OK) {
				return ERR_INVALID_DATA;
			}
			observed_paths.insert(path);
			finding.permanent_test_paths.push_back(path);
		}

		const String ledger_case_id = finding.case_id;
		Vector<String> resolved_case_ids;
		if (p_current_ids.has(ledger_case_id)) {
			resolved_case_ids.push_back(ledger_case_id);
		} else {
			resolved_case_ids = p_migrations.resolve(ledger_case_id);
			if (resolved_case_ids.is_empty()) {
				return ERR_INVALID_DATA;
			}
			resolved_case_ids.sort();
			finding.migrated_from = ledger_case_id;
		}
		for (const String &resolved_case_id : resolved_case_ids) {
			if (!p_current_ids.has(resolved_case_id)) {
				return ERR_INVALID_DATA;
			}
			finding.resolved_case_ids.push_back(resolved_case_id);
		}
		for (const String &resolved_case_id : resolved_case_ids) {
			const FSCompletenessResolvedCell *cell = p_resolution.find_cell_by_id(resolved_case_id);
			if (cell == nullptr || !ledger_dimension_is_known(*cell, finding.dimension)) {
				return ERR_INVALID_DATA;
			}
			FSCompletenessFinding resolved_finding = finding;
			resolved_finding.case_id = resolved_case_id;
			const String reconciliation_key = resolved_case_id + "|" + finding.dimension;
			if (r_findings.has(reconciliation_key)) {
				return ERR_INVALID_DATA;
			}
			r_findings[reconciliation_key] = resolved_finding;
		}
	}
	return OK;
}

static Error canonicalize_owned_future_path(const String &p_canonical_root,
		const String &p_target, String &r_canonical_target) {
	r_canonical_target.clear();
	if (p_target.simplify_path() != p_target ||
			!TemporaryProjectTree::is_strict_descendant(p_canonical_root, p_target)) {
		return ERR_UNAUTHORIZED;
	}
	Ref<DirAccess> filesystem = DirAccess::create(DirAccess::ACCESS_FILESYSTEM);
	if (filesystem.is_null()) {
		return ERR_UNAVAILABLE;
	}
	String existing_ancestor = p_target;
	while (!filesystem->file_exists(existing_ancestor) && !filesystem->dir_exists(existing_ancestor)) {
		const String parent = existing_ancestor.get_base_dir();
		if (parent.is_empty() || parent == existing_ancestor ||
				(parent != p_canonical_root &&
						!TemporaryProjectTree::is_strict_descendant(p_canonical_root, parent))) {
			return ERR_UNAUTHORIZED;
		}
		existing_ancestor = parent;
	}
	const String canonical_ancestor = TemporaryProjectTree::canonicalize_existing_path(existing_ancestor);
	if (filesystem->is_link(existing_ancestor) || canonical_ancestor.is_empty() ||
			canonical_ancestor != existing_ancestor) {
		return ERR_UNAUTHORIZED;
	}
	r_canonical_target = (canonical_ancestor + p_target.substr(existing_ancestor.length())).simplify_path();
	return r_canonical_target == p_target ? OK : ERR_UNAUTHORIZED;
}

static Error validate_owned_report_path(const String &p_canonical_scratch_root,
		const String &p_catalog_root, const String &p_report_path) {
	if (p_report_path.simplify_path() != p_report_path ||
			p_report_path.get_extension() != "json" ||
			!TemporaryProjectTree::is_strict_descendant(p_canonical_scratch_root, p_report_path)) {
		return ERR_UNAUTHORIZED;
	}
	const String artifact_root = p_canonical_scratch_root.path_join("report-artifacts");
	if (p_report_path == artifact_root ||
			TemporaryProjectTree::is_strict_descendant(artifact_root, p_report_path)) {
		return ERR_UNAUTHORIZED;
	}

	Error catalog_open_error = OK;
	Ref<DirAccess> catalog_directory = DirAccess::open(p_catalog_root, &catalog_open_error);
	if (catalog_directory.is_null()) {
		return catalog_open_error == OK ? ERR_CANT_OPEN : catalog_open_error;
	}
	const String lexical_catalog_root =
			catalog_directory->get_current_dir().replace("\\", "/").simplify_path();
	const String canonical_catalog_root = TemporaryProjectTree::canonicalize_existing_path(lexical_catalog_root);
	if (canonical_catalog_root.is_empty() || canonical_catalog_root != lexical_catalog_root) {
		return ERR_UNAUTHORIZED;
	}
	String canonical_artifact_root;
	const Error artifact_root_error = canonicalize_owned_future_path(
			p_canonical_scratch_root, artifact_root, canonical_artifact_root);
	if (artifact_root_error != OK) {
		return artifact_root_error;
	}
	if (canonical_catalog_root == canonical_artifact_root ||
			TemporaryProjectTree::is_strict_descendant(canonical_artifact_root, canonical_catalog_root) ||
			TemporaryProjectTree::is_strict_descendant(canonical_catalog_root, canonical_artifact_root)) {
		return ERR_UNAUTHORIZED;
	}
	if (p_report_path == canonical_catalog_root ||
			TemporaryProjectTree::is_strict_descendant(canonical_catalog_root, p_report_path)) {
		return ERR_UNAUTHORIZED;
	}
	Ref<DirAccess> filesystem = DirAccess::create(DirAccess::ACCESS_FILESYSTEM);
	if (filesystem.is_null()) {
		return ERR_UNAVAILABLE;
	}

	String current = p_report_path;
	bool is_report_target = true;
	while (current != p_canonical_scratch_root) {
		if (!TemporaryProjectTree::is_strict_descendant(p_canonical_scratch_root, current)) {
			return ERR_UNAUTHORIZED;
		}
		const bool is_directory = filesystem->dir_exists(current);
		const bool exists = is_directory || filesystem->file_exists(current);
		if (filesystem->is_link(current)) {
			return ERR_UNAUTHORIZED;
		}
		if (exists) {
			String canonical_path;
			if (TemporaryProjectTree::resolve_existing_owned_path(current, canonical_path) != OK ||
					canonical_path != current) {
				return ERR_UNAUTHORIZED;
			}
			if (is_report_target && is_directory) {
				return ERR_CANT_CREATE;
			}
		}
		const String parent = current.get_base_dir();
		if (parent.is_empty() || parent == current) {
			return ERR_UNAUTHORIZED;
		}
		current = parent;
		is_report_target = false;
	}
	return OK;
}

class ReportArtifactScope {
	String artifact_root;
	// The surfaces this run executes, taken from the run rather than enumerated again here. An artifact
	// tree that offered a directory for a surface the run never observed would advertise evidence that
	// cannot exist.
	HashSet<String> staged_surfaces;
	Vector<String> created_files;
	Vector<String> created_directories;
	bool committed = false;

	Error ensure_directory(const String &p_canonical_scratch_root, const String &p_path) {
		Ref<DirAccess> filesystem = DirAccess::create(DirAccess::ACCESS_FILESYSTEM);
		if (filesystem.is_null()) {
			return ERR_UNAVAILABLE;
		}
		if (filesystem->is_link(p_path)) {
			return ERR_UNAUTHORIZED;
		}
		if (!filesystem->dir_exists(p_path)) {
			if (filesystem->file_exists(p_path)) {
				return ERR_ALREADY_EXISTS;
			}
			const Error create_error = DirAccess::make_dir_absolute(p_path);
			if (create_error != OK) {
				return create_error;
			}
			created_directories.push_back(p_path);
		}
		String canonical_path;
		if (TemporaryProjectTree::resolve_existing_owned_path(p_path, canonical_path) != OK ||
				canonical_path != p_path ||
				!TemporaryProjectTree::is_strict_descendant(p_canonical_scratch_root, canonical_path)) {
			return ERR_UNAUTHORIZED;
		}
		return OK;
	}

	Error validate_artifact_target(const String &p_canonical_scratch_root,
			const FSCompletenessResolvedCell &p_cell, const String &p_path) const {
		const String surface = p_cell.coordinates.get("surface", String());
		if (!staged_surfaces.has(surface)) {
			return ERR_INVALID_DATA;
		}
		const String surface_root = artifact_root.path_join(surface);
		if (p_path.simplify_path() != p_path || p_path.get_base_dir() != surface_root ||
				!TemporaryProjectTree::is_strict_descendant(surface_root, p_path) ||
				!TemporaryProjectTree::is_strict_descendant(p_canonical_scratch_root, p_path)) {
			return ERR_UNAUTHORIZED;
		}
		String canonical_surface_root;
		if (TemporaryProjectTree::resolve_existing_owned_path(
					surface_root, canonical_surface_root) != OK ||
				canonical_surface_root != surface_root) {
			return ERR_UNAUTHORIZED;
		}
		Ref<DirAccess> filesystem = DirAccess::create(DirAccess::ACCESS_FILESYSTEM);
		if (filesystem.is_null()) {
			return ERR_UNAVAILABLE;
		}
		if (filesystem->is_link(p_path)) {
			return ERR_UNAUTHORIZED;
		}
		if (filesystem->file_exists(p_path) || filesystem->dir_exists(p_path)) {
			String canonical_path;
			if (TemporaryProjectTree::resolve_existing_owned_path(p_path, canonical_path) != OK ||
					canonical_path != p_path || canonical_path.get_base_dir() != canonical_surface_root) {
				return ERR_UNAUTHORIZED;
			}
		}
		return OK;
	}

public:
	~ReportArtifactScope() {
		if (committed) {
			return;
		}
		for (int index = created_files.size() - 1; index >= 0; index--) {
			DirAccess::remove_absolute(created_files[index]);
		}
		for (int index = created_directories.size() - 1; index >= 0; index--) {
			DirAccess::remove_absolute(created_directories[index]);
		}
	}

	Error stage(const String &p_canonical_scratch_root, const Vector<String> &p_surfaces,
			const Vector<const FSCompletenessResolvedCell *> &p_cells,
			const ProgramIndex &p_program_index,
			FSCompletenessPersistedWriteHook p_persisted_write_hook) {
		staged_surfaces.clear();
		for (const String &surface : p_surfaces) {
			staged_surfaces.insert(surface);
		}
		artifact_root = p_canonical_scratch_root.path_join("report-artifacts");
		if (!TemporaryProjectTree::is_strict_descendant(p_canonical_scratch_root, artifact_root)) {
			return ERR_UNAUTHORIZED;
		}
		Error error = ensure_directory(p_canonical_scratch_root, artifact_root);
		if (error != OK) {
			return error;
		}
		for (const String &surface : p_surfaces) {
			error = ensure_directory(p_canonical_scratch_root, artifact_root.path_join(surface));
			if (error != OK) {
				return error;
			}
		}

		Ref<DirAccess> filesystem = DirAccess::create(DirAccess::ACCESS_FILESYSTEM);
		if (filesystem.is_null()) {
			return ERR_UNAVAILABLE;
		}
		for (const FSCompletenessResolvedCell *selected_cell : p_cells) {
			const FSCompletenessResolvedCell &cell = *selected_cell;
			const FSCompletenessProgram *program = p_program_index.find(cell.case_id);
			if (program == nullptr) {
				return ERR_INVALID_DATA;
			}
			const String path = artifact_path(cell);
			error = validate_artifact_target(p_canonical_scratch_root, cell, path);
			if (error != OK) {
				return error;
			}
			if (filesystem->file_exists(path)) {
				String canonical_path;
				Error read_error = OK;
				const String source = FileAccess::get_file_as_string(path, &read_error);
				if (TemporaryProjectTree::resolve_existing_owned_path(path, canonical_path) != OK ||
						canonical_path != path || read_error != OK || source != program->source) {
					return ERR_ALREADY_EXISTS;
				}
				continue;
			}
			error = validate_artifact_target(p_canonical_scratch_root, cell, path);
			if (error != OK) {
				return error;
			}
			Error open_error = OK;
			Ref<FileAccess> file = FileAccess::open(path, FileAccess::WRITE | FileAccess::WRITE_EXCL, &open_error);
			if (file.is_null()) {
				return open_error == OK ? ERR_CANT_CREATE : open_error;
			}
			created_files.push_back(path);
			if (!file->store_string(program->source)) {
				const Error write_error = file->get_error();
				file->close();
				file.unref();
				return write_error == OK ? ERR_CANT_CREATE : write_error;
			}
			file->flush();
			const Error write_error = file->get_error();
			file->close();
			file.unref();
			if (write_error != OK) {
				return write_error;
			}
			if (p_persisted_write_hook != nullptr) {
				p_persisted_write_hook(path);
			}
			Error read_error = OK;
			const String persisted_source = FileAccess::get_file_as_string(path, &read_error);
			if (read_error != OK) {
				return read_error;
			}
			if (persisted_source != program->source) {
				return ERR_FILE_CORRUPT;
			}
			String canonical_path;
			if (TemporaryProjectTree::resolve_existing_owned_path(path, canonical_path) != OK ||
					canonical_path != path) {
				return ERR_UNAUTHORIZED;
			}
		}
		return OK;
	}

	String artifact_path(const FSCompletenessResolvedCell &p_cell) const {
		return artifact_root.path_join(p_cell.coordinates.get("surface", String())).path_join(p_cell.case_id + ".fs");
	}

	void commit() { committed = true; }
};

class ReportTemporaryFile {
	String path;
	bool active = false;

public:
	~ReportTemporaryFile() {
		if (active) {
			DirAccess::remove_absolute(path);
		}
	}

	void retain_for_cleanup(const String &p_path) {
		path = p_path;
		active = true;
	}

	void release() { active = false; }
};

static Error replace_report_file(const String &p_temporary_path, const String &p_report_path) {
#ifdef WINDOWS_ENABLED
	const Char16String temporary_utf16 = p_temporary_path.utf16();
	const Char16String report_utf16 = p_report_path.utf16();
	if (FileAccess::exists(p_report_path)) {
		return ReplaceFileW((LPCWSTR)report_utf16.get_data(), (LPCWSTR)temporary_utf16.get_data(),
					   nullptr, REPLACEFILE_IGNORE_MERGE_ERRORS | REPLACEFILE_IGNORE_ACL_ERRORS,
					   nullptr, nullptr) != 0
				? OK
				: FAILED;
	}
	return MoveFileW((LPCWSTR)temporary_utf16.get_data(), (LPCWSTR)report_utf16.get_data()) != 0 ? OK : FAILED;
#else
	// The temp file is a sibling of the destination, so the platform rename is one
	// process-visible atomic replacement. This does not promise power-loss durability.
	return DirAccess::rename_absolute(p_temporary_path, p_report_path);
#endif
}

static Error write_report_atomically(const String &p_canonical_scratch_root,
		const String &p_catalog_root, const String &p_report_path, const Dictionary &p_report,
		FSCompletenessPersistedWriteHook p_persisted_write_hook) {
	Error validation_error = validate_owned_report_path(
			p_canonical_scratch_root, p_catalog_root, p_report_path);
	if (validation_error != OK) {
		return validation_error;
	}
	const String contents = JSON::stringify(p_report, "\t", false, true) + "\n";
	String temporary_path;
	Ref<FileAccess> temporary;
	ReportTemporaryFile temporary_cleanup;
	for (int attempt = 0; attempt < 128; attempt++) {
		temporary_path = p_report_path + vformat(".tmp.%d.%d", OS::get_singleton()->get_process_id(), attempt);
		Error open_error = OK;
		temporary = FileAccess::open(
				temporary_path, FileAccess::WRITE | FileAccess::WRITE_EXCL, &open_error);
		if (temporary.is_valid()) {
			break;
		}
		if (open_error != ERR_ALREADY_EXISTS && open_error != ERR_ALREADY_IN_USE) {
			return open_error == OK ? ERR_CANT_CREATE : open_error;
		}
	}
	if (temporary.is_null()) {
		return ERR_ALREADY_EXISTS;
	}
	temporary_cleanup.retain_for_cleanup(temporary_path);
	if (!temporary->store_string(contents)) {
		const Error write_error = temporary->get_error();
		temporary->close();
		temporary.unref();
		return write_error == OK ? ERR_CANT_CREATE : write_error;
	}
	temporary->flush();
	const Error write_error = temporary->get_error();
	temporary->close();
	temporary.unref();
	if (write_error != OK) {
		return write_error;
	}
	if (p_persisted_write_hook != nullptr) {
		p_persisted_write_hook(temporary_path);
	}
	Error read_error = OK;
	const String persisted_contents = FileAccess::get_file_as_string(temporary_path, &read_error);
	if (read_error != OK) {
		return read_error;
	}
	if (persisted_contents != contents) {
		return ERR_FILE_CORRUPT;
	}
	String canonical_temporary_path;
	if (TemporaryProjectTree::resolve_existing_owned_path(
				temporary_path, canonical_temporary_path) != OK ||
			canonical_temporary_path != temporary_path) {
		return ERR_UNAUTHORIZED;
	}
	validation_error = validate_owned_report_path(
			p_canonical_scratch_root, p_catalog_root, p_report_path);
	if (validation_error != OK) {
		return validation_error;
	}
	canonical_temporary_path.clear();
	if (TemporaryProjectTree::resolve_existing_owned_path(
				temporary_path, canonical_temporary_path) != OK ||
			canonical_temporary_path != temporary_path) {
		return ERR_UNAUTHORIZED;
	}
	const Error rename_error = replace_report_file(temporary_path, p_report_path);
	if (rename_error == OK) {
		temporary_cleanup.release();
	}
	return rename_error;
}

static Error resolve_or_create_owned_scratch_root(const String &p_scratch_root, String &r_canonical_root) {
	r_canonical_root.clear();
	const String owned_root = TemporaryProjectTree::get_test_scratch_root();
	const String requested_root = p_scratch_root.simplify_path();
	if (owned_root.is_empty() || requested_root != p_scratch_root ||
			!TemporaryProjectTree::is_strict_descendant(owned_root, requested_root)) {
		return ERR_UNAUTHORIZED;
	}
	if (DirAccess::dir_exists_absolute(requested_root)) {
		const Error resolve_error = TemporaryProjectTree::resolve_existing_owned_path(
				requested_root, r_canonical_root);
		return resolve_error == OK && r_canonical_root == requested_root ? OK : ERR_UNAUTHORIZED;
	}
	if (FileAccess::exists(requested_root)) {
		return ERR_ALREADY_EXISTS;
	}

	String existing_ancestor = requested_root.get_base_dir();
	while (existing_ancestor != owned_root && !DirAccess::dir_exists_absolute(existing_ancestor)) {
		if (FileAccess::exists(existing_ancestor)) {
			return ERR_CANT_CREATE;
		}
		const String parent = existing_ancestor.get_base_dir();
		if (parent.is_empty() || parent == existing_ancestor ||
				!TemporaryProjectTree::is_strict_descendant(owned_root, existing_ancestor)) {
			return ERR_UNAUTHORIZED;
		}
		existing_ancestor = parent;
	}
	if (existing_ancestor != owned_root) {
		String canonical_ancestor;
		if (TemporaryProjectTree::resolve_existing_owned_path(
					existing_ancestor, canonical_ancestor) != OK ||
				canonical_ancestor != existing_ancestor) {
			return ERR_UNAUTHORIZED;
		}
	}

	const Error create_error = DirAccess::make_dir_recursive_absolute(requested_root);
	if (create_error != OK) {
		return create_error;
	}
	const Error resolve_error = TemporaryProjectTree::resolve_existing_owned_path(
			requested_root, r_canonical_root);
	return resolve_error == OK && r_canonical_root == requested_root ? OK : ERR_UNAUTHORIZED;
}

static Dictionary expected_dimensions(const FSCompletenessResolvedCell &p_cell) {
	Dictionary expected;
	for (const String &dimension_name : sorted_dimension_keys(p_cell.dimensions)) {
		expected[dimension_name] = p_cell.dimensions[dimension_name].expected;
	}
	return expected;
}

static Dictionary observed_dimensions(const FSCompletenessResolvedCell &p_cell,
		const FSCompletenessRuntimeResult &p_actual) {
	Dictionary observed;
	for (const String &dimension_name : sorted_dimension_keys(p_cell.dimensions)) {
		observed[dimension_name] = p_actual.dimensions.get(dimension_name, Variant());
	}
	return observed;
}

// Readings an adapter recorded for a human rather than for a verdict. A closed dimension vocabulary
// says which outcome a cell reached but never what the surfaces actually rendered, so a disagreement
// would otherwise be a finding nobody could classify without re-running it. Keys under the reserved
// `evidence.` prefix are published verbatim and are never compared, so adding one can change what a
// report explains but never what it decides.
static Dictionary adapter_evidence(const FSCompletenessRuntimeResult &p_actual) {
	static const String prefix = "evidence.";
	Dictionary evidence;
	for (const String &key : Completeness::sorted_dictionary_keys(p_actual.dimensions)) {
		if (key.begins_with(prefix)) {
			evidence[key.substr(prefix.length())] = p_actual.dimensions[key];
		}
	}
	return evidence;
}

static Dictionary canonical_provenance(const FSCompletenessResolvedCell &p_cell) {
	Dictionary provenance;
	for (const String &dimension_name : sorted_dimension_keys(p_cell.dimensions)) {
		provenance[dimension_name] = provenance_report(p_cell.dimensions[dimension_name].canonical_provenance);
	}
	return provenance;
}

static Dictionary all_agreeing_provenance(const FSCompletenessResolvedCell &p_cell) {
	Dictionary provenance;
	for (const String &dimension_name : sorted_dimension_keys(p_cell.dimensions)) {
		provenance[dimension_name] =
				agreeing_provenance_report(p_cell.dimensions[dimension_name].agreeing_provenance);
	}
	return provenance;
}

// The strongest severity the adapter recorded, independent of whether an annotation kept the
// diagnostic out of the reported list. A case expected to be rejected that only carries warnings has
// regressed even when the analyzer still reports something.
static String diagnostic_severity_profile(const FSCompletenessObservation &p_observation) {
	bool has_warning = false;
	for (int index = 0; index < p_observation.diagnostic_records.size(); index++) {
		const Dictionary record = p_observation.diagnostic_records[index];
		const String severity = record.get("severity", String());
		if (severity == "error") {
			return "error";
		}
		if (severity == "warning") {
			has_warning = true;
		}
	}
	return has_warning ? "warning" : "none";
}

// The one dimension whose expected value can be an analyzer warning. A build whose analyzer emits none
// observes "no warning" for it whatever the product does, so that observation decides nothing.
static const char *DIAGNOSTIC_SEVERITY_DIMENSION = "diagnostic_severity";

static bool observed_expected_dimensions(const FSCompletenessResolvedCell &p_cell,
		const FSCompletenessRuntimeResult &p_actual, bool p_skip_diagnostic_severity) {
	for (const KeyValue<String, FSCompletenessResolvedDimension> &dimension : p_cell.dimensions) {
		if (p_skip_diagnostic_severity && dimension.key == DIAGNOSTIC_SEVERITY_DIMENSION) {
			continue;
		}
		if (p_actual.dimensions.get(dimension.key, Variant()) != dimension.value.expected) {
			return false;
		}
	}
	return true;
}

struct CellSortsBeforeByID {
	bool operator()(const FSCompletenessResolvedCell *p_left, const FSCompletenessResolvedCell *p_right) const {
		return p_left->case_id < p_right->case_id;
	}
};

static void sort_cells_by_id(Vector<const FSCompletenessResolvedCell *> &r_cells) {
	r_cells.sort_custom<CellSortsBeforeByID>();
}

// Wall-clock cost of each stage of one run, in microseconds. Reported as milliseconds so a budget can
// be read off the report directly. The report stage covers assembling the report document; the atomic
// write that publishes it cannot be part of the numbers the document itself carries.
struct StageTimings {
	uint64_t load = 0;
	uint64_t resolve = 0;
	uint64_t render = 0;
	uint64_t analyze = 0;
	uint64_t execute = 0;
	uint64_t report = 0;
	uint64_t total = 0;

	static uint64_t now() {
		return OS::get_singleton() == nullptr ? 0 : OS::get_singleton()->get_ticks_usec();
	}

	// Microseconds elapsed since p_start, clamped at zero so a clock that moves backwards can never
	// publish a negative duration.
	static uint64_t since(uint64_t p_start) {
		const uint64_t current = now();
		return current < p_start ? 0 : current - p_start;
	}

	Dictionary to_report() const {
		Dictionary milliseconds;
		milliseconds["load"] = double(load) / 1000.0;
		milliseconds["resolve"] = double(resolve) / 1000.0;
		milliseconds["render"] = double(render) / 1000.0;
		milliseconds["analyze"] = double(analyze) / 1000.0;
		milliseconds["execute"] = double(execute) / 1000.0;
		milliseconds["report"] = double(report) / 1000.0;
		milliseconds["total"] = double(total) / 1000.0;
		return milliseconds;
	}
};

// A run that outlives its budget is a harness-level failure, not a product observation: the evidence
// it would have published is incomplete, so it publishes a partial report naming the stage it was in
// and nothing that could be read as a verdict about the product.
// Turns a published report document into the structural failure a crossed budget makes it. The
// evidence the run did gather stays exactly as it was observed; only the verdict changes, and the
// record naming the crossing is appended last because that is when it happened.
static Dictionary document_with_timeout_verdict(const Dictionary &p_document, const String &p_detail) {
	Dictionary document = p_document.duplicate(true);
	FSCompletenessStructuralFailure failure =
			make_structural_failure(FSCompletenessStructuralStage::RUN_TIMEOUT, p_detail, String(), String(), String(), ERR_TIMEOUT);
	Array failures = document.get("structural_failures", Array());
	failures.push_back(FSCompletenessRunner::structural_failure_report(failure));
	document["structural_failures"] = failures;
	document["success"] = false;
	document["outcome"] = "structural_failure";
	return document;
}

static Dictionary timeout_report(const String &p_family, const StageTimings &p_timings,
		const Vector<FSCompletenessStructuralFailure> &p_failures) {
	Dictionary report;
	report["schema_version"] = 1.0;
	report["family"] = p_family;
	report["success"] = false;
	report["cell_count"] = 0.0;
	Dictionary executed_by_surface;
	executed_by_surface["text"] = 0.0;
	executed_by_surface["bytecode"] = 0.0;
	report["executed_by_surface"] = executed_by_surface;
	report["coverage_by_chain_length"] = Dictionary();
	report["coverage_by_dimension"] = Dictionary();
	report["uncovered_required_dimensions"] = 0.0;
	report["text_bytecode_parity_failures"] = 0.0;
	report["outcome"] = "structural_failure";
	report["findings"] = Array();
	Array structural_failures;
	for (const FSCompletenessStructuralFailure &failure : p_failures) {
		structural_failures.push_back(FSCompletenessRunner::structural_failure_report(failure));
	}
	report["structural_failures"] = structural_failures;
	Dictionary ledger;
	ledger["reconciled"] = Array();
	ledger["stale"] = Array();
	report["ledger"] = ledger;
	report["exceptions"] = Array();
	report["cases"] = Array();
	report["timings_ms"] = p_timings.to_report();
	report["configuration"] = FSCompletenessRunner::configuration_report();
	return report;
}

// The document a build publishes for a family whose adapter it does not compile. Every cell the
// matrix resolves is published as not covered, carrying the reason nothing could judge it: a build
// that observes nothing about a family has no verdict for it, and leaving the cells out would let a
// narrower configuration publish a document that looks like a clean run of a smaller matrix.
static Dictionary adapter_unavailable_report(const String &p_family, const String &p_published_surface,
		const Vector<const FSCompletenessResolvedCell *> &p_cells, const FSCompletenessManifest &p_manifest,
		int p_uncovered_dimension_count, const StageTimings &p_timings) {
	Vector<const FSCompletenessResolvedCell *> sorted_cells = p_cells;
	sort_cells_by_id(sorted_cells);
	Array cases;
	for (const FSCompletenessResolvedCell *cell : sorted_cells) {
		if (!p_published_surface.is_empty() &&
				String(cell->coordinates.get("surface", String())) != p_published_surface) {
			continue;
		}
		Dictionary case_report;
		case_report["case_id"] = cell->case_id;
		case_report["coordinates"] = sorted_dictionary_copy(cell->coordinates);
		case_report["expected"] = expected_dimensions(*cell);
		case_report["actual"] = Dictionary();
		case_report["canonical_provenance"] = canonical_provenance(*cell);
		case_report["agreeing_provenance"] = all_agreeing_provenance(*cell);
		case_report["status"] = "not_covered";
		case_report["passed"] = false;
		case_report["category"] = "not_covered";
		case_report["not_covered_reason"] =
				String(FSCompletenessNotCoveredReason::ADAPTER_UNAVAILABLE_IN_CONFIGURATION);
		case_report["runtime_passed"] = false;
		case_report["runtime_status"] = String();
		case_report["diagnostics"] = Array();
		case_report["diagnostic_records"] = Array();
		case_report["diagnostic_severity"] = Dictionary();
		case_report["produced_output"] = String();
		case_report["expected_output"] = String();
		case_report["artifact_path"] = String();
		case_report["parity_evidence"] = Dictionary();
		cases.push_back(case_report);
	}
	Array exceptions;
	for (const FSCompletenessException &exception : p_manifest.exceptions) {
		Dictionary exception_report;
		exception_report["exception_id"] = exception.id;
		exception_report["parent"] = exception.parent;
		// A carve-out this build bound no cell to is not a carve-out nobody exercises: it is one this
		// build could not reach, and it carries the same reason its cells do.
		exception_report["witnessed"] = false;
		exception_report["not_covered_reason"] =
				String(FSCompletenessNotCoveredReason::ADAPTER_UNAVAILABLE_IN_CONFIGURATION);
		exception_report["positive_witnesses"] = Array();
		exception_report["boundary_witnesses"] = Array();
		exceptions.push_back(exception_report);
	}

	Dictionary report;
	report["schema_version"] = 1.0;
	report["family"] = p_family;
	// Not covered is neither a pass nor a failure. The run did everything it could do in this build,
	// so it is a success; what it could not observe is published cell by cell rather than in the
	// verdict, and `--require-tooling` is what turns an unobservable family into a refusal.
	report["success"] = true;
	report["cell_count"] = double(p_cells.size());
	Dictionary executed_by_surface;
	executed_by_surface["text"] = 0.0;
	executed_by_surface["bytecode"] = 0.0;
	report["executed_by_surface"] = executed_by_surface;
	report["coverage_by_chain_length"] = Dictionary();
	report["coverage_by_dimension"] = Dictionary();
	report["uncovered_required_dimensions"] = double(p_uncovered_dimension_count);
	report["text_bytecode_parity_failures"] = 0.0;
	report["outcome"] = "not_covered";
	report["findings"] = Array();
	report["structural_failures"] = Array();
	Dictionary ledger;
	ledger["reconciled"] = Array();
	ledger["stale"] = Array();
	report["ledger"] = ledger;
	report["exceptions"] = exceptions;
	report["cases"] = cases;
	report["published_surface"] = p_published_surface;
	report["census"] = Dictionary();
	report["configuration"] = FSCompletenessRunner::configuration_report();
	report["unconfirmed_census_witnesses"] = Array();
	report["timings_ms"] = p_timings.to_report();
	return report;
}

static Error run_family(const FSCompletenessRunOptions &p_options, FSCompletenessRunResult &r_result) {
	r_result = FSCompletenessRunResult();
	StageTimings timings;
	const uint64_t run_started_at = StageTimings::now();
	const FSCompletenessClock clock = p_options.clock == nullptr ? StageTimings::now : p_options.clock;
	const auto deadline_exceeded = [&]() {
		return p_options.deadline_usec != 0 && clock() >= p_options.deadline_usec;
	};
	if (p_options.catalog_root.is_empty() || p_options.family.is_empty() ||
			p_options.scratch_root.is_empty() || p_options.report_path.is_empty()) {
		return ERR_INVALID_PARAMETER;
	}
	String canonical_scratch_root;
	const Error scratch_error = resolve_or_create_owned_scratch_root(
			p_options.scratch_root, canonical_scratch_root);
	if (scratch_error != OK) {
		return scratch_error;
	}
	const Error report_path_error = validate_owned_report_path(
			canonical_scratch_root, p_options.catalog_root, p_options.report_path);
	if (report_path_error != OK) {
		return report_path_error;
	}
	// Publishing the partial report is what keeps a gate from reading an abandoned run as a clean one,
	// so the deadline is only enforced once the report path is known to be writable.
	const auto abort_after_timeout = [&](const char *p_stage) -> bool {
		if (!deadline_exceeded()) {
			return false;
		}
		timings.total = StageTimings::since(run_started_at);
		Vector<FSCompletenessStructuralFailure> failures;
		failures.push_back(make_structural_failure(FSCompletenessStructuralStage::RUN_TIMEOUT,
				vformat("The run exceeded its wall-clock budget during the %s stage.", p_stage), String(),
				String(), String(), ERR_TIMEOUT));
		const Dictionary partial_report = timeout_report(p_options.family, timings, failures);
		const Error publish_error = write_report_atomically(canonical_scratch_root, p_options.catalog_root,
				p_options.report_path, partial_report, p_options.persisted_write_hook);
		r_result.success = false;
		r_result.outcome = "structural_failure";
		if (publish_error != OK) {
			// Nothing reached disk, so the result must not carry a report either: a consumer that reads
			// one would credit this run with evidence it never published.
			failures.push_back(make_structural_failure(
					FSCompletenessStructuralStage::RUN_TIMEOUT_REPORT_UNWRITABLE,
					"The partial report of a run that exceeded its budget could not be published.", String(),
					String(), String(), publish_error));
			sort_structural_failures(failures);
			r_result.structural_failures = failures;
			return true;
		}
		r_result.structural_failures = failures;
		r_result.report = partial_report;
		return true;
	};
	if (abort_after_timeout("start")) {
		return ERR_TIMEOUT;
	}

	uint64_t stage_started_at = StageTimings::now();
	// The catalog, its rule manifest, the resolved matrix, and the migrations are immutable inputs, so
	// they are loaded once per process per catalog root and family. The canonical root is the key: a
	// staged copy of the catalog is a different input and must never read back the tracked one.
	const String canonical_catalog_root =
			TemporaryProjectTree::canonicalize_existing_path(p_options.catalog_root);
	if (canonical_catalog_root.is_empty()) {
		return ERR_CANT_RESOLVE;
	}
	Vector<String> errors;
	const FSCompletenessCatalogRecord *catalog_record = nullptr;
	Error error =
			FSCompletenessCatalogCache::get(canonical_catalog_root, p_options.family, catalog_record, errors);
	if (error != OK || catalog_record == nullptr) {
		return error == OK ? ERR_INVALID_DATA : error;
	}
	const FSCompletenessManifest &manifest = catalog_record->manifest;
	const FSCompletenessResolution &resolution = catalog_record->resolution;
	const FSCompletenessMigrations &migrations = catalog_record->migrations;

	// One structural failure is the whole verdict of a run that never got to observe anything.
	const auto fail_structurally = [&](const char *p_stage, const String &p_detail,
										   Error p_error = ERR_INVALID_DATA) {
		Vector<FSCompletenessStructuralFailure> failures;
		failures.push_back(
				make_structural_failure(p_stage, p_detail, String(), String(), String(), p_error));
		r_result.outcome = "structural_failure";
		r_result.structural_failures = failures;
	};
	const String duplicate_adapter_id = FSCompletenessAdapterRegistry::validate();
	if (!duplicate_adapter_id.is_empty()) {
		fail_structurally(FSCompletenessStructuralStage::ADAPTER_DUPLICATE,
				vformat("The adapter registry registers id '%s' more than once.", duplicate_adapter_id));
		return ERR_INVALID_DATA;
	}
	const FSCompletenessFamilyAdapter *adapter = FSCompletenessAdapterRegistry::find(manifest.adapter);
	// An adapter the catalog declares but this build does not compile is not an unknown adapter: the
	// family is real and its cells are real, and what this build lacks is the surface they observe.
	// The run goes on far enough to resolve the matrix and then publishes every cell of it as not
	// covered, so a document produced here still names the coverage this configuration owes.
	const bool adapter_unavailable_in_configuration =
			adapter == nullptr && FSCompletenessAdapterRegistry::is_configuration_gated(manifest.adapter);
	if (adapter == nullptr && !adapter_unavailable_in_configuration) {
		fail_structurally(FSCompletenessStructuralStage::ADAPTER_UNKNOWN,
				vformat("rules/%s.json:$.adapter names adapter '%s', which is not registered.",
						p_options.family, manifest.adapter));
		return ERR_INVALID_DATA;
	}
	// The configuration a report carries and the adapters a run can reach are two statements about
	// the same build. A build that says it compiled the tooling surfaces and cannot hand out the
	// adapter that drives them contradicts itself, and a contradiction is refused rather than
	// published as a family nobody has to cover.
	if (adapter_unavailable_in_configuration &&
			bool(FSCompletenessRunner::configuration_report().get("tools_enabled", false))) {
		fail_structurally(FSCompletenessStructuralStage::ADAPTER_UNAVAILABLE_IN_CONFIGURATION,
				vformat("rules/%s.json:$.adapter names adapter '%s', which this build reports as compiled "
						"in but does not register.",
						p_options.family, manifest.adapter));
		return ERR_INVALID_DATA;
	}

	const Vector<String> *domain_surfaces = manifest.domain.getptr("surface");
	if (domain_surfaces == nullptr) {
		return ERR_INVALID_DATA;
	}
	// Selected surfaces keep the manifest's own axis order: the report is a document whose member
	// order is part of its bytes, and a run must not reorder it by narrowing what it executes.
	Vector<String> selected_surfaces;
	for (const String &surface : *domain_surfaces) {
		if (p_options.surfaces.is_empty() || p_options.surfaces.has(surface)) {
			selected_surfaces.push_back(surface);
		}
	}
	if (selected_surfaces.size() != (p_options.surfaces.is_empty() ? domain_surfaces->size() : p_options.surfaces.size())) {
		return ERR_INVALID_PARAMETER;
	}
	HashSet<String> selected_surface_set;
	for (const String &surface : selected_surfaces) {
		selected_surface_set.insert(surface);
	}
	// Publishing a surface the run does not execute would produce a document with a matrix and no case
	// in it. Refusing the combination here keeps that document from existing at all, rather than
	// leaving a consumer to notice that a clean-looking report is empty.
	if (!p_options.published_surface.is_empty() &&
			!selected_surface_set.has(p_options.published_surface)) {
		Vector<String> quoted_surfaces;
		for (const String &surface : selected_surfaces) {
			quoted_surfaces.push_back(vformat("'%s'", surface));
		}
		fail_structurally(FSCompletenessStructuralStage::PUBLISHED_SURFACE_NOT_EXECUTED,
				vformat("The run publishes surface '%s' but executes only %s.", p_options.published_surface,
						String(", ").join(quoted_surfaces)),
				ERR_INVALID_PARAMETER);
		return ERR_INVALID_PARAMETER;
	}
	Vector<const FSCompletenessResolvedCell *> selected_cells;
	for (const FSCompletenessResolvedCell &cell : resolution.cells) {
		if (selected_surface_set.has(String(cell.coordinates.get("surface", String())))) {
			selected_cells.push_back(&cell);
		}
	}
	timings.load += StageTimings::since(stage_started_at);
	if (abort_after_timeout("load")) {
		return ERR_TIMEOUT;
	}
	if (adapter_unavailable_in_configuration) {
		timings.total = StageTimings::since(run_started_at);
		const Dictionary report = adapter_unavailable_report(p_options.family, p_options.published_surface,
				selected_cells, manifest, resolution.uncovered_dimension_count, timings);
		const Error publish_error = write_report_atomically(canonical_scratch_root, p_options.catalog_root,
				p_options.report_path, report, p_options.persisted_write_hook);
		if (publish_error != OK) {
			return publish_error;
		}
		for (const FSCompletenessResolvedCell *cell : selected_cells) {
			r_result.not_covered_case_ids.push_back(cell->case_id);
		}
		r_result.not_covered_case_ids.sort();
		// The verdict is decided once more from a clock read taken after the last write, exactly as the
		// path that observes cells does. Publishing a document is the one span with no stage boundary
		// inside it, so a budget crossed there would otherwise be published as an in-budget run and read
		// by a gate as a clean one.
		if (deadline_exceeded()) {
			const String timeout_detail =
					"The run exceeded its wall-clock budget while publishing its report.";
			Vector<FSCompletenessStructuralFailure> failures;
			failures.push_back(make_structural_failure(FSCompletenessStructuralStage::RUN_TIMEOUT,
					timeout_detail, String(), String(), String(), ERR_TIMEOUT));
			const Dictionary timed_out_report = document_with_timeout_verdict(report, timeout_detail);
			const Error republish_error = write_report_atomically(canonical_scratch_root,
					p_options.catalog_root, p_options.report_path, timed_out_report,
					p_options.persisted_write_hook);
			r_result.success = false;
			r_result.outcome = "structural_failure";
			if (republish_error != OK) {
				// The document on disk still claims the run was in budget and nothing replaced it, so the
				// result must not carry a report a consumer would trust.
				failures.push_back(make_structural_failure(
						FSCompletenessStructuralStage::RUN_TIMEOUT_REPORT_UNWRITABLE,
						"The verdict of a run that exceeded its budget could not be republished.", String(),
						String(), String(), republish_error));
				sort_structural_failures(failures);
				r_result.structural_failures = failures;
				return ERR_TIMEOUT;
			}
			r_result.structural_failures = failures;
			r_result.report = timed_out_report;
			return ERR_TIMEOUT;
		}
		r_result.success = true;
		r_result.outcome = "not_covered";
		r_result.report = report;
		WARN_PRINT(vformat("Family '%s' covers none of its %d cells in this build: %s.", p_options.family,
				selected_cells.size(),
				FSCompletenessNotCoveredReason::ADAPTER_UNAVAILABLE_IN_CONFIGURATION));
		return OK;
	}

	stage_started_at = StageTimings::now();
	HashSet<String> current_ids;
	for (const FSCompletenessResolvedCell &cell : resolution.cells) {
		current_ids.insert(cell.case_id);
	}
	const String repository_root = Completeness::find_repository_root(p_options.catalog_root);
	if (repository_root.is_empty()) {
		return ERR_UNAUTHORIZED;
	}
	HashMap<String, FSCompletenessFinding> ledger;
	error = load_findings_ledger(p_options.catalog_root.path_join("findings"), p_options.family,
			resolution, current_ids, migrations, repository_root, p_options.tracked_file_probe, ledger);
	if (error != OK) {
		return error;
	}
	timings.load += StageTimings::since(stage_started_at);

	stage_started_at = StageTimings::now();
	Vector<FSCompletenessStructuralFailure> structural_failures;
	Vector<FSCompletenessWitnessBinding> witness_bindings;
	error = collect_witness_bindings(manifest, resolution, witness_bindings, structural_failures);
	timings.resolve += StageTimings::since(stage_started_at);
	if (abort_after_timeout("resolve")) {
		return ERR_TIMEOUT;
	}
	if (error != OK) {
		sort_structural_failures(structural_failures);
		r_result.outcome = "structural_failure";
		r_result.structural_failures = structural_failures;
		return error;
	}

	stage_started_at = StageTimings::now();
	Vector<FSCompletenessProgram> programs;
	programs.reserve(selected_cells.size());
	for (const FSCompletenessResolvedCell *selected_cell : selected_cells) {
		const FSCompletenessResolvedCell &cell = *selected_cell;
		FSCompletenessProgram program;
		error = adapter->render(cell, program);
		if (error != OK) {
			return error;
		}
		const String rendered_case_id = program.case_id;
		const String rendered_surface = program.surface;
		const Dictionary rendered_coordinates = program.coordinates.duplicate(true);
		const String rendered_expected_output = program.expected_output;
		if (p_options.program_mutator != nullptr) {
			p_options.program_mutator(program);
		}
		if (program.case_id != rendered_case_id || program.case_id != cell.case_id ||
				program.surface != rendered_surface ||
				program.surface != String(cell.coordinates.get("surface", String())) ||
				program.coordinates != rendered_coordinates || program.coordinates != cell.coordinates ||
				program.expected_output != rendered_expected_output) {
			return ERR_INVALID_DATA;
		}
		programs.push_back(program);
	}
	ReportArtifactScope artifact_scope;
	ProgramIndex program_index;
	program_index.build(programs);
	if (programs.size() != selected_cells.size()) {
		fail_structurally(FSCompletenessStructuralStage::PROGRAM_CARDINALITY,
				vformat("The run rendered %d programs for %d selected cells.", programs.size(),
						selected_cells.size()));
		return ERR_INVALID_DATA;
	}
	error = artifact_scope.stage(canonical_scratch_root, selected_surfaces, selected_cells, program_index,
			p_options.persisted_write_hook);
	if (error != OK) {
		return error;
	}

	timings.render += StageTimings::since(stage_started_at);
	if (abort_after_timeout("render")) {
		return ERR_TIMEOUT;
	}

	stage_started_at = StageTimings::now();
	FSCompletenessRuntimeBatch batch;
	error = adapter->execute(canonical_scratch_root, programs, batch);
	timings.execute += StageTimings::since(stage_started_at);
	if (abort_after_timeout("execute")) {
		return ERR_TIMEOUT;
	}
	if (error == ERR_UNAVAILABLE) {
		// An adapter reports ERR_UNAVAILABLE when the surface it drives could not be reached at all -
		// a host process that never started, a readiness record that could not be read, a helper that
		// refused. That is a defect in the harness or in the host rather than an observation about the
		// product, and the staged programs are kept so it can be reproduced from the same inputs.
		fail_structurally(FSCompletenessStructuralStage::TOOLING_HOST_UNAVAILABLE,
				vformat("Family '%s' could not drive the surface it observes.", p_options.family),
				ERR_UNAVAILABLE);
		artifact_scope.commit();
		return ERR_UNAVAILABLE;
	}
	if (error != OK) {
		return error;
	}
	stage_started_at = StageTimings::now();
	for (const FSCompletenessResolvedCell *cell : selected_cells) {
		const FSCompletenessRuntimeResult *actual = runtime_result_for(*cell, batch);
		if (actual == nullptr || !runtime_result_identity_matches(*cell, *actual)) {
			return ERR_INVALID_DATA;
		}
	}
	if (p_options.observation_mutator != nullptr) {
		for (const FSCompletenessResolvedCell *cell : selected_cells) {
			FSCompletenessRuntimeResult *actual = runtime_result_for(*cell, batch);
			if (actual == nullptr) {
				return ERR_INVALID_DATA;
			}
			p_options.observation_mutator(static_cast<FSCompletenessObservation &>(*actual));
			if (!runtime_result_identity_matches(*cell, *actual)) {
				return ERR_INVALID_DATA;
			}
		}
	}
	if (p_options.runtime_result_mutator != nullptr) {
		for (const FSCompletenessResolvedCell *cell : selected_cells) {
			FSCompletenessRuntimeResult *actual = runtime_result_for(*cell, batch);
			if (actual == nullptr) {
				return ERR_INVALID_DATA;
			}
			p_options.runtime_result_mutator(*actual);
			if (!runtime_result_identity_matches(*cell, *actual)) {
				return ERR_INVALID_DATA;
			}
		}
	}
	// A witness whose cell this run did not select produced no evidence to identify; the surfaces it
	// covers are validated by the runs that do select them.
	Vector<FSCompletenessWitnessBinding> executed_witness_bindings;
	for (const FSCompletenessWitnessBinding &binding : witness_bindings) {
		if (binding.cell != nullptr &&
				selected_surface_set.has(String(binding.cell->coordinates.get("surface", String())))) {
			executed_witness_bindings.push_back(binding);
		}
	}
	const Error witness_identity_error =
			validate_witness_runtime_identity(executed_witness_bindings, batch, structural_failures);
	if (witness_identity_error != OK) {
		sort_structural_failures(structural_failures);
		r_result.outcome = "structural_failure";
		r_result.structural_failures = structural_failures;
		return witness_identity_error;
	}

	// What this build could observe at all decides which cells it may judge, and it is read from the
	// configuration the report carries rather than from a macro at each site, so a document and the run
	// that produced it can never disagree about which cells were in scope.
	const Dictionary configuration = FSCompletenessRunner::configuration_report();
	const bool analyzer_warnings = bool(configuration.get("analyzer_warnings", true));
	HashSet<String> diagnostics_unobservable_case_ids;
	if (!analyzer_warnings) {
		for (const FSCompletenessResolvedCell *selected_cell : selected_cells) {
			if (FSCompletenessRunner::expectation_requires_analyzer_warnings(
						expected_dimensions(*selected_cell))) {
				diagnostics_unobservable_case_ids.insert(selected_cell->case_id);
			}
		}
	}

	Vector<FSCompletenessFinding> findings;
	for (const FSCompletenessResolvedCell *selected_cell : selected_cells) {
		const FSCompletenessResolvedCell &cell = *selected_cell;
		// Only the expectation this build cannot observe is exempt. Everything else the cell declares -
		// its output, its runtime status, its analyzer verdict and every other dimension - is observed
		// here exactly as it is anywhere, so a regression in a cell that also expects a warning is
		// reported by the build that cannot see the warning.
		const bool diagnostics_unobservable = diagnostics_unobservable_case_ids.has(cell.case_id);
		const FSCompletenessRuntimeResult *actual = runtime_result_for(cell, batch);
		const FSCompletenessProgram *program = program_index.find(cell.case_id);
		if (actual == nullptr || program == nullptr) {
			return ERR_INVALID_DATA;
		}
		const String artifact_path = artifact_scope.artifact_path(cell);
		if (!actual->passed || actual->status != "ok") {
			append_finding(findings, p_options.family, cell.case_id, "runtime_status",
					runtime_status(true, "ok"), runtime_status(actual->passed, actual->status), artifact_path);
		}
		if (!actual->diagnostics.is_empty()) {
			append_finding(findings, p_options.family, cell.case_id, "diagnostics",
					PackedStringArray(), actual->diagnostics, artifact_path);
		}
		if (actual->produced_output != program->expected_output) {
			append_finding(findings, p_options.family, cell.case_id, "output",
					program->expected_output, actual->produced_output, artifact_path);
		}
		const FSCompletenessResolvedDimension *analysis_dimension = cell.dimensions.getptr("analysis");
		if (analysis_dimension != nullptr) {
			const String expected_analysis = analysis_dimension->expected;
			const String severity_profile = diagnostic_severity_profile(*actual);
			if (expected_analysis == "reject" && severity_profile != "error") {
				append_finding(findings, p_options.family, cell.case_id, "diagnostic_severity", "error",
						severity_profile, artifact_path);
			} else if (expected_analysis == "accept" && severity_profile == "error") {
				append_finding(findings, p_options.family, cell.case_id, "diagnostic_severity",
						"at_most_warning", severity_profile, artifact_path);
			}
		}
		for (const String &dimension_name : sorted_dimension_keys(cell.dimensions)) {
			if (diagnostics_unobservable && dimension_name == DIAGNOSTIC_SEVERITY_DIMENSION) {
				continue;
			}
			const FSCompletenessResolvedDimension &dimension = cell.dimensions[dimension_name];
			const Variant observed = actual->dimensions.get(dimension_name, Variant());
			if (observed == dimension.expected) {
				continue;
			}
			append_finding(findings, p_options.family, cell.case_id, dimension_name,
					dimension.expected, observed, artifact_path);
		}
	}

	struct SurfacePair {
		const FSCompletenessResolvedCell *text = nullptr;
		const FSCompletenessResolvedCell *bytecode = nullptr;
	};
	HashMap<String, SurfacePair> pairs;
	Vector<String> duplicated_pair_keys;
	for (const FSCompletenessResolvedCell *cell : selected_cells) {
		const String pair_key = semantic_pair_key(cell->coordinates, "surface");
		SurfacePair *pair = pairs.getptr(pair_key);
		if (pair == nullptr) {
			pairs[pair_key] = SurfacePair();
			pair = pairs.getptr(pair_key);
		}
		const String cell_surface = cell->coordinates.get("surface", String());
		if (cell_surface != "text" && cell_surface != "bytecode") {
			// The batch files results under exactly these two surfaces, so a cell on any other one could
			// never be paired with anything; refusing it here keeps it from being silently filed as
			// bytecode.
			return ERR_INVALID_DATA;
		}
		const FSCompletenessResolvedCell **slot = cell_surface == "text" ? &pair->text : &pair->bytecode;
		if (*slot != nullptr) {
			duplicated_pair_keys.push_back(pair_key);
			continue;
		}
		*slot = cell;
	}
	Vector<String> pair_keys;
	for (const KeyValue<String, SurfacePair> &pair : pairs) {
		pair_keys.push_back(pair.key);
	}
	pair_keys.sort();
	// The cardinality the matrix must have is the domain's: one program per selected surface leaf for
	// every semantic pair. Naming the pair key keeps a shape defect attributable instead of reported
	// as an anonymous invalid run.
	duplicated_pair_keys.sort();
	for (const String &pair_key : duplicated_pair_keys) {
		fail_structurally(FSCompletenessStructuralStage::PROGRAM_CARDINALITY,
				vformat("Semantic pair '%s' carries more than one program on one surface.", pair_key));
		return ERR_INVALID_DATA;
	}
	for (const String &pair_key : pair_keys) {
		const SurfacePair &pair = pairs[pair_key];
		const int programs_in_pair = (pair.text != nullptr ? 1 : 0) + (pair.bytecode != nullptr ? 1 : 0);
		if (programs_in_pair != selected_surfaces.size()) {
			fail_structurally(FSCompletenessStructuralStage::PROGRAM_CARDINALITY,
					vformat("Semantic pair '%s' carries %d programs across %d selected surfaces.", pair_key,
							programs_in_pair, selected_surfaces.size()));
			return ERR_INVALID_DATA;
		}
	}
	int parity_failures = 0;
	// The one derivation of which pairs could be compared at all. Parity verdicts and ledger
	// reconciliation both read it, so a pair can never be compared by one and assumed by the other.
	int compared_surface_pairs = 0;
	HashSet<String> compared_case_ids;
	HashSet<String> parity_case_ids;
	HashMap<String, Dictionary> parity_evidence_by_case;
	HashMap<String, String> parity_partner_case_id;
	for (const String &pair_key : pair_keys) {
		const SurfacePair &pair = pairs[pair_key];
		if (pair.text == nullptr || pair.bytecode == nullptr) {
			// Parity is agreement between two observations. A run that executed only one surface of a
			// pair has nothing to compare, and must not report agreement it never observed.
			continue;
		}
		const FSCompletenessRuntimeResult *text = runtime_result_for(*pair.text, batch);
		const FSCompletenessRuntimeResult *bytecode = runtime_result_for(*pair.bytecode, batch);
		if (text == nullptr || bytecode == nullptr) {
			return ERR_INVALID_DATA;
		}
		compared_surface_pairs++;
		compared_case_ids.insert(pair.text->case_id);
		compared_case_ids.insert(pair.bytecode->case_id);

		const Dictionary evidence = aggregate_parity_evidence(*pair.text, *pair.bytecode, *text, *bytecode);
		const bool pair_failed = evidence.has("output") || evidence.has("diagnostics") ||
				evidence.has("diagnostic_records") || evidence.has("runtime_status") ||
				!Dictionary(evidence.get("dimensions", Dictionary())).is_empty();
		if (!pair_failed) {
			continue;
		}
		parity_failures++;
		parity_case_ids.insert(pair.text->case_id);
		parity_case_ids.insert(pair.bytecode->case_id);
		parity_evidence_by_case[pair.text->case_id] = evidence;
		parity_evidence_by_case[pair.bytecode->case_id] = evidence;
		parity_partner_case_id[pair.text->case_id] = pair.bytecode->case_id;
		parity_partner_case_id[pair.bytecode->case_id] = pair.text->case_id;
		bool parity_attached = false;
		for (FSCompletenessFinding &finding : findings) {
			if (finding.case_id == pair.text->case_id || finding.case_id == pair.bytecode->case_id) {
				finding.parity_evidence = evidence;
				parity_attached = true;
			}
		}
		if (!parity_attached) {
			FSCompletenessFinding finding;
			finding.case_id = pair.text->case_id;
			finding.family = p_options.family;
			finding.dimension = FSCompletenessRunner::TEXT_BYTECODE_PARITY_DIMENSION;
			finding.finding_id = make_finding_id(finding.case_id, finding.dimension);
			finding.expected = "matching_surface_observations";
			finding.parity_evidence = evidence;
			finding.actual = finding.parity_evidence;
			const FSCompletenessProgram *text_program = program_index.find(pair.text->case_id);
			if (text_program == nullptr) {
				return ERR_INVALID_DATA;
			}
			finding.artifact_path = artifact_scope.artifact_path(*pair.text);
			findings.push_back(finding);
		}
	}
	// A ledger entry is in scope only when this run could have contradicted it: its case must have been
	// executed, and a parity classification additionally needs the pair it names to have been compared.
	// Both conditions read the same derivation the parity verdicts came from, so an entry can never be
	// called stale on evidence the run never gathered - whether the missing surface was excluded by a
	// filter or was never in the family's domain at all.
	HashSet<String> executed_case_ids;
	for (const FSCompletenessResolvedCell *cell : selected_cells) {
		executed_case_ids.insert(cell->case_id);
	}
	HashMap<String, FSCompletenessFinding> reconcilable_ledger;
	for (const KeyValue<String, FSCompletenessFinding> &entry : ledger) {
		if (!executed_case_ids.has(entry.value.case_id)) {
			continue;
		}
		if (entry.value.dimension == FSCompletenessRunner::TEXT_BYTECODE_PARITY_DIMENSION &&
				!compared_case_ids.has(entry.value.case_id)) {
			continue;
		}
		// An entry classifying the one dimension this build could not observe is beyond its reach: the
		// run gathered nothing that could reproduce it or contradict it.
		if (entry.value.dimension == DIAGNOSTIC_SEVERITY_DIMENSION &&
				diagnostics_unobservable_case_ids.has(entry.value.case_id)) {
			continue;
		}
		reconcilable_ledger.insert(entry.key, entry.value);
	}
	ledger = reconcilable_ledger;

	HashSet<String> reconciled_ledger_entries;
	for (FSCompletenessFinding &finding : findings) {
		const String reconciliation_key = finding.case_id + "|" + finding.dimension;
		const FSCompletenessFinding *known = ledger.getptr(reconciliation_key);
		if (known == nullptr) {
			continue;
		}
		// Classification carries disposition and ownership only. The finding stays in the report and
		// keeps its evidence, so a classified mismatch still fails the run.
		finding.finding_id = known->finding_id;
		finding.classification = known->classification;
		finding.issue_url = known->issue_url;
		finding.closure_packet_url = known->closure_packet_url;
		finding.permanent_test_paths = known->permanent_test_paths;
		finding.migrated_from = known->migrated_from;
		finding.resolved_case_ids = known->resolved_case_ids;
		reconciled_ledger_entries.insert(reconciliation_key);
	}
	Vector<String> reconciled_keys;
	Vector<String> stale_keys;
	for (const KeyValue<String, FSCompletenessFinding> &entry : ledger) {
		if (reconciled_ledger_entries.has(entry.key)) {
			reconciled_keys.push_back(entry.key);
		} else {
			stale_keys.push_back(entry.key);
		}
	}
	reconciled_keys.sort();
	stale_keys.sort();
	Array reconciled_report;
	for (const String &key : reconciled_keys) {
		const FSCompletenessFinding &entry = ledger[key];
		Dictionary record;
		record["reconciliation_key"] = key;
		record["finding_id"] = entry.finding_id;
		record["case_id"] = entry.case_id;
		record["dimension"] = entry.dimension;
		record["classification"] = entry.classification;
		reconciled_report.push_back(record);
	}
	Array stale_report;
	for (const String &key : stale_keys) {
		const FSCompletenessFinding &entry = ledger[key];
		Dictionary record;
		record["reconciliation_key"] = key;
		record["finding_id"] = entry.finding_id;
		record["case_id"] = entry.case_id;
		record["dimension"] = entry.dimension;
		record["classification"] = entry.classification;
		stale_report.push_back(record);
		structural_failures.push_back(make_structural_failure(
				FSCompletenessStructuralStage::LEDGER_ENTRY_STALE,
				vformat("Ledger entry '%s' classifies dimension '%s', which no longer reproduces.",
						entry.finding_id, entry.dimension),
				entry.case_id, String(), String(), ERR_INVALID_DATA));
	}
	Dictionary ledger_report;
	ledger_report["reconciled"] = reconciled_report;
	ledger_report["stale"] = stale_report;
	sort_findings(findings);

	HashMap<String, Array> blocking_finding_ids_by_case;
	for (const FSCompletenessFinding &finding : findings) {
		Array *blocking = blocking_finding_ids_by_case.getptr(finding.case_id);
		if (blocking == nullptr) {
			blocking_finding_ids_by_case[finding.case_id] = Array();
			blocking = blocking_finding_ids_by_case.getptr(finding.case_id);
		}
		blocking->push_back(finding.finding_id);
	}
	// A parity failure fails both cases of the pair even when only one of them carries a finding, so
	// each case of a failing pair inherits its partner's findings as blocking evidence.
	for (const String &case_id : parity_case_ids) {
		const String *partner = parity_partner_case_id.getptr(case_id);
		const Array *partner_blocking =
				partner == nullptr ? nullptr : blocking_finding_ids_by_case.getptr(*partner);
		Array *blocking = blocking_finding_ids_by_case.getptr(case_id);
		if (blocking == nullptr) {
			blocking_finding_ids_by_case[case_id] = Array();
			blocking = blocking_finding_ids_by_case.getptr(case_id);
		}
		if (partner_blocking == nullptr) {
			continue;
		}
		for (int index = 0; index < partner_blocking->size(); index++) {
			if (!blocking->has((*partner_blocking)[index])) {
				blocking->push_back((*partner_blocking)[index]);
			}
		}
	}
	// A cell that failed on evidence this build did gather is a failure; a cell that survived everything
	// observable and left an expectation unobserved is the one this build could not judge. Deciding it
	// here means the case records, the exception records and the run result cannot disagree about it.
	// Each uncovered cell carries the reason it could not be judged rather than a reason chosen again
	// at every place the document mentions it, so a second reason cannot be spelled one way in a case
	// record and another way in a witness record.
	HashMap<String, String> not_covered_reason_by_case;
	for (const FSCompletenessResolvedCell *selected_cell : selected_cells) {
		if (!diagnostics_unobservable_case_ids.has(selected_cell->case_id) ||
				blocking_finding_ids_by_case.getptr(selected_cell->case_id) != nullptr) {
			continue;
		}
		not_covered_reason_by_case.insert(selected_cell->case_id,
				FSCompletenessNotCoveredReason::DIAGNOSTICS_UNAVAILABLE_IN_CONFIGURATION);
		r_result.not_covered_case_ids.push_back(selected_cell->case_id);
	}
	r_result.not_covered_case_ids.sort();
	for (const String &uncovered : r_result.not_covered_case_ids) {
		WARN_PRINT(vformat("Family '%s' left cell '%s' uncovered in this build: %s.", p_options.family,
				uncovered, not_covered_reason_by_case[uncovered]));
	}

	HashMap<String, Dictionary> exception_reports;
	Vector<String> exception_ids;
	HashMap<String, int> declared_witness_count;
	HashMap<String, int> executed_witness_count;
	for (const FSCompletenessException &exception : manifest.exceptions) {
		Dictionary exception_report;
		exception_report["exception_id"] = exception.id;
		exception_report["parent"] = exception.parent;
		exception_report["witnessed"] = true;
		exception_report["positive_witnesses"] = Array();
		exception_report["boundary_witnesses"] = Array();
		exception_reports[exception.id] = exception_report;
		exception_ids.push_back(exception.id);
		declared_witness_count[exception.id] =
				exception.positive_witnesses.size() + exception.boundary_witnesses.size();
		executed_witness_count[exception.id] = 0;
	}
	for (const FSCompletenessWitnessBinding &binding : executed_witness_bindings) {
		Dictionary *exception_report = exception_reports.getptr(binding.exception_id);
		if (exception_report == nullptr) {
			return ERR_INVALID_DATA;
		}
		const Array *blocking = blocking_finding_ids_by_case.getptr(binding.cell->case_id);
		const String *witness_not_covered_reason =
				not_covered_reason_by_case.getptr(binding.cell->case_id);
		const bool witness_not_covered = witness_not_covered_reason != nullptr;
		Dictionary witness_report;
		witness_report["witness_id"] = binding.witness_id;
		witness_report["case_id"] = binding.cell->case_id;
		witness_report["witnessed"] = blocking == nullptr && !witness_not_covered;
		witness_report["blocking_finding_ids"] = blocking == nullptr ? Array() : *blocking;
		if (witness_not_covered) {
			witness_report["not_covered_reason"] = *witness_not_covered_reason;
		}
		executed_witness_count[binding.exception_id]++;
		Array witnesses = (*exception_report)[binding.boundary ? "boundary_witnesses" : "positive_witnesses"];
		witnesses.push_back(witness_report);
		(*exception_report)[binding.boundary ? "boundary_witnesses" : "positive_witnesses"] = witnesses;
		if (blocking != nullptr) {
			(*exception_report)["witnessed"] = false;
		}
		// An exception whose witness this build could not judge is not witnessed here and is not
		// unwitnessed either: it says what it stands on rather than claiming a verdict.
		if (witness_not_covered) {
			(*exception_report)["witnessed"] = false;
			(*exception_report)["not_covered_reason"] = *witness_not_covered_reason;
		}
	}
	// An exception is witnessed only when every witness it declares was observed. A run narrowed to
	// one surface leaves the witnesses of the other unobserved, and an unobserved witness is not a
	// satisfied one: reporting otherwise would let a narrowed run publish evidence it never gathered.
	for (const String &exception_id : exception_ids) {
		Dictionary *exception_report = exception_reports.getptr(exception_id);
		if (exception_report == nullptr) {
			return ERR_INVALID_DATA;
		}
		if (executed_witness_count[exception_id] != declared_witness_count[exception_id]) {
			(*exception_report)["witnessed"] = false;
		}
	}
	Array exceptions_report;
	for (const String &exception_id : exception_ids) {
		exceptions_report.push_back(exception_reports[exception_id]);
	}
	sort_structural_failures(structural_failures);
	timings.analyze += StageTimings::since(stage_started_at);
	if (abort_after_timeout("analyze")) {
		return ERR_TIMEOUT;
	}

	stage_started_at = StageTimings::now();
	Dictionary executed_by_surface;
	for (const String &surface : selected_surfaces) {
		const HashMap<String, FSCompletenessRuntimeResult> *results = batch.results_for_surface(surface);
		executed_by_surface[surface] = double(results == nullptr ? 0 : results->size());
	}
	Dictionary coverage_by_chain_length;
	for (int length = 0; length <= manifest.max_chain_length; length++) {
		coverage_by_chain_length[String::num_int64(length)] = 0.0;
	}
	Dictionary coverage_by_dimension;
	for (const FSCompletenessResolvedCell *cell : selected_cells) {
		for (const KeyValue<String, FSCompletenessResolvedDimension> &dimension : cell->dimensions) {
			const String chain_key = String::num_int64(dimension.value.canonical_provenance.size());
			coverage_by_chain_length[chain_key] = double(coverage_by_chain_length.get(chain_key, 0.0)) + 1.0;
			coverage_by_dimension[dimension.key] = double(coverage_by_dimension.get(dimension.key, 0.0)) + 1.0;
		}
	}
	coverage_by_dimension = sorted_dictionary_copy(coverage_by_dimension);

	Vector<const FSCompletenessResolvedCell *> sorted_cells = selected_cells;
	sort_cells_by_id(sorted_cells);
	Array cases;
	HashSet<String> published_case_ids;
	for (const FSCompletenessResolvedCell *cell : sorted_cells) {
		if (!p_options.published_surface.is_empty() &&
				String(cell->coordinates.get("surface", String())) != p_options.published_surface) {
			continue;
		}
		published_case_ids.insert(cell->case_id);
		const FSCompletenessRuntimeResult *actual = runtime_result_for(*cell, batch);
		const FSCompletenessProgram *program = program_index.find(cell->case_id);
		if (actual == nullptr || program == nullptr) {
			return ERR_INVALID_DATA;
		}
		const String *not_covered_reason = not_covered_reason_by_case.getptr(cell->case_id);
		const bool not_covered = not_covered_reason != nullptr;
		bool passed = actual->passed && actual->status == "ok" && actual->diagnostics.is_empty() &&
				actual->produced_output == program->expected_output &&
				observed_expected_dimensions(
						*cell, *actual, diagnostics_unobservable_case_ids.has(cell->case_id)) &&
				!parity_case_ids.has(cell->case_id);
		for (const FSCompletenessFinding &finding : findings) {
			if (finding.case_id == cell->case_id) {
				passed = false;
				break;
			}
		}
		// A cell this build could not judge is neither passed nor failed: publishing it as a pass would
		// claim evidence nothing gathered.
		if (not_covered) {
			passed = false;
		}
		Dictionary case_report;
		case_report["case_id"] = cell->case_id;
		case_report["coordinates"] = sorted_dictionary_copy(cell->coordinates);
		case_report["expected"] = expected_dimensions(*cell);
		case_report["actual"] = observed_dimensions(*cell, *actual);
		case_report["canonical_provenance"] = canonical_provenance(*cell);
		case_report["agreeing_provenance"] = all_agreeing_provenance(*cell);
		case_report["status"] = not_covered ? "not_covered" : (passed ? "passed" : "failed");
		case_report["passed"] = passed;
		// The category and the reason are written only where they apply: a build that judged every cell
		// publishes exactly the document it published before this distinction existed.
		if (not_covered) {
			case_report["category"] = "not_covered";
			case_report["not_covered_reason"] = *not_covered_reason;
		}
		case_report["runtime_passed"] = actual->passed;
		case_report["runtime_status"] = actual->status;
		Array diagnostics;
		for (const String &diagnostic : actual->diagnostics) {
			diagnostics.push_back(diagnostic);
		}
		case_report["diagnostics"] = diagnostics;
		case_report["diagnostic_records"] = actual->diagnostic_records;
		case_report["diagnostic_severity"] = diagnostic_severity_profile(*actual);
		case_report["produced_output"] = actual->produced_output;
		case_report["expected_output"] = program->expected_output;
		case_report["artifact_path"] = artifact_scope.artifact_path(*cell);
		const Dictionary *case_parity_evidence = parity_evidence_by_case.getptr(cell->case_id);
		case_report["parity_evidence"] =
				case_parity_evidence == nullptr ? Dictionary() : *case_parity_evidence;
		// Written only where an adapter recorded something, so a family that records nothing publishes
		// exactly the document it published before this member existed.
		const Dictionary case_adapter_evidence = adapter_evidence(*actual);
		if (!case_adapter_evidence.is_empty()) {
			case_report["adapter_evidence"] = case_adapter_evidence;
		}
		cases.push_back(case_report);
	}
	// The representation census is evidence this report carries. A cell that claims to be covered by a
	// witness nothing binds is an uncovered cell reported as a covered one, so the run refuses to
	// publish a clean verdict rather than summarizing a coverage claim it could not confirm. A census
	// that cannot be read is the same refusal: a malformed document is never "no census".
	FSCompletenessCensusSummary census_summary;
	Vector<String> census_errors;
	if (FSCompletenessCensus::load(p_options.catalog_root, census_summary, census_errors) != OK) {
		structural_failures.push_back(make_structural_failure(
				FSCompletenessStructuralStage::CENSUS_WITNESS_UNRESOLVED,
				vformat("The representation census could not be read: %s", String(" | ").join(census_errors)),
				String(), String(), String(), ERR_INVALID_DATA));
	} else {
		for (const String &unresolved :
				FSCompletenessCensus::unresolved_witnesses(p_options.catalog_root, census_summary)) {
			structural_failures.push_back(make_structural_failure(
					FSCompletenessStructuralStage::CENSUS_WITNESS_UNRESOLVED,
					vformat("A census claim has no witness that resolves: %s", unresolved),
					String(), String(), String(), ERR_INVALID_DATA));
		}
		// A witness the running configuration never compiled is neither bound nor broken. Reporting it
		// as a refusal would make the verdict depend on the build rather than on the catalog, and
		// dropping it silently would let a configuration publish a coverage claim it never confirmed.
		r_result.unconfirmed_census_witnesses =
				FSCompletenessCensus::unconfirmable_witnesses(p_options.catalog_root, census_summary);
		for (const String &unconfirmed : r_result.unconfirmed_census_witnesses) {
			WARN_PRINT(vformat("Type-completeness census claim unconfirmed in this build: %s", unconfirmed));
		}
	}
	sort_structural_failures(structural_failures);

	Array report_findings;
	bool success = findings.is_empty() && structural_failures.is_empty();
	for (const FSCompletenessFinding &finding : findings) {
		// A published document must stay self-consistent: a finding may only name a case the document
		// reports. The run-level verdict below still counts every finding the run produced.
		if (!published_case_ids.has(finding.case_id)) {
			continue;
		}
		report_findings.push_back(finding_report(finding));
	}
	Array structural_failures_report;
	for (const FSCompletenessStructuralFailure &failure : structural_failures) {
		structural_failures_report.push_back(FSCompletenessRunner::structural_failure_report(failure));
	}
	String outcome = !structural_failures.is_empty() ? "structural_failure"
													 : (findings.is_empty() ? "passed" : "product_mismatch");

	Dictionary report;
	report["schema_version"] = 1.0;
	report["family"] = p_options.family;
	report["success"] = success;
	report["cell_count"] = double(selected_cells.size());
	report["executed_by_surface"] = executed_by_surface;
	report["coverage_by_chain_length"] = coverage_by_chain_length;
	report["coverage_by_dimension"] = coverage_by_dimension;
	report["uncovered_required_dimensions"] = double(resolution.uncovered_dimension_count);
	report["text_bytecode_parity_failures"] = double(parity_failures);
	report["outcome"] = outcome;
	report["findings"] = report_findings;
	report["structural_failures"] = structural_failures_report;
	report["ledger"] = ledger_report;
	report["exceptions"] = exceptions_report;
	report["cases"] = cases;
	report["published_surface"] = p_options.published_surface;
	report["census"] = FSCompletenessCensus::summary_report(census_summary);
	report["configuration"] = FSCompletenessRunner::configuration_report();
	// How much of the census this build could confirm belongs in the document rather than in the
	// console of the run that produced it: an artifact is all a consumer of another machine's run ever
	// reads. It describes the build rather than the product, so it is not evidence and never reaches a
	// comparison.
	Array unconfirmed_census_witnesses;
	for (const String &unconfirmed : r_result.unconfirmed_census_witnesses) {
		unconfirmed_census_witnesses.push_back(unconfirmed);
	}
	report["unconfirmed_census_witnesses"] = unconfirmed_census_witnesses;
	// The last thing decided before a document is published is whether it carries evidence at all. A
	// document that claims a matrix and observes nothing for it is a defect in whatever produced it,
	// and every path that assembles one arrives here, so none of them can publish a clean verdict.
	if (!FSCompletenessRunner::report_carries_evidence(report)) {
		structural_failures.push_back(make_structural_failure(FSCompletenessStructuralStage::NO_EVIDENCE,
				vformat("The report observes no case for the %d cells the run executed.",
						selected_cells.size()),
				String(), String(), String(), ERR_INVALID_DATA));
		sort_structural_failures(structural_failures);
		Array settled_failures;
		for (const FSCompletenessStructuralFailure &failure : structural_failures) {
			settled_failures.push_back(FSCompletenessRunner::structural_failure_report(failure));
		}
		report["structural_failures"] = settled_failures;
		success = false;
		outcome = "structural_failure";
		report["success"] = success;
		report["outcome"] = outcome;
	}
	timings.report += StageTimings::since(stage_started_at);
	timings.total = StageTimings::since(run_started_at);
	report["timings_ms"] = timings.to_report();
	error = write_report_atomically(
			canonical_scratch_root, p_options.catalog_root, p_options.report_path, report,
			p_options.persisted_write_hook);
	if (error != OK) {
		return error;
	}
	artifact_scope.commit();

	// The verdict is decided once more here, from a clock read taken after the last write. Assembling
	// and publishing a report is the one span of a run with no stage boundary inside it, so a budget
	// crossed there would otherwise be published as an in-budget run and read by a gate as a clean one.
	// The evidence stays in the document; only the verdict it carries changes.
	if (deadline_exceeded()) {
		const String timeout_detail = "The run exceeded its wall-clock budget while publishing its report.";
		structural_failures.push_back(make_structural_failure(
				FSCompletenessStructuralStage::RUN_TIMEOUT, timeout_detail, String(), String(), String(), ERR_TIMEOUT));
		sort_structural_failures(structural_failures);
		report = document_with_timeout_verdict(report, timeout_detail);
		timings.total = StageTimings::since(run_started_at);
		report["timings_ms"] = timings.to_report();
		const Error republish_error = write_report_atomically(canonical_scratch_root,
				p_options.catalog_root, p_options.report_path, report, p_options.persisted_write_hook);
		r_result.success = false;
		r_result.executed_cells = selected_cells.size();
		r_result.compared_surface_pairs = compared_surface_pairs;
		r_result.findings = findings;
		r_result.outcome = "structural_failure";
		if (republish_error != OK) {
			// The document on disk still claims the run was in budget and nothing replaced it, so the
			// result must not carry a report a consumer would trust.
			structural_failures.push_back(make_structural_failure(
					FSCompletenessStructuralStage::RUN_TIMEOUT_REPORT_UNWRITABLE,
					"The verdict of a run that exceeded its budget could not be republished.", String(),
					String(), String(), republish_error));
			sort_structural_failures(structural_failures);
			r_result.structural_failures = structural_failures;
			return ERR_TIMEOUT;
		}
		r_result.structural_failures = structural_failures;
		r_result.report = report;
		return ERR_TIMEOUT;
	}

	FSCompletenessRunResult completed;
	// What the run could not judge was decided long before the verdict was, so it is carried across
	// rather than dropped by the wholesale assignment below.
	completed.not_covered_case_ids = r_result.not_covered_case_ids;
	completed.unconfirmed_census_witnesses = r_result.unconfirmed_census_witnesses;
	completed.success = success;
	completed.executed_cells = selected_cells.size();
	completed.compared_surface_pairs = compared_surface_pairs;
	completed.findings = findings;
	completed.structural_failures = structural_failures;
	completed.outcome = outcome;
	completed.report = report;
	r_result = completed;
	if (!structural_failures.is_empty()) {
		return ERR_INVALID_DATA;
	}
	return findings.is_empty() ? OK : FAILED;
}

} // namespace

Error collect_witness_bindings(const FSCompletenessManifest &p_manifest,
		const FSCompletenessResolution &p_resolution,
		Vector<FSCompletenessWitnessBinding> &r_bindings,
		Vector<FSCompletenessStructuralFailure> &r_failures) {
	r_bindings.clear();
	HashSet<String> witness_ids;
	HashMap<String, String> witness_id_by_coordinates;
	bool failed = false;
	for (const FSCompletenessException &exception : p_manifest.exceptions) {
		for (int pass = 0; pass < 2; pass++) {
			const bool boundary = pass == 1;
			const Vector<FSCompletenessWitness> &declared =
					boundary ? exception.boundary_witnesses : exception.positive_witnesses;
			for (const FSCompletenessWitness &witness : declared) {
				const String &witness_id = witness.id;
				FSCompletenessWitnessBinding binding;
				binding.witness_id = witness_id;
				binding.exception_id = exception.id;
				binding.parent_relation_id = exception.parent;
				binding.boundary = boundary;
				if (witness_ids.has(witness_id)) {
					r_failures.push_back(make_structural_failure(
							FSCompletenessStructuralStage::WITNESS_DECLARED_TWICE,
							"A witness ID is declared more than once.", String(), witness_id, exception.id,
							ERR_INVALID_DATA));
					failed = true;
					continue;
				}
				witness_ids.insert(witness_id);
				const Dictionary coordinates = witness.coordinates;
				if (!witness_coordinates_are_resolvable(p_manifest, coordinates)) {
					r_failures.push_back(make_structural_failure(
							FSCompletenessStructuralStage::WITNESS_ID_UNKNOWN,
							"The declared coordinates do not name a manifest domain leaf on every domain axis.",
							String(), witness_id, exception.id, ERR_INVALID_DATA));
					failed = true;
					continue;
				}
				const String coordinate_identity = canonical_variant_identity(coordinates);
				const String *twin = witness_id_by_coordinates.getptr(coordinate_identity);
				if (twin != nullptr) {
					r_failures.push_back(make_structural_failure(
							FSCompletenessStructuralStage::WITNESS_DECLARED_TWICE,
							vformat("Witness '%s' declares the same coordinates.", *twin), String(), witness_id,
							exception.id, ERR_INVALID_DATA));
					failed = true;
					continue;
				}
				witness_id_by_coordinates.insert(coordinate_identity, witness_id);
				int matches = 0;
				const FSCompletenessResolvedCell *cell =
						p_resolution.find_cell_by_coordinates(coordinates, matches);
				if (cell == nullptr || matches == 0) {
					r_failures.push_back(make_structural_failure(
							FSCompletenessStructuralStage::WITNESS_CELL_MISSING,
							"No resolved cell carries the witness coordinates.", String(), witness_id,
							exception.id, ERR_INVALID_DATA));
					failed = true;
					continue;
				}
				if (matches != 1) {
					r_failures.push_back(make_structural_failure(
							FSCompletenessStructuralStage::WITNESS_CELL_AMBIGUOUS,
							vformat("%d resolved cells carry the witness coordinates.", matches), String(),
							witness_id, exception.id, ERR_INVALID_DATA));
					failed = true;
					continue;
				}
				bool observes = false;
				for (const KeyValue<String, FSCompletenessResolvedDimension> &dimension : cell->dimensions) {
					observes = observes ||
							(boundary ? provenance_has_parent_without_exception(
												dimension.value, exception.parent)
									  : provenance_has_exception_parent(
												dimension.value, exception.id, exception.parent));
				}
				if (!observes) {
					r_failures.push_back(make_structural_failure(
							boundary ? FSCompletenessStructuralStage::WITNESS_BOUNDARY_PROVENANCE_MISSING
									 : FSCompletenessStructuralStage::WITNESS_EXCEPTION_PROVENANCE_MISSING,
							boundary ? "No dimension derives through the parent relation without the exception."
									 : "No dimension derives through the exception.",
							cell->case_id, witness_id, exception.id, ERR_INVALID_DATA));
					failed = true;
					continue;
				}
				binding.cell = cell;
				r_bindings.push_back(binding);
			}
		}
	}
	return failed ? ERR_INVALID_DATA : OK;
}

HashSet<String> FSCompletenessRunner::builtin_dimensions() {
	return HashSet<String>({ "output", "diagnostics", "runtime_status",
			FSCompletenessRunner::TEXT_BYTECODE_PARITY_DIMENSION, "diagnostic_severity" });
}

bool FSCompletenessRunner::report_carries_evidence(const Dictionary &p_report) {
	const Variant cell_count = p_report.get("cell_count", 0.0);
	if (cell_count.get_type() != Variant::FLOAT && cell_count.get_type() != Variant::INT) {
		return false;
	}
	if (double(cell_count) <= 0.0) {
		return true;
	}
	const Variant cases = p_report.get("cases", Variant());
	return cases.get_type() == Variant::ARRAY && !Array(cases).is_empty();
}

Dictionary FSCompletenessRunner::structural_failure_report(
		const FSCompletenessStructuralFailure &p_failure) {
	Dictionary report;
	report["stage"] = p_failure.stage;
	report["detail"] = p_failure.detail;
	report["case_id"] = p_failure.case_id;
	report["witness_id"] = p_failure.witness_id;
	report["exception_id"] = p_failure.exception_id;
	report["error_code"] = double(int(p_failure.error_code));
	return report;
}

Dictionary FSCompletenessRunner::configuration_report() {
	Dictionary configuration;
	// Whether the editor tooling surfaces are compiled into this binary at all. It is deliberately not
	// the same question as whether debugging checks are on: a template build with tests enabled runs
	// this harness with no tooling surface to observe.
#ifdef FS_COMPLETENESS_TOOLING_ADAPTER_AVAILABLE
	configuration["tools_enabled"] = true;
#else
	configuration["tools_enabled"] = false;
#endif
	Array adapters;
	for (const String &adapter_id : FSCompletenessAdapterRegistry::ids()) {
		adapters.push_back(adapter_id);
	}
	configuration["adapters"] = adapters;
	// Whether this binary's analyzer produces warnings at all. It is a different question from
	// `tools_enabled`: warnings are a debugging surface of the front-end (see the guards around
	// `FSParser::push_warning`), so a debug export template emits them and a release one does not,
	// whatever either build does about the editor. A build that compiles this harness always has the
	// front-end, so the debugging half of that condition is the whole of it here.
#ifdef DEBUG_ENABLED
	configuration["analyzer_warnings"] = true;
#else
	configuration["analyzer_warnings"] = false;
#endif
	return configuration;
}

bool FSCompletenessRunner::expectation_requires_analyzer_warnings(const Dictionary &p_expected_dimensions) {
	return String(p_expected_dimensions.get("diagnostic_severity", String())) == "warning";
}

namespace {

// Warning evidence is dropped rather than rewritten: a record that is not there and a record that
// could not be produced are the same observation to a build that produces none.
Variant without_warning_diagnostics(const Variant &p_value) {
	if (p_value.get_type() == Variant::DICTIONARY) {
		const Dictionary source = p_value;
		Dictionary observable;
		for (const String &key : Completeness::sorted_dictionary_keys(source)) {
			if (key == "diagnostic_severity" && String(source[key]) == "warning") {
				observable[key] = "none";
				continue;
			}
			if (key != "diagnostic_records" || source[key].get_type() != Variant::ARRAY) {
				observable[key] = without_warning_diagnostics(source[key]);
				continue;
			}
			const Array records = source[key];
			Array kept;
			for (int index = 0; index < records.size(); index++) {
				const Variant &record = records[index];
				if (record.get_type() == Variant::DICTIONARY &&
						String(Dictionary(record).get("severity", String())) == "warning") {
					continue;
				}
				kept.push_back(without_warning_diagnostics(record));
			}
			observable[key] = kept;
		}
		return observable;
	}
	if (p_value.get_type() == Variant::ARRAY) {
		const Array source = p_value;
		Array observable;
		for (int index = 0; index < source.size(); index++) {
			observable.push_back(without_warning_diagnostics(source[index]));
		}
		return observable;
	}
	return p_value;
}

// Case ids p_document publishes whose expectations only an analyzer that warns could observe. Read
// off the document rather than off the run, so the produced report and a document captured on
// another build drop exactly the same cells.
HashSet<String> cases_requiring_analyzer_warnings(const Dictionary &p_document) {
	HashSet<String> case_ids;
	const Variant &cases = p_document.get("cases", Variant());
	if (cases.get_type() != Variant::ARRAY) {
		return case_ids;
	}
	const Array case_records = cases;
	for (int index = 0; index < case_records.size(); index++) {
		if (case_records[index].get_type() != Variant::DICTIONARY) {
			continue;
		}
		const Dictionary case_record = case_records[index];
		const Variant &expected = case_record.get("expected", Variant());
		if (expected.get_type() != Variant::DICTIONARY ||
				!FSCompletenessRunner::expectation_requires_analyzer_warnings(expected)) {
			continue;
		}
		case_ids.insert(case_record.get("case_id", String()));
	}
	return case_ids;
}

bool exception_witnesses_any(const Dictionary &p_exception, const HashSet<String> &p_case_ids) {
	for (const char *member : { "positive_witnesses", "boundary_witnesses" }) {
		const Variant &witnesses = p_exception.get(member, Variant());
		if (witnesses.get_type() != Variant::ARRAY) {
			continue;
		}
		const Array witness_records = witnesses;
		for (int index = 0; index < witness_records.size(); index++) {
			if (witness_records[index].get_type() != Variant::DICTIONARY) {
				continue;
			}
			if (p_case_ids.has(String(Dictionary(witness_records[index]).get("case_id", String())))) {
				return true;
			}
		}
	}
	return false;
}

} // namespace

Variant FSCompletenessRunner::evidence_observable_in_configuration(
		const Variant &p_document, const Dictionary &p_configuration) {
	if (bool(p_configuration.get("analyzer_warnings", true))) {
		return p_document;
	}
	if (p_document.get_type() != Variant::DICTIONARY) {
		return without_warning_diagnostics(p_document);
	}
	const Dictionary document = p_document;
	const HashSet<String> dropped_case_ids = cases_requiring_analyzer_warnings(document);
	Dictionary comparable;
	for (const String &key : Completeness::sorted_dictionary_keys(document)) {
		if (key == "cases" && document[key].get_type() == Variant::ARRAY) {
			const Array case_records = document[key];
			Array kept;
			for (int index = 0; index < case_records.size(); index++) {
				const Variant &case_record = case_records[index];
				if (case_record.get_type() == Variant::DICTIONARY &&
						dropped_case_ids.has(String(Dictionary(case_record).get("case_id", String())))) {
					continue;
				}
				kept.push_back(case_record);
			}
			comparable[key] = kept;
			continue;
		}
		// An exception a dropped cell witnesses is decided by evidence this build cannot gather, so it
		// travels with its witnesses instead of being reported as an exception nothing observed.
		if (key == "exceptions" && document[key].get_type() == Variant::ARRAY) {
			const Array exception_records = document[key];
			Array kept;
			for (int index = 0; index < exception_records.size(); index++) {
				const Variant &exception_record = exception_records[index];
				if (exception_record.get_type() == Variant::DICTIONARY &&
						exception_witnesses_any(exception_record, dropped_case_ids)) {
					continue;
				}
				kept.push_back(exception_record);
			}
			comparable[key] = kept;
			continue;
		}
		comparable[key] = document[key];
	}
	// Warning evidence is dropped from whatever survived, wherever it sits: a document narrowed to the
	// cells both builds carry still records warnings only one of them could have produced.
	return without_warning_diagnostics(comparable);
}

Vector<String> FSCompletenessRunner::non_evidence_report_members() {
	return Vector<String>({ "timings_ms", "configuration", "unconfirmed_census_witnesses" });
}

Error FSCompletenessRunner::publish_owned_document(const String &p_scratch_root,
		const String &p_catalog_root, const String &p_document_path, const Dictionary &p_document) {
	String canonical_scratch_root;
	const Error scratch_error = resolve_or_create_owned_scratch_root(p_scratch_root, canonical_scratch_root);
	if (scratch_error != OK) {
		return scratch_error;
	}
	const Error path_error =
			validate_owned_report_path(canonical_scratch_root, p_catalog_root, p_document_path);
	if (path_error != OK) {
		return path_error;
	}
	return write_report_atomically(
			canonical_scratch_root, p_catalog_root, p_document_path, p_document, nullptr);
}

Error FSCompletenessRunner::republish_timed_out_document(const String &p_scratch_root,
		const String &p_catalog_root, const String &p_document_path, const String &p_detail) {
	Error read_error = OK;
	const String source = FileAccess::get_file_as_string(p_document_path, &read_error);
	if (read_error != OK) {
		return read_error;
	}
	Variant data;
	Vector<String> parse_errors;
	const Error parse_error = parse_type_completeness_json(source, p_document_path, data, parse_errors);
	if (parse_error != OK) {
		return parse_error;
	}
	if (data.get_type() != Variant::DICTIONARY) {
		return ERR_INVALID_DATA;
	}
	return publish_owned_document(p_scratch_root, p_catalog_root, p_document_path,
			document_with_timeout_verdict(data, p_detail));
}

Error FSCompletenessRunner::run(
		const FSCompletenessRunOptions &p_options, FSCompletenessRunResult &r_result) {
	const Error error = run_family(p_options, r_result);
	// A run that reached publication classified itself. Anything else aborted before evidence could
	// be published: naming the outcome keeps a broken run from reading like a clean one, and keeps it
	// out of the product-mismatch channel whatever error the aborted operation happened to return.
	if (r_result.outcome != "not_run") {
		return error;
	}
	r_result.outcome = "structural_failure";
	r_result.structural_failures.push_back(make_structural_failure(FSCompletenessStructuralStage::RUN_ABORTED,
			"The run aborted before completeness evidence could be published.", String(), String(),
			String(), error));
	return error == FAILED ? ERR_INVALID_DATA : error;
}

} // namespace FSTests
