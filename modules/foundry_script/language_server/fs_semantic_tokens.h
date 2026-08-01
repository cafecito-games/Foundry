/**************************************************************************/
/*  fs_semantic_tokens.h                                                  */
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

#include "foundry_lsp.h"

#include "core/string/ustring.h"
#include "core/templates/vector.h"
#include "core/variant/variant.h"

/**
 * Encoding side of the `textDocument/semanticTokens/full` request.
 *
 * Producers hand this class source-ordered classified spans expressed in Foundry Script source
 * coordinates; the class turns them into the flat five-integer delta records the protocol
 * transmits and is the single place that converts source columns into UTF-16 code units.
 */
class FSSemanticTokens {
public:
	/**
	 * A classified region of source text.
	 *
	 * `line` is zero-based. `start_column` and `length` are zero-based counts of Unicode code
	 * points, matching how `String` indexes text, so producers never have to reason about
	 * surrogate pairs or tab expansion. Spans are single-line by contract: a construct spanning
	 * several lines must be emitted as one span per line.
	 */
	struct Span {
		int line = 0;
		int start_column = 0;
		int length = 0;
		LSP::SemanticTokenType type = LSP::SemanticTokenType::KEYWORD;
		uint32_t modifiers = 0;
	};

	/** The `tokenModifiers` bit for a legend modifier. */
	static uint32_t modifier_bit(LSP::SemanticTokenModifier p_modifier);

	/** The number of UTF-16 code units `p_text` encodes to. */
	static int utf16_length(const String &p_text);

	/**
	 * Converts a zero-based code point column on `p_line` into a zero-based UTF-16 code unit
	 * column. Columns past the end of the line clamp to the line's UTF-16 length.
	 */
	static int code_point_column_to_utf16_column(const String &p_line, int p_code_point_column);

	/**
	 * Encodes source-sorted spans into five-integer delta records
	 * (`deltaLine`, `deltaStart`, `length`, `tokenType`, `tokenModifiers`).
	 *
	 * Spans that fall outside `p_lines`, carry a non-positive length, name a type outside the
	 * legend, or regress behind the previously emitted span are dropped rather than emitted, so a
	 * partially classified or malformed document still yields a well-formed stream. `deltaStart`
	 * and `length` count UTF-16 code units, as the protocol requires.
	 */
	static PackedInt32Array encode(const Vector<Span> &p_spans, const Vector<String> &p_lines);

	/**
	 * Splits `p_source` the way the encoder expects to receive it, preserving a trailing empty
	 * line so a document ending in a newline still has a final (empty) line to index.
	 */
	static Vector<String> split_lines(const String &p_source);

	/**
	 * Collects the spans for a document.
	 *
	 * This transport-level pass is purely lexical: it marks reserved words so the request has
	 * observable output end to end. Symbol and contextual classification (types, members,
	 * parameters, annotations, and the modifier bits that go with them) is a separate concern and
	 * extends this producer rather than replacing the encoder.
	 */
	static Vector<Span> collect(const String &p_source, const Vector<String> &p_lines);
};
