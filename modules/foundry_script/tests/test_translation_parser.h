/**************************************************************************/
/*  test_translation_parser.h                                             */
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

#ifdef TOOLS_ENABLED

#include "../editor/fs_translation_parser_plugin.h"
#include "fs_temporary_project_tree.h"

#include "tests/test_macros.h"

namespace FSTests {

TEST_CASE("[Modules][FoundryScript] Translation extraction visits destructuring initializers") {
	TemporaryProjectTree tree("foundry_script_translation_parser");
	tree.write_file("destructure.fs", R"(func build() -> void:
	var (label, _) = (tr("Play"), 0)
	const (title, _) = (tr("Menu"), 1)
	print(label)
	print(title)
)");

	Ref<FSEditorTranslationParserPlugin> plugin;
	plugin.instantiate();

	Vector<Vector<String>> translations;
	// Loading a script from outside `res://` reports a compilation error; the resource still carries
	// its source code, which is all the parser plugin reads.
	ERR_PRINT_OFF;
	const Error parse_error = plugin->parse_file(tree.root.path_join("destructure.fs"), &translations);
	ERR_PRINT_ON;
	REQUIRE_EQ(parse_error, OK);

	Vector<String> extracted;
	for (int i = 0; i < translations.size(); i++) {
		REQUIRE_FALSE(translations[i].is_empty());
		extracted.push_back(translations[i][0]);
	}
	CHECK(extracted.has("Play"));
	CHECK(extracted.has("Menu"));
}

} // namespace FSTests

#endif // TOOLS_ENABLED
