/**************************************************************************/
/*  editor_run_native.cpp                                                 */
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

#include "editor_run_native.h"

#include "editor/editor_node.h"
#include "editor/export/editor_export.h"
#include "editor/export/editor_export_platform.h"
#include "editor/export/editor_export_platform_apple_embedded.h"
#include "editor/export/editor_export_preset.h"
#include "editor/settings/editor_settings.h"
#include "editor/themes/editor_scale.h"

#ifdef MACOS_ENABLED
#include "editor/run/ios_run_target_platform.h"
#endif

EditorRunNative *EditorRunNative::singleton = nullptr;

// Path to the project's run-target config, parallel to `export_presets.cfg`.
static const char *RUN_TARGETS_CONFIG_PATH = "res://run_targets.cfg";

Vector<RunTargetMenuEntry> EditorRunNative::build_menu_model(const Vector<RunTarget> &p_targets, const HashMap<String, Vector<RunTargetDevice>> &p_devices_by_platform) {
	Vector<RunTargetMenuEntry> entries;

	// Track which (platform, device id) pairs a configured target already claims so
	// the same physical device is not also offered as an unconfigured "set up" row.
	HashMap<String, HashMap<String, bool>> claimed_by_platform;

	// Locate the live device a target deploys to: an explicit device id matches by
	// id; an "auto"/empty selection follows the same first-device rule the adapters
	// use, so the badge the user sees matches what a run would actually target.
	auto find_device = [](const Vector<RunTargetDevice> &p_devices, const String &p_device_id, const RunTargetDevice **r_match) -> bool {
		*r_match = nullptr;
		if (p_devices.is_empty()) {
			return false;
		}
		if (p_device_id.is_empty() || p_device_id == "auto") {
			*r_match = &p_devices[0];
			return true;
		}
		for (int i = 0; i < p_devices.size(); i++) {
			if (p_devices[i].id == p_device_id) {
				*r_match = &p_devices[i];
				return true;
			}
		}
		return false;
	};

	for (int i = 0; i < p_targets.size(); i++) {
		const RunTarget &target = p_targets[i];

		RunTargetMenuEntry entry;
		entry.kind = RunTargetMenuEntry::TARGET;
		entry.platform = target.platform;
		entry.label = target.name;
		entry.target_name = target.name;
		entry.device_id = target.device_id;

		const Vector<RunTargetDevice> *devices = p_devices_by_platform.getptr(target.platform);
		const RunTargetDevice *match = nullptr;
		if (devices != nullptr && find_device(*devices, target.device_id, &match) && match != nullptr) {
			entry.device_id = match->id;
			entry.badge = match->badge;
			entry.runnable = true;
			claimed_by_platform[target.platform][match->id] = true;
		} else {
			// The target is configured but its device is not currently connected.
			entry.badge = ReadinessStep::BLOCKED;
			entry.runnable = false;
		}

		entries.push_back(entry);
	}

	// Any connected device not already represented by a configured target is offered
	// as an unconfigured device the user can set up.
	for (const KeyValue<String, Vector<RunTargetDevice>> &platform_devices : p_devices_by_platform) {
		const String &platform = platform_devices.key;
		const HashMap<String, bool> *claimed = claimed_by_platform.getptr(platform);
		for (const RunTargetDevice &device : platform_devices.value) {
			if (claimed != nullptr && claimed->has(device.id)) {
				continue;
			}
			RunTargetMenuEntry entry;
			entry.kind = RunTargetMenuEntry::SETUP_DEVICE;
			entry.platform = platform;
			entry.label = device.name;
			entry.device_id = device.id;
			entry.badge = device.badge;
			entry.runnable = true;
			entries.push_back(entry);
		}
	}

	return entries;
}

Ref<Texture2D> EditorRunNative::_badge_icon(ReadinessStep::Status p_status) const {
	switch (p_status) {
		case ReadinessStep::OK:
			return get_editor_theme_icon(SNAME("StatusSuccess"));
		case ReadinessStep::ACTION_NEEDED:
			return get_editor_theme_icon(SNAME("StatusWarning"));
		case ReadinessStep::BLOCKED:
		default:
			return get_editor_theme_icon(SNAME("StatusError"));
	}
}

void EditorRunNative::_refresh_run_target_manager() {
#ifdef MACOS_ENABLED
	if (ios_platform == nullptr) {
		ios_platform = memnew(IOSRunTargetPlatform);
		run_target_manager.register_platform("ios", ios_platform);
	}
#endif
	run_target_manager.load(RUN_TARGETS_CONFIG_PATH);
}

