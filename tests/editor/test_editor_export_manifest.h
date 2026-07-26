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
#include "editor/file_system/editor_file_system.h"

#include "core/os/os.h"
#include "tests/core/config/test_project_settings.h"
#include "tests/test_macros.h"

namespace TestEditorExportManifest {

static const String IMPORTED_PATH = "res://tests/editor/fixtures/export_manifest/imported.keepdata";
static const String MALFORMED_IMPORTED_PATH = "res://tests/editor/fixtures/export_manifest/malformed.keepdata";
static const String SKIPPED_PATH = "res://tests/editor/fixtures/export_manifest/skipped.skipdata";
static const String CUSTOMIZED_IMPORTED_RESOURCE_PATH = "res://tests/editor/fixtures/export_manifest/customized_imported_resource.tres";
static const String CUSTOMIZED_RESOURCE_PATH = "res://tests/editor/fixtures/export_manifest/customized_resource.tres";
static const String CUSTOMIZED_SCENE_PATH = "res://tests/editor/fixtures/export_manifest/customized_scene.tscn";

class ScopedManifestExportScratch {
	String saved_project_data_dir_name;
	String scoped_root;
	Error setup_error = OK;

public:
	ScopedManifestExportScratch() {
		saved_project_data_dir_name = ProjectSettings::get_singleton()->get_project_data_dir_name();

		Ref<DirAccess> filesystem = DirAccess::create(DirAccess::ACCESS_FILESYSTEM);
		String resource_root = ProjectSettings::get_singleton()->get_resource_path();
		if (resource_root.is_empty()) {
			resource_root = filesystem->get_current_dir();
		}
		resource_root = resource_root.simplify_path();

		String scratch_root = resource_root.path_join(".test_scratch");
		if (OS::get_singleton()->has_environment("FOUNDRY_TEST_SCRATCH")) {
			const String configured_root = OS::get_singleton()->get_environment("FOUNDRY_TEST_SCRATCH");
			if (!configured_root.is_empty()) {
				scratch_root = configured_root;
			}
		}
		scratch_root = scratch_root.simplify_path();

		String relative_scratch_root = resource_root.path_to(scratch_root).trim_suffix("/");
		if (relative_scratch_root.is_absolute_path() || relative_scratch_root == ".." || relative_scratch_root.begins_with("../")) {
			setup_error = ERR_INVALID_PARAMETER;
			return;
		}

		scoped_root = scratch_root.path_join("editor_export_manifest_process_" + itos(OS::get_singleton()->get_process_id()));
		const String project_data_path = scoped_root.path_join(".foundry");
		setup_error = filesystem->make_dir_recursive(project_data_path);
		if (setup_error != OK) {
			return;
		}

		TestProjectSettingsInternalsAccessor::project_data_dir_name() = resource_root.path_to(project_data_path).trim_suffix("/");
	}

	~ScopedManifestExportScratch() {
		TestProjectSettingsInternalsAccessor::project_data_dir_name() = saved_project_data_dir_name;
		if (scoped_root.is_empty()) {
			return;
		}

		Ref<DirAccess> cleanup = DirAccess::open(scoped_root);
		if (cleanup.is_null()) {
			return;
		}
		cleanup->set_include_hidden(true);
		const Error erase_error = cleanup->erase_contents_recursive();
		CHECK_EQ(erase_error, OK);
		cleanup.unref();
		if (erase_error == OK) {
			CHECK_EQ(DirAccess::remove_absolute(scoped_root), OK);
		}
	}

	Error get_setup_error() const {
		return setup_error;
	}
};

class ScopedManifestEditorFileSystem {
	EditorFileSystem *editor_file_system = nullptr;
	Error setup_error = OK;

public:
	ScopedManifestEditorFileSystem() {
		if (EditorFileSystem::get_singleton() != nullptr) {
			setup_error = ERR_ALREADY_IN_USE;
			return;
		}

		editor_file_system = memnew(EditorFileSystem);
		if (EditorFileSystem::get_singleton() != editor_file_system) {
			setup_error = ERR_CANT_CREATE;
		}
	}

