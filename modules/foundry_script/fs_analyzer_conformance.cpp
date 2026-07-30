/**************************************************************************/
/*  fs_analyzer_conformance.cpp                                           */
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

#include "fs_analyzer.h"

#include "foundry_script.h"
#include "fs_conformance_registry.h"
#include "fs_trait_utils.h"

#include "core/config/engine.h"
#include "core/config/project_settings.h"
#include "core/object/script_language.h"

static String _class_or_trait_name(const FSParser::ClassNode *p_class) {
	if (p_class == nullptr) {
		return "<unknown>";
	}
	if (p_class->identifier != nullptr) {
		return p_class->identifier->name;
	}
	return p_class->fqcn.get_file();
}

static String _localize_script_path(const String &p_path) {
	if (ProjectSettings::get_singleton() == nullptr || p_path.is_empty()) {
		return p_path;
	}
	return ProjectSettings::get_singleton()->localize_path(p_path);
}

FSAnalyzer::ScopedCurrentClass::ScopedCurrentClass(FSAnalyzer *p_analyzer, FSParser::ClassNode *p_current_class) {
	analyzer = p_analyzer;
	previous_class = analyzer->parser->current_class;
	analyzer->parser->current_class = p_current_class;
}

FSAnalyzer::ScopedCurrentClass::~ScopedCurrentClass() {
	if (analyzer != nullptr) {
		analyzer->parser->current_class = previous_class;
	}
}

FSParser::ClassNode *FSAnalyzer::resolve_conformance_target(FSParser::ConformanceNode *p_conformance, FSParser::DataType &r_target_type) {
	if (p_conformance->target == nullptr) {
		// A missing target is already a parse error; nothing more to resolve.
		return nullptr;
	}

	{
		ScopedCurrentClass current_class_scope(this, parser->head);
		r_target_type = resolve_datatype(p_conformance->target);
	}

	if (r_target_type.kind == FSParser::DataType::CLASS && r_target_type.class_type != nullptr) {
		return r_target_type.class_type;
	}
	if (r_target_type.kind == FSParser::DataType::SCRIPT && !r_target_type.script_path.is_empty() &&
			r_target_type.script_path.get_extension() == FSLanguage::get_singleton()->get_extension()) {
		Ref<FSParserRef> ref = dependency_parser_access.depended_parser_for(r_target_type.script_path, FSParserRef::INTERFACE_SOLVED);
		if (ref.is_valid() && ref->get_status() >= FSParserRef::INTERFACE_SOLVED) {
			return ref->get_parser()->head;
		}
	}

	// A native engine-class target (`extend Node uses ...`) is supported via a synthesized stand-in
	// ClassNode whose base is the native class, so the shared conformance machinery applies. An engine
	// singleton is rejected: conforming a single global instance to a trait has no consistent receiver.
	if (r_target_type.kind == FSParser::DataType::NATIVE && r_target_type.native_type != StringName() &&
			!r_target_type.is_meta_type) {
		if (Engine::get_singleton()->has_singleton(r_target_type.native_type)) {
			push_error(vformat(R"(Cannot retroactively conform engine singleton "%s" to a trait.)", r_target_type.native_type), p_conformance->target);
			return nullptr;
		}
		return resolve_native_conformance_shim(p_conformance, r_target_type);
	}

	if (r_target_type.kind == FSParser::DataType::BUILTIN) {
		if (r_target_type.builtin_type == Variant::NIL || r_target_type.builtin_type == Variant::OBJECT) {
			push_error(vformat(R"(Cannot retroactively conform "%s" to a trait.)", Variant::get_type_name(r_target_type.builtin_type)), p_conformance->target);
			return nullptr;
		}
		return resolve_builtin_conformance_shim(p_conformance, r_target_type);
	}

	if (r_target_type.kind == FSParser::DataType::VARIANT) {
		push_error(R"(Retroactive conformance supports only Foundry Script class, native engine-class, and builtin value-type targets.)", p_conformance->target);
		return nullptr;
	}

	// Enum, metatype, and other unsupported targets.
	if (!r_target_type.is_variant()) {
		push_error(R"(Retroactive conformance supports only Foundry Script class, native engine-class, and builtin value-type targets.)", p_conformance->target);
	}
	return nullptr;
}

