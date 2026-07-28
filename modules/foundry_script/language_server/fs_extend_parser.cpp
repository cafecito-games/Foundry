/**************************************************************************/
/*  fs_extend_parser.cpp                                                  */
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

#include "fs_extend_parser.h"

#include "../editor/fs_docgen.h"
#include "../foundry_script.h"
#include "../fs_analyzer.h"
#include "../fs_position.h"
#include "fs_language_protocol.h"
#include "fs_workspace.h"

#include "core/io/file_access.h"

LSP::Position FoundryPosition::to_lsp(const Vector<String> &p_lines) const {
	LSP::Position res;

	// Special case: `line = 0` -> root class (range covers everything).
	if (line <= 0) {
		return res;
	}
	// Special case: `line = p_lines.size() + 1` -> root class (range covers everything).
	if (line >= p_lines.size() + 1) {
		res.line = p_lines.size();
		return res;
	}
	res.line = line - 1;

	// Special case: `column = 0` -> Starts at beginning of line.
	if (column <= 0) {
		return res;
	}

	res.character = FSTextPosition::godot_column_to_text_column(p_lines[res.line], column);

	return res;
}

FoundryPosition FoundryPosition::from_lsp(const LSP::Position p_pos, const Vector<String> &p_lines) {
	FoundryPosition res(p_pos.line + 1, p_pos.character + 1);

	// Line outside of actual text is valid (-> pos/cursor at end of text).
	if (res.line > p_lines.size()) {
		return res;
	}

	res.column = FSTextPosition::text_column_to_godot_column(p_lines[p_pos.line], p_pos.character);

	return res;
}

LSP::Range FoundryRange::to_lsp(const Vector<String> &p_lines) const {
	LSP::Range res;
	res.start = start.to_lsp(p_lines);
	res.end = end.to_lsp(p_lines);
	return res;
}

FoundryRange FoundryRange::from_lsp(const LSP::Range &p_range, const Vector<String> &p_lines) {
	FoundryPosition start = FoundryPosition::from_lsp(p_range.start, p_lines);
	FoundryPosition end = FoundryPosition::from_lsp(p_range.end, p_lines);
	return FoundryRange(start, end);
}

void ExtendFSParser::update_diagnostics() {
	diagnostics.clear();

	const List<ParserError> &parser_errors = get_errors();
	for (const ParserError &error : parser_errors) {
		LSP::Diagnostic diagnostic;
		diagnostic.severity = LSP::DiagnosticSeverity::Error;
		diagnostic.message = error.message;
		diagnostic.source = "foundry_script";
		diagnostic.code = -1;
		LSP::Range range;
		LSP::Position pos;
		const PackedStringArray line_array = get_lines();
		int line = CLAMP(LINE_NUMBER_TO_INDEX(error.line), 0, line_array.size() - 1);
		const String &line_text = line_array[line];
		pos.line = line;
		pos.character = line_text.length() - line_text.strip_edges(true, false).length();
		range.start = pos;
		range.end = range.start;
		range.end.character = line_text.strip_edges(false).length();
		diagnostic.range = range;
		diagnostics.push_back(diagnostic);
	}

	const List<FSWarning> &parser_warnings = get_warnings();
	for (const FSWarning &warning : parser_warnings) {
		LSP::Diagnostic diagnostic;
		diagnostic.severity = LSP::DiagnosticSeverity::Warning;
		diagnostic.message = "(" + warning.get_name() + "): " + warning.get_message();
		diagnostic.source = "foundry_script";
		diagnostic.code = warning.code;
		LSP::Range range;
		LSP::Position pos;
		int line = LINE_NUMBER_TO_INDEX(warning.start_line);
		const String &line_text = get_lines()[line];
		pos.line = line;
		pos.character = line_text.length() - line_text.strip_edges(true, false).length();
		range.start = pos;
		range.end = pos;
		range.end.character = line_text.strip_edges(false).length();
		diagnostic.range = range;
		diagnostics.push_back(diagnostic);
	}
}

void ExtendFSParser::update_symbols() {
	members.clear();

	if (const FSParser::ClassNode *gdclass = dynamic_cast<const FSParser::ClassNode *>(get_tree())) {
		parse_class_symbol(gdclass, class_symbol);

		for (int i = 0; i < class_symbol.children.size(); i++) {
			const LSP::DocumentSymbol &symbol = class_symbol.children[i];
			// Annotation declarations get a symbol for go-to-definition, but they are not class
			// members and must not surface in member completion or `self.`-style member lookup. They
			// are the only children emitted with `Operator` kind, so they can be skipped reliably
			// without colliding with a method that happens to share a source line.
			if (symbol.kind == LSP::SymbolKind::Operator) {
				continue;
			}
			members.insert(symbol.name, &symbol);

			// Cache level one inner classes and traits.
			if (symbol.kind == LSP::SymbolKind::Class || symbol.kind == LSP::SymbolKind::Interface) {
				ClassMembers inner_class;
				for (int j = 0; j < symbol.children.size(); j++) {
					const LSP::DocumentSymbol &s = symbol.children[j];
					inner_class.insert(s.name, &s);
				}
				inner_classes.insert(symbol.name, inner_class);
			}
		}
	}
}

void ExtendFSParser::update_document_links(const String &p_code) {
	document_links.clear();

	FSTokenizerText scr_tokenizer;
	Ref<FileAccess> fs = FileAccess::create(FileAccess::ACCESS_RESOURCES);
	scr_tokenizer.set_source_code(p_code);
	while (true) {
		FSTokenizer::Token token = scr_tokenizer.scan();
		if (token.type == FSTokenizer::Token::TK_EOF) {
			break;
		} else if (token.type == FSTokenizer::Token::LITERAL) {
			const Variant &const_val = token.literal;
			if (const_val.get_type() == Variant::STRING) {
				String scr_path = const_val;
				if (scr_path.is_relative_path()) {
					scr_path = get_path().get_base_dir().path_join(scr_path).simplify_path();
				}
				bool exists = fs->file_exists(scr_path);

				if (exists) {
					String value = const_val;
					LSP::DocumentLink link;
					link.target = FSLanguageProtocol::get_singleton()->get_workspace()->get_file_uri(scr_path);
					link.range = FoundryRange(FoundryPosition(token.start_line, token.start_column), FoundryPosition(token.end_line, token.end_column)).to_lsp(lines);
					document_links.push_back(link);
				}
			}
		}
	}
}

