/**************************************************************************/
/*  test_tokenizer_line_continuation.h                                    */
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

#include "../fs_tokenizer.h"

#include "core/string/string_builder.h"
#include "tests/test_macros.h"

TEST_CASE("[Modules][FoundryScript] FSTokenizer rejects excessive line continuations") {
	FSTokenizerText tokenizer;
	StringBuilder source;
	source.append("var x = 1");
	for (int i = 0; i <= Variant::MAX_RECURSION_DEPTH; i++) {
		source.append(" \\\n");
	}
	source.append("+ 2");
	tokenizer.set_source_code(source.as_string());

	bool saw_error = false;
	FSTokenizer::Token token = tokenizer.scan();
	while (token.type != FSTokenizer::Token::TK_EOF) {
		if (token.type == FSTokenizer::Token::ERROR) {
			saw_error = true;
			CHECK(String(token.literal).contains("Too many line continuations"));
			break;
		}
		token = tokenizer.scan();
	}
	CHECK(saw_error);
}
