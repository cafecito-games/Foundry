# GDScript Canonical Formatter Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers-extended-cc:subagent-driven-development (recommended) or superpowers-extended-cc:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a `gofmt`-style canonical formatter for this fork's GDScript, exposed as a headless CLI for CI/precommit, reusing the engine's real tokenizer and parser as the single source of truth.

**Architecture:** A new `GDScriptFormatter` core (text in, text out) runs its own tokenize pass (to capture comments and original literal source text), then a separate `parse()` pass, then a recursive `GDScriptPrinter` walk that emits canonical text with structural indentation, interleaving comments by line number. A `--gdscript-format` command (registered the same way as `--gdscript-generate-tests`) drives it over files/dirs/stdin with `--check`/`--write`/`--diff` modes and CI exit codes.

**Tech Stack:** C++ (Godot engine module conventions, tabs/width-4, 120 col), doctest tests, the existing `.gd`/golden fixture infrastructure under `modules/gdscript/tests/scripts/`.

**Spec:** `docs/superpowers/specs/2026-06-27-gdscript-canonical-formatter-design.md`

**Key facts established during research (use verbatim):**
- Whole feature lives under `#ifdef TOOLS_ENABLED` — the tokenizer only records comments under `TOOLS_ENABLED` (`gdscript_tokenizer.cpp:1237,1368`). Editor and `tests=yes` builds satisfy this.
- `modules/gdscript/SCsub` globs `*.cpp`, so `gdscript_format.cpp` is compiled automatically. No SCsub edit needed for the core.
- Tokenizer API: `GDScriptTokenizerText`, `void set_source_code(const String &)`, `Token scan()`, `const HashMap<int, CommentData> &get_comments() const`. `Token` has `Type type; Variant literal; String source; int start_line, end_line, start_column, end_column;`.
- `CommentData { String comment; bool new_line; }` — `new_line == true` means full-line (or after indentation only); `false` means inline (after code). `comment` includes the leading `#`.
- Parser API: `Error parse(const String &p_source_code, const String &p_script_path, bool p_for_completion, bool p_parse_body = true)`, `ClassNode *get_tree() const`, `const List<ParserError> &get_errors() const`. `ParserError { String message; int line; int column; }`.
- `LiteralNode` keeps only `Variant value` (no quote/number source) — recover original text from the token index built in the tokenize pass.

---

## File Structure

- `modules/gdscript/gdscript_format.h` — public `GDScriptFormatter` API + `GDScriptPrinter` declaration + result/options structs.
- `modules/gdscript/gdscript_format.cpp` — pipeline, token index, printer (all node print routines), comment interleaving, style normalization.
- `modules/gdscript/register_types.cpp` — register the `--gdscript-format` and `--gdscript-generate-format-tests` commands (mirror `generate_gdscript_tests`).
- `main/main.cpp` — route `--gdscript-format` into the test-command dispatch (one-line addition next to `is_test_command`).
- `modules/gdscript/tests/test_format.h` — doctest suite: golden fixtures, idempotency, refuse-on-error, semantic (token-stream) preservation.
- `modules/gdscript/tests/scripts/format/**` — `input.gd` / `expected.gd` fixture pairs.
- `.pre-commit-config.yaml` — local hook running `--gdscript-format --check` on staged `*.gd`.
- CI workflow (`.github/workflows/*`) — a `--check` gate step.

---

## Task 1: Formatter pipeline + printer foundation

**Goal:** A `GDScriptFormatter` that turns source text into canonical text (default spacing, structural indentation), refusing to format on parse error, with full node coverage and faithful literals.

**Files:**
- Create: `modules/gdscript/gdscript_format.h`
- Create: `modules/gdscript/gdscript_format.cpp`
- Test: `modules/gdscript/tests/test_format.h` (foundation cases only; expanded in Task 5)

**Acceptance Criteria:**
- [ ] `GDScriptFormatter::format(source, path, &out_result)` returns success + canonical text, or failure with `file:line:col` diagnostics on parse error (and leaves no partial output).
- [ ] Indentation is derived from tree depth using tabs; input whitespace never influences it.
- [ ] Every `Node::Type` enum value has a print routine; an unhandled type triggers `ERR_FAIL`/`DEV_ASSERT` in dev builds (no silent drop).
- [ ] String and numeric literals reproduce from `Token.source` via the literal index (no Variant round-trip).
- [ ] Fork syntax prints structurally: `final`/`abstract` modifiers, generics `[T]`/`[T: Bound]`, trait declarations/use, `AsyncCallable`/`async Callable` signature types.

**Verify:** `./bin/godot.linuxbsd.editor.dev.x86_64 --headless --test --test-suite="*Format*"` → foundation cases PASS.

**Steps:**

- [ ] **Step 1: Write the failing foundation test**

Add `modules/gdscript/tests/test_format.h`:

