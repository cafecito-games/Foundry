/**************************************************************************/
/*  startup_router.cpp                                                    */
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

#include "startup_router.h"

#include "core/io/file_access.h"

#include "editor/project_manager/known_project_store.h"

bool StartupRouter::is_openable_project(const String &p_path) {
	if (p_path.is_empty()) {
		return false;
	}
	return FileAccess::exists(p_path.path_join("project.foundry"));
}

StartupRouter::Decision StartupRouter::resolve_launch(
		bool p_explicit_project_requested,
		bool p_explicit_project_valid,
		const String &p_cwd,
		KnownProjectStore &p_store) {
	Decision decision;

	// An explicit path pins the outcome: a valid one opens directly, and an invalid one
	// must be reported rather than silently swapped for a remembered project.
	if (p_explicit_project_requested) {
		decision.route = p_explicit_project_valid ? ROUTE_OPEN_EXPLICIT : ROUTE_EXPLICIT_INVALID;
		return decision;
	}

	// A project in the working directory is opened directly, mirroring the historical
	// "run Foundry inside a project folder" behavior, and taking precedence over the
	// remembered project.
	if (is_openable_project(p_cwd)) {
		decision.route = ROUTE_OPEN_CWD;
		return decision;
	}

	// Fall back to the last valid remembered project when one resolves on disk.
	const String candidate = p_store.get_auto_open_path();
	if (!candidate.is_empty()) {
		if (is_openable_project(candidate)) {
			decision.route = ROUTE_OPEN_REMEMBERED;
			decision.project_path = candidate;
			return decision;
		}

		// The remembered project is gone or invalid: mark it missing (never silently
		// removed) so the projectless shell can surface it, then drop to that shell.
		p_store.mark_project_missing(candidate);
		decision.store_modified = true;
	}

	decision.route = ROUTE_PROJECTLESS_SHELL;
	return decision;
}

Error StartupRouter::record_project_opened(KnownProjectStore &p_store, const String &p_project_path) {
	p_store.mark_project_opened(p_project_path);
	return p_store.save();
}