void EditorRunNative::_rebuild_popup() {
	PopupMenu *popup = remote_debug->get_popup();
	popup->clear();
	run_target_entries.clear();

	// Gather live devices from every registered adapter, keyed by platform. Only
	// platforms with an adapter (iOS on macOS today) contribute run-target rows;
	// everything else still flows through the legacy per-platform enumeration below.
	HashMap<String, Vector<RunTargetDevice>> devices_by_platform;
	HashMap<String, bool> adapter_platforms;
	{
		HashMap<String, bool> candidate_platforms;
		candidate_platforms["ios"] = true;
		for (const RunTarget &target : run_target_manager.get_targets()) {
			candidate_platforms[target.platform] = true;
		}
		for (const KeyValue<String, bool> &candidate : candidate_platforms) {
			RunTargetPlatform *adapter = run_target_manager.get_platform(candidate.key);
			if (adapter == nullptr) {
				continue;
			}
			adapter_platforms[candidate.key] = true;
			devices_by_platform[candidate.key] = adapter->list_devices();
		}
	}

	run_target_entries = build_menu_model(run_target_manager.get_targets(), devices_by_platform);

	if (!run_target_entries.is_empty()) {
		popup->add_separator(TTRC("Run Targets"));
		const String active = run_target_manager.get_active_target_name();
		for (int i = 0; i < run_target_entries.size(); i++) {
			const RunTargetMenuEntry &entry = run_target_entries[i];
			String label = entry.label;
			if (entry.kind == RunTargetMenuEntry::SETUP_DEVICE) {
				label = vformat(TTR("Set up this device… (%s)"), entry.label);
			}
			popup->add_icon_item(_badge_icon(entry.badge), label, RUN_TARGET_ID_BASE + i);
			popup->set_item_indent(-1, 2);
			if (entry.kind == RunTargetMenuEntry::TARGET && entry.target_name == active) {
				popup->set_item_checked(-1, true);
			}
			if (entry.kind == RunTargetMenuEntry::TARGET && !entry.runnable) {
				popup->set_item_tooltip(-1, TTRC("The device for this target is not connected."));
			}
		}
	}

	// Legacy per-export-platform device enumeration. Platforms already covered by a
	// run-target adapter (iOS on macOS) are skipped here to avoid listing the same
	// devices twice; all other platforms keep their existing behavior unchanged.
	int device_shortcut_id = 1;
	for (int i = 0; i < EditorExport::get_singleton()->get_export_preset_count(); i++) {
		Ref<EditorExportPreset> preset = EditorExport::get_singleton()->get_export_preset(i);
		Ref<EditorExportPlatform> eep = preset->get_platform();
		if (eep.is_null()) {
			continue;
		}
		if (adapter_platforms.has("ios") && Object::cast_to<EditorExportPlatformAppleEmbedded>(eep.ptr()) != nullptr) {
			continue;
		}
		const int platform_idx = EditorExport::get_singleton()->get_export_platform_index_by_name(eep->get_name());
		const int device_count = MIN(eep->get_options_count(), 9000);
		if (device_count > 0 && preset->is_runnable()) {
			popup->add_icon_item(eep->get_run_icon(), eep->get_name(), -1);
			popup->set_item_disabled(-1, true);
			for (int j = 0; j < device_count; j++) {
				popup->add_icon_item(eep->get_option_icon(j), eep->get_option_label(j), EditorExport::encode_platform_device_id(platform_idx, j));
				popup->set_item_tooltip(-1, eep->get_option_tooltip(j));
				popup->set_item_indent(-1, 2);
				if (device_shortcut_id <= 4 && eep->is_option_runnable(j)) {
					// Assign shortcuts for the first 4 devices added in the list.
					popup->set_item_shortcut(-1, ED_GET_SHORTCUT(vformat("remote_deploy/deploy_to_device_%d", device_shortcut_id)), true);
					device_shortcut_id += 1;
				}
			}
		}
	}

	if (popup->get_item_count() == 0) {
		remote_debug->set_disabled(true);
		remote_debug->set_tooltip_text(TTRC("No Remote Deploy export presets configured."));
	} else {
		remote_debug->set_disabled(false);
		remote_debug->set_tooltip_text(TTRC("Remote Deploy"));
	}
}

void EditorRunNative::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_THEME_CHANGED: {
			remote_debug->set_button_icon(get_editor_theme_icon(SNAME("PlayRemote")));
		} break;

		case NOTIFICATION_PROCESS: {
			bool changed = EditorExport::get_singleton()->poll_export_platforms() || first;

			if (changed) {
				_rebuild_popup();
				first = false;
			}
		} break;
	}
}

void EditorRunNative::_confirm_run_native() {
	run_confirmed = true;
	resume_run_native();
}

void EditorRunNative::_show_result(const String &p_text, bool p_is_error) {
	result_dialog_log->clear();
	result_dialog_log->add_text(p_text);
	if (p_is_error) {
		result_dialog->popup_centered_ratio(0.5);
	}
}

