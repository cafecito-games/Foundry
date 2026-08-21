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

#include "fs_temporary_project_tree.h"
#include "fs_type_completeness_case_id.h"
#include "fs_type_completeness_graph.h"
#include "fs_type_completeness_manifest.h"

#include "core/os/os.h"
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

static int graph_error_count(const Vector<String> &p_errors, const String &p_needle) {
	int count = 0;
	for (const String &error : p_errors) {
		if (error.contains(p_needle)) {
			count++;
		}
	}
	return count;
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

static String completeness_migration_tree_name(const String &p_name) {
	return vformat("%s_%d", p_name, OS::get_singleton()->get_process_id());
}

static HashSet<String> completeness_current_ids(std::initializer_list<const char *> p_ids) {
	HashSet<String> ids;
	for (const char *id : p_ids) {
		ids.insert(id);
	}
	return ids;
}

TEST_SUITE("[Modules][FoundryScript][TypeCompleteness][Graph]") {
	TEST_CASE("TypeCompleteness Graph case IDs are canonical and collision resistant") {
		Dictionary first;
		first["surface"] = "text";
		first["destination"] = "union";
		Dictionary reversed;
		reversed["destination"] = "union";
		reversed["surface"] = "text";

		const String first_id = FSCompletenessCaseID::make("assignment_compatibility", first);
		const String reversed_id = FSCompletenessCaseID::make("assignment_compatibility", reversed);
		CHECK_EQ(first_id, reversed_id);
		CHECK(first_id.begins_with("fstc-v1-"));
		CHECK_EQ(first_id.length(), 28);
		for (int i = 8; i < first_id.length(); i++) {
			const char32_t character = first_id[i];
			CHECK(((character >= '0' && character <= '9') || (character >= 'a' && character <= 'f')));
		}

		CHECK_NE(FSCompletenessCaseID::make("another_family", first), first_id);
		Dictionary changed = first.duplicate();
		changed["surface"] = "bytecode";
		CHECK_NE(FSCompletenessCaseID::make("assignment_compatibility", changed), first_id);

		Dictionary escaped;
		escaped["a|"] = "%=|";
		escaped["a"] = "|=%";
		CHECK_EQ(FSCompletenessCaseID::canonical_coordinates(escaped), "a=%7C%3D%25|a%7C=%25%3D%7C");
		Dictionary collision_attempt;
		collision_attempt["a"] = "|=%|a|=%=|";
		CHECK_NE(FSCompletenessCaseID::canonical_coordinates(escaped),
				FSCompletenessCaseID::canonical_coordinates(collision_attempt));

		Dictionary unexpected_number;
		unexpected_number["coordinate"] = 1;
		Dictionary unexpected_string;
		unexpected_string["coordinate"] = "1";
		CHECK_EQ(FSCompletenessCaseID::canonical_coordinates(unexpected_number),
				FSCompletenessCaseID::canonical_coordinates(unexpected_number));
		CHECK_NE(FSCompletenessCaseID::canonical_coordinates(unexpected_number),
				FSCompletenessCaseID::canonical_coordinates(unexpected_string));

		Dictionary nested_first;
		nested_first["z"] = 2;
		nested_first["a"] = 1;
		Dictionary nested_reversed;
		nested_reversed["a"] = 1;
		nested_reversed["z"] = 2;
		Dictionary unexpected_nested_first;
		unexpected_nested_first["coordinate"] = nested_first;
		Dictionary unexpected_nested_reversed;
		unexpected_nested_reversed["coordinate"] = nested_reversed;
		CHECK_EQ(FSCompletenessCaseID::canonical_coordinates(unexpected_nested_first),
				FSCompletenessCaseID::canonical_coordinates(unexpected_nested_reversed));
	}

	TEST_CASE("TypeCompleteness Graph assigns distinct stable IDs to the union pilot") {
		FSCompletenessCatalog catalog;
		FSCompletenessManifest manifest;
		Vector<String> errors;
		load_type_completeness_graph_inputs(catalog, manifest, errors);

		FSCompletenessResolution resolution;
		REQUIRE_MESSAGE(FSCompletenessGraph::resolve(manifest, catalog, resolution, errors) == OK,
				String(" | ").join(errors));
		REQUIRE_EQ(resolution.cells.size(), 40);
		HashSet<String> ids;
		for (const FSCompletenessResolvedCell &cell : resolution.cells) {
			CHECK(cell.case_id.begins_with("fstc-v1-"));
			CHECK_EQ(cell.case_id, FSCompletenessCaseID::make(manifest.family, cell.coordinates));
			CHECK_FALSE(ids.has(cell.case_id));
			ids.insert(cell.case_id);
		}
		CHECK_EQ(ids.size(), 40);
	}

	TEST_CASE("TypeCompleteness Graph migration aliases load and resolve atomically") {
		TemporaryProjectTree tree(completeness_migration_tree_name("type_completeness_migration_split"));
		REQUIRE(tree.is_valid());
		tree.write_file("migrations/v1.json", R"JSON({
  "schema_version": 1,
  "migrations": [{"old_id": "a", "new_ids": ["b", "c"], "reason": "The case split."}]
})JSON");

		FSCompletenessMigrations migrations;
		Vector<String> errors;
		CHECK_EQ(migrations.load(tree.root.path_join("migrations"), completeness_current_ids({ "b", "c" }), errors), OK);
		CHECK(errors.is_empty());
		CHECK_EQ(migrations.resolve("a"), Vector<String>({ "b", "c" }));
		CHECK(migrations.resolve("unknown").is_empty());

		tree.write_file("migrations/v1.json", R"JSON({
  "schema_version": 1,
  "migrations": [{"old_id": "a", "new_ids": ["missing"], "reason": "Invalid reload."}]
})JSON");
		errors.push_back("stale");
		CHECK_EQ(migrations.load(tree.root.path_join("migrations"), completeness_current_ids({ "b", "c" }), errors),
				ERR_INVALID_DATA);
		CHECK_FALSE(errors.has("stale"));
		CHECK(migrations.resolve("a").is_empty());
	}

	TEST_CASE("TypeCompleteness Graph migration aliases reject cycles") {
		TemporaryProjectTree tree(completeness_migration_tree_name("type_completeness_migration_cycle"));
		REQUIRE(tree.is_valid());
		tree.write_file("migrations/v1.json", R"JSON({
  "schema_version": 1,
  "migrations": [
    {"old_id": "a", "new_ids": ["b"], "reason": "Forward."},
    {"old_id": "b", "new_ids": ["a"], "reason": "Backward."}
  ]
})JSON");

		FSCompletenessMigrations migrations;
		Vector<String> errors;
		CHECK_EQ(migrations.load(tree.root.path_join("migrations"), completeness_current_ids({ "a", "b" }), errors),
				ERR_INVALID_DATA);
		CHECK(graph_errors_contain(errors, "migration alias cycle"));
		CHECK(migrations.resolve("a").is_empty());
	}

	TEST_CASE("TypeCompleteness Graph migration aliases aggregate deterministic validation errors") {
		TemporaryProjectTree tree(completeness_migration_tree_name("type_completeness_migration_invalid"));
		REQUIRE(tree.is_valid());
		tree.write_file("migrations/z.json", R"JSON({
  "schema_version": 1,
  "migrations": [{"old_id": "dup", "new_ids": ["current"], "reason": "Second file."}]
})JSON");
		tree.write_file("migrations/a.json", R"JSON({
  "schema_version": 1,
  "migrations": [
    {"old_id": "dup", "new_ids": ["current"], "reason": "First file."},
    {"old_id": "self", "new_ids": ["self"], "reason": "Self."},
    {"old_id": "orphan", "new_ids": ["missing"], "reason": "Orphan."},
    {"old_id": "empty", "new_ids": [], "reason": "Empty."},
    {"old_id": "duplicates", "new_ids": ["current", "current", ""], "reason": "Duplicates."},
    {"old_id": "no_reason", "new_ids": ["current"], "reason": ""}
  ]
})JSON");

		FSCompletenessMigrations migrations;
		Vector<String> errors;
		CHECK_EQ(migrations.load(tree.root.path_join("migrations"), completeness_current_ids({ "current", "self" }), errors),
				ERR_INVALID_DATA);
		REQUIRE(errors.size() >= 8);
		Vector<String> sorted_errors = errors;
		sorted_errors.sort();
		CHECK_EQ(errors, sorted_errors);
		CHECK(graph_errors_contain(errors, "duplicate old_id 'dup'"));
		CHECK(graph_errors_contain(errors, "must not alias itself"));
		CHECK(graph_errors_contain(errors, "replacement ID 'missing' is not a current case ID"));
		CHECK(graph_errors_contain(errors, "new_ids must not be empty"));
		CHECK(graph_errors_contain(errors, "duplicate replacement ID 'current'"));
		CHECK(graph_errors_contain(errors, "replacement ID must not be empty"));
		CHECK(graph_errors_contain(errors, "reason must not be empty"));
		CHECK(migrations.resolve("dup").is_empty());
	}

	TEST_CASE("TypeCompleteness Graph migration aliases reject schema and type mismatches") {
		struct InvalidMigrationDocument {
			const char *name;
			const char *contents;
			const char *expected;
		};
		const InvalidMigrationDocument documents[] = {
			{ "root.json", "[]", "$: expected object" },
			{ "schema_type.json", R"JSON({"schema_version":"1","migrations":[]})JSON", "$.schema_version: expected integer" },
			{ "schema_value.json", R"JSON({"schema_version":2,"migrations":[]})JSON", "$.schema_version: expected 1" },
			{ "migrations_type.json", R"JSON({"schema_version":1,"migrations":{}})JSON", "$.migrations: expected array" },
			{ "record_type.json", R"JSON({"schema_version":1,"migrations":[false]})JSON", "$.migrations[0]: expected object" },
			{ "field_types.json", R"JSON({"schema_version":1,"migrations":[{"old_id":1,"new_ids":"b","reason":false}]})JSON", "$.migrations[0].old_id: expected string" },
			{ "root_extra.json", R"JSON({"schema_version":1,"migrations":[],"extra":true})JSON", "$: unknown field 'extra'" },
			{ "record_extra.json", R"JSON({"schema_version":1,"migrations":[{"old_id":"a","new_ids":["b"],"reason":"x","extra":true}]})JSON", "$.migrations[0]: unknown field 'extra'" },
		};

		for (const InvalidMigrationDocument &document : documents) {
			CAPTURE(document.name);
			TemporaryProjectTree tree(completeness_migration_tree_name(String("type_completeness_migration_") + document.name));
			REQUIRE(tree.is_valid());
			tree.write_file(String("migrations/") + document.name, document.contents);
			FSCompletenessMigrations migrations;
			Vector<String> errors;
			CHECK_EQ(migrations.load(tree.root.path_join("migrations"), completeness_current_ids({ "b" }), errors),
					ERR_INVALID_DATA);
			CHECK(graph_errors_contain(errors, String(document.name) + ": " + document.expected));
		}
	}

	TEST_CASE("TypeCompleteness Graph migration aliases report filesystem and JSON failures") {
		TemporaryProjectTree tree(completeness_migration_tree_name("type_completeness_migration_io"));
		REQUIRE(tree.is_valid());
		FSCompletenessMigrations migrations;
		Vector<String> errors;
		CHECK_NE(migrations.load(tree.root.path_join("missing"), HashSet<String>(), errors), OK);
		CHECK(graph_errors_contain(errors, tree.root.path_join("missing")));

		tree.write_file("migrations/bad.json", "{not json");
		CHECK_EQ(migrations.load(tree.root.path_join("migrations"), HashSet<String>(), errors), ERR_PARSE_ERROR);
		CHECK(graph_errors_contain(errors, tree.root.path_join("migrations/bad.json")));
	}

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
			CHECK_FALSE(graph_errors_contain(errors, "no reachable disposition"));
			CHECK_EQ(resolution.uncovered_dimension_count, 0);
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

		SUBCASE("Every domain cell needs a disposition outside required predicates") {
			errors.clear();
			resolution = FSCompletenessResolution();
			FSCompletenessManifest invalid = manifest;
			invalid.relations.clear();
			invalid.exceptions.clear();
			invalid.anchors.resize(1);
			invalid.required_dimensions.clear();
			FSCompletenessRequiredDimension local_analysis;
			local_analysis.dimension = "analysis";
			local_analysis.when["destination"] = "plain";
			local_analysis.when["source_proof"] = "static_member";
			local_analysis.when["boundary"] = "argument_binding";
			invalid.required_dimensions.push_back(local_analysis);
			CHECK_EQ(FSCompletenessGraph::resolve(invalid, catalog, resolution, errors), ERR_INVALID_DATA);
			CHECK(graph_errors_contain(errors, "has no reachable disposition"));
			CHECK_FALSE(graph_errors_contain(errors,
					"source_proof=numeric_constant, boundary=argument_binding, surface=text} required dimension"));
		}

		SUBCASE("An empty requirement list does not waive cell reachability") {
			errors.clear();
			resolution = FSCompletenessResolution();
			FSCompletenessManifest invalid = manifest;
			invalid.required_dimensions.clear();
			invalid.anchors.clear();
			CHECK_EQ(FSCompletenessGraph::resolve(invalid, catalog, resolution, errors), ERR_INVALID_DATA);
			CHECK(graph_errors_contain(errors, "has no reachable disposition"));
		}

		SUBCASE("An empty cell counts each unique matching required dimension") {
			errors.clear();
			resolution = FSCompletenessResolution();
			FSCompletenessManifest invalid = manifest;
			invalid.domain["destination"] = Vector<String>({ "plain" });
			invalid.domain["source_proof"] = Vector<String>({ "gradual", "erased" });
			invalid.domain["boundary"] = Vector<String>({ "argument_binding" });
			invalid.domain["surface"] = Vector<String>({ "text" });
			invalid.required_dimensions.clear();
			FSCompletenessRequiredDimension analysis;
			analysis.dimension = "analysis";
			analysis.when["source_proof"] = "erased";
			invalid.required_dimensions.push_back(analysis);
			invalid.required_dimensions.push_back(analysis);
			FSCompletenessRequiredDimension runtime;
			runtime.dimension = "runtime_obligation";
			runtime.when["source_proof"] = "erased";
			invalid.required_dimensions.push_back(runtime);
			invalid.anchors.clear();
			FSCompletenessAnchor anchor;
			anchor.id = "reachable_gradual";
			anchor.coordinates["destination"] = "plain";
			anchor.coordinates["source_proof"] = "gradual";
			anchor.coordinates["boundary"] = "argument_binding";
			anchor.coordinates["surface"] = "text";
			anchor.expect["analysis"] = "accept";
			invalid.anchors.push_back(anchor);
			invalid.relations.clear();
			invalid.exceptions.clear();
			resolution.cells.resize(1);
			resolution.max_observed_chain_length = 99;
			resolution.uncovered_dimension_count = 99;
			resolution.ambiguous_dimension_count = 99;

			CHECK_EQ(FSCompletenessGraph::resolve(invalid, catalog, resolution, errors), ERR_INVALID_DATA);
			CHECK(resolution.cells.is_empty());
			CHECK_EQ(resolution.max_observed_chain_length, 0);
			CHECK_EQ(resolution.uncovered_dimension_count, 0);
			CHECK_EQ(resolution.ambiguous_dimension_count, 0);
			CHECK_EQ(graph_error_count(errors,
							 "cell {destination=plain, source_proof=erased, boundary=argument_binding, surface=text} has no reachable disposition"),
					1);
			CHECK_EQ(graph_error_count(errors, "required dimension"), 2);
			CHECK_EQ(graph_error_count(errors, "required dimension 'analysis'"), 1);
			CHECK_EQ(graph_error_count(errors, "required dimension 'runtime_obligation'"), 1);
		}
	}

	TEST_CASE("TypeCompleteness Graph deduplicates and totally orders agreeing provenance") {
		FSCompletenessCatalog catalog;
		FSCompletenessManifest manifest;
		Vector<String> errors;
		load_type_completeness_graph_inputs(catalog, manifest, errors);

		SUBCASE("Duplicate anchors do not multiply identical paths") {
			FSCompletenessManifest duplicated = manifest;
			duplicated.anchors.push_back(duplicated.anchors[0]);
			FSCompletenessResolution resolution;
			REQUIRE_MESSAGE(FSCompletenessGraph::resolve(duplicated, catalog, resolution, errors) == OK,
					String(" | ").join(errors));

			const FSCompletenessResolvedCell *anchor_cell = find_completeness_cell(
					resolution, "plain", "static_member", "argument_binding", "text");
			REQUIRE(anchor_cell != nullptr);
			const FSCompletenessResolvedDimension *anchor_analysis = anchor_cell->find_dimension("analysis");
			REQUIRE(anchor_analysis != nullptr);
			REQUIRE_EQ(anchor_analysis->agreeing_provenance.size(), 1);
			CHECK(anchor_analysis->agreeing_provenance[0].is_empty());

			const FSCompletenessResolvedCell *derived_cell = find_completeness_cell(
					resolution, "plain", "numeric_constant", "argument_binding", "text");
			REQUIRE(derived_cell != nullptr);
			const FSCompletenessResolvedDimension *carrier = derived_cell->find_dimension("stored_carrier");
			REQUIRE(carrier != nullptr);
			REQUIRE_EQ(carrier->agreeing_provenance.size(), 1);
			CHECK_EQ(carrier->agreeing_provenance[0][0].relation_id, "plain_numeric_constant_parity");
		}

		SUBCASE("Equal ID sequences use full snapshots as a deterministic tie breaker") {
			FSCompletenessManifest converging = manifest;
			converging.domain["destination"] = Vector<String>({ "plain" });
			converging.domain["source_proof"] = Vector<String>({ "gradual", "erased", "static_member" });
			converging.domain["boundary"] = Vector<String>({ "argument_binding" });
			converging.domain["surface"] = Vector<String>({ "text" });
			converging.required_dimensions.clear();
			FSCompletenessRequiredDimension analysis;
			analysis.dimension = "analysis";
			converging.required_dimensions.push_back(analysis);
			converging.anchors.clear();
			for (const String &proof : Vector<String>({ "gradual", "erased" })) {
				FSCompletenessAnchor anchor;
				anchor.id = proof;
				anchor.coordinates["destination"] = "plain";
				anchor.coordinates["source_proof"] = proof;
				anchor.coordinates["boundary"] = "argument_binding";
				anchor.coordinates["surface"] = "text";
				anchor.expect["analysis"] = "accept";
				converging.anchors.push_back(anchor);
			}
			converging.relations.clear();
			FSCompletenessRelation relation;
			relation.id = "converge_to_static";
			Dictionary unproven;
			unproven["class"] = "unproven";
			relation.from["source_proof"] = unproven;
			relation.to["source_proof"] = "static_member";
			relation.derive["analysis"] = "same";
			converging.relations.push_back(relation);
			converging.exceptions.clear();

			FSCompletenessResolution resolution;
			REQUIRE_MESSAGE(FSCompletenessGraph::resolve(converging, catalog, resolution, errors) == OK,
					String(" | ").join(errors));
			const FSCompletenessResolvedCell *target = find_completeness_cell(
					resolution, "plain", "static_member", "argument_binding", "text");
			REQUIRE(target != nullptr);
			const FSCompletenessResolvedDimension *target_analysis = target->find_dimension("analysis");
			REQUIRE(target_analysis != nullptr);
			REQUIRE_EQ(target_analysis->agreeing_provenance.size(), 2);
			REQUIRE_EQ(target_analysis->canonical_provenance.size(), 1);
			const FSCompletenessProvenanceStep &canonical = target_analysis->canonical_provenance[0];
			CHECK_EQ(canonical.relation_id, "converge_to_static");
			CHECK(canonical.exception_id.is_empty());
			CHECK_EQ(canonical.source_coordinates.get("source_proof", String()), Variant("erased"));
			CHECK_EQ(canonical.target_coordinates.get("source_proof", String()), Variant("static_member"));
			CHECK_EQ(canonical.input_dimensions.get("analysis", String()), Variant("accept"));
			CHECK_EQ(canonical.output_dimensions.get("analysis", String()), Variant("accept"));
			REQUIRE_EQ(target_analysis->agreeing_provenance[1].size(), 1);
			const FSCompletenessProvenanceStep &other = target_analysis->agreeing_provenance[1][0];
			CHECK_EQ(other.relation_id, "converge_to_static");
			CHECK(other.exception_id.is_empty());
			CHECK_EQ(other.source_coordinates.get("source_proof", String()), Variant("gradual"));
			CHECK_EQ(other.target_coordinates.get("source_proof", String()), Variant("static_member"));
			CHECK_EQ(other.input_dimensions.get("analysis", String()), Variant("accept"));
			CHECK_EQ(other.output_dimensions.get("analysis", String()), Variant("accept"));
		}

		SUBCASE("Overlapping identical requirements are counted once") {
			FSCompletenessManifest overlapping = manifest;
			overlapping.domain["destination"] = Vector<String>({ "plain" });
			overlapping.domain["source_proof"] = Vector<String>({ "numeric_constant" });
			overlapping.domain["boundary"] = Vector<String>({ "argument_binding" });
			overlapping.domain["surface"] = Vector<String>({ "text" });
			overlapping.required_dimensions.clear();
			FSCompletenessRequiredDimension carrier;
			carrier.dimension = "stored_carrier";
			overlapping.required_dimensions.push_back(carrier);
			overlapping.required_dimensions.push_back(carrier);
			overlapping.anchors.clear();
			FSCompletenessAnchor anchor;
			anchor.id = "numeric_without_carrier";
			anchor.coordinates["destination"] = "plain";
			anchor.coordinates["source_proof"] = "numeric_constant";
			anchor.coordinates["boundary"] = "argument_binding";
			anchor.coordinates["surface"] = "text";
			anchor.expect["analysis"] = "accept";
			overlapping.anchors.push_back(anchor);
			overlapping.relations.clear();
			overlapping.exceptions.clear();

			FSCompletenessResolution resolution;
			CHECK_EQ(FSCompletenessGraph::resolve(overlapping, catalog, resolution, errors), ERR_INVALID_DATA);
			CHECK_EQ(graph_error_count(errors, "required dimension 'stored_carrier' has no reachable disposition"), 1);
		}
	}
}

} // namespace FSTests
