/**************************************************************************/
/*  test_tile_dock_offsets.h                                              */
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

#include "core/io/config_file.h"
#include "editor/editor_tile_dock_region.h"
#include "editor/gui/side_rail_state.h"
#include "editor/themes/editor_scale.h"
#include "scene/gui/control.h"
#include "scene/gui/split_container.h"

#include "tests/test_macros.h"

namespace TestTileDockOffsets {

TEST_CASE("[Editor][TileDockOffsets] gap map resolves identities relative to the centre") {
	struct Case {
		Vector<bool> visible;
		int center_index = 0;
		int left_center = TileDockGapMap::ABSENT;
		int center_right = TileDockGapMap::ABSENT;
		int gap_count = 0;
	};

	const Case cases[] = {
		{ Vector<bool>{ true, true, true }, 1, 0, 1, 2 },
		{ Vector<bool>{ false, true, true }, 1, TileDockGapMap::ABSENT, 0, 1 },
		{ Vector<bool>{ true, true, false }, 1, 0, TileDockGapMap::ABSENT, 1 },
		{ Vector<bool>{ false, true, false }, 1, TileDockGapMap::ABSENT, TileDockGapMap::ABSENT, 0 },
		{ Vector<bool>{ true, false, true }, 1, TileDockGapMap::ABSENT, TileDockGapMap::ABSENT, 1 },
		{ Vector<bool>{ true, true, true, true }, 2, 1, 2, 3 },
		{ Vector<bool>{ false, true, true, true }, 2, 0, 1, 2 },
		{ Vector<bool>{ true, true, true }, -1, TileDockGapMap::ABSENT, TileDockGapMap::ABSENT, 2 },
		{ Vector<bool>{ true, true, true }, 3, TileDockGapMap::ABSENT, TileDockGapMap::ABSENT, 2 },
		{ Vector<bool>(), 0, TileDockGapMap::ABSENT, TileDockGapMap::ABSENT, 0 },
	};

	for (const Case &c : cases) {
		const TileDockGapMap map = tile_dock_gap_map(c.visible, c.center_index);
		CHECK(map.left_center == c.left_center);
		CHECK(map.center_right == c.center_right);
		CHECK(map.gap_count == c.gap_count);
	}
}

struct TileDockFixture {
	HSplitContainer *body = nullptr;
	Control *left = nullptr;
	Control *center = nullptr;
	EditorTileDockRegion region;

	TileDockFixture() {
		body = memnew(HSplitContainer);
		left = memnew(Control);
		body->add_child(left);
		center = memnew(Control);
		body->add_child(center);
		region.attach(body, center);
	}

	~TileDockFixture() {
		memdelete(body);
	}

	Control *right_tabs() const {
		return Object::cast_to<Control>(region.get_right_tabs());
	}

