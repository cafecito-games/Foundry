/**************************************************************************/
/*  editor_run_native.h                                                   */
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

#include "editor/run/run_target_manager.h"
#include "editor/run/run_target_platform.h"

#include "core/templates/hash_map.h"
#include "core/templates/vector.h"
#include "scene/gui/box_container.h"
#include "scene/gui/dialogs.h"
#include "scene/gui/menu_button.h"
#include "scene/gui/rich_text_label.h"

// One row of the run-bar deploy dropdown that comes from the run-target layer
// (configured targets and live-detected devices), as opposed to the legacy
// per-export-platform device rows. Built by `build_menu_model` from the
// manager's targets and the adapters' device lists, so the model is pure and
// unit-testable without constructing the Node or touching real devices.
struct RunTargetMenuEntry {
	enum Kind {
		// A target the user configured in `run_targets.cfg`. Selecting it makes it
		// the active target and deploys to it.
		TARGET,
		// A connected device with no configured target yet. Selecting it routes the
		// user into setup rather than deploying.
		SETUP_DEVICE,
	};

	Kind kind = TARGET;
	String platform; // Run-target platform string, e.g. "ios".
	String label; // Display text (target name, or device name for setup rows).
	String target_name; // For TARGET: the configured target's name.
	String device_id; // Resolved/selected device id; "auto" or empty when unbound.
	ReadinessStep::Status badge = ReadinessStep::OK;
	// For TARGET rows: whether the target currently has a connected device to
	// deploy to. A configured target whose device is unplugged is shown but not
	// runnable (its badge reflects the blocked state).
	bool runnable = true;
};

class EditorRunNative : public HBoxContainer {
	GDCLASS(EditorRunNative, HBoxContainer);

	RichTextLabel *result_dialog_log = nullptr;
	AcceptDialog *result_dialog = nullptr;
	ConfirmationDialog *run_native_confirm = nullptr;
	bool run_confirmed = false;

	MenuButton *remote_debug = nullptr;
	bool first = true;

	int resume_id = -1;

	// Owns the project's run targets and the registered platform adapters. The
	// Targets dock (and any other surface) reaches the same instance through
	// `EditorRunNative::get_singleton()->get_run_target_manager()`.
	RunTargetManager run_target_manager;
	RunTargetPlatform *ios_platform = nullptr; // macOS only; owned, registered as "ios".

	// The run-target rows currently shown in the popup, parallel to the item ids
	// the popup hands back (offset by `RUN_TARGET_ID_BASE`). Rebuilt every poll.
	Vector<RunTargetMenuEntry> run_target_entries;

	static EditorRunNative *singleton;

	void _confirm_run_native();
	void _refresh_run_target_manager();
	void _rebuild_popup();
	void _show_result(const String &p_text, bool p_is_error);
	Error _start_run_target(int p_entry_index);
	Error _deploy_run_target(const RunTarget &p_target);
	bool _find_target(const String &p_name, RunTarget &r_target) const;
	Ref<Texture2D> _badge_icon(ReadinessStep::Status p_status) const;

protected:
	static void _bind_methods();
	void _notification(int p_what);

public:
	// Popup item ids at or above this base index into `run_target_entries`; ids
	// below it are legacy `EditorExport::encode_platform_device_id` device ids
	// (`platform_idx * 10000 + device_idx`). The base sits far above any reachable
	// legacy id — it would take 10000 export platforms to collide — so the two id
	// spaces never overlap, even with many GDExtension export platforms registered.
	static constexpr int RUN_TARGET_ID_BASE = 100'000'000;

	static EditorRunNative *get_singleton() { return singleton; }
	RunTargetManager *get_run_target_manager() { return &run_target_manager; }

	// Pure builder for the run-target portion of the deploy dropdown. Produces one
	// `TARGET` row per configured target (badge sourced from the matching live
	// device, or BLOCKED when its device is not connected) and one `SETUP_DEVICE`
	// row per connected device that no configured target claims. Static and
	// side-effect free so it can be unit-tested headlessly.
	static Vector<RunTargetMenuEntry> build_menu_model(const Vector<RunTarget> &p_targets, const HashMap<String, Vector<RunTargetDevice>> &p_devices_by_platform);

	Error start_run_native(int p_id);
	void resume_run_native();

	bool is_deploy_debug_remote_enabled() const;

	EditorRunNative();
	~EditorRunNative();
};
