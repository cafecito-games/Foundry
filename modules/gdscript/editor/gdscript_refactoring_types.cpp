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

// True when a bare `p_class_name` would bind to something other than a namespaced
// global class: a builtin type, a native class, or a global-namespace global
// class. These all win over (or collide with) a namespaced import, so the bare
// spelling cannot be made to name the target by importing.
bool bare_name_is_globally_shadowed(const String &p_class_name) {
	if (Variant::get_type_by_name(p_class_name) != Variant::VARIANT_MAX) {
		return true;
	}
	if (ClassDB::class_exists(p_class_name)) {
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

enum class ClassSpelling {
	BARE_NO_IMPORT, // Bare name already resolves to the target.
	BARE_WITH_IMPORT, // Bare name resolves to the target once the namespace is imported.
	QUALIFIED, // Use the fully-qualified `namespace.Class`, which resolves on its own.
};

// Chooses the minimal class spelling that resolves to `p_target_namespace`'s
// `p_class_name` at the annotation site. Prefers the bare name (optionally adding
// an import), and falls back to the always-resolvable qualified spelling when a
// bare reference would be shadowed or ambiguous.
ClassSpelling choose_class_spelling(const String &p_target_namespace, const String &p_class_name, const GDScriptRefactorTypes::AnnotationScope &p_scope) {
	// A class in the current namespace resolves bare to itself; the current
	// namespace wins first. (A same-named class elsewhere in the current namespace
	// is a project-level conflict regardless of this refactor.)
	if (p_target_namespace == p_scope.current_namespace) {
		return ClassSpelling::BARE_NO_IMPORT;
	}
	// A bare name colliding with a builtin/native/global-namespace class can never
	// name a namespaced class, and importing cannot change that.
	if (bare_name_is_globally_shadowed(p_class_name)) {
		return ClassSpelling::QUALIFIED;
	}
	// Another in-scope namespace defining the same name makes a bare reference
	// ambiguous; importing the target would not help.
	if (count_conflicting_in_scope_definitions(p_target_namespace, p_class_name, p_scope) > 0) {
		return ClassSpelling::QUALIFIED;
	}
	if (namespace_is_in_scope(p_target_namespace, p_scope)) {
		return ClassSpelling::BARE_NO_IMPORT;
	}
	return ClassSpelling::BARE_WITH_IMPORT;
}

// Mirrors DataType::to_string() for the container/type-argument structure, but
// renders each CLASS/SCRIPT leaf with the minimal in-scope spelling and records
// the namespaces that must be imported for the spelling to resolve.
bool render_scoped(const GDScriptParser::DataType &p_type, const GDScriptRefactorTypes::AnnotationScope &p_scope, String &r_rendered, HashSet<String> &r_required_imports) {
	String target_namespace;
	String class_name;
	if (get_global_class_namespace(p_type, target_namespace, class_name)) {
		String spelling;
		switch (choose_class_spelling(target_namespace, class_name, p_scope)) {
			case ClassSpelling::BARE_NO_IMPORT:
				spelling = class_name;
				break;
			case ClassSpelling::BARE_WITH_IMPORT:
				spelling = class_name;
				r_required_imports.insert(target_namespace);
				break;
			case ClassSpelling::QUALIFIED:
				// A fully-qualified `namespace.Class` resolves on its own, so it needs
				// no import.
				spelling = target_namespace + "." + class_name;
				break;
		}
		// Specialized type arguments on a namespaced class still need scoping.
		if (!p_type.type_arguments.is_empty()) {
			String arguments;
			for (int i = 0; i < p_type.type_arguments.size(); i++) {
				if (i > 0) {
					arguments += ", ";
				}
				String argument_rendered;
				if (!render_scoped(p_type.type_arguments[i], p_scope, argument_rendered, r_required_imports)) {
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
	// Array/Dictionary still contributes its import and qualified spelling.
	if (p_type.kind == GDScriptParser::DataType::BUILTIN) {
		if (p_type.builtin_type == Variant::ARRAY && p_type.has_container_element_type(0)) {
			String element_rendered;
			if (!render_scoped(p_type.get_container_element_type(0), p_scope, element_rendered, r_required_imports)) {
				return false;
			}
			r_rendered = vformat("Array[%s]", element_rendered);
			return true;
		}
		if (p_type.builtin_type == Variant::DICTIONARY && p_type.has_container_element_types()) {
			String key_rendered;
			String value_rendered;
			if (!render_scoped(p_type.get_container_element_type_or_variant(0), p_scope, key_rendered, r_required_imports) ||
					!render_scoped(p_type.get_container_element_type_or_variant(1), p_scope, value_rendered, r_required_imports)) {
				return false;
			}
			r_rendered = vformat("Dictionary[%s, %s]", key_rendered, value_rendered);
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
			if (!render_scoped(p_type.type_arguments[i], p_scope, argument_rendered, r_required_imports)) {
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
	HashSet<String> required_imports;
	if (!render_scoped(p_type, p_scope, rendered, required_imports)) {
		return false;
	}
	if (!is_usable_spelling(rendered)) {
		return false;
	}
	r_rendered = rendered;
	r_required_imports = required_imports;
	return true;
}

#endif // TOOLS_ENABLED
