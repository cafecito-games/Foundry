/**************************************************************************/
/*  gdscript_format.h                                                     */
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

#include "gdscript_parser.h"
#include "gdscript_tokenizer.h"

#include "core/string/ustring.h"
#include "core/templates/hash_map.h"

// Turns GDScript source text into canonical text. The formatter never mutates
// the parser or tokenizer: it runs its own tokenize pass to capture comments
// and the original literal source text, parses for the structural tree, then
// walks the tree to emit canonical output. On a parse error it refuses to
// format (empty output, populated diagnostics).
class GDScriptFormatter {
public:
	struct Result {
		String formatted;
		String error_message;
		int error_line = 0;
		int error_column = 0;
	};

	// Returns OK and fills result.formatted, or a parse error with
	// result.error_* populated and result.formatted left empty.
	Error format(const String &p_source, const String &p_path, Result &r_result);
};

// Internal: walks a parsed tree and emits canonical text. One instance per file.
class GDScriptPrinter {
public:
	struct LiteralToken {
		String source; // Exact original text including quotes / number form.
	};

	GDScriptPrinter(const HashMap<int, GDScriptTokenizer::CommentData> &p_comments,
			const HashMap<uint64_t, LiteralToken> &p_literals);

	String print_tree(const GDScriptParser::ClassNode *p_root, bool p_is_tool);

private:
	const HashMap<int, GDScriptTokenizer::CommentData> &comments;
	const HashMap<uint64_t, LiteralToken> &literals;

	String output;
	int indent_level = 0;
	int last_emitted_line = 0; // For comment / blank-line bookkeeping (Task 2).

	// Buffer helpers.
	void write_indent();
	void write(const String &p_text);
	void newline();

	// Comment reattachment and blank-line normalization.
	//
	// Comments live in a line-keyed map (1-based, the same indexing the parser
	// uses for node start/end lines). `last_emitted_line` is a cursor over the
	// source: it advances past every line whose content has been emitted so each
	// comment is consumed exactly once. `emit_leading_trivia` walks the gap
	// before the next node, emitting full-line comments at the current indent and
	// normalizing the blank lines that separate them.
	String normalize_comment_text(const String &p_raw) const;
	bool is_full_line_comment(int p_line) const;
	void emit_comment_line(int p_line, const String &p_raw_comment);
	void emit_leading_trivia(int p_next_line, int p_required_blanks);
	void emit_trailing_comment(int p_line);
	void flush_block_tail_comments();
	void flush_tail_comments();

	static const GDScriptParser::Node *member_node(const GDScriptParser::ClassNode::Member &p_member);
	static int member_start_line(const GDScriptParser::ClassNode::Member &p_member);
	static bool member_is_definition(const GDScriptParser::ClassNode::Member &p_member);
	static void header_line_range(const GDScriptParser::ClassNode *p_class, int &r_min_line, int &r_max_line);

	// Position key for the literal index: (line << 32) | column.
	static uint64_t pos_key(int p_line, int p_column) {
		return (uint64_t(uint32_t(p_line)) << 32) | uint32_t(p_column);
	}

	// Declarations.
	void print_class(const GDScriptParser::ClassNode *p_class, bool p_is_root, bool p_is_tool);
	void print_class_header(const GDScriptParser::ClassNode *p_class, bool p_is_tool);
	void print_extends_clause(const GDScriptParser::ClassNode *p_class);
	void print_class_body(const GDScriptParser::ClassNode *p_class, bool p_is_root);
	void print_member(const GDScriptParser::ClassNode::Member &p_member);
	void print_function(const GDScriptParser::FunctionNode *p_function);
	void print_variable(const GDScriptParser::VariableNode *p_variable);
	void print_constant(const GDScriptParser::ConstantNode *p_constant);
	void print_signal(const GDScriptParser::SignalNode *p_signal);
	void print_enum(const GDScriptParser::EnumNode *p_enum);
	void print_annotations(const List<GDScriptParser::AnnotationNode *> &p_annotations);
	void print_annotation_inline(const GDScriptParser::AnnotationNode *p_annotation);
	void print_annotation_declaration(const GDScriptParser::AnnotationDeclarationNode *p_declaration);
	void print_parameter(const GDScriptParser::ParameterNode *p_parameter);
	void print_type_parameters(const Vector<GDScriptParser::TypeParameterNode *> &p_params);
	void print_type(const GDScriptParser::TypeNode *p_type);

