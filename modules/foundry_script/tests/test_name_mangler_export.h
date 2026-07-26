/**************************************************************************/
/*  test_name_mangler_export.h                                           */
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

#include "modules/foundry_script/editor/fs_editor_export_plugin.h"
#include "modules/foundry_script/editor/fs_name_mangler_export.h"

#include "fs_name_mangler_export_test_utils.h"
#include "fs_temporary_project_tree.h"

#include "editor/export/editor_export_platform.h"
#include "modules/foundry_script/foundry_script.h"
#include "modules/foundry_script/fs_bytecode_export.h"
#include "modules/foundry_script/fs_bytecode_loader.h"
#include "modules/foundry_script/fs_cache.h"
#include "modules/foundry_script/fs_name_mangler_analysis.h"
#include "modules/foundry_script/fs_name_mangler_application.h"
#include "modules/foundry_script/fs_tokenizer_buffer.h"
#include "modules/foundry_script/tests/test_bytecode_serialization.h"

#include "core/config/project_settings.h"
#include "core/io/file_access.h"
#include "core/io/file_access_pack.h"
#include "core/io/resource_loader.h"
#include "core/io/resource_saver.h"
#include "core/os/os.h"
#include "scene/resources/animation.h"
#include "scene/resources/animation_library.h"
#include "scene/resources/packed_scene.h"
#include "tests/test_macros.h"

namespace FSTests {

class NameManglerExportTestPlatform : public EditorExportPlatform {
	FOUNDRY_SOFTCLASS(NameManglerExportTestPlatform, EditorExportPlatform);

public:
	virtual void get_preset_features(const Ref<EditorExportPreset> &p_preset,
			List<String> *r_features) const override {}
	virtual void get_export_options(List<ExportOption> *r_options) const override {}
	virtual String get_name() const override { return "NameManglerExportTest"; }
	virtual String get_os_name() const override { return "NameManglerExportTest"; }
	virtual Ref<Texture2D> get_logo() const override { return Ref<Texture2D>(); }
	virtual bool has_valid_export_configuration(
			const Ref<EditorExportPreset> &p_preset, String &r_error,
			bool &r_missing_templates, bool p_debug = false) const override {
		r_missing_templates = false;
		return true;
	}
	virtual bool has_valid_project_configuration(
			const Ref<EditorExportPreset> &p_preset,
			String &r_error) const override {
		return true;
	}
	virtual List<String> get_binary_extensions(
			const Ref<EditorExportPreset> &p_preset) const override {
		return List<String>();
	}
	virtual Error export_project(const Ref<EditorExportPreset> &p_preset,
			bool p_debug, const String &p_path,
			BitField<EditorExportPlatform::DebugFlags> p_flags = 0) override {
		return OK;
	}
	virtual void get_platform_features(
			List<String> *r_features) const override {}
};

class TestEditorExportFoundryScript : public EditorExportFoundryScript {
	FOUNDRY_SOFTCLASS(
			TestEditorExportFoundryScript, EditorExportFoundryScript);

public:
	void begin_for_test(const Ref<EditorExportPreset> &p_preset,
			bool p_debug = false) {
		set_export_preset(p_preset);
		_export_begin(HashSet<String>(), p_debug, String(), 0);
	}

	Error prepare_for_test(const ExportFileManifest &p_manifest,
			String &r_error) {
		return _prepare_export_file_manifest(p_manifest, r_error);
	}

	Error validate_late_for_test(const String &p_path,
			String &r_error) const {
		return _validate_late_export_file(p_path, r_error);
	}

	void export_file_for_test(const String &p_path) {
		_export_file(p_path, ResourceLoader::get_resource_type(p_path),
				HashSet<String>());
	}

	void clear_output_for_test() {
		_clear();
	}

	int get_output_count_for_test() const {
		return extra_files.size();
	}

	String get_output_path_for_test(int p_index) const {
		ERR_FAIL_INDEX_V(p_index, extra_files.size(), String());
		return extra_files[p_index].path;
	}

	Vector<uint8_t> get_output_bytes_for_test(int p_index) const {
		ERR_FAIL_INDEX_V(
				p_index, extra_files.size(), Vector<uint8_t>());
		return extra_files[p_index].data;
	}

	bool get_output_remap_for_test(int p_index) const {
		ERR_FAIL_INDEX_V(p_index, extra_files.size(), false);
		return extra_files[p_index].remap;
	}

	bool is_skipped_for_test() const {
		return skipped;
	}

	void end_for_test() {
		_export_end();
		_clear();
	}
};

struct NameManglerExportPluginEndGuard {
	Ref<TestEditorExportFoundryScript> plugin;

	explicit NameManglerExportPluginEndGuard(
			const Ref<TestEditorExportFoundryScript> &p_plugin) :
			plugin(p_plugin) {}

	~NameManglerExportPluginEndGuard() {
		if (plugin.is_valid()) {
			plugin->end_for_test();
		}
	}
};

static bool name_mangler_export_last_message_contains(
		const Ref<NameManglerExportTestPlatform> &p_platform,
		const String &p_fragment) {
	if (p_platform.is_null() || p_platform->get_message_count() == 0) {
		return false;
	}
	return p_platform
			->get_message(p_platform->get_message_count() - 1)
			.text.contains(p_fragment);
}

struct NameManglerExportFixture {
	TemporaryProjectTree tree;
	Vector<String> cached_script_paths;

	explicit NameManglerExportFixture(const String &p_name) :
			tree("fs_name_mangler_export_" + p_name + "_" + itos(OS::get_singleton()->get_ticks_usec())) {
		if (!FSLanguage::get_singleton()->has_any_global_constant(SNAME("RefCounted"))) {
			FSLanguage::get_singleton()->init();
		}
	}

	~NameManglerExportFixture() {
		for (const String &path : cached_script_paths) {
			FSCache::remove_script(path);
		}
	}

	String write_source(const String &p_relative_path, const String &p_source) {
		tree.write_file(p_relative_path, p_source);
		const String path = tree.root.path_join(p_relative_path);
		cached_script_paths.push_back(path);
		return path;
	}

	String write_tokens(const String &p_relative_path, const String &p_source) {
		const Vector<uint8_t> buffer = FSTokenizerBuffer::parse_code_string(
				p_source, FSTokenizerBuffer::COMPRESS_NONE);
		REQUIRE_FALSE(buffer.is_empty());
		const String path = tree.root.path_join(p_relative_path);
		write_buffer(path, buffer);
		cached_script_paths.push_back(path);
		return path;
	}

	String write_bytecode(const String &p_relative_path, const String &p_source,
			bool p_annotated_static_unload) {
		const String source_path =
				write_source(p_relative_path.get_basename() + "_source.fs", p_source);
		Error error = OK;
		const Ref<FoundryScript> script = FSCache::get_full_script(
				source_path, error, String(), true);
		REQUIRE_EQ(error, OK);
		REQUIRE(script.is_valid());
		if (error != OK || script.is_null()) {
			return String();
		}
		REQUIRE(script->is_valid());
		if (!script->is_valid()) {
			return String();
		}

		Vector<uint8_t> buffer;
		FSBytecodeExporter exporter;
		REQUIRE_EQ(exporter.serialize(
						   script, buffer, p_annotated_static_unload),
				OK);
		REQUIRE_FALSE(buffer.is_empty());

		const String path = tree.root.path_join(p_relative_path);
		write_buffer(path, buffer);
		cached_script_paths.push_back(path);
		FSCache::remove_script(source_path);
		return path;
	}

	static void write_buffer(const String &p_path,
			const Vector<uint8_t> &p_buffer) {
		Ref<FileAccess> file = FileAccess::open(p_path, FileAccess::WRITE);
		REQUIRE(file.is_valid());
		if (file.is_null()) {
			return;
		}
		file->store_buffer(p_buffer.ptr(), p_buffer.size());
	}

	String write_evidence_scene(const String &p_relative_path,
			const String &p_emitter_path, const String &p_receiver_path,
			const StringName &p_signal = SNAME("scene_signal"),
			const StringName &p_method = SNAME("scene_handler")) {
		Error error = OK;
		const Ref<FoundryScript> emitter_script = FSCache::get_full_script(
				p_emitter_path, error, String(), true);
		REQUIRE_EQ(error, OK);
		REQUIRE(emitter_script.is_valid());
		const Ref<FoundryScript> receiver_script = FSCache::get_full_script(
				p_receiver_path, error, String(), true);
		REQUIRE_EQ(error, OK);
		REQUIRE(receiver_script.is_valid());

		Ref<Animation> animation;
		animation.instantiate();
		const int value_track = animation->add_track(Animation::TYPE_VALUE);
		animation->track_set_path(
				value_track, NodePath("Receiver:animated_value"));
		animation->track_insert_key(value_track, 0.0, 2.0);
		const int method_track = animation->add_track(Animation::TYPE_METHOD);
		animation->track_set_path(method_track, NodePath("Receiver"));
		Dictionary method_key;
		method_key["method"] = SNAME("animation_handler");
		method_key["args"] = Array();
		animation->track_insert_key(method_track, 0.0, method_key);
		Ref<AnimationLibrary> library;
		library.instantiate();
		REQUIRE_EQ(library->add_animation(SNAME("evidence"), animation), OK);

		Ref<PackedScene> scene;
		scene.instantiate();
		const Ref<SceneState> state = scene->get_state();
		const int node_type = state->add_name(SNAME("Node"));
		const int player_type =
				state->add_name(SNAME("AnimationPlayer"));
		const int root_node = state->add_node(
				-1, -1, node_type, state->add_name(SNAME("Root")),
				-1, -1, 70);
		const int emitter_node = state->add_node(
				root_node, root_node, node_type,
				state->add_name(SNAME("Emitter")), -1, -1, 71);
		state->add_node_property(
				emitter_node, state->add_name(SNAME("script")),
				state->add_value(emitter_script));
		const int receiver_node = state->add_node(
				root_node, root_node, node_type,
				state->add_name(SNAME("Receiver")), -1, -1, 72);
		state->add_node_property(
				receiver_node, state->add_name(SNAME("script")),
				state->add_value(receiver_script));
		state->add_node_property(
				receiver_node, state->add_name(SNAME("stored_value")),
				state->add_value(11));
		const int player_node = state->add_node(
				root_node, root_node, player_type,
				state->add_name(SNAME("AnimationPlayer")), -1, -1, 73);
		state->add_node_property(
				player_node, state->add_name(SNAME("root_node")),
				state->add_value(NodePath("..")));
		state->add_node_property(
				player_node, state->add_name(SNAME("libraries/")),
				state->add_value(library));
		state->add_connection(
				emitter_node, receiver_node, state->add_name(p_signal),
				state->add_name(p_method), Object::CONNECT_PERSIST, 0, {});

		const String path = tree.root.path_join(p_relative_path);
		REQUIRE_EQ(ResourceSaver::save(scene, path), OK);
		return path;
	}

