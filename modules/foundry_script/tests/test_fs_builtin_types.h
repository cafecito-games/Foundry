/**************************************************************************/
/*  test_fs_builtin_types.h                                               */
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

#include "modules/foundry_script/foundry_script.h"
#include "modules/foundry_script/fs_builtin_sources.h"
#include "modules/foundry_script/fs_cache.h"
#include "modules/foundry_script/fs_parser.h"

#include "core/object/script_language.h"

#include "tests/test_macros.h"

namespace TestFSBuiltinTypes {

TEST_CASE("[FSBuiltinTypes] Every builtin source loads, analyzes, and compiles cleanly") {
	// A broken builtin must fail here rather than as a confusing diagnostic in an unrelated
	// user script that merely mentions one of these types.
	FSLanguage::get_singleton()->init();

	List<String> builtin_paths;
	FSBuiltinSources::get_registered_paths(&builtin_paths);
	CHECK_FALSE(builtin_paths.is_empty());

	for (const String &builtin_path : builtin_paths) {
		INFO("builtin source: ", builtin_path);

		Error err = OK;
		const Ref<FoundryScript> script = FSCache::get_full_script(builtin_path, err);
		CHECK_EQ(err, OK);
		CHECK(script.is_valid());
		if (script.is_valid()) {
			CHECK(script->is_valid());
		}
	}
}

TEST_CASE("[FSBuiltinTypes] JsonNode case ordinals are the wire contract") {
	// The native encoder lowers a JsonNode by its case tag, so reordering these cases silently
	// changes what every encoded document means.
	String source;
	CHECK(FSBuiltinSources::get_source("foundry://builtin/json_node.fs", source));

	FSParser parser;
	CHECK_EQ(parser.parse(source, "foundry://builtin/json_node.fs", false), OK);

	const FSParser::ClassNode *declared_class = parser.get_tree();
	if (declared_class == nullptr || !declared_class->is_enum_file || declared_class->enum_file_decl == nullptr) {
		FAIL("foundry://builtin/json_node.fs does not declare a file-level enum.");
		return;
	}

	const FSParser::EnumNode *json_node = declared_class->enum_file_decl;
	const Vector<String> expected_cases = { "Null", "Bool", "Int", "Float", "Str", "Array", "Object" };
	CHECK_EQ(json_node->values.size(), expected_cases.size());
	if (json_node->values.size() != expected_cases.size()) {
		return;
	}
	for (int i = 0; i < expected_cases.size(); i++) {
		const FSParser::EnumNode::Value &value = json_node->values[i];
		CHECK(value.identifier != nullptr);
		if (value.identifier == nullptr) {
			continue;
		}
		CHECK_EQ(String(value.identifier->name), expected_cases[i]);
		CHECK_EQ(value.index, i);
	}
}

TEST_CASE("[FSBuiltinTypes] Builtin types are registered as global classes") {
	CHECK(ScriptServer::is_global_class("JsonNode"));
	CHECK(ScriptServer::is_global_class_enum("JsonNode"));
	CHECK_EQ(ScriptServer::get_global_class_path("JsonNode"), "foundry://builtin/json_node.fs");
	CHECK_EQ(String(ScriptServer::get_global_class_language("JsonNode")), "FoundryScript");

	CHECK(ScriptServer::is_global_class("JsonSerializable"));
	CHECK(ScriptServer::is_global_class_trait("JsonSerializable"));

	CHECK(ScriptServer::is_global_class("JsonDecodeError"));
	CHECK_FALSE(ScriptServer::is_global_class_trait("JsonDecodeError"));
	CHECK_EQ(String(ScriptServer::get_global_class_native_base("JsonDecodeError")), "RefCounted");

	CHECK(ScriptServer::is_global_class("JsonResult"));
	CHECK_EQ(String(ScriptServer::get_global_class_native_base("JsonResult")), "RefCounted");
}

TEST_CASE("[FSBuiltinTypes] Builtin global classes survive a project reload") {
	// Loading a project rebuilds the global class table from that project's class cache. Types
	// that ship in the binary belong to the language, not to the project, so they must remain.
	ScriptServer::global_classes_clear();
	CHECK(ScriptServer::is_global_class("JsonNode"));
	CHECK(ScriptServer::is_global_class("JsonSerializable"));

	ScriptServer::reload_global_classes_from_project();
	CHECK(ScriptServer::is_global_class("JsonNode"));
	CHECK_EQ(ScriptServer::get_global_class_path("JsonNode"), "foundry://builtin/json_node.fs");
	CHECK(ScriptServer::is_global_class("JsonSerializable"));
	CHECK(ScriptServer::is_builtin_global_class("JsonResult"));
	CHECK_FALSE(ScriptServer::is_builtin_global_class("RefCounted"));
}

TEST_CASE("[FSBuiltinTypes] A project class cannot take over a builtin type name") {
	// A stale project class cache from before these types existed must not redefine what
	// `JsonNode` means, and must not be silently dropped from that project's cache either.
	ERR_PRINT_OFF;
	ScriptServer::add_global_class(SNAME("JsonNode"), SNAME("RefCounted"), SNAME("FoundryScript"),
			"res://project_json_node.fs", false, false, false, false);
	ERR_PRINT_ON;

	CHECK_EQ(ScriptServer::get_global_class_path("JsonNode"), "foundry://builtin/json_node.fs");
	CHECK(ScriptServer::is_global_class_enum("JsonNode"));
	CHECK_EQ(String(ScriptServer::get_global_class_base("JsonNode")), "");
}

TEST_CASE("[FSBuiltinTypes] A project scan cannot remove a builtin global class") {
	// The editor reconciles the global class table against the files it finds on disk. A builtin
	// type has no file, so it must not be reconciled away.
	ScriptServer::remove_global_class(SNAME("JsonNode"));
	CHECK(ScriptServer::is_global_class("JsonNode"));

	ScriptServer::remove_global_class_by_path("foundry://builtin/json_serializable.fs");
	CHECK(ScriptServer::is_global_class("JsonSerializable"));
}

} // namespace TestFSBuiltinTypes