Error EditorRunNative::_start_run_target(int p_entry_index) {
	ERR_FAIL_INDEX_V(p_entry_index, run_target_entries.size(), ERR_INVALID_PARAMETER);
	const RunTargetMenuEntry entry = run_target_entries[p_entry_index];

	if (entry.kind == RunTargetMenuEntry::SETUP_DEVICE) {
		// No configured target to deploy to yet: route the user into setup. The
		// Targets dock listens for this; until it exists the message explains the
		// next step rather than silently doing nothing.
		emit_signal(SNAME("setup_target_requested"), entry.platform, entry.device_id, entry.label);
		_show_result(vformat(TTR("\"%s\" is connected but not set up as a run target yet.\nOpen the Targets panel to configure signing and add it as a run target."), entry.label), true);
		return OK;
	}

	// Resolve the configured target to its preset, device, debug flags, and adapter.
	RunTarget target;
	bool found = false;
	for (const RunTarget &candidate : run_target_manager.get_targets()) {
		if (candidate.name == entry.target_name) {
			target = candidate;
			found = true;
			break;
		}
	}
	if (!found) {
		_show_result(vformat(TTR("Run target \"%s\" no longer exists."), entry.target_name), true);
		return ERR_DOES_NOT_EXIST;
	}

	// Selecting a target makes it the active selection, persisted for next session.
	run_target_manager.set_active_target(target.name);
	run_target_manager.save();

	RunTargetManager::ResolvedTarget resolved;
	const Error resolve_error = run_target_manager.resolve(target, resolved);
	if (resolve_error != OK || resolved.platform_adapter == nullptr || resolved.preset.is_null()) {
		_show_result(vformat(TTR("Cannot run \"%s\": its export preset \"%s\" is missing or its platform has no adapter."), target.name, target.export_preset), true);
		return resolve_error != OK ? resolve_error : ERR_UNAVAILABLE;
	}

	if (!EditorNode::get_singleton()->ensure_main_scene(true)) {
		return OK;
	}

	// Surface the Doctor's next step instead of letting a deploy fail silently: if
	// anything in the readiness ladder is unmet, show the first actionable rung and
	// stop here so the user knows exactly what to fix.
	const Vector<ReadinessStep> ladder = resolved.platform_adapter->probe_readiness(target);
	for (const ReadinessStep &step : ladder) {
		if (step.status != ReadinessStep::OK) {
			String message = vformat(TTR("\"%s\" is not ready to run yet.\n\n%s\n%s"), target.name, step.title, step.detail);
			if (!step.fix_hint.is_empty()) {
				message += "\n\n" + step.fix_hint;
			}
			_show_result(message, true);
			return ERR_UNAVAILABLE;
		}
	}

	resolved.preset->update_value_overrides();

	// Wire the running app back to the editor debugger exactly like the legacy
	// native-run path, then hand off to the adapter's deploy.
	emit_signal(SNAME("native_run"), resolved.preset);

	const Error run_error = resolved.platform_adapter->run(target, resolved.debug_flags);
	if (run_error != OK) {
		_show_result(vformat(TTR("Deploying \"%s\" failed. See the Output log for details."), target.name), true);
	}
	return run_error;
}

