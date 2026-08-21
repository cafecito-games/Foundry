/**************************************************************************/
/*  fs_type_completeness_runner.h                                         */
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

#include "fs_type_completeness_union_adapter.h"

#include "core/error/error_list.h"
#include "core/templates/vector.h"
#include "core/variant/dictionary.h"

namespace FSTests {

struct FSCompletenessFinding {
	String finding_id;
	String case_id;
	String family;
	String dimension;
	Variant expected;
	Variant actual;
	String classification = "unclassified";
	String artifact_path;
	Dictionary parity_evidence;
	String issue_url;
	String closure_packet_url;
	PackedStringArray permanent_test_paths;
	String migrated_from;
	PackedStringArray resolved_case_ids;
};

using FSCompletenessTrackedFileProbe = Error (*)(const String &p_repository_root, const String &p_path,
		String &r_output, int &r_exit_code);
using FSCompletenessPersistedWriteHook = void (*)(const String &p_path);

// Monotonic microsecond clock the deadline is measured on. Injectable so a timeout can be proven
// deterministically instead of by racing a real wall clock.
using FSCompletenessClock = uint64_t (*)();

struct FSCompletenessRunOptions {
	String catalog_root;
	String family;
	String scratch_root;
	String report_path;
	void (*program_mutator)(FSCompletenessProgram &) = nullptr;
	void (*observation_mutator)(FSCompletenessObservation &) = nullptr;
	void (*runtime_result_mutator)(FSCompletenessRuntimeResult &) = nullptr;
	FSCompletenessTrackedFileProbe tracked_file_probe = nullptr;
	FSCompletenessPersistedWriteHook persisted_write_hook = nullptr;

	// Surface whose cases the published report carries; empty publishes every case. The run always
	// executes both surfaces, because parity evidence is only meaningful when both were observed;
	// this only narrows the published document for a consumer that compares one configuration.
	String published_surface;

	// Absolute deadline on `clock`, in microseconds. Zero runs without a deadline.
	uint64_t deadline_usec = 0;
	FSCompletenessClock clock = nullptr;
};

// A defect in the harness, the catalog, or the evidence contract rather than an observation about
// the product. Structural failures are reported separately from findings so a gate can never read
// a broken run as a clean one, and never file a harness defect as a product defect.
struct FSCompletenessStructuralFailure {
	String stage;
	String detail;
	String case_id;
	String witness_id;
	String exception_id;
	Error error_code = OK;
};

struct FSCompletenessRunResult {
	bool success = false;
	int executed_cells = 0;
	Vector<FSCompletenessFinding> findings;
	Vector<FSCompletenessStructuralFailure> structural_failures;
	String outcome = "not_run";
	Dictionary report;
};

class FSCompletenessRunner {
public:
	static Error run(const FSCompletenessRunOptions &p_options, FSCompletenessRunResult &r_result);

	// Publishes a document that belongs to a run without being a family report - the index over a
	// multi-family invocation - under exactly the contract a report is published with: the path must
	// be owned by the scratch root, must not resolve through a link or into the catalog or the
	// artifact tree, and the file is replaced atomically.
	static Error publish_owned_document(const String &p_scratch_root, const String &p_catalog_root,
			const String &p_document_path, const Dictionary &p_document);

	// Rewrites an already published document's verdict into the structural failure a crossed budget
	// makes it, and republishes it. A run notices its own crossing after its last write; the command
	// driving several runs can only notice after the last write of the whole invocation. Both go
	// through this, so a report, an index over it, and the exit code derived from them cannot disagree.
	static Error republish_timed_out_document(const String &p_scratch_root, const String &p_catalog_root,
			const String &p_document_path, const String &p_detail);
};

} // namespace FSTests