LSP::Range ExtendFSParser::range_of_node(const FSParser::Node *p_node) const {
	FoundryPosition start(p_node->start_line, p_node->start_column);
	FoundryPosition end(p_node->end_line, p_node->end_column);
	return FoundryRange(start, end).to_lsp(lines);
}

String ExtendFSParser::enum_case_detail(const FSParser::EnumNode::Value &p_value) {
	const String name = p_value.identifier != nullptr ? String(p_value.identifier->name) : String();

	if (p_value.parent_enum == nullptr || !p_value.parent_enum->is_tagged_union) {
		return name + " = " + itos(p_value.value);
	}

	// A tagged-union tag is ordinal by declaration order rather than something the source spells,
	// so a case is described by its declared shape: its payload signature, or just its name.
	if (!p_value.has_payload()) {
		return name;
	}

	String detail = name + "(";
	for (int i = 0; i < p_value.payload_fields.size(); i++) {
		if (i > 0) {
			detail += ", ";
		}
		const FSParser::EnumNode::PayloadField &field = p_value.payload_fields[i];
		if (field.identifier != nullptr) {
			detail += String(field.identifier->name);
		}
		if (field.type != nullptr) {
			const FSParser::DataType field_type = FSAnalyzer::type_from_metatype(field.type->get_datatype());
			if (field_type.is_hard_type()) {
				detail += ": " + field_type.to_string();
			}
		}
	}
	return detail + ")";
}

void ExtendFSParser::append_enum_symbol_children(const FSParser::EnumNode *p_enum, LSP::DocumentSymbol &r_symbol) {
	const String uri = get_uri();

	for (const FSParser::EnumNode::Value &value : p_enum->values) {
		LSP::DocumentSymbol child;

		child.name = value.identifier->name;
		child.kind = LSP::SymbolKind::EnumMember;
		child.deprecated = false;
		child.range.start = FoundryPosition(value.line, value.start_column).to_lsp(lines);
		child.range.end = FoundryPosition(value.end_line, value.end_column).to_lsp(lines);
		child.selectionRange = range_of_node(value.identifier);
		child.documentation = value.doc_data.description;
		child.uri = uri;
		child.script_path = path;
		child.detail = enum_case_detail(value);

		r_symbol.children.push_back(child);
	}

	for (const FSParser::FunctionNode *function : p_enum->functions) {
		LSP::DocumentSymbol child;
		parse_function_symbol(function, child);
		r_symbol.children.push_back(child);
	}
}

void ExtendFSParser::append_tuple_symbol_fields(const FSParser::TupleNode *p_tuple, LSP::DocumentSymbol &r_symbol) {
	const String uri = get_uri();

	for (int i = 0; i < p_tuple->fields.size(); i++) {
		const FSParser::TupleNode::Field &field = p_tuple->fields[i];
		if (field.identifier == nullptr) {
			// A positional field has no name to report as its own symbol.
			continue;
		}
		LSP::DocumentSymbol field_symbol;
		field_symbol.name = field.identifier->name;
		field_symbol.kind = LSP::SymbolKind::Field;
		field_symbol.deprecated = false;
		field_symbol.range.start = FoundryPosition(field.line, field.start_column).to_lsp(lines);
		field_symbol.range.end = FoundryPosition(field.line, field.end_column).to_lsp(lines);
		field_symbol.selectionRange = range_of_node(field.identifier);
		field_symbol.uri = uri;
		field_symbol.script_path = path;
		r_symbol.children.push_back(field_symbol);
	}
}

