/**************************************************************************/
/*  gdscript_format.cpp                                                   */
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

#include "gdscript_format.h"

#ifdef TOOLS_ENABLED

#include "core/error/error_macros.h"
#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/os/os.h"
#include "core/templates/local_vector.h"

#include <stdio.h>

static String binary_operator_text(GDScriptParser::BinaryOpNode::OpType p_operation) {
	switch (p_operation) {
		case GDScriptParser::BinaryOpNode::OP_ADDITION:
			return "+";
		case GDScriptParser::BinaryOpNode::OP_SUBTRACTION:
			return "-";
		case GDScriptParser::BinaryOpNode::OP_MULTIPLICATION:
			return "*";
		case GDScriptParser::BinaryOpNode::OP_DIVISION:
			return "/";
		case GDScriptParser::BinaryOpNode::OP_MODULO:
			return "%";
		case GDScriptParser::BinaryOpNode::OP_POWER:
			return "**";
		case GDScriptParser::BinaryOpNode::OP_BIT_LEFT_SHIFT:
			return "<<";
		case GDScriptParser::BinaryOpNode::OP_BIT_RIGHT_SHIFT:
			return ">>";
		case GDScriptParser::BinaryOpNode::OP_BIT_AND:
			return "&";
		case GDScriptParser::BinaryOpNode::OP_BIT_OR:
			return "|";
		case GDScriptParser::BinaryOpNode::OP_BIT_XOR:
			return "^";
		case GDScriptParser::BinaryOpNode::OP_LOGIC_AND:
			return "and";
		case GDScriptParser::BinaryOpNode::OP_LOGIC_OR:
			return "or";
		case GDScriptParser::BinaryOpNode::OP_CONTENT_TEST:
			return "in";
		case GDScriptParser::BinaryOpNode::OP_COMP_EQUAL:
			return "==";
		case GDScriptParser::BinaryOpNode::OP_COMP_NOT_EQUAL:
			return "!=";
		case GDScriptParser::BinaryOpNode::OP_COMP_LESS:
			return "<";
		case GDScriptParser::BinaryOpNode::OP_COMP_LESS_EQUAL:
			return "<=";
		case GDScriptParser::BinaryOpNode::OP_COMP_GREATER:
			return ">";
		case GDScriptParser::BinaryOpNode::OP_COMP_GREATER_EQUAL:
			return ">=";
	}
	return "?";
}

static String assignment_operator_text(GDScriptParser::AssignmentNode::Operation p_operation) {
	switch (p_operation) {
		case GDScriptParser::AssignmentNode::OP_NONE:
			return "=";
		case GDScriptParser::AssignmentNode::OP_ADDITION:
			return "+=";
		case GDScriptParser::AssignmentNode::OP_SUBTRACTION:
			return "-=";
		case GDScriptParser::AssignmentNode::OP_MULTIPLICATION:
			return "*=";
		case GDScriptParser::AssignmentNode::OP_DIVISION:
			return "/=";
		case GDScriptParser::AssignmentNode::OP_MODULO:
			return "%=";
		case GDScriptParser::AssignmentNode::OP_POWER:
			return "**=";
		case GDScriptParser::AssignmentNode::OP_BIT_SHIFT_LEFT:
			return "<<=";
		case GDScriptParser::AssignmentNode::OP_BIT_SHIFT_RIGHT:
			return ">>=";
		case GDScriptParser::AssignmentNode::OP_BIT_AND:
			return "&=";
		case GDScriptParser::AssignmentNode::OP_BIT_OR:
			return "|=";
		case GDScriptParser::AssignmentNode::OP_BIT_XOR:
			return "^=";
	}
	return "=";
}

// Re-quotes a single `$`/`%` node-path segment when its name cannot be written
// bare. The parsed `full_path` drops the original quotes, so a name like
// `My Node` (from `$"My Node"`) must be re-wrapped or the output would tokenize
// as two identifiers and change the token stream.
// Bidirectional / isolate format controls the tokenizer refuses to accept raw in
// a string literal (`gdscript_tokenizer.cpp`: "Invisible text direction control
// character ..."). They are legal only when written as an escape, so the formatter
// must emit them as `\uXXXX` for the output to re-parse.
static bool is_disallowed_raw_string_control(char32_t p_character) {
	return p_character == 0x200E || p_character == 0x200F ||
			(p_character >= 0x202A && p_character <= 0x202E) ||
			(p_character >= 0x2066 && p_character <= 0x2069);
}

// Encodes a decoded String value as a canonical double-quoted GDScript string
// literal, escaping the inverse of every escape the tokenizer decodes (see
// `gdscript_tokenizer.cpp`): backslash, double quote, and the control escapes
// `\a \b \f \n \r \t \v`. Any remaining control character, and the bidi/isolate
// format characters the tokenizer rejects raw, are emitted as `\uXXXX`.
// Use this for fields the parser stores as already-decoded Strings (extends path,
// `@icon` path, quoted node-path segments); literals backed by a source token go
// through the token index instead.
static String quote_string_literal(const String &p_value) {
	String result = "\"";
	for (int i = 0; i < p_value.length(); i++) {
		const char32_t character = p_value[i];
		switch (character) {
			case '\\':
				result += "\\\\";
				break;
			case '"':
				result += "\\\"";
				break;
			case '\a':
				result += "\\a";
				break;
			case '\b':
				result += "\\b";
				break;
			case '\f':
				result += "\\f";
				break;
			case '\n':
				result += "\\n";
				break;
			case '\r':
				result += "\\r";
				break;
			case '\t':
				result += "\\t";
				break;
			case '\v':
				result += "\\v";
				break;
			default:
				if (character < 0x20 || is_disallowed_raw_string_control(character)) {
					result += "\\u" + String::num_uint64(character, 16).lpad(4, "0");
				} else {
					result += String::chr(character);
				}
				break;
		}
	}
	result += "\"";
	return result;
}

static String node_path_segment_text(const String &p_segment) {
	if (p_segment.is_empty()) {
		return p_segment; // Empty parts come from leading/internal slashes.
	}
	String prefix;
	String name = p_segment;
	if (name[0] == '%') {
		prefix = "%"; // Unique-name marker keeps its position before the quote.
		name = name.substr(1);
	}
	if (name.is_empty() || !name.is_valid_unicode_identifier()) {
		return prefix + quote_string_literal(name);
	}
	return prefix + name;
}

static String unary_operator_text(GDScriptParser::UnaryOpNode::OpType p_operation) {
	switch (p_operation) {
		case GDScriptParser::UnaryOpNode::OP_POSITIVE:
			return "+";
		case GDScriptParser::UnaryOpNode::OP_NEGATIVE:
			return "-";
		case GDScriptParser::UnaryOpNode::OP_COMPLEMENT:
			return "~";
		case GDScriptParser::UnaryOpNode::OP_LOGIC_NOT:
			return "not";
	}
	return "?";
}

// Mirrors `GDScriptParser::Precedence` (gdscript_parser.h) one-for-one. The
// parser's enum is private, so the printer keeps its own copy in the same order:
// higher value binds tighter. The reconstructed parentheses depend on this
// matching the parser exactly, so the two must be kept in sync.
//
// The postfix tiers (FPREC_CALL / FPREC_ATTRIBUTE / FPREC_SUBSCRIPT) exist only
// to preserve the parser's ordering; the printer never emits an operator at
// those levels, it only compares against them when deciding whether a base needs
// parentheses.
enum FormatPrecedence {
	FPREC_NONE,
	FPREC_ASSIGNMENT,
	FPREC_CAST,
	FPREC_TERNARY,
	FPREC_LOGIC_OR,
	FPREC_LOGIC_AND,
	FPREC_LOGIC_NOT,
	FPREC_CONTENT_TEST,
	FPREC_COMPARISON,
	FPREC_BIT_OR,
	FPREC_BIT_XOR,
	FPREC_BIT_AND,
	FPREC_BIT_SHIFT,
	FPREC_ADDITION_SUBTRACTION,
	FPREC_FACTOR,
	FPREC_SIGN,
	FPREC_BIT_NOT,
	FPREC_POWER,
	FPREC_TYPE_TEST,
	FPREC_AWAIT,
	FPREC_CALL,
	FPREC_ATTRIBUTE,
	FPREC_SUBSCRIPT,
	FPREC_PRIMARY,
};

static int binary_operator_precedence(GDScriptParser::BinaryOpNode::OpType p_operation) {
	switch (p_operation) {
		case GDScriptParser::BinaryOpNode::OP_ADDITION:
		case GDScriptParser::BinaryOpNode::OP_SUBTRACTION:
			return FPREC_ADDITION_SUBTRACTION;
		case GDScriptParser::BinaryOpNode::OP_MULTIPLICATION:
		case GDScriptParser::BinaryOpNode::OP_DIVISION:
		case GDScriptParser::BinaryOpNode::OP_MODULO:
			return FPREC_FACTOR;
		case GDScriptParser::BinaryOpNode::OP_POWER:
			return FPREC_POWER;
		case GDScriptParser::BinaryOpNode::OP_BIT_LEFT_SHIFT:
		case GDScriptParser::BinaryOpNode::OP_BIT_RIGHT_SHIFT:
			return FPREC_BIT_SHIFT;
		case GDScriptParser::BinaryOpNode::OP_BIT_AND:
			return FPREC_BIT_AND;
		case GDScriptParser::BinaryOpNode::OP_BIT_OR:
			return FPREC_BIT_OR;
		case GDScriptParser::BinaryOpNode::OP_BIT_XOR:
			return FPREC_BIT_XOR;
		case GDScriptParser::BinaryOpNode::OP_LOGIC_AND:
			return FPREC_LOGIC_AND;
		case GDScriptParser::BinaryOpNode::OP_LOGIC_OR:
			return FPREC_LOGIC_OR;
		case GDScriptParser::BinaryOpNode::OP_CONTENT_TEST:
			return FPREC_CONTENT_TEST;
		case GDScriptParser::BinaryOpNode::OP_COMP_EQUAL:
		case GDScriptParser::BinaryOpNode::OP_COMP_NOT_EQUAL:
		case GDScriptParser::BinaryOpNode::OP_COMP_LESS:
		case GDScriptParser::BinaryOpNode::OP_COMP_LESS_EQUAL:
		case GDScriptParser::BinaryOpNode::OP_COMP_GREATER:
		case GDScriptParser::BinaryOpNode::OP_COMP_GREATER_EQUAL:
			return FPREC_COMPARISON;
	}
	return FPREC_PRIMARY;
}

static int unary_operator_precedence(GDScriptParser::UnaryOpNode::OpType p_operation) {
	switch (p_operation) {
		case GDScriptParser::UnaryOpNode::OP_POSITIVE:
		case GDScriptParser::UnaryOpNode::OP_NEGATIVE:
			return FPREC_SIGN;
		case GDScriptParser::UnaryOpNode::OP_COMPLEMENT:
			return FPREC_BIT_NOT;
		case GDScriptParser::UnaryOpNode::OP_LOGIC_NOT:
			return FPREC_LOGIC_NOT;
	}
	return FPREC_PRIMARY;
}

// Precedence of an expression node as seen by its parent. Atoms (literals,
// identifiers, calls, subscripts, collections, ...) never need wrapping, so they
// report the maximum precedence.
static int expression_precedence(const GDScriptParser::ExpressionNode *p_expression) {
	switch (p_expression->type) {
		case GDScriptParser::Node::ASSIGNMENT:
			return FPREC_ASSIGNMENT;
		case GDScriptParser::Node::CAST:
			return FPREC_CAST;
		case GDScriptParser::Node::TERNARY_OPERATOR:
			return FPREC_TERNARY;
		case GDScriptParser::Node::BINARY_OPERATOR:
			return binary_operator_precedence(static_cast<const GDScriptParser::BinaryOpNode *>(p_expression)->operation);
		case GDScriptParser::Node::UNARY_OPERATOR:
			return unary_operator_precedence(static_cast<const GDScriptParser::UnaryOpNode *>(p_expression)->operation);
		case GDScriptParser::Node::TYPE_TEST:
			return FPREC_TYPE_TEST;
		case GDScriptParser::Node::AWAIT:
			return FPREC_AWAIT;
		case GDScriptParser::Node::LAMBDA:
			// A lambda body greedily extends to the end of the line, so anything
			// that follows it (a postfix `.method()`, an operator) must be inside
			// parentheses. Rank it lowest so every operand context wraps it.
			return FPREC_NONE;
		default:
			return FPREC_PRIMARY;
	}
}

// Re-quotes a string literal to canonical double quotes. Only the simple,
// single-line `'...'` form is rewritten, and only when its content has no
// unescaped `"` that double quotes would break. Triple-quoted (`'''...'''`) and
// raw (`r'...'`) strings keep their delimiters; escapes and content are never
// altered. A leading `&`/`^` (StringName / NodePath) prefix is preserved.
static String canonicalize_string_literal(const String &p_source) {
	int start = 0;
	while (start < p_source.length() && p_source[start] != '\'' && p_source[start] != '"') {
		const char32_t prefix_char = p_source[start];
		if (prefix_char == 'r' || prefix_char == 'R') {
			return p_source; // Raw string: leave the delimiter untouched.
		}
		start++;
	}
	if (start >= p_source.length() || p_source[start] != '\'') {
		return p_source; // Already double-quoted, or not a string at all.
	}
	if (start + 2 < p_source.length() && p_source[start + 1] == '\'' && p_source[start + 2] == '\'') {
		return p_source; // Triple-quoted single string.
	}
	if (p_source[p_source.length() - 1] != '\'') {
		return p_source; // Defensive: not a balanced single-quoted literal.
	}
	const String prefix = p_source.substr(0, start);
	const String inner = p_source.substr(start + 1, p_source.length() - start - 2);
	if (inner.find_char('"') != -1) {
		return p_source; // A double quote inside would have to be escaped; keep single.
	}
	return prefix + "\"" + inner + "\"";
}

