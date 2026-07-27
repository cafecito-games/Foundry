/**************************************************************************/
/*  test_foundry_script.cpp                                               */
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

#include "test_foundry_script.h"

#include "fs_temporary_project_tree.h"

#ifdef TOOLS_ENABLED
#include "../editor/fs_docgen.h"
#include "../editor/fs_highlighter.h"
#endif
#include "../foundry_script.h"
#include "../fs_analyzer.h"
#include "../fs_byte_codegen.h"
#include "../fs_bytecode_verifier.h"
#include "../fs_cache.h"
#include "../fs_compiler.h"
#include "../fs_parser.h"
#include "../fs_reflection.h"
#include "../fs_script_test_guard.h"
#include "../fs_tokenizer.h"
#include "../fs_tokenizer_buffer.h"

#include "core/config/project_settings.h"
#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/io/resource_uid.h"
#include "core/os/os.h"
#include "core/string/string_builder.h"
#include "tests/test_tools.h"

#ifdef TOOLS_ENABLED
#include "editor/settings/editor_settings.h"
#include "scene/gui/text_edit.h"
#endif

namespace FSTests {

TEST_CASE("[Modules][FoundryScript] Language reserved words include namespace declarations") {
	Vector<String> reserved_words = FSLanguage::get_singleton()->get_reserved_words();
	CHECK(reserved_words.has("import"));
	CHECK(reserved_words.has("namespace"));
}

TEST_CASE("[Modules][FoundryScript] Language reserved words include hard trait declarations") {
	Vector<String> reserved_words = FSLanguage::get_singleton()->get_reserved_words();
	CHECK(reserved_words.has("enum_name"));
	CHECK(reserved_words.has("trait"));
	CHECK(reserved_words.has("trait_name"));
	CHECK_FALSE(reserved_words.has("uses"));
}

TEST_CASE("[Modules][FoundryScript] Language reserved words include tuple") {
	Vector<String> reserved_words = FSLanguage::get_singleton()->get_reserved_words();
	int tuple_count = 0;
	for (const String &word : reserved_words) {
		if (word == "tuple") {
			tuple_count++;
		}
	}
	CHECK(tuple_count == 1);
}

TEST_CASE("[Modules][FoundryScript] Tokenizer emits TUPLE for the tuple keyword") {
	FSTokenizerText tokenizer;
	tokenizer.set_source_code("tuple");
	FSTokenizer::Token token = tokenizer.scan();
	CHECK(token.type == FSTokenizer::Token::TUPLE);
	CHECK_EQ(token.get_name(), String("tuple"));
}

TEST_CASE("[Modules][FoundryScript] Tokenizer lexes a digit after a value token as a tuple index") {
	// After an IDENTIFIER (a value token), `.<digit>` is a `PERIOD` followed by a plain
	// decimal-integer literal, not the start of a float. This is what makes `t.0` tuple index
	// access viable instead of unconditionally lexing as the float `0.0`.
	FSTokenizerText tokenizer;
	tokenizer.set_source_code("t.0");

	FSTokenizer::Token identifier = tokenizer.scan();
	CHECK(identifier.type == FSTokenizer::Token::IDENTIFIER);
	CHECK_EQ(String(identifier.literal), String("t"));

	FSTokenizer::Token period = tokenizer.scan();
	CHECK(period.type == FSTokenizer::Token::PERIOD);

	FSTokenizer::Token index = tokenizer.scan();
	CHECK(index.type == FSTokenizer::Token::LITERAL);
	CHECK(index.literal.get_type() == Variant::INT);
	CHECK_EQ(int64_t(index.literal), 0);

	// The tokenizer inserts an implicit trailing NEWLINE before TK_EOF.
	FSTokenizer::Token newline = tokenizer.scan();
	CHECK(newline.type == FSTokenizer::Token::NEWLINE);

	FSTokenizer::Token eof = tokenizer.scan();
	CHECK(eof.type == FSTokenizer::Token::TK_EOF);
}

TEST_CASE("[Modules][FoundryScript] Tokenizer lexes chained tuple indices as nested member access") {
	// `x.0.1` must lex as `x`, `.`, `0`, `.`, `1` (nested tuple index access), not `x` followed
	// by the float `0.1`.
	FSTokenizerText tokenizer;
	tokenizer.set_source_code("x.0.1");

	FSTokenizer::Token::Type expected_types[] = {
		FSTokenizer::Token::IDENTIFIER,
		FSTokenizer::Token::PERIOD,
		FSTokenizer::Token::LITERAL,
		FSTokenizer::Token::PERIOD,
		FSTokenizer::Token::LITERAL,
		FSTokenizer::Token::NEWLINE, // Implicit trailing newline before TK_EOF.
		FSTokenizer::Token::TK_EOF,
	};
	for (const FSTokenizer::Token::Type expected_type : expected_types) {
		FSTokenizer::Token token = tokenizer.scan();
		CHECK(token.type == expected_type);
		if (token.type == FSTokenizer::Token::LITERAL) {
			CHECK(token.literal.get_type() == Variant::INT);
		}
	}

	FSTokenizerText reparsed;
	reparsed.set_source_code("x.0.1");
	reparsed.scan(); // x
	reparsed.scan(); // .
	FSTokenizer::Token first_index = reparsed.scan();
	CHECK_EQ(int64_t(first_index.literal), 0);
	reparsed.scan(); // .
	FSTokenizer::Token second_index = reparsed.scan();
	CHECK_EQ(int64_t(second_index.literal), 1);
}

TEST_CASE("[Modules][FoundryScript] Tokenizer leaves ordinary float literals unaffected by tuple index lexing") {
	struct Case {
		const char *source;
		double expected_value;
	};
	const Case cases[] = {
		{ ".5", 0.5 },
		{ "1.0", 1.0 },
		{ "0.5e3", 500.0 },
	};

	for (const Case &test_case : cases) {
		FSTokenizerText tokenizer;
		tokenizer.set_source_code(test_case.source);
		FSTokenizer::Token token = tokenizer.scan();
		CHECK_MESSAGE(token.type == FSTokenizer::Token::LITERAL, test_case.source);
		CHECK(token.literal.get_type() == Variant::FLOAT);
		CHECK_EQ(double(token.literal), test_case.expected_value);

		// The tokenizer inserts an implicit trailing NEWLINE before TK_EOF.
		FSTokenizer::Token newline = tokenizer.scan();
		CHECK(newline.type == FSTokenizer::Token::NEWLINE);

		FSTokenizer::Token eof = tokenizer.scan();
		CHECK(eof.type == FSTokenizer::Token::TK_EOF);
	}
}

TEST_CASE("[Modules][FoundryScript] Tokenizer treats a tuple index after grouping close as member access") {
	// `(f()).0` must behave like member access after `)`, exactly like after any other value
	// token, rather than lexing `.0` as a float.
	FSTokenizerText tokenizer;
	tokenizer.set_source_code("(f()).0");

	FSTokenizer::Token::Type expected_types[] = {
		FSTokenizer::Token::PARENTHESIS_OPEN,
		FSTokenizer::Token::IDENTIFIER,
		FSTokenizer::Token::PARENTHESIS_OPEN,
		FSTokenizer::Token::PARENTHESIS_CLOSE,
		FSTokenizer::Token::PARENTHESIS_CLOSE,
		FSTokenizer::Token::PERIOD,
		FSTokenizer::Token::LITERAL,
		FSTokenizer::Token::NEWLINE, // Implicit trailing newline before TK_EOF.
		FSTokenizer::Token::TK_EOF,
	};
	for (const FSTokenizer::Token::Type expected_type : expected_types) {
		FSTokenizer::Token token = tokenizer.scan();
		CHECK(token.type == expected_type);
		if (token.type == FSTokenizer::Token::LITERAL) {
			CHECK(token.literal.get_type() == Variant::INT);
			CHECK_EQ(int64_t(token.literal), 0);
		}
	}
}

TEST_CASE("[Modules][FoundryScript] Tokenizer treats whitespace before a tuple index as member access") {
	// Whitespace between the base expression and `.` does not change disambiguation: only real
	// tokens update `last_token`, so `x .0` is still member access, matching `x.0`.
	FSTokenizerText tokenizer;
	tokenizer.set_source_code("x .0");

	FSTokenizer::Token identifier = tokenizer.scan();
	CHECK(identifier.type == FSTokenizer::Token::IDENTIFIER);

	FSTokenizer::Token period = tokenizer.scan();
	CHECK(period.type == FSTokenizer::Token::PERIOD);

	FSTokenizer::Token index = tokenizer.scan();
	CHECK(index.type == FSTokenizer::Token::LITERAL);
	CHECK(index.literal.get_type() == Variant::INT);
	CHECK_EQ(int64_t(index.literal), 0);
}

TEST_CASE("[Modules][FoundryScript] Tokenizer keeps a tuple index inside a suppressed multiline newline") {
	// Inside `(...)`/`[...]`/`{...}` the parser puts the tokenizer in multiline mode so physical
	// newlines are not surfaced as `NEWLINE` tokens. `newline()` must not clobber `last_token`
	// with the suppressed newline in that mode, or a tuple index split across a line inside
	// grouping (`t` on one line, `.0` on the next) would wrongly disambiguate as a float.
	FSTokenizerText tokenizer;
	tokenizer.set_multiline_mode(true);
	tokenizer.set_source_code("t\n.0");

	FSTokenizer::Token identifier = tokenizer.scan();
	CHECK(identifier.type == FSTokenizer::Token::IDENTIFIER);

	FSTokenizer::Token period = tokenizer.scan();
	CHECK(period.type == FSTokenizer::Token::PERIOD);

	FSTokenizer::Token index = tokenizer.scan();
	CHECK(index.type == FSTokenizer::Token::LITERAL);
	CHECK(index.literal.get_type() == Variant::INT);
	CHECK_EQ(int64_t(index.literal), 0);
}

TEST_CASE("[Modules][FoundryScript] Tokenizer treats a tuple index after a keyword-spelled identifier as member access") {
	// `match`, `when`, and `uses` are keyword tokens that are still accepted as ordinary
	// identifiers (`Token::is_identifier()`); a value spelled with one of these names must
	// disambiguate a following `.<digit>` as member access exactly like any other identifier.
	const char *sources[] = { "match.0", "when.0", "uses.0" };
	const FSTokenizer::Token::Type keyword_types[] = {
		FSTokenizer::Token::MATCH,
		FSTokenizer::Token::WHEN,
		FSTokenizer::Token::USES,
	};

	for (int i = 0; i < 3; i++) {
		FSTokenizerText tokenizer;
		tokenizer.set_source_code(sources[i]);

		FSTokenizer::Token keyword = tokenizer.scan();
		CHECK_MESSAGE(keyword.type == keyword_types[i], sources[i]);

		FSTokenizer::Token period = tokenizer.scan();
		CHECK_MESSAGE(period.type == FSTokenizer::Token::PERIOD, sources[i]);

		FSTokenizer::Token index = tokenizer.scan();
		CHECK_MESSAGE(index.type == FSTokenizer::Token::LITERAL, sources[i]);
		CHECK(index.literal.get_type() == Variant::INT);
		CHECK_EQ(int64_t(index.literal), 0);
	}
}

TEST_CASE("[Modules][FoundryScript] TUPLE keyword is still valid as a node name and attribute name") {
	// `tuple` must remain usable after `$`/`.` even though it is now a dedicated keyword token,
	// matching how other hard keywords like `trait`/`trait_name` stay valid there.
	FSTokenizerText tokenizer;
	tokenizer.set_source_code("tuple");
	FSTokenizer::Token token = tokenizer.scan();
	CHECK(token.type == FSTokenizer::Token::TUPLE);
	CHECK(token.is_node_name());

	FSParser parser;
	Error err = parser.parse(R"(
func _ready():
	return $tuple
)",
			"user://tuple_node_name.fs", false);
	CHECK_EQ(err, OK);

	// `.tuple` attribute access must also keep parsing: `parse_attribute` re-spells node-name
	// keyword tokens as `IDENTIFIER` using the same `is_node_name()` set.
	FSParser attribute_parser;
	err = attribute_parser.parse(R"(
func _ready():
	return self.tuple
)",
			"user://tuple_attribute_name.fs", false);
	CHECK_EQ(err, OK);
}

TEST_CASE("[Modules][FoundryScript] Tokenizer rejects an exponent on a tuple index") {
	// `x.0e5` is not a valid tuple index (only a bare decimal integer is); the tokenizer must
	// report an error rather than silently reinterpreting it as a float.
	FSTokenizerText tokenizer;
	tokenizer.set_source_code("x.0e5");

	FSTokenizer::Token identifier = tokenizer.scan();
	CHECK(identifier.type == FSTokenizer::Token::IDENTIFIER);

	FSTokenizer::Token period = tokenizer.scan();
	CHECK(period.type == FSTokenizer::Token::PERIOD);

	FSTokenizer::Token index = tokenizer.scan();
	CHECK(index.type == FSTokenizer::Token::LITERAL);
	CHECK(index.literal.get_type() == Variant::INT);
	CHECK_EQ(int64_t(index.literal), 0);

	FSTokenizer::Token error = tokenizer.scan();
	CHECK(error.type == FSTokenizer::Token::ERROR);
	CHECK(String(error.literal).contains("tuple index"));
}

TEST_CASE("[Modules][FoundryScript] Tokenizer rejects a hex prefix on a tuple index") {
	// `x.0x1` is not a valid tuple index; the leading `0` must not be reinterpreted as a
	// hexadecimal prefix once it follows a `.` that disambiguated to member access.
	FSTokenizerText tokenizer;
	tokenizer.set_source_code("x.0x1");

	FSTokenizer::Token identifier = tokenizer.scan();
	CHECK(identifier.type == FSTokenizer::Token::IDENTIFIER);

	FSTokenizer::Token period = tokenizer.scan();
	CHECK(period.type == FSTokenizer::Token::PERIOD);

	FSTokenizer::Token index = tokenizer.scan();
	CHECK(index.type == FSTokenizer::Token::LITERAL);
	CHECK(index.literal.get_type() == Variant::INT);
	CHECK_EQ(int64_t(index.literal), 0);

	FSTokenizer::Token error = tokenizer.scan();
	CHECK(error.type == FSTokenizer::Token::ERROR);
	CHECK(String(error.literal).contains("tuple index"));
}

TEST_CASE("[Modules][FoundryScript] Tokenizer emits ENUM_NAME for the enum_name keyword") {
	CHECK_EQ(FSTokenizerBuffer::TOKENIZER_VERSION, 107);

	FSTokenizerText tokenizer;
	tokenizer.set_source_code("enum_name");
	FSTokenizer::Token token = tokenizer.scan();
	CHECK(token.type == FSTokenizer::Token::ENUM_NAME);
	CHECK_EQ(token.get_name(), String("enum_name"));
	CHECK(token.is_node_name());
}

TEST_CASE("[Modules][FoundryScript] Parser does not publish invalid enum_name identity") {
	struct Case {
		const char *source;
		StringName expected_name;
	};

	const Case cases[] = {
		{ "class_name Existing\nenum_name Invalid:\n\tA = 0\n", SNAME("Existing") },
		{ "trait_name Existing\nenum_name Invalid:\n\tA = 0\n", SNAME("Existing") },
		{ "extends RefCounted\nenum_name Invalid:\n\tA = 0\n", StringName() },
		{ "uses Existing\nenum_name Invalid:\n\tA = 0\n", StringName() },
		{ "@tool\nenum_name Invalid:\n\tA = 0\n", StringName() },
	};

	for (const Case &test_case : cases) {
		FSParser parser;
		const Error error = parser.parse(test_case.source, "user://invalid_enum_name_identity.fs", false, false);
		CHECK(error != OK);

		const FSParser::ClassNode *root = parser.get_tree();
		CHECK(root != nullptr);
		if (root == nullptr) {
			continue;
		}
		CHECK_FALSE(root->is_enum_file);
		CHECK(root->enum_file_decl == nullptr);
		if (test_case.expected_name == StringName()) {
			CHECK(root->identifier == nullptr);
			CHECK(root->qualified_global_name.is_empty());
			CHECK_EQ(root->fqcn, "user://invalid_enum_name_identity.fs");
		} else {
			CHECK(root->identifier != nullptr);
			if (root->identifier == nullptr) {
				continue;
			}
			CHECK_EQ(root->identifier->name, test_case.expected_name);
			CHECK_EQ(root->qualified_global_name, String(test_case.expected_name));
			CHECK_EQ(root->fqcn, String(test_case.expected_name));
		}
	}
}

TEST_CASE("[Modules][FoundryScript] Language reserved words include abstract but not async") {
	Vector<String> reserved_words = FSLanguage::get_singleton()->get_reserved_words();
	int abstract_count = 0;
	for (const String &word : reserved_words) {
		if (word == "abstract") {
			abstract_count++;
		}
	}
	CHECK(abstract_count == 1);
	CHECK_FALSE(reserved_words.has("async"));
}

TEST_CASE("[Modules][FoundryScript] Tokenizer emits ABSTRACT for the abstract keyword") {
	FSTokenizerText tokenizer;
	tokenizer.set_source_code("abstract");
	FSTokenizer::Token token = tokenizer.scan();
	CHECK(token.type == FSTokenizer::Token::ABSTRACT);
}

TEST_CASE("[Modules][FoundryScript] Tokenizer keeps tab indent source span inside the script buffer at EOF") {
	FSTokenizerText tokenizer;
	tokenizer.set_source_code("\t\t\t\tidentifier");

	FSTokenizer::Token indent = tokenizer.scan();
	CHECK(indent.type == FSTokenizer::Token::INDENT);
	CHECK_EQ(indent.source, String("\t\t\t\t"));

	FSTokenizer::Token identifier = tokenizer.scan();
	CHECK(identifier.type == FSTokenizer::Token::IDENTIFIER);
	CHECK_EQ(identifier.source, String("identifier"));
}

TEST_CASE("[Modules][FoundryScript] ABSTRACT keyword is still valid as a node name") {
	FSTokenizerText tokenizer;
	tokenizer.set_source_code("abstract");
	FSTokenizer::Token token = tokenizer.scan();
	CHECK(token.type == FSTokenizer::Token::ABSTRACT);
	CHECK(token.is_node_name());

	// `$abstract` must keep resolving to a node named `abstract` rather than
	// producing a parse error now that `abstract` is a keyword.
	FSParser parser;
	Error err = parser.parse(R"(
func _ready():
	return $abstract
)",
			"user://abstract_node_name.fs", false);
	CHECK_EQ(err, OK);
}

TEST_CASE("[Modules][FoundryScript] Language reserved words include final") {
	Vector<String> reserved_words = FSLanguage::get_singleton()->get_reserved_words();
	int final_count = 0;
	for (const String &word : reserved_words) {
		if (word == "final") {
			final_count++;
		}
	}
	CHECK(final_count == 1);
}

TEST_CASE("[Modules][FoundryScript] Tokenizer emits FINAL for the final keyword") {
	FSTokenizerText tokenizer;
	tokenizer.set_source_code("final");
	FSTokenizer::Token token = tokenizer.scan();
	CHECK(token.type == FSTokenizer::Token::FINAL);
}

TEST_CASE("[Modules][FoundryScript] FINAL keyword is still valid as a node name") {
	FSTokenizerText tokenizer;
	tokenizer.set_source_code("final");
	FSTokenizer::Token token = tokenizer.scan();
	CHECK(token.type == FSTokenizer::Token::FINAL);
	CHECK(token.is_node_name());

	// `$final` must keep resolving to a node named `final` rather than producing a
	// parse error now that `final` is a keyword.
	FSParser parser;
	Error err = parser.parse(R"(
func _ready():
	return $final
)",
			"user://final_node_name.fs", false);
	CHECK_EQ(err, OK);
}

TEST_CASE("[Modules][FoundryScript] Parser bounds expression nesting instead of crashing") {
	// A deeply nested expression must produce a parse error rather than overflowing the
	// native stack (SIGSEGV). The depth is far beyond both any legitimate source and the
	// parser's recursion limit, so each prefix form that recurses is exercised.
	const int depth = 50000;

	struct Case {
		const char *name;
		String source;
	};
	const Case cases[] = {
		{ "user://deep_parens.fs", "var x = " + String("(").repeat(depth) + "1" + String(")").repeat(depth) + "\n" },
		{ "user://deep_subscript.fs", "var x = " + String("[").repeat(depth) + "\n" },
		{ "user://deep_dictionary.fs", "var x = " + String("{").repeat(depth) + "\n" },
		{ "user://deep_unary.fs", "var x = " + String("-").repeat(depth) + "1\n" },
	};

	for (const Case &test_case : cases) {
		FSParser parser;
		Error err = parser.parse(test_case.source, test_case.name, false);
		CHECK_MESSAGE(err != OK, "Deeply nested expression should error, not crash: ", test_case.name);
	}
}

TEST_CASE("[Modules][FoundryScript] Parser bounds statement nesting instead of crashing") {
	// Chained single-line `if` statements recurse through the block parser; this must
	// report an error rather than overflowing the native stack.
	const int depth = 50000;
	String source = "func f():\n\t" + String("if true: ").repeat(depth) + "pass\n";

	FSParser parser;
	Error err = parser.parse(source, "user://deep_statements.fs", false);
	CHECK(err != OK);
}

TEST_CASE("[Modules][FoundryScript] Parser bounds type nesting instead of crashing") {
	// Nested type annotations recurse through parse_type(); a pathologically nested type
	// must report a parse error rather than overflowing the native stack.
	const int depth = 50000;
	String source = "var x: " + String("Array[").repeat(depth) + "int" + String("]").repeat(depth) + "\n";

	FSParser parser;
	Error err = parser.parse(source, "user://deep_type.fs", false);
	CHECK(err != OK);
}

TEST_CASE("[Modules][FoundryScript] Parser bounds elif-chain nesting instead of crashing") {
	// `elif` chains recurse through parse_if() rather than parse_statement(); a long
	// chain must report a parse error rather than overflowing the native stack.
	const int depth = 50000;
	String source = "func f():\n\tif false: pass\n";
	source += String("\telif false: pass\n").repeat(depth);

	FSParser parser;
	Error err = parser.parse(source, "user://deep_elif.fs", false);
	CHECK(err != OK);
}

TEST_CASE("[Modules][FoundryScript] Parser bounds match-pattern nesting instead of crashing") {
	// Nested array/dictionary match patterns recurse through parse_match_pattern(); a
	// pathologically nested pattern must report a parse error rather than crashing.
	const int depth = 50000;
	String source = "func f():\n\tmatch x:\n\t\t" + String("[").repeat(depth) + "0" + String("]").repeat(depth) + ":\n\t\t\tpass\n";

	FSParser parser;
	Error err = parser.parse(source, "user://deep_match_pattern.fs", false);
	CHECK(err != OK);
}

static PackedStringArray parse_source_errors(const String &p_source) {
	FSParser parser;
	Error err = parser.parse(p_source, "user://namespace_import_test.fs", false);
	PackedStringArray errors;
	if (err == OK) {
		return errors;
	}

	for (const FSParser::ParserError &parser_error : parser.get_errors()) {
		errors.push_back(parser_error.message);
	}
	return errors;
}

static void check_parse_source_error(const String &p_source, const String &p_expected_error) {
	INFO(p_source);
	PackedStringArray errors = parse_source_errors(p_source);
	CHECK_EQ(errors.size(), 1);
	if (errors.size() != 1) {
		return;
	}
	CHECK_EQ(errors[0], p_expected_error);
}

static const FSParser::FunctionNode *find_parser_function(const FSParser::ClassNode *p_class, const StringName &p_name) {
	ERR_FAIL_NULL_V(p_class, nullptr);

	for (const FSParser::ClassNode::Member &member : p_class->members) {
		if (member.type != FSParser::ClassNode::Member::FUNCTION || member.function == nullptr ||
				member.function->identifier == nullptr) {
			continue;
		}
		if (member.function->identifier->name == p_name) {
			return member.function;
		}
	}

	return nullptr;
}

static const FSParser::EnumNode *find_parser_enum(const FSParser::ClassNode *p_class, const StringName &p_name) {
	ERR_FAIL_NULL_V(p_class, nullptr);

	for (const FSParser::ClassNode::Member &member : p_class->members) {
		if (member.type != FSParser::ClassNode::Member::ENUM || member.m_enum == nullptr ||
				member.m_enum->identifier == nullptr) {
			continue;
		}
		if (member.m_enum->identifier->name == p_name) {
			return member.m_enum;
		}
	}

	return nullptr;
}

static const FSParser::FunctionNode *find_enum_function(const FSParser::EnumNode *p_enum, const StringName &p_name) {
	ERR_FAIL_NULL_V(p_enum, nullptr);

	for (const FSParser::FunctionNode *function : p_enum->functions) {
		if (function != nullptr && function->identifier != nullptr && function->identifier->name == p_name) {
			return function;
		}
	}

	return nullptr;
}

static const FSParser::VariableNode *find_function_local_variable(const FSParser::FunctionNode *p_function, const StringName &p_name) {
	ERR_FAIL_NULL_V(p_function, nullptr);
	ERR_FAIL_NULL_V(p_function->body, nullptr);

	for (const FSParser::Node *statement : p_function->body->statements) {
		if (statement == nullptr || statement->type != FSParser::Node::VARIABLE) {
			continue;
		}
		const FSParser::VariableNode *variable = static_cast<const FSParser::VariableNode *>(statement);
		if (variable->identifier != nullptr && variable->identifier->name == p_name) {
			return variable;
		}
	}

	return nullptr;
}

static const FSParser::CallNode *find_local_variable_call(const FSParser::FunctionNode *p_function, const StringName &p_name) {
	const FSParser::VariableNode *variable = find_function_local_variable(p_function, p_name);
	if (variable == nullptr || variable->initializer == nullptr || variable->initializer->type != FSParser::Node::CALL) {
		return nullptr;
	}
	return static_cast<const FSParser::CallNode *>(variable->initializer);
}

static MethodInfo find_method_info(const List<MethodInfo> &p_methods, const StringName &p_name) {
	for (const MethodInfo &method : p_methods) {
		if (method.name == p_name) {
			return method;
		}
	}
	return MethodInfo();
}

static Vector<StringName> get_script_trait_vector(const Ref<Script> &p_script) {
	List<StringName> trait_list;
	p_script->get_script_trait_list(&trait_list);

	Vector<StringName> traits;
	for (const StringName &trait : trait_list) {
		traits.push_back(trait);
	}
	return traits;
}

class TestFSTraitReflectionAccessor {
public:
	static void set_base(const Ref<FoundryScript> &p_script, const Ref<FoundryScript> &p_base) {
		p_script->base = p_base;
	}

	static void set_script_trait_list(const Ref<FoundryScript> &p_script, const Vector<StringName> &p_traits) {
		p_script->script_trait_list = p_traits;
	}
};

class TestFSGenericReflectionAccessor {
public:
	static void set_type_parameters(const Ref<FoundryScript> &p_script, const Vector<FoundryScript::TypeParameter> &p_parameters) {
		p_script->type_parameters = p_parameters;
	}
};

struct ScopedFSNativeGlobals {
	bool initialized = false;

	ScopedFSNativeGlobals() {
		if (!FSLanguage::get_singleton()->has_any_global_constant(SNAME("RefCounted"))) {
			FSLanguage::get_singleton()->init();
			initialized = true;
		}
	}

	~ScopedFSNativeGlobals() {
		// Some sibling FoundryScript doctests initialize the language without finishing it; only tear down state owned by this guard.
		if (initialized) {
			FSLanguage::get_singleton()->finish();
		}
	}
};

struct ScopedFSAutoloadSetting {
	StringName name;

	ScopedFSAutoloadSetting(const StringName &p_name, const String &p_path, bool p_singleton = true, int p_order = 10) {
		name = p_name;
		clear();

		const String setting = "autoload/" + String(name);
		ProjectSettings::get_singleton()->set_setting(setting, p_singleton ? "*" + p_path : p_path);
		ProjectSettings::get_singleton()->set_order(setting, p_order);
	}

	~ScopedFSAutoloadSetting() {
		clear();
	}

	void clear() {
		ProjectSettings *project_settings = ProjectSettings::get_singleton();
		const String setting = "autoload/" + String(name);
		if (project_settings->has_setting(setting)) {
			project_settings->clear(setting);
		}
		if (project_settings->has_autoload(name)) {
			project_settings->remove_autoload(name);
		}
	}
};

struct ScopedResourceUIDRegistration {
	ResourceUID::ID id = ResourceUID::INVALID_ID;

	ScopedResourceUIDRegistration(const String &p_path) {
		ResourceUID *resource_uid = ResourceUID::get_singleton();
		id = resource_uid->create_id();
		resource_uid->add_id(id, p_path);
	}

	~ScopedResourceUIDRegistration() {
		if (id != ResourceUID::INVALID_ID && ResourceUID::get_singleton()->has_id(id)) {
			ResourceUID::get_singleton()->remove_id(id);
		}
	}

	String get_uid_path() const {
		return ResourceUID::get_singleton()->id_to_text(id);
	}
};

static const FSParser::ClassNode *find_parser_class(const FSParser::ClassNode *p_class, const StringName &p_name) {
	ERR_FAIL_NULL_V(p_class, nullptr);

	for (const FSParser::ClassNode::Member &member : p_class->members) {
		if (member.type != FSParser::ClassNode::Member::CLASS || member.m_class == nullptr ||
				member.m_class->identifier == nullptr) {
			continue;
		}
		if (member.m_class->identifier->name == p_name) {
			return member.m_class;
		}
	}

	return nullptr;
}

static const FSParser::ClassNode *find_parser_trait(const FSParser::ClassNode *p_class, const StringName &p_name) {
	ERR_FAIL_NULL_V(p_class, nullptr);

	for (const FSParser::ClassNode::Member &member : p_class->members) {
		if (member.type != FSParser::ClassNode::Member::CLASS || member.m_class == nullptr ||
				!member.m_class->is_trait || member.m_class->identifier == nullptr) {
			continue;
		}
		if (member.m_class->identifier->name == p_name) {
			return member.m_class;
		}
	}

	return nullptr;
}

static const FSParser::VariableNode *find_parser_variable(const FSParser::ClassNode *p_class, const StringName &p_name) {
	ERR_FAIL_NULL_V(p_class, nullptr);

	for (const FSParser::ClassNode::Member &member : p_class->members) {
		if (member.type != FSParser::ClassNode::Member::VARIABLE || member.variable == nullptr ||
				member.variable->identifier == nullptr) {
			continue;
		}
		if (member.variable->identifier->name == p_name) {
			return member.variable;
		}
	}

	return nullptr;
}

TEST_CASE("[Modules][FoundryScript] Parser sets is_final on final declarations") {
	FSParser parser;
	Error err = parser.parse(R"(
final var member_value := 1
final static var shared := 2
var plain_value := 3

final func locked() -> void:
	pass

func unlocked() -> void:
	pass

final class Inner:
	pass

class Plain:
	pass
)",
			"user://final_flags.fs", false);
	CHECK_EQ(err, OK);

	const FSParser::ClassNode *root = parser.get_tree();
	CHECK(root != nullptr);
	if (root == nullptr) {
		return;
	}

	const FSParser::VariableNode *member_value = find_parser_variable(root, SNAME("member_value"));
	CHECK(member_value != nullptr);
	if (member_value != nullptr) {
		CHECK(member_value->is_final);
		CHECK_FALSE(member_value->is_static);
	}

	const FSParser::VariableNode *shared = find_parser_variable(root, SNAME("shared"));
	CHECK(shared != nullptr);
	if (shared != nullptr) {
		CHECK(shared->is_final);
		CHECK(shared->is_static);
	}

	const FSParser::VariableNode *plain_value = find_parser_variable(root, SNAME("plain_value"));
	CHECK(plain_value != nullptr);
	if (plain_value != nullptr) {
		CHECK_FALSE(plain_value->is_final);
	}

	const FSParser::FunctionNode *locked = find_parser_function(root, SNAME("locked"));
	CHECK(locked != nullptr);
	if (locked != nullptr) {
		CHECK(locked->is_final);
	}

	const FSParser::FunctionNode *unlocked = find_parser_function(root, SNAME("unlocked"));
	CHECK(unlocked != nullptr);
	if (unlocked != nullptr) {
		CHECK_FALSE(unlocked->is_final);
	}

	const FSParser::ClassNode *inner = find_parser_class(root, SNAME("Inner"));
	CHECK(inner != nullptr);
	if (inner != nullptr) {
		CHECK(inner->is_final);
	}

	const FSParser::ClassNode *plain = find_parser_class(root, SNAME("Plain"));
	CHECK(plain != nullptr);
	if (plain != nullptr) {
		CHECK_FALSE(plain->is_final);
	}
}

static bool has_parser_error(const FSParser &p_parser, const String &p_expected_error) {
	for (const FSParser::ParserError &parser_error : p_parser.get_errors()) {
		if (parser_error.message == p_expected_error) {
			return true;
		}
	}
	return false;
}

static String first_parser_error_message(const FSParser &p_parser) {
	for (const FSParser::ParserError &parser_error : p_parser.get_errors()) {
		return parser_error.message;
	}
	return String();
}

