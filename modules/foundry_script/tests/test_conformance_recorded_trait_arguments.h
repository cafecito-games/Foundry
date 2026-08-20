/**************************************************************************/
/*  test_conformance_recorded_trait_arguments.h                           */
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
#include "modules/foundry_script/fs_conformance_registry.h"
#include "modules/foundry_script/fs_function.h"
#include "modules/foundry_script/fs_parser.h"
#include "modules/foundry_script/fs_trait_utils.h"
#include "modules/foundry_script/fs_type.h"

#include "tests/test_macros.h"

// A retroactive conformance (`extend Target uses Trait[...]`) records its trait arguments on the
// declaration side of `FSConformanceRegistry`, and the static type relation compares them for a
// `NATIVE`, a `BUILTIN`, and a retroactively conformed `CLASS` source. The rule is deliberately
// one-sided: only two confidently identified, differing arguments are a conflict, so every case
// below that cannot be decided with certainty must stay accepted.
namespace FSRecordedTraitArgumentTests {

using RecordedTypeArgument = FSConformanceRegistry::RecordedTypeArgument;

static FSParser::DataType make_builtin(Variant::Type p_type, NumericType p_numeric_type = NumericType::NONE) {
	FSParser::DataType type;
	type.kind = FSParser::DataType::BUILTIN;
	type.type_source = FSParser::DataType::ANNOTATED_EXPLICIT;
	type.builtin_type = p_type;
	type.numeric_type = p_numeric_type;
	return type;
}

static FSParser::DataType make_variant() {
	FSParser::DataType type;
	type.kind = FSParser::DataType::VARIANT;
	type.type_source = FSParser::DataType::ANNOTATED_EXPLICIT;
	return type;
}

static FSParser::DataType make_native(const StringName &p_class) {
	FSParser::DataType type;
	type.kind = FSParser::DataType::NATIVE;
	type.type_source = FSParser::DataType::ANNOTATED_EXPLICIT;
	type.native_type = p_class;
	return type;
}

static FSParser::DataType make_class(FSParser::ClassNode *p_class, const Vector<FSParser::DataType> &p_type_arguments) {
	FSParser::DataType type;
	type.kind = FSParser::DataType::CLASS;
	type.type_source = FSParser::DataType::ANNOTATED_EXPLICIT;
	type.class_type = p_class;
	type.type_arguments = p_type_arguments;
	return type;
}

static FSParser::ClassNode *find_member_class(FSParser::ClassNode *p_class, const StringName &p_name) {
	for (const FSParser::ClassNode::Member &member : p_class->members) {
		if (member.type == FSParser::ClassNode::Member::CLASS && member.m_class != nullptr &&
				member.m_class->identifier != nullptr && member.m_class->identifier->name == p_name) {
			return member.m_class;
		}
	}
	return nullptr;
}

// One analyzed file whose conformances the registry really holds, so each case states only the pair
// of types it is about. Registration is a process-global side effect of analysis, so the file's
// entries are dropped again when the fixture goes out of scope.
class RecordedTraitArgumentFixture {
public:
	static constexpr const char *SOURCE_PATH = "user://recorded_trait_arguments.fs";

	FSParser parser;
	FSParser::ClassNode *keeper = nullptr;
	FSParser::ClassNode *open_keeper = nullptr;
	FSParser::ClassNode *retro_target = nullptr;
	FSParser::ClassNode *open_target = nullptr;
	Vector<String> error_messages;

	RecordedTraitArgumentFixture() {
		const char *source = R"(
trait Keeper[T]:
	abstract func keep(item: T) -> T


trait OpenKeeper[T]:
	abstract func size() -> int


class RetroTarget:
	pass


class OpenTarget:
	pass


extend RefCounted uses Keeper[int]:
	func keep(item: int) -> int:
		return item


extend int uses Keeper[int]:
	func keep(item: int) -> int:
		return item


extend RetroTarget uses Keeper[int]:
	func keep(item: int) -> int:
		return item


extend OpenTarget uses OpenKeeper:
	func size() -> int:
		return 0


func test() -> void:
	pass
)";
		REQUIRE_EQ(parser.parse(source, SOURCE_PATH, false), OK);
		FSAnalyzer analyzer(&parser);
		// The bare `extend OpenTarget uses OpenKeeper` is an arity error and registers nothing; the
		// specialized `Keeper[int]` conformances still register, so every case below states only the
		// pair of types it is about.
		analyzer.analyze();
		for (const FSParser::ParserError &error : parser.get_errors()) {
			error_messages.push_back(error.message);
		}

