/**************************************************************************/
/*  fs_type.cpp                                                           */
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

#include "fs_type.h"

#include "foundry_script.h"
#include "fs_conformance_registry.h"
#include "fs_trait_utils.h"

#include "core/object/class_db.h"

// A type parameter that leaves nothing behind for the runtime to check a value against.
//
// A method-scope parameter is chosen per call and erased to Variant, and `FSDataType::is_type()`
// accepts any value for one, so no check exists or can be emitted for a slot declared with it.
//
// The other scopes survive execution. A class-scope parameter -- which includes the synthetic
// `@Self` -- is reified onto the instance from its type arguments, and a slot declared with it is
// projected against that reification before a store (`_project_binding_data_type()` in
// `foundry_script.cpp`), which is what makes a member write through a raw `Box` a genuinely
// runtime-checked assignment. Enum-scope parameters keep the same allowance: they are resolved
// against the enum declaration rather than per call.
static bool _is_erased_type_parameter(const FSParser::DataType &p_type) {
	return p_type.kind == FSParser::DataType::TYPE_PARAMETER &&
			p_type.type_parameter_scope == FSParser::DataType::TYPE_PARAMETER_METHOD;
}

// A parameter bounded by a `final` class denotes exactly that class: no subtype of the bound can
// exist, so the caller has only the bound (or its nullable form) to choose from. A value the bound
// accepts is therefore a value every possible type argument accepts, and the erasure argument does
// not apply. Only the direct bound counts, matching how a final bound closes a receiver elsewhere.
static bool _is_type_parameter_bounded_by_final_class(const FSParser::DataType &p_type) {
	return p_type.kind == FSParser::DataType::TYPE_PARAMETER && p_type.type_parameter_bound.size() == 1 &&
			p_type.type_parameter_bound[0].kind == FSParser::DataType::CLASS &&
			p_type.type_parameter_bound[0].class_type != nullptr &&
			p_type.type_parameter_bound[0].class_type->is_final;
}

// A type-parameter destination nothing at the destination can decide. A method-scope parameter never
// can be. A class-scope one normally can, because the receiver reifies it and the store is validated
// against that reification -- but a static frame has no receiver, so in one the class-scope parameter
// is exactly as undecidable as a method-scope one. `@Self` is excluded: it denotes the class the frame
// runs against, which a static frame has too, and it lowers to a concrete script rather than an erased
// slot.
static bool _is_undecidable_type_parameter_target(const FSParser::DataType &p_type, const FSTypeCompatibility::Options &p_options) {
	if (_is_erased_type_parameter(p_type)) {
		return true;
	}
	return !p_options.receiver_is_available && p_type.kind == FSParser::DataType::TYPE_PARAMETER &&
			p_type.type_parameter_scope == FSParser::DataType::TYPE_PARAMETER_CLASS &&
			p_type.type_parameter_name != SNAME("@Self");
}

bool FSTypeCompatibility::resolve_final_class_bound(const FSParser::DataType &p_type, FSParser::DataType &r_resolved) {
	// `@Self` denotes the class a frame runs against and has its own lowering everywhere; it is never
	// resolved through a declared bound.
	if (p_type.type_parameter_name == SNAME("@Self") || !_is_type_parameter_bounded_by_final_class(p_type)) {
		return false;
	}
	FSParser::DataType resolved = p_type.type_parameter_bound[0];
	resolved.type_source = p_type.type_source;
	// Wrappers the parameter itself declared survive resolution: `T?` resolves to `Label?` and `Type[T]`
	// to a class handle for `Label`, so the resolved shape lowers exactly as the position demanded.
	resolved.is_nullable = resolved.is_nullable || p_type.is_nullable;
	resolved.is_meta_type = p_type.is_meta_type;
	resolved.is_type_handle_annotation = p_type.is_type_handle_annotation;
	resolved.is_coroutine = p_type.is_coroutine;
	r_resolved = resolved;
	return true;
}

// Whether the runtime slot compiled for the position reached so far still states the bound a
// final-bounded parameter resolves to. Only a position that does can license a gradual source, because
// only there does compiler lowering emit a check against the resolved bound.
//
// `p_final_bound_is_representable` is sticky-off: once traversal crosses a wrapper whose lowering
// discards the resolved type, nothing below it can regain evidence. `p_nullable_is_expressible` says
// whether a nullable node here is still evidence, and holds on the tuple spine and at the root -- both
// carry `is_nullable` to run time -- but not inside a `ContainerType`, which has no such field.
//
// Wrapping an erased parameter in a container creates no evidence for an *unbounded* parameter:
// `Array[T]` compiles to a plain untyped Array, so a concrete `Array[int]` stored there is never
// element-checked and comes back out as an `Array[T]` the callee trusts. The same holds for both
// dictionary slots, for generic type arguments, and for the parameter/return slots of a callable
// signature, so the leaf rule from a bare `T` destination is applied to every leaf of the shape. A leaf
// bounded by a `final` class is different exactly where its bound survives lowering: as a typed
// container element, as a specialized type argument, and along the tuple spine.
static bool _destination_has_erased_type_parameter(const FSParser::DataType &p_type, int p_depth = 0,
		bool p_final_bound_is_representable = true, bool p_nullable_is_expressible = true) {
	if (unlikely(p_depth > Variant::MAX_RECURSION_DEPTH)) {
		// A shape this deep cannot be reasoned about usefully; leave the decision to the rules that ran
		// before this one rather than inventing a rejection here.
		return false;
	}

	// A nullable node off the tuple spine lowers to a container slot that cannot say "or null", so the
	// runtime keeps no evidence for it or for anything below it.
	const bool representable = p_final_bound_is_representable && !(p_type.is_nullable && !p_nullable_is_expressible);

	if (p_type.kind == FSParser::DataType::TYPE_PARAMETER) {
		if (!_is_erased_type_parameter(p_type)) {
			return false;
		}
		return !(representable && _is_type_parameter_bounded_by_final_class(p_type));
	}

	if (p_type.kind == FSParser::DataType::TUPLE) {
		// A tuple destination is tested element by element, and a parameter named directly by an element
		// decides that element. A container declared around a parameter inside a tuple is deliberately
		// left alone: its runtime element typing belongs to its concrete consumer, which is why a
		// concrete value is accepted into a `(int, Array[T])` destination today. Descending into one here
		// would answer "undecidable" for a destination the compatibility rules accept.
		for (const FSParser::DataType &element : p_type.container_element_types) {
			if (element.kind != FSParser::DataType::TYPE_PARAMETER && element.kind != FSParser::DataType::TUPLE) {
				continue;
			}
			if (_destination_has_erased_type_parameter(element, p_depth + 1, representable, p_nullable_is_expressible)) {
				return true;
			}
		}
		return false;
	}

	// A typed container element and a specialized handle's type argument both keep a concrete resolved
	// bound in their runtime descriptor, so a final bound stays enforced across either crossing. Neither
	// descriptor retains a nested tuple's shape, though, so a tuple below one is analyzer-only.
	const Vector<FSParser::DataType> *reified_slots[] = {
		&p_type.container_element_types,
		&p_type.type_arguments,
	};
	for (const Vector<FSParser::DataType> *slots : reified_slots) {
		for (const FSParser::DataType &slot : *slots) {
			const bool slot_representable = representable && slot.kind != FSParser::DataType::TUPLE;
			if (_destination_has_erased_type_parameter(slot, p_depth + 1, slot_representable, false)) {
				return true;
			}
		}
	}

	// A callable/signal signature erases completely at run time and a union erases to one untyped slot,
	// so no bound resolved below either is ever checked.
	const Vector<FSParser::DataType> *erasing_slots[] = {
		&p_type.method_parameter_types,
		&p_type.method_return_type,
		&p_type.method_rest_parameter_type,
		&p_type.union_members,
	};
	for (const Vector<FSParser::DataType> *slots : erasing_slots) {
		for (const FSParser::DataType &slot : *slots) {
			if (_destination_has_erased_type_parameter(slot, p_depth + 1, false, false)) {
				return true;
			}
		}
	}
	return false;
}

// A resource path identifies the one class that owns the file. Every inner class compiled from the
// file reports that same path, so a conformance lookup by path on behalf of an inner class would
// answer with a sibling's or the root class's conformance. See
// `FSConformanceRegistry::Conformance::target_script_path`.
static bool _path_identifies_script(const Ref<Script> &p_script) {
	const FoundryScript *foundry_script = Object::cast_to<FoundryScript>(p_script.ptr());
	return foundry_script == nullptr || foundry_script->is_root_script();
}

static bool _is_signature_builtin_type(Variant::Type p_type) {
	return p_type == Variant::CALLABLE || p_type == Variant::SIGNAL;
}

static bool _property_signature_equal(const PropertyInfo &p_left, const PropertyInfo &p_right) {
	// Only the type-identifying fields matter for signature compatibility. Storage/editor usage flags do
	// not: a synthesized slot (DataType::to_property_info, usage NONE) must still match an equivalent
	// natural MethodInfo slot (e.g. a utility function like `sin`, usage DEFAULT). The one usage bit that
	// is type-relevant is NIL_IS_VARIANT, which distinguishes a `Variant` slot from a concrete/`void` NIL.
	return p_left.type == p_right.type &&
			p_left.class_name == p_right.class_name &&
			p_left.hint == p_right.hint &&
			p_left.hint_string == p_right.hint_string &&
			(p_left.usage & PROPERTY_USAGE_NIL_IS_VARIANT) == (p_right.usage & PROPERTY_USAGE_NIL_IS_VARIANT);
}