static int count_parser_errors(const FSParser &p_parser, const String &p_expected_error) {
	int count = 0;
	for (const FSParser::ParserError &parser_error : p_parser.get_errors()) {
		if (parser_error.message == p_expected_error) {
			count++;
		}
	}
	return count;
}

static Error analyze_source(FSParser &r_parser, const String &p_source,
		const String &p_path = "user://trait_analyzer_test.fs") {
	Error err = r_parser.parse(p_source, p_path, false);
	if (err != OK) {
		return err;
	}

	FSAnalyzer analyzer(&r_parser);
	return analyzer.analyze();
}

#ifdef DEBUG_ENABLED
static bool has_parser_warning(const FSParser &p_parser, FSWarning::Code p_code, const String &p_expected_message) {
	for (const FSWarning &warning : p_parser.get_warnings()) {
		if (warning.code == p_code && warning.get_message() == p_expected_message) {
			return true;
		}
	}
	return false;
}

static int count_parser_warnings(const FSParser &p_parser, FSWarning::Code p_code) {
	int count = 0;
	for (const FSWarning &warning : p_parser.get_warnings()) {
		if (warning.code == p_code) {
			count++;
		}
	}
	return count;
}
#endif // DEBUG_ENABLED

static String write_temp_script(const String &p_file_name, const String &p_source) {
	Error err = OK;
	Ref<FileAccess> file = FileAccess::create_temp(FileAccess::WRITE, p_file_name.get_basename(), "fs", true, &err);
	CHECK_EQ(err, OK);
	CHECK(file.is_valid());
	if (file.is_valid()) {
		file->store_string(p_source);
		return file->get_path_absolute();
	}
	return String();
}

struct TempScriptFile {
	String path;

	TempScriptFile(const String &p_file_name, const String &p_source) {
		path = write_temp_script(p_file_name, p_source);
	}

	~TempScriptFile() {
		if (!path.is_empty()) {
			DirAccess::remove_absolute(path);
		}
	}
};

static void restore_global_script_classes(const Array &p_classes) {
	ScriptServer::global_classes_clear();
	for (const Variant &script_class : p_classes) {
		Dictionary c = script_class;
		if (!c.has("class") || !c.has("language") || !c.has("path") || !c.has("base") ||
				!c.has("is_abstract") || !c.has("is_tool")) {
			continue;
		}
		const bool is_trait = c.has("is_trait") && c["is_trait"];
		const bool is_enum = c.has("is_enum") && c["is_enum"];
		ScriptServer::add_global_class(c["class"], c["base"], c["language"], c["path"], c["is_abstract"], c["is_tool"], is_trait, is_enum);
	}
	ProjectSettings::get_singleton()->store_global_class_list(p_classes);
}

struct GlobalScriptClassCacheBackup {
	Array classes;
	String cache_path;
	String cache_contents;
	bool cache_existed = false;

	GlobalScriptClassCacheBackup() {
		classes = ProjectSettings::get_singleton()->get_global_class_list();
		cache_path = ProjectSettings::get_singleton()->get_global_class_list_path();
		cache_existed = FileAccess::exists(cache_path);
		if (cache_existed) {
			Ref<FileAccess> file = FileAccess::open(cache_path, FileAccess::READ);
			if (file.is_valid()) {
				cache_contents = file->get_as_utf8_string();
			}
		}
	}

	~GlobalScriptClassCacheBackup() {
		restore_global_script_classes(classes);
		if (cache_existed) {
			Ref<FileAccess> file = FileAccess::open(cache_path, FileAccess::WRITE);
			if (file.is_valid()) {
				file->store_string(cache_contents);
			}
		} else {
			DirAccess::remove_absolute(cache_path);
		}
	}
};

static String register_global_script_class(const TempScriptFile &p_script) {
	String base_type;
	bool is_abstract = false;
	bool is_tool = false;
	bool is_trait = false;
	String class_name = FSLanguage::get_singleton()->get_global_class_name(p_script.path, &base_type, nullptr,
			&is_abstract, &is_tool, &is_trait);
	CHECK_FALSE(class_name.is_empty());
	if (!class_name.is_empty()) {
		ScriptServer::add_global_class(class_name, base_type, FSLanguage::get_singleton()->get_name(), p_script.path,
				is_abstract, is_tool, is_trait);
	}
	return class_name;
}

#ifdef TOOLS_ENABLED
static Color get_highlighted_color_at(const Dictionary &p_highlighting, int p_column) {
	Color color;
	Array columns = p_highlighting.keys();
	columns.sort();

	for (int i = 0; i < columns.size(); i++) {
		const int column = columns[i];
		if (column > p_column) {
			break;
		}
		const Dictionary info = p_highlighting[column];
		if (info.has("color")) {
			color = info["color"];
		}
	}

	return color;
}

TEST_CASE("[Modules][FoundryScript][Editor] Syntax highlighter treats async as contextual function modifier") {
	TextEdit *text_edit = memnew(TextEdit);
	text_edit->set_text(R"(async func load() -> void:
static async func make() -> void:
var async = 1
func async() -> int:
	return async
)");

	Ref<FSSyntaxHighlighter> highlighter;
	highlighter.instantiate();
	highlighter->set_text_edit(text_edit);
	highlighter->_update_cache();

	const Color keyword_color = EDITOR_GET("text_editor/theme/highlighting/keyword_color");
	const Color normal_color = text_edit->get_theme_color(SceneStringName(font_color));
	const Color function_definition_color = EDITOR_GET("text_editor/theme/highlighting/foundry_script/function_definition_color");

	CHECK_EQ(get_highlighted_color_at(highlighter->_get_line_syntax_highlighting_impl(0), 0), keyword_color);
	CHECK_EQ(get_highlighted_color_at(highlighter->_get_line_syntax_highlighting_impl(1), 7), keyword_color);
	CHECK_EQ(get_highlighted_color_at(highlighter->_get_line_syntax_highlighting_impl(2), 4), normal_color);
	CHECK_EQ(get_highlighted_color_at(highlighter->_get_line_syntax_highlighting_impl(3), 5), function_definition_color);

	memdelete(text_edit);
}

TEST_CASE("[Modules][FoundryScript][Editor] Completion suggests UID-backed autoloads as script types") {
	ScopedFSNativeGlobals native_globals;
	TempScriptFile autoload_script("completion_uid_autoload.fs", "extends Node\n");
	ScopedResourceUIDRegistration uid(autoload_script.path);
	ScopedFSAutoloadSetting autoload(SNAME("CompletionUidAutoload"), uid.get_uid_path());

	List<ScriptLanguage::CodeCompletionOption> options;
	bool forced = false;
	String call_hint;
	const String code = "class Child extends CompletionUid" + String::chr(0xFFFF);

	const Error err = FSLanguage::get_singleton()->complete_code(code, "user://completion_uid_consumer.fs", nullptr, &options, forced, call_hint);
	CHECK_EQ(err, OK);

	bool found_autoload_type = false;
	for (const ScriptLanguage::CodeCompletionOption &option : options) {
		if (option.display == "CompletionUidAutoload") {
			found_autoload_type = option.kind == ScriptLanguage::CODE_COMPLETION_KIND_CLASS;
			break;
		}
	}
	CHECK(found_autoload_type);
}

TEST_CASE("[Modules][FoundryScript][Editor] Symbol lookup resolves UID-backed autoload singleton scripts") {
	ScopedFSNativeGlobals native_globals;
	TempScriptFile autoload_script("lookup_uid_autoload.fs", "extends Node\n");
	ScopedResourceUIDRegistration uid(autoload_script.path);
	ScopedFSAutoloadSetting autoload(SNAME("LookupUidAutoload"), uid.get_uid_path());

	FSLanguage::LookupResult result;
	const Error err = FSLanguage::get_singleton()->lookup_code(
			"func _ready():\n\tLookupUidAutoload" + String::chr(0xFFFF) + "\n",
			"LookupUidAutoload",
			"user://lookup_uid_consumer.fs",
			nullptr,
			result);

	CHECK_EQ(err, OK);
	CHECK_EQ(result.type, ScriptLanguage::LOOKUP_RESULT_CLASS);
	CHECK_EQ(result.class_name, "LookupUidAutoload");
	CHECK_EQ(result.script_path, autoload_script.path);
	CHECK_EQ(result.location, 0);
}

TEST_CASE("[Modules][FoundryScript][Editor] Syntax highlighter colors UID-backed autoload singletons") {
	ScopedFSNativeGlobals native_globals;
	TempScriptFile autoload_script("highlight_uid_autoload.fs", "extends Node\n");
	ScopedResourceUIDRegistration uid(autoload_script.path);
	ScopedFSAutoloadSetting autoload(SNAME("HighlightUidAutoload"), uid.get_uid_path());

	TextEdit *text_edit = memnew(TextEdit);
	text_edit->set_text("func _ready() -> void:\n\tHighlightUidAutoload\n");

	Ref<FSSyntaxHighlighter> highlighter;
	highlighter.instantiate();
	highlighter->set_text_edit(text_edit);
	highlighter->_update_cache();

	const Color usertype_color = EDITOR_GET("text_editor/theme/highlighting/user_type_color");
	CHECK_EQ(get_highlighted_color_at(highlighter->_get_line_syntax_highlighting_impl(1), 1), usertype_color);

	memdelete(text_edit);
}
#endif // TOOLS_ENABLED

TEST_CASE("[Modules][FoundryScript] Parser stores namespace and import declarations") {
	FSParser parser;
	Error err = parser.parse(R"(
namespace characters.controllers
import characters
import characters.stats
abstract class_name MyCharacterController
extends Node
)",
			"user://my_character_controller.fs", false);

	CHECK(err == OK);
	if (err != OK) {
		return;
	}
	const FSParser::ClassNode *root = parser.get_tree();
	CHECK(root != nullptr);
	if (root == nullptr) {
		return;
	}
	CHECK(root->identifier != nullptr);
	if (root->identifier == nullptr) {
		return;
	}

	CHECK_EQ(root->namespace_name, "characters.controllers");
	CHECK_EQ(root->qualified_global_name, "characters.controllers.MyCharacterController");
	CHECK_EQ(root->fqcn, "characters.controllers.MyCharacterController");
	CHECK_EQ(root->identifier->name, SNAME("MyCharacterController"));
	CHECK(root->is_abstract);
	CHECK_EQ(root->imports.size(), 2);
	if (root->imports.size() != 2) {
		return;
	}
	CHECK_EQ(root->imports[0], "characters");
	CHECK_EQ(root->imports[1], "characters.stats");
}

TEST_CASE("[Modules][FoundryScript] Parser accepts imports without namespace") {
	FSParser parser;
	Error err = parser.parse(R"(
import shared
import shared.types
class_name UsesShared
)",
			"user://uses_shared.fs", false);

	CHECK(err == OK);
	if (err != OK) {
		return;
	}
	const FSParser::ClassNode *root = parser.get_tree();
	CHECK(root != nullptr);
	if (root == nullptr) {
		return;
	}
	CHECK(root->identifier != nullptr);
	if (root->identifier == nullptr) {
		return;
	}

	CHECK(root->namespace_name.is_empty());
	CHECK_EQ(root->qualified_global_name, "UsesShared");
	CHECK_EQ(root->fqcn, "UsesShared");
	CHECK_EQ(root->imports.size(), 2);
	if (root->imports.size() != 2) {
		return;
	}
	CHECK_EQ(root->imports[0], "shared");
	CHECK_EQ(root->imports[1], "shared.types");
}

TEST_CASE("[Modules][FoundryScript] Parser stores global trait declarations and uses") {
	FSParser parser;
	Error err = parser.parse(R"(
namespace characters.stats
trait_name Damageable
extends Node
uses Trackable, shared.CombatTag

signal died
var health: int = 100
)",
			"user://damageable.fs", false);

	CHECK_EQ(err, OK);
	if (err != OK) {
		return;
	}

	const FSParser::ClassNode *root = parser.get_tree();
	CHECK(root != nullptr);
	if (root == nullptr) {
		return;
	}
	CHECK(root->is_trait);
	CHECK(root->trait_name_used);
	CHECK(root->identifier != nullptr);
	if (root->identifier == nullptr) {
		return;
	}
	CHECK_EQ(root->identifier->name, SNAME("Damageable"));
	CHECK_EQ(root->namespace_name, "characters.stats");
	CHECK_EQ(root->qualified_global_name, "characters.stats.Damageable");
	CHECK_EQ(root->fqcn, "characters.stats.Damageable");
	CHECK_EQ(root->extends.size(), 1);
	if (root->extends.size() != 1) {
		return;
	}
	CHECK_EQ(root->extends[0]->name, SNAME("Node"));
	CHECK_EQ(root->used_traits.size(), 2);
	if (root->used_traits.size() != 2) {
		return;
	}
	CHECK_EQ(root->used_traits[0].to_string(), "Trackable");
	CHECK_EQ(root->used_traits[1].to_string(), "shared.CombatTag");
}

TEST_CASE("[Modules][FoundryScript] Parser stores inline traits and class uses") {
	FSParser parser;
	Error err = parser.parse(R"(
class_name Player
extends Node
uses Damageable, characters.Movable

trait LocalTrait extends Node:
	uses Trackable
	func touch() -> void:
		pass
)",
			"user://player.fs", false);

	CHECK_EQ(err, OK);
	if (err != OK) {
		return;
	}

	const FSParser::ClassNode *root = parser.get_tree();
	CHECK(root != nullptr);
	if (root == nullptr) {
		return;
	}
	CHECK_FALSE(root->is_trait);
	CHECK_FALSE(root->trait_name_used);
	CHECK_EQ(root->used_traits.size(), 2);
	if (root->used_traits.size() != 2) {
		return;
	}
	CHECK_EQ(root->used_traits[0].to_string(), "Damageable");
	CHECK_EQ(root->used_traits[1].to_string(), "characters.Movable");

	const FSParser::ClassNode *local_trait = find_parser_trait(root, SNAME("LocalTrait"));
	CHECK(local_trait != nullptr);
	if (local_trait == nullptr) {
		return;
	}
	CHECK(local_trait->is_trait);
	CHECK_FALSE(local_trait->trait_name_used);
	CHECK_EQ(local_trait->outer, root);
	CHECK_EQ(local_trait->fqcn, "Player::LocalTrait");
	CHECK_EQ(local_trait->extends.size(), 1);
	if (local_trait->extends.size() != 1) {
		return;
	}
	CHECK_EQ(local_trait->extends[0]->name, SNAME("Node"));
	CHECK_EQ(local_trait->used_traits.size(), 1);
	if (local_trait->used_traits.size() != 1) {
		return;
	}
	CHECK_EQ(local_trait->used_traits[0].to_string(), "Trackable");
	CHECK(find_parser_function(local_trait, SNAME("touch")) != nullptr);
}

TEST_CASE("[Modules][FoundryScript] Parser stores inline root trait metadata without extends") {
	const String script_path = "user://root_inline_trait.fs";
	FSParser parser;
	Error err = parser.parse(R"(
trait LocalTrait uses Trackable:
	pass
)",
			script_path, false);

	CHECK_EQ(err, OK);
	if (err != OK) {
		return;
	}

	const FSParser::ClassNode *root = parser.get_tree();
	CHECK(root != nullptr);
	if (root == nullptr) {
		return;
	}

	const FSParser::ClassNode *local_trait = find_parser_trait(root, SNAME("LocalTrait"));
	CHECK(local_trait != nullptr);
	if (local_trait == nullptr) {
		return;
	}
	CHECK_EQ(local_trait->outer, root);
	CHECK_EQ(local_trait->fqcn, FoundryScript::canonicalize_path(script_path) + "::LocalTrait");
	CHECK_EQ(local_trait->used_traits.size(), 1);
	if (local_trait->used_traits.size() != 1) {
		return;
	}
	CHECK_EQ(local_trait->used_traits[0].to_string(), "Trackable");
}

TEST_CASE("[Modules][FoundryScript] Parser keeps uses available as an identifier") {
	FSParser parser;
	Error err = parser.parse(R"(
var uses := 1

func echo(uses: int) -> int:
	var nested := uses
	return nested
)",
			"user://uses_identifier.fs", false);

	CHECK_EQ(err, OK);
	if (err != OK) {
		return;
	}

	const FSParser::ClassNode *root = parser.get_tree();
	CHECK(root != nullptr);
	if (root == nullptr) {
		return;
	}
	CHECK(root->has_member(SNAME("uses")));
	if (root->has_member(SNAME("uses"))) {
		CHECK_EQ(root->get_member(SNAME("uses")).type, FSParser::ClassNode::Member::VARIABLE);
	}
	CHECK(find_parser_function(root, SNAME("echo")) != nullptr);
}

TEST_CASE("[Modules][FoundryScript] Parser stores retroactive conformance declarations") {
	FSParser parser;
	Error err = parser.parse(R"(
extend characters.Foo uses Bar, shared.Baz:
	func required_method(x: int) -> void:
		print(x)
	static func make() -> int:
		return 0
)",
			"user://conformance.fs", false);

	CHECK_EQ(err, OK);
	if (err != OK) {
		return;
	}

	const FSParser::ClassNode *root = parser.get_tree();
	CHECK(root != nullptr);
	if (root == nullptr) {
		return;
	}
	CHECK_EQ(root->conformances.size(), 1);
	if (root->conformances.size() != 1) {
		return;
	}

	const FSParser::ConformanceNode *conformance = root->conformances[0];
	CHECK(conformance != nullptr);
	if (conformance == nullptr) {
		return;
	}
	CHECK(conformance->target != nullptr);
	if (conformance->target != nullptr) {
		CHECK_EQ(conformance->target->type_chain.size(), 2);
		if (conformance->target->type_chain.size() == 2) {
			CHECK_EQ(conformance->target->type_chain[0]->name, SNAME("characters"));
			CHECK_EQ(conformance->target->type_chain[1]->name, SNAME("Foo"));
		}
		CHECK(conformance->target->container_types.is_empty());
	}
	CHECK_EQ(conformance->traits.size(), 2);
	if (conformance->traits.size() == 2) {
		CHECK_EQ(conformance->traits[0].to_string(), "Bar");
		CHECK_EQ(conformance->traits[1].to_string(), "shared.Baz");
	}
	CHECK_EQ(conformance->witnesses.size(), 2);
	if (conformance->witnesses.size() == 2) {
		CHECK(conformance->witnesses[0]->identifier != nullptr);
		CHECK_EQ(conformance->witnesses[0]->identifier->name, SNAME("required_method"));
		CHECK(conformance->witnesses[1]->identifier != nullptr);
		CHECK_EQ(conformance->witnesses[1]->identifier->name, SNAME("make"));
	}
}

TEST_CASE("[Modules][FoundryScript] Parser rejects type arguments on a conformance target") {
	check_parse_source_error(R"(
extend Box[int] uses Bar:
	func required_method() -> void:
		pass
)",
			R"("extend" applies to all specializations; remove the type arguments.)");
}

TEST_CASE("[Modules][FoundryScript] Parser rejects non-method members in a conformance body") {
	check_parse_source_error(R"(
extend Foo uses Bar:
	var state := 1
)",
			R"(An "extend" conformance body may only contain methods.)");
}

TEST_CASE("[Modules][FoundryScript] Parser requires uses in a conformance declaration") {
	check_parse_source_error(R"(
extend Foo:
	func required_method() -> void:
		pass
)",
			R"(Expected "uses" after the "extend" target type.)");
}

TEST_CASE("[Modules][FoundryScript] Parser keeps extend available as an identifier") {
	FSParser parser;
	Error err = parser.parse(R"(
var extend = 1

func use_it() -> int:
	var extend := 2
	return extend
)",
			"user://extend_identifier.fs", false);

	CHECK_EQ(err, OK);
	if (err != OK) {
		return;
	}

	const FSParser::ClassNode *root = parser.get_tree();
	CHECK(root != nullptr);
	if (root == nullptr) {
		return;
	}
	CHECK(root->has_member(SNAME("extend")));
	if (root->has_member(SNAME("extend"))) {
		CHECK_EQ(root->get_member(SNAME("extend")).type, FSParser::ClassNode::Member::VARIABLE);
	}
	CHECK(find_parser_function(root, SNAME("use_it")) != nullptr);
	CHECK_EQ(root->conformances.size(), 0);
}

TEST_CASE("[Modules][FoundryScript] Analyzer resolves inline trait uses") {
	FSParser parser;
	Error err = analyze_source(parser, R"(
class_name Player
uses Damageable

trait Damageable:
	abstract func take_damage(amount: int) -> void

func take_damage(amount: int) -> void:
	pass
)",
			"user://player_inline_trait_resolution.fs");

	INFO(first_parser_error_message(parser));
	CHECK_EQ(err, OK);
	if (err != OK) {
		return;
	}

	const FSParser::ClassNode *root = parser.get_tree();
	const FSParser::ClassNode *damageable = find_parser_trait(root, SNAME("Damageable"));
	CHECK(damageable != nullptr);
	CHECK_EQ(root->used_traits.size(), 1);
	if (damageable == nullptr || root->used_traits.size() != 1) {
		return;
	}
	CHECK_EQ(root->used_traits[0].resolved_trait, damageable);
	CHECK_EQ(root->resolved_traits.size(), 1);
	CHECK_EQ(root->resolved_traits[0], damageable);
}

TEST_CASE("[Modules][FoundryScript] Analyzer resolves global and namespace-qualified trait uses") {
	GlobalScriptClassCacheBackup backup;
	ScriptServer::global_classes_clear();

	TempScriptFile trait("namespaced_damageable_trait.fs", R"(
namespace characters
trait_name Damageable
extends RefCounted

abstract func take_damage(amount: int) -> void
)");

	String base_type;
	bool is_abstract = true;
	bool is_tool = true;
	bool is_trait = false;
	String trait_name = FSLanguage::get_singleton()->get_global_class_name(trait.path, &base_type, nullptr,
			&is_abstract, &is_tool, &is_trait);
	CHECK_EQ(trait_name, "characters.Damageable");
	CHECK_EQ(base_type, "RefCounted");
	CHECK_FALSE(is_abstract);
	CHECK_FALSE(is_tool);
	CHECK(is_trait);
	if (trait_name.is_empty()) {
		return;
	}

	ScriptServer::add_global_class(trait_name, base_type, FSLanguage::get_singleton()->get_name(), trait.path,
			is_abstract, is_tool, is_trait);

	FSParser imported_parser;
	Error err = analyze_source(imported_parser, R"(
import characters
class_name ImportedPlayer
uses Damageable

func take_damage(amount: int) -> void:
	pass
)",
			"user://player_imported_trait_resolution.fs");

	INFO(first_parser_error_message(imported_parser));
	CHECK_EQ(err, OK);
	if (err != OK) {
		return;
	}

	const FSParser::ClassNode *imported_root = imported_parser.get_tree();
	CHECK_EQ(imported_root->used_traits.size(), 1);
	if (imported_root->used_traits.size() != 1) {
		return;
	}
	CHECK(imported_root->used_traits[0].resolved_trait != nullptr);
	if (imported_root->used_traits[0].resolved_trait == nullptr) {
		return;
	}
	CHECK_EQ(imported_root->used_traits[0].resolved_trait->get_global_name(), SNAME("characters.Damageable"));

	FSParser qualified_parser;
	err = analyze_source(qualified_parser, R"(
class_name QualifiedPlayer
uses characters.Damageable

func take_damage(amount: int) -> void:
	pass
)",
			"user://player_qualified_trait_resolution.fs");

	INFO(first_parser_error_message(qualified_parser));
	CHECK_EQ(err, OK);
	if (err != OK) {
		return;
	}

	const FSParser::ClassNode *qualified_root = qualified_parser.get_tree();
	CHECK_EQ(qualified_root->used_traits.size(), 1);
	if (qualified_root->used_traits.size() != 1) {
		return;
	}
	CHECK(qualified_root->used_traits[0].resolved_trait != nullptr);
	if (qualified_root->used_traits[0].resolved_trait == nullptr) {
		return;
	}
	CHECK_EQ(qualified_root->used_traits[0].resolved_trait->get_global_name(), SNAME("characters.Damageable"));
}

TEST_CASE("[Modules][FoundryScript] Analyzer enforces trait base class constraints") {
	FSParser parser;
	Error err = analyze_source(parser, R"(
class_name Player
extends RefCounted
uses Movable

trait Movable extends Node2D:
	pass
)",
			"user://trait_base_constraint_error.fs");

	CHECK_EQ(err, ERR_PARSE_ERROR);
	CHECK(has_parser_error(parser,
			R"(Class "Player" cannot use trait "Movable" because it does not inherit from "Node2D".)"));

	GlobalScriptClassCacheBackup backup;
	ScriptServer::global_classes_clear();

	TempScriptFile base_script("trait_base_constraint_external_base.fs", R"(
class_name ActorBase
extends Node2D
)");
	String base_class_name = register_global_script_class(base_script);
	CHECK_EQ(base_class_name, "ActorBase");

	FSParser external_base_parser;
	err = analyze_source(external_base_parser, R"(
class_name Actor
extends ActorBase
uses Movable

trait Movable extends Node2D:
	pass
)",
			"user://trait_base_constraint_external_base_user.fs");

	INFO(first_parser_error_message(external_base_parser));
	CHECK_EQ(err, OK);
}

TEST_CASE("[Modules][FoundryScript] Analyzer enforces required trait methods") {
	FSParser missing_parser;
	Error err = analyze_source(missing_parser, R"(
class_name Player
uses Damageable

trait Damageable:
	abstract func take_damage(amount: int) -> void
)",
			"user://trait_required_method_error.fs");

	CHECK_EQ(err, ERR_PARSE_ERROR);
	CHECK(has_parser_error(missing_parser,
			R"msg(Class "Player" must implement trait method "Damageable.take_damage()".)msg"));

	FSParser async_parser;
	err = analyze_source(async_parser, R"(
class_name Player
uses RemoteLoadable

trait RemoteLoadable:
	abstract async func fetch() -> String

func fetch() -> String:
	return ""
)",
			"user://trait_async_required_method_error.fs");

	CHECK_EQ(err, ERR_PARSE_ERROR);
	CHECK(has_parser_error(async_parser,
			R"msg(The function "fetch()" must be async because it implements async trait method "RemoteLoadable.fetch()".)msg"));

	FSParser sync_parser;
	err = analyze_source(sync_parser, R"(
class_name Player
uses LocalLoadable

trait LocalLoadable:
	abstract func fetch() -> String

async func fetch() -> String:
	return ""
)",
			"user://trait_sync_required_method_error.fs");

	CHECK_EQ(err, ERR_PARSE_ERROR);
	CHECK(has_parser_error(sync_parser,
			R"msg(The function "fetch()" cannot be async because it implements synchronous trait method )msg"
			R"msg("LocalLoadable.fetch()".)msg"));

	FSParser signature_parser;
	err = analyze_source(signature_parser, R"(
class_name Player
uses Damageable

trait Damageable:
	abstract func take_damage(amount: int) -> void

func take_damage(amount: String) -> void:
	pass
)",
			"user://trait_signature_required_method_error.fs");

	CHECK_EQ(err, ERR_PARSE_ERROR);
	CHECK(has_parser_error(signature_parser,
			R"msg(The function "take_damage()" signature does not match required trait method "Damageable.take_damage()".)msg"));

	FSParser native_parser;
	err = analyze_source(native_parser, R"(
class_name Named
extends RefCounted
uses NamedTrait

trait NamedTrait:
	abstract func get_class() -> String
)",
			"user://trait_required_method_native_base.fs");

	INFO(first_parser_error_message(native_parser));
	CHECK_EQ(err, OK);

	GlobalScriptClassCacheBackup backup;
	ScriptServer::global_classes_clear();

	TempScriptFile base_script("trait_required_method_external_base.fs", R"(
class_name TraitMethodBase
extends RefCounted

func take_damage(amount: int) -> void:
	pass
)");
	String base_class_name = register_global_script_class(base_script);
	CHECK_EQ(base_class_name, "TraitMethodBase");

	FSParser external_parser;
	err = analyze_source(external_parser, R"(
class_name Player
extends TraitMethodBase
uses Damageable

trait Damageable:
	abstract func take_damage(amount: int) -> void
)",
			"user://trait_required_method_external_base_user.fs");

	INFO(first_parser_error_message(external_parser));
	CHECK_EQ(err, OK);
}

TEST_CASE("[Modules][FoundryScript] Analyzer resolves transitive external scope classes") {
	// Regression for #732: class-scope lookup must walk a multi-hop extends chain and find
	// symbols declared in the root script without relying on defensive external-parser caching
	// inside get_class_node_current_scope_classes().
	const String root = OS::get_singleton()->get_temp_path().path_join("transitive_external_scope");
	Ref<DirAccess> dir = DirAccess::create(DirAccess::ACCESS_FILESYSTEM);
	CHECK_EQ(dir->make_dir_recursive(root), OK);

	const String root_path = root.path_join("transitive_external_parser_lookup_root.fs");
	const String mid_path = root.path_join("transitive_external_parser_lookup_mid.fs");
	const String leaf_path = root.path_join("transitive_external_parser_lookup.fs");

	auto write_script = [&](const String &p_path, const String &p_source) {
		Ref<FileAccess> file = FileAccess::open(p_path, FileAccess::WRITE);
		CHECK(file.is_valid());
		if (file.is_valid()) {
			file->store_string(p_source);
		}
	};

	write_script(root_path, R"(
class LeafType:
	const MARK := "transitive-leaf"
)");
	write_script(mid_path, R"(extends "transitive_external_parser_lookup_root.fs")");
	const String leaf_source = R"(
extends "transitive_external_parser_lookup_mid.fs"

func test() -> void:
	var leaf := LeafType.new()
	print(leaf.MARK)
)";
	write_script(leaf_path, leaf_source);

	FSCache::remove_parser(root_path);
	FSCache::remove_parser(mid_path);
	FSCache::remove_parser(leaf_path);

	FSParser parser;
	Error err = parser.parse(leaf_source, leaf_path, false);
	INFO(first_parser_error_message(parser));
	CHECK_EQ(err, OK);

	FSAnalyzer analyzer(&parser);
	err = analyzer.resolve_inheritance();
	INFO(first_parser_error_message(parser));
	CHECK_EQ(err, OK);

	err = analyzer.resolve_interface();
	INFO(first_parser_error_message(parser));
	CHECK_EQ(err, OK);

	err = analyzer.resolve_body();
	INFO(first_parser_error_message(parser));
	CHECK_EQ(err, OK);

	FSCache::remove_parser(root_path);
	FSCache::remove_parser(mid_path);
	FSCache::remove_parser(leaf_path);
	dir->remove(root_path);
	dir->remove(mid_path);
	dir->remove(leaf_path);
	dir->remove(root);
}

TEST_CASE("[Modules][FoundryScript] Analyzer rejects trait inheritance and construction") {
	GlobalScriptClassCacheBackup backup;
	ScriptServer::global_classes_clear();

	TempScriptFile trait("global_trait_inheritance_gate.fs", R"(
trait_name Damageable
)");
	String trait_class_name = register_global_script_class(trait);
	CHECK_EQ(trait_class_name, "Damageable");

	FSParser extends_parser;
	Error err = analyze_source(extends_parser, R"(
class_name Player
extends Damageable
)",
			"user://trait_extends_error.fs");

	CHECK_EQ(err, ERR_PARSE_ERROR);
	CHECK(has_parser_error(extends_parser,
			R"(Class "Player" cannot extend trait "Damageable"; use "uses Damageable" instead.)"));

	const String new_source = R"(
class_name Player

trait Damageable:
	pass

func test() -> void:
	var _damageable = Damageable.new()
)";
	TempScriptFile new_script("trait_constructor_error.fs", new_source);

	FSParser new_parser;
	err = analyze_source(new_parser, new_source, new_script.path);

	CHECK_EQ(err, ERR_PARSE_ERROR);
	CHECK(has_parser_error(new_parser, R"(Cannot construct trait "Damageable".)"));
}

TEST_CASE("[Modules][FoundryScript] Analyzer accepts traits as static value types") {
	FSParser annotation_parser;
	Error err = analyze_source(annotation_parser, R"(
class_name Player

trait Damageable:
	pass

var _damageable: Damageable
)",
			"user://trait_type_annotation.fs");

	INFO(first_parser_error_message(annotation_parser));
	CHECK_EQ(err, OK);

	FSParser type_test_parser;
	err = analyze_source(type_test_parser, R"(
class_name Player

trait Damageable:
	pass

func test(value: Variant) -> void:
	var _result = value is Damageable
)",
			"user://trait_type_test.fs");

	INFO(first_parser_error_message(type_test_parser));
	CHECK_EQ(err, OK);

	FSParser cast_parser;
	err = analyze_source(cast_parser, R"(
class_name Player

trait Damageable:
	pass

func test(value: Variant) -> void:
	var _result = value as Damageable
)",
			"user://trait_cast.fs");

	INFO(first_parser_error_message(cast_parser));
	CHECK_EQ(err, OK);
}

TEST_CASE("[Modules][FoundryScript] Analyzer resolves trait signatures in trait scope") {
	const String source = R"(
class_name Player
uses Loader

trait Loader:
	class Payload:
		pass

	abstract func load(value: Payload) -> Payload

func load(value: Loader.Payload) -> Loader.Payload:
	return value
)";
	TempScriptFile script("trait_signature_scope_resolution.fs", source);

	FSParser parser;
	Error err = analyze_source(parser, source, script.path);

	INFO(first_parser_error_message(parser));
	CHECK_EQ(err, OK);
}

