/**************************************************************************/
/*  test_trait_type_arguments.h                                           */
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

#include "modules/foundry_script/fs_analyzer.h"
#include "modules/foundry_script/fs_parser.h"
#include "modules/foundry_script/fs_trait_utils.h"

#include "tests/test_macros.h"

namespace FSTests {

// These tests drive the single trait-argument substitution walker that the analyzer, the compiler,
// the static type relation, and the editor refactoring surface all share. Its answers decide which
// type a conformance binds, so a regression here is an analyzer/compiler/runtime split rather than a
// local inconsistency.

static const FSParser::ClassNode *trait_arguments_find_class(const FSParser::ClassNode *p_root, const StringName &p_name) {
	if (p_root == nullptr || !p_root->has_member(p_name)) {
		return nullptr;
	}
	const FSParser::ClassNode::Member member = p_root->get_member(p_name);
	if (member.type != FSParser::ClassNode::Member::CLASS) {
		return nullptr;
	}
	return member.m_class;
}

// The single binding a one-parameter trait received, as a printable string, or "<unbound>" when the
// walker found no evidence for it.
static String trait_arguments_binding(const HashMap<StringName, FSParser::DataType> &p_bindings, const StringName &p_parameter) {
	const FSParser::DataType *bound = p_bindings.getptr(p_parameter);
	if (bound == nullptr) {
		return "<unbound>";
	}
	return bound->to_string();
}

TEST_CASE("[Modules][FoundryScript][Traits] Direct generic trait use binds its own parameters") {
	FSParser parser;
	const String source = R"(
trait Keeper[T]:
	var value: T

class C:
	uses Keeper[String]
)";
	REQUIRE(parser.parse(source, "res://trait_arguments_direct.fs", false) == OK);
	FSAnalyzer analyzer(&parser);
	REQUIRE(analyzer.analyze() == OK);

	const FSParser::ClassNode *keeper = trait_arguments_find_class(parser.get_tree(), "Keeper");
	const FSParser::ClassNode *implementer = trait_arguments_find_class(parser.get_tree(), "C");
	REQUIRE(keeper != nullptr);
	REQUIRE(implementer != nullptr);

	const HashMap<StringName, FSParser::DataType> bindings = fs_trait_type_argument_bindings(implementer, keeper);
	CHECK(bindings.size() == 1);
	CHECK(trait_arguments_binding(bindings, "T") == "String");
}

TEST_CASE("[Modules][FoundryScript][Traits] Transitive generic supertrait composes through the intermediate") {
	FSParser parser;
	const String source = R"(
trait Storage[T]:
	var value: T

trait Wrapper:
	uses Storage[int]

class C:
	uses Wrapper
)";
	REQUIRE(parser.parse(source, "res://trait_arguments_transitive.fs", false) == OK);
	FSAnalyzer analyzer(&parser);
	REQUIRE(analyzer.analyze() == OK);

	const FSParser::ClassNode *storage = trait_arguments_find_class(parser.get_tree(), "Storage");
	const FSParser::ClassNode *implementer = trait_arguments_find_class(parser.get_tree(), "C");
	REQUIRE(storage != nullptr);
	REQUIRE(implementer != nullptr);

	const HashMap<StringName, FSParser::DataType> bindings = fs_trait_type_argument_bindings(implementer, storage);
	CHECK(bindings.size() == 1);
	CHECK(trait_arguments_binding(bindings, "T") == "int");
}

TEST_CASE("[Modules][FoundryScript][Traits] Forwarded type parameter is re-specialized through the outer binding") {
	FSParser parser;
	const String source = R"(
trait Storage[T]:
	var value: T

trait Wrapper[U]:
	uses Storage[U]

class C:
	uses Wrapper[float]
)";
	REQUIRE(parser.parse(source, "res://trait_arguments_forwarded.fs", false) == OK);
	FSAnalyzer analyzer(&parser);
	REQUIRE(analyzer.analyze() == OK);

	const FSParser::ClassNode *storage = trait_arguments_find_class(parser.get_tree(), "Storage");
	const FSParser::ClassNode *implementer = trait_arguments_find_class(parser.get_tree(), "C");
	REQUIRE(storage != nullptr);
	REQUIRE(implementer != nullptr);

	// The intermediate binds `Storage[U]`; only composing with `C`'s `Wrapper[float]` turns that into
	// a concrete answer rather than leaving the walker reporting `T := U`.
	const HashMap<StringName, FSParser::DataType> bindings = fs_trait_type_argument_bindings(implementer, storage);
	CHECK(bindings.size() == 1);
	CHECK(trait_arguments_binding(bindings, "T") == "float");
}

