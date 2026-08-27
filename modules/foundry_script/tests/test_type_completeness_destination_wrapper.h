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

// Rejection text of each form the probe classifier recognizes, spelled the way the runner reports it.
// A test drives the classification from these rather than from a coordinate, which is the whole point
// of the dimension: the same cell reports a different obligation when its probe reports differently.
// A probe whose boundary never carried the value: whatever refused it did so after the crossing, at
// the destination's own use, so the boundary's check is not what this cell observed.
static const char *destination_wrapper_deferred_use_rejection =
		"FS_TEST_RUNTIME_ERROR\nconstruction skipped\nboundary skipped\n>> SCRIPT ERROR at case.fs:1 on "
		"test(): Invalid type in function 'identity_uint'. Argument 1 has type \"RefCounted\", but the "
		"parameter requires \"uint\".\n";
static const char *destination_wrapper_typed_rejection =
		"FS_TEST_RUNTIME_ERROR\nconstruction skipped\nboundary carried\n>> SCRIPT ERROR at case.fs:1 on "
		"test(): Trying to "
		"assign value of type 'Object' to a variable of type 'uint'.\n";
static const char *destination_wrapper_membership_rejection =
		"FS_TEST_RUNTIME_ERROR\nconstruction skipped\nboundary carried\n>> SCRIPT ERROR at case.fs:1 on "
		"test(): Cannot store "
		"a value of type \"RefCounted\" in a slot of type \"String | uint\": the value is none of the "
		"alternatives.\n";
// The same typed rejection reported by a program whose wrapper took the probe value before the
// boundary refused it: the boundary is what checked, so the construction stage is not blamed.
static const char *destination_wrapper_typed_rejection_after_construction =
		"FS_TEST_RUNTIME_ERROR\nconstruction box other\nboundary carried\n>> SCRIPT ERROR at case.fs:1 on "
		"test(): Trying to "
		"assign value of type 'Object' to a variable of type 'uint'.\n";
// A wrapper that refused the probe value and said so without aborting, and one that aborted before
// the construction line could print. Neither reached the boundary under test.
static const char *destination_wrapper_construction_rejection =
		"FS_TEST_RUNTIME_ERROR\n>> ERROR: Attempted to set a variable of type 'Object' into a TypedArray "
		"of type 'uint'.\nconstruction array true 39 0 empty\nboundary carried\n";
static const char *destination_wrapper_construction_abort =
		"FS_TEST_RUNTIME_ERROR\n>> SCRIPT ERROR at case.fs:1 on test(): Invalid assignment of property or "
		"key 'value' with value of type 'RefCounted' on a base object of type 'RefCounted (Carrier)'.\n";

