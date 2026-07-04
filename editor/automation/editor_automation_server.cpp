/**************************************************************************/
/*  editor_automation_server.cpp                                          */
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

#include "editor_automation_server.h"

#include "editor/automation/editor_automation_acceptance_workflow.h"
#include "editor/automation/editor_automation_events.h"
#include "editor/automation/editor_automation_indicator.h"
#include "editor/automation/editor_automation_mcp_server.h"
#include "editor/automation/editor_automation_wait.h"
#include "editor/automation/editor_workflow_test_driver.h"

#include "core/crypto/crypto_core.h"
#include "core/io/json.h"
#include "core/object/message_queue.h"
#include "core/os/os.h"
#include "editor/editor_log.h"
#include "editor/editor_node.h"
#include "editor/gui/editor_title_bar.h"
#include "editor/run/editor_run_bar.h"
#include "scene/main/scene_tree.h"

EditorAutomationServer *EditorAutomationServer::singleton = nullptr;
bool EditorAutomationServer::cli_enabled = false;
String EditorAutomationServer::cli_transport;
int EditorAutomationServer::cli_port = -1;
String EditorAutomationServer::cli_token;
String EditorAutomationServer::cli_run_workflow;
bool EditorAutomationServer::cli_failure_screenshots = false;

void EditorAutomationServer::apply_cli_options(const FoundryCLIParser::CLIInvocation &p_invocation) {
	cli_enabled = p_invocation.automation;
	cli_transport = p_invocation.automation_transport;
	cli_port = p_invocation.automation_port;
	cli_token = p_invocation.automation_token;
	cli_run_workflow = p_invocation.automation_run_workflow;
	cli_failure_screenshots = p_invocation.automation_failure_screenshots;
}

EditorAutomationServer *EditorAutomationServer::get_singleton() {
	return singleton;
}

EditorAutomationServer::EditorAutomationServer() {
	singleton = this;
	enabled = cli_enabled;
	if (!enabled) {
		return;
	}

	transport = Transport::MCP;
	port = cli_port >= 0 ? cli_port : 0;
	token = cli_token;
	set_process_internal(true);
}

EditorAutomationServer::~EditorAutomationServer() {
	stop();
#if defined(DEV_ENABLED)
	if (indicator != nullptr) {
		if (indicator->get_parent() != nullptr) {
			indicator->get_parent()->remove_child(indicator);
		}
		memdelete(indicator);
		indicator = nullptr;
	}
#endif
	if (mcp_server != nullptr) {
		memdelete(mcp_server);
		mcp_server = nullptr;
	}
	if (singleton == this) {
		singleton = nullptr;
	}
}

String EditorAutomationServer::get_transport_name() const {
	switch (transport) {
		case Transport::MCP:
			return "mcp";
		case Transport::NONE:
			return String();
	}
	return String();
}

String EditorAutomationServer::_generate_token() const {
	constexpr int k_token_bytes = 32;
	uint8_t bytes[k_token_bytes];
	CryptoCore::RandomGenerator rng;
	ERR_FAIL_COND_V(rng.init() != OK, String());
	ERR_FAIL_COND_V(rng.get_random_bytes(bytes, k_token_bytes) != OK, String());
	return String::hex_encode_buffer(bytes, k_token_bytes);
}

void EditorAutomationServer::_ensure_dev_indicator() {
#if defined(DEV_ENABLED)
	if (!enabled || indicator != nullptr) {
		return;
	}

	EditorNode *editor_node = EditorNode::get_singleton();
	if (editor_node == nullptr) {
		return;
	}

	EditorTitleBar *title_bar = EditorNode::get_title_bar();
	if (title_bar == nullptr) {
		return;
	}

	indicator = memnew(EditorAutomationIndicator);
	indicator->set_mouse_filter(Control::MOUSE_FILTER_IGNORE);
	title_bar->add_child(indicator);

	// Keep the indicator adjacent to the run bar without displacing centered controls.
	EditorRunBar *run_bar = EditorRunBar::get_singleton();
	if (run_bar != nullptr) {
		title_bar->move_child(indicator, run_bar->get_index());
	}
#endif
}

