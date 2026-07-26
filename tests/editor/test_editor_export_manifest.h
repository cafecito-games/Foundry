/**************************************************************************/
/*  test_editor_export_manifest.h                                        */
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

#include "editor/export/editor_export_platform.h"
#include "editor/export/editor_export_plugin.h"

#include "tests/test_macros.h"

namespace TestEditorExportManifest {

static const String IMPORTED_PATH = "res://tests/editor/fixtures/export_manifest/imported.keepdata";
static const String SKIPPED_PATH = "res://tests/editor/fixtures/export_manifest/skipped.skipdata";

struct SaveCapture {
	Vector<String> paths;

	static Error save(const Ref<EditorExportPreset> &p_preset, void *p_userdata, const String &p_path, const Vector<uint8_t> &p_data, int p_file, int p_total, const Vector<String> &p_enc_in_filters, const Vector<String> &p_enc_ex_filters, const Vector<uint8_t> &p_key, uint64_t p_seed, bool p_delta) {
		static_cast<SaveCapture *>(p_userdata)->paths.push_back(p_path);
		return OK;
	}
};

class RecordingManifestPlugin : public EditorExportPlugin {
	FOUNDRY_SOFTCLASS(RecordingManifestPlugin, EditorExportPlugin);

	String plugin_name;
	Vector<String> *events = nullptr;

public:
	Vector<String> prepared_sources;
	Vector<String> prepared_generated;
	mutable Vector<String> validated_late_paths;
	Error preparation_error = OK;
	Error late_validation_error = OK;
	bool customize_resources = false;
	bool customize_scenes = false;
	String prepare_generated_path;
	String export_generated_path;

	RecordingManifestPlugin(const String &p_name, Vector<String> *p_events) :
			plugin_name(p_name), events(p_events) {}

	virtual String get_name() const override {
		return plugin_name;
	}

	void queue_initial_file(const String &p_path) {
		add_file(p_path, Vector<uint8_t>(), false);
	}

	Error prepare_for_test(const ExportFileManifest &p_manifest, String &r_error) {
		return _prepare_export_file_manifest(p_manifest, r_error);
	}

protected:
	virtual bool _begin_customize_resources(const Ref<EditorExportPlatform> &p_platform, const Vector<String> &p_features) override {
		events->push_back("begin_resources:" + plugin_name);
		return customize_resources;
	}

	virtual bool _begin_customize_scenes(const Ref<EditorExportPlatform> &p_platform, const Vector<String> &p_features) override {
		events->push_back("begin_scenes:" + plugin_name);
		return customize_scenes;
	}

	virtual void _end_customize_resources() override {
		events->push_back("end_resources:" + plugin_name);
	}

	virtual void _end_customize_scenes() override {
		events->push_back("end_scenes:" + plugin_name);
	}

	virtual uint64_t _get_customization_configuration_hash() const override {
		return plugin_name.hash64();
	}

	virtual Error _prepare_export_file_manifest(const ExportFileManifest &p_manifest, String &r_error) override {
		events->push_back("prepare:" + plugin_name);
		prepared_sources = p_manifest.source_paths;
		prepared_generated = p_manifest.generated_paths;
		if (!prepare_generated_path.is_empty()) {
			add_file(prepare_generated_path, Vector<uint8_t>(), false);
		}
		if (preparation_error != OK) {
			r_error = "manifest preparation rejected";
		}
		return preparation_error;
	}

	virtual Error _validate_late_export_file(const String &p_path, String &r_error) const override {
		events->push_back("validate:" + plugin_name + ":" + p_path);
		validated_late_paths.push_back(p_path);
		if (late_validation_error != OK) {
			r_error = "late generated file rejected";
		}
		return late_validation_error;
	}

