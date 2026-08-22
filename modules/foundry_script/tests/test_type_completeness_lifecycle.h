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

// The transition hands back whatever the subsystem already held for the identity instead of doing the
// work the family names. Restored whichever way the scope ends.
struct TransitionInvalidationSkipped {
	TransitionInvalidationSkipped() { LifecycleInternal::set_skip_transition_invalidation_for_test(true); }
	~TransitionInvalidationSkipped() {
		LifecycleInternal::set_skip_transition_invalidation_for_test(false);
	}
};

// The dimension one cell observed, so a fault can be stated as the value it changed.
static String lifecycle_identity_of(const String &p_family, const String &p_destination,
		const String &p_state, const String &p_surface) {
	const FSCompletenessProgram program =
			lifecycle_program(p_family, p_destination, p_state, p_surface);
	Error structural_error = ERR_BUG;
	const FSCompletenessObservation observation =
			FSLifecycleAdapter::shared().observe_transition(program, p_family, &structural_error);
	CHECK_EQ(structural_error, OK);
	CHECK_MESSAGE(observation.diagnostics.is_empty(),
			String(" | ").join(Vector<String>(observation.diagnostics)));
	return observation.dimensions.get("semantic_identity", String());
}

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
							FSLifecycleAdapter::shared().observe_transition(program, "lifecycle_bytecode_export_load", &structural_error);
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
								 .observe_transition(stale, "lifecycle_bytecode_export_load", &structural_error)
								 .dimensions.get("semantic_identity", String())),
				"preserved");
		REQUIRE_EQ(structural_error, OK);

		ArtifactLoadedFromSource from_source;
		const FSCompletenessObservation reaching_stale =
				FSLifecycleAdapter::shared().observe_transition(stale, "lifecycle_bytecode_export_load", &structural_error);
		CHECK_EQ(structural_error, OK);
		CHECK_EQ(String(reaching_stale.dimensions.get("semantic_identity", String())), "projected");
		// A stage whose source nobody revised cannot tell the two loaders apart, which is why the
		// stale stage exists rather than the clean one carrying this evidence.
		const FSCompletenessObservation reaching_clean =
				FSLifecycleAdapter::shared().observe_transition(clean, "lifecycle_bytecode_export_load", &structural_error);
		CHECK_EQ(structural_error, OK);
		CHECK_EQ(String(reaching_clean.dimensions.get("semantic_identity", String())), "preserved");
	}

	TEST_CASE("TypeCompleteness Lifecycle re-deriving families read the identity, not what they held") {
		// A reload, a cache replacement and a reinitialization all answer the same question - what does
		// this identity declare now - so on the stale stage, where the identity was revised after the
		// subject was compiled, each has to carry the revision. A transition that handed back what the
		// subsystem already had would carry the original instead, and that is the fault seam.
		for (const String &family : { String("lifecycle_reload"), String("lifecycle_cache_replacement"),
					 String("lifecycle_shutdown_reinitialization") }) {
			CAPTURE(family);
			CHECK_EQ(lifecycle_identity_of(family, "plain", "stale", "text"), "projected");
			CHECK_EQ(lifecycle_identity_of(family, "plain", "clean", "text"), "preserved");
			{
				TransitionInvalidationSkipped reused;
				CHECK_EQ(lifecycle_identity_of(family, "plain", "stale", "text"), "preserved");
				// A stage whose identity nobody revised cannot tell the two apart, which is why the
				// stale stage is the one carrying this evidence.
				CHECK_EQ(lifecycle_identity_of(family, "plain", "clean", "text"), "preserved");
			}
			CHECK_EQ(lifecycle_identity_of(family, "plain", "stale", "text"), "projected");
		}
	}

	TEST_CASE("TypeCompleteness Lifecycle reports a refused transition rather than a carried type") {
		// Damaging what each family's transition produces has to be visible in every family, or the
		// cell is reporting a canned outcome rather than what its subsystem did.
		for (const String &family : FSLifecycleAdapter::families()) {
			CAPTURE(family);
			const String carried = lifecycle_identity_of(family, "plain", "clean", "text");
			CHECK_EQ(carried, "preserved");
			{
				CorruptedTransitionArtifact corrupted;
				const String damaged = lifecycle_identity_of(family, "plain", "clean", "text");
				CHECK_NE(damaged, carried);
			}
			CHECK_EQ(lifecycle_identity_of(family, "plain", "clean", "text"), carried);
		}
	}

	TEST_CASE("TypeCompleteness Lifecycle a cache replacement has to replace the entry") {
		// Reading the type back through the cache is only evidence of a replacement if the entry that
		// comes back is not the one that went in. Handing back the entry the replacement was supposed to
		// retire is the regression this family exists to catch, so it reports nothing carried rather
		// than the type it would have read out of the stale object.
		CHECK_EQ(lifecycle_identity_of("lifecycle_cache_replacement", "plain", "clean", "text"),
				"preserved");
		{
			TransitionInvalidationSkipped reused;
			CHECK_EQ(lifecycle_identity_of("lifecycle_cache_replacement", "plain", "stale", "text"),
					"preserved");
		}
		CHECK_EQ(lifecycle_identity_of("lifecycle_cache_replacement", "plain", "stale", "text"),
				"projected");
	}

	TEST_CASE("TypeCompleteness Lifecycle an incremental stage runs the whole transition again") {
		// Applying the transition to its own output has to mean the whole transition, including whatever
		// the family does before re-deriving. A second pass that skipped the language cycle would report
		// one shutdown followed by two recompilations as if the subsystem had been taken down twice, and
		// a defect that only appears on the second one would read as preserved.
		const uint64_t before_clean = LifecycleInternal::language_cycles_for_test();
		CHECK_EQ(lifecycle_identity_of("lifecycle_shutdown_reinitialization", "plain", "clean", "text"),
				"preserved");
		const uint64_t clean_cycles = LifecycleInternal::language_cycles_for_test() - before_clean;
		CHECK_EQ(lifecycle_identity_of("lifecycle_shutdown_reinitialization", "plain", "incremental",
						 "text"),
				"preserved");
		const uint64_t incremental_cycles =
				LifecycleInternal::language_cycles_for_test() - before_clean - clean_cycles;

		CHECK_EQ(clean_cycles, uint64_t(1));
		CHECK_EQ(incremental_cycles, uint64_t(2));
		// The families that re-derive without taking the language down never ask for a cycle at all, so
		// the count belongs to the one family whose transition is the cycle.
		const uint64_t before_reload = LifecycleInternal::language_cycles_for_test();
		CHECK_EQ(lifecycle_identity_of("lifecycle_reload", "plain", "incremental", "text"), "preserved");
		CHECK_EQ(LifecycleInternal::language_cycles_for_test(), before_reload);
	}

	TEST_CASE("TypeCompleteness Lifecycle reflection projection differs by what the surface can spell") {
		// The reflection surface describes a member with a Variant type, a class name and a hint, so a
		// declared type it cannot spell reaches it projected or not at all. These are the readings the
		// surface actually produced, not a shape assumed for it.
		CHECK_EQ(lifecycle_identity_of("lifecycle_proxy_reflection", "plain", "clean", "text"), "preserved");
		CHECK_EQ(lifecycle_identity_of("lifecycle_proxy_reflection", "union", "clean", "text"), "erased");
		CHECK_EQ(lifecycle_identity_of("lifecycle_proxy_reflection", "optional", "clean", "text"),
				"projected");
		// The bytecode writer carries all three, so the difference is the surface rather than the type.
		CHECK_EQ(lifecycle_identity_of("lifecycle_bytecode_export_load", "union", "clean", "text"),
				"preserved");
		CHECK_EQ(lifecycle_identity_of("lifecycle_bytecode_export_load", "optional", "clean", "text"),
				"preserved");
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

			// The tracked document was captured on one configuration. Narrowing both sides to what this
			// build could have observed is the identity on that configuration, so this stays the same
			// byte-identity comparison there, and compares observations rather than configurations
			// anywhere else.
			const Dictionary configuration = Dictionary(result.report).get("configuration", Dictionary());
			const Variant expected =
					lifecycle_tracked_document(vformat("expected_reports/%s.json", family));
			const Variant produced_evidence = lifecycle_tracked_evidence(
					FSCompletenessRunner::evidence_observable_in_configuration(result.report, configuration));
			const Variant expected_evidence = lifecycle_tracked_evidence(
					FSCompletenessRunner::evidence_observable_in_configuration(expected, configuration));
			CHECK_EQ(JSON::stringify(produced_evidence, "  ", true, true),
					JSON::stringify(expected_evidence, "  ", true, true));
		}
	}
}

} // namespace FSTests
