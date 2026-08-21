/**************************************************************************/
/*  test_type_completeness_gate_safety.h                                  */
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

#ifdef DEBUG_ENABLED
#include "modules/foundry_script/tests/fs_test_warning_settings.h"
#endif
#include "modules/foundry_script/tests/test_type_completeness_union_pilot.h"

namespace FSTests {

static String gate_safety_severity_case_id;

static void gate_safety_inject_error_record(FSCompletenessObservation &r_observation) {
	if (r_observation.case_id != gate_safety_severity_case_id) {
		return;
	}
	Dictionary record;
	record["severity"] = "error";
	record["category"] = "analysis";
	record["code"] = "injected_severity_mutation";
	record["line"] = 1.0;
	record["column"] = 1.0;
	record["message"] = "injected severity mutation";
	record["suppressed"] = false;
	r_observation.diagnostic_records.push_back(record);
}

static void gate_safety_inject_warning_record(FSCompletenessObservation &r_observation) {
	if (r_observation.case_id != gate_safety_severity_case_id) {
		return;
	}
	Dictionary record;
	record["severity"] = "warning";
	record["category"] = "analysis";
	record["code"] = "INJECTED_WARNING";
	record["line"] = 1.0;
	record["column"] = 1.0;
	record["message"] = "injected downgraded diagnostic";
	record["suppressed"] = true;
	r_observation.diagnostic_records.push_back(record);
}

static String gate_safety_partner_case_id;

static void gate_safety_corrupt_partner_obligation(FSCompletenessObservation &r_observation) {
	if (r_observation.case_id == gate_safety_partner_case_id) {
		r_observation.dimensions["runtime_obligation"] = "typed_destination_check";
	}
}

static String gate_safety_finding_id(const String &p_case_id, const String &p_dimension) {
	return "fstcf-v1-" + (p_case_id + "|" + p_dimension).sha256_text().substr(0, 20);
}

static String gate_safety_finding_record(const String &p_finding_id, const String &p_case_id,
		const String &p_dimension, const String &p_classification) {
	return vformat(R"JSON({
	"schema_version": 1,
	"finding_id": "%s",
	"case_id": "%s",
	"family": "union_destination_membership",
	"dimension": "%s",
	"classification": "%s",
	"issue_url": "https://example.invalid/issues/7",
	"closure_packet_url": "https://example.invalid/closure/7",
	"permanent_test_paths": ["modules/foundry_script/tests/test_type_completeness_gate_safety.h"]
}
)JSON",
			p_finding_id, p_case_id, p_dimension, p_classification);
}

static String gate_safety_witness_case_id(
		const FSCompletenessResolution &p_resolution, const String &p_witness_id) {
	Dictionary coordinates;
	if (FSUnionCompletenessAdapter::witness_coordinates(p_witness_id, coordinates) != OK) {
		return String();
	}
	for (const FSCompletenessResolvedCell &cell : p_resolution.cells) {
		if (cell.coordinates == coordinates) {
			return cell.case_id;
		}
	}
	return String();
}

static String gate_safety_pilot_case_id(const FSCompletenessResolution &p_resolution,
		const String &p_surface, const String &p_destination, const String &p_source_proof,
		const String &p_boundary) {
	for (const FSCompletenessResolvedCell &cell : p_resolution.cells) {
		if (cell.coordinates.get("surface", String()) == p_surface &&
				cell.coordinates.get("destination", String()) == p_destination &&
				cell.coordinates.get("source_proof", String()) == p_source_proof &&
				cell.coordinates.get("boundary", String()) == p_boundary) {
			return cell.case_id;
		}
	}
	return String();
}

static void gate_safety_rewrite_staged_rules(
		const String &p_catalog_root, const Vector<String> &p_replacements) {
	const String rules_path = p_catalog_root.path_join("rules/union_destination_membership.json");
	Error read_error = OK;
	String source = FileAccess::get_file_as_string(rules_path, &read_error);
	REQUIRE_EQ(read_error, OK);
	REQUIRE_EQ(p_replacements.size() % 2, 0);
	for (int index = 0; index + 1 < p_replacements.size(); index += 2) {
		REQUIRE_MESSAGE(source.contains(p_replacements[index]), p_replacements[index]);
		source = source.replace(p_replacements[index], p_replacements[index + 1]);
	}
	Ref<FileAccess> file = FileAccess::open(rules_path, FileAccess::WRITE);
	REQUIRE(file.is_valid());
	REQUIRE(file->store_string(source));
}

