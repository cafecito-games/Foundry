/**************************************************************************/
/*  test_type_completeness_harness.h                                      */
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
#include "fs_type_completeness_cache.h"
#include "fs_type_completeness_cli.h"
#include "fs_type_completeness_common.h"
#include "fs_type_completeness_runner.h"

#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/io/json.h"
#include "core/math/math_funcs.h"
#include "core/os/os.h"
#include "core/os/thread.h"
#include "core/templates/safe_refcount.h"
#include "tests/test_macros.h"

namespace FSTests {

static const String completeness_family = "union_destination_membership";

static Completeness::FSCompletenessBudgets load_tracked_completeness_budgets() {
	Completeness::FSCompletenessBudgets budgets;
	Vector<String> errors;
	REQUIRE_MESSAGE(Completeness::FSCompletenessBudgets::load(
							Completeness::FSCompletenessBudgets::tracked_path(), budgets, errors) == OK,
			String(" | ").join(errors));
	return budgets;
}

// A resolution built by hand, with the same shape the graph produces: one cell per (index, surface)
// pair, so lookups have both a unique key and a coordinate key to resolve.
static FSCompletenessResolution synthetic_completeness_resolution(int p_cell_count) {
	FSCompletenessResolution resolution;
	for (int index = 0; index < p_cell_count; index++) {
		FSCompletenessResolvedCell cell;
		Dictionary coordinates;
		coordinates["destination"] = vformat("destination_%d", index);
		coordinates["surface"] = index % 2 == 0 ? "text" : "bytecode";
		cell.coordinates = coordinates;
		cell.case_id = FSCompletenessCaseID::make("synthetic", coordinates);
		resolution.cells.push_back(cell);
	}
	resolution.build_indices();
	return resolution;
}

static String completeness_budgets_document(const String &p_overrides) {
	return vformat(R"JSON({
	"schema_version": 1,
	"presubmit_hard_timeout_seconds": 180,
	"strict_shard_hard_timeout_seconds": 600,
	"scheduled_shard_target_seconds": 1800,
	"scheduled_shard_hard_timeout_seconds": 2400,
	"second_family_smoke_seconds": 60%s
}
)JSON",
			p_overrides);
}

static void completeness_inject_diagnostic(FSCompletenessObservation &r_observation) {
	r_observation.diagnostics.push_back("injected completeness harness diagnostic");
}

// A clock that only advances when the run publishes its report: the write hook below moves it past the
// deadline exactly during publication, which is the one window no stage boundary covers.
static SafeNumeric<uint64_t> completeness_publication_clock_usec;

static uint64_t completeness_publication_clock() {
	return completeness_publication_clock_usec.get();
}

static void completeness_advance_clock_on_report_write(const String &p_path) {
	// Rendered programs are staged before publication; only the report write must move the clock.
	if (p_path.get_extension() == "fs") {
		return;
	}
	completeness_publication_clock_usec.set(1000000000ULL);
}

// Always past any deadline a caller can set, so a timeout is proven by the contract rather than by
// racing a real clock.
static uint64_t completeness_elapsed_clock() {
	return UINT64_MAX;
}

struct CompletenessCacheProbe {
	String root;
	String family;
	const FSCompletenessCatalogRecord *record = nullptr;
	Error error = OK;
	int cell_count = -1;
};

static void completeness_cache_probe(void *p_probe) {
	CompletenessCacheProbe *probe = static_cast<CompletenessCacheProbe *>(p_probe);
	Vector<String> errors;
	probe->error = FSCompletenessCatalogCache::get(probe->root, probe->family, probe->record, errors);
	probe->cell_count = probe->record == nullptr ? -1 : probe->record->resolution.cells.size();
}