	virtual void _export_file(const String &p_path, const String &p_type, const HashSet<String> &p_features) override {
		events->push_back("export_file:" + plugin_name + ":" + p_path);
		if (!export_generated_path.is_empty()) {
			add_file(export_generated_path, Vector<uint8_t>(), false);
		}
	}
};

class TestManifestExportPlatform : public EditorExportPlatform {
	FOUNDRY_SOFTCLASS(TestManifestExportPlatform, EditorExportPlatform);

public:
	virtual void get_preset_features(const Ref<EditorExportPreset> &p_preset, List<String> *r_features) const override {}
	virtual void get_export_options(List<ExportOption> *r_options) const override {}
	virtual String get_name() const override { return "ManifestTest"; }
	virtual String get_os_name() const override { return "ManifestTest"; }
	virtual Ref<Texture2D> get_logo() const override { return Ref<Texture2D>(); }
	virtual bool has_valid_export_configuration(const Ref<EditorExportPreset> &p_preset, String &r_error, bool &r_missing_templates, bool p_debug = false) const override {
		r_missing_templates = false;
		return true;
	}
	virtual bool has_valid_project_configuration(const Ref<EditorExportPreset> &p_preset, String &r_error) const override { return true; }
	virtual List<String> get_binary_extensions(const Ref<EditorExportPreset> &p_preset) const override { return List<String>(); }
	virtual Error export_project(const Ref<EditorExportPreset> &p_preset, bool p_debug, const String &p_path, BitField<EditorExportPlatform::DebugFlags> p_flags = 0) override { return OK; }
	virtual void get_platform_features(List<String> *r_features) const override {}

