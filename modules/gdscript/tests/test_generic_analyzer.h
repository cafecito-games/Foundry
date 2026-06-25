/**************************************************************************/
/*  test_generic_analyzer.h                                               */
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
#include "modules/gdscript/gdscript_parser.h"

#include "tests/test_macros.h"

namespace GDScriptTests {

// These tests drive the analyzer end to end and inspect resolved DataTypes, so they
// verify that type parameters and specialized handles are interpreted (not just parsed).

static const GDScriptParser::ClassNode *generic_find_inner_class(const GDScriptParser::ClassNode *p_class, const StringName &p_name) {
	if (!p_class->has_member(p_name)) {
		return nullptr;
	}
	const GDScriptParser::ClassNode::Member member = p_class->get_member(p_name);
	if (member.type != GDScriptParser::ClassNode::Member::CLASS) {
		return nullptr;
	}
	return member.m_class;
}

static const GDScriptParser::VariableNode *generic_find_member_variable(const GDScriptParser::ClassNode *p_class, const StringName &p_name) {
	if (!p_class->has_member(p_name)) {
		return nullptr;
	}
	const GDScriptParser::ClassNode::Member member = p_class->get_member(p_name);
	if (member.type != GDScriptParser::ClassNode::Member::VARIABLE) {
		return nullptr;
	}
	return member.variable;
}

static const GDScriptParser::FunctionNode *generic_find_function(const GDScriptParser::ClassNode *p_class, const StringName &p_name) {
	if (!p_class->has_function(p_name)) {
		return nullptr;
	}
	return p_class->get_member(p_name).function;
}

static const GDScriptParser::VariableNode *generic_find_local_variable(const GDScriptParser::FunctionNode *p_function, const StringName &p_name) {
	if (p_function == nullptr || p_function->body == nullptr) {
		return nullptr;
	}
	for (GDScriptParser::Node *statement : p_function->body->statements) {
		if (statement->type != GDScriptParser::Node::VARIABLE) {
			continue;
		}
		const GDScriptParser::VariableNode *variable = static_cast<const GDScriptParser::VariableNode *>(statement);
		if (variable->identifier && variable->identifier->name == p_name) {
			return variable;
		}
	}
	return nullptr;
}

TEST_CASE("[Modules][GDScript] Analyzer resolves a class type parameter inside a generic class") {
	GDScriptParser parser;
	const Error parse_error = parser.parse("class Box[T]:\n\tvar value: T\n", "user://test.gd", false);
	REQUIRE(parse_error == OK);

	GDScriptAnalyzer analyzer(&parser);
	REQUIRE(analyzer.analyze() == OK);

	const GDScriptParser::ClassNode *box = generic_find_inner_class(parser.get_tree(), "Box");
	REQUIRE(box != nullptr);
	const GDScriptParser::VariableNode *value = generic_find_member_variable(box, "value");
	REQUIRE(value != nullptr);

	const GDScriptParser::DataType type = value->get_datatype();
	CHECK(type.kind == GDScriptParser::DataType::TYPE_PARAMETER);
	CHECK(type.type_parameter_name == StringName("T"));
	CHECK(type.type_parameter_scope == GDScriptParser::DataType::TYPE_PARAMETER_CLASS);
	CHECK(type.type_parameter_index == 0);
}

TEST_CASE("[Modules][GDScript] Analyzer resolves a method type parameter in signatures") {
	GDScriptParser parser;
	const Error parse_error = parser.parse("func identity[T](v: T) -> T:\n\treturn v\n", "user://test.gd", false);
	REQUIRE(parse_error == OK);

	GDScriptAnalyzer analyzer(&parser);
	REQUIRE(analyzer.analyze() == OK);

	const GDScriptParser::FunctionNode *identity = generic_find_function(parser.get_tree(), "identity");
	REQUIRE(identity != nullptr);
	REQUIRE(identity->parameters.size() == 1);

	const GDScriptParser::DataType parameter_type = identity->parameters[0]->get_datatype();
	CHECK(parameter_type.kind == GDScriptParser::DataType::TYPE_PARAMETER);
	CHECK(parameter_type.type_parameter_name == StringName("T"));
	CHECK(parameter_type.type_parameter_scope == GDScriptParser::DataType::TYPE_PARAMETER_METHOD);

	const GDScriptParser::DataType return_type = identity->get_datatype();
	CHECK(return_type.kind == GDScriptParser::DataType::TYPE_PARAMETER);
	CHECK(return_type.type_parameter_name == StringName("T"));
}

TEST_CASE("[Modules][GDScript] Analyzer records the bound of a constrained type parameter") {
	GDScriptParser parser;
	const Error parse_error = parser.parse("class Holder[T: RefCounted]:\n\tvar value: T\n", "user://test.gd", false);
	REQUIRE(parse_error == OK);

	GDScriptAnalyzer analyzer(&parser);
	REQUIRE(analyzer.analyze() == OK);

	const GDScriptParser::ClassNode *holder = generic_find_inner_class(parser.get_tree(), "Holder");
	REQUIRE(holder != nullptr);
	const GDScriptParser::VariableNode *value = generic_find_member_variable(holder, "value");
	REQUIRE(value != nullptr);

	const GDScriptParser::DataType type = value->get_datatype();
	REQUIRE(type.kind == GDScriptParser::DataType::TYPE_PARAMETER);
	REQUIRE(type.type_parameter_bound.size() == 1);
	CHECK(type.type_parameter_bound[0].kind == GDScriptParser::DataType::NATIVE);
	CHECK(type.type_parameter_bound[0].native_type == StringName("RefCounted"));
}

TEST_CASE("[Modules][GDScript] Analyzer resolves a specialized class handle into type arguments") {
	GDScriptParser parser;
	const Error parse_error = parser.parse("class Box[T]:\n\tvar value: T\nvar boxed: Box[int]\n", "user://test.gd", false);
	REQUIRE(parse_error == OK);

	GDScriptAnalyzer analyzer(&parser);
	REQUIRE(analyzer.analyze() == OK);

	const GDScriptParser::VariableNode *boxed = generic_find_member_variable(parser.get_tree(), "boxed");
	REQUIRE(boxed != nullptr);

	const GDScriptParser::DataType type = boxed->get_datatype();
	CHECK(type.kind == GDScriptParser::DataType::CLASS);
	REQUIRE(type.type_arguments.size() == 1);
	CHECK(type.type_arguments[0].kind == GDScriptParser::DataType::BUILTIN);
	CHECK(type.type_arguments[0].builtin_type == Variant::INT);
}

TEST_CASE("[Modules][GDScript] Analyzer substitutes type arguments on member access") {
	GDScriptParser parser;
	const String source =
			"class Box[T]:\n"
			"\tvar value: T\n"
			"func test() -> void:\n"
			"\tvar b: Box[int]\n"
			"\tvar got := b.value\n";
	const Error parse_error = parser.parse(source, "user://test.gd", false);
	REQUIRE(parse_error == OK);

	GDScriptAnalyzer analyzer(&parser);
	REQUIRE(analyzer.analyze() == OK);

	const GDScriptParser::FunctionNode *test = generic_find_function(parser.get_tree(), "test");
	REQUIRE(test != nullptr);
	const GDScriptParser::VariableNode *got = generic_find_local_variable(test, "got");
	REQUIRE(got != nullptr);

	const GDScriptParser::DataType type = got->get_datatype();
	CHECK(type.kind == GDScriptParser::DataType::BUILTIN);
	CHECK(type.builtin_type == Variant::INT);
}

TEST_CASE("[Modules][GDScript] Analyzer rejects a wrong type-argument arity") {
	GDScriptParser parser;
	const Error parse_error = parser.parse("class Box[T]:\n\tvar value: T\nvar bad: Box[int, String]\n", "user://test.gd", false);
	REQUIRE(parse_error == OK);

	GDScriptAnalyzer analyzer(&parser);
	const Error analyze_error = analyzer.analyze();
	CHECK(analyze_error != OK);

	bool found_arity_error = false;
	for (const GDScriptParser::ParserError &parser_error : parser.get_errors()) {
		if (parser_error.message.contains("type argument")) {
			found_arity_error = true;
			break;
		}
	}
	CHECK(found_arity_error);
}

TEST_CASE("[Modules][GDScript] Analyzer treats specialized handles as invariant") {
	GDScriptParser parser;
	const String source =
			"class Box[T]:\n"
			"\tvar value: T\n"
			"func same(a: Box[int]) -> void:\n"
			"\tvar b: Box[int] = a\n"
			"func different(a: Box[int]) -> void:\n"
			"\tvar b: Box[String] = a\n";
	const Error parse_error = parser.parse(source, "user://test.gd", false);
	REQUIRE(parse_error == OK);

	GDScriptAnalyzer analyzer(&parser);
	const Error analyze_error = analyzer.analyze();
	CHECK(analyze_error != OK);

	// The only error must come from the mismatched specialization, not the matching one.
	REQUIRE(parser.get_errors().size() == 1);
}

TEST_CASE("[Modules][GDScript] Analyzer rejects specializing a non-generic class") {
	GDScriptParser parser;
	const Error parse_error = parser.parse("class Plain:\n\tpass\nvar bad: Plain[int]\n", "user://test.gd", false);
	REQUIRE(parse_error == OK);

	GDScriptAnalyzer analyzer(&parser);
	const Error analyze_error = analyzer.analyze();
	CHECK(analyze_error != OK);
}

TEST_CASE("[Modules][GDScript] Analyzer accepts a type argument that satisfies a class bound") {
	GDScriptParser parser;
	// `Resource` derives from `RefCounted`, so it satisfies the upper bound.
	const Error parse_error = parser.parse("class Box[T: RefCounted]:\n\tvar value: T\nvar ok: Box[Resource]\n", "user://test.gd", false);
	REQUIRE(parse_error == OK);

	GDScriptAnalyzer analyzer(&parser);
	CHECK(analyzer.analyze() == OK);

	const GDScriptParser::VariableNode *ok = generic_find_member_variable(parser.get_tree(), "ok");
	REQUIRE(ok != nullptr);
	const GDScriptParser::DataType type = ok->get_datatype();
	CHECK(type.kind == GDScriptParser::DataType::CLASS);
	REQUIRE(type.type_arguments.size() == 1);
	CHECK(type.type_arguments[0].native_type == StringName("Resource"));
}

TEST_CASE("[Modules][GDScript] Analyzer rejects a type argument that violates a class bound") {
	GDScriptParser parser;
	// `Object` is a supertype of `RefCounted`, so it does not satisfy the bound.
	const Error parse_error = parser.parse("class Box[T: RefCounted]:\n\tvar value: T\nvar bad: Box[Object]\n", "user://test.gd", false);
	REQUIRE(parse_error == OK);

	GDScriptAnalyzer analyzer(&parser);
	CHECK(analyzer.analyze() != OK);

	bool found_bound_error = false;
	for (const GDScriptParser::ParserError &parser_error : parser.get_errors()) {
		if (parser_error.message.contains("bound")) {
			found_bound_error = true;
			break;
		}
	}
	CHECK(found_bound_error);
}

TEST_CASE("[Modules][GDScript] Analyzer rejects a builtin type argument against a class bound") {
	GDScriptParser parser;
	const Error parse_error = parser.parse("class Box[T: RefCounted]:\n\tvar value: T\nvar bad: Box[int]\n", "user://test.gd", false);
	REQUIRE(parse_error == OK);

	GDScriptAnalyzer analyzer(&parser);
	CHECK(analyzer.analyze() != OK);
}

TEST_CASE("[Modules][GDScript] Analyzer accepts a user-class type argument satisfying a user-class bound") {
	GDScriptParser parser;
	const String source =
			"class Animal:\n"
			"\tpass\n"
			"class Dog extends Animal:\n"
			"\tpass\n"
			"class Box[T: Animal]:\n"
			"\tvar value: T\n"
			"var ok: Box[Dog]\n";
	const Error parse_error = parser.parse(source, "user://test.gd", false);
	REQUIRE(parse_error == OK);

	GDScriptAnalyzer analyzer(&parser);
	CHECK(analyzer.analyze() == OK);
}

TEST_CASE("[Modules][GDScript] Analyzer rejects a concrete Variant type argument against a class bound") {
	GDScriptParser parser;
	const Error parse_error = parser.parse("class Box[T: RefCounted]:\n\tvar value: T\nvar bad: Box[Variant]\n", "user://test.gd", false);
	REQUIRE(parse_error == OK);

	GDScriptAnalyzer analyzer(&parser);
	CHECK(analyzer.analyze() != OK);

	bool found_bound_error = false;
	for (const GDScriptParser::ParserError &parser_error : parser.get_errors()) {
		if (parser_error.message.contains("bound")) {
			found_bound_error = true;
			break;
		}
	}
	CHECK(found_bound_error);
}

TEST_CASE("[Modules][GDScript] Analyzer rejects a weakly-bounded type parameter as a type argument") {
	GDScriptParser parser;
	// `U` is only bounded by `Object`, so it cannot satisfy `Box`'s `RefCounted` bound.
	const String source =
			"class Box[T: RefCounted]:\n"
			"\tvar value: T\n"
			"class Wrapper[U: Object]:\n"
			"\tvar bad: Box[U]\n";
	const Error parse_error = parser.parse(source, "user://test.gd", false);
	REQUIRE(parse_error == OK);

	GDScriptAnalyzer analyzer(&parser);
	CHECK(analyzer.analyze() != OK);
}

TEST_CASE("[Modules][GDScript] Analyzer accepts a compatibly-bounded type parameter as a type argument") {
	GDScriptParser parser;
	// `U` is bounded by `Resource`, which derives from `RefCounted`, so it satisfies the bound.
	const String source =
			"class Box[T: RefCounted]:\n"
			"\tvar value: T\n"
			"class Wrapper[U: Resource]:\n"
			"\tvar ok: Box[U]\n";
	const Error parse_error = parser.parse(source, "user://test.gd", false);
	REQUIRE(parse_error == OK);

	GDScriptAnalyzer analyzer(&parser);
	CHECK(analyzer.analyze() == OK);
}

TEST_CASE("[Modules][GDScript] Analyzer rejects an unbounded type parameter as a type argument") {
	GDScriptParser parser;
	const String source =
			"class Box[T: RefCounted]:\n"
			"\tvar value: T\n"
			"class Wrapper[U]:\n"
			"\tvar bad: Box[U]\n";
	const Error parse_error = parser.parse(source, "user://test.gd", false);
	REQUIRE(parse_error == OK);

	GDScriptAnalyzer analyzer(&parser);
	CHECK(analyzer.analyze() != OK);
}

TEST_CASE("[Modules][GDScript] Analyzer rejects a Variant-bounded type parameter as a type argument") {
	GDScriptParser parser;
	// `U` is bounded only by `Variant`, so it cannot satisfy `Box`'s `RefCounted` bound.
	const String source =
			"class Box[T: RefCounted]:\n"
			"\tvar value: T\n"
			"class Wrapper[U: Variant]:\n"
			"\tvar bad: Box[U]\n";
	const Error parse_error = parser.parse(source, "user://test.gd", false);
	REQUIRE(parse_error == OK);

	GDScriptAnalyzer analyzer(&parser);
	CHECK(analyzer.analyze() != OK);
}

TEST_CASE("[Modules][GDScript] Analyzer rejects a dependent bound against the wrong sibling argument") {
	GDScriptParser parser;
	// `T: U` requires the second argument to derive from whatever `U` is bound to. `U` is given
	// `Resource`, and `RefCounted` does not derive from `Resource`, so this must be rejected.
	const String source =
			"class Box[U: Resource, T: U]:\n"
			"\tvar value: T\n"
			"var bad: Box[Resource, RefCounted]\n";
	const Error parse_error = parser.parse(source, "user://test.gd", false);
	REQUIRE(parse_error == OK);

	GDScriptAnalyzer analyzer(&parser);
	CHECK(analyzer.analyze() != OK);

	bool found_bound_error = false;
	for (const GDScriptParser::ParserError &parser_error : parser.get_errors()) {
		if (parser_error.message.contains("bound")) {
			found_bound_error = true;
			break;
		}
	}
	CHECK(found_bound_error);
}

TEST_CASE("[Modules][GDScript] Analyzer rejects a forward dependent bound against the wrong sibling argument") {
	GDScriptParser parser;
	// `T: U` references a parameter declared after it. `U` is given `PackedScene`, and `Resource`
	// does not derive from `PackedScene`, so the first argument must be rejected.
	const String source =
			"class Box[T: U, U: Resource]:\n"
			"\tvar value: T\n"
			"var bad: Box[Resource, PackedScene]\n";
	const Error parse_error = parser.parse(source, "user://test.gd", false);
	REQUIRE(parse_error == OK);

	GDScriptAnalyzer analyzer(&parser);
	CHECK(analyzer.analyze() != OK);

	bool found_bound_error = false;
	for (const GDScriptParser::ParserError &parser_error : parser.get_errors()) {
		if (parser_error.message.contains("bound")) {
			found_bound_error = true;
			break;
		}
	}
	CHECK(found_bound_error);
}

TEST_CASE("[Modules][GDScript] Analyzer accepts a forward dependent bound against a satisfying sibling argument") {
	GDScriptParser parser;
	// `U` is given `Resource`; `PackedScene` derives from `Resource`, so `T: U` with `T = PackedScene`
	// is satisfied.
	const String source =
			"class Box[T: U, U: Resource]:\n"
			"\tvar value: T\n"
			"var ok: Box[PackedScene, Resource]\n";
	const Error parse_error = parser.parse(source, "user://test.gd", false);
	REQUIRE(parse_error == OK);

	GDScriptAnalyzer analyzer(&parser);
	CHECK(analyzer.analyze() == OK);
}

TEST_CASE("[Modules][GDScript] Analyzer accepts a dependent bound against a satisfying sibling argument") {
	GDScriptParser parser;
	// `U` is given `Resource`; `Resource` derives from `Resource`, so `T: U` is satisfied.
	const String source =
			"class Box[U: Resource, T: U]:\n"
			"\tvar value: T\n"
			"var ok: Box[Resource, Resource]\n";
	const Error parse_error = parser.parse(source, "user://test.gd", false);
	REQUIRE(parse_error == OK);

	GDScriptAnalyzer analyzer(&parser);
	CHECK(analyzer.analyze() == OK);
}

TEST_CASE("[Modules][GDScript] Analyzer resolves a class bound ignoring shadowing method parameters") {
	GDScriptParser parser;
	// The method type parameter `Real` must not shadow the class `Real` named by `Box`'s bound;
	// `Object` does not derive from the class `Real`, so the specialization must be rejected.
	const String source =
			"class Real:\n"
			"\tpass\n"
			"class Box[T: Real]:\n"
			"\tvar value: T\n"
			"func shadowing[Real]() -> void:\n"
			"\tvar bad: Box[Object]\n";
	const Error parse_error = parser.parse(source, "user://test.gd", false);
	REQUIRE(parse_error == OK);

	GDScriptAnalyzer analyzer(&parser);
	CHECK(analyzer.analyze() != OK);

	bool found_bound_error = false;
	for (const GDScriptParser::ParserError &parser_error : parser.get_errors()) {
		if (parser_error.message.contains("bound")) {
			found_bound_error = true;
			break;
		}
	}
	CHECK(found_bound_error);
}

TEST_CASE("[Modules][GDScript] Analyzer does not let a method type parameter poison a cached class bound") {
	GDScriptParser parser;
	// Resolving `item: T` inside `poison` first touches `T`'s bound while the method type parameter
	// `Real` is in scope. The class bound must still resolve to the class `Real`, so `Box[Object]`
	// is rejected.
	const String source =
			"class Real:\n"
			"\tpass\n"
			"class Box[T: Real]:\n"
			"\tvar value: T\n"
			"\tfunc poison[Real](item: T) -> void:\n"
			"\t\tpass\n"
			"var bad: Box[Object]\n";
	const Error parse_error = parser.parse(source, "user://test.gd", false);
	REQUIRE(parse_error == OK);

	GDScriptAnalyzer analyzer(&parser);
	CHECK(analyzer.analyze() != OK);

	bool found_bound_error = false;
	for (const GDScriptParser::ParserError &parser_error : parser.get_errors()) {
		if (parser_error.message.contains("bound")) {
			found_bound_error = true;
			break;
		}
	}
	CHECK(found_bound_error);
}

TEST_CASE("[Modules][GDScript] Analyzer exposes bound members on a type-parameter value") {
	GDScriptParser parser;
	// `value` has the type parameter type `T` bound by `RefCounted`, so calling a
	// `RefCounted` method on it must resolve through the bound to its `int` return.
	const String source =
			"class Box[T: RefCounted]:\n"
			"\tvar value: T\n"
			"\tfunc test() -> void:\n"
			"\t\tvar refs := value.get_reference_count()\n";
	const Error parse_error = parser.parse(source, "user://test.gd", false);
	REQUIRE(parse_error == OK);

	GDScriptAnalyzer analyzer(&parser);
	REQUIRE(analyzer.analyze() == OK);

	const GDScriptParser::ClassNode *box = generic_find_inner_class(parser.get_tree(), "Box");
	REQUIRE(box != nullptr);
	const GDScriptParser::FunctionNode *test = generic_find_function(box, "test");
	REQUIRE(test != nullptr);
	const GDScriptParser::VariableNode *refs = generic_find_local_variable(test, "refs");
	REQUIRE(refs != nullptr);

	const GDScriptParser::DataType type = refs->get_datatype();
	CHECK(type.kind == GDScriptParser::DataType::BUILTIN);
	CHECK(type.builtin_type == Variant::INT);
}

TEST_CASE("[Modules][GDScript] Analyzer infers a method type parameter from an argument") {
	GDScriptParser parser;
	// `identity(42)` solves `T = int` by unifying the argument `42` against the declared
	// parameter type `T`, so the call's result type is the substituted `int`.
	const String source =
			"func identity[T](value: T) -> T:\n"
			"\treturn value\n"
			"func test() -> void:\n"
			"\tvar n := identity(42)\n";
	const Error parse_error = parser.parse(source, "user://test.gd", false);
	REQUIRE(parse_error == OK);

	GDScriptAnalyzer analyzer(&parser);
	REQUIRE(analyzer.analyze() == OK);

	const GDScriptParser::FunctionNode *test = generic_find_function(parser.get_tree(), "test");
	REQUIRE(test != nullptr);
	const GDScriptParser::VariableNode *n = generic_find_local_variable(test, "n");
	REQUIRE(n != nullptr);

	const GDScriptParser::DataType type = n->get_datatype();
	CHECK(type.kind == GDScriptParser::DataType::BUILTIN);
	CHECK(type.builtin_type == Variant::INT);
}

TEST_CASE("[Modules][GDScript] Analyzer infers a type parameter through a container argument") {
	GDScriptParser parser;
	// Unifying `Array[int]` against the declared `Array[T]` solves `T = int`, so `first`
	// returns `int`.
	const String source =
			"func first[T](items: Array[T]) -> T:\n"
			"\treturn items[0]\n"
			"func test() -> void:\n"
			"\tvar values: Array[int] = [1, 2]\n"
			"\tvar got := first(values)\n";
	const Error parse_error = parser.parse(source, "user://test.gd", false);
	REQUIRE(parse_error == OK);

	GDScriptAnalyzer analyzer(&parser);
	REQUIRE(analyzer.analyze() == OK);

	const GDScriptParser::FunctionNode *test = generic_find_function(parser.get_tree(), "test");
	REQUIRE(test != nullptr);
	const GDScriptParser::VariableNode *got = generic_find_local_variable(test, "got");
	REQUIRE(got != nullptr);

	const GDScriptParser::DataType type = got->get_datatype();
	CHECK(type.kind == GDScriptParser::DataType::BUILTIN);
	CHECK(type.builtin_type == Variant::INT);
}

TEST_CASE("[Modules][GDScript] Analyzer substitutes a container return type from inference") {
	GDScriptParser parser;
	// `swap(1, 2)` solves `T = int`, so the declared `Array[T]` return becomes `Array[int]`.
	const String source =
			"func swap[T](a: T, b: T) -> Array[T]:\n"
			"\treturn [b, a]\n"
			"func test() -> void:\n"
			"\tvar pair := swap(1, 2)\n";
	const Error parse_error = parser.parse(source, "user://test.gd", false);
	REQUIRE(parse_error == OK);

	GDScriptAnalyzer analyzer(&parser);
	REQUIRE(analyzer.analyze() == OK);

	const GDScriptParser::FunctionNode *test = generic_find_function(parser.get_tree(), "test");
	REQUIRE(test != nullptr);
	const GDScriptParser::VariableNode *pair = generic_find_local_variable(test, "pair");
	REQUIRE(pair != nullptr);

	const GDScriptParser::DataType type = pair->get_datatype();
	CHECK(type.kind == GDScriptParser::DataType::BUILTIN);
	CHECK(type.builtin_type == Variant::ARRAY);
	REQUIRE(type.has_container_element_type(0));
	CHECK(type.get_container_element_type(0).builtin_type == Variant::INT);
}

TEST_CASE("[Modules][GDScript] Analyzer rejects conflicting inferred type parameters") {
	GDScriptParser parser;
	// `swap(1, "x")` binds `T` to both `int` and `String`, which conflict, so inference fails.
	const String source =
			"func swap[T](a: T, b: T) -> Array[T]:\n"
			"\treturn [b, a]\n"
			"func test() -> void:\n"
			"\tvar pair := swap(1, \"x\")\n";
	const Error parse_error = parser.parse(source, "user://test.gd", false);
	REQUIRE(parse_error == OK);

	GDScriptAnalyzer analyzer(&parser);
	CHECK(analyzer.analyze() != OK);

	bool found_inference_error = false;
	for (const GDScriptParser::ParserError &parser_error : parser.get_errors()) {
		if (parser_error.message.contains("infer")) {
			found_inference_error = true;
			break;
		}
	}
	CHECK(found_inference_error);
}

TEST_CASE("[Modules][GDScript] Analyzer rejects an unsolved type parameter") {
	GDScriptParser parser;
	// `T` appears only in the return type, so no argument constrains it and inference cannot solve it.
	const String source =
			"func make[T]() -> T:\n"
			"\treturn null\n"
			"func test() -> void:\n"
			"\tvar value := make()\n";
	const Error parse_error = parser.parse(source, "user://test.gd", false);
	REQUIRE(parse_error == OK);

	GDScriptAnalyzer analyzer(&parser);
	CHECK(analyzer.analyze() != OK);

	bool found_inference_error = false;
	for (const GDScriptParser::ParserError &parser_error : parser.get_errors()) {
		if (parser_error.message.contains("infer")) {
			found_inference_error = true;
			break;
		}
	}
	CHECK(found_inference_error);
}

TEST_CASE("[Modules][GDScript] Analyzer applies an explicit method type argument") {
	GDScriptParser parser;
	// Explicit `[int]` short-circuits inference, so `swap[int](1, 2)` returns `Array[int]`.
	const String source =
			"func swap[T](a: T, b: T) -> Array[T]:\n"
			"\treturn [b, a]\n"
			"func test() -> void:\n"
			"\tvar pair := swap[int](1, 2)\n";
	const Error parse_error = parser.parse(source, "user://test.gd", false);
	REQUIRE(parse_error == OK);

	GDScriptAnalyzer analyzer(&parser);
	REQUIRE(analyzer.analyze() == OK);

	const GDScriptParser::FunctionNode *test = generic_find_function(parser.get_tree(), "test");
	REQUIRE(test != nullptr);
	const GDScriptParser::VariableNode *pair = generic_find_local_variable(test, "pair");
	REQUIRE(pair != nullptr);

	const GDScriptParser::DataType type = pair->get_datatype();
	CHECK(type.kind == GDScriptParser::DataType::BUILTIN);
	CHECK(type.builtin_type == Variant::ARRAY);
	REQUIRE(type.has_container_element_type(0));
	CHECK(type.get_container_element_type(0).builtin_type == Variant::INT);
}

TEST_CASE("[Modules][GDScript] Analyzer lets an explicit type argument override inference") {
	GDScriptParser parser;
	// The arguments would infer `int`, but `[float]` is applied explicitly instead.
	const String source =
			"func box[T](value: T) -> Array[T]:\n"
			"\treturn [value]\n"
			"func test() -> void:\n"
			"\tvar boxed := box[float](1)\n";
	const Error parse_error = parser.parse(source, "user://test.gd", false);
	REQUIRE(parse_error == OK);

	GDScriptAnalyzer analyzer(&parser);
	REQUIRE(analyzer.analyze() == OK);

	const GDScriptParser::FunctionNode *test = generic_find_function(parser.get_tree(), "test");
	REQUIRE(test != nullptr);
	const GDScriptParser::VariableNode *boxed = generic_find_local_variable(test, "boxed");
	REQUIRE(boxed != nullptr);

	const GDScriptParser::DataType type = boxed->get_datatype();
	REQUIRE(type.has_container_element_type(0));
	CHECK(type.get_container_element_type(0).builtin_type == Variant::FLOAT);
}

TEST_CASE("[Modules][GDScript] Analyzer rejects an inferred type argument that violates a method bound") {
	GDScriptParser parser;
	// `T` is bounded by `RefCounted`; inferring `T = int` from the argument violates that bound.
	const String source =
			"func keep[T: RefCounted](value: T) -> T:\n"
			"\treturn value\n"
			"func test() -> void:\n"
			"\tvar bad := keep(1)\n";
	const Error parse_error = parser.parse(source, "user://test.gd", false);
	REQUIRE(parse_error == OK);

	GDScriptAnalyzer analyzer(&parser);
	CHECK(analyzer.analyze() != OK);

	bool found_bound_error = false;
	for (const GDScriptParser::ParserError &parser_error : parser.get_errors()) {
		if (parser_error.message.contains("bound")) {
			found_bound_error = true;
			break;
		}
	}
	CHECK(found_bound_error);
}

TEST_CASE("[Modules][GDScript] Analyzer accepts an inferred type argument that satisfies a method bound") {
	GDScriptParser parser;
	// `Resource` derives from `RefCounted`, so inferring `T = Resource` satisfies the bound.
	const String source =
			"func keep[T: RefCounted](value: T) -> T:\n"
			"\treturn value\n"
			"func test() -> void:\n"
			"\tvar res := Resource.new()\n"
			"\tvar ok := keep(res)\n";
	const Error parse_error = parser.parse(source, "user://test.gd", false);
	REQUIRE(parse_error == OK);

	GDScriptAnalyzer analyzer(&parser);
	CHECK(analyzer.analyze() == OK);
}

TEST_CASE("[Modules][GDScript] Analyzer rejects an explicit type argument that violates a method bound") {
	GDScriptParser parser;
	// Explicit `[int]` is applied directly but must still satisfy the `RefCounted` bound.
	const String source =
			"func keep[T: RefCounted](value: T) -> T:\n"
			"\treturn value\n"
			"func test() -> void:\n"
			"\tvar bad := keep[int](null)\n";
	const Error parse_error = parser.parse(source, "user://test.gd", false);
	REQUIRE(parse_error == OK);

	GDScriptAnalyzer analyzer(&parser);
	CHECK(analyzer.analyze() != OK);

	bool found_bound_error = false;
	for (const GDScriptParser::ParserError &parser_error : parser.get_errors()) {
		if (parser_error.message.contains("bound")) {
			found_bound_error = true;
			break;
		}
	}
	CHECK(found_bound_error);
}

TEST_CASE("[Modules][GDScript] Analyzer treats inferred type parameters as invariant") {
	GDScriptParser parser;
	// `T` is solved from both arguments; `int` and `float` are different types, so they conflict
	// rather than widening (type parameters are invariant).
	const String source =
			"func choose[T](a: T, _b: T) -> T:\n"
			"\treturn a\n"
			"func test() -> void:\n"
			"\tvar bad := choose(1, 2.0)\n";
	const Error parse_error = parser.parse(source, "user://test.gd", false);
	REQUIRE(parse_error == OK);

	GDScriptAnalyzer analyzer(&parser);
	CHECK(analyzer.analyze() != OK);

	bool found_inference_error = false;
	for (const GDScriptParser::ParserError &parser_error : parser.get_errors()) {
		if (parser_error.message.contains("infer")) {
			found_inference_error = true;
			break;
		}
	}
	CHECK(found_inference_error);
}

TEST_CASE("[Modules][GDScript] Analyzer checks a method bound used only in the body") {
	GDScriptParser parser;
	// `T` is bounded by `RefCounted` and appears only in the body, not in the signature. An
	// explicit `[int]` must still be rejected against the bound.
	const String source =
			"func use_t[T: RefCounted]() -> void:\n"
			"\tvar _x: T\n"
			"func test() -> void:\n"
			"\tuse_t[int]()\n";
	const Error parse_error = parser.parse(source, "user://test.gd", false);
	REQUIRE(parse_error == OK);

	GDScriptAnalyzer analyzer(&parser);
	CHECK(analyzer.analyze() != OK);

	bool found_bound_error = false;
	for (const GDScriptParser::ParserError &parser_error : parser.get_errors()) {
		if (parser_error.message.contains("bound")) {
			found_bound_error = true;
			break;
		}
	}
	CHECK(found_bound_error);
}

TEST_CASE("[Modules][GDScript] Analyzer checks a dependent method bound against the sibling argument") {
	GDScriptParser parser;
	// `T: U`; `U` is inferred `PackedScene`, so `T` must derive from `PackedScene`. `Resource` does
	// not, so the inferred `T = Resource` must be rejected.
	const String source =
			"func dep[U: Resource, T: U](u: U, t: T) -> void:\n"
			"\tpass\n"
			"func test(ps: PackedScene, r: Resource) -> void:\n"
			"\tdep(ps, r)\n";
	const Error parse_error = parser.parse(source, "user://test.gd", false);
	REQUIRE(parse_error == OK);

	GDScriptAnalyzer analyzer(&parser);
	CHECK(analyzer.analyze() != OK);

	bool found_bound_error = false;
	for (const GDScriptParser::ParserError &parser_error : parser.get_errors()) {
		if (parser_error.message.contains("bound")) {
			found_bound_error = true;
			break;
		}
	}
	CHECK(found_bound_error);
}

TEST_CASE("[Modules][GDScript] Analyzer accepts a satisfying dependent method bound") {
	GDScriptParser parser;
	// `U` is inferred `Resource` and `T` is inferred `PackedScene`, which derives from `Resource`,
	// so `T: U` is satisfied.
	const String source =
			"func dep[U: Resource, T: U](u: U, t: T) -> void:\n"
			"\tpass\n"
			"func test(r: Resource, ps: PackedScene) -> void:\n"
			"\tdep(r, ps)\n";
	const Error parse_error = parser.parse(source, "user://test.gd", false);
	REQUIRE(parse_error == OK);

	GDScriptAnalyzer analyzer(&parser);
	CHECK(analyzer.analyze() == OK);
}

TEST_CASE("[Modules][GDScript] Analyzer rejects unifying a type parameter with a concrete type") {
	GDScriptParser parser;
	// `pair` is called with one outer type-parameter argument (`x: U`) and one `int`. `U` and `int`
	// are different, so `T` cannot be solved and must conflict rather than silently merge.
	const String source =
			"func pair[T](a: T, _b: T) -> T:\n"
			"\treturn a\n"
			"func outer[U](x: U) -> void:\n"
			"\tvar _r := pair(x, 1)\n";
	const Error parse_error = parser.parse(source, "user://test.gd", false);
	REQUIRE(parse_error == OK);

	GDScriptAnalyzer analyzer(&parser);
	CHECK(analyzer.analyze() != OK);

	bool found_inference_error = false;
	for (const GDScriptParser::ParserError &parser_error : parser.get_errors()) {
		if (parser_error.message.contains("infer")) {
			found_inference_error = true;
			break;
		}
	}
	CHECK(found_inference_error);
}

TEST_CASE("[Modules][GDScript] Analyzer unifies a type parameter with the same type parameter") {
	GDScriptParser parser;
	// Forwarding a single outer `U` into both `T` positions solves `T = U` consistently.
	const String source =
			"func pair[T](a: T, _b: T) -> T:\n"
			"\treturn a\n"
			"func outer[U](x: U) -> void:\n"
			"\tvar _r := pair(x, x)\n";
	const Error parse_error = parser.parse(source, "user://test.gd", false);
	REQUIRE(parse_error == OK);

	GDScriptAnalyzer analyzer(&parser);
	CHECK(analyzer.analyze() == OK);
}

TEST_CASE("[Modules][GDScript] Analyzer treats a shadowed generic-method name as an index call") {
	GDScriptParser parser;
	// A local `gen` shadows the generic method `gen`, so `gen[0]()` is a call on an index of the
	// local array, not a generic application.
	const String source =
			"func gen[T](v: T) -> T:\n"
			"\treturn v\n"
			"func test() -> void:\n"
			"\tvar gen := [10, 20]\n"
			"\tvar _x = gen[0]()\n";
	const Error parse_error = parser.parse(source, "user://test.gd", false);
	REQUIRE(parse_error == OK);

	GDScriptAnalyzer analyzer(&parser);
	CHECK(analyzer.analyze() != OK);

	bool found_expression_error = false;
	for (const GDScriptParser::ParserError &parser_error : parser.get_errors()) {
		if (parser_error.message.contains("Cannot call on an expression")) {
			found_expression_error = true;
			break;
		}
	}
	CHECK(found_expression_error);
}

TEST_CASE("[Modules][GDScript] Analyzer applies a generic method before a later same-named local") {
	GDScriptParser parser;
	// `gen[int](1)` precedes the local `gen`, so the name still resolves to the generic method at
	// that position; a local declared later in the block must not shadow it retroactively.
	const String source =
			"func gen[T](v: T) -> T:\n"
			"\treturn v\n"
			"func test() -> void:\n"
			"\tvar a := gen[int](1)\n"
			"\tprint(a)\n"
			"\tvar gen := [10]\n"
			"\tprint(gen)\n";
	const Error parse_error = parser.parse(source, "user://test.gd", false);
	REQUIRE(parse_error == OK);

	GDScriptAnalyzer analyzer(&parser);
	CHECK(analyzer.analyze() == OK);

	const GDScriptParser::FunctionNode *test = generic_find_function(parser.get_tree(), "test");
	REQUIRE(test != nullptr);
	const GDScriptParser::VariableNode *a = generic_find_local_variable(test, "a");
	REQUIRE(a != nullptr);
	CHECK(a->get_datatype().builtin_type == Variant::INT);
}

TEST_CASE("[Modules][GDScript] Analyzer enforces a body-only method bound regardless of source order") {
	GDScriptParser parser;
	// `T: RefCounted` appears only in the body of `use_t`, which is declared after its caller. The
	// bound must still be enforced against the explicit `[int]`.
	const String source =
			"func test() -> void:\n"
			"\tuse_t[int]()\n"
			"func use_t[T: RefCounted]() -> void:\n"
			"\tvar _x: T\n";
	const Error parse_error = parser.parse(source, "user://test.gd", false);
	REQUIRE(parse_error == OK);

	GDScriptAnalyzer analyzer(&parser);
	CHECK(analyzer.analyze() != OK);

	bool found_bound_error = false;
	for (const GDScriptParser::ParserError &parser_error : parser.get_errors()) {
		if (parser_error.message.contains("bound")) {
			found_bound_error = true;
			break;
		}
	}
	CHECK(found_bound_error);
}

TEST_CASE("[Modules][GDScript] Analyzer keeps an unbounded type-parameter value dynamic") {
	GDScriptParser parser;
	// Without a bound, `T` exposes no members, so member access stays dynamic rather than
	// gaining any concrete surface.
	const String source =
			"class Box[T]:\n"
			"\tvar value: T\n"
			"\tfunc test() -> void:\n"
			"\t\tvar refs = value.get_reference_count()\n";
	const Error parse_error = parser.parse(source, "user://test.gd", false);
	REQUIRE(parse_error == OK);

	GDScriptAnalyzer analyzer(&parser);
	REQUIRE(analyzer.analyze() == OK);

	const GDScriptParser::ClassNode *box = generic_find_inner_class(parser.get_tree(), "Box");
	REQUIRE(box != nullptr);
	const GDScriptParser::FunctionNode *test = generic_find_function(box, "test");
	REQUIRE(test != nullptr);
	const GDScriptParser::VariableNode *refs = generic_find_local_variable(test, "refs");
	REQUIRE(refs != nullptr);

	// An unbounded parameter has no static surface, so the value stays a Variant.
	CHECK(refs->get_datatype().is_variant());
}

TEST_CASE("[Modules][GDScript] Analyzer rebinds a parent type parameter through inheritance") {
	GDScriptParser parser;
	// `Stack[U] extends List[U]` rebinds `List`'s parameter to `Stack`'s own `U`.
	const String source =
			"class List[T]:\n"
			"\tvar head: T\n"
			"class Stack[U] extends List[U]:\n"
			"\tpass\n";
	const Error parse_error = parser.parse(source, "user://test.gd", false);
	REQUIRE(parse_error == OK);

	GDScriptAnalyzer analyzer(&parser);
	REQUIRE(analyzer.analyze() == OK);

	const GDScriptParser::ClassNode *stack = generic_find_inner_class(parser.get_tree(), "Stack");
	REQUIRE(stack != nullptr);

	// The resolved base carries one type argument: the child's own parameter `U`.
	const GDScriptParser::DataType base = stack->base_type;
	CHECK(base.kind == GDScriptParser::DataType::CLASS);
	REQUIRE(base.type_arguments.size() == 1);
	CHECK(base.type_arguments[0].kind == GDScriptParser::DataType::TYPE_PARAMETER);
	CHECK(base.type_arguments[0].type_parameter_name == StringName("U"));
}

TEST_CASE("[Modules][GDScript] Analyzer binds a concrete type argument through inheritance") {
	GDScriptParser parser;
	// `IntList extends List[int]` binds `List`'s parameter to a concrete type.
	const String source =
			"class List[T]:\n"
			"\tvar head: T\n"
			"class IntList extends List[int]:\n"
			"\tpass\n";
	const Error parse_error = parser.parse(source, "user://test.gd", false);
	REQUIRE(parse_error == OK);

	GDScriptAnalyzer analyzer(&parser);
	REQUIRE(analyzer.analyze() == OK);

	const GDScriptParser::ClassNode *int_list = generic_find_inner_class(parser.get_tree(), "IntList");
	REQUIRE(int_list != nullptr);

	const GDScriptParser::DataType base = int_list->base_type;
	CHECK(base.kind == GDScriptParser::DataType::CLASS);
	REQUIRE(base.type_arguments.size() == 1);
	CHECK(base.type_arguments[0].kind == GDScriptParser::DataType::BUILTIN);
	CHECK(base.type_arguments[0].builtin_type == Variant::INT);
}

TEST_CASE("[Modules][GDScript] Analyzer substitutes an inherited member through a concrete base") {
	GDScriptParser parser;
	// Accessing the inherited `head` on an `IntList` substitutes `List`'s `T` to `int`.
	const String source =
			"class List[T]:\n"
			"\tvar head: T\n"
			"class IntList extends List[int]:\n"
			"\tpass\n"
			"func test() -> void:\n"
			"\tvar list: IntList\n"
			"\tvar got := list.head\n";
	const Error parse_error = parser.parse(source, "user://test.gd", false);
	REQUIRE(parse_error == OK);

	GDScriptAnalyzer analyzer(&parser);
	REQUIRE(analyzer.analyze() == OK);

	const GDScriptParser::FunctionNode *test = generic_find_function(parser.get_tree(), "test");
	REQUIRE(test != nullptr);
	const GDScriptParser::VariableNode *got = generic_find_local_variable(test, "got");
	REQUIRE(got != nullptr);

	const GDScriptParser::DataType type = got->get_datatype();
	CHECK(type.kind == GDScriptParser::DataType::BUILTIN);
	CHECK(type.builtin_type == Variant::INT);
}

TEST_CASE("[Modules][GDScript] Analyzer substitutes an inherited member through a specialized handle") {
	GDScriptParser parser;
	// A `Stack[int]` use site flows `int` up through `Stack[U] extends List[U]` to `List`'s `head`.
	const String source =
			"class List[T]:\n"
			"\tvar head: T\n"
			"class Stack[U] extends List[U]:\n"
			"\tpass\n"
			"func test() -> void:\n"
			"\tvar stack: Stack[int]\n"
			"\tvar got := stack.head\n";
	const Error parse_error = parser.parse(source, "user://test.gd", false);
	REQUIRE(parse_error == OK);

	GDScriptAnalyzer analyzer(&parser);
	REQUIRE(analyzer.analyze() == OK);

	const GDScriptParser::FunctionNode *test = generic_find_function(parser.get_tree(), "test");
	REQUIRE(test != nullptr);
	const GDScriptParser::VariableNode *got = generic_find_local_variable(test, "got");
	REQUIRE(got != nullptr);

	const GDScriptParser::DataType type = got->get_datatype();
	CHECK(type.kind == GDScriptParser::DataType::BUILTIN);
	CHECK(type.builtin_type == Variant::INT);
}

TEST_CASE("[Modules][GDScript] Analyzer substitutes an inherited member across multiple levels") {
	GDScriptParser parser;
	// Two levels of rebinding: `Grid extends Matrix[int]`, `Matrix[V] extends Cell[V]`.
	const String source =
			"class Cell[T]:\n"
			"\tvar value: T\n"
			"class Matrix[V] extends Cell[V]:\n"
			"\tpass\n"
			"class Grid extends Matrix[int]:\n"
			"\tpass\n"
			"func test() -> void:\n"
			"\tvar grid: Grid\n"
			"\tvar got := grid.value\n";
	const Error parse_error = parser.parse(source, "user://test.gd", false);
	REQUIRE(parse_error == OK);

	GDScriptAnalyzer analyzer(&parser);
	REQUIRE(analyzer.analyze() == OK);

	const GDScriptParser::FunctionNode *test = generic_find_function(parser.get_tree(), "test");
	REQUIRE(test != nullptr);
	const GDScriptParser::VariableNode *got = generic_find_local_variable(test, "got");
	REQUIRE(got != nullptr);

	const GDScriptParser::DataType type = got->get_datatype();
	CHECK(type.kind == GDScriptParser::DataType::BUILTIN);
	CHECK(type.builtin_type == Variant::INT);
}

TEST_CASE("[Modules][GDScript] Analyzer substitutes an inherited method return through inheritance") {
	GDScriptParser parser;
	// `IntList extends List[int]` makes the inherited `get_head() -> T` resolve to `-> int`.
	const String source =
			"class List[T]:\n"
			"\tvar head: T\n"
			"\tfunc get_head() -> T:\n"
			"\t\treturn head\n"
			"class IntList extends List[int]:\n"
			"\tpass\n"
			"func test() -> void:\n"
			"\tvar list: IntList\n"
			"\tvar got := list.get_head()\n";
	const Error parse_error = parser.parse(source, "user://test.gd", false);
	REQUIRE(parse_error == OK);

	GDScriptAnalyzer analyzer(&parser);
	REQUIRE(analyzer.analyze() == OK);

	const GDScriptParser::FunctionNode *test = generic_find_function(parser.get_tree(), "test");
	REQUIRE(test != nullptr);
	const GDScriptParser::VariableNode *got = generic_find_local_variable(test, "got");
	REQUIRE(got != nullptr);

	const GDScriptParser::DataType type = got->get_datatype();
	CHECK(type.kind == GDScriptParser::DataType::BUILTIN);
	CHECK(type.builtin_type == Variant::INT);
}

TEST_CASE("[Modules][GDScript] Analyzer rejects empty type-argument brackets in an extends clause") {
	GDScriptParser parser;
	const String source =
			"class List[T]:\n"
			"\tvar head: T\n"
			"class Bad extends List[]:\n"
			"\tpass\n";
	// Empty brackets are a parse error, not a silent unspecialized inheritance.
	CHECK(parser.parse(source, "user://test.gd", false) != OK);
}

TEST_CASE("[Modules][GDScript] Analyzer rejects a wrong arity in a generic extends clause") {
	GDScriptParser parser;
	const String source =
			"class List[T]:\n"
			"\tvar head: T\n"
			"class Bad extends List[int, String]:\n"
			"\tpass\n";
	const Error parse_error = parser.parse(source, "user://test.gd", false);
	REQUIRE(parse_error == OK);

	GDScriptAnalyzer analyzer(&parser);
	CHECK(analyzer.analyze() != OK);

	bool found_arity_error = false;
	for (const GDScriptParser::ParserError &parser_error : parser.get_errors()) {
		if (parser_error.message.contains("type argument")) {
			found_arity_error = true;
			break;
		}
	}
	CHECK(found_arity_error);
}

TEST_CASE("[Modules][GDScript] Analyzer rejects type arguments on a non-generic extends clause") {
	GDScriptParser parser;
	const String source =
			"class Plain:\n"
			"\tpass\n"
			"class Bad extends Plain[int]:\n"
			"\tpass\n";
	const Error parse_error = parser.parse(source, "user://test.gd", false);
	REQUIRE(parse_error == OK);

	GDScriptAnalyzer analyzer(&parser);
	CHECK(analyzer.analyze() != OK);
}

} // namespace GDScriptTests