		FSParser::ClassNode *tree = parser.get_tree();
		REQUIRE(tree != nullptr);
		keeper = find_member_class(tree, StringName("Keeper"));
		open_keeper = find_member_class(tree, StringName("OpenKeeper"));
		retro_target = find_member_class(tree, StringName("RetroTarget"));
		open_target = find_member_class(tree, StringName("OpenTarget"));
		REQUIRE(keeper != nullptr);
		REQUIRE(open_keeper != nullptr);
		REQUIRE(retro_target != nullptr);
		REQUIRE(open_target != nullptr);
	}

	~RecordedTraitArgumentFixture() {
		FSConformanceRegistry::get_singleton()->clear_file(SOURCE_PATH);
	}

	FSParser::DataType keeper_of(const FSParser::DataType &p_argument) const {
		return make_class(keeper, { p_argument });
	}

	FSParser::DataType bare_keeper() const {
		return make_class(keeper, Vector<FSParser::DataType>());
	}

	FSParser::DataType open_keeper_of(const FSParser::DataType &p_argument) const {
		return make_class(open_keeper, { p_argument });
	}

	FSParser::DataType retro_source() const {
		return make_class(retro_target, Vector<FSParser::DataType>());
	}

	FSParser::DataType open_source() const {
		return make_class(open_target, Vector<FSParser::DataType>());
	}
};

TEST_CASE("[Modules][FoundryScript][TypeCompatibility] A native trait target rejects a recorded conformance argument that conflicts") {
	RecordedTraitArgumentFixture fixture;
	const FSParser::DataType source = make_native(StringName("RefCounted"));

	CHECK_FALSE(FSTypeCompatibility::check(fixture.keeper_of(make_builtin(Variant::STRING)), source).compatible);
	// A subclass reaches the same conformance through the ancestor walk and is checked against it.
	CHECK_FALSE(FSTypeCompatibility::check(fixture.keeper_of(make_builtin(Variant::STRING)),
			make_native(StringName("Resource")))
					.compatible);
}

TEST_CASE("[Modules][FoundryScript][TypeCompatibility] A native trait target accepts a recorded argument that agrees") {
	RecordedTraitArgumentFixture fixture;
	const FSParser::DataType source = make_native(StringName("RefCounted"));

	CHECK(FSTypeCompatibility::check(fixture.keeper_of(make_builtin(Variant::INT)), source).compatible);
	// A raw trait destination asks only the nominal question, so it still accepts.
	CHECK(FSTypeCompatibility::check(fixture.bare_keeper(), source).compatible);
}

TEST_CASE("[Modules][FoundryScript][TypeCompatibility] A bare generic conformance is rejected and records nothing") {
	RecordedTraitArgumentFixture fixture;
	// `extend OpenTarget uses OpenKeeper` supplies no type arguments, which is an arity error; the
	// entry is dropped, so the registry holds no conformance for the target and no destination —
	// specialized or raw — accepts it.
	bool missing_arguments_reported = false;
	for (const String &message : fixture.error_messages) {
		if (message == R"(Generic trait "OpenKeeper" expects 1 type argument(s), but 0 were given.)") {
			missing_arguments_reported = true;
		}
	}
	CHECK(missing_arguments_reported);
	Vector<RecordedTypeArgument> recorded;
	CHECK_FALSE(FSTypeCompatibility::project_registry_trait_arguments(
			fixture.open_source(), StringName("OpenKeeper"), recorded));
	CHECK_FALSE(FSTypeCompatibility::check(fixture.open_keeper_of(make_builtin(Variant::STRING)), fixture.open_source())
					.compatible);
	CHECK_FALSE(FSTypeCompatibility::check(make_class(fixture.open_keeper, Vector<FSParser::DataType>()), fixture.open_source())
					.compatible);
}