// Canonicalizes numeric literal casing: lowercase the `0x`/`0b` base prefix and
// the float exponent marker, uppercase hexadecimal digits. Underscores and the
// rest of the digit text are preserved verbatim.
static String canonicalize_number_literal(const String &p_source) {
	if (p_source.length() >= 2 && p_source[0] == '0' && (p_source[1] == 'x' || p_source[1] == 'X')) {
		return "0x" + p_source.substr(2).to_upper();
	}
	if (p_source.length() >= 2 && p_source[0] == '0' && (p_source[1] == 'b' || p_source[1] == 'B')) {
		return "0b" + p_source.substr(2);
	}
	return p_source.replace("E", "e");
}

// True when the author wrote a collection node (array / dictionary / call) across
// several lines. The formatter respects that layout without introducing new
// wrapping of its own, so this drives "keep the author's multi-line shape",
// not "this currently overflows the line width".
static bool node_was_authored_multiline(const GDScriptParser::Node *p_node) {
	return p_node->end_line > p_node->start_line;
}

// A compound statement whose body is its own suite (`if`/`for`/`while`/`match`).
// Their bodies emit any trailing inline comment on the body's last line
// internally, so the enclosing suite must not also try to attach one (which would
// double-emit it).
static bool is_block_statement(GDScriptParser::Node::Type p_type) {
	return p_type == GDScriptParser::Node::IF || p_type == GDScriptParser::Node::FOR ||
			p_type == GDScriptParser::Node::WHILE || p_type == GDScriptParser::Node::MATCH;
}

// True when `p_line` falls inside a non-class member of `p_class` (or, recursively,
// inside a non-class member of a nested class). Used to tell a class-body string
// comment (not inside any member -> recover it) from a genuine string node inside
// a function/variable/etc. (an AST node already emitted by the normal walk).
static bool class_line_is_inside_member(const GDScriptParser::ClassNode *p_class, int p_line) {
	if (p_class == nullptr) {
		return false;
	}
	for (int i = 0; i < p_class->members.size(); i++) {
		const GDScriptParser::ClassNode::Member &member = p_class->members[i];
		const GDScriptParser::Node *node = member.get_source_node();
		if (node == nullptr || node->start_line <= 0) {
			continue;
		}
		// A member's annotations sit above its node, and multi-line annotation
		// arguments (`@export_enum(\n"A",\n)`) put strings on their own lines before
		// the node's start_line; extend the range to cover them so those argument
		// strings are not mistaken for class-body string comments.
		int member_start = node->start_line;
		for (const GDScriptParser::AnnotationNode *annotation : node->annotations) {
			if (annotation->start_line > 0 && annotation->start_line < member_start) {
				member_start = annotation->start_line;
			}
		}
		if (p_line < member_start || p_line > node->end_line) {
			continue;
		}
		if (member.type == GDScriptParser::ClassNode::Member::CLASS) {
			return class_line_is_inside_member(member.m_class, p_line);
		}
		return true; // Inside a function / variable / constant / signal / enum.
	}
	return false; // In a gap between members (or the header region): a string comment.
}

Error GDScriptFormatter::format(const String &p_source, const String &p_path, Result &r_result) {
	// Pass 1: tokenize to capture comments and original literal source text.
	GDScriptTokenizerText tokenizer;
	tokenizer.set_source_code(p_source);

	HashMap<uint64_t, GDScriptPrinter::LiteralToken> literals;
	// Standalone region annotations (`@warning_ignore_start`/`@warning_ignore_restore`)
	// are consumed and discarded by the parser, so they never reach the tree. Recover
	// them here, keyed by source line, and reattach them through the trivia walker.
	HashMap<int, GDScriptPrinter::StandaloneAnnotation> standalone_annotations;
	// Source lines of head-header keywords the AST does not retain (see HeaderLines).
	// `namespace`/`import`/`@tool`/`@icon`/`@static_unload` are head-only, so their
	// first occurrence is the head's; `extends` may also appear on inner classes, so
	// only its first occurrence is recorded (the head's, when it has one).
	GDScriptPrinter::HeaderLines header_lines;
	GDScriptTokenizer::Token token = tokenizer.scan();
	while (token.type != GDScriptTokenizer::Token::TK_EOF) {
		if (token.type == GDScriptTokenizer::Token::ERROR) {
			break; // The parse pass below produces the authoritative diagnostic.
		}
		// Built-in numeric constants (`PI`/`TAU`/`INF`/`NaN`) parse into a LiteralNode
		// from their own CONST_* tokens, not a LITERAL token, so index them too or the
		// printer would Variant-stringify the float value (`3.14159...`), changing the
		// syntax (and possibly precision) of an otherwise value-equal literal.
		const bool is_const_keyword_token = token.type == GDScriptTokenizer::Token::CONST_PI ||
				token.type == GDScriptTokenizer::Token::CONST_TAU ||
				token.type == GDScriptTokenizer::Token::CONST_INF ||
				token.type == GDScriptTokenizer::Token::CONST_NAN;
		if (token.type == GDScriptTokenizer::Token::LITERAL || is_const_keyword_token) {
			const uint64_t key = (uint64_t(uint32_t(token.start_line)) << 32) | uint32_t(token.start_column);
			literals[key] = GDScriptPrinter::LiteralToken{ token.source };
		}
		if (token.type == GDScriptTokenizer::Token::NAMESPACE && header_lines.name_space == 0) {
			header_lines.name_space = token.start_line;
		} else if (token.type == GDScriptTokenizer::Token::IMPORT) {
			header_lines.imports.push_back(token.start_line);
		} else if (token.type == GDScriptTokenizer::Token::EXTENDS && header_lines.extends == 0) {
			header_lines.extends = token.start_line;
		} else if (token.type == GDScriptTokenizer::Token::ANNOTATION) {
			if (token.source == "@tool" && header_lines.tool == 0) {
				header_lines.tool = token.start_line;
			} else if (token.source == "@icon" && header_lines.icon == 0) {
				header_lines.icon = token.start_line;
			} else if (token.source == "@static_unload" && header_lines.static_unload == 0) {
				header_lines.static_unload = token.start_line;
			}
		}
		if (token.type == GDScriptTokenizer::Token::ANNOTATION &&
				(token.source == "@warning_ignore_start" || token.source == "@warning_ignore_restore")) {
			GDScriptPrinter::StandaloneAnnotation entry;
			entry.text = token.source;
			entry.end_line = token.end_line;
			const int start_line = token.start_line;
			GDScriptTokenizer::Token next = tokenizer.scan();
			if (next.type == GDScriptTokenizer::Token::PARENTHESIS_OPEN) {
				entry.text += "(";
				int depth = 1;
				GDScriptTokenizer::Token argument = tokenizer.scan();
				while (depth > 0 && argument.type != GDScriptTokenizer::Token::TK_EOF &&
						argument.type != GDScriptTokenizer::Token::ERROR) {
					entry.end_line = argument.end_line;
					if (argument.type == GDScriptTokenizer::Token::PARENTHESIS_OPEN) {
						depth++;
						entry.text += "(";
					} else if (argument.type == GDScriptTokenizer::Token::PARENTHESIS_CLOSE) {
						depth--;
						if (depth == 0) {
							break;
						}
						entry.text += ")";
					} else if (argument.type == GDScriptTokenizer::Token::COMMA && depth == 1) {
						entry.text += ", ";
					} else if (argument.type == GDScriptTokenizer::Token::LITERAL) {
						entry.text += canonicalize_string_literal(argument.source);
					} else {
						entry.text += argument.source;
					}
					argument = tokenizer.scan();
				}
				entry.text += ")";
				standalone_annotations[start_line] = entry;
				token = tokenizer.scan(); // Resume after the closing parenthesis.
				continue;
			}
			standalone_annotations[start_line] = entry;
			token = next; // No argument list; process the look-ahead token next.
			continue;
		}
		token = tokenizer.scan();
	}
	const HashMap<int, GDScriptTokenizer::CommentData> comments = tokenizer.get_comments();

	// Pass 2: parse for the structural tree. The parser re-tokenizes internally;
	// that is intended and keeps the parser unmodified.
	GDScriptParser parser;
	const Error parse_error = parser.parse(p_source, p_path, false);
	if (parse_error != OK || !parser.get_errors().is_empty()) {
		r_result.formatted = String();
		if (!parser.get_errors().is_empty()) {
			const GDScriptParser::ParserError &first = parser.get_errors().front()->get();
			r_result.error_message = first.message;
			r_result.error_line = first.line;
			r_result.error_column = first.column;
		} else {
			r_result.error_message = "Failed to parse GDScript source.";
			r_result.error_line = 1;
			r_result.error_column = 1;
		}
		return ERR_PARSE_ERROR;
	}

	// A bare string literal at statement position in a class/top-level body is
	// consumed by the parser as a multi-line comment, producing no member, so it is
	// absent from the tree. Recover such "string comments": scan for standalone
	// string tokens (at a line start) and keep the ones that do not fall inside any
	// real member -- a string inside a function/variable/etc. is a genuine AST node
	// (a string statement or expression) and is emitted by the normal walk.
	HashMap<int, GDScriptPrinter::StringComment> string_comments;
	{
		GDScriptTokenizerText string_tokenizer;
		string_tokenizer.set_source_code(p_source);
		GDScriptTokenizer::Token::Type previous_type = GDScriptTokenizer::Token::NEWLINE;
		for (GDScriptTokenizer::Token string_token = string_tokenizer.scan();
				string_token.type != GDScriptTokenizer::Token::TK_EOF && string_token.type != GDScriptTokenizer::Token::ERROR;
				string_token = string_tokenizer.scan()) {
			const bool at_line_start = previous_type == GDScriptTokenizer::Token::NEWLINE ||
					previous_type == GDScriptTokenizer::Token::INDENT ||
					previous_type == GDScriptTokenizer::Token::DEDENT;
			if (at_line_start && string_token.type == GDScriptTokenizer::Token::LITERAL &&
					string_token.literal.get_type() == Variant::STRING &&
					!class_line_is_inside_member(parser.get_tree(), string_token.start_line)) {
				string_comments[string_token.start_line] = GDScriptPrinter::StringComment{ string_token.source, string_token.end_line };
			}
			previous_type = string_token.type;
		}
	}

	// Pass 3: print.
	const Vector<String> source_lines = p_source.split("\n");
	GDScriptPrinter printer(comments, literals, standalone_annotations, source_lines, header_lines, string_comments);
	r_result.formatted = printer.print_tree(parser.get_tree(), parser.is_tool());
	return OK;
}

GDScriptPrinter::GDScriptPrinter(const HashMap<int, GDScriptTokenizer::CommentData> &p_comments,
		const HashMap<uint64_t, LiteralToken> &p_literals,
		const HashMap<int, StandaloneAnnotation> &p_standalone_annotations,
		const Vector<String> &p_source_lines, const HeaderLines &p_header_lines,
		const HashMap<int, StringComment> &p_string_comments) :
		comments(p_comments), literals(p_literals), standalone_annotations(p_standalone_annotations), source_lines(p_source_lines), header_lines(p_header_lines), string_comments(p_string_comments) {
}

int GDScriptPrinter::line_indent_columns(int p_line) const {
	if (p_line < 1 || p_line > source_lines.size()) {
		return 0;
	}
	// Leading-whitespace width in columns, so the measure is comparable whatever the
	// source uses (tabs, spaces, or a mix): a tab advances to the next tab stop and a
	// space counts as one column. Used only for relative depth comparisons.
	const int tab_width = 4;
	const String &line = source_lines[p_line - 1];
	int columns = 0;
	for (int i = 0; i < line.length(); i++) {
		if (line[i] == '\t') {
			columns += tab_width - (columns % tab_width);
		} else if (line[i] == ' ') {
			columns += 1;
		} else {
			break;
		}
	}
	return columns;
}

void GDScriptPrinter::write_indent() {
	for (int i = 0; i < indent_level; i++) {
		output += "\t";
	}
}

void GDScriptPrinter::write(const String &p_text) {
	output += p_text;
}

void GDScriptPrinter::newline() {
	output += "\n";
}

String GDScriptPrinter::normalize_comment_text(const String &p_raw) const {
	// `p_raw` includes the leading `#`. Doc comments (`##`) and shebang-style
	// first lines (`#!`) are preserved verbatim apart from trailing whitespace;
	// every other comment gets exactly one space after the `#`.
	if (p_raw.begins_with("##") || p_raw.begins_with("#!")) {
		return p_raw.strip_edges(false, true);
	}
	const String body = p_raw.substr(1).strip_edges();
	if (body.is_empty()) {
		return "#";
	}
	return "# " + body;
}