// Stages a copy of the tracked catalog and gives it a second rule family that resolves over the same
// partitions and dimensions, so one root can be asked for two families.
static String stage_completeness_catalog_with_second_family(
		TemporaryProjectTree &p_tree, const String &p_second_family) {
	const String staged_root = p_tree.root.path_join("catalog");
	Ref<DirAccess> filesystem = DirAccess::create(DirAccess::ACCESS_FILESYSTEM);
	REQUIRE(filesystem.is_valid());
	REQUIRE_EQ(filesystem->copy_dir(tracked_catalog_root(), staged_root), OK);
	if (p_second_family.is_empty()) {
		return staged_root;
	}
	Error read_error = OK;
	const String source = FileAccess::get_file_as_string(
			staged_root.path_join("rules").path_join(completeness_family + ".json"), &read_error);
	REQUIRE_EQ(read_error, OK);
	const String mirrored = source.replace(
			vformat("\"family\": \"%s\"", completeness_family), vformat("\"family\": \"%s\"", p_second_family));
	REQUIRE_NE(mirrored, source);
	p_tree.write_file(String("catalog/rules/").path_join(p_second_family + ".json"), mirrored);
	return staged_root;
}

TEST_SUITE("[Modules][FoundryScript][TypeCompleteness][Harness]") {
	TEST_CASE("TypeCompleteness Harness sorted dictionary keys are complete and ascending") {
		Dictionary coordinates;
		coordinates["surface"] = "text";
		coordinates["boundary"] = "argument_binding";
		coordinates["destination"] = "plain";
		const Vector<String> keys = Completeness::sorted_dictionary_keys(coordinates);
		REQUIRE_EQ(keys.size(), 3);
		CHECK_EQ(keys[0], "boundary");
		CHECK_EQ(keys[1], "destination");
		CHECK_EQ(keys[2], "surface");
		CHECK(Completeness::sorted_dictionary_keys(Dictionary()).is_empty());
	}

	TEST_CASE("TypeCompleteness Harness JSON integers accept both engine number forms") {
		int value = -1;
		CHECK(Completeness::parse_json_integer(Variant(1), value));
		CHECK_EQ(value, 1);
		CHECK(Completeness::parse_json_integer(Variant(1.0), value));
		CHECK_EQ(value, 1);
		CHECK(Completeness::parse_json_integer(Variant(-7.0), value));
		CHECK_EQ(value, -7);

		const Variant rejected[] = { Variant(1.5), Variant("1"), Variant(true), Variant(),
			Variant(Math::INF), Variant(Math::NaN), Variant(double(INT32_MAX) + 1.0),
			Variant(double(INT32_MIN) - 1.0) };
		for (const Variant &candidate : rejected) {
			CAPTURE(candidate.stringify());
			int rejected_value = 99;
			CHECK_FALSE(Completeness::parse_json_integer(candidate, rejected_value));
			CHECK_EQ(rejected_value, 99);
		}
	}

	TEST_CASE("TypeCompleteness Harness canonical identity is order independent and cycle safe") {
		Dictionary first;
		first["b"] = "two";
		first["a"] = "one";
		Dictionary second;
		second["a"] = "one";
		second["b"] = "two";
		CHECK_EQ(Completeness::canonical_variant_identity(first), Completeness::canonical_variant_identity(second));

		Dictionary different;
		different["a"] = "one";
		different["b"] = "three";
		CHECK_NE(Completeness::canonical_variant_identity(first), Completeness::canonical_variant_identity(different));

		// A string and a number that stringify the same must not share an identity.
		CHECK_NE(Completeness::canonical_variant_identity(Variant("1")),
				Completeness::canonical_variant_identity(Variant(1)));

		Dictionary cyclic;
		cyclic["self"] = cyclic;
		CHECK(Completeness::canonical_variant_identity(cyclic).contains("X:CYCLE"));
		cyclic.clear();
	}

	TEST_CASE("TypeCompleteness Harness semantic pair key erases only the surface axis") {
		Dictionary text_coordinates;
		text_coordinates["destination"] = "plain";
		text_coordinates["surface"] = "text";
		Dictionary bytecode_coordinates;
		bytecode_coordinates["destination"] = "plain";
		bytecode_coordinates["surface"] = "bytecode";
		CHECK_EQ(Completeness::semantic_pair_key(text_coordinates, "surface"),
				Completeness::semantic_pair_key(bytecode_coordinates, "surface"));
		CHECK_NE(Completeness::semantic_pair_key(text_coordinates, "destination"),
				Completeness::semantic_pair_key(bytecode_coordinates, "destination"));
		// The caller's Dictionary is never modified.
		CHECK_EQ(String(text_coordinates.get("surface", String())), "text");
	}

	TEST_CASE("TypeCompleteness Harness JSON directory enumeration applies one policy") {
		TemporaryProjectTree tree(
				vformat("type_completeness_enumerate_%d", OS::get_singleton()->get_process_id()));
		REQUIRE(tree.is_valid());
		tree.write_file("inputs/b.json", "{}");
		tree.write_file("inputs/a.json", "{}");
		tree.write_file("inputs/.hidden.json", "{}");
		tree.write_file("inputs/notes.txt", "unexpected");
		tree.write_file("inputs/upper.JSON", "{}");
		tree.write_file("inputs/nested/c.json", "{}");

		Vector<String> files;
		Vector<Completeness::JsonDirectoryError> errors;
		const String directory = tree.root.path_join("inputs");
		CHECK_EQ(Completeness::enumerate_json_directory(
						 directory, Completeness::JsonDirectoryPolicy(), files, errors),
				ERR_INVALID_DATA);
		REQUIRE_EQ(files.size(), 2);
		CHECK_EQ(files[0], directory.path_join("a.json"));
		CHECK_EQ(files[1], directory.path_join("b.json"));

		bool refused_subdirectory = false;
		bool refused_text = false;
		bool refused_uppercase = false;
		for (const Completeness::JsonDirectoryError &error : errors) {
			refused_subdirectory = refused_subdirectory ||
					(error.kind == Completeness::JsonDirectoryErrorKind::SUBDIRECTORY_ENTRY &&
							error.entry == "nested");
			refused_text = refused_text || error.entry == "notes.txt";
			refused_uppercase = refused_uppercase || error.entry == "upper.JSON";
			CHECK_FALSE(Completeness::is_directory_level_error(error.kind));
		}
		CHECK(refused_subdirectory);
		CHECK(refused_text);
		CHECK(refused_uppercase);

		Vector<String> empty_files;
		Vector<Completeness::JsonDirectoryError> empty_errors;
		tree.write_file("empty/.keep", "");
		CHECK_EQ(Completeness::enumerate_json_directory(tree.root.path_join("empty"),
						 Completeness::JsonDirectoryPolicy(), empty_files, empty_errors),
				ERR_INVALID_DATA);
		REQUIRE_EQ(empty_errors.size(), 1);
		CHECK_EQ(empty_errors[0].kind, Completeness::JsonDirectoryErrorKind::EMPTY_DIRECTORY);
		CHECK_FALSE(Completeness::is_directory_level_error(empty_errors[0].kind));

		Vector<String> missing_files;
		Vector<Completeness::JsonDirectoryError> missing_errors;
		CHECK_EQ(Completeness::enumerate_json_directory(tree.root.path_join("absent"),
						 Completeness::JsonDirectoryPolicy(), missing_files, missing_errors),
				ERR_INVALID_DATA);
		REQUIRE_EQ(missing_errors.size(), 1);
		CHECK_EQ(missing_errors[0].kind, Completeness::JsonDirectoryErrorKind::DIRECTORY_UNOPENABLE);
		CHECK(Completeness::is_directory_level_error(missing_errors[0].kind));
	}

	TEST_CASE("TypeCompleteness Harness tracked budgets carry the locked values") {
		const Completeness::FSCompletenessBudgets budgets = load_tracked_completeness_budgets();
		CHECK_EQ(budgets.presubmit_hard_timeout_seconds, 180);
		CHECK_EQ(budgets.strict_shard_hard_timeout_seconds, 600);
		CHECK_EQ(budgets.scheduled_shard_target_seconds, 1800);
		CHECK_EQ(budgets.scheduled_shard_hard_timeout_seconds, 2400);
		CHECK_EQ(budgets.second_family_smoke_seconds, 60);
		CHECK_EQ(budgets.hard_timeout_seconds_for_tier("presubmit"), 180);
		CHECK_EQ(budgets.hard_timeout_seconds_for_tier("strict"), 600);
		CHECK_EQ(budgets.hard_timeout_seconds_for_tier("scheduled"), 2400);
		CHECK_EQ(budgets.hard_timeout_seconds_for_tier("nightly"), -1);
	}

	TEST_CASE("TypeCompleteness Harness budgets never fall back to defaults") {
		TemporaryProjectTree tree(vformat("type_completeness_budgets_%d", OS::get_singleton()->get_process_id()));
		REQUIRE(tree.is_valid());

		struct MalformedCase {
			const char *name;
			const char *document;
			const char *expected_path;
		};
		const MalformedCase cases[] = {
			{ "missing_member",
					"{\n\t\"schema_version\": 1,\n\t\"presubmit_hard_timeout_seconds\": 180,\n"
					"\t\"strict_shard_hard_timeout_seconds\": 600,\n\t\"scheduled_shard_target_seconds\": 1800,\n"
					"\t\"scheduled_shard_hard_timeout_seconds\": 2400\n}\n",
					"$.second_family_smoke_seconds" },
			{ "unknown_member", nullptr, "$.nightly_hard_timeout_seconds" },
			{ "fractional", nullptr, "$.presubmit_hard_timeout_seconds" },
			{ "string_value", nullptr, "$.presubmit_hard_timeout_seconds" },
			{ "boolean_value", nullptr, "$.presubmit_hard_timeout_seconds" },
			{ "non_positive", nullptr, "$.presubmit_hard_timeout_seconds" },
			{ "wrong_schema_version", nullptr, "$.schema_version" },
			{ "not_an_object", "[]\n", "$" },
			{ "malformed_json", "{\n", "$" },
		};
		for (const MalformedCase &malformed : cases) {
			CAPTURE(malformed.name);
			String document;
			if (malformed.document != nullptr) {
				document = malformed.document;
			} else if (String(malformed.name) == "unknown_member") {
				document = completeness_budgets_document(",\n\t\"nightly_hard_timeout_seconds\": 60");
			} else if (String(malformed.name) == "fractional") {
				document = completeness_budgets_document(String())
								   .replace("\"presubmit_hard_timeout_seconds\": 180",
										   "\"presubmit_hard_timeout_seconds\": 180.5");
			} else if (String(malformed.name) == "string_value") {
				document = completeness_budgets_document(String())
								   .replace("\"presubmit_hard_timeout_seconds\": 180",
										   "\"presubmit_hard_timeout_seconds\": \"180\"");
			} else if (String(malformed.name) == "boolean_value") {
				document = completeness_budgets_document(String())
								   .replace("\"presubmit_hard_timeout_seconds\": 180",
										   "\"presubmit_hard_timeout_seconds\": true");
			} else if (String(malformed.name) == "non_positive") {
				document = completeness_budgets_document(String())
								   .replace("\"presubmit_hard_timeout_seconds\": 180",
										   "\"presubmit_hard_timeout_seconds\": 0");
			} else {
				document = completeness_budgets_document(String())
								   .replace("\"schema_version\": 1", "\"schema_version\": 2");
			}
			const String relative_path = vformat("budgets/%s.json", malformed.name);
			tree.write_file(relative_path, document);
			Completeness::FSCompletenessBudgets budgets;
			Vector<String> errors;
			CHECK_NE(Completeness::FSCompletenessBudgets::load(
							 tree.root.path_join(relative_path), budgets, errors),
					OK);
			CHECK_FALSE(errors.is_empty());
			CHECK_EQ(budgets.presubmit_hard_timeout_seconds, 0);
			bool named_the_path = false;
			for (const String &error : errors) {
				named_the_path = named_the_path || error.contains(malformed.expected_path);
			}
			CHECK_MESSAGE(named_the_path, String(" | ").join(errors));
		}

		Completeness::FSCompletenessBudgets absent;
		Vector<String> absent_errors;
		CHECK_NE(Completeness::FSCompletenessBudgets::load(
						 tree.root.path_join("budgets/absent.json"), absent, absent_errors),
				OK);
		CHECK_FALSE(absent_errors.is_empty());
		CHECK_EQ(absent.second_family_smoke_seconds, 0);
	}

	TEST_CASE("TypeCompleteness Harness catalog cache loads one key once") {
		TemporaryProjectTree tree(vformat("type_completeness_cache_%d", OS::get_singleton()->get_process_id()));
		REQUIRE(tree.is_valid());
		const String staged_root = stage_completeness_catalog_with_second_family(tree, "mirror_family");
		const String canonical_root = TemporaryProjectTree::canonicalize_existing_path(staged_root);
		REQUIRE_FALSE(canonical_root.is_empty());

		const int loads_before = FSCompletenessCatalogCache::load_count();
		const FSCompletenessCatalogRecord *first = nullptr;
		Vector<String> errors;
		REQUIRE_MESSAGE(
				FSCompletenessCatalogCache::get(canonical_root, completeness_family, first, errors) == OK,
				String(" | ").join(errors));
		REQUIRE_NE(first, nullptr);
		CHECK_EQ(FSCompletenessCatalogCache::load_count(), loads_before + 1);

		const FSCompletenessCatalogRecord *again = nullptr;
		REQUIRE_EQ(FSCompletenessCatalogCache::get(canonical_root, completeness_family, again, errors), OK);
		CHECK_EQ(again, first);
		CHECK_EQ(FSCompletenessCatalogCache::load_count(), loads_before + 1);

		// A second family under the same root is a distinct key, never an alias of the first.
		const FSCompletenessCatalogRecord *mirror = nullptr;
		REQUIRE_MESSAGE(FSCompletenessCatalogCache::get(canonical_root, "mirror_family", mirror, errors) == OK,
				String(" | ").join(errors));
		REQUIRE_NE(mirror, nullptr);
		CHECK_NE(mirror, first);
		CHECK_EQ(mirror->manifest.family, "mirror_family");
		CHECK_EQ(first->manifest.family, completeness_family);
		CHECK_EQ(mirror->resolution.cells.size(), first->resolution.cells.size());
		CHECK_EQ(FSCompletenessCatalogCache::load_count(), loads_before + 2);

		// The tracked catalog is a different root, so a staged copy can never be read back for it.
		const FSCompletenessCatalogRecord *tracked = nullptr;
		const String canonical_tracked = TemporaryProjectTree::canonicalize_existing_path(tracked_catalog_root());
		REQUIRE_FALSE(canonical_tracked.is_empty());
		REQUIRE_EQ(FSCompletenessCatalogCache::get(canonical_tracked, completeness_family, tracked, errors), OK);
		CHECK_NE(tracked, first);
	}

	TEST_CASE("TypeCompleteness Harness cached catalogs do not observe later file changes") {
		TemporaryProjectTree tree(
				vformat("type_completeness_cache_mutation_%d", OS::get_singleton()->get_process_id()));
		REQUIRE(tree.is_valid());
		const String staged_root = stage_completeness_catalog_with_second_family(tree, String());
		const String canonical_root = TemporaryProjectTree::canonicalize_existing_path(staged_root);
		REQUIRE_FALSE(canonical_root.is_empty());

		const FSCompletenessCatalogRecord *record = nullptr;
		Vector<String> errors;
		REQUIRE_MESSAGE(
				FSCompletenessCatalogCache::get(canonical_root, completeness_family, record, errors) == OK,
				String(" | ").join(errors));
		REQUIRE_NE(record, nullptr);
		const int cell_count = record->resolution.cells.size();
		const int loads_after_first = FSCompletenessCatalogCache::load_count();

		// Deliberately corrupting the rule manifest after the load proves the documented contract: the
		// store is never invalidated, so a test that means to change a catalog must stage a fresh root.
		tree.write_file("catalog/rules/" + completeness_family + ".json", "{ not json");
		const FSCompletenessCatalogRecord *cached = nullptr;
		REQUIRE_EQ(FSCompletenessCatalogCache::get(canonical_root, completeness_family, cached, errors), OK);
		CHECK_EQ(cached, record);
		CHECK_EQ(cached->resolution.cells.size(), cell_count);
		CHECK_EQ(FSCompletenessCatalogCache::load_count(), loads_after_first);
	}

	TEST_CASE("TypeCompleteness Harness concurrent readers share one catalog record") {
		const String canonical_root = TemporaryProjectTree::canonicalize_existing_path(tracked_catalog_root());
		REQUIRE_FALSE(canonical_root.is_empty());

		CompletenessCacheProbe probes[4];
		Thread threads[4];
		for (int index = 0; index < 4; index++) {
			probes[index].root = canonical_root;
			probes[index].family = completeness_family;
			threads[index].start(completeness_cache_probe, &probes[index]);
		}
		for (int index = 0; index < 4; index++) {
			threads[index].wait_to_finish();
		}
		for (int index = 0; index < 4; index++) {
			CAPTURE(index);
			CHECK_EQ(probes[index].error, OK);
			REQUIRE_NE(probes[index].record, nullptr);
			CHECK_EQ(probes[index].record, probes[0].record);
			CHECK_EQ(probes[index].cell_count, probes[0].cell_count);
			CHECK(probes[index].cell_count > 0);
		}
	}

	TEST_CASE("TypeCompleteness Harness the shared baseline runs once and stays uncontaminated") {
		const FSCompletenessRunResult &first = FSCompletenessBaseline::shared(completeness_family);
		if (first.outcome == "structural_failure" && !first.structural_failures.is_empty() &&
				first.structural_failures[0].stage == "baseline_unavailable") {
			Completeness::fs_completeness_skip(
					"the shared baseline scratch root is unavailable in this environment");
			return;
		}
		const int runs_after_first = FSCompletenessBaseline::run_count();
		CHECK(first.success);
		CHECK_EQ(first.outcome, "passed");
		CHECK(first.executed_cells > 0);
		const Dictionary baseline_report = first.report.duplicate(true);

		TemporaryProjectTree tree(
				vformat("type_completeness_contamination_%d", OS::get_singleton()->get_process_id()));
		REQUIRE(tree.is_valid());
		FSCompletenessRunOptions options;
		options.catalog_root = tracked_catalog_root();
		options.family = completeness_family;
		options.scratch_root = tree.root;
		options.report_path = tree.root.path_join("report.json");
		options.observation_mutator = completeness_inject_diagnostic;
		FSCompletenessRunResult mutated;
		CHECK_EQ(FSCompletenessRunner::run(options, mutated), FAILED);
		CHECK_FALSE(mutated.findings.is_empty());

		const FSCompletenessRunResult &again = FSCompletenessBaseline::shared(completeness_family);
		CHECK_EQ(&again, &first);
		CHECK_EQ(FSCompletenessBaseline::run_count(), runs_after_first);
		CHECK(again.success);
		CHECK_EQ(again.report, baseline_report);
	}

	TEST_CASE("TypeCompleteness Harness resolution lookups are indexed") {
		const Completeness::FSCompletenessBudgets budgets = load_tracked_completeness_budgets();
		const int cell_count = 1000;
		const uint64_t started_at = OS::get_singleton()->get_ticks_usec();
		const FSCompletenessResolution resolution = synthetic_completeness_resolution(cell_count);
		REQUIRE_EQ(resolution.cells.size(), cell_count);
		for (const FSCompletenessResolvedCell &cell : resolution.cells) {
			const FSCompletenessResolvedCell *by_id = resolution.find_cell_by_id(cell.case_id);
			REQUIRE_NE(by_id, nullptr);
			CHECK_EQ(by_id->case_id, cell.case_id);
			int matches = 0;
			const FSCompletenessResolvedCell *by_coordinates =
					resolution.find_cell_by_coordinates(cell.coordinates, matches);
			REQUIRE_NE(by_coordinates, nullptr);
			CHECK_EQ(matches, 1);
			CHECK_EQ(by_coordinates->case_id, cell.case_id);
		}
		const double elapsed_seconds = double(OS::get_singleton()->get_ticks_usec() - started_at) / 1000000.0;
		MESSAGE(vformat("[type-completeness] %d synthetic cells resolved in %.3f s (budget %d s)",
				cell_count, elapsed_seconds, budgets.second_family_smoke_seconds));
		CHECK(elapsed_seconds < double(budgets.second_family_smoke_seconds));

		int missing_matches = -1;
		CHECK_EQ(resolution.find_cell_by_coordinates(Dictionary(), missing_matches), nullptr);
		CHECK_EQ(missing_matches, 0);
		CHECK_EQ(resolution.find_cell_by_id("absent"), nullptr);
	}

	TEST_CASE("TypeCompleteness Harness a budget crossed while publishing is still a timeout") {
		TemporaryProjectTree tree(
				vformat("type_completeness_publish_timeout_%d", OS::get_singleton()->get_process_id()));
		REQUIRE(tree.is_valid());
		completeness_publication_clock_usec.set(0);

		FSCompletenessRunOptions options;
		options.catalog_root = tracked_catalog_root();
		options.family = completeness_family;
		options.scratch_root = tree.root;
		options.report_path = tree.root.path_join("report.json");
		options.deadline_usec = 1000;
		options.clock = completeness_publication_clock;
		options.persisted_write_hook = completeness_advance_clock_on_report_write;
		FSCompletenessRunResult result;
		// No stage boundary falls between assembling the report and writing it, so a run that crosses
		// its budget there would otherwise be published as a clean, in-budget run.
		CHECK_EQ(FSCompletenessRunner::run(options, result), ERR_TIMEOUT);
		CHECK_FALSE(result.success);
		CHECK_EQ(result.outcome, "structural_failure");
		bool named_the_timeout = false;
		for (const FSCompletenessStructuralFailure &failure : result.structural_failures) {
			named_the_timeout = named_the_timeout || failure.stage == "run_timeout";
		}
		CHECK(named_the_timeout);

		REQUIRE(FileAccess::exists(options.report_path));
		Error read_error = OK;
		const String source = FileAccess::get_file_as_string(options.report_path, &read_error);
		REQUIRE_EQ(read_error, OK);
		JSON json;
		REQUIRE_EQ(json.parse(source), OK);
		const Dictionary published = json.get_data();
		CHECK_EQ(String(published.get("outcome", String())), "structural_failure");
		CHECK_EQ(bool(published.get("success", true)), false);
		bool published_the_timeout = false;
		const Array published_failures = published.get("structural_failures", Array());
		for (int index = 0; index < published_failures.size(); index++) {
			published_the_timeout = published_the_timeout ||
					String(Dictionary(published_failures[index]).get("stage", String())) == "run_timeout";
		}
		CHECK(published_the_timeout);
		// The evidence the run did gather stays in the document; only the verdict changes.
		CHECK_FALSE(Array(published.get("cases", Array())).is_empty());

		// The document a consumer has to be able to refuse. Its member set is the one every report
		// carries, and each structural-failure record carries exactly these members: the Python
		// comparator's timeout fixture is written to this shape, so a change here has to change it too.
		CHECK_EQ(Completeness::sorted_dictionary_keys(published),
				Vector<String>({ "cases", "cell_count", "census", "coverage_by_chain_length",
						"coverage_by_dimension", "exceptions", "executed_by_surface", "family", "findings",
						"ledger", "outcome", "published_surface", "schema_version", "structural_failures",
						"success", "text_bytecode_parity_failures", "timings_ms",
						"uncovered_required_dimensions" }));
		for (int index = 0; index < published_failures.size(); index++) {
			CAPTURE(index);
			const Dictionary record = published_failures[index];
			CHECK_EQ(Completeness::sorted_dictionary_keys(record),
					Vector<String>({ "case_id", "detail", "error_code", "exception_id", "stage",
							"witness_id" }));
			CHECK_EQ(Variant(record["error_code"]).get_type(), Variant::FLOAT);
			CHECK_EQ(double(record["error_code"]), double(ERR_TIMEOUT));
		}
		completeness_publication_clock_usec.set(0);
	}

	TEST_CASE("TypeCompleteness Harness a timeout that cannot publish carries no report") {
		TemporaryProjectTree tree(
				vformat("type_completeness_timeout_unwritable_%d", OS::get_singleton()->get_process_id()));
		REQUIRE(tree.is_valid());
		tree.write_file("report.json", "caller report sentinel\n");
		for (int attempt = 0; attempt < 128; attempt++) {
			tree.write_file(vformat("report.json.tmp.%d.%d", OS::get_singleton()->get_process_id(), attempt),
					"occupied temp\n");
		}

		FSCompletenessRunOptions options;
		options.catalog_root = tracked_catalog_root();
		options.family = completeness_family;
		options.scratch_root = tree.root;
		options.report_path = tree.root.path_join("report.json");
		options.deadline_usec = 1;
		options.clock = completeness_elapsed_clock;
		FSCompletenessRunResult result;
		CHECK_EQ(FSCompletenessRunner::run(options, result), ERR_TIMEOUT);
		CHECK_EQ(result.outcome, "structural_failure");
		CHECK(result.report.is_empty());
		bool named_the_unwritable_report = false;
		for (const FSCompletenessStructuralFailure &failure : result.structural_failures) {
			named_the_unwritable_report =
					named_the_unwritable_report || failure.stage == "run_timeout_report_unwritable";
		}
		CHECK(named_the_unwritable_report);
		CHECK_EQ(FileAccess::get_file_as_string(options.report_path), "caller report sentinel\n");
	}

	TEST_CASE("TypeCompleteness Harness the baseline refuses a scratch root it does not own") {
		String root;
		REQUIRE_EQ(FSCompletenessBaseline::resolve_scratch_root("refusal_probe_family", root), OK);
		REQUIRE_FALSE(root.is_empty());
		{
			TemporaryProjectTree occupied("type_completeness_baseline_refusal_probe_family_" +
					String::num_int64(OS::get_singleton()->get_process_id()));
			REQUIRE(occupied.is_valid());
			CHECK_EQ(occupied.root, root);
			// An existing root belongs to another run; the baseline never adopts it.
			String reused;
			CHECK_EQ(FSCompletenessBaseline::resolve_scratch_root("refusal_probe_family", reused),
					ERR_ALREADY_IN_USE);
		}
		String released;
		CHECK_EQ(FSCompletenessBaseline::resolve_scratch_root("refusal_probe_family", released), OK);
		CHECK_EQ(released, root);
	}

	TEST_CASE("TypeCompleteness Harness reported parity failures equal the disagreeing pairs") {
		const FSCompletenessRunResult *shared_baseline =
				FSCompletenessBaseline::shared_or_skip(completeness_family);
		if (shared_baseline == nullptr) {
			return;
		}
		const FSCompletenessRunResult &baseline = *shared_baseline;
		const String canonical_root = TemporaryProjectTree::canonicalize_existing_path(tracked_catalog_root());
		REQUIRE_FALSE(canonical_root.is_empty());
		const FSCompletenessCatalogRecord *record = nullptr;
		Vector<String> errors;
		REQUIRE_EQ(FSCompletenessCatalogCache::get(canonical_root, completeness_family, record, errors), OK);
		REQUIRE_NE(record, nullptr);

		Vector<FSCompletenessProgram> programs;
		for (const FSCompletenessResolvedCell &cell : record->resolution.cells) {
			FSCompletenessProgram program;
			REQUIRE_EQ(FSDestinationWrapperAdapter::shared().render(cell, program), OK);
			programs.push_back(program);
		}
		TemporaryProjectTree tree(vformat("type_completeness_parity_%d", OS::get_singleton()->get_process_id()));
		REQUIRE(tree.is_valid());
		FSCompletenessRuntimeBatch batch;
		REQUIRE_EQ(FSDestinationWrapperAdapter::shared().execute(tree.root, programs, batch), OK);

		HashMap<String, int> disagreeing_pairs;
		for (const FSCompletenessResolvedCell &cell : record->resolution.cells) {
			if (String(cell.coordinates.get("surface", String())) != "text") {
				continue;
			}
			const String pair_key = Completeness::semantic_pair_key(cell.coordinates, "surface");
			const FSCompletenessRuntimeResult *text = batch.text.getptr(cell.case_id);
			REQUIRE_NE(text, nullptr);
			const FSCompletenessRuntimeResult *bytecode = nullptr;
			for (const FSCompletenessResolvedCell &partner : record->resolution.cells) {
				if (String(partner.coordinates.get("surface", String())) == "bytecode" &&
						Completeness::semantic_pair_key(partner.coordinates, "surface") == pair_key) {
					bytecode = batch.bytecode.getptr(partner.case_id);
					break;
				}
			}
			REQUIRE_NE(bytecode, nullptr);
			if (compare_surface_evidence(*text, *bytecode).any()) {
				disagreeing_pairs.insert(pair_key, 1);
			}
		}
		CHECK_EQ(int(baseline.report.get("text_bytecode_parity_failures", -1)), disagreeing_pairs.size());
	}
}

} // namespace FSTests