```cpp
#ifndef TEST_FORMAT_H
#define TEST_FORMAT_H

#ifdef TOOLS_ENABLED

#include "../gdscript_format.h"

#include "tests/test_macros.h"

namespace GDScriptTests {

static String format_or_fail(const String &p_source) {
	GDScriptFormatter formatter;
	GDScriptFormatter::Result result;
	Error err = formatter.format(p_source, "test.gd", result);
	CHECK_MESSAGE(err == OK, "Source must format without parse errors.");
	return result.formatted;
}

TEST_SUITE("[Modules][GDScript][Format]") {
	TEST_CASE("[Format] Reindents structurally with tabs") {
		String source = "func f():\n        return     1+2\n";
		String expected = "func f():\n\treturn 1 + 2\n";
		CHECK_EQ(format_or_fail(source), expected);
	}

	TEST_CASE("[Format] Refuses to format on parse error") {
		GDScriptFormatter formatter;
		GDScriptFormatter::Result result;
		Error err = formatter.format("func (:\n", "bad.gd", result);
		CHECK(err != OK);
		CHECK(result.formatted.is_empty());
		CHECK(result.error_line > 0);
	}

	TEST_CASE("[Format] Preserves string contents and normalizes to double quotes") {
		// Quote normalization itself lands in Task 3; here we assert the
		// literal text round-trips via the token index rather than the Variant.
		CHECK_EQ(format_or_fail("var s = \"a\\tb\"\n"), "var s = \"a\\tb\"\n");
	}
}

} // namespace GDScriptTests

#endif // TOOLS_ENABLED
#endif // TEST_FORMAT_H
```

- [ ] **Step 2: Run the test to confirm it fails to compile/link**

Run: `./bin/godot.linuxbsd.editor.dev.x86_64 --headless --test --test-suite="*Format*"`
Expected: build failure — `gdscript_format.h` does not exist yet.

- [ ] **Step 3: Declare the formatter API**

Create `modules/gdscript/gdscript_format.h`:

```cpp
#ifndef GDSCRIPT_FORMAT_H
#define GDSCRIPT_FORMAT_H

#ifdef TOOLS_ENABLED

#include "gdscript_parser.h"
#include "gdscript_tokenizer.h"

#include "core/string/ustring.h"
#include "core/templates/hash_map.h"

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
		String source; // exact original text incl. quotes / number form
	};

	GDScriptPrinter(const HashMap<int, GDScriptTokenizer::CommentData> &p_comments,
			const HashMap<uint64_t, LiteralToken> &p_literals);

	String print_tree(const GDScriptParser::ClassNode *p_root);

private:
	const HashMap<int, GDScriptTokenizer::CommentData> &comments;
	const HashMap<uint64_t, LiteralToken> &literals;

	String output;
	int indent_level = 0;
	int last_emitted_line = 0; // for comment/blank-line bookkeeping (Task 2)

	// Buffer helpers.
	void write_indent();
	void write(const String &p_text);
	void newline();

	// Position key for the literal index: (line << 32) | column.
	static uint64_t pos_key(int p_line, int p_column) {
		return (uint64_t(uint32_t(p_line)) << 32) | uint32_t(p_column);
	}

	// Declarations.
	void print_class(const GDScriptParser::ClassNode *p_class, bool p_is_root);
	void print_class_body(const GDScriptParser::ClassNode *p_class);
	void print_member(const GDScriptParser::ClassNode::Member &p_member);
	void print_function(const GDScriptParser::FunctionNode *p_function);
	void print_variable(const GDScriptParser::VariableNode *p_variable);
	void print_constant(const GDScriptParser::ConstantNode *p_constant);
	void print_signal(const GDScriptParser::SignalNode *p_signal);
	void print_enum(const GDScriptParser::EnumNode *p_enum);
	void print_annotation(const GDScriptParser::AnnotationNode *p_annotation);
	void print_parameter(const GDScriptParser::ParameterNode *p_parameter);
	void print_type_parameters(const Vector<GDScriptParser::TypeParameterNode *> &p_params);
	void print_type(const GDScriptParser::TypeNode *p_type);

	// Statements / suites.
	void print_suite(const GDScriptParser::SuiteNode *p_suite);
	void print_statement(const GDScriptParser::Node *p_statement);

	// Expressions.
	void print_expression(const GDScriptParser::ExpressionNode *p_expression);
	void print_literal(const GDScriptParser::LiteralNode *p_literal);
	void print_binary_op(const GDScriptParser::BinaryOpNode *p_op);
	void print_call(const GDScriptParser::CallNode *p_call);
	// ... one declaration per remaining expression node (see Step 6 switch).
};

#endif // TOOLS_ENABLED
#endif // GDSCRIPT_FORMAT_H
```

- [ ] **Step 4: Implement the pipeline (tokenize pass → parse pass → print)**

Create `modules/gdscript/gdscript_format.cpp` starting with the driver:

```cpp
#include "gdscript_format.h"

#ifdef TOOLS_ENABLED

Error GDScriptFormatter::format(const String &p_source, const String &p_path, Result &r_result) {
	// Pass 1: tokenize to capture comments and original literal source text.
	GDScriptTokenizerText tokenizer;
	tokenizer.set_source_code(p_source);

	HashMap<uint64_t, GDScriptPrinter::LiteralToken> literals;
	for (GDScriptTokenizer::Token token = tokenizer.scan();
			token.type != GDScriptTokenizer::Token::TK_EOF;
			token = tokenizer.scan()) {
		if (token.type == GDScriptTokenizer::Token::LITERAL) {
			uint64_t key = (uint64_t(uint32_t(token.start_line)) << 32) | uint32_t(token.start_column);
			literals[key] = GDScriptPrinter::LiteralToken{ token.source };
		}
		if (token.type == GDScriptTokenizer::Token::ERROR) {
			break; // parse pass below produces the authoritative diagnostic
		}
	}
	const HashMap<int, GDScriptTokenizer::CommentData> comments = tokenizer.get_comments();

	// Pass 2: parse for the structural tree (parser re-tokenizes internally;
	// that is fine and keeps the parser unmodified).
	GDScriptParser parser;
	Error parse_err = parser.parse(p_source, p_path, /*for_completion=*/false);
	if (parse_err != OK || !parser.get_errors().is_empty()) {
		const GDScriptParser::ParserError &first = parser.get_errors().front()->get();
		r_result.error_message = first.message;
		r_result.error_line = first.line;
		r_result.error_column = first.column;
		r_result.formatted = String();
		return ERR_PARSE_ERROR;
	}

	// Pass 3: print.
	GDScriptPrinter printer(comments, literals);
	r_result.formatted = printer.print_tree(parser.get_tree());
	return OK;
}
```

