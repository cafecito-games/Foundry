/**************************************************************************/
/*  test_editor_automation_server.h                                       */
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

#include "editor/automation/editor_automation_indicator.h"
#include "editor/automation/editor_automation_mcp_server.h"

#include "core/object/message_queue.h"
#include "scene/main/window.h"

#include "tests/test_macros.h"

namespace TestEditorAutomationServer {

static void server_flush_frames(int p_count = 1) {
	for (int i = 0; i < p_count; i++) {
		SceneTree::get_singleton()->process(1.0 / 60.0);
		MessageQueue::get_singleton()->flush();
	}
}

TEST_CASE("[Editor][EditorAutomation] indicator shows transport and port while active") {
	Window *window = memnew(Window);
	SceneTree::get_singleton()->get_root()->add_child(window);

	EditorAutomationIndicator *indicator = memnew(EditorAutomationIndicator);
	window->add_child(indicator);
	server_flush_frames();

	indicator->set_active("mcp", 4242, "http://127.0.0.1:4242/mcp");
	CHECK(indicator->is_active());
	CHECK(indicator->is_visible());
	CHECK(indicator->get_tooltip_text().contains("mcp"));
	CHECK(indicator->get_tooltip_text().contains("4242"));

	indicator->clear_active();
	CHECK_FALSE(indicator->is_active());
	CHECK_FALSE(indicator->is_visible());

	window->queue_free();
}

TEST_CASE("[Editor][Automation][MCP] explicit port conflict fails to bind") {
	EditorAutomationMCPServer first;
	first.set_token("token-a");
	REQUIRE(first.listen(0, IPAddress("127.0.0.1")) == OK);
	const int bound_port = first.get_port();
	REQUIRE(bound_port > 0);

	EditorAutomationMCPServer second;
	second.set_token("token-b");
	CHECK(second.listen(bound_port, IPAddress("127.0.0.1")) == ERR_ALREADY_IN_USE);

	first.stop();
}

TEST_CASE("[Editor][Automation][MCP] shutdown releases port for rapid relisten") {
	EditorAutomationMCPServer first;
	first.set_token("token-a");
	REQUIRE(first.listen(0, IPAddress("127.0.0.1")) == OK);
	const int bound_port = first.get_port();
	REQUIRE(bound_port > 0);
	first.stop();

	EditorAutomationMCPServer second;
	second.set_token("token-b");
	CHECK(second.listen(bound_port, IPAddress("127.0.0.1")) == OK);
	second.stop();
}

TEST_CASE("[Editor][Automation][MCP] port zero selects a non-zero local port") {
	EditorAutomationMCPServer server;
	server.set_token("token");
	REQUIRE(server.listen(0, IPAddress("127.0.0.1")) == OK);
	CHECK(server.get_port() > 0);
	server.stop();
}

} // namespace TestEditorAutomationServer
