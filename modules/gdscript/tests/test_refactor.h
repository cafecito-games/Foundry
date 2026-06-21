/**************************************************************************/
/*  test_refactor.h                                                       */
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

#pragma once

#ifdef TOOLS_ENABLED

#include "tests/test_macros.h"

#include "../editor/gdscript_refactoring.h"

#include "core/io/file_access.h"

namespace GDScriptTests {

inline RefactorContext make_context(const String &p_path) {
	Error err = OK;
	String source = FileAccess::get_file_as_string(p_path, &err);
	REQUIRE_MESSAGE(err == OK, vformat("Cannot read '%s'", p_path));
	RefactorContext ctx;
	ctx.path = p_path;
	ctx.source = source;
	return ctx;
}

inline RefactorLocation caret(int p_line, int p_column) {
	RefactorLocation loc;
	loc.start_line = loc.end_line = p_line;
	loc.start_column = loc.end_column = p_column;
	return loc;
}

TEST_SUITE("[Modules][GDScript][Refactor]") {
	TEST_CASE("Engine returns no refactors for a trivial location") {
		RefactorContext ctx = make_context("modules/gdscript/tests/scripts/refactor/empty.gd");
		Vector<RefactorAvailability> available = GDScriptRefactoring::get_available_refactors(ctx, caret(0, 0));
		CHECK(available.is_empty());
	}
}

} // namespace GDScriptTests

#endif // TOOLS_ENABLED
