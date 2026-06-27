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

	// A standalone region annotation (`@warning_ignore_start`/`@warning_ignore_restore`).
	// The parser applies these as side effects and discards the nodes, so they are
	// not in the tree; the formatter recovers them from its own tokenize pass and
	// reattaches them by source line through the trivia walker. `end_line` lets the
	// line cursor skip a (rare) multi-line annotation.
	struct StandaloneAnnotation {
		String text; // Canonical reconstructed text, e.g. `@warning_ignore_start("x")`.
		int end_line = 0;
	};

	// A bare string literal used as a comment in a class/top-level body. The parser
	// consumes it as a multi-line comment without creating a member, so it is absent
	// from the tree; the formatter recovers it from the tokenize pass (the exact
	// source text) and reattaches it by source line through the trivia walker.
	struct StringComment {
		String text; // Exact original literal text, e.g. `"""..."""` (possibly multi-line).
		int end_line = 0;
	};

	// Source lines of the head class's header keywords that the AST does not record
	// (`@tool`/`@icon`/`@static_unload` consumed into flags, `namespace`, `import`,
	// and a path-only `extends`). Recovered from the tokenize pass so the header
	// emission can flush leading comments above each sub-line in source order.
	struct HeaderLines {
		int tool = 0;
		int icon = 0;
		int static_unload = 0;
		int name_space = 0;
		int extends = 0;
		Vector<int> imports;
	};

	GDScriptPrinter(const HashMap<int, GDScriptTokenizer::CommentData> &p_comments,
			const HashMap<uint64_t, LiteralToken> &p_literals,
			const HashMap<int, StandaloneAnnotation> &p_standalone_annotations,
			const Vector<String> &p_source_lines, const HeaderLines &p_header_lines,
			const HashMap<int, StringComment> &p_string_comments);

	String print_tree(const GDScriptParser::ClassNode *p_root, bool p_is_tool);

