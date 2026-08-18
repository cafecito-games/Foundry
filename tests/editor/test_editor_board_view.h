/**************************************************************************/
/*  test_editor_board_view.h                                              */
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

#include "editor/editor_board_view.h"

#include "tests/test_macros.h"

namespace TestEditorBoardView {

TEST_CASE("[Editor][BoardView] Scroll offset lands each board at the origin") {
	const Size2 viewport(1600, 900);
	CHECK(EditorBoardView::scroll_offset_for_index(0, viewport) == doctest::Approx(0.0));
	CHECK(EditorBoardView::scroll_offset_for_index(1, viewport) == doctest::Approx(-1600.0));
	CHECK(EditorBoardView::scroll_offset_for_index(3, viewport) == doctest::Approx(-4800.0));
}

TEST_CASE("[Editor][BoardView] Overview scale fits every board") {
	const Size2 viewport(1600, 900);
	// One board must not be blown up past its natural size.
	CHECK(EditorBoardView::overview_scale_for(1, viewport) == doctest::Approx(1.0));
	// Three boards plus gutters must fit inside the viewport width.
	const real_t scale = EditorBoardView::overview_scale_for(3, viewport);
	CHECK(scale < 1.0);
	CHECK(3 * viewport.width * scale + 2 * EditorBoardView::OVERVIEW_GUTTER <= viewport.width);
}

TEST_CASE("[Editor][BoardView] Point hit-testing selects the right board") {
	const Size2 viewport(1600, 900);
	EditorBoardView view;
	view.enter_overview(3, 1, viewport);

	// The centered active board contains the viewport center.
	CHECK(view.index_at_point(Point2(800, 450), 3, viewport) == 1);
	// Far above every board is empty space.
	CHECK(view.index_at_point(Point2(800, 5), 3, viewport) == -1);
}

} // namespace TestEditorBoardView
