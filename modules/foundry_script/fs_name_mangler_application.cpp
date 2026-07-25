/**************************************************************************/
/*  fs_name_mangler_application.cpp                                       */
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

#include "fs_name_mangler_application.h"

#ifdef TOOLS_ENABLED

String FSNameManglerApplication::Diagnostic::format() const {
	String prefix = surface;
	if (source_name != StringName()) {
		if (!prefix.is_empty()) {
			prefix += " ";
		}
		prefix += vformat("`%s`", source_name);
	}
	return prefix.is_empty() ? message : prefix + ": " + message;
}

FSNameManglerApplication::Transaction::Transaction() {
}

FSNameManglerApplication::Transaction::~Transaction() {
	rollback();
}

Error FSNameManglerApplication::Transaction::_fail(const String &p_surface,
		const StringName &p_source_name, const String &p_message, Error p_error,
		Vector<Diagnostic> &r_diagnostics) {
	Diagnostic diagnostic;
	diagnostic.surface = p_surface;
	diagnostic.source_name = p_source_name;
	diagnostic.message = p_message;
	r_diagnostics.push_back(diagnostic);
	state = STATE_FINISHED;
	class_snapshots.clear();
	return p_error;
}

bool FSNameManglerApplication::Transaction::_index_class(const Ref<FoundryScript> &p_script,
		HashSet<const FoundryScript *> &r_classes, Vector<Diagnostic> &r_diagnostics) {
	if (p_script.is_null()) {
		_fail("root graph", StringName(), "Found a null Foundry Script.", ERR_INVALID_PARAMETER, r_diagnostics);
		return false;
	}
	if (r_classes.has(p_script.ptr())) {
		_fail("root graph", p_script->local_name,
				"Found the same Foundry Script class more than once.", ERR_INVALID_PARAMETER, r_diagnostics);
		return false;
	}
	r_classes.insert(p_script.ptr());

	ClassSnapshot snapshot;
	snapshot.script = p_script;
	snapshot.local_name = p_script->local_name;
	snapshot.global_name = p_script->global_name;
	snapshot.fully_qualified_name = p_script->fully_qualified_name;
	class_snapshots.push_back(snapshot);

	for (const KeyValue<StringName, Ref<FoundryScript>> &subclass : p_script->subclasses) {
		if (!_index_class(subclass.value, r_classes, r_diagnostics)) {
			return false;
		}
	}
	return true;
}

Error FSNameManglerApplication::Transaction::begin(
		const Vector<Ref<FoundryScript>> &p_scripts,
		const RBMap<StringName, StringName> &p_rename_map,
		Vector<Diagnostic> &r_diagnostics) {
	r_diagnostics.clear();
	if (state != STATE_UNUSED) {
		Diagnostic diagnostic;
		diagnostic.surface = "transaction";
		diagnostic.message = "This name-application transaction has already been used.";
		r_diagnostics.push_back(diagnostic);
		return ERR_ALREADY_IN_USE;
	}

	if (p_scripts.is_empty()) {
		return _fail("root graph", StringName(), "At least one root script is required.",
				ERR_INVALID_PARAMETER, r_diagnostics);
	}

	HashSet<const FoundryScript *> classes;
	for (const Ref<FoundryScript> &script : p_scripts) {
		if (script.is_valid() && script->_owner != nullptr) {
			return _fail("root graph", script->local_name,
					"Nested classes cannot be supplied as roots.", ERR_INVALID_PARAMETER, r_diagnostics);
		}
		if (!_index_class(script, classes, r_diagnostics)) {
			return ERR_INVALID_PARAMETER;
		}
	}

	(void)p_rename_map;
	state = STATE_ACTIVE;
	return OK;
}

void FSNameManglerApplication::Transaction::rollback() {
	if (state != STATE_ACTIVE) {
		return;
	}
	for (const ClassSnapshot &snapshot : class_snapshots) {
		snapshot.script->local_name = snapshot.local_name;
		snapshot.script->global_name = snapshot.global_name;
		snapshot.script->fully_qualified_name = snapshot.fully_qualified_name;
	}
	class_snapshots.clear();
	state = STATE_FINISHED;
}

bool FSNameManglerApplication::Transaction::is_active() const {
	return state == STATE_ACTIVE;
}

FSNameManglerApplication::Transaction::State FSNameManglerApplication::Transaction::get_state() const {
	return state;
}

#endif // TOOLS_ENABLED