	// Statements / suites.
	void print_suite(const GDScriptParser::SuiteNode *p_suite);
	void print_statement(const GDScriptParser::Node *p_statement);
	void print_assignment(const GDScriptParser::AssignmentNode *p_assignment);
	void print_if(const GDScriptParser::IfNode *p_if, bool p_is_elif);
	void print_for(const GDScriptParser::ForNode *p_for);
	void print_while(const GDScriptParser::WhileNode *p_while);
	void print_match(const GDScriptParser::MatchNode *p_match);
	void print_match_branch(const GDScriptParser::MatchBranchNode *p_branch);
	void print_pattern(const GDScriptParser::PatternNode *p_pattern);
	void print_return(const GDScriptParser::ReturnNode *p_return);
	void print_assert(const GDScriptParser::AssertNode *p_assert);

	// Expressions.
	void print_expression(const GDScriptParser::ExpressionNode *p_expression);
	// Prints `p_child`, wrapping it in parentheses only when its operator binds
	// looser than `p_min_precedence` requires, so the output re-parses to the same
	// tree (the parser discards the author's original parentheses).
	void print_operand(int p_min_precedence, const GDScriptParser::ExpressionNode *p_child);
	void print_literal(const GDScriptParser::LiteralNode *p_literal);
	void print_binary_op(const GDScriptParser::BinaryOpNode *p_op);
	void print_unary_op(const GDScriptParser::UnaryOpNode *p_op);
	void print_ternary_op(const GDScriptParser::TernaryOpNode *p_op);
	void print_call(const GDScriptParser::CallNode *p_call);
	void print_argument_list(const Vector<GDScriptParser::ExpressionNode *> &p_arguments,
			const Vector<StringName> &p_argument_names, bool p_multiline);
	// Prints a `p_open`...`p_close` delimited list. When `p_multiline` (and the
	// list is non-empty) each item goes on its own indented line with a trailing
	// comma after the last; otherwise items are joined with `, ` on one line.
	// `p_emit_item(i)` writes item `i` (without separators or surrounding spaces).
	template <typename EmitItem>
	void print_delimited_items(const char *p_open, const char *p_close, int p_count, bool p_multiline, EmitItem p_emit_item) {
		write(p_open);
		if (!p_multiline || p_count == 0) {
			for (int i = 0; i < p_count; i++) {
				if (i > 0) {
					write(", ");
				}
				p_emit_item(i);
			}
			write(p_close);
			return;
		}
		indent_level++;
		for (int i = 0; i < p_count; i++) {
			newline();
			write_indent();
			p_emit_item(i);
			write(",");
		}
		indent_level--;
		newline();
		write_indent();
		write(p_close);
	}
	void print_subscript(const GDScriptParser::SubscriptNode *p_subscript);
	void print_cast(const GDScriptParser::CastNode *p_cast);
	void print_await(const GDScriptParser::AwaitNode *p_await);
	void print_array(const GDScriptParser::ArrayNode *p_array);
	void print_dictionary(const GDScriptParser::DictionaryNode *p_dictionary);
	void print_lambda(const GDScriptParser::LambdaNode *p_lambda);
	void print_preload(const GDScriptParser::PreloadNode *p_preload);
	void print_get_node(const GDScriptParser::GetNodeNode *p_get_node);
	void print_type_test(const GDScriptParser::TypeTestNode *p_test);
};

// Headless command driving GDScriptFormatter over files, directories, and stdin
// with CI-friendly exit codes. Registered as the `--gdscript-format` test command
// and dispatched from the test-command entrypoint.
class GDScriptFormatterCLI {
public:
	enum Mode {
		MODE_STDOUT, // Default: write formatted text to stdout.
		MODE_WRITE, // Rewrite each file in place.
		MODE_CHECK, // List files that would change; exit non-zero if any differ.
		MODE_DIFF, // Print a unified diff; exit non-zero if any differ.
	};

	struct Options {
		Mode mode = MODE_STDOUT;
		Vector<String> paths;
		bool read_stdin = false;
	};

	static void run_from_cmdline();

	// Parses the formatter arguments that follow `--gdscript-format`. Exposed for
	// unit testing of the pure argument/mode parsing (no process side effects).
	static Options parse_options(const List<String> &p_cmdline_args);

private:
	static Vector<String> collect_files(const Vector<String> &p_paths, bool &r_had_error);
	static void collect_gd_scripts_recursive(const String &p_dir, Vector<String> &r_files);
	static String make_unified_diff(const String &p_path, const String &p_original, const String &p_formatted);
	static void print_raw(const String &p_text);
};

#endif // TOOLS_ENABLED
