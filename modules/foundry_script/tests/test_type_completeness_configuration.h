/**************************************************************************/
/*  test_type_completeness_configuration.h                                */
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
#include "modules/foundry_script/tests/fs_type_completeness_cache.h"
#include "modules/foundry_script/tests/fs_type_completeness_common.h"
#include "modules/foundry_script/tests/fs_type_completeness_runner.h"

#include "core/io/json.h"
#include "core/os/os.h"
#include "core/variant/dictionary.h"
#include "core/variant/variant.h"

#include "tests/test_macros.h"

namespace FSTests {

// A family whose rules expect an analyzer warning from some of their cells, and one that expects none
// but whose cells produce warnings when the analyzer emits any. Between them they cover both ways a
// build without warnings differs from one with them.
static const char *configuration_warning_expecting_family = "wrapper_parity_argument_binding";
static const char *configuration_warning_observing_family = "union_destination_membership";

static bool configuration_analyzer_warnings() {
	return bool(FSCompletenessRunner::configuration_report().get("analyzer_warnings", true));
}

// Breaks the one thing every cell of every family is judged on, whatever else it expects.
static void configuration_corrupt_produced_output(FSCompletenessObservation &r_observation) {
	r_observation.produced_output = "corrupted by the configuration probe\n";
}

static Dictionary configuration_with_analyzer_warnings(bool p_analyzer_warnings) {
	Dictionary configuration;
	configuration["analyzer_warnings"] = p_analyzer_warnings;
	return configuration;
}

// A document shaped like a published report in exactly the members the configuration projection
// reads: two cases, one of which expects a warning, and the exceptions each of them witnesses.
static Dictionary configuration_evidence_document() {
	Dictionary warning_case;
	warning_case["case_id"] = "expects_warning";
	Dictionary warning_expectation;
	warning_expectation["diagnostic_severity"] = "warning";
	warning_case["expected"] = warning_expectation;
	warning_case["diagnostic_severity"] = "warning";
	Array warning_records;
	Dictionary warning_record;
	warning_record["severity"] = "warning";
	warning_record["code"] = "UNSAFE_CALL_ARGUMENT";
	warning_records.push_back(warning_record);
	warning_case["diagnostic_records"] = warning_records;

	Dictionary error_case;
	error_case["case_id"] = "expects_rejection";
	Dictionary error_expectation;
	error_expectation["analysis"] = "reject";
	error_case["expected"] = error_expectation;
	error_case["diagnostic_severity"] = "error";
	Array error_records;
	Dictionary suppressed_warning;
	suppressed_warning["severity"] = "warning";
	suppressed_warning["code"] = "UNSAFE_CALL_ARGUMENT";
	error_records.push_back(suppressed_warning);
	Dictionary error_record;
	error_record["severity"] = "error";
	error_record["code"] = "INVALID_ARGUMENT";
	error_records.push_back(error_record);
	error_case["diagnostic_records"] = error_records;

	Array cases;
	cases.push_back(warning_case);
	cases.push_back(error_case);

	Array exceptions;
	for (const char *pair : { "expects_warning", "expects_rejection" }) {
		Dictionary exception;
		exception["exception_id"] = String("witnessed_by_") + pair;
		Array witnesses;
		Dictionary witness;
		witness["case_id"] = pair;
		witnesses.push_back(witness);
		exception["positive_witnesses"] = witnesses;
		exception["boundary_witnesses"] = Array();
		exceptions.push_back(exception);
	}

	Dictionary document;
	document["cases"] = cases;
	document["exceptions"] = exceptions;
	document["cell_count"] = 2.0;
	return document;
}

static Vector<String> configuration_case_ids(const Variant &p_document) {
	Vector<String> case_ids;
	const Array cases = Dictionary(p_document).get("cases", Array());
	for (int index = 0; index < cases.size(); index++) {
		case_ids.push_back(Dictionary(cases[index]).get("case_id", String()));
	}
	return case_ids;
}

TEST_SUITE("[Modules][FoundryScript][TypeCompleteness][Configuration]") {
	TEST_CASE("TypeCompleteness Configuration names the diagnostics surface this build has") {
		const Dictionary configuration = FSCompletenessRunner::configuration_report();
		REQUIRE(configuration.has("analyzer_warnings"));
		CHECK_EQ(configuration["analyzer_warnings"].get_type(), Variant::BOOL);

		// The claim is tied to what a run actually observes rather than to the macro it was decided
		// from: a build that says it emits warnings has to have observed one somewhere in a family whose
		// programs warrant them, and a build that says it emits none may not have observed any.
		const FSCompletenessRunResult *baseline =
				FSCompletenessBaseline::shared_or_skip(configuration_warning_observing_family);
		if (baseline == nullptr) {
			return;
		}
		bool observed_warning = false;
		const Array cases = Dictionary(baseline->report).get("cases", Array());
		REQUIRE_FALSE(cases.is_empty());
		for (int index = 0; index < cases.size(); index++) {
			if (String(Dictionary(cases[index]).get("diagnostic_severity", String())) == "warning") {
				observed_warning = true;
				break;
			}
		}
		CHECK_EQ(observed_warning, configuration_analyzer_warnings());
	}

	TEST_CASE("TypeCompleteness Configuration publishes a cell it cannot judge as not covered") {
		const FSCompletenessRunResult *baseline =
				FSCompletenessBaseline::shared_or_skip(configuration_warning_expecting_family);
		if (baseline == nullptr) {
			return;
		}
		// Whatever the configuration, the run publishes evidence and reaches a verdict: a build that
		// cannot judge some cells is not a build that aborts.
		CHECK_EQ(baseline->outcome, "passed");
		CHECK(FSCompletenessRunner::report_carries_evidence(baseline->report));

		HashSet<String> findings_by_case;
		const Array findings = Dictionary(baseline->report).get("findings", Array());
		for (int index = 0; index < findings.size(); index++) {
			findings_by_case.insert(Dictionary(findings[index]).get("case_id", String()));
		}

		Vector<String> reported_not_covered;
		int warning_expecting_cases = 0;
		const Array cases = Dictionary(baseline->report).get("cases", Array());
		REQUIRE_FALSE(cases.is_empty());
		for (int index = 0; index < cases.size(); index++) {
			const Dictionary case_record = cases[index];
			const String case_id = case_record.get("case_id", String());
			CAPTURE(case_id);
			const bool expects_warning = FSCompletenessRunner::expectation_requires_analyzer_warnings(
					case_record.get("expected", Dictionary()));
			warning_expecting_cases += expects_warning ? 1 : 0;
			const bool not_covered = String(case_record.get("status", String())) == "not_covered";
			// A cell is not covered exactly when this build cannot observe what it expects. Nothing else
			// may reach that status, and nothing that expects a warning may escape it.
			CHECK_EQ(not_covered, expects_warning && !configuration_analyzer_warnings());
			if (!not_covered) {
				CHECK_FALSE(case_record.has("not_covered_reason"));
				continue;
			}
			reported_not_covered.push_back(case_id);
			CHECK_EQ(String(case_record.get("not_covered_reason", String())),
					String(FSCompletenessNotCoveredReason::DIAGNOSTICS_UNAVAILABLE_IN_CONFIGURATION));
			CHECK_EQ(String(case_record.get("category", String())), "not_covered");
			// It is neither a pass nor a failure, and nothing was derived from an observation that could
			// not decide it.
			CHECK_FALSE(bool(case_record.get("passed", true)));
			CHECK_FALSE(findings_by_case.has(case_id));
		}
		CHECK_GT(warning_expecting_cases, 0);
		reported_not_covered.sort();
		REQUIRE_EQ(reported_not_covered.size(), baseline->not_covered_case_ids.size());
		for (int index = 0; index < reported_not_covered.size(); index++) {
			CHECK_EQ(reported_not_covered[index], baseline->not_covered_case_ids[index]);
		}
		if (configuration_analyzer_warnings()) {
			CHECK(baseline->not_covered_case_ids.is_empty());
		} else {
			CHECK_FALSE(baseline->not_covered_case_ids.is_empty());
		}
	}

	TEST_CASE("TypeCompleteness Configuration judges a cell on everything the build can observe") {
		// A cell expecting a warning is exempt from that expectation on a build that emits none, and from
		// nothing else: its output, its runtime status and its other dimensions are observed here exactly
		// as they are anywhere, so a regression in one of them is reported by this build too.
		TemporaryProjectTree tree(vformat("type_completeness_configuration_evidence_%d",
				OS::get_singleton()->get_process_id()));
		REQUIRE(tree.is_valid());
		FSCompletenessRunOptions options;
		options.catalog_root = tracked_catalog_root();
		options.family = configuration_warning_expecting_family;
		options.scratch_root = tree.root;
		options.report_path = tree.root.path_join("report.json");
		options.observation_mutator = configuration_corrupt_produced_output;

		FSCompletenessRunResult result;
		CHECK_EQ(FSCompletenessRunner::run(options, result), FAILED);
		CHECK_EQ(result.outcome, "product_mismatch");
		CHECK_FALSE(result.success);
		CHECK(result.structural_failures.is_empty());
		// Nothing is published as unjudged when every cell failed on evidence this build did gather.
		CHECK(result.not_covered_case_ids.is_empty());

		HashSet<String> findings_by_case;
		for (const FSCompletenessFinding &finding : result.findings) {
			findings_by_case.insert(finding.case_id);
		}
		int warning_expecting_cases = 0;
		const Array cases = Dictionary(result.report).get("cases", Array());
		REQUIRE_FALSE(cases.is_empty());
		for (int index = 0; index < cases.size(); index++) {
			const Dictionary case_record = cases[index];
			if (!FSCompletenessRunner::expectation_requires_analyzer_warnings(
						case_record.get("expected", Dictionary()))) {
				continue;
			}
			warning_expecting_cases++;
			CAPTURE(String(case_record.get("case_id", String())));
			CHECK_EQ(String(case_record.get("status", String())), "failed");
			CHECK_FALSE(case_record.has("not_covered_reason"));
			CHECK(findings_by_case.has(String(case_record.get("case_id", String()))));
		}
		CHECK_GT(warning_expecting_cases, 0);
	}

	TEST_CASE("TypeCompleteness Configuration compares only what a build could observe") {
		const Dictionary document = configuration_evidence_document();

		SUBCASE("a build that warns compares the whole document") {
			const Variant observable = FSCompletenessRunner::evidence_observable_in_configuration(
					document, configuration_with_analyzer_warnings(true));
			CHECK_EQ(JSON::stringify(observable, "  "), JSON::stringify(Variant(document), "  "));
		}

		SUBCASE("a build that does not warn drops what it could not have produced") {
			const Variant observable = FSCompletenessRunner::evidence_observable_in_configuration(
					document, configuration_with_analyzer_warnings(false));
			// The cell whose expectation is a warning, and the exception it witnesses, are not comparable
			// at all; the cell that expects a rejection still is, minus the warning it never emitted.
			const Vector<String> comparable_case_ids = configuration_case_ids(observable);
			REQUIRE_EQ(comparable_case_ids.size(), 1);
			CHECK_EQ(comparable_case_ids[0], "expects_rejection");
			const Array exceptions = Dictionary(observable).get("exceptions", Array());
			REQUIRE_EQ(exceptions.size(), 1);
			CHECK_EQ(String(Dictionary(exceptions[0]).get("exception_id", String())),
					"witnessed_by_expects_rejection");
			const Dictionary surviving_case = Array(Dictionary(observable).get("cases", Array()))[0];
			const Array records = surviving_case.get("diagnostic_records", Array());
			REQUIRE_EQ(records.size(), 1);
			CHECK_EQ(String(Dictionary(records[0]).get("severity", String())), "error");
			// An error is still an error: only the severity a build cannot produce is normalized away.
			CHECK_EQ(String(surviving_case.get("diagnostic_severity", String())), "error");
			CHECK_EQ(double(Dictionary(observable).get("cell_count", 0.0)), 2.0);
		}

		SUBCASE("an observed warning reads as no diagnostic where none can be produced") {
			Dictionary warning_only;
			warning_only["diagnostic_severity"] = "warning";
			warning_only["diagnostic_records"] = Array(Dictionary(document).get("cases", Array()));
			const Dictionary observable = FSCompletenessRunner::evidence_observable_in_configuration(
					warning_only, configuration_with_analyzer_warnings(false));
			CHECK_EQ(String(observable.get("diagnostic_severity", String())), "none");
		}
	}

	TEST_CASE("TypeCompleteness Configuration publishes what the build could not confirm") {
		const FSCompletenessRunResult *baseline =
				FSCompletenessBaseline::shared_or_skip(configuration_warning_observing_family);
		if (baseline == nullptr) {
			return;
		}
		const Dictionary report = baseline->report;
		REQUIRE(report.has("unconfirmed_census_witnesses"));
		const Array published = report["unconfirmed_census_witnesses"];
		// The document says it, not only the console of the run that wrote it: an artifact is the only
		// thing a consumer of another machine's run ever reads.
		REQUIRE_EQ(published.size(), baseline->unconfirmed_census_witnesses.size());
		for (int index = 0; index < published.size(); index++) {
			CHECK_EQ(String(published[index]), baseline->unconfirmed_census_witnesses[index]);
		}

		// Every census claim the report carries says which build compiles the witness it stands on, so an
		// unconfirmed claim is attributable without the catalog beside the report.
		const Dictionary census = report.get("census", Dictionary());
		const Array entries = census.get("entries", Array());
		REQUIRE_FALSE(entries.is_empty());
		int editor_only_claims = 0;
		for (int index = 0; index < entries.size(); index++) {
			const Dictionary witness = Dictionary(entries[index]).get("witness", Dictionary());
			REQUIRE(witness.has("build_configuration"));
			if (String(witness["build_configuration"]) == "editor") {
				editor_only_claims++;
			}
		}
		CHECK_GT(editor_only_claims, 0);
		// The member says what the build could not confirm, which two runs that observed the same product
		// still disagree about, so it may never be compared as evidence.
		CHECK(FSCompletenessRunner::non_evidence_report_members().has("unconfirmed_census_witnesses"));
		if (configuration_analyzer_warnings()) {
			// Every configuration this census declares is compiled into an editor build, and this is one.
			CHECK(published.is_empty());
		} else {
			// Every editor-only coverage claim is unconfirmed here, and the negative witnesses the
			// exemptions stand on are unconfirmed alongside them.
			CHECK_GE(published.size(), editor_only_claims);
			CHECK_FALSE(published.is_empty());
		}
	}

	TEST_CASE("TypeCompleteness Configuration reasons are a closed vocabulary") {
		HashSet<String> declared;
		for (const char *reason : FSCompletenessNotCoveredReason::ALL) {
			const String reason_id = reason;
			CAPTURE(reason_id);
			CHECK_FALSE(reason_id.is_empty());
			CHECK_FALSE(declared.has(reason_id));
			declared.insert(reason_id);
		}
		CHECK(declared.has(FSCompletenessNotCoveredReason::DIAGNOSTICS_UNAVAILABLE_IN_CONFIGURATION));

		// Nothing a report publishes may name a reason outside the vocabulary, whichever build published
		// it: a spelling a consumer has no branch for is an unread verdict.
		const FSCompletenessRunResult *baseline =
				FSCompletenessBaseline::shared_or_skip(configuration_warning_expecting_family);
		if (baseline == nullptr) {
			return;
		}
		const Array cases = Dictionary(baseline->report).get("cases", Array());
		for (int index = 0; index < cases.size(); index++) {
			const Dictionary case_record = cases[index];
			if (!case_record.has("not_covered_reason")) {
				continue;
			}
			CHECK(declared.has(String(case_record["not_covered_reason"])));
		}
		const Array exceptions = Dictionary(baseline->report).get("exceptions", Array());
		for (int index = 0; index < exceptions.size(); index++) {
			const Dictionary exception_record = exceptions[index];
			if (!exception_record.has("not_covered_reason")) {
				continue;
			}
			CHECK(declared.has(String(exception_record["not_covered_reason"])));
		}
	}
}

} // namespace FSTests