static bool _method_signature_equal(const MethodInfo &p_left, const MethodInfo &p_right) {
	if (!_property_signature_equal(p_left.return_val, p_right.return_val)) {
		return false;
	}
	if (p_left.arguments.size() != p_right.arguments.size()) {
		return false;
	}
	for (int i = 0; i < p_left.arguments.size(); i++) {
		if (!_property_signature_equal(p_left.arguments[i], p_right.arguments[i])) {
			return false;
		}
	}
	return true;
}

static bool _datatype_invariant_equal(const FSParser::DataType &p_a, const FSParser::DataType &p_b);

static bool _datatype_method_signature_equal(const FSParser::DataType &p_left, const FSParser::DataType &p_right,
		bool p_contravariant_rest = false, bool p_strict_null = false);

// Strict signature-slot comparison for the explicit `Callable[[...], ...]` / `Signal[[...]]` path.
// `operator==` establishes matching outer structure (including is_nullable, generic type arguments, and
// container element kinds) and then the composite slots it only compared shallowly are recursed into so a
// nested Callable/Signal mismatch is still detected. This is the long-standing explicit-signature
// behavior and is intentionally left untouched: only the non-explicit source path is broadened below.
static bool _datatype_signature_slot_equal(const FSParser::DataType &p_left, const FSParser::DataType &p_right) {
	if (p_left != p_right) {
		return false;
	}
	// `operator==` above already established matching outer structure; recurse into the composite
	// slots it only compared shallowly. The size guards keep the lenient outcome it returns for
	// UNDETECTED/INFERRED operands (where the slot vectors may legitimately differ in length).
	if (p_left.container_element_types.size() == p_right.container_element_types.size()) {
		for (int i = 0; i < p_left.container_element_types.size(); i++) {
			if (!_datatype_signature_slot_equal(p_left.container_element_types[i], p_right.container_element_types[i])) {
				return false;
			}
		}
	}
	if (p_left.type_arguments.size() == p_right.type_arguments.size()) {
		for (int i = 0; i < p_left.type_arguments.size(); i++) {
			if (!_datatype_signature_slot_equal(p_left.type_arguments[i], p_right.type_arguments[i])) {
				return false;
			}
		}
	}
	if (p_left.kind == FSParser::DataType::BUILTIN && _is_signature_builtin_type(p_left.builtin_type) &&
			p_left.has_method_signature && p_right.has_method_signature) {
		return _datatype_method_signature_equal(p_left, p_right);
	}
	return true;
}

// Lenient signature-slot comparison for the non-explicit source path (a lambda, function reference, or
// declared signal where at least one side has no explicit annotation). The historical `MethodInfo`
// fallback compared such slots through `DataType::to_property_info`; reusing that serialization keeps
// every slot byte-for-byte identical to the prior behavior, so type parameters, non-hard
// (inferred/undetected) types, generic type arguments, nullability, and deep container nesting all
// collapse exactly as they did before and nothing the old path accepted is newly tightened. The one
// thing the property form cannot express is a Callable/Signal's nested method signature, so recurse to
// recover exactly that — and because the recursion re-enters `_datatype_method_signature_equal`, a nested
// slot that is itself an explicit Callable/Signal is dispatched to the strict comparison above.
static bool _nonexplicit_signature_slot_equal(const FSParser::DataType &p_left, const FSParser::DataType &p_right) {
	if (!_property_signature_equal(p_left.to_property_info(""), p_right.to_property_info(""))) {
		return false;
	}
	if (p_left.kind == FSParser::DataType::BUILTIN && _is_signature_builtin_type(p_left.builtin_type) &&
			p_left.has_method_signature && p_right.has_method_signature) {
		return _datatype_method_signature_equal(p_left, p_right);
	}
	return true;
}

// Whether a Callable/Signal type carries the rich `method_parameter_types`/`method_return_type`
// recursion data rather than only a `MethodInfo`. Explicit `Callable[[...], ...]` annotations set
// `has_explicit_method_signature`, but lambdas and function references populate the rich vectors from
// their FunctionNode without that flag (see `make_callable_type(MethodInfo, FunctionNode)` in the
// analyzer). A native, MethodInfo-only callable records neither parameter nor return DataTypes, so it
// is distinguished by both rich vectors being empty. A Callable always records its return type (even
// `void`), and a zero-argument callable legitimately has an empty parameter vector, so the presence of
// either rich vector signals that the structural recursion is available.
static bool _has_rich_method_signature(const FSParser::DataType &p_type) {
	if (!p_type.has_method_signature) {
		return false;
	}
	return p_type.has_explicit_method_signature ||
			!p_type.method_parameter_types.is_empty() ||
			!p_type.method_return_type.is_empty() ||
			!p_type.method_rest_parameter_type.is_empty();
}

// Compares the rest tails of two Callable/Signal signatures. In an assignment position `p_left` is the
// target (what callers are promised) and `p_right` the assigned source, so the shared contravariant rest
// rule applies. In an invariant position (a nested container element, for instance) neither side is a
// target and the slots must match exactly.
static bool _method_signature_rest_slots_equal(const FSParser::DataType &p_left, const FSParser::DataType &p_right,
		bool (*p_slot_equal)(const FSParser::DataType &, const FSParser::DataType &), bool p_contravariant_rest,
		bool p_strict_null) {
	if (p_contravariant_rest) {
		FSParser::DataType target_rest;
		FSParser::DataType source_rest;
		const bool target_is_variadic = FSTypeCompatibility::callable_signature_rest_parameter_type(p_left, target_rest);
		const bool source_is_variadic = FSTypeCompatibility::callable_signature_rest_parameter_type(p_right, source_rest);
		return FSTypeCompatibility::rest_parameter_accepts_required_arguments(
				source_is_variadic ? &source_rest : nullptr,
				target_is_variadic ? &target_rest : nullptr, p_strict_null);
	}
	if (p_left.method_rest_parameter_type.size() != p_right.method_rest_parameter_type.size()) {
		return false;
	}
	for (int i = 0; i < p_left.method_rest_parameter_type.size(); i++) {
		if (!p_slot_equal(p_left.method_rest_parameter_type[i], p_right.method_rest_parameter_type[i])) {
			return false;
		}
	}
	return true;
}

static bool _method_signature_slots_equal(const FSParser::DataType &p_left, const FSParser::DataType &p_right,
		bool (*p_slot_equal)(const FSParser::DataType &, const FSParser::DataType &), bool p_contravariant_rest,
		bool p_strict_null) {
	if (p_left.method_parameter_types.size() != p_right.method_parameter_types.size()) {
		return false;
	}
	for (int i = 0; i < p_left.method_parameter_types.size(); i++) {
		if (!p_slot_equal(p_left.method_parameter_types[i], p_right.method_parameter_types[i])) {
			return false;
		}
	}
	if (!_method_signature_rest_slots_equal(p_left, p_right, p_slot_equal, p_contravariant_rest, p_strict_null)) {
		return false;
	}
	if (p_left.builtin_type == Variant::CALLABLE) {
		if (p_left.method_return_type.size() != p_right.method_return_type.size()) {
			return false;
		}
		for (int i = 0; i < p_left.method_return_type.size(); i++) {
			if (!p_slot_equal(p_left.method_return_type[i], p_right.method_return_type[i])) {
				return false;
			}
		}
	}
	return true;
}

static bool _datatype_method_signature_equal(const FSParser::DataType &p_left, const FSParser::DataType &p_right,
		bool p_contravariant_rest, bool p_strict_null) {
	// AsyncCallable and plain Callable are not interchangeable: a callable whose signature is async
	// carries a coroutine result that a synchronous Callable does not, so their signatures differ.
	if (p_left.signature_is_async != p_right.signature_is_async) {
		return false;
	}
	// Both sides written as explicit annotations: compare the rich slots strictly, exactly as the
	// explicit Callable/Signal path always has.
	if (p_left.has_explicit_method_signature && p_right.has_explicit_method_signature) {
		return _method_signature_slots_equal(p_left, p_right, _datatype_signature_slot_equal, p_contravariant_rest, p_strict_null);
	}
	// A lambda/function-reference Callable or a declared Signal carries rich slots without the explicit
	// flag. Compare those slots the way the `MethodInfo` fallback did, but recurse to catch the nested
	// Callable/Signal mismatches the fallback erased — the #382 fix for the non-explicit source path.
	if (_has_rich_method_signature(p_left) && _has_rich_method_signature(p_right)) {
		return _method_signature_slots_equal(p_left, p_right, _nonexplicit_signature_slot_equal, p_contravariant_rest, p_strict_null);
	}
	return _method_signature_equal(p_left.method_info, p_right.method_info);
}