void ExtendFSParser::parse_class_symbol(const FSParser::ClassNode *p_class, LSP::DocumentSymbol &r_symbol) {
	const String uri = get_uri();

	r_symbol.uri = uri;
	r_symbol.script_path = path;
	r_symbol.children.clear();
	r_symbol.name = p_class->identifier != nullptr ? String(p_class->identifier->name) : String();
	if (r_symbol.name.is_empty()) {
		r_symbol.name = path.get_file();
	}
	// Traits have no dedicated LSP symbol kind; Interface is the closest analog.
	r_symbol.kind = p_class->is_trait ? LSP::SymbolKind::Interface : LSP::SymbolKind::Class;
	r_symbol.deprecated = false;
	r_symbol.range = range_of_node(p_class);
	if (p_class->identifier) {
		r_symbol.selectionRange = range_of_node(p_class->identifier);
	} else {
		// No meaningful `selectionRange`, but we must ensure that it is inside of `range`.
		r_symbol.selectionRange.start = r_symbol.range.start;
		r_symbol.selectionRange.end = r_symbol.range.start;
	}
	r_symbol.detail = (p_class->is_trait ? "trait " : "class ") + r_symbol.name;
	{
		String doc = p_class->doc_data.brief;
		if (!p_class->doc_data.description.is_empty()) {
			doc += "\n\n" + p_class->doc_data.description;
		}

		if (!p_class->doc_data.tutorials.is_empty()) {
			doc += "\n";
			for (const Pair<String, String> &tutorial : p_class->doc_data.tutorials) {
				if (tutorial.first.is_empty()) {
					doc += vformat("\n@tutorial: %s", tutorial.second);
				} else {
					doc += vformat("\n@tutorial(%s): %s", tutorial.first, tutorial.second);
				}
			}
		}
		r_symbol.documentation = doc;
	}

	if (p_class->is_enum_file && p_class->enum_file_decl != nullptr) {
		const FSParser::EnumNode *enum_node = p_class->enum_file_decl;
		const StringName global_name = p_class->get_global_name();
		if (global_name != StringName()) {
			r_symbol.name = global_name;
		}
		r_symbol.kind = LSP::SymbolKind::Enum;
		r_symbol.range = range_of_node(enum_node);
		if (enum_node->identifier != nullptr) {
			r_symbol.selectionRange = range_of_node(enum_node->identifier);
		}
		r_symbol.detail = "enum " + r_symbol.name;

		append_enum_symbol_children(enum_node, r_symbol);
		return;
	}

	if (p_class->is_tuple_file && p_class->tuple_file_decl != nullptr) {
		const FSParser::TupleNode *tuple_node = p_class->tuple_file_decl;
		const StringName global_name = p_class->get_global_name();
		if (global_name != StringName()) {
			r_symbol.name = global_name;
		}
		r_symbol.kind = LSP::SymbolKind::Struct;
		r_symbol.range = range_of_node(tuple_node);
		if (tuple_node->identifier != nullptr) {
			r_symbol.selectionRange = range_of_node(tuple_node->identifier);
		}
		r_symbol.detail = "tuple " + r_symbol.name;

		append_tuple_symbol_fields(tuple_node, r_symbol);
		return;
	}

	for (int i = 0; i < p_class->members.size(); i++) {
		const ClassNode::Member &m = p_class->members[i];

		switch (m.type) {
			case ClassNode::Member::VARIABLE: {
				LSP::DocumentSymbol symbol;
				symbol.name = m.variable->identifier->name;
				symbol.kind = m.variable->property == VariableNode::PROP_NONE ? LSP::SymbolKind::Variable : LSP::SymbolKind::Property;
				symbol.deprecated = false;
				symbol.range = range_of_node(m.variable);
				symbol.selectionRange = range_of_node(m.variable->identifier);
				if (m.variable->exported) {
					symbol.detail += "@export ";
				}
				symbol.detail += "var " + m.variable->identifier->name;
				if (m.get_datatype().is_hard_type()) {
					symbol.detail += ": " + m.get_datatype().to_string();
				}
				if (m.variable->initializer != nullptr) {
					String value_text;
					if (m.variable->initializer->is_constant) {
						// Render the constant through the shared doc-value formatter rather than
						// JSON. JSON cannot represent non-finite floats, so a valid FoundryScript
						// constant like `const X = NAN` would otherwise emit a spurious engine
						// warning and render as a value-losing `null`; the doc formatter prints
						// `nan`/`inf` and keeps enum values rendered by name.
						value_text = FSDocGen::docvalue_from_expression(m.variable->initializer, m.get_datatype());
					} else if (m.variable->initializer->type == FSParser::Node::IDENTIFIER &&
							m.variable->initializer->get_datatype().is_meta_type) {
						value_text = static_cast<const FSParser::IdentifierNode *>(m.variable->initializer)->name;
					}
					if (!value_text.is_empty()) {
						symbol.detail += " = " + value_text;
					}
				}

				symbol.documentation = m.variable->doc_data.description;
				symbol.uri = uri;
				symbol.script_path = path;

				if (m.variable->initializer && m.variable->initializer->type == FSParser::Node::LAMBDA) {
					FSParser::LambdaNode *lambda_node = (FSParser::LambdaNode *)m.variable->initializer;
					LSP::DocumentSymbol lambda;
					parse_function_symbol(lambda_node->function, lambda);
					// Merge lambda into current variable.
					symbol.children.append_array(lambda.children);
				}

				if (m.variable->getter && m.variable->getter->type == FSParser::Node::FUNCTION) {
					LSP::DocumentSymbol get_symbol;
					parse_function_symbol(m.variable->getter, get_symbol);
					get_symbol.local = true;
					symbol.children.push_back(get_symbol);
				}
				if (m.variable->setter && m.variable->setter->type == FSParser::Node::FUNCTION) {
					LSP::DocumentSymbol set_symbol;
					parse_function_symbol(m.variable->setter, set_symbol);
					set_symbol.local = true;
					symbol.children.push_back(set_symbol);
				}

				r_symbol.children.push_back(symbol);
			} break;
			case ClassNode::Member::CONSTANT: {
				LSP::DocumentSymbol symbol;

				symbol.name = m.constant->identifier->name;
				symbol.kind = LSP::SymbolKind::Constant;
				symbol.deprecated = false;
				symbol.range = range_of_node(m.constant);
				symbol.selectionRange = range_of_node(m.constant->identifier);
				symbol.documentation = m.constant->doc_data.description;
				symbol.uri = uri;
				symbol.script_path = path;

				symbol.detail = "const " + symbol.name;
				if (m.constant->get_datatype().is_hard_type()) {
					symbol.detail += ": " + m.constant->get_datatype().to_string();
				}

				const Variant &default_value = m.constant->initializer->reduced_value;
				String value_text;
				if (default_value.get_type() == Variant::OBJECT) {
					Ref<Resource> res = default_value;
					if (res.is_valid() && !res->get_path().is_empty()) {
						value_text = "preload(\"" + res->get_path() + "\")";
						if (symbol.documentation.is_empty()) {
							ExtendFSParser *parser = FSLanguageProtocol::get_singleton()->get_parse_result(res->get_path());
							if (parser) {
								symbol.documentation = parser->class_symbol.documentation;
							}
						}
					} else {
						value_text = default_value.to_json_string();
					}
				} else {
					value_text = default_value.to_json_string();
				}
				if (!value_text.is_empty()) {
					symbol.detail += " = " + value_text;
				}

				r_symbol.children.push_back(symbol);
			} break;
			case ClassNode::Member::SIGNAL: {
				LSP::DocumentSymbol symbol;
				symbol.name = m.signal->identifier->name;
				symbol.kind = LSP::SymbolKind::Event;
				symbol.deprecated = false;
				symbol.range = range_of_node(m.signal);
				symbol.selectionRange = range_of_node(m.signal->identifier);
				symbol.documentation = m.signal->doc_data.description;
				symbol.uri = uri;
				symbol.script_path = path;
				symbol.detail = "signal " + String(m.signal->identifier->name) + "(";
				for (int j = 0; j < m.signal->parameters.size(); j++) {
					if (j > 0) {
						symbol.detail += ", ";
					}
					const FSParser::ParameterNode *parameter = m.signal->parameters[j];
					symbol.detail += String(parameter->identifier->name);
					if (parameter->get_datatype().is_hard_type()) {
						symbol.detail += ": " + parameter->get_datatype().to_string();
					}
				}
				symbol.detail += ")";

				for (FSParser::ParameterNode *param : m.signal->parameters) {
					LSP::DocumentSymbol param_symbol;
					param_symbol.name = param->identifier->name;
					param_symbol.kind = LSP::SymbolKind::Variable;
					param_symbol.deprecated = false;
					param_symbol.local = true;
					param_symbol.range = range_of_node(param);
					param_symbol.selectionRange = range_of_node(param->identifier);
					param_symbol.uri = uri;
					param_symbol.script_path = path;
					param_symbol.detail = "var " + param_symbol.name;
					if (param->get_datatype().is_hard_type()) {
						param_symbol.detail += ": " + param->get_datatype().to_string();
					}
					symbol.children.push_back(param_symbol);
				}
				r_symbol.children.push_back(symbol);
			} break;
			case ClassNode::Member::ENUM_VALUE: {
				LSP::DocumentSymbol symbol;

				symbol.name = m.enum_value.identifier->name;
				symbol.kind = LSP::SymbolKind::EnumMember;
				symbol.deprecated = false;
				symbol.range.start = FoundryPosition(m.enum_value.line, m.enum_value.start_column).to_lsp(lines);
				symbol.range.end = FoundryPosition(m.enum_value.end_line, m.enum_value.end_column).to_lsp(lines);
				symbol.selectionRange = range_of_node(m.enum_value.identifier);
				symbol.documentation = m.enum_value.doc_data.description;
				symbol.uri = uri;
				symbol.script_path = path;

				symbol.detail = enum_case_detail(m.enum_value);

				r_symbol.children.push_back(symbol);
			} break;
			case ClassNode::Member::ENUM: {
				LSP::DocumentSymbol symbol;
				symbol.name = m.m_enum->identifier->name;
				symbol.kind = LSP::SymbolKind::Enum;
				symbol.range = range_of_node(m.m_enum);
				symbol.selectionRange = range_of_node(m.m_enum->identifier);
				symbol.documentation = m.m_enum->doc_data.description;
				symbol.uri = uri;
				symbol.script_path = path;

				symbol.detail = "enum " + String(m.m_enum->identifier->name) + ":";
				for (int j = 0; j < m.m_enum->values.size(); j++) {
					symbol.detail += "\n\t" + enum_case_detail(m.m_enum->values[j]);
				}

				append_enum_symbol_children(m.m_enum, symbol);

				r_symbol.children.push_back(symbol);
			} break;
			case ClassNode::Member::FUNCTION: {
				LSP::DocumentSymbol symbol;
				parse_function_symbol(m.function, symbol, p_class);
				r_symbol.children.push_back(symbol);
			} break;
			case ClassNode::Member::CLASS: {
				LSP::DocumentSymbol symbol;
				parse_class_symbol(m.m_class, symbol);
				r_symbol.children.push_back(symbol);
			} break;
			case ClassNode::Member::TUPLE: {
				LSP::DocumentSymbol symbol;
				symbol.name = m.m_tuple->identifier != nullptr ? String(m.m_tuple->identifier->name) : String();
				symbol.kind = LSP::SymbolKind::Struct;
				symbol.range = range_of_node(m.m_tuple);
				if (m.m_tuple->identifier != nullptr) {
					symbol.selectionRange = range_of_node(m.m_tuple->identifier);
				}
				symbol.documentation = m.m_tuple->doc_data.description;
				symbol.uri = uri;
				symbol.script_path = path;

				symbol.detail = "tuple " + symbol.name;

				append_tuple_symbol_fields(m.m_tuple, symbol);

				r_symbol.children.push_back(symbol);
			} break;
			case ClassNode::Member::GROUP:
				break; // No-op, but silences warnings.
			case ClassNode::Member::UNDEFINED:
				break; // Unreachable.
		}
	}

	// Custom annotation declarations are root-only and are not class members, but they need a
	// symbol so go-to-definition can navigate `@my_annotation` usages to their declaration.
	for (const FSParser::AnnotationDeclarationNode *declaration : p_class->annotation_declarations) {
		if (declaration->identifier == nullptr) {
			continue;
		}
		LSP::DocumentSymbol symbol;
		symbol.name = declaration->identifier->name;
		// No dedicated LSP symbol kind exists for annotations. `Operator` is unused by any class
		// member here, so it doubles as a marker that keeps these symbols out of the member cache.
		symbol.kind = LSP::SymbolKind::Operator;
		symbol.deprecated = false;
		symbol.range = range_of_node(declaration);
		symbol.selectionRange = range_of_node(declaration->identifier);
		symbol.uri = uri;
		symbol.script_path = path;
		symbol.detail = "annotation " + String(declaration->identifier->name);
		r_symbol.children.push_back(symbol);
	}
}

