/**************************************************************************/
/*  test_inspector_density.h                                              */
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

#include "editor/inspector/editor_inspector.h"
#include "editor/settings/editor_settings.h"
#include "editor/themes/editor_theme.h"
#include "editor/themes/editor_theme_manager.h"

#include "tests/test_macros.h"

namespace TestInspectorDensity {

// Regenerates the editor theme with `interface/theme/inspector_density` set to
// `p_density`, restoring the previous setting value afterwards.
static Ref<EditorTheme> generate_theme_for_density(const String &p_density) {
	String previous = EDITOR_GET("interface/theme/inspector_density");
	EditorSettings::get_singleton()->set_manually("interface/theme/inspector_density", p_density);
	Ref<EditorTheme> theme = EditorThemeManager::generate_theme();
	EditorSettings::get_singleton()->set_manually("interface/theme/inspector_density", previous);
	return theme;
}

static int measure_property_minimum_height(const Ref<EditorTheme> &p_theme) {
	EditorProperty *property = memnew(EditorProperty);
	property->set_theme(p_theme);
	int height = (int)property->get_minimum_size().height;
	memdelete(property);
	return height;
}

TEST_CASE("[Editor][InspectorDensity] density factor is distinct and strictly ordered") {
	float compact = EditorThemeManager::get_inspector_density_scale("Compact");
	float default_density = EditorThemeManager::get_inspector_density_scale("Default");
	float spacious = EditorThemeManager::get_inspector_density_scale("Spacious");

	CHECK(compact < default_density);
	CHECK(default_density < spacious);
	CHECK(default_density == doctest::Approx(1.0f));
}

TEST_CASE("[Editor][InspectorDensity] unrecognized density value falls back to the default factor") {
	CHECK(EditorThemeManager::get_inspector_density_scale("") == doctest::Approx(1.0f));
	CHECK(EditorThemeManager::get_inspector_density_scale("Nonexistent") == doctest::Approx(1.0f));
}

TEST_CASE("[Editor][InspectorDensity] ThemeConfiguration hash differs across density levels") {
	EditorThemeManager::ThemeConfiguration compact_config;
	compact_config.inspector_density = "Compact";

	EditorThemeManager::ThemeConfiguration default_config;
	default_config.inspector_density = "Default";

	EditorThemeManager::ThemeConfiguration spacious_config;
	spacious_config.inspector_density = "Spacious";

	uint32_t compact_hash = compact_config.hash();
	uint32_t default_hash = default_config.hash();
	uint32_t spacious_hash = spacious_config.hash();

	CHECK(compact_hash != default_hash);
	CHECK(default_hash != spacious_hash);
	CHECK(compact_hash != spacious_hash);
}

TEST_CASE("[Editor][InspectorDensity] generated theme produces distinct ordered inspector_property_height") {
	for (const String &style : { String("Modern"), String("Classic") }) {
		String previous_style = EDITOR_GET("interface/theme/style");
		EditorSettings::get_singleton()->set_manually("interface/theme/style", style);

		Ref<EditorTheme> compact_theme = generate_theme_for_density("Compact");
		Ref<EditorTheme> default_theme = generate_theme_for_density("Default");
		Ref<EditorTheme> spacious_theme = generate_theme_for_density("Spacious");

		REQUIRE(compact_theme.is_valid());
		REQUIRE(default_theme.is_valid());
		REQUIRE(spacious_theme.is_valid());

		int compact_height = compact_theme->get_constant(SNAME("inspector_property_height"), SNAME("Editor"));
		int default_height = default_theme->get_constant(SNAME("inspector_property_height"), SNAME("Editor"));
		int spacious_height = spacious_theme->get_constant(SNAME("inspector_property_height"), SNAME("Editor"));

		CAPTURE(style);
		CHECK(compact_height < default_height);
		CHECK(default_height < spacious_height);

		EditorSettings::get_singleton()->set_manually("interface/theme/style", previous_style);
	}
}

TEST_CASE("[Editor][InspectorDensity] measured EditorProperty minimum height is strictly ordered") {
	Ref<EditorTheme> compact_theme = generate_theme_for_density("Compact");
	Ref<EditorTheme> default_theme = generate_theme_for_density("Default");
	Ref<EditorTheme> spacious_theme = generate_theme_for_density("Spacious");

	int compact_height = measure_property_minimum_height(compact_theme);
	int default_height = measure_property_minimum_height(default_theme);
	int spacious_height = measure_property_minimum_height(spacious_theme);

	CHECK(compact_height < default_height);
	CHECK(default_height < spacious_height);
}

TEST_CASE("[Editor][InspectorDensity] inspector_property_height never drops below the font-height floor") {
	Ref<EditorTheme> compact_theme = generate_theme_for_density("Compact");
	REQUIRE(compact_theme.is_valid());

	Ref<Font> font = compact_theme->get_font(SceneStringName(font), SNAME("LineEdit"));
	int font_size = compact_theme->get_font_size(SceneStringName(font_size), SNAME("LineEdit"));
	int floor_height = (int)(font->get_height(font_size) + 2 * EDSCALE);
	int property_height = compact_theme->get_constant(SNAME("inspector_property_height"), SNAME("Editor"));

	CHECK(property_height >= floor_height);
}

TEST_CASE("[Editor][InspectorDensity] density composes with every spacing preset") {
	String previous_preset = EDITOR_GET("interface/theme/spacing_preset");

	for (const String &preset : { String("Compact"), String("Default"), String("Spacious") }) {
		EditorSettings::get_singleton()->set_manually("interface/theme/spacing_preset", preset);

		Ref<EditorTheme> compact_density_theme = generate_theme_for_density("Compact");
		Ref<EditorTheme> default_density_theme = generate_theme_for_density("Default");

		REQUIRE(compact_density_theme.is_valid());
		REQUIRE(default_density_theme.is_valid());

		int compact_height = measure_property_minimum_height(compact_density_theme);
		int default_height = measure_property_minimum_height(default_density_theme);

		CAPTURE(preset);
		CHECK(compact_height < default_height);
	}

	EditorSettings::get_singleton()->set_manually("interface/theme/spacing_preset", previous_preset);
}

TEST_CASE("[Editor][InspectorDensity] setting name is under a group the outdated-theme check watches") {
	static const char *WATCHED_GROUPS[] = {
		"interface/theme",
		"interface/editor/font",
		"interface/editor/main_font",
		"interface/editor/code_font",
		"editors/visual_editors",
		"text_editor/theme",
		"text_editor/help/help",
		"docks/property_editor/subresource_hue_tint",
		"filesystem/file_dialog/thumbnail_size",
		"run/output/font_size",
	};

	const String setting_name = "interface/theme/inspector_density";
	bool matched = false;
	for (const char *group : WATCHED_GROUPS) {
		if (setting_name.begins_with(group)) {
			matched = true;
			break;
		}
	}

	CHECK(matched);

	EditorSettings::get_singleton()->mark_setting_changed(setting_name);
	CHECK(EditorSettings::get_singleton()->check_changed_settings_in_group("interface/theme"));
}

} // namespace TestInspectorDensity
