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
#include "core/variant/variant.h"

struct ContainerType {
	Variant::Type builtin_type = Variant::NIL;
	StringName class_name;
	Ref<Script> script;
	Vector<ContainerType> element_types;
	// Reified type arguments of a specialized script type, e.g. the `int` in `Box[int]`. Empty for
	// unspecialized types. Unlike `element_types` (which describes typed Array/Dictionary contents),
	// these carry the bound generic arguments of a script handle.
	Vector<ContainerType> type_arguments;

	bool operator==(const ContainerType &p_type) const;
	bool operator!=(const ContainerType &p_type) const;
	String get_type_name() const;
};

struct ContainerTypeValidate {
	Variant::Type type = Variant::NIL;
	StringName class_name;
	Ref<Script> script;
	Vector<ContainerTypeValidate> element_types;
	// Reified type arguments of a specialized script element type, e.g. the `int` in an
	// `Array[Box[int]]` element. Carried through so `Box[int]` and `Box[String]` element typings stay
	// distinct at runtime. Empty for non-generic or unspecialized types (all native/engine uses).
	Vector<ContainerType> type_arguments;
	const char *where = "container";

private:
	bool _internal_validate(Variant &inout_variant, const char *p_operation, bool p_output_errors) const;
	bool _internal_validate_object(const Variant &p_variant, const char *p_operation, bool p_output_errors) const;
	bool _internal_validate_array(Variant &inout_variant, const char *p_operation, bool p_output_errors) const;
	bool _internal_validate_dictionary(Variant &inout_variant, const char *p_operation, bool p_output_errors) const;

public:
	ContainerTypeValidate() = default;
	ContainerTypeValidate(const ContainerType &p_type);

	_FORCE_INLINE_ bool validate(Variant &inout_variant, const char *p_operation = "use") const {
		return _internal_validate(inout_variant, p_operation, true);
	}

	_FORCE_INLINE_ bool validate_object(const Variant &p_variant, const char *p_operation = "use") const {
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

namespace ContainerTypeDescriptor {
bool from_variant(const Variant &p_descriptor, ContainerType &r_type, String *r_error = nullptr);
Variant to_variant(const ContainerType &p_type);
} // namespace ContainerTypeDescriptor