private:
	const HashMap<int, GDScriptTokenizer::CommentData> &comments;
	const HashMap<uint64_t, LiteralToken> &literals;
	const HashMap<int, StandaloneAnnotation> &standalone_annotations;
	// The original source split into lines (1-based: line N is `source_lines[N - 1]`).
	// The comment map has no column, so a full-line comment's original indentation is
	// recovered from the leading whitespace of its source line, which drives whether a
	// tail comment belongs to a block body or to the following, shallower-indented node.
	const Vector<String> &source_lines;
	HeaderLines header_lines;
	const HashMap<int, StringComment> &string_comments;

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
	// Leading-whitespace width of source line `p_line` (1-based) in columns (tabs
	// expand to a 4-wide tab stop, spaces count as one); 0 if out of range. Used for
	// relative-depth comparisons that must work for tab- or space-indented sources.
	int line_indent_columns(int p_line) const;
	bool is_full_line_comment(int p_line) const;
	// Emits full-line comments whose source line falls in `(last_emitted_line,
	// p_until_line)`, each on its own line at the current indent. Used to interleave
	// comments inside a multi-line collection / call between its items.
	void flush_inner_comments(int p_until_line);
	// Emits trivia -- full-line comments and recovered standalone warning
	// annotations -- in `(last_emitted_line, p_until_line)` at the current indent, in
	// source order, advancing the cursor. Used to keep trivia that sits between an
	// annotation/header sub-line and the following node (and between annotations).
	void flush_trivia_until(int p_until_line);
	// True when a full-line comment sits strictly between `p_after` and `p_before`.
	// Lets an *empty* multi-line collection keep an interior comment (`[\n\t# c\n]`).
	bool has_full_line_comment_between(int p_after, int p_before) const;
	// The source line of the `else` keyword, found as the only non-comment,
	// non-blank line between the true block's end and the else block's first
	// statement. The parser records no node for `else`, and the else suite's
	// start_line is its first statement, not the `else` line.
	int find_else_line(int p_true_block_end, int p_else_block_start) const;
	// A trivia line is a full-line comment or a recovered standalone annotation;
	// `emit_trivia_line` emits whichever sits at `p_line` and advances the cursor.
	bool is_trivia_line(int p_line) const;
	void emit_trivia_line(int p_line);
	void emit_comment_line(int p_line, const String &p_raw_comment);
	void emit_leading_trivia(int p_next_line, int p_required_blanks);
	void emit_trailing_comment(int p_line);
	// Appends an inline comment on source line `p_line` to the current output line
	// (no trailing newline), for an inline comment after an item inside a multi-line
	// collection (`1,  # note`). No-op when the line has no unconsumed inline comment.
	void append_inline_comment(int p_line);
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
	void print_trait_use(const GDScriptParser::ClassNode::TraitUse &p_use);
	void print_class_body(const GDScriptParser::ClassNode *p_class, bool p_is_root);
	void print_member(const GDScriptParser::ClassNode::Member &p_member);
	void print_function(const GDScriptParser::FunctionNode *p_function);
	void print_variable(const GDScriptParser::VariableNode *p_variable);
	void print_constant(const GDScriptParser::ConstantNode *p_constant);
	void print_signal(const GDScriptParser::SignalNode *p_signal);
	void print_enum(const GDScriptParser::EnumNode *p_enum);
	// Emits each annotation on its own line, keeping a comment on an annotation line
	// (inline) and a full-line comment between annotations or between the last
	// annotation and the annotated node (when `p_target_line` is the node's line).
	void print_annotations(const List<GDScriptParser::AnnotationNode *> &p_annotations, int p_target_line = 0);
	void print_annotation_inline(const GDScriptParser::AnnotationNode *p_annotation);
	void print_annotation_declaration(const GDScriptParser::AnnotationDeclarationNode *p_declaration);
	void print_parameter(const GDScriptParser::ParameterNode *p_parameter);
	void print_type_parameters(const Vector<GDScriptParser::TypeParameterNode *> &p_params);
	void print_type(const GDScriptParser::TypeNode *p_type);

	// Statements / suites.
	void print_suite(const GDScriptParser::SuiteNode *p_suite);
	void print_statement(const GDScriptParser::Node *p_statement);
	// Prints a single statement with no leading indent or trailing newline, for a
	// single-line lambda body (`func(): return x`). Returns false when the
	// statement cannot be expressed inline (compound blocks, properties), so the
	// caller falls back to a multi-line body.
	bool print_statement_inline(const GDScriptParser::Node *p_statement);
	bool try_print_suite_inline(const GDScriptParser::SuiteNode *p_suite);
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
			const Vector<StringName> &p_argument_names, bool p_multiline, int p_open_line, int p_close_line);
	// Prints a `p_open`...`p_close` delimited list. When `p_multiline` (and the
	// list is non-empty) each item goes on its own indented line with a trailing
	// comma after the last; otherwise items are joined with `, ` on one line.
	// `p_emit_item(i)` writes item `i` (without separators or surrounding spaces).
	//
	// In the multi-line layout, full-line comments authored between items (and
	// before the closing delimiter) are interleaved at the inner indent.
	// `p_open_line`/`p_close_line` are the source lines of the open/close delimiters
	// and `p_item_start_line(i)`/`p_item_end_line(i)` the source span of item `i`,
	// all used only to place those comments; pass 0 to disable comment interleaving.
	template <typename EmitItem, typename ItemStartLine, typename ItemEndLine>
	void print_delimited_items(const char *p_open, const char *p_close, int p_count, bool p_multiline,
			int p_open_line, int p_close_line, EmitItem p_emit_item, ItemStartLine p_item_start_line, ItemEndLine p_item_end_line) {
		write(p_open);
		// An empty collection still needs the multi-line layout when it was authored
		// multi-line with an interior comment, so the comment has a place to live.
		const bool keep_multiline = p_multiline &&
				(p_count > 0 || has_full_line_comment_between(p_open_line, p_close_line));
		if (!keep_multiline) {
			for (int i = 0; i < p_count; i++) {
				if (i > 0) {
					write(", ");
				}
				p_emit_item(i);
			}
			write(p_close);
			return;
		}
		if (p_open_line > last_emitted_line) {
			last_emitted_line = p_open_line;
		}
		indent_level++;
		for (int i = 0; i < p_count; i++) {
			flush_inner_comments(p_item_start_line(i));
			newline();
			write_indent();
			p_emit_item(i);
			write(",");
			append_inline_comment(p_item_end_line(i));
			if (p_item_end_line(i) > last_emitted_line) {
				last_emitted_line = p_item_end_line(i);
			}
		}
		flush_inner_comments(p_close_line);
		indent_level--;
		newline();
		write_indent();
		write(p_close);
		if (p_close_line > last_emitted_line) {
			last_emitted_line = p_close_line;
		}
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

	// Regenerates the golden `expected.gd` next to each `input.gd` fixture by
	// formatting the input with the current formatter. Driven by the
	// `--gdscript-generate-format-tests` command; the optional path argument
	// overrides the default fixture root.
	static void generate_format_tests();

	// Parses the formatter arguments that follow `--gdscript-format`. Exposed for
	// unit testing of the pure argument/mode parsing (no process side effects).
	static Options parse_options(const List<String> &p_cmdline_args);

	// Recursively collects `*.gd` files under `p_paths` (directories recurse, files
	// are taken as-is), skipping hidden and symlinked directories. `r_had_error` is
	// set when a path is missing or a directory cannot be opened or listed. Exposed
	// for unit testing of the filesystem traversal.
	static Vector<String> collect_files(const Vector<String> &p_paths, bool &r_had_error);

private:
	static void collect_gd_scripts_recursive(const String &p_dir, Vector<String> &r_files, bool &r_had_error);
	static String make_unified_diff(const String &p_path, const String &p_original, const String &p_formatted);
	static void print_raw(const String &p_text);
};

#endif // TOOLS_ENABLED
