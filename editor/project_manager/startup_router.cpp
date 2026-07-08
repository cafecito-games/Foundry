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

#include "core/config/project_settings.h"
#include "core/io/config_file.h"
#include "core/io/file_access.h"

#include "editor/project_manager/known_project_store.h"

bool StartupRouter::has_project_config(const String &p_path) {
	return !p_path.is_empty() && FileAccess::exists(p_path.path_join("project.foundry"));
}

bool StartupRouter::is_openable_project(const String &p_path) {
	if (!has_project_config(p_path)) {
		return false;
	}
	const String config = p_path.path_join("project.foundry");
	// Confirm the project config actually parses. A present but malformed project.foundry
	// must not be treated as openable, otherwise it would be auto-opened, rejected by
	// ProjectSettings::setup(), and retried on every launch instead of falling back.
	Ref<ConfigFile> config_file;
	config_file.instantiate();
	if (config_file->load(config) != OK) {
		return false;
	}

	// Only a project that opens without the Project Manager's conversion/warning prompt is
	// eligible for silent auto-open (see the compatibility gate in ProjectManager). A config
	// version of 0 (unversioned) or below the current one needs conversion, and a newer
	// version is incompatible; in every such case the project must go through the projectless
	// shell so the user can choose to convert, not be auto-opened (and possibly retried)
	// every launch.
	const int config_version = (int)config_file->get_value("", "config_version", 0);
	if (config_version != ProjectSettings::CONFIG_VERSION) {
		return false;
	}

	// Features not supported by this build (including a version stamp from a different engine
	// version) also require a warning prompt, so they are not eligible for silent auto-open.
	const PackedStringArray features = config_file->get_value("application", "config/features", PackedStringArray());
	if (!ProjectSettings::get_unsupported_features(features).is_empty()) {
		return false;
	}

	return true;
}

bool StartupRouter::args_request_runtime_launch(const List<String> &p_main_args) {
	for (const String &arg : p_main_args) {
		if (arg == "-s" || arg == "--script" || arg == "--main-loop" ||
				arg == "--scene" || arg == "--run-test-runner") {
			return true;
		}
		// A positional argument that names a scene/resource file directs a runtime launch.
		// The extension gate matches Main::start(): a non-scene positional (e.g. an option
		// value or a project's own custom argument) is not treated as a scene.
		if (arg.length() && arg[0] != '-') {
			if (arg.ends_with(".scn") || arg.ends_with(".tscn") || arg.ends_with(".escn") ||
					arg.ends_with(".res") || arg.ends_with(".tres")) {
				return true;
			}
		}
	}
	return false;
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

	// A project in the working directory takes precedence over a remembered project,
	// mirroring the historical "run Foundry inside a project folder" behavior. Precedence
	// keys on the mere presence of a project.foundry, not on silent openability: when the
	// cwd project needs conversion, the launch is still about that project, so the normal
	// cwd path (open, convert prompt, or run) must handle it rather than silently opening
	// an unrelated remembered project.
	if (has_project_config(p_cwd)) {
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
