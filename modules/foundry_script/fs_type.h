/**************************************************************************/
/*  fs_type.h                                                             */
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

#include "fs_parser.h"

// The one decision point for what an integer width means when two of them meet.
//
// Two questions have to be answered the same way everywhere, or the language grows corners: which
// type an operator's operands agree on, and whether a value of one width may enter a slot of
// another. Both are answered here so that a binary operation, an assignment, an argument, a return,
// a signal emission, and a typed collection element cannot drift apart.
class FSNumericConversion {
public:
	// What it costs to move a value of one numeric type into a slot of another.
	enum class Conversion {
		// The slot constrains nothing the value does not already satisfy.
		IDENTITY,
		// Every value of the source is representable in the destination, so the conversion is safe
		// without inspecting the value.
		IMPLICIT_WIDEN,
		// Not safe in general, but the analyzer proved this particular constant is representable.
		CONSTANT_CHECKED,
		// A representable value exists, but proving it needs an explicit conversion at the call site.
		EXPLICIT_REQUIRED,
		// The two slots are not numerically related.
		INVALID,
	};

	// The value-preserving common type of two integer descriptors.
	//
	// Implements the promotion matrix exactly: the four same-type rows, `int`/`long`, `int`/`uint`,
	// `uint`/`long`, and `uint`/`ulong`. Every other signed/unsigned pair -- `int`/`ulong`,
	// `long`/`ulong`, and any narrowing-only mix -- has no type that holds all of both ranges and is
	// rejected. The relation is symmetric, so operand order never changes the answer.
	//
	// `NumericType::NONE` is the absence of a width constraint rather than a fifth type: a pair that
	// includes one yields `NONE` and succeeds, which is what keeps a slot that never declared a width
	// behaving exactly as it did before descriptors existed. The 8- and 16-bit native-only
	// descriptors promote as their 32-bit counterparts, since neither has a source spelling that
	// could name a result.
	static bool promote_integer_pair(NumericType p_left, NumericType p_right, NumericType &r_result);

	// Classifies moving a value of `p_source` into a slot of `p_target`. Pass the source expression's
	// constant value when it has one, so an exactly representable constant can cross a boundary a
	// dynamic value of the same type may not.
	//
	// Only integer and floating built-ins are numerically related; anything else is `INVALID` and the
	// caller's own type rules decide.
	//
	// The classification is about value preservation alone. Whether a conversion the caller can
	// actually lower exists is a separate question the caller keeps answering for itself: crossing
	// between the signed and unsigned carriers is value-preserving in one direction but has no
	// registered `Variant` conversion, so the built-in compatibility rules reject it before this
	// classifier is consulted.
	static Conversion classify(const FSParser::DataType &p_target, const FSParser::DataType &p_source, const Variant *p_constant_source_value);

	// Whether a built-in type participates in numeric conversion at all.
	static bool is_numeric_builtin(const FSParser::DataType &p_type);
};

class FSTypeCompatibility {
public:
	struct Options {
		bool allow_implicit_conversion = false;
		bool strict_dynamic = false;
		bool strict_null = false;
		// The source expression's constant value, when the source is a constant. A constant may enter
		// a narrower or differently signed numeric slot that a dynamic value of the same type may not,
		// because its exact value can be checked against the destination's range. Never propagated
		// into a nested element check: the constant describes the whole value, not its parts.
		const Variant *constant_source_value = nullptr;
		// False while a static frame is checked. A class type parameter is normally checkable because the
		// receiver reifies it, which is what lets a concrete value enter such a slot under a runtime
		// check. A static frame has no receiver, so nothing reifies the parameter and no check exists:
		// the destination is then as undecidable as a method-scope one and is refused the same way.
		bool receiver_is_available = true;
	};

	struct Result {
		bool compatible = false;
		bool requires_runtime_check = false;
		bool uses_implicit_conversion = false;

		Result() = default;
		Result(bool p_compatible, bool p_requires_runtime_check, bool p_uses_implicit_conversion) :
				compatible(p_compatible),
				requires_runtime_check(p_requires_runtime_check),
				uses_implicit_conversion(p_uses_implicit_conversion) {}
	};

	static Result check(const FSParser::DataType &p_target, const FSParser::DataType &p_source);
	static Result check(const FSParser::DataType &p_target, const FSParser::DataType &p_source, const Options &p_options);
	static bool is_compatible(const FSParser::DataType &p_target, const FSParser::DataType &p_source, bool p_allow_implicit_conversion = false);

	// Whether a statically incompatible assignment from p_wide to p_narrow may still be accepted with
	// a runtime-checked conversion. This is the usual supertype-to-subtype case, where the runtime can
	// verify the value really is the narrower type. Tuples are excluded: every tuple erases to the same
	// read-only Array, so no runtime evidence distinguishes a named tuple from its unnamed erasure and
	// the "check" would silently accept unnamed -> named or named A -> named B.
	static bool allows_runtime_narrowing(const FSParser::DataType &p_narrow, const FSParser::DataType &p_wide);