TEST_CASE("[Modules][FoundryScript][TypeCompatibility] A builtin trait target rejects a recorded conformance argument that conflicts") {
	RecordedTraitArgumentFixture fixture;
	const FSParser::DataType source = make_builtin(Variant::INT);

	CHECK_FALSE(FSTypeCompatibility::check(fixture.keeper_of(make_builtin(Variant::STRING)), source).compatible);
	CHECK(FSTypeCompatibility::check(fixture.keeper_of(make_builtin(Variant::INT)), source).compatible);
	CHECK(FSTypeCompatibility::check(fixture.bare_keeper(), source).compatible);
}

TEST_CASE("[Modules][FoundryScript][TypeCompatibility] A retroactively conformed class rejects a recorded conformance argument that conflicts") {
	RecordedTraitArgumentFixture fixture;
	const FSParser::DataType source = fixture.retro_source();

	CHECK_FALSE(FSTypeCompatibility::check(fixture.keeper_of(make_builtin(Variant::STRING)), source).compatible);
	CHECK(FSTypeCompatibility::check(fixture.keeper_of(make_builtin(Variant::INT)), source).compatible);
	CHECK(FSTypeCompatibility::check(fixture.bare_keeper(), source).compatible);
}

TEST_CASE("[Modules][FoundryScript][TypeCompatibility] A recorded argument the registry cannot identify never conflicts") {
	RecordedTypeArgument recorded;
	recorded.kind = RecordedTypeArgument::BUILTIN;
	recorded.builtin_type = Variant::INT;

	// An unidentified recorded side proves nothing, whatever the destination declares.
	CHECK_FALSE(FSTypeCompatibility::recorded_argument_conflicts(RecordedTypeArgument(), make_builtin(Variant::STRING)));

	SUBCASE("an unset destination") {
		FSParser::DataType unresolved;
		unresolved.kind = FSParser::DataType::UNRESOLVED;
		CHECK_FALSE(FSTypeCompatibility::recorded_argument_conflicts(recorded, unresolved));
	}
	SUBCASE("a Variant destination") {
		FSParser::DataType variant;
		variant.kind = FSParser::DataType::VARIANT;
		variant.type_source = FSParser::DataType::ANNOTATED_EXPLICIT;
		CHECK_FALSE(FSTypeCompatibility::recorded_argument_conflicts(recorded, variant));
	}
	SUBCASE("a type parameter destination") {
		FSParser::DataType parameter;
		parameter.kind = FSParser::DataType::TYPE_PARAMETER;
		parameter.type_source = FSParser::DataType::ANNOTATED_EXPLICIT;
		parameter.type_parameter_name = StringName("T");
		CHECK_FALSE(FSTypeCompatibility::recorded_argument_conflicts(recorded, parameter));
	}
	SUBCASE("an enum destination") {
		FSParser::DataType enum_type;
		enum_type.kind = FSParser::DataType::ENUM;
		enum_type.type_source = FSParser::DataType::ANNOTATED_EXPLICIT;
		enum_type.native_type = StringName("SomeEnum");
		CHECK_FALSE(FSTypeCompatibility::recorded_argument_conflicts(recorded, enum_type));
	}
	SUBCASE("a tuple destination") {
		FSParser::DataType tuple;
		tuple.kind = FSParser::DataType::TUPLE;
		tuple.type_source = FSParser::DataType::ANNOTATED_EXPLICIT;
		CHECK_FALSE(FSTypeCompatibility::recorded_argument_conflicts(recorded, tuple));
	}
	SUBCASE("a union destination") {
		FSParser::DataType union_type;
		union_type.kind = FSParser::DataType::UNION;
		union_type.type_source = FSParser::DataType::ANNOTATED_EXPLICIT;
		CHECK_FALSE(FSTypeCompatibility::recorded_argument_conflicts(recorded, union_type));
	}
	SUBCASE("a meta-type destination") {
		FSParser::DataType meta = make_builtin(Variant::INT);
		meta.is_meta_type = true;
		CHECK_FALSE(FSTypeCompatibility::recorded_argument_conflicts(recorded, meta));
	}
	SUBCASE("a type-handle destination") {
		FSParser::DataType handle = make_builtin(Variant::INT);
		handle.is_type_handle_annotation = true;
		CHECK_FALSE(FSTypeCompatibility::recorded_argument_conflicts(recorded, handle));
	}
	SUBCASE("an unspecialized recorded side against a typed container destination") {
		// A bare `Array` states nothing about the elements the destination declares, so the differing
		// component count is an absence of evidence rather than a conflict.
		FSParser::DataType typed_array = make_builtin(Variant::ARRAY);
		typed_array.set_container_element_type(0, make_builtin(Variant::STRING));
		RecordedTypeArgument array_recorded;
		array_recorded.kind = RecordedTypeArgument::BUILTIN;
		array_recorded.builtin_type = Variant::ARRAY;
		CHECK_FALSE(FSTypeCompatibility::recorded_argument_conflicts(array_recorded, typed_array));
	}
	SUBCASE("a component the recorded form cannot identify") {
		// The container's own identity agrees and its element has no flattened identity, so the
		// composite as a whole contradicts nothing.
		FSParser::DataType tuple_element;
		tuple_element.kind = FSParser::DataType::TUPLE;
		tuple_element.type_source = FSParser::DataType::ANNOTATED_EXPLICIT;
		FSParser::DataType recorded_array_type = make_builtin(Variant::ARRAY);
		recorded_array_type.set_container_element_type(0, tuple_element);
		const RecordedTypeArgument array_recorded =
				FSConformanceRegistry::reduce_type_argument(recorded_array_type);
		REQUIRE_EQ(array_recorded.kind, RecordedTypeArgument::BUILTIN);
		REQUIRE_EQ(array_recorded.container_element_types.size(), 1);
		REQUIRE_EQ(array_recorded.container_element_types[0].kind, RecordedTypeArgument::UNKNOWN);

		FSParser::DataType typed_array = make_builtin(Variant::ARRAY);
		typed_array.set_container_element_type(0, make_builtin(Variant::STRING));
		CHECK_FALSE(FSTypeCompatibility::recorded_argument_conflicts(array_recorded, typed_array));
	}
	SUBCASE("a callable destination") {
		FSParser::DataType callable = make_builtin(Variant::CALLABLE);
		callable.has_method_signature = true;
		RecordedTypeArgument callable_recorded;
		callable_recorded.kind = RecordedTypeArgument::BUILTIN;
		callable_recorded.builtin_type = Variant::CALLABLE;
		CHECK_FALSE(FSTypeCompatibility::recorded_argument_conflicts(callable_recorded, callable));
	}
}

