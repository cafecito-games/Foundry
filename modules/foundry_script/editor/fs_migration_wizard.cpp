/**************************************************************************/
/*  fs_migration_wizard.cpp                                               */
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

#ifdef TOOLS_ENABLED

#include "fs_migration_wizard.h"

#include "core/string/string_builder.h"

String MigrationWizardResult::summary() const {
	StringBuilder builder;

	if (!error_message.is_empty()) {
		builder.append("Migration wizard error: ");
		builder.append(error_message);
		builder.append("\n");
	}

	builder.append("=== Migration report ===\n");
	builder.append(report.format());
	builder.append("\n");

	builder.append("=== Apply ===\n");
	if (!applied) {
		if (blocked_by_vcs_guard) {
			builder.append("Apply blocked by version-control safety guard: ");
			builder.append(apply_result.vcs_guard.message);
			builder.append("\nRe-run with the version-control acknowledgment to proceed.\n");
		} else if (apply_requested) {
			// The apply stage was requested but the driver failed. The driver may fail after some
			// files were already committed, so do NOT claim nothing changed; point at the partial
			// edit set instead.
			builder.append("Apply failed: ");
			builder.append(apply_result.error_message.is_empty() ? error_message : apply_result.error_message);
			builder.append("\n");
			if (!apply_result.changed_files.is_empty()) {
				builder.append(vformat("Warning: %d file(s) may have already been modified before the failure; review your version control.\n",
						apply_result.changed_files.size()));
			}
		} else {
			builder.append("Preview only -- no files were modified. Re-run with apply enabled to commit edits.\n");
		}
	} else {
		builder.append(vformat("Applied %d annotation(s) across %d file(s) in %d iteration(s).\n",
				apply_result.total_annotations_applied, apply_result.changed_files.size(), apply_result.iterations));
		if (!apply_result.converged) {
			builder.append("Warning: the fixpoint did not converge within the iteration bound; the edit set may be incomplete.\n");
		}
		if (!apply_result.unanalyzed_files.is_empty()) {
			builder.append(vformat("%d file(s) could not be analyzed and were left untouched.\n", apply_result.unanalyzed_files.size()));
		}
	}
	builder.append("\n");

	if (strict_evaluated) {
		builder.append("=== Strict activation ===\n");
		if (!strict_plan.ok) {
			builder.append("Strict plan error: ");
			builder.append(strict_plan.error_message);
			builder.append("\n");
		} else if (strict_plan.clean) {
			builder.append("No strict-mode violations remain; the requested strict flags can be enabled cleanly.\n");
		} else {
			builder.append(vformat("%d strict-mode violation(s) remain.\n", strict_plan.violations.size()));
		}

		if (strict_activated) {
			if (strict_result.persisted) {
				builder.append("Strict settings were activated and persisted to the project.\n");
			} else {
				builder.append("Strict settings are live for this session but could not be saved to the project; ");
				builder.append("they will not survive an editor restart: ");
				builder.append(strict_result.persist_error);
				builder.append("\n");
			}
			if (strict_result.override_masked) {
				builder.append("Warning: a per-feature override still masks the effective value: ");
				builder.append(strict_result.effective_warning);
				builder.append("\n");
			}
		} else if (strict_result.ok && !strict_result.blocked_reason.is_empty()) {
			builder.append("Strict settings were not activated: ");
			builder.append(strict_result.blocked_reason);
			builder.append("\n");
		} else if (!strict_result.ok && !strict_result.error_message.is_empty()) {
			builder.append("Strict activation error: ");
			builder.append(strict_result.error_message);
			builder.append("\n");
		} else {
			builder.append("Preview only -- the strict settings were not changed.\n");
		}
		builder.append("\n");
	}

	return builder.as_string();
}

bool MigrationWizardResult::succeeded() const {
	if (!ok) {
		return false;
	}
	// A requested strict activation that the gate refused leaves the settings unchanged.
	if (strict_activation_blocked) {
		return false;
	}
	if (strict_activated) {
		// An activation that flipped the in-memory setting but failed to persist is lost on exit.
		if (!strict_result.persisted) {
			return false;
		}
		// An activation a per-feature override still masks did not actually make strict mode live.
		if (strict_result.override_masked) {
			return false;
		}
	}
	return true;
}