FSParser::ClassNode *FSAnalyzer::resolve_builtin_conformance_shim(FSParser::ConformanceNode *p_conformance, const FSParser::DataType &p_builtin_type) {
	if (p_conformance->builtin_target_shim != nullptr) {
		return p_conformance->builtin_target_shim;
	}

	FSParser::ClassNode *shim = parser->alloc_recovery_node<FSParser::ClassNode>();
	shim->fqcn = String(Variant::get_type_name(p_builtin_type.builtin_type));
	shim->resolved_interface = true;
	shim->resolved_body = true;
	shim->resolved_trait_uses = true;
	shim->is_builtin_conformance_shim = true;

	FSParser::DataType self_type = p_builtin_type;
	self_type.is_meta_type = false;
	if (self_type.type_source == FSParser::DataType::UNDETECTED) {
		self_type.type_source = FSParser::DataType::ANNOTATED_EXPLICIT;
	}
	shim->set_datatype(self_type);

	p_conformance->builtin_target_shim = shim;
	return shim;
}

FSParser::ClassNode *FSAnalyzer::resolve_native_conformance_shim(FSParser::ConformanceNode *p_conformance, const FSParser::DataType &p_native_type) {
	if (p_conformance->native_target_shim != nullptr) {
		return p_conformance->native_target_shim;
	}

	// The stand-in carries no members and is never registered as a real class; its only job is to make
	// the native class the base so witness `self`/member access resolves against the native surface and
	// the trait-conformance validation reuses the Foundry Script class path. It is owned by the parser
	// and freed with the parse tree.
	FSParser::ClassNode *shim = parser->alloc_recovery_node<FSParser::ClassNode>();
	shim->fqcn = String(p_native_type.native_type);
	shim->extends_used = true;
	shim->base_type = p_native_type;
	shim->base_type.is_meta_type = false;
	if (shim->base_type.type_source == FSParser::DataType::UNDETECTED) {
		shim->base_type.type_source = FSParser::DataType::ANNOTATED_EXPLICIT;
	}
	// Mark every resolution phase done so the shared passes treat it as a fully-resolved, member-less
	// class and never try to (re)parse or look it up in this file's class table.
	shim->resolved_interface = true;
	shim->resolved_body = true;
	shim->resolved_trait_uses = true;
	shim->is_native_conformance_shim = true;

	FSParser::DataType self_type;
	self_type.kind = FSParser::DataType::CLASS;
	self_type.type_source = FSParser::DataType::ANNOTATED_EXPLICIT;
	self_type.class_type = shim;
	// Carry the native base so witness-body member/method resolution against `self` falls through to
	// the engine class's ClassDB surface (mirroring how a real class's datatype records its native
	// base), the same way `resolve_class_inheritance` stamps `class_type.native_type` for real classes.
	self_type.native_type = p_native_type.native_type;
	self_type.is_meta_type = true;
	shim->set_datatype(self_type);

	p_conformance->native_target_shim = shim;
	return shim;
}

