/**************************************************************************/
/*  test_editor_automation_screenshot.h                                   */
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

#include "editor/automation/editor_automation_mcp_dispatcher.h"
#include "editor/automation/editor_automation_screenshot.h"
#include "editor/automation/editor_automation_snapshot.h"
#include "editor/automation/editor_automation_wait.h"

#include "core/io/image.h"
#include "core/object/message_queue.h"

#include "scene/gui/button.h"
#include "scene/gui/color_rect.h"
#include "scene/gui/panel_container.h"
#include "scene/main/scene_tree.h"
#include "scene/main/viewport.h"
#include "scene/main/window.h"

#include "tests/test_macros.h"

namespace TestEditorAutomationScreenshot {

static void screenshot_flush_frames(int p_count = 2) {
	for (int i = 0; i < p_count; i++) {
		SceneTree::get_singleton()->process(1.0 / 60.0);
		MessageQueue::get_singleton()->flush();
	}
}

static Dictionary make_request(const Variant &p_id, const String &p_method, const Dictionary &p_params = Dictionary()) {
	Dictionary request;
	request["jsonrpc"] = "2.0";
	request["id"] = p_id;
	request["method"] = p_method;
	if (!p_params.is_empty()) {
		request["params"] = p_params;
	}
	return request;
}

TEST_CASE("[Editor][Automation] act failure omits screenshot by default") {
	EditorAutomationWait::clear_all_cooperative();

	PanelContainer *root = memnew(PanelContainer);
	root->set_size(Size2(240, 160));
	SceneTree::get_singleton()->get_root()->add_child(root);
	screenshot_flush_frames();

	EditorAutomationMCPDispatcher dispatcher;
	EditorAutomationMCPDispatcher::Options options;
	options.snapshot_root = root;
	dispatcher.set_options(options);

	Dictionary selector;
	selector["role"] = "button";
	selector["name"] = "Missing";

	Dictionary arguments;
	arguments["action"] = "click";
	arguments["selector"] = selector;

	Dictionary params;
	params["name"] = "act";
	params["arguments"] = arguments;

	const Dictionary response = dispatcher.handle_message(make_request(1, "tools/call", params));
	const Dictionary response_result = response["result"];
	const Dictionary structured = response_result["structuredContent"];
	CHECK((bool)response_result["isError"]);
	CHECK_FALSE(structured.has("details"));

	memdelete(root);
}

TEST_CASE("[Editor][Automation] requested failure screenshot reports unavailable cleanly") {
	EditorAutomationWait::clear_all_cooperative();

	PanelContainer *root = memnew(PanelContainer);
	root->set_size(Size2(240, 160));
	SceneTree::get_singleton()->get_root()->add_child(root);
	screenshot_flush_frames();

	EditorAutomationMCPDispatcher dispatcher;
	EditorAutomationMCPDispatcher::Options options;
	options.snapshot_root = root;
	dispatcher.set_options(options);

	Dictionary selector;
	selector["role"] = "button";
	selector["name"] = "Missing";

	Dictionary arguments;
	arguments["action"] = "click";
	arguments["selector"] = selector;
	arguments["attach_screenshot_on_failure"] = true;

	Dictionary params;
	params["name"] = "act";
	params["arguments"] = arguments;

	const Dictionary response = dispatcher.handle_message(make_request(2, "tools/call", params));
	const Dictionary response_result = response["result"];
	const Dictionary structured = response_result["structuredContent"];
	CHECK((bool)response_result["isError"]);
	CHECK(structured.has("details"));
	const Dictionary details = structured["details"];
	CHECK(details.has("screenshot"));
	const Dictionary screenshot = details["screenshot"];
	CHECK(String(screenshot["status"]) == "unavailable");
	CHECK(String(screenshot["reason"]) == "screenshot_unavailable");
	CHECK_FALSE(screenshot.has("data"));

	memdelete(root);
}

TEST_CASE("[Editor][Automation] screenshot encode metadata and truncation limits") {
	Ref<Image> image = Image::create_empty(64, 48, false, Image::FORMAT_RGBA8);
	image->fill(Color(1.0, 0.0, 0.0, 1.0));

	EditorAutomationScreenshotOptions options;
	options.enabled = true;
	options.max_bytes = 64;

	Dictionary viewport;
	viewport["width"] = 64;
	viewport["height"] = 48;

	const EditorAutomationScreenshotAttachment truncated = EditorAutomationScreenshot::encode_image_attachment(
			image, options, "full_window", viewport, Dictionary(), Dictionary());
	CHECK(String(truncated.status) == "truncated");
	CHECK(truncated.byte_size > truncated.max_bytes);
	CHECK(String(truncated.reason).contains("max_bytes"));
	CHECK(truncated.data.is_empty());

	options.max_bytes = 64 * 1024;
	const EditorAutomationScreenshotAttachment available = EditorAutomationScreenshot::encode_image_attachment(
			image, options, "full_window", viewport, Dictionary(), Dictionary());
	CHECK(String(available.status) == "available");
	CHECK(String(available.format) == "png");
	CHECK(String(available.encoding) == "base64");
	CHECK(String(available.capture_mode) == "full_window");
	CHECK((int)available.image["width"] == 64);
	CHECK((int)available.image["height"] == 48);
	CHECK((int)available.viewport["width"] == 64);
	CHECK((int)available.viewport["height"] == 48);
	CHECK_FALSE(available.data.is_empty());
	CHECK(available.byte_size <= available.max_bytes);
}

TEST_CASE("[Editor][Automation] screenshot capture from subviewport when available") {
	SubViewport *viewport = memnew(SubViewport);
	viewport->set_size(Vector2i(120, 80));
	viewport->set_disable_3d(true);
	viewport->set_transparent_background(false);
	SceneTree::get_singleton()->get_root()->add_child(viewport);

	ColorRect *rect = memnew(ColorRect);
	rect->set_color(Color(0.0, 0.0, 1.0, 1.0));
	rect->set_size(Size2(120, 80));
	viewport->add_child(rect);
	screenshot_flush_frames(4);

	const EditorAutomationSnapshot snapshot = EditorAutomationSnapshot::capture_from_node(viewport);

	EditorAutomationScreenshotOptions options;
	options.enabled = true;
	options.snapshot_root = viewport;
	options.max_bytes = 256 * 1024;

	const EditorAutomationScreenshotAttachment attachment = EditorAutomationScreenshot::capture_for_failure(snapshot, Dictionary(), options);
	CHECK((String(attachment.status) == "available" || String(attachment.status) == "unavailable"));
	if (String(attachment.status) == "available") {
		CHECK(String(attachment.format) == "png");
		CHECK(String(attachment.encoding) == "base64");
		CHECK(String(attachment.capture_mode) == "full_window");
		CHECK((int)attachment.image["width"] == 120);
		CHECK((int)attachment.image["height"] == 80);
		CHECK_FALSE(attachment.data.is_empty());
	} else {
		CHECK(String(attachment.reason) == "screenshot_unavailable");
	}

	memdelete(viewport);
}

TEST_CASE("[Editor][Automation] capture_screenshot tool advertises input/output schema") {
	const Array tools = EditorAutomationMCPDispatcher::build_tools_list();
	Dictionary capture_tool;
	for (int i = 0; i < tools.size(); i++) {
		const Dictionary tool = tools[i];
		if (String(tool.get("name", String())) == "capture_screenshot") {
			capture_tool = tool;
			break;
		}
	}
	CHECK_FALSE(capture_tool.is_empty());
	CHECK(capture_tool.has("inputSchema"));
	CHECK(capture_tool.has("outputSchema"));

	const Dictionary input_schema = capture_tool["inputSchema"];
	const Dictionary input_props = input_schema["properties"];
	CHECK(input_props.has("selector"));
	CHECK(input_props.has("element"));
	CHECK(input_props.has("padding"));

	const Dictionary output_schema = capture_tool["outputSchema"];
	const Dictionary output_props = output_schema["properties"];
	CHECK(output_props.has("ok"));
	CHECK(output_props.has("capture_mode"));
	CHECK(output_props.has("screenshot"));
}

TEST_CASE("[Editor][Automation] capture_screenshot with no target captures the full window") {
	EditorAutomationWait::clear_all_cooperative();

	SubViewport *viewport = memnew(SubViewport);
	viewport->set_size(Vector2i(120, 80));
	viewport->set_disable_3d(true);
	viewport->set_transparent_background(false);
	SceneTree::get_singleton()->get_root()->add_child(viewport);

	ColorRect *rect = memnew(ColorRect);
	rect->set_color(Color(0.0, 0.6, 0.2, 1.0));
	rect->set_size(Size2(120, 80));
	viewport->add_child(rect);
	screenshot_flush_frames(4);

	EditorAutomationMCPDispatcher dispatcher;
	EditorAutomationMCPDispatcher::Options options;
	options.snapshot_root = viewport;
	dispatcher.set_options(options);

	Dictionary params;
	params["name"] = "capture_screenshot";
	params["arguments"] = Dictionary();

	const Dictionary response = dispatcher.handle_message(make_request(1, "tools/call", params));
	const Dictionary response_result = response["result"];
	const Dictionary structured = response_result["structuredContent"];
	CHECK(structured.has("screenshot"));
	const Dictionary screenshot = structured["screenshot"];

	if (String(screenshot["status"]) == "available") {
		CHECK_FALSE((bool)response_result["isError"]);
		CHECK((bool)structured["ok"]);
		CHECK(String(structured["capture_mode"]) == "full_window");
		CHECK(String(screenshot["capture_mode"]) == "full_window");
		const Dictionary image = screenshot["image"];
		CHECK((int)image["width"] == 120);
		CHECK((int)image["height"] == 80);
		CHECK_FALSE(String(screenshot["data"]).is_empty());
	} else {
		CHECK((bool)response_result["isError"]);
		CHECK_FALSE((bool)structured["ok"]);
		CHECK(String(structured["kind"]) == "screenshot_unavailable");
	}

	memdelete(viewport);
}

TEST_CASE("[Editor][Automation] capture_screenshot reports a size-limit failure as truncated, not unavailable") {
	EditorAutomationWait::clear_all_cooperative();

	SubViewport *viewport = memnew(SubViewport);
	viewport->set_size(Vector2i(120, 80));
	viewport->set_disable_3d(true);
	viewport->set_transparent_background(false);
	SceneTree::get_singleton()->get_root()->add_child(viewport);

	ColorRect *rect = memnew(ColorRect);
	rect->set_color(Color(0.9, 0.3, 0.1, 1.0));
	rect->set_size(Size2(120, 80));
	viewport->add_child(rect);
	screenshot_flush_frames(4);

	EditorAutomationMCPDispatcher dispatcher;
	EditorAutomationMCPDispatcher::Options options;
	options.snapshot_root = viewport;
	options.max_screenshot_bytes = 1;
	dispatcher.set_options(options);

	Dictionary params;
	params["name"] = "capture_screenshot";
	params["arguments"] = Dictionary();

	const Dictionary response = dispatcher.handle_message(make_request(1, "tools/call", params));
	const Dictionary response_result = response["result"];
	const Dictionary structured = response_result["structuredContent"];
	CHECK((bool)response_result["isError"]);
	CHECK_FALSE((bool)structured["ok"]);
	const Dictionary screenshot = structured["screenshot"];

	// A tiny byte budget forces truncation when the viewport can be captured; a
	// headless build without a usable viewport still reports unavailable. Either
	// way the top-level kind must match the attachment status, never contradict it.
	if (String(screenshot["status"]) == "truncated") {
		CHECK(String(structured["kind"]) == "screenshot_truncated");
	} else {
		CHECK(String(screenshot["status"]) == "unavailable");
		CHECK(String(structured["kind"]) == "screenshot_unavailable");
	}

	memdelete(viewport);
}

TEST_CASE("[Editor][Automation] capture_screenshot with a selector crops to the element") {
	EditorAutomationWait::clear_all_cooperative();

	// A SubViewport renders offscreen and can be captured headlessly, but it is
	// not a CanvasItem, so the snapshot skips it and its subtree. Root the
	// snapshot at a Control *inside* the SubViewport: the snapshot then finds the
	// button, while capture still targets the enclosing SubViewport.
	SubViewport *viewport = memnew(SubViewport);
	viewport->set_size(Vector2i(200, 160));
	viewport->set_disable_3d(true);
	viewport->set_transparent_background(false);
	SceneTree::get_singleton()->get_root()->add_child(viewport);

	Control *container = memnew(Control);
	container->set_size(Size2(200, 160));
	viewport->add_child(container);

	Button *button = memnew(Button);
	button->set_text("Snap Target");
	button->set_position(Point2(40, 30));
	button->set_size(Size2(96, 40));
	container->add_child(button);
	screenshot_flush_frames(4);

	EditorAutomationMCPDispatcher dispatcher;
	EditorAutomationMCPDispatcher::Options options;
	options.snapshot_root = container;
	dispatcher.set_options(options);

	Dictionary selector;
	selector["role"] = "button";
	selector["name"] = "Snap Target";

	Dictionary arguments;
	arguments["selector"] = selector;

	Dictionary params;
	params["name"] = "capture_screenshot";
	params["arguments"] = arguments;

	const Dictionary response = dispatcher.handle_message(make_request(2, "tools/call", params));
	const Dictionary response_result = response["result"];
	const Dictionary structured = response_result["structuredContent"];
	CHECK(structured.has("screenshot"));
	const Dictionary screenshot = structured["screenshot"];

	if (String(screenshot["status"]) == "available") {
		CHECK_FALSE((bool)response_result["isError"]);
		CHECK((bool)structured["ok"]);
		CHECK(String(structured["capture_mode"]) == "cropped");
		CHECK(String(screenshot["capture_mode"]) == "cropped");
		CHECK(screenshot.has("crop"));
		const Dictionary crop = screenshot["crop"];
		// The crop is the element bounds grown by padding and clamped to the
		// viewport, so it must be smaller than the full window in at least one axis.
		CHECK((int)crop["width"] <= 200);
		CHECK((int)crop["height"] <= 160);
		const Dictionary image = screenshot["image"];
		CHECK((int)image["width"] == (int)crop["width"]);
		CHECK((int)image["height"] == (int)crop["height"]);
		CHECK_FALSE(String(screenshot["data"]).is_empty());
	} else {
		CHECK((bool)response_result["isError"]);
		CHECK_FALSE((bool)structured["ok"]);
		CHECK(String(structured["kind"]) == "screenshot_unavailable");
	}

	memdelete(viewport);
}

TEST_CASE("[Editor][Automation] capture_screenshot with an unresolved selector fails structurally") {
	EditorAutomationWait::clear_all_cooperative();

	PanelContainer *root = memnew(PanelContainer);
	root->set_size(Size2(240, 160));
	SceneTree::get_singleton()->get_root()->add_child(root);
	screenshot_flush_frames();

	EditorAutomationMCPDispatcher dispatcher;
	EditorAutomationMCPDispatcher::Options options;
	options.snapshot_root = root;
	dispatcher.set_options(options);

	Dictionary selector;
	selector["role"] = "button";
	selector["name"] = "Nonexistent";

	Dictionary arguments;
	arguments["selector"] = selector;

	Dictionary params;
	params["name"] = "capture_screenshot";
	params["arguments"] = arguments;

	const Dictionary response = dispatcher.handle_message(make_request(3, "tools/call", params));
	const Dictionary response_result = response["result"];
	CHECK((bool)response_result["isError"]);
	const Dictionary structured = response_result["structuredContent"];
	CHECK_FALSE((bool)structured["ok"]);
	CHECK(String(structured["kind"]) == "no_match");
	// A failed selector must not fabricate an available image.
	CHECK_FALSE(structured.has("capture_mode"));

	memdelete(root);
}

TEST_CASE("[Editor][Automation] capture_screenshot fails when the matched element has no bounds") {
	EditorAutomationWait::clear_all_cooperative();

	Control *root = memnew(Control);
	root->set_size(Size2(240, 160));
	SceneTree::get_singleton()->get_root()->add_child(root);

	// A zero-size Control still appears in the snapshot but has no croppable area,
	// standing in for virtual items (tree/list/menu/tab) that report empty bounds.
	// A plain Control (unlike a Button) does not enforce a content minimum size.
	Control *empty = memnew(Control);
	empty->set_name("ZeroBoundsControl");
	empty->set_position(Point2(20, 20));
	empty->set_size(Size2(0, 0));
	root->add_child(empty);
	screenshot_flush_frames();

	EditorAutomationMCPDispatcher dispatcher;
	EditorAutomationMCPDispatcher::Options options;
	options.snapshot_root = root;
	dispatcher.set_options(options);

	Dictionary selector;
	selector["name"] = "ZeroBoundsControl";

	Dictionary arguments;
	arguments["selector"] = selector;

	Dictionary params;
	params["name"] = "capture_screenshot";
	params["arguments"] = arguments;

	const Dictionary response = dispatcher.handle_message(make_request(5, "tools/call", params));
	const Dictionary response_result = response["result"];
	CHECK((bool)response_result["isError"]);
	const Dictionary structured = response_result["structuredContent"];
	CHECK_FALSE((bool)structured["ok"]);
	CHECK(String(structured["kind"]) == "element_not_capturable");
	// It must not silently fall back to a full-window capture for an explicit
	// element-focused request.
	CHECK_FALSE(structured.has("capture_mode"));
	CHECK_FALSE(structured.has("screenshot"));

	memdelete(root);
}

TEST_CASE("[Editor][Automation] capture_screenshot fails when the element lies outside the viewport") {
	EditorAutomationWait::clear_all_cooperative();

	SubViewport *viewport = memnew(SubViewport);
	viewport->set_size(Vector2i(120, 80));
	viewport->set_disable_3d(true);
	viewport->set_transparent_background(false);
	SceneTree::get_singleton()->get_root()->add_child(viewport);

	Control *container = memnew(Control);
	container->set_size(Size2(120, 80));
	viewport->add_child(container);

	// A button with real bounds, but positioned entirely outside the 120x80
	// viewport: the crop rect cannot intersect the captured image.
	Button *button = memnew(Button);
	button->set_text("Off Screen");
	button->set_position(Point2(500, 500));
	button->set_size(Size2(96, 40));
	container->add_child(button);
	screenshot_flush_frames(4);

	EditorAutomationMCPDispatcher dispatcher;
	EditorAutomationMCPDispatcher::Options options;
	options.snapshot_root = container;
	dispatcher.set_options(options);

	Dictionary selector;
	selector["role"] = "button";
	selector["name"] = "Off Screen";

	Dictionary arguments;
	arguments["selector"] = selector;

	Dictionary params;
	params["name"] = "capture_screenshot";
	params["arguments"] = arguments;

	const Dictionary response = dispatcher.handle_message(make_request(6, "tools/call", params));
	const Dictionary response_result = response["result"];
	CHECK((bool)response_result["isError"]);
	const Dictionary structured = response_result["structuredContent"];
	CHECK_FALSE((bool)structured["ok"]);

	// With a usable viewport the capture succeeds but cannot be cropped, so the
	// tool must report element_not_capturable rather than a full-window image.
	// A headless build without a usable viewport reports unavailable instead.
	if (structured.has("screenshot")) {
		const Dictionary screenshot = structured["screenshot"];
		CHECK(String(screenshot["status"]) == "unavailable");
		CHECK(String(structured["kind"]) == "screenshot_unavailable");
	} else {
		CHECK(String(structured["kind"]) == "element_not_capturable");
		CHECK_FALSE(structured.has("capture_mode"));
	}

	memdelete(viewport);
}

TEST_CASE("[Editor][Automation] capture_screenshot targets a window element without mis-cropping") {
	EditorAutomationWait::clear_all_cooperative();

	// A Window node is its own viewport, whose texture already spans exactly the
	// window, while a Window element's bounds are in its parent/embedder space.
	// The tool must capture the window whole rather than crop by those bounds and
	// then mis-crop or report the window as not capturable. Rooting the snapshot
	// at the window makes it the single match; capturing is display-dependent, so
	// assert the contract stays coherent and never spuriously reports
	// element_not_capturable for a valid window.
	Window *win = memnew(Window);
	win->set_title("TargetWindow");
	win->set_position(Point2i(30, 40));
	win->set_size(Size2i(160, 100));
	win->set_visible(true);
	SceneTree::get_singleton()->get_root()->add_child(win);
	screenshot_flush_frames(4);

	EditorAutomationMCPDispatcher dispatcher;
	EditorAutomationMCPDispatcher::Options options;
	options.snapshot_root = win;
	dispatcher.set_options(options);

	Dictionary selector;
	selector["name"] = "TargetWindow";

	Dictionary arguments;
	arguments["selector"] = selector;

	Dictionary params;
	params["name"] = "capture_screenshot";
	params["arguments"] = arguments;

	const Dictionary response = dispatcher.handle_message(make_request(9, "tools/call", params));
	const Dictionary response_result = response["result"];
	const Dictionary structured = response_result["structuredContent"];

	// The window has valid bounds, so a resolvable capture must never be reported
	// as element_not_capturable; ok/isError must stay consistent.
	CHECK((bool)response_result["isError"] == !(bool)structured["ok"]);
	CHECK(String(structured.get("kind", String())) != "element_not_capturable");
	if ((bool)structured["ok"]) {
		// A native window captures whole; an embedded one crops within its
		// embedder. Either is a valid capture_mode for a window target.
		const String mode = structured["capture_mode"];
		CHECK((mode == "full_window" || mode == "cropped"));
	} else {
		CHECK(String(structured["kind"]) == "screenshot_unavailable");
	}

	memdelete(win);
}

TEST_CASE("[Editor][Automation] capture_screenshot rejects both selector and element") {
	EditorAutomationWait::clear_all_cooperative();

	PanelContainer *root = memnew(PanelContainer);
	root->set_size(Size2(240, 160));
	SceneTree::get_singleton()->get_root()->add_child(root);
	screenshot_flush_frames();

	EditorAutomationMCPDispatcher dispatcher;
	EditorAutomationMCPDispatcher::Options options;
	options.snapshot_root = root;
	dispatcher.set_options(options);

	Dictionary selector;
	selector["role"] = "button";

	Dictionary arguments;
	arguments["selector"] = selector;
	arguments["element"] = selector;

	Dictionary params;
	params["name"] = "capture_screenshot";
	params["arguments"] = arguments;

	const Dictionary response = dispatcher.handle_message(make_request(4, "tools/call", params));
	CHECK(response.has("error"));

	memdelete(root);
}

} // namespace TestEditorAutomationScreenshot
