/**************************************************************************/
/*  run_targets_panel.cpp                                                 */
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

#include "run_targets_panel.h"

#include "editor/editor_string_names.h"
#include "editor/export/editor_export.h"
#include "editor/export/editor_export_platform.h"
#include "editor/export/editor_export_preset.h"
#include "editor/run/editor_run_native.h"
#include "editor/run/ios_run_target_platform.h"
#include "editor/run/run_target_manager.h"
#include "editor/themes/editor_scale.h"

#include "core/config/project_settings.h"
#include "core/string/char_utils.h"
#include "scene/gui/box_container.h"
#include "scene/gui/button.h"
#include "scene/gui/dialogs.h"
#include "scene/gui/item_list.h"
#include "scene/gui/label.h"
#include "scene/gui/line_edit.h"
#include "scene/gui/option_button.h"
#include "scene/gui/separator.h"
#include "scene/gui/split_container.h"

namespace {

constexpr const char *RUN_TARGETS_CONFIG_PATH = "res://run_targets.cfg";
constexpr const char *IOS_PLATFORM = "ios";
constexpr const char *IOS_EXPORT_PLATFORM_OS_NAME = "iOS";
constexpr const char *DEFAULT_SIGNING_MODE = "automatic";
constexpr const char *AUTO_DEVICE_ID = "auto";

// Item metadata marking the team picker's manual-entry escape hatch. A real
// Apple team id is a non-empty alphanumeric string, so this sentinel can never
// collide with one.
constexpr const char *MANUAL_TEAM_ITEM = "\x01manual";

// Maps the lowercase `RunTarget::platform` string to the export platform's
// `get_os_name()` so a target can find its export platform. Only iOS exists
// today; the layer admits more without touching the panel's call sites.
String export_os_name_for_platform(const String &p_platform) {
	if (p_platform == IOS_PLATFORM) {
		return IOS_EXPORT_PLATFORM_OS_NAME;
	}
	return String();
}

} // namespace

bool RunTargetsPanel::is_valid_bundle_id(const String &p_identifier, String *r_error) {
	if (p_identifier.is_empty()) {
		if (r_error) {
			*r_error = TTR("Bundle identifier is missing.");
		}
		return false;
	}
	for (int i = 0; i < p_identifier.length(); i++) {
		const char32_t c = p_identifier[i];
		if (!(is_ascii_alphanumeric_char(c) || c == '-' || c == '.')) {
			if (r_error) {
				*r_error = vformat(TTR("The character '%s' is not allowed in the bundle identifier."), String::chr(c));
			}
			return false;
		}
	}
	return true;
}

String RunTargetsPanel::default_bundle_id_for_project(const String &p_project_name) {
	String slug;
	for (int i = 0; i < p_project_name.length(); i++) {
		const char32_t c = p_project_name[i];
		if (is_ascii_alphanumeric_char(c)) {
			slug += String::chr(c);
		}
	}
	slug = slug.to_lower();
	if (slug.is_empty()) {
		slug = "game";
	}
	return "com.example." + slug;
}

int RunTargetsPanel::first_actionable_step_index(const Vector<ReadinessStep> &p_steps) {
	for (int i = 0; i < p_steps.size(); i++) {
		if (p_steps[i].status != ReadinessStep::Status::OK) {
			return i;
		}
	}
	return -1;
}

String RunTargetsPanel::unique_target_name(const String &p_preferred, const Vector<RunTarget> &p_existing) {
	String base = p_preferred.strip_edges();
	if (base.is_empty()) {
		base = TTR("New Target");
	}
	String candidate = base;
	int suffix = 2;
	bool collision = true;
	while (collision) {
		collision = false;
		for (int i = 0; i < p_existing.size(); i++) {
			if (p_existing[i].name == candidate) {
				collision = true;
				break;
			}
		}
		if (collision) {
			candidate = base + " " + itos(suffix++);
		}
	}
	return candidate;
}

RunTarget RunTargetsPanel::make_device_setup_target(const String &p_platform, const String &p_device_id, const String &p_device_name, const String &p_export_preset, const Vector<RunTarget> &p_existing) {
	String preferred = p_device_name.strip_edges();
	if (preferred.is_empty()) {
		preferred = p_device_id.strip_edges();
	}

	RunTarget target;
	target.name = unique_target_name(preferred, p_existing);
	target.platform = p_platform;
	target.export_preset = p_export_preset;
	target.device_id = p_device_id.is_empty() ? String(AUTO_DEVICE_ID) : p_device_id;
	target.signing_mode = DEFAULT_SIGNING_MODE;
	return target;
}