void ExtendFSParser::parse_function_symbol(const FSParser::FunctionNode *p_func, LSP::DocumentSymbol &r_symbol, const FSParser::ClassNode *p_owner_class) {
	const String uri = get_uri();

	bool is_named = p_func->identifier != nullptr;

	r_symbol.name = is_named ? p_func->identifier->name : "";
	r_symbol.kind = (p_func->is_static || p_func->source_lambda != nullptr) ? LSP::SymbolKind::Function : LSP::SymbolKind::Method;
	if (p_func->is_declared_async) {
		r_symbol.detail = p_func->is_static ? "static async func" : "async func";
	} else {
		r_symbol.detail = p_func->is_static ? "static func" : "func";
	}
	if (is_named) {
		r_symbol.detail += " " + String(p_func->identifier->name);
	}
	// Show the generic type-parameter list (`[T, U: Bound]`) for a generic method.
	if (!p_func->type_parameters.is_empty()) {
		r_symbol.detail += "[";
		bool first_type_parameter = true;
		for (const FSParser::TypeParameterNode *type_parameter : p_func->type_parameters) {
			if (type_parameter == nullptr || type_parameter->identifier == nullptr) {
				continue;
			}
			if (!first_type_parameter) {
				r_symbol.detail += ", ";
			}
			first_type_parameter = false;
			r_symbol.detail += type_parameter->identifier->name;
			// Method type parameters do not get an eager `resolved_bound`, so fall back to the bound
			// TypeNode's datatype (a metatype handle in type position, hence the meta strip).
			FSParser::DataType bound_type = type_parameter->resolved_bound;
			if ((!bound_type.is_set() || bound_type.is_variant()) && type_parameter->bound != nullptr) {
				bound_type = type_parameter->bound->get_datatype();
				bound_type.is_meta_type = false;
			}
			if (bound_type.is_set() && !bound_type.is_variant()) {
				r_symbol.detail += ": " + bound_type.to_string();
			}
		}
		r_symbol.detail += "]";
	}
	r_symbol.detail += "(";
	r_symbol.deprecated = false;
	r_symbol.range = range_of_node(p_func);
	if (is_named) {
		r_symbol.selectionRange = range_of_node(p_func->identifier);
	} else {
		r_symbol.selectionRange.start = r_symbol.selectionRange.end = r_symbol.range.start;
	}
	r_symbol.documentation = p_func->doc_data.description;
	r_symbol.uri = uri;
	r_symbol.script_path = path;
	if (p_owner_class != nullptr && p_owner_class->is_trait && p_owner_class->identifier != nullptr) {
		r_symbol.trait_source = p_owner_class->identifier->name;
	}

	String parameters;
	for (int i = 0; i < p_func->parameters.size(); i++) {
		const ParameterNode *parameter = p_func->parameters[i];
		if (i > 0) {
			parameters += ", ";
		}
		parameters += String(parameter->identifier->name);
		if (parameter->get_datatype().is_hard_type()) {
			parameters += ": " + parameter->get_datatype().to_string();
		}
		if (parameter->initializer != nullptr) {
			const FSParser::DataType param_type = parameter->get_datatype();
			const Variant &reduced_value = parameter->initializer->reduced_value;
			if (param_type.kind == FSParser::DataType::ENUM && !param_type.enum_values.is_empty() && reduced_value.get_type() == Variant::INT) {
				parameters += " = " + FSDocGen::docvalue_from_enum_value(reduced_value, param_type.enum_values);
			} else {
				parameters += " = " + reduced_value.to_json_string();
			}
		}
	}
	if (p_func->is_vararg()) {
		if (!p_func->parameters.is_empty()) {
			parameters += ", ";
		}
		const ParameterNode *rest_param = p_func->rest_parameter;
		parameters += "..." + rest_param->identifier->name + ": " + rest_param->get_datatype().to_string();
	}
	r_symbol.detail += parameters + ")";

	const DataType return_type = p_func->get_datatype();
	if (return_type.is_hard_type()) {
		if (return_type.kind == DataType::BUILTIN && return_type.builtin_type == Variant::NIL) {
			r_symbol.detail += " -> void";
		} else {
			r_symbol.detail += " -> " + return_type.to_string();
		}
	}

	List<FSParser::SuiteNode *> function_nodes;

	List<FSParser::Node *> node_stack;
	node_stack.push_back(p_func->body);

	while (!node_stack.is_empty()) {
		FSParser::Node *node = node_stack.front()->get();
		node_stack.pop_front();

		switch (node->type) {
			case FSParser::TypeNode::IF: {
				FSParser::IfNode *if_node = (FSParser::IfNode *)node;
				node_stack.push_back(if_node->true_block);
				if (if_node->false_block) {
					node_stack.push_back(if_node->false_block);
				}
			} break;

			case FSParser::TypeNode::FOR: {
				FSParser::ForNode *for_node = (FSParser::ForNode *)node;
				node_stack.push_back(for_node->loop);
			} break;

			case FSParser::TypeNode::WHILE: {
				FSParser::WhileNode *while_node = (FSParser::WhileNode *)node;
				node_stack.push_back(while_node->loop);
			} break;

			case FSParser::TypeNode::MATCH: {
				FSParser::MatchNode *match_node = (FSParser::MatchNode *)node;
				for (FSParser::MatchBranchNode *branch_node : match_node->branches) {
					node_stack.push_back(branch_node);
				}
			} break;

			case FSParser::TypeNode::MATCH_BRANCH: {
				FSParser::MatchBranchNode *match_node = (FSParser::MatchBranchNode *)node;
				node_stack.push_back(match_node->block);
			} break;

			case FSParser::TypeNode::SUITE: {
				FSParser::SuiteNode *suite_node = (FSParser::SuiteNode *)node;
				function_nodes.push_back(suite_node);
				for (int i = 0; i < suite_node->statements.size(); ++i) {
					node_stack.push_back(suite_node->statements[i]);
				}
			} break;

			default:
				continue;
		}
	}

	for (List<FSParser::SuiteNode *>::Element *N = function_nodes.front(); N; N = N->next()) {
		const FSParser::SuiteNode *suite_node = N->get();
		for (int i = 0; i < suite_node->locals.size(); i++) {
			const SuiteNode::Local &local = suite_node->locals[i];
			LSP::DocumentSymbol symbol;
			symbol.name = local.name;
			symbol.kind = local.type == SuiteNode::Local::CONSTANT ? LSP::SymbolKind::Constant : LSP::SymbolKind::Variable;
			switch (local.type) {
				case SuiteNode::Local::CONSTANT:
					symbol.range = range_of_node(local.constant);
					symbol.selectionRange = range_of_node(local.constant->identifier);
					break;
				case SuiteNode::Local::VARIABLE:
					symbol.range = range_of_node(local.variable);
					symbol.selectionRange = range_of_node(local.variable->identifier);
					if (local.variable->initializer && local.variable->initializer->type == FSParser::Node::LAMBDA) {
						FSParser::LambdaNode *lambda_node = (FSParser::LambdaNode *)local.variable->initializer;
						LSP::DocumentSymbol lambda;
						parse_function_symbol(lambda_node->function, lambda);
						// Merge lambda into current variable.
						// -> Only interested in new variables, not lambda itself.
						symbol.children.append_array(lambda.children);
					}
					break;
				case SuiteNode::Local::PARAMETER:
					symbol.range = range_of_node(local.parameter);
					symbol.selectionRange = range_of_node(local.parameter->identifier);
					break;
				case SuiteNode::Local::FOR_VARIABLE:
				case SuiteNode::Local::PATTERN_BIND:
				case SuiteNode::Local::CASE_BIND:
					symbol.range = range_of_node(local.bind);
					symbol.selectionRange = range_of_node(local.bind);
					break;
				default:
					// Fallback.
					symbol.range.start = FoundryPosition(local.start_line, local.start_column).to_lsp(get_lines());
					symbol.range.end = FoundryPosition(local.end_line, local.end_column).to_lsp(get_lines());
					symbol.selectionRange = symbol.range;
					break;
			}
			symbol.local = true;
			symbol.uri = uri;
			symbol.script_path = path;
			symbol.detail = local.type == SuiteNode::Local::CONSTANT ? "const " : "var ";
			symbol.detail += symbol.name;
			if (local.get_datatype().is_hard_type()) {
				symbol.detail += ": " + local.get_datatype().to_string();
			}
			switch (local.type) {
				case SuiteNode::Local::CONSTANT:
					symbol.documentation = local.constant->doc_data.description;
					break;
				case SuiteNode::Local::VARIABLE:
					symbol.documentation = local.variable->doc_data.description;
					break;
				default:
					break;
			}
			r_symbol.children.push_back(symbol);
		}
	}
}