Error EditorRunNative::start_run_native(int p_id) {
	ERR_FAIL_COND_V(p_id < 0, FAILED);

	if (p_id >= RUN_TARGET_ID_BASE) {
		return _start_run_target(p_id - RUN_TARGET_ID_BASE);
	}

	const int platform = EditorExport::decode_platform_from_id(p_id);
	const int idx = EditorExport::decode_device_from_id(p_id);
	resume_id = p_id;

	if (!EditorNode::get_singleton()->ensure_main_scene(true)) {
		return OK;
	}

	Ref<EditorExportPlatform> eep = EditorExport::get_singleton()->get_export_platform(platform);
	ERR_FAIL_COND_V(eep.is_null(), ERR_UNAVAILABLE);

	Ref<EditorExportPreset> preset;

	for (int i = 0; i < EditorExport::get_singleton()->get_export_preset_count(); i++) {
		Ref<EditorExportPreset> ep = EditorExport::get_singleton()->get_export_preset(i);
		if (ep->is_runnable() && ep->get_platform() == eep) {
			preset = ep;
			break;
		}
	}

	if (preset.is_null()) {
		EditorNode::get_singleton()->show_warning(TTR("No runnable export preset found for this platform.\nPlease add a runnable preset in the Export menu or define an existing preset as runnable."));
		return ERR_UNAVAILABLE;
	}

	String architecture = eep->get_device_architecture(idx);
	if (!run_confirmed && !architecture.is_empty()) {
		String preset_arch = "architectures/" + architecture;
		bool is_arch_enabled = preset->get(preset_arch);

		if (!is_arch_enabled) {
			run_native_confirm->set_text(vformat(TTR("Warning: The CPU architecture \"%s\" is not active in your export preset.\n\nRun \"Remote Deploy\" anyway?"), architecture));
			run_native_confirm->popup_centered();
			return OK;
		}
	}
	run_confirmed = false;

	preset->update_value_overrides();

	if (eep->is_option_runnable(idx)) {
		emit_signal(SNAME("native_run"), preset);
	}

	BitField<EditorExportPlatform::DebugFlags> flags = 0;

	bool deploy_debug_remote = is_deploy_debug_remote_enabled();
	bool deploy_dumb = EditorSettings::get_singleton()->get_project_metadata("debug_options", "run_file_server", false);
	bool debug_collisions = EditorSettings::get_singleton()->get_project_metadata("debug_options", "run_debug_collisions", false);
	bool debug_navigation = EditorSettings::get_singleton()->get_project_metadata("debug_options", "run_debug_navigation", false);

	if (deploy_debug_remote) {
		flags.set_flag(EditorExportPlatform::DEBUG_FLAG_REMOTE_DEBUG);
	}
	if (deploy_dumb) {
		flags.set_flag(EditorExportPlatform::DEBUG_FLAG_DUMB_CLIENT);
	}
	if (debug_collisions) {
		flags.set_flag(EditorExportPlatform::DEBUG_FLAG_VIEW_COLLISIONS);
	}
	if (debug_navigation) {
		flags.set_flag(EditorExportPlatform::DEBUG_FLAG_VIEW_NAVIGATION);
	}

	eep->clear_messages();
	Error err = eep->run(preset, idx, flags);
	result_dialog_log->clear();
	if (eep->fill_log_messages(result_dialog_log, err)) {
		if (eep->get_worst_message_type() >= EditorExportPlatform::EXPORT_MESSAGE_ERROR) {
			result_dialog->popup_centered_ratio(0.5);
		}
	}
	return err;
}

void EditorRunNative::resume_run_native() {
	start_run_native(resume_id);
}

void EditorRunNative::_bind_methods() {
	ADD_SIGNAL(MethodInfo("native_run", PropertyInfo(Variant::OBJECT, "preset", PROPERTY_HINT_RESOURCE_TYPE, "EditorExportPreset")));
	ADD_SIGNAL(MethodInfo("setup_target_requested", PropertyInfo(Variant::STRING, "platform"), PropertyInfo(Variant::STRING, "device_id"), PropertyInfo(Variant::STRING, "device_name")));
}

bool EditorRunNative::is_deploy_debug_remote_enabled() const {
	return EditorSettings::get_singleton()->get_project_metadata("debug_options", "run_deploy_remote_debug", true);
}

EditorRunNative::EditorRunNative() {
	singleton = this;

	ED_SHORTCUT("remote_deploy/deploy_to_device_1", TTRC("Deploy to First Device in List"), KeyModifierMask::SHIFT | Key::F5);
	ED_SHORTCUT_OVERRIDE("remote_deploy/deploy_to_device_1", "macos", KeyModifierMask::META | KeyModifierMask::SHIFT | Key::B);
	ED_SHORTCUT("remote_deploy/deploy_to_device_2", TTRC("Deploy to Second Device in List"));
	ED_SHORTCUT("remote_deploy/deploy_to_device_3", TTRC("Deploy to Third Device in List"));
	ED_SHORTCUT("remote_deploy/deploy_to_device_4", TTRC("Deploy to Fourth Device in List"));

	_refresh_run_target_manager();

	remote_debug = memnew(MenuButton);
	remote_debug->set_flat(false);
	remote_debug->set_theme_type_variation("RunBarButton");
	remote_debug->get_popup()->set_auto_translate_mode(AUTO_TRANSLATE_MODE_DISABLED);
	remote_debug->get_popup()->connect(SceneStringName(id_pressed), callable_mp(this, &EditorRunNative::start_run_native));
	remote_debug->set_tooltip_text(TTRC("Remote Deploy"));
	remote_debug->set_disabled(true);

	add_child(remote_debug);

	result_dialog = memnew(AcceptDialog);
	result_dialog->set_title(TTR("Project Run"));
	result_dialog_log = memnew(RichTextLabel);
	result_dialog_log->set_custom_minimum_size(Size2(300, 80) * EDSCALE);
	result_dialog->add_child(result_dialog_log);

	add_child(result_dialog);
	result_dialog->hide();

	run_native_confirm = memnew(ConfirmationDialog);
	add_child(run_native_confirm);
	run_native_confirm->connect(SceneStringName(confirmed), callable_mp(this, &EditorRunNative::_confirm_run_native));

	set_process(true);
}

EditorRunNative::~EditorRunNative() {
	if (ios_platform != nullptr) {
		run_target_manager.unregister_platform("ios");
		memdelete(ios_platform);
		ios_platform = nullptr;
	}
	if (singleton == this) {
		singleton = nullptr;
	}
}
