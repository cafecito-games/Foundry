/**************************************************************************/
/*  fs_refactoring_names.h                                                */
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

#pragma once

#ifdef TOOLS_ENABLED

#include "core/string/ustring.h"

#ifndef FOUNDRY_SCRIPT_NO_LSP
#include "../language_server/foundry_lsp.h"
#endif

namespace FSRefactorNames {

// Returns true if p_name is a legal, non-reserved FoundryScript identifier.
// On failure, sets r_reason to a human-readable explanation.
bool validate_identifier(const String &p_name, String &r_reason);
String identifier_at_column(const String &p_line, int p_column);

#ifndef FOUNDRY_SCRIPT_NO_LSP
// Returns true when renaming p_target to p_new_name would collide with another
// symbol declared in the same scope (the same parent in the document-symbol tree).
//
// p_root is the script's root class symbol (parser->get_symbols()); p_target is
// the parser-owned symbol being renamed (the exact pointer resolved by the
// workspace, since the parent is located by pointer identity within the tree).
//
// This check is conservative: if the target's parent scope cannot be located in
// the tree, it returns false (no collision). A genuine name clash that slips
// through is still surfaced by the editor's re-parse after the edit is applied.
bool has_scope_collision(const LSP::DocumentSymbol &p_root, const LSP::DocumentSymbol *p_target, const String &p_new_name, String &r_reason);
#endif // FOUNDRY_SCRIPT_NO_LSP

} // namespace FSRefactorNames

#endif // TOOLS_ENABLED