String ExtendFSParser::get_text_for_completion(const LSP::Position &p_cursor) const {
	String longthing;
	int len = lines.size();
	for (int i = 0; i < len; i++) {
		if (i == p_cursor.line) {
			longthing += lines[i].substr(0, p_cursor.character);
			longthing += String::chr(0xFFFF); // Not unicode, represents the cursor.
			longthing += lines[i].substr(p_cursor.character);
		} else {
			longthing += lines[i];
		}

		if (i != len - 1) {
			longthing += "\n";
		}
	}

	return longthing;
}

String ExtendFSParser::get_text_for_lookup_symbol(const LSP::Position &p_cursor, const String &p_symbol, bool p_func_required) const {
	String longthing;
	int len = lines.size();
	for (int i = 0; i < len; i++) {
		if (i == p_cursor.line) {
			// This code tries to insert the symbol into the preexisting code. Due to using a simple
			// algorithm, the results might not always match the option semantically (e.g. different
			// identifier name). This is fine because symbol lookup will prioritize the provided
			// symbol name over the actual code. Establishing a syntactic target (e.g. identifier)
			// is usually sufficient.

			String line = lines[i];
			String first_part = line.substr(0, p_cursor.character);
			String last_part = line.substr(p_cursor.character, lines[i].length());
			if (!p_symbol.is_empty()) {
				String left_cursor_text;
				for (int c = p_cursor.character - 1; c >= 0; c--) {
					left_cursor_text = line.substr(c, p_cursor.character - c);
					if (p_symbol.begins_with(left_cursor_text)) {
						first_part = line.substr(0, c);
						first_part += p_symbol;
						break;
					} else if (c == 0) {
						// No preexisting code that matches the option. Insert option in place.
						first_part += p_symbol;
					}
				}
			}

			longthing += first_part;
			longthing += String::chr(0xFFFF); // Not unicode, represents the cursor.
			if (p_func_required) {
				longthing += "("; // Tell the parser this is a function call.
			}
			longthing += last_part;
		} else {
			longthing += lines[i];
		}

		if (i != len - 1) {
			longthing += "\n";
		}
	}

	return longthing;
}

