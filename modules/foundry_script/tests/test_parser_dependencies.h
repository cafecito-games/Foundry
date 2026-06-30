/**************************************************************************/
/*  test_parser_dependencies.h                                            */
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

#include "../fs_parser.h"

#include "tests/test_macros.h"

static HashSet<String> _dependency_set(const FSParser &p_parser) {
	HashSet<String> dep_set;
	for (const String &dep : p_parser.get_dependencies()) {
		dep_set.insert(dep);
	}
	return dep_set;
}

TEST_CASE("[Modules][FoundryScript] FSParser get_dependencies collects extends and preload paths") {
	FSParser parser;
	const String source = R"(
extends "res://base.fs"

func test() -> void:
	var resource = preload("res://other.fs")
	pass
)";
	CHECK(parser.parse(source, "res://main.fs", false) == OK);

	HashSet<String> dep_set = _dependency_set(parser);
	CHECK(dep_set.has("res://base.fs"));
	CHECK(dep_set.has("res://other.fs"));
}

TEST_CASE("[Modules][FoundryScript] FSParser get_dependencies normalizes relative preload paths") {
	FSParser parser;
	const String source = R"(
func test() -> void:
	var resource = preload("../shared/helper.fs")
	pass
)";
	CHECK(parser.parse(source, "res://game/scripts/main.fs", false) == OK);

	HashSet<String> dep_set = _dependency_set(parser);
	CHECK(dep_set.has("res://game/shared/helper.fs"));
}

TEST_CASE("[Modules][FoundryScript] FSParser get_dependencies deduplicates repeated paths") {
	FSParser parser;
	const String source = R"(
extends "res://base.fs"

func test() -> void:
	var a = preload("res://base.fs")
	var b = preload("res://base.fs")
	pass
)";
	CHECK(parser.parse(source, "res://main.fs", false) == OK);

	HashSet<String> dep_set = _dependency_set(parser);
	CHECK(dep_set.size() == 1);
	CHECK(dep_set.has("res://base.fs"));
}

TEST_CASE("[Modules][FoundryScript] FSParser get_dependencies collects inner class extends paths") {
	FSParser parser;
	const String source = R"(
class Outer:
	class Inner extends "res://inner_base.fs":
		pass

func test() -> void:
	pass
)";
	CHECK(parser.parse(source, "res://main.fs", false) == OK);

	HashSet<String> dep_set = _dependency_set(parser);
	CHECK(dep_set.has("res://inner_base.fs"));
}