bool GDScriptPrinter::is_full_line_comment(int p_line) const {
	HashMap<int, GDScriptTokenizer::CommentData>::ConstIterator found = comments.find(p_line);
	return found && found->value.new_line;
}

// Emits one full-line comment at the current indent and advances the cursor past
// its source line so it is never emitted twice.
void GDScriptPrinter::emit_comment_line(int p_line, const String &p_raw_comment) {
	write_indent();
	write(normalize_comment_text(p_raw_comment));
	newline();
	last_emitted_line = p_line;
}

bool GDScriptPrinter::is_trivia_line(int p_line) const {
	return is_full_line_comment(p_line) || standalone_annotations.has(p_line) || string_comments.has(p_line);
}

// Emits the trivia at `p_line` (a full-line comment, a recovered standalone
// annotation, or a recovered string comment) at the current indent and advances
// the cursor past it.
void GDScriptPrinter::emit_trivia_line(int p_line) {
	HashMap<int, StandaloneAnnotation>::ConstIterator annotation = standalone_annotations.find(p_line);
	if (annotation) {
		write_indent();
		write(annotation->value.text);
		newline();
		last_emitted_line = MAX(p_line, annotation->value.end_line);
		return;
	}
	HashMap<int, StringComment>::ConstIterator string_comment = string_comments.find(p_line);
	if (string_comment) {
		// The literal text may span several lines; the first line gets the current
		// indent and the rest is emitted verbatim (its content is part of the string).
		write_indent();
		write(string_comment->value.text);
		newline();
		last_emitted_line = MAX(p_line, string_comment->value.end_line);
		return;
	}
	emit_comment_line(p_line, comments.find(p_line)->value.comment);
}

// Emits the full-line comments and the normalized blank lines that sit between
// the last emitted source line and `p_next_line` (exclusive). `p_required_blanks`
// is the structural minimum to enforce before the first emitted piece (comment
// or the upcoming node); runs of blank lines otherwise collapse to a single one,
// and the very top of the file never gains leading blanks.
void GDScriptPrinter::emit_leading_trivia(int p_next_line, int p_required_blanks) {
	const bool at_file_start = last_emitted_line == 0;
	int line = last_emitted_line + 1;
	bool first_piece = true;
	bool done = false;
	while (!done) {
		int blank_run = 0;
		while (line < p_next_line && !is_trivia_line(line)) {
			blank_run++;
			line++;
		}
		const bool trivia_follows = line < p_next_line;

		int blanks;
		if (first_piece) {
			if (at_file_start) {
				blanks = 0;
			} else if (p_required_blanks > 0) {
				blanks = p_required_blanks;
			} else {
				blanks = blank_run > 0 ? 1 : 0;
			}
		} else {
			blanks = blank_run > 0 ? 1 : 0;
		}
		for (int i = 0; i < blanks; i++) {
			newline();
		}
		first_piece = false;

		if (trivia_follows) {
			emit_trivia_line(line);
			line = last_emitted_line + 1;
		} else {
			done = true;
		}
	}
}

// Appends an inline comment (` # ...`) to the just-emitted line. The caller emits
// the code line first; this rewrites its trailing newline so the comment trails
// the code with the canonical two-space gap.
void GDScriptPrinter::emit_trailing_comment(int p_line) {
	HashMap<int, GDScriptTokenizer::CommentData>::ConstIterator found = comments.find(p_line);
	if (!found || found->value.new_line) {
		return;
	}
	if (output.ends_with("\n")) {
		output = output.substr(0, output.length() - 1);
	}
	write("  ");
	write(normalize_comment_text(found->value.comment));
	newline();
	if (p_line > last_emitted_line) {
		last_emitted_line = p_line;
	}
}

// Emits the comments that trail the last statement of a block body, taken from
// the lines immediately following the last emitted line (a blank line or a
// non-trivia line ends the run). A trailing full-line comment belongs to this body
// only when its source indentation is at least the body's own (measured from the
// last emitted body statement, so it is correct whether the source is tab- or
// space-indented); a shallower comment (e.g. a column-0 doc comment before the next
// member) belongs to the following node and is left for its leading-trivia flush.
void GDScriptPrinter::flush_block_tail_comments() {
	const int body_columns = line_indent_columns(last_emitted_line);
	int line = last_emitted_line + 1;
	while (is_trivia_line(line) && line_indent_columns(line) >= body_columns) {
		emit_trivia_line(line);
		line = last_emitted_line + 1;
	}
}

void GDScriptPrinter::append_inline_comment(int p_line) {
	if (p_line <= last_emitted_line) {
		return; // Already consumed.
	}
	HashMap<int, GDScriptTokenizer::CommentData>::ConstIterator found = comments.find(p_line);
	if (!found || found->value.new_line) {
		return;
	}
	write("  ");
	write(normalize_comment_text(found->value.comment));
	last_emitted_line = p_line;
}

int GDScriptPrinter::find_else_line(int p_true_block_end, int p_else_block_start) const {
	// The gap between the true block and the else block's first statement holds only
	// the `else` line plus comments/blank lines, so the first non-comment, non-blank
	// line in it is the `else` keyword.
	for (int line = p_true_block_end + 1; line < p_else_block_start; line++) {
		if (is_full_line_comment(line)) {
			continue;
		}
		if (line >= 1 && line <= source_lines.size() && source_lines[line - 1].strip_edges().is_empty()) {
			continue;
		}
		return line;
	}
	return 0;
}

bool GDScriptPrinter::has_full_line_comment_between(int p_after, int p_before) const {
	if (p_after <= 0 || p_before <= 0) {
		return false;
	}
	for (int line = p_after + 1; line < p_before; line++) {
		if (is_full_line_comment(line)) {
			return true;
		}
	}
	return false;
}

void GDScriptPrinter::flush_trivia_until(int p_until_line) {
	int line = last_emitted_line + 1;
	while (line < p_until_line) {
		if (is_trivia_line(line)) {
			// Emits a full-line comment or a recovered standalone warning annotation,
			// advancing the cursor (a multi-line annotation past its end line).
			emit_trivia_line(line);
			line = last_emitted_line + 1;
		} else {
			line++;
		}
	}
}

void GDScriptPrinter::flush_inner_comments(int p_until_line) {
	for (int line = last_emitted_line + 1; line < p_until_line; line++) {
		if (!is_full_line_comment(line)) {
			continue;
		}
		newline();
		write_indent();
		write(normalize_comment_text(comments.find(line)->value.comment));
		last_emitted_line = line;
	}
}

void GDScriptPrinter::flush_tail_comments() {
	int max_line = 0;
	for (const KeyValue<int, GDScriptTokenizer::CommentData> &entry : comments) {
		if (entry.value.new_line && entry.key > max_line) {
			max_line = entry.key;
		}
	}
	for (const KeyValue<int, StandaloneAnnotation> &entry : standalone_annotations) {
		const int annotation_end = MAX(entry.key, entry.value.end_line);
		if (annotation_end > max_line) {
			max_line = annotation_end;
		}
	}
	for (const KeyValue<int, StringComment> &entry : string_comments) {
		const int string_end = MAX(entry.key, entry.value.end_line);
		if (string_end > max_line) {
			max_line = string_end;
		}
	}
	if (max_line > last_emitted_line) {
		emit_leading_trivia(max_line + 1, 0);
	}
}

const GDScriptParser::Node *GDScriptPrinter::member_node(const GDScriptParser::ClassNode::Member &p_member) {
	switch (p_member.type) {
		case GDScriptParser::ClassNode::Member::CLASS:
			return p_member.m_class;
		case GDScriptParser::ClassNode::Member::CONSTANT:
			return p_member.constant;
		case GDScriptParser::ClassNode::Member::FUNCTION:
			return p_member.function;
		case GDScriptParser::ClassNode::Member::SIGNAL:
			return p_member.signal;
		case GDScriptParser::ClassNode::Member::VARIABLE:
			return p_member.variable;
		case GDScriptParser::ClassNode::Member::ENUM:
			return p_member.m_enum;
		case GDScriptParser::ClassNode::Member::ENUM_VALUE:
			return p_member.enum_value.parent_enum;
		case GDScriptParser::ClassNode::Member::GROUP:
			return p_member.annotation;
		case GDScriptParser::ClassNode::Member::UNDEFINED:
			return nullptr;
	}
	return nullptr;
}

int GDScriptPrinter::member_start_line(const GDScriptParser::ClassNode::Member &p_member) {
	const GDScriptParser::Node *node = member_node(p_member);
	if (node == nullptr) {
		return 0;
	}
	int start = node->start_line;

	const List<GDScriptParser::AnnotationNode *> *annotations = nullptr;
	switch (p_member.type) {
		case GDScriptParser::ClassNode::Member::CLASS:
			annotations = &p_member.m_class->annotations;
			break;
		case GDScriptParser::ClassNode::Member::CONSTANT:
			annotations = &p_member.constant->annotations;
			break;
		case GDScriptParser::ClassNode::Member::FUNCTION:
			annotations = &p_member.function->annotations;
			break;
		case GDScriptParser::ClassNode::Member::SIGNAL:
			annotations = &p_member.signal->annotations;
			break;
		case GDScriptParser::ClassNode::Member::VARIABLE:
			annotations = &p_member.variable->annotations;
			break;
		case GDScriptParser::ClassNode::Member::ENUM:
			annotations = &p_member.m_enum->annotations;
			break;
		case GDScriptParser::ClassNode::Member::GROUP:
			annotations = &p_member.annotation->annotations;
			break;
		default:
			break;
	}
	if (annotations != nullptr) {
		for (const GDScriptParser::AnnotationNode *annotation : *annotations) {
			if (annotation->start_line > 0 && annotation->start_line < start) {
				start = annotation->start_line;
			}
		}
	}
	return start;
}

bool GDScriptPrinter::member_is_definition(const GDScriptParser::ClassNode::Member &p_member) {
	return p_member.type == GDScriptParser::ClassNode::Member::FUNCTION ||
			p_member.type == GDScriptParser::ClassNode::Member::CLASS;
}

void GDScriptPrinter::header_line_range(const GDScriptParser::ClassNode *p_class, int &r_min_line, int &r_max_line) {
	r_min_line = 0;
	r_max_line = 0;
	const auto consider = [&](int p_line) {
		if (p_line <= 0) {
			return;
		}
		if (r_min_line == 0 || p_line < r_min_line) {
			r_min_line = p_line;
		}
		if (p_line > r_max_line) {
			r_max_line = p_line;
		}
	};
	for (const GDScriptParser::AnnotationNode *annotation : p_class->annotations) {
		consider(annotation->start_line);
	}
	if (p_class->identifier != nullptr) {
		consider(p_class->identifier->start_line);
	}
	for (int i = 0; i < p_class->extends.size(); i++) {
		consider(p_class->extends[i]->start_line);
	}
}

String GDScriptPrinter::print_tree(const GDScriptParser::ClassNode *p_root, bool p_is_tool) {
	ERR_FAIL_NULL_V(p_root, String());
	print_class(p_root, true, p_is_tool);
	// Guarantee exactly one trailing newline.
	while (output.ends_with("\n\n")) {
		output = output.substr(0, output.length() - 1);
	}
	if (!output.ends_with("\n")) {
		output += "\n";
	}
	return output;
}

void GDScriptPrinter::print_extends_clause(const GDScriptParser::ClassNode *p_class) {
	bool first = true;
	if (!p_class->extends_path.is_empty()) {
		write(quote_string_literal(p_class->extends_path));
		first = false;
	}
	for (int i = 0; i < p_class->extends.size(); i++) {
		if (!first) {
			write(".");
		}
		write(p_class->extends[i]->name);
		first = false;
	}
	if (!p_class->extends_type_arguments.is_empty()) {
		write("[");
		for (int i = 0; i < p_class->extends_type_arguments.size(); i++) {
			if (i > 0) {
				write(", ");
			}
			print_type(p_class->extends_type_arguments[i]);
		}
		write("]");
	}
}

