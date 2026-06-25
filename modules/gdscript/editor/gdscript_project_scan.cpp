/**************************************************************************/
/*  gdscript_project_scan.cpp                                             */
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

#include "gdscript_project_scan.h"

#ifdef TOOLS_ENABLED

#include "core/io/dir_access.h"
#include "core/io/file_access.h"

namespace {

// Recursively collects `.gd` files under p_dir_path. p_at_root is true only for the scan root,
// so root-only rules (the project add-ons folder) apply where they should. Pruned directories are
// recorded in p_skipped so the caller can report excluded code. Order is irrelevant here: the
// caller sorts the flat results, so the output is deterministic regardless of enumeration order.
void scan_directory(const String &p_dir_path, bool p_at_root, const ProjectScanOptions &p_options, Vector<String> &r_files, Vector<String> &r_skipped) {
	Ref<DirAccess> dir = DirAccess::open(p_dir_path);
	if (dir.is_null() || dir->list_dir_begin() != OK) {
		// A directory that cannot be entered or listed (e.g. unreadable permissions) is recorded
		// and skipped rather than silently reported as empty; the rest of the walk continues.
		r_skipped.push_back(p_dir_path);
		return;
	}

	Vector<String> subdirectories;

	for (String entry = dir->get_next(); !entry.is_empty(); entry = dir->get_next()) {
		// Hidden entries are never migrated: names beginning with `.` (covering `.godot`, `.git`,
		// `.import`, and `.` / `..`) plus filesystem-hidden entries (the Windows hidden attribute).
		if (entry.begins_with(".") || dir->current_is_hidden()) {
			continue;
		}

		const String child_path = p_dir_path.path_join(entry);

		if (dir->current_is_dir()) {
			// A directory symlink can point outside the project or back into an ancestor, which
			// would let the walk escape the project or loop forever. Record it and never follow it.
			if (dir->is_link(child_path)) {
				r_skipped.push_back(child_path);
				continue;
			}
			subdirectories.push_back(child_path);
		} else if (entry.get_extension().to_lower() == "gd") {
			r_files.push_back(child_path);
		}
	}
	dir->list_dir_end();

	for (const String &subdirectory : subdirectories) {
		// The project add-ons folder (res://addons) holds third-party editor plugins; pruned
		// unless the caller opts in. Only the root-level folder is third-party by convention, so a
		// first-party directory that merely happens to be named `addons` deeper in the tree is kept.
		if (p_at_root && !p_options.include_addons && subdirectory.get_file() == "addons") {
			r_skipped.push_back(subdirectory);
			continue;
		}

		// A nested project is a self-contained unit migrated on its own, never as part of this one.
		if (FileAccess::exists(subdirectory.path_join("project.godot"))) {
			r_skipped.push_back(subdirectory);
			continue;
		}

		// Honor the editor's ignore marker so vendored/generated trees stay untouched.
		if (p_options.respect_gdignore && FileAccess::exists(subdirectory.path_join(".gdignore"))) {
			r_skipped.push_back(subdirectory);
			continue;
		}

		scan_directory(subdirectory, false, p_options, r_files, r_skipped);
	}
}

} // namespace

ProjectScanResult GDScriptProjectScan::scan(const String &p_root, const ProjectScanOptions &p_options) {
	ProjectScanResult result;

	Ref<DirAccess> root_dir = DirAccess::open(p_root);
	if (root_dir.is_null()) {
		// An unreadable root is the one fatal precondition: there is no work list to produce.
		result.error_message = vformat("Cannot open project root '%s'.", p_root);
		return result;
	}

	scan_directory(p_root, true, p_options, result.files, result.skipped_directories);

	// Sort so the same project always yields the same ordered work list, independent of the
	// filesystem's enumeration order.
	result.files.sort();
	result.skipped_directories.sort();

	result.ok = true;
	return result;
}

#endif // TOOLS_ENABLED
