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

static FSCompletenessFinding *find_direct_finding(Vector<FSCompletenessFinding> &r_findings,
		const String &p_case_id, const String &p_dimension) {
	for (FSCompletenessFinding &finding : r_findings) {
		if (finding.case_id == p_case_id && finding.dimension == p_dimension) {
			return &finding;
		}
	}
	return nullptr;
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
	return report;
}

static void sort_findings(Vector<FSCompletenessFinding> &r_findings) {
	for (int i = 1; i < r_findings.size(); i++) {
		const FSCompletenessFinding finding = r_findings[i];
		int position = i;
		while (position > 0 && finding.finding_id < r_findings[position - 1].finding_id) {
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
				if (actual->dimensions.get(dimension.key, Variant()) != dimension.value.expected) {
					return ERR_INVALID_DATA;
				}
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
				if (actual->dimensions.get(dimension.key, Variant()) != dimension.value.expected) {
					return ERR_INVALID_DATA;
				}
			}
			if (!observed_parent_dimension) {
				return ERR_INVALID_DATA;
			}
		}
	}
	return OK;
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
		if (finding_ids.has(finding.finding_id) || finding.family != p_family ||
				!classification_is_known(finding.classification) ||
				(!finding.issue_url.begins_with("https://") && !finding.issue_url.begins_with("http://")) ||
				(!finding.closure_packet_url.begins_with("https://") &&
						!finding.closure_packet_url.begins_with("http://"))) {
			return ERR_INVALID_DATA;
		}
		finding_ids.insert(finding.finding_id);

		if (!p_current_ids.has(finding.case_id)) {
			const Vector<String> migrated_ids = p_migrations.resolve(finding.case_id);
			if (migrated_ids.size() != 1 || !p_current_ids.has(migrated_ids[0])) {
				return ERR_INVALID_DATA;
			}
			finding.case_id = migrated_ids[0];
		}
		const FSCompletenessResolvedCell *cell = find_cell_by_id(p_resolution, finding.case_id);
		if (cell == nullptr ||
				(finding.dimension != "text_bytecode_parity" && !cell->dimensions.has(finding.dimension))) {
			return ERR_INVALID_DATA;
		}

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
			observed_paths.insert(path);
			finding.permanent_test_paths.push_back(path);
		}

		const String reconciliation_key = finding.case_id + "|" + finding.dimension;
		if (r_findings.has(reconciliation_key)) {
			return ERR_INVALID_DATA;
		}
		r_findings[reconciliation_key] = finding;
	}
	return OK;
}

static Error write_report_atomically(const String &p_report_path, const Dictionary &p_report) {
	const String contents = JSON::stringify(p_report, "\t", false, true) + "\n";
	String temporary_path;
	Ref<FileAccess> temporary;
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
	temporary->store_string(contents);
	const Error write_error = temporary->get_error();
	temporary.unref();
	if (write_error != OK) {
		DirAccess::remove_absolute(temporary_path);
		return write_error;
	}
	const Error rename_error = DirAccess::rename_absolute(temporary_path, p_report_path);
	if (rename_error != OK) {
		DirAccess::remove_absolute(temporary_path);
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
	if (p_options.report_path.simplify_path() != p_options.report_path ||
			!TemporaryProjectTree::is_strict_descendant(canonical_scratch_root, p_options.report_path)) {
		return ERR_UNAUTHORIZED;
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
	error = validate_witness_observations(manifest, resolution, batch);
	if (error != OK) {
		return error;
	}

	Vector<FSCompletenessFinding> findings;
	for (const FSCompletenessResolvedCell &cell : resolution.cells) {
		const FSCompletenessRuntimeResult *actual = runtime_result_for(cell, batch);
		if (actual == nullptr) {
			return ERR_INVALID_DATA;
		}
		for (const String &dimension_name : sorted_dimension_keys(cell.dimensions)) {
			const FSCompletenessResolvedDimension &dimension = cell.dimensions[dimension_name];
			const Variant observed = actual->dimensions.get(dimension_name, Variant());
			if (observed == dimension.expected) {
				continue;
			}
			FSCompletenessFinding finding;
			finding.finding_id = make_finding_id(cell.case_id, dimension_name);
			finding.case_id = cell.case_id;
			finding.family = p_options.family;
			finding.dimension = dimension_name;
			finding.expected = dimension.expected;
			finding.actual = observed;
			finding.artifact_path = canonical_scratch_root.path_join(cell.case_id + ".fs");
			findings.push_back(finding);
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

		Vector<String> dimension_names = sorted_dimension_keys(pair.text->dimensions);
		for (const String &dimension_name : sorted_dimension_keys(pair.bytecode->dimensions)) {
			if (!dimension_names.has(dimension_name)) {
				dimension_names.push_back(dimension_name);
			}
		}
		dimension_names.sort();
		bool pair_failed = text->produced_output != bytecode->produced_output;
		bool parity_attached = false;
		for (const String &dimension_name : dimension_names) {
			const Variant text_value = text->dimensions.get(dimension_name, Variant());
			const Variant bytecode_value = bytecode->dimensions.get(dimension_name, Variant());
			if (text_value == bytecode_value) {
				continue;
			}
			pair_failed = true;
			FSCompletenessFinding *direct =
					find_direct_finding(findings, pair.text->case_id, dimension_name);
			if (direct == nullptr) {
				direct = find_direct_finding(findings, pair.bytecode->case_id, dimension_name);
			}
			if (direct != nullptr) {
				direct->parity_evidence = parity_evidence(
						text_value, bytecode_value, pair.text->case_id, pair.bytecode->case_id);
				parity_attached = true;
			}
		}
		if (!pair_failed) {
			continue;
		}
		parity_failures++;
		if (!parity_attached) {
			FSCompletenessFinding finding;
			finding.case_id = pair.text->case_id;
			finding.family = p_options.family;
			finding.dimension = "text_bytecode_parity";
			finding.finding_id = make_finding_id(finding.case_id, finding.dimension);
			finding.expected = "matching_surface_observations";
			finding.parity_evidence = parity_evidence(text->produced_output,
					bytecode->produced_output, pair.text->case_id, pair.bytecode->case_id);
			finding.actual = finding.parity_evidence;
			finding.artifact_path = canonical_scratch_root.path_join(pair.text->case_id + ".fs");
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
		if (actual == nullptr) {
			return ERR_INVALID_DATA;
		}
		bool passed = actual->passed && actual->diagnostics.is_empty() &&
				observed_expected_dimensions(*cell, *actual);
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
		case_report["artifact_path"] = canonical_scratch_root.path_join(cell->case_id + ".fs");
		cases.push_back(case_report);
	}
	Array report_findings;
	bool success = true;
	for (const FSCompletenessFinding &finding : findings) {
		report_findings.push_back(finding_report(finding));
		if (finding.classification == "unclassified") {
			success = false;
		}
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
	error = write_report_atomically(p_options.report_path, report);
	if (error != OK) {
		return error;
	}
	FSCompletenessRunResult completed;
	completed.success = success;
	completed.executed_cells = resolution.cells.size();
	completed.findings = findings;
	completed.report = report;
	r_result = completed;
	return success ? OK : FAILED;
}

} // namespace FSTests