void GDScriptPrinter::print_class(const GDScriptParser::ClassNode *p_class, bool p_is_root, bool p_is_tool) {
	if (p_is_root) {
		int header_min_line = 0;
		int header_max_line = 0;
		header_line_range(p_class, header_min_line, header_max_line);
		// Fold in the header keyword lines the AST does not retain, so the leading
		// comment flush above the header starts at the true first header line.
		// `header_lines.extends` records the first `extends` in the file, which is an
		// inner class's when the head has none, so only trust it when the head extends.
		const int header_keyword_lines[] = { header_lines.tool, header_lines.icon,
			header_lines.static_unload, header_lines.name_space,
			p_class->extends_used ? header_lines.extends : 0 };
		for (const int keyword_line : header_keyword_lines) {
			if (keyword_line > 0 && (header_min_line == 0 || keyword_line < header_min_line)) {
				header_min_line = keyword_line;
			}
		}
		for (const int import_line : header_lines.imports) {
			if (import_line > 0 && (header_min_line == 0 || import_line < header_min_line)) {
				header_min_line = import_line;
			}
		}
		if (header_min_line > 0) {
			emit_leading_trivia(header_min_line, 0);
		}
		print_class_header(p_class, p_is_tool);
		if (header_max_line > last_emitted_line) {
			last_emitted_line = header_max_line;
		}
		for (const GDScriptParser::AnnotationDeclarationNode *declaration : p_class->annotation_declarations) {
			if (declaration->start_line > 0) {
				emit_leading_trivia(declaration->start_line, 0);
			}
			print_annotation_declaration(declaration);
			if (declaration->end_line > last_emitted_line) {
				last_emitted_line = declaration->end_line;
			}
			// An inline comment on the declaration's own line.
			emit_trailing_comment(declaration->end_line);
		}
		print_class_body(p_class, true);
		return;
	}

	write_indent();
	if (p_class->is_abstract) {
		write("abstract ");
	} else if (p_class->is_final) {
		write("final ");
	}
	write(p_class->is_trait ? "trait " : "class ");
	if (p_class->identifier != nullptr) {
		write(p_class->identifier->name);
	}
	print_type_parameters(p_class->type_parameters);
	if (p_class->extends_used) {
		write(" extends ");
		print_extends_clause(p_class);
	}
	for (int i = 0; i < p_class->used_traits.size(); i++) {
		write(i == 0 ? " uses " : ", ");
		print_trait_use(p_class->used_traits[i]);
	}
	write(":");
	newline();
	last_emitted_line = p_class->start_line;
	emit_trailing_comment(p_class->start_line);
	indent_level++;
	if (p_class->members.is_empty()) {
		// An inner class with an empty body (only `pass`) still needs a body line.
		write_indent();
		write("pass");
		newline();
	} else {
		print_class_body(p_class, false);
	}
	indent_level--;
}

void GDScriptPrinter::print_class_header(const GDScriptParser::ClassNode *p_class, bool p_is_tool) {
	// Emit each header sub-line in grammar (= source) order, flushing full-line
	// comments above it and keeping an inline comment on it, so comments
	// interleaved through the header keep their place. Header lines the AST does not
	// retain come from the tokenize-pass `header_lines`.
	//
	// The grammar pins annotation placement around `namespace`/`import`: script/
	// file-level annotations come before them, class-level (and custom) annotations
	// after (`Class annotations must appear after "namespace" and "import"
	// declarations.`). `@tool`/`@icon`/`@static_unload` are script-level flags
	// recovered from the class, not the annotation list, and always lead.
	// Terminates the header sub-line just written: the caller flushes the full-line
	// comments above it first, then this ends the line and attaches any inline
	// comment on it.
	const auto finish_header_line = [&](int p_line) {
		newline();
		if (p_line > last_emitted_line) {
			last_emitted_line = p_line;
		}
		emit_trailing_comment(p_line);
	};

	if (p_is_tool) {
		if (header_lines.tool > 0) {
			flush_trivia_until(header_lines.tool);
		}
		write("@tool");
		finish_header_line(header_lines.tool);
	}
	if (!p_class->icon_path.is_empty()) {
		if (header_lines.icon > 0) {
			flush_trivia_until(header_lines.icon);
		}
		write("@icon(" + quote_string_literal(p_class->icon_path) + ")");
		finish_header_line(header_lines.icon);
	}
	if (p_class->annotated_static_unload) {
		if (header_lines.static_unload > 0) {
			flush_trivia_until(header_lines.static_unload);
		}
		write("@static_unload");
		finish_header_line(header_lines.static_unload);
	}

	if (!p_class->namespace_name.is_empty()) {
		if (header_lines.name_space > 0) {
			flush_trivia_until(header_lines.name_space);
		}
		write("namespace ");
		write(p_class->namespace_name);
		finish_header_line(header_lines.name_space);
	}
	for (int i = 0; i < p_class->imports.size(); i++) {
		const int import_line = i < header_lines.imports.size() ? header_lines.imports[i] : 0;
		if (import_line > 0) {
			flush_trivia_until(import_line);
		}
		write("import ");
		write(p_class->imports[i]);
		finish_header_line(import_line);
	}

	// The only SCRIPT-target annotations are `@tool`/`@icon`/`@static_unload`, which
	// the parser consumes into the flags emitted above and never stores in the list.
	// So every entry in `annotations` is a class-level (or custom) annotation, which
	// the grammar requires to appear *after* `namespace`/`import`.
	const int class_name_line = p_class->identifier != nullptr ? p_class->identifier->start_line : 0;
	print_annotations(p_class->annotations, class_name_line);

	String modifier;
	if (p_class->is_abstract) {
		modifier = "abstract ";
	} else if (p_class->is_final) {
		modifier = "final ";
	}

	bool modifier_consumed = false;
	if (p_class->identifier != nullptr) {
		write(modifier);
		modifier_consumed = true;
		write(p_class->trait_name_used ? "trait_name " : "class_name ");
		write(p_class->identifier->name);
		print_type_parameters(p_class->type_parameters);
		finish_header_line(class_name_line);
	}

	if (p_class->extends_used) {
		const int extends_line = !p_class->extends.is_empty() && p_class->extends[0] != nullptr
				? p_class->extends[0]->start_line
				: header_lines.extends;
		if (extends_line > 0) {
			flush_trivia_until(extends_line);
		}
		if (!modifier_consumed) {
			write(modifier);
			modifier_consumed = true;
		}
		write("extends ");
		print_extends_clause(p_class);
		finish_header_line(extends_line);
	}

	// All used traits share a single `uses` statement; a second `uses` line is a
	// parse error ("Cannot use \"uses\" more than once in the same class.").
	if (!p_class->used_traits.is_empty()) {
		int uses_line = 0;
		if (!p_class->used_traits[0].name.is_empty() && p_class->used_traits[0].name[0] != nullptr) {
			uses_line = p_class->used_traits[0].name[0]->start_line;
		}
		if (uses_line > 0) {
			flush_trivia_until(uses_line);
		}
		for (int i = 0; i < p_class->used_traits.size(); i++) {
			write(i == 0 ? "uses " : ", ");
			print_trait_use(p_class->used_traits[i]);
		}
		finish_header_line(uses_line);
	}
}

void GDScriptPrinter::print_trait_use(const GDScriptParser::ClassNode::TraitUse &p_use) {
	write(p_use.to_string());
	if (!p_use.type_arguments.is_empty()) {
		write("[");
		for (int j = 0; j < p_use.type_arguments.size(); j++) {
			if (j > 0) {
				write(", ");
			}
			print_type(p_use.type_arguments[j]);
		}
		write("]");
	}
}

void GDScriptPrinter::print_class_body(const GDScriptParser::ClassNode *p_class, bool p_is_root) {
	const GDScriptParser::ClassNode::Member *previous = nullptr;
	for (int i = 0; i < p_class->members.size(); i++) {
		const GDScriptParser::ClassNode::Member &member = p_class->members[i];
		// Unnamed enum values are flattened into the class; only the first one
		// renders (it reconstructs the whole `enum { ... }`), so skip the rest.
		if (member.type == GDScriptParser::ClassNode::Member::ENUM_VALUE && member.enum_value.index != 0) {
			continue;
		}

		const GDScriptParser::Node *node = member_node(member);
		const int start_line = member_start_line(member);

		// Canonical vertical spacing around definitions (functions and classes):
		// two blank lines at the top level, one inside a nested class. Other
		// member kinds (vars, constants, signals) keep no enforced blank.
		const int top_level_definition_blanks = 2;
		const int nested_definition_blanks = 1;
		int required_blanks = 0;
		if (previous != nullptr) {
			const bool definition = member_is_definition(member) || member_is_definition(*previous);
			required_blanks = definition ? (p_is_root ? top_level_definition_blanks : nested_definition_blanks) : 0;
		}
		if (start_line > 0) {
			emit_leading_trivia(start_line, required_blanks);
		}

		print_member(member);

		if (node != nullptr) {
			// A member that emits a suite internally (a function/class body, or a
			// variable with an inline `get:`/`set:` property block) already attaches
			// any inline comment on its last body line; emitting one here too would
			// duplicate it. Every other member (and a single-line one, e.g. a bodyless
			// abstract method) can carry an inline comment on its closing line here.
			bool has_own_body_flush = false;
			if (node->start_line != node->end_line) {
				if (member.type == GDScriptParser::ClassNode::Member::FUNCTION ||
						member.type == GDScriptParser::ClassNode::Member::CLASS) {
					has_own_body_flush = true;
				} else if (member.type == GDScriptParser::ClassNode::Member::VARIABLE &&
						member.variable->property == GDScriptParser::VariableNode::PROP_INLINE) {
					has_own_body_flush = true;
				}
			}
			if (!has_own_body_flush) {
				emit_trailing_comment(node->end_line);
			}
			// A body-trailing comment may already have advanced the cursor past
			// the node's end line; never move it backward (that would re-emit it).
			if (node->end_line > last_emitted_line) {
				last_emitted_line = node->end_line;
			}
		}
		previous = &member;
	}

	if (p_is_root) {
		flush_tail_comments();
	}
}

void GDScriptPrinter::print_member(const GDScriptParser::ClassNode::Member &p_member) {
	switch (p_member.type) {
		case GDScriptParser::ClassNode::Member::CLASS:
			print_annotations(p_member.m_class->annotations, p_member.m_class->start_line);
			print_class(p_member.m_class, false, false);
			break;
		case GDScriptParser::ClassNode::Member::CONSTANT:
			print_annotations(p_member.constant->annotations, p_member.constant->start_line);
			print_constant(p_member.constant);
			break;
		case GDScriptParser::ClassNode::Member::FUNCTION:
			print_annotations(p_member.function->annotations, p_member.function->start_line);
			print_function(p_member.function);
			break;
		case GDScriptParser::ClassNode::Member::SIGNAL:
			print_annotations(p_member.signal->annotations, p_member.signal->start_line);
			print_signal(p_member.signal);
			break;
		case GDScriptParser::ClassNode::Member::VARIABLE:
			print_annotations(p_member.variable->annotations, p_member.variable->start_line);
			print_variable(p_member.variable);
			break;
		case GDScriptParser::ClassNode::Member::ENUM:
			print_annotations(p_member.m_enum->annotations, p_member.m_enum->start_line);
			print_enum(p_member.m_enum);
			break;
		case GDScriptParser::ClassNode::Member::ENUM_VALUE:
			// Unnamed enum values are flattened into the class as individual members.
			// Reconstruct the whole `enum { ... }` once, from the first value.
			if (p_member.enum_value.index == 0 && p_member.enum_value.parent_enum != nullptr) {
				print_enum(p_member.enum_value.parent_enum);
			}
			break;
		case GDScriptParser::ClassNode::Member::GROUP:
			print_annotations(p_member.annotation->annotations);
			write_indent();
			print_annotation_inline(p_member.annotation);
			newline();
			break;
		case GDScriptParser::ClassNode::Member::UNDEFINED:
			ERR_FAIL_MSG("GDScriptPrinter: undefined class member.");
	}
}

void GDScriptPrinter::print_annotations(const List<GDScriptParser::AnnotationNode *> &p_annotations, int p_target_line) {
	for (const GDScriptParser::AnnotationNode *annotation : p_annotations) {
		// Full-line comments that sit between the previous annotation and this one.
		if (annotation->start_line > 0) {
			flush_trivia_until(annotation->start_line);
		}
		write_indent();
		print_annotation_inline(annotation);
		newline();
		if (annotation->end_line > last_emitted_line) {
			last_emitted_line = annotation->end_line;
		}
		// An inline comment on the annotation's own line. When the annotation shares
		// its source line with the annotated node (`@export var x  # note`), the
		// formatter splits them onto separate lines and the comment belongs to the
		// node's line, which the node's own trailing-comment emission handles -- so
		// emit it here only when the annotation occupies its own source line.
		if (p_target_line == 0 || annotation->end_line < p_target_line) {
			emit_trailing_comment(annotation->end_line);
		}
	}
	// Full-line comments between the last annotation and the node it annotates.
	if (p_target_line > 0) {
		flush_trivia_until(p_target_line);
	}
}

void GDScriptPrinter::print_annotation_inline(const GDScriptParser::AnnotationNode *p_annotation) {
	write(p_annotation->name);
	if (p_annotation->arguments.is_empty()) {
		return;
	}
	write("(");
	for (int i = 0; i < p_annotation->arguments.size(); i++) {
		if (i > 0) {
			write(", ");
		}
		if (i < p_annotation->argument_names.size() && !String(p_annotation->argument_names[i]).is_empty()) {
			write(p_annotation->argument_names[i]);
			write(" = ");
		}
		print_expression(p_annotation->arguments[i]);
	}
	write(")");
}