TEST_CASE("[Modules][FoundryScript][TypeCompatibility] A composite recorded argument is compared component by component") {
	FSParser::DataType recorded_array_type = make_builtin(Variant::ARRAY);
	recorded_array_type.set_container_element_type(0, make_builtin(Variant::INT));
	const RecordedTypeArgument recorded = FSConformanceRegistry::reduce_type_argument(recorded_array_type);
	REQUIRE_EQ(recorded.kind, RecordedTypeArgument::BUILTIN);
	REQUIRE_EQ(recorded.builtin_type, Variant::ARRAY);
	REQUIRE_EQ(recorded.container_element_types.size(), 1);
	CHECK_EQ(recorded.container_element_types[0].kind, RecordedTypeArgument::BUILTIN);
	CHECK_EQ(recorded.container_element_types[0].builtin_type, Variant::INT);

	FSParser::DataType conflicting = make_builtin(Variant::ARRAY);
	conflicting.set_container_element_type(0, make_builtin(Variant::STRING));
	CHECK(FSTypeCompatibility::recorded_argument_conflicts(recorded, conflicting));

	FSParser::DataType agreeing = make_builtin(Variant::ARRAY);
	agreeing.set_container_element_type(0, make_builtin(Variant::INT));
	CHECK_FALSE(FSTypeCompatibility::recorded_argument_conflicts(recorded, agreeing));

	// A component states nothing when the destination declares none at all.
	CHECK_FALSE(FSTypeCompatibility::recorded_argument_conflicts(recorded, make_builtin(Variant::ARRAY)));
}

