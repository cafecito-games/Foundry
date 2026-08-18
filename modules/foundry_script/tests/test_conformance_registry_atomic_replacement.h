/**************************************************************************/
/*  test_conformance_registry_atomic_replacement.h                        */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             FOUNDRY ENGINE                             */
/*          A fork of the Godot Engine (https://godotengine.org)          */
/*                       https://www.cafecito.games                       */
/**************************************************************************/
/* Copyright (c) 2026-present Cafecito Games LLC.                         */
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

#include "modules/foundry_script/fs_conformance_registry.h"

#include "core/os/os.h"
#include "core/os/thread.h"
#include "core/templates/safe_refcount.h"

#include "tests/test_macros.h"

// Declaration-side conformance registration used to be check-then-register: each cross-file query
// took the registry lock on its own and the write took it again later. Two analyzers reanalyzing two
// files could therefore both observe the absence of the other's conformance and both publish, leaving
// a pair no single-file analysis can detect afterwards.
//
// These cases drive `try_replace_file_conformances()` directly, from real threads released together
// by a barrier, and assert the invariants the atomic operation owes: whichever thread wins, exactly
// one conflicting declaration is accepted and the other is rejected with a record precise enough to
// reproduce the diagnostic. Which filename wins is deliberately never asserted.
namespace FSConformanceRegistryAtomicReplacementTests {

// A registry state that exists only for the duration of one case: whatever a case registers is
// dropped again, because the registry is a process-global singleton shared with every other test.
class RegistryScope {
	Vector<String> source_files;

public:
	void track(const String &p_source_file) {
		if (!source_files.has(p_source_file)) {
			source_files.push_back(p_source_file);
		}
	}

	~RegistryScope() {
		for (const String &source_file : source_files) {
			FSConformanceRegistry::get_singleton()->clear_file(source_file);
		}
	}
};

static FSConformanceRegistry::RecordedTypeArgument builtin_argument(Variant::Type p_type) {
	FSConformanceRegistry::RecordedTypeArgument argument;
	argument.kind = FSConformanceRegistry::RecordedTypeArgument::BUILTIN;
	argument.builtin_type = p_type;
	return argument;
}

static Vector<FSConformanceRegistry::RecordedTypeArgument> builtin_arguments(Variant::Type p_type) {
	Vector<FSConformanceRegistry::RecordedTypeArgument> arguments;
	arguments.push_back(builtin_argument(p_type));
	return arguments;
}

// A script-class conformance: the target belongs to a file, so it is never mistaken for an engine
// class, and `target_native_base` is the engine class its inheritance chain bottoms out at.
static FSConformanceRegistry::Conformance script_conformance(const String &p_source_file,
		const String &p_target_fqcn, const StringName &p_trait_name, int p_conformance_index) {
	FSConformanceRegistry::Conformance conformance;
	conformance.target_keys.push_back(p_target_fqcn);
	conformance.target_fqcn = p_target_fqcn;
	conformance.target_script_path = p_source_file;
	conformance.target_is_root_class = true;
	conformance.target_label = p_target_fqcn;
	conformance.trait_name = p_trait_name;
	conformance.source_file = p_source_file;
	conformance.conformance_index = p_conformance_index;
	return conformance;
}

// An engine-class conformance (`extend Node uses ...`): keyed by the bare engine-class name, owned by
// no script file.
static FSConformanceRegistry::Conformance native_conformance(const String &p_source_file,
		const StringName &p_native_class, const StringName &p_trait_name, int p_conformance_index) {
	FSConformanceRegistry::Conformance conformance;
	conformance.target_keys.push_back(String(p_native_class));
	conformance.target_fqcn = String(p_native_class);
	conformance.target_is_root_class = true;
	conformance.target_label = String(p_native_class);
	conformance.trait_name = p_trait_name;
	conformance.source_file = p_source_file;
	conformance.conformance_index = p_conformance_index;
	return conformance;
}

// Two submissions released as close together as the platform allows, repeated so both interleavings
// are exercised. Every repetition starts from a registry that holds neither file.
struct ConcurrentSubmission {
	String first_file;
	Vector<FSConformanceRegistry::Conformance> first_candidates;
	String second_file;
	Vector<FSConformanceRegistry::Conformance> second_candidates;

