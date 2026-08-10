/**************************************************************************/
/*  test_editor_board_overview.h                                          */
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

#include "editor/editor_board.h"
#include "editor/editor_board_strip.h"
#include "editor/editor_data.h"
#include "editor/editor_scene_pane_tile.h"
#include "editor/editor_scene_workspace.h"

#include "scene/gui/button.h"
#include "scene/gui/control.h"
#include "scene/gui/subviewport_container.h"
#include "scene/main/viewport.h"
#include "scene/main/window.h"
#include "scene/scene_string_names.h"

#include "tests/test_macros.h"

namespace TestEditorBoardOverview {

struct OverviewHarness {
	Control *host = nullptr;
	EditorData editor_data;
	EditorSelection *selection = nullptr;
	EditorBoardStrip *strip = nullptr;

	// Boards are laid out at the full strip rect, so the strip needs a real size before any
	// overview geometry -- scale, preview shrink, caption placement -- means anything. The
	// host is mounted in the live SceneTree so children actually fold into layout.
	void mount(int p_board_count = 3) {
		host = memnew(Control);
		host->set_custom_minimum_size(Size2(800, 600));
		SceneTree::get_singleton()->get_root()->add_child(host);
		selection = memnew(EditorSelection);
		strip = EditorBoardStrip::create(selection, &editor_data);
		host->add_child(strip);
		strip->set_anchors_and_offsets_preset(Control::PRESET_FULL_RECT);
		host->set_size(Size2(800, 600));
		strip->set_size(Size2(800, 600));
		for (int i = strip->get_board_count(); i < p_board_count; i++) {
			strip->add_board(vformat("Board %d", i + 1));
		}
		pump();
	}

	void pump(double p_delta = 0.016) {
		SceneTree::get_singleton()->process(p_delta);
		MessageQueue::get_singleton()->flush();
	}

	// Advances past the transition duration so the ease-out lands and dormancy settles.
	void settle() {
		pump(1.0);
	}

	ScenePaneTile *first_tile(int p_board_index) const {
		EditorBoard *board = p_board_index >= 0 && p_board_index < strip->get_board_count() ? strip->get_board(p_board_index) : nullptr;
		if (!board || !board->get_workspace()) {
			return nullptr;
		}
		WorkspaceLeafNode *leaf = board->get_workspace()->get_focused_leaf();
		return leaf ? leaf->get_pane_tile() : nullptr;
	}