void GDScriptPrinter::print_annotation_declaration(const GDScriptParser::AnnotationDeclarationNode *p_declaration) {
	write_indent();
	write("annotation ");
	if (p_declaration->identifier != nullptr) {
		write(p_declaration->identifier->name);
	}
	write("(");
	for (int i = 0; i < p_declaration->parameters.size(); i++) {
		if (i > 0) {
			write(", ");
		}
		print_parameter(p_declaration->parameters[i]);
	}
	if (p_declaration->rest_parameter != nullptr) {
		if (!p_declaration->parameters.is_empty()) {
			write(", ");
		}
		write("...");
		print_parameter(p_declaration->rest_parameter);
	}
	write(")");

	write(" targets ");
	const uint32_t targets = p_declaration->targets;
	const struct {
		GDScriptParser::AnnotationDeclarationNode::Target flag;
		const char *name;
	} target_names[] = {
		{ GDScriptParser::AnnotationDeclarationNode::TARGET_CLASS, "CLASS" },
		{ GDScriptParser::AnnotationDeclarationNode::TARGET_METHOD, "METHOD" },
		{ GDScriptParser::AnnotationDeclarationNode::TARGET_VARIABLE, "VARIABLE" },
		{ GDScriptParser::AnnotationDeclarationNode::TARGET_SIGNAL, "SIGNAL" },
		{ GDScriptParser::AnnotationDeclarationNode::TARGET_CONSTANT, "CONSTANT" },
	};
	bool first = true;
	for (const auto &entry : target_names) {
		if (targets & entry.flag) {
			if (!first) {
				write(", ");
			}
			write(entry.name);
			first = false;
		}
	}
	newline();
}

void GDScriptPrinter::print_function(const GDScriptParser::FunctionNode *p_function) {
	write_indent();
	if (p_function->is_abstract) {
		write("abstract ");
	}
	if (p_function->is_final) {
		write("final ");
	}
	if (p_function->is_static) {
		write("static ");
	}
	if (p_function->is_declared_async) {
		write("async ");
	}
	write("func ");
	if (p_function->identifier != nullptr) {
		write(p_function->identifier->name);
	}
	print_type_parameters(p_function->type_parameters);
	write("(");
	for (int i = 0; i < p_function->parameters.size(); i++) {
		if (i > 0) {
			write(", ");
		}
		print_parameter(p_function->parameters[i]);
	}
	if (p_function->rest_parameter != nullptr) {
		if (!p_function->parameters.is_empty()) {
			write(", ");
		}
		write("...");
		print_parameter(p_function->rest_parameter);
	}
	write(")");
	if (p_function->return_type != nullptr) {
		write(" -> ");
		print_type(p_function->return_type);
	}
	if (p_function->is_abstract && (p_function->body == nullptr || p_function->body->statements.is_empty())) {
		// A well-formed abstract method declares a signature only (its body is empty);
		// a trailing `:` is a parse error, so emit the bare declaration line. (An
		// abstract method that still carries real statements is an error fixture; fall
		// through so its body is preserved and the parsed tree is unchanged.)
		newline();
		return;
	}
	write(":");
	newline();
	if (p_function->body == nullptr) {
		return;
	}
	last_emitted_line = p_function->start_line;
	emit_trailing_comment(p_function->start_line);
	indent_level++;
	print_suite(p_function->body);
	flush_block_tail_comments();
	indent_level--;
}

void GDScriptPrinter::print_variable(const GDScriptParser::VariableNode *p_variable) {
	write_indent();
	if (p_variable->is_static) {
		write("static ");
	}
	if (p_variable->is_final) {
		write("final ");
	}
	write("var ");
	write(p_variable->identifier->name);

	if (p_variable->datatype_specifier != nullptr) {
		write(": ");
		print_type(p_variable->datatype_specifier);
		if (p_variable->initializer != nullptr) {
			write(" = ");
			print_expression(p_variable->initializer);
		}
	} else if (p_variable->infer_datatype) {
		write(" := ");
		if (p_variable->initializer != nullptr) {
			print_expression(p_variable->initializer);
		}
	} else if (p_variable->initializer != nullptr) {
		write(" = ");
		print_expression(p_variable->initializer);
	}

	if (p_variable->property == GDScriptParser::VariableNode::PROP_NONE) {
		newline();
		return;
	}

	write(":");
	newline();
	last_emitted_line = p_variable->start_line;
	emit_trailing_comment(p_variable->start_line);
	indent_level++;
	if (p_variable->property == GDScriptParser::VariableNode::PROP_INLINE) {
		if (p_variable->getter != nullptr) {
			// Full-line comments between the `var x:` line and the `get:` block.
			flush_trivia_until(p_variable->getter->start_line);
			write_indent();
			write("get:");
			newline();
			last_emitted_line = p_variable->getter->start_line;
			emit_trailing_comment(p_variable->getter->start_line);
			indent_level++;
			print_suite(p_variable->getter->body);
			flush_block_tail_comments();
			indent_level--;
		}
		if (p_variable->setter != nullptr) {
			// Full-line comments before the `set(...):` block (after `var x:` or the getter).
			flush_trivia_until(p_variable->setter->start_line);
			write_indent();
			write("set(");
			if (p_variable->setter_parameter != nullptr) {
				write(p_variable->setter_parameter->name);
			}
			write("):");
			newline();
			last_emitted_line = p_variable->setter->start_line;
			emit_trailing_comment(p_variable->setter->start_line);
			indent_level++;
			print_suite(p_variable->setter->body);
			flush_block_tail_comments();
			indent_level--;
		}
	} else { // PROP_SETGET
		// The `get = getter, set = setter` clauses share a single indented line;
		// splitting them across lines ends the property block early ("Expected end
		// of indented block for property."). Interior comments (between `var x:` and
		// the clauses, or between the clauses) therefore have no place to interleave,
		// so emit them all on their own lines *before* the single clause line. Doing
		// it before (not after) keeps it idempotent: on a reformat those comments are
		// already above the clause line and stay there.
		flush_trivia_until(p_variable->end_line);
		write_indent();
		bool wrote_clause = false;
		if (p_variable->getter_pointer != nullptr) {
			write("get = ");
			write(p_variable->getter_pointer->name);
			wrote_clause = true;
		}
		if (p_variable->setter_pointer != nullptr) {
			if (wrote_clause) {
				write(", ");
			}
			write("set = ");
			write(p_variable->setter_pointer->name);
		}
		newline();
		if (p_variable->end_line > last_emitted_line) {
			last_emitted_line = p_variable->end_line;
		}
	}
	indent_level--;
}

void GDScriptPrinter::print_constant(const GDScriptParser::ConstantNode *p_constant) {
	write_indent();
	write("const ");
	write(p_constant->identifier->name);
	if (p_constant->datatype_specifier != nullptr) {
		write(": ");
		print_type(p_constant->datatype_specifier);
		write(" = ");
	} else if (p_constant->infer_datatype) {
		write(" := ");
	} else {
		write(" = ");
	}
	if (p_constant->initializer != nullptr) {
		print_expression(p_constant->initializer);
	}
	newline();
}

void GDScriptPrinter::print_signal(const GDScriptParser::SignalNode *p_signal) {
	write_indent();
	write("signal ");
	write(p_signal->identifier->name);
	if (!p_signal->parameters.is_empty()) {
		write("(");
		for (int i = 0; i < p_signal->parameters.size(); i++) {
			if (i > 0) {
				write(", ");
			}
			print_parameter(p_signal->parameters[i]);
		}
		write(")");
	}
	newline();
}

void GDScriptPrinter::print_enum(const GDScriptParser::EnumNode *p_enum) {
	write_indent();
	write("enum ");
	if (p_enum->identifier != nullptr) {
		write(p_enum->identifier->name);
		write(" ");
	}
	// Keep an enum the author wrote across several lines multi-line, so comments
	// and value doc comments between its values keep a place to live. (When it has
	// no values and no interior comment, `print_delimited_items` collapses it to a
	// single-line `{}`.)
	const bool multiline = node_was_authored_multiline(p_enum);
	print_delimited_items(
			"{", "}", p_enum->values.size(), multiline, p_enum->start_line, p_enum->end_line,
			[&](int p_index) {
				const GDScriptParser::EnumNode::Value &value = p_enum->values[p_index];
				write(value.identifier->name);
				if (value.custom_value != nullptr) {
					write(" = ");
					print_expression(value.custom_value);
				}
			},
			[&](int p_index) { return p_enum->values[p_index].line; },
			[&](int p_index) {
				const GDScriptParser::EnumNode::Value &value = p_enum->values[p_index];
				return value.custom_value != nullptr ? value.custom_value->end_line : value.line;
			});
	newline();
}

void GDScriptPrinter::print_parameter(const GDScriptParser::ParameterNode *p_parameter) {
	write(p_parameter->identifier->name);
	if (p_parameter->datatype_specifier != nullptr) {
		write(": ");
		print_type(p_parameter->datatype_specifier);
		if (p_parameter->initializer != nullptr) {
			write(" = ");
			print_expression(p_parameter->initializer);
		}
	} else if (p_parameter->infer_datatype) {
		write(" := ");
		if (p_parameter->initializer != nullptr) {
			print_expression(p_parameter->initializer);
		}
	} else if (p_parameter->initializer != nullptr) {
		write(" = ");
		print_expression(p_parameter->initializer);
	}
}

void GDScriptPrinter::print_type_parameters(const Vector<GDScriptParser::TypeParameterNode *> &p_params) {
	if (p_params.is_empty()) {
		return;
	}
	write("[");
	for (int i = 0; i < p_params.size(); i++) {
		if (i > 0) {
			write(", ");
		}
		const GDScriptParser::TypeParameterNode *type_parameter = p_params[i];
		if (type_parameter->identifier != nullptr) {
			write(type_parameter->identifier->name);
		}
		if (type_parameter->bound != nullptr) {
			write(": ");
			print_type(type_parameter->bound);
		}
	}
	write("]");
}

void GDScriptPrinter::print_type(const GDScriptParser::TypeNode *p_type) {
	if (p_type == nullptr) {
		return;
	}
	if (p_type->type_chain.is_empty()) {
		write("void");
	} else {
		for (int i = 0; i < p_type->type_chain.size(); i++) {
			if (i > 0) {
				write(".");
			}
			write(p_type->type_chain[i]->name);
		}
	}

	if (p_type->has_signature) {
		write("[[");
		for (int i = 0; i < p_type->signature_parameter_types.size(); i++) {
			if (i > 0) {
				write(", ");
			}
			print_type(p_type->signature_parameter_types[i]);
		}
		write("]");
		if (p_type->signature_return_type != nullptr) {
			write(", ");
			print_type(p_type->signature_return_type);
		}
		write("]");
	} else if (!p_type->container_types.is_empty()) {
		write("[");
		for (int i = 0; i < p_type->container_types.size(); i++) {
			if (i > 0) {
				write(", ");
			}
			print_type(p_type->container_types[i]);
		}
		write("]");
	}

	if (p_type->is_nullable) {
		write("?");
	}
}

void GDScriptPrinter::print_suite(const GDScriptParser::SuiteNode *p_suite) {
	if (p_suite == nullptr || p_suite->statements.is_empty()) {
		write_indent();
		write("pass");
		newline();
		return;
	}
	for (int i = 0; i < p_suite->statements.size(); i++) {
		const GDScriptParser::Node *statement = p_suite->statements[i];
		emit_leading_trivia(statement->start_line, 0);
		print_statement(statement);
		// An inline comment on the statement's last physical line trails it. Block
		// statements emit their own body-tail comments, so skip them here to avoid
		// a double emission; every other statement (including multi-line collections)
		// can carry an inline comment on its closing line.
		if (!is_block_statement(statement->type)) {
			emit_trailing_comment(statement->end_line);
		}
		if (statement->end_line > last_emitted_line) {
			last_emitted_line = statement->end_line;
		}
	}
}

void GDScriptPrinter::print_statement(const GDScriptParser::Node *p_statement) {
	// Statement-level annotations (`@warning_ignore(...)`, ...) attach to the node
	// and print on their own lines above it at the same indent.
	print_annotations(p_statement->annotations, p_statement->start_line);
	switch (p_statement->type) {
		case GDScriptParser::Node::VARIABLE:
			print_variable(static_cast<const GDScriptParser::VariableNode *>(p_statement));
			break;
		case GDScriptParser::Node::CONSTANT:
			print_constant(static_cast<const GDScriptParser::ConstantNode *>(p_statement));
			break;
		case GDScriptParser::Node::ASSIGNMENT:
			write_indent();
			print_assignment(static_cast<const GDScriptParser::AssignmentNode *>(p_statement));
			newline();
			break;
		case GDScriptParser::Node::IF:
			print_if(static_cast<const GDScriptParser::IfNode *>(p_statement), false);
			break;
		case GDScriptParser::Node::FOR:
			print_for(static_cast<const GDScriptParser::ForNode *>(p_statement));
			break;
		case GDScriptParser::Node::WHILE:
			print_while(static_cast<const GDScriptParser::WhileNode *>(p_statement));
			break;
		case GDScriptParser::Node::MATCH:
			print_match(static_cast<const GDScriptParser::MatchNode *>(p_statement));
			break;
		case GDScriptParser::Node::RETURN:
			print_return(static_cast<const GDScriptParser::ReturnNode *>(p_statement));
			break;
		case GDScriptParser::Node::ASSERT:
			print_assert(static_cast<const GDScriptParser::AssertNode *>(p_statement));
			break;
		case GDScriptParser::Node::BREAK:
			write_indent();
			write("break");
			newline();
			break;
		case GDScriptParser::Node::CONTINUE:
			write_indent();
			write("continue");
			newline();
			break;
		case GDScriptParser::Node::PASS:
			write_indent();
			write("pass");
			newline();
			break;
		case GDScriptParser::Node::BREAKPOINT:
			write_indent();
			write("breakpoint");
			newline();
			break;
		default:
			ERR_FAIL_COND_MSG(!p_statement->is_expression(), "GDScriptPrinter: unhandled statement node type " + itos(p_statement->type) + ".");
			write_indent();
			print_expression(static_cast<const GDScriptParser::ExpressionNode *>(p_statement));
			newline();
			break;
	}
}

