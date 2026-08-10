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
	void mount(int p_board_count = 3, const Size2 &p_size = Size2(800, 600)) {
		host = memnew(Control);
		host->set_custom_minimum_size(p_size);
		SceneTree::get_singleton()->get_root()->add_child(host);
		selection = memnew(EditorSelection);
		strip = EditorBoardStrip::create(selection, &editor_data);
		host->add_child(strip);
		strip->set_anchors_and_offsets_preset(Control::PRESET_FULL_RECT);
		host->set_size(p_size);
		strip->set_size(p_size);
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

	// The rect a control actually occupies in its parent's space, its own canvas scale
	// included. Control::get_rect() reports the unscaled layout size, so it cannot tell a
	// board that was scaled down from one that was resized down.
	static Rect2 scaled_rect(const Control *p_control) {
		return p_control->get_transform().xform(Rect2(Point2(), p_control->get_size()));
	}

	// The width a control actually covers on screen, every ancestor scale included.
	static real_t screen_width(const Control *p_control) {
		return p_control->get_global_transform().xform(Rect2(Point2(), p_control->get_size())).size.width;
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
	// The fixed gutters eat into the width too, so at six boards the divisor is a notch
	// past six rather than exactly six.
	CHECK(EditorBoardStrip::overview_preview_shrink_for(6, viewport) == 7);

	// A degenerate viewport must still produce a legal divisor rather than a division by
	// zero propagating into SubViewportContainer::set_stretch_shrink().
	CHECK(EditorBoardStrip::overview_preview_shrink_for(4, Size2()) == 1);
	CHECK(EditorBoardStrip::overview_preview_shrink_for(0, viewport) == 1);
}

TEST_CASE("[Editor][Boards] Overview shrinks every preview viewport and restores it exactly") {
	OverviewHarness h;
	h.mount();

	// Resolving the container through the viewport keeps the assertion on the real node the
	// tile hands to the renderer, rather than on a stand-in built by the test.
	Vector<SubViewportContainer *> containers;
	Vector<SubViewport *> viewports;
	for (int i = 0; i < h.strip->get_board_count(); i++) {
		ScenePaneTile *tile = h.first_tile(i);
		SubViewport *viewport = tile ? tile->get_preview_3d_viewport() : nullptr;
		SubViewportContainer *container = viewport ? Object::cast_to<SubViewportContainer>(viewport->get_parent()) : nullptr;
		if (!container) {
			h.unmount();
			FAIL_CHECK("every board's tile must own a stretch-enabled preview container");
			return;
		}
		CHECK(tile->get_preview_render_shrink() == 1);
		CHECK(container->get_stretch_shrink() == 1);
		containers.push_back(container);
		viewports.push_back(viewport);
	}

	// The editor's own layout gives these hidden previews a degenerate size, which would
	// make a shrink assertion vacuous, so each container is driven at a known size. Sizing
	// the container is exactly what the renderer-facing path does: the container derives
	// the SubViewport's render resolution from its own rect and the shrink.
	const Size2 probe_size(400, 300);
	const int expected_shrink = EditorBoardStrip::overview_preview_shrink_for(h.strip->get_board_count(), Size2(800, 600));
	CHECK(expected_shrink > 1);

	h.strip->set_overview(true);
	h.pump();

	for (int i = 0; i < containers.size(); i++) {
		CHECK(h.first_tile(i)->get_preview_render_shrink() == expected_shrink);
		CHECK(containers[i]->get_stretch_shrink() == expected_shrink);
		// The shrink must reach the SubViewport itself. It would not have if it had been
		// implemented with SubViewport::set_size(), which a stretch-enabled container makes
		// warn and no-op, silently leaving every board rendering at full resolution.
		containers[i]->set_size(probe_size);
		CHECK(viewports[i]->get_size() == Size2i(probe_size / expected_shrink));
	}

	h.strip->set_overview(false);
	h.settle();

	for (int i = 0; i < containers.size(); i++) {
		CHECK(h.first_tile(i)->get_preview_render_shrink() == 1);
		CHECK(containers[i]->get_stretch_shrink() == 1);
		containers[i]->set_size(probe_size);
		CHECK(viewports[i]->get_size() == Size2i(probe_size));
	}

	h.unmount();
}

TEST_CASE("[Editor][Boards] Overview scales boards whole and keeps every scene viewport on screen") {
	// A typical editor window at the default UI scale. At this width a board that is
	// *resized* into the overview rect re-runs its own layout at roughly a third of the
	// width, where the in-tile docks hold their minimum widths and the scene viewport
	// absorbs the whole loss until nothing of the scene is left to recognize the board by.
	const Size2 window(1512, 982);
	const int board_count = 3;

	OverviewHarness h;
	h.mount(board_count, window);
	REQUIRE(h.strip->get_board_count() == board_count);

	// A board with no scene shows its empty placeholder instead of a tile, and a hidden
	// tile is never laid out at all. Every board gets a scene so the assertion below is
	// made against a real, laid-out scene surface.
	for (int i = 0; i < board_count; i++) {
		h.strip->set_active_board(i);
		h.settle();
		h.editor_data.add_edited_scene(-1);
		h.pump();
	}
	h.strip->set_active_board(0);
	h.settle();

	h.strip->set_overview(true);
	h.settle();
	REQUIRE(h.strip->is_overview_active());

	const real_t board_scale = EditorBoardView::overview_scale_for(board_count, window);
	CHECK(board_scale < real_t(1.0));

	for (int i = 0; i < board_count; i++) {
		EditorBoard *board = h.strip->get_board(i);
		ScenePaneTile *tile = h.first_tile(i);
		Control *content_host = tile ? tile->get_content_host() : nullptr;
		if (!board || !content_host) {
			h.unmount();
			FAIL_CHECK("every board must own a tile with a scene content host");
			return;
		}

		// The board's own layout never re-runs at the smaller size: it stays laid out at
		// the full strip rect and is shrunk by a canvas scale instead.
		CHECK(board->get_size().is_equal_approx(window));
		CHECK(Math::is_equal_approx(board->get_scale().x, board_scale));
		CHECK(Math::is_equal_approx(board->get_scale().y, board_scale));

		// The observable the defect destroyed: the scene viewport still covers real screen
		// width in the overview.
		const real_t viewport_width = OverviewHarness::screen_width(content_host);
		INFO(vformat("board %d scene viewport screen width: %f", i, viewport_width));
		CHECK(viewport_width > real_t(0.0));

		// Scaling, not reflowing, also means the miniature keeps the focused board's shape.
		const Rect2 board_rect = OverviewHarness::scaled_rect(board);
		CHECK(board_rect.size.height > real_t(0.0));
		CHECK(Math::abs(board_rect.size.width / board_rect.size.height - window.width / window.height) < real_t(0.01));
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
		CHECK(caption->get_size().is_equal_approx(caption->get_combined_minimum_size()));
		// A caption folded into the scaled container would have come out at roughly
		// board_scale times its natural height instead.
		CHECK(caption->get_size().height > caption->get_combined_minimum_size().height * board_scale * 1.5);

		// And each one is centered on the board it names, measured against the board's
		// on-screen rect rather than its unscaled layout rect.
		const Rect2 board_rect = OverviewHarness::scaled_rect(h.strip->get_board(i));
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

TEST_CASE("[Editor][Boards] A split during overview bounds the new tile immediately") {
	OverviewHarness h;
	h.mount();

	h.strip->set_overview(true);
	h.pump();
	CHECK(h.strip->is_overview_active());

	const int expected_shrink = EditorBoardStrip::overview_preview_shrink_for(h.strip->get_board_count(), Size2(800, 600));
	CHECK(expected_shrink > 1);

	EditorSceneWorkspace *workspace = h.strip->get_board(0)->get_workspace();
	WorkspaceLeafNode *new_leaf = workspace->split(workspace->get_focused_leaf(), false, EditorSceneWorkspace::SPLIT_SIDE_SECOND);
	if (!new_leaf) {
		h.unmount();
		FAIL_CHECK("splitting board 0 while the overview is up must still succeed");
		return;
	}

	ScenePaneTile *new_tile = new_leaf->get_pane_tile();
	if (!new_tile) {
		h.unmount();
		FAIL_CHECK("the split must produce a tile");
		return;
	}
	// A tile born mid-overview must join it already bounded, not render at full
	// resolution until the next overview toggle happens to re-sync it.
	CHECK(new_tile->get_preview_render_shrink() == expected_shrink);
	CHECK(new_tile->is_preview_refresh_throttled());

	h.unmount();
}

TEST_CASE("[Editor][Boards] A restore during overview leaves every preview unshrunk and unthrottled") {
	OverviewHarness h;
	h.mount();

	// Every board section is intentionally left without a persisted tiling tree, so each
	// restored board keeps its fresh default single-leaf workspace and workspace-level
	// restore never runs. That keeps this test isolated to the strip's own overview
	// bookkeeping: a per-board content restore would otherwise route its own initial
	// focus request back through the strip and incidentally clear the overview state the
	// same way a fix would, masking exactly the bug this test exists to catch.
	Ref<ConfigFile> config;
	config.instantiate();
	config->set_value("Boards", "board_count", h.strip->get_board_count());
	config->set_value("Boards", "active_board", 0);
	for (int i = 0; i < h.strip->get_board_count(); i++) {
		config->set_value("Boards", vformat("board_%d_id", i), i);
	}

	h.strip->set_overview(true);
	h.pump();
	CHECK(h.strip->is_overview_active());

	// A layout or session restore can land mid-overview; the restored strip must not
	// inherit the overview's shrunk, throttled preview bounds with no overview left to
	// undo them.
	h.strip->restore_from_config(config);
	h.pump();

	CHECK_FALSE(h.strip->is_overview_active());
	for (int i = 0; i < h.strip->get_board_count(); i++) {
		ScenePaneTile *tile = h.first_tile(i);
		if (!tile) {
			h.unmount();
			FAIL_CHECK("every restored board must own a tile");
			return;
		}
		CHECK(tile->get_preview_render_shrink() == 1);
		CHECK_FALSE(tile->is_preview_refresh_throttled());
	}

	h.unmount();
}

} // namespace TestEditorBoardOverview
