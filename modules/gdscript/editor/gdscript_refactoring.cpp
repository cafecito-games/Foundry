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

#include "core/string/char_utils.h"

#ifndef GDSCRIPT_NO_LSP
#include "../language_server/gdscript_extend_parser.h"
#include "../language_server/gdscript_language_protocol.h"
#include "../language_server/gdscript_workspace.h"
#include "../language_server/godot_lsp.h"
#endif // GDSCRIPT_NO_LSP

namespace {

struct TypeAnnotationCandidate {
	bool matched = false;
	bool enabled = false;
	String disabled_reason;
	RefactorTextEdit edit;
};

struct ExtractVariableCandidate {
	bool matched = false;
	bool enabled = false;
	String disabled_reason;
	String suggested_name;
	RefactorTextEdit declaration_edit;
	RefactorTextEdit replacement_edit;
};

struct TypeAnnotationCandidateCache {
	bool valid = false;
	String path;
	uint64_t source_hash = 0;
	int source_length = 0;
	RefactorLocation location;
	TypeAnnotationCandidate candidate;
};

TypeAnnotationCandidateCache type_annotation_cache;

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
	r_candidate.declaration_edit.start_line = insertion_line;
	r_candidate.declaration_edit.start_column = 0;
	r_candidate.declaration_edit.end_line = insertion_line;
	r_candidate.declaration_edit.end_column = 0;
	r_candidate.declaration_edit.new_text = indent + "var " + name + ": " + rendered_type + " = " + expression_text + "\n";

	r_candidate.replacement_edit.start_line = p_location.start_line;
	r_candidate.replacement_edit.start_column = p_location.start_column;
	r_candidate.replacement_edit.end_line = p_location.end_line;
	r_candidate.replacement_edit.end_column = p_location.end_column;
	r_candidate.replacement_edit.new_text = name;
	r_candidate.suggested_name = name;
	r_candidate.enabled = true;
	return true;
}

bool is_simple_identifier_assignment(const GDScriptParser::AssignmentNode *p_assignment) {
	return p_assignment != nullptr &&
			p_assignment->operation == GDScriptParser::AssignmentNode::OP_NONE &&
			p_assignment->assignee != nullptr &&
			p_assignment->assignee->type == GDScriptParser::Node::IDENTIFIER;
}

