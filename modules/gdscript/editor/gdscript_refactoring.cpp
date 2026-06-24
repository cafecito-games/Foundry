/**************************************************************************/
/*  gdscript_refactoring.cpp                                              */
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

#include "gdscript_refactoring.h"

#ifdef TOOLS_ENABLED

#include "gdscript_refactoring_names.h"
#include "gdscript_refactoring_types.h"

#include "../gdscript_analyzer.h"
#include "../gdscript_position.h"

#include "core/os/mutex.h"
#include "core/string/char_utils.h"

#ifndef GDSCRIPT_NO_LSP
#include "../language_server/gdscript_extend_parser.h"
#include "../language_server/gdscript_language_protocol.h"
#include "../language_server/gdscript_workspace.h"
#include "../language_server/godot_lsp.h"
#endif // GDSCRIPT_NO_LSP

#ifdef GDSCRIPT_NO_LSP
class GDScriptParseResultProvider;
#endif // GDSCRIPT_NO_LSP

namespace {

struct TypeAnnotationCandidate {
	bool matched = false;
	bool enabled = false;
	String disabled_reason;
	RefactorTextEdit edit;
	// Caret-test span of the declaration this candidate was found at. Used by the
	// caret-driven path to select the candidate under the caret; the headless
	// collector ignores it.
	int line = -1;
	int caret_span_start = -1;
	int caret_span_end = -1;
};

struct ExtractVariableCandidate {
	bool matched = false;
	bool enabled = false;
	String disabled_reason;
	String suggested_name;
	RefactorTextEdit declaration_edit;
	RefactorTextEdit replacement_edit;
	int name_line = -1;
	int name_column = -1;
};

struct ExtractMethodParameter {
	StringName name;
	GDScriptParser::DataType datatype;
};

struct ExtractMethodLocal {
	StringName name;
	GDScriptParser::DataType datatype;
};

struct ExtractMethodCandidate {
	bool matched = false;
	bool enabled = false;
	String disabled_reason;
	String suggested_name;
	RefactorTextEdit replacement_edit;
	RefactorTextEdit method_edit;
	int name_line = -1;
	int name_column = -1;
};

struct InlineVariableUse {
	const GDScriptParser::IdentifierNode *identifier = nullptr;
	const GDScriptParser::Node *parent = nullptr;
};

struct InlineVariableCandidate {
	bool matched = false;
	bool enabled = false;
	String disabled_reason;
	RefactorTextEdit declaration_edit;
	Vector<RefactorTextEdit> replacement_edits;
};

#ifndef GDSCRIPT_NO_LSP
class RefactorParseResultProvider : public GDScriptParseResultProvider {
	mutable HashMap<String, ExtendGDScriptParser *> parse_results;

	void parse_source(const String &p_path, const String &p_source) const {
		if (!p_path.has_extension("gd")) {
			return;
		}

		parse_results[p_path] = ExtendGDScriptParser::parse_source(p_source, p_path);
	}

public:
	explicit RefactorParseResultProvider(const RefactorContext &p_context) {
		parse_source(p_context.path, p_context.source);
	}

	~RefactorParseResultProvider() {
		for (KeyValue<String, ExtendGDScriptParser *> &E : parse_results) {
			memdelete(E.value);
		}
	}

	const ExtendGDScriptParser *get_parse_result(const String &p_path) const override {
		ExtendGDScriptParser **existing = parse_results.getptr(p_path);
		if (existing != nullptr) {
			return *existing;
		}

		// Cross-file refactors intentionally parse non-active scripts from disk
		// instead of consulting connected clients' unsaved buffers or the shared
		// protocol cache.
		ExtendGDScriptParser *parser = ExtendGDScriptParser::parse_file(p_path);
		if (parser != nullptr) {
			parse_results[p_path] = parser;
		}
		return parser;
	}
};

bool can_use_refactor_parse_results() {
	GDScriptLanguageProtocol *protocol = GDScriptLanguageProtocol::get_singleton();
	return protocol != nullptr && protocol->get_workspace().is_valid();
}

class RefactorParseResultProviderScope {
	RefactorParseResultProvider *parse_results = nullptr;

public:
	explicit RefactorParseResultProviderScope(const RefactorContext &p_context) {
		if (can_use_refactor_parse_results()) {
			parse_results = memnew(RefactorParseResultProvider(p_context));
		}
	}

	~RefactorParseResultProviderScope() {
		if (parse_results != nullptr) {
			memdelete(parse_results);
		}
	}