static bool _class_has_trait(const FSParser::ClassNode *p_class, const FSParser::ClassNode *p_trait) {
	if (p_class == nullptr || p_trait == nullptr) {
		return false;
	}

	if (p_class == p_trait || p_class->fqcn == p_trait->fqcn) {
		return true;
	}

	const StringName trait_name = fs_trait_identity_name(p_trait);
	const FSConformanceRegistry *registry = FSConformanceRegistry::get_singleton();
	const FSParser::ClassNode *current = p_class;
	while (current != nullptr) {
		for (const FSParser::ClassNode *trait : current->resolved_traits) {
			if (trait == p_trait || trait->fqcn == p_trait->fqcn) {
				return true;
			}
		}

		// A retroactive conformance (`extend Target uses Trait`) is recorded externally rather than
		// in `resolved_traits`, so consult the conformance registry for this class and each ancestor.
		if (registry->has_conformance(current->fqcn, trait_name) ||
				registry->has_conformance(current->get_global_name(), trait_name)) {
			return true;
		}

		if (current->base_type.kind == FSParser::DataType::CLASS) {
			current = current->base_type.class_type;
		} else if (current->base_type.kind == FSParser::DataType::SCRIPT && current->base_type.script_type.is_valid()) {
			const FoundryScript *foundry_script =
					Object::cast_to<FoundryScript>(
							current->base_type.script_type.ptr());
			if ((foundry_script != nullptr &&
						foundry_script->has_script_trait_parse(
								trait_name)) ||
					(foundry_script == nullptr &&
							current->base_type.script_type
									->has_script_trait(trait_name))) {
				return true;
			}
			return (_path_identifies_script(current->base_type.script_type) &&
						   registry->has_conformance(current->base_type.script_path, trait_name)) ||
					registry->has_conformance(current->base_type.script_type->get_global_name(), trait_name) ||
					registry->native_class_conforms(current->base_type.script_type->get_instance_base_type(), trait_name);
		} else if (current->base_type.kind == FSParser::DataType::NATIVE) {
			// The class chain bottoms out at a native base; a conformance declared on that native class
			// (or any of its ancestors) applies to this Foundry Script class too.
			return registry->native_class_conforms(current->base_type.native_type, trait_name);
		} else {
			break;
		}
	}

	return false;
}

static FSParser::DataType _type_handle_represented_type(const FSParser::DataType &p_type) {
	FSParser::DataType result = p_type;
	result.is_type_handle_annotation = false;
	result.is_meta_type = false;
	result.is_pseudo_type = false;
	result.is_constant = false;
	result.is_nullable = false;
	return result;
}

// The descriptor a native-only 8- or 16-bit width promotes as. Neither has a source spelling, so
// neither can name a result type; their whole range fits the 32-bit descriptor of the same carrier.
static NumericType _promotion_descriptor(NumericType p_numeric_type) {
	switch (p_numeric_type) {
		case NumericType::INT8:
		case NumericType::INT16:
			return NumericType::INT32;
		case NumericType::UINT8:
		case NumericType::UINT16:
			return NumericType::UINT32;
		default:
			return p_numeric_type;
	}
}

bool FSNumericConversion::promote_integer_pair(NumericType p_left, NumericType p_right, NumericType &r_result) {
	if (!numeric_type_is_valid(p_left) || !numeric_type_is_valid(p_right)) {
		return false;
	}

	// An unconstrained side cannot contribute a width, and pinning the other side's width onto the
	// result would claim a constraint the pair never had.
	if (p_left == NumericType::NONE || p_right == NumericType::NONE) {
		r_result = NumericType::NONE;
		return true;
	}

	const NumericType left = _promotion_descriptor(p_left);
	const NumericType right = _promotion_descriptor(p_right);

	if (left == right) {
		r_result = left;
		return true;
	}

	// The matrix is symmetric, so each mixed row is listed once and both orders reach it.
	const NumericType low = MIN(left, right);
	const NumericType high = MAX(left, right);

	if (low == NumericType::INT32 && high == NumericType::INT64) {
		r_result = NumericType::INT64; // int, long -> long
		return true;
	}
	if (low == NumericType::INT32 && high == NumericType::UINT32) {
		r_result = NumericType::INT64; // int, uint -> long
		return true;
	}
	if (low == NumericType::UINT32 && high == NumericType::INT64) {
		r_result = NumericType::INT64; // uint, long -> long
		return true;
	}
	if (low == NumericType::UINT32 && high == NumericType::UINT64) {
		r_result = NumericType::UINT64; // uint, ulong -> ulong
		return true;
	}

	// Everything else -- `int`/`ulong`, `long`/`ulong` -- would have to drop either the signed
	// minimum or the unsigned maximum.
	return false;
}

bool FSNumericConversion::is_numeric_builtin(const FSParser::DataType &p_type) {
	if (p_type.kind != FSParser::DataType::BUILTIN) {
		return false;
	}
	return p_type.builtin_type == Variant::INT || p_type.builtin_type == Variant::UINT ||
			p_type.builtin_type == Variant::FLOAT;
}

// Whether the destination's range covers every value the source can hold. Both descriptors are real
// widths; a carrier crossing is decided by the caller, since the two carriers partition the range.
static bool _numeric_range_covers(NumericType p_target, NumericType p_source) {
	if (numeric_type_carrier(p_target) != numeric_type_carrier(p_source)) {
		return false;
	}
	return numeric_type_minimum(p_source) >= numeric_type_minimum(p_target) &&
			numeric_type_maximum(p_source) <= numeric_type_maximum(p_target);
}

// Whether a constant's exact value could be stored in `p_numeric_type`, regardless of which carrier
// the constant itself was folded into. `numeric_type_contains()` additionally requires the value's
// carrier to already agree with the descriptor, which is exactly right for a dynamic value -- a
// carrier mismatch there proves nothing -- but a constant's magnitude and sign are known outright, so
// design section 6.1 lets it cross to the other carrier when it is representable there: a positive
// `int` constant may enter a `uint`/`ulong` slot, and a `uint` constant within the signed range may
// enter an `int`/`long` slot. A negative constant is never representable in an unsigned slot, and an
// out-of-range magnitude is never representable in either.
static bool _constant_fits_numeric_type(NumericType p_numeric_type, const Variant &p_value) {
	if (p_value.get_type() == Variant::UINT) {
		return p_value.operator uint64_t() <= numeric_type_maximum(p_numeric_type);
	}
	if (p_value.get_type() == Variant::INT) {
		const int64_t value = p_value.operator int64_t();
		if (value < 0) {
			return value >= numeric_type_minimum(p_numeric_type);
		}
		return uint64_t(value) <= numeric_type_maximum(p_numeric_type);
	}
	return false;
}

// Whether the double-backed `float` holds this integer without rounding. Above 2^53 the doubles
// thin out, so the round trip is the proof rather than the width.
static bool _integer_is_exact_as_double(const Variant &p_value) {
	if (p_value.get_type() == Variant::UINT) {
		const uint64_t value = p_value.operator uint64_t();
		const double as_double = double(value);
		return as_double < 18446744073709551616.0 && uint64_t(as_double) == value;
	}
	if (p_value.get_type() == Variant::INT) {
		const int64_t value = p_value.operator int64_t();
		const double as_double = double(value);
		return as_double >= -9223372036854775808.0 && as_double < 9223372036854775808.0 && int64_t(as_double) == value;
	}
	return false;
}

FSNumericConversion::Conversion FSNumericConversion::classify(const FSParser::DataType &p_target, const FSParser::DataType &p_source, const Variant *p_constant_source_value) {
	if (!is_numeric_builtin(p_target) || !is_numeric_builtin(p_source)) {
		return Conversion::INVALID;
	}

	const bool target_is_float = p_target.builtin_type == Variant::FLOAT;
	const bool source_is_float = p_source.builtin_type == Variant::FLOAT;

	if (target_is_float && source_is_float) {
		return Conversion::IDENTITY;
	}

	if (target_is_float) {
		// `int` and `uint` fit the double's exact integer range whole. The 64-bit widths do not, so
		// only a constant whose value survives the round trip may cross without an explicit cast. A
		// source that declared no width states no range, so it keeps its pre-descriptor behavior.
		const NumericType source_numeric_type = p_source.numeric_type;
		if (source_numeric_type == NumericType::NONE) {
			return Conversion::IDENTITY;
		}
		if (numeric_type_bit_width(source_numeric_type) <= 32) {
			return Conversion::IMPLICIT_WIDEN;
		}
		if (p_constant_source_value != nullptr && _integer_is_exact_as_double(*p_constant_source_value)) {
			return Conversion::CONSTANT_CHECKED;
		}
		return Conversion::EXPLICIT_REQUIRED;
	}

	if (source_is_float) {
		// Truncating a float into an integer slot is governed by the existing conversion rules, which
		// no width descriptor refines.
		return Conversion::IDENTITY;
	}

	// Conversion asks about the exact declared ranges, so the descriptors are used as written. The
	// promotion normalization that folds the native-only 8- and 16-bit widths into their 32-bit
	// counterparts belongs to result-type selection alone: applying it here would range-check an
	// 8-bit destination as if it were 32 bits wide.
	const NumericType target_numeric_type = p_target.numeric_type;
	const NumericType source_numeric_type = p_source.numeric_type;
	if (target_numeric_type != NumericType::NONE && source_numeric_type == NumericType::NONE &&
			p_constant_source_value != nullptr) {
		// The source states no width, but the value is known exactly, so the destination's range is
		// still checkable rather than simply unconstrained. Without this, an unsuffixed constant too
		// large for the destination would enter it on the strength of declaring nothing.
		return _constant_fits_numeric_type(target_numeric_type, *p_constant_source_value)
				? Conversion::CONSTANT_CHECKED
				: Conversion::EXPLICIT_REQUIRED;
	}
	if (target_numeric_type == NumericType::NONE || source_numeric_type == NumericType::NONE) {
		// One side constrains no width, so there is no width to preserve or violate.
		return Conversion::IDENTITY;
	}
	if (target_numeric_type == source_numeric_type) {
		return Conversion::IDENTITY;
	}
	if (_numeric_range_covers(target_numeric_type, source_numeric_type)) {
		return Conversion::IMPLICIT_WIDEN;
	}
	if (target_numeric_type == NumericType::INT64 && source_numeric_type == NumericType::UINT32) {
		// The one carrier-crossing widen design section 6.1 lists unconditionally: every `uint` value is
		// representable as a `long`, so this needs no proof the way a constant crossing does.
		// `int`/`long` and `uint`/`ulong` are same-carrier and already handled by `_numeric_range_covers()`
		// above; `uint`/`long` is the only pair whose carriers differ but whose crossing is still total.
		return Conversion::IMPLICIT_WIDEN;
	}
	if (p_constant_source_value != nullptr && _constant_fits_numeric_type(target_numeric_type, *p_constant_source_value)) {
		return Conversion::CONSTANT_CHECKED;
	}
	return Conversion::EXPLICIT_REQUIRED;
}