String ExtendFSParser::get_identifier_under_position(const LSP::Position &p_position, LSP::Range &r_range) const {
	ERR_FAIL_INDEX_V(p_position.line, lines.size(), "");
	String line = lines[p_position.line];
	if (line.is_empty()) {
		return "";
	}
	ERR_FAIL_INDEX_V(p_position.character, line.size(), "");

	// `p_position` cursor is BETWEEN chars, not ON chars.
	// ->
	// ```foundry_script
	// var member| := some_func|(some_variable|)
	//           ^             ^              ^
	//           |             |              | cursor on `some_variable, position on `)`
	//           |             |
	//           |             | cursor on `some_func`, pos on `(`
	//           |
	//           | cursor on `member`, pos on ` ` (space)
	// ```
	// -> Move position to previous character if:
	//    * Position not on valid identifier char.
	//    * Prev position is valid identifier char.
	LSP::Position pos = p_position;
	if (
			pos.character >= line.length() // Cursor at end of line.
			|| (!is_unicode_identifier_continue(line[pos.character]) // Not on valid identifier char.
					   && (pos.character > 0 // Not line start -> there is a prev char.
								  && is_unicode_identifier_continue(line[pos.character - 1]) // Prev is valid identifier char.
								  ))) {
		pos.character--;
	}

	int start_pos = pos.character;
	for (int c = pos.character; c >= 0; c--) {
		start_pos = c;
		char32_t ch = line[c];
		bool valid_char = is_unicode_identifier_continue(ch);
		if (!valid_char) {
			break;
		}
	}

	int end_pos = pos.character;
	for (int c = pos.character; c < line.length(); c++) {
		char32_t ch = line[c];
		bool valid_char = is_unicode_identifier_continue(ch);
		if (!valid_char) {
			break;
		}
		end_pos = c;
	}

	if (!is_unicode_identifier_start(line[start_pos + 1])) {
		return "";
	}

	if (start_pos < end_pos) {
		r_range.start.line = r_range.end.line = pos.line;
		r_range.start.character = start_pos + 1;
		r_range.end.character = end_pos + 1;
		return line.substr(start_pos + 1, end_pos - start_pos);
	}

	return "";
}

String ExtendFSParser::get_uri() const {
	return FSLanguageProtocol::get_singleton()->get_workspace()->get_file_uri(path);
}

