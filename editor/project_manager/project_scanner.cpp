/**************************************************************************/
/*  project_scanner.cpp                                                   */
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

#include "project_scanner.h"

#include "core/io/dir_access.h"
#include "scene/gui/box_container.h"
#include "scene/gui/dialogs.h"
#include "scene/gui/label.h"
#include "scene/gui/progress_bar.h"

void ProjectScanner::scan_folder_recursive(const String &p_path, List<String> *r_projects, const SafeFlag &p_active) {
	if (!p_active.is_set()) {
		return;
	}

	Ref<DirAccess> da = DirAccess::create(DirAccess::ACCESS_FILESYSTEM);
	Error error = da->change_dir(p_path);
	ERR_FAIL_COND_MSG(error != OK, vformat("Failed to open the path \"%s\" for scanning (code %d).", p_path, error));

	da->list_dir_begin();
	String n = da->get_next();
	while (!n.is_empty()) {
		if (!p_active.is_set()) {
			da->list_dir_end();
			return;
		}

		if (da->current_is_dir() && n[0] != '.') {
			scan_folder_recursive(da->get_current_dir().path_join(n), r_projects, p_active);
		} else if (n == "project.foundry") {
			r_projects->push_back(da->get_current_dir());
		}
		n = da->get_next();
	}
	da->list_dir_end();
}

void ProjectScanner::_scan_thread(void *p_scan_state) {
	ScanState *scan_state = static_cast<ScanState *>(p_scan_state);

	print_verbose(vformat("Scanning for projects in \"%s\".", scan_state->path_to_scan));
	scan_folder_recursive(scan_state->path_to_scan, &scan_state->found_projects, scan_state->scan_in_progress);
	print_verbose(vformat("Found %d project(s).", scan_state->found_projects.size()));

	scan_state->scan_in_progress.clear();
}

void ProjectScanner::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_PROCESS: {
			if (scan_state == nullptr) {
				set_process(false);
				break;
			}
			// Wait for the worker thread to finish before draining results.
			if (!scan_state->scan_in_progress.is_set()) {
				set_process(false);
				_scan_finished();
			}
		} break;
	}
}

void ProjectScanner::_scan_finished() {
	if (scan_state == nullptr) {
		return;
	}

	// Clearing the flag first tells a still-running worker to abort at its next check.
	scan_state->scan_in_progress.clear();
	if (scan_state->thread != nullptr) {
		scan_state->thread->wait_to_finish();
		memdelete(scan_state->thread);
		scan_state->thread = nullptr;
	}

	if (scan_progress != nullptr) {
		scan_progress->hide();
	}

	// A cancelled scan reports nothing so partial discoveries never reach the store.
	PackedStringArray found;
	if (!scan_state->canceled) {
		for (const String &project_path : scan_state->found_projects) {
			found.push_back(project_path);
		}
	}

	memdelete(scan_state);
	scan_state = nullptr;

	emit_signal(SNAME("scan_finished"), found);
}

void ProjectScanner::scan_folder(const String &p_path) {
	if (scan_state != nullptr) {
		return;
	}

	if (scan_progress == nullptr && is_inside_tree()) {
		scan_progress = memnew(AcceptDialog);
		scan_progress->set_title(TTRC("Scanning"));
		scan_progress->set_ok_button_text(TTRC("Cancel"));

		VBoxContainer *vb = memnew(VBoxContainer);
		scan_progress->add_child(vb);

		Label *label = memnew(Label);
		label->set_text(TTRC("Scanning for projects..."));
		vb->add_child(label);

		ProgressBar *progress = memnew(ProgressBar);
		progress->set_indeterminate(true);
		vb->add_child(progress);

		add_child(scan_progress);
		scan_progress->connect(SceneStringName(confirmed), callable_mp(this, &ProjectScanner::cancel));
		scan_progress->connect("canceled", callable_mp(this, &ProjectScanner::cancel));
	}

	scan_state = memnew(ScanState);
	scan_state->path_to_scan = p_path;
	scan_state->scan_in_progress.set();
	scan_state->thread = memnew(Thread);
	scan_state->thread->start(&ProjectScanner::_scan_thread, scan_state);

	if (scan_progress != nullptr) {
		scan_progress->popup_centered();
	}
	set_process(true);
}

bool ProjectScanner::is_scanning() const {
	return scan_state != nullptr;
}

void ProjectScanner::cancel() {
	if (scan_state == nullptr) {
		return;
	}
	// Mark the scan cancelled so partial discoveries are dropped, then signal the
	// worker to stop; NOTIFICATION_PROCESS drains it on the next frame. Draining
	// here would join a thread mid-callback from the dialog's own signal.
	scan_state->canceled = true;
	scan_state->scan_in_progress.clear();
}

void ProjectScanner::_bind_methods() {
	ADD_SIGNAL(MethodInfo("scan_finished", PropertyInfo(Variant::PACKED_STRING_ARRAY, "found_projects")));
}

ProjectScanner::ProjectScanner() {
	set_process(false);
}

ProjectScanner::~ProjectScanner() {
	if (scan_state != nullptr) {
		scan_state->scan_in_progress.clear();
		if (scan_state->thread != nullptr) {
			scan_state->thread->wait_to_finish();
			memdelete(scan_state->thread);
		}
		memdelete(scan_state);
		scan_state = nullptr;
	}
}