FSTypeCompatibility::Result FSTypeCompatibility::check(const FSParser::DataType &p_target, const FSParser::DataType &p_source) {
	return check(p_target, p_source, Options());
}

FSTypeCompatibility::Result FSTypeCompatibility::check(const FSParser::DataType &p_target, const FSParser::DataType &p_source, const Options &p_options) {
	Result result;

	// Preserve the current analyzer behavior for parser bugs: don't add user-facing fallout.
	ERR_FAIL_COND_V_MSG(!p_target.is_set(), Result(true, false, false), "Parser bug (please report): Trying to check compatibility of unset target type");
	ERR_FAIL_COND_V_MSG(!p_source.is_set(), Result(true, false, false), "Parser bug (please report): Trying to check compatibility of unset value type");

	if (p_target.kind == FSParser::DataType::VARIANT) {
		result.compatible = true;
		return result;
	}

	if (p_source.kind == FSParser::DataType::VARIANT) {
		result.compatible = !p_options.strict_dynamic;
		result.requires_runtime_check = true;
		return result;
	}

	if (p_options.strict_null && p_source.is_nullable && !p_target.is_nullable) {
		result.compatible = false;
		return result;
	}

	if (p_target.is_nullable && p_source.kind == FSParser::DataType::BUILTIN && p_source.builtin_type == Variant::NIL) {
		// A nullable target accepts null regardless of its underlying kind. This must run before the
		// builtin and enum target branches below, which would otherwise reject null for those kinds.
		result.compatible = true;
		return result;
	}

	if (p_source.kind == FSParser::DataType::UNION) {
		// A value whose static type is a set of alternatives satisfies a concrete target only when every
		// alternative does. The runtime carries no tag, so nothing narrows the value at the boundary and
		// a target that accepts only some alternatives would accept the wrong value at runtime.
		Options member_options = p_options;
		member_options.constant_source_value = nullptr;
		for (const FSParser::DataType &member : p_source.union_members) {
			// Nullability was hoisted onto the union during normalization, so it is put back on each
			// alternative before it faces the target's own null rules.
			FSParser::DataType source_member = member;
			source_member.is_nullable = p_source.is_nullable;
			const Result member_result = check(p_target, source_member, member_options);
			if (!member_result.compatible) {
				return result;
			}
			if (p_target.kind != FSParser::DataType::UNION && member_result.uses_implicit_conversion &&
					(source_member.kind != FSParser::DataType::BUILTIN || p_target.builtin_type != source_member.builtin_type)) {
				// The value is one untyped slot at runtime, so a per-alternative conversion cannot be
				// emitted for it: the compiler sees a single erased source, not this alternative. Accepting
				// an alternative that only reaches the target by changing carrier would leave the target
				// slot holding an unconverted value that fails its own declared type. A width-only
				// conversion is fine, since width is out-of-band metadata and the stored value is unchanged.
				// A union target carries no carrier of its own, so it applies this same rule per
				// alternative in its own branch and this one must not second-guess it against `NIL`.
				return result;
			}
			result.uses_implicit_conversion = result.uses_implicit_conversion || member_result.uses_implicit_conversion;
		}
		result.compatible = true;
		// A union erases to an untyped value, so reaching a typed slot always costs a runtime check.
		result.requires_runtime_check = true;
		return result;
	}

	if (p_target.kind == FSParser::DataType::UNION) {
		// A union target accepts a source that satisfies any one of its alternatives.
		for (const FSParser::DataType &member : p_target.union_members) {
			FSParser::DataType target_member = member;
			target_member.is_nullable = p_target.is_nullable;
			const Result member_result = check(target_member, p_source, p_options);
			if (!member_result.compatible) {
				continue;
			}
			if (member_result.uses_implicit_conversion &&
					(p_source.kind != FSParser::DataType::BUILTIN || target_member.builtin_type != p_source.builtin_type)) {
				// A union slot is untyped at runtime, so no conversion instruction is emitted for it.
				// An alternative reachable only by changing the value's carrier would therefore hold an
				// unconverted value and fail its own type test. A width-only conversion is fine: width is
				// out-of-band metadata and the stored value is unchanged.
				continue;
			}
			result.compatible = true;
			result.uses_implicit_conversion = member_result.uses_implicit_conversion;
			// An alternative that only accepts the source under a runtime check keeps that obligation.
			// The union slot itself emits none, so the caller has to treat the flow as unsafe rather than
			// read a set membership it never proved: an erased type parameter is the case that matters.
			result.requires_runtime_check = member_result.requires_runtime_check;
			return result;
		}
		return result;
	}

	if (p_target.is_type_handle_annotation && p_source.kind == FSParser::DataType::BUILTIN && p_source.builtin_type == Variant::NIL) {
		// Type handles are object-like values at runtime, so null is accepted even without an explicit
		// nullable suffix, matching legacy Object compatibility.
		result.compatible = true;
		return result;
	}

	if (p_target.is_type_handle_annotation) {
		if (!p_source.is_meta_type && !p_source.is_type_handle_annotation) {
			return result;
		}
		return check(_type_handle_represented_type(p_target), _type_handle_represented_type(p_source), p_options);
	}

	if (p_target.kind == FSParser::DataType::TYPE_PARAMETER || p_source.kind == FSParser::DataType::TYPE_PARAMETER) {
		// Type parameters are erased to Variant at runtime, so the two directions are not symmetric.
		if (p_target.kind == FSParser::DataType::TYPE_PARAMETER && p_source.kind == FSParser::DataType::TYPE_PARAMETER) {
			// Two handles are statically compatible only when they denote the same parameter.
			// Nullability is deliberately excluded from this identity comparison so `T` widens to `T?`
			// the same way `Node` widens to `Node?`. The unsafe direction (`T?` into `T`) is already
			// rejected by the strict-null gate above, which runs before this branch, so ignoring
			// nullability here cannot admit an unexpected null.
			FSParser::DataType target_identity = p_target;
			FSParser::DataType source_identity = p_source;
			target_identity.is_nullable = false;
			source_identity.is_nullable = false;
			result.compatible = target_identity == source_identity;
		} else if (_is_undecidable_type_parameter_target(p_target, p_options) && _is_type_parameter_bounded_by_final_class(p_target)) {
			// The parameter denotes exactly its bound, so the assignment is decided against the bound like
			// any other concrete destination.
			return check(p_target.type_parameter_bound[0], p_source, p_options);
		} else if (_is_undecidable_type_parameter_target(p_target, p_options)) {
			// A downcast is licensed by a check the runtime can actually perform: `var n: Node2D = node`
			// is accepted because `Node2D` still exists at run time and the value carries its class. A
			// type-parameter destination has neither half of that. The caller picks `T` and it is erased
			// before the callee runs, so there is nothing to test a value against and no check is emitted
			// for the assignment. Accepting a concrete value here would launder it into a `T` slot
			// untested, so only a value already known to be `T` satisfies one. Flow narrowing refines the
			// value, never the parameter: `if value is int` makes the value an `int` and leaves `T` alone.
			result.compatible = false;
		} else {
			// A type parameter as the source is the downcast shape instead: the destination is a concrete
			// type the runtime can still name, and the erased value carries what a type test needs.
			result.compatible = true;
			result.requires_runtime_check = true;
		}
		return result;
	}

	if (p_target.kind == FSParser::DataType::TUPLE || p_source.kind == FSParser::DataType::TUPLE) {
		// Tuples are their own family. They erase to a read-only Array at runtime, so this branch must
		// run before the builtin branch below, which would otherwise let `Variant::can_convert_strict`
		// silently trade a tuple for a mutable Array and lose the immutability guarantee.
		if (p_target.kind != FSParser::DataType::TUPLE || p_source.kind != FSParser::DataType::TUPLE) {
			return result;
		}
		if (p_target.is_meta_type != p_source.is_meta_type) {
			// A tuple declaration handle is not one of its values.
			return result;
		}
		if (p_target.tuple_name != StringName()) {
			// A named tuple is nominal: only the same declaration satisfies it. Building one from an
			// unnamed tuple (or from a different named tuple) requires explicit construction. The
			// element check keeps two specializations of a generic declaration distinct.
			if (p_target.native_type != p_source.native_type || p_target.script_path != p_source.script_path ||
					p_target.container_element_types.size() != p_source.container_element_types.size()) {
				return result;
			}
			for (int i = 0; i < p_target.container_element_types.size(); i++) {
				if (!_datatype_invariant_equal(p_target.container_element_types[i], p_source.container_element_types[i])) {
					return result;
				}
			}
			result.compatible = true;
			return result;
		}
		// An unnamed target is structural: arity plus invariant elements. A named source erases to it.
		if (p_target.container_element_types.size() != p_source.container_element_types.size()) {
			return result;
		}
		Options element_options = p_options;
		element_options.allow_implicit_conversion = false;
		element_options.constant_source_value = nullptr;
		for (int i = 0; i < p_target.container_element_types.size(); i++) {
			// Elements are invariant in v1, so both directions must hold. Going through `check` (rather
			// than plain equality) keeps a dynamic element flowing with a runtime check instead of
			// rejecting it outright, matching how every other slot treats Variant.
			const Result forward = check(p_target.container_element_types[i], p_source.container_element_types[i], element_options);
			if (!forward.compatible) {
				return result;
			}
			const Result backward = check(p_source.container_element_types[i], p_target.container_element_types[i], element_options);
			if (!backward.compatible) {
				return result;
			}
			result.requires_runtime_check = result.requires_runtime_check || forward.requires_runtime_check;
		}
		result.compatible = true;
		return result;
	}

	if (p_target.kind == FSParser::DataType::BUILTIN) {
		result.compatible = p_source.kind == FSParser::DataType::BUILTIN && p_target.builtin_type == p_source.builtin_type;
		if (!result.compatible && p_options.allow_implicit_conversion) {
			result.compatible = Variant::can_convert_strict(p_source.builtin_type, p_target.builtin_type);
			result.uses_implicit_conversion = result.compatible;
		}

		const bool both_numeric = p_source.kind == FSParser::DataType::BUILTIN &&
				FSNumericConversion::is_numeric_builtin(p_target) && FSNumericConversion::is_numeric_builtin(p_source);
		// Computed once and reused below.
		const FSNumericConversion::Conversion conversion = both_numeric
				? FSNumericConversion::classify(p_target, p_source, p_options.constant_source_value)
				: FSNumericConversion::Conversion::INVALID;
		// Both carriers are integer: `int`/`uint` crossing to `long`/`ulong` (design section 6.1). The
		// floating side is excluded here because it needs no constant carve-out of its own:
		// `Variant::can_convert_strict()` above already answers unconditionally for every integer
		// carrier reaching `float` (`int`/`uint` because every 32-bit value is exactly representable,
		// `long`/`ulong` because the existing `int64_t` -> `double` conversion is unconditional too), and
		// the `both_numeric` block below narrows that down to the widths design section 6.1 actually
		// allows implicitly, still permitting a `long`/`ulong` constant that survives the round trip.
		const bool both_integer_carriers = both_numeric && p_target.builtin_type != Variant::FLOAT && p_source.builtin_type != Variant::FLOAT;
		if (!result.compatible && p_options.allow_implicit_conversion && both_integer_carriers &&
				(conversion == FSNumericConversion::Conversion::CONSTANT_CHECKED ||
						conversion == FSNumericConversion::Conversion::IMPLICIT_WIDEN)) {
			// `Variant::can_convert_strict()` above has no unconditional answer for `int`/`uint` (or
			// `long`/`ulong`): most values of one carrier are not representable on the other. A constant
			// is different -- its exact value is known -- which is what design section 6.1 permits. A
			// `uint` source widening to `long` is different again: `classify()` only reaches
			// `IMPLICIT_WIDEN` for a carrier crossing when every value of the source is representable in
			// the target regardless of which value it holds, so no constant is needed to prove it.
			result.compatible = true;
			result.uses_implicit_conversion = true;
		}
		if (result.compatible && both_numeric) {
			// Width is part of the target's contract, so every numeric boundary -- assignment,
			// argument, return, signal emission, typed collection element -- asks the same classifier
			// whether the value may cross. A conversion that needs proof is rejected unless the source
			// is a constant whose exact value the destination is known to hold.
			//
			// A widening or a proven constant is still a conversion, so it is only available where a
			// conversion is. An invariant position -- a typed container element, a generic argument, an
			// override's signature -- asks with conversions disabled and therefore requires the exact
			// width, which is what keeps `Array[int]` and `Array[long]` distinct.
			const bool conversion_allowed = conversion == FSNumericConversion::Conversion::IDENTITY ||
					(p_options.allow_implicit_conversion &&
							(conversion == FSNumericConversion::Conversion::IMPLICIT_WIDEN ||
									conversion == FSNumericConversion::Conversion::CONSTANT_CHECKED));
			if (!conversion_allowed) {
				result.compatible = false;
				result.uses_implicit_conversion = false;
			}
		}
		if (!result.compatible && (p_target.builtin_type == Variant::INT || p_target.builtin_type == Variant::UINT) &&
				p_source.kind == FSParser::DataType::ENUM && !p_source.is_meta_type && !p_source.is_tagged_union) {
			// An int-backed enum value is also an integer, on either carrier: native flag parameters are
			// routinely unsigned while the flag enum that names their values is int-backed, and the
			// binding layer reads both carriers for an integer parameter. A tagged-union value is a
			// read-only `[tag, payload...]` Array, so it is deliberately not integer-compatible.
			result.compatible = true;
		}
		if (result.compatible && p_source.kind == FSParser::DataType::BUILTIN && p_target.builtin_type == p_source.builtin_type && _is_signature_builtin_type(p_target.builtin_type)) {
			if (p_target.has_method_signature && p_source.has_method_signature) {
				// Assignment position: the target states what callers may pass, so its rest tail is
				// contravariant while every other slot stays invariant.
				result.compatible = _datatype_method_signature_equal(p_target, p_source, true, p_options.strict_null);
			} else if (p_target.has_method_signature && !p_source.has_method_signature) {
				result.requires_runtime_check = true;
			}
			// Enforce the async marker even when only one side carries a method signature, so a bare
			// `AsyncCallable` is still distinct from a bare `Callable`. An async target requires an
			// async source, and a synchronous target that carries a signature rejects an async source.
			// A bare, signatureless synchronous `Callable` target still accepts any callable (e.g. the
			// `Callable` parameter of `Signal.connect`), so async-ness is only enforced when the target
			// is itself async or carries an explicit signature.
			if (result.compatible && p_target.builtin_type == Variant::CALLABLE &&
					p_target.signature_is_async != p_source.signature_is_async &&
					(p_target.signature_is_async || p_target.has_method_signature)) {
				result.compatible = false;
			}
		}
		if (result.compatible && p_target.builtin_type == Variant::ARRAY && p_source.builtin_type == Variant::ARRAY) {
			if (p_target.has_container_element_type(0) && p_source.has_container_element_type(0)) {
				Options element_options = p_options;
				element_options.allow_implicit_conversion = false;
				element_options.constant_source_value = nullptr;
				const Result element_result = check(p_target.get_container_element_type(0), p_source.get_container_element_type(0), element_options);
				result.compatible = element_result.compatible;
				result.requires_runtime_check = result.requires_runtime_check || element_result.requires_runtime_check;
				result.uses_implicit_conversion = result.uses_implicit_conversion || element_result.uses_implicit_conversion;
			}
		}
		if (result.compatible && p_target.builtin_type == Variant::DICTIONARY && p_source.builtin_type == Variant::DICTIONARY) {
			Options element_options = p_options;
			element_options.allow_implicit_conversion = false;
			element_options.constant_source_value = nullptr;
			if (p_target.has_container_element_type(0) && p_source.has_container_element_type(0)) {
				const Result key_result = check(p_target.get_container_element_type(0), p_source.get_container_element_type(0), element_options);
				result.compatible = key_result.compatible;
				result.requires_runtime_check = result.requires_runtime_check || key_result.requires_runtime_check;
				result.uses_implicit_conversion = result.uses_implicit_conversion || key_result.uses_implicit_conversion;
			}
			if (result.compatible && p_target.has_container_element_type(1) && p_source.has_container_element_type(1)) {
				const Result value_result = check(p_target.get_container_element_type(1), p_source.get_container_element_type(1), element_options);
				result.compatible = value_result.compatible;
				result.requires_runtime_check = result.requires_runtime_check || value_result.requires_runtime_check;
				result.uses_implicit_conversion = result.uses_implicit_conversion || value_result.uses_implicit_conversion;
			}
		}
		return result;
	}

	if (p_target.kind == FSParser::DataType::ENUM) {
		if (p_source.kind == FSParser::DataType::BUILTIN && p_source.builtin_type == Variant::INT &&
				!p_target.is_tagged_union) {
			// An int can stand in for an int-backed enum value, but never for a tagged-union value:
			// no integer carries a case tag plus its payload.
			result.compatible = true;
			return result;
		}
		if (p_source.kind == FSParser::DataType::ENUM && p_source.native_type == p_target.native_type) {
			// Nominal identity is not enough for a generic tagged union: its type arguments are invariant,
			// so `Result[int, String]` and `Result[float, String]` are unrelated types even though `int`
			// converts to `float`. Matching argument-by-argument here is what keeps a payload read through
			// one application from seeing another application's payload types.
			if (p_source.type_arguments.size() != p_target.type_arguments.size()) {
				return result;
			}
			for (int i = 0; i < p_target.type_arguments.size(); i++) {
				if (!_datatype_invariant_equal(p_target.type_arguments[i], p_source.type_arguments[i])) {
					return result;
				}
			}
			result.compatible = true;
			return result;
		}
		return result;
	}

	if (p_source.kind == FSParser::DataType::BUILTIN && p_source.builtin_type == Variant::NIL) {
		// null is acceptable in object types in the legacy compatibility rules.
		result.compatible = !p_options.strict_null || p_target.is_nullable;
		return result;
	}

	if (p_target.is_coroutine || p_source.is_coroutine) {
		// Coroutine[T] is its own family: a coroutine target requires a coroutine source (and vice
		// versa), and the phantom result type is matched invariantly, like a typed Array element.
		// Generic NATIVE inheritance (FSFunctionState) must not be used here, so this branch
		// runs before the native compatibility logic below.
		if (!p_target.is_coroutine || !p_source.is_coroutine) {
			return result;
		}
		result.compatible = true;
		if (p_target.has_container_element_type(0) && p_source.has_container_element_type(0)) {
			Options element_options = p_options;
			element_options.allow_implicit_conversion = false;
			element_options.constant_source_value = nullptr;
			const Result element_result = check(p_target.get_container_element_type(0), p_source.get_container_element_type(0), element_options);
			result.compatible = element_result.compatible;
			result.requires_runtime_check = element_result.requires_runtime_check;
			result.uses_implicit_conversion = element_result.uses_implicit_conversion;
		}
		return result;
	}

	if (p_target.kind == FSParser::DataType::CLASS && p_target.class_type != nullptr &&
			p_target.class_type->is_trait && !p_target.is_meta_type) {
		if (p_source.kind == FSParser::DataType::CLASS && !p_source.is_meta_type) {
			result.compatible = _class_has_trait(p_source.class_type, p_target.class_type);
			return result;
		}
		if (p_source.kind == FSParser::DataType::SCRIPT && p_source.script_type.is_valid() && !p_source.is_meta_type) {
			const StringName trait_name = fs_trait_identity_name(p_target.class_type);
			const FoundryScript *foundry_script =
					Object::cast_to<FoundryScript>(
							p_source.script_type.ptr());
			result.compatible =
					foundry_script != nullptr
					? foundry_script->has_script_trait_parse(
							  trait_name)
					: p_source.script_type->has_script_trait(
							  trait_name);
			if (!result.compatible) {
				// A retroactively-conformed script type carries its conformance in the registry, not in
				// the compiled script's own trait set. A conformance declared on the script's native base
				// class (`extend Node uses ...`) also applies to any script extending that class.
				const FSConformanceRegistry *registry = FSConformanceRegistry::get_singleton();
				result.compatible = (_path_identifies_script(p_source.script_type) &&
											registry->has_conformance(p_source.script_path, trait_name)) ||
						registry->has_conformance(p_source.script_type->get_global_name(), trait_name) ||
						registry->native_class_conforms(p_source.script_type->get_instance_base_type(), trait_name);
			}
			return result;
		}
		if (p_source.kind == FSParser::DataType::NATIVE && !p_source.is_meta_type) {
			// A native value satisfies a trait target when its class (or any ancestor) was retroactively
			// conformed via `extend <native class> uses Trait`.
			const StringName trait_name = fs_trait_identity_name(p_target.class_type);
			const FSConformanceRegistry *registry = FSConformanceRegistry::get_singleton();
			result.compatible = registry->native_class_conforms(p_source.native_type, trait_name);
			return result;
		}
		if (p_source.kind == FSParser::DataType::BUILTIN && !p_source.is_meta_type) {
			const StringName trait_name = fs_trait_identity_name(p_target.class_type);
			const FSConformanceRegistry *registry = FSConformanceRegistry::get_singleton();
			result.compatible = registry->builtin_type_conforms(p_source.builtin_type, trait_name);
			return result;
		}
		return result;
	}

	StringName src_native;
	Ref<Script> src_script;
	const FSParser::ClassNode *src_class = nullptr;

	switch (p_source.kind) {
		case FSParser::DataType::NATIVE:
			if (p_target.kind != FSParser::DataType::NATIVE) {
				return result;
			}
			if (p_source.is_meta_type) {
				src_native = FSNativeClass::get_class_static();
			} else {
				src_native = p_source.native_type;
			}
			break;
		case FSParser::DataType::SCRIPT:
			if (p_target.kind == FSParser::DataType::CLASS) {
				return result;
			}
			if (p_source.script_type.is_null()) {
				return result;
			}
			if (p_source.is_meta_type) {
				src_native = p_source.script_type->get_class_name();
			} else {
				src_script = p_source.script_type;
				src_native = src_script->get_instance_base_type();
			}
			break;
		case FSParser::DataType::CLASS:
			if (p_source.is_meta_type) {
				src_native = FoundryScript::get_class_static();
			} else {
				src_class = p_source.class_type;
				const FSParser::ClassNode *base = src_class;
				while (base->base_type.kind == FSParser::DataType::CLASS) {
					base = base->base_type.class_type;
				}
				src_native = base->base_type.native_type;
				src_script = base->base_type.script_type;
			}
			break;
		case FSParser::DataType::TUPLE:
		case FSParser::DataType::UNION:
		case FSParser::DataType::TYPE_PARAMETER:
		case FSParser::DataType::VARIANT:
		case FSParser::DataType::BUILTIN:
		case FSParser::DataType::ENUM:
		case FSParser::DataType::RESOLVING:
		case FSParser::DataType::UNRESOLVED:
			break;
	}

	switch (p_target.kind) {
		case FSParser::DataType::NATIVE:
			if (p_target.is_meta_type) {
				result.compatible = ClassDB::is_parent_class(src_native, FSNativeClass::get_class_static());
			} else {
				result.compatible = ClassDB::is_parent_class(src_native, p_target.native_type);
			}
			return result;
		case FSParser::DataType::SCRIPT:
			if (p_target.is_meta_type) {
				result.compatible = ClassDB::is_parent_class(src_native, p_target.script_type->get_class_name());
				return result;
			}
			while (src_script.is_valid()) {
				if (src_script == p_target.script_type) {
					result.compatible = true;
					return result;
				}
				src_script = src_script->get_base_script();
			}
			return result;
		case FSParser::DataType::CLASS:
			if (p_target.is_meta_type) {
				result.compatible = ClassDB::is_parent_class(src_native, FoundryScript::get_class_static());
				return result;
			}
			if (p_source.is_meta_type) {
				// A class handle denotes the class; it is not an instance of it. The inheritance walk
				// below compares `class_type` identity, which a handle for the same class would satisfy.
				return result;
			}
			{
				// Walk the source's inheritance chain, carrying its type arguments down each level so a
				// specialized handle is compared invariantly against the target at the matching class:
				// `Stack[String] extends List[U]` reaches `List` as `List[String]`, which is not `List[int]`.
				FSParser::DataType current = p_source;
				while (current.class_type != nullptr) {
					if (current.class_type == p_target.class_type || current.class_type->fqcn == p_target.class_type->fqcn) {
						// Specialized generic handles are invariant in their type arguments: a `Box[int]`
						// is not a `Box[String]`. A bare (unspecialized) source stays compatible.
						if (p_target.has_type_arguments() && current.has_type_arguments()) {
							if (current.type_arguments.size() != p_target.type_arguments.size()) {
								result.compatible = false;
							} else {
								result.compatible = true;
								for (int i = 0; i < current.type_arguments.size(); i++) {
									if (!_datatype_invariant_equal(current.type_arguments[i], p_target.type_arguments[i])) {
										result.compatible = false;
										break;
									}
								}
							}
						} else {
							result.compatible = true;
						}
						return result;
					}

					// Step to the base, substituting this level's type arguments into the base handle so
					// the next comparison sees concrete arguments.
					FSParser::DataType parent = current.class_type->base_type;
					if (current.has_type_arguments() && !current.class_type->type_parameters.is_empty()) {
						const Vector<FSParser::TypeParameterNode *> &type_parameters = current.class_type->type_parameters;
						HashMap<StringName, FSParser::DataType> bindings;
						const int binding_count = MIN(type_parameters.size(), current.type_arguments.size());
						for (int i = 0; i < binding_count; i++) {
							const FSParser::TypeParameterNode *parameter = type_parameters[i];
							if (parameter != nullptr && parameter->identifier != nullptr) {
								bindings.insert(parameter->identifier->name, current.type_arguments[i]);
							}
						}
						if (!bindings.is_empty()) {
							parent = FSParser::DataType::substitute(parent, bindings);
						}
					}
					current = parent;
				}
				return result;
			}
		case FSParser::DataType::TUPLE:
		case FSParser::DataType::UNION:
		case FSParser::DataType::TYPE_PARAMETER:
		case FSParser::DataType::VARIANT:
		case FSParser::DataType::BUILTIN:
		case FSParser::DataType::ENUM:
		case FSParser::DataType::RESOLVING:
		case FSParser::DataType::UNRESOLVED:
			break;
	}

	return result;
}

