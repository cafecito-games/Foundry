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

// The dimensions one cell observed, so a fault can be stated as the values it changed.
static FSCompletenessObservation lifecycle_observation_of(const String &p_family,
		const String &p_destination, const String &p_state, const String &p_surface) {
	const FSCompletenessProgram program =
			lifecycle_program(p_family, p_destination, p_state, p_surface);
	Error structural_error = ERR_BUG;
	const FSCompletenessObservation observation =
			FSLifecycleAdapter::shared().observe_transition(program, p_family, &structural_error);
	CHECK_EQ(structural_error, OK);
	CHECK_MESSAGE(observation.diagnostics.is_empty(),
			String(" | ").join(Vector<String>(observation.diagnostics)));
	return observation;
}

static String lifecycle_outcome_of(const String &p_family, const String &p_destination,
		const String &p_state, const String &p_surface) {
	return lifecycle_observation_of(p_family, p_destination, p_state, p_surface)
			.dimensions.get("transition_outcome", String());
}

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

// Damages the artifact a bytecode-surface cell restores its subject from, and nothing else.
struct CorruptedRestoredSubject {
	CorruptedRestoredSubject() { LifecycleInternal::set_corrupt_restored_subject_for_test(true); }
	~CorruptedRestoredSubject() { LifecycleInternal::set_corrupt_restored_subject_for_test(false); }
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

	TEST_CASE("TypeCompleteness Lifecycle a fault in one surface's subject cannot reach the other") {
		// Each cell selects the artifact under test once, from its surface: the script the front-end
		// compiled, or a compiled binary restored from that script's export. Damaging what the binary is
		// restored from has to stop the bytecode cell and leave the text cell of the same family reading
		// exactly what it read before, or the two surfaces are not measuring two different objects.
		for (const String &family : FSLifecycleAdapter::families()) {
			CAPTURE(family);
			const String text_reading = lifecycle_identity_of(family, "plain", "clean", "text");
			const String bytecode_reading = lifecycle_identity_of(family, "plain", "clean", "bytecode");
			CHECK_EQ(text_reading, bytecode_reading);
			{
				CorruptedRestoredSubject damaged;
				const FSCompletenessProgram bytecode_program =
						lifecycle_program(family, "plain", "clean", "bytecode");
				Error structural_error = OK;
				const FSCompletenessObservation refused = FSLifecycleAdapter::shared().observe_transition(
						bytecode_program, family, &structural_error);
				// The cell could not select its subject, which is a defect in the harness rather than a
				// reading about the product, so it publishes no dimension at all.
				CHECK_NE(structural_error, OK);
				CHECK_FALSE(refused.dimensions.has("semantic_identity"));
				CHECK_FALSE(refused.dimensions.has("transition_outcome"));
				CHECK_EQ(lifecycle_identity_of(family, "plain", "clean", "text"), text_reading);
			}
			CHECK_EQ(lifecycle_identity_of(family, "plain", "clean", "bytecode"), bytecode_reading);
		}
	}

	TEST_CASE("TypeCompleteness Lifecycle a synthetic scope holds several files in one identity space") {
		// A dependent and its dependency have to share a tree for `preload` to resolve between them, and
		// one lock so a two-file program is still serialized against every other synthetic source.
		Vector<DestinationWrapperInternal::SyntheticSourceFile> files;
		files.push_back({ "dependent.fs", "const Dependency := preload(\"dependency.fs\")\n" });
		files.push_back({ "dependency.fs", "class Carrier:\n\tvar value: uint = 0U\n" });
		DestinationWrapperInternal::SyntheticSourceScope scope(files);
		REQUIRE(scope.is_available());
		// The identity the scope stands for is its first file, and each file is reachable by name.
		CHECK_EQ(scope.get_path(), scope.get_path_for("dependent.fs"));
		CHECK_NE(scope.get_path_for("dependency.fs"), String());
		CHECK_EQ(scope.get_path_for("dependency.fs").get_base_dir(), scope.get_path().get_base_dir());
		CHECK(FileAccess::exists(scope.get_path()));
		CHECK(FileAccess::exists(scope.get_path_for("dependency.fs")));
		// A file the scope does not hold is nothing rather than a path that does not exist.
		CHECK_EQ(scope.get_path_for("absent.fs"), String());
	}

