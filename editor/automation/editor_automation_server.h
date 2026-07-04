/**************************************************************************/
/*  editor_automation_server.h                                            */
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

#include "editor/plugins/editor_plugin.h"
#include "main/cli_parser.h"

class EditorAutomationMCPServer;

class EditorAutomationServer : public EditorPlugin {
	FOUNDRY_CLASS(EditorAutomationServer, EditorPlugin);

public:
	enum class Transport {
		NONE,
		MCP,
	};

private:
	static EditorAutomationServer *singleton;
	static bool cli_enabled;
	static String cli_transport;
	static int cli_port;
	static String cli_token;
	static String cli_run_workflow;
	static bool cli_failure_screenshots;

	bool enabled = false;
	Transport transport = Transport::NONE;
	int port = 0;
	String token;
	String endpoint;
	bool local_only = true;
	bool started = false;
	bool start_attempted = false;
	bool workflow_run_attempted = false;
	bool workflow_run_completed = false;

	EditorAutomationMCPServer *mcp_server = nullptr;

	String _generate_token() const;
	void _show_dev_indicator() const;
	bool _start_mcp_transport();
	void _run_acceptance_workflow_if_requested();
	void _notification(int p_what);

public:
	static void apply_cli_options(const FoundryCLIParser::CLIInvocation &p_invocation);
	static EditorAutomationServer *get_singleton();

	EditorAutomationServer();
	~EditorAutomationServer();

	bool is_enabled() const { return enabled; }
	String get_transport_name() const;
	int get_port() const { return port; }
	const String &get_token() const { return token; }
	const String &get_endpoint() const { return endpoint; }
	bool is_local_only() const { return local_only; }
	bool is_started() const { return started; }

	void start();
	void stop();
};
