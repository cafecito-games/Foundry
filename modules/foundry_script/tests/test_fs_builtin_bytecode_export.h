/**************************************************************************/
/*  test_fs_builtin_bytecode_export.h                                     */
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

#include "modules/foundry_script/fs_builtin_sources.h"
#include "modules/foundry_script/fs_bytecode_loader.h"
#include "modules/foundry_script/fs_cache.h"

#include "test_name_mangler_export.h"

#include "tests/test_macros.h"

namespace FSTests {

// Registers an extra builtin source for one case and takes it back out again, including the cache
// entries the export compile leaves behind, so the registered builtin set other cases see is
// unchanged.
struct ScopedBuiltinSource {
	String path;

	ScopedBuiltinSource(const String &p_path, const String &p_source) :
			path(p_path) {
		FSBuiltinSources::register_source(path, p_source);
	}

	~ScopedBuiltinSource() {
		FSBuiltinSources::unregister_source(path);
		FSCache::remove_script(path);
	}
};

static Vector<String> builtin_export_registered_paths() {
	List<String> paths;
	FSBuiltinSources::get_registered_paths(&paths);
	Vector<String> result;
	for (const String &path : paths) {
		result.push_back(path);
	}
	return result;
}

static Ref<TestEditorExportFoundryScript> builtin_export_make_plugin(
		const Ref<NameManglerExportTestPlatform> &p_platform,
		EditorExportPreset::ScriptExportMode p_mode, bool p_name_mangling) {
	Ref<EditorExportPreset> preset = p_platform->create_preset();
	preset->set_script_export_mode(p_mode);
	preset->set_script_name_mangling_enabled(p_name_mangling);
	Ref<TestEditorExportFoundryScript> plugin =
			memnew(TestEditorExportFoundryScript);
	plugin->begin_for_test(preset);
	return plugin;
}

// Publishes every staged builtin skeleton before any of them links, which is what lets mutually
// referencing builtins resolve; the runtime cache does the same thing by publishing a shell first.
static void builtin_export_link_all(
		const Vector<String> &p_paths, const Vector<Vector<uint8_t>> &p_buffers) {
	REQUIRE_EQ(p_paths.size(), p_buffers.size());
	BytecodeTestResolver resolver;
	Vector<Ref<FoundryScript>> skeletons;
	for (int i = 0; i < p_paths.size(); i++) {
		Ref<FoundryScript> script;
		script.instantiate();
		script->set_path_cache(p_paths[i]);
		FSBytecodeLoader loader;
		REQUIRE_EQ(loader.load_skeleton(p_buffers[i], script), OK);
		skeletons.push_back(script);
		resolver.scripts[p_paths[i] + "::" + script->get_fully_qualified_name()] = script;
	}
	for (int i = 0; i < p_paths.size(); i++) {
		FSBytecodeLoader loader;
		loader.set_resolver(&resolver);
		CHECK_EQ(loader.load_full(p_buffers[i], skeletons[i]), OK);
		CHECK(skeletons[i]->is_valid());
	}
}

TEST_CASE("[FoundryScript][BuiltinBytecodeExport] Compiled-bytecode preparation stages one private artifact per builtin") {
	NameManglerExportFixture fixture("builtin_artifact_set");
	const Vector<String> builtin_paths = builtin_export_registered_paths();
	REQUIRE_FALSE(builtin_paths.is_empty());

	Ref<NameManglerExportTestPlatform> platform =
			memnew(NameManglerExportTestPlatform);
	Ref<TestEditorExportFoundryScript> plugin = builtin_export_make_plugin(
			platform, EditorExportPreset::MODE_SCRIPT_COMPILED_BYTECODE, false);
	NameManglerExportPluginEndGuard end_guard(plugin);

	EditorExportPlugin::ExportFileManifest manifest;
	String error;
	REQUIRE_EQ(plugin->prepare_for_test(manifest, error), OK);
	CHECK(error.is_empty());
	CHECK_FALSE(plugin->is_skipped_for_test());

	REQUIRE_EQ(plugin->get_output_count_for_test(), builtin_paths.size());
	Vector<Vector<uint8_t>> buffers;
	for (int i = 0; i < builtin_paths.size(); i++) {
		// The published order follows the sorted virtual paths, so the pack contents of two
		// exports of the same project are comparable entry by entry.
		CHECK_EQ(plugin->get_output_path_for_test(i),
				FSBuiltinSources::get_exported_bytecode_path(builtin_paths[i]));
		// Builtins are addressed through their virtual identity, never through a `.remap`.
		CHECK_FALSE(plugin->get_output_remap_for_test(i));
		const Vector<uint8_t> bytes = plugin->get_output_bytes_for_test(i);
		CHECK_FALSE(bytes.is_empty());
		CHECK_EQ(FSBytecodeLoader::check_header(bytes), OK);
		buffers.push_back(bytes);
	}

	builtin_export_link_all(builtin_paths, buffers);
}

TEST_CASE("[FoundryScript][BuiltinBytecodeExport] Repeated preparation produces identical paths and bytes") {
	NameManglerExportFixture fixture("builtin_determinism");
	Ref<NameManglerExportTestPlatform> platform =
			memnew(NameManglerExportTestPlatform);
	EditorExportPlugin::ExportFileManifest manifest;

	Vector<String> first_paths;
	Vector<Vector<uint8_t>> first_buffers;
	for (int round = 0; round < 2; round++) {
		Ref<TestEditorExportFoundryScript> plugin = builtin_export_make_plugin(
				platform, EditorExportPreset::MODE_SCRIPT_COMPILED_BYTECODE, false);
		NameManglerExportPluginEndGuard end_guard(plugin);
		String error;
		REQUIRE_EQ(plugin->prepare_for_test(manifest, error), OK);
		REQUIRE_GT(plugin->get_output_count_for_test(), 0);

		if (round == 0) {
			for (int i = 0; i < plugin->get_output_count_for_test(); i++) {
				first_paths.push_back(plugin->get_output_path_for_test(i));
				first_buffers.push_back(plugin->get_output_bytes_for_test(i));
			}
			continue;
		}

		REQUIRE_EQ(plugin->get_output_count_for_test(), first_paths.size());
		for (int i = 0; i < first_paths.size(); i++) {
			CHECK_EQ(plugin->get_output_path_for_test(i), first_paths[i]);
			CHECK_EQ(plugin->get_output_bytes_for_test(i), first_buffers[i]);
		}
	}
}

TEST_CASE("[FoundryScript][BuiltinBytecodeExport] Preparation is one shot per export") {
	NameManglerExportFixture fixture("builtin_one_shot");
	Ref<NameManglerExportTestPlatform> platform =
			memnew(NameManglerExportTestPlatform);
	Ref<TestEditorExportFoundryScript> plugin = builtin_export_make_plugin(
			platform, EditorExportPreset::MODE_SCRIPT_COMPILED_BYTECODE, false);
	NameManglerExportPluginEndGuard end_guard(plugin);

	EditorExportPlugin::ExportFileManifest manifest;
	String error;
	REQUIRE_EQ(plugin->prepare_for_test(manifest, error), OK);
	const int published = plugin->get_output_count_for_test();
	REQUIRE_GT(published, 0);
	CHECK_EQ(plugin->prepare_for_test(manifest, error), OK);
	CHECK_EQ(plugin->get_output_count_for_test(), published);
}

TEST_CASE("[FoundryScript][BuiltinBytecodeExport] Only compiled-bytecode mode emits private builtin artifacts") {
	NameManglerExportFixture fixture("builtin_modes");
	Ref<NameManglerExportTestPlatform> platform =
			memnew(NameManglerExportTestPlatform);
	const Vector<EditorExportPreset::ScriptExportMode> modes = {
		EditorExportPreset::MODE_SCRIPT_TEXT,
		EditorExportPreset::MODE_SCRIPT_BINARY_TOKENS,
		EditorExportPreset::MODE_SCRIPT_BINARY_TOKENS_COMPRESSED,
	};
	for (const EditorExportPreset::ScriptExportMode mode : modes) {
		Ref<TestEditorExportFoundryScript> plugin =
				builtin_export_make_plugin(platform, mode, false);
		NameManglerExportPluginEndGuard end_guard(plugin);
		EditorExportPlugin::ExportFileManifest manifest;
		String error;
		CHECK_EQ(plugin->prepare_for_test(manifest, error), OK);
		CHECK_EQ(plugin->get_output_count_for_test(), 0);
	}
}

TEST_CASE("[FoundryScript][BuiltinBytecodeExport] A reserved output path fails before publication") {
	NameManglerExportFixture fixture("builtin_collision");
	const Vector<String> builtin_paths = builtin_export_registered_paths();
	REQUIRE_FALSE(builtin_paths.is_empty());
	const String collided_path =
			FSBuiltinSources::get_exported_bytecode_path(builtin_paths[0]);
	REQUIRE_FALSE(collided_path.is_empty());

	Ref<NameManglerExportTestPlatform> platform =
			memnew(NameManglerExportTestPlatform);
	for (int round = 0; round < 2; round++) {
		platform->clear_messages();
		Ref<TestEditorExportFoundryScript> plugin = builtin_export_make_plugin(
				platform, EditorExportPreset::MODE_SCRIPT_COMPILED_BYTECODE, false);
		NameManglerExportPluginEndGuard end_guard(plugin);

		EditorExportPlugin::ExportFileManifest manifest;
		// A project file and a previously generated file are equally disqualifying: the exporter
		// must never overwrite either one.
		if (round == 0) {
			manifest.source_paths.push_back(collided_path);
		} else {
			manifest.generated_paths.push_back(collided_path);
		}
		String error;
		ERR_PRINT_OFF;
		const Error prepare_error = plugin->prepare_for_test(manifest, error);
		ERR_PRINT_ON;
		CHECK_EQ(prepare_error, ERR_ALREADY_EXISTS);
		CHECK(error.contains(collided_path));
		CHECK_EQ(plugin->get_output_count_for_test(), 0);
		CHECK(name_mangler_export_last_message_contains(platform, collided_path));
	}
}

TEST_CASE("[FoundryScript][BuiltinBytecodeExport] A broken builtin fails atomically and names its virtual path") {
	NameManglerExportFixture fixture("builtin_broken");
	// Sorts after every shipped builtin, so the healthy ones have already been staged when this
	// one fails: nothing may be published.
	const String broken_path = "foundry://builtin/zz_broken_export_probe.fs";
	ScopedBuiltinSource broken_source(broken_path,
			"class_name ZzBrokenExportProbe extends RefCounted\n"
			"func broken( -> int:\n"
			"\treturn 1\n");

	Ref<NameManglerExportTestPlatform> platform =
			memnew(NameManglerExportTestPlatform);
	Ref<TestEditorExportFoundryScript> plugin = builtin_export_make_plugin(
			platform, EditorExportPreset::MODE_SCRIPT_COMPILED_BYTECODE, false);
	NameManglerExportPluginEndGuard end_guard(plugin);

	EditorExportPlugin::ExportFileManifest manifest;
	String error;
	ERR_PRINT_OFF;
	const Error prepare_error = plugin->prepare_for_test(manifest, error);
	ERR_PRINT_ON;
	CHECK_NE(prepare_error, OK);
	CHECK(error.contains(broken_path));
	CHECK_EQ(plugin->get_output_count_for_test(), 0);
	CHECK(name_mangler_export_last_message_contains(platform, broken_path));
}

TEST_CASE("[FoundryScript][BuiltinBytecodeExport] Runtime bytecode lookups map builtin identities") {
	CHECK_EQ(FSCache::get_bytecode_artifact_path("foundry://builtin/json_node.fs"),
			"res://.foundry/builtin/json_node.fsb");
	// Everything else reads its own path; the caller has already applied any remap.
	CHECK_EQ(FSCache::get_bytecode_artifact_path("res://player.fsb"), "res://player.fsb");
	CHECK_EQ(FSCache::get_bytecode_artifact_path("res://player.fsc"), "res://player.fsc");
	// An unmappable builtin identity keeps its own path so the failure names what was asked for.
	CHECK_EQ(FSCache::get_bytecode_artifact_path("foundry://builtin/no_extension"),
			"foundry://builtin/no_extension");

	// A builtin whose private artifact was never packaged fails as a plain unreadable file
	// instead of silently falling back to the embedded source.
	ERR_PRINT_OFF;
	const Vector<uint8_t> missing =
			FSCache::get_binary_tokens("foundry://builtin/zz_unpackaged_probe.fs");
	ERR_PRINT_ON;
	CHECK(missing.is_empty());
}

TEST_CASE("[FoundryScript][BuiltinBytecodeExport] Name-mangled exports treat builtins as engine externals") {
	NameManglerExportFixture fixture("builtin_name_mangled");
	const String source_path = fixture.write_source(
			"builtin_user.fs",
			"extends RefCounted\n"
			"uses JsonSerializable\n"
			"\n"
			"var stored_label: String = \"kept\"\n"
			"\n"
			"func to_json() -> JsonNode:\n"
			"\tvar entries: Dictionary[String, JsonNode] = {}\n"
			"\tentries[\"label\"] = JsonNode.Str(stored_label)\n"
			"\treturn JsonNode.Object(entries)\n"
			"\n"
			"static func from_json(node: JsonNode) -> JsonResult[Self]:\n"
			"\tmatch node:\n"
			"\t\tJsonNode.Object(var entries):\n"
			"\t\t\tif not entries.has(\"label\"):\n"
			"\t\t\t\treturn JsonResult[Self].fail(\"missing field\", \"$.label\")\n"
			"\t\t\treturn JsonResult[Self].fail(\"not decodable\", \"$.label\")\n"
			"\t\t_:\n"
			"\t\t\treturn JsonResult[Self].fail(\"expected an object\", \"$\")\n");

	// The project manifest deliberately holds no builtin path: builtins are engine-provided
	// externals, so requiring them here would make engine-owned API part of the project graph.
	FSNameManglerExport::Input input;
	input.manifest_paths.push_back(source_path);
	const FSNameManglerExport::Result result = FSNameManglerExport::prepare(input);
	for (const FSNameManglerExport::Diagnostic &diagnostic : result.diagnostics) {
		MESSAGE(diagnostic.format());
	}
	CHECK_EQ(result.error, OK);
	CHECK(result.diagnostics.is_empty());
	REQUIRE(result.scripts.has(source_path));
	if (!result.scripts.has(source_path)) {
		return;
	}

	Vector<String> dependencies;
	FSBytecodeLoader loader;
	REQUIRE_EQ(loader.read_dependencies(result.scripts[source_path].bytes, dependencies), OK);
	bool records_builtin_identity = false;
	for (const String &dependency : dependencies) {
		if (FSBuiltinSources::is_builtin_path(dependency)) {
			// The recorded path is the unchanged public identity, not the private artifact.
			CHECK(dependency.ends_with(".fs"));
			records_builtin_identity = true;
		}
	}
	CHECK(records_builtin_identity);

	// Builtin declarations keep their names: only the project script's own members are renamed.
	const Ref<FoundryScript> decode_error =
			FSCache::get_cached_script("foundry://builtin/json_decode_error.fs");
	REQUIRE(decode_error.is_valid());
	if (decode_error.is_valid()) {
		CHECK(decode_error->get_members().has(SNAME("message")));
		CHECK(decode_error->get_members().has(SNAME("path")));
		CHECK(decode_error->get_member_functions().has(SNAME("create")));
	}

	Ref<NameManglerExportTestPlatform> platform =
			memnew(NameManglerExportTestPlatform);
	Ref<TestEditorExportFoundryScript> plugin = builtin_export_make_plugin(
			platform, EditorExportPreset::MODE_SCRIPT_COMPILED_BYTECODE, true);
	NameManglerExportPluginEndGuard end_guard(plugin);

	EditorExportPlugin::ExportFileManifest manifest;
	manifest.source_paths.push_back(source_path);
	String error;
	REQUIRE_EQ(plugin->prepare_for_test(manifest, error), OK);

	const Vector<String> builtin_paths = builtin_export_registered_paths();
	REQUIRE_EQ(plugin->get_output_count_for_test(), builtin_paths.size());
	for (int i = 0; i < builtin_paths.size(); i++) {
		const String output_path = plugin->get_output_path_for_test(i);
		CHECK_EQ(output_path, FSBuiltinSources::get_exported_bytecode_path(builtin_paths[i]));
		CHECK_FALSE(plugin->get_output_remap_for_test(i));
		// Sealed-manifest validation has to accept exactly these engine-owned outputs once.
		String late_error;
		CHECK_EQ(plugin->validate_late_for_test(output_path, late_error), OK);
		CHECK(late_error.is_empty());
	}
}

} // namespace FSTests

#endif // TOOLS_ENABLED