> Note: confirm the EOF/ERROR token enum spelling against `gdscript_tokenizer.h` (`Token::Type`) while implementing; adjust `TK_EOF`/`EOF` to the actual identifier.

- [ ] **Step 5: Implement the buffer + indentation helpers**

```cpp
GDScriptPrinter::GDScriptPrinter(const HashMap<int, GDScriptTokenizer::CommentData> &p_comments,
		const HashMap<uint64_t, LiteralToken> &p_literals) :
		comments(p_comments), literals(p_literals) {}

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

String GDScriptPrinter::print_tree(const GDScriptParser::ClassNode *p_root) {
	print_class(p_root, /*is_root=*/true);
	// Guarantee a single trailing newline.
	while (output.ends_with("\n\n")) {
		output = output.substr(0, output.length() - 1);
	}
	if (!output.ends_with("\n")) {
		output += "\n";
	}
	return output;
}
```

- [ ] **Step 6: Implement the statement/expression dispatch with full node coverage**

Use an explicit switch over `Node::Type` so a new node type fails loudly. Fill in every case (field references from the spec research):

```cpp
void GDScriptPrinter::print_statement(const GDScriptParser::Node *p_statement) {
	using Node = GDScriptParser::Node;
	switch (p_statement->type) {
		case Node::VARIABLE: print_variable(static_cast<const GDScriptParser::VariableNode *>(p_statement)); break;
		case Node::CONSTANT: print_constant(static_cast<const GDScriptParser::ConstantNode *>(p_statement)); break;
		case Node::ASSIGNMENT: /* assignee <op>= assigned_value */ break;
		case Node::IF: /* if/elif/else from IfNode condition/true_block/false_block */ break;
		case Node::FOR: /* for variable[: type] in list: loop */ break;
		case Node::WHILE: /* while condition: loop */ break;
		case Node::MATCH: /* match test: branches (patterns + guard_body + block) */ break;
		case Node::RETURN: /* return [return_value] (void_return → bare) */ break;
		case Node::ASSERT: /* assert(condition[, message]) */ break;
		case Node::BREAK: write_indent(); write("break"); newline(); break;
		case Node::CONTINUE: write_indent(); write("continue"); newline(); break;
		case Node::PASS: write_indent(); write("pass"); newline(); break;
		case Node::BREAKPOINT: write_indent(); write("breakpoint"); newline(); break;
		default: // Expression-statement fallthrough.
			write_indent();
			print_expression(static_cast<const GDScriptParser::ExpressionNode *>(p_statement));
			newline();
			break;
	}
}

void GDScriptPrinter::print_expression(const GDScriptParser::ExpressionNode *p_expression) {
	using Node = GDScriptParser::Node;
	switch (p_expression->type) {
		case Node::LITERAL: print_literal(static_cast<const GDScriptParser::LiteralNode *>(p_expression)); break;
		case Node::IDENTIFIER: write(static_cast<const GDScriptParser::IdentifierNode *>(p_expression)->name); break;
		case Node::BINARY_OPERATOR: print_binary_op(static_cast<const GDScriptParser::BinaryOpNode *>(p_expression)); break;
		case Node::UNARY_OPERATOR: /* op operand (no space for unary) */ break;
		case Node::TERNARY_OPERATOR: /* true_expr if condition else false_expr */ break;
		case Node::CALL: print_call(static_cast<const GDScriptParser::CallNode *>(p_expression)); break;
		case Node::SUBSCRIPT: /* base[index] or base.attribute; type_arguments → [T, U] */ break;
		case Node::CAST: /* operand as cast_type */ break;
		case Node::AWAIT: /* await to_await */ break;
		case Node::ARRAY: /* [elements] */ break;
		case Node::DICTIONARY: /* {key: value} or Lua style per DictionaryNode::style */ break;
		case Node::LAMBDA: /* func(params)[ -> ret]: body, named via function->identifier */ break;
		case Node::PRELOAD: /* preload(path) */ break;
		case Node::SELF: write("self"); break;
		case Node::GET_NODE: /* $path or get_node(...) per use_dollar/full_path */ break;
		case Node::TYPE_TEST: /* operand is test_type */ break;
		case Node::TYPE: print_type(static_cast<const GDScriptParser::TypeNode *>(p_expression)); break;
		default:
			ERR_FAIL_MSG("GDScriptPrinter: unhandled expression node type " + itos(p_expression->type));
	}
}
```

- [ ] **Step 7: Implement literals from the token index (faithful round-trip)**

```cpp
void GDScriptPrinter::print_literal(const GDScriptParser::LiteralNode *p_literal) {
	uint64_t key = pos_key(p_literal->start_line, p_literal->start_column);
	HashMap<uint64_t, LiteralToken>::ConstIterator found = literals.find(key);
	if (found) {
		write(found->value.source); // exact original text (quotes/number form)
		return;
	}
	// Fallback (synthesized literals without a backing token): stringify the value.
	write(p_literal->value.operator String());
}
```