void EditorAutomationServer::_apply_dev_indicator() {
#if defined(DEV_ENABLED)
	_ensure_dev_indicator();
	if (indicator != nullptr) {
		indicator->set_active(get_transport_name(), port, endpoint);
	}
#endif
}

void EditorAutomationServer::_show_dev_indicator() {
	EditorNode *editor_node = EditorNode::get_singleton();
	if (editor_node != nullptr && editor_node->get_log() != nullptr) {
		editor_node->get_log()->add_message("--- Automation Active ---", EditorLog::MSG_TYPE_EDITOR);
	}
#if defined(DEV_ENABLED)
	// Defer UI mutation until after the current editor frame finishes laying out.
	callable_mp(this, &EditorAutomationServer::_apply_dev_indicator).call_deferred();
#endif
}

void EditorAutomationServer::_hide_dev_indicator() {
#if defined(DEV_ENABLED)
	if (indicator != nullptr) {
		indicator->clear_active();
	}
#endif
}

#if defined(DEV_ENABLED)
bool EditorAutomationServer::is_indicator_visible() const {
	return indicator != nullptr && indicator->is_active();
}
#endif

bool EditorAutomationServer::_start_mcp_transport() {
	if (mcp_server == nullptr) {
		mcp_server = memnew(EditorAutomationMCPServer);
	}
	mcp_server->set_token(token);

	EditorAutomationMCPDispatcher::Options options;
	options.snapshot_root = nullptr; // Capture from the live editor.
	options.attach_screenshot_on_failure = cli_failure_screenshots;
	mcp_server->set_dispatcher_options(options);

	const int requested_port = port < 0 ? 0 : port;
	const bool allow_port_reuse = requested_port == 0;
	const Error err = mcp_server->listen(requested_port, IPAddress("127.0.0.1"), allow_port_reuse);
	if (err != OK) {
		String failure;
		if (err == ERR_ALREADY_IN_USE) {
			failure = vformat("Editor automation MCP server failed to bind 127.0.0.1:%d because the port is already in use.", requested_port);
		} else {
			failure = vformat("Editor automation MCP server failed to listen on 127.0.0.1:%d (error %d).", requested_port, (int)err);
		}
		_fail_startup(failure, requested_port, err);
		return false;
	}

	port = mcp_server->get_port();
	endpoint = vformat("http://127.0.0.1:%d/mcp", port);
	return true;
}

void EditorAutomationServer::_fail_startup(const String &p_message, int p_requested_port, Error p_error) {
	if (mcp_server != nullptr) {
		mcp_server->stop();
		memdelete(mcp_server);
		mcp_server = nullptr;
	}

	OS::get_singleton()->printerr("%s\n", p_message.utf8().get_data());
	OS::get_singleton()->print("%s\n", p_message.utf8().get_data());

	Dictionary machine_line;
	machine_line["error"] = "automation_startup_failed";
	machine_line["message"] = p_message;
	machine_line["transport"] = get_transport_name();
	machine_line["requested_port"] = p_requested_port;
	machine_line["error_code"] = (int)p_error;
	OS::get_singleton()->print("FOUNDRY_AUTOMATION_ERROR %s\n", JSON::stringify(machine_line, "", false).utf8().get_data());

	EditorNode *editor_node = EditorNode::get_singleton();
	if (editor_node != nullptr && editor_node->get_log() != nullptr) {
		editor_node->get_log()->add_message(p_message, EditorLog::MSG_TYPE_ERROR);
	}

	_hide_dev_indicator();
	started = false;

	if (SceneTree *tree = get_tree()) {
		tree->quit(EXIT_FAILURE);
	}
}