	void set_column_visible(bool p_left, bool p_center, bool p_right) {
		left->set_visible(p_left);
		center->set_visible(p_center);
		if (right_tabs()) {
			right_tabs()->set_visible(p_right);
		}
	}
};

TEST_CASE("[Editor][TileDockOffsets] save writes both keys when all columns are visible") {
	TileDockFixture fixture;
	fixture.set_column_visible(true, true, true);

	PackedInt32Array offsets;
	offsets.push_back(220);
	offsets.push_back(-240);
	fixture.body->set_split_offsets(offsets);

	Ref<ConfigFile> config;
	config.instantiate();
	const String section = "WorkspaceLeaf_0";
	fixture.region.save_layout(config, section);

	REQUIRE(config->has_section_key(section, "tile_dock_hsplit_1"));
	REQUIRE(config->has_section_key(section, "tile_dock_hsplit_2"));
	CHECK(int(config->get_value(section, "tile_dock_hsplit_1")) == int(220 / EDSCALE));
	CHECK(int(config->get_value(section, "tile_dock_hsplit_2")) == int(-240 / EDSCALE));
}

TEST_CASE("[Editor][TileDockOffsets] save leaves absent gaps untouched") {
	TileDockFixture fixture;
	Ref<ConfigFile> config;
	config.instantiate();
	const String section = "WorkspaceLeaf_0";
	config->set_value(section, "tile_dock_hsplit_1", 111);
	config->set_value(section, "tile_dock_hsplit_2", 222);

	SUBCASE("left column hidden") {
		fixture.set_column_visible(false, true, true);
		PackedInt32Array offsets;
		offsets.push_back(333);
		fixture.body->set_split_offsets(offsets);

		fixture.region.save_layout(config, section);

		CHECK(int(config->get_value(section, "tile_dock_hsplit_1")) == 111);
		CHECK(int(config->get_value(section, "tile_dock_hsplit_2")) == int(333 / EDSCALE));
	}

	SUBCASE("right column hidden") {
		fixture.set_column_visible(true, true, false);
		PackedInt32Array offsets;
		offsets.push_back(444);
		fixture.body->set_split_offsets(offsets);

		fixture.region.save_layout(config, section);

		CHECK(int(config->get_value(section, "tile_dock_hsplit_1")) == int(444 / EDSCALE));
		CHECK(int(config->get_value(section, "tile_dock_hsplit_2")) == 222);
	}

	SUBCASE("both columns hidden") {
		fixture.set_column_visible(false, true, false);
		PackedInt32Array offsets;
		offsets.push_back(555);
		fixture.body->set_split_offsets(offsets);

		fixture.region.save_layout(config, section);

		CHECK(int(config->get_value(section, "tile_dock_hsplit_1")) == 111);
		CHECK(int(config->get_value(section, "tile_dock_hsplit_2")) == 222);
	}
}

TEST_CASE("[Editor][TileDockOffsets] save does not create keys for absent gaps") {
	TileDockFixture fixture;
	fixture.set_column_visible(false, true, false);

	Ref<ConfigFile> config;
	config.instantiate();
	const String section = "WorkspaceLeaf_0";
	fixture.region.save_layout(config, section);

	CHECK_FALSE(config->has_section_key(section, "tile_dock_hsplit_1"));
	CHECK_FALSE(config->has_section_key(section, "tile_dock_hsplit_2"));
}

TEST_CASE("[Editor][TileDockOffsets] load applies keys by gap identity") {
	TileDockFixture fixture;
	Ref<ConfigFile> config;
	config.instantiate();
	const String section = "WorkspaceLeaf_0";
	config->set_value(section, "tile_dock_hsplit_1", 180);
	config->set_value(section, "tile_dock_hsplit_2", 260);

	SUBCASE("all columns visible") {
		fixture.set_column_visible(true, true, true);
		fixture.region.load_layout(config, section);

		PackedInt32Array offsets = fixture.body->get_split_offsets();
		REQUIRE(offsets.size() == 2);
		CHECK(offsets[0] == 180 * EDSCALE);
		CHECK(offsets[1] == 260 * EDSCALE);
	}

	SUBCASE("left hidden uses tile_dock_hsplit_2 for the surviving gap") {
		fixture.set_column_visible(false, true, true);
		fixture.region.load_layout(config, section);

		PackedInt32Array offsets = fixture.body->get_split_offsets();
		REQUIRE(offsets.size() == 1);
		CHECK(offsets[0] == 260 * EDSCALE);
	}

	SUBCASE("right hidden uses tile_dock_hsplit_1 for the surviving gap") {
		fixture.set_column_visible(true, true, false);
		fixture.region.load_layout(config, section);

		PackedInt32Array offsets = fixture.body->get_split_offsets();
		REQUIRE(offsets.size() == 1);
		CHECK(offsets[0] == 180 * EDSCALE);
	}
}

TEST_CASE("[Editor][TileDockOffsets] load initializes missing offset slots to zero") {
	TileDockFixture fixture;
	fixture.set_column_visible(true, true, true);

	Ref<ConfigFile> config;
	config.instantiate();
	const String section = "WorkspaceLeaf_0";
	config->set_value(section, "tile_dock_hsplit_1", 150);

	fixture.region.load_layout(config, section);

	PackedInt32Array offsets = fixture.body->get_split_offsets();
	REQUIRE(offsets.size() == 2);
	CHECK(offsets[0] == 150 * EDSCALE);
	CHECK(offsets[1] == 0);
}

TEST_CASE("[Editor][TileDockOffsets] load sizes offsets to MAX(1, gap_count)") {
	TileDockFixture fixture;
	Ref<ConfigFile> config;
	config.instantiate();
	const String section = "WorkspaceLeaf_0";

	SUBCASE("both sides hidden") {
		fixture.set_column_visible(false, true, false);
		fixture.region.load_layout(config, section);

		PackedInt32Array offsets = fixture.body->get_split_offsets();
		CHECK(offsets.size() == 1);
	}

	SUBCASE("one side hidden") {
		fixture.set_column_visible(false, true, true);
		fixture.region.load_layout(config, section);

		PackedInt32Array offsets = fixture.body->get_split_offsets();
		CHECK(offsets.size() == 1);
	}
}

TEST_CASE("[Editor][TileDockOffsets] round trip preserves widths across a collapsed save") {
	TileDockFixture fixture;
	const String section = "WorkspaceLeaf_0";
	Ref<ConfigFile> config;
	config.instantiate();

	fixture.set_column_visible(true, true, true);
	PackedInt32Array original;
	original.push_back(220);
	original.push_back(-240);
	fixture.body->set_split_offsets(original);
	fixture.region.save_layout(config, section);

	fixture.set_column_visible(false, true, true);
	PackedInt32Array collapsed;
	collapsed.push_back(-240);
	fixture.body->set_split_offsets(collapsed);
	fixture.region.save_layout(config, section);

	fixture.set_column_visible(true, true, true);
	fixture.region.load_layout(config, section);

	PackedInt32Array restored = fixture.body->get_split_offsets();
	REQUIRE(restored.size() == 2);
	CHECK(restored[0] == int(220 / EDSCALE) * EDSCALE);
	CHECK(restored[1] == int(-240 / EDSCALE) * EDSCALE);
}

} // namespace TestTileDockOffsets