- [ ] **Step 8: Implement a representative non-trivial expression (binary op) and call**

```cpp
void GDScriptPrinter::print_binary_op(const GDScriptParser::BinaryOpNode *p_op) {
	print_expression(p_op->left_operand);
	write(" ");
	write(binary_operator_token(p_op->operation)); // local helper mapping OpType → "+", "and", etc.
	write(" ");
	print_expression(p_op->right_operand);
}

void GDScriptPrinter::print_call(const GDScriptParser::CallNode *p_call) {
	if (p_call->is_super) {
		write("super");
		if (p_call->callee) { write("."); }
	}
	if (p_call->callee) {
		print_expression(p_call->callee);
	} else if (!p_call->function_name.operator String().is_empty()) {
		write(p_call->function_name);
	}
	write("(");
	for (int i = 0; i < p_call->arguments.size(); i++) {
		if (i > 0) { write(", "); }
		// Named-argument syntax (fork feature): name = value when argument_names[i] set.
		if (i < p_call->argument_names.size() && !p_call->argument_names[i].operator String().is_empty()) {
			write(p_call->argument_names[i]);
			write(" = ");
		}
		print_expression(p_call->arguments[i]);
	}
	write(")");
}
```

- [ ] **Step 9: Implement class/function/member/suite printing (structure + fork modifiers)**

```cpp
void GDScriptPrinter::print_class(const GDScriptParser::ClassNode *p_class, bool p_is_root) {
	if (!p_is_root) {
		write_indent();
		if (p_class->is_abstract) { write("abstract "); }
		if (p_class->is_final) { write("final "); }
		write(p_class->is_trait ? "trait " : "class ");
		write(p_class->identifier->name);
		print_type_parameters(p_class->type_parameters); // [T], [T: Bound]
		// extends chain (p_class->extends) + extends_type_arguments, if present.
		write(":");
		newline();
		indent_level++;
		print_class_body(p_class);
		indent_level--;
		return;
	}
	// Root: class_name / extends header, used_traits, then members.
	print_class_body(p_class);
}

void GDScriptPrinter::print_function(const GDScriptParser::FunctionNode *p_function) {
	write_indent();
	if (p_function->is_abstract) { write("abstract "); }
	if (p_function->is_final) { write("final "); }
	if (p_function->is_static) { write("static "); }
	if (p_function->is_declared_async) { write("async "); }
	write("func ");
	write(p_function->identifier->name);
	print_type_parameters(p_function->type_parameters);
	write("(");
	for (int i = 0; i < p_function->parameters.size(); i++) {
		if (i > 0) { write(", "); }
		print_parameter(p_function->parameters[i]);
	}
	// rest_parameter (varargs) handled here if present.
	write(")");
	if (p_function->return_type) {
		write(" -> ");
		print_type(p_function->return_type);
	}
	write(":");
	newline();
	if (p_function->is_abstract || p_function->body == nullptr) {
		return; // abstract methods have no body
	}
	indent_level++;
	print_suite(p_function->body);
	indent_level--;
}

void GDScriptPrinter::print_suite(const GDScriptParser::SuiteNode *p_suite) {
	if (p_suite->statements.is_empty()) {
		write_indent();
		write("pass");
		newline();
		return;
	}
	for (int i = 0; i < p_suite->statements.size(); i++) {
		print_statement(p_suite->statements[i]);
	}
}
```

> Fill in `print_member` (switch on `ClassNode::Member::type`), `print_variable`/`print_constant` (identifier, `: type`, ` = ` initializer; `VariableNode::property` getter/setter forms), `print_signal`, `print_enum`, `print_annotation` (emit on its own line before the member; `@name(args)`), `print_parameter` (`name: type` / `name := value` / `name = value` via `infer_datatype`/`initializer`), `print_type_parameters` (`[A, B: Bound]`), and `print_type` (`type_chain` joined by `.`, `container_types` as `[...]`, Callable/`async` signature via `signature_parameter_types`/`signature_return_type`/`signature_is_async`). Each uses the fields enumerated in the spec research; show the real code when implementing, no placeholders in the final source.

- [ ] **Step 10: Run the foundation tests to green**

Run: `./bin/godot.linuxbsd.editor.dev.x86_64 --headless --test --test-suite="*Format*"`
Expected: the three foundation cases PASS.

- [ ] **Step 11: Commit**

```bash
git add modules/gdscript/gdscript_format.h modules/gdscript/gdscript_format.cpp modules/gdscript/tests/test_format.h
git commit -m "feat(gdscript): add canonical formatter core and printer"
```

---

## Task 2: Comment reattachment + blank-line normalization

**Goal:** Interleave comments (from the line-keyed map) into output at the right indent, and normalize blank lines per canonical style.

**Files:**
- Modify: `modules/gdscript/gdscript_format.cpp`
- Modify: `modules/gdscript/gdscript_format.h` (helpers: `flush_comments_before`, `emit_trailing_comment`, `blank_line_bookkeeping`)
- Test: `modules/gdscript/tests/test_format.h`