void RunTargetsPanel::_ensure_manager() {
	if (manager != nullptr) {
		return;
	}

	auto mark_config_read_only = [this](Error p_load_error) {
		// A malformed/unreadable run_targets.cfg leaves the manager without a save
		// path; editing would silently lose changes, so keep the panel read-only and
		// tell the user instead of pretending edits persist.
		config_writable = false;
		ERR_PRINT(vformat("Run Targets: could not load \"%s\" (error %d). The panel is read-only until the file is fixed.", String(RUN_TARGETS_CONFIG_PATH), p_load_error));
	};

	auto use_shared_manager = [this, &mark_config_read_only](RunTargetManager *p_manager) {
		if (p_manager == nullptr) {
			return false;
		}

		manager = p_manager;
		if (manager->get_last_load_error() != OK || !manager->has_loaded_config_path()) {
			mark_config_read_only(manager->get_last_load_error());
		}
		return true;
	};

	EditorRunNative *run_native = EditorRunNative::get_singleton();
	if (run_native != nullptr && use_shared_manager(run_native->get_run_target_manager())) {
		// The run bar's deploy dropdown owns the live manager; consume it without
		// taking ownership so configuration edits update the same state.
		return;
	}

	if (use_shared_manager(RunTargetManager::get_singleton())) {
		// Fallback compatibility for callers that still publish a manager through
		// the legacy singleton path.
		return;
	}

	// No shared manager is available yet. Create a minimal one so the panel works
	// on its own, load the project's targets, register the iOS adapter on macOS,
	// and publish it so other fallback consumers share this single instance.
	manager = memnew(RunTargetManager);
	owns_manager = true;
	const Error load_error = manager->load(RUN_TARGETS_CONFIG_PATH);
	if (load_error != OK) {
		mark_config_read_only(load_error);
	}

#ifdef MACOS_ENABLED
	owned_adapter = memnew(IOSRunTargetPlatform);
	manager->register_platform(IOS_PLATFORM, owned_adapter);
#endif

	RunTargetManager::set_singleton(manager);
}

int RunTargetsPanel::_selected_target_index() const {
	const Vector<int> selected = target_list->get_selected_items();
	if (selected.is_empty()) {
		return -1;
	}
	const int index = selected[0];
	if (index < 0 || index >= manager->get_targets().size()) {
		return -1;
	}
	return index;
}

bool RunTargetsPanel::_get_selected_target(RunTarget &r_target) const {
	const int index = _selected_target_index();
	if (index < 0) {
		return false;
	}
	r_target = manager->get_targets()[index];
	return true;
}

void RunTargetsPanel::_commit_target(int p_index, const RunTarget &p_target) {
	if (!config_writable) {
		return;
	}

	Vector<RunTarget> targets = manager->get_targets();
	if (p_index < 0 || p_index >= targets.size()) {
		return;
	}
	targets.write[p_index] = p_target;
	manager->set_targets(targets);
	manager->save();
}

Ref<EditorExportPreset> RunTargetsPanel::_resolve_preset(const RunTarget &p_target) const {
	EditorExport *editor_export = EditorExport::get_singleton();
	if (editor_export == nullptr) {
		return Ref<EditorExportPreset>();
	}
	for (int i = 0; i < editor_export->get_export_preset_count(); i++) {
		Ref<EditorExportPreset> preset = editor_export->get_export_preset(i);
		if (preset.is_valid() && preset->get_name() == p_target.export_preset) {
			return preset;
		}
	}
	return Ref<EditorExportPreset>();
}

Ref<EditorExportPreset> RunTargetsPanel::_get_or_create_preset_for_platform(const String &p_platform) const {
	EditorExport *editor_export = EditorExport::get_singleton();
	if (editor_export == nullptr) {
		return Ref<EditorExportPreset>();
	}

	const String os_name = export_os_name_for_platform(p_platform);
	if (os_name.is_empty()) {
		return Ref<EditorExportPreset>();
	}

	// Reuse an existing preset for the platform when one is present, preferring a
	// runnable one. Device enumeration (and thus automatic-device run) only works
	// against a runnable preset, so promote a non-runnable fallback if that is all
	// the project has.
	Ref<EditorExportPreset> fallback;
	for (int i = 0; i < editor_export->get_export_preset_count(); i++) {
		Ref<EditorExportPreset> preset = editor_export->get_export_preset(i);
		if (preset.is_valid() && preset->get_platform().is_valid() && preset->get_platform()->get_os_name() == os_name) {
			if (preset->is_runnable()) {
				return preset;
			}
			if (fallback.is_null()) {
				fallback = preset;
			}
		}
	}
	if (fallback.is_valid()) {
		fallback->set_runnable(true);
		return fallback;
	}

	// Otherwise create one from the matching export platform.
	for (int i = 0; i < editor_export->get_export_platform_count(); i++) {
		Ref<EditorExportPlatform> platform = editor_export->get_export_platform(i);
		if (platform.is_null() || platform->get_os_name() != os_name) {
			continue;
		}
		Ref<EditorExportPreset> preset = platform->create_preset();
		if (preset.is_null()) {
			return Ref<EditorExportPreset>();
		}

		// Pick a name no existing preset uses; targets resolve their preset by name,
		// so a collision (e.g. an unrelated preset already named "iOS") would let a
		// target edit or run the wrong preset.
		String preset_name = os_name;
		int suffix = 2;
		bool taken = true;
		while (taken) {
			taken = false;
			for (int j = 0; j < editor_export->get_export_preset_count(); j++) {
				Ref<EditorExportPreset> existing = editor_export->get_export_preset(j);
				if (existing.is_valid() && existing->get_name() == preset_name) {
					taken = true;
					break;
				}
			}
			if (taken) {
				preset_name = os_name + " " + itos(suffix++);
			}
		}

		// Register the preset first so the configuration setters below (each of
		// which persists through to `export_presets.cfg`) write the new entry too.
		editor_export->add_export_preset(preset);
		preset->set_name(preset_name);
		preset->set_runnable(true);
		preset->set(PRESET_KEY_BUNDLE_ID, default_bundle_id_for_project(GLOBAL_GET("application/config/name")));
		return preset;
	}

	return Ref<EditorExportPreset>();
}

