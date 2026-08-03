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
