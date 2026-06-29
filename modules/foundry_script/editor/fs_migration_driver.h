/**************************************************************************/
/*  fs_migration_driver.h                                                 */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
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

#ifdef TOOLS_ENABLED

#include "fs_fixpoint_inference.h"
#include "fs_project_scan.h"

#include "core/string/ustring.h"
#include "core/templates/vector.h"

#include "editor/script/script_refactor_vcs_guard.h"

// Options for a full migration run: how the project is enumerated (scan stage) and how
// annotations are inferred and verified (fixpoint stage). Defaults match the safe-by-default
// stance of the wizard: third-party code is excluded and strict modes stay off.
struct MigrationDriverOptions {
	ProjectScanOptions scan;
	FixpointInferenceOptions inference;

	// Before writing any edits, inspect the project's version-control state and
	// refuse to overwrite an unversioned, dirty, or indeterminate working tree.
	// The migration applies edits in place across many files, so a clean commit
	// or backup is the user's only reliable way to revert; on by default.
	bool enforce_vcs_safety_guard = true;
	// Set once the user has seen the guard's warning and chosen to proceed anyway.
	// When true the guard result is still reported but does not block the run.
	bool acknowledge_vcs_warning = false;
	// Test/embedding seam. When non-null, the driver derives its version-control
	// safety verdict from this injected working-tree state through the guard's pure
	// evaluate() function instead of inspecting the real project with git. This lets
	// headless callers — notably the unit tests — exercise the block / acknowledge /
	// proceed handling deterministically without a git checkout or spawned git
	// processes. The live per-target ignored-file refinement (a separate git probe)
	// is skipped when state is injected; the injected state alone fixes the verdict.
	const ScriptRefactorVCSGuard::WorkingTreeState *vcs_state_override = nullptr;
};

// The combined report of a single migration run, joining the scan stage's honest account of
// what was considered with the fixpoint stage's account of what was changed and skipped. The
// edit set (changed_files) is the stable, verified result the wizard previews and commits.
struct MigrationDriverResult {
	// True only means the run completed without a fatal error; it does not imply any
	// annotations were applied. Consult changed_files and skipped for actual results.
	bool ok = false;
	String error_message; // Set only on a fatal, no-op failure (unreadable root or file).

	// Version-control safety guard outcome, evaluated before any file is written.
	// When enforce_vcs_safety_guard is on and the guard warns without an
	// acknowledgment, the run is blocked (ok=false, blocked_by_vcs_guard=true)
	// before the inference stage and no file is modified.
	ScriptRefactorVCSGuard::Result vcs_guard;
	bool blocked_by_vcs_guard = false;
	// Migration-target scripts that git ignores, so they would be overwritten with no
	// version-control recovery. Populated only when the tree is otherwise clean and the
	// guard is enabled; a non-empty list drives the IGNORED_TARGETS guard status.
	Vector<String> ignored_targets;

	// Scan stage.
	Vector<String> scanned_files; // Every `.fs` the driver considered, deterministically ordered.
	Vector<String> skipped_directories; // Directories pruned by scan (addons, .fsignore, nested), ordered.

	// Inference stage (mirrors the fixpoint report so callers need not unpack two structs).
	int iterations = 0; // Fixpoint passes, including the final confirming pass.
	int total_annotations_applied = 0;
	// The applied annotations bucketed by declaration kind. `inferable.total` equals
	// total_annotations_applied; lets a projection report coverage by kind directly.
	FixpointInferableCounts inferable;
	// True means the fixpoint reached a real stable point (a pass applied nothing). False means
	// the iteration bound stopped the loop early, so the edit set may not yet be complete.
	bool converged = false;
	Vector<FixpointFileChange> changed_files; // One entry per file the run rewrote.
	Vector<FixpointSkipped> skipped; // Declarations left untyped, each with a reason.
	// Files the driver scanned but could not analyze (parse/analyze error, unresolved
	// dependency), each with a reason. Counted distinctly from changed and skipped so an
	// all-zero edit set on an unanalyzable project is not mistaken for a clean run.
	Vector<FixpointUnanalyzed> unanalyzed_files;
};

// The dependency-ordered migration driver: the single pipeline that turns a project root into a
// stable, verified edit set. It enumerates the project with FSProjectScan (excluding
// third-party code unless opted in), then drives Add Type Annotation to a fixpoint over the
// discovered files with FSFixpointInference, which collects candidates per pass and routes
// every accepted rewrite through the verification harness before committing it. Both stages are
// deterministic (sorted scan, fixpoint ordering), so a given project yields the same edit set on
// every run.
class FSMigrationDriver {
public:
	// Runs the full scan -> fixpoint pipeline rooted at p_root (a `res://` path or any path
	// DirAccess can open). Mutates files on disk and global FSCache state, so it is NOT
	// safe to call concurrently with a live editing session or another run(). Returns a fatal
	// error (ok=false) only when the root cannot be enumerated or a discovered file cannot be
	// read; an empty project is a successful no-op.
	static MigrationDriverResult run(
			const String &p_root,
			const MigrationDriverOptions &p_options = MigrationDriverOptions());
};

#endif // TOOLS_ENABLED
