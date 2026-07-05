/**************************************************************************/
/*  foundry_test_progress.cpp                                             */
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

#include "foundry_test_progress.h"

#include "core/io/file_access.h"
#include "core/io/json.h"
#include "main/cli_parser.h"

#include "tests/test_macros.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>

namespace FoundryTestProgress {

static Config g_config;
static bool g_counting_pass = false;
static unsigned g_filtered_test_count = 0;

void configure_from_invocation(const FoundryCLIParser::CLIInvocation &p_invocation) {
	g_config = Config();
	g_filtered_test_count = 0;

	const bool has_progress = p_invocation.test_progress;
	const bool has_format = !p_invocation.test_progress_format.is_empty();
	const bool has_file = !p_invocation.test_progress_file.is_empty();

	g_config.enabled = has_progress || has_format || has_file;
	if (!g_config.enabled) {
		return;
	}

	if (has_format) {
		if (p_invocation.test_progress_format == "jsonl") {
			g_config.format = Format::JSONL;
		} else {
			g_config.format = Format::TEXT;
		}
	} else if (has_file) {
		g_config.format = Format::JSONL;
	} else {
		g_config.format = Format::TEXT;
	}

	g_config.stdout_enabled = has_progress || has_format;
	g_config.file_enabled = has_file;
	g_config.file_path = p_invocation.test_progress_file;

	int heartbeat = p_invocation.test_progress_heartbeat_seconds;
	if (heartbeat < 0) {
		heartbeat = 30;
	}
	g_config.heartbeat_seconds = heartbeat;
}

void set_doctest_quiet(bool p_quiet) {
	g_config.doctest_quiet = p_quiet;
}

bool is_enabled() {
	return g_config.enabled;
}

const Config &get_config() {
	return g_config;
}

void begin_counting_pass() {
	g_counting_pass = true;
}

void end_counting_pass() {
	g_counting_pass = false;
}

bool is_counting_pass() {
	return g_counting_pass;
}

String format_event_text(const String &p_kind, int p_index, int p_test_count, const String &p_name,
		const String &p_status, int64_t p_elapsed_ms) {
	String kind_padded = p_kind;
	while (kind_padded.length() < 9) {
		kind_padded += " ";
	}
	String line = "[foundry-test] " + kind_padded;

	if (p_index > 0) {
		if (p_test_count > 0) {
			line += vformat("%d/%d ", p_index, p_test_count);
		} else {
			line += vformat("%d/? ", p_index);
		}
	}

	if (p_elapsed_ms >= 0) {
		line += vformat("%dms ", (int)p_elapsed_ms);
	}

	if (!p_status.is_empty()) {
		line += p_status + " ";
	}

	line += p_name;
	return line;
}

String format_event_jsonl(const Dictionary &p_event) {
	return JSON::stringify(p_event);
}

String status_from_failure_flags(int p_failure_flags, bool p_test_case_success) {
	using namespace doctest::TestCaseFailureReason;
	if (p_failure_flags & (Crash | Exception)) {
		return "crashed";
	}
	if (!p_test_case_success || (p_failure_flags & AssertFailure)) {
		return "failed";
	}
	return "passed";
}

struct FoundryTestProgressListener : public doctest::IReporter {
	explicit FoundryTestProgressListener(const doctest::ContextOptions &) {}

	std::mutex state_mutex;
	std::mutex heartbeat_mutex;
	std::condition_variable heartbeat_cv;
	std::thread heartbeat_thread;
	std::atomic<bool> heartbeat_stop{ false };
	std::atomic<bool> heartbeat_active{ false };

	int current_index = 0;
	int test_count = 0;
	String current_name;
	String current_suite;
	String current_file;
	unsigned current_line = 0;
	std::chrono::steady_clock::time_point current_start;

	Ref<FileAccess> progress_file;

	~FoundryTestProgressListener() override {
		stop_heartbeat_thread();
	}

	bool should_emit() const {
		return FoundryTestProgress::is_enabled() && !FoundryTestProgress::is_counting_pass();
	}