	FSConformanceRegistry::RegistrationResult first_result;
	FSConformanceRegistry::RegistrationResult second_result;

private:
	SafeNumeric<int> arrived;
	SafeFlag released;

	struct Arm {
		ConcurrentSubmission *submission = nullptr;
		bool is_first = false;
	};

	static void _run(void *p_argument) {
		Arm *arm = static_cast<Arm *>(p_argument);
		ConcurrentSubmission *submission = arm->submission;
		// The barrier: both threads announce arrival, and neither proceeds until the second one has,
		// so the two calls contend for the registry lock instead of running in sequence.
		submission->arrived.increment();
		while (!submission->released.is_set()) {
			OS::get_singleton()->delay_usec(1);
		}
		FSConformanceRegistry::RegistrationResult result =
				FSConformanceRegistry::get_singleton()->try_replace_file_conformances(
						arm->is_first ? submission->first_file : submission->second_file,
						arm->is_first ? submission->first_candidates : submission->second_candidates);
		if (arm->is_first) {
			submission->first_result = result;
		} else {
			submission->second_result = result;
		}
	}

public:
	void run() {
		arrived.set(0);
		released.clear();
		first_result = FSConformanceRegistry::RegistrationResult();
		second_result = FSConformanceRegistry::RegistrationResult();

		Arm first_arm{ this, true };
		Arm second_arm{ this, false };
		Thread first_thread;
		Thread second_thread;
		first_thread.start(_run, &first_arm);
		second_thread.start(_run, &second_arm);
		while (arrived.get() < 2) {
			OS::get_singleton()->delay_usec(1);
		}
		released.set();
		first_thread.wait_to_finish();
		second_thread.wait_to_finish();
	}

