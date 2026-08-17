/**************************************************************************/
/*  fs_fixture_cli.h                                                      */
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

#include "core/string/ustring.h"
#include "core/templates/vector.h"
#include "core/variant/dictionary.h"

namespace FSTests {

// `foundry test fixtures` entry point: runs a named slice of the `.fs` fixture corpus and
// reports the outcome as data. The same corpus runs inside a single doctest case, which can
// only be selected as a whole, so this is the addressable path to one fixture.
class FSFixtureCLI {
public:
	// Which corpus passes to execute. The doctest suite runs the plain pass and the
	// compiled-bytecode round-trip as two separate cases; PASS_ALL reproduces both.
	enum PassSelection {
		PASS_ALL,
		PASS_TEXT,
		PASS_BYTECODE,
	};

	struct Options {
		String corpus_dir;
		// Corpus-relative glob or substring patterns; empty runs the whole corpus.
		Vector<String> patterns;
		// Pure-JSON report destination. Empty prints the report to stdout instead, which
		// the engine's own startup output shares.
		String output_path;
		bool print_filenames = false;
		// Tokenize the plain pass from a token buffer, mirroring the binary-tokens CI run.
		bool binary_tokens = false;
		PassSelection passes = PASS_ALL;
	};

	// Runs the selection and returns the report. Owns the script language lifecycle for each
	// pass. `r_failed_count` counts fixture executions whose output did not match expected
	// output, and is -1 when the corpus could not be collected at all.
	static Dictionary run(const Options &p_options, int &r_failed_count);

	// Process entry point: runs the selection, emits the report, and returns the exit code.
	// A selection that matches no fixture is a scoping mistake, not an empty success, so it
	// fails the same way an unmatched `test run --case` pattern does.
	static int run_cli(const Options &p_options);
};

} // namespace FSTests
