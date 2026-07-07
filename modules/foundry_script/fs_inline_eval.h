/**************************************************************************/
/*  fs_inline_eval.h                                                      */
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

#include "core/object/ref_counted.h"
#include "core/string/ustring.h"

class ScriptRunner;

// Compiles a short inline Foundry Script snippet into a runnable ScriptRunner
// for `foundry script eval`. The snippet is wrapped in a generated runner whose
// `run(args)` body is the snippet, so the existing ScriptRunner host provides the
// SceneTree main loop, async completion, and integer exit-code contract. No new
// grammar mode is introduced and no top-level executable-statement semantics.
class FSInlineEval {
public:
	// Synthetic in-memory source path used as the cache key and diagnostic label
	// for the generated runner. The source never touches disk.
	static String synthetic_source_path();

	// Wraps the user snippet in the generated runner source. Public so the exact
	// wrapping (indentation, injected default `return 0`) is unit-testable.
	static String build_runner_source(const String &p_user_source);

	// Compiles the generated runner. Returns a null Ref and fills r_error on
	// parse/analyze/compile failure or when the snippet does not compile to a
	// runnable ScriptRunner.
	static Ref<ScriptRunner> compile_runner(const String &p_user_source, String &r_error);
};