TEST_CASE("[Modules][FoundryScript][Traits] An argument-less use is skipped in favour of a later binding use") {
	FSParser parser;
	const String source = R"(
trait Storage[T]:
	var value: T

trait Wrapper:
	uses Storage[int]

class C:
	uses Storage, Wrapper
)";
	REQUIRE(parser.parse(source, "res://trait_arguments_bare_first.fs", false) == OK);
	FSAnalyzer analyzer(&parser);
	REQUIRE(analyzer.analyze() == OK);

	const FSParser::ClassNode *storage = trait_arguments_find_class(parser.get_tree(), "Storage");
	const FSParser::ClassNode *implementer = trait_arguments_find_class(parser.get_tree(), "C");
	REQUIRE(storage != nullptr);
	REQUIRE(implementer != nullptr);

	// `uses Storage` supplies no arguments, so it proves nothing about `T` and must not stop the
	// search. Returning empty here is what let the compiler bake an untyped slot for a parameter the
	// analyzer had already typed as `int`.
	const HashMap<StringName, FSParser::DataType> bindings = fs_trait_type_argument_bindings(implementer, storage);
	CHECK(bindings.size() == 1);
	CHECK(trait_arguments_binding(bindings, "T") == "int");
}

TEST_CASE("[Modules][FoundryScript][Traits] Binding is independent of the order of the uses list") {
	const String bare_first = R"(
trait Storage[T]:
	var value: T

trait Wrapper:
	uses Storage[int]

class C:
	uses Storage, Wrapper
)";
	const String bare_last = R"(
trait Storage[T]:
	var value: T

trait Wrapper:
	uses Storage[int]

class C:
	uses Wrapper, Storage
)";

	FSParser first_parser;
	REQUIRE(first_parser.parse(bare_first, "res://trait_arguments_order_a.fs", false) == OK);
	FSAnalyzer first_analyzer(&first_parser);
	REQUIRE(first_analyzer.analyze() == OK);

	FSParser second_parser;
	REQUIRE(second_parser.parse(bare_last, "res://trait_arguments_order_b.fs", false) == OK);
	FSAnalyzer second_analyzer(&second_parser);
	REQUIRE(second_analyzer.analyze() == OK);

	const FSParser::ClassNode *first_storage = trait_arguments_find_class(first_parser.get_tree(), "Storage");
	const FSParser::ClassNode *first_class = trait_arguments_find_class(first_parser.get_tree(), "C");
	const FSParser::ClassNode *second_storage = trait_arguments_find_class(second_parser.get_tree(), "Storage");
	const FSParser::ClassNode *second_class = trait_arguments_find_class(second_parser.get_tree(), "C");
	REQUIRE(first_storage != nullptr);
	REQUIRE(first_class != nullptr);
	REQUIRE(second_storage != nullptr);
	REQUIRE(second_class != nullptr);

	const HashMap<StringName, FSParser::DataType> first = fs_trait_type_argument_bindings(first_class, first_storage);
	const HashMap<StringName, FSParser::DataType> second = fs_trait_type_argument_bindings(second_class, second_storage);

	CHECK(first.size() == second.size());
	CHECK(trait_arguments_binding(first, "T") == trait_arguments_binding(second, "T"));
	CHECK(trait_arguments_binding(first, "T") == "int");
}

TEST_CASE("[Modules][FoundryScript][Traits] Fewer supplied arguments than parameters leave the tail unbound") {
	FSParser parser;
	const String source = R"(
trait Pair[K, V]:
	var key: K
	var value: V

class C:
	uses Pair[int]
)";
	// The use site is under-applied, so analysis is expected to complain; the walker still has to
	// answer without reading past the arguments it was given.
	parser.parse(source, "res://trait_arguments_arity.fs", false);
	FSAnalyzer analyzer(&parser);
	analyzer.analyze();

	const FSParser::ClassNode *pair = trait_arguments_find_class(parser.get_tree(), "Pair");
	const FSParser::ClassNode *implementer = trait_arguments_find_class(parser.get_tree(), "C");
	REQUIRE(pair != nullptr);
	REQUIRE(implementer != nullptr);

	const HashMap<StringName, FSParser::DataType> bindings = fs_trait_type_argument_bindings(implementer, pair);
	CHECK(bindings.size() <= 1);
	CHECK(trait_arguments_binding(bindings, "V") == "<unbound>");
}

TEST_CASE("[Modules][FoundryScript][Traits] An unapplied trait yields no bindings") {
	FSParser parser;
	const String source = R"(
trait Storage[T]:
	var value: T

trait Other[T]:
	var other: T

class C:
	uses Storage[int]
)";
	REQUIRE(parser.parse(source, "res://trait_arguments_unapplied.fs", false) == OK);
	FSAnalyzer analyzer(&parser);
	REQUIRE(analyzer.analyze() == OK);

	const FSParser::ClassNode *other = trait_arguments_find_class(parser.get_tree(), "Other");
	const FSParser::ClassNode *implementer = trait_arguments_find_class(parser.get_tree(), "C");
	REQUIRE(other != nullptr);
	REQUIRE(implementer != nullptr);

	CHECK(fs_trait_type_argument_bindings(implementer, other).is_empty());
}