	// The invariant every conflicting pair owes, stated without naming a winner: one side registered
	// its declaration, the other registered nothing and was told exactly why.
	void check_exactly_one_accepted(FSConformanceRegistry::RegistrationConflict::Kind p_kind) const {
		const bool first_won = first_result.conflicts.is_empty();
		const FSConformanceRegistry::RegistrationResult &winner = first_won ? first_result : second_result;
		const FSConformanceRegistry::RegistrationResult &loser = first_won ? second_result : first_result;

		CHECK(winner.conflicts.is_empty());
		CHECK_GT(winner.registered_count, 0);
		REQUIRE_EQ(loser.conflicts.size(), 1);
		CHECK_EQ(loser.conflicts[0].kind, p_kind);
		CHECK_EQ(loser.registered_count, 0);
		CHECK_FALSE(loser.conflicts[0].conflicting_source_file.is_empty());
		CHECK_EQ(loser.conflicts[0].conflicting_source_file,
				first_won ? first_file : second_file);
	}
};

static constexpr int CONCURRENCY_REPETITIONS = 24;

TEST_CASE("[Modules][FoundryScript][Conformance] Concurrent duplicate memberships leave exactly one owner") {
	RegistryScope scope;
	const String first_file = "user://atomic_duplicate_first.fs";
	const String second_file = "user://atomic_duplicate_second.fs";
	scope.track(first_file);
	scope.track(second_file);

	ConcurrentSubmission submission;
	submission.first_file = first_file;
	submission.second_file = second_file;
	submission.first_candidates.push_back(
			script_conformance("user://atomic_duplicate_target.fs", "AtomicDuplicateTarget", "AtomicDuplicateTrait", 0));
	submission.first_candidates.write[0].source_file = first_file;
	submission.second_candidates.push_back(
			script_conformance("user://atomic_duplicate_target.fs", "AtomicDuplicateTarget", "AtomicDuplicateTrait", 0));
	submission.second_candidates.write[0].source_file = second_file;

	for (int repetition = 0; repetition < CONCURRENCY_REPETITIONS; repetition++) {
		FSConformanceRegistry::get_singleton()->clear_file(first_file);
		FSConformanceRegistry::get_singleton()->clear_file(second_file);

		submission.run();

		submission.check_exactly_one_accepted(
				FSConformanceRegistry::RegistrationConflict::DUPLICATE_MEMBERSHIP);
		// One membership, one owner: the surviving declaration is the only one the index answers with.
		const String owner = FSConformanceRegistry::get_singleton()->get_conformance_source(
				"AtomicDuplicateTarget", "AtomicDuplicateTrait");
		CHECK((owner == first_file || owner == second_file));
		const bool first_registered =
				!FSConformanceRegistry::get_singleton()->get_file_conformances(first_file).is_empty();
		const bool second_registered =
				!FSConformanceRegistry::get_singleton()->get_file_conformances(second_file).is_empty();
		CHECK_NE(first_registered, second_registered);
	}
}

TEST_CASE("[Modules][FoundryScript][Conformance] Concurrent witness collisions leave exactly one witness") {
	RegistryScope scope;
	const String first_file = "user://atomic_witness_first.fs";
	const String second_file = "user://atomic_witness_second.fs";
	scope.track(first_file);
	scope.track(second_file);

	// Two different traits on one target, each supplying a witness under the same method name. Nothing
	// at run time could decide which of the two a call to that name means.
	ConcurrentSubmission submission;
	submission.first_file = first_file;
	submission.second_file = second_file;
	FSConformanceRegistry::Conformance first = script_conformance(
			"user://atomic_witness_target.fs", "AtomicWitnessTarget", "AtomicWitnessTraitA", 0);
	first.source_file = first_file;
	first.witnesses.insert("atomic_witness_label", nullptr);
	FSConformanceRegistry::Conformance second = script_conformance(
			"user://atomic_witness_target.fs", "AtomicWitnessTarget", "AtomicWitnessTraitB", 0);
	second.source_file = second_file;
	second.witnesses.insert("atomic_witness_label", nullptr);
	submission.first_candidates.push_back(first);
	submission.second_candidates.push_back(second);

	for (int repetition = 0; repetition < CONCURRENCY_REPETITIONS; repetition++) {
		FSConformanceRegistry::get_singleton()->clear_file(first_file);
		FSConformanceRegistry::get_singleton()->clear_file(second_file);

		submission.run();

		submission.check_exactly_one_accepted(
				FSConformanceRegistry::RegistrationConflict::WITNESS_COLLISION);
		const FSConformanceRegistry::RegistrationResult &loser =
				submission.first_result.conflicts.is_empty() ? submission.second_result : submission.first_result;
		CHECK_EQ(loser.conflicts[0].method_name, StringName("atomic_witness_label"));
		CHECK_EQ(loser.conflicts[0].target_label, String("AtomicWitnessTarget"));
	}
}

TEST_CASE("[Modules][FoundryScript][Conformance] Concurrent native-chain contradictions reject exactly one") {
	RegistryScope scope;
	const String first_file = "user://atomic_native_chain_first.fs";
	const String second_file = "user://atomic_native_chain_second.fs";
	scope.track(first_file);
	scope.track(second_file);

	// `RefCounted` is an `Object`, so a conformance on either answers for a `RefCounted` receiver;
	// binding the trait to `int` on one level and `String` on the other is incoherent.
	ConcurrentSubmission submission;
	submission.first_file = first_file;
	submission.second_file = second_file;
	FSConformanceRegistry::Conformance first =
			native_conformance(first_file, "Object", "AtomicNativeChainTrait", 0);
	first.trait_type_arguments = builtin_arguments(Variant::INT);
	FSConformanceRegistry::Conformance second =
			native_conformance(second_file, "RefCounted", "AtomicNativeChainTrait", 0);
	second.trait_type_arguments = builtin_arguments(Variant::STRING);
	submission.first_candidates.push_back(first);
	submission.second_candidates.push_back(second);

	for (int repetition = 0; repetition < CONCURRENCY_REPETITIONS; repetition++) {
		FSConformanceRegistry::get_singleton()->clear_file(first_file);
		FSConformanceRegistry::get_singleton()->clear_file(second_file);

		submission.run();

		submission.check_exactly_one_accepted(
				FSConformanceRegistry::RegistrationConflict::CHAIN_COHERENCE);
		const Vector<FSConformanceRegistry::NativeConformanceRecord> records =
				FSConformanceRegistry::get_singleton()->get_native_conformance_records("AtomicNativeChainTrait");
		CHECK_EQ(records.size(), 1);
	}
}

TEST_CASE("[Modules][FoundryScript][Conformance] Concurrent mixed script/native contradictions reject exactly one") {
	RegistryScope scope;
	const String native_file = "user://atomic_mixed_chain_native.fs";
	const String script_file = "user://atomic_mixed_chain_script.fs";
	scope.track(native_file);
	scope.track(script_file);

	// One semantic chain runs from a script class through the engine ancestry it ends on, so an
	// `extend Object` declaration and a script class that bottoms out on `RefCounted` describe the same
	// trait for overlapping receivers.
	ConcurrentSubmission submission;
	submission.first_file = native_file;
	submission.second_file = script_file;
	FSConformanceRegistry::Conformance native =
			native_conformance(native_file, "Object", "AtomicMixedChainTrait", 0);
	native.trait_type_arguments = builtin_arguments(Variant::INT);
	FSConformanceRegistry::Conformance script =
			script_conformance(script_file, "AtomicMixedChainHolder", "AtomicMixedChainTrait", 0);
	script.target_native_base = "RefCounted";
	script.trait_type_arguments = builtin_arguments(Variant::STRING);
	submission.first_candidates.push_back(native);
	submission.second_candidates.push_back(script);

	for (int repetition = 0; repetition < CONCURRENCY_REPETITIONS; repetition++) {
		FSConformanceRegistry::get_singleton()->clear_file(native_file);
		FSConformanceRegistry::get_singleton()->clear_file(script_file);

		submission.run();

		submission.check_exactly_one_accepted(
				FSConformanceRegistry::RegistrationConflict::CHAIN_COHERENCE);
		const int native_records =
				FSConformanceRegistry::get_singleton()->get_native_conformance_records("AtomicMixedChainTrait").size();
		const int script_records =
				FSConformanceRegistry::get_singleton()->get_script_conformance_records("AtomicMixedChainTrait").size();
		CHECK_EQ(native_records + script_records, 1);
	}
}

TEST_CASE("[Modules][FoundryScript][Conformance] Two non-conflicting files registered concurrently both survive") {
	RegistryScope scope;
	const String first_file = "user://atomic_independent_first.fs";
	const String second_file = "user://atomic_independent_second.fs";
	scope.track(first_file);
	scope.track(second_file);

	ConcurrentSubmission submission;
	submission.first_file = first_file;
	submission.second_file = second_file;
	FSConformanceRegistry::Conformance first =
			script_conformance(first_file, "AtomicIndependentFirst", "AtomicIndependentTrait", 0);
	FSConformanceRegistry::Conformance second =
			script_conformance(second_file, "AtomicIndependentSecond", "AtomicIndependentTrait", 0);
	submission.first_candidates.push_back(first);
	submission.second_candidates.push_back(second);

	for (int repetition = 0; repetition < CONCURRENCY_REPETITIONS; repetition++) {
		FSConformanceRegistry::get_singleton()->clear_file(first_file);
		FSConformanceRegistry::get_singleton()->clear_file(second_file);

		submission.run();

		CHECK(submission.first_result.conflicts.is_empty());
		CHECK(submission.second_result.conflicts.is_empty());
		CHECK_EQ(FSConformanceRegistry::get_singleton()->get_conformance_source(
						 "AtomicIndependentFirst", "AtomicIndependentTrait"),
				first_file);
		CHECK_EQ(FSConformanceRegistry::get_singleton()->get_conformance_source(
						 "AtomicIndependentSecond", "AtomicIndependentTrait"),
				second_file);
	}
}

TEST_CASE("[Modules][FoundryScript][Conformance] Unchanged reanalysis of one file keeps its conformances") {
	RegistryScope scope;
	const String source_file = "user://atomic_reanalysis.fs";
	scope.track(source_file);

	Vector<FSConformanceRegistry::Conformance> candidates;
	FSConformanceRegistry::Conformance entry =
			script_conformance(source_file, "AtomicReanalysisTarget", "AtomicReanalysisTrait", 0);
	entry.trait_type_arguments = builtin_arguments(Variant::INT);
	candidates.push_back(entry);

	FSConformanceRegistry *registry = FSConformanceRegistry::get_singleton();
	FSConformanceRegistry::RegistrationResult first = registry->try_replace_file_conformances(source_file, candidates);
	CHECK(first.conflicts.is_empty());
	CHECK_EQ(first.registered_count, 1);

	// The file's previous entries are excluded from the view it is judged against, so resubmitting the
	// same declarations cannot conflict with itself.
	FSConformanceRegistry::RegistrationResult second = registry->try_replace_file_conformances(source_file, candidates);
	CHECK(second.conflicts.is_empty());
	CHECK_EQ(second.registered_count, 1);
	CHECK_EQ(registry->get_conformance_source("AtomicReanalysisTarget", "AtomicReanalysisTrait"), source_file);
	CHECK_EQ(registry->get_file_conformances(source_file).size(), 1);
}

TEST_CASE("[Modules][FoundryScript][Conformance] A file that declares no conformance replaces its set with empty") {
	RegistryScope scope;
	const String source_file = "user://atomic_empty_replacement.fs";
	scope.track(source_file);

	FSConformanceRegistry *registry = FSConformanceRegistry::get_singleton();
	Vector<FSConformanceRegistry::Conformance> candidates;
	candidates.push_back(script_conformance(source_file, "AtomicEmptyTarget", "AtomicEmptyTrait", 0));
	registry->try_replace_file_conformances(source_file, candidates);
	REQUIRE_EQ(registry->get_file_conformances(source_file).size(), 1);

	const FSConformanceRegistry::RegistrationResult result =
			registry->try_replace_file_conformances(source_file, Vector<FSConformanceRegistry::Conformance>());
	CHECK(result.conflicts.is_empty());
	CHECK_EQ(result.registered_count, 0);
	CHECK(registry->get_file_conformances(source_file).is_empty());
	CHECK(registry->get_conformance_source("AtomicEmptyTarget", "AtomicEmptyTrait").is_empty());
}

TEST_CASE("[Modules][FoundryScript][Conformance] A rejected replacement still drops the source's stale entries") {
	RegistryScope scope;
	const String owner_file = "user://atomic_stale_owner.fs";
	const String other_file = "user://atomic_stale_other.fs";
	scope.track(owner_file);
	scope.track(other_file);

	FSConformanceRegistry *registry = FSConformanceRegistry::get_singleton();

	// A registry state where both files claim the membership, which is what an earlier analysis of the
	// owner followed by another file taking the target over leaves behind.
	Vector<FSConformanceRegistry::Conformance> original;
	original.push_back(script_conformance(owner_file, "AtomicStaleTarget", "AtomicStaleTrait", 0));
	original.write[0].target_script_path = "user://atomic_stale_target.fs";
	registry->register_file_conformances(owner_file, original);

	Vector<FSConformanceRegistry::Conformance> foreign;
	foreign.push_back(script_conformance(other_file, "AtomicStaleTarget", "AtomicStaleTrait", 0));
	foreign.write[0].target_script_path = "user://atomic_stale_target.fs";
	registry->register_file_conformances(other_file, foreign);

	// Reanalysis of the owner: the contradicting declaration is rejected, but a second, unrelated
	// declaration in the same file still registers, and the rejected one leaves nothing behind.
	Vector<FSConformanceRegistry::Conformance> replacement = original;
	FSConformanceRegistry::Conformance other =
			script_conformance(owner_file, "AtomicStaleOther", "AtomicStaleTrait", 1);
	other.target_script_path = "user://atomic_stale_other_target.fs";
	replacement.push_back(other);

	const FSConformanceRegistry::RegistrationResult result =
			registry->try_replace_file_conformances(owner_file, replacement);
	REQUIRE_EQ(result.conflicts.size(), 1);
	CHECK_EQ(result.conflicts[0].kind, FSConformanceRegistry::RegistrationConflict::DUPLICATE_MEMBERSHIP);
	CHECK_EQ(result.conflicts[0].conformance_index, 0);
	CHECK_EQ(result.conflicts[0].conflicting_source_file, other_file);
	CHECK_EQ(result.registered_count, 1);
	// The owner's stale claim on the contested target is gone, and only its coherent declaration stays.
	REQUIRE_EQ(registry->get_file_conformances(owner_file).size(), 1);
	CHECK_EQ(registry->get_file_conformances(owner_file)[0].target_fqcn, String("AtomicStaleOther"));
	CHECK_EQ(registry->get_conformance_source("AtomicStaleTarget", "AtomicStaleTrait"), other_file);
	CHECK_EQ(registry->get_conformance_source("AtomicStaleOther", "AtomicStaleTrait"), owner_file);
}

TEST_CASE("[Modules][FoundryScript][Conformance] A conflict on one identity rejects its whole declaration") {
	RegistryScope scope;
	const String owner_file = "user://atomic_declaration_group.fs";
	const String other_file = "user://atomic_declaration_group_other.fs";
	scope.track(owner_file);
	scope.track(other_file);

	FSConformanceRegistry *registry = FSConformanceRegistry::get_singleton();

	// Another file already owns the implied supertrait identity.
	Vector<FSConformanceRegistry::Conformance> foreign;
	foreign.push_back(script_conformance(other_file, "AtomicGroupTarget", "AtomicGroupSuperTrait", 0));
	foreign.write[0].target_script_path = "user://atomic_group_target.fs";
	REQUIRE_EQ(registry->try_replace_file_conformances(other_file, foreign).registered_count, 1);

	// One `ConformanceNode` emitting a direct identity and an implied supertrait identity. Registering
	// only the direct one would answer some membership queries with a conformance that was rejected.
	Vector<FSConformanceRegistry::Conformance> candidates;
	FSConformanceRegistry::Conformance direct =
			script_conformance(owner_file, "AtomicGroupTarget", "AtomicGroupTrait", 0);
	direct.target_script_path = "user://atomic_group_target.fs";
	FSConformanceRegistry::Conformance implied =
			script_conformance(owner_file, "AtomicGroupTarget", "AtomicGroupSuperTrait", 0);
	implied.target_script_path = "user://atomic_group_target.fs";
	candidates.push_back(direct);
	candidates.push_back(implied);

	const FSConformanceRegistry::RegistrationResult result =
			registry->try_replace_file_conformances(owner_file, candidates);
	CHECK_EQ(result.conflicts.size(), 1);
	CHECK_EQ(result.registered_count, 0);
	CHECK(registry->get_conformance_source("AtomicGroupTarget", "AtomicGroupTrait").is_empty());
	CHECK_EQ(registry->get_conformance_source("AtomicGroupTarget", "AtomicGroupSuperTrait"), other_file);
}

TEST_CASE("[Modules][FoundryScript][Conformance] Non-conflicting declarations register regardless of order") {
	RegistryScope scope;
	const String source_file = "user://atomic_order_independence.fs";
	scope.track(source_file);

	FSConformanceRegistry *registry = FSConformanceRegistry::get_singleton();

	Vector<FSConformanceRegistry::Conformance> forward;
	forward.push_back(script_conformance(source_file, "AtomicOrderFirst", "AtomicOrderTrait", 0));
	forward.write[0].target_script_path = "user://atomic_order_first.fs";
	forward.push_back(script_conformance(source_file, "AtomicOrderSecond", "AtomicOrderTrait", 1));
	forward.write[1].target_script_path = "user://atomic_order_second.fs";

	Vector<FSConformanceRegistry::Conformance> reversed;
	reversed.push_back(forward[1]);
	reversed.push_back(forward[0]);

	const FSConformanceRegistry::RegistrationResult forward_result =
			registry->try_replace_file_conformances(source_file, forward);
	CHECK(forward_result.conflicts.is_empty());
	CHECK_EQ(forward_result.registered_count, 2);

	const FSConformanceRegistry::RegistrationResult reversed_result =
			registry->try_replace_file_conformances(source_file, reversed);
	CHECK(reversed_result.conflicts.is_empty());
	CHECK_EQ(reversed_result.registered_count, 2);
	CHECK_EQ(registry->get_conformance_source("AtomicOrderFirst", "AtomicOrderTrait"), source_file);
	CHECK_EQ(registry->get_conformance_source("AtomicOrderSecond", "AtomicOrderTrait"), source_file);
}

} // namespace FSConformanceRegistryAtomicReplacementTests
