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
#include "fs_diagnostic_names.h"
#include "fs_trait_utils.h"
#include "fs_type.h"

#include "core/config/engine.h"
#include "core/config/project_settings.h"
#include "core/object/class_db.h"
#include "core/object/script_language.h"

// The arguments one conformance supplies for one trait identity in its implied closure, flattened for
// the registry. The direct trait takes the declaration's own arguments; an implied supertrait takes
// its binding by the direct trait, re-expressed in the conformance's terms.
//
// Mirrors `FSCompiler::_conformance_trait_type_arguments()` position for position: the parse-side
// record and the runtime record must describe the same conformance identically, or the analyzer and
// the runtime disagree about what a conformance proved. Both are projected by
// `fs_project_conformance_trait_arguments()` and differ only in how one projected position is stored.
//
// Each position is reduced on its own. A position that stayed open -- unset, or still a bare type
// parameter such as `Self` on a non-final target -- reduces to `RecordedTypeArgument::UNKNOWN`, which
// the comparator treats as an absence of evidence at that position alone. It never erases what a
// concrete sibling position proved.
static Vector<FSConformanceRegistry::RecordedTypeArgument> _recorded_conformance_trait_arguments(
		const FSParser::ClassNode *p_direct_trait,
		const Vector<FSParser::DataType> &p_conformance_arguments,
		const HashMap<StringName, FSParser::DataType> &p_direct_bindings,
		const FSParser::ClassNode *p_identity_trait,
		const FSParser::ClassNode *p_target) {
	Vector<FSParser::DataType> resolved;
	if (!fs_project_conformance_trait_arguments(p_direct_trait, p_conformance_arguments, p_direct_bindings,
				p_identity_trait, p_target, resolved)) {
		return Vector<FSConformanceRegistry::RecordedTypeArgument>();
	}

	Vector<FSConformanceRegistry::RecordedTypeArgument> arguments;
	arguments.resize(resolved.size());
	for (int i = 0; i < resolved.size(); i++) {
		arguments.write[i] = FSConformanceRegistry::reduce_type_argument(resolved[i]);
	}
	return arguments;
}

// Whether two engine classes are on one ClassDB inheritance chain, excluding the class itself. A
// conformance declared on either one answers for receivers of the other, which is what makes two
// declarations along the chain describe the same trait for overlapping values.
static bool _native_classes_are_on_one_chain(const StringName &p_class, const StringName &p_other) {
	return p_class != p_other &&
			(ClassDB::is_parent_class(p_class, p_other) || ClassDB::is_parent_class(p_other, p_class));
}

// Finds a conformance on `p_native_class`'s ClassDB chain that binds `p_identity` to different type
// arguments than `p_applied`.
//
// `native_class_conforms()` resolves membership by walking the chain, nearest conformance first, so
// two declarations along one chain bind the same trait for overlapping receivers. Widening a value to
// the ancestor's type then silently switches which arguments apply: a call typed against the
// ancestor's arguments dispatches the descendant's witness, which is the same incoherence
// `trait_binding_conflicts_with_chain()` rejects for a script-class chain.
//
// `p_pending` carries the conformances the file currently being analyzed has validated so far, and
// `p_source_file`'s own registry entries are dropped: they are what this analysis is about to replace,
// so comparing against them would make an unchanged reanalysis contradict itself.
//
// This is an early diagnostic, not the authority. `FSConformanceRegistry` decides the same question
// again under its own lock when the file's declarations are published, because an answer read here has
// already expired by the time the write happens. Reporting it here is what keeps a rejected
// declaration out of the analysis that follows, so one contradiction produces one diagnostic instead
// of also producing the downstream signature errors a doomed conformance would raise.
//
// Precision is bounded by what the registry can record: whatever `RecordedTypeArgument` cannot
// identify with certainty is an absence of evidence and never a wildcard. That bound is shared with
// the store check in `FSTypeCompatibility`, which reads the same recorded form through the same
// comparator, so widening what the form can carry lifts both at once rather than letting the
// declaration side and the use side diverge.
static bool _native_chain_binding_conflicts(const StringName &p_native_class, const StringName &p_identity,
		const Vector<FSConformanceRegistry::RecordedTypeArgument> &p_applied,
		const Vector<FSConformanceRegistry::Conformance> &p_pending, const String &p_source_file,
		StringName &r_conflicting_class, String &r_conflicting_source) {
	if (p_native_class == StringName() || p_identity == StringName() || p_applied.is_empty()) {
		return false;
	}

	for (const FSConformanceRegistry::Conformance &pending : p_pending) {
		if (pending.trait_name != p_identity || !pending.target_script_path.is_empty()) {
			continue;
		}
		const StringName pending_class = StringName(pending.target_fqcn);
		if (!ClassDB::class_exists(pending_class) || !_native_classes_are_on_one_chain(p_native_class, pending_class)) {
			continue;
		}
		if (FSTypeCompatibility::recorded_arguments_conflict(pending.trait_type_arguments, p_applied)) {
			r_conflicting_class = pending_class;
			r_conflicting_source = pending.source_file;
			return true;
		}
	}

	for (const FSConformanceRegistry::NativeConformanceRecord &record :
			FSConformanceRegistry::get_singleton()->get_native_conformance_records(p_identity, false, p_source_file)) {
		if (!_native_classes_are_on_one_chain(p_native_class, record.native_class)) {
			continue;
		}
		if (FSTypeCompatibility::recorded_arguments_conflict(record.trait_type_arguments, p_applied)) {
			r_conflicting_class = record.native_class;
			r_conflicting_source = record.source_file;
			return true;
		}
	}

	return false;
}

// The one diagnostic every chain-coherence rejection uses, so the same contradiction reads the same
// way whichever of the two declarations is the one being analyzed.
// How a conflicting declaration's location is spelled in a diagnostic: a contradiction with another
// declaration in the file being analyzed reads as "this file", the same way the in-file checks phrase
// it, and any other file is named by its localized path.
static String _conflict_location(const String &p_conflicting_source, const String &p_analyzed_file) {
	return p_conflicting_source == p_analyzed_file
			? String("this file")
			: vformat(R"("%s")", fs_diagnostic_file_reference(p_conflicting_source));
}