const LSP::DocumentSymbol *ExtendFSParser::search_symbol_defined_at_line(int p_line, const LSP::DocumentSymbol &p_parent, const String &p_symbol_name) const {
	const LSP::DocumentSymbol *ret = nullptr;
	if (p_line < p_parent.range.start.line) {
		return ret;
	} else if (p_parent.range.start.line == p_line && (p_symbol_name.is_empty() || p_parent.name == p_symbol_name)) {
		return &p_parent;
	} else {
		for (int i = 0; i < p_parent.children.size(); i++) {
			ret = search_symbol_defined_at_line(p_line, p_parent.children[i], p_symbol_name);
			if (ret) {
				break;
			}
		}
	}
	return ret;
}

Error ExtendFSParser::get_left_function_call(const LSP::Position &p_position, LSP::Position &r_func_pos, int &r_arg_index) const {
	ERR_FAIL_INDEX_V(p_position.line, lines.size(), ERR_INVALID_PARAMETER);

	int bracket_stack = 0;
	int index = 0;

	bool found = false;
	for (int l = p_position.line; l >= 0; --l) {
		String line = lines[l];
		int c = line.length() - 1;
		if (l == p_position.line) {
			c = MIN(c, p_position.character - 1);
		}

		while (c >= 0) {
			const char32_t &character = line[c];
			if (character == ')') {
				++bracket_stack;
			} else if (character == '(') {
				--bracket_stack;
				if (bracket_stack < 0) {
					found = true;
				}
			}
			if (bracket_stack <= 0 && character == ',') {
				++index;
			}
			--c;
			if (found) {
				r_func_pos.character = c;
				break;
			}
		}

		if (found) {
			r_func_pos.line = l;
			r_arg_index = index;
			return OK;
		}
	}

	return ERR_METHOD_NOT_FOUND;
}

const LSP::DocumentSymbol *ExtendFSParser::get_symbol_defined_at_line(int p_line, const String &p_symbol_name) const {
	// A negative line is the location-0 root sentinel (e.g. autoload singletons), which always
	// resolves to the script root. Line 0 is a genuine first source line: it resolves to the root
	// only for a whole-line lookup or the root class itself, so a declaration on the first line
	// (e.g. the opening `annotation` line of an annotation-only file) resolves to its own symbol.
	if (p_line < 0) {
		return &class_symbol;
	}
	if (p_line == 0 && (p_symbol_name.is_empty() || p_symbol_name == class_symbol.name)) {
		return &class_symbol;
	}
	return search_symbol_defined_at_line(p_line, class_symbol, p_symbol_name);
}

const LSP::DocumentSymbol *ExtendFSParser::get_member_symbol(const String &p_name, const String &p_subclass) const {
	if (p_subclass.is_empty()) {
		const LSP::DocumentSymbol *const *ptr = members.getptr(p_name);
		if (ptr) {
			return *ptr;
		}
	} else {
		if (const ClassMembers *_class = inner_classes.getptr(p_subclass)) {
			const LSP::DocumentSymbol *const *ptr = _class->getptr(p_name);
			if (ptr) {
				return *ptr;
			}
		}
	}

	return nullptr;
}

const List<LSP::DocumentLink> &ExtendFSParser::get_document_links() const {
	return document_links;
}

const Array &ExtendFSParser::get_member_completions() {
	if (member_completions.is_empty()) {
		for (const KeyValue<String, const LSP::DocumentSymbol *> &E : members) {
			const LSP::DocumentSymbol *symbol = E.value;
			LSP::CompletionItem item = symbol->make_completion_item();
			item.data = JOIN_SYMBOLS(path, E.key);
			member_completions.push_back(item.to_json());
		}

		for (const KeyValue<String, ClassMembers> &E : inner_classes) {
			const ClassMembers *inner_class = &E.value;

			for (const KeyValue<String, const LSP::DocumentSymbol *> &F : *inner_class) {
				const LSP::DocumentSymbol *symbol = F.value;
				LSP::CompletionItem item = symbol->make_completion_item();
				item.data = JOIN_SYMBOLS(path, JOIN_SYMBOLS(E.key, F.key));
				member_completions.push_back(item.to_json());
			}
		}
	}

	return member_completions;
}

Dictionary ExtendFSParser::dump_function_api(const FSParser::FunctionNode *p_func) const {
	ERR_FAIL_NULL_V(p_func, Dictionary());
	Dictionary func;
	func["name"] = p_func->identifier->name;
	func["return_type"] = p_func->get_datatype().to_string();
	func["rpc_config"] = p_func->rpc_config;
	Array parameters;
	for (int i = 0; i < p_func->parameters.size(); i++) {
		Dictionary arg;
		arg["name"] = p_func->parameters[i]->identifier->name;
		arg["type"] = p_func->parameters[i]->get_datatype().to_string();
		if (p_func->parameters[i]->initializer != nullptr) {
			arg["default_value"] = p_func->parameters[i]->initializer->reduced_value;
		}
		parameters.push_back(arg);
	}
	if (const LSP::DocumentSymbol *symbol = get_symbol_defined_at_line(LINE_NUMBER_TO_INDEX(p_func->start_line))) {
		func["signature"] = symbol->detail;
		func["description"] = symbol->documentation;
	}
	func["arguments"] = parameters;
	return func;
}

