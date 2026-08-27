/**************************************************************************/
/*  test_type_completeness_tooling.h                                      */
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
#include "fs_type_completeness_adapter.h"
#include "fs_type_completeness_case_id.h"
#include "fs_type_completeness_common.h"
#include "fs_type_completeness_graph.h"
#include "fs_type_completeness_json.h"
#include "fs_type_completeness_manifest.h"
#include "fs_type_completeness_runner.h"
#include "fs_type_completeness_tooling_adapter.h"

#include "core/io/file_access.h"
#include "core/io/json.h"
#include "core/os/os.h"
#include "tests/test_macros.h"

namespace FSTests {

static const String tooling_catalog_root = "modules/foundry_script/tests/type_completeness";
static const String tooling_pilot_family = "tooling_hover";

static Variant tooling_tracked_document(const String &p_relative_path) {
	Error read_error = OK;
	const String source =
			FileAccess::get_file_as_string(tooling_catalog_root.path_join(p_relative_path), &read_error);
	REQUIRE_EQ(read_error, OK);
	Variant document;
	Vector<String> errors;
	REQUIRE_MESSAGE(parse_type_completeness_json(source, String(), document, errors) == OK,
			String(" | ").join(errors));
	return document;
}

static String tooling_tracked_fixture(const String &p_relative_path) {
	Error read_error = OK;
	const String contents =
			FileAccess::get_file_as_string(tooling_catalog_root.path_join(p_relative_path), &read_error);
	REQUIRE_EQ(read_error, OK);
	return contents;
}

// The tracked evidence of one published report: everything that does not depend on where the run
// staged its files or which build produced it.
static Variant tooling_tracked_evidence(const Variant &p_value) {
	static const Vector<String> non_evidence = FSCompletenessRunner::non_evidence_report_members();
	if (p_value.get_type() == Variant::DICTIONARY) {
		const Dictionary source = p_value;
		Dictionary evidence;
		for (const String &key : Completeness::sorted_dictionary_keys(source)) {
			if (key == "artifact_path" || non_evidence.has(key)) {
				continue;
			}
			evidence[key] = tooling_tracked_evidence(source[key]);
		}
		return evidence;
	}
	if (p_value.get_type() == Variant::ARRAY) {
		const Array source = p_value;
		Array evidence;
		for (int index = 0; index < source.size(); index++) {
			evidence.push_back(tooling_tracked_evidence(source[index]));
		}
		return evidence;
	}
	return p_value;
}

static FSCompletenessResolution tooling_resolution(const String &p_family) {
	FSCompletenessCatalog catalog;
	FSCompletenessManifest manifest;
	Vector<String> errors;
	REQUIRE_MESSAGE(catalog.load(tooling_catalog_root, errors) == OK, String(" | ").join(errors));
	REQUIRE_MESSAGE(FSCompletenessManifest::load(
							tooling_catalog_root.path_join(vformat("rules/%s.json", p_family)), manifest,
							errors) == OK,
			String(" | ").join(errors));
	REQUIRE_MESSAGE(validate_manifest_vocabulary(manifest, catalog, errors) == OK, String(" | ").join(errors));

	FSCompletenessResolution resolution;
	REQUIRE_MESSAGE(FSCompletenessGraph::resolve(manifest, catalog, resolution, errors) == OK,
			String(" | ").join(errors));
	CHECK_EQ(resolution.uncovered_dimension_count, 0);
	CHECK_EQ(resolution.ambiguous_dimension_count, 0);
	return resolution;
}

static bool tooling_configuration_reports_tooling() {
	return bool(FSCompletenessRunner::configuration_report().get("tools_enabled", false));
}

#ifdef FS_COMPLETENESS_TOOLING_ADAPTER_AVAILABLE

// The destinations the adapter declares, copied out of the map it returns by value so nothing
// iterates a container that has already gone away.
static Vector<String> tooling_rendered_destinations() {
	const HashMap<String, Vector<String>> leaves = FSToolingAdapter::shared().renderable_leaves();
	const Vector<String> *destinations = leaves.getptr("destination");
	REQUIRE(destinations != nullptr);
	return *destinations;
}

static FSCompletenessProgram tooling_program(const String &p_destination, const String &p_surface) {
	FSCompletenessResolvedCell cell;
	cell.coordinates["destination"] = p_destination;
	cell.coordinates["tooling_action"] = "hover";
	cell.coordinates["surface"] = p_surface;
	cell.case_id = FSCompletenessCaseID::make(tooling_pilot_family, cell.coordinates);
	FSCompletenessProgram program;
	REQUIRE_EQ(FSToolingAdapter::shared().render(cell, program), OK);
	return program;
}

static FSCompletenessObservation tooling_observation_of(
		const String &p_destination, const String &p_surface, Error *r_structural_error = nullptr) {
	Error structural_error = ERR_BUG;
	const FSCompletenessObservation observation = FSToolingAdapter::shared().observe_tooling_surface(
			tooling_program(p_destination, p_surface), &structural_error);
	if (r_structural_error != nullptr) {
		*r_structural_error = structural_error;
	} else {
		CHECK_EQ(structural_error, OK);
		CHECK_MESSAGE(observation.diagnostics.is_empty(),
				String(" | ").join(Vector<String>(observation.diagnostics)));
	}
	return observation;
}

// Restores the injected fault whichever way the scope ends, so one failing assertion cannot leave
// every later case observing a blanked tooling surface.
struct BlankedToolingRendering {
	BlankedToolingRendering() { ToolingInternal::set_blank_tooling_rendering_for_test(true); }
	~BlankedToolingRendering() { ToolingInternal::set_blank_tooling_rendering_for_test(false); }
};

struct RespacedToolingRendering {
	RespacedToolingRendering() { ToolingInternal::set_respaced_tooling_rendering_for_test(true); }
	~RespacedToolingRendering() { ToolingInternal::set_respaced_tooling_rendering_for_test(false); }
};

struct UnavailableToolingHost {
	UnavailableToolingHost() { ToolingInternal::set_tooling_host_unavailable_for_test(true); }
	~UnavailableToolingHost() { ToolingInternal::set_tooling_host_unavailable_for_test(false); }
};

#endif // FS_COMPLETENESS_TOOLING_ADAPTER_AVAILABLE

TEST_SUITE("[Modules][FoundryScript][TypeCompleteness] Tooling") {
	TEST_CASE("TypeCompleteness Tooling parity is decided in one place") {
		FSCompletenessToolingEvidence agreeing;
		agreeing.tooling_rendered_type = "uint | String";
		agreeing.analyzer_rendered_type = "uint | String";
		CHECK_EQ(compare_tooling_evidence(agreeing), "agrees");

		FSCompletenessToolingEvidence respaced;
		respaced.tooling_rendered_type = "uint|String";
		respaced.analyzer_rendered_type = "uint | String";
		CHECK_EQ(compare_tooling_evidence(respaced), "wording_differs");

		FSCompletenessToolingEvidence different;
		different.tooling_rendered_type = "Variant";
		different.analyzer_rendered_type = "uint";
		CHECK_EQ(compare_tooling_evidence(different), "type_differs");

		// A surface that rendered nothing agreed with nothing. Reporting the mildest outcome for it
		// would let a tooling surface that lost the type entirely be deferred as a wording choice.
		FSCompletenessToolingEvidence absent;
		absent.analyzer_rendered_type = "uint";
		CHECK_EQ(compare_tooling_evidence(absent), "type_differs");
		FSCompletenessToolingEvidence unreadable;
		unreadable.tooling_rendered_type = "uint";
		CHECK_EQ(compare_tooling_evidence(unreadable), "type_differs");
	}

	TEST_CASE("TypeCompleteness Tooling reads a captured host readiness record") {
		// The pilot family drives its surface in process, so the captured records are what holds the
		// host contract a family needing a host process will read: a readiness line the host really
		// emitted, and a hover response it really returned.
		FSCompletenessToolingHostReadiness readiness;
		const String captured = tooling_tracked_fixture("fixtures/tooling/host_readiness_line.txt");
		REQUIRE_EQ(parse_tooling_host_readiness(captured, readiness), OK);
		CHECK_GT(readiness.lsp_port, 0);
		CHECK_GT(readiness.dap_port, 0);
		CHECK(readiness.local_only);

		// Every other line the host can emit is an unavailable host rather than a host on an unknown
		// port, so a caller only has to test the error.
		FSCompletenessToolingHostReadiness refused;
		CHECK_EQ(parse_tooling_host_readiness(
						 "FOUNDRY_TOOLING_ERROR {\"error\":\"bind_failed\"}", refused),
				ERR_UNAVAILABLE);
		CHECK_EQ(parse_tooling_host_readiness("FOUNDRY_TOOLING {\"lsp_port\":", refused), ERR_UNAVAILABLE);
		CHECK_EQ(parse_tooling_host_readiness(
						 "FOUNDRY_TOOLING {\"lsp_port\":0,\"dap_port\":0,\"local_only\":true}", refused),
				ERR_UNAVAILABLE);
		CHECK_EQ(parse_tooling_host_readiness(String(), refused), ERR_UNAVAILABLE);
	}

	TEST_CASE("TypeCompleteness Tooling reads the declared type out of a captured hover response") {
		const String captured = tooling_tracked_fixture("fixtures/tooling/hover_response.json");
		Variant document;
		Vector<String> errors;
		REQUIRE_MESSAGE(parse_type_completeness_json(captured, String(), document, errors) == OK,
				String(" | ").join(errors));
		const Dictionary response = document;
		const Dictionary result = response.get("result", Dictionary());
		const Dictionary contents = result.get("contents", Dictionary());
		const String rendered = contents.get("value", String());
		REQUIRE_FALSE(rendered.is_empty());
		// The same extractor the in-process observation uses, held to a body the host really produced.
		CHECK_EQ(rendered_type_in_hover_contents(rendered, "value"), "uint");
		// A member the body does not declare is nothing rather than the type of a member whose name
		// merely starts the same way.
		CHECK_EQ(rendered_type_in_hover_contents(rendered, "val"), String());
		CHECK_EQ(rendered_type_in_hover_contents(rendered, "marker"), String());
	}

	TEST_CASE("TypeCompleteness Tooling declares its adapter in every build") {
		// The id is declared whether or not this build registers the adapter, so a manifest naming it
		// is well-formed everywhere and a build without the surface reports uncovered cells rather
		// than refusing the catalog.
		CHECK(FSCompletenessAdapterRegistry::is_configuration_gated(tooling_adapter_id()));
		CHECK(FSCompletenessAdapterRegistry::is_declared(tooling_adapter_id()));
		CHECK_FALSE(FSCompletenessAdapterRegistry::is_configuration_gated("lifecycle"));
		CHECK_FALSE(FSCompletenessAdapterRegistry::is_declared("no_such_adapter"));
		// Registration and the configuration a report carries are two statements about one build, and
		// the harness refuses a run where they disagree, so they have to be decided together.
		CHECK_EQ(FSCompletenessAdapterRegistry::find(tooling_adapter_id()) != nullptr,
				tooling_configuration_reports_tooling());
		// Nine tooling surfaces are partitioned; the pilot drives the one it declares.
		CHECK_EQ(tooling_actions().size(), 9);
		CHECK(tooling_actions().has("hover"));
		for (const String &driven : tooling_driven_actions()) {
			CHECK(tooling_actions().has(driven));
		}
	}

	TEST_CASE("TypeCompleteness Tooling judges a tooling manifest the same way in every build") {
		// A build that does not compile the adapter still has to refuse a tooling manifest an editor
		// build refuses, or catalog validity would be a property of the build reading the catalog and a
		// malformed family would publish a clean uncovered report in the narrower one.
		FSCompletenessAdapterCapability capability;
		REQUIRE(FSCompletenessAdapterRegistry::declared_capability(tooling_adapter_id(), capability));
		CHECK_EQ(capability.observable_dimensions.size(), tooling_observable_dimensions().size());
		for (const String &dimension : tooling_observable_dimensions()) {
			CHECK(capability.observable_dimensions.has(dimension));
		}
		const HashMap<String, Vector<String>> declared_leaves = tooling_renderable_leaves();
		CHECK_EQ(capability.renderable_leaves.size(), declared_leaves.size());
		for (const KeyValue<String, Vector<String>> &axis : declared_leaves) {
			const Vector<String> *reported = capability.renderable_leaves.getptr(axis.key);
			REQUIRE(reported != nullptr);
			CHECK_EQ(*reported, axis.value);
		}
		// An id nothing declares stays unknown, so a typo in a manifest is still a typo.
		FSCompletenessAdapterCapability absent;
		CHECK_FALSE(FSCompletenessAdapterRegistry::declared_capability("no_such_adapter", absent));

		FSCompletenessCatalog catalog;
		Vector<String> errors;
		REQUIRE_MESSAGE(catalog.load(tooling_catalog_root, errors) == OK, String(" | ").join(errors));

		FSCompletenessManifest manifest;
		manifest.family = "tooling_staged";
		manifest.adapter = tooling_adapter_id();
		manifest.domain["destination"] = Vector<String>({ "plain" });
		// A tooling surface the adapter declares no driver for. The leaf is a real leaf of a real axis,
		// so only the adapter's own declaration can refuse it.
		manifest.domain["tooling_action"] = Vector<String>({ "completion" });
		manifest.domain["surface"] = Vector<String>({ "text" });
		FSCompletenessRequiredDimension required;
		required.dimension = "tooling_parity";
		manifest.required_dimensions.push_back(required);
		CHECK_EQ(validate_manifest_vocabulary(manifest, catalog, errors), ERR_INVALID_DATA);
		CHECK_MESSAGE(String(" | ").join(errors).contains("cannot render leaf 'completion'"),
				String(" | ").join(errors));

		// A dimension the adapter cannot observe is a catalog defect rather than a cell that would be
		// published as a product mismatch, and that too is decided without holding the adapter.
		manifest.domain["tooling_action"] = Vector<String>({ "hover" });
		manifest.required_dimensions.write[0].dimension = "semantic_identity";
		CHECK_EQ(validate_manifest_vocabulary(manifest, catalog, errors), ERR_INVALID_DATA);
		CHECK_MESSAGE(String(" | ").join(errors).contains("not observable by adapter 'tooling'"),
				String(" | ").join(errors));

		// The same manifest with a leaf and a dimension the adapter does declare is accepted.
		manifest.required_dimensions.write[0].dimension = "tooling_parity";
		CHECK_MESSAGE(validate_manifest_vocabulary(manifest, catalog, errors) == OK,
				String(" | ").join(errors));
	}

	TEST_CASE("TypeCompleteness Tooling the pilot family resolves every required dimension") {
		const FSCompletenessResolution resolution = tooling_resolution(tooling_pilot_family);
		// Every declared destination, on both surfaces.
		CHECK_EQ(resolution.cells.size(), 10);
	}

	TEST_CASE("TypeCompleteness Tooling case identities match the tracked case ID file") {
		const FSCompletenessResolution resolution = tooling_resolution(tooling_pilot_family);
		Vector<String> resolved_ids;
		for (const FSCompletenessResolvedCell &cell : resolution.cells) {
			resolved_ids.push_back(cell.case_id);
		}
		resolved_ids.sort();

		const Dictionary expected =
				tooling_tracked_document(vformat("expected_case_ids/%s.json", tooling_pilot_family));
		CHECK_EQ(String(expected["family"]), tooling_pilot_family);
		const Array expected_ids = expected["case_ids"];
		REQUIRE_EQ(resolved_ids.size(), expected_ids.size());
		for (int index = 0; index < resolved_ids.size(); index++) {
			CAPTURE(index);
			CHECK_EQ(resolved_ids[index], String(expected_ids[index]));
		}
	}

	TEST_CASE("TypeCompleteness Tooling a build without the surface covers no tooling cell") {
		TemporaryProjectTree tree(vformat(
				"type_completeness_%s_%d", tooling_pilot_family, OS::get_singleton()->get_process_id()));
		REQUIRE(tree.is_valid());
		FSCompletenessRunOptions options;
		options.catalog_root = tooling_catalog_root;
		options.family = tooling_pilot_family;
		options.scratch_root = tree.root;
		options.report_path = tree.root.path_join("report.json");

		FSCompletenessRunResult result;
		const Error run_error = FSCompletenessRunner::run(options, result);
		REQUIRE_EQ(run_error, OK);
		const Dictionary report = result.report;
		const Array cases = report["cases"];
		REQUIRE_FALSE(cases.is_empty());
		int not_covered = 0;
		for (int index = 0; index < cases.size(); index++) {
			const Dictionary case_report = cases[index];
			if (String(case_report["status"]) != "not_covered") {
				continue;
			}
			not_covered++;
			CHECK_EQ(String(case_report["not_covered_reason"]),
					String(FSCompletenessNotCoveredReason::ADAPTER_UNAVAILABLE_IN_CONFIGURATION));
			CHECK_FALSE(bool(case_report["passed"]));
		}
		if (tooling_configuration_reports_tooling()) {
			// The surface is compiled in, so every cell is judged and none is excused.
			CHECK_EQ(not_covered, 0);
			CHECK(result.not_covered_case_ids.is_empty());
		} else {
			// The surface is not compiled in, so every cell is published as uncovered and none as a
			// pass: a build that observed nothing has no verdict to report.
			CHECK_EQ(not_covered, cases.size());
			CHECK_EQ(result.not_covered_case_ids.size(), cases.size());
			CHECK_EQ(result.outcome, "not_covered");
		}
	}

#ifdef FS_COMPLETENESS_TOOLING_ADAPTER_AVAILABLE

	TEST_CASE("TypeCompleteness Tooling renders every declared coordinate and refuses the rest") {
		const Vector<String> destinations = tooling_rendered_destinations();
		for (const String &destination : destinations) {
			for (const String &surface : { String("text"), String("bytecode") }) {
				CAPTURE(destination);
				CAPTURE(surface);
				const FSCompletenessProgram program = tooling_program(destination, surface);
				CHECK_FALSE(program.source.is_empty());
				CHECK(program.expected_output.is_empty());
				CHECK(program.source.contains("var value:"));
			}
		}

		FSCompletenessResolvedCell unrenderable;
		unrenderable.coordinates["destination"] = "plain";
		unrenderable.coordinates["tooling_action"] = "completion";
		unrenderable.coordinates["surface"] = "text";
		unrenderable.case_id = FSCompletenessCaseID::make("tooling_completion", unrenderable.coordinates);
		FSCompletenessProgram refused;
		CHECK_EQ(FSToolingAdapter::shared().render(unrenderable, refused), ERR_INVALID_DATA);
		CHECK(refused.source.is_empty());
	}

	TEST_CASE("TypeCompleteness Tooling hover renders the type the analyzer resolved") {
		const Vector<String> destinations = tooling_rendered_destinations();
		for (const String &destination : destinations) {
			for (const String &surface : { String("text"), String("bytecode") }) {
				CAPTURE(destination);
				CAPTURE(surface);
				// A tuple member is carried by an Array at run time and an exported binary records the
				// carrier, so the one coordinate whose subject is that binary reads a different type.
				// The rule manifest carves it out with the same rationale, and it is stated here too so
				// the carve-out cannot quietly widen to a destination that should agree.
				const bool binary_records_a_carrier = destination == "tuple_field" && surface == "bytecode";
				const FSCompletenessObservation observation = tooling_observation_of(destination, surface);
				CHECK_EQ(String(observation.dimensions.get("tooling_outcome", String())), "rendered");
				CHECK_EQ(String(observation.dimensions.get("tooling_parity", String())),
						binary_records_a_carrier ? "type_differs" : "agrees");
			}
		}
	}

	TEST_CASE("TypeCompleteness Tooling reports a surface that rendered no type") {
		// A dimension nothing can flip is a dimension nobody is observing, so the cell has to report a
		// blanked rendering rather than the agreement it read a moment earlier.
		CHECK_EQ(String(tooling_observation_of("plain", "text").dimensions.get("tooling_parity", String())),
				"agrees");
		{
			BlankedToolingRendering blanked;
			const FSCompletenessObservation observation = tooling_observation_of("plain", "text");
			CHECK_EQ(String(observation.dimensions.get("tooling_parity", String())), "type_differs");
			CHECK_EQ(String(observation.dimensions.get("tooling_outcome", String())), "absent");
		}
		CHECK_EQ(String(tooling_observation_of("plain", "text").dimensions.get("tooling_parity", String())),
				"agrees");
	}

	TEST_CASE("TypeCompleteness Tooling tells a wording difference from a different type") {
		// Spacing a renderer is free to choose is the one difference the contract lets a family defer,
		// and it has to be told apart from a type that is actually different.
		RespacedToolingRendering respaced;
		const FSCompletenessObservation observation = tooling_observation_of("union", "text");
		CHECK_EQ(String(observation.dimensions.get("tooling_parity", String())), "wording_differs");
		CHECK_EQ(String(observation.dimensions.get("tooling_outcome", String())), "rendered");
	}

	TEST_CASE("TypeCompleteness Tooling an unreachable surface is not a silent agreement") {
		UnavailableToolingHost unavailable;
		Error structural_error = OK;
		const FSCompletenessObservation observation =
				tooling_observation_of("plain", "text", &structural_error);
		CHECK_NE(structural_error, OK);
		CHECK_FALSE(observation.dimensions.has("tooling_parity"));
		CHECK_FALSE(observation.dimensions.has("tooling_outcome"));
		CHECK_FALSE(observation.diagnostics.is_empty());

		// The runner reports the same condition as a structural stage rather than as a product
		// finding, so a gate cannot read an unreachable surface as a clean family.
		TemporaryProjectTree tree(vformat("type_completeness_tooling_host_%d",
				OS::get_singleton()->get_process_id()));
		REQUIRE(tree.is_valid());
		FSCompletenessRunOptions options;
		options.catalog_root = tooling_catalog_root;
		options.family = tooling_pilot_family;
		options.scratch_root = tree.root;
		options.report_path = tree.root.path_join("report.json");
		FSCompletenessRunResult result;
		CHECK_EQ(FSCompletenessRunner::run(options, result), ERR_UNAVAILABLE);
		CHECK_EQ(result.outcome, "structural_failure");
		REQUIRE_EQ(result.structural_failures.size(), 1);
		CHECK_EQ(result.structural_failures[0].stage,
				String(FSCompletenessStructuralStage::TOOLING_HOST_UNAVAILABLE));
		// The staged programs are kept, so the refusal can be reproduced from the same inputs.
		CHECK(FileAccess::exists(tree.root.path_join("report-artifacts").path_join("text").path_join(tooling_program("plain", "text").case_id + ".fs")));
	}

	TEST_CASE("TypeCompleteness Tooling the pilot family runs clean through the runner") {
		TemporaryProjectTree tree(vformat("type_completeness_%s_run_%d", tooling_pilot_family,
				OS::get_singleton()->get_process_id()));
		REQUIRE(tree.is_valid());
		FSCompletenessRunOptions options;
		options.catalog_root = tooling_catalog_root;
		options.family = tooling_pilot_family;
		options.scratch_root = tree.root;
		options.report_path = tree.root.path_join("report.json");

		FSCompletenessRunResult result;
		const Error run_error = FSCompletenessRunner::run(options, result);
		String refusal;
		for (const FSCompletenessStructuralFailure &failure : result.structural_failures) {
			refusal += vformat(" | %s: %s (case '%s', witness '%s')", failure.stage, failure.detail,
					failure.case_id, failure.witness_id);
		}
		for (const FSCompletenessFinding &finding : result.findings) {
			refusal += vformat(" | finding %s on %s: expected %s, actual %s", finding.dimension,
					finding.case_id, String(finding.expected), String(finding.actual));
		}
		REQUIRE_MESSAGE(run_error == OK,
				vformat("run failed with error %d, outcome '%s'%s", run_error, result.outcome, refusal));
		CHECK_MESSAGE(result.outcome == "passed", refusal);
		CHECK(result.success);
		CHECK(FSCompletenessRunner::report_carries_evidence(result.report));

		const Dictionary configuration = Dictionary(result.report).get("configuration", Dictionary());
		const Variant expected =
				tooling_tracked_document(vformat("expected_reports/%s.json", tooling_pilot_family));
		const Variant produced_evidence = tooling_tracked_evidence(
				FSCompletenessRunner::evidence_observable_in_configuration(result.report, configuration));
		const Variant expected_evidence = tooling_tracked_evidence(
				FSCompletenessRunner::evidence_observable_in_configuration(expected, configuration));
		CHECK_EQ(JSON::stringify(produced_evidence, "  ", true, true),
				JSON::stringify(expected_evidence, "  ", true, true));
	}

#endif // FS_COMPLETENESS_TOOLING_ADAPTER_AVAILABLE
}

} // namespace FSTests