static String _chain_conflict_message(const String &p_trait_label, const String &p_conflicting_target,
		const String &p_conflicting_source, const String &p_analyzed_file) {
	const String location = _conflict_location(p_conflicting_source, p_analyzed_file);
	return vformat(R"(Trait "%s" is already applied with different type arguments by the conformance for "%s" in %s; conformances on one inheritance chain must apply it with the same type arguments.)",
			p_trait_label, p_conflicting_target, location);
}

// Whether a conformance declared on `p_declared_on` answers for receivers whose static chain bottoms
// out at `p_terminal`. Unlike the engine-only rule, this is one-directional: a script class extending
// `RefCounted` is answered for by `RefCounted` and by every `RefCounted` ancestor, but never by a
// `RefCounted` *subclass* it has no values in common with.
static bool _native_ancestry_answers_for(const StringName &p_terminal, const StringName &p_declared_on) {
	if (p_terminal == StringName() || p_declared_on == StringName()) {
		return false;
	}
	return p_terminal == p_declared_on || ClassDB::is_parent_class(p_terminal, p_declared_on);
}

// The engine class a script class's inheritance chain bottoms out at, or an empty name when the chain
// cannot be followed to one. Follows the same steps `FSTypeCompatibility` walks when it reads a
// retroactive conformance off a chain, so a class's semantic chain has one terminus for both.
static StringName _terminal_native_class(const FSParser::ClassNode *p_class) {
	const FSParser::ClassNode *current = p_class;
	int depth = 0;
	while (current != nullptr) {
		if (unlikely(depth++ > Variant::MAX_RECURSION_DEPTH)) {
			return StringName();
		}
		const FSParser::DataType &base = current->base_type;
		if (base.kind == FSParser::DataType::CLASS) {
			current = base.class_type;
		} else if (base.kind == FSParser::DataType::SCRIPT && base.script_type.is_valid()) {
			return base.script_type->get_instance_base_type();
		} else if (base.kind == FSParser::DataType::NATIVE) {
			return base.native_type;
		} else {
			return StringName();
		}
	}
	return StringName();
}

// Every script class above `p_class` on its inheritance chain, identified the way the registry
// identifies a conformance target. A base reached as a bare script reference has no `ClassNode` to
// read a fully-qualified name from, so it contributes the identities a root class can be registered
// under — its path, and its global name when it has one — and the walk stops there rather than
// guessing at what lies further up.
static Vector<String> _script_ancestor_keys(const FSParser::ClassNode *p_class) {
	Vector<String> keys;
	if (p_class == nullptr) {
		return keys;
	}
	int depth = 0;
	FSParser::DataType current = p_class->base_type;
	while (depth++ <= Variant::MAX_RECURSION_DEPTH) {
		if (current.kind == FSParser::DataType::CLASS && current.class_type != nullptr) {
			if (!current.class_type->fqcn.is_empty()) {
				keys.push_back(current.class_type->fqcn);
			}
			current = current.class_type->base_type;
			continue;
		}
		if (current.kind == FSParser::DataType::SCRIPT) {
			if (!current.script_path.is_empty()) {
				keys.push_back(current.script_path);
			}
			if (current.script_type.is_valid()) {
				const StringName global_name = current.script_type->get_global_name();
				if (global_name != StringName()) {
					keys.push_back(String(global_name));
				}
			}
		}
		break;
	}
	return keys;
}

// The arguments `p_class` binds `p_identity_trait`'s own type parameters to through its `uses`
// clauses, flattened the way the registry records a conformance's arguments. Empty when the class
// applies the trait without ever supplying arguments, which proves nothing.
static Vector<FSConformanceRegistry::RecordedTypeArgument> _recorded_class_trait_arguments(
		const FSParser::ClassNode *p_class, const FSParser::ClassNode *p_identity_trait) {
	Vector<FSConformanceRegistry::RecordedTypeArgument> arguments;
	if (p_class == nullptr || p_identity_trait == nullptr || p_identity_trait->type_parameters.is_empty()) {
		return arguments;
	}
	const HashMap<StringName, FSParser::DataType> bindings =
			fs_trait_type_argument_bindings(p_class, p_identity_trait);
	if (bindings.is_empty()) {
		return arguments;
	}
	for (const FSParser::TypeParameterNode *type_parameter : p_identity_trait->type_parameters) {
		FSParser::DataType argument;
		if (type_parameter != nullptr && type_parameter->identifier != nullptr) {
			const FSParser::DataType *bound = bindings.getptr(type_parameter->identifier->name);
			if (bound != nullptr) {
				argument = *bound;
			}
		}
		arguments.push_back(FSConformanceRegistry::reduce_type_argument(
				fs_reify_self_in_trait_argument(p_class, argument)));
	}
	return arguments;
}

// Every class declared in one file, outer class first, so a rule that is about a whole file's
// declarations does not depend on where in the file a class was nested.
static void _collect_declared_classes(const FSParser::ClassNode *p_class, Vector<const FSParser::ClassNode *> &r_classes) {
	if (p_class == nullptr) {
		return;
	}
	r_classes.push_back(p_class);
	for (const FSParser::ClassNode::Member &member : p_class->members) {
		if (member.type == FSParser::ClassNode::Member::CLASS) {
			_collect_declared_classes(member.m_class, r_classes);
		}
	}
}

