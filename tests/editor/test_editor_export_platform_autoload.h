/**************************************************************************/
/*  test_editor_export_platform_autoload.h                                */
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

#ifdef TOOLS_ENABLED

#include "modules/modules_enabled.gen.h"

#ifdef MODULE_FOUNDRY_SCRIPT_ENABLED

#include "editor/export/editor_export_platform.h"

#include "core/config/project_settings.h"
#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/object/script_language.h"
#include "core/os/os.h"
#include "scene/resources/texture.h"

#include "tests/test_macros.h"
#include "tests/test_utils.h"

namespace TestEditorExportPlatformAutoload {

class ScopedScriptServerClass {
	StringName class_name;

public:
	ScopedScriptServerClass(const StringName &p_class_name, const String &p_base, const String &p_path) {
		class_name = p_class_name;
		ScriptServer::remove_global_class(class_name);
		ScriptServer::add_global_class(class_name, p_base, SNAME("FoundryScript"), p_path, false, false, false);
	}

	~ScopedScriptServerClass() {
		ScriptServer::remove_global_class(class_name);
	}
};

class ScopedProjectSettings {
	Vector<StringName> names;

	void clear_name(const StringName &p_name) {
		ProjectSettings *project_settings = ProjectSettings::get_singleton();
		if (project_settings->has_setting(p_name)) {
			project_settings->clear(p_name);
		}

		const String setting_name = p_name;
		if (setting_name.begins_with("autoload/") || setting_name.begins_with("autoload_prepend/")) {
			const StringName autoload_name = setting_name.get_slicec('/', 1);
			if (project_settings->has_autoload(autoload_name)) {
				project_settings->remove_autoload(autoload_name);
			}
		}
	}

public:
	~ScopedProjectSettings() {
		for (const StringName &name : names) {
			clear_name(name);
		}
	}

	void set(const StringName &p_name, const Variant &p_value) {
		clear_name(p_name);
		names.push_back(p_name);
		ProjectSettings::get_singleton()->set_setting(p_name, p_value);
	}
};

class ScopedExportTempFiles {
	String root;
	Vector<String> files;
	Vector<String> directories;

public:
	ScopedExportTempFiles(const String &p_name) {
		root = TestUtils::get_temp_path(p_name + "_" + itos(OS::get_singleton()->get_ticks_usec()));
		REQUIRE_EQ(DirAccess::make_dir_recursive_absolute(root), OK);
		directories.push_back(root);
	}

	~ScopedExportTempFiles() {
		for (const String &file : files) {
			DirAccess::remove_absolute(file);
		}
		for (int i = directories.size() - 1; i >= 0; i--) {
			DirAccess::remove_absolute(directories[i]);
		}
	}

	String write(const String &p_file_name, const String &p_contents) {
		const String path = root.path_join(p_file_name);
		Ref<FileAccess> file = FileAccess::open(path, FileAccess::WRITE);
		REQUIRE(file.is_valid());
		file->store_string(p_contents);
		files.push_back(path);
		return path;
	}

	String make_directory(const String &p_dir_name) {
		const String path = root.path_join(p_dir_name);
		REQUIRE_EQ(DirAccess::make_dir_recursive_absolute(path), OK);
		directories.push_back(path);
		return path;
	}
};

class TestPresetOverrideExportPlatform : public EditorExportPlatform {
	FOUNDRY_SOFTCLASS(TestPresetOverrideExportPlatform, EditorExportPlatform);

public:
	virtual void get_preset_features(const Ref<EditorExportPreset> &p_preset, List<String> *r_features) const override {}
	virtual void get_export_options(List<ExportOption> *r_options) const override {}
	virtual String get_name() const override { return "Test"; }
	virtual String get_os_name() const override { return "Test"; }
	virtual Ref<Texture2D> get_logo() const override { return Ref<Texture2D>(); }
	virtual bool has_valid_export_configuration(const Ref<EditorExportPreset> &p_preset, String &r_error, bool &r_missing_templates, bool p_debug = false) const override { return true; }
	virtual bool has_valid_project_configuration(const Ref<EditorExportPreset> &p_preset, String &r_error) const override { return true; }
	virtual List<String> get_binary_extensions(const Ref<EditorExportPreset> &p_preset) const override { return List<String>(); }
	virtual Error export_project(const Ref<EditorExportPreset> &p_preset, bool p_debug, const String &p_path, BitField<EditorExportPlatform::DebugFlags> p_flags = 0) override { return OK; }
	virtual void get_platform_features(List<String> *r_features) const override {}

