/**************************************************************************/
/*  gdscript_migration_report.cpp                                         */
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

#include "gdscript_migration_report.h"

#ifdef TOOLS_ENABLED

#include "gdscript_batch_candidates.h"
#include "gdscript_migration_driver.h"
#include "gdscript_refactoring.h"
#include "gdscript_verification_harness.h"

#include "../gdscript_position.h"

#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/string/string_builder.h"

namespace {

// Attempts the widen-to-nullable satisfier at a single nullable violation, then gates the
// resulting edit through the verification harness. Returns true only when the satisfier
// produces an edit AND the harness accepts it as introducing no new diagnostics, so a
// counted "satisfiable" violation is always one with a proven, previewable fix.
bool nullable_violation_is_satisfiable(const StrictViolation &p_violation, const Vector<String> &p_universe) {
	Error err = OK;
	const String source = FileAccess::get_file_as_string(p_violation.path, &err);
	if (err != OK) {
		return false;
	}

	RefactorContext context;
	context.path = p_violation.path;
	context.source = source;

	// A violation's line/column come from the parser as a 1-based line and a 1-based Godot
	// column; the refactor API takes a 0-based line and a 0-based text column. Convert so the
	// caret lands on the offending value, which sits inside the declaration/return statement
	// the satisfier anchors on.
	const Vector<String> lines = source.split("\n");
	const int caret_line = p_violation.line - 1;
	if (caret_line < 0 || caret_line >= lines.size()) {
		return false;
	}
	const int caret_column = GDScriptTextPosition::godot_column_to_text_column(lines[caret_line], p_violation.column);
	if (caret_column < 0) {
		return false;
	}

	RefactorLocation location;
	location.start_line = caret_line;
	location.start_column = caret_column;
	location.end_line = caret_line;
	location.end_column = caret_column;

	const RefactorResult fix = GDScriptRefactoring::prepare(context, location, RefactorKind::WIDEN_TO_NULLABLE, RefactorParams());
	if (!fix.ok || fix.edits.is_empty()) {
		return false;
	}

	VerificationCandidate candidate;
	candidate.path = p_violation.path;
	candidate.line = caret_line;
	candidate.edits = fix.edits;

	VerificationOptions options;
	options.strict_null_checks = true;
	const VerificationResult verified = GDScriptVerificationHarness::verify({ candidate }, p_universe, options);
	return verified.ok && verified.accepted.size() == 1;
}

// Buckets a disabled Add Type Annotation reason into one of the report's skip categories.
// The matched phrases are the stable parts of the reasons GDScriptRefactoring emits (the
// declaration kind is interpolated into some of them, so we key off the surrounding text):
// see find_assignable_type_annotation / find_function_return_type_annotation and
// render_annotation_or_disable in gdscript_refactoring.cpp. Anything unrecognized is counted
// as "other" so the skipped total always accounts for every disabled site.
void tally_skip_reason(const String &p_reason, MigrationSkippedCounts &r_counts) {
	r_counts.total++;
	if (p_reason.contains("already has a")) {
		// "This declaration already has a type annotation."
		// "This function already has a return type annotation."
		r_counts.already_typed++;
	} else if (p_reason.contains("spans multiple lines")) {
		r_counts.multi_line++;
	} else if (p_reason.contains("cannot be written as an explicit annotation")) {
		r_counts.unrenderable_type++;
	} else if (p_reason.contains("Cannot infer a type")) {
		r_counts.no_inferred_type++;
	} else if (p_reason.contains("unresolved after fixpoint")) {
		// Projection-only: a site that was inferable in isolation but never landed because
		// verification rejected its edit or the iteration bound was hit. It ends the run
		// untyped, so it belongs with the other "no concrete type committed" sites.
		r_counts.no_inferred_type++;
	} else {
		r_counts.other++;
	}
}

// Maps a disabled Add Type Annotation reason to the manual follow-up category a human should
// act on, or returns false when the reason describes no outstanding work (an already-typed
// site owes nothing). The phrase matching mirrors tally_skip_reason so the punch-list and the
// tallies stay in lockstep.
bool follow_up_category_for_skip_reason(const String &p_reason, MigrationFollowUpCategory &r_category) {
	if (p_reason.contains("already has a")) {
		// Already typed: no follow-up is owed.
		return false;
	}
	if (p_reason.contains("spans multiple lines")) {
		r_category = MigrationFollowUpCategory::MULTI_LINE_DECLARATION;
		return true;
	}
	if (p_reason.contains("cannot be written as an explicit annotation")) {
		// An inferred type with no renderable annotation -- in practice an untyped container
		// element type, which is exactly the "untyped containers" follow-up bucket.
		r_category = MigrationFollowUpCategory::UNTYPED_CONTAINER;
		return true;
	}
	// "Cannot infer a type" and "unresolved after fixpoint" both end with no concrete type.
	r_category = MigrationFollowUpCategory::NO_INFERRED_TYPE;
	return true;
}

// Records a skipped site in the follow-up punch-list when it represents outstanding manual work.
// The line is normalized to 1-based for display; a -1 anchor is preserved as "unknown".
void collect_skip_follow_up(const String &p_path, int p_line, const String &p_reason, Vector<MigrationFollowUpEntry> &r_follow_ups) {
	MigrationFollowUpCategory category = MigrationFollowUpCategory::NO_INFERRED_TYPE;
	if (!follow_up_category_for_skip_reason(p_reason, category)) {
		return;
	}
	MigrationFollowUpEntry entry;
	entry.category = category;
	entry.path = p_path;
	entry.line = p_line;
	entry.detail = p_reason;
	r_follow_ups.push_back(entry);
}

void tally_inferable_kind(const String &p_kind, MigrationInferableCounts &r_counts) {
	r_counts.total++;
	if (p_kind == "variable") {
		r_counts.variable++;
	} else if (p_kind == "constant") {
		r_counts.constant++;
	} else if (p_kind == "parameter") {
		r_counts.parameter++;
	} else if (p_kind == "return") {
		r_counts.return_type++;
	}
	// An unrecognized kind still counts toward the total, so the per-kind breakdown can
	// never overstate coverage relative to it.
}

} // namespace

