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

// Recursively collects `.gd` files under p_dir_path. Pruned directories are recorded in
// p_skipped so the caller can report excluded code. Order is irrelevant here: the caller sorts
// the flat results, so the output is deterministic regardless of filesystem enumeration order.
void scan_directory(const String &p_dir_path, const ProjectScanOptions &p_options, Vector<String> &r_files, Vector<String> &r_skipped) {
	Ref<DirAccess> dir = DirAccess::open(p_dir_path);
	if (dir.is_null()) {
		// An unreadable subdirectory is recorded and skipped; the rest of the walk continues.
		r_skipped.push_back(p_dir_path);
		return;
	}

	Vector<String> subdirectories;

	dir->list_dir_begin();
	for (String entry = dir->get_next(); !entry.is_empty(); entry = dir->get_next()) {
		// Hidden entries (names beginning with `.`, plus `.` and `..`) are never migrated: this
		// covers `.godot`, `.git`, `.import`, and similar tooling directories.
		if (entry.begins_with(".")) {
			continue;
		}

		const String child_path = p_dir_path.path_join(entry);

		if (dir->current_is_dir()) {
			subdirectories.push_back(child_path);
		} else if (entry.get_extension().to_lower() == "gd") {
			r_files.push_back(child_path);
		}
	}
	dir->list_dir_end();

	for (const String &subdirectory : subdirectories) {
		// addons/ holds third-party editor plugins; pruned unless the caller opts in.
		if (!p_options.include_addons && subdirectory.get_file() == "addons") {
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

		scan_directory(subdirectory, p_options, r_files, r_skipped);
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

	scan_directory(p_root, p_options, result.files, result.skipped_directories);

	// Sort so the same project always yields the same ordered work list, independent of the
	// filesystem's enumeration order.
	result.files.sort();
	result.skipped_directories.sort();

	result.ok = true;
	return result;
}

#endif // TOOLS_ENABLED