MigrationWizardResult FSMigrationWizard::run(const String &p_root, const MigrationWizardOptions &p_options) {
	MigrationWizardResult result;

	// Stage 1: the dry-run report. Always runs first and is what the user/CI sees before anything is
	// committed. By default it is the truly read-only single-pass snapshot. The accurate projection
	// matches what apply commits but is NOT side-effect free -- it drives the migration driver's
	// dry-run path, which writes each pass to disk and restores it while SKIPPING the VCS guard --
	// so the wizard never forces it on (not even for an apply run, whose own guarded apply stage
	// produces the ground-truth edit set in apply_result). It is used only when the caller has
	// explicitly opted into those side effects.
	MigrationReportOptions report_options;
	report_options.scan = p_options.scan;
	report_options.strict_null_checks = p_options.strict_null_checks;
	report_options.strict_dynamic_checks = p_options.strict_dynamic_checks;
	report_options.projection = p_options.projection;
	result.report = FSMigrationReport::generate(p_root, report_options);
	if (!result.report.ok) {
		result.ok = false;
		result.error_message = result.report.error_message;
		return result;
	}

	// Stage 2: the atomic apply. Runs only when requested; otherwise the wizard stops as a
	// preview. A VCS-blocked apply short-circuits the strict stage since nothing was written.
	result.apply_requested = p_options.apply;
	if (p_options.apply) {
		MigrationDriverOptions driver_options;
		driver_options.scan = p_options.scan;
		driver_options.inference.strict_null_checks = p_options.strict_null_checks;
		driver_options.inference.strict_dynamic_checks = p_options.strict_dynamic_checks;
		driver_options.enforce_vcs_safety_guard = p_options.enforce_vcs_safety_guard;
		driver_options.acknowledge_vcs_warning = p_options.acknowledge_vcs_warning;

		result.apply_result = FSMigrationDriver::run(p_root, driver_options);
		result.blocked_by_vcs_guard = result.apply_result.blocked_by_vcs_guard;
		if (!result.apply_result.ok) {
			result.applied = false;
			result.ok = false;
			result.error_message = result.apply_result.blocked_by_vcs_guard
					? result.apply_result.vcs_guard.message
					: result.apply_result.error_message;
			return result;
		}
		result.applied = true;
	}

	// Stage 3: the gated strict-settings activation. The read-only plan always runs after the
	// report so the caller can preview the cost; the flip happens only when activate_strict is on
	// and the activation's own gates (confirmation, clean report or allow-with-violations) pass.
	const bool any_strict_requested = p_options.strict_null_checks || p_options.strict_dynamic_checks;
	if (any_strict_requested) {
		StrictActivationRequest strict_request;
		strict_request.strict_null_checks = p_options.strict_null_checks;
		strict_request.strict_dynamic_checks = p_options.strict_dynamic_checks;
		strict_request.confirmed = p_options.confirm_strict_activation;
		strict_request.allow_with_violations = p_options.allow_strict_with_violations;

		const Vector<String> paths = result.report.scanned_files;
		result.strict_plan = FSStrictActivation::evaluate(paths, strict_request);
		result.strict_evaluated = true;

		if (p_options.activate_strict) {
			result.strict_result = FSStrictActivation::activate(paths, strict_request);
			result.strict_activated = result.strict_result.activated;
			if (!result.strict_result.ok) {
				result.ok = false;
				result.error_message = result.strict_result.error_message;
				return result;
			}
			// The flip was requested but the gate (confirmation / clean report) refused it. Not a
			// fatal error, but flagged so a caller enforcing activation can fail rather than report
			// a false success.
			result.strict_activation_blocked = !result.strict_result.activated;
		}
	}

	// Persist the manual follow-up punch-list when a path was requested, so a scripted run leaves a
	// regenerable artifact alongside the project. Written LAST, after the apply stage, so that when
	// the follow-up path is inside the project it does not dirty the working tree before the apply's
	// version-control safety guard inspects it (which would otherwise block an otherwise-clean run).
	if (!p_options.follow_up_path.is_empty()) {
		const Error follow_up_error = FSMigrationReport::write_follow_up(result.report, p_options.follow_up_path);
		if (follow_up_error != OK) {
			result.ok = false;
			result.error_message = vformat("Failed to write follow-up report to '%s' (error %d).", p_options.follow_up_path, follow_up_error);
			return result;
		}
	}

	result.ok = true;
	return result;
}

#endif // TOOLS_ENABLED