	Error export_candidates(const Ref<EditorExportPreset> &p_preset, const HashSet<String> &p_paths, const Vector<Ref<EditorExportPlugin>> &p_plugins, SaveCapture &r_capture) {
		return _export_project_files_with_manifest(p_preset, false, p_paths, p_plugins, SaveCapture::save, nullptr, &r_capture, nullptr);
	}
};

TEST_CASE("[Editor][ExportManifest] Native hooks receive owned manifest snapshots") {
	Vector<String> events;
	Ref<RecordingManifestPlugin> plugin = memnew(RecordingManifestPlugin("Plugin", &events));
	EditorExportPlugin::ExportFileManifest manifest;
	manifest.source_paths.push_back("res://a.fs");
	manifest.source_paths.push_back("res://b.tscn");
	manifest.generated_paths.push_back("res://generated.fsb");

	String error;
	CHECK_EQ(plugin->prepare_for_test(manifest, error), OK);
	CHECK_EQ(plugin->prepared_sources, manifest.source_paths);
	CHECK_EQ(plugin->prepared_generated, manifest.generated_paths);

	manifest.source_paths.clear();
	manifest.generated_paths.clear();
	CHECK_EQ(plugin->prepared_sources.size(), 2);
	CHECK_EQ(plugin->prepared_generated.size(), 1);
}

TEST_CASE("[Editor][ExportManifest] Preparation sees a sorted effective manifest and aborts before output") {
	Ref<TestManifestExportPlatform> platform = memnew(TestManifestExportPlatform);
	Ref<EditorExportPreset> preset = platform->create_preset();

	HashSet<String> paths;
	paths.insert("res://manifest/root.tres");
	paths.insert("res://manifest/dependency.tres");
	paths.insert("res://manifest/included.tres");
	paths.insert(IMPORTED_PATH);
	paths.insert(SKIPPED_PATH);

	Vector<String> events;
	Ref<RecordingManifestPlugin> bravo = memnew(RecordingManifestPlugin("Bravo", &events));
	Ref<RecordingManifestPlugin> alpha = memnew(RecordingManifestPlugin("Alpha", &events));
	alpha->customize_resources = true;
	alpha->customize_scenes = true;
	alpha->queue_initial_file("res://generated/zeta.bin");
	bravo->queue_initial_file("res://generated/alpha.bin");
	bravo->preparation_error = ERR_INVALID_DATA;

	Vector<Ref<EditorExportPlugin>> plugins;
	plugins.push_back(bravo);
	plugins.push_back(alpha);

	SaveCapture capture;
	CHECK_EQ(platform->export_candidates(preset, paths, plugins, capture), ERR_INVALID_DATA);
	CHECK(capture.paths.is_empty());

	const Vector<String> expected_sources = {
		"res://manifest/dependency.tres",
		"res://manifest/included.tres",
		"res://manifest/root.tres",
		IMPORTED_PATH,
	};
	const Vector<String> expected_generated = {
		"res://generated/alpha.bin",
		"res://generated/zeta.bin",
	};
	CHECK_EQ(alpha->prepared_sources, expected_sources);
	CHECK_EQ(alpha->prepared_generated, expected_generated);
	CHECK_EQ(bravo->prepared_sources, expected_sources);
	CHECK_EQ(bravo->prepared_generated, expected_generated);
	CHECK_EQ(alpha->prepared_sources.find(SKIPPED_PATH), -1);
	CHECK_EQ(alpha->prepared_sources.find("res://manifest/excluded.tres"), -1);

	CHECK_EQ(events[0], "begin_resources:Alpha");
	CHECK_EQ(events[1], "begin_scenes:Alpha");
	CHECK_EQ(events[2], "begin_resources:Bravo");
	CHECK_EQ(events[3], "begin_scenes:Bravo");
	CHECK_EQ(events[4], "prepare:Alpha");
	CHECK_EQ(events[5], "prepare:Bravo");
	CHECK_NE(events.find("end_resources:Alpha"), -1);
	CHECK_NE(events.find("end_scenes:Alpha"), -1);

	REQUIRE_GT(platform->get_message_count(), 0);
	const EditorExportPlatform::ExportMessage &message = platform->get_message(platform->get_message_count() - 1);
	CHECK_EQ(message.msg_type, EditorExportPlatform::EXPORT_MESSAGE_ERROR);
	CHECK(message.text.contains("Bravo"));
	CHECK(message.text.contains("manifest preparation rejected"));
}

TEST_CASE("[Editor][ExportManifest] Late generated files are rejected before their save callback") {
	Ref<TestManifestExportPlatform> platform = memnew(TestManifestExportPlatform);
	Ref<EditorExportPreset> preset = platform->create_preset();

	HashSet<String> paths;
	paths.insert("res://manifest/source.fs");

	Vector<String> events;
	Ref<RecordingManifestPlugin> bravo = memnew(RecordingManifestPlugin("Bravo", &events));
	Ref<RecordingManifestPlugin> alpha = memnew(RecordingManifestPlugin("Alpha", &events));
	alpha->export_generated_path = "res://generated/late.fsb";
	bravo->late_validation_error = ERR_INVALID_DATA;

	Vector<Ref<EditorExportPlugin>> plugins;
	plugins.push_back(bravo);
	plugins.push_back(alpha);

	SaveCapture capture;
	CHECK_EQ(platform->export_candidates(preset, paths, plugins, capture), ERR_INVALID_DATA);
	CHECK_EQ(capture.paths.find("res://generated/late.fsb"), -1);
	CHECK_EQ(alpha->validated_late_paths, Vector<String>({ "res://generated/late.fsb" }));
	CHECK_EQ(bravo->validated_late_paths, Vector<String>({ "res://generated/late.fsb" }));

	const int export_index = events.find("export_file:Alpha:res://manifest/source.fs");
	const int alpha_validation_index = events.find("validate:Alpha:res://generated/late.fsb");
	const int bravo_validation_index = events.find("validate:Bravo:res://generated/late.fsb");
	CHECK_GE(export_index, 0);
	CHECK_GT(alpha_validation_index, export_index);
	CHECK_GT(bravo_validation_index, alpha_validation_index);
}

TEST_CASE("[Editor][ExportManifest] Only files pending before preparation are exempt from late validation") {
	Ref<TestManifestExportPlatform> platform = memnew(TestManifestExportPlatform);
	Ref<EditorExportPreset> preset = platform->create_preset();

	Vector<String> events;
	Ref<RecordingManifestPlugin> bravo = memnew(RecordingManifestPlugin("Bravo", &events));
	Ref<RecordingManifestPlugin> alpha = memnew(RecordingManifestPlugin("Alpha", &events));
	alpha->queue_initial_file("res://generated/initial.bin");
	alpha->prepare_generated_path = "res://generated/from_prepare.fsb";
	bravo->late_validation_error = ERR_INVALID_DATA;

	Vector<Ref<EditorExportPlugin>> plugins;
	plugins.push_back(bravo);
	plugins.push_back(alpha);

	SaveCapture capture;
	CHECK_EQ(platform->export_candidates(preset, HashSet<String>(), plugins, capture), ERR_INVALID_DATA);
	CHECK_NE(capture.paths.find("res://generated/initial.bin"), -1);
	CHECK_EQ(capture.paths.find("res://generated/from_prepare.fsb"), -1);
	CHECK_EQ(alpha->prepared_generated, Vector<String>({ "res://generated/initial.bin" }));
	CHECK_EQ(alpha->validated_late_paths, Vector<String>({ "res://generated/from_prepare.fsb" }));
	CHECK_EQ(bravo->validated_late_paths, Vector<String>({ "res://generated/from_prepare.fsb" }));
}

} // namespace TestEditorExportManifest

#endif // TOOLS_ENABLED
