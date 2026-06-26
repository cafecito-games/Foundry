/**************************************************************************/
/*  gdscript_refactoring_types.cpp                                        */
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

#include "gdscript_refactoring_types.h"

#ifdef TOOLS_ENABLED

#include "core/object/class_db.h"
#include "core/object/script_language.h"
#include "core/variant/variant.h"

namespace {

bool is_renderable_type(const GDScriptParser::DataType &p_type) {
	if (!p_type.is_set() || p_type.is_variant()) {
		return false;
	}
	if (p_type.kind == GDScriptParser::DataType::BUILTIN && p_type.builtin_type == Variant::NIL) {
		return false;
	}
	if (p_type.is_meta_type || p_type.is_pseudo_type) {
		return false;
	}
	return true;
}

bool is_usable_spelling(const String &p_rendered) {
	// A concrete, non-Variant type can still stringify to empty or placeholder
	// text (e.g. an invalid script reference), which is not a usable annotation.
	return !p_rendered.is_empty() && p_rendered != "null" && !p_rendered.contains("<unresolved type>");
}

// Returns the namespace and bare class name of a CLASS/SCRIPT type that refers to
// a namespaced global class, or false when the type is not a namespaced global
// class (a builtin, a local/inner class, or a class in the global namespace).
bool get_global_class_namespace(const GDScriptParser::DataType &p_type, String &r_namespace, String &r_class_name) {
	if (p_type.kind == GDScriptParser::DataType::CLASS) {
		const GDScriptParser::ClassNode *class_node = p_type.class_type;
		if (class_node == nullptr || class_node->outer != nullptr) {
			// Inner classes are not global; only a root class carries a namespace.
			return false;
		}
		if (class_node->namespace_name.is_empty() || class_node->identifier == nullptr) {
			return false;
		}
		r_namespace = class_node->namespace_name;
		r_class_name = class_node->identifier->name;
		return true;
	}
	if (p_type.kind == GDScriptParser::DataType::SCRIPT) {
		if (p_type.script_type.is_null()) {
			return false;
		}
		const StringName global_name = p_type.script_type->get_global_name();
		if (global_name == StringName()) {
			return false;
		}
		StringName class_name;
		String namespace_name;
		ScriptServer::get_global_class_name_parts(global_name, &class_name, &namespace_name);
		if (namespace_name.is_empty()) {
			return false;
		}
		r_namespace = namespace_name;
		r_class_name = class_name;
		return true;
	}
	return false;
}

bool namespace_defines_class(const String &p_namespace, const String &p_class_name) {
	if (p_namespace.is_empty()) {
		return false;
	}
	return ScriptServer::is_global_class(p_namespace + "." + p_class_name);
}

// True when a bare `p_name` names a builtin type or a native engine class. The
// analyzer resolves these before any namespace lookup, so they shadow a bare
// reference and also block a qualified chain whose leading segment is such a name.
bool name_is_builtin_or_native(const String &p_name) {
	return Variant::get_type_by_name(p_name) != Variant::VARIANT_MAX || ClassDB::class_exists(p_name);
}

// True when a bare `p_class_name` would bind to something other than a namespaced
// global class: a builtin type, a native class, or a global-namespace global
// class. These all win over (or collide with) a namespaced import, so the bare
// spelling cannot be made to name the target by importing.
bool bare_name_is_globally_shadowed(const String &p_class_name) {
	if (name_is_builtin_or_native(p_class_name)) {
		return true;
	}
	// A global class with no namespace is referred to by its bare name.
	return ScriptServer::is_global_class(p_class_name);
}

// Counts in-scope namespaces (current + imports) other than the target that also
// define `p_class_name`. Any such namespace makes a bare reference ambiguous or
// shadowed, so a bare spelling cannot safely name the target.
int count_conflicting_in_scope_definitions(const String &p_target_namespace, const String &p_class_name, const GDScriptRefactorTypes::AnnotationScope &p_scope) {
	int conflicts = 0;
	if (p_scope.current_namespace != p_target_namespace && namespace_defines_class(p_scope.current_namespace, p_class_name)) {
		conflicts++;
	}
	for (const String &imported : p_scope.imported_namespaces) {
		if (imported != p_target_namespace && namespace_defines_class(imported, p_class_name)) {
			conflicts++;
		}
	}
	return conflicts;
}

bool namespace_is_in_scope(const String &p_namespace, const GDScriptRefactorTypes::AnnotationScope &p_scope) {
	if (p_namespace == p_scope.current_namespace) {
		return true;
	}
	for (const String &imported : p_scope.imported_namespaces) {
		if (imported == p_namespace) {
			return true;
		}
	}
	return false;
}

// True when the bare `p_class_name` resolves to `p_target_namespace`'s class at
// the annotation site. A class already in scope is rendered bare; anything else
// is rendered fully-qualified, which resolves on its own without an import.
//
// Out-of-scope classes are intentionally NOT rendered bare-with-a-new-import:
// adding an `import` exposes every class in that namespace, which can make an
// unrelated bare name ambiguous, and per-candidate import edits cannot coordinate
// across sibling annotations in a batch. The qualified spelling sidesteps both
// hazards.
bool bare_name_resolves_to_target(const String &p_target_namespace, const String &p_class_name, const GDScriptRefactorTypes::AnnotationScope &p_scope) {
	// A bare name colliding with a builtin/native/global-namespace class can never
	// name a namespaced class (the analyzer resolves those first). This holds even
	// for a same-namespace class, so the check comes first.
	if (bare_name_is_globally_shadowed(p_class_name)) {
		return false;
	}
	// A class in the current namespace resolves bare to itself; the current
	// namespace wins over imports. (A same-named class elsewhere in the current
	// namespace is a project-level conflict regardless of this refactor.)
	if (p_target_namespace == p_scope.current_namespace) {
		return true;
	}
	// Another in-scope namespace defining the same name makes a bare reference
	// ambiguous.
	if (count_conflicting_in_scope_definitions(p_target_namespace, p_class_name, p_scope) > 0) {
		return false;
	}
	return namespace_is_in_scope(p_target_namespace, p_scope);
}

// Mutable state shared across one annotation's leaves. When force_qualified is
// true, every namespaced class is rendered fully qualified regardless of scope;
// used to build a scope-independent identity for comparing types across call
// sites.
struct RenderState {
	HashSet<String> required_imports; // Currently always empty; see render_annotatable_type_in_scope.
	bool force_qualified = false;
};

// Mirrors DataType::to_string() for the container/type-argument/signature
// structure, but renders each CLASS/SCRIPT leaf with the minimal in-scope
// spelling.
bool render_scoped(const GDScriptParser::DataType &p_type, const GDScriptRefactorTypes::AnnotationScope &p_scope, String &r_rendered, RenderState &r_state) {
	String target_namespace;
	String class_name;
	if (get_global_class_namespace(p_type, target_namespace, class_name)) {
		const bool render_bare = !r_state.force_qualified && bare_name_resolves_to_target(target_namespace, class_name, p_scope);
		if (!render_bare && !r_state.force_qualified) {
			// A qualified `namespace.Class` only resolves when its leading segment is
			// not bound to something the analyzer resolves before a namespace chain: a
			// builtin type, a native class (e.g. `Node.foo.Bar` binds `Node` to the
			// native class and then fails), or a local class-scope name (an inner class
			// or member). A global-class root is fine because the analyzer matches the
			// longest qualified global class before the bare root. When the root is
			// shadowed and the class is not in scope, neither spelling resolves, so the
			// site is left un-annotated rather than emitting invalid source. The
			// force_qualified identity path keeps the spelling for comparison only.
			const String namespace_root = target_namespace.get_slicec('.', 0);
			if (name_is_builtin_or_native(namespace_root) || p_scope.shadowing_local_names.has(namespace_root)) {
				return false;
			}
		}
		// A fully-qualified `namespace.Class` resolves on its own, so it needs no import.
		String spelling = render_bare ? class_name : target_namespace + "." + class_name;
		// Specialized type arguments on a namespaced class still need scoping.
		if (!p_type.type_arguments.is_empty()) {
			String arguments;
			for (int i = 0; i < p_type.type_arguments.size(); i++) {
				if (i > 0) {
					arguments += ", ";
				}
				String argument_rendered;
				if (!render_scoped(p_type.type_arguments[i], p_scope, argument_rendered, r_state)) {
					return false;
				}
				arguments += argument_rendered;
			}
			spelling += vformat("[%s]", arguments);
		}
		if (p_type.is_nullable) {
			spelling += "?";
		}
		r_rendered = spelling;
		return true;
	}

	// Recurse into container element types so a cross-namespace class nested in an
	// Array/Dictionary still contributes its import and qualified spelling. The
	// nullable marker is preserved so an inferred `Array[T]?` is not narrowed.
	const String nullable_suffix = p_type.is_nullable ? "?" : "";
	if (p_type.kind == GDScriptParser::DataType::BUILTIN) {
		if (p_type.builtin_type == Variant::ARRAY && p_type.has_container_element_type(0)) {
			String element_rendered;
			if (!render_scoped(p_type.get_container_element_type(0), p_scope, element_rendered, r_state)) {
				return false;
			}
			r_rendered = vformat("Array[%s]%s", element_rendered, nullable_suffix);
			return true;
		}
		if (p_type.builtin_type == Variant::DICTIONARY && p_type.has_container_element_types()) {
			String key_rendered;
			String value_rendered;
			if (!render_scoped(p_type.get_container_element_type_or_variant(0), p_scope, key_rendered, r_state) ||
					!render_scoped(p_type.get_container_element_type_or_variant(1), p_scope, value_rendered, r_state)) {
				return false;
			}
			r_rendered = vformat("Dictionary[%s, %s]%s", key_rendered, value_rendered, nullable_suffix);
			return true;
		}
		// A Callable/Signal with an explicit signature can carry namespaced classes
		// in its parameter and return types, so scope those too.
		if ((p_type.builtin_type == Variant::CALLABLE || p_type.builtin_type == Variant::SIGNAL) && p_type.has_explicit_method_signature) {
			const bool has_return = p_type.builtin_type == Variant::CALLABLE;
			String parameters;
			for (int i = 0; i < p_type.method_parameter_types.size(); i++) {
				if (i > 0) {
					parameters += ", ";
				}
				String parameter_rendered;
				if (!render_scoped(p_type.method_parameter_types[i], p_scope, parameter_rendered, r_state)) {
					return false;
				}
				parameters += parameter_rendered;
			}
			String signature;
			if (has_return) {
				String return_rendered = "void";
				if (!p_type.method_return_type.is_empty()) {
					const GDScriptParser::DataType &return_type = p_type.method_return_type[0];
					const bool return_is_void = return_type.kind == GDScriptParser::DataType::BUILTIN && return_type.builtin_type == Variant::NIL;
					if (!return_is_void && !render_scoped(return_type, p_scope, return_rendered, r_state)) {
						return false;
					} else if (return_is_void) {
						return_rendered = "void";
					}
				}
				signature = vformat("[[%s], %s]", parameters, return_rendered);
			} else {
				signature = vformat("[[%s]]", parameters);
			}
			r_rendered = vformat("%s%s%s", has_return ? "Callable" : "Signal", signature, nullable_suffix);
			return true;
		}
	}

	// A generic whose head is not itself a namespaced class (e.g. a global-namespace
	// `Box[T]`) can still carry namespaced type arguments. Render the head via the
	// engine's spelling (with the arguments stripped) and recurse so a nested
	// cross-namespace class contributes its qualified spelling and import.
	if (!p_type.type_arguments.is_empty()) {
		GDScriptParser::DataType head = p_type;
		head.type_arguments.clear();
		head.is_nullable = false;
		const String head_rendered = head.to_string();
		if (!is_usable_spelling(head_rendered)) {
			return false;
		}
		String arguments;
		for (int i = 0; i < p_type.type_arguments.size(); i++) {
			if (i > 0) {
				arguments += ", ";
			}
			String argument_rendered;
			if (!render_scoped(p_type.type_arguments[i], p_scope, argument_rendered, r_state)) {
				return false;
			}
			arguments += argument_rendered;
		}
		String rendered = vformat("%s[%s]", head_rendered, arguments);
		if (p_type.is_nullable) {
			rendered += "?";
		}
		r_rendered = rendered;
		return true;
	}

	// Non-namespaced leaf: defer to the engine's own spelling.
	const String rendered = p_type.to_string();
	if (!is_usable_spelling(rendered)) {
		return false;
	}
	r_rendered = rendered;
	return true;
}

} // namespace

