/**************************************************************************/
/*  test_extract_method_name_prompt.h                                     */
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

#include "editor/script/extract_method_name_prompt.h"
#include "tests/test_macros.h"

namespace TestExtractMethodNamePrompt {

RefactorLocation extract_selection() {
	RefactorLocation loc;
	loc.start_line = 1;
	loc.start_column = 0;
	loc.end_line = 2;
	loc.end_column = 0;
	return loc;
}

Vector<String> member_names(const String &p_name) {
	Vector<String> names;
	names.push_back(p_name);
	return names;
}

TEST_CASE("[Editor][ExtractMethodNamePrompt] Accepts valid names") {
	const RefactorLocation selection = extract_selection();
	ExtractMethodNamePromptModel model;
	model.begin(selection, Vector<String>(), "_extracted_method");

	CHECK(model.has_pending_request());
	CHECK(model.is_valid());
	CHECK_EQ(model.get_name(), "_extracted_method");
	CHECK(model.get_error_message().is_empty());

	const RefactorLocation &stored_location = model.get_location();
	CHECK_EQ(stored_location.start_line, selection.start_line);
	CHECK_EQ(stored_location.start_column, selection.start_column);
	CHECK_EQ(stored_location.end_line, selection.end_line);
	CHECK_EQ(stored_location.end_column, selection.end_column);

	model.set_name("_print_ready");
	CHECK(model.is_valid());

	String confirmed;
	CHECK(model.confirm(confirmed));
	CHECK_EQ(confirmed, "_print_ready");
}

TEST_CASE("[Editor][ExtractMethodNamePrompt] Rejects invalid identifiers") {
	ExtractMethodNamePromptModel model;
	model.begin(extract_selection(), Vector<String>(), "_extracted_method");
	model.set_name("1bad");

	CHECK(model.has_pending_request());
	CHECK_FALSE(model.is_valid());
	CHECK(model.get_error_message().contains("valid identifier"));

	String confirmed;
	CHECK_FALSE(model.confirm(confirmed));
	CHECK(confirmed.is_empty());

	model.set_name("_ok");

	CHECK(model.is_valid());
	CHECK(model.get_error_message().is_empty());
	CHECK(model.confirm(confirmed));
	CHECK_EQ(confirmed, "_ok");
}

TEST_CASE("[Editor][ExtractMethodNamePrompt] Rejects reserved keywords") {
	ExtractMethodNamePromptModel model;
	model.begin(extract_selection(), Vector<String>(), "_extracted_method");
	model.set_name("class");

	CHECK(model.has_pending_request());
	CHECK_FALSE(model.is_valid());
	CHECK(model.get_error_message().contains("reserved keyword"));

	String confirmed;
	CHECK_FALSE(model.confirm(confirmed));
	CHECK(confirmed.is_empty());
}

TEST_CASE("[Editor][ExtractMethodNamePrompt] Rejects empty names") {
	ExtractMethodNamePromptModel model;
	model.begin(extract_selection(), Vector<String>(), "_extracted_method");
	model.set_name("");

	CHECK(model.has_pending_request());
	CHECK_FALSE(model.is_valid());
	CHECK_EQ(model.get_error_message(), "Name cannot be empty.");

	String confirmed;
	CHECK_FALSE(model.confirm(confirmed));
	CHECK(confirmed.is_empty());
}

TEST_CASE("[Editor][ExtractMethodNamePrompt] Rejects collisions in the target inner class") {
	RefactorLocation loc;
	loc.start_line = 2;
	loc.start_column = 0;
	loc.end_line = 3;
	loc.end_column = 0;

	ExtractMethodNamePromptModel model;
	model.begin(loc, member_names("existing"), "_extracted_method");
	model.set_name("existing");

	CHECK(model.has_pending_request());
	CHECK_FALSE(model.is_valid());
	CHECK(model.get_error_message().contains("already exists"));

	String confirmed;
	CHECK_FALSE(model.confirm(confirmed));
	CHECK(confirmed.is_empty());
}

TEST_CASE("[Editor][ExtractMethodNamePrompt] Rejects member collisions") {
	ExtractMethodNamePromptModel model;
	model.begin(extract_selection(), member_names("existing"), "_extracted_method");
	model.set_name("existing");

	CHECK(model.has_pending_request());
	CHECK_FALSE(model.is_valid());
	CHECK(model.get_error_message().contains("already exists"));

	String confirmed;
	CHECK_FALSE(model.confirm(confirmed));
	CHECK(confirmed.is_empty());
}

TEST_CASE("[Editor][ExtractMethodNamePrompt] Cancel clears pending state") {
	ExtractMethodNamePromptModel model;
	model.begin(extract_selection(), Vector<String>(), "_extracted_method");
	REQUIRE(model.has_pending_request());

	model.cancel();

	CHECK_FALSE(model.has_pending_request());
	CHECK_FALSE(model.is_valid());
	CHECK(model.get_name().is_empty());
	CHECK(model.get_error_message().is_empty());

	String confirmed;
	CHECK_FALSE(model.confirm(confirmed));
}

} // namespace TestExtractMethodNamePrompt

#endif // TOOLS_ENABLED
