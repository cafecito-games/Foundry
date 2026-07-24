/**************************************************************************/
/*  fs_refactoring_edits.h                                                */
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

#include "fs_refactoring.h"

namespace FSRefactorEdits {

// Applies edits to p_source. Returns false (leaving r_result unchanged) if any
// edits overlap or address out-of-range positions. Edits may be supplied in any
// order; they are sorted and applied last-to-first internally.
bool apply(const String &p_source, const Vector<RefactorTextEdit> &p_edits, String &r_result);
int find_edit_at_location(const Vector<RefactorTextEdit> &p_edits, const RefactorLocation &p_location);

// Sorts edits by start position (ties broken by end position). Already-sorted
// vectors are left untouched, so equal-position edits keep their order.
void sort_edits(Vector<RefactorTextEdit> &r_edits);

// Checks the source-independent edit invariants shared by all refactors: every
// edit spans a non-negative ordered range, edits are sorted by start position,
// and no two edits overlap. Returns true when they hold; otherwise fills
// r_error with a description of the first violation.
bool validate_structure(const Vector<RefactorTextEdit> &p_edits, String &r_error);

// validate_structure plus source-dependent invariants: every range resolves to
// a valid offset in p_source and every expected-text guard matches the source.
bool validate(const String &p_source, const Vector<RefactorTextEdit> &p_edits, String &r_error);

} // namespace FSRefactorEdits

#endif // TOOLS_ENABLED
