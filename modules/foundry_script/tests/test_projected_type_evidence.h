/**************************************************************************/
/*  test_projected_type_evidence.h                                        */
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

#include "../foundry_script.h"
#include "../fs_analyzer.h"
#include "../fs_compiler.h"
#include "../fs_parser.h"

#include "core/variant/container_type_validate.h"

#include "tests/test_macros.h"

// Coverage for the recursive type evidence a generic inheritance chain projects onto an ancestor's
// type parameters. The end-to-end accept/reject behavior (and its `.fsb` round trip) lives in the
// `runtime/features/generic_dependent_argument_*` and `runtime/errors/generic_dependent_argument_*`
// fixtures; these tests pin the evidence itself, which a fixture cannot observe.

namespace FSTests {

struct ScopedProjectedEvidenceLanguage {
	ScopedProjectedEvidenceLanguage() {
		if (!FSLanguage::get_singleton()->has_any_global_constant(SNAME("RefCounted"))) {
			FSLanguage::get_singleton()->init();
		}
	}
};

static Ref<FoundryScript> compile_projected_evidence_source(const String &p_source) {
	static int unique_index = 0;
	const String path = vformat("user://test_projected_type_evidence_%d.fs", unique_index++);

	Ref<FoundryScript> script;
	script.instantiate();
	script->set_path(path);
	script->set_source_code(p_source);

	FSParser parser;
	Error error = parser.parse(p_source, script->get_path(), false);
	REQUIRE(error == OK);

	FSAnalyzer analyzer(&parser);
	error = analyzer.analyze();
	REQUIRE(error == OK);

	FSCompiler compiler;
	error = compiler.compile(&parser, script.ptr(), false);
	REQUIRE(error == OK);

	error = script->reload();
	REQUIRE(error == OK);

	return script;
}

static Ref<FoundryScript> projected_evidence_subclass(const Ref<FoundryScript> &p_script, const StringName &p_name) {
	const HashMap<StringName, Ref<FoundryScript>>::ConstIterator element = p_script->get_subclasses().find(p_name);
	return element ? element->value : Ref<FoundryScript>();
}

// `class Mid[U] extends Base[Pair[int, U]]` is the shape the whole-type dependency flag used to
// erase: `Pair` and `int` are settled for every subclass, `U` is not.
static const char *projected_evidence_source =
		"class Pair[A, B]:\n"
		"\tpass\n"
		"\n"
		"class Base[X]:\n"
		"\tvar value: X\n"
		"\n"
		"class Mid[U] extends Base[Pair[int, U]]:\n"
		"\tpass\n"
		"\n"
		"class Leaf extends Mid[String]:\n"
		"\tpass\n"
		"\n"
		"class VariantLeaf extends Mid[Variant]:\n"
		"\tpass\n"
		"\n"
		"class RawLeaf extends Mid:\n"
		"\tpass\n"
		"\n"
		"class RawLeafHeir[W] extends RawLeaf:\n"
		"\tpass\n";

static ProjectedContainerType project_onto_base(const Ref<FoundryScript> &p_leaf, const Ref<FoundryScript> &p_base,
		const Vector<ContainerType> &p_leaf_type_arguments) {
	Vector<ProjectedContainerType> projected;
	REQUIRE(p_leaf->project_type_arguments_onto_base(p_base, p_leaf_type_arguments, projected));
	REQUIRE_EQ(projected.size(), 1);
	return projected[0];
}

TEST_CASE("[Modules][FoundryScript][Generics] Multi-level forwarding resolves a dependent argument exactly") {
	ScopedProjectedEvidenceLanguage language;
	const Ref<FoundryScript> script = compile_projected_evidence_source(projected_evidence_source);
	const Ref<FoundryScript> base = projected_evidence_subclass(script, "Base");
	const Ref<FoundryScript> pair = projected_evidence_subclass(script, "Pair");
	const Ref<FoundryScript> leaf = projected_evidence_subclass(script, "Leaf");
	REQUIRE(base.is_valid());
	REQUIRE(pair.is_valid());
	REQUIRE(leaf.is_valid());

	const ProjectedContainerType projected = project_onto_base(leaf, base, Vector<ContainerType>());
	CHECK_EQ(projected.state, ProjectedContainerType::EXACT);
	CHECK_EQ(projected.outer.script, Ref<Script>(pair));
	REQUIRE_EQ(projected.type_arguments.size(), 2);
	CHECK_EQ(projected.type_arguments[0].to_container_type().builtin_type, Variant::INT);
	CHECK_EQ(projected.type_arguments[1].to_container_type().builtin_type, Variant::STRING);
}

TEST_CASE("[Modules][FoundryScript][Generics] An unresolved argument leaves its siblings enforced") {
	ScopedProjectedEvidenceLanguage language;
	const Ref<FoundryScript> script = compile_projected_evidence_source(projected_evidence_source);
	const Ref<FoundryScript> base = projected_evidence_subclass(script, "Base");
	const Ref<FoundryScript> pair = projected_evidence_subclass(script, "Pair");
	const Ref<FoundryScript> mid = projected_evidence_subclass(script, "Mid");
	REQUIRE(base.is_valid());
	REQUIRE(pair.is_valid());
	REQUIRE(mid.is_valid());

	// A raw `Mid` knows `Pair` and the `int`, but nothing about `U`.
	const ProjectedContainerType raw = project_onto_base(mid, base, Vector<ContainerType>());
	CHECK_EQ(raw.state, ProjectedContainerType::PARTIAL);
	CHECK_EQ(raw.outer.script, Ref<Script>(pair));
	REQUIRE_EQ(raw.type_arguments.size(), 2);
	CHECK_EQ(raw.type_arguments[0].state, ProjectedContainerType::EXACT);
	CHECK_EQ(raw.type_arguments[0].to_container_type().builtin_type, Variant::INT);
	CHECK_FALSE(raw.type_arguments[1].is_known());

	ContainerType expected_pair;
	expected_pair.builtin_type = Variant::OBJECT;
	expected_pair.class_name = pair->get_instance_base_type();
	expected_pair.script = pair;
	ContainerType int_argument;
	int_argument.builtin_type = Variant::INT;
	ContainerType float_argument;
	float_argument.builtin_type = Variant::FLOAT;
	ContainerType string_argument;
	string_argument.builtin_type = Variant::STRING;

	expected_pair.type_arguments = { int_argument, string_argument };
	CHECK_FALSE(raw.conflicts_with_expected(expected_pair));
	expected_pair.type_arguments = { int_argument, float_argument };
	CHECK_FALSE(raw.conflicts_with_expected(expected_pair));
	expected_pair.type_arguments = { float_argument, string_argument };
	CHECK(raw.conflicts_with_expected(expected_pair));

	// Reifying `U` on the instance resolves the same binding completely.
	ContainerType reified_string;
	reified_string.builtin_type = Variant::STRING;
	const ProjectedContainerType reified = project_onto_base(mid, base, { reified_string });
	CHECK_EQ(reified.state, ProjectedContainerType::EXACT);
	REQUIRE_EQ(reified.type_arguments.size(), 2);
	CHECK_EQ(reified.type_arguments[1].to_container_type().builtin_type, Variant::STRING);
}

TEST_CASE("[Modules][FoundryScript][Generics] An explicit Variant argument stays invariant evidence") {
	ScopedProjectedEvidenceLanguage language;
	const Ref<FoundryScript> script = compile_projected_evidence_source(projected_evidence_source);
	const Ref<FoundryScript> base = projected_evidence_subclass(script, "Base");
	const Ref<FoundryScript> variant_leaf = projected_evidence_subclass(script, "VariantLeaf");
	REQUIRE(base.is_valid());
	REQUIRE(variant_leaf.is_valid());

	const ProjectedContainerType projected = project_onto_base(variant_leaf, base, Vector<ContainerType>());
	CHECK_EQ(projected.state, ProjectedContainerType::EXACT);
	REQUIRE_EQ(projected.type_arguments.size(), 2);

	// `Variant` is a resolved answer, not a missing one: it is known, and a concrete expectation for
	// that slot conflicts with it.
	const ProjectedContainerType &variant_argument = projected.type_arguments[1];
	CHECK(variant_argument.is_known());
	CHECK_EQ(variant_argument.to_container_type(), ContainerType());
	ContainerType string_argument;
	string_argument.builtin_type = Variant::STRING;
	CHECK(variant_argument.conflicts_with_expected(string_argument));
}

TEST_CASE("[Modules][FoundryScript][Generics] A raw extends step cannot collide with a later ordinal") {
	ScopedProjectedEvidenceLanguage language;
	const Ref<FoundryScript> script = compile_projected_evidence_source(projected_evidence_source);
	const Ref<FoundryScript> base = projected_evidence_subclass(script, "Base");
	const Ref<FoundryScript> raw_leaf = projected_evidence_subclass(script, "RawLeaf");
	const Ref<FoundryScript> raw_leaf_heir = projected_evidence_subclass(script, "RawLeafHeir");
	REQUIRE(base.is_valid());
	REQUIRE(raw_leaf.is_valid());
	REQUIRE(raw_leaf_heir.is_valid());

	// `class RawLeaf extends Mid` supplies nothing for `U`, so `U` is permanently unresolved.
	const ProjectedContainerType raw = project_onto_base(raw_leaf, base, Vector<ContainerType>());
	REQUIRE_EQ(raw.type_arguments.size(), 2);
	CHECK_EQ(raw.type_arguments[0].to_container_type().builtin_type, Variant::INT);
	CHECK_FALSE(raw.type_arguments[1].is_known());

	// `class RawLeafHeir[W] extends RawLeaf` declares its own parameter at the very ordinal `U` used to
	// occupy. Reifying `W` must not resurrect evidence for the unrelated `U`.
	ContainerType reified_string;
	reified_string.builtin_type = Variant::STRING;
	const ProjectedContainerType heir = project_onto_base(raw_leaf_heir, base, { reified_string });
	REQUIRE_EQ(heir.type_arguments.size(), 2);
	CHECK_EQ(heir.type_arguments[0].to_container_type().builtin_type, Variant::INT);
	CHECK_FALSE(heir.type_arguments[1].is_known());
}

TEST_CASE("[Modules][FoundryScript][Generics] Class-handle erasure keeps a typed container typed") {
	ScopedProjectedEvidenceLanguage language;
	const Ref<FoundryScript> script = compile_projected_evidence_source(projected_evidence_source);
	const Ref<FoundryScript> pair = projected_evidence_subclass(script, "Pair");
	REQUIRE(pair.is_valid());

	// `Array[RefCounted]` holding a `Pair[int]` handle: erasing the handle to the bare script it
	// specializes must not cost the array its declared element type, which a partially known destination
	// could not restore afterwards.
	ContainerType int_argument;
	int_argument.builtin_type = Variant::INT;
	const Ref<FSSpecializedClassHandle> handle = FSSpecializedClassHandle::create(pair, { int_argument });
	REQUIRE(handle.is_valid());

	ContainerType element_type;
	element_type.builtin_type = Variant::OBJECT;
	element_type.class_name = SNAME("RefCounted");

	Array typed_source;
	typed_source.set_typed(element_type);
	typed_source.push_back(handle);

	ContainerType expected;
	expected.builtin_type = Variant::ARRAY;
	expected.element_types.push_back(element_type);

	Variant value = typed_source;
	REQUIRE(FoundryScript::erase_specialized_class_handles_for_container_type(expected, value));
	const Array erased = value;
	CHECK(erased.is_typed());
	CHECK_EQ(erased.get_element_type(), element_type);
	REQUIRE_EQ(erased.size(), 1);
	CHECK_EQ(Ref<Script>(erased[0]), Ref<Script>(pair));
}

} // namespace FSTests