void RunTargetsPanel::_refresh_target_list(int p_select_index) {
	target_list->clear();
	const Vector<RunTarget> &targets = manager->get_targets();
	for (int i = 0; i < targets.size(); i++) {
		const String label = targets[i].name.is_empty() ? TTR("(unnamed)") : targets[i].name;
		target_list->add_item(label);
	}

	int select = p_select_index;
	if (select < 0 || select >= targets.size()) {
		select = targets.is_empty() ? -1 : 0;
	}
	if (select >= 0) {
		target_list->select(select);
	}

	const bool has_selection = select >= 0;
	add_button->set_disabled(!config_writable);
	remove_button->set_disabled(!has_selection || !config_writable);
	rename_button->set_disabled(!has_selection || !config_writable);
	details_container->set_visible(has_selection);
	recheck_button->set_disabled(!has_selection);

	_load_selection_into_fields();
}

void RunTargetsPanel::_load_selection_into_fields() {
	RunTarget target;
	if (!_get_selected_target(target)) {
		_clear_readiness_rows();
		readiness_placeholder->set_visible(true);
		return;
	}

	updating_fields = true;

	// Only automatic signing is offered today (delegated to Xcode), so the option
	// is always the first entry regardless of what is stored.
	signing_mode_option->select(0);

	Ref<EditorExportPreset> preset = _resolve_preset(target);
	String bundle_id;
	String team_id = target.team_id;
	if (preset.is_valid()) {
		bundle_id = preset->get(PRESET_KEY_BUNDLE_ID);
		if (team_id.is_empty()) {
			team_id = preset->get(PRESET_KEY_TEAM_ID);
		}
	}
	bundle_id_edit->set_text(bundle_id);
	team_id_edit->set_text(team_id);
	_refresh_team_options(target, team_id);

	_refresh_device_options(target);

	updating_fields = false;

	_update_bundle_id_validity(bundle_id);
	_reprobe_selected();
}

void RunTargetsPanel::_refresh_device_options(const RunTarget &p_target) {
	device_option->clear();
	device_option->add_item(TTR("Automatic"));
	device_option->set_item_metadata(0, AUTO_DEVICE_ID);
	int select = 0;

	RunTargetPlatform *adapter = manager->get_platform(p_target.platform);
	if (adapter != nullptr) {
		const Vector<RunTargetDevice> devices = adapter->list_devices();
		for (int i = 0; i < devices.size(); i++) {
			const int item_index = device_option->get_item_count();
			device_option->add_item(devices[i].name.is_empty() ? devices[i].id : devices[i].name);
			device_option->set_item_metadata(item_index, devices[i].id);
			if (devices[i].id == p_target.device_id) {
				select = item_index;
			}
		}
	}

	// A remembered device that is not currently connected should still be shown so
	// the selection is not silently dropped.
	if (select == 0 && !p_target.device_id.is_empty() && p_target.device_id != AUTO_DEVICE_ID) {
		const int item_index = device_option->get_item_count();
		device_option->add_item(vformat(TTR("%s (not connected)"), p_target.device_id));
		device_option->set_item_metadata(item_index, p_target.device_id);
		select = item_index;
	}

	device_option->select(select);
}

