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
#include "gdscript_refactoring.h"
#include "gdscript_verification_harness.h"

#include "core/string/string_builder.h"

namespace {

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
	} else {
		r_counts.other++;
	}
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

	// An empty project is a successful report with nothing to tally.
	if (scan.files.is_empty()) {
		result.ok = true;
		return result;
	}

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
				switch (violation.category) {
					case StrictViolationCategory::NULLABLE:
						result.strict.nullable++;
						break;
					case StrictViolationCategory::VARIANT_BOUNDARY:
						result.strict.variant_boundary++;
						break;
					default:
						result.strict.unknown++;
						break;
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
	builder.append("Single-pass coverage snapshot; no fixpoint iteration or verification is run.\n");
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
		builder.append(vformat("  variant boundary: %d\n", strict.variant_boundary));
		builder.append(vformat("  unknown:          %d\n", strict.unknown));
	}

	builder.append("\nNo files were modified.\n");
	return builder.as_string();
}

#endif // TOOLS_ENABLED