TEST_CASE("[Modules][FoundryScript] Analyzer emits one unresolved trait error") {
	FSParser parser;
	Error err = analyze_source(parser, R"(
class_name Player
uses MissingTrait
)",
			"user://trait_unresolved_single_error.fs");

	CHECK_EQ(err, ERR_PARSE_ERROR);
	CHECK_EQ(count_parser_errors(parser, R"(Could not resolve trait "MissingTrait".)"), 1);
}

TEST_CASE("[Modules][FoundryScript] Analyzer resolves trait composition without diamond duplication") {
	FSParser parser;
	Error err = analyze_source(parser, R"(
class_name Player
uses Damageable, Trackable

trait Identified:
	abstract func id() -> int

trait Damageable uses Identified:
	pass

trait Trackable uses Identified:
	pass

func id() -> int:
	return 1
)",
			"user://trait_diamond_resolution.fs");

	INFO(first_parser_error_message(parser));
	CHECK_EQ(err, OK);
	if (err != OK) {
		return;
	}

	const FSParser::ClassNode *root = parser.get_tree();
	CHECK_EQ(root->resolved_traits.size(), 3);
	CHECK_EQ(root->resolved_traits[0]->identifier->name, SNAME("Damageable"));
	CHECK_EQ(root->resolved_traits[1]->identifier->name, SNAME("Identified"));
	CHECK_EQ(root->resolved_traits[2]->identifier->name, SNAME("Trackable"));
}

TEST_CASE("[Modules][FoundryScript] Parser accepts contextual async function modifiers") {
	FSParser parser;
	Error err = parser.parse(R"(
abstract class_name AsyncParserContract

async func load() -> int:
	return 1

static async func make() -> int:
	return 2

abstract async func download_data() -> String

@rpc async func remote_load() -> void:
	pass

@rpc static async func remote_make() -> void:
	pass

class InlineAsync: async func tick() -> void: pass

class InlineStatic: static func make() -> int: return 3
)",
			"user://async_parser_contract.fs", false);

	CHECK_EQ(err, OK);
	if (err != OK) {
		return;
	}

	const FSParser::ClassNode *root = parser.get_tree();
	CHECK(root != nullptr);
	if (root == nullptr) {
		return;
	}

	const FSParser::FunctionNode *load = find_parser_function(root, SNAME("load"));
	CHECK(load != nullptr);
	if (load == nullptr) {
		return;
	}
	CHECK(load->is_declared_async);
	CHECK(load->is_coroutine);
	CHECK(!load->is_static);
#ifdef TOOLS_ENABLED
	CHECK_EQ(load->signature, "() -> int");
#endif // TOOLS_ENABLED

	const FSParser::FunctionNode *make = find_parser_function(root, SNAME("make"));
	CHECK(make != nullptr);
	if (make == nullptr) {
		return;
	}
	CHECK(make->is_declared_async);
	CHECK(make->is_coroutine);
	CHECK(make->is_static);
#ifdef TOOLS_ENABLED
	CHECK_EQ(make->signature, "() -> int");
#endif // TOOLS_ENABLED

	const FSParser::FunctionNode *download_data = find_parser_function(root, SNAME("download_data"));
	CHECK(download_data != nullptr);
	if (download_data == nullptr) {
		return;
	}
	CHECK(download_data->is_declared_async);
	CHECK(download_data->is_coroutine);
	CHECK(!download_data->is_static);
#ifdef TOOLS_ENABLED
	CHECK_EQ(download_data->signature, "() -> String");
#endif // TOOLS_ENABLED

	const FSParser::FunctionNode *remote_load = find_parser_function(root, SNAME("remote_load"));
	CHECK(remote_load != nullptr);
	if (remote_load == nullptr) {
		return;
	}
	CHECK(remote_load->is_declared_async);
	CHECK(remote_load->is_coroutine);
	CHECK(!remote_load->is_static);

	const FSParser::FunctionNode *remote_make = find_parser_function(root, SNAME("remote_make"));
	CHECK(remote_make != nullptr);
	if (remote_make == nullptr) {
		return;
	}
	CHECK(remote_make->is_declared_async);
	CHECK(remote_make->is_coroutine);
	CHECK(remote_make->is_static);

	const FSParser::ClassNode *inline_async = find_parser_class(root, SNAME("InlineAsync"));
	CHECK(inline_async != nullptr);
	if (inline_async == nullptr) {
		return;
	}
	const FSParser::FunctionNode *tick = find_parser_function(inline_async, SNAME("tick"));
	CHECK(tick != nullptr);
	if (tick == nullptr) {
		return;
	}
	CHECK(tick->is_declared_async);
	CHECK(tick->is_coroutine);
	CHECK(!tick->is_static);

	const FSParser::ClassNode *inline_static = find_parser_class(root, SNAME("InlineStatic"));
	CHECK(inline_static != nullptr);
	if (inline_static == nullptr) {
		return;
	}
	const FSParser::FunctionNode *inline_make = find_parser_function(inline_static, SNAME("make"));
	CHECK(inline_make != nullptr);
	if (inline_make == nullptr) {
		return;
	}
	CHECK(!inline_make->is_declared_async);
	CHECK(!inline_make->is_coroutine);
	CHECK(inline_make->is_static);
}

TEST_CASE("[Modules][FoundryScript] Parser stores enum host functions and ownership") {
	FSParser parser;
	Error err = parser.parse(R"(
## Log level documentation.
enum LogLevel:
	INFO = 1
	WARNING = 2

	## Name function documentation.
	@rpc func name() -> String:
		return "name"

	static async func parse(p_name: String) -> int:
		return p_name.length()

enum FunctionsOnly:
	func value() -> int:
		return 1

class EnumHost:
	enum Nested:
		ACTIVE = 1

		static func load() -> int:
			return ACTIVE
)",
			"user://enum_host_function_parser.fs", false);

	INFO(first_parser_error_message(parser));
	CHECK_EQ(err, OK);
	if (err != OK) {
		return;
	}

	const FSParser::ClassNode *root = parser.get_tree();
	CHECK(root != nullptr);
	if (root == nullptr) {
		return;
	}

	const FSParser::EnumNode *log_level = find_parser_enum(root, SNAME("LogLevel"));
	CHECK(log_level != nullptr);
	if (log_level == nullptr) {
		return;
	}
	CHECK_EQ(log_level->functions.size(), 2);
	if (log_level->functions.size() != 2) {
		return;
	}
	CHECK_EQ(log_level->functions[0]->identifier->name, SNAME("name"));
	CHECK_EQ(log_level->functions[0]->owner_enum, log_level);
	CHECK_FALSE(log_level->functions[0]->is_static);
	CHECK_FALSE(log_level->functions[0]->is_declared_async);
	CHECK_EQ(log_level->functions[0]->annotations.size(), 1);
#ifdef TOOLS_ENABLED
	CHECK_EQ(log_level->doc_data.description.strip_edges(), "Log level documentation.");
	CHECK_EQ(log_level->functions[0]->doc_data.description.strip_edges(), "Name function documentation.");
#endif // TOOLS_ENABLED
	CHECK_EQ(log_level->functions[1]->identifier->name, SNAME("parse"));
	CHECK_EQ(log_level->functions[1]->owner_enum, log_level);
	CHECK(log_level->functions[1]->is_static);
	CHECK(log_level->functions[1]->is_declared_async);
	CHECK(log_level->functions[1]->is_coroutine);
	CHECK_EQ(log_level->functions_indices[SNAME("name")], 0);
	CHECK_EQ(log_level->functions_indices[SNAME("parse")], 1);

	const FSParser::EnumNode *functions_only = find_parser_enum(root, SNAME("FunctionsOnly"));
	CHECK(functions_only != nullptr);
	if (functions_only == nullptr) {
		return;
	}
	CHECK(functions_only->values.is_empty());
	CHECK_EQ(functions_only->functions.size(), 1);
	if (functions_only->functions.size() != 1) {
		return;
	}
	CHECK_EQ(functions_only->functions[0]->owner_enum, functions_only);

	const FSParser::ClassNode *enum_host = find_parser_class(root, SNAME("EnumHost"));
	CHECK(enum_host != nullptr);
	if (enum_host == nullptr) {
		return;
	}
	const FSParser::EnumNode *nested = find_parser_enum(enum_host, SNAME("Nested"));
	CHECK(nested != nullptr);
	if (nested == nullptr) {
		return;
	}
	CHECK_EQ(nested->functions.size(), 1);
	if (nested->functions.size() != 1) {
		return;
	}
	CHECK_EQ(nested->functions[0]->owner_enum, nested);
	CHECK(nested->functions[0]->is_static);
}

TEST_CASE("[Modules][FoundryScript] Parser stores enum_name host functions") {
	FSParser parser;
	Error err = parser.parse(R"(
enum_name GlobalLogLevel:
	INFO = 1

	func name() -> String:
		return "info"
)",
			"user://global_log_level.fs", false);

	INFO(first_parser_error_message(parser));
	CHECK_EQ(err, OK);
	if (err != OK) {
		return;
	}

	const FSParser::ClassNode *root = parser.get_tree();
	CHECK(root != nullptr);
	if (root == nullptr) {
		return;
	}
	CHECK(root->enum_file_decl != nullptr);
	if (root->enum_file_decl == nullptr) {
		return;
	}
	CHECK_EQ(root->enum_file_decl->functions.size(), 1);
	if (root->enum_file_decl->functions.size() != 1) {
		return;
	}
	CHECK_EQ(root->enum_file_decl->functions[0]->identifier->name, SNAME("name"));
	CHECK_EQ(root->enum_file_decl->functions[0]->owner_enum, root->enum_file_decl);
}

TEST_CASE("[Modules][FoundryScript] Analyzer resolves enum host function signatures and bodies in named enums") {
	FSParser parser;
	Error err = analyze_source(parser, R"(
enum Status:
	READY = 1
	DONE = 2

	func identity() -> Self:
		return self

	static func normalize(value: Status) -> Status:
		return value
)",
			"user://enum_host_function_analyzer.fs");

	INFO(first_parser_error_message(parser));
	CHECK_EQ(err, OK);
	if (err != OK) {
		return;
	}

	const FSParser::ClassNode *root = parser.get_tree();
	CHECK(root != nullptr);
	if (root == nullptr) {
		return;
	}

	const FSParser::EnumNode *status = find_parser_enum(root, SNAME("Status"));
	CHECK(status != nullptr);
	if (status == nullptr) {
		return;
	}

	const FSParser::FunctionNode *identity = find_enum_function(status, SNAME("identity"));
	CHECK(identity != nullptr);
	if (identity == nullptr) {
		return;
	}
	CHECK(identity->resolved_signature);
	CHECK(identity->resolved_body);

	const FSParser::DataType &identity_type = identity->get_datatype();
	CHECK_EQ(identity_type.kind, FSParser::DataType::ENUM);
	CHECK_EQ(identity_type.builtin_type, Variant::INT);
	CHECK_FALSE(identity_type.is_meta_type);
	CHECK_EQ(identity_type.enum_type, SNAME("Status"));
	CHECK_EQ(identity_type.class_type, root);
	CHECK_EQ(identity_type.script_path, "user://enum_host_function_analyzer.fs");
	CHECK_EQ(identity_type.enum_values.size(), 2);

	CHECK_EQ(identity->body->statements.size(), 1);
	if (identity->body->statements.size() == 1) {
		const FSParser::Node *statement = identity->body->statements[0];
		CHECK_EQ(statement->type, FSParser::Node::RETURN);
		if (statement->type == FSParser::Node::RETURN) {
			const FSParser::ReturnNode *return_statement = static_cast<const FSParser::ReturnNode *>(statement);
			CHECK(return_statement->return_value != nullptr);
			if (return_statement->return_value != nullptr) {
				CHECK_EQ(return_statement->return_value->type, FSParser::Node::SELF);
				const FSParser::DataType &self_type = return_statement->return_value->get_datatype();
				CHECK_EQ(self_type.kind, FSParser::DataType::ENUM);
				CHECK_EQ(self_type.builtin_type, Variant::INT);
				CHECK_FALSE(self_type.is_meta_type);
				CHECK_EQ(self_type.enum_type, SNAME("Status"));
				CHECK_EQ(self_type.class_type, root);
				CHECK_EQ(self_type.script_path, "user://enum_host_function_analyzer.fs");
				CHECK_EQ(self_type.enum_values.size(), 2);
			}
		}
	}

	const FSParser::FunctionNode *normalize = find_enum_function(status, SNAME("normalize"));
	CHECK(normalize != nullptr);
	if (normalize != nullptr) {
		CHECK(normalize->resolved_signature);
		CHECK(normalize->resolved_body);
		CHECK(normalize->is_static);
		CHECK_EQ(normalize->get_datatype().kind, FSParser::DataType::ENUM);
		CHECK_EQ(normalize->get_datatype().enum_type, SNAME("Status"));
		CHECK_EQ(normalize->parameters.size(), 1);
		if (normalize->parameters.size() == 1) {
			CHECK_EQ(normalize->parameters[0]->get_datatype().kind, FSParser::DataType::ENUM);
			CHECK_EQ(normalize->parameters[0]->get_datatype().enum_type, SNAME("Status"));
		}
	}
}

TEST_CASE("[Modules][FoundryScript] Analyzer resolves enum host function signatures and bodies in enum_name files") {
	GlobalScriptClassCacheBackup backup;
	ScriptServer::global_classes_clear();

	const String source = R"(
namespace tests.enums
enum_name GlobalStatus:
	READY = 1
	DONE = 2

	func identity() -> Self:
		return self

	static func normalize(value: Self) -> Self:
		return value

	static func unqualified_value() -> Self:
		return READY

	static func qualified_value() -> Self:
		return GlobalStatus.DONE
)";
	TempScriptFile script("global_status.fs", source);

	String base_type;
	bool is_abstract = false;
	bool is_tool = false;
	bool is_trait = false;
	bool is_enum = false;
	const String global_name = FSLanguage::get_singleton()->get_global_class_name(script.path, &base_type, nullptr,
			&is_abstract, &is_tool, &is_trait, &is_enum);
	CHECK_EQ(global_name, "tests.enums.GlobalStatus");
	CHECK(is_enum);
	if (global_name.is_empty() || !is_enum) {
		return;
	}
	ScriptServer::add_global_class(global_name, base_type, FSLanguage::get_singleton()->get_name(), script.path,
			is_abstract, is_tool, is_trait, is_enum);

	FSParser parser;
	Error err = analyze_source(parser, source, script.path);
	INFO(first_parser_error_message(parser));
	CHECK_EQ(err, OK);
	if (err != OK) {
		return;
	}

	const FSParser::ClassNode *root = parser.get_tree();
	CHECK(root != nullptr);
	if (root == nullptr || root->enum_file_decl == nullptr) {
		return;
	}
	const FSParser::EnumNode *status = root->enum_file_decl;

	const FSParser::FunctionNode *identity = find_enum_function(status, SNAME("identity"));
	CHECK(identity != nullptr);
	if (identity == nullptr) {
		return;
	}
	CHECK(identity->resolved_signature);
	CHECK(identity->resolved_body);
	CHECK_EQ(identity->get_datatype().kind, FSParser::DataType::ENUM);
	CHECK_EQ(identity->get_datatype().builtin_type, Variant::INT);
	CHECK_FALSE(identity->get_datatype().is_meta_type);
	CHECK_EQ(identity->get_datatype().enum_type, SNAME("tests.enums.GlobalStatus"));
	CHECK_EQ(identity->get_datatype().native_type, SNAME("tests.enums.GlobalStatus"));
	CHECK_EQ(identity->get_datatype().class_type, root);
	CHECK_EQ(identity->get_datatype().script_path, script.path);
	CHECK_EQ(identity->get_datatype().enum_values.size(), 2);

	CHECK_EQ(identity->body->statements.size(), 1);
	if (identity->body->statements.size() == 1) {
		const FSParser::ReturnNode *return_statement =
				static_cast<const FSParser::ReturnNode *>(identity->body->statements[0]);
		CHECK(return_statement->return_value != nullptr);
		if (return_statement->return_value != nullptr) {
			const FSParser::DataType &self_type = return_statement->return_value->get_datatype();
			CHECK_EQ(self_type.kind, FSParser::DataType::ENUM);
			CHECK_FALSE(self_type.is_meta_type);
			CHECK_EQ(self_type.enum_type, SNAME("tests.enums.GlobalStatus"));
			CHECK_EQ(self_type.native_type, SNAME("tests.enums.GlobalStatus"));
			CHECK_EQ(self_type.class_type, root);
			CHECK_EQ(self_type.script_path, script.path);
			CHECK_EQ(self_type.enum_values.size(), 2);
		}
	}

	const FSParser::FunctionNode *normalize = find_enum_function(status, SNAME("normalize"));
	CHECK(normalize != nullptr);
	if (normalize != nullptr) {
		CHECK(normalize->resolved_signature);
		CHECK(normalize->resolved_body);
		CHECK(normalize->is_static);
		CHECK_EQ(normalize->get_datatype().kind, FSParser::DataType::ENUM);
		CHECK_EQ(normalize->get_datatype().enum_type, SNAME("tests.enums.GlobalStatus"));
		CHECK_EQ(normalize->parameters.size(), 1);
		if (normalize->parameters.size() == 1) {
			CHECK_EQ(normalize->parameters[0]->get_datatype().kind, FSParser::DataType::ENUM);
			CHECK_EQ(normalize->parameters[0]->get_datatype().enum_type, SNAME("tests.enums.GlobalStatus"));
		}
	}

	for (const StringName &function_name : { SNAME("unqualified_value"), SNAME("qualified_value") }) {
		const FSParser::FunctionNode *value_function = find_enum_function(status, function_name);
		CHECK(value_function != nullptr);
		if (value_function != nullptr) {
			CHECK(value_function->resolved_signature);
			CHECK(value_function->resolved_body);
			CHECK_EQ(value_function->get_datatype().kind, FSParser::DataType::ENUM);
			CHECK_EQ(value_function->get_datatype().enum_type, SNAME("tests.enums.GlobalStatus"));
		}
	}
}

TEST_CASE("[Modules][FoundryScript] Analyzer resolves enum host function calls and records same-file metadata") {
	FSParser parser;
	const String path = "user://enum_host_function_call_metadata.fs";
	Error err = analyze_source(parser, R"(
enum Status:
	READY = 1
	DONE = 2

	func label() -> String:
		return "ready"

	func keys() -> String:
		return "instance keys"

	static func parse(text: String) -> Self:
		return READY if text == "ready" else DONE


func probe() -> void:
	var value: Status = Status.DONE
	var literal_call: String = Status.READY.label()
	var typed_call: String = value.label()
	var instance_keys_call: String = value.keys()
	var static_call: Status = Status.parse("ready")
	var dictionary_call: Array = Status.keys()
	var instance_callable: Callable[[], String] = value.label
	var static_callable: Callable[[String], Status] = Status.parse
)",
			path);

	INFO(first_parser_error_message(parser));
	CHECK_EQ(err, OK);
	if (err != OK) {
		return;
	}

	const FSParser::ClassNode *root = parser.get_tree();
	const FSParser::EnumNode *status = find_parser_enum(root, SNAME("Status"));
	const FSParser::FunctionNode *probe = find_parser_function(root, SNAME("probe"));
	CHECK(status != nullptr);
	CHECK(probe != nullptr);
	if (status == nullptr || probe == nullptr) {
		return;
	}

	const FSParser::FunctionNode *label = find_enum_function(status, SNAME("label"));
	const FSParser::FunctionNode *parse = find_enum_function(status, SNAME("parse"));
	CHECK(label != nullptr);
	CHECK(parse != nullptr);
	if (label == nullptr || parse == nullptr) {
		return;
	}

	const FSParser::CallNode *literal_call = find_local_variable_call(probe, SNAME("literal_call"));
	const FSParser::CallNode *typed_call = find_local_variable_call(probe, SNAME("typed_call"));
	const FSParser::CallNode *instance_keys_call = find_local_variable_call(probe, SNAME("instance_keys_call"));
	const FSParser::CallNode *static_call = find_local_variable_call(probe, SNAME("static_call"));
	const FSParser::CallNode *dictionary_call = find_local_variable_call(probe, SNAME("dictionary_call"));
	CHECK(literal_call != nullptr);
	CHECK(typed_call != nullptr);
	CHECK(instance_keys_call != nullptr);
	CHECK(static_call != nullptr);
	CHECK(dictionary_call != nullptr);
	if (literal_call == nullptr || typed_call == nullptr || instance_keys_call == nullptr || static_call == nullptr || dictionary_call == nullptr) {
		return;
	}

	for (const FSParser::CallNode *call : { literal_call, typed_call }) {
		CHECK_EQ(call->enum_call_kind, FSParser::CallNode::ENUM_CALL_INSTANCE);
		CHECK_EQ(call->enum_call_owner_script_path, path);
		CHECK_EQ(call->enum_call_owner_class, root->fqcn);
		CHECK_EQ(call->enum_call_enum_type, SNAME("Status"));
		CHECK_EQ(call->enum_call_function, SNAME("label"));
		CHECK_FALSE(call->is_static);
		CHECK_EQ(call->get_datatype().builtin_type, Variant::STRING);
	}

	CHECK_EQ(instance_keys_call->enum_call_kind, FSParser::CallNode::ENUM_CALL_INSTANCE);
	CHECK_EQ(instance_keys_call->enum_call_owner_script_path, path);
	CHECK_EQ(instance_keys_call->enum_call_owner_class, root->fqcn);
	CHECK_EQ(instance_keys_call->enum_call_enum_type, SNAME("Status"));
	CHECK_EQ(instance_keys_call->enum_call_function, SNAME("keys"));
	CHECK_FALSE(instance_keys_call->is_static);
	CHECK_EQ(instance_keys_call->get_datatype().builtin_type, Variant::STRING);

	CHECK_EQ(static_call->enum_call_kind, FSParser::CallNode::ENUM_CALL_STATIC);
	CHECK_EQ(static_call->enum_call_owner_script_path, path);
	CHECK_EQ(static_call->enum_call_owner_class, root->fqcn);
	CHECK_EQ(static_call->enum_call_enum_type, SNAME("Status"));
	CHECK_EQ(static_call->enum_call_function, SNAME("parse"));
	CHECK(static_call->is_static);
	CHECK_EQ(static_call->get_datatype().kind, FSParser::DataType::ENUM);
	CHECK_EQ(static_call->get_datatype().enum_type, SNAME("Status"));

	CHECK_EQ(dictionary_call->enum_call_kind, FSParser::CallNode::ENUM_CALL_NONE);
	CHECK(dictionary_call->enum_call_owner_script_path.is_empty());
	CHECK_EQ(dictionary_call->enum_call_owner_class, StringName());
	CHECK_EQ(dictionary_call->enum_call_enum_type, StringName());
	CHECK_EQ(dictionary_call->enum_call_function, StringName());

	const FSParser::VariableNode *instance_callable = find_function_local_variable(probe, SNAME("instance_callable"));
	const FSParser::VariableNode *static_callable = find_function_local_variable(probe, SNAME("static_callable"));
	CHECK(instance_callable != nullptr);
	CHECK(static_callable != nullptr);
	if (instance_callable == nullptr || static_callable == nullptr) {
		return;
	}
	CHECK(instance_callable->initializer != nullptr);
	CHECK(static_callable->initializer != nullptr);
	if (instance_callable->initializer == nullptr || static_callable->initializer == nullptr ||
			instance_callable->initializer->type != FSParser::Node::SUBSCRIPT ||
			static_callable->initializer->type != FSParser::Node::SUBSCRIPT) {
		return;
	}

	const FSParser::SubscriptNode *instance_attribute =
			static_cast<const FSParser::SubscriptNode *>(instance_callable->initializer);
	const FSParser::SubscriptNode *static_attribute =
			static_cast<const FSParser::SubscriptNode *>(static_callable->initializer);
	CHECK(instance_attribute->attribute != nullptr);
	CHECK(static_attribute->attribute != nullptr);
	if (instance_attribute->attribute == nullptr || static_attribute->attribute == nullptr) {
		return;
	}
	CHECK_EQ(instance_attribute->attribute->source, FSParser::IdentifierNode::MEMBER_FUNCTION);
	CHECK_EQ(instance_attribute->attribute->function_source, label);
	CHECK_FALSE(instance_attribute->attribute->function_source_is_static);
	CHECK_EQ(static_attribute->attribute->source, FSParser::IdentifierNode::MEMBER_FUNCTION);
	CHECK_EQ(static_attribute->attribute->function_source, parse);
	CHECK(static_attribute->attribute->function_source_is_static);

	const FSParser::DataType &instance_callable_type = instance_attribute->get_datatype();
	CHECK_EQ(instance_callable_type.kind, FSParser::DataType::BUILTIN);
	CHECK_EQ(instance_callable_type.builtin_type, Variant::CALLABLE);
	CHECK(instance_callable_type.has_explicit_method_signature);
	CHECK_EQ(instance_callable_type.method_parameter_types.size(), 0);
	CHECK_EQ(instance_callable_type.method_return_type.size(), 1);
	if (instance_callable_type.method_return_type.size() == 1) {
		CHECK_EQ(instance_callable_type.method_return_type[0].builtin_type, Variant::STRING);
	}

	const FSParser::DataType &static_callable_type = static_attribute->get_datatype();
	CHECK_EQ(static_callable_type.kind, FSParser::DataType::BUILTIN);
	CHECK_EQ(static_callable_type.builtin_type, Variant::CALLABLE);
	CHECK(static_callable_type.has_explicit_method_signature);
	CHECK_EQ(static_callable_type.method_parameter_types.size(), 1);
	CHECK_EQ(static_callable_type.method_return_type.size(), 1);
	if (static_callable_type.method_parameter_types.size() == 1) {
		CHECK_EQ(static_callable_type.method_parameter_types[0].builtin_type, Variant::STRING);
	}
	if (static_callable_type.method_return_type.size() == 1) {
		CHECK_EQ(static_callable_type.method_return_type[0].kind, FSParser::DataType::ENUM);
		CHECK_EQ(static_callable_type.method_return_type[0].enum_type, SNAME("Status"));
	}
}

TEST_CASE("[Modules][FoundryScript] Analyzer leaves invalid enum host calls without dispatch metadata") {
	FSParser parser;
	Error err = analyze_source(parser, R"(
enum Status:
	READY = 1

	func label() -> String:
		return "ready"

	static func parse() -> Self:
		return READY


func probe() -> void:
	Status.READY.parse()
	Status.label()
)",
			"user://invalid_enum_host_function_call_metadata.fs");

	CHECK_NE(err, OK);
	CHECK(has_parser_error(parser, R"ERR(Cannot call static enum function "parse()" on enum value "Status".)ERR"));
	CHECK(has_parser_error(parser, R"ERR(Cannot call instance enum function "label()" on enum type "Status".)ERR"));

	const FSParser::FunctionNode *probe = find_parser_function(parser.get_tree(), SNAME("probe"));
	CHECK(probe != nullptr);
	if (probe == nullptr) {
		return;
	}
	CHECK_EQ(probe->body->statements.size(), 2);
	if (probe->body->statements.size() != 2) {
		return;
	}

	for (const FSParser::Node *statement : probe->body->statements) {
		CHECK(statement != nullptr);
		CHECK_EQ(statement->type, FSParser::Node::CALL);
		if (statement == nullptr || statement->type != FSParser::Node::CALL) {
			continue;
		}
		const FSParser::CallNode *call = static_cast<const FSParser::CallNode *>(statement);
		CHECK_EQ(call->enum_call_kind, FSParser::CallNode::ENUM_CALL_NONE);
		CHECK(call->enum_call_owner_script_path.is_empty());
		CHECK_EQ(call->enum_call_owner_class, StringName());
		CHECK_EQ(call->enum_call_enum_type, StringName());
		CHECK_EQ(call->enum_call_function, StringName());
	}
}

TEST_CASE("[Modules][FoundryScript] Analyzer records nested enum host function owner metadata") {
	FSParser parser;
	const String path = "user://nested_enum_host_function_call.fs";
	Error err = analyze_source(parser, R"(
class Outer:
	class Inner:
		enum Status:
			READY = 1

			static func parse() -> Self:
				return READY

		func probe() -> void:
			var call: Status = Status.parse()
)",
			path);

	INFO(first_parser_error_message(parser));
	CHECK_EQ(err, OK);
	if (err != OK) {
		return;
	}

	const FSParser::ClassNode *root = parser.get_tree();
	const FSParser::ClassNode *outer = find_parser_class(root, SNAME("Outer"));
	const FSParser::ClassNode *inner = find_parser_class(outer, SNAME("Inner"));
	const FSParser::FunctionNode *probe = find_parser_function(inner, SNAME("probe"));
	const FSParser::CallNode *call = find_local_variable_call(probe, SNAME("call"));
	CHECK(inner != nullptr);
	CHECK(call != nullptr);
	if (inner == nullptr || call == nullptr) {
		return;
	}

	CHECK_EQ(call->enum_call_kind, FSParser::CallNode::ENUM_CALL_STATIC);
	CHECK_EQ(call->enum_call_owner_script_path, path);
	CHECK_EQ(call->enum_call_owner_class, inner->fqcn);
	CHECK_EQ(call->enum_call_enum_type, SNAME("Status"));
	CHECK_EQ(call->enum_call_function, SNAME("parse"));
}

TEST_CASE("[Modules][FoundryScript] Compiler stores enum host functions outside member methods at runtime") {
	ScopedFSNativeGlobals native_globals;
	FSParser parser;
	const String path = "user://enum_host_function_runtime_storage.fs";
	Error err = parser.parse(R"(
enum RootStatus:
	READY = 1

	func describe() -> String:
		return str(self)

	static func initial() -> Self:
		return READY

class Left:
	enum LeftStatus:
		READY = 11

		func describe() -> String:
			return "left"

class Right:
	enum RightStatus:
		READY = 22

		func describe() -> String:
			return "right"

func ordinary() -> void:
	pass
)",
			path, false);
	CHECK_EQ(err, OK);
	if (err != OK) {
		return;
	}

	FSAnalyzer analyzer(&parser);
	err = analyzer.analyze();
	INFO(first_parser_error_message(parser));
	CHECK_EQ(err, OK);
	if (err != OK) {
		return;
	}

	Ref<FoundryScript> script;
	script.instantiate();
	script->set_path(path);
	FSCompiler compiler;
	err = compiler.compile(&parser, script.ptr(), false);
	INFO(compiler.get_error());
	CHECK_EQ(err, OK);
	if (err != OK) {
		return;
	}

	CHECK(script->get_member_functions().has(SNAME("ordinary")));
	CHECK_FALSE(script->get_member_functions().has(SNAME("describe")));
	CHECK_FALSE(script->get_member_functions().has(SNAME("initial")));

	FSFunction *root_instance = script->get_enum_function(SNAME("RootStatus"), SNAME("describe"), false);
	FSFunction *root_static = script->get_enum_function(SNAME("RootStatus"), SNAME("initial"), true);
	CHECK(root_instance != nullptr);
	CHECK(root_static != nullptr);
	if (root_instance == nullptr || root_static == nullptr) {
		return;
	}
	CHECK_NE(root_instance, root_static);
	CHECK_EQ(root_instance->get_script(), script.ptr());
	CHECK_EQ(root_static->get_script(), script.ptr());
	CHECK(script->get_enum_function(SNAME("RootStatus"), SNAME("initial"), false) == nullptr);
	CHECK(script->get_enum_function(SNAME("RootStatus"), SNAME("describe"), true) == nullptr);

	const FSParser::ClassNode *left_node = find_parser_class(parser.get_tree(), SNAME("Left"));
	const FSParser::ClassNode *right_node = find_parser_class(parser.get_tree(), SNAME("Right"));
	CHECK(left_node != nullptr);
	CHECK(right_node != nullptr);
	if (left_node == nullptr || right_node == nullptr) {
		return;
	}
	FoundryScript *left_script = script->find_class(left_node->fqcn);
	FoundryScript *right_script = script->find_class(right_node->fqcn);
	CHECK(left_script != nullptr);
	CHECK(right_script != nullptr);
	if (left_script == nullptr || right_script == nullptr) {
		return;
	}

	FSFunction *left_describe = left_script->get_enum_function(SNAME("LeftStatus"), SNAME("describe"), false);
	FSFunction *right_describe = right_script->get_enum_function(SNAME("RightStatus"), SNAME("describe"), false);
	CHECK(left_describe != nullptr);
	CHECK(right_describe != nullptr);
	if (left_describe == nullptr || right_describe == nullptr) {
		return;
	}
	CHECK_NE(left_describe, right_describe);
	CHECK_EQ(left_describe->get_script(), left_script);
	CHECK_EQ(right_describe->get_script(), right_script);
	CHECK(script->get_enum_function(SNAME("Missing"), SNAME("describe"), false) == nullptr);

	script->clear();
	CHECK(script->get_enum_function(SNAME("RootStatus"), SNAME("describe"), false) == nullptr);
	CHECK(script->get_enum_function(SNAME("RootStatus"), SNAME("initial"), true) == nullptr);
}