Dictionary ExtendFSParser::dump_class_api(const FSParser::ClassNode *p_class) const {
	ERR_FAIL_NULL_V(p_class, Dictionary());
	Dictionary class_api;

	class_api["name"] = p_class->identifier != nullptr ? String(p_class->identifier->name) : String();
	class_api["path"] = path;
	Array extends_class;
	for (int i = 0; i < p_class->extends.size(); i++) {
		extends_class.append(String(p_class->extends[i]->name));
	}
	class_api["extends_class"] = extends_class;
	class_api["extends_file"] = String(p_class->extends_path);
	class_api["icon"] = String(p_class->icon_path);

	if (const LSP::DocumentSymbol *symbol = get_symbol_defined_at_line(LINE_NUMBER_TO_INDEX(p_class->start_line))) {
		class_api["signature"] = symbol->detail;
		class_api["description"] = symbol->documentation;
	}

	Array nested_classes;
	Array constants;
	Array class_members;
	Array signals;
	Array methods;
	Array static_functions;

	for (int i = 0; i < p_class->members.size(); i++) {
		const ClassNode::Member &m = p_class->members[i];
		switch (m.type) {
			case ClassNode::Member::CLASS:
				nested_classes.push_back(dump_class_api(m.m_class));
				break;
			case ClassNode::Member::CONSTANT: {
				Dictionary api;
				api["name"] = m.constant->identifier->name;
				api["value"] = m.constant->initializer->reduced_value;
				api["data_type"] = m.constant->get_datatype().to_string();
				if (const LSP::DocumentSymbol *symbol = get_symbol_defined_at_line(LINE_NUMBER_TO_INDEX(m.constant->start_line))) {
					api["signature"] = symbol->detail;
					api["description"] = symbol->documentation;
				}
				constants.push_back(api);
			} break;
			case ClassNode::Member::ENUM_VALUE: {
				Dictionary api;
				api["name"] = m.enum_value.identifier->name;
				api["value"] = m.enum_value.value;
				api["data_type"] = m.get_datatype().to_string();
				if (const LSP::DocumentSymbol *symbol = get_symbol_defined_at_line(LINE_NUMBER_TO_INDEX(m.enum_value.line))) {
					api["signature"] = symbol->detail;
					api["description"] = symbol->documentation;
				}
				constants.push_back(api);
			} break;
			case ClassNode::Member::ENUM: {
				Dictionary enum_dict;
				for (int j = 0; j < m.m_enum->values.size(); j++) {
					enum_dict[m.m_enum->values[j].identifier->name] = m.m_enum->values[j].value;
				}

				Dictionary api;
				api["name"] = m.m_enum->identifier->name;
				api["value"] = enum_dict;
				api["data_type"] = m.get_datatype().to_string();
				if (const LSP::DocumentSymbol *symbol = get_symbol_defined_at_line(LINE_NUMBER_TO_INDEX(m.m_enum->start_line))) {
					api["signature"] = symbol->detail;
					api["description"] = symbol->documentation;
				}
				constants.push_back(api);
			} break;
			case ClassNode::Member::VARIABLE: {
				Dictionary api;
				api["name"] = m.variable->identifier->name;
				api["data_type"] = m.variable->get_datatype().to_string();
				api["default_value"] = m.variable->initializer != nullptr ? m.variable->initializer->reduced_value : Variant();
				api["setter"] = m.variable->setter ? ("@" + String(m.variable->identifier->name) + "_setter") : (m.variable->setter_pointer != nullptr ? String(m.variable->setter_pointer->name) : String());
				api["getter"] = m.variable->getter ? ("@" + String(m.variable->identifier->name) + "_getter") : (m.variable->getter_pointer != nullptr ? String(m.variable->getter_pointer->name) : String());
				api["export"] = m.variable->exported;
				if (const LSP::DocumentSymbol *symbol = get_symbol_defined_at_line(LINE_NUMBER_TO_INDEX(m.variable->start_line))) {
					api["signature"] = symbol->detail;
					api["description"] = symbol->documentation;
				}
				class_members.push_back(api);
			} break;
			case ClassNode::Member::SIGNAL: {
				Dictionary api;
				api["name"] = m.signal->identifier->name;
				Array pars;
				for (int j = 0; j < m.signal->parameters.size(); j++) {
					pars.append(String(m.signal->parameters[j]->identifier->name));
				}
				api["arguments"] = pars;
				if (const LSP::DocumentSymbol *symbol = get_symbol_defined_at_line(LINE_NUMBER_TO_INDEX(m.signal->start_line))) {
					api["signature"] = symbol->detail;
					api["description"] = symbol->documentation;
				}
				signals.push_back(api);
			} break;
			case ClassNode::Member::FUNCTION: {
				if (m.function->is_static) {
					static_functions.append(dump_function_api(m.function));
				} else {
					methods.append(dump_function_api(m.function));
				}
			} break;
			case ClassNode::Member::TUPLE:
				// Tuple declarations do not have a dedicated API bucket yet; they are omitted
				// from this legacy dump like other type-only declarations without a value.
				break;
			case ClassNode::Member::GROUP:
				break; // No-op, but silences warnings.
			case ClassNode::Member::UNDEFINED:
				break; // Unreachable.
		}
	}

	class_api["sub_classes"] = nested_classes;
	class_api["constants"] = constants;
	class_api["members"] = class_members;
	class_api["signals"] = signals;
	class_api["methods"] = methods;
	class_api["static_functions"] = static_functions;

	return class_api;
}

Dictionary ExtendFSParser::generate_api() const {
	Dictionary api;
	if (const FSParser::ClassNode *gdclass = dynamic_cast<const FSParser::ClassNode *>(get_tree())) {
		api = dump_class_api(gdclass);
	}
	return api;
}

ExtendFSParser *ExtendFSParser::parse_source(const String &p_code, const String &p_path) {
	ExtendFSParser *parser = memnew(ExtendFSParser);
	parser->parse(p_code, p_path);
	return parser;
}

#ifdef TESTS_ENABLED
static uint64_t parse_file_count_for_test = 0;

uint64_t ExtendFSParser::get_parse_file_count_for_test() {
	return parse_file_count_for_test;
}

void ExtendFSParser::reset_parse_file_count_for_test() {
	parse_file_count_for_test = 0;
}
#endif // TESTS_ENABLED

ExtendFSParser *ExtendFSParser::parse_file(const String &p_path) {
	if (!p_path.has_extension("fs")) {
		return nullptr;
	}

	Error err = OK;
	const String source = FileAccess::get_file_as_string(p_path, &err);
	if (err != OK) {
		return nullptr;
	}

#ifdef TESTS_ENABLED
	parse_file_count_for_test++;
#endif // TESTS_ENABLED

	return parse_source(source, p_path);
}

void ExtendFSParser::parse(const String &p_code, const String &p_path) {
	path = p_path;
	lines = p_code.split("\n");

	parse_result = FSParser::parse(p_code, p_path, false);
	FSAnalyzer analyzer(this);

	if (parse_result == OK) {
		parse_result = analyzer.analyze();
	}
	update_diagnostics();
	update_symbols();
	update_document_links(p_code);
}
