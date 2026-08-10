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

#include "editor/gui/editor_spin_slider.h"
#include "editor/inspector/editor_inspector.h"
#include "editor/inspector/editor_properties.h"
#include "editor/inspector/editor_properties_array_dict.h"
#include "editor/settings/editor_settings.h"
#include "editor/themes/editor_theme.h"
#include "editor/themes/editor_theme_manager.h"

#include "core/object/class_db.h"
#include "core/object/ref_counted.h"
#include "scene/main/scene_tree.h"
#include "scene/main/window.h"

#include "tests/test_macros.h"

// Friend accessor granting the test suite access to `EditorPropertyArray` internals without
// widening its public API. Declared as a friend in editor/inspector/editor_properties_array_dict.h.
class EditorPropertyArrayTestAccess {
public:
	static EditorSpinSlider *get_size_slider(EditorPropertyArray *p_editor) { return p_editor->size_slider; }
};

namespace TestInspectorDensity {

// A minimal native class exposing a typed `float` array property, registered with ClassDB so
// `EditorPropertyArray::set_object_and_property()`/`update_property()` exercise their real
// `Object::get()` path instead of a test-only shortcut.
class ArrayPropertyFixtureObject : public RefCounted {
	FOUNDRY_CLASS(ArrayPropertyFixtureObject, RefCounted);

	Array items;

protected:
	static void _bind_methods() {
		ClassDB::bind_method(D_METHOD("set_items", "value"), &ArrayPropertyFixtureObject::set_items);
		ClassDB::bind_method(D_METHOD("get_items"), &ArrayPropertyFixtureObject::get_items);
		ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "items"), "set_items", "get_items");
	}

public:
	void set_items(const Array &p_value) { items = p_value; }
	Array get_items() const { return items; }
};

static void ensure_array_fixture_registered() {
	if (!ClassDB::class_exists("ArrayPropertyFixtureObject")) {
		FOUNDRY_REGISTER_CLASS(ArrayPropertyFixtureObject);
	}
}

// `EditorPropertyArray` only builds its unfolded body (including `size_slider`) inside
// `update_property()`, and only when the edited object reports the section as unfolded. The child
// `EditorSpinSlider` is created after `set_theme()` has already run on the editor, so unlike a
// property whose spin slider exists from construction, this one only receives
// `NOTIFICATION_THEME_CHANGED` (and populates its `theme_cache`) if the editor is already inside the
// `SceneTree` when the child is parented, matching the trap documented on
// `measure_float_property_minimum_height`.
static int measure_array_size_slider_minimum_height(const Ref<EditorTheme> &p_theme) {
	ensure_array_fixture_registered();
	Ref<ArrayPropertyFixtureObject> object = memnew(ArrayPropertyFixtureObject);
	Array items;
	items.push_back(1.0);
	items.push_back(2.0);
	object->set_items(items);

	EditorPropertyArray *editor = memnew(EditorPropertyArray);
	editor->setup(Variant::ARRAY);
	SceneTree::get_singleton()->get_root()->add_child(editor);
	editor->set_theme(p_theme);
	editor->set_object_and_property(object.ptr(), "items");
	object->editor_set_section_unfold("items", true);
	editor->update_property();

	EditorSpinSlider *size_slider = EditorPropertyArrayTestAccess::get_size_slider(editor);
	REQUIRE(size_slider != nullptr);
	int height = (int)size_slider->get_minimum_size().height;

	SceneTree::get_singleton()->get_root()->remove_child(editor);
	memdelete(editor);
	return height;
}

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
	// `set_theme` only notifies descendants of NOTIFICATION_THEME_CHANGED when the control is
	// inside the scene tree; this control is deliberately standalone, so refresh its theme item
	// cache directly the same way entering the tree would.
	property->notification(Control::NOTIFICATION_THEME_CHANGED);
	int height = (int)property->get_minimum_size().height;
	memdelete(property);
	return height;
}

static int measure_text_property_minimum_height(const Ref<EditorTheme> &p_theme) {
	EditorPropertyText *property = memnew(EditorPropertyText);
	property->set_theme(p_theme);
	property->notification(Control::NOTIFICATION_THEME_CHANGED);
	int height = (int)property->get_minimum_size().height;
	memdelete(property);
	return height;
}

// EditorProperty::get_minimum_size() only folds a child's minimum size into the row height when
// Container::as_sortable_control() considers that child visible in tree, which requires the
// property to actually be inside the SceneTree (a standalone control's `parent_visible_in_tree`
// defaults to false). Measuring EditorPropertyFloat outside the tree would silently degrade to just
// the container's own `inspector_property_height` floor and never exercise the spin slider's own
// get_minimum_size() at all, so this attaches the property to the root window for the duration of
// the measurement.
static int measure_float_property_minimum_height(const Ref<EditorTheme> &p_theme) {
	EditorPropertyFloat *property = memnew(EditorPropertyFloat);
	property->set_theme(p_theme);
	SceneTree::get_singleton()->get_root()->add_child(property);
	property->notification(Control::NOTIFICATION_THEME_CHANGED);
	int height = (int)property->get_minimum_size().height;
	SceneTree::get_singleton()->get_root()->remove_child(property);
	memdelete(property);
	return height;
}

