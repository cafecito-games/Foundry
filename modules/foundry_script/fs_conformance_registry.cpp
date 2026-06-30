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

FSConformanceRegistry *FSConformanceRegistry::singleton = nullptr;

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

void FSConformanceRegistry::clear() {
	MutexLock lock(mutex);
	conformances_by_file.clear();
	index.clear();
}

bool FSConformanceRegistry::has_conformance(const String &p_target_key, const StringName &p_trait_name) const {
	if (p_target_key.is_empty() || p_trait_name == StringName()) {
		return false;
	}
	MutexLock lock(mutex);
	const HashMap<StringName, String> *traits = index.getptr(p_target_key);
	return traits != nullptr && traits->has(p_trait_name);
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
