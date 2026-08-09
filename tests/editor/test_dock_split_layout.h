/**************************************************************************/
/*  test_dock_split_layout.h                                              */
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

#include "editor/docks/dock_split_layout.h"

#include "tests/test_macros.h"

// Headless, node-free unit tests for the dock split layout helpers (see #1995).
// `EditorDockManager` is not constructible in the doctest environment, so these
// exercises drive the pure helper pair directly with synthetic visibility masks
// and offset arrays.

namespace TestDockSplitLayout {

// ---------------------------------------------------------------------------
// dock_split_layout_writes
// ---------------------------------------------------------------------------

TEST_CASE("[DockSplitLayout][Editor] writes nothing for a hidden column and never emits a zero placeholder") {
	Vector<bool> visible = { false, true };
	Vector<int32_t> offsets;
	offsets.push_back(100);

	Vector<DockHSplitWrite> out = dock_split_layout_writes(visible, offsets);

	REQUIRE(out.size() == 1);
	CHECK(out[0].split_index == 1);
	CHECK(out[0].offset == 100);
}

TEST_CASE("[DockSplitLayout][Editor] writes all columns in order when every split is visible") {
	Vector<bool> visible = { true, true, true, true };
	Vector<int32_t> offsets = { 10, 20, 30, 40 };

	Vector<DockHSplitWrite> out = dock_split_layout_writes(visible, offsets);

	REQUIRE(out.size() == 4);
	CHECK(out[0].split_index == 0);
	CHECK(out[0].offset == 10);
	CHECK(out[1].split_index == 1);
	CHECK(out[1].offset == 20);
	CHECK(out[2].split_index == 2);
	CHECK(out[2].offset == 30);
	CHECK(out[3].split_index == 3);
	CHECK(out[3].offset == 40);
}

TEST_CASE("[DockSplitLayout][Editor] writes keep the offset cursor in sync across a hidden middle split") {
	// A hidden split between two visible ones must not desynchronise the cursor:
	// offset [a,b,c] maps to visible columns 0, 2, 3.
	Vector<bool> visible = { true, false, true, true };
	Vector<int32_t> offsets = { 10, 20, 30 };

	Vector<DockHSplitWrite> out = dock_split_layout_writes(visible, offsets);

	REQUIRE(out.size() == 3);
	CHECK(out[0].split_index == 0);
	CHECK(out[0].offset == 10);
	CHECK(out[1].split_index == 2);
	CHECK(out[1].offset == 20);
	CHECK(out[2].split_index == 3);
	CHECK(out[2].offset == 30);
}

TEST_CASE("[DockSplitLayout][Editor] writes omit, rather than zero, visible splits when offsets run short") {
	Vector<bool> visible = { true, true, true, true };
	Vector<int32_t> offsets = { 10, 20 };

	Vector<DockHSplitWrite> out = dock_split_layout_writes(visible, offsets);

	REQUIRE(out.size() == 2);
	CHECK(out[0].split_index == 0);
	CHECK(out[0].offset == 10);
	CHECK(out[1].split_index == 1);
	CHECK(out[1].offset == 20);
}

TEST_CASE("[DockSplitLayout][Editor] writes ignore surplus offsets beyond the visible splits") {
	Vector<bool> visible = { true, false, false, false };
	Vector<int32_t> offsets = { 10, 20, 30 };

	Vector<DockHSplitWrite> out = dock_split_layout_writes(visible, offsets);

	REQUIRE(out.size() == 1);
	CHECK(out[0].split_index == 0);
	CHECK(out[0].offset == 10);
}

TEST_CASE("[DockSplitLayout][Editor] writes return an empty list when there are no splits or none are visible") {
	// All hidden.
	{
		Vector<bool> visible = { false, false };
		Vector<int32_t> offsets = { 10, 20 };

		Vector<DockHSplitWrite> out = dock_split_layout_writes(visible, offsets);

		CHECK(out.size() == 0);
	}
	// No splits at all.
	{
		Vector<bool> visible;
		Vector<int32_t> offsets;

		Vector<DockHSplitWrite> out = dock_split_layout_writes(visible, offsets);

		CHECK(out.size() == 0);
	}
}

// ---------------------------------------------------------------------------
// dock_split_layout_offsets
// ---------------------------------------------------------------------------

TEST_CASE("[DockSplitLayout][Editor] offsets return one entry per visible column in split order") {
	// count(visible == true) entries, skipping hidden columns' stored values.
	Vector<bool> visible = { true, false, true, true };
	Vector<DockHSplitStored> stored;
	stored.resize(4);
	for (int i = 0; i < 4; i++) {
		stored.write[i].present = true;
		stored.write[i].offset = (i + 1) * 10; // 10, 20, 30, 40
	}

	Vector<int32_t> out = dock_split_layout_offsets(visible, stored);

	REQUIRE(out.size() == 3);
	CHECK(out[0] == 10);
	CHECK(out[1] == 30);
	CHECK(out[2] == 40);
}

TEST_CASE("[DockSplitLayout][Editor] offsets contribute 0 in place for an absent key instead of shifting") {
	// All visible, but stored presence [T, F, T, T]: the absent key yields 0 at
	// its own position rather than shortening the array and shifting later columns.
	Vector<bool> visible = { true, true, true, true };
	Vector<DockHSplitStored> stored;
	stored.resize(4);
	stored.write[0] = { true, 10 };
	stored.write[1] = { false, 0 }; // absent
	stored.write[2] = { true, 30 };
	stored.write[3] = { true, 40 };

	Vector<int32_t> out = dock_split_layout_offsets(visible, stored);

	REQUIRE(out.size() == 4);
	CHECK(out[0] == 10);
	CHECK(out[1] == 0);
	CHECK(out[2] == 30);
	CHECK(out[3] == 40);
}

TEST_CASE("[DockSplitLayout][Editor] offsets report an error and return empty on a size mismatch") {
	Vector<bool> visible = { true, true };
	Vector<DockHSplitStored> stored;
	stored.resize(3); // Deliberately disagrees with `visible`.

	ERR_PRINT_OFF;
	Vector<int32_t> out = dock_split_layout_offsets(visible, stored);
	ERR_PRINT_ON;

	CHECK(out.size() == 0);
}

// ---------------------------------------------------------------------------
// Round trip
// ---------------------------------------------------------------------------

TEST_CASE("[DockSplitLayout][Editor] writes then offsets round-trip offsets unchanged") {
	// For any visibility mask V and offsets O with |O| == count(V), feeding
	// dock_split_layout_writes(V, O) into an all-present stored map and passing it
	// back through dock_split_layout_offsets(V, ...) returns O unchanged.
	struct Row {
		Vector<bool> visible;
		Vector<int32_t> offsets;
	};

	const Row rows[] = {
		{ { true, true, true, true }, { 10, 20, 30, 40 } },
		{ { true, false, true, true }, { 10, 20, 30 } },
		{ { true, false, false, false }, { 10 } },
		{ { false, false, false, false }, {} },
		{ { false, true, false, true }, { 70, 80 } },
		{ {}, {} },
	};

	for (const Row &row : rows) {
		Vector<DockHSplitWrite> writes = dock_split_layout_writes(row.visible, row.offsets);

		// Rebuild a full stored vector from the writes: every written column is
		// present with its offset; unwritten (hidden) columns stay absent/irrelevant.
		Vector<DockHSplitStored> stored;
		stored.resize(row.visible.size());
		for (const DockHSplitWrite &w : writes) {
			stored.write[w.split_index].present = true;
			stored.write[w.split_index].offset = w.offset;
		}

		Vector<int32_t> result = dock_split_layout_offsets(row.visible, stored);

		INFO("round-trip failed for a visibility mask");
		REQUIRE(result.size() == row.offsets.size());
		for (int i = 0; i < row.offsets.size(); i++) {
			CHECK(result[i] == row.offsets[i]);
		}
	}
}

} // namespace TestDockSplitLayout
