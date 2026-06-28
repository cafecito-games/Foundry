/**************************************************************************/
/*  fs_container_inference.h                                              */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
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

#ifdef TOOLS_ENABLED

#include "../fs_parser.h"

#include "core/string/ustring.h"
#include "core/templates/vector.h"

// Infers the element type of a bare `Array` local variable from how it is used
// inside its declaring function, so the migration wizard can upgrade
// `var items = []` to `var items: Array[int]` when (and only when) that is
// provably correct.
//
// Soundness model: Arrays are reference types, so the inference is only
// attempted for a local variable whose initializer is an array *literal* (the
// full initial contents are then known) and which never escapes the function
// (it is never passed as an argument, returned, aliased, stored, or captured by
// a lambda). Every mutation observed on the variable must come from a modeled,
// monomorphic operation; anything unmodelled forces a conservative skip. The
// post-edit verification harness is a second line of defense, not the first.
class FSContainerInference {
public:
	enum Outcome {
		// The declaration is not a bare-`Array` local literal, so element
		// inference does not apply. The caller keeps the analyzer's own type.
		NOT_APPLICABLE,
		// A bare `Array` literal with no usage that pins an element type. The
		// declaration is left as `Array` rather than guessing.
		NO_EVIDENCE,
		// A single concrete element type was proven. `element_type` carries the
		// upgraded `Array[T]` type, ready to render.
		INFERRED,
		// Usage pushes more than one concrete element type. Skipped and reported.
		MIXED,
		// The variable escapes the function, so its contents cannot be bounded.
		ESCAPES,
		// A modeled mutation supplies an element whose type the analyzer could
		// not resolve to a concrete, renderable type. Skipped and reported.
		UNPROVABLE,
		// An element read flows into a variable that is later reassigned to a value
		// incompatible with the inferred element type. The read is `Variant` while
		// the container is bare, so the reassignment is currently valid; typing the
		// container would narrow the read and reject it. Skipped and reported.
		READ_NARROWS,
	};

	struct Result {
		Outcome outcome = NOT_APPLICABLE;
		// Valid only when `outcome == INFERRED`: the bare container type augmented
		// with the inferred element(s), e.g. `Array[int]` or `Dictionary[String, int]`.
		FSParser::DataType element_type;
		// Human-readable explanation for the skipped outcomes (MIXED / ESCAPES /
		// UNPROVABLE), suitable for the wizard's "skipped and reported" surface.
		String detail;
	};

	// Infers the element type of `p_decl` from its usages within
	// `p_function_body` (the declaring function's top-level suite). `p_decl`
	// must be a local variable declared somewhere inside that body.
	static Result infer_local_array_element_type(
			const FSParser::VariableNode *p_decl,
			const FSParser::SuiteNode *p_function_body);

	// Infers the key and value types of a bare `Dictionary` local from its usages
	// within `p_function_body`, mirroring the soundness model of the array path:
	// the initializer must be a dictionary literal, the variable must never escape,
	// and every observed mutation must contribute key/value types drawn from a
	// directly-observed argument. On success, `Result::element_type` is the bare
	// `Dictionary` augmented with both the key (index 0) and value (index 1) types,
	// e.g. `Dictionary[String, int]`.
	static Result infer_local_dictionary_element_type(
			const FSParser::VariableNode *p_decl,
			const FSParser::SuiteNode *p_function_body);

	// Infers the element type of a bare `Array` member variable from how it is used
	// across the whole declaring class. Unlike a local, a member is reachable from
	// every method of the class (and its nested subclasses), so the union is taken
	// over all of those bodies rather than one function. The soundness boundary is
	// also wider: a member can be mutated through any reference to its instance, so
	// the inference bails conservatively unless the member is provably private to
	// the class. Since FoundryScript has no enforced access modifiers, only members that
	// follow the leading-underscore "private" convention are considered. It is
	// skipped (with an explanation) when it is part of the public API (no leading
	// underscore), `@export`ed, has a custom setter/getter, is `static`, is
	// accessed through a base other than `self`, or the instance escapes in a way
	// external code could exploit to mutate it. `p_class` declares `p_member`.
	//
	// `p_subclasses` is the project-wide set of classes that `extends` `p_class`
	// (transitively), discovered from the dependency closure by the caller. Because
	// FoundryScript has no `final`, such a subclass can mutate the inherited member from
	// its own methods with a different element type; an empty inference here would be
	// unsound. Each subclass body is folded into the same union/escape model as the
	// declaring class, except its references to the member resolve by name (the
	// subclass is parsed in a separate tree, so it cannot share the member node's
	// pointer identity). When the open world cannot be bounded -- because the caller
	// could not prove it enumerated every subclass -- pass `p_subclasses_complete`
	// as false and the inference bails conservatively.
	static Result infer_member_array_element_type(
			const FSParser::VariableNode *p_member,
			const FSParser::ClassNode *p_class,
			const Vector<const FSParser::ClassNode *> &p_subclasses = Vector<const FSParser::ClassNode *>(),
			bool p_subclasses_complete = true);

	// Infers the key and value types of a bare `Dictionary` member variable across
	// the whole declaring class, with the same widened soundness boundary as
	// `infer_member_array_element_type` (including the project-wide subclass scan).
	static Result infer_member_dictionary_element_type(
			const FSParser::VariableNode *p_member,
			const FSParser::ClassNode *p_class,
			const Vector<const FSParser::ClassNode *> &p_subclasses = Vector<const FSParser::ClassNode *>(),
			bool p_subclasses_complete = true);
};

#endif // TOOLS_ENABLED