static int measure_standalone_spin_slider_minimum_height(const Ref<EditorTheme> &p_theme) {
	EditorSpinSlider *spin_slider = memnew(EditorSpinSlider);
	spin_slider->set_theme(p_theme);
	spin_slider->notification(Control::NOTIFICATION_THEME_CHANGED);
	int height = (int)spin_slider->get_minimum_size().height;
	memdelete(spin_slider);
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

TEST_CASE("[Editor][InspectorDensity] measured EditorPropertyText minimum height is strictly ordered") {
	// EditorPropertyText's LineEdit child previously read the unscaled base "LineEdit" stylebox,
	// so its minimum size dominated EditorProperty's row height and Compact/Spacious had no
	// visible effect on the most common (String) property rows. The LineEdit must use the
	// density-scaled "EditorInspectorLineEdit" variation for this to shrink and grow correctly.
	Ref<EditorTheme> compact_theme = generate_theme_for_density("Compact");
	Ref<EditorTheme> default_theme = generate_theme_for_density("Default");
	Ref<EditorTheme> spacious_theme = generate_theme_for_density("Spacious");

	int compact_height = measure_text_property_minimum_height(compact_theme);
	int default_height = measure_text_property_minimum_height(default_theme);
	int spacious_height = measure_text_property_minimum_height(spacious_theme);

	CHECK(compact_height < default_height);
	CHECK(default_height < spacious_height);
}

TEST_CASE("[Editor][InspectorDensity] measured EditorPropertyFloat minimum height is strictly ordered") {
	// EditorPropertyFloat is backed directly by an EditorSpinSlider (no LineEdit sibling), so this
	// isolates whether EditorSpinSlider::get_minimum_size() itself responds to inspector density.
	Ref<EditorTheme> compact_theme = generate_theme_for_density("Compact");
	Ref<EditorTheme> default_theme = generate_theme_for_density("Default");
	Ref<EditorTheme> spacious_theme = generate_theme_for_density("Spacious");

	int compact_height = measure_float_property_minimum_height(compact_theme);
	int default_height = measure_float_property_minimum_height(default_theme);
	int spacious_height = measure_float_property_minimum_height(spacious_theme);

	CAPTURE(compact_height);
	CAPTURE(default_height);
	CAPTURE(spacious_height);
	CHECK(compact_height < default_height);
	CHECK(default_height < spacious_height);
}

TEST_CASE("[Editor][InspectorDensity] measured EditorPropertyArray Size row minimum height is strictly ordered") {
	// The array/dictionary "Size:" row sits in the same panel as the element rows below it
	// (editor/inspector/editor_properties_array_dict.cpp), which use the density-scaled
	// EditorPropertyContainer variation. Without EditorInspectorSpinSlider on `size_slider`, this
	// row stays frozen at the unscaled size while its neighbors shrink or grow around it.
	Ref<EditorTheme> compact_theme = generate_theme_for_density("Compact");
	Ref<EditorTheme> default_theme = generate_theme_for_density("Default");
	Ref<EditorTheme> spacious_theme = generate_theme_for_density("Spacious");

	int compact_height = measure_array_size_slider_minimum_height(compact_theme);
	int default_height = measure_array_size_slider_minimum_height(default_theme);
	int spacious_height = measure_array_size_slider_minimum_height(spacious_theme);

	CAPTURE(compact_height);
	CAPTURE(default_height);
	CAPTURE(spacious_height);
	CHECK(compact_height < default_height);
	CHECK(default_height < spacious_height);
}

TEST_CASE("[Editor][InspectorDensity] EditorSpinSlider outside the inspector is unaffected by density") {
	// EditorSpinSlider is also used for viewport zoom, timelines, and other non-inspector chrome.
	// A standalone instance (no EditorInspectorSpinSlider variation applied) must not change height
	// as the inspector density setting changes.
	Ref<EditorTheme> compact_theme = generate_theme_for_density("Compact");
	Ref<EditorTheme> default_theme = generate_theme_for_density("Default");
	Ref<EditorTheme> spacious_theme = generate_theme_for_density("Spacious");

	int compact_height = measure_standalone_spin_slider_minimum_height(compact_theme);
	int default_height = measure_standalone_spin_slider_minimum_height(default_theme);
	int spacious_height = measure_standalone_spin_slider_minimum_height(spacious_theme);

	CAPTURE(compact_height);
	CAPTURE(default_height);
	CAPTURE(spacious_height);
	CHECK(compact_height == default_height);
	CHECK(default_height == spacious_height);
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