// Every class-`uses` binding one file fixes, flattened the way the registry records it so another file
// can be judged against it without re-loading this file's parse tree.
//
// Only a binding that actually supplied arguments is recorded: an empty argument list proves nothing
// about the trait's parameters, and recording it would state a constraint the source never made. A
// trait's whole implied identity closure is walked, because a class that binds a subtrait binds every
// supertrait identity that subtrait forwards to.
static Vector<FSConformanceRegistry::ClassTraitBinding> _collect_class_trait_bindings(
		const FSParser::ClassNode *p_head, const String &p_source_file) {
	Vector<FSConformanceRegistry::ClassTraitBinding> bindings;
	Vector<const FSParser::ClassNode *> declared_classes;
	_collect_declared_classes(p_head, declared_classes);
	for (const FSParser::ClassNode *declared : declared_classes) {
		if (declared->is_trait || declared->is_native_conformance_shim || declared->is_builtin_conformance_shim ||
				!declared->resolved_trait_uses || declared->fqcn.is_empty() || declared->resolved_traits.is_empty()) {
			continue;
		}
		const StringName native_base = _terminal_native_class(declared);
		const Vector<String> ancestor_keys = _script_ancestor_keys(declared);
		HashSet<StringName> recorded_identities;
		for (const FSParser::ClassNode *applied_trait : declared->resolved_traits) {
			for (const FSParser::ClassNode *identity_node : fs_trait_identity_closure_nodes(applied_trait)) {
				const StringName identity = fs_trait_identity_name(identity_node);
				if (identity == StringName() || recorded_identities.has(identity)) {
					continue;
				}
				const Vector<FSConformanceRegistry::RecordedTypeArgument> arguments =
						_recorded_class_trait_arguments(declared, identity_node);
				if (arguments.is_empty()) {
					continue;
				}
				recorded_identities.insert(identity);
				FSConformanceRegistry::ClassTraitBinding binding;
				binding.target_fqcn = declared->fqcn;
				binding.target_label = fs_class_or_trait_diagnostic_name(declared);
				binding.target_native_base = native_base;
				binding.target_script_ancestor_fqcns = ancestor_keys;
				binding.trait_name = identity;
				binding.trait_label = fs_class_or_trait_diagnostic_name(identity_node);
				binding.trait_type_arguments = arguments;
				binding.source_file = p_source_file;
				bindings.push_back(binding);
			}
		}
	}
	return bindings;
}

bool FSAnalyzer::trait_binding_conflicts_with_native_ancestry(const FSParser::ClassNode *p_class,
		const FSParser::ClassNode *p_identity_trait,
		const Vector<FSConformanceRegistry::RecordedTypeArgument> &p_applied,
		const Vector<FSConformanceRegistry::Conformance> &p_pending, String &r_message) const {
	// A trait declaration has no receivers of its own; the classes that apply it are where its binding
	// meets an engine chain, and each of those is checked in its own right.
	if (p_class == nullptr || p_class->is_trait || p_identity_trait == nullptr || p_applied.is_empty()) {
		return false;
	}
	const StringName terminal = _terminal_native_class(p_class);
	const StringName identity = fs_trait_identity_name(p_identity_trait);
	if (terminal == StringName() || identity == StringName()) {
		return false;
	}

	const String source_file = parser->script_path;
	for (const FSConformanceRegistry::Conformance &pending : p_pending) {
		if (pending.trait_name != identity || !pending.target_script_path.is_empty()) {
			continue;
		}
		const StringName pending_class = StringName(pending.target_fqcn);
		if (!ClassDB::class_exists(pending_class) || !_native_ancestry_answers_for(terminal, pending_class)) {
			continue;
		}
		if (FSTypeCompatibility::recorded_arguments_conflict(pending.trait_type_arguments, p_applied)) {
			r_message = _chain_conflict_message(fs_class_or_trait_diagnostic_name(p_identity_trait), String(pending_class),
					pending.source_file, source_file);
			return true;
		}
	}

	// This file's own registry entries are either stale (an earlier analysis of it) or superseded by
	// `p_pending`, so they are dropped rather than compared: only other files contribute here.
	for (const FSConformanceRegistry::NativeConformanceRecord &record :
			FSConformanceRegistry::get_singleton()->get_native_conformance_records(identity, true, source_file)) {
		if (!_native_ancestry_answers_for(terminal, record.native_class)) {
			continue;
		}
		if (FSTypeCompatibility::recorded_arguments_conflict(record.trait_type_arguments, p_applied)) {
			r_message = _chain_conflict_message(fs_class_or_trait_diagnostic_name(p_identity_trait), String(record.native_class),
					record.source_file, source_file);
			return true;
		}
	}

	return false;
}

bool FSAnalyzer::trait_binding_conflicts_with_script_ancestry(const FSParser::ClassNode *p_class,
		const FSParser::ClassNode *p_identity_trait,
		const Vector<FSConformanceRegistry::RecordedTypeArgument> &p_applied,
		const Vector<FSConformanceRegistry::Conformance> &p_pending, String &r_message) const {
	// A trait declaration has no receivers of its own, and an engine or builtin stand-in sits on no
	// script chain: the engine half of the rule answers for those.
	if (p_class == nullptr || p_class->is_trait || p_class->is_native_conformance_shim ||
			p_class->is_builtin_conformance_shim || p_identity_trait == nullptr || p_applied.is_empty()) {
		return false;
	}
	const StringName identity = fs_trait_identity_name(p_identity_trait);
	if (identity == StringName() || p_class->fqcn.is_empty()) {
		return false;
	}

	const String source_file = parser->script_path;
	const String trait_label = fs_class_or_trait_diagnostic_name(p_identity_trait);
	const Vector<String> ancestor_keys = _script_ancestor_keys(p_class);
	// Either declaration may be the one being analyzed, so both directions of the chain relation are
	// asked: the other target may stand above `p_class` or below it.
	const auto answers_for_same_receivers = [&](const String &p_other_fqcn,
													const Vector<String> &p_other_ancestor_keys) {
		return !p_other_fqcn.is_empty() && p_other_fqcn != p_class->fqcn &&
				(ancestor_keys.has(p_other_fqcn) || p_other_ancestor_keys.has(p_class->fqcn));
	};

	for (const FSConformanceRegistry::Conformance &pending : p_pending) {
		if (pending.trait_name != identity || pending.target_script_path.is_empty() ||
				!answers_for_same_receivers(pending.target_fqcn, pending.target_script_ancestor_fqcns)) {
			continue;
		}
		if (FSTypeCompatibility::recorded_arguments_conflict(pending.trait_type_arguments, p_applied)) {
			r_message = _chain_conflict_message(trait_label, pending.target_label, pending.source_file, source_file);
			return true;
		}
	}

	// A class's own `uses` clause registers nothing, so a descendant declared in this file is read from
	// the parse tree. Only the descendant direction is read here: a binding an *ancestor* class carries
	// through its own `uses` is what `trait_binding_conflicts_with_chain()` already rejects, with the
	// diagnostic that names the two argument lists.
	Vector<const FSParser::ClassNode *> declared_classes;
	_collect_declared_classes(parser->head, declared_classes);
	for (const FSParser::ClassNode *declared : declared_classes) {
		if (declared == p_class || declared->is_trait || !declared->resolved_trait_uses ||
				!_script_ancestor_keys(declared).has(p_class->fqcn)) {
			continue;
		}
		const Vector<FSConformanceRegistry::RecordedTypeArgument> used =
				_recorded_class_trait_arguments(declared, p_identity_trait);
		if (FSTypeCompatibility::recorded_arguments_conflict(used, p_applied)) {
			r_message = _chain_conflict_message(trait_label, fs_class_or_trait_diagnostic_name(declared), source_file, source_file);
			return true;
		}
	}

	for (const FSConformanceRegistry::ScriptConformanceRecord &record :
			FSConformanceRegistry::get_singleton()->get_script_conformance_records(identity, true, source_file)) {
		if (!answers_for_same_receivers(record.target_fqcn, record.target_script_ancestor_fqcns)) {
			continue;
		}
		if (FSTypeCompatibility::recorded_arguments_conflict(record.trait_type_arguments, p_applied)) {
			r_message = _chain_conflict_message(trait_label, record.target_label, record.source_file, source_file);
			return true;
		}
	}

	// A class in a loaded file binds the trait for its own chain through its `uses` clause the same way a
	// conformance on that chain would, and registers no conformance to be found above.
	for (const FSConformanceRegistry::ClassTraitBinding &binding :
			FSConformanceRegistry::get_singleton()->get_script_trait_binding_records(identity, true, source_file)) {
		if (!answers_for_same_receivers(binding.target_fqcn, binding.target_script_ancestor_fqcns)) {
			continue;
		}
		if (FSTypeCompatibility::recorded_arguments_conflict(binding.trait_type_arguments, p_applied)) {
			r_message = _chain_conflict_message(trait_label, binding.target_label, binding.source_file, source_file);
			return true;
		}
	}

	return false;
}