void RunTargetsPanel::_refresh_team_options(const RunTarget &p_target, const String &p_current_team_id) {
	team_option->clear();
	detected_teams.clear();

	RunTargetPlatform *adapter = manager->get_platform(p_target.platform);
	if (adapter != nullptr) {
		detected_teams = adapter->list_signing_teams();
	}

	// An explicit "None" entry so a stale or wrong team can be cleared from the
	// picker (the manual field's empty submit is a deliberate no-op).
	team_option->add_item(TTR("None"));
	team_option->set_item_metadata(0, String());

	int select = p_current_team_id.is_empty() ? 0 : -1;
	bool current_present = false;
	for (int i = 0; i < detected_teams.size(); i++) {
		const SigningTeam &team = detected_teams[i];
		const int item_index = team_option->get_item_count();
		team_option->add_item(vformat("%s (%s)", team.name, team.id));
		team_option->set_item_metadata(item_index, team.id);
		if (team.id == p_current_team_id) {
			select = item_index;
			current_present = true;
		}
	}

	// A remembered team that is not among the detected ones should still be shown
	// so the selection is not silently dropped (mirrors the device picker).
	if (!current_present && !p_current_team_id.is_empty()) {
		const int item_index = team_option->get_item_count();
		team_option->add_item(vformat(TTR("%s (not detected)"), p_current_team_id));
		team_option->set_item_metadata(item_index, p_current_team_id);
		select = item_index;
	}

	team_option->add_separator();
	const int manual_index = team_option->get_item_count();
	team_option->add_item(TTR("Enter Team ID manually…"));
	team_option->set_item_metadata(manual_index, MANUAL_TEAM_ITEM);

	// Defensive fallback; an empty team already resolves to "None" above.
	if (select < 0) {
		select = manual_index;
	}
	team_option->select(select);
	team_id_edit->set_visible(select == manual_index);
}

void RunTargetsPanel::_on_target_selected(int p_index) {
	_load_selection_into_fields();
	remove_button->set_disabled(!config_writable);
	rename_button->set_disabled(!config_writable);
	details_container->set_visible(true);
	recheck_button->set_disabled(false);
}

void RunTargetsPanel::_on_add_pressed() {
	if (!config_writable) {
		return;
	}

	Ref<EditorExportPreset> preset = _get_or_create_preset_for_platform(IOS_PLATFORM);

	RunTarget target;
	target.name = unique_target_name(TTR("New Target"), manager->get_targets());
	target.platform = IOS_PLATFORM;
	target.export_preset = preset.is_valid() ? preset->get_name() : String();
	target.device_id = AUTO_DEVICE_ID;
	target.signing_mode = DEFAULT_SIGNING_MODE;

	Vector<RunTarget> targets = manager->get_targets();
	targets.push_back(target);
	manager->set_targets(targets);
	manager->save();

	_refresh_target_list(targets.size() - 1);
	_refresh_unconfigured_devices();
}

HashMap<String, Vector<RunTargetDevice>> RunTargetsPanel::_gather_devices_by_platform() const {
	HashMap<String, Vector<RunTargetDevice>> devices_by_platform;

	// Probe iOS plus any platform a configured target references, so a target on a
	// platform without an adapter never makes the section claim it has no devices
	// for a platform it cannot enumerate.
	HashMap<String, bool> candidate_platforms;
	candidate_platforms[IOS_PLATFORM] = true;
	for (const RunTarget &target : manager->get_targets()) {
		candidate_platforms[target.platform] = true;
	}
	for (const KeyValue<String, bool> &candidate : candidate_platforms) {
		RunTargetPlatform *adapter = manager->get_platform(candidate.key);
		if (adapter != nullptr) {
			devices_by_platform[candidate.key] = adapter->list_devices();
		}
	}
	return devices_by_platform;
}

void RunTargetsPanel::_refresh_unconfigured_devices() {
	_clear_device_rows();

	const HashMap<String, Vector<RunTargetDevice>> devices_by_platform = _gather_devices_by_platform();
	// build_menu_model already partitions devices into configured-target rows and
	// SETUP_DEVICE rows for connected devices no target claims; reuse it so the panel
	// and the run-bar selector agree on which devices still need setting up.
	const Vector<RunTargetMenuEntry> entries = EditorRunNative::build_menu_model(manager->get_targets(), devices_by_platform);

	bool any_unconfigured = false;
	for (const RunTargetMenuEntry &entry : entries) {
		if (entry.kind != RunTargetMenuEntry::SETUP_DEVICE) {
			continue;
		}
		any_unconfigured = true;

		const String device_label = entry.label.is_empty() ? entry.device_id : entry.label;
		Button *setup_button = memnew(Button);
		setup_button->set_text(vformat(TTR("Set up %s…"), device_label));
		setup_button->set_tooltip_text(TTR("Create a run target for this connected device."));
		setup_button->set_disabled(!config_writable);
		setup_button->connect(SceneStringName(pressed), callable_mp(this, &RunTargetsPanel::_on_setup_device_pressed).bind(entry.platform, entry.device_id, device_label));
		devices_container->add_child(setup_button);
	}

	devices_placeholder->set_visible(!any_unconfigured);
}

