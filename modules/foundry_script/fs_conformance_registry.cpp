/**************************************************************************/
/*  fs_conformance_registry.cpp                                           */
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

#include "fs_conformance_registry.h"

#include "foundry_script.h"
#include "fs_function.h"
#include "fs_type.h"

#include "core/object/class_db.h"
#include "core/templates/hash_set.h"

FSConformanceRegistry *FSConformanceRegistry::singleton = nullptr;
thread_local const FSConformanceRegistry::Visibility *FSConformanceRegistry::active_visibility = nullptr;
thread_local String FSConformanceRegistry::in_flight_source_file;

FSConformanceRegistry::ScopedVisibility::ScopedVisibility(const Visibility *p_visibility) {
	previous = active_visibility;
	active_visibility = p_visibility;
}

FSConformanceRegistry::ScopedVisibility::~ScopedVisibility() {
	active_visibility = previous;
}

FSConformanceRegistry::ScopedInFlightReplacement::ScopedInFlightReplacement(const String &p_source_file) {
	previous = in_flight_source_file;
	in_flight_source_file = p_source_file;
}

FSConformanceRegistry::ScopedInFlightReplacement::~ScopedInFlightReplacement() {
	in_flight_source_file = previous;
}

bool FSConformanceRegistry::_is_visible(const String &p_source_file) {
	if (!in_flight_source_file.is_empty() && p_source_file == in_flight_source_file) {
		return false;
	}
	return active_visibility == nullptr || active_visibility->can_see(p_source_file);
}

// The alias set a producer supplies includes the target file's resource path, because a caller that
// only holds a path must still find a conformance declared on that file's class. The path identifies
// exactly one class though: the file's root class. Every inner class compiled from the file reports
// the same path, and a root class declared without `class_name` even has that path as its FQCN, so
// keeping the path for an inner-class target lets a lookup answer with a sibling's conformance — or
// answer the root class with an inner class's conformance. Exactly that one alias is dropped; every
// other alias is stored as given, so an alias set that is wrong for other reasons still reaches the
// integrity checks that exist to catch it.
static Vector<String> _identifying_target_keys(const Vector<String> &p_target_keys, const String &p_target_script_path, bool p_target_is_root_class) {
	if (p_target_is_root_class || p_target_script_path.is_empty() || !p_target_keys.has(p_target_script_path)) {
		return p_target_keys;
	}
	Vector<String> identifying;
	for (const String &target_key : p_target_keys) {
		if (target_key != p_target_script_path) {
			identifying.push_back(target_key);
		}
	}
	return identifying;
}

// Runtime entries carry the target as a pointer instead of a name, so the same rule is applied through
// the target script. Native and builtin targets deliberately use the declaring script as a codegen
// stand-in; that script is a root script and its aliases are engine/builtin type names, so those
// entries are never narrowed.
static Vector<String> _identifying_runtime_target_keys(const FSConformanceRegistry::RuntimeConformance &p_conformance) {
	const FoundryScript *target_script = p_conformance.target_script;
	if (target_script == nullptr || target_script->is_root_script()) {
		return p_conformance.target_keys;
	}
	return _identifying_target_keys(p_conformance.target_keys, target_script->get_script_path(), false);
}

bool FSConformanceRegistry::validate_runtime_conformance_target(
		const RuntimeConformance &p_conformance,
		const FoundryScript *p_declaring_script,
		const Vector<String> &p_authoritative_target_keys,
		String &r_error) {
	r_error.clear();
	if (p_conformance.target_script == nullptr) {
		r_error = "the runtime entry has no target script";
		return false;
	}
	if (p_authoritative_target_keys.is_empty()) {
		r_error = "the correlated target has no aliases";
		return false;
	}

	const auto same_aliases =
			[](const Vector<String> &p_left,
					const Vector<String> &p_right) {
				if (p_left.size() != p_right.size()) {
					return false;
				}
				Vector<String> left = p_left;
				Vector<String> right = p_right;
				left.sort();
				right.sort();
				return left == right;
			};
	for (const String &target_key : p_authoritative_target_keys) {
		if (target_key.is_empty()) {
			r_error = "the correlated target contains an empty alias";
			return false;
		}
	}
	if (!same_aliases(
				p_conformance.target_keys,
				p_authoritative_target_keys)) {
		r_error =
				"the runtime aliases differ from the correlated target aliases";
		return false;
	}

	const bool native_or_builtin_stand_in =
			p_authoritative_target_keys.size() == 1 &&
			(ClassDB::class_exists(
					 StringName(p_authoritative_target_keys[0])) ||
					Variant::get_type_by_name(
							p_authoritative_target_keys[0]) <
							Variant::VARIANT_MAX);
	if (native_or_builtin_stand_in) {
		if (p_declaring_script == nullptr ||
				p_conformance.target_script != p_declaring_script) {
			r_error = vformat(
					"native/builtin target `%s` is not represented by its "
					"declaring-script stand-in",
					p_authoritative_target_keys[0]);
			return false;
		}
	} else {
		Vector<String> script_aliases;
		const String fully_qualified_name =
				p_conformance.target_script->get_fully_qualified_name();
		const String global_name =
				p_conformance.target_script->get_global_name();
		const String script_path =
				p_conformance.target_script->get_script_path();
		if (!fully_qualified_name.is_empty()) {
			script_aliases.push_back(fully_qualified_name);
		}
		if (!global_name.is_empty()) {
			// The analyzer/compiler intentionally preserve this alias even when a root global
			// class uses the same text as its fully-qualified identity.
			script_aliases.push_back(global_name);
		}
		// The script path identifies the target only when the target is its file's root script; an inner
		// class is identified by its fully-qualified name alone, matching how registration narrows the
		// stored alias set.
		if (!script_path.is_empty() &&
				!script_aliases.has(script_path) &&
				p_conformance.target_script->is_root_script()) {
			script_aliases.push_back(script_path);
		}
		if (!same_aliases(
					script_aliases,
					p_authoritative_target_keys)) {
			r_error = vformat(
					"target script `%s` does not own the correlated aliases",
					p_conformance.target_script
							->get_fully_qualified_name());
			return false;
		}
	}

	for (const KeyValue<StringName, FSFunction *> &witness :
			p_conformance.functions) {
		if (witness.value == nullptr) {
			r_error = vformat(
					"witness `%s` is null", witness.key);
			return false;
		}
		if (witness.value->get_script() !=
				p_conformance.target_script) {
			r_error = vformat(
					"witness `%s` is owned by a different target "
					"representation",
					witness.key);
			return false;
		}
	}
	return true;
}

