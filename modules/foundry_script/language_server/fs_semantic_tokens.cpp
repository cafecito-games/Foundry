/**************************************************************************/
/*  fs_semantic_tokens.cpp                                                */
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

#include "fs_semantic_tokens.h"

#include "modules/foundry_script/fs_position.h"
#include "modules/foundry_script/fs_tokenizer.h"

namespace {

// The reserved words of the language, as spelled in `GRAMMAR.md` section 2.5.
//
// Two groups are deliberately excluded because a purely lexical pass cannot tell them apart from
// ordinary names, and emitting a wrong classification is worse than emitting none:
//   - the numeric keyword constants (`PI`, `TAU`, `INF`, `NAN`), which are values;
//   - `match`, `when`, and `uses`, which `Token::is_identifier()` accepts wherever an identifier is
//     expected, so `var match = 1` declares a variable rather than opening a match statement.
bool is_reserved_word(FSTokenizer::Token::Type p_type) {
	switch (p_type) {
		case FSTokenizer::Token::AND:
		case FSTokenizer::Token::OR:
		case FSTokenizer::Token::NOT:
		case FSTokenizer::Token::IF:
		case FSTokenizer::Token::ELIF:
		case FSTokenizer::Token::ELSE:
		case FSTokenizer::Token::FOR:
		case FSTokenizer::Token::WHILE:
		case FSTokenizer::Token::BREAK:
		case FSTokenizer::Token::CONTINUE:
		case FSTokenizer::Token::PASS:
		case FSTokenizer::Token::RETURN:
		case FSTokenizer::Token::ABSTRACT:
		case FSTokenizer::Token::AS:
		case FSTokenizer::Token::ASSERT:
		case FSTokenizer::Token::AWAIT:
		case FSTokenizer::Token::BREAKPOINT:
		case FSTokenizer::Token::CLASS:
		case FSTokenizer::Token::CLASS_NAME:
		case FSTokenizer::Token::ENUM_NAME:
		case FSTokenizer::Token::TK_CONST:
		case FSTokenizer::Token::ENUM:
		case FSTokenizer::Token::EXTENDS:
		case FSTokenizer::Token::FINAL:
		case FSTokenizer::Token::FUNC:
		case FSTokenizer::Token::IMPORT:
		case FSTokenizer::Token::TK_IN:
		case FSTokenizer::Token::IS:
		case FSTokenizer::Token::NAMESPACE:
		case FSTokenizer::Token::PRELOAD:
		case FSTokenizer::Token::SELF:
		case FSTokenizer::Token::SIGNAL:
		case FSTokenizer::Token::STATIC:
		case FSTokenizer::Token::SUPER:
		case FSTokenizer::Token::TRAIT:
		case FSTokenizer::Token::TRAIT_NAME:
		case FSTokenizer::Token::TUPLE:
		case FSTokenizer::Token::TUPLE_NAME:
		case FSTokenizer::Token::VAR:
		case FSTokenizer::Token::TK_VOID:
		case FSTokenizer::Token::YIELD:
			return true;
		default:
			return false;
	}
}

} // namespace

uint32_t FSSemanticTokens::modifier_bit(LSP::SemanticTokenModifier p_modifier) {
	ERR_FAIL_COND_V(p_modifier >= LSP::SemanticTokenModifier::MAX, 0);
	return 1u << static_cast<uint32_t>(p_modifier);
}

int FSSemanticTokens::utf16_length(const String &p_text) {
	int length = 0;
	for (int i = 0; i < p_text.length(); i++) {
		// Code points outside the Basic Multilingual Plane encode as a surrogate pair.
		length += p_text[i] > char32_t(0xFFFF) ? 2 : 1;
	}
	return length;
}

int FSSemanticTokens::code_point_column_to_utf16_column(const String &p_line, int p_code_point_column) {
	const int limit = MIN(p_code_point_column, p_line.length());
	int column = 0;
	for (int i = 0; i < limit; i++) {
		column += p_line[i] > char32_t(0xFFFF) ? 2 : 1;
	}
	return column;
}

