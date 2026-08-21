/**************************************************************************/
/*  fs_type_completeness_union_adapter.h                                 */
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

#include "fs_type_completeness_graph.h"

#include "core/variant/variant.h"

namespace FSTests {

struct FSCompletenessProgram {
	String case_id;
	String surface;
	String source;
	String expected_output;
};

struct FSCompletenessObservation {
	String case_id;
	String surface;
	Dictionary dimensions;
	PackedStringArray diagnostics;
	String produced_output;
};

struct TemporaryProjectTree;

namespace UnionCompletenessInternal {

class SyntheticSourceScope {
	String path;
	TemporaryProjectTree *tree = nullptr;
	bool source_available = false;
	bool lock_held = false;

public:
	SyntheticSourceScope(const String &p_identity, const String &p_source);
	~SyntheticSourceScope();

	SyntheticSourceScope(const SyntheticSourceScope &) = delete;
	SyntheticSourceScope &operator=(const SyntheticSourceScope &) = delete;

	bool is_available() const { return source_available; }
	const String &get_path() const { return path; }
};

} // namespace UnionCompletenessInternal

class FSUnionCompletenessAdapter {
public:
	static Error render(const FSCompletenessResolvedCell &, FSCompletenessProgram &);
	static FSCompletenessObservation analyze(const FSCompletenessProgram &, const String &p_surface);
	static FSCompletenessObservation inspect_runtime_contract(const FSCompletenessProgram &, const Dictionary &);
	static Error witness_coordinates(const String &, Dictionary &);
};

} // namespace FSTests