bool FSAnalyzer::native_conformance_conflicts_with_script_chain(const StringName &p_native_class,
		const FSParser::ClassNode *p_identity_trait,
		const Vector<FSConformanceRegistry::RecordedTypeArgument> &p_applied,
		const Vector<FSConformanceRegistry::Conformance> &p_pending, String &r_message) const {
	if (p_native_class == StringName() || p_identity_trait == nullptr || p_applied.is_empty()) {
		return false;
	}
	const StringName identity = fs_trait_identity_name(p_identity_trait);
	if (identity == StringName()) {
		return false;
	}

	const String source_file = parser->script_path;
	const String trait_label = fs_class_or_trait_diagnostic_name(p_identity_trait);

	for (const FSConformanceRegistry::Conformance &pending : p_pending) {
		if (pending.trait_name != identity || pending.target_script_path.is_empty() ||
				!_native_ancestry_answers_for(pending.target_native_base, p_native_class)) {
			continue;
		}
		if (FSTypeCompatibility::recorded_arguments_conflict(pending.trait_type_arguments, p_applied)) {
			r_message = _chain_conflict_message(trait_label, pending.target_label, pending.source_file, source_file);
			return true;
		}
	}

	// A class's own `uses` clause registers nothing, so this file's classes are read from the parse
	// tree. Reading the whole file rather than the declarations seen so far is what keeps the answer
	// independent of where in the file each declaration sits.
	Vector<const FSParser::ClassNode *> declared_classes;
	_collect_declared_classes(parser->head, declared_classes);
	for (const FSParser::ClassNode *declared : declared_classes) {
		if (declared->is_trait || !declared->resolved_trait_uses ||
				!_native_ancestry_answers_for(_terminal_native_class(declared), p_native_class)) {
			continue;
		}
		const Vector<FSConformanceRegistry::RecordedTypeArgument> used =
				_recorded_class_trait_arguments(declared, p_identity_trait);
		if (FSTypeCompatibility::recorded_arguments_conflict(used, p_applied)) {
			r_message = _chain_conflict_message(trait_label, fs_class_or_trait_diagnostic_name(declared), source_file, source_file);
			return true;
		}
	}

	for (const FSConformanceRegistry::ScriptConformanceRecord &record :
			FSConformanceRegistry::get_singleton()->get_script_conformance_records(identity, true, source_file)) {
		if (!_native_ancestry_answers_for(record.target_native_base, p_native_class)) {
			continue;
		}
		if (FSTypeCompatibility::recorded_arguments_conflict(record.trait_type_arguments, p_applied)) {
			r_message = _chain_conflict_message(trait_label, record.target_label, record.source_file, source_file);
			return true;
		}
	}

	// A class in a loaded file whose chain bottoms out on this engine class, or on a subclass of it, binds
	// the trait for receivers this declaration also answers for. Its `uses` clause registers no
	// conformance, so the binding is read from the records the declaring file published for it.
	for (const FSConformanceRegistry::ClassTraitBinding &binding :
			FSConformanceRegistry::get_singleton()->get_script_trait_binding_records(identity, true, source_file)) {
		if (!_native_ancestry_answers_for(binding.target_native_base, p_native_class)) {
			continue;
		}
		if (FSTypeCompatibility::recorded_arguments_conflict(binding.trait_type_arguments, p_applied)) {
			r_message = _chain_conflict_message(trait_label, binding.target_label, binding.source_file, source_file);
			return true;
		}
	}

	return false;
}

