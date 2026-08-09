/**************************************************************************/
/*  test_doc_tools_xml.h                                                  */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             FOUNDRY ENGINE                             */
/*          A fork of the Godot Engine (https://godotengine.org)          */
/*                       https://www.cafecito.games                       */
/**************************************************************************/
/* Copyright (c) 2026-present Cafecito Games LLC.                         */
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

#include "editor/doc/doc_tools.h"

#include "tests/test_macros.h"

namespace TestDocToolsXml {

TEST_CASE("[Editor][DocToolsXml] self-closing tutorials preserve following methods") {
	static constexpr char xml[] = R"(<?xml version="1.0" encoding="UTF-8" ?>
<class name="EmptyTutorialsFixture" inherits="RefCounted">
	<brief_description>Fixture.</brief_description>
	<description>Fixture description.</description>
	<tutorials />
	<methods>
		<method name="after_tutorials">
			<return type="void" />
			<description>Still parsed.</description>
		</method>
	</methods>
</class>)";

	DocTools docs;
	REQUIRE_EQ(docs.load_xml(reinterpret_cast<const uint8_t *>(xml), sizeof(xml) - 1), OK);
	REQUIRE(docs.class_list.has("EmptyTutorialsFixture"));
	const DocData::ClassDoc &class_doc = docs.class_list["EmptyTutorialsFixture"];
	REQUIRE_EQ(class_doc.methods.size(), 1);
	if (class_doc.methods.size() == 1) {
		CHECK_EQ(class_doc.methods[0].name, "after_tutorials");
	}
}

} // namespace TestDocToolsXml

#endif // TOOLS_ENABLED
