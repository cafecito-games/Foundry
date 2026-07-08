/**************************************************************************/
/*  startup_router.h                                                      */
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

#include "core/error/error_list.h"
#include "core/string/ustring.h"
#include "core/templates/list.h"

class KnownProjectStore;

// Decides how an interactive editor launch should resolve into a concrete project,
// replacing the old binary editor-vs-project-manager decision with the projectless
// startup routing tree (see docs/superpowers/specs/2026-07-08-projectless-startup-dialog-design.md).
//
// The router is deliberately free of engine/EditorNode state so it can be unit tested
// headlessly: it takes the already-known launch inputs plus a loaded KnownProjectStore
// and returns a routing decision. The caller (main startup) applies the decision by
// choosing which project path to load, or by dropping to the projectless shell.
class StartupRouter {
public:
	enum Route {
		// Load the explicit project path the user pinned on the command line.
		ROUTE_OPEN_EXPLICIT,
		// Load the project found in the current working directory.
		ROUTE_OPEN_CWD,
		// Auto-open the last valid remembered project (see Decision::project_path).
		ROUTE_OPEN_REMEMBERED,
		// No project to open: launch the projectless editor shell.
		ROUTE_PROJECTLESS_SHELL,
		// The user pinned an explicit project path that is invalid: report it and do
		// NOT silently fall back to a remembered project.
		ROUTE_EXPLICIT_INVALID,
	};

	struct Decision {
		Route route = ROUTE_PROJECTLESS_SHELL;
		// Canonical path to open; only meaningful for ROUTE_OPEN_REMEMBERED.
		String project_path;
		// True when resolve_launch() mutated the store in memory (e.g. marked a stale
		// auto-open candidate missing) and the caller should persist it.
		bool store_modified = false;
	};

	// True when the directory contains a `project.foundry` file, regardless of whether it
	// is silently openable. Used to give the working directory precedence over a remembered
	// project even when the cwd project needs conversion.
	static bool has_project_config(const String &p_path);

	// A project directory is openable for silent auto-open when it contains a `project.foundry`
	// that parses, is at the current config version, and uses no build-unsupported features —
	// i.e. it opens without the Project Manager's conversion/warning prompt.
	static bool is_openable_project(const String &p_path);

	// True when the engine launch arguments request a runtime scene/script execution: a
	// positional scene resource path, `--scene`, `-s`/`--script`, `--main-loop`, or
	// `--run-test-runner`. Mirrors the runtime detection in Main::start() so a legacy
	// game/script run (which shares the command-less CLI shape of a bare editor launch)
	// is excluded from projectless editor routing and recents recording.
	static bool args_request_runtime_launch(const List<String> &p_main_args);

	// Resolves the editor launch route.
	//
	// p_explicit_project_requested: the user pinned a specific project path (via
	//   `--project` or a positional path). When true, a remembered project is never
	//   auto-opened, so an invalid explicit path cannot silently open something else.
	// p_explicit_project_valid: that pinned path resolved and will be loaded. Only
	//   consulted when p_explicit_project_requested is true.
	// p_cwd: the working directory, checked for a project when no explicit path was
	//   given.
	// p_store: a loaded known-project store. When the auto-open candidate no longer
	//   resolves it is marked missing (never silently removed) and store_modified is
	//   set on the returned decision.
	static Decision resolve_launch(
			bool p_explicit_project_requested,
			bool p_explicit_project_valid,
			const String &p_cwd,
			KnownProjectStore &p_store);

	// Records a successful project open into the store: stamps it opened (which fronts
	// recents and sets it as the auto-open candidate) and persists the store. Returns
	// the save error.
	static Error record_project_opened(KnownProjectStore &p_store, const String &p_project_path);
};
