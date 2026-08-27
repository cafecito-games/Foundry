/**************************************************************************/
/*  fs_type_completeness_cli.h                                            */
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

#include "fs_type_completeness_runner.h"

#include "core/string/ustring.h"
#include "core/templates/vector.h"

namespace FSTests {

// Command-line entry point of the type-completeness harness: `foundry test completeness run`.
//
// Every refusal is fail-closed. A missing or malformed budgets document, an unknown tier, a timeout
// larger than the tier allows, or a family with no rule manifest all end the invocation before any
// family runs; nothing falls back to a default.
class FSCompletenessCLI {
public:
	// Exit codes are the interface the presubmit gate and the scheduled shards read.
	enum ExitCode {
		EXIT_PASSED = 0,
		EXIT_PRODUCT_MISMATCH = 1,
		// Also covers invalid arguments: a gate must not read a misinvocation as a clean run.
		EXIT_STRUCTURAL_FAILURE = 2,
		EXIT_TIMEOUT = 3,
	};

	struct Options {
		Vector<String> families;
		String catalog_root;
		String scratch_root;
		String report_path;
		// Surface whose cases the published reports carry; empty publishes both.
		String surface;
		// Budget tier the invocation belongs to. Required.
		String tier;
		// Zero uses the tier's hard timeout; a larger value than the tier allows is refused.
		int timeout_seconds = 0;
		// Repository-relative or absolute path of the budgets document; empty uses the tracked one.
		String budgets_path;
		// Refuses the invocation when this build cannot observe the tooling surfaces at all. The
		// families still run and still publish their documents, so the uncovered cells are on record;
		// only the exit code changes, which is what lets a gate demand tooling coverage from the
		// configurations that owe it without changing what a narrower configuration publishes.
		bool require_tooling = false;
		// Test seam: monotonic microsecond clock the deadline is measured on.
		FSCompletenessClock clock = nullptr;
		// Test seam: forwarded to every family run, so a test can produce a deterministic product
		// mismatch without a catalog whose expectations are deliberately wrong.
		void (*observation_mutator)(FSCompletenessObservation &) = nullptr;
		// Test seam: forwarded to every family run, so a test can act at the moment evidence is
		// persisted - the window between assembling a report and publishing it.
		FSCompletenessPersistedWriteHook persisted_write_hook = nullptr;
	};

	// Runs the invocation and returns its exit code. Diagnostics are printed as they are produced.
	//
	// r_published_paths, when given, collects every path the invocation actually published, appended
	// as each one is published rather than inferred afterwards from what happens to exist. A caller
	// that has to keep this run's evidence alive - the command-line host, whose per-run `user://` root
	// may contain the scratch tree - must use this: the index is written last and may never be written
	// at all, so a set of paths derived from it would leave earlier, successfully published reports
	// unprotected.
	static int run(const Options &p_options, PackedStringArray *r_published_paths = nullptr);
};

// Command-line entry point of the capability-scoped selector: `foundry test completeness select`.
//
// The selector is the single source of truth for which families a change set can affect. It exists as
// a command so the presubmit wrapper can consume the selection without re-implementing any part of
// FSCompletenessCapabilityMap::select in another language.
class FSCompletenessSelectCLI {
public:
	enum ExitCode {
		EXIT_SELECTED = 0,
		// Any validation error at all: an unreadable input, an unloadable capability map, a rule
		// directory the map disagrees with, or a changed path the map refuses.
		EXIT_REFUSED = 2,
	};

	struct Options {
		// File listing one changed repository-relative path per line, LF separated.
		String changed_paths_path;
		// Catalog root holding `capabilities.json` and the `rules` directory.
		String catalog_root;
		// JSON is the only supported encoding and must be requested explicitly.
		bool json = false;
	};

	// Runs the selection and returns its exit code. The JSON document is always produced, including
	// when the selection is refused: a caller that has to publish a broad slice on a refusal needs
	// the families the selector did reach.
	//
	// r_document, when given, receives the document instead of stdout; the command-line host passes
	// nothing and the document is printed verbatim, with no other stdout output in any path.
	static int run(const Options &p_options, String *r_document = nullptr);
};

} // namespace FSTests
