/**************************************************************************/
/*  fs_migration_driver.cpp                                               */
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

#include "fs_migration_driver.h"

#ifdef TOOLS_ENABLED

#include "core/config/project_settings.h"

MigrationDriverResult FSMigrationDriver::run(const String &p_root, const MigrationDriverOptions &p_options) {
	MigrationDriverResult result;

	// Stage 1: enumerate the project. A fatal scan failure (unreadable root) aborts the run
	// before anything is touched on disk.
	const ProjectScanResult scan = FSProjectScan::scan(p_root, p_options.scan);
	result.scanned_files = scan.files;
	result.skipped_directories = scan.skipped_directories;
	if (!scan.ok) {
		result.error_message = scan.error_message;
		return result;
	}

	// An empty project is a successful no-op: nothing to infer, nothing changed.
	if (scan.files.is_empty()) {
		result.ok = true;
		result.converged = true;
		return result;
	}

	// Safety guard: the inference stage overwrites scripts in place across the whole
	// project, and that is only reliably reversible from version control. Inspect the
	// working tree before touching any file and, unless the caller has acknowledged the
	// warning, refuse to run over an unversioned, dirty, or indeterminate project. This
	// resolves issue #42's second acceptance criterion.
	//
	// A dry run restores every touched file before returning and never leaves a change
	// on disk, so version-control safety does not apply; the projection report relies on
	// this to run over a working tree it does not own (e.g. an uncommitted CI checkout).
	if (p_options.enforce_vcs_safety_guard && !p_options.inference.dry_run) {
		if (p_options.vcs_state_override != nullptr) {
			// Injected working-tree state: derive the verdict purely through evaluate(),
			// without spawning git. The per-target ignored-file refinement below is a
			// separate git probe and is intentionally skipped here; the injected state
			// alone fixes the verdict.
			result.vcs_guard = ScriptRefactorVCSGuard::evaluate(*p_options.vcs_state_override);
		} else {
			// p_root may be a `res://` path or an absolute OS path; globalize_path() handles both.
			const String project_path = ProjectSettings::get_singleton()->globalize_path(p_root);
			result.vcs_guard = ScriptRefactorVCSGuard::inspect_project(project_path);

			// The tree-level status check ignores `.gitignore`d build artifacts to avoid false
			// positives, but a git-ignored file that is itself a migration target would be
			// overwritten with no version-control recovery. The driver knows the concrete target
			// set, so it checks that here and upgrades an otherwise-clean verdict accordingly.
			if (result.vcs_guard.status == ScriptRefactorVCSGuard::Status::SAFE) {
				Vector<String> globalized_targets;
				for (const String &file : scan.files) {
					globalized_targets.push_back(ProjectSettings::get_singleton()->globalize_path(file));
				}
				bool ignore_check_succeeded = true;
				result.ignored_targets = ScriptRefactorVCSGuard::find_ignored_targets(project_path, globalized_targets, ignore_check_succeeded);
				if (!ignore_check_succeeded) {
					// The ignored-target check could not reach a reliable conclusion. Degrade to
					// UNKNOWN rather than proceeding as safe, so an indeterminate check still warns.
					result.vcs_guard.status = ScriptRefactorVCSGuard::Status::UNKNOWN;
					result.vcs_guard.message = TTR("Could not determine whether any migration targets are excluded from version control. Make sure your working tree is committed or backed up before applying the migration.");
				} else if (!result.ignored_targets.is_empty()) {
					result.vcs_guard.status = ScriptRefactorVCSGuard::Status::IGNORED_TARGETS;
					result.vcs_guard.message = vformat(
							TTR("%d script(s) this migration would change are excluded from version control (.gitignore). They cannot be restored from git after the migration. Commit or un-ignore them, or back up before continuing."),
							result.ignored_targets.size());
				}
			}
		}

		if (result.vcs_guard.should_warn() && !p_options.acknowledge_vcs_warning) {
			result.blocked_by_vcs_guard = true;
			result.error_message = result.vcs_guard.message;
			return result;
		}
	}

	// Stage 2: drive Add Type Annotation to a fixpoint over the discovered files. Each pass
	// collects candidates and routes accepted rewrites through the verification harness, so the
	// committed edit set is verified and the loop stops once it stabilizes.
	const FixpointInferenceResult inference = FSFixpointInference::run(scan.files, p_options.inference);
	if (!inference.ok) {
		// A fatal inference failure may have left some files already committed; surface the
		// error rather than reporting a clean run, but keep whatever the harness recorded.
		result.error_message = inference.error_message;
		result.iterations = inference.iterations;
		result.total_annotations_applied = inference.total_annotations_applied;
		result.inferable = inference.inferable;
		result.converged = inference.converged;
		result.changed_files = inference.changed_files;
		result.skipped = inference.skipped;
		result.unanalyzed_files = inference.unanalyzed_files;
		return result;
	}

	result.iterations = inference.iterations;
	result.total_annotations_applied = inference.total_annotations_applied;
	result.inferable = inference.inferable;
	result.converged = inference.converged;
	result.changed_files = inference.changed_files;
	result.skipped = inference.skipped;
	result.unanalyzed_files = inference.unanalyzed_files;
	result.ok = true;
	return result;
}

#endif // TOOLS_ENABLED