MigrationReportResult GDScriptMigrationReport::generate(const String &p_root, const MigrationReportOptions &p_options) {
	MigrationReportResult result;

	// Stage 1: enumerate the project. A fatal scan failure (unreadable root) is the only thing
	// that aborts the report; everything else is recorded and reported honestly.
	const ProjectScanResult scan = GDScriptProjectScan::scan(p_root, p_options.scan);
	result.scanned_files = scan.files;
	result.skipped_directories = scan.skipped_directories;
	result.total_scripts_scanned = scan.files.size();
	if (!scan.ok) {
		result.error_message = scan.error_message;
		return result;
	}

	const bool strict_requested = p_options.strict_null_checks || p_options.strict_dynamic_checks;
	result.strict.requested = strict_requested;
	result.projection = p_options.projection;

	// An empty project is a successful report with nothing to tally.
	if (scan.files.is_empty()) {
		result.ok = true;
		return result;
	}

	if (p_options.projection) {
		// Projection mode: drive the full dependency-ordered fixpoint + verification exactly as a
		// real run would, but in dry-run so the tree is restored before returning. The resulting
		// counts are the exact set of annotations the migration would commit -- fixing both the
		// single-pass over-count (verification-rejected sites) and under-count (cascade-only sites).
		MigrationDriverOptions driver_options;
		driver_options.scan = p_options.scan;
		driver_options.inference.strict_null_checks = p_options.strict_null_checks;
		driver_options.inference.strict_dynamic_checks = p_options.strict_dynamic_checks;
		driver_options.inference.dry_run = true;
		const MigrationDriverResult projection = GDScriptMigrationDriver::run(p_root, driver_options);
		if (!projection.ok) {
			// A fatal projection failure (e.g. a failed dry-run restore) is fatal for the report:
			// reporting partial counts from an aborted run would be dishonest.
			result.error_message = projection.error_message;
			return result;
		}

		// The committed annotations, already bucketed by declaration kind by the fixpoint.
		result.inferable.variable = projection.inferable.variable;
		result.inferable.constant = projection.inferable.constant;
		result.inferable.parameter = projection.inferable.parameter;
		result.inferable.return_type = projection.inferable.return_type;
		result.inferable.total = projection.inferable.total;

		// The sites the run left untyped, bucketed by reason through the same taxonomy the
		// single-pass report uses.
		for (const FixpointSkipped &skip : projection.skipped) {
			tally_skip_reason(skip.reason, result.skipped);
			// FixpointSkipped.line carries the candidate's 0-based anchor; normalize to 1-based.
			const int display_line = skip.line >= 0 ? skip.line + 1 : -1;
			collect_skip_follow_up(skip.path, display_line, skip.reason, result.follow_ups);
		}

		// Files the run could not analyze are reported the same way the single-pass report does.
		for (const FixpointUnanalyzed &unanalyzed : projection.unanalyzed_files) {
			result.unanalyzable_files.push_back(unanalyzed.path);
		}
	} else {
		// Stage 2: collect every Add Type Annotation candidate across the discovered files without
		// touching disk, then bucket each site by whether it is inferable (and its kind) or skipped
		// (and why).
		// The report mirrors the verified migration run, which re-checks the dependency
		// closure, so member container element inference is enabled for the tally.
		const BatchCandidatesResult batch = GDScriptBatchCandidates::collect(scan.files, RefactorKind::ADD_TYPE_ANNOTATION, /* allow_member_container_inference */ true);
		if (!batch.ok) {
			// Only reached if headless collection is unavailable for the refactor kind, which cannot
			// happen for the fixed ADD_TYPE_ANNOTATION kind; surface it rather than report a half-run.
			result.error_message = batch.error_message;
			return result;
		}

		for (const BatchFileCandidates &file : batch.files) {
			if (!file.ok) {
				result.unanalyzable_files.push_back(file.path);
				continue;
			}
			for (const RefactorCandidate &candidate : file.candidates) {
				if (candidate.enabled) {
					tally_inferable_kind(candidate.declaration_kind, result.inferable);
				} else {
					tally_skip_reason(candidate.disabled_reason, result.skipped);
					// RefactorCandidate.line is the 0-based anchor; normalize to 1-based.
					const int display_line = candidate.line >= 0 ? candidate.line + 1 : -1;
					collect_skip_follow_up(file.path, display_line, candidate.disabled_reason, result.follow_ups);
				}
			}
		}
	}

	// Stage 3 (optional): project the diagnostics strict mode would add. Read-only, and a
	// failure here is non-fatal: the coverage tallies above stand on their own.
	if (strict_requested) {
		VerificationOptions verification_options;
		verification_options.strict_null_checks = p_options.strict_null_checks;
		verification_options.strict_dynamic_checks = p_options.strict_dynamic_checks;
		const StrictPreviewResult preview = GDScriptVerificationHarness::preview_strict(scan.files, verification_options);
		if (preview.ok) {
			for (const StrictViolation &violation : preview.violations) {
				result.strict.total++;
				// A strict violation is a manual follow-up unless a satisfier proves an auto-fix for
				// it. Each branch decides both the tally and whether (and as what) it is a follow-up.
				bool is_follow_up = true;
				MigrationFollowUpCategory category = MigrationFollowUpCategory::STRICT_UNKNOWN;
				switch (violation.category) {
					case StrictViolationCategory::NULLABLE:
						result.strict.nullable++;
						category = MigrationFollowUpCategory::STRICT_NULLABLE;
						if (nullable_violation_is_satisfiable(violation, scan.files)) {
							result.strict.nullable_satisfiable++;
							// A proven widen-to-nullable fix exists, so no manual work is owed here.
							is_follow_up = false;
						}
						break;
					case StrictViolationCategory::VARIANT_BOUNDARY:
						result.strict.variant_boundary++;
						category = MigrationFollowUpCategory::STRICT_VARIANT_BOUNDARY;
						break;
					default:
						result.strict.unknown++;
						category = MigrationFollowUpCategory::STRICT_UNKNOWN;
						break;
				}
				if (is_follow_up) {
					MigrationFollowUpEntry entry;
					entry.category = category;
					entry.path = violation.path;
					entry.line = violation.line; // Strict violations carry a 1-based line.
					entry.detail = violation.message;
					result.follow_ups.push_back(entry);
				}
			}
		} else {
			result.strict.error = preview.error_message;
		}
	}

	result.ok = true;
	return result;
}

