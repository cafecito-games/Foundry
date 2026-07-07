/**************************************************************************/
/*  fs_inline_eval.cpp                                                    */
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

#include "fs_inline_eval.h"

#include "fs_cache.h"

#include "core/io/resource_loader.h"
#include "core/object/class_db.h"
#include "core/object/script_language.h"
#include "core/object/script_runner.h"

String FSInlineEval::synthetic_source_path() {
	return "user://foundry_inline_eval.fs";
}

String FSInlineEval::build_runner_source(const String &p_user_source) {
	// Normalize line endings so a snippet pasted from any platform indents cleanly.
	const String normalized = p_user_source.replace("\r\n", "\n").replace("\r", "\n");

	String body;
	if (!normalized.is_empty()) {
		const Vector<String> lines = normalized.split("\n");
		for (const String &line : lines) {
			// Keep blank lines blank; indent real statements into the run() body.
			if (line.is_empty()) {
				body += "\n";
			} else {
				body += "\t" + line + "\n";
			}
		}
	}

	// The generated wrapper keeps the grammar unchanged: the snippet becomes the body
	// of a ScriptRunner.run() override. `args` exposes the user args passed after `--`.
	// `unused_parameter` is silenced because a snippet need not reference `args`, and
	// `unreachable_code` is silenced on the injected default `return 0` so a snippet
	// that ends in its own `return` does not emit a spurious warning.
	String source;
	source += "extends ScriptRunner\n\n";
	source += "@warning_ignore(\"unused_parameter\")\n";
	source += "func run(args: PackedStringArray) -> int:\n";
	source += body;
	source += "\t@warning_ignore(\"unreachable_code\")\n";
	source += "\treturn 0\n";
	return source;
}

Ref<ScriptRunner> FSInlineEval::compile_runner(const String &p_user_source, String &r_error) {
	r_error = String();

	const String path = synthetic_source_path();
	const String source = build_runner_source(p_user_source);

	// Feed the generated source to the normal Foundry Script load pipeline through an
	// in-memory override so parse/analyze/compile diagnostics refer to the synthetic
	// path and no file is written to disk. CACHE_MODE_IGNORE forces a fresh compile so
	// repeated evals in the same process never reuse a previous snippet.
	FSCache::set_source_override(path, source);

	// The FS loader returns a script even when the source has parse/analysis errors
	// (its validity is reported through Script::is_valid()), so the error state is
	// inspected below rather than through the loader's own error out-parameter.
	Ref<Script> script_res = ResourceLoader::load(path, "", ResourceFormatLoader::CACHE_MODE_IGNORE);

	FSCache::clear_source_override(path);

	if (script_res.is_null()) {
		r_error = "Failed to compile the inline script.";
		return Ref<ScriptRunner>();
	}
	if (!script_res->is_valid()) {
		r_error = "The inline script has parse, analysis, or compile errors.";
		return Ref<ScriptRunner>();
	}
	if (!script_res->can_instantiate() ||
			!ClassDB::is_parent_class(script_res->get_instance_base_type(), "ScriptRunner")) {
		r_error = "The inline script did not compile to a runnable ScriptRunner.";
		return Ref<ScriptRunner>();
	}

	ScriptRunner *runner_object = memnew(ScriptRunner);
	Ref<ScriptRunner> runner(runner_object);
	runner->set_script(script_res);
	return runner;
}
