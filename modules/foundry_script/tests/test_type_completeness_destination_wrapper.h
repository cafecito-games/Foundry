/**************************************************************************/
/*  test_type_completeness_destination_wrapper.h                          */
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
#include "fs_test_language_lifecycle.h"
#include "fs_type_completeness_case_id.h"
#include "fs_type_completeness_common.h"
#include "fs_type_completeness_destination_wrapper_adapter.h"
#include "fs_type_completeness_graph.h"
#include "fs_type_completeness_json.h"
#include "fs_type_completeness_manifest.h"
#include "fs_type_completeness_runner.h"

#include "core/io/file_access.h"
#include "core/io/json.h"
#include "core/os/os.h"
#include "tests/test_macros.h"

namespace FSTests {

static const String destination_wrapper_catalog_root = "modules/foundry_script/tests/type_completeness";

// Family names under `rules/` whose manifest names the destination-wrapper adapter, read from the
// rule directory rather than from a list in a test: the catalog is the source of truth for which
// families exist, so a family added without its fixtures fails here instead of going unnoticed.
static Vector<String> destination_wrapper_rule_families() {
	Vector<String> files;
	Vector<Completeness::JsonDirectoryError> directory_errors;
	Completeness::JsonDirectoryPolicy policy;
	REQUIRE_EQ(Completeness::enumerate_json_directory(
					   destination_wrapper_catalog_root.path_join("rules"), policy, files, directory_errors),
			OK);
	Vector<String> families;
	for (const String &file : files) {
		FSCompletenessManifest manifest;
		Vector<String> errors;
		REQUIRE_MESSAGE(FSCompletenessManifest::load(file, manifest, errors) == OK, String(" | ").join(errors));
		if (manifest.adapter == FSDestinationWrapperAdapter::shared().id()) {
			families.push_back(manifest.family);
		}
	}
	families.sort();
	return families;
}

static Vector<String> destination_wrapper_parity_families() {
	Vector<String> families;
	for (const String &family : destination_wrapper_rule_families()) {
		if (family.begins_with("wrapper_parity_")) {
			families.push_back(family);
		}
	}
	return families;
}

static FSCompletenessResolution destination_wrapper_resolution(const String &p_family) {
	FSCompletenessCatalog catalog;
	FSCompletenessManifest manifest;
	Vector<String> errors;
	REQUIRE_MESSAGE(catalog.load(destination_wrapper_catalog_root, errors) == OK, String(" | ").join(errors));
	REQUIRE_MESSAGE(FSCompletenessManifest::load(
							destination_wrapper_catalog_root.path_join(vformat("rules/%s.json", p_family)),
							manifest, errors) == OK,
			String(" | ").join(errors));
	REQUIRE_MESSAGE(validate_manifest_vocabulary(manifest, catalog, errors) == OK, String(" | ").join(errors));

	FSCompletenessResolution resolution;
	REQUIRE_MESSAGE(FSCompletenessGraph::resolve(manifest, catalog, resolution, errors) == OK,
			String(" | ").join(errors));
	CHECK_EQ(resolution.uncovered_dimension_count, 0);
	CHECK_EQ(resolution.ambiguous_dimension_count, 0);
	return resolution;
}

// The relation or exception a provenance step applied, which is the step's identity. A step that has
// already been narrowed to its identity stays as it is, so narrowing a narrowed document is a no-op
// and both sides of a comparison can go through this.
static Variant destination_wrapper_provenance_step_id(const Variant &p_step) {
	if (p_step.get_type() != Variant::DICTIONARY) {
		return p_step;
	}
	const Dictionary step = p_step;
	const String exception_id = step.get("exception_id", String());
	return exception_id.is_empty() ? step.get("relation_id", String()) : Variant(exception_id);
}

static Variant destination_wrapper_narrowed_provenance(const Variant &p_provenance) {
	if (p_provenance.get_type() != Variant::DICTIONARY) {
		return p_provenance;
	}
	const Dictionary provenance = p_provenance;
	Dictionary narrowed;
	for (const String &dimension : Completeness::sorted_dictionary_keys(provenance)) {
		const Variant chain_value = provenance[dimension];
		if (chain_value.get_type() != Variant::ARRAY) {
			narrowed[dimension] = chain_value;
			continue;
		}
		const Array chain = chain_value;
		Array ids;
		for (int index = 0; index < chain.size(); index++) {
			ids.push_back(destination_wrapper_provenance_step_id(chain[index]));
		}
		narrowed[dimension] = ids;
	}
	return narrowed;
}

// How many chains agreed on each dimension. The chains themselves are combinatorial - every order in
// which the source, destination, and census-child relations compose reaches the same cell - so the
// tracked evidence keeps the count, which a lost or a spurious derivation still changes, instead of
// hundreds of coordinate dictionaries per cell.
static Variant destination_wrapper_agreement_counts(const Variant &p_provenance) {
	if (p_provenance.get_type() != Variant::DICTIONARY) {
		return p_provenance;
	}
	const Dictionary provenance = p_provenance;
	Dictionary counts;
	for (const String &dimension : Completeness::sorted_dictionary_keys(provenance)) {
		const Variant chains = provenance[dimension];
		// A count is published as a JSON number, and a parsed JSON number is a float; producing an int
		// here would make a tracked document differ from the run that produced it by type alone.
		counts[dimension] =
				chains.get_type() == Variant::ARRAY ? Variant(double(Array(chains).size())) : chains;
	}
	return counts;
}

// The tracked evidence of one published report: everything that does not depend on where the run
// staged its files, with provenance narrowed to identities. Applying it to an already narrowed
// document changes nothing, so a produced report and a tracked one are compared through exactly one
// reduction rather than through a reduction on one side and a format on the other.
// True for a report member that describes where a run staged its files or which build produced it
// rather than what it observed. The runner owns the list, so a member added there is dropped here
// without a second list to keep in step.
static bool destination_wrapper_is_non_evidence_member(const String &p_key) {
	static const Vector<String> non_evidence = FSCompletenessRunner::non_evidence_report_members();
	return p_key == "artifact_path" || non_evidence.has(p_key);
}

static Variant destination_wrapper_tracked_evidence(const Variant &p_value) {
	if (p_value.get_type() == Variant::DICTIONARY) {
		const Dictionary source = p_value;
		Dictionary evidence;
		for (const String &key : Completeness::sorted_dictionary_keys(source)) {
			if (destination_wrapper_is_non_evidence_member(key)) {
				continue;
			}
			if (key == "canonical_provenance") {
				evidence[key] = destination_wrapper_narrowed_provenance(source[key]);
				continue;
			}
			if (key == "agreeing_provenance") {
				evidence[key] = destination_wrapper_agreement_counts(source[key]);
				continue;
			}
			evidence[key] = destination_wrapper_tracked_evidence(source[key]);
		}
		return evidence;
	}
	if (p_value.get_type() == Variant::ARRAY) {
		const Array source = p_value;
		Array evidence;
		for (int index = 0; index < source.size(); index++) {
			evidence.push_back(destination_wrapper_tracked_evidence(source[index]));
		}
		return evidence;
	}
	return p_value;
}

static Variant destination_wrapper_tracked_document(const String &p_relative_path) {
	Error read_error = OK;
	const String source =
			FileAccess::get_file_as_string(destination_wrapper_catalog_root.path_join(p_relative_path), &read_error);
	REQUIRE_EQ(read_error, OK);
	Variant document;
	Vector<String> errors;
	REQUIRE_MESSAGE(parse_type_completeness_json(source, String(), document, errors) == OK,
			String(" | ").join(errors));
	return document;
}

// The first place two evidence documents differ, as a JSON path. A family publishes hundreds of
// cells, so a whole-document inequality is not a diagnosable failure on its own.
static String destination_wrapper_first_difference(
		const Variant &p_produced, const Variant &p_expected, const String &p_path) {
	if (p_produced.get_type() != p_expected.get_type()) {
		return vformat("%s: produced type %d, expected type %d", p_path, int(p_produced.get_type()),
				int(p_expected.get_type()));
	}
	if (p_produced.get_type() == Variant::DICTIONARY) {
		const Dictionary produced = p_produced;
		const Dictionary expected = p_expected;
		const Vector<String> produced_keys = Completeness::sorted_dictionary_keys(produced);
		const Vector<String> expected_keys = Completeness::sorted_dictionary_keys(expected);
		if (produced_keys != expected_keys) {
			return vformat("%s: produced keys [%s], expected keys [%s]", p_path,
					String(", ").join(produced_keys), String(", ").join(expected_keys));
		}
		for (const String &key : produced_keys) {
			const String difference =
					destination_wrapper_first_difference(produced[key], expected[key], p_path + "." + key);
			if (!difference.is_empty()) {
				return difference;
			}
		}
		return String();
	}
	if (p_produced.get_type() == Variant::ARRAY) {
		const Array produced = p_produced;
		const Array expected = p_expected;
		if (produced.size() != expected.size()) {
			return vformat("%s: produced %d entries, expected %d", p_path, produced.size(), expected.size());
		}
		for (int index = 0; index < produced.size(); index++) {
			const String difference = destination_wrapper_first_difference(
					produced[index], expected[index], vformat("%s[%d]", p_path, index));
			if (!difference.is_empty()) {
				return difference;
			}
		}
		return String();
	}
	return p_produced == p_expected
			? String()
			: vformat("%s: produced '%s', expected '%s'", p_path, String(p_produced), String(p_expected));
}

TEST_SUITE("[Modules][FoundryScript][TypeCompleteness][DestinationWrapper]") {
	TEST_CASE("TypeCompleteness DestinationWrapper serves exactly the families the rule directory declares") {
		CHECK_EQ(FSDestinationWrapperAdapter::shared().id(), "destination_wrapper");
		const Vector<String> declared = FSDestinationWrapperAdapter::families();
		const Vector<String> tracked = destination_wrapper_rule_families();
		REQUIRE_EQ(declared.size(), tracked.size());
		for (int index = 0; index < declared.size(); index++) {
			CAPTURE(index);
			CHECK_EQ(declared[index], tracked[index]);
		}
	}

	TEST_CASE("TypeCompleteness DestinationWrapper census_child equals the representation inventory") {
		const Dictionary representations = destination_wrapper_tracked_document("census/representations.json");
		const Array inventory = representations["representations"];
		HashSet<String> expected_leaves;
		expected_leaves.insert("none");
		for (int index = 0; index < inventory.size(); index++) {
			const Dictionary representation = inventory[index];
			const String representation_id = representation["id"];
			const Array child_slots = representation.get("child_slots", Array());
			for (int slot_index = 0; slot_index < child_slots.size(); slot_index++) {
				const Dictionary slot = child_slots[slot_index];
				expected_leaves.insert(vformat("%s.%s", representation_id, String(slot["id"])));
			}
		}

		const Dictionary partition = destination_wrapper_tracked_document("partitions/census_child.json");
		CHECK_EQ(String(partition["axis"]), "census_child");
		const Array declared_leaves = partition["leaves"];
		HashSet<String> declared;
		for (int index = 0; index < declared_leaves.size(); index++) {
			const String leaf = declared_leaves[index];
			CHECK_FALSE(declared.has(leaf));
			declared.insert(leaf);
			CHECK_MESSAGE(expected_leaves.has(leaf),
					vformat("census_child leaf '%s' names no representation child slot", leaf));
		}
		for (const String &leaf : expected_leaves) {
			CHECK_MESSAGE(declared.has(leaf),
					vformat("representation child slot '%s' has no census_child leaf", leaf));
		}
		CHECK_EQ(declared.size(), expected_leaves.size());
	}

	TEST_CASE("TypeCompleteness DestinationWrapper partitions the census inventory into observed and not") {
		const Dictionary partition = destination_wrapper_tracked_document("partitions/census_child.json");
		const Array declared_leaves = partition["leaves"];
		const FSCompletenessFamilyAdapter &adapter = FSDestinationWrapperAdapter::shared();
		HashSet<String> observed;
		for (const String &leaf : destination_wrapper_census_children()) {
			observed.insert(leaf);
		}
		HashSet<String> unobserved;
		for (const String &leaf : destination_wrapper_unobserved_census_children()) {
			CHECK_FALSE(observed.has(leaf));
			unobserved.insert(leaf);
		}
		// A leaf is renderable exactly when the adapter can read the child back out of the
		// representation it names. A renderable leaf the adapter never reads would let a family claim
		// a census child while observing nothing about it.
		for (int index = 0; index < declared_leaves.size(); index++) {
			const String leaf = declared_leaves[index];
			CAPTURE(leaf);
			CHECK_EQ(adapter.can_render("census_child", leaf), observed.has(leaf));
			const bool classified = observed.has(leaf) || unobserved.has(leaf);
			CHECK(classified);
		}
		CHECK_EQ(observed.size() + unobserved.size(), declared_leaves.size());
	}

	TEST_CASE("TypeCompleteness DestinationWrapper observes every census child a family may select") {
		const bool initialized_here = ensure_fs_language_initialized();
		for (const String &leaf : destination_wrapper_census_children()) {
			CAPTURE(leaf);
			FSCompletenessResolvedCell cell;
			cell.coordinates["destination"] = "plain";
			cell.coordinates["source_proof"] = "static_member";
			cell.coordinates["boundary"] = "argument_binding";
			cell.coordinates["census_child"] = leaf;
			cell.coordinates["surface"] = "text";
			cell.case_id = FSCompletenessCaseID::make("wrapper_parity_argument_binding", cell.coordinates);

			FSCompletenessProgram program;
			REQUIRE_EQ(FSDestinationWrapperAdapter::shared().render(cell, program), OK);
			Dictionary runtime_context;
			runtime_context["produced_output"] = program.expected_output;
			const FSCompletenessObservation observation =
					FSDestinationWrapperAdapter::shared().inspect_runtime_contract(program, runtime_context);
			CHECK_MESSAGE(observation.diagnostics.is_empty(), String(" | ").join(observation.diagnostics));
			const String evidence = observation.dimensions.get("census_child_evidence", String());
			CHECK_FALSE(evidence.is_empty());
			const bool reports_absent = evidence == "absent";
			const bool is_the_absent_leaf = leaf == "none";
			CHECK_EQ(reports_absent, is_the_absent_leaf);
		}
		if (initialized_here) {
			FSLanguage::get_singleton()->finish();
		}
	}

	TEST_CASE("TypeCompleteness DestinationWrapper reads the carrier from each boundary's own slot") {
		// One expected carrier per destination. The observation reads it back from the slot the
		// boundary wrote through, so this also proves each boundary inspects its own site rather than
		// a declaration that merely happens to have the same type.
		struct DestinationCarrier {
			const char *destination;
			const char *carrier;
		};
		const DestinationCarrier carriers[] = {
			{ "plain", "plain_destination" },
			{ "union", "admitting_alternative" },
			{ "optional", "optional_payload" },
			{ "container_element", "element_slot" },
			{ "generic_argument", "reified_argument" },
			{ "tuple_field", "tuple_slot" },
			{ "callable_slot", "callable_slot" },
			{ "nominal_class", "nominal_instance" },
			{ "trait", "trait_witness" },
		};

		// Compiling a program needs a live language; a direct observation does not go through the
		// runner, which is what brings one up for a family run.
		const bool initialized_here = ensure_fs_language_initialized();
		for (const String &boundary : destination_wrapper_boundaries()) {
			for (const DestinationCarrier &expected : carriers) {
				const String family = "wrapper_parity_" + boundary;
				FSCompletenessResolvedCell cell;
				cell.coordinates["destination"] = expected.destination;
				cell.coordinates["source_proof"] = "static_member";
				cell.coordinates["boundary"] = boundary;
				cell.coordinates["census_child"] = "none";
				cell.coordinates["surface"] = "text";
				cell.case_id = FSCompletenessCaseID::make(family, cell.coordinates);

				FSCompletenessProgram program;
				const Error render_error = FSDestinationWrapperAdapter::shared().render(cell, program);
				if (render_error != OK) {
					// The one combination the language has no spelling for.
					CHECK_EQ(String(expected.destination), "union");
					CHECK_EQ(boundary, "container_element_store");
					continue;
				}
				CAPTURE(boundary);
				CAPTURE(expected.destination);
				Dictionary runtime_context;
				runtime_context["produced_output"] = program.expected_output;
				const FSCompletenessObservation observation =
						FSDestinationWrapperAdapter::shared().inspect_runtime_contract(program, runtime_context);
				CHECK_MESSAGE(observation.diagnostics.is_empty(), String(" | ").join(observation.diagnostics));
				CHECK_EQ(String(observation.dimensions.get("stored_carrier", String())), expected.carrier);
			}
		}
		if (initialized_here) {
			FSLanguage::get_singleton()->finish();
		}
	}

	TEST_CASE("TypeCompleteness DestinationWrapper observes a cell the same way however often it runs") {
		// Every analyzer-only read - a census `parser_data_type.*` slot, a typed local's descriptor -
		// comes from the record the program was compiled from. Reading one by analyzing the source
		// again would ask the question after the program's own classes are registered, and the answer
		// would then depend on what else the process had already registered: a different shard
		// composition or a different platform would classify the same cell differently.
		struct RepeatedCell {
			const char *destination;
			const char *boundary;
			const char *census_child;
		};
		const RepeatedCell cells[] = {
			// The cell a sharded Linux run classified differently from an unsharded run.
			{ "tuple_field", "container_element_store", "parser_data_type.container_element_types" },
			{ "container_element", "container_element_store", "parser_data_type.container_element_types" },
			{ "union", "assignment", "parser_data_type.type_arguments" },
			{ "plain", "assignment", "parser_data_type.type_parameter_bound" },
			{ "trait", "member_store", "parser_data_type.method_rest_parameter_type" },
		};

		const bool initialized_here = ensure_fs_language_initialized();
		for (const RepeatedCell &repeated : cells) {
			CAPTURE(repeated.destination);
			CAPTURE(repeated.boundary);
			CAPTURE(repeated.census_child);
			FSCompletenessResolvedCell cell;
			cell.coordinates["destination"] = repeated.destination;
			cell.coordinates["source_proof"] = "static_member";
			cell.coordinates["boundary"] = repeated.boundary;
			cell.coordinates["census_child"] = repeated.census_child;
			cell.coordinates["surface"] = "text";
			cell.case_id =
					FSCompletenessCaseID::make(String("wrapper_parity_") + repeated.boundary, cell.coordinates);

			FSCompletenessProgram program;
			REQUIRE_EQ(FSDestinationWrapperAdapter::shared().render(cell, program), OK);
			Dictionary runtime_context;
			runtime_context["produced_output"] = program.expected_output;

			String first_carrier;
			String first_census;
			for (int attempt = 0; attempt < 3; attempt++) {
				CAPTURE(attempt);
				const FSCompletenessObservation observation =
						FSDestinationWrapperAdapter::shared().inspect_runtime_contract(program, runtime_context);
				CHECK_MESSAGE(observation.diagnostics.is_empty(), String(" | ").join(observation.diagnostics));
				const String carrier = observation.dimensions.get("stored_carrier", String());
				const String census = observation.dimensions.get("census_child_evidence", String());
				CHECK_FALSE(carrier.is_empty());
				CHECK_FALSE(census.is_empty());
				if (attempt == 0) {
					first_carrier = carrier;
					first_census = census;
					continue;
				}
				CHECK_EQ(carrier, first_carrier);
				CHECK_EQ(census, first_census);
			}
		}
		if (initialized_here) {
			FSLanguage::get_singleton()->finish();
		}
	}

	TEST_CASE("TypeCompleteness DestinationWrapper reports an unreadable census child structurally") {
		// A census child the adapter cannot read back is a defect in the harness or the catalog, not an
		// observation about the product. Filing it as a product mismatch would let an infrastructure
		// failure read as a finding and would leave the cell's required dimension simply missing.
		const bool initialized_here = ensure_fs_language_initialized();
		FSCompletenessResolvedCell cell;
		cell.coordinates["destination"] = "plain";
		cell.coordinates["source_proof"] = "static_member";
		cell.coordinates["boundary"] = "argument_binding";
		cell.coordinates["census_child"] = "parser_data_type.container_element_types";
		cell.coordinates["surface"] = "text";
		cell.case_id = FSCompletenessCaseID::make("wrapper_parity_argument_binding", cell.coordinates);

		FSCompletenessProgram program;
		REQUIRE_EQ(FSDestinationWrapperAdapter::shared().render(cell, program), OK);
		Dictionary runtime_context;
		runtime_context["produced_output"] = program.expected_output;
		const FSCompletenessObservation observed =
				FSDestinationWrapperAdapter::shared().inspect_runtime_contract(program, runtime_context);
		CHECK_MESSAGE(observed.diagnostics.is_empty(), String(" | ").join(observed.diagnostics));
		CHECK_FALSE(String(observed.dimensions.get("census_child_evidence", String())).is_empty());

		// The same coordinates over a program that never declares the witness the leaf names.
		FSCompletenessProgram stripped = program;
		String without_witness;
		for (const String &line : program.source.split("\n")) {
			if (!line.contains("census_container")) {
				without_witness += line + "\n";
			}
		}
		stripped.source = without_witness.trim_suffix("\n");
		REQUIRE_NE(stripped.source, program.source);
		Error structural_error = OK;
		const FSCompletenessObservation refused = FSDestinationWrapperAdapter::shared().inspect_runtime_contract(
				stripped, runtime_context, &structural_error);
		CHECK_EQ(structural_error, ERR_INVALID_DATA);
		CHECK_FALSE(refused.diagnostics.is_empty());
		if (initialized_here) {
			FSLanguage::get_singleton()->finish();
		}
	}

	TEST_CASE("TypeCompleteness DestinationWrapper refuses a census child it cannot observe") {
		const Vector<String> unobserved = destination_wrapper_unobserved_census_children();
		REQUIRE_FALSE(unobserved.is_empty());
		for (const String &leaf : unobserved) {
			CAPTURE(leaf);
			CHECK_FALSE(FSDestinationWrapperAdapter::shared().can_render("census_child", leaf));
			FSCompletenessResolvedCell cell;
			cell.coordinates["destination"] = "plain";
			cell.coordinates["source_proof"] = "static_member";
			cell.coordinates["boundary"] = "argument_binding";
			cell.coordinates["census_child"] = leaf;
			cell.coordinates["surface"] = "text";
			cell.case_id = FSCompletenessCaseID::make("wrapper_parity_argument_binding", cell.coordinates);
			FSCompletenessProgram program;
			CHECK_EQ(FSDestinationWrapperAdapter::shared().render(cell, program), ERR_INVALID_DATA);
		}
	}

	TEST_CASE("TypeCompleteness DestinationWrapper fails the census cell when a reflection hint is lost") {
		// Restores the seam however the case leaves, so one failing expectation cannot make every later
		// test in the process observe a blanked hint.
		struct BlankedReflectionHint {
			BlankedReflectionHint() { DestinationWrapperInternal::set_blank_reflection_hint_for_test(true); }
			~BlankedReflectionHint() { DestinationWrapperInternal::set_blank_reflection_hint_for_test(false); }
		};

		TemporaryProjectTree tree(vformat("type_completeness_blank_hint_%d", OS::get_singleton()->get_process_id()));
		REQUIRE(tree.is_valid());
		FSCompletenessRunOptions options;
		options.catalog_root = destination_wrapper_catalog_root;
		options.family = "wrapper_parity_reflective_write";
		options.scratch_root = tree.root;
		options.report_path = tree.root.path_join("report.json");

		FSCompletenessRunResult result;
		{
			BlankedReflectionHint blanked;
			REQUIRE_EQ(FSCompletenessRunner::run(options, result), FAILED);
		}
		CHECK(result.structural_failures.is_empty());
		CHECK_EQ(result.outcome, "product_mismatch");
		CHECK_FALSE(result.success);

		int census_findings = 0;
		for (const FSCompletenessFinding &finding : result.findings) {
			if (finding.dimension == "census_child_evidence") {
				census_findings++;
				CHECK_EQ(String(finding.expected), "reflection_hint_23_39");
				CHECK_NE(String(finding.actual), String(finding.expected));
			}
		}
		int judged_hint_cells = 0;
		int not_covered_hint_cells = 0;
		const Array cases = Dictionary(result.report).get("cases", Array());
		for (int index = 0; index < cases.size(); index++) {
			const Dictionary case_record = cases[index];
			const Dictionary expected = case_record.get("expected", Dictionary());
			if (String(expected.get("census_child_evidence", String())) != "reflection_hint_23_39") {
				continue;
			}
			if (String(case_record.get("status", String())) == "not_covered") {
				not_covered_hint_cells++;
				continue;
			}
			judged_hint_cells++;
		}
		// One cell per destination, source proof, and surface of the hint-string leaf. How many of them
		// a build can judge is a property of the build, so the matrix is asserted whole and the blanked
		// hint has to fail every cell this build did judge.
		CHECK_EQ(judged_hint_cells + not_covered_hint_cells, 126);
		CHECK_EQ(census_findings, judged_hint_cells);
	}

	TEST_CASE("TypeCompleteness DestinationWrapper refuses a destination a boundary cannot spell") {
		// A typed container enforces exactly one element type, so a union element slot is not a
		// spellable destination. The adapter refuses the coordinates rather than rendering a program
		// that cannot parse, which is what keeps an unrenderable cell from reading as a rejection.
		FSCompletenessResolvedCell cell;
		cell.coordinates["destination"] = "union";
		cell.coordinates["source_proof"] = "static_member";
		cell.coordinates["boundary"] = "container_element_store";
		cell.coordinates["census_child"] = "none";
		cell.coordinates["surface"] = "text";
		cell.case_id = FSCompletenessCaseID::make("wrapper_parity_container_element_store", cell.coordinates);

		FSCompletenessProgram program;
		CHECK_EQ(FSDestinationWrapperAdapter::shared().render(cell, program), ERR_INVALID_DATA);
		CHECK(program.source.is_empty());

		FSCompletenessResolvedCell accepted = cell;
		accepted.coordinates["destination"] = "optional";
		accepted.case_id =
				FSCompletenessCaseID::make("wrapper_parity_container_element_store", accepted.coordinates);
		CHECK_EQ(FSDestinationWrapperAdapter::shared().render(accepted, program), OK);
		CHECK_FALSE(program.source.is_empty());
	}

	TEST_CASE("TypeCompleteness DestinationWrapper families resolve every required dimension") {
		const Vector<String> families = destination_wrapper_parity_families();
		REQUIRE_FALSE(families.is_empty());
		for (const String &family : families) {
			CAPTURE(family);
			const FSCompletenessResolution resolution = destination_wrapper_resolution(family);
			CHECK_FALSE(resolution.cells.is_empty());
		}
	}

	TEST_CASE("TypeCompleteness DestinationWrapper case identities match the tracked case ID files") {
		for (const String &family : destination_wrapper_parity_families()) {
			CAPTURE(family);
			const FSCompletenessResolution resolution = destination_wrapper_resolution(family);
			Vector<String> resolved_ids;
			for (const FSCompletenessResolvedCell &cell : resolution.cells) {
				resolved_ids.push_back(cell.case_id);
			}
			resolved_ids.sort();

			const Dictionary expected =
					destination_wrapper_tracked_document(vformat("expected_case_ids/%s.json", family));
			CHECK_EQ(String(expected["family"]), family);
			const Array expected_ids = expected["case_ids"];
			REQUIRE_EQ(resolved_ids.size(), expected_ids.size());
			for (int index = 0; index < resolved_ids.size(); index++) {
				CAPTURE(index);
				CHECK_EQ(resolved_ids[index], String(expected_ids[index]));
			}
		}
	}

	TEST_CASE("TypeCompleteness DestinationWrapper reports match the captured evidence documents") {
		for (const String &family : destination_wrapper_parity_families()) {
			CAPTURE(family);
			TemporaryProjectTree tree(vformat("type_completeness_%s_%d", family,
					OS::get_singleton()->get_process_id()));
			REQUIRE(tree.is_valid());
			FSCompletenessRunOptions options;
			options.catalog_root = destination_wrapper_catalog_root;
			options.family = family;
			options.scratch_root = tree.root;
			options.report_path = tree.root.path_join("report.json");

			FSCompletenessRunResult result;
			const Error run_error = FSCompletenessRunner::run(options, result);
			// A refusal reports its stage rather than only an error code: a family that renders
			// hundreds of cells is not diagnosable from `ERR_INVALID_DATA` alone.
			String refusal;
			for (const FSCompletenessStructuralFailure &failure : result.structural_failures) {
				refusal += vformat(" | %s: %s (case '%s', witness '%s')", failure.stage, failure.detail,
						failure.case_id, failure.witness_id);
			}
			REQUIRE_MESSAGE(run_error == OK,
					vformat("run failed with error %d, outcome '%s'%s", run_error, result.outcome, refusal));
			CHECK_MESSAGE(result.structural_failures.is_empty(), refusal);
			CHECK_EQ(result.outcome, "passed");
			CHECK(result.success);
			CHECK(FSCompletenessRunner::report_carries_evidence(result.report));

			// Every declared exception has to be observed by the run, otherwise the manifest claims a
			// carve-out no cell exercises.
			const Array exceptions = result.report["exceptions"];
			CHECK_FALSE(exceptions.is_empty());
			for (int index = 0; index < exceptions.size(); index++) {
				const Dictionary exception_report = exceptions[index];
				CAPTURE(String(exception_report["exception_id"]));
				// An exception this build could not judge says so, and says it about every run on this
				// configuration. Anything else is a carve-out no cell exercised.
				const String not_covered_reason = exception_report.get("not_covered_reason", String());
				if (!not_covered_reason.is_empty()) {
					CHECK_EQ(not_covered_reason,
							String(FSCompletenessNotCoveredReason::DIAGNOSTICS_UNAVAILABLE_IN_CONFIGURATION));
					CHECK_FALSE(bool(exception_report["witnessed"]));
					continue;
				}
				CHECK(bool(exception_report["witnessed"]));
			}

			const Variant expected = destination_wrapper_tracked_document(
					vformat("expected_reports/%s.json", family));
			// The tracked document was captured on a build whose analyzer warns. Narrowing both sides to
			// what this build could have observed is the identity on such a build, so this stays the same
			// byte-identity comparison there, and compares observations rather than configurations on a
			// build that cannot produce the warnings the tracked cells carry.
			const Dictionary configuration = Dictionary(result.report).get("configuration", Dictionary());
			const Variant produced_evidence = destination_wrapper_tracked_evidence(
					FSCompletenessRunner::evidence_observable_in_configuration(result.report, configuration));
			const Variant expected_evidence = destination_wrapper_tracked_evidence(
					FSCompletenessRunner::evidence_observable_in_configuration(expected, configuration));
			CHECK_MESSAGE(JSON::stringify(produced_evidence, "  ") == JSON::stringify(expected_evidence, "  "),
					destination_wrapper_first_difference(produced_evidence, expected_evidence, "$"));
		}
	}
}

} // namespace FSTests