TEST_CASE("[Modules][FoundryScript][Traits] Null class or null trait yields no bindings") {
	FSParser parser;
	const String source = R"(
trait Marker:
	var flag: bool

class C:
	uses Marker
)";
	REQUIRE(parser.parse(source, "res://trait_arguments_guards.fs", false) == OK);
	FSAnalyzer analyzer(&parser);
	REQUIRE(analyzer.analyze() == OK);

	const FSParser::ClassNode *marker = trait_arguments_find_class(parser.get_tree(), "Marker");
	const FSParser::ClassNode *implementer = trait_arguments_find_class(parser.get_tree(), "C");
	REQUIRE(marker != nullptr);
	REQUIRE(implementer != nullptr);

	CHECK(fs_trait_type_argument_bindings(nullptr, marker).is_empty());
	CHECK(fs_trait_type_argument_bindings(implementer, nullptr).is_empty());
	CHECK(fs_trait_type_argument_bindings(nullptr, nullptr).is_empty());
	// A trait with no parameters has nothing to bind even when it is genuinely applied.
	CHECK(fs_trait_type_argument_bindings(implementer, marker).is_empty());
}

TEST_CASE("[Modules][FoundryScript][Traits] Trait nodes with equal fully qualified names are the same declaration") {
	const String source = R"(
trait Storage[T]:
	var value: T

class C:
	uses Storage[String]
)";
	// Two independent parses of one script yield distinct `ClassNode`s for the same declaration, which
	// is the shape a caller sees when the trait is reached through a depended parser. Pointer equality
	// alone would report no binding.
	FSParser owning_parser;
	REQUIRE(owning_parser.parse(source, "res://trait_arguments_identity.fs", false) == OK);
	FSAnalyzer owning_analyzer(&owning_parser);
	REQUIRE(owning_analyzer.analyze() == OK);

	FSParser mirror_parser;
	REQUIRE(mirror_parser.parse(source, "res://trait_arguments_identity.fs", false) == OK);
	FSAnalyzer mirror_analyzer(&mirror_parser);
	REQUIRE(mirror_analyzer.analyze() == OK);

	const FSParser::ClassNode *implementer = trait_arguments_find_class(owning_parser.get_tree(), "C");
	const FSParser::ClassNode *mirror_storage = trait_arguments_find_class(mirror_parser.get_tree(), "Storage");
	REQUIRE(implementer != nullptr);
	REQUIRE(mirror_storage != nullptr);
	REQUIRE(implementer->used_traits.size() == 1);
	REQUIRE(implementer->used_traits[0].resolved_trait != mirror_storage);
	REQUIRE(!mirror_storage->fqcn.is_empty());
	REQUIRE(implementer->used_traits[0].resolved_trait->fqcn == mirror_storage->fqcn);

	const HashMap<StringName, FSParser::DataType> bindings = fs_trait_type_argument_bindings(implementer, mirror_storage);
	CHECK(bindings.size() == 1);
	CHECK(trait_arguments_binding(bindings, "T") == "String");
}

TEST_CASE("[Modules][FoundryScript][Traits] A trait use bindings helper zips parameters against resolved arguments") {
	FSParser parser;
	const String source = R"(
trait Pair[K, V]:
	var key: K
	var value: V

trait Marker:
	var flag: bool

class C:
	uses Pair[int, String], Marker
)";
	REQUIRE(parser.parse(source, "res://trait_arguments_zip.fs", false) == OK);
	FSAnalyzer analyzer(&parser);
	REQUIRE(analyzer.analyze() == OK);

	const FSParser::ClassNode *pair = trait_arguments_find_class(parser.get_tree(), "Pair");
	const FSParser::ClassNode *marker = trait_arguments_find_class(parser.get_tree(), "Marker");
	const FSParser::ClassNode *implementer = trait_arguments_find_class(parser.get_tree(), "C");
	REQUIRE(pair != nullptr);
	REQUIRE(marker != nullptr);
	REQUIRE(implementer != nullptr);
	REQUIRE(implementer->used_traits.size() == 2);

	const FSParser::ClassNode::TraitUse &pair_use = implementer->used_traits[0];
	const FSParser::ClassNode::TraitUse &marker_use = implementer->used_traits[1];

	const HashMap<StringName, FSParser::DataType> exact = fs_trait_use_type_argument_bindings(pair, pair_use);
	CHECK(exact.size() == 2);
	CHECK(trait_arguments_binding(exact, "K") == "int");
	CHECK(trait_arguments_binding(exact, "V") == "String");

	// A trait with no parameters, and a null trait, both bind nothing.
	CHECK(fs_trait_use_type_argument_bindings(marker, marker_use).is_empty());
	CHECK(fs_trait_use_type_argument_bindings(nullptr, pair_use).is_empty());

	// An argument-less entry reports "nothing proven" rather than a partial answer.
	CHECK(fs_trait_use_type_argument_bindings(pair, marker_use).is_empty());
}

} // namespace FSTests