String MigrationReportResult::format() const {
	if (!ok) {
		return vformat("GDScript migration dry-run report\nReport failed: %s\n", error_message);
	}

	StringBuilder builder;
	builder.append("GDScript migration dry-run report\n");
	builder.append("=================================\n");
	if (projection) {
		builder.append("Fixpoint+verification-accurate projection: the exact edit set a migration run would commit (no files changed).\n");
	} else {
		builder.append("Single-pass coverage snapshot; no fixpoint iteration or verification is run.\n");
	}
	builder.append(vformat("Scripts scanned: %d\n", total_scripts_scanned));

	builder.append(vformat("Directories skipped: %d\n", skipped_directories.size()));
	for (const String &directory : skipped_directories) {
		builder.append(vformat("  %s\n", directory));
	}

	if (!unanalyzable_files.is_empty()) {
		builder.append(vformat("Files that could not be analyzed: %d\n", unanalyzable_files.size()));
		for (const String &file : unanalyzable_files) {
			builder.append(vformat("  %s\n", file));
		}
	}

	builder.append(vformat("\nInferable type annotations: %d\n", inferable.total));
	builder.append(vformat("  variables:  %d\n", inferable.variable));
	builder.append(vformat("  constants:  %d\n", inferable.constant));
	builder.append(vformat("  parameters: %d\n", inferable.parameter));
	builder.append(vformat("  returns:    %d\n", inferable.return_type));

	builder.append(vformat("\nSites skipped: %d\n", skipped.total));
	builder.append(vformat("  already typed:         %d\n", skipped.already_typed));
	builder.append(vformat("  no inferred type:      %d\n", skipped.no_inferred_type));
	builder.append(vformat("  multi-line assignment: %d\n", skipped.multi_line));
	builder.append(vformat("  unrenderable type:     %d\n", skipped.unrenderable_type));
	builder.append(vformat("  other:                 %d\n", skipped.other));

	builder.append("\n");
	if (!strict.requested) {
		builder.append("Projected strict-mode violations: not requested\n");
	} else if (!strict.error.is_empty()) {
		builder.append(vformat("Projected strict-mode violations: preview failed: %s\n", strict.error));
	} else {
		builder.append(vformat("Projected strict-mode violations: %d\n", strict.total));
		builder.append(vformat("  nullable:         %d\n", strict.nullable));
		builder.append(vformat("    auto-fixable:   %d\n", strict.nullable_satisfiable));
		builder.append(vformat("  variant boundary: %d\n", strict.variant_boundary));
		builder.append(vformat("  unknown:          %d\n", strict.unknown));
	}

	builder.append("\nNo files were modified.\n");
	return builder.as_string();
}