	// True when `p_type` names a class-scope type parameter in a position a store can be validated
	// against the running receiver: the type itself, or a typed-container element at any depth. Such a
	// slot erases like any other parameter, but the receiver reifies the argument, so the store is
	// checked rather than rejected.
	//
	// Two positions are deliberately not included, because no store into them is checkable and this
	// answer must match the one code generation gives. A parameter used as the type argument of a
	// specialized class handle (`Holder[T]`): constructing one inside the declaring class does not reify
	// `T` onto the constructed instance, so enforcing the slot would reject values the program
	// legitimately produces. And a tuple slot, which erases to an untyped Array describing none of its
	// elements.
	static bool destination_depends_on_receiver_type_parameter(const FSParser::DataType &p_type);

	// A type parameter whose only bound is a `final` class denotes exactly that class: no subtype of the
	// bound can exist, so the bound is the one type argument the parameter can ever stand for. Resolves
	// such a parameter into that concrete bound, carrying every wrapper the parameter itself declared --
	// nullability, a `Type[...]` handle layer, a metatype layer -- so the resolved shape lowers exactly
	// as the position that named the parameter would have. Returns false for every other type, leaving
	// `r_resolved` untouched.
	//
	// Compiler lowering resolves through this before ordinary type-parameter erasure, which is what makes
	// a final-bounded slot a genuinely runtime-checked one; the analyzer's undecidability rules resolve
	// through it as well, so the two agree on which positions carry a real check.
	static bool resolve_final_class_bound(const FSParser::DataType &p_type, FSParser::DataType &r_resolved);

	// Whether lowering a position still states `p_resolved_bound`. Pass `p_wrappers_are_expressible =
	// true` where the position travels as an `FSDataType` -- the declaration's own root and the tuple
	// spine -- and false where it becomes a `ContainerType`, which records neither "or null" nor the
	// class-handle layer. A bound carrying one of those wrappers is therefore evidence only in the
	// former, so compiler lowering keeps ordinary erasure in the latter and the analyzer refuses a
	// gradual source there.
	static bool final_class_bound_survives_lowering(const FSParser::DataType &p_resolved_bound, bool p_wrappers_are_expressible);

	// True when nothing at the destination can decide a value against `p_type`, so a store into it is
	// neither justified statically nor verified at run time. Both the typed and the gradual paths ask
	// this one question, which is what keeps them from answering differently for the same declaration.
	//
	// Pass `p_options.receiver_is_available = false` while a static frame is checked; a default-
	// constructed `Options` describes a frame that has a receiver.
	static bool destination_is_undecidable_type_parameter(const FSParser::DataType &p_type, const Options &p_options);

	// Structural identity used for invariant positions such as a typed container element: two types
	// match only when every nested slot -- container elements, generic arguments, and callable/signal
	// parameter, return and rest signatures -- matches as well. `DataType::operator==` stops at the
	// principal type, so it accepts two callables with different signatures.
	static bool is_invariant_equal(const FSParser::DataType &p_a, const FSParser::DataType &p_b);

	// Whether a rest ("...") tail narrows the trailing arguments below Variant. A bare `Array` and an
	// `Array[Variant]` tail are both gradual and therefore not narrowing.
	static bool rest_parameter_type_is_narrowing(const FSParser::DataType &p_rest_parameter_type);

	// The single rest-tail acceptance rule shared by class overrides, abstract requirements, trait
	// witnesses, callable assignment and signal connection.
	//
	// A rest tail sits in parameter position: callers pass individual elements, so the element type is
	// contravariant. The implementation may accept the same or a broader element type, and a gradual
	// implementation tail accepts anything, but a typed implementation tail cannot satisfy a gradual
	// requirement whose callers may pass any value.
	//
	// Pass `nullptr` for a side that declares no rest tail at all. A requirement without a rest tail
	// promises no trailing arguments, so an extra implementation tail is governed by the arity interval
	// alone; a requirement with a rest tail is unreachable for an implementation that has none.
	static bool rest_parameter_accepts_required_arguments(
			const FSParser::DataType *p_implementation_rest_array,
			const FSParser::DataType *p_required_rest_array,
			bool p_strict_null = false);

	// Whether a rest tail can absorb one fixed argument the requirement declares but the implementation
	// does not, which the caller delivers into the tail instead. Same contravariant element rule: a
	// gradual tail absorbs anything, a typed tail must accept the declared type.
	static bool rest_parameter_accepts_required_argument(
			const FSParser::DataType *p_implementation_rest_array,
			const FSParser::DataType &p_required_argument_type,
			bool p_strict_null = false);

	// Resolves the rest tail a Callable/Signal type promises. Returns false when the signature is not
	// variadic. `METHOD_FLAG_VARARG` is the arity bit, while the rich rest slot is filled only when the
	// element narrows below Variant, so a variadic signature without that slot yields a gradual `Array`.
	static bool callable_signature_rest_parameter_type(const FSParser::DataType &p_signature, FSParser::DataType &r_rest_array);
};
