/**************************************************************************/
/*  test_verification_harness.h                                           */
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

#include "../editor/gdscript_batch_candidates.h"
#include "../editor/gdscript_verification_harness.h"

#include "core/io/file_access.h"

#include "test_refactor.h" // GDScriptTests::TemporaryScriptFile, make_context

#ifndef GDSCRIPT_NO_LSP

#include "editor/file_system/editor_file_system.h"

namespace GDScriptTests {

// Collect enabled Add Type Annotation candidates for one file as VerificationCandidates.
static Vector<VerificationCandidate> enabled_candidates_for(const String &p_path) {
	Vector<VerificationCandidate> out;
	BatchCandidatesResult batch = GDScriptBatchCandidates::collect({ p_path });
	for (const BatchFileCandidates &file : batch.files) {
		for (const RefactorCandidate &candidate : file.candidates) {
			if (!candidate.enabled) {
				continue;
			}
			VerificationCandidate vc;
			vc.path = file.path;
			vc.line = candidate.line;
			vc.edits = candidate.edits;
			out.push_back(vc);
		}
	}
	return out;
}

TEST_SUITE("[Modules][GDScript][Verification]") {
	TEST_CASE("Independently-sound candidates are all accepted") {
		EditorFileSystem *editor_file_system = memnew(EditorFileSystem);
		GDScriptLanguageProtocol *protocol = GDScriptTests::initialize(GDScriptTests::root);
		REQUIRE(protocol);

		const String path = "res://refactor/verify_clean.gd";
		const String source =
				"func compute():\n"
				"\treturn inner()\n"
				"func inner():\n"
				"\treturn 42\n";
		TemporaryScriptFile file(path, source);

		Vector<VerificationCandidate> candidates = enabled_candidates_for(path);
		REQUIRE_GT(candidates.size(), 0);

		VerificationResult result = GDScriptVerificationHarness::verify(candidates, { path });
		REQUIRE(result.ok);
		CHECK_EQ(result.rejected.size(), 0);
		CHECK_EQ(result.accepted.size(), candidates.size());
		CHECK_EQ(result.accepted_error_count, result.baseline_error_count);

		// verify() must not modify files on disk.
		CHECK_EQ(FileAccess::get_file_as_string(path), source);

		memdelete(protocol);
		memdelete(editor_file_system);
		GDScriptTests::finish_language();
	}

	TEST_CASE("A candidate that breaks a dependent is rejected with a diagnostic") {
		EditorFileSystem *editor_file_system = memnew(EditorFileSystem);
		GDScriptLanguageProtocol *protocol = GDScriptTests::initialize(GDScriptTests::root);
		REQUIRE(protocol);

		// provider.gd: untyped getter whose inferred return type is int.
		const String provider_path = "res://refactor/verify_provider.gd";
		const String provider_source =
				"func get_value():\n"
				"\treturn 42\n";
		TemporaryScriptFile provider(provider_path, provider_source);

		// consumer.gd: uses preload to depend on the provider and assigns the getter's
		// result to a String. Before get_value() is typed, the call returns Variant so
		// the assignment is allowed. Once typed -> int the assignment is a hard error,
		// so the provider candidate must be rejected.
		const String consumer_path = "res://refactor/verify_consumer.gd";
		const String consumer_source =
				"const Provider = preload(\"res://refactor/verify_provider.gd\")\n"
				"func use() -> void:\n"
				"\tvar p: Provider = Provider.new()\n"
				"\tvar s: String = p.get_value()\n";
		TemporaryScriptFile consumer(consumer_path, consumer_source);

		Vector<VerificationCandidate> candidates = enabled_candidates_for(provider_path);
		REQUIRE_GT(candidates.size(), 0);

		Vector<String> universe = { provider_path, consumer_path };
		VerificationResult result = GDScriptVerificationHarness::verify(candidates, universe);
		REQUIRE(result.ok);
		CHECK_GT(result.rejected.size(), 0);
		CHECK_GT(result.rejected[0].diagnostics.size(), 0);

		// Files unchanged on disk.
		CHECK_EQ(FileAccess::get_file_as_string(provider_path), provider_source);
		CHECK_EQ(FileAccess::get_file_as_string(consumer_path), consumer_source);

		memdelete(protocol);
		memdelete(editor_file_system);
		GDScriptTests::finish_language();
	}
}

} // namespace GDScriptTests

#endif // GDSCRIPT_NO_LSP

#endif // TOOLS_ENABLED