	String write_built_in_script_scene(const String &p_relative_path) {
		const String path = tree.root.path_join(p_relative_path);
		Ref<FoundryScript> script;
		script.instantiate();
		script->set_path_cache(path + "::FoundryScript_name_mangler_export");
		script->set_source_code(
				"extends Node\n"
				"func embedded_marker() -> int:\n"
				"\treturn 7\n");
		REQUIRE_EQ(script->reload(), OK);
		REQUIRE(script->is_built_in());

		Node *root_node = memnew(Node);
		root_node->set_name("Root");
		root_node->set_script(script);
		REQUIRE_EQ(root_node->get_script(), script);

		Ref<PackedScene> scene;
		scene.instantiate();
		REQUIRE_EQ(scene->pack(root_node), OK);
		const Error save_error = ResourceSaver::save(scene, path);
		memdelete(root_node);
		REQUIRE_EQ(save_error, OK);
		return path;
	}
};

struct NameManglerExportGlobalGuard {
	StringName name;

	~NameManglerExportGlobalGuard() {
		FSLanguage::get_singleton()->set_compiling_for_export(false);
		if (FSLanguage::get_singleton()->get_named_globals_map().has(name)) {
			FSLanguage::get_singleton()->remove_named_global_constant(name);
		}
		if (ProjectSettings::get_singleton()->has_autoload(name)) {
			ProjectSettings::get_singleton()->remove_autoload(name);
		}
	}
};

static bool name_mangler_export_has_keep(
		const FSNameManglerExport::Result &p_result,
		const StringName &p_name, const String &p_detail_fragment = String()) {
	const String prefix = vformat("Keeping \"%s\": ", p_name);
	for (const String &line : p_result.keep_log) {
		if (line.begins_with(prefix) &&
				(p_detail_fragment.is_empty() ||
						line.contains(p_detail_fragment))) {
			return true;
		}
	}
	return false;
}

static Vector<uint8_t> name_mangler_export_serialize(
		const Ref<FoundryScript> &p_script,
		bool p_annotated_static_unload = false) {
	FSBytecodeExporter exporter;
	Vector<uint8_t> buffer;
	REQUIRE_EQ(
			exporter.serialize(
					p_script, buffer, p_annotated_static_unload),
			OK);
	REQUIRE_FALSE(buffer.is_empty());
	return buffer;
}

static Ref<FoundryScript> name_mangler_export_load_prepared(
		const Vector<uint8_t> &p_buffer, const String &p_path) {
	Ref<FoundryScript> script;
	script.instantiate();
	script->set_path_cache(p_path);
	BytecodeTestResolver resolver;
	FSBytecodeLoader loader;
	loader.set_resolver(&resolver);
	REQUIRE_EQ(loader.load_full(p_buffer, script), OK);
	REQUIRE(script->is_valid());
	return script;
}

static int64_t name_mangler_export_run(
		const Ref<FoundryScript> &p_script, const StringName &p_method) {
	Callable::CallError call_error;
	const Variant instance_variant =
			p_script->_new(nullptr, 0, call_error);
	REQUIRE_EQ(
			call_error.error, Callable::CallError::CALL_OK);
	Object *instance = instance_variant;
	REQUIRE(instance != nullptr);
	const Variant result =
			instance->callp(p_method, nullptr, 0, call_error);
	CHECK_EQ(call_error.error, Callable::CallError::CALL_OK);
	return result;
}

struct NameManglerExportConstantGuard {
	Ref<FoundryScript> script;
	StringName name;

	NameManglerExportConstantGuard(
			const Ref<FoundryScript> &p_script,
			const StringName &p_name, const Variant &p_value) :
			script(p_script), name(p_name) {
		HashMap<StringName, Variant> &constants =
				const_cast<HashMap<StringName, Variant> &>(
						script->get_constants());
		REQUIRE_FALSE(constants.has(name));
		constants.insert(name, p_value);
	}