void RunTargetsPanel::_clear_device_rows() {
	for (int i = devices_container->get_child_count() - 1; i >= 0; i--) {
		Node *child = devices_container->get_child(i);
		if (child == devices_placeholder) {
			continue;
		}
		child->queue_free();
		devices_container->remove_child(child);
	}
}

void RunTargetsPanel::_on_setup_device_pressed(const String &p_platform, const String &p_device_id, const String &p_device_name) {
	if (!config_writable) {
		return;
	}

	Ref<EditorExportPreset> preset = _get_or_create_preset_for_platform(p_platform);
	const RunTarget target = make_device_setup_target(p_platform, p_device_id, p_device_name, preset.is_valid() ? preset->get_name() : String(), manager->get_targets());

	Vector<RunTarget> targets = manager->get_targets();
	targets.push_back(target);
	manager->set_targets(targets);
	manager->save();

	// Select the freshly created target so the user lands on its signing/team
	// fields and readiness ladder, then drop the now-claimed device from the list.
	_refresh_target_list(targets.size() - 1);
	_refresh_unconfigured_devices();
}

void RunTargetsPanel::_on_remove_pressed() {
	if (!config_writable) {
		return;
	}

	const int index = _selected_target_index();
	if (index < 0) {
		return;
	}
	Vector<RunTarget> targets = manager->get_targets();
	targets.remove_at(index);
	manager->set_targets(targets);
	manager->save();

	_refresh_target_list(MIN(index, targets.size() - 1));
	_refresh_unconfigured_devices();
}

void RunTargetsPanel::_on_rename_pressed() {
	if (!config_writable) {
		return;
	}

	RunTarget target;
	if (!_get_selected_target(target)) {
		return;
	}
	rename_field->set_text(target.name);
	rename_dialog->popup_centered();
	rename_field->select_all();
	rename_field->grab_focus();
}

void RunTargetsPanel::_on_rename_confirmed() {
	if (!config_writable) {
		return;
	}

	const int index = _selected_target_index();
	if (index < 0) {
		return;
	}
	const String new_name = rename_field->get_text().strip_edges();
	if (new_name.is_empty()) {
		return;
	}
	// Targets are stored and resolved by name; refuse a rename that would collide
	// with another target and make the active selection ambiguous.
	if (!_is_name_available(new_name, index)) {
		ERR_PRINT(vformat("Run Targets: a target named \"%s\" already exists.", new_name));
		return;
	}

	RunTarget target = manager->get_targets()[index];
	const String previous_name = target.name;
	// Capture this before the commit: _commit_target() calls set_targets(), which
	// drops the active selection once the renamed target no longer matches the
	// stored active name.
	const bool was_active = manager->get_active_target_name() == previous_name;

	target.name = new_name;
	_commit_target(index, target);

	// Keep the active selection pointing at the same target after a rename.
	if (was_active) {
		manager->set_active_target(new_name);
		manager->save();
	}

	_refresh_target_list(index);
}

void RunTargetsPanel::_on_signing_mode_changed(int p_index) {
	if (updating_fields) {
		return;
	}
	if (!config_writable) {
		return;
	}

	const int index = _selected_target_index();
	if (index < 0) {
		return;
	}
	RunTarget target = manager->get_targets()[index];
	target.signing_mode = DEFAULT_SIGNING_MODE;
	_commit_target(index, target);
}

void RunTargetsPanel::_on_bundle_id_changed(const String &p_text) {
	_update_bundle_id_validity(p_text);
}

void RunTargetsPanel::_on_bundle_id_submitted() {
	if (updating_fields) {
		return;
	}
	if (!config_writable) {
		return;
	}

	RunTarget target;
	if (!_get_selected_target(target)) {
		return;
	}
	const String text = bundle_id_edit->get_text();
	if (!is_valid_bundle_id(text)) {
		// Leave the invalid text visible with its warning; do not write it through.
		return;
	}
	Ref<EditorExportPreset> preset = _resolve_preset(target);
	if (preset.is_valid()) {
		// Writing through the preset setter persists it to export_presets.cfg.
		preset->set(PRESET_KEY_BUNDLE_ID, text);
	}
}

void RunTargetsPanel::_apply_team_id(const String &p_team_id) {
	if (!config_writable) {
		return;
	}

	const int index = _selected_target_index();
	if (index < 0) {
		return;
	}
	RunTarget target = manager->get_targets()[index];
	target.team_id = p_team_id;
	_commit_target(index, target);

	Ref<EditorExportPreset> preset = _resolve_preset(target);
	if (preset.is_valid()) {
		preset->set(PRESET_KEY_TEAM_ID, p_team_id);
	}
}

