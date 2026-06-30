/**************************************************************************/
/*  test_release_vm_runtime_error.h                                       */
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

#include "fs_test_runner.h"

#include "tests/test_macros.h"

namespace FSTests {

// Exercises VM runtime error reporting through `_err_print_error()` with
// `ERR_HANDLER_SCRIPT`. Unlike debugger-only handling, this path must work in
// release/template builds where `DEBUG_ENABLED` is off.
//
// Verified release build (Linux):
//   python3 -m SCons platform=linuxbsd target=template_release tests=yes module_text_server_fb_enabled=yes cache_path="$HOME/.scons_cache" -j$(nproc)
//   ./bin/foundry.linuxbsd.template_release.x86_64 --headless --test --test-case="*Release-build VM runtime error*"
static FSTest::TestResult run_runtime_error_fixture(const String &p_relative_path) {
	const String base_dir = "modules/foundry_script/tests/scripts/";
	const String source = base_dir.path_join(p_relative_path);
	const String output = source.get_basename() + ".out";

	FSTestRunner::test_function_name = StringName("test");
	init_language(base_dir);
	FSTest test(source, output, base_dir);
	FSTest::TestResult result = test.run_test();
	FSTestRunner::test_function_name = StringName();
	finish_language();
	return result;
}

TEST_CASE("[Modules][FoundryScript] Release-build VM runtime error reporting") {
	SUBCASE("Typed return mismatch is reported through ERR_HANDLER_SCRIPT") {
		const FSTest::TestResult result = run_runtime_error_fixture("runtime/errors/return_typed_int_mismatch.fs");

		CHECK(result.status == FSTest::FS_TEST_RUNTIME_ERROR);
		CHECK(result.output.contains("Trying to return value of type \"String\" from a function whose return type is \"int\"."));
#ifdef DEBUG_ENABLED
		CHECK_MESSAGE(result.passed, "Fixture output should match return_typed_int_mismatch.out.");
#endif
	}

	SUBCASE("Invalid operator is reported through ERR_HANDLER_SCRIPT") {
		const FSTest::TestResult result = run_runtime_error_fixture("runtime/errors/operator_dynamic_cache_miss.fs");

		CHECK(result.status == FSTest::FS_TEST_RUNTIME_ERROR);
		CHECK(result.output.contains("Invalid operands 'String' and 'Array' in operator '+'."));
#ifdef DEBUG_ENABLED
		CHECK_MESSAGE(result.passed, "Fixture output should match operator_dynamic_cache_miss.out.");
#endif
	}
}

} // namespace FSTests
