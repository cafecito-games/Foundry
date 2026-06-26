/**************************************************************************/
/*  gdscript_migration_driver.cpp                                         */
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

#include "gdscript_migration_driver.h"

#ifdef TOOLS_ENABLED

MigrationDriverResult GDScriptMigrationDriver::run(const String &p_root, const MigrationDriverOptions &p_options) {
	MigrationDriverResult result;

	// Stage 1: enumerate the project. A fatal scan failure (unreadable root) aborts the run
	// before anything is touched on disk.
	const ProjectScanResult scan = GDScriptProjectScan::scan(p_root, p_options.scan);
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

	// Stage 2: drive Add Type Annotation to a fixpoint over the discovered files. Each pass
	// collects candidates and routes accepted rewrites through the verification harness, so the
	// committed edit set is verified and the loop stops once it stabilizes.
	const FixpointInferenceResult inference = GDScriptFixpointInference::run(scan.files, p_options.inference);
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
