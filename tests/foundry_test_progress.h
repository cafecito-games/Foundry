/**************************************************************************/
/*  foundry_test_progress.h                                               */
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

#include "core/string/ustring.h"
#include "core/variant/dictionary.h"
#include "main/cli_parser.h"

namespace FoundryTestProgress {

enum class Format {
	TEXT,
	JSONL,
};

struct Config {
	bool enabled = false;
	Format format = Format::TEXT;
	bool stdout_enabled = false;
	bool file_enabled = false;
	String file_path;
	int heartbeat_seconds = 30;
	bool doctest_quiet = false;
};

void configure_from_invocation(const FoundryCLIParser::CLIInvocation &p_invocation);
void set_doctest_quiet(bool p_quiet);
// Records the full-suite (pre-partition) case count so the progress stream can self-report it
// on `run_start`/`run_end`. Every shard reports the same value; the aggregator cross-checks it.
void set_full_suite_case_count(int p_count);
bool is_enabled();
const Config &get_config();

void begin_counting_pass();
void end_counting_pass();
bool is_counting_pass();

String format_event_text(const String &p_kind, int p_index, int p_test_count, const String &p_name,
		const String &p_status = String(), int64_t p_elapsed_ms = -1);
String format_event_jsonl(const Dictionary &p_event);
String status_from_failure_flags(int p_failure_flags, bool p_test_case_success);

// Writes one progress line to the process' stdout without passing it to the engine's print handlers.
// The stream describes the run rather than the code under test, and a test that captures prints must
// never see it in the output it is judging.
void write_stdout_line(const String &p_line);

typedef void (*LineSink)(void *p_userdata, const String &p_line);

// Redirects `write_stdout_line()` on the calling thread only, so a test can observe the routing of a
// synthetic line without it reaching the real progress stream. The override is thread-local because
// the heartbeat thread emits real events concurrently and must keep writing to the actual stream.
// Passing `nullptr` restores stdout for the calling thread.
void set_thread_line_sink(LineSink p_sink, void *p_userdata);

} // namespace FoundryTestProgress
