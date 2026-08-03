/**************************************************************************/
/*  fs_semantic_tokens.cpp                                                */
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

#include "fs_semantic_tokens.h"

#include "core/object/class_db.h"
#include "modules/foundry_script/fs_parser.h"
#include "modules/foundry_script/fs_position.h"
#include "modules/foundry_script/fs_tokenizer.h"
#include "modules/foundry_script/fs_utility_functions.h"

namespace {

using TokenType = LSP::SemanticTokenType;
using TokenModifier = LSP::SemanticTokenModifier;

// Mirrors `FSSemanticTokens::modifier_bit()` without its range check, for the internal call sites
// that only ever pass a literal legend entry.
constexpr uint32_t bit(TokenModifier p_modifier) {
	return 1u << static_cast<uint32_t>(p_modifier);
}

// Rank of a classification candidate. A word can be described by more than one producer -- the
// lexical scan sees `extend` as an identifier while the tree knows it opens a conformance -- so the
// merge step needs a total order that does not depend on visit order.
enum CandidateRank {
	// A reserved word recognized purely from the token stream.
	RANK_RESERVED_WORD = 1,
	// A word whose role only the tree proves: contextual keywords and namespace segments.
	RANK_CONTEXTUAL = 2,
	// An identifier classified from its resolved declaration or data type.
	RANK_SYMBOL = 3,
};

struct Candidate {
	FSSemanticTokens::Span span;
	int rank = RANK_RESERVED_WORD;
	// Insertion index, so equally ranked candidates still have a deterministic order.
	int sequence = 0;
};

struct CandidateSorter {
	bool operator()(const Candidate &p_left, const Candidate &p_right) const {
		if (p_left.span.line != p_right.span.line) {
			return p_left.span.line < p_right.span.line;
		}
		if (p_left.span.start_column != p_right.span.start_column) {
			return p_left.span.start_column < p_right.span.start_column;
		}
		if (p_left.rank != p_right.rank) {
			return p_left.rank > p_right.rank;
		}
		if (p_left.span.length != p_right.span.length) {
			return p_left.span.length > p_right.span.length;
		}
		return p_left.sequence < p_right.sequence;
	}
};

enum DeclaredSymbolKind {
	DECLARED_SYMBOL_NONE,
	DECLARED_SYMBOL_CLASS,
	DECLARED_SYMBOL_ENUM,
	DECLARED_SYMBOL_ENUM_MEMBER,
	DECLARED_SYMBOL_METHOD,
	DECLARED_SYMBOL_TUPLE,
};

struct DeclaredSymbol {
	DeclaredSymbolKind kind = DECLARED_SYMBOL_NONE;
	const FSParser::ClassNode *class_node = nullptr;
	const FSParser::EnumNode *enum_node = nullptr;
	const FSParser::FunctionNode *function_node = nullptr;
};

// A single-line token of the document, in the code point coordinates the encoder consumes.
struct LexicalToken {
	FSTokenizer::Token::Type type = FSTokenizer::Token::EMPTY;
	int line = 0;
	int start_column = 0;
	int length = 0;
	String text;
};

// The reserved words of the language, as spelled in `GRAMMAR.md` section 2.5.
//
// Two groups are deliberately excluded from the lexical pass because the token stream alone cannot
// tell them apart from ordinary names:
//   - the numeric keyword constants (`PI`, `TAU`, `INF`, `NAN`), which are values;
//   - `match`, `when`, and `uses`, which `Token::is_identifier()` accepts wherever an identifier is
//     expected, so `var match = 1` declares a variable rather than opening a match statement. The
//     tree pass claims them back at the positions where they really are keywords.
bool is_reserved_word(FSTokenizer::Token::Type p_type) {
	switch (p_type) {
		case FSTokenizer::Token::AND:
		case FSTokenizer::Token::OR:
		case FSTokenizer::Token::NOT:
		case FSTokenizer::Token::IF:
		case FSTokenizer::Token::ELIF:
		case FSTokenizer::Token::ELSE:
		case FSTokenizer::Token::FOR:
		case FSTokenizer::Token::WHILE:
		case FSTokenizer::Token::BREAK:
		case FSTokenizer::Token::CONTINUE:
		case FSTokenizer::Token::PASS:
		case FSTokenizer::Token::RETURN:
		case FSTokenizer::Token::ABSTRACT:
		case FSTokenizer::Token::AS:
		case FSTokenizer::Token::ASSERT:
		case FSTokenizer::Token::AWAIT:
		case FSTokenizer::Token::BREAKPOINT:
		case FSTokenizer::Token::CLASS:
		case FSTokenizer::Token::CLASS_NAME:
		case FSTokenizer::Token::ENUM_NAME:
		case FSTokenizer::Token::TK_CONST:
		case FSTokenizer::Token::ENUM:
		case FSTokenizer::Token::EXTENDS:
		case FSTokenizer::Token::FINAL:
		case FSTokenizer::Token::FUNC:
		case FSTokenizer::Token::IMPORT:
		case FSTokenizer::Token::TK_IN:
		case FSTokenizer::Token::IS:
		case FSTokenizer::Token::NAMESPACE:
		case FSTokenizer::Token::PRELOAD:
		case FSTokenizer::Token::SELF:
		case FSTokenizer::Token::SIGNAL:
		case FSTokenizer::Token::STATIC:
		case FSTokenizer::Token::SUPER:
		case FSTokenizer::Token::TRAIT:
		case FSTokenizer::Token::TRAIT_NAME:
		case FSTokenizer::Token::TUPLE:
		case FSTokenizer::Token::TUPLE_NAME:
		case FSTokenizer::Token::VAR:
		case FSTokenizer::Token::TK_VOID:
		case FSTokenizer::Token::YIELD:
			return true;
		default:
			return false;
	}
}

bool is_layout_token(FSTokenizer::Token::Type p_type) {
	return p_type == FSTokenizer::Token::NEWLINE ||
			p_type == FSTokenizer::Token::INDENT ||
			p_type == FSTokenizer::Token::DEDENT;
}

// How far into a get-node path the scan currently is. `FSParser::parse_get_node` accepts
// `("$" | "%") ["/"] segment? (("/" | "%") segment?)*`, so a node name is only a path segment
// directly after a path opener or separator, and everything after a consumed segment is ordinary
// code again unless another separator follows.
enum NodePathState {
	NODE_PATH_NONE,
	NODE_PATH_EXPECTS_NAME,
	NODE_PATH_AFTER_NAME,
};

NodePathState next_node_path_state(NodePathState p_state, const FSTokenizer::Token &p_token, bool p_previous_can_precede_bin_op) {
	switch (p_token.type) {
		case FSTokenizer::Token::DOLLAR:
			return NODE_PATH_EXPECTS_NAME;
		case FSTokenizer::Token::SLASH:
			return p_state == NODE_PATH_NONE ? NODE_PATH_NONE : NODE_PATH_EXPECTS_NAME;
		case FSTokenizer::Token::PERCENT:
			// `%` opens a unique-name path in prefix position and separates segments inside a path;
			// after a token that can end a value it is the modulo operator instead.
			return (p_state != NODE_PATH_NONE || !p_previous_can_precede_bin_op) ? NODE_PATH_EXPECTS_NAME : NODE_PATH_NONE;
		default:
			break;
	}
	const bool is_segment = p_token.is_node_name() || p_token.type == FSTokenizer::Token::LITERAL;
	return (p_state == NODE_PATH_EXPECTS_NAME && is_segment) ? NODE_PATH_AFTER_NAME : NODE_PATH_NONE;
}

// How far into a `namespace`/`import` dotted name the scan currently is. Neither declaration keeps
// its segments as nodes -- the tree stores only the joined string -- so the segments are recovered
// from the token stream, where the opening reserved word makes the position unambiguous.
enum NamespaceState {
	NAMESPACE_NONE,
	NAMESPACE_EXPECTS_SEGMENT,
	NAMESPACE_AFTER_SEGMENT,
};

/**
 * Turns one analyzed document into classification candidates.
 *
 * The walker only reads the tree: every span it produces is anchored to a node's recorded source
 * position and is discarded unless the text at that position still spells the name the node claims.
 * That keeps a half-finished edit -- where the analyzer may have synthesized or relocated nodes --
 * from painting unrelated text.
 */
class DocumentClassifier {
public:
	explicit DocumentClassifier(const Vector<String> &p_lines) :
			lines(p_lines) {}

	void scan_tokens(const String &p_source);
	void classify_tree(const FSParser::ClassNode *p_root);
	Vector<FSSemanticTokens::Span> resolve();

private:
	const Vector<String> &lines;
	Vector<LexicalToken> tokens;
	Vector<Candidate> candidates;
	const FSParser::ClassNode *current_class = nullptr;
	FSParser::DataType current_dispatch_type;
	bool has_current_dispatch_type = false;

	void add_span(int p_line, int p_start_column, int p_length, TokenType p_type, uint32_t p_modifiers, int p_rank);
	// Emits a span at a node's start position, but only when the source there really spells p_text.
	bool add_word(int p_godot_line, int p_godot_column, const String &p_text, TokenType p_type, uint32_t p_modifiers, int p_rank);
	bool add_identifier(const FSParser::IdentifierNode *p_identifier, TokenType p_type, uint32_t p_modifiers);
	// Emits a contextual keyword found between two source positions, e.g. the `when` of a match
	// guard. Positions are the parser's one-based, tab-expanded coordinates.
	void add_contextual_keyword(const String &p_text, int p_from_line, int p_from_column, int p_to_line, int p_to_column);
	// Like add_contextual_keyword(), but emits the last match in the range. This disambiguates a
	// contextual delimiter from an earlier declaration identifier with the same spelling.
	void add_last_contextual_keyword(const String &p_text, const FSParser::Node *p_from, const FSParser::Node *p_to);

	void classify_chain(const Vector<FSParser::IdentifierNode *> &p_chain, const FSParser::DataType &p_datatype, TokenType p_fallback, bool p_allow_enum_case);
	// Returns whether the identifier resolved to something classifiable.
	bool classify_identifier_reference(const FSParser::IdentifierNode *p_identifier);
	DeclaredSymbol declared_symbol_from_member(const FSParser::ClassNode::Member &p_member) const;
	DeclaredSymbol find_declared_symbol(const StringName &p_name) const;
	DeclaredSymbol resolve_declared_symbol(const FSParser::ExpressionNode *p_expression) const;
	bool classify_declared_symbol(const FSParser::IdentifierNode *p_identifier, const DeclaredSymbol &p_symbol);
	bool datatype_has_native_method(const FSParser::DataType &p_datatype, const StringName &p_name) const;
	bool datatype_has_native_property(const FSParser::DataType &p_datatype, const StringName &p_name) const;
	bool datatype_has_native_signal(const FSParser::DataType &p_datatype, const StringName &p_name) const;
	bool current_class_has_native_method(const StringName &p_name) const;
	bool current_class_has_native_property(const StringName &p_name) const;
	bool current_class_has_native_signal(const StringName &p_name) const;