	TEST_CASE("TypeCompleteness Lifecycle a dependent follows the dependency it was invalidated for") {
		// The dependent never names the carried type: it holds the dependency's class, and what the cell
		// reports is the dependent's own view of that class's member. Revising the dependency has to
		// reach the dependent through the closure the product computes for that file, and a transition
		// that invalidated only the dependency leaves the dependent reporting the view it already had.
		CHECK_EQ(lifecycle_identity_of("lifecycle_dependency_invalidation", "plain", "clean", "text"),
				"preserved");
		CHECK_EQ(lifecycle_identity_of("lifecycle_dependency_invalidation", "plain", "stale", "text"),
				"projected");
		{
			TransitionInvalidationSkipped reused;
			CHECK_EQ(lifecycle_identity_of("lifecycle_dependency_invalidation", "plain", "stale", "text"),
					"preserved");
			// A stage that revised nothing cannot tell the two apart, which is why the stale stage is
			// the one carrying this evidence.
			CHECK_EQ(lifecycle_identity_of("lifecycle_dependency_invalidation", "plain", "clean", "text"),
					"preserved");
		}
		CHECK_EQ(lifecycle_identity_of("lifecycle_dependency_invalidation", "plain", "stale", "text"),
				"projected");
	}

	TEST_CASE("TypeCompleteness Lifecycle a loaded binary is not re-read when its identity reloads") {
		// On the bytecode surface the identity under test is a compiled binary the cache loads in its
		// own right, so this is the reload of a binary rather than of a source file. An exported binary
		// is immutable and an already loaded one is never re-read, so reloading the identity carries the
		// type the binary was built with even after the artifact behind it was replaced - and a damaged
		// artifact reads the same as a healthy one, because neither is read at all.
		CHECK_EQ(lifecycle_identity_of("lifecycle_reload", "plain", "stale", "text"), "projected");
		CHECK_EQ(lifecycle_identity_of("lifecycle_reload", "plain", "stale", "bytecode"), "preserved");
		CHECK_EQ(lifecycle_outcome_of("lifecycle_reload", "plain", "failure_recovery", "text"), "recovered");
		CHECK_EQ(lifecycle_outcome_of("lifecycle_reload", "plain", "failure_recovery", "bytecode"),
				"indistinguishable");
		// The families that retire what stands for the identity do read the replacement, on either
		// surface, so the difference above is the reload rather than the surface.
		CHECK_EQ(lifecycle_identity_of("lifecycle_cache_replacement", "plain", "stale", "bytecode"),
				"projected");
		CHECK_EQ(lifecycle_identity_of("lifecycle_shutdown_reinitialization", "plain", "stale", "bytecode"),
				"projected");
	}

	TEST_CASE("TypeCompleteness Lifecycle a recovery is measured against the healthy reading") {
		// A damaged attempt is only evidence of a recovery if it reads differently from a healthy one.
		// The reflection surface spells a union member as an untyped slot, which is also what it spells
		// when nothing reached it, so that cell recovers from nothing it can observe and says so.
		CHECK_EQ(lifecycle_outcome_of("lifecycle_proxy_reflection", "union", "failure_recovery", "text"),
				"indistinguishable");
		// The same stage on a destination the surface can spell does observe the difference, so the
		// verdict is a property of what the surface carries rather than of the stage.
		CHECK_EQ(lifecycle_outcome_of("lifecycle_proxy_reflection", "plain", "failure_recovery", "text"),
				"recovered");
		// A transition that refuses a damaged input reads differently from a healthy one whatever the
		// declared type is, so no destination of those families is ever indistinguishable.
		for (const String &family : { String("lifecycle_bytecode_export_load"), String("lifecycle_reload"),
					 String("lifecycle_cache_replacement"),
					 String("lifecycle_shutdown_reinitialization") }) {
			CAPTURE(family);
			CHECK_EQ(lifecycle_outcome_of(family, "union", "failure_recovery", "text"), "recovered");
		}
	}

	TEST_CASE("TypeCompleteness Lifecycle a reload updates the entry the identity already holds") {
		// A reload and a replacement both re-read the identity from disk and differ in what happens to
		// the entry the cache holds for it. Each family refuses the other's outcome, so a "reload" that
		// installed a fresh object could not report a carried type.
		CHECK_EQ(lifecycle_identity_of("lifecycle_reload", "plain", "clean", "text"), "preserved");
		CHECK_EQ(lifecycle_identity_of("lifecycle_cache_replacement", "plain", "clean", "text"),
				"preserved");
		// Both re-read the identity, so both carry the revision the stale stage put behind it - which a
		// transition that re-parsed the source the script already held would not.
		CHECK_EQ(lifecycle_identity_of("lifecycle_reload", "plain", "stale", "text"), "projected");
		CHECK_EQ(lifecycle_identity_of("lifecycle_cache_replacement", "plain", "stale", "text"),
				"projected");
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

			// A family declares a carve-out only where one destination answers differently from the rest;
			// everything a whole stage does belongs to that stage's relation. Whichever it is, an
			// exception nothing observes is a carve-out no cell exercises.
			const Array exceptions = result.report["exceptions"];
			for (int index = 0; index < exceptions.size(); index++) {
				const Dictionary exception_report = exceptions[index];
				CAPTURE(String(exception_report["exception_id"]));
				CHECK(bool(exception_report["witnessed"]));
			}

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