void EditorAutomationServer::start() {
	ERR_FAIL_COND(!enabled);
	ERR_FAIL_COND(started);

	if (token.is_empty()) {
		token = _generate_token();
	}

	if (transport == Transport::MCP) {
		if (!_start_mcp_transport()) {
			return;
		}
	}

	const String transport_name = get_transport_name();
	const String message = vformat("Editor automation enabled (transport=%s, endpoint=%s)", transport_name, endpoint);
	OS::get_singleton()->print("%s\n", message.utf8().get_data());

	// Machine-readable line for launching test harnesses / agent hosts.
	Dictionary machine_line;
	machine_line["transport"] = transport_name;
	machine_line["endpoint"] = endpoint;
	machine_line["port"] = port;
	machine_line["token"] = token;
	machine_line["local_only"] = local_only;
	OS::get_singleton()->print("FOUNDRY_AUTOMATION %s\n", JSON::stringify(machine_line, "", false).utf8().get_data());

	EditorNode *editor_node = EditorNode::get_singleton();
	if (editor_node != nullptr && editor_node->get_log() != nullptr) {
		editor_node->get_log()->add_message(message, EditorLog::MSG_TYPE_EDITOR);
	}
	_show_dev_indicator();
	started = true;
}

void EditorAutomationServer::stop() {
	if (!started && mcp_server == nullptr) {
		_hide_dev_indicator();
		return;
	}
	started = false;
	if (mcp_server != nullptr) {
		mcp_server->stop();
	}
	_hide_dev_indicator();
	EditorNode *editor_node = EditorNode::get_singleton();
	if (editor_node != nullptr && editor_node->get_log() != nullptr) {
		editor_node->get_log()->add_message("--- Editor automation stopped ---", EditorLog::MSG_TYPE_EDITOR);
	}
}

void EditorAutomationServer::_run_acceptance_workflow_if_requested() {
	if (cli_run_workflow.is_empty() || workflow_run_attempted) {
		return;
	}
	workflow_run_attempted = true;

	EditorWorkflowTestDriver driver;
	EditorWorkflowTestDriver::Options options;
	options.default_wait_timeout_ms = 60000;
	options.attach_screenshot_on_failure = cli_failure_screenshots;
	driver.configure(options);

	EditorAutomationAcceptanceWorkflow::Result workflow_result;
	if (cli_run_workflow == "mvp") {
		workflow_result = EditorAutomationAcceptanceWorkflow::run_mvp(driver);
	} else {
		workflow_result.ok = false;
		workflow_result.workflow = cli_run_workflow;
		workflow_result.message = vformat("Unknown automation workflow '%s'.", cli_run_workflow);
	}

	EditorAutomationAcceptanceWorkflow::print_result(workflow_result);
	workflow_run_completed = true;
	stop();

	if (SceneTree *tree = get_tree()) {
		workflow_quit_exit_code = workflow_result.ok ? EXIT_SUCCESS : EXIT_FAILURE;
		// Let deferred editor startup reparents settle before tearing down.
		workflow_quit_frames_remaining = 10;
	}
}

void EditorAutomationServer::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_EXIT_TREE: {
			stop();
		} break;

		case NOTIFICATION_INTERNAL_PROCESS: {
			if (!enabled) {
				break;
			}
			if (workflow_quit_frames_remaining > 0) {
				workflow_quit_frames_remaining--;
				if (workflow_quit_frames_remaining == 0) {
					if (EditorNode *editor_node = EditorNode::get_singleton()) {
						callable_mp(editor_node, &EditorNode::quit_for_automation_workflow).call_deferred(workflow_quit_exit_code);
					} else if (SceneTree *tree = get_tree()) {
						tree->quit(workflow_quit_exit_code);
					}
				}
				break;
			}
			if (!start_attempted) {
				EditorNode *editor_node = EditorNode::get_singleton();
				if (editor_node == nullptr || !editor_node->is_editor_ready()) {
					break;
				}
				start_attempted = true;
				start();
				if (!cli_run_workflow.is_empty()) {
					_run_acceptance_workflow_if_requested();
					break;
				}
			}
			if (started && mcp_server != nullptr) {
				EditorAutomationEvents::poll_sources();
				EditorAutomationWait::poll_all_cooperative();
				mcp_server->poll();
			}
		} break;
	}
}