TEST_CASE("[Modules][FoundryScript] Enum host function lookup failures report the stable owner identity") {
	const String path = "user://enum_host_function_lookup_failure.fs";
	Ref<FoundryScript> script;
	script.instantiate();
	script->set_path(path);

	struct LookupCase {
		StringName owner_class;
		StringName enum_type;
		StringName function;
		bool is_static = false;
		String expected_reason;
	};

	const LookupCase cases[] = {
		{ StringName(path + "::MissingOwner"), SNAME("Status"), SNAME("parse"), true, "the owner class was not found" },
		{ StringName(path), SNAME("MissingStatus"), SNAME("parse"), true, "the compiled function was not found" },
		{ StringName(path), SNAME("Status"), SNAME("missing_label"), false, "the compiled function was not found" },
	};

	for (const LookupCase &lookup_case : cases) {
		FSByteCodeGenerator generator;
		generator.write_start(script.ptr(), "enum_lookup_probe", true, Variant(), FSDataType());

		const uint32_t target_index = generator.add_temporary(FSDataType());
		const FSCodeGenerator::Address target(FSCodeGenerator::Address::TEMPORARY, target_index);
		const Variant receiver = lookup_case.is_static ? Variant(Dictionary()) : Variant(7);
		const uint32_t receiver_index = generator.add_or_get_constant(receiver);
		const FSCodeGenerator::Address base(FSCodeGenerator::Address::CONSTANT, receiver_index);
		generator.write_enum_call(target, base, Vector<FSCodeGenerator::Address>(), StringName(path),
				lookup_case.owner_class, lookup_case.enum_type, lookup_case.function, lookup_case.is_static, false);
		generator.write_return(target);
		generator.pop_temporary();

		FSFunction *function = generator.write_end();
		CHECK(function != nullptr);
		if (function == nullptr) {
			continue;
		}
		CHECK_EQ(FSBytecodeVerifier::verify_function(function, 0, path), OK);

		FSScriptTestGuard::GuardRecord guard;
		FSScriptTestGuard::push(&guard);
		Callable::CallError call_error;
		ERR_PRINT_OFF;
		function->call(nullptr, nullptr, 0, call_error);
		ERR_PRINT_ON;
		FSScriptTestGuard::pop(&guard);

		CHECK_EQ(call_error.error, Callable::CallError::CALL_OK);
		CHECK(guard.runtime_error_occurred);
		const String call_kind = lookup_case.is_static ? "static enum function" : "instance enum function";
		CHECK(guard.runtime_error_message.contains(call_kind));
		CHECK(guard.runtime_error_message.contains(String(lookup_case.owner_class)));
		const String function_identity = String(lookup_case.enum_type) + "." + String(lookup_case.function) + "()";
		CHECK(guard.runtime_error_message.contains(function_identity));
		CHECK(guard.runtime_error_message.contains(lookup_case.expected_reason));

		memdelete(function);
	}
}

TEST_CASE("[Modules][FoundryScript] Analyzer records cross-file enum_name host function metadata") {
	GlobalScriptClassCacheBackup backup;
	ScriptServer::global_classes_clear();

	TempScriptFile enum_script("remote_status.fs", R"(
namespace tests.enumcalls
enum_name RemoteStatus:
	READY = 1
	DONE = 2

	func label() -> String:
		return "ready"

	static func parse(text: String) -> Self:
		return READY if text == "ready" else DONE
)");

	String base_type;
	bool is_abstract = false;
	bool is_tool = false;
	bool is_trait = false;
	bool is_enum = false;
	const String global_name = FSLanguage::get_singleton()->get_global_class_name(enum_script.path, &base_type, nullptr,
			&is_abstract, &is_tool, &is_trait, &is_enum);
	CHECK_EQ(global_name, "tests.enumcalls.RemoteStatus");
	CHECK(is_enum);
	if (global_name.is_empty() || !is_enum) {
		return;
	}
	ScriptServer::add_global_class(global_name, base_type, FSLanguage::get_singleton()->get_name(), enum_script.path,
			is_abstract, is_tool, is_trait, is_enum);

	FSParser parser;
	Error err = analyze_source(parser, R"(
namespace tests.consumer
import tests.enumcalls

func probe() -> void:
	var value: RemoteStatus = RemoteStatus.DONE
	var instance_call: String = value.label()
	var static_call: RemoteStatus = RemoteStatus.parse("ready")
	var dictionary_call: Array = RemoteStatus.keys()
)",
			"user://enum_host_function_cross_file_consumer.fs");

	INFO(first_parser_error_message(parser));
	CHECK_EQ(err, OK);
	if (err == OK) {
		const FSParser::FunctionNode *probe = find_parser_function(parser.get_tree(), SNAME("probe"));
		const FSParser::CallNode *instance_call = find_local_variable_call(probe, SNAME("instance_call"));
		const FSParser::CallNode *static_call = find_local_variable_call(probe, SNAME("static_call"));
		const FSParser::CallNode *dictionary_call = find_local_variable_call(probe, SNAME("dictionary_call"));
		CHECK(instance_call != nullptr);
		CHECK(static_call != nullptr);
		CHECK(dictionary_call != nullptr);
		if (instance_call != nullptr && static_call != nullptr && dictionary_call != nullptr) {
			CHECK_EQ(instance_call->enum_call_kind, FSParser::CallNode::ENUM_CALL_INSTANCE);
			CHECK_EQ(instance_call->enum_call_owner_script_path, enum_script.path);
			CHECK_EQ(instance_call->enum_call_owner_class, SNAME("tests.enumcalls.RemoteStatus"));
			CHECK_EQ(instance_call->enum_call_enum_type, SNAME("tests.enumcalls.RemoteStatus"));
			CHECK_EQ(instance_call->enum_call_function, SNAME("label"));

			CHECK_EQ(static_call->enum_call_kind, FSParser::CallNode::ENUM_CALL_STATIC);
			CHECK_EQ(static_call->enum_call_owner_script_path, enum_script.path);
			CHECK_EQ(static_call->enum_call_owner_class, SNAME("tests.enumcalls.RemoteStatus"));
			CHECK_EQ(static_call->enum_call_enum_type, SNAME("tests.enumcalls.RemoteStatus"));
			CHECK_EQ(static_call->enum_call_function, SNAME("parse"));

			CHECK_EQ(dictionary_call->enum_call_kind, FSParser::CallNode::ENUM_CALL_NONE);
			CHECK(dictionary_call->enum_call_owner_script_path.is_empty());
			CHECK_EQ(dictionary_call->enum_call_owner_class, StringName());
			CHECK_EQ(dictionary_call->enum_call_enum_type, StringName());
			CHECK_EQ(dictionary_call->enum_call_function, StringName());
		}
	}

	FSCache::remove_parser(enum_script.path);
	FSCache::remove_script(enum_script.path);
}

TEST_CASE("[Modules][FoundryScript] Parser rejects invalid enum host function declarations") {
	check_parse_source_error(R"(
enum Ordered:
	func name() -> String:
		return "name"
	A = 0
)",
			"Enum values must be declared before enum functions.");

	check_parse_source_error(R"(
enum:
	A = 0
	func name() -> String:
		return "name"
)",
			"Only named enums can declare functions.");

	check_parse_source_error(R"(
enum InvalidMember:
	A = 0
	var value = 1
)",
			"Only function declarations are allowed in enum bodies.");

	check_parse_source_error(R"(
enum AbstractFunction:
	abstract func name() -> String
)",
			R"(The "abstract" modifier cannot be applied to enum functions.)");

	check_parse_source_error(R"(
enum FinalFunction:
	final func name() -> String:
		return "name"
)",
			R"(The "final" modifier cannot be applied to enum functions.)");
}

TEST_CASE("[Modules][FoundryScript] Parser rejects invalid async function modifier positions") {
	check_parse_source_error(R"(
func async load() -> void:
	pass
)",
			R"("async" must appear before "func" when used as a function modifier.)");

	check_parse_source_error(R"(
async var value = 1
)",
			R"(The "async" modifier cannot be applied to variables.)");

	FSParser parser;
	Error err = parser.parse(R"(
func async load() -> void:
	pass

func after() -> void:
	pass
)",
			"user://async_recovery.fs", false);
	CHECK(err != OK);
	CHECK_EQ(parser.get_errors().size(), 1);

	const FSParser::ClassNode *root = parser.get_tree();
	CHECK(root != nullptr);
	if (root == nullptr) {
		return;
	}
	CHECK(find_parser_function(root, SNAME("after")) != nullptr);

	FSParser inline_parser;
	Error inline_err = inline_parser.parse(R"(
class BrokenInline: static async var value = 1

func after_inline() -> void:
	pass
)",
			"user://async_inline_recovery.fs", false);
	CHECK(inline_err != OK);
	CHECK_GE(inline_parser.get_errors().size(), 1);

	const FSParser::ClassNode *inline_root = inline_parser.get_tree();
	CHECK(inline_root != nullptr);
	if (inline_root == nullptr) {
		return;
	}
	CHECK(find_parser_function(inline_root, SNAME("after_inline")) != nullptr);
}

TEST_CASE("[Modules][FoundryScript] Parser recovery preserves a leading async modifier") {
	// The malformed `var = ...` aborts mid-declaration with the contextual `async`
	// identifier as the next token, so the recovery in `synchronize()` must stop at
	// `async` rather than skipping over it and dropping the recovered function's
	// `is_declared_async` / `is_coroutine` metadata.
	FSParser parser;
	Error err = parser.parse(R"(
var = async func load() -> void:
	pass

func after() -> void:
	pass
)",
			"user://async_modifier_recovery.fs", false);

	CHECK(err != OK);

	const FSParser::ClassNode *root = parser.get_tree();
	CHECK(root != nullptr);
	if (root == nullptr) {
		return;
	}

	const FSParser::FunctionNode *load = find_parser_function(root, SNAME("load"));
	CHECK(load != nullptr);
	if (load != nullptr) {
		CHECK(load->is_declared_async);
		CHECK(load->is_coroutine);
	}

	CHECK(find_parser_function(root, SNAME("after")) != nullptr);
}

TEST_CASE("[Modules][FoundryScript] Parser recovery preserves a leading abstract modifier") {
	// The malformed `var = ...` aborts mid-declaration with the `abstract` keyword as
	// the next token, so the recovery in `synchronize()` must stop at `abstract`
	// rather than skipping over it and dropping the recovered declaration's
	// `is_abstract` metadata.
	FSParser parser;
	Error err = parser.parse(R"(
var = abstract func load() -> void:
	pass

func after() -> void:
	pass
)",
			"user://abstract_func_modifier_recovery.fs", false);

	CHECK(err != OK);

	const FSParser::ClassNode *root = parser.get_tree();
	CHECK(root != nullptr);
	if (root == nullptr) {
		return;
	}

	const FSParser::FunctionNode *load = find_parser_function(root, SNAME("load"));
	CHECK(load != nullptr);
	if (load != nullptr) {
		CHECK(load->is_abstract);
	}

	CHECK(find_parser_function(root, SNAME("after")) != nullptr);

	// The same recovery must keep `is_abstract` on a recovered `abstract class`.
	FSParser class_parser;
	Error class_err = class_parser.parse(R"(
var = abstract class Shape:
	func area() -> float:
		return 0.0

func after() -> void:
	pass
)",
			"user://abstract_class_modifier_recovery.fs", false);

	CHECK(class_err != OK);

	const FSParser::ClassNode *class_root = class_parser.get_tree();
	CHECK(class_root != nullptr);
	if (class_root == nullptr) {
		return;
	}

	const FSParser::ClassNode *shape = find_parser_class(class_root, SNAME("Shape"));
	CHECK(shape != nullptr);
	if (shape != nullptr) {
		CHECK(shape->is_abstract);
	}

	CHECK(find_parser_function(class_root, SNAME("after")) != nullptr);
}

TEST_CASE("[Modules][FoundryScript] Parser collects declaration modifiers in any order") {
	FSParser parser;
	Error err = parser.parse(R"(
abstract class Shape:
	abstract func area() -> float

abstract trait Drawable:
	abstract func draw() -> void

async static func make() -> int:
	return 1

static async func build() -> int:
	return 2
)",
			"user://declaration_modifiers.fs", false);

	CHECK_EQ(err, OK);
	if (err != OK) {
		return;
	}

	const FSParser::ClassNode *root = parser.get_tree();
	CHECK(root != nullptr);
	if (root == nullptr) {
		return;
	}

	const FSParser::ClassNode *shape = find_parser_class(root, SNAME("Shape"));
	CHECK(shape != nullptr);
	if (shape != nullptr) {
		CHECK(shape->is_abstract);
		const FSParser::FunctionNode *area = find_parser_function(shape, SNAME("area"));
		CHECK(area != nullptr);
		if (area != nullptr) {
			CHECK(area->is_abstract);
		}
	}

	const FSParser::ClassNode *drawable = find_parser_trait(root, SNAME("Drawable"));
	CHECK(drawable != nullptr);
	if (drawable != nullptr) {
		CHECK(drawable->is_abstract);
		CHECK(drawable->is_trait);
	}

	const FSParser::FunctionNode *make = find_parser_function(root, SNAME("make"));
	CHECK(make != nullptr);
	if (make != nullptr) {
		CHECK(make->is_static);
		CHECK(make->is_declared_async);
		CHECK(make->is_coroutine);
	}

	const FSParser::FunctionNode *build = find_parser_function(root, SNAME("build"));
	CHECK(build != nullptr);
	if (build != nullptr) {
		CHECK(build->is_static);
		CHECK(build->is_declared_async);
	}
}

TEST_CASE("[Modules][FoundryScript] Parser allows static abstract methods only inside traits") {
	FSParser trait_parser;
	Error trait_err = trait_parser.parse(R"(
abstract trait Factory:
	static abstract func create() -> int
)",
			"user://static_abstract_trait.fs", false);

	CHECK_EQ(trait_err, OK);
	if (trait_err == OK) {
		const FSParser::ClassNode *trait_root = trait_parser.get_tree();
		const FSParser::ClassNode *factory = find_parser_trait(trait_root, SNAME("Factory"));
		CHECK(factory != nullptr);
		if (factory != nullptr) {
			const FSParser::FunctionNode *create = find_parser_function(factory, SNAME("create"));
			CHECK(create != nullptr);
			if (create != nullptr) {
				CHECK(create->is_static);
				CHECK(create->is_abstract);
			}
		}
	}

	check_parse_source_error(R"(
static abstract func create() -> int
)",
			R"(The "abstract" and "static" modifiers cannot be combined outside a trait.)");
}

TEST_CASE("[Modules][FoundryScript] Parser validates declaration modifier targets") {
	check_parse_source_error(R"(
abstract var value = 1
)",
			R"(The "abstract" modifier cannot be applied to variables.)");

	check_parse_source_error(R"(
abstract abstract func run() -> void
)",
			R"(The "abstract" modifier was already specified.)");

	check_parse_source_error(R"(
static signal triggered
)",
			R"(The "static" modifier cannot be applied to signals.)");
}

TEST_CASE("[Modules][FoundryScript] Parser accepts keyword abstract async functions") {
	// The `abstract` keyword must accept `abstract async func`: such a declaration
	// is an async contract that forces overriding implementations to be async.
	FSParser parser;
	Error err = parser.parse(R"(
abstract class AbstractAsync:
	abstract async func fetch() -> String
	async abstract func reload() -> String
)",
			"user://keyword_abstract_async.fs", false);

	CHECK_EQ(err, OK);
	if (err != OK) {
		return;
	}

	const FSParser::ClassNode *root = parser.get_tree();
	CHECK(root != nullptr);
	if (root == nullptr) {
		return;
	}
	const FSParser::ClassNode *abstract_async = find_parser_class(root, SNAME("AbstractAsync"));
	CHECK(abstract_async != nullptr);
	if (abstract_async == nullptr) {
		return;
	}

	const FSParser::FunctionNode *fetch = find_parser_function(abstract_async, SNAME("fetch"));
	CHECK(fetch != nullptr);
	if (fetch != nullptr) {
		CHECK(fetch->is_abstract);
		CHECK(fetch->is_declared_async);
		CHECK(fetch->is_coroutine);
		CHECK(!fetch->is_static);
	}

	// Modifier order must not matter (`async abstract` parses identically).
	const FSParser::FunctionNode *reload = find_parser_function(abstract_async, SNAME("reload"));
	CHECK(reload != nullptr);
	if (reload != nullptr) {
		CHECK(reload->is_abstract);
		CHECK(reload->is_declared_async);
		CHECK(reload->is_coroutine);
		CHECK(!reload->is_static);
	}
}

TEST_CASE("[Modules][FoundryScript] Parser keeps async usable as an identifier outside modifier positions") {
	FSParser parser;
	Error err = parser.parse(R"(
func async() -> int:
	var async = 1
	return async
)",
			"user://async_identifier.fs", false);

	CHECK_EQ(err, OK);
	if (err != OK) {
		return;
	}

	const FSParser::ClassNode *root = parser.get_tree();
	CHECK(root != nullptr);
	if (root == nullptr) {
		return;
	}
	const FSParser::FunctionNode *async_function = find_parser_function(root, SNAME("async"));
	CHECK(async_function != nullptr);
	if (async_function == nullptr) {
		return;
	}
	CHECK(!async_function->is_declared_async);
	CHECK(!async_function->is_coroutine);
}

TEST_CASE("[Modules][FoundryScript] Analyzer marks coroutine function metadata as async") {
	FSParser parser;
	Error err = parser.parse(R"(
async func explicit_async() -> int:
	return 1

func body_inferred_async() -> int:
	@warning_ignore("redundant_await")
	await 0
	return 2

func synchronous() -> int:
	return 3
)",
			"user://async_metadata.fs", false);

	CHECK_EQ(err, OK);
	if (err != OK) {
		return;
	}

	FSAnalyzer analyzer(&parser);
	err = analyzer.analyze();
	CHECK_EQ(err, OK);
	if (err != OK) {
		return;
	}

	const FSParser::ClassNode *root = parser.get_tree();
	CHECK(root != nullptr);
	if (root == nullptr) {
		return;
	}

	const FSParser::FunctionNode *explicit_async = find_parser_function(root, SNAME("explicit_async"));
	CHECK(explicit_async != nullptr);
	if (explicit_async == nullptr) {
		return;
	}
	CHECK((explicit_async->info.flags & METHOD_FLAG_ASYNC) != 0);

	const FSParser::FunctionNode *body_inferred_async = find_parser_function(root, SNAME("body_inferred_async"));
	CHECK(body_inferred_async != nullptr);
	if (body_inferred_async == nullptr) {
		return;
	}
	CHECK((body_inferred_async->info.flags & METHOD_FLAG_ASYNC) != 0);

	const FSParser::FunctionNode *synchronous = find_parser_function(root, SNAME("synchronous"));
	CHECK(synchronous != nullptr);
	if (synchronous == nullptr) {
		return;
	}
	CHECK_FALSE((synchronous->info.flags & METHOD_FLAG_ASYNC) != 0);
}

TEST_CASE("[Modules][FoundryScript] Compiled coroutine functions reflect async method flags") {
	ScopedFSNativeGlobals native_globals;
	FSParser parser;
	Error err = parser.parse(R"(
async func explicit_async() -> int:
	return 1

func body_inferred_async() -> int:
	@warning_ignore("redundant_await")
	await 0
	return 2

func synchronous() -> int:
	return 3
)",
			"user://async_reflection.fs", false);

	CHECK_EQ(err, OK);
	if (err != OK) {
		return;
	}

	FSAnalyzer analyzer(&parser);
	err = analyzer.analyze();
	CHECK_EQ(err, OK);
	if (err != OK) {
		return;
	}

	FSCompiler compiler;
	Ref<FoundryScript> script;
	script.instantiate();
	script->set_path("user://async_reflection.fs");

	err = compiler.compile(&parser, script.ptr(), false);
	INFO(compiler.get_error());
	CHECK_EQ(err, OK);
	if (err != OK) {
		return;
	}

	const MethodInfo explicit_async_info = script->get_method_info(SNAME("explicit_async"));
	CHECK_EQ(explicit_async_info.name, SNAME("explicit_async"));
	CHECK((explicit_async_info.flags & METHOD_FLAG_ASYNC) != 0);
	CHECK_EQ(explicit_async_info.return_val.type, Variant::INT);

	const MethodInfo body_inferred_async_info = script->get_method_info(SNAME("body_inferred_async"));
	CHECK_EQ(body_inferred_async_info.name, SNAME("body_inferred_async"));
	CHECK((body_inferred_async_info.flags & METHOD_FLAG_ASYNC) != 0);
	CHECK_EQ(body_inferred_async_info.return_val.type, Variant::INT);

	const MethodInfo synchronous_info = script->get_method_info(SNAME("synchronous"));
	CHECK_EQ(synchronous_info.name, SNAME("synchronous"));
	CHECK_FALSE((synchronous_info.flags & METHOD_FLAG_ASYNC) != 0);
	CHECK_EQ(synchronous_info.return_val.type, Variant::INT);

	Object *obj = ClassDB::instantiate(script->get_native()->get_name());
	CHECK(obj != nullptr);
	if (obj == nullptr) {
		return;
	}

	Ref<RefCounted> obj_ref;
	if (obj->is_ref_counted()) {
		obj_ref = Ref<RefCounted>(Object::cast_to<RefCounted>(obj));
	}
	obj->set_script(script);

	List<MethodInfo> reflected_methods;
	obj->get_method_list(&reflected_methods);

	const MethodInfo reflected_explicit_async_info = find_method_info(reflected_methods, SNAME("explicit_async"));
	CHECK_EQ(reflected_explicit_async_info.name, SNAME("explicit_async"));
	CHECK((reflected_explicit_async_info.flags & METHOD_FLAG_ASYNC) != 0);

	const MethodInfo reflected_body_inferred_async_info = find_method_info(reflected_methods, SNAME("body_inferred_async"));
	CHECK_EQ(reflected_body_inferred_async_info.name, SNAME("body_inferred_async"));
	CHECK((reflected_body_inferred_async_info.flags & METHOD_FLAG_ASYNC) != 0);

	const MethodInfo reflected_synchronous_info = find_method_info(reflected_methods, SNAME("synchronous"));
	CHECK_EQ(reflected_synchronous_info.name, SNAME("synchronous"));
	CHECK_FALSE((reflected_synchronous_info.flags & METHOD_FLAG_ASYNC) != 0);

	if (obj_ref.is_null()) {
		memdelete(obj);
	}
}

TEST_CASE("[Modules][FoundryScript] Compiled abstract functions reflect required method contracts") {
	ScopedFSNativeGlobals native_globals;
	FSParser parser;
	Error err = parser.parse(R"(
abstract class_name RequiredMethodReflection
extends RefCounted

abstract func required_contract(amount: int, label: String = "default") -> bool

abstract func untyped_required_contract()

func implemented() -> void:
	pass
)",
			"user://required_method_reflection.fs", false);

	CHECK_EQ(err, OK);
	if (err != OK) {
		return;
	}

	FSAnalyzer analyzer(&parser);
	err = analyzer.analyze();
	CHECK_EQ(err, OK);
	if (err != OK) {
		return;
	}

	FSCompiler compiler;
	Ref<FoundryScript> script;
	script.instantiate();
	script->set_path("user://required_method_reflection.fs");

	err = compiler.compile(&parser, script.ptr(), false);
	INFO(compiler.get_error());
	CHECK_EQ(err, OK);
	if (err != OK) {
		return;
	}

	const MethodInfo required_info = script->get_method_info(SNAME("required_contract"));
	CHECK_EQ(required_info.name, SNAME("required_contract"));
	CHECK((required_info.flags & METHOD_FLAG_VIRTUAL_REQUIRED) != 0);
	CHECK_EQ(required_info.arguments.size(), 2);
	CHECK_EQ(required_info.arguments[0].type, Variant::INT);
	CHECK_EQ(required_info.arguments[1].type, Variant::STRING);
	CHECK_EQ(required_info.default_arguments.size(), 1);
	CHECK_EQ(required_info.return_val.type, Variant::BOOL);

	List<MethodInfo> script_methods;
	script->get_script_method_list(&script_methods);
	const MethodInfo reflected_required_info = find_method_info(script_methods, SNAME("required_contract"));
	CHECK_EQ(reflected_required_info.name, SNAME("required_contract"));
	CHECK((reflected_required_info.flags & METHOD_FLAG_VIRTUAL_REQUIRED) != 0);

	const MethodInfo untyped_required_info = script->get_method_info(SNAME("untyped_required_contract"));
	CHECK_EQ(untyped_required_info.name, SNAME("untyped_required_contract"));
	CHECK((untyped_required_info.flags & METHOD_FLAG_VIRTUAL_REQUIRED) != 0);
	CHECK_EQ(untyped_required_info.return_val.type, Variant::NIL);

	const MethodInfo implemented_info = script->get_method_info(SNAME("implemented"));
	CHECK_EQ(implemented_info.name, SNAME("implemented"));
	CHECK_FALSE((implemented_info.flags & METHOD_FLAG_VIRTUAL_REQUIRED) != 0);
}

TEST_CASE("[Modules][FoundryScript] Scripts reflect trait identities") {
	Ref<FoundryScript> base;
	base.instantiate();

	Ref<FoundryScript> script;
	script.instantiate();

	SUBCASE("empty by default") {
		Vector<StringName> reflected_traits = get_script_trait_vector(script);
		CHECK(reflected_traits.is_empty());

		Variant bound_traits_variant = script->call(SNAME("get_script_trait_list"));
		CHECK_EQ(bound_traits_variant.get_type(), Variant::ARRAY);

		TypedArray<StringName> bound_traits = bound_traits_variant;
		CHECK(bound_traits.is_empty());
		CHECK_FALSE(script->has_script_trait(SNAME("Damageable")));
		CHECK_FALSE(bool(script->call(SNAME("has_script_trait"), SNAME("Damageable"))));
	}

	SUBCASE("merges base traits with duplicate suppression") {
		Vector<StringName> base_traits;
		base_traits.push_back(SNAME("Damageable"));
		base_traits.push_back(SNAME("Trackable"));
		TestFSTraitReflectionAccessor::set_script_trait_list(base, base_traits);

		Vector<StringName> script_traits;
		script_traits.push_back(SNAME("characters.Movable"));
		script_traits.push_back(SNAME("Damageable"));
		TestFSTraitReflectionAccessor::set_script_trait_list(script, script_traits);
		TestFSTraitReflectionAccessor::set_base(script, base);

		Vector<StringName> reflected_traits = get_script_trait_vector(script);
		CHECK_EQ(reflected_traits.size(), 3);
		CHECK_EQ(reflected_traits[0], SNAME("characters.Movable"));
		CHECK_EQ(reflected_traits[1], SNAME("Damageable"));
		CHECK_EQ(reflected_traits[2], SNAME("Trackable"));

		CHECK(script->has_script_trait(SNAME("characters.Movable")));
		CHECK(script->has_script_trait(SNAME("Damageable")));
		CHECK(script->has_script_trait(SNAME("Trackable")));
		CHECK_FALSE(script->has_script_trait(SNAME("MissingTrait")));

		Variant bound_traits_variant = script->call(SNAME("get_script_trait_list"));
		CHECK_EQ(bound_traits_variant.get_type(), Variant::ARRAY);

		TypedArray<StringName> bound_traits = bound_traits_variant;
		CHECK_EQ(bound_traits.size(), 3);
		const StringName first_bound_trait = bound_traits[0];
		const StringName second_bound_trait = bound_traits[1];
		const StringName third_bound_trait = bound_traits[2];
		CHECK_EQ(first_bound_trait, SNAME("characters.Movable"));
		CHECK_EQ(second_bound_trait, SNAME("Damageable"));
		CHECK_EQ(third_bound_trait, SNAME("Trackable"));

		CHECK(bool(script->call(SNAME("has_script_trait"), SNAME("Trackable"))));
		CHECK_FALSE(bool(script->call(SNAME("has_script_trait"), SNAME("MissingTrait"))));
	}
}

TEST_CASE("[Modules][FoundryScript] Scripts reflect declared generic type parameters") {
	ScopedFSNativeGlobals native_globals;

	SUBCASE("non-generic script reports no parameters") {
		Ref<FoundryScript> script;
		script.instantiate();

		CHECK_FALSE(script->is_generic());
		CHECK(script->get_type_parameters().is_empty());

		Variant bound_variant = script->call(SNAME("get_type_parameter_list"));
		CHECK_EQ(bound_variant.get_type(), Variant::ARRAY);
		TypedArray<FSTypeParameter> bound_parameters = bound_variant;
		CHECK(bound_parameters.is_empty());
		CHECK_FALSE(bool(script->call(SNAME("is_generic"))));
	}

	SUBCASE("reflection accessor exposes parameter descriptors") {
		Ref<FoundryScript> script;
		script.instantiate();

		Vector<FoundryScript::TypeParameter> parameters;

		FoundryScript::TypeParameter key;
		key.name = SNAME("K");
		key.index = 0;
		parameters.push_back(key);

		FoundryScript::TypeParameter value;
		value.name = SNAME("V");
		value.index = 1;
		value.has_bound = true;
		value.bound = PropertyInfo(Variant::OBJECT, "", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_NONE, "RefCounted");
		parameters.push_back(value);

		TestFSGenericReflectionAccessor::set_type_parameters(script, parameters);

		CHECK(script->is_generic());
		CHECK_EQ(script->get_type_parameters().size(), 2);

		Variant bound_variant = script->call(SNAME("get_type_parameter_list"));
		CHECK_EQ(bound_variant.get_type(), Variant::ARRAY);
		TypedArray<FSTypeParameter> bound_parameters = bound_variant;
		CHECK_EQ(bound_parameters.size(), 2);
		if (bound_parameters.size() != 2) {
			return;
		}

		Ref<FSTypeParameter> key_entry = bound_parameters[0];
		CHECK(key_entry.is_valid());
		Ref<FSTypeParameter> value_entry = bound_parameters[1];
		CHECK(value_entry.is_valid());
		if (key_entry.is_null() || value_entry.is_null()) {
			return;
		}

		CHECK_EQ(key_entry->get_parameter_name(), SNAME("K"));
		CHECK_EQ(key_entry->get_index(), 0);
		CHECK_EQ(key_entry->get_scope(), SNAME("class"));
		CHECK_FALSE(key_entry->is_bounded());
		CHECK(key_entry->get_bound().is_empty());

		CHECK_EQ(value_entry->get_parameter_name(), SNAME("V"));
		CHECK_EQ(value_entry->get_index(), 1);
		CHECK(value_entry->is_bounded());
		const Dictionary bound_info = value_entry->get_bound();
		CHECK_EQ(int(bound_info["type"]), int(Variant::OBJECT));
		CHECK_EQ(StringName(bound_info["class_name"]), SNAME("RefCounted"));

		// The descriptors are also accessible through the bound property surface.
		CHECK_EQ(StringName(key_entry->get(SNAME("name"))), SNAME("K"));
		CHECK_EQ(int(value_entry->get(SNAME("index"))), 1);
		CHECK(bool(value_entry->get(SNAME("has_bound"))));
	}

	SUBCASE("compiled generic classes carry their parameters") {
		FSParser parser;
		Error err = parser.parse(R"(
class_name GenericReflectionRoot[T]
extends RefCounted

class Pair[K, V: RefCounted]:
	var first
	var second
)",
				"user://generic_reflection.fs", false);
		CHECK_EQ(err, OK);
		if (err != OK) {
			return;
		}

		FSAnalyzer analyzer(&parser);
		err = analyzer.analyze();
		CHECK_EQ(err, OK);
		if (err != OK) {
			return;
		}

		FSCompiler compiler;
		Ref<FoundryScript> script;
		script.instantiate();
		script->set_path("user://generic_reflection.fs");
		err = compiler.compile(&parser, script.ptr(), false);
		INFO(compiler.get_error());
		CHECK_EQ(err, OK);
		if (err != OK) {
			return;
		}

		CHECK(script->is_generic());
		const Vector<FoundryScript::TypeParameter> &root_parameters = script->get_type_parameters();
		CHECK_EQ(root_parameters.size(), 1);
		if (root_parameters.size() == 1) {
			CHECK_EQ(root_parameters[0].name, SNAME("T"));
			CHECK_EQ(root_parameters[0].index, 0);
			CHECK_FALSE(root_parameters[0].has_bound);
		}

		const Ref<FoundryScript> *pair_ptr = script->get_subclasses().getptr(SNAME("Pair"));
		CHECK(pair_ptr != nullptr);
		if (pair_ptr == nullptr) {
			return;
		}
		Ref<FoundryScript> pair = *pair_ptr;
		CHECK(pair.is_valid());
		if (pair.is_null()) {
			return;
		}
		const Vector<FoundryScript::TypeParameter> &pair_parameters = pair->get_type_parameters();
		CHECK_EQ(pair_parameters.size(), 2);
		if (pair_parameters.size() != 2) {
			return;
		}
		CHECK_EQ(pair_parameters[0].name, SNAME("K"));
		CHECK_EQ(pair_parameters[0].index, 0);
		CHECK_FALSE(pair_parameters[0].has_bound);
		CHECK_EQ(pair_parameters[1].name, SNAME("V"));
		CHECK_EQ(pair_parameters[1].index, 1);
		CHECK(pair_parameters[1].has_bound);
		CHECK_EQ(pair_parameters[1].bound.type, Variant::OBJECT);
		CHECK_EQ(pair_parameters[1].bound.class_name, SNAME("RefCounted"));
	}
}

