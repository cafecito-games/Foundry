/**************************************************************************/
/*  run_targets_panel.h                                                   */
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

#include "editor/docks/editor_dock.h"
#include "editor/run/run_target.h"
#include "editor/run/run_target_platform.h"

class Button;
class LineEdit;
class ItemList;
class Label;
class OptionButton;
class VBoxContainer;
class ConfirmationDialog;
class EditorExportPreset;
class RunTargetManager;
class RunTargetPlatform;

// The Targets dock: configure run targets, edit their signing/device, and view
// the live readiness ladder for the selected target. It is a thin view over the
// shared `RunTargetManager` (run targets + active selection) and the linked
// `EditorExportPreset` (bundle id, signing team) — it invents no new build
// config and never duplicates the readiness logic, which lives in the Doctor.
//
// The manager is consumed from `RunTargetManager::get_singleton()` so the dock
// and the run-bar selector share one source of truth. When no manager has been
// installed yet (e.g. on a develop checkout where that owner has not landed), the
// dock creates and installs a minimal one so it remains functional on its own.
class RunTargetsPanel : public EditorDock {
	GDCLASS(RunTargetsPanel, EditorDock);

public:
	// Validates an iOS bundle identifier the way the Apple export platform does:
	// non-empty and limited to ASCII alphanumerics, '-' and '.'. Pure helper so
	// the live-validity indicator is unit-testable without an editor. Returns the
	// human-readable reason in `r_error` when invalid.
	static bool is_valid_bundle_id(const String &p_identifier, String *r_error = nullptr);

	// Derives a placeholder bundle id from a project name: lowercased, stripped of
	// characters not allowed in an identifier, as `com.example.<slug>`. Used when
	// auto-creating an export preset for a freshly added target.
	static String default_bundle_id_for_project(const String &p_project_name);

	// Index of the first step the user can act on (the first non-OK rung), or -1
	// when every step is satisfied. The dock expands this step's fix hint; pure so
	// the rendering decision is testable independently of the widgets.
	static int first_actionable_step_index(const Vector<ReadinessStep> &p_steps);

	RunTargetsPanel();
	~RunTargetsPanel();

protected:
	void _notification(int p_what);

private:
	// The bundle id / signing team are stored on the linked export preset under
	// these option keys (owned by the Apple export platform).
	static constexpr const char *PRESET_KEY_BUNDLE_ID = "application/bundle_identifier";
	static constexpr const char *PRESET_KEY_TEAM_ID = "application/app_store_team_id";

	RunTargetManager *manager = nullptr;
	bool owns_manager = false;
	// The fallback adapter created alongside an owned manager; kept here so it
	// outlives the manager and is freed in our destructor.
	RunTargetPlatform *owned_adapter = nullptr;

	bool initialized = false;
	bool updating_fields = false; // Guards field writes while loading a selection.
	// False when the project's run_targets.cfg failed to load (malformed/unreadable)
	// and an owned manager therefore has no usable save path; the dock disables
	// mutation rather than silently dropping edits.
	bool config_writable = true;

	// Targets list region.
	ItemList *target_list = nullptr;
	Button *add_button = nullptr;
	Button *remove_button = nullptr;
	Button *rename_button = nullptr;
	ConfirmationDialog *rename_dialog = nullptr;
	LineEdit *rename_field = nullptr;

	// Signing & devices region.
	VBoxContainer *details_container = nullptr;
	OptionButton *signing_mode_option = nullptr;
	LineEdit *bundle_id_edit = nullptr;
	Label *bundle_id_status = nullptr;
	LineEdit *team_id_edit = nullptr;
	OptionButton *device_option = nullptr;

	// Readiness region.
	VBoxContainer *readiness_container = nullptr;
	Button *recheck_button = nullptr;
	Label *readiness_placeholder = nullptr;

	void _ensure_manager();
	int _selected_target_index() const;
	bool _get_selected_target(RunTarget &r_target) const;
	void _commit_target(int p_index, const RunTarget &p_target);
	Ref<EditorExportPreset> _resolve_preset(const RunTarget &p_target) const;

	void _refresh_target_list(int p_select_index = -1);
	void _load_selection_into_fields();
	void _refresh_device_options(const RunTarget &p_target);

	// Returns a copy of `p_target` with its `team_id` filled in from the linked
	// export preset's signing team when the target itself has none, so a team
	// stored only on the preset is not reported as a missing-team readiness
	// failure. The readiness probe reads `RunTarget::team_id`.
	RunTarget _effective_target_for_probe(const RunTarget &p_target) const;

	// True when a target name is unused, or used only by the target at
	// `p_ignore_index` (so a rename can keep its own name).
	bool _is_name_available(const String &p_name, int p_ignore_index) const;

	void _on_target_selected(int p_index);
	void _on_add_pressed();
	void _on_remove_pressed();
	void _on_rename_pressed();
	void _on_rename_confirmed();
	void _on_signing_mode_changed(int p_index);
	void _on_bundle_id_changed(const String &p_text);
	void _on_bundle_id_submitted();
	void _on_team_id_submitted();
	void _on_device_changed(int p_index);
	void _on_recheck_pressed();

	void _update_bundle_id_validity(const String &p_text);
	void _reprobe_selected();
	void _render_readiness(const Vector<ReadinessStep> &p_steps);
	void _clear_readiness_rows();

	// Finds an export preset for the target's platform, creating one if the project
	// has none. Returns a null Ref when no matching export platform is registered.
	Ref<EditorExportPreset> _get_or_create_preset_for_platform(const String &p_platform) const;
};