**Acceptance Criteria:**
- [ ] Full-line comments (`CommentData.new_line == true`) are emitted before the next node at that node's indent.
- [ ] Inline comments (`new_line == false`) are emitted as ` # ...` (two spaces) trailing the code on their source line.
- [ ] Comment-only files and comments after the last statement are preserved.
- [ ] Runs of blank lines collapse to at most one inside blocks; two blank lines between top-level definitions and one between methods are enforced regardless of input.
- [ ] Exactly one space after `#` is enforced (`#foo` → `# foo`), without altering shebang-style `#!` first lines or `##` doc comments.

**Verify:** `./bin/godot.linuxbsd.editor.dev.x86_64 --headless --test --test-suite="*Format*"` → comment/blank-line cases PASS.

**Steps:**

- [ ] **Step 1: Write failing comment/blank-line tests**

```cpp
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
```

- [ ] **Step 2: Run to confirm failure**

Run: `./bin/godot.linuxbsd.editor.dev.x86_64 --headless --test --test-suite="*Format*"`
Expected: the four new cases FAIL.

- [ ] **Step 3: Add a normalized comment lookup**

Comments are keyed by line. Add helpers that emit and consume comments by line, tracking a cursor so each comment is emitted once:

```cpp
String GDScriptPrinter::normalize_comment_text(const String &p_raw) {
	// p_raw includes the leading '#'. Preserve '##' doc comments and '#!' first line.
	if (p_raw.begins_with("##") || p_raw.begins_with("#!")) {
		return p_raw;
	}
	String body = p_raw.substr(1).strip_edges(/*left=*/true, /*right=*/false);
	return "# " + body;
}

void GDScriptPrinter::flush_comments_before(int p_line) {
	// Emit any full-line comments on lines (last_emitted_line, p_line).
	for (int line = last_emitted_line + 1; line < p_line; line++) {
		HashMap<int, GDScriptTokenizer::CommentData>::ConstIterator c = comments.find(line);
		if (c && c->value.new_line) {
			write_indent();
			write(normalize_comment_text(c->value.comment));
			newline();
		}
		// A blank source line with no comment → candidate single blank (see Step 5).
	}
}

void GDScriptPrinter::emit_trailing_comment(int p_line) {
	HashMap<int, GDScriptTokenizer::CommentData>::ConstIterator c = comments.find(p_line);
	if (c && !c->value.new_line) {
		write("  ");
		write(normalize_comment_text(c->value.comment));
	}
}
```

- [ ] **Step 4: Hook comment flushing into statement/member emission**

Before emitting a statement/member, call `flush_comments_before(node->start_line)`; after emitting its line (before `newline()`), call `emit_trailing_comment(node->end_line)`; update `last_emitted_line = node->end_line`. Wire these into `print_statement`, `print_member`, and the suite/class-body loops. After the final node, flush any remaining comments (comment-only tails) via `flush_comments_before(INT_MAX)` analogue that runs to the max comment line.

- [ ] **Step 5: Blank-line normalization**

Track whether the input had a gap (a source line in the flushed range with neither code nor comment) to decide an intentional single blank; enforce structural blanks in the class-body/suite loops:

```cpp
// In print_class_body between members:
if (index > 0) {
	int required = at_top_level ? 2 : 1;
	for (int i = 0; i < required; i++) { newline(); }
}
// Inside print_suite between statements: at most one blank, only if the input had one.
```

Collapse trailing duplicate blanks in `print_tree` (already trims to one trailing newline).

- [ ] **Step 6: Run tests to green**

Run: `./bin/godot.linuxbsd.editor.dev.x86_64 --headless --test --test-suite="*Format*"`
Expected: all comment/blank-line cases PASS.

- [ ] **Step 7: Commit**

```bash
git add modules/gdscript/gdscript_format.cpp modules/gdscript/gdscript_format.h modules/gdscript/tests/test_format.h
git commit -m "feat(gdscript): interleave comments and normalize blank lines in formatter"
```

---

## Task 3: Canonical style normalization

**Goal:** Apply the canonical-style decisions that are not purely structural: double-quote normalization, trailing commas, redundant-paren policy, numeric casing, and fork-syntax spacing nuances.

**Files:**
- Modify: `modules/gdscript/gdscript_format.cpp`
- Test: `modules/gdscript/tests/test_format.h`

**Acceptance Criteria:**
- [ ] Single-quoted strings normalize to double quotes unless the content contains an unescaped `"` (then leave single). Escapes and contents are otherwise untouched.
- [ ] Multi-line array/dictionary/argument literals get a trailing comma on the last element; single-line ones do not.
- [ ] Numeric literals normalize casing: lowercase `0x`/`0b`/exponent `e`, hex digits uppercase (`0xFF`), preserve underscores.
- [ ] Redundant parentheses policy: preserve as written in v1 (conservative); add a focused test locking this behavior so a future change is deliberate.
- [ ] Fork syntax spacing: `[T, U]` with `, ` and no inner padding; `[T: Bound]`; `AsyncCallable[[int, String], bool]` / `async func(...) -> ...` signatures spaced canonically; trait `uses`/declaration spacing.
- [ ] No auto-wrap/auto-join: existing author line breaks within an expression are preserved.

**Verify:** `./bin/godot.linuxbsd.editor.dev.x86_64 --headless --test --test-suite="*Format*"` → style cases PASS.

**Steps:**

- [ ] **Step 1: Write failing style tests**

