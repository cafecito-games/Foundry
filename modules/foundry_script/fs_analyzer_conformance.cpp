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

FSAnalyzer::ScopedWitnessScope::ScopedWitnessScope(FSAnalyzer *p_analyzer, FSParser::ClassNode *p_target, FSParser::ClassNode *p_declaration_scope) {
	analyzer = p_analyzer;
	previous_target = analyzer->witness_target_class;
	previous_declaration_scope = analyzer->witness_declaration_scope;
	analyzer->witness_target_class = p_target;
	analyzer->witness_declaration_scope = p_declaration_scope;
}

FSAnalyzer::ScopedWitnessScope::~ScopedWitnessScope() {
	if (analyzer != nullptr) {
		analyzer->witness_target_class = previous_target;
		analyzer->witness_declaration_scope = previous_declaration_scope;
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

FSAnalyzer::ConformanceVisibility::ConformanceVisibility(FSAnalyzer *p_analyzer) {
	analyzer = p_analyzer;
}

bool FSAnalyzer::ConformanceVisibility::can_see(const String &p_source_file) const {
	if (analyzer == nullptr || analyzer->parser == nullptr || p_source_file.is_empty()) {
		return true;
	}
	if (p_source_file == analyzer->parser->script_path || visible_files.has(p_source_file)) {
		return true;
	}

	// Breadth-first over the dependency graph, from two sources per file:
	//
	//  - What it *declares*: the `preload` and `extends` paths its parse tree carries. These come from
	//    the whole file at once, so a `preload` is a dependency no matter where it sits relative to the
	//    code that needs the conformance. Reading the resolved dependencies alone would make visibility
	//    depend on statement order, and an error reported before a later `preload` is never revisited.
	//  - What it has *resolved*: `depended_parsers`, which reaches on through files this analysis has
	//    already pulled in.
	//
	// Every file reached on the way is a dependency too, so the whole visited set is memoized. Only
	// positive answers are kept: the graph grows during an analysis, so a "no" can become a "yes".
	List<FSParser *> pending;
	HashSet<const FSParser *> seen;
	pending.push_back(analyzer->parser);
	seen.insert(analyzer->parser);
	while (!pending.is_empty()) {
		FSParser *current = pending.front()->get();
		pending.pop_front();
		for (const String &declared : current->get_dependencies()) {
			visible_files.insert(declared);
		}
		for (const KeyValue<String, Ref<FSParserRef>> &dependency : current->get_depended_parsers()) {
			visible_files.insert(dependency.key);
			FSParser *dependency_parser = dependency.value.is_valid() ? dependency.value->get_parser() : nullptr;
			if (dependency_parser != nullptr && !seen.has(dependency_parser)) {
				seen.insert(dependency_parser);
				pending.push_back(dependency_parser);
			}
		}
	}
	return visible_files.has(p_source_file);
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
	auto static_witness_for_target = [&](const String &p_target_fqcn) -> FSParser::FunctionNode * {
		String declaring_file;
		int conformance_index = -1;
		if (!registry->find_witness_location(p_target_fqcn, p_method, declaring_file, conformance_index)) {
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

	// A native engine class and a builtin value type are conformance targets too. Each is registered
	// under a synthesized stand-in whose FQCN is the bare class name or `Variant` type name, and the
	// engine inheritance chain is walked so a witness declared on a base class stays reachable from a
	// subclass — the same reach `FSConformanceRegistry::find_native_witness_function` gives the runtime.
	if (p_target_type.kind == FSParser::DataType::NATIVE) {
		for (StringName cursor = p_target_type.native_type; cursor != StringName(); cursor = ClassDB::get_parent_class(cursor)) {
			FSParser::FunctionNode *witness = static_witness_for_target(String(cursor));
			if (witness != nullptr) {
				return witness;
			}
		}
		return nullptr;
	}
	if (p_target_type.kind == FSParser::DataType::BUILTIN) {
		// Builtins have no inheritance chain, so this is an exact lookup.
		return static_witness_for_target(String(Variant::get_type_name(p_target_type.builtin_type)));
	}

	// Lookups go strictly by a target's fully-qualified class name. A conformance is also registered
	// under looser aliases (global class name, script path) that the runtime uses, but those do not
	// identify a class on their own: every class declared in a file, inner classes included, shares the
	// file's path, and a root class without `class_name` has that same path as its FQCN. Matching an
	// alias would let one class answer a call with an unrelated sibling's witness.
	//
	// The base chain is walked so a witness declared on a base class stays reachable through a derived
	// type, matching how the runtime resolves a static witness in `FoundryScript::callp`.
	for (const FSParser::ClassNode *cursor = p_target_type.class_type; cursor != nullptr; cursor = cursor->base_type.class_type) {
		FSParser::FunctionNode *witness = static_witness_for_target(cursor->fqcn);
		if (witness != nullptr) {
			return witness;
		}
	}
	// A target that arrives as a bare script reference has no ClassNode to read an FQCN from, but a root
	// class's FQCN *is* its script path, so the path identifies it exactly.
	if (p_target_type.class_type == nullptr && !p_target_type.script_path.is_empty()) {
		return static_witness_for_target(p_target_type.script_path);
	}
	return nullptr;
}

// Guards a probe already under way on this thread. A probed file resolving its own interface can
// reach an unresolved call on a final receiver and ask the same question; that nested query answers
// from the registry as it stands rather than starting a second, re-entrant sweep.
static thread_local bool indexed_conformance_probe_in_progress = false;

void FSAnalyzer::ensure_indexed_conformance_files_registered() {
	// The conformance registry is process-global and fills up as a side effect of analyzing files, so
	// on a cold run it holds nothing and the hidden-witness diagnostic below cannot fire — the call
	// degrades to an unsafe-access warning and fails at run time instead. The project-wide declaration
	// index knows every conformance-declaring file on disk, so raising those files to
	// `INTERFACE_SOLVED` here makes the diagnostic depend on the project rather than on analysis order.
	//
	// This deliberately does NOT go through `get_depended_parser_for` /
	// `raise_depended_parser_for` the way `raise_declared_conformance_dependencies` does. Those
	// memoize into `depended_parsers`, which `ConformanceVisibility::can_see` walks: a probed file
	// landing there would make its conformances *visible* to this file, type-checking a call with no
	// load edge behind it and reinstating the run-time failure this diagnostic exists to prevent.
	// The probe also passes no cache owner: `FSCache::get_parser`'s owner argument records a *compile*
	// dependency, which `FSCache::finish_compiling` then forces a full load of. Naming this file there
	// would make every conformance file in the project a runtime dependency of it — a load edge with
	// no visibility behind it, the mirror image of the bug. The cost is that a probed file's contents
	// are not part of this file's cache invalidation closure; index freshness is tracked separately.
	//
	// `raise_status()` advances a parser's status before running each phase, so a probed file that
	// leads back here cannot recurse, and its sticky error result makes re-probing a broken file
	// cheap. Anything a probed file fails at stays in its own parser and is never reported here.
	if (indexed_conformance_files_probed) {
		return;
	}
	indexed_conformance_files_probed = true;
	if (indexed_conformance_probe_in_progress) {
		return;
	}
	indexed_conformance_probe_in_progress = true;

	for (const String &conformance_file : FSLanguage::get_singleton()->get_all_conformance_files()) {
		if (conformance_file == parser->script_path) {
			continue;
		}
		Error error = OK;
		Ref<FSParserRef> conformance_ref = FSCache::get_parser(conformance_file, FSParserRef::PARSED, error);
		if (error != OK || conformance_ref.is_null()) {
			continue;
		}
		const FSParser *conformance_parser = conformance_ref->get_parser();
		if (conformance_parser == nullptr || conformance_parser->head == nullptr || conformance_parser->head->conformances.is_empty()) {
			continue;
		}
		conformance_ref->raise_status(FSParserRef::INTERFACE_SOLVED);
	}

	indexed_conformance_probe_in_progress = false;
}

bool FSAnalyzer::reachable_conformance_supplies_method(const FSParser::DataType &p_target_type, const StringName &p_method) {
	if (p_method == StringName()) {
		return false;
	}
	const FSConformanceRegistry *registry = FSConformanceRegistry::get_singleton();
	if (registry == nullptr) {
		return false;
	}

	// Callers ask this on the unresolved-call miss path, where the answer decides whether a call is
	// rejected. It has to reflect the project rather than whatever this process analyzed first, so the
	// index sweep runs here too — it is idempotent per analysis, so a caller that already probed pays
	// nothing.
	ensure_indexed_conformance_files_registered();

	// The base chain is walked because a conformance declared on a base stays reachable through the
	// derived type, matching `find_static_conformance_witness` and the runtime's witness lookup.
	for (const FSParser::ClassNode *cursor = p_target_type.class_type; cursor != nullptr; cursor = cursor->base_type.class_type) {
		String witness_source;
		int witness_conformance_index = -1;
		if (registry->find_witness_location(cursor->fqcn, p_method, witness_source, witness_conformance_index)) {
			return true;
		}
	}
	return false;
}

bool FSAnalyzer::find_hidden_conformance_witness(const FSParser::DataType &p_target_type, const StringName &p_method,
		String &r_source_file, StringName &r_trait_name) {
	r_source_file = String();
	r_trait_name = StringName();
	if (p_method == StringName()) {
		return false;
	}
	const FSConformanceRegistry *registry = FSConformanceRegistry::get_singleton();
	if (registry == nullptr) {
		return false;
	}

	// Only a receiver whose runtime type is exactly its static type is diagnosed. An unresolved method
	// on an open type is legal and merely unsafe: the value may be a subtype that declares the method
	// normally, and the runtime resolves it. Turning that into an error on the strength of a
	// same-named hidden witness would reject working code. A builtin has no subtypes, and neither does
	// a `final` class, so for those the hidden witness is the only thing the call could have meant.
	const bool is_builtin_receiver = p_target_type.kind == FSParser::DataType::BUILTIN;
	if (!is_builtin_receiver && (p_target_type.class_type == nullptr || !p_target_type.class_type->is_final)) {
		return false;
	}

	// Past the receiver gate this is already the unresolved-call miss path, so the sweep that makes
	// the answer order-independent is paid for only by code that is about to be diagnosed anyway.
	ensure_indexed_conformance_files_registered();

	if (is_builtin_receiver) {
		return registry->find_hidden_witness_declaration(String(Variant::get_type_name(p_target_type.builtin_type)),
				p_method, r_source_file, r_trait_name);
	}

	// A conformance this file *can* reach means the call has a well-defined meaning for this file and
	// is left alone — whether it sits below the hidden one (shadowing it) or above (the level the call
	// falls through to). Only a name that no reachable conformance supplies at all is reported, so the
	// diagnostic can never take away a call that works.
	if (reachable_conformance_supplies_method(p_target_type, p_method)) {
		return false;
	}
	for (const FSParser::ClassNode *cursor = p_target_type.class_type; cursor != nullptr; cursor = cursor->base_type.class_type) {
		if (registry->find_hidden_witness_declaration(cursor->fqcn, p_method, r_source_file, r_trait_name)) {
			return true;
		}
	}
	return false;
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
				// A witness signature is resolved here, before its body, and must see the same dual
				// scope the body does: the target first, then the declaring file's type scope. The
				// binding is confined to the witness so the target's own methods, checked on the
				// path below, keep their unaltered scope.
				ScopedWitnessScope witness_scope(this, p_target, parser->head);
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

void FSAnalyzer::raise_declared_conformance_dependencies() {
	// A conformance takes effect when its declaring file is loaded, and `preload`/`extends` load a file
	// for the whole script, not from the statement they appear on. Registration used to happen as a side
	// effect of *reducing* each `preload` expression, so a conformance used above the `preload` that
	// brings it in was not registered yet and the use failed. Register everything this file declares it
	// loads, up front.
	//
	// Only files that actually declare conformances are raised, so this stays a no-op for the vast
	// majority of dependencies. `raise_status()` advances a parser's status before running each phase, so
	// a cycle re-entering the same file returns instead of recursing.
	const String extension = FSLanguage::get_singleton()->get_extension();
	for (const String &dependency_path : parser->get_dependencies()) {
		if (dependency_path.get_extension() != extension || dependency_path == parser->script_path) {
			continue;
		}
		Ref<FSParserRef> dependency_ref;
		if (dependency_parser_access.raise_depended_parser_for(dependency_path, FSParserRef::PARSED, dependency_ref) != OK) {
			continue;
		}
		const FSParser *dependency_parser = dependency_ref.is_valid() ? dependency_ref->get_parser() : nullptr;
		if (dependency_parser == nullptr || dependency_parser->head == nullptr || dependency_parser->head->conformances.is_empty()) {
			continue;
		}
		dependency_parser_access.raise_parser_to_status(dependency_ref, FSParserRef::INTERFACE_SOLVED);
	}
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
			entry.target_fqcn = target->fqcn;
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
		//
		// A witness has a dual scope: the target's member/type scope first, and the lexical type scope
		// of the file declaring this `extend` as a fallback. Native and builtin stand-ins have no
		// lexical outer, and a foreign target's outer chain belongs to another parse tree, so without
		// that fallback a witness could not name the types declared beside it. The fallback supplies
		// only type-bearing declarations and never becomes `parser->current_class`, so the target keeps
		// winning on collisions and the declaring file's values stay out of the target's surface.
		ScopedCurrentClass current_class_scope(this, target);
		ScopedWitnessScope witness_scope(this, target, parser->head);
		for (FSParser::FunctionNode *witness : conformance->witnesses) {
			if (witness == nullptr) {
				continue;
			}
			resolve_function_signature(witness, witness);
			resolve_function_body(witness);
		}
	}
}
