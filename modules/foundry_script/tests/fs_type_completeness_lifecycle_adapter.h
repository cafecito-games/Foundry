/**************************************************************************/
/*  fs_type_completeness_lifecycle_adapter.h                              */
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

#include "fs_type_completeness_adapter.h"

#include "core/typedefs.h"

namespace FSTests {

namespace LifecycleInternal {

// Test seam: corrupts the artifact every lifecycle transition consumes, so a cell that claims to
// observe whether the carried type survived a transition can be proven to report `rejected` when it
// does not. A dimension nothing can flip is a dimension nobody is observing.
void set_corrupt_transition_artifact_for_test(bool p_corrupt);

// Test seam: reconstructs the artifact by recompiling whatever the identity it recorded serves at
// load time, which is what a loader that resolved a serialized type against current state instead of
// against its own bytes would arrive at. It is invisible to every stage whose source is unchanged and
// visible to the stale stage, which is what proves that stage detects the regression it names.
void set_load_transition_artifact_from_source_for_test(bool p_load_from_source);

// Test seam: makes a transition hand back whatever the subsystem already held for the identity
// instead of re-deriving it, which is what a reload, a cache replacement, or a reinitialization that
// quietly kept its old answer would do. It is invisible to every stage whose identity still serves
// what it was compiled from, and visible on the stale stage, which is what proves those families
// re-derive rather than remember.
void set_skip_transition_invalidation_for_test(bool p_skip);

// Test seam: damages the artifact a bytecode-surface cell restores its subject from, and nothing
// else. A text-surface cell restores nothing, so this reaches only the cells whose subject is the
// compiled binary - which is what proves the two surfaces are measured on two different objects.
void set_corrupt_restored_subject_for_test(bool p_corrupt);

// How many times a transition has taken the language down and brought it back. A stage that applies
// the transition to its own output has to be two of these rather than one cycle followed by two
// re-derivations, which is a difference nothing else about the cell would show.
uint64_t language_cycles_for_test();

} // namespace LifecycleInternal

// The leaves this adapter renders on each axis. Declared once here and returned verbatim by
// `renderable_leaves()`, so a manifest domain, a partition test, and the renderer cannot disagree
// about what the adapter covers.
Vector<String> lifecycle_destinations();
Vector<String> lifecycle_states();

// One completeness family per lifecycle transition: the subsystem that carries a declared type from
// one artifact to the next. Every family renders the same program - a class holding one member of the
// declared type - and differs only in which subsystem it hands that program to and what it does with
// what comes back, so a transition is a transition function here rather than a family-shaped copy of
// the whole adapter.
//
// `semantic_identity` is read by rendering the member's type before the transition and again after
// it, off the artifacts the transition actually produced. It is never derived from the cell's
// coordinates: two test seams inject a failed transition and a reused pre-transition artifact, and
// the regression tests require both to flip it.
class FSLifecycleAdapter : public FSCompletenessFamilyAdapter {
public:
	// The one instance of this adapter, and the one the registry hands out.
	static const FSLifecycleAdapter &shared();

	// Families whose rule manifests name this adapter. A rendered program carries its coordinates and
	// its identity but never the family that derived them, so the adapter needs the list to prove a
	// batch's case IDs are the canonical identities of their coordinates under one family. The rule
	// directory is the source of truth; a test holds this list and that directory together.
	static Vector<String> families();

	String id() const override;
	Error render(const FSCompletenessResolvedCell &p_cell, FSCompletenessProgram &r_program) const override;
	FSCompletenessObservation analyze(
			const FSCompletenessProgram &p_program, const String &p_surface) const override;
	Error execute(const String &p_scratch_root, const Vector<FSCompletenessProgram> &p_programs,
			FSCompletenessRuntimeBatch &r_batch) const override;
	HashSet<String> observable_dimensions() const override;
	HashMap<String, Vector<String>> renderable_leaves() const override;

	// The observation one program produces, without the batch bookkeeping `execute` wraps it in.
	// `r_structural_error`, when given, reports whether the observation failed as a harness defect
	// rather than as an observation about the product: a run that could not carry out its transition
	// at all has no reading under which its silence means the type survived.
	// p_family names the transition to carry the program's declared type across; a rendered program
	// carries its coordinates and its identity but never its family.
	FSCompletenessObservation observe_transition(const FSCompletenessProgram &p_program,
			const String &p_family, Error *r_structural_error = nullptr) const;
};

} // namespace FSTests