```cpp
TEST_CASE("[Format] Normalizes single quotes to double") {
	CHECK_EQ(format_or_fail("var s = 'hi'\n"), "var s = \"hi\"\n");
}
TEST_CASE("[Format] Keeps single quotes when content has a double quote") {
	CHECK_EQ(format_or_fail("var s = 'say \"hi\"'\n"), "var s = 'say \"hi\"'\n");
}
TEST_CASE("[Format] Normalizes hex literal casing") {
	CHECK_EQ(format_or_fail("var n = 0Xff\n"), "var n = 0xFF\n");
}
TEST_CASE("[Format] Single-line array has no trailing comma") {
	CHECK_EQ(format_or_fail("var a = [1, 2, 3]\n"), "var a = [1, 2, 3]\n");
}
```

- [ ] **Step 2: Run to confirm failure**

Run: `./bin/godot.linuxbsd.editor.dev.x86_64 --headless --test --test-suite="*Format*"`
Expected: style cases FAIL.

- [ ] **Step 3: Implement quote normalization in `print_literal`**

When the backing `Token.source` is a string literal, post-process its delimiter:

```cpp
String GDScriptPrinter::canonicalize_string_literal(const String &p_source) {
	// p_source is the exact token incl. its delimiter(s). Handle prefixes
	// (r"" raw, """ triple) by only touching the simple single→double case.
	if (p_source.length() >= 2 && p_source[0] == '\'' && !is_triple_or_raw(p_source)) {
		String inner = p_source.substr(1, p_source.length() - 2);
		if (inner.find("\"") == -1) {
			return "\"" + inner + "\"";
		}
	}
	return p_source;
}
```

Call it from `print_literal` for string tokens; numeric normalization (`canonicalize_number_literal`) handles `0x`/`0b`/`e` casing.

- [ ] **Step 4: Implement trailing-comma rule for collections/arguments**

In array/dictionary/call/parameter printing, detect multi-line by comparing the node's `start_line` and `end_line`; when multi-line, emit each element on its own indented line with a trailing comma after the last. When single-line, keep `, ` separators and no trailing comma. (No new wrapping: only respect the author's existing multi-line layout.)

- [ ] **Step 5: Lock fork-syntax spacing**

Implement `print_type_parameters`, generic `SubscriptNode::type_arguments`, and the `TypeNode` Callable/`async` signature emission with canonical spacing, plus trait declaration/use spacing. Add tests:

```cpp
TEST_CASE("[Format] Generic type parameters spaced canonically") {
	CHECK_EQ(format_or_fail("class Box[T,U]:\n\tpass\n"), "class Box[T, U]:\n\tpass\n");
}
TEST_CASE("[Format] AsyncCallable signature spacing") {
	CHECK_EQ(format_or_fail("var f: AsyncCallable[[int,String],bool]\n"),
			"var f: AsyncCallable[[int, String], bool]\n");
}
```

- [ ] **Step 6: Run tests to green**

Run: `./bin/godot.linuxbsd.editor.dev.x86_64 --headless --test --test-suite="*Format*"`
Expected: all style cases PASS.

- [ ] **Step 7: Commit**

```bash
git add modules/gdscript/gdscript_format.cpp modules/gdscript/tests/test_format.h
git commit -m "feat(gdscript): apply canonical style normalization in formatter"
```

---

## Task 4: Wire `--gdscript-format` CLI with check/write/diff modes

**Goal:** A headless command driving the formatter over files/dirs/stdin with CI-friendly exit codes, registered the same way as `--gdscript-generate-tests`.

**Files:**
- Modify: `main/main.cpp` (route `--gdscript-format` into the existing test-command dispatch)
- Modify: `modules/gdscript/register_types.cpp` (register the command handler)
- Create: handler `gdscript_format_command()` (in `register_types.cpp` or a small `gdscript_format.cpp` free function)

**Acceptance Criteria:**
- [ ] `godot --headless --gdscript-format [MODE] [paths...]`; directories recurse for `*.gd`; no path or `-` reads stdin and writes stdout.
- [ ] Default mode writes formatted text to stdout; `--write`/`-w` rewrites in place; `--check` lists files that would change and exits 1 if any differ; `--diff`/`-d` prints a unified diff and exits 1 if any differ.
- [ ] Parse errors print `path:line:col: message` to stderr, skip that file, and force a non-zero overall exit.
- [ ] An all-formatted `--check` run exits 0.

**Verify:**
```
echo 'func f():
        return 1' | ./bin/godot.linuxbsd.editor.dev.x86_64 --headless --gdscript-format -
```
→ prints canonical text, exit 0. And `--gdscript-format --check <dirty-dir>` → exit 1.

**Steps:**

- [ ] **Step 1: Route the flag in main.cpp**

In `main/main.cpp` next to `is_test_command` (around line 968), add:

```cpp
const bool is_format_command = strcmp(argv[x], "--gdscript-format") == 0;
```

and include it in the dispatch condition:

```cpp
if (is_test || is_test_command || is_format_command) {
```

(The handler runs under `test_setup()`/`test_cleanup()` like `--gdscript-generate-tests`, so the GDScript language is initialized and the process shuts down cleanly.)

- [ ] **Step 2: Register the command handler**

In `modules/gdscript/register_types.cpp`, near `generate_gdscript_tests` (line ~245), add under `#ifdef TOOLS_ENABLED` + `TESTS_ENABLED`:

```cpp
void gdscript_format_command() {
	GDScriptFormatterCLI::run_from_cmdline();
}
// ...
REGISTER_TEST_COMMAND("--gdscript-format", &gdscript_format_command);
```

Add `#include "gdscript_format.h"` to that file.

- [ ] **Step 3: Implement the CLI driver**

