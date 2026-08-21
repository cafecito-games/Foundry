/**************************************************************************/
/*  fs_type_completeness_runner.cpp                                      */
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
#include "fs_type_completeness_case_id.h"
#include "fs_type_completeness_graph.h"
#include "fs_type_completeness_json.h"
#include "fs_type_completeness_manifest.h"

#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/io/json.h"
#include "core/os/os.h"
#include "core/templates/hash_set.h"
#include "tests/test_utils.h"

#ifdef WINDOWS_ENABLED
#include <windows.h>
#endif

namespace FSTests {

namespace {

static Vector<String> sorted_dictionary_keys(const Dictionary &p_dictionary) {
	Vector<String> keys;
	const Array raw_keys = p_dictionary.keys();
	keys.reserve(raw_keys.size());
	for (int i = 0; i < raw_keys.size(); i++) {
		keys.push_back(raw_keys[i]);
	}
	keys.sort();
	return keys;
}

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

static const FSCompletenessResolvedCell *find_cell_by_coordinates(
		const FSCompletenessResolution &p_resolution, const Dictionary &p_coordinates, int &r_matches) {
	r_matches = 0;
	const FSCompletenessResolvedCell *match = nullptr;
	for (const FSCompletenessResolvedCell &cell : p_resolution.cells) {
		if (cell.coordinates == p_coordinates) {
			r_matches++;
			match = &cell;
		}
	}
	return match;
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

static Error validate_witness_resolution(const FSCompletenessManifest &p_manifest,
		const FSCompletenessResolution &p_resolution) {
	HashSet<String> witness_ids;
	for (const FSCompletenessException &exception : p_manifest.exceptions) {
		for (const String &witness_id : exception.positive_witnesses) {
			if (witness_ids.has(witness_id)) {
				return ERR_INVALID_DATA;
			}
			witness_ids.insert(witness_id);
			Dictionary coordinates;
			if (FSUnionCompletenessAdapter::witness_coordinates(witness_id, coordinates) != OK) {
				return ERR_INVALID_DATA;
			}
			int matches = 0;
			const FSCompletenessResolvedCell *cell =
					find_cell_by_coordinates(p_resolution, coordinates, matches);
			if (matches != 1 || cell == nullptr) {
				return ERR_INVALID_DATA;
			}
			bool observes_exception = false;
			for (const KeyValue<String, FSCompletenessResolvedDimension> &dimension : cell->dimensions) {
				observes_exception = observes_exception ||
						provenance_has_exception_parent(dimension.value, exception.id, exception.parent);
			}
			if (!observes_exception) {
				return ERR_INVALID_DATA;
			}
		}

		for (const String &witness_id : exception.boundary_witnesses) {
			if (witness_ids.has(witness_id)) {
				return ERR_INVALID_DATA;
			}
			witness_ids.insert(witness_id);
			Dictionary coordinates;
			if (FSUnionCompletenessAdapter::witness_coordinates(witness_id, coordinates) != OK) {
				return ERR_INVALID_DATA;
			}
			int matches = 0;
			const FSCompletenessResolvedCell *cell =
					find_cell_by_coordinates(p_resolution, coordinates, matches);
			if (matches != 1 || cell == nullptr) {
				return ERR_INVALID_DATA;
			}
			bool observes_parent = false;
			for (const KeyValue<String, FSCompletenessResolvedDimension> &dimension : cell->dimensions) {
				observes_parent = observes_parent ||
						provenance_has_parent_without_exception(dimension.value, exception.parent);
			}
			if (!observes_parent) {
				return ERR_INVALID_DATA;
			}
		}
	}
	return OK;
}

static const FSCompletenessRuntimeResult *runtime_result_for(
		const FSCompletenessResolvedCell &p_cell, const FSCompletenessRuntimeBatch &p_batch) {
	const String surface = p_cell.coordinates.get("surface", String());
	return surface == "text" ? p_batch.text.getptr(p_cell.case_id) : p_batch.bytecode.getptr(p_cell.case_id);
}

static FSCompletenessRuntimeResult *runtime_result_for(
		const FSCompletenessResolvedCell &p_cell, FSCompletenessRuntimeBatch &r_batch) {
	const String surface = p_cell.coordinates.get("surface", String());
	return surface == "text" ? r_batch.text.getptr(p_cell.case_id) : r_batch.bytecode.getptr(p_cell.case_id);
}

static String semantic_pair_key(const Dictionary &p_coordinates) {
	Dictionary semantic_coordinates = p_coordinates.duplicate();
	semantic_coordinates.erase("surface");
	return FSCompletenessCaseID::canonical_coordinates(semantic_coordinates);
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

static void sort_findings(Vector<FSCompletenessFinding> &r_findings) {
	for (int i = 1; i < r_findings.size(); i++) {
		const FSCompletenessFinding finding = r_findings[i];
		int position = i;
		while (position > 0 && (finding.finding_id < r_findings[position - 1].finding_id ||
					(finding.finding_id == r_findings[position - 1].finding_id &&
							(finding.case_id < r_findings[position - 1].case_id ||
									(finding.case_id == r_findings[position - 1].case_id &&
											finding.dimension < r_findings[position - 1].dimension))))) {
			r_findings.write[position] = r_findings[position - 1];
			position--;
		}
		r_findings.write[position] = finding;
	}
}

static Error validate_witness_observations(const FSCompletenessManifest &p_manifest,
		const FSCompletenessResolution &p_resolution, const FSCompletenessRuntimeBatch &p_batch) {
	for (const FSCompletenessException &exception : p_manifest.exceptions) {
		for (const String &witness_id : exception.positive_witnesses) {
			Dictionary coordinates;
			if (FSUnionCompletenessAdapter::witness_coordinates(witness_id, coordinates) != OK) {
				return ERR_INVALID_DATA;
			}
			int matches = 0;
			const FSCompletenessResolvedCell *cell =
					find_cell_by_coordinates(p_resolution, coordinates, matches);
			const FSCompletenessRuntimeResult *actual =
					cell == nullptr ? nullptr : runtime_result_for(*cell, p_batch);
			if (matches != 1 || cell == nullptr || actual == nullptr) {
				return ERR_INVALID_DATA;
			}
			bool observed_exception_dimension = false;
			for (const KeyValue<String, FSCompletenessResolvedDimension> &dimension : cell->dimensions) {
				if (!provenance_has_exception_parent(dimension.value, exception.id, exception.parent)) {
					continue;
				}
				observed_exception_dimension = true;
			}
			if (!observed_exception_dimension) {
				return ERR_INVALID_DATA;
			}
		}

		for (const String &witness_id : exception.boundary_witnesses) {
			Dictionary coordinates;
			if (FSUnionCompletenessAdapter::witness_coordinates(witness_id, coordinates) != OK) {
				return ERR_INVALID_DATA;
			}
			int matches = 0;
			const FSCompletenessResolvedCell *cell =
					find_cell_by_coordinates(p_resolution, coordinates, matches);
			const FSCompletenessRuntimeResult *actual =
					cell == nullptr ? nullptr : runtime_result_for(*cell, p_batch);
			if (matches != 1 || cell == nullptr || actual == nullptr) {
				return ERR_INVALID_DATA;
			}
			bool observed_parent_dimension = false;
			for (const KeyValue<String, FSCompletenessResolvedDimension> &dimension : cell->dimensions) {
				if (!provenance_has_parent_without_exception(dimension.value, exception.parent)) {
					continue;
				}
				observed_parent_dimension = true;
			}
			if (!observed_parent_dimension) {
				return ERR_INVALID_DATA;
			}
		}
	}
	return OK;
}

static const FSCompletenessProgram *find_program_by_id(
		const Vector<FSCompletenessProgram> &p_programs, const String &p_case_id) {
	for (const FSCompletenessProgram &program : p_programs) {
		if (program.case_id == p_case_id) {
			return &program;
		}
	}
	return nullptr;
}

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
	bool has_primary = false;
	if (p_text.produced_output != p_bytecode.produced_output) {
		const Dictionary output = parity_evidence(
				p_text.produced_output, p_bytecode.produced_output, p_text.case_id, p_bytecode.case_id);
		evidence["output"] = output;
		evidence["text"] = p_text.produced_output;
		evidence["bytecode"] = p_bytecode.produced_output;
		has_primary = true;
	}
	if (p_text.diagnostics != p_bytecode.diagnostics) {
		evidence["diagnostics"] = parity_evidence(
				p_text.diagnostics, p_bytecode.diagnostics, p_text.case_id, p_bytecode.case_id);
	}
	if (p_text.passed != p_bytecode.passed || p_text.status != p_bytecode.status) {
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

static bool parse_schema_version(const Variant &p_value) {
	if (p_value.get_type() != Variant::INT && p_value.get_type() != Variant::FLOAT) {
		return false;
	}
	const double value = p_value;
	return value == 1.0;
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

static const FSCompletenessResolvedCell *find_cell_by_id(
		const FSCompletenessResolution &p_resolution, const String &p_case_id) {
	for (const FSCompletenessResolvedCell &cell : p_resolution.cells) {
		if (cell.case_id == p_case_id) {
			return &cell;
		}
	}
	return nullptr;
}

static bool ledger_dimension_is_known(
		const FSCompletenessResolvedCell &p_cell, const String &p_dimension) {
	return p_cell.dimensions.has(p_dimension) || p_dimension == "output" ||
			p_dimension == "diagnostics" || p_dimension == "runtime_status" ||
			p_dimension == "text_bytecode_parity";
}

static Error validate_permanent_test_path(const String &p_path) {
	if (p_path.is_empty() || p_path.is_absolute_path() || p_path.simplify_path() != p_path ||
			p_path.contains("://") || p_path.contains("/../") ||
			p_path.ends_with("/..")) {
		return ERR_INVALID_DATA;
	}
	const String repository_root = TestUtils::get_tests_dir().get_base_dir().simplify_path();
	const String absolute_path = repository_root.path_join(p_path).simplify_path();
	if (!TemporaryProjectTree::is_strict_descendant(repository_root, absolute_path) ||
			!FileAccess::exists(absolute_path) || DirAccess::dir_exists_absolute(absolute_path)) {
		return ERR_INVALID_DATA;
	}
	Ref<DirAccess> filesystem = DirAccess::create(DirAccess::ACCESS_FILESYSTEM);
	if (filesystem.is_null()) {
		return ERR_UNAVAILABLE;
	}
	for (String current = absolute_path; current != repository_root; current = current.get_base_dir()) {
		if (current.is_empty() || filesystem->is_link(current)) {
			return ERR_UNAUTHORIZED;
		}
	}
	return OK;
}

static Error load_findings_ledger(const String &p_directory, const String &p_family,
		const FSCompletenessResolution &p_resolution, const HashSet<String> &p_current_ids,
		const FSCompletenessMigrations &p_migrations,
		HashMap<String, FSCompletenessFinding> &r_findings) {
	r_findings.clear();
	Error open_error = OK;
	Ref<DirAccess> directory = DirAccess::open(p_directory, &open_error);
	if (directory.is_null()) {
		return open_error == OK ? ERR_CANT_OPEN : open_error;
	}
	if (directory->list_dir_begin() != OK) {
		return ERR_CANT_OPEN;
	}
	Vector<String> files;
	for (String entry = directory->get_next(); !entry.is_empty(); entry = directory->get_next()) {
		if (entry == "." || entry == ".." || entry == ".gitkeep") {
			continue;
		}
		if (directory->current_is_dir() || entry.get_extension().to_lower() != "json") {
			directory->list_dir_end();
			return ERR_INVALID_DATA;
		}
		files.push_back(p_directory.path_join(entry));
	}
	directory->list_dir_end();
	files.sort();

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
		if (!object_has_exact_fields(record, required_fields) ||
				!parse_schema_version(record["schema_version"])) {
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
			if (validate_permanent_test_path(path) != OK) {
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
			const FSCompletenessResolvedCell *cell = find_cell_by_id(p_resolution, resolved_case_id);
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

static Error validate_owned_report_path(
		const String &p_canonical_scratch_root, const String &p_report_path) {
	if (p_report_path.simplify_path() != p_report_path ||
			!TemporaryProjectTree::is_strict_descendant(p_canonical_scratch_root, p_report_path)) {
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

	Error stage(const String &p_canonical_scratch_root,
			const Vector<FSCompletenessProgram> &p_programs) {
		artifact_root = p_canonical_scratch_root.path_join("report-artifacts");
		if (!TemporaryProjectTree::is_strict_descendant(p_canonical_scratch_root, artifact_root)) {
			return ERR_UNAUTHORIZED;
		}
		Error error = ensure_directory(p_canonical_scratch_root, artifact_root);
		if (error != OK) {
			return error;
		}
		for (const String &surface : { String("text"), String("bytecode") }) {
			error = ensure_directory(p_canonical_scratch_root, artifact_root.path_join(surface));
			if (error != OK) {
				return error;
			}
		}

		Ref<DirAccess> filesystem = DirAccess::create(DirAccess::ACCESS_FILESYSTEM);
		if (filesystem.is_null()) {
			return ERR_UNAVAILABLE;
		}
		for (const FSCompletenessProgram &program : p_programs) {
			const String path = artifact_path(program);
			if (filesystem->is_link(path)) {
				return ERR_UNAUTHORIZED;
			}
			if (filesystem->file_exists(path)) {
				String canonical_path;
				Error read_error = OK;
				const String source = FileAccess::get_file_as_string(path, &read_error);
				if (TemporaryProjectTree::resolve_existing_owned_path(path, canonical_path) != OK ||
						canonical_path != path || read_error != OK || source != program.source) {
					return ERR_ALREADY_EXISTS;
				}
				continue;
			}
			Error open_error = OK;
			Ref<FileAccess> file = FileAccess::open(path, FileAccess::WRITE | FileAccess::WRITE_EXCL, &open_error);
			if (file.is_null()) {
				return open_error == OK ? ERR_CANT_CREATE : open_error;
			}
			created_files.push_back(path);
			if (!file->store_string(program.source)) {
				file.unref();
				return ERR_CANT_CREATE;
			}
			file->flush();
			const Error write_error = file->get_error();
			file.unref();
			if (write_error != OK) {
				return write_error;
			}
			String canonical_path;
			if (TemporaryProjectTree::resolve_existing_owned_path(path, canonical_path) != OK ||
					canonical_path != path) {
				return ERR_UNAUTHORIZED;
			}
		}
		return OK;
	}

	String artifact_path(const FSCompletenessProgram &p_program) const {
		return artifact_root.path_join(p_program.surface).path_join(p_program.case_id + ".fs");
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
				nullptr, nullptr) != 0 ?
				OK :
				FAILED;
	}
	return MoveFileW((LPCWSTR)temporary_utf16.get_data(), (LPCWSTR)report_utf16.get_data()) != 0 ? OK : FAILED;
#else
	// The temp file is a sibling of the destination, so the platform rename is one
	// process-visible atomic replacement. This does not promise power-loss durability.
	return DirAccess::rename_absolute(p_temporary_path, p_report_path);
#endif
}

static Error write_report_atomically(const String &p_canonical_scratch_root,
		const String &p_report_path, const Dictionary &p_report) {
	Error validation_error = validate_owned_report_path(p_canonical_scratch_root, p_report_path);
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
		temporary.unref();
		return ERR_CANT_CREATE;
	}
	temporary->flush();
	const Error write_error = temporary->get_error();
	temporary.unref();
	if (write_error != OK) {
		return write_error;
	}
	String canonical_temporary_path;
	if (TemporaryProjectTree::resolve_existing_owned_path(
				temporary_path, canonical_temporary_path) != OK ||
			canonical_temporary_path != temporary_path) {
		return ERR_UNAUTHORIZED;
	}
	validation_error = validate_owned_report_path(p_canonical_scratch_root, p_report_path);
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

static bool observed_expected_dimensions(
		const FSCompletenessResolvedCell &p_cell, const FSCompletenessRuntimeResult &p_actual) {
	for (const KeyValue<String, FSCompletenessResolvedDimension> &dimension : p_cell.dimensions) {
		if (p_actual.dimensions.get(dimension.key, Variant()) != dimension.value.expected) {
			return false;
		}
	}
	return true;
}

static void sort_cells_by_id(Vector<const FSCompletenessResolvedCell *> &r_cells) {
	for (int i = 1; i < r_cells.size(); i++) {
		const FSCompletenessResolvedCell *cell = r_cells[i];
		int position = i;
		while (position > 0 && cell->case_id < r_cells[position - 1]->case_id) {
			r_cells.write[position] = r_cells[position - 1];
			position--;
		}
		r_cells.write[position] = cell;
	}
}

} // namespace

Error FSCompletenessRunner::run(
		const FSCompletenessRunOptions &p_options, FSCompletenessRunResult &r_result) {
	r_result = FSCompletenessRunResult();
	if (p_options.catalog_root.is_empty() || p_options.family.is_empty() ||
			p_options.scratch_root.is_empty() || p_options.report_path.is_empty() ||
			p_options.family != "union_destination_membership") {
		return ERR_INVALID_PARAMETER;
	}
	String canonical_scratch_root;
	const Error scratch_error = resolve_or_create_owned_scratch_root(
			p_options.scratch_root, canonical_scratch_root);
	if (scratch_error != OK) {
		return scratch_error;
	}
	const Error report_path_error = validate_owned_report_path(
			canonical_scratch_root, p_options.report_path);
	if (report_path_error != OK) {
		return report_path_error;
	}

	Vector<String> errors;
	FSCompletenessCatalog catalog;
	Error error = catalog.load(p_options.catalog_root, errors);
	if (error != OK) {
		return error;
	}
	FSCompletenessManifest manifest;
	error = FSCompletenessManifest::load(
			p_options.catalog_root.path_join("rules").path_join(p_options.family + ".json"), manifest, errors);
	if (error != OK) {
		return error;
	}
	if (manifest.family != p_options.family) {
		return ERR_INVALID_DATA;
	}
	error = validate_manifest_vocabulary(manifest, catalog, errors);
	if (error != OK) {
		return error;
	}
	FSCompletenessResolution resolution;
	error = FSCompletenessGraph::resolve(manifest, catalog, resolution, errors);
	if (error != OK) {
		return error;
	}

	HashSet<String> current_ids;
	for (const FSCompletenessResolvedCell &cell : resolution.cells) {
		current_ids.insert(cell.case_id);
	}
	FSCompletenessMigrations migrations;
	error = migrations.load(p_options.catalog_root.path_join("migrations"), current_ids, errors);
	if (error != OK) {
		return error;
	}
	HashMap<String, FSCompletenessFinding> ledger;
	error = load_findings_ledger(p_options.catalog_root.path_join("findings"), p_options.family,
			resolution, current_ids, migrations, ledger);
	if (error != OK) {
		return error;
	}
	error = validate_witness_resolution(manifest, resolution);
	if (error != OK) {
		return error;
	}

	Vector<FSCompletenessProgram> programs;
	programs.reserve(resolution.cells.size());
	for (const FSCompletenessResolvedCell &cell : resolution.cells) {
		FSCompletenessProgram program;
		error = FSUnionCompletenessAdapter::render(cell, program);
		if (error != OK) {
			return error;
		}
		const FSCompletenessObservation analysis =
				FSUnionCompletenessAdapter::analyze(program, program.surface);
		if (!analysis.diagnostics.is_empty() || analysis.dimensions.get("analysis", String()) != "accept") {
			return ERR_INVALID_DATA;
		}
		programs.push_back(program);
	}
	ReportArtifactScope artifact_scope;
	error = artifact_scope.stage(canonical_scratch_root, programs);
	if (error != OK) {
		return error;
	}

	FSCompletenessRuntimeBatch batch;
	error = FSUnionCompletenessAdapter::execute(canonical_scratch_root, programs, batch);
	if (error != OK) {
		return error;
	}
	if (p_options.observation_mutator != nullptr) {
		for (const FSCompletenessResolvedCell &cell : resolution.cells) {
			FSCompletenessRuntimeResult *actual = runtime_result_for(cell, batch);
			if (actual == nullptr) {
				return ERR_INVALID_DATA;
			}
			p_options.observation_mutator(static_cast<FSCompletenessObservation &>(*actual));
		}
	}
	if (p_options.runtime_result_mutator != nullptr) {
		for (const FSCompletenessResolvedCell &cell : resolution.cells) {
			FSCompletenessRuntimeResult *actual = runtime_result_for(cell, batch);
			if (actual == nullptr) {
				return ERR_INVALID_DATA;
			}
			p_options.runtime_result_mutator(*actual);
		}
	}
	error = validate_witness_observations(manifest, resolution, batch);
	if (error != OK) {
		return error;
	}

	Vector<FSCompletenessFinding> findings;
	for (const FSCompletenessResolvedCell &cell : resolution.cells) {
		const FSCompletenessRuntimeResult *actual = runtime_result_for(cell, batch);
		const FSCompletenessProgram *program = find_program_by_id(programs, cell.case_id);
		if (actual == nullptr || program == nullptr) {
			return ERR_INVALID_DATA;
		}
		const String artifact_path = artifact_scope.artifact_path(*program);
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
		for (const String &dimension_name : sorted_dimension_keys(cell.dimensions)) {
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
	for (const FSCompletenessResolvedCell &cell : resolution.cells) {
		const String pair_key = semantic_pair_key(cell.coordinates);
		SurfacePair *pair = pairs.getptr(pair_key);
		if (pair == nullptr) {
			pairs[pair_key] = SurfacePair();
			pair = pairs.getptr(pair_key);
		}
		if (cell.coordinates.get("surface", String()) == "text") {
			if (pair->text != nullptr) {
				return ERR_INVALID_DATA;
			}
			pair->text = &cell;
		} else {
			if (pair->bytecode != nullptr) {
				return ERR_INVALID_DATA;
			}
			pair->bytecode = &cell;
		}
	}
	Vector<String> pair_keys;
	for (const KeyValue<String, SurfacePair> &pair : pairs) {
		pair_keys.push_back(pair.key);
	}
	pair_keys.sort();
	int parity_failures = 0;
	HashSet<String> parity_case_ids;
	for (const String &pair_key : pair_keys) {
		const SurfacePair &pair = pairs[pair_key];
		if (pair.text == nullptr || pair.bytecode == nullptr) {
			return ERR_INVALID_DATA;
		}
		const FSCompletenessRuntimeResult *text = runtime_result_for(*pair.text, batch);
		const FSCompletenessRuntimeResult *bytecode = runtime_result_for(*pair.bytecode, batch);
		if (text == nullptr || bytecode == nullptr) {
			return ERR_INVALID_DATA;
		}

		const Dictionary evidence = aggregate_parity_evidence(*pair.text, *pair.bytecode, *text, *bytecode);
		const bool pair_failed = evidence.has("output") || evidence.has("diagnostics") ||
				evidence.has("runtime_status") || !Dictionary(evidence.get("dimensions", Dictionary())).is_empty();
		if (!pair_failed) {
			continue;
		}
		parity_failures++;
		parity_case_ids.insert(pair.text->case_id);
		parity_case_ids.insert(pair.bytecode->case_id);
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
			finding.dimension = "text_bytecode_parity";
			finding.finding_id = make_finding_id(finding.case_id, finding.dimension);
			finding.expected = "matching_surface_observations";
			finding.parity_evidence = evidence;
			finding.actual = finding.parity_evidence;
			const FSCompletenessProgram *text_program = find_program_by_id(programs, pair.text->case_id);
			if (text_program == nullptr) {
				return ERR_INVALID_DATA;
			}
			finding.artifact_path = artifact_scope.artifact_path(*text_program);
			findings.push_back(finding);
		}
	}
	HashSet<String> reconciled_ledger_entries;
	for (FSCompletenessFinding &finding : findings) {
		const String reconciliation_key = finding.case_id + "|" + finding.dimension;
		const FSCompletenessFinding *known = ledger.getptr(reconciliation_key);
		if (known == nullptr) {
			continue;
		}
		finding.finding_id = known->finding_id;
		finding.classification = known->classification;
		finding.issue_url = known->issue_url;
		finding.closure_packet_url = known->closure_packet_url;
		finding.permanent_test_paths = known->permanent_test_paths;
		finding.migrated_from = known->migrated_from;
		finding.resolved_case_ids = known->resolved_case_ids;
		reconciled_ledger_entries.insert(reconciliation_key);
	}
	if (reconciled_ledger_entries.size() != ledger.size()) {
		return ERR_INVALID_DATA;
	}
	sort_findings(findings);

	Dictionary executed_by_surface;
	executed_by_surface["text"] = double(batch.text.size());
	executed_by_surface["bytecode"] = double(batch.bytecode.size());
	Dictionary coverage_by_chain_length;
	for (int length = 0; length <= manifest.max_chain_length; length++) {
		coverage_by_chain_length[String::num_int64(length)] = 0.0;
	}
	Dictionary coverage_by_dimension;
	for (const FSCompletenessResolvedCell &cell : resolution.cells) {
		for (const KeyValue<String, FSCompletenessResolvedDimension> &dimension : cell.dimensions) {
			const String chain_key = String::num_int64(dimension.value.canonical_provenance.size());
			coverage_by_chain_length[chain_key] = double(coverage_by_chain_length.get(chain_key, 0.0)) + 1.0;
			coverage_by_dimension[dimension.key] = double(coverage_by_dimension.get(dimension.key, 0.0)) + 1.0;
		}
	}
	coverage_by_dimension = sorted_dictionary_copy(coverage_by_dimension);

	Vector<const FSCompletenessResolvedCell *> sorted_cells;
	for (const FSCompletenessResolvedCell &cell : resolution.cells) {
		sorted_cells.push_back(&cell);
	}
	sort_cells_by_id(sorted_cells);
	Array cases;
	for (const FSCompletenessResolvedCell *cell : sorted_cells) {
		const FSCompletenessRuntimeResult *actual = runtime_result_for(*cell, batch);
		const FSCompletenessProgram *program = find_program_by_id(programs, cell->case_id);
		if (actual == nullptr || program == nullptr) {
			return ERR_INVALID_DATA;
		}
		bool passed = actual->passed && actual->status == "ok" && actual->diagnostics.is_empty() &&
				actual->produced_output == program->expected_output &&
				observed_expected_dimensions(*cell, *actual) && !parity_case_ids.has(cell->case_id);
		for (const FSCompletenessFinding &finding : findings) {
			if (finding.case_id == cell->case_id) {
				passed = false;
				break;
			}
		}
		Dictionary case_report;
		case_report["case_id"] = cell->case_id;
		case_report["coordinates"] = sorted_dictionary_copy(cell->coordinates);
		case_report["expected"] = expected_dimensions(*cell);
		case_report["actual"] = observed_dimensions(*cell, *actual);
		case_report["canonical_provenance"] = canonical_provenance(*cell);
		case_report["agreeing_provenance"] = all_agreeing_provenance(*cell);
		case_report["status"] = passed ? "passed" : "failed";
		case_report["passed"] = actual->passed;
		case_report["runtime_status"] = actual->status;
		Array diagnostics;
		for (const String &diagnostic : actual->diagnostics) {
			diagnostics.push_back(diagnostic);
		}
		case_report["diagnostics"] = diagnostics;
		case_report["produced_output"] = actual->produced_output;
		case_report["expected_output"] = program->expected_output;
		case_report["artifact_path"] = artifact_scope.artifact_path(*program);
		cases.push_back(case_report);
	}
	Array report_findings;
	const bool success = findings.is_empty();
	for (const FSCompletenessFinding &finding : findings) {
		report_findings.push_back(finding_report(finding));
	}

	Dictionary report;
	report["schema_version"] = 1.0;
	report["family"] = p_options.family;
	report["success"] = success;
	report["cell_count"] = double(resolution.cells.size());
	report["executed_by_surface"] = executed_by_surface;
	report["coverage_by_chain_length"] = coverage_by_chain_length;
	report["coverage_by_dimension"] = coverage_by_dimension;
	report["uncovered_required_dimensions"] = double(resolution.uncovered_dimension_count);
	report["text_bytecode_parity_failures"] = double(parity_failures);
	report["findings"] = report_findings;
	report["cases"] = cases;
	error = write_report_atomically(canonical_scratch_root, p_options.report_path, report);
	if (error != OK) {
		return error;
	}
	artifact_scope.commit();
	FSCompletenessRunResult completed;
	completed.success = success;
	completed.executed_cells = resolution.cells.size();
	completed.findings = findings;
	completed.report = report;
	r_result = completed;
	return success ? OK : FAILED;
}

} // namespace FSTests
