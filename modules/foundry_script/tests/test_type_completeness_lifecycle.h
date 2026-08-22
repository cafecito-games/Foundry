/**************************************************************************/
/*  test_type_completeness_lifecycle.h                                    */
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
#include "fs_type_completeness_graph.h"
#include "fs_type_completeness_json.h"
#include "fs_type_completeness_lifecycle_adapter.h"
#include "fs_type_completeness_manifest.h"
#include "fs_type_completeness_runner.h"

#include "core/io/file_access.h"
#include "core/io/json.h"
#include "core/os/os.h"
#include "tests/test_macros.h"

namespace FSTests {

static const String lifecycle_catalog_root = "modules/foundry_script/tests/type_completeness";

static FSCompletenessProgram lifecycle_program(
		const String &p_family, const String &p_destination, const String &p_state, const String &p_surface) {
	FSCompletenessResolvedCell cell;
	cell.coordinates["destination"] = p_destination;
	cell.coordinates["lifecycle_state"] = p_state;
	cell.coordinates["surface"] = p_surface;
	cell.case_id = FSCompletenessCaseID::make(p_family, cell.coordinates);
	FSCompletenessProgram program;
	REQUIRE_EQ(FSLifecycleAdapter::shared().render(cell, program), OK);
	return program;
}

// Everything below the guard exercises a transition the editor build is the only one that carries,
// so it is compiled there only; the other branch asserts that the adapter refuses rather than
// reporting a type that survived a transition this build cannot perform.
#ifdef TOOLS_ENABLED

// Families under `rules/` whose manifest names the lifecycle adapter, read from the rule directory
// rather than from a list in a test: the catalog is the source of truth for which families exist, so
// a family added without its fixtures fails here instead of going unnoticed.
static Vector<String> lifecycle_rule_families() {
	Vector<String> files;
	Vector<Completeness::JsonDirectoryError> directory_errors;
	Completeness::JsonDirectoryPolicy policy;
	REQUIRE_EQ(Completeness::enumerate_json_directory(
					   lifecycle_catalog_root.path_join("rules"), policy, files, directory_errors),
			OK);
	Vector<String> families;
	for (const String &file : files) {
		FSCompletenessManifest manifest;
		Vector<String> errors;
		REQUIRE_MESSAGE(FSCompletenessManifest::load(file, manifest, errors) == OK, String(" | ").join(errors));
		if (manifest.adapter == FSLifecycleAdapter::shared().id()) {
			families.push_back(manifest.family);
		}
	}
	families.sort();
	return families;
}

static FSCompletenessResolution lifecycle_resolution(const String &p_family) {
	FSCompletenessCatalog catalog;
	FSCompletenessManifest manifest;
	Vector<String> errors;
	REQUIRE_MESSAGE(catalog.load(lifecycle_catalog_root, errors) == OK, String(" | ").join(errors));
	REQUIRE_MESSAGE(FSCompletenessManifest::load(
							lifecycle_catalog_root.path_join(vformat("rules/%s.json", p_family)), manifest,
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

// The tracked evidence of one published report: everything that does not depend on where the run
// staged its files or which build produced it. The runner owns the member list, so this and the other
// families' reductions cannot disagree about what counts as evidence.
static Variant lifecycle_tracked_evidence(const Variant &p_value) {
	static const Vector<String> non_evidence = FSCompletenessRunner::non_evidence_report_members();
	if (p_value.get_type() == Variant::DICTIONARY) {
		const Dictionary source = p_value;
		Dictionary evidence;
		for (const String &key : Completeness::sorted_dictionary_keys(source)) {
			if (key == "artifact_path" || non_evidence.has(key)) {
				continue;
			}
			evidence[key] = lifecycle_tracked_evidence(source[key]);
		}
		return evidence;
	}
	if (p_value.get_type() == Variant::ARRAY) {
		const Array source = p_value;
		Array evidence;
		for (int index = 0; index < source.size(); index++) {
			evidence.push_back(lifecycle_tracked_evidence(source[index]));
		}
		return evidence;
	}
	return p_value;
}

static Variant lifecycle_tracked_document(const String &p_relative_path) {
	Error read_error = OK;
	const String source = FileAccess::get_file_as_string(
			lifecycle_catalog_root.path_join(p_relative_path), &read_error);
	REQUIRE_EQ(read_error, OK);
	Variant document;
	Vector<String> errors;
	REQUIRE_MESSAGE(parse_type_completeness_json(source, String(), document, errors) == OK,
			String(" | ").join(errors));
	return document;
}

// Restores the injected fault whichever way the scope ends, so one failing assertion cannot leave
// every later case observing a corrupted transition.
struct CorruptedTransitionArtifact {
	CorruptedTransitionArtifact() { LifecycleInternal::set_corrupt_transition_artifact_for_test(true); }
	~CorruptedTransitionArtifact() { LifecycleInternal::set_corrupt_transition_artifact_for_test(false); }
};

struct ArtifactLoadedFromSource {
	ArtifactLoadedFromSource() {
		LifecycleInternal::set_load_transition_artifact_from_source_for_test(true);
	}
	~ArtifactLoadedFromSource() {
		LifecycleInternal::set_load_transition_artifact_from_source_for_test(false);
	}
};

TEST_SUITE("[Modules][FoundryScript][TypeCompleteness] Lifecycle") {
	TEST_CASE("TypeCompleteness Lifecycle renders every declared coordinate and refuses the rest") {
		for (const String &destination : lifecycle_destinations()) {
			for (const String &state : lifecycle_states()) {
				CAPTURE(destination);
				CAPTURE(state);
				const FSCompletenessProgram program = lifecycle_program(
						"lifecycle_bytecode_export_load", destination, state, "text");
				CHECK_FALSE(program.source.is_empty());
				CHECK(program.expected_output.is_empty());
				// The reordered stage declares the same members in the other order, so the difference
				// between the two sources is a position rather than a member set.
				CHECK(program.source.contains("var marker: int = 0"));
				CHECK(program.source.contains("var value:"));
				const int marker_at = program.source.find("var marker");
				const int value_at = program.source.find("var value");
				CHECK_EQ(state == "reordered", marker_at < value_at);
			}
		}

		FSCompletenessResolvedCell unrenderable;
		unrenderable.coordinates["destination"] = "trait";
		unrenderable.coordinates["lifecycle_state"] = "clean";
		unrenderable.coordinates["surface"] = "text";
		unrenderable.case_id =
				FSCompletenessCaseID::make("lifecycle_bytecode_export_load", unrenderable.coordinates);
		FSCompletenessProgram refused;
		CHECK_EQ(FSLifecycleAdapter::shared().render(unrenderable, refused), ERR_INVALID_DATA);
		CHECK(refused.source.is_empty());
	}

	TEST_CASE("TypeCompleteness Lifecycle observes the carried type off the transition's own artifact") {
		for (const String &destination : lifecycle_destinations()) {
			for (const String &state : lifecycle_states()) {
				for (const String &surface : { String("text"), String("bytecode") }) {
					CAPTURE(destination);
					CAPTURE(state);
					CAPTURE(surface);
					const FSCompletenessProgram program = lifecycle_program(
							"lifecycle_bytecode_export_load", destination, state, surface);
					Error structural_error = ERR_BUG;
					const FSCompletenessObservation observation =
							FSLifecycleAdapter::shared().observe_transition(program, &structural_error);
					CHECK_EQ(structural_error, OK);
					CHECK_MESSAGE(observation.diagnostics.is_empty(),
							String(" | ").join(Vector<String>(observation.diagnostics)));
					CHECK_EQ(String(observation.dimensions.get("semantic_identity", String())), "preserved");
					CHECK_EQ(String(observation.dimensions.get("transition_outcome", String())),
							state == "failure_recovery" ? "recovered" : "completed");
				}
			}
		}
	}

	TEST_CASE("TypeCompleteness Lifecycle reports a refused transition rather than a preserved type") {
		// A dimension nothing can flip is a dimension nobody is observing. Corrupting the artifact the
		// transition consumes is a fault in the transition, not in the reading of it.
		const FSCompletenessProgram program =
				lifecycle_program("lifecycle_bytecode_export_load", "union", "clean", "text");
		{
			CorruptedTransitionArtifact corrupted;
			Error structural_error = ERR_BUG;
			const FSCompletenessObservation observation =
					FSLifecycleAdapter::shared().observe_transition(program, &structural_error);
			CHECK_EQ(structural_error, OK);
			CHECK_EQ(String(observation.dimensions.get("semantic_identity", String())), "rejected");
			CHECK_EQ(String(observation.dimensions.get("transition_outcome", String())), "refused");
		}
		Error structural_error = ERR_BUG;
		const FSCompletenessObservation restored =
				FSLifecycleAdapter::shared().observe_transition(program, &structural_error);
		CHECK_EQ(structural_error, OK);
		CHECK_EQ(String(restored.dimensions.get("semantic_identity", String())), "preserved");
	}

	TEST_CASE("TypeCompleteness Lifecycle stale stage catches a loader that reaches back to the source") {
		// The stale stage revises the source behind the very identity the artifact recorded. A loader
		// that resolved the serialized type against that source instead of against its own bytes would
		// hand back the revision, and this is the stage that has to see it.
		const FSCompletenessProgram stale =
				lifecycle_program("lifecycle_bytecode_export_load", "plain", "stale", "text");
		const FSCompletenessProgram clean =
				lifecycle_program("lifecycle_bytecode_export_load", "plain", "clean", "text");
		Error structural_error = ERR_BUG;
		CHECK_EQ(String(FSLifecycleAdapter::shared()
								 .observe_transition(stale, &structural_error)
								 .dimensions.get("semantic_identity", String())),
				"preserved");
		REQUIRE_EQ(structural_error, OK);

		ArtifactLoadedFromSource from_source;
		const FSCompletenessObservation reaching_stale =
				FSLifecycleAdapter::shared().observe_transition(stale, &structural_error);
		CHECK_EQ(structural_error, OK);
		CHECK_EQ(String(reaching_stale.dimensions.get("semantic_identity", String())), "projected");
		// A stage whose source nobody revised cannot tell the two loaders apart, which is why the
		// stale stage exists rather than the clean one carrying this evidence.
		const FSCompletenessObservation reaching_clean =
				FSLifecycleAdapter::shared().observe_transition(clean, &structural_error);
		CHECK_EQ(structural_error, OK);
		CHECK_EQ(String(reaching_clean.dimensions.get("semantic_identity", String())), "preserved");
	}

	TEST_CASE("TypeCompleteness Lifecycle families resolve every required dimension") {
		const Vector<String> families = lifecycle_rule_families();
		REQUIRE_FALSE(families.is_empty());
		for (const String &family : families) {
			CAPTURE(family);
			const FSCompletenessResolution resolution = lifecycle_resolution(family);
			CHECK_FALSE(resolution.cells.is_empty());
			// Every leaf of every declared axis, on both surfaces.
			CHECK_EQ(resolution.cells.size(),
					lifecycle_destinations().size() * lifecycle_states().size() * 2);
		}
	}

	TEST_CASE("TypeCompleteness Lifecycle case identities match the tracked case ID files") {
		for (const String &family : lifecycle_rule_families()) {
			CAPTURE(family);
			const FSCompletenessResolution resolution = lifecycle_resolution(family);
			Vector<String> resolved_ids;
			for (const FSCompletenessResolvedCell &cell : resolution.cells) {
				resolved_ids.push_back(cell.case_id);
			}
			resolved_ids.sort();

			const Dictionary expected =
					lifecycle_tracked_document(vformat("expected_case_ids/%s.json", family));
			CHECK_EQ(String(expected["family"]), family);
			const Array expected_ids = expected["case_ids"];
			REQUIRE_EQ(resolved_ids.size(), expected_ids.size());
			for (int index = 0; index < resolved_ids.size(); index++) {
				CAPTURE(index);
				CHECK_EQ(resolved_ids[index], String(expected_ids[index]));
			}
		}
	}

	TEST_CASE("TypeCompleteness Lifecycle families run clean through the runner") {
		for (const String &family : lifecycle_rule_families()) {
			CAPTURE(family);
			TemporaryProjectTree tree(
					vformat("type_completeness_%s_%d", family, OS::get_singleton()->get_process_id()));
			REQUIRE(tree.is_valid());
			FSCompletenessRunOptions options;
			options.catalog_root = lifecycle_catalog_root;
			options.family = family;
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
			CHECK_EQ(result.outcome, "passed");
			CHECK(result.success);
			CHECK(FSCompletenessRunner::report_carries_evidence(result.report));

			// The family declares no carve-out: every stage of the transition is expected to carry the
			// declared type, and the one stage that reports a different transition outcome reports it
			// from its relation rather than from an exception to one.
			const Array exceptions = result.report["exceptions"];
			CHECK(exceptions.is_empty());

			const Variant expected =
					lifecycle_tracked_document(vformat("expected_reports/%s.json", family));
			const Variant produced_evidence = lifecycle_tracked_evidence(result.report);
			const Variant expected_evidence = lifecycle_tracked_evidence(expected);
			CHECK_EQ(JSON::stringify(produced_evidence, "  ", true, true),
					JSON::stringify(expected_evidence, "  ", true, true));
		}
	}
}

#else // No editor tooling.

TEST_SUITE("[Modules][FoundryScript][TypeCompleteness] Lifecycle") {
	TEST_CASE("TypeCompleteness Lifecycle refuses a transition this build does not carry") {
		// The bytecode export the family carries a declared type through is compiled into editor builds
		// only. Reporting the absence structurally is the only honest reading: a build with no transition
		// to observe has not observed a type surviving one.
		const FSCompletenessProgram program =
				lifecycle_program("lifecycle_bytecode_export_load", "plain", "clean", "text");
		Error structural_error = OK;
		const FSCompletenessObservation observation =
				FSLifecycleAdapter::shared().observe_transition(program, &structural_error);
		CHECK_EQ(structural_error, ERR_UNAVAILABLE);
		CHECK_FALSE(observation.diagnostics.is_empty());
		CHECK_FALSE(observation.dimensions.has("semantic_identity"));
		CHECK_FALSE(observation.dimensions.has("transition_outcome"));
	}
}

#endif // TOOLS_ENABLED

} // namespace FSTests
