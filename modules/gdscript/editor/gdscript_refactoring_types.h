/**************************************************************************/
/*  gdscript_refactoring_types.h                                          */
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

#include "../gdscript_parser.h"

#include "core/string/ustring.h"
#include "core/templates/hash_set.h"
#include "core/templates/vector.h"

namespace GDScriptRefactorTypes {

// The namespace scope an annotation is being inserted into, used to pick the
// minimal class spelling that resolves at the insertion site. Empty namespace
// plus empty imports describes a global (non-namespaced) file, where every
// global class is in scope by its bare name.
struct AnnotationScope {
	String current_namespace; // The file's `namespace` declaration; empty for the global namespace.
	Vector<String> imported_namespaces; // The file's `import` declarations.
};

// Renders p_type to its GDScript source spelling when it is worth annotating.
// Returns false for Variant / non-hard / empty types, leaving r_rendered unset.
bool render_annotatable_type(const GDScriptParser::DataType &p_type, String &r_rendered);

// Namespace-aware rendering: chooses the minimal class spelling that resolves at
// the insertion site described by p_scope. A global class is rendered bare when
// it already resolves in scope (same namespace, or imported without colliding
// with a builtin/native/global class or another in-scope namespace), and
// fully-qualified (`namespace.Class`, which resolves on its own) otherwise.
// Container element types, type arguments, and Callable/Signal signatures are
// rendered the same way. r_required_imports reports namespaces that would need a
// new import for the chosen spelling to resolve; it is currently always empty
// because out-of-scope classes are qualified rather than imported, but the
// parameter is kept so a future caller that can safely insert imports can opt in.
// Behaves identically to render_annotatable_type for global (non-namespaced)
// classes. Returns false in the same cases as render_annotatable_type.
bool render_annotatable_type_in_scope(const GDScriptParser::DataType &p_type, const AnnotationScope &p_scope, String &r_rendered, HashSet<String> &r_required_imports);

// Renders an annotatable type with every namespaced class fully qualified,
// regardless of scope. Two types that render the same bare spelling but name
// different classes (e.g. `alpha.Thing` vs `beta.Thing`) produce different
// identities here, so callers comparing types across sites can tell them apart.
// Returns false in the same cases as render_annotatable_type.
bool render_qualified_identity(const GDScriptParser::DataType &p_type, String &r_identity);

} // namespace GDScriptRefactorTypes

#endif // TOOLS_ENABLED
