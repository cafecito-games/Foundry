/**************************************************************************/
/*  test_side_rail_state.h                                                */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             FOUNDRY ENGINE                             */
/*          A fork of the Godot Engine (https://godotengine.org)          */
/*                       https://www.cafecito.games                       */
/**************************************************************************/
/* Copyright (c) 2026-present Cafecito Games LLC.                         */
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

#include "editor/gui/side_rail_state.h"

#include "tests/test_macros.h"

namespace TestSideRailState {

static SideRailSideState make_state(SideRailMode p_mode, int p_drawer, int p_last) {
	SideRailSideState state;
	state.mode = p_mode;
	state.drawer_dock = p_drawer;
	state.last_drawer_dock = p_last;
	return state;
}

TEST_CASE("[Editor][SideRail] pressing a toggle covers every row of the transition table") {
	struct Case {
		const char *name = nullptr;
		SideRailSideState state;
		int pressed_dock = 0;
		int shown_dock = -1;
		SideRailToggleAction action = SideRailToggleAction::FOCUS_DOCK;
		SideRailMode mode = SideRailMode::DOCKED;
		int drawer_dock = -1;
		int last_drawer_dock = -1;
	};

	const Case cases[] = {
		{ "docked, pressing the shown dock collapses the side",
				make_state(SideRailMode::DOCKED, -1, -1), 1, 1,
				SideRailToggleAction::COLLAPSE_SIDE, SideRailMode::RAILED, -1, 1 },
		{ "docked, pressing another dock only focuses it",
				make_state(SideRailMode::DOCKED, -1, -1), 2, 1,
				SideRailToggleAction::FOCUS_DOCK, SideRailMode::DOCKED, -1, -1 },
		{ "docked with nothing shown, pressing a dock focuses it",
				make_state(SideRailMode::DOCKED, -1, -1), 0, -1,
				SideRailToggleAction::FOCUS_DOCK, SideRailMode::DOCKED, -1, -1 },
		{ "railed with the drawer closed, pressing a dock opens it",
				make_state(SideRailMode::RAILED, -1, 2), 0, -1,
				SideRailToggleAction::OPEN_DRAWER, SideRailMode::RAILED, 0, 0 },
		{ "railed, pressing another dock switches the drawer to it",
				make_state(SideRailMode::RAILED, 0, 0), 2, 0,
				SideRailToggleAction::OPEN_DRAWER, SideRailMode::RAILED, 2, 2 },
		{ "railed, pressing the open dock closes the drawer",
				make_state(SideRailMode::RAILED, 2, 2), 2, 2,
				SideRailToggleAction::CLOSE_DRAWER, SideRailMode::RAILED, -1, 2 },
	};

	for (const Case &c : cases) {
		INFO(c.name);
		const SideRailToggleResult result = side_rail_press_toggle(c.state, c.pressed_dock, c.shown_dock);
		CHECK(result.action == c.action);
		CHECK(result.state.mode == c.mode);
		CHECK(result.state.drawer_dock == c.drawer_dock);
		CHECK(result.state.last_drawer_dock == c.last_drawer_dock);
	}
}

TEST_CASE("[Editor][SideRail] the close button closes the drawer and collapses a docked side") {
	SUBCASE("from railed with an open drawer") {
		const SideRailSideState next = side_rail_close_drawer(make_state(SideRailMode::RAILED, 1, 1), -1);
		CHECK(next.mode == SideRailMode::RAILED);
		CHECK(next.drawer_dock == -1);
		CHECK(next.last_drawer_dock == 1);
	}

	SUBCASE("from docked, remembering the dock that was shown") {
		const SideRailSideState next = side_rail_close_drawer(make_state(SideRailMode::DOCKED, -1, -1), 3);
		CHECK(next.mode == SideRailMode::RAILED);
		CHECK(next.drawer_dock == -1);
		CHECK(next.last_drawer_dock == 3);
	}

	SUBCASE("from docked with nothing shown, keeping the previous memory") {
		const SideRailSideState next = side_rail_close_drawer(make_state(SideRailMode::DOCKED, -1, 2), -1);
		CHECK(next.mode == SideRailMode::RAILED);
		CHECK(next.drawer_dock == -1);
		CHECK(next.last_drawer_dock == 2);
	}
}

TEST_CASE("[Editor][SideRail] the expand button returns the side to docked") {
	const SideRailSideState next = side_rail_expand(make_state(SideRailMode::RAILED, 2, 0));
	CHECK(next.mode == SideRailMode::DOCKED);
	CHECK(next.drawer_dock == -1);
	// The dock the drawer was on is what a later collapse reopens.
	CHECK(next.last_drawer_dock == 2);
}

TEST_CASE("[Editor][SideRail] the mode toggle is reversible and restores the last drawer dock") {
	const SideRailSideState docked = make_state(SideRailMode::DOCKED, -1, -1);

	const SideRailSideState railed = side_rail_toggle_mode(docked, 1);
	CHECK(railed.mode == SideRailMode::RAILED);
	CHECK(railed.drawer_dock == 1);

	const SideRailSideState back_to_docked = side_rail_toggle_mode(railed, 1);
	CHECK(back_to_docked.mode == SideRailMode::DOCKED);
	CHECK(back_to_docked.drawer_dock == -1);

	const SideRailSideState railed_again = side_rail_toggle_mode(back_to_docked, -1);
	CHECK(railed_again.mode == SideRailMode::RAILED);
	CHECK(railed_again.drawer_dock == 1);

	SUBCASE("collapsing a side that shows nothing and has no memory leaves the drawer closed") {
		const SideRailSideState empty = side_rail_toggle_mode(make_state(SideRailMode::DOCKED, -1, -1), -1);
		CHECK(empty.mode == SideRailMode::RAILED);
		CHECK(empty.drawer_dock == -1);
	}

	SUBCASE("a closed drawer reopens on the remembered dock") {
		const SideRailSideState closed = make_state(SideRailMode::RAILED, -1, 2);
		const SideRailSideState expanded = side_rail_toggle_mode(closed, -1);
		CHECK(expanded.mode == SideRailMode::DOCKED);
		const SideRailSideState reopened = side_rail_toggle_mode(expanded, -1);
		CHECK(reopened.drawer_dock == 2);
	}
}

TEST_CASE("[Editor][SideRail] focusing a dock opens the drawer only on a railed side") {
	const SideRailSideState railed = side_rail_focus_dock(make_state(SideRailMode::RAILED, 0, 0), 2);
	CHECK(railed.drawer_dock == 2);
	CHECK(railed.last_drawer_dock == 2);

	// On a docked side the tab stack already owns the selection, so the state
	// machine must not invent a drawer dock.
	const SideRailSideState docked = side_rail_focus_dock(make_state(SideRailMode::DOCKED, -1, -1), 2);
	CHECK(docked.mode == SideRailMode::DOCKED);
	CHECK(docked.drawer_dock == -1);
	CHECK(docked.last_drawer_dock == -1);

	const SideRailSideState unknown = side_rail_focus_dock(make_state(SideRailMode::RAILED, 1, 1), -1);
	CHECK(unknown.drawer_dock == 1);
}

TEST_CASE("[Editor][SideRail] an unavailable dock is dropped from the drawer and from the memory") {
	const SideRailSideState both = side_rail_drawer_dock_unavailable(make_state(SideRailMode::RAILED, 1, 1), 1);
	CHECK(both.drawer_dock == -1);
	CHECK(both.last_drawer_dock == -1);

	const SideRailSideState memory_only = side_rail_drawer_dock_unavailable(make_state(SideRailMode::RAILED, 0, 1), 1);
	CHECK(memory_only.drawer_dock == 0);
	CHECK(memory_only.last_drawer_dock == -1);

	const SideRailSideState unrelated = side_rail_drawer_dock_unavailable(make_state(SideRailMode::RAILED, 0, 0), 2);
	CHECK(unrelated.drawer_dock == 0);
	CHECK(unrelated.last_drawer_dock == 0);

	const SideRailSideState absent = side_rail_drawer_dock_unavailable(make_state(SideRailMode::RAILED, 0, 0), -1);
	CHECK(absent.drawer_dock == 0);
}

TEST_CASE("[Editor][SideRail] per-side visibility decision over mode, drawer dock and dock count") {
	struct Case {
		const char *name = nullptr;
		SideRailSideState state;
		int dock_count = 0;
		bool side_shown = false;
		bool all_docks_shown = false;
		int single_dock = -1;
	};

	const Case cases[] = {
		{ "docked shows the side as it always was",
				make_state(SideRailMode::DOCKED, -1, -1), 3, true, true, -1 },
		{ "docked ignores a stale drawer dock",
				make_state(SideRailMode::DOCKED, 2, 2), 3, true, true, -1 },
		{ "docked with no docks still shows the side",
				make_state(SideRailMode::DOCKED, -1, -1), 0, true, true, -1 },
		{ "railed with an open drawer shows exactly that dock",
				make_state(SideRailMode::RAILED, 1, 1), 3, true, false, 1 },
		{ "railed with a closed drawer shows nothing",
				make_state(SideRailMode::RAILED, -1, 1), 3, false, false, -1 },
		{ "railed on a dock past the end of the list shows nothing",
				make_state(SideRailMode::RAILED, 3, 3), 3, false, false, -1 },
		{ "railed on an empty side shows nothing",
				make_state(SideRailMode::RAILED, 0, 0), 0, false, false, -1 },
	};

	for (const Case &c : cases) {
		INFO(c.name);
		const SideRailSideVisibility visibility = side_rail_side_visibility(c.state, c.dock_count);
		CHECK(visibility.side_shown == c.side_shown);
		CHECK(visibility.all_docks_shown == c.all_docks_shown);
		CHECK(visibility.single_dock == c.single_dock);
	}
}

} // namespace TestSideRailState
