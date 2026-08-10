/**************************************************************************/
/*  test_editor_automation_drag.h                                         */
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

#include "editor/automation/editor_automation_action.h"
#include "editor/automation/editor_automation_driver.h"
#include "editor/automation/editor_automation_input.h"
#include "editor/automation/editor_automation_mcp_contracts.h"
#include "editor/automation/editor_automation_mcp_dispatcher.h"
#include "editor/automation/editor_automation_server.h"
#include "editor/automation/editor_automation_snapshot.h"

#include "core/object/message_queue.h"
#include "scene/gui/tab_bar.h"
#include "scene/main/scene_tree.h"
#include "scene/main/viewport.h"
#include "scene/main/window.h"
#include "servers/display/display_server.h"

#include "tests/test_macros.h"

namespace TestEditorAutomationDrag {

class DragPayloadSource : public Control {
	FOUNDRY_CLASS(DragPayloadSource, Control);

public:
	Variant get_drag_data(const Point2 &p_point) override {
		return "drag_payload";
	}
};

class DragPayloadTarget : public Control {
	FOUNDRY_CLASS(DragPayloadTarget, Control);

public:
	bool dropped = false;
	Variant dropped_data;

	bool can_drop_data(const Point2 &p_point, const Variant &p_data) const override {
		return p_data.get_type() == Variant::STRING;
	}

	void drop_data(const Point2 &p_point, const Variant &p_data) override {
		dropped = true;
		dropped_data = p_data;
	}
};

class ForcedDrawCounter : public Object {
	FOUNDRY_CLASS(ForcedDrawCounter, Object);

public:
	int draws = 0;

	void on_frame_post_draw() {
		draws++;
	}
};

struct DragHarness {
	Window *window = nullptr;
	DragPayloadSource *source = nullptr;
	DragPayloadTarget *target = nullptr;

	void mount() {
		window = memnew(Window);
		window->set_title("Automation Drag Harness");
		window->set_size(Size2i(500, 300));
		SceneTree::get_singleton()->get_root()->add_child(window);
		window->set_visible(true);
		MessageQueue::get_singleton()->flush();

		source = memnew(DragPayloadSource);
		source->set_name("DragSource");
		source->set_size(Size2(120, 40));
		source->set_mouse_filter(Control::MOUSE_FILTER_STOP);
		window->add_child(source);

		target = memnew(DragPayloadTarget);
		target->set_name("DropTarget");
		target->set_size(Size2(120, 40));
		target->set_position(Vector2(300, 0));
		target->set_mouse_filter(Control::MOUSE_FILTER_STOP);
		window->add_child(target);
		MessageQueue::get_singleton()->flush();
	}

