/**************************************************************************/
/*  fs_type_completeness_destination_wrapper_adapter.h                    */
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

#include "core/os/mutex.h"
#include "core/variant/variant.h"

namespace FSTests {

// The source the unsuppressed-diagnostic probe analyzes: the same program with its warning
// suppression annotations removed. Removal replaces exactly the annotation's own characters, so the
// result keeps the original line count, keeps the indentation of every line that still has content,
// and shifts columns after a removed span only by `column_shift_by_line`.
struct FSCompletenessProbeSource {
	String text;
	HashMap<int, int> column_shift_by_line;

	// The column `p_column` on `p_line` of this probe source occupies in the original program.
	int original_column(int p_line, int p_column) const {
		const int *shift = column_shift_by_line.getptr(p_line);
		return shift == nullptr ? p_column : p_column + *shift;
	}
};

FSCompletenessProbeSource make_unsuppressed_probe_source(const String &p_source);

struct TemporaryProjectTree;

namespace DestinationWrapperInternal {

using PersistedWriteTestHook = void (*)(const String &);

void set_persisted_write_test_hook(PersistedWriteTestHook p_hook);

// Test seam: makes the reflection observation report what a regression that stopped populating
// an exported property's hint would report, so a census cell that claims to observe the hint
// string can be proven to fail when it is lost.
void set_blank_reflection_hint_for_test(bool p_blank);

// Serializes the whole lifetime of one synthetic source file: the tree, the cache overrides keyed by
// its path, and its removal. The lock is a member so it is released exactly when the scope ends,
// whichever way the constructor left the scope unusable.
class SyntheticSourceScope {
	MutexLock<Mutex> lock;
	String path;
	TemporaryProjectTree *tree = nullptr;
	bool source_available = false;

public:
	SyntheticSourceScope(const String &p_identity, const String &p_source);
	~SyntheticSourceScope();

	SyntheticSourceScope(const SyntheticSourceScope &) = delete;
	SyntheticSourceScope &operator=(const SyntheticSourceScope &) = delete;

	bool is_available() const { return source_available; }
	const String &get_path() const { return path; }
};

} // namespace DestinationWrapperInternal

// The leaves this adapter renders on each axis. Declared once here and returned verbatim by
// `renderable_leaves()`, so a manifest domain, a partition test, and the renderer cannot disagree
// about what the adapter covers.
Vector<String> destination_wrapper_destinations();
Vector<String> destination_wrapper_boundaries();
Vector<String> destination_wrapper_source_proofs();
Vector<String> destination_wrapper_census_children();

// Census child slots this adapter renders a witness for but cannot read back at the representation
// the leaf names, so they are deliberately not renderable and no family may select one.
Vector<String> destination_wrapper_unobserved_census_children();

class FSDestinationWrapperAdapter : public FSCompletenessFamilyAdapter {
public:
	// The one instance of this adapter, and the one the registry hands out.
	static const FSDestinationWrapperAdapter &shared();

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

	FSCompletenessObservation inspect_runtime_contract(
			const FSCompletenessProgram &p_program, const Dictionary &p_runtime_context) const;
};

} // namespace FSTests
