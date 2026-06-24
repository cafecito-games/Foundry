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
		CHECK(result.converged);
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

	TEST_CASE("Duplicate input paths are collapsed and reported once") {
		// Initialize the GDScript test project so res:// resolves to the test
		// scripts directory and the analyzer can read dependencies from disk.
		EditorFileSystem *editor_file_system = memnew(EditorFileSystem);
		GDScriptLanguageProtocol *protocol = GDScriptTests::initialize(GDScriptTests::root);
		REQUIRE(protocol);

		const String path = "res://refactor/fixpoint_dedup_leaf.gd";
		const String source = "static func value():\n\treturn 42\n";
		TemporaryScriptFile file(path, source);

		// The same path appears twice in the input vector.
		Vector<String> paths;
		paths.push_back(path);
		paths.push_back(path);

		FixpointInferenceResult result = GDScriptFixpointInference::run(paths);
		REQUIRE(result.ok);

		// The file is reported exactly once despite the duplicate input.
		CHECK_EQ(result.changed_files.size(), 1);
		CHECK(FileAccess::get_file_as_string(path).contains("static func value() -> int:"));

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
			CHECK_FALSE(result.converged);

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
			CHECK(result.converged);
			CHECK(result.iterations > 1);

			CHECK(FileAccess::get_file_as_string(path_c).contains("static func value() -> int:"));
			CHECK(FileAccess::get_file_as_string(path_b).contains("static func relay() -> int:"));
			CHECK(FileAccess::get_file_as_string(path_a).contains("var x: int = B.relay()"));
		}

		memdelete(protocol);
		memdelete(editor_file_system);
		GDScriptTests::finish_language();
	}

	TEST_CASE("Mutually-referencing files terminate and report honest counts") {
		// Initialize the GDScript test project so res:// resolves to the test
		// scripts directory and cross-file analysis can read dependencies from disk.
		EditorFileSystem *editor_file_system = memnew(EditorFileSystem);
		GDScriptLanguageProtocol *protocol = GDScriptTests::initialize(GDScriptTests::root);
		REQUIRE(protocol);

		const String path_a = "res://refactor/fixpoint_cycle_a.gd";
		const String path_b = "res://refactor/fixpoint_cycle_b.gd";

		// A and B reference each other; each also has an independently-typable leaf
		// so the run produces some annotations and then converges.
		const String source_a =
				"const B = preload(\"res://refactor/fixpoint_cycle_b.gd\")\n"
				"static func a_leaf():\n"
				"\treturn 1\n"
				"static func uses_b():\n"
				"\treturn B.b_leaf()\n";
		const String source_b =
				"const A = preload(\"res://refactor/fixpoint_cycle_a.gd\")\n"
				"static func b_leaf():\n"
				"\treturn 2\n"
				"static func uses_a():\n"
				"\treturn A.a_leaf()\n";

		TemporaryScriptFile file_a(path_a, source_a);
		TemporaryScriptFile file_b(path_b, source_b);

		Vector<String> paths;
		paths.push_back(path_a);
		paths.push_back(path_b);

		FixpointInferenceResult result = GDScriptFixpointInference::run(paths);
		REQUIRE(result.ok);

		// Terminated well under the safety ceiling.
		CHECK(result.iterations < 1000);

		// The independently-typable leaves are resolved.
		CHECK(FileAccess::get_file_as_string(path_a).contains("static func a_leaf() -> int:"));
		CHECK(FileAccess::get_file_as_string(path_b).contains("static func b_leaf() -> int:"));

		// Report counts are internally consistent.
		int summed = 0;
		for (const FixpointFileChange &change : result.changed_files) {
			CHECK(change.annotations_applied > 0);
			summed += change.annotations_applied;
		}
		CHECK_EQ(summed, result.total_annotations_applied);

		memdelete(protocol);
		memdelete(editor_file_system);
		GDScriptTests::finish_language();
	}

	TEST_CASE("Run does not commit an edit that would break a dependent") {
		EditorFileSystem *editor_file_system = memnew(EditorFileSystem);
		GDScriptLanguageProtocol *protocol = GDScriptTests::initialize(GDScriptTests::root);
		REQUIRE(protocol);

		const String provider_path = "res://refactor/fixpoint_provider.gd";
		const String provider_source =
				"func get_value():\n"
				"\treturn 42\n";
		TemporaryScriptFile provider(provider_path, provider_source);

		// The consumer uses preload so GDScriptCache records a parser inverse dependency
		// from the provider back to the consumer, which the harness needs to re-analyze
		// the consumer when verifying changes to the provider.
		const String consumer_path = "res://refactor/fixpoint_consumer.gd";
		const String consumer_source =
				"const Provider = preload(\"res://refactor/fixpoint_provider.gd\")\n"
				"func use() -> void:\n"
				"\tvar p: Provider = Provider.new()\n"
				"\tvar s: String = p.get_value()\n";
		TemporaryScriptFile consumer(consumer_path, consumer_source);

		Vector<String> paths = { provider_path, consumer_path };
		FixpointInferenceResult result = GDScriptFixpointInference::run(paths);
		REQUIRE(result.ok);

		// The run reaches a real fixpoint: the only candidate is permanently rejected, so
		// a pass eventually applies nothing rather than the bound merely being exhausted.
		CHECK(result.converged);

		// The breaking return-type edit must not have been committed.
		const String provider_after = FileAccess::get_file_as_string(provider_path);
		CHECK_FALSE(provider_after.contains("func get_value() -> int:"));
		CHECK_EQ(provider_after, provider_source);

		// The consumer is already fully typed (its locals and return type are annotated),
		// so the run has nothing to legitimately apply there and must leave it untouched.
		const String consumer_after = FileAccess::get_file_as_string(consumer_path);
		CHECK_EQ(consumer_after, consumer_source);

		// It is reported as a skip.
		bool reported = false;
		for (const FixpointSkipped &skip : result.skipped) {
			if (skip.path == provider_path) {
				reported = true;
			}
		}
		CHECK(reported);

		memdelete(protocol);
		memdelete(editor_file_system);
		GDScriptTests::finish_language();
	}

	TEST_CASE("Edge cases: empty input, unreadable path, unanalyzable file") {
		// Initialize the GDScript test project so res:// resolves to the test
		// scripts directory and the analyzer can read dependencies from disk.
		EditorFileSystem *editor_file_system = memnew(EditorFileSystem);
		GDScriptLanguageProtocol *protocol = GDScriptTests::initialize(GDScriptTests::root);
		REQUIRE(protocol);

		SUBCASE("empty input is a successful no-op") {
			FixpointInferenceResult result = GDScriptFixpointInference::run(Vector<String>());
			CHECK(result.ok);
			CHECK_EQ(result.changed_files.size(), 0);
			CHECK_EQ(result.total_annotations_applied, 0);
		}

		SUBCASE("unreadable path fails fatally without changes") {
			Vector<String> paths;
			paths.push_back("res://refactor/fixpoint_does_not_exist.gd");

			FixpointInferenceResult result = GDScriptFixpointInference::run(paths);
			CHECK_FALSE(result.ok);
			CHECK_FALSE(result.error_message.is_empty());
			CHECK_EQ(result.changed_files.size(), 0);
		}

		SUBCASE("an unanalyzable file does not abort the batch") {
			const String path_broken = "res://refactor/fixpoint_broken.gd";
			const String path_good = "res://refactor/fixpoint_good_leaf.gd";

			// Malformed function signature: fails to parse, so analysis never succeeds.
			const String source_broken = "func broken( ->:\n\tpass\n";
			const String source_good = "static func value():\n\treturn 42\n";

			TemporaryScriptFile file_broken(path_broken, source_broken);
			TemporaryScriptFile file_good(path_good, source_good);

			Vector<String> paths;
			paths.push_back(path_broken);
			paths.push_back(path_good);

			FixpointInferenceResult result = GDScriptFixpointInference::run(paths);
			REQUIRE(result.ok);

			// The valid leaf is still typed despite the broken sibling.
			CHECK(FileAccess::get_file_as_string(path_good).contains("static func value() -> int:"));
		}

		SUBCASE("an unprovable declaration is reported once in skipped") {
			const String path_unprovable = "res://refactor/fixpoint_unprovable.gd";
			const String path_good = "res://refactor/fixpoint_skip_good_leaf.gd";

			// A bare `var` with no initializer is matched but cannot have a type
			// inferred, so it yields a disabled Add-Type-Annotation candidate that
			// never gets applied. The leaf is independently typeable.
			const String source_unprovable = "var unprovable\n";
			const String source_good = "static func value():\n\treturn 42\n";

			TemporaryScriptFile file_unprovable(path_unprovable, source_unprovable);
			TemporaryScriptFile file_good(path_good, source_good);

			Vector<String> paths;
			paths.push_back(path_unprovable);
			paths.push_back(path_good);

			FixpointInferenceResult result = GDScriptFixpointInference::run(paths);
			REQUIRE(result.ok);

			// The typeable leaf is resolved.
			CHECK(FileAccess::get_file_as_string(path_good).contains("static func value() -> int:"));

			// The unprovable file is reported, exactly once, and the good leaf is not
			// listed because it was successfully applied.
			int unprovable_skips = 0;
			int good_skips = 0;
			for (const FixpointSkipped &skipped : result.skipped) {
				if (skipped.path == path_unprovable) {
					unprovable_skips++;
				} else if (skipped.path == path_good) {
					good_skips++;
				}
			}
			CHECK_FALSE(result.skipped.is_empty());
			CHECK_EQ(unprovable_skips, 1);
			CHECK_EQ(good_skips, 0);
		}

		memdelete(protocol);
		memdelete(editor_file_system);
		GDScriptTests::finish_language();
	}
}

} // namespace GDScriptTests

#endif // !GDSCRIPT_NO_LSP

#endif // TOOLS_ENABLED
