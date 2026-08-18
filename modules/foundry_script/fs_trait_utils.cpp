/**************************************************************************/
/*  fs_trait_utils.cpp                                                    */
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

#include "fs_trait_utils.h"

#include "core/templates/hash_set.h"

Vector<FSParser::ClassNode *> fs_trait_identity_closure_nodes(const FSParser::ClassNode *p_trait) {
	Vector<FSParser::ClassNode *> nodes;
	if (p_trait == nullptr) {
		return nodes;
	}

	HashSet<StringName> seen;
	auto append_identity = [&](FSParser::ClassNode *p_member) {
		const StringName identity = fs_trait_identity_name(p_member);
		if (identity != StringName() && !seen.has(identity)) {
			seen.insert(identity);
			nodes.push_back(p_member);
		}
	};

	append_identity(const_cast<FSParser::ClassNode *>(p_trait));
	for (FSParser::ClassNode *supertrait : p_trait->resolved_traits) {
		if (supertrait != nullptr) {
			append_identity(supertrait);
		}
	}
	return nodes;
}

Vector<StringName> fs_trait_identity_closure(const FSParser::ClassNode *p_trait) {
	Vector<StringName> identities;
	for (const FSParser::ClassNode *identity : fs_trait_identity_closure_nodes(p_trait)) {
		identities.push_back(fs_trait_identity_name(identity));
	}
	return identities;
}

// Two nodes name the same trait declaration when they are the same node, or when they carry the same
// non-empty fully qualified name — the per-declaration unique key, which is what lets a trait reached
// through a depended parser match the node the caller already holds. Global class name is
// deliberately not used: two distinct declarations may share one, and conflating them is a separate
// diagnostic's job.
static bool _same_trait_declaration(const FSParser::ClassNode *p_a, const FSParser::ClassNode *p_b) {
	if (p_a == p_b) {
		return true;
	}
	return p_a != nullptr && p_b != nullptr && !p_a->fqcn.is_empty() && p_a->fqcn == p_b->fqcn;
}

HashMap<StringName, FSParser::DataType> fs_trait_use_type_argument_bindings(
		const FSParser::ClassNode *p_trait, const FSParser::ClassNode::TraitUse &p_trait_use) {
	HashMap<StringName, FSParser::DataType> bindings;
	if (p_trait == nullptr || p_trait->type_parameters.is_empty()) {
		return bindings;
	}

	const int count = MIN(p_trait->type_parameters.size(), p_trait_use.resolved_type_arguments.size());
	for (int i = 0; i < count; i++) {
		const FSParser::TypeParameterNode *type_parameter = p_trait->type_parameters[i];
		if (type_parameter != nullptr && type_parameter->identifier != nullptr) {
			bindings.insert(type_parameter->identifier->name, p_trait_use.resolved_type_arguments[i]);
		}
	}
	return bindings;
}

