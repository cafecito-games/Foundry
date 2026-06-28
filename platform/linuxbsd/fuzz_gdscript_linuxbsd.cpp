/**************************************************************************/
/*  fuzz_gdscript_linuxbsd.cpp                                            */
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

// Linux libFuzzer entry point. libFuzzer supplies main(); this translation unit
// replaces the normal platform main() when the engine is built with
// `use_fuzzer=yes` (which requires Clang, since libFuzzer ships with LLVM). It
// only constructs the OS and hands off to the shared, platform-independent
// harness in modules/gdscript/tests/fuzz/. The `--headless` argument selects the
// dummy display/audio servers so no GUI or device is needed.

#include "os_linuxbsd.h"

#include "modules/gdscript/tests/fuzz/gdscript_fuzzer.h"

extern "C" int LLVMFuzzerInitialize(int *p_argc, char ***p_argv) {
	static char headless_arg[] = "--headless";
	static char *engine_argv[] = { headless_arg };

	// Heap-allocated and intentionally never freed: the engine is brought up once
	// for the lifetime of the fuzzing process and torn down via _exit().
	OS_LinuxBSD *os = new OS_LinuxBSD();

	gdscript_fuzzer_initialize_engine(os, 1, engine_argv);

	return 0;
}