bool FSTypeCompatibility::is_compatible(const FSParser::DataType &p_target, const FSParser::DataType &p_source, bool p_allow_implicit_conversion) {
	Options options;
	options.allow_implicit_conversion = p_allow_implicit_conversion;
	return check(p_target, p_source, options).compatible;
}

bool FSTypeCompatibility::is_invariant_equal(const FSParser::DataType &p_a, const FSParser::DataType &p_b) {
	return p_a == p_b && _datatype_invariant_equal(p_a, p_b);
}

bool FSTypeCompatibility::rest_parameter_type_is_narrowing(const FSParser::DataType &p_rest_parameter_type) {
	return p_rest_parameter_type.kind == FSParser::DataType::BUILTIN &&
			p_rest_parameter_type.builtin_type == Variant::ARRAY &&
			p_rest_parameter_type.has_container_element_type(0) &&
			!p_rest_parameter_type.get_container_element_type(0).is_variant();
}

bool FSTypeCompatibility::rest_parameter_accepts_required_arguments(
		const FSParser::DataType *p_implementation_rest_array,
		const FSParser::DataType *p_required_rest_array, bool p_strict_null) {
	if (p_required_rest_array == nullptr) {
		// The requirement promises no trailing arguments, so there is nothing the implementation must
		// accept. Whether it may declare a rest tail of its own is an arity question.
		return true;
	}
	if (p_implementation_rest_array == nullptr) {
		// The requirement's arity interval is unreachable without a rest tail.
		return false;
	}
	if (!rest_parameter_type_is_narrowing(*p_implementation_rest_array)) {
		// A gradual implementation tail accepts every trailing argument the requirement allows.
		return true;
	}
	if (!rest_parameter_type_is_narrowing(*p_required_rest_array)) {
		// Callers of the gradual requirement may pass any value, which a typed tail would reject.
		return false;
	}
	Options element_options;
	element_options.strict_null = p_strict_null;
	return check(p_implementation_rest_array->get_container_element_type(0),
			p_required_rest_array->get_container_element_type(0), element_options)
			.compatible;
}