static HashMap<StringName, FSParser::DataType> _trait_type_argument_bindings(
		const FSParser::ClassNode *p_class, const FSParser::ClassNode *p_trait, int p_depth) {
	HashMap<StringName, FSParser::DataType> bindings;
	// The analyzer rejects cyclic trait use, but the type relation and the editor refactoring surface
	// can be handed partially resolved trees, where an unbounded recursion is a hang rather than a
	// diagnostic.
	if (unlikely(p_depth > Variant::MAX_RECURSION_DEPTH)) {
		return bindings;
	}
	if (p_class == nullptr || p_trait == nullptr || p_trait->type_parameters.is_empty()) {
		return bindings;
	}

	for (const FSParser::ClassNode::TraitUse &trait_use : p_class->used_traits) {
		const FSParser::ClassNode *used_trait = trait_use.resolved_trait;
		if (used_trait == nullptr) {
			continue;
		}
		if (_same_trait_declaration(used_trait, p_trait)) {
			bindings = fs_trait_use_type_argument_bindings(p_trait, trait_use);
			if (bindings.is_empty()) {
				// A bare `uses Storage` on a generic trait supplies nothing, so it proves nothing about
				// the trait's parameters. Keep looking: a later entry, or a supertrait hop, may still
				// bind them.
				continue;
			}
			return bindings;
		}

		bool reaches_trait = false;
		for (const FSParser::ClassNode *supertrait : used_trait->resolved_traits) {
			if (_same_trait_declaration(supertrait, p_trait)) {
				reaches_trait = true;
				break;
			}
		}
		if (reaches_trait) {
			// Compose the intermediate trait's binding of the target with this class's binding of the
			// intermediate, so `class C: uses Storing[String]` over `trait Storing[T]: uses Keeper[T]`
			// reports `Keeper`'s parameter as `String` rather than as `T`.
			const HashMap<StringName, FSParser::DataType> inner =
					_trait_type_argument_bindings(used_trait, p_trait, p_depth + 1);
			if (inner.is_empty()) {
				continue;
			}
			const HashMap<StringName, FSParser::DataType> outer =
					_trait_type_argument_bindings(p_class, used_trait, p_depth + 1);
			for (const KeyValue<StringName, FSParser::DataType> &binding : inner) {
				bindings.insert(binding.key, FSParser::DataType::substitute(binding.value, outer));
			}
			return bindings;
		}
	}
	return bindings;
}

HashMap<StringName, FSParser::DataType> fs_trait_type_argument_bindings(
		const FSParser::ClassNode *p_class, const FSParser::ClassNode *p_trait) {
	return _trait_type_argument_bindings(p_class, p_trait, 0);
}

static bool _references_self(const FSParser::DataType &p_argument, int p_depth) {
	if (unlikely(p_depth > Variant::MAX_RECURSION_DEPTH)) {
		return true;
	}
	if (p_argument.kind == FSParser::DataType::TYPE_PARAMETER &&
			p_argument.type_parameter_name == SNAME("@Self")) {
		return true;
	}
	// Tuple elements share the container-element slot, so one walk covers `Array[Self]`,
	// `Dictionary[String, Self]`, and `(int, Self)` alike.
	for (const FSParser::DataType &element : p_argument.container_element_types) {
		if (_references_self(element, p_depth + 1)) {
			return true;
		}
	}
	for (const FSParser::DataType &type_argument : p_argument.type_arguments) {
		if (_references_self(type_argument, p_depth + 1)) {
			return true;
		}
	}
	for (const FSParser::DataType &member : p_argument.union_members) {
		if (_references_self(member, p_depth + 1)) {
			return true;
		}
	}
	return false;
}

bool fs_trait_argument_references_self(const FSParser::DataType &p_argument) {
	return _references_self(p_argument, 0);
}

bool fs_trait_implementer_reifies_self(const FSParser::ClassNode *p_implementer) {
	// A trait is never the implementer of its own `Self`: the class that applies it is, and that class
	// is judged on its own finality where the application is read.
	return p_implementer != nullptr && !p_implementer->is_trait && p_implementer->is_final &&
			p_implementer->type_parameters.is_empty();
}

FSParser::DataType fs_reify_self_in_trait_argument(
		const FSParser::ClassNode *p_implementer, const FSParser::DataType &p_argument) {
	if (!fs_trait_implementer_reifies_self(p_implementer) || !fs_trait_argument_references_self(p_argument)) {
		return p_argument;
	}

	FSParser::DataType self_type = p_implementer->get_datatype();
	if (!self_type.is_set()) {
		return p_argument;
	}
	// The class's own datatype is its *handle*; `Self` in an argument names the class as a type, and a
	// non-generic class has no arguments of its own to carry.
	self_type.is_meta_type = false;
	self_type.is_pseudo_type = false;
	self_type.is_constant = false;
	self_type.type_arguments.clear();

	HashMap<StringName, FSParser::DataType> bindings;
	bindings.insert(SNAME("@Self"), self_type);
	// `DataType::substitute()` already recurses through type arguments, container and tuple elements,
	// union members, and method signatures, and carries a nullable or `Type[...]` layer written on the
	// `Self` node onto the class it resolves to.
	return FSParser::DataType::substitute(p_argument, bindings);
}

