/**************************************************************************/
/*  fs_conformance_registry.cpp                                           */
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

#include "fs_conformance_registry.h"

#include "foundry_script.h"
#include "fs_function.h"

#include "core/object/class_db.h"

FSConformanceRegistry *FSConformanceRegistry::singleton = nullptr;

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
		if (!script_path.is_empty() &&
				!script_aliases.has(script_path)) {
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
		conformances_by_file[p_source_file] = p_conformances;
	}
	_rebuild_index();
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
					runtime_trait_index[target_key][conformance.trait_name] =
							file_entry.key;
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
		runtime_by_file[p_source_file] = p_conformances;
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

void FSConformanceRegistry::clear() {
	MutexLock lock(mutex);
	conformances_by_file.clear();
	index.clear();
	runtime_by_file.clear();
	runtime_index.clear();
	runtime_trait_index.clear();
}

bool FSConformanceRegistry::has_conformance(const String &p_target_key, const StringName &p_trait_name, bool p_include_runtime) const {
	if (p_target_key.is_empty() || p_trait_name == StringName()) {
		return false;
	}
	MutexLock lock(mutex);
	const HashMap<StringName, String> *traits = index.getptr(p_target_key);
	if (traits != nullptr && traits->has(p_trait_name)) {
		return true;
	}
	if (!p_include_runtime) {
		return false;
	}
	const HashMap<StringName, String> *runtime_traits =
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
		if (traits != nullptr && traits->has(p_trait_name)) {
			return true;
		}
		if (p_include_runtime) {
			const HashMap<StringName, String> *runtime_traits =
					runtime_trait_index.getptr(String(cursor));
			if (runtime_traits != nullptr &&
					runtime_traits->has(p_trait_name)) {
				return true;
			}
		}
	}
	return false;
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