void FSAnalyzer::check_trait_uses_against_conformance_chain(FSParser::ClassNode *p_class) {
	if (p_class == nullptr || p_class->is_trait || !p_class->resolved_trait_uses) {
		return;
	}
	for (const FSParser::ClassNode *applied_trait : p_class->resolved_traits) {
		String message;
		const Vector<FSConformanceRegistry::RecordedTypeArgument> applied =
				_recorded_class_trait_arguments(p_class, applied_trait);
		if (trait_binding_conflicts_with_native_ancestry(p_class, applied_trait, applied,
					Vector<FSConformanceRegistry::Conformance>(), message) ||
				trait_binding_conflicts_with_script_ancestry(p_class, applied_trait, applied,
						Vector<FSConformanceRegistry::Conformance>(), message)) {
			push_error(message, p_class);
			reported_trait_use_chain_conflicts.insert(p_class->fqcn);
			return;
		}
	}
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

FSParser::FunctionNode *FSAnalyzer::find_conformance_witness(const FSParser::DataType &p_target_type, const StringName &p_method) {
	if (p_method == StringName()) {
		return nullptr;
	}
	const FSConformanceRegistry *registry = FSConformanceRegistry::get_singleton();
	if (registry == nullptr) {
		return nullptr;
	}

	// A witness from a retroactive conformance — `static` or instance — is resolved here. The runtime
	// reaches an instance witness through the receiver's member-miss fallback and a `static` witness
	// through the type, so analysis resolves the exact same function each dispatch will run; resolving
	// both up front keeps the analyzer and the runtime in agreement on a conformed target.
	//
	// The registry's own witness nodes are borrowed from the declaring file's parse tree, which a
	// registration can outlive, so they are never dereferenced here. The registry only reports *where*
	// the conformance was declared; the node is then re-found in a parse tree this analysis holds live.
	auto witness_for_target = [&](const String &p_target_fqcn) -> FSParser::FunctionNode * {
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
				return witness;
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
			FSParser::FunctionNode *witness = witness_for_target(String(cursor));
			if (witness != nullptr) {
				return witness;
			}
		}
		return nullptr;
	}
	if (p_target_type.kind == FSParser::DataType::BUILTIN) {
		// Builtins have no inheritance chain, so this is an exact lookup.
		return witness_for_target(String(Variant::get_type_name(p_target_type.builtin_type)));
	}

	// Lookups go strictly by a target's fully-qualified class name. A conformance is also registered
	// under looser aliases (global class name, script path) that the runtime uses, but those do not
	// identify a class on their own: every class declared in a file, inner classes included, shares the
	// file's path, and a root class without `class_name` has that same path as its FQCN. Matching an
	// alias would let one class answer a call with an unrelated sibling's witness.
	//
	// The base chain is walked so a witness declared on a base class stays reachable through a derived
	// type, matching how the runtime resolves a witness in `FoundryScript::callp`.
	for (const FSParser::ClassNode *cursor = p_target_type.class_type; cursor != nullptr; cursor = cursor->base_type.class_type) {
		FSParser::FunctionNode *witness = witness_for_target(cursor->fqcn);
		if (witness != nullptr) {
			return witness;
		}
	}
	// A target that arrives as a bare script reference has no ClassNode to read an FQCN from, but a root
	// class's FQCN *is* its script path, so the path identifies it exactly.
	if (p_target_type.class_type == nullptr && !p_target_type.script_path.is_empty()) {
		return witness_for_target(p_target_type.script_path);
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

// A type parameter stands for whatever satisfies its bound, and a conformance declared on the bound
// is reachable through every such value, so both witness questions are answered against the bound.
// The substitution is a single step, matching what member resolution itself does: a parameter bounded
// by another parameter is left alone rather than chased to the chain's root. Only a class bound is
// substituted — a builtin bound would otherwise reach the builtin arm of the hidden-witness gate,
// which answers for a receiver that really is that builtin, and would newly diagnose calls the
// builtin's own member resolution already handles.
static FSParser::DataType _conformance_target_through_type_parameter(const FSParser::DataType &p_type) {
	if (p_type.kind == FSParser::DataType::TYPE_PARAMETER && !p_type.type_parameter_bound.is_empty() &&
			p_type.type_parameter_bound[0].kind == FSParser::DataType::CLASS) {
		return p_type.type_parameter_bound[0];
	}
	return p_type;
}

bool FSAnalyzer::reachable_conformance_supplies_method(const FSParser::DataType &p_target_type, const StringName &p_method) {
	const FSParser::DataType target_type = _conformance_target_through_type_parameter(p_target_type);
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
	// derived type, matching `find_conformance_witness` and the runtime's witness lookup.
	for (const FSParser::ClassNode *cursor = target_type.class_type; cursor != nullptr; cursor = cursor->base_type.class_type) {
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
	const FSParser::DataType target_type = _conformance_target_through_type_parameter(p_target_type);
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
	// a `final` class, so for those the hidden witness is the only thing the call could have meant. A
	// type parameter is judged by the bound it was normalized to above, which is closed exactly when
	// the bound class is.
	const bool is_builtin_receiver = target_type.kind == FSParser::DataType::BUILTIN;
	if (!is_builtin_receiver && (target_type.class_type == nullptr || !target_type.class_type->is_final)) {
		return false;
	}

	// Past the receiver gate this is already the unresolved-call miss path, so the sweep that makes
	// the answer order-independent is paid for only by code that is about to be diagnosed anyway.
	ensure_indexed_conformance_files_registered();

	if (is_builtin_receiver) {
		return registry->find_hidden_witness_declaration(String(Variant::get_type_name(target_type.builtin_type)),
				p_method, r_source_file, r_trait_name);
	}

	// A conformance this file *can* reach means the call has a well-defined meaning for this file and
	// is left alone — whether it sits below the hidden one (shadowing it) or above (the level the call
	// falls through to). Only a name that no reachable conformance supplies at all is reported, so the
	// diagnostic can never take away a call that works.
	if (reachable_conformance_supplies_method(target_type, p_method)) {
		return false;
	}
	for (const FSParser::ClassNode *cursor = target_type.class_type; cursor != nullptr; cursor = cursor->base_type.class_type) {
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
			for (const KeyValue<StringName, FSParser::DataType> &entry : fs_trait_type_argument_bindings(p_trait, requirement_trait)) {
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
								   fs_class_or_trait_diagnostic_name(p_target), fs_class_or_trait_diagnostic_name(p_trait),
								   fs_class_or_trait_diagnostic_name(requirement_trait), function_name),
						p_conformance);
			}
			valid = false;
		}
	}

	return valid;
}

