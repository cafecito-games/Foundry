/**************************************************************************/
/*  fs_refactoring_match_cases.h                                          */
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

class FSParseResultProvider;

// Fill Missing Match Cases: with the caret inside a `match` over a tagged union, generate one
// branch per case the analyzer proved uncovered, plus a `null:` arm when the subject is nullable
// and its `null` value is uncovered. The uncovered set is read from the analyzed `MatchNode`
// (`uncovered_case_names` / `uncovered_includes_null`), never re-derived here.
namespace FSRefactorMatchCases {

struct FillMatchCasesCandidate {
	bool enabled = false;
	String disabled_reason; // Populated when !enabled.
	// The single edit that writes the generated arms: an insertion after the last branch, or a
	// replacement of the `pass` placeholder lines of a branchless match.
	RefactorTextEdit edit;
	// Position of the first generated `pass` body, relative to the edit's start line and absolute
	// in columns. The editor parks the caret there so the user can start typing the first body.
	int anchor_line_offset = 0;
	int anchor_column = 0;
};

FillMatchCasesCandidate find_candidate(
		const RefactorContext &p_context,
		const RefactorLocation &p_location,
		const FSParseResultProvider *p_parse_results);

RefactorResult prepare(
		const RefactorContext &p_context,
		const RefactorLocation &p_location,
		const FSParseResultProvider *p_parse_results);

} // namespace FSRefactorMatchCases

#endif // TOOLS_ENABLED