TEST_CASE("[Modules][FoundryScript][TypeCompatibility] A recorded builtin argument without a declared width agrees with both widths") {
	RecordedTypeArgument unwidened;
	unwidened.kind = RecordedTypeArgument::BUILTIN;
	unwidened.builtin_type = Variant::INT;
	unwidened.numeric_type = NumericType::NONE;

	CHECK_FALSE(FSTypeCompatibility::recorded_argument_conflicts(unwidened, make_builtin(Variant::INT, NumericType::INT32)));
	CHECK_FALSE(FSTypeCompatibility::recorded_argument_conflicts(unwidened, make_builtin(Variant::INT, NumericType::INT64)));

	RecordedTypeArgument widened = unwidened;
	widened.numeric_type = NumericType::INT32;
	// The reverse direction is the same rule: a destination that declared no width agrees with a
	// recorded one, and two declared widths agree only with themselves.
	CHECK_FALSE(FSTypeCompatibility::recorded_argument_conflicts(widened, make_builtin(Variant::INT, NumericType::NONE)));
	CHECK_FALSE(FSTypeCompatibility::recorded_argument_conflicts(widened, make_builtin(Variant::INT, NumericType::INT32)));
	CHECK(FSTypeCompatibility::recorded_argument_conflicts(widened, make_builtin(Variant::INT, NumericType::INT64)));
}

// Hides every declaring file, standing in for a caller whose dependency set does not load the
// conformance.
class HideEverythingVisibility : public FSConformanceRegistry::Visibility {
public:
	bool can_see(const String &p_source_file) const override { return false; }
};

TEST_CASE("[Modules][FoundryScript][TypeCompatibility] A recorded argument from a conformance the caller cannot see is not evidence") {
	RecordedTraitArgumentFixture fixture;
	const FSParser::DataType source = make_native(StringName("RefCounted"));
	REQUIRE_FALSE(FSTypeCompatibility::check(fixture.keeper_of(make_builtin(Variant::STRING)), source).compatible);

	HideEverythingVisibility hidden;
	FSConformanceRegistry::ScopedVisibility visibility_scope(&hidden);

	// The recorded arguments are filtered by the same visibility that filters membership, so a store
	// can never be rejected on the strength of a conformance the same relation says does not exist.
	FSConformanceRegistry *registry = FSConformanceRegistry::get_singleton();
	REQUIRE_FALSE(registry->native_class_conforms(StringName("RefCounted"), StringName("Keeper")));
	Vector<RecordedTypeArgument> recorded;
	CHECK_FALSE(registry->get_native_recorded_trait_arguments(
			StringName("RefCounted"), StringName("Keeper"), recorded));
	CHECK_FALSE(registry->get_recorded_trait_arguments(
			fixture.retro_target->fqcn, StringName("Keeper"), recorded));
	CHECK_FALSE(registry->get_builtin_recorded_trait_arguments(
			Variant::INT, StringName("Keeper"), recorded));
}

// A conformance whose application mixes a concrete argument with one that stays open, so the
// registry has to record the two positions independently: `Duo[int, Self]` on a non-final target
// proves `int` at position 0 and proves nothing at position 1.
class PartiallyOpenTraitArgumentFixture {
public:
	static constexpr const char *SOURCE_PATH = "user://partially_open_trait_arguments.fs";

	FSParser parser;
	FSParser::ClassNode *duo = nullptr;
	FSParser::ClassNode *pair = nullptr;
	FSParser::ClassNode *direct_target = nullptr;
	FSParser::ClassNode *supertrait_target = nullptr;
	FSParser::ClassNode *composite_target = nullptr;
	FSParser::ClassNode *wholly_open_target = nullptr;
	FSParser::ClassNode *variant_target = nullptr;

	PartiallyOpenTraitArgumentFixture() {
		const char *source = R"(
trait Duo[A, B]:
	abstract func first() -> A

	abstract func accept(item: B) -> void


trait DuoSub[A, B]:
	uses Duo[A, B]

	abstract func label() -> String


class Pair[A, B]:
	pass


class DirectTarget:
	pass


class SupertraitTarget:
	pass


class CompositeTarget:
	pass


class WhollyOpenTarget:
	pass


class VariantTarget:
	pass


extend DirectTarget uses Duo[int, Self]:
	func first() -> int:
		return 0

	func accept(item: Self) -> void:
		pass


extend SupertraitTarget uses DuoSub[int, Self]:
	func first() -> int:
		return 0

	func accept(item: Self) -> void:
		pass

	func label() -> String:
		return "sub"


extend CompositeTarget uses Duo[int, Pair[int, Self]]:
	func first() -> int:
		return 0

	func accept(item: Pair[int, Self]) -> void:
		pass


extend WhollyOpenTarget uses Duo[Self, Self]:
	func first() -> Self:
		return self

	func accept(item: Self) -> void:
		pass


extend VariantTarget uses Duo[Variant, int]:
	func first() -> Variant:
		return 0

	func accept(item: int) -> void:
		pass


func test() -> void:
	pass
)";
		REQUIRE_EQ(parser.parse(source, SOURCE_PATH, false), OK);
		FSAnalyzer analyzer(&parser);
		REQUIRE_EQ(analyzer.analyze(), OK);