bool GDScriptRefactorTypes::render_annotatable_type(const GDScriptParser::DataType &p_type, String &r_rendered) {
	if (!is_renderable_type(p_type)) {
		return false;
	}
	const String rendered = p_type.to_string();
	if (!is_usable_spelling(rendered)) {
		return false;
	}
	r_rendered = rendered;
	return true;
}

bool GDScriptRefactorTypes::render_annotatable_type_in_scope(const GDScriptParser::DataType &p_type, const AnnotationScope &p_scope, String &r_rendered, HashSet<String> &r_required_imports) {
	if (!is_renderable_type(p_type)) {
		return false;
	}
	String rendered;
	RenderState state;
	if (!render_scoped(p_type, p_scope, rendered, state)) {
		return false;
	}
	if (!is_usable_spelling(rendered)) {
		return false;
	}
	r_rendered = rendered;
	r_required_imports = state.required_imports;
	return true;
}

bool GDScriptRefactorTypes::render_qualified_identity(const GDScriptParser::DataType &p_type, String &r_identity) {
	if (!is_renderable_type(p_type)) {
		return false;
	}
	String rendered;
	RenderState state;
	state.force_qualified = true;
	if (!render_scoped(p_type, AnnotationScope(), rendered, state)) {
		return false;
	}
	if (!is_usable_spelling(rendered)) {
		return false;
	}
	r_identity = rendered;
	return true;
}

#endif // TOOLS_ENABLED
