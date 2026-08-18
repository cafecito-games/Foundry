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

TEST_CASE("[Modules][FoundryScript][Traits] An argument-less use is skipped in favor of a later binding use") {
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

// How `p_implementer` binds a one-parameter trait after the shared `Self` rule has been applied to it,
// as a printable string. This is the exact composition the analyzer's record, the compiler's runtime
// record, and the static store relation each perform, so a divergence here is a divergence there.
static String trait_arguments_reified_binding(const FSParser::ClassNode *p_implementer,
		const FSParser::ClassNode *p_trait, const StringName &p_parameter) {
	const HashMap<StringName, FSParser::DataType> bindings = fs_trait_type_argument_bindings(p_implementer, p_trait);
	const FSParser::DataType *bound = bindings.getptr(p_parameter);
	if (bound == nullptr) {
		return "<unbound>";
	}
	return fs_reify_self_in_trait_argument(p_implementer, *bound).to_string();
}

TEST_CASE("[Modules][FoundryScript][Traits] Self in a direct trait argument reifies for a final non-generic implementer") {
	FSParser parser;
	const String source = R"(
trait Keeper[T]:
	var value: T

final class Closed:
	uses Keeper[Self]

class OpenSelf:
	uses Keeper[Self]

final class FinalBox[V]:
	uses Keeper[Self]
)";
	REQUIRE(parser.parse(source, "res://trait_arguments_self_direct.fs", false) == OK);
	FSAnalyzer analyzer(&parser);
	REQUIRE(analyzer.analyze() == OK);

	const FSParser::ClassNode *keeper = trait_arguments_find_class(parser.get_tree(), "Keeper");
	const FSParser::ClassNode *closed = trait_arguments_find_class(parser.get_tree(), "Closed");
	const FSParser::ClassNode *open_self = trait_arguments_find_class(parser.get_tree(), "OpenSelf");
	const FSParser::ClassNode *final_box = trait_arguments_find_class(parser.get_tree(), "FinalBox");
	REQUIRE(keeper != nullptr);
	REQUIRE(closed != nullptr);
	REQUIRE(open_self != nullptr);
	REQUIRE(final_box != nullptr);

	CHECK(fs_trait_implementer_reifies_self(closed));
	CHECK_FALSE(fs_trait_implementer_reifies_self(open_self));
	CHECK_FALSE(fs_trait_implementer_reifies_self(final_box));
	CHECK_FALSE(fs_trait_implementer_reifies_self(keeper));
	CHECK_FALSE(fs_trait_implementer_reifies_self(nullptr));

	CHECK(trait_arguments_reified_binding(closed, keeper, "T") == "Closed");
	// A subclass receiver contradicts any concrete reading, so a non-final implementer stays open, and
	// a `final` generic one has one receiver identity per specialization.
	CHECK(trait_arguments_reified_binding(open_self, keeper, "T") == "Self");
	CHECK(trait_arguments_reified_binding(final_box, keeper, "T") == "Self");
}

TEST_CASE("[Modules][FoundryScript][Traits] Self reifies at every nesting depth of a trait argument") {
	FSParser parser;
	const String source = R"(
trait Keeper[T]:
	var value: T

class Pair[A, B]:
	pass

final class NestedClosed:
	uses Keeper[Pair[int, Self]]

final class ArrayClosed:
	uses Keeper[Array[Self]]

final class DictionaryClosed:
	uses Keeper[Dictionary[String, Self]]

final class TupleClosed:
	uses Keeper[(int, Self)]

final class NullableClosed:
	uses Keeper[Self?]

final class HandleClosed:
	uses Keeper[Type[Self]]

class NestedOpen:
	uses Keeper[Pair[int, Self]]
)";
	REQUIRE(parser.parse(source, "res://trait_arguments_self_nested.fs", false) == OK);
	FSAnalyzer analyzer(&parser);
	REQUIRE(analyzer.analyze() == OK);

	const FSParser::ClassNode *keeper = trait_arguments_find_class(parser.get_tree(), "Keeper");
	REQUIRE(keeper != nullptr);

	struct Expectation {
		const char *class_name;
		const char *binding;
	};
	const Expectation expectations[] = {
		{ "NestedClosed", "Pair[int, NestedClosed]" },
		{ "ArrayClosed", "Array[ArrayClosed]" },
		{ "DictionaryClosed", "Dictionary[String, DictionaryClosed]" },
		{ "TupleClosed", "(int, TupleClosed)" },
		{ "NullableClosed", "NullableClosed?" },
		{ "HandleClosed", "HandleClosed" },
	};
	for (const Expectation &expectation : expectations) {
		const FSParser::ClassNode *implementer = trait_arguments_find_class(parser.get_tree(), expectation.class_name);
		REQUIRE(implementer != nullptr);
		CHECK(trait_arguments_reified_binding(implementer, keeper, "T") == String(expectation.binding));
	}

	const FSParser::ClassNode *nested_open = trait_arguments_find_class(parser.get_tree(), "NestedOpen");
	REQUIRE(nested_open != nullptr);
	// The whole composite survives with its `Self` component intact: the concrete `int` sibling is not
	// erased, and the open component is not filled in.
	CHECK(trait_arguments_reified_binding(nested_open, keeper, "T") == "Pair[int, Self]");
	const HashMap<StringName, FSParser::DataType> open_bindings = fs_trait_type_argument_bindings(nested_open, keeper);
	const FSParser::DataType *open_bound = open_bindings.getptr("T");
	REQUIRE(open_bound != nullptr);
	CHECK(fs_trait_argument_references_self(*open_bound));
}