void RunTargetsPanel::_on_team_option_selected(int p_index) {
	if (updating_fields) {
		return;
	}
	const String team_id = team_option->get_item_metadata(p_index);
	if (team_id == MANUAL_TEAM_ITEM) {
		// Reveal the manual field and let the user type; nothing is committed until
		// they submit so the remembered team is not cleared by merely switching here.
		team_id_edit->set_visible(true);
		team_id_edit->grab_focus();
		return;
	}

	team_id_edit->set_visible(false);
	team_id_edit->set_text(team_id);
	_apply_team_id(team_id);
	_reprobe_selected();
}

void RunTargetsPanel::_on_team_id_submitted() {
	if (updating_fields) {
		return;
	}
	// Only the visible manual field commits. When it is hidden the event is just a
	// side effect of switching to a detected team, which commits on its own.
	if (!team_id_edit->is_visible()) {
		return;
	}
	const String team_id = team_id_edit->get_text().strip_edges();
	if (team_id.is_empty()) {
		// Treat an empty manual field as "no change" so simply tabbing through it
		// never wipes a remembered team. The picker stays on manual entry.
		return;
	}
	_apply_team_id(team_id);
	_reprobe_selected();
}

void RunTargetsPanel::_on_device_changed(int p_index) {
	if (updating_fields) {
		return;
	}
	if (!config_writable) {
		return;
	}

	const int index = _selected_target_index();
	if (index < 0) {
		return;
	}
	RunTarget target = manager->get_targets()[index];
	target.device_id = device_option->get_item_metadata(p_index);
	_commit_target(index, target);

	// Binding/unbinding a device changes which devices are still unconfigured.
	_refresh_unconfigured_devices();
}

void RunTargetsPanel::_on_recheck_pressed() {
	_reprobe_selected();
}

void RunTargetsPanel::_update_bundle_id_validity(const String &p_text) {
	String error;
	if (is_valid_bundle_id(p_text, &error)) {
		bundle_id_status->set_text(TTR("Looks good."));
		bundle_id_status->add_theme_color_override(SceneStringName(font_color), get_theme_color(SNAME("success_color"), EditorStringName(Editor)));
	} else {
		bundle_id_status->set_text(error);
		bundle_id_status->add_theme_color_override(SceneStringName(font_color), get_theme_color(SNAME("warning_color"), EditorStringName(Editor)));
	}
}

void RunTargetsPanel::_reprobe_selected() {
	RunTarget target;
	if (!_get_selected_target(target)) {
		_clear_readiness_rows();
		readiness_placeholder->set_visible(true);
		return;
	}

	RunTargetPlatform *adapter = manager->get_platform(target.platform);
	if (adapter == nullptr) {
		_clear_readiness_rows();
		readiness_placeholder->set_text(vformat(TTR("No run-target support is available for platform \"%s\" on this system."), target.platform));
		readiness_placeholder->set_visible(true);
		return;
	}

	const Vector<ReadinessStep> steps = adapter->probe_readiness(_effective_target_for_probe(target));
	_render_readiness(steps);
}

RunTarget RunTargetsPanel::_effective_target_for_probe(const RunTarget &p_target) const {
	RunTarget effective = p_target;
	if (effective.team_id.is_empty()) {
		Ref<EditorExportPreset> preset = _resolve_preset(p_target);
		if (preset.is_valid()) {
			effective.team_id = preset->get(PRESET_KEY_TEAM_ID);
		}
	}
	return effective;
}

bool RunTargetsPanel::_is_name_available(const String &p_name, int p_ignore_index) const {
	const Vector<RunTarget> &targets = manager->get_targets();
	for (int i = 0; i < targets.size(); i++) {
		if (i != p_ignore_index && targets[i].name == p_name) {
			return false;
		}
	}
	return true;
}

void RunTargetsPanel::_clear_readiness_rows() {
	for (int i = readiness_container->get_child_count() - 1; i >= 0; i--) {
		Node *child = readiness_container->get_child(i);
		if (child == readiness_placeholder) {
			continue;
		}
		child->queue_free();
		readiness_container->remove_child(child);
	}
}

