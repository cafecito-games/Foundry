/**************************************************************************/
/*  run_target_manager.cpp                                                */
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

#include "run_target_manager.h"

#include "editor/export/editor_export.h"
#include "editor/export/editor_export_platform.h"
#include "editor/export/editor_export_preset.h"
#include "editor/settings/editor_settings.h"

#include "core/io/config_file.h"

namespace {

// Section/key used to persist the active selection alongside the `[target.N]`
// sections written by `RunTarget::save_all`.
constexpr const char *META_SECTION = "meta";
constexpr const char *ACTIVE_TARGET_KEY = "active_target";

// The default preset lookup: resolves a preset by name against the editor's
// `EditorExport` singleton. Returns a null Ref when the singleton is absent
// (e.g. headless tooling) or no preset matches.
class EditorExportPresetProvider : public RunTargetManager::PresetProvider {
public:
	virtual Ref<EditorExportPreset> find_preset_by_name(const String &p_name) const override {
		EditorExport *editor_export = EditorExport::get_singleton();
		if (editor_export == nullptr) {
			return Ref<EditorExportPreset>();
		}
		for (int i = 0; i < editor_export->get_export_preset_count(); i++) {
			Ref<EditorExportPreset> preset = editor_export->get_export_preset(i);
			if (preset.is_valid() && preset->get_name() == p_name) {
				return preset;
			}
		}
		return Ref<EditorExportPreset>();
	}
};

} // namespace

void RunTargetManager::register_platform(const String &p_platform, RunTargetPlatform *p_adapter) {
	ERR_FAIL_NULL(p_adapter);
	ERR_FAIL_COND_MSG(p_platform.is_empty(), "Cannot register a run-target adapter for an empty platform string.");
	adapters[p_platform] = p_adapter;
}

void RunTargetManager::unregister_platform(const String &p_platform) {
	adapters.erase(p_platform);
}

RunTargetPlatform *RunTargetManager::get_platform(const String &p_platform) const {
	HashMap<String, RunTargetPlatform *>::ConstIterator found = adapters.find(p_platform);
	if (found == adapters.end()) {
		return nullptr;
	}
	return found->value;
}

Error RunTargetManager::load(const String &p_path) {
	// Build the new state in locals and only commit it after a clean load. A
	// malformed/unreadable config must leave the manager's existing path,
	// targets, and active selection untouched, so a later save() cannot clobber
	// the file with empty targets and a stale active marker.
	Error error = OK;
	const Vector<RunTarget> loaded_targets = RunTarget::load_all(p_path, &error);
	if (error != OK) {
		return error;
	}

	String loaded_active;
	Ref<ConfigFile> config;
	config.instantiate();
	if (config->load(p_path) == OK && config->has_section_key(META_SECTION, ACTIVE_TARGET_KEY)) {
		loaded_active = config->get_value(META_SECTION, ACTIVE_TARGET_KEY, String());
	}

	// Drop a dangling active selection so callers never resolve a target that is
	// no longer present.
	if (!loaded_active.is_empty()) {
		bool found = false;
		for (int i = 0; i < loaded_targets.size(); i++) {
			if (loaded_targets[i].name == loaded_active) {
				found = true;
				break;
			}
		}
		if (!found) {
			loaded_active = String();
		}
	}

	config_path = p_path;
	targets = loaded_targets;
	active_target_name = loaded_active;
	return OK;
}

Error RunTargetManager::save() {
	ERR_FAIL_COND_V_MSG(config_path.is_empty(), ERR_UNCONFIGURED, "RunTargetManager::save() called before load() established a path.");

	Error error = RunTarget::save_all(config_path, targets);
	if (error != OK) {
		return error;
	}

	if (active_target_name.is_empty()) {
		// `save_all` wrote a fresh file with no meta section, so there is nothing
		// more to persist.
		return OK;
	}

	Ref<ConfigFile> config;
	config.instantiate();
	error = config->load(config_path);
	if (error != OK) {
		return error;
	}
	config->set_value(META_SECTION, ACTIVE_TARGET_KEY, active_target_name);
	return config->save(config_path);
}