	void walk_annotations(const FSParser::Node *p_node);
	void walk_annotation(const FSParser::AnnotationNode *p_annotation);
	void walk_annotation_declaration(const FSParser::AnnotationDeclarationNode *p_declaration);
	void walk_class(const FSParser::ClassNode *p_class);
	void walk_conformance(const FSParser::ConformanceNode *p_conformance);
	void walk_constant(const FSParser::ConstantNode *p_constant, bool p_is_member);
	void walk_enum(const FSParser::EnumNode *p_enum);
	void walk_enum_value(const FSParser::EnumNode::Value &p_value);
	void walk_expression(const FSParser::ExpressionNode *p_expression);
	void walk_call(const FSParser::CallNode *p_call);
	void walk_function(const FSParser::FunctionNode *p_function, bool p_is_lambda);
	void walk_node(const FSParser::Node *p_node);
	void walk_parameter(const FSParser::ParameterNode *p_parameter);
	void walk_pattern(const FSParser::PatternNode *p_pattern);
	void walk_signal(const FSParser::SignalNode *p_signal);
	void walk_subscript(const FSParser::SubscriptNode *p_subscript, bool p_skip_attribute);
	void walk_suite(const FSParser::SuiteNode *p_suite);
	void walk_trait_use(const FSParser::ClassNode::TraitUse &p_use, const FSParser::Node *p_owner);
	void walk_tuple(const FSParser::TupleNode *p_tuple);
	void walk_type(const FSParser::TypeNode *p_type);
	void walk_type_parameter(const FSParser::TypeParameterNode *p_type_parameter);
	void walk_variable(const FSParser::VariableNode *p_variable, bool p_is_member);
};

// Whether a data type names something that only the engine defines, so a reference to it carries
// `defaultLibrary`. A project symbol that happens to share the spelling resolves to a script or
// class type instead and is left without the modifier.
bool is_default_library_type(const FSParser::DataType &p_datatype) {
	switch (p_datatype.kind) {
		case FSParser::DataType::BUILTIN:
		case FSParser::DataType::NATIVE:
		case FSParser::DataType::VARIANT:
			return true;
		case FSParser::DataType::ENUM:
			// A script-declared enum records where it came from; a native one does not.
			return p_datatype.class_type == nullptr && p_datatype.script_path.is_empty();
		default:
			return false;
	}
}

struct TypeClassification {
	TokenType type = TokenType::TYPE;
	uint32_t modifiers = 0;
	bool valid = false;
};

TypeClassification classify_datatype(const FSParser::DataType &p_datatype) {
	TypeClassification result;
	result.valid = true;
	switch (p_datatype.kind) {
		case FSParser::DataType::BUILTIN:
		case FSParser::DataType::VARIANT:
			result.type = TokenType::TYPE;
			break;
		case FSParser::DataType::NATIVE:
			result.type = TokenType::CLASS;
			break;
		case FSParser::DataType::SCRIPT:
			result.type = TokenType::CLASS;
			break;
		case FSParser::DataType::CLASS:
			result.type = (p_datatype.class_type != nullptr && p_datatype.class_type->is_trait) ? TokenType::INTERFACE : TokenType::CLASS;
			break;
		case FSParser::DataType::ENUM:
			result.type = TokenType::ENUM;
			break;
		case FSParser::DataType::TUPLE:
			result.type = TokenType::STRUCT;
			break;
		case FSParser::DataType::TYPE_PARAMETER:
			result.type = TokenType::TYPE_PARAMETER;
			break;
		default:
			result.valid = false;
			return result;
	}
	if (is_default_library_type(p_datatype)) {
		result.modifiers |= bit(TokenModifier::DEFAULT_LIBRARY);
	}
	return result;
}

// The namespace a resolved type lives in, or an empty string when it has none. Used to tell a
// namespace-qualified reference (`game.Player`) from an outer-class-qualified one (`Outer.Inner`).
String namespace_of_datatype(const FSParser::DataType &p_datatype) {
	// Enums and tuples declared globally keep their declaring class too, so the namespace of a
	// qualified reference is wherever that class ended up, whatever the type's kind is.
	if (p_datatype.class_type == nullptr) {
		return String();
	}
	const FSParser::ClassNode *root = p_datatype.class_type;
	while (root->outer != nullptr) {
		root = root->outer;
	}
	return root->namespace_name;
}

bool is_namespace_prefix(const String &p_namespace, const String &p_prefix) {
	if (p_namespace.is_empty() || p_prefix.is_empty()) {
		return false;
	}
	return p_namespace == p_prefix || p_namespace.begins_with(p_prefix + ".");
}

void DocumentClassifier::add_span(int p_line, int p_start_column, int p_length, TokenType p_type, uint32_t p_modifiers, int p_rank) {
	if (p_length <= 0 || p_line < 0 || p_line >= lines.size() || p_start_column < 0) {
		return;
	}
	Candidate candidate;
	candidate.span.line = p_line;
	candidate.span.start_column = p_start_column;
	candidate.span.length = p_length;
	candidate.span.type = p_type;
	candidate.span.modifiers = p_modifiers;
	candidate.rank = p_rank;
	candidate.sequence = candidates.size();
	candidates.push_back(candidate);
}

bool DocumentClassifier::add_word(int p_godot_line, int p_godot_column, const String &p_text, TokenType p_type, uint32_t p_modifiers, int p_rank) {
	const int line_index = p_godot_line - 1;
	if (p_text.is_empty() || line_index < 0 || line_index >= lines.size()) {
		return false;
	}
	const String &line = lines[line_index];
	const int start_column = FSTextPosition::godot_column_to_text_column(line, p_godot_column);
	if (start_column + p_text.length() > line.length()) {
		return false;
	}
	// A node keeps its recorded position through analysis even when the analyzer rewrites or
	// synthesizes it, so confirm the source still spells the name before painting over it.
	if (line.substr(start_column, p_text.length()) != p_text) {
		return false;
	}
	add_span(line_index, start_column, p_text.length(), p_type, p_modifiers, p_rank);
	return true;
}

bool DocumentClassifier::add_identifier(const FSParser::IdentifierNode *p_identifier, TokenType p_type, uint32_t p_modifiers) {
	if (p_identifier == nullptr) {
		return false;
	}
	return add_word(p_identifier->start_line, p_identifier->start_column, String(p_identifier->name), p_type, p_modifiers, RANK_SYMBOL);
}

void DocumentClassifier::add_contextual_keyword(const String &p_text, int p_from_line, int p_from_column, int p_to_line, int p_to_column) {
	for (const LexicalToken &token : tokens) {
		const int token_line = token.line + 1;
		const int token_column = FSTextPosition::text_column_to_godot_column(lines[token.line], token.start_column);
		if (token_line < p_from_line || (token_line == p_from_line && token_column < p_from_column)) {
			continue;
		}
		if (token_line > p_to_line || (token_line == p_to_line && token_column >= p_to_column)) {
			break;
		}
		if (token.text == p_text) {
			add_span(token.line, token.start_column, token.length, TokenType::KEYWORD, 0, RANK_CONTEXTUAL);
			return;
		}
	}
}

void DocumentClassifier::add_last_contextual_keyword(const String &p_text, const FSParser::Node *p_from, const FSParser::Node *p_to) {
	if (p_from == nullptr) {
		return;
	}
	const int to_line = p_to != nullptr ? p_to->start_line : p_from->end_line;
	const int to_column = p_to != nullptr ? p_to->start_column : p_from->end_column;
	const LexicalToken *last_match = nullptr;
	for (const LexicalToken &token : tokens) {
		const int token_line = token.line + 1;
		const int token_column = FSTextPosition::text_column_to_godot_column(lines[token.line], token.start_column);
		if (token_line < p_from->start_line || (token_line == p_from->start_line && token_column < p_from->start_column)) {
			continue;
		}
		if (token_line > to_line || (token_line == to_line && token_column >= to_column)) {
			break;
		}
		if (token.text == p_text) {
			last_match = &token;
		}
	}
	if (last_match != nullptr) {
		add_span(last_match->line, last_match->start_column, last_match->length, TokenType::KEYWORD, 0, RANK_CONTEXTUAL);
	}
}

void DocumentClassifier::scan_tokens(const String &p_source) {
	if (p_source.is_empty()) {
		return;
	}

	FSTokenizerText tokenizer;
	tokenizer.set_source_code(p_source);

	// The tokenizer synthesizes layout tokens (`NEWLINE`, `INDENT`, `DEDENT`) and error tokens that
	// consume no source, so token count is not bounded by source length alone. Cap the scan so a
	// pathological or truncated document cannot spin here forever.
	const int scan_limit = p_source.length() * 4 + 64;
	bool previous_was_period = false;
	bool previous_can_precede_bin_op = false;
	NodePathState node_path_state = NODE_PATH_NONE;
	NamespaceState namespace_state = NAMESPACE_NONE;
	uint32_t namespace_modifiers = 0;
	int bracket_depth = 0;

	for (int scanned = 0; scanned < scan_limit; scanned++) {
		const FSTokenizer::Token token = tokenizer.scan();
		if (token.type == FSTokenizer::Token::TK_EOF) {
			break;
		}
		// Layout tokens carry no source of their own. Inside a grouping construct a line break is a
		// continuation, so an attribute or node path stays readable across it; at statement level it
		// ends the logical line and nothing before it can influence what comes next.
		if (is_layout_token(token.type)) {
			if (bracket_depth == 0) {
				previous_was_period = false;
				previous_can_precede_bin_op = false;
				node_path_state = NODE_PATH_NONE;
				namespace_state = NAMESPACE_NONE;
			}
			continue;
		}

		switch (token.type) {
			case FSTokenizer::Token::PARENTHESIS_OPEN:
			case FSTokenizer::Token::BRACKET_OPEN:
			case FSTokenizer::Token::BRACE_OPEN:
				bracket_depth++;
				break;
			case FSTokenizer::Token::PARENTHESIS_CLOSE:
			case FSTokenizer::Token::BRACKET_CLOSE:
			case FSTokenizer::Token::BRACE_CLOSE:
				bracket_depth = MAX(bracket_depth - 1, 0);
				break;
			default:
				break;
		}

		const bool after_period = previous_was_period;
		previous_was_period = token.type == FSTokenizer::Token::PERIOD;

		const NodePathState previous_path_state = node_path_state;
		node_path_state = next_node_path_state(previous_path_state, token, previous_can_precede_bin_op);
		// A keyword spelled in attribute position is an ordinary attribute name, so it ends a value
		// just like an identifier would. Without this, the `%` in `self.class % 2` would look like a
		// unique-name path opener. Mirrors the tokenizer's own `last_token_is_keyword_attribute`.
		previous_can_precede_bin_op = token.can_precede_bin_op() || (after_period && token.is_node_name());

		// Single-line tokens are recorded so the tree pass can look up contextual keywords by their
		// spelling. A multi-line token (a block string) can never be one.
		int line_index = token.start_line - 1;
		if (token.start_line == token.end_line && line_index >= 0 && line_index < lines.size()) {
			const String &line = lines[line_index];
			const int start_column = FSTextPosition::godot_column_to_text_column(line, token.start_column);
			const int end_column = FSTextPosition::godot_column_to_text_column(line, token.end_column);
			if (end_column > start_column) {
				LexicalToken lexical;
				lexical.type = token.type;
				lexical.line = line_index;
				lexical.start_column = start_column;
				lexical.length = end_column - start_column;
				lexical.text = line.substr(start_column, lexical.length);
				tokens.push_back(lexical);
			}
		}

		// `namespace a.b` and `import a.b` are the only places a namespace is written as source, and
		// the tree keeps just the joined name, so the segments are recovered here.
		const NamespaceState previous_namespace_state = namespace_state;
		if (token.type == FSTokenizer::Token::NAMESPACE) {
			namespace_state = NAMESPACE_EXPECTS_SEGMENT;
			namespace_modifiers = bit(TokenModifier::DECLARATION);
		} else if (token.type == FSTokenizer::Token::IMPORT) {
			namespace_state = NAMESPACE_EXPECTS_SEGMENT;
			namespace_modifiers = 0;
		} else if (previous_namespace_state == NAMESPACE_EXPECTS_SEGMENT && token.is_identifier()) {
			namespace_state = NAMESPACE_AFTER_SEGMENT;
			if (!tokens.is_empty()) {
				const LexicalToken &lexical = tokens[tokens.size() - 1];
				add_span(lexical.line, lexical.start_column, lexical.length, TokenType::NAMESPACE, namespace_modifiers, RANK_CONTEXTUAL);
			}
		} else if (previous_namespace_state == NAMESPACE_AFTER_SEGMENT && token.type == FSTokenizer::Token::PERIOD) {
			namespace_state = NAMESPACE_EXPECTS_SEGMENT;
		} else {
			namespace_state = NAMESPACE_NONE;
		}

		if (!is_reserved_word(token.type)) {
			continue;
		}
		// `self.class`, `node.signal`: the parser re-spells a reserved word in attribute position as
		// an ordinary identifier, so highlighting it as a keyword would be plainly wrong.
		if (after_period) {
			continue;
		}
		// `$class/signal`: every path segment is a node name, not a keyword.
		if (previous_path_state == NODE_PATH_EXPECTS_NAME && token.is_node_name()) {
			continue;
		}
		// Reserved words never span lines; anything that claims to did not come from real source.
		if (token.start_line != token.end_line) {
			continue;
		}
		if (line_index < 0 || line_index >= lines.size()) {
			continue;
		}

		// Tokenizer lines and columns are one-based, and its columns expand a tab to the editor's
		// indent size. `godot_column_to_text_column` is the inverse used everywhere else in the
		// language server, and it yields the code point column `encode()` consumes.
		const String &line = lines[line_index];
		const int start_column = FSTextPosition::godot_column_to_text_column(line, token.start_column);
		const int end_column = FSTextPosition::godot_column_to_text_column(line, token.end_column);
		add_span(line_index, start_column, end_column - start_column, TokenType::KEYWORD, 0, RANK_RESERVED_WORD);
	}
}

void DocumentClassifier::classify_tree(const FSParser::ClassNode *p_root) {
	if (p_root == nullptr) {
		return;
	}
	walk_class(p_root);
}

Vector<FSSemanticTokens::Span> DocumentClassifier::resolve() {
	Vector<FSSemanticTokens::Span> result;
	candidates.sort_custom<CandidateSorter>();

	for (const Candidate &candidate : candidates) {
		if (!result.is_empty()) {
			FSSemanticTokens::Span &previous = result.write[result.size() - 1];
			if (previous.line == candidate.span.line && candidate.span.start_column < previous.start_column + previous.length) {
				// An identical span of the same type is the same classification reached twice, so
				// the modifiers describe one token together. Anything else conflicts, and the
				// lower-ranked candidate loses.
				if (previous.start_column == candidate.span.start_column && previous.length == candidate.span.length && previous.type == candidate.span.type) {
					previous.modifiers |= candidate.span.modifiers;
				}
				continue;
			}
		}
		result.push_back(candidate.span);
	}
	return result;
}

void DocumentClassifier::classify_chain(const Vector<FSParser::IdentifierNode *> &p_chain, const FSParser::DataType &p_datatype, TokenType p_fallback, bool p_allow_enum_case) {
	const int count = p_chain.size();
	if (count == 0) {
		return;
	}

	TypeClassification tail = classify_datatype(p_datatype);
	if (!tail.valid) {
		tail.type = p_fallback;
		tail.modifiers = 0;
	}

	// `Message.Move` in an `is` test names a case of a tagged union, so the last segment is the case
	// and the one before it is the enum itself.
	const bool names_enum_case = p_allow_enum_case && count >= 2 &&
			p_datatype.kind == FSParser::DataType::ENUM && p_datatype.enum_case_name != StringName();
	const int enum_index = names_enum_case ? count - 2 : -1;

	const String type_namespace = namespace_of_datatype(p_datatype);
	String prefix;
	for (int i = 0; i < count; i++) {
		if (i > 0) {
			prefix += ".";
		}
		prefix += String(p_chain[i]->name);

		if (i == count - 1) {
			if (names_enum_case) {
				add_identifier(p_chain[i], TokenType::ENUM_MEMBER, bit(TokenModifier::READONLY) | (tail.modifiers & bit(TokenModifier::DEFAULT_LIBRARY)));
			} else {
				add_identifier(p_chain[i], tail.type, tail.modifiers);
			}
		} else if (i == enum_index) {
			add_identifier(p_chain[i], TokenType::ENUM, tail.modifiers & bit(TokenModifier::DEFAULT_LIBRARY));
		} else if (is_namespace_prefix(type_namespace, prefix)) {
			add_identifier(p_chain[i], TokenType::NAMESPACE, 0);
		} else {
			add_identifier(p_chain[i], TokenType::CLASS, 0);
		}
	}
}

bool DocumentClassifier::classify_identifier_reference(const FSParser::IdentifierNode *p_identifier) {
	if (p_identifier == nullptr) {
		return false;
	}

	switch (p_identifier->source) {
		case FSParser::IdentifierNode::FUNCTION_PARAMETER:
			return add_identifier(p_identifier, TokenType::PARAMETER, 0);
		case FSParser::IdentifierNode::LOCAL_VARIABLE:
		case FSParser::IdentifierNode::LOCAL_ITERATOR:
		case FSParser::IdentifierNode::LOCAL_BIND:
			return add_identifier(p_identifier, TokenType::VARIABLE, 0);
		case FSParser::IdentifierNode::LOCAL_CONSTANT:
			return add_identifier(p_identifier, TokenType::VARIABLE, bit(TokenModifier::READONLY));
		case FSParser::IdentifierNode::MEMBER_VARIABLE:
			return add_identifier(p_identifier, TokenType::PROPERTY, 0);
		case FSParser::IdentifierNode::INHERITED_VARIABLE:
			// Native methods and signals used as values arrive through the analyzer's inherited
			// variable fallback, so recover their actual kind and ownership from the enclosing class.
			if (current_class_has_native_method(p_identifier->name)) {
				return add_identifier(p_identifier, TokenType::METHOD, bit(TokenModifier::DEFAULT_LIBRARY));
			}
			if (current_class_has_native_signal(p_identifier->name)) {
				return add_identifier(p_identifier, TokenType::EVENT, bit(TokenModifier::DEFAULT_LIBRARY));
			}
			return add_identifier(p_identifier, TokenType::PROPERTY,
					current_class_has_native_property(p_identifier->name) ? bit(TokenModifier::DEFAULT_LIBRARY) : 0);
		case FSParser::IdentifierNode::STATIC_VARIABLE:
			return add_identifier(p_identifier, TokenType::PROPERTY, bit(TokenModifier::STATIC));
		case FSParser::IdentifierNode::MEMBER_CONSTANT:
			// Classify the declaration, not its value: a project constant remains a property even
			// when its resolved value has an enum data type. Enum declarations themselves reach the
			// declaration lookup below and are classified as enums.
			return add_identifier(p_identifier, TokenType::PROPERTY, bit(TokenModifier::READONLY));
		case FSParser::IdentifierNode::MEMBER_FUNCTION: {
			uint32_t modifiers = 0;
			const bool is_static = p_identifier->function_source_is_static ||
					(p_identifier->function_source != nullptr && p_identifier->function_source->is_static);
			if (is_static) {
				modifiers |= bit(TokenModifier::STATIC);
			}
			return add_identifier(p_identifier, TokenType::METHOD, modifiers);
		}
		case FSParser::IdentifierNode::MEMBER_SIGNAL:
			return add_identifier(p_identifier, TokenType::EVENT, 0);
		case FSParser::IdentifierNode::NATIVE_CLASS:
			return add_identifier(p_identifier, TokenType::CLASS, bit(TokenModifier::DEFAULT_LIBRARY));
		case FSParser::IdentifierNode::STATIC_SELF_CLASS:
			// `Self` in an expression position names the receiver's class, so it highlights as a class
			// rather than falling through to the meta-type path, which would report the bound instead.
			return add_identifier(p_identifier, TokenType::CLASS, 0);
		case FSParser::IdentifierNode::MEMBER_CLASS:
		case FSParser::IdentifierNode::UNDEFINED_SOURCE:
			break;
	}

	// Nothing declared the name in this file's scopes, so the resolved data type is all that is
	// left. A meta type names a type; an enum value names a case; anything else stays unclassified
	// rather than guessing.
	const FSParser::DataType datatype = p_identifier->get_datatype();
	if (datatype.is_meta_type) {
		const TypeClassification classification = classify_datatype(datatype);
		return classification.valid && add_identifier(p_identifier, classification.type, classification.modifiers);
	}
	if (datatype.kind == FSParser::DataType::ENUM && datatype.enum_case_name != StringName()) {
		uint32_t modifiers = bit(TokenModifier::READONLY);
		if (is_default_library_type(datatype)) {
			modifiers |= bit(TokenModifier::DEFAULT_LIBRARY);
		}
		return add_identifier(p_identifier, TokenType::ENUM_MEMBER, modifiers);
	}
	// A managed buffer may not have a loadable script resource yet, leaving every segment of an
	// otherwise valid inner-type chain unresolved. Consult declarations only after analyzer-backed
	// sources and data types have had the opportunity to classify the identifier.
	if (p_identifier->source == FSParser::IdentifierNode::UNDEFINED_SOURCE ||
			p_identifier->source == FSParser::IdentifierNode::MEMBER_CLASS) {
		const DeclaredSymbol symbol = find_declared_symbol(p_identifier->name);
		if (classify_declared_symbol(p_identifier, symbol)) {
			return true;
		}
	}
	// The analyzer resolves an inner class lazily, so a reference to one can carry no usable data
	// type; the recorded source still says the name is a class.
	if (p_identifier->source == FSParser::IdentifierNode::MEMBER_CLASS) {
		return add_identifier(p_identifier, TokenType::CLASS, 0);
	}
	return false;
}

DeclaredSymbol DocumentClassifier::declared_symbol_from_member(const FSParser::ClassNode::Member &p_member) const {
	DeclaredSymbol result;
	switch (p_member.type) {
		case FSParser::ClassNode::Member::CLASS:
			result.kind = DECLARED_SYMBOL_CLASS;
			result.class_node = p_member.m_class;
			break;
		case FSParser::ClassNode::Member::ENUM:
			result.kind = DECLARED_SYMBOL_ENUM;
			result.enum_node = p_member.m_enum;
			break;
		case FSParser::ClassNode::Member::FUNCTION:
			result.kind = DECLARED_SYMBOL_METHOD;
			result.function_node = p_member.function;
			break;
		case FSParser::ClassNode::Member::TUPLE:
			result.kind = DECLARED_SYMBOL_TUPLE;
			break;
		default:
			break;
	}
	return result;
}

DeclaredSymbol DocumentClassifier::find_declared_symbol(const StringName &p_name) const {
	for (const FSParser::ClassNode *scope = current_class; scope != nullptr; scope = scope->outer) {
		const bool is_lexical_outer = scope != current_class;
		HashSet<const FSParser::ClassNode *> visited;
		for (const FSParser::ClassNode *owner = scope; owner != nullptr && !visited.has(owner);) {
			visited.insert(owner);
			if (owner->has_member(p_name)) {
				const DeclaredSymbol symbol = declared_symbol_from_member(owner->get_member(p_name));
				// A nested class can refer to types declared by a lexical outer, but it has no
				// implicit outer instance and bare lookup does not import the outer's static methods.
				if (!is_lexical_outer || symbol.kind != DECLARED_SYMBOL_METHOD) {
					return symbol;
				}
			}
			// Applied-trait methods are project declarations too. They must be considered before a
			// same-named native method makes the bare-call fallback claim default-library ownership.
			for (const FSParser::ClassNode *trait : owner->resolved_traits) {
				if (trait != nullptr && trait->has_member(p_name)) {
					const DeclaredSymbol symbol = declared_symbol_from_member(trait->get_member(p_name));
					if (!is_lexical_outer || symbol.kind != DECLARED_SYMBOL_METHOD) {
						return symbol;
					}
				}
			}
			owner = owner->base_type.kind == FSParser::DataType::CLASS ? owner->base_type.class_type : nullptr;
		}
	}
	return DeclaredSymbol();
}

DeclaredSymbol DocumentClassifier::resolve_declared_symbol(const FSParser::ExpressionNode *p_expression) const {
	if (p_expression == nullptr) {
		return DeclaredSymbol();
	}
	if (p_expression->type == FSParser::Node::IDENTIFIER) {
		const FSParser::IdentifierNode *identifier = static_cast<const FSParser::IdentifierNode *>(p_expression);
		if (identifier->source != FSParser::IdentifierNode::UNDEFINED_SOURCE &&
				identifier->source != FSParser::IdentifierNode::MEMBER_CLASS) {
			return DeclaredSymbol();
		}
		return find_declared_symbol(identifier->name);
	}
	if (p_expression->type != FSParser::Node::SUBSCRIPT) {
		return DeclaredSymbol();
	}

	const FSParser::SubscriptNode *subscript = static_cast<const FSParser::SubscriptNode *>(p_expression);
	if (!subscript->is_attribute || subscript->attribute == nullptr) {
		return DeclaredSymbol();
	}
	const DeclaredSymbol base = resolve_declared_symbol(subscript->base);
	if (base.kind == DECLARED_SYMBOL_CLASS && base.class_node != nullptr && base.class_node->has_member(subscript->attribute->name)) {
		return declared_symbol_from_member(base.class_node->get_member(subscript->attribute->name));
	}
	if (base.kind == DECLARED_SYMBOL_ENUM && base.enum_node != nullptr) {
		for (const FSParser::EnumNode::Value &value : base.enum_node->values) {
			if (value.identifier != nullptr && value.identifier->name == subscript->attribute->name) {
				DeclaredSymbol result;
				result.kind = DECLARED_SYMBOL_ENUM_MEMBER;
				result.enum_node = base.enum_node;
				return result;
			}
		}
	}
	return DeclaredSymbol();
}

bool DocumentClassifier::classify_declared_symbol(const FSParser::IdentifierNode *p_identifier, const DeclaredSymbol &p_symbol) {
	switch (p_symbol.kind) {
		case DECLARED_SYMBOL_CLASS:
			return add_identifier(p_identifier, TokenType::CLASS, 0);
		case DECLARED_SYMBOL_ENUM:
			return add_identifier(p_identifier, TokenType::ENUM, 0);
		case DECLARED_SYMBOL_ENUM_MEMBER:
			return add_identifier(p_identifier, TokenType::ENUM_MEMBER, bit(TokenModifier::READONLY));
		case DECLARED_SYMBOL_METHOD:
			return add_identifier(p_identifier, TokenType::METHOD,
					p_symbol.function_node != nullptr && p_symbol.function_node->is_static ? bit(TokenModifier::STATIC) : 0);
		case DECLARED_SYMBOL_TUPLE:
			return add_identifier(p_identifier, TokenType::STRUCT, 0);
		case DECLARED_SYMBOL_NONE:
			return false;
	}
	return false;
}

// Whether a non-FoundryScript script already declares `p_name` as a property, method, or signal.
// A name claimed by any one of those kinds shadows every native lookup for that name -- a script
// property named `ready` must not let a native-signal check fall through to `Node.ready`.
bool script_declares_member(const Ref<Script> &p_script, const StringName &p_name) {
	if (p_script->has_method(p_name) || p_script->has_script_signal(p_name)) {
		return true;
	}
	List<PropertyInfo> properties;
	p_script->get_script_property_list(&properties);
	for (const PropertyInfo &property : properties) {
		if (property.name == p_name) {
			return true;
		}
	}
	return false;
}

bool DocumentClassifier::datatype_has_native_method(const FSParser::DataType &p_datatype, const StringName &p_name) const {
	FSParser::DataType datatype = p_datatype;
	HashSet<const FSParser::ClassNode *> visited_classes;
	while (datatype.kind == FSParser::DataType::CLASS && datatype.class_type != nullptr && !visited_classes.has(datatype.class_type)) {
		const FSParser::ClassNode *owner = datatype.class_type;
		visited_classes.insert(owner);
		if (owner->has_member(p_name)) {
			return false;
		}
		for (const FSParser::ClassNode *trait : owner->resolved_traits) {
			if (trait != nullptr && trait->has_member(p_name)) {
				return false;
			}
		}
		datatype = owner->base_type;
	}

	if (datatype.kind == FSParser::DataType::BUILTIN) {
		return Variant::has_builtin_method(datatype.builtin_type, p_name);
	}
	StringName native_base = datatype.native_type;
	if (datatype.kind == FSParser::DataType::SCRIPT && datatype.script_type.is_valid()) {
		HashSet<const Script *> visited_scripts;
		for (Ref<Script> script = datatype.script_type;
				script.is_valid() && !visited_scripts.has(script.ptr());
				script = script->get_base_script()) {
			visited_scripts.insert(script.ptr());
			if (script_declares_member(script, p_name)) {
				return false;
			}
			if (script->get_instance_base_type() != StringName()) {
				native_base = script->get_instance_base_type();
			}
		}
	}
	return native_base != StringName() && ClassDB::has_method(native_base, p_name);
}

bool DocumentClassifier::datatype_has_native_property(const FSParser::DataType &p_datatype, const StringName &p_name) const {
	FSParser::DataType datatype = p_datatype;
	HashSet<const FSParser::ClassNode *> visited_classes;
	while (datatype.kind == FSParser::DataType::CLASS && datatype.class_type != nullptr && !visited_classes.has(datatype.class_type)) {
		const FSParser::ClassNode *owner = datatype.class_type;
		visited_classes.insert(owner);
		if (owner->has_member(p_name)) {
			return false;
		}
		for (const FSParser::ClassNode *trait : owner->resolved_traits) {
			if (trait != nullptr && trait->has_member(p_name)) {
				return false;
			}
		}
		datatype = owner->base_type;
	}

	StringName native_base = datatype.native_type;
	if (datatype.kind == FSParser::DataType::SCRIPT && datatype.script_type.is_valid()) {
		HashSet<const Script *> visited_scripts;
		for (Ref<Script> script = datatype.script_type;
				script.is_valid() && !visited_scripts.has(script.ptr());
				script = script->get_base_script()) {
			visited_scripts.insert(script.ptr());
			if (script_declares_member(script, p_name)) {
				return false;
			}
			if (script->get_instance_base_type() != StringName()) {
				native_base = script->get_instance_base_type();
			}
		}
	}
	return native_base != StringName() && ClassDB::has_property(native_base, p_name);
}

bool DocumentClassifier::datatype_has_native_signal(const FSParser::DataType &p_datatype, const StringName &p_name) const {
	FSParser::DataType datatype = p_datatype;
	HashSet<const FSParser::ClassNode *> visited_classes;
	while (datatype.kind == FSParser::DataType::CLASS && datatype.class_type != nullptr && !visited_classes.has(datatype.class_type)) {
		const FSParser::ClassNode *owner = datatype.class_type;
		visited_classes.insert(owner);
		if (owner->has_member(p_name)) {
			return false;
		}
		for (const FSParser::ClassNode *trait : owner->resolved_traits) {
			if (trait != nullptr && trait->has_member(p_name)) {
				return false;
			}
		}
		datatype = owner->base_type;
	}

	StringName native_base = datatype.native_type;
	if (datatype.kind == FSParser::DataType::SCRIPT && datatype.script_type.is_valid()) {
		HashSet<const Script *> visited_scripts;
		for (Ref<Script> script = datatype.script_type;
				script.is_valid() && !visited_scripts.has(script.ptr());
				script = script->get_base_script()) {
			visited_scripts.insert(script.ptr());
			if (script_declares_member(script, p_name)) {
				return false;
			}
			if (script->get_instance_base_type() != StringName()) {
				native_base = script->get_instance_base_type();
			}
		}
	}
	return native_base != StringName() && ClassDB::has_signal(native_base, p_name);
}

bool DocumentClassifier::current_class_has_native_method(const StringName &p_name) const {
	if (has_current_dispatch_type) {
		return datatype_has_native_method(current_dispatch_type, p_name);
	}
	return current_class != nullptr && datatype_has_native_method(current_class->get_datatype(), p_name);
}

bool DocumentClassifier::current_class_has_native_property(const StringName &p_name) const {
	if (has_current_dispatch_type) {
		return datatype_has_native_property(current_dispatch_type, p_name);
	}
	return current_class != nullptr && datatype_has_native_property(current_class->get_datatype(), p_name);
}

bool DocumentClassifier::current_class_has_native_signal(const StringName &p_name) const {
	if (has_current_dispatch_type) {
		return datatype_has_native_signal(current_dispatch_type, p_name);
	}
	return current_class != nullptr && datatype_has_native_signal(current_class->get_datatype(), p_name);
}

void DocumentClassifier::walk_annotations(const FSParser::Node *p_node) {
	if (p_node == nullptr) {
		return;
	}
	for (const FSParser::AnnotationNode *annotation : p_node->annotations) {
		walk_annotation(annotation);
	}
}

void DocumentClassifier::walk_annotation(const FSParser::AnnotationNode *p_annotation) {
	if (p_annotation == nullptr) {
		return;
	}
	// A built-in annotation resolves to parser-owned information; a custom one resolves to a
	// declaration written in Foundry Script and therefore is not part of the default library.
	const uint32_t modifiers = (p_annotation->info != nullptr && !p_annotation->is_custom) ? bit(TokenModifier::DEFAULT_LIBRARY) : 0;
	add_word(p_annotation->start_line, p_annotation->start_column, String(p_annotation->name), TokenType::DECORATOR, modifiers, RANK_SYMBOL);
	for (const FSParser::ExpressionNode *argument : p_annotation->arguments) {
		walk_expression(argument);
	}
}

void DocumentClassifier::walk_annotation_declaration(const FSParser::AnnotationDeclarationNode *p_declaration) {
	if (p_declaration == nullptr) {
		return;
	}
	walk_annotations(p_declaration);
	add_word(p_declaration->start_line, p_declaration->start_column, "annotation", TokenType::KEYWORD, 0, RANK_CONTEXTUAL);
	add_identifier(p_declaration->identifier, TokenType::DECORATOR, bit(TokenModifier::DECLARATION));
	for (const FSParser::ParameterNode *parameter : p_declaration->parameters) {
		walk_parameter(parameter);
	}
	walk_parameter(p_declaration->rest_parameter);

	if (p_declaration->targets != FSParser::AnnotationDeclarationNode::TARGET_NONE) {
		// `targets` separates the parameter list from the target list, so it can only appear after
		// the last parameter and before the end of the declaration.
		const FSParser::Node *after = p_declaration->rest_parameter != nullptr
				? static_cast<const FSParser::Node *>(p_declaration->rest_parameter)
				: (p_declaration->parameters.is_empty() ? static_cast<const FSParser::Node *>(p_declaration->identifier)
														: static_cast<const FSParser::Node *>(p_declaration->parameters[p_declaration->parameters.size() - 1]));
		if (after != nullptr) {
			add_contextual_keyword("targets", after->end_line, after->end_column, p_declaration->end_line, p_declaration->end_column + 1);
		}
	}
}

void DocumentClassifier::walk_trait_use(const FSParser::ClassNode::TraitUse &p_use, const FSParser::Node *p_owner) {
	if (p_use.name.is_empty()) {
		return;
	}
	if (p_owner != nullptr) {
		add_last_contextual_keyword("uses", p_owner, p_use.name[0]);
	}
	FSParser::DataType datatype;
	if (p_use.resolved_trait != nullptr) {
		datatype.kind = FSParser::DataType::CLASS;
		datatype.class_type = p_use.resolved_trait;
	}
	classify_chain(p_use.name, datatype, TokenType::INTERFACE, false);
	for (const FSParser::TypeNode *argument : p_use.type_arguments) {
		walk_type(argument);
	}
}

void DocumentClassifier::walk_class(const FSParser::ClassNode *p_class) {
	if (p_class == nullptr) {
		return;
	}
	const FSParser::ClassNode *previous_class = current_class;
	current_class = p_class;
	walk_annotations(p_class);

	uint32_t modifiers = bit(TokenModifier::DECLARATION);
	if (p_class->is_abstract) {
		modifiers |= bit(TokenModifier::ABSTRACT);
	}
	if (p_class->is_final) {
		modifiers |= bit(TokenModifier::FINAL);
	}
	// An `enum_name`/`tuple_name` file has a synthetic head class that borrows the declaration's
	// identifier, so classifying it as a class would rename the only declaration in the file.
	if (p_class->is_enum_file && p_class->enum_file_decl != nullptr) {
		walk_enum(p_class->enum_file_decl);
	} else if (p_class->is_tuple_file && p_class->tuple_file_decl != nullptr) {
		walk_tuple(p_class->tuple_file_decl);
	} else {
		add_identifier(p_class->identifier, p_class->is_trait ? TokenType::INTERFACE : TokenType::CLASS, modifiers);
	}

	for (const FSParser::TypeParameterNode *type_parameter : p_class->type_parameters) {
		walk_type_parameter(type_parameter);
	}
	classify_chain(p_class->extends, p_class->base_type, TokenType::CLASS, false);
	for (const FSParser::TypeNode *argument : p_class->extends_type_arguments) {
		walk_type(argument);
	}
	for (const FSParser::ClassNode::TraitUse &use : p_class->used_traits) {
		walk_trait_use(use, p_class);
	}

	for (const FSParser::ClassNode::Member &member : p_class->members) {
		switch (member.type) {
			case FSParser::ClassNode::Member::CLASS:
				walk_class(member.m_class);
				break;
			case FSParser::ClassNode::Member::CONSTANT:
				walk_constant(member.constant, true);
				break;
			case FSParser::ClassNode::Member::FUNCTION:
				walk_function(member.function, false);
				break;
			case FSParser::ClassNode::Member::SIGNAL:
				walk_signal(member.signal);
				break;
			case FSParser::ClassNode::Member::VARIABLE:
				walk_variable(member.variable, true);
				break;
			case FSParser::ClassNode::Member::ENUM:
				walk_enum(member.m_enum);
				break;
			case FSParser::ClassNode::Member::ENUM_VALUE:
				walk_enum_value(member.enum_value);
				break;
			case FSParser::ClassNode::Member::GROUP:
				walk_annotation(member.annotation);
				break;
			case FSParser::ClassNode::Member::TUPLE:
				walk_tuple(member.m_tuple);
				break;
			case FSParser::ClassNode::Member::UNDEFINED:
				break;
		}
	}

	for (const FSParser::AnnotationDeclarationNode *declaration : p_class->annotation_declarations) {
		walk_annotation_declaration(declaration);
	}
	for (const FSParser::ConformanceNode *conformance : p_class->conformances) {
		walk_conformance(conformance);
	}
	current_class = previous_class;
}

void DocumentClassifier::walk_conformance(const FSParser::ConformanceNode *p_conformance) {
	if (p_conformance == nullptr) {
		return;
	}
	walk_annotations(p_conformance);
	add_word(p_conformance->start_line, p_conformance->start_column, "extend", TokenType::KEYWORD, 0, RANK_CONTEXTUAL);
	walk_type(p_conformance->target);
	for (const FSParser::ClassNode::TraitUse &use : p_conformance->traits) {
		walk_trait_use(use, p_conformance);
	}

	const FSParser::ClassNode *previous_class = current_class;
	const FSParser::DataType previous_dispatch_type = current_dispatch_type;
	const bool previous_has_dispatch_type = has_current_dispatch_type;
	const FSParser::DataType target_type = p_conformance->target != nullptr ? p_conformance->target->get_datatype() : FSParser::DataType();
	if (target_type.kind == FSParser::DataType::CLASS) {
		current_class = target_type.class_type;
		has_current_dispatch_type = false;
	} else if (target_type.kind == FSParser::DataType::NATIVE && p_conformance->native_target_shim != nullptr) {
		current_class = p_conformance->native_target_shim;
		has_current_dispatch_type = false;
	} else if (target_type.kind == FSParser::DataType::BUILTIN && p_conformance->builtin_target_shim != nullptr) {
		current_class = p_conformance->builtin_target_shim;
		has_current_dispatch_type = false;
	} else if (p_conformance->target != nullptr) {
		// Cross-file script targets do not expose their parser node here. Keep their analyzed
		// DataType so bare-call fallback can still distinguish script members from the native base.
		current_class = nullptr;
		current_dispatch_type = target_type;
		has_current_dispatch_type = true;
	}
	for (const FSParser::FunctionNode *witness : p_conformance->witnesses) {
		walk_function(witness, false);
	}
	current_class = previous_class;
	current_dispatch_type = previous_dispatch_type;
	has_current_dispatch_type = previous_has_dispatch_type;
}

void DocumentClassifier::walk_constant(const FSParser::ConstantNode *p_constant, bool p_is_member) {
	if (p_constant == nullptr) {
		return;
	}
	walk_annotations(p_constant);
	add_identifier(p_constant->identifier, p_is_member ? TokenType::PROPERTY : TokenType::VARIABLE,
			bit(TokenModifier::DECLARATION) | bit(TokenModifier::READONLY));
	walk_type(p_constant->datatype_specifier);
	walk_expression(p_constant->initializer);
}

void DocumentClassifier::walk_enum(const FSParser::EnumNode *p_enum) {
	if (p_enum == nullptr) {
		return;
	}
	walk_annotations(p_enum);
	add_identifier(p_enum->identifier, TokenType::ENUM, bit(TokenModifier::DECLARATION));
	for (const FSParser::EnumNode::Value &value : p_enum->values) {
		walk_enum_value(value);
	}
	for (const FSParser::FunctionNode *function : p_enum->functions) {
		walk_function(function, false);
	}
}

void DocumentClassifier::walk_enum_value(const FSParser::EnumNode::Value &p_value) {
	add_identifier(p_value.identifier, TokenType::ENUM_MEMBER, bit(TokenModifier::DECLARATION) | bit(TokenModifier::READONLY));
	for (const FSParser::EnumNode::PayloadField &field : p_value.payload_fields) {
		add_identifier(field.identifier, TokenType::PROPERTY, bit(TokenModifier::DECLARATION) | bit(TokenModifier::READONLY));
		walk_type(field.type);
	}
	walk_expression(p_value.custom_value);
}

void DocumentClassifier::walk_function(const FSParser::FunctionNode *p_function, bool p_is_lambda) {
	if (p_function == nullptr) {
		return;
	}
	walk_annotations(p_function);

	uint32_t modifiers = bit(TokenModifier::DECLARATION);
	if (p_function->is_static) {
		modifiers |= bit(TokenModifier::STATIC);
	}
	if (p_function->is_abstract) {
		modifiers |= bit(TokenModifier::ABSTRACT);
	}
	if (p_function->is_final) {
		modifiers |= bit(TokenModifier::FINAL);
	}
	if (p_function->is_declared_async) {
		modifiers |= bit(TokenModifier::ASYNC);
		// `async` is an ordinary identifier everywhere but directly in front of a function's
		// modifiers, so only a declaration the parser accepted as async may claim it. The node
		// starts at `func`, after the modifiers, so the search starts at the head of that line.
		const FSParser::Node *until = p_function->identifier != nullptr
				? static_cast<const FSParser::Node *>(p_function->identifier)
				: static_cast<const FSParser::Node *>(p_function);
		add_contextual_keyword("async", p_function->start_line, 1, until->start_line, until->start_column);
	}
	add_identifier(p_function->identifier, p_is_lambda ? TokenType::FUNCTION : TokenType::METHOD, modifiers);

	for (const FSParser::TypeParameterNode *type_parameter : p_function->type_parameters) {
		walk_type_parameter(type_parameter);
	}
	for (const FSParser::ParameterNode *parameter : p_function->parameters) {
		walk_parameter(parameter);
	}
	walk_parameter(p_function->rest_parameter);
	walk_type(p_function->return_type);
	walk_suite(p_function->body);
}

void DocumentClassifier::walk_parameter(const FSParser::ParameterNode *p_parameter) {
	if (p_parameter == nullptr) {
		return;
	}
	walk_annotations(p_parameter);
	add_identifier(p_parameter->identifier, TokenType::PARAMETER, bit(TokenModifier::DECLARATION));
	walk_type(p_parameter->datatype_specifier);
	walk_expression(p_parameter->initializer);
}

void DocumentClassifier::walk_pattern(const FSParser::PatternNode *p_pattern) {
	if (p_pattern == nullptr) {
		return;
	}
	switch (p_pattern->pattern_type) {
		case FSParser::PatternNode::PT_LITERAL:
			walk_expression(p_pattern->literal);
			break;
		case FSParser::PatternNode::PT_EXPRESSION:
			walk_expression(p_pattern->expression);
			break;
		case FSParser::PatternNode::PT_BIND:
			add_identifier(p_pattern->bind, TokenType::VARIABLE, bit(TokenModifier::DECLARATION));
			break;
		case FSParser::PatternNode::PT_DICTIONARY:
			for (const FSParser::PatternNode::Pair &pair : p_pattern->dictionary) {
				walk_expression(pair.key);
				walk_pattern(pair.value_pattern);
			}
			break;
		case FSParser::PatternNode::PT_ENUM_CASE:
			walk_type(p_pattern->case_type);
			break;
		default:
			break;
	}
	for (const FSParser::PatternNode *sub_pattern : p_pattern->array) {
		walk_pattern(sub_pattern);
	}
}

void DocumentClassifier::walk_signal(const FSParser::SignalNode *p_signal) {
	if (p_signal == nullptr) {
		return;
	}
	walk_annotations(p_signal);
	add_identifier(p_signal->identifier, TokenType::EVENT, bit(TokenModifier::DECLARATION));
	for (const FSParser::ParameterNode *parameter : p_signal->parameters) {
		walk_parameter(parameter);
	}
}

void DocumentClassifier::walk_subscript(const FSParser::SubscriptNode *p_subscript, bool p_skip_attribute) {
	if (p_subscript == nullptr) {
		return;
	}
	walk_expression(p_subscript->base);

	if (!p_subscript->is_attribute) {
		// `index` aliases the first bracket argument, and the parallel vector is only filled for a
		// multi-argument or nullable list, so the two spellings describe one argument list.
		Vector<const FSParser::ExpressionNode *> arguments;
		if (!p_subscript->type_arguments.is_empty()) {
			for (const FSParser::ExpressionNode *argument : p_subscript->type_arguments) {
				arguments.push_back(argument);
			}
		} else if (p_subscript->index != nullptr) {
			arguments.push_back(p_subscript->index);
		}

		// A generic specialization written as an expression (`Box[int].new()`) reaches the analyzer
		// as an ordinary subscript, so the bracket contents are only known to be types through the
		// resolved type arguments of the specialization itself.
		const FSParser::DataType subscript_type = p_subscript->get_datatype();
		const bool is_specialization = subscript_type.is_meta_type &&
				subscript_type.type_arguments.size() == arguments.size();

		for (int i = 0; i < arguments.size(); i++) {
			walk_expression(arguments[i]);
			if (!is_specialization || arguments[i]->type != FSParser::Node::IDENTIFIER) {
				continue;
			}
			const FSParser::IdentifierNode *argument = static_cast<const FSParser::IdentifierNode *>(arguments[i]);
			if (argument->source != FSParser::IdentifierNode::UNDEFINED_SOURCE || argument->get_datatype().is_meta_type) {
				continue;
			}
			const TypeClassification classification = classify_datatype(subscript_type.type_arguments[i]);
			if (classification.valid) {
				add_identifier(argument, classification.type, classification.modifiers);
			}
		}
		return;
	}
	if (p_skip_attribute || p_subscript->attribute == nullptr) {
		return;
	}

	const FSParser::DataType base_type = p_subscript->base != nullptr ? p_subscript->base->get_datatype() : FSParser::DataType();
	const FSParser::DataType attribute_type = p_subscript->attribute->get_datatype();

	uint32_t modifiers = 0;
	// Variant attributes and Dictionary dot keys are dynamically supplied by user data. The
	// receiver's library-owned type does not make an arbitrary member a library symbol.
	const bool has_dynamic_attributes = base_type.kind == FSParser::DataType::VARIANT ||
			(base_type.kind == FSParser::DataType::BUILTIN && base_type.builtin_type == Variant::DICTIONARY);
	if (!has_dynamic_attributes && (is_default_library_type(base_type) || datatype_has_native_property(base_type, p_subscript->attribute->name) || datatype_has_native_signal(base_type, p_subscript->attribute->name))) {
		modifiers |= bit(TokenModifier::DEFAULT_LIBRARY);
	}

	// A member the analyzer already resolved says what it is; the data type of a method or signal
	// used as a value is only `Callable`/`Signal`, which a property could carry as well. A
	// `MEMBER_FUNCTION`/`MEMBER_SIGNAL` source is always a project (or external-script) declaration --
	// native lookups reach here through `INHERITED_VARIABLE` -- so ownership modifiers come from the
	// resolved declaration, never from whether the receiver spelling happens to be a type handle.
	if (p_subscript->attribute->source == FSParser::IdentifierNode::MEMBER_FUNCTION) {
		const bool is_static = p_subscript->attribute->function_source_is_static ||
				(p_subscript->attribute->function_source != nullptr && p_subscript->attribute->function_source->is_static);
		if (is_static) {
			modifiers |= bit(TokenModifier::STATIC);
		}
		add_identifier(p_subscript->attribute, TokenType::METHOD, modifiers & ~bit(TokenModifier::DEFAULT_LIBRARY));
		return;
	}
	if (p_subscript->attribute->source == FSParser::IdentifierNode::MEMBER_SIGNAL) {
		add_identifier(p_subscript->attribute, TokenType::EVENT, modifiers & ~bit(TokenModifier::DEFAULT_LIBRARY));
		return;
	}
	if (p_subscript->attribute->source == FSParser::IdentifierNode::MEMBER_VARIABLE ||
			p_subscript->attribute->source == FSParser::IdentifierNode::STATIC_VARIABLE) {
		if (p_subscript->attribute->source == FSParser::IdentifierNode::STATIC_VARIABLE) {
			modifiers |= bit(TokenModifier::STATIC);
		}
		if (attribute_type.is_constant || attribute_type.is_read_only) {
			modifiers |= bit(TokenModifier::READONLY);
		}
		add_identifier(p_subscript->attribute, TokenType::PROPERTY, modifiers);
		return;
	}
	if (attribute_type.is_meta_type) {
		const TypeClassification classification = classify_datatype(attribute_type);
		if (classification.valid) {
			add_identifier(p_subscript->attribute, classification.type, classification.modifiers);
		}
		return;
	}
	if (base_type.is_meta_type && base_type.kind == FSParser::DataType::ENUM) {
		add_identifier(p_subscript->attribute, TokenType::ENUM_MEMBER, modifiers | bit(TokenModifier::READONLY));
		return;
	}
	const DeclaredSymbol declared_symbol = resolve_declared_symbol(p_subscript);
	if (classify_declared_symbol(p_subscript->attribute, declared_symbol)) {
		return;
	}
	if (datatype_has_native_method(base_type, p_subscript->attribute->name)) {
		if (base_type.is_meta_type) {
			modifiers |= bit(TokenModifier::STATIC);
		}
		add_identifier(p_subscript->attribute, TokenType::METHOD, modifiers | bit(TokenModifier::DEFAULT_LIBRARY));
		return;
	}
	if (attribute_type.kind == FSParser::DataType::BUILTIN && attribute_type.builtin_type == Variant::SIGNAL) {
		add_identifier(p_subscript->attribute, TokenType::EVENT, modifiers);
		return;
	}
	if (base_type.is_meta_type) {
		modifiers |= bit(TokenModifier::STATIC);
	}
	if (attribute_type.is_constant || attribute_type.is_read_only) {
		modifiers |= bit(TokenModifier::READONLY);
	}
	add_identifier(p_subscript->attribute, TokenType::PROPERTY, modifiers);
}

void DocumentClassifier::walk_suite(const FSParser::SuiteNode *p_suite) {
	if (p_suite == nullptr) {
		return;
	}
	for (const FSParser::Node *statement : p_suite->statements) {
		walk_node(statement);
	}
}

void DocumentClassifier::walk_tuple(const FSParser::TupleNode *p_tuple) {
	if (p_tuple == nullptr) {
		return;
	}
	walk_annotations(p_tuple);
	add_identifier(p_tuple->identifier, TokenType::STRUCT, bit(TokenModifier::DECLARATION));
	for (const FSParser::TupleNode::Field &field : p_tuple->fields) {
		// Tuple elements are immutable once built, so a field is always read-only.
		add_identifier(field.identifier, TokenType::PROPERTY, bit(TokenModifier::DECLARATION) | bit(TokenModifier::READONLY));
		walk_type(field.type);
	}
}

void DocumentClassifier::walk_type(const FSParser::TypeNode *p_type) {
	if (p_type == nullptr) {
		return;
	}
	classify_chain(p_type->type_chain, p_type->get_datatype(), TokenType::TYPE, p_type->allows_enum_case);
	for (const FSParser::TypeNode *element : p_type->tuple_element_types) {
		walk_type(element);
	}
	for (const FSParser::TypeNode *container : p_type->container_types) {
		walk_type(container);
	}
	for (const FSParser::TypeNode *parameter : p_type->signature_parameter_types) {
		walk_type(parameter);
	}
	walk_type(p_type->signature_return_type);
}

void DocumentClassifier::walk_type_parameter(const FSParser::TypeParameterNode *p_type_parameter) {
	if (p_type_parameter == nullptr) {
		return;
	}
	add_identifier(p_type_parameter->identifier, TokenType::TYPE_PARAMETER, bit(TokenModifier::DECLARATION));
	walk_type(p_type_parameter->bound);
}

void DocumentClassifier::walk_variable(const FSParser::VariableNode *p_variable, bool p_is_member) {
	if (p_variable == nullptr) {
		return;
	}
	walk_annotations(p_variable);

	uint32_t modifiers = bit(TokenModifier::DECLARATION);
	if (p_variable->is_static) {
		modifiers |= bit(TokenModifier::STATIC);
	}
	if (p_variable->is_final) {
		modifiers |= bit(TokenModifier::FINAL) | bit(TokenModifier::READONLY);
	}
	add_identifier(p_variable->identifier, p_is_member ? TokenType::PROPERTY : TokenType::VARIABLE, modifiers);
	walk_type(p_variable->datatype_specifier);
	walk_expression(p_variable->initializer);

	switch (p_variable->property) {
		case FSParser::VariableNode::PROP_NONE:
			break;
		case FSParser::VariableNode::PROP_INLINE: {
			// The accessor functions carry a synthesized name, so only the `get`/`set` word they
			// start with is classifiable, and it is a keyword only here.
			if (p_variable->getter != nullptr) {
				add_word(p_variable->getter->start_line, p_variable->getter->start_column, "get", TokenType::KEYWORD, 0, RANK_CONTEXTUAL);
				walk_suite(p_variable->getter->body);
			}
			if (p_variable->setter != nullptr) {
				add_word(p_variable->setter->start_line, p_variable->setter->start_column, "set", TokenType::KEYWORD, 0, RANK_CONTEXTUAL);
				add_identifier(p_variable->setter_parameter, TokenType::PARAMETER, bit(TokenModifier::DECLARATION));
				walk_suite(p_variable->setter->body);
			}
		} break;
		case FSParser::VariableNode::PROP_SETGET: {
			if (p_variable->getter_pointer != nullptr) {
				add_last_contextual_keyword("get", p_variable, p_variable->getter_pointer);
				add_identifier(p_variable->getter_pointer, TokenType::METHOD, 0);
			}
			if (p_variable->setter_pointer != nullptr) {
				add_last_contextual_keyword("set", p_variable, p_variable->setter_pointer);
				add_identifier(p_variable->setter_pointer, TokenType::METHOD, 0);
			}
		} break;
	}
}

void DocumentClassifier::walk_call(const FSParser::CallNode *p_call) {
	if (p_call == nullptr) {
		return;
	}
	for (const FSParser::ExpressionNode *argument : p_call->arguments) {
		walk_expression(argument);
	}

	const FSParser::ExpressionNode *callee = p_call->callee;
	if (callee == nullptr) {
		return;
	}
	if (callee->type == FSParser::Node::SUBSCRIPT) {
		const FSParser::SubscriptNode *subscript = static_cast<const FSParser::SubscriptNode *>(callee);
		if (subscript->is_attribute && subscript->attribute != nullptr) {
			const FSParser::DataType attribute_type = subscript->attribute->get_datatype();
			// A tagged-union case constructor and a named-tuple constructor are called like
			// functions but name a value shape, not a method.
			if (p_call->is_enum_case_construction || (attribute_type.kind == FSParser::DataType::ENUM && attribute_type.enum_case_name != StringName())) {
				walk_subscript(subscript, false);
				return;
			}
			if (p_call->is_tuple_construction) {
				walk_subscript(subscript, true);
				add_identifier(subscript->attribute, TokenType::STRUCT, 0);
				return;
			}
			walk_subscript(subscript, true);
			const FSParser::DataType base_type = subscript->base != nullptr ? subscript->base->get_datatype() : FSParser::DataType();
			const FSParser::IdentifierNode::Source attribute_source = subscript->attribute->source;
			const bool is_resolved_property = attribute_source == FSParser::IdentifierNode::MEMBER_VARIABLE ||
					attribute_source == FSParser::IdentifierNode::STATIC_VARIABLE ||
					(attribute_source == FSParser::IdentifierNode::INHERITED_VARIABLE &&
							datatype_has_native_property(base_type, subscript->attribute->name));
			if (is_resolved_property) {
				walk_subscript(subscript, false);
				return;
			}
			uint32_t modifiers = 0;
			// A library-owned receiver can still expose a project-owned retroactive-conformance
			// witness. Only a method found on the native surface (or the built-in constructor
			// spelling, which has no MethodBind) carries defaultLibrary.
			const bool is_default_library_constructor = base_type.is_meta_type &&
					subscript->attribute->name == SNAME("new") && is_default_library_type(base_type);
			if (is_default_library_constructor || datatype_has_native_method(base_type, subscript->attribute->name)) {
				modifiers |= bit(TokenModifier::DEFAULT_LIBRARY);
			}
			// The analyzer records static dispatch on the call even when the receiver identifier's
			// data type does not retain its meta-type flag (notably built-in type names).
			if (p_call->is_static || base_type.is_meta_type) {
				modifiers |= bit(TokenModifier::STATIC);
			}
			if (attribute_type.is_meta_type) {
				// `Outer.Inner()` constructs a type rather than calling a method.
				walk_subscript(subscript, false);
				return;
			}
			const DeclaredSymbol declared_symbol = resolve_declared_symbol(subscript);
			if (declared_symbol.kind != DECLARED_SYMBOL_NONE) {
				walk_subscript(subscript, false);
				return;
			}
			add_identifier(subscript->attribute, TokenType::METHOD, modifiers);
			return;
		}
		walk_subscript(subscript, false);
		return;
	}

	if (callee->type != FSParser::Node::IDENTIFIER) {
		walk_expression(callee);
		return;
	}

	const FSParser::IdentifierNode *identifier = static_cast<const FSParser::IdentifierNode *>(callee);
	if (p_call->is_tuple_construction) {
		// A named tuple is built through call syntax, but the callee names the tuple declaration
		// itself; the analyzer types it as the constructed value, not as a type handle.
		add_identifier(identifier, TokenType::STRUCT, 0);
		return;
	}
	// Project declarations follow the analyzer's scope lookup and take precedence over utilities
	// with the same spelling.
	if (classify_identifier_reference(identifier)) {
		return;
	}
	if (identifier->source == FSParser::IdentifierNode::UNDEFINED_SOURCE && !identifier->get_datatype().is_meta_type) {
		// Utility functions are the language's only callables that are neither members nor locals.
		if (FSUtilityFunctions::function_exists(identifier->name) || Variant::has_utility_function(identifier->name)) {
			add_identifier(identifier, TokenType::FUNCTION, bit(TokenModifier::DEFAULT_LIBRARY));
			return;
		}
	}
	// A bare call names a method of the enclosing class. The analyzer resolves the dispatch
	// without rewriting the callee identifier's source, so nothing else identifies it.
	const uint32_t modifiers = current_class_has_native_method(identifier->name)
			? bit(TokenModifier::DEFAULT_LIBRARY)
			: 0;
	add_identifier(identifier, TokenType::METHOD, modifiers);
}

void DocumentClassifier::walk_expression(const FSParser::ExpressionNode *p_expression) {
	if (p_expression == nullptr) {
		return;
	}
	walk_annotations(p_expression);

	switch (p_expression->type) {
		case FSParser::Node::IDENTIFIER:
			classify_identifier_reference(static_cast<const FSParser::IdentifierNode *>(p_expression));
			break;
		case FSParser::Node::ARRAY: {
			const FSParser::ArrayNode *array = static_cast<const FSParser::ArrayNode *>(p_expression);
			for (const FSParser::ExpressionNode *element : array->elements) {
				walk_expression(element);
			}
		} break;
		case FSParser::Node::TUPLE_LITERAL: {
			const FSParser::TupleLiteralNode *tuple = static_cast<const FSParser::TupleLiteralNode *>(p_expression);
			for (const FSParser::ExpressionNode *element : tuple->elements) {
				walk_expression(element);
			}
		} break;
		case FSParser::Node::DICTIONARY: {
			const FSParser::DictionaryNode *dictionary = static_cast<const FSParser::DictionaryNode *>(p_expression);
			for (const FSParser::DictionaryNode::Pair &pair : dictionary->elements) {
				walk_expression(pair.key);
				walk_expression(pair.value);
			}
		} break;
		case FSParser::Node::ASSIGNMENT: {
			const FSParser::AssignmentNode *assignment = static_cast<const FSParser::AssignmentNode *>(p_expression);
			walk_expression(assignment->assignee);
			walk_expression(assignment->assigned_value);
		} break;
		case FSParser::Node::AWAIT:
			walk_expression(static_cast<const FSParser::AwaitNode *>(p_expression)->to_await);
			break;
		case FSParser::Node::BINARY_OPERATOR: {
			const FSParser::BinaryOpNode *binary = static_cast<const FSParser::BinaryOpNode *>(p_expression);
			walk_expression(binary->left_operand);
			walk_expression(binary->right_operand);
		} break;
		case FSParser::Node::UNARY_OPERATOR:
			walk_expression(static_cast<const FSParser::UnaryOpNode *>(p_expression)->operand);
			break;
		case FSParser::Node::TERNARY_OPERATOR: {
			const FSParser::TernaryOpNode *ternary = static_cast<const FSParser::TernaryOpNode *>(p_expression);
			walk_expression(ternary->condition);
			walk_expression(ternary->true_expr);
			walk_expression(ternary->false_expr);
		} break;
		case FSParser::Node::CAST: {
			const FSParser::CastNode *cast = static_cast<const FSParser::CastNode *>(p_expression);
			walk_expression(cast->operand);
			walk_type(cast->cast_type);
		} break;
		case FSParser::Node::TYPE_TEST: {
			const FSParser::TypeTestNode *test = static_cast<const FSParser::TypeTestNode *>(p_expression);
			walk_expression(test->operand);
			walk_type(test->test_type);
			for (const FSParser::IdentifierNode *bind : test->case_binds) {
				add_identifier(bind, TokenType::VARIABLE, bit(TokenModifier::DECLARATION));
			}
		} break;
		case FSParser::Node::CALL:
			walk_call(static_cast<const FSParser::CallNode *>(p_expression));
			break;
		case FSParser::Node::SUBSCRIPT:
			walk_subscript(static_cast<const FSParser::SubscriptNode *>(p_expression), false);
			break;
		case FSParser::Node::LAMBDA:
			walk_function(static_cast<const FSParser::LambdaNode *>(p_expression)->function, true);
			break;
		case FSParser::Node::PRELOAD:
			walk_expression(static_cast<const FSParser::PreloadNode *>(p_expression)->path);
			break;
		default:
			break;
	}
}

void DocumentClassifier::walk_node(const FSParser::Node *p_node) {
	if (p_node == nullptr) {
		return;
	}

	switch (p_node->type) {
		case FSParser::Node::CLASS:
			walk_class(static_cast<const FSParser::ClassNode *>(p_node));
			return;
		case FSParser::Node::VARIABLE:
			walk_variable(static_cast<const FSParser::VariableNode *>(p_node), false);
			return;
		case FSParser::Node::CONSTANT:
			walk_constant(static_cast<const FSParser::ConstantNode *>(p_node), false);
			return;
		case FSParser::Node::VARIABLE_DESTRUCTURE: {
			const FSParser::VariableDestructureNode *destructure = static_cast<const FSParser::VariableDestructureNode *>(p_node);
			walk_annotations(destructure);
			for (const FSParser::VariableNode *binding : destructure->bindings) {
				walk_variable(binding, false);
			}
			walk_expression(destructure->initializer);
			return;
		}
		case FSParser::Node::FUNCTION:
			walk_function(static_cast<const FSParser::FunctionNode *>(p_node), false);
			return;
		case FSParser::Node::SUITE:
			walk_suite(static_cast<const FSParser::SuiteNode *>(p_node));
			return;
		case FSParser::Node::IF: {
			const FSParser::IfNode *node = static_cast<const FSParser::IfNode *>(p_node);
			walk_expression(node->condition);
			walk_suite(node->true_block);
			walk_suite(node->false_block);
			return;
		}
		case FSParser::Node::WHILE: {
			const FSParser::WhileNode *node = static_cast<const FSParser::WhileNode *>(p_node);
			walk_expression(node->condition);
			walk_suite(node->loop);
			return;
		}
		case FSParser::Node::FOR: {
			const FSParser::ForNode *node = static_cast<const FSParser::ForNode *>(p_node);
			add_identifier(node->variable, TokenType::VARIABLE, bit(TokenModifier::DECLARATION));
			walk_type(node->datatype_specifier);
			walk_expression(node->list);
			walk_suite(node->loop);
			return;
		}
		case FSParser::Node::MATCH: {
			const FSParser::MatchNode *node = static_cast<const FSParser::MatchNode *>(p_node);
			// `match` is accepted as an ordinary identifier, so only a real match statement -- which
			// is exactly what this node is -- may claim the word as a keyword.
			add_word(node->start_line, node->start_column, "match", TokenType::KEYWORD, 0, RANK_CONTEXTUAL);
			walk_expression(node->test);
			for (const FSParser::MatchBranchNode *branch : node->branches) {
				walk_node(branch);
			}
			return;
		}
		case FSParser::Node::MATCH_BRANCH: {
			const FSParser::MatchBranchNode *node = static_cast<const FSParser::MatchBranchNode *>(p_node);
			for (const FSParser::PatternNode *pattern : node->patterns) {
				walk_pattern(pattern);
			}
			if (node->guard_body != nullptr) {
				// `when` sits between the last pattern and the branch body. Bounding the search by
				// the patterns keeps a `when` written as an ordinary name inside one of them --
				// the tokenizer accepts it there -- from being mistaken for the guard.
				const FSParser::Node *after_patterns = node->patterns.is_empty()
						? static_cast<const FSParser::Node *>(node)
						: static_cast<const FSParser::Node *>(node->patterns[node->patterns.size() - 1]);
				const int to_line = node->block != nullptr ? node->block->start_line : node->end_line;
				const int to_column = node->block != nullptr ? node->block->start_column : node->end_column;
				add_contextual_keyword("when", after_patterns->end_line, after_patterns->end_column, to_line, to_column);
				walk_suite(node->guard_body);
			}
			walk_suite(node->block);
			return;
		}
		case FSParser::Node::RETURN:
			walk_expression(static_cast<const FSParser::ReturnNode *>(p_node)->return_value);
			return;
		case FSParser::Node::ASSERT: {
			const FSParser::AssertNode *node = static_cast<const FSParser::AssertNode *>(p_node);
			walk_expression(node->condition);
			walk_expression(node->message);
			return;
		}
		case FSParser::Node::ANNOTATION:
			walk_annotation(static_cast<const FSParser::AnnotationNode *>(p_node));
			return;
		case FSParser::Node::PATTERN:
			walk_pattern(static_cast<const FSParser::PatternNode *>(p_node));
			return;
		default:
			break;
	}

	if (p_node->is_expression()) {
		walk_expression(static_cast<const FSParser::ExpressionNode *>(p_node));
		return;
	}
	walk_annotations(p_node);
}

} // namespace