bool FSTypeCompatibility::rest_parameter_accepts_required_argument(
		const FSParser::DataType *p_implementation_rest_array,
		const FSParser::DataType &p_required_argument_type, bool p_strict_null) {
	if (p_implementation_rest_array == nullptr) {
		return false;
	}
	if (!rest_parameter_type_is_narrowing(*p_implementation_rest_array)) {
		return true;
	}
	if (p_required_argument_type.is_variant() && p_required_argument_type.is_hard_type()) {
		// Same exception the fixed-parameter contravariance uses: a hard `Variant` promises callers may
		// pass anything, which a narrowing tail would reject. `is_compatible()` would say yes because
		// one of its operands is `Variant`.
		return false;
	}
	if (!p_required_argument_type.is_set()) {
		// Still resolving; an unset type is treated as compatible everywhere else too.
		return true;
	}
	Options element_options;
	element_options.strict_null = p_strict_null;
	return check(p_implementation_rest_array->get_container_element_type(0), p_required_argument_type, element_options)
			.compatible;
}

bool FSTypeCompatibility::callable_signature_rest_parameter_type(const FSParser::DataType &p_signature, FSParser::DataType &r_rest_array) {
	if (!(p_signature.method_info.flags & METHOD_FLAG_VARARG)) {
		return false;
	}
	if (p_signature.has_method_rest_parameter_type()) {
		r_rest_array = p_signature.get_method_rest_parameter_type();
	} else {
		r_rest_array = FSParser::DataType();
		r_rest_array.type_source = FSParser::DataType::ANNOTATED_EXPLICIT;
		r_rest_array.kind = FSParser::DataType::BUILTIN;
		r_rest_array.builtin_type = Variant::ARRAY;
	}
	return true;
}