FSConformanceRegistry *FSConformanceRegistry::get_singleton() {
	// The registry is process-global and lazily created so it is reachable from the analyzer,
	// the type-compatibility engine, and (in Phase 3) the runtime without an explicit owner.
	if (singleton == nullptr) {
		singleton = memnew(FSConformanceRegistry);
	}
	return singleton;
}

void FSConformanceRegistry::_rebuild_index() {
	index.clear();
	for (const KeyValue<String, Vector<Conformance>> &file_entry : conformances_by_file) {
		for (const Conformance &conformance : file_entry.value) {
			if (conformance.trait_name == StringName()) {
				continue;
			}
			for (const String &target_key : conformance.target_keys) {
				if (target_key.is_empty()) {
					continue;
				}
				index[target_key][conformance.trait_name] = conformance.source_file;
			}
		}
	}
}

void FSConformanceRegistry::register_file_conformances(const String &p_source_file, const Vector<Conformance> &p_conformances) {
	MutexLock lock(mutex);
	if (p_conformances.is_empty()) {
		conformances_by_file.erase(p_source_file);
	} else {
		Vector<Conformance> stored = p_conformances;
		for (Conformance &conformance : stored) {
			conformance.target_keys = _identifying_target_keys(
					conformance.target_keys, conformance.target_script_path, conformance.target_is_root_class);
		}
		conformances_by_file[p_source_file] = stored;
	}
	_rebuild_index();
}

// A conformance whose target is an engine class rather than a script class. The target is keyed by the
// bare engine-class name and belongs to no script file, so a script class that happens to share a name
// with an engine class is never mistaken for one.
static bool _is_native_target(const FSConformanceRegistry::Conformance &p_conformance) {
	return p_conformance.target_script_path.is_empty() && ClassDB::class_exists(p_conformance.target_fqcn);
}

