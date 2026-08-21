/**************************************************************************/
/*  test_type_completeness_adapters.h                                     */
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

#include "modules/foundry_script/tests/fs_temporary_project_tree.h"
#include "modules/foundry_script/tests/fs_type_completeness_adapter.h"
#include "modules/foundry_script/tests/fs_type_completeness_cache.h"
#include "modules/foundry_script/tests/fs_type_completeness_runner.h"

#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/os/os.h"
#include "tests/test_macros.h"

namespace FSTests {

static const String synthetic_completeness_family = "synthetic_identity";
static const String synthetic_completeness_adapter_id = "synthetic_pair_identity";

// The test-only family's catalog inputs. The shape axis is data: adding a leaf to the partition and
// to the rule domain is the only thing that changes how large the matrix is.
static String synthetic_partition_document(bool p_with_triple) {
	return vformat(R"JSON({
  "schema_version": 1,
  "axis": "synthetic_shape",
  "leaves": ["scalar", "pair"%s],
  "classes": {}
}
)JSON",
			p_with_triple ? ", \"triple\"" : "");
}

static String synthetic_dimension_document() {
	return vformat(R"JSON({
  "schema_version": 1,
  "adapter": "%s",
  "dimensions": [{"id": "synthetic_identity", "outcomes": ["identical"]}]
}
)JSON",
			synthetic_completeness_adapter_id);
}

static String synthetic_rule_document(bool p_with_triple, const String &p_adapter = String()) {
	return vformat(R"JSON({
  "schema_version": 2,
  "family": "%s",
  "adapter": "%s",
  "domain": {
    "synthetic_shape": ["scalar", "pair"%s],
    "surface": ["text", "bytecode"]
  },
  "required_dimensions": [{"dimension": "synthetic_identity", "when": {}}],
  "anchors": [{
    "id": "scalar_anchor",
    "coordinates": {"synthetic_shape": "scalar"},
    "expect": {"synthetic_identity": "identical"},
    "surfaces": ["text", "bytecode"]
  }],
  "relations": [
    {
      "id": "scalar_to_pair",
      "from": {"synthetic_shape": "scalar"},
      "to": {"synthetic_shape": "pair"},
      "derive": {"synthetic_identity": "same"}
    }%s
  ],
  "exceptions": []
}
)JSON",
			synthetic_completeness_family,
			p_adapter.is_empty() ? synthetic_completeness_adapter_id : p_adapter,
			p_with_triple ? ", \"triple\"" : "",
			p_with_triple ? R"JSON(,
    {
      "id": "scalar_to_triple",
      "from": {"synthetic_shape": "scalar"},
      "to": {"synthetic_shape": "triple"},
      "derive": {"synthetic_identity": "same"}
    })JSON"
						  : "");
}

// Stages a copy of the tracked catalog and adds the test-only family to it. Nothing synthetic is
// tracked, so the reachability the capability map validates over the tracked rule directory is
// unaffected by anything this file does.
static String stage_synthetic_completeness_catalog(TemporaryProjectTree &p_tree,
		const String &p_relative_root, bool p_with_triple, const String &p_rule_document = String()) {
	const String staged_root = p_tree.root.path_join(p_relative_root);
	Ref<DirAccess> filesystem = DirAccess::create(DirAccess::ACCESS_FILESYSTEM);
	REQUIRE(filesystem.is_valid());
	REQUIRE_EQ(filesystem->copy_dir(tracked_catalog_root(), staged_root), OK);
	p_tree.write_file(p_relative_root.path_join("partitions/synthetic_shape.json"),
			synthetic_partition_document(p_with_triple));
	p_tree.write_file(
			p_relative_root.path_join("dimensions/synthetic.json"), synthetic_dimension_document());
	p_tree.write_file(p_relative_root.path_join("rules/synthetic_identity.json"),
			p_rule_document.is_empty() ? synthetic_rule_document(p_with_triple) : p_rule_document);
	return staged_root;
}

static Error load_synthetic_catalog_errors(const String &p_catalog_root, Vector<String> &r_errors) {
	const FSCompletenessCatalogRecord *record = nullptr;
	return FSCompletenessCatalogCache::get(TemporaryProjectTree::canonicalize_existing_path(p_catalog_root),
			synthetic_completeness_family, record, r_errors);
}