// `p_exempt_final_bound` distinguishes the two questions this traversal answers. Code generation and
// the check-emission rules ask "would a store here be validated against the receiver", where a final
// bound changes nothing: the slot is still reified and still checked. The undecidability rule asks
// "is there anything to decide", and a parameter bounded by a `final` class denotes exactly that
// bound, so it is decidable without a receiver -- which is why `FSTypeCompatibility::check()` settles
// such a destination against the bound in a static frame rather than refusing it.
//
// `p_nullable_is_expressible` says whether a nullable node at this position can still be evidence, and
// is true only on the tuple spine: a tuple destination's own root, its elements, and recursively the
// elements of a nested tuple element. There the shape travels as an `FSDataType` all the way to the
// compiled descriptor, which keeps a tuple element's `is_nullable` on purpose (see
// `fs_byte_codegen.cpp:299-308`), so "this type or null" is expressible. Everywhere else -- through a
// non-tuple node's `container_element_types`, or through any `type_arguments` -- the shape becomes a
// `ContainerType`, which has no such field, and evidence built for a nullable node there would reject
// the nulls the destination legitimately admits. The flag is sticky-off, never sticky-on: a tuple
// nested under a typed container or a type argument is analyzer-only, so traversal never re-enters the
// expressible world once it has left it.
//
// The compiler's `_type_depends_on_declared_type_parameters()` in `fs_compiler.cpp` answers the same
// question, and the two must stay in agreement: a "yes" there emits a check, and a "yes" here is what
// makes an enclosing lambda capture its receiver, so a disagreement is either a silently missing check
// or a capture taken for a check that never happens.
static bool _depends_on_receiver_type_parameter(
		const FSParser::DataType &p_type, bool p_nullable_is_expressible, int p_depth, bool p_exempt_final_bound = false) {
	if (unlikely(p_depth > Variant::MAX_RECURSION_DEPTH)) {
		return false;
	}
	if (p_depth == 0 && p_type.is_meta_type && !p_type.is_type_handle_annotation) {
		// A bare specialized meta-type destination (`var handle := Holder[T]`) holds a class handle, not
		// an instance. Its runtime value is a distinct handle object, while the container type a check
		// would be built from describes the class itself, so the check would reject the very handle the
		// declaration is for. A `Type[...]` destination is different and stays included: its handle layer
		// travels beside the shape, which is exactly what makes it checkable.
		return false;
	}
	if (p_type.is_nullable && !p_nullable_is_expressible) {
		// Off the tuple spine a nullable node admits null, which no container type can express, so the
		// runtime deliberately keeps no evidence for it or anything below it. Answering yes for such a
		// slot would only make an enclosing lambda capture its receiver for a check that never happens,
		// which costs a reference cycle and buys nothing.
		return false;
	}
	if (p_type.kind == FSParser::DataType::TYPE_PARAMETER) {
		if (p_exempt_final_bound && _is_type_parameter_bounded_by_final_class(p_type)) {
			// The exemption is withdrawn wherever the resolved bound stops surviving lowering, which the
			// caller reports by clearing the flag on the way down.
			return false;
		}
		// `@Self` is scoped to the class but is not reified per instance: it denotes the class the frame
		// runs against, which a static frame has as well, and it lowers to a concrete script rather than
		// to an erased slot.
		return p_type.type_parameter_scope == FSParser::DataType::TYPE_PARAMETER_CLASS &&
				p_type.type_parameter_name != SNAME("@Self");
	}
	// Typed-container elements -- which is also where a tuple keeps its positional element types -- and
	// the type arguments of a specialized class handle are all reified at run time: the container from
	// its element metadata, the handle from what its construction recorded on the instance. A parameter
	// in any of those positions is therefore decidable against a receiver, and a `(int, T)` slot is
	// reported exactly as an `Array[T]` one is.
	//
	// A callable/signal signature slot and a union member are not, and deliberately stay out: a
	// signature erases completely at run time and a union erases to one untyped slot, so a check
	// emitted for either would assert nothing while claiming to enforce the parameter. That is the
	// opposite reading from `_destination_has_erased_type_parameter()`, which walks both because it
	// answers "is this undecidable", where erasure is the reason to say yes.
	const bool child_expressible = p_nullable_is_expressible && p_type.kind == FSParser::DataType::TUPLE;
	// A tuple reached from a typed container or a type argument is analyzer-only: the container descriptor
	// keeps a bare Array, so a final bound named inside it resolves to nothing the runtime states and the
	// exemption must not travel there.
	const bool crossing_into_container = p_type.kind != FSParser::DataType::TUPLE;
	for (const FSParser::DataType &element_type : p_type.container_element_types) {
		const bool child_exempt = p_exempt_final_bound &&
				!(crossing_into_container && element_type.kind == FSParser::DataType::TUPLE);
		if (_depends_on_receiver_type_parameter(element_type, child_expressible, p_depth + 1, child_exempt)) {
			return true;
		}
	}
	for (const FSParser::DataType &type_argument : p_type.type_arguments) {
		const bool child_exempt = p_exempt_final_bound && type_argument.kind != FSParser::DataType::TUPLE;
		if (_depends_on_receiver_type_parameter(type_argument, false, p_depth + 1, child_exempt)) {
			return true;
		}
	}
	return false;
}

// A tuple destination carries its shape to run time as a compiled descriptor, which is the one place a
// nullable node stays evidence; every other declared shape lowers to a container type that cannot
// express "or null".
static bool _nullable_is_expressible_at_root(const FSParser::DataType &p_type) {
	return p_type.kind == FSParser::DataType::TUPLE;
}