// The runtime context one direct observation needs: what the cell's own program printed, and what its
// negative probe did. Both are supplied, because a cell whose probe never ran has no evidence for its
// runtime obligation and is refused rather than reported without one.
static Dictionary destination_wrapper_runtime_context(const String &p_produced_output,
		const String &p_probe_status = "runtime_error",
		const String &p_probe_output = destination_wrapper_typed_rejection) {
	Dictionary context;
	context["produced_output"] = p_produced_output;
	context["negative_probe_status"] = p_probe_status;
	context["negative_probe_output"] = p_probe_output;
	return context;
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
			const Dictionary runtime_context = destination_wrapper_runtime_context(program.expected_output);
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
					// The two element types a typed container has no typed slot for.
					const bool has_no_typed_element_slot =
							String(expected.destination) == "union" || String(expected.destination) == "optional";
					CHECK(has_no_typed_element_slot);
					CHECK_EQ(boundary, "container_element_store");
					continue;
				}
				CAPTURE(boundary);
				CAPTURE(expected.destination);
				const Dictionary runtime_context =
						destination_wrapper_runtime_context(program.expected_output);
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
			const Dictionary runtime_context = destination_wrapper_runtime_context(program.expected_output);

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
		const Dictionary runtime_context = destination_wrapper_runtime_context(program.expected_output);
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
		// spellable destination. The adapter refuses it rather than rendering a program that cannot
		// parse, which is what keeps an unrenderable cell from reading as a rejection.
		//
		// `Array[uint?]` is spellable and is rendered, even though the language gives it no typed
		// container: that is a product defect, and a defect is represented rather than deleted.
		for (const String &unspellable : { String("union") }) {
			CAPTURE(unspellable);
			FSCompletenessResolvedCell cell;
			cell.coordinates["destination"] = unspellable;
			cell.coordinates["source_proof"] = "static_member";
			cell.coordinates["boundary"] = "container_element_store";
			cell.coordinates["census_child"] = "none";
			cell.coordinates["surface"] = "text";
			cell.case_id =
					FSCompletenessCaseID::make("wrapper_parity_container_element_store", cell.coordinates);

			FSCompletenessProgram program;
			CHECK_EQ(FSDestinationWrapperAdapter::shared().render(cell, program), ERR_INVALID_DATA);
			CHECK(program.source.is_empty());
		}

		FSCompletenessResolvedCell accepted;
		accepted.coordinates["destination"] = "container_element";
		accepted.coordinates["source_proof"] = "static_member";
		accepted.coordinates["boundary"] = "container_element_store";
		accepted.coordinates["census_child"] = "none";
		accepted.coordinates["surface"] = "text";
		accepted.case_id =
				FSCompletenessCaseID::make("wrapper_parity_container_element_store", accepted.coordinates);
		FSCompletenessProgram program;
		CHECK_EQ(FSDestinationWrapperAdapter::shared().render(accepted, program), OK);
		CHECK_FALSE(program.source.is_empty());
	}

	TEST_CASE("TypeCompleteness DestinationWrapper renders one negative probe per destination and boundary") {
		// Every wrapper-parity cell carries a probe, and cells that differ only in what their source
		// proves or which census child they declare carry the same one: the probe measures the boundary
		// and the destination, so staging it per cell would run the same program seven times over.
		HashMap<String, String> probes_by_key;
		for (const String &boundary : destination_wrapper_boundaries()) {
			for (const String &destination : destination_wrapper_destinations()) {
				HashSet<String> keys_for_pair;
				for (const String &source_proof : destination_wrapper_source_proofs()) {
					for (const String &census_child : { String("none"),
								 String("parser_data_type.container_element_types") }) {
						FSCompletenessResolvedCell cell;
						cell.coordinates["destination"] = destination;
						cell.coordinates["source_proof"] = source_proof;
						cell.coordinates["boundary"] = boundary;
						cell.coordinates["census_child"] = census_child;
						cell.coordinates["surface"] = "text";
						cell.case_id =
								FSCompletenessCaseID::make("wrapper_parity_" + boundary, cell.coordinates);

						FSCompletenessProgram program;
						if (FSDestinationWrapperAdapter::shared().render(cell, program) != OK) {
							continue;
						}
						CAPTURE(boundary);
						CAPTURE(destination);
						CAPTURE(source_proof);
						CAPTURE(census_child);
						REQUIRE_FALSE(program.negative_probe.is_empty());
						CHECK_FALSE(program.negative_probe.staged_document.is_empty());
						CHECK(program.negative_probe.source.contains("RefCounted.new()"));
						CHECK_NE(program.negative_probe.source, program.source);
						keys_for_pair.insert(program.negative_probe.key);

						const String *known = probes_by_key.getptr(program.negative_probe.key);
						if (known == nullptr) {
							probes_by_key.insert(program.negative_probe.key, program.negative_probe.source);
						} else {
							CHECK_EQ(*known, program.negative_probe.source);
						}
					}
				}
				CHECK(keys_for_pair.size() <= 1);
			}
		}
		CHECK_FALSE(probes_by_key.is_empty());
	}

	TEST_CASE("TypeCompleteness DestinationWrapper reads the runtime obligation off its probe") {
		// The obligation is what the probe did, not what the destination's descriptor says: the same
		// cell, observed against four probe verdicts, reports four obligations. A descriptor-derived
		// dimension would report the same value for all four.
		struct ProbeVerdict {
			const char *status;
			const char *output;
			const char *obligation;
		};
		const ProbeVerdict verdicts[] = {
			{ "runtime_error", destination_wrapper_typed_rejection, "typed_destination_check" },
			{ "runtime_error", destination_wrapper_membership_rejection, "union_membership_check" },
			{ "runtime_error", destination_wrapper_typed_rejection_after_construction, "typed_destination_check" },
			{ "runtime_error", destination_wrapper_construction_rejection, "wrapper_construction_check" },
			{ "runtime_error", destination_wrapper_construction_abort, "wrapper_construction_check" },
			{ "runtime_error", destination_wrapper_deferred_use_rejection, "deferred_use_check" },
			// A boundary that raised nothing and left its destination holding something no destination
			// in the matrix describes took the probe value: that is a silent admission.
			{ "ok", "FS_TEST_OK\nother\n", "unchecked_destination" },
			{ "ok", "FS_TEST_OK\nuint 0\n", "silent_destination_refusal" },
			{ "runtime_error",
					"FS_TEST_RUNTIME_ERROR\nconstruction skipped\nboundary carried\n>> SCRIPT ERROR: something "
					"else entirely.\n",
					"unclassified_rejection" },
		};

		const bool initialized_here = ensure_fs_language_initialized();
		FSCompletenessResolvedCell cell;
		cell.coordinates["destination"] = "plain";
		cell.coordinates["source_proof"] = "static_member";
		cell.coordinates["boundary"] = "assignment";
		cell.coordinates["census_child"] = "none";
		cell.coordinates["surface"] = "text";
		cell.case_id = FSCompletenessCaseID::make("wrapper_parity_assignment", cell.coordinates);
		FSCompletenessProgram program;
		REQUIRE_EQ(FSDestinationWrapperAdapter::shared().render(cell, program), OK);

		for (const ProbeVerdict &verdict : verdicts) {
			CAPTURE(String(verdict.obligation));
			CAPTURE(String(verdict.output));
			const FSCompletenessObservation observation =
					FSDestinationWrapperAdapter::shared().inspect_runtime_contract(program,
							destination_wrapper_runtime_context(
									program.expected_output, verdict.status, verdict.output));
			CHECK_MESSAGE(observation.diagnostics.is_empty(), String(" | ").join(observation.diagnostics));
			CHECK_EQ(String(observation.dimensions.get("runtime_obligation", String())), verdict.obligation);
		}

		// A cell whose probe never ran has no evidence for its obligation, so the observation is
		// refused as a harness defect rather than published without the dimension.
		Dictionary without_probe;
		without_probe["produced_output"] = program.expected_output;
		Error structural_error = OK;
		const FSCompletenessObservation refused = FSDestinationWrapperAdapter::shared().inspect_runtime_contract(
				program, without_probe, &structural_error);
		CHECK_EQ(structural_error, ERR_INVALID_DATA);
		CHECK_FALSE(refused.dimensions.has("runtime_obligation"));
		if (initialized_here) {
			FSLanguage::get_singleton()->finish();
		}
	}

	TEST_CASE("TypeCompleteness DestinationWrapper reads the stored carrier off its readback line") {
		// The carrier is the token the program printed after the store, so a destination that kept its
		// descriptor while storing into a different shape reports a different carrier. Feeding the same
		// cell another destination's readback proves the dimension follows the output and not the
		// coordinate; feeding it a token no destination produces reports corruption.
		const bool initialized_here = ensure_fs_language_initialized();
		FSCompletenessResolvedCell cell;
		cell.coordinates["destination"] = "plain";
		cell.coordinates["source_proof"] = "static_member";
		cell.coordinates["boundary"] = "assignment";
		cell.coordinates["census_child"] = "none";
		cell.coordinates["surface"] = "text";
		cell.case_id = FSCompletenessCaseID::make("wrapper_parity_assignment", cell.coordinates);
		FSCompletenessProgram program;
		REQUIRE_EQ(FSDestinationWrapperAdapter::shared().render(cell, program), OK);

		const FSCompletenessObservation intact = FSDestinationWrapperAdapter::shared().inspect_runtime_contract(
				program, destination_wrapper_runtime_context(program.expected_output));
		CHECK_MESSAGE(intact.diagnostics.is_empty(), String(" | ").join(intact.diagnostics));
		CHECK_EQ(String(intact.dimensions.get("stored_carrier", String())), "plain_destination");

		const FSCompletenessObservation elsewhere =
				FSDestinationWrapperAdapter::shared().inspect_runtime_contract(program,
						destination_wrapper_runtime_context("uint 5\ncarrier uint 5\n"));
		CHECK_EQ(String(elsewhere.dimensions.get("stored_carrier", String())), "nominal_instance");

		const FSCompletenessObservation corrupted =
				FSDestinationWrapperAdapter::shared().inspect_runtime_contract(
						program, destination_wrapper_runtime_context("uint 5\nint 5\n"));
		CHECK_EQ(String(corrupted.dimensions.get("stored_carrier", String())), "corrupted_carrier");
		if (initialized_here) {
			FSLanguage::get_singleton()->finish();
		}
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
			CHECK_MESSAGE(result.structural_failures.is_empty(), refusal);
			// A family is clean when nothing it observed is unaccounted for. A product defect the ledger
			// classifies is accounted for: the cells still expect the law and still fail against it, which
			// is how a known gap is represented rather than deleted. Anything the ledger does not name is
			// a regression and fails here.
			const Dictionary ledger = Dictionary(result.report).get("ledger", Dictionary());
			HashSet<String> reconciled;
			const Array reconciled_records = ledger.get("reconciled", Array());
			for (int index = 0; index < reconciled_records.size(); index++) {
				reconciled.insert(Dictionary(reconciled_records[index]).get("finding_id", String()));
			}
			CHECK(Array(ledger.get("stale", Array())).is_empty());
			String unreconciled;
			for (const FSCompletenessFinding &finding : result.findings) {
				if (!reconciled.has(finding.finding_id)) {
					unreconciled += vformat(" | %s on %s: expected %s, actual %s", finding.dimension,
							finding.case_id, String(finding.expected), String(finding.actual));
				}
			}
			CHECK_MESSAGE(unreconciled.is_empty(), unreconciled);
			const bool published_a_verdict = run_error == OK || result.outcome == "product_mismatch";
			REQUIRE_MESSAGE(published_a_verdict,
					vformat("run failed with error %d, outcome '%s'%s", run_error, result.outcome, refusal));
			const bool has_findings = !result.findings.is_empty();
			const String expected_outcome = has_findings ? "product_mismatch" : "passed";
			CHECK_EQ(result.outcome, expected_outcome);
			CHECK_EQ(result.success, !has_findings);
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
					// The reason names a capability this build lacks, and it comes from the closed
					// vocabulary rather than from a spelling repeated here.
					bool declared_reason = false;
					for (const char *reason : FSCompletenessNotCoveredReason::ALL) {
						declared_reason = declared_reason || not_covered_reason == reason;
					}
					CHECK_MESSAGE(declared_reason, not_covered_reason);
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