bool fs_project_conformance_trait_arguments(
		const FSParser::ClassNode *p_direct_trait,
		const Vector<FSParser::DataType> &p_conformance_arguments,
		const HashMap<StringName, FSParser::DataType> &p_direct_bindings,
		const FSParser::ClassNode *p_identity_trait,
		const FSParser::ClassNode *p_implementer,
		Vector<FSParser::DataType> &r_arguments) {
	r_arguments.clear();
	if (p_direct_trait == nullptr || p_identity_trait == nullptr || p_identity_trait->type_parameters.is_empty()) {
		return false;
	}

	if (p_identity_trait == p_direct_trait) {
		// A bare application of a generic trait supplies nothing at all, which states nothing about
		// the identity's parameters rather than leaving each of them open.
		if (p_conformance_arguments.size() != p_identity_trait->type_parameters.size()) {
			return false;
		}
		for (const FSParser::DataType &argument : p_conformance_arguments) {
			r_arguments.push_back(fs_reify_self_in_trait_argument(p_implementer, argument));
		}
		return true;
	}

	const HashMap<StringName, FSParser::DataType> substitution =
			fs_trait_type_argument_bindings(p_direct_trait, p_identity_trait);
	Vector<FSParser::DataType> projected;
	for (const FSParser::TypeParameterNode *type_parameter : p_identity_trait->type_parameters) {
		if (type_parameter == nullptr || type_parameter->identifier == nullptr) {
			return false;
		}
		const FSParser::DataType *bound = substitution.getptr(type_parameter->identifier->name);
		if (bound == nullptr) {
			return false;
		}
		projected.push_back(fs_reify_self_in_trait_argument(
				p_implementer, FSParser::DataType::substitute(*bound, p_direct_bindings)));
	}
	r_arguments = projected;
	return true;
}

#ifndef FOUNDRY_SCRIPT_NO_FRONTEND

#include "fs_cache.h"
#include "fs_conformance_registry.h"

bool fs_class_has_named_trait(const FSParser::ClassNode *p_class, const StringName &p_trait_name) {
	if (p_class == nullptr || p_trait_name == StringName()) {
		return false;
	}

	const FSConformanceRegistry *registry = FSConformanceRegistry::get_singleton();
	const FSParser::ClassNode *current = p_class;
	while (current != nullptr) {
		if (current->is_trait && fs_trait_identity_name(current) == p_trait_name) {
			return true;
		}

		for (const FSParser::ClassNode *trait : current->resolved_traits) {
			if (trait != nullptr && fs_trait_identity_name(trait) == p_trait_name) {
				return true;
			}
		}

		if (registry->has_conformance(current->fqcn, p_trait_name) ||
				registry->has_conformance(current->get_global_name(), p_trait_name)) {
			return true;
		}

		if (current->base_type.kind == FSParser::DataType::CLASS) {
			current = current->base_type.class_type;
		} else if (current->base_type.kind == FSParser::DataType::SCRIPT && !current->base_type.script_path.is_empty()) {
			Error err = OK;
			Ref<FSParserRef> base_parser_ref = FSCache::get_parser(current->base_type.script_path, FSParserRef::INTERFACE_SOLVED, err);
			if (err != OK || base_parser_ref.is_null()) {
				return false;
			}
			current = base_parser_ref->get_parser()->get_tree();
		} else if (current->base_type.kind == FSParser::DataType::NATIVE) {
			return registry->native_class_conforms(current->base_type.native_type, p_trait_name);
		} else {
			break;
		}
	}

	return false;
}

#else // FOUNDRY_SCRIPT_NO_FRONTEND

bool fs_class_has_named_trait(const FSParser::ClassNode *p_class, const StringName &p_trait_name) {
	(void)p_class;
	(void)p_trait_name;
	return false;
}

#endif // FOUNDRY_SCRIPT_NO_FRONTEND
