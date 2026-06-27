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
		const String escaped = name.replace("\\", "\\\\").replace("\"", "\\\"");
		return prefix + "\"" + escaped + "\"";
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

Error GDScriptFormatter::format(const String &p_source, const String &p_path, Result &r_result) {
	// Pass 1: tokenize to capture comments and original literal source text.
	GDScriptTokenizerText tokenizer;
	tokenizer.set_source_code(p_source);

	HashMap<uint64_t, GDScriptPrinter::LiteralToken> literals;
	for (GDScriptTokenizer::Token token = tokenizer.scan();
			token.type != GDScriptTokenizer::Token::TK_EOF;
			token = tokenizer.scan()) {
		if (token.type == GDScriptTokenizer::Token::LITERAL) {
			const uint64_t key = (uint64_t(uint32_t(token.start_line)) << 32) | uint32_t(token.start_column);
			literals[key] = GDScriptPrinter::LiteralToken{ token.source };
		}
		if (token.type == GDScriptTokenizer::Token::ERROR) {
			break; // The parse pass below produces the authoritative diagnostic.
		}
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

	// Pass 3: print.
	GDScriptPrinter printer(comments, literals);
	r_result.formatted = printer.print_tree(parser.get_tree(), parser.is_tool());
	return OK;
}

GDScriptPrinter::GDScriptPrinter(const HashMap<int, GDScriptTokenizer::CommentData> &p_comments,
		const HashMap<uint64_t, LiteralToken> &p_literals) :
		comments(p_comments), literals(p_literals) {
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
		while (line < p_next_line && !is_full_line_comment(line)) {
			blank_run++;
			line++;
		}
		const bool comment_follows = line < p_next_line;

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

		if (comment_follows) {
			HashMap<int, GDScriptTokenizer::CommentData>::ConstIterator found = comments.find(line);
			emit_comment_line(line, found->value.comment);
			line++;
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

// Emits the comments that trail the last statement of a block body. Only the
// comments on the lines immediately following the last emitted line are taken
// (a blank line ends the run): without per-comment column data this is the
// deterministic way to keep a body-trailing comment inside the body at its
// indent rather than letting the next outer member steal it at a shallower
// indent. Comments separated from the body by a blank line are left for the
// enclosing scope's leading flush.
void GDScriptPrinter::flush_block_tail_comments() {
	int line = last_emitted_line + 1;
	while (is_full_line_comment(line)) {
		HashMap<int, GDScriptTokenizer::CommentData>::ConstIterator found = comments.find(line);
		emit_comment_line(line, found->value.comment);
		line++;
	}
}

void GDScriptPrinter::flush_tail_comments() {
	int max_line = 0;
	for (const KeyValue<int, GDScriptTokenizer::CommentData> &entry : comments) {
		if (entry.value.new_line && entry.key > max_line) {
			max_line = entry.key;
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
		write("\"" + p_class->extends_path + "\"");
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
			last_emitted_line = declaration->end_line;
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
	write(":");
	newline();
	last_emitted_line = p_class->start_line;
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
	if (!p_class->namespace_name.is_empty()) {
		write("namespace ");
		write(p_class->namespace_name);
		newline();
	}
	for (const String &import_name : p_class->imports) {
		write("import ");
		write(import_name);
		newline();
	}

	if (p_is_tool) {
		write("@tool");
		newline();
	}
	if (!p_class->icon_path.is_empty()) {
		write("@icon(\"" + p_class->icon_path + "\")");
		newline();
	}
	if (p_class->annotated_static_unload) {
		write("@static_unload");
		newline();
	}
	print_annotations(p_class->annotations);

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
		newline();
	}

	if (p_class->extends_used) {
		if (!modifier_consumed) {
			write(modifier);
			modifier_consumed = true;
		}
		write("extends ");
		print_extends_clause(p_class);
		newline();
	}

	for (int i = 0; i < p_class->used_traits.size(); i++) {
		const GDScriptParser::ClassNode::TraitUse &trait_use = p_class->used_traits[i];
		write("uses ");
		write(trait_use.to_string());
		if (!trait_use.type_arguments.is_empty()) {
			write("[");
			for (int j = 0; j < trait_use.type_arguments.size(); j++) {
				if (j > 0) {
					write(", ");
				}
				print_type(trait_use.type_arguments[j]);
			}
			write("]");
		}
		newline();
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
			if (node->start_line == node->end_line) {
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
			print_annotations(p_member.m_class->annotations);
			print_class(p_member.m_class, false, false);
			break;
		case GDScriptParser::ClassNode::Member::CONSTANT:
			print_annotations(p_member.constant->annotations);
			print_constant(p_member.constant);
			break;
		case GDScriptParser::ClassNode::Member::FUNCTION:
			print_annotations(p_member.function->annotations);
			print_function(p_member.function);
			break;
		case GDScriptParser::ClassNode::Member::SIGNAL:
			print_annotations(p_member.signal->annotations);
			print_signal(p_member.signal);
			break;
		case GDScriptParser::ClassNode::Member::VARIABLE:
			print_annotations(p_member.variable->annotations);
			print_variable(p_member.variable);
			break;
		case GDScriptParser::ClassNode::Member::ENUM:
			print_annotations(p_member.m_enum->annotations);
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

void GDScriptPrinter::print_annotations(const List<GDScriptParser::AnnotationNode *> &p_annotations) {
	for (const GDScriptParser::AnnotationNode *annotation : p_annotations) {
		write_indent();
		print_annotation_inline(annotation);
		newline();
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
	write(":");
	newline();
	if (p_function->is_abstract || p_function->body == nullptr) {
		return; // Abstract methods have no body.
	}
	last_emitted_line = p_function->start_line;
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
	indent_level++;
	if (p_variable->property == GDScriptParser::VariableNode::PROP_INLINE) {
		if (p_variable->getter != nullptr) {
			write_indent();
			write("get:");
			newline();
			last_emitted_line = p_variable->getter->start_line;
			indent_level++;
			print_suite(p_variable->getter->body);
			flush_block_tail_comments();
			indent_level--;
		}
		if (p_variable->setter != nullptr) {
			write_indent();
			write("set(");
			if (p_variable->setter_parameter != nullptr) {
				write(p_variable->setter_parameter->name);
			}
			write("):");
			newline();
			last_emitted_line = p_variable->setter->start_line;
			indent_level++;
			print_suite(p_variable->setter->body);
			flush_block_tail_comments();
			indent_level--;
		}
	} else { // PROP_SETGET
		if (p_variable->getter_pointer != nullptr) {
			write_indent();
			write("get = ");
			write(p_variable->getter_pointer->name);
			newline();
		}
		if (p_variable->setter_pointer != nullptr) {
			write_indent();
			write("set = ");
			write(p_variable->setter_pointer->name);
			newline();
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
	write("{");
	for (int i = 0; i < p_enum->values.size(); i++) {
		if (i > 0) {
			write(", ");
		}
		const GDScriptParser::EnumNode::Value &value = p_enum->values[i];
		write(value.identifier->name);
		if (value.custom_value != nullptr) {
			write(" = ");
			print_expression(value.custom_value);
		}
	}
	write("}");
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
		if (statement->start_line == statement->end_line) {
			emit_trailing_comment(statement->end_line);
		}
		if (statement->end_line > last_emitted_line) {
			last_emitted_line = statement->end_line;
		}
	}
}

void GDScriptPrinter::print_statement(const GDScriptParser::Node *p_statement) {
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
	indent_level++;
	print_suite(p_if->true_block);
	indent_level--;

	if (p_if->false_block == nullptr) {
		return;
	}

	// An `elif` is parsed as an else block holding a single `if` that begins on the same line.
	const bool is_elif_chain = p_if->false_block->statements.size() == 1 &&
			p_if->false_block->statements[0]->type == GDScriptParser::Node::IF &&
			p_if->false_block->start_line == p_if->false_block->statements[0]->start_line;
	if (is_elif_chain) {
		print_if(static_cast<const GDScriptParser::IfNode *>(p_if->false_block->statements[0]), true);
	} else {
		write_indent();
		write("else:");
		newline();
		last_emitted_line = p_if->false_block->start_line;
		indent_level++;
		print_suite(p_if->false_block);
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
	indent_level++;
	print_suite(p_for->loop);
	indent_level--;
}

void GDScriptPrinter::print_while(const GDScriptParser::WhileNode *p_while) {
	write_indent();
	write("while ");
	print_expression(p_while->condition);
	write(":");
	newline();
	last_emitted_line = p_while->start_line;
	indent_level++;
	print_suite(p_while->loop);
	indent_level--;
}

void GDScriptPrinter::print_match(const GDScriptParser::MatchNode *p_match) {
	write_indent();
	write("match ");
	print_expression(p_match->test);
	write(":");
	newline();
	last_emitted_line = p_match->start_line;
	indent_level++;
	for (int i = 0; i < p_match->branches.size(); i++) {
		const GDScriptParser::MatchBranchNode *branch = p_match->branches[i];
		emit_leading_trivia(branch->start_line, 0);
		print_match_branch(branch);
		last_emitted_line = branch->end_line;
	}
	indent_level--;
}

void GDScriptPrinter::print_match_branch(const GDScriptParser::MatchBranchNode *p_branch) {
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
	last_emitted_line = p_branch->start_line;
	indent_level++;
	print_suite(p_branch->block);
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
			write("[");
			for (int i = 0; i < p_pattern->array.size(); i++) {
				if (i > 0) {
					write(", ");
				}
				print_pattern(p_pattern->array[i]);
			}
			write("]");
			break;
		case GDScriptParser::PatternNode::PT_DICTIONARY:
			write("{");
			for (int i = 0; i < p_pattern->dictionary.size(); i++) {
				if (i > 0) {
					write(", ");
				}
				const GDScriptParser::PatternNode::Pair &pair = p_pattern->dictionary[i];
				if (pair.key != nullptr) {
					print_expression(pair.key);
					if (pair.value_pattern != nullptr) {
						write(": ");
						print_pattern(pair.value_pattern);
					}
				} else {
					write("..");
				}
			}
			write("}");
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
	// Fallback for synthesized literals without a backing token.
	write(p_literal->value.operator String());
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
		write("super");
		if (p_call->callee != nullptr) {
			write(".");
		}
	}
	if (p_call->callee != nullptr) {
		// The callee binds as the base of a call, tighter than any operator.
		print_operand(FPREC_CALL, p_call->callee);
	} else if (!String(p_call->function_name).is_empty()) {
		write(p_call->function_name);
	}
	print_argument_list(p_call->arguments, p_call->argument_names, node_was_authored_multiline(p_call));
}

// Prints a parenthesized argument list, honoring the author's single- or
// multi-line layout (see `print_delimited_items`).
void GDScriptPrinter::print_argument_list(const Vector<GDScriptParser::ExpressionNode *> &p_arguments,
		const Vector<StringName> &p_argument_names, bool p_multiline) {
	print_delimited_items("(", ")", p_arguments.size(), p_multiline, [&](int p_index) {
		if (p_index < p_argument_names.size() && !String(p_argument_names[p_index]).is_empty()) {
			write(p_argument_names[p_index]);
			write(" = ");
		}
		print_expression(p_arguments[p_index]);
	});
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
	print_delimited_items("[", "]", p_array->elements.size(), node_was_authored_multiline(p_array), [&](int p_index) {
		print_expression(p_array->elements[p_index]);
	});
}

void GDScriptPrinter::print_dictionary(const GDScriptParser::DictionaryNode *p_dictionary) {
	const bool lua_style = p_dictionary->style == GDScriptParser::DictionaryNode::LUA_TABLE;
	print_delimited_items("{", "}", p_dictionary->elements.size(), node_was_authored_multiline(p_dictionary), [&](int p_index) {
		const GDScriptParser::DictionaryNode::Pair &pair = p_dictionary->elements[p_index];
		print_expression(pair.key);
		write(lua_style ? " = " : ": ");
		print_expression(pair.value);
	});
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
	write(")");
	if (function->return_type != nullptr) {
		write(" -> ");
		print_type(function->return_type);
	}
	write(":");
	newline();
	last_emitted_line = function->start_line;
	indent_level++;
	print_suite(function->body);
	flush_block_tail_comments();
	indent_level--;
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
	size_t read = 0;
	while ((read = fread(buffer, 1, sizeof(buffer), stdin)) > 0) {
		int previous = bytes.size();
		bytes.resize(previous + (int)read);
		memcpy(bytes.ptrw() + previous, buffer, read);
	}
	String result;
	if (!bytes.is_empty()) {
		result.append_utf8((const char *)bytes.ptr(), bytes.size());
	}
	return result;
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

void GDScriptFormatterCLI::collect_gd_scripts_recursive(const String &p_dir, Vector<String> &r_files) {
	Ref<DirAccess> dir = DirAccess::open(p_dir);
	if (dir.is_null()) {
		return;
	}
	dir->list_dir_begin();
	for (String entry = dir->get_next(); !entry.is_empty(); entry = dir->get_next()) {
		if (entry == "." || entry == "..") {
			continue;
		}
		const String full_path = p_dir.path_join(entry);
		if (dir->current_is_dir()) {
			collect_gd_scripts_recursive(full_path, r_files);
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
			collect_gd_scripts_recursive(path, files);
		} else if (FileAccess::exists(path)) {
			files.push_back(path);
		} else {
			fprintf(stderr, "%s: no such file or directory\n", path.utf8().get_data());
			r_had_error = true;
		}
	}
	return files;
}

String GDScriptFormatterCLI::make_unified_diff(const String &p_path, const String &p_original, const String &p_formatted) {
	const Vector<String> a = p_original.split("\n");
	const Vector<String> b = p_formatted.split("\n");
	const int n = a.size();
	const int m = b.size();

	// Longest common subsequence over lines; the table drives a simple unified
	// diff that emits the whole file as a single hunk.
	LocalVector<int> table;
	table.resize((n + 1) * (m + 1));
	for (uint32_t i = 0; i < table.size(); i++) {
		table[i] = 0;
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

	String result;
	result += "--- " + p_path + "\n";
	result += "+++ " + p_path + "\n";
	result += "@@ -1," + itos(n) + " +1," + itos(m) + " @@\n";
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
					Ref<FileAccess> output = FileAccess::open(file, FileAccess::WRITE);
					if (output.is_null()) {
						fprintf(stderr, "%s: could not write file\n", file.utf8().get_data());
						had_error = true;
					} else {
						output->store_string(result.formatted);
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

#endif // TOOLS_ENABLED