		FSParser::ClassNode *tree = parser.get_tree();
		REQUIRE(tree != nullptr);
		duo = find_member_class(tree, StringName("Duo"));
		pair = find_member_class(tree, StringName("Pair"));
		direct_target = find_member_class(tree, StringName("DirectTarget"));
		supertrait_target = find_member_class(tree, StringName("SupertraitTarget"));
		composite_target = find_member_class(tree, StringName("CompositeTarget"));
		wholly_open_target = find_member_class(tree, StringName("WhollyOpenTarget"));
		variant_target = find_member_class(tree, StringName("VariantTarget"));
		REQUIRE(duo != nullptr);
		REQUIRE(pair != nullptr);
		REQUIRE(direct_target != nullptr);
		REQUIRE(supertrait_target != nullptr);
		REQUIRE(composite_target != nullptr);
		REQUIRE(wholly_open_target != nullptr);
		REQUIRE(variant_target != nullptr);
	}

	~PartiallyOpenTraitArgumentFixture() {
		FSConformanceRegistry::get_singleton()->clear_file(SOURCE_PATH);
	}

	StringName duo_identity() const { return fs_trait_identity_name(duo); }

	FSParser::DataType duo_of(const FSParser::DataType &p_first, const FSParser::DataType &p_second) const {
		return make_class(duo, { p_first, p_second });
	}

	FSParser::DataType pair_of(const FSParser::DataType &p_first, const FSParser::DataType &p_second) const {
		return make_class(pair, { p_first, p_second });
	}

	FSParser::DataType source_of(FSParser::ClassNode *p_target) const {
		return make_class(p_target, Vector<FSParser::DataType>());
	}
};

TEST_CASE("[Modules][FoundryScript][TypeCompatibility] A conformance records a concrete argument beside an open one") {
	PartiallyOpenTraitArgumentFixture fixture;
	const FSParser::DataType source = fixture.source_of(fixture.direct_target);

	Vector<RecordedTypeArgument> recorded;
	REQUIRE(FSTypeCompatibility::project_registry_trait_arguments(source, fixture.duo_identity(), recorded));
	REQUIRE_EQ(recorded.size(), 2);
	CHECK_EQ(recorded[0].kind, RecordedTypeArgument::BUILTIN);
	CHECK_EQ(recorded[0].builtin_type, Variant::INT);
	// `Self` on a non-final target is not reified here, so the position stays open rather than
	// erasing the `int` its sibling proved.
	CHECK_EQ(recorded[1].kind, RecordedTypeArgument::UNKNOWN);

	CHECK_FALSE(FSTypeCompatibility::check(
			fixture.duo_of(make_builtin(Variant::STRING), source), source)
					.compatible);
	CHECK(FSTypeCompatibility::check(
			fixture.duo_of(make_builtin(Variant::INT), make_builtin(Variant::STRING)), source)
					.compatible);
	CHECK(FSTypeCompatibility::check(
			fixture.duo_of(make_builtin(Variant::INT), make_builtin(Variant::FLOAT)), source)
					.compatible);
}

TEST_CASE("[Modules][FoundryScript][TypeCompatibility] A supertrait identity records the same open position as a direct one") {
	PartiallyOpenTraitArgumentFixture fixture;
	const FSParser::DataType source = fixture.source_of(fixture.supertrait_target);

	Vector<RecordedTypeArgument> recorded;
	REQUIRE(FSTypeCompatibility::project_registry_trait_arguments(source, fixture.duo_identity(), recorded));
	REQUIRE_EQ(recorded.size(), 2);
	CHECK_EQ(recorded[0].kind, RecordedTypeArgument::BUILTIN);
	CHECK_EQ(recorded[0].builtin_type, Variant::INT);
	CHECK_EQ(recorded[1].kind, RecordedTypeArgument::UNKNOWN);

	CHECK_FALSE(FSTypeCompatibility::check(
			fixture.duo_of(make_builtin(Variant::STRING), source), source)
					.compatible);
	CHECK(FSTypeCompatibility::check(
			fixture.duo_of(make_builtin(Variant::INT), make_builtin(Variant::STRING)), source)
					.compatible);
}

