/**************************************************************************/
/*  fuzz_foundry_script_macos.mm                                          */
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

// macOS libFuzzer entry point. libFuzzer supplies main(); this translation unit
// replaces the normal platform main() when the engine is built with
// `use_fuzzer=yes`. It only constructs the headless OS and hands off to the
// shared, platform-independent harness in modules/foundry_script/tests/fuzz/.

#include "os_macos.h"

#include "modules/foundry_script/tests/fuzz/fs_fuzzer.h"

extern "C" int LLVMFuzzerInitialize(int *p_argc, char ***p_argv) {
	// The headless OS is used deliberately: OS_MacOS_NSApp::run() drives a Cocoa
	// loop that never returns, whereas the fuzzer only needs the engine
	// initialized, not running.
	static char headless_arg[] = "--headless";
	static char *engine_argv[] = { headless_arg };

	OS_MacOS_Headless *os = memnew(OS_MacOS_Headless("godot", 1, engine_argv));

	@autoreleasepool {
		fs_fuzzer_initialize_engine(os, 1, engine_argv);
	}

	return 0;
}
