/**************************************************************************/
/*  test_type_union.h                                                     */
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

#include "modules/foundry_script/fs_parser.h"
#include "modules/foundry_script/fs_type.h"

#include "core/variant/numeric_type.h"

#include "tests/test_macros.h"

// Coverage for transparent type aliases and `A | B` type unions at the front end: the alias
// declaration and the union type node the parser builds, and the canonical set semantics the
// `UNION` DataType kind carries. The script corpus covers the diagnostics; these cases assert on
// the parsed structure and on the type model, neither of which a fixture can observe.

namespace FSTests {

static const FSParser::TypeAliasNode *find_type_alias(const FSParser::ClassNode *p_class, const StringName &p_name) {
	if (p_class == nullptr || !p_class->has_member(p_name)) {
		return nullptr;
	}
	const FSParser::ClassNode::Member member = p_class->get_member(p_name);
	if (member.type != FSParser::ClassNode::Member::TYPE_ALIAS) {
		return nullptr;
	}
	return member.type_alias;
}

static StringName union_member_name(const FSParser::TypeNode *p_union, int p_index) {
	if (p_union == nullptr || p_index < 0 || p_index >= p_union->union_member_types.size()) {
		return StringName();
	}
	const FSParser::TypeNode *member = p_union->union_member_types[p_index];
	if (member == nullptr || member->type_chain.is_empty()) {
		return StringName();
	}
	return member->type_chain[0]->name;
}

static FSParser::DataType union_test_builtin(Variant::Type p_carrier, NumericType p_numeric_type = NumericType::NONE) {
	FSParser::DataType type;
	type.kind = FSParser::DataType::BUILTIN;
	type.type_source = FSParser::DataType::ANNOTATED_EXPLICIT;
	type.builtin_type = p_carrier;
	type.numeric_type = p_numeric_type;
	return type;
}

static FSParser::DataType union_test_type_parameter(const StringName &p_name) {
	FSParser::DataType type;
	type.kind = FSParser::DataType::TYPE_PARAMETER;
	type.type_source = FSParser::DataType::ANNOTATED_EXPLICIT;
	type.type_parameter_name = p_name;
	return type;
}

TEST_CASE("[Modules][FoundryScript][TypeUnion] Alias declarations parse at file and class scope") {
	FSParser parser;
	const Error error = parser.parse(
			"type Unsigned = uint | ulong\n"
			"type Meters = float\n"
			"class Inner:\n"
			"\ttype Local = int | float\n",
			"user://test.fs", false);
	REQUIRE(error == OK);

	const FSParser::ClassNode *root_class = parser.get_tree();
	REQUIRE(root_class != nullptr);

	const FSParser::TypeAliasNode *unsigned_alias = find_type_alias(root_class, "Unsigned");
	REQUIRE(unsigned_alias != nullptr);
	REQUIRE(unsigned_alias->aliased_type != nullptr);
	CHECK(unsigned_alias->aliased_type->is_union);
	REQUIRE(unsigned_alias->aliased_type->union_member_types.size() == 2);
	CHECK(union_member_name(unsigned_alias->aliased_type, 0) == StringName("uint"));
	CHECK(union_member_name(unsigned_alias->aliased_type, 1) == StringName("ulong"));

	// A lone alternative is not wrapped: it stays the type node the author wrote.
	const FSParser::TypeAliasNode *meters_alias = find_type_alias(root_class, "Meters");
	REQUIRE(meters_alias != nullptr);
	REQUIRE(meters_alias->aliased_type != nullptr);
	CHECK_FALSE(meters_alias->aliased_type->is_union);
	REQUIRE(meters_alias->aliased_type->type_chain.size() == 1);
	CHECK(meters_alias->aliased_type->type_chain[0]->name == StringName("float"));

	REQUIRE(root_class->has_member("Inner"));
	const FSParser::ClassNode::Member inner_member = root_class->get_member("Inner");
	REQUIRE(inner_member.type == FSParser::ClassNode::Member::CLASS);
	const FSParser::TypeAliasNode *local_alias = find_type_alias(inner_member.m_class, "Local");
	REQUIRE(local_alias != nullptr);
	REQUIRE(local_alias->aliased_type != nullptr);
	CHECK(local_alias->aliased_type->is_union);
	CHECK(local_alias->aliased_type->union_member_types.size() == 2);
}

TEST_CASE("[Modules][FoundryScript][TypeUnion] `type` remains an ordinary identifier") {
	FSParser parser;
	const Error error = parser.parse(
			"func test():\n"
			"\tvar type = 5\n"
			"\ttype = type | 2\n"
			"\tprint(type)\n",
			"user://test.fs", false);
	REQUIRE(error == OK);
	CHECK(parser.get_errors().is_empty());
}

TEST_CASE("[Modules][FoundryScript][TypeUnion] `|` binds looser than `?` inside a type") {
	FSParser parser;
	const Error error = parser.parse("type MaybeCount = int? | uint\n", "user://test.fs", false);
	REQUIRE(error == OK);

	const FSParser::TypeAliasNode *alias = find_type_alias(parser.get_tree(), "MaybeCount");
	REQUIRE(alias != nullptr);
	REQUIRE(alias->aliased_type != nullptr);
	// The `?` belongs to the member it follows, not to the union: there is no parenthesized type
	// form, so a nullable union can only be written by marking a member.
	CHECK_FALSE(alias->aliased_type->is_nullable);
	REQUIRE(alias->aliased_type->union_member_types.size() == 2);
	CHECK(alias->aliased_type->union_member_types[0]->is_nullable);
	CHECK_FALSE(alias->aliased_type->union_member_types[1]->is_nullable);
}

TEST_CASE("[Modules][FoundryScript][TypeUnion] Unions parse in every type position") {
	FSParser parser;
	const Error error = parser.parse(
			"func combine(left: int | float, values: Array[int | uint]) -> int | float:\n"
			"\tvar local: String | StringName = \"x\"\n"
			"\treturn left\n"
			"func bounded[T: int | uint](value: T) -> T:\n"
			"\treturn value\n",
			"user://test.fs", false);
	REQUIRE(error == OK);
	CHECK(parser.get_errors().is_empty());

	const FSParser::ClassNode *root_class = parser.get_tree();
	REQUIRE(root_class != nullptr);
	REQUIRE(root_class->has_function("combine"));
	const FSParser::FunctionNode *combine = root_class->get_member("combine").function;
	REQUIRE(combine->parameters.size() == 2);
	REQUIRE(combine->parameters[0]->datatype_specifier != nullptr);
	CHECK(combine->parameters[0]->datatype_specifier->is_union);
	REQUIRE(combine->parameters[1]->datatype_specifier != nullptr);
	REQUIRE(combine->parameters[1]->datatype_specifier->container_types.size() == 1);
	CHECK(combine->parameters[1]->datatype_specifier->container_types[0]->is_union);
	REQUIRE(combine->return_type != nullptr);
	CHECK(combine->return_type->is_union);

	REQUIRE(root_class->has_function("bounded"));
	const FSParser::FunctionNode *bounded = root_class->get_member("bounded").function;
	REQUIRE(bounded->type_parameters.size() == 1);
	REQUIRE(bounded->type_parameters[0]->bound != nullptr);
	CHECK(bounded->type_parameters[0]->bound->is_union);
}

TEST_CASE("[Modules][FoundryScript][TypeUnion] Expression-level `|` stays bitwise OR") {
	FSParser parser;
	const Error error = parser.parse(
			"func test():\n"
			"\tvar flags = 1 | 2\n"
			"\tprint(flags)\n",
			"user://test.fs", false);
	REQUIRE(error == OK);

	const FSParser::ClassNode *root_class = parser.get_tree();
	REQUIRE(root_class != nullptr);
	REQUIRE(root_class->has_function("test"));
	const FSParser::FunctionNode *test_function = root_class->get_member("test").function;
	REQUIRE(test_function->body != nullptr);
	REQUIRE(test_function->body->statements.size() >= 1);
	const FSParser::Node *first_statement = test_function->body->statements[0];
	REQUIRE(first_statement->type == FSParser::Node::VARIABLE);
	const FSParser::VariableNode *flags = static_cast<const FSParser::VariableNode *>(first_statement);
	REQUIRE(flags->initializer != nullptr);
	REQUIRE(flags->initializer->type == FSParser::Node::BINARY_OPERATOR);
	const FSParser::BinaryOpNode *binary_operator = static_cast<const FSParser::BinaryOpNode *>(flags->initializer);
	CHECK(binary_operator->operation == FSParser::BinaryOpNode::OP_BIT_OR);
}

TEST_CASE("[Modules][FoundryScript][TypeUnion] The right-hand side of `is` is a type position") {
	FSParser parser;
	// `is` takes a type, so `|` there unions the two types rather than applying bitwise OR to the
	// test result. This is the one place the contextual operator changes an existing spelling.
	const Error error = parser.parse(
			"func test():\n"
			"\tvar value = 1\n"
			"\tprint(value is int | uint)\n",
			"user://test.fs", false);
	REQUIRE(error == OK);

	const FSParser::ClassNode *root_class = parser.get_tree();
	REQUIRE(root_class != nullptr);
	REQUIRE(root_class->has_function("test"));
	const FSParser::FunctionNode *test_function = root_class->get_member("test").function;
	REQUIRE(test_function->body != nullptr);
	REQUIRE(test_function->body->statements.size() >= 2);
	const FSParser::Node *print_statement = test_function->body->statements[1];
	REQUIRE(print_statement->type == FSParser::Node::CALL);
	const FSParser::CallNode *print_call = static_cast<const FSParser::CallNode *>(print_statement);
	REQUIRE(print_call->arguments.size() == 1);
	REQUIRE(print_call->arguments[0]->type == FSParser::Node::TYPE_TEST);
	const FSParser::TypeTestNode *type_test = static_cast<const FSParser::TypeTestNode *>(print_call->arguments[0]);
	REQUIRE(type_test->test_type != nullptr);
	CHECK(type_test->test_type->is_union);
	CHECK(type_test->test_type->union_member_types.size() == 2);
}

TEST_CASE("[Modules][FoundryScript][TypeUnion] A single member collapses to that member") {
	Vector<FSParser::DataType> members;
	members.push_back(union_test_builtin(Variant::INT, NumericType::INT32));

	const FSParser::DataType collapsed = FSParser::DataType::make_union(members);
	CHECK(collapsed.kind == FSParser::DataType::BUILTIN);
	CHECK(collapsed.builtin_type == Variant::INT);
	// A one-member alias keeps the runtime typing of what it aliases, width included.
	CHECK(collapsed.numeric_type == NumericType::INT32);
	CHECK_FALSE(collapsed.is_union());
}

TEST_CASE("[Modules][FoundryScript][TypeUnion] Duplicate members are removed") {
	Vector<FSParser::DataType> members;
	members.push_back(union_test_builtin(Variant::INT));
	members.push_back(union_test_builtin(Variant::FLOAT));
	members.push_back(union_test_builtin(Variant::INT));

	const FSParser::DataType result = FSParser::DataType::make_union(members);
	REQUIRE(result.is_union());
	CHECK(result.union_members.size() == 2);
}

TEST_CASE("[Modules][FoundryScript][TypeUnion] Nested unions are flattened") {
	Vector<FSParser::DataType> inner_members;
	inner_members.push_back(union_test_builtin(Variant::INT));
	inner_members.push_back(union_test_builtin(Variant::FLOAT));

	Vector<FSParser::DataType> outer_members;
	outer_members.push_back(FSParser::DataType::make_union(inner_members));
	outer_members.push_back(union_test_builtin(Variant::STRING));

	const FSParser::DataType result = FSParser::DataType::make_union(outer_members);
	REQUIRE(result.is_union());
	CHECK(result.union_members.size() == 3);
	for (const FSParser::DataType &member : result.union_members) {
		CHECK_FALSE(member.is_union());
	}
}

TEST_CASE("[Modules][FoundryScript][TypeUnion] Nullability is hoisted onto the union") {
	FSParser::DataType nullable_int = union_test_builtin(Variant::INT);
	nullable_int.is_nullable = true;

	Vector<FSParser::DataType> left_members;
	left_members.push_back(nullable_int);
	left_members.push_back(union_test_builtin(Variant::FLOAT));

	FSParser::DataType nullable_float = union_test_builtin(Variant::FLOAT);
	nullable_float.is_nullable = true;
	Vector<FSParser::DataType> right_members;
	right_members.push_back(union_test_builtin(Variant::INT));
	right_members.push_back(nullable_float);

	const FSParser::DataType left = FSParser::DataType::make_union(left_members);
	const FSParser::DataType right = FSParser::DataType::make_union(right_members);

	REQUIRE(left.is_union());
	CHECK(left.is_nullable);
	for (const FSParser::DataType &member : left.union_members) {
		CHECK_FALSE(member.is_nullable);
	}
	// `int? | float` and `int | float?` denote the same nullable set.
	CHECK(left == right);
}

TEST_CASE("[Modules][FoundryScript][TypeUnion] Member order does not affect identity") {
	Vector<FSParser::DataType> written_order;
	written_order.push_back(union_test_builtin(Variant::STRING));
	written_order.push_back(union_test_builtin(Variant::INT));
	written_order.push_back(union_test_builtin(Variant::FLOAT));

	Vector<FSParser::DataType> other_order;
	other_order.push_back(union_test_builtin(Variant::FLOAT));
	other_order.push_back(union_test_builtin(Variant::STRING));
	other_order.push_back(union_test_builtin(Variant::INT));

	const FSParser::DataType left = FSParser::DataType::make_union(written_order);
	const FSParser::DataType right = FSParser::DataType::make_union(other_order);
	REQUIRE(left.is_union());
	CHECK(left == right);
	CHECK(left.to_string() == right.to_string());
}

TEST_CASE("[Modules][FoundryScript][TypeUnion] A union prints as valid source") {
	Vector<FSParser::DataType> members;
	members.push_back(union_test_builtin(Variant::INT));
	members.push_back(union_test_builtin(Variant::FLOAT));

	const FSParser::DataType plain = FSParser::DataType::make_union(members);
	CHECK(plain.to_string() == "float | int");

	FSParser::DataType nullable = plain;
	nullable.is_nullable = true;
	// The `?` hangs off the first member, since `(A | B)?` is not spellable and `A? | B`
	// normalizes back to this same type.
	CHECK(nullable.to_string() == "float? | int");
}

TEST_CASE("[Modules][FoundryScript][TypeUnion] Width-only members keep their diagnostic names") {
	Vector<FSParser::DataType> members;
	members.push_back(union_test_builtin(Variant::UINT, NumericType::UINT8));
	members.push_back(union_test_builtin(Variant::UINT, NumericType::UINT16));

	const FSParser::DataType result = FSParser::DataType::make_union(members);
	REQUIRE(result.is_union());
	CHECK(result.to_string_diagnostic() == "uint16 | uint8");
}

TEST_CASE("[Modules][FoundryScript][TypeUnion] A multi-member union reports untyped property info") {
	Vector<FSParser::DataType> members;
	members.push_back(union_test_builtin(Variant::INT));
	members.push_back(union_test_builtin(Variant::FLOAT));

	const PropertyInfo union_property = FSParser::DataType::make_union(members).to_property_info("value");
	CHECK(union_property.type == Variant::NIL);
	CHECK((union_property.usage & PROPERTY_USAGE_NIL_IS_VARIANT) != 0);

	// A collapsed single member is indistinguishable from the member, carrier included.
	Vector<FSParser::DataType> single_member;
	single_member.push_back(union_test_builtin(Variant::FLOAT));
	const PropertyInfo member_property = FSParser::DataType::make_union(single_member).to_property_info("value");
	CHECK(member_property.type == Variant::FLOAT);
	CHECK((member_property.usage & PROPERTY_USAGE_NIL_IS_VARIANT) == 0);
}

TEST_CASE("[Modules][FoundryScript][TypeUnion] Substitution rewrites and renormalizes members") {
	Vector<FSParser::DataType> members;
	members.push_back(union_test_type_parameter("T"));
	members.push_back(union_test_builtin(Variant::FLOAT));
	const FSParser::DataType parameterized = FSParser::DataType::make_union(members);
	REQUIRE(parameterized.is_union());

	HashMap<StringName, FSParser::DataType> to_string_binding;
	to_string_binding["T"] = union_test_builtin(Variant::STRING);
	const FSParser::DataType substituted = FSParser::DataType::substitute(parameterized, to_string_binding);
	REQUIRE(substituted.is_union());
	REQUIRE(substituted.union_members.size() == 2);

	Vector<FSParser::DataType> expected_members;
	expected_members.push_back(union_test_builtin(Variant::STRING));
	expected_members.push_back(union_test_builtin(Variant::FLOAT));
	CHECK(substituted == FSParser::DataType::make_union(expected_members));

	// Binding the parameter to a member already present collapses the set to that member.
	HashMap<StringName, FSParser::DataType> to_float_binding;
	to_float_binding["T"] = union_test_builtin(Variant::FLOAT);
	const FSParser::DataType collapsed = FSParser::DataType::substitute(parameterized, to_float_binding);
	CHECK_FALSE(collapsed.is_union());
	CHECK(collapsed.kind == FSParser::DataType::BUILTIN);
	CHECK(collapsed.builtin_type == Variant::FLOAT);
}

TEST_CASE("[Modules][FoundryScript][TypeUnion] A concrete source satisfies a union target through one member") {
	Vector<FSParser::DataType> members;
	members.push_back(union_test_builtin(Variant::INT, NumericType::INT32));
	members.push_back(union_test_builtin(Variant::STRING));
	const FSParser::DataType target = FSParser::DataType::make_union(members);
	REQUIRE(target.is_union());

	CHECK(FSTypeCompatibility::check(target, union_test_builtin(Variant::INT, NumericType::INT32)).compatible);
	CHECK(FSTypeCompatibility::check(target, union_test_builtin(Variant::STRING)).compatible);
	CHECK_FALSE(FSTypeCompatibility::check(target, union_test_builtin(Variant::FLOAT)).compatible);
}

TEST_CASE("[Modules][FoundryScript][TypeUnion] A union source satisfies a concrete target only when every member does") {
	Vector<FSParser::DataType> members;
	members.push_back(union_test_builtin(Variant::INT, NumericType::INT32));
	members.push_back(union_test_builtin(Variant::STRING));
	const FSParser::DataType source = FSParser::DataType::make_union(members);
	REQUIRE(source.is_union());

	// Neither concrete alternative accepts the other, so no concrete member type accepts the set.
	CHECK_FALSE(FSTypeCompatibility::check(union_test_builtin(Variant::INT, NumericType::INT32), source).compatible);
	CHECK_FALSE(FSTypeCompatibility::check(union_test_builtin(Variant::STRING), source).compatible);

	FSParser::DataType variant_target;
	variant_target.kind = FSParser::DataType::VARIANT;
	variant_target.type_source = FSParser::DataType::ANNOTATED_EXPLICIT;
	CHECK(FSTypeCompatibility::check(variant_target, source).compatible);
}

TEST_CASE("[Modules][FoundryScript][TypeUnion] A union satisfies a union that covers all its members") {
	Vector<FSParser::DataType> narrow_members;
	narrow_members.push_back(union_test_builtin(Variant::INT, NumericType::INT32));
	narrow_members.push_back(union_test_builtin(Variant::STRING));
	const FSParser::DataType narrow = FSParser::DataType::make_union(narrow_members);

	Vector<FSParser::DataType> wide_members = narrow_members;
	wide_members.push_back(union_test_builtin(Variant::FLOAT));
	const FSParser::DataType wide = FSParser::DataType::make_union(wide_members);

	REQUIRE(narrow.is_union());
	REQUIRE(wide.is_union());
	CHECK(FSTypeCompatibility::check(wide, narrow).compatible);
	// `float` has nothing to satisfy in the narrower set.
	CHECK_FALSE(FSTypeCompatibility::check(narrow, wide).compatible);
}

TEST_CASE("[Modules][FoundryScript][TypeUnion] A nullable union accepts null and its members' nullability") {
	Vector<FSParser::DataType> members;
	FSParser::DataType nullable_int = union_test_builtin(Variant::INT, NumericType::INT32);
	nullable_int.is_nullable = true;
	members.push_back(nullable_int);
	members.push_back(union_test_builtin(Variant::STRING));
	const FSParser::DataType nullable_union = FSParser::DataType::make_union(members);
	REQUIRE(nullable_union.is_union());
	REQUIRE(nullable_union.is_nullable);

	FSParser::DataType null_source = union_test_builtin(Variant::NIL);
	CHECK(FSTypeCompatibility::check(nullable_union, null_source).compatible);

	FSTypeCompatibility::Options strict;
	strict.strict_null = true;
	// Nullability is hoisted, so a nullable source is answered by the union's own nullability rather
	// than by whichever member it happens to match.
	CHECK(FSTypeCompatibility::check(nullable_union, nullable_int, strict).compatible);

	Vector<FSParser::DataType> plain_members;
	plain_members.push_back(union_test_builtin(Variant::INT, NumericType::INT32));
	plain_members.push_back(union_test_builtin(Variant::STRING));
	const FSParser::DataType plain_union = FSParser::DataType::make_union(plain_members);
	CHECK_FALSE(FSTypeCompatibility::check(plain_union, nullable_int, strict).compatible);
}

} // namespace FSTests