Add to `gdscript_format.{h,cpp}` a `GDScriptFormatterCLI` with `static void run_from_cmdline()` that:
1. Reads `OS::get_singleton()->get_cmdline_args()`.
2. Parses mode flags (`--write`/`-w`, `--check`, `--diff`/`-d`) and collects path args after `--gdscript-format`.
3. For `-`/no path: read stdin, format, write stdout.
4. For each path: if directory, recurse collecting `*.gd`; format each file.
5. Apply the mode (stdout / in-place write / list-on-diff / unified diff).
6. On parse error: print `path:line:col: message` to stderr; mark failure.
7. `OS::get_singleton()->set_exit_code(any_diff_or_error ? EXIT_FAILURE : EXIT_SUCCESS)`.

```cpp
void GDScriptFormatterCLI::run_from_cmdline() {
	Options options = parse_options(OS::get_singleton()->get_cmdline_args());
	bool needs_change = false;
	bool had_error = false;

	for (const String &file : collect_files(options.paths)) {
		String source = FileAccess::get_file_as_string(file);
		GDScriptFormatter formatter;
		GDScriptFormatter::Result result;
		if (formatter.format(source, file, result) != OK) {
			fprintf(stderr, "%s:%d:%d: %s\n", file.utf8().get_data(),
					result.error_line, result.error_column, result.error_message.utf8().get_data());
			had_error = true;
			continue;
		}
		if (result.formatted != source) {
			needs_change = true;
			apply_mode(options.mode, file, source, result.formatted);
		} else if (options.mode == MODE_STDOUT) {
			print_raw(result.formatted);
		}
	}
	bool failure = had_error || ((options.mode == MODE_CHECK || options.mode == MODE_DIFF) && needs_change);
	OS::get_singleton()->set_exit_code(failure ? EXIT_FAILURE : EXIT_SUCCESS);
}
```

- [ ] **Step 4: Manual verification**

Run the two `Verify` commands above on a hand-made clean file and a dirty file; confirm stdout content and exit codes (`echo $?`).

- [ ] **Step 5: Commit**

```bash
git add main/main.cpp modules/gdscript/register_types.cpp modules/gdscript/gdscript_format.h modules/gdscript/gdscript_format.cpp
git commit -m "feat(gdscript): add --gdscript-format CLI with check/write/diff modes"
```

---

## Task 5: Test harness, fixtures, and property/semantic tests

**Goal:** Golden-file fixtures plus corpus-wide property tests proving idempotency, refuse-on-error, and semantic (token-stream) preservation, with a regen command for intentional style changes.

**Files:**
- Modify: `modules/gdscript/tests/test_format.h`
- Create: `modules/gdscript/tests/scripts/format/**` (`input.gd`/`expected.gd` pairs)
- Modify: `modules/gdscript/register_types.cpp` (add `--gdscript-generate-format-tests`)

**Acceptance Criteria:**
- [ ] A fixture runner formats each `format/**/input.gd` and compares byte-for-byte to its `expected.gd`.
- [ ] Fixture coverage: basics (spacing/indent/blanks), comments, strings/quotes, collections/trailing commas, and fork syntax (`final`, `abstract`, generics, `AsyncCallable`, traits).
- [ ] Idempotency: for every `.gd` script under `modules/gdscript/tests/scripts/`, `format(x) == format(format(x))` (skipping known-bad/`errors` scripts).
- [ ] Refuse-on-error: every script under `analyzer/errors/` and `runtime/errors/` that fails to parse is refused (returns error, empty output).
- [ ] Semantic preservation: for each formattable script, the meaningful token stream (token types + literal values, excluding whitespace/newline/indent/comment trivia) is identical before and after formatting.
- [ ] `--gdscript-generate-format-tests` rewrites each `expected.gd` from current formatter output.

**Verify:** `./bin/godot.linuxbsd.editor.dev.x86_64 --headless --test --test-suite="*Format*"` → all pass.

**Steps:**

- [ ] **Step 1: Add the fixture runner test**

```cpp
TEST_CASE("[Format] Golden fixtures round-trip") {
	const String root = "modules/gdscript/tests/scripts/format";
	for (const String &input : collect_fixture_inputs(root)) {
		String expected_path = input.replace("input.gd", "expected.gd");
		String formatted = format_or_fail(FileAccess::get_file_as_string(input));
		String expected = FileAccess::get_file_as_string(expected_path);
		CHECK_MESSAGE(formatted == expected, "Fixture mismatch: " + input);
	}
}
```

- [ ] **Step 2: Author the first fixtures**

Create at least one pair per category, e.g. `modules/gdscript/tests/scripts/format/basics/spacing/input.gd` + `expected.gd`, ..., `format/fork/generics/input.gd` + `expected.gd`. Write `expected.gd` by hand for the first pair to anchor correctness, then use Step 6's generator for the rest after eyeballing.

- [ ] **Step 3: Add the idempotency property test**

```cpp
TEST_CASE("[Format] Idempotent over the whole corpus") {
	for (const String &script : collect_gd_scripts("modules/gdscript/tests/scripts")) {
		if (is_known_unparseable(script)) { continue; }
		String source = FileAccess::get_file_as_string(script);
		GDScriptFormatter formatter;
		GDScriptFormatter::Result first, second;
		if (formatter.format(source, script, first) != OK) { continue; }
		REQUIRE(formatter.format(first.formatted, script, second) == OK);
		CHECK_MESSAGE(first.formatted == second.formatted, "Not idempotent: " + script);
	}
}
```

