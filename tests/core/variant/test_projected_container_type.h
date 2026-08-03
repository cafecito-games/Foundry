/**************************************************************************/
/*  test_projected_container_type.h                                       */
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

#include "core/variant/container_type_validate.h"

#include "tests/test_macros.h"

namespace TestProjectedContainerType {

static ContainerType make_builtin(Variant::Type p_type, NumericType p_numeric_type = NumericType::NONE) {
	ContainerType type;
	type.builtin_type = p_type;
	type.numeric_type = p_numeric_type;
	return type;
}

static ContainerType make_object(const StringName &p_class_name, const Vector<ContainerType> &p_type_arguments = {}, bool p_is_type_handle = false) {
	ContainerType type;
	type.builtin_type = Variant::OBJECT;
	type.class_name = p_class_name;
	type.type_arguments = p_type_arguments;
	type.is_type_handle = p_is_type_handle;
	return type;
}

static ContainerType make_array_of(const ContainerType &p_element) {
	ContainerType type;
	type.builtin_type = Variant::ARRAY;
	type.element_types.push_back(p_element);
	return type;
}

// `Pair[int, ?]`: the outer type and the first argument are known, the second is not.
static ProjectedContainerType make_partial_pair(const ContainerType &p_known_first) {
	ProjectedContainerType projected;
	projected.state = ProjectedContainerType::PARTIAL;
	projected.outer = make_object(SNAME("RefCounted"));
	projected.type_arguments.push_back(ProjectedContainerType::exact(p_known_first));
	projected.type_arguments.push_back(ProjectedContainerType());
	return projected;
}

TEST_CASE("[ProjectedContainerType] Exact evidence reproduces invariant comparison") {
	const ContainerType expected = make_object(SNAME("RefCounted"), { make_builtin(Variant::INT), make_builtin(Variant::STRING) });
	const ProjectedContainerType projected = ProjectedContainerType::exact(expected);

	CHECK_EQ(projected.state, ProjectedContainerType::EXACT);
	CHECK_FALSE(projected.conflicts_with_expected(expected));
	CHECK_EQ(projected.to_container_type(), expected);

	// A differing argument, a differing arity, and a differing outer class all conflict.
	CHECK(projected.conflicts_with_expected(make_object(SNAME("RefCounted"), { make_builtin(Variant::FLOAT), make_builtin(Variant::STRING) })));
	CHECK(projected.conflicts_with_expected(make_object(SNAME("RefCounted"), { make_builtin(Variant::INT) })));
	CHECK(projected.conflicts_with_expected(make_object(SNAME("Node"), { make_builtin(Variant::INT), make_builtin(Variant::STRING) })));
}

TEST_CASE("[ProjectedContainerType] Unknown evidence never conflicts") {
	const ProjectedContainerType unknown;
	CHECK_FALSE(unknown.is_known());
	CHECK_FALSE(unknown.conflicts_with_expected(make_object(SNAME("RefCounted"), { make_builtin(Variant::INT) })));
	CHECK_FALSE(unknown.conflicts_with(ProjectedContainerType::exact(make_builtin(Variant::INT))));
	CHECK_FALSE(ProjectedContainerType::exact(make_builtin(Variant::INT)).conflicts_with(unknown));
}

TEST_CASE("[ProjectedContainerType] Partial evidence keeps enforcing its known arguments") {
	const ProjectedContainerType projected = make_partial_pair(make_builtin(Variant::INT));

	// The known `int` slot still rejects a `float`, and the unknown slot accepts anything.
	CHECK(projected.conflicts_with_expected(make_object(SNAME("RefCounted"), { make_builtin(Variant::FLOAT), make_builtin(Variant::STRING) })));
	CHECK_FALSE(projected.conflicts_with_expected(make_object(SNAME("RefCounted"), { make_builtin(Variant::INT), make_builtin(Variant::STRING) })));
	CHECK_FALSE(projected.conflicts_with_expected(make_object(SNAME("RefCounted"), { make_builtin(Variant::INT), make_builtin(Variant::BOOL) })));

	// A wrong outer class rejects even though a descendant is unknown.
	CHECK(projected.conflicts_with_expected(make_object(SNAME("Node"), { make_builtin(Variant::INT), make_builtin(Variant::STRING) })));
}

TEST_CASE("[ProjectedContainerType] Explicit Variant is exact evidence, not an unresolved slot") {
	const ProjectedContainerType explicit_variant = ProjectedContainerType::exact(ContainerType());
	CHECK(explicit_variant.is_known());
	CHECK_EQ(explicit_variant.state, ProjectedContainerType::EXACT);

	// An unconstrained expected slot matches; a constrained one does not.
	CHECK_FALSE(explicit_variant.conflicts_with_expected(ContainerType()));
	CHECK(explicit_variant.conflicts_with_expected(make_builtin(Variant::INT)));

	// An unknown slot, by contrast, satisfies both.
	const ProjectedContainerType unknown;
	CHECK_FALSE(unknown.conflicts_with_expected(make_builtin(Variant::INT)));
}

TEST_CASE("[ProjectedContainerType] Numeric width and handle shape reject with unknown descendants") {
	ProjectedContainerType projected = make_partial_pair(make_builtin(Variant::INT));

	// Same carrier, different declared width.
	ProjectedContainerType widened = projected;
	widened.type_arguments.write[0] = ProjectedContainerType::exact(make_builtin(Variant::INT, NumericType::INT32));
	CHECK(widened.conflicts_with_expected(make_object(SNAME("RefCounted"), { make_builtin(Variant::INT, NumericType::INT64), ContainerType() })));
	CHECK_FALSE(widened.conflicts_with_expected(make_object(SNAME("RefCounted"), { make_builtin(Variant::INT, NumericType::INT32), ContainerType() })));

	// A class handle and an instance describe disjoint value sets.
	projected.outer.is_type_handle = true;
	CHECK(projected.conflicts_with_expected(make_object(SNAME("RefCounted"), { make_builtin(Variant::INT), ContainerType() })));
	CHECK_FALSE(projected.conflicts_with_expected(make_object(SNAME("RefCounted"), { make_builtin(Variant::INT), ContainerType() }, true)));
}

TEST_CASE("[ProjectedContainerType] Untyped containers are enforced value by value") {
	// `Array[Dictionary[int, ?]]`: neither container can be compared through element metadata when the
	// assigned value carries none, so the known key type has to be checked per entry.
	ProjectedContainerType partial_dictionary;
	partial_dictionary.state = ProjectedContainerType::PARTIAL;
	partial_dictionary.outer = make_builtin(Variant::DICTIONARY);
	partial_dictionary.element_types.push_back(ProjectedContainerType::exact(make_builtin(Variant::INT)));
	partial_dictionary.element_types.push_back(ProjectedContainerType());

	ProjectedContainerType partial_array;
	partial_array.state = ProjectedContainerType::PARTIAL;
	partial_array.outer = make_builtin(Variant::ARRAY);
	partial_array.element_types.push_back(partial_dictionary);

	Dictionary accepted_entry;
	accepted_entry[1] = "one";
	Array accepted;
	accepted.push_back(accepted_entry);
	Variant accepted_value = accepted;
	CHECK(partial_array.validate_value(accepted_value, "member", "assign"));

	Dictionary rejected_entry;
	rejected_entry["one"] = "one";
	Array rejected;
	rejected.push_back(rejected_entry);
	Variant rejected_value = rejected;
	ERR_PRINT_OFF;
	CHECK_FALSE(partial_array.validate_value(rejected_value, "member", "assign"));
	ERR_PRINT_ON;

	// A descendant validator that converts an element writes the converted form back, so the stored
	// value matches what validation accepted.
	ProjectedContainerType partial_array_of_typed_arrays;
	partial_array_of_typed_arrays.state = ProjectedContainerType::PARTIAL;
	partial_array_of_typed_arrays.outer = make_builtin(Variant::ARRAY);
	partial_array_of_typed_arrays.element_types.push_back(ProjectedContainerType::exact(make_array_of(make_builtin(Variant::INT))));

	Array untyped_inner;
	untyped_inner.push_back(1);
	Array outer;
	outer.push_back(untyped_inner);
	Variant convertible_value = outer;
	REQUIRE(partial_array_of_typed_arrays.validate_value(convertible_value, "member", "assign"));
	const Array stored = convertible_value;
	REQUIRE_EQ(stored.size(), 1);
	const Array stored_inner = stored[0];
	CHECK(stored_inner.is_typed());
	CHECK_EQ(stored_inner.get_element_type(), make_builtin(Variant::INT));

	// A typed outer container whose metadata does not witness the known evidence is still checked value
	// by value, and keeps both its own element type and the conversions that check produced.
	Array typed_outer;
	typed_outer.set_typed(make_builtin(Variant::ARRAY));
	Array typed_outer_inner;
	typed_outer_inner.push_back(2);
	typed_outer.push_back(typed_outer_inner);
	Variant typed_outer_value = typed_outer;
	REQUIRE(partial_array_of_typed_arrays.validate_value(typed_outer_value, "member", "assign"));
	const Array typed_stored = typed_outer_value;
	CHECK(typed_stored.is_typed());
	CHECK_EQ(typed_stored.get_element_type(), make_builtin(Variant::ARRAY));
	const Array typed_stored_inner = typed_stored[0];
	CHECK(typed_stored_inner.is_typed());
	CHECK_EQ(typed_stored_inner.get_element_type(), make_builtin(Variant::INT));

	// An entirely unknown element slot still accepts anything.
	ProjectedContainerType unknown_element_array;
	unknown_element_array.state = ProjectedContainerType::PARTIAL;
	unknown_element_array.outer = make_builtin(Variant::ARRAY);
	unknown_element_array.element_types.push_back(ProjectedContainerType());
	CHECK(unknown_element_array.validate_value(rejected_value, "member", "assign"));
}

TEST_CASE("[ProjectedContainerType] Unspecialized source metadata does not witness known arguments") {
	// Expected `Array[RefCounted[int, ?]]` against a source typed as a raw `Array[RefCounted]`: the
	// source metadata never mentions the arguments, so comparing against it proves nothing about them.
	ProjectedContainerType partial_element;
	partial_element.state = ProjectedContainerType::PARTIAL;
	partial_element.outer = make_object(SNAME("RefCounted"));
	partial_element.type_arguments.push_back(ProjectedContainerType::exact(make_builtin(Variant::INT)));
	partial_element.type_arguments.push_back(ProjectedContainerType());

	const ProjectedContainerType raw_source = ProjectedContainerType::exact(make_object(SNAME("RefCounted")));
	CHECK_FALSE(partial_element.conflicts_with(raw_source));
	CHECK_FALSE(partial_element.is_witnessed_by(raw_source));

	const ProjectedContainerType specialized_source = ProjectedContainerType::exact(
			make_object(SNAME("RefCounted"), { make_builtin(Variant::INT), make_builtin(Variant::STRING) }));
	CHECK_FALSE(partial_element.conflicts_with(specialized_source));
	CHECK(partial_element.is_witnessed_by(specialized_source));

	// An entirely unknown expectation is witnessed by anything, including nothing at all.
	CHECK(ProjectedContainerType().is_witnessed_by(raw_source));
	CHECK(ProjectedContainerType().is_witnessed_by(ProjectedContainerType()));
	CHECK_FALSE(specialized_source.is_witnessed_by(ProjectedContainerType()));
}

TEST_CASE("[ProjectedContainerType] Nesting deeper than the recursion cap degrades only that subtree") {
	ContainerType deep = make_builtin(Variant::INT);
	for (int i = 0; i < Variant::MAX_RECURSION_DEPTH + 4; i++) {
		deep = make_array_of(deep);
	}

	const ProjectedContainerType projected = ProjectedContainerType::exact(deep);
	REQUIRE(projected.is_known());

	const ProjectedContainerType *node = &projected;
	int depth = 0;
	while (node->is_known() && !node->element_types.is_empty()) {
		node = &node->element_types[0];
		depth++;
	}
	// Every level up to the cap kept its evidence; the subtree past it did not.
	CHECK_EQ(depth, Variant::MAX_RECURSION_DEPTH + 1);
	CHECK_FALSE(node->is_known());
	CHECK_FALSE(projected.conflicts_with_expected(deep));
}

} // namespace TestProjectedContainerType
