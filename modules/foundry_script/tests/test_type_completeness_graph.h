/**************************************************************************/
/*  test_type_completeness_graph.h                                        */
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

#include "fs_type_completeness_graph.h"
#include "fs_type_completeness_manifest.h"

#include "tests/test_macros.h"

namespace FSTests {

static const String type_completeness_graph_root = "modules/foundry_script/tests/type_completeness";

static bool graph_errors_contain(const Vector<String> &p_errors, const String &p_needle) {
	for (const String &error : p_errors) {
		if (error.contains(p_needle)) {
			return true;
		}
	}
	return false;
}

static const FSCompletenessResolvedCell *find_completeness_cell(const FSCompletenessResolution &p_resolution,
		const String &p_destination, const String &p_source_proof, const String &p_boundary, const String &p_surface) {
	for (const FSCompletenessResolvedCell &cell : p_resolution.cells) {
		if (cell.coordinates.get("destination", String()) == p_destination &&
				cell.coordinates.get("source_proof", String()) == p_source_proof &&
				cell.coordinates.get("boundary", String()) == p_boundary &&
				cell.coordinates.get("surface", String()) == p_surface) {
			return &cell;
		}
	}
	return nullptr;
}

static void load_type_completeness_graph_inputs(FSCompletenessCatalog &r_catalog,
		FSCompletenessManifest &r_manifest, Vector<String> &r_errors) {
	REQUIRE_MESSAGE(r_catalog.load(type_completeness_graph_root, r_errors) == OK, String(" | ").join(r_errors));
	REQUIRE_MESSAGE(FSCompletenessManifest::load(
							type_completeness_graph_root.path_join("rules/union_destination_membership.json"), r_manifest, r_errors) == OK,
			String(" | ").join(r_errors));
	REQUIRE_MESSAGE(validate_manifest_vocabulary(r_manifest, r_catalog, r_errors) == OK, String(" | ").join(r_errors));
}

TEST_SUITE("[Modules][FoundryScript][TypeCompleteness][Graph]") {
	TEST_CASE("TypeCompleteness Graph resolves the union pilot with semantic provenance") {
		FSCompletenessCatalog catalog;
		FSCompletenessManifest manifest;
		Vector<String> errors;
		load_type_completeness_graph_inputs(catalog, manifest, errors);

		FSCompletenessResolution resolution;
		REQUIRE_MESSAGE(FSCompletenessGraph::resolve(manifest, catalog, resolution, errors) == OK,
				String(" | ").join(errors));
		CHECK(errors.is_empty());
		CHECK_EQ(resolution.cells.size(), 40);
		CHECK_EQ(resolution.max_observed_chain_length, 3);
		CHECK_EQ(resolution.uncovered_dimension_count, 0);
		CHECK_EQ(resolution.ambiguous_dimension_count, 0);

		const FSCompletenessResolvedCell *plain_numeric = find_completeness_cell(
				resolution, "plain", "numeric_constant", "argument_binding", "text");
		REQUIRE(plain_numeric != nullptr);
		const FSCompletenessResolvedDimension *plain_carrier = plain_numeric->find_dimension("stored_carrier");
		REQUIRE(plain_carrier != nullptr);
		CHECK_EQ(plain_carrier->expected, Variant("plain_destination"));
		REQUIRE_EQ(plain_carrier->canonical_provenance.size(), 1);
		const FSCompletenessProvenanceStep &carrier_step = plain_carrier->canonical_provenance[0];
		CHECK_EQ(carrier_step.relation_id, "plain_numeric_constant_parity");
		CHECK_EQ(carrier_step.source_coordinates.get("source_proof", String()), Variant("static_member"));
		CHECK_EQ(carrier_step.target_coordinates.get("source_proof", String()), Variant("numeric_constant"));
		CHECK_EQ(carrier_step.input_dimensions.get("analysis", String()), Variant("accept"));
		CHECK_EQ(carrier_step.output_dimensions.get("stored_carrier", String()), Variant("plain_destination"));
		CHECK_FALSE(plain_carrier->agreeing_provenance.is_empty());

		const FSCompletenessResolvedCell *union_gradual = find_completeness_cell(
				resolution, "union", "gradual", "argument_binding", "text");
		REQUIRE(union_gradual != nullptr);
		const FSCompletenessResolvedDimension *membership = union_gradual->find_dimension("runtime_obligation");
		REQUIRE(membership != nullptr);
		CHECK_EQ(membership->expected, Variant("union_membership_check"));
		REQUIRE_EQ(membership->canonical_provenance.size(), 1);
		CHECK_EQ(membership->canonical_provenance[0].relation_id, "union_wrapper_parity");
		CHECK_EQ(membership->canonical_provenance[0].exception_id, "unproven_source_requires_membership");

		const FSCompletenessResolvedCell *erased_reflective_union = find_completeness_cell(
				resolution, "union", "erased", "reflective_write", "text");
		REQUIRE(erased_reflective_union != nullptr);
		const FSCompletenessResolvedDimension *erased_analysis = erased_reflective_union->find_dimension("analysis");
		REQUIRE(erased_analysis != nullptr);
		CHECK_EQ(erased_analysis->expected, Variant("accept"));
		REQUIRE_EQ(erased_analysis->canonical_provenance.size(), 3);
		CHECK_EQ(erased_analysis->canonical_provenance[0].relation_id, "gradual_to_erased_parity");
		CHECK_EQ(erased_analysis->canonical_provenance[1].relation_id, "reflective_boundary_parity");
		CHECK_EQ(erased_analysis->canonical_provenance[2].relation_id, "union_wrapper_parity");
		CHECK_EQ(erased_analysis->canonical_provenance[2].exception_id, "unproven_source_requires_membership");
	}

	TEST_CASE("TypeCompleteness Graph rejects invalid derivation graphs") {
		FSCompletenessCatalog catalog;
		FSCompletenessManifest manifest;
		Vector<String> errors;
		load_type_completeness_graph_inputs(catalog, manifest, errors);

		FSCompletenessResolution resolution;

		SUBCASE("No anchor can reach most cells") {
			errors.clear();
			resolution = FSCompletenessResolution();
			FSCompletenessManifest invalid = manifest;
			invalid.anchors.clear();
			CHECK_EQ(FSCompletenessGraph::resolve(invalid, catalog, resolution, errors), ERR_INVALID_DATA);
			CHECK(graph_errors_contain(errors, "has no reachable disposition"));
		}

		SUBCASE("A fixed dimension transform cannot be omitted") {
			errors.clear();
			resolution = FSCompletenessResolution();
			FSCompletenessManifest invalid = manifest;
			for (FSCompletenessRelation &relation : invalid.relations) {
				if (relation.id == "plain_numeric_constant_parity") {
					relation.derive.erase("stored_carrier");
				}
			}
			CHECK_EQ(FSCompletenessGraph::resolve(invalid, catalog, resolution, errors), ERR_INVALID_DATA);
			CHECK(graph_errors_contain(errors, "stored_carrier"));
		}

		SUBCASE("A relation cannot repeat or revisit its target") {
			errors.clear();
			resolution = FSCompletenessResolution();
			FSCompletenessManifest invalid = manifest;
			FSCompletenessRelation loop;
			loop.id = "unproven_to_erased_loop";
			Dictionary unproven;
			unproven["class"] = "unproven";
			loop.from["source_proof"] = unproven;
			loop.to["source_proof"] = "erased";
			loop.derive["analysis"] = "same";
			invalid.relations.push_back(loop);
			CHECK_EQ(FSCompletenessGraph::resolve(invalid, catalog, resolution, errors), ERR_INVALID_DATA);
			const bool reports_cycle = graph_errors_contain(errors, "revisit") || graph_errors_contain(errors, "repeat");
			CHECK(reports_cycle);
		}

		SUBCASE("Agreeing cells cannot carry conflicting outcomes") {
			errors.clear();
			resolution = FSCompletenessResolution();
			FSCompletenessManifest invalid = manifest;
			FSCompletenessRelation conflict;
			conflict.id = "conflicting_plain_boundary";
			conflict.from["destination"] = "plain";
			conflict.from["boundary"] = "argument_binding";
			conflict.to["boundary"] = "reflective_write";
			conflict.derive["analysis"] = "reject";
			invalid.relations.push_back(conflict);
			CHECK_EQ(FSCompletenessGraph::resolve(invalid, catalog, resolution, errors), ERR_INVALID_DATA);
			CHECK(graph_errors_contain(errors, "incompatible outcomes"));
		}

		SUBCASE("Incomparable maximal predicates are ambiguous") {
			errors.clear();
			resolution = FSCompletenessResolution();
			FSCompletenessManifest invalid = manifest;
			FSCompletenessRelation peer;
			peer.id = "static_member_wrapper";
			peer.from["source_proof"] = "static_member";
			peer.to["destination"] = "union";
			peer.derive["analysis"] = "same";
			invalid.relations.push_back(peer);
			CHECK_EQ(FSCompletenessGraph::resolve(invalid, catalog, resolution, errors), ERR_INVALID_DATA);
			CHECK(graph_errors_contain(errors, "incomparable maximal predicates"));
		}

		SUBCASE("A matching exception replaces the entire parent derivation") {
			errors.clear();
			resolution = FSCompletenessResolution();
			FSCompletenessManifest invalid = manifest;
			invalid.exceptions.write[0].derive.erase("analysis");
			CHECK_EQ(FSCompletenessGraph::resolve(invalid, catalog, resolution, errors), ERR_INVALID_DATA);
			CHECK(graph_errors_contain(errors, "analysis"));
			CHECK(graph_errors_contain(errors, "has no reachable disposition"));
		}

		SUBCASE("An exception predicate must strictly narrow its parent") {
			errors.clear();
			resolution = FSCompletenessResolution();
			FSCompletenessManifest invalid = manifest;
			invalid.exceptions.write[0].when.clear();
			invalid.exceptions.write[0].derive.erase("analysis");
			CHECK_EQ(FSCompletenessGraph::resolve(invalid, catalog, resolution, errors), ERR_INVALID_DATA);
			const bool reports_specificity = graph_errors_contain(errors, "strictly narrow") ||
					graph_errors_contain(errors, "tied maximal predicates");
			CHECK(reports_specificity);
		}

		SUBCASE("Cycles beyond the coverage bound are still rejected") {
			errors.clear();
			resolution = FSCompletenessResolution();
			FSCompletenessManifest invalid = manifest;
			const char *proofs[] = { "static_member", "numeric_constant", "gradual", "erased" };
			for (int i = 0; i < 4; i++) {
				FSCompletenessRelation edge;
				edge.id = vformat("long_cycle_%d", i);
				edge.from["destination"] = "union";
				edge.from["boundary"] = "reflective_write";
				edge.from["source_proof"] = proofs[i];
				edge.to["source_proof"] = proofs[(i + 1) % 4];
				edge.derive["analysis"] = "same";
				invalid.relations.push_back(edge);
			}
			CHECK_EQ(FSCompletenessGraph::resolve(invalid, catalog, resolution, errors), ERR_INVALID_DATA);
			CHECK(graph_errors_contain(errors, "revisit"));
		}
	}
}

} // namespace FSTests
