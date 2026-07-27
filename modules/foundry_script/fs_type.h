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
};