TEST_CASE("[Modules][FoundryScript][TypeCompatibility] A composite argument keeps its known component beside an open one") {
	PartiallyOpenTraitArgumentFixture fixture;
	const FSParser::DataType source = fixture.source_of(fixture.composite_target);

	Vector<RecordedTypeArgument> recorded;
	REQUIRE(FSTypeCompatibility::project_registry_trait_arguments(source, fixture.duo_identity(), recorded));
	REQUIRE_EQ(recorded.size(), 2);
	REQUIRE_EQ(recorded[1].kind, RecordedTypeArgument::SCRIPT_CLASS);
	REQUIRE_EQ(recorded[1].type_arguments.size(), 2);
	CHECK_EQ(recorded[1].type_arguments[0].kind, RecordedTypeArgument::BUILTIN);
	CHECK_EQ(recorded[1].type_arguments[0].builtin_type, Variant::INT);
	CHECK_EQ(recorded[1].type_arguments[1].kind, RecordedTypeArgument::UNKNOWN);

	CHECK_FALSE(FSTypeCompatibility::check(
			fixture.duo_of(make_builtin(Variant::INT), fixture.pair_of(make_builtin(Variant::STRING), source)), source)
					.compatible);
	CHECK(FSTypeCompatibility::check(
			fixture.duo_of(make_builtin(Variant::INT),
					fixture.pair_of(make_builtin(Variant::INT), make_builtin(Variant::STRING))),
			source)
					.compatible);
}

TEST_CASE("[Modules][FoundryScript][TypeCompatibility] An explicit Variant argument is recorded as an open position") {
	PartiallyOpenTraitArgumentFixture fixture;
	const FSParser::DataType source = fixture.source_of(fixture.variant_target);

	Vector<RecordedTypeArgument> recorded;
	REQUIRE(FSTypeCompatibility::project_registry_trait_arguments(source, fixture.duo_identity(), recorded));
	REQUIRE_EQ(recorded.size(), 2);
	// `Variant` is written evidence of nothing: the declaration named a position it does not constrain,
	// which is deliberately the same record an open `Self` position leaves behind.
	CHECK_EQ(recorded[0].kind, RecordedTypeArgument::UNKNOWN);
	CHECK_EQ(recorded[1].kind, RecordedTypeArgument::BUILTIN);
	CHECK_EQ(recorded[1].builtin_type, Variant::INT);

	CHECK(FSTypeCompatibility::check(
			fixture.duo_of(make_builtin(Variant::STRING), make_builtin(Variant::INT)), source)
					.compatible);
	CHECK(FSTypeCompatibility::check(
			fixture.duo_of(make_builtin(Variant::FLOAT), make_builtin(Variant::INT)), source)
					.compatible);
	// The concrete sibling is untouched by the open position beside it.
	CHECK_FALSE(FSTypeCompatibility::check(
			fixture.duo_of(make_variant(), make_builtin(Variant::STRING)), source)
					.compatible);
}

TEST_CASE("[Modules][FoundryScript][TypeCompatibility] A wholly open argument vector accepts every specialization") {
	PartiallyOpenTraitArgumentFixture fixture;
	const FSParser::DataType source = fixture.source_of(fixture.wholly_open_target);

	Vector<RecordedTypeArgument> recorded;
	REQUIRE(FSTypeCompatibility::project_registry_trait_arguments(source, fixture.duo_identity(), recorded));
	REQUIRE_EQ(recorded.size(), 2);
	CHECK_EQ(recorded[0].kind, RecordedTypeArgument::UNKNOWN);
	CHECK_EQ(recorded[1].kind, RecordedTypeArgument::UNKNOWN);

	CHECK(FSTypeCompatibility::check(
			fixture.duo_of(make_builtin(Variant::STRING), make_builtin(Variant::FLOAT)), source)
					.compatible);
	CHECK(FSTypeCompatibility::check(
			fixture.duo_of(make_builtin(Variant::INT), make_builtin(Variant::INT)), source)
					.compatible);
}

