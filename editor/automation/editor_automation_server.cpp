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

#include "core/crypto/crypto_core.h"
#include "core/os/os.h"
#include "editor/editor_log.h"
#include "editor/editor_node.h"

EditorAutomationServer *EditorAutomationServer::singleton = nullptr;
bool EditorAutomationServer::cli_enabled = false;
String EditorAutomationServer::cli_transport;
int EditorAutomationServer::cli_port = -1;
String EditorAutomationServer::cli_token;

void EditorAutomationServer::apply_cli_options(const FoundryCLIParser::CLIInvocation &p_invocation) {
	cli_enabled = p_invocation.automation;
	cli_transport = p_invocation.automation_transport;
	cli_port = p_invocation.automation_port;
	cli_token = p_invocation.automation_token;
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

	String token_hex;
	token_hex.resize(k_token_bytes * 2);
	char *token_chars = token_hex.ptrw();
	static const char hex_digits[] = "0123456789abcdef";
	for (int i = 0; i < k_token_bytes; i++) {
		token_chars[i * 2] = hex_digits[(bytes[i] >> 4) & 0x0f];
		token_chars[i * 2 + 1] = hex_digits[bytes[i] & 0x0f];
	}
	return token_hex;
}

void EditorAutomationServer::_show_dev_indicator() const {
	if (!enabled) {
		return;
	}
	EditorNode::get_log()->add_message("--- Automation Active ---", EditorLog::MSG_TYPE_EDITOR);
}

void EditorAutomationServer::start() {
	ERR_FAIL_COND(!enabled);
	ERR_FAIL_COND(started);

	if (token.is_empty()) {
		token = _generate_token();
	}

	const String transport_name = get_transport_name();
	const String message = "Editor automation enabled (transport=" + transport_name + ")";
	OS::get_singleton()->print_line(message);
	EditorNode::get_log()->add_message(message, EditorLog::MSG_TYPE_EDITOR);
	_show_dev_indicator();
	started = true;
}

void EditorAutomationServer::stop() {
	if (!started) {
		return;
	}
	started = false;
	EditorNode::get_log()->add_message("--- Editor automation stopped ---", EditorLog::MSG_TYPE_EDITOR);
}

void EditorAutomationServer::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_EXIT_TREE: {
			stop();
		} break;

		case NOTIFICATION_INTERNAL_PROCESS: {
			if (!enabled || start_attempted) {
				break;
			}
			EditorNode *editor_node = EditorNode::get_singleton();
			if (editor_node == nullptr || !editor_node->is_editor_ready()) {
				break;
			}
			start_attempted = true;
			start();
		} break;
	}
}
