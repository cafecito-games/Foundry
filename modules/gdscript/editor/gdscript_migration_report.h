/**************************************************************************/
/*  gdscript_migration_report.h                                           */
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

#include "gdscript_project_scan.h"

#include "core/string/ustring.h"
#include "core/templates/vector.h"

// Inferable (enabled) Add Type Annotation sites, bucketed by the kind of declaration
// each one annotates. `total` counts every inferable site; the per-kind fields sum to
// `total` whenever every site carries a recognized declaration kind.
struct MigrationInferableCounts {
	int variable = 0;
	int constant = 0;
	int parameter = 0;
	int return_type = 0;
	int total = 0;
};

// Sites the analyzer found but could not type, bucketed by why. The categories mirror the
// disabled reasons GDScriptRefactoring emits for Add Type Annotation: a site whose reason
// matches none of the known buckets is counted in `other` so nothing is silently dropped.
struct MigrationSkippedCounts {
	int already_typed = 0; // The declaration already carries an explicit annotation.
	int no_inferred_type = 0; // No concrete type could be inferred (Variant / unresolved).
	int multi_line = 0; // The assignment spans multiple lines, so the edit cannot be placed.
	int unrenderable_type = 0; // A type was inferred but cannot be written (e.g. a container element type).
	int other = 0; // A reason outside the buckets above (kept so the total stays honest).
	int total = 0;
};

// Projected diagnostics that turning on strict mode would introduce, bucketed by the kind
// of strict rule each one breaks. Populated only when a strict flag is requested; otherwise
// `requested` is false and the counts stay zero.
struct MigrationStrictCounts {
	bool requested = false;
	String error; // Set if the strict preview failed; counts stay zero and the rest of the report is still valid.
	int total = 0;
	int nullable = 0; // strict_null_checks violations.
	int variant_boundary = 0; // strict_dynamic_checks violations.
	int unknown = 0; // Uncategorized strict-only diagnostics.
};

// Options for a dry-run report. Scanning follows the same safe-by-default rules as a real
// migration (third-party code excluded unless opted in). The strict flags control only the
// optional strict-mode projection; the report never writes to disk regardless of them.
struct MigrationReportOptions {
	ProjectScanOptions scan;
	bool strict_null_checks = false;
	bool strict_dynamic_checks = false;
};

// The dry-run report: an honest, read-only account of what a migration would do to a project
// before any edit is made. It joins the scan stage's coverage (scripts considered, directories
// pruned) with a per-site tally of what the Add Type Annotation refactor could and could not
// type, plus an optional projection of strict-mode violations.
struct MigrationReportResult {
	// True only means the report completed without a fatal error; an empty project is a
	// successful report with zero counts.
	bool ok = false;
	String error_message; // Set only on a fatal failure (an unreadable root).

	// Scan stage.
	int total_scripts_scanned = 0;
	Vector<String> scanned_files; // Every `.gd` considered, deterministically ordered.
	Vector<String> skipped_directories; // Directories pruned by scan (addons, .gdignore, nested).
	Vector<String> unanalyzable_files; // Scanned files that could not be analyzed (counted, not hidden).

	MigrationInferableCounts inferable;
	MigrationSkippedCounts skipped;
	MigrationStrictCounts strict;

	// Renders the counts as the human-readable report a command or menu action prints. The text
	// is for display only; callers that need the numbers read the fields above directly.
	String format() const;
};

// The dry-run migration report generator. It enumerates a project with GDScriptProjectScan,
// collects Add Type Annotation candidates across the discovered files with GDScriptBatchCandidates,
// and (when a strict flag is requested) projects strict-mode violations with the verification
// harness. Every stage it consults is read-only, so a report never modifies anything on disk.
class GDScriptMigrationReport {
public:
	// Builds the report for the project rooted at p_root (a `res://` path or any path DirAccess
	// can open). Returns a fatal error (ok=false) only when the root cannot be enumerated; an
	// unanalyzable individual file is recorded in unanalyzable_files and does not abort the report.
	static MigrationReportResult generate(
			const String &p_root,
			const MigrationReportOptions &p_options = MigrationReportOptions());
};

#endif // TOOLS_ENABLED