// One hand-built runtime record whose first argument names a script that no longer exists, standing in
// for an argument script unloaded after the conformance was recorded. The registry holds argument
// scripts weakly on purpose, so this is the state a real reload leaves behind.
class FreedRuntimeArgumentFixture {
public:
	static constexpr const char *SOURCE_FILE = "user://freed_runtime_trait_argument.fs";
	static constexpr const char *SCRIPT_TARGET_KEY = "FreedArgumentTarget";

	StringName trait_name = StringName("FreedArgumentKeeper");
	ObjectID freed_script_id;

	FreedRuntimeArgumentFixture() {
		{
			Ref<FoundryScript> unloaded_argument;
			unloaded_argument.instantiate();
			freed_script_id = unloaded_argument->get_instance_id();
		}
		REQUIRE(freed_script_id.is_valid());
		REQUIRE(ObjectDB::get_instance(freed_script_id) == nullptr);

		FSWeakContainerType freed_argument;
		freed_argument.builtin_type = Variant::OBJECT;
		freed_argument.class_name = StringName("RefCounted");
		freed_argument.script_id = freed_script_id;

		FSWeakContainerType live_argument;
		live_argument.builtin_type = Variant::INT;

		FSConformanceRegistry::RuntimeConformance conformance;
		// The same record answers the script-key, native-ancestry, and builtin lookups, so all three
		// registry entry points are checked against one conformance.
		conformance.target_keys = { String(SCRIPT_TARGET_KEY), String("RefCounted"), String("int") };
		conformance.trait_name = trait_name;
		conformance.trait_type_arguments = { freed_argument, live_argument };
		FSConformanceRegistry::get_singleton()->register_runtime_witnesses(SOURCE_FILE, { conformance });
	}

	~FreedRuntimeArgumentFixture() {
		FSConformanceRegistry::get_singleton()->clear_runtime_witnesses(SOURCE_FILE);
	}
};

static void check_freed_position_degraded(const Vector<ContainerType> &p_arguments) {
	REQUIRE_EQ(p_arguments.size(), 2);
	// The freed position is unconstrained rather than a bare `RefCounted` stand-in, and rather than
	// gone: keeping the arity is what lets the live sibling still be compared.
	CHECK_EQ(p_arguments[0].builtin_type, Variant::NIL);
	CHECK(p_arguments[0].script.is_null());
	CHECK(p_arguments[0].class_name == StringName());
	CHECK_EQ(p_arguments[1].builtin_type, Variant::INT);
}

TEST_CASE("[Modules][FoundryScript][TypeCompatibility] A freed argument script degrades only its own position") {
	FreedRuntimeArgumentFixture fixture;
	FSConformanceRegistry *registry = FSConformanceRegistry::get_singleton();

	Vector<ContainerType> arguments;
	REQUIRE(registry->get_conformance_type_arguments(
			FreedRuntimeArgumentFixture::SCRIPT_TARGET_KEY, fixture.trait_name, arguments));
	check_freed_position_degraded(arguments);

	// The native chain reaches the same entry through the ancestor walk.
	REQUIRE(registry->get_native_conformance_type_arguments(
			StringName("Resource"), fixture.trait_name, arguments));
	check_freed_position_degraded(arguments);

	REQUIRE(registry->get_builtin_conformance_type_arguments(Variant::INT, fixture.trait_name, arguments));
	check_freed_position_degraded(arguments);
}

TEST_CASE("[Modules][FoundryScript][TypeCompatibility] A store through a degraded record still sees the live sibling") {
	FreedRuntimeArgumentFixture fixture;

	ContainerType string_argument;
	string_argument.builtin_type = Variant::STRING;
	ContainerType int_argument;
	int_argument.builtin_type = Variant::INT;

	// A destination that only contradicts the freed position has nothing to contradict: that position
	// carries no evidence at all, exactly like one the declaration left open.
	CHECK(FSDataType::trait_specialization_matches({ string_argument, int_argument },
			Ref<Script>(), fixture.trait_name, nullptr, Variant(7), true));
	// The live `int` sibling survived the degradation and still rejects.
	CHECK_FALSE(FSDataType::trait_specialization_matches({ string_argument, string_argument },
			Ref<Script>(), fixture.trait_name, nullptr, Variant(7), true));
}

} // namespace FSRecordedTraitArgumentTests
