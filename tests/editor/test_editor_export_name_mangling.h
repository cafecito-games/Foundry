/**************************************************************************/
/*  test_editor_export_name_mangling.h                                    */
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

#ifdef TOOLS_ENABLED

#include "core/io/config_file.h"
#include "editor/export/editor_export_platform.h"

#include "scene/resources/texture.h"

#include "tests/test_macros.h"

namespace TestEditorExportNameMangling {

class TestNameManglerExportPlatform : public EditorExportPlatform {
	FOUNDRY_SOFTCLASS(TestNameManglerExportPlatform, EditorExportPlatform);

public:
	virtual void get_preset_features(const Ref<EditorExportPreset> &p_preset, List<String> *r_features) const override {}
	virtual void get_export_options(List<ExportOption> *r_options) const override {}
	virtual String get_name() const override { return "NameManglerTest"; }
	virtual String get_os_name() const override { return "NameManglerTest"; }
	virtual Ref<Texture2D> get_logo() const override { return Ref<Texture2D>(); }
	virtual bool has_valid_export_configuration(const Ref<EditorExportPreset> &p_preset, String &r_error, bool &r_missing_templates, bool p_debug = false) const override {
		r_missing_templates = false;
		return true;
	}
	virtual bool has_valid_project_configuration(const Ref<EditorExportPreset> &p_preset, String &r_error) const override { return true; }
	virtual List<String> get_binary_extensions(const Ref<EditorExportPreset> &p_preset) const override { return List<String>(); }
	virtual Error export_project(const Ref<EditorExportPreset> &p_preset, bool p_debug, const String &p_path, BitField<EditorExportPlatform::DebugFlags> p_flags = 0) override { return OK; }
	virtual void get_platform_features(List<String> *r_features) const override {}
};

TEST_CASE("[Editor][Export][NameMangler] Presets retain explicit mangling state across script modes") {
	Ref<TestNameManglerExportPlatform> platform = memnew(TestNameManglerExportPlatform);
	Ref<EditorExportPreset> preset = platform->create_preset();

	CHECK_FALSE(preset->is_script_name_mangling_enabled());
	CHECK(preset->get_script_name_mangling_keep_rules().is_empty());
	CHECK(preset->is_script_name_mangling_available());

	preset->set_script_name_mangling_enabled(true);
	preset->set_script_name_mangling_keep_rules("res://mangling.keep");
	CHECK(preset->is_script_name_mangling_enabled());
	CHECK_EQ(preset->get_script_name_mangling_keep_rules(), "res://mangling.keep");

	preset->set_script_export_mode(EditorExportPreset::MODE_SCRIPT_TEXT);
	CHECK_FALSE(preset->is_script_name_mangling_available());
	CHECK(preset->is_script_name_mangling_enabled());
	CHECK_EQ(preset->get_script_name_mangling_keep_rules(), "res://mangling.keep");

	preset->set_script_name_mangling_enabled(false);
	CHECK_FALSE(preset->is_script_name_mangling_enabled());
	CHECK_EQ(preset->get_script_name_mangling_keep_rules(), "res://mangling.keep");
}

TEST_CASE("[Editor][Export][NameMangler] Enabled mangling outside bytecode mode invalidates the preset") {
	Ref<TestNameManglerExportPlatform> platform = memnew(TestNameManglerExportPlatform);
	Ref<EditorExportPreset> preset = platform->create_preset();
	preset->set_script_export_mode(EditorExportPreset::MODE_SCRIPT_TEXT);
	preset->set_script_name_mangling_enabled(true);

	String error;
	bool missing_templates = true;
	CHECK_FALSE(platform->can_export(preset, error, missing_templates));
	CHECK_FALSE(missing_templates);
	CHECK(error.contains("Foundry Script name mangling requires the Compiled bytecode script export mode."));
}

TEST_CASE("[Editor][Export][NameMangler] Preset config keys round-trip through the accessors") {
	Ref<TestNameManglerExportPlatform> platform = memnew(TestNameManglerExportPlatform);
	Ref<EditorExportPreset> source = platform->create_preset();
	source->set_script_name_mangling_enabled(true);
	source->set_script_name_mangling_keep_rules("res://export/mangling.keep");

	Ref<ConfigFile> config;
	config.instantiate();
	const String section = "preset.0";
	config->set_value(section, "script_name_mangling_enabled", source->is_script_name_mangling_enabled());
	config->set_value(section, "script_name_mangling_keep_rules", source->get_script_name_mangling_keep_rules());

	CHECK(config->has_section_key(section, "script_name_mangling_enabled"));
	CHECK(config->has_section_key(section, "script_name_mangling_keep_rules"));

	Ref<EditorExportPreset> restored = platform->create_preset();
	restored->set_script_name_mangling_enabled(config->get_value(section, "script_name_mangling_enabled", false));
	restored->set_script_name_mangling_keep_rules(config->get_value(section, "script_name_mangling_keep_rules", String()));

	CHECK(restored->is_script_name_mangling_enabled());
	CHECK_EQ(restored->get_script_name_mangling_keep_rules(), "res://export/mangling.keep");
}

TEST_CASE("[Editor][Export][NameMangler] Preset duplication copies both mangling values") {
	Ref<TestNameManglerExportPlatform> platform = memnew(TestNameManglerExportPlatform);
	Ref<EditorExportPreset> source = platform->create_preset();
	source->set_script_name_mangling_enabled(true);
	source->set_script_name_mangling_keep_rules("res://export/mangling.keep");

	Ref<EditorExportPreset> duplicate = platform->create_preset();
	duplicate->copy_script_name_mangling_settings_from(source);

	CHECK(duplicate->is_script_name_mangling_enabled());
	CHECK_EQ(duplicate->get_script_name_mangling_keep_rules(), "res://export/mangling.keep");
}

} // namespace TestEditorExportNameMangling

#endif // TOOLS_ENABLED
