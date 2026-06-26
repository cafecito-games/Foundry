/**************************************************************************/
/*  test_annotation_index.h                                               */
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

#include "modules/gdscript/gdscript.h"

#include "tests/test_macros.h"

namespace GDScriptTests {

// These tests exercise the cross-file custom annotation index directly so the
// duplicate-identity and annotation-only-namespace lookups stay correct
// independently of the analyzer that consumes them.

TEST_CASE("[Modules][GDScript] Annotation index resolves namespaces and duplicates") {
	GDScriptLanguage *language = GDScriptLanguage::get_singleton();
	REQUIRE(language != nullptr);

	language->clear_global_annotations();

	language->add_global_annotation(SNAME("cafecito.test.suite"), "res://library_a.gd");
	language->add_global_annotation(SNAME("cafecito.test.fixture"), "res://library_a.gd");

	CHECK(language->is_global_annotation(SNAME("cafecito.test.suite")));
	CHECK_FALSE(language->is_global_annotation(SNAME("cafecito.test.missing")));

	SUBCASE("Annotation-only namespaces are discoverable") {
		CHECK(language->namespace_has_annotations("cafecito.test"));
		// Parent namespaces are considered to exist, matching global-class prefix semantics.
		CHECK(language->namespace_has_annotations("cafecito"));
		// A name that is not a namespace prefix must not match.
		CHECK_FALSE(language->namespace_has_annotations("cafecito.testing"));
		CHECK_FALSE(language->namespace_has_annotations("other"));
		CHECK_FALSE(language->namespace_has_annotations(String()));
	}

	SUBCASE("A canonical identity in a single file is not a duplicate") {
		// Re-registering the same canonical from the same path keeps it single.
		language->add_global_annotation(SNAME("cafecito.test.suite"), "res://library_a.gd");
		CHECK_FALSE(language->is_duplicated_global_annotation(SNAME("cafecito.test.suite")));
	}

	SUBCASE("A canonical identity in two files is a duplicate") {
		language->add_global_annotation(SNAME("cafecito.test.suite"), "res://library_b.gd");
		CHECK(language->is_duplicated_global_annotation(SNAME("cafecito.test.suite")));

		// Removing one declaring file collapses the duplicate again.
		language->remove_global_annotations_by_path("res://library_b.gd");
		CHECK_FALSE(language->is_duplicated_global_annotation(SNAME("cafecito.test.suite")));
		CHECK(language->is_global_annotation(SNAME("cafecito.test.suite")));
	}

	SUBCASE("Clearing drops every indexed annotation") {
		language->clear_global_annotations();
		CHECK_FALSE(language->is_global_annotation(SNAME("cafecito.test.suite")));
		CHECK_FALSE(language->namespace_has_annotations("cafecito.test"));
	}

	language->clear_global_annotations();
}

} // namespace GDScriptTests
