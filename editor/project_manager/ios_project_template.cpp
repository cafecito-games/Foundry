/**************************************************************************/
/*  ios_project_template.cpp                                              */
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

#include "ios_project_template.h"

#include "editor/run/run_target_manager.h"

#include "core/io/config_file.h"
#include "core/templates/vector.h"

namespace IOSProjectTemplate {

String derive_bundle_identifier(const String &p_project_name) {
	const String lowered = p_project_name.to_lower();

	String slug;
	for (int i = 0; i < lowered.length(); i++) {
		const char32_t character = lowered[i];
		const bool is_ascii_lower = character >= 'a' && character <= 'z';
		const bool is_digit = character >= '0' && character <= '9';
		if (is_ascii_lower || is_digit) {
			slug += character;
		}
	}

	if (slug.is_empty()) {
		slug = "game";
	}

	return "com.example." + slug;
}

RunTarget make_default_target() {
	RunTarget target;
	target.name = "iOS Device";
	target.platform = "ios";
	target.export_preset = PRESET_NAME;
	target.device_id = "auto";
	target.signing_mode = "automatic";
	target.team_id = String();
	return target;
}

Error seed(const String &p_project_path, const String &p_project_name) {
	// Write a minimal-but-loadable iOS export preset. Only the keys the editor's
	// preset loader (`EditorExport::load_config`) reads are set; everything else
	// resolves from the preset's own defaults when the project is opened.
	Ref<ConfigFile> presets;
	presets.instantiate();

	const String section = "preset.0";
	presets->set_value(section, "name", PRESET_NAME);
	presets->set_value(section, "platform", PRESET_NAME);
	presets->set_value(section, "runnable", true);
	presets->set_value(section, "dedicated_server", false);
	presets->set_value(section, "custom_features", "");
	presets->set_value(section, "export_filter", "all_resources");
	presets->set_value(section, "include_filter", "");
	presets->set_value(section, "exclude_filter", "");
	presets->set_value(section, "export_path", "");

	const String options_section = "preset.0.options";
	presets->set_value(options_section, "application/bundle_identifier", derive_bundle_identifier(p_project_name));
	presets->set_value(options_section, "application/app_store_team_id", "");

	const Error presets_error = presets->save(p_project_path.path_join("export_presets.cfg"));
	if (presets_error != OK) {
		return presets_error;
	}

	const String targets_path = p_project_path.path_join("run_targets.cfg");
	Vector<RunTarget> targets;
	targets.push_back(make_default_target());
	const Error targets_error = RunTarget::save_all(targets_path, targets);
	if (targets_error != OK) {
		return targets_error;
	}

	// Ask the editor to reveal Run Targets configuration the first time this
	// project is opened, landing the user on the readiness ladder ("here's what's
	// left to run on your phone") instead of leaving the seeded target
	// undiscovered.
	return RunTargetManager::request_show_configuration_on_first_open(targets_path);
}

} // namespace IOSProjectTemplate
