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

#include "modules/foundry_script/fs_analyzer.h"
#include "modules/foundry_script/fs_conformance_registry.h"
#include "modules/foundry_script/fs_parser.h"
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
		REQUIRE_EQ(analyzer.analyze(), OK);

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

TEST_CASE("[Modules][FoundryScript][TypeCompatibility] A trait target accepts a conformance that recorded no arguments") {
	RecordedTraitArgumentFixture fixture;
	// `extend OpenTarget uses OpenKeeper` supplies nothing, which is an absence of evidence rather
	// than a wildcard, so every specialization of the destination stays accepted.
	Vector<RecordedTypeArgument> recorded;
	CHECK_FALSE(FSTypeCompatibility::project_registry_trait_arguments(
			fixture.open_source(), StringName("OpenKeeper"), recorded));
	CHECK(FSTypeCompatibility::check(fixture.open_keeper_of(make_builtin(Variant::STRING)), fixture.open_source())
					.compatible);
	CHECK(FSTypeCompatibility::check(fixture.open_keeper_of(make_builtin(Variant::INT)), fixture.open_source())
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

} // namespace FSRecordedTraitArgumentTests