bool GDScriptPrinter::print_statement_inline(const GDScriptParser::Node *p_statement) {
	switch (p_statement->type) {
		case GDScriptParser::Node::PASS:
			write("pass");
			return true;
		case GDScriptParser::Node::BREAK:
			write("break");
			return true;
		case GDScriptParser::Node::CONTINUE:
			write("continue");
			return true;
		case GDScriptParser::Node::BREAKPOINT:
			write("breakpoint");
			return true;
		case GDScriptParser::Node::RETURN: {
			const GDScriptParser::ReturnNode *return_node = static_cast<const GDScriptParser::ReturnNode *>(p_statement);
			write("return");
			if (return_node->return_value != nullptr) {
				write(" ");
				print_expression(return_node->return_value);
			}
			return true;
		}
		case GDScriptParser::Node::ASSERT: {
			const GDScriptParser::AssertNode *assert_node = static_cast<const GDScriptParser::AssertNode *>(p_statement);
			write("assert(");
			print_expression(assert_node->condition);
			if (assert_node->message != nullptr) {
				write(", ");
				print_expression(assert_node->message);
			}
			write(")");
			return true;
		}
		case GDScriptParser::Node::ASSIGNMENT:
			print_assignment(static_cast<const GDScriptParser::AssignmentNode *>(p_statement));
			return true;
		case GDScriptParser::Node::VARIABLE: {
			const GDScriptParser::VariableNode *variable = static_cast<const GDScriptParser::VariableNode *>(p_statement);
			if (variable->property != GDScriptParser::VariableNode::PROP_NONE || variable->is_static) {
				return false;
			}
			write("var ");
			write(variable->identifier->name);
			if (variable->datatype_specifier != nullptr) {
				write(": ");
				print_type(variable->datatype_specifier);
				if (variable->initializer != nullptr) {
					write(" = ");
					print_expression(variable->initializer);
				}
			} else if (variable->infer_datatype) {
				write(" := ");
				if (variable->initializer != nullptr) {
					print_expression(variable->initializer);
				}
			} else if (variable->initializer != nullptr) {
				write(" = ");
				print_expression(variable->initializer);
			}
			return true;
		}
		default:
			if (p_statement->is_expression()) {
				print_expression(static_cast<const GDScriptParser::ExpressionNode *>(p_statement));
				return true;
			}
			return false;
	}
}

bool GDScriptPrinter::try_print_suite_inline(const GDScriptParser::SuiteNode *p_suite) {
	if (p_suite == nullptr || p_suite->statements.is_empty()) {
		return false;
	}
	const int previous_length = output.length();
	for (int i = 0; i < p_suite->statements.size(); i++) {
		if (i > 0) {
			write("; ");
		}
		if (!print_statement_inline(p_suite->statements[i])) {
			// Roll back any partial inline output and let the caller emit a block.
			output = output.substr(0, previous_length);
			return false;
		}
	}
	return true;
}

void GDScriptPrinter::print_assignment(const GDScriptParser::AssignmentNode *p_assignment) {
	print_expression(p_assignment->assignee);
	write(" ");
	write(assignment_operator_text(p_assignment->operation));
	write(" ");
	print_expression(p_assignment->assigned_value);
}

void GDScriptPrinter::print_if(const GDScriptParser::IfNode *p_if, bool p_is_elif) {
	write_indent();
	write(p_is_elif ? "elif " : "if ");
	print_expression(p_if->condition);
	write(":");
	newline();
	last_emitted_line = p_if->start_line;
	emit_trailing_comment(p_if->start_line);
	indent_level++;
	print_suite(p_if->true_block);
	flush_block_tail_comments();
	indent_level--;

	if (p_if->false_block == nullptr) {
		return;
	}

	// An `elif` is parsed as an else block holding a single `if` that begins on the same line.
	const bool is_elif_chain = p_if->false_block->statements.size() == 1 &&
			p_if->false_block->statements[0]->type == GDScriptParser::Node::IF &&
			p_if->false_block->start_line == p_if->false_block->statements[0]->start_line;
	// Invariant: a continuation header line (`elif`/`else` here, like `get:`/`set:`
	// or a class header sub-line) is not a fresh statement/member, so the enclosing
	// suite loop gives it no leading-trivia flush. Before writing it, flush every
	// unemitted trivia line (comment or recovered annotation) above its source line,
	// then attach any inline comment on the line itself.
	if (is_elif_chain) {
		const GDScriptParser::IfNode *elif = static_cast<const GDScriptParser::IfNode *>(p_if->false_block->statements[0]);
		flush_trivia_until(elif->start_line);
		print_if(elif, true); // The recursive call attaches the `elif` line's own inline comment.
	} else {
		// The else suite's `start_line` is its first statement, not the `else` line,
		// so recover the `else` line to place comments above it and its own inline
		// comment correctly. Comments between `else:` and the first statement are
		// flushed by `print_suite`'s leading-trivia pass.
		const int else_line = find_else_line(p_if->true_block->end_line, p_if->false_block->start_line);
		flush_trivia_until(else_line > 0 ? else_line : p_if->false_block->start_line);
		write_indent();
		write("else:");
		newline();
		if (else_line > last_emitted_line) {
			last_emitted_line = else_line;
		}
		emit_trailing_comment(else_line);
		indent_level++;
		print_suite(p_if->false_block);
		flush_block_tail_comments();
		indent_level--;
	}
}

void GDScriptPrinter::print_for(const GDScriptParser::ForNode *p_for) {
	write_indent();
	write("for ");
	write(p_for->variable->name);
	if (p_for->datatype_specifier != nullptr) {
		write(": ");
		print_type(p_for->datatype_specifier);
	}
	write(" in ");
	print_expression(p_for->list);
	write(":");
	newline();
	last_emitted_line = p_for->start_line;
	emit_trailing_comment(p_for->start_line);
	indent_level++;
	print_suite(p_for->loop);
	flush_block_tail_comments();
	indent_level--;
}

void GDScriptPrinter::print_while(const GDScriptParser::WhileNode *p_while) {
	write_indent();
	write("while ");
	print_expression(p_while->condition);
	write(":");
	newline();
	last_emitted_line = p_while->start_line;
	emit_trailing_comment(p_while->start_line);
	indent_level++;
	print_suite(p_while->loop);
	flush_block_tail_comments();
	indent_level--;
}

void GDScriptPrinter::print_match(const GDScriptParser::MatchNode *p_match) {
	write_indent();
	write("match ");
	print_expression(p_match->test);
	write(":");
	newline();
	last_emitted_line = p_match->start_line;
	emit_trailing_comment(p_match->start_line);
	indent_level++;
	if (p_match->branches.is_empty()) {
		// A branchless `match` still needs an indented body line (e.g. `pass`).
		write_indent();
		write("pass");
		newline();
	}
	for (int i = 0; i < p_match->branches.size(); i++) {
		const GDScriptParser::MatchBranchNode *branch = p_match->branches[i];
		emit_leading_trivia(branch->start_line, 0);
		print_match_branch(branch);
		// The branch's tail-comment flush may already have advanced the cursor; never
		// move it backward (that would re-emit a comment).
		if (branch->end_line > last_emitted_line) {
			last_emitted_line = branch->end_line;
		}
	}
	indent_level--;
}

void GDScriptPrinter::print_match_branch(const GDScriptParser::MatchBranchNode *p_branch) {
	// A match branch may carry its own annotations (e.g. `@warning_ignore(...)`).
	print_annotations(p_branch->annotations, p_branch->start_line);
	write_indent();
	for (int i = 0; i < p_branch->patterns.size(); i++) {
		if (i > 0) {
			write(", ");
		}
		print_pattern(p_branch->patterns[i]);
	}
	if (p_branch->guard_body != nullptr && !p_branch->guard_body->statements.is_empty()) {
		write(" when ");
		print_expression(static_cast<const GDScriptParser::ExpressionNode *>(p_branch->guard_body->statements[0]));
	}
	write(":");
	newline();
	// A multi-line array/dict pattern advances the cursor past `start_line`, so the
	// header line (where the `:` lands and an inline comment would sit) is the later
	// of the branch start and the cursor; never move the cursor backward.
	const int header_line = MAX(p_branch->start_line, last_emitted_line);
	last_emitted_line = header_line;
	emit_trailing_comment(header_line);
	indent_level++;
	print_suite(p_branch->block);
	flush_block_tail_comments();
	indent_level--;
}

void GDScriptPrinter::print_pattern(const GDScriptParser::PatternNode *p_pattern) {
	switch (p_pattern->pattern_type) {
		case GDScriptParser::PatternNode::PT_LITERAL:
			print_literal(p_pattern->literal);
			break;
		case GDScriptParser::PatternNode::PT_EXPRESSION:
			print_expression(p_pattern->expression);
			break;
		case GDScriptParser::PatternNode::PT_BIND:
			write("var ");
			write(p_pattern->bind->name);
			break;
		case GDScriptParser::PatternNode::PT_ARRAY:
			// Route multi-line array patterns through the shared delimited-items
			// machinery so interior comments are interleaved (same as array literals).
			print_delimited_items(
					"[", "]", p_pattern->array.size(), node_was_authored_multiline(p_pattern),
					p_pattern->start_line, p_pattern->end_line,
					[&](int p_index) { print_pattern(p_pattern->array[p_index]); },
					[&](int p_index) { return p_pattern->array[p_index]->start_line; },
					[&](int p_index) { return p_pattern->array[p_index]->end_line; });
			break;
		case GDScriptParser::PatternNode::PT_DICTIONARY:
			print_delimited_items(
					"{", "}", p_pattern->dictionary.size(), node_was_authored_multiline(p_pattern),
					p_pattern->start_line, p_pattern->end_line,
					[&](int p_index) {
						const GDScriptParser::PatternNode::Pair &pair = p_pattern->dictionary[p_index];
						if (pair.key != nullptr) {
							print_expression(pair.key);
							if (pair.value_pattern != nullptr) {
								write(": ");
								print_pattern(pair.value_pattern);
							}
						} else {
							write("..");
						}
					},
					[&](int p_index) {
						const GDScriptParser::PatternNode::Pair &pair = p_pattern->dictionary[p_index];
						if (pair.key != nullptr) {
							return pair.key->start_line;
						}
						return pair.value_pattern != nullptr ? pair.value_pattern->start_line : p_pattern->start_line;
					},
					[&](int p_index) {
						const GDScriptParser::PatternNode::Pair &pair = p_pattern->dictionary[p_index];
						if (pair.value_pattern != nullptr) {
							return pair.value_pattern->end_line;
						}
						return pair.key != nullptr ? pair.key->end_line : p_pattern->start_line;
					});
			break;
		case GDScriptParser::PatternNode::PT_REST:
			write("..");
			break;
		case GDScriptParser::PatternNode::PT_WILDCARD:
			write("_");
			break;
	}
}

void GDScriptPrinter::print_return(const GDScriptParser::ReturnNode *p_return) {
	write_indent();
	write("return");
	if (p_return->return_value != nullptr) {
		write(" ");
		print_expression(p_return->return_value);
	}
	newline();
}

void GDScriptPrinter::print_assert(const GDScriptParser::AssertNode *p_assert) {
	write_indent();
	write("assert(");
	print_expression(p_assert->condition);
	if (p_assert->message != nullptr) {
		write(", ");
		print_expression(p_assert->message);
	}
	write(")");
	newline();
}

void GDScriptPrinter::print_expression(const GDScriptParser::ExpressionNode *p_expression) {
	if (p_expression == nullptr) {
		return;
	}
	switch (p_expression->type) {
		case GDScriptParser::Node::LITERAL:
			print_literal(static_cast<const GDScriptParser::LiteralNode *>(p_expression));
			break;
		case GDScriptParser::Node::IDENTIFIER:
			write(static_cast<const GDScriptParser::IdentifierNode *>(p_expression)->name);
			break;
		case GDScriptParser::Node::SELF:
			write("self");
			break;
		case GDScriptParser::Node::BINARY_OPERATOR:
			print_binary_op(static_cast<const GDScriptParser::BinaryOpNode *>(p_expression));
			break;
		case GDScriptParser::Node::UNARY_OPERATOR:
			print_unary_op(static_cast<const GDScriptParser::UnaryOpNode *>(p_expression));
			break;
		case GDScriptParser::Node::TERNARY_OPERATOR:
			print_ternary_op(static_cast<const GDScriptParser::TernaryOpNode *>(p_expression));
			break;
		case GDScriptParser::Node::ASSIGNMENT:
			print_assignment(static_cast<const GDScriptParser::AssignmentNode *>(p_expression));
			break;
		case GDScriptParser::Node::CALL:
			print_call(static_cast<const GDScriptParser::CallNode *>(p_expression));
			break;
		case GDScriptParser::Node::SUBSCRIPT:
			print_subscript(static_cast<const GDScriptParser::SubscriptNode *>(p_expression));
			break;
		case GDScriptParser::Node::CAST:
			print_cast(static_cast<const GDScriptParser::CastNode *>(p_expression));
			break;
		case GDScriptParser::Node::AWAIT:
			print_await(static_cast<const GDScriptParser::AwaitNode *>(p_expression));
			break;
		case GDScriptParser::Node::ARRAY:
			print_array(static_cast<const GDScriptParser::ArrayNode *>(p_expression));
			break;
		case GDScriptParser::Node::DICTIONARY:
			print_dictionary(static_cast<const GDScriptParser::DictionaryNode *>(p_expression));
			break;
		case GDScriptParser::Node::LAMBDA:
			print_lambda(static_cast<const GDScriptParser::LambdaNode *>(p_expression));
			break;
		case GDScriptParser::Node::PRELOAD:
			print_preload(static_cast<const GDScriptParser::PreloadNode *>(p_expression));
			break;
		case GDScriptParser::Node::GET_NODE:
			print_get_node(static_cast<const GDScriptParser::GetNodeNode *>(p_expression));
			break;
		case GDScriptParser::Node::TYPE_TEST:
			print_type_test(static_cast<const GDScriptParser::TypeTestNode *>(p_expression));
			break;
		default:
			ERR_FAIL_MSG("GDScriptPrinter: unhandled expression node type " + itos(p_expression->type) + ".");
	}
}