// Whether a parsed file declares a class that applies any trait, anywhere in its class tree.
//
// Deliberately coarser than what `_collect_class_trait_bindings()` ends up recording. This runs on a
// merely parsed tree, where no `uses` clause has been resolved yet, so the trait a clause names — and
// with it the identity closure the binding is actually read off — is not known. A clause that writes no
// type arguments of its own still binds a generic trait whenever the trait it names specializes one
// (`trait IntKeeper uses Keeper[int]`), and that specialization is only visible after resolution.
//
// Answering "declares a `uses` at all" is the smallest question that can be answered here and can never
// be narrower than what collection records, so the prescan cannot skip a file whose bindings would have
// mattered. Over-answering only raises a file whose recorded bindings then turn out to be empty, which
// costs an interface resolution and states nothing.
static bool _declares_class_trait_use(const FSParser::ClassNode *p_class) {
	if (p_class == nullptr) {
		return false;
	}
	// Only a class binds a trait for receivers; a trait declaration has none of its own.
	if (!p_class->is_trait && !p_class->used_traits.is_empty()) {
		return true;
	}
	for (const FSParser::ClassNode::Member &member : p_class->members) {
		if (member.type == FSParser::ClassNode::Member::CLASS && _declares_class_trait_use(member.m_class)) {
			return true;
		}
	}
	return false;
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
	//
	// This file's own conformances are also checked for coherence against what its dependencies bind, and
	// a dependency binds a trait through a plain `uses` clause as much as through an `extend`. A class's
	// `uses` registers nothing until that file reaches conformance registration, so a dependency that
	// only binds is raised too — but only on behalf of a file that has an `extend` to judge, so an
	// ordinary file never pulls its dependencies' interfaces forward for a comparison it will not make.
	//
	// Loading composes: a file two hops away is loaded by this one exactly as a direct dependency is, and
	// binds the chains this file's declarations sit on just the same. For a file with conformances to
	// judge, the walk therefore follows the whole load closure rather than stopping at a direct
	// dependency that happens to declare nothing itself. `visited` is what terminates it, since the load
	// graph may contain cycles.
	const bool declares_conformances = parser->head != nullptr && !parser->head->conformances.is_empty();
	const String extension = FSLanguage::get_singleton()->get_extension();
	List<String> frontier = parser->get_dependencies();
	HashSet<String> visited;
	visited.insert(parser->script_path);
	loaded_dependency_closure.clear();
	while (!frontier.is_empty()) {
		const String dependency_path = frontier.front()->get();
		frontier.pop_front();
		if (dependency_path.get_extension() != extension || visited.has(dependency_path)) {
			continue;
		}
		visited.insert(dependency_path);
		loaded_dependency_closure.insert(dependency_path);
		Ref<FSParserRef> dependency_ref;
		if (dependency_parser_access.raise_depended_parser_for(dependency_path, FSParserRef::PARSED, dependency_ref) != OK) {
			continue;
		}
		const FSParser *dependency_parser = dependency_ref.is_valid() ? dependency_ref->get_parser() : nullptr;
		if (dependency_parser == nullptr || dependency_parser->head == nullptr) {
			continue;
		}
		if (declares_conformances) {
			for (const String &nested_path : dependency_parser->get_dependencies()) {
				if (!visited.has(nested_path)) {
					frontier.push_back(nested_path);
				}
			}
		}
		if (dependency_parser->head->conformances.is_empty() &&
				!(declares_conformances && _declares_class_trait_use(dependency_parser->head))) {
			continue;
		}
		dependency_parser_access.raise_parser_to_status(dependency_ref, FSParserRef::INTERFACE_SOLVED);
	}
}

void FSAnalyzer::report_binding_chain_conflicts(const FSConformanceRegistry::RegistrationResult &p_result,
		const String &p_source_file) {
	if (p_result.binding_conflicts.is_empty()) {
		return;
	}
	Vector<const FSParser::ClassNode *> declared_classes;
	_collect_declared_classes(parser->head, declared_classes);
	for (const FSConformanceRegistry::BindingConflict &conflict : p_result.binding_conflicts) {
		// A class the `uses` check already reported on has been told about this contradiction once; the
		// registry's record of it is the same one seen again, not a second one.
		if (reported_trait_use_chain_conflicts.has(conflict.target_fqcn)) {
			continue;
		}
		const FSParser::ClassNode *binding_class = nullptr;
		for (const FSParser::ClassNode *declared : declared_classes) {
			if (declared->fqcn == conflict.target_fqcn) {
				binding_class = declared;
				break;
			}
		}
		if (binding_class == nullptr) {
			continue;
		}
		reported_trait_use_chain_conflicts.insert(conflict.target_fqcn);
		push_error(_chain_conflict_message(conflict.trait_label, conflict.conflicting_target_label,
						   conflict.conflicting_source_file, p_source_file),
				binding_class);
	}
}

