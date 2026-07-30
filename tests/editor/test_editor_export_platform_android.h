/**************************************************************************/
/*  test_editor_export_platform_android.h                                 */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             FOUNDRY ENGINE                             */
/*          A fork of the Godot Engine (https://godotengine.org)          */
/*                       https://www.cafecito.games                       */
/**************************************************************************/
/* Copyright (c) 2026-present Cafecito Games LLC.                         */
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

#ifdef TOOLS_ENABLED

#include "editor/export/editor_export_preset.h"
#include "editor/settings/editor_settings.h"
#include "platform/android/export/export_plugin.h"

#include "tests/test_macros.h"

namespace TestEditorExportPlatformAndroid {

const char *ADVANCED_MODE_SETTING = "_export_preset_advanced_mode";

// `EditorExportPreset::are_advanced_options_enabled()` reads an editor setting that only
// the full editor defines, so give it a known value for the duration of a test case and
// restore the previous state afterwards.
class ScopedAdvancedOptionsSetting {
	bool had_setting = false;
	Variant previous_value;

public:
	explicit ScopedAdvancedOptionsSetting(bool p_enabled) {
		EditorSettings *settings = EditorSettings::get_singleton();
		had_setting = settings->has_setting(ADVANCED_MODE_SETTING);
		if (had_setting) {
			previous_value = settings->get_setting(ADVANCED_MODE_SETTING);
		}
		settings->set_setting(ADVANCED_MODE_SETTING, p_enabled);
	}

	~ScopedAdvancedOptionsSetting() {
		EditorSettings *settings = EditorSettings::get_singleton();
		if (had_setting) {
			settings->set_setting(ADVANCED_MODE_SETTING, previous_value);
		} else {
			settings->erase(ADVANCED_MODE_SETTING);
		}
	}
};

const char *FOUNDRY_JAVA_SUBOPTIONS[] = {
	"gradle_build/foundry_java/gradle_plugin_maven",
	"gradle_build/foundry_java/gradle_plugin_local",
	"gradle_build/foundry_java/maven_repositories",
	"gradle_build/foundry_java/maven_artifacts",
	"gradle_build/foundry_java/local_artifacts",
};

static Ref<EditorExportPreset> make_preset(bool p_use_gradle_build, bool p_foundry_java_enabled) {
	Ref<EditorExportPreset> preset;
	preset.instantiate();
	preset->set("gradle_build/use_gradle_build", p_use_gradle_build);
	preset->set("gradle_build/foundry_java/enabled", p_foundry_java_enabled);
	return preset;
}

TEST_CASE("[Editor][EditorExportPlatformAndroid] Foundry-Java toggle stays visible without Gradle builds") {
	Ref<EditorExportPlatformAndroid> platform;
	platform.instantiate();

	SUBCASE("advanced options disabled") {
		ScopedAdvancedOptionsSetting advanced_options(false);

		// A preset saved with the toggle enabled but Gradle builds since turned off is an
		// invalid combination that export validation rejects before any build work begins.
		// The toggle has to stay visible so the user can repair the stale preset.
		Ref<EditorExportPreset> stale_preset = make_preset(false, true);
		CHECK(platform->get_export_option_visibility(stale_preset.ptr(), "gradle_build/foundry_java/enabled"));

		Ref<EditorExportPreset> disabled_preset = make_preset(false, false);
		CHECK(platform->get_export_option_visibility(disabled_preset.ptr(), "gradle_build/foundry_java/enabled"));
	}

	SUBCASE("advanced options enabled") {
		ScopedAdvancedOptionsSetting advanced_options(true);

		Ref<EditorExportPreset> stale_preset = make_preset(false, true);
		CHECK(platform->get_export_option_visibility(stale_preset.ptr(), "gradle_build/foundry_java/enabled"));
	}

	SUBCASE("Gradle builds enabled") {
		ScopedAdvancedOptionsSetting advanced_options(false);

		Ref<EditorExportPreset> preset = make_preset(true, false);
		CHECK(platform->get_export_option_visibility(preset.ptr(), "gradle_build/foundry_java/enabled"));
	}
}

TEST_CASE("[Editor][EditorExportPlatformAndroid] Foundry-Java suboptions follow the toggle") {
	Ref<EditorExportPlatformAndroid> platform;
	platform.instantiate();
	ScopedAdvancedOptionsSetting advanced_options(false);

	SUBCASE("hidden when the toggle is disabled") {
		Ref<EditorExportPreset> preset = make_preset(true, false);
		for (const char *option : FOUNDRY_JAVA_SUBOPTIONS) {
			CHECK_FALSE_MESSAGE(platform->get_export_option_visibility(preset.ptr(), option), String(option));
		}
	}

	SUBCASE("shown when the toggle is enabled, even without Gradle builds") {
		Ref<EditorExportPreset> preset = make_preset(false, true);
		for (const char *option : FOUNDRY_JAVA_SUBOPTIONS) {
			CHECK_MESSAGE(platform->get_export_option_visibility(preset.ptr(), option), String(option));
		}
	}
}

TEST_CASE("[Editor][EditorExportPlatformAndroid] APK expansion is not an export option") {
	Ref<EditorExportPlatformAndroid> platform;
	platform.instantiate();

	List<EditorExportPlatform::ExportOption> options;
	platform->get_export_options(&options);
	for (const EditorExportPlatform::ExportOption &option : options) {
		CHECK_FALSE_MESSAGE(String(option.option.name).begins_with("apk_expansion/"), String(option.option.name));
	}
}

TEST_CASE("[Editor][EditorExportPlatformAndroid] Stale APK expansion preset keys are inert") {
	Ref<EditorExportPlatformAndroid> platform;
	platform.instantiate();
	ScopedAdvancedOptionsSetting advanced_options(true);

	// A preset written before APK expansion was removed keeps its keys, because
	// `EditorExportPreset` retains values for options no platform registers. They
	// have to stay inert: no diagnostic, no effect on the export.
	Ref<EditorExportPreset> preset = make_preset(true, false);
	preset->set("apk_expansion/enable", true);
	preset->set("apk_expansion/SALT", "stale-salt");
	preset->set("apk_expansion/public_key", "");

	CHECK(platform->get_export_option_warning(preset.ptr(), "apk_expansion/enable").is_empty());
	CHECK(platform->get_export_option_warning(preset.ptr(), "apk_expansion/public_key").is_empty());
}

} // namespace TestEditorExportPlatformAndroid

#endif // TOOLS_ENABLED
