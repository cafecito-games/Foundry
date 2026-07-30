/**************************************************************************/
/*  test_accessibility_server.h                                           */
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

#include "servers/display/accessibility_server.h"

#include "tests/test_macros.h"

namespace TestAccessibilityServer {

// Windowing display servers (macOS/X11/Wayland/Windows) call
// `AccessibilityServer::get_singleton()->window_create(...)` unconditionally
// when creating a window. That is only safe because startup guarantees a
// non-null singleton: the "dummy" driver is registered as the final entry and
// always constructs, so the fallback search in `Main::setup2()` is expected to
// reach it whenever the preferred driver (e.g. "accesskit") fails to load.
//
// A fallback loop bounded by `get_create_function_count() - 1` skips that last
// entry, leaving the singleton null and crashing `_create_window()`. These
// tests lock the contract that makes the full-range fallback correct.
TEST_CASE("[AccessibilityServer] dummy fallback driver is always available") {
	const int count = AccessibilityServer::get_create_function_count();
	REQUIRE(count >= 1);

	// The guaranteed fallback ("dummy") must be the last-registered driver, so
	// any fallback search that stops before the final index would miss it.
	CHECK(String("dummy") == AccessibilityServer::get_create_function_name(count - 1));

	// A full-range fallback (as startup must perform) always finds a usable
	// driver even when the preferred one fails; a `count - 1` bound would not.
	const int preferred_idx = 0;
	int fallback_idx = -1;
	for (int i = 0; i < count; i++) {
		if (i == preferred_idx) {
			continue;
		}
		fallback_idx = i;
		break;
	}
	if (count > 1) {
		CHECK(fallback_idx == count - 1);
	}
}

} // namespace TestAccessibilityServer
