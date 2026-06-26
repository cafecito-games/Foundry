/**************************************************************************/
/*  gdscript_migration_wizard.h                                           */
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

#include "gdscript_migration_driver.h"
#include "gdscript_migration_report.h"
#include "gdscript_strict_activation.h"

#include "core/string/ustring.h"

// The end-to-end migration wizard: the single orchestrator that runs the full sequence the
// editor entry point and the headless command both drive -- a dry-run report, the atomic apply,
// and the gated strict-settings activation -- in order, against one project root. It owns no UI
// and writes nothing of its own beyond delegating to the stages it composes, so the editor
// dialog and the CLI share exactly the same flow and the same safety gates.
//
// The wizard is a thin sequencer over the existing stages; it does not reimplement any of them.
// It exists so a caller does not have to re-derive the correct order, share options between
// stages, or decide when to stop (a fatal report or a VCS-blocked apply short-circuits the rest).

// What the wizard should do, and how each stage is configured. The same options drive both the
// editor and headless entry points so their behavior cannot diverge.
struct MigrationWizardOptions {
	// The dry-run report is always produced first (it is read-only and the user/CI sees it before
	// anything is written). These flags also configure the apply and activation stages below.
	bool strict_null_checks = false; // Project to enable debug/gdscript/analysis/strict_null_checks.
	bool strict_dynamic_checks = false; // Project to enable debug/gdscript/analysis/strict_dynamic_checks.

	// How the project is enumerated; shared by every stage so the report, apply, and activation
	// all consider the same set of files. Defaults match the wizard's safe-by-default stance.
	ProjectScanOptions scan;

	// Whether the dry-run report uses the fixpoint+verification-accurate projection. Projection is
	// exact (it matches what apply commits) but is NOT free of side effects: it drives the migration
	// driver's dry-run path, which writes each pass to disk and restores the files before returning,
	// and it runs without the apply VCS guard. So it is off by default -- a preview is then the truly
	// read-only single-pass snapshot. The wizard forces it on whenever apply is requested (the tree
	// is about to be written anyway, under the guard), so a committed run always previews its exact
	// edit set; a caller wanting the accurate projection without applying can opt in here.
	bool projection = false;

	// When false (default), the wizard stops after the report: a preview-only run that writes
	// nothing. The editor wires this to the user's confirmation; the CLI wires it to an explicit
	// --apply flag so a scripted run never mutates files unless asked.
	bool apply = false;

	// The version-control safety guard for the apply stage. The guard refuses to overwrite an
	// unversioned or dirty tree unless acknowledged; on by default, matching the driver.
	bool enforce_vcs_safety_guard = true;
	bool acknowledge_vcs_warning = false;

	// When false (default), the wizard stops after apply and only previews what activating the
	// strict flags would cost (StrictActivationPlan). When true it performs the gated flip, which
	// still requires confirm_strict_activation and a clean (or allow_with_violations) report.
	bool activate_strict = false;
	bool confirm_strict_activation = false; // The explicit-confirmation gate for the flip.
	bool allow_strict_with_violations = false; // The gradual escape hatch: flip despite violations.

	// When set, the manual follow-up punch-list from the report is written to this path. Empty
	// means it is not persisted (the report text still carries the summary either way).
	String follow_up_path;
};

// The combined outcome of a wizard run: each stage's result plus which stages actually ran. A
// caller renders summary() for humans/CI or reads the per-stage structs for detail.
struct MigrationWizardResult {
	// False only on a fatal error in a stage that ran (an unreadable root, a VCS-blocked apply);
	// see error_message. A preview-only run with nothing to apply is still a success.
	bool ok = false;
	String error_message;

	// The dry-run report always runs and is always populated.
	MigrationReportResult report;

	// True when options.apply asked for the apply stage to run (whether or not it succeeded).
	// Distinguishes a preview (no apply requested) from an apply that ran and failed.
	bool apply_requested = false;
	// The apply stage runs only when options.apply is true. applied stays false otherwise.
	bool applied = false;
	MigrationDriverResult apply_result;
	// True when the apply stage was requested but the VCS guard blocked it (no file was written).
	bool blocked_by_vcs_guard = false;

	// The strict-activation plan always runs after the report (read-only preview). When
	// options.activate_strict is true the wizard also calls activate(); strict_activated reflects
	// whether a flip actually went through.
	bool strict_evaluated = false;
	StrictActivationPlan strict_plan;
	bool strict_activated = false;
	StrictActivationResult strict_result;
	// True when activation was requested (options.activate_strict) but the gate refused to flip any
	// setting -- e.g. no confirmation, or remaining violations without the escape hatch. The flip
	// itself is not a fatal error (ok stays true so the report and apply still stand), but a caller
	// enforcing strict activation (a CI run) should treat this as a failure rather than success.
	bool strict_activation_blocked = false;

	// A human-readable, multi-section summary of every stage that ran: the report, the apply
	// outcome (or that it was a preview), and the strict plan/activation. This is what the CLI
	// prints and the editor dialog can show; callers needing numbers read the fields above.
	String summary() const;
};

// The migration wizard orchestrator. run() executes the full report -> apply -> strict-activation
// sequence for the project rooted at p_root, honoring the gates in p_options. It mutates files on
// disk and global ProjectSettings/GDScript cache state only in the stages the options enable, so a
// default (preview-only) run writes nothing. Like the stages it composes, it is NOT safe to call
// concurrently with a live editing session or another run.
class GDScriptMigrationWizard {
public:
	static MigrationWizardResult run(
			const String &p_root,
			const MigrationWizardOptions &p_options = MigrationWizardOptions());
};

#endif // TOOLS_ENABLED