bool FSTypeCompatibility::destination_depends_on_receiver_type_parameter(const FSParser::DataType &p_type) {
	return _depends_on_receiver_type_parameter(p_type, _nullable_is_expressible_at_root(p_type), 0);
}

// Two disjoint reasons make a destination undecidable, and the answer is the union of both because
// each names a slot the compiler emits no check for:
//
//   * The shape names a method-scope parameter anywhere. Such a parameter is chosen per call and
//     erased before the callee runs, so no frame -- static or instance -- has anything to check
//     against. This half does not depend on the frame and is asked unconditionally.
//   * The frame has no receiver and the shape names a class-scope parameter in a position a store
//     would have been validated against that receiver. With a receiver those positions are checked;
//     without one the same slot is exactly as undecidable as a method-scope parameter.
//
// The second half is asked of `_depends_on_receiver_type_parameter()`, the traversal code generation
// itself uses, rather than of the erasure walk, so a "yes" always names a slot that would have been
// checked had a receiver existed. The two traversals differ: a nullable node off the tuple spine, a
// bare specialized meta-type destination, a callable-signature slot and a union member are unchecked
// with a receiver as well, so answering "undecidable" for them only in a static frame would make the
// static and the instance frame disagree about the same declaration. Closing those shapes is the
// business of the issues that own them, and it must close them for every frame at once -- as the
// nullable tuple element already was, which is why it is reported here too. A leaf bounded by a `final`
// class is exempt from both halves wherever compiler lowering can still state the bound it resolves to:
// it denotes exactly that bound, which `check()` settles a static-frame destination against instead of
// refusing it, and lowering turns the slot into a genuinely checked one. Where a wrapper discards the
// resolved bound -- a callable signature, a union member, a nullable node off the tuple spine, a tuple
// under a typed container -- the exemption is withdrawn and the gradual flow is refused, because
// claiming a check the compiled slot does not perform is what made the exemption unsound.
bool FSTypeCompatibility::destination_is_undecidable_type_parameter(const FSParser::DataType &p_type, const Options &p_options) {
	if (_destination_has_erased_type_parameter(p_type)) {
		return true;
	}
	return !p_options.receiver_is_available &&
			_depends_on_receiver_type_parameter(p_type, _nullable_is_expressible_at_root(p_type), 0, true);
}

bool FSTypeCompatibility::allows_runtime_narrowing(const FSParser::DataType &p_narrow, const FSParser::DataType &p_wide) {
	if (p_narrow.kind == FSParser::DataType::TUPLE || p_wide.kind == FSParser::DataType::TUPLE) {
		return false;
	}
	if (p_narrow.kind == FSParser::DataType::BUILTIN && p_wide.kind == FSParser::DataType::BUILTIN &&
			p_narrow.builtin_type == p_wide.builtin_type && _is_signature_builtin_type(p_narrow.builtin_type) &&
			p_narrow.has_method_signature && p_wide.has_method_signature) {
		// A Callable/Signal erases its signature at runtime, so nothing distinguishes two signatures
		// there. Every other signature slot is compared symmetrically, so this only ever mattered once
		// rest tails became contravariant: the unsafe direction must stay a static error rather than
		// become a "runtime check" the runtime cannot perform.
		return false;
	}
	if (destination_is_undecidable_type_parameter(p_narrow, Options())) {
		// The reverse-compatibility rule below reads an assignment as a downcast the runtime will check.
		// An erased type-parameter destination has no runtime type to check against and gets no emitted
		// check at all, so nothing backs that reading and the assignment must stay a static error. Same
		// reasoning as the erased Callable signature and the numeric width below: an allowance that
		// claims a runtime check must name one the runtime can actually perform. A parameter buried in a
		// container or signature slot is no more decidable than a bare one, so the whole shape is walked.
		// A default-constructed `Options` is deliberate: this rule is consulted from frames of both kinds,
		// and a static frame's own missing receiver is judged by its caller before narrowing is offered.
		return false;
	}
	if (FSNumericConversion::is_numeric_builtin(p_narrow) && FSNumericConversion::is_numeric_builtin(p_wide)) {
		// A Variant carries the integer carrier, not the declared width, so nothing at the destination
		// can verify at run time that the value really fits the narrower type. Same reasoning as the
		// erased Callable signature above: a conversion the runtime cannot check must stay a static
		// error rather than become an unsafe-but-allowed assignment.
		return false;
	}
	if (p_wide.kind == FSParser::DataType::UNION && FSNumericConversion::is_numeric_builtin(p_narrow)) {
		// A union erases to one untyped slot, so the runtime check behind a downcast sees only the stored
		// value. A declared integer width is not part of that value, so an alternative the destination
		// cannot represent would be laundered into the slot untested -- the same conversion the concrete
		// wide-to-narrow case above refuses. Each numeric alternative is judged by the rule that governs
		// the concrete assignment it stands for, implicit conversions included, so a widening alternative
		// such as `int` into `long` still passes. Alternatives the runtime can still tell apart, a class
		// downcast in particular, keep going through the reverse-compatibility rule below.
		for (const FSParser::DataType &member : p_wide.union_members) {
			if (FSNumericConversion::is_numeric_builtin(member) && !is_compatible(p_narrow, member, true)) {
				return false;
			}
		}
	}
	return is_compatible(p_wide, p_narrow);
}

static bool _datatype_invariant_equal(const FSParser::DataType &p_a, const FSParser::DataType &p_b) {
	if (p_a.kind != p_b.kind ||
			p_a.is_nullable != p_b.is_nullable ||
			p_a.is_meta_type != p_b.is_meta_type ||
			p_a.is_type_handle_annotation != p_b.is_type_handle_annotation ||
			p_a.has_method_signature != p_b.has_method_signature ||
			p_a.signature_is_async != p_b.signature_is_async ||
			p_a.container_element_types.size() != p_b.container_element_types.size() ||
			p_a.type_arguments.size() != p_b.type_arguments.size() ||
			p_a.method_parameter_types.size() != p_b.method_parameter_types.size() ||
			p_a.method_return_type.size() != p_b.method_return_type.size() ||
			p_a.method_rest_parameter_type.size() != p_b.method_rest_parameter_type.size() ||
			p_a.type_parameter_bound.size() != p_b.type_parameter_bound.size()) {
		return false;
	}

	bool equal = false;
	switch (p_a.kind) {
		case FSParser::DataType::VARIANT:
			equal = true;
			break;
		case FSParser::DataType::BUILTIN:
			// Two declared widths are different types even on one carrier, so a typed container or
			// generic argument of `int` is not identical to one of `long`. A slot that declared no width
			// carries no evidence either way and stays identical to both.
			equal = p_a.builtin_type == p_b.builtin_type && numeric_types_agree(p_a.numeric_type, p_b.numeric_type);
			break;
		case FSParser::DataType::NATIVE:
		case FSParser::DataType::ENUM:
			equal = p_a.native_type == p_b.native_type;
			break;
		case FSParser::DataType::SCRIPT:
			equal = p_a.script_type == p_b.script_type;
			break;
		case FSParser::DataType::CLASS:
			equal = p_a.class_type == p_b.class_type ||
					(p_a.class_type != nullptr && p_b.class_type != nullptr &&
							p_a.class_type->fqcn == p_b.class_type->fqcn);
			break;
		case FSParser::DataType::TYPE_PARAMETER:
			equal = p_a.type_parameter_name == p_b.type_parameter_name &&
					p_a.type_parameter_scope == p_b.type_parameter_scope &&
					p_a.type_parameter_index == p_b.type_parameter_index;
			break;
		case FSParser::DataType::TUPLE:
			equal = p_a.native_type == p_b.native_type && p_a.script_path == p_b.script_path;
			break;
		case FSParser::DataType::UNION:
			// Members are canonically ordered, so identity is positional.
			equal = p_a.union_members == p_b.union_members;
			break;
		case FSParser::DataType::RESOLVING:
		case FSParser::DataType::UNRESOLVED:
			break;
	}
	if (!equal) {
		return false;
	}

	for (int i = 0; i < p_a.type_parameter_bound.size(); i++) {
		if (!_datatype_invariant_equal(p_a.type_parameter_bound[i], p_b.type_parameter_bound[i])) {
			return false;
		}
	}
	for (int i = 0; i < p_a.container_element_types.size(); i++) {
		if (!_datatype_invariant_equal(p_a.container_element_types[i], p_b.container_element_types[i])) {
			return false;
		}
	}
	for (int i = 0; i < p_a.type_arguments.size(); i++) {
		if (!_datatype_invariant_equal(p_a.type_arguments[i], p_b.type_arguments[i])) {
			return false;
		}
	}
	for (int i = 0; i < p_a.method_parameter_types.size(); i++) {
		if (!_datatype_invariant_equal(p_a.method_parameter_types[i], p_b.method_parameter_types[i])) {
			return false;
		}
	}
	for (int i = 0; i < p_a.method_return_type.size(); i++) {
		if (!_datatype_invariant_equal(p_a.method_return_type[i], p_b.method_return_type[i])) {
			return false;
		}
	}
	for (int i = 0; i < p_a.method_rest_parameter_type.size(); i++) {
		if (!_datatype_invariant_equal(p_a.method_rest_parameter_type[i], p_b.method_rest_parameter_type[i])) {
			return false;
		}
	}
	return true;
}
