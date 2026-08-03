/**************************************************************************/
/*  test_editor_run_bar.h                                                 */
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

#include "editor/run/editor_run_bar.h"

#include "tests/test_macros.h"

namespace TestEditorRunBar {

TEST_CASE("[EditorRunBar] Run options menu exposes run targets configuration") {
	const Vector<EditorRunBar::RunOptionsMenuEntry> entries = EditorRunBar::build_run_options_menu_model();

	REQUIRE_EQ(entries.size(), 1);
	CHECK_EQ(entries[0].id, EditorRunBar::RUN_OPTIONS_CONFIGURE_RUN_TARGETS);
	CHECK_EQ(entries[0].label, String("Run Targets Configuration..."));
}

TEST_CASE("[EditorRunBar] A debugger stop only applies to its represented process") {
	CHECK(EditorRunBar::is_debug_session_exit_current(200, 200));
	CHECK_FALSE(EditorRunBar::is_debug_session_exit_current(200, 100));
	CHECK(EditorRunBar::is_debug_session_exit_current(0, 100));
}

} // namespace TestEditorRunBar

#endif // TOOLS_ENABLED