void GDScriptPrinter::print_literal(const GDScriptParser::LiteralNode *p_literal) {
	const uint64_t key = pos_key(p_literal->start_line, p_literal->start_column);
	HashMap<uint64_t, LiteralToken>::ConstIterator found = literals.find(key);
	if (found) {
		// Start from the exact original token text, then apply canonical casing /
		// quoting that never changes the value the literal evaluates to.
		switch (p_literal->value.get_type()) {
			case Variant::STRING:
			case Variant::STRING_NAME:
			case Variant::NODE_PATH:
				write(canonicalize_string_literal(found->value.source));
				break;
			case Variant::INT:
			case Variant::FLOAT:
				write(canonicalize_number_literal(found->value.source));
				break;
			default:
				write(found->value.source);
				break;
		}
		return;
	}
	// Fallback for synthesized literals without a backing token. String-valued
	// literals must still be emitted as escaped, quoted literals (with their
	// StringName / NodePath prefix) rather than as raw decoded text.
	switch (p_literal->value.get_type()) {
		case Variant::STRING:
			write(quote_string_literal(p_literal->value));
			break;
		case Variant::STRING_NAME:
			write("&" + quote_string_literal(p_literal->value));
			break;
		case Variant::NODE_PATH:
			write("^" + quote_string_literal(p_literal->value));
			break;
		default:
			write(p_literal->value.operator String());
			break;
	}
}

void GDScriptPrinter::print_operand(int p_min_precedence, const GDScriptParser::ExpressionNode *p_child) {
	if (p_child == nullptr) {
		return;
	}
	const bool needs_parentheses = expression_precedence(p_child) < p_min_precedence;
	if (needs_parentheses) {
		write("(");
		print_expression(p_child);
		write(")");
	} else {
		print_expression(p_child);
	}
}

void GDScriptPrinter::print_binary_op(const GDScriptParser::BinaryOpNode *p_op) {
	// All GDScript binary operators are left-associative (the parser parses the
	// right operand one precedence level tighter), so the left operand may share
	// the operator's precedence without parentheses while the right operand may
	// not.
	const int precedence = binary_operator_precedence(p_op->operation);
	print_operand(precedence, p_op->left_operand);
	write(" ");
	write(binary_operator_text(p_op->operation));
	write(" ");
	print_operand(precedence + 1, p_op->right_operand);
}

void GDScriptPrinter::print_unary_op(const GDScriptParser::UnaryOpNode *p_op) {
	const String operator_text = unary_operator_text(p_op->operation);
	write(operator_text);
	if (p_op->operation == GDScriptParser::UnaryOpNode::OP_LOGIC_NOT) {
		write(" ");
	}
	print_operand(unary_operator_precedence(p_op->operation), p_op->operand);
}

void GDScriptPrinter::print_ternary_op(const GDScriptParser::TernaryOpNode *p_op) {
	// The value branch (left operand) must be parenthesized when it is itself a
	// ternary, otherwise re-parsing would re-group it; the condition and the
	// alternative branch are parsed at the ternary level (right-associative), so
	// a nested ternary there needs no parentheses.
	print_operand(FPREC_TERNARY + 1, p_op->true_expr);
	write(" if ");
	print_operand(FPREC_TERNARY, p_op->condition);
	write(" else ");
	print_operand(FPREC_TERNARY, p_op->false_expr);
}

void GDScriptPrinter::print_call(const GDScriptParser::CallNode *p_call) {
	if (p_call->is_super) {
		// `super(...)` (callee null) is an implicit call to the parent method of the
		// same name; `function_name` holds the enclosing method's name and must not
		// be emitted. `super.method(...)` keeps its explicit callee.
		write("super");
		if (p_call->callee != nullptr) {
			write(".");
			print_operand(FPREC_CALL, p_call->callee);
		}
	} else if (p_call->callee != nullptr) {
		// The callee binds as the base of a call, tighter than any operator.
		print_operand(FPREC_CALL, p_call->callee);
	} else if (!String(p_call->function_name).is_empty()) {
		write(p_call->function_name);
	}
	print_argument_list(p_call->arguments, p_call->argument_names, node_was_authored_multiline(p_call),
			p_call->start_line, p_call->end_line);
}

// Prints a parenthesized argument list, honoring the author's single- or
// multi-line layout (see `print_delimited_items`).
void GDScriptPrinter::print_argument_list(const Vector<GDScriptParser::ExpressionNode *> &p_arguments,
		const Vector<StringName> &p_argument_names, bool p_multiline, int p_open_line, int p_close_line) {
	print_delimited_items(
			"(", ")", p_arguments.size(), p_multiline, p_open_line, p_close_line,
			[&](int p_index) {
				if (p_index < p_argument_names.size() && !String(p_argument_names[p_index]).is_empty()) {
					write(p_argument_names[p_index]);
					write(" = ");
				}
				print_expression(p_arguments[p_index]);
			},
			[&](int p_index) { return p_arguments[p_index]->start_line; },
			[&](int p_index) { return p_arguments[p_index]->end_line; });
}

void GDScriptPrinter::print_subscript(const GDScriptParser::SubscriptNode *p_subscript) {
	// `.attribute` and `[index]` bind tighter than every operator, so a base that
	// is itself an operator expression must be parenthesized to keep the tree.
	print_operand(FPREC_CALL, p_subscript->base);
	if (p_subscript->is_attribute) {
		write(".");
		if (p_subscript->attribute != nullptr) {
			write(p_subscript->attribute->name);
		}
		return;
	}
	write("[");
	if (!p_subscript->type_arguments.is_empty()) {
		for (int i = 0; i < p_subscript->type_arguments.size(); i++) {
			if (i > 0) {
				write(", ");
			}
			print_expression(p_subscript->type_arguments[i]);
			if (i < p_subscript->type_argument_is_nullable.size() && p_subscript->type_argument_is_nullable[i]) {
				write("?");
			}
		}
	} else {
		print_expression(p_subscript->index);
	}
	write("]");
}

void GDScriptPrinter::print_cast(const GDScriptParser::CastNode *p_cast) {
	print_operand(FPREC_CAST, p_cast->operand);
	write(" as ");
	print_type(p_cast->cast_type);
}

void GDScriptPrinter::print_await(const GDScriptParser::AwaitNode *p_await) {
	write("await ");
	print_operand(FPREC_AWAIT, p_await->to_await);
}

void GDScriptPrinter::print_array(const GDScriptParser::ArrayNode *p_array) {
	print_delimited_items(
			"[", "]", p_array->elements.size(), node_was_authored_multiline(p_array),
			p_array->start_line, p_array->end_line,
			[&](int p_index) { print_expression(p_array->elements[p_index]); },
			[&](int p_index) { return p_array->elements[p_index]->start_line; },
			[&](int p_index) { return p_array->elements[p_index]->end_line; });
}

void GDScriptPrinter::print_dictionary(const GDScriptParser::DictionaryNode *p_dictionary) {
	const bool lua_style = p_dictionary->style == GDScriptParser::DictionaryNode::LUA_TABLE;
	print_delimited_items(
			"{", "}", p_dictionary->elements.size(), node_was_authored_multiline(p_dictionary),
			p_dictionary->start_line, p_dictionary->end_line,
			[&](int p_index) {
				const GDScriptParser::DictionaryNode::Pair &pair = p_dictionary->elements[p_index];
				print_expression(pair.key);
				write(lua_style ? " = " : ": ");
				print_expression(pair.value);
			},
			[&](int p_index) { return p_dictionary->elements[p_index].key->start_line; },
			[&](int p_index) { return p_dictionary->elements[p_index].value->end_line; });
}

void GDScriptPrinter::print_lambda(const GDScriptParser::LambdaNode *p_lambda) {
	const GDScriptParser::FunctionNode *function = p_lambda->function;
	write("func");
	if (function->identifier != nullptr) {
		write(" ");
		write(function->identifier->name);
	}
	write("(");
	for (int i = 0; i < function->parameters.size(); i++) {
		if (i > 0) {
			write(", ");
		}
		print_parameter(function->parameters[i]);
	}
	if (function->rest_parameter != nullptr) {
		if (!function->parameters.is_empty()) {
			write(", ");
		}
		write("...");
		print_parameter(function->rest_parameter);
	}
	write(")");
	if (function->return_type != nullptr) {
		write(" -> ");
		print_type(function->return_type);
	}
	write(":");

	// A lambda the author wrote on a single line keeps its inline body
	// (`func(): return x`). Emitting it as a multi-line block would corrupt any
	// surrounding expression, e.g. `(func(): return x).call()`.
	if (p_lambda->start_line == p_lambda->end_line) {
		write(" ");
		if (try_print_suite_inline(function->body)) {
			return;
		}
		// Could not inline (e.g. a property or static var): drop the trailing space
		// and fall through to the block form.
		output = output.substr(0, output.length() - 1);
	}

	newline();
	last_emitted_line = function->start_line;
	indent_level++;
	print_suite(function->body);
	flush_block_tail_comments();
	indent_level--;
	// A lambda is an expression: the enclosing statement (or a following postfix
	// like `).call()`) supplies the terminator. Drop the block body's trailing
	// newline so it does not double into a spurious blank line that grows on every
	// reformat.
	if (output.ends_with("\n")) {
		output = output.substr(0, output.length() - 1);
	}
}

void GDScriptPrinter::print_preload(const GDScriptParser::PreloadNode *p_preload) {
	write("preload(");
	print_expression(p_preload->path);
	write(")");
}

void GDScriptPrinter::print_get_node(const GDScriptParser::GetNodeNode *p_get_node) {
	if (p_get_node->use_dollar) {
		write("$");
	}
	const Vector<String> segments = p_get_node->full_path.split("/");
	String path;
	for (int i = 0; i < segments.size(); i++) {
		if (i > 0) {
			path += "/";
		}
		path += node_path_segment_text(segments[i]);
	}
	write(path);
}

void GDScriptPrinter::print_type_test(const GDScriptParser::TypeTestNode *p_test) {
	print_operand(FPREC_TYPE_TEST, p_test->operand);
	write(" is ");
	print_type(p_test->test_type);
}

static String read_all_stdin() {
	Vector<uint8_t> bytes;
	uint8_t buffer[4096];
	size_t bytes_read = 0;
	while ((bytes_read = fread(buffer, 1, sizeof(buffer), stdin)) > 0) {
		int previous = bytes.size();
		bytes.resize(previous + (int)bytes_read);
		memcpy(bytes.ptrw() + previous, buffer, bytes_read);
	}
	String result;
	if (!bytes.is_empty()) {
		result.append_utf8((const char *)bytes.ptr(), bytes.size());
	}
	return result;
}