- [ ] **Step 4: Add refuse-on-error and semantic-preservation tests**

```cpp
TEST_CASE("[Format] Refuses unparseable error fixtures") {
	for (const String &script : collect_gd_scripts("modules/gdscript/tests/scripts/analyzer/errors")) {
		String source = FileAccess::get_file_as_string(script);
		GDScriptFormatter formatter;
		GDScriptFormatter::Result result;
		if (has_parse_error(source, script)) {
			CHECK(formatter.format(source, script, result) != OK);
			CHECK(result.formatted.is_empty());
		}
	}
}

TEST_CASE("[Format] Preserves the meaningful token stream") {
	for (const String &script : collect_gd_scripts("modules/gdscript/tests/scripts")) {
		String source = FileAccess::get_file_as_string(script);
		GDScriptFormatter formatter;
		GDScriptFormatter::Result result;
		if (formatter.format(source, script, result) != OK) { continue; }
		CHECK_MESSAGE(significant_tokens(source) == significant_tokens(result.formatted),
				"Token stream changed: " + script);
	}
}
```

`significant_tokens()` tokenizes via `GDScriptTokenizerText` and returns a vector of `(Token::Type, literal-as-string)` excluding `NEWLINE`/`INDENT`/`DEDENT`/whitespace and comment trivia.

- [ ] **Step 5: Run to confirm failures, then make fixtures/harness pass**

Run: `./bin/godot.linuxbsd.editor.dev.x86_64 --headless --test --test-suite="*Format*"`
Expected first: FAIL on missing fixtures/helpers. Implement the helper functions and fixtures until green.

- [ ] **Step 6: Add the fixture regeneration command**

In `register_types.cpp`, register `REGISTER_TEST_COMMAND("--gdscript-generate-format-tests", &gdscript_generate_format_tests)`. The handler walks `format/**/input.gd`, formats each, and writes the sibling `expected.gd`. Wire `--gdscript-generate-format-tests` into the same main.cpp dispatch condition as Task 4 Step 1.

Run after authoring inputs:
`./bin/godot.linuxbsd.editor.dev.x86_64 --headless --gdscript-generate-format-tests`
Then review the diff before committing.

- [ ] **Step 7: Commit**

```bash
git add modules/gdscript/tests/test_format.h modules/gdscript/tests/scripts/format modules/gdscript/register_types.cpp main/main.cpp
git commit -m "test(gdscript): add formatter fixtures, idempotency, and semantic-preservation tests"
```

---

## Task 6: Precommit hook + CI gate

**Goal:** Enforce canonical formatting in precommit and CI via `--gdscript-format --check`.

**Files:**
- Modify: `.pre-commit-config.yaml`
- Modify/Create: CI workflow under `.github/workflows/`

**Acceptance Criteria:**
- [ ] A local precommit hook runs `--gdscript-format --check` on staged `*.gd` files and fails on any diff.
- [ ] A CI job step runs `--check` over the GDScript tree and fails on any diff.
- [ ] The expected binary path is documented, consistent with the repo's existing binary-invocation conventions.

**Verify:** Mis-format a tracked `.gd` file; confirm the hook and the CI step both fail; reformat with `--write`; confirm both pass.

**Steps:**

- [ ] **Step 1: Add the precommit hook**

Add to `.pre-commit-config.yaml` a `repo: local` hook:

```yaml
  - repo: local
    hooks:
      - id: gdscript-format
        name: GDScript canonical format
        entry: bin/godot.linuxbsd.editor.dev.x86_64 --headless --gdscript-format --check
        language: system
        files: \.gd$
        pass_filenames: true
```

Document in the hook comment that the binary must be built (`tests=yes`/editor) and that contributors run `--gdscript-format --write` to fix.

- [ ] **Step 2: Add the CI gate**

Add a step to the relevant workflow (after the build that produces the editor binary) running:

```bash
./bin/godot.linuxbsd.editor.dev.x86_64 --headless --gdscript-format --check .
```

so the job fails if any committed `.gd` is unformatted.

- [ ] **Step 3: Verify the gate**

Intentionally mis-format a file, run the hook and the CI command locally, confirm exit 1; run `--write`, confirm exit 0.

- [ ] **Step 4: Commit**

```bash
git add .pre-commit-config.yaml .github/workflows
git commit -m "ci(gdscript): enforce canonical formatting in precommit and CI"
```

---

## Notes for the implementer

- **Build:** `python3 -m SCons platform=linuxbsd target=editor dev_build=yes tests=yes module_text_server_fb_enabled=yes -j$(nproc)` (mirrors CI; binary at `bin/godot.linuxbsd.editor.dev.x86_64`). On macOS use `platform=macos`.
- **Test filter:** `--test-suite="*Format*"` runs only this suite (per repo memory, suite filters are reliable here; the suite tag is `[Modules][GDScript][Format]`).
- **Enum spellings:** verify `Token::Type` identifiers (EOF/ERROR/LITERAL/NEWLINE/INDENT/DEDENT) and `BinaryOpNode::OpType`/`AssignmentNode::Operation` spellings against the headers while implementing — the plan uses representative names.
- **Parser unmodified:** the formatter does its own tokenize pass for comments + literal sources; do not add getters to or otherwise change the parser/tokenizer.
- **TOOLS_ENABLED:** keep the whole feature (and its test header body) under `#ifdef TOOLS_ENABLED`.
