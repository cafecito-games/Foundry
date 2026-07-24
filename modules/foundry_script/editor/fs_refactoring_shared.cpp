/**************************************************************************/
/*  fs_refactoring_shared.cpp                                             */
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

#include "fs_refactoring_shared.h"

#ifdef TOOLS_ENABLED

#include "fs_refactoring_edits.h"

#include "../fs_position.h"

#include "core/string/char_utils.h"

#ifndef FOUNDRY_SCRIPT_NO_LSP
#include "../language_server/foundry_lsp.h"
#include "../language_server/fs_extend_parser.h"
#include "../language_server/fs_language_protocol.h"
#endif // FOUNDRY_SCRIPT_NO_LSP

namespace FSRefactorShared {

#ifndef FOUNDRY_SCRIPT_NO_LSP
void RefactorParseResultProvider::parse_source(const String &p_path, const String &p_source) const {
	if (!p_path.has_extension("fs")) {
		return;
	}

	parse_results[p_path] = ExtendFSParser::parse_source(p_source, p_path);
}

RefactorParseResultProvider::RefactorParseResultProvider(const RefactorContext &p_context) {
	parse_source(p_context.path, p_context.source);
}

RefactorParseResultProvider::~RefactorParseResultProvider() {
	for (KeyValue<String, ExtendFSParser *> &E : parse_results) {
		memdelete(E.value);
	}
}

const ExtendFSParser *RefactorParseResultProvider::get_parse_result(const String &p_path) const {
	ExtendFSParser **existing = parse_results.getptr(p_path);
	if (existing != nullptr) {
		return *existing;
	}

	// Cross-file refactors intentionally parse non-active scripts from disk
	// instead of consulting connected clients' unsaved buffers or the shared
	// protocol cache.
	ExtendFSParser *parser = ExtendFSParser::parse_file(p_path);
	if (parser != nullptr) {
		parse_results[p_path] = parser;
	}
	return parser;
}

const ExtendFSParser *RefactorParseResultProvider::peek_parse_result(const String &p_path) const {
	ExtendFSParser **existing = parse_results.getptr(p_path);
	return existing != nullptr ? *existing : nullptr;
}

bool can_use_refactor_parse_results() {
	FSLanguageProtocol *protocol = FSLanguageProtocol::get_singleton();
	return protocol != nullptr && protocol->get_workspace().is_valid();
}

RefactorParseResultProviderScope::RefactorParseResultProviderScope(const RefactorContext &p_context) {
	if (can_use_refactor_parse_results()) {
		parse_results = memnew(RefactorParseResultProvider(p_context));
	}
}

RefactorParseResultProviderScope::~RefactorParseResultProviderScope() {
	if (parse_results != nullptr) {
		memdelete(parse_results);
	}
}

const FSParseResultProvider *RefactorParseResultProviderScope::get() const {
	return parse_results;
}
#endif // FOUNDRY_SCRIPT_NO_LSP

bool same_location(const RefactorLocation &p_a, const RefactorLocation &p_b) {
	return p_a.start_line == p_b.start_line &&
			p_a.start_column == p_b.start_column &&
			p_a.end_line == p_b.end_line &&
			p_a.end_column == p_b.end_column;
}

bool caret_on_segment(const RefactorLocation &p_location, int p_line, int p_start_column, int p_end_column) {
	if (p_location.has_selection() || p_location.start_line != p_line) {
		return false;
	}
	if (p_end_column < p_start_column) {
		p_end_column = p_start_column;
	}
	return p_location.start_column >= p_start_column && p_location.start_column <= p_end_column;
}

// Multi-line counterpart of caret_on_segment for a span that opens at
// (p_start_line, p_start_column) and closes on a later (p_end_line,
// p_end_column). A caret on an interior line matches at any column; on the
// boundary lines it must be at or past the opening column / at or before the
// closing column.
bool caret_in_multiline_span(const RefactorLocation &p_location, int p_start_line, int p_start_column, int p_end_line, int p_end_column) {
	if (p_location.has_selection()) {
		return false;
	}
	const int caret_line = p_location.start_line;
	if (caret_line < p_start_line || caret_line > p_end_line) {
		return false;
	}
	if (caret_line == p_start_line) {
		return p_location.start_column >= p_start_column;
	}
	if (caret_line == p_end_line) {
		return p_location.start_column <= p_end_column;
	}
	return true;
}

RefactorFileEdit *find_or_add_file_edit(Vector<RefactorFileEdit> &r_file_edits, const String &p_path) {
	for (RefactorFileEdit &file_edit : r_file_edits) {
		if (file_edit.path == p_path) {
			return &file_edit;
		}
	}

	RefactorFileEdit file_edit;
	file_edit.path = p_path;
	r_file_edits.push_back(file_edit);
	// The returned pointer is for immediate use only; a later push_back can move the Vector storage.
	return &r_file_edits.write[r_file_edits.size() - 1];
}

void append_warning(RefactorResult &r_result, const String &p_warning) {
	if (!r_result.warning.is_empty()) {
		r_result.warning += " ";
	}
	r_result.warning += p_warning;
}

int skip_string_literal(const String &p_line, int p_quote_column) {
	const char32_t quote = p_line[p_quote_column];
	bool escaped = false;
	for (int i = p_quote_column + 1; i < p_line.length(); i++) {
		const char32_t c = p_line[i];
		if (escaped) {
			escaped = false;
			continue;
		}
		if (c == '\\') {
			escaped = true;
			continue;
		}
		if (c == quote) {
			return i + 1;
		}
	}
	return p_line.length();
}

bool is_column_inside_string_literal(const String &p_line, int p_column) {
	if (p_column < 0 || p_column >= p_line.length()) {
		return false;
	}

	for (int i = 0; i < p_line.length(); i++) {
		const char32_t c = p_line[i];
		if (c == '#') {
			return false;
		}
		if (c != '"' && c != '\'') {
			continue;
		}

		const int end = skip_string_literal(p_line, i);
		if (p_column >= i && p_column < end) {
			return true;
		}
		i = end - 1;
	}

	return false;
}

// Locates the colon that terminates a function signature: the first top-level
// ':' that is not inside the parameter list, a bracketed/braced type or default
// value, a string literal, or a trailing comment. The scan starts at
// p_search_start on p_start_line and continues across the following lines up to
// and including p_last_line, so wrapped (multi-line) signatures are handled the
// same way as single-line ones. Bracket depth and triple-quoted (multi-line)
// string state both carry across line breaks, so a colon is only treated as the
// body colon when every parameter-list and container delimiter opened so far has
// been closed and the scan is outside any string. Returns true and writes the
// colon position when found; '#' ends the scan of the current line at a comment.
bool find_function_signature_colon(const Vector<String> &p_lines, int p_start_line, int p_search_start, int p_last_line, int &r_line, int &r_column) {
	int depth = 0;
	bool in_multiline_string = false;
	char32_t multiline_string_quote = 0; // The quote character that opened the active triple-quoted string.
	const int last_line = MIN(p_last_line, p_lines.size() - 1);
	for (int line_index = p_start_line; line_index <= last_line; line_index++) {
		const String &line = p_lines[line_index];
		int i = line_index == p_start_line ? (p_search_start < 0 ? 0 : p_search_start) : 0;
		for (; i < line.length(); i++) {
			const char32_t c = line[i];
			if (in_multiline_string) {
				if (c == '\\') {
					i++; // Skip the escaped character so an escaped quote never closes the string.
					continue;
				}
				if (c == multiline_string_quote && i + 2 < line.length() && line[i + 1] == multiline_string_quote && line[i + 2] == multiline_string_quote) {
					in_multiline_string = false;
					i += 2; // Skip the closing triple quote.
				}
				continue;
			}
			if (c == '#') {
				break;
			}
			if (c == '"' || c == '\'') {
				if (i + 2 < line.length() && line[i + 1] == c && line[i + 2] == c) {
					// A triple-quoted string may stay open past the end of the line.
					in_multiline_string = true;
					multiline_string_quote = c;
					i += 2; // Skip the opening triple quote; the body is consumed in string state.
					continue;
				}
				i = skip_string_literal(line, i) - 1;
				continue;
			}
			if (c == '(' || c == '[' || c == '{') {
				depth++;
				continue;
			}
			if (c == ')' || c == ']' || c == '}') {
				if (depth > 0) {
					depth--;
				}
				continue;
			}
			if (c == ':' && depth == 0) {
				r_line = line_index;
				r_column = i;
				return true;
			}
		}
	}
	return false;
}

bool is_identifier_boundary(const String &p_line, int p_start, int p_end) {
	if (p_start > 0 && is_unicode_identifier_continue(p_line[p_start - 1])) {
		return false;
	}
	if (p_end < p_line.length() && is_unicode_identifier_continue(p_line[p_end])) {
		return false;
	}
	return true;
}

bool find_identifier_on_line(const Vector<String> &p_lines, int p_line, const StringName &p_name, const RefactorLocation *p_location, int &r_start, int &r_end) {
	if (p_line < 0 || p_line >= p_lines.size()) {
		return false;
	}
	const String line = p_lines[p_line];
	const String name = String(p_name);
	if (name.is_empty()) {
		return false;
	}
	int from = 0;
	while (from <= line.length()) {
		const int position = line.find(name, from);
		if (position < 0) {
			return false;
		}
		const int end_position = position + name.length();
		if (is_identifier_boundary(line, position, end_position) && (p_location == nullptr || caret_on_segment(*p_location, p_line, position, end_position))) {
			r_start = position;
			r_end = end_position;
			return true;
		}
		from = end_position;
	}
	return false;
}

bool get_identifier_text_span(const Vector<String> &p_lines, int p_line, const FSParser::IdentifierNode *p_identifier, const RefactorLocation *p_location, int &r_start, int &r_end) {
	if (p_identifier == nullptr) {
		return false;
	}
	if (p_identifier->start_line == p_line + 1 && p_identifier->start_column > 0 && p_identifier->end_column > p_identifier->start_column) {
		const String line = p_lines[p_line];
		const String name = String(p_identifier->name);
		const int start = FSTextPosition::godot_column_to_text_column(line, p_identifier->start_column);
		const int end = FSTextPosition::godot_column_to_text_column(line, p_identifier->end_column);
		if (start >= 0 && end <= line.length() && end > start && line.substr(start, end - start) == name) {
			r_start = start;
			r_end = end;
			return true;
		}
	}
	return find_identifier_on_line(p_lines, p_line, p_identifier->name, p_location, r_start, r_end);
}

bool get_node_text_start(const Vector<String> &p_lines, int p_line, const FSParser::Node *p_node, const String &p_expected_text, int &r_start) {
	if (p_node == nullptr || p_node->start_line != p_line + 1 || p_node->start_column <= 0 || p_line < 0 || p_line >= p_lines.size()) {
		return false;
	}
	const String line = p_lines[p_line];
	const int start = FSTextPosition::godot_column_to_text_column(line, p_node->start_column);
	if (start < 0 || start + p_expected_text.length() > line.length()) {
		return false;
	}
	if (line.substr(start, p_expected_text.length()) != p_expected_text) {
		return false;
	}
	r_start = start;
	return true;
}

int find_assignment_rhs_start(const String &p_line, int p_equal_index) {
	int rhs_start = p_equal_index + 1;
	while (rhs_start < p_line.length() && is_whitespace(p_line[rhs_start])) {
		rhs_start++;
	}
	return rhs_start;
}

// Locates a declaration's assignment operator (`=` or the `=` of `:=`) starting
// at p_search_start on p_start_line and following continuation lines. A line
// continues onto the next through an explicit trailing backslash, while a
// bracket/brace/parenthesis remains open, or inside a multi-line string; after a
// backslash the tokenizer also skips whitespace- and comment-only lines, so this
// scan does too. The scan is string/comment/bracket-aware so an `=` inside a
// string, comment, or nested container is never mistaken for the assignment
// operator. Returns true and writes the `=` position when found, along with
// whether it is the `=` of an inferred declaration (`:=`, where the colon may be
// separated from the `=` by whitespace or a line break, e.g. `: =`).
bool find_declaration_assignment_operator(const Vector<String> &p_lines, int p_start_line, int p_search_start, int &r_line, int &r_column, bool &r_is_inferred, int &r_colon_line, int &r_colon_column) {
	int depth = 0;
	bool in_multiline_string = false;
	char32_t multiline_string_quote = 0; // The quote character that opened the active triple-quoted string.
	bool backslash_continuation = false; // A prior line ended with `\`, so blank/comment lines keep the declaration open.
	int colon_line = -1; // Position of the most recent colon, so an inferred `: =` can drop it.
	int colon_column = -1;
	bool last_meaningful_was_colon = false; // The last non-whitespace token character before the cursor was a colon.
	for (int line_index = p_start_line; line_index < p_lines.size(); line_index++) {
		const String &line = p_lines[line_index];
		int i = line_index == p_start_line ? (p_search_start < 0 ? 0 : p_search_start) : 0;
		bool explicit_continuation = false;
		bool saw_token = false; // This physical line carried a real token (not just whitespace or a comment).
		for (; i < line.length(); i++) {
			const char32_t c = line[i];
			if (in_multiline_string) {
				if (c == '\\') {
					i++; // Skip the escaped character so an escaped quote never closes the string.
					continue;
				}
				if (c == multiline_string_quote && i + 2 < line.length() && line[i + 1] == multiline_string_quote && line[i + 2] == multiline_string_quote) {
					in_multiline_string = false;
					i += 2; // Skip the closing triple quote.
				}
				continue;
			}
			if (c == '#') {
				break;
			}
			if (is_whitespace(c)) {
				continue;
			}
			if (c == '\\' && i == line.length() - 1) {
				// A trailing backslash continues the declaration onto the next line.
				explicit_continuation = true;
				continue;
			}
			saw_token = true;
			if (c == '"' || c == '\'') {
				if (i + 2 < line.length() && line[i + 1] == c && line[i + 2] == c) {
					// A triple-quoted string may stay open past the end of the line.
					in_multiline_string = true;
					multiline_string_quote = c;
					i += 2; // Skip the opening triple quote; the body is consumed in string state.
					last_meaningful_was_colon = false;
					continue;
				}
				i = skip_string_literal(line, i) - 1;
				last_meaningful_was_colon = false;
				continue;
			}
			if (c == '(' || c == '[' || c == '{') {
				depth++;
				last_meaningful_was_colon = false;
				continue;
			}
			if (c == ')' || c == ']' || c == '}') {
				if (depth > 0) {
					depth--;
				}
				last_meaningful_was_colon = false;
				continue;
			}
			if (c == '=' && depth == 0) {
				// Compound assignment operators (`==`, `<=`, etc.) cannot open a
				// declaration's initializer, so the first top-level `=` is the
				// assignment operator. A preceding colon (`:=` or `: =`) marks an
				// inferred declaration.
				r_line = line_index;
				r_column = i;
				r_is_inferred = last_meaningful_was_colon;
				r_colon_line = colon_line;
				r_colon_column = colon_column;
				return true;
			}
			last_meaningful_was_colon = c == ':';
			if (last_meaningful_was_colon) {
				colon_line = line_index;
				colon_column = i;
			}
		}
		if (in_multiline_string) {
			continue; // A triple-quoted string carries the declaration onto the next line.
		}
		if (explicit_continuation) {
			backslash_continuation = true;
		} else if (saw_token) {
			// A physical line with a real token but no trailing backslash only
			// continues while a bracket pair is still open.
			backslash_continuation = false;
		}
		if (!backslash_continuation && depth == 0) {
			// No continuation: the declaration ends without an assignment operator
			// beyond the search start.
			return false;
		}
	}
	return false;
}

bool is_location_ordered(const RefactorLocation &p_location) {
	if (p_location.start_line < p_location.end_line) {
		return true;
	}
	return p_location.start_line == p_location.end_line && p_location.start_column <= p_location.end_column;
}

String get_leading_whitespace(const String &p_line) {
	int end = 0;
	while (end < p_line.length() && is_whitespace(p_line[end])) {
		end++;
	}
	return p_line.substr(0, end);
}

int get_trailing_whitespace_start_column(const String &p_line) {
	int start = p_line.length();
	while (start > 0 && is_whitespace(p_line[start - 1])) {
		start--;
	}
	return start;
}

bool string_name_vector_has(const Vector<StringName> &p_names, const StringName &p_name) {
	for (const StringName &name : p_names) {
		if (name == p_name) {
			return true;
		}
	}
	return false;
}

bool get_single_line_selection_text(const Vector<String> &p_lines, const RefactorLocation &p_location, String &r_text) {
	if (p_location.start_line != p_location.end_line || p_location.start_line < 0 || p_location.start_line >= p_lines.size()) {
		return false;
	}
	const String line = p_lines[p_location.start_line];
	if (p_location.start_column < 0 || p_location.end_column < p_location.start_column || p_location.end_column > line.length()) {
		return false;
	}
	r_text = line.substr(p_location.start_column, p_location.end_column - p_location.start_column);
	return !r_text.is_empty();
}

bool get_node_text_range(const Vector<String> &p_lines, const FSParser::Node *p_node, RefactorLocation &r_range) {
	if (p_node == nullptr || p_node->start_line <= 0 || p_node->end_line <= 0) {
		return false;
	}
	const int start_line = p_node->start_line - 1;
	const int end_line = p_node->end_line - 1;
	if (start_line < 0 || start_line >= p_lines.size() || end_line < 0 || end_line >= p_lines.size()) {
		return false;
	}
	const int start_column = FSTextPosition::godot_column_to_text_column(p_lines[start_line], p_node->start_column);
	const int end_column = FSTextPosition::godot_column_to_text_column(p_lines[end_line], p_node->end_column);
	if (start_column < 0 || end_column < 0) {
		return false;
	}
	r_range.start_line = start_line;
	r_range.start_column = start_column;
	r_range.end_line = end_line;
	r_range.end_column = end_column;
	return true;
}

bool get_single_line_node_text(const Vector<String> &p_lines, const FSParser::Node *p_node, String &r_text, RefactorLocation *r_range) {
	RefactorLocation range;
	if (!get_node_text_range(p_lines, p_node, range) || range.start_line != range.end_line) {
		return false;
	}
	if (range.start_line < 0 || range.start_line >= p_lines.size()) {
		return false;
	}
	const String line = p_lines[range.start_line];
	if (range.start_column < 0 || range.end_column < range.start_column || range.end_column > line.length()) {
		return false;
	}
	r_text = line.substr(range.start_column, range.end_column - range.start_column);
	if (r_range != nullptr) {
		*r_range = range;
	}
	return !r_text.is_empty();
}

// Reconstructs a node's source text even when it spans multiple lines, joining the
// intermediate lines verbatim. Single-line nodes are sliced directly; multi-line nodes
// reproduce the original line breaks so the recovered text parses identically.
bool get_multi_line_node_text(const Vector<String> &p_lines, const FSParser::Node *p_node, String &r_text) {
	RefactorLocation range;
	if (!get_node_text_range(p_lines, p_node, range)) {
		return false;
	}
	if (range.start_line == range.end_line) {
		const String line = p_lines[range.start_line];
		if (range.start_column < 0 || range.end_column < range.start_column || range.end_column > line.length()) {
			return false;
		}
		r_text = line.substr(range.start_column, range.end_column - range.start_column);
		return !r_text.is_empty();
	}

	const String first_line = p_lines[range.start_line];
	const String last_line = p_lines[range.end_line];
	if (range.start_column < 0 || range.start_column > first_line.length() || range.end_column < 0 || range.end_column > last_line.length()) {
		return false;
	}
	String text = first_line.substr(range.start_column, first_line.length() - range.start_column);
	for (int line_index = range.start_line + 1; line_index < range.end_line; line_index++) {
		text += "\n" + p_lines[line_index];
	}
	text += "\n" + last_line.substr(0, range.end_column);
	r_text = text;
	return !r_text.is_empty();
}

bool expression_matches_selection(const RefactorLocation &p_location, const Vector<String> &p_lines, const FSParser::ExpressionNode *p_expression) {
	RefactorLocation expression_range;
	if (!get_node_text_range(p_lines, p_expression, expression_range)) {
		return false;
	}
	return same_location(p_location, expression_range);
}

int get_node_start_line_0(const FSParser::Node *p_node) {
	return p_node != nullptr ? p_node->start_line - 1 : -1;
}

int get_node_end_line_exclusive_0(const FSParser::Node *p_node) {
	return p_node != nullptr ? p_node->end_line : -1;
}

void get_source_end_position(const String &p_source, int &r_line, int &r_column) {
	r_line = 0;
	r_column = 0;
	for (int i = 0; i < p_source.length(); i++) {
		if (p_source[i] == '\n') {
			r_line++;
			r_column = 0;
		} else {
			r_column++;
		}
	}
}

bool caret_within_range(const RefactorLocation &p_location, const RefactorLocation &p_range) {
	if (p_location.has_selection()) {
		return false;
	}
	// caret_in_multiline_span only checks the lower column bound on its start line, which
	// for a single-line range would match a caret anywhere to the right of the statement
	// (e.g. a trailing `; other_statement`). Bound a single-line range on both ends so the
	// cast site is selected only when the caret is actually inside it.
	if (p_range.start_line == p_range.end_line) {
		return p_location.start_line == p_range.start_line &&
				p_location.start_column >= p_range.start_column &&
				p_location.start_column <= p_range.end_column;
	}
	return caret_in_multiline_span(p_location, p_range.start_line, p_range.start_column, p_range.end_line, p_range.end_column);
}

void finalize_result(RefactorResult &r_result) {
	if (!r_result.ok) {
		return;
	}
	FSRefactorEdits::sort_edits(r_result.edits);
	FSRefactorEdits::sort_edits(r_result.rename_occurrences);
	for (RefactorFileEdit &file_edit : r_result.file_edits) {
		FSRefactorEdits::sort_edits(file_edit.edits);
	}

	String error;
	if (!FSRefactorEdits::validate_structure(r_result.edits, error) ||
			!FSRefactorEdits::validate_structure(r_result.rename_occurrences, error)) {
		r_result = RefactorResult();
		r_result.error_message = vformat("The refactor produced invalid edits: %s", error);
		return;
	}
	for (const RefactorFileEdit &file_edit : r_result.file_edits) {
		if (!FSRefactorEdits::validate_structure(file_edit.edits, error)) {
			r_result = RefactorResult();
			r_result.error_message = vformat("The refactor produced invalid edits for '%s': %s", file_edit.path, error);
			return;
		}
	}
}

void finalize_candidates(RefactorCandidatesResult &r_result) {
	if (!r_result.ok) {
		return;
	}
	String error;
	for (RefactorCandidate &candidate : r_result.candidates) {
		if (!candidate.enabled) {
			continue;
		}
		FSRefactorEdits::sort_edits(candidate.edits);
		if (!FSRefactorEdits::validate_structure(candidate.edits, error)) {
			candidate.enabled = false;
			candidate.disabled_reason = vformat("The refactor produced invalid edits: %s", error);
			candidate.edits.clear();
		}
	}
}

} // namespace FSRefactorShared

#endif // TOOLS_ENABLED