void RunTargetsPanel::_render_readiness(const Vector<ReadinessStep> &p_steps) {
	_clear_readiness_rows();

	if (p_steps.is_empty()) {
		readiness_placeholder->set_text(TTR("No readiness information for this target."));
		readiness_placeholder->set_visible(true);
		return;
	}
	readiness_placeholder->set_visible(false);

	const int actionable = first_actionable_step_index(p_steps);

	const Color ok_color = get_theme_color(SNAME("success_color"), EditorStringName(Editor));
	const Color warning_color = get_theme_color(SNAME("warning_color"), EditorStringName(Editor));
	const Color error_color = get_theme_color(SNAME("error_color"), EditorStringName(Editor));
	const Color disabled_color = get_theme_color(SNAME("font_disabled_color"), EditorStringName(Editor));

	for (int i = 0; i < p_steps.size(); i++) {
		const ReadinessStep &step = p_steps[i];

		VBoxContainer *row = memnew(VBoxContainer);
		readiness_container->add_child(row);

		HBoxContainer *header = memnew(HBoxContainer);
		row->add_child(header);

		Label *marker = memnew(Label);
		Color marker_color;
		String marker_text;
		if (step.status == ReadinessStep::Status::OK) {
			marker_text = String::utf8("✓"); // Check mark.
			marker_color = ok_color;
		} else if (i == actionable) {
			marker_text = String::utf8("▶"); // Current step.
			marker_color = (step.status == ReadinessStep::Status::BLOCKED) ? error_color : warning_color;
		} else {
			marker_text = String::utf8("○"); // Upcoming.
			marker_color = disabled_color;
		}
		marker->set_text(marker_text);
		marker->add_theme_color_override(SceneStringName(font_color), marker_color);
		header->add_child(marker);

		Label *title_label = memnew(Label);
		title_label->set_text(step.title);
		title_label->set_h_size_flags(SIZE_EXPAND_FILL);
		if (step.status != ReadinessStep::Status::OK && i != actionable) {
			title_label->add_theme_color_override(SceneStringName(font_color), disabled_color);
		}
		header->add_child(title_label);

		// Expand the step the user can act on with its detail and fix hint.
		if (i == actionable) {
			if (!step.detail.is_empty()) {
				Label *detail = memnew(Label);
				detail->set_text(step.detail);
				detail->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
				row->add_child(detail);
			}
			if (!step.fix_hint.is_empty()) {
				Label *hint = memnew(Label);
				hint->set_text(step.fix_hint);
				hint->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
				hint->add_theme_color_override(SceneStringName(font_color), warning_color);
				row->add_child(hint);
			}
		}
	}
}

void RunTargetsPanel::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_READY: {
			_ensure_manager();
			_refresh_target_list();
			_refresh_unconfigured_devices();
			initialized = true;
		} break;

		case NOTIFICATION_VISIBILITY_CHANGED: {
			// Opening the panel triggers an on-demand reprobe so the ladder reflects
			// the current device/toolchain state, and re-enumerates connected devices
			// so a just-plugged-in device offers its "Set up this device…" row live.
			if (initialized && is_visible_in_tree()) {
				_reprobe_selected();
				_refresh_unconfigured_devices();
			}
		} break;
	}
}