static bool completeness_error_reported(const Vector<String> &p_errors, const String &p_expected) {
	for (const String &error : p_errors) {
		if (error.contains(p_expected)) {
			return true;
		}
	}
	return false;
}

static Error run_synthetic_family(TemporaryProjectTree &p_tree, const String &p_catalog_root,
		const String &p_scratch_relative_root, const HashSet<String> &p_surfaces,
		FSCompletenessRunResult &r_result) {
	const String scratch_root = p_tree.root.path_join(p_scratch_relative_root);
	REQUIRE_EQ(DirAccess::make_dir_recursive_absolute(scratch_root), OK);
	FSCompletenessRunOptions options;
	options.catalog_root = p_catalog_root;
	options.family = synthetic_completeness_family;
	options.scratch_root = scratch_root;
	options.report_path = scratch_root.path_join("report.json");
	options.surfaces = p_surfaces;
	return FSCompletenessRunner::run(options, r_result);
}

static String synthetic_tree_name(const String &p_purpose) {
	return vformat("type_completeness_%s_%d", p_purpose, OS::get_singleton()->get_process_id());
}

TEST_SUITE("[Modules][FoundryScript][TypeCompleteness][Adapters]") {
	TEST_CASE("TypeCompleteness Adapters registry resolves every registered id exactly once") {
		const Vector<String> ids = FSCompletenessAdapterRegistry::ids();
		REQUIRE_FALSE(ids.is_empty());
		for (int index = 1; index < ids.size(); index++) {
			CHECK_LT(ids[index - 1], ids[index]);
		}
		for (const String &adapter_id : ids) {
			CAPTURE(adapter_id);
			const FSCompletenessFamilyAdapter *adapter = FSCompletenessAdapterRegistry::find(adapter_id);
			REQUIRE(adapter != nullptr);
			CHECK_EQ(adapter->id(), adapter_id);
			CHECK_EQ(FSCompletenessAdapterRegistry::find(adapter_id), adapter);
		}
		CHECK(ids.has("union_destination_membership"));
		CHECK(ids.has(synthetic_completeness_adapter_id));
		CHECK(FSCompletenessAdapterRegistry::find("no_such_adapter") == nullptr);
		CHECK(FSCompletenessAdapterRegistry::find(String()) == nullptr);
		CHECK(FSCompletenessAdapterRegistry::validate().is_empty());
	}

	TEST_CASE("TypeCompleteness Adapters registry refuses a table that registers an id twice") {
		const FSCompletenessFamilyAdapter *adapter =
				FSCompletenessAdapterRegistry::find(synthetic_completeness_adapter_id);
		REQUIRE(adapter != nullptr);
		const FSCompletenessFamilyAdapter *other =
				FSCompletenessAdapterRegistry::find("union_destination_membership");
		REQUIRE(other != nullptr);

		CHECK(FSCompletenessAdapterRegistry::validate(Vector<const FSCompletenessFamilyAdapter *>({ adapter, other }))
						.is_empty());
		CHECK_EQ(FSCompletenessAdapterRegistry::validate(
						 Vector<const FSCompletenessFamilyAdapter *>({ adapter, other, adapter })),
				synthetic_completeness_adapter_id);
		CHECK_EQ(FSCompletenessAdapterRegistry::validate(
						 Vector<const FSCompletenessFamilyAdapter *>({ adapter, nullptr })),
				"<null>");
	}

	TEST_CASE("TypeCompleteness Adapters every structural stage renders into a report") {
		HashSet<String> rendered;
		for (const char *stage : FSCompletenessStructuralStage::ALL) {
			CAPTURE(stage);
			FSCompletenessStructuralFailure failure;
			failure.stage = stage;
			failure.detail = vformat("%s detail", stage);
			failure.case_id = "case";
			failure.witness_id = "witness";
			failure.exception_id = "exception";
			failure.error_code = ERR_INVALID_DATA;
			const Dictionary report = FSCompletenessRunner::structural_failure_report(failure);
			CHECK_EQ(String(report["stage"]), String(stage));
			CHECK_EQ(String(report["detail"]), failure.detail);
			CHECK_EQ(String(report["case_id"]), "case");
			CHECK_EQ(String(report["witness_id"]), "witness");
			CHECK_EQ(String(report["exception_id"]), "exception");
			CHECK_EQ(int(double(report["error_code"])), int(ERR_INVALID_DATA));
			CHECK_FALSE(rendered.has(String(stage)));
			rendered.insert(String(stage));
		}
		CHECK_FALSE(rendered.is_empty());
	}

	TEST_CASE("TypeCompleteness Adapters builtin dimensions are the runner's own observations") {
		const HashSet<String> dimensions = FSCompletenessRunner::builtin_dimensions();
		CHECK_EQ(dimensions.size(), 5);
		for (const char *dimension : { "output", "diagnostics", "runtime_status", "text_bytecode_parity",
					 "diagnostic_severity" }) {
			CAPTURE(dimension);
			CHECK(dimensions.has(dimension));
		}
		CHECK_FALSE(dimensions.has("synthetic_identity"));
	}

	TEST_CASE("TypeCompleteness Adapters a synthetic family runs through the shared runner") {
		TemporaryProjectTree tree(synthetic_tree_name("synthetic_run"));
		REQUIRE(tree.is_valid());
		const String catalog_root = stage_synthetic_completeness_catalog(tree, "catalog", false);

		FSCompletenessRunResult result;
		REQUIRE_EQ(run_synthetic_family(tree, catalog_root, "scratch", HashSet<String>(), result), OK);
		CHECK(result.success);
		CHECK_EQ(result.outcome, "passed");
		CHECK_EQ(result.executed_cells, 4);
		CHECK(result.findings.is_empty());
		CHECK(result.structural_failures.is_empty());
		CHECK_EQ(int(double(result.report["cell_count"])), 4);
		const Dictionary executed_by_surface = result.report["executed_by_surface"];
		CHECK_EQ(int(double(executed_by_surface["text"])), 2);
		CHECK_EQ(int(double(executed_by_surface["bytecode"])), 2);
		CHECK_EQ(int(double(result.report["text_bytecode_parity_failures"])), 0);
		CHECK_EQ(String(result.report["family"]), synthetic_completeness_family);
		CHECK_EQ(Array(result.report["cases"]).size(), 4);
	}

	TEST_CASE("TypeCompleteness Adapters a wider shape partition widens the matrix without a code change") {
		TemporaryProjectTree tree(synthetic_tree_name("synthetic_widened"));
		REQUIRE(tree.is_valid());
		const String narrow_root = stage_synthetic_completeness_catalog(tree, "narrow", false);
		const String wide_root = stage_synthetic_completeness_catalog(tree, "wide", true);

		FSCompletenessRunResult narrow;
		REQUIRE_EQ(run_synthetic_family(tree, narrow_root, "narrow-scratch", HashSet<String>(), narrow), OK);
		FSCompletenessRunResult wide;
		REQUIRE_EQ(run_synthetic_family(tree, wide_root, "wide-scratch", HashSet<String>(), wide), OK);

		CHECK_EQ(int(double(narrow.report["cell_count"])), 4);
		CHECK_EQ(int(double(wide.report["cell_count"])), 6);
		const Dictionary wide_executed = wide.report["executed_by_surface"];
		CHECK_EQ(int(double(wide_executed["text"])), 3);
		CHECK_EQ(int(double(wide_executed["bytecode"])), 3);
	}

	TEST_CASE("TypeCompleteness Adapters a filtered surface run reports only the surfaces it ran") {
		TemporaryProjectTree tree(synthetic_tree_name("synthetic_filtered"));
		REQUIRE(tree.is_valid());
		const String catalog_root = stage_synthetic_completeness_catalog(tree, "catalog", false);

		FSCompletenessRunResult text_only;
		REQUIRE_EQ(run_synthetic_family(
						   tree, catalog_root, "text-scratch", HashSet<String>({ "text" }), text_only),
				OK);
		CHECK(text_only.success);
		CHECK_EQ(text_only.executed_cells, 2);
		CHECK_EQ(int(double(text_only.report["cell_count"])), 2);
		const Dictionary executed_by_surface = text_only.report["executed_by_surface"];
		CHECK_EQ(executed_by_surface.size(), 1);
		CHECK_EQ(int(double(executed_by_surface["text"])), 2);
		// No pair had both surfaces observed, so no parity verdict was reached for any of them.
		CHECK_EQ(int(double(text_only.report["text_bytecode_parity_failures"])), 0);
		CHECK_EQ(Array(text_only.report["cases"]).size(), 2);

		FSCompletenessRunResult refused;
		CHECK_EQ(run_synthetic_family(tree, catalog_root, "bad-scratch",
						  HashSet<String>({ "text", "assembly" }), refused),
				ERR_INVALID_PARAMETER);
		CHECK_FALSE(refused.success);
		CHECK_EQ(refused.outcome, "structural_failure");
	}

	TEST_CASE("TypeCompleteness Adapters an unregistered manifest adapter fails the run structurally") {
		TemporaryProjectTree tree(synthetic_tree_name("synthetic_unknown_adapter"));
		REQUIRE(tree.is_valid());
		const String catalog_root = stage_synthetic_completeness_catalog(
				tree, "catalog", false, synthetic_rule_document(false, "no_such_adapter"));

		FSCompletenessRunResult result;
		CHECK_EQ(run_synthetic_family(tree, catalog_root, "scratch", HashSet<String>(), result),
				ERR_INVALID_DATA);
		CHECK_FALSE(result.success);
		CHECK_EQ(result.outcome, "structural_failure");
		REQUIRE_EQ(result.structural_failures.size(), 1);
		CHECK_EQ(result.structural_failures[0].stage, FSCompletenessStructuralStage::ADAPTER_UNKNOWN);
		CHECK_EQ(result.structural_failures[0].detail,
				"rules/synthetic_identity.json:$.adapter names adapter 'no_such_adapter', which is not "
				"registered.");
	}

	TEST_CASE("TypeCompleteness Adapters a manifest without a usable adapter member is refused") {
		TemporaryProjectTree tree(synthetic_tree_name("synthetic_adapter_member"));
		REQUIRE(tree.is_valid());
		const String rule = synthetic_rule_document(false);

		struct AdapterMemberCase {
			const char *replacement;
			const char *expected;
		};
		const AdapterMemberCase cases[] = {
			{ "", "$.adapter: required field is missing" },
			{ "  \"adapter\": \"\",\n", "$.adapter: must be non-empty" },
			{ "  \"adapter\": 7,\n", "$.adapter: expected a string" },
		};
		int index = 0;
		for (const AdapterMemberCase &adapter_case : cases) {
			CAPTURE(adapter_case.expected);
			const String relative_root = vformat("catalog%d", index++);
			const String document = rule.replace(
					vformat("  \"adapter\": \"%s\",\n", synthetic_completeness_adapter_id),
					adapter_case.replacement);
			REQUIRE_NE(document, rule);
			const String catalog_root =
					stage_synthetic_completeness_catalog(tree, relative_root, false, document);
			Vector<String> errors;
			CHECK_EQ(load_synthetic_catalog_errors(catalog_root, errors), ERR_INVALID_DATA);
			CHECK_MESSAGE(completeness_error_reported(errors, adapter_case.expected),
					String(" | ").join(errors));
		}
	}

	TEST_CASE("TypeCompleteness Adapters a schema version other than 2 is refused") {
		TemporaryProjectTree tree(synthetic_tree_name("synthetic_schema_version"));
		REQUIRE(tree.is_valid());
		const String document =
				synthetic_rule_document(false).replace("\"schema_version\": 2", "\"schema_version\": 1");
		const String catalog_root = stage_synthetic_completeness_catalog(tree, "catalog", false, document);
		Vector<String> errors;
		CHECK_EQ(load_synthetic_catalog_errors(catalog_root, errors), ERR_INVALID_DATA);
		CHECK_MESSAGE(completeness_error_reported(errors, "$.schema_version: must equal 2"),
				String(" | ").join(errors));
	}

	TEST_CASE("TypeCompleteness Adapters a dimension file naming an unregistered adapter is refused") {
		TemporaryProjectTree tree(synthetic_tree_name("synthetic_dimension_adapter"));
		REQUIRE(tree.is_valid());
		const String catalog_root = stage_synthetic_completeness_catalog(tree, "catalog", false);
		tree.write_file("catalog/dimensions/synthetic.json",
				synthetic_dimension_document().replace(
						synthetic_completeness_adapter_id, "no_such_adapter"));
		Vector<String> errors;
		CHECK_EQ(load_synthetic_catalog_errors(catalog_root, errors), ERR_INVALID_DATA);
		CHECK_MESSAGE(
				completeness_error_reported(errors, "synthetic.json: $.adapter: unknown adapter 'no_such_adapter'"),
				String(" | ").join(errors));
	}

	TEST_CASE("TypeCompleteness Adapters a domain the adapter cannot serve is refused before the matrix runs") {
		TemporaryProjectTree tree(synthetic_tree_name("synthetic_domain"));
		REQUIRE(tree.is_valid());

		SUBCASE("a leaf the adapter cannot render") {
			const String document = synthetic_rule_document(false).replace(
					"\"synthetic_shape\": [\"scalar\", \"pair\"]", "\"synthetic_shape\": [\"scalar\", \"union\"]");
			const String catalog_root =
					stage_synthetic_completeness_catalog(tree, "leaf", false, document);
			Vector<String> errors;
			CHECK_EQ(load_synthetic_catalog_errors(catalog_root, errors), ERR_INVALID_DATA);
			CHECK_MESSAGE(completeness_error_reported(errors, "$.domain.synthetic_shape[1]"),
					String(" | ").join(errors));
		}

		SUBCASE("a dimension the adapter cannot observe") {
			const String document = synthetic_rule_document(false).replace(
					"{\"dimension\": \"synthetic_identity\", \"when\": {}}",
					"{\"dimension\": \"analysis\", \"when\": {}}");
			const String catalog_root =
					stage_synthetic_completeness_catalog(tree, "dimension", false, document);
			Vector<String> errors;
			CHECK_EQ(load_synthetic_catalog_errors(catalog_root, errors), ERR_INVALID_DATA);
			CHECK_MESSAGE(completeness_error_reported(errors,
								  "dimension 'analysis' is not observable by adapter 'synthetic_pair_identity'"),
					String(" | ").join(errors));
		}

		SUBCASE("a domain with no surface axis") {
			const String document = synthetic_rule_document(false).replace(
					",\n    \"surface\": [\"text\", \"bytecode\"]", "");
			const String catalog_root =
					stage_synthetic_completeness_catalog(tree, "surface", false, document);
			Vector<String> errors;
			CHECK_EQ(load_synthetic_catalog_errors(catalog_root, errors), ERR_INVALID_DATA);
			CHECK_MESSAGE(
					completeness_error_reported(errors, "$.domain.surface: required domain axis is missing"),
					String(" | ").join(errors));
		}
	}

	TEST_CASE("TypeCompleteness Adapters a ledger dimension is known only when it is observed") {
		TemporaryProjectTree tree(synthetic_tree_name("synthetic_ledger_dimension"));
		REQUIRE(tree.is_valid());
		const String catalog_root = stage_synthetic_completeness_catalog(tree, "catalog", false);
		const FSCompletenessCatalogRecord *record = nullptr;
		Vector<String> errors;
		REQUIRE_MESSAGE(
				FSCompletenessCatalogCache::get(TemporaryProjectTree::canonicalize_existing_path(catalog_root),
						synthetic_completeness_family, record, errors) == OK,
				String(" | ").join(errors));
		REQUIRE(record != nullptr);
		REQUIRE_FALSE(record->resolution.cells.is_empty());
		const FSCompletenessResolvedCell &cell = record->resolution.cells[0];
		CHECK(cell.dimensions.has("synthetic_identity"));
		CHECK_FALSE(cell.dimensions.has("no_such_dimension"));
		CHECK(FSCompletenessRunner::builtin_dimensions().has("runtime_status"));
		CHECK_FALSE(FSCompletenessRunner::builtin_dimensions().has("synthetic_identity"));
	}
}

} // namespace FSTests
