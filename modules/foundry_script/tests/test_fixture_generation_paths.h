/**************************************************************************/
/*  test_fixture_generation_paths.h                                       */
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

#include "fs_temporary_project_tree.h"
#include "fs_test_runner.h"

#include "core/io/file_access.h"

#include "tests/test_macros.h"

namespace FSTests {

#ifdef TOOLS_ENABLED

// `foundry test generate-fixtures` accepts any directory inside the corpus, but the
// runner that consumes the generated `.out` files always walks from the corpus root.
// Expected output therefore has to record script paths relative to that root no matter
// which directory generation was pointed at, or generating a subdirectory would rewrite
// tracked fixtures into paths a later full run can never reproduce.
TEST_SUITE("[Modules][FoundryScript][FixtureGeneration]") {
	TEST_CASE("Generating a corpus subdirectory keeps corpus-root-relative script paths") {
		TemporaryProjectTree tree("fixture_generation_subdirectory");
		tree.write_file("project.foundry",
				"config_version=5\n\n[application]\n\nconfig/name=\"Fixture Generation Corpus\"\n");
		tree.write_file("runtime/features/out_of_bounds.fs",
				"func test():\n\tvar array := [1, 2, 3]\n\tvar _value = array[4]\n");

		const String generated_path = tree.root.path_join("runtime/features/out_of_bounds.out");

		FSTestRunner runner(tree.root.path_join("runtime/features"), true, false);
		REQUIRE_MESSAGE(runner.generate_outputs(), "Generating a subdirectory's fixtures must succeed.");

		Error read_error = OK;
		const String generated = FileAccess::get_file_as_string(generated_path, &read_error);
		REQUIRE_MESSAGE(read_error == OK, "The subdirectory's expected-output file must be written.");
		INFO("Generated output:\n", generated);
		CHECK_MESSAGE(generated.contains("runtime/features/out_of_bounds.fs:"),
				"Script paths must stay relative to the corpus root, not the invocation directory.");
		CHECK_MESSAGE(!generated.contains(" at out_of_bounds.fs:"),
				"A truncated script path would fail every later `test run`.");
	}
}

#endif // TOOLS_ENABLED

} // namespace FSTests