// Whether two engine classes are on one ClassDB inheritance chain, excluding the class itself. A
// conformance declared on either one answers for receivers of the other, which is what makes two
// declarations along the chain describe the same trait for overlapping values.
static bool _native_classes_are_on_one_chain(const StringName &p_class, const StringName &p_other) {
	return p_class != p_other &&
			(ClassDB::is_parent_class(p_class, p_other) || ClassDB::is_parent_class(p_other, p_class));
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

bool FSConformanceRegistry::_declaration_witnesses_collide(const Conformance &p_candidate,
		const Vector<const Conformance *> &p_view, RegistrationConflict &r_conflict) const {
	if (p_candidate.target_fqcn.is_empty() || p_candidate.witnesses.is_empty()) {
		return false;
	}
	// A witness collision is a property of the program, not of what one file loads: two files supplying
	// the same method name for one target contradict each other whether or not either can see the other.
	for (const KeyValue<StringName, FSParser::FunctionNode *> &witness : p_candidate.witnesses) {
		if (witness.key == StringName()) {
			continue;
		}
		for (const Conformance *existing : p_view) {
			if (!existing->target_keys.has(p_candidate.target_fqcn) || !existing->witnesses.has(witness.key)) {
				continue;
			}
			r_conflict.kind = RegistrationConflict::WITNESS_COLLISION;
			r_conflict.conformance_index = p_candidate.conformance_index;
			r_conflict.target_label = p_candidate.target_label;
			r_conflict.trait_name = p_candidate.trait_name;
			r_conflict.method_name = witness.key;
			r_conflict.conflicting_target_label = existing->target_label;
			r_conflict.conflicting_source_file = existing->source_file;
			return true;
		}
	}
	return false;
}

bool FSConformanceRegistry::_candidate_conflicts(const Conformance &p_candidate, const String &p_source_file,
		const Vector<const Conformance *> &p_view, RegistrationConflict &r_conflict) const {
	if (p_candidate.trait_name == StringName()) {
		return false;
	}
	// A candidate this submission already accepted belongs to the file being replaced, which the
	// in-flight suppression deliberately hides from this thread's queries. It is still what the file is
	// about to declare, so it takes part in the comparison rather than being read out of the store.
	const auto reaches_candidate = [&](const Conformance &p_existing) {
		return p_existing.source_file == p_source_file || _is_visible(p_existing.source_file);
	};
	const bool candidate_is_native = _is_native_target(p_candidate);
	const StringName candidate_native_class = candidate_is_native ? StringName(p_candidate.target_fqcn) : StringName();

	// Chain coherence runs before membership so a contradiction between two levels of one chain is
	// reported as the contradiction it is rather than as an unrelated duplicate on some shared alias.
	if (!p_candidate.trait_type_arguments.is_empty()) {
		for (const Conformance *existing : p_view) {
			if (existing->trait_name != p_candidate.trait_name) {
				continue;
			}
			const bool existing_is_native = _is_native_target(*existing);
			bool answers_for_same_receivers = false;
			if (candidate_is_native && existing_is_native) {
				answers_for_same_receivers =
						_native_classes_are_on_one_chain(candidate_native_class, StringName(existing->target_fqcn));
			} else if (candidate_is_native) {
				// A script class whose chain bottoms out on this engine class, or on a subclass of it, is
				// answered for by the candidate. An engine declaration reaches a script class the way an
				// import does, so only a declaration the caller may see decides how it binds the trait.
				answers_for_same_receivers = reaches_candidate(*existing) &&
						_native_ancestry_answers_for(existing->target_native_base, candidate_native_class);
			} else if (existing_is_native) {
				answers_for_same_receivers = reaches_candidate(*existing) &&
						_native_ancestry_answers_for(p_candidate.target_native_base, StringName(existing->target_fqcn));
			}
			if (!answers_for_same_receivers) {
				continue;
			}
			if (!FSTypeCompatibility::recorded_arguments_conflict(existing->trait_type_arguments,
						p_candidate.trait_type_arguments)) {
				continue;
			}
			r_conflict.kind = RegistrationConflict::CHAIN_COHERENCE;
			r_conflict.conformance_index = p_candidate.conformance_index;
			r_conflict.target_label = p_candidate.target_label;
			r_conflict.trait_name = p_candidate.trait_name;
			r_conflict.conflicting_target_label =
					existing->target_label.is_empty() ? existing->target_fqcn : existing->target_label;
			r_conflict.conflicting_source_file = existing->source_file;
			return true;
		}
	}

	// Duplicate membership. Matching is on the candidate's exact fully-qualified name against the other
	// side's alias set, which is the same identity relation the flattened lookup index answers with.
	if (!p_candidate.target_fqcn.is_empty()) {
		for (const Conformance *existing : p_view) {
			if (existing->trait_name != p_candidate.trait_name ||
					!existing->target_keys.has(p_candidate.target_fqcn)) {
				continue;
			}
			r_conflict.kind = RegistrationConflict::DUPLICATE_MEMBERSHIP;
			r_conflict.conformance_index = p_candidate.conformance_index;
			r_conflict.target_label = p_candidate.target_label;
			r_conflict.trait_name = p_candidate.trait_name;
			r_conflict.conflicting_target_label =
					existing->target_label.is_empty() ? existing->target_fqcn : existing->target_label;
			r_conflict.conflicting_source_file = existing->source_file;
			return true;
		}
	}

	return false;
}

FSConformanceRegistry::RegistrationResult FSConformanceRegistry::try_replace_file_conformances(
		const String &p_source_file, const Vector<Conformance> &p_candidates) {
	RegistrationResult result;

	MutexLock lock(mutex);

	Vector<Conformance> normalized = p_candidates;
	for (Conformance &conformance : normalized) {
		conformance.target_keys = _identifying_target_keys(
				conformance.target_keys, conformance.target_script_path, conformance.target_is_root_class);
	}

	// The view the candidates are judged against. The submitting file's previous entries are excluded so
	// an unchanged reanalysis cannot conflict with itself; they stay in `conformances_by_file` until the
	// replacement below commits, so no reader ever sees the file's conformances missing.
	Vector<const Conformance *> view;
	for (const KeyValue<String, Vector<Conformance>> &file_entry : conformances_by_file) {
		if (file_entry.key == p_source_file) {
			continue;
		}
		for (const Conformance &conformance : file_entry.value) {
			view.push_back(&conformance);
		}
	}
	const int foreign_entry_count = view.size();

	Vector<Conformance> accepted;
	// One `ConformanceNode` is the unit of rejection, so the candidates are walked declaration by
	// declaration: a conflict on any identity or witness one of them emits drops every entry it emitted,
	// including the ones its implied supertraits produced.
	int declaration_start = 0;
	while (declaration_start < normalized.size()) {
		int declaration_end = declaration_start + 1;
		while (declaration_end < normalized.size() &&
				normalized[declaration_end].conformance_index == normalized[declaration_start].conformance_index) {
			declaration_end++;
		}

		RegistrationConflict conflict;
		bool conflicts = false;
		// Membership and chain coherence are per identity; the witness map is shared by every entry the
		// declaration emitted, so it is checked once, and last, so a contradiction is reported as the
		// membership or chain contradiction it is rather than as the witness collision it also implies.
		for (int entry_index = declaration_start; entry_index < declaration_end && !conflicts; entry_index++) {
			conflicts = _candidate_conflicts(normalized[entry_index], p_source_file, view, conflict);
		}
		if (!conflicts) {
			conflicts = _declaration_witnesses_collide(normalized[declaration_start], view, conflict);
		}

		if (conflicts) {
			result.conflicts.push_back(conflict);
		} else {
			for (int entry_index = declaration_start; entry_index < declaration_end; entry_index++) {
				accepted.push_back(normalized[entry_index]);
			}
			// `Vector` is copy-on-write and may reallocate, so the borrowed view is rebuilt rather than
			// appended to.
			view.resize(foreign_entry_count);
			for (const Conformance &entry : accepted) {
				view.push_back(&entry);
			}
		}

		declaration_start = declaration_end;
	}

	if (accepted.is_empty()) {
		conformances_by_file.erase(p_source_file);
	} else {
		conformances_by_file[p_source_file] = accepted;
	}
	_rebuild_index();

	result.registered_count = accepted.size();
	return result;
}

void FSConformanceRegistry::clear_file(const String &p_source_file) {
	MutexLock lock(mutex);
	if (conformances_by_file.erase(p_source_file)) {
		_rebuild_index();
	}
}

void FSConformanceRegistry::_rebuild_runtime_index() {
	runtime_index.clear();
	runtime_trait_index.clear();
	for (const KeyValue<String, Vector<RuntimeConformance>> &file_entry : runtime_by_file) {
		for (const RuntimeConformance &conformance : file_entry.value) {
			for (const String &target_key : conformance.target_keys) {
				if (target_key.is_empty()) {
					continue;
				}
				if (conformance.trait_name != StringName()) {
					RuntimeTraitEntry &trait_entry =
							runtime_trait_index[target_key][conformance.trait_name];
					trait_entry.source_file = file_entry.key;
					trait_entry.type_arguments =
							conformance.trait_type_arguments;
				}
				WitnessFunctionMap &functions = runtime_index[target_key];
				for (const KeyValue<StringName, FSFunction *> &witness : conformance.functions) {
					if (witness.value != nullptr) {
						functions[witness.key] = witness.value;
					}
				}
			}
		}
	}
}

void FSConformanceRegistry::register_runtime_witnesses(const String &p_source_file, const Vector<RuntimeConformance> &p_conformances) {
	MutexLock lock(mutex);
	if (p_conformances.is_empty()) {
		runtime_by_file.erase(p_source_file);
	} else {
		Vector<RuntimeConformance> stored = p_conformances;
		for (RuntimeConformance &conformance : stored) {
			conformance.target_keys = _identifying_runtime_target_keys(conformance);
		}
		runtime_by_file[p_source_file] = stored;
	}
	_rebuild_runtime_index();
}

void FSConformanceRegistry::clear_runtime_witnesses(const String &p_source_file) {
	MutexLock lock(mutex);
	if (runtime_by_file.erase(p_source_file)) {
		_rebuild_runtime_index();
	}
}

Vector<FSConformanceRegistry::RuntimeConformance> FSConformanceRegistry::get_runtime_witnesses(const String &p_source_file) const {
	MutexLock lock(mutex);
	const Vector<RuntimeConformance> *entries = runtime_by_file.getptr(p_source_file);
	return entries != nullptr ? *entries : Vector<RuntimeConformance>();
}

FSFunction *FSConformanceRegistry::find_witness_function(const String &p_target_key, const StringName &p_method) const {
	if (p_target_key.is_empty() || p_method == StringName()) {
		return nullptr;
	}
	MutexLock lock(mutex);
	const WitnessFunctionMap *functions = runtime_index.getptr(p_target_key);
	if (functions == nullptr) {
		return nullptr;
	}
	FSFunction *const *function = functions->getptr(p_method);
	return function != nullptr ? *function : nullptr;
}

FSFunction *FSConformanceRegistry::find_witness_function_for_target(const FoundryScript *p_target_script, const StringName &p_method) const {
	if (p_target_script == nullptr || p_method == StringName()) {
		return nullptr;
	}
	MutexLock lock(mutex);
	for (const KeyValue<String, Vector<RuntimeConformance>> &file_entry : runtime_by_file) {
		for (const RuntimeConformance &conformance : file_entry.value) {
			if (conformance.target_script != p_target_script) {
				continue;
			}
			FSFunction *const *function = conformance.functions.getptr(p_method);
			if (function != nullptr && *function != nullptr) {
				return *function;
			}
		}
	}
	return nullptr;
}

FSFunction *FSConformanceRegistry::find_native_witness_function(const StringName &p_native_class, const StringName &p_method) const {
	if (p_native_class == StringName() || p_method == StringName()) {
		return nullptr;
	}
	MutexLock lock(mutex);
	// Walk the engine inheritance chain so a witness declared on a base class dispatches for a subclass
	// instance, mirroring `native_class_conforms`.
	for (StringName cursor = p_native_class; cursor != StringName(); cursor = ClassDB::get_parent_class(cursor)) {
		const WitnessFunctionMap *functions = runtime_index.getptr(String(cursor));
		if (functions == nullptr) {
			continue;
		}
		FSFunction *const *function = functions->getptr(p_method);
		if (function != nullptr) {
			return *function;
		}
	}
	return nullptr;
}

FSFunction *FSConformanceRegistry::find_native_trait_witness_function(const StringName &p_native_class,
		const StringName &p_trait_name, const StringName &p_method) const {
	if (p_native_class == StringName() || p_trait_name == StringName() || p_method == StringName()) {
		return nullptr;
	}
	MutexLock lock(mutex);
	// The alias index collapses every trait into one method map, so answering per trait means scanning
	// the runtime store. The engine inheritance chain is the outer loop, so the nearest conforming
	// ancestor wins even when a further ancestor conforms too.
	for (StringName cursor = p_native_class; cursor != StringName(); cursor = ClassDB::get_parent_class(cursor)) {
		const String target_key = String(cursor);
		for (const KeyValue<String, Vector<RuntimeConformance>> &file_entry : runtime_by_file) {
			for (const RuntimeConformance &conformance : file_entry.value) {
				if (conformance.trait_name != p_trait_name || !conformance.target_keys.has(target_key)) {
					continue;
				}
				FSFunction *const *function = conformance.functions.getptr(p_method);
				if (function != nullptr && *function != nullptr) {
					return *function;
				}
			}
		}
	}
	return nullptr;
}

void FSConformanceRegistry::clear() {
	MutexLock lock(mutex);
	conformances_by_file.clear();
	index.clear();
	runtime_by_file.clear();
	runtime_index.clear();
	runtime_trait_index.clear();
}

void FSConformanceRegistry::clear_declarations() {
	MutexLock lock(mutex);
	conformances_by_file.clear();
	index.clear();
}

bool FSConformanceRegistry::has_conformance(const String &p_target_key, const StringName &p_trait_name, bool p_include_runtime) const {
	if (p_target_key.is_empty() || p_trait_name == StringName()) {
		return false;
	}
	MutexLock lock(mutex);
	const HashMap<StringName, String> *traits = index.getptr(p_target_key);
	if (traits != nullptr) {
		const String *source_file = traits->getptr(p_trait_name);
		if (source_file != nullptr && _is_visible(*source_file)) {
			return true;
		}
	}
	if (!p_include_runtime) {
		return false;
	}
	const HashMap<StringName, RuntimeTraitEntry> *runtime_traits =
			runtime_trait_index.getptr(p_target_key);
	return runtime_traits != nullptr && runtime_traits->has(p_trait_name);
}

bool FSConformanceRegistry::builtin_type_conforms(Variant::Type p_type, const StringName &p_trait_name, bool p_include_runtime) const {
	if (p_type == Variant::NIL || p_type == Variant::OBJECT || p_trait_name == StringName()) {
		return false;
	}
	return has_conformance(Variant::get_type_name(p_type), p_trait_name, p_include_runtime);
}

FSFunction *FSConformanceRegistry::find_builtin_witness_function(Variant::Type p_type, const StringName &p_method) const {
	if (p_type == Variant::NIL || p_type == Variant::OBJECT || p_method == StringName()) {
		return nullptr;
	}
	return find_witness_function(Variant::get_type_name(p_type), p_method);
}

bool FSConformanceRegistry::native_class_conforms(const StringName &p_native_class, const StringName &p_trait_name, bool p_include_runtime) const {
	if (p_native_class == StringName() || p_trait_name == StringName()) {
		return false;
	}
	MutexLock lock(mutex);
	// Walk the engine inheritance chain so a conformance declared on a base class is honored for any
	// subclass instance (e.g. a `Sprite2D` satisfies `extend Node2D uses ...`).
	for (StringName cursor = p_native_class; cursor != StringName(); cursor = ClassDB::get_parent_class(cursor)) {
		const HashMap<StringName, String> *traits = index.getptr(String(cursor));
		if (traits != nullptr) {
			const String *source_file = traits->getptr(p_trait_name);
			if (source_file != nullptr && _is_visible(*source_file)) {
				return true;
			}
		}
		if (p_include_runtime) {
			const HashMap<StringName, RuntimeTraitEntry> *runtime_traits =
					runtime_trait_index.getptr(String(cursor));
			if (runtime_traits != nullptr &&
					runtime_traits->has(p_trait_name)) {
				return true;
			}
		}
	}
	return false;
}

static FSConformanceRegistry::RecordedTypeArgument _reduce_type_argument(const FSParser::DataType &p_type, int p_depth) {
	using RecordedTypeArgument = FSConformanceRegistry::RecordedTypeArgument;
	RecordedTypeArgument recorded;
	if (unlikely(p_depth > Variant::MAX_RECURSION_DEPTH)) {
		return recorded;
	}
	if (!p_type.is_set() || p_type.is_meta_type || p_type.is_type_handle_annotation) {
		return recorded;
	}

	switch (p_type.kind) {
		case FSParser::DataType::BUILTIN: {
			if (p_type.has_method_signature) {
				// A `Callable`/`Signal` carries a signature this form cannot compare.
				return recorded;
			}
			recorded.kind = RecordedTypeArgument::BUILTIN;
			recorded.builtin_type = p_type.builtin_type;
			recorded.numeric_type = p_type.numeric_type;
		} break;
		case FSParser::DataType::NATIVE: {
			if (p_type.native_type == StringName()) {
				return recorded;
			}
			recorded.kind = RecordedTypeArgument::NATIVE_CLASS;
			recorded.native_class = p_type.native_type;
		} break;
		case FSParser::DataType::CLASS: {
			if (p_type.class_type == nullptr) {
				return recorded;
			}
			const String global_name = String(p_type.class_type->get_global_name());
			if (p_type.class_type->fqcn.is_empty() && global_name.is_empty()) {
				return recorded;
			}
			recorded.kind = RecordedTypeArgument::SCRIPT_CLASS;
			recorded.script_fqcn = p_type.class_type->fqcn;
			recorded.script_global_name = global_name;
		} break;
		case FSParser::DataType::SCRIPT: {
			const String global_name = p_type.script_type.is_valid()
					? String(p_type.script_type->get_global_name())
					: String();
			if (!p_type.script_type.is_valid() && p_type.script_path.is_empty()) {
				return recorded;
			}
			recorded.kind = RecordedTypeArgument::SCRIPT_CLASS;
			recorded.script_fqcn = p_type.script_path;
			recorded.script_global_name = global_name;
			if (recorded.script_fqcn.is_empty() && recorded.script_global_name.is_empty()) {
				return RecordedTypeArgument();
			}
		} break;
		default:
			// `VARIANT`, `TYPE_PARAMETER`, `ENUM`, `TUPLE`, and `UNION` all reduce to an absence of
			// evidence: none of them has an identity this flattened form can compare with certainty.
			return recorded;
	}

	recorded.is_nullable = p_type.is_nullable;
	// A composite keeps its components rather than erasing the whole position: the components this
	// form cannot represent reduce to `UNKNOWN` individually, so a known sibling stays comparable.
	recorded.type_arguments.resize(p_type.type_arguments.size());
	for (int i = 0; i < p_type.type_arguments.size(); i++) {
		recorded.type_arguments.write[i] = _reduce_type_argument(p_type.type_arguments[i], p_depth + 1);
	}
	recorded.container_element_types.resize(p_type.container_element_types.size());
	for (int i = 0; i < p_type.container_element_types.size(); i++) {
		recorded.container_element_types.write[i] = _reduce_type_argument(p_type.container_element_types[i], p_depth + 1);
	}
	return recorded;
}

FSConformanceRegistry::RecordedTypeArgument FSConformanceRegistry::reduce_type_argument(const FSParser::DataType &p_type) {
	return _reduce_type_argument(p_type, 0);
}

bool FSConformanceRegistry::_has_visible_conformance(const String &p_target_key, const StringName &p_trait_name) const {
	const HashMap<StringName, String> *traits = index.getptr(p_target_key);
	if (traits == nullptr) {
		return false;
	}
	const String *source_file = traits->getptr(p_trait_name);
	return source_file != nullptr && _is_visible(*source_file);
}

bool FSConformanceRegistry::_recorded_trait_arguments_for_key(const String &p_target_key,
		const StringName &p_trait_name, Vector<RecordedTypeArgument> &r_arguments) const {
	r_arguments.clear();
	if (!_has_visible_conformance(p_target_key, p_trait_name)) {
		return false;
	}
	const HashMap<StringName, String> *traits = index.getptr(p_target_key);
	const Vector<Conformance> *entries = conformances_by_file.getptr(*traits->getptr(p_trait_name));
	if (entries == nullptr) {
		return false;
	}
	for (const Conformance &conformance : *entries) {
		if (conformance.trait_name != p_trait_name || !conformance.target_keys.has(p_target_key)) {
			continue;
		}
		if (conformance.trait_type_arguments.is_empty()) {
			return false;
		}
		r_arguments = conformance.trait_type_arguments;
		return true;
	}
	return false;
}

bool FSConformanceRegistry::get_recorded_trait_arguments(const String &p_target_key,
		const StringName &p_trait_name, Vector<RecordedTypeArgument> &r_arguments) const {
	r_arguments.clear();
	if (p_target_key.is_empty() || p_trait_name == StringName()) {
		return false;
	}
	MutexLock lock(mutex);
	return _recorded_trait_arguments_for_key(p_target_key, p_trait_name, r_arguments);
}

bool FSConformanceRegistry::get_native_recorded_trait_arguments(const StringName &p_native_class,
		const StringName &p_trait_name, Vector<RecordedTypeArgument> &r_arguments) const {
	r_arguments.clear();
	if (p_native_class == StringName() || p_trait_name == StringName()) {
		return false;
	}
	MutexLock lock(mutex);
	// The nearest conforming ancestor wins, matching how membership itself is answered. A hidden
	// conformance does not shadow a visible one further up, for the same reason.
	for (StringName cursor = p_native_class; cursor != StringName(); cursor = ClassDB::get_parent_class(cursor)) {
		const String key = String(cursor);
		if (_has_visible_conformance(key, p_trait_name)) {
			return _recorded_trait_arguments_for_key(key, p_trait_name, r_arguments);
		}
	}
	return false;
}

bool FSConformanceRegistry::get_builtin_recorded_trait_arguments(Variant::Type p_type,
		const StringName &p_trait_name, Vector<RecordedTypeArgument> &r_arguments) const {
	r_arguments.clear();
	if (p_type == Variant::NIL || p_type == Variant::OBJECT || p_trait_name == StringName()) {
		return false;
	}
	return get_recorded_trait_arguments(Variant::get_type_name(p_type), p_trait_name, r_arguments);
}

bool FSConformanceRegistry::_live_runtime_type_arguments(const RuntimeTraitEntry &p_entry, Vector<ContainerType> &r_arguments) const {
	if (p_entry.type_arguments.is_empty()) {
		return false;
	}
	Vector<ContainerType> arguments;
	arguments.resize(p_entry.type_arguments.size());
	for (int i = 0; i < p_entry.type_arguments.size(); i++) {
		// A freed argument script is an absence of evidence, never a null-script stand-in to compare
		// against: materializing one would silently widen the recorded argument to its bare class.
		if (!p_entry.type_arguments[i].is_fully_live()) {
			return false;
		}
		arguments.write[i] = p_entry.type_arguments[i].to_container_type();
	}
	r_arguments = arguments;
	return true;
}

bool FSConformanceRegistry::get_conformance_type_arguments(const String &p_target_key, const StringName &p_trait_name,
		Vector<ContainerType> &r_arguments) const {
	r_arguments.clear();
	if (p_target_key.is_empty() || p_trait_name == StringName()) {
		return false;
	}
	MutexLock lock(mutex);
	const HashMap<StringName, RuntimeTraitEntry> *runtime_traits =
			runtime_trait_index.getptr(p_target_key);
	if (runtime_traits == nullptr) {
		return false;
	}
	const RuntimeTraitEntry *entry = runtime_traits->getptr(p_trait_name);
	return entry != nullptr && _live_runtime_type_arguments(*entry, r_arguments);
}

bool FSConformanceRegistry::get_native_conformance_type_arguments(const StringName &p_native_class,
		const StringName &p_trait_name, Vector<ContainerType> &r_arguments) const {
	r_arguments.clear();
	if (p_native_class == StringName() || p_trait_name == StringName()) {
		return false;
	}
	MutexLock lock(mutex);
	// The nearest conforming ancestor wins, matching how membership itself is answered.
	for (StringName cursor = p_native_class; cursor != StringName(); cursor = ClassDB::get_parent_class(cursor)) {
		const HashMap<StringName, RuntimeTraitEntry> *runtime_traits =
				runtime_trait_index.getptr(String(cursor));
		if (runtime_traits == nullptr) {
			continue;
		}
		const RuntimeTraitEntry *entry = runtime_traits->getptr(p_trait_name);
		if (entry != nullptr) {
			return _live_runtime_type_arguments(*entry, r_arguments);
		}
	}
	return false;
}

bool FSConformanceRegistry::get_builtin_conformance_type_arguments(Variant::Type p_type,
		const StringName &p_trait_name, Vector<ContainerType> &r_arguments) const {
	r_arguments.clear();
	if (p_type == Variant::NIL || p_type == Variant::OBJECT || p_trait_name == StringName()) {
		return false;
	}
	return get_conformance_type_arguments(Variant::get_type_name(p_type), p_trait_name, r_arguments);
}

Vector<FSConformanceRegistry::NativeConformanceRecord> FSConformanceRegistry::get_native_conformance_records(
		const StringName &p_trait_name, bool p_visible_only, const String &p_excluded_source_file) const {
	Vector<NativeConformanceRecord> records;
	if (p_trait_name == StringName()) {
		return records;
	}
	MutexLock lock(mutex);
	for (const KeyValue<String, Vector<Conformance>> &file_entry : conformances_by_file) {
		if (file_entry.key == p_excluded_source_file || (p_visible_only && !_is_visible(file_entry.key))) {
			continue;
		}
		for (const Conformance &conformance : file_entry.value) {
			// A native target is keyed by the bare engine-class name and belongs to no script file, so
			// a script class that happens to share a name with an engine class is never mistaken for one.
			if (conformance.trait_name != p_trait_name || !conformance.target_script_path.is_empty() ||
					!ClassDB::class_exists(conformance.target_fqcn)) {
				continue;
			}
			NativeConformanceRecord record;
			record.native_class = StringName(conformance.target_fqcn);
			record.source_file = conformance.source_file;
			record.trait_type_arguments = conformance.trait_type_arguments;
			records.push_back(record);
		}
	}
	return records;
}

Vector<FSConformanceRegistry::ScriptConformanceRecord> FSConformanceRegistry::get_script_conformance_records(
		const StringName &p_trait_name, bool p_visible_only, const String &p_excluded_source_file) const {
	Vector<ScriptConformanceRecord> records;
	if (p_trait_name == StringName()) {
		return records;
	}
	MutexLock lock(mutex);
	for (const KeyValue<String, Vector<Conformance>> &file_entry : conformances_by_file) {
		if (file_entry.key == p_excluded_source_file || (p_visible_only && !_is_visible(file_entry.key))) {
			continue;
		}
		for (const Conformance &conformance : file_entry.value) {
			// Only a script-class target records a terminal engine class; an engine or builtin target, and
			// a producer that could not resolve the chain, leave it empty, which states nothing about a
			// chain rather than placing the target on `Object`'s.
			if (conformance.trait_name != p_trait_name || conformance.target_native_base == StringName()) {
				continue;
			}
			ScriptConformanceRecord record;
			record.target_fqcn = conformance.target_fqcn;
			record.target_label = conformance.target_label.is_empty() ? conformance.target_fqcn : conformance.target_label;
			record.target_native_base = conformance.target_native_base;
			record.source_file = conformance.source_file;
			record.trait_type_arguments = conformance.trait_type_arguments;
			records.push_back(record);
		}
	}
	return records;
}

String FSConformanceRegistry::get_conformance_source(const String &p_target_key, const StringName &p_trait_name) const {
	if (p_target_key.is_empty() || p_trait_name == StringName()) {
		return String();
	}
	MutexLock lock(mutex);
	const HashMap<StringName, String> *traits = index.getptr(p_target_key);
	if (traits == nullptr) {
		return String();
	}
	const String *source = traits->getptr(p_trait_name);
	return source != nullptr ? *source : String();
}

FSConformanceRegistry::WitnessMap FSConformanceRegistry::get_witnesses(const String &p_target_key, const StringName &p_trait_name) const {
	MutexLock lock(mutex);
	for (const KeyValue<String, Vector<Conformance>> &file_entry : conformances_by_file) {
		if (!_is_visible(file_entry.key)) {
			continue;
		}
		for (const Conformance &conformance : file_entry.value) {
			if (conformance.trait_name != p_trait_name) {
				continue;
			}
			if (conformance.target_keys.has(p_target_key)) {
				return conformance.witnesses;
			}
		}
	}
	return WitnessMap();
}

Vector<FSConformanceRegistry::Conformance> FSConformanceRegistry::get_file_conformances(const String &p_source_file) const {
	MutexLock lock(mutex);
	const Vector<Conformance> *entries = conformances_by_file.getptr(p_source_file);
	return entries != nullptr ? *entries : Vector<Conformance>();
}

String FSConformanceRegistry::get_witness_source(const String &p_target_key, const StringName &p_method, StringName &r_trait_name) const {
	r_trait_name = StringName();
	if (p_target_key.is_empty() || p_method == StringName()) {
		return String();
	}
	MutexLock lock(mutex);
	for (const KeyValue<String, Vector<Conformance>> &file_entry : conformances_by_file) {
		for (const Conformance &conformance : file_entry.value) {
			if (!conformance.target_keys.has(p_target_key)) {
				continue;
			}
			if (conformance.witnesses.has(p_method)) {
				r_trait_name = conformance.trait_name;
				return conformance.source_file;
			}
		}
	}
	return String();
}

bool FSConformanceRegistry::find_witness_location(const String &p_target_fqcn, const StringName &p_method,
		String &r_source_file, int &r_conformance_index) const {
	r_source_file = String();
	r_conformance_index = -1;
	if (p_target_fqcn.is_empty() || p_method == StringName()) {
		return false;
	}
	MutexLock lock(mutex);
	for (const KeyValue<String, Vector<Conformance>> &file_entry : conformances_by_file) {
		if (!_is_visible(file_entry.key)) {
			continue;
		}
		for (const Conformance &conformance : file_entry.value) {
			if (conformance.conformance_index < 0 || conformance.target_fqcn != p_target_fqcn ||
					!conformance.witnesses.has(p_method)) {
				continue;
			}
			r_source_file = conformance.source_file;
			r_conformance_index = conformance.conformance_index;
			return true;
		}
	}
	return false;
}

bool FSConformanceRegistry::find_hidden_witness_declaration(const String &p_target_fqcn, const StringName &p_method,
		String &r_source_file, StringName &r_trait_name) const {
	r_source_file = String();
	r_trait_name = StringName();
	if (p_target_fqcn.is_empty() || p_method == StringName()) {
		return false;
	}
	MutexLock lock(mutex);
	for (const KeyValue<String, Vector<Conformance>> &file_entry : conformances_by_file) {
		if (_is_visible(file_entry.key)) {
			continue;
		}
		for (const Conformance &conformance : file_entry.value) {
			if (conformance.target_fqcn != p_target_fqcn || !conformance.witnesses.has(p_method)) {
				continue;
			}
			r_source_file = conformance.source_file;
			r_trait_name = conformance.trait_name;
			return true;
		}
	}
	return false;
}

FSConformanceRegistry::FSConformanceRegistry() {
	if (singleton == nullptr) {
		singleton = this;
	}
}

FSConformanceRegistry::~FSConformanceRegistry() {
	if (singleton == this) {
		singleton = nullptr;
	}
}