HashMap<StringName, FSParser::DataType> FSAnalyzer::conformance_trait_substitution(FSParser::ClassNode *p_trait,
		const FSParser::ClassNode::TraitUse &p_trait_use) {
	HashMap<StringName, FSParser::DataType> bindings;
	if (p_trait == nullptr || p_trait->type_parameters.is_empty() || p_trait_use.resolved_type_arguments.is_empty()) {
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

FSParser::FunctionNode *FSAnalyzer::find_static_conformance_witness(const FSParser::DataType &p_target_type, const StringName &p_method) {
	if (p_method == StringName()) {
		return nullptr;
	}
	const FSConformanceRegistry *registry = FSConformanceRegistry::get_singleton();
	if (registry == nullptr) {
		return nullptr;
	}

	// Only a `static` witness is resolved here. An instance witness is reached through the receiver and
	// already dispatches via the runtime's member-miss fallback; resolving it statically would change
	// how instance calls on a conformed target are typed, which this lookup deliberately leaves alone.
	//
	// The registry's own witness nodes are borrowed from the declaring file's parse tree, which a
	// registration can outlive, so they are never dereferenced here. The registry only reports *where*
	// the conformance was declared; the node is then re-found in a parse tree this analysis holds live.
	auto static_witness_for_key = [&](const String &p_target_key) -> FSParser::FunctionNode * {
		String declaring_file;
		int conformance_index = -1;
		if (!registry->find_witness_location(p_target_key, p_method, declaring_file, conformance_index)) {
			return nullptr;
		}

		FSParser::ClassNode *declaring_class = nullptr;
		if (declaring_file == parser->script_path) {
			declaring_class = parser->head;
		} else {
			const Ref<FSParserRef> declaring_ref =
					dependency_parser_access.depended_parser_for(declaring_file, FSParserRef::INTERFACE_SOLVED);
			if (declaring_ref.is_valid() && declaring_ref->get_parser() != nullptr) {
				declaring_class = declaring_ref->get_parser()->head;
			}
		}
		if (declaring_class == nullptr || conformance_index >= declaring_class->conformances.size()) {
			return nullptr;
		}

		const FSParser::ConformanceNode *conformance = declaring_class->conformances[conformance_index];
		if (conformance == nullptr) {
			return nullptr;
		}
		for (FSParser::FunctionNode *witness : conformance->witnesses) {
			if (witness != nullptr && witness->identifier != nullptr && witness->identifier->name == p_method) {
				return witness->is_static ? witness : nullptr;
			}
		}
		return nullptr;
	};

	// Alias keys mirror the ones the conformance registration records, and the base chain is walked so a
	// witness declared on a base class is reachable through a derived type, matching how the runtime
	// resolves a static witness in `FoundryScript::callp`.
	//
	// The class-identity aliases are tried first, across the whole chain, because a script path is NOT a
	// unique target identity: every class declared in a file, inner classes included, registers that same
	// path. Falling back to it before an FQCN would let a sibling class's witness win a lookup the
	// runtime resolves by identity. The path is only a last resort, for a target that reached here as a
	// bare script reference with no ClassNode.
	for (const FSParser::ClassNode *cursor = p_target_type.class_type; cursor != nullptr; cursor = cursor->base_type.class_type) {
		FSParser::FunctionNode *witness = static_witness_for_key(cursor->fqcn);
		if (witness == nullptr) {
			const StringName global_name = cursor->get_global_name();
			if (global_name != StringName()) {
				witness = static_witness_for_key(String(global_name));
			}
		}
		if (witness != nullptr) {
			return witness;
		}
	}
	if (!p_target_type.script_path.is_empty()) {
		return static_witness_for_key(p_target_type.script_path);
	}
	return nullptr;
}

bool FSAnalyzer::validate_conformance(FSParser::ConformanceNode *p_conformance, FSParser::ClassNode *p_target,
		FSParser::ClassNode *p_trait, const HashMap<StringName, FSParser::DataType> &p_trait_substitution) {
	// Witnesses are looked up by method name; the same name supplied by the target's own surface or by
	// an inherited method also satisfies a requirement.
	HashMap<StringName, FSParser::FunctionNode *> witnesses_by_name;
	for (FSParser::FunctionNode *witness : p_conformance->witnesses) {
		if (witness != nullptr && witness->identifier != nullptr) {
			witnesses_by_name.insert(witness->identifier->name, witness);
		}
	}

	bool valid = true;
	HashSet<StringName> missing_methods;

	// The full requirement set is the directly-applied trait plus its transitive supertraits.
	Vector<FSParser::ClassNode *> requirement_traits;
	requirement_traits.push_back(p_trait);
	for (FSParser::ClassNode *transitive : p_trait->resolved_traits) {
		if (!requirement_traits.has(transitive)) {
			requirement_traits.push_back(transitive);
		}
	}

	for (FSParser::ClassNode *requirement_trait : requirement_traits) {
		resolve_class_interface(requirement_trait, p_conformance);

		// Compose the use-site substitution: the directly-applied trait keeps the conformance binding,
		// a transitive supertrait re-specializes how `p_trait` binds it through that binding.
		HashMap<StringName, FSParser::DataType> substitution;
		if (requirement_trait == p_trait) {
			substitution = p_trait_substitution;
		} else {
			for (const KeyValue<StringName, FSParser::DataType> &entry : trait_type_argument_substitution(p_trait, requirement_trait)) {
				substitution.insert(entry.key, FSParser::DataType::substitute(entry.value, p_trait_substitution));
			}
		}

		for (const FSParser::ClassNode::Member &member : requirement_trait->members) {
			if (member.type != FSParser::ClassNode::Member::FUNCTION || member.function == nullptr) {
				continue;
			}
			FSParser::FunctionNode *required = member.function;
			const StringName function_name = required->identifier != nullptr ? required->identifier->name : StringName();
			if (function_name == StringName()) {
				continue;
			}

			FSParser::FunctionNode *const *witness = witnesses_by_name.getptr(function_name);
			if (witness != nullptr) {
				TraitMethodImplementation implementation;
				implementation.function = *witness;
				implementation.owner_class = p_target;
				if (!validate_trait_method_signature(requirement_trait, p_target, required, implementation, substitution)) {
					valid = false;
				}
				continue;
			}

			// No witness: a concrete trait method already provides a default, an abstract one must be
			// satisfied by the target's existing surface (its own members, base chain, or native base).
			if (!required->is_abstract) {
				continue;
			}

			TraitMethodImplementation implementation;
			if (find_trait_implementation(p_target, function_name, implementation)) {
				if (!validate_trait_method_signature(requirement_trait, p_target, required, implementation, substitution)) {
					valid = false;
				}
				continue;
			}

			if (!missing_methods.has(function_name)) {
				missing_methods.insert(function_name);
				push_error(vformat(R"*(Conformance of "%s" to trait "%s" must implement trait method "%s.%s()".)*",
								   _class_or_trait_name(p_target), _class_or_trait_name(p_trait),
								   _class_or_trait_name(requirement_trait), function_name),
						p_conformance);
			}
			valid = false;
		}
	}

	return valid;
}

void FSAnalyzer::resolve_conformances(FSParser::ClassNode *p_class) {
	require_completed_analyzer_phase(AnalyzerPhase::INTERFACE_AND_MEMBER_SURFACE, AnalyzerPhase::TRAIT_CONFORMANCE_REGISTRATION);

	const String source_file = parser->script_path;

	// Re-analysis of a file replaces its previously-registered conformances wholesale, mirroring how
	// global classes are re-registered, so stale duplicates never accumulate.
	FSConformanceRegistry *registry = FSConformanceRegistry::get_singleton();
	if (p_class == nullptr || p_class->conformances.is_empty()) {
		registry->clear_file(source_file);
		return;
	}

	registry->clear_file(source_file);

	// Track `(target, trait)` pairs declared in this file to reject duplicate conformances locally; a
	// pair already registered by a *different* file is a cross-file duplicate.
	HashSet<String> seen_pairs;
	// Track witness method names per target key within this file to reject same-target witness
	// collisions across different trait conformances.
	HashMap<String, HashMap<StringName, StringName>> seen_witnesses_by_target;
	Vector<FSConformanceRegistry::Conformance> valid_entries;

	for (int conformance_index = 0; conformance_index < p_class->conformances.size(); conformance_index++) {
		FSParser::ConformanceNode *conformance = p_class->conformances[conformance_index];
		if (conformance == nullptr) {
			continue;
		}

		FSParser::DataType target_type;
		FSParser::ClassNode *target = resolve_conformance_target(conformance, target_type);
		if (target == nullptr) {
			continue;
		}

		// The target's own trait uses and interface must be solved before checking redundancy and
		// requirement satisfaction against its existing surface. A synthesized native stand-in is not in
		// this file's class table and carries no members/uses, so its resolution is already complete.
		if (parser->has_class(target)) {
			resolve_trait_uses(target);
			resolve_class_interface(target, conformance);
		}

		const StringName target_global = target->get_global_name();
		Vector<String> target_keys;
		target_keys.push_back(target->fqcn);
		if (target_global != StringName()) {
			target_keys.push_back(String(target_global));
		}
		if (!target_type.script_path.is_empty() && !target_keys.has(target_type.script_path)) {
			target_keys.push_back(target_type.script_path);
		}

		const String target_key = target->fqcn;
		bool conformance_witness_checked = false;
		bool conformance_witness_collision = false;
		StringName witness_trait_label;
		for (FSParser::ClassNode::TraitUse &trait_use : conformance->traits) {
			// The `uses` clause is written in the head file, so resolve trait names in the head's scope.
			FSParser::ClassNode *trait = resolve_trait_reference(parser->head, trait_use, conformance);
			if (trait == nullptr) {
				continue;
			}
			if (resolve_trait_uses(trait, conformance) != OK || resolve_class_inheritance(trait, conformance) != OK) {
				continue;
			}

			// Specialize a generic trait at the conformance site (`uses Container[int]`), resolving the
			// arguments in the head's scope, exactly as an ordinary `uses` clause would.
			if (!trait_use.type_arguments.is_empty() && !trait->type_parameters.is_empty()) {
				FSParser::DataType trait_handle = type_from_metatype(trait->get_datatype());
				trait_handle.is_meta_type = false;
				bool applied = false;
				{
					ScopedCurrentClass current_class_scope(this, parser->head);
					applied = apply_class_type_arguments(trait_handle, trait_use.type_arguments, trait_use.type_arguments[0]);
				}
				if (applied) {
					trait_use.resolved_type_arguments = trait_handle.type_arguments;
				}
			} else if (!trait_use.type_arguments.is_empty() && trait->type_parameters.is_empty()) {
				push_error(vformat(R"(Trait "%s" is not generic and cannot take type arguments.)", _class_or_trait_name(trait)), trait_use.type_arguments[0]);
				continue;
			}

			// The trait's base constraint is an inheritance requirement on the target.
			if (!class_satisfies_trait_base(target, trait)) {
				push_error(vformat(R"(Class "%s" cannot conform to trait "%s" because it does not inherit from "%s".)",
								   _class_or_trait_name(target), _class_or_trait_name(trait),
								   trait->base_type.to_string()),
						conformance);
				continue;
			}

			const StringName trait_identity = fs_trait_identity_name(trait);

			// Coherence: a conformance redundant with the target's own `uses` is rejected.
			bool redundant = false;
			for (FSParser::ClassNode *owned_trait : target->resolved_traits) {
				if (owned_trait == trait || owned_trait->fqcn == trait->fqcn) {
					redundant = true;
					break;
				}
			}
			if (redundant) {
				push_error(vformat(R"(Class "%s" already conforms to trait "%s" through its own "uses"; the conformance is redundant.)",
								   _class_or_trait_name(target), _class_or_trait_name(trait)),
						conformance);
				continue;
			}

			// Coherence: the same `(target, trait)` pair cannot be declared twice, in this file or another.
			const String pair_key = target->fqcn + "\n" + String(trait_identity);
			if (seen_pairs.has(pair_key)) {
				push_error(vformat(R"(Class "%s" already has a conformance to trait "%s" in this file.)",
								   _class_or_trait_name(target), _class_or_trait_name(trait)),
						conformance);
				continue;
			}
			const String other_source = registry->get_conformance_source(target->fqcn, trait_identity);
			if (!other_source.is_empty() && other_source != source_file) {
				push_error(vformat(R"(Class "%s" already conforms to trait "%s" via a conformance in "%s".)",
								   _class_or_trait_name(target), _class_or_trait_name(trait), _localize_script_path(other_source)),
						conformance);
				continue;
			}
			seen_pairs.insert(pair_key);

			if (!conformance_witness_checked) {
				conformance_witness_checked = true;
				HashMap<StringName, StringName> &seen_witnesses = seen_witnesses_by_target[target_key];
				for (FSParser::FunctionNode *witness : conformance->witnesses) {
					if (witness == nullptr || witness->identifier == nullptr) {
						continue;
					}
					const StringName witness_name = witness->identifier->name;
					if (witness_name == StringName()) {
						continue;
					}

					const StringName *existing_trait = seen_witnesses.getptr(witness_name);
					if (existing_trait != nullptr) {
						push_error(vformat(R"*(Class "%s" already provides a witness for method "%s()" through its conformance to trait "%s" in this file.)*",
										   _class_or_trait_name(target), witness_name, String(*existing_trait)),
								conformance);
						conformance_witness_collision = true;
						continue;
					}

					StringName other_trait;
					const String other_witness_source = registry->get_witness_source(target_key, witness_name, other_trait);
					if (!other_witness_source.is_empty() && other_witness_source != source_file) {
						push_error(vformat(R"*(Class "%s" already has a witness for method "%s()" via a conformance in "%s".)*",
										   _class_or_trait_name(target), witness_name, _localize_script_path(other_witness_source)),
								conformance);
						conformance_witness_collision = true;
						continue;
					}
				}
			}
			if (conformance_witness_collision) {
				continue;
			}

			const HashMap<StringName, FSParser::DataType> substitution = conformance_trait_substitution(trait, trait_use);
			if (!validate_conformance(conformance, target, trait, substitution)) {
				continue;
			}

			FSConformanceRegistry::Conformance entry;
			entry.target_keys = target_keys;
			entry.trait_name = trait_identity;
			entry.source_file = source_file;
			entry.conformance_index = conformance_index;
			for (FSParser::FunctionNode *witness : conformance->witnesses) {
				if (witness != nullptr && witness->identifier != nullptr) {
					entry.witnesses.insert(witness->identifier->name, witness);
				}
			}
			valid_entries.push_back(entry);
			if (witness_trait_label == StringName()) {
				witness_trait_label = trait_identity;
			}
		}

		if (witness_trait_label != StringName()) {
			HashMap<StringName, StringName> &seen_witnesses = seen_witnesses_by_target[target_key];
			for (FSParser::FunctionNode *witness : conformance->witnesses) {
				if (witness != nullptr && witness->identifier != nullptr && witness->identifier->name != StringName()) {
					seen_witnesses.insert(witness->identifier->name, witness_trait_label);
				}
			}
		}
	}

	registry->register_file_conformances(source_file, valid_entries);
}

void FSAnalyzer::resolve_conformance_bodies(FSParser::ClassNode *p_class) {
	require_completed_analyzer_phase(AnalyzerPhase::TRAIT_CONFORMANCE_REGISTRATION, AnalyzerPhase::CONFORMANCE_WITNESS_BODY);

	if (p_class == nullptr || p_class->conformances.is_empty()) {
		return;
	}

	for (FSParser::ConformanceNode *conformance : p_class->conformances) {
		if (conformance == nullptr || conformance->target == nullptr) {
			continue;
		}
		// The target was already resolved during the interface pass; reuse the cached datatype.
		const FSParser::DataType target_type = conformance->target->get_datatype();
		FSParser::ClassNode *target = nullptr;
		if (target_type.kind == FSParser::DataType::CLASS) {
			target = target_type.class_type;
		} else if (target_type.kind == FSParser::DataType::SCRIPT && !target_type.script_path.is_empty()) {
			Ref<FSParserRef> ref = parser->get_depended_parser_for(target_type.script_path);
			if (ref.is_valid()) {
				target = ref->get_parser()->head;
			}
		} else if (target_type.kind == FSParser::DataType::NATIVE) {
			// A native target's witnesses are resolved against the synthesized stand-in so `self` and
			// member access bind to the native surface.
			target = conformance->native_target_shim;
		} else if (target_type.kind == FSParser::DataType::BUILTIN) {
			target = conformance->builtin_target_shim;
		}
		if (target == nullptr) {
			continue;
		}

		// Witness bodies are resolved with the implicit `self`/enclosing type bound to the target, so
		// `self`, member access, and type errors inside the witnesses are reported against the target's
		// surface — the same body-resolution path class methods use.
		ScopedCurrentClass current_class_scope(this, target);
		for (FSParser::FunctionNode *witness : conformance->witnesses) {
			if (witness == nullptr) {
				continue;
			}
			resolve_function_signature(witness, witness);
			resolve_function_body(witness);
		}
	}
}