void FSAnalyzer::resolve_conformances(FSParser::ClassNode *p_class) {
	require_completed_analyzer_phase(AnalyzerPhase::INTERFACE_AND_MEMBER_SURFACE, AnalyzerPhase::TRAIT_CONFORMANCE_REGISTRATION);

	const String source_file = parser->script_path;

	// Re-analysis of a file replaces its previously-registered conformances wholesale, mirroring how
	// global classes are re-registered, so stale duplicates never accumulate. The replacement is one
	// atomic registry operation rather than a clear followed by a later write: nothing observes this
	// file's conformances as absent while it is being reanalyzed, and the cross-file conflicts the
	// registry is authoritative for are decided against the store as it stands at the moment of the
	// write. The checks below run against a registry that still holds this file's previous entries, so
	// each of them drops `source_file` from what it compares against.
	FSConformanceRegistry *registry = FSConformanceRegistry::get_singleton();
	// What this file's classes bind through their own `uses` clauses. Published with the conformances, in
	// the same atomic replacement, so a file that declares only `uses` still constrains the chains it
	// binds — a file that declares no conformance at all is exactly the case the registry could not see.
	const Vector<FSConformanceRegistry::ClassTraitBinding> trait_bindings =
			_collect_class_trait_bindings(parser->head, source_file);
	if (p_class == nullptr || p_class->conformances.is_empty()) {
		// The load closure is published here too: a file that declares only `uses` is still one end of the
		// edge that licenses comparing its bindings against another file's conformance, and it is the only
		// side that can record that edge.
		report_binding_chain_conflicts(
				registry->try_replace_file_conformances(source_file,
						Vector<FSConformanceRegistry::Conformance>(), trait_bindings, loaded_dependency_closure),
				source_file);
		return;
	}

	// This file's previous declarations stay in the store for every other reader, but they are what this
	// pass is about to replace, so they are hidden from this thread: without that, a witness would find
	// its own stale registration as the method it overrides and contradict itself.
	FSConformanceRegistry::ScopedInFlightReplacement in_flight_replacement(source_file);

	// Track the declaration that first emitted each `(target, trait)` membership. Two paths through
	// the same declaration may share an implied supertrait (a diamond), but an explicit duplicate or
	// overlap with another declaration is incoherent.
	HashMap<String, int> seen_membership_conformances;
	// Track witness method names per target key within this file to reject same-target witness
	// collisions across different trait conformances.
	HashMap<String, HashMap<StringName, StringName>> seen_witnesses_by_target;
	Vector<FSConformanceRegistry::Conformance> valid_entries;
	// Declarations an early cross-file diagnostic already reported. The registry re-decides the same
	// conflicts authoritatively and returns its own records; one of them naming a declaration already
	// reported here is the same contradiction seen twice, not a second one.
	HashSet<int> reported_declarations;
	// How each trait identity this file emits should be spelled in a diagnostic. The registry answers
	// with identity names, which are fully qualified for a trait declared without an explicit name, so
	// the label is remembered here where the parse tree is at hand.
	HashMap<StringName, String> identity_labels;

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
					applied = apply_type_arguments(trait_handle, trait_generic_declaration(trait), trait_use.type_arguments, trait_use.type_arguments[0]);
				}
				if (applied) {
					trait_use.resolved_type_arguments = trait_handle.type_arguments;
				}
			} else if (!trait_use.type_arguments.is_empty() && trait->type_parameters.is_empty()) {
				push_error(vformat(R"(Trait "%s" is not generic and cannot take type arguments.)", fs_class_or_trait_diagnostic_name(trait)), trait_use.type_arguments[0]);
				continue;
			} else if (trait_use.type_arguments.is_empty() && !trait->type_parameters.is_empty()) {
				// A generic trait must be applied with type arguments, at a conformance site as much as
				// at an ordinary `uses`. Conformances only ever resolve in the declaring file's analyzer,
				// so this error needs no deferral to stay order-stable.
				push_error(vformat(R"(Generic trait "%s" expects %d type argument(s), but 0 were given.)",
								   fs_class_or_trait_diagnostic_name(trait), trait->type_parameters.size()),
						trait_use.name.is_empty() ? static_cast<const FSParser::Node *>(conformance) : static_cast<const FSParser::Node *>(trait_use.name[0]));
				continue;
			}

			// The trait's base constraint is an inheritance requirement on the target.
			if (!class_satisfies_trait_base(target, trait)) {
				push_error(vformat(R"(Class "%s" cannot conform to trait "%s" because it does not inherit from "%s".)",
								   fs_class_or_trait_diagnostic_name(target), fs_class_or_trait_diagnostic_name(trait),
								   trait->base_type.to_string()),
						conformance);
				continue;
			}

			const StringName trait_identity = fs_trait_identity_name(trait);
			const Vector<FSParser::ClassNode *> trait_identity_nodes = fs_trait_identity_closure_nodes(trait);
			Vector<StringName> trait_identities;
			for (const FSParser::ClassNode *identity_node : trait_identity_nodes) {
				trait_identities.push_back(fs_trait_identity_name(identity_node));
			}
			if (trait_identities.is_empty()) {
				continue;
			}

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
								   fs_class_or_trait_diagnostic_name(target), fs_class_or_trait_diagnostic_name(trait)),
						conformance);
				continue;
			}

			// A trait's type arguments are fixed by the first class on the target's inheritance chain
			// that applies it. Recording different ones here would make the value satisfy the trait at
			// two arguments at once, since the runtime relation accepts either evidence source.
			if (parser->has_class(target) && !trait_use.resolved_type_arguments.is_empty()) {
				String inherited_arguments;
				String recorded_arguments;
				const FSParser::ClassNode *binding_ancestor = nullptr;
				if (trait_binding_conflicts_with_chain(target, trait, trait_use.resolved_type_arguments,
							conformance, inherited_arguments, recorded_arguments, binding_ancestor)) {
					push_error(vformat(R"(Trait "%s" is already applied with type arguments ("%s") by "%s"; the conformance for "%s" cannot record ("%s").)",
									   fs_class_or_trait_diagnostic_name(trait), inherited_arguments,
									   fs_class_or_trait_diagnostic_name(binding_ancestor), fs_class_or_trait_diagnostic_name(target),
									   recorded_arguments),
							conformance);
					continue;
				}
			}

			// What this declaration proves for each identity in the trait's closure, flattened the way
			// the registry stores it. Computed once: the chain-coherence check below and the registry
			// entries built at the end of this loop must record the same thing.
			const HashMap<StringName, FSParser::DataType> substitution = fs_trait_use_type_argument_bindings(trait, trait_use);
			Vector<Vector<FSConformanceRegistry::RecordedTypeArgument>> identity_arguments;
			for (const FSParser::ClassNode *identity_node : trait_identity_nodes) {
				identity_arguments.push_back(_recorded_conformance_trait_arguments(trait,
						trait_use.resolved_type_arguments, substitution, identity_node, target));
			}

			// The ClassDB counterpart of the chain rule above: an engine-class target inherits nothing
			// through `uses` clauses, so its chain's bindings live in the conformance registry instead.
			// One semantic chain spans a script class, its script bases, and the engine ancestry it ends
			// on, so an engine-class declaration is compared against both the engine classes on its own
			// chain and the script classes that bottom out on it.
			bool chain_conflict = false;
			String chain_conflict_message;
			for (int identity_index = 0; identity_index < trait_identities.size(); identity_index++) {
				if (target->is_native_conformance_shim) {
					StringName conflicting_class;
					String conflicting_source;
					if (_native_chain_binding_conflicts(StringName(target->fqcn), trait_identities[identity_index],
								identity_arguments[identity_index], valid_entries, source_file, conflicting_class,
								conflicting_source)) {
						chain_conflict_message = _chain_conflict_message(
								fs_class_or_trait_diagnostic_name(trait_identity_nodes[identity_index]), String(conflicting_class),
								conflicting_source, source_file);
						chain_conflict = true;
						break;
					}
					if (native_conformance_conflicts_with_script_chain(StringName(target->fqcn),
								trait_identity_nodes[identity_index], identity_arguments[identity_index], valid_entries,
								chain_conflict_message)) {
						chain_conflict = true;
						break;
					}
				} else if (trait_binding_conflicts_with_native_ancestry(target, trait_identity_nodes[identity_index],
								   identity_arguments[identity_index], valid_entries, chain_conflict_message) ||
						trait_binding_conflicts_with_script_ancestry(target, trait_identity_nodes[identity_index],
								identity_arguments[identity_index], valid_entries, chain_conflict_message)) {
					chain_conflict = true;
					break;
				}
			}
			if (chain_conflict) {
				push_error(chain_conflict_message, conformance);
				reported_declarations.insert(conformance_index);
				continue;
			}

			// Coherence applies to the whole implied identity closure. A repeated implied identity within
			// this declaration is the ordinary diamond case; a repeated direct identity or an overlap with
			// another declaration remains an error.
			bool membership_conflict = false;
			for (int identity_index = 0; identity_index < trait_identities.size(); identity_index++) {
				const StringName identity = trait_identities[identity_index];
				const String pair_key = target->fqcn + "\n" + String(identity);
				const int *existing_conformance = seen_membership_conformances.getptr(pair_key);
				if (existing_conformance != nullptr &&
						(*existing_conformance != conformance_index || identity_index == 0)) {
					push_error(vformat(R"(Class "%s" already has a conformance to trait "%s" in this file.)",
									   fs_class_or_trait_diagnostic_name(target), String(identity)),
							conformance);
					membership_conflict = true;
					// The declaration is rejected here, so the registry's own verdict on it below would be
					// the same contradiction reported twice.
					reported_declarations.insert(conformance_index);
					break;
				}

				const String other_source = registry->get_conformance_source(target->fqcn, identity);
				if (!other_source.is_empty() && other_source != source_file) {
					push_error(vformat(R"(Class "%s" already conforms to trait "%s" via a conformance in "%s".)",
									   fs_class_or_trait_diagnostic_name(target), String(identity), fs_diagnostic_file_reference(other_source)),
							conformance);
					membership_conflict = true;
					reported_declarations.insert(conformance_index);
					break;
				}
			}
			if (membership_conflict) {
				continue;
			}

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
										   fs_class_or_trait_diagnostic_name(target), witness_name, String(*existing_trait)),
								conformance);
						conformance_witness_collision = true;
						// As above: this declaration has already been told why it is rejected.
						reported_declarations.insert(conformance_index);
						continue;
					}

					StringName other_trait;
					const String other_witness_source = registry->get_witness_source(target_key, witness_name, other_trait);
					if (!other_witness_source.is_empty() && other_witness_source != source_file) {
						push_error(vformat(R"*(Class "%s" already has a witness for method "%s()" via a conformance in "%s".)*",
										   fs_class_or_trait_diagnostic_name(target), witness_name, fs_diagnostic_file_reference(other_witness_source)),
								conformance);
						conformance_witness_collision = true;
						reported_declarations.insert(conformance_index);
						continue;
					}
				}
			}
			if (conformance_witness_collision) {
				continue;
			}

			if (!validate_conformance(conformance, target, trait, substitution)) {
				continue;
			}

			FSConformanceRegistry::Conformance entry;
			entry.target_keys = target_keys;
			entry.target_fqcn = target->fqcn;
			entry.target_script_path = target_type.script_path;
			entry.target_is_root_class = target->outer == nullptr;
			// Only a script-class target sits on an engine chain it does not itself name; recording the
			// terminus here is what lets another file's engine-class declaration be compared against this
			// one without re-loading this file's parse tree.
			if (!target->is_native_conformance_shim && !target->is_builtin_conformance_shim) {
				entry.target_native_base = _terminal_native_class(target);
				// The script half of the same chain: a conformance on a script base answers for this
				// target's receivers too, and another file cannot see that from the target's name alone.
				entry.target_script_ancestor_fqcns = _script_ancestor_keys(target);
			}
			entry.target_label = fs_class_or_trait_diagnostic_name(target);
			entry.source_file = source_file;
			entry.conformance_index = conformance_index;
			for (FSParser::FunctionNode *witness : conformance->witnesses) {
				if (witness != nullptr && witness->identifier != nullptr) {
					entry.witnesses.insert(witness->identifier->name, witness);
				}
			}
			for (int identity_index = 0; identity_index < trait_identities.size(); identity_index++) {
				const StringName &identity = trait_identities[identity_index];
				const String pair_key = target->fqcn + "\n" + String(identity);
				const int *existing_conformance = seen_membership_conformances.getptr(pair_key);
				if (existing_conformance != nullptr && *existing_conformance == conformance_index) {
					continue;
				}
				entry.trait_name = identity;
				entry.trait_type_arguments = identity_arguments[identity_index];
				valid_entries.push_back(entry);
				seen_membership_conformances.insert(pair_key, conformance_index);
				identity_labels.insert(identity, fs_class_or_trait_diagnostic_name(trait_identity_nodes[identity_index]));
			}
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

	// One submission decides and publishes everything the registry is authoritative for. The checks
	// above are early diagnostics read from a registry state that has already expired by now; this is
	// where the outcome is actually fixed, against the store as it is at the moment of the write.
	// Diagnostics are produced from the returned value records afterwards, with the registry lock
	// already released and each rejection anchored back to the `ConformanceNode` its
	// `conformance_index` names.
	const FSConformanceRegistry::RegistrationResult result =
			registry->try_replace_file_conformances(source_file, valid_entries, trait_bindings,
					loaded_dependency_closure);
	report_binding_chain_conflicts(result, source_file);
	for (const FSConformanceRegistry::RegistrationConflict &conflict : result.conflicts) {
		if (reported_declarations.has(conflict.conformance_index)) {
			continue;
		}
		FSParser::ConformanceNode *conformance = conflict.conformance_index >= 0 &&
						conflict.conformance_index < p_class->conformances.size()
				? p_class->conformances[conflict.conformance_index]
				: nullptr;
		if (conformance == nullptr) {
			continue;
		}
		switch (conflict.kind) {
			case FSConformanceRegistry::RegistrationConflict::DUPLICATE_MEMBERSHIP: {
				push_error(vformat(R"(Class "%s" already conforms to trait "%s" via a conformance in %s.)",
								   conflict.target_label, String(conflict.trait_name),
								   _conflict_location(conflict.conflicting_source_file, source_file)),
						conformance);
			} break;
			case FSConformanceRegistry::RegistrationConflict::WITNESS_COLLISION: {
				push_error(vformat(R"*(Class "%s" already has a witness for method "%s()" via a conformance in %s.)*",
								   conflict.target_label, conflict.method_name,
								   _conflict_location(conflict.conflicting_source_file, source_file)),
						conformance);
			} break;
			case FSConformanceRegistry::RegistrationConflict::CHAIN_COHERENCE: {
				const String *trait_label = identity_labels.getptr(conflict.trait_name);
				push_error(_chain_conflict_message(trait_label != nullptr ? *trait_label : String(conflict.trait_name),
								   conflict.conflicting_target_label, conflict.conflicting_source_file, source_file),
						conformance);
			} break;
		}
	}
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
