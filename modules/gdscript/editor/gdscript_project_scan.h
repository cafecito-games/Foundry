/**************************************************************************/
/*  gdscript_project_scan.h                                               */
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

#include "core/string/ustring.h"
#include "core/templates/vector.h"

// Controls which directories the scan descends into. Everything excluded here is
// excluded by default and re-included only by flipping the corresponding flag, so the
// wizard never silently rewrites third-party code without an explicit opt-in.
struct ProjectScanOptions {
	// addons/ holds third-party editor plugins; excluded by default, opt in to migrate them.
	bool include_addons = false;
	// Directories marked with a `.gdignore` file are excluded by default (the same marker the
	// editor file system honors); set false to descend into them anyway.
	bool respect_gdignore = true;
};

// The ordered work list produced for the migration driver, plus an honest account of what was
// pruned so the wizard can report excluded code rather than hide it.
struct ProjectScanResult {
	bool ok = false; // false only on a fatal precondition (e.g. an unreadable root directory).
	String error_message; // Populated only when !ok.
	Vector<String> files; // Discovered `.gd` paths, deduplicated and ordered deterministically.
	Vector<String> skipped_directories; // Pruned directories (addons, .gdignore, nested projects), ordered.
};

// Read-only project enumerator: the first stage of the migration wizard. It walks a project
// root, collects every `.gd` script the driver should consider, honors the project's ignore
// markers, and excludes third-party code (addons/, .gdignore'd, and nested projects) unless the
// caller opts in. The returned list is sorted so a given project always yields the same ordered
// work list, independent of filesystem enumeration order.
class GDScriptProjectScan {
public:
	// Enumerates `.gd` scripts under p_root. p_root may be a `res://` path or any path DirAccess
	// can open; discovered files keep that root's prefix. Hidden entries (names beginning with `.`
	// or carrying the filesystem hidden attribute) are always skipped, and directory symlinks are
	// never followed so the walk cannot escape the project or loop. The call fails (ok=false, empty
	// files) only when p_root itself cannot be opened or listed; an unreadable subdirectory is
	// recorded in skipped_directories and skipped without aborting the rest of the scan.
	static ProjectScanResult scan(const String &p_root, const ProjectScanOptions &p_options = ProjectScanOptions());
};

#endif // TOOLS_ENABLED
