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
#include "modules/foundry_script/fs_analyzer.h"
#include "modules/foundry_script/fs_builtin_sources.h"
#include "modules/foundry_script/fs_cache.h"
#include "modules/foundry_script/fs_compiler.h"
#include "modules/foundry_script/fs_parser.h"

#include "core/object/script_language.h"
#include "core/variant/variant.h"

#include "tests/test_macros.h"
#include "tests/test_tools.h"

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

// `JsonNode` is a global builtin, so a plain script can name it with no import. This helper
// compiles such a probe script that funnels `JsonNode.of()` through a `match` and reports back
// a plain string, so the test can assert on `JsonNode.of()`'s behavior without reaching into the
// enum instance's internal representation.
static Ref<FoundryScript> compile_json_of_probe() {
	static int unique_index = 0;
	const String path = vformat("user://test_json_of_probe_%d.fs", unique_index++);

	const char *source =
			"class_name JsonOfProbe extends RefCounted\n"
			"\n"
			"static func describe(value: Variant) -> String:\n"
			"\tvar node := JsonNode.of(value)\n"
			"\tmatch node:\n"
			"\t\tJsonNode.Null:\n"
			"\t\t\treturn \"Null\"\n"
			"\t\tJsonNode.Bool(var payload):\n"
			"\t\t\treturn \"Bool:%s\" % payload\n"
			"\t\tJsonNode.Int(var payload):\n"
			"\t\t\treturn \"Int:%s\" % payload\n"
			"\t\tJsonNode.Float(var payload):\n"
			"\t\t\treturn \"Float:%s\" % payload\n"
			"\t\tJsonNode.Str(var payload):\n"
			"\t\t\treturn \"Str:%s\" % payload\n"
			"\t\tJsonNode.Array(var payload):\n"
			"\t\t\treturn \"Array:%d\" % payload.size()\n"
			"\t\tJsonNode.Object(var payload):\n"
			"\t\t\treturn \"Object:%d\" % payload.size()\n"
			"\treturn \"Unknown\"\n";

	Ref<FoundryScript> script;
	script.instantiate();
	script->set_path(path);
	script->set_source_code(source);

	FSParser parser;
	Error error = parser.parse(source, script->get_path(), false);
	REQUIRE(error == OK);

	FSAnalyzer analyzer(&parser);
	error = analyzer.analyze();
	REQUIRE(error == OK);

	FSCompiler compiler;
	error = compiler.compile(&parser, script.ptr(), false);
	REQUIRE(error == OK);

	error = script->reload();
	REQUIRE(error == OK);

	return script;
}

static String describe_json_of(const Ref<FoundryScript> &p_probe, const Variant &p_value) {
	// `FoundryScript::callp` overrides `Object::callp` as protected, so dispatch through the
	// `Object` base (as the bytecode static-method tests do) rather than the derived pointer.
	Object *probe_object = p_probe.ptr();
	const Variant *args[1] = { &p_value };
	Callable::CallError call_error;
	const Variant result = probe_object->callp(SNAME("describe"), args, 1, call_error);
	REQUIRE(call_error.error == Callable::CallError::CALL_OK);
	return result;
}

TEST_CASE("[FSBuiltinTypes] JsonNode.of() wraps a plain Variant tree") {
	FSLanguage::get_singleton()->init();
	const Ref<FoundryScript> probe = compile_json_of_probe();

	CHECK_EQ(describe_json_of(probe, Variant()), "Null");
	CHECK_EQ(describe_json_of(probe, Variant(true)), "Bool:true");
	CHECK_EQ(describe_json_of(probe, Variant(int64_t(7))), "Int:7");
	CHECK_EQ(describe_json_of(probe, Variant(1.5)), "Float:1.5");
	CHECK_EQ(describe_json_of(probe, Variant(String("hello"))), "Str:hello");

	Array nested_array;
	nested_array.push_back(1);
	nested_array.push_back("two");
	CHECK_EQ(describe_json_of(probe, Variant(nested_array)), "Array:2");

	Dictionary nested_dictionary;
	nested_dictionary["a"] = 1;
	nested_dictionary["b"] = 2;
	nested_dictionary["c"] = 3;
	CHECK_EQ(describe_json_of(probe, Variant(nested_dictionary)), "Object:3");
}

TEST_CASE("[FSBuiltinTypes] JsonNode.of() push_errors and returns Null for an unsupported type") {
	FSLanguage::get_singleton()->init();
	const Ref<FoundryScript> probe = compile_json_of_probe();

	ErrorDetector detector;
	ERR_PRINT_OFF;
	const String description = describe_json_of(probe, Variant(Vector2(1, 2)));
	ERR_PRINT_ON;

	CHECK_EQ(description, "Null");
	CHECK(detector.has_error);
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