uint32_t FSSemanticTokens::modifier_bit(LSP::SemanticTokenModifier p_modifier) {
	ERR_FAIL_COND_V(p_modifier >= LSP::SemanticTokenModifier::MAX, 0);
	return 1u << static_cast<uint32_t>(p_modifier);
}

int FSSemanticTokens::utf16_length(const String &p_text) {
	int length = 0;
	for (int i = 0; i < p_text.length(); i++) {
		// Code points outside the Basic Multilingual Plane encode as a surrogate pair.
		length += p_text[i] > char32_t(0xFFFF) ? 2 : 1;
	}
	return length;
}

int FSSemanticTokens::code_point_column_to_utf16_column(const String &p_line, int p_code_point_column) {
	const int limit = MIN(p_code_point_column, p_line.length());
	int column = 0;
	for (int i = 0; i < limit; i++) {
		column += p_line[i] > char32_t(0xFFFF) ? 2 : 1;
	}
	return column;
}

PackedInt32Array FSSemanticTokens::encode(const Vector<Span> &p_spans, const Vector<String> &p_lines) {
	PackedInt32Array data;

	bool has_previous = false;
	int previous_line = 0;
	int previous_utf16_start = 0;
	// Tracked in source coordinates so overlap is rejected before any UTF-16 conversion happens.
	int previous_end_column = 0;

	for (const Span &span : p_spans) {
		if (span.line < 0 || span.line >= p_lines.size()) {
			continue;
		}
		if (span.start_column < 0 || span.length <= 0) {
			continue;
		}
		if (span.type >= LSP::SemanticTokenType::MAX) {
			continue;
		}

		const String &line = p_lines[span.line];
		if (span.start_column >= line.length()) {
			continue;
		}
		// Clamp rather than drop: a span whose producer over-measured the tail of a line still
		// describes a real token, it just cannot extend past the line it lives on.
		const int end_column = MIN(span.start_column + span.length, line.length());

		// Emitting a span that starts before the previous one ended would break the sorted,
		// non-overlapping guarantee the delta encoding depends on, and `deltaStart` would go
		// negative. Drop the offender instead of corrupting the whole stream.
		if (has_previous && span.line < previous_line) {
			continue;
		}
		if (has_previous && span.line == previous_line && span.start_column < previous_end_column) {
			continue;
		}

		const int utf16_start = code_point_column_to_utf16_column(line, span.start_column);
		const int utf16_end = code_point_column_to_utf16_column(line, end_column);

		const int delta_line = has_previous ? span.line - previous_line : span.line;
		const int delta_start = (has_previous && delta_line == 0) ? utf16_start - previous_utf16_start : utf16_start;

		data.push_back(delta_line);
		data.push_back(delta_start);
		data.push_back(utf16_end - utf16_start);
		data.push_back(static_cast<int>(span.type));
		data.push_back(static_cast<int>(span.modifiers));

		has_previous = true;
		previous_line = span.line;
		previous_utf16_start = utf16_start;
		previous_end_column = end_column;
	}

	return data;
}

Vector<String> FSSemanticTokens::split_lines(const String &p_source) {
	if (p_source.is_empty()) {
		return Vector<String>();
	}
	return p_source.split("\n", true);
}

Vector<FSSemanticTokens::Span> FSSemanticTokens::collect(const FSParser *p_parser, const String &p_source, const Vector<String> &p_lines) {
	if (p_source.is_empty()) {
		return Vector<Span>();
	}

	DocumentClassifier classifier(p_lines);
	classifier.scan_tokens(p_source);
	if (p_parser != nullptr) {
		classifier.classify_tree(p_parser->get_tree());
	}
	return classifier.resolve();
}
