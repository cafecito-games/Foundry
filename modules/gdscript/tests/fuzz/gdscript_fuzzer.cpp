/**************************************************************************/
/*  gdscript_fuzzer.cpp                                                   */
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

#include "gdscript_fuzzer.h"

#include "../../gdscript_analyzer.h"
#include "../../gdscript_cache.h"
#include "../../gdscript_parser.h"

#include "core/os/os.h"
#include "core/string/ustring.h"

#include "main/main.h"

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>

namespace {
bool engine_ready = false;

// The engine expects an orderly Main::cleanup(); it is never run here because
// the process only ever fuzzes. Letting the C runtime tear down Godot's global
// StringName/ClassDB tables at exit() trips their allocator and mutex teardown
// and aborts. Registering this from the libFuzzer initializer (after every
// engine global has registered its own __cxa_atexit destructor) means it runs
// first on exit and terminates before that teardown can crash. Crashes during
// the fuzz loop itself are caught by the sanitizer mid-run and are unaffected.
void fuzzer_fast_exit() {
	_exit(0);
}
} // namespace

void gdscript_fuzzer_initialize_engine(OS *p_os, int p_argc, char **p_argv) {
	const Error err = Main::setup("godot", p_argc, p_argv, true);
	engine_ready = err == OK;

	// Godot installs its own crash handler during setup, which would otherwise
	// shadow the signal handlers that libFuzzer and the sanitizers rely on to
	// produce a usable crash report.
	if (p_os != nullptr) {
		p_os->disable_crash_handler();
	}

	atexit(fuzzer_fast_exit);
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *p_data, size_t p_size) {
	if (!engine_ready) {
		return 0;
	}

	const String source = String::utf8(reinterpret_cast<const char *>(p_data), p_size);
	const String path = "fuzz://input.gd";

	GDScriptParser parser;
	const Error parse_error = parser.parse(source, path, false);

	// Only feed a clean parse tree to the analyzer; on a parse error the tree is
	// in a recovery state the analyzer is not expected to handle. The analyzer is
	// where this fork's stricter typing, generics, and trait resolution live, so
	// it is the more interesting target once parsing succeeds.
	if (parse_error == OK) {
		GDScriptAnalyzer analyzer(&parser);
		analyzer.analyze();
	}

	// The analyzer can register the script and its parser under `path` in the
	// global GDScriptCache. Drop them so each iteration starts from a clean cache
	// and a crash always reproduces from the single input that caused it.
	GDScriptCache::remove_parser(path);
	GDScriptCache::remove_script(path);

	return 0;
}