TEST_CASE("[Modules][FoundryScript] Parser rejects invalid namespace and import declarations") {
	check_parse_source_error(R"(
namespace first
namespace second
class_name DuplicateNamespace
)",
			R"("namespace" can only be used once.)");

	check_parse_source_error(R"(
namespace characters.
class_name MalformedNamespace
)",
			R"(Expected identifier after "." in namespace declaration.)");

	check_parse_source_error(R"(
import .characters
class_name MalformedImport
)",
			R"(Expected identifier after "import".)");

	check_parse_source_error(R"(
import characters
namespace characters.controllers
class_name NamespaceAfterImport
)",
			R"("namespace" must be declared before "import".)");

	check_parse_source_error(R"(
@warning_ignore("redundant_await")
namespace characters.controllers
class_name ClassAnnotationBeforeNamespace
)",
			R"(Class annotations must appear after "namespace" and "import" declarations.)");

	{
		PackedStringArray errors = parse_source_errors(R"(
@warning_ignore("redundant_await")
namespace characters.controllers import characters
class_name ClassAnnotationBeforeMalformedNamespace
)");
		CHECK_EQ(errors.size(), 2);
		if (errors.size() == 2) {
			CHECK_EQ(errors[0], R"(Class annotations must appear after "namespace" and "import" declarations.)");
			CHECK_EQ(errors[1], R"(Expected end of statement after namespace declaration, found "import" instead.)");
		}
	}

	check_parse_source_error(R"(
namespace characters.controllers
@tool
class_name ScriptAnnotationAfterNamespace
)",
			R"(Annotation "@tool" must be at the top of the script, before "extends" and "class_name".)");

	check_parse_source_error(R"(
class_name ImportAfterClassName
import characters
)",
			R"("import" declarations must appear before "class_name", "extends", and body declarations.)");
}

TEST_CASE("[Modules][FoundryScript] Parser rejects invalid trait declaration ordering") {
	check_parse_source_error(R"(
class_name Actor
trait_name Damageable
)",
			R"("trait_name" cannot be combined with "class_name" in the same file.)");

	check_parse_source_error(R"(
trait_name Damageable
class_name Actor
)",
			R"("class_name" cannot be combined with "trait_name" in the same file.)");

	check_parse_source_error(R"(
func body() -> void:
	pass
uses Damageable
)",
			R"("uses" declarations must appear before class body declarations.)");

	check_parse_source_error(R"(
class Broken:
	func body() -> void:
		pass
	uses Damageable
)",
			R"("uses" declarations must appear before class body declarations.)");

	check_parse_source_error(R"(
class_name Actor
uses Damageable
uses Trackable
)",
			R"(Cannot use "uses" more than once in the same class.)");

	check_parse_source_error(R"(
class Broken:
	uses Damageable
	uses Trackable
)",
			R"(Cannot use "uses" more than once in the same class.)");

	check_parse_source_error(R"(
class Broken:
	uses Damageable
	extends Node
)",
			R"("extends" must appear before "uses".)");
}

TEST_CASE("[Modules][FoundryScript] Global class names use namespace-qualified identity") {
	TempScriptFile script("qualified_global_name.fs", R"(
namespace characters
class_name BaseCharacter
extends Node
)");

	String base_type;
	bool is_abstract = true;
	bool is_tool = true;
	bool is_enum = true;
	String class_name = FSLanguage::get_singleton()->get_global_class_name(script.path, &base_type, nullptr,
			&is_abstract, &is_tool, nullptr, &is_enum);

	CHECK_EQ(class_name, "characters.BaseCharacter");
	CHECK_EQ(base_type, "Node");
	CHECK_FALSE(is_abstract);
	CHECK_FALSE(is_tool);
	CHECK_FALSE(is_enum);
}

TEST_CASE("[Modules][FoundryScript] Global namespace class names stay unqualified") {
	TempScriptFile script("unqualified_global_name.fs", R"(
class_name PlainCharacter
extends Node
)");

	String class_name = FSLanguage::get_singleton()->get_global_class_name(script.path);

	CHECK_EQ(class_name, "PlainCharacter");
}

TEST_CASE("[Modules][FoundryScript] Global trait names use namespace-qualified identity") {
	TempScriptFile script("global_trait_name.fs", R"(
namespace characters
trait_name Damageable
extends Node2D
)");

	String base_type;
	bool is_abstract = true;
	bool is_tool = true;
	bool is_enum = true;
	String class_name = FSLanguage::get_singleton()->get_global_class_name(script.path, &base_type, nullptr,
			&is_abstract, &is_tool, nullptr, &is_enum);

	CHECK_EQ(class_name, "characters.Damageable");
	CHECK_EQ(base_type, "Node2D");
	CHECK_FALSE(is_abstract);
	CHECK_FALSE(is_tool);
	CHECK_FALSE(is_enum);
}

TEST_CASE("[Modules][FoundryScript] Global enum files report enum metadata") {
	TempScriptFile script("qualified_global_enum.fs", R"(
namespace items
enum_name WeaponType:
	SWORD = 0
	BOW = 1
)");

	String base_type = "sentinel";
	bool is_abstract = true;
	bool is_tool = true;
	bool is_trait = true;
	bool is_enum = false;
	String enum_name = FSLanguage::get_singleton()->get_global_class_name(script.path, &base_type, nullptr,
			&is_abstract, &is_tool, &is_trait, &is_enum);

	CHECK_EQ(enum_name, "items.WeaponType");
	CHECK(base_type.is_empty());
	CHECK_FALSE(is_abstract);
	CHECK_FALSE(is_tool);
	CHECK_FALSE(is_trait);
	CHECK(is_enum);
}

TEST_CASE("[Modules][FoundryScript] Global enum files register with the enum flag") {
	GlobalScriptClassCacheBackup backup;
	ScriptServer::global_classes_clear();

	TempScriptFile script("register_global_enum.fs", R"(
namespace items
enum_name ElementKind:
	FIRE = 0
	ICE = 1
)");

	String base_type;
	bool is_abstract = false;
	bool is_tool = false;
	bool is_trait = false;
	bool is_enum = false;
	String enum_name = FSLanguage::get_singleton()->get_global_class_name(script.path, &base_type, nullptr,
			&is_abstract, &is_tool, &is_trait, &is_enum);
	CHECK_EQ(enum_name, "items.ElementKind");
	if (enum_name.is_empty()) {
		return;
	}

	ScriptServer::add_global_class(enum_name, base_type, FSLanguage::get_singleton()->get_name(), script.path,
			is_abstract, is_tool, is_trait, is_enum);

	CHECK(ScriptServer::is_global_class_enum("items.ElementKind"));
	CHECK_FALSE(ScriptServer::is_global_class_trait("items.ElementKind"));
}

TEST_CASE("[Modules][FoundryScript] Global enum fixture files round-trip the enum flag through the cache") {
	GlobalScriptClassCacheBackup backup;
	ScriptServer::global_classes_clear();
	ProjectSettings::get_singleton()->store_global_class_list(Array());

	const String fixture_path = "modules/foundry_script/tests/scripts/registration/global_enum_cache.notest.fs";

	String base_type = "sentinel";
	bool is_abstract = true;
	bool is_tool = true;
	bool is_trait = true;
	bool is_enum = false;
	String enum_name = FSLanguage::get_singleton()->get_global_class_name(fixture_path, &base_type, nullptr,
			&is_abstract, &is_tool, &is_trait, &is_enum);

	CHECK_EQ(enum_name, "tests.registration.FixtureGlobalEnum");
	CHECK(base_type.is_empty());
	CHECK_FALSE(is_abstract);
	CHECK_FALSE(is_tool);
	CHECK_FALSE(is_trait);
	CHECK(is_enum);
	if (enum_name.is_empty()) {
		return;
	}

	ScriptServer::add_global_class(enum_name, base_type, FSLanguage::get_singleton()->get_name(), fixture_path,
			is_abstract, is_tool, is_trait, is_enum);

	CHECK(ScriptServer::is_global_class_enum("tests.registration.FixtureGlobalEnum"));
	CHECK_FALSE(ScriptServer::is_global_class_trait("tests.registration.FixtureGlobalEnum"));
	ScriptServer::save_global_classes();

	TypedArray<Dictionary> script_classes = ProjectSettings::get_singleton()->get_global_class_list();
	CHECK_EQ(script_classes.size(), 1);
	if (script_classes.size() == 1) {
		Dictionary script_class = script_classes[0];
		CHECK_EQ(String(script_class["class"]), "tests.registration.FixtureGlobalEnum");
		CHECK_EQ(String(script_class["path"]), fixture_path);
		CHECK(script_class.has("is_enum"));
		CHECK(bool(script_class["is_enum"]));
		CHECK(script_class.has("is_trait"));
		CHECK_FALSE(bool(script_class["is_trait"]));
	}

	ScriptServer::global_classes_clear();
	ProjectSettings::get_singleton()->refresh_global_class_list();
	CHECK(ScriptServer::is_global_class("tests.registration.FixtureGlobalEnum"));
	CHECK(ScriptServer::is_global_class_enum("tests.registration.FixtureGlobalEnum"));
	CHECK_FALSE(ScriptServer::is_global_class_trait("tests.registration.FixtureGlobalEnum"));
	CHECK_EQ(ScriptServer::get_global_class_path("tests.registration.FixtureGlobalEnum"), fixture_path);
	CHECK(ScriptServer::get_global_class_base("tests.registration.FixtureGlobalEnum").is_empty());
}

TEST_CASE("[Modules][FoundryScript] Loaded namespaced global class keeps qualified runtime identity") {
	GlobalScriptClassCacheBackup backup;
	ScriptServer::global_classes_clear();

	FSParser parser;
	Error err = parser.parse(R"(
namespace characters
class_name RuntimeCharacter
)",
			"user://qualified_runtime_global_name.fs", false);
	CHECK_EQ(err, OK);

	Ref<FoundryScript> compiled;
	compiled.instantiate();
	FSCompiler::make_scripts(compiled.ptr(), parser.get_tree(), false);
	ScriptServer::add_global_class("characters.RuntimeCharacter", "RefCounted",
			FSLanguage::get_singleton()->get_name(), "user://qualified_runtime_global_name.fs", false, false, false);

	CHECK_EQ(compiled->get_global_name(), "characters.RuntimeCharacter");
	CHECK(ScriptServer::is_global_class(compiled->get_global_name()));
}

TEST_CASE("[Modules][FoundryScript] Namespaced global class is referenceable as a value") {
	ScopedFSNativeGlobals native_globals;
	GlobalScriptClassCacheBackup backup;
	ScriptServer::global_classes_clear();

	// The producer lives in `tests.nsvalue` and exposes a static factory plus an
	// instance method, so consumers can exercise both static calls and `.new()`.
	const String producer_path = "user://ns_value_producer.fs";
	{
		Ref<FileAccess> file = FileAccess::open(producer_path, FileAccess::WRITE);
		CHECK(file.is_valid());
		if (file.is_valid()) {
			file->store_string(R"(namespace tests.nsvalue
class_name NsValueProducer
extends RefCounted

static func make() -> int:
	return 42

func doubled(value: int) -> int:
	return value * 2
)");
		}
	}
	ScriptServer::add_global_class("tests.nsvalue.NsValueProducer", "RefCounted",
			FSLanguage::get_singleton()->get_name(), producer_path, false, false, false);

	// Compile the consumer through analyze + compile, which is where the namespaced
	// value reference is emitted. Without the fix, codegen cannot map the dotted
	// global class name and `compile()` fails; the resulting Error is the regression
	// signal.
	auto compile_consumer = [](const String &p_source, const String &p_path) -> Error {
		// Write the consumer to disk so a self-reference can load its own script path.
		{
			Ref<FileAccess> file = FileAccess::open(p_path, FileAccess::WRITE);
			CHECK(file.is_valid());
			if (file.is_valid()) {
				file->store_string(p_source);
			}
		}
		FSParser parser;
		Error parse_err = parser.parse(p_source, p_path, false);
		CHECK_EQ(parse_err, OK);
		FSAnalyzer analyzer(&parser);
		Error analyze_err = analyzer.analyze();
		CHECK_EQ(analyze_err, OK);
		Ref<FoundryScript> script;
		script.instantiate();
		script->set_path(p_path);
		FSCompiler compiler;
		Error compile_err = OK;
		if (analyze_err == OK) {
			compile_err = compiler.compile(&parser, script.ptr(), false);
			if (compile_err != OK) {
				MESSAGE("compile error: ", compiler.get_error());
			}
		} else {
			compile_err = analyze_err;
		}
		// Release the compiled consumer and drop its cached parser/script so no
		// FoundryScript outlives the test. A self-reference compiles a constant Ref to
		// the script itself, so clear() is needed to break that cycle before the Ref is
		// dropped; otherwise the script stays in the global script list and trips a
		// DEV_ASSERT in its destructor at shutdown.
		script->clear();
		script.unref();
		FSCache::remove_parser(p_path);
		FSCache::remove_script(p_path);
		DirAccess::remove_absolute(p_path);
		return compile_err;
	};

	SUBCASE("short name static call from the same namespace") {
		CHECK_EQ(compile_consumer(R"(namespace tests.nsvalue
extends RefCounted

static func run() -> int:
	return NsValueProducer.make()
)",
						 "user://ns_value_consumer_short_static.fs"),
				OK);
	}

	SUBCASE("fully qualified static call") {
		CHECK_EQ(compile_consumer(R"(namespace some.other
extends RefCounted

static func run() -> int:
	return tests.nsvalue.NsValueProducer.make()
)",
						 "user://ns_value_consumer_qualified_static.fs"),
				OK);
	}

	SUBCASE("short name construction from the same namespace") {
		CHECK_EQ(compile_consumer(R"(namespace tests.nsvalue
extends RefCounted

static func run() -> RefCounted:
	return NsValueProducer.new()
)",
						 "user://ns_value_consumer_short_new.fs"),
				OK);
	}

	SUBCASE("imported short name construction") {
		CHECK_EQ(compile_consumer(R"(namespace some.other
import tests.nsvalue
extends RefCounted

static func run() -> RefCounted:
	return NsValueProducer.new()
)",
						 "user://ns_value_consumer_import_new.fs"),
				OK);
	}

	SUBCASE("self reference by its own namespaced class name") {
		CHECK_EQ(compile_consumer(R"(namespace tests.nsvalue
class_name NsValueSelf
extends RefCounted

static func build() -> RefCounted:
	return NsValueSelf.new()
)",
						 "user://ns_value_consumer_self.fs"),
				OK);
	}

	FSCache::remove_parser(producer_path);
	FSCache::remove_script(producer_path);
	DirAccess::remove_absolute(producer_path);
}

TEST_CASE("[Modules][FoundryScript] Namespaced global class property metadata uses qualified names") {
	FSParser parser;
	Error err = parser.parse(R"(
namespace characters
class_name PropertyTarget

var direct: PropertyTarget
var list: Array[PropertyTarget]
var map: Dictionary[String, PropertyTarget]
)",
			"user://qualified_property_metadata.fs", false);
	CHECK_EQ(err, OK);

	FSAnalyzer analyzer(&parser);
	err = analyzer.analyze();

	CHECK_EQ(err, OK);
	const FSParser::ClassNode *root = parser.get_tree();
	CHECK(root != nullptr);
	if (err != OK || root == nullptr || root->members.size() < 3) {
		return;
	}

	CHECK_EQ(root->get_global_name(), "characters.PropertyTarget");

	const PropertyInfo direct = root->members[0].variable->get_datatype().to_property_info("direct");
	CHECK_EQ(direct.type, Variant::OBJECT);
	CHECK_EQ(direct.class_name, "characters.PropertyTarget");

	const PropertyInfo list = root->members[1].variable->get_datatype().to_property_info("list");
	CHECK_EQ(list.type, Variant::ARRAY);
	CHECK_EQ(list.hint, PROPERTY_HINT_ARRAY_TYPE);
	CHECK_EQ(list.hint_string, "characters.PropertyTarget");

	const PropertyInfo map = root->members[2].variable->get_datatype().to_property_info("map");
	CHECK_EQ(map.type, Variant::DICTIONARY);
	CHECK_EQ(map.hint, PROPERTY_HINT_DICTIONARY_TYPE);
	CHECK_EQ(map.hint_string, "String;characters.PropertyTarget");
}

#ifdef TOOLS_ENABLED
TEST_CASE("[Modules][FoundryScript] Docgen emits qualified names for namespaced global class types") {
	FSParser parser;
	Error err = parser.parse(R"(
namespace characters
class_name DocTarget

var direct: DocTarget
var list: Array[DocTarget]
var map: Dictionary[String, DocTarget]
)",
			"user://qualified_docgen_types.fs", false);
	CHECK_EQ(err, OK);

	FSAnalyzer analyzer(&parser);
	err = analyzer.analyze();

	CHECK_EQ(err, OK);
	const FSParser::ClassNode *root = parser.get_tree();
	CHECK(root != nullptr);
	if (err != OK || root == nullptr) {
		return;
	}

	Ref<FoundryScript> script;
	script.instantiate();
	FSCompiler::make_scripts(script.ptr(), root, false);
	FSDocGen::generate_docs(script.ptr(), root);

	const Vector<DocData::ClassDoc> docs = script->get_documentation();
	CHECK_EQ(docs.size(), 1);
	if (docs.size() != 1 || docs[0].properties.size() < 3) {
		return;
	}

	CHECK_EQ(docs[0].properties[0].type, "characters.DocTarget");
	CHECK_EQ(docs[0].properties[1].type, "characters.DocTarget[]");
	CHECK_EQ(docs[0].properties[2].type, "Dictionary[String, characters.DocTarget]");
}

TEST_CASE("[Modules][FoundryScript] Cleared script refuses to instantiate") {
	// Regression test: `clear()` deletes every compiled function (this is what
	// `FSLanguage::finish()` does to all live scripts), but a stale Ref or ResourceCache entry
	// can keep the cleared script alive and hand it out again on a later cache-hit load.
	// Such a script must report itself as non-instantiable and fail construction loudly instead
	// of producing a half-constructed instance whose member defaults never ran.
	if (!FSLanguage::get_singleton()->has_any_global_constant(SNAME("RefCounted"))) {
		FSLanguage::get_singleton()->init();
	}

	const String source =
			"var health: int = 10\n"
			"\n"
			"func doubled() -> int:\n"
			"\treturn health * 2\n";

	Ref<FoundryScript> script;
	script.instantiate();
	script->set_path("user://cleared_script_target.fs");
	script->set_source_code(source);
	const Error err = script->reload();
	CHECK_EQ(err, OK);
	if (err != OK) {
		return;
	}
	CHECK(script->can_instantiate());

	// Sanity: the freshly compiled script instantiates and runs.
	Callable::CallError call_error;
	call_error.error = Callable::CallError::CALL_OK;
	Variant instance = script->_new(nullptr, 0, call_error);
	CHECK_EQ(call_error.error, Callable::CallError::CALL_OK);
	CHECK_EQ(instance.get_type(), Variant::OBJECT);
	instance = Variant();

	script->clear();

	CHECK_FALSE(script->is_valid());
	CHECK_FALSE(script->can_instantiate());

	// `new()` must fail cleanly with a call error, not return a broken instance.
	call_error.error = Callable::CallError::CALL_OK;
	const Variant cleared_instance = script->_new(nullptr, 0, call_error);
	CHECK_EQ(call_error.error, Callable::CallError::CALL_ERROR_INVALID_METHOD);
	CHECK_EQ(cleared_instance.get_type(), Variant::NIL);

	// Attaching the cleared script to an object must not create a script instance.
	Object *object = memnew(Object);
	ERR_PRINT_OFF;
	object->set_script(script);
	ERR_PRINT_ON;
	CHECK(object->get_script_instance() == nullptr);
	memdelete(object);
}

TEST_CASE("[Modules][FoundryScript] Docs are generated lazily on request after reload()") {
	// Regression test for lazy documentation generation. reload() no longer generates docs
	// eagerly (doc generation was ~22% of cold script load time and is only needed by editor
	// surfaces, never to load/instantiate a scene). get_documentation() must still return the
	// correct docs by generating them lazily on first request.
	if (!FSLanguage::get_singleton()->has_any_global_constant(SNAME("RefCounted"))) {
		FSLanguage::get_singleton()->init();
	}

	const String source =
			"## The current health.\n"
			"var health: int = 10\n"
			"\n"
			"## Returns the doubled health.\n"
			"func doubled() -> int:\n"
			"\treturn health * 2\n";

	Ref<FoundryScript> script;
	script.instantiate();
	script->set_path("user://lazy_doc_target.fs");
	script->set_source_code(source);

	const Error err = script->reload();
	CHECK_EQ(err, OK);
	if (err != OK) {
		return;
	}

	// Documentation is not populated by reload() itself; it is produced on demand here.
	const Vector<DocData::ClassDoc> docs = script->get_documentation();
	CHECK(docs.size() >= 1);
	if (docs.size() < 1) {
		return;
	}

	const DocData::ClassDoc &cd = docs[0];

	bool found_property = false;
	for (const DocData::PropertyDoc &p : cd.properties) {
		if (p.name == "health") {
			found_property = true;
			CHECK(p.description.strip_edges() == "The current health.");
		}
	}
	CHECK(found_property);

	bool found_method = false;
	for (const DocData::MethodDoc &m : cd.methods) {
		if (m.name == "doubled") {
			found_method = true;
			CHECK(m.description.strip_edges() == "Returns the doubled health.");
		}
	}
	CHECK(found_method);
}

TEST_CASE("[Modules][FoundryScript] Docgen emits enum_name files as enum class docs") {
	FSParser parser;
	Error err = parser.parse(R"(
namespace items

## Weapon type summary.
enum_name WeaponType:
	## Uses a blade.
	SWORD = 3
	## Uses a bow.
	BOW = 4

	## Formats a weapon type.
	func label(prefix: String = "") -> String:
		return prefix

	## Loads a weapon type asynchronously.
	static async func load(text: String, prefix: String = ">") -> String:
		return prefix + text
)",
			"user://weapon_type_docgen.fs", false);
	CHECK_EQ(err, OK);

	FSAnalyzer analyzer(&parser);
	err = analyzer.analyze();

	CHECK_EQ(err, OK);
	const FSParser::ClassNode *root = parser.get_tree();
	CHECK(root != nullptr);
	if (err != OK || root == nullptr) {
		return;
	}

	Ref<FoundryScript> script;
	script.instantiate();
	FSCompiler::make_scripts(script.ptr(), root, false);
	FSDocGen::generate_docs(script.ptr(), root);

	const Vector<DocData::ClassDoc> docs = script->get_documentation();
	CHECK_EQ(docs.size(), 1);
	if (docs.size() != 1) {
		return;
	}

	Dictionary doc_dict = DocData::ClassDoc::to_dict(docs[0]);
	CHECK(doc_dict.has("is_enum"));
	if (doc_dict.has("is_enum")) {
		CHECK(bool(doc_dict["is_enum"]));
	}

	DocData::ClassDoc restored = DocData::ClassDoc::from_dict(doc_dict);
	Dictionary restored_dict = DocData::ClassDoc::to_dict(restored);
	CHECK(restored_dict.has("is_enum"));
	if (restored_dict.has("is_enum")) {
		CHECK(bool(restored_dict["is_enum"]));
	}

	CHECK_EQ(docs[0].name, "items.WeaponType");
	CHECK(docs[0].inherits.is_empty());
	CHECK_EQ(docs[0].enums.size(), 1);
	CHECK(docs[0].enums.has("WeaponType"));
	if (docs[0].enums.has("WeaponType")) {
		CHECK_EQ(docs[0].enums["WeaponType"].description, "Weapon type summary.");
	}

	const DocData::ConstantDoc *sword = nullptr;
	const DocData::ConstantDoc *bow = nullptr;
	for (const DocData::ConstantDoc &constant : docs[0].constants) {
		if (constant.name == "SWORD") {
			sword = &constant;
		} else if (constant.name == "BOW") {
			bow = &constant;
		}
	}

	CHECK_EQ(docs[0].constants.size(), 2);
	CHECK(sword != nullptr);
	CHECK(bow != nullptr);
	if (sword == nullptr || bow == nullptr) {
		return;
	}

	CHECK_EQ(sword->type, "int");
	CHECK_EQ(sword->enumeration, "WeaponType");
	CHECK_EQ(sword->description, "Uses a blade.");

	CHECK_EQ(bow->type, "int");
	CHECK_EQ(bow->enumeration, "WeaponType");
	CHECK_EQ(bow->description, "Uses a bow.");

	const DocData::MethodDoc *label = nullptr;
	const DocData::MethodDoc *load = nullptr;
	for (const DocData::MethodDoc &method : docs[0].methods) {
		if (method.name == "label") {
			label = &method;
		} else if (method.name == "load") {
			load = &method;
		}
	}

	CHECK_EQ(docs[0].methods.size(), 2);
	CHECK(label != nullptr);
	CHECK(load != nullptr);
	if (label == nullptr || load == nullptr) {
		return;
	}

	CHECK(label->qualifiers.is_empty());
	CHECK_EQ(label->return_type, "String");
	CHECK_EQ(label->description, "Formats a weapon type.");
	CHECK_EQ(label->arguments.size(), 1);
	if (label->arguments.size() != 1) {
		return;
	}
	CHECK_EQ(label->arguments[0].name, "prefix");
	CHECK_EQ(label->arguments[0].type, "String");
	CHECK_EQ(label->arguments[0].default_value, R"("")");

	CHECK_EQ(load->qualifiers, "static async");
	CHECK_EQ(load->return_type, "String");
	CHECK_EQ(load->description, "Loads a weapon type asynchronously.");
	CHECK_EQ(load->arguments.size(), 2);
	if (load->arguments.size() != 2) {
		return;
	}
	CHECK_EQ(load->arguments[0].name, "text");
	CHECK_EQ(load->arguments[0].type, "String");
	CHECK_EQ(load->arguments[1].name, "prefix");
	CHECK_EQ(load->arguments[1].type, "String");
	CHECK_EQ(load->arguments[1].default_value, R"(">")");
}

TEST_CASE("[Modules][FoundryScript] Docgen does not flatten nested enum functions into class methods") {
	FSParser parser;
	Error err = parser.parse(R"(
enum Status:
	READY = 1

	## Formats a status.
	func label() -> String:
		return "ready"
)",
			"user://nested_enum_method_docgen.fs", false);
	CHECK_EQ(err, OK);

	FSAnalyzer analyzer(&parser);
	err = analyzer.analyze();
	CHECK_EQ(err, OK);

	const FSParser::ClassNode *root = parser.get_tree();
	CHECK(root != nullptr);
	if (err != OK || root == nullptr) {
		return;
	}

	Ref<FoundryScript> script;
	script.instantiate();
	FSCompiler::make_scripts(script.ptr(), root, false);
	FSDocGen::generate_docs(script.ptr(), root);

	const Vector<DocData::ClassDoc> docs = script->get_documentation();
	CHECK_EQ(docs.size(), 1);
	if (docs.size() != 1) {
		return;
	}
	CHECK(docs[0].enums.has("Status"));
	CHECK(docs[0].methods.is_empty());
}

TEST_CASE("[Modules][FoundryScript] Docgen names UID-backed autoload scripts from the autoload index") {
	ScopedFSNativeGlobals native_globals;
	TempScriptFile autoload_script("docgen_uid_autoload.fs", "extends Node\nvar count: int\n");
	ScopedResourceUIDRegistration uid(autoload_script.path);
	ScopedFSAutoloadSetting autoload(SNAME("DocgenUidAutoload"), uid.get_uid_path());

	FSParser parser;
	Error err = parser.parse("extends Node\nvar count: int\n", autoload_script.path, false);
	CHECK_EQ(err, OK);

	FSAnalyzer analyzer(&parser);
	err = analyzer.analyze();
	CHECK_EQ(err, OK);

	const FSParser::ClassNode *root = parser.get_tree();
	CHECK(root != nullptr);
	if (err != OK || root == nullptr) {
		return;
	}

	Ref<FoundryScript> script;
	script.instantiate();
	script->set_path(autoload_script.path);
	FSCompiler::make_scripts(script.ptr(), root, false);
	FSDocGen::generate_docs(script.ptr(), root);

	const Vector<DocData::ClassDoc> docs = script->get_documentation();
	CHECK_EQ(docs.size(), 1);
	if (docs.size() != 1) {
		return;
	}

	CHECK_EQ(docs[0].name, "DocgenUidAutoload");
	CHECK_EQ(docs[0].script_path, autoload_script.path);
}

TEST_CASE("[Modules][FoundryScript] Docgen emits custom annotation declarations") {
	FSParser parser;
	Error err = parser.parse(R"(
namespace cafecito

## Marks the script's entry point.
annotation entry targets CLASS, METHOD

## Sets a timeout in seconds.
annotation timeout(seconds: float = 5.0) targets METHOD

annotation tags(...names: String) targets CLASS, METHOD
)",
			"user://annotation_docgen.fs", false);
	CHECK_EQ(err, OK);

	FSAnalyzer analyzer(&parser);
	err = analyzer.analyze();

	CHECK_EQ(err, OK);
	const FSParser::ClassNode *root = parser.get_tree();
	CHECK(root != nullptr);
	if (err != OK || root == nullptr) {
		return;
	}

	Ref<FoundryScript> script;
	script.instantiate();
	FSCompiler::make_scripts(script.ptr(), root, false);
	FSDocGen::generate_docs(script.ptr(), root);

	const Vector<DocData::ClassDoc> docs = script->get_documentation();
	CHECK_EQ(docs.size(), 1);
	if (docs.size() != 1) {
		return;
	}

	// An annotation's doc comment must not leak into the script/class description, even when the
	// script declares only annotations and no members.
	CHECK(docs[0].brief_description.is_empty());
	CHECK(docs[0].description.is_empty());

	const DocData::MethodDoc *entry = nullptr;
	const DocData::MethodDoc *timeout = nullptr;
	const DocData::MethodDoc *tags = nullptr;
	for (const DocData::MethodDoc &annotation : docs[0].annotations) {
		if (annotation.name == "@entry") {
			entry = &annotation;
		} else if (annotation.name == "@timeout") {
			timeout = &annotation;
		} else if (annotation.name == "@tags") {
			tags = &annotation;
		}
	}

	CHECK_EQ(docs[0].annotations.size(), 3);

	CHECK(entry != nullptr);
	CHECK(timeout != nullptr);
	CHECK(tags != nullptr);
	if (entry == nullptr || timeout == nullptr || tags == nullptr) {
		return;
	}

	CHECK_EQ(entry->qualifiers, "class method");
	CHECK_EQ(entry->description, "Marks the script's entry point.");
	CHECK(entry->arguments.is_empty());

	CHECK_EQ(timeout->qualifiers, "method");
	CHECK_EQ(timeout->description, "Sets a timeout in seconds.");
	CHECK_EQ(timeout->arguments.size(), 1);
	if (timeout->arguments.size() == 1) {
		CHECK_EQ(timeout->arguments[0].name, "seconds");
		CHECK_EQ(timeout->arguments[0].type, "float");
		CHECK_EQ(timeout->arguments[0].default_value, "5.0");
	}

	CHECK_EQ(tags->qualifiers, "class method vararg");
	CHECK(tags->description.is_empty());
	CHECK(tags->arguments.is_empty());
	CHECK_EQ(tags->rest_argument.name, "names");
	CHECK_EQ(tags->rest_argument.type, "String");
}

TEST_CASE("[Modules][FoundryScript] Docgen emits async method qualifiers") {
	FSParser parser;
	Error err = parser.parse(R"(
async func load() -> int:
	return 1

static async func make() -> void:
	pass

func legacy_wait(done: Signal) -> void:
	await done
)",
			"user://async_docgen.fs", false);
	CHECK_EQ(err, OK);

	FSAnalyzer analyzer(&parser);
	err = analyzer.analyze();

	CHECK_EQ(err, OK);
	const FSParser::ClassNode *root = parser.get_tree();
	CHECK(root != nullptr);
	if (err != OK || root == nullptr) {
		return;
	}

	Ref<FoundryScript> script;
	script.instantiate();
	FSCompiler::make_scripts(script.ptr(), root, false);
	FSDocGen::generate_docs(script.ptr(), root);

	const Vector<DocData::ClassDoc> docs = script->get_documentation();
	CHECK_EQ(docs.size(), 1);
	if (docs.size() != 1) {
		return;
	}

	String load_qualifiers;
	String make_qualifiers;
	String legacy_wait_qualifiers;
	for (const DocData::MethodDoc &method : docs[0].methods) {
		if (method.name == "load") {
			load_qualifiers = method.qualifiers;
		} else if (method.name == "make") {
			make_qualifiers = method.qualifiers;
		} else if (method.name == "legacy_wait") {
			legacy_wait_qualifiers = method.qualifiers;
		}
	}

	CHECK_EQ(load_qualifiers, "async");
	CHECK_EQ(make_qualifiers, "static async");
	CHECK_EQ(legacy_wait_qualifiers, "async");
}

