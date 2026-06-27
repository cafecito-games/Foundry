/**************************************************************************/
/*  test_editor_help_type_links.h                                         */
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

#include "editor/doc/editor_help.h"

#include "tests/test_macros.h"

namespace TestEditorHelpTypeLinks {

// The ordered class-link targets the help renderer would emit for a doc type.
static Vector<String> class_link_targets(const String &p_type) {
	Vector<String> targets;
	for (const EditorHelp::HelpTypeRenderSegment &segment : EditorHelp::_build_type_render_segments(p_type, "", false, "")) {
		if (segment.kind == EditorHelp::HelpTypeRenderSegment::CLASS_LINK) {
			targets.push_back(segment.link);
		}
	}
	return targets;
}

// The concatenated visible text the help renderer would show for a doc type.
static String rendered_text(const String &p_type) {
	String text;
	for (const EditorHelp::HelpTypeRenderSegment &segment : EditorHelp::_build_type_render_segments(p_type, "", false, "")) {
		text += segment.text;
	}
	return text;
}

TEST_CASE("[Editor][EditorHelp] Top-level and array-of coroutine results link the result type") {
	// Regression coverage for the cases #342 already fixed.
	CHECK(class_link_targets("Coroutine[String]") == Vector<String>({ "String" }));
	CHECK(rendered_text("Coroutine[String]") == "Coroutine[String]");

	CHECK(class_link_targets("Coroutine[String][]") == Vector<String>({ "Array", "String" }));
	CHECK(rendered_text("Coroutine[String][]") == "Array[Coroutine[String]]");
}

TEST_CASE("[Editor][EditorHelp] Coroutine nested in a Dictionary value links the result type, not the synthetic class") {
	const Vector<String> targets = class_link_targets("Dictionary[String, Coroutine[int]]");
	CHECK(targets == Vector<String>({ "Dictionary", "String", "int" }));
	CHECK(rendered_text("Dictionary[String, Coroutine[int]]") == "Dictionary[String, Coroutine[int]]");
	// No link may target the pageless `Coroutine[...]` spelling.
	for (const String &target : targets) {
		CHECK(target.find("Coroutine") == -1);
		CHECK(target.find("[") == -1);
	}
}

TEST_CASE("[Editor][EditorHelp] Deeply nested coroutine arrays peel every array level") {
	CHECK(class_link_targets("Coroutine[String][][]") == Vector<String>({ "Array", "Array", "String" }));
	CHECK(rendered_text("Coroutine[String][][]") == "Array[Array[Coroutine[String]]]");
}

TEST_CASE("[Editor][EditorHelp] Nested non-coroutine generics link every leaf and split on the top-level comma") {
	// Pre-existing dead-link/mis-split behavior this fix also resolves. Docgen
	// spells typed arrays with the `T[]` suffix and typed dictionaries with the
	// `Dictionary[K, V]` prefix, so nested forms mix both spellings.

	// `Dictionary[String, Array[int]]` is spelled `Dictionary[String, int[]]`.
	CHECK(class_link_targets("Dictionary[String, int[]]") == Vector<String>({ "Dictionary", "String", "Array", "int" }));
	CHECK(rendered_text("Dictionary[String, int[]]") == "Dictionary[String, Array[int]]");

	// A dictionary key that is itself a dictionary must split on the top-level comma.
	CHECK(class_link_targets("Dictionary[Dictionary[int, String], bool]") == Vector<String>({ "Dictionary", "Dictionary", "int", "String", "bool" }));
	CHECK(rendered_text("Dictionary[Dictionary[int, String], bool]") == "Dictionary[Dictionary[int, String], bool]");

	// `Array[Array[int]]` is spelled `int[][]`; every array level must peel.
	CHECK(class_link_targets("int[][]") == Vector<String>({ "Array", "Array", "int" }));
	CHECK(rendered_text("int[][]") == "Array[Array[int]]");
}

TEST_CASE("[Editor][EditorHelp] Leaf and synthetic-alias types keep their existing link targets") {
	CHECK(class_link_targets("int") == Vector<String>({ "int" }));
	CHECK(rendered_text("int") == "int");

	// `AsyncCallable` is a synthetic spelling that links to the real `Callable` page.
	CHECK(class_link_targets("AsyncCallable") == Vector<String>({ "Callable" }));
	CHECK(rendered_text("AsyncCallable") == "AsyncCallable");
}

} // namespace TestEditorHelpTypeLinks

#endif // TOOLS_ENABLED