String MigrationFollowUpEntry::category_name(MigrationFollowUpCategory p_category) {
	switch (p_category) {
		case MigrationFollowUpCategory::MULTI_LINE_DECLARATION:
			return "Multi-line declarations";
		case MigrationFollowUpCategory::UNTYPED_CONTAINER:
			return "Untyped containers";
		case MigrationFollowUpCategory::NO_INFERRED_TYPE:
			return "No inferable type";
		case MigrationFollowUpCategory::STRICT_NULLABLE:
			return "Strict-mode violations (nullable)";
		case MigrationFollowUpCategory::STRICT_VARIANT_BOUNDARY:
			return "Strict-mode violations (variant boundary)";
		case MigrationFollowUpCategory::STRICT_UNKNOWN:
			return "Strict-mode violations (uncategorized)";
	}
	return "Other";
}

String MigrationReportResult::format_follow_up() const {
	if (!ok) {
		return vformat("GDScript migration follow-up report\nReport failed: %s\n", error_message);
	}

	StringBuilder builder;
	builder.append("GDScript migration follow-up report\n");
	builder.append("===================================\n");
	builder.append("Sites a migration could not auto-resolve, grouped by category. Each one needs a\n");
	builder.append("manual decision before strict typing is complete.\n\n");

	// Caveats are outstanding work that has no per-site list: files the analyzer could not read
	// at all (so their declarations were invisible to the tally) and a strict preview that failed
	// (so any strict violations went uncounted). They are surfaced before -- and independently of
	// -- the per-site list so a report is never read as "all clear" while work was silently skipped.
	const bool has_unanalyzable = !unanalyzable_files.is_empty();
	const bool strict_preview_failed = strict.requested && !strict.error.is_empty();

	if (has_unanalyzable) {
		builder.append(vformat("Files that could not be analyzed (their sites are NOT in the list below): %d\n", unanalyzable_files.size()));
		for (const String &file : unanalyzable_files) {
			builder.append(vformat("  %s\n", file));
		}
		builder.append("\n");
	}
	if (strict_preview_failed) {
		builder.append(vformat("Strict-mode preview failed, so strict violations are NOT in the list below: %s\n\n", strict.error));
	}

	if (follow_ups.is_empty()) {
		if (has_unanalyzable || strict_preview_failed) {
			// No per-site follow-ups, but the caveats above mean the report is not a clean bill of
			// health: outstanding work exists that simply could not be enumerated.
			builder.append("No per-site follow-ups, but see the caveats above for work that could not be enumerated.\n");
		} else {
			builder.append("No follow-up sites: every scanned declaration was either typed or auto-migratable.\n");
		}
		return builder.as_string();
	}

	// Group by category, preserving the scan-ordered entries within each group. Iterating the
	// categories in their declaration order gives a stable, deterministic section order.
	const MigrationFollowUpCategory order[] = {
		MigrationFollowUpCategory::MULTI_LINE_DECLARATION,
		MigrationFollowUpCategory::UNTYPED_CONTAINER,
		MigrationFollowUpCategory::NO_INFERRED_TYPE,
		MigrationFollowUpCategory::STRICT_NULLABLE,
		MigrationFollowUpCategory::STRICT_VARIANT_BOUNDARY,
		MigrationFollowUpCategory::STRICT_UNKNOWN,
	};

	builder.append(vformat("Total follow-up sites: %d\n", follow_ups.size()));

	for (const MigrationFollowUpCategory category : order) {
		int category_count = 0;
		for (const MigrationFollowUpEntry &entry : follow_ups) {
			if (entry.category == category) {
				category_count++;
			}
		}
		if (category_count == 0) {
			continue;
		}
		builder.append(vformat("\n%s: %d\n", MigrationFollowUpEntry::category_name(category), category_count));
		for (const MigrationFollowUpEntry &entry : follow_ups) {
			if (entry.category != category) {
				continue;
			}
			if (entry.line >= 0) {
				builder.append(vformat("  %s:%d  %s\n", entry.path, entry.line, entry.detail));
			} else {
				builder.append(vformat("  %s  %s\n", entry.path, entry.detail));
			}
		}
	}

	return builder.as_string();
}