// Writes formatted text without clobbering the original on failure: store into a
// temp sibling, flush, then atomically rename over the target. The original is
// only replaced once the new content is fully and successfully written.
static bool write_file_atomic(const String &p_path, const String &p_content, String &r_error_message) {
	// Choose a temp sibling name unlikely to collide with real files: the pid plus
	// a per-process counter, skipping any candidate that already exists (a stale
	// temp, an unrelated file, or a symlink). Note this is not fully race-safe:
	// `FileAccess` opens via `fopen("wb")` and exposes no exclusive/no-follow create
	// (no `O_EXCL`/`O_NOFOLLOW`), so a symlink planted at the chosen path between the
	// existence check and the open could still be followed and truncated. The
	// unpredictable name plus the existence check make that race very narrow, and it
	// only matters for a writable, attacker-controlled directory; closing it fully
	// would need a FileAccess API change out of scope here.
	static uint32_t temp_counter = 0;
	const uint64_t process_id = OS::get_singleton() != nullptr ? uint64_t(OS::get_singleton()->get_process_id()) : 0;
	String temp_path;
	for (int attempt = 0; attempt < 4096; attempt++) {
		const String candidate = p_path + ".gdformat-tmp." + itos(process_id) + "." + itos(temp_counter++);
		if (!FileAccess::exists(candidate) && !DirAccess::exists(candidate)) {
			temp_path = candidate;
			break;
		}
	}
	if (temp_path.is_empty()) {
		r_error_message = "could not find an unused temporary file name";
		return false;
	}
	{
		Ref<FileAccess> output = FileAccess::open(temp_path, FileAccess::WRITE);
		if (output.is_null()) {
			r_error_message = "could not open temporary file for writing";
			return false;
		}
		if (!output->store_string(p_content)) {
			r_error_message = "could not write formatted text";
			output.unref();
			DirAccess::remove_absolute(temp_path);
			return false;
		}
		output->flush();
		if (output->get_error() != OK) {
			r_error_message = "error while writing formatted text";
			output.unref();
			DirAccess::remove_absolute(temp_path);
			return false;
		}
	}
	if (DirAccess::rename_absolute(temp_path, p_path) != OK) {
		// The atomic replace failed. Crucially, the temp file is NOT deleted here: it
		// holds the fully-formatted content (the desired output). On some platforms
		// `DirAccess::rename` is not a true atomic replace -- the Windows implementation
		// removes the destination before `MoveFileW`, so a failed move can leave the
		// original already gone. If the original is missing, try once more to move the
		// temp into its place (recovering the destination the failed rename removed). If
		// that also fails, leave the temp in place and report its path. The guarantee: a
		// failed write leaves EITHER the original intact OR the formatted content
		// recoverable at the reported path -- never both lost.
		if (!FileAccess::exists(p_path) && !DirAccess::exists(p_path) &&
				DirAccess::rename_absolute(temp_path, p_path) == OK) {
			return true; // Recovered: the formatted content now occupies the original path.
		}
		r_error_message = "could not replace original file; formatted output preserved at \"" + temp_path + "\"";
		return false;
	}
	return true;
}

void GDScriptFormatterCLI::print_raw(const String &p_text) {
	const CharString utf8 = p_text.utf8();
	if (utf8.length() > 0) {
		fwrite(utf8.get_data(), 1, utf8.length(), stdout);
	}
	fflush(stdout);
}

GDScriptFormatterCLI::Options GDScriptFormatterCLI::parse_options(const List<String> &p_cmdline_args) {
	Options options;
	bool reached_command = false;
	for (const String &argument : p_cmdline_args) {
		if (!reached_command) {
			if (argument == "--gdscript-format") {
				reached_command = true;
			}
			continue;
		}
		if (argument == "--write" || argument == "-w") {
			options.mode = MODE_WRITE;
		} else if (argument == "--check") {
			options.mode = MODE_CHECK;
		} else if (argument == "--diff" || argument == "-d") {
			options.mode = MODE_DIFF;
		} else if (argument == "-") {
			options.read_stdin = true;
		} else {
			options.paths.push_back(argument);
		}
	}
	if (options.paths.is_empty()) {
		options.read_stdin = true;
	}
	return options;
}

void GDScriptFormatterCLI::collect_gd_scripts_recursive(const String &p_dir, Vector<String> &r_files, bool &r_had_error) {
	Error open_error = OK;
	Ref<DirAccess> dir = DirAccess::open(p_dir, &open_error);
	if (dir.is_null() || open_error != OK) {
		// An unreadable subtree must not be silently skipped (a CI `--check` would
		// pass blind); report it and mark failure, but keep scanning siblings.
		fprintf(stderr, "%s: could not open directory\n", p_dir.utf8().get_data());
		r_had_error = true;
		return;
	}
	// Skip hidden entries (`.git`, `.godot`, `.import`, ...): `.godot` caches can
	// hold generated `.gd` files that must never be reformatted.
	dir->set_include_hidden(false);
	if (dir->list_dir_begin() != OK) {
		// A searchable-but-unreadable directory opens (the path resolves) but cannot
		// be listed; do not let that subtree be silently skipped.
		fprintf(stderr, "%s: could not list directory\n", p_dir.utf8().get_data());
		r_had_error = true;
		return;
	}
	for (String entry = dir->get_next(); !entry.is_empty(); entry = dir->get_next()) {
		if (entry == "." || entry == ".." || dir->current_is_hidden()) {
			continue;
		}
		const String full_path = p_dir.path_join(entry);
		if (dir->current_is_dir()) {
			// Do not descend into symlinked directories: following them would let a
			// `--write` run escape the target tree (rewriting external `.gd` files) or
			// loop forever on a symlink cycle. `is_link` is queried with the bare entry
			// name because `dir` is positioned at `p_dir`; passing the joined path would
			// (on Unix) be resolved relative to the open directory and miss the link.
			if (dir->is_link(entry)) {
				continue;
			}
			collect_gd_scripts_recursive(full_path, r_files, r_had_error);
		} else if (entry.get_extension() == "gd") {
			r_files.push_back(full_path);
		}
	}
	dir->list_dir_end();
}

Vector<String> GDScriptFormatterCLI::collect_files(const Vector<String> &p_paths, bool &r_had_error) {
	Vector<String> files;
	for (const String &path : p_paths) {
		if (DirAccess::exists(path)) {
			collect_gd_scripts_recursive(path, files, r_had_error);
		} else if (FileAccess::exists(path)) {
			files.push_back(path);
		} else {
			fprintf(stderr, "%s: no such file or directory\n", path.utf8().get_data());
			r_had_error = true;
		}
	}
	// Deterministic order so `--check`/`--diff` output is stable across runs and
	// filesystems (a CI gate must be reproducible).
	files.sort();
	return files;
}

// Splits into lines for diffing. A trailing newline does not introduce a
// phantom empty final line (so the hunk header line counts stay accurate); a
// file without a trailing newline keeps its real last line.
static Vector<String> split_lines_for_diff(const String &p_text) {
	Vector<String> lines = p_text.split("\n");
	if (!lines.is_empty() && p_text.ends_with("\n")) {
		lines.remove_at(lines.size() - 1);
	}
	return lines;
}

String GDScriptFormatterCLI::make_unified_diff(const String &p_path, const String &p_original, const String &p_formatted) {
	const Vector<String> a = split_lines_for_diff(p_original);
	const Vector<String> b = split_lines_for_diff(p_formatted);
	const int n = a.size();
	const int m = b.size();

	String result;
	result += "--- " + p_path + "\n";
	result += "+++ " + p_path + "\n";
	result += "@@ -1," + itos(n) + " +1," + itos(m) + " @@\n";

	// Guard against the O(n*m) LCS table on large files: fall back to a
	// whole-file replace hunk rather than allocating gigabytes.
	const int max_lcs_lines = 5000;
	if (n > max_lcs_lines || m > max_lcs_lines) {
		for (int i = 0; i < n; i++) {
			result += "-" + a[i] + "\n";
		}
		for (int j = 0; j < m; j++) {
			result += "+" + b[j] + "\n";
		}
		return result;
	}

	// Longest common subsequence over lines; the table drives a simple unified
	// diff that emits the whole file as a single hunk.
	LocalVector<int> table;
	table.resize((n + 1) * (m + 1));
	for (uint32_t t = 0; t < table.size(); t++) {
		table[t] = 0;
	}
	const auto at = [&](int p_i, int p_j) -> int & { return table[p_i * (m + 1) + p_j]; };
	for (int i = n - 1; i >= 0; i--) {
		for (int j = m - 1; j >= 0; j--) {
			if (a[i] == b[j]) {
				at(i, j) = at(i + 1, j + 1) + 1;
			} else {
				at(i, j) = MAX(at(i + 1, j), at(i, j + 1));
			}
		}
	}

	int i = 0;
	int j = 0;
	while (i < n && j < m) {
		if (a[i] == b[j]) {
			result += " " + a[i] + "\n";
			i++;
			j++;
		} else if (at(i + 1, j) >= at(i, j + 1)) {
			result += "-" + a[i] + "\n";
			i++;
		} else {
			result += "+" + b[j] + "\n";
			j++;
		}
	}
	while (i < n) {
		result += "-" + a[i] + "\n";
		i++;
	}
	while (j < m) {
		result += "+" + b[j] + "\n";
		j++;
	}
	return result;
}

void GDScriptFormatterCLI::run_from_cmdline() {
	const Options options = parse_options(OS::get_singleton()->get_cmdline_args());

	bool needs_change = false;
	bool had_error = false;

	// Mixing the stdin sentinel (`-`) with explicit paths is rejected rather than
	// silently formatting only stdin and dropping the named files.
	if (options.read_stdin && !options.paths.is_empty()) {
		fprintf(stderr, "gdscript-format: cannot combine stdin (\"-\") with file or directory paths.\n");
		OS::get_singleton()->set_exit_code(EXIT_FAILURE);
		return;
	}

	if (options.read_stdin) {
		const String source = read_all_stdin();
		GDScriptFormatter formatter;
		GDScriptFormatter::Result result;
		if (formatter.format(source, "<stdin>", result) != OK) {
			fprintf(stderr, "<stdin>:%d:%d: %s\n", result.error_line, result.error_column, result.error_message.utf8().get_data());
			OS::get_singleton()->set_exit_code(EXIT_FAILURE);
			return;
		}
		const bool differs = result.formatted != source;
		switch (options.mode) {
			case MODE_CHECK:
				if (differs) {
					fprintf(stdout, "<stdin>\n");
				}
				break;
			case MODE_DIFF:
				if (differs) {
					print_raw(make_unified_diff("<stdin>", source, result.formatted));
				}
				break;
			default:
				print_raw(result.formatted);
				break;
		}
		const bool failure = (options.mode == MODE_CHECK || options.mode == MODE_DIFF) && differs;
		OS::get_singleton()->set_exit_code(failure ? EXIT_FAILURE : EXIT_SUCCESS);
		return;
	}

	const Vector<String> files = collect_files(options.paths, had_error);
	for (const String &file : files) {
		Error read_error = OK;
		const String source = FileAccess::get_file_as_string(file, &read_error);
		if (read_error != OK) {
			fprintf(stderr, "%s: could not read file\n", file.utf8().get_data());
			had_error = true;
			continue;
		}
		GDScriptFormatter formatter;
		GDScriptFormatter::Result result;
		if (formatter.format(source, file, result) != OK) {
			fprintf(stderr, "%s:%d:%d: %s\n", file.utf8().get_data(),
					result.error_line, result.error_column, result.error_message.utf8().get_data());
			had_error = true;
			continue;
		}
		const bool differs = result.formatted != source;
		if (differs) {
			needs_change = true;
		}
		switch (options.mode) {
			case MODE_STDOUT:
				print_raw(result.formatted);
				break;
			case MODE_WRITE:
				if (differs) {
					String write_error;
					if (!write_file_atomic(file, result.formatted, write_error)) {
						fprintf(stderr, "%s: %s\n", file.utf8().get_data(), write_error.utf8().get_data());
						had_error = true;
					}
				}
				break;
			case MODE_CHECK:
				if (differs) {
					fprintf(stdout, "%s\n", file.utf8().get_data());
				}
				break;
			case MODE_DIFF:
				if (differs) {
					print_raw(make_unified_diff(file, source, result.formatted));
				}
				break;
		}
	}
	fflush(stdout);

	const bool failure = had_error || ((options.mode == MODE_CHECK || options.mode == MODE_DIFF) && needs_change);
	OS::get_singleton()->set_exit_code(failure ? EXIT_FAILURE : EXIT_SUCCESS);
}

void GDScriptFormatterCLI::generate_format_tests() {
	String root = "modules/gdscript/tests/scripts/format";
	const List<String> cmdline_args = OS::get_singleton()->get_cmdline_args();
	for (const List<String>::Element *E = cmdline_args.front(); E; E = E->next()) {
		if (E->get() == "--gdscript-generate-format-tests" && E->next()) {
			root = E->next()->get();
			break;
		}
	}

	Vector<String> all_scripts;
	bool had_error = false;
	collect_gd_scripts_recursive(root, all_scripts, had_error);
	all_scripts.sort();

	int written = 0;
	for (const String &input_path : all_scripts) {
		if (input_path.get_file() != "input.gd") {
			continue;
		}
		Error read_error = OK;
		const String source = FileAccess::get_file_as_string(input_path, &read_error);
		if (read_error != OK) {
			fprintf(stderr, "%s: could not read fixture input\n", input_path.utf8().get_data());
			had_error = true;
			continue;
		}
		GDScriptFormatter formatter;
		GDScriptFormatter::Result result;
		if (formatter.format(source, input_path, result) != OK) {
			fprintf(stderr, "%s:%d:%d: %s\n", input_path.utf8().get_data(),
					result.error_line, result.error_column, result.error_message.utf8().get_data());
			had_error = true;
			continue;
		}
		const String expected_path = input_path.get_base_dir().path_join("expected.gd");
		String write_error;
		if (!write_file_atomic(expected_path, result.formatted, write_error)) {
			fprintf(stderr, "%s: %s\n", expected_path.utf8().get_data(), write_error.utf8().get_data());
			had_error = true;
			continue;
		}
		written++;
	}
	fprintf(stdout, "Regenerated %d formatter fixture(s) under %s\n", written, root.utf8().get_data());
	fflush(stdout);
	OS::get_singleton()->set_exit_code(had_error ? EXIT_FAILURE : EXIT_SUCCESS);
}

#endif // TOOLS_ENABLED
