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

#include "core/error/error_macros.h"
#include "core/io/config_file.h"
#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/io/json.h"
#include "core/object/class_db.h"
#include "core/os/os.h"
#include "core/templates/list.h"
#include "core/variant/callable.h"
#include "core/variant/dictionary.h"
#include "core/variant/variant.h"

#include <cstdio>
#include <cstdlib>

namespace GDScriptTests {

namespace {

// Tracks whether a GDScript runtime error was emitted while a workload was being
// compiled, initialized, or called. Such failures surface through the error
// handler with type ERR_HANDLER_SCRIPT and leave the Callable::CallError as
// CALL_OK, so the call-error check alone would not notice a broken workload.
// Only ERR_HANDLER_SCRIPT is treated as a failure: GDScript::reload() emits a
// spurious ERR_HANDLER_ERROR ("Condition \"err\" is true") on success, so
// flagging generic engine errors would produce false positives.
struct WorkloadErrorTracker {
	bool errored = false;
};

void workload_error_handler(void *p_userdata, const char *p_function, const char *p_file, int p_line, const char *p_error, const char *p_explanation, bool p_editor_notify, ErrorHandlerType p_type) {
	if (p_type == ERR_HANDLER_SCRIPT) {
		static_cast<WorkloadErrorTracker *>(p_userdata)->errored = true;
	}
}

} // namespace

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
	bool all_ok = true;
	for (const String &sub : subdirs) {
		all_ok = collect_variants(sub, r_variants) && all_ok;
	}
	return all_ok;
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

	// Catch GDScript runtime errors, which do not surface as Callable::CallError.
	// Installed before reload()/set_script() so failures in static initializers,
	// member initializers, and _init() are also detected.
	WorkloadErrorTracker tracker;
	ErrorHandlerList error_handler;
	error_handler.errfunc = workload_error_handler;
	error_handler.userdata = &tracker;
	add_error_handler(&error_handler);

	Object *obj = nullptr;
	Ref<RefCounted> obj_ref;
	const auto fail = [&](const String &p_message) -> double {
		remove_error_handler(&error_handler);
		if (obj != nullptr && obj_ref.is_null()) {
			memdelete(obj);
		}
		ERR_FAIL_V_MSG(-1.0, p_message);
	};

	if (script->reload() != OK || tracker.errored) {
		return fail("Could not reload: " + p_variant.script_path);
	}

	obj = ClassDB::instantiate(script->get_native()->get_name());
	if (obj == nullptr) {
		return fail("Could not instantiate native base for: " + p_variant.script_path);
	}
	if (obj->is_ref_counted()) {
		obj_ref = Ref<RefCounted>(Object::cast_to<RefCounted>(obj));
	}
	obj->set_script(script);
	if (tracker.errored) {
		return fail("Workload failed during initialization: " + p_variant.script_path);
	}
	ScriptInstance *instance = obj->get_script_instance();
	if (instance == nullptr) {
		return fail("Could not attach script instance for: " + p_variant.script_path);
	}

	const StringName method = "run_benchmark";

	// Warmup (discarded).
	for (int i = 0; i < p_variant.config.warmup; i++) {
		Variant arg = p_variant.config.iterations;
		const Variant *argp = &arg;
		Callable::CallError err;
		instance->callp(method, &argp, 1, err);
		if (err.error != Callable::CallError::CALL_OK || tracker.errored) {
			return fail("Workload failed (warmup): " + p_variant.script_path);
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
	if (err.error != Callable::CallError::CALL_OK || tracker.errored) {
		return fail("Workload failed (measured): " + p_variant.script_path);
	}

	remove_error_handler(&error_handler);
	if (obj_ref.is_null()) {
		memdelete(obj);
	}
	return elapsed_usec;
}

bool GDScriptBenchmarkRunner::run_all(HashMap<String, double> &r_results) const {
	Vector<WorkloadVariant> variants;
	if (!collect_variants(source_dir, variants)) {
		return false;
	}
	if (variants.is_empty()) {
		ERR_PRINT("No benchmark variants found under: " + source_dir);
		return false;
	}

	bool all_ok = true;
	for (const WorkloadVariant &variant : variants) {
		const double usec = run_variant(variant);
		if (usec < 0.0) {
			all_ok = false;
			continue;
		}
		r_results["gdscript:" + variant.case_name + "/" + variant.variant_name] = usec;
	}
	return all_ok;
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
	HashMap<String, double> results;
	const bool ok = runner.run_all(results);

	// Emit the documented corpus format: a JSON object mapping
	// "gdscript:<case>/<variant>" to the measured microseconds.
	Dictionary json_map;
	for (const KeyValue<String, double> &entry : results) {
		json_map[entry.key] = entry.value;
	}
	const String json = JSON::stringify(json_map, "\t", true, true);
	bool wrote_output = true;
	if (output_path.is_empty()) {
		// Stdout is the result channel; keep it pure JSON so callers can parse it.
		print_line(json);
	} else {
		Ref<FileAccess> file = FileAccess::open(output_path, FileAccess::WRITE);
		if (file.is_null()) {
			ERR_PRINT("Could not open benchmark output file: " + output_path);
			wrote_output = false;
		} else {
			file->store_string(json);
			file->close();
			print_line(vformat("gdscript-benchmark: wrote %d variant(s) to %s.", results.size(), output_path));
		}
	}

	// Terminate immediately rather than returning into editor startup. `_Exit`
	// is used instead of `exit()` because the engine is only partway through
	// initialization here: running C++ static destructors on a half-built engine
	// aborts on some platforms. The benchmark output file is already flushed and
	// closed above, so nothing is lost.
	fflush(nullptr);
	std::_Exit(ok && wrote_output ? 0 : 1);
}

} // namespace GDScriptTests