TEST_CASE("[Modules][FoundryScript][Traits] An implied supertrait projects the same final implementer") {
	FSParser parser;
	const String source = R"(
trait Keeper[T]:
	var value: T

trait Storing[T]:
	uses Keeper[T]

class Pair[A, B]:
	pass

final class SupertraitClosed:
	uses Storing[Pair[int, Self]]

class SupertraitOpen:
	uses Storing[Pair[int, Self]]
)";
	REQUIRE(parser.parse(source, "res://trait_arguments_self_supertrait.fs", false) == OK);
	FSAnalyzer analyzer(&parser);
	REQUIRE(analyzer.analyze() == OK);

	const FSParser::ClassNode *keeper = trait_arguments_find_class(parser.get_tree(), "Keeper");
	const FSParser::ClassNode *storing = trait_arguments_find_class(parser.get_tree(), "Storing");
	const FSParser::ClassNode *closed = trait_arguments_find_class(parser.get_tree(), "SupertraitClosed");
	const FSParser::ClassNode *open = trait_arguments_find_class(parser.get_tree(), "SupertraitOpen");
	REQUIRE(keeper != nullptr);
	REQUIRE(storing != nullptr);
	REQUIRE(closed != nullptr);
	REQUIRE(open != nullptr);

	// The direct trait and the identity reached through it answer with the same reified implementer.
	CHECK(trait_arguments_reified_binding(closed, storing, "T") == "Pair[int, SupertraitClosed]");
	CHECK(trait_arguments_reified_binding(closed, keeper, "T") == "Pair[int, SupertraitClosed]");
	CHECK(trait_arguments_reified_binding(open, storing, "T") == "Pair[int, Self]");
	CHECK(trait_arguments_reified_binding(open, keeper, "T") == "Pair[int, Self]");
}

TEST_CASE("[Modules][FoundryScript][Traits] A subclass of a non-final implementer leaves the base declaration open") {
	FSParser parser;
	const String source = R"(
trait Keeper[T]:
	var value: T

class OpenSelf:
	uses Keeper[Self]

final class ClosedChild extends OpenSelf:
	pass
)";
	REQUIRE(parser.parse(source, "res://trait_arguments_self_subclass.fs", false) == OK);
	FSAnalyzer analyzer(&parser);
	REQUIRE(analyzer.analyze() == OK);

	const FSParser::ClassNode *keeper = trait_arguments_find_class(parser.get_tree(), "Keeper");
	const FSParser::ClassNode *open_self = trait_arguments_find_class(parser.get_tree(), "OpenSelf");
	const FSParser::ClassNode *child = trait_arguments_find_class(parser.get_tree(), "ClosedChild");
	REQUIRE(keeper != nullptr);
	REQUIRE(open_self != nullptr);
	REQUIRE(child != nullptr);

	// The declaring level is what is judged. A `final` subclass does not make the base's recorded
	// `Self` concrete, and the base's own record is unchanged by the subclass existing.
	CHECK(trait_arguments_reified_binding(open_self, keeper, "T") == "Self");
	CHECK(fs_trait_type_argument_bindings(child, keeper).is_empty());
}

} // namespace FSTests
