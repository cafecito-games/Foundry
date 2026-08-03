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

class FSTypeCompatibility {
public:
	struct Options {
		bool allow_implicit_conversion = false;
		bool strict_dynamic = false;
		bool strict_null = false;
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