static const FSCompletenessFinding *gate_safety_find_finding(
		const FSCompletenessRunResult &p_result, const String &p_case_id, const String &p_dimension) {
	for (const FSCompletenessFinding &finding : p_result.findings) {
		if (finding.case_id == p_case_id && finding.dimension == p_dimension) {
			return &finding;
		}
	}
	return nullptr;
}

static bool gate_safety_has_structural_stage(
		const FSCompletenessRunResult &p_result, const String &p_stage) {
	for (const FSCompletenessStructuralFailure &failure : p_result.structural_failures) {
		if (failure.stage == p_stage) {
			return true;
		}
	}
	return false;
}

static Dictionary gate_safety_case_report(const Dictionary &p_report, const String &p_case_id) {
	const Array cases = p_report.get("cases", Array());
	for (int index = 0; index < cases.size(); index++) {
		const Dictionary case_report = cases[index];
		if (String(case_report.get("case_id", String())) == p_case_id) {
			return case_report;
		}
	}
	return Dictionary();
}

TEST_SUITE("[Modules][FoundryScript][TypeCompleteness][GateSafety]") {
	TEST_CASE("TypeCompleteness GateSafety separates product mismatches from structural failures") {
		const FSCompletenessResolution resolution = load_union_pilot_completeness_resolution();
		union_pilot_mutated_case_id = gate_safety_pilot_case_id(
				resolution, "text", "union", "numeric_constant", "argument_binding");
		REQUIRE_FALSE(union_pilot_mutated_case_id.is_empty());
		union_pilot_mutation_count = 0;

		TemporaryProjectTree tree(
				vformat("type_completeness_gate_channels_%d", OS::get_singleton()->get_process_id()));
		REQUIRE(tree.is_valid());
		FSCompletenessRunOptions options;
		options.catalog_root = type_completeness_union_pilot_root;
		options.family = "union_destination_membership";
		options.scratch_root = tree.root;
		options.report_path = tree.root.path_join("report.json");
		options.observation_mutator = corrupt_union_pilot_stored_carrier;
		FSCompletenessRunResult result;
		CHECK_EQ(FSCompletenessRunner::run(options, result), FAILED);
		CHECK_EQ(result.outcome, "product_mismatch");
		CHECK(result.structural_failures.is_empty());
		CHECK_FALSE(result.findings.is_empty());
		CHECK_EQ(String(result.report.get("outcome", String())), "product_mismatch");
		CHECK(Array(result.report.get("structural_failures", Array())).is_empty());

		FSCompletenessRunOptions aborted = options;
		aborted.catalog_root = tree.root.path_join("missing_catalog");
		aborted.observation_mutator = nullptr;
		FSCompletenessRunResult aborted_result;
		const Error aborted_error = FSCompletenessRunner::run(aborted, aborted_result);
		CHECK_NE(aborted_error, OK);
		CHECK_NE(aborted_error, FAILED);
		CHECK_EQ(aborted_result.outcome, "structural_failure");
		REQUIRE_FALSE(aborted_result.structural_failures.is_empty());
		CHECK_EQ(aborted_result.structural_failures[0].stage, "run_aborted");
		CHECK(aborted_result.findings.is_empty());
	}

	TEST_CASE("TypeCompleteness GateSafety reports a stale ledger entry without discarding evidence") {
		const FSCompletenessResolution resolution = load_union_pilot_completeness_resolution();
		const String case_id = gate_safety_pilot_case_id(
				resolution, "text", "union", "numeric_constant", "argument_binding");
		REQUIRE_FALSE(case_id.is_empty());
		const String finding_id = gate_safety_finding_id(case_id, "stored_carrier");

		TemporaryProjectTree tree(
				vformat("type_completeness_gate_stale_%d", OS::get_singleton()->get_process_id()));
		REQUIRE(tree.is_valid());
		const String catalog_root = stage_union_pilot_completeness_catalog(tree);
		tree.write_file("catalog/findings/" + finding_id + ".json",
				gate_safety_finding_record(finding_id, case_id, "stored_carrier", "product_defect"));

		FSCompletenessRunOptions options;
		options.catalog_root = catalog_root;
		options.family = "union_destination_membership";
		options.scratch_root = tree.root;
		options.report_path = tree.root.path_join("report.json");
		FSCompletenessRunResult result;
		CHECK_EQ(FSCompletenessRunner::run(options, result), ERR_INVALID_DATA);
		CHECK_EQ(result.outcome, "structural_failure");
		CHECK_FALSE(result.success);
		CHECK(result.findings.is_empty());
		REQUIRE_EQ(result.structural_failures.size(), 1);
		CHECK_EQ(result.structural_failures[0].stage, "ledger_entry_stale");
		CHECK_EQ(result.structural_failures[0].case_id, case_id);

		const Dictionary report = result.report;
		CHECK_EQ(String(report.get("outcome", String())), "structural_failure");
		CHECK_EQ(bool(report.get("success", true)), false);
		CHECK_EQ(Array(report.get("cases", Array())).size(), 40);
		const Dictionary ledger = report.get("ledger", Dictionary());
		CHECK(Array(ledger.get("reconciled", Array())).is_empty());
		const Array stale = ledger.get("stale", Array());
		REQUIRE_EQ(stale.size(), 1);
		CHECK_EQ(String(Dictionary(stale[0]).get("finding_id", String())), finding_id);
		CHECK_EQ(String(Dictionary(stale[0]).get("case_id", String())), case_id);
		CHECK_EQ(String(Dictionary(stale[0]).get("dimension", String())), "stored_carrier");

		Error read_error = OK;
		const String published = FileAccess::get_file_as_string(options.report_path, &read_error);
		REQUIRE_EQ(read_error, OK);
		JSON json;
		REQUIRE_EQ(json.parse(published), OK);
		CHECK_EQ(Dictionary(json.get_data()), report);
	}

	TEST_CASE("TypeCompleteness GateSafety keeps a classified failing witness blocking and unwitnessed") {
		const FSCompletenessResolution resolution = load_union_pilot_completeness_resolution();
		union_pilot_mutated_case_id =
				gate_safety_witness_case_id(resolution, "text_gradual_argument_binding");
		REQUIRE_FALSE(union_pilot_mutated_case_id.is_empty());
		const String finding_id =
				gate_safety_finding_id(union_pilot_mutated_case_id, "runtime_obligation");

		TemporaryProjectTree tree(
				vformat("type_completeness_gate_witness_%d", OS::get_singleton()->get_process_id()));
		REQUIRE(tree.is_valid());
		const String catalog_root = stage_union_pilot_completeness_catalog(tree);
		tree.write_file("catalog/findings/" + finding_id + ".json",
				gate_safety_finding_record(finding_id, union_pilot_mutated_case_id, "runtime_obligation",
						"intentional_unsupported"));

		FSCompletenessRunOptions options;
		options.catalog_root = catalog_root;
		options.family = "union_destination_membership";
		options.scratch_root = tree.root;
		options.report_path = tree.root.path_join("report.json");
		options.observation_mutator = corrupt_union_pilot_witness_dimension;
		FSCompletenessRunResult result;
		CHECK_EQ(FSCompletenessRunner::run(options, result), FAILED);
		CHECK_FALSE(result.success);
		CHECK(result.structural_failures.is_empty());
		const FSCompletenessFinding *finding =
				gate_safety_find_finding(result, union_pilot_mutated_case_id, "runtime_obligation");
		REQUIRE(finding != nullptr);
		CHECK_EQ(finding->classification, "intentional_unsupported");
		CHECK_EQ(finding->finding_id, finding_id);

		const Array exceptions = result.report.get("exceptions", Array());
		REQUIRE_EQ(exceptions.size(), 1);
		const Dictionary exception_report = exceptions[0];
		CHECK_EQ(String(exception_report.get("exception_id", String())),
				"unproven_source_requires_membership");
		CHECK_EQ(bool(exception_report.get("witnessed", true)), false);
		const Array positive = exception_report.get("positive_witnesses", Array());
		REQUIRE_EQ(positive.size(), 2);
		bool checked_witness = false;
		for (int index = 0; index < positive.size(); index++) {
			const Dictionary witness = positive[index];
			if (String(witness.get("witness_id", String())) != "text_gradual_argument_binding") {
				continue;
			}
			checked_witness = true;
			CHECK_EQ(String(witness.get("case_id", String())), union_pilot_mutated_case_id);
			CHECK_EQ(bool(witness.get("witnessed", true)), false);
			const Array blocking = witness.get("blocking_finding_ids", Array());
			CHECK(blocking.has(finding_id));
		}
		CHECK(checked_witness);
		const Dictionary ledger = result.report.get("ledger", Dictionary());
		CHECK_EQ(Array(ledger.get("reconciled", Array())).size(), 1);
		CHECK(Array(ledger.get("stale", Array())).is_empty());
	}

	TEST_CASE("TypeCompleteness GateSafety reports every exception witnessed on a clean run") {
		TemporaryProjectTree tree(
				vformat("type_completeness_gate_witnessed_%d", OS::get_singleton()->get_process_id()));
		REQUIRE(tree.is_valid());
		FSCompletenessRunOptions options;
		options.catalog_root = type_completeness_union_pilot_root;
		options.family = "union_destination_membership";
		options.scratch_root = tree.root;
		options.report_path = tree.root.path_join("report.json");
		FSCompletenessRunResult result;
		REQUIRE_EQ(FSCompletenessRunner::run(options, result), OK);
		CHECK_EQ(result.outcome, "passed");
		CHECK(result.structural_failures.is_empty());

		const Array exceptions = result.report.get("exceptions", Array());
		REQUIRE_EQ(exceptions.size(), 1);
		const Dictionary exception_report = exceptions[0];
		CHECK_EQ(bool(exception_report.get("witnessed", false)), true);
		CHECK_EQ(Array(exception_report.get("positive_witnesses", Array())).size(), 2);
		CHECK_EQ(Array(exception_report.get("boundary_witnesses", Array())).size(), 1);
		const Array boundary = exception_report.get("boundary_witnesses", Array());
		CHECK_EQ(String(Dictionary(boundary[0]).get("witness_id", String())),
				"text_static_member_argument_binding");
		CHECK_EQ(bool(Dictionary(boundary[0]).get("witnessed", false)), true);
	}

	TEST_CASE("TypeCompleteness GateSafety records analyzer diagnostic severity and codes") {
		TemporaryProjectTree tree(
				vformat("type_completeness_gate_severity_evidence_%d", OS::get_singleton()->get_process_id()));
		REQUIRE(tree.is_valid());
		FSCompletenessRunOptions options;
		options.catalog_root = type_completeness_union_pilot_root;
		options.family = "union_destination_membership";
		options.scratch_root = tree.root;
		options.report_path = tree.root.path_join("report.json");
		FSCompletenessRunResult result;
		REQUIRE_EQ(FSCompletenessRunner::run(options, result), OK);

		const Array cases = result.report.get("cases", Array());
		REQUIRE_EQ(cases.size(), 40);
		int cases_with_severity = 0;
		for (int index = 0; index < cases.size(); index++) {
			const Dictionary case_report = cases[index];
			const String severity = case_report.get("diagnostic_severity", String());
			CHECK_NE(severity, "error");
			CHECK((severity == "none" || severity == "warning"));
			cases_with_severity++;
			const Array records = case_report.get("diagnostic_records", Array());
			for (int record_index = 0; record_index < records.size(); record_index++) {
				const Dictionary record = records[record_index];
				CHECK(record.has("severity"));
				CHECK(record.has("code"));
				CHECK(record.has("category"));
				CHECK(record.has("suppressed"));
				CHECK_FALSE(String(record.get("code", String())).is_empty());
			}
		}
		CHECK_EQ(cases_with_severity, 40);
	}

	// Warning collection is a debug-build capability, so these cases are scoped to it.
#ifdef DEBUG_ENABLED
	TEST_CASE("TypeCompleteness GateSafety records a warning code and its suppression") {
		// Warning levels are process-global and are not initialized by a test run, so the ambient pass
		// only sees this code when the case pins it.
		Vector<WarningLevelOverride> overrides;
		WarningLevelOverride unused_variable;
		unused_variable.code = FSWarning::UNUSED_VARIABLE;
		unused_variable.level = FSWarning::WARN;
		overrides.push_back(unused_variable);
		const WarningSettingsScope warning_settings(overrides);

		FSCompletenessProgram warned;
		warned.case_id = "gate_safety_unused_variable";
		warned.surface = "text";
		warned.source = "func test() -> void:\n\tvar unused_local := 1\n";

		const FSCompletenessObservation observation = FSUnionCompletenessAdapter::analyze(warned, "text");
		CHECK_EQ(String(observation.dimensions.get("analysis", String())), "accept");
		CHECK(observation.diagnostics.is_empty());
		bool found_warning = false;
		for (int index = 0; index < observation.diagnostic_records.size(); index++) {
			const Dictionary record = observation.diagnostic_records[index];
			if (String(record.get("code", String())) != "UNUSED_VARIABLE") {
				continue;
			}
			found_warning = true;
			CHECK_EQ(String(record.get("severity", String())), "warning");
			CHECK_EQ(String(record.get("category", String())), "analysis");
			CHECK_EQ(bool(record.get("suppressed", true)), false);
			CHECK_EQ(int(record.get("line", 0)), 2);
		}
		CHECK(found_warning);

		FSCompletenessProgram suppressed;
		suppressed.case_id = "gate_safety_unused_variable_suppressed";
		suppressed.surface = "text";
		suppressed.source =
				"func test() -> void:\n\t@warning_ignore(\"unused_variable\")\n\tvar unused_local := 1\n";

		const FSCompletenessObservation suppressed_observation =
				FSUnionCompletenessAdapter::analyze(suppressed, "text");
		CHECK_EQ(String(suppressed_observation.dimensions.get("analysis", String())), "accept");
		CHECK(suppressed_observation.diagnostics.is_empty());
		bool found_suppressed = false;
		for (int index = 0; index < suppressed_observation.diagnostic_records.size(); index++) {
			const Dictionary record = suppressed_observation.diagnostic_records[index];
			if (String(record.get("code", String())) != "UNUSED_VARIABLE") {
				continue;
			}
			found_suppressed = true;
			CHECK_EQ(String(record.get("severity", String())), "warning");
			CHECK_EQ(bool(record.get("suppressed", false)), true);
			CHECK_EQ(int(record.get("line", 0)), 3);
		}
		CHECK(found_suppressed);

		FSCompletenessProgram inline_suppressed;
		inline_suppressed.case_id = "gate_safety_unused_variable_inline";
		inline_suppressed.surface = "text";
		inline_suppressed.source =
				"func test() -> void:\n\t@warning_ignore(\"unused_variable\") var unused_local := 1\n";

		const FSCompletenessObservation inline_observation =
				FSUnionCompletenessAdapter::analyze(inline_suppressed, "text");
		CHECK_EQ(String(inline_observation.dimensions.get("analysis", String())), "accept");
		CHECK(inline_observation.diagnostics.is_empty());
		bool found_inline_suppressed = false;
		for (int index = 0; index < inline_observation.diagnostic_records.size(); index++) {
			const Dictionary record = inline_observation.diagnostic_records[index];
			if (String(record.get("code", String())) != "UNUSED_VARIABLE") {
				continue;
			}
			found_inline_suppressed = true;
			CHECK_EQ(bool(record.get("suppressed", false)), true);
			CHECK_EQ(int(record.get("line", 0)), 2);
		}
		CHECK(found_inline_suppressed);
	}

	TEST_CASE("TypeCompleteness GateSafety records a suppressed error-level diagnostic") {
		Vector<WarningLevelOverride> overrides;
		WarningLevelOverride unused_variable;
		unused_variable.code = FSWarning::UNUSED_VARIABLE;
		unused_variable.level = FSWarning::ERROR;
		overrides.push_back(unused_variable);
		const WarningSettingsScope warning_settings(overrides);

		FSCompletenessProgram suppressed;
		suppressed.case_id = "gate_safety_suppressed_error_level";
		suppressed.surface = "text";
		suppressed.source =
				"func test() -> void:\n\t@warning_ignore(\"unused_variable\")\n\tvar unused_local := 1\n";

		const FSCompletenessObservation observation = FSUnionCompletenessAdapter::analyze(suppressed, "text");
		// The annotation hides the promoted diagnostic from the analysis outcome entirely.
		CHECK_EQ(String(observation.dimensions.get("analysis", String())), "accept");
		CHECK(observation.diagnostics.is_empty());
		bool found_suppressed_error = false;
		for (int index = 0; index < observation.diagnostic_records.size(); index++) {
			const Dictionary record = observation.diagnostic_records[index];
			if (String(record.get("severity", String())) != "error") {
				continue;
			}
			found_suppressed_error = true;
			CHECK_EQ(String(record.get("code", String())), "suppressed_analysis_error");
			CHECK_EQ(bool(record.get("suppressed", false)), true);
		}
		CHECK(found_suppressed_error);
	}
#endif // DEBUG_ENABLED

	TEST_CASE("TypeCompleteness GateSafety records rejected analysis as error severity") {
		FSCompletenessProgram invalid;
		invalid.case_id = "gate_safety_rejected_program";
		invalid.surface = "text";
		invalid.source = "func test() -> void:\n\tvar value: int = \"not an integer\"\n";

		const FSCompletenessObservation observation = FSUnionCompletenessAdapter::analyze(invalid, "text");
		CHECK_EQ(String(observation.dimensions.get("analysis", String())), "reject");
		REQUIRE_FALSE(observation.diagnostics.is_empty());
		int error_records = 0;
		for (int index = 0; index < observation.diagnostic_records.size(); index++) {
			const Dictionary record = observation.diagnostic_records[index];
			if (String(record.get("severity", String())) != "error") {
				continue;
			}
			error_records++;
			CHECK_EQ(String(record.get("code", String())), "analyzer_error");
			CHECK_EQ(String(record.get("category", String())), "analysis");
		}
		CHECK_EQ(error_records, observation.diagnostics.size());
	}

	TEST_CASE("TypeCompleteness GateSafety detects a warning-to-error severity mutation") {
		const FSCompletenessResolution resolution = load_union_pilot_completeness_resolution();
		gate_safety_severity_case_id = gate_safety_pilot_case_id(
				resolution, "text", "plain", "gradual", "argument_binding");
		REQUIRE_FALSE(gate_safety_severity_case_id.is_empty());

		TemporaryProjectTree tree(
				vformat("type_completeness_gate_warning_to_error_%d", OS::get_singleton()->get_process_id()));
		REQUIRE(tree.is_valid());
		FSCompletenessRunOptions options;
		options.catalog_root = type_completeness_union_pilot_root;
		options.family = "union_destination_membership";
		options.scratch_root = tree.root;
		options.report_path = tree.root.path_join("report.json");
		options.observation_mutator = gate_safety_inject_error_record;
		FSCompletenessRunResult result;
		CHECK_EQ(FSCompletenessRunner::run(options, result), FAILED);
		const FSCompletenessFinding *finding =
				gate_safety_find_finding(result, gate_safety_severity_case_id, "diagnostic_severity");
		REQUIRE(finding != nullptr);
		CHECK_EQ(String(finding->expected), "at_most_warning");
		CHECK_EQ(String(finding->actual), "error");
		CHECK_EQ(String(gate_safety_case_report(result.report, gate_safety_severity_case_id)
								 .get("diagnostic_severity", String())),
				"error");
		gate_safety_severity_case_id.clear();
	}

	TEST_CASE("TypeCompleteness GateSafety detects an error-to-warning severity mutation") {
		const FSCompletenessResolution resolution = load_union_pilot_completeness_resolution();
		gate_safety_severity_case_id = gate_safety_pilot_case_id(
				resolution, "text", "plain", "gradual", "argument_binding");
		REQUIRE_FALSE(gate_safety_severity_case_id.is_empty());

		TemporaryProjectTree tree(
				vformat("type_completeness_gate_error_to_warning_%d", OS::get_singleton()->get_process_id()));
		REQUIRE(tree.is_valid());
		const String catalog_root = stage_union_pilot_completeness_catalog(tree);
		Vector<String> replacements;
		replacements.push_back("\"analysis\": \"accept\"");
		replacements.push_back("\"analysis\": \"reject\"");
		gate_safety_rewrite_staged_rules(catalog_root, replacements);

		FSCompletenessRunOptions options;
		options.catalog_root = catalog_root;
		options.family = "union_destination_membership";
		options.scratch_root = tree.root;
		options.report_path = tree.root.path_join("report.json");
		options.observation_mutator = gate_safety_inject_warning_record;
		FSCompletenessRunResult result;
		CHECK_EQ(FSCompletenessRunner::run(options, result), FAILED);

		const FSCompletenessFinding *severity_finding =
				gate_safety_find_finding(result, gate_safety_severity_case_id, "diagnostic_severity");
		REQUIRE(severity_finding != nullptr);
		CHECK_EQ(String(severity_finding->expected), "error");
		CHECK_EQ(String(severity_finding->actual), "warning");
		CHECK(gate_safety_find_finding(result, gate_safety_severity_case_id, "analysis") != nullptr);

		int severity_findings = 0;
		for (const FSCompletenessFinding &finding : result.findings) {
			if (finding.dimension != "diagnostic_severity") {
				continue;
			}
			severity_findings++;
			CHECK_EQ(String(finding.expected), "error");
			CHECK_NE(String(finding.actual), "error");
		}
		CHECK_EQ(severity_findings, 40);
		gate_safety_severity_case_id.clear();
	}

	TEST_CASE("TypeCompleteness GateSafety names unknown and duplicated witness declarations") {
		TemporaryProjectTree tree(
				vformat("type_completeness_gate_witness_id_%d", OS::get_singleton()->get_process_id()));
		REQUIRE(tree.is_valid());
		const String unknown_catalog = stage_union_pilot_completeness_catalog_at(tree, "unknown_catalog");
		Vector<String> unknown_replacements;
		unknown_replacements.push_back("\"text_gradual_argument_binding\"");
		unknown_replacements.push_back("\"not_a_declared_witness\"");
		gate_safety_rewrite_staged_rules(unknown_catalog, unknown_replacements);

		FSCompletenessRunOptions options;
		options.catalog_root = unknown_catalog;
		options.family = "union_destination_membership";
		options.scratch_root = tree.root.path_join("unknown_scratch");
		options.report_path = options.scratch_root.path_join("report.json");
		FSCompletenessRunResult result;
		CHECK_EQ(FSCompletenessRunner::run(options, result), ERR_INVALID_DATA);
		CHECK_EQ(result.outcome, "structural_failure");
		CHECK(gate_safety_has_structural_stage(result, "witness_id_unknown"));
		CHECK_EQ(result.structural_failures[0].witness_id, "not_a_declared_witness");
		CHECK_EQ(result.structural_failures[0].exception_id, "unproven_source_requires_membership");
		CHECK_FALSE(FileAccess::exists(options.report_path));

		const String duplicate_catalog = stage_union_pilot_completeness_catalog_at(tree, "duplicate_catalog");
		Vector<String> duplicate_replacements;
		duplicate_replacements.push_back("\"text_static_member_argument_binding\"");
		duplicate_replacements.push_back("\"text_gradual_argument_binding\"");
		gate_safety_rewrite_staged_rules(duplicate_catalog, duplicate_replacements);

		FSCompletenessRunOptions duplicate_options;
		duplicate_options.catalog_root = duplicate_catalog;
		duplicate_options.family = "union_destination_membership";
		duplicate_options.scratch_root = tree.root.path_join("duplicate_scratch");
		duplicate_options.report_path = duplicate_options.scratch_root.path_join("report.json");
		FSCompletenessRunResult duplicate_result;
		CHECK_EQ(FSCompletenessRunner::run(duplicate_options, duplicate_result), ERR_INVALID_DATA);
		CHECK_EQ(duplicate_result.outcome, "structural_failure");
		CHECK(gate_safety_has_structural_stage(duplicate_result, "witness_declared_twice"));
	}

	TEST_CASE("TypeCompleteness GateSafety names a witness that never observes its exception") {
		TemporaryProjectTree tree(
				vformat("type_completeness_gate_witness_provenance_%d", OS::get_singleton()->get_process_id()));
		REQUIRE(tree.is_valid());
		const String catalog_root = stage_union_pilot_completeness_catalog(tree);
		Vector<String> replacements;
		replacements.push_back("\"text_gradual_argument_binding\"");
		replacements.push_back("\"gate_safety_swap_placeholder\"");
		replacements.push_back("\"text_static_member_argument_binding\"");
		replacements.push_back("\"text_gradual_argument_binding\"");
		replacements.push_back("\"gate_safety_swap_placeholder\"");
		replacements.push_back("\"text_static_member_argument_binding\"");
		gate_safety_rewrite_staged_rules(catalog_root, replacements);

		FSCompletenessRunOptions options;
		options.catalog_root = catalog_root;
		options.family = "union_destination_membership";
		options.scratch_root = tree.root;
		options.report_path = tree.root.path_join("report.json");
		FSCompletenessRunResult result;
		CHECK_EQ(FSCompletenessRunner::run(options, result), ERR_INVALID_DATA);
		CHECK_EQ(result.outcome, "structural_failure");
		CHECK(gate_safety_has_structural_stage(result, "witness_exception_provenance_missing"));
		for (const FSCompletenessStructuralFailure &failure : result.structural_failures) {
			if (failure.stage != "witness_exception_provenance_missing") {
				continue;
			}
			CHECK_EQ(failure.witness_id, "text_static_member_argument_binding");
			CHECK_FALSE(failure.case_id.is_empty());
		}
	}

	TEST_CASE("TypeCompleteness GateSafety unwitnesses an exception when only the paired surface fails") {
		const FSCompletenessResolution resolution = load_union_pilot_completeness_resolution();
		const String witness_case_id =
				gate_safety_witness_case_id(resolution, "text_gradual_argument_binding");
		REQUIRE_FALSE(witness_case_id.is_empty());
		gate_safety_partner_case_id =
				gate_safety_pilot_case_id(resolution, "bytecode", "union", "gradual", "argument_binding");
		REQUIRE_FALSE(gate_safety_partner_case_id.is_empty());

		TemporaryProjectTree tree(
				vformat("type_completeness_gate_pair_witness_%d", OS::get_singleton()->get_process_id()));
		REQUIRE(tree.is_valid());
		FSCompletenessRunOptions options;
		options.catalog_root = type_completeness_union_pilot_root;
		options.family = "union_destination_membership";
		options.scratch_root = tree.root;
		options.report_path = tree.root.path_join("report.json");
		options.observation_mutator = gate_safety_corrupt_partner_obligation;
		FSCompletenessRunResult result;
		CHECK_EQ(FSCompletenessRunner::run(options, result), FAILED);
		// The witness case itself carries no finding; only its paired surface does.
		CHECK(gate_safety_find_finding(result, witness_case_id, "runtime_obligation") == nullptr);
		const FSCompletenessFinding *partner_finding =
				gate_safety_find_finding(result, gate_safety_partner_case_id, "runtime_obligation");
		REQUIRE(partner_finding != nullptr);
		CHECK_EQ(String(gate_safety_case_report(result.report, witness_case_id).get("status", String())),
				"failed");

		const Array exceptions = result.report.get("exceptions", Array());
		REQUIRE_EQ(exceptions.size(), 1);
		const Dictionary exception_report = exceptions[0];
		CHECK_EQ(bool(exception_report.get("witnessed", true)), false);
		const Array positive = exception_report.get("positive_witnesses", Array());
		bool checked_witness = false;
		for (int index = 0; index < positive.size(); index++) {
			const Dictionary witness = positive[index];
			if (String(witness.get("witness_id", String())) != "text_gradual_argument_binding") {
				continue;
			}
			checked_witness = true;
			CHECK_EQ(bool(witness.get("witnessed", true)), false);
			CHECK(Array(witness.get("blocking_finding_ids", Array())).has(partner_finding->finding_id));
		}
		CHECK(checked_witness);
		gate_safety_partner_case_id.clear();
	}

	TEST_CASE("TypeCompleteness GateSafety keeps parity evidence on both surfaces of a failing pair") {
		const FSCompletenessResolution resolution = load_union_pilot_completeness_resolution();
		select_union_pilot_pair(resolution, "union", "numeric_constant", "argument_binding");
		union_pilot_mutated_case_id = union_pilot_mutated_pair_text_id;
		union_pilot_mutation_count = 0;

		TemporaryProjectTree tree(
				vformat("type_completeness_gate_parity_%d", OS::get_singleton()->get_process_id()));
		REQUIRE(tree.is_valid());
		FSCompletenessRunOptions options;
		options.catalog_root = type_completeness_union_pilot_root;
		options.family = "union_destination_membership";
		options.scratch_root = tree.root;
		options.report_path = tree.root.path_join("report.json");
		options.observation_mutator = corrupt_union_pilot_stored_carrier;
		FSCompletenessRunResult result;
		CHECK_EQ(FSCompletenessRunner::run(options, result), FAILED);
		CHECK_EQ(int(result.report.get("text_bytecode_parity_failures", 0)), 1);

		for (const String &case_id : { union_pilot_mutated_pair_text_id, union_pilot_mutated_pair_bytecode_id }) {
			CAPTURE(case_id);
			const Dictionary case_report = gate_safety_case_report(result.report, case_id);
			REQUIRE_FALSE(case_report.is_empty());
			CHECK_EQ(String(case_report.get("status", String())), "failed");
			const Dictionary evidence = case_report.get("parity_evidence", Dictionary());
			REQUIRE_FALSE(evidence.is_empty());
			CHECK_EQ(String(evidence.get("text_case_id", String())), union_pilot_mutated_pair_text_id);
			CHECK_EQ(String(evidence.get("bytecode_case_id", String())), union_pilot_mutated_pair_bytecode_id);
			CHECK(Dictionary(evidence.get("dimensions", Dictionary())).has("stored_carrier"));
		}

		const Dictionary passing_case = gate_safety_case_report(result.report,
				gate_safety_pilot_case_id(resolution, "text", "plain", "static_member", "argument_binding"));
		REQUIRE_FALSE(passing_case.is_empty());
		CHECK(Dictionary(passing_case.get("parity_evidence", Dictionary())).is_empty());
	}
}

} // namespace FSTests