TEST_CASE("[Modules][FoundryScript] Docgen marks traits") {
	FSParser parser;
	Error err = parser.parse(R"(
trait InnerTrait:
	func helper() -> void:
		pass

class PlainInner:
	pass
)",
			"user://trait_docgen.fs", false);
	CHECK_EQ(err, OK);

	FSAnalyzer analyzer(&parser);
	err = analyzer.analyze();
	CHECK_EQ(err, OK);
	const FSParser::ClassNode *root = parser.get_tree();
	CHECK(root != nullptr);
	if (err != OK || root == nullptr) {
		return;
	}

	Ref<FoundryScript> script;
	script.instantiate();
	FSCompiler::make_scripts(script.ptr(), root, false);
	FSDocGen::generate_docs(script.ptr(), root);

	const Vector<DocData::ClassDoc> docs = script->get_documentation();

	bool found_trait = false;
	bool found_plain_inner = false;
	bool found_root = false;
	for (const DocData::ClassDoc &class_doc : docs) {
		if (class_doc.name.ends_with("InnerTrait")) {
			found_trait = true;
			CHECK(class_doc.is_trait);
		} else if (class_doc.name.ends_with("PlainInner")) {
			found_plain_inner = true;
			CHECK_FALSE(class_doc.is_trait);
		} else {
			found_root = true;
			CHECK_FALSE(class_doc.is_trait);
		}
	}
	CHECK(found_trait);
	CHECK(found_plain_inner);
	CHECK(found_root);

	// The trait marker survives a dictionary round-trip.
	DocData::ClassDoc trait_doc;
	trait_doc.is_trait = true;
	const DocData::ClassDoc restored = DocData::ClassDoc::from_dict(DocData::ClassDoc::to_dict(trait_doc));
	CHECK(restored.is_trait);
}

TEST_CASE("[Modules][FoundryScript] Docgen records trait uses") {
	FSParser parser;
	Error err = parser.parse(R"(
trait Identified:
	func id() -> int:
		return 1

trait Damageable uses Identified:
	func describe() -> String:
		return "damage"

class Player:
	uses Damageable
)",
			"user://trait_uses_docgen.fs", false);
	CHECK_EQ(err, OK);

	FSAnalyzer analyzer(&parser);
	err = analyzer.analyze();
	CHECK_EQ(err, OK);
	const FSParser::ClassNode *root = parser.get_tree();
	CHECK(root != nullptr);
	if (err != OK || root == nullptr) {
		return;
	}

	Ref<FoundryScript> script;
	script.instantiate();
	FSCompiler::make_scripts(script.ptr(), root, false);
	FSDocGen::generate_docs(script.ptr(), root);

	const Vector<DocData::ClassDoc> docs = script->get_documentation();

	DocData::ClassDoc damageable_doc;
	DocData::ClassDoc player_doc;
	for (const DocData::ClassDoc &class_doc : docs) {
		if (class_doc.name.ends_with(".Damageable")) {
			damageable_doc = class_doc;
		} else if (class_doc.name.ends_with(".Player")) {
			player_doc = class_doc;
		}
	}

	CHECK(!damageable_doc.name.is_empty());
	CHECK(!player_doc.name.is_empty());
	if (damageable_doc.name.is_empty() || player_doc.name.is_empty()) {
		return;
	}
	CHECK_EQ(damageable_doc.used_traits.size(), 1);
	if (damageable_doc.used_traits.is_empty()) {
		return;
	}
	CHECK(damageable_doc.used_traits[0].ends_with(".Identified"));
	CHECK_EQ(player_doc.used_traits.size(), 1);
	if (player_doc.used_traits.is_empty()) {
		return;
	}
	CHECK(player_doc.used_traits[0].ends_with(".Damageable"));

	const DocData::ClassDoc restored = DocData::ClassDoc::from_dict(DocData::ClassDoc::to_dict(player_doc));
	CHECK_EQ(restored.used_traits.size(), 1);
	if (restored.used_traits.is_empty()) {
		return;
	}
	CHECK(restored.used_traits[0].ends_with(".Damageable"));
}

#endif // TOOLS_ENABLED

TEST_CASE("[Modules][FoundryScript] Namespaced global classes can share a local name") {
	GlobalScriptClassCacheBackup backup;
	ScriptServer::global_classes_clear();

	ScriptServer::add_global_class("characters.Controller", "Node", FSLanguage::get_singleton()->get_name(), "res://characters/controller.fs", false, false, false);
	ScriptServer::add_global_class("ui.Controller", "Node", FSLanguage::get_singleton()->get_name(), "res://ui/controller.fs", false, false, false);

	CHECK(ScriptServer::is_global_class("characters.Controller"));
	CHECK(ScriptServer::is_global_class("ui.Controller"));
	CHECK_EQ(ScriptServer::get_global_class_path("characters.Controller"), "res://characters/controller.fs");
	CHECK_EQ(ScriptServer::get_global_class_path("ui.Controller"), "res://ui/controller.fs");
}

TEST_CASE("[Modules][FoundryScript] Global script class cache saves qualified class names") {
	GlobalScriptClassCacheBackup backup;
	ScriptServer::global_classes_clear();
	ProjectSettings::get_singleton()->store_global_class_list(Array());

	ScriptServer::add_global_class("characters.BaseCharacter", "Node", FSLanguage::get_singleton()->get_name(),
			"res://characters/base_character.fs", true, false, false);
	ScriptServer::save_global_classes();

	TypedArray<Dictionary> script_classes = ProjectSettings::get_singleton()->get_global_class_list();
	CHECK_EQ(script_classes.size(), 1);
	if (script_classes.size() == 1) {
		Dictionary script_class = script_classes[0];
		CHECK_EQ(String(script_class["class"]), "characters.BaseCharacter");
		CHECK_FALSE(script_class.has("namespace"));
		CHECK_FALSE(script_class.has("class_name"));
	}
}

TEST_CASE("[Modules][FoundryScript] Old global script class cache entries still load") {
	GlobalScriptClassCacheBackup backup;
	ScriptServer::global_classes_clear();

	Dictionary old_script_class;
	old_script_class["class"] = "LegacyCharacter";
	old_script_class["language"] = FSLanguage::get_singleton()->get_name();
	old_script_class["path"] = "res://legacy_character.fs";
	old_script_class["base"] = "Node";
	old_script_class["is_abstract"] = false;
	old_script_class["is_tool"] = false;

	Array old_cache;
	old_cache.push_back(old_script_class);
	ProjectSettings::get_singleton()->store_global_class_list(old_cache);

	ProjectSettings::get_singleton()->refresh_global_class_list();

	CHECK(ScriptServer::is_global_class("LegacyCharacter"));
	CHECK_EQ(ScriptServer::get_global_class_path("LegacyCharacter"), "res://legacy_character.fs");
	// Caches written before the `is_trait` flag existed default to not-a-trait.
	CHECK_FALSE(ScriptServer::is_global_class_trait("LegacyCharacter"));
	// Caches written before the `is_enum` flag existed default to not-an-enum.
	CHECK_FALSE(ScriptServer::is_global_class_enum("LegacyCharacter"));
}

TEST_CASE("[Modules][FoundryScript] Global class registry tracks the trait flag") {
	GlobalScriptClassCacheBackup backup;
	ScriptServer::global_classes_clear();

	ScriptServer::add_global_class("combat.Damageable", "RefCounted", FSLanguage::get_singleton()->get_name(),
			"res://combat/damageable.fs", false, false, true);
	ScriptServer::add_global_class("characters.BaseCharacter", "Node", FSLanguage::get_singleton()->get_name(),
			"res://characters/base_character.fs", false, false, false);

	CHECK(ScriptServer::is_global_class_trait("combat.Damageable"));
	CHECK_FALSE(ScriptServer::is_global_class_trait("characters.BaseCharacter"));

	// Re-registering with a different flag updates the cached bit.
	ScriptServer::add_global_class("characters.BaseCharacter", "Node", FSLanguage::get_singleton()->get_name(),
			"res://characters/base_character.fs", false, false, true);
	CHECK(ScriptServer::is_global_class_trait("characters.BaseCharacter"));
}

TEST_CASE("[Modules][FoundryScript] Global class registry tracks the enum flag") {
	GlobalScriptClassCacheBackup backup;
	ScriptServer::global_classes_clear();

	ScriptServer::add_global_class("items.WeaponType", "RefCounted", FSLanguage::get_singleton()->get_name(),
			"res://items/weapon_type.fs", false, false, false, true);
	ScriptServer::add_global_class("characters.BaseCharacter", "Node", FSLanguage::get_singleton()->get_name(),
			"res://characters/base_character.fs", false, false, false, false);

	CHECK(ScriptServer::is_global_class_enum("items.WeaponType"));
	CHECK_FALSE(ScriptServer::is_global_class_enum("characters.BaseCharacter"));

	// Re-registering with a different flag updates the cached bit.
	ScriptServer::add_global_class("characters.BaseCharacter", "Node", FSLanguage::get_singleton()->get_name(),
			"res://characters/base_character.fs", false, false, false, true);
	CHECK(ScriptServer::is_global_class_enum("characters.BaseCharacter"));
}

TEST_CASE("[Modules][FoundryScript] Global script class cache round-trips the trait flag") {
	GlobalScriptClassCacheBackup backup;
	ScriptServer::global_classes_clear();
	ProjectSettings::get_singleton()->store_global_class_list(Array());

	ScriptServer::add_global_class("combat.Damageable", "RefCounted", FSLanguage::get_singleton()->get_name(),
			"res://combat/damageable.fs", false, false, true);
	ScriptServer::save_global_classes();

	TypedArray<Dictionary> script_classes = ProjectSettings::get_singleton()->get_global_class_list();
	CHECK_EQ(script_classes.size(), 1);
	if (script_classes.size() == 1) {
		Dictionary script_class = script_classes[0];
		CHECK(script_class.has("is_trait"));
		CHECK(bool(script_class["is_trait"]));
	}

	// Reloading the serialized cache restores the trait flag.
	ScriptServer::global_classes_clear();
	ProjectSettings::get_singleton()->refresh_global_class_list();
	CHECK(ScriptServer::is_global_class_trait("combat.Damageable"));
}

TEST_CASE("[Modules][FoundryScript] Global script class cache round-trips the enum flag") {
	GlobalScriptClassCacheBackup backup;
	ScriptServer::global_classes_clear();
	ProjectSettings::get_singleton()->store_global_class_list(Array());

	ScriptServer::add_global_class("items.WeaponType", "RefCounted", FSLanguage::get_singleton()->get_name(),
			"res://items/weapon_type.fs", false, false, false, true);
	ScriptServer::save_global_classes();

	TypedArray<Dictionary> script_classes = ProjectSettings::get_singleton()->get_global_class_list();
	CHECK_EQ(script_classes.size(), 1);
	if (script_classes.size() == 1) {
		Dictionary script_class = script_classes[0];
		CHECK(script_class.has("is_enum"));
		CHECK(bool(script_class["is_enum"]));
	}

	// Reloading the serialized cache restores the enum flag.
	ScriptServer::global_classes_clear();
	ProjectSettings::get_singleton()->refresh_global_class_list();
	CHECK(ScriptServer::is_global_class_enum("items.WeaponType"));
}

TEST_CASE("[Modules][FoundryScript] Fully qualified global class collisions keep both paths visible") {
	GlobalScriptClassCacheBackup backup;
	ScriptServer::global_classes_clear();

	ScriptServer::add_global_class("characters.BaseCharacter", "Node", FSLanguage::get_singleton()->get_name(),
			"res://characters/base_character.fs", false, false, false);

	FSParser parser;
	Error err = parser.parse(R"(
namespace characters
class_name BaseCharacter
extends Node
)",
			"res://duplicates/base_character.fs", false);
	CHECK_EQ(err, OK);

	FSAnalyzer analyzer(&parser);
	err = analyzer.analyze();

	CHECK_NE(err, OK);
	const String expected_error = R"(Class "characters.BaseCharacter" from "res://duplicates/base_character.fs" )"
								  "collides with global script class from \"res://characters/base_character.fs\".";
	bool found_expected_error = false;
	for (const FSParser::ParserError &parser_error : parser.get_errors()) {
		if (parser_error.message == expected_error) {
			found_expected_error = true;
			break;
		}
	}
	CHECK(found_expected_error);
	CHECK_EQ(ScriptServer::get_global_class_path("characters.BaseCharacter"), "res://characters/base_character.fs");
}

TEST_CASE("[Modules][FoundryScript] Global class re-registration can update the script path") {
	GlobalScriptClassCacheBackup backup;
	ScriptServer::global_classes_clear();

	ScriptServer::add_global_class("characters.MovedCharacter", "Node", FSLanguage::get_singleton()->get_name(),
			"res://characters/old_path.fs", false, false, false);
	ScriptServer::add_global_class("characters.MovedCharacter", "Node", FSLanguage::get_singleton()->get_name(),
			"res://characters/new_path.fs", false, false, false);

	CHECK_EQ(ScriptServer::get_global_class_path("characters.MovedCharacter"), "res://characters/new_path.fs");
}

TEST_CASE("[Modules][FoundryScript] Filesystem scan registers global classes with their metadata") {
	GlobalScriptClassCacheBackup backup;
	ScriptServer::global_classes_clear();

	TemporaryProjectTree tree("fs_global_class_scan_discovery");
	tree.write_file("player.fs", "class_name ScanPlayer\nextends Node\n");
	tree.write_file("npc/guard.fs", "namespace scan.npcs\nclass_name ScanGuard\nextends Node\n");
	tree.write_file("combat/sharable.fs", "trait_name ScanSharable\n\nabstract func share() -> void\n");
	tree.write_file("items/weapon_type.fs", "enum_name ScanWeaponType:\n\tAXE = 0\n\tSWORD = 1\n");
	tree.write_file("base/creature.fs", "abstract class_name ScanCreature\nextends Node\n");
	tree.write_file("tools/painter.fs", "@tool\nclass_name ScanPainter\nextends Node\n");
	tree.write_file("notes.txt", "not a script\n");

	ScriptServer::scan_global_classes(tree.root);

	CHECK(ScriptServer::is_global_class("ScanPlayer"));
	CHECK_EQ(ScriptServer::get_global_class_path("ScanPlayer"), tree.root.path_join("player.fs"));
	CHECK_EQ(ScriptServer::get_global_class_base("ScanPlayer"), StringName("Node"));
	CHECK_EQ(ScriptServer::get_global_class_language("ScanPlayer"), FSLanguage::get_singleton()->get_name());

	CHECK(ScriptServer::is_global_class("scan.npcs.ScanGuard"));
	CHECK_EQ(ScriptServer::get_global_class_path("scan.npcs.ScanGuard"), tree.root.path_join("npc/guard.fs"));

	CHECK(ScriptServer::is_global_class_trait("ScanSharable"));
	CHECK(ScriptServer::is_global_class_enum("ScanWeaponType"));
	CHECK(ScriptServer::is_global_class_abstract("ScanCreature"));
	CHECK(ScriptServer::is_global_class_tool("ScanPainter"));
}

TEST_CASE("[Modules][FoundryScript] Filesystem scan honors the editor's directory skip rules") {
	GlobalScriptClassCacheBackup backup;
	ScriptServer::global_classes_clear();

	TemporaryProjectTree tree("fs_global_class_scan_skip_rules");
	tree.write_file("keep.fs", "class_name ScanKeepRoot\nextends Node\n");
	// Unlike the migration wizard's scan, addons are first-party for class resolution: the editor
	// registers their global classes, so the headless scan must too.
	tree.write_file("addons/lib/plugin_class.fs", "class_name ScanAddonClass\nextends Node\n");
	tree.write_file("vendor/.fsignore", "");
	tree.write_file("vendor/lib.fs", "class_name ScanVendored\nextends Node\n");
	tree.write_file(".hidden/secret.fs", "class_name ScanHiddenClass\nextends Node\n");
	tree.write_file("nested/project.foundry", "[application]\n");
	tree.write_file("nested/sub.fs", "class_name ScanNestedClass\nextends Node\n");

	ScriptServer::scan_global_classes(tree.root);

	CHECK(ScriptServer::is_global_class("ScanKeepRoot"));
	CHECK(ScriptServer::is_global_class("ScanAddonClass"));
	CHECK_FALSE(ScriptServer::is_global_class("ScanVendored"));
	CHECK_FALSE(ScriptServer::is_global_class("ScanHiddenClass"));
	CHECK_FALSE(ScriptServer::is_global_class("ScanNestedClass"));
}

TEST_CASE("[Modules][FoundryScript] Filesystem scan reconciles stale entries against disk state") {
	GlobalScriptClassCacheBackup backup;
	ScriptServer::global_classes_clear();

	TemporaryProjectTree tree("fs_global_class_scan_stale");
	tree.write_file("renamed.fs", "class_name ScanNewName\nextends Node\n");

	// A cache entry whose file was deleted, and one whose file now declares a different class.
	ScriptServer::add_global_class("ScanGhost", "Node", FSLanguage::get_singleton()->get_name(),
			tree.root.path_join("ghost.fs"), false, false, false);
	ScriptServer::add_global_class("ScanOldName", "Node", FSLanguage::get_singleton()->get_name(),
			tree.root.path_join("renamed.fs"), false, false, false);
	// Entries the scan is not authoritative for: another language's class under the scanned root,
	// and a FoundryScript class outside it.
	ScriptServer::add_global_class("ScanForeignLanguage", "Node", "NotFoundryScript",
			tree.root.path_join("foreign.fs"), false, false, false);
	ScriptServer::add_global_class("ScanElsewhere", "Node", FSLanguage::get_singleton()->get_name(),
			"res://elsewhere/thing.fs", false, false, false);

	ScriptServer::scan_global_classes(tree.root);

	CHECK_FALSE(ScriptServer::is_global_class("ScanGhost"));
	CHECK_FALSE(ScriptServer::is_global_class("ScanOldName"));
	CHECK(ScriptServer::is_global_class("ScanNewName"));
	CHECK_EQ(ScriptServer::get_global_class_path("ScanNewName"), tree.root.path_join("renamed.fs"));
	CHECK(ScriptServer::is_global_class("ScanForeignLanguage"));
	CHECK(ScriptServer::is_global_class("ScanElsewhere"));
}

TEST_CASE("[Modules][FoundryScript] Filesystem scan keeps the first file on duplicate class names") {
	GlobalScriptClassCacheBackup backup;
	ScriptServer::global_classes_clear();

	TemporaryProjectTree tree("fs_global_class_scan_duplicates");
	tree.write_file("alpha.fs", "class_name ScanDuplicate\nextends Node\n");
	tree.write_file("beta.fs", "class_name ScanDuplicate\nextends Node\n");

	ERR_PRINT_OFF;
	ScriptServer::scan_global_classes(tree.root);
	ERR_PRINT_ON;

	CHECK(ScriptServer::is_global_class("ScanDuplicate"));
	// Files are scanned in sorted order, so the collision always resolves to the same file.
	CHECK_EQ(ScriptServer::get_global_class_path("ScanDuplicate"), tree.root.path_join("alpha.fs"));
}

TEST_CASE("[Modules][FoundryScript] Filesystem scan indexes annotation-only libraries") {
	GlobalScriptClassCacheBackup backup;
	ScriptServer::global_classes_clear();

	TemporaryProjectTree tree("fs_global_class_scan_annotations");
	const String library_path = tree.root.path_join("testlib/annotations.fs");
	tree.write_file("testlib/annotations.fs", "namespace scan.testlib\n\nannotation slow targets CLASS\n");

	ScriptServer::scan_global_classes(tree.root);

	CHECK(FSLanguage::get_singleton()->is_global_annotation(StringName("scan.testlib.slow")));
	CHECK_EQ(FSLanguage::get_singleton()->get_global_annotation_path(StringName("scan.testlib.slow")), library_path);

	FSLanguage::get_singleton()->remove_global_annotations_by_path(library_path);
}

TEST_CASE("[Modules][FoundryScript] Filesystem scan resolves global classes without a cache file") {
	ScopedFSNativeGlobals native_globals;
	GlobalScriptClassCacheBackup backup;
	ScriptServer::global_classes_clear();

	TemporaryProjectTree tree("fs_global_class_scan_resolution");
	tree.write_file("unit.fs", "class_name ScanCachelessUnit\nextends RefCounted\n\nfunc describe() -> String:\n\treturn \"unit\"\n");
	tree.write_file("consumer.fs",
			"extends RefCounted\n\nfunc run() -> String:\n\tvar unit := ScanCachelessUnit.new()\n\treturn unit.describe()\n");

	ScriptServer::scan_global_classes(tree.root);
	CHECK(ScriptServer::is_global_class("ScanCachelessUnit"));

	// The consumer references the class purely by its global name; with no cache file anywhere,
	// only the scan makes this resolvable.
	const String consumer_path = tree.root.path_join("consumer.fs");
	Ref<FileAccess> consumer_file = FileAccess::open(consumer_path, FileAccess::READ);
	CHECK(consumer_file.is_valid());
	if (consumer_file.is_null()) {
		return;
	}

	FSParser parser;
	CHECK_EQ(parser.parse(consumer_file->get_as_utf8_string(), consumer_path, false), OK);
	FSAnalyzer analyzer(&parser);
	CHECK_EQ(analyzer.analyze(), OK);

	Ref<FoundryScript> script;
	script.instantiate();
	script->set_path(consumer_path);
	FSCompiler compiler;
	CHECK_EQ(compiler.compile(&parser, script.ptr(), false), OK);
}

TEST_CASE("[Modules][FoundryScript] Filesystem scan resolves trait widening without a cache file") {
	ScopedFSNativeGlobals native_globals;
	GlobalScriptClassCacheBackup backup;
	ScriptServer::global_classes_clear();

	TemporaryProjectTree tree("fs_global_class_scan_trait_widening");
	tree.write_file("b_trait.fs", R"(trait_name BTrait

abstract func value() -> int
)");
	tree.write_file("b_impl.fs", R"(class_name BImpl
extends RefCounted
uses BTrait

func _init() -> void:
	pass

func value() -> int:
	return 42
)");
	tree.write_file("consumer.fs", R"(extends RefCounted

static func that_trait(t: BTrait) -> int:
	return t.value()

static func current() -> BImpl:
	return BImpl.new()

func run() -> int:
	var direct: BTrait = BImpl.new()
	var assigned: BTrait = current()
	var casted := current() as BTrait
	return that_trait(current())
)");

	ScriptServer::scan_global_classes(tree.root);
	CHECK(ScriptServer::is_global_class("BTrait"));
	CHECK(ScriptServer::is_global_class("BImpl"));

	// The consumer references both globals purely by name; with no cache file anywhere, only the
	// scan makes them resolvable. Trait widening must not depend on another script having already
	// raised the conformer to `INTERFACE_SOLVED`.
	const String consumer_path = tree.root.path_join("consumer.fs");
	Ref<FileAccess> consumer_file = FileAccess::open(consumer_path, FileAccess::READ);
	CHECK(consumer_file.is_valid());
	if (consumer_file.is_null()) {
		return;
	}

	FSParser parser;
	CHECK_EQ(parser.parse(consumer_file->get_as_utf8_string(), consumer_path, false), OK);
	FSAnalyzer analyzer(&parser);
	CHECK_EQ(analyzer.analyze(), OK);

	Ref<FoundryScript> script;
	script.instantiate();
	script->set_path(consumer_path);
	FSCompiler compiler;
	CHECK_EQ(compiler.compile(&parser, script.ptr(), false), OK);
}

TEST_CASE("[Modules][FoundryScript] Global class cache version tracks mutations") {
	GlobalScriptClassCacheBackup backup;
	ScriptServer::global_classes_clear();

	const uint64_t empty_version = ScriptServer::get_global_class_cache_version();
	ScriptServer::add_global_class("characters.VersionedCharacter", "Node", FSLanguage::get_singleton()->get_name(),
			"res://characters/versioned_character.fs", false, false, false);
	const uint64_t added_version = ScriptServer::get_global_class_cache_version();
	CHECK_NE(added_version, empty_version);

	ScriptServer::add_global_class("characters.VersionedCharacter", "Node", FSLanguage::get_singleton()->get_name(),
			"res://characters/versioned_character.fs", false, false, false);
	CHECK_EQ(ScriptServer::get_global_class_cache_version(), added_version);

	ScriptServer::add_global_class("characters.VersionedCharacter", "Node", FSLanguage::get_singleton()->get_name(),
			"res://characters/renamed_character.fs", false, false, false);
	const uint64_t updated_version = ScriptServer::get_global_class_cache_version();
	CHECK_NE(updated_version, added_version);

	ScriptServer::remove_global_class("characters.VersionedCharacter");
	const uint64_t removed_version = ScriptServer::get_global_class_cache_version();
	CHECK_NE(removed_version, updated_version);

	ScriptServer::remove_global_class("characters.VersionedCharacter");
	CHECK_EQ(ScriptServer::get_global_class_cache_version(), removed_version);
}

#ifdef DEBUG_ENABLED
TEST_CASE("[Modules][FoundryScript] Analyzer reports mixed namespace directories through warning levels") {
	GlobalScriptClassCacheBackup backup;
	ScriptServer::global_classes_clear();

	const String warning_setting = FSWarning::get_setting_path_from_code(FSWarning::MIXED_NAMESPACE_DIRECTORY);
	const Variant original_warning_setting = ProjectSettings::get_singleton()->get_setting(warning_setting);

	TempScriptFile global_script("mixed_global_script.fs", R"(
class_name MixedGlobalScript
extends Node
)");
	TempScriptFile namespaced_script("mixed_namespaced_script.fs", R"(
namespace characters
class_name MixedNamespacedScript
extends Node
)");
	TempScriptFile ui_script("mixed_ui_script.fs", R"(
namespace ui
class_name MixedUiScript
extends Node
)");

	ScriptServer::add_global_class("MixedGlobalScript", "Node", FSLanguage::get_singleton()->get_name(), global_script.path, false, false, false);
	ScriptServer::add_global_class("characters.MixedNamespacedScript", "Node", FSLanguage::get_singleton()->get_name(), namespaced_script.path, false, false, false);
	ScriptServer::add_global_class("ui.MixedUiScript", "Node", FSLanguage::get_singleton()->get_name(), ui_script.path, false, false, false);

	const String expected_directory = FoundryScript::canonicalize_path(namespaced_script.path).get_base_dir();
	const String expected_warning = vformat(R"(Directory "%s" contains global script classes from mixed namespaces: "<global>", "characters", and "ui".)", expected_directory);

	ProjectSettings::get_singleton()->set_setting(warning_setting, (int)FSWarning::WARN);
	FSParser::update_project_settings();

	FSParser warn_parser;
	Error err = warn_parser.parse(R"(
namespace characters
class_name MixedNamespacedScript
extends Node
)",
			namespaced_script.path, false);
	CHECK_EQ(err, OK);
	if (err == OK) {
		FSAnalyzer analyzer(&warn_parser);
		err = analyzer.analyze();
		CHECK_EQ(err, OK);
		CHECK(has_parser_warning(warn_parser, FSWarning::MIXED_NAMESPACE_DIRECTORY, expected_warning));
	}

	FSParser global_trigger_parser;
	err = global_trigger_parser.parse(R"(
class_name MixedGlobalScript
extends Node
)",
			global_script.path, false);
	CHECK_EQ(err, OK);
	if (err == OK) {
		FSAnalyzer analyzer(&global_trigger_parser);
		err = analyzer.analyze();
		CHECK_EQ(err, OK);
		CHECK(has_parser_warning(global_trigger_parser, FSWarning::MIXED_NAMESPACE_DIRECTORY, expected_warning));
	}

	ProjectSettings::get_singleton()->set_setting(warning_setting, (int)FSWarning::IGNORE);
	FSParser::update_project_settings();
	FSParser ignore_parser;
	err = ignore_parser.parse(R"(
namespace characters
class_name MixedNamespacedScript
extends Node
)",
			namespaced_script.path, false);
	CHECK_EQ(err, OK);
	if (err == OK) {
		FSAnalyzer analyzer(&ignore_parser);
		err = analyzer.analyze();
		CHECK_EQ(err, OK);
		CHECK_EQ(count_parser_warnings(ignore_parser, FSWarning::MIXED_NAMESPACE_DIRECTORY), 0);
	}

	ProjectSettings::get_singleton()->set_setting(warning_setting, (int)FSWarning::ERROR);
	FSParser::update_project_settings();
	FSParser error_parser;
	err = error_parser.parse(R"(
namespace characters
class_name MixedNamespacedScript
extends Node
)",
			namespaced_script.path, false);
	CHECK_EQ(err, OK);
	if (err == OK) {
		FSAnalyzer analyzer(&error_parser);
		err = analyzer.analyze();
		CHECK_NE(err, OK);
		CHECK(has_parser_error(error_parser, expected_warning + " (Warning treated as error.)"));
	}

	ScriptServer::global_classes_clear();
	ScriptServer::add_global_class("MixedGlobalScript", "Node", FSLanguage::get_singleton()->get_name(), global_script.path, false, false, false);
	ScriptServer::add_global_class("characters.MixedNamespacedScript", "Node", FSLanguage::get_singleton()->get_name(), namespaced_script.path, false, false, false);

	const String expected_two_namespace_warning = vformat(R"(Directory "%s" contains global script classes from mixed namespaces: "<global>" and "characters".)", expected_directory);
	ProjectSettings::get_singleton()->set_setting(warning_setting, (int)FSWarning::WARN);
	FSParser::update_project_settings();
	FSParser two_namespace_parser;
	err = two_namespace_parser.parse(R"(
namespace characters
class_name MixedNamespacedScript
extends Node
)",
			namespaced_script.path, false);
	CHECK_EQ(err, OK);
	if (err == OK) {
		FSAnalyzer analyzer(&two_namespace_parser);
		err = analyzer.analyze();
		CHECK_EQ(err, OK);
		CHECK(has_parser_warning(two_namespace_parser, FSWarning::MIXED_NAMESPACE_DIRECTORY, expected_two_namespace_warning));
	}

	ScriptServer::global_classes_clear();
	TempScriptFile same_namespace_peer("same_namespace_peer.fs", R"(
namespace characters
class_name SameNamespacePeer
extends Node
)");
	ScriptServer::add_global_class("characters.MixedNamespacedScript", "Node", FSLanguage::get_singleton()->get_name(), namespaced_script.path, false, false, false);
	ScriptServer::add_global_class("characters.SameNamespacePeer", "Node", FSLanguage::get_singleton()->get_name(), same_namespace_peer.path, false, false, false);

	ProjectSettings::get_singleton()->set_setting(warning_setting, (int)FSWarning::WARN);
	FSParser::update_project_settings();
	FSParser same_namespace_parser;
	err = same_namespace_parser.parse(R"(
namespace characters
class_name MixedNamespacedScript
extends Node
)",
			namespaced_script.path, false);
	CHECK_EQ(err, OK);
	if (err == OK) {
		FSAnalyzer analyzer(&same_namespace_parser);
		err = analyzer.analyze();
		CHECK_EQ(err, OK);
		CHECK_EQ(count_parser_warnings(same_namespace_parser, FSWarning::MIXED_NAMESPACE_DIRECTORY), 0);
	}

	ProjectSettings::get_singleton()->set_setting(warning_setting, original_warning_setting);
	FSParser::update_project_settings();
}
#endif // DEBUG_ENABLED

