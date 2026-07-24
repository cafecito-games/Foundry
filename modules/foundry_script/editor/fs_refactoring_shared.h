/**************************************************************************/
/*  fs_refactoring_shared.h                                               */
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

#include "fs_refactoring.h"

#include "../fs_parser.h"

#include "core/string/ustring.h"
#include "core/templates/hash_map.h"
#include "core/templates/vector.h"

#ifndef FOUNDRY_SCRIPT_NO_LSP
#include "../language_server/fs_workspace.h"

class ExtendFSParser;
#endif // FOUNDRY_SCRIPT_NO_LSP

// Infrastructure shared by every refactor implementation: parse-result access,
// caret/location predicates, source-range and node-text recovery, and edit and
// result assembly. Individual refactors build on these helpers and must not
// keep feature-specific state here.
namespace FSRefactorShared {

#ifndef FOUNDRY_SCRIPT_NO_LSP
// Serves parse results for the active buffer (and, on demand, other project
// files read from disk) so cross-file refactors never consult connected
// clients' unsaved buffers or the shared protocol cache.
class RefactorParseResultProvider : public FSParseResultProvider {
	mutable HashMap<String, ExtendFSParser *> parse_results;

	void parse_source(const String &p_path, const String &p_source) const;

public:
	explicit RefactorParseResultProvider(const RefactorContext &p_context);
	~RefactorParseResultProvider();

	const ExtendFSParser *get_parse_result(const String &p_path) const override;
	const ExtendFSParser *peek_parse_result(const String &p_path) const override;
};

bool can_use_refactor_parse_results();

// Owns a RefactorParseResultProvider for the duration of one refactor entry
// point, when the language server workspace is available.
class RefactorParseResultProviderScope {
	RefactorParseResultProvider *parse_results = nullptr;

public:
	explicit RefactorParseResultProviderScope(const RefactorContext &p_context);
	~RefactorParseResultProviderScope();

	const FSParseResultProvider *get() const;
};
#endif // FOUNDRY_SCRIPT_NO_LSP

// Caret and location predicates. Locations are 0-based; see RefactorLocation.
bool same_location(const RefactorLocation &p_a, const RefactorLocation &p_b);
bool caret_on_segment(const RefactorLocation &p_location, int p_line, int p_start_column, int p_end_column);
bool caret_in_multiline_span(const RefactorLocation &p_location, int p_start_line, int p_start_column, int p_end_line, int p_end_column);
bool caret_within_range(const RefactorLocation &p_location, const RefactorLocation &p_range);
bool is_location_ordered(const RefactorLocation &p_location);

// Raw source-line scanning. These helpers are string- and comment-aware so a
// quote or `#` never produces a false structural match.
int skip_string_literal(const String &p_line, int p_quote_column);
bool is_column_inside_string_literal(const String &p_line, int p_column);
bool find_function_signature_colon(const Vector<String> &p_lines, int p_start_line, int p_search_start, int p_last_line, int &r_line, int &r_column);
bool find_declaration_assignment_operator(const Vector<String> &p_lines, int p_start_line, int p_search_start, int &r_line, int &r_column, bool &r_is_inferred, int &r_colon_line, int &r_colon_column);
int find_assignment_rhs_start(const String &p_line, int p_equal_index);
String get_leading_whitespace(const String &p_line);
int get_trailing_whitespace_start_column(const String &p_line);
void get_source_end_position(const String &p_source, int &r_line, int &r_column);

// Identifier and node text recovery from the split source lines.
bool is_identifier_boundary(const String &p_line, int p_start, int p_end);
bool find_identifier_on_line(const Vector<String> &p_lines, int p_line, const StringName &p_name, const RefactorLocation *p_location, int &r_start, int &r_end);
bool get_identifier_text_span(const Vector<String> &p_lines, int p_line, const FSParser::IdentifierNode *p_identifier, const RefactorLocation *p_location, int &r_start, int &r_end);
bool get_node_text_start(const Vector<String> &p_lines, int p_line, const FSParser::Node *p_node, const String &p_expected_text, int &r_start);
bool get_single_line_selection_text(const Vector<String> &p_lines, const RefactorLocation &p_location, String &r_text);
bool get_node_text_range(const Vector<String> &p_lines, const FSParser::Node *p_node, RefactorLocation &r_range);
bool get_single_line_node_text(const Vector<String> &p_lines, const FSParser::Node *p_node, String &r_text, RefactorLocation *r_range = nullptr);
bool get_multi_line_node_text(const Vector<String> &p_lines, const FSParser::Node *p_node, String &r_text);
bool expression_matches_selection(const RefactorLocation &p_location, const Vector<String> &p_lines, const FSParser::ExpressionNode *p_expression);
int get_node_start_line_0(const FSParser::Node *p_node);
int get_node_end_line_exclusive_0(const FSParser::Node *p_node);

bool string_name_vector_has(const Vector<StringName> &p_names, const StringName &p_name);

// Edit and result assembly.
RefactorFileEdit *find_or_add_file_edit(Vector<RefactorFileEdit> &r_file_edits, const String &p_path);
void append_warning(RefactorResult &r_result, const String &p_warning);

// Enforces the edit-level invariants shared by all refactors on a prepared
// result: every edit vector is sorted, and every vector has ordered,
// non-overlapping ranges. A structural violation marks the result as failed so
// a broken refactor can never hand invalid edits to a caller.
void finalize_result(RefactorResult &r_result);

// Same invariants for headless candidate collection: each enabled candidate's
// edits are sorted, and a structural violation disables that candidate.
void finalize_candidates(RefactorCandidatesResult &r_result);

} // namespace FSRefactorShared

#endif // TOOLS_ENABLED
