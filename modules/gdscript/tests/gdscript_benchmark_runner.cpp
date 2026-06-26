/**************************************************************************/
/*  gdscript_benchmark_runner.cpp                                         */
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

#include "gdscript_benchmark_runner.h"

#include "../gdscript.h"

#include "core/io/config_file.h"
#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/object/class_db.h"
#include "core/os/os.h"
#include "core/templates/list.h"
#include "core/variant/callable.h"
#include "core/variant/variant.h"

#include <cstdlib>

namespace GDScriptTests {

GDScriptBenchmarkRunner::GDScriptBenchmarkRunner(const String &p_source_dir) {
	source_dir = p_source_dir;
}

GDScriptBenchmarkRunner::CaseConfig GDScriptBenchmarkRunner::load_case_config(const String &p_case_dir) const {
	CaseConfig config;
	Ref<ConfigFile> cfg;
	cfg.instantiate();
	const String cfg_path = p_case_dir.path_join("case.cfg");
	if (cfg->load(cfg_path) == OK) {
		config.iterations = (int)cfg->get_value("case", "iterations", config.iterations);
		config.warmup = (int)cfg->get_value("case", "warmup", config.warmup);
		config.overhead_threshold_percent = (int)cfg->get_value("case", "overhead_threshold_percent", config.overhead_threshold_percent);
		config.note = (String)cfg->get_value("case", "note", config.note);
	}
	return config;
}

bool GDScriptBenchmarkRunner::collect_variants(const String &p_dir, Vector<WorkloadVariant> &r_variants) const {
	Ref<DirAccess> dir = DirAccess::open(p_dir);
	ERR_FAIL_COND_V_MSG(dir.is_null(), false, "Could not open benchmark directory: " + p_dir);

	dir->list_dir_begin();
	String name = dir->get_next();
	Vector<String> subdirs;
	while (!name.is_empty()) {
		if (dir->current_is_dir() && !name.begins_with(".")) {
			subdirs.push_back(p_dir.path_join(name));
		}
		name = dir->get_next();
	}
	dir->list_dir_end();

	// A directory that contains a case.cfg is a case; otherwise recurse.
	const String case_name = p_dir.get_file();
	const bool is_case = FileAccess::exists(p_dir.path_join("case.cfg"));
	if (is_case) {
		const CaseConfig config = load_case_config(p_dir);
		Ref<DirAccess> case_dir = DirAccess::open(p_dir);
		case_dir->list_dir_begin();
		String entry = case_dir->get_next();
		while (!entry.is_empty()) {
			if (!case_dir->current_is_dir() && entry.get_extension() == "gd") {
				WorkloadVariant variant;
				variant.case_name = case_name;
				variant.variant_name = entry.get_basename();
				variant.script_path = p_dir.path_join(entry);
				variant.config = config;
				r_variants.push_back(variant);
			}
			entry = case_dir->get_next();
		}
		case_dir->list_dir_end();
	}

	// Sort subdirectories for deterministic discovery order.
	subdirs.sort();
	for (const String &sub : subdirs) {
		collect_variants(sub, r_variants);
	}
	return true;
}

double GDScriptBenchmarkRunner::run_variant(const WorkloadVariant &p_variant) const {
	// Compile (excluded from timing).
	Ref<GDScript> script;
	script.instantiate();
	if (script->load_source_code(p_variant.script_path) != OK) {
		ERR_PRINT("Could not load: " + p_variant.script_path);
		return -1.0;
	}
	script->set_path(p_variant.script_path);
	if (script->reload() != OK) {
		ERR_PRINT("Could not reload: " + p_variant.script_path);
		return -1.0;
	}

	Object *obj = ClassDB::instantiate(script->get_native()->get_name());
	ERR_FAIL_NULL_V_MSG(obj, -1.0, "Could not instantiate native base for: " + p_variant.script_path);
	Ref<RefCounted> obj_ref;
	if (obj->is_ref_counted()) {
		obj_ref = Ref<RefCounted>(Object::cast_to<RefCounted>(obj));
	}
	obj->set_script(script);
	ScriptInstance *instance = obj->get_script_instance();
	if (instance == nullptr) {
		if (obj_ref.is_null()) {
			memdelete(obj);
		}
		ERR_FAIL_V_MSG(-1.0, "Could not attach script instance for: " + p_variant.script_path);
	}

	const StringName method = "run_benchmark";

	// Warmup (discarded).
	for (int i = 0; i < p_variant.config.warmup; i++) {
		Variant arg = p_variant.config.iterations;
		const Variant *argp = &arg;
		Callable::CallError err;
		instance->callp(method, &argp, 1, err);
		if (err.error != Callable::CallError::CALL_OK) {
			if (obj_ref.is_null()) {
				memdelete(obj);
			}
			ERR_FAIL_V_MSG(-1.0, "Workload call failed (warmup): " + p_variant.script_path);
		}
	}

	// Measured.
	const String what = p_variant.case_name + "/" + p_variant.variant_name;
	Variant arg = p_variant.config.iterations;
	const Variant *argp = &arg;
	Callable::CallError err;
	const uint64_t from = OS::get_singleton()->get_ticks_usec();
	OS::get_singleton()->benchmark_begin_measure("gdscript", what);
	instance->callp(method, &argp, 1, err);
	OS::get_singleton()->benchmark_end_measure("gdscript", what);
	const double elapsed_usec = (double)(OS::get_singleton()->get_ticks_usec() - from);
	if (err.error != Callable::CallError::CALL_OK) {
		if (obj_ref.is_null()) {
			memdelete(obj);
		}
		ERR_FAIL_V_MSG(-1.0, "Workload call failed (measured): " + p_variant.script_path);
	}

	if (obj_ref.is_null()) {
		memdelete(obj);
	}
	return elapsed_usec;
}

HashMap<String, double> GDScriptBenchmarkRunner::run_all() const {
	HashMap<String, double> results;
	Vector<WorkloadVariant> variants;
	collect_variants(source_dir, variants);
	for (const WorkloadVariant &variant : variants) {
		const double usec = run_variant(variant);
		if (usec >= 0.0) {
			results["gdscript:" + variant.case_name + "/" + variant.variant_name] = usec;
		}
	}
	return results;
}

void GDScriptBenchmarkRunner::handle_cmdline() {
	List<String> args = OS::get_singleton()->get_cmdline_args();
	String dir;
	String output_path;
	for (List<String>::Element *E = args.front(); E; E = E->next()) {
		if (E->get() == "--gdscript-benchmark" && E->next()) {
			dir = E->next()->get();
		} else if (E->get() == "--gdscript-benchmark-output" && E->next()) {
			output_path = E->next()->get();
		}
	}
	if (dir.is_empty()) {
		return; // Flag not present; normal startup continues.
	}

	GDScriptBenchmarkRunner runner(dir);
	HashMap<String, double> results = runner.run_all();

	// Reuse the engine benchmark module's JSON serialization.
	if (!output_path.is_empty()) {
		OS::get_singleton()->set_use_benchmark(true);
		OS::get_singleton()->set_benchmark_file(output_path);
	}
	OS::get_singleton()->benchmark_dump();

	print_line(vformat("gdscript-benchmark: ran %d variant(s).", results.size()));
	exit(0);
}

} // namespace GDScriptTests
