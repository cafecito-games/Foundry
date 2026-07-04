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

} // namespace TestEditorAutomationScreenshot