PackedInt32Array FSSemanticTokens::encode(const Vector<Span> &p_spans, const Vector<String> &p_lines) {
	PackedInt32Array data;

	bool has_previous = false;
	int previous_line = 0;
	int previous_utf16_start = 0;
	// Tracked in source coordinates so overlap is rejected before any UTF-16 conversion happens.
	int previous_end_column = 0;

	for (const Span &span : p_spans) {
		if (span.line < 0 || span.line >= p_lines.size()) {
			continue;
		}
		if (span.start_column < 0 || span.length <= 0) {
			continue;
		}
		if (span.type >= LSP::SemanticTokenType::MAX) {
			continue;
		}

		const String &line = p_lines[span.line];
		if (span.start_column >= line.length()) {
			continue;
		}
		// Clamp rather than drop: a span whose producer over-measured the tail of a line still
		// describes a real token, it just cannot extend past the line it lives on.
		const int end_column = MIN(span.start_column + span.length, line.length());

		// Emitting a span that starts before the previous one ended would break the sorted,
		// non-overlapping guarantee the delta encoding depends on, and `deltaStart` would go
		// negative. Drop the offender instead of corrupting the whole stream.
		if (has_previous && span.line < previous_line) {
			continue;
		}
		if (has_previous && span.line == previous_line && span.start_column < previous_end_column) {
			continue;
		}

		const int utf16_start = code_point_column_to_utf16_column(line, span.start_column);
		const int utf16_end = code_point_column_to_utf16_column(line, end_column);

		const int delta_line = has_previous ? span.line - previous_line : span.line;
		const int delta_start = (has_previous && delta_line == 0) ? utf16_start - previous_utf16_start : utf16_start;

		data.push_back(delta_line);
		data.push_back(delta_start);
		data.push_back(utf16_end - utf16_start);
		data.push_back(static_cast<int>(span.type));
		data.push_back(static_cast<int>(span.modifiers));

		has_previous = true;
		previous_line = span.line;
		previous_utf16_start = utf16_start;
		previous_end_column = end_column;
	}

	return data;
}

Vector<String> FSSemanticTokens::split_lines(const String &p_source) {
	if (p_source.is_empty()) {
		return Vector<String>();
	}
	return p_source.split("\n", true);
}

Vector<FSSemanticTokens::Span> FSSemanticTokens::collect(const String &p_source, const Vector<String> &p_lines) {
	Vector<Span> spans;
	if (p_source.is_empty()) {
		return spans;
	}

	FSTokenizerText tokenizer;
	tokenizer.set_source_code(p_source);

	// The tokenizer synthesizes layout tokens (`NEWLINE`, `INDENT`, `DEDENT`) and error tokens that
	// consume no source, so token count is not bounded by source length alone. Cap the scan so a
	// pathological or truncated document cannot spin here forever.
	const int scan_limit = p_source.length() * 4 + 64;
	bool previous_was_period = false;
	bool in_node_path = false;

	for (int scanned = 0; scanned < scan_limit; scanned++) {
		const FSTokenizer::Token token = tokenizer.scan();
		if (token.type == FSTokenizer::Token::TK_EOF) {
			break;
		}

		const bool after_period = previous_was_period;
		previous_was_period = token.type == FSTokenizer::Token::PERIOD;

		// A get-node path runs from `$` until the first token that cannot continue it, and every
		// segment inside it is a node name rather than a keyword (`GRAMMAR.md` section 2.5).
		const bool after_node_path_start = in_node_path;
		if (token.type == FSTokenizer::Token::DOLLAR) {
			in_node_path = true;
		} else if (!in_node_path ||
				!(token.type == FSTokenizer::Token::SLASH ||
						token.type == FSTokenizer::Token::PERCENT ||
						token.is_node_name())) {
			in_node_path = false;
		}

		if (!is_reserved_word(token.type)) {
			continue;
		}
		// `self.class`, `node.signal`: the parser re-spells a reserved word in attribute position as
		// an ordinary identifier, so highlighting it as a keyword would be plainly wrong.
		if (after_period) {
			continue;
		}
		if (after_node_path_start && token.is_node_name()) {
			continue;
		}
		// Reserved words never span lines; anything that claims to did not come from real source.
		if (token.start_line != token.end_line) {
			continue;
		}

		const int line_index = token.start_line - 1;
		if (line_index < 0 || line_index >= p_lines.size()) {
			continue;
		}

		// Tokenizer lines and columns are one-based, and its columns expand a tab to the editor's
		// indent size. `godot_column_to_text_column` is the inverse used everywhere else in the
		// language server, and it yields the code point column `encode()` consumes.
		const String &line = p_lines[line_index];
		const int start_column = FSTextPosition::godot_column_to_text_column(line, token.start_column);
		const int end_column = FSTextPosition::godot_column_to_text_column(line, token.end_column);

		Span span;
		span.line = line_index;
		span.start_column = start_column;
		span.length = end_column - start_column;
		span.type = LSP::SemanticTokenType::KEYWORD;
		spans.push_back(span);
	}

	return spans;
}