	void unmount() {
		SceneTree::get_singleton()->get_root()->remove_child(host);
		memdelete(host);
		memdelete(selection);
	}
};

TEST_CASE("[Editor][Boards] Overview preview shrink tracks the on-screen board scale") {
	const Size2 viewport(800, 600);

	// One board fills the viewport, so nothing is shrunk.
	CHECK(EditorBoardStrip::overview_preview_shrink_for(1, viewport) == 1);

	// Three boards plus their gutters fit at roughly a third of the width, so previews
	// render at roughly a ninth of the pixels.
	CHECK(EditorBoardStrip::overview_preview_shrink_for(3, viewport) == 3);
	CHECK(EditorBoardStrip::overview_preview_shrink_for(6, viewport) == 6);

	// A degenerate viewport must still produce a legal divisor rather than a division by
	// zero propagating into SubViewportContainer::set_stretch_shrink().
	CHECK(EditorBoardStrip::overview_preview_shrink_for(4, Size2()) == 1);
	CHECK(EditorBoardStrip::overview_preview_shrink_for(0, viewport) == 1);
}

TEST_CASE("[Editor][Boards] Overview shrinks every preview viewport and restores it exactly") {
	OverviewHarness h;
	h.mount();

	Vector<Size2> baseline;
	for (int i = 0; i < h.strip->get_board_count(); i++) {
		ScenePaneTile *tile = h.first_tile(i);
		if (!tile || !tile->get_preview_3d_viewport()) {
			h.unmount();
			FAIL_CHECK("every board must expose a preview viewport to bound");
			return;
		}
		baseline.push_back(tile->get_preview_3d_viewport()->get_size());
		CHECK(tile->get_preview_render_shrink() == 1);
	}
	// A zero-sized baseline would make the shrink assertion below vacuously true.
	CHECK(baseline[0].width > 0);

	h.strip->set_overview(true);
	h.pump();

	for (int i = 0; i < h.strip->get_board_count(); i++) {
		ScenePaneTile *tile = h.first_tile(i);
		if (!tile) {
			continue;
		}
		CHECK(tile->get_preview_render_shrink() == EditorBoardStrip::overview_preview_shrink_for(h.strip->get_board_count(), Size2(800, 600)));
		// The shrink must actually reach the SubViewport. It would not if it had been
		// implemented with SubViewport::set_size(), which a stretch-enabled container makes
		// warn and no-op, silently leaving every board at full render resolution.
		CHECK(tile->get_preview_3d_viewport()->get_size().width < baseline[i].width);
	}

	h.strip->set_overview(false);
	h.settle();

	for (int i = 0; i < h.strip->get_board_count(); i++) {
		ScenePaneTile *tile = h.first_tile(i);
		if (!tile) {
			continue;
		}
		CHECK(tile->get_preview_render_shrink() == 1);
		CHECK(tile->get_preview_3d_viewport()->get_size() == baseline[i]);
	}

	h.unmount();
}

TEST_CASE("[Editor][Boards] Overview refreshes previews on a throttled tick, not every frame") {
	OverviewHarness h;
	h.mount();

	ScenePaneTile *tile = h.first_tile(0);
	if (!tile) {
		h.unmount();
		FAIL_CHECK("board 0 must own a tile");
		return;
	}
	CHECK_FALSE(tile->is_preview_refresh_throttled());

	h.strip->set_overview(true);
	CHECK(tile->is_preview_refresh_throttled());
	CHECK(h.strip->get_overview_refresh_count() == 0);

	// One second of 60 Hz frames. A per-frame refresh would tick about 60 times; the bound
	// is 15 Hz, and the count must land near that regardless of frame pacing.
	const int frames = 60;
	for (int i = 0; i < frames; i++) {
		h.pump(1.0 / 60.0);
	}
	const uint32_t ticks = h.strip->get_overview_refresh_count();
	CHECK(ticks > 0);
	CHECK(ticks < uint32_t(frames));
	CHECK(ticks >= 12);
	CHECK(ticks <= 18);

	h.strip->set_overview(false);
	h.settle();
	CHECK_FALSE(tile->is_preview_refresh_throttled());

	h.unmount();
}

TEST_CASE("[Editor][Boards] Every board is live in overview and exactly one survives the exit") {
	OverviewHarness h;
	h.mount();

	CHECK(h.strip->get_board(1)->is_dormant());
	CHECK(h.strip->get_board(2)->is_dormant());

	h.strip->set_overview(true);
	h.pump();
	CHECK(h.strip->is_overview_active());
	for (int i = 0; i < h.strip->get_board_count(); i++) {
		CHECK_FALSE(h.strip->get_board(i)->is_dormant());
	}

	// Boards stay live through the zoom back in so none of them blanks mid-motion.
	h.strip->set_overview(false);
	CHECK_FALSE(h.strip->is_overview_active());
	CHECK_FALSE(h.strip->get_board(1)->is_dormant());

	h.settle();
	int awake = 0;
	for (int i = 0; i < h.strip->get_board_count(); i++) {
		awake += h.strip->get_board(i)->is_dormant() ? 0 : 1;
	}
	CHECK(awake == 1);
	CHECK_FALSE(h.strip->get_board(h.strip->get_active_index())->is_dormant());

	h.unmount();
}

TEST_CASE("[Editor][Boards] Entering overview mid-switch keeps both boards live") {
	OverviewHarness h;
	h.mount();

	h.strip->set_active_board(1);
	// Mid-slide: the outgoing board is still awake and queued to sleep on settle.
	h.pump(0.05);
	CHECK_FALSE(h.strip->get_board(0)->is_dormant());

	h.strip->set_overview(true);
	// The queued sleep must be dropped, not merely deferred: every board is on screen now.
	h.settle();
	for (int i = 0; i < h.strip->get_board_count(); i++) {
		CHECK_FALSE(h.strip->get_board(i)->is_dormant());
	}
	CHECK(h.strip->is_overview_active());

	h.unmount();
}

TEST_CASE("[Editor][Boards] Clicking inside a shrunk board selects it without moving tile focus") {
	OverviewHarness h;
	h.mount();

	EditorSceneWorkspace *third = h.strip->get_board(2)->get_workspace();
	if (!third) {
		h.unmount();
		FAIL_CHECK("board 2 must own a workspace");
		return;
	}
	// Give the board two leaves so a focus request can name one that is not already focused.
	const int original_leaf_id = third->get_focused_leaf_id();
	WorkspaceLeafNode *extra = third->split(third->get_focused_leaf(), false, EditorSceneWorkspace::SPLIT_SIDE_SECOND);
	h.pump();
	if (!extra) {
		h.unmount();
		FAIL_CHECK("splitting board 2 must produce a second leaf");
		return;
	}
	const int focused_before = third->get_focused_leaf_id();
	const int unfocused_leaf_id = focused_before == extra->get_leaf_id() ? original_leaf_id : extra->get_leaf_id();
	CHECK(focused_before != unfocused_leaf_id);

	h.strip->set_overview(true);
	h.pump();

	// This is what an interaction inside a board raises. In the overview it must become a
	// board selection instead of retargeting the editor at that leaf.
	third->request_leaf_focus(unfocused_leaf_id);

	CHECK(h.strip->get_active_index() == 2);
	CHECK_FALSE(h.strip->is_overview_active());
	CHECK(third->get_focused_leaf_id() == focused_before);

	h.settle();
	CHECK(third->get_focused_leaf_id() == focused_before);

	h.unmount();
}

TEST_CASE("[Editor][Boards] Overview captions are unscaled, aligned to their board, and open it") {
	OverviewHarness h;
	h.mount();

	Control *overlay = h.strip->get_caption_overlay();
	if (!overlay) {
		h.unmount();
		FAIL_CHECK("the strip must own a caption overlay");
		return;
	}
	CHECK_FALSE(overlay->is_visible());

	h.strip->set_overview(true);
	h.settle();

	CHECK(overlay->is_visible());
	if (overlay->get_child_count() != h.strip->get_board_count()) {
		h.unmount();
		FAIL_CHECK("one caption per board");
		return;
	}

	const real_t board_scale = EditorBoardView::overview_scale_for(h.strip->get_board_count(), Size2(800, 600));
	for (int i = 0; i < overlay->get_child_count(); i++) {
		Button *caption = Object::cast_to<Button>(overlay->get_child(i));
		if (!caption) {
			h.unmount();
			FAIL_CHECK("captions must be real clickable controls");
			return;
		}
		CHECK(caption->get_text() == h.strip->get_board(i)->get_title());
		// Captions sit outside the scaled board container: they are laid out at their own
		// natural size, so their text stays at full font size while the boards shrink.
		CHECK(caption->get_size() == caption->get_combined_minimum_size());
		// A caption folded into the scaled container would have come out at roughly
		// board_scale times its natural height instead.
		CHECK(caption->get_size().height > caption->get_combined_minimum_size().height * board_scale * 1.5);

		// And each one is centred on the board it names.
		const Rect2 board_rect = h.strip->get_board(i)->get_rect();
		const real_t caption_center = caption->get_position().x + caption->get_size().width * 0.5;
		CHECK(Math::abs(caption_center - board_rect.get_center().x) < 1.0);
	}

	Button *last_caption = Object::cast_to<Button>(overlay->get_child(2));
	if (!last_caption) {
		h.unmount();
		FAIL_CHECK("the third caption must exist");
		return;
	}
	last_caption->emit_signal(SceneStringName(pressed));

	CHECK_FALSE(h.strip->is_overview_active());
	CHECK(h.strip->get_active_index() == 2);
	h.settle();
	CHECK_FALSE(h.strip->get_board(2)->is_dormant());
	CHECK(h.strip->get_board(0)->is_dormant());
	CHECK_FALSE(overlay->is_visible());

	h.unmount();
}

TEST_CASE("[Editor][Boards] Closing a board during overview leaves no board stranded awake") {
	OverviewHarness h;
	h.mount();

	h.strip->set_overview(true);
	h.pump();

	CHECK(h.strip->close_board(1));
	h.settle();

	CHECK_FALSE(h.strip->is_overview_active());
	CHECK(h.strip->get_board_count() == 2);
	int awake = 0;
	for (int i = 0; i < h.strip->get_board_count(); i++) {
		awake += h.strip->get_board(i)->is_dormant() ? 0 : 1;
		ScenePaneTile *tile = h.first_tile(i);
		if (tile) {
			CHECK(tile->get_preview_render_shrink() == 1);
			CHECK_FALSE(tile->is_preview_refresh_throttled());
		}
	}
	CHECK(awake == 1);

	h.unmount();
}

} // namespace TestEditorBoardOverview