TEST_CASE("[Modules][FoundryScript] Analyzer resolves namespaced global classes") {
	GlobalScriptClassCacheBackup backup;
	ScriptServer::global_classes_clear();

	TempScriptFile base_character("base_character.fs", R"(
namespace characters
class_name BaseCharacter
extends Node

enum Role:
	HERO = 11
)");
	TempScriptFile controller("my_character_controller.fs", R"(
namespace characters.controllers
class_name MyCharacterController
extends Node

enum State:
	IDLE = 17
)");
	TempScriptFile stat_block("stat_block.fs", R"(
namespace shared
class_name StatBlock
extends Resource
)");
	TempScriptFile global_base_character("global_base_character.fs", R"(
class_name BaseCharacter
extends Resource
)");
	TempScriptFile global_stat_block("global_stat_block.fs", R"(
class_name StatBlock
extends Node
)");

	ScriptServer::add_global_class("characters.BaseCharacter", "Node", FSLanguage::get_singleton()->get_name(), base_character.path, false, false, false);
	ScriptServer::add_global_class("characters.controllers.MyCharacterController", "Node", FSLanguage::get_singleton()->get_name(), controller.path, false, false, false);
	ScriptServer::add_global_class("shared.StatBlock", "Resource", FSLanguage::get_singleton()->get_name(), stat_block.path, false, false, false);
	ScriptServer::add_global_class("BaseCharacter", "Resource", FSLanguage::get_singleton()->get_name(), global_base_character.path, false, false, false);
	ScriptServer::add_global_class("StatBlock", "Node", FSLanguage::get_singleton()->get_name(), global_stat_block.path, false, false, false);

	FSParser parser;
	Error err = parser.parse(R"(
namespace characters
import characters
import shared
class_name Hero
extends Node

var same_namespace: BaseCharacter
var imported: StatBlock
var child_namespace: controllers.MyCharacterController
var fully_qualified: characters.BaseCharacter
var same_namespace_nested: BaseCharacter.Role = BaseCharacter.Role.HERO
var child_namespace_nested: controllers.MyCharacterController.State = controllers.MyCharacterController.State.IDLE
var fully_qualified_nested: characters.BaseCharacter.Role = characters.BaseCharacter.Role.HERO

const SAME_NAMESPACE_ROLE = BaseCharacter.Role.HERO
const CHILD_NAMESPACE_STATE = controllers.MyCharacterController.State.IDLE
const FULLY_QUALIFIED_ROLE = characters.BaseCharacter.Role.HERO

func make_instances() -> void:
	var same := BaseCharacter.new()
	var child := controllers.MyCharacterController.new()
	var qualified := characters.BaseCharacter.new()
)",
			"user://hero.fs", false);
	CHECK_EQ(err, OK);
	if (err != OK) {
		return;
	}

	FSAnalyzer analyzer(&parser);
	err = analyzer.analyze();

	CHECK_EQ(err, OK);
	const FSParser::ClassNode *root = parser.get_tree();
	CHECK(root != nullptr);
	if (err != OK || root == nullptr || root->members.size() < 4) {
		return;
	}

	CHECK_EQ(root->members[0].variable->get_datatype().to_property_info("same_namespace").class_name, "characters.BaseCharacter");
	CHECK_EQ(root->members[1].variable->get_datatype().to_property_info("imported").class_name, "shared.StatBlock");
	CHECK_EQ(root->members[2].variable->get_datatype().to_property_info("child_namespace").class_name, "characters.controllers.MyCharacterController");
	CHECK_EQ(root->members[3].variable->get_datatype().to_property_info("fully_qualified").class_name, "characters.BaseCharacter");
	CHECK_EQ(root->members[4].variable->get_datatype().kind, FSParser::DataType::ENUM);
	CHECK_EQ(root->members[4].variable->get_datatype().native_type, "characters.BaseCharacter.Role");
	CHECK(root->members[4].variable->initializer->is_constant);
	CHECK_EQ(root->members[4].variable->initializer->reduced_value, Variant(11));
	CHECK_EQ(root->members[5].variable->get_datatype().kind, FSParser::DataType::ENUM);
	CHECK_EQ(root->members[5].variable->get_datatype().native_type, "characters.controllers.MyCharacterController.State");
	CHECK(root->members[5].variable->initializer->is_constant);
	CHECK_EQ(root->members[5].variable->initializer->reduced_value, Variant(17));
	CHECK_EQ(root->members[6].variable->get_datatype().kind, FSParser::DataType::ENUM);
	CHECK_EQ(root->members[6].variable->get_datatype().native_type, "characters.BaseCharacter.Role");
	CHECK(root->members[6].variable->initializer->is_constant);
	CHECK_EQ(root->members[6].variable->initializer->reduced_value, Variant(11));

	const FSParser::ConstantNode *same_namespace_role = root->get_member(SNAME("SAME_NAMESPACE_ROLE")).constant;
	CHECK(same_namespace_role != nullptr);
	if (same_namespace_role != nullptr) {
		CHECK(same_namespace_role->initializer->is_constant);
		CHECK_EQ(same_namespace_role->initializer->reduced_value, Variant(11));
	}

	const FSParser::ConstantNode *child_namespace_state = root->get_member(SNAME("CHILD_NAMESPACE_STATE")).constant;
	CHECK(child_namespace_state != nullptr);
	if (child_namespace_state != nullptr) {
		CHECK(child_namespace_state->initializer->is_constant);
		CHECK_EQ(child_namespace_state->initializer->reduced_value, Variant(17));
	}

	const FSParser::ConstantNode *fully_qualified_role = root->get_member(SNAME("FULLY_QUALIFIED_ROLE")).constant;
	CHECK(fully_qualified_role != nullptr);
	if (fully_qualified_role != nullptr) {
		CHECK(fully_qualified_role->initializer->is_constant);
		CHECK_EQ(fully_qualified_role->initializer->reduced_value, Variant(11));
	}
}

TEST_CASE("[Modules][FoundryScript] Analyzer resolves namespaced global traits") {
	GlobalScriptClassCacheBackup backup;
	ScriptServer::global_classes_clear();

	TempScriptFile damageable("ns_trait_damageable.fs", R"(
namespace combat
trait_name Damageable

var health: int = 100
)");
	TempScriptFile loggable("ns_trait_loggable.fs", R"(
namespace shared
trait_name Loggable

var log_count: int = 0
)");
	TempScriptFile trackable("ns_trait_trackable.fs", R"(
namespace combat.controllers
trait_name Trackable

var tracked: bool = false
)");

	ScriptServer::add_global_class("combat.Damageable", "RefCounted", FSLanguage::get_singleton()->get_name(), damageable.path, false, false, false);
	ScriptServer::add_global_class("shared.Loggable", "RefCounted", FSLanguage::get_singleton()->get_name(), loggable.path, false, false, false);
	ScriptServer::add_global_class("combat.controllers.Trackable", "RefCounted", FSLanguage::get_singleton()->get_name(), trackable.path, false, false, false);

	FSParser parser;
	Error err = parser.parse(R"(
namespace combat
import shared
extends RefCounted
uses Damageable, Loggable, controllers.Trackable

var same_namespace: Damageable
var imported: Loggable
var child_namespace: controllers.Trackable
var fully_qualified: combat.Damageable
)",
			"user://ns_trait_consumer.fs", false);
	CHECK_EQ(err, OK);
	if (err != OK) {
		return;
	}

	FSAnalyzer analyzer(&parser);
	err = analyzer.analyze();
	CHECK_EQ(err, OK);

	const FSParser::ClassNode *root = parser.get_tree();
	CHECK(root != nullptr);
	if (err != OK || root == nullptr) {
		return;
	}

	CHECK_EQ(root->used_traits.size(), 3);
	if (root->used_traits.size() == 3) {
		CHECK(root->used_traits[0].resolved_trait != nullptr);
		CHECK(root->used_traits[1].resolved_trait != nullptr);
		CHECK(root->used_traits[2].resolved_trait != nullptr);
		if (root->used_traits[0].resolved_trait != nullptr) {
			CHECK_EQ(root->used_traits[0].resolved_trait->get_global_name(), StringName("combat.Damageable"));
		}
		if (root->used_traits[1].resolved_trait != nullptr) {
			CHECK_EQ(root->used_traits[1].resolved_trait->get_global_name(), StringName("shared.Loggable"));
		}
		if (root->used_traits[2].resolved_trait != nullptr) {
			CHECK_EQ(root->used_traits[2].resolved_trait->get_global_name(), StringName("combat.controllers.Trackable"));
		}
	}

	const FSParser::VariableNode *same_namespace = root->get_member(SNAME("same_namespace")).variable;
	CHECK(same_namespace != nullptr);
	if (same_namespace != nullptr) {
		CHECK_EQ(same_namespace->get_datatype().to_property_info("same_namespace").class_name, "combat.Damageable");
	}

	const FSParser::VariableNode *imported = root->get_member(SNAME("imported")).variable;
	CHECK(imported != nullptr);
	if (imported != nullptr) {
		CHECK_EQ(imported->get_datatype().to_property_info("imported").class_name, "shared.Loggable");
	}

	const FSParser::VariableNode *child_namespace = root->get_member(SNAME("child_namespace")).variable;
	CHECK(child_namespace != nullptr);
	if (child_namespace != nullptr) {
		CHECK_EQ(child_namespace->get_datatype().to_property_info("child_namespace").class_name, "combat.controllers.Trackable");
	}

	const FSParser::VariableNode *fully_qualified = root->get_member(SNAME("fully_qualified")).variable;
	CHECK(fully_qualified != nullptr);
	if (fully_qualified != nullptr) {
		CHECK_EQ(fully_qualified->get_datatype().to_property_info("fully_qualified").class_name, "combat.Damageable");
	}
}

#ifdef DEBUG_ENABLED
TEST_CASE("[Modules][FoundryScript] Analyzer reports mixed namespace directories for trait declarations") {
	GlobalScriptClassCacheBackup backup;
	ScriptServer::global_classes_clear();

	const String warning_setting = FSWarning::get_setting_path_from_code(FSWarning::MIXED_NAMESPACE_DIRECTORY);
	const Variant original_warning_setting = ProjectSettings::get_singleton()->get_setting(warning_setting);

	TempScriptFile namespaced_class("mixed_trait_class.fs", R"(
namespace characters
class_name MixedTraitClass
extends Node
)");
	TempScriptFile namespaced_trait("mixed_trait_decl.fs", R"(
namespace combat
trait_name MixedDeclTrait
)");

	ScriptServer::add_global_class("characters.MixedTraitClass", "Node", FSLanguage::get_singleton()->get_name(), namespaced_class.path, false, false, false);
	ScriptServer::add_global_class("combat.MixedDeclTrait", "RefCounted", FSLanguage::get_singleton()->get_name(), namespaced_trait.path, false, false, true);

	const String expected_directory = FoundryScript::canonicalize_path(namespaced_trait.path).get_base_dir();
	const String expected_warning = vformat(R"(Directory "%s" contains global script classes from mixed namespaces: "characters" and "combat".)", expected_directory);

	ProjectSettings::get_singleton()->set_setting(warning_setting, (int)FSWarning::WARN);
	FSParser::update_project_settings();

	// A `trait_name` declaration is a global script class, so analyzing it must surface the mixed-namespace warning.
	FSParser trait_parser;
	Error err = trait_parser.parse(R"(
namespace combat
trait_name MixedDeclTrait
)",
			namespaced_trait.path, false);
	CHECK_EQ(err, OK);
	if (err == OK) {
		FSAnalyzer analyzer(&trait_parser);
		err = analyzer.analyze();
		CHECK_EQ(err, OK);
		CHECK(has_parser_warning(trait_parser, FSWarning::MIXED_NAMESPACE_DIRECTORY, expected_warning));
	}

	// A namespaced class sharing the directory with a differently-namespaced trait is warned too.
	FSParser class_parser;
	err = class_parser.parse(R"(
namespace characters
class_name MixedTraitClass
extends Node
)",
			namespaced_class.path, false);
	CHECK_EQ(err, OK);
	if (err == OK) {
		FSAnalyzer analyzer(&class_parser);
		err = analyzer.analyze();
		CHECK_EQ(err, OK);
		CHECK(has_parser_warning(class_parser, FSWarning::MIXED_NAMESPACE_DIRECTORY, expected_warning));
	}

	// A trait sharing a namespace with a peer class in the same directory must NOT warn.
	ScriptServer::global_classes_clear();
	TempScriptFile same_namespace_peer("mixed_same_ns_peer.fs", R"(
namespace combat
class_name MixedSameNamespacePeer
extends Node
)");
	ScriptServer::add_global_class("combat.MixedDeclTrait", "RefCounted", FSLanguage::get_singleton()->get_name(), namespaced_trait.path, false, false, true);
	ScriptServer::add_global_class("combat.MixedSameNamespacePeer", "Node", FSLanguage::get_singleton()->get_name(), same_namespace_peer.path, false, false, false);

	FSParser same_namespace_parser;
	err = same_namespace_parser.parse(R"(
namespace combat
trait_name MixedDeclTrait
)",
			namespaced_trait.path, false);
	CHECK_EQ(err, OK);
	if (err == OK) {
		FSAnalyzer analyzer(&same_namespace_parser);
		err = analyzer.analyze();
		CHECK_EQ(err, OK);
		CHECK_EQ(count_parser_warnings(same_namespace_parser, FSWarning::MIXED_NAMESPACE_DIRECTORY), 0);
	}

	ProjectSettings::get_singleton()->set_setting(warning_setting, original_warning_setting);
	FSParser::update_project_settings();
}
#endif // DEBUG_ENABLED

TEST_CASE("[Modules][FoundryScript] Analyzer keeps local and native names ahead of namespace imports") {
	GlobalScriptClassCacheBackup backup;
	ScriptServer::global_classes_clear();

	TempScriptFile imported_base("imported_base_character.fs", R"(
namespace characters
class_name BaseCharacter
extends Node
)");
	TempScriptFile imported_node("imported_node.fs", R"(
namespace characters
class_name Node
extends Resource
)");
	TempScriptFile imported_controller("imported_controller.fs", R"(
namespace characters.controllers
class_name MyCharacterController
extends Resource
)");

	ScriptServer::add_global_class("characters.BaseCharacter", "Node", FSLanguage::get_singleton()->get_name(), imported_base.path, false, false, false);
	ScriptServer::add_global_class("characters.Node", "Resource", FSLanguage::get_singleton()->get_name(), imported_node.path, false, false, false);
	ScriptServer::add_global_class("characters.controllers.MyCharacterController", "Resource", FSLanguage::get_singleton()->get_name(), imported_controller.path, false, false, false);

	FSParser parser;
	Error err = parser.parse(R"(
import characters
class_name Precedence
extends Node

class BaseCharacter:
	extends RefCounted

var native_node: Node
var local_class: BaseCharacter
var controllers := { "MyCharacterController": 1 }

func check_local_precedence() -> int:
	var controllers := { "MyCharacterController": 1 }
	var controller_value: int = controllers.MyCharacterController
	return controller_value

func check_member_precedence() -> int:
	var controller_value: int = controllers.MyCharacterController
	return controller_value
)",
			"user://precedence.fs", false);
	CHECK_EQ(err, OK);
	if (err != OK) {
		return;
	}

	FSAnalyzer analyzer(&parser);
	err = analyzer.analyze();

	CHECK_EQ(err, OK);
	const FSParser::ClassNode *root = parser.get_tree();
	CHECK(root != nullptr);
	if (err != OK || root == nullptr || root->members.size() < 3) {
		return;
	}

	const FSParser::DataType native_node_type = root->get_member(SNAME("native_node")).variable->get_datatype();
	CHECK_EQ(native_node_type.kind, FSParser::DataType::NATIVE);
	CHECK_EQ(native_node_type.native_type, SNAME("Node"));

	const FSParser::DataType local_class_type = root->get_member(SNAME("local_class")).variable->get_datatype();
	CHECK_EQ(local_class_type.kind, FSParser::DataType::CLASS);
	CHECK_EQ(local_class_type.class_type, root->get_member(SNAME("BaseCharacter")).m_class);
}

TEST_CASE("[Modules][FoundryScript] Analyzer reports namespace import errors") {
	GlobalScriptClassCacheBackup backup;
	ScriptServer::global_classes_clear();

	TempScriptFile characters_controller("characters_controller.fs", R"(
namespace characters
class_name Controller
extends Node
)");
	TempScriptFile ui_controller("ui_controller.fs", R"(
namespace ui
class_name Controller
extends Node
)");

	ScriptServer::add_global_class("characters.Controller", "Node", FSLanguage::get_singleton()->get_name(), characters_controller.path, false, false, false);
	ScriptServer::add_global_class("ui.Controller", "Node", FSLanguage::get_singleton()->get_name(), ui_controller.path, false, false, false);

	FSParser missing_import_parser;
	Error err = missing_import_parser.parse(R"(
import missing.tools
class_name MissingImport
extends Node
)",
			"user://missing_import.fs", false);
	CHECK_EQ(err, OK);
	if (err == OK) {
		FSAnalyzer analyzer(&missing_import_parser);
		err = analyzer.analyze();
		CHECK_NE(err, OK);
		CHECK(has_parser_error(missing_import_parser, R"(Could not find imported namespace "missing.tools".)"));
	}

	FSParser duplicate_missing_import_parser;
	err = duplicate_missing_import_parser.parse(R"(
import missing.tools
import missing.tools
class_name DuplicateMissingImport
extends Node
)",
			"user://duplicate_missing_import.fs", false);
	CHECK_EQ(err, OK);
	if (err == OK) {
		FSAnalyzer analyzer(&duplicate_missing_import_parser);
		err = analyzer.analyze();
		CHECK_NE(err, OK);
		CHECK_EQ(count_parser_errors(duplicate_missing_import_parser, R"(Could not find imported namespace "missing.tools".)"), 1);
	}

	FSParser ambiguous_parser;
	err = ambiguous_parser.parse(R"(
import characters
import ui
class_name AmbiguousImport
extends Node

var controller: Controller
)",
			"user://ambiguous_import.fs", false);
	CHECK_EQ(err, OK);
	if (err == OK) {
		FSAnalyzer analyzer(&ambiguous_parser);
		err = analyzer.analyze();
		CHECK_NE(err, OK);
		CHECK(has_parser_error(ambiguous_parser, R"(Could not resolve type "Controller": imported namespaces "characters" and "ui" are ambiguous.)"));
	}
}

static void test_tokenizer(const String &p_code, const Vector<String> &p_lines) {
	FSTokenizerText tokenizer;
	tokenizer.set_source_code(p_code);

	int tab_size = 4;
#ifdef TOOLS_ENABLED
	if (EditorSettings::get_singleton()) {
		tab_size = EditorSettings::get_singleton()->get_setting("text_editor/behavior/indent/size");
	}
#endif // TOOLS_ENABLED
	String tab = String(" ").repeat(tab_size);

	FSTokenizer::Token current = tokenizer.scan();
	while (current.type != FSTokenizer::Token::TK_EOF) {
		StringBuilder token;
		token += " --> "; // Padding for line number.

		if (current.start_line != current.end_line) {
			// Print "vvvvvv" to point at the token.
			StringBuilder pointer;
			pointer += "     "; // Padding for line number.

			int line_width = 0;
			if (current.start_line - 1 >= 0 && current.start_line - 1 < p_lines.size()) {
				line_width = p_lines[current.start_line - 1].replace("\t", tab).length();
			}

			const int offset = MAX(0, current.start_column - 1);
			const int width = MAX(0, line_width - current.start_column + 1);
			pointer += String::chr(' ').repeat(offset) + String::chr('v').repeat(width);

			print_line(pointer.as_string());
		}

		for (int l = current.start_line; l <= current.end_line && l <= p_lines.size(); l++) {
			print_line(vformat("%04d %s", l, p_lines[l - 1]).replace("\t", tab));
		}

		{
			// Print "^^^^^^" to point at the token.
			StringBuilder pointer;
			pointer += "     "; // Padding for line number.

			if (current.start_line == current.end_line) {
				const int offset = MAX(0, current.start_column - 1);
				const int width = MAX(0, current.end_column - current.start_column);
				pointer += String::chr(' ').repeat(offset) + String::chr('^').repeat(width);
			} else {
				const int width = MAX(0, current.end_column - 1);
				pointer += String::chr('^').repeat(width);
			}

			print_line(pointer.as_string());
		}

		token += current.get_name();

		if (current.type == FSTokenizer::Token::ERROR || current.type == FSTokenizer::Token::LITERAL || current.type == FSTokenizer::Token::IDENTIFIER || current.type == FSTokenizer::Token::ANNOTATION) {
			token += "(";
			token += Variant::get_type_name(current.literal.get_type());
			token += ") ";
			token += current.literal;
		}

		print_line(token.as_string());

		print_line("-------------------------------------------------------");

		current = tokenizer.scan();
	}

	print_line(current.get_name()); // Should be EOF
}

static void test_tokenizer_buffer(const Vector<uint8_t> &p_buffer, const Vector<String> &p_lines);

static void test_tokenizer_buffer(const String &p_code, const Vector<String> &p_lines) {
	Vector<uint8_t> binary = FSTokenizerBuffer::parse_code_string(p_code, FSTokenizerBuffer::COMPRESS_NONE);
	test_tokenizer_buffer(binary, p_lines);
}

static void test_tokenizer_buffer(const Vector<uint8_t> &p_buffer, const Vector<String> &p_lines) {
	FSTokenizerBuffer tokenizer;
	tokenizer.set_code_buffer(p_buffer);

	int tab_size = 4;
#ifdef TOOLS_ENABLED
	if (EditorSettings::get_singleton()) {
		tab_size = EditorSettings::get_singleton()->get_setting("text_editor/behavior/indent/size");
	}
#endif // TOOLS_ENABLED
	String tab = String(" ").repeat(tab_size);

	FSTokenizer::Token current = tokenizer.scan();
	while (current.type != FSTokenizer::Token::TK_EOF) {
		StringBuilder token;
		token += " --> "; // Padding for line number.

		for (int l = current.start_line; l <= current.end_line && l <= p_lines.size(); l++) {
			print_line(vformat("%04d %s", l, p_lines[l - 1]).replace("\t", tab));
		}

		token += current.get_name();

		if (current.type == FSTokenizer::Token::ERROR || current.type == FSTokenizer::Token::LITERAL || current.type == FSTokenizer::Token::IDENTIFIER || current.type == FSTokenizer::Token::ANNOTATION) {
			token += "(";
			token += Variant::get_type_name(current.literal.get_type());
			token += ") ";
			token += current.literal;
		}

		print_line(token.as_string());

		print_line("-------------------------------------------------------");

		current = tokenizer.scan();
	}

	print_line(current.get_name()); // Should be EOF
}

static void test_parser(const String &p_code, const String &p_script_path, const Vector<String> &p_lines) {
	FSParser parser;
	Error err = parser.parse(p_code, p_script_path, false);

	if (err != OK) {
		const List<FSParser::ParserError> &errors = parser.get_errors();
		for (const FSParser::ParserError &error : errors) {
			print_line(vformat("%02d:%02d: %s", error.line, error.column, error.message));
		}
	}

	FSAnalyzer analyzer(&parser);
	err = analyzer.analyze();

	if (err != OK) {
		const List<FSParser::ParserError> &errors = parser.get_errors();
		for (const FSParser::ParserError &error : errors) {
			print_line(vformat("%02d:%02d: %s", error.line, error.column, error.message));
		}
	}

#ifdef TOOLS_ENABLED
	FSParser::TreePrinter printer;
	printer.print_tree(parser);
#endif
}

static void disassemble_function(const FSFunction *p_func, const Vector<String> &p_lines) {
	ERR_FAIL_NULL(p_func);

	String arg_string;
	bool is_first_arg = true;
	for (const PropertyInfo &arg_info : p_func->get_method_info().arguments) {
		if (!is_first_arg) {
			arg_string += ", ";
		}
		arg_string += arg_info.name;
		is_first_arg = false;
	}
	if (p_func->is_vararg()) {
		// `MethodInfo` does not support the rest parameter name.
		arg_string += (p_func->get_argument_count() == 0) ? "...args" : ", ...args";
	}

	print_line(vformat("Function %s(%s)", p_func->get_name(), arg_string));
#ifdef TOOLS_ENABLED
	p_func->disassemble(p_lines);
#endif
	print_line("");
	print_line("");
}

static void recursively_disassemble_functions(const Ref<FoundryScript> p_script, const Vector<String> &p_lines) {
	print_line(vformat("Class %s", p_script->get_fully_qualified_name()));
	print_line("");
	print_line("");

	const FSFunction *implicit_initializer = p_script->get_implicit_initializer();
	if (implicit_initializer != nullptr) {
		disassemble_function(implicit_initializer, p_lines);
	}

	const FSFunction *implicit_ready = p_script->get_implicit_ready();
	if (implicit_ready != nullptr) {
		disassemble_function(implicit_ready, p_lines);
	}

	const FSFunction *static_initializer = p_script->get_static_initializer();
	if (static_initializer != nullptr) {
		disassemble_function(static_initializer, p_lines);
	}

	for (const KeyValue<FSFunction *, FoundryScript::LambdaInfo> &E : p_script->get_lambda_info()) {
		disassemble_function(E.key, p_lines);
	}

	for (const KeyValue<StringName, FSFunction *> &E : p_script->get_member_functions()) {
		disassemble_function(E.value, p_lines);
	}

	for (const KeyValue<StringName, Ref<FoundryScript>> &E : p_script->get_subclasses()) {
		recursively_disassemble_functions(E.value, p_lines);
	}
}

static void test_compiler(const String &p_code, const String &p_script_path, const Vector<String> &p_lines) {
	FSParser parser;
	Error err = parser.parse(p_code, p_script_path, false);

	if (err != OK) {
		print_line("Error in parser:");
		const List<FSParser::ParserError> &errors = parser.get_errors();
		for (const FSParser::ParserError &error : errors) {
			print_line(vformat("%02d:%02d: %s", error.line, error.column, error.message));
		}
		return;
	}

	FSAnalyzer analyzer(&parser);
	err = analyzer.analyze();

	if (err != OK) {
		print_line("Error in analyzer:");
		const List<FSParser::ParserError> &errors = parser.get_errors();
		for (const FSParser::ParserError &error : errors) {
			print_line(vformat("%02d:%02d: %s", error.line, error.column, error.message));
		}
		return;
	}

	FSCompiler compiler;
	Ref<FoundryScript> script;
	script.instantiate();
	script->set_path(p_script_path);

	err = compiler.compile(&parser, script.ptr(), false);

	if (err) {
		print_line("Error in compiler:");
		print_line(vformat("%02d:%02d: %s", compiler.get_error_line(), compiler.get_error_column(), compiler.get_error()));
		return;
	}

	recursively_disassemble_functions(script, p_lines);
}

void test(TestType p_type) {
	List<String> cmdlargs = OS::get_singleton()->get_cmdline_args();

	if (cmdlargs.is_empty()) {
		return;
	}

	String test = cmdlargs.back()->get();
	if (!test.ends_with(".fs") && !test.ends_with(".fsc")) {
		print_line("This test expects a path to a FoundryScript file as its last parameter. Got: " + test);
		return;
	}

	Ref<FileAccess> fa = FileAccess::open(test, FileAccess::READ);
	ERR_FAIL_COND_MSG(fa.is_null(), "Could not open file: " + test);

	// Initialize the language for the test routine.
	init_language(fa->get_path_absolute().get_base_dir());

	// Load global classes.
	TypedArray<Dictionary> script_classes = ProjectSettings::get_singleton()->get_global_class_list();
	for (int i = 0; i < script_classes.size(); i++) {
		Dictionary c = script_classes[i];
		if (!c.has("class") || !c.has("language") || !c.has("path") || !c.has("base") || !c.has("is_abstract") || !c.has("is_tool")) {
			continue;
		}
		const bool is_trait = c.has("is_trait") && c["is_trait"];
		const bool is_enum = c.has("is_enum") && c["is_enum"];
		ScriptServer::add_global_class(c["class"], c["base"], c["language"], c["path"], c["is_abstract"], c["is_tool"], is_trait, is_enum);
	}

	Vector<uint8_t> buf;
	uint64_t flen = fa->get_length();
	buf.resize(flen + 1);
	fa->get_buffer(buf.ptrw(), flen);
	buf.write[flen] = 0;

	String code = String::utf8((const char *)&buf[0]);

	Vector<String> lines;
	int last = 0;
	for (int i = 0; i <= code.length(); i++) {
		if (code[i] == '\n' || code[i] == 0) {
			lines.push_back(code.substr(last, i - last));
			last = i + 1;
		}
	}

	switch (p_type) {
		case TEST_TOKENIZER:
			test_tokenizer(code, lines);
			break;
		case TEST_TOKENIZER_BUFFER:
			if (test.ends_with(".fsc")) {
				test_tokenizer_buffer(buf, lines);
			} else {
				test_tokenizer_buffer(code, lines);
			}
			break;
		case TEST_PARSER:
			test_parser(code, test, lines);
			break;
		case TEST_COMPILER:
			test_compiler(code, test, lines);
			break;
		case TEST_BYTECODE:
			print_line("Not implemented.");
	}

	finish_language();
}

static const Vector<FoundryScript::AnnotationUsage> *find_annotation_usages(const HashMap<StringName, Vector<FoundryScript::AnnotationUsage>> &p_table, const StringName &p_name) {
	const Vector<FoundryScript::AnnotationUsage> *usages = p_table.getptr(p_name);
	return usages;
}

TEST_CASE("[Modules][FoundryScript] Compiled scripts persist custom annotation metadata") {
	ScopedFSNativeGlobals native_globals;
	FSParser parser;
	Error err = parser.parse(R"(
namespace cafecito.persist_demo

extends RefCounted
uses Mixin

annotation suite(name: String = "") targets CLASS
annotation tags(...names: String) targets CLASS, METHOD
annotation test targets METHOD
annotation timeout(seconds: float) targets METHOD
annotation cases(provider: String) targets METHOD
annotation fixture targets VARIABLE
annotation label(text: String) targets VARIABLE

trait Mixin:
	@fixture
	@label("from_trait")
	var helper: int

	@test
	@tags("trait")
	func trait_method() -> void:
		pass

@fixture
var world: int

@export var exported_value: int = 0

@test
@timeout(10.0)
@cases(provider = "crit_rows")
@tags("a", "b")
func crit_table() -> void:
	pass

@suite(name = "Combat")
@tags("gameplay")
class Inner:
	@test
	func inner_method() -> void:
		pass
)",
			"user://annotation_metadata.fs", false);

	CHECK_EQ(err, OK);
	if (err != OK) {
		return;
	}

	FSAnalyzer analyzer(&parser);
	err = analyzer.analyze();
	CHECK_EQ(err, OK);
	if (err != OK) {
		return;
	}

	FSCompiler compiler;
	Ref<FoundryScript> script;
	script.instantiate();
	script->set_path("user://annotation_metadata.fs");

	err = compiler.compile(&parser, script.ptr(), false);
	INFO(compiler.get_error());
	CHECK_EQ(err, OK);
	if (err != OK) {
		return;
	}

	// Root class carries no annotations of its own.
	CHECK(script->get_class_annotations().is_empty());

	// Root method annotations preserve source order and split positional/named arguments.
	{
		const Vector<FoundryScript::AnnotationUsage> *usages = find_annotation_usages(script->get_method_annotations(), SNAME("crit_table"));
		CHECK(usages != nullptr);
		if (usages != nullptr) {
			CHECK_EQ(usages->size(), 4);
			if (usages->size() == 4) {
				CHECK_EQ((*usages)[0].name, SNAME("test"));
				CHECK_EQ((*usages)[0].qualified_name, SNAME("cafecito.persist_demo.test"));
				CHECK((*usages)[0].args.is_empty());
				CHECK((*usages)[0].kwargs.is_empty());

				CHECK_EQ((*usages)[1].name, SNAME("timeout"));
				CHECK_EQ((*usages)[1].args.size(), 1);
				if (!(*usages)[1].args.is_empty()) {
					CHECK_EQ(double((*usages)[1].args[0]), doctest::Approx(10.0));
				}
				CHECK((*usages)[1].kwargs.is_empty());

				CHECK_EQ((*usages)[2].name, SNAME("cases"));
				CHECK((*usages)[2].args.is_empty());
				CHECK_EQ((*usages)[2].kwargs.size(), 1);
				CHECK_EQ(String((*usages)[2].kwargs[SNAME("provider")]), "crit_rows");

				CHECK_EQ((*usages)[3].name, SNAME("tags"));
				CHECK_EQ((*usages)[3].args.size(), 2);
				if ((*usages)[3].args.size() == 2) {
					CHECK_EQ(String((*usages)[3].args[0]), "a");
					CHECK_EQ(String((*usages)[3].args[1]), "b");
				}
			}
		}
	}

	// Root member-variable annotations. Custom usages carry `is_builtin == false`.
	{
		const Vector<FoundryScript::AnnotationUsage> *usages = find_annotation_usages(script->get_variable_annotations(), SNAME("world"));
		CHECK(usages != nullptr);
		if (usages != nullptr) {
			CHECK_EQ(usages->size(), 1);
			if (!usages->is_empty()) {
				CHECK_EQ((*usages)[0].name, SNAME("fixture"));
				CHECK_FALSE((*usages)[0].is_builtin);
			}
		}

		// Built-in annotations such as `@export` are now reflected as metadata, tagged `is_builtin`.
		const Vector<FoundryScript::AnnotationUsage> *exported = find_annotation_usages(script->get_variable_annotations(), SNAME("exported_value"));
		CHECK(exported != nullptr);
		if (exported != nullptr) {
			CHECK_EQ(exported->size(), 1);
			if (!exported->is_empty()) {
				CHECK_EQ((*exported)[0].name, SNAME("export"));
				CHECK_EQ((*exported)[0].qualified_name, SNAME("export"));
				CHECK((*exported)[0].is_builtin);
				CHECK((*exported)[0].args.is_empty());
				CHECK((*exported)[0].kwargs.is_empty());
			}
		}
	}

	// Concrete trait members flattened into the implementer carry their declaration annotations.
	{
		const Vector<FoundryScript::AnnotationUsage> *method_usages = find_annotation_usages(script->get_method_annotations(), SNAME("trait_method"));
		CHECK(method_usages != nullptr);
		if (method_usages != nullptr) {
			CHECK_EQ(method_usages->size(), 2);
			if (method_usages->size() == 2) {
				CHECK_EQ((*method_usages)[0].name, SNAME("test"));
				CHECK_EQ((*method_usages)[1].name, SNAME("tags"));
				CHECK_EQ((*method_usages)[1].args.size(), 1);
				if (!(*method_usages)[1].args.is_empty()) {
					CHECK_EQ(String((*method_usages)[1].args[0]), "trait");
				}
			}
		}

		const Vector<FoundryScript::AnnotationUsage> *variable_usages = find_annotation_usages(script->get_variable_annotations(), SNAME("helper"));
		CHECK(variable_usages != nullptr);
		if (variable_usages != nullptr) {
			CHECK_EQ(variable_usages->size(), 2);
			if (variable_usages->size() == 2) {
				CHECK_EQ((*variable_usages)[0].name, SNAME("fixture"));
				CHECK_EQ((*variable_usages)[1].name, SNAME("label"));
				CHECK_EQ((*variable_usages)[1].args.size(), 1);
				if (!(*variable_usages)[1].args.is_empty()) {
					CHECK_EQ(String((*variable_usages)[1].args[0]), "from_trait");
				}
			}
		}
	}

	// Inner-class annotations live on the compiled subclass and are direct-only.
	{
		const HashMap<StringName, Ref<FoundryScript>> &subclasses = script->get_subclasses();
		CHECK(subclasses.has(SNAME("Inner")));
		if (subclasses.has(SNAME("Inner"))) {
			Ref<FoundryScript> inner = subclasses[SNAME("Inner")];
			CHECK(inner.is_valid());
			if (inner.is_valid()) {
				const Vector<FoundryScript::AnnotationUsage> &class_usages = inner->get_class_annotations();
				CHECK_EQ(class_usages.size(), 2);
				if (class_usages.size() == 2) {
					CHECK_EQ(class_usages[0].name, SNAME("suite"));
					CHECK_EQ(class_usages[0].qualified_name, SNAME("cafecito.persist_demo.suite"));
					CHECK_EQ(class_usages[0].kwargs.size(), 1);
					CHECK_EQ(String(class_usages[0].kwargs[SNAME("name")]), "Combat");
					CHECK_EQ(class_usages[1].name, SNAME("tags"));
					CHECK_EQ(class_usages[1].args.size(), 1);
					if (!class_usages[1].args.is_empty()) {
						CHECK_EQ(String(class_usages[1].args[0]), "gameplay");
					}
				}

				const Vector<FoundryScript::AnnotationUsage> *inner_method = find_annotation_usages(inner->get_method_annotations(), SNAME("inner_method"));
				CHECK(inner_method != nullptr);
				if (inner_method != nullptr) {
					CHECK_EQ(inner_method->size(), 1);
					if (!inner_method->is_empty()) {
						CHECK_EQ((*inner_method)[0].name, SNAME("test"));
					}
				}
			}
		}
	}

	// Clearing the compiled script wipes the persisted annotation metadata.
	script->clear();
	CHECK(script->get_class_annotations().is_empty());
	CHECK(script->get_method_annotations().is_empty());
	CHECK(script->get_variable_annotations().is_empty());
}