	~NameManglerExportConstantGuard() {
		if (script.is_valid()) {
			HashMap<StringName, Variant> &constants =
					const_cast<HashMap<StringName, Variant> &>(
							script->get_constants());
			constants.erase(name);
		}
	}
};

TEST_CASE("[FoundryScript][NameManglerExport][Graph] Invalid roots fail transactionally in stable order") {
	FSNameManglerExport::Input input;
	input.manifest_paths.push_back("res://z_missing_name_mangler_export.fs");
	input.manifest_paths.push_back(String());
	input.manifest_paths.push_back("res://a_missing_name_mangler_export.fs");
	input.manifest_paths.push_back("res://z_missing_name_mangler_export.fs");

	const FSNameManglerExport::Result result = FSNameManglerExport::prepare(input);

	CHECK_NE(result.error, OK);
	CHECK(result.scripts.is_empty());
	REQUIRE_EQ(result.diagnostics.size(), 3);
	if (result.diagnostics.size() != 3) {
		return;
	}
	CHECK_EQ(result.diagnostics[0].stage, "graph");
	CHECK(result.diagnostics[0].source.is_empty());
	CHECK_EQ(result.diagnostics[0].message, "Manifest path is empty.");
	CHECK_EQ(result.diagnostics[1].stage, "graph");
	CHECK_EQ(result.diagnostics[1].source, "res://a_missing_name_mangler_export.fs");
	CHECK(result.diagnostics[1].message.contains("could not be loaded"));
	CHECK_EQ(result.diagnostics[2].stage, "graph");
	CHECK_EQ(result.diagnostics[2].source, "res://z_missing_name_mangler_export.fs");
	CHECK(result.diagnostics[2].message.contains("could not be loaded"));
	CHECK_EQ(result.diagnostics[1].format(), "[graph] res://a_missing_name_mangler_export.fs: " + result.diagnostics[1].message);
}

TEST_CASE("[FoundryScript][NameManglerExport][Graph] Empty manifests are valid and sensitive paths are exact") {
	const FSNameManglerExport::Result result = FSNameManglerExport::prepare(FSNameManglerExport::Input());
	CHECK_EQ(result.error, OK);
	CHECK(result.scripts.is_empty());
	CHECK(result.keep_log.is_empty());
	CHECK(result.diagnostics.is_empty());

	CHECK(FSNameManglerExport::is_sensitive_generated_path("res://generated.FSB"));
	CHECK(FSNameManglerExport::is_sensitive_generated_path("res://scene.tscn"));
	CHECK(FSNameManglerExport::is_sensitive_generated_path("res://resource.res"));
	CHECK_FALSE(FSNameManglerExport::is_sensitive_generated_path("res://image.png"));
	CHECK_FALSE(FSNameManglerExport::is_sensitive_generated_path("res://directory.fs/file.txt"));
}

TEST_CASE("[FoundryScript][NameManglerExport][Graph] Source token and bytecode roots publish exact metadata and restore the profile") {
	NameManglerExportFixture fixture("formats");
	const String source_path = fixture.write_source(
			"source.fs",
			"extends RefCounted\n"
			"func source_value() -> int:\n"
			"\treturn 1\n");
	const String token_path = fixture.write_tokens(
			"tokens.fsc",
			"extends RefCounted\n"
			"func token_value() -> int:\n"
			"\treturn 2\n");
	const String bytecode_path = fixture.write_bytecode(
			"compiled.fsb",
			"extends RefCounted\n"
			"func bytecode_value() -> int:\n"
			"\treturn 3\n",
			true);

	const bool previous_call_stack_tracking =
			FSLanguage::get_singleton()->should_track_call_stack();
	FSLanguage::get_singleton()->set_track_call_stack(true);

	FSNameManglerExport::Input input;
	input.manifest_paths.push_back(token_path);
	input.manifest_paths.push_back(bytecode_path);
	input.manifest_paths.push_back(source_path);
	input.release_profile = true;
	const FSNameManglerExport::Result result =
			FSNameManglerExport::prepare(input);

	CHECK_EQ(result.error, OK);
	CHECK(result.diagnostics.is_empty());
	REQUIRE_EQ(result.scripts.size(), 3);

	const RBMap<String, FSNameManglerExport::PreparedScript>::Element
			*source_entry = result.scripts.find(source_path);
	REQUIRE(source_entry != nullptr);
	if (source_entry == nullptr) {
		return;
	}
	CHECK_EQ(source_entry->value().source_path, source_path);
	CHECK_EQ(source_entry->value().output_path,
			source_path.get_basename() + ".fsb");
	CHECK(source_entry->value().remap);
	REQUIRE_FALSE(source_entry->value().bytes.is_empty());
	if (source_entry->value().bytes.is_empty()) {
		return;
	}

	const RBMap<String, FSNameManglerExport::PreparedScript>::Element
			*token_entry = result.scripts.find(token_path);
	REQUIRE(token_entry != nullptr);
	if (token_entry == nullptr) {
		return;
	}
	CHECK_EQ(token_entry->value().source_path, token_path);
	CHECK_EQ(token_entry->value().output_path,
			token_path.get_basename() + ".fsb");
	CHECK(token_entry->value().remap);
	REQUIRE_FALSE(token_entry->value().bytes.is_empty());
	if (token_entry->value().bytes.is_empty()) {
		return;
	}

	const RBMap<String, FSNameManglerExport::PreparedScript>::Element
			*bytecode_entry = result.scripts.find(bytecode_path);
	REQUIRE(bytecode_entry != nullptr);
	if (bytecode_entry == nullptr) {
		return;
	}
	CHECK_EQ(bytecode_entry->value().source_path, bytecode_path);
	CHECK_EQ(bytecode_entry->value().output_path, bytecode_path);
	CHECK_FALSE(bytecode_entry->value().remap);
	REQUIRE_FALSE(bytecode_entry->value().bytes.is_empty());
	if (bytecode_entry->value().bytes.is_empty()) {
		return;
	}

	Vector<String> bytecode_dependencies;
	FSBytecodeLoader bytecode_loader;
	REQUIRE_EQ(bytecode_loader.read_dependencies(
					   bytecode_entry->value().bytes, bytecode_dependencies),
			OK);
	CHECK(bytecode_loader.get_annotated_static_unload());

	CHECK_FALSE(FSLanguage::get_singleton()->is_compiling_for_export());
	CHECK(FSLanguage::get_singleton()->should_track_call_stack());
	FSLanguage::get_singleton()->set_track_call_stack(
			previous_call_stack_tracking);
}

TEST_CASE("[FoundryScript][NameManglerExport][Graph] Missing dependencies identify the dependency path") {
	NameManglerExportFixture fixture("missing_dependency");
	const String missing_path =
			fixture.tree.root.path_join("missing_dependency.fs");
	const String source_path = fixture.write_source(
			"broken.fs",
			vformat(
					"extends RefCounted\n"
					"const Missing = preload(\"%s\")\n",
					missing_path));

	FSNameManglerExport::Input input;
	input.manifest_paths.push_back(source_path);
	ERR_PRINT_OFF;
	const FSNameManglerExport::Result result =
			FSNameManglerExport::prepare(input);
	ERR_PRINT_ON;

	CHECK_NE(result.error, OK);
	CHECK(result.scripts.is_empty());
	REQUIRE_FALSE(result.diagnostics.is_empty());
	if (result.diagnostics.is_empty()) {
		return;
	}
	CHECK_EQ(result.diagnostics[0].stage, "graph");
	CHECK_EQ(result.diagnostics[0].source, source_path);
	CHECK(result.diagnostics[0].message.contains(missing_path));
}

TEST_CASE("[FoundryScript][NameManglerExport][Graph] Canonical path aliases and bytecode remaps fail closed") {
	NameManglerExportFixture fixture("canonical_aliases");
	const String source_path = fixture.write_source(
			"aliases/shared.fs",
			"extends RefCounted\n"
			"func value() -> int:\n"
			"\treturn 1\n");
	const String spelling_alias =
			fixture.tree.root.path_join("aliases/./shared.fs");

	FSNameManglerExport::Input spelling_input;
	spelling_input.manifest_paths.push_back(source_path);
	spelling_input.manifest_paths.push_back(spelling_alias);
	const FSNameManglerExport::Result spelling_result =
			FSNameManglerExport::prepare(spelling_input);
	CHECK_NE(spelling_result.error, OK);
	CHECK(spelling_result.scripts.is_empty());
	REQUIRE_EQ(spelling_result.diagnostics.size(), 1);
	if (spelling_result.diagnostics.size() != 1) {
		return;
	}
	CHECK(spelling_result.diagnostics[0].message.contains(
			"Duplicate canonical script identity"));
	CHECK(spelling_result.diagnostics[0].message.contains(
			source_path.simplify_path()));

	const String remapped_source_path = fixture.write_source(
			"remapped.fs",
			"extends RefCounted\n"
			"func source_value() -> int:\n"
			"\treturn 2\n");
	const String remapped_binary_path = fixture.write_bytecode(
			"remapped.fsb",
			"extends RefCounted\n"
			"func binary_value() -> int:\n"
			"\treturn 3\n",
			false);
	fixture.tree.write_file(
			"remapped.fs.remap",
			vformat("[remap]\n\npath=\"%s\"\n", remapped_binary_path));
	FSCache::remove_script(remapped_source_path);

	FSNameManglerExport::Input remap_input;
	remap_input.manifest_paths.push_back(remapped_source_path);
	remap_input.manifest_paths.push_back(remapped_binary_path);
	const FSNameManglerExport::Result remap_result =
			FSNameManglerExport::prepare(remap_input);
	CHECK_NE(remap_result.error, OK);
	CHECK(remap_result.scripts.is_empty());
	REQUIRE_EQ(remap_result.diagnostics.size(), 1);
	if (remap_result.diagnostics.size() != 1) {
		return;
	}
	CHECK(remap_result.diagnostics[0].message.contains(
			"Duplicate canonical script identity"));
}

TEST_CASE("[FoundryScript][NameManglerExport][Graph] Dependencies must be represented in the sealed manifest") {
	NameManglerExportFixture fixture("closed_dependency");
	const String dependency_path = fixture.write_source(
			"dependency.fs",
			"extends RefCounted\n"
			"const VALUE := 7\n");
	const String source_path = fixture.write_source(
			"root.fs",
			vformat(
					"extends RefCounted\n"
					"const Dependency = preload(\"%s\")\n"
					"func value() -> int:\n"
					"\treturn Dependency.VALUE\n",
					dependency_path));

	FSNameManglerExport::Input incomplete_input;
	incomplete_input.manifest_paths.push_back(source_path);
	const FSNameManglerExport::Result incomplete_result =
			FSNameManglerExport::prepare(incomplete_input);
	CHECK_NE(incomplete_result.error, OK);
	CHECK(incomplete_result.scripts.is_empty());
	REQUIRE_EQ(incomplete_result.diagnostics.size(), 1);
	if (incomplete_result.diagnostics.size() != 1) {
		return;
	}
	CHECK(incomplete_result.diagnostics[0].message.contains(dependency_path));
	CHECK(incomplete_result.diagnostics[0].message.contains(
			"outside the sealed export manifest"));

	FSNameManglerExport::Input complete_input;
	complete_input.manifest_paths.push_back(source_path);
	complete_input.manifest_paths.push_back(dependency_path);
	const FSNameManglerExport::Result complete_result =
			FSNameManglerExport::prepare(complete_input);
	CHECK_EQ(complete_result.error, OK);
	CHECK(complete_result.diagnostics.is_empty());
	REQUIRE_EQ(complete_result.scripts.size(), 2);
	CHECK(complete_result.scripts.has(source_path));
	CHECK(complete_result.scripts.has(dependency_path));
}

TEST_CASE("[FoundryScript][NameManglerExport][Graph] Imported resources load through their stable source paths") {
	NameManglerExportFixture fixture("imported_resource");
	const String source_path =
			fixture.tree.root.path_join("catalog.fixture");
	const String imported_path =
			fixture.tree.root.path_join("catalog.tres");
	fixture.tree.write_file("catalog.fixture", "fixture source\n");
	fixture.tree.write_file(
			"catalog.tres",
			"[gd_resource type=\"Resource\" format=3]\n"
			"\n"
			"[resource]\n"
			"resource_name = \"Imported catalog\"\n");
	fixture.tree.write_file(
			"catalog.fixture.import",
			vformat(
					"[remap]\n"
					"\n"
					"importer=\"name_mangler_export_fixture\"\n"
					"type=\"Resource\"\n"
					"path=\"%s\"\n",
					imported_path));

	REQUIRE_EQ(ResourceLoader::get_resource_type(source_path), "Resource");
	FSNameManglerExport::Input input;
	input.manifest_paths.push_back(source_path);
	const FSNameManglerExport::Result result =
			FSNameManglerExport::prepare(input);
	CHECK_EQ(result.error, OK);
	CHECK(result.scripts.is_empty());
	CHECK(result.diagnostics.is_empty());
}

TEST_CASE("[FoundryScript][NameManglerExport][Graph] Unsupported editor globals fail with stable names") {
	NameManglerExportFixture fixture("unsupported_global");
	const StringName global_name = "NameManglerExportEditorOnlyGlobal";
	NameManglerExportGlobalGuard global_guard{ global_name };
	const String scene_path =
			fixture.tree.root.path_join("editor_only_global.tscn");
	fixture.tree.write_file(
			"editor_only_global.tscn",
			"[gd_scene format=3]\n"
			"\n"
			"[node name=\"Root\" type=\"Node\"]\n");
	ProjectSettings::AutoloadInfo editor_only_global;
	editor_only_global.name = global_name;
	editor_only_global.path = scene_path;
	editor_only_global.is_singleton = true;
	ProjectSettings::get_singleton()->add_autoload(editor_only_global);
	FSLanguage::get_singleton()->add_named_global_constant(
			global_name, Variant());

	// Existing .fsb inputs cannot be recompiled under the export flag. Build one from ordinary
	// editor bytecode so its runtime-only named-global fixup reaches the graph validator.
	const String bytecode_path = fixture.write_bytecode(
			"unsupported.fsb",
			"extends RefCounted\n"
			"func value():\n"
			"\treturn NameManglerExportEditorOnlyGlobal\n",
			false);

	FSNameManglerExport::Input input;
	input.manifest_paths.push_back(scene_path);
	input.manifest_paths.push_back(bytecode_path);
	const FSNameManglerExport::Result result =
			FSNameManglerExport::prepare(input);
	CHECK_NE(result.error, OK);
	CHECK(result.scripts.is_empty());
	REQUIRE_EQ(result.diagnostics.size(), 1);
	if (result.diagnostics.size() != 1) {
		return;
	}
	CHECK_EQ(result.diagnostics[0].stage, "graph");
	CHECK_EQ(result.diagnostics[0].source, bytecode_path);
	CHECK(result.diagnostics[0].message.contains(String(global_name)));
	CHECK_FALSE(FSLanguage::get_singleton()->is_compiling_for_export());
}

TEST_CASE("[FoundryScript][NameManglerExport][Graph] Autoload export compilation restores ordinary editor bytecode") {
	NameManglerExportFixture fixture("autoload_restore");
	const StringName autoload_name = "NameManglerExportAutoload";
	NameManglerExportGlobalGuard global_guard{ autoload_name };
	const String scene_path = fixture.tree.root.path_join("autoload.tscn");
	fixture.tree.write_file(
			"autoload.tscn",
			"[gd_scene format=3]\n"
			"\n"
			"[node name=\"Root\" type=\"Node\"]\n");
	ProjectSettings::AutoloadInfo autoload;
	autoload.name = autoload_name;
	autoload.path = scene_path;
	autoload.is_singleton = true;
	ProjectSettings::get_singleton()->add_autoload(autoload);
	FSLanguage::get_singleton()->add_named_global_constant(
			autoload_name, Variant());
	const String source_path = fixture.write_source(
			"autoload_user.fs",
			"extends RefCounted\n"
			"func value():\n"
			"\treturn NameManglerExportAutoload\n");
	const String token_path = fixture.write_tokens(
			"autoload_user_tokens.fsc",
			"extends RefCounted\n"
			"func value():\n"
			"\treturn NameManglerExportAutoload\n");

	FSNameManglerExport::Input input;
	input.manifest_paths.push_back(scene_path);
	input.manifest_paths.push_back(source_path);
	input.manifest_paths.push_back(token_path);
	const FSNameManglerExport::Result result =
			FSNameManglerExport::prepare(input);
	CHECK_EQ(result.error, OK);
	CHECK(result.diagnostics.is_empty());
	REQUIRE(result.scripts.has(source_path));
	if (!result.scripts.has(source_path)) {
		return;
	}
	REQUIRE_FALSE(result.scripts[source_path].bytes.is_empty());
	if (result.scripts[source_path].bytes.is_empty()) {
		return;
	}
	REQUIRE(result.scripts.has(token_path));
	if (!result.scripts.has(token_path)) {
		return;
	}
	REQUIRE_FALSE(result.scripts[token_path].bytes.is_empty());
	if (result.scripts[token_path].bytes.is_empty()) {
		return;
	}

	const Ref<FoundryScript> restored_script =
			FSCache::get_cached_script(source_path);
	REQUIRE(restored_script.is_valid());
	if (restored_script.is_null()) {
		return;
	}
	const Vector<StringName> editor_named_globals =
			FSBytecodeExporter::collect_unsupported_named_globals(
					restored_script);
	REQUIRE_EQ(editor_named_globals.size(), 1);
	if (editor_named_globals.size() != 1) {
		return;
	}
	CHECK_EQ(editor_named_globals[0], autoload_name);
	const Ref<FoundryScript> restored_token_script =
			FSCache::get_cached_script(token_path);
	REQUIRE(restored_token_script.is_valid());
	if (restored_token_script.is_null()) {
		return;
	}
	const Vector<StringName> token_editor_named_globals =
			FSBytecodeExporter::collect_unsupported_named_globals(
					restored_token_script);
	REQUIRE_EQ(token_editor_named_globals.size(), 1);
	if (token_editor_named_globals.size() != 1) {
		return;
	}
	CHECK_EQ(token_editor_named_globals[0], autoload_name);
	CHECK_FALSE(FSLanguage::get_singleton()->is_compiling_for_export());
}

TEST_CASE("[FoundryScript][NameManglerExport][Graph] Reversed manifests produce identical ordered buffers") {
	NameManglerExportFixture fixture("reversed");
	const String alpha_path = fixture.write_source(
			"alpha.fs",
			"extends RefCounted\n"
			"func alpha() -> String:\n"
			"\treturn \"alpha\"\n");
	const String zulu_path = fixture.write_tokens(
			"zulu.fsc",
			"extends RefCounted\n"
			"func zulu() -> String:\n"
			"\treturn \"zulu\"\n");

	FSNameManglerExport::Input forward_input;
	forward_input.manifest_paths.push_back(alpha_path);
	forward_input.manifest_paths.push_back(zulu_path);
	const FSNameManglerExport::Result forward =
			FSNameManglerExport::prepare(forward_input);

	FSNameManglerExport::Input reverse_input;
	reverse_input.manifest_paths.push_back(zulu_path);
	reverse_input.manifest_paths.push_back(alpha_path);
	const FSNameManglerExport::Result reverse =
			FSNameManglerExport::prepare(reverse_input);

	CHECK_EQ(forward.error, OK);
	CHECK_EQ(reverse.error, OK);
	CHECK(forward.diagnostics.is_empty());
	CHECK(reverse.diagnostics.is_empty());
	REQUIRE_EQ(forward.scripts.size(), 2);
	REQUIRE_EQ(reverse.scripts.size(), 2);
	for (const KeyValue<String, FSNameManglerExport::PreparedScript> &entry :
			forward.scripts) {
		REQUIRE(reverse.scripts.has(entry.key));
		if (!reverse.scripts.has(entry.key)) {
			return;
		}
		CHECK_EQ(reverse.scripts[entry.key].source_path,
				entry.value.source_path);
		CHECK_EQ(reverse.scripts[entry.key].output_path,
				entry.value.output_path);
		CHECK_EQ(reverse.scripts[entry.key].remap, entry.value.remap);
		REQUIRE_FALSE(entry.value.bytes.is_empty());
		if (entry.value.bytes.is_empty()) {
			return;
		}
		CHECK_EQ(reverse.scripts[entry.key].bytes, entry.value.bytes);
	}
}

TEST_CASE("[FoundryScript][NameManglerExport][Evidence] Scene bindings and configured rules compose before analysis") {
	NameManglerExportFixture fixture("evidence_composition");
	const String emitter_path = fixture.write_source(
			"emitter.fs",
			"extends Node\n"
			"signal scene_signal\n"
			"func private_emitter_helper() -> void:\n"
			"\tpass\n");
	const String receiver_path = fixture.write_source(
			"receiver.fs",
			"extends Node\n"
			"@export var stored_value: int\n"
			"var animated_value: float\n"
			"func scene_handler() -> void:\n"
			"\tpass\n"
			"func animation_handler() -> void:\n"
			"\tpass\n"
			"func rule_kept() -> void:\n"
			"\tpass\n"
			"func private_helper() -> void:\n"
			"\tpass\n");
	const String scene_path = fixture.write_evidence_scene(
			"main.tscn", emitter_path, receiver_path);
	fixture.tree.write_file(
			"name-mangler.pro",
			"-keepclassmembers class ** {\n"
			"\trule_kept;\n"
			"}\n");
	const String rules_path =
			fixture.tree.root.path_join("name-mangler.pro");

	FSNameManglerExport::Input input;
	input.manifest_paths.push_back(receiver_path);
	input.manifest_paths.push_back(scene_path);
	input.manifest_paths.push_back(emitter_path);
	input.keep_rules_path = rules_path;
	const FSNameManglerExport::Result result =
			FSNameManglerExport::prepare(input);

	INFO("Diagnostics:");
	for (const FSNameManglerExport::Diagnostic &diagnostic :
			result.diagnostics) {
		INFO(diagnostic.format());
	}
	CHECK_EQ(result.error, OK);
	CHECK(result.diagnostics.is_empty());
	REQUIRE_EQ(result.scripts.size(), 2);
	CHECK(result.scripts.has(emitter_path));
	CHECK(result.scripts.has(receiver_path));
	CHECK(name_mangler_export_has_keep(
			result, SNAME("scene_signal"),
			"scene/resource reference (connection signal in " +
					scene_path));
	CHECK(name_mangler_export_has_keep(
			result, SNAME("scene_handler"),
			"scene/resource reference (connection method in " +
					scene_path));
	CHECK(name_mangler_export_has_keep(
			result, SNAME("stored_value"),
			"scene/resource reference (serialized property in " +
					scene_path));
	CHECK(name_mangler_export_has_keep(
			result, SNAME("animated_value"),
			"scene/resource reference (animation property in " +
					scene_path));
	CHECK(name_mangler_export_has_keep(
			result, SNAME("animation_handler"),
			"scene/resource reference (animation method in " +
					scene_path));
	CHECK(name_mangler_export_has_keep(
			result, SNAME("rule_kept"), "explicit keep rule"));
	CHECK_FALSE(name_mangler_export_has_keep(
			result, SNAME("private_helper")));
	CHECK_FALSE(name_mangler_export_has_keep(
			result, SNAME("private_emitter_helper")));
}

TEST_CASE("[FoundryScript][NameManglerExport][Evidence] Configured rules fail closed while an omitted path is ignored") {
	NameManglerExportFixture fixture("rules_fail_closed");
	const String source_path = fixture.write_source(
			"rules.fs",
			"extends RefCounted\n"
			"func private_helper() -> void:\n"
			"\tpass\n");
	fixture.tree.write_file(
			"malformed.pro",
			"-keepclassmembers class ** {\n"
			"\tmissing_semicolon\n"
			"}\n");
	const String malformed_path =
			fixture.tree.root.path_join("malformed.pro");
	const String missing_path =
			fixture.tree.root.path_join("missing.pro");

	FSNameManglerExport::Input malformed_input;
	malformed_input.manifest_paths.push_back(source_path);
	malformed_input.keep_rules_path = malformed_path;
	const FSNameManglerExport::Result malformed =
			FSNameManglerExport::prepare(malformed_input);
	CHECK_EQ(malformed.error, ERR_PARSE_ERROR);
	CHECK(malformed.scripts.is_empty());
	REQUIRE_EQ(malformed.diagnostics.size(), 1);
	if (malformed.diagnostics.size() == 1) {
		CHECK_EQ(malformed.diagnostics[0].severity,
				FSNameManglerExport::DIAGNOSTIC_ERROR);
		CHECK_EQ(malformed.diagnostics[0].stage, "rules");
		CHECK_EQ(malformed.diagnostics[0].source, malformed_path);
		CHECK(malformed.diagnostics[0].message.contains(
				"must end with `;`"));
	}

	FSNameManglerExport::Input missing_input;
	missing_input.manifest_paths.push_back(source_path);
	missing_input.keep_rules_path = missing_path;
	const FSNameManglerExport::Result missing =
			FSNameManglerExport::prepare(missing_input);
	CHECK_EQ(missing.error, ERR_FILE_NOT_FOUND);
	CHECK(missing.scripts.is_empty());
	REQUIRE_EQ(missing.diagnostics.size(), 1);
	if (missing.diagnostics.size() == 1) {
		CHECK_EQ(missing.diagnostics[0].severity,
				FSNameManglerExport::DIAGNOSTIC_ERROR);
		CHECK_EQ(missing.diagnostics[0].stage, "rules");
		CHECK_EQ(missing.diagnostics[0].source, missing_path);
		CHECK(missing.diagnostics[0].message.contains(
				"Could not read keep-rules file"));
	}

	FSNameManglerExport::Input omitted_input;
	omitted_input.manifest_paths.push_back(source_path);
	const FSNameManglerExport::Result omitted =
			FSNameManglerExport::prepare(omitted_input);
	CHECK_EQ(omitted.error, OK);
	CHECK(omitted.diagnostics.is_empty());
	REQUIRE_EQ(omitted.scripts.size(), 1);
	CHECK(omitted.scripts.has(source_path));
	CHECK_FALSE(name_mangler_export_has_keep(
			omitted, SNAME("private_helper")));
}

TEST_CASE("[FoundryScript][NameManglerExport][Evidence] Incomplete binding safety rejects all prepared scripts") {
	NameManglerExportFixture fixture("incomplete_binding");
	const String emitter_path = fixture.write_source(
			"emitter.fs",
			"extends Node\n"
			"signal scene_signal\n");
	const String receiver_path = fixture.write_source(
			"receiver.fs",
			"extends Node\n"
			"func private_helper() -> void:\n"
			"\tpass\n"
			"func animation_handler() -> void:\n"
			"\tpass\n"
			"var animated_value: float\n"
			"@export var stored_value: int\n");
	const String scene_path = fixture.write_evidence_scene(
			"stale.tscn", emitter_path, receiver_path,
			SNAME("missing_signal"), SNAME("missing_handler"));

	FSNameManglerExport::Input input;
	input.manifest_paths.push_back(scene_path);
	input.manifest_paths.push_back(receiver_path);
	input.manifest_paths.push_back(emitter_path);
	const FSNameManglerExport::Result result =
			FSNameManglerExport::prepare(input);

	CHECK_NE(result.error, OK);
	CHECK(result.scripts.is_empty());
	CHECK(result.keep_log.is_empty());
	REQUIRE_EQ(result.diagnostics.size(), 2);
	if (result.diagnostics.size() == 2) {
		CHECK_EQ(result.diagnostics[0].stage, "binding");
		CHECK_EQ(result.diagnostics[0].source, scene_path);
		CHECK_EQ(result.diagnostics[1].stage, "binding");
		CHECK_EQ(result.diagnostics[1].source, scene_path);
		CHECK(result.diagnostics[0].message <=
				result.diagnostics[1].message);
		const bool mentions_missing_binding =
				result.diagnostics[0].message.contains("missing_") ||
				result.diagnostics[1].message.contains("missing_");
		CHECK(mentions_missing_binding);
	}
}

TEST_CASE("[FoundryScript][NameManglerExport][Serialize] Prepared bytes are mangled deterministic and executable while live scripts roll back") {
	NameManglerExportFixture fixture("serialize_deterministic");
	const String alpha_path = fixture.write_source(
			"alpha.fs",
			"extends RefCounted\n"
			"var facade_private_value: float = 1.5\n"
			"func facade_private_compute() -> int:\n"
			"\tvar vector := Vector2(facade_private_value, 2.5)\n"
			"\tvector.x = 3.0\n"
			"\tvar values := {}\n"
			"\tvalues[\"value\"] = vector.x + vector.y\n"
			"\tvar left: Variant = 1\n"
			"\tvar right: Variant = 2\n"
			"\treturn int(values[\"value\"] + (left + right))\n"
			"@keep_name\n"
			"func run() -> int:\n"
			"\treturn facade_private_compute()\n");
	const String zulu_path = fixture.write_source(
			"zulu.fs",
			"extends RefCounted\n"
			"var second_private_value: int = 40\n"
			"func second_private_compute() -> int:\n"
			"\treturn second_private_value + 2\n"
			"@keep_name\n"
			"func run() -> int:\n"
			"\treturn second_private_compute()\n");
	Error error = OK;
	const Ref<FoundryScript> alpha_script = FSCache::get_full_script(
			alpha_path, error, String(), true);
	REQUIRE_EQ(error, OK);
	REQUIRE(alpha_script.is_valid());
	const Ref<FoundryScript> zulu_script = FSCache::get_full_script(
			zulu_path, error, String(), true);
	REQUIRE_EQ(error, OK);
	REQUIRE(zulu_script.is_valid());
	const Vector<uint8_t> alpha_baseline =
			name_mangler_export_serialize(alpha_script);
	const Vector<uint8_t> zulu_baseline =
			name_mangler_export_serialize(zulu_script);
	REQUIRE(bytecode_buffer_contains(
			alpha_baseline, "facade_private_compute"));
	REQUIRE(bytecode_buffer_contains(
			alpha_baseline, "facade_private_value"));
	REQUIRE(bytecode_buffer_contains(
			zulu_baseline, "second_private_compute"));

	FSNameManglerExport::Input forward_input;
	forward_input.manifest_paths.push_back(alpha_path);
	forward_input.manifest_paths.push_back(zulu_path);
	const FSNameManglerExport::Result forward =
			FSNameManglerExport::prepare(forward_input);
	const FSNameManglerExport::Result repeated =
			FSNameManglerExport::prepare(forward_input);

	FSNameManglerExport::Input reverse_input;
	reverse_input.manifest_paths.push_back(zulu_path);
	reverse_input.manifest_paths.push_back(alpha_path);
	const FSNameManglerExport::Result reverse =
			FSNameManglerExport::prepare(reverse_input);

	REQUIRE_EQ(forward.error, OK);
	REQUIRE_EQ(repeated.error, OK);
	REQUIRE_EQ(reverse.error, OK);
	CHECK(forward.diagnostics.is_empty());
	CHECK(repeated.diagnostics.is_empty());
	CHECK(reverse.diagnostics.is_empty());
	CHECK_EQ(forward.keep_log, repeated.keep_log);
	CHECK_EQ(forward.keep_log, reverse.keep_log);
	REQUIRE_EQ(forward.scripts.size(), 2);
	REQUIRE_EQ(repeated.scripts.size(), 2);
	REQUIRE_EQ(reverse.scripts.size(), 2);
	for (const KeyValue<String, FSNameManglerExport::PreparedScript> &entry :
			forward.scripts) {
		REQUIRE(repeated.scripts.has(entry.key));
		REQUIRE(reverse.scripts.has(entry.key));
		CHECK_EQ(repeated.scripts[entry.key].source_path,
				entry.value.source_path);
		CHECK_EQ(reverse.scripts[entry.key].output_path,
				entry.value.output_path);
		CHECK_EQ(reverse.scripts[entry.key].remap, entry.value.remap);
		CHECK_EQ(repeated.scripts[entry.key].bytes, entry.value.bytes);
		CHECK_EQ(reverse.scripts[entry.key].bytes, entry.value.bytes);
	}

	const Vector<uint8_t> &prepared_alpha =
			forward.scripts[alpha_path].bytes;
	CHECK(bytecode_buffer_contains(prepared_alpha, "run"));
	CHECK_FALSE(bytecode_buffer_contains(
			prepared_alpha, "facade_private_compute"));
	CHECK_FALSE(bytecode_buffer_contains(
			prepared_alpha, "facade_private_value"));
	const Ref<FoundryScript> loaded_alpha =
			name_mangler_export_load_prepared(
					prepared_alpha, alpha_path);
	CHECK_EQ(name_mangler_export_run(
					 loaded_alpha, SNAME("run")),
			8);
	CHECK_EQ(name_mangler_export_serialize(loaded_alpha),
			prepared_alpha);

	const Ref<FoundryScript> restored_alpha =
			FSCache::get_cached_script(alpha_path);
	const Ref<FoundryScript> restored_zulu =
			FSCache::get_cached_script(zulu_path);
	REQUIRE(restored_alpha.is_valid());
	REQUIRE(restored_zulu.is_valid());
	CHECK(restored_alpha->is_valid());
	CHECK(restored_zulu->is_valid());
	CHECK(restored_alpha->get_member_functions().has(
			SNAME("facade_private_compute")));
	CHECK(restored_zulu->get_member_functions().has(
			SNAME("second_private_compute")));
	CHECK_EQ(name_mangler_export_serialize(restored_alpha),
			alpha_baseline);
	CHECK_EQ(name_mangler_export_serialize(restored_zulu),
			zulu_baseline);
}

TEST_CASE("[FoundryScript][NameManglerExport][Serialize] Loaded bytecode rebakes operator caches and preserves static unload") {
	NameManglerExportFixture fixture("serialize_loaded_bytecode");
	const String bytecode_path = fixture.write_bytecode(
			"loaded.fsb",
			"extends RefCounted\n"
			"static var loaded_private_static: int = 40\n"
			"func loaded_private_compute() -> int:\n"
			"\tvar left: Variant = 1\n"
			"\tvar right: Variant = 2\n"
			"\treturn loaded_private_static + int(left + right)\n"
			"@keep_name\n"
			"func run() -> int:\n"
			"\treturn loaded_private_compute()\n",
			true);
	Error error = OK;
	const Ref<FoundryScript> loaded_script =
			FSCache::get_full_script(
					bytecode_path, error, String(), true);
	REQUIRE_EQ(error, OK);
	REQUIRE(loaded_script.is_valid());
	CHECK_EQ(name_mangler_export_run(
					 loaded_script, SNAME("run")),
			43);
	const Vector<uint8_t> loaded_baseline =
			name_mangler_export_serialize(loaded_script, true);

	FSNameManglerExport::Input input;
	input.manifest_paths.push_back(bytecode_path);
	const FSNameManglerExport::Result result =
			FSNameManglerExport::prepare(input);
	REQUIRE_EQ(result.error, OK);
	CHECK(result.diagnostics.is_empty());
	REQUIRE_EQ(result.scripts.size(), 1);
	REQUIRE(result.scripts.has(bytecode_path));
	const Vector<uint8_t> &prepared =
			result.scripts[bytecode_path].bytes;
	CHECK_FALSE(bytecode_buffer_contains(
			prepared, "loaded_private_compute"));
	CHECK_FALSE(bytecode_buffer_contains(
			prepared, "loaded_private_static"));
	Vector<String> dependencies;
	FSBytecodeLoader flag_loader;
	REQUIRE_EQ(flag_loader.read_dependencies(
					   prepared, dependencies),
			OK);
	CHECK(flag_loader.get_has_static_data());
	CHECK(flag_loader.get_annotated_static_unload());

	const Ref<FoundryScript> prepared_script =
			name_mangler_export_load_prepared(
					prepared, bytecode_path);
	CHECK_EQ(name_mangler_export_run(
					 prepared_script, SNAME("run")),
			43);
	CHECK_EQ(name_mangler_export_serialize(
					 prepared_script, true),
			prepared);
	const Ref<FoundryScript> restored =
			FSCache::get_cached_script(bytecode_path);
	REQUIRE(restored.is_valid());
	CHECK(restored->get_member_functions().has(
			SNAME("loaded_private_compute")));
	CHECK_EQ(name_mangler_export_run(restored, SNAME("run")), 43);
	CHECK_EQ(name_mangler_export_serialize(restored, true),
			loaded_baseline);
}

TEST_CASE("[FoundryScript][NameManglerExport][Serialize] Application failure returns no bytes and leaves the active owner intact") {
	NameManglerExportFixture fixture("serialize_application_failure");
	const String owner_path = fixture.write_source(
			"owner.fs",
			"extends RefCounted\n"
			"var owner_private_value: int = 1\n"
			"func owner_private_method() -> int:\n"
			"\treturn owner_private_value\n");
	const String target_path = fixture.write_source(
			"target.fs",
			"extends RefCounted\n"
			"func target_private_method() -> int:\n"
			"\treturn 2\n");
	Error error = OK;
	const Ref<FoundryScript> owner_script = FSCache::get_full_script(
			owner_path, error, String(), true);
	REQUIRE_EQ(error, OK);
	REQUIRE(owner_script.is_valid());
	const Ref<FoundryScript> target_script = FSCache::get_full_script(
			target_path, error, String(), true);
	REQUIRE_EQ(error, OK);
	REQUIRE(target_script.is_valid());
	const Vector<uint8_t> owner_baseline =
			name_mangler_export_serialize(owner_script);
	const Vector<uint8_t> target_baseline =
			name_mangler_export_serialize(target_script);
	FSNameManglerAnalysis::Input owner_input;
	owner_input.scripts.push_back(owner_script);
	const FSNameManglerAnalysis::Result owner_analysis =
			FSNameManglerAnalysis::analyze(owner_input);
	REQUIRE_EQ(owner_analysis.error, OK);
	REQUIRE_FALSE(owner_analysis.rename_map.is_empty());

	FSNameManglerApplication::Transaction owner_transaction;
	Vector<FSNameManglerApplication::Diagnostic> owner_diagnostics;
	REQUIRE_EQ(owner_transaction.begin(
					   owner_input.scripts,
					   owner_analysis.rename_map,
					   owner_diagnostics),
			OK);
	REQUIRE(owner_transaction.is_active());

	FSNameManglerExport::Input input;
	input.manifest_paths.push_back(target_path);
	const FSNameManglerExport::Result result =
			FSNameManglerExport::prepare(input);
	CHECK_EQ(result.error, ERR_ALREADY_IN_USE);
	CHECK(result.scripts.is_empty());
	CHECK(result.keep_log.is_empty());
	REQUIRE_FALSE(result.diagnostics.is_empty());
	if (!result.diagnostics.is_empty()) {
		CHECK_EQ(result.diagnostics[0].stage, "application");
	}
	CHECK(owner_transaction.is_active());
	const Ref<FoundryScript> restored_target =
			FSCache::get_cached_script(target_path);
	REQUIRE(restored_target.is_valid());
	CHECK(restored_target->get_member_functions().has(
			SNAME("target_private_method")));
	CHECK_EQ(name_mangler_export_serialize(restored_target),
			target_baseline);

	owner_transaction.rollback();
	CHECK_FALSE(owner_transaction.is_active());
	CHECK(owner_script->get_member_functions().has(
			SNAME("owner_private_method")));
	CHECK_EQ(name_mangler_export_serialize(owner_script),
			owner_baseline);
}

TEST_CASE("[FoundryScript][NameManglerExport][Serialize] Later serialization failure discards earlier buffers and rolls back every root") {
	NameManglerExportFixture fixture("serialize_late_failure");
	const String alpha_path = fixture.write_source(
			"alpha.fs",
			"extends RefCounted\n"
			"func alpha_private_marker() -> int:\n"
			"\treturn 1\n");
	const String zulu_path = fixture.write_bytecode(
			"zulu.fsb",
			"extends RefCounted\n"
			"func zulu_private_marker() -> int:\n"
			"\treturn 2\n",
			false);
	Error error = OK;
	const Ref<FoundryScript> alpha_script = FSCache::get_full_script(
			alpha_path, error, String(), true);
	REQUIRE_EQ(error, OK);
	REQUIRE(alpha_script.is_valid());
	const Ref<FoundryScript> zulu_script = FSCache::get_full_script(
			zulu_path, error, String(), true);
	REQUIRE_EQ(error, OK);
	REQUIRE(zulu_script.is_valid());
	const Vector<uint8_t> alpha_baseline =
			name_mangler_export_serialize(alpha_script);
	const Vector<uint8_t> zulu_baseline =
			name_mangler_export_serialize(zulu_script);

	Ref<RefCounted> callable_target;
	callable_target.instantiate();
	const StringName live_constant =
			SNAME("zulu_live_callable");
	FSNameManglerExport::Result result;
	{
		NameManglerExportConstantGuard constant_guard(
				zulu_script, live_constant,
				Callable(callable_target.ptr(),
						SNAME("get_instance_id")));
		FSNameManglerExport::Input input;
		input.manifest_paths.push_back(zulu_path);
		input.manifest_paths.push_back(alpha_path);
		ERR_PRINT_OFF;
		result = FSNameManglerExport::prepare(input);
		ERR_PRINT_ON;
	}

	CHECK_EQ(result.error, ERR_INVALID_PARAMETER);
	CHECK(result.scripts.is_empty());
	CHECK(result.keep_log.is_empty());
	REQUIRE_FALSE(result.diagnostics.is_empty());
	if (!result.diagnostics.is_empty()) {
		CHECK_EQ(result.diagnostics[0].stage, "serialization");
		CHECK_EQ(result.diagnostics[0].source, zulu_path);
	}
	const Ref<FoundryScript> restored_alpha =
			FSCache::get_cached_script(alpha_path);
	const Ref<FoundryScript> restored_zulu =
			FSCache::get_cached_script(zulu_path);
	REQUIRE(restored_alpha.is_valid());
	REQUIRE(restored_zulu.is_valid());
	CHECK(restored_alpha->is_valid());
	CHECK(restored_zulu->is_valid());
	CHECK(restored_alpha->get_member_functions().has(
			SNAME("alpha_private_marker")));
	CHECK(restored_zulu->get_member_functions().has(
			SNAME("zulu_private_marker")));
	CHECK_EQ(name_mangler_export_serialize(restored_alpha),
			alpha_baseline);
	CHECK_EQ(name_mangler_export_serialize(restored_zulu),
			zulu_baseline);
}

TEST_CASE("[FoundryScript][NameManglerExport][Plugin] Preparation validates mode and the sealed generated manifest") {
	Ref<NameManglerExportTestPlatform> platform =
			memnew(NameManglerExportTestPlatform);
	Ref<EditorExportPreset> preset = platform->create_preset();
	preset->set_script_name_mangling_enabled(true);

	Ref<TestEditorExportFoundryScript> plugin =
			memnew(TestEditorExportFoundryScript);
	NameManglerExportPluginEndGuard end_guard(plugin);
	preset->set_script_export_mode(EditorExportPreset::MODE_SCRIPT_TEXT);
	plugin->begin_for_test(preset);

	EditorExportPlugin::ExportFileManifest manifest;
	String error;
	CHECK_EQ(plugin->prepare_for_test(manifest, error), ERR_INVALID_PARAMETER);
	CHECK_EQ(error,
			"Foundry Script name mangling requires the Compiled bytecode script export mode.");

	preset->set_script_export_mode(
			EditorExportPreset::MODE_SCRIPT_COMPILED_BYTECODE);
	const Vector<String> sensitive_extensions = {
		"fs", "fsc", "fsb", "tscn", "scn", "tres", "res"
	};
	for (const String &extension : sensitive_extensions) {
		plugin->begin_for_test(preset);
		manifest.generated_paths = {
			"res://generated/pending." + extension
		};
		error.clear();
		CHECK_EQ(
				plugin->prepare_for_test(manifest, error), ERR_INVALID_DATA);
		CHECK(error.contains(manifest.generated_paths[0]));
	}

	plugin->begin_for_test(preset);
	manifest.generated_paths = { "res://generated/pending.bin" };
	error.clear();
	CHECK_EQ(plugin->prepare_for_test(manifest, error), OK);
	CHECK(error.is_empty());
}

TEST_CASE("[FoundryScript][NameManglerExport][Plugin] Late validation rejects only sensitive files after preparation") {
	Ref<NameManglerExportTestPlatform> platform =
			memnew(NameManglerExportTestPlatform);
	Ref<EditorExportPreset> preset = platform->create_preset();
	preset->set_script_export_mode(
			EditorExportPreset::MODE_SCRIPT_COMPILED_BYTECODE);
	preset->set_script_name_mangling_enabled(true);
	Ref<TestEditorExportFoundryScript> plugin =
			memnew(TestEditorExportFoundryScript);
	NameManglerExportPluginEndGuard end_guard(plugin);
	plugin->begin_for_test(preset);

	EditorExportPlugin::ExportFileManifest manifest;
	String error;
	REQUIRE_EQ(plugin->prepare_for_test(manifest, error), OK);
	const Vector<String> sensitive_extensions = {
		"fs", "fsc", "fsb", "tscn", "scn", "tres", "res"
	};
	for (const String &extension : sensitive_extensions) {
		error.clear();
		const String path = "res://generated/late." + extension;
		CHECK_EQ(
				plugin->validate_late_for_test(path, error), ERR_INVALID_DATA);
		CHECK(error.contains(path));
	}
	error.clear();
	CHECK_EQ(
			plugin->validate_late_for_test(
					"res://generated/late.bin", error),
			OK);
	CHECK(error.is_empty());

	plugin->begin_for_test(preset);
	error.clear();
	CHECK_EQ(plugin->validate_late_for_test(
					 "res://generated/not_sealed.fsb", error),
			OK);
}

TEST_CASE("[FoundryScript][NameManglerExport][Plugin] Built-in scripts fail closed with or without mangling") {
	NameManglerExportFixture fixture("plugin_built_in_scripts");
	const Vector<String> scene_paths = {
		fixture.write_built_in_script_scene("built_in.tscn"),
		fixture.write_built_in_script_scene("built_in.scn"),
	};
	for (const String &scene_path : scene_paths) {
		HashSet<StringName> classes_used;
		ResourceLoader::get_classes_used(scene_path, &classes_used);
		REQUIRE(classes_used.has(SNAME("FoundryScript")));
	}

	Ref<NameManglerExportTestPlatform> platform =
			memnew(NameManglerExportTestPlatform);
	for (const bool name_mangling_enabled : { false, true }) {
		for (const String &scene_path : scene_paths) {
			platform->clear_messages();
			Ref<EditorExportPreset> preset = platform->create_preset();
			preset->set_script_export_mode(
					EditorExportPreset::MODE_SCRIPT_COMPILED_BYTECODE);
			preset->set_script_name_mangling_enabled(
					name_mangling_enabled);
			Ref<TestEditorExportFoundryScript> plugin =
					memnew(TestEditorExportFoundryScript);
			NameManglerExportPluginEndGuard end_guard(plugin);
			plugin->begin_for_test(preset);

			plugin->export_file_for_test(scene_path);
			CHECK(plugin->is_skipped_for_test());
			CHECK_EQ(plugin->get_output_count_for_test(), 0);
			CHECK(name_mangler_export_last_message_contains(
					platform, "contains a built-in script"));
		}
	}
}

TEST_CASE("[FoundryScript][NameManglerExport][Plugin] Preparation is one shot and emits the prepared cache") {
	NameManglerExportFixture fixture("plugin_prepared_cache");
	const String source_path = fixture.write_source(
			"alpha.fs",
			"extends RefCounted\n"
			"func alpha_private_marker() -> int:\n"
			"\treturn 11\n");
	const String rules_path =
			fixture.tree.root.path_join("keep_rules.pro");
	fixture.tree.write_file("keep_rules.pro", "# intentionally empty\n");

	FSNameManglerExport::Input reference_input;
	reference_input.manifest_paths.push_back(source_path);
	reference_input.keep_rules_path = rules_path;
	reference_input.release_profile = true;
	const FSNameManglerExport::Result reference =
			FSNameManglerExport::prepare(reference_input);
	REQUIRE_EQ(reference.error, OK);
	REQUIRE(reference.scripts.has(source_path));

	Ref<NameManglerExportTestPlatform> platform =
			memnew(NameManglerExportTestPlatform);
	Ref<EditorExportPreset> preset = platform->create_preset();
	preset->set_script_export_mode(
			EditorExportPreset::MODE_SCRIPT_COMPILED_BYTECODE);
	preset->set_script_name_mangling_enabled(true);
	preset->set_script_name_mangling_keep_rules(rules_path);
	Ref<TestEditorExportFoundryScript> plugin =
			memnew(TestEditorExportFoundryScript);
	NameManglerExportPluginEndGuard end_guard(plugin);
	plugin->begin_for_test(preset);

	EditorExportPlugin::ExportFileManifest manifest;
	manifest.source_paths.push_back(source_path);
	String error;
	REQUIRE_EQ(plugin->prepare_for_test(manifest, error), OK);
	REQUIRE_EQ(DirAccess::remove_absolute(rules_path), OK);
	CHECK_EQ(plugin->prepare_for_test(manifest, error), OK);

	const FSNameManglerExport::PreparedScript &prepared =
			reference.scripts[source_path];
	plugin->clear_output_for_test();
	plugin->export_file_for_test(source_path);
	CHECK_EQ(plugin->get_output_count_for_test(), 1);
	if (plugin->get_output_count_for_test() != 1) {
		return;
	}
	CHECK_EQ(plugin->get_output_path_for_test(0),
			prepared.output_path);
	CHECK_EQ(plugin->get_output_bytes_for_test(0),
			prepared.bytes);
	CHECK_EQ(plugin->get_output_remap_for_test(0), prepared.remap);
	CHECK_FALSE(plugin->is_skipped_for_test());

	error.clear();
	CHECK_EQ(plugin->validate_late_for_test(prepared.output_path, error),
			OK);
	CHECK(error.is_empty());
	error.clear();
	CHECK_EQ(plugin->validate_late_for_test(prepared.output_path, error),
			ERR_INVALID_DATA);
	CHECK(error.contains(prepared.output_path));
	error.clear();
	CHECK_EQ(plugin->validate_late_for_test(
					 "res://foreign_generated.fsb", error),
			ERR_INVALID_DATA);
	CHECK(error.contains("res://foreign_generated.fsb"));
}

TEST_CASE("[FoundryScript][NameManglerExport][Plugin] Warning-only keep rules do not veto preparation") {
	NameManglerExportFixture fixture("plugin_warning_rules");
	const String source_path = fixture.write_source(
			"warning.fs",
			"extends RefCounted\n"
			"func warning_private_marker() -> int:\n"
			"\treturn 13\n");
	fixture.tree.write_file(
			"warning_rules.pro",
			"-keep class missing.WarningOnly\n");
	const String rules_path =
			fixture.tree.root.path_join("warning_rules.pro");

	FSNameManglerExport::Input reference_input;
	reference_input.manifest_paths.push_back(source_path);
	reference_input.keep_rules_path = rules_path;
	reference_input.release_profile = true;
	const FSNameManglerExport::Result reference =
			FSNameManglerExport::prepare(reference_input);
	REQUIRE_EQ(reference.error, OK);
	REQUIRE_EQ(reference.diagnostics.size(), 1);
	if (reference.diagnostics.size() != 1) {
		return;
	}
	CHECK_EQ(reference.diagnostics[0].severity,
			FSNameManglerExport::DIAGNOSTIC_WARNING);
	CHECK(reference.diagnostics[0].message.contains("warning"));
	REQUIRE(reference.scripts.has(source_path));

	Ref<NameManglerExportTestPlatform> platform =
			memnew(NameManglerExportTestPlatform);
	Ref<EditorExportPreset> preset = platform->create_preset();
	preset->set_script_export_mode(
			EditorExportPreset::MODE_SCRIPT_COMPILED_BYTECODE);
	preset->set_script_name_mangling_enabled(true);
	preset->set_script_name_mangling_keep_rules(rules_path);
	Ref<TestEditorExportFoundryScript> plugin =
			memnew(TestEditorExportFoundryScript);
	NameManglerExportPluginEndGuard end_guard(plugin);
	plugin->begin_for_test(preset);

	EditorExportPlugin::ExportFileManifest manifest;
	manifest.source_paths.push_back(source_path);
	String error;
	ERR_PRINT_OFF;
	const Error preparation_error =
			plugin->prepare_for_test(manifest, error);
	ERR_PRINT_ON;
	CHECK_EQ(preparation_error, OK);
	CHECK(error.is_empty());
	REQUIRE_EQ(platform->get_message_count(), 1);
	if (platform->get_message_count() != 1) {
		return;
	}
	CHECK_EQ(platform->get_message(0).msg_type,
			EditorExportPlatform::EXPORT_MESSAGE_WARNING);
	CHECK(platform->get_message(0).text.contains("warning"));

	plugin->clear_output_for_test();
	plugin->export_file_for_test(source_path);
	CHECK_EQ(plugin->get_output_count_for_test(), 1);
}

TEST_CASE("[FoundryScript][NameManglerExport][Plugin] Source token and bytecode callbacks use exact cached metadata") {
	NameManglerExportFixture fixture("plugin_formats");
	const String source_path = fixture.write_source(
			"source.fs",
			"extends RefCounted\n"
			"func source_private_marker() -> int:\n"
			"\treturn 1\n");
	const String token_path = fixture.write_tokens(
			"tokens.fsc",
			"extends RefCounted\n"
			"func token_private_marker() -> int:\n"
			"\treturn 2\n");
	const String bytecode_path = fixture.write_bytecode(
			"compiled.fsb",
			"extends RefCounted\n"
			"func bytecode_private_marker() -> int:\n"
			"\treturn 3\n",
			true);
	Vector<String> paths = { source_path, token_path, bytecode_path };

	FSNameManglerExport::Input reference_input;
	reference_input.manifest_paths = paths;
	reference_input.release_profile = true;
	const FSNameManglerExport::Result reference =
			FSNameManglerExport::prepare(reference_input);
	REQUIRE_EQ(reference.error, OK);
	REQUIRE_EQ(reference.scripts.size(), 3);

	Ref<NameManglerExportTestPlatform> platform =
			memnew(NameManglerExportTestPlatform);
	Ref<EditorExportPreset> preset = platform->create_preset();
	preset->set_script_export_mode(
			EditorExportPreset::MODE_SCRIPT_COMPILED_BYTECODE);
	preset->set_script_name_mangling_enabled(true);
	Ref<TestEditorExportFoundryScript> plugin =
			memnew(TestEditorExportFoundryScript);
	NameManglerExportPluginEndGuard end_guard(plugin);
	plugin->begin_for_test(preset);

	EditorExportPlugin::ExportFileManifest manifest;
	manifest.source_paths = paths;
	String error;
	REQUIRE_EQ(plugin->prepare_for_test(manifest, error), OK);

	for (const String &path : paths) {
		const FSNameManglerExport::PreparedScript &prepared =
				reference.scripts[path];
		plugin->clear_output_for_test();
		plugin->export_file_for_test(path);
		CHECK_EQ(plugin->get_output_count_for_test(), 1);
		if (plugin->get_output_count_for_test() != 1) {
			return;
		}
		CHECK_EQ(plugin->get_output_path_for_test(0),
				prepared.output_path);
		CHECK_EQ(plugin->get_output_bytes_for_test(0),
				prepared.bytes);
		CHECK_EQ(plugin->get_output_remap_for_test(0),
				prepared.remap);
		CHECK_EQ(plugin->is_skipped_for_test(), !prepared.remap);
	}
}

TEST_CASE("[FoundryScript][NameManglerExport][Plugin] Missing cache entries fail closed and lifecycle boundaries discard bytes") {
	NameManglerExportFixture fixture("plugin_lifecycle");
	const String alpha_path = fixture.write_source(
			"alpha.fs",
			"extends RefCounted\n"
			"func alpha_private_marker() -> int:\n"
			"\treturn 1\n");
	const String beta_path = fixture.write_source(
			"beta.fs",
			"extends RefCounted\n"
			"func beta_private_marker() -> int:\n"
			"\treturn 2\n");

	Ref<NameManglerExportTestPlatform> platform =
			memnew(NameManglerExportTestPlatform);
	Ref<EditorExportPreset> preset = platform->create_preset();
	preset->set_script_export_mode(
			EditorExportPreset::MODE_SCRIPT_COMPILED_BYTECODE);
	preset->set_script_name_mangling_enabled(true);
	Ref<TestEditorExportFoundryScript> plugin =
			memnew(TestEditorExportFoundryScript);
	NameManglerExportPluginEndGuard end_guard(plugin);
	plugin->begin_for_test(preset);

	EditorExportPlugin::ExportFileManifest manifest;
	manifest.source_paths.push_back(alpha_path);
	String error;
	REQUIRE_EQ(plugin->prepare_for_test(manifest, error), OK);

	plugin->clear_output_for_test();
	ERR_PRINT_OFF;
	plugin->export_file_for_test(beta_path);
	ERR_PRINT_ON;
	CHECK(plugin->is_skipped_for_test());
	CHECK_EQ(plugin->get_output_count_for_test(), 0);
	CHECK(name_mangler_export_last_message_contains(
			platform, "prepared cache entry"));

	platform->clear_messages();
	plugin->begin_for_test(preset);
	ERR_PRINT_OFF;
	plugin->export_file_for_test(alpha_path);
	ERR_PRINT_ON;
	CHECK(name_mangler_export_last_message_contains(
			platform, "prepared cache entry"));
	CHECK(plugin->is_skipped_for_test());
	CHECK_EQ(plugin->get_output_count_for_test(), 0);

	platform->clear_messages();
	manifest.generated_paths = { "res://generated/late.fsb" };
	error.clear();
	CHECK_EQ(plugin->prepare_for_test(manifest, error), ERR_INVALID_DATA);
	ERR_PRINT_OFF;
	plugin->export_file_for_test(alpha_path);
	ERR_PRINT_ON;
	CHECK(name_mangler_export_last_message_contains(
			platform, "prepared cache entry"));
	CHECK(plugin->is_skipped_for_test());
	CHECK_EQ(plugin->get_output_count_for_test(), 0);

	platform->clear_messages();
	plugin->begin_for_test(preset);
	manifest.generated_paths.clear();
	REQUIRE_EQ(plugin->prepare_for_test(manifest, error), OK);
	plugin->end_for_test();
	plugin->begin_for_test(preset);
	ERR_PRINT_OFF;
	plugin->export_file_for_test(alpha_path);
	ERR_PRINT_ON;
	CHECK(name_mangler_export_last_message_contains(
			platform, "prepared cache entry"));
	CHECK(plugin->is_skipped_for_test());
	CHECK_EQ(plugin->get_output_count_for_test(), 0);
}

TEST_CASE("[FoundryScript][NameManglerExport][Plugin] Disabled mode preserves legacy bytes and ignores keep rules") {
	NameManglerExportFixture fixture("plugin_disabled");
	const String source_path = fixture.write_source(
			"legacy.fs",
			"extends RefCounted\n"
			"func legacy_private_marker() -> int:\n"
			"\treturn 7\n");

	Ref<NameManglerExportTestPlatform> platform =
			memnew(NameManglerExportTestPlatform);
	struct DisabledOutput {
		String path;
		Vector<uint8_t> bytes;
		bool remap = false;
		bool skipped = false;
	};
	auto export_disabled = [&](const String &p_rules_path) {
		Ref<EditorExportPreset> preset = platform->create_preset();
		preset->set_script_export_mode(
				EditorExportPreset::MODE_SCRIPT_COMPILED_BYTECODE);
		preset->set_script_name_mangling_enabled(false);
		preset->set_script_name_mangling_keep_rules(p_rules_path);
		Ref<TestEditorExportFoundryScript> plugin =
				memnew(TestEditorExportFoundryScript);
		NameManglerExportPluginEndGuard end_guard(plugin);
		plugin->begin_for_test(preset);
		EditorExportPlugin::ExportFileManifest manifest;
		manifest.source_paths.push_back(source_path);
		String error;
		const Error preparation_error =
				plugin->prepare_for_test(manifest, error);
		CHECK_EQ(preparation_error, OK);
		DisabledOutput output;
		if (preparation_error != OK) {
			return output;
		}
		plugin->clear_output_for_test();
		plugin->export_file_for_test(source_path);
		CHECK_EQ(plugin->get_output_count_for_test(), 1);
		if (plugin->get_output_count_for_test() != 1) {
			return output;
		}
		output.path = plugin->get_output_path_for_test(0);
		output.bytes = plugin->get_output_bytes_for_test(0);
		output.remap = plugin->get_output_remap_for_test(0);
		output.skipped = plugin->is_skipped_for_test();
		return output;
	};

	const DisabledOutput baseline = export_disabled(String());
	const String output_path = source_path.get_basename() + ".fsb";
	CHECK_EQ(baseline.path, output_path);
	CHECK_FALSE(baseline.bytes.is_empty());
	CHECK(baseline.remap);
	CHECK_FALSE(baseline.skipped);

	const String missing_rules =
			fixture.tree.root.path_join("missing_keep_rules.pro");
	CHECK_FALSE(FileAccess::exists(missing_rules));
	const DisabledOutput with_missing_rules =
			export_disabled(missing_rules);
	CHECK_EQ(with_missing_rules.path, baseline.path);
	CHECK_EQ(with_missing_rules.bytes, baseline.bytes);
	CHECK_EQ(with_missing_rules.remap, baseline.remap);
	CHECK_EQ(with_missing_rules.skipped, baseline.skipped);
	CHECK_EQ(platform->get_message_count(), 0);
}

TEST_CASE("[FoundryScript][NameManglerExport][Pack] Command-first export runs a real mangled pack") {
	TemporaryProjectTree project(
			"fs_name_mangler_export_pack_" +
			itos(OS::get_singleton()->get_ticks_usec()));
	project.write_file(
			"project.foundry",
			"[application]\n"
			"config/name=\"Name Mangler Pack Acceptance\"\n"
			"run/main_scene=\"res://main.tscn\"\n"
			"\n"
			"[rendering]\n"
			"renderer/rendering_method=\"gl_compatibility\"\n");
	project.write_file(
			"export_presets.cfg",
			"[preset.0]\n"
			"name=\"Mangled\"\n"
			"platform=\"Linux\"\n"
			"runnable=false\n"
			"dedicated_server=false\n"
			"custom_features=\"\"\n"
			"export_filter=\"all_resources\"\n"
			"include_filter=\"\"\n"
			"exclude_filter=\"\"\n"
			"export_path=\"\"\n"
			"script_export_mode=3\n"
			"script_name_mangling_enabled=true\n"
			"script_name_mangling_keep_rules=\"res://name-mangler.pro\"\n"
			"\n"
			"[preset.0.options]\n"
			"custom_template/debug=\"\"\n"
			"custom_template/release=\"\"\n");
	project.write_file(
			"name-mangler.pro",
			"-keepclassmembers class ** {\n"
			"\trule_kept;\n"
			"}\n");
	project.write_file(
			"base.fs",
			"extends Node\n"
			"@export var inherited_override: int = 0\n"
			"func pack_private_unkept_marker(value: int) -> int:\n"
			"\treturn value + 1\n");
	project.write_file(
			"emitter.fs",
			"extends Node\n"
			"signal scene_signal(value: int)\n"
			"func fire() -> void:\n"
			"\tscene_signal.emit(7)\n");
	project.write_file(
			"receiver.fs",
			"extends \"res://base.fs\"\n"
			"var connection_value: int = 0\n"
			"var animated_value: float = 0.0\n"
			"func scene_handler(value: int) -> void:\n"
			"\tconnection_value = value\n"
			"func rule_kept(value: int) -> int:\n"
			"\treturn value + 5\n"
			"func cross_file_value() -> int:\n"
			"\treturn pack_private_unkept_marker(40)\n");
	project.write_file(
			"payload.fs",
			"extends Resource\n"
			"@export var stored_value: int = 0\n");
	project.write_file(
			"payload.tres",
			"[gd_resource type=\"Resource\" load_steps=2 format=3]\n"
			"\n"
			"[ext_resource type=\"Script\" path=\"res://payload.fs\" id=\"1_payload\"]\n"
			"\n"
			"[resource]\n"
			"script = ExtResource(\"1_payload\")\n"
			"stored_value = 11\n");
	project.write_file(
			"main.fs",
			"extends Node\n"
			"const EmitterScript = preload(\"res://emitter.fs\")\n"
			"const ReceiverScript = preload(\"res://receiver.fs\")\n"
			"const PayloadScript = preload(\"res://payload.fs\")\n"
			"@onready var emitter: EmitterScript = $Emitter\n"
			"@onready var receiver: ReceiverScript = $Receiver\n"
			"@onready var animation_player: AnimationPlayer = $AnimationPlayer\n"
			"func _ready() -> void:\n"
			"\temitter.fire()\n"
			"\tanimation_player.play(\"probe\")\n"
			"\tanimation_player.advance(0.1)\n"
			"\tvar payload := load(\"res://payload.tres\") as PayloadScript\n"
			"\tvar dynamic_value: int = receiver.call(\"rule_kept\", 5)\n"
			"\tvar valid := receiver.connection_value == 7\n"
			"\tvalid = valid and receiver.inherited_override == 23\n"
			"\tvalid = valid and receiver.animated_value == 9.0\n"
			"\tvalid = valid and payload != null and payload.stored_value == 11\n"
			"\tvalid = valid and dynamic_value == 10\n"
			"\tvalid = valid and receiver.cross_file_value() == 41\n"
			"\tif valid:\n"
			"\t\tprint(\"NAME_MANGLER_PACK_RUNTIME_OK\")\n"
			"\t\tget_tree().quit(0)\n"
			"\telse:\n"
			"\t\tpush_error(\"NAME_MANGLER_PACK_RUNTIME_FAILED\")\n"
			"\t\tget_tree().quit(1)\n");
	project.write_file(
			"main.tscn",
			"[gd_scene load_steps=8 format=3]\n"
			"\n"
			"[ext_resource type=\"Script\" path=\"res://main.fs\" id=\"1_main\"]\n"
			"[ext_resource type=\"Script\" path=\"res://emitter.fs\" id=\"2_emitter\"]\n"
			"[ext_resource type=\"Script\" path=\"res://receiver.fs\" id=\"3_receiver\"]\n"
			"\n"
			"[sub_resource type=\"Animation\" id=\"Animation_reset\"]\n"
			"resource_name = \"RESET\"\n"
			"length = 0.0\n"
			"tracks/0/type = \"value\"\n"
			"tracks/0/path = NodePath(\"Receiver:animated_value\")\n"
			"tracks/0/keys = {\"times\": PackedFloat32Array(0), \"transitions\": PackedFloat32Array(1), \"update\": 0, \"values\": [0.0]}\n"
			"\n"
			"[sub_resource type=\"Animation\" id=\"Animation_probe\"]\n"
			"resource_name = \"probe\"\n"
			"length = 0.1\n"
			"tracks/0/type = \"value\"\n"
			"tracks/0/path = NodePath(\"Receiver:animated_value\")\n"
			"tracks/0/keys = {\"times\": PackedFloat32Array(0), \"transitions\": PackedFloat32Array(1), \"update\": 0, \"values\": [9.0]}\n"
			"\n"
			"[sub_resource type=\"AnimationLibrary\" id=\"AnimationLibrary_main\"]\n"
			"_data = {\"RESET\": SubResource(\"Animation_reset\"), \"probe\": SubResource(\"Animation_probe\")}\n"
			"\n"
			"[node name=\"Main\" type=\"Node\"]\n"
			"script = ExtResource(\"1_main\")\n"
			"\n"
			"[node name=\"Emitter\" type=\"Node\" parent=\".\"]\n"
			"script = ExtResource(\"2_emitter\")\n"
			"\n"
			"[node name=\"Receiver\" type=\"Node\" parent=\".\"]\n"
			"script = ExtResource(\"3_receiver\")\n"
			"inherited_override = 23\n"
			"\n"
			"[node name=\"AnimationPlayer\" type=\"AnimationPlayer\" parent=\".\"]\n"
			"libraries/ = SubResource(\"AnimationLibrary_main\")\n"
			"\n"
			"[connection signal=\"scene_signal\" from=\"Emitter\" to=\"Receiver\" method=\"scene_handler\"]\n");

	const String pack_path = project.root.path_join("mangled.pck");
	List<String> export_arguments;
	export_arguments.push_back("--headless");
	export_arguments.push_back("project");
	export_arguments.push_back("export");
	export_arguments.push_back("--project");
	export_arguments.push_back(project.root);
	export_arguments.push_back("--preset");
	export_arguments.push_back("Mangled");
	export_arguments.push_back("--output");
	export_arguments.push_back(pack_path);
	export_arguments.push_back("--mode");
	export_arguments.push_back("pack");
	const NameManglerPackProcessResult export_result =
			name_mangler_export_run_process(export_arguments);
	INFO("Export output:\n", export_result.output);
	REQUIRE_EQ(export_result.error, OK);
	REQUIRE_EQ(export_result.exit_code, 0);
	REQUIRE(FileAccess::exists(pack_path));
	if (export_result.error != OK || export_result.exit_code != 0 ||
			!FileAccess::exists(pack_path)) {
		return;
	}

	const String runtime_root = project.root.path_join("runtime");
	const Error make_runtime_error =
			DirAccess::make_dir_recursive_absolute(runtime_root);
	REQUIRE_EQ(make_runtime_error, OK);
	if (make_runtime_error != OK) {
		return;
	}
	const String runtime_pack = runtime_root.path_join(
			OS::get_singleton()->get_executable_path().get_file() + ".pck");
	Ref<DirAccess> filesystem =
			DirAccess::create(DirAccess::ACCESS_FILESYSTEM);
	REQUIRE(filesystem.is_valid());
	if (filesystem.is_null()) {
		return;
	}
	const Error copy_error = filesystem->copy(pack_path, runtime_pack);
	REQUIRE_EQ(copy_error, OK);
	if (copy_error != OK) {
		return;
	}
	List<String> runtime_arguments;
	runtime_arguments.push_back("--headless");
	runtime_arguments.push_back("project");
	runtime_arguments.push_back("run");
	const NameManglerPackProcessResult runtime_result =
			name_mangler_export_run_process(
					runtime_arguments, runtime_root);
	INFO("Runtime output:\n", runtime_result.output);
	REQUIRE_EQ(runtime_result.error, OK);
	if (runtime_result.error != OK) {
		return;
	}
	CHECK_EQ(runtime_result.exit_code, 0);
	CHECK(runtime_result.output.contains(
			"NAME_MANGLER_PACK_RUNTIME_OK"));

	NameManglerPackMount mount;
	REQUIRE_EQ(mount.mount(pack_path), OK);
	PackedData *packed_data = PackedData::get_singleton();
	REQUIRE(packed_data != nullptr);
	const Vector<String> script_stems = {
		"base", "emitter", "receiver", "payload", "main"
	};
	for (const String &stem : script_stems) {
		const String source_path = "res://" + stem + ".fs";
		const String bytecode_path = "res://" + stem + ".fsb";
		CHECK_FALSE(packed_data->has_path(source_path));
		REQUIRE(packed_data->has_path(bytecode_path));
	}
	const Vector<uint8_t> receiver_bytes =
			mount.read("res://receiver.fsb");
	REQUIRE_FALSE(receiver_bytes.is_empty());
	CHECK(bytecode_buffer_contains(receiver_bytes, "scene_handler"));
	CHECK(bytecode_buffer_contains(receiver_bytes, "rule_kept"));
	CHECK(bytecode_buffer_contains(receiver_bytes, "animated_value"));
	CHECK(bytecode_buffer_contains(
			receiver_bytes, "inherited_override"));
	CHECK_FALSE(bytecode_buffer_contains(
			receiver_bytes, "pack_private_unkept_marker"));
}

} // namespace FSTests

#endif // TOOLS_ENABLED
