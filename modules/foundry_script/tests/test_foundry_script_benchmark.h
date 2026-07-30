/**************************************************************************/
/*  test_foundry_script_benchmark.h                                       */
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

#include "../foundry_script.h"
#include "../tests/fs_benchmark_runner.h"

#include "core/math/math_funcs.h"
#include "core/variant/array.h"
#include "core/variant/dictionary.h"

#include "tests/test_macros.h"

namespace FSTests {

TEST_CASE("[FSBenchmark] Runs a case and reports a finite positive time") {
	// The runner compiles and runs `.fs` workloads, so the language runtime must
	// be initialized (mirrors the dynamic-load test in fs_test_runner_suite.h).
	FSLanguage::get_singleton()->init();

	FSBenchmarkRunner runner("modules/foundry_script/tests/benchmarks/_baseline");
	HashMap<String, double> results;
	const bool ok = runner.run_all(results);
	CHECK(ok);
	CHECK(results.size() >= 1);
	for (const KeyValue<String, double> &entry : results) {
		CHECK(entry.value > 0.0);
		CHECK(Math::is_finite(entry.value));
	}
}

TEST_CASE("[FSBenchmark] Profile pass captures per-function rows") {
	FSLanguage::get_singleton()->init();

	FSBenchmarkRunner runner("modules/foundry_script/tests/benchmarks/_baseline");
	Dictionary profile;
	const bool ok = runner.profile_all(profile);
	CHECK(ok);
	CHECK(profile.size() >= 1);

	// Every variant key maps to an array of function rows, and at least one row
	// (the workload's run_benchmark) must carry a positive call count plus the
	// documented self/total time and signature fields.
	bool saw_run_benchmark = false;
	for (const Variant &key : profile.keys()) {
		const Array functions = profile[key];
		for (int i = 0; i < functions.size(); i++) {
			const Dictionary row = functions[i];
			CHECK(row.has("signature"));
			CHECK(row.has("call_count"));
			CHECK(row.has("self_time"));
			CHECK(row.has("total_time"));
			const int64_t call_count = row["call_count"];
			CHECK(call_count > 0);
			// The function profiler labels each row with a non-empty signature
			// (`<path>::<line>::<function>`); the workload entry point must appear.
			const String signature = row["signature"];
			CHECK_FALSE(signature.is_empty());
			if (signature.contains("run_benchmark")) {
				saw_run_benchmark = true;
			}
		}
	}
	CHECK(saw_run_benchmark);
}

} // namespace FSTests
