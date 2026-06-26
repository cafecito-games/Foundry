/**************************************************************************/
/*  gdscript_benchmark_runner.h                                           */
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

#include "core/string/ustring.h"
#include "core/templates/hash_map.h"
#include "core/templates/vector.h"
#include "core/variant/dictionary.h"

namespace GDScriptTests {

// Runs a corpus of `.gd` benchmark workloads (see benchmarks/README.md).
// Each case directory holds workload variants plus a `case.cfg`. Variants are
// the `.gd` files in the directory; each exposes `run_benchmark(iterations)`.
class GDScriptBenchmarkRunner {
	String source_dir;

	struct CaseConfig {
		int iterations = 1000;
		int warmup = 3;
		int overhead_threshold_percent = -1;
		String note;
	};

	struct WorkloadVariant {
		String case_name; // e.g. "proxy_dispatch"
		String variant_name; // e.g. "feature" (the .gd basename)
		String script_path;
		CaseConfig config;
	};

	bool collect_variants(const String &p_dir, Vector<WorkloadVariant> &r_variants) const;
	CaseConfig load_case_config(const String &p_case_dir) const;
	// Runs one variant; returns measured microseconds, or -1.0 on failure.
	double run_variant(const WorkloadVariant &p_variant) const;
	// Runs one variant under the GDScript function profiler, appending a
	// per-function row (a Dictionary {signature, call_count, self_time,
	// total_time}) to `r_functions` for every function the workload actually
	// invoked. Returns false if the workload failed to compile or run.
	bool profile_variant(const WorkloadVariant &p_variant, Array &r_functions) const;

public:
	explicit GDScriptBenchmarkRunner(const String &p_source_dir);

	// Runs every discovered variant, filling r_results with a map whose key is
	// "gdscript:<case>/<variant>" and whose value is the measured microseconds.
	// Returns false if discovery failed, no variant was found, or any variant
	// failed to compile/run.
	bool run_all(HashMap<String, double> &r_results) const;

	// Profiles every discovered variant under the GDScript function profiler in a
	// pass separate from the timing run, filling r_profile with a map whose key is
	// "<case>/<variant>" and whose value is an Array of per-function rows. Returns
	// false if discovery failed, no variant was found, or any variant failed.
	bool profile_all(Dictionary &r_profile) const;

	// CLI entry: scans for `--gdscript-benchmark <dir>`, runs, dumps JSON, exits.
	static void handle_cmdline();
};

} // namespace GDScriptTests
