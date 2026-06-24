/**************************************************************************/
/*  test_fixpoint_inference.h                                             */
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

#ifdef TOOLS_ENABLED

#include "tests/test_macros.h"

#include "../editor/gdscript_fixpoint_inference.h"

#include "core/io/file_access.h"

#include "test_refactor.h" // GDScriptTests::TemporaryScriptFile

#ifndef GDSCRIPT_NO_LSP

#include "editor/file_system/editor_file_system.h"

namespace GDScriptTests {

TEST_SUITE("[Modules][GDScript][Fixpoint]") {
	TEST_CASE("Single-file run types every resolvable declaration and is idempotent") {
		// Initialize the GDScript test project so res:// resolves to the test
		// scripts directory and the analyzer can read dependencies from disk.
		EditorFileSystem *editor_file_system = memnew(EditorFileSystem);
		GDScriptLanguageProtocol *protocol = GDScriptTests::initialize(GDScriptTests::root);
		REQUIRE(protocol);

		const String path = "res://refactor/fixpoint_single.gd";
		const String source =
				"func compute():\n"
				"\treturn inner()\n"
				"func inner():\n"
				"\treturn 42\n";
		TemporaryScriptFile file(path, source);

		Vector<String> paths;
		paths.push_back(path);

		FixpointInferenceResult result = GDScriptFixpointInference::run(paths);
		REQUIRE(result.ok);
		REQUIRE_EQ(result.changed_files.size(), 1);

		const String after = FileAccess::get_file_as_string(path);
		CHECK(after.contains("func compute() -> int:"));
		CHECK(after.contains("func inner() -> int:"));

		// Running again on the now-typed file changes nothing.
		FixpointInferenceResult second = GDScriptFixpointInference::run(paths);
		REQUIRE(second.ok);
		CHECK_EQ(second.changed_files.size(), 0);

		memdelete(protocol);
		memdelete(editor_file_system);
		GDScriptTests::finish_language();
	}

	TEST_CASE("Cross-file chain converges past the leaf; one pass types only the leaf") {
		// Initialize the GDScript test project so res:// resolves to the test
		// scripts directory and cross-file analysis can read dependencies from disk.
		EditorFileSystem *editor_file_system = memnew(EditorFileSystem);
		GDScriptLanguageProtocol *protocol = GDScriptTests::initialize(GDScriptTests::root);
		REQUIRE(protocol);

		const String path_a = "res://refactor/fixpoint_chain_a.gd";
		const String path_b = "res://refactor/fixpoint_chain_b.gd";
		const String path_c = "res://refactor/fixpoint_chain_c.gd";

		// C is the leaf; B relays C; A reads B. Each layer needs the one below it
		// typed and written before its own return/var becomes inferable.
		const String source_c =
				"static func value():\n"
				"\treturn 42\n";
		const String source_b =
				"const C = preload(\"res://refactor/fixpoint_chain_c.gd\")\n"
				"static func relay():\n"
				"\treturn C.value()\n";
		const String source_a =
				"const B = preload(\"res://refactor/fixpoint_chain_b.gd\")\n"
				"var x = B.relay()\n";

		Vector<String> paths;
		paths.push_back(path_a);
		paths.push_back(path_b);
		paths.push_back(path_c);

		SUBCASE("single pass types only the leaf") {
			TemporaryScriptFile file_a(path_a, source_a);
			TemporaryScriptFile file_b(path_b, source_b);
			TemporaryScriptFile file_c(path_c, source_c);

			FixpointInferenceOptions options;
			options.max_iterations = 1;
			FixpointInferenceResult result = GDScriptFixpointInference::run(paths, options);
			REQUIRE(result.ok);

			CHECK(FileAccess::get_file_as_string(path_c).contains("static func value() -> int:"));
			CHECK_FALSE(FileAccess::get_file_as_string(path_b).contains("relay() -> "));
			CHECK_FALSE(FileAccess::get_file_as_string(path_a).contains("var x: "));
		}

		SUBCASE("full fixpoint types the whole chain") {
			TemporaryScriptFile file_a(path_a, source_a);
			TemporaryScriptFile file_b(path_b, source_b);
			TemporaryScriptFile file_c(path_c, source_c);

			FixpointInferenceResult result = GDScriptFixpointInference::run(paths);
			REQUIRE(result.ok);
			CHECK(result.iterations > 1);

			CHECK(FileAccess::get_file_as_string(path_c).contains("static func value() -> int:"));
			CHECK(FileAccess::get_file_as_string(path_b).contains("static func relay() -> int:"));
			CHECK(FileAccess::get_file_as_string(path_a).contains("var x: int = B.relay()"));
		}

		memdelete(protocol);
		memdelete(editor_file_system);
		GDScriptTests::finish_language();
	}
}

} // namespace GDScriptTests

#endif // !GDSCRIPT_NO_LSP

#endif // TOOLS_ENABLED