	void open_progress_file_if_needed() {
		if (!g_config.file_enabled || progress_file.is_valid()) {
			return;
		}
		progress_file = FileAccess::open(g_config.file_path, FileAccess::WRITE);
	}

	void write_event(const String &p_text, const Dictionary &p_json) {
		if (g_config.stdout_enabled && !g_config.doctest_quiet) {
			if (g_config.format == Format::JSONL) {
				print_line("FOUNDRY_TEST_EVENT " + format_event_jsonl(p_json));
			} else {
				print_line(p_text);
			}
		}

		if (g_config.file_enabled) {
			open_progress_file_if_needed();
			if (progress_file.is_valid()) {
				progress_file->store_line(format_event_jsonl(p_json));
				progress_file->flush();
			}
		}
	}

	Dictionary make_base_event(const String &p_event) const {
		Dictionary event;
		event["version"] = 1;
		event["event"] = p_event;
		return event;
	}

	void populate_test_identity(Dictionary &r_event) const {
		if (current_index > 0) {
			r_event["index"] = current_index;
		}
		if (!current_name.is_empty()) {
			r_event["name"] = current_name;
		}
		if (!current_suite.is_empty()) {
			r_event["suite"] = current_suite;
		}
		if (!current_file.is_empty()) {
			r_event["file"] = current_file;
		}
		if (current_line > 0) {
			r_event["line"] = (int)current_line;
		}
	}

	void emit_run_start() {
		Dictionary event = make_base_event("run_start");
		if (test_count > 0) {
			event["test_count"] = test_count;
		}
		write_event(String(), event);
	}

	void emit_test_start() {
		Dictionary event = make_base_event("test_start");
		populate_test_identity(event);
		write_event(format_event_text("START", current_index, test_count, current_name), event);
	}

	void emit_test_heartbeat(int p_index, int p_test_count, const String &p_name, const String &p_suite,
			const String &p_file, unsigned p_line, int64_t p_elapsed_ms) {
		Dictionary event = make_base_event("test_heartbeat");
		if (p_index > 0) {
			event["index"] = p_index;
		}
		if (!p_name.is_empty()) {
			event["name"] = p_name;
		}
		if (!p_suite.is_empty()) {
			event["suite"] = p_suite;
		}
		if (!p_file.is_empty()) {
			event["file"] = p_file;
		}
		if (p_line > 0) {
			event["line"] = (int)p_line;
		}
		event["elapsed_ms"] = (int)p_elapsed_ms;
		write_event(format_event_text("HEARTBEAT", p_index, p_test_count, p_name, String(), p_elapsed_ms), event);
	}

	void emit_test_end(const String &p_status, int64_t p_duration_ms) {
		Dictionary event = make_base_event("test_end");
		populate_test_identity(event);
		event["status"] = p_status;
		event["duration_ms"] = (int)p_duration_ms;
		write_event(format_event_text("END", current_index, test_count, current_name, p_status, p_duration_ms), event);
	}

	void emit_run_end(const doctest::TestRunStats &p_stats, const String &p_status) {
		Dictionary event = make_base_event("run_end");
		event["status"] = p_status;
		event["passed"] = (int)(p_stats.numTestCasesPassingFilters - p_stats.numTestCasesFailed);
		event["failed"] = (int)p_stats.numTestCasesFailed;
		event["skipped"] = (int)(p_stats.numTestCases - p_stats.numTestCasesPassingFilters);
		event["assertions"] = p_stats.numAsserts;
		write_event(String(), event);
	}

	void stop_heartbeat_thread() {
		{
			std::lock_guard<std::mutex> lock(heartbeat_mutex);
			if (!heartbeat_active.load()) {
				return;
			}
			heartbeat_stop.store(true);
		}
		heartbeat_cv.notify_all();
		if (heartbeat_thread.joinable()) {
			heartbeat_thread.join();
		}
		heartbeat_stop.store(false);
		heartbeat_active.store(false);
	}