const char *GDScriptMigrationReport::DEFAULT_FOLLOW_UP_PATH = "res://gdscript_migration_followup.md";

Error GDScriptMigrationReport::write_follow_up(const MigrationReportResult &p_report, const String &p_path) {
	// A fatal-failure report carries no trustworthy follow-up list, so persisting it would be
	// misleading. Refuse it explicitly rather than write an empty or half-formed artifact.
	if (!p_report.ok) {
		return ERR_INVALID_DATA;
	}

	// Create any missing parent directories. Select the access backend from the path itself
	// (create_for_path resolves res://, user://, and bare filesystem paths to the right backend),
	// so a user:// report path is not forced through the resource backend.
	const String base_dir = p_path.get_base_dir();
	if (!base_dir.is_empty()) {
		Ref<DirAccess> dir = DirAccess::create_for_path(base_dir);
		if (dir.is_valid() && !dir->dir_exists(base_dir)) {
			const Error make_dir_error = dir->make_dir_recursive(base_dir);
			if (make_dir_error != OK) {
				return make_dir_error;
			}
		}
	}

	Error open_error = OK;
	Ref<FileAccess> file = FileAccess::open(p_path, FileAccess::WRITE, &open_error);
	if (open_error != OK || file.is_null()) {
		return open_error != OK ? open_error : ERR_CANT_CREATE;
	}
	file->store_string(p_report.format_follow_up());
	return OK;
}

#endif // TOOLS_ENABLED