TEST_CASE("[Modules][FoundryScript] Compiled scripts persist built-in annotation metadata") {
	ScopedFSNativeGlobals native_globals;
	FSParser parser;
	Error err = parser.parse(R"(
@tool
extends Node

annotation test targets METHOD

@export_range(0, 100) var ranged: int = 1

@onready var ready_node: Node = self

@export var exported: int = 0

@rpc("any_peer", "reliable")
func networked() -> void:
	pass

@warning_ignore("unused_parameter")
@test
func custom_and_builtin(unused: int) -> void:
	pass
)",
			"user://builtin_annotation_metadata.fs", false);

	CHECK_EQ(err, OK);
	if (err != OK) {
		return;
	}

	FSAnalyzer analyzer(&parser);
	err = analyzer.analyze();
	CHECK_EQ(err, OK);
	if (err != OK) {
		return;
	}

	FSCompiler compiler;
	Ref<FoundryScript> script;
	script.instantiate();
	script->set_path("user://builtin_annotation_metadata.fs");

	err = compiler.compile(&parser, script.ptr(), false);
	INFO(compiler.get_error());
	CHECK_EQ(err, OK);
	if (err != OK) {
		return;
	}

	// Script-configuration annotations (`@tool`, `@icon`, `@static_unload`) are applied by the parser
	// and not retained on the AST, so they are intentionally not surfaced as class annotations.
	CHECK(script->get_class_annotations().is_empty());

	// `@export_range(0, 100)` carries its positional arguments in `args`.
	{
		const Vector<FoundryScript::AnnotationUsage> *usages = find_annotation_usages(script->get_variable_annotations(), SNAME("ranged"));
		CHECK(usages != nullptr);
		if (usages != nullptr) {
			CHECK_EQ(usages->size(), 1);
			if (!usages->is_empty()) {
				CHECK_EQ((*usages)[0].name, SNAME("export_range"));
				CHECK((*usages)[0].is_builtin);
				CHECK_EQ((*usages)[0].args.size(), 2);
				if ((*usages)[0].args.size() == 2) {
					CHECK_EQ(int((*usages)[0].args[0]), 0);
					CHECK_EQ(int((*usages)[0].args[1]), 100);
				}
				CHECK((*usages)[0].kwargs.is_empty());
			}
		}
	}

	// `@onready` is a marker built-in with no arguments.
	{
		const Vector<FoundryScript::AnnotationUsage> *usages = find_annotation_usages(script->get_variable_annotations(), SNAME("ready_node"));
		CHECK(usages != nullptr);
		if (usages != nullptr && !usages->is_empty()) {
			CHECK_EQ((*usages)[0].name, SNAME("onready"));
			CHECK((*usages)[0].is_builtin);
			CHECK((*usages)[0].args.is_empty());
		}
	}

	// `@rpc` arguments are reflected positionally.
	{
		const Vector<FoundryScript::AnnotationUsage> *usages = find_annotation_usages(script->get_method_annotations(), SNAME("networked"));
		CHECK(usages != nullptr);
		if (usages != nullptr && !usages->is_empty()) {
			CHECK_EQ((*usages)[0].name, SNAME("rpc"));
			CHECK((*usages)[0].is_builtin);
			CHECK_EQ((*usages)[0].args.size(), 2);
		}
	}

	// A custom annotation on a method is still recorded and stays non-built-in. `@warning_ignore` is a
	// diagnostic directive whose arguments only resolve under DEBUG_ENABLED, so it is excluded from
	// reflected metadata to stay consistent across build configurations.
	{
		const Vector<FoundryScript::AnnotationUsage> *usages = find_annotation_usages(script->get_method_annotations(), SNAME("custom_and_builtin"));
		CHECK(usages != nullptr);
		if (usages != nullptr) {
			CHECK_EQ(usages->size(), 1);
			if (!usages->is_empty()) {
				CHECK_EQ((*usages)[0].name, SNAME("test"));
				CHECK_FALSE((*usages)[0].is_builtin);
			}
		}
	}
}

TEST_CASE("[Modules][FoundryScript] keep_name metadata covers supported declarations") {
	ScopedFSNativeGlobals native_globals;
	FSParser parser;
	Error err = parser.parse(R"(
@keep_name
class_name KeepNameMetadataRoot

@keep_name var kept_member: int
@keep_name signal kept_signal
@keep_name const KEPT_CONSTANT = 1
@keep_name enum KeptEnum:
	VALUE = 0
	@keep_name func kept_enum_instance() -> void:
		pass
	@keep_name func shared_enum_escape() -> void:
		pass
@keep_name enum KeptStaticEnum:
	VALUE = 0
	@keep_name static func kept_enum_static() -> void:
		pass
	@keep_name static func shared_enum_escape() -> void:
		pass
@keep_name func kept_method() -> void:
	pass
@keep_name class KeptInner:
	pass
)",
			"user://keep_name_metadata.fs", false);

	String parser_error_messages;
	for (const FSParser::ParserError &parser_error : parser.get_errors()) {
		parser_error_messages += parser_error.message + "\n";
	}
	INFO(parser_error_messages);
	CHECK_EQ(err, OK);
	if (err != OK) {
		return;
	}

	FSAnalyzer analyzer(&parser);
	err = analyzer.analyze();
	CHECK_EQ(err, OK);
	if (err != OK) {
		return;
	}

	FSCompiler compiler;
	Ref<FoundryScript> script;
	script.instantiate();
	script->set_path("user://keep_name_metadata.fs");

	err = compiler.compile(&parser, script.ptr(), false);
	INFO(compiler.get_error());
	CHECK_EQ(err, OK);
	if (err != OK) {
		return;
	}

	const auto check_keep_name_usage = [](const Vector<FoundryScript::AnnotationUsage> *p_usages) {
		CHECK(p_usages != nullptr);
		if (p_usages == nullptr) {
			return;
		}
		CHECK_EQ(p_usages->size(), 1);
		if (p_usages->is_empty()) {
			return;
		}
		CHECK_EQ((*p_usages)[0].name, SNAME("keep_name"));
		CHECK_EQ((*p_usages)[0].qualified_name, SNAME("keep_name"));
		CHECK((*p_usages)[0].is_builtin);
		CHECK((*p_usages)[0].args.is_empty());
		CHECK((*p_usages)[0].kwargs.is_empty());
	};

	check_keep_name_usage(&script->get_class_annotations());
	check_keep_name_usage(find_annotation_usages(script->get_variable_annotations(), SNAME("kept_member")));
	check_keep_name_usage(find_annotation_usages(script->get_signal_annotations(), SNAME("kept_signal")));
	check_keep_name_usage(find_annotation_usages(script->get_constant_annotations(), SNAME("KEPT_CONSTANT")));
	check_keep_name_usage(find_annotation_usages(script->get_constant_annotations(), SNAME("KeptEnum")));
	check_keep_name_usage(find_annotation_usages(script->get_constant_annotations(), SNAME("KeptStaticEnum")));
	check_keep_name_usage(find_annotation_usages(script->get_method_annotations(), SNAME("kept_method")));
	check_keep_name_usage(find_annotation_usages(script->get_method_annotations(), SNAME("kept_enum_instance")));
	check_keep_name_usage(find_annotation_usages(script->get_method_annotations(), SNAME("kept_enum_static")));
	check_keep_name_usage(find_annotation_usages(script->get_method_annotations(), SNAME("shared_enum_escape")));

	const Ref<FoundryScript> inner = script->get_subclasses()[SNAME("KeptInner")];
	CHECK(inner.is_valid());
	if (inner.is_valid()) {
		check_keep_name_usage(&inner->get_class_annotations());
	}

	List<MethodInfo> public_annotations;
	FSLanguage::get_singleton()->get_public_annotations(&public_annotations);
	bool found_public_keep_name = false;
	for (const MethodInfo &annotation : public_annotations) {
		if (annotation.name == SNAME("@keep_name")) {
			found_public_keep_name = true;
			break;
		}
	}
	CHECK(found_public_keep_name);
}

TEST_CASE("[Modules][FoundryScript] keep_name does not expand custom enum annotation targets") {
	FSParser parser;
	const Error err = parser.parse(
			"annotation marker targets CONSTANT\n"
			"@marker enum Marked:\n"
			"\tVALUE = 0\n",
			"user://keep_name_custom_enum_target.fs", false);

	String parser_error_messages;
	for (const FSParser::ParserError &parser_error : parser.get_errors()) {
		parser_error_messages += parser_error.message + "\n";
	}
	INFO(parser_error_messages);
	CHECK_EQ(err, ERR_PARSE_ERROR);
	CHECK(parser_error_messages.contains(R"(Annotation "@marker" cannot be applied to a enum.)"));
}

TEST_CASE("[Modules][FoundryScript] FSAnnotation descriptor snapshots annotation metadata") {
	FoundryScript::AnnotationUsage usage;
	usage.name = SNAME("timeout");
	usage.qualified_name = SNAME("cafecito.test.timeout");
	usage.args.push_back(10.0);
	usage.args.push_back("slow");
	usage.kwargs[SNAME("provider")] = "crit_rows";

	Ref<FSAnnotation> descriptor = FSAnnotation::from_usage(usage);
	CHECK(descriptor.is_valid());
	if (descriptor.is_null()) {
		return;
	}

	SUBCASE("getters expose the resolved metadata") {
		CHECK_EQ(descriptor->get_annotation_name(), SNAME("timeout"));
		CHECK_EQ(descriptor->get_qualified_name(), SNAME("cafecito.test.timeout"));

		Array args = descriptor->get_arguments();
		CHECK_EQ(args.size(), 2);
		if (args.size() == 2) {
			CHECK_EQ(double(args[0]), 10.0);
			CHECK_EQ(String(args[1]), "slow");
		}

		Dictionary kwargs = descriptor->get_named_arguments();
		CHECK_EQ(kwargs.size(), 1);
		CHECK_EQ(String(kwargs[SNAME("provider")]), "crit_rows");

		// Custom annotations default to non-built-in.
		CHECK_FALSE(descriptor->is_builtin());
	}

	SUBCASE("bound accessors are reachable through the script API") {
		CHECK_EQ(descriptor->call(SNAME("get_annotation_name")), Variant(SNAME("timeout")));
		CHECK_EQ(descriptor->call(SNAME("get_qualified_name")), Variant(SNAME("cafecito.test.timeout")));
		CHECK_EQ(descriptor->get(SNAME("name")), Variant(SNAME("timeout")));
		CHECK_EQ(Array(descriptor->get(SNAME("args"))).size(), 2);
		CHECK_EQ(Dictionary(descriptor->get(SNAME("kwargs"))).size(), 1);
		CHECK_EQ(descriptor->get(SNAME("builtin")), Variant(false));
	}

	SUBCASE("the built-in flag is carried from the source usage") {
		FoundryScript::AnnotationUsage builtin_usage;
		builtin_usage.name = SNAME("export_range");
		builtin_usage.qualified_name = SNAME("export_range");
		builtin_usage.args.push_back(0.0);
		builtin_usage.args.push_back(100.0);
		builtin_usage.is_builtin = true;

		Ref<FSAnnotation> builtin_descriptor = FSAnnotation::from_usage(builtin_usage);
		CHECK(builtin_descriptor.is_valid());
		if (builtin_descriptor.is_valid()) {
			CHECK(builtin_descriptor->is_builtin());
			CHECK_EQ(builtin_descriptor->get(SNAME("builtin")), Variant(true));
			CHECK_EQ(builtin_descriptor->get_arguments().size(), 2);
		}
	}

	SUBCASE("mutating returned snapshots leaves the descriptor and source metadata intact") {
		Array args = descriptor->get_arguments();
		args.push_back("injected");
		Dictionary kwargs = descriptor->get_named_arguments();
		kwargs[SNAME("evil")] = true;

		// The next read is unaffected by the previous mutation.
		CHECK_EQ(descriptor->get_arguments().size(), 2);
		CHECK_EQ(descriptor->get_named_arguments().size(), 1);

		// The originating usage metadata is untouched.
		CHECK_EQ(usage.args.size(), 2);
		CHECK_EQ(usage.kwargs.size(), 1);
	}
}

static Dictionary find_descriptor_by_name(const TypedArray<Dictionary> &p_descriptors, const StringName &p_name) {
	for (int i = 0; i < p_descriptors.size(); i++) {
		const Dictionary descriptor = p_descriptors[i];
		if (StringName(descriptor.get("name", StringName())) == p_name) {
			return descriptor;
		}
	}
	return Dictionary();
}

TEST_CASE("[Modules][FoundryScript] FSReflection exposes custom annotation metadata") {
	ScopedFSNativeGlobals native_globals;
	FSParser parser;
	Error err = parser.parse(R"(
namespace cafecito.reflect_cpp

annotation suite(name: String = "") targets CLASS
annotation test targets METHOD
annotation timeout(seconds: float) targets METHOD
annotation fixture targets VARIABLE
annotation tags(...names: String) targets METHOD
annotation repeatable(value: String) targets METHOD

@suite(name = "Base Suite")
class Base:
	@fixture
	var base_var: int = 0

	@test
	@timeout(2.0)
	func base_method() -> void:
		pass

	@timeout(7.0)
	func shared_method() -> void:
		pass

class Derived extends Base:
	@test
	func derived_method() -> void:
		pass

	@tags("override")
	func shared_method() -> void:
		pass

trait Mixin:
	@test
	@tags("trait")
	func mixin_method() -> void:
		pass

class Impl uses Mixin:
	@repeatable("a")
	@repeatable("b")
	func repeated() -> void:
		pass
)",
			"user://annotation_reflection_cpp.fs", false);

	CHECK_EQ(err, OK);
	if (err != OK) {
		return;
	}

	FSAnalyzer analyzer(&parser);
	err = analyzer.analyze();
	CHECK_EQ(err, OK);
	if (err != OK) {
		return;
	}

	FSCompiler compiler;
	Ref<FoundryScript> script;
	script.instantiate();
	script->set_path("user://annotation_reflection_cpp.fs");

	err = compiler.compile(&parser, script.ptr(), false);
	INFO(compiler.get_error());
	CHECK_EQ(err, OK);
	if (err != OK) {
		return;
	}

	const HashMap<StringName, Ref<FoundryScript>> &subclasses = script->get_subclasses();
	CHECK(subclasses.has(SNAME("Base")));
	CHECK(subclasses.has(SNAME("Derived")));
	CHECK(subclasses.has(SNAME("Impl")));
	if (!subclasses.has(SNAME("Base")) || !subclasses.has(SNAME("Derived")) || !subclasses.has(SNAME("Impl"))) {
		return;
	}
	Ref<FoundryScript> base = subclasses[SNAME("Base")];
	Ref<FoundryScript> derived = subclasses[SNAME("Derived")];
	Ref<FoundryScript> impl = subclasses[SNAME("Impl")];

	Ref<FSReflection> reflection;
	reflection.instantiate();

	SUBCASE("class annotations are direct-only") {
		TypedArray<FSAnnotation> base_class = reflection->get_class_annotations(base);
		CHECK_EQ(base_class.size(), 1);
		if (base_class.size() == 1) {
			Ref<FSAnnotation> suite = base_class[0];
			CHECK(suite.is_valid());
			if (suite.is_valid()) {
				CHECK_EQ(suite->get_annotation_name(), SNAME("suite"));
				CHECK_EQ(suite->get_qualified_name(), SNAME("cafecito.reflect_cpp.suite"));
				CHECK_EQ(String(suite->get_named_arguments()[SNAME("name")]), "Base Suite");
			}
		}

		// Derived defines no class annotations of its own and does not inherit them.
		CHECK(reflection->get_class_annotations(derived).is_empty());
	}

	SUBCASE("method annotations follow the effective view") {
		TypedArray<FSAnnotation> base_method = reflection->get_method_annotations(base, SNAME("base_method"));
		CHECK_EQ(base_method.size(), 2);
		if (base_method.size() == 2) {
			CHECK_EQ(Ref<FSAnnotation>(base_method[0])->get_annotation_name(), SNAME("test"));
			CHECK_EQ(Ref<FSAnnotation>(base_method[1])->get_annotation_name(), SNAME("timeout"));
		}

		// Base method annotations remain visible through the derived script.
		CHECK_EQ(reflection->get_method_annotations(derived, SNAME("base_method")).size(), 2);

		// A trait-flattened method carries its declaration annotations through the implementer.
		TypedArray<FSAnnotation> mixin_method = reflection->get_method_annotations(impl, SNAME("mixin_method"));
		CHECK_EQ(mixin_method.size(), 2);
		if (mixin_method.size() == 2) {
			CHECK_EQ(Ref<FSAnnotation>(mixin_method[0])->get_annotation_name(), SNAME("test"));
			CHECK_EQ(Ref<FSAnnotation>(mixin_method[1])->get_annotation_name(), SNAME("tags"));
		}
	}

	SUBCASE("variable annotations follow the effective view") {
		TypedArray<FSAnnotation> base_var = reflection->get_variable_annotations(base, SNAME("base_var"));
		CHECK_EQ(base_var.size(), 1);
		if (base_var.size() == 1) {
			CHECK_EQ(Ref<FSAnnotation>(base_var[0])->get_annotation_name(), SNAME("fixture"));
		}
		CHECK_EQ(reflection->get_variable_annotations(derived, SNAME("base_var")).size(), 1);
	}

	SUBCASE("the effective switch restricts results to the exact target script") {
		// Effective (the default) walks the base chain; direct (false) restricts to the leaf script.
		CHECK_EQ(reflection->get_method_annotations(derived, SNAME("base_method"), true).size(), 2);
		CHECK(reflection->get_method_annotations(derived, SNAME("base_method"), false).is_empty());
		CHECK_EQ(reflection->get_method_annotations(base, SNAME("base_method"), false).size(), 2);

		CHECK_EQ(reflection->get_variable_annotations(derived, SNAME("base_var"), true).size(), 1);
		CHECK(reflection->get_variable_annotations(derived, SNAME("base_var"), false).is_empty());
		CHECK_EQ(reflection->get_variable_annotations(base, SNAME("base_var"), false).size(), 1);

		// Trait-flattened members are owned by the implementer, so the direct view still sees them.
		CHECK_EQ(reflection->get_method_annotations(impl, SNAME("mixin_method"), false).size(), 2);

		// The generic accessors honor the flag too.
		CHECK_EQ(reflection->get_annotations(derived, SNAME("base_method"), SNAME("method"), true).size(), 2);
		CHECK(reflection->get_annotations(derived, SNAME("base_method"), SNAME("method"), false).is_empty());
		CHECK(reflection->has_annotation(derived, SNAME("base_method"), SNAME("test"), SNAME("method"), true));
		CHECK_FALSE(reflection->has_annotation(derived, SNAME("base_method"), SNAME("test"), SNAME("method"), false));
		CHECK(reflection->get_annotation(derived, SNAME("base_method"), SNAME("test"), SNAME("method"), false).is_null());
		CHECK(reflection->get_annotation(derived, SNAME("base_method"), SNAME("test"), SNAME("method"), true).is_valid());

		// Class annotations stay direct-only regardless of the flag.
		CHECK_EQ(reflection->get_annotations(base, SNAME(""), SNAME("class"), false).size(), 1);
		CHECK_EQ(reflection->get_annotations(base, SNAME(""), SNAME("class"), true).size(), 1);
	}

	SUBCASE("repeated annotations are preserved in source order") {
		TypedArray<FSAnnotation> repeated = reflection->get_method_annotations(impl, SNAME("repeated"));
		CHECK_EQ(repeated.size(), 2);
		if (repeated.size() == 2) {
			CHECK_EQ(String(Ref<FSAnnotation>(repeated[0])->get_arguments()[0]), "a");
			CHECK_EQ(String(Ref<FSAnnotation>(repeated[1])->get_arguments()[0]), "b");
		}
	}

	SUBCASE("has_annotation and get_annotation match short and qualified names") {
		CHECK(reflection->has_annotation(base, SNAME("base_method"), SNAME("test"), SNAME("method")));
		CHECK(reflection->has_annotation(base, SNAME("base_method"), SNAME("cafecito.reflect_cpp.timeout"), SNAME("method")));
		CHECK_FALSE(reflection->has_annotation(base, SNAME("base_method"), SNAME("missing"), SNAME("method")));
		CHECK(reflection->has_annotation(base, SNAME(""), SNAME("suite"), SNAME("class")));
		CHECK(reflection->has_annotation(base, SNAME("base_var"), SNAME("fixture"), SNAME("variable")));

		Ref<FSAnnotation> timeout = reflection->get_annotation(base, SNAME("base_method"), SNAME("timeout"), SNAME("method"));
		CHECK(timeout.is_valid());
		if (timeout.is_valid()) {
			CHECK_EQ(double(timeout->get_arguments()[0]), doctest::Approx(2.0));
		}
		CHECK(reflection->get_annotation(base, SNAME("base_method"), SNAME("missing"), SNAME("method")).is_null());
	}

	SUBCASE("generic get_annotations dispatches on kind") {
		CHECK_EQ(reflection->get_annotations(base, SNAME(""), SNAME("class")).size(), 1);
		CHECK_EQ(reflection->get_annotations(base, SNAME("base_method"), SNAME("method")).size(), 2);
		CHECK_EQ(reflection->get_annotations(base, SNAME("base_var"), SNAME("variable")).size(), 1);
	}

	SUBCASE("descriptors embed annotations in method and property dictionaries") {
		const Dictionary method_descriptor = find_descriptor_by_name(reflection->get_methods(base), SNAME("base_method"));
		CHECK(method_descriptor.has("annotations"));
		CHECK_EQ(Array(method_descriptor["annotations"]).size(), 2);

		const Dictionary method_info = reflection->get_method_info(base, SNAME("base_method"));
		CHECK(method_info.has("annotations"));
		CHECK_EQ(Array(method_info["annotations"]).size(), 2);

		const Dictionary property_descriptor = find_descriptor_by_name(reflection->get_properties(base), SNAME("base_var"));
		CHECK(property_descriptor.has("annotations"));
		CHECK_EQ(Array(property_descriptor["annotations"]).size(), 1);
	}

	SUBCASE("an override and the base method it shadows keep their own embedded annotations") {
		// Effective method annotations resolve to the override.
		TypedArray<FSAnnotation> effective = reflection->get_method_annotations(derived, SNAME("shared_method"));
		CHECK_EQ(effective.size(), 1);
		if (effective.size() == 1) {
			CHECK_EQ(Ref<FSAnnotation>(effective[0])->get_annotation_name(), SNAME("tags"));
		}

		// get_methods(Derived) lists both shared_method declarations (override + base), each carrying
		// the annotations of the declaration it represents rather than the leaf's effective set.
		TypedArray<Dictionary> methods = reflection->get_methods(derived);
		int shared_entries = 0;
		bool saw_override_tags = false;
		bool saw_base_timeout = false;
		for (int i = 0; i < methods.size(); i++) {
			const Dictionary descriptor = methods[i];
			if (StringName(descriptor.get("name", StringName())) != SNAME("shared_method")) {
				continue;
			}
			shared_entries++;
			const TypedArray<FSAnnotation> annotations = descriptor["annotations"];
			if (annotations.size() == 1) {
				const StringName annotation_name = Ref<FSAnnotation>(annotations[0])->get_annotation_name();
				saw_override_tags = saw_override_tags || annotation_name == SNAME("tags");
				saw_base_timeout = saw_base_timeout || annotation_name == SNAME("timeout");
			}
		}
		CHECK_EQ(shared_entries, 2);
		CHECK(saw_override_tags);
		CHECK(saw_base_timeout);
	}

	SUBCASE("invalid, non-script, and freed targets return empty results without crashing") {
		CHECK(reflection->get_class_annotations(Variant(42)).is_empty());
		CHECK(reflection->get_method_annotations(Variant(), SNAME("base_method")).is_empty());
		CHECK(reflection->get_variable_annotations(Variant("not a script"), SNAME("base_var")).is_empty());
		CHECK(reflection->get_method_parameter_annotations(Variant(), SNAME("base_method"), SNAME("unused")).is_empty());
		CHECK(reflection->get_signal_parameter_annotations(Variant(42), SNAME("missing"), SNAME("amount")).is_empty());
		CHECK_FALSE(reflection->has_annotation(Variant(42), SNAME("base_method"), SNAME("test"), SNAME("method")));
		CHECK(reflection->get_annotation(Variant(42), SNAME("base_method"), SNAME("test"), SNAME("method")).is_null());

		Object *freed = memnew(Object);
		Variant freed_target(freed);
		memdelete(freed);
		CHECK(reflection->get_class_annotations(freed_target).is_empty());
		CHECK_FALSE(reflection->has_annotation(freed_target, SNAME("base_method"), SNAME("test"), SNAME("method")));
		CHECK(reflection->get_annotation(freed_target, SNAME("base_method"), SNAME("test"), SNAME("method")).is_null());
	}
}

TEST_CASE("[Modules][FoundryScript] FSReflection exposes parameter annotation metadata") {
	ScopedFSNativeGlobals native_globals;
	FSParser parser;
	Error err = parser.parse(R"(
namespace cafecito.reflect_parameter_cpp

annotation inject targets PARAMETER
annotation range(min: float, max: float) targets PARAMETER

class Base:
	func spawn(@inject factory: String, @range(0.0, 10.0) threat: float) -> void:
		pass

	signal damaged(@range(0.0, 999.0) amount: float)

class Derived extends Base:
	func spawn(@inject override_factory: String, @range(1.0, 5.0) threat: float) -> void:
		pass

trait Mixin:
	func mixin_call(@inject helper: String) -> void:
		pass

class Impl uses Mixin:
	pass
)",
			"user://annotation_reflection_parameter_cpp.fs", false);

	CHECK_EQ(err, OK);
	if (err != OK) {
		return;
	}

	FSAnalyzer analyzer(&parser);
	err = analyzer.analyze();
	CHECK_EQ(err, OK);
	if (err != OK) {
		return;
	}

	FSCompiler compiler;
	Ref<FoundryScript> script;
	script.instantiate();
	script->set_path("user://annotation_reflection_parameter_cpp.fs");

	err = compiler.compile(&parser, script.ptr(), false);
	INFO(compiler.get_error());
	CHECK_EQ(err, OK);
	if (err != OK) {
		return;
	}

	const HashMap<StringName, Ref<FoundryScript>> &subclasses = script->get_subclasses();
	CHECK(subclasses.has(SNAME("Base")));
	CHECK(subclasses.has(SNAME("Derived")));
	CHECK(subclasses.has(SNAME("Impl")));
	if (!subclasses.has(SNAME("Base")) || !subclasses.has(SNAME("Derived")) || !subclasses.has(SNAME("Impl"))) {
		return;
	}
	Ref<FoundryScript> base = subclasses[SNAME("Base")];
	Ref<FoundryScript> derived = subclasses[SNAME("Derived")];
	Ref<FoundryScript> impl = subclasses[SNAME("Impl")];

	Ref<FSReflection> reflection;
	reflection.instantiate();

	SUBCASE("method parameter annotations resolve by owner and parameter name") {
		TypedArray<FSAnnotation> factory = reflection->get_method_parameter_annotations(base, SNAME("spawn"), SNAME("factory"));
		CHECK_EQ(factory.size(), 1);
		if (factory.size() == 1) {
			CHECK_EQ(Ref<FSAnnotation>(factory[0])->get_annotation_name(), SNAME("inject"));
		}

		TypedArray<FSAnnotation> threat = reflection->get_method_parameter_annotations(base, SNAME("spawn"), SNAME("threat"));
		CHECK_EQ(threat.size(), 1);
		if (threat.size() == 1) {
			CHECK_EQ(double(Ref<FSAnnotation>(threat[0])->get_arguments()[0]), doctest::Approx(0.0));
			CHECK_EQ(double(Ref<FSAnnotation>(threat[0])->get_arguments()[1]), doctest::Approx(10.0));
		}
	}

	SUBCASE("signal parameter annotations resolve by owner and parameter name") {
		TypedArray<FSAnnotation> amount = reflection->get_signal_parameter_annotations(base, SNAME("damaged"), SNAME("amount"));
		CHECK_EQ(amount.size(), 1);
		if (amount.size() == 1) {
			CHECK_EQ(double(Ref<FSAnnotation>(amount[0])->get_arguments()[0]), doctest::Approx(0.0));
			CHECK_EQ(double(Ref<FSAnnotation>(amount[0])->get_arguments()[1]), doctest::Approx(999.0));
		}
	}

	SUBCASE("parameter annotations follow the effective owner declaration") {
		// Derived inherits spawn without reusing the base parameter names, so the override
		// replaces the base parameter annotation view entirely.
		CHECK(reflection->get_method_parameter_annotations(derived, SNAME("spawn"), SNAME("factory"), true).is_empty());
		CHECK(reflection->get_method_parameter_annotations(derived, SNAME("spawn"), SNAME("factory"), false).is_empty());
		CHECK_EQ(reflection->get_method_parameter_annotations(derived, SNAME("spawn"), SNAME("override_factory"), false).size(), 1);
		CHECK_EQ(reflection->get_method_parameter_annotations(base, SNAME("spawn"), SNAME("factory"), false).size(), 1);
		CHECK_EQ(reflection->get_method_parameter_annotations(impl, SNAME("mixin_call"), SNAME("helper")).size(), 1);
	}

	SUBCASE("method descriptors embed parameter annotations in argument dictionaries") {
		Ref<FSMethodDescriptor> descriptor = reflection->get_method_descriptor(base, SNAME("spawn"));
		CHECK(descriptor.is_valid());
		if (!descriptor.is_valid()) {
			return;
		}
		TypedArray<Dictionary> arguments = descriptor->get_arguments();
		CHECK_EQ(arguments.size(), 2);
		if (arguments.size() == 2) {
			const Dictionary factory_arg = arguments[0];
			const Dictionary threat_arg = arguments[1];
			CHECK(factory_arg.has("annotations"));
			CHECK_EQ(Array(factory_arg["annotations"]).size(), 1);
			CHECK(threat_arg.has("annotations"));
			CHECK_EQ(Array(threat_arg["annotations"]).size(), 1);
		}
	}

	SUBCASE("invalid targets and unknown parameters return empty arrays") {
		CHECK(reflection->get_method_parameter_annotations(base, SNAME("missing"), SNAME("factory")).is_empty());
		CHECK(reflection->get_method_parameter_annotations(base, SNAME("spawn"), SNAME("missing")).is_empty());
		CHECK(reflection->get_method_parameter_annotations(Variant(42), SNAME("spawn"), SNAME("factory")).is_empty());
	}
}
} // namespace FSTests
