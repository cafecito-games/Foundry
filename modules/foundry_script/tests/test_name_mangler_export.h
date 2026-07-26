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

#include "modules/foundry_script/editor/fs_name_mangler_export.h"

#include "fs_temporary_project_tree.h"

#include "modules/foundry_script/foundry_script.h"
#include "modules/foundry_script/fs_bytecode_export.h"
#include "modules/foundry_script/fs_bytecode_loader.h"
#include "modules/foundry_script/fs_cache.h"
#include "modules/foundry_script/fs_tokenizer_buffer.h"

#include "core/config/project_settings.h"
#include "core/io/file_access.h"
#include "core/io/resource_loader.h"
#include "core/os/os.h"
#include "tests/test_macros.h"

namespace FSTests {

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

} // namespace FSTests

#endif // TOOLS_ENABLED