RunTargetsPanel::RunTargetsPanel() {
	set_name(TTRC("Run Targets"));

	HSplitContainer *split = memnew(HSplitContainer);
	split->set_v_size_flags(SIZE_EXPAND_FILL);
	add_child(split);

	// Targets list region.
	VBoxContainer *list_column = memnew(VBoxContainer);
	list_column->set_custom_minimum_size(Size2(140, 0) * EDSCALE);
	split->add_child(list_column);

	target_list = memnew(ItemList);
	target_list->set_v_size_flags(SIZE_EXPAND_FILL);
	target_list->set_auto_translate_mode(AUTO_TRANSLATE_MODE_DISABLED);
	target_list->connect(SceneStringName(item_selected), callable_mp(this, &RunTargetsPanel::_on_target_selected));
	list_column->add_child(target_list);

	HBoxContainer *list_buttons = memnew(HBoxContainer);
	list_column->add_child(list_buttons);

	add_button = memnew(Button);
	add_button->set_text(TTR("Add"));
	add_button->connect(SceneStringName(pressed), callable_mp(this, &RunTargetsPanel::_on_add_pressed));
	list_buttons->add_child(add_button);

	remove_button = memnew(Button);
	remove_button->set_text(TTR("Remove"));
	remove_button->connect(SceneStringName(pressed), callable_mp(this, &RunTargetsPanel::_on_remove_pressed));
	list_buttons->add_child(remove_button);

	rename_button = memnew(Button);
	rename_button->set_text(TTR("Rename"));
	rename_button->connect(SceneStringName(pressed), callable_mp(this, &RunTargetsPanel::_on_rename_pressed));
	list_buttons->add_child(rename_button);

	// Connected-but-unconfigured devices: one-click "Set up this device…" rows.
	list_column->add_child(memnew(HSeparator));

	Label *devices_header = memnew(Label);
	devices_header->set_text(TTR("Connected Devices"));
	devices_header->set_theme_type_variation("HeaderSmall");
	list_column->add_child(devices_header);

	devices_container = memnew(VBoxContainer);
	list_column->add_child(devices_container);

	devices_placeholder = memnew(Label);
	devices_placeholder->set_text(TTR("No unconfigured devices detected."));
	devices_placeholder->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
	devices_container->add_child(devices_placeholder);

	// Details column (signing & devices, readiness).
	details_container = memnew(VBoxContainer);
	details_container->set_h_size_flags(SIZE_EXPAND_FILL);
	split->add_child(details_container);

	Label *signing_header = memnew(Label);
	signing_header->set_text(TTR("Signing & Devices"));
	signing_header->set_theme_type_variation("HeaderSmall");
	details_container->add_child(signing_header);

	signing_mode_option = memnew(OptionButton);
	signing_mode_option->add_item(TTR("Automatic (Xcode)"));
	signing_mode_option->connect(SceneStringName(item_selected), callable_mp(this, &RunTargetsPanel::_on_signing_mode_changed));
	details_container->add_child(signing_mode_option);

	Label *bundle_label = memnew(Label);
	bundle_label->set_text(TTR("Bundle Identifier"));
	details_container->add_child(bundle_label);

	bundle_id_edit = memnew(LineEdit);
	bundle_id_edit->set_placeholder("com.example.game");
	bundle_id_edit->connect(SceneStringName(text_changed), callable_mp(this, &RunTargetsPanel::_on_bundle_id_changed));
	bundle_id_edit->connect(SceneStringName(text_submitted), callable_mp(this, &RunTargetsPanel::_on_bundle_id_submitted).unbind(1));
	bundle_id_edit->connect(SceneStringName(focus_exited), callable_mp(this, &RunTargetsPanel::_on_bundle_id_submitted));
	details_container->add_child(bundle_id_edit);

	bundle_id_status = memnew(Label);
	bundle_id_status->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
	details_container->add_child(bundle_id_status);

	Label *team_label = memnew(Label);
	team_label->set_text(TTR("Signing Team"));
	details_container->add_child(team_label);

	team_option = memnew(OptionButton);
	team_option->connect(SceneStringName(item_selected), callable_mp(this, &RunTargetsPanel::_on_team_option_selected));
	details_container->add_child(team_option);

	team_id_edit = memnew(LineEdit);
	team_id_edit->set_placeholder(TTR("Apple Developer team ID"));
	team_id_edit->connect(SceneStringName(text_submitted), callable_mp(this, &RunTargetsPanel::_on_team_id_submitted).unbind(1));
	team_id_edit->connect(SceneStringName(focus_exited), callable_mp(this, &RunTargetsPanel::_on_team_id_submitted));
	details_container->add_child(team_id_edit);

	Label *device_label = memnew(Label);
	device_label->set_text(TTR("Device"));
	details_container->add_child(device_label);

	device_option = memnew(OptionButton);
	device_option->connect(SceneStringName(item_selected), callable_mp(this, &RunTargetsPanel::_on_device_changed));
	details_container->add_child(device_option);

	details_container->add_child(memnew(HSeparator));

	HBoxContainer *readiness_header = memnew(HBoxContainer);
	details_container->add_child(readiness_header);

	Label *readiness_title = memnew(Label);
	readiness_title->set_text(TTR("Readiness"));
	readiness_title->set_theme_type_variation("HeaderSmall");
	readiness_title->set_h_size_flags(SIZE_EXPAND_FILL);
	readiness_header->add_child(readiness_title);

	recheck_button = memnew(Button);
	recheck_button->set_text(TTR("Recheck"));
	recheck_button->connect(SceneStringName(pressed), callable_mp(this, &RunTargetsPanel::_on_recheck_pressed));
	readiness_header->add_child(recheck_button);

	readiness_container = memnew(VBoxContainer);
	readiness_container->set_v_size_flags(SIZE_EXPAND_FILL);
	details_container->add_child(readiness_container);

	readiness_placeholder = memnew(Label);
	readiness_placeholder->set_text(TTR("Select a target to view its readiness."));
	readiness_placeholder->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
	readiness_container->add_child(readiness_placeholder);

	// Rename dialog.
	rename_dialog = memnew(ConfirmationDialog);
	rename_dialog->set_title(TTR("Rename Target"));
	rename_field = memnew(LineEdit);
	rename_dialog->add_child(rename_field);
	rename_dialog->register_text_enter(rename_field);
	rename_dialog->connect(SceneStringName(confirmed), callable_mp(this, &RunTargetsPanel::_on_rename_confirmed));
	add_child(rename_dialog);
}

RunTargetsPanel::~RunTargetsPanel() {
	if (owns_manager) {
		if (RunTargetManager::get_singleton() == manager) {
			RunTargetManager::set_singleton(nullptr);
		}
		if (manager != nullptr) {
			memdelete(manager);
			manager = nullptr;
		}
		if (owned_adapter != nullptr) {
			memdelete(owned_adapter);
			owned_adapter = nullptr;
		}
	}
}
