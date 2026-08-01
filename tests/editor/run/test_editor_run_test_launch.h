/**************************************************************************/
/*  test_editor_run_test_launch.h                                         */
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

#include "editor/debugger/debug_adapter/debug_adapter_parser.h"
#include "editor/run/editor_run.h"
#include "main/cli_parser.h"

#include "tests/test_macros.h"

namespace TestEditorRunTestLaunch {

static PackedStringArray to_array(const List<String> &p_arguments) {
	PackedStringArray arguments;
	for (const String &argument : p_arguments) {
		arguments.push_back(argument);
	}
	return arguments;
}

static int index_of(const PackedStringArray &p_arguments, const String &p_value) {
	for (int i = 0; i < p_arguments.size(); i++) {
		if (p_arguments[i] == p_value) {
			return i;
		}
	}
	return -1;
}

static EditorRun::LaunchContext make_context() {
	EditorRun::LaunchContext context;
	context.resource_path = "/projects/demo";
	context.debug_uri = "tcp://127.0.0.1:6007";
	context.editor_pid = 4242;
	return context;
}

static EditorRun::TestLaunch make_launch() {
	EditorRun::TestLaunch launch;
	launch.runner = "res://addons/example/run.fs";
	launch.adapter_protocol_version = EditorRun::TEST_ADAPTER_PROTOCOL_VERSION;
	launch.report_path = "/scratch/report.tap";
	launch.test_ids.push_back("suite::alpha");
	launch.test_ids.push_back("suite::beta");
	return launch;
}

TEST_CASE("[Editor][EditorRun] A project_test launch builds a project test command") {
	const PackedStringArray arguments = to_array(EditorRun::build_project_test_arguments(make_context(), make_launch()));

	REQUIRE(arguments.size() >= 2);
	CHECK_EQ(arguments[0], "project");
	CHECK_EQ(arguments[1], "test");

	const int separator = index_of(arguments, "--");
	REQUIRE(separator > 0);

	// Engine-owned options stay ahead of the separator.
	const int project_index = index_of(arguments, "--project");
	const int runner_index = index_of(arguments, "--runner");
	const int remote_debug_index = index_of(arguments, "--remote-debug");
	const int editor_pid_index = index_of(arguments, "--editor-pid");
	REQUIRE(project_index > 0);
	REQUIRE(runner_index > 0);
	REQUIRE(remote_debug_index > 0);
	REQUIRE(editor_pid_index > 0);
	CHECK(project_index < separator);
	CHECK(runner_index < separator);
	CHECK(remote_debug_index < separator);
	CHECK(editor_pid_index < separator);
	CHECK_EQ(arguments[project_index + 1], "/projects/demo");
	CHECK_EQ(arguments[runner_index + 1], "res://addons/example/run.fs");
	CHECK_EQ(arguments[remote_debug_index + 1], "tcp://127.0.0.1:6007");
	CHECK_EQ(arguments[editor_pid_index + 1], "4242");

	// Adapter-owned options only appear behind it.
	PackedStringArray adapter_arguments;
	for (int i = separator + 1; i < arguments.size(); i++) {
		adapter_arguments.push_back(arguments[i]);
	}
	PackedStringArray expected;
	expected.push_back("adapter");
	expected.push_back("run");
	expected.push_back("--protocol-version");
	expected.push_back("1");
	expected.push_back("--report");
	expected.push_back("/scratch/report.tap");
	expected.push_back("--select");
	expected.push_back("suite::alpha");
	expected.push_back("--select");
	expected.push_back("suite::beta");
	CHECK_EQ(adapter_arguments, expected);
}

TEST_CASE("[Editor][EditorRun] A project_test launch repeats every selected id verbatim") {
	EditorRun::TestLaunch launch = make_launch();
	launch.test_ids.clear();
	launch.test_ids.push_back("suite::alpha");
	launch.test_ids.push_back("suite::alpha");

	const PackedStringArray arguments = to_array(EditorRun::build_project_test_arguments(make_context(), launch));

	int selections = 0;
	for (int i = 0; i < arguments.size(); i++) {
		if (arguments[i] == "--select") {
			REQUIRE(i + 1 < arguments.size());
			CHECK_EQ(arguments[i + 1], "suite::alpha");
			selections++;
		}
	}
	CHECK_EQ(selections, 2);
}

TEST_CASE("[Editor][EditorRun] A project_test launch without a debug host omits transport options") {
	EditorRun::LaunchContext context = make_context();
	context.debug_uri = String();
	context.editor_pid = 0;

	const PackedStringArray arguments = to_array(EditorRun::build_project_test_arguments(context, make_launch()));
	CHECK_EQ(index_of(arguments, "--remote-debug"), -1);
	CHECK_EQ(index_of(arguments, "--editor-pid"), -1);
}

TEST_CASE("[Editor][EditorRun] The built project_test command parses back into a runnable invocation") {
	PackedStringArray command;
	command.push_back("foundry");
	command.append_array(to_array(EditorRun::build_project_test_arguments(make_context(), make_launch())));

	const FoundryCLIParser::ParseResult result = FoundryCLIParser::parse(command);
	REQUIRE_MESSAGE(result.ok, result.error);
	CHECK_EQ(result.invocation.kind, FoundryCLIParser::CLIInvocation::PROJECT_TEST);
	CHECK_EQ(result.invocation.runner, "res://addons/example/run.fs");
	CHECK_EQ(result.invocation.project_path, "/projects/demo");
	CHECK(FoundryCLIParser::can_run_without_main_scene(result.invocation));

	PackedStringArray expected_passthrough;
	expected_passthrough.push_back("--remote-debug");
	expected_passthrough.push_back("tcp://127.0.0.1:6007");
	expected_passthrough.push_back("--editor-pid");
	expected_passthrough.push_back("4242");
	CHECK_EQ(result.invocation.passthrough_args, expected_passthrough);

	PackedStringArray expected_user_args;
	expected_user_args.push_back("adapter");
	expected_user_args.push_back("run");
	expected_user_args.push_back("--protocol-version");
	expected_user_args.push_back("1");
	expected_user_args.push_back("--report");
	expected_user_args.push_back("/scratch/report.tap");
	expected_user_args.push_back("--select");
	expected_user_args.push_back("suite::alpha");
	expected_user_args.push_back("--select");
	expected_user_args.push_back("suite::beta");
	CHECK_EQ(result.user_args, expected_user_args);
}

TEST_CASE("[Editor][EditorRun] A trusted editor's forwarded options keep the command recognizable") {
	EditorRun::LaunchContext context = make_context();
	// This is what `Main::get_forwardable_cli_arguments()` hands to a launched child
	// when the editor itself was started in trusted mode.
	context.forwardable_arguments.push_back("--foundry-build-trusted");
	context.forwardable_arguments.push_back("--verbose");

	PackedStringArray command;
	command.push_back("foundry");
	command.append_array(to_array(EditorRun::build_project_test_arguments(context, make_launch())));

	const FoundryCLIParser::ParseResult result = FoundryCLIParser::parse(command);
	REQUIRE_MESSAGE(result.ok, result.error);
	CHECK_EQ(result.invocation.kind, FoundryCLIParser::CLIInvocation::PROJECT_TEST);
	CHECK(result.trusted);
	CHECK_EQ(result.invocation.runner, "res://addons/example/run.fs");
}

TEST_CASE("[Editor][DebugAdapter] An absent foundry/launch object means a scene launch") {
	Dictionary arguments;
	arguments["project"] = "/projects/demo";

	DebugAdapterParser::LaunchRequest request;
	String error;
	REQUIRE(DebugAdapterParser::parse_launch_request(arguments, request, error));
	CHECK_EQ(request.kind, DebugAdapterParser::LaunchRequest::KIND_SCENE);
	CHECK(error.is_empty());
}

TEST_CASE("[Editor][DebugAdapter] A project_test launch object carries the runner selection") {
	Dictionary adapter;
	adapter["protocolVersion"] = EditorRun::TEST_ADAPTER_PROTOCOL_VERSION;
	adapter["report"] = "/scratch/report.tap";
	Array ids;
	ids.push_back("suite::alpha");
	ids.push_back("suite::beta");
	adapter["testIds"] = ids;

	Dictionary launch;
	launch["kind"] = "project_test";
	launch["runner"] = "res://addons/example/run.fs";
	launch["adapter"] = adapter;

	Dictionary arguments;
	arguments["foundry/launch"] = launch;

	DebugAdapterParser::LaunchRequest request;
	String error;
	REQUIRE_MESSAGE(DebugAdapterParser::parse_launch_request(arguments, request, error), error);
	CHECK_EQ(request.kind, DebugAdapterParser::LaunchRequest::KIND_PROJECT_TEST);
	CHECK_EQ(request.test_launch.runner, "res://addons/example/run.fs");
	CHECK_EQ(request.test_launch.report_path, "/scratch/report.tap");
	REQUIRE_EQ(request.test_launch.test_ids.size(), 2);
	CHECK_EQ(request.test_launch.test_ids[0], "suite::alpha");
	CHECK_EQ(request.test_launch.test_ids[1], "suite::beta");
}

TEST_CASE("[Editor][DebugAdapter] A malformed project_test launch is rejected with a reason") {
	SUBCASE("unsupported kind") {
		Dictionary launch;
		launch["kind"] = "scene";
		Dictionary arguments;
		arguments["foundry/launch"] = launch;

		DebugAdapterParser::LaunchRequest request;
		String error;
		CHECK_FALSE(DebugAdapterParser::parse_launch_request(arguments, request, error));
		CHECK(error.contains("unsupported launch kind"));
	}

	SUBCASE("missing runner") {
		Dictionary launch;
		launch["kind"] = "project_test";
		Dictionary arguments;
		arguments["foundry/launch"] = launch;

		DebugAdapterParser::LaunchRequest request;
		String error;
		CHECK_FALSE(DebugAdapterParser::parse_launch_request(arguments, request, error));
		CHECK(error.contains("runner"));
	}

	SUBCASE("missing adapter object") {
		Dictionary launch;
		launch["kind"] = "project_test";
		launch["runner"] = "res://run.fs";
		Dictionary arguments;
		arguments["foundry/launch"] = launch;

		DebugAdapterParser::LaunchRequest request;
		String error;
		CHECK_FALSE(DebugAdapterParser::parse_launch_request(arguments, request, error));
		CHECK(error.contains("adapter"));
	}

	SUBCASE("unsupported protocol version") {
		Dictionary adapter;
		adapter["protocolVersion"] = EditorRun::TEST_ADAPTER_PROTOCOL_VERSION + 1;
		Dictionary launch;
		launch["kind"] = "project_test";
		launch["runner"] = "res://run.fs";
		launch["adapter"] = adapter;
		Dictionary arguments;
		arguments["foundry/launch"] = launch;

		DebugAdapterParser::LaunchRequest request;
		String error;
		CHECK_FALSE(DebugAdapterParser::parse_launch_request(arguments, request, error));
		CHECK(error.contains("protocol version"));
	}

	SUBCASE("non-string runner") {
		Dictionary adapter;
		adapter["protocolVersion"] = EditorRun::TEST_ADAPTER_PROTOCOL_VERSION;
		adapter["report"] = "/scratch/report.tap";
		Dictionary launch;
		launch["kind"] = "project_test";
		launch["runner"] = 17;
		launch["adapter"] = adapter;
		Dictionary arguments;
		arguments["foundry/launch"] = launch;

		DebugAdapterParser::LaunchRequest request;
		String error;
		CHECK_FALSE(DebugAdapterParser::parse_launch_request(arguments, request, error));
		CHECK(error.contains("runner"));
	}

	SUBCASE("non-string selected id") {
		Dictionary adapter;
		adapter["protocolVersion"] = EditorRun::TEST_ADAPTER_PROTOCOL_VERSION;
		adapter["report"] = "/scratch/report.tap";
		Array ids;
		ids.push_back(3);
		adapter["testIds"] = ids;
		Dictionary launch;
		launch["kind"] = "project_test";
		launch["runner"] = "res://run.fs";
		launch["adapter"] = adapter;
		Dictionary arguments;
		arguments["foundry/launch"] = launch;

		DebugAdapterParser::LaunchRequest request;
		String error;
		CHECK_FALSE(DebugAdapterParser::parse_launch_request(arguments, request, error));
		CHECK(error.contains("array of strings"));
	}

	SUBCASE("missing report") {
		Dictionary adapter;
		adapter["protocolVersion"] = EditorRun::TEST_ADAPTER_PROTOCOL_VERSION;
		Dictionary launch;
		launch["kind"] = "project_test";
		launch["runner"] = "res://run.fs";
		launch["adapter"] = adapter;
		Dictionary arguments;
		arguments["foundry/launch"] = launch;

		DebugAdapterParser::LaunchRequest request;
		String error;
		CHECK_FALSE(DebugAdapterParser::parse_launch_request(arguments, request, error));
		CHECK(error.contains("report"));
	}

	SUBCASE("fractional protocol version") {
		Dictionary adapter;
		adapter["protocolVersion"] = 1.5;
		adapter["report"] = "/scratch/report.tap";
		Dictionary launch;
		launch["kind"] = "project_test";
		launch["runner"] = "res://run.fs";
		launch["adapter"] = adapter;
		Dictionary arguments;
		arguments["foundry/launch"] = launch;

		DebugAdapterParser::LaunchRequest request;
		String error;
		CHECK_FALSE(DebugAdapterParser::parse_launch_request(arguments, request, error));
		CHECK(error.contains("integer"));
	}

	SUBCASE("empty selected id") {
		Dictionary adapter;
		adapter["protocolVersion"] = EditorRun::TEST_ADAPTER_PROTOCOL_VERSION;
		adapter["report"] = "/scratch/report.tap";
		Array ids;
		ids.push_back("");
		adapter["testIds"] = ids;
		Dictionary launch;
		launch["kind"] = "project_test";
		launch["runner"] = "res://run.fs";
		launch["adapter"] = adapter;
		Dictionary arguments;
		arguments["foundry/launch"] = launch;

		DebugAdapterParser::LaunchRequest request;
		String error;
		CHECK_FALSE(DebugAdapterParser::parse_launch_request(arguments, request, error));
		CHECK(error.contains("empty ids"));
	}
}

TEST_CASE("[Editor][DebugAdapter] Unknown selected ids are forwarded for the runner to reject") {
	Dictionary adapter;
	adapter["protocolVersion"] = EditorRun::TEST_ADAPTER_PROTOCOL_VERSION;
	adapter["report"] = "/scratch/report.tap";
	Array ids;
	ids.push_back("suite::does_not_exist");
	adapter["testIds"] = ids;

	Dictionary launch;
	launch["kind"] = "project_test";
	launch["runner"] = "res://run.fs";
	launch["adapter"] = adapter;

	Dictionary arguments;
	arguments["foundry/launch"] = launch;

	DebugAdapterParser::LaunchRequest request;
	String error;
	REQUIRE_MESSAGE(DebugAdapterParser::parse_launch_request(arguments, request, error), error);
	REQUIRE_EQ(request.test_launch.test_ids.size(), 1);
	CHECK_EQ(request.test_launch.test_ids[0], "suite::does_not_exist");

	const PackedStringArray arguments_vector = to_array(EditorRun::build_project_test_arguments(make_context(), request.test_launch));
	const int select_index = index_of(arguments_vector, "--select");
	REQUIRE(select_index > 0);
	CHECK_EQ(arguments_vector[select_index + 1], "suite::does_not_exist");
}

} // namespace TestEditorRunTestLaunch