void RunTargetManager::set_targets(const Vector<RunTarget> &p_targets) {
	targets = p_targets;
	if (!active_target_name.is_empty() && find_target_index(active_target_name) < 0) {
		active_target_name = String();
	}
}

bool RunTargetManager::set_active_target(const String &p_name) {
	if (p_name.is_empty()) {
		active_target_name = String();
		return true;
	}
	if (find_target_index(p_name) < 0) {
		return false;
	}
	active_target_name = p_name;
	return true;
}

bool RunTargetManager::has_active_target() const {
	return !active_target_name.is_empty() && find_target_index(active_target_name) >= 0;
}

RunTarget RunTargetManager::get_active_target() const {
	const int index = find_target_index(active_target_name);
	if (index < 0) {
		return RunTarget();
	}
	return targets[index];
}

Error RunTargetManager::resolve(const RunTarget &p_target, ResolvedTarget &r_resolved) const {
	RunTargetPlatform *adapter = get_platform(p_target.platform);
	if (adapter == nullptr) {
		ERR_PRINT(vformat("Run target \"%s\" uses platform \"%s\", which has no registered adapter.", p_target.name, p_target.platform));
		return ERR_UNAVAILABLE;
	}

	Ref<EditorExportPreset> preset = find_preset(p_target.export_preset);
	if (preset.is_null()) {
		ERR_PRINT(vformat("Run target \"%s\" references export preset \"%s\", which no longer exists.", p_target.name, p_target.export_preset));
		return ERR_DOES_NOT_EXIST;
	}

	r_resolved.preset = preset;
	r_resolved.device_id = p_target.device_id;
	r_resolved.debug_flags = compute_debug_flags();
	r_resolved.platform_adapter = adapter;
	return OK;
}

int RunTargetManager::find_target_index(const String &p_name) const {
	for (int i = 0; i < targets.size(); i++) {
		if (targets[i].name == p_name) {
			return i;
		}
	}
	return -1;
}

Ref<EditorExportPreset> RunTargetManager::find_preset(const String &p_name) const {
	if (preset_provider != nullptr) {
		return preset_provider->find_preset_by_name(p_name);
	}
	static EditorExportPresetProvider default_provider;
	return default_provider.find_preset_by_name(p_name);
}

int RunTargetManager::compute_debug_flags() const {
	BitField<EditorExportPlatform::DebugFlags> flags = {};

	EditorSettings *settings = EditorSettings::get_singleton();

	// A run-target deploy wires the running app back to the editor's remote
	// debugger, but honor the user's "Deploy Remote Debug" opt-out exactly like
	// the native deploy path (`EditorRunNative::is_deploy_debug_remote_enabled`).
	// Headless tooling has no editor settings, so it keeps the on-by-default
	// behavior.
	bool remote_debug = true;
	if (settings != nullptr) {
		remote_debug = settings->get_project_metadata("debug_options", "run_deploy_remote_debug", true);
	}
	if (remote_debug) {
		flags.set_flag(EditorExportPlatform::DEBUG_FLAG_REMOTE_DEBUG);
	}

	// Fold in the optional debug overlays the user toggled for in-editor runs so a
	// run-target deploy matches the normal Play button.
	if (settings != nullptr) {
		if (settings->get_project_metadata("debug_options", "run_file_server", false)) {
			flags.set_flag(EditorExportPlatform::DEBUG_FLAG_DUMB_CLIENT);
		}
		if (settings->get_project_metadata("debug_options", "run_debug_collisions", false)) {
			flags.set_flag(EditorExportPlatform::DEBUG_FLAG_VIEW_COLLISIONS);
		}
		if (settings->get_project_metadata("debug_options", "run_debug_navigation", false)) {
			flags.set_flag(EditorExportPlatform::DEBUG_FLAG_VIEW_NAVIGATION);
		}
	}

	return static_cast<int>(flags);
}
