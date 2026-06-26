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
	// Nullable violations the widen-to-nullable satisfier can resolve: it produces an edit
	// (widening the boundary type from `T` to `T?`) that the verification harness confirms
	// introduces no new analyzer diagnostics. The widening only applies where the boundary
	// differs from the value purely in nullability, so it makes the declared type admit the
	// null the analyzer proved can already reach it rather than changing the value's type. A
	// subset of `nullable`; the remainder stay report-only. Counted only when
	// strict_null_checks is requested.
	int nullable_satisfiable = 0;
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
	// Projection mode. When false (default), the inferable/skipped tallies are the single-pass
	// coverage snapshot described on MigrationReportResult. When true, they instead reflect the
	// ACTUAL dependency-ordered fixpoint + verification outcome -- the exact set of annotations
	// GDScriptMigrationDriver::run would commit -- computed without writing to disk. This fixes
	// the single-pass over-count (a site verification would reject) and under-count (a site that
	// only becomes inferable after a dependency is typed). The strict projection is independent
	// and still honored in either mode.
	bool projection = false;
};

// The category a follow-up site falls under, so the manual punch-list can group sites by the
// kind of work each one needs. These mirror the skip reasons and strict violations the report
// already collects; the manual report exists to turn those tallies into an actionable list of
// concrete file:line sites a human must revisit.
enum class MigrationFollowUpCategory {
	MULTI_LINE_DECLARATION, // A declaration whose assignment spans multiple lines, so no annotation could be placed.
	UNTYPED_CONTAINER, // A site whose only inferable type is an untyped/unrenderable container (no element type).
	NO_INFERRED_TYPE, // A site where no concrete type could be inferred (Variant / unresolved after fixpoint).
	STRICT_NULLABLE, // A strict_null_checks violation requiring a manual null guard or nullable annotation.
	STRICT_VARIANT_BOUNDARY, // A strict_dynamic_checks violation requiring a manual cast or boundary type.
	STRICT_UNKNOWN, // An uncategorized strict-only diagnostic.
};

// One concrete site a human must revisit, with enough location to jump straight to it. The
// detail carries the verbatim reason or message so the punch-list is self-explanatory without
// re-running the analyzer.
struct MigrationFollowUpEntry {
	MigrationFollowUpCategory category = MigrationFollowUpCategory::NO_INFERRED_TYPE;
	String path; // The `res://` (or absolute) path of the file the site lives in.
	int line = -1; // 1-based source line of the site (-1 when the source did not supply one).
	String detail; // The verbatim skip reason or strict-violation message describing the site.

	// Stable, human-readable name of a category, used as the section heading in the rendered
	// follow-up report.
	static String category_name(MigrationFollowUpCategory p_category);
};

// The dry-run report: an honest, read-only snapshot of a project's migration coverage and
// friction before any edit is made. It joins the scan stage's coverage (scripts considered,
// directories pruned) with a per-site tally of what the Add Type Annotation refactor can and
// cannot type, plus an optional projection of strict-mode violations.
//
// By default the tally is a single, independent pass: each site is "inferable" when the analyzer
// resolves a concrete, renderable type for it as the project stands today, and "skipped" (with a
// reason) otherwise. It is intentionally NOT a simulation of a full migration run: it does not
// iterate the dependency-ordered fixpoint (so a site that would only become inferable after a
// dependency is typed is reported as skipped here), and it does not run post-edit verification
// (so a site the verification harness would later reject for breaking a dependent is still
// counted as inferable). The numbers are an upper bound on per-pass coverage and a map of
// immediate friction, which is what surfacing migration viability before the first edit calls for.
//
// In projection mode (MigrationReportOptions::projection) the inferable/skipped tallies are
// instead the fixpoint+verification-accurate outcome: the exact set of annotations a real
// migration run would commit, computed by driving GDScriptMigrationDriver in a non-committing
// dry-run that restores every touched file before returning. This removes both the single-pass
// over-count and under-count at the cost of running the full pipeline.
struct MigrationReportResult {
	// True only means the report completed without a fatal error; an empty project is a
	// successful report with zero counts.
	bool ok = false;
	String error_message; // Set only on a fatal failure (an unreadable root).

	// True when the inferable/skipped tallies below are the fixpoint+verification-accurate
	// projection (MigrationReportOptions::projection) rather than the single-pass snapshot.
	bool projection = false;

	// Scan stage.
	int total_scripts_scanned = 0;
	Vector<String> scanned_files; // Every `.gd` considered, deterministically ordered.
	Vector<String> skipped_directories; // Directories pruned by scan (addons, .gdignore, nested).
	Vector<String> unanalyzable_files; // Scanned files that could not be analyzed (counted, not hidden).

	MigrationInferableCounts inferable;
	MigrationSkippedCounts skipped;
	MigrationStrictCounts strict;

	// The manual follow-up punch-list: every site a migration could NOT auto-resolve, in scan
	// order, each with its category, file, and line. This is the actionable detail behind the
	// `skipped` and `strict` tallies above -- the set of places a human must revisit. Sites that
	// are already typed are not follow-ups (no work is owed) and are excluded.
	Vector<MigrationFollowUpEntry> follow_ups;

	// Renders the counts as the human-readable report a command or menu action prints. The text
	// is for display only; callers that need the numbers read the fields above directly.
	String format() const;

	// Renders the manual follow-up punch-list: the `follow_ups` entries grouped by category, each
	// listed as `path:line  detail`. This is the regenerable artifact issue #44 calls for -- a
	// human-readable list of every site the migration left for manual follow-up. Returns a report
	// with an explicit "no follow-up sites" note when nothing was left behind.
	String format_follow_up() const;
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

	// The default location, relative to the project root, where the manual follow-up report is
	// written. Kept stable so the artifact can be regenerated in place and tracked alongside the
	// project.
	static const char *DEFAULT_FOLLOW_UP_PATH;

	// Writes the manual follow-up punch-list (MigrationReportResult::format_follow_up) to
	// p_path (a `res://` path or any path DirAccess can open), creating intermediate directories
	// as needed. This is the regenerable, on-demand artifact: calling it again overwrites the
	// previous report in place. Returns OK on success, or the FileAccess error otherwise. A report
	// that did not complete (ok == false) is refused so a fatal-failure report is never persisted.
	static Error write_follow_up(
			const MigrationReportResult &p_report,
			const String &p_path = String(DEFAULT_FOLLOW_UP_PATH));
};

#endif // TOOLS_ENABLED