	void start_heartbeat_thread() {
		if (g_config.heartbeat_seconds <= 0) {
			return;
		}

		stop_heartbeat_thread();

		const int interval_seconds = g_config.heartbeat_seconds;
		const int snapshot_index = current_index;
		const int snapshot_count = test_count;
		const String snapshot_name = current_name;
		const String snapshot_suite = current_suite;
		const String snapshot_file = current_file;
		const unsigned snapshot_line = current_line;
		const auto snapshot_start = current_start;

		{
			std::lock_guard<std::mutex> lock(heartbeat_mutex);
			heartbeat_stop.store(false);
			heartbeat_active.store(true);
		}

		heartbeat_thread = std::thread([this, interval_seconds, snapshot_index, snapshot_count, snapshot_name,
											   snapshot_suite, snapshot_file, snapshot_line, snapshot_start]() {
			std::unique_lock<std::mutex> lock(heartbeat_mutex);
			while (!heartbeat_stop.load()) {
				if (heartbeat_cv.wait_for(lock, std::chrono::seconds(interval_seconds),
							[this]() { return heartbeat_stop.load(); })) {
					break;
				}

				const auto now = std::chrono::steady_clock::now();
				const int64_t elapsed_ms =
						std::chrono::duration_cast<std::chrono::milliseconds>(now - snapshot_start).count();
				lock.unlock();
				emit_test_heartbeat(snapshot_index, snapshot_count, snapshot_name, snapshot_suite, snapshot_file,
						snapshot_line, elapsed_ms);
				lock.lock();
			}
		});
	}

	void test_run_start() override {
		if (!should_emit()) {
			return;
		}

		std::lock_guard<std::mutex> lock(state_mutex);
		current_index = 0;
		if (g_filtered_test_count > 0) {
			test_count = (int)g_filtered_test_count;
		}
		emit_run_start();
	}

	void test_run_end(const doctest::TestRunStats &p_stats) override {
		stop_heartbeat_thread();

		if (FoundryTestProgress::is_counting_pass()) {
			g_filtered_test_count = p_stats.numTestCasesPassingFilters;
			return;
		}

		if (!should_emit()) {
			return;
		}

		const String status = p_stats.numTestCasesFailed > 0 ? "failed" : "passed";
		emit_run_end(p_stats, status);
		progress_file.unref();
	}

	void test_case_start(const doctest::TestCaseData &p_in) override {
		if (!should_emit()) {
			return;
		}

		stop_heartbeat_thread();

		std::lock_guard<std::mutex> lock(state_mutex);
		current_index++;
		current_name = String(p_in.m_name);
		current_suite = String(p_in.m_test_suite);
		current_file = String(p_in.m_file.c_str());
		current_line = p_in.m_line;
		current_start = std::chrono::steady_clock::now();

		emit_test_start();
		start_heartbeat_thread();
	}

	void test_case_end(const doctest::CurrentTestCaseStats &p_stats) override {
		if (!should_emit()) {
			return;
		}

		stop_heartbeat_thread();

		const String status = status_from_failure_flags(p_stats.failure_flags, p_stats.testCaseSuccess);
		const int64_t duration_ms = (int64_t)(p_stats.seconds * 1000.0);
		emit_test_end(status, duration_ms);
	}

	void report_query(const doctest::QueryData &p_in) override {
		if (FoundryTestProgress::is_counting_pass() && p_in.run_stats != nullptr) {
			g_filtered_test_count = p_in.run_stats->numTestCasesPassingFilters;
		}
	}
	void test_case_reenter(const doctest::TestCaseData &) override {}
	void test_case_exception(const doctest::TestCaseException &) override {}
	void subcase_start(const doctest::SubcaseSignature &) override {}
	void subcase_end() override {}
	void log_assert(const doctest::AssertData &) override {}
	void log_message(const doctest::MessageData &) override {}
	void test_case_skipped(const doctest::TestCaseData &) override {}
};

} // namespace FoundryTestProgress

REGISTER_LISTENER("FoundryTestProgressListener", 0, FoundryTestProgress::FoundryTestProgressListener);