bool find_assignable_type_annotation(const RefactorLocation &p_location, const Vector<String> &p_lines, const GDScriptParser::AssignableNode *p_assignable, const String &p_kind, bool p_has_keyword, TypeAnnotationCandidate &r_candidate) {
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
	const RefactorLocation *identifier_location = p_has_keyword ? nullptr : &p_location;
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
	if (!caret_on_segment(p_location, line_index, declaration_start, declaration_end)) {
		return false;
	}

	r_candidate.matched = true;
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

bool find_type_annotation_in_suite(const RefactorLocation &p_location, const Vector<String> &p_lines, const GDScriptParser::SuiteNode *p_suite, TypeAnnotationCandidate &r_candidate);

bool find_function_return_type_annotation(const RefactorLocation &p_location, const Vector<String> &p_lines, const GDScriptParser::FunctionNode *p_function, TypeAnnotationCandidate &r_candidate) {
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
	// refactor intentionally handles single-line function signatures.
	const int body_colon = line.rfind(":");
	if (function_start < 0 || body_colon < 0) {
		return false;
	}
	if (!caret_on_segment(p_location, line_index, function_start, body_colon)) {
		return false;
	}

	r_candidate.matched = true;
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

bool find_type_annotation_in_function(const RefactorLocation &p_location, const Vector<String> &p_lines, const GDScriptParser::FunctionNode *p_function, TypeAnnotationCandidate &r_candidate) {
	if (p_function == nullptr) {
		return false;
	}

	for (const GDScriptParser::ParameterNode *parameter : p_function->parameters) {
		if (find_assignable_type_annotation(p_location, p_lines, parameter, "parameter", false, r_candidate)) {
			return true;
		}
	}
	if (p_function->rest_parameter != nullptr && find_assignable_type_annotation(p_location, p_lines, p_function->rest_parameter, "parameter", false, r_candidate)) {
		return true;
	}
	if (find_function_return_type_annotation(p_location, p_lines, p_function, r_candidate)) {
		return true;
	}
	if (find_type_annotation_in_suite(p_location, p_lines, p_function->body, r_candidate)) {
		return true;
	}
	return false;
}

bool find_type_annotation_in_class(const RefactorLocation &p_location, const Vector<String> &p_lines, const GDScriptParser::ClassNode *p_class, TypeAnnotationCandidate &r_candidate) {
	if (p_class == nullptr) {
		return false;
	}

	for (const GDScriptParser::ClassNode::Member &member : p_class->members) {
		switch (member.type) {
			case GDScriptParser::ClassNode::Member::CONSTANT:
				if (find_assignable_type_annotation(p_location, p_lines, member.constant, "constant", true, r_candidate)) {
					return true;
				}
				break;
			case GDScriptParser::ClassNode::Member::VARIABLE:
				if (find_assignable_type_annotation(p_location, p_lines, member.variable, "variable", true, r_candidate)) {
					return true;
				}
				break;
			case GDScriptParser::ClassNode::Member::FUNCTION:
				if (find_type_annotation_in_function(p_location, p_lines, member.function, r_candidate)) {
					return true;
				}
				break;
			case GDScriptParser::ClassNode::Member::CLASS:
				if (find_type_annotation_in_class(p_location, p_lines, member.m_class, r_candidate)) {
					return true;
				}
				break;
			default:
				break;
		}
	}
	return false;
}

bool find_type_annotation_in_suite(const RefactorLocation &p_location, const Vector<String> &p_lines, const GDScriptParser::SuiteNode *p_suite, TypeAnnotationCandidate &r_candidate) {
	if (p_suite == nullptr) {
		return false;
	}

	for (const GDScriptParser::Node *statement : p_suite->statements) {
		if (statement == nullptr) {
			continue;
		}
		switch (statement->type) {
			case GDScriptParser::Node::VARIABLE:
				if (find_assignable_type_annotation(p_location, p_lines, static_cast<const GDScriptParser::VariableNode *>(statement), "variable", true, r_candidate)) {
					return true;
				}
				break;
			case GDScriptParser::Node::CONSTANT:
				if (find_assignable_type_annotation(p_location, p_lines, static_cast<const GDScriptParser::ConstantNode *>(statement), "constant", true, r_candidate)) {
					return true;
				}
				break;
			case GDScriptParser::Node::IF: {
				const GDScriptParser::IfNode *if_node = static_cast<const GDScriptParser::IfNode *>(statement);
				if (find_type_annotation_in_suite(p_location, p_lines, if_node->true_block, r_candidate) || find_type_annotation_in_suite(p_location, p_lines, if_node->false_block, r_candidate)) {
					return true;
				}
			} break;
			case GDScriptParser::Node::FOR: {
				const GDScriptParser::ForNode *for_node = static_cast<const GDScriptParser::ForNode *>(statement);
				if (find_type_annotation_in_suite(p_location, p_lines, for_node->loop, r_candidate)) {
					return true;
				}
			} break;
			case GDScriptParser::Node::WHILE: {
				const GDScriptParser::WhileNode *while_node = static_cast<const GDScriptParser::WhileNode *>(statement);
				if (find_type_annotation_in_suite(p_location, p_lines, while_node->loop, r_candidate)) {
					return true;
				}
			} break;
			case GDScriptParser::Node::MATCH: {
				const GDScriptParser::MatchNode *match_node = static_cast<const GDScriptParser::MatchNode *>(statement);
				for (const GDScriptParser::MatchBranchNode *branch : match_node->branches) {
					if (branch != nullptr && find_type_annotation_in_suite(p_location, p_lines, branch->block, r_candidate)) {
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

bool find_extract_variable_in_suite(const RefactorLocation &p_location, const Vector<String> &p_lines, const GDScriptParser::SuiteNode *p_suite, ExtractVariableCandidate &r_candidate);

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
				if (expression_matches_selection(p_location, p_lines, assignment->assigned_value) && !is_simple_identifier_assignment(assignment)) {
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

bool get_cached_type_annotation_candidate(const RefactorContext &p_context, const RefactorLocation &p_location, TypeAnnotationCandidate &r_candidate) {
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
	type_annotation_cache.valid = true;
	type_annotation_cache.path = p_context.path;
	type_annotation_cache.source_hash = p_context.source.hash64();
	type_annotation_cache.source_length = p_context.source.length();
	type_annotation_cache.location = p_location;
	type_annotation_cache.candidate = p_candidate;
}

TypeAnnotationCandidate find_type_annotation_candidate_in_tree(const RefactorLocation &p_location, const Vector<String> &p_lines, const GDScriptParser::ClassNode *p_tree) {
	TypeAnnotationCandidate candidate;

	_ALLOW_DISCARD_ find_type_annotation_in_class(p_location, p_lines, p_tree, candidate);
	if (!candidate.matched && candidate.disabled_reason.is_empty()) {
		candidate.disabled_reason = "Place the caret on an untyped declaration with an inferred concrete type.";
	}
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

ExtractVariableCandidate find_extract_variable_candidate_uncached(const RefactorContext &p_context, const RefactorLocation &p_location, const Vector<String> &p_lines) {
#ifndef GDSCRIPT_NO_LSP
	GDScriptLanguageProtocol *protocol = GDScriptLanguageProtocol::get_singleton();
	const ExtendGDScriptParser *lsp_parser = protocol != nullptr ? protocol->get_parse_result(p_context.path) : nullptr;
	if (lsp_parser != nullptr && source_lines_match(lsp_parser->get_lines(), p_lines)) {
		if (lsp_parser->parse_result != OK) {
			ExtractVariableCandidate candidate;
			candidate.disabled_reason = "Cannot analyze this script.";
			return candidate;
		}
		return find_extract_variable_candidate_in_tree(p_location, p_lines, lsp_parser->get_tree());
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

ExtractVariableCandidate find_extract_variable_candidate(const RefactorContext &p_context, const RefactorLocation &p_location) {
	ExtractVariableCandidate candidate;
	if (!p_location.has_selection()) {
		candidate.disabled_reason = "Select one expression to extract.";
		return candidate;
	}
	if (!is_location_ordered(p_location)) {
		candidate.disabled_reason = "Select one expression to extract.";
		return candidate;
	}
	if (p_location.start_line != p_location.end_line) {
		candidate.disabled_reason = "Extract variable currently supports single-line expressions.";
		return candidate;
	}

	const Vector<String> lines = p_context.source.split("\n");
	return find_extract_variable_candidate_uncached(p_context, p_location, lines);
}

RefactorResult prepare_extract_variable(const RefactorContext &p_context, const RefactorLocation &p_location) {
	RefactorResult result;
	const ExtractVariableCandidate candidate = find_extract_variable_candidate(p_context, p_location);
	if (!candidate.enabled) {
		result.ok = false;
		result.error_message = candidate.disabled_reason;
		return result;
	}

	result.ok = true;
	result.suggested_name = candidate.suggested_name;
	result.edits.push_back(candidate.declaration_edit);
	result.edits.push_back(candidate.replacement_edit);
	return result;
}

TypeAnnotationCandidate find_type_annotation_candidate_uncached(const RefactorContext &p_context, const RefactorLocation &p_location, const Vector<String> &p_lines) {
#ifndef GDSCRIPT_NO_LSP
	GDScriptLanguageProtocol *protocol = GDScriptLanguageProtocol::get_singleton();
	const ExtendGDScriptParser *lsp_parser = protocol != nullptr ? protocol->get_parse_result(p_context.path) : nullptr;
	if (lsp_parser != nullptr && source_lines_match(lsp_parser->get_lines(), p_lines)) {
		if (lsp_parser->parse_result != OK) {
			TypeAnnotationCandidate candidate;
			candidate.disabled_reason = "Cannot analyze this script.";
			return candidate;
		}
		return find_type_annotation_candidate_in_tree(p_location, p_lines, lsp_parser->get_tree());
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

TypeAnnotationCandidate find_type_annotation_candidate(const RefactorContext &p_context, const RefactorLocation &p_location) {
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
	candidate = find_type_annotation_candidate_uncached(p_context, p_location, lines);
	cache_type_annotation_candidate(p_context, p_location, candidate);
	return candidate;
}

RefactorResult prepare_type_annotation(const RefactorContext &p_context, const RefactorLocation &p_location) {
	RefactorResult result;
	const TypeAnnotationCandidate candidate = find_type_annotation_candidate(p_context, p_location);
	if (!candidate.enabled) {
		result.ok = false;
		result.error_message = candidate.disabled_reason;
		return result;
	}

	result.ok = true;
	result.edits.push_back(candidate.edit);
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
#endif // GDSCRIPT_NO_LSP

static RefactorResult prepare_rename(const RefactorContext &p_context, const RefactorLocation &p_location, const RefactorParams &p_params) {
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

	LSP::TextDocumentPositionParams doc_position = make_document_position(workspace, p_context, p_location);

	LSP::DocumentSymbol symbol;
	LSP::Range identifier_range;
	if (!workspace->can_rename(doc_position, symbol, identifier_range)) {
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
	// receive the workspace-owned symbol rather than the copy returned by `can_rename`.
	const LSP::DocumentSymbol *resolved_symbol = workspace->resolve_symbol(doc_position);
	if (!resolved_symbol) {
		result.ok = false;
		result.error_message = "Cannot rename this symbol.";
		return result;
	}

	const ExtendGDScriptParser *parser = protocol->get_parse_result(p_context.path);
	if (parser) {
		String collision_reason;
		if (GDScriptRefactorNames::has_scope_collision(parser->get_symbols(), resolved_symbol, p_params.new_name, collision_reason)) {
			result.ok = false;
			result.error_message = collision_reason;
			return result;
		}
	}

	// An @export variable's references can live outside this script (the inspector,
	// scene/resource files), which a file-local rename will not touch. The LSP
	// builds the symbol's `detail` with an "@export " prefix for exported vars
	// (see gdscript_extend_parser.cpp), so the prefix is a reliable signal here.
	const bool is_exported = resolved_symbol->detail.contains("@export ");

	// This rename is file-local: only usages in the current document are edited.
	// Track whether any usage lives in another file so we can warn that those
	// references were left untouched and may break the project.
	bool has_out_of_file_usage = false;

	const Vector<LSP::Location> usages = workspace->find_all_usages(*resolved_symbol);
	for (const LSP::Location &usage : usages) {
		if (usage.uri != doc_position.textDocument.uri) {
			has_out_of_file_usage = true;
			continue;
		}
		RefactorTextEdit edit;
		edit.start_line = usage.range.start.line;
		edit.start_column = usage.range.start.character;
		edit.end_line = usage.range.end.line;
		edit.end_column = usage.range.end.character;
		edit.new_text = p_params.new_name;
		result.edits.push_back(edit);
	}

	if (result.edits.is_empty()) {
		result.ok = false;
		result.error_message = "No references found to rename.";
		return result;
	}

	if (is_exported && has_out_of_file_usage) {
		result.warning = "This is an exported variable, and references were found in other files. Only references in this script were renamed; references elsewhere (other scripts, the inspector, or scene files) were not updated.";
	} else if (is_exported) {
		result.warning = "This is an exported variable; references outside this script (such as in the inspector or scene files) will not be updated.";
	} else if (has_out_of_file_usage) {
		result.warning = "References to this symbol were found in other files. Only references in this script were renamed; references in other files were not updated.";
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
	} else {
		LSP::TextDocumentPositionParams doc_position = make_document_position(workspace, p_context, p_location);
		LSP::DocumentSymbol symbol;
		LSP::Range identifier_range;
		rename.enabled = workspace->can_rename(doc_position, symbol, identifier_range);
		if (!rename.enabled) {
			rename.disabled_reason = "Place the caret on a renameable symbol.";
		}
	}
#endif // GDSCRIPT_NO_LSP

	result.push_back(rename);

	RefactorAvailability extract_variable;
	extract_variable.kind = RefactorKind::EXTRACT_VARIABLE;
	extract_variable.title = "Extract Variable";
	const ExtractVariableCandidate extract_candidate = find_extract_variable_candidate(p_context, p_location);
	extract_variable.enabled = extract_candidate.enabled;
	if (!extract_variable.enabled) {
		extract_variable.disabled_reason = extract_candidate.disabled_reason;
	}
	result.push_back(extract_variable);

	RefactorAvailability add_type;
	add_type.kind = RefactorKind::ADD_TYPE_ANNOTATION;
	add_type.title = "Add Type Annotation";
	const TypeAnnotationCandidate type_candidate = find_type_annotation_candidate(p_context, p_location);
	add_type.enabled = type_candidate.enabled;
	if (!add_type.enabled) {
		add_type.disabled_reason = type_candidate.disabled_reason;
	}
	result.push_back(add_type);
	return result;
}

RefactorResult GDScriptRefactoring::prepare(const RefactorContext &p_context, const RefactorLocation &p_location, RefactorKind p_kind, const RefactorParams &p_params) {
	switch (p_kind) {
		case RefactorKind::RENAME:
			return prepare_rename(p_context, p_location, p_params);
		case RefactorKind::EXTRACT_VARIABLE:
			return prepare_extract_variable(p_context, p_location);
		case RefactorKind::ADD_TYPE_ANNOTATION:
			return prepare_type_annotation(p_context, p_location);
		default:
			break;
	}

	RefactorResult result;
	result.ok = false;
	result.error_message = "Refactor not implemented.";
	return result;
}

#endif // TOOLS_ENABLED
