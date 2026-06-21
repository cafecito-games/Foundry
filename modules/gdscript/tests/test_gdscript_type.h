/**************************************************************************/
/*  test_gdscript_type.h                                                  */
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

#include "modules/gdscript/gdscript_analyzer.h"
#include "modules/gdscript/gdscript.h"
#include "modules/gdscript/gdscript_type.h"
#include "modules/gdscript/editor/gdscript_docgen.h"

#include "core/config/project_settings.h"

#include "tests/test_macros.h"

namespace GDScriptTests {

static GDScriptParser::DataType make_builtin_type(Variant::Type p_type) {
	GDScriptParser::DataType type;
	type.kind = GDScriptParser::DataType::BUILTIN;
	type.type_source = GDScriptParser::DataType::ANNOTATED_EXPLICIT;
	type.builtin_type = p_type;
	return type;
}

static GDScriptParser::DataType make_variant_type() {
	GDScriptParser::DataType type;
	type.kind = GDScriptParser::DataType::VARIANT;
	type.type_source = GDScriptParser::DataType::ANNOTATED_EXPLICIT;
	return type;
}

static GDScriptParser::DataType make_native_type(const StringName &p_native_type) {
	GDScriptParser::DataType type;
	type.kind = GDScriptParser::DataType::NATIVE;
	type.type_source = GDScriptParser::DataType::ANNOTATED_EXPLICIT;
	type.builtin_type = Variant::OBJECT;
	type.native_type = p_native_type;
	return type;
}

static GDScriptParser::DataType make_signature_builtin_type(Variant::Type p_builtin_type, Variant::Type p_argument_type, Variant::Type p_return_type) {
	GDScriptParser::DataType type = make_builtin_type(p_builtin_type);
	type.has_method_signature = true;
	type.has_explicit_method_signature = true;
	type.method_parameter_types.push_back(make_builtin_type(p_argument_type));
	type.method_return_type.push_back(make_builtin_type(p_return_type));
	type.method_info.return_val.type = p_return_type;
	type.method_info.arguments.push_back(PropertyInfo(p_argument_type, "arg"));
	return type;
}

static Error analyze_source(const String &p_source, bool p_strict_null_checks = false, bool p_strict_dynamic_checks = false) {
	GDScriptParser parser;
	Error err = parser.parse(p_source, "user://test.gd", false);
	if (err != OK) {
		return err;
	}

	GDScriptAnalyzer analyzer(&parser);
	analyzer.set_strict_null_checks(p_strict_null_checks);
	analyzer.set_strict_dynamic_checks(p_strict_dynamic_checks);
	return analyzer.analyze();
}

static Error analyze_source_with_project_settings(const String &p_source) {
	GDScriptParser parser;
	Error err = parser.parse(p_source, "user://test.gd", false);
	if (err != OK) {
		return err;
	}

	GDScriptAnalyzer analyzer(&parser);
	return analyzer.analyze();
}

TEST_CASE("[Modules][GDScript] Type compatibility marks dynamic source as runtime checked") {
	const GDScriptParser::DataType target = make_builtin_type(Variant::INT);
	const GDScriptParser::DataType source = make_variant_type();

	const GDScriptTypeCompatibility::Result result = GDScriptTypeCompatibility::check(target, source);

	CHECK(result.compatible);
	CHECK(result.requires_runtime_check);
	CHECK_FALSE(result.uses_implicit_conversion);
}

TEST_CASE("[Modules][GDScript] Type compatibility can reject dynamic source for strict checks") {
	const GDScriptParser::DataType target = make_builtin_type(Variant::INT);
	const GDScriptParser::DataType source = make_variant_type();
	GDScriptTypeCompatibility::Options options;
	options.strict_dynamic = true;

	const GDScriptTypeCompatibility::Result result = GDScriptTypeCompatibility::check(target, source, options);

	CHECK_FALSE(result.compatible);
	CHECK(result.requires_runtime_check);
	CHECK_FALSE(result.uses_implicit_conversion);
}

TEST_CASE("[Modules][GDScript] Type compatibility can reject null source for strict checks") {
	const GDScriptParser::DataType target = make_native_type(SNAME("Node"));
	const GDScriptParser::DataType source = make_builtin_type(Variant::NIL);
	GDScriptParser::DataType nullable_target = target;
	nullable_target.is_nullable = true;
	GDScriptTypeCompatibility::Options options;
	options.strict_null = true;

	const GDScriptTypeCompatibility::Result legacy_result = GDScriptTypeCompatibility::check(target, source);
	const GDScriptTypeCompatibility::Result strict_result = GDScriptTypeCompatibility::check(target, source, options);
	const GDScriptTypeCompatibility::Result nullable_result = GDScriptTypeCompatibility::check(nullable_target, source, options);

	CHECK(legacy_result.compatible);
	CHECK_FALSE(strict_result.compatible);
	CHECK(nullable_result.compatible);
}

TEST_CASE("[Modules][GDScript] Type compatibility can reject nullable source for strict checks") {
	const GDScriptParser::DataType target = make_native_type(SNAME("Node"));
	GDScriptParser::DataType nullable_source = target;
	nullable_source.is_nullable = true;
	GDScriptParser::DataType nullable_target = target;
	nullable_target.is_nullable = true;
	GDScriptTypeCompatibility::Options options;
	options.strict_null = true;

	const GDScriptTypeCompatibility::Result legacy_result = GDScriptTypeCompatibility::check(target, nullable_source);
	const GDScriptTypeCompatibility::Result strict_result = GDScriptTypeCompatibility::check(target, nullable_source, options);
	const GDScriptTypeCompatibility::Result nullable_result = GDScriptTypeCompatibility::check(nullable_target, nullable_source, options);

	CHECK(legacy_result.compatible);
	CHECK_FALSE(strict_result.compatible);
	CHECK(nullable_result.compatible);
}

TEST_CASE("[Modules][GDScript] Type compatibility reports implicit builtin conversion") {
	const GDScriptParser::DataType target = make_builtin_type(Variant::FLOAT);
	const GDScriptParser::DataType source = make_builtin_type(Variant::INT);
	GDScriptTypeCompatibility::Options options;
	options.allow_implicit_conversion = true;

	const GDScriptTypeCompatibility::Result result = GDScriptTypeCompatibility::check(target, source, options);

	CHECK(result.compatible);
	CHECK_FALSE(result.requires_runtime_check);
	CHECK(result.uses_implicit_conversion);
}

TEST_CASE("[Modules][GDScript] Type compatibility preserves non-builtin implicit conversion") {
	const GDScriptParser::DataType rid_target = make_builtin_type(Variant::RID);
	GDScriptParser::DataType native_source;
	native_source.kind = GDScriptParser::DataType::NATIVE;
	native_source.type_source = GDScriptParser::DataType::ANNOTATED_EXPLICIT;
	native_source.builtin_type = Variant::OBJECT;
	native_source.native_type = SNAME("Node");

	GDScriptParser::DataType dictionary_target = make_builtin_type(Variant::DICTIONARY);
	GDScriptParser::DataType enum_metatype_source;
	enum_metatype_source.kind = GDScriptParser::DataType::ENUM;
	enum_metatype_source.type_source = GDScriptParser::DataType::ANNOTATED_EXPLICIT;
	enum_metatype_source.builtin_type = Variant::DICTIONARY;
	enum_metatype_source.is_meta_type = true;

	GDScriptTypeCompatibility::Options options;
	options.allow_implicit_conversion = true;

	const GDScriptTypeCompatibility::Result rid_result = GDScriptTypeCompatibility::check(rid_target, native_source, options);
	const GDScriptTypeCompatibility::Result enum_result = GDScriptTypeCompatibility::check(dictionary_target, enum_metatype_source, options);

	CHECK(rid_result.compatible);
	CHECK(rid_result.uses_implicit_conversion);
	CHECK(enum_result.compatible);
	CHECK(enum_result.uses_implicit_conversion);
}

TEST_CASE("[Modules][GDScript] Type compatibility keeps typed arrays invariant") {
	GDScriptParser::DataType target = make_builtin_type(Variant::ARRAY);
	target.set_container_element_type(0, make_builtin_type(Variant::INT));
	GDScriptParser::DataType same_source = make_builtin_type(Variant::ARRAY);
	same_source.set_container_element_type(0, make_builtin_type(Variant::INT));
	GDScriptParser::DataType different_source = make_builtin_type(Variant::ARRAY);
	different_source.set_container_element_type(0, make_builtin_type(Variant::FLOAT));

	CHECK(GDScriptTypeCompatibility::check(target, same_source).compatible);
	CHECK_FALSE(GDScriptTypeCompatibility::check(target, different_source).compatible);
}

TEST_CASE("[Modules][GDScript] Type compatibility checks callable and signal signatures") {
	const GDScriptParser::DataType callable_target = make_signature_builtin_type(Variant::CALLABLE, Variant::INT, Variant::BOOL);
	const GDScriptParser::DataType callable_source = make_signature_builtin_type(Variant::CALLABLE, Variant::INT, Variant::BOOL);
	const GDScriptParser::DataType callable_bad_arg = make_signature_builtin_type(Variant::CALLABLE, Variant::FLOAT, Variant::BOOL);
	const GDScriptParser::DataType callable_bad_return = make_signature_builtin_type(Variant::CALLABLE, Variant::INT, Variant::STRING);
	const GDScriptParser::DataType untyped_callable = make_builtin_type(Variant::CALLABLE);

	const GDScriptTypeCompatibility::Result untyped_source_result = GDScriptTypeCompatibility::check(callable_target, untyped_callable);

	CHECK(GDScriptTypeCompatibility::check(callable_target, callable_source).compatible);
	CHECK_FALSE(GDScriptTypeCompatibility::check(callable_target, callable_bad_arg).compatible);
	CHECK_FALSE(GDScriptTypeCompatibility::check(callable_target, callable_bad_return).compatible);
	CHECK(untyped_source_result.compatible);
	CHECK(untyped_source_result.requires_runtime_check);

	const GDScriptParser::DataType signal_target = make_signature_builtin_type(Variant::SIGNAL, Variant::INT, Variant::NIL);
	const GDScriptParser::DataType signal_source = make_signature_builtin_type(Variant::SIGNAL, Variant::INT, Variant::NIL);
	const GDScriptParser::DataType signal_bad_arg = make_signature_builtin_type(Variant::SIGNAL, Variant::STRING, Variant::NIL);

	CHECK(GDScriptTypeCompatibility::check(signal_target, signal_source).compatible);
	CHECK_FALSE(GDScriptTypeCompatibility::check(signal_target, signal_bad_arg).compatible);

	GDScriptParser::DataType callable_array_target = make_builtin_type(Variant::ARRAY);
	callable_array_target.set_container_element_type(0, callable_target);
	GDScriptParser::DataType callable_array_source = make_builtin_type(Variant::ARRAY);
	callable_array_source.set_container_element_type(0, callable_source);
	GDScriptParser::DataType callable_array_bad_source = make_builtin_type(Variant::ARRAY);
	callable_array_bad_source.set_container_element_type(0, callable_bad_arg);

	CHECK(GDScriptTypeCompatibility::check(callable_array_target, callable_array_source).compatible);
	CHECK_FALSE(GDScriptTypeCompatibility::check(callable_array_target, callable_array_bad_source).compatible);
}

TEST_CASE("[Modules][GDScript] Parser resolves nullable type annotations") {
	GDScriptParser parser;
	Error err = parser.parse("var maybe_node: Node?\nvar maybe_nodes: Array[Node?]\n", "user://nullable_type.gd", false);
	REQUIRE(err == OK);

	GDScriptAnalyzer analyzer(&parser);
	err = analyzer.analyze();
	REQUIRE(err == OK);

	const GDScriptParser::ClassNode *root = parser.get_tree();
	REQUIRE(root != nullptr);
	REQUIRE(root->members.size() == 2);
	REQUIRE(root->members[0].type == GDScriptParser::ClassNode::Member::VARIABLE);
	REQUIRE(root->members[1].type == GDScriptParser::ClassNode::Member::VARIABLE);

	const GDScriptParser::VariableNode *variable = root->members[0].variable;
	REQUIRE(variable != nullptr);
	const GDScriptParser::DataType variable_type = variable->get_datatype();

	CHECK(variable_type.is_nullable);
	CHECK(variable_type.to_string() == "Node?");

	const GDScriptParser::VariableNode *array_variable = root->members[1].variable;
	REQUIRE(array_variable != nullptr);
	const GDScriptParser::DataType array_type = array_variable->get_datatype();

	CHECK(array_type.has_container_element_type(0));
	CHECK(array_type.get_container_element_type(0).is_nullable);
	CHECK(array_type.to_string() == "Array[Node?]");
}

TEST_CASE("[Modules][GDScript] Parser resolves callable and signal signature annotations") {
	GDScriptParser parser;
	Error err = parser.parse("var callback: Callable[[int], bool]\nvar event: Signal[[String]]\nvar maybe_callback: Callable[[Node?], void]\nvar maybe_event: Signal[[Node?]]\n", "user://signature_type.gd", false);
	CHECK(err == OK);
	if (err != OK) {
		return;
	}

	GDScriptAnalyzer analyzer(&parser);
	err = analyzer.analyze();
	CHECK(err == OK);
	if (err != OK) {
		return;
	}

	const GDScriptParser::ClassNode *root = parser.get_tree();
	CHECK(root != nullptr);
	if (root == nullptr) {
		return;
	}
	CHECK(root->members.size() == 4);
	if (root->members.size() != 4) {
		return;
	}
	CHECK(root->members[0].type == GDScriptParser::ClassNode::Member::VARIABLE);
	CHECK(root->members[1].type == GDScriptParser::ClassNode::Member::VARIABLE);
	CHECK(root->members[2].type == GDScriptParser::ClassNode::Member::VARIABLE);
	CHECK(root->members[3].type == GDScriptParser::ClassNode::Member::VARIABLE);
	if (root->members[0].type != GDScriptParser::ClassNode::Member::VARIABLE || root->members[1].type != GDScriptParser::ClassNode::Member::VARIABLE || root->members[2].type != GDScriptParser::ClassNode::Member::VARIABLE || root->members[3].type != GDScriptParser::ClassNode::Member::VARIABLE) {
		return;
	}

	const GDScriptParser::DataType callable_type = root->members[0].variable->get_datatype();
	CHECK(callable_type.kind == GDScriptParser::DataType::BUILTIN);
	CHECK(callable_type.builtin_type == Variant::CALLABLE);
	CHECK(callable_type.has_method_signature);
	CHECK(callable_type.has_explicit_method_signature);
	CHECK(callable_type.method_info.arguments.size() == 1);
	if (callable_type.method_info.arguments.size() != 1) {
		return;
	}
	CHECK(callable_type.method_info.arguments[0].type == Variant::INT);
	CHECK(callable_type.method_info.return_val.type == Variant::BOOL);
	CHECK(callable_type.to_string() == "Callable[[int], bool]");

	const GDScriptParser::DataType signal_type = root->members[1].variable->get_datatype();
	CHECK(signal_type.kind == GDScriptParser::DataType::BUILTIN);
	CHECK(signal_type.builtin_type == Variant::SIGNAL);
	CHECK(signal_type.has_method_signature);
	CHECK(signal_type.has_explicit_method_signature);
	CHECK(signal_type.method_info.arguments.size() == 1);
	if (signal_type.method_info.arguments.size() != 1) {
		return;
	}
	CHECK(signal_type.method_info.arguments[0].type == Variant::STRING);
	CHECK(signal_type.method_info.return_val.type == Variant::NIL);
	CHECK(signal_type.to_string() == "Signal[[String]]");

	const GDScriptParser::DataType nullable_callable_type = root->members[2].variable->get_datatype();
	CHECK(nullable_callable_type.to_string() == "Callable[[Node?], void]");
	const GDScriptParser::DataType nullable_signal_type = root->members[3].variable->get_datatype();
	CHECK(nullable_signal_type.to_string() == "Signal[[Node?]]");
}

TEST_CASE("[Modules][GDScript] Docgen displays nested typed container values") {
	GDScriptParser parser;
	Error err = parser.parse("const VALUES: Array[Dictionary[String, int]] = [{ \"score\": 10 }]\nconst GROUPS: Dictionary[String, Array[int]] = { \"scores\": [1] }\n", "user://nested_docgen_type.gd", false);
	REQUIRE(err == OK);

	GDScriptAnalyzer analyzer(&parser);
	err = analyzer.analyze();
	REQUIRE(err == OK);

	const GDScriptParser::ClassNode *root = parser.get_tree();
	REQUIRE(root != nullptr);

	Ref<GDScript> script;
	script.instantiate();
	GDScriptDocGen::generate_docs(script.ptr(), root);

	const Vector<DocData::ClassDoc> docs = script->get_documentation();
	REQUIRE(docs.size() == 1);
	REQUIRE(docs[0].constants.size() == 2);
	CHECK(docs[0].constants[0].value == "Array[Dictionary[String, int]]([Dictionary[String, int]({\"score\": 10})])");
	CHECK(docs[0].constants[1].value == "Dictionary[String, Array[int]]({\"scores\": Array[int]([1])})");
}

TEST_CASE("[Modules][GDScript] Analyzer checks callable and signal signature assignments") {
	const String source_prefix = "func accepts_int(value: int) -> bool:\n\treturn true\n";

	CHECK(analyze_source(source_prefix + "var callback: Callable[[int], bool] = accepts_int\n") == OK);
	CHECK(analyze_source(source_prefix + "var callback: Callable[[float], bool] = accepts_int\n") != OK);
	CHECK(analyze_source(source_prefix + "var callback: Callable[[int], String] = accepts_int\n") != OK);

	CHECK(analyze_source("signal event(value: int)\nvar typed_event: Signal[[int]] = event\n") == OK);
	CHECK(analyze_source("signal event(value: int)\nvar typed_event: Signal[[String]] = event\n") != OK);
}

TEST_CASE("[Modules][GDScript] Analyzer preserves lambda callable signatures") {
	CHECK(analyze_source("func test() -> void:\n\tvar callback: Callable[[int], bool] = func(value: int) -> bool:\n\t\treturn true\n") == OK);
	CHECK(analyze_source("func test() -> void:\n\tvar callback: Callable[[int], bool] = func(value: String) -> bool:\n\t\treturn true\n") != OK);
	CHECK(analyze_source("func test() -> void:\n\tvar callback: Callable[[int], bool] = func(value: int) -> String:\n\t\treturn \"ok\"\n") != OK);
	CHECK(analyze_source("func test() -> void:\n\tvar callback: Callable[[int], bool] = func(value: int) -> bool:\n\t\treturn true\n\tcallback.call(\"bad\")\n") != OK);
	CHECK(analyze_source("func test() -> void:\n\tvar callback: Callable[[int], bool] = func(value: int) -> bool:\n\t\treturn true\n\tvar result: bool = callback.call(1)\n") == OK);
	CHECK(analyze_source("func make_callback() -> Callable[[int], bool]:\n\treturn func(value: int) -> bool:\n\t\treturn true\n") == OK);
	CHECK(analyze_source("func make_callback() -> Callable[[int], bool]:\n\treturn func(value: String) -> bool:\n\t\treturn true\n") != OK);
	CHECK(analyze_source("func make_callback() -> Callable[[int], bool]:\n\treturn func(value: int) -> String:\n\t\treturn \"ok\"\n") != OK);
}

TEST_CASE("[Modules][GDScript] Analyzer checks typed Callable.call invocations") {
	const String source_prefix = "func accepts_int(value: int) -> bool:\n\treturn true\nfunc get_dynamic() -> Variant:\n\treturn 1\nfunc test() -> void:\n\tvar callback: Callable[[int], bool] = accepts_int\n";
	const String inferred_source_prefix = "func accepts_int(value: int) -> bool:\n\treturn true\nfunc test() -> void:\n\tvar callback := accepts_int\n";

	CHECK(analyze_source(source_prefix + "\tvar result: bool = callback.call(1)\n") == OK);
	CHECK(analyze_source(source_prefix + "\tcallback.call(\"bad\")\n") != OK);
	CHECK(analyze_source(source_prefix + "\tvar result: String = callback.call(1)\n") != OK);
	CHECK(analyze_source(source_prefix + "\tcallback.call()\n") != OK);
	CHECK(analyze_source(source_prefix + "\tcallback.call(1, 2)\n") != OK);
	CHECK(analyze_source(source_prefix + "\tcallback.callv([1])\n") == OK);
	CHECK(analyze_source(source_prefix + "\tcallback.callv([\"bad\"])\n") != OK);
	CHECK(analyze_source(source_prefix + "\tcallback.callv([])\n") != OK);
	CHECK(analyze_source(source_prefix + "\tcallback.callv([1, 2])\n") != OK);
	CHECK(analyze_source(source_prefix + "\tvar result: bool = callback.callv([1])\n") == OK);
	CHECK(analyze_source(source_prefix + "\tvar result: String = callback.callv([1])\n") != OK);
	CHECK(analyze_source(source_prefix + "\tcallback.callv([get_dynamic()])\n") == OK);
	CHECK(analyze_source(source_prefix + "\tcallback.callv([get_dynamic()])\n", false, true) != OK);
	CHECK(analyze_source(inferred_source_prefix + "\tcallback.call(\"legacy dynamic\")\n") == OK);
	CHECK(analyze_source(inferred_source_prefix + "\tcallback.callv([\"legacy dynamic\"])\n") == OK);
}

TEST_CASE("[Modules][GDScript] Analyzer checks typed Callable.call_deferred invocations") {
	const String source_prefix = "func accepts_int(value: int) -> bool:\n\treturn true\nfunc get_dynamic() -> Variant:\n\treturn 1\nfunc test() -> void:\n\tvar callback: Callable[[int], bool] = accepts_int\n";
	const String inferred_source_prefix = "func accepts_int(value: int) -> bool:\n\treturn true\nfunc test() -> void:\n\tvar callback := accepts_int\n";

	CHECK(analyze_source(source_prefix + "\tcallback.call_deferred(1)\n") == OK);
	CHECK(analyze_source(source_prefix + "\tcallback.call_deferred(\"bad\")\n") != OK);
	CHECK(analyze_source(source_prefix + "\tcallback.call_deferred()\n") != OK);
	CHECK(analyze_source(source_prefix + "\tcallback.call_deferred(1, 2)\n") != OK);
	CHECK(analyze_source(source_prefix + "\tvar result: bool = callback.call_deferred(1)\n") != OK);
	CHECK(analyze_source(source_prefix + "\tcallback.call_deferred(get_dynamic())\n") == OK);
	CHECK(analyze_source(source_prefix + "\tcallback.call_deferred(get_dynamic())\n", false, true) != OK);
	CHECK(analyze_source(inferred_source_prefix + "\tcallback.call_deferred(\"legacy dynamic\")\n") == OK);
}

TEST_CASE("[Modules][GDScript] Analyzer checks typed Callable.rpc invocations") {
	const String source_prefix = "func accepts_int(value: int) -> bool:\n\treturn true\nfunc get_dynamic() -> Variant:\n\treturn 1\nfunc test() -> void:\n\tvar callback: Callable[[int], bool] = accepts_int\n";
	const String inferred_source_prefix = "func accepts_int(value: int) -> bool:\n\treturn true\nfunc test() -> void:\n\tvar callback := accepts_int\n";

	CHECK(analyze_source(source_prefix + "\tcallback.rpc(1)\n") == OK);
	CHECK(analyze_source(source_prefix + "\tcallback.rpc(\"bad\")\n") != OK);
	CHECK(analyze_source(source_prefix + "\tcallback.rpc()\n") != OK);
	CHECK(analyze_source(source_prefix + "\tcallback.rpc(1, 2)\n") != OK);
	CHECK(analyze_source(source_prefix + "\tvar result: bool = callback.rpc(1)\n") != OK);
	CHECK(analyze_source(source_prefix + "\tcallback.rpc(get_dynamic())\n") == OK);
	CHECK(analyze_source(source_prefix + "\tcallback.rpc(get_dynamic())\n", false, true) != OK);
	CHECK(analyze_source(inferred_source_prefix + "\tcallback.rpc(\"legacy dynamic\")\n") == OK);
}

TEST_CASE("[Modules][GDScript] Analyzer checks typed Callable.rpc_id invocations") {
	const String source_prefix = "func accepts_int(value: int) -> bool:\n\treturn true\nfunc get_dynamic() -> Variant:\n\treturn 1\nfunc get_peer() -> Variant:\n\treturn 1\nfunc test() -> void:\n\tvar callback: Callable[[int], bool] = accepts_int\n";
	const String inferred_source_prefix = "func accepts_int(value: int) -> bool:\n\treturn true\nfunc test() -> void:\n\tvar callback := accepts_int\n";

	CHECK(analyze_source(source_prefix + "\tcallback.rpc_id(1, 1)\n") == OK);
	CHECK(analyze_source(source_prefix + "\tcallback.rpc_id(\"bad\", 1)\n") != OK);
	CHECK(analyze_source(source_prefix + "\tcallback.rpc_id(1, \"bad\")\n") != OK);
	CHECK(analyze_source(source_prefix + "\tcallback.rpc_id()\n") != OK);
	CHECK(analyze_source(source_prefix + "\tcallback.rpc_id(1)\n") != OK);
	CHECK(analyze_source(source_prefix + "\tcallback.rpc_id(1, 1, 2)\n") != OK);
	CHECK(analyze_source(source_prefix + "\tvar result: bool = callback.rpc_id(1, 1)\n") != OK);
	CHECK(analyze_source(source_prefix + "\tcallback.rpc_id(get_peer(), get_dynamic())\n") == OK);
	CHECK(analyze_source(source_prefix + "\tcallback.rpc_id(get_peer(), get_dynamic())\n", false, true) != OK);
	CHECK(analyze_source(inferred_source_prefix + "\tcallback.rpc_id(1, \"legacy dynamic\")\n") == OK);
}

TEST_CASE("[Modules][GDScript] Analyzer checks typed Callable.bind signatures") {
	const String source_prefix = "func accepts_int_string(value: int, text: String) -> bool:\n\treturn true\nfunc get_dynamic() -> Variant:\n\treturn \"ok\"\nfunc test() -> void:\n\tvar callback: Callable[[int, String], bool] = accepts_int_string\n";
	const String inferred_source_prefix = "func accepts_int_string(value: int, text: String) -> bool:\n\treturn true\nfunc test() -> void:\n\tvar callback := accepts_int_string\n";

	CHECK(analyze_source(source_prefix + "\tvar bound := callback.bind(\"ok\")\n\tvar result: bool = bound.call(1)\n") == OK);
	CHECK(analyze_source(source_prefix + "\tcallback.bind(1)\n") != OK);
	CHECK(analyze_source(source_prefix + "\tcallback.bind(\"ok\").call(\"bad\")\n") != OK);
	CHECK(analyze_source(source_prefix + "\tcallback.bind(\"ok\").call()\n") != OK);
	CHECK(analyze_source(source_prefix + "\tcallback.bind(\"ok\").call(1, 2)\n") != OK);
	CHECK(analyze_source(source_prefix + "\tvar result: String = callback.bind(\"ok\").call(1)\n") != OK);
	CHECK(analyze_source(source_prefix + "\tcallback.bind(1, \"ok\").call()\n") == OK);
	CHECK(analyze_source(source_prefix + "\tcallback.bind(\"ok\", 1)\n") != OK);
	CHECK(analyze_source(source_prefix + "\tcallback.bind(1, \"ok\", false)\n") != OK);
	CHECK(analyze_source(source_prefix + "\tcallback.bind(get_dynamic()).call(1)\n") == OK);
	CHECK(analyze_source(source_prefix + "\tcallback.bind(get_dynamic()).call(1)\n", false, true) != OK);
	CHECK(analyze_source(inferred_source_prefix + "\tcallback.bind(\"legacy dynamic\").call(\"legacy dynamic\")\n") == OK);
}

TEST_CASE("[Modules][GDScript] Analyzer checks typed Callable.bindv signatures") {
	const String source_prefix = "func accepts_int_string(value: int, text: String) -> bool:\n\treturn true\nfunc get_dynamic() -> Variant:\n\treturn \"ok\"\nfunc test() -> void:\n\tvar callback: Callable[[int, String], bool] = accepts_int_string\n";
	const String inferred_source_prefix = "func accepts_int_string(value: int, text: String) -> bool:\n\treturn true\nfunc test() -> void:\n\tvar callback := accepts_int_string\n";

	CHECK(analyze_source(source_prefix + "\tvar bound := callback.bindv([\"ok\"])\n\tvar result: bool = bound.call(1)\n") == OK);
	CHECK(analyze_source(source_prefix + "\tcallback.bindv([1])\n") != OK);
	CHECK(analyze_source(source_prefix + "\tcallback.bindv([\"ok\"]).call(\"bad\")\n") != OK);
	CHECK(analyze_source(source_prefix + "\tcallback.bindv([\"ok\"]).call()\n") != OK);
	CHECK(analyze_source(source_prefix + "\tcallback.bindv([\"ok\"]).call(1, 2)\n") != OK);
	CHECK(analyze_source(source_prefix + "\tvar result: String = callback.bindv([\"ok\"]).call(1)\n") != OK);
	CHECK(analyze_source(source_prefix + "\tcallback.bindv([1, \"ok\"]).call()\n") == OK);
	CHECK(analyze_source(source_prefix + "\tcallback.bindv([\"ok\", 1])\n") != OK);
	CHECK(analyze_source(source_prefix + "\tcallback.bindv([1, \"ok\", false])\n") != OK);
	CHECK(analyze_source(source_prefix + "\tcallback.bindv([get_dynamic()]).call(1)\n") == OK);
	CHECK(analyze_source(source_prefix + "\tcallback.bindv([get_dynamic()]).call(1)\n", false, true) != OK);
	CHECK(analyze_source(inferred_source_prefix + "\tcallback.bindv([\"legacy dynamic\"]).call(\"legacy dynamic\")\n") == OK);
}

TEST_CASE("[Modules][GDScript] Analyzer checks typed Callable.unbind signatures") {
	const String source_prefix = "func accept_string(text: String) -> void:\n\tpass\nfunc test() -> void:\n\tvar callback: Callable[[String], void] = accept_string\n";
	const String inferred_source_prefix = "func accept_string(text: String) -> void:\n\tpass\nfunc test() -> void:\n\tvar callback := accept_string\n";

	CHECK(analyze_source(source_prefix + "\tcallback.unbind(1).call(\"ok\", 1)\n") == OK);
	CHECK(analyze_source(source_prefix + "\tcallback.unbind(1).call(1, \"ignored\")\n") != OK);
	CHECK(analyze_source(source_prefix + "\tcallback.unbind(1).call(\"ok\")\n") != OK);
	CHECK(analyze_source(source_prefix + "\tcallback.unbind(1).call(\"ok\", 1, 2)\n") != OK);
	CHECK(analyze_source(source_prefix + "\tcallback.unbind(2).call(\"ok\", 1, 2)\n") == OK);
	CHECK(analyze_source(source_prefix + "\tcallback.unbind(0).call(\"ok\")\n") != OK);
	CHECK(analyze_source(source_prefix + "\tcallback.unbind(\"bad\")\n") != OK);
	CHECK(analyze_source(inferred_source_prefix + "\tcallback.unbind(1).call(1, \"legacy dynamic\")\n") == OK);
}

TEST_CASE("[Modules][GDScript] Analyzer checks bound Callable Signal.connect signatures") {
	const String source_prefix = "signal event(value: int)\nsignal event_with_extra(value: String, ignored: int)\nfunc accept_int_string(value: int, text: String) -> void:\n\tpass\nfunc accept_string(text: String) -> void:\n\tpass\nfunc test() -> void:\n\tvar typed_event: Signal[[int]] = event\n\tvar typed_event_with_extra: Signal[[String, int]] = event_with_extra\n\tvar int_string_callback: Callable[[int, String], void] = accept_int_string\n\tvar string_callback: Callable[[String], void] = accept_string\n";

	CHECK(analyze_source(source_prefix + "\ttyped_event.connect(int_string_callback.bind(\"ok\"))\n") == OK);
	CHECK(analyze_source(source_prefix + "\ttyped_event.connect(int_string_callback.bind(1))\n") != OK);
	CHECK(analyze_source(source_prefix + "\ttyped_event.connect(int_string_callback.bind(\"ok\", 1))\n") != OK);
	CHECK(analyze_source(source_prefix + "\ttyped_event_with_extra.connect(string_callback.unbind(1))\n") == OK);
	CHECK(analyze_source(source_prefix + "\ttyped_event_with_extra.connect(string_callback)\n") != OK);
}

TEST_CASE("[Modules][GDScript] Analyzer checks GDScript member Callable.call invocations") {
	const String source_prefix = "class Worker:\n\tfunc stringify(value: int) -> String:\n\t\treturn \"ok\"\nfunc get_dynamic() -> Variant:\n\treturn 1\nfunc test(worker: Worker) -> void:\n";

	CHECK(analyze_source(source_prefix + "\tvar result: String = worker.stringify.call(1)\n") == OK);
	CHECK(analyze_source(source_prefix + "\tworker.stringify.call(\"bad\")\n") != OK);
	CHECK(analyze_source(source_prefix + "\tworker.stringify.call()\n") != OK);
	CHECK(analyze_source(source_prefix + "\tworker.stringify.call(1, 2)\n") != OK);
	CHECK(analyze_source(source_prefix + "\tvar result: int = worker.stringify.call(1)\n") != OK);
	CHECK(analyze_source(source_prefix + "\tworker.stringify.call(get_dynamic())\n") == OK);
	CHECK(analyze_source(source_prefix + "\tworker.stringify.call(get_dynamic())\n", false, true) != OK);
}

TEST_CASE("[Modules][GDScript] Analyzer checks native member Callable.call invocations") {
	const String source_prefix = "func get_dynamic() -> Variant:\n\treturn 1\nfunc test(node: Node) -> void:\n";

	CHECK(analyze_source(source_prefix + "\tvar result: String = node.get_name.call()\n") == OK);
	CHECK(analyze_source(source_prefix + "\tnode.get_name.call(1)\n") != OK);
	CHECK(analyze_source(source_prefix + "\tvar result: int = node.get_name.call()\n") != OK);
	CHECK(analyze_source(source_prefix + "\tvar child: Node = node.get_child.call(0)\n") == OK);
	CHECK(analyze_source(source_prefix + "\tnode.get_child.call(\"bad\")\n") != OK);
	CHECK(analyze_source(source_prefix + "\tnode.get_child.call()\n") != OK);
	CHECK(analyze_source(source_prefix + "\tnode.get_child.call(0, false)\n") == OK);
	CHECK(analyze_source(source_prefix + "\tnode.get_child.call(0, false, true)\n") != OK);
	CHECK(analyze_source(source_prefix + "\tnode.get_child.call(get_dynamic())\n") == OK);
	CHECK(analyze_source(source_prefix + "\tnode.get_child.call(get_dynamic())\n", false, true) != OK);
}

TEST_CASE("[Modules][GDScript] Analyzer checks builtin member Callable.call invocations") {
	const String source_prefix = "func get_dynamic() -> Variant:\n\treturn 1\nfunc test(text: String) -> void:\n";

	CHECK(analyze_source(source_prefix + "\tvar result: String = text.substr.call(1)\n") == OK);
	CHECK(analyze_source(source_prefix + "\ttext.substr.call(1, 2)\n") == OK);
	CHECK(analyze_source(source_prefix + "\ttext.substr.call(\"bad\")\n") != OK);
	CHECK(analyze_source(source_prefix + "\ttext.substr.call()\n") != OK);
	CHECK(analyze_source(source_prefix + "\ttext.substr.call(1, 2, 3)\n") != OK);
	CHECK(analyze_source(source_prefix + "\tvar result: int = text.substr.call(1)\n") != OK);
	CHECK(analyze_source(source_prefix + "\ttext.substr.call(get_dynamic())\n") == OK);
	CHECK(analyze_source(source_prefix + "\ttext.substr.call(get_dynamic())\n", false, true) != OK);
}

TEST_CASE("[Modules][GDScript] Analyzer checks typed Callable constructors from constant method names") {
	const String source_prefix = "class Worker:\n\tfunc stringify(value: int) -> String:\n\t\treturn \"ok\"\nfunc get_dynamic() -> Variant:\n\treturn 1\nfunc test(worker: Worker, node: Node, text: String, method_name: StringName) -> void:\n";

	CHECK(analyze_source(source_prefix + "\tvar result: String = Callable(worker, \"stringify\").call(1)\n") == OK);
	CHECK(analyze_source(source_prefix + "\tCallable(worker, \"stringify\").call(\"bad\")\n") != OK);
	CHECK(analyze_source(source_prefix + "\tCallable(worker, \"stringify\").call()\n") != OK);
	CHECK(analyze_source(source_prefix + "\tCallable(worker, \"stringify\").call(1, 2)\n") != OK);
	CHECK(analyze_source(source_prefix + "\tvar result: int = Callable(worker, \"stringify\").call(1)\n") != OK);
	CHECK(analyze_source(source_prefix + "\tCallable(worker, \"stringify\").call(get_dynamic())\n") == OK);
	CHECK(analyze_source(source_prefix + "\tCallable(worker, \"stringify\").call(get_dynamic())\n", false, true) != OK);
	CHECK(analyze_source(source_prefix + "\tvar result: Node = Callable.create(node, &\"get_child\").call(0)\n") == OK);
	CHECK(analyze_source(source_prefix + "\tCallable.create(node, &\"get_child\").call(\"bad\")\n") != OK);
	CHECK(analyze_source(source_prefix + "\tCallable.create(node, &\"get_child\").call()\n") != OK);
	CHECK(analyze_source(source_prefix + "\tCallable.create(node, &\"get_child\").call(0, false)\n") == OK);
	CHECK(analyze_source(source_prefix + "\tCallable.create(node, &\"get_child\").call(0, false, true)\n") != OK);
	CHECK(analyze_source(source_prefix + "\tvar result: String = Callable.create(text, \"substr\").call(1)\n") == OK);
	CHECK(analyze_source(source_prefix + "\tCallable.create(text, \"substr\").call(1, 2)\n") == OK);
	CHECK(analyze_source(source_prefix + "\tCallable.create(text, \"substr\").call(\"bad\")\n") != OK);
	CHECK(analyze_source(source_prefix + "\tCallable.create(text, \"substr\").call()\n") != OK);
	CHECK(analyze_source(source_prefix + "\tCallable.create(text, \"substr\").call(1, 2, 3)\n") != OK);
	CHECK(analyze_source(source_prefix + "\tvar result: int = Callable.create(text, \"substr\").call(1)\n") != OK);
	CHECK(analyze_source(source_prefix + "\tCallable.create(text, \"substr\").call(get_dynamic())\n") == OK);
	CHECK(analyze_source(source_prefix + "\tCallable.create(text, \"substr\").call(get_dynamic())\n", false, true) != OK);
	CHECK(analyze_source(source_prefix + "\tCallable(worker, method_name).call(\"legacy dynamic\")\n") == OK);
	CHECK(analyze_source(source_prefix + "\tCallable(worker, \"unknown\").call(\"legacy dynamic\")\n") == OK);
}

TEST_CASE("[Modules][GDScript] Analyzer checks typed Object.call invocations from constant method names") {
	const String source_prefix = "class Worker:\n\tfunc stringify(value: int) -> String:\n\t\treturn \"ok\"\nfunc get_dynamic() -> Variant:\n\treturn 1\nfunc test(worker: Worker, node: Node, text: String, method_name: StringName) -> void:\n";

	CHECK(analyze_source(source_prefix + "\tvar result: String = worker.call(\"stringify\", 1)\n") == OK);
	CHECK(analyze_source(source_prefix + "\tworker.call(\"stringify\", \"bad\")\n") != OK);
	CHECK(analyze_source(source_prefix + "\tworker.call(\"stringify\")\n") != OK);
	CHECK(analyze_source(source_prefix + "\tworker.call(\"stringify\", 1, 2)\n") != OK);
	CHECK(analyze_source(source_prefix + "\tvar result: int = worker.call(\"stringify\", 1)\n") != OK);
	CHECK(analyze_source(source_prefix + "\tworker.call(\"stringify\", get_dynamic())\n") == OK);
	CHECK(analyze_source(source_prefix + "\tworker.call(\"stringify\", get_dynamic())\n", false, true) != OK);
	CHECK(analyze_source(source_prefix + "\tvar result: Node = node.call(&\"get_child\", 0)\n") == OK);
	CHECK(analyze_source(source_prefix + "\tnode.call(&\"get_child\", \"bad\")\n") != OK);
	CHECK(analyze_source(source_prefix + "\tnode.call(&\"get_child\")\n") != OK);
	CHECK(analyze_source(source_prefix + "\tnode.call(&\"get_child\", 0, false)\n") == OK);
	CHECK(analyze_source(source_prefix + "\tnode.call(&\"get_child\", 0, false, true)\n") != OK);
	CHECK(analyze_source(source_prefix + "\tworker.call(method_name, \"legacy dynamic\")\n") == OK);
	CHECK(analyze_source(source_prefix + "\tworker.call(\"unknown\", \"legacy dynamic\")\n") == OK);
}

TEST_CASE("[Modules][GDScript] Analyzer checks typed Object.call_deferred invocations from constant method names") {
	const String source_prefix = "class Worker:\n\tfunc stringify(value: int) -> String:\n\t\treturn \"ok\"\nfunc get_dynamic() -> Variant:\n\treturn 1\nfunc test(worker: Worker, node: Node, method_name: StringName) -> void:\n";

	CHECK(analyze_source(source_prefix + "\tworker.call_deferred(\"stringify\", 1)\n") == OK);
	CHECK(analyze_source(source_prefix + "\tworker.call_deferred(\"stringify\", \"bad\")\n") != OK);
	CHECK(analyze_source(source_prefix + "\tworker.call_deferred(\"stringify\")\n") != OK);
	CHECK(analyze_source(source_prefix + "\tworker.call_deferred(\"stringify\", 1, 2)\n") != OK);
	CHECK(analyze_source(source_prefix + "\tvar result: String = worker.call_deferred(\"stringify\", 1)\n") != OK);
	CHECK(analyze_source(source_prefix + "\tworker.call_deferred(\"stringify\", get_dynamic())\n") == OK);
	CHECK(analyze_source(source_prefix + "\tworker.call_deferred(\"stringify\", get_dynamic())\n", false, true) != OK);
	CHECK(analyze_source(source_prefix + "\tnode.call_deferred(&\"get_child\", 0)\n") == OK);
	CHECK(analyze_source(source_prefix + "\tnode.call_deferred(&\"get_child\", \"bad\")\n") != OK);
	CHECK(analyze_source(source_prefix + "\tnode.call_deferred(&\"get_child\", 0, false)\n") == OK);
	CHECK(analyze_source(source_prefix + "\tworker.call_deferred(method_name, \"legacy dynamic\")\n") == OK);
	CHECK(analyze_source(source_prefix + "\tworker.call_deferred(\"unknown\", \"legacy dynamic\")\n") == OK);
}

TEST_CASE("[Modules][GDScript] Analyzer checks typed Object.callv invocations from constant method names") {
	const String source_prefix = "class Worker:\n\tfunc stringify(value: int) -> String:\n\t\treturn \"ok\"\nfunc get_dynamic() -> Variant:\n\treturn 1\nfunc test(worker: Worker, node: Node, method_name: StringName, arguments: Array) -> void:\n";

	CHECK(analyze_source(source_prefix + "\tvar result: String = worker.callv(\"stringify\", [1])\n") == OK);
	CHECK(analyze_source(source_prefix + "\tworker.callv(\"stringify\", [\"bad\"])\n") != OK);
	CHECK(analyze_source(source_prefix + "\tworker.callv(\"stringify\", [])\n") != OK);
	CHECK(analyze_source(source_prefix + "\tworker.callv(\"stringify\", [1, 2])\n") != OK);
	CHECK(analyze_source(source_prefix + "\tvar result: int = worker.callv(\"stringify\", [1])\n") != OK);
	CHECK(analyze_source(source_prefix + "\tworker.callv(\"stringify\", [get_dynamic()])\n") == OK);
	CHECK(analyze_source(source_prefix + "\tworker.callv(\"stringify\", [get_dynamic()])\n", false, true) != OK);
	CHECK(analyze_source(source_prefix + "\tvar result: Node = node.callv(&\"get_child\", [0])\n") == OK);
	CHECK(analyze_source(source_prefix + "\tnode.callv(&\"get_child\", [\"bad\"])\n") != OK);
	CHECK(analyze_source(source_prefix + "\tnode.callv(&\"get_child\", [])\n") != OK);
	CHECK(analyze_source(source_prefix + "\tnode.callv(&\"get_child\", [0, false])\n") == OK);
	CHECK(analyze_source(source_prefix + "\tnode.callv(&\"get_child\", [0, false, true])\n") != OK);
	CHECK(analyze_source(source_prefix + "\tworker.callv(\"stringify\", arguments)\n") == OK);
	CHECK(analyze_source(source_prefix + "\tworker.callv(method_name, [\"legacy dynamic\"])\n") == OK);
	CHECK(analyze_source(source_prefix + "\tworker.callv(\"unknown\", [\"legacy dynamic\"])\n") == OK);
}

TEST_CASE("[Modules][GDScript] Analyzer checks typed Object property reflection from constant property names") {
	const String source_prefix = "class Worker:\n\tvar count: int\n\tvar title: String\nfunc get_dynamic() -> Variant:\n\treturn 1\nfunc test(worker: Worker, node: Node, property_name: StringName) -> void:\n";

	CHECK(analyze_source(source_prefix + "\tvar result: int = worker.get(\"count\")\n") == OK);
	CHECK(analyze_source(source_prefix + "\tvar result: String = worker.get(\"count\")\n") != OK);
	CHECK(analyze_source(source_prefix + "\tworker.set(\"count\", 1)\n") == OK);
	CHECK(analyze_source(source_prefix + "\tworker.set(\"count\", \"bad\")\n") != OK);
	CHECK(analyze_source(source_prefix + "\tworker.set(\"count\", get_dynamic())\n") == OK);
	CHECK(analyze_source(source_prefix + "\tworker.set(\"count\", get_dynamic())\n", false, true) != OK);
	CHECK(analyze_source(source_prefix + "\tworker.set_deferred(\"title\", \"ok\")\n") == OK);
	CHECK(analyze_source(source_prefix + "\tworker.set_deferred(\"title\", 1)\n") != OK);
	CHECK(analyze_source(source_prefix + "\tworker.set_deferred(\"count\", get_dynamic())\n") == OK);
	CHECK(analyze_source(source_prefix + "\tworker.set_deferred(\"count\", get_dynamic())\n", false, true) != OK);
	CHECK(analyze_source(source_prefix + "\tvar result: String = worker.set_deferred(\"title\", \"ok\")\n") != OK);
	CHECK(analyze_source(source_prefix + "\tvar result: StringName = node.get(&\"name\")\n") == OK);
	CHECK(analyze_source(source_prefix + "\tvar result: int = node.get(&\"name\")\n") != OK);
	CHECK(analyze_source(source_prefix + "\tnode.set(&\"name\", &\"Child\")\n") == OK);
	CHECK(analyze_source(source_prefix + "\tnode.set(&\"name\", 1)\n") != OK);
	CHECK(analyze_source(source_prefix + "\tnode.set_deferred(&\"name\", &\"Child\")\n") == OK);
	CHECK(analyze_source(source_prefix + "\tnode.set_deferred(&\"name\", 1)\n") != OK);
	CHECK(analyze_source(source_prefix + "\tworker.get(property_name)\n") == OK);
	CHECK(analyze_source(source_prefix + "\tworker.set(property_name, \"legacy dynamic\")\n") == OK);
	CHECK(analyze_source(source_prefix + "\tworker.set(\"unknown\", \"legacy dynamic\")\n") == OK);
}

TEST_CASE("[Modules][GDScript] Analyzer checks typed Object indexed property reflection from constant property paths") {
	const String source_prefix = "class Worker:\n\tvar count: int\n\tvar position: Vector2\nfunc get_dynamic() -> Variant:\n\treturn 1\nfunc test(worker: Worker, node: Node, node_2d: Node2D, property_path: NodePath) -> void:\n";

	CHECK(analyze_source(source_prefix + "\tvar result: int = worker.get_indexed(^\"count\")\n") == OK);
	CHECK(analyze_source(source_prefix + "\tvar result: String = worker.get_indexed(^\"count\")\n") != OK);
	CHECK(analyze_source(source_prefix + "\tworker.set_indexed(^\"count\", 1)\n") == OK);
	CHECK(analyze_source(source_prefix + "\tworker.set_indexed(^\"count\", \"bad\")\n") != OK);
	CHECK(analyze_source(source_prefix + "\tworker.set_indexed(^\"count\", get_dynamic())\n") == OK);
	CHECK(analyze_source(source_prefix + "\tworker.set_indexed(^\"count\", get_dynamic())\n", false, true) != OK);
	CHECK(analyze_source(source_prefix + "\tvar result: float = worker.get_indexed(^\"position:x\")\n") == OK);
	CHECK(analyze_source(source_prefix + "\tvar result: String = worker.get_indexed(^\"position:x\")\n") != OK);
	CHECK(analyze_source(source_prefix + "\tworker.set_indexed(^\"position:x\", 1.0)\n") == OK);
	CHECK(analyze_source(source_prefix + "\tworker.set_indexed(^\"position:x\", \"bad\")\n") != OK);
	CHECK(analyze_source(source_prefix + "\tvar result: StringName = node.get_indexed(^\"name\")\n") == OK);
	CHECK(analyze_source(source_prefix + "\tvar result: int = node.get_indexed(^\"name\")\n") != OK);
	CHECK(analyze_source(source_prefix + "\tnode.set_indexed(^\"name\", &\"Child\")\n") == OK);
	CHECK(analyze_source(source_prefix + "\tnode.set_indexed(^\"name\", 1)\n") != OK);
	CHECK(analyze_source(source_prefix + "\tvar result: float = node_2d.get_indexed(^\"position:x\")\n") == OK);
	CHECK(analyze_source(source_prefix + "\tnode_2d.set_indexed(^\"position:x\", 1.0)\n") == OK);
	CHECK(analyze_source(source_prefix + "\tnode_2d.set_indexed(^\"position:x\", \"bad\")\n") != OK);
	CHECK(analyze_source(source_prefix + "\tworker.get_indexed(property_path)\n") == OK);
	CHECK(analyze_source(source_prefix + "\tworker.set_indexed(property_path, \"legacy dynamic\")\n") == OK);
	CHECK(analyze_source(source_prefix + "\tworker.set_indexed(^\"unknown\", \"legacy dynamic\")\n") == OK);
}

TEST_CASE("[Modules][GDScript] Analyzer checks typed Node.rpc invocations from constant method names") {
	const String source_prefix = "class Worker extends Node:\n\tfunc stringify(value: int) -> String:\n\t\treturn \"ok\"\nfunc get_dynamic() -> Variant:\n\treturn 1\nfunc test(worker: Worker, method_name: StringName) -> void:\n";

	CHECK(analyze_source(source_prefix + "\tworker.rpc(\"stringify\", 1)\n") == OK);
	CHECK(analyze_source(source_prefix + "\tworker.rpc(\"stringify\", \"bad\")\n") != OK);
	CHECK(analyze_source(source_prefix + "\tworker.rpc(\"stringify\")\n") != OK);
	CHECK(analyze_source(source_prefix + "\tworker.rpc(\"stringify\", 1, 2)\n") != OK);
	CHECK(analyze_source(source_prefix + "\tvar result: String = worker.rpc(\"stringify\", 1)\n") != OK);
	CHECK(analyze_source(source_prefix + "\tworker.rpc(\"stringify\", get_dynamic())\n") == OK);
	CHECK(analyze_source(source_prefix + "\tworker.rpc(\"stringify\", get_dynamic())\n", false, true) != OK);
	CHECK(analyze_source(source_prefix + "\tworker.rpc(method_name, \"legacy dynamic\")\n") == OK);
	CHECK(analyze_source(source_prefix + "\tworker.rpc(\"unknown\", \"legacy dynamic\")\n") == OK);
}

TEST_CASE("[Modules][GDScript] Analyzer checks typed Node.rpc_id invocations from constant method names") {
	const String source_prefix = "class Worker extends Node:\n\tfunc stringify(value: int) -> String:\n\t\treturn \"ok\"\nfunc get_dynamic() -> Variant:\n\treturn 1\nfunc get_peer() -> Variant:\n\treturn 1\nfunc test(worker: Worker, method_name: StringName) -> void:\n";

	CHECK(analyze_source(source_prefix + "\tworker.rpc_id(1, \"stringify\", 1)\n") == OK);
	CHECK(analyze_source(source_prefix + "\tworker.rpc_id(\"bad\", \"stringify\", 1)\n") != OK);
	CHECK(analyze_source(source_prefix + "\tworker.rpc_id(1, \"stringify\", \"bad\")\n") != OK);
	CHECK(analyze_source(source_prefix + "\tworker.rpc_id(1, \"stringify\")\n") != OK);
	CHECK(analyze_source(source_prefix + "\tworker.rpc_id(1, \"stringify\", 1, 2)\n") != OK);
	CHECK(analyze_source(source_prefix + "\tvar result: String = worker.rpc_id(1, \"stringify\", 1)\n") != OK);
	CHECK(analyze_source(source_prefix + "\tworker.rpc_id(get_peer(), \"stringify\", get_dynamic())\n") == OK);
	CHECK(analyze_source(source_prefix + "\tworker.rpc_id(get_peer(), \"stringify\", get_dynamic())\n", false, true) != OK);
	CHECK(analyze_source(source_prefix + "\tworker.rpc_id(1, method_name, \"legacy dynamic\")\n") == OK);
	CHECK(analyze_source(source_prefix + "\tworker.rpc_id(1, \"unknown\", \"legacy dynamic\")\n") == OK);
}

TEST_CASE("[Modules][GDScript] Analyzer checks typed Signal.emit invocations") {
	const String source_prefix = "signal event(value: int)\nfunc test() -> void:\n\tvar typed_event: Signal[[int]] = event\n";
	const String inferred_source_prefix = "signal event(value: int)\nfunc test() -> void:\n\tvar typed_event := event\n";

	CHECK(analyze_source(source_prefix + "\ttyped_event.emit(1)\n") == OK);
	CHECK(analyze_source(source_prefix + "\ttyped_event.emit(\"bad\")\n") != OK);
	CHECK(analyze_source(source_prefix + "\ttyped_event.emit()\n") != OK);
	CHECK(analyze_source(source_prefix + "\ttyped_event.emit(1, 2)\n") != OK);
	CHECK(analyze_source(inferred_source_prefix + "\ttyped_event.emit(\"legacy dynamic\")\n") == OK);
}

TEST_CASE("[Modules][GDScript] Analyzer checks typed Signal.connect callables") {
	const String source_prefix = "signal event(value: String)\nfunc accept_string(value: String) -> void:\n\tpass\nfunc accept_int(value: int) -> void:\n\tpass\nfunc accept_any(value: Variant) -> void:\n\tpass\nfunc accept_none() -> void:\n\tpass\nfunc test() -> void:\n\tvar typed_event: Signal[[String]] = event\n";
	const String inferred_source_prefix = "signal event(value: String)\nfunc accept_int(value: int) -> void:\n\tpass\nfunc test() -> void:\n\tvar typed_event := event\n";

	CHECK(analyze_source(source_prefix + "\tvar callback: Callable[[String], void] = accept_string\n\ttyped_event.connect(callback)\n") == OK);
	CHECK(analyze_source(source_prefix + "\ttyped_event.connect(accept_string)\n") == OK);
	CHECK(analyze_source(source_prefix + "\tvar callback: Callable[[int], void] = accept_int\n\ttyped_event.connect(callback)\n") != OK);
	CHECK(analyze_source(source_prefix + "\ttyped_event.connect(accept_int)\n") != OK);
	CHECK(analyze_source(source_prefix + "\tvar callback: Callable[[], void] = accept_none\n\ttyped_event.connect(callback)\n") != OK);
	CHECK(analyze_source(source_prefix + "\tvar callback: Callable[[Variant], void] = accept_any\n\ttyped_event.connect(callback)\n") == OK);
	CHECK(analyze_source(inferred_source_prefix + "\ttyped_event.connect(accept_int)\n") == OK);
	CHECK(analyze_source("signal event(value: Variant)\nfunc accept_string(value: String) -> void:\n\tpass\nfunc test() -> void:\n\tvar typed_event: Signal[[Variant]] = event\n\ttyped_event.connect(accept_string)\n") != OK);
}

TEST_CASE("[Modules][GDScript] Analyzer can reject nullable typed Signal.connect callables") {
	const String source = "signal event(value: Node?)\nfunc accept_node(value: Node) -> void:\n\tpass\nfunc accept_nullable_node(value: Node?) -> void:\n\tpass\nfunc test() -> void:\n\tvar typed_event: Signal[[Node?]] = event\n";
	const String nullable_source = source + "\tvar callback: Callable[[Node?], void] = accept_nullable_node\n\ttyped_event.connect(callback)\n";
	const String non_nullable_source = source + "\tvar callback: Callable[[Node], void] = accept_node\n\ttyped_event.connect(callback)\n";

	CHECK(analyze_source(non_nullable_source) == OK);
	CHECK(analyze_source(non_nullable_source, true) != OK);
	CHECK(analyze_source(nullable_source, true) == OK);
}

TEST_CASE("[Modules][GDScript] Analyzer checks GDScript member Signal.connect callables") {
	const String source_prefix = "class Emitter:\n\tsignal event(value: String)\nfunc accept_string(value: String) -> void:\n\tpass\nfunc accept_int(value: int) -> void:\n\tpass\nfunc accept_any(value: Variant) -> void:\n\tpass\nfunc accept_none() -> void:\n\tpass\nfunc test(emitter: Emitter) -> void:\n";

	CHECK(analyze_source(source_prefix + "\temitter.event.connect(accept_string)\n") == OK);
	CHECK(analyze_source(source_prefix + "\temitter.event.connect(accept_int)\n") != OK);
	CHECK(analyze_source(source_prefix + "\temitter.event.connect(accept_none)\n") != OK);
	CHECK(analyze_source(source_prefix + "\temitter.event.connect(accept_any)\n") == OK);
}

TEST_CASE("[Modules][GDScript] Analyzer checks GDScript member Signal.emit arguments") {
	const String source_prefix = "class Emitter:\n\tsignal event(value: String)\nfunc get_dynamic() -> Variant:\n\treturn \"ok\"\nfunc test(emitter: Emitter) -> void:\n";

	CHECK(analyze_source(source_prefix + "\temitter.event.emit(\"ok\")\n") == OK);
	CHECK(analyze_source(source_prefix + "\temitter.event.emit(1)\n") != OK);
	CHECK(analyze_source(source_prefix + "\temitter.event.emit()\n") != OK);
	CHECK(analyze_source(source_prefix + "\temitter.event.emit(\"ok\", \"extra\")\n") != OK);
	CHECK(analyze_source(source_prefix + "\temitter.event.emit(get_dynamic())\n") == OK);
	CHECK(analyze_source(source_prefix + "\temitter.event.emit(get_dynamic())\n", false, true) != OK);
}

TEST_CASE("[Modules][GDScript] Analyzer checks native member Signal.connect callables") {
	const String source_prefix = "func accept_none() -> void:\n\tpass\nfunc accept_float(value: float) -> void:\n\tpass\nfunc accept_string(value: String) -> void:\n\tpass\nfunc test(button: Button, range: Range) -> void:\n";

	CHECK(analyze_source(source_prefix + "\tbutton.pressed.connect(accept_none)\n") == OK);
	CHECK(analyze_source(source_prefix + "\tbutton.pressed.connect(accept_string)\n") != OK);
	CHECK(analyze_source(source_prefix + "\trange.value_changed.connect(accept_float)\n") == OK);
	CHECK(analyze_source(source_prefix + "\trange.value_changed.connect(accept_string)\n") != OK);
}

TEST_CASE("[Modules][GDScript] Analyzer checks native member Signal.emit arguments") {
	const String source_prefix = "func get_dynamic() -> Variant:\n\treturn 1.0\nfunc test(button: Button, range: Range) -> void:\n";

	CHECK(analyze_source(source_prefix + "\tbutton.pressed.emit()\n") == OK);
	CHECK(analyze_source(source_prefix + "\tbutton.pressed.emit(1)\n") != OK);
	CHECK(analyze_source(source_prefix + "\trange.value_changed.emit(1.0)\n") == OK);
	CHECK(analyze_source(source_prefix + "\trange.value_changed.emit(\"bad\")\n") != OK);
	CHECK(analyze_source(source_prefix + "\trange.value_changed.emit()\n") != OK);
	CHECK(analyze_source(source_prefix + "\trange.value_changed.emit(1.0, 2.0)\n") != OK);
	CHECK(analyze_source(source_prefix + "\trange.value_changed.emit(get_dynamic())\n") == OK);
	CHECK(analyze_source(source_prefix + "\trange.value_changed.emit(get_dynamic())\n", false, true) != OK);
}

TEST_CASE("[Modules][GDScript] Analyzer checks local Object.connect callables") {
	const String source_prefix = "signal event(value: String)\nfunc accept_string(value: String) -> void:\n\tpass\nfunc accept_int(value: int) -> void:\n\tpass\nfunc accept_any(value: Variant) -> void:\n\tpass\nfunc accept_none() -> void:\n\tpass\nfunc test() -> void:\n";
	const String dynamic_signal_source = "signal event(value: String)\nfunc accept_int(value: int) -> void:\n\tpass\nfunc test(signal_name: StringName) -> void:\n\tconnect(signal_name, accept_int)\n";

	CHECK(analyze_source(source_prefix + "\tconnect(\"event\", accept_string)\n") == OK);
	CHECK(analyze_source(source_prefix + "\tself.connect(\"event\", accept_string)\n") == OK);
	CHECK(analyze_source(source_prefix + "\tconnect(&\"event\", accept_string)\n") == OK);
	CHECK(analyze_source(source_prefix + "\tconnect(\"event\", accept_int)\n") != OK);
	CHECK(analyze_source(source_prefix + "\tself.connect(\"event\", accept_int)\n") != OK);
	CHECK(analyze_source(source_prefix + "\tconnect(\"event\", accept_none)\n") != OK);
	CHECK(analyze_source(source_prefix + "\tconnect(\"event\", accept_any)\n") == OK);
	CHECK(analyze_source(dynamic_signal_source) == OK);
	CHECK(analyze_source(source_prefix + "\tconnect(\"unknown\", accept_int)\n") == OK);
}

TEST_CASE("[Modules][GDScript] Analyzer can reject nullable local Object.connect callables") {
	const String source = "signal event(value: Node?)\nfunc accept_node(value: Node) -> void:\n\tpass\nfunc accept_nullable_node(value: Node?) -> void:\n\tpass\nfunc test() -> void:\n";

	CHECK(analyze_source(source + "\tconnect(\"event\", accept_node)\n") == OK);
	CHECK(analyze_source(source + "\tconnect(\"event\", accept_node)\n", true) != OK);
	CHECK(analyze_source(source + "\tconnect(\"event\", accept_nullable_node)\n", true) == OK);
}

TEST_CASE("[Modules][GDScript] Analyzer checks local Object.emit_signal arguments") {
	const String source_prefix = "signal event(value: String)\nfunc get_dynamic() -> Variant:\n\treturn \"ok\"\nfunc test() -> void:\n";
	const String dynamic_signal_source = "signal event(value: String)\nfunc test(signal_name: StringName) -> void:\n\temit_signal(signal_name, 1)\n";

	CHECK(analyze_source(source_prefix + "\temit_signal(\"event\", \"ok\")\n") == OK);
	CHECK(analyze_source(source_prefix + "\tself.emit_signal(\"event\", \"ok\")\n") == OK);
	CHECK(analyze_source(source_prefix + "\temit_signal(&\"event\", \"ok\")\n") == OK);
	CHECK(analyze_source(source_prefix + "\temit_signal(\"event\", 1)\n") != OK);
	CHECK(analyze_source(source_prefix + "\tself.emit_signal(\"event\", 1)\n") != OK);
	CHECK(analyze_source(source_prefix + "\temit_signal(\"event\")\n") != OK);
	CHECK(analyze_source(source_prefix + "\temit_signal(\"event\", \"ok\", \"extra\")\n") != OK);
	CHECK(analyze_source(source_prefix + "\temit_signal(\"event\", get_dynamic())\n") == OK);
	CHECK(analyze_source(source_prefix + "\temit_signal(\"event\", get_dynamic())\n", false, true) != OK);
	CHECK(analyze_source(dynamic_signal_source) == OK);
	CHECK(analyze_source(source_prefix + "\temit_signal(\"unknown\", 1)\n") == OK);
}

TEST_CASE("[Modules][GDScript] Analyzer can reject nullable local Object.emit_signal arguments") {
	const String source = "signal event(value: Node)\nsignal nullable_event(value: Node?)\nfunc test() -> void:\n";

	CHECK(analyze_source(source + "\temit_signal(\"event\", null)\n") == OK);
	CHECK(analyze_source(source + "\temit_signal(\"event\", null)\n", true) != OK);
	CHECK(analyze_source(source + "\temit_signal(\"nullable_event\", null)\n", true) == OK);
}

TEST_CASE("[Modules][GDScript] Analyzer checks local Object.disconnect callables") {
	const String source_prefix = "signal event(value: String)\nfunc accept_string(value: String) -> void:\n\tpass\nfunc accept_int(value: int) -> void:\n\tpass\nfunc accept_any(value: Variant) -> void:\n\tpass\nfunc accept_none() -> void:\n\tpass\nfunc test() -> void:\n";
	const String dynamic_signal_source = "signal event(value: String)\nfunc accept_int(value: int) -> void:\n\tpass\nfunc test(signal_name: StringName) -> void:\n\tdisconnect(signal_name, accept_int)\n";

	CHECK(analyze_source(source_prefix + "\tdisconnect(\"event\", accept_string)\n") == OK);
	CHECK(analyze_source(source_prefix + "\tself.disconnect(\"event\", accept_string)\n") == OK);
	CHECK(analyze_source(source_prefix + "\tdisconnect(&\"event\", accept_string)\n") == OK);
	CHECK(analyze_source(source_prefix + "\tdisconnect(\"event\", accept_int)\n") != OK);
	CHECK(analyze_source(source_prefix + "\tself.disconnect(\"event\", accept_int)\n") != OK);
	CHECK(analyze_source(source_prefix + "\tdisconnect(\"event\", accept_none)\n") != OK);
	CHECK(analyze_source(source_prefix + "\tdisconnect(\"event\", accept_any)\n") == OK);
	CHECK(analyze_source(dynamic_signal_source) == OK);
	CHECK(analyze_source(source_prefix + "\tdisconnect(\"unknown\", accept_int)\n") == OK);
}

TEST_CASE("[Modules][GDScript] Analyzer can reject nullable local Object.disconnect callables") {
	const String source = "signal event(value: Node?)\nfunc accept_node(value: Node) -> void:\n\tpass\nfunc accept_nullable_node(value: Node?) -> void:\n\tpass\nfunc test() -> void:\n";

	CHECK(analyze_source(source + "\tdisconnect(\"event\", accept_node)\n") == OK);
	CHECK(analyze_source(source + "\tdisconnect(\"event\", accept_node)\n", true) != OK);
	CHECK(analyze_source(source + "\tdisconnect(\"event\", accept_nullable_node)\n", true) == OK);
}

TEST_CASE("[Modules][GDScript] Analyzer checks inherited native self Object signal callables") {
	const String source_prefix = "extends Node\nfunc accept_none() -> void:\n\tpass\nfunc accept_node(value: Node) -> void:\n\tpass\nfunc accept_string(value: String) -> void:\n\tpass\nfunc test() -> void:\n";
	const String dynamic_signal_source = "extends Node\nfunc accept_string(value: String) -> void:\n\tpass\nfunc test(signal_name: StringName) -> void:\n\tconnect(signal_name, accept_string)\n\tdisconnect(signal_name, accept_string)\n";

	CHECK(analyze_source(source_prefix + "\tconnect(\"tree_entered\", accept_none)\n\tdisconnect(\"tree_entered\", accept_none)\n") == OK);
	CHECK(analyze_source(source_prefix + "\tself.connect(\"tree_entered\", accept_none)\n\tself.disconnect(\"tree_entered\", accept_none)\n") == OK);
	CHECK(analyze_source(source_prefix + "\tconnect(&\"tree_entered\", accept_none)\n\tdisconnect(&\"tree_entered\", accept_none)\n") == OK);
	CHECK(analyze_source(source_prefix + "\tconnect(\"tree_entered\", accept_string)\n") != OK);
	CHECK(analyze_source(source_prefix + "\tdisconnect(\"tree_entered\", accept_string)\n") != OK);
	CHECK(analyze_source(source_prefix + "\tconnect(\"child_entered_tree\", accept_node)\n\tdisconnect(\"child_entered_tree\", accept_node)\n") == OK);
	CHECK(analyze_source(source_prefix + "\tconnect(\"child_entered_tree\", accept_string)\n") != OK);
	CHECK(analyze_source(source_prefix + "\tdisconnect(\"child_entered_tree\", accept_string)\n") != OK);
	CHECK(analyze_source(dynamic_signal_source) == OK);
	CHECK(analyze_source(source_prefix + "\tconnect(\"unknown\", accept_string)\n\tdisconnect(\"unknown\", accept_string)\n") == OK);
}

TEST_CASE("[Modules][GDScript] Analyzer checks inherited native self Object.emit_signal arguments") {
	const String source_prefix = "extends Node\nfunc get_dynamic() -> Variant:\n\treturn self\nfunc test() -> void:\n";
	const String dynamic_signal_source = "extends Node\nfunc test(signal_name: StringName) -> void:\n\temit_signal(signal_name, 1)\n";

	CHECK(analyze_source(source_prefix + "\temit_signal(\"tree_entered\")\n") == OK);
	CHECK(analyze_source(source_prefix + "\tself.emit_signal(\"tree_entered\")\n") == OK);
	CHECK(analyze_source(source_prefix + "\temit_signal(&\"tree_entered\")\n") == OK);
	CHECK(analyze_source(source_prefix + "\temit_signal(\"tree_entered\", 1)\n") != OK);
	CHECK(analyze_source(source_prefix + "\temit_signal(\"child_entered_tree\", self)\n") == OK);
	CHECK(analyze_source(source_prefix + "\temit_signal(\"child_entered_tree\", \"bad\")\n") != OK);
	CHECK(analyze_source(source_prefix + "\temit_signal(\"child_entered_tree\")\n") != OK);
	CHECK(analyze_source(source_prefix + "\temit_signal(\"child_entered_tree\", self, self)\n") != OK);
	CHECK(analyze_source(source_prefix + "\temit_signal(\"child_entered_tree\", get_dynamic())\n") == OK);
	CHECK(analyze_source(source_prefix + "\temit_signal(\"child_entered_tree\", get_dynamic())\n", false, true) != OK);
	CHECK(analyze_source(dynamic_signal_source) == OK);
	CHECK(analyze_source(source_prefix + "\temit_signal(\"unknown\", 1)\n") == OK);
}

TEST_CASE("[Modules][GDScript] Analyzer checks typed receiver Object signal callables") {
	const String source_prefix = "class Emitter:\n\tsignal event(value: String)\nfunc accept_string(value: String) -> void:\n\tpass\nfunc accept_int(value: int) -> void:\n\tpass\nfunc accept_any(value: Variant) -> void:\n\tpass\nfunc accept_none() -> void:\n\tpass\nfunc test(emitter: Emitter) -> void:\n";
	const String dynamic_signal_source = "class Emitter:\n\tsignal event(value: String)\nfunc accept_int(value: int) -> void:\n\tpass\nfunc test(emitter: Emitter, signal_name: StringName) -> void:\n\temitter.connect(signal_name, accept_int)\n\temitter.disconnect(signal_name, accept_int)\n";
	const String unknown_signal_source = "class Emitter:\n\tsignal event(value: String)\nfunc accept_int(value: int) -> void:\n\tpass\nfunc test(emitter: Emitter) -> void:\n\temitter.connect(\"unknown\", accept_int)\n\temitter.disconnect(\"unknown\", accept_int)\n";

	CHECK(analyze_source(source_prefix + "\temitter.connect(\"event\", accept_string)\n\temitter.disconnect(\"event\", accept_string)\n") == OK);
	CHECK(analyze_source(source_prefix + "\temitter.connect(&\"event\", accept_string)\n\temitter.disconnect(&\"event\", accept_string)\n") == OK);
	CHECK(analyze_source(source_prefix + "\temitter.connect(\"event\", accept_int)\n") != OK);
	CHECK(analyze_source(source_prefix + "\temitter.disconnect(\"event\", accept_int)\n") != OK);
	CHECK(analyze_source(source_prefix + "\temitter.connect(\"event\", accept_none)\n") != OK);
	CHECK(analyze_source(source_prefix + "\temitter.disconnect(\"event\", accept_none)\n") != OK);
	CHECK(analyze_source(source_prefix + "\temitter.connect(\"event\", accept_any)\n\temitter.disconnect(\"event\", accept_any)\n") == OK);
	CHECK(analyze_source(dynamic_signal_source) == OK);
	CHECK(analyze_source(unknown_signal_source) == OK);
}

TEST_CASE("[Modules][GDScript] Analyzer checks typed receiver Object.emit_signal arguments") {
	const String source_prefix = "class Emitter:\n\tsignal event(value: String)\nfunc get_dynamic() -> Variant:\n\treturn \"ok\"\nfunc test(emitter: Emitter) -> void:\n";
	const String dynamic_signal_source = "class Emitter:\n\tsignal event(value: String)\nfunc test(emitter: Emitter, signal_name: StringName) -> void:\n\temitter.emit_signal(signal_name, 1)\n";
	const String dynamic_receiver_source = "func test(emitter: Variant) -> void:\n\temitter.emit_signal(\"event\", 1)\n";

	CHECK(analyze_source(source_prefix + "\temitter.emit_signal(\"event\", \"ok\")\n") == OK);
	CHECK(analyze_source(source_prefix + "\temitter.emit_signal(&\"event\", \"ok\")\n") == OK);
	CHECK(analyze_source(source_prefix + "\temitter.emit_signal(\"event\", 1)\n") != OK);
	CHECK(analyze_source(source_prefix + "\temitter.emit_signal(\"event\")\n") != OK);
	CHECK(analyze_source(source_prefix + "\temitter.emit_signal(\"event\", \"ok\", \"extra\")\n") != OK);
	CHECK(analyze_source(source_prefix + "\temitter.emit_signal(\"event\", get_dynamic())\n") == OK);
	CHECK(analyze_source(source_prefix + "\temitter.emit_signal(\"event\", get_dynamic())\n", false, true) != OK);
	CHECK(analyze_source(dynamic_signal_source) == OK);
	CHECK(analyze_source(source_prefix + "\temitter.emit_signal(\"unknown\", 1)\n") == OK);
	CHECK(analyze_source(dynamic_receiver_source) == OK);
}

TEST_CASE("[Modules][GDScript] Analyzer can reject nullable typed receiver Object signal calls") {
	const String source = "class Emitter:\n\tsignal event(value: Node?)\nfunc accept_node(value: Node) -> void:\n\tpass\nfunc accept_nullable_node(value: Node?) -> void:\n\tpass\nfunc test(emitter: Emitter) -> void:\n";

	CHECK(analyze_source(source + "\temitter.connect(\"event\", accept_node)\n\temitter.disconnect(\"event\", accept_node)\n") == OK);
	CHECK(analyze_source(source + "\temitter.connect(\"event\", accept_node)\n", true) != OK);
	CHECK(analyze_source(source + "\temitter.disconnect(\"event\", accept_node)\n", true) != OK);
	CHECK(analyze_source(source + "\temitter.connect(\"event\", accept_nullable_node)\n\temitter.disconnect(\"event\", accept_nullable_node)\n", true) == OK);
}

TEST_CASE("[Modules][GDScript] Analyzer can reject nullable typed receiver Object.emit_signal arguments") {
	const String source = "class Emitter:\n\tsignal event(value: Node)\n\tsignal nullable_event(value: Node?)\nfunc test(emitter: Emitter) -> void:\n";

	CHECK(analyze_source(source + "\temitter.emit_signal(\"event\", null)\n") == OK);
	CHECK(analyze_source(source + "\temitter.emit_signal(\"event\", null)\n", true) != OK);
	CHECK(analyze_source(source + "\temitter.emit_signal(\"nullable_event\", null)\n", true) == OK);
}

TEST_CASE("[Modules][GDScript] Analyzer checks typed GDScript receiver inherited native signal callables") {
	const String source_prefix = "class Emitter extends Node:\n\tpass\nfunc accept_none() -> void:\n\tpass\nfunc accept_node(value: Node) -> void:\n\tpass\nfunc accept_string(value: String) -> void:\n\tpass\nfunc test(emitter: Emitter) -> void:\n";
	const String dynamic_signal_source = "class Emitter extends Node:\n\tpass\nfunc accept_string(value: String) -> void:\n\tpass\nfunc test(emitter: Emitter, signal_name: StringName) -> void:\n\temitter.connect(signal_name, accept_string)\n\temitter.disconnect(signal_name, accept_string)\n";

	CHECK(analyze_source(source_prefix + "\temitter.connect(\"tree_entered\", accept_none)\n\temitter.disconnect(\"tree_entered\", accept_none)\n") == OK);
	CHECK(analyze_source(source_prefix + "\temitter.connect(&\"tree_entered\", accept_none)\n\temitter.disconnect(&\"tree_entered\", accept_none)\n") == OK);
	CHECK(analyze_source(source_prefix + "\temitter.connect(\"tree_entered\", accept_string)\n") != OK);
	CHECK(analyze_source(source_prefix + "\temitter.disconnect(\"tree_entered\", accept_string)\n") != OK);
	CHECK(analyze_source(source_prefix + "\temitter.connect(\"child_entered_tree\", accept_node)\n\temitter.disconnect(\"child_entered_tree\", accept_node)\n") == OK);
	CHECK(analyze_source(source_prefix + "\temitter.connect(\"child_entered_tree\", accept_string)\n") != OK);
	CHECK(analyze_source(source_prefix + "\temitter.disconnect(\"child_entered_tree\", accept_string)\n") != OK);
	CHECK(analyze_source(dynamic_signal_source) == OK);
	CHECK(analyze_source(source_prefix + "\temitter.connect(\"unknown\", accept_string)\n\temitter.disconnect(\"unknown\", accept_string)\n") == OK);
}

TEST_CASE("[Modules][GDScript] Analyzer checks typed GDScript receiver inherited native Object.emit_signal arguments") {
	const String source_prefix = "class Emitter extends Node:\n\tpass\nfunc get_dynamic() -> Variant:\n\treturn null\nfunc test(emitter: Emitter) -> void:\n";
	const String dynamic_signal_source = "class Emitter extends Node:\n\tpass\nfunc test(emitter: Emitter, signal_name: StringName) -> void:\n\temitter.emit_signal(signal_name, 1)\n";

	CHECK(analyze_source(source_prefix + "\temitter.emit_signal(\"tree_entered\")\n") == OK);
	CHECK(analyze_source(source_prefix + "\temitter.emit_signal(&\"tree_entered\")\n") == OK);
	CHECK(analyze_source(source_prefix + "\temitter.emit_signal(\"tree_entered\", 1)\n") != OK);
	CHECK(analyze_source(source_prefix + "\temitter.emit_signal(\"child_entered_tree\", emitter)\n") == OK);
	CHECK(analyze_source(source_prefix + "\temitter.emit_signal(\"child_entered_tree\", \"bad\")\n") != OK);
	CHECK(analyze_source(source_prefix + "\temitter.emit_signal(\"child_entered_tree\")\n") != OK);
	CHECK(analyze_source(source_prefix + "\temitter.emit_signal(\"child_entered_tree\", emitter, emitter)\n") != OK);
	CHECK(analyze_source(source_prefix + "\temitter.emit_signal(\"child_entered_tree\", get_dynamic())\n") == OK);
	CHECK(analyze_source(source_prefix + "\temitter.emit_signal(\"child_entered_tree\", get_dynamic())\n", false, true) != OK);
	CHECK(analyze_source(dynamic_signal_source) == OK);
	CHECK(analyze_source(source_prefix + "\temitter.emit_signal(\"unknown\", 1)\n") == OK);
}

TEST_CASE("[Modules][GDScript] Analyzer checks native receiver Object signal callables") {
	const String source_prefix = "func accept_none() -> void:\n\tpass\nfunc accept_float(value: float) -> void:\n\tpass\nfunc accept_string(value: String) -> void:\n\tpass\nfunc test(button: Button, range: Range) -> void:\n";
	const String dynamic_signal_source = "func accept_string(value: String) -> void:\n\tpass\nfunc test(button: Button, signal_name: StringName) -> void:\n\tbutton.connect(signal_name, accept_string)\n\tbutton.disconnect(signal_name, accept_string)\n";

	CHECK(analyze_source(source_prefix + "\tbutton.connect(\"pressed\", accept_none)\n\tbutton.disconnect(\"pressed\", accept_none)\n") == OK);
	CHECK(analyze_source(source_prefix + "\tbutton.connect(&\"pressed\", accept_none)\n\tbutton.disconnect(&\"pressed\", accept_none)\n") == OK);
	CHECK(analyze_source(source_prefix + "\tbutton.connect(\"pressed\", accept_string)\n") != OK);
	CHECK(analyze_source(source_prefix + "\tbutton.disconnect(\"pressed\", accept_string)\n") != OK);
	CHECK(analyze_source(source_prefix + "\trange.connect(\"value_changed\", accept_float)\n\trange.disconnect(\"value_changed\", accept_float)\n") == OK);
	CHECK(analyze_source(source_prefix + "\trange.connect(\"value_changed\", accept_string)\n") != OK);
	CHECK(analyze_source(source_prefix + "\trange.disconnect(\"value_changed\", accept_string)\n") != OK);
	CHECK(analyze_source(dynamic_signal_source) == OK);
	CHECK(analyze_source(source_prefix + "\tbutton.connect(\"unknown\", accept_string)\n\tbutton.disconnect(\"unknown\", accept_string)\n") == OK);
}

TEST_CASE("[Modules][GDScript] Analyzer checks native receiver Object.emit_signal arguments") {
	const String source_prefix = "func get_dynamic() -> Variant:\n\treturn 1.0\nfunc test(button: Button, range: Range) -> void:\n";
	const String dynamic_signal_source = "func test(button: Button, signal_name: StringName) -> void:\n\tbutton.emit_signal(signal_name, 1)\n";
	const String dynamic_receiver_source = "func test(button: Variant) -> void:\n\tbutton.emit_signal(\"pressed\", 1)\n";

	CHECK(analyze_source(source_prefix + "\tbutton.emit_signal(\"pressed\")\n") == OK);
	CHECK(analyze_source(source_prefix + "\tbutton.emit_signal(&\"pressed\")\n") == OK);
	CHECK(analyze_source(source_prefix + "\tbutton.emit_signal(\"pressed\", 1)\n") != OK);
	CHECK(analyze_source(source_prefix + "\trange.emit_signal(\"value_changed\", 1.0)\n") == OK);
	CHECK(analyze_source(source_prefix + "\trange.emit_signal(\"value_changed\", \"bad\")\n") != OK);
	CHECK(analyze_source(source_prefix + "\trange.emit_signal(\"value_changed\")\n") != OK);
	CHECK(analyze_source(source_prefix + "\trange.emit_signal(\"value_changed\", 1.0, 2.0)\n") != OK);
	CHECK(analyze_source(source_prefix + "\trange.emit_signal(\"value_changed\", get_dynamic())\n") == OK);
	CHECK(analyze_source(source_prefix + "\trange.emit_signal(\"value_changed\", get_dynamic())\n", false, true) != OK);
	CHECK(analyze_source(dynamic_signal_source) == OK);
	CHECK(analyze_source(source_prefix + "\tbutton.emit_signal(\"unknown\", 1)\n") == OK);
	CHECK(analyze_source(dynamic_receiver_source) == OK);
}

TEST_CASE("[Modules][GDScript] Analyzer can reject null assignments to non-null types") {
	CHECK(analyze_source("var node: Node = null\n") == OK);
	CHECK(analyze_source("var node: Node = null\n", true) != OK);
	CHECK(analyze_source("var node: Node? = null\n", true) == OK);
}

TEST_CASE("[Modules][GDScript] Analyzer can reject null source arguments and returns to non-null types") {
	const String argument_source = "func accept_node(node: Node) -> void:\n\tpass\nfunc test() -> void:\n\taccept_node(null)\n";
	const String nullable_argument_source = "func accept_node(node: Node?) -> void:\n\tpass\nfunc test() -> void:\n\taccept_node(null)\n";
	const String return_source = "func get_node() -> Node:\n\treturn null\n";
	const String nullable_return_source = "func get_node() -> Node?:\n\treturn null\n";

	CHECK(analyze_source(argument_source) == OK);
	CHECK(analyze_source(argument_source, true) != OK);
	CHECK(analyze_source(nullable_argument_source, true) == OK);
	CHECK(analyze_source(return_source) == OK);
	CHECK(analyze_source(return_source, true) != OK);
	CHECK(analyze_source(nullable_return_source, true) == OK);
}

TEST_CASE("[Modules][GDScript] Analyzer narrows nullable locals after null checks") {
	const String source_prefix = "func accept_node(node: Node) -> void:\n\tpass\n";
	const String if_not_null_source = source_prefix + "func test(node: Node?) -> void:\n\tif node != null:\n\t\taccept_node(node)\n";
	const String null_not_equal_source = source_prefix + "func test(node: Node?) -> void:\n\tif null != node:\n\t\taccept_node(node)\n";
	const String else_source = source_prefix + "func test(node: Node?) -> void:\n\tif node == null:\n\t\tpass\n\telse:\n\t\taccept_node(node)\n";
	const String local_variable_source = source_prefix + "func test() -> void:\n\tvar node: Node? = null\n\tif node != null:\n\t\taccept_node(node)\n";
	const String outside_source = source_prefix + "func test(node: Node?) -> void:\n\tif node != null:\n\t\tpass\n\taccept_node(node)\n";
	const String reassigned_source = source_prefix + "func test(node: Node?) -> void:\n\tif node != null:\n\t\tnode = null\n\t\taccept_node(node)\n";

	CHECK(analyze_source(if_not_null_source, true) == OK);
	CHECK(analyze_source(null_not_equal_source, true) == OK);
	CHECK(analyze_source(else_source, true) == OK);
	CHECK(analyze_source(local_variable_source, true) == OK);
	CHECK(analyze_source(outside_source, true) != OK);
	CHECK(analyze_source(reassigned_source, true) != OK);
}

TEST_CASE("[Modules][GDScript] Analyzer keeps nullable narrowing across non-mutating calls") {
	const String source = "func accept_node(node: Node) -> void:\n\tpass\nfunc do_nothing() -> void:\n\tpass\nfunc test(node: Node?) -> void:\n\tif node != null:\n\t\tdo_nothing()\n\t\taccept_node(node)\n";

	CHECK(analyze_source(source, true) == OK);
}

TEST_CASE("[Modules][GDScript] Analyzer invalidates captured nullable narrowing across calls") {
	const String source = "func accept_node(node: Node) -> void:\n\tpass\nfunc do_nothing() -> void:\n\tpass\nfunc test(node: Node?) -> void:\n\tvar read_node := func() -> void:\n\t\tprint(node)\n\tif node != null:\n\t\tdo_nothing()\n\t\taccept_node(node)\n";

	CHECK(analyze_source(source, true) != OK);
}

TEST_CASE("[Modules][GDScript] Analyzer narrows nullable locals after non-null assert") {
	const String source_prefix = "func accept_node(node: Node) -> void:\n\tpass\n";
	const String assert_source = source_prefix + "func test(node: Node?) -> void:\n\tassert(node != null)\n\taccept_node(node)\n";
	const String null_not_equal_source = source_prefix + "func test(node: Node?) -> void:\n\tassert(null != node)\n\taccept_node(node)\n";
	const String reassigned_source = source_prefix + "func test(node: Node?) -> void:\n\tassert(node != null)\n\tnode = null\n\taccept_node(node)\n";

	CHECK(analyze_source(assert_source, true) == OK);
	CHECK(analyze_source(null_not_equal_source, true) == OK);
	CHECK(analyze_source(reassigned_source, true) != OK);
}

TEST_CASE("[Modules][GDScript] Analyzer narrows nullable locals for assignments and returns") {
	const String assignment_source = "func test(maybe: Node?) -> void:\n\tvar node: Node = maybe\n";
	const String narrowed_assignment_source = "func test(maybe: Node?) -> void:\n\tif maybe != null:\n\t\tvar node: Node = maybe\n";
	const String return_source = "func get_node(maybe: Node?) -> Node:\n\treturn maybe\n";
	const String narrowed_return_source = "func get_node(maybe: Node?) -> Node:\n\tassert(maybe != null)\n\treturn maybe\n";

	CHECK(analyze_source(assignment_source, true) != OK);
	CHECK(analyze_source(narrowed_assignment_source, true) == OK);
	CHECK(analyze_source(return_source, true) != OK);
	CHECK(analyze_source(narrowed_return_source, true) == OK);
}

TEST_CASE("[Modules][GDScript] Analyzer narrows Variant locals after type tests") {
	const String source_prefix = "func accept_node(node: Node) -> void:\n\tpass\nfunc accept_button(button: Button) -> void:\n\tpass\n";
	const String node_test_source = source_prefix + "func test(value: Variant) -> void:\n\tif value is Node:\n\t\taccept_node(value)\n";
	const String button_test_source = source_prefix + "func test(value: Variant) -> void:\n\tif value is Button:\n\t\taccept_button(value)\n";
	const String is_not_else_source = source_prefix + "func test(value: Variant) -> void:\n\tif value is not Node:\n\t\tpass\n\telse:\n\t\taccept_node(value)\n";
	const String outside_source = source_prefix + "func test(value: Variant) -> void:\n\tif value is Node:\n\t\tpass\n\taccept_node(value)\n";
	const String reassigned_source = source_prefix + "func test(value: Variant) -> void:\n\tif value is Node:\n\t\tvalue = 1\n\t\taccept_node(value)\n";

	CHECK(analyze_source(node_test_source, false, true) == OK);
	CHECK(analyze_source(button_test_source, false, true) == OK);
	CHECK(analyze_source(is_not_else_source, false, true) == OK);
	CHECK(analyze_source(outside_source, false, true) != OK);
	CHECK(analyze_source(reassigned_source, false, true) != OK);
}

TEST_CASE("[Modules][GDScript] Analyzer propagates typed match pattern bind types") {
	const String source_prefix = "func accept_node(node: Node) -> void:\n\tpass\n";
	const String array_source = source_prefix + "func test(nodes: Array[Node]) -> void:\n\tmatch nodes:\n\t\t[var node]:\n\t\t\taccept_node(node)\n";
	const String dictionary_source = source_prefix + "func test(nodes: Dictionary[String, Node]) -> void:\n\tmatch nodes:\n\t\t{\"node\": var node}:\n\t\t\taccept_node(node)\n";
	const String nested_array_source = source_prefix + "func test(nodes: Array[Array[Node]]) -> void:\n\tmatch nodes:\n\t\t[[var node]]:\n\t\t\taccept_node(node)\n";

	CHECK(analyze_source(array_source, false, true) == OK);
	CHECK(analyze_source(dictionary_source, false, true) == OK);
	CHECK(analyze_source(nested_array_source, false, true) == OK);
}

TEST_CASE("[Modules][GDScript] Analyzer can reject null source callable and signal arguments to non-null types") {
	const String callable_source = "func accept_node(node: Node) -> void:\n\tpass\nfunc test() -> void:\n\tvar callback: Callable[[Node], void] = accept_node\n\tcallback.call(null)\n";
	const String nullable_callable_source = "func accept_node(node: Node?) -> void:\n\tpass\nfunc test() -> void:\n\tvar callback: Callable[[Node?], void] = accept_node\n\tcallback.call(null)\n";
	const String signal_source = "signal event(node: Node)\nfunc test() -> void:\n\tvar typed_event: Signal[[Node]] = event\n\ttyped_event.emit(null)\n";
	const String nullable_signal_source = "signal event(node: Node?)\nfunc test() -> void:\n\tvar typed_event: Signal[[Node?]] = event\n\ttyped_event.emit(null)\n";

	CHECK(analyze_source(callable_source) == OK);
	CHECK(analyze_source(callable_source, true) != OK);
	CHECK(analyze_source(nullable_callable_source, true) == OK);
	CHECK(analyze_source(signal_source) == OK);
	CHECK(analyze_source(signal_source, true) != OK);
	CHECK(analyze_source(nullable_signal_source, true) == OK);
}

TEST_CASE("[Modules][GDScript] Analyzer can reject null elements in non-null typed containers") {
	const String array_source = "var nodes: Array[Node] = [null]\n";
	const String nullable_array_source = "var nodes: Array[Node?] = [null]\n";
	const String dictionary_source = "var nodes: Dictionary[String, Node] = { \"node\": null }\n";
	const String nullable_dictionary_source = "var nodes: Dictionary[String, Node?] = { \"node\": null }\n";

	CHECK(analyze_source(array_source) == OK);
	CHECK(analyze_source(array_source, true) != OK);
	CHECK(analyze_source(nullable_array_source, true) == OK);
	CHECK(analyze_source(dictionary_source) == OK);
	CHECK(analyze_source(dictionary_source, true) != OK);
	CHECK(analyze_source(nullable_dictionary_source, true) == OK);
}

TEST_CASE("[Modules][GDScript] Analyzer can reject null container elements in arguments and returns") {
	const String array_argument_source = "func accept_nodes(nodes: Array[Node]) -> void:\n\tpass\nfunc test() -> void:\n\taccept_nodes([null])\n";
	const String nullable_array_argument_source = "func accept_nodes(nodes: Array[Node?]) -> void:\n\tpass\nfunc test() -> void:\n\taccept_nodes([null])\n";
	const String array_return_source = "func get_nodes() -> Array[Node]:\n\treturn [null]\n";
	const String nullable_array_return_source = "func get_nodes() -> Array[Node?]:\n\treturn [null]\n";
	const String dictionary_argument_source = "func accept_nodes(nodes: Dictionary[String, Node]) -> void:\n\tpass\nfunc test() -> void:\n\taccept_nodes({ \"node\": null })\n";
	const String nullable_dictionary_argument_source = "func accept_nodes(nodes: Dictionary[String, Node?]) -> void:\n\tpass\nfunc test() -> void:\n\taccept_nodes({ \"node\": null })\n";
	const String dictionary_return_source = "func get_nodes() -> Dictionary[String, Node]:\n\treturn { \"node\": null }\n";
	const String nullable_dictionary_return_source = "func get_nodes() -> Dictionary[String, Node?]:\n\treturn { \"node\": null }\n";

	CHECK(analyze_source(array_argument_source) == OK);
	CHECK(analyze_source(array_argument_source, true) != OK);
	CHECK(analyze_source(nullable_array_argument_source, true) == OK);
	CHECK(analyze_source(array_return_source) == OK);
	CHECK(analyze_source(array_return_source, true) != OK);
	CHECK(analyze_source(nullable_array_return_source, true) == OK);
	CHECK(analyze_source(dictionary_argument_source) == OK);
	CHECK(analyze_source(dictionary_argument_source, true) != OK);
	CHECK(analyze_source(nullable_dictionary_argument_source, true) == OK);
	CHECK(analyze_source(dictionary_return_source) == OK);
	CHECK(analyze_source(dictionary_return_source, true) != OK);
	CHECK(analyze_source(nullable_dictionary_return_source, true) == OK);
}

TEST_CASE("[Modules][GDScript] Analyzer can reject dynamic source assignments to static types") {
	const String source = "func get_dynamic() -> Variant:\n\treturn 1\nvar typed_value: int = get_dynamic()\n";

	CHECK(analyze_source(source) == OK);
	CHECK(analyze_source(source, false, true) != OK);
	CHECK(analyze_source("func get_dynamic() -> Variant:\n\treturn 1\nvar dynamic_value: Variant = get_dynamic()\n", false, true) == OK);
}

TEST_CASE("[Modules][GDScript] Analyzer can reject dynamic source arguments and returns to static types") {
	const String argument_source = "func get_dynamic() -> Variant:\n\treturn 1\nfunc accept_int(value: int) -> void:\n\tpass\nfunc test() -> void:\n\taccept_int(get_dynamic())\n";
	const String return_source = "func get_dynamic() -> Variant:\n\treturn 1\nfunc get_int() -> int:\n\treturn get_dynamic()\n";

	CHECK(analyze_source(argument_source) == OK);
	CHECK(analyze_source(argument_source, false, true) != OK);
	CHECK(analyze_source(return_source) == OK);
	CHECK(analyze_source(return_source, false, true) != OK);
}

TEST_CASE("[Modules][GDScript] Analyzer can reject dynamic elements in typed containers") {
	const String array_source = "func get_dynamic() -> Variant:\n\treturn 1\nvar values: Array[int] = [get_dynamic()]\n";
	const String variant_array_source = "func get_dynamic() -> Variant:\n\treturn 1\nvar values: Array[Variant] = [get_dynamic()]\n";
	const String dictionary_source = "func get_dynamic() -> Variant:\n\treturn 1\nvar values: Dictionary[String, int] = { \"value\": get_dynamic() }\n";
	const String variant_dictionary_source = "func get_dynamic() -> Variant:\n\treturn 1\nvar values: Dictionary[String, Variant] = { \"value\": get_dynamic() }\n";
	const String dictionary_key_source = "func get_dynamic() -> Variant:\n\treturn \"value\"\nvar values: Dictionary[String, int] = { get_dynamic(): 1 }\n";
	const String variant_dictionary_key_source = "func get_dynamic() -> Variant:\n\treturn \"value\"\nvar values: Dictionary[Variant, int] = { get_dynamic(): 1 }\n";

	CHECK(analyze_source(array_source) == OK);
	CHECK(analyze_source(array_source, false, true) != OK);
	CHECK(analyze_source(variant_array_source, false, true) == OK);
	CHECK(analyze_source(dictionary_source) == OK);
	CHECK(analyze_source(dictionary_source, false, true) != OK);
	CHECK(analyze_source(variant_dictionary_source, false, true) == OK);
	CHECK(analyze_source(dictionary_key_source) == OK);
	CHECK(analyze_source(dictionary_key_source, false, true) != OK);
	CHECK(analyze_source(variant_dictionary_key_source, false, true) == OK);
}

TEST_CASE("[Modules][GDScript] Analyzer checks typed Array mutation methods") {
	const String source_prefix = "func get_dynamic() -> Variant:\n\treturn 1\nfunc test(values: Array[int], texts: Array[String], untyped: Array) -> void:\n";

	CHECK(analyze_source(source_prefix + "\tvalues.append(1)\n") == OK);
	CHECK(analyze_source(source_prefix + "\tvalues.append(\"bad\")\n") != OK);
	CHECK(analyze_source(source_prefix + "\tvalues.push_back(1)\n") == OK);
	CHECK(analyze_source(source_prefix + "\tvalues.push_back(\"bad\")\n") != OK);
	CHECK(analyze_source(source_prefix + "\tvalues.push_front(1)\n") == OK);
	CHECK(analyze_source(source_prefix + "\tvalues.push_front(\"bad\")\n") != OK);
	CHECK(analyze_source(source_prefix + "\tvalues.insert(0, 1)\n") == OK);
	CHECK(analyze_source(source_prefix + "\tvalues.insert(0, \"bad\")\n") != OK);
	CHECK(analyze_source(source_prefix + "\tvalues.set(0, 1)\n") == OK);
	CHECK(analyze_source(source_prefix + "\tvalues.set(0, \"bad\")\n") != OK);
	CHECK(analyze_source(source_prefix + "\tvalues.fill(1)\n") == OK);
	CHECK(analyze_source(source_prefix + "\tvalues.fill(\"bad\")\n") != OK);
	CHECK(analyze_source(source_prefix + "\tvalues.assign(values)\n") == OK);
	CHECK(analyze_source(source_prefix + "\tvalues.assign(texts)\n") != OK);
	CHECK(analyze_source(source_prefix + "\tvalues.assign(untyped)\n") == OK);
	CHECK(analyze_source(source_prefix + "\tvalues.append(get_dynamic())\n") == OK);
	CHECK(analyze_source(source_prefix + "\tvalues.append(get_dynamic())\n", false, true) != OK);
	CHECK(analyze_source(source_prefix + "\tuntyped.append(\"legacy dynamic\")\n") == OK);
	CHECK(analyze_source(source_prefix + "\tuntyped.push_back(\"legacy dynamic\")\n") == OK);
	CHECK(analyze_source(source_prefix + "\tuntyped.insert(0, \"legacy dynamic\")\n") == OK);
	CHECK(analyze_source(source_prefix + "\tuntyped.set(0, \"legacy dynamic\")\n") == OK);
	CHECK(analyze_source(source_prefix + "\tuntyped.assign(texts)\n") == OK);
}

TEST_CASE("[Modules][GDScript] Analyzer checks typed Array append_array and concatenation") {
	const String source_prefix = "func get_dynamic() -> Variant:\n\treturn []\nfunc test(values: Array[int], texts: Array[String], untyped: Array) -> void:\n";

	CHECK(analyze_source(source_prefix + "\tvalues.append_array(values)\n") == OK);
	CHECK(analyze_source(source_prefix + "\tvalues.append_array(texts)\n") != OK);
	CHECK(analyze_source(source_prefix + "\tvalues.append_array(untyped)\n") == OK);
	CHECK(analyze_source(source_prefix + "\tvalues.append_array(get_dynamic())\n") == OK);
	CHECK(analyze_source(source_prefix + "\tvalues.append_array(get_dynamic())\n", false, true) != OK);
	CHECK(analyze_source(source_prefix + "\tuntyped.append_array(texts)\n") == OK);
	CHECK(analyze_source(source_prefix + "\tvar result: Array[int] = values + values\n") == OK);
	CHECK(analyze_source(source_prefix + "\tvar result: Array[String] = values + values\n") != OK);
	CHECK(analyze_source(source_prefix + "\tvar result: Array = values + texts\n") != OK);
	CHECK(analyze_source(source_prefix + "\tvar result: Array[int] = values + untyped\n") == OK);
	CHECK(analyze_source(source_prefix + "\tvar result: Array[int] = untyped + values\n") == OK);
	CHECK(analyze_source(source_prefix + "\tvalues += values\n") == OK);
	CHECK(analyze_source(source_prefix + "\tvalues += texts\n") != OK);
	CHECK(analyze_source(source_prefix + "\tvalues += untyped\n") == OK);
	CHECK(analyze_source(source_prefix + "\tuntyped += values\n") == OK);
}

TEST_CASE("[Modules][GDScript] Analyzer checks typed Dictionary mutation methods") {
	const String source_prefix = "func get_dynamic_key() -> Variant:\n\treturn \"one\"\nfunc get_dynamic_value() -> Variant:\n\treturn 1\nfunc get_dynamic_dictionary() -> Variant:\n\treturn {}\nfunc test(values: Dictionary[String, int], texts: Dictionary[String, String], int_keys: Dictionary[int, int], untyped: Dictionary) -> void:\n";

	CHECK(analyze_source(source_prefix + "\tvalues.set(\"one\", 1)\n") == OK);
	CHECK(analyze_source(source_prefix + "\tvalues.set(1, 1)\n") != OK);
	CHECK(analyze_source(source_prefix + "\tvalues.set(\"one\", \"bad\")\n") != OK);
	CHECK(analyze_source(source_prefix + "\tvalues.set(get_dynamic_key(), 1)\n") == OK);
	CHECK(analyze_source(source_prefix + "\tvalues.set(get_dynamic_key(), 1)\n", false, true) != OK);
	CHECK(analyze_source(source_prefix + "\tvalues.set(\"one\", get_dynamic_value())\n") == OK);
	CHECK(analyze_source(source_prefix + "\tvalues.set(\"one\", get_dynamic_value())\n", false, true) != OK);
	CHECK(analyze_source(source_prefix + "\tvalues.assign(values)\n") == OK);
	CHECK(analyze_source(source_prefix + "\tvalues.assign(texts)\n") != OK);
	CHECK(analyze_source(source_prefix + "\tvalues.assign(int_keys)\n") != OK);
	CHECK(analyze_source(source_prefix + "\tvalues.assign(untyped)\n") == OK);
	CHECK(analyze_source(source_prefix + "\tvalues.assign(get_dynamic_dictionary())\n") == OK);
	CHECK(analyze_source(source_prefix + "\tvalues.assign(get_dynamic_dictionary())\n", false, true) != OK);
	CHECK(analyze_source(source_prefix + "\tvalues.merge(values)\n") == OK);
	CHECK(analyze_source(source_prefix + "\tvalues.merge(values, true)\n") == OK);
	CHECK(analyze_source(source_prefix + "\tvalues.merge(values, \"bad\")\n") != OK);
	CHECK(analyze_source(source_prefix + "\tvalues.merge(texts)\n") != OK);
	CHECK(analyze_source(source_prefix + "\tvalues.merge(int_keys)\n") != OK);
	CHECK(analyze_source(source_prefix + "\tvalues.merge(untyped)\n") == OK);
	CHECK(analyze_source(source_prefix + "\tvalues.merge(get_dynamic_dictionary())\n") == OK);
	CHECK(analyze_source(source_prefix + "\tvalues.merge(get_dynamic_dictionary())\n", false, true) != OK);
	CHECK(analyze_source(source_prefix + "\tvalues[\"one\"] = 1\n") == OK);
	CHECK(analyze_source(source_prefix + "\tvalues[1] = 1\n") != OK);
	CHECK(analyze_source(source_prefix + "\tvalues[\"one\"] = \"bad\"\n") != OK);
	CHECK(analyze_source(source_prefix + "\tvalues[get_dynamic_key()] = 1\n") == OK);
	CHECK(analyze_source(source_prefix + "\tvalues[get_dynamic_key()] = 1\n", false, true) != OK);
	CHECK(analyze_source(source_prefix + "\tvalues[\"one\"] = get_dynamic_value()\n") == OK);
	CHECK(analyze_source(source_prefix + "\tvalues[\"one\"] = get_dynamic_value()\n", false, true) != OK);
	CHECK(analyze_source(source_prefix + "\tvalues[\"one\"] += 1\n") == OK);
	CHECK(analyze_source(source_prefix + "\tvalues[\"one\"] += \"bad\"\n") != OK);
	CHECK(analyze_source(source_prefix + "\tuntyped.set(1, \"legacy dynamic\")\n") == OK);
	CHECK(analyze_source(source_prefix + "\tuntyped.assign(texts)\n") == OK);
	CHECK(analyze_source(source_prefix + "\tuntyped.merge(texts)\n") == OK);
	CHECK(analyze_source(source_prefix + "\tuntyped[1] = \"legacy dynamic\"\n") == OK);
}

TEST_CASE("[Modules][GDScript] Analyzer propagates typed Dictionary keys and values") {
	const String source_prefix = "func get_dynamic_key() -> Variant:\n\treturn \"one\"\nfunc get_dynamic_value() -> Variant:\n\treturn 1\nfunc get_dynamic_keys() -> Variant:\n\treturn [\"one\"]\nfunc test(values: Dictionary[String, int], texts: Dictionary[String, String], untyped: Dictionary) -> void:\n";

	CHECK(analyze_source(source_prefix + "\tfor key in values:\n\t\tvar typed_key: String = key\n") == OK);
	CHECK(analyze_source(source_prefix + "\tfor key: int in values:\n\t\tpass\n") != OK);
	CHECK(analyze_source(source_prefix + "\tfor key in untyped:\n\t\tvar legacy_dynamic: int = key\n") == OK);
	CHECK(analyze_source(source_prefix + "\tvar keys: Array[String] = values.keys()\n") == OK);
	CHECK(analyze_source(source_prefix + "\tvar keys: Array[int] = values.keys()\n") != OK);
	CHECK(analyze_source(source_prefix + "\tvar entries: Array[int] = values.values()\n") == OK);
	CHECK(analyze_source(source_prefix + "\tvar entries: Array[String] = values.values()\n") != OK);
	CHECK(analyze_source(source_prefix + "\tfor value in values.values():\n\t\tvar typed_value: int = value\n") == OK);
	CHECK(analyze_source(source_prefix + "\tfor value: String in values.values():\n\t\tpass\n") != OK);
	CHECK(analyze_source(source_prefix + "\tvar got: int = values.get(\"one\", 0)\n") == OK);
	CHECK(analyze_source(source_prefix + "\tvar got: String = values.get(\"one\", 0)\n") != OK);
	CHECK(analyze_source(source_prefix + "\tvalues.get(1, 0)\n") != OK);
	CHECK(analyze_source(source_prefix + "\tvalues.get(\"one\", \"bad\")\n") != OK);
	CHECK(analyze_source(source_prefix + "\tvalues.get(get_dynamic_key(), 0)\n") == OK);
	CHECK(analyze_source(source_prefix + "\tvalues.get(get_dynamic_key(), 0)\n", false, true) != OK);
	CHECK(analyze_source(source_prefix + "\tvalues.get(\"one\", get_dynamic_value())\n") == OK);
	CHECK(analyze_source(source_prefix + "\tvalues.get(\"one\", get_dynamic_value())\n", false, true) != OK);
	CHECK(analyze_source(source_prefix + "\tvar added: int = values.get_or_add(\"two\", 2)\n") == OK);
	CHECK(analyze_source(source_prefix + "\tvalues.get_or_add(1, 2)\n") != OK);
	CHECK(analyze_source(source_prefix + "\tvalues.get_or_add(\"two\", \"bad\")\n") != OK);
	CHECK(analyze_source(source_prefix + "\tvalues.has(\"one\")\n") == OK);
	CHECK(analyze_source(source_prefix + "\tvalues.has(1)\n") != OK);
	CHECK(analyze_source(source_prefix + "\tvalues.erase(\"one\")\n") == OK);
	CHECK(analyze_source(source_prefix + "\tvalues.erase(1)\n") != OK);
	CHECK(analyze_source(source_prefix + "\tvalues.has_all([\"one\"])\n") == OK);
	CHECK(analyze_source(source_prefix + "\tvalues.has_all([1])\n") != OK);
	CHECK(analyze_source(source_prefix + "\tvalues.has_all(get_dynamic_keys())\n") == OK);
	CHECK(analyze_source(source_prefix + "\tvalues.has_all(get_dynamic_keys())\n", false, true) != OK);
	CHECK(analyze_source(source_prefix + "\tvar found: String = values.find_key(1)\n") == OK);
	CHECK(analyze_source(source_prefix + "\tvar found: int = values.find_key(1)\n") != OK);
	CHECK(analyze_source(source_prefix + "\tvalues.find_key(\"bad\")\n") != OK);
	CHECK(analyze_source(source_prefix + "\tvar merged_values: Dictionary[String, int] = values.merged(values)\n") == OK);
	CHECK(analyze_source(source_prefix + "\tvalues.merged(texts)\n") != OK);
	CHECK(analyze_source(source_prefix + "\tvar duplicate_values: Dictionary[String, int] = values.duplicate()\n") == OK);
	CHECK(analyze_source(source_prefix + "\tvar duplicate_values: Dictionary[String, int] = values.duplicate_deep()\n") == OK);
	CHECK(analyze_source(source_prefix + "\tuntyped.get(1, \"legacy dynamic\")\n") == OK);
	CHECK(analyze_source(source_prefix + "\tuntyped.keys()\n") == OK);
	CHECK(analyze_source(source_prefix + "\tuntyped.values()\n") == OK);
}

TEST_CASE("[Modules][GDScript] Analyzer preserves callable and signal signatures inside containers") {
	const String source_prefix = "signal event(value: String)\nsignal number_event(value: int)\nfunc accepts_int(value: int) -> bool:\n\treturn true\nfunc accepts_string(value: String) -> bool:\n\treturn true\nfunc get_dynamic() -> Variant:\n\treturn accepts_int\nfunc test() -> void:\n";

	CHECK(analyze_source(source_prefix + "\tvar callbacks: Array[Callable[[int], bool]] = [accepts_int]\n\tvar result: bool = callbacks[0].call(1)\n") == OK);
	CHECK(analyze_source(source_prefix + "\tvar callbacks: Array[Callable[[int], bool]] = [accepts_int]\n\tcallbacks[0].call(\"bad\")\n") != OK);
	CHECK(analyze_source(source_prefix + "\tvar callbacks: Array[Callable[[int], bool]] = [accepts_int]\n\tcallbacks.get(0).call(\"bad\")\n") != OK);
	CHECK(analyze_source(source_prefix + "\tvar callbacks: Array[Callable[[int], bool]] = [accepts_int]\n\tcallbacks.front().call(\"bad\")\n") != OK);
	CHECK(analyze_source(source_prefix + "\tvar callbacks: Array[Callable[[int], bool]] = [accepts_int]\n\tcallbacks.duplicate()[0].call(\"bad\")\n") != OK);
	CHECK(analyze_source(source_prefix + "\tvar callbacks: Array[Callable[[int], bool]] = [accepts_int]\n\tcallbacks.slice(0, 1)[0].call(\"bad\")\n") != OK);
	CHECK(analyze_source(source_prefix + "\tvar callbacks: Array[Callable[[int], bool]] = [accepts_string]\n") != OK);
	CHECK(analyze_source(source_prefix + "\tvar callbacks: Array[Callable[[int], bool]] = []\n\tcallbacks.append(accepts_int)\n") == OK);
	CHECK(analyze_source(source_prefix + "\tvar callbacks: Array[Callable[[int], bool]] = []\n\tcallbacks.append(accepts_string)\n") != OK);
	CHECK(analyze_source(source_prefix + "\tvar callbacks: Array[Callable[[int], bool]] = []\n\tcallbacks.append(get_dynamic())\n") == OK);
	CHECK(analyze_source(source_prefix + "\tvar callbacks: Array[Callable[[int], bool]] = []\n\tcallbacks.append(get_dynamic())\n", false, true) != OK);
	CHECK(analyze_source(source_prefix + "\tvar callbacks: Dictionary[String, Callable[[int], bool]] = { \"ok\": accepts_int }\n\tvar result: bool = callbacks[\"ok\"].call(1)\n") == OK);
	CHECK(analyze_source(source_prefix + "\tvar callbacks: Dictionary[String, Callable[[int], bool]] = { \"ok\": accepts_int }\n\tcallbacks[\"ok\"].call(\"bad\")\n") != OK);
	CHECK(analyze_source(source_prefix + "\tvar callbacks: Dictionary[String, Callable[[int], bool]] = { \"bad\": accepts_string }\n") != OK);
	CHECK(analyze_source(source_prefix + "\tvar callbacks: Dictionary[String, Callable[[int], bool]] = {}\n\tcallbacks.set(\"ok\", accepts_int)\n") == OK);
	CHECK(analyze_source(source_prefix + "\tvar callbacks: Dictionary[String, Callable[[int], bool]] = {}\n\tcallbacks.set(\"bad\", accepts_string)\n") != OK);
	CHECK(analyze_source(source_prefix + "\tvar signals: Array[Signal[[String]]] = [event]\n\tsignals[0].emit(\"ok\")\n") == OK);
	CHECK(analyze_source(source_prefix + "\tvar signals: Array[Signal[[String]]] = [event]\n\tsignals[0].emit(1)\n") != OK);
	CHECK(analyze_source(source_prefix + "\tvar signals: Array[Signal[[String]]] = [event]\n\tsignals.get(0).emit(1)\n") != OK);
	CHECK(analyze_source(source_prefix + "\tvar signals: Array[Signal[[String]]] = [event]\n\tsignals.duplicate()[0].emit(1)\n") != OK);
	CHECK(analyze_source(source_prefix + "\tvar signals: Array[Signal[[String]]] = [number_event]\n") != OK);
	CHECK(analyze_source(source_prefix + "\tvar signals: Array[Signal[[String]]] = []\n\tsignals.append(event)\n") == OK);
	CHECK(analyze_source(source_prefix + "\tvar signals: Array[Signal[[String]]] = []\n\tsignals.append(number_event)\n") != OK);
	CHECK(analyze_source(source_prefix + "\tvar signals: Dictionary[String, Signal[[String]]] = { \"ok\": event }\n\tsignals[\"ok\"].emit(\"ok\")\n") == OK);
	CHECK(analyze_source(source_prefix + "\tvar signals: Dictionary[String, Signal[[String]]] = { \"ok\": event }\n\tsignals[\"ok\"].emit(1)\n") != OK);
	CHECK(analyze_source(source_prefix + "\tvar signals: Dictionary[String, Signal[[String]]] = { \"bad\": number_event }\n") != OK);
	CHECK(analyze_source(source_prefix + "\tvar untyped: Array = []\n\tuntyped.append(accepts_string)\n") == OK);
	CHECK(analyze_source(source_prefix + "\tvar untyped_dictionary: Dictionary = {}\n\tuntyped_dictionary.set(\"callback\", accepts_string)\n") == OK);
}

TEST_CASE("[Modules][GDScript] Analyzer preserves nested container element types") {
	const String source_prefix = "func get_dynamic_array() -> Variant:\n\treturn [1]\nfunc get_dynamic_dictionary() -> Variant:\n\treturn { \"one\": 1 }\nfunc test() -> void:\n";

	CHECK(analyze_source(source_prefix + "\tvar nested: Array[Array[int]] = [[1]]\n\tnested[0].append(2)\n\tvar value: int = nested[0][0]\n") == OK);
	CHECK(analyze_source(source_prefix + "\tvar nested: Array[Array[int]] = [[1]]\n\tnested[0].append(\"bad\")\n") != OK);
	CHECK(analyze_source(source_prefix + "\tvar nested: Array[Array[int]] = [[\"bad\"]]\n") != OK);
	CHECK(analyze_source(source_prefix + "\tvar nested: Array[Array[int]] = []\n\tnested.append([1])\n") == OK);
	CHECK(analyze_source(source_prefix + "\tvar nested: Array[Array[int]] = []\n\tnested.append([\"bad\"])\n") != OK);
	CHECK(analyze_source(source_prefix + "\tvar nested: Dictionary[String, Array[int]] = { \"one\": [1] }\n\tnested[\"one\"].append(2)\n") == OK);
	CHECK(analyze_source(source_prefix + "\tvar nested: Dictionary[String, Array[int]] = { \"one\": [1] }\n\tnested[\"one\"].append(\"bad\")\n") != OK);
	CHECK(analyze_source(source_prefix + "\tvar nested: Dictionary[String, Array[int]] = { \"one\": [\"bad\"] }\n") != OK);
	CHECK(analyze_source(source_prefix + "\tvar nested: Dictionary[String, Array[int]] = {}\n\tnested.set(\"one\", [1])\n") == OK);
	CHECK(analyze_source(source_prefix + "\tvar nested: Dictionary[String, Array[int]] = {}\n\tnested.set(\"one\", [\"bad\"])\n") != OK);
	CHECK(analyze_source(source_prefix + "\tvar nested: Array[Dictionary[String, int]] = [{ \"one\": 1 }]\n\tnested[0].set(\"two\", 2)\n") == OK);
	CHECK(analyze_source(source_prefix + "\tvar nested: Array[Dictionary[String, int]] = [{ \"one\": 1 }]\n\tnested[0].set(\"two\", \"bad\")\n") != OK);
	CHECK(analyze_source(source_prefix + "\tvar nested: Array[Dictionary[String, int]] = [{ \"one\": \"bad\" }]\n") != OK);
	CHECK(analyze_source(source_prefix + "\tvar nested: Dictionary[String, Dictionary[String, int]] = { \"outer\": { \"one\": 1 } }\n\tnested[\"outer\"].set(\"two\", 2)\n") == OK);
	CHECK(analyze_source(source_prefix + "\tvar nested: Dictionary[String, Dictionary[String, int]] = { \"outer\": { \"one\": 1 } }\n\tnested[\"outer\"].set(\"two\", \"bad\")\n") != OK);
	CHECK(analyze_source(source_prefix + "\tvar nested: Array[Array[int]] = [get_dynamic_array()]\n") == OK);
	CHECK(analyze_source(source_prefix + "\tvar nested: Array[Array[int]] = [get_dynamic_array()]\n", false, true) != OK);
	CHECK(analyze_source(source_prefix + "\tvar nested: Dictionary[String, Dictionary[String, int]] = { \"outer\": get_dynamic_dictionary() }\n") == OK);
	CHECK(analyze_source(source_prefix + "\tvar nested: Dictionary[String, Dictionary[String, int]] = { \"outer\": get_dynamic_dictionary() }\n", false, true) != OK);
}

TEST_CASE("[Modules][GDScript] Analyzer can reject dynamic container elements in arguments and returns") {
	const String array_argument_source = "func get_dynamic() -> Variant:\n\treturn 1\nfunc accept_values(values: Array[int]) -> void:\n\tpass\nfunc test() -> void:\n\taccept_values([get_dynamic()])\n";
	const String variant_array_argument_source = "func get_dynamic() -> Variant:\n\treturn 1\nfunc accept_values(values: Array[Variant]) -> void:\n\tpass\nfunc test() -> void:\n\taccept_values([get_dynamic()])\n";
	const String array_return_source = "func get_dynamic() -> Variant:\n\treturn 1\nfunc get_values() -> Array[int]:\n\treturn [get_dynamic()]\n";
	const String variant_array_return_source = "func get_dynamic() -> Variant:\n\treturn 1\nfunc get_values() -> Array[Variant]:\n\treturn [get_dynamic()]\n";
	const String dictionary_argument_source = "func get_dynamic() -> Variant:\n\treturn 1\nfunc accept_values(values: Dictionary[String, int]) -> void:\n\tpass\nfunc test() -> void:\n\taccept_values({ \"value\": get_dynamic() })\n";
	const String variant_dictionary_argument_source = "func get_dynamic() -> Variant:\n\treturn 1\nfunc accept_values(values: Dictionary[String, Variant]) -> void:\n\tpass\nfunc test() -> void:\n\taccept_values({ \"value\": get_dynamic() })\n";
	const String dictionary_return_source = "func get_dynamic() -> Variant:\n\treturn 1\nfunc get_values() -> Dictionary[String, int]:\n\treturn { \"value\": get_dynamic() }\n";
	const String variant_dictionary_return_source = "func get_dynamic() -> Variant:\n\treturn 1\nfunc get_values() -> Dictionary[String, Variant]:\n\treturn { \"value\": get_dynamic() }\n";

	CHECK(analyze_source(array_argument_source) == OK);
	CHECK(analyze_source(array_argument_source, false, true) != OK);
	CHECK(analyze_source(variant_array_argument_source, false, true) == OK);
	CHECK(analyze_source(array_return_source) == OK);
	CHECK(analyze_source(array_return_source, false, true) != OK);
	CHECK(analyze_source(variant_array_return_source, false, true) == OK);
	CHECK(analyze_source(dictionary_argument_source) == OK);
	CHECK(analyze_source(dictionary_argument_source, false, true) != OK);
	CHECK(analyze_source(variant_dictionary_argument_source, false, true) == OK);
	CHECK(analyze_source(dictionary_return_source) == OK);
	CHECK(analyze_source(dictionary_return_source, false, true) != OK);
	CHECK(analyze_source(variant_dictionary_return_source, false, true) == OK);
}

TEST_CASE("[Modules][GDScript] Analyzer reads strict null project setting") {
	const String setting_path = "debug/gdscript/analysis/strict_null_checks";
	ProjectSettings *project_settings = ProjectSettings::get_singleton();
	const Variant previous_value = project_settings->get_setting(setting_path);

	project_settings->set_setting(setting_path, true);
	CHECK(analyze_source_with_project_settings("var node: Node = null\n") != OK);

	project_settings->set_setting(setting_path, previous_value);
}

TEST_CASE("[Modules][GDScript] Analyzer reads strict dynamic project setting") {
	const String setting_path = "debug/gdscript/analysis/strict_dynamic_checks";
	ProjectSettings *project_settings = ProjectSettings::get_singleton();
	const Variant previous_value = project_settings->get_setting(setting_path);

	project_settings->set_setting(setting_path, true);
	CHECK(analyze_source_with_project_settings("func get_dynamic() -> Variant:\n\treturn 1\nvar typed_value: int = get_dynamic()\n") != OK);

	project_settings->set_setting(setting_path, previous_value);
}

} // namespace GDScriptTests
