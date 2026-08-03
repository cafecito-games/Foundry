/**************************************************************************/
/*  container_type_validate.h                                             */
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

#include "core/object/script_language.h"
#include "core/templates/vector.h"
#include "core/variant/numeric_type.h"
#include "core/variant/variant.h"

struct ContainerType {
	Variant::Type builtin_type = Variant::NIL;
	// Exact width and signedness of an integer slot. `NONE` means the slot is constrained by its
	// carrier alone, which is what every non-numeric and every unannotated numeric slot uses.
	NumericType numeric_type = NumericType::NONE;
	StringName class_name;
	Ref<Script> script;
	Vector<ContainerType> element_types;
	// Reified type arguments of a specialized script type, e.g. the `int` in `Box[int]`. Empty for
	// unspecialized types. Unlike `element_types` (which describes typed Array/Dictionary contents),
	// these carry the bound generic arguments of a script handle.
	Vector<ContainerType> type_arguments;
	// True when the described slot holds a class handle for the type above (`Type[Node]`) rather than an
	// instance of it. Legal only when `builtin_type == Variant::OBJECT`.
	bool is_type_handle = false;

	bool operator==(const ContainerType &p_type) const;
	bool operator!=(const ContainerType &p_type) const;
	String get_type_name() const;
};

struct ContainerTypeValidate {
	Variant::Type type = Variant::NIL;
	// Mirrors `ContainerType::numeric_type`. Only consulted when it is not `NONE`, so a slot without a
	// declared width keeps behaving exactly as it did before descriptors existed.
	NumericType numeric_type = NumericType::NONE;
	StringName class_name;
	Ref<Script> script;
	Vector<ContainerTypeValidate> element_types;
	// Reified type arguments of a specialized script element type, e.g. the `int` in an
	// `Array[Box[int]]` element. Carried through so `Box[int]` and `Box[String]` element typings stay
	// distinct at runtime. Empty for non-generic or unspecialized types (all native/engine uses).
	Vector<ContainerType> type_arguments;
	// Mirrors `ContainerType::is_type_handle`: the slot holds a class handle for the type above, not an
	// instance of it.
	bool is_type_handle = false;
	const char *where = "container";

private:
	bool _internal_validate(Variant &inout_variant, const char *p_operation, bool p_output_errors) const;
	bool _internal_validate_object(const Variant &p_variant, const char *p_operation, bool p_output_errors) const;
	bool _internal_validate_class_handle(const Variant &p_variant, const char *p_operation, bool p_output_errors) const;
	bool _internal_validate_array(Variant &inout_variant, const char *p_operation, bool p_output_errors) const;
	bool _internal_validate_dictionary(Variant &inout_variant, const char *p_operation, bool p_output_errors) const;
	// The described type without the `Type[...]` handle wrapper.
	String _get_value_type_name() const;

public:
	ContainerTypeValidate() = default;
	ContainerTypeValidate(const ContainerType &p_type);

	_FORCE_INLINE_ bool validate(Variant &inout_variant, const char *p_operation = "use") const {
		return _internal_validate(inout_variant, p_operation, true);
	}

	_FORCE_INLINE_ bool validate_object(const Variant &p_variant, const char *p_operation = "use") const {
		if (is_type_handle) {
			return _internal_validate_class_handle(p_variant, p_operation, true);
		}
		return _internal_validate_object(p_variant, p_operation, true);
	}

	_FORCE_INLINE_ bool test_validate(const Variant &p_variant) const {
		Variant tmp = p_variant;
		return _internal_validate(tmp, "", false);
	}

	ContainerType get_container_type() const;
	Variant make_default() const;
	String get_type_name() const;

	bool can_reference(const ContainerTypeValidate &p_type) const;
	bool operator==(const ContainerTypeValidate &p_type) const;
	bool operator!=(const ContainerTypeValidate &p_type) const;
};

// Recursive evidence about a type projected through a generic inheritance chain.
//
// A projection cannot always resolve every part of a type: `class Mid[U] extends Base[Pair[int, U]]`
// fixes the outer `Pair` and its first argument for every subclass, while `U` only becomes concrete
// once a leaf supplies it. A single "known / not known" bit for the whole type would have to discard
// the `int` alongside the unresolved `U`, so evidence is tracked per node instead: the known parts
// keep validating invariantly and only the genuinely unresolved subtrees stay gradual.
struct ProjectedContainerType {
	enum State : uint8_t {
		// No evidence at all. The slot stays gradual and never rejects a value.
		UNKNOWN,
		// The node itself is known, and so is each descendant not marked `UNKNOWN`.
		PARTIAL,
		// The complete subtree is known and validates invariantly.
		EXACT,
	};

	State state = UNKNOWN;
	// Shallow node identity only: builtin carrier, numeric width, native class, script, and the
	// class-handle bit. The child vectors below carry the descendants, so `outer.element_types` and
	// `outer.type_arguments` are always empty.
	ContainerType outer;
	Vector<ProjectedContainerType> element_types;
	Vector<ProjectedContainerType> type_arguments;

	// Builds complete evidence from a fully resolved type. Subtrees deeper than
	// `Variant::MAX_RECURSION_DEPTH` degrade to `UNKNOWN` rather than failing the whole conversion.
	static ProjectedContainerType exact(const ContainerType &p_type);

	_FORCE_INLINE_ bool is_known() const { return state != UNKNOWN; }

	// Materializes the described type. Unknown subtrees become unconstrained slots, so the result is
	// only equivalent to the evidence when `state == EXACT`.
	ContainerType to_container_type() const;
	String get_type_name() const;

	// True when this evidence contradicts the fully known type `p_expected`. Unknown subtrees never
	// contradict anything; known ones must match exactly, arguments included.
	bool conflicts_with_expected(const ContainerType &p_expected) const;
	// Symmetric form used when both sides may carry unknown subtrees: only two known, differing nodes
	// are a conflict.
	bool conflicts_with(const ProjectedContainerType &p_other) const;

	// Validates a write of `r_value` into a slot described by this evidence, converting the value where
	// a fully known container type would. Unknown subtrees are skipped; every known one is enforced.
	bool validate_value(Variant &r_value, const char *p_where, const char *p_operation) const;

private:
	static ProjectedContainerType _exact(const ContainerType &p_type, int p_depth);
	bool _validate_known_descendants(Variant &p_value, const char *p_where, const char *p_operation) const;
};

namespace ContainerTypeDescriptor {
bool from_variant(const Variant &p_descriptor, ContainerType &r_type, String *r_error = nullptr);
Variant to_variant(const ContainerType &p_type);
} // namespace ContainerTypeDescriptor
