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

#include "modules/gdscript/editor/gdscript_docgen.h"
#include "modules/gdscript/gdscript.h"
#include "modules/gdscript/gdscript_analyzer.h"
#include "modules/gdscript/gdscript_type.h"

#include "core/config/project_settings.h"

#include "tests/test_macros.h"

namespace GDScriptTests {

// Initializing the language or running the script fixtures enables project warnings globally, and some
// of them default to errors (e.g. NATIVE_METHOD_OVERRIDE). The analyzer-error helpers below validate
// analyzer errors only, so they suppress warnings while analyzing to stay isolated from that global state.
class IgnoreWarningsScope {
#ifdef DEBUG_ENABLED
	bool previous_ignore = false;
#endif

public:
	IgnoreWarningsScope() {
#ifdef DEBUG_ENABLED
		previous_ignore = GDScriptParser::is_ignoring_warnings();
		GDScriptParser::set_ignoring_warnings(true);
#endif
	}
	~IgnoreWarningsScope() {
#ifdef DEBUG_ENABLED
		GDScriptParser::set_ignoring_warnings(previous_ignore);
#endif
	}
};

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

// Mirrors the analyzer's file-static make_coroutine_type: a Coroutine[T] is a NATIVE skin over
// GDScriptFunctionState whose result type T lives in container_element_types[0].
static GDScriptParser::DataType make_coroutine_type(const GDScriptParser::DataType &p_result_type) {
	GDScriptParser::DataType type;
	type.kind = GDScriptParser::DataType::NATIVE;
	type.type_source = GDScriptParser::DataType::ANNOTATED_EXPLICIT;
	type.builtin_type = Variant::OBJECT;
	type.native_type = SNAME("GDScriptFunctionState");
	type.is_coroutine = true;
	type.set_container_element_type(0, p_result_type);
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

// Exposes the analyzer's private PropertyInfo decode path so the encode/decode round-trip used at the
// MethodInfo/PropertyInfo (cross-script) boundary can be exercised directly, independent of the script
// test runner (which parses referenced scripts in-batch and therefore never crosses the serialized
// boundary). See the friend declaration in gdscript_analyzer.h.
class TestGDScriptAnalyzerAccessor {
public:
	static GDScriptParser::DataType decode_property(const PropertyInfo &p_property) {
		GDScriptParser parser;
		GDScriptAnalyzer analyzer(&parser);
		return analyzer.type_from_property(p_property);
	}

	// Resolves the call-result return type the analyzer derives for a method exposed through MethodInfo
	// (the cross-script boundary), including the METHOD_FLAG_ASYNC coroutine wrap.
	static GDScriptParser::DataType method_return_type(const PropertyInfo &p_return_val, bool p_async) {
		GDScriptParser parser;
		GDScriptAnalyzer analyzer(&parser);
		MethodInfo info;
		info.return_val = p_return_val;
		if (p_async) {
			info.flags |= METHOD_FLAG_ASYNC;
		}
		GDScriptParser::DataType return_type;
		List<GDScriptParser::DataType> parameter_types;
		int default_argument_count = 0;
		BitField<MethodFlags> method_flags;
		analyzer.function_signature_from_info(info, return_type, parameter_types, default_argument_count, method_flags);
		return return_type;
	}
};

static Error analyze_source(const String &p_source, bool p_strict_null_checks = false, bool p_strict_dynamic_checks = false) {
	IgnoreWarningsScope ignore_warnings;
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

static PackedStringArray analyze_source_errors(const String &p_source, bool p_strict_null_checks = false, bool p_strict_dynamic_checks = false) {
	IgnoreWarningsScope ignore_warnings;
	GDScriptParser parser;
	Error err = parser.parse(p_source, "user://test.gd", false);
	PackedStringArray errors;
	if (err != OK) {
		for (const GDScriptParser::ParserError &parser_error : parser.get_errors()) {
			errors.push_back(parser_error.message);
		}
		return errors;
	}

	GDScriptAnalyzer analyzer(&parser);
	analyzer.set_strict_null_checks(p_strict_null_checks);
	analyzer.set_strict_dynamic_checks(p_strict_dynamic_checks);
	analyzer.analyze();
	for (const GDScriptParser::ParserError &parser_error : parser.get_errors()) {
		errors.push_back(parser_error.message);
	}
	return errors;
}

static void check_source_error(
		const String &p_source,
		const String &p_expected_error,
		bool p_strict_null_checks = false,
		bool p_strict_dynamic_checks = false) {
	INFO(p_source);
	PackedStringArray errors = analyze_source_errors(p_source, p_strict_null_checks, p_strict_dynamic_checks);
	REQUIRE(errors.size() == 1);
	CHECK_EQ(errors[0], p_expected_error);
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

static void check_legacy_ok_strict_null_rejected(const String &p_source) {
	INFO(p_source);
	CHECK(analyze_source(p_source) == OK);
	CHECK(analyze_source(p_source, true) != OK);
}

static void check_legacy_ok_strict_dynamic_rejected(const String &p_source) {
	INFO(p_source);
	CHECK(analyze_source(p_source) == OK);
	CHECK(analyze_source(p_source, false, true) != OK);
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

TEST_CASE("[Modules][GDScript] Type compatibility accepts null source for nullable builtin and enum targets") {
	const GDScriptParser::DataType source = make_builtin_type(Variant::NIL);

	GDScriptParser::DataType nullable_builtin = make_builtin_type(Variant::BOOL);
	nullable_builtin.is_nullable = true;

	GDScriptParser::DataType nullable_enum;
	nullable_enum.kind = GDScriptParser::DataType::ENUM;
	nullable_enum.type_source = GDScriptParser::DataType::ANNOTATED_EXPLICIT;
	nullable_enum.builtin_type = Variant::INT;
	nullable_enum.native_type = SNAME("Direction");
	nullable_enum.is_nullable = true;

	GDScriptTypeCompatibility::Options options;
	options.strict_null = true;

	const GDScriptTypeCompatibility::Result builtin_result = GDScriptTypeCompatibility::check(nullable_builtin, source, options);
	const GDScriptTypeCompatibility::Result enum_result = GDScriptTypeCompatibility::check(nullable_enum, source, options);

	CHECK(builtin_result.compatible);
	CHECK(enum_result.compatible);

	// A non-nullable builtin still rejects null.
	const GDScriptParser::DataType non_nullable_builtin = make_builtin_type(Variant::BOOL);
	const GDScriptTypeCompatibility::Result non_nullable_result = GDScriptTypeCompatibility::check(non_nullable_builtin, source, options);
	CHECK_FALSE(non_nullable_result.compatible);
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

TEST_CASE("[Modules][GDScript] Type compatibility treats Type handle nullability as outer state") {
	GDScriptParser::DataType target = make_native_type(SNAME("Node"));
	target.is_meta_type = true;
	target.is_type_handle_annotation = true;
	GDScriptParser::DataType source = make_native_type(SNAME("Button"));
	source.is_meta_type = true;
	source.is_type_handle_annotation = true;
	GDScriptParser::DataType nullable_target = target;
	nullable_target.is_nullable = true;
	GDScriptParser::DataType nullable_source = source;
	nullable_source.is_nullable = true;
	GDScriptParser::DataType wrong_source = make_native_type(SNAME("RefCounted"));
	wrong_source.is_meta_type = true;
	wrong_source.is_type_handle_annotation = true;
	wrong_source.is_nullable = true;
	GDScriptTypeCompatibility::Options options;
	options.strict_null = true;

	CHECK_FALSE(GDScriptTypeCompatibility::check(target, nullable_source, options).compatible);
	CHECK(GDScriptTypeCompatibility::check(nullable_target, nullable_source, options).compatible);
	CHECK_FALSE(GDScriptTypeCompatibility::check(nullable_target, wrong_source, options).compatible);
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

static GDScriptParser::DataType make_callable_with_parameter(const GDScriptParser::DataType &p_parameter_type) {
	GDScriptParser::DataType type = make_builtin_type(Variant::CALLABLE);
	type.has_method_signature = true;
	type.has_explicit_method_signature = true;
	type.method_parameter_types.push_back(p_parameter_type);
	type.method_return_type.push_back(make_builtin_type(Variant::NIL));
	type.method_info.return_val.type = Variant::NIL;
	type.method_info.arguments.push_back(PropertyInfo(Variant::CALLABLE, "handler"));
	return type;
}

static GDScriptParser::DataType make_callable_returning(const GDScriptParser::DataType &p_return_type) {
	GDScriptParser::DataType type = make_builtin_type(Variant::CALLABLE);
	type.has_method_signature = true;
	type.has_explicit_method_signature = true;
	type.method_return_type.push_back(p_return_type);
	type.method_info.return_val.type = Variant::CALLABLE;
	return type;
}

TEST_CASE("[Modules][GDScript] Type compatibility recurses through nested callable signatures") {
	const GDScriptParser::DataType inner_int = make_signature_builtin_type(Variant::CALLABLE, Variant::INT, Variant::NIL);
	const GDScriptParser::DataType inner_string = make_signature_builtin_type(Variant::CALLABLE, Variant::STRING, Variant::NIL);

	// A Callable nested inside another Callable's parameter signature must match deeply.
	const GDScriptParser::DataType parameter_target = make_callable_with_parameter(inner_int);
	const GDScriptParser::DataType parameter_matching_source = make_callable_with_parameter(inner_int);
	const GDScriptParser::DataType parameter_mismatched_source = make_callable_with_parameter(inner_string);

	CHECK(GDScriptTypeCompatibility::check(parameter_target, parameter_matching_source).compatible);
	CHECK_FALSE(GDScriptTypeCompatibility::check(parameter_target, parameter_mismatched_source).compatible);

	// A Callable nested inside another Callable's return signature must match deeply.
	const GDScriptParser::DataType return_target = make_callable_returning(inner_int);
	const GDScriptParser::DataType return_matching_source = make_callable_returning(inner_int);
	const GDScriptParser::DataType return_mismatched_source = make_callable_returning(inner_string);

	CHECK(GDScriptTypeCompatibility::check(return_target, return_matching_source).compatible);
	CHECK_FALSE(GDScriptTypeCompatibility::check(return_target, return_mismatched_source).compatible);

	// A Callable hidden inside a typed container slot of another Callable's signature must still
	// match deeply: `Callable[[Array[Callable[[int], void]]], void]` differs from the String variant.
	GDScriptParser::DataType array_of_int = make_builtin_type(Variant::ARRAY);
	array_of_int.set_container_element_type(0, inner_int);
	GDScriptParser::DataType array_of_string = make_builtin_type(Variant::ARRAY);
	array_of_string.set_container_element_type(0, inner_string);

	const GDScriptParser::DataType container_target = make_callable_with_parameter(array_of_int);
	const GDScriptParser::DataType container_matching_source = make_callable_with_parameter(array_of_int);
	const GDScriptParser::DataType container_mismatched_source = make_callable_with_parameter(array_of_string);

	CHECK(GDScriptTypeCompatibility::check(container_target, container_matching_source).compatible);
	CHECK_FALSE(GDScriptTypeCompatibility::check(container_target, container_mismatched_source).compatible);
}

TEST_CASE("[Modules][GDScript] Parser resolves nullable type annotations") {
	GDScriptParser parser;
	Error err = parser.parse("var maybe_node: Node?\nvar maybe_nodes: Array[Node?]\n", "user://nullable_type.gd", false);
	REQUIRE(err == OK);

	GDScriptAnalyzer analyzer(&parser);
	err = analyzer.analyze();
	REQUIRE(err == OK);

	const GDScriptParser::ClassNode *root_class = parser.get_tree();
	REQUIRE(root_class != nullptr);
	REQUIRE(root_class->members.size() == 2);
	REQUIRE(root_class->members[0].type == GDScriptParser::ClassNode::Member::VARIABLE);
	REQUIRE(root_class->members[1].type == GDScriptParser::ClassNode::Member::VARIABLE);

	const GDScriptParser::VariableNode *variable = root_class->members[0].variable;
	REQUIRE(variable != nullptr);
	const GDScriptParser::DataType variable_type = variable->get_datatype();

	CHECK(variable_type.is_nullable);
	CHECK(variable_type.to_string() == "Node?");

	const GDScriptParser::VariableNode *array_variable = root_class->members[1].variable;
	REQUIRE(array_variable != nullptr);
	const GDScriptParser::DataType array_type = array_variable->get_datatype();

	CHECK(array_type.has_container_element_type(0));
	CHECK(array_type.get_container_element_type(0).is_nullable);
	CHECK(array_type.to_string() == "Array[Node?]");
}

TEST_CASE("[Modules][GDScript] Analyzer resolves Type annotations as class-handle expectations") {
	GDScriptParser parser;
	const String source = "class User:\n"
						  "\tpass\n\n"
						  "var user_type: Type[User]\n"
						  "var maybe_user_type: Type[User]?\n";
	Error err = parser.parse(source, "user://type_metatype_annotation.gd", false);
	REQUIRE(err == OK);

	GDScriptAnalyzer analyzer(&parser);
	err = analyzer.analyze();
	REQUIRE(err == OK);

	const GDScriptParser::ClassNode *root_class = parser.get_tree();
	REQUIRE(root_class != nullptr);
	REQUIRE(root_class->members.size() == 3);
	REQUIRE(root_class->members[1].type == GDScriptParser::ClassNode::Member::VARIABLE);
	REQUIRE(root_class->members[2].type == GDScriptParser::ClassNode::Member::VARIABLE);

	const GDScriptParser::VariableNode *variable = root_class->members[1].variable;
	REQUIRE(variable != nullptr);
	const GDScriptParser::DataType variable_type = variable->get_datatype();

	CHECK(variable_type.kind == GDScriptParser::DataType::CLASS);
	CHECK(variable_type.class_type != nullptr);
	CHECK(variable_type.is_meta_type);
	CHECK(variable_type.is_type_handle_annotation);
	CHECK(variable_type.to_string() == "Type[User]");

	const GDScriptParser::VariableNode *nullable_variable = root_class->members[2].variable;
	REQUIRE(nullable_variable != nullptr);
	const GDScriptParser::DataType nullable_variable_type = nullable_variable->get_datatype();

	CHECK(nullable_variable_type.is_nullable);
	CHECK(nullable_variable_type.is_type_handle_annotation);
	CHECK(nullable_variable_type.to_string() == "Type[User]?");
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

	const GDScriptParser::ClassNode *root_class = parser.get_tree();
	CHECK(root_class != nullptr);
	if (root_class == nullptr) {
		return;
	}
	CHECK(root_class->members.size() == 4);
	if (root_class->members.size() != 4) {
		return;
	}
	CHECK(root_class->members[0].type == GDScriptParser::ClassNode::Member::VARIABLE);
	CHECK(root_class->members[1].type == GDScriptParser::ClassNode::Member::VARIABLE);
	CHECK(root_class->members[2].type == GDScriptParser::ClassNode::Member::VARIABLE);
	CHECK(root_class->members[3].type == GDScriptParser::ClassNode::Member::VARIABLE);
	if (root_class->members[0].type != GDScriptParser::ClassNode::Member::VARIABLE || root_class->members[1].type != GDScriptParser::ClassNode::Member::VARIABLE || root_class->members[2].type != GDScriptParser::ClassNode::Member::VARIABLE || root_class->members[3].type != GDScriptParser::ClassNode::Member::VARIABLE) {
		return;
	}

	const GDScriptParser::DataType callable_type = root_class->members[0].variable->get_datatype();
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

	const GDScriptParser::DataType signal_type = root_class->members[1].variable->get_datatype();
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

	const GDScriptParser::DataType nullable_callable_type = root_class->members[2].variable->get_datatype();
	CHECK(nullable_callable_type.to_string() == "Callable[[Node?], void]");
	const GDScriptParser::DataType nullable_signal_type = root_class->members[3].variable->get_datatype();
	CHECK(nullable_signal_type.to_string() == "Signal[[Node?]]");
}

TEST_CASE("[Modules][GDScript] AsyncCallable stringifies distinctly from Callable") {
	GDScriptParser parser;
	Error err = parser.parse("var explicit: AsyncCallable[[int], bool]\nvar bare: AsyncCallable\n", "user://async_callable_type.gd", false);
	REQUIRE(err == OK);

	GDScriptAnalyzer analyzer(&parser);
	err = analyzer.analyze();
	REQUIRE(err == OK);

	const GDScriptParser::ClassNode *root_class = parser.get_tree();
	REQUIRE(root_class != nullptr);
	REQUIRE(root_class->members.size() == 2);

	const GDScriptParser::DataType explicit_type = root_class->members[0].variable->get_datatype();
	CHECK(explicit_type.builtin_type == Variant::CALLABLE);
	CHECK(explicit_type.signature_is_async);
	CHECK(explicit_type.has_explicit_method_signature);
	CHECK(explicit_type.to_string() == "AsyncCallable[[int], bool]");

	const GDScriptParser::DataType bare_type = root_class->members[1].variable->get_datatype();
	CHECK(bare_type.builtin_type == Variant::CALLABLE);
	CHECK(bare_type.signature_is_async);
	CHECK(bare_type.to_string() == "AsyncCallable");
}

TEST_CASE("[Modules][GDScript] AsyncCallable async marker survives the PropertyInfo boundary") {
	// A typed AsyncCallable that crosses the MethodInfo/PropertyInfo boundary (e.g. exposed by another
	// compiled script or round-tripped through a property hint string) must keep its async marker, so it
	// stays distinct from a plain Callable. This exercises the encode (DataType::to_property_info) and
	// decode (GDScriptAnalyzer::type_from_property) pair directly.

	GDScriptParser::DataType async_callable = make_signature_builtin_type(Variant::CALLABLE, Variant::INT, Variant::NIL);
	async_callable.signature_is_async = true;

	const PropertyInfo async_info = async_callable.to_property_info("handler");
	CHECK(async_info.type == Variant::CALLABLE);
	CHECK(async_info.hint == PROPERTY_HINT_CALLABLE_TYPE);
	// The async marker is encoded as a leading token on the hint string (the type name itself cannot
	// express AsyncCallable through PropertyInfo).
	CHECK(async_info.hint_string.begins_with("async "));

	const GDScriptParser::DataType decoded_async = TestGDScriptAnalyzerAccessor::decode_property(async_info);
	CHECK(decoded_async.builtin_type == Variant::CALLABLE);
	CHECK(decoded_async.signature_is_async);
	CHECK(decoded_async.has_explicit_method_signature);
	REQUIRE(decoded_async.method_parameter_types.size() == 1);
	CHECK(decoded_async.method_parameter_types[0].builtin_type == Variant::INT);

	// A bare AsyncCallable (async marker, no explicit signature) also keeps its marker across the
	// boundary even though it carries no signature suffix.
	GDScriptParser::DataType bare_async = make_builtin_type(Variant::CALLABLE);
	bare_async.signature_is_async = true;
	const PropertyInfo bare_async_info = bare_async.to_property_info("handler");
	CHECK(bare_async_info.type == Variant::CALLABLE);
	CHECK(bare_async_info.hint == PROPERTY_HINT_CALLABLE_TYPE);
	CHECK(bare_async_info.hint_string == "async");
	const GDScriptParser::DataType decoded_bare_async = TestGDScriptAnalyzerAccessor::decode_property(bare_async_info);
	CHECK(decoded_bare_async.builtin_type == Variant::CALLABLE);
	CHECK(decoded_bare_async.signature_is_async);
	CHECK_FALSE(decoded_bare_async.has_explicit_method_signature);

	// A bare (synchronous) Callable stays untyped with no hint and no marker.
	const GDScriptParser::DataType bare_sync = make_builtin_type(Variant::CALLABLE);
	const PropertyInfo bare_sync_info = bare_sync.to_property_info("handler");
	CHECK(bare_sync_info.hint == PROPERTY_HINT_NONE);
	const GDScriptParser::DataType decoded_bare_sync = TestGDScriptAnalyzerAccessor::decode_property(bare_sync_info);
	CHECK_FALSE(decoded_bare_sync.signature_is_async);

	// A plain (synchronous) Callable must not gain the marker on the same round-trip.
	const GDScriptParser::DataType sync_callable = make_signature_builtin_type(Variant::CALLABLE, Variant::INT, Variant::NIL);
	const PropertyInfo sync_info = sync_callable.to_property_info("handler");
	CHECK(sync_info.hint == PROPERTY_HINT_CALLABLE_TYPE);
	CHECK_FALSE(sync_info.hint_string.begins_with("async "));
	const GDScriptParser::DataType decoded_sync = TestGDScriptAnalyzerAccessor::decode_property(sync_info);
	CHECK(decoded_sync.builtin_type == Variant::CALLABLE);
	CHECK_FALSE(decoded_sync.signature_is_async);

	// After the round-trip the two signatures stay incompatible: an async value cannot satisfy a sync
	// target, mirroring the in-memory rule.
	CHECK_FALSE(GDScriptTypeCompatibility::check(decoded_sync, decoded_async).compatible);

	// A nested AsyncCallable inside a Signal parameter keeps its async marker across the same boundary.
	GDScriptParser::DataType signal_with_async = make_builtin_type(Variant::SIGNAL);
	signal_with_async.has_method_signature = true;
	signal_with_async.has_explicit_method_signature = true;
	signal_with_async.method_parameter_types.push_back(async_callable);
	signal_with_async.method_info.arguments.push_back(async_callable.to_property_info("cb"));

	const PropertyInfo signal_info = signal_with_async.to_property_info("evt");
	CHECK(signal_info.type == Variant::SIGNAL);
	CHECK(signal_info.hint == PROPERTY_HINT_CALLABLE_TYPE);
	// Signals are never async, so the suffix carries the marker on the nested callable name instead.
	CHECK_FALSE(signal_info.hint_string.begins_with("async "));
	CHECK(signal_info.hint_string.contains("AsyncCallable"));

	const GDScriptParser::DataType decoded_signal = TestGDScriptAnalyzerAccessor::decode_property(signal_info);
	CHECK(decoded_signal.builtin_type == Variant::SIGNAL);
	CHECK_FALSE(decoded_signal.signature_is_async);
	REQUIRE(decoded_signal.method_parameter_types.size() == 1);
	CHECK(decoded_signal.method_parameter_types[0].builtin_type == Variant::CALLABLE);
	CHECK(decoded_signal.method_parameter_types[0].signature_is_async);
}

TEST_CASE("[Modules][GDScript] Coroutine result type survives the PropertyInfo boundary") {
	// A Coroutine[T] that crosses the MethodInfo/PropertyInfo boundary (e.g. a synchronous method on
	// another compiled script that returns an in-flight handle) must keep both its coroutine identity and
	// the phantom result type T. Without the dedicated hint it would leak as a bare GDScriptFunctionState,
	// so a cross-script consumer would lose T and the ability to await it as T. This exercises the encode
	// (DataType::to_property_info) and decode (GDScriptAnalyzer::type_from_property) pair directly.

	// Coroutine[String]: a builtin result type round-trips exactly.
	GDScriptParser::DataType coroutine_string = make_coroutine_type(make_builtin_type(Variant::STRING));
	const PropertyInfo string_info = coroutine_string.to_property_info("handle");
	CHECK(string_info.type == Variant::OBJECT);
	CHECK(string_info.class_name == StringName("GDScriptFunctionState"));
	CHECK(string_info.hint == PROPERTY_HINT_COROUTINE_TYPE);
	CHECK(string_info.hint_string == "String");

	const GDScriptParser::DataType decoded_string = TestGDScriptAnalyzerAccessor::decode_property(string_info);
	CHECK(decoded_string.kind == GDScriptParser::DataType::NATIVE);
	CHECK(decoded_string.is_coroutine);
	CHECK(decoded_string.native_type == StringName("GDScriptFunctionState"));
	REQUIRE(decoded_string.has_container_element_type(0));
	CHECK(decoded_string.get_container_element_type(0).builtin_type == Variant::STRING);
	CHECK(decoded_string.to_string() == "Coroutine[String]");

	// Coroutine[void]: a NIL result is encoded as "void" and rebuilt as a NIL result.
	GDScriptParser::DataType coroutine_void = make_coroutine_type(make_builtin_type(Variant::NIL));
	const PropertyInfo void_info = coroutine_void.to_property_info("handle");
	CHECK(void_info.hint == PROPERTY_HINT_COROUTINE_TYPE);
	CHECK(void_info.hint_string == "void");
	const GDScriptParser::DataType decoded_void = TestGDScriptAnalyzerAccessor::decode_property(void_info);
	CHECK(decoded_void.is_coroutine);
	REQUIRE(decoded_void.has_container_element_type(0));
	CHECK(decoded_void.get_container_element_type(0).builtin_type == Variant::NIL);
	CHECK(decoded_void.to_string() == "Coroutine[void]");

	// An explicit Coroutine[Variant] keeps a Variant result slot across the boundary.
	GDScriptParser::DataType coroutine_variant = make_coroutine_type(make_variant_type());
	const PropertyInfo variant_info = coroutine_variant.to_property_info("handle");
	CHECK(variant_info.hint == PROPERTY_HINT_COROUTINE_TYPE);
	CHECK(variant_info.hint_string == "Variant");
	const GDScriptParser::DataType decoded_variant = TestGDScriptAnalyzerAccessor::decode_property(variant_info);
	CHECK(decoded_variant.is_coroutine);
	REQUIRE(decoded_variant.has_container_element_type(0));
	CHECK(decoded_variant.get_container_element_type(0).kind == GDScriptParser::DataType::VARIANT);

	// A coroutine with no result slot round-trips as a result-less coroutine (empty hint), staying
	// awaitable and gradually compatible rather than masquerading as a concrete Coroutine[Variant].
	GDScriptParser::DataType coroutine_bare;
	coroutine_bare.kind = GDScriptParser::DataType::NATIVE;
	coroutine_bare.type_source = GDScriptParser::DataType::ANNOTATED_EXPLICIT;
	coroutine_bare.builtin_type = Variant::OBJECT;
	coroutine_bare.native_type = SNAME("GDScriptFunctionState");
	coroutine_bare.is_coroutine = true;
	const PropertyInfo bare_info = coroutine_bare.to_property_info("handle");
	CHECK(bare_info.hint == PROPERTY_HINT_COROUTINE_TYPE);
	CHECK(bare_info.hint_string == "");
	const GDScriptParser::DataType decoded_bare = TestGDScriptAnalyzerAccessor::decode_property(bare_info);
	CHECK(decoded_bare.is_coroutine);
	CHECK_FALSE(decoded_bare.has_container_element_type(0));

	// A result type that cannot round-trip faithfully (here a type parameter) is dropped, so the handle
	// crosses as a result-less coroutine instead of a fake Coroutine[Variant].
	GDScriptParser::DataType type_parameter;
	type_parameter.kind = GDScriptParser::DataType::TYPE_PARAMETER;
	type_parameter.type_source = GDScriptParser::DataType::ANNOTATED_EXPLICIT;
	GDScriptParser::DataType coroutine_lossy = make_coroutine_type(type_parameter);
	const PropertyInfo lossy_info = coroutine_lossy.to_property_info("handle");
	CHECK(lossy_info.hint == PROPERTY_HINT_COROUTINE_TYPE);
	CHECK(lossy_info.hint_string == "");
	const GDScriptParser::DataType decoded_lossy = TestGDScriptAnalyzerAccessor::decode_property(lossy_info);
	CHECK(decoded_lossy.is_coroutine);
	CHECK_FALSE(decoded_lossy.has_container_element_type(0));

	// A nested Array result (Coroutine[Array[int]]) round-trips through the recursive grammar.
	GDScriptParser::DataType array_int = make_builtin_type(Variant::ARRAY);
	array_int.set_container_element_type(0, make_builtin_type(Variant::INT));
	GDScriptParser::DataType coroutine_array = make_coroutine_type(array_int);
	const PropertyInfo array_info = coroutine_array.to_property_info("handle");
	CHECK(array_info.hint == PROPERTY_HINT_COROUTINE_TYPE);
	CHECK(array_info.hint_string == "Array[int]");
	const GDScriptParser::DataType decoded_array = TestGDScriptAnalyzerAccessor::decode_property(array_info);
	CHECK(decoded_array.is_coroutine);
	REQUIRE(decoded_array.has_container_element_type(0));
	CHECK(decoded_array.get_container_element_type(0).builtin_type == Variant::ARRAY);
	REQUIRE(decoded_array.get_container_element_type(0).has_container_element_type(0));
	CHECK(decoded_array.get_container_element_type(0).get_container_element_type(0).builtin_type == Variant::INT);

	// A native result type round-trips, and the decoded coroutine compares equal to the in-memory one.
	GDScriptParser::DataType coroutine_native = make_coroutine_type(make_native_type(SNAME("RefCounted")));
	const PropertyInfo native_info = coroutine_native.to_property_info("handle");
	CHECK(native_info.hint == PROPERTY_HINT_COROUTINE_TYPE);
	CHECK(native_info.hint_string == "RefCounted");
	const GDScriptParser::DataType decoded_native = TestGDScriptAnalyzerAccessor::decode_property(native_info);
	CHECK(GDScriptTypeCompatibility::check(coroutine_native, decoded_native).compatible);
	CHECK(GDScriptTypeCompatibility::check(decoded_native, coroutine_native).compatible);
}

static GDScriptParser::DataType make_array_of(const GDScriptParser::DataType &p_element) {
	GDScriptParser::DataType array = make_builtin_type(Variant::ARRAY);
	array.set_container_element_type(0, p_element);
	return array;
}

static GDScriptParser::DataType make_dictionary_of(const GDScriptParser::DataType &p_key, const GDScriptParser::DataType &p_value) {
	GDScriptParser::DataType dictionary = make_builtin_type(Variant::DICTIONARY);
	dictionary.set_container_element_type(0, p_key);
	dictionary.set_container_element_type(1, p_value);
	return dictionary;
}

TEST_CASE("[Modules][GDScript] Coroutine container element types survive the PropertyInfo boundary") {
	// A Coroutine[T] nested as a typed-array element or a typed-dictionary key/value must keep both its
	// coroutine identity and the phantom result type T across the MethodInfo/PropertyInfo (cross-script)
	// boundary. Without the dedicated element encoding the coroutine element would leak as a bare
	// GDScriptFunctionState, so a cross-script consumer of `Array[Coroutine[String]]` would lose T and the
	// ability to await each element as String. This exercises the encode (DataType::to_property_info) and
	// decode (GDScriptAnalyzer::type_from_property) pair directly, which the in-batch script test runner
	// never crosses.

	// Array[Coroutine[String]]: a builtin result type round-trips exactly.
	const GDScriptParser::DataType array_coroutine_string = make_array_of(make_coroutine_type(make_builtin_type(Variant::STRING)));
	const PropertyInfo array_info = array_coroutine_string.to_property_info("jobs");
	CHECK(array_info.type == Variant::ARRAY);
	CHECK(array_info.hint == PROPERTY_HINT_ARRAY_TYPE);
	CHECK(array_info.hint_string == "Coroutine[String]");

	const GDScriptParser::DataType decoded_array = TestGDScriptAnalyzerAccessor::decode_property(array_info);
	CHECK(decoded_array.builtin_type == Variant::ARRAY);
	REQUIRE(decoded_array.has_container_element_type(0));
	const GDScriptParser::DataType decoded_element = decoded_array.get_container_element_type(0);
	CHECK(decoded_element.kind == GDScriptParser::DataType::NATIVE);
	CHECK(decoded_element.is_coroutine);
	CHECK(decoded_element.native_type == StringName("GDScriptFunctionState"));
	REQUIRE(decoded_element.has_container_element_type(0));
	CHECK(decoded_element.get_container_element_type(0).builtin_type == Variant::STRING);
	CHECK(decoded_array.to_string() == "Array[Coroutine[String]]");

	// Array[Coroutine[void]]: a NIL result is encoded as "void" and rebuilt as a NIL result.
	const GDScriptParser::DataType array_coroutine_void = make_array_of(make_coroutine_type(make_builtin_type(Variant::NIL)));
	const PropertyInfo void_info = array_coroutine_void.to_property_info("jobs");
	CHECK(void_info.hint_string == "Coroutine[void]");
	const GDScriptParser::DataType decoded_void = TestGDScriptAnalyzerAccessor::decode_property(void_info);
	REQUIRE(decoded_void.has_container_element_type(0));
	CHECK(decoded_void.get_container_element_type(0).is_coroutine);
	CHECK(decoded_void.to_string() == "Array[Coroutine[void]]");

	// Array[Coroutine[Array[int]]]: a nested container result round-trips through the recursive grammar.
	GDScriptParser::DataType array_int = make_builtin_type(Variant::ARRAY);
	array_int.set_container_element_type(0, make_builtin_type(Variant::INT));
	const GDScriptParser::DataType array_coroutine_array = make_array_of(make_coroutine_type(array_int));
	const PropertyInfo nested_info = array_coroutine_array.to_property_info("jobs");
	CHECK(nested_info.hint_string == "Coroutine[Array[int]]");
	const GDScriptParser::DataType decoded_nested = TestGDScriptAnalyzerAccessor::decode_property(nested_info);
	CHECK(decoded_nested.to_string() == "Array[Coroutine[Array[int]]]");

	// Array[Coroutine] (no result slot) round-trips as a result-less coroutine element. It is encoded as
	// the bracketed "Coroutine[]" so the element stays unambiguous: a bare "Coroutine" would collide with
	// an ordinary class named Coroutine. It stays awaitable rather than masquerading as Coroutine[Variant].
	GDScriptParser::DataType bare_coroutine;
	bare_coroutine.kind = GDScriptParser::DataType::NATIVE;
	bare_coroutine.type_source = GDScriptParser::DataType::ANNOTATED_EXPLICIT;
	bare_coroutine.builtin_type = Variant::OBJECT;
	bare_coroutine.native_type = SNAME("GDScriptFunctionState");
	bare_coroutine.is_coroutine = true;
	const PropertyInfo bare_info = make_array_of(bare_coroutine).to_property_info("jobs");
	CHECK(bare_info.hint_string == "Coroutine[]");
	const GDScriptParser::DataType decoded_bare = TestGDScriptAnalyzerAccessor::decode_property(bare_info);
	REQUIRE(decoded_bare.has_container_element_type(0));
	CHECK(decoded_bare.get_container_element_type(0).is_coroutine);
	CHECK_FALSE(decoded_bare.get_container_element_type(0).has_container_element_type(0));

	// A coroutine element whose result type cannot round-trip faithfully (here a type parameter) drops the
	// result, crossing as a result-less coroutine element instead of a fake Coroutine[Variant].
	GDScriptParser::DataType type_parameter;
	type_parameter.kind = GDScriptParser::DataType::TYPE_PARAMETER;
	type_parameter.type_source = GDScriptParser::DataType::ANNOTATED_EXPLICIT;
	const PropertyInfo lossy_info = make_array_of(make_coroutine_type(type_parameter)).to_property_info("jobs");
	CHECK(lossy_info.hint_string == "Coroutine[]");
	const GDScriptParser::DataType decoded_lossy = TestGDScriptAnalyzerAccessor::decode_property(lossy_info);
	REQUIRE(decoded_lossy.has_container_element_type(0));
	CHECK(decoded_lossy.get_container_element_type(0).is_coroutine);
	CHECK_FALSE(decoded_lossy.get_container_element_type(0).has_container_element_type(0));

	// Dictionary[String, Coroutine[int]]: a coroutine value round-trips while the key stays a plain leaf.
	const GDScriptParser::DataType dict_value_coroutine = make_dictionary_of(make_builtin_type(Variant::STRING), make_coroutine_type(make_builtin_type(Variant::INT)));
	const PropertyInfo dict_value_info = dict_value_coroutine.to_property_info("table");
	CHECK(dict_value_info.type == Variant::DICTIONARY);
	CHECK(dict_value_info.hint == PROPERTY_HINT_DICTIONARY_TYPE);
	CHECK(dict_value_info.hint_string == "String;Coroutine[int]");
	const GDScriptParser::DataType decoded_dict_value = TestGDScriptAnalyzerAccessor::decode_property(dict_value_info);
	REQUIRE(decoded_dict_value.has_container_element_types());
	CHECK(decoded_dict_value.get_container_element_type(0).builtin_type == Variant::STRING);
	CHECK(decoded_dict_value.get_container_element_type(1).is_coroutine);
	CHECK(decoded_dict_value.to_string() == "Dictionary[String, Coroutine[int]]");

	// Dictionary[Coroutine[int], String]: a coroutine key round-trips while the value stays a plain leaf.
	const GDScriptParser::DataType dict_key_coroutine = make_dictionary_of(make_coroutine_type(make_builtin_type(Variant::INT)), make_builtin_type(Variant::STRING));
	const PropertyInfo dict_key_info = dict_key_coroutine.to_property_info("table");
	CHECK(dict_key_info.hint_string == "Coroutine[int];String");
	const GDScriptParser::DataType decoded_dict_key = TestGDScriptAnalyzerAccessor::decode_property(dict_key_info);
	REQUIRE(decoded_dict_key.has_container_element_types());
	CHECK(decoded_dict_key.get_container_element_type(0).is_coroutine);
	CHECK(decoded_dict_key.get_container_element_type(1).builtin_type == Variant::STRING);
	CHECK(decoded_dict_key.to_string() == "Dictionary[Coroutine[int], String]");

	// A plain (non-coroutine) native array element keeps its bare class-name hint and decodes back as an
	// ordinary native element, never as a coroutine: only the bracketed element form is treated as a
	// coroutine, so an array of an ordinary class (including one literally named Coroutine) stays safe.
	const GDScriptParser::DataType array_native = make_array_of(make_native_type(SNAME("RefCounted")));
	const PropertyInfo native_array_info = array_native.to_property_info("handles");
	CHECK(native_array_info.hint_string == "RefCounted");
	const GDScriptParser::DataType decoded_native_array = TestGDScriptAnalyzerAccessor::decode_property(native_array_info);
	REQUIRE(decoded_native_array.has_container_element_type(0));
	CHECK(decoded_native_array.get_container_element_type(0).kind == GDScriptParser::DataType::NATIVE);
	CHECK_FALSE(decoded_native_array.get_container_element_type(0).is_coroutine);

	// The decoded Array[Coroutine[String]] compares equal both ways with the in-memory original, so a
	// cross-script consumer can both receive and assign the typed container without a false mismatch.
	CHECK(GDScriptTypeCompatibility::check(array_coroutine_string, decoded_array).compatible);
	CHECK(GDScriptTypeCompatibility::check(decoded_array, array_coroutine_string).compatible);
}

TEST_CASE("[Modules][GDScript] Callable/Signal signature with a class named Coroutine does not collide with the coroutine skin") {
	// The Callable/Signal signature grammar serializes an ordinary class/native named "Coroutine" to the
	// bare leaf token "Coroutine"; only the reserved coroutine syntax carries brackets ("Coroutine[T]").
	// A bare-leaf decoder branch would misread such a class as the GDScriptFunctionState coroutine skin
	// when it crosses the MethodInfo/PropertyInfo boundary, silently corrupting its type. The decoder must
	// treat only the bracketed form as a coroutine, so a class literally named "Coroutine" round-trips as
	// that class while a genuine Coroutine[T] still round-trips as the coroutine skin.

	// A real, exposed native class literally named "Coroutine" (e.g. a GDExtension class) is the only kind of
	// "Coroutine"-named slot the signature grammar actually encodes: script/class leaves cross untyped, and a
	// bare native name only round-trips when ClassDB knows it. Such a native serializes to the bare leaf
	// "Coroutine" (the reserved coroutine syntax always carries brackets), so its cross-script PropertyInfo
	// carries a Callable/Signal signature suffix whose parameter/return leaf is the bare token "Coroutine".
	// The bug lives in the decoder (GDScriptAnalyzer::type_from_property -> _decode_signature_type_base), so we
	// drive it directly with the exact hint strings such a native produces, independent of ClassDB state.
	const auto make_callable_signature = [](const String &p_suffix) {
		PropertyInfo info;
		info.type = Variant::CALLABLE;
		info.hint = PROPERTY_HINT_CALLABLE_TYPE;
		info.hint_string = p_suffix;
		return info;
	};
	const auto make_signal_signature = [](const String &p_suffix) {
		PropertyInfo info;
		info.type = Variant::SIGNAL;
		info.hint = PROPERTY_HINT_CALLABLE_TYPE;
		info.hint_string = p_suffix;
		return info;
	};

	// A Callable whose single parameter is the class named "Coroutine" must not decode back as the coroutine
	// skin: the bare leaf is an ordinary class name, never a result-less coroutine.
	const GDScriptParser::DataType decoded_callable_class =
			TestGDScriptAnalyzerAccessor::decode_property(make_callable_signature("[[Coroutine], void]"));
	CHECK(decoded_callable_class.builtin_type == Variant::CALLABLE);
	REQUIRE(decoded_callable_class.method_parameter_types.size() == 1);
	const GDScriptParser::DataType decoded_class_param = decoded_callable_class.method_parameter_types[0];
	CHECK_FALSE(decoded_class_param.is_coroutine);
	CHECK(decoded_class_param.native_type != StringName("GDScriptFunctionState"));

	// The same class as a Callable return slot must also stay un-skinned.
	const GDScriptParser::DataType decoded_callable_return =
			TestGDScriptAnalyzerAccessor::decode_property(make_callable_signature("[[], Coroutine]"));
	REQUIRE(decoded_callable_return.method_return_type.size() == 1);
	CHECK_FALSE(decoded_callable_return.method_return_type[0].is_coroutine);
	CHECK(decoded_callable_return.method_return_type[0].native_type != StringName("GDScriptFunctionState"));

	// A Signal carrying the class named "Coroutine" as a parameter keeps the same disambiguation.
	const GDScriptParser::DataType decoded_signal_class =
			TestGDScriptAnalyzerAccessor::decode_property(make_signal_signature("[[Coroutine]]"));
	CHECK(decoded_signal_class.builtin_type == Variant::SIGNAL);
	REQUIRE(decoded_signal_class.method_parameter_types.size() == 1);
	CHECK_FALSE(decoded_signal_class.method_parameter_types[0].is_coroutine);
	CHECK(decoded_signal_class.method_parameter_types[0].native_type != StringName("GDScriptFunctionState"));

	// A genuine Coroutine[Variant] parameter is always bracketed and must still decode as the coroutine skin,
	// so the disambiguation does not regress real coroutine signature slots.
	const GDScriptParser::DataType decoded_callable_coroutine =
			TestGDScriptAnalyzerAccessor::decode_property(make_callable_signature("[[Coroutine[Variant]], void]"));
	REQUIRE(decoded_callable_coroutine.method_parameter_types.size() == 1);
	const GDScriptParser::DataType decoded_coroutine_param = decoded_callable_coroutine.method_parameter_types[0];
	CHECK(decoded_coroutine_param.is_coroutine);
	CHECK(decoded_coroutine_param.native_type == StringName("GDScriptFunctionState"));
	REQUIRE(decoded_coroutine_param.has_container_element_type(0));
	CHECK(decoded_coroutine_param.get_container_element_type(0).kind == GDScriptParser::DataType::VARIANT);

	// A genuine bracketed Coroutine[String] result also still round-trips as the coroutine skin.
	const GDScriptParser::DataType decoded_callable_coroutine_ret =
			TestGDScriptAnalyzerAccessor::decode_property(make_callable_signature("[[], Coroutine[String]]"));
	REQUIRE(decoded_callable_coroutine_ret.method_return_type.size() == 1);
	const GDScriptParser::DataType decoded_coroutine_ret = decoded_callable_coroutine_ret.method_return_type[0];
	CHECK(decoded_coroutine_ret.is_coroutine);
	REQUIRE(decoded_coroutine_ret.has_container_element_type(0));
	CHECK(decoded_coroutine_ret.get_container_element_type(0).builtin_type == Variant::STRING);
}

TEST_CASE("[Modules][GDScript] Async MethodInfo wraps the declared return type in a coroutine") {
	// METHOD_FLAG_ASYNC means the call result is Coroutine[declared return type]. The declared return type
	// lives in MethodInfo::return_val, so the wrap is unconditional, mirroring the in-memory async call
	// site. An async method declared `-> String` yields Coroutine[String]; one declared `-> Coroutine[T]`
	// yields Coroutine[Coroutine[T]] (the wrap must not be suppressed just because the declared return
	// already decodes as a coroutine via PROPERTY_HINT_COROUTINE_TYPE), while a synchronous method that
	// returns a coroutine handle is not re-wrapped.
	const PropertyInfo string_return = make_builtin_type(Variant::STRING).to_property_info("");
	const GDScriptParser::DataType async_string = TestGDScriptAnalyzerAccessor::method_return_type(string_return, true);
	CHECK(async_string.is_coroutine);
	REQUIRE(async_string.has_container_element_type(0));
	CHECK(async_string.get_container_element_type(0).builtin_type == Variant::STRING);
	CHECK(async_string.to_string() == "Coroutine[String]");

	const PropertyInfo coroutine_return = make_coroutine_type(make_builtin_type(Variant::STRING)).to_property_info("");
	const GDScriptParser::DataType async_coroutine = TestGDScriptAnalyzerAccessor::method_return_type(coroutine_return, true);
	CHECK(async_coroutine.is_coroutine);
	REQUIRE(async_coroutine.has_container_element_type(0));
	const GDScriptParser::DataType inner = async_coroutine.get_container_element_type(0);
	CHECK(inner.is_coroutine);
	REQUIRE(inner.has_container_element_type(0));
	CHECK(inner.get_container_element_type(0).builtin_type == Variant::STRING);
	CHECK(async_coroutine.to_string() == "Coroutine[Coroutine[String]]");

	const GDScriptParser::DataType sync_coroutine = TestGDScriptAnalyzerAccessor::method_return_type(coroutine_return, false);
	CHECK(sync_coroutine.is_coroutine);
	REQUIRE(sync_coroutine.has_container_element_type(0));
	CHECK(sync_coroutine.get_container_element_type(0).builtin_type == Variant::STRING);
	CHECK(sync_coroutine.to_string() == "Coroutine[String]");
}

TEST_CASE("[Modules][GDScript] Async method reference infers a bare AsyncCallable type") {
	GDScriptParser parser;
	Error err = parser.parse("async func _fetch() -> int:\n\treturn 1\nvar handler := _fetch\n", "user://async_method_reference.gd", false);
	REQUIRE(err == OK);

	GDScriptAnalyzer analyzer(&parser);
	err = analyzer.analyze();
	REQUIRE(err == OK);

	const GDScriptParser::ClassNode *root_class = parser.get_tree();
	REQUIRE(root_class != nullptr);

	const GDScriptParser::VariableNode *handler = nullptr;
	for (const GDScriptParser::ClassNode::Member &member : root_class->members) {
		if (member.type == GDScriptParser::ClassNode::Member::VARIABLE && member.variable->identifier->name == "handler") {
			handler = member.variable;
			break;
		}
	}
	REQUIRE(handler != nullptr);

	const GDScriptParser::DataType handler_type = handler->get_datatype();
	CHECK(handler_type.builtin_type == Variant::CALLABLE);
	// A reference to an async method carries the async marker but no explicit method
	// signature, so it stringifies as a bare `AsyncCallable` rather than `Callable`.
	CHECK(handler_type.signature_is_async);
	CHECK_FALSE(handler_type.has_explicit_method_signature);
	CHECK(handler_type.to_string() == "AsyncCallable");
}

TEST_CASE("[Modules][GDScript] Docgen renders AsyncCallable parameter and return types") {
	GDScriptParser parser;
	Error err = parser.parse("var handler: AsyncCallable[[int], bool]\n", "user://async_callable_docgen.gd", false);
	REQUIRE(err == OK);

	GDScriptAnalyzer analyzer(&parser);
	err = analyzer.analyze();
	REQUIRE(err == OK);

	const GDScriptParser::ClassNode *root_class = parser.get_tree();
	REQUIRE(root_class != nullptr);

	Ref<GDScript> script;
	script.instantiate();
	GDScriptDocGen::generate_docs(script.ptr(), root_class);

	const Vector<DocData::ClassDoc> docs = script->get_documentation();
	REQUIRE(docs.size() == 1);
	REQUIRE(docs[0].properties.size() == 1);
	CHECK(docs[0].properties[0].type == "AsyncCallable");
}

TEST_CASE("[Modules][GDScript] Docgen renders Coroutine result types") {
	GDScriptParser parser;
	Error err = parser.parse("enum Direction { NORTH, SOUTH }\nvar pending: Coroutine[String]\nvar jobs: Array[Coroutine[String]] = []\nvar step: Coroutine[Direction]\n", "user://coroutine_docgen.gd", false);
	REQUIRE(err == OK);

	GDScriptAnalyzer analyzer(&parser);
	err = analyzer.analyze();
	REQUIRE(err == OK);

	const GDScriptParser::ClassNode *root_class = parser.get_tree();
	REQUIRE(root_class != nullptr);

	Ref<GDScript> script;
	script.instantiate();
	GDScriptDocGen::generate_docs(script.ptr(), root_class);

	const Vector<DocData::ClassDoc> docs = script->get_documentation();
	REQUIRE(docs.size() == 1);
	REQUIRE(docs[0].properties.size() == 3);
	// The native `GDScriptFunctionState` name must never leak; docs render the phantom result type.
	CHECK(docs[0].properties[0].type == "Coroutine[String]");
	// An Array of coroutines uses the doc `T[]` array spelling over the coroutine type.
	CHECK(docs[0].properties[1].type == "Coroutine[String][]");
	// An enum result collapses to its underlying `int`, matching `Array[Enum]` -> `int[]`,
	// so the wrapped spelling never emits a dead class-style help link for the enum name.
	CHECK(docs[0].properties[2].type == "Coroutine[int]");
}

TEST_CASE("[Modules][GDScript] Docgen displays nested typed container values") {
	GDScriptParser parser;
	Error err = parser.parse("const VALUES: Array[Dictionary[String, int]] = [{ \"score\": 10 }]\nconst GROUPS: Dictionary[String, Array[int]] = { \"scores\": [1] }\n", "user://nested_docgen_type.gd", false);
	REQUIRE(err == OK);

	GDScriptAnalyzer analyzer(&parser);
	err = analyzer.analyze();
	REQUIRE(err == OK);

	const GDScriptParser::ClassNode *root_class = parser.get_tree();
	REQUIRE(root_class != nullptr);

	Ref<GDScript> script;
	script.instantiate();
	GDScriptDocGen::generate_docs(script.ptr(), root_class);

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
	// bind() is a variadic builtin: binding more arguments than the target's arity is accepted.
	CHECK(analyze_source(source_prefix + "\tcallback.bind(1, \"ok\", false)\n") == OK);
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
	// bindv() is variadic over the bound array: more elements than the target's arity is accepted.
	CHECK(analyze_source(source_prefix + "\tcallback.bindv([1, \"ok\", false])\n") == OK);
	CHECK(analyze_source(source_prefix + "\tcallback.bindv([get_dynamic()]).call(1)\n") == OK);
	CHECK(analyze_source(source_prefix + "\tcallback.bindv([get_dynamic()]).call(1)\n", false, true) != OK);
	CHECK(analyze_source(inferred_source_prefix + "\tcallback.bindv([\"legacy dynamic\"]).call(\"legacy dynamic\")\n") == OK);
}

TEST_CASE("[Modules][GDScript] Analyzer preserves typed vararg Callable.bind signatures") {
	const String source_prefix = "func accept_int_vararg(value: int, ...args: Array) -> bool:\n\treturn true\nfunc test() -> void:\n\tvar callback := Callable(self, \"accept_int_vararg\")\n";
	const String default_source_prefix = "func accept_default_vararg(value: int = 1, ...args: Array) -> bool:\n\treturn true\nfunc test() -> void:\n\tvar callback := Callable(self, \"accept_default_vararg\")\n";
	const String multi_fixed_source_prefix = "func accept_int_string_vararg(value: int, text: String, ...args: Array) -> bool:\n\treturn true\nfunc test() -> void:\n\tvar callback := Callable(self, \"accept_int_string_vararg\")\n";

	CHECK(analyze_source(source_prefix + "\tcallback.call(1)\n") == OK);
	CHECK(analyze_source(source_prefix + "\tcallback.call(1, \"extra\")\n") == OK);
	CHECK(analyze_source(source_prefix + "\tcallback.call(\"bad\")\n") != OK);
	CHECK(analyze_source(source_prefix + "\tcallback.bind(\"extra\").call(1, \"more\")\n") == OK);
	CHECK(analyze_source(source_prefix + "\tcallback.bind(\"extra\").call(\"bad\")\n") != OK);
	CHECK(analyze_source(source_prefix + "\tcallback.bind(1).call()\n") == OK);
	CHECK(analyze_source(source_prefix + "\tcallback.bind(1).call(\"bad\")\n") != OK);
	CHECK(analyze_source(source_prefix + "\tcallback.bindv([\"extra\"]).call(1)\n") == OK);
	CHECK(analyze_source(source_prefix + "\tcallback.bindv([\"extra\"]).call(\"bad\")\n") != OK);
	CHECK(analyze_source(source_prefix + "\tcallback.bindv([1]).call()\n") == OK);
	CHECK(analyze_source(source_prefix + "\tcallback.bindv([1]).call(\"bad\")\n") != OK);
	CHECK(analyze_source(default_source_prefix + "\tcallback.call()\n") == OK);
	CHECK(analyze_source(default_source_prefix + "\tcallback.bind(\"extra\").call()\n") != OK);
	CHECK(analyze_source(default_source_prefix + "\tcallback.bindv([\"extra\"]).call()\n") != OK);
	CHECK(analyze_source(multi_fixed_source_prefix + "\tcallback.bind(\"ok\").call(1)\n") == OK);
	CHECK(analyze_source(multi_fixed_source_prefix + "\tcallback.bind(\"ok\").call()\n") != OK);
	CHECK(analyze_source(multi_fixed_source_prefix + "\tcallback.bind(\"ok\").call(1, \"more\")\n") == OK);
	CHECK(analyze_source(multi_fixed_source_prefix + "\tcallback.bind(\"ok\").call(1, 2)\n") != OK);
	CHECK(analyze_source(multi_fixed_source_prefix + "\tcallback.bind(1).call()\n") != OK);
	CHECK(analyze_source(multi_fixed_source_prefix + "\tcallback.bind(1).call(2, \"ok\")\n") == OK);
	CHECK(analyze_source(multi_fixed_source_prefix + "\tcallback.bind(1).call(\"bad\", \"ok\")\n") != OK);
	CHECK(analyze_source(multi_fixed_source_prefix + "\tcallback.bind(1, \"ok\").call()\n") == OK);
	CHECK(analyze_source(multi_fixed_source_prefix + "\tcallback.bind(1, \"ok\").callv([])\n") == OK);
	CHECK(analyze_source(multi_fixed_source_prefix + "\tcallback.bind(1, \"ok\").call(2)\n") != OK);
	CHECK(analyze_source(multi_fixed_source_prefix + "\tcallback.bind(1, \"ok\").call(2, \"more\")\n") == OK);
	CHECK(analyze_source(multi_fixed_source_prefix + "\tcallback.bind(1, \"ok\").call(\"bad\", \"more\")\n") != OK);
	CHECK(analyze_source(multi_fixed_source_prefix + "\tcallback.bindv([\"ok\"]).call(1)\n") == OK);
	CHECK(analyze_source(multi_fixed_source_prefix + "\tcallback.bindv([\"ok\"]).call(1, 2)\n") != OK);
	CHECK(analyze_source(multi_fixed_source_prefix + "\tcallback.bindv([1, \"ok\"]).call()\n") == OK);
	CHECK(analyze_source(multi_fixed_source_prefix + "\tcallback.bindv([1, \"ok\"]).callv([])\n") == OK);
	CHECK(analyze_source(multi_fixed_source_prefix + "\tcallback.bindv([1, \"ok\"]).call(2)\n") != OK);
	CHECK(analyze_source(multi_fixed_source_prefix + "\tcallback.bindv([1, \"ok\"]).call(2, \"more\")\n") == OK);
	CHECK(analyze_source(multi_fixed_source_prefix + "\tcallback.bindv([1, \"ok\"]).call(\"bad\", \"more\")\n") != OK);
	CHECK(analyze_source(multi_fixed_source_prefix + "\tcallback.bind(1, \"ok\").unbind(1).call(\"ignored\")\n") == OK);
	CHECK(analyze_source(multi_fixed_source_prefix + "\tcallback.bind(1, \"ok\").unbind(1).call(1, \"bad\")\n") != OK);
	CHECK(analyze_source(multi_fixed_source_prefix + "\tcallback.bind(1, \"ok\").unbind(1).call(1, \"ok\", \"ignored\")\n") == OK);
	CHECK(analyze_source(multi_fixed_source_prefix + "\tcallback.bind(1, \"ok\").unbind(1).call(\"bad\", \"ok\", \"ignored\")\n") != OK);
	CHECK(analyze_source(multi_fixed_source_prefix + "\tcallback.bindv([1, \"ok\"]).unbind(1).call(\"ignored\")\n") == OK);
	CHECK(analyze_source(multi_fixed_source_prefix + "\tcallback.bindv([1, \"ok\"]).unbind(1).call(1, \"bad\")\n") != OK);
}

TEST_CASE("[Modules][GDScript] Analyzer preserves non-contiguous vararg Callable arities for Signal.connect") {
	const String source_prefix = "signal none\nsignal one(value: int)\nsignal one_string(value: String)\nsignal two(value: int, text: String)\nsignal three(value: int, text: String, ignored: bool)\nfunc accept_int_string_vararg(value: int, text: String, ...args: Array) -> bool:\n\treturn true\nfunc test() -> void:\n\tvar none_event: Signal[[]] = none\n\tvar one_event: Signal[[int]] = one\n\tvar one_string_event: Signal[[String]] = one_string\n\tvar two_event: Signal[[int, String]] = two\n\tvar three_event: Signal[[int, String, bool]] = three\n\tvar callback := Callable(self, \"accept_int_string_vararg\")\n";

	CHECK(analyze_source(source_prefix + "\tnone_event.connect(callback.bind(1, \"ok\"))\n") == OK);
	CHECK(analyze_source(source_prefix + "\tone_event.connect(callback.bind(1, \"ok\"))\n") != OK);
	CHECK(analyze_source(source_prefix + "\ttwo_event.connect(callback.bind(1, \"ok\"))\n") == OK);
	CHECK(analyze_source(source_prefix + "\tnone_event.connect(callback.bindv([1, \"ok\"]))\n") == OK);
	CHECK(analyze_source(source_prefix + "\tone_event.connect(callback.bindv([1, \"ok\"]))\n") != OK);
	CHECK(analyze_source(source_prefix + "\ttwo_event.connect(callback.bindv([1, \"ok\"]))\n") == OK);
	CHECK(analyze_source(source_prefix + "\tone_string_event.connect(callback.bind(1, \"ok\").unbind(1))\n") == OK);
	CHECK(analyze_source(source_prefix + "\ttwo_event.connect(callback.bind(1, \"ok\").unbind(1))\n") != OK);
	CHECK(analyze_source(source_prefix + "\tthree_event.connect(callback.bind(1, \"ok\").unbind(1))\n") == OK);
	CHECK(analyze_source(source_prefix + "\tone_string_event.connect(callback.bindv([1, \"ok\"]).unbind(1))\n") == OK);
	CHECK(analyze_source(source_prefix + "\ttwo_event.connect(callback.bindv([1, \"ok\"]).unbind(1))\n") != OK);
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
	CHECK(analyze_source(source_prefix + "\tCallable.create(worker, method_name).call(\"legacy dynamic\")\n") == OK);
	CHECK(analyze_source(source_prefix + "\tCallable.create(worker, \"unknown\").call(\"legacy dynamic\")\n") == OK);
	CHECK(analyze_source(source_prefix + "\tCallable(worker, method_name).call(1)\n", false, true) != OK);
	CHECK(analyze_source(source_prefix + "\tCallable(worker, \"unknown\").call(1)\n", false, true) != OK);
	CHECK(analyze_source(source_prefix + "\tCallable.create(worker, method_name).call(1)\n", false, true) != OK);
	CHECK(analyze_source(source_prefix + "\tCallable.create(worker, \"unknown\").call(1)\n", false, true) != OK);
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

TEST_CASE("[Modules][GDScript] Analyzer rejects dynamic reflection fallbacks in strict mode") {
	const String source_prefix = "class Worker extends Node:\n\tvar count: int\n\tfunc stringify(value: int) -> String:\n\t\treturn \"ok\"\nfunc test(worker: Worker, method_name: StringName, property_name: StringName, property_path: NodePath) -> void:\n";

	CHECK(analyze_source(source_prefix + "\tworker.call(method_name, 1)\n", false, true) != OK);
	CHECK(analyze_source(source_prefix + "\tworker.call(\"unknown\", 1)\n", false, true) != OK);
	CHECK(analyze_source(source_prefix + "\tworker.call_deferred(method_name, 1)\n", false, true) != OK);
	CHECK(analyze_source(source_prefix + "\tworker.call_deferred(\"unknown\", 1)\n", false, true) != OK);
	CHECK(analyze_source(source_prefix + "\tworker.callv(method_name, [1])\n", false, true) != OK);
	CHECK(analyze_source(source_prefix + "\tworker.callv(\"unknown\", [1])\n", false, true) != OK);
	CHECK(analyze_source(source_prefix + "\tworker.rpc(method_name, 1)\n", false, true) != OK);
	CHECK(analyze_source(source_prefix + "\tworker.rpc(\"unknown\", 1)\n", false, true) != OK);
	CHECK(analyze_source(source_prefix + "\tworker.rpc_id(1, method_name, 1)\n", false, true) != OK);
	CHECK(analyze_source(source_prefix + "\tworker.rpc_id(1, \"unknown\", 1)\n", false, true) != OK);
	CHECK(analyze_source(source_prefix + "\tworker.get(property_name)\n", false, true) != OK);
	CHECK(analyze_source(source_prefix + "\tworker.get(\"unknown\")\n", false, true) != OK);
	CHECK(analyze_source(source_prefix + "\tworker.set(property_name, 1)\n", false, true) != OK);
	CHECK(analyze_source(source_prefix + "\tworker.set(\"unknown\", 1)\n", false, true) != OK);
	CHECK(analyze_source(source_prefix + "\tworker.set_deferred(property_name, 1)\n", false, true) != OK);
	CHECK(analyze_source(source_prefix + "\tworker.set_deferred(\"unknown\", 1)\n", false, true) != OK);
	CHECK(analyze_source(source_prefix + "\tworker.get_indexed(property_path)\n", false, true) != OK);
	CHECK(analyze_source(source_prefix + "\tworker.get_indexed(^\"unknown\")\n", false, true) != OK);
	CHECK(analyze_source(source_prefix + "\tworker.set_indexed(property_path, 1)\n", false, true) != OK);
	CHECK(analyze_source(source_prefix + "\tworker.set_indexed(^\"unknown\", 1)\n", false, true) != OK);
}

TEST_CASE("[Modules][GDScript] Analyzer preserves legacy reflection fallbacks until strict dynamic is enabled") {
	const String source_prefix = "class Worker extends Node:\n\tvar count: int\n\tfunc stringify(value: int) -> String:\n\t\treturn \"ok\"\nfunc test(worker: Worker, method_name: StringName, property_name: StringName, property_path: NodePath) -> void:\n";

	check_legacy_ok_strict_dynamic_rejected(source_prefix + "\tworker.call(method_name, 1)\n");
	check_legacy_ok_strict_dynamic_rejected(source_prefix + "\tworker.call(\"unknown\", 1)\n");
	check_legacy_ok_strict_dynamic_rejected(source_prefix + "\tworker.call_deferred(method_name, 1)\n");
	check_legacy_ok_strict_dynamic_rejected(source_prefix + "\tworker.call_deferred(\"unknown\", 1)\n");
	check_legacy_ok_strict_dynamic_rejected(source_prefix + "\tworker.callv(method_name, [1])\n");
	check_legacy_ok_strict_dynamic_rejected(source_prefix + "\tworker.callv(\"unknown\", [1])\n");
	check_legacy_ok_strict_dynamic_rejected(source_prefix + "\tworker.rpc(method_name, 1)\n");
	check_legacy_ok_strict_dynamic_rejected(source_prefix + "\tworker.rpc(\"unknown\", 1)\n");
	check_legacy_ok_strict_dynamic_rejected(source_prefix + "\tworker.rpc_id(1, method_name, 1)\n");
	check_legacy_ok_strict_dynamic_rejected(source_prefix + "\tworker.rpc_id(1, \"unknown\", 1)\n");
	check_legacy_ok_strict_dynamic_rejected(source_prefix + "\tworker.get(property_name)\n");
	check_legacy_ok_strict_dynamic_rejected(source_prefix + "\tworker.get(\"unknown\")\n");
	check_legacy_ok_strict_dynamic_rejected(source_prefix + "\tworker.set(property_name, 1)\n");
	check_legacy_ok_strict_dynamic_rejected(source_prefix + "\tworker.set(\"unknown\", 1)\n");
	check_legacy_ok_strict_dynamic_rejected(source_prefix + "\tworker.set_deferred(property_name, 1)\n");
	check_legacy_ok_strict_dynamic_rejected(source_prefix + "\tworker.set_deferred(\"unknown\", 1)\n");
	check_legacy_ok_strict_dynamic_rejected(source_prefix + "\tworker.get_indexed(property_path)\n");
	check_legacy_ok_strict_dynamic_rejected(source_prefix + "\tworker.get_indexed(^\"unknown\")\n");
	check_legacy_ok_strict_dynamic_rejected(source_prefix + "\tworker.set_indexed(property_path, 1)\n");
	check_legacy_ok_strict_dynamic_rejected(source_prefix + "\tworker.set_indexed(^\"unknown\", 1)\n");
}

TEST_CASE("[Modules][GDScript] Analyzer rejects unsafe dynamic expression fallbacks in strict mode") {
	const String source_prefix = "class Worker:\n\tvar count: int\nfunc test(worker: Worker, dynamic_value: Variant, dynamic_key: Variant, values: Array[int]) -> void:\n";

	CHECK(analyze_source(source_prefix + "\tdynamic_value.call_missing(1)\n") == OK);
	CHECK(analyze_source(source_prefix + "\tworker.call_missing(1)\n") == OK);
	CHECK(analyze_source(source_prefix + "\tvar value := values[dynamic_key]\n") == OK);
	CHECK(analyze_source(source_prefix + "\tdynamic_value.call_missing(1)\n", false, true) != OK);
	CHECK(analyze_source(source_prefix + "\tworker.call_missing(1)\n", false, true) != OK);
	CHECK(analyze_source(source_prefix + "\tvar value := dynamic_value.some_property\n", false, true) != OK);
	CHECK(analyze_source(source_prefix + "\tvar value := worker.some_property\n", false, true) != OK);
	CHECK(analyze_source(source_prefix + "\tvar value := dynamic_value[\"key\"]\n", false, true) != OK);
	CHECK(analyze_source(source_prefix + "\tvar value := values[dynamic_key]\n", false, true) != OK);
	CHECK(analyze_source(source_prefix + "\tvar value := dynamic_value + 1\n", false, true) != OK);
	CHECK(analyze_source(source_prefix + "\tvar value := -dynamic_value\n", false, true) != OK);
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

TEST_CASE("[Modules][GDScript] Analyzer checks typed Signal constructors from constant signal names") {
	const String source_prefix = "class Emitter:\n\tsignal event(value: String)\nfunc accept_string(value: String) -> void:\n\tpass\nfunc accept_int(value: int) -> void:\n\tpass\nfunc test(emitter: Emitter, button: Button, signal_name: StringName) -> void:\n";

	CHECK(analyze_source(source_prefix + "\tSignal(emitter, \"event\").emit(\"ok\")\n") == OK);
	CHECK(analyze_source(source_prefix + "\tSignal(emitter, \"event\").emit(1)\n") != OK);
	CHECK(analyze_source(source_prefix + "\tSignal(emitter, \"event\").connect(accept_string)\n") == OK);
	CHECK(analyze_source(source_prefix + "\tSignal(emitter, \"event\").connect(accept_int)\n") != OK);
	CHECK(analyze_source(source_prefix + "\tSignal(button, \"pressed\").emit()\n") == OK);
	CHECK(analyze_source(source_prefix + "\tSignal(button, \"pressed\").emit(1)\n") != OK);
	CHECK(analyze_source(source_prefix + "\tSignal(emitter, signal_name).emit(\"legacy dynamic\")\n") == OK);
	CHECK(analyze_source(source_prefix + "\tSignal(emitter, \"unknown\").emit(\"legacy dynamic\")\n") == OK);
	CHECK(analyze_source(source_prefix + "\tSignal(button, signal_name).emit(\"legacy dynamic\")\n") == OK);
	CHECK(analyze_source(source_prefix + "\tSignal(button, \"unknown\").emit(\"legacy dynamic\")\n") == OK);
	CHECK(analyze_source(source_prefix + "\tSignal(emitter, signal_name).emit(\"ok\")\n", false, true) != OK);
	CHECK(analyze_source(source_prefix + "\tSignal(emitter, \"unknown\").emit(\"ok\")\n", false, true) != OK);
	CHECK(analyze_source(source_prefix + "\tSignal(button, signal_name).emit()\n", false, true) != OK);
	CHECK(analyze_source(source_prefix + "\tSignal(button, \"unknown\").emit()\n", false, true) != OK);
}

TEST_CASE("[Modules][GDScript] Analyzer rejects dynamic signal fallbacks in strict mode") {
	const String local_source = "signal event(value: String)\nfunc accept_string(value: String) -> void:\n\tpass\nfunc test(signal_name: StringName) -> void:\n";
	const String typed_source = "class Emitter:\n\tsignal event(value: String)\nfunc accept_string(value: String) -> void:\n\tpass\nfunc test(emitter: Emitter, button: Button, signal_name: StringName) -> void:\n";

	CHECK(analyze_source(local_source + "\tconnect(signal_name, accept_string)\n", false, true) != OK);
	CHECK(analyze_source(local_source + "\tconnect(\"unknown\", accept_string)\n", false, true) != OK);
	CHECK(analyze_source(local_source + "\tdisconnect(signal_name, accept_string)\n", false, true) != OK);
	CHECK(analyze_source(local_source + "\tdisconnect(\"unknown\", accept_string)\n", false, true) != OK);
	CHECK(analyze_source(local_source + "\temit_signal(signal_name, \"ok\")\n", false, true) != OK);
	CHECK(analyze_source(local_source + "\temit_signal(\"unknown\", \"ok\")\n", false, true) != OK);
	CHECK(analyze_source(typed_source + "\temitter.connect(signal_name, accept_string)\n", false, true) != OK);
	CHECK(analyze_source(typed_source + "\temitter.connect(\"unknown\", accept_string)\n", false, true) != OK);
	CHECK(analyze_source(typed_source + "\temitter.disconnect(signal_name, accept_string)\n", false, true) != OK);
	CHECK(analyze_source(typed_source + "\temitter.disconnect(\"unknown\", accept_string)\n", false, true) != OK);
	CHECK(analyze_source(typed_source + "\temitter.emit_signal(signal_name, \"ok\")\n", false, true) != OK);
	CHECK(analyze_source(typed_source + "\temitter.emit_signal(\"unknown\", \"ok\")\n", false, true) != OK);
	CHECK(analyze_source(typed_source + "\tbutton.connect(signal_name, accept_string)\n", false, true) != OK);
	CHECK(analyze_source(typed_source + "\tbutton.connect(\"unknown\", accept_string)\n", false, true) != OK);
	CHECK(analyze_source(typed_source + "\tbutton.disconnect(signal_name, accept_string)\n", false, true) != OK);
	CHECK(analyze_source(typed_source + "\tbutton.disconnect(\"unknown\", accept_string)\n", false, true) != OK);
	CHECK(analyze_source(typed_source + "\tbutton.emit_signal(signal_name)\n", false, true) != OK);
	CHECK(analyze_source(typed_source + "\tbutton.emit_signal(\"unknown\")\n", false, true) != OK);
}

TEST_CASE("[Modules][GDScript] Analyzer preserves legacy callable and signal dynamics until strict dynamic is enabled") {
	const String callable_source = "func accepts_int(value: int) -> bool:\n\treturn true\nfunc get_dynamic() -> Variant:\n\treturn 1\nfunc get_peer() -> Variant:\n\treturn 1\nfunc test() -> void:\n\tvar callback: Callable[[int], bool] = accepts_int\n";
	const String signal_source = "signal event(value: int)\nfunc accept_int(value: int) -> void:\n\tpass\nfunc get_dynamic() -> Variant:\n\treturn 1\nfunc get_dynamic_callable() -> Variant:\n\treturn accept_int\nfunc test(signal_name: StringName) -> void:\n\tvar typed_event: Signal[[int]] = event\n";
	const String object_signal_source = "class Emitter:\n\tsignal event(value: int)\nfunc accept_int(value: int) -> void:\n\tpass\nfunc test(emitter: Emitter, signal_name: StringName) -> void:\n";

	check_legacy_ok_strict_dynamic_rejected(callable_source + "\tcallback.call(get_dynamic())\n");
	check_legacy_ok_strict_dynamic_rejected(callable_source + "\tcallback.call_deferred(get_dynamic())\n");
	check_legacy_ok_strict_dynamic_rejected(callable_source + "\tcallback.callv([get_dynamic()])\n");
	check_legacy_ok_strict_dynamic_rejected(callable_source + "\tcallback.rpc(get_dynamic())\n");
	check_legacy_ok_strict_dynamic_rejected(callable_source + "\tcallback.rpc_id(get_peer(), get_dynamic())\n");
	check_legacy_ok_strict_dynamic_rejected(signal_source + "\ttyped_event.emit(get_dynamic())\n");
	check_legacy_ok_strict_dynamic_rejected(signal_source + "\ttyped_event.connect(get_dynamic_callable())\n");
	check_legacy_ok_strict_dynamic_rejected(signal_source + "\tconnect(signal_name, accept_int)\n");
	check_legacy_ok_strict_dynamic_rejected(signal_source + "\tdisconnect(signal_name, accept_int)\n");
	check_legacy_ok_strict_dynamic_rejected(signal_source + "\temit_signal(signal_name, 1)\n");
	check_legacy_ok_strict_dynamic_rejected(object_signal_source + "\temitter.connect(signal_name, accept_int)\n");
	check_legacy_ok_strict_dynamic_rejected(object_signal_source + "\temitter.disconnect(signal_name, accept_int)\n");
	check_legacy_ok_strict_dynamic_rejected(object_signal_source + "\temitter.emit_signal(signal_name, 1)\n");
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
	CHECK(analyze_source(source_prefix + "\tvar connected: bool = typed_event.is_connected(accept_string)\n") == OK);
	CHECK(analyze_source(source_prefix + "\tvar connected: bool = typed_event.is_connected(accept_int)\n") != OK);
	CHECK(analyze_source(source_prefix + "\tvar connected: bool = typed_event.is_connected(accept_none)\n") != OK);
	CHECK(analyze_source(source_prefix + "\tvar connected: bool = typed_event.is_connected(accept_any)\n") == OK);
	CHECK(analyze_source(inferred_source_prefix + "\tvar connected: bool = typed_event.is_connected(accept_int)\n") == OK);
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
	const String dynamic_signal_source = "signal event(value: String)\nfunc accept_int(value: int) -> void:\n\tpass\nfunc test(signal_name: StringName) -> void:\n\tconnect(signal_name, accept_int)\n\tis_connected(signal_name, accept_int)\n";

	CHECK(analyze_source(source_prefix + "\tconnect(\"event\", accept_string)\n") == OK);
	CHECK(analyze_source(source_prefix + "\tself.connect(\"event\", accept_string)\n") == OK);
	CHECK(analyze_source(source_prefix + "\tconnect(&\"event\", accept_string)\n") == OK);
	CHECK(analyze_source(source_prefix + "\tconnect(\"event\", accept_int)\n") != OK);
	CHECK(analyze_source(source_prefix + "\tself.connect(\"event\", accept_int)\n") != OK);
	CHECK(analyze_source(source_prefix + "\tconnect(\"event\", accept_none)\n") != OK);
	CHECK(analyze_source(source_prefix + "\tconnect(\"event\", accept_any)\n") == OK);
	CHECK(analyze_source(dynamic_signal_source) == OK);
	CHECK(analyze_source(source_prefix + "\tconnect(\"unknown\", accept_int)\n") == OK);
	CHECK(analyze_source(source_prefix + "\tvar connected: bool = is_connected(\"event\", accept_string)\n") == OK);
	CHECK(analyze_source(source_prefix + "\tvar connected: bool = self.is_connected(\"event\", accept_string)\n") == OK);
	CHECK(analyze_source(source_prefix + "\tvar connected: bool = is_connected(&\"event\", accept_string)\n") == OK);
	CHECK(analyze_source(source_prefix + "\tvar connected: bool = is_connected(\"event\", accept_int)\n") != OK);
	CHECK(analyze_source(source_prefix + "\tvar connected: bool = self.is_connected(\"event\", accept_int)\n") != OK);
	CHECK(analyze_source(source_prefix + "\tvar connected: bool = is_connected(\"event\", accept_none)\n") != OK);
	CHECK(analyze_source(source_prefix + "\tvar connected: bool = is_connected(\"event\", accept_any)\n") == OK);
	CHECK(analyze_source(source_prefix + "\tvar connected: bool = is_connected(\"unknown\", accept_int)\n") == OK);
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
	const String dynamic_signal_source = "class Emitter:\n\tsignal event(value: String)\nfunc accept_int(value: int) -> void:\n\tpass\nfunc test(emitter: Emitter, signal_name: StringName) -> void:\n\temitter.connect(signal_name, accept_int)\n\temitter.disconnect(signal_name, accept_int)\n\tvar connected: bool = emitter.is_connected(signal_name, accept_int)\n";
	const String unknown_signal_source = "class Emitter:\n\tsignal event(value: String)\nfunc accept_int(value: int) -> void:\n\tpass\nfunc test(emitter: Emitter) -> void:\n\temitter.connect(\"unknown\", accept_int)\n\temitter.disconnect(\"unknown\", accept_int)\n\tvar connected: bool = emitter.is_connected(\"unknown\", accept_int)\n";

	CHECK(analyze_source(source_prefix + "\temitter.connect(\"event\", accept_string)\n\temitter.disconnect(\"event\", accept_string)\n") == OK);
	CHECK(analyze_source(source_prefix + "\temitter.connect(&\"event\", accept_string)\n\temitter.disconnect(&\"event\", accept_string)\n") == OK);
	CHECK(analyze_source(source_prefix + "\temitter.connect(\"event\", accept_int)\n") != OK);
	CHECK(analyze_source(source_prefix + "\temitter.disconnect(\"event\", accept_int)\n") != OK);
	CHECK(analyze_source(source_prefix + "\temitter.connect(\"event\", accept_none)\n") != OK);
	CHECK(analyze_source(source_prefix + "\temitter.disconnect(\"event\", accept_none)\n") != OK);
	CHECK(analyze_source(source_prefix + "\temitter.connect(\"event\", accept_any)\n\temitter.disconnect(\"event\", accept_any)\n") == OK);
	CHECK(analyze_source(dynamic_signal_source) == OK);
	CHECK(analyze_source(unknown_signal_source) == OK);
	CHECK(analyze_source(source_prefix + "\tvar connected: bool = emitter.is_connected(\"event\", accept_string)\n") == OK);
	CHECK(analyze_source(source_prefix + "\tvar connected: bool = emitter.is_connected(&\"event\", accept_string)\n") == OK);
	CHECK(analyze_source(source_prefix + "\tvar connected: bool = emitter.is_connected(\"event\", accept_int)\n") != OK);
	CHECK(analyze_source(source_prefix + "\tvar connected: bool = emitter.is_connected(\"event\", accept_none)\n") != OK);
	CHECK(analyze_source(source_prefix + "\tvar connected: bool = emitter.is_connected(\"event\", accept_any)\n") == OK);
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

TEST_CASE("[Modules][GDScript] Analyzer preserves legacy nullable flows until strict null is enabled") {
	check_legacy_ok_strict_null_rejected("func test(maybe: Node?) -> void:\n\tvar node: Node = maybe\n");
	check_legacy_ok_strict_null_rejected("func test(maybe: Node?, fallback: Node) -> void:\n\tvar node: Node = fallback\n\tnode = maybe\n");
	check_legacy_ok_strict_null_rejected("func accept_node(node: Node) -> void:\n\tpass\nfunc test(maybe: Node?) -> void:\n\taccept_node(maybe)\n");
	check_legacy_ok_strict_null_rejected("func get_node(maybe: Node?) -> Node:\n\treturn maybe\n");

	CHECK(analyze_source("func test(maybe: Node?) -> void:\n\tvar node: Node? = maybe\n", true) == OK);
	CHECK(analyze_source("func accept_node(node: Node?) -> void:\n\tpass\nfunc test(maybe: Node?) -> void:\n\taccept_node(maybe)\n", true) == OK);
	CHECK(analyze_source("func get_node(maybe: Node?) -> Node?:\n\treturn maybe\n", true) == OK);
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

TEST_CASE("[Modules][GDScript] Analyzer checks custom Object hash/equality signatures") {
	const String valid_source = "extends RefCounted\nfunc _equals(other: Variant) -> bool:\n\treturn false\nfunc _hash_code() -> int:\n\treturn 0\n";
	const String invalid_equals_source = "extends RefCounted\nfunc _equals(other: Variant) -> int:\n\treturn 0\nfunc _hash_code() -> int:\n\treturn 0\n";
	const String invalid_hash_source = "extends RefCounted\nfunc _equals(other: Variant) -> bool:\n\treturn false\nfunc _hash_code(value: int) -> int:\n\treturn value\n";

	CHECK(analyze_source(valid_source) == OK);
	CHECK(analyze_source(invalid_equals_source) != OK);
	CHECK(analyze_source(invalid_hash_source) != OK);
}

TEST_CASE("[Modules][GDScript] Analyzer can reject dynamic source arguments and returns to static types") {
	const String argument_source = "func get_dynamic() -> Variant:\n\treturn 1\nfunc accept_int(value: int) -> void:\n\tpass\nfunc test() -> void:\n\taccept_int(get_dynamic())\n";
	const String return_source = "func get_dynamic() -> Variant:\n\treturn 1\nfunc get_int() -> int:\n\treturn get_dynamic()\n";

	CHECK(analyze_source(argument_source) == OK);
	CHECK(analyze_source(argument_source, false, true) != OK);
	CHECK(analyze_source(return_source) == OK);
	CHECK(analyze_source(return_source, false, true) != OK);
}

TEST_CASE("[Modules][GDScript] Analyzer preserves legacy Variant flows until strict dynamic is enabled") {
	const String source_prefix = "func get_dynamic() -> Variant:\n\treturn 1\n";

	check_legacy_ok_strict_dynamic_rejected(source_prefix + "func test() -> void:\n\tvar value: int = get_dynamic()\n");
	check_legacy_ok_strict_dynamic_rejected(source_prefix + "func test() -> void:\n\tvar value: int = 1\n\tvalue = get_dynamic()\n");
	check_legacy_ok_strict_dynamic_rejected(source_prefix + "func accept_int(value: int) -> void:\n\tpass\nfunc test() -> void:\n\taccept_int(get_dynamic())\n");
	check_legacy_ok_strict_dynamic_rejected(source_prefix + "func get_int() -> int:\n\treturn get_dynamic()\n");

	CHECK(analyze_source(source_prefix + "func test() -> void:\n\tvar value: Variant = get_dynamic()\n", false, true) == OK);
	CHECK(analyze_source(source_prefix + "func accept_variant(value: Variant) -> void:\n\tpass\nfunc test() -> void:\n\taccept_variant(get_dynamic())\n", false, true) == OK);
	CHECK(analyze_source(source_prefix + "func get_variant() -> Variant:\n\treturn get_dynamic()\n", false, true) == OK);
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

TEST_CASE("[Modules][GDScript] Analyzer preserves legacy nested Variant containers until strict dynamic is enabled") {
	const String source_prefix = "func get_dynamic() -> Variant:\n\treturn 1\nfunc test() -> void:\n";

	check_legacy_ok_strict_dynamic_rejected(source_prefix + "\tvar nested: Array[Array[int]] = [[get_dynamic()]]\n");
	check_legacy_ok_strict_dynamic_rejected(source_prefix + "\tvar nested: Dictionary[String, Array[int]] = { \"values\": [get_dynamic()] }\n");
	check_legacy_ok_strict_dynamic_rejected(source_prefix + "\tvar nested: Array[Dictionary[String, int]] = [{ \"value\": get_dynamic() }]\n");
	check_legacy_ok_strict_dynamic_rejected(source_prefix + "\tvar nested: Dictionary[String, Dictionary[String, int]] = { \"outer\": { \"value\": get_dynamic() } }\n");
	check_legacy_ok_strict_dynamic_rejected(source_prefix + "\tvar nested: Array[Array[int]] = [[1]]\n\tnested[0].append(get_dynamic())\n");
	check_legacy_ok_strict_dynamic_rejected(source_prefix + "\tvar nested: Dictionary[String, Array[int]] = { \"values\": [] }\n\tnested[\"values\"].append(get_dynamic())\n");
	check_legacy_ok_strict_dynamic_rejected("func get_dynamic() -> Variant:\n\treturn 1\nfunc accept_nested(values: Array[Array[int]]) -> void:\n\tpass\nfunc test() -> void:\n\taccept_nested([[get_dynamic()]])\n");
	check_legacy_ok_strict_dynamic_rejected("func get_dynamic() -> Variant:\n\treturn 1\nfunc get_nested() -> Array[Array[int]]:\n\treturn [[get_dynamic()]]\n");

	CHECK(analyze_source(source_prefix + "\tvar nested: Array[Array[Variant]] = [[get_dynamic()]]\n", false, true) == OK);
	CHECK(analyze_source("func get_dynamic() -> Variant:\n\treturn 1\nfunc accept_nested(values: Array[Array[Variant]]) -> void:\n\tpass\nfunc test() -> void:\n\taccept_nested([[get_dynamic()]])\n", false, true) == OK);
	CHECK(analyze_source("func get_dynamic() -> Variant:\n\treturn 1\nfunc get_nested() -> Array[Array[Variant]]:\n\treturn [[get_dynamic()]]\n", false, true) == OK);
}

TEST_CASE("[Modules][GDScript] Analyzer reports strict dynamic argument diagnostics") {
	const String callable_source = "func accepts_int(value: int) -> bool:\n\treturn true\nfunc get_dynamic() -> Variant:\n\treturn 1\nfunc test() -> void:\n\tvar callback: Callable[[int], bool] = accepts_int\n\tcallback.call(get_dynamic())\n";
	const String callv_source = "func accepts_int(value: int) -> bool:\n\treturn true\nfunc get_dynamic() -> Variant:\n\treturn 1\nfunc test() -> void:\n\tvar callback: Callable[[int], bool] = accepts_int\n\tcallback.callv([get_dynamic()])\n";
	const String signal_source = "signal event(value: int)\nfunc get_dynamic() -> Variant:\n\treturn 1\nfunc test() -> void:\n\tvar typed_event: Signal[[int]] = event\n\ttyped_event.emit(get_dynamic())\n";

	PackedStringArray callable_errors = analyze_source_errors(callable_source, false, true);
	REQUIRE(callable_errors.size() == 1);
	CHECK_EQ(callable_errors[0], R"*(Cannot pass Variant value as argument 1 of "call()" in strict dynamic mode; expected "int".)*");

	PackedStringArray callv_errors = analyze_source_errors(callv_source, false, true);
	REQUIRE(callv_errors.size() == 1);
	CHECK_EQ(callv_errors[0], R"*(Cannot pass Variant value as argument 1 of "callv()" in strict dynamic mode; expected "int".)*");

	PackedStringArray signal_errors = analyze_source_errors(signal_source, false, true);
	REQUIRE(signal_errors.size() == 1);
	CHECK_EQ(signal_errors[0], R"*(Cannot pass Variant value as argument 1 of "emit()" in strict dynamic mode; expected "int".)*");
}

TEST_CASE("[Modules][GDScript] Analyzer reports strict nullable argument diagnostics") {
	const String callable_source = "func accepts_node(node: Node) -> void:\n\tpass\nfunc get_node() -> Node?:\n\treturn null\nfunc test() -> void:\n\tvar callback: Callable[[Node], void] = accepts_node\n\tcallback.call(get_node())\n";
	const String signal_source = "signal event(node: Node)\nfunc get_node() -> Node?:\n\treturn null\nfunc test() -> void:\n\tvar typed_event: Signal[[Node]] = event\n\ttyped_event.emit(get_node())\n";

	PackedStringArray callable_errors = analyze_source_errors(callable_source, true);
	REQUIRE(callable_errors.size() == 1);
	CHECK_EQ(callable_errors[0], R"*(Cannot pass nullable value of type "Node?" as argument 1 of "call()"; expected non-nullable "Node".)*");

	PackedStringArray signal_errors = analyze_source_errors(signal_source, true);
	REQUIRE(signal_errors.size() == 1);
	CHECK_EQ(signal_errors[0], R"*(Cannot pass nullable value of type "Node?" as argument 1 of "emit()"; expected non-nullable "Node".)*");
}

TEST_CASE("[Modules][GDScript] Analyzer reports strict assignment diagnostics") {
	const String dynamic_initializer_source = "func get_dynamic() -> Variant:\n\treturn 1\n"
											  "var typed_value: int = get_dynamic()\n";
	const String dynamic_assignment_source = "func get_dynamic() -> Variant:\n\treturn 1\nfunc test() -> void:\n"
											 "\tvar typed_value: int = 1\n\ttyped_value = get_dynamic()\n";
	const String nullable_initializer_source = "func get_nullable_node() -> Node?:\n\treturn null\nvar node: Node = get_nullable_node()\n";
	const String nullable_assignment_source = "func get_nullable_node() -> Node?:\n\treturn null\nfunc test() -> void:\n"
											  "\tvar node: Node = Node.new()\n\tnode = get_nullable_node()\n";

	check_source_error(dynamic_initializer_source,
			R"*(Cannot assign Variant value to variable "typed_value" in strict dynamic mode; expected "int".)*",
			false, true);
	check_source_error(dynamic_assignment_source,
			R"*(Cannot assign Variant value to variable "typed_value" in strict dynamic mode; expected "int".)*",
			false, true);
	check_source_error(nullable_initializer_source,
			R"*(Cannot assign nullable value of type "Node?" to variable "node"; expected non-nullable "Node".)*",
			true);
	check_source_error(nullable_assignment_source,
			R"*(Cannot assign nullable value of type "Node?" to variable "node"; expected non-nullable "Node".)*",
			true);
}

TEST_CASE("[Modules][GDScript] Analyzer reports strict nullable Type handle diagnostics") {
	const String base_source = "func get_nullable_node_type() -> Type[Node]?:\n"
							   "\treturn null\n\n";
	const String nullable_initializer_source = base_source + "var node_handle: Type[Node] = get_nullable_node_type()\n";
	const String nullable_assignment_source = base_source + "func test() -> void:\n"
															"\tvar node_handle: Type[Node] = Node\n"
															"\tnode_handle = get_nullable_node_type()\n";
	const String nullable_argument_source = base_source + "func accept_node_type(_klass: Type[Node]) -> void:\n"
														  "\tpass\n\n"
														  "func test() -> void:\n"
														  "\taccept_node_type(get_nullable_node_type())\n";
	const String nullable_callv_source = base_source + "func accept_node_type(_klass: Type[Node]) -> void:\n"
													   "\tpass\n\n"
													   "func test() -> void:\n"
													   "\tcallv(\"accept_node_type\", [get_nullable_node_type()])\n";

	check_source_error(nullable_initializer_source,
			"Cannot assign nullable value of type \"Type[Node]?\" to variable \"node_handle\"; "
			"expected non-nullable \"Type[Node]\".",
			true);
	check_source_error(nullable_assignment_source,
			"Cannot assign nullable value of type \"Type[Node]?\" to variable \"node_handle\"; "
			"expected non-nullable \"Type[Node]\".",
			true);
	check_source_error(nullable_argument_source,
			"Cannot pass nullable value of type \"Type[Node]?\" as argument 1 of \"accept_node_type()\"; "
			"expected non-nullable \"Type[Node]\".",
			true);
	check_source_error(nullable_callv_source,
			"Cannot pass nullable value of type \"Type[Node]?\" as argument 1 of \"callv()\"; "
			"expected non-nullable \"Type[Node]\".",
			true);
}

TEST_CASE("[Modules][GDScript] Analyzer error checks ignore globally enabled warnings") {
	// Initializing the language enables project warnings globally and some of them default to errors
	// (e.g. NATIVE_METHOD_OVERRIDE, triggered here by overriding the native "get_node" method). The
	// analyzer-error helpers must report analyzer errors only, regardless of that leaked warning state.
	GDScriptLanguage::get_singleton()->init();

	const String source = "func get_node() -> Node?:\n\treturn null\nvar node: Node = get_node()\n";
	check_source_error(source,
			R"*(Cannot assign nullable value of type "Node?" to variable "node"; expected non-nullable "Node".)*",
			true);
}

TEST_CASE("[Modules][GDScript] Analyzer reports strict return diagnostics") {
	const String dynamic_return_source = "func get_dynamic() -> Variant:\n\treturn 1\nfunc get_int() -> int:\n"
										 "\treturn get_dynamic()\n";
	const String nullable_return_source = "func get_node() -> Node?:\n\treturn null\nfunc get_required_node() -> Node:\n"
										  "\treturn get_node()\n";

	check_source_error(dynamic_return_source,
			R"*(Cannot return Variant value in strict dynamic mode; expected "int".)*",
			false, true);
	check_source_error(nullable_return_source,
			R"*(Cannot return nullable value of type "Node?"; expected non-nullable "Node".)*",
			true);
}

TEST_CASE("[Modules][GDScript] Analyzer reports strict nested container diagnostics") {
	const String array_source = "func get_dynamic() -> Variant:\n\treturn 1\n"
								"var values: Array[Array[int]] = [[get_dynamic()]]\n";
	const String dictionary_source = "func get_dynamic() -> Variant:\n\treturn 1\n"
									 "var values: Dictionary[String, Array[int]] = { \"values\": [get_dynamic()] }\n";

	check_source_error(array_source,
			R"*(Cannot include Variant value in array literal for "Array[int]" in strict dynamic mode.)*",
			false, true);
	check_source_error(dictionary_source,
			R"*(Cannot include Variant value in array literal for "Array[int]" in strict dynamic mode.)*",
			false, true);
}

TEST_CASE("[Modules][GDScript] Analyzer reports strict reflection fallback diagnostics") {
	const String source_prefix = "class Worker extends Node:\n\tvar count: int\n"
								 "\tfunc stringify(value: int) -> String:\n\t\treturn \"ok\"\n"
								 "func test(worker: Worker, method_name: StringName, property_name: StringName) -> void:\n";

	check_source_error(source_prefix + "\tworker.call(method_name, 1)\n",
			R"*(Cannot use dynamic method name for "call()" on type "Worker" in strict dynamic mode.)*",
			false, true);
	check_source_error(source_prefix + "\tworker.call(\"unknown\", 1)\n",
			R"*(Cannot resolve method "unknown" on type "Worker" for "call()" in strict dynamic mode.)*",
			false, true);
	check_source_error(source_prefix + "\tworker.get(property_name)\n",
			R"*(Cannot use dynamic property name for "get()" on type "Worker" in strict dynamic mode.)*",
			false, true);
	check_source_error(source_prefix + "\tworker.get(\"unknown\")\n",
			R"*(Cannot resolve property "unknown" on type "Worker" for "get()" in strict dynamic mode.)*",
			false, true);
}

TEST_CASE("[Modules][GDScript] Analyzer reports strict signal connection diagnostics") {
	const String dynamic_signal_source = "class Emitter:\n\tsignal event(value: int)\n"
										 "func accept_int(value: int) -> void:\n\tpass\n"
										 "func test(emitter: Emitter, signal_name: StringName) -> void:\n"
										 "\temitter.connect(signal_name, accept_int)\n";
	const String unknown_signal_source = "class Emitter:\n\tsignal event(value: int)\n"
										 "func accept_int(value: int) -> void:\n\tpass\n"
										 "func test(emitter: Emitter) -> void:\n"
										 "\temitter.connect(\"missing\", accept_int)\n";
	const String nullable_callable_source = "signal event(value: Node?)\nfunc accept_node(value: Node) -> void:\n\tpass\n"
											"func test() -> void:\n\tconnect(\"event\", accept_node)\n";

	check_source_error(dynamic_signal_source,
			R"*(Cannot use dynamic signal name for "connect()" on type "Emitter" in strict dynamic mode.)*",
			false, true);
	check_source_error(unknown_signal_source,
			R"*(Cannot resolve signal "missing" on type "Emitter" for "connect()" in strict dynamic mode.)*",
			false, true);
	check_source_error(nullable_callable_source,
			"Cannot connect signal \"Signal[[Node?]]\" to callable \"Callable[[Node], void]\": signal argument 1 "
			"is nullable type \"Node?\", but callable parameter expects non-nullable \"Node\".",
			true);
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

static GDScriptParser::DataType make_typed_array_type(Variant::Type p_element_type) {
	GDScriptParser::DataType type = make_builtin_type(Variant::ARRAY);
	type.set_container_element_type(0, make_builtin_type(p_element_type));
	return type;
}

static GDScriptParser::DataType make_callable_signature_type(
		const Vector<GDScriptParser::DataType> &p_params,
		const GDScriptParser::DataType &p_return,
		bool p_is_signal = false) {
	GDScriptParser::DataType type = make_builtin_type(p_is_signal ? Variant::SIGNAL : Variant::CALLABLE);
	type.has_method_signature = true;
	type.has_explicit_method_signature = true;
	type.method_parameter_types = p_params;
	if (!p_is_signal) {
		type.method_return_type.push_back(p_return);
	}
	return type;
}

TEST_CASE("[Modules][GDScript] Callable/Signal property encoding") {
	{
		Vector<GDScriptParser::DataType> params;
		params.push_back(make_builtin_type(Variant::INT));
		const PropertyInfo info = make_callable_signature_type(params, make_builtin_type(Variant::BOOL)).to_property_info("cb");
		CHECK(info.type == Variant::CALLABLE);
		CHECK(info.hint == PROPERTY_HINT_CALLABLE_TYPE);
		CHECK(info.hint_string == "[[int], bool]");
	}
	{
		Vector<GDScriptParser::DataType> params;
		params.push_back(make_builtin_type(Variant::INT));
		const PropertyInfo info = make_callable_signature_type(params, make_variant_type(), true).to_property_info("sig");
		CHECK(info.type == Variant::SIGNAL);
		CHECK(info.hint == PROPERTY_HINT_CALLABLE_TYPE);
		CHECK(info.hint_string == "[[int]]");
	}
	{
		Vector<GDScriptParser::DataType> inner_params;
		inner_params.push_back(make_builtin_type(Variant::INT));
		GDScriptParser::DataType inner = make_callable_signature_type(inner_params, make_builtin_type(Variant::NIL));
		Vector<GDScriptParser::DataType> params;
		params.push_back(inner);
		const PropertyInfo info = make_callable_signature_type(params, make_builtin_type(Variant::NIL)).to_property_info("cb");
		CHECK(info.hint_string == "[[Callable[[int], void]], void]");
	}
	{
		Vector<GDScriptParser::DataType> params;
		params.push_back(make_typed_array_type(Variant::INT));
		const PropertyInfo info = make_callable_signature_type(params, make_builtin_type(Variant::NIL)).to_property_info("cb");
		CHECK(info.hint_string == "[[Array[int]], void]");
	}
	{
		// Nullable parameter and return slots keep their `?` marker.
		GDScriptParser::DataType nullable_node = make_native_type("Node");
		nullable_node.is_nullable = true;
		Vector<GDScriptParser::DataType> params;
		params.push_back(nullable_node);
		GDScriptParser::DataType nullable_return = make_native_type("Node");
		nullable_return.is_nullable = true;
		const PropertyInfo info = make_callable_signature_type(params, nullable_return).to_property_info("cb");
		CHECK(info.hint_string == "[[Node?], Node?]");
	}
	{
		GDScriptParser::DataType untyped = make_builtin_type(Variant::CALLABLE);
		const PropertyInfo info = untyped.to_property_info("cb");
		CHECK(info.hint == PROPERTY_HINT_NONE);
		CHECK(info.hint_string.is_empty());
	}
}

static GDScriptParser::DataType make_enum_value_type(const StringName &p_native_type) {
	GDScriptParser::DataType type;
	type.kind = GDScriptParser::DataType::ENUM;
	type.type_source = GDScriptParser::DataType::ANNOTATED_EXPLICIT;
	type.builtin_type = Variant::INT;
	type.native_type = p_native_type;
	return type;
}

TEST_CASE("[Modules][GDScript] Callable/Signal enum signature leaves encode their identity") {
	// A built-in enum parameter (Vector3.Axis) is now encoded by its identity so the decoder can rebuild
	// it across the script-API boundary, instead of suppressing the whole hint and crossing untyped.
	{
		Vector<GDScriptParser::DataType> params;
		params.push_back(make_enum_value_type(SNAME("Vector3.Axis")));
		const PropertyInfo info = make_callable_signature_type(params, make_builtin_type(Variant::NIL)).to_property_info("cb");
		CHECK(info.hint == PROPERTY_HINT_CALLABLE_TYPE);
		CHECK(info.hint_string == "[[Vector3.Axis], void]");
	}

	// A native-class enum (Object.ConnectFlags) encodes its declaring-class-qualified name.
	{
		Vector<GDScriptParser::DataType> params;
		params.push_back(make_enum_value_type(SNAME("Object.ConnectFlags")));
		const PropertyInfo info = make_callable_signature_type(params, make_builtin_type(Variant::NIL)).to_property_info("cb");
		CHECK(info.hint == PROPERTY_HINT_CALLABLE_TYPE);
		CHECK(info.hint_string == "[[Object.ConnectFlags], void]");
	}

	// A global enum (Error) is encoded as a bare name (it has no base class in the grammar).
	{
		Vector<GDScriptParser::DataType> params;
		params.push_back(make_enum_value_type(SNAME("Error")));
		const PropertyInfo info = make_callable_signature_type(params, make_builtin_type(Variant::NIL)).to_property_info("cb");
		CHECK(info.hint == PROPERTY_HINT_CALLABLE_TYPE);
		CHECK(info.hint_string == "[[Error], void]");
	}

	// An enum nested inside a typed Array element shares the same leaf encoder, so it gains the same
	// fidelity (the Array/Dictionary element-hint extension called out in the acceptance criteria).
	{
		GDScriptParser::DataType array_of_axis = make_builtin_type(Variant::ARRAY);
		array_of_axis.set_container_element_type(0, make_enum_value_type(SNAME("Vector3.Axis")));
		Vector<GDScriptParser::DataType> params;
		params.push_back(array_of_axis);
		const PropertyInfo info = make_callable_signature_type(params, make_builtin_type(Variant::NIL)).to_property_info("cb");
		CHECK(info.hint == PROPERTY_HINT_CALLABLE_TYPE);
		CHECK(info.hint_string == "[[Array[Vector3.Axis]], void]");
	}

	// A script/class enum has no reproducible global name in the flat hint grammar, so the callable
	// crosses untyped (the accepted non-global script/class leaf limitation) rather than emitting a
	// lossy hint that the decoder could not reconstruct to an equal type.
	{
		Vector<GDScriptParser::DataType> params;
		params.push_back(make_enum_value_type(SNAME("res://script.gd.Kind")));
		const PropertyInfo info = make_callable_signature_type(params, make_builtin_type(Variant::NIL)).to_property_info("cb");
		CHECK(info.hint == PROPERTY_HINT_NONE);
		CHECK(info.hint_string.is_empty());
	}
}

} // namespace GDScriptTests
