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
	// Comment interleaving consumes `comments`; it is wired in a later task.
	(void)comments;
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
		print_class_header(p_class, p_is_tool);
		for (const GDScriptParser::AnnotationDeclarationNode *declaration : p_class->annotation_declarations) {
			print_annotation_declaration(declaration);
		}
		print_class_body(p_class);
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
	indent_level++;
	print_class_body(p_class);
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

void GDScriptPrinter::print_class_body(const GDScriptParser::ClassNode *p_class) {
	for (int i = 0; i < p_class->members.size(); i++) {
		print_member(p_class->members[i]);
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
	indent_level++;
	print_suite(p_function->body);
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
	indent_level++;
	if (p_variable->property == GDScriptParser::VariableNode::PROP_INLINE) {
		if (p_variable->getter != nullptr) {
			write_indent();
			write("get:");
			newline();
			indent_level++;
			print_suite(p_variable->getter->body);
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
			indent_level++;
			print_suite(p_variable->setter->body);
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
		print_statement(p_suite->statements[i]);
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
	last_emitted_line = p_statement->end_line;
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
	indent_level++;
	for (int i = 0; i < p_match->branches.size(); i++) {
		print_match_branch(p_match->branches[i]);
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
		write(found->value.source); // Exact original text (quotes / number form).
		return;
	}
	// Fallback for synthesized literals without a backing token.
	write(p_literal->value.operator String());
}

void GDScriptPrinter::print_binary_op(const GDScriptParser::BinaryOpNode *p_op) {
	// Operand grouping (precedence-driven parentheses) is deferred to a later
	// task; the parse tree already encodes precedence and associativity.
	print_expression(p_op->left_operand);
	write(" ");
	write(binary_operator_text(p_op->operation));
	write(" ");
	print_expression(p_op->right_operand);
}

void GDScriptPrinter::print_unary_op(const GDScriptParser::UnaryOpNode *p_op) {
	const String operator_text = unary_operator_text(p_op->operation);
	write(operator_text);
	if (p_op->operation == GDScriptParser::UnaryOpNode::OP_LOGIC_NOT) {
		write(" ");
	}
	print_expression(p_op->operand);
}

void GDScriptPrinter::print_ternary_op(const GDScriptParser::TernaryOpNode *p_op) {
	print_expression(p_op->true_expr);
	write(" if ");
	print_expression(p_op->condition);
	write(" else ");
	print_expression(p_op->false_expr);
}

void GDScriptPrinter::print_call(const GDScriptParser::CallNode *p_call) {
	if (p_call->is_super) {
		write("super");
		if (p_call->callee != nullptr) {
			write(".");
		}
	}
	if (p_call->callee != nullptr) {
		print_expression(p_call->callee);
	} else if (!String(p_call->function_name).is_empty()) {
		write(p_call->function_name);
	}
	write("(");
	for (int i = 0; i < p_call->arguments.size(); i++) {
		if (i > 0) {
			write(", ");
		}
		if (i < p_call->argument_names.size() && !String(p_call->argument_names[i]).is_empty()) {
			write(p_call->argument_names[i]);
			write(" = ");
		}
		print_expression(p_call->arguments[i]);
	}
	write(")");
}

void GDScriptPrinter::print_subscript(const GDScriptParser::SubscriptNode *p_subscript) {
	print_expression(p_subscript->base);
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
	print_expression(p_cast->operand);
	write(" as ");
	print_type(p_cast->cast_type);
}

void GDScriptPrinter::print_await(const GDScriptParser::AwaitNode *p_await) {
	write("await ");
	print_expression(p_await->to_await);
}

void GDScriptPrinter::print_array(const GDScriptParser::ArrayNode *p_array) {
	write("[");
	for (int i = 0; i < p_array->elements.size(); i++) {
		if (i > 0) {
			write(", ");
		}
		print_expression(p_array->elements[i]);
	}
	write("]");
}

void GDScriptPrinter::print_dictionary(const GDScriptParser::DictionaryNode *p_dictionary) {
	if (p_dictionary->elements.is_empty()) {
		write("{}");
		return;
	}
	write("{");
	for (int i = 0; i < p_dictionary->elements.size(); i++) {
		if (i > 0) {
			write(", ");
		}
		const GDScriptParser::DictionaryNode::Pair &pair = p_dictionary->elements[i];
		if (p_dictionary->style == GDScriptParser::DictionaryNode::LUA_TABLE) {
			print_expression(pair.key);
			write(" = ");
			print_expression(pair.value);
		} else {
			print_expression(pair.key);
			write(": ");
			print_expression(pair.value);
		}
	}
	write("}");
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
	indent_level++;
	print_suite(function->body);
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
	print_expression(p_test->operand);
	write(" is ");
	print_type(p_test->test_type);
}

#endif // TOOLS_ENABLED
