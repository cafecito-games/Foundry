/**************************************************************************/
/*  fuzz_gdscript_macos.mm                                                */
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

// libFuzzer entry point for the GDScript tokenizer + parser.
//
// libFuzzer supplies main(); this translation unit replaces the normal
// platform main() when the engine is built with `use_fuzzer=yes`. The engine
// is brought up once (headless) and then each fuzzer iteration drives a fresh
// parser over the mutated input. The parser's hard contract is that *any* byte
// sequence must produce either a clean error list or a valid parse tree, never
// a crash, so any abort here is a genuine robustness bug.

#include "main/main.h"

#include "os_macos.h"

// The macOS SDK defines `nil` as a macro, which collides with member names such
// as `Variant nil;` inside the GDScript headers. Suppress it for the include.
#pragma push_macro("nil")
#undef nil
#include "modules/gdscript/gdscript_analyzer.h"
#include "modules/gdscript/gdscript_cache.h"
#include "modules/gdscript/gdscript_parser.h"
#pragma pop_macro("nil")

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>

namespace {
OS_MacOS *fuzz_os = nullptr;
bool engine_ready = false;

// The engine expects an orderly Main::cleanup(); it is never run here because
// the process only ever fuzzes. Letting the C runtime tear down Godot's global
// StringName/ClassDB tables at exit() trips their allocator and mutex teardown
// and aborts. Registering this from LLVMFuzzerInitialize (after every engine
// global has registered its own __cxa_atexit destructor) means it runs first on
// exit and terminates before that teardown can crash. Crashes during the fuzz
// loop itself are caught by AddressSanitizer mid-run and are unaffected.
void fuzz_fast_exit() {
	_exit(0);
}
} // namespace

extern "C" int LLVMFuzzerInitialize(int *p_argc, char ***p_argv) {
	// Boot the engine headless once. Main::setup() with the second phase enabled
	// registers core types, the GDScript module, and ClassDB, which the parser
	// relies on for built-in type and global name resolution. The headless OS is
	// used deliberately: OS_MacOS_NSApp::run() drives a Cocoa loop that never
	// returns, whereas we only need the engine initialized, not running.
	static char headless_arg[] = "--headless";
	static char *engine_argv[] = { headless_arg };

	fuzz_os = memnew(OS_MacOS_Headless("godot", 1, engine_argv));

	@autoreleasepool {
		const Error err = Main::setup("godot", 1, engine_argv, true);
		engine_ready = err == OK;
	}

	// Godot installs its own crash handler during setup, which would otherwise
	// shadow the signal handlers that libFuzzer and AddressSanitizer rely on to
	// produce a usable crash report.
	if (fuzz_os != nullptr) {
		fuzz_os->disable_crash_handler();
	}

	atexit(fuzz_fast_exit);

	return 0;
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
