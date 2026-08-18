/**************************************************************************/
/*  fs_trait_utils.h                                                      */
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

static _FORCE_INLINE_ StringName fs_trait_identity_name(const FSParser::ClassNode *p_trait) {
	ERR_FAIL_NULL_V(p_trait, StringName());

	const StringName global_name = p_trait->get_global_name();
	if (global_name != StringName()) {
		return global_name;
	}
	return StringName(p_trait->fqcn);
}

// The trait itself followed by its supertraits, deduplicated by runtime identity name. The node form
// exists so a caller that must ask each identity a question about its own declaration (its type
// parameters, say) walks exactly the set `fs_trait_identity_closure` names, in the same order.
Vector<FSParser::ClassNode *> fs_trait_identity_closure_nodes(const FSParser::ClassNode *p_trait);
Vector<StringName> fs_trait_identity_closure(const FSParser::ClassNode *p_trait);
bool fs_class_has_named_trait(const FSParser::ClassNode *p_class, const StringName &p_trait_name);

// One `uses` entry's binding of the trait it names: `p_trait`'s type parameters zipped against the
// arguments the entry supplied. Empty when the entry supplied none, which states that this entry
// proves nothing about the trait's parameters rather than that the trait has none.
HashMap<StringName, FSParser::DataType> fs_trait_use_type_argument_bindings(
		const FSParser::ClassNode *p_trait, const FSParser::ClassNode::TraitUse &p_trait_use);

// How `p_class` binds `p_trait`'s type parameters, expressed in `p_class`'s own frame, following
// both direct `uses` entries and transitive supertrait hops. Empty when `p_class` does not apply the
// trait, or applies it without ever supplying type arguments.
//
// This is the single walker the analyzer, the compiler, the static type relation, and the editor
// refactoring surface all use. A divergence here is not an inconsistency but an analyzer/compiler/
// runtime split: a program that type-checks against one binding and executes against another.
//
// Only meaningful after analysis. `TraitUse::resolved_type_arguments` is populated by the analyzer,
// so a consumer running before or without analysis sees every entry as argument-less.
HashMap<StringName, FSParser::DataType> fs_trait_type_argument_bindings(
		const FSParser::ClassNode *p_class, const FSParser::ClassNode *p_trait);

// The arguments one conformance supplies for one trait identity in its implied closure, in the
// identity's own parameter order. The direct trait takes the declaration's own arguments; an implied
// supertrait takes its binding by the direct trait, re-expressed in the conformance's terms through
// `p_direct_bindings`.
//
// This is the one projection both conformance records are built from: the analyzer reduces each
// entry to the registry's flattened form and the compiler lowers each entry to a runtime descriptor.
// Two separate walks would let the declaration-side record and the runtime record describe the same
// conformance differently, which is an analyzer/runtime split about what a conformance proved.
//
// Returns false -- and leaves `r_arguments` empty -- only when the projection itself fails: a missing
// trait, an identity with no parameters, an application whose arity does not match, or an identity
// parameter the direct trait never binds. A position that projects to an unset type or to a type
// parameter still yields an entry: it is an open position, and it never erases what a concrete
// sibling position proved.
bool fs_project_conformance_trait_arguments(
		const FSParser::ClassNode *p_direct_trait,
		const Vector<FSParser::DataType> &p_conformance_arguments,
		const HashMap<StringName, FSParser::DataType> &p_direct_bindings,
		const FSParser::ClassNode *p_identity_trait,
		Vector<FSParser::DataType> &r_arguments);