	~ScopedManifestEditorFileSystem() {
		if (editor_file_system == nullptr) {
			return;
		}

		memdelete(editor_file_system);
		CHECK(EditorFileSystem::get_singleton() == nullptr);
	}

	Error get_setup_error() const {
		return setup_error;
	}
};

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
	String customize_resource_path;
	String customize_scene_path;
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

	virtual Ref<Resource> _customize_resource(const Ref<Resource> &p_resource, const String &p_path) override {
		events->push_back("customize_resource:" + plugin_name + ":" + p_path);
		if (p_path == customize_resource_path) {
			return p_resource;
		}
		return Ref<Resource>();
	}

	virtual Node *_customize_scene(Node *p_root, const String &p_path) override {
		events->push_back("customize_scene:" + plugin_name + ":" + p_path);
		return p_path == customize_scene_path ? p_root : nullptr;
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

	Error export_candidates_in_current_environment(const Ref<EditorExportPreset> &p_preset, const HashSet<String> &p_paths, const Vector<Ref<EditorExportPlugin>> &p_plugins, SaveCapture &r_capture) {
		const Error error = _export_project_files_with_manifest(p_preset, false, p_paths, p_plugins, SaveCapture::save, nullptr, &r_capture, nullptr);
		const String workspace_cache_path = DirAccess::create(DirAccess::ACCESS_FILESYSTEM)->get_current_dir().path_join("exported");
		CHECK_FALSE(DirAccess::dir_exists_absolute(workspace_cache_path));
		return error;
	}

	Error export_candidates(const Ref<EditorExportPreset> &p_preset, const HashSet<String> &p_paths, const Vector<Ref<EditorExportPlugin>> &p_plugins, SaveCapture &r_capture) {
		ScopedManifestExportScratch scratch;
		REQUIRE_EQ(scratch.get_setup_error(), OK);
		if (scratch.get_setup_error() != OK) {
			return scratch.get_setup_error();
		}

		ScopedManifestEditorFileSystem editor_file_system;
		REQUIRE_EQ(editor_file_system.get_setup_error(), OK);
		if (editor_file_system.get_setup_error() != OK) {
			return editor_file_system.get_setup_error();
		}

		return export_candidates_in_current_environment(
				p_preset, p_paths, p_plugins, r_capture);
	}
};

static Error downgrade_customization_cache_to_legacy_format() {
	const String cache_root_path =
			ProjectSettings::get_singleton()
					->get_project_data_path()
					.path_join("exported");
	Ref<DirAccess> cache_root = DirAccess::open(cache_root_path);
	CHECK(cache_root.is_valid());
	if (cache_root.is_null()) {
		return ERR_FILE_NOT_FOUND;
	}

	const PackedStringArray cache_directories =
			cache_root->get_directories();
	CHECK_EQ(cache_directories.size(), 1);
	if (cache_directories.size() != 1) {
		return ERR_INVALID_DATA;
	}

	const String cache_path =
			cache_root_path.path_join(cache_directories[0])
					.path_join("file_cache");
	Ref<FileAccess> cache = FileAccess::open(cache_path, FileAccess::READ);
	CHECK(cache.is_valid());
	if (cache.is_null()) {
		return ERR_FILE_CANT_OPEN;
	}

	const Vector<String> lines = cache->get_as_text().split(
			"\n", false);
	cache.unref();
	String legacy_contents;
	for (const String &line : lines) {
		const Vector<String> fields = line.split("::", true);
		CHECK_GE(fields.size(), 5);
		if (fields.size() < 5) {
			return ERR_INVALID_DATA;
		}
		legacy_contents += fields[0] + "::" + fields[1] + "::" +
				fields[2] + "::" + fields[3] + "\n";
	}

	cache = FileAccess::open(cache_path, FileAccess::WRITE);
	CHECK(cache.is_valid());
	if (cache.is_null()) {
		return ERR_FILE_CANT_OPEN;
	}
	cache->store_string(legacy_contents);
	return OK;
}

static Error read_customization_cache_fields(const String &p_source_path, Vector<String> &r_fields) {
	const String cache_root_path =
			ProjectSettings::get_singleton()
					->get_project_data_path()
					.path_join("exported");
	Ref<DirAccess> cache_root = DirAccess::open(cache_root_path);
	CHECK(cache_root.is_valid());
	if (cache_root.is_null()) {
		return ERR_FILE_NOT_FOUND;
	}

	const PackedStringArray cache_directories =
			cache_root->get_directories();
	CHECK_EQ(cache_directories.size(), 1);
	if (cache_directories.size() != 1) {
		return ERR_INVALID_DATA;
	}

	const String cache_path =
			cache_root_path.path_join(cache_directories[0])
					.path_join("file_cache");
	Ref<FileAccess> cache = FileAccess::open(cache_path, FileAccess::READ);
	CHECK(cache.is_valid());
	if (cache.is_null()) {
		return ERR_FILE_CANT_OPEN;
	}

	const Vector<String> lines = cache->get_as_text().split(
			"\n", false);
	for (const String &line : lines) {
		const Vector<String> fields = line.split("::", true);
		if (!fields.is_empty() && fields[0] == p_source_path) {
			r_fields = fields;
			return OK;
		}
	}
	return ERR_DOES_NOT_EXIST;
}

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

TEST_CASE("[Editor][ExportManifest] Malformed import metadata excludes only the affected path") {
	Ref<TestManifestExportPlatform> platform =
			memnew(TestManifestExportPlatform);
	Ref<EditorExportPreset> preset = platform->create_preset();

	Vector<String> events;
	Ref<RecordingManifestPlugin> plugin =
			memnew(RecordingManifestPlugin("Plugin", &events));
	Vector<Ref<EditorExportPlugin>> plugins;
	plugins.push_back(plugin);

	HashSet<String> paths;
	paths.insert(IMPORTED_PATH);
	paths.insert(MALFORMED_IMPORTED_PATH);

	SaveCapture capture;
	ERR_PRINT_OFF;
	const Error export_error =
			platform->export_candidates(preset, paths, plugins, capture);
	ERR_PRINT_ON;
	CHECK_EQ(export_error, OK);
	CHECK_EQ(plugin->prepared_sources, Vector<String>({ IMPORTED_PATH }));
	CHECK_NE(capture.paths.find(IMPORTED_PATH), -1);
	CHECK_EQ(capture.paths.find(MALFORMED_IMPORTED_PATH), -1);
	CHECK_EQ(capture.paths.find(MALFORMED_IMPORTED_PATH + ".import"), -1);
	CHECK_EQ(platform->get_message_count(), 0);
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

TEST_CASE("[Editor][ExportManifest] Customized scene and resource outputs are rejected before export save") {
	auto run_rejected_customization = [](const String &p_source_path,
											  bool p_scene,
											  const String &p_output_extension) {
		Ref<TestManifestExportPlatform> platform =
				memnew(TestManifestExportPlatform);
		Ref<EditorExportPreset> preset = platform->create_preset();

		Vector<String> events;
		Ref<RecordingManifestPlugin> bravo =
				memnew(RecordingManifestPlugin("Bravo", &events));
		Ref<RecordingManifestPlugin> alpha =
				memnew(RecordingManifestPlugin("Alpha", &events));
		if (p_scene) {
			alpha->customize_scenes = true;
			alpha->customize_scene_path = p_source_path;
		} else {
			alpha->customize_resources = true;
			alpha->customize_resource_path = p_source_path;
		}
		bravo->late_validation_error = ERR_INVALID_DATA;

		Vector<Ref<EditorExportPlugin>> plugins;
		plugins.push_back(bravo);
		plugins.push_back(alpha);
		HashSet<String> paths;
		paths.insert(p_source_path);

		SaveCapture capture;
		CHECK_EQ(platform->export_candidates(
						 preset, paths, plugins, capture),
				ERR_INVALID_DATA);
		REQUIRE_EQ(alpha->validated_late_paths.size(), 1);
		REQUIRE_EQ(bravo->validated_late_paths.size(), 1);
		if (alpha->validated_late_paths.size() != 1 ||
				bravo->validated_late_paths.size() != 1) {
			return;
		}
		const String customized_path =
				alpha->validated_late_paths[0];
		CHECK_EQ(bravo->validated_late_paths[0], customized_path);
		CHECK_NE(customized_path, p_source_path);
		CHECK_EQ(customized_path.get_extension(), p_output_extension);
		CHECK_EQ(capture.paths.find(customized_path), -1);
		if (FileAccess::exists(p_source_path + ".import")) {
			CHECK_EQ(capture.paths.find(p_source_path + ".import"), -1);
		}
		CHECK_NE(events.find(
						 p_scene ? "end_scenes:Alpha" : "end_resources:Alpha"),
				-1);
	};

	SUBCASE("resource") {
		run_rejected_customization(
				CUSTOMIZED_RESOURCE_PATH, false, "res");
	}
	SUBCASE("scene") {
		run_rejected_customization(
				CUSTOMIZED_SCENE_PATH, true, "scn");
	}
	SUBCASE("imported resource") {
		run_rejected_customization(
				CUSTOMIZED_IMPORTED_RESOURCE_PATH, false, "res");
	}
}

TEST_CASE("[Editor][ExportManifest] Cached customized outputs remain rejectable and legacy caches recompute") {
	auto run_cached_rejection = [](bool p_downgrade_to_legacy) {
		Ref<TestManifestExportPlatform> platform =
				memnew(TestManifestExportPlatform);
		Ref<EditorExportPreset> preset = platform->create_preset();
		Vector<String> events;
		Ref<RecordingManifestPlugin> bravo =
				memnew(RecordingManifestPlugin("Bravo", &events));
		Ref<RecordingManifestPlugin> alpha =
				memnew(RecordingManifestPlugin("Alpha", &events));
		alpha->customize_resources = true;
		alpha->customize_resource_path = CUSTOMIZED_RESOURCE_PATH;

		Vector<Ref<EditorExportPlugin>> plugins;
		plugins.push_back(bravo);
		plugins.push_back(alpha);
		HashSet<String> paths;
		paths.insert(CUSTOMIZED_RESOURCE_PATH);

		ScopedManifestExportScratch scratch;
		REQUIRE_EQ(scratch.get_setup_error(), OK);
		if (scratch.get_setup_error() != OK) {
			return;
		}
		ScopedManifestEditorFileSystem editor_file_system;
		REQUIRE_EQ(editor_file_system.get_setup_error(), OK);
		if (editor_file_system.get_setup_error() != OK) {
			return;
		}

		SaveCapture warm_capture;
		CHECK_EQ(platform->export_candidates_in_current_environment(
						 preset, paths, plugins, warm_capture),
				OK);
		REQUIRE_EQ(alpha->validated_late_paths.size(), 1);
		REQUIRE_EQ(bravo->validated_late_paths.size(), 1);
		if (alpha->validated_late_paths.size() != 1 ||
				bravo->validated_late_paths.size() != 1) {
			return;
		}
		const String customized_path =
				alpha->validated_late_paths[0];
		CHECK_NE(warm_capture.paths.find(customized_path), -1);

		if (p_downgrade_to_legacy) {
			const Error downgrade_error =
					downgrade_customization_cache_to_legacy_format();
			REQUIRE_EQ(downgrade_error, OK);
			if (downgrade_error != OK) {
				return;
			}
		}

		alpha->validated_late_paths.clear();
		bravo->validated_late_paths.clear();
		bravo->late_validation_error = ERR_INVALID_DATA;
		events.clear();
		SaveCapture rejected_capture;
		CHECK_EQ(platform->export_candidates_in_current_environment(
						 preset, paths, plugins, rejected_capture),
				ERR_INVALID_DATA);
		CHECK_EQ(alpha->validated_late_paths,
				Vector<String>({ customized_path }));
		CHECK_EQ(bravo->validated_late_paths,
				Vector<String>({ customized_path }));
		CHECK_EQ(rejected_capture.paths.find(customized_path), -1);
		const int customize_event = events.find(
				"customize_resource:Alpha:" +
				CUSTOMIZED_RESOURCE_PATH);
		if (p_downgrade_to_legacy) {
			CHECK_NE(customize_event, -1);
		} else {
			CHECK_EQ(customize_event, -1);
		}
	};

	SUBCASE("persisted customization metadata") {
		run_cached_rejection(false);
	}
	SUBCASE("legacy cache entry") {
		run_cached_rejection(true);
	}
}

TEST_CASE("[Editor][ExportManifest] Legacy pure-conversion caches recompute no-op customization provenance") {
	Ref<TestManifestExportPlatform> platform =
			memnew(TestManifestExportPlatform);
	Ref<EditorExportPreset> preset = platform->create_preset();
	Vector<String> events;
	Ref<RecordingManifestPlugin> bravo =
			memnew(RecordingManifestPlugin("Bravo", &events));
	Ref<RecordingManifestPlugin> alpha =
			memnew(RecordingManifestPlugin("Alpha", &events));
	alpha->customize_resources = true;

	Vector<Ref<EditorExportPlugin>> plugins;
	plugins.push_back(bravo);
	plugins.push_back(alpha);
	HashSet<String> paths;
	paths.insert(CUSTOMIZED_RESOURCE_PATH);

	ScopedManifestExportScratch scratch;
	REQUIRE_EQ(scratch.get_setup_error(), OK);
	if (scratch.get_setup_error() != OK) {
		return;
	}
	ScopedManifestEditorFileSystem editor_file_system;
	REQUIRE_EQ(editor_file_system.get_setup_error(), OK);
	if (editor_file_system.get_setup_error() != OK) {
		return;
	}

	SaveCapture warm_capture;
	CHECK_EQ(platform->export_candidates_in_current_environment(
					 preset, paths, plugins, warm_capture),
			OK);
	CHECK(alpha->validated_late_paths.is_empty());
	CHECK(bravo->validated_late_paths.is_empty());

	const Error downgrade_error =
			downgrade_customization_cache_to_legacy_format();
	REQUIRE_EQ(downgrade_error, OK);
	if (downgrade_error != OK) {
		return;
	}

	events.clear();
	bravo->late_validation_error = ERR_INVALID_DATA;
	SaveCapture recomputed_capture;
	CHECK_EQ(platform->export_candidates_in_current_environment(
					 preset, paths, plugins, recomputed_capture),
			OK);
	CHECK_NE(events.find("customize_resource:Alpha:" +
					 CUSTOMIZED_RESOURCE_PATH),
			-1);
	CHECK(alpha->validated_late_paths.is_empty());
	CHECK(bravo->validated_late_paths.is_empty());

	bool found_binary_resource = false;
	for (const String &path : recomputed_capture.paths) {
		if (path.get_extension() == "res") {
			found_binary_resource = true;
		}
	}
	CHECK(found_binary_resource);

	Vector<String> cache_fields;
	const Error cache_read_error =
			read_customization_cache_fields(
					CUSTOMIZED_RESOURCE_PATH, cache_fields);
	REQUIRE_EQ(cache_read_error, OK);
	if (cache_read_error != OK) {
		return;
	}
	REQUIRE_EQ(cache_fields.size(), 5);
	if (cache_fields.size() != 5) {
		return;
	}
	CHECK_EQ(cache_fields[4], "0");

	events.clear();
	SaveCapture cached_capture;
	CHECK_EQ(platform->export_candidates_in_current_environment(
					 preset, paths, plugins, cached_capture),
			OK);
	CHECK_EQ(events.find("customize_resource:Alpha:" +
					 CUSTOMIZED_RESOURCE_PATH),
			-1);
	CHECK(alpha->validated_late_paths.is_empty());
	CHECK(bravo->validated_late_paths.is_empty());
}

TEST_CASE("[Editor][ExportManifest] Allowed customization and unchanged manifest roots still save") {
	SUBCASE("allowed customized output") {
		Ref<TestManifestExportPlatform> platform =
				memnew(TestManifestExportPlatform);
		Ref<EditorExportPreset> preset = platform->create_preset();
		Vector<String> events;
		Ref<RecordingManifestPlugin> alpha =
				memnew(RecordingManifestPlugin("Alpha", &events));
		alpha->customize_resources = true;
		alpha->customize_resource_path = CUSTOMIZED_RESOURCE_PATH;

		Vector<Ref<EditorExportPlugin>> plugins;
		plugins.push_back(alpha);
		HashSet<String> paths;
		paths.insert(CUSTOMIZED_RESOURCE_PATH);
		SaveCapture capture;
		CHECK_EQ(platform->export_candidates(
						 preset, paths, plugins, capture),
				OK);
		REQUIRE_EQ(alpha->validated_late_paths.size(), 1);
		if (alpha->validated_late_paths.size() != 1) {
			return;
		}
		const String customized_path =
				alpha->validated_late_paths[0];
		CHECK_EQ(customized_path.get_extension(), "res");
		CHECK_NE(capture.paths.find(customized_path), -1);
	}

	SUBCASE("unchanged source root") {
		Ref<TestManifestExportPlatform> platform =
				memnew(TestManifestExportPlatform);
		Ref<EditorExportPreset> preset = platform->create_preset();
		Vector<String> events;
		Ref<RecordingManifestPlugin> bravo =
				memnew(RecordingManifestPlugin("Bravo", &events));
		bravo->customize_scenes = true;
		bravo->late_validation_error = ERR_INVALID_DATA;

		Vector<Ref<EditorExportPlugin>> plugins;
		plugins.push_back(bravo);
		HashSet<String> paths;
		paths.insert(IMPORTED_PATH);
		SaveCapture capture;
		CHECK_EQ(platform->export_candidates(
						 preset, paths, plugins, capture),
				OK);
		CHECK(bravo->validated_late_paths.is_empty());
		CHECK_NE(capture.paths.find(IMPORTED_PATH), -1);
	}

	SUBCASE("representation-only conversion") {
		Ref<TestManifestExportPlatform> platform =
				memnew(TestManifestExportPlatform);
		Ref<EditorExportPreset> preset = platform->create_preset();
		Vector<String> events;
		Ref<RecordingManifestPlugin> bravo =
				memnew(RecordingManifestPlugin("Bravo", &events));
		bravo->late_validation_error = ERR_INVALID_DATA;

		Vector<Ref<EditorExportPlugin>> plugins;
		plugins.push_back(bravo);
		HashSet<String> paths;
		paths.insert(CUSTOMIZED_SCENE_PATH);
		SaveCapture capture;
		CHECK_EQ(platform->export_candidates(
						 preset, paths, plugins, capture),
				OK);
		CHECK(bravo->validated_late_paths.is_empty());
		bool found_binary_scene = false;
		for (const String &path : capture.paths) {
			if (path.get_extension() == "scn") {
				found_binary_scene = true;
			}
		}
		CHECK(found_binary_scene);
	}
}

} // namespace TestEditorExportManifest

#endif // TOOLS_ENABLED