	void unmount() {
		memdelete(window);
	}
};

static const EditorAutomationElement *find_element(const EditorAutomationSnapshot &p_snapshot, const String &p_name) {
	for (int i = 0; i < p_snapshot.get_element_count(); i++) {
		const EditorAutomationElement &element = p_snapshot.get_element(i);
		if (element.name == p_name) {
			return &element;
		}
	}
	return nullptr;
}

static Dictionary element_target(const EditorAutomationElement *p_element) {
	Dictionary target;
	if (p_element != nullptr) {
		target["id"] = p_element->id;
	}
	return target;
}

TEST_CASE("[Editor][Automation] drag with hold leaves the gesture in flight and mouse_up drops") {
	EditorAutomationInput::reset_pointer_state();

	DragHarness harness;
	harness.mount();

	const EditorAutomationSnapshot snapshot = EditorAutomationSnapshot::capture_from_node(harness.window);
	const EditorAutomationElement *source_element = find_element(snapshot, "DragSource");
	const EditorAutomationElement *target_element = find_element(snapshot, "DropTarget");
	REQUIRE(source_element != nullptr);
	REQUIRE(target_element != nullptr);

	Dictionary options;
	options["route"] = "input";
	options["hold"] = true;
	options["target"] = element_target(target_element);

	const EditorAutomationActionResult held = EditorAutomationDriver::perform(snapshot, "drag", element_target(source_element), options);
	MessageQueue::get_singleton()->flush();
	CHECK(held.ok);
	CHECK(bool(held.details["holding"]));

	// The gesture is still in flight: the drag has begun but nothing has been
	// dropped, which is precisely the state a mid-gesture capture needs.
	CHECK(harness.window->gui_is_dragging());
	CHECK_FALSE(harness.target->dropped);
	CHECK(EditorAutomationInput::get_held_mouse_button() == MouseButton::LEFT);

	Dictionary release_options;
	release_options["route"] = "input";

	const EditorAutomationActionResult released = EditorAutomationDriver::perform(snapshot, "mouse_up", element_target(target_element), release_options);
	MessageQueue::get_singleton()->flush();
	CHECK(released.ok);
	CHECK(released.route == EditorAutomationActionRouteNames::INPUT_MOUSE_UP);
	CHECK_FALSE(harness.window->gui_is_dragging());
	CHECK(harness.target->dropped);
	CHECK(String(harness.target->dropped_data) == "drag_payload");
	CHECK(EditorAutomationInput::get_held_mouse_button() == MouseButton::NONE);

	harness.unmount();
}

TEST_CASE("[Editor][Automation] mouse_down, mouse_move and mouse_up compose a drop") {
	EditorAutomationInput::reset_pointer_state();

	DragHarness harness;
	harness.mount();

	const EditorAutomationSnapshot snapshot = EditorAutomationSnapshot::capture_from_node(harness.window);
	const EditorAutomationElement *source_element = find_element(snapshot, "DragSource");
	const EditorAutomationElement *target_element = find_element(snapshot, "DropTarget");
	REQUIRE(source_element != nullptr);
	REQUIRE(target_element != nullptr);

	Dictionary options;
	options["route"] = "input";

	const EditorAutomationActionResult down = EditorAutomationDriver::perform(snapshot, "mouse_down", element_target(source_element), options);
	MessageQueue::get_singleton()->flush();
	CHECK(down.ok);
	CHECK(down.route == EditorAutomationActionRouteNames::INPUT_MOUSE_DOWN);
	CHECK(bool(down.details["button_held"]));
	CHECK_FALSE(harness.window->gui_is_dragging());

	const EditorAutomationActionResult move = EditorAutomationDriver::perform(snapshot, "mouse_move", element_target(target_element), options);
	MessageQueue::get_singleton()->flush();
	CHECK(move.ok);
	CHECK(move.route == EditorAutomationActionRouteNames::INPUT_MOUSE_MOVE);
	CHECK(bool(move.details["dragging"]));
	CHECK_FALSE(harness.target->dropped);

	const EditorAutomationActionResult up = EditorAutomationDriver::perform(snapshot, "mouse_up", element_target(target_element), options);
	MessageQueue::get_singleton()->flush();
	CHECK(up.ok);
	CHECK(harness.target->dropped);
	CHECK(String(harness.target->dropped_data) == "drag_payload");

	harness.unmount();
}

TEST_CASE("[Editor][Automation] a completed gesture returns the system pointer to where it started") {
	EditorAutomationInput::reset_pointer_state();

	DisplayServer *display_server = DisplayServer::get_singleton();
	REQUIRE(display_server != nullptr);
	REQUIRE(display_server->has_feature(DisplayServer::FEATURE_MOUSE_WARP));

	DragHarness harness;
	harness.mount();

	const Point2i resting_pointer(7, 11);
	display_server->warp_mouse(resting_pointer);
	REQUIRE(display_server->mouse_get_position() == resting_pointer);

	const Vector2 from = harness.source->get_global_rect().get_center();
	const Vector2 to = harness.target->get_global_rect().get_center();

	PackedStringArray events;
	EditorAutomationInputModifiers modifiers;
	CHECK(EditorAutomationInput::push_mouse_drag(
			harness.window, from, to, Vector<Vector2>(), MouseButton::LEFT, modifiers, events, true));
	MessageQueue::get_singleton()->flush();

	CHECK(harness.target->dropped);
	CHECK(display_server->mouse_get_position() == resting_pointer);

	harness.unmount();
}

TEST_CASE("[Editor][Automation] a held gesture keeps the pointer until the release returns it") {
	EditorAutomationInput::reset_pointer_state();

	DisplayServer *display_server = DisplayServer::get_singleton();
	REQUIRE(display_server != nullptr);
	REQUIRE(display_server->has_feature(DisplayServer::FEATURE_MOUSE_WARP));

	DragHarness harness;
	harness.mount();

	const Point2i resting_pointer(9, 13);
	display_server->warp_mouse(resting_pointer);
	REQUIRE(display_server->mouse_get_position() == resting_pointer);

	const Vector2 from = harness.source->get_global_rect().get_center();
	const Vector2 to = harness.target->get_global_rect().get_center();

	PackedStringArray events;
	EditorAutomationInputModifiers modifiers;
	CHECK(EditorAutomationInput::push_mouse_drag(
			harness.window, from, to, Vector<Vector2>(), MouseButton::LEFT, modifiers, events, false));
	MessageQueue::get_singleton()->flush();

	// Drop tracking reads the system pointer, so an in-flight gesture has to
	// keep it at the drag position.
	CHECK(display_server->mouse_get_position() != resting_pointer);
	CHECK(EditorAutomationInput::get_held_mouse_button() == MouseButton::LEFT);

	PackedStringArray release_events;
	CHECK(EditorAutomationInput::end_mouse_gesture(harness.window, to, modifiers, release_events));
	MessageQueue::get_singleton()->flush();

	CHECK(harness.target->dropped);
	CHECK(display_server->mouse_get_position() == resting_pointer);

	harness.unmount();
}

TEST_CASE("[Editor][Automation] a failed gesture step does not strand the system pointer") {
	EditorAutomationInput::reset_pointer_state();

	DisplayServer *display_server = DisplayServer::get_singleton();
	REQUIRE(display_server != nullptr);
	REQUIRE(display_server->has_feature(DisplayServer::FEATURE_MOUSE_WARP));

	DragHarness harness;
	harness.mount();

	const Point2i resting_pointer(21, 5);
	display_server->warp_mouse(resting_pointer);
	REQUIRE(display_server->mouse_get_position() == resting_pointer);

	const Vector2 from = harness.source->get_global_rect().get_center();
	const Vector2 to = harness.target->get_global_rect().get_center();

	PackedStringArray events;
	EditorAutomationInputModifiers modifiers;
	CHECK(EditorAutomationInput::begin_mouse_gesture(harness.window, from, MouseButton::LEFT, modifiers, events));
	MessageQueue::get_singleton()->flush();

	// A move whose viewport went away mid-gesture is the abandonment case: the
	// step fails, and the pointer must not be left at the drag position.
	ERR_PRINT_OFF;
	CHECK_FALSE(EditorAutomationInput::move_mouse_gesture(nullptr, to, Vector<Vector2>(), modifiers, events));
	ERR_PRINT_ON;

	CHECK(display_server->mouse_get_position() == resting_pointer);
	CHECK(EditorAutomationInput::get_held_mouse_button() == MouseButton::NONE);

	EditorAutomationInput::reset_pointer_state();
	harness.unmount();
}

TEST_CASE("[Editor][Automation] a new press finishes a gesture that was never released") {
	EditorAutomationInput::reset_pointer_state();

	DragHarness harness;
	harness.mount();

	const Vector2 from = harness.source->get_global_rect().get_center();

	PackedStringArray events;
	EditorAutomationInputModifiers modifiers;
	CHECK(EditorAutomationInput::begin_mouse_gesture(harness.window, from, MouseButton::LEFT, modifiers, events));
	MessageQueue::get_singleton()->flush();
	CHECK(EditorAutomationInput::get_held_mouse_button() == MouseButton::LEFT);
	CHECK_FALSE(events.has("mouse_released"));

	PackedStringArray second_events;
	CHECK(EditorAutomationInput::begin_mouse_gesture(harness.window, from, MouseButton::RIGHT, modifiers, second_events));
	MessageQueue::get_singleton()->flush();

	// The stale left button is released before the new press, so exactly one
	// button is held afterwards.
	CHECK(second_events.has("mouse_released"));
	CHECK(EditorAutomationInput::get_held_mouse_button() == MouseButton::RIGHT);

	EditorAutomationInput::reset_pointer_state();
	harness.unmount();
}

TEST_CASE("[Editor][Automation] automation teardown releases a gesture left in flight") {
	EditorAutomationInput::reset_pointer_state();

	DisplayServer *display_server = DisplayServer::get_singleton();
	REQUIRE(display_server != nullptr);

	DragHarness harness;
	harness.mount();

	const Point2i resting_pointer(3, 29);
	display_server->warp_mouse(resting_pointer);
	REQUIRE(display_server->mouse_get_position() == resting_pointer);

	EditorAutomationServer *automation_server = memnew(EditorAutomationServer);
	SceneTree::get_singleton()->get_root()->add_child(automation_server);
	MessageQueue::get_singleton()->flush();

	const Vector2 from = harness.source->get_global_rect().get_center();
	const Vector2 to = harness.target->get_global_rect().get_center();

	PackedStringArray events;
	EditorAutomationInputModifiers modifiers;
	CHECK(EditorAutomationInput::push_mouse_drag(
			harness.window, from, to, Vector<Vector2>(), MouseButton::LEFT, modifiers, events, false));
	MessageQueue::get_singleton()->flush();
	REQUIRE(EditorAutomationInput::get_held_mouse_button() == MouseButton::LEFT);
	CHECK(harness.window->gui_is_dragging());

	// Leaving the tree runs the session teardown that owns the held button.
	SceneTree::get_singleton()->get_root()->remove_child(automation_server);
	memdelete(automation_server);
	MessageQueue::get_singleton()->flush();

	CHECK(EditorAutomationInput::get_held_mouse_button() == MouseButton::NONE);
	CHECK_FALSE(harness.window->gui_is_dragging());
	CHECK(display_server->mouse_get_position() == resting_pointer);

	harness.unmount();
}

TEST_CASE("[Editor][Automation] input-routed drag reorders tabs") {
	EditorAutomationInput::reset_pointer_state();

	Window *window = memnew(Window);
	window->set_title("Automation Tab Reorder");
	window->set_size(Size2i(600, 200));
	SceneTree::get_singleton()->get_root()->add_child(window);
	window->set_visible(true);

	TabBar *tab_bar = memnew(TabBar);
	tab_bar->set_name("ReorderBar");
	tab_bar->set_size(Size2(600, 40));
	tab_bar->add_tab("arena");
	tab_bar->add_tab("hud");
	tab_bar->add_tab("terrain");
	tab_bar->set_drag_to_rearrange_enabled(true);
	window->add_child(tab_bar);
	MessageQueue::get_singleton()->flush();
	SceneTree::get_singleton()->process(1.0 / 60.0);
	MessageQueue::get_singleton()->flush();

	REQUIRE(tab_bar->get_tab_count() == 3);
	CHECK(tab_bar->get_tab_title(0) == "arena");

	const Rect2 first_tab = tab_bar->get_tab_rect(0);
	const Rect2 last_tab = tab_bar->get_tab_rect(2);
	const Vector2 from = tab_bar->get_global_transform_with_canvas().xform(first_tab.get_center());
	const Vector2 to = tab_bar->get_global_transform_with_canvas().xform(Vector2(last_tab.position.x + last_tab.size.x - 2, last_tab.get_center().y));

	PackedStringArray events;
	EditorAutomationInputModifiers modifiers;
	const bool dispatched = EditorAutomationInput::push_mouse_drag(
			window, from, to, Vector<Vector2>(), MouseButton::LEFT, modifiers, events, true);
	MessageQueue::get_singleton()->flush();

	CHECK(dispatched);
	CHECK(events.has("mouse_pressed"));
	CHECK(events.has("mouse_motion"));
	CHECK(events.has("mouse_released"));
	CHECK(tab_bar->get_tab_title(2) == "arena");
	CHECK(tab_bar->get_tab_title(0) == "hud");

	memdelete(window);
}

TEST_CASE("[Editor][Automation] capture_screenshot force_draw renders a frame before reading") {
	RenderingServer *rendering_server = RenderingServer::get_singleton();
	if (rendering_server == nullptr) {
		return;
	}

	ForcedDrawCounter counter;
	rendering_server->connect("frame_post_draw", callable_mp(&counter, &ForcedDrawCounter::on_frame_post_draw));

	PanelContainer *root = memnew(PanelContainer);
	root->set_size(Size2(240, 160));
	SceneTree::get_singleton()->get_root()->add_child(root);
	MessageQueue::get_singleton()->flush();

	EditorAutomationMCPDispatcher dispatcher;
	EditorAutomationMCPDispatcher::Options dispatcher_options;
	dispatcher_options.snapshot_root = root;
	dispatcher.set_options(dispatcher_options);

	Dictionary request;
	request["jsonrpc"] = "2.0";
	request["id"] = 1;
	request["method"] = "tools/call";
	Dictionary params;
	params["name"] = "capture_screenshot";
	params["arguments"] = Dictionary();
	request["params"] = params;

	const int before_plain = counter.draws;
	dispatcher.handle_message(request);
	CHECK(counter.draws == before_plain);

	Dictionary force_arguments;
	force_arguments["force_draw"] = true;
	params["arguments"] = force_arguments;
	request["params"] = params;
	request["id"] = 2;

	const int before_forced = counter.draws;
	dispatcher.handle_message(request);
	CHECK(counter.draws > before_forced);

	rendering_server->disconnect("frame_post_draw", callable_mp(&counter, &ForcedDrawCounter::on_frame_post_draw));

	SceneTree::get_singleton()->get_root()->remove_child(root);
	memdelete(root);
}

TEST_CASE("[Editor][Automation] pointer gesture actions and hold option are advertised") {
	const PackedStringArray actions = EditorAutomationMCPContracts::action_names();
	CHECK(actions.has("mouse_down"));
	CHECK(actions.has("mouse_move"));
	CHECK(actions.has("mouse_up"));

	Dictionary args;
	args["hold"] = true;
	args["release"] = false;
	const EditorAutomationMCPParseResult<EditorAutomationMCPActionArgs> parsed = EditorAutomationMCPActionArgs::parse(args, "args");
	REQUIRE(parsed.ok);
	const Dictionary values = parsed.value.to_dictionary();
	CHECK(bool(values["hold"]));
	CHECK_FALSE(bool(values["release"]));

	Dictionary screenshot_args;
	screenshot_args["force_draw"] = true;
	const EditorAutomationMCPParseResult<EditorAutomationMCPCaptureScreenshotInput> screenshot_parsed =
			EditorAutomationMCPCaptureScreenshotInput::parse(screenshot_args);
	REQUIRE(screenshot_parsed.ok);
	CHECK(bool(screenshot_parsed.value.to_dictionary()["force_draw"]));
}

} // namespace TestEditorAutomationDrag
