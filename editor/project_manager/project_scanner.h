/**************************************************************************/
/*  project_scanner.h                                                     */
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

#include "core/os/thread.h"
#include "core/templates/safe_refcount.h"
#include "scene/main/node.h"

class AcceptDialog;

// Asynchronous recursive discovery of Foundry projects (directories containing a
// `project.foundry`) under a chosen folder. Runs the directory walk on a worker
// thread so a large tree does not block the window, shows an indeterminate
// progress modal with a cancel action, and reports the discovered project
// directories through the `scan_finished` signal.
//
// The recursive walk itself is exposed as a static helper so callers that already
// own their own threading (e.g. the project manager list) share the exact same
// discovery logic instead of forking it.
class ProjectScanner : public Node {
	FOUNDRY_CLASS(ProjectScanner, Node);

	struct ScanState {
		Thread *thread = nullptr;
		String path_to_scan;
		List<String> found_projects;
		SafeFlag scan_in_progress;
		// Set when the user cancels: results discovered before the abort are
		// discarded rather than reported, so a cancelled scan has no side effects.
		bool canceled = false;
	};

	ScanState *scan_state = nullptr;
	AcceptDialog *scan_progress = nullptr;

	static void _scan_thread(void *p_scan_state);
	void _scan_finished();

protected:
	void _notification(int p_what);
	static void _bind_methods();

public:
	// Appends every directory containing a `project.foundry` at or below p_path to
	// r_projects. Hidden directories (leading `.`) are skipped. Returns early
	// without error once p_active is cleared so an in-flight scan can be cancelled.
	static void scan_folder_recursive(const String &p_path, List<String> *r_projects, const SafeFlag &p_active);

	// Starts an asynchronous scan of p_path and pops up the progress modal. When the
	// scan completes or is cancelled, emits `scan_finished(PackedStringArray)` with
	// the discovered project directories (empty on cancel). Ignored while a scan is
	// already running.
	void scan_folder(const String &p_path);
	bool is_scanning() const;
	void cancel();

	ProjectScanner();
	~ProjectScanner();
};
