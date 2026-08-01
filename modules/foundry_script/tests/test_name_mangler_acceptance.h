/**************************************************************************/
/*  test_name_mangler_acceptance.h                                        */
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

#include "fs_name_mangler_export_test_utils.h"
#include "fs_temporary_project_tree.h"
#include "modules/foundry_script/fs_builtin_sources.h"
#include "modules/foundry_script/tests/test_bytecode_serialization.h"

#include "core/io/dir_access.h"
#include "core/templates/rb_map.h"
#include "tests/test_macros.h"

namespace FSTests {

static NameManglerPackProcessResult name_mangler_acceptance_export(
		const String &p_project_root, const String &p_preset,
		const String &p_pack_path) {
	List<String> arguments;
	arguments.push_back("--headless");
	arguments.push_back("project");
	arguments.push_back("export");
	arguments.push_back("--project");
	arguments.push_back(p_project_root);
	arguments.push_back("--preset");
	arguments.push_back(p_preset);
	arguments.push_back("--output");
	arguments.push_back(p_pack_path);
	arguments.push_back("--mode");
	arguments.push_back("pack");
	return name_mangler_export_run_process(arguments);
}

static NameManglerPackProcessResult name_mangler_acceptance_run(
		const String &p_pack_path, const String &p_runtime_root) {
	NameManglerPackProcessResult failed;
	const Error make_error =
			DirAccess::make_dir_recursive_absolute(p_runtime_root);
	if (make_error != OK) {
		failed.error = make_error;
		return failed;
	}
	Ref<DirAccess> filesystem =
			DirAccess::create(DirAccess::ACCESS_FILESYSTEM);
	if (filesystem.is_null()) {
		failed.error = ERR_CANT_CREATE;
		return failed;
	}
	const String runtime_pack = p_runtime_root.path_join(
			OS::get_singleton()->get_executable_path().get_file() + ".pck");
	const Error copy_error = filesystem->copy(p_pack_path, runtime_pack);
	if (copy_error != OK) {
		failed.error = copy_error;
		return failed;
	}

	List<String> arguments;
	arguments.push_back("--headless");
	arguments.push_back("project");
	arguments.push_back("run");
	return name_mangler_export_run_process(arguments, p_runtime_root);
}

static String name_mangler_acceptance_transcript(
		const String &p_output) {
	const String prefix = "NAME_MANGLER_ACCEPTANCE|";
	String transcript;
	for (const String &raw_line : p_output.split("\n")) {
		const String line = raw_line.strip_edges();
		if (!line.begins_with(prefix)) {
			continue;
		}
		REQUIRE_MESSAGE(transcript.is_empty(),
				"Runtime emitted more than one acceptance transcript.");
		transcript = line;
	}
	return transcript;
}

static String name_mangler_acceptance_missing_call_diagnostic(
		const String &p_output, const String &p_target) {
	const String lower_target = p_target.to_lower();
	for (const String &raw_line : p_output.split("\n")) {
		const String line = raw_line.strip_edges();
		const String lower_line = line.to_lower();
		const bool has_missing_call_meaning =
				lower_line.contains("nonexistent function") ||
				lower_line.contains("invalid call") ||
				lower_line.contains("method not found");
		if (lower_line.contains(lower_target) &&
				has_missing_call_meaning) {
			return line;
		}
	}
	return String();
}

static RBMap<String, Vector<uint8_t>>
name_mangler_acceptance_read_scripts(const String &p_pack_path) {
	RBMap<String, Vector<uint8_t>> scripts;
	NameManglerPackMount mount;
	REQUIRE_EQ(mount.mount(p_pack_path), OK);
	PackedData *packed_data = PackedData::get_singleton();
	REQUIRE(packed_data != nullptr);
	for (const String &path : packed_data->get_file_paths()) {
		if (path.get_extension().to_lower() != "fsb") {
			continue;
		}
		const Vector<uint8_t> bytes = mount.read(path);
		REQUIRE_FALSE(bytes.is_empty());
		const String simplified = path.simplify_path();
		const String resource_path = simplified.begins_with("res://") ? simplified : "res://" + simplified.trim_prefix("/");
		scripts.insert(resource_path, bytes);
	}
	return scripts;
}

static void name_mangler_acceptance_require_script_inventory(
		const RBMap<String, Vector<uint8_t>> &p_scripts,
		const String &p_label) {
	static constexpr int expected_project_path_count = 7;
	static const char *expected_project_paths[expected_project_path_count] = {
		"res://acceptance_base.fsb",
		"res://acceptance_derived.fsb",
		"res://acceptance_trait.fsb",
		"res://emitter.fsb",
		"res://main.fsb",
		"res://payload.fsb",
		"res://reflector.fsb",
	};
	CAPTURE(p_label);

	// A compiled-bytecode pack also carries one private companion artifact per registered
	// builtin, so a stripped template can load the engine-provided types it has no front-end
	// to compile. Both the ordinary and the name-mangled export must contain the same set.
	Vector<String> expected_paths;
	List<String> builtin_paths;
	FSBuiltinSources::get_registered_paths(&builtin_paths);
	for (const String &builtin_path : builtin_paths) {
		const String artifact_path =
				FSBuiltinSources::get_exported_bytecode_path(builtin_path);
		REQUIRE_FALSE(artifact_path.is_empty());
		expected_paths.push_back(artifact_path);
	}
	for (int i = 0; i < expected_project_path_count; i++) {
		expected_paths.push_back(expected_project_paths[i]);
	}
	expected_paths.sort();

	REQUIRE_EQ(p_scripts.size(), expected_paths.size());
	if (p_scripts.size() != expected_paths.size()) {
		return;
	}
	int index = 0;
	for (const KeyValue<String, Vector<uint8_t>> &entry : p_scripts) {
		CAPTURE(index);
		CHECK_EQ(entry.key, expected_paths[index]);
		index++;
	}
}

static bool name_mangler_acceptance_contains(
		const RBMap<String, Vector<uint8_t>> &p_scripts,
		const String &p_marker) {
	for (const KeyValue<String, Vector<uint8_t>> &entry : p_scripts) {
		if (bytecode_buffer_contains(entry.value, p_marker)) {
			return true;
		}
	}
	return false;
}

static void name_mangler_acceptance_write_safe_project(
		TemporaryProjectTree &p_project) {
	p_project.write_file(
			"project.foundry",
			"[application]\n"
			"config/name=\"Name Mangler End-to-End Acceptance\"\n"
			"run/main_scene=\"res://main.tscn\"\n"
			"\n"
			"[rendering]\n"
			"renderer/rendering_method=\"gl_compatibility\"\n");
	p_project.write_file(
			"export_presets.cfg",
			"[preset.0]\n"
			"name=\"Unmangled\"\n"
			"platform=\"Linux\"\n"
			"runnable=false\n"
			"dedicated_server=false\n"
			"custom_features=\"\"\n"
			"export_filter=\"all_resources\"\n"
			"include_filter=\"\"\n"
			"exclude_filter=\"\"\n"
			"export_path=\"\"\n"
			"script_export_mode=3\n"
			"script_name_mangling_enabled=false\n"
			"script_name_mangling_keep_rules=\"\"\n"
			"\n"
			"[preset.0.options]\n"
			"custom_template/debug=\"\"\n"
			"custom_template/release=\"\"\n"
			"\n"
			"[preset.1]\n"
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
			"[preset.1.options]\n"
			"custom_template/debug=\"\"\n"
			"custom_template/release=\"\"\n");
	p_project.write_file(
			"name-mangler.pro",
			"-keepclassmembers class ** {\n"
			"\truled_dynamic_target;\n"
			"}\n");
	p_project.write_file(
			"acceptance_trait.fs",
			"trait_name AcceptanceTrait[T]\n"
			"\n"
			"abstract func trait_value(value: T) -> T\n");
	p_project.write_file(
			"acceptance_base.fs",
			"class_name AcceptanceGenericBase[T]\n"
			"extends Node\n"
			"\n"
			"signal declared_signal(value: T)\n"
			"@export var scene_export_value: int = 0\n"
			"var acceptance_private_member_marker: T\n"
			"\n"
			"func acceptance_private_method_marker(value: T) -> T:\n"
			"\tacceptance_private_member_marker = value\n"
			"\tdeclared_signal.emit(value)\n"
			"\treturn acceptance_private_member_marker\n");
	p_project.write_file(
			"acceptance_derived.fs",
			"class_name AcceptanceDerived\n"
			"extends \"res://acceptance_base.fs\"[int]\n"
			"uses AcceptanceTrait[int]\n"
			"\n"
			"var declared_value: int = 0\n"
			"var scene_value: int = 0\n"
			"\n"
			"func declared_handler(value: int) -> void:\n"
			"\tdeclared_value = value\n"
			"\n"
			"func scene_handler(value: int) -> void:\n"
			"\tscene_value = value\n"
			"\n"
			"func trait_value(value: int) -> int:\n"
			"\treturn acceptance_private_member_marker + value\n"
			"\n"
			"@rpc(\"any_peer\", \"call_local\")\n"
			"func rpc_surface(value: int) -> int:\n"
			"\treturn value + 3\n"
			"\n"
			"@keep_name\n"
			"func annotated_dynamic_target(value: int) -> int:\n"
			"\treturn value + 4\n"
			"\n"
			"func ruled_dynamic_target(value: int) -> int:\n"
			"\treturn value + 5\n");
	p_project.write_file(
			"emitter.fs",
			"extends Node\n"
			"\n"
			"signal scene_surface_signal(value: int)\n"
			"\n"
			"func fire() -> void:\n"
			"\tscene_surface_signal.emit(7)\n");
	p_project.write_file(
			"reflector.fs",
			"class_name AcceptanceReflector\n"
			"extends RefCounted\n"
			"\n"
			"func reflection_surface_marker() -> int:\n"
			"\treturn 1\n"
			"\n"
			"func reflection_count() -> int:\n"
			"\tvar count := 0\n"
			"\tfor method in get_method_list():\n"
			"\t\tif str(method.name).begins_with(\"reflection_\"):\n"
			"\t\t\tcount += 1\n"
			"\treturn count\n");
	p_project.write_file(
			"payload.fs",
			"class_name AcceptancePayload\n"
			"extends Resource\n"
			"\n"
			"@export var resource_export_value: int = 0\n");
	p_project.write_file(
			"payload.tres",
			"[gd_resource type=\"Resource\" load_steps=2 format=3]\n"
			"\n"
			"[ext_resource type=\"Script\" path=\"res://payload.fs\" id=\"1_payload\"]\n"
			"\n"
			"[resource]\n"
			"script = ExtResource(\"1_payload\")\n"
			"resource_export_value = 11\n");
	p_project.write_file(
			"main.fs",
			"extends Node\n"
			"\n"
			"@onready var emitter: Node = $Emitter\n"
			"@onready var receiver: AcceptanceDerived = $Receiver\n"
			"\n"
			"func _ready() -> void:\n"
			"\treceiver.declared_signal.connect(receiver.declared_handler)\n"
			"\tvar direct_value: int = receiver.acceptance_private_method_marker(40)\n"
			"\temitter.fire()\n"
			"\tvar payload := load(\"res://payload.tres\") as AcceptancePayload\n"
			"\tvar widened: AcceptanceTrait[int] = receiver\n"
			"\tvar suffixes := PackedStringArray([\"dynamic_target\"])\n"
			"\tvar suffix := suffixes[0]\n"
			"\tvar annotated_value: int = receiver.call(\"annotated_\" + suffix, 1)\n"
			"\tvar ruled_value: int = receiver.call(\"ruled_\" + suffix, 1)\n"
			"\tvar reflection_value := AcceptanceReflector.new().reflection_count()\n"
			"\tvar rpc_value := receiver.rpc_surface(39)\n"
			"\tvar rpc_config_count: int = int(receiver.get_script().get_rpc_config().size())\n"
			"\tvar trait_result := widened.trait_value(2)\n"
			"\tprint(\"NAME_MANGLER_ACCEPTANCE|declared=%d|scene=%d|scene_export=%d|resource_export=%d|rpc=%d|rpc_config=%d|trait=%d|reflection=%d|annotated=%d|rule=%d|ready=%d\" % [\n"
			"\t\t\treceiver.declared_value,\n"
			"\t\t\treceiver.scene_value,\n"
			"\t\t\treceiver.scene_export_value,\n"
			"\t\t\tpayload.resource_export_value,\n"
			"\t\t\trpc_value,\n"
			"\t\t\trpc_config_count,\n"
			"\t\t\ttrait_result,\n"
			"\t\t\treflection_value,\n"
			"\t\t\tannotated_value,\n"
			"\t\t\truled_value,\n"
			"\t\t\t1 if direct_value == 40 else 0])\n"
			"\tget_tree().quit(0)\n");
	p_project.write_file(
			"main.tscn",
			"[gd_scene load_steps=4 format=3]\n"
			"\n"
			"[ext_resource type=\"Script\" path=\"res://main.fs\" id=\"1_main\"]\n"
			"[ext_resource type=\"Script\" path=\"res://emitter.fs\" id=\"2_emitter\"]\n"
			"[ext_resource type=\"Script\" path=\"res://acceptance_derived.fs\" id=\"3_receiver\"]\n"
			"\n"
			"[node name=\"Main\" type=\"Node\"]\n"
			"script = ExtResource(\"1_main\")\n"
			"\n"
			"[node name=\"Emitter\" type=\"Node\" parent=\".\"]\n"
			"script = ExtResource(\"2_emitter\")\n"
			"\n"
			"[node name=\"Receiver\" type=\"Node\" parent=\".\"]\n"
			"script = ExtResource(\"3_receiver\")\n"
			"scene_export_value = 23\n"
			"\n"
			"[connection signal=\"scene_surface_signal\" from=\"Emitter\" to=\"Receiver\" method=\"scene_handler\"]\n");
}

static void name_mangler_acceptance_write_unsafe_project(
		TemporaryProjectTree &p_project) {
	p_project.write_file(
			"project.foundry",
			"[application]\n"
			"config/name=\"Name Mangler Unsafe Dispatch Acceptance\"\n"
			"run/main_scene=\"res://main.tscn\"\n"
			"\n"
			"[rendering]\n"
			"renderer/rendering_method=\"gl_compatibility\"\n");
	p_project.write_file(
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
			"script_name_mangling_keep_rules=\"\"\n"
			"\n"
			"[preset.0.options]\n"
			"custom_template/debug=\"\"\n"
			"custom_template/release=\"\"\n");
	p_project.write_file(
			"main.fs",
			"extends Node\n"
			"\n"
			"func unsafe_dynamic_target() -> void:\n"
			"\tprint(\"UNSAFE_DYNAMIC_TARGET_EXECUTED\")\n"
			"\n"
			"func _exit_after_probe() -> void:\n"
			"\tget_tree().quit(0)\n"
			"\n"
			"func _ready() -> void:\n"
			"\tget_tree().process_frame.connect(_exit_after_probe, CONNECT_ONE_SHOT)\n"
			"\tvar parts := PackedStringArray([\"unsafe_dynamic_\", \"target\"])\n"
			"\tvar target := parts[0] + parts[1]\n"
			"\tcall(target)\n");
	p_project.write_file(
			"main.tscn",
			"[gd_scene load_steps=2 format=3]\n"
			"\n"
			"[ext_resource type=\"Script\" path=\"res://main.fs\" id=\"1_main\"]\n"
			"\n"
			"[node name=\"Main\" type=\"Node\"]\n"
			"script = ExtResource(\"1_main\")\n");
}

TEST_CASE("[FoundryScript][NameManglerAcceptance][Parity] Real exports preserve semantics strip names and repeat deterministically") {
	TemporaryProjectTree project(
			"fs_name_mangler_acceptance_safe_" +
			itos(OS::get_singleton()->get_ticks_usec()));
	name_mangler_acceptance_write_safe_project(project);

	const String unmangled_pack =
			project.root.path_join("unmangled.pck");
	const String mangled_a_pack =
			project.root.path_join("mangled_a.pck");
	const String mangled_b_pack =
			project.root.path_join("mangled_b.pck");
	const NameManglerPackProcessResult unmangled_export =
			name_mangler_acceptance_export(
					project.root, "Unmangled", unmangled_pack);
	const NameManglerPackProcessResult mangled_a_export =
			name_mangler_acceptance_export(
					project.root, "Mangled", mangled_a_pack);
	const NameManglerPackProcessResult mangled_b_export =
			name_mangler_acceptance_export(
					project.root, "Mangled", mangled_b_pack);
	INFO("Unmangled export:\n", unmangled_export.output);
	INFO("Mangled export A:\n", mangled_a_export.output);
	INFO("Mangled export B:\n", mangled_b_export.output);
	REQUIRE_EQ(unmangled_export.error, OK);
	REQUIRE_EQ(mangled_a_export.error, OK);
	REQUIRE_EQ(mangled_b_export.error, OK);
	REQUIRE_EQ(unmangled_export.exit_code, 0);
	REQUIRE_EQ(mangled_a_export.exit_code, 0);
	REQUIRE_EQ(mangled_b_export.exit_code, 0);
	if (unmangled_export.error != OK ||
			mangled_a_export.error != OK ||
			mangled_b_export.error != OK ||
			unmangled_export.exit_code != 0 ||
			mangled_a_export.exit_code != 0 ||
			mangled_b_export.exit_code != 0) {
		return;
	}

	const NameManglerPackProcessResult unmangled_runtime =
			name_mangler_acceptance_run(
					unmangled_pack,
					project.root.path_join("runtime_unmangled"));
	const NameManglerPackProcessResult mangled_runtime =
			name_mangler_acceptance_run(
					mangled_a_pack,
					project.root.path_join("runtime_mangled"));
	INFO("Unmangled runtime:\n", unmangled_runtime.output);
	INFO("Mangled runtime:\n", mangled_runtime.output);
	REQUIRE_EQ(unmangled_runtime.error, OK);
	REQUIRE_EQ(mangled_runtime.error, OK);
	REQUIRE_EQ(unmangled_runtime.exit_code, 0);
	REQUIRE_EQ(mangled_runtime.exit_code, 0);
	if (unmangled_runtime.error != OK ||
			mangled_runtime.error != OK ||
			unmangled_runtime.exit_code != 0 ||
			mangled_runtime.exit_code != 0) {
		return;
	}
	const String expected =
			"NAME_MANGLER_ACCEPTANCE|declared=40|scene=7|scene_export=23|resource_export=11|rpc=42|rpc_config=1|trait=42|reflection=2|annotated=5|rule=6|ready=1";
	const String unmangled_transcript =
			name_mangler_acceptance_transcript(
					unmangled_runtime.output);
	const String mangled_transcript =
			name_mangler_acceptance_transcript(
					mangled_runtime.output);
	REQUIRE_EQ(unmangled_transcript, expected);
	REQUIRE_EQ(mangled_transcript, expected);
	CHECK_EQ(mangled_transcript, unmangled_transcript);

	const RBMap<String, Vector<uint8_t>> unmangled_scripts =
			name_mangler_acceptance_read_scripts(unmangled_pack);
	const RBMap<String, Vector<uint8_t>> mangled_a_scripts =
			name_mangler_acceptance_read_scripts(mangled_a_pack);
	const RBMap<String, Vector<uint8_t>> mangled_b_scripts =
			name_mangler_acceptance_read_scripts(mangled_b_pack);
	name_mangler_acceptance_require_script_inventory(
			unmangled_scripts, "Unmangled");
	name_mangler_acceptance_require_script_inventory(
			mangled_a_scripts, "Mangled A");
	name_mangler_acceptance_require_script_inventory(
			mangled_b_scripts, "Mangled B");
	for (const KeyValue<String, Vector<uint8_t>> &entry :
			mangled_a_scripts) {
		const bool has_path = mangled_b_scripts.has(entry.key);
		REQUIRE(has_path);
		if (!has_path) {
			continue;
		}
		CHECK_EQ(mangled_b_scripts[entry.key], entry.value);
	}

	const String private_member =
			"acceptance_private_member_marker";
	const String private_method =
			"acceptance_private_method_marker";
	REQUIRE(name_mangler_acceptance_contains(
			unmangled_scripts, private_member));
	REQUIRE(name_mangler_acceptance_contains(
			unmangled_scripts, private_method));
	CHECK_FALSE(name_mangler_acceptance_contains(
			mangled_a_scripts, private_member));
	CHECK_FALSE(name_mangler_acceptance_contains(
			mangled_a_scripts, private_method));

	const struct {
		const char *path;
		const char *marker;
	} kept[] = {
		{ "res://emitter.fsb", "scene_surface_signal" },
		{ "res://acceptance_derived.fsb", "scene_handler" },
		{ "res://acceptance_base.fsb", "scene_export_value" },
		{ "res://payload.fsb", "resource_export_value" },
		{ "res://acceptance_derived.fsb", "rpc_surface" },
		{ "res://main.fsb", "_ready" },
		{ "res://reflector.fsb", "reflection_surface_marker" },
		{ "res://acceptance_derived.fsb",
				"annotated_dynamic_target" },
		{ "res://acceptance_derived.fsb",
				"ruled_dynamic_target" },
	};
	for (const auto &expectation : kept) {
		CAPTURE(expectation.path);
		CAPTURE(expectation.marker);
		const bool has_path =
				mangled_a_scripts.has(expectation.path);
		REQUIRE(has_path);
		if (!has_path) {
			continue;
		}
		CHECK(bytecode_buffer_contains(
				mangled_a_scripts[expectation.path],
				expectation.marker));
	}
}

TEST_CASE("[FoundryScript][NameManglerAcceptance][DynamicDispatch] Unescaped computed calls fail with the reconstructed target") {
	TemporaryProjectTree project(
			"fs_name_mangler_acceptance_unsafe_" +
			itos(OS::get_singleton()->get_ticks_usec()));
	name_mangler_acceptance_write_unsafe_project(project);
	const String pack_path = project.root.path_join("unsafe.pck");
	const NameManglerPackProcessResult export_result =
			name_mangler_acceptance_export(
					project.root, "Mangled", pack_path);
	INFO("Unsafe export:\n", export_result.output);
	REQUIRE_EQ(export_result.error, OK);
	REQUIRE_EQ(export_result.exit_code, 0);
	if (export_result.error != OK || export_result.exit_code != 0) {
		return;
	}

	const NameManglerPackProcessResult runtime_result =
			name_mangler_acceptance_run(
					pack_path,
					project.root.path_join("runtime"));
	INFO("Unsafe runtime:\n", runtime_result.output);
	REQUIRE_EQ(runtime_result.error, OK);
	REQUIRE_EQ(runtime_result.exit_code, 0);
	if (runtime_result.error != OK ||
			runtime_result.exit_code != 0) {
		return;
	}
	const String missing_call_diagnostic =
			name_mangler_acceptance_missing_call_diagnostic(
					runtime_result.output,
					"unsafe_dynamic_target");
	CHECK_FALSE(missing_call_diagnostic.is_empty());
	CHECK_FALSE(runtime_result.output.contains(
			"UNSAFE_DYNAMIC_TARGET_EXECUTED"));

	const RBMap<String, Vector<uint8_t>> scripts =
			name_mangler_acceptance_read_scripts(pack_path);
	REQUIRE(scripts.has("res://main.fsb"));
	if (!scripts.has("res://main.fsb")) {
		return;
	}
	CHECK_FALSE(bytecode_buffer_contains(
			scripts["res://main.fsb"],
			"unsafe_dynamic_target"));
}

} // namespace FSTests

#endif // TOOLS_ENABLED