	Error collect_autoload_export_paths(const Ref<EditorExportPreset> &p_preset, Vector<String> &r_paths) const {
		return _collect_autoload_export_paths(p_preset, r_paths);
	}
};

TEST_CASE("[Editor][Export] Script-owned autoload cache write failure is a hard forced-file error") {
	ScopedExportTempFiles files("editor_export_autoload_cache_failure");

	const String script_path = files.write("export_cache_required.fs",
			"@autoload\n"
			"class_name EditorExportAutoloadCacheRequired extends Node\n");
	ScopedScriptServerClass registered_autoload(
			SNAME("EditorExportAutoloadCacheRequired"),
			"Node",
			script_path);

	const String blocked_cache_path = files.make_directory("autoload_index_cache.cfg");

	Vector<String> forced_files;
	const Error err = EditorExportPlatform::collect_forced_export_files(
			Ref<EditorExportPreset>(),
			forced_files,
			true,
			blocked_cache_path);

	CHECK_NE(err, OK);
	CHECK_EQ(forced_files.find(blocked_cache_path), -1);
}

TEST_CASE("[Editor][Export] Legacy forced-file helper rejects autoload index rebuild errors") {
	ScopedExportTempFiles files("editor_export_legacy_autoload_rebuild_failure");

	const String script_path = files.write("legacy_invalid_autoload.fs",
			"@autoload\n"
			"class_name EditorExportLegacyInvalidAutoload extends RefCounted\n");
	ScopedScriptServerClass registered_autoload(
			SNAME("EditorExportLegacyInvalidAutoload"),
			"RefCounted",
			script_path);

	ERR_PRINT_OFF;
	Vector<String> forced_files = EditorExportPlatform::get_forced_export_files(Ref<EditorExportPreset>());
	ERR_PRINT_ON;

	CHECK(forced_files.is_empty());
}

TEST_CASE("[Editor][Export] Selected autoload dependencies honor preset feature overrides") {
	ScopedExportTempFiles files("editor_export_autoload_preset_override");
	ScopedProjectSettings settings;

	const String base_path = files.write("preset_override_base.fs", "extends Node\n");
	const String override_path = files.write("preset_override_feature.fs", "extends Node\n");

	settings.set(SNAME("autoload/EditorExportPresetOverrideAutoload"), "*" + base_path);
	settings.set(SNAME("autoload/EditorExportPresetOverrideAutoload.feature_override"), "*" + override_path);

	Ref<TestPresetOverrideExportPlatform> platform = memnew(TestPresetOverrideExportPlatform);
	Ref<EditorExportPreset> preset = platform->create_preset();
	preset->set_export_filter(EditorExportPreset::EXPORT_SELECTED_RESOURCES);
	preset->set_custom_features("feature_override");

	Vector<String> autoload_paths;
	const Error err = platform->collect_autoload_export_paths(preset, autoload_paths);

	REQUIRE_EQ(err, OK);
	CHECK_NE(autoload_paths.find(override_path), -1);
	CHECK_EQ(autoload_paths.find(base_path), -1);

	Ref<EditorExportPreset> base_preset = platform->create_preset();
	base_preset->set_export_filter(EditorExportPreset::EXPORT_SELECTED_RESOURCES);

	Vector<String> base_autoload_paths;
	const Error base_err = platform->collect_autoload_export_paths(base_preset, base_autoload_paths);

	REQUIRE_EQ(base_err, OK);
	CHECK_NE(base_autoload_paths.find(base_path), -1);
	CHECK_EQ(base_autoload_paths.find(override_path), -1);
}

} // namespace TestEditorExportPlatformAutoload

#endif // MODULE_FOUNDRY_SCRIPT_ENABLED

#endif // TOOLS_ENABLED
