/**************************************************************************/
/*  test_foundry_test_progress.h                                          */
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

#include "core/io/json.h"
#include "core/string/print_string.h"
#include "main/cli_parser.h"
#include "tests/foundry_test_progress.h"

#include "tests/test_macros.h"

namespace TestFoundryTestProgress {

TEST_CASE("[FoundryTestProgress] Text events include stable prefix and index") {
	const String line = FoundryTestProgress::format_event_text("START", 3, 10, "[FoundryCLIParser] case");
	CHECK(line.begins_with("[foundry-test] START"));
	CHECK(line.contains("3/10"));
	CHECK(line.contains("[FoundryCLIParser] case"));
}

TEST_CASE("[FoundryTestProgress] JSONL events include version and event name") {
	Dictionary event;
	event["version"] = 2;
	event["event"] = "test_start";
	event["index"] = 1;
	event["name"] = "example";

	const String json = FoundryTestProgress::format_event_jsonl(event);
	Variant parsed = JSON::parse_string(json);
	REQUIRE(parsed.get_type() == Variant::DICTIONARY);
	const Dictionary parsed_dict = parsed;
	CHECK_EQ(int(parsed_dict["version"]), 2);
	CHECK_EQ(parsed_dict["event"], "test_start");
	CHECK_EQ(int(parsed_dict["index"]), 1);
	CHECK_EQ(parsed_dict["name"], "example");
}

TEST_CASE("[FoundryTestProgress] CLI configuration enables stdout and file sinks independently") {
	FoundryCLIParser::CLIInvocation stdout_only;
	stdout_only.test_progress = true;
	FoundryTestProgress::configure_from_invocation(stdout_only);
	CHECK(FoundryTestProgress::is_enabled());
	CHECK(FoundryTestProgress::get_config().stdout_enabled);
	CHECK_FALSE(FoundryTestProgress::get_config().file_enabled);
	CHECK_EQ((int)FoundryTestProgress::get_config().format, (int)FoundryTestProgress::Format::TEXT);

	FoundryCLIParser::CLIInvocation file_only;
	file_only.test_progress_file = "/tmp/foundry-test-progress.jsonl";
	FoundryTestProgress::configure_from_invocation(file_only);
	CHECK(FoundryTestProgress::is_enabled());
	CHECK_FALSE(FoundryTestProgress::get_config().stdout_enabled);
	CHECK(FoundryTestProgress::get_config().file_enabled);
	CHECK_EQ((int)FoundryTestProgress::get_config().format, (int)FoundryTestProgress::Format::JSONL);
}

TEST_CASE("[FoundryTestProgress] Progress lines never reach the engine's print handlers") {
	// A test that judges output captures it with a print handler, and the heartbeat thread emits while
	// such a test is running. An event delivered to the handler list would land inside the output the
	// running test is about to judge, which is a failure invented by the runner watching the run.
	struct CapturedPrints {
		Vector<String> lines;
		static void handle(void *p_userdata, const String &p_message, bool, bool) {
			static_cast<CapturedPrints *>(p_userdata)->lines.push_back(p_message);
		}
	};

	// The synthetic event is delivered to a thread-local sink instead of the process' stdout, so this
	// test never injects a fabricated heartbeat into the progress stream of the run executing it.
	struct CapturedProgress {
		Vector<String> lines;
		static void handle(void *p_userdata, const String &p_line) {
			static_cast<CapturedProgress *>(p_userdata)->lines.push_back(p_line);
		}
	};

	struct ScopedProgressSink {
		explicit ScopedProgressSink(CapturedProgress *p_target) {
			FoundryTestProgress::set_thread_line_sink(&CapturedProgress::handle, p_target);
		}
		~ScopedProgressSink() {
			FoundryTestProgress::set_thread_line_sink(nullptr, nullptr);
		}
	};

	struct ScopedPrintHandler {
		PrintHandlerList handler;
		explicit ScopedPrintHandler(CapturedPrints *p_target) {
			handler.printfunc = &CapturedPrints::handle;
			handler.userdata = p_target;
			add_print_handler(&handler);
		}
		~ScopedPrintHandler() {
			remove_print_handler(&handler);
		}
	};

	const String heartbeat_line = FoundryTestProgress::format_event_text("HEARTBEAT", 1, 1, "example", String(), 30000);

	CapturedProgress progress;
	CapturedPrints captured;
	{
		ScopedProgressSink progress_sink(&progress);
		ScopedPrintHandler print_handler(&captured);
		FoundryTestProgress::write_stdout_line(heartbeat_line);
		print_line("output of the code under test");
	}

	REQUIRE_EQ(captured.lines.size(), 1);
	CHECK_EQ(captured.lines[0], "output of the code under test");

	REQUIRE_EQ(progress.lines.size(), 1);
	CHECK_EQ(progress.lines[0], heartbeat_line);
}

TEST_CASE("[FoundryTestProgress] Failure flags map to progress status strings") {
	using namespace doctest::TestCaseFailureReason;
	CHECK_EQ(FoundryTestProgress::status_from_failure_flags(None, true), "passed");
	CHECK_EQ(FoundryTestProgress::status_from_failure_flags(AssertFailure, false), "failed");
	CHECK_EQ(FoundryTestProgress::status_from_failure_flags(Crash, false), "crashed");
}

} // namespace TestFoundryTestProgress
