/**************************************************************************/
/*  run_target_manager.h                                                  */
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

#include "editor/run/run_target.h"
#include "editor/run/run_target_platform.h"

#include "core/error/error_list.h"
#include "core/object/ref_counted.h"
#include "core/string/ustring.h"
#include "core/templates/hash_map.h"
#include "core/templates/vector.h"

class EditorExportPreset;

// Owns the project's run targets and the active selection, and resolves a target
// into the concrete inputs a deploy needs: its linked export preset, the device
// to deploy to, the debug flags, and the platform adapter that performs the run.
//
// The manager never talks to a concrete platform; adapters register themselves
// by platform string and the manager dispatches by `RunTarget::platform`. This
// keeps the core reusable (iOS today, Android/desktop later) and testable with a
// fake adapter and a fake preset provider.
class RunTargetManager {
public:
	// Process-wide fallback accessor for editor surfaces that need a manager when
	// the run bar's `EditorRunNative` owner is unavailable. Consumers read it and
	// must tolerate a null result (e.g. headless tooling, or before the editor has
	// set up a fallback manager).
	static RunTargetManager *get_singleton();
	static void set_singleton(RunTargetManager *p_manager);

	// First-open marker for Run Targets configuration. The "Mobile (iOS)"
	// project template seeds a half-configured run target and asks the editor to
	// reveal configuration once, on first open, so the run-target workflow is
	// discoverable. The flag is stored in run_targets.cfg's meta section so no
	// extra file is introduced.
	//
	// `request_show_configuration_on_first_open` writes the flag, preserving any
	// targets and active selection already in the file.
	// `consume_show_configuration_on_first_open` reports whether the flag was set
	// and clears it, so configuration is revealed at most once.
	static Error request_show_configuration_on_first_open(const String &p_config_path);
	static bool consume_show_configuration_on_first_open(const String &p_config_path);

	static Error request_show_dock_on_first_open(const String &p_config_path);
	static bool consume_show_dock_on_first_open(const String &p_config_path);

	// Looks up the export preset a target links to by name. The default
	// production source is `EditorExport`; tests inject a fake so resolution can
	// be exercised without a running editor.
	class PresetProvider {
	public:
		virtual Ref<EditorExportPreset> find_preset_by_name(const String &p_name) const = 0;
		virtual ~PresetProvider() {}
	};

	// The concrete inputs a deploy needs, produced by `resolve`.
	struct ResolvedTarget {
		Ref<EditorExportPreset> preset;
		String device_id;
		int debug_flags = 0;
		RunTargetPlatform *platform_adapter = nullptr;
	};

	// Registers `p_adapter` for `p_platform` (e.g. "ios"). The manager does not
	// take ownership; the caller keeps the adapter alive for the manager's life.
	void register_platform(const String &p_platform, RunTargetPlatform *p_adapter);
	void unregister_platform(const String &p_platform);
	RunTargetPlatform *get_platform(const String &p_platform) const;

	// Loads targets and the persisted active selection from `p_path`. A missing
	// file yields an empty target list (not an error). Returns the load error for
	// a malformed/unreadable file.
	Error load(const String &p_path);
	Error get_last_load_error() const { return last_load_error; }
	bool has_loaded_config_path() const { return !config_path.is_empty(); }

	// Persists the current targets and active selection back to the loaded path.
	Error save();

	const Vector<RunTarget> &get_targets() const { return targets; }
	void set_targets(const Vector<RunTarget> &p_targets);

	// Selects the active target by name. Returns false (and changes nothing) if no
	// target with that name exists. Passing an empty name clears the selection.
	bool set_active_target(const String &p_name);
	bool has_active_target() const;
	String get_active_target_name() const { return active_target_name; }
	// Returns the active target by value. Check `has_active_target()` first; this
	// returns a default-constructed `RunTarget` when nothing is selected.
	RunTarget get_active_target() const;

	// Resolves `p_target` into the inputs a deploy needs. Returns:
	//   - ERR_UNAVAILABLE if no adapter is registered for the target's platform,
	//   - ERR_DOES_NOT_EXIST if the linked export preset no longer exists,
	//   - OK with `r_resolved` populated otherwise.
	Error resolve(const RunTarget &p_target, ResolvedTarget &r_resolved) const;

	// Overrides the preset lookup used by `resolve`. Passing nullptr restores the
	// default `EditorExport`-backed lookup. The manager does not take ownership.
	void set_preset_provider(PresetProvider *p_provider) { preset_provider = p_provider; }

private:
	Vector<RunTarget> targets;
	String active_target_name;
	String config_path;
	Error last_load_error = ERR_UNCONFIGURED;
	HashMap<String, RunTargetPlatform *> adapters;
	PresetProvider *preset_provider = nullptr;

	int find_target_index(const String &p_name) const;
	Ref<EditorExportPreset> find_preset(const String &p_name) const;
	int compute_debug_flags() const;
};
