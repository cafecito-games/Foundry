/**************************************************************************/
/*  test_format.h                                                         */
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

#include "../foundry_script.h"
#include "../fs_format.h"
#include "../fs_parser.h"

#include "core/io/dir_access.h"
#include "core/io/file_access.h"

#include "tests/test_macros.h"

namespace FSTests {

static String format_or_fail(const String &p_source) {
	FSFormatter formatter;
	FSFormatter::Result result;
	Error err = formatter.format(p_source, "test.fs", result);
	CHECK_MESSAGE(err == OK, "Source must format without parse errors.");
	return result.formatted;
}

// Counts the comments in `p_text` by re-tokenizing it the same way the formatter
// does. A canonical formatter must neither drop nor duplicate comments, so the
// count must be identical before and after formatting.
static int count_comments(const String &p_text) {
	FSTokenizerText tokenizer;
	tokenizer.set_source_code(p_text);
	for (FSTokenizer::Token token = tokenizer.scan();
			token.type != FSTokenizer::Token::TK_EOF && token.type != FSTokenizer::Token::ERROR;
			token = tokenizer.scan()) {
		// Drain the token stream so `get_comments()` is fully populated.
	}
	return tokenizer.get_comments().size();
}

// True when `p_line` falls inside a non-class member of `p_class` (recursing into
// nested classes). Mirrors the formatter's classifier so the invariant below
// counts the same string comments the formatter recovers.
static bool string_comment_line_inside_member(const FSParser::ClassNode *p_class, int p_line) {
	if (p_class == nullptr) {
		return false;
	}
	for (int i = 0; i < p_class->members.size(); i++) {
		const FSParser::ClassNode::Member &member = p_class->members[i];
		const FSParser::Node *node = member.get_source_node();
		if (node == nullptr || node->start_line <= 0) {
			continue;
		}
		int member_start = node->start_line;
		for (const FSParser::AnnotationNode *annotation : node->annotations) {
			if (annotation->start_line > 0 && annotation->start_line < member_start) {
				member_start = annotation->start_line;
			}
		}
		if (p_line < member_start || p_line > node->end_line) {
			continue;
		}
		if (member.type == FSParser::ClassNode::Member::CLASS) {
			return string_comment_line_inside_member(member.m_class, p_line);
		}
		return true;
	}
	return false;
}

// Counts class-body string comments: a standalone string literal (at a line start)
// the parser does NOT place inside a member -- so it consumed it as a multi-line
// comment with no AST node. This is layout-independent (it does not count
// collection/argument/pattern element strings, which sit inside a member), so it
// is the durable guard for the otherwise-invisible string-comment data-loss class.
// Returns -1 when the text does not parse cleanly.
static int count_class_body_string_comments(const String &p_text) {
	FSParser parser;
	if (parser.parse(p_text, "string_comment_count.fs", false) != OK || !parser.get_errors().is_empty()) {
		return -1;
	}
	FSTokenizerText tokenizer;
	tokenizer.set_source_code(p_text);
	int count = 0;
	FSTokenizer::Token::Type previous_type = FSTokenizer::Token::NEWLINE;
	for (FSTokenizer::Token token = tokenizer.scan();
			token.type != FSTokenizer::Token::TK_EOF && token.type != FSTokenizer::Token::ERROR;
			token = tokenizer.scan()) {
		const bool at_line_start = previous_type == FSTokenizer::Token::NEWLINE ||
				previous_type == FSTokenizer::Token::INDENT ||
				previous_type == FSTokenizer::Token::DEDENT;
		if (at_line_start && token.type == FSTokenizer::Token::LITERAL &&
				token.literal.get_type() == Variant::STRING &&
				!string_comment_line_inside_member(parser.get_tree(), token.start_line)) {
			count++;
		}
		previous_type = token.type;
	}
	return count;
}

// ---------------------------------------------------------------------------
// Corpus collection helpers.
// ---------------------------------------------------------------------------

static void collect_gd_scripts_recursive(const String &p_dir, Vector<String> &r_files) {
	Ref<DirAccess> dir = DirAccess::open(p_dir);
	if (dir.is_null()) {
		return;
	}
	dir->set_include_hidden(false);
	dir->list_dir_begin();
	for (String entry = dir->get_next(); !entry.is_empty(); entry = dir->get_next()) {
		if (entry == "." || entry == ".." || dir->current_is_hidden()) {
			continue;
		}
		const String full_path = p_dir.path_join(entry);
		if (dir->current_is_dir()) {
			collect_gd_scripts_recursive(full_path, r_files);
		} else if (entry.get_extension() == "fs") {
			r_files.push_back(full_path);
		}
	}
	dir->list_dir_end();
}

static Vector<String> collect_gd_scripts(const String &p_dir) {
	Vector<String> files;
	collect_gd_scripts_recursive(p_dir, files);
	files.sort();
	return files;
}

// A single error fixture is narrow-skipped from the idempotency and tree-
// preservation sweeps. `analyzer/errors/abstract_methods.fs` contains bodyless
// lambdas (`func():` with no body) that are invalid FoundryScript -- the analyzer
// rejects them. They parse only because surrounding parentheses (which the parser
// discards, leaving no paren node) keep the malformed construct readable;
// formatting them necessarily synthesizes a `pass` body, which both adds a
// statement to the tree and destabilizes blank-line accounting. The construct is
// not valid code any formatter is expected to round-trip, so it is excluded by
// path. Every other corpus script is swept unconditionally.
static bool is_narrow_skipped_fixture(const String &p_path) {
	return p_path.ends_with("analyzer/errors/abstract_methods.fs");
}

// Parses `p_source` with no analysis pass (the raw syntactic tree). Returns true
// and the parser by reference only when parsing produced no errors.
static bool parse_no_errors(FSParser &p_parser, const String &p_source, const String &p_path) {
	Error err = p_parser.parse(p_source, p_path, false);
	return err == OK && p_parser.get_errors().is_empty();
}

// ---------------------------------------------------------------------------
// Parse-tree structural equivalence.
//
// This is the real semantic-preservation invariant. The formatter legitimately
// rewrites the token stream (reconstructs minimal parentheses, normalizes quotes,
// `not in` -> prefix `not`, `&&` -> `and`), all of which fold to the same parsed
// tree. We compare the parsed structure only: node types, operator enums,
// identifier names, literal Variant values, and child structure -- ignoring line
// and column trivia, comments, and any post-parse analysis fields.
// ---------------------------------------------------------------------------

static bool node_eq(const FSParser::Node *p_a, const FSParser::Node *p_b);

static bool variant_literal_eq(const Variant &p_a, const Variant &p_b) {
	// Distinguish e.g. `2` (int) from `2.0` (float); both must keep their type.
	if (p_a.get_type() != p_b.get_type()) {
		return false;
	}
	return p_a == p_b;
}

static StringName identifier_name(const FSParser::IdentifierNode *p_identifier) {
	return p_identifier != nullptr ? p_identifier->name : StringName();
}

// The tokenizer folds a `-`/`+` directly before a number into a signed numeric
// literal when the preceding token cannot be a binary-operator's left operand
// (e.g. after `(`), but keeps it as a unary operator when a space intervenes
// (`- 2`). The formatter canonically drops that space, so `unary(-, 2)` becomes
// the literal `-2`. They carry the same constant value, so the comparator treats
// a unary sign on a numeric literal as equal to the folded literal. The scope is
// deliberately narrow (sign on a numeric constant only) so it cannot mask a real
// operator or operand change.
static bool numeric_literal_value(const FSParser::Node *p_node, Variant &r_value) {
	if (p_node->type == FSParser::Node::LITERAL) {
		const Variant &value = static_cast<const FSParser::LiteralNode *>(p_node)->value;
		if (value.get_type() == Variant::INT || value.get_type() == Variant::FLOAT) {
			r_value = value;
			return true;
		}
		return false;
	}
	if (p_node->type == FSParser::Node::UNARY_OPERATOR) {
		const FSParser::UnaryOpNode *unary = static_cast<const FSParser::UnaryOpNode *>(p_node);
		const bool is_sign = unary->operation == FSParser::UnaryOpNode::OP_NEGATIVE ||
				unary->operation == FSParser::UnaryOpNode::OP_POSITIVE;
		if (!is_sign || unary->operand == nullptr || unary->operand->type != FSParser::Node::LITERAL) {
			return false;
		}
		const Variant &operand = static_cast<const FSParser::LiteralNode *>(unary->operand)->value;
		if (operand.get_type() != Variant::INT && operand.get_type() != Variant::FLOAT) {
			return false;
		}
		if (unary->operation == FSParser::UnaryOpNode::OP_POSITIVE) {
			r_value = operand;
		} else if (operand.get_type() == Variant::INT) {
			r_value = -int64_t(operand);
		} else {
			r_value = -double(operand);
		}
		return true;
	}
	return false;
}

template <typename T>
static bool node_vector_eq(const Vector<T *> &p_a, const Vector<T *> &p_b) {
	if (p_a.size() != p_b.size()) {
		return false;
	}
	for (int i = 0; i < p_a.size(); i++) {
		if (!node_eq(p_a[i], p_b[i])) {
			return false;
		}
	}
	return true;
}

static bool identifier_chain_eq(const Vector<FSParser::IdentifierNode *> &p_a, const Vector<FSParser::IdentifierNode *> &p_b) {
	if (p_a.size() != p_b.size()) {
		return false;
	}
	for (int i = 0; i < p_a.size(); i++) {
		if (identifier_name(p_a[i]) != identifier_name(p_b[i])) {
			return false;
		}
	}
	return true;
}

static bool string_name_vector_eq(const Vector<StringName> &p_a, const Vector<StringName> &p_b) {
	if (p_a.size() != p_b.size()) {
		return false;
	}
	for (int i = 0; i < p_a.size(); i++) {
		if (p_a[i] != p_b[i]) {
			return false;
		}
	}
	return true;
}

static bool annotations_eq(const List<FSParser::AnnotationNode *> &p_a, const List<FSParser::AnnotationNode *> &p_b) {
	if (p_a.size() != p_b.size()) {
		return false;
	}
	const List<FSParser::AnnotationNode *>::ConstIterator end_a = p_a.end();
	List<FSParser::AnnotationNode *>::ConstIterator it_a = p_a.begin();
	List<FSParser::AnnotationNode *>::ConstIterator it_b = p_b.begin();
	for (; it_a != end_a; ++it_a, ++it_b) {
		if (!node_eq(*it_a, *it_b)) {
			return false;
		}
	}
	return true;
}

static bool assignable_eq(const FSParser::AssignableNode *p_a, const FSParser::AssignableNode *p_b) {
	return identifier_name(p_a->identifier) == identifier_name(p_b->identifier) &&
			p_a->infer_datatype == p_b->infer_datatype &&
			node_eq(p_a->datatype_specifier, p_b->datatype_specifier) &&
			node_eq(p_a->initializer, p_b->initializer);
}

static bool pattern_eq(const FSParser::PatternNode *p_a, const FSParser::PatternNode *p_b) {
	if (p_a->pattern_type != p_b->pattern_type) {
		return false;
	}
	using PatternNode = FSParser::PatternNode;
	switch (p_a->pattern_type) {
		case PatternNode::PT_LITERAL:
			return node_eq(p_a->literal, p_b->literal);
		case PatternNode::PT_EXPRESSION:
			return node_eq(p_a->expression, p_b->expression);
		case PatternNode::PT_BIND:
			return identifier_name(p_a->bind) == identifier_name(p_b->bind);
		case PatternNode::PT_ARRAY:
			return node_vector_eq(p_a->array, p_b->array) && p_a->rest_used == p_b->rest_used;
		case PatternNode::PT_DICTIONARY: {
			if (p_a->dictionary.size() != p_b->dictionary.size() || p_a->rest_used != p_b->rest_used) {
				return false;
			}
			for (int i = 0; i < p_a->dictionary.size(); i++) {
				if (!node_eq(p_a->dictionary[i].key, p_b->dictionary[i].key) ||
						!node_eq(p_a->dictionary[i].value_pattern, p_b->dictionary[i].value_pattern)) {
					return false;
				}
			}
			return true;
		}
		case PatternNode::PT_REST:
		case PatternNode::PT_WILDCARD:
			return true;
	}
	return false;
}

static bool class_member_eq(const FSParser::ClassNode::Member &p_a, const FSParser::ClassNode::Member &p_b) {
	if (p_a.type != p_b.type) {
		return false;
	}
	using Member = FSParser::ClassNode::Member;
	if (p_a.type == Member::ENUM_VALUE) {
		return identifier_name(p_a.enum_value.identifier) == identifier_name(p_b.enum_value.identifier) &&
				node_eq(p_a.enum_value.custom_value, p_b.enum_value.custom_value);
	}
	return node_eq(p_a.get_source_node(), p_b.get_source_node());
}

static bool trait_use_eq(const FSParser::ClassNode::TraitUse &p_a, const FSParser::ClassNode::TraitUse &p_b) {
	return identifier_chain_eq(p_a.name, p_b.name) && node_vector_eq(p_a.type_arguments, p_b.type_arguments);
}

static bool node_eq(const FSParser::Node *p_a, const FSParser::Node *p_b) {
	if (p_a == nullptr || p_b == nullptr) {
		return p_a == p_b;
	}
	{
		// A unary sign on a numeric literal and the equivalent signed literal carry
		// the same constant value (see `numeric_literal_value`).
		Variant value_a;
		Variant value_b;
		if (numeric_literal_value(p_a, value_a) && numeric_literal_value(p_b, value_b)) {
			return variant_literal_eq(value_a, value_b);
		}
	}
	if (p_a->type != p_b->type) {
		return false;
	}
	if (!annotations_eq(p_a->annotations, p_b->annotations)) {
		return false;
	}

	using Node = FSParser::Node;
	switch (p_a->type) {
		case Node::ANNOTATION: {
			const FSParser::AnnotationNode *a = static_cast<const FSParser::AnnotationNode *>(p_a);
			const FSParser::AnnotationNode *b = static_cast<const FSParser::AnnotationNode *>(p_b);
			return a->name == b->name && node_vector_eq(a->arguments, b->arguments) &&
					string_name_vector_eq(a->argument_names, b->argument_names);
		}
		case Node::ANNOTATION_DECLARATION: {
			const FSParser::AnnotationDeclarationNode *a = static_cast<const FSParser::AnnotationDeclarationNode *>(p_a);
			const FSParser::AnnotationDeclarationNode *b = static_cast<const FSParser::AnnotationDeclarationNode *>(p_b);
			return identifier_name(a->identifier) == identifier_name(b->identifier) && a->targets == b->targets &&
					node_vector_eq(a->parameters, b->parameters) && node_eq(a->rest_parameter, b->rest_parameter);
		}
		case Node::ARRAY: {
			const FSParser::ArrayNode *a = static_cast<const FSParser::ArrayNode *>(p_a);
			const FSParser::ArrayNode *b = static_cast<const FSParser::ArrayNode *>(p_b);
			return node_vector_eq(a->elements, b->elements);
		}
		case Node::ASSERT: {
			const FSParser::AssertNode *a = static_cast<const FSParser::AssertNode *>(p_a);
			const FSParser::AssertNode *b = static_cast<const FSParser::AssertNode *>(p_b);
			return node_eq(a->condition, b->condition) && node_eq(a->message, b->message);
		}
		case Node::ASSIGNMENT: {
			const FSParser::AssignmentNode *a = static_cast<const FSParser::AssignmentNode *>(p_a);
			const FSParser::AssignmentNode *b = static_cast<const FSParser::AssignmentNode *>(p_b);
			return a->operation == b->operation && node_eq(a->assignee, b->assignee) &&
					node_eq(a->assigned_value, b->assigned_value);
		}
		case Node::AWAIT: {
			const FSParser::AwaitNode *a = static_cast<const FSParser::AwaitNode *>(p_a);
			const FSParser::AwaitNode *b = static_cast<const FSParser::AwaitNode *>(p_b);
			return node_eq(a->to_await, b->to_await);
		}
		case Node::BINARY_OPERATOR: {
			const FSParser::BinaryOpNode *a = static_cast<const FSParser::BinaryOpNode *>(p_a);
			const FSParser::BinaryOpNode *b = static_cast<const FSParser::BinaryOpNode *>(p_b);
			return a->operation == b->operation && node_eq(a->left_operand, b->left_operand) &&
					node_eq(a->right_operand, b->right_operand);
		}
		case Node::BREAK:
		case Node::BREAKPOINT:
		case Node::CONTINUE:
		case Node::PASS:
		case Node::SELF:
			return true;
		case Node::CALL: {
			const FSParser::CallNode *a = static_cast<const FSParser::CallNode *>(p_a);
			const FSParser::CallNode *b = static_cast<const FSParser::CallNode *>(p_b);
			return a->function_name == b->function_name && a->is_super == b->is_super &&
					node_eq(a->callee, b->callee) && node_vector_eq(a->arguments, b->arguments) &&
					string_name_vector_eq(a->argument_names, b->argument_names);
		}
		case Node::CAST: {
			const FSParser::CastNode *a = static_cast<const FSParser::CastNode *>(p_a);
			const FSParser::CastNode *b = static_cast<const FSParser::CastNode *>(p_b);
			return node_eq(a->operand, b->operand) && node_eq(a->cast_type, b->cast_type);
		}
		case Node::CLASS: {
			const FSParser::ClassNode *a = static_cast<const FSParser::ClassNode *>(p_a);
			const FSParser::ClassNode *b = static_cast<const FSParser::ClassNode *>(p_b);
			if (identifier_name(a->identifier) != identifier_name(b->identifier) ||
					a->is_abstract != b->is_abstract || a->is_final != b->is_final ||
					a->is_trait != b->is_trait || a->trait_name_used != b->trait_name_used ||
					a->extends_used != b->extends_used || a->extends_path != b->extends_path ||
					a->icon_path != b->icon_path || a->annotated_static_unload != b->annotated_static_unload) {
				return false;
			}
			if (!identifier_chain_eq(a->extends, b->extends) ||
					!node_vector_eq(a->extends_type_arguments, b->extends_type_arguments) ||
					!node_vector_eq(a->type_parameters, b->type_parameters) ||
					!node_vector_eq(a->annotation_declarations, b->annotation_declarations)) {
				return false;
			}
			if (a->used_traits.size() != b->used_traits.size()) {
				return false;
			}
			for (int i = 0; i < a->used_traits.size(); i++) {
				if (!trait_use_eq(a->used_traits[i], b->used_traits[i])) {
					return false;
				}
			}
			if (a->members.size() != b->members.size()) {
				return false;
			}
			for (int i = 0; i < a->members.size(); i++) {
				if (!class_member_eq(a->members[i], b->members[i])) {
					return false;
				}
			}
			return true;
		}
		case Node::CONSTANT:
		case Node::PARAMETER:
			return assignable_eq(static_cast<const FSParser::AssignableNode *>(p_a),
					static_cast<const FSParser::AssignableNode *>(p_b));
		case Node::VARIABLE: {
			const FSParser::VariableNode *a = static_cast<const FSParser::VariableNode *>(p_a);
			const FSParser::VariableNode *b = static_cast<const FSParser::VariableNode *>(p_b);
			if (!assignable_eq(a, b) || a->property != b->property || a->is_static != b->is_static ||
					a->is_final != b->is_final || a->onready != b->onready || a->exported != b->exported) {
				return false;
			}
			if (a->property == FSParser::VariableNode::PROP_SETGET) {
				if (identifier_name(a->setter_pointer) != identifier_name(b->setter_pointer) ||
						identifier_name(a->getter_pointer) != identifier_name(b->getter_pointer)) {
					return false;
				}
			} else if (a->property == FSParser::VariableNode::PROP_INLINE) {
				if (!node_eq(a->setter, b->setter) || !node_eq(a->getter, b->getter)) {
					return false;
				}
			}
			return true;
		}
		case Node::DICTIONARY: {
			const FSParser::DictionaryNode *a = static_cast<const FSParser::DictionaryNode *>(p_a);
			const FSParser::DictionaryNode *b = static_cast<const FSParser::DictionaryNode *>(p_b);
			if (a->elements.size() != b->elements.size()) {
				return false;
			}
			for (int i = 0; i < a->elements.size(); i++) {
				if (!node_eq(a->elements[i].key, b->elements[i].key) ||
						!node_eq(a->elements[i].value, b->elements[i].value)) {
					return false;
				}
			}
			return true;
		}
		case Node::ENUM: {
			const FSParser::EnumNode *a = static_cast<const FSParser::EnumNode *>(p_a);
			const FSParser::EnumNode *b = static_cast<const FSParser::EnumNode *>(p_b);
			if (identifier_name(a->identifier) != identifier_name(b->identifier) ||
					a->values.size() != b->values.size()) {
				return false;
			}
			for (int i = 0; i < a->values.size(); i++) {
				if (identifier_name(a->values[i].identifier) != identifier_name(b->values[i].identifier) ||
						!node_eq(a->values[i].custom_value, b->values[i].custom_value)) {
					return false;
				}
			}
			return true;
		}
		case Node::FOR: {
			const FSParser::ForNode *a = static_cast<const FSParser::ForNode *>(p_a);
			const FSParser::ForNode *b = static_cast<const FSParser::ForNode *>(p_b);
			return identifier_name(a->variable) == identifier_name(b->variable) &&
					node_eq(a->datatype_specifier, b->datatype_specifier) && node_eq(a->list, b->list) &&
					node_eq(a->loop, b->loop);
		}
		case Node::FUNCTION: {
			const FSParser::FunctionNode *a = static_cast<const FSParser::FunctionNode *>(p_a);
			const FSParser::FunctionNode *b = static_cast<const FSParser::FunctionNode *>(p_b);
			return identifier_name(a->identifier) == identifier_name(b->identifier) &&
					a->is_abstract == b->is_abstract && a->is_final == b->is_final &&
					a->is_static == b->is_static && a->is_declared_async == b->is_declared_async &&
					node_vector_eq(a->type_parameters, b->type_parameters) &&
					node_vector_eq(a->parameters, b->parameters) &&
					node_eq(a->rest_parameter, b->rest_parameter) &&
					node_eq(a->return_type, b->return_type) && node_eq(a->body, b->body);
		}
		case Node::GET_NODE: {
			const FSParser::GetNodeNode *a = static_cast<const FSParser::GetNodeNode *>(p_a);
			const FSParser::GetNodeNode *b = static_cast<const FSParser::GetNodeNode *>(p_b);
			return a->full_path == b->full_path && a->use_dollar == b->use_dollar;
		}
		case Node::IDENTIFIER: {
			const FSParser::IdentifierNode *a = static_cast<const FSParser::IdentifierNode *>(p_a);
			const FSParser::IdentifierNode *b = static_cast<const FSParser::IdentifierNode *>(p_b);
			return a->name == b->name;
		}
		case Node::IF: {
			const FSParser::IfNode *a = static_cast<const FSParser::IfNode *>(p_a);
			const FSParser::IfNode *b = static_cast<const FSParser::IfNode *>(p_b);
			return node_eq(a->condition, b->condition) && node_eq(a->true_block, b->true_block) &&
					node_eq(a->false_block, b->false_block);
		}
		case Node::LAMBDA: {
			const FSParser::LambdaNode *a = static_cast<const FSParser::LambdaNode *>(p_a);
			const FSParser::LambdaNode *b = static_cast<const FSParser::LambdaNode *>(p_b);
			return node_eq(a->function, b->function);
		}
		case Node::LITERAL: {
			const FSParser::LiteralNode *a = static_cast<const FSParser::LiteralNode *>(p_a);
			const FSParser::LiteralNode *b = static_cast<const FSParser::LiteralNode *>(p_b);
			return variant_literal_eq(a->value, b->value);
		}
		case Node::MATCH: {
			const FSParser::MatchNode *a = static_cast<const FSParser::MatchNode *>(p_a);
			const FSParser::MatchNode *b = static_cast<const FSParser::MatchNode *>(p_b);
			return node_eq(a->test, b->test) && node_vector_eq(a->branches, b->branches);
		}
		case Node::MATCH_BRANCH: {
			const FSParser::MatchBranchNode *a = static_cast<const FSParser::MatchBranchNode *>(p_a);
			const FSParser::MatchBranchNode *b = static_cast<const FSParser::MatchBranchNode *>(p_b);
			if (a->patterns.size() != b->patterns.size() || a->has_wildcard != b->has_wildcard) {
				return false;
			}
			for (int i = 0; i < a->patterns.size(); i++) {
				if (!pattern_eq(a->patterns[i], b->patterns[i])) {
					return false;
				}
			}
			return node_eq(a->guard_body, b->guard_body) && node_eq(a->block, b->block);
		}
		case Node::PATTERN:
			return pattern_eq(static_cast<const FSParser::PatternNode *>(p_a),
					static_cast<const FSParser::PatternNode *>(p_b));
		case Node::PRELOAD: {
			const FSParser::PreloadNode *a = static_cast<const FSParser::PreloadNode *>(p_a);
			const FSParser::PreloadNode *b = static_cast<const FSParser::PreloadNode *>(p_b);
			return node_eq(a->path, b->path);
		}
		case Node::RETURN: {
			const FSParser::ReturnNode *a = static_cast<const FSParser::ReturnNode *>(p_a);
			const FSParser::ReturnNode *b = static_cast<const FSParser::ReturnNode *>(p_b);
			return a->void_return == b->void_return && node_eq(a->return_value, b->return_value);
		}
		case Node::SIGNAL: {
			const FSParser::SignalNode *a = static_cast<const FSParser::SignalNode *>(p_a);
			const FSParser::SignalNode *b = static_cast<const FSParser::SignalNode *>(p_b);
			return identifier_name(a->identifier) == identifier_name(b->identifier) &&
					node_vector_eq(a->parameters, b->parameters);
		}
		case Node::SUBSCRIPT: {
			const FSParser::SubscriptNode *a = static_cast<const FSParser::SubscriptNode *>(p_a);
			const FSParser::SubscriptNode *b = static_cast<const FSParser::SubscriptNode *>(p_b);
			if (a->is_attribute != b->is_attribute || !node_eq(a->base, b->base)) {
				return false;
			}
			if (a->is_attribute) {
				return identifier_name(a->attribute) == identifier_name(b->attribute);
			}
			if (a->type_argument_is_nullable.size() != b->type_argument_is_nullable.size()) {
				return false;
			}
			for (int i = 0; i < a->type_argument_is_nullable.size(); i++) {
				if (a->type_argument_is_nullable[i] != b->type_argument_is_nullable[i]) {
					return false;
				}
			}
			return node_vector_eq(a->type_arguments, b->type_arguments) && node_eq(a->index, b->index);
		}
		case Node::SUITE: {
			const FSParser::SuiteNode *a = static_cast<const FSParser::SuiteNode *>(p_a);
			const FSParser::SuiteNode *b = static_cast<const FSParser::SuiteNode *>(p_b);
			return node_vector_eq(a->statements, b->statements);
		}
		case Node::TERNARY_OPERATOR: {
			const FSParser::TernaryOpNode *a = static_cast<const FSParser::TernaryOpNode *>(p_a);
			const FSParser::TernaryOpNode *b = static_cast<const FSParser::TernaryOpNode *>(p_b);
			return node_eq(a->condition, b->condition) && node_eq(a->true_expr, b->true_expr) &&
					node_eq(a->false_expr, b->false_expr);
		}
		case Node::TYPE: {
			const FSParser::TypeNode *a = static_cast<const FSParser::TypeNode *>(p_a);
			const FSParser::TypeNode *b = static_cast<const FSParser::TypeNode *>(p_b);
			return identifier_chain_eq(a->type_chain, b->type_chain) &&
					node_vector_eq(a->container_types, b->container_types) &&
					a->has_signature == b->has_signature && a->signature_is_async == b->signature_is_async &&
					a->is_nullable == b->is_nullable &&
					node_vector_eq(a->signature_parameter_types, b->signature_parameter_types) &&
					node_eq(a->signature_return_type, b->signature_return_type);
		}
		case Node::TYPE_PARAMETER: {
			const FSParser::TypeParameterNode *a = static_cast<const FSParser::TypeParameterNode *>(p_a);
			const FSParser::TypeParameterNode *b = static_cast<const FSParser::TypeParameterNode *>(p_b);
			return identifier_name(a->identifier) == identifier_name(b->identifier) && node_eq(a->bound, b->bound);
		}
		case Node::TYPE_TEST: {
			const FSParser::TypeTestNode *a = static_cast<const FSParser::TypeTestNode *>(p_a);
			const FSParser::TypeTestNode *b = static_cast<const FSParser::TypeTestNode *>(p_b);
			return node_eq(a->operand, b->operand) && node_eq(a->test_type, b->test_type);
		}
		case Node::UNARY_OPERATOR: {
			const FSParser::UnaryOpNode *a = static_cast<const FSParser::UnaryOpNode *>(p_a);
			const FSParser::UnaryOpNode *b = static_cast<const FSParser::UnaryOpNode *>(p_b);
			return a->operation == b->operation && node_eq(a->operand, b->operand);
		}
		case Node::WHILE: {
			const FSParser::WhileNode *a = static_cast<const FSParser::WhileNode *>(p_a);
			const FSParser::WhileNode *b = static_cast<const FSParser::WhileNode *>(p_b);
			return node_eq(a->condition, b->condition) && node_eq(a->loop, b->loop);
		}
		case Node::NONE:
			return true;
	}
	return false;
}

// Parses both the original and formatted source and compares the trees. Returns
// false when the formatted text fails to parse or diverges structurally.
static bool trees_equivalent(const String &p_original, const String &p_formatted, const String &p_path) {
	FSParser parser_original;
	FSParser parser_formatted;
	if (!parse_no_errors(parser_original, p_original, p_path)) {
		// The original did not parse cleanly; the formatter would have refused it,
		// so there is nothing to compare.
		return true;
	}
	if (!parse_no_errors(parser_formatted, p_formatted, p_path)) {
		return false;
	}
	if (parser_original.is_tool() != parser_formatted.is_tool()) {
		return false;
	}
	return node_eq(parser_original.get_tree(), parser_formatted.get_tree());
}

TEST_SUITE("[Modules][FoundryScript][Format]") {
	TEST_CASE("[Format] Reindents structurally with tabs") {
		String source = "func f():\n        return     1+2\n";
		String expected = "func f():\n\treturn 1 + 2\n";
		CHECK_EQ(format_or_fail(source), expected);
	}

	TEST_CASE("[Format] Refuses to format on parse error") {
		FSFormatter formatter;
		FSFormatter::Result result;
		Error err = formatter.format("func (:\n", "bad.fs", result);
		CHECK(err != OK);
		CHECK(result.formatted.is_empty());
		CHECK(result.error_line > 0);
	}

	TEST_CASE("[Format] Preserves string contents and normalizes to double quotes") {
		// Quote normalization itself lands in Task 3; here we assert the
		// literal text round-trips via the token index rather than the Variant.
		CHECK_EQ(format_or_fail("var s = \"a\\tb\"\n"), "var s = \"a\\tb\"\n");
	}

	TEST_CASE("[Format] Re-quotes node paths that need quoting") {
		// A quoted node name must stay quoted so the token stream is preserved.
		CHECK_EQ(format_or_fail("var n = $\"My Node\"\n"), "var n = $\"My Node\"\n");
	}

	TEST_CASE("[Format] Keeps bare node paths unquoted") {
		CHECK_EQ(format_or_fail("var n = $Player/Sprite\n"), "var n = $Player/Sprite\n");
	}

	TEST_CASE("[Format] Keeps a full-line comment above a statement at its indent") {
		String source = "func f():\n#hi\n\treturn 1\n";
		String expected = "func f():\n\t# hi\n\treturn 1\n";
		CHECK_EQ(format_or_fail(source), expected);
	}

	TEST_CASE("[Format] Keeps an inline comment trailing the statement") {
		String source = "var x = 1 #count\n";
		String expected = "var x = 1  # count\n";
		CHECK_EQ(format_or_fail(source), expected);
	}

	TEST_CASE("[Format] Collapses multiple blank lines inside a block to one") {
		String source = "func f():\n\tvar a = 1\n\n\n\tvar b = 2\n";
		String expected = "func f():\n\tvar a = 1\n\n\tvar b = 2\n";
		CHECK_EQ(format_or_fail(source), expected);
	}

	TEST_CASE("[Format] Two blank lines between top-level functions") {
		String source = "func a():\n\tpass\nfunc b():\n\tpass\n";
		String expected = "func a():\n\tpass\n\n\nfunc b():\n\tpass\n";
		CHECK_EQ(format_or_fail(source), expected);
	}

	TEST_CASE("[Format] One blank line between methods inside an inner class") {
		String source = "class Inner:\n\tfunc a():\n\t\tpass\n\tfunc b():\n\t\tpass\n";
		String expected = "class Inner:\n\tfunc a():\n\t\tpass\n\n\tfunc b():\n\t\tpass\n";
		CHECK_EQ(format_or_fail(source), expected);
	}

	TEST_CASE("[Format] Places a full-line comment above the correct statement") {
		// Guards line-index alignment: the comment sits between `b` and `d` and
		// must stay above `d`, never drift up to `a`/`b` (off-by-one regression).
		String source = "var a = 1\nvar b = 2\n# c\nvar d = 3\n";
		String expected = "var a = 1\nvar b = 2\n# c\nvar d = 3\n";
		CHECK_EQ(format_or_fail(source), expected);
	}

	TEST_CASE("[Format] Emits mixed inline and full-line comments exactly once") {
		String source = "func f():\n\tvar a = 1 #first\n\t#second\n\treturn a\n";
		String expected = "func f():\n\tvar a = 1  # first\n\t# second\n\treturn a\n";
		CHECK_EQ(format_or_fail(source), expected);
	}

	TEST_CASE("[Format] Preserves a comment-only file") {
		CHECK_EQ(format_or_fail("# just a comment\n"), "# just a comment\n");
	}

	TEST_CASE("[Format] Preserves a comment after the last statement") {
		String source = "var x = 1\n# trailing\n";
		String expected = "var x = 1\n# trailing\n";
		CHECK_EQ(format_or_fail(source), expected);
	}

	TEST_CASE("[Format] Keeps a trailing comment inside the function body") {
		String source = "func f():\n\tvar a = 1\n\t# trailing in f\nfunc g():\n\tpass\n";
		String expected = "func f():\n\tvar a = 1\n\t# trailing in f\n\n\nfunc g():\n\tpass\n";
		CHECK_EQ(format_or_fail(source), expected);
	}

	TEST_CASE("[Format] Keeps a trailing comment inside an inner-class method") {
		String source = "class Inner:\n\tfunc a():\n\t\tvar x = 1\n\t\t# trailing in a\n\tfunc b():\n\t\tpass\n";
		String expected = "class Inner:\n\tfunc a():\n\t\tvar x = 1\n\t\t# trailing in a\n\n\tfunc b():\n\t\tpass\n";
		CHECK_EQ(format_or_fail(source), expected);
	}

	TEST_CASE("[Format] Enforces one space after the hash") {
		CHECK_EQ(format_or_fail("#foo\n"), "# foo\n");
	}

	TEST_CASE("[Format] Leaves doc comments and shebang lines untouched") {
		CHECK_EQ(format_or_fail("#!shebang\n##doc comment\nvar x = 1\n"),
				"#!shebang\n##doc comment\nvar x = 1\n");
	}

	TEST_CASE("[Format] Normalizes single quotes to double") {
		CHECK_EQ(format_or_fail("var s = 'hi'\n"), "var s = \"hi\"\n");
	}

	TEST_CASE("[Format] Keeps single quotes when content has a double quote") {
		CHECK_EQ(format_or_fail("var s = 'say \"hi\"'\n"), "var s = 'say \"hi\"'\n");
	}

	TEST_CASE("[Format] Leaves double-quoted strings untouched") {
		CHECK_EQ(format_or_fail("var s = \"hi\"\n"), "var s = \"hi\"\n");
	}

	TEST_CASE("[Format] Leaves triple-quoted strings untouched") {
		CHECK_EQ(format_or_fail("var s = '''hi'''\n"), "var s = '''hi'''\n");
	}

	TEST_CASE("[Format] Leaves raw strings untouched") {
		CHECK_EQ(format_or_fail("var s = r'hi'\n"), "var s = r'hi'\n");
	}

	TEST_CASE("[Format] Normalizes hex literal casing") {
		CHECK_EQ(format_or_fail("var n = 0Xff\n"), "var n = 0xFF\n");
	}

	TEST_CASE("[Format] Normalizes binary literal prefix casing") {
		CHECK_EQ(format_or_fail("var n = 0B1010\n"), "var n = 0b1010\n");
	}

	TEST_CASE("[Format] Normalizes exponent casing and preserves underscores") {
		CHECK_EQ(format_or_fail("var n = 1_000E3\n"), "var n = 1_000e3\n");
		CHECK_EQ(format_or_fail("var h = 0xDE_AD\n"), "var h = 0xDE_AD\n");
	}

	TEST_CASE("[Format] Single-line array has no trailing comma") {
		CHECK_EQ(format_or_fail("var a = [1, 2, 3]\n"), "var a = [1, 2, 3]\n");
	}

	TEST_CASE("[Format] Multi-line array gains a trailing comma") {
		CHECK_EQ(format_or_fail("var a = [\n\t1,\n\t2\n]\n"),
				"var a = [\n\t1,\n\t2,\n]\n");
	}

	TEST_CASE("[Format] Multi-line dictionary gains a trailing comma") {
		CHECK_EQ(format_or_fail("var d = {\n\t\"a\": 1,\n\t\"b\": 2\n}\n"),
				"var d = {\n\t\"a\": 1,\n\t\"b\": 2,\n}\n");
	}

	TEST_CASE("[Format] Multi-line call arguments gain a trailing comma") {
		CHECK_EQ(format_or_fail("func f():\n\tfoo(\n\t\t1,\n\t\t2\n\t)\n"),
				"func f():\n\tfoo(\n\t\t1,\n\t\t2,\n\t)\n");
	}

	TEST_CASE("[Format] Keeps parentheses required by precedence") {
		CHECK_EQ(format_or_fail("var n = (1 + 2) * 3\n"), "var n = (1 + 2) * 3\n");
	}

	TEST_CASE("[Format] Drops parentheses not required by precedence") {
		CHECK_EQ(format_or_fail("var n = 1 + (2 * 3)\n"), "var n = 1 + 2 * 3\n");
	}

	TEST_CASE("[Format] Keeps parentheses for left-associativity grouping") {
		CHECK_EQ(format_or_fail("var n = 1 - (2 - 3)\n"), "var n = 1 - (2 - 3)\n");
		CHECK_EQ(format_or_fail("var n = 12 / (3 / 2)\n"), "var n = 12 / (3 / 2)\n");
	}

	TEST_CASE("[Format] Drops parentheses on the left for left-associativity") {
		CHECK_EQ(format_or_fail("var n = (1 - 2) - 3\n"), "var n = 1 - 2 - 3\n");
	}

	TEST_CASE("[Format] Keeps parentheses across logical operator precedence") {
		CHECK_EQ(format_or_fail("var b = (a or b) and c\n"), "var b = (a or b) and c\n");
		CHECK_EQ(format_or_fail("var b = a or (b and c)\n"), "var b = a or b and c\n");
	}

	TEST_CASE("[Format] Parenthesizes a unary operand by precedence") {
		CHECK_EQ(format_or_fail("var n = -a * b\n"), "var n = -a * b\n");
		CHECK_EQ(format_or_fail("var n = -(a * b)\n"), "var n = -(a * b)\n");
	}

	TEST_CASE("[Format] Parenthesizes nested ternary on the value side") {
		CHECK_EQ(format_or_fail("var n = (a if b else c) if d else e\n"),
				"var n = (a if b else c) if d else e\n");
		CHECK_EQ(format_or_fail("var n = a if b else c if d else e\n"),
				"var n = a if b else c if d else e\n");
	}

	TEST_CASE("[Format] Parenthesizes an await operand by precedence") {
		CHECK_EQ(format_or_fail("func f():\n\tawait (a + b)\n"),
				"func f():\n\tawait (a + b)\n");
		CHECK_EQ(format_or_fail("func f():\n\tawait a + b\n"),
				"func f():\n\tawait a + b\n");
	}

	TEST_CASE("[Format] Parenthesizes a cast operand by precedence") {
		CHECK_EQ(format_or_fail("var n = (a + b) as int\n"), "var n = a + b as int\n");
		CHECK_EQ(format_or_fail("var n = a + (b as int)\n"), "var n = a + (b as int)\n");
	}

	TEST_CASE("[Format] Generic type parameters spaced canonically") {
		CHECK_EQ(format_or_fail("class Box[T,U]:\n\tpass\n"), "class Box[T, U]:\n\tpass\n");
	}

	TEST_CASE("[Format] AsyncCallable signature spacing") {
		CHECK_EQ(format_or_fail("var f: AsyncCallable[[int,String],bool]\n"),
				"var f: AsyncCallable[[int, String], bool]\n");
	}

	TEST_CASE("[Format] CLI option parsing recognizes modes and paths") {
		List<String> args;
		args.push_back("--headless");
		args.push_back("--foundry_script-format");
		args.push_back("--check");
		args.push_back("a.fs");
		args.push_back("dir");
		FSFormatterCLI::Options options = FSFormatterCLI::parse_options(args);
		CHECK_EQ(options.mode, FSFormatterCLI::MODE_CHECK);
		CHECK_FALSE(options.read_stdin);
		REQUIRE_EQ(options.paths.size(), 2);
		CHECK_EQ(options.paths[0], "a.fs");
		CHECK_EQ(options.paths[1], "dir");
	}

	TEST_CASE("[Format] CLI option parsing defaults to stdin and write/diff flags") {
		List<String> only_command;
		only_command.push_back("--foundry_script-format");
		FSFormatterCLI::Options defaulted = FSFormatterCLI::parse_options(only_command);
		CHECK(defaulted.read_stdin);
		CHECK_EQ(defaulted.mode, FSFormatterCLI::MODE_STDOUT);

		List<String> dash;
		dash.push_back("--foundry_script-format");
		dash.push_back("-w");
		dash.push_back("-");
		FSFormatterCLI::Options stdin_write = FSFormatterCLI::parse_options(dash);
		CHECK_EQ(stdin_write.mode, FSFormatterCLI::MODE_WRITE);
		CHECK(stdin_write.read_stdin);

		List<String> diff;
		diff.push_back("--foundry_script-format");
		diff.push_back("--diff");
		diff.push_back("x.fs");
		FSFormatterCLI::Options diff_options = FSFormatterCLI::parse_options(diff);
		CHECK_EQ(diff_options.mode, FSFormatterCLI::MODE_DIFF);
		CHECK_FALSE(diff_options.read_stdin);
	}

	TEST_CASE("[Format] CLI option parsing flags mixed stdin and paths") {
		// `--foundry_script-format - file.fs` requests both stdin and a path. `run_from_cmdline`
		// rejects this state (it would otherwise silently format only stdin and drop the
		// file); the parser surfaces it as `read_stdin` with a non-empty `paths`.
		List<String> mixed;
		mixed.push_back("--foundry_script-format");
		mixed.push_back("-");
		mixed.push_back("file.fs");
		FSFormatterCLI::Options options = FSFormatterCLI::parse_options(mixed);
		CHECK(options.read_stdin);
		REQUIRE_EQ(options.paths.size(), 1);
		CHECK_EQ(options.paths[0], "file.fs");
	}

	TEST_CASE("[Format] Directory collection skips symlinked subdirectories") {
		Ref<DirAccess> da = DirAccess::create(DirAccess::ACCESS_FILESYSTEM);
		REQUIRE(da.is_valid());
		const String root = da->get_current_dir().path_join("gdfmt_fs_symtest");
		const String relative_tree = "gdfmt_fs_symtest/tree";

		const auto cleanup = [&]() {
			da->remove(root.path_join("tree/link"));
			da->remove(root.path_join("tree/keep.fs"));
			da->remove(root.path_join("outside/outside.fs"));
			da->remove(root.path_join("tree"));
			da->remove(root.path_join("outside"));
			da->remove(root);
		};
		cleanup(); // Clear any tree leaked by a previous crashed run.

		REQUIRE(da->make_dir_recursive(root.path_join("tree")) == OK);
		REQUIRE(da->make_dir_recursive(root.path_join("outside")) == OK);
		{
			Ref<FileAccess> file = FileAccess::open(root.path_join("tree/keep.fs"), FileAccess::WRITE);
			REQUIRE(file.is_valid());
			file->store_string("var a = 1\n");
		}
		{
			Ref<FileAccess> file = FileAccess::open(root.path_join("outside/outside.fs"), FileAccess::WRITE);
			REQUIRE(file.is_valid());
			file->store_string("var b = 2\n");
		}
		// A directory symlink inside the scanned tree pointing at a sibling outside it.
		REQUIRE(da->create_link(root.path_join("outside"), root.path_join("tree/link")) == OK);

		const auto sees_outside = [](const Vector<String> &p_files) {
			for (const String &collected : p_files) {
				if (collected.ends_with("outside.fs")) {
					return true;
				}
			}
			return false;
		};
		const auto sees_keep = [](const Vector<String> &p_files) {
			for (const String &collected : p_files) {
				if (collected.ends_with("keep.fs")) {
					return true;
				}
			}
			return false;
		};

		// Relative root is the regression: `is_link` must be queried with the bare
		// entry name, not the joined relative path.
		{
			Vector<String> paths;
			paths.push_back(relative_tree);
			bool had_error = false;
			const Vector<String> files = FSFormatterCLI::collect_files(paths, had_error);
			CHECK(sees_keep(files));
			CHECK_FALSE_MESSAGE(sees_outside(files), "Symlinked dir must not be followed (relative root).");
			CHECK_FALSE(had_error);
		}
		// Absolute root.
		{
			Vector<String> paths;
			paths.push_back(root.path_join("tree"));
			bool had_error = false;
			const Vector<String> files = FSFormatterCLI::collect_files(paths, had_error);
			CHECK(sees_keep(files));
			CHECK_FALSE_MESSAGE(sees_outside(files), "Symlinked dir must not be followed (absolute root).");
			CHECK_FALSE(had_error);
		}

		cleanup();
	}

	TEST_CASE("[Format] Directory collection flags a missing path") {
		Vector<String> paths;
		paths.push_back("gdfmt_this_path_does_not_exist_42/none.fs");
		bool had_error = false;
		// The missing path is reported to stderr by design; silence that expected
		// diagnostic so it doesn't masquerade as a real failure in the test log.
		ERR_PRINT_OFF;
		const Vector<String> files = FSFormatterCLI::collect_files(paths, had_error);
		ERR_PRINT_ON;
		CHECK(files.is_empty());
		CHECK_MESSAGE(had_error, "A missing path must mark the collection as failed.");
	}

	TEST_CASE("[Format] Class annotation is emitted after namespace and re-parses") {
		// Regression: class-level annotations must follow `namespace`/`import`;
		// emitting them first is rejected ("Class annotations must appear after
		// \"namespace\" and \"import\" declarations.").
		const String source =
				"namespace cafecito.demo\n\n"
				"@marker\n"
				"class_name Thing\n"
				"extends RefCounted\n\n"
				"annotation marker targets CLASS\n";
		const String formatted = format_or_fail(source);
		const int namespace_pos = formatted.find("namespace ");
		const int marker_pos = formatted.find("@marker");
		CHECK(namespace_pos >= 0);
		CHECK_MESSAGE(marker_pos > namespace_pos, "Class annotation must follow namespace.");
		FSParser reparser;
		CHECK_MESSAGE(parse_no_errors(reparser, formatted, "reformatted.fs"),
				"Formatted namespace + class annotation must re-parse cleanly.");
	}

	TEST_CASE("[Format] String-valued paths escape special characters") {
		// Regression: `extends`/`@icon` paths are reconstructed from the parser's
		// already-decoded String value. A path containing `"` or `\` must be escaped,
		// or the output is an invalid/unterminated literal that re-parses differently.
		{
			// extends path containing `"` (`\"`) and `\` (`\\`).
			const String source = "extends \"res://a\\\"b\\\\c.fs\"\n";
			const String formatted = format_or_fail(source);
			FSParser reparser;
			CHECK_MESSAGE(parse_no_errors(reparser, formatted, "x.fs"),
					vformat("Formatted extends path must re-parse: %s", formatted));
			CHECK_MESSAGE(trees_equivalent(source, formatted, "x.fs"),
					"Formatting must preserve the extends path value.");
		}
		{
			// @icon path containing an embedded quote.
			const String source = "@icon(\"res://a\\\"b.svg\")\nextends RefCounted\n";
			const String formatted = format_or_fail(source);
			FSParser reparser;
			CHECK_MESSAGE(parse_no_errors(reparser, formatted, "y.fs"),
					vformat("Formatted @icon path must re-parse: %s", formatted));
			CHECK_MESSAGE(trees_equivalent(source, formatted, "y.fs"),
					"Formatting must preserve the @icon path value.");
		}
		{
			// U+202E (right-to-left override) is rejected raw in a string literal, so
			// the decoded value must round-trip as a `\uXXXX` escape, not raw text.
			const String source = "extends \"res://\\u202e.fs\"\n";
			const String formatted = format_or_fail(source);
			CHECK_MESSAGE(formatted.contains("\\u202e"), vformat("Bidi control must stay escaped: %s", formatted));
			FSParser reparser;
			CHECK_MESSAGE(parse_no_errors(reparser, formatted, "z.fs"),
					vformat("Formatted bidi-control path must re-parse: %s", formatted));
			CHECK_MESSAGE(trees_equivalent(source, formatted, "z.fs"),
					"Formatting must preserve the bidi-control path value.");
		}
	}

	TEST_CASE("[Format] Standalone warning-region annotations survive formatting") {
		// Regression: `@warning_ignore_start`/`@warning_ignore_restore` are applied
		// by the parser and dropped from the tree; the formatter recovers them from
		// its own tokenize pass so they are not silently lost.
		const String source =
				"func f():\n"
				"\t@warning_ignore_start(\"unused_variable\")\n"
				"\tvar x = 1\n"
				"\t@warning_ignore_restore(\"unused_variable\")\n"
				"\treturn 0\n";
		const String formatted = format_or_fail(source);
		CHECK(formatted.contains("@warning_ignore_start(\"unused_variable\")"));
		CHECK(formatted.contains("@warning_ignore_restore(\"unused_variable\")"));
		FSParser reparser;
		CHECK(parse_no_errors(reparser, formatted, "reformatted.fs"));
	}

	TEST_CASE("[Format] Golden fixtures match byte-for-byte") {
		const String root = "modules/foundry_script/tests/scripts/format";
		Vector<String> inputs;
		for (const String &script : collect_gd_scripts(root)) {
			if (script.get_file() == "input.fs") {
				inputs.push_back(script);
			}
		}
		CHECK_MESSAGE(!inputs.is_empty(), vformat("Expected at least one fixture under %s", root));

		for (const String &input_path : inputs) {
			const String expected_path = input_path.get_base_dir().path_join("expected.fs");
			Error read_error = OK;
			const String source = FileAccess::get_file_as_string(input_path, &read_error);
			REQUIRE_MESSAGE(read_error == OK, vformat("Could not read fixture input: %s", input_path));
			const String expected = FileAccess::get_file_as_string(expected_path, &read_error);
			REQUIRE_MESSAGE(read_error == OK, vformat("Missing fixture expected.fs for: %s", input_path));

			FSFormatter formatter;
			FSFormatter::Result result;
			const Error err = formatter.format(source, input_path, result);
			CHECK_MESSAGE(err == OK, vformat("Fixture input failed to format: %s", input_path));
			CHECK_MESSAGE(result.formatted == expected, vformat("Fixture output mismatch: %s", input_path));
		}
	}

	TEST_CASE("[Format] Idempotent over the script corpus") {
		const String root = "modules/foundry_script/tests/scripts";
		int checked = 0;
		for (const String &script : collect_gd_scripts(root)) {
			if (is_narrow_skipped_fixture(script)) {
				continue;
			}
			Error read_error = OK;
			const String source = FileAccess::get_file_as_string(script, &read_error);
			if (read_error != OK) {
				continue;
			}
			FSFormatter formatter;
			FSFormatter::Result first;
			if (formatter.format(source, script, first) != OK) {
				continue; // Unparsable scripts are covered by the refuse-on-error test.
			}
			FSFormatter::Result second;
			REQUIRE_MESSAGE(formatter.format(first.formatted, script, second) == OK,
					vformat("Formatted output failed to re-parse: %s", script));
			CHECK_MESSAGE(first.formatted == second.formatted, vformat("Formatter is not idempotent for: %s", script));
			checked++;
		}
		MESSAGE("Idempotency: checked ", checked, " parseable corpus scripts.");
		CHECK(checked > 0);
	}

	TEST_CASE("[Format] Refuses to format unparsable error fixtures") {
		// `analyzer/errors` and `runtime/errors` hold semantic/runtime failures that
		// still parse cleanly; `parser/errors` holds the genuine parse failures that
		// exercise the refusal path. All three are swept: any script that fails to
		// parse must be refused, and `parser/errors` guarantees the path is covered.
		const char *error_roots[] = {
			"modules/foundry_script/tests/scripts/analyzer/errors",
			"modules/foundry_script/tests/scripts/runtime/errors",
			"modules/foundry_script/tests/scripts/parser/errors",
		};
		int refused = 0;
		for (const char *error_root : error_roots) {
			for (const String &script : collect_gd_scripts(error_root)) {
				Error read_error = OK;
				const String source = FileAccess::get_file_as_string(script, &read_error);
				if (read_error != OK) {
					continue;
				}
				FSParser parser;
				if (parse_no_errors(parser, source, script)) {
					continue; // A semantic/runtime error, not a parse error: it formats fine.
				}
				FSFormatter formatter;
				FSFormatter::Result result;
				CHECK_MESSAGE(formatter.format(source, script, result) != OK,
						vformat("Expected refusal for unparsable fixture: %s", script));
				CHECK_MESSAGE(result.formatted.is_empty(), vformat("Refused fixture must leave no output: %s", script));
				refused++;
			}
		}
		CHECK(refused > 0);
	}

	TEST_CASE("[Format] Preserves the parsed tree across the corpus") {
		const String root = "modules/foundry_script/tests/scripts";
		int checked = 0;
		for (const String &script : collect_gd_scripts(root)) {
			if (is_narrow_skipped_fixture(script)) {
				continue;
			}
			Error read_error = OK;
			const String source = FileAccess::get_file_as_string(script, &read_error);
			if (read_error != OK) {
				continue;
			}
			FSFormatter formatter;
			FSFormatter::Result result;
			if (formatter.format(source, script, result) != OK) {
				continue;
			}
			CHECK_MESSAGE(trees_equivalent(source, result.formatted, script),
					vformat("Formatting changed the parsed tree for: %s", script));
			checked++;
		}
		MESSAGE("Tree preservation: checked ", checked, " parseable corpus scripts.");
		CHECK(checked > 0);
	}

	TEST_CASE("[Format] Preserves all comments") {
		// A global invariant: formatting must never drop or duplicate a comment. The
		// parse-tree sweep cannot see comments (they are trivia, not AST), so this
		// count-equality check is what guarantees every comment survives everywhere.
		const String root = "modules/foundry_script/tests/scripts";
		int checked = 0;
		for (const String &script : collect_gd_scripts(root)) {
			if (is_narrow_skipped_fixture(script)) {
				continue;
			}
			Error read_error = OK;
			const String source = FileAccess::get_file_as_string(script, &read_error);
			if (read_error != OK) {
				continue;
			}
			FSFormatter formatter;
			FSFormatter::Result result;
			if (formatter.format(source, script, result) != OK) {
				continue;
			}
			CHECK_MESSAGE(count_comments(source) == count_comments(result.formatted),
					vformat("Comment count changed (%d -> %d) for: %s",
							count_comments(source), count_comments(result.formatted), script));
			checked++;
		}
		MESSAGE("Comment preservation: checked ", checked, " parseable corpus scripts.");
		CHECK(checked > 0);
	}

	TEST_CASE("[Format] Preserves class-body string comments") {
		// String literals used as comments in a class/top-level body are consumed by
		// the parser without an AST node, so they are invisible to the tree, comment-
		// count, and (since consistently dropped) idempotency sweeps. This count guard
		// catches dropping or duplicating any of them.
		const String root = "modules/foundry_script/tests/scripts";
		int checked = 0;
		for (const String &script : collect_gd_scripts(root)) {
			if (is_narrow_skipped_fixture(script)) {
				continue;
			}
			Error read_error = OK;
			const String source = FileAccess::get_file_as_string(script, &read_error);
			if (read_error != OK) {
				continue;
			}
			FSFormatter formatter;
			FSFormatter::Result result;
			if (formatter.format(source, script, result) != OK) {
				continue;
			}
			const int source_count = count_class_body_string_comments(source);
			const int formatted_count = count_class_body_string_comments(result.formatted);
			CHECK_MESSAGE(source_count == formatted_count,
					vformat("String-comment count changed (%d -> %d) for: %s", source_count, formatted_count, script));
			checked++;
		}
		MESSAGE("String-comment preservation: checked ", checked, " parseable corpus scripts.");
		CHECK(checked > 0);
	}

	TEST_CASE("[Format] Language bridge reformats through the canonical core") {
		// The editor and LSP front-ends reach the formatter through the
		// ScriptLanguage::format_code bridge; it must produce exactly what driving
		// the core formatter directly produces (single source of truth).
		const String source = "func f():\n\treturn  1\n";
		String formatted;
		String error_message;
		const bool ok = FSLanguage::get_singleton()->format_code(source, "test.fs", formatted, &error_message);
		CHECK(ok);
		CHECK(error_message.is_empty());
		CHECK_EQ(formatted, format_or_fail(source));
	}

	TEST_CASE("[Format] Language bridge refuses to format on parse error") {
		const String source = "func f(:\n";
		String formatted = "untouched";
		String error_message;
		const bool ok = FSLanguage::get_singleton()->format_code(source, "test.fs", formatted, &error_message);
		CHECK_FALSE(ok);
		// The output reference is left untouched and the diagnostic is surfaced.
		CHECK_EQ(formatted, "untouched");
		CHECK_FALSE(error_message.is_empty());
	}
}

} // namespace FSTests

#endif // TOOLS_ENABLED