	const GDScriptParseResultProvider *get() const {
		return parse_results;
	}
};
#endif // GDSCRIPT_NO_LSP

struct TypeAnnotationCandidateCache {
	bool valid = false;
	String path;
	uint64_t source_hash = 0;
	int source_length = 0;
	RefactorLocation location;
	TypeAnnotationCandidate candidate;
};

TypeAnnotationCandidateCache type_annotation_cache;

struct ExtractVariableCandidateCache {
	bool valid = false;
	String path;
	uint64_t source_hash = 0;
	int source_length = 0;
	RefactorLocation location;
	ExtractVariableCandidate candidate;
};

ExtractVariableCandidateCache extract_variable_cache;

struct ExtractMethodCandidateCache {
	bool valid = false;
	String path;
	uint64_t source_hash = 0;
	int source_length = 0;
	RefactorLocation location;
	ExtractMethodCandidate candidate;
};

ExtractMethodCandidateCache extract_method_cache;

struct InlineVariableCandidateCache {
	bool valid = false;
	String path;
	uint64_t source_hash = 0;
	int source_length = 0;
	RefactorLocation location;
	InlineVariableCandidate candidate;
};

InlineVariableCandidateCache inline_variable_cache;

Mutex refactor_candidate_cache_mutex;

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

bool is_supported_dynamic_string_call(const String &p_name) {
	return p_name == "get" ||
			p_name == "set" ||
			p_name == "call" ||
			p_name == "call_deferred" ||
			p_name == "set_deferred";
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

// Locates the colon that terminates a single-line function signature: the first
// top-level ':' that is not inside the parameter list, a bracketed/braced type or
// default value, a string literal, or a trailing comment. Returns -1 when none is
// found on the line (e.g. a multi-line signature). Scanning from the signature
// start and stopping at the first '#' keeps the return-type edit off comment and
// string colons.
int find_function_signature_colon(const String &p_line, int p_search_start) {
	int depth = 0;
	for (int i = p_search_start < 0 ? 0 : p_search_start; i < p_line.length(); i++) {
		const char32_t c = p_line[i];
		if (c == '#') {
			break;
		}
		if (c == '"' || c == '\'') {
			i = skip_string_literal(p_line, i) - 1;
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
			return i;
		}
	}
	return -1;
}

int find_first_string_argument_column(
		const String &p_line,
		int p_open_paren_column,
		const String &p_identifier,
		int p_match_column = -1) {
	int pos = p_open_paren_column + 1;
	while (pos < p_line.length() && is_whitespace(p_line[pos])) {
		pos++;
	}

	if (pos >= p_line.length() || (p_line[pos] != '"' && p_line[pos] != '\'')) {
		return -1;
	}

	const char32_t quote = p_line[pos];
	String value;
	bool escaped = false;
	for (int i = pos + 1; i < p_line.length(); i++) {
		const char32_t c = p_line[i];
		if (escaped) {
			value += String::chr(c);
			escaped = false;
			continue;
		}
		if (c == '\\') {
			escaped = true;
			continue;
		}
		if (c == quote) {
			const bool column_matches = p_match_column < 0 || (p_match_column >= pos && p_match_column <= i);
			return value == p_identifier && column_matches ? pos + 1 : -1;
		}
		value += String::chr(c);
	}

	return -1;
}

int find_dynamic_string_reference_column(const String &p_line, const String &p_identifier, int p_match_column = -1) {
	// This is a conservative scan for common Object string-call forms. It is not a
	// complete dynamic-reference parser and intentionally ignores unrelated strings.
	for (int i = 0; i < p_line.length(); i++) {
		const char32_t c = p_line[i];
		if (c == '#') {
			return -1;
		}
		if (c == '"' || c == '\'') {
			i = skip_string_literal(p_line, i) - 1;
			continue;
		}
		if (!is_unicode_identifier_start(c)) {
			continue;
		}

		const int name_start = i;
		i++;
		while (i < p_line.length() && is_unicode_identifier_continue(p_line[i])) {
			i++;
		}
		const int name_end = i;
		const String name = p_line.substr(name_start, name_end - name_start);
		if (!is_supported_dynamic_string_call(name)) {
			i--;
			continue;
		}

		int open_paren = name_end;
		while (open_paren < p_line.length() && is_whitespace(p_line[open_paren])) {
			open_paren++;
		}
		if (open_paren >= p_line.length() || p_line[open_paren] != '(') {
			i--;
			continue;
		}

		const int column = find_first_string_argument_column(p_line, open_paren, p_identifier, p_match_column);
		if (column >= 0) {
			return column;
		}
		i--;
	}

	return -1;
}

bool render_annotation_or_disable(const GDScriptParser::DataType &p_type, TypeAnnotationCandidate &r_candidate, String &r_rendered) {
	if (!GDScriptRefactorTypes::render_annotatable_type(p_type, r_rendered)) {
		r_candidate.disabled_reason = "The inferred type cannot be written as an explicit annotation.";
		return false;
	}
	return true;
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

bool get_identifier_text_span(const Vector<String> &p_lines, int p_line, const GDScriptParser::IdentifierNode *p_identifier, const RefactorLocation *p_location, int &r_start, int &r_end) {
	if (p_identifier == nullptr) {
		return false;
	}
	if (p_identifier->start_line == p_line + 1 && p_identifier->start_column > 0 && p_identifier->end_column > p_identifier->start_column) {
		const String line = p_lines[p_line];
		const String name = String(p_identifier->name);
		const int start = GDScriptTextPosition::godot_column_to_text_column(line, p_identifier->start_column);
		const int end = GDScriptTextPosition::godot_column_to_text_column(line, p_identifier->end_column);
		if (start >= 0 && end <= line.length() && end > start && line.substr(start, end - start) == name) {
			r_start = start;
			r_end = end;
			return true;
		}
	}
	return find_identifier_on_line(p_lines, p_line, p_identifier->name, p_location, r_start, r_end);
}

bool get_node_text_start(const Vector<String> &p_lines, int p_line, const GDScriptParser::Node *p_node, const String &p_expected_text, int &r_start) {
	if (p_node == nullptr || p_node->start_line != p_line + 1 || p_node->start_column <= 0 || p_line < 0 || p_line >= p_lines.size()) {
		return false;
	}
	const String line = p_lines[p_line];
	const int start = GDScriptTextPosition::godot_column_to_text_column(line, p_node->start_column);
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

bool string_name_vector_has(const Vector<StringName> &p_names, const StringName &p_name) {
	for (const StringName &name : p_names) {
		if (name == p_name) {
			return true;
		}
	}
	return false;
}

bool extract_method_has_param(const Vector<ExtractMethodParameter> &p_parameters, const StringName &p_name) {
	for (const ExtractMethodParameter &parameter : p_parameters) {
		if (parameter.name == p_name) {
			return true;
		}
	}
	return false;
}

bool extract_method_has_local(const Vector<ExtractMethodLocal> &p_locals, const StringName &p_name) {
	for (const ExtractMethodLocal &local : p_locals) {
		if (local.name == p_name) {
			return true;
		}
	}
	return false;
}

void extract_method_add_local(
		Vector<ExtractMethodLocal> &r_locals,
		const StringName &p_name,
		const GDScriptParser::DataType &p_datatype) {
	if (extract_method_has_local(r_locals, p_name)) {
		return;
	}

	ExtractMethodLocal local;
	local.name = p_name;
	local.datatype = p_datatype;
	r_locals.push_back(local);
}

void extract_method_add_name(Vector<StringName> &r_names, const StringName &p_name) {
	if (!string_name_vector_has(r_names, p_name)) {
		r_names.push_back(p_name);
	}
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

bool get_node_text_range(const Vector<String> &p_lines, const GDScriptParser::Node *p_node, RefactorLocation &r_range) {
	if (p_node == nullptr || p_node->start_line <= 0 || p_node->end_line <= 0) {
		return false;
	}
	const int start_line = p_node->start_line - 1;
	const int end_line = p_node->end_line - 1;
	if (start_line < 0 || start_line >= p_lines.size() || end_line < 0 || end_line >= p_lines.size()) {
		return false;
	}
	const int start_column = GDScriptTextPosition::godot_column_to_text_column(p_lines[start_line], p_node->start_column);
	const int end_column = GDScriptTextPosition::godot_column_to_text_column(p_lines[end_line], p_node->end_column);
	if (start_column < 0 || end_column < 0) {
		return false;
	}
	r_range.start_line = start_line;
	r_range.start_column = start_column;
	r_range.end_line = end_line;
	r_range.end_column = end_column;
	return true;
}

bool get_single_line_node_text(const Vector<String> &p_lines, const GDScriptParser::Node *p_node, String &r_text, RefactorLocation *r_range = nullptr) {
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

bool expression_matches_selection(const RefactorLocation &p_location, const Vector<String> &p_lines, const GDScriptParser::ExpressionNode *p_expression) {
	RefactorLocation expression_range;
	if (!get_node_text_range(p_lines, p_expression, expression_range)) {
		return false;
	}
	return same_location(p_location, expression_range);
}

bool suite_or_ancestors_have_local(const GDScriptParser::SuiteNode *p_suite, const StringName &p_name) {
	const GDScriptParser::SuiteNode *suite = p_suite;
	while (suite != nullptr) {
		if (suite->has_local(p_name)) {
			return true;
		}
		suite = suite->parent_block;
	}
	return false;
}

String expression_name_hint(const GDScriptParser::ExpressionNode *p_expression) {
	if (p_expression == nullptr) {
		return String();
	}

	switch (p_expression->type) {
		case GDScriptParser::Node::IDENTIFIER:
			return String(static_cast<const GDScriptParser::IdentifierNode *>(p_expression)->name);
		case GDScriptParser::Node::SUBSCRIPT: {
			const GDScriptParser::SubscriptNode *subscript = static_cast<const GDScriptParser::SubscriptNode *>(p_expression);
			if (subscript->is_attribute && subscript->attribute != nullptr) {
				return String(subscript->attribute->name);
			}
		} break;
		case GDScriptParser::Node::CALL: {
			const GDScriptParser::CallNode *call = static_cast<const GDScriptParser::CallNode *>(p_expression);
			String name = expression_name_hint(call->callee);
			if (name.begins_with("get_") && name.length() > 4) {
				name = name.substr(4);
			} else if (name.begins_with("is_") && name.length() > 3) {
				name = name.substr(3);
			} else if (name.begins_with("has_") && name.length() > 4) {
				name = name.substr(4);
			}
			return name;
		}
		case GDScriptParser::Node::AWAIT:
			return expression_name_hint(static_cast<const GDScriptParser::AwaitNode *>(p_expression)->to_await);
		case GDScriptParser::Node::CAST:
			return expression_name_hint(static_cast<const GDScriptParser::CastNode *>(p_expression)->operand);
		default:
			break;
	}

	return String();
}

String fallback_name_for_type(const GDScriptParser::DataType &p_type) {
	if (p_type.kind == GDScriptParser::DataType::BUILTIN) {
		switch (p_type.builtin_type) {
			case Variant::BOOL:
				return "flag";
			case Variant::STRING:
			case Variant::STRING_NAME:
			case Variant::NODE_PATH:
				return "text";
			case Variant::ARRAY:
			case Variant::PACKED_BYTE_ARRAY:
			case Variant::PACKED_INT32_ARRAY:
			case Variant::PACKED_INT64_ARRAY:
			case Variant::PACKED_FLOAT32_ARRAY:
			case Variant::PACKED_FLOAT64_ARRAY:
			case Variant::PACKED_STRING_ARRAY:
			case Variant::PACKED_VECTOR2_ARRAY:
			case Variant::PACKED_VECTOR3_ARRAY:
			case Variant::PACKED_COLOR_ARRAY:
			case Variant::PACKED_VECTOR4_ARRAY:
				return "items";
			case Variant::DICTIONARY:
				return "map";
			default:
				break;
		}
	}
	return "value";
}

String make_unique_local_name(const GDScriptParser::ExpressionNode *p_expression, const GDScriptParser::DataType &p_type, const GDScriptParser::SuiteNode *p_suite) {
	String base = expression_name_hint(p_expression);
	String reason;
	if (base.is_empty() || !GDScriptRefactorNames::validate_identifier(base, reason)) {
		base = fallback_name_for_type(p_type);
	}
	if (!GDScriptRefactorNames::validate_identifier(base, reason)) {
		base = "value";
	}

	String name = base;
	int suffix = 2;
	while (!GDScriptRefactorNames::validate_identifier(name, reason) || suite_or_ancestors_have_local(p_suite, StringName(name))) {
		name = vformat("%s_%d", base, suffix);
		suffix++;
		if (suffix > 1000) {
			return String();
		}
	}
	return name;
}

bool try_extract_direct_expression(const RefactorLocation &p_location, const Vector<String> &p_lines, const GDScriptParser::ExpressionNode *p_expression, const GDScriptParser::Node *p_statement, const GDScriptParser::SuiteNode *p_suite, ExtractVariableCandidate &r_candidate) {
	if (!expression_matches_selection(p_location, p_lines, p_expression)) {
		return false;
	}

	r_candidate.matched = true;
	if (p_suite == nullptr || p_suite->parent_function == nullptr) {
		r_candidate.disabled_reason = "Extract variable is only available inside a function body.";
		return true;
	}
	if (p_statement == nullptr || p_statement->start_line <= 0 || p_statement->start_line > p_lines.size()) {
		r_candidate.disabled_reason = "Cannot find a safe insertion point for this expression.";
		return true;
	}

	String rendered_type;
	if (!GDScriptRefactorTypes::render_annotatable_type(p_expression->get_datatype(), rendered_type)) {
		r_candidate.disabled_reason = "The inferred type cannot be written as an explicit annotation.";
		return true;
	}

	String expression_text;
	if (!get_single_line_selection_text(p_lines, p_location, expression_text)) {
		r_candidate.disabled_reason = "Extract variable currently supports single-line expressions.";
		return true;
	}

	const String name = make_unique_local_name(p_expression, p_expression->get_datatype(), p_suite);
	if (name.is_empty()) {
		r_candidate.disabled_reason = "Cannot suggest a safe local variable name.";
		return true;
	}

	const int insertion_line = p_statement->start_line - 1;
	const String indent = get_leading_whitespace(p_lines[insertion_line]);
	const String declaration_prefix = indent + "var ";
	r_candidate.declaration_edit.start_line = insertion_line;
	r_candidate.declaration_edit.start_column = 0;
	r_candidate.declaration_edit.end_line = insertion_line;
	r_candidate.declaration_edit.end_column = 0;
	// The replacement expression is inside an indented function-body statement,
	// so its start offset is after this column-0 insertion. That keeps edit
	// ordering unambiguous even though RefactorEdits sorts only by start offset.
	r_candidate.declaration_edit.new_text = declaration_prefix + name + ": " + rendered_type +
			" = " + expression_text + "\n";

	r_candidate.replacement_edit.start_line = p_location.start_line;
	r_candidate.replacement_edit.start_column = p_location.start_column;
	r_candidate.replacement_edit.end_line = p_location.end_line;
	r_candidate.replacement_edit.end_column = p_location.end_column;
	r_candidate.replacement_edit.new_text = name;
	r_candidate.suggested_name = name;
	r_candidate.name_line = insertion_line;
	r_candidate.name_column = declaration_prefix.length();
	r_candidate.enabled = true;
	return true;
}

bool is_self_attribute(const GDScriptParser::ExpressionNode *p_expression) {
	if (p_expression == nullptr || p_expression->type != GDScriptParser::Node::SUBSCRIPT) {
		return false;
	}
	const GDScriptParser::SubscriptNode *subscript = static_cast<const GDScriptParser::SubscriptNode *>(p_expression);
	return subscript->is_attribute && subscript->base != nullptr && subscript->base->type == GDScriptParser::Node::SELF;
}

bool is_safe_extract_assignment(const GDScriptParser::AssignmentNode *p_assignment) {
	if (p_assignment == nullptr || p_assignment->assignee == nullptr) {
		return false;
	}
	if (p_assignment->assignee->type == GDScriptParser::Node::IDENTIFIER) {
		return true;
	}
	return p_assignment->operation == GDScriptParser::AssignmentNode::OP_NONE &&
			is_self_attribute(p_assignment->assignee);
}

bool find_assignable_type_annotation(const Vector<String> &p_lines, const GDScriptParser::AssignableNode *p_assignable, const String &p_kind, bool p_has_keyword, TypeAnnotationCandidate &r_candidate) {
	if (p_assignable == nullptr || p_assignable->identifier == nullptr) {
		return false;
	}

	const int line_index = p_assignable->start_line - 1;
	if (line_index < 0 || line_index >= p_lines.size()) {
		return false;
	}

	const String line = p_lines[line_index];
	int name_start = 0;
	int name_end = 0;
	const RefactorLocation *identifier_location = nullptr; // Collect mode resolves the declaration identifier without caret scoping.
	if (!get_identifier_text_span(p_lines, line_index, p_assignable->identifier, identifier_location, name_start, name_end)) {
		return false;
	}
	int declaration_start = name_start;
	if (p_has_keyword) {
		const String keyword = p_assignable->type == GDScriptParser::Node::CONSTANT ? "const" : "var";
		int keyword_start = 0;
		if (get_node_text_start(p_lines, line_index, p_assignable, keyword, keyword_start)) {
			declaration_start = keyword_start;
		} else {
			keyword_start = line.find(keyword);
			if (keyword_start >= 0 && keyword_start <= name_start) {
				declaration_start = keyword_start;
			}
		}
	}

	// The parser does not expose the assignment operator token, so this refactor
	// intentionally handles declarations whose assignment delimiter is on the
	// declaration line.
	const int equal_index = line.find("=", name_end);
	const int declaration_end = equal_index >= 0 ? equal_index : name_end;

	r_candidate.matched = true;
	r_candidate.line = line_index;
	r_candidate.caret_span_start = declaration_start;
	r_candidate.caret_span_end = declaration_end;
	if (p_assignable->datatype_specifier != nullptr) {
		r_candidate.disabled_reason = "This declaration already has a type annotation.";
		return true;
	}
	if (p_assignable->initializer == nullptr || equal_index < 0) {
		r_candidate.disabled_reason = vformat("Cannot infer a type for this %s.", p_kind);
		return true;
	}

	String rendered_type;
	if (!render_annotation_or_disable(p_assignable->get_datatype(), r_candidate, rendered_type)) {
		return true;
	}

	const int rhs_start = find_assignment_rhs_start(line, equal_index);
	r_candidate.edit.start_line = line_index;
	r_candidate.edit.start_column = name_end;
	r_candidate.edit.end_line = line_index;
	r_candidate.edit.end_column = rhs_start;
	r_candidate.edit.new_text = ": " + rendered_type + " = ";
	r_candidate.enabled = true;
	return true;
}

bool find_function_return_type_annotation(const Vector<String> &p_lines, const GDScriptParser::FunctionNode *p_function, TypeAnnotationCandidate &r_candidate) {
	if (p_function == nullptr || p_function->identifier == nullptr) {
		return false;
	}

	const int line_index = p_function->start_line - 1;
	if (line_index < 0 || line_index >= p_lines.size()) {
		return false;
	}

	const String line = p_lines[line_index];
	int function_start = 0;
	if (!get_node_text_start(p_lines, line_index, p_function, "func", function_start)) {
		function_start = line.find("func");
	}
	// The parser does not expose the body-colon token for the signature, so this
	// refactor intentionally handles single-line function signatures. Scan for the
	// first top-level colon so a trailing comment or string colon is never targeted.
	const int body_colon = find_function_signature_colon(line, function_start);
	if (function_start < 0 || body_colon < 0) {
		return false;
	}

	r_candidate.matched = true;
	r_candidate.line = line_index;
	r_candidate.caret_span_start = function_start;
	r_candidate.caret_span_end = body_colon;
	if (p_function->return_type != nullptr) {
		r_candidate.disabled_reason = "This function already has a return type annotation.";
		return true;
	}

	String rendered_type;
	if (!render_annotation_or_disable(p_function->get_datatype(), r_candidate, rendered_type)) {
		return true;
	}

	r_candidate.edit.start_line = line_index;
	r_candidate.edit.start_column = body_colon;
	r_candidate.edit.end_line = line_index;
	r_candidate.edit.end_column = body_colon + 1;
	r_candidate.edit.new_text = " -> " + rendered_type + ":";
	r_candidate.enabled = true;
	return true;
}

bool find_extract_variable_in_suite(const RefactorLocation &p_location, const Vector<String> &p_lines, const GDScriptParser::SuiteNode *p_suite, ExtractVariableCandidate &r_candidate);

int get_node_start_line_0(const GDScriptParser::Node *p_node) {
	return p_node != nullptr ? p_node->start_line - 1 : -1;
}

int get_node_end_line_exclusive_0(const GDScriptParser::Node *p_node) {
	return p_node != nullptr ? p_node->end_line : -1;
}

bool extract_method_source_before(const GDScriptParser::IdentifierNode *p_identifier, int p_selection_start_line) {
	if (p_identifier == nullptr) {
		return false;
	}

	int start_line = -1;
	switch (p_identifier->source) {
		case GDScriptParser::IdentifierNode::FUNCTION_PARAMETER:
			start_line = get_node_start_line_0(p_identifier->parameter_source);
			break;
		case GDScriptParser::IdentifierNode::LOCAL_VARIABLE:
			start_line = get_node_start_line_0(p_identifier->variable_source);
			break;
		case GDScriptParser::IdentifierNode::LOCAL_CONSTANT:
			start_line = get_node_start_line_0(p_identifier->constant_source);
			break;
		case GDScriptParser::IdentifierNode::LOCAL_ITERATOR:
		case GDScriptParser::IdentifierNode::LOCAL_BIND:
			start_line = get_node_start_line_0(p_identifier->bind_source);
			break;
		default:
			break;
	}
	return start_line >= 0 && start_line < p_selection_start_line;
}

bool extract_method_source_inside(
		const GDScriptParser::IdentifierNode *p_identifier,
		const RefactorLocation &p_location) {
	if (p_identifier == nullptr) {
		return false;
	}

	int start_line = -1;
	switch (p_identifier->source) {
		case GDScriptParser::IdentifierNode::LOCAL_VARIABLE:
			start_line = get_node_start_line_0(p_identifier->variable_source);
			break;
		case GDScriptParser::IdentifierNode::LOCAL_CONSTANT:
			start_line = get_node_start_line_0(p_identifier->constant_source);
			break;
		case GDScriptParser::IdentifierNode::LOCAL_ITERATOR:
		case GDScriptParser::IdentifierNode::LOCAL_BIND:
			start_line = get_node_start_line_0(p_identifier->bind_source);
			break;
		default:
			break;
	}
	return start_line >= p_location.start_line && start_line < p_location.end_line;
}

bool extract_method_is_local_like_identifier(const GDScriptParser::IdentifierNode *p_identifier) {
	if (p_identifier == nullptr) {
		return false;
	}

	switch (p_identifier->source) {
		case GDScriptParser::IdentifierNode::FUNCTION_PARAMETER:
		case GDScriptParser::IdentifierNode::LOCAL_VARIABLE:
		case GDScriptParser::IdentifierNode::LOCAL_CONSTANT:
		case GDScriptParser::IdentifierNode::LOCAL_ITERATOR:
		case GDScriptParser::IdentifierNode::LOCAL_BIND:
			return true;
		default:
			return false;
	}
}

struct ExtractMethodReadState {
	Vector<ExtractMethodParameter> parameters;
	Vector<ExtractMethodLocal> declared_inside;
	Vector<StringName> *reads_after = nullptr;
	String disabled_reason;
};

void collect_extract_method_expr_reads(
		const GDScriptParser::ExpressionNode *p_expression,
		const RefactorLocation &p_location,
		ExtractMethodReadState &r_state);

void collect_extract_method_stmt_reads(
		const GDScriptParser::Node *p_statement,
		const RefactorLocation &p_location,
		ExtractMethodReadState &r_state);

void collect_extract_method_suite_reads(
		const GDScriptParser::SuiteNode *p_suite,
		const RefactorLocation &p_location,
		ExtractMethodReadState &r_state) {
	if (p_suite == nullptr || !r_state.disabled_reason.is_empty()) {
		return;
	}

	for (const GDScriptParser::Node *statement : p_suite->statements) {
		collect_extract_method_stmt_reads(statement, p_location, r_state);
		if (!r_state.disabled_reason.is_empty()) {
			return;
		}
	}
}

void collect_extract_method_identifier_read(
		const GDScriptParser::IdentifierNode *p_identifier,
		const RefactorLocation &p_location,
		ExtractMethodReadState &r_state) {
	if (!extract_method_is_local_like_identifier(p_identifier)) {
		return;
	}

	if (r_state.reads_after != nullptr) {
		if (extract_method_source_inside(p_identifier, p_location)) {
			extract_method_add_name(*r_state.reads_after, p_identifier->name);
		}
		return;
	}

	if (!extract_method_source_before(p_identifier, p_location.start_line) ||
			extract_method_has_param(r_state.parameters, p_identifier->name)) {
		return;
	}

	ExtractMethodParameter parameter;
	parameter.name = p_identifier->name;
	parameter.datatype = p_identifier->get_datatype();
	r_state.parameters.push_back(parameter);
}

void collect_extract_method_expr_reads(
		const GDScriptParser::ExpressionNode *p_expression,
		const RefactorLocation &p_location,
		ExtractMethodReadState &r_state) {
	if (p_expression == nullptr || !r_state.disabled_reason.is_empty()) {
		return;
	}

	switch (p_expression->type) {
		case GDScriptParser::Node::AWAIT:
			r_state.disabled_reason = "Cannot extract statements containing await.";
			return;
		case GDScriptParser::Node::IDENTIFIER:
			collect_extract_method_identifier_read(
					static_cast<const GDScriptParser::IdentifierNode *>(p_expression),
					p_location,
					r_state);
			return;
		case GDScriptParser::Node::ARRAY: {
			const GDScriptParser::ArrayNode *array = static_cast<const GDScriptParser::ArrayNode *>(p_expression);
			for (const GDScriptParser::ExpressionNode *element : array->elements) {
				collect_extract_method_expr_reads(element, p_location, r_state);
			}
		} break;
		case GDScriptParser::Node::ASSIGNMENT: {
			const GDScriptParser::AssignmentNode *assignment = static_cast<const GDScriptParser::AssignmentNode *>(p_expression);
			if (assignment->operation != GDScriptParser::AssignmentNode::OP_NONE) {
				collect_extract_method_expr_reads(assignment->assignee, p_location, r_state);
			}
			collect_extract_method_expr_reads(assignment->assigned_value, p_location, r_state);
		} break;
		case GDScriptParser::Node::BINARY_OPERATOR: {
			const GDScriptParser::BinaryOpNode *binary = static_cast<const GDScriptParser::BinaryOpNode *>(p_expression);
			collect_extract_method_expr_reads(binary->left_operand, p_location, r_state);
			collect_extract_method_expr_reads(binary->right_operand, p_location, r_state);
		} break;
		case GDScriptParser::Node::CALL: {
			const GDScriptParser::CallNode *call = static_cast<const GDScriptParser::CallNode *>(p_expression);
			collect_extract_method_expr_reads(call->callee, p_location, r_state);
			for (const GDScriptParser::ExpressionNode *argument : call->arguments) {
				collect_extract_method_expr_reads(argument, p_location, r_state);
			}
		} break;
		case GDScriptParser::Node::CAST: {
			const GDScriptParser::CastNode *cast = static_cast<const GDScriptParser::CastNode *>(p_expression);
			collect_extract_method_expr_reads(cast->operand, p_location, r_state);
		} break;
		case GDScriptParser::Node::DICTIONARY: {
			const GDScriptParser::DictionaryNode *dictionary = static_cast<const GDScriptParser::DictionaryNode *>(p_expression);
			for (const GDScriptParser::DictionaryNode::Pair &pair : dictionary->elements) {
				collect_extract_method_expr_reads(pair.key, p_location, r_state);
				collect_extract_method_expr_reads(pair.value, p_location, r_state);
			}
		} break;
		case GDScriptParser::Node::GET_NODE:
			break;
		case GDScriptParser::Node::LAMBDA:
			r_state.disabled_reason = "Cannot extract statements containing a lambda.";
			return;
		case GDScriptParser::Node::LITERAL:
			break;
		case GDScriptParser::Node::PRELOAD:
			break;
		case GDScriptParser::Node::SELF:
			break;
		case GDScriptParser::Node::SUBSCRIPT: {
			const GDScriptParser::SubscriptNode *subscript = static_cast<const GDScriptParser::SubscriptNode *>(p_expression);
			collect_extract_method_expr_reads(subscript->base, p_location, r_state);
			if (!subscript->is_attribute) {
				collect_extract_method_expr_reads(subscript->index, p_location, r_state);
			}
		} break;
		case GDScriptParser::Node::TERNARY_OPERATOR: {
			const GDScriptParser::TernaryOpNode *ternary = static_cast<const GDScriptParser::TernaryOpNode *>(p_expression);
			collect_extract_method_expr_reads(ternary->condition, p_location, r_state);
			collect_extract_method_expr_reads(ternary->true_expr, p_location, r_state);
			collect_extract_method_expr_reads(ternary->false_expr, p_location, r_state);
		} break;
		case GDScriptParser::Node::TYPE_TEST: {
			const GDScriptParser::TypeTestNode *type_test = static_cast<const GDScriptParser::TypeTestNode *>(p_expression);
			collect_extract_method_expr_reads(type_test->operand, p_location, r_state);
		} break;
		case GDScriptParser::Node::UNARY_OPERATOR: {
			const GDScriptParser::UnaryOpNode *unary = static_cast<const GDScriptParser::UnaryOpNode *>(p_expression);
			collect_extract_method_expr_reads(unary->operand, p_location, r_state);
		} break;
		default:
			break;
	}
}

void collect_extract_method_stmt_reads(
		const GDScriptParser::Node *p_statement,
		const RefactorLocation &p_location,
		ExtractMethodReadState &r_state) {
	if (p_statement == nullptr || !r_state.disabled_reason.is_empty()) {
		return;
	}

	switch (p_statement->type) {
		case GDScriptParser::Node::RETURN:
			r_state.disabled_reason = "Cannot extract statements containing return.";
			return;
		case GDScriptParser::Node::BREAK:
			r_state.disabled_reason = "Cannot extract statements containing break.";
			return;
		case GDScriptParser::Node::CONTINUE:
			r_state.disabled_reason = "Cannot extract statements containing continue.";
			return;
		case GDScriptParser::Node::AWAIT:
			r_state.disabled_reason = "Cannot extract statements containing await.";
			return;
		case GDScriptParser::Node::VARIABLE: {
			const GDScriptParser::VariableNode *variable = static_cast<const GDScriptParser::VariableNode *>(p_statement);
			if (r_state.reads_after == nullptr && variable->identifier != nullptr) {
				extract_method_add_local(r_state.declared_inside, variable->identifier->name, variable->get_datatype());
			}
			collect_extract_method_expr_reads(variable->initializer, p_location, r_state);
		} break;
		case GDScriptParser::Node::CONSTANT: {
			const GDScriptParser::ConstantNode *constant = static_cast<const GDScriptParser::ConstantNode *>(p_statement);
			if (r_state.reads_after == nullptr && constant->identifier != nullptr) {
				extract_method_add_local(r_state.declared_inside, constant->identifier->name, constant->get_datatype());
			}
			collect_extract_method_expr_reads(constant->initializer, p_location, r_state);
		} break;
		case GDScriptParser::Node::ASSIGNMENT:
			collect_extract_method_expr_reads(
					static_cast<const GDScriptParser::AssignmentNode *>(p_statement),
					p_location,
					r_state);
			break;
		case GDScriptParser::Node::ASSERT: {
			const GDScriptParser::AssertNode *assert_node = static_cast<const GDScriptParser::AssertNode *>(p_statement);
			collect_extract_method_expr_reads(assert_node->condition, p_location, r_state);
			collect_extract_method_expr_reads(assert_node->message, p_location, r_state);
		} break;
		case GDScriptParser::Node::IF: {
			const GDScriptParser::IfNode *if_node = static_cast<const GDScriptParser::IfNode *>(p_statement);
			collect_extract_method_expr_reads(if_node->condition, p_location, r_state);
			collect_extract_method_suite_reads(if_node->true_block, p_location, r_state);
			collect_extract_method_suite_reads(if_node->false_block, p_location, r_state);
		} break;
		case GDScriptParser::Node::FOR: {
			const GDScriptParser::ForNode *for_node = static_cast<const GDScriptParser::ForNode *>(p_statement);
			collect_extract_method_expr_reads(for_node->list, p_location, r_state);
			collect_extract_method_suite_reads(for_node->loop, p_location, r_state);
		} break;
		case GDScriptParser::Node::WHILE: {
			const GDScriptParser::WhileNode *while_node = static_cast<const GDScriptParser::WhileNode *>(p_statement);
			collect_extract_method_expr_reads(while_node->condition, p_location, r_state);
			collect_extract_method_suite_reads(while_node->loop, p_location, r_state);
		} break;
		case GDScriptParser::Node::MATCH: {
			const GDScriptParser::MatchNode *match_node = static_cast<const GDScriptParser::MatchNode *>(p_statement);
			collect_extract_method_expr_reads(match_node->test, p_location, r_state);
			for (const GDScriptParser::MatchBranchNode *branch : match_node->branches) {
				if (branch != nullptr) {
					collect_extract_method_suite_reads(branch->block, p_location, r_state);
				}
			}
		} break;
		default:
			if (p_statement->is_expression()) {
				collect_extract_method_expr_reads(
						static_cast<const GDScriptParser::ExpressionNode *>(p_statement),
						p_location,
						r_state);
			}
			break;
	}
}

void collect_extract_method_reads_after(
		const GDScriptParser::SuiteNode *p_suite,
		const RefactorLocation &p_location,
		Vector<StringName> &r_reads_after,
		String &r_disabled_reason) {
	if (p_suite == nullptr || !r_disabled_reason.is_empty()) {
		return;
	}

	ExtractMethodReadState state;
	state.reads_after = &r_reads_after;
	for (const GDScriptParser::Node *statement : p_suite->statements) {
		if (statement == nullptr) {
			continue;
		}
		if (get_node_start_line_0(statement) >= p_location.end_line) {
			collect_extract_method_stmt_reads(statement, p_location, state);
			if (!state.disabled_reason.is_empty()) {
				r_disabled_reason = state.disabled_reason;
				return;
			}
		}
	}
}

void collect_extract_method_external_assigns_in_suite(
		const GDScriptParser::SuiteNode *p_suite,
		const RefactorLocation &p_location,
		Vector<StringName> &r_assigned_existing);

void collect_extract_method_external_assign(
		const GDScriptParser::ExpressionNode *p_expression,
		const RefactorLocation &p_location,
		Vector<StringName> &r_assigned_existing) {
	if (p_expression == nullptr) {
		return;
	}
	if (p_expression->type != GDScriptParser::Node::IDENTIFIER) {
		return;
	}

	const GDScriptParser::IdentifierNode *identifier = static_cast<const GDScriptParser::IdentifierNode *>(p_expression);
	if (!extract_method_source_inside(identifier, p_location)) {
		extract_method_add_name(r_assigned_existing, identifier->name);
	}
}

void collect_extract_method_external_assigns_in_statement(
		const GDScriptParser::Node *p_statement,
		const RefactorLocation &p_location,
		Vector<StringName> &r_assigned_existing) {
	if (p_statement == nullptr) {
		return;
	}

	switch (p_statement->type) {
		case GDScriptParser::Node::ASSIGNMENT: {
			const GDScriptParser::AssignmentNode *assignment = static_cast<const GDScriptParser::AssignmentNode *>(p_statement);
			collect_extract_method_external_assign(assignment->assignee, p_location, r_assigned_existing);
		} break;
		case GDScriptParser::Node::IF: {
			const GDScriptParser::IfNode *if_node = static_cast<const GDScriptParser::IfNode *>(p_statement);
			collect_extract_method_external_assigns_in_suite(if_node->true_block, p_location, r_assigned_existing);
			collect_extract_method_external_assigns_in_suite(if_node->false_block, p_location, r_assigned_existing);
		} break;
		case GDScriptParser::Node::FOR:
			collect_extract_method_external_assigns_in_suite(
					static_cast<const GDScriptParser::ForNode *>(p_statement)->loop,
					p_location,
					r_assigned_existing);
			break;
		case GDScriptParser::Node::WHILE:
			collect_extract_method_external_assigns_in_suite(
					static_cast<const GDScriptParser::WhileNode *>(p_statement)->loop,
					p_location,
					r_assigned_existing);
			break;
		case GDScriptParser::Node::MATCH: {
			const GDScriptParser::MatchNode *match_node = static_cast<const GDScriptParser::MatchNode *>(p_statement);
			for (const GDScriptParser::MatchBranchNode *branch : match_node->branches) {
				if (branch != nullptr) {
					collect_extract_method_external_assigns_in_suite(branch->block, p_location, r_assigned_existing);
				}
			}
		} break;
		default:
			break;
	}
}

void collect_extract_method_external_assigns_in_suite(
		const GDScriptParser::SuiteNode *p_suite,
		const RefactorLocation &p_location,
		Vector<StringName> &r_assigned_existing) {
	if (p_suite == nullptr) {
		return;
	}

	for (const GDScriptParser::Node *statement : p_suite->statements) {
		collect_extract_method_external_assigns_in_statement(statement, p_location, r_assigned_existing);
	}
}

bool find_extract_method_range(
		const RefactorLocation &p_location,
		const GDScriptParser::SuiteNode *p_suite,
		int &r_first_statement,
		int &r_last_statement) {
	if (p_suite == nullptr || !p_location.has_selection() || p_location.start_column != 0 || p_location.end_column != 0) {
		return false;
	}

	r_first_statement = -1;
	r_last_statement = -1;
	for (int i = 0; i < p_suite->statements.size(); i++) {
		const GDScriptParser::Node *statement = p_suite->statements[i];
		if (statement == nullptr) {
			continue;
		}
		if (get_node_start_line_0(statement) == p_location.start_line) {
			r_first_statement = i;
		}
		if (get_node_end_line_exclusive_0(statement) == p_location.end_line) {
			r_last_statement = i;
		}
	}

	return r_first_statement >= 0 && r_last_statement >= r_first_statement;
}

String make_unique_method_name(const GDScriptParser::ClassNode *p_class) {
	const String base = "_extracted_method";
	String name = base;
	int suffix = 2;
	String reason;
	while (!GDScriptRefactorNames::validate_identifier(name, reason) ||
			(p_class != nullptr && p_class->has_member(StringName(name)))) {
		name = vformat("%s_%d", base, suffix);
		suffix++;
		if (suffix > 1000) {
			return String();
		}
	}
	return name;
}

String build_extract_method_body(
		const Vector<String> &p_lines,
		int p_start_line,
		int p_end_line,
		const String &p_remove_indent,
		const String &p_add_indent) {
	String body;
	for (int line_index = p_start_line; line_index < p_end_line && line_index < p_lines.size(); line_index++) {
		const String line = p_lines[line_index];
		if (line.strip_edges().is_empty()) {
			body += "\n";
			continue;
		}
		if (!p_remove_indent.is_empty() && line.begins_with(p_remove_indent)) {
			body += p_add_indent + line.substr(p_remove_indent.length()) + "\n";
		} else {
			body += p_add_indent + line + "\n";
		}
	}
	return body;
}

bool extract_method_position_is_eof(
		const Vector<String> &p_lines,
		bool p_source_has_final_newline,
		int p_line) {
	return !p_source_has_final_newline && p_line == p_lines.size() && p_line > 0;
}

void set_extract_method_edit_line_end(
		RefactorTextEdit &r_edit,
		const Vector<String> &p_lines,
		bool p_source_has_final_newline,
		int p_line) {
	if (extract_method_position_is_eof(p_lines, p_source_has_final_newline, p_line)) {
		r_edit.end_line = p_line - 1;
		r_edit.end_column = p_lines[p_line - 1].length();
		return;
	}

	r_edit.end_line = p_line;
	r_edit.end_column = 0;
}

void set_extract_method_insertion(
		RefactorTextEdit &r_edit,
		const Vector<String> &p_lines,
		bool p_source_has_final_newline,
		int p_line,
		const String &p_method_text) {
	if (extract_method_position_is_eof(p_lines, p_source_has_final_newline, p_line)) {
		r_edit.start_line = p_line - 1;
		r_edit.start_column = p_lines[p_line - 1].length();
		r_edit.end_line = r_edit.start_line;
		r_edit.end_column = r_edit.start_column;
		r_edit.new_text = "\n\n" + p_method_text;
		return;
	}

	r_edit.start_line = p_line;
	r_edit.start_column = 0;
	r_edit.end_line = p_line;
	r_edit.end_column = 0;
	r_edit.new_text = "\n" + p_method_text;
}

ExtractMethodCandidate build_extract_method_candidate(
		const RefactorLocation &p_location,
		const Vector<String> &p_lines,
		bool p_source_has_final_newline,
		const GDScriptParser::ClassNode *p_class,
		const GDScriptParser::FunctionNode *p_function,
		const GDScriptParser::SuiteNode *p_suite,
		int p_first_statement,
		int p_last_statement) {
	ExtractMethodCandidate candidate;
	candidate.matched = true;

	if (p_function == nullptr || p_suite == nullptr || p_function->body == nullptr) {
		candidate.disabled_reason = "Select whole statements inside one function body.";
		return candidate;
	}
	if (p_location.start_line < 0 || p_location.start_line >= p_lines.size() ||
			p_location.end_line <= p_location.start_line || p_location.end_line > p_lines.size()) {
		candidate.disabled_reason = "Select whole statements inside one function body.";
		return candidate;
	}

	ExtractMethodReadState read_state;
	for (int i = p_first_statement; i <= p_last_statement; i++) {
		collect_extract_method_stmt_reads(p_suite->statements[i], p_location, read_state);
		if (!read_state.disabled_reason.is_empty()) {
			candidate.disabled_reason = read_state.disabled_reason;
			return candidate;
		}
	}

	Vector<StringName> reads_after;
	String disabled_reason;
	collect_extract_method_reads_after(p_suite, p_location, reads_after, disabled_reason);
	if (!disabled_reason.is_empty()) {
		candidate.disabled_reason = disabled_reason;
		return candidate;
	}

	Vector<StringName> assigned_existing;
	for (int i = p_first_statement; i <= p_last_statement; i++) {
		collect_extract_method_external_assigns_in_statement(p_suite->statements[i], p_location, assigned_existing);
	}
	if (!assigned_existing.is_empty()) {
		candidate.disabled_reason = "Cannot extract an assignment to an existing local.";
		return candidate;
	}

	Vector<ExtractMethodLocal> outputs;
	for (const ExtractMethodLocal &local : read_state.declared_inside) {
		if (string_name_vector_has(reads_after, local.name)) {
			outputs.push_back(local);
		}
	}
	if (outputs.size() > 1) {
		candidate.disabled_reason = "Extract method does not support multiple output values yet.";
		return candidate;
	}

	Vector<String> rendered_parameter_types;
	for (const ExtractMethodParameter &parameter : read_state.parameters) {
		String rendered_type;
		if (!GDScriptRefactorTypes::render_annotatable_type(parameter.datatype, rendered_type)) {
			candidate.disabled_reason = vformat("Cannot render a type for parameter '%s'.", String(parameter.name));
			return candidate;
		}
		rendered_parameter_types.push_back(rendered_type);
	}

	String rendered_return_type = "void";
	if (outputs.size() == 1 &&
			!GDScriptRefactorTypes::render_annotatable_type(outputs[0].datatype, rendered_return_type)) {
		candidate.disabled_reason = vformat("Cannot render a return type for '%s'.", String(outputs[0].name));
		return candidate;
	}

	const String method_name = make_unique_method_name(p_class);
	if (method_name.is_empty()) {
		candidate.disabled_reason = "Cannot suggest a safe method name.";
		return candidate;
	}

	const String function_indent = get_leading_whitespace(p_lines[p_function->start_line - 1]);
	const String statement_indent = get_leading_whitespace(p_lines[p_location.start_line]);
	const String method_body_indent = function_indent + "\t";

	String parameter_list;
	String argument_list;
	for (int i = 0; i < read_state.parameters.size(); i++) {
		if (i > 0) {
			parameter_list += ", ";
			argument_list += ", ";
		}
		parameter_list += String(read_state.parameters[i].name) + ": " + rendered_parameter_types[i];
		argument_list += String(read_state.parameters[i].name);
	}

	const String call = method_name + "(" + argument_list + ")";
	candidate.replacement_edit.start_line = p_location.start_line;
	candidate.replacement_edit.start_column = 0;
	set_extract_method_edit_line_end(
			candidate.replacement_edit,
			p_lines,
			p_source_has_final_newline,
			p_location.end_line);
	if (outputs.size() == 1) {
		candidate.replacement_edit.new_text = statement_indent + "var " + String(outputs[0].name) + ": " +
				rendered_return_type + " = " + call + "\n";
	} else {
		candidate.replacement_edit.new_text = statement_indent + call + "\n";
	}

	String method_text = function_indent + "func " + method_name + "(" + parameter_list + ") -> " +
			rendered_return_type + ":\n";
	method_text += build_extract_method_body(
			p_lines,
			p_location.start_line,
			p_location.end_line,
			statement_indent,
			method_body_indent);
	if (outputs.size() == 1) {
		method_text += method_body_indent + "return " + String(outputs[0].name) + "\n";
	}

	const int insertion_line = p_function->end_line;
	set_extract_method_insertion(
			candidate.method_edit,
			p_lines,
			p_source_has_final_newline,
			insertion_line,
			method_text);
	candidate.suggested_name = method_name;
	candidate.name_line = insertion_line + 1;
	candidate.name_column = function_indent.length() + String("func ").length();
	candidate.enabled = true;
	return candidate;
}

bool find_extract_method_in_suite(
		const RefactorLocation &p_location,
		const Vector<String> &p_lines,
		bool p_source_has_final_newline,
		const GDScriptParser::ClassNode *p_class,
		const GDScriptParser::FunctionNode *p_function,
		const GDScriptParser::SuiteNode *p_suite,
		ExtractMethodCandidate &r_candidate);

bool find_extract_method_in_children(
		const RefactorLocation &p_location,
		const Vector<String> &p_lines,
		bool p_source_has_final_newline,
		const GDScriptParser::ClassNode *p_class,
		const GDScriptParser::FunctionNode *p_function,
		const GDScriptParser::Node *p_statement,
		ExtractMethodCandidate &r_candidate) {
	if (p_statement == nullptr) {
		return false;
	}

	switch (p_statement->type) {
		case GDScriptParser::Node::IF: {
			const GDScriptParser::IfNode *if_node = static_cast<const GDScriptParser::IfNode *>(p_statement);
			return find_extract_method_in_suite(
						   p_location,
						   p_lines,
						   p_source_has_final_newline,
						   p_class,
						   p_function,
						   if_node->true_block,
						   r_candidate) ||
					find_extract_method_in_suite(
							p_location,
							p_lines,
							p_source_has_final_newline,
							p_class,
							p_function,
							if_node->false_block,
							r_candidate);
		}
		case GDScriptParser::Node::FOR:
			return find_extract_method_in_suite(
					p_location,
					p_lines,
					p_source_has_final_newline,
					p_class,
					p_function,
					static_cast<const GDScriptParser::ForNode *>(p_statement)->loop,
					r_candidate);
		case GDScriptParser::Node::WHILE:
			return find_extract_method_in_suite(
					p_location,
					p_lines,
					p_source_has_final_newline,
					p_class,
					p_function,
					static_cast<const GDScriptParser::WhileNode *>(p_statement)->loop,
					r_candidate);
		case GDScriptParser::Node::MATCH: {
			const GDScriptParser::MatchNode *match_node = static_cast<const GDScriptParser::MatchNode *>(p_statement);
			for (const GDScriptParser::MatchBranchNode *branch : match_node->branches) {
				if (branch != nullptr &&
						find_extract_method_in_suite(
								p_location,
								p_lines,
								p_source_has_final_newline,
								p_class,
								p_function,
								branch->block,
								r_candidate)) {
					return true;
				}
			}
		} break;
		default:
			break;
	}
	return false;
}

bool find_extract_method_in_suite(
		const RefactorLocation &p_location,
		const Vector<String> &p_lines,
		bool p_source_has_final_newline,
		const GDScriptParser::ClassNode *p_class,
		const GDScriptParser::FunctionNode *p_function,
		const GDScriptParser::SuiteNode *p_suite,
		ExtractMethodCandidate &r_candidate) {
	if (p_suite == nullptr) {
		return false;
	}

	int first_statement = -1;
	int last_statement = -1;
	if (find_extract_method_range(p_location, p_suite, first_statement, last_statement)) {
		r_candidate = build_extract_method_candidate(
				p_location,
				p_lines,
				p_source_has_final_newline,
				p_class,
				p_function,
				p_suite,
				first_statement,
				last_statement);
		return true;
	}

	for (const GDScriptParser::Node *statement : p_suite->statements) {
		if (find_extract_method_in_children(
					p_location,
					p_lines,
					p_source_has_final_newline,
					p_class,
					p_function,
					statement,
					r_candidate)) {
			return true;
		}
	}
	return false;
}

bool find_extract_method_in_function(
		const RefactorLocation &p_location,
		const Vector<String> &p_lines,
		bool p_source_has_final_newline,
		const GDScriptParser::ClassNode *p_class,
		const GDScriptParser::FunctionNode *p_function,
		ExtractMethodCandidate &r_candidate) {
	if (p_function == nullptr || p_function->body == nullptr) {
		return false;
	}
	return find_extract_method_in_suite(
			p_location,
			p_lines,
			p_source_has_final_newline,
			p_class,
			p_function,
			p_function->body,
			r_candidate);
}

bool find_extract_method_in_class(
		const RefactorLocation &p_location,
		const Vector<String> &p_lines,
		bool p_source_has_final_newline,
		const GDScriptParser::ClassNode *p_class,
		ExtractMethodCandidate &r_candidate) {
	if (p_class == nullptr) {
		return false;
	}

	for (const GDScriptParser::ClassNode::Member &member : p_class->members) {
		switch (member.type) {
			case GDScriptParser::ClassNode::Member::FUNCTION:
				if (find_extract_method_in_function(
							p_location,
							p_lines,
							p_source_has_final_newline,
							p_class,
							member.function,
							r_candidate)) {
					return true;
				}
				break;
			case GDScriptParser::ClassNode::Member::CLASS:
				if (find_extract_method_in_class(p_location, p_lines, p_source_has_final_newline, member.m_class, r_candidate)) {
					return true;
				}
				break;
			default:
				break;
		}
	}
	return false;
}

bool find_extract_variable_in_function(const RefactorLocation &p_location, const Vector<String> &p_lines, const GDScriptParser::FunctionNode *p_function, ExtractVariableCandidate &r_candidate) {
	if (p_function == nullptr || p_function->body == nullptr) {
		return false;
	}
	return find_extract_variable_in_suite(p_location, p_lines, p_function->body, r_candidate);
}

bool find_extract_variable_in_class(const RefactorLocation &p_location, const Vector<String> &p_lines, const GDScriptParser::ClassNode *p_class, ExtractVariableCandidate &r_candidate) {
	if (p_class == nullptr) {
		return false;
	}

	for (const GDScriptParser::ClassNode::Member &member : p_class->members) {
		switch (member.type) {
			case GDScriptParser::ClassNode::Member::FUNCTION:
				if (find_extract_variable_in_function(p_location, p_lines, member.function, r_candidate)) {
					return true;
				}
				break;
			case GDScriptParser::ClassNode::Member::CLASS:
				if (find_extract_variable_in_class(p_location, p_lines, member.m_class, r_candidate)) {
					return true;
				}
				break;
			default:
				break;
		}
	}
	return false;
}

bool find_extract_variable_in_suite(const RefactorLocation &p_location, const Vector<String> &p_lines, const GDScriptParser::SuiteNode *p_suite, ExtractVariableCandidate &r_candidate) {
	if (p_suite == nullptr) {
		return false;
	}

	for (const GDScriptParser::Node *statement : p_suite->statements) {
		if (statement == nullptr) {
			continue;
		}

		switch (statement->type) {
			case GDScriptParser::Node::VARIABLE: {
				const GDScriptParser::VariableNode *variable = static_cast<const GDScriptParser::VariableNode *>(statement);
				if (try_extract_direct_expression(p_location, p_lines, variable->initializer, statement, p_suite, r_candidate)) {
					return true;
				}
			} break;
			case GDScriptParser::Node::CONSTANT: {
				const GDScriptParser::ConstantNode *constant = static_cast<const GDScriptParser::ConstantNode *>(statement);
				if (try_extract_direct_expression(p_location, p_lines, constant->initializer, statement, p_suite, r_candidate)) {
					return true;
				}
			} break;
			case GDScriptParser::Node::RETURN: {
				const GDScriptParser::ReturnNode *return_node = static_cast<const GDScriptParser::ReturnNode *>(statement);
				if (try_extract_direct_expression(p_location, p_lines, return_node->return_value, statement, p_suite, r_candidate)) {
					return true;
				}
			} break;
			case GDScriptParser::Node::ASSIGNMENT: {
				const GDScriptParser::AssignmentNode *assignment = static_cast<const GDScriptParser::AssignmentNode *>(statement);
				if (expression_matches_selection(p_location, p_lines, assignment->assigned_value) && !is_safe_extract_assignment(assignment)) {
					r_candidate.matched = true;
					r_candidate.disabled_reason = "Cannot safely extract this assignment expression.";
					return true;
				}
				if (try_extract_direct_expression(p_location, p_lines, assignment->assigned_value, statement, p_suite, r_candidate)) {
					return true;
				}
			} break;
			case GDScriptParser::Node::ASSERT: {
				const GDScriptParser::AssertNode *assert_node = static_cast<const GDScriptParser::AssertNode *>(statement);
				if (expression_matches_selection(p_location, p_lines, assert_node->condition) ||
						expression_matches_selection(p_location, p_lines, assert_node->message)) {
					r_candidate.matched = true;
					r_candidate.disabled_reason = "Cannot safely extract assert expressions.";
					return true;
				}
			} break;
			case GDScriptParser::Node::IF: {
				const GDScriptParser::IfNode *if_node = static_cast<const GDScriptParser::IfNode *>(statement);
				if (try_extract_direct_expression(p_location, p_lines, if_node->condition, statement, p_suite, r_candidate) ||
						find_extract_variable_in_suite(p_location, p_lines, if_node->true_block, r_candidate) ||
						find_extract_variable_in_suite(p_location, p_lines, if_node->false_block, r_candidate)) {
					return true;
				}
			} break;
			case GDScriptParser::Node::FOR: {
				const GDScriptParser::ForNode *for_node = static_cast<const GDScriptParser::ForNode *>(statement);
				if (try_extract_direct_expression(p_location, p_lines, for_node->list, statement, p_suite, r_candidate) ||
						find_extract_variable_in_suite(p_location, p_lines, for_node->loop, r_candidate)) {
					return true;
				}
			} break;
			case GDScriptParser::Node::WHILE: {
				const GDScriptParser::WhileNode *while_node = static_cast<const GDScriptParser::WhileNode *>(statement);
				if (expression_matches_selection(p_location, p_lines, while_node->condition)) {
					r_candidate.matched = true;
					r_candidate.disabled_reason = "Cannot safely extract a while loop condition.";
					return true;
				}
				if (find_extract_variable_in_suite(p_location, p_lines, while_node->loop, r_candidate)) {
					return true;
				}
			} break;
			case GDScriptParser::Node::MATCH: {
				const GDScriptParser::MatchNode *match_node = static_cast<const GDScriptParser::MatchNode *>(statement);
				if (try_extract_direct_expression(p_location, p_lines, match_node->test, statement, p_suite, r_candidate)) {
					return true;
				}
				for (const GDScriptParser::MatchBranchNode *branch : match_node->branches) {
					if (branch != nullptr && find_extract_variable_in_suite(p_location, p_lines, branch->block, r_candidate)) {
						return true;
					}
				}
			} break;
			default:
				break;
		}
	}
	return false;
}

bool find_inline_variable_target_in_suite(const RefactorLocation &p_location, const Vector<String> &p_lines, const GDScriptParser::SuiteNode *p_suite, const GDScriptParser::VariableNode *&r_variable, const GDScriptParser::FunctionNode *&r_function);
bool find_inline_variable_target_in_expression(const RefactorLocation &p_location, const Vector<String> &p_lines, const GDScriptParser::ExpressionNode *p_expression, const GDScriptParser::VariableNode *&r_variable, const GDScriptParser::FunctionNode *&r_function);

bool find_inline_variable_declaration(const RefactorLocation &p_location, const Vector<String> &p_lines, const GDScriptParser::SuiteNode *p_suite, const GDScriptParser::VariableNode *p_variable, const GDScriptParser::VariableNode *&r_variable, const GDScriptParser::FunctionNode *&r_function) {
	if (p_suite == nullptr || p_suite->parent_function == nullptr || p_variable == nullptr || p_variable->identifier == nullptr) {
		return false;
	}
	const int line_index = p_variable->start_line - 1;
	if (line_index < 0 || line_index >= p_lines.size()) {
		return false;
	}
	const String line = p_lines[line_index];
	int name_start = 0;
	int name_end = 0;
	if (!get_identifier_text_span(p_lines, line_index, p_variable->identifier, nullptr, name_start, name_end)) {
		return false;
	}
	int declaration_start = name_start;
	int keyword_start = 0;
	if (get_node_text_start(p_lines, line_index, p_variable, "var", keyword_start)) {
		declaration_start = keyword_start;
	} else {
		keyword_start = line.find("var");
		if (keyword_start >= 0 && keyword_start <= name_start) {
			declaration_start = keyword_start;
		}
	}
	const int equal_index = line.find("=", name_end);
	const int declaration_end = equal_index >= 0 ? equal_index : name_end;
	if (!caret_on_segment(p_location, line_index, declaration_start, declaration_end)) {
		return false;
	}
	r_variable = p_variable;
	r_function = p_suite->parent_function;
	return true;
}

bool find_inline_variable_target_in_identifier(const RefactorLocation &p_location, const Vector<String> &p_lines, const GDScriptParser::IdentifierNode *p_identifier, const GDScriptParser::VariableNode *&r_variable, const GDScriptParser::FunctionNode *&r_function) {
	if (p_identifier == nullptr || p_identifier->source != GDScriptParser::IdentifierNode::LOCAL_VARIABLE || p_identifier->variable_source == nullptr) {
		return false;
	}
	if (p_identifier->source_function != nullptr &&
			(p_location.start_line + 1 < p_identifier->source_function->start_line ||
					p_location.start_line + 1 > p_identifier->source_function->end_line)) {
		return false;
	}
	const int line_index = p_identifier->start_line - 1;
	int name_start = 0;
	int name_end = 0;
	if (!get_identifier_text_span(p_lines, line_index, p_identifier, &p_location, name_start, name_end) ||
			!caret_on_segment(p_location, line_index, name_start, name_end)) {
		return false;
	}
	r_variable = p_identifier->variable_source;
	r_function = p_identifier->source_function;
	return true;
}

bool find_inline_variable_target_in_lambda(const RefactorLocation &p_location, const Vector<String> &p_lines, const GDScriptParser::LambdaNode *p_lambda, const GDScriptParser::VariableNode *&r_variable, const GDScriptParser::FunctionNode *&r_function) {
	if (p_lambda == nullptr || p_lambda->function == nullptr) {
		return false;
	}
	return find_inline_variable_target_in_suite(p_location, p_lines, p_lambda->function->body, r_variable, r_function);
}

bool find_inline_variable_target_in_expression(const RefactorLocation &p_location, const Vector<String> &p_lines, const GDScriptParser::ExpressionNode *p_expression, const GDScriptParser::VariableNode *&r_variable, const GDScriptParser::FunctionNode *&r_function) {
	if (p_expression == nullptr) {
		return false;
	}

	switch (p_expression->type) {
		case GDScriptParser::Node::ARRAY: {
			const GDScriptParser::ArrayNode *array = static_cast<const GDScriptParser::ArrayNode *>(p_expression);
			for (const GDScriptParser::ExpressionNode *element : array->elements) {
				if (find_inline_variable_target_in_expression(p_location, p_lines, element, r_variable, r_function)) {
					return true;
				}
			}
		} break;
		case GDScriptParser::Node::ASSIGNMENT: {
			const GDScriptParser::AssignmentNode *assignment = static_cast<const GDScriptParser::AssignmentNode *>(p_expression);
			return find_inline_variable_target_in_expression(p_location, p_lines, assignment->assignee, r_variable, r_function) ||
					find_inline_variable_target_in_expression(p_location, p_lines, assignment->assigned_value, r_variable, r_function);
		}
		case GDScriptParser::Node::AWAIT:
			return find_inline_variable_target_in_expression(p_location, p_lines, static_cast<const GDScriptParser::AwaitNode *>(p_expression)->to_await, r_variable, r_function);
		case GDScriptParser::Node::BINARY_OPERATOR: {
			const GDScriptParser::BinaryOpNode *binary = static_cast<const GDScriptParser::BinaryOpNode *>(p_expression);
			return find_inline_variable_target_in_expression(p_location, p_lines, binary->left_operand, r_variable, r_function) ||
					find_inline_variable_target_in_expression(p_location, p_lines, binary->right_operand, r_variable, r_function);
		}
		case GDScriptParser::Node::CALL: {
			const GDScriptParser::CallNode *call = static_cast<const GDScriptParser::CallNode *>(p_expression);
			if (find_inline_variable_target_in_expression(p_location, p_lines, call->callee, r_variable, r_function)) {
				return true;
			}
			for (const GDScriptParser::ExpressionNode *argument : call->arguments) {
				if (find_inline_variable_target_in_expression(p_location, p_lines, argument, r_variable, r_function)) {
					return true;
				}
			}
		} break;
		case GDScriptParser::Node::CAST:
			return find_inline_variable_target_in_expression(p_location, p_lines, static_cast<const GDScriptParser::CastNode *>(p_expression)->operand, r_variable, r_function);
		case GDScriptParser::Node::DICTIONARY: {
			const GDScriptParser::DictionaryNode *dictionary = static_cast<const GDScriptParser::DictionaryNode *>(p_expression);
			for (const GDScriptParser::DictionaryNode::Pair &pair : dictionary->elements) {
				if (find_inline_variable_target_in_expression(p_location, p_lines, pair.key, r_variable, r_function) ||
						find_inline_variable_target_in_expression(p_location, p_lines, pair.value, r_variable, r_function)) {
					return true;
				}
			}
		} break;
		case GDScriptParser::Node::IDENTIFIER:
			return find_inline_variable_target_in_identifier(p_location, p_lines, static_cast<const GDScriptParser::IdentifierNode *>(p_expression), r_variable, r_function);
		case GDScriptParser::Node::LAMBDA:
			return find_inline_variable_target_in_lambda(p_location, p_lines, static_cast<const GDScriptParser::LambdaNode *>(p_expression), r_variable, r_function);
		case GDScriptParser::Node::PRELOAD:
			return find_inline_variable_target_in_expression(p_location, p_lines, static_cast<const GDScriptParser::PreloadNode *>(p_expression)->path, r_variable, r_function);
		case GDScriptParser::Node::SUBSCRIPT: {
			const GDScriptParser::SubscriptNode *subscript = static_cast<const GDScriptParser::SubscriptNode *>(p_expression);
			if (find_inline_variable_target_in_expression(p_location, p_lines, subscript->base, r_variable, r_function)) {
				return true;
			}
			if (!subscript->is_attribute && find_inline_variable_target_in_expression(p_location, p_lines, subscript->index, r_variable, r_function)) {
				return true;
			}
		} break;
		case GDScriptParser::Node::TERNARY_OPERATOR: {
			const GDScriptParser::TernaryOpNode *ternary = static_cast<const GDScriptParser::TernaryOpNode *>(p_expression);
			return find_inline_variable_target_in_expression(p_location, p_lines, ternary->condition, r_variable, r_function) ||
					find_inline_variable_target_in_expression(p_location, p_lines, ternary->true_expr, r_variable, r_function) ||
					find_inline_variable_target_in_expression(p_location, p_lines, ternary->false_expr, r_variable, r_function);
		}
		case GDScriptParser::Node::TYPE_TEST:
			return find_inline_variable_target_in_expression(p_location, p_lines, static_cast<const GDScriptParser::TypeTestNode *>(p_expression)->operand, r_variable, r_function);
		case GDScriptParser::Node::UNARY_OPERATOR:
			return find_inline_variable_target_in_expression(p_location, p_lines, static_cast<const GDScriptParser::UnaryOpNode *>(p_expression)->operand, r_variable, r_function);
		default:
			break;
	}
	return false;
}

bool find_inline_variable_target_in_suite(const RefactorLocation &p_location, const Vector<String> &p_lines, const GDScriptParser::SuiteNode *p_suite, const GDScriptParser::VariableNode *&r_variable, const GDScriptParser::FunctionNode *&r_function) {
	if (p_suite == nullptr) {
		return false;
	}

	for (const GDScriptParser::Node *statement : p_suite->statements) {
		if (statement == nullptr) {
			continue;
		}
		switch (statement->type) {
			case GDScriptParser::Node::VARIABLE: {
				const GDScriptParser::VariableNode *variable = static_cast<const GDScriptParser::VariableNode *>(statement);
				if (find_inline_variable_declaration(p_location, p_lines, p_suite, variable, r_variable, r_function) ||
						find_inline_variable_target_in_expression(p_location, p_lines, variable->initializer, r_variable, r_function)) {
					return true;
				}
			} break;
			case GDScriptParser::Node::CONSTANT: {
				const GDScriptParser::ConstantNode *constant = static_cast<const GDScriptParser::ConstantNode *>(statement);
				if (find_inline_variable_target_in_expression(p_location, p_lines, constant->initializer, r_variable, r_function)) {
					return true;
				}
			} break;
			case GDScriptParser::Node::RETURN: {
				const GDScriptParser::ReturnNode *return_node = static_cast<const GDScriptParser::ReturnNode *>(statement);
				if (find_inline_variable_target_in_expression(p_location, p_lines, return_node->return_value, r_variable, r_function)) {
					return true;
				}
			} break;
			case GDScriptParser::Node::ASSIGNMENT:
				if (find_inline_variable_target_in_expression(p_location, p_lines, static_cast<const GDScriptParser::AssignmentNode *>(statement), r_variable, r_function)) {
					return true;
				}
				break;
			case GDScriptParser::Node::ASSERT: {
				const GDScriptParser::AssertNode *assert_node = static_cast<const GDScriptParser::AssertNode *>(statement);
				if (find_inline_variable_target_in_expression(p_location, p_lines, assert_node->condition, r_variable, r_function) ||
						find_inline_variable_target_in_expression(p_location, p_lines, assert_node->message, r_variable, r_function)) {
					return true;
				}
			} break;
			case GDScriptParser::Node::IF: {
				const GDScriptParser::IfNode *if_node = static_cast<const GDScriptParser::IfNode *>(statement);
				if (find_inline_variable_target_in_expression(p_location, p_lines, if_node->condition, r_variable, r_function) ||
						find_inline_variable_target_in_suite(p_location, p_lines, if_node->true_block, r_variable, r_function) ||
						find_inline_variable_target_in_suite(p_location, p_lines, if_node->false_block, r_variable, r_function)) {
					return true;
				}
			} break;
			case GDScriptParser::Node::FOR: {
				const GDScriptParser::ForNode *for_node = static_cast<const GDScriptParser::ForNode *>(statement);
				if (find_inline_variable_target_in_expression(p_location, p_lines, for_node->list, r_variable, r_function) ||
						find_inline_variable_target_in_suite(p_location, p_lines, for_node->loop, r_variable, r_function)) {
					return true;
				}
			} break;
			case GDScriptParser::Node::WHILE: {
				const GDScriptParser::WhileNode *while_node = static_cast<const GDScriptParser::WhileNode *>(statement);
				if (find_inline_variable_target_in_expression(p_location, p_lines, while_node->condition, r_variable, r_function) ||
						find_inline_variable_target_in_suite(p_location, p_lines, while_node->loop, r_variable, r_function)) {
					return true;
				}
			} break;
			case GDScriptParser::Node::MATCH: {
				const GDScriptParser::MatchNode *match_node = static_cast<const GDScriptParser::MatchNode *>(statement);
				if (find_inline_variable_target_in_expression(p_location, p_lines, match_node->test, r_variable, r_function)) {
					return true;
				}
				for (const GDScriptParser::MatchBranchNode *branch : match_node->branches) {
					if (branch != nullptr &&
							(find_inline_variable_target_in_suite(p_location, p_lines, branch->guard_body, r_variable, r_function) ||
									find_inline_variable_target_in_suite(p_location, p_lines, branch->block, r_variable, r_function))) {
						return true;
					}
				}
			} break;
			default:
				if (statement->is_expression() && find_inline_variable_target_in_expression(p_location, p_lines, static_cast<const GDScriptParser::ExpressionNode *>(statement), r_variable, r_function)) {
					return true;
				}
				break;
		}
	}
	return false;
}

bool find_inline_variable_target_in_function(const RefactorLocation &p_location, const Vector<String> &p_lines, const GDScriptParser::FunctionNode *p_function, const GDScriptParser::VariableNode *&r_variable, const GDScriptParser::FunctionNode *&r_function) {
	if (p_function == nullptr) {
		return false;
	}
	return find_inline_variable_target_in_suite(p_location, p_lines, p_function->body, r_variable, r_function);
}

bool find_inline_variable_target_in_class(const RefactorLocation &p_location, const Vector<String> &p_lines, const GDScriptParser::ClassNode *p_class, const GDScriptParser::VariableNode *&r_variable, const GDScriptParser::FunctionNode *&r_function) {
	if (p_class == nullptr) {
		return false;
	}

	for (const GDScriptParser::ClassNode::Member &member : p_class->members) {
		switch (member.type) {
			case GDScriptParser::ClassNode::Member::FUNCTION:
				if (find_inline_variable_target_in_function(p_location, p_lines, member.function, r_variable, r_function)) {
					return true;
				}
				break;
			case GDScriptParser::ClassNode::Member::CLASS:
				if (find_inline_variable_target_in_class(p_location, p_lines, member.m_class, r_variable, r_function)) {
					return true;
				}
				break;
			default:
				break;
		}
	}
	return false;
}

struct InlineVariableUsageCollection {
	Vector<InlineVariableUse> reads;
	bool has_assignment_target = false;
	bool has_lambda_capture = false;
};

void collect_inline_variable_uses_in_suite(const GDScriptParser::SuiteNode *p_suite, const GDScriptParser::VariableNode *p_target, const GDScriptParser::FunctionNode *p_target_function, const GDScriptParser::FunctionNode *p_current_function, InlineVariableUsageCollection &r_collection);
void collect_inline_variable_uses_in_expression(const GDScriptParser::ExpressionNode *p_expression, const GDScriptParser::Node *p_parent, bool p_assignment_target, const GDScriptParser::VariableNode *p_target, const GDScriptParser::FunctionNode *p_target_function, const GDScriptParser::FunctionNode *p_current_function, InlineVariableUsageCollection &r_collection);

void collect_inline_variable_identifier_use(const GDScriptParser::IdentifierNode *p_identifier, const GDScriptParser::Node *p_parent, bool p_assignment_target, const GDScriptParser::VariableNode *p_target, const GDScriptParser::FunctionNode *p_target_function, const GDScriptParser::FunctionNode *p_current_function, InlineVariableUsageCollection &r_collection) {
	if (p_identifier == nullptr || p_identifier->source != GDScriptParser::IdentifierNode::LOCAL_VARIABLE || p_identifier->variable_source != p_target) {
		return;
	}
	if (p_current_function != p_target_function) {
		r_collection.has_lambda_capture = true;
		return;
	}
	if (p_assignment_target) {
		r_collection.has_assignment_target = true;
		return;
	}
	InlineVariableUse use;
	use.identifier = p_identifier;
	use.parent = p_parent;
	r_collection.reads.push_back(use);
}

void collect_inline_variable_uses_in_expression(const GDScriptParser::ExpressionNode *p_expression, const GDScriptParser::Node *p_parent, bool p_assignment_target, const GDScriptParser::VariableNode *p_target, const GDScriptParser::FunctionNode *p_target_function, const GDScriptParser::FunctionNode *p_current_function, InlineVariableUsageCollection &r_collection) {
	if (p_expression == nullptr) {
		return;
	}

	switch (p_expression->type) {
		case GDScriptParser::Node::ARRAY: {
			const GDScriptParser::ArrayNode *array = static_cast<const GDScriptParser::ArrayNode *>(p_expression);
			for (const GDScriptParser::ExpressionNode *element : array->elements) {
				collect_inline_variable_uses_in_expression(element, p_expression, false, p_target, p_target_function, p_current_function, r_collection);
			}
		} break;
		case GDScriptParser::Node::ASSIGNMENT: {
			const GDScriptParser::AssignmentNode *assignment = static_cast<const GDScriptParser::AssignmentNode *>(p_expression);
			collect_inline_variable_uses_in_expression(assignment->assignee, p_expression, true, p_target, p_target_function, p_current_function, r_collection);
			collect_inline_variable_uses_in_expression(assignment->assigned_value, p_expression, false, p_target, p_target_function, p_current_function, r_collection);
		} break;
		case GDScriptParser::Node::AWAIT:
			collect_inline_variable_uses_in_expression(static_cast<const GDScriptParser::AwaitNode *>(p_expression)->to_await, p_expression, false, p_target, p_target_function, p_current_function, r_collection);
			break;
		case GDScriptParser::Node::BINARY_OPERATOR: {
			const GDScriptParser::BinaryOpNode *binary = static_cast<const GDScriptParser::BinaryOpNode *>(p_expression);
			collect_inline_variable_uses_in_expression(binary->left_operand, p_expression, false, p_target, p_target_function, p_current_function, r_collection);
			collect_inline_variable_uses_in_expression(binary->right_operand, p_expression, false, p_target, p_target_function, p_current_function, r_collection);
		} break;
		case GDScriptParser::Node::CALL: {
			const GDScriptParser::CallNode *call = static_cast<const GDScriptParser::CallNode *>(p_expression);
			collect_inline_variable_uses_in_expression(call->callee, p_expression, false, p_target, p_target_function, p_current_function, r_collection);
			for (const GDScriptParser::ExpressionNode *argument : call->arguments) {
				collect_inline_variable_uses_in_expression(argument, p_expression, false, p_target, p_target_function, p_current_function, r_collection);
			}
		} break;
		case GDScriptParser::Node::CAST:
			collect_inline_variable_uses_in_expression(static_cast<const GDScriptParser::CastNode *>(p_expression)->operand, p_expression, false, p_target, p_target_function, p_current_function, r_collection);
			break;
		case GDScriptParser::Node::DICTIONARY: {
			const GDScriptParser::DictionaryNode *dictionary = static_cast<const GDScriptParser::DictionaryNode *>(p_expression);
			for (const GDScriptParser::DictionaryNode::Pair &pair : dictionary->elements) {
				collect_inline_variable_uses_in_expression(pair.key, p_expression, false, p_target, p_target_function, p_current_function, r_collection);
				collect_inline_variable_uses_in_expression(pair.value, p_expression, false, p_target, p_target_function, p_current_function, r_collection);
			}
		} break;
		case GDScriptParser::Node::IDENTIFIER:
			collect_inline_variable_identifier_use(static_cast<const GDScriptParser::IdentifierNode *>(p_expression), p_parent, p_assignment_target, p_target, p_target_function, p_current_function, r_collection);
			break;
		case GDScriptParser::Node::LAMBDA: {
			const GDScriptParser::LambdaNode *lambda = static_cast<const GDScriptParser::LambdaNode *>(p_expression);
			if (lambda->function != nullptr) {
				collect_inline_variable_uses_in_suite(lambda->function->body, p_target, p_target_function, lambda->function, r_collection);
			}
		} break;
		case GDScriptParser::Node::PRELOAD:
			collect_inline_variable_uses_in_expression(static_cast<const GDScriptParser::PreloadNode *>(p_expression)->path, p_expression, false, p_target, p_target_function, p_current_function, r_collection);
			break;
		case GDScriptParser::Node::SUBSCRIPT: {
			const GDScriptParser::SubscriptNode *subscript = static_cast<const GDScriptParser::SubscriptNode *>(p_expression);
			collect_inline_variable_uses_in_expression(subscript->base, p_expression, p_assignment_target, p_target, p_target_function, p_current_function, r_collection);
			if (!subscript->is_attribute) {
				collect_inline_variable_uses_in_expression(subscript->index, p_expression, false, p_target, p_target_function, p_current_function, r_collection);
			}
		} break;
		case GDScriptParser::Node::TERNARY_OPERATOR: {
			const GDScriptParser::TernaryOpNode *ternary = static_cast<const GDScriptParser::TernaryOpNode *>(p_expression);
			collect_inline_variable_uses_in_expression(ternary->condition, p_expression, false, p_target, p_target_function, p_current_function, r_collection);
			collect_inline_variable_uses_in_expression(ternary->true_expr, p_expression, false, p_target, p_target_function, p_current_function, r_collection);
			collect_inline_variable_uses_in_expression(ternary->false_expr, p_expression, false, p_target, p_target_function, p_current_function, r_collection);
		} break;
		case GDScriptParser::Node::TYPE_TEST:
			collect_inline_variable_uses_in_expression(static_cast<const GDScriptParser::TypeTestNode *>(p_expression)->operand, p_expression, false, p_target, p_target_function, p_current_function, r_collection);
			break;
		case GDScriptParser::Node::UNARY_OPERATOR:
			collect_inline_variable_uses_in_expression(static_cast<const GDScriptParser::UnaryOpNode *>(p_expression)->operand, p_expression, false, p_target, p_target_function, p_current_function, r_collection);
			break;
		default:
			break;
	}
}

void collect_inline_variable_uses_in_suite(const GDScriptParser::SuiteNode *p_suite, const GDScriptParser::VariableNode *p_target, const GDScriptParser::FunctionNode *p_target_function, const GDScriptParser::FunctionNode *p_current_function, InlineVariableUsageCollection &r_collection) {
	if (p_suite == nullptr) {
		return;
	}

	for (const GDScriptParser::Node *statement : p_suite->statements) {
		if (statement == nullptr) {
			continue;
		}
		switch (statement->type) {
			case GDScriptParser::Node::VARIABLE: {
				const GDScriptParser::VariableNode *variable = static_cast<const GDScriptParser::VariableNode *>(statement);
				collect_inline_variable_uses_in_expression(variable->initializer, statement, false, p_target, p_target_function, p_current_function, r_collection);
			} break;
			case GDScriptParser::Node::CONSTANT: {
				const GDScriptParser::ConstantNode *constant = static_cast<const GDScriptParser::ConstantNode *>(statement);
				collect_inline_variable_uses_in_expression(constant->initializer, statement, false, p_target, p_target_function, p_current_function, r_collection);
			} break;
			case GDScriptParser::Node::RETURN: {
				const GDScriptParser::ReturnNode *return_node = static_cast<const GDScriptParser::ReturnNode *>(statement);
				collect_inline_variable_uses_in_expression(return_node->return_value, statement, false, p_target, p_target_function, p_current_function, r_collection);
			} break;
			case GDScriptParser::Node::ASSIGNMENT:
				collect_inline_variable_uses_in_expression(static_cast<const GDScriptParser::AssignmentNode *>(statement), nullptr, false, p_target, p_target_function, p_current_function, r_collection);
				break;
			case GDScriptParser::Node::ASSERT: {
				const GDScriptParser::AssertNode *assert_node = static_cast<const GDScriptParser::AssertNode *>(statement);
				collect_inline_variable_uses_in_expression(assert_node->condition, statement, false, p_target, p_target_function, p_current_function, r_collection);
				collect_inline_variable_uses_in_expression(assert_node->message, statement, false, p_target, p_target_function, p_current_function, r_collection);
			} break;
			case GDScriptParser::Node::IF: {
				const GDScriptParser::IfNode *if_node = static_cast<const GDScriptParser::IfNode *>(statement);
				collect_inline_variable_uses_in_expression(if_node->condition, statement, false, p_target, p_target_function, p_current_function, r_collection);
				collect_inline_variable_uses_in_suite(if_node->true_block, p_target, p_target_function, p_current_function, r_collection);
				collect_inline_variable_uses_in_suite(if_node->false_block, p_target, p_target_function, p_current_function, r_collection);
			} break;
			case GDScriptParser::Node::FOR: {
				const GDScriptParser::ForNode *for_node = static_cast<const GDScriptParser::ForNode *>(statement);
				collect_inline_variable_uses_in_expression(for_node->list, statement, false, p_target, p_target_function, p_current_function, r_collection);
				collect_inline_variable_uses_in_suite(for_node->loop, p_target, p_target_function, p_current_function, r_collection);
			} break;
			case GDScriptParser::Node::WHILE: {
				const GDScriptParser::WhileNode *while_node = static_cast<const GDScriptParser::WhileNode *>(statement);
				collect_inline_variable_uses_in_expression(while_node->condition, statement, false, p_target, p_target_function, p_current_function, r_collection);
				collect_inline_variable_uses_in_suite(while_node->loop, p_target, p_target_function, p_current_function, r_collection);
			} break;
			case GDScriptParser::Node::MATCH: {
				const GDScriptParser::MatchNode *match_node = static_cast<const GDScriptParser::MatchNode *>(statement);
				collect_inline_variable_uses_in_expression(match_node->test, statement, false, p_target, p_target_function, p_current_function, r_collection);
				for (const GDScriptParser::MatchBranchNode *branch : match_node->branches) {
					if (branch != nullptr) {
						collect_inline_variable_uses_in_suite(branch->guard_body, p_target, p_target_function, p_current_function, r_collection);
						collect_inline_variable_uses_in_suite(branch->block, p_target, p_target_function, p_current_function, r_collection);
					}
				}
			} break;
			default:
				if (statement->is_expression()) {
					collect_inline_variable_uses_in_expression(static_cast<const GDScriptParser::ExpressionNode *>(statement), nullptr, false, p_target, p_target_function, p_current_function, r_collection);
				}
				break;
		}
	}
}

void collect_inline_variable_uses_in_class(const GDScriptParser::ClassNode *p_class, const GDScriptParser::VariableNode *p_target, const GDScriptParser::FunctionNode *p_target_function, InlineVariableUsageCollection &r_collection) {
	if (p_class == nullptr) {
		return;
	}
	for (const GDScriptParser::ClassNode::Member &member : p_class->members) {
		switch (member.type) {
			case GDScriptParser::ClassNode::Member::FUNCTION:
				if (member.function != nullptr) {
					collect_inline_variable_uses_in_suite(member.function->body, p_target, p_target_function, member.function, r_collection);
				}
				break;
			case GDScriptParser::ClassNode::Member::CLASS:
				collect_inline_variable_uses_in_class(member.m_class, p_target, p_target_function, r_collection);
				break;
			default:
				break;
		}
	}
}

bool expression_has_side_effects(const GDScriptParser::ExpressionNode *p_expression) {
	if (p_expression == nullptr) {
		return false;
	}

	switch (p_expression->type) {
		case GDScriptParser::Node::ASSIGNMENT:
		case GDScriptParser::Node::AWAIT:
		case GDScriptParser::Node::CALL:
		case GDScriptParser::Node::GET_NODE:
		case GDScriptParser::Node::LAMBDA:
		case GDScriptParser::Node::PRELOAD:
			return true;
		case GDScriptParser::Node::ARRAY: {
			const GDScriptParser::ArrayNode *array = static_cast<const GDScriptParser::ArrayNode *>(p_expression);
			for (const GDScriptParser::ExpressionNode *element : array->elements) {
				if (expression_has_side_effects(element)) {
					return true;
				}
			}
			return false;
		}
		case GDScriptParser::Node::BINARY_OPERATOR: {
			const GDScriptParser::BinaryOpNode *binary = static_cast<const GDScriptParser::BinaryOpNode *>(p_expression);
			return expression_has_side_effects(binary->left_operand) || expression_has_side_effects(binary->right_operand);
		}
		case GDScriptParser::Node::CAST:
			return expression_has_side_effects(static_cast<const GDScriptParser::CastNode *>(p_expression)->operand);
		case GDScriptParser::Node::DICTIONARY: {
			const GDScriptParser::DictionaryNode *dictionary = static_cast<const GDScriptParser::DictionaryNode *>(p_expression);
			for (const GDScriptParser::DictionaryNode::Pair &pair : dictionary->elements) {
				if (expression_has_side_effects(pair.key) || expression_has_side_effects(pair.value)) {
					return true;
				}
			}
			return false;
		}
		case GDScriptParser::Node::SUBSCRIPT: {
			const GDScriptParser::SubscriptNode *subscript = static_cast<const GDScriptParser::SubscriptNode *>(p_expression);
			return expression_has_side_effects(subscript->base) ||
					(!subscript->is_attribute && expression_has_side_effects(subscript->index));
		}
		case GDScriptParser::Node::TERNARY_OPERATOR: {
			const GDScriptParser::TernaryOpNode *ternary = static_cast<const GDScriptParser::TernaryOpNode *>(p_expression);
			return expression_has_side_effects(ternary->condition) ||
					expression_has_side_effects(ternary->true_expr) ||
					expression_has_side_effects(ternary->false_expr);
		}
		case GDScriptParser::Node::TYPE_TEST:
			return expression_has_side_effects(static_cast<const GDScriptParser::TypeTestNode *>(p_expression)->operand);
		case GDScriptParser::Node::UNARY_OPERATOR:
			return expression_has_side_effects(static_cast<const GDScriptParser::UnaryOpNode *>(p_expression)->operand);
		default:
			return false;
	}
}

int binary_precedence(const GDScriptParser::BinaryOpNode *p_binary) {
	if (p_binary == nullptr) {
		return 0;
	}
	switch (p_binary->operation) {
		case GDScriptParser::BinaryOpNode::OP_LOGIC_OR:
			return 4;
		case GDScriptParser::BinaryOpNode::OP_LOGIC_AND:
			return 5;
		case GDScriptParser::BinaryOpNode::OP_CONTENT_TEST:
			return 7;
		case GDScriptParser::BinaryOpNode::OP_COMP_EQUAL:
		case GDScriptParser::BinaryOpNode::OP_COMP_NOT_EQUAL:
		case GDScriptParser::BinaryOpNode::OP_COMP_LESS:
		case GDScriptParser::BinaryOpNode::OP_COMP_LESS_EQUAL:
		case GDScriptParser::BinaryOpNode::OP_COMP_GREATER:
		case GDScriptParser::BinaryOpNode::OP_COMP_GREATER_EQUAL:
			return 8;
		case GDScriptParser::BinaryOpNode::OP_BIT_OR:
			return 9;
		case GDScriptParser::BinaryOpNode::OP_BIT_XOR:
			return 10;
		case GDScriptParser::BinaryOpNode::OP_BIT_AND:
			return 11;
		case GDScriptParser::BinaryOpNode::OP_BIT_LEFT_SHIFT:
		case GDScriptParser::BinaryOpNode::OP_BIT_RIGHT_SHIFT:
			return 12;
		case GDScriptParser::BinaryOpNode::OP_ADDITION:
		case GDScriptParser::BinaryOpNode::OP_SUBTRACTION:
			return 13;
		case GDScriptParser::BinaryOpNode::OP_MULTIPLICATION:
		case GDScriptParser::BinaryOpNode::OP_DIVISION:
		case GDScriptParser::BinaryOpNode::OP_MODULO:
			return 14;
		case GDScriptParser::BinaryOpNode::OP_POWER:
			return 17;
	}
	return 0;
}

int unary_precedence(const GDScriptParser::UnaryOpNode *p_unary) {
	if (p_unary == nullptr) {
		return 0;
	}
	switch (p_unary->operation) {
		case GDScriptParser::UnaryOpNode::OP_LOGIC_NOT:
			return 6;
		case GDScriptParser::UnaryOpNode::OP_POSITIVE:
		case GDScriptParser::UnaryOpNode::OP_NEGATIVE:
			return 15;
		case GDScriptParser::UnaryOpNode::OP_COMPLEMENT:
			return 16;
	}
	return 0;
}

int expression_precedence(const GDScriptParser::ExpressionNode *p_expression) {
	if (p_expression == nullptr) {
		return 0;
	}
	switch (p_expression->type) {
		case GDScriptParser::Node::ASSIGNMENT:
			return 1;
		case GDScriptParser::Node::CAST:
			return 2;
		case GDScriptParser::Node::TERNARY_OPERATOR:
			return 3;
		case GDScriptParser::Node::BINARY_OPERATOR:
			return binary_precedence(static_cast<const GDScriptParser::BinaryOpNode *>(p_expression));
		case GDScriptParser::Node::UNARY_OPERATOR:
			return unary_precedence(static_cast<const GDScriptParser::UnaryOpNode *>(p_expression));
		case GDScriptParser::Node::TYPE_TEST:
			return 18;
		case GDScriptParser::Node::AWAIT:
			return 19;
		case GDScriptParser::Node::CALL:
		case GDScriptParser::Node::SUBSCRIPT:
			return 20;
		default:
			return 21;
	}
}

bool inline_replacement_needs_parentheses(const GDScriptParser::ExpressionNode *p_initializer, const InlineVariableUse &p_use) {
	if (p_initializer == nullptr || p_use.parent == nullptr || p_use.identifier == nullptr) {
		return false;
	}
	const int initializer_precedence = expression_precedence(p_initializer);

	switch (p_use.parent->type) {
		case GDScriptParser::Node::BINARY_OPERATOR: {
			const GDScriptParser::BinaryOpNode *parent_binary = static_cast<const GDScriptParser::BinaryOpNode *>(p_use.parent);
			const int parent_precedence = binary_precedence(parent_binary);
			if (initializer_precedence < parent_precedence) {
				return true;
			}
			if (initializer_precedence == parent_precedence) {
				return p_use.identifier == parent_binary->right_operand ||
						(parent_binary->operation == GDScriptParser::BinaryOpNode::OP_POWER &&
								p_use.identifier == parent_binary->left_operand);
			}
			return false;
		}
		case GDScriptParser::Node::AWAIT: {
			const GDScriptParser::AwaitNode *await = static_cast<const GDScriptParser::AwaitNode *>(p_use.parent);
			return await->to_await == p_use.identifier && initializer_precedence < expression_precedence(await);
		}
		case GDScriptParser::Node::CALL: {
			const GDScriptParser::CallNode *call = static_cast<const GDScriptParser::CallNode *>(p_use.parent);
			return call->callee == p_use.identifier && initializer_precedence < expression_precedence(call);
		}
		case GDScriptParser::Node::CAST: {
			const GDScriptParser::CastNode *cast = static_cast<const GDScriptParser::CastNode *>(p_use.parent);
			return cast->operand == p_use.identifier && initializer_precedence < expression_precedence(cast);
		}
		case GDScriptParser::Node::SUBSCRIPT: {
			const GDScriptParser::SubscriptNode *subscript = static_cast<const GDScriptParser::SubscriptNode *>(p_use.parent);
			return subscript->base == p_use.identifier && initializer_precedence < expression_precedence(subscript);
		}
		case GDScriptParser::Node::TYPE_TEST: {
			const GDScriptParser::TypeTestNode *type_test = static_cast<const GDScriptParser::TypeTestNode *>(p_use.parent);
			return type_test->operand == p_use.identifier && initializer_precedence < expression_precedence(type_test);
		}
		case GDScriptParser::Node::UNARY_OPERATOR:
			return initializer_precedence <= expression_precedence(static_cast<const GDScriptParser::ExpressionNode *>(p_use.parent));
		default:
			break;
	}
	return false;
}

bool inline_declaration_has_trailing_content(const Vector<String> &p_lines, const RefactorLocation &p_initializer_range) {
	if (p_initializer_range.start_line != p_initializer_range.end_line ||
			p_initializer_range.end_line < 0 || p_initializer_range.end_line >= p_lines.size()) {
		return true;
	}
	const String line = p_lines[p_initializer_range.end_line];
	for (int i = p_initializer_range.end_column; i < line.length(); i++) {
		if (!is_whitespace(line[i])) {
			return true;
		}
	}
	return false;
}

bool make_inline_variable_declaration_edit(const Vector<String> &p_lines, const GDScriptParser::VariableNode *p_variable, RefactorTextEdit &r_edit) {
	if (p_variable == nullptr || p_variable->start_line <= 0) {
		return false;
	}
	const int line_index = p_variable->start_line - 1;
	if (line_index < 0 || line_index >= p_lines.size()) {
		return false;
	}
	r_edit.start_line = line_index;
	r_edit.start_column = 0;
	if (line_index + 1 < p_lines.size()) {
		r_edit.end_line = line_index + 1;
		r_edit.end_column = 0;
	} else {
		r_edit.end_line = line_index;
		r_edit.end_column = p_lines[line_index].length();
	}
	r_edit.new_text = "";
	return true;
}

InlineVariableCandidate find_inline_variable_candidate_in_tree(const RefactorLocation &p_location, const Vector<String> &p_lines, const GDScriptParser::ClassNode *p_tree) {
	InlineVariableCandidate candidate;
	const GDScriptParser::VariableNode *target = nullptr;
	const GDScriptParser::FunctionNode *target_function = nullptr;

	if (!find_inline_variable_target_in_class(p_location, p_lines, p_tree, target, target_function)) {
		candidate.disabled_reason = "Place the caret on a local variable declaration or use.";
		return candidate;
	}

	candidate.matched = true;
	if (target_function == nullptr) {
		candidate.disabled_reason = "Inline variable is only available for local variables.";
		return candidate;
	}
	if (target->initializer == nullptr) {
		candidate.disabled_reason = "This local variable has no initializer.";
		return candidate;
	}
	if (target->assignments > 1) {
		candidate.disabled_reason = "This local variable is reassigned.";
		return candidate;
	}

	String initializer_text;
	RefactorLocation initializer_range;
	if (!get_single_line_node_text(p_lines, target->initializer, initializer_text, &initializer_range)) {
		candidate.disabled_reason = "Inline variable currently supports single-line initializers.";
		return candidate;
	}
	if (inline_declaration_has_trailing_content(p_lines, initializer_range)) {
		candidate.disabled_reason = "Cannot inline declarations with trailing content after the initializer.";
		return candidate;
	}

	InlineVariableUsageCollection usages;
	collect_inline_variable_uses_in_class(p_tree, target, target_function, usages);
	if (usages.has_assignment_target) {
		candidate.disabled_reason = "This local variable is assigned after declaration.";
		return candidate;
	}
	if (usages.has_lambda_capture) {
		candidate.disabled_reason = "Cannot inline a local variable captured by a lambda.";
		return candidate;
	}
	if (usages.reads.is_empty()) {
		candidate.disabled_reason = "No read usages found for this local variable.";
		return candidate;
	}
	if (usages.reads.size() > 1 && expression_has_side_effects(target->initializer)) {
		candidate.disabled_reason = "Cannot inline a side-effecting initializer into multiple uses.";
		return candidate;
	}
	if (!make_inline_variable_declaration_edit(p_lines, target, candidate.declaration_edit)) {
		candidate.disabled_reason = "Cannot remove this local variable declaration safely.";
		return candidate;
	}

	for (const InlineVariableUse &use : usages.reads) {
		RefactorLocation range;
		if (!get_node_text_range(p_lines, use.identifier, range)) {
			candidate.disabled_reason = "Cannot locate a local variable use for replacement.";
			candidate.replacement_edits.clear();
			return candidate;
		}

		RefactorTextEdit edit;
		edit.start_line = range.start_line;
		edit.start_column = range.start_column;
		edit.end_line = range.end_line;
		edit.end_column = range.end_column;
		edit.new_text = inline_replacement_needs_parentheses(target->initializer, use) ? "(" + initializer_text + ")" : initializer_text;
		candidate.replacement_edits.push_back(edit);
	}

	candidate.enabled = true;
	return candidate;
}

bool source_lines_match(const Vector<String> &p_left, const Vector<String> &p_right) {
	if (p_left.size() != p_right.size()) {
		return false;
	}
	for (int i = 0; i < p_left.size(); i++) {
		if (p_left[i] != p_right[i]) {
			return false;
		}
	}
	return true;
}

// Cache locks only protect shared state. Concurrent misses may compute the same pure candidate twice.
bool get_cached_type_annotation_candidate(const RefactorContext &p_context, const RefactorLocation &p_location, TypeAnnotationCandidate &r_candidate) {
	MutexLock lock(refactor_candidate_cache_mutex);

	if (!type_annotation_cache.valid ||
			type_annotation_cache.path != p_context.path ||
			type_annotation_cache.source_hash != p_context.source.hash64() ||
			type_annotation_cache.source_length != p_context.source.length() ||
			!same_location(type_annotation_cache.location, p_location)) {
		return false;
	}
	r_candidate = type_annotation_cache.candidate;
	return true;
}

void cache_type_annotation_candidate(const RefactorContext &p_context, const RefactorLocation &p_location, const TypeAnnotationCandidate &p_candidate) {
	MutexLock lock(refactor_candidate_cache_mutex);

	type_annotation_cache.valid = true;
	type_annotation_cache.path = p_context.path;
	type_annotation_cache.source_hash = p_context.source.hash64();
	type_annotation_cache.source_length = p_context.source.length();
	type_annotation_cache.location = p_location;
	type_annotation_cache.candidate = p_candidate;
}

bool get_cached_extract_variable_candidate(const RefactorContext &p_context, const RefactorLocation &p_location, ExtractVariableCandidate &r_candidate) {
	MutexLock lock(refactor_candidate_cache_mutex);

	if (!extract_variable_cache.valid ||
			extract_variable_cache.path != p_context.path ||
			extract_variable_cache.source_hash != p_context.source.hash64() ||
			extract_variable_cache.source_length != p_context.source.length() ||
			!same_location(extract_variable_cache.location, p_location)) {
		return false;
	}
	r_candidate = extract_variable_cache.candidate;
	return true;
}

void cache_extract_variable_candidate(const RefactorContext &p_context, const RefactorLocation &p_location, const ExtractVariableCandidate &p_candidate) {
	MutexLock lock(refactor_candidate_cache_mutex);

	extract_variable_cache.valid = true;
	extract_variable_cache.path = p_context.path;
	extract_variable_cache.source_hash = p_context.source.hash64();
	extract_variable_cache.source_length = p_context.source.length();
	extract_variable_cache.location = p_location;
	extract_variable_cache.candidate = p_candidate;
}

bool get_cached_extract_method_candidate(
		const RefactorContext &p_context,
		const RefactorLocation &p_location,
		ExtractMethodCandidate &r_candidate) {
	MutexLock lock(refactor_candidate_cache_mutex);

	if (!extract_method_cache.valid ||
			extract_method_cache.path != p_context.path ||
			extract_method_cache.source_hash != p_context.source.hash64() ||
			extract_method_cache.source_length != p_context.source.length() ||
			!same_location(extract_method_cache.location, p_location)) {
		return false;
	}
	r_candidate = extract_method_cache.candidate;
	return true;
}

void cache_extract_method_candidate(
		const RefactorContext &p_context,
		const RefactorLocation &p_location,
		const ExtractMethodCandidate &p_candidate) {
	MutexLock lock(refactor_candidate_cache_mutex);

	extract_method_cache.valid = true;
	extract_method_cache.path = p_context.path;
	extract_method_cache.source_hash = p_context.source.hash64();
	extract_method_cache.source_length = p_context.source.length();
	extract_method_cache.location = p_location;
	extract_method_cache.candidate = p_candidate;
}

bool get_cached_inline_variable_candidate(const RefactorContext &p_context, const RefactorLocation &p_location, InlineVariableCandidate &r_candidate) {
	MutexLock lock(refactor_candidate_cache_mutex);

	if (!inline_variable_cache.valid ||
			inline_variable_cache.path != p_context.path ||
			inline_variable_cache.source_hash != p_context.source.hash64() ||
			inline_variable_cache.source_length != p_context.source.length() ||
			!same_location(inline_variable_cache.location, p_location)) {
		return false;
	}
	r_candidate = inline_variable_cache.candidate;
	return true;
}

void cache_inline_variable_candidate(const RefactorContext &p_context, const RefactorLocation &p_location, const InlineVariableCandidate &p_candidate) {
	MutexLock lock(refactor_candidate_cache_mutex);

	inline_variable_cache.valid = true;
	inline_variable_cache.path = p_context.path;
	inline_variable_cache.source_hash = p_context.source.hash64();
	inline_variable_cache.source_length = p_context.source.length();
	inline_variable_cache.location = p_location;
	inline_variable_cache.candidate = p_candidate;
}

void collect_type_annotation_in_suite(const Vector<String> &p_lines, const GDScriptParser::SuiteNode *p_suite, Vector<TypeAnnotationCandidate> &r_candidates);

void collect_assignable_candidate(const Vector<String> &p_lines, const GDScriptParser::AssignableNode *p_assignable, const String &p_kind, bool p_has_keyword, Vector<TypeAnnotationCandidate> &r_candidates) {
	TypeAnnotationCandidate candidate;
	if (find_assignable_type_annotation(p_lines, p_assignable, p_kind, p_has_keyword, candidate) && candidate.matched) {
		r_candidates.push_back(candidate);
	}
}

void collect_type_annotation_in_function(const Vector<String> &p_lines, const GDScriptParser::FunctionNode *p_function, Vector<TypeAnnotationCandidate> &r_candidates) {
	if (p_function == nullptr) {
		return;
	}
	for (const GDScriptParser::ParameterNode *parameter : p_function->parameters) {
		collect_assignable_candidate(p_lines, parameter, "parameter", false, r_candidates);
	}
	if (p_function->rest_parameter != nullptr) {
		collect_assignable_candidate(p_lines, p_function->rest_parameter, "parameter", false, r_candidates);
	}
	TypeAnnotationCandidate return_candidate;
	if (find_function_return_type_annotation(p_lines, p_function, return_candidate) && return_candidate.matched) {
		r_candidates.push_back(return_candidate);
	}
	collect_type_annotation_in_suite(p_lines, p_function->body, r_candidates);
}

void collect_type_annotation_in_class(const Vector<String> &p_lines, const GDScriptParser::ClassNode *p_class, Vector<TypeAnnotationCandidate> &r_candidates) {
	if (p_class == nullptr) {
		return;
	}
	for (const GDScriptParser::ClassNode::Member &member : p_class->members) {
		switch (member.type) {
			case GDScriptParser::ClassNode::Member::CONSTANT:
				collect_assignable_candidate(p_lines, member.constant, "constant", true, r_candidates);
				break;
			case GDScriptParser::ClassNode::Member::VARIABLE:
				collect_assignable_candidate(p_lines, member.variable, "variable", true, r_candidates);
				break;
			case GDScriptParser::ClassNode::Member::FUNCTION:
				collect_type_annotation_in_function(p_lines, member.function, r_candidates);
				break;
			case GDScriptParser::ClassNode::Member::CLASS:
				collect_type_annotation_in_class(p_lines, member.m_class, r_candidates);
				break;
			default:
				break;
		}
	}
}

void collect_type_annotation_in_suite(const Vector<String> &p_lines, const GDScriptParser::SuiteNode *p_suite, Vector<TypeAnnotationCandidate> &r_candidates) {
	if (p_suite == nullptr) {
		return;
	}
	for (const GDScriptParser::Node *statement : p_suite->statements) {
		if (statement == nullptr) {
			continue;
		}
		switch (statement->type) {
			case GDScriptParser::Node::VARIABLE:
				collect_assignable_candidate(p_lines, static_cast<const GDScriptParser::VariableNode *>(statement), "variable", true, r_candidates);
				break;
			case GDScriptParser::Node::CONSTANT:
				collect_assignable_candidate(p_lines, static_cast<const GDScriptParser::ConstantNode *>(statement), "constant", true, r_candidates);
				break;
			case GDScriptParser::Node::IF: {
				const GDScriptParser::IfNode *if_node = static_cast<const GDScriptParser::IfNode *>(statement);
				collect_type_annotation_in_suite(p_lines, if_node->true_block, r_candidates);
				collect_type_annotation_in_suite(p_lines, if_node->false_block, r_candidates);
			} break;
			case GDScriptParser::Node::FOR: {
				const GDScriptParser::ForNode *for_node = static_cast<const GDScriptParser::ForNode *>(statement);
				collect_type_annotation_in_suite(p_lines, for_node->loop, r_candidates);
			} break;
			case GDScriptParser::Node::WHILE: {
				const GDScriptParser::WhileNode *while_node = static_cast<const GDScriptParser::WhileNode *>(statement);
				collect_type_annotation_in_suite(p_lines, while_node->loop, r_candidates);
			} break;
			case GDScriptParser::Node::MATCH: {
				const GDScriptParser::MatchNode *match_node = static_cast<const GDScriptParser::MatchNode *>(statement);
				for (const GDScriptParser::MatchBranchNode *branch : match_node->branches) {
					if (branch != nullptr) {
						collect_type_annotation_in_suite(p_lines, branch->block, r_candidates);
					}
				}
			} break;
			default:
				break;
		}
	}
}

Vector<TypeAnnotationCandidate> collect_type_annotation_candidates_in_tree(const Vector<String> &p_lines, const GDScriptParser::ClassNode *p_tree) {
	Vector<TypeAnnotationCandidate> candidates;
	collect_type_annotation_in_class(p_lines, p_tree, candidates);
	return candidates;
}

TypeAnnotationCandidate find_type_annotation_candidate_in_tree(const RefactorLocation &p_location, const Vector<String> &p_lines, const GDScriptParser::ClassNode *p_tree) {
	const Vector<TypeAnnotationCandidate> candidates = collect_type_annotation_candidates_in_tree(p_lines, p_tree);
	for (const TypeAnnotationCandidate &candidate : candidates) {
		if (caret_on_segment(p_location, candidate.line, candidate.caret_span_start, candidate.caret_span_end)) {
			return candidate;
		}
	}

	TypeAnnotationCandidate candidate;
	candidate.disabled_reason = "Place the caret on an untyped declaration with an inferred concrete type.";
	return candidate;
}

ExtractVariableCandidate find_extract_variable_candidate_in_tree(const RefactorLocation &p_location, const Vector<String> &p_lines, const GDScriptParser::ClassNode *p_tree) {
	ExtractVariableCandidate candidate;

	_ALLOW_DISCARD_ find_extract_variable_in_class(p_location, p_lines, p_tree, candidate);
	if (!candidate.matched && candidate.disabled_reason.is_empty()) {
		candidate.disabled_reason = "Select one complete expression that can be safely extracted.";
	}
	return candidate;
}

ExtractMethodCandidate find_extract_method_candidate_in_tree(
		const RefactorLocation &p_location,
		const Vector<String> &p_lines,
		bool p_source_has_final_newline,
		const GDScriptParser::ClassNode *p_tree) {
	ExtractMethodCandidate candidate;

	_ALLOW_DISCARD_ find_extract_method_in_class(p_location, p_lines, p_source_has_final_newline, p_tree, candidate);
	if (!candidate.matched && candidate.disabled_reason.is_empty()) {
		candidate.disabled_reason = "Select complete statements inside one function body.";
	}
	return candidate;
}

ExtractVariableCandidate find_extract_variable_candidate_uncached(
		const RefactorContext &p_context,
		const RefactorLocation &p_location,
		const Vector<String> &p_lines,
		const GDScriptParseResultProvider *p_parse_results) {
#ifndef GDSCRIPT_NO_LSP
	if (p_parse_results != nullptr) {
		const ExtendGDScriptParser *lsp_parser = p_parse_results->get_parse_result(p_context.path);
		if (lsp_parser != nullptr && source_lines_match(lsp_parser->get_lines(), p_lines)) {
			if (lsp_parser->parse_result != OK) {
				ExtractVariableCandidate candidate;
				candidate.disabled_reason = "Cannot analyze this script.";
				return candidate;
			}
			return find_extract_variable_candidate_in_tree(p_location, p_lines, lsp_parser->get_tree());
		}
	}
#endif // GDSCRIPT_NO_LSP

	GDScriptParser parser;
	Error err = parser.parse(p_context.source, p_context.path, false);
	if (err == OK) {
		GDScriptAnalyzer analyzer(&parser);
		err = analyzer.analyze();
	}
	if (err != OK) {
		ExtractVariableCandidate candidate;
		candidate.disabled_reason = "Cannot analyze this script.";
		return candidate;
	}

	return find_extract_variable_candidate_in_tree(p_location, p_lines, parser.get_tree());
}

ExtractMethodCandidate find_extract_method_candidate_uncached(
		const RefactorContext &p_context,
		const RefactorLocation &p_location,
		const Vector<String> &p_lines,
		const GDScriptParseResultProvider *p_parse_results) {
	const bool source_has_final_newline = p_context.source.ends_with("\n");
#ifndef GDSCRIPT_NO_LSP
	if (p_parse_results != nullptr) {
		const ExtendGDScriptParser *lsp_parser = p_parse_results->get_parse_result(p_context.path);
		if (lsp_parser != nullptr && source_lines_match(lsp_parser->get_lines(), p_lines)) {
			if (lsp_parser->parse_result != OK) {
				ExtractMethodCandidate candidate;
				candidate.disabled_reason = "Cannot analyze this script.";
				return candidate;
			}
			return find_extract_method_candidate_in_tree(
					p_location,
					p_lines,
					source_has_final_newline,
					lsp_parser->get_tree());
		}
	}
#endif // GDSCRIPT_NO_LSP

	GDScriptParser parser;
	Error err = parser.parse(p_context.source, p_context.path, false);
	if (err == OK) {
		GDScriptAnalyzer analyzer(&parser);
		err = analyzer.analyze();
	}
	if (err != OK) {
		ExtractMethodCandidate candidate;
		candidate.disabled_reason = "Cannot analyze this script.";
		return candidate;
	}

	return find_extract_method_candidate_in_tree(
			p_location,
			p_lines,
			source_has_final_newline,
			parser.get_tree());
}

InlineVariableCandidate find_inline_variable_candidate_uncached(
		const RefactorContext &p_context,
		const RefactorLocation &p_location,
		const Vector<String> &p_lines,
		const GDScriptParseResultProvider *p_parse_results) {
#ifndef GDSCRIPT_NO_LSP
	if (p_parse_results != nullptr) {
		const ExtendGDScriptParser *lsp_parser = p_parse_results->get_parse_result(p_context.path);
		if (lsp_parser != nullptr && source_lines_match(lsp_parser->get_lines(), p_lines)) {
			if (lsp_parser->parse_result != OK) {
				InlineVariableCandidate candidate;
				candidate.disabled_reason = "Cannot analyze this script.";
				return candidate;
			}
			return find_inline_variable_candidate_in_tree(p_location, p_lines, lsp_parser->get_tree());
		}
	}
#endif // GDSCRIPT_NO_LSP

	GDScriptParser parser;
	Error err = parser.parse(p_context.source, p_context.path, false);
	if (err == OK) {
		GDScriptAnalyzer analyzer(&parser);
		err = analyzer.analyze();
	}
	if (err != OK) {
		InlineVariableCandidate candidate;
		candidate.disabled_reason = "Cannot analyze this script.";
		return candidate;
	}

	return find_inline_variable_candidate_in_tree(p_location, p_lines, parser.get_tree());
}

ExtractVariableCandidate find_extract_variable_candidate(
		const RefactorContext &p_context,
		const RefactorLocation &p_location,
		const GDScriptParseResultProvider *p_parse_results) {
	ExtractVariableCandidate candidate;
	if (get_cached_extract_variable_candidate(p_context, p_location, candidate)) {
		return candidate;
	}

	if (!p_location.has_selection()) {
		candidate.disabled_reason = "Select one expression to extract.";
		cache_extract_variable_candidate(p_context, p_location, candidate);
		return candidate;
	}
	if (!is_location_ordered(p_location)) {
		candidate.disabled_reason = "Select one expression to extract.";
		cache_extract_variable_candidate(p_context, p_location, candidate);
		return candidate;
	}
	if (p_location.start_line != p_location.end_line) {
		candidate.disabled_reason = "Extract variable currently supports single-line expressions.";
		cache_extract_variable_candidate(p_context, p_location, candidate);
		return candidate;
	}

	const Vector<String> lines = p_context.source.split("\n");
	candidate = find_extract_variable_candidate_uncached(p_context, p_location, lines, p_parse_results);
	cache_extract_variable_candidate(p_context, p_location, candidate);
	return candidate;
}

ExtractMethodCandidate find_extract_method_candidate(
		const RefactorContext &p_context,
		const RefactorLocation &p_location,
		const GDScriptParseResultProvider *p_parse_results) {
	ExtractMethodCandidate candidate;
	if (get_cached_extract_method_candidate(p_context, p_location, candidate)) {
		return candidate;
	}

	if (!p_location.has_selection()) {
		candidate.disabled_reason = "Select complete statements to extract.";
		cache_extract_method_candidate(p_context, p_location, candidate);
		return candidate;
	}
	if (!is_location_ordered(p_location)) {
		candidate.disabled_reason = "Select complete statements to extract.";
		cache_extract_method_candidate(p_context, p_location, candidate);
		return candidate;
	}

	const Vector<String> lines = p_context.source.split("\n");
	candidate = find_extract_method_candidate_uncached(p_context, p_location, lines, p_parse_results);
	cache_extract_method_candidate(p_context, p_location, candidate);
	return candidate;
}

RefactorResult prepare_extract_variable(
		const RefactorContext &p_context,
		const RefactorLocation &p_location,
		const GDScriptParseResultProvider *p_parse_results) {
	RefactorResult result;
	const ExtractVariableCandidate candidate = find_extract_variable_candidate(p_context, p_location, p_parse_results);
	if (!candidate.enabled) {
		result.ok = false;
		result.error_message = candidate.disabled_reason;
		return result;
	}

	result.ok = true;
	result.suggested_name = candidate.suggested_name;
	result.rename_anchor_line = candidate.name_line;
	result.rename_anchor_column = candidate.name_column;
	result.edits.push_back(candidate.declaration_edit);
	result.edits.push_back(candidate.replacement_edit);
	return result;
}

RefactorResult prepare_extract_method(
		const RefactorContext &p_context,
		const RefactorLocation &p_location,
		const GDScriptParseResultProvider *p_parse_results) {
	RefactorResult result;
	const ExtractMethodCandidate candidate = find_extract_method_candidate(p_context, p_location, p_parse_results);
	if (!candidate.enabled) {
		result.ok = false;
		result.error_message = candidate.disabled_reason;
		return result;
	}

	result.ok = true;
	result.suggested_name = candidate.suggested_name;
	result.rename_anchor_line = candidate.name_line;
	result.rename_anchor_column = candidate.name_column;
	result.edits.push_back(candidate.replacement_edit);
	result.edits.push_back(candidate.method_edit);
	return result;
}

InlineVariableCandidate find_inline_variable_candidate(
		const RefactorContext &p_context,
		const RefactorLocation &p_location,
		const GDScriptParseResultProvider *p_parse_results) {
	InlineVariableCandidate candidate;
	if (get_cached_inline_variable_candidate(p_context, p_location, candidate)) {
		return candidate;
	}

	if (p_location.has_selection()) {
		candidate.disabled_reason = "Place the caret on a local variable declaration or use.";
		cache_inline_variable_candidate(p_context, p_location, candidate);
		return candidate;
	}

	const Vector<String> lines = p_context.source.split("\n");
	candidate = find_inline_variable_candidate_uncached(p_context, p_location, lines, p_parse_results);
	cache_inline_variable_candidate(p_context, p_location, candidate);
	return candidate;
}

RefactorResult prepare_inline_variable(
		const RefactorContext &p_context,
		const RefactorLocation &p_location,
		const GDScriptParseResultProvider *p_parse_results) {
	RefactorResult result;
	const InlineVariableCandidate candidate = find_inline_variable_candidate(p_context, p_location, p_parse_results);
	if (!candidate.enabled) {
		result.ok = false;
		result.error_message = candidate.disabled_reason;
		return result;
	}

	result.ok = true;
	result.edits.push_back(candidate.declaration_edit);
	for (const RefactorTextEdit &edit : candidate.replacement_edits) {
		result.edits.push_back(edit);
	}
	return result;
}

TypeAnnotationCandidate find_type_annotation_candidate_uncached(
		const RefactorContext &p_context,
		const RefactorLocation &p_location,
		const Vector<String> &p_lines,
		const GDScriptParseResultProvider *p_parse_results) {
#ifndef GDSCRIPT_NO_LSP
	if (p_parse_results != nullptr) {
		const ExtendGDScriptParser *lsp_parser = p_parse_results->get_parse_result(p_context.path);
		if (lsp_parser != nullptr && source_lines_match(lsp_parser->get_lines(), p_lines)) {
			if (lsp_parser->parse_result != OK) {
				TypeAnnotationCandidate candidate;
				candidate.disabled_reason = "Cannot analyze this script.";
				return candidate;
			}
			return find_type_annotation_candidate_in_tree(p_location, p_lines, lsp_parser->get_tree());
		}
	}
#endif // GDSCRIPT_NO_LSP

	GDScriptParser parser;
	Error err = parser.parse(p_context.source, p_context.path, false);
	if (err == OK) {
		GDScriptAnalyzer analyzer(&parser);
		err = analyzer.analyze();
	}
	if (err != OK) {
		TypeAnnotationCandidate candidate;
		candidate.disabled_reason = "Cannot analyze this script.";
		return candidate;
	}

	return find_type_annotation_candidate_in_tree(p_location, p_lines, parser.get_tree());
}

TypeAnnotationCandidate find_type_annotation_candidate(
		const RefactorContext &p_context,
		const RefactorLocation &p_location,
		const GDScriptParseResultProvider *p_parse_results) {
	TypeAnnotationCandidate candidate;
	if (get_cached_type_annotation_candidate(p_context, p_location, candidate)) {
		return candidate;
	}

	if (p_location.has_selection()) {
		candidate.disabled_reason = "Place the caret on an untyped declaration.";
		cache_type_annotation_candidate(p_context, p_location, candidate);
		return candidate;
	}

	const Vector<String> lines = p_context.source.split("\n");
	candidate = find_type_annotation_candidate_uncached(p_context, p_location, lines, p_parse_results);
	cache_type_annotation_candidate(p_context, p_location, candidate);
	return candidate;
}

RefactorResult prepare_type_annotation(
		const RefactorContext &p_context,
		const RefactorLocation &p_location,
		const GDScriptParseResultProvider *p_parse_results) {
	RefactorResult result;
	const TypeAnnotationCandidate candidate = find_type_annotation_candidate(p_context, p_location, p_parse_results);
	if (!candidate.enabled) {
		result.ok = false;
		result.error_message = candidate.disabled_reason;
		return result;
	}

	result.ok = true;
	result.edits.push_back(candidate.edit);
	return result;
}

RefactorCandidatesResult collect_type_annotation_candidates(
		const RefactorContext &p_context,
		const GDScriptParseResultProvider *p_parse_results) {
	RefactorCandidatesResult result;
	const Vector<String> lines = p_context.source.split("\n");

	const GDScriptParser::ClassNode *tree = nullptr;
#ifndef GDSCRIPT_NO_LSP
	if (p_parse_results != nullptr) {
		const ExtendGDScriptParser *lsp_parser = p_parse_results->get_parse_result(p_context.path);
		if (lsp_parser != nullptr && source_lines_match(lsp_parser->get_lines(), lines)) {
			if (lsp_parser->parse_result != OK) {
				result.error_message = "Cannot analyze this script.";
				return result;
			}
			tree = lsp_parser->get_tree();
		}
	}
#endif // GDSCRIPT_NO_LSP

	GDScriptParser parser;
	if (tree == nullptr) {
		Error err = parser.parse(p_context.source, p_context.path, false);
		if (err == OK) {
			GDScriptAnalyzer analyzer(&parser);
			err = analyzer.analyze();
		}
		if (err != OK) {
			result.error_message = "Cannot analyze this script.";
			return result;
		}
		tree = parser.get_tree();
	}

	const Vector<TypeAnnotationCandidate> candidates = collect_type_annotation_candidates_in_tree(lines, tree);
	for (const TypeAnnotationCandidate &candidate : candidates) {
		RefactorCandidate public_candidate;
		public_candidate.kind = RefactorKind::ADD_TYPE_ANNOTATION;
		public_candidate.enabled = candidate.enabled;
		public_candidate.disabled_reason = candidate.disabled_reason;
		public_candidate.line = candidate.line;
		public_candidate.column = candidate.caret_span_start; // Anchor at the start of the declaration span.
		if (candidate.enabled) {
			public_candidate.edits.push_back(candidate.edit);
		}
		result.candidates.push_back(public_candidate);
	}
	result.ok = true;
	return result;
}

} // namespace

#ifndef GDSCRIPT_NO_LSP
static LSP::TextDocumentPositionParams make_document_position(const Ref<GDScriptWorkspace> &p_workspace, const RefactorContext &p_context, const RefactorLocation &p_location) {
	LSP::TextDocumentPositionParams doc_position;
	doc_position.textDocument.uri = p_workspace->get_file_uri(p_context.path);
	doc_position.position.line = p_location.start_line;
	doc_position.position.character = p_location.start_column;
	return doc_position;
}

static bool is_string_literal_usage(
		const String &p_path,
		const LSP::Location &p_usage,
		const GDScriptParseResultProvider &p_parse_results) {
	const ExtendGDScriptParser *parser = p_parse_results.get_parse_result(p_path);
	if (!parser) {
		return false;
	}

	const PackedStringArray &lines = parser->get_lines();
	if (p_usage.range.start.line < 0 || p_usage.range.start.line >= lines.size()) {
		return false;
	}

	return is_column_inside_string_literal(lines[p_usage.range.start.line], p_usage.range.start.character);
}

static void collect_dynamic_string_references(
		const Ref<GDScriptWorkspace> &p_workspace,
		const LSP::DocumentSymbol &p_symbol,
		const GDScriptParseResultProvider &p_parse_results,
		RefactorResult &r_result) {
	List<String> paths;
	if (p_symbol.local) {
		// Object string-call APIs cannot reference local variables or parameters.
		return;
	}

	p_workspace->list_project_script_files(paths);

	for (const String &path : paths) {
		const ExtendGDScriptParser *parser = p_parse_results.get_parse_result(path);
		if (!parser) {
			continue;
		}

		const PackedStringArray &lines = parser->get_lines();
		for (int i = 0; i < lines.size(); i++) {
			const String &line = lines[i];
			const int column = find_dynamic_string_reference_column(line, p_symbol.name);
			if (column < 0) {
				continue;
			}

			RefactorUnresolvedReference unresolved;
			unresolved.path = path;
			unresolved.line = i;
			unresolved.column = column;
			unresolved.message = "String-based dynamic reference cannot be renamed automatically.";
			r_result.unresolved_references.push_back(unresolved);
		}
	}
}

static void collect_parse_error_textual_references(
		const Ref<GDScriptWorkspace> &p_workspace,
		const LSP::DocumentSymbol &p_symbol,
		const GDScriptParseResultProvider &p_parse_results,
		RefactorResult &r_result) {
	if (p_symbol.local) {
		return;
	}

	List<String> paths;
	p_workspace->list_project_script_files(paths);

	for (const String &path : paths) {
		const ExtendGDScriptParser *parser = p_parse_results.get_parse_result(path);
		if (parser == nullptr || parser->parse_result == OK) {
			continue;
		}

		const PackedStringArray &lines = parser->get_lines();
		for (int i = 0; i < lines.size(); i++) {
			const int column = lines[i].find(p_symbol.name);
			if (column < 0) {
				continue;
			}

			RefactorUnresolvedReference unresolved;
			unresolved.path = path;
			unresolved.line = i;
			unresolved.column = column;
			unresolved.message = "Script could not be parsed; verify references to this symbol after rename.";
			r_result.unresolved_references.push_back(unresolved);
			break;
		}
	}
}
#endif // GDSCRIPT_NO_LSP

static RefactorResult prepare_rename(
		const RefactorContext &p_context,
		const RefactorLocation &p_location,
		const RefactorParams &p_params,
		const GDScriptParseResultProvider *p_parse_results) {
	RefactorResult result;

#ifdef GDSCRIPT_NO_LSP
	result.ok = false;
	result.error_message = "Rename requires the language server, which is not available in this build.";
	return result;
#else
	GDScriptLanguageProtocol *protocol = GDScriptLanguageProtocol::get_singleton();
	Ref<GDScriptWorkspace> workspace = protocol ? protocol->get_workspace() : Ref<GDScriptWorkspace>();
	if (workspace.is_null()) {
		result.ok = false;
		result.error_message = "Rename requires the language server, which is not available in this build.";
		return result;
	}

	if (p_parse_results == nullptr) {
		result.ok = false;
		result.error_message = "Rename requires the language server, which is not available in this build.";
		return result;
	}

	LSP::TextDocumentPositionParams doc_position = make_document_position(workspace, p_context, p_location);

	LSP::DocumentSymbol symbol;
	LSP::Range identifier_range;
	if (!workspace->can_rename(doc_position, symbol, identifier_range, p_parse_results)) {
		result.ok = false;
		result.error_message = "Cannot rename this symbol.";
		return result;
	}

	String reason;
	if (!GDScriptRefactorNames::validate_identifier(p_params.new_name, reason)) {
		result.ok = false;
		result.error_message = reason;
		return result;
	}

	// `find_all_usages` matches usages by the address of the resolved symbol, so it must
	// receive the provider-owned symbol rather than the copy returned by `can_rename`.
	const LSP::DocumentSymbol *resolved_symbol = workspace->resolve_symbol(doc_position, "", false, p_parse_results);
	if (!resolved_symbol) {
		result.ok = false;
		result.error_message = "Cannot rename this symbol.";
		return result;
	}

	const ExtendGDScriptParser *parser = p_parse_results->get_parse_result(p_context.path);
	if (parser) {
		String collision_reason;
		if (GDScriptRefactorNames::has_scope_collision(parser->get_symbols(), resolved_symbol, p_params.new_name, collision_reason)) {
			result.ok = false;
			result.error_message = collision_reason;
			return result;
		}
	}

	// An @export variable's references can live outside scripts (the inspector,
	// scene/resource files), which GDScript symbol resolution cannot enumerate.
	// The LSP builds the symbol's `detail` with an "@export " prefix for exported
	// vars (see gdscript_extend_parser.cpp), so the prefix is a reliable signal here.
	const bool is_exported = resolved_symbol->detail.contains("@export ");

	const Vector<LSP::Location> usages = workspace->find_all_usages(*resolved_symbol, p_parse_results);
	for (const LSP::Location &usage : usages) {
		const String path = workspace->get_file_path(usage.uri);
		if (path.is_empty()) {
			result.ok = false;
			result.error_message = "Cannot resolve a rename target path.";
			return result;
		}
		if (is_string_literal_usage(path, usage, *p_parse_results)) {
			continue;
		}

		RefactorTextEdit edit;
		edit.start_line = usage.range.start.line;
		edit.start_column = usage.range.start.character;
		edit.end_line = usage.range.end.line;
		edit.end_column = usage.range.end.character;
		edit.has_expected_text = true;
		edit.expected_text = resolved_symbol->name;
		edit.new_text = p_params.new_name;

		RefactorFileEdit *file_edit = find_or_add_file_edit(result.file_edits, path);
		file_edit->edits.push_back(edit);
		if (path == p_context.path) {
			// The script editor applies rename through file_edits, but tests and
			// direct engine consumers still use edits for the active file.
			result.edits.push_back(edit);

			RefactorTextEdit occurrence = edit;
			occurrence.new_text = String();
			result.rename_occurrences.push_back(occurrence);
		}
	}

	if (result.file_edits.is_empty()) {
		result.ok = false;
		result.error_message = "No references found to rename.";
		return result;
	}

	const int parse_error_reference_count = result.unresolved_references.size();
	collect_parse_error_textual_references(workspace, *resolved_symbol, *p_parse_results, result);
	const bool has_parse_error_references = result.unresolved_references.size() > parse_error_reference_count;

	const int dynamic_reference_count = result.unresolved_references.size();
	collect_dynamic_string_references(workspace, *resolved_symbol, *p_parse_results, result);
	const bool has_dynamic_references = result.unresolved_references.size() > dynamic_reference_count;

	if (has_parse_error_references) {
		append_warning(result, "Some scripts could not be parsed; references in those files may need manual verification.");
	}
	if (has_dynamic_references) {
		append_warning(result, "Some string-based dynamic references to this symbol could not be resolved statically "
							   "and were not renamed.");
	}
	if (is_exported) {
		append_warning(result, "This is an exported variable; references outside scripts (such as in the inspector "
							   "or scene files) will not be updated.");
	}

	result.ok = true;
	result.suggested_name = symbol.name;
	result.rename_anchor_line = identifier_range.start.line;
	result.rename_anchor_column = identifier_range.start.character;
	return result;
#endif // GDSCRIPT_NO_LSP
}

Vector<RefactorAvailability> GDScriptRefactoring::get_available_refactors(const RefactorContext &p_context, const RefactorLocation &p_location) {
	Vector<RefactorAvailability> result;

#ifndef GDSCRIPT_NO_LSP
	RefactorParseResultProviderScope parse_results(p_context);
	const GDScriptParseResultProvider *parse_result_provider = parse_results.get();
#else
	const GDScriptParseResultProvider *parse_result_provider = nullptr;
#endif // GDSCRIPT_NO_LSP

	RefactorAvailability rename;
	rename.kind = RefactorKind::RENAME;
	rename.title = "Rename Symbol";

#ifdef GDSCRIPT_NO_LSP
	rename.enabled = false;
	rename.disabled_reason = "Rename requires the language server.";
#else
	GDScriptLanguageProtocol *protocol = GDScriptLanguageProtocol::get_singleton();
	Ref<GDScriptWorkspace> workspace = protocol ? protocol->get_workspace() : Ref<GDScriptWorkspace>();
	if (workspace.is_null()) {
		rename.enabled = false;
		rename.disabled_reason = "Rename requires the language server.";
	} else if (parse_result_provider == nullptr) {
		rename.enabled = false;
		rename.disabled_reason = "Rename requires the language server.";
	} else {
		LSP::TextDocumentPositionParams doc_position = make_document_position(workspace, p_context, p_location);
		LSP::DocumentSymbol symbol;
		LSP::Range identifier_range;
		rename.enabled = workspace->can_rename(doc_position, symbol, identifier_range, parse_result_provider);
		if (!rename.enabled) {
			rename.disabled_reason = "Place the caret on a renameable symbol.";
		}
	}
#endif // GDSCRIPT_NO_LSP

	result.push_back(rename);

	RefactorAvailability extract_variable;
	extract_variable.kind = RefactorKind::EXTRACT_VARIABLE;
	extract_variable.title = "Extract Variable";
	const ExtractVariableCandidate extract_candidate = find_extract_variable_candidate(p_context, p_location, parse_result_provider);
	extract_variable.enabled = extract_candidate.enabled;
	if (!extract_variable.enabled) {
		extract_variable.disabled_reason = extract_candidate.disabled_reason;
	}
	result.push_back(extract_variable);

	RefactorAvailability extract_method;
	extract_method.kind = RefactorKind::EXTRACT_METHOD;
	extract_method.title = "Extract Method";
	const ExtractMethodCandidate extract_method_candidate = find_extract_method_candidate(p_context, p_location, parse_result_provider);
	extract_method.enabled = extract_method_candidate.enabled;
	if (!extract_method.enabled) {
		extract_method.disabled_reason = extract_method_candidate.disabled_reason;
	}
	result.push_back(extract_method);

	RefactorAvailability add_type;
	add_type.kind = RefactorKind::ADD_TYPE_ANNOTATION;
	add_type.title = "Add Type Annotation";
	const TypeAnnotationCandidate type_candidate = find_type_annotation_candidate(p_context, p_location, parse_result_provider);
	add_type.enabled = type_candidate.enabled;
	if (!add_type.enabled) {
		add_type.disabled_reason = type_candidate.disabled_reason;
	}
	result.push_back(add_type);

	RefactorAvailability inline_variable;
	inline_variable.kind = RefactorKind::INLINE_VARIABLE;
	inline_variable.title = "Inline Variable";
	const InlineVariableCandidate inline_candidate = find_inline_variable_candidate(p_context, p_location, parse_result_provider);
	inline_variable.enabled = inline_candidate.enabled;
	if (!inline_variable.enabled) {
		inline_variable.disabled_reason = inline_candidate.disabled_reason;
	}
	result.push_back(inline_variable);
	return result;
}

RefactorResult GDScriptRefactoring::prepare(const RefactorContext &p_context, const RefactorLocation &p_location, RefactorKind p_kind, const RefactorParams &p_params) {
#ifndef GDSCRIPT_NO_LSP
	RefactorParseResultProviderScope parse_results(p_context);
	const GDScriptParseResultProvider *parse_result_provider = parse_results.get();
#else
	const GDScriptParseResultProvider *parse_result_provider = nullptr;
#endif // GDSCRIPT_NO_LSP

	switch (p_kind) {
		case RefactorKind::RENAME:
			return prepare_rename(p_context, p_location, p_params, parse_result_provider);
		case RefactorKind::EXTRACT_VARIABLE:
			return prepare_extract_variable(p_context, p_location, parse_result_provider);
		case RefactorKind::EXTRACT_METHOD:
			return prepare_extract_method(p_context, p_location, parse_result_provider);
		case RefactorKind::ADD_TYPE_ANNOTATION:
			return prepare_type_annotation(p_context, p_location, parse_result_provider);
		case RefactorKind::INLINE_VARIABLE:
			return prepare_inline_variable(p_context, p_location, parse_result_provider);
		default:
			break;
	}

	RefactorResult result;
	result.ok = false;
	result.error_message = "Refactor not implemented.";
	return result;
}

RefactorCandidatesResult GDScriptRefactoring::find_candidates(const RefactorContext &p_context, RefactorKind p_kind) {
	if (p_kind != RefactorKind::ADD_TYPE_ANNOTATION) {
		RefactorCandidatesResult result;
		result.error_message = "Headless candidate collection is not implemented for this refactor.";
		return result;
	}

#ifndef GDSCRIPT_NO_LSP
	RefactorParseResultProviderScope parse_results(p_context);
	const GDScriptParseResultProvider *parse_result_provider = parse_results.get();
#else
	const GDScriptParseResultProvider *parse_result_provider = nullptr;
#endif // GDSCRIPT_NO_LSP

	return collect_type_annotation_candidates(p_context, parse_result_provider);
}

#endif // TOOLS_ENABLED
