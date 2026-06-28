/**************************************************************************/
/*  fs_refactoring_names.cpp                                              */
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

#include "fs_refactoring_names.h"

#ifdef TOOLS_ENABLED

#include "../foundry_script.h"

#include "core/string/char_utils.h"
#include "core/templates/hash_set.h"
#include "core/variant/variant.h"

namespace {

bool is_reserved_keyword(const String &p_name) {
	static const HashSet<String> keywords = []() {
		HashSet<String> set;
		for (const String &word : FSLanguage::get_singleton()->get_reserved_words()) {
			set.insert(word);
		}
		return set;
	}();
	return keywords.has(p_name);
}

} // namespace

#ifndef GDSCRIPT_NO_LSP
namespace {

// Locates the parent symbol whose `children` Vector directly contains p_target,
// matching by pointer identity. Returns nullptr if p_target is the root itself
// or cannot be found anywhere in the tree.
const LSP::DocumentSymbol *find_parent_scope(const LSP::DocumentSymbol &p_node, const LSP::DocumentSymbol *p_target) {
	for (const LSP::DocumentSymbol &child : p_node.children) {
		if (&child == p_target) {
			return &p_node;
		}
		if (const LSP::DocumentSymbol *found = find_parent_scope(child, p_target)) {
			return found;
		}
	}
	return nullptr;
}

} // namespace

bool FSRefactorNames::has_scope_collision(const LSP::DocumentSymbol &p_root, const LSP::DocumentSymbol *p_target, const String &p_new_name, String &r_reason) {
	r_reason = String();
	if (!p_target) {
		return false;
	}

	const LSP::DocumentSymbol *parent = find_parent_scope(p_root, p_target);
	if (!parent) {
		// Scope could not be determined; stay conservative and allow the rename.
		// The editor re-parse will still report any genuine breakage.
		return false;
	}

	for (const LSP::DocumentSymbol &sibling : parent->children) {
		if (&sibling == p_target) {
			continue;
		}
		if (sibling.name == p_new_name) {
			r_reason = vformat("A symbol named '%s' already exists in this scope.", p_new_name);
			return true;
		}
	}
	return false;
}
#endif // GDSCRIPT_NO_LSP

bool FSRefactorNames::validate_identifier(const String &p_name, String &r_reason) {
	if (p_name.is_empty()) {
		r_reason = "Name cannot be empty.";
		return false;
	}
	if (!p_name.is_valid_unicode_identifier()) {
		r_reason = vformat("'%s' is not a valid identifier.", p_name);
		return false;
	}
	if (is_reserved_keyword(p_name)) {
		r_reason = vformat("'%s' is a reserved keyword.", p_name);
		return false;
	}
	r_reason = String();
	return true;
}

String FSRefactorNames::identifier_at_column(const String &p_line, int p_column) {
	if (p_column < 0 || p_column > p_line.length()) {
		return String();
	}

	int probe_column = p_column;
	if (probe_column == p_line.length() || !is_unicode_identifier_continue(p_line[probe_column])) {
		probe_column--;
	}
	if (probe_column < 0 || !is_unicode_identifier_continue(p_line[probe_column])) {
		return String();
	}

	int start_column = probe_column;
	while (start_column > 0 && is_unicode_identifier_continue(p_line[start_column - 1])) {
		start_column--;
	}

	int end_column = probe_column + 1;
	while (end_column < p_line.length() && is_unicode_identifier_continue(p_line[end_column])) {
		end_column++;
	}

	const String identifier = p_line.substr(start_column, end_column - start_column);
	return identifier.is_valid_unicode_identifier() ? identifier : String();
}

#endif // TOOLS_ENABLED
