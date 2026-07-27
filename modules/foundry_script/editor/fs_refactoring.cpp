/**************************************************************************/
/*  fs_refactoring.cpp                                                    */
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

#include "fs_refactoring.h"

#ifdef TOOLS_ENABLED

#include "fs_container_inference.h"
#include "fs_refactoring_edits.h"
#include "fs_refactoring_names.h"
#include "fs_refactoring_shared.h"
#include "fs_refactoring_types.h"

#include "../fs_analyzer.h"
#include "../fs_position.h"
#include "../fs_script_extensible_native_hooks.h"
#include "../fs_type.h"

#include "core/io/file_access.h"
#include "core/object/class_db.h"
#include "core/os/mutex.h"
#include "core/string/char_utils.h"
#include "core/templates/hash_set.h"

#ifndef FOUNDRY_SCRIPT_NO_LSP
#include "core/object/class_db.h"
#include "core/object/script_language.h"

#include "../language_server/foundry_lsp.h"
#include "../language_server/fs_extend_parser.h"
#include "../language_server/fs_language_protocol.h"
#include "../language_server/fs_workspace.h"
#endif // FOUNDRY_SCRIPT_NO_LSP

#ifdef FOUNDRY_SCRIPT_NO_LSP
class FSParseResultProvider;
#endif // FOUNDRY_SCRIPT_NO_LSP

using namespace FSRefactorShared;

namespace {

struct TypeAnnotationCandidate {
	bool matched = false;
	bool enabled = false;
	String disabled_reason;
	// Declaration kind this candidate annotates: "variable", "constant",
	// "parameter", or "return". Surfaced through RefactorCandidate so batch callers
	// can bucket sites by kind.
	String kind;
	RefactorTextEdit edit;
	// Caret-test span of the declaration this candidate was found at. Used by the
	// caret-driven path to select the candidate under the caret; the headless
	// collector ignores it. The span opens at (line, caret_span_start). It closes
	// at (caret_span_end_line, caret_span_end) when caret_span_end_line is set, so
	// a wrapped (multi-line) signature stays selectable across its lines;
	// otherwise it closes at (line, caret_span_end).
	int line = -1;
	int caret_span_start = -1;
	int caret_span_end = -1;
	int caret_span_end_line = -1;
};

// Namespace context for rendering type annotations in a file that declares a
// `namespace` and/or `import`s. Carries the scope used to pick the minimal class
// spelling that resolves at the insertion site. A null context (or a file in the
// global namespace with no imports) renders annotations exactly as before.
struct TypeAnnotationRenderContext {
	FSRefactorTypes::AnnotationScope scope;
};

// The project-wide set of classes that `extends` a given class (transitively),
// used to make member container element inference subclass-aware: a subclass can
// mutate an inherited member with a different element type, so its writers must be
// folded into the inference's union. `complete` records whether the caller could
// prove it enumerated every such subclass; when it could not (e.g. a project script
// failed to parse), the inference bails conservatively rather than guessing.
struct MemberSubclassClosure {
	Vector<const FSParser::ClassNode *> subclasses;
	bool complete = true;
};

// Adds names declared by p_class that the analyzer resolves before a namespace
// chain (its own name, members, and type parameters) to r_scope's shadow set, so
// a qualified spelling is never rooted at one of them.
void add_class_scope_shadow_names(FSRefactorTypes::AnnotationScope &r_scope, const FSParser::ClassNode *p_class) {
	if (p_class == nullptr) {
		return;
	}
	if (p_class->identifier != nullptr) {
		r_scope.shadowing_local_names.push_back(p_class->identifier->name);
	}
	for (const FSParser::ClassNode::Member &member : p_class->members) {
		const String member_name = member.get_name();
		if (!member_name.is_empty()) {
			r_scope.shadowing_local_names.push_back(member_name);
		}
	}
	for (const FSParser::TypeParameterNode *type_parameter : p_class->type_parameters) {
		if (type_parameter != nullptr && type_parameter->identifier != nullptr) {
			r_scope.shadowing_local_names.push_back(type_parameter->identifier->name);
		}
	}
}

// Builds the namespace render context for the root class of the edited file. Each
// class's own scope names are added as the collector descends into it, so this
// only carries the file-level namespace and imports.
TypeAnnotationRenderContext make_type_annotation_render_context(const FSParser::ClassNode *p_root) {
	TypeAnnotationRenderContext context;
	if (p_root != nullptr) {
		context.scope.current_namespace = p_root->namespace_name;
		context.scope.imported_namespaces = p_root->imports;
	}
	return context;
}

struct ExtractVariableCandidate {
	bool matched = false;
	bool enabled = false;
	String disabled_reason;
	String suggested_name;
	RefactorTextEdit declaration_edit;
	RefactorTextEdit replacement_edit;
	int name_line = -1;
	int name_column = -1;
};

struct ExtractMethodParameter {
	StringName name;
	FSParser::DataType datatype;
};

struct ExtractMethodLocal {
	StringName name;
	FSParser::DataType datatype;
};

struct ExtractMethodCandidate {
	bool matched = false;
	bool enabled = false;
	String disabled_reason;
	String suggested_name;
	Vector<String> member_names;
	RefactorTextEdit replacement_edit;
	RefactorTextEdit method_edit;
	int name_line = -1;
	int name_column = -1;
};

struct InlineVariableUse {
	const FSParser::IdentifierNode *identifier = nullptr;
	const FSParser::Node *parent = nullptr;
};

struct InlineVariableCandidate {
	bool matched = false;
	bool enabled = false;
	String disabled_reason;
	RefactorTextEdit declaration_edit;
	Vector<RefactorTextEdit> replacement_edits;
};

// An abstract method the target class owes, paired with the source lines of the
// file that declares it. The declaring lines are required to recover verbatim
// parameter-default text: a default's source span points into the file that
// declares the method, which is not the target file when the abstract method is
// inherited from a base class in another script.
struct OwedAbstractMethod {
	const FSParser::FunctionNode *function = nullptr;
	const Vector<String> *declaring_lines = nullptr;
	const FSParser::ClassNode *declaring_class = nullptr;
	FSParser::DataType specialized_base;
};

struct ImplementAbstractCandidate {
	bool matched = false;
	bool enabled = false;
	String disabled_reason;
	// Owed abstract methods, most-derived-first.
	Vector<OwedAbstractMethod> abstract_methods;
	// The rendered block depends on declarations parsed from files other than the
	// active script. Such candidates cannot be cached by only active path/source/caret.
	bool depends_on_external_declarations = false;
	// Insertion point: the line AFTER the last member of the target class (1-based,
	// matching FunctionNode::end_line semantics used by extract method).
	int insertion_line = -1;
	// Finalized stub text, rendered during detection while the parse tree is alive so
	// no live FunctionNode pointer is ever cached.
	String rendered_block;
};

struct OverrideMethodCandidate {
	bool enabled = false;
	RefactorOverrideMethodCandidate public_candidate;
	String rendered_block;
	int insertion_line = -1;
};

struct OverrideMethodCollection {
	bool ok = false;
	String error_message;
	Vector<OverrideMethodCandidate> candidates;
};

enum class StyleOrderBucket {
	SIGNAL,
	ENUM,
	CONSTANT,
	STATIC_VARIABLE,
	EXPORTED_VARIABLE,
	PUBLIC_VARIABLE,
	PRIVATE_VARIABLE,
	ONREADY_PUBLIC_VARIABLE,
	ONREADY_PRIVATE_VARIABLE,
	STATIC_INIT,
	STATIC_METHOD,
	BUILTIN_VIRTUAL_METHOD,
	CUSTOM_OVERRIDE_METHOD,
	PUBLIC_METHOD,
	PRIVATE_METHOD,
	INNER_TYPE,
};

struct StyleOrderBlock {
	int original_index = -1;
	StyleOrderBucket bucket = StyleOrderBucket::PUBLIC_METHOD;
	int start_line = -1;
	int end_line = -1; // Exclusive.
	String text;
};

struct StyleOrderCandidate {
	bool enabled = false;
	String disabled_reason;
	Vector<RefactorTextEdit> edits;
};

struct StyleOrderBlockComparator {
	bool operator()(const StyleOrderBlock &p_a, const StyleOrderBlock &p_b) const {
		const int a_bucket = static_cast<int>(p_a.bucket);
		const int b_bucket = static_cast<int>(p_b.bucket);
		if (a_bucket != b_bucket) {
			return a_bucket < b_bucket;
		}
		return p_a.original_index < p_b.original_index;
	}
};

struct TypeAnnotationCandidateCache {
	bool valid = false;
	String path;
	uint64_t source_hash = 0;
	int source_length = 0;
	RefactorLocation location;
	TypeAnnotationCandidate candidate;
};

TypeAnnotationCandidateCache type_annotation_cache;

struct ExtractVariableCandidateCache {
	bool valid = false;
	String path;
	uint64_t source_hash = 0;
	int source_length = 0;
	RefactorLocation location;
	ExtractVariableCandidate candidate;
};

ExtractVariableCandidateCache extract_variable_cache;

struct ExtractMethodCandidateCache {
	bool valid = false;
	String path;
	uint64_t source_hash = 0;
	int source_length = 0;
	RefactorLocation location;
	ExtractMethodCandidate candidate;
};

ExtractMethodCandidateCache extract_method_cache;

struct InlineVariableCandidateCache {
	bool valid = false;
	String path;
	uint64_t source_hash = 0;
	int source_length = 0;
	RefactorLocation location;
	InlineVariableCandidate candidate;
};

InlineVariableCandidateCache inline_variable_cache;

struct ImplementAbstractCandidateCache {
	bool valid = false;
	String path;
	uint64_t source_hash = 0;
	int source_length = 0;
	RefactorLocation location;
	ImplementAbstractCandidate candidate;
};

ImplementAbstractCandidateCache implement_abstract_cache;

struct StyleOrderCandidateCache {
	bool valid = false;
	String path;
	uint64_t source_hash = 0;
	int source_length = 0;
	StyleOrderCandidate candidate;
};

StyleOrderCandidateCache style_order_cache;

Mutex refactor_candidate_cache_mutex;

bool is_supported_dynamic_string_call(const String &p_name) {
	return p_name == "get" ||
			p_name == "set" ||
			p_name == "call" ||
			p_name == "call_deferred" ||
			p_name == "set_deferred";
}

int find_first_string_argument_column(
		const String &p_line,
		int p_open_paren_column,
		const String &p_identifier,
		int p_match_column = -1) {
	int pos = p_open_paren_column + 1;
	while (pos < p_line.length() && is_whitespace(p_line[pos])) {
		pos++;
	}

	if (pos >= p_line.length() || (p_line[pos] != '"' && p_line[pos] != '\'')) {
		return -1;
	}

	const char32_t quote = p_line[pos];
	String value;
	bool escaped = false;
	for (int i = pos + 1; i < p_line.length(); i++) {
		const char32_t c = p_line[i];
		if (escaped) {
			value += String::chr(c);
			escaped = false;
			continue;
		}
		if (c == '\\') {
			escaped = true;
			continue;
		}
		if (c == quote) {
			const bool column_matches = p_match_column < 0 || (p_match_column >= pos && p_match_column <= i);
			return value == p_identifier && column_matches ? pos + 1 : -1;
		}
		value += String::chr(c);
	}

	return -1;
}

int find_dynamic_string_reference_column(const String &p_line, const String &p_identifier, int p_match_column = -1) {
	// This is a conservative scan for common Object string-call forms. It is not a
	// complete dynamic-reference parser and intentionally ignores unrelated strings.
	for (int i = 0; i < p_line.length(); i++) {
		const char32_t c = p_line[i];
		if (c == '#') {
			return -1;
		}
		if (c == '"' || c == '\'') {
			i = skip_string_literal(p_line, i) - 1;
			continue;
		}
		if (!is_unicode_identifier_start(c)) {
			continue;
		}

		const int name_start = i;
		i++;
		while (i < p_line.length() && is_unicode_identifier_continue(p_line[i])) {
			i++;
		}
		const int name_end = i;
		const String name = p_line.substr(name_start, name_end - name_start);
		if (!is_supported_dynamic_string_call(name)) {
			i--;
			continue;
		}

		int open_paren = name_end;
		while (open_paren < p_line.length() && is_whitespace(p_line[open_paren])) {
			open_paren++;
		}
		if (open_paren >= p_line.length() || p_line[open_paren] != '(') {
			i--;
			continue;
		}

		const int column = find_first_string_argument_column(p_line, open_paren, p_identifier, p_match_column);
		if (column >= 0) {
			return column;
		}
		i--;
	}

	return -1;
}

bool render_annotation_or_disable(const FSParser::DataType &p_type, TypeAnnotationCandidate &r_candidate, String &r_rendered, const TypeAnnotationRenderContext *p_render_context = nullptr) {
	if (p_render_context == nullptr) {
		if (!FSRefactorTypes::render_annotatable_type(p_type, r_rendered)) {
			r_candidate.disabled_reason = "The inferred type cannot be written as an explicit annotation.";
			return false;
		}
		return true;
	}

	// Namespace-aware rendering: a cross-namespace class is spelled so it resolves
	// at the insertion site (bare when already in scope, qualified otherwise),
	// rather than emitting a bare name the verification harness would then drop.
	HashSet<String> required_imports;
	if (!FSRefactorTypes::render_annotatable_type_in_scope(p_type, p_render_context->scope, r_rendered, required_imports)) {
		r_candidate.disabled_reason = "The inferred type cannot be written as an explicit annotation.";
		return false;
	}
	return true;
}

bool extract_method_has_param(const Vector<ExtractMethodParameter> &p_parameters, const StringName &p_name) {
	for (const ExtractMethodParameter &parameter : p_parameters) {
		if (parameter.name == p_name) {
			return true;
		}
	}
	return false;
}

bool extract_method_has_local(const Vector<ExtractMethodLocal> &p_locals, const StringName &p_name) {
	for (const ExtractMethodLocal &local : p_locals) {
		if (local.name == p_name) {
			return true;
		}
	}
	return false;
}

void extract_method_add_local(
		Vector<ExtractMethodLocal> &r_locals,
		const StringName &p_name,
		const FSParser::DataType &p_datatype) {
	if (extract_method_has_local(r_locals, p_name)) {
		return;
	}

	ExtractMethodLocal local;
	local.name = p_name;
	local.datatype = p_datatype;
	r_locals.push_back(local);
}

void extract_method_add_name(Vector<StringName> &r_names, const StringName &p_name) {
	if (!string_name_vector_has(r_names, p_name)) {
		r_names.push_back(p_name);
	}
}

bool suite_or_ancestors_have_local(const FSParser::SuiteNode *p_suite, const StringName &p_name) {
	const FSParser::SuiteNode *suite = p_suite;
	while (suite != nullptr) {
		if (suite->has_local(p_name)) {
			return true;
		}
		suite = suite->parent_block;
	}
	return false;
}

String expression_name_hint(const FSParser::ExpressionNode *p_expression) {
	if (p_expression == nullptr) {
		return String();
	}

	switch (p_expression->type) {
		case FSParser::Node::IDENTIFIER:
			return String(static_cast<const FSParser::IdentifierNode *>(p_expression)->name);
		case FSParser::Node::SUBSCRIPT: {
			const FSParser::SubscriptNode *subscript = static_cast<const FSParser::SubscriptNode *>(p_expression);
			if (subscript->is_attribute && subscript->attribute != nullptr) {
				return String(subscript->attribute->name);
			}
		} break;
		case FSParser::Node::CALL: {
			const FSParser::CallNode *call = static_cast<const FSParser::CallNode *>(p_expression);
			String name = expression_name_hint(call->callee);
			if (name.begins_with("get_") && name.length() > 4) {
				name = name.substr(4);
			} else if (name.begins_with("is_") && name.length() > 3) {
				name = name.substr(3);
			} else if (name.begins_with("has_") && name.length() > 4) {
				name = name.substr(4);
			}
			return name;
		}
		case FSParser::Node::AWAIT:
			return expression_name_hint(static_cast<const FSParser::AwaitNode *>(p_expression)->to_await);
		case FSParser::Node::CAST:
			return expression_name_hint(static_cast<const FSParser::CastNode *>(p_expression)->operand);
		default:
			break;
	}

	return String();
}

String fallback_name_for_type(const FSParser::DataType &p_type) {
	if (p_type.kind == FSParser::DataType::BUILTIN) {
		switch (p_type.builtin_type) {
			case Variant::BOOL:
				return "flag";
			case Variant::STRING:
			case Variant::STRING_NAME:
			case Variant::NODE_PATH:
				return "text";
			case Variant::ARRAY:
			case Variant::PACKED_BYTE_ARRAY:
			case Variant::PACKED_INT32_ARRAY:
			case Variant::PACKED_INT64_ARRAY:
			case Variant::PACKED_FLOAT32_ARRAY:
			case Variant::PACKED_FLOAT64_ARRAY:
			case Variant::PACKED_STRING_ARRAY:
			case Variant::PACKED_VECTOR2_ARRAY:
			case Variant::PACKED_VECTOR3_ARRAY:
			case Variant::PACKED_COLOR_ARRAY:
			case Variant::PACKED_VECTOR4_ARRAY:
				return "items";
			case Variant::DICTIONARY:
				return "map";
			default:
				break;
		}
	}
	return "value";
}

String make_unique_local_name(const FSParser::ExpressionNode *p_expression, const FSParser::DataType &p_type, const FSParser::SuiteNode *p_suite) {
	String base = expression_name_hint(p_expression);
	String reason;
	if (base.is_empty() || !FSRefactorNames::validate_identifier(base, reason)) {
		base = fallback_name_for_type(p_type);
	}
	if (!FSRefactorNames::validate_identifier(base, reason)) {
		base = "value";
	}

	String name = base;
	int suffix = 2;
	while (!FSRefactorNames::validate_identifier(name, reason) || suite_or_ancestors_have_local(p_suite, StringName(name))) {
		name = vformat("%s_%d", base, suffix);
		suffix++;
		if (suffix > 1000) {
			return String();
		}
	}
	return name;
}

bool try_extract_direct_expression(const RefactorLocation &p_location, const Vector<String> &p_lines, const FSParser::ExpressionNode *p_expression, const FSParser::Node *p_statement, const FSParser::SuiteNode *p_suite, ExtractVariableCandidate &r_candidate) {
	if (!expression_matches_selection(p_location, p_lines, p_expression)) {
		return false;
	}

	r_candidate.matched = true;
	if (p_suite == nullptr || p_suite->parent_function == nullptr) {
		r_candidate.disabled_reason = "Extract variable is only available inside a function body.";
		return true;
	}
	if (p_statement == nullptr || p_statement->start_line <= 0 || p_statement->start_line > p_lines.size()) {
		r_candidate.disabled_reason = "Cannot find a safe insertion point for this expression.";
		return true;
	}

	String rendered_type;
	if (!FSRefactorTypes::render_annotatable_type(p_expression->get_datatype(), rendered_type)) {
		r_candidate.disabled_reason = "The inferred type cannot be written as an explicit annotation.";
		return true;
	}

	String expression_text;
	if (!get_single_line_selection_text(p_lines, p_location, expression_text)) {
		r_candidate.disabled_reason = "Extract variable currently supports single-line expressions.";
		return true;
	}

	const String name = make_unique_local_name(p_expression, p_expression->get_datatype(), p_suite);
	if (name.is_empty()) {
		r_candidate.disabled_reason = "Cannot suggest a safe local variable name.";
		return true;
	}

	const int insertion_line = p_statement->start_line - 1;
	const String indent = get_leading_whitespace(p_lines[insertion_line]);
	const String declaration_prefix = indent + "var ";
	r_candidate.declaration_edit.start_line = insertion_line;
	r_candidate.declaration_edit.start_column = 0;
	r_candidate.declaration_edit.end_line = insertion_line;
	r_candidate.declaration_edit.end_column = 0;
	// The replacement expression is inside an indented function-body statement,
	// so its start offset is after this column-0 insertion. That keeps edit
	// ordering unambiguous even though RefactorEdits sorts only by start offset.
	r_candidate.declaration_edit.new_text = declaration_prefix + name + ": " + rendered_type +
			" = " + expression_text + "\n";

	r_candidate.replacement_edit.start_line = p_location.start_line;
	r_candidate.replacement_edit.start_column = p_location.start_column;
	r_candidate.replacement_edit.end_line = p_location.end_line;
	r_candidate.replacement_edit.end_column = p_location.end_column;
	r_candidate.replacement_edit.new_text = name;
	r_candidate.suggested_name = name;
	r_candidate.name_line = insertion_line;
	r_candidate.name_column = declaration_prefix.length();
	r_candidate.enabled = true;
	return true;
}

bool is_self_attribute(const FSParser::ExpressionNode *p_expression) {
	if (p_expression == nullptr || p_expression->type != FSParser::Node::SUBSCRIPT) {
		return false;
	}
	const FSParser::SubscriptNode *subscript = static_cast<const FSParser::SubscriptNode *>(p_expression);
	return subscript->is_attribute && subscript->base != nullptr && subscript->base->type == FSParser::Node::SELF;
}

bool is_safe_extract_assignment(const FSParser::AssignmentNode *p_assignment) {
	if (p_assignment == nullptr || p_assignment->assignee == nullptr) {
		return false;
	}
	if (p_assignment->assignee->type == FSParser::Node::IDENTIFIER) {
		return true;
	}
	return p_assignment->operation == FSParser::AssignmentNode::OP_NONE &&
			is_self_attribute(p_assignment->assignee);
}

bool find_assignable_type_annotation(const Vector<String> &p_lines, const FSParser::AssignableNode *p_assignable, const String &p_kind, bool p_has_keyword, TypeAnnotationCandidate &r_candidate, const FSParser::SuiteNode *p_function_body = nullptr, const FSParser::ClassNode *p_member_class = nullptr, const TypeAnnotationRenderContext *p_render_context = nullptr, const MemberSubclassClosure *p_member_subclasses = nullptr) {
	if (p_assignable == nullptr || p_assignable->identifier == nullptr) {
		return false;
	}

	const int line_index = p_assignable->start_line - 1;
	if (line_index < 0 || line_index >= p_lines.size()) {
		return false;
	}

	const String line = p_lines[line_index];
	int name_start = 0;
	int name_end = 0;
	const RefactorLocation *identifier_location = nullptr; // Collect mode resolves the declaration identifier without caret scoping.
	if (!get_identifier_text_span(p_lines, line_index, p_assignable->identifier, identifier_location, name_start, name_end)) {
		return false;
	}
	int declaration_start = name_start;
	if (p_has_keyword) {
		const String keyword = p_assignable->type == FSParser::Node::CONSTANT ? "const" : "var";
		int keyword_start = 0;
		if (get_node_text_start(p_lines, line_index, p_assignable, keyword, keyword_start)) {
			declaration_start = keyword_start;
		} else {
			keyword_start = line.find(keyword);
			if (keyword_start >= 0 && keyword_start <= name_start) {
				declaration_start = keyword_start;
			}
		}
	}

	// The parser does not expose the assignment operator token, so this refactor
	// locates it by scanning the declaration text, following continuation lines
	// when the assignment operator wraps onto a later line.
	int equal_line = -1;
	int equal_column = -1;
	bool is_inferred = false;
	int colon_line = -1;
	int colon_column = -1;
	const bool found_equal = p_assignable->initializer != nullptr &&
			find_declaration_assignment_operator(p_lines, line_index, name_end, equal_line, equal_column, is_inferred, colon_line, colon_column);
	const int declaration_end = found_equal && equal_line == line_index ? equal_column : name_end;

	r_candidate.matched = true;
	r_candidate.kind = p_kind;
	r_candidate.line = line_index;
	r_candidate.caret_span_start = declaration_start;
	r_candidate.caret_span_end = declaration_end;
	if (found_equal && equal_line != line_index) {
		// A wrapped assignment closes its caret span on the operator line so the
		// caret selects the annotation anywhere from the name through the `=`.
		r_candidate.caret_span_end = equal_column;
		r_candidate.caret_span_end_line = equal_line;
	}
	if (p_assignable->datatype_specifier != nullptr) {
		r_candidate.disabled_reason = "This declaration already has a type annotation.";
		return true;
	}
	if (p_assignable->initializer == nullptr) {
		r_candidate.disabled_reason = vformat("Cannot infer a type for this %s.", p_kind);
		return true;
	}
	if (!found_equal) {
		// The initializer resolved, but the assignment operator could not be
		// located (an unusual wrapped form). Report the site as a counted skip
		// rather than producing an unsafe edit.
		r_candidate.disabled_reason = vformat("Cannot annotate this %s because its assignment spans multiple lines.", p_kind);
		return true;
	}

	// For a bare local `Array` or `Dictionary` literal, try to recover concrete
	// element type(s) from how the variable is used in its function, upgrading
	// `Array` to `Array[T]` or `Dictionary` to `Dictionary[K, V]`. Anything the
	// inference cannot prove falls back to the analyzer's own type, so this never
	// narrows a declaration unsafely.
	FSParser::DataType effective_type = p_assignable->get_datatype();
	if (p_assignable->type == FSParser::Node::VARIABLE && (p_function_body != nullptr || p_member_class != nullptr)) {
		const FSParser::VariableNode *variable = static_cast<const FSParser::VariableNode *>(p_assignable);
		// A member variable is analyzed class-wide (including project-wide subclass
		// writers); a local is analyzed against its function body. Member inference
		// requires the subclass closure to bound the open world: when it is absent
		// (no workspace, or a FOUNDRY_SCRIPT_NO_LSP build that cannot scan the project),
		// completeness cannot be proven, so the inference must bail conservatively.
		const Vector<const FSParser::ClassNode *> subclasses =
				p_member_subclasses != nullptr ? p_member_subclasses->subclasses : Vector<const FSParser::ClassNode *>();
		const bool subclasses_complete = p_member_subclasses != nullptr && p_member_subclasses->complete;
		const FSContainerInference::Result array_inference = p_member_class != nullptr
				? FSContainerInference::infer_member_array_element_type(variable, p_member_class, subclasses, subclasses_complete)
				: FSContainerInference::infer_local_array_element_type(variable, p_function_body);
		if (array_inference.outcome == FSContainerInference::INFERRED) {
			effective_type = array_inference.element_type;
		} else {
			const FSContainerInference::Result dictionary_inference = p_member_class != nullptr
					? FSContainerInference::infer_member_dictionary_element_type(variable, p_member_class, subclasses, subclasses_complete)
					: FSContainerInference::infer_local_dictionary_element_type(variable, p_function_body);
			if (dictionary_inference.outcome == FSContainerInference::INFERRED) {
				effective_type = dictionary_inference.element_type;
			}
		}
	}

	if (!effective_type.is_type_handle_annotation && effective_type.is_meta_type && p_assignable->initializer != nullptr &&
			p_assignable->initializer->type == FSParser::Node::IDENTIFIER &&
			(effective_type.kind == FSParser::DataType::NATIVE || effective_type.kind == FSParser::DataType::SCRIPT ||
					effective_type.kind == FSParser::DataType::CLASS)) {
		effective_type.is_type_handle_annotation = true;
	}

	String rendered_type;
	if (!render_annotation_or_disable(effective_type, r_candidate, rendered_type, p_render_context)) {
		return true;
	}

	if (equal_line == line_index) {
		const int rhs_start = find_assignment_rhs_start(line, equal_column);
		r_candidate.edit.start_line = line_index;
		r_candidate.edit.start_column = name_end;
		r_candidate.edit.end_line = line_index;
		r_candidate.edit.end_column = rhs_start;
		r_candidate.edit.new_text = ": " + rendered_type + " = ";
		r_candidate.enabled = true;
		return true;
	}

	// The assignment operator wraps onto a continuation line. Insert the
	// annotation after the name and replace through the `=`, preserving the
	// continuation text verbatim. An inferred declaration (`:=` or `: =`) drops
	// its colon because the explicit annotation now carries the type. The
	// replacement reproduces the original line breaks so the recovered
	// declaration parses identically.
	//
	// Reconstruct the span [name_end on line_index, equal_column on equal_line),
	// omitting the inferred colon at (colon_line, colon_column) when present.
	String continuation;
	for (int wrapped = line_index; wrapped <= equal_line; wrapped++) {
		if (wrapped != line_index) {
			continuation += "\n";
		}
		const String &wrapped_line = p_lines[wrapped];
		const int slice_start = wrapped == line_index ? name_end : 0;
		const int slice_end = wrapped == equal_line ? equal_column : wrapped_line.length();
		if (is_inferred && colon_line == wrapped && colon_column >= slice_start && colon_column < slice_end) {
			continuation += wrapped_line.substr(slice_start, colon_column - slice_start);
			continuation += wrapped_line.substr(colon_column + 1, slice_end - (colon_column + 1));
		} else {
			continuation += wrapped_line.substr(slice_start, slice_end - slice_start);
		}
	}
	r_candidate.edit.start_line = line_index;
	r_candidate.edit.start_column = name_end;
	r_candidate.edit.end_line = equal_line;
	r_candidate.edit.end_column = equal_column + 1;
	r_candidate.edit.new_text = ": " + rendered_type + continuation + "=";
	r_candidate.enabled = true;
	return true;
}

bool find_function_return_type_annotation(const Vector<String> &p_lines, const FSParser::FunctionNode *p_function, TypeAnnotationCandidate &r_candidate, const TypeAnnotationRenderContext *p_render_context = nullptr) {
	if (p_function == nullptr || p_function->identifier == nullptr) {
		return false;
	}

	const int line_index = p_function->start_line - 1;
	if (line_index < 0 || line_index >= p_lines.size()) {
		return false;
	}

	const String line = p_lines[line_index];
	int function_start = 0;
	if (!get_node_text_start(p_lines, line_index, p_function, "func", function_start)) {
		// An `async func` places the async keyword before `func`, so the node may
		// start at `async`; fall back to locating the `func` keyword on the line.
		function_start = line.find("func");
	}
	if (function_start < 0) {
		return false;
	}
	if (p_function->is_abstract) {
		// A bodyless abstract declaration has no body colon, and the parser points
		// its synthetic body suite at the next member, so scanning for a colon would
		// run into the following declaration. Abstract functions also cannot infer a
		// return type from a body, so leave them out of return-type collection.
		return false;
	}
	// The parser does not expose the body-colon token for the signature, so scan
	// for the first top-level colon, following the signature across wrapped lines.
	// A non-abstract function always has a body, so its first body statement bounds
	// the scan and keeps it off a later declaration's colon.
	const int last_signature_line = p_function->body != nullptr ? p_function->body->start_line - 1 : line_index;
	int body_colon_line = line_index;
	int body_colon = -1;
	if (!find_function_signature_colon(p_lines, line_index, function_start, last_signature_line, body_colon_line, body_colon)) {
		return false;
	}

	r_candidate.matched = true;
	r_candidate.kind = "return";
	r_candidate.line = line_index;
	r_candidate.caret_span_start = function_start;
	// A wrapped signature closes its caret span on the body-colon line so the caret
	// selects the return-type annotation anywhere from `func` through that colon.
	r_candidate.caret_span_end = body_colon;
	r_candidate.caret_span_end_line = body_colon_line;
	if (p_function->return_type != nullptr) {
		r_candidate.disabled_reason = "This function already has a return type annotation.";
		return true;
	}

	String rendered_type;
	if (!render_annotation_or_disable(p_function->get_datatype(), r_candidate, rendered_type, p_render_context)) {
		return true;
	}

	r_candidate.edit.start_line = body_colon_line;
	r_candidate.edit.start_column = body_colon;
	r_candidate.edit.end_line = body_colon_line;
	r_candidate.edit.end_column = body_colon + 1;
	r_candidate.edit.new_text = " -> " + rendered_type + ":";
	r_candidate.enabled = true;
	return true;
}

#ifndef FOUNDRY_SCRIPT_NO_LSP
struct CallsiteParameterTypeState {
	bool has_call = false;
	bool failed = false;
	String rendered_type;
	// A scope-independent, fully-qualified identity used to decide whether call
	// sites agree. Two classes that render the same bare name but live in different
	// namespaces (e.g. alpha.Thing vs beta.Thing) have distinct identities, so they
	// are treated as disagreeing rather than conflated.
	String identity;
	// The DataType backing rendered_type, kept so the parameter annotation can be
	// re-rendered namespace-aware (qualified + import) at the target file's scope.
	FSParser::DataType datatype;
	bool has_datatype = false;
};

const FSParser::IdentifierNode *get_call_identifier(const FSParser::CallNode *p_call) {
	if (p_call == nullptr || p_call->callee == nullptr) {
		return nullptr;
	}

	switch (p_call->callee->type) {
		case FSParser::Node::IDENTIFIER:
			return static_cast<const FSParser::IdentifierNode *>(p_call->callee);
		case FSParser::Node::SUBSCRIPT: {
			const FSParser::SubscriptNode *subscript = static_cast<const FSParser::SubscriptNode *>(p_call->callee);
			return subscript->is_attribute ? subscript->attribute : nullptr;
		}
		default:
			break;
	}

	return nullptr;
}

bool call_resolves_to_symbol(
		const Ref<FSWorkspace> &p_workspace,
		const String &p_path,
		const ExtendFSParser *p_parser,
		const FSParser::CallNode *p_call,
		const LSP::DocumentSymbol *p_target_symbol,
		const FSParseResultProvider *p_parse_results) {
	ERR_FAIL_COND_V(p_workspace.is_null(), false);
	ERR_FAIL_NULL_V(p_parser, false);
	ERR_FAIL_NULL_V(p_call, false);
	ERR_FAIL_NULL_V(p_target_symbol, false);

	const FSParser::IdentifierNode *identifier = get_call_identifier(p_call);
	if (identifier == nullptr || identifier->name != p_call->function_name) {
		return false;
	}

	LSP::TextDocumentPositionParams doc_position;
	doc_position.textDocument.uri = p_workspace->get_file_uri(p_path);
	doc_position.position = FoundryPosition(identifier->start_line, identifier->start_column).to_lsp(p_parser->get_lines());
	return p_workspace->resolve_symbol(doc_position, String(), true, p_parse_results) == p_target_symbol;
}

void collect_callsite_parameter_type_in_expression(
		const Ref<FSWorkspace> &p_workspace,
		const String &p_path,
		const ExtendFSParser *p_parser,
		const FSParser::ExpressionNode *p_expression,
		const LSP::DocumentSymbol *p_target_symbol,
		int p_parameter_index,
		const FSParseResultProvider *p_parse_results,
		CallsiteParameterTypeState &r_state);

void collect_callsite_parameter_type_in_suite(
		const Ref<FSWorkspace> &p_workspace,
		const String &p_path,
		const ExtendFSParser *p_parser,
		const FSParser::SuiteNode *p_suite,
		const LSP::DocumentSymbol *p_target_symbol,
		int p_parameter_index,
		const FSParseResultProvider *p_parse_results,
		CallsiteParameterTypeState &r_state);

void collect_callsite_parameter_type_in_variable(
		const Ref<FSWorkspace> &p_workspace,
		const String &p_path,
		const ExtendFSParser *p_parser,
		const FSParser::VariableNode *p_variable,
		const LSP::DocumentSymbol *p_target_symbol,
		int p_parameter_index,
		const FSParseResultProvider *p_parse_results,
		CallsiteParameterTypeState &r_state) {
	if (p_variable == nullptr || r_state.failed) {
		return;
	}

	collect_callsite_parameter_type_in_expression(p_workspace, p_path, p_parser, p_variable->initializer, p_target_symbol, p_parameter_index, p_parse_results, r_state);
	if (p_variable->property != FSParser::VariableNode::PROP_INLINE) {
		return;
	}
	if (p_variable->setter != nullptr) {
		collect_callsite_parameter_type_in_suite(p_workspace, p_path, p_parser, p_variable->setter->body, p_target_symbol, p_parameter_index, p_parse_results, r_state);
	}
	if (p_variable->getter != nullptr) {
		collect_callsite_parameter_type_in_suite(p_workspace, p_path, p_parser, p_variable->getter->body, p_target_symbol, p_parameter_index, p_parse_results, r_state);
	}
}

void collect_callsite_parameter_type_from_call(
		const Ref<FSWorkspace> &p_workspace,
		const String &p_path,
		const ExtendFSParser *p_parser,
		const FSParser::CallNode *p_call,
		const LSP::DocumentSymbol *p_target_symbol,
		int p_parameter_index,
		const FSParseResultProvider *p_parse_results,
		CallsiteParameterTypeState &r_state) {
	if (r_state.failed || !call_resolves_to_symbol(p_workspace, p_path, p_parser, p_call, p_target_symbol, p_parse_results)) {
		return;
	}

	r_state.has_call = true;
	if (p_parameter_index < 0 || p_parameter_index >= p_call->arguments.size()) {
		r_state.failed = true;
		return;
	}

	const FSParser::DataType argument_type = p_call->arguments[p_parameter_index]->get_datatype();
	String rendered_type;
	if (!FSRefactorTypes::render_annotatable_type(argument_type, rendered_type)) {
		r_state.failed = true;
		return;
	}
	// Compare call sites by a scope-independent qualified identity, not the bare
	// rendering, so two same-named classes from different namespaces disagree.
	String identity;
	if (!FSRefactorTypes::render_qualified_identity(argument_type, identity)) {
		r_state.failed = true;
		return;
	}

	if (r_state.identity.is_empty()) {
		r_state.rendered_type = rendered_type;
		r_state.identity = identity;
		r_state.datatype = argument_type;
		r_state.has_datatype = true;
	} else if (r_state.identity != identity) {
		r_state.failed = true;
	}
}

void collect_callsite_parameter_type_in_expression(
		const Ref<FSWorkspace> &p_workspace,
		const String &p_path,
		const ExtendFSParser *p_parser,
		const FSParser::ExpressionNode *p_expression,
		const LSP::DocumentSymbol *p_target_symbol,
		int p_parameter_index,
		const FSParseResultProvider *p_parse_results,
		CallsiteParameterTypeState &r_state) {
	if (p_expression == nullptr || r_state.failed) {
		return;
	}

	switch (p_expression->type) {
		case FSParser::Node::ARRAY: {
			const FSParser::ArrayNode *array = static_cast<const FSParser::ArrayNode *>(p_expression);
			for (const FSParser::ExpressionNode *element : array->elements) {
				collect_callsite_parameter_type_in_expression(p_workspace, p_path, p_parser, element, p_target_symbol, p_parameter_index, p_parse_results, r_state);
			}
		} break;
		case FSParser::Node::TUPLE_LITERAL: {
			const FSParser::TupleLiteralNode *tuple_literal = static_cast<const FSParser::TupleLiteralNode *>(p_expression);
			for (const FSParser::ExpressionNode *element : tuple_literal->elements) {
				collect_callsite_parameter_type_in_expression(p_workspace, p_path, p_parser, element, p_target_symbol, p_parameter_index, p_parse_results, r_state);
			}
		} break;
		case FSParser::Node::ASSIGNMENT: {
			const FSParser::AssignmentNode *assignment = static_cast<const FSParser::AssignmentNode *>(p_expression);
			collect_callsite_parameter_type_in_expression(p_workspace, p_path, p_parser, assignment->assignee, p_target_symbol, p_parameter_index, p_parse_results, r_state);
			collect_callsite_parameter_type_in_expression(p_workspace, p_path, p_parser, assignment->assigned_value, p_target_symbol, p_parameter_index, p_parse_results, r_state);
		} break;
		case FSParser::Node::AWAIT:
			collect_callsite_parameter_type_in_expression(p_workspace, p_path, p_parser, static_cast<const FSParser::AwaitNode *>(p_expression)->to_await, p_target_symbol, p_parameter_index, p_parse_results, r_state);
			break;
		case FSParser::Node::BINARY_OPERATOR: {
			const FSParser::BinaryOpNode *binary = static_cast<const FSParser::BinaryOpNode *>(p_expression);
			collect_callsite_parameter_type_in_expression(p_workspace, p_path, p_parser, binary->left_operand, p_target_symbol, p_parameter_index, p_parse_results, r_state);
			collect_callsite_parameter_type_in_expression(p_workspace, p_path, p_parser, binary->right_operand, p_target_symbol, p_parameter_index, p_parse_results, r_state);
		} break;
		case FSParser::Node::CALL: {
			const FSParser::CallNode *call = static_cast<const FSParser::CallNode *>(p_expression);
			collect_callsite_parameter_type_from_call(p_workspace, p_path, p_parser, call, p_target_symbol, p_parameter_index, p_parse_results, r_state);
			collect_callsite_parameter_type_in_expression(p_workspace, p_path, p_parser, call->callee, p_target_symbol, p_parameter_index, p_parse_results, r_state);
			for (const FSParser::ExpressionNode *argument : call->arguments) {
				collect_callsite_parameter_type_in_expression(p_workspace, p_path, p_parser, argument, p_target_symbol, p_parameter_index, p_parse_results, r_state);
			}
		} break;
		case FSParser::Node::CAST:
			collect_callsite_parameter_type_in_expression(p_workspace, p_path, p_parser, static_cast<const FSParser::CastNode *>(p_expression)->operand, p_target_symbol, p_parameter_index, p_parse_results, r_state);
			break;
		case FSParser::Node::DICTIONARY: {
			const FSParser::DictionaryNode *dictionary = static_cast<const FSParser::DictionaryNode *>(p_expression);
			for (const FSParser::DictionaryNode::Pair &pair : dictionary->elements) {
				collect_callsite_parameter_type_in_expression(p_workspace, p_path, p_parser, pair.key, p_target_symbol, p_parameter_index, p_parse_results, r_state);
				collect_callsite_parameter_type_in_expression(p_workspace, p_path, p_parser, pair.value, p_target_symbol, p_parameter_index, p_parse_results, r_state);
			}
		} break;
		case FSParser::Node::LAMBDA:
			if (static_cast<const FSParser::LambdaNode *>(p_expression)->function != nullptr) {
				collect_callsite_parameter_type_in_suite(p_workspace, p_path, p_parser, static_cast<const FSParser::LambdaNode *>(p_expression)->function->body, p_target_symbol, p_parameter_index, p_parse_results, r_state);
			}
			break;
		case FSParser::Node::PRELOAD:
			collect_callsite_parameter_type_in_expression(p_workspace, p_path, p_parser, static_cast<const FSParser::PreloadNode *>(p_expression)->path, p_target_symbol, p_parameter_index, p_parse_results, r_state);
			break;
		case FSParser::Node::SUBSCRIPT: {
			const FSParser::SubscriptNode *subscript = static_cast<const FSParser::SubscriptNode *>(p_expression);
			collect_callsite_parameter_type_in_expression(p_workspace, p_path, p_parser, subscript->base, p_target_symbol, p_parameter_index, p_parse_results, r_state);
			if (!subscript->is_attribute) {
				collect_callsite_parameter_type_in_expression(p_workspace, p_path, p_parser, subscript->index, p_target_symbol, p_parameter_index, p_parse_results, r_state);
			}
		} break;
		case FSParser::Node::TERNARY_OPERATOR: {
			const FSParser::TernaryOpNode *ternary = static_cast<const FSParser::TernaryOpNode *>(p_expression);
			collect_callsite_parameter_type_in_expression(p_workspace, p_path, p_parser, ternary->condition, p_target_symbol, p_parameter_index, p_parse_results, r_state);
			collect_callsite_parameter_type_in_expression(p_workspace, p_path, p_parser, ternary->true_expr, p_target_symbol, p_parameter_index, p_parse_results, r_state);
			collect_callsite_parameter_type_in_expression(p_workspace, p_path, p_parser, ternary->false_expr, p_target_symbol, p_parameter_index, p_parse_results, r_state);
		} break;
		case FSParser::Node::TYPE_TEST:
			collect_callsite_parameter_type_in_expression(p_workspace, p_path, p_parser, static_cast<const FSParser::TypeTestNode *>(p_expression)->operand, p_target_symbol, p_parameter_index, p_parse_results, r_state);
			break;
		case FSParser::Node::UNARY_OPERATOR:
			collect_callsite_parameter_type_in_expression(p_workspace, p_path, p_parser, static_cast<const FSParser::UnaryOpNode *>(p_expression)->operand, p_target_symbol, p_parameter_index, p_parse_results, r_state);
			break;
		default:
			break;
	}
}

void collect_callsite_parameter_type_in_node(
		const Ref<FSWorkspace> &p_workspace,
		const String &p_path,
		const ExtendFSParser *p_parser,
		const FSParser::Node *p_node,
		const LSP::DocumentSymbol *p_target_symbol,
		int p_parameter_index,
		const FSParseResultProvider *p_parse_results,
		CallsiteParameterTypeState &r_state) {
	if (p_node == nullptr || r_state.failed) {
		return;
	}
	if (p_node->is_expression()) {
		collect_callsite_parameter_type_in_expression(p_workspace, p_path, p_parser, static_cast<const FSParser::ExpressionNode *>(p_node), p_target_symbol, p_parameter_index, p_parse_results, r_state);
		return;
	}

	switch (p_node->type) {
		case FSParser::Node::ASSERT: {
			const FSParser::AssertNode *assert_node = static_cast<const FSParser::AssertNode *>(p_node);
			collect_callsite_parameter_type_in_expression(p_workspace, p_path, p_parser, assert_node->condition, p_target_symbol, p_parameter_index, p_parse_results, r_state);
			collect_callsite_parameter_type_in_expression(p_workspace, p_path, p_parser, assert_node->message, p_target_symbol, p_parameter_index, p_parse_results, r_state);
		} break;
		case FSParser::Node::CONSTANT:
			collect_callsite_parameter_type_in_expression(p_workspace, p_path, p_parser, static_cast<const FSParser::ConstantNode *>(p_node)->initializer, p_target_symbol, p_parameter_index, p_parse_results, r_state);
			break;
		case FSParser::Node::FOR: {
			const FSParser::ForNode *for_node = static_cast<const FSParser::ForNode *>(p_node);
			collect_callsite_parameter_type_in_expression(p_workspace, p_path, p_parser, for_node->list, p_target_symbol, p_parameter_index, p_parse_results, r_state);
			collect_callsite_parameter_type_in_suite(p_workspace, p_path, p_parser, for_node->loop, p_target_symbol, p_parameter_index, p_parse_results, r_state);
		} break;
		case FSParser::Node::IF: {
			const FSParser::IfNode *if_node = static_cast<const FSParser::IfNode *>(p_node);
			collect_callsite_parameter_type_in_expression(p_workspace, p_path, p_parser, if_node->condition, p_target_symbol, p_parameter_index, p_parse_results, r_state);
			collect_callsite_parameter_type_in_suite(p_workspace, p_path, p_parser, if_node->true_block, p_target_symbol, p_parameter_index, p_parse_results, r_state);
			collect_callsite_parameter_type_in_suite(p_workspace, p_path, p_parser, if_node->false_block, p_target_symbol, p_parameter_index, p_parse_results, r_state);
		} break;
		case FSParser::Node::MATCH: {
			const FSParser::MatchNode *match_node = static_cast<const FSParser::MatchNode *>(p_node);
			collect_callsite_parameter_type_in_expression(p_workspace, p_path, p_parser, match_node->test, p_target_symbol, p_parameter_index, p_parse_results, r_state);
			for (const FSParser::MatchBranchNode *branch : match_node->branches) {
				collect_callsite_parameter_type_in_suite(p_workspace, p_path, p_parser, branch->guard_body, p_target_symbol, p_parameter_index, p_parse_results, r_state);
				collect_callsite_parameter_type_in_suite(p_workspace, p_path, p_parser, branch->block, p_target_symbol, p_parameter_index, p_parse_results, r_state);
			}
		} break;
		case FSParser::Node::RETURN:
			collect_callsite_parameter_type_in_expression(p_workspace, p_path, p_parser, static_cast<const FSParser::ReturnNode *>(p_node)->return_value, p_target_symbol, p_parameter_index, p_parse_results, r_state);
			break;
		case FSParser::Node::VARIABLE:
			collect_callsite_parameter_type_in_variable(p_workspace, p_path, p_parser, static_cast<const FSParser::VariableNode *>(p_node), p_target_symbol, p_parameter_index, p_parse_results, r_state);
			break;
		case FSParser::Node::WHILE: {
			const FSParser::WhileNode *while_node = static_cast<const FSParser::WhileNode *>(p_node);
			collect_callsite_parameter_type_in_expression(p_workspace, p_path, p_parser, while_node->condition, p_target_symbol, p_parameter_index, p_parse_results, r_state);
			collect_callsite_parameter_type_in_suite(p_workspace, p_path, p_parser, while_node->loop, p_target_symbol, p_parameter_index, p_parse_results, r_state);
		} break;
		default:
			break;
	}
}

void collect_callsite_parameter_type_in_suite(
		const Ref<FSWorkspace> &p_workspace,
		const String &p_path,
		const ExtendFSParser *p_parser,
		const FSParser::SuiteNode *p_suite,
		const LSP::DocumentSymbol *p_target_symbol,
		int p_parameter_index,
		const FSParseResultProvider *p_parse_results,
		CallsiteParameterTypeState &r_state) {
	if (p_suite == nullptr || r_state.failed) {
		return;
	}
	for (const FSParser::Node *statement : p_suite->statements) {
		collect_callsite_parameter_type_in_node(p_workspace, p_path, p_parser, statement, p_target_symbol, p_parameter_index, p_parse_results, r_state);
	}
}

void collect_callsite_parameter_type_in_class(
		const Ref<FSWorkspace> &p_workspace,
		const String &p_path,
		const ExtendFSParser *p_parser,
		const FSParser::ClassNode *p_class,
		const LSP::DocumentSymbol *p_target_symbol,
		int p_parameter_index,
		const FSParseResultProvider *p_parse_results,
		CallsiteParameterTypeState &r_state) {
	if (p_class == nullptr || r_state.failed) {
		return;
	}

	for (const FSParser::ClassNode::Member &member : p_class->members) {
		switch (member.type) {
			case FSParser::ClassNode::Member::FUNCTION:
				if (member.function != nullptr) {
					collect_callsite_parameter_type_in_suite(p_workspace, p_path, p_parser, member.function->body, p_target_symbol, p_parameter_index, p_parse_results, r_state);
				}
				break;
			case FSParser::ClassNode::Member::CLASS:
				collect_callsite_parameter_type_in_class(p_workspace, p_path, p_parser, member.m_class, p_target_symbol, p_parameter_index, p_parse_results, r_state);
				break;
			case FSParser::ClassNode::Member::CONSTANT:
				collect_callsite_parameter_type_in_expression(p_workspace, p_path, p_parser, member.constant->initializer, p_target_symbol, p_parameter_index, p_parse_results, r_state);
				break;
			case FSParser::ClassNode::Member::VARIABLE:
				collect_callsite_parameter_type_in_variable(p_workspace, p_path, p_parser, member.variable, p_target_symbol, p_parameter_index, p_parse_results, r_state);
				break;
			default:
				break;
		}
	}
}

bool class_hierarchy_has_function(const FSParser::ClassNode *p_class, const StringName &p_function_name) {
	ERR_FAIL_NULL_V(p_class, false);

	const FSParser::ClassNode *base_class = p_class->base_type.class_type;
	while (base_class != nullptr) {
		if (base_class->has_member(p_function_name) &&
				base_class->get_member(p_function_name).type == FSParser::ClassNode::Member::FUNCTION) {
			return true;
		}
		base_class = base_class->base_type.class_type;
	}

	Ref<Script> base_script = p_class->base_type.script_type;
	while (base_script.is_valid()) {
		if (base_script->has_method(p_function_name)) {
			return true;
		}
		base_script = base_script->get_base_script();
	}

	StringName native_base = p_class->base_type.native_type;
	while (native_base != StringName()) {
		if (ClassDB::has_method(native_base, p_function_name, true)) {
			return true;
		}
		native_base = ClassDB::get_parent_class(native_base);
	}

	return false;
}

bool infer_parameter_type_from_call_sites(
		const FSParser::ClassNode *p_class,
		const FSParser::FunctionNode *p_function,
		int p_parameter_index,
		const Ref<FSWorkspace> &p_workspace,
		const ExtendFSParser *p_target_parser,
		const FSParseResultProvider *p_parse_results,
		String &r_rendered_type,
		FSParser::DataType &r_datatype,
		bool &r_has_datatype) {
	ERR_FAIL_COND_V(p_workspace.is_null(), false);
	ERR_FAIL_NULL_V(p_class, false);
	ERR_FAIL_NULL_V(p_function, false);
	ERR_FAIL_NULL_V(p_target_parser, false);
	ERR_FAIL_NULL_V(p_parse_results, false);

	if (p_function->identifier == nullptr || p_function->identifier->name == StringName()) {
		return false;
	}
	const String function_name = String(p_function->identifier->name);
	if (function_name.begins_with("_")) {
		return false;
	}
	if (class_hierarchy_has_function(p_class, p_function->identifier->name)) {
		return false;
	}

	const LSP::DocumentSymbol *target_symbol = p_target_parser->get_symbol_defined_at_line(
			LINE_NUMBER_TO_INDEX(p_function->start_line),
			function_name);
	if (target_symbol == nullptr || !target_symbol->native_class.is_empty()) {
		return false;
	}

	CallsiteParameterTypeState state;
	List<String> paths;
	p_workspace->list_project_script_files(paths);
	for (const String &path : paths) {
		// A file whose source never names the function cannot call it; skip the parse.
		if (!p_workspace->source_may_reference_symbol(path, function_name, p_parse_results)) {
			continue;
		}
		const ExtendFSParser *parser = p_parse_results->get_parse_result(path);
		if (parser == nullptr || parser->parse_result != OK) {
			continue;
		}

		const FSParser::ClassNode *tree = parser->get_tree();
		if (tree == nullptr) {
			continue;
		}

		const Vector<String> &lines = parser->get_lines();
		for (int i = 0; i < lines.size(); i++) {
			if (find_dynamic_string_reference_column(lines[i], function_name) >= 0) {
				return false;
			}
		}

		collect_callsite_parameter_type_in_class(p_workspace, path, parser, tree, target_symbol, p_parameter_index, p_parse_results, state);

		if (state.failed) {
			return false;
		}
	}

	if (!state.has_call || state.rendered_type.is_empty()) {
		return false;
	}

	r_rendered_type = state.rendered_type;
	r_datatype = state.datatype;
	r_has_datatype = state.has_datatype;
	return true;
}

void apply_callsite_parameter_type_annotation(
		const FSParser::ClassNode *p_class,
		const FSParser::FunctionNode *p_function,
		const FSParser::ParameterNode *p_parameter,
		int p_parameter_index,
		const Ref<FSWorkspace> &p_workspace,
		const ExtendFSParser *p_target_parser,
		const FSParseResultProvider *p_parse_results,
		TypeAnnotationCandidate &r_candidate,
		const TypeAnnotationRenderContext *p_render_context) {
	if (!r_candidate.matched || r_candidate.enabled || p_parameter == nullptr ||
			p_parameter->datatype_specifier != nullptr || p_parameter->initializer != nullptr) {
		return;
	}
	if (p_workspace.is_null() || p_target_parser == nullptr || p_parse_results == nullptr) {
		return;
	}

	String rendered_type;
	FSParser::DataType inferred_type;
	bool has_inferred_type = false;
	if (!infer_parameter_type_from_call_sites(p_class, p_function, p_parameter_index, p_workspace, p_target_parser, p_parse_results, rendered_type, inferred_type, has_inferred_type)) {
		r_candidate.disabled_reason = "Cannot infer a type for this parameter from resolved call sites.";
		return;
	}

	// Render namespace-aware so a cross-namespace parameter type is spelled to
	// resolve at the insertion site, matching the other annotation sites. When the
	// scoped spelling cannot resolve there (e.g. a namespace rooted at a native
	// name), disable the candidate rather than emitting the unscoped bare name.
	if (p_render_context != nullptr && has_inferred_type) {
		String scoped_rendered;
		HashSet<String> required_imports;
		if (!FSRefactorTypes::render_annotatable_type_in_scope(inferred_type, p_render_context->scope, scoped_rendered, required_imports)) {
			r_candidate.disabled_reason = "The inferred type cannot be written as an explicit annotation.";
			return;
		}
		rendered_type = scoped_rendered;
	}

	r_candidate.edit.start_line = r_candidate.line;
	r_candidate.edit.start_column = r_candidate.caret_span_end;
	r_candidate.edit.end_line = r_candidate.line;
	r_candidate.edit.end_column = r_candidate.caret_span_end;
	r_candidate.edit.new_text = ": " + rendered_type;
	r_candidate.enabled = true;
	r_candidate.disabled_reason = String();
}
#endif // FOUNDRY_SCRIPT_NO_LSP

bool find_extract_variable_in_suite(const RefactorLocation &p_location, const Vector<String> &p_lines, const FSParser::SuiteNode *p_suite, ExtractVariableCandidate &r_candidate);

bool extract_method_source_before(const FSParser::IdentifierNode *p_identifier, int p_selection_start_line) {
	if (p_identifier == nullptr) {
		return false;
	}

	int start_line = -1;
	switch (p_identifier->source) {
		case FSParser::IdentifierNode::FUNCTION_PARAMETER:
			start_line = get_node_start_line_0(p_identifier->parameter_source);
			break;
		case FSParser::IdentifierNode::LOCAL_VARIABLE:
			start_line = get_node_start_line_0(p_identifier->variable_source);
			break;
		case FSParser::IdentifierNode::LOCAL_CONSTANT:
			start_line = get_node_start_line_0(p_identifier->constant_source);
			break;
		case FSParser::IdentifierNode::LOCAL_ITERATOR:
		case FSParser::IdentifierNode::LOCAL_BIND:
			start_line = get_node_start_line_0(p_identifier->bind_source);
			break;
		default:
			break;
	}
	return start_line >= 0 && start_line < p_selection_start_line;
}

bool extract_method_source_inside(
		const FSParser::IdentifierNode *p_identifier,
		const RefactorLocation &p_location) {
	if (p_identifier == nullptr) {
		return false;
	}

	int start_line = -1;
	switch (p_identifier->source) {
		case FSParser::IdentifierNode::LOCAL_VARIABLE:
			start_line = get_node_start_line_0(p_identifier->variable_source);
			break;
		case FSParser::IdentifierNode::LOCAL_CONSTANT:
			start_line = get_node_start_line_0(p_identifier->constant_source);
			break;
		case FSParser::IdentifierNode::LOCAL_ITERATOR:
		case FSParser::IdentifierNode::LOCAL_BIND:
			start_line = get_node_start_line_0(p_identifier->bind_source);
			break;
		default:
			break;
	}
	return start_line >= p_location.start_line && start_line < p_location.end_line;
}

bool extract_method_is_local_like_identifier(const FSParser::IdentifierNode *p_identifier) {
	if (p_identifier == nullptr) {
		return false;
	}

	switch (p_identifier->source) {
		case FSParser::IdentifierNode::FUNCTION_PARAMETER:
		case FSParser::IdentifierNode::LOCAL_VARIABLE:
		case FSParser::IdentifierNode::LOCAL_CONSTANT:
		case FSParser::IdentifierNode::LOCAL_ITERATOR:
		case FSParser::IdentifierNode::LOCAL_BIND:
			return true;
		default:
			return false;
	}
}

struct ExtractMethodReadState {
	Vector<ExtractMethodParameter> parameters;
	Vector<ExtractMethodLocal> declared_inside;
	Vector<StringName> *reads_after = nullptr;
	String disabled_reason;
};

void collect_extract_method_expr_reads(
		const FSParser::ExpressionNode *p_expression,
		const RefactorLocation &p_location,
		ExtractMethodReadState &r_state);

void collect_extract_method_stmt_reads(
		const FSParser::Node *p_statement,
		const RefactorLocation &p_location,
		ExtractMethodReadState &r_state);

void collect_extract_method_suite_reads(
		const FSParser::SuiteNode *p_suite,
		const RefactorLocation &p_location,
		ExtractMethodReadState &r_state) {
	if (p_suite == nullptr || !r_state.disabled_reason.is_empty()) {
		return;
	}

	for (const FSParser::Node *statement : p_suite->statements) {
		collect_extract_method_stmt_reads(statement, p_location, r_state);
		if (!r_state.disabled_reason.is_empty()) {
			return;
		}
	}
}

void collect_extract_method_identifier_read(
		const FSParser::IdentifierNode *p_identifier,
		const RefactorLocation &p_location,
		ExtractMethodReadState &r_state) {
	if (!extract_method_is_local_like_identifier(p_identifier)) {
		return;
	}

	if (r_state.reads_after != nullptr) {
		if (extract_method_source_inside(p_identifier, p_location)) {
			extract_method_add_name(*r_state.reads_after, p_identifier->name);
		}
		return;
	}

	if (!extract_method_source_before(p_identifier, p_location.start_line) ||
			extract_method_has_param(r_state.parameters, p_identifier->name)) {
		return;
	}

	ExtractMethodParameter parameter;
	parameter.name = p_identifier->name;
	parameter.datatype = p_identifier->get_datatype();
	r_state.parameters.push_back(parameter);
}

void collect_extract_method_expr_reads(
		const FSParser::ExpressionNode *p_expression,
		const RefactorLocation &p_location,
		ExtractMethodReadState &r_state) {
	if (p_expression == nullptr || !r_state.disabled_reason.is_empty()) {
		return;
	}

	switch (p_expression->type) {
		case FSParser::Node::AWAIT:
			r_state.disabled_reason = "Cannot extract statements containing await.";
			return;
		case FSParser::Node::IDENTIFIER:
			collect_extract_method_identifier_read(
					static_cast<const FSParser::IdentifierNode *>(p_expression),
					p_location,
					r_state);
			return;
		case FSParser::Node::ARRAY: {
			const FSParser::ArrayNode *array = static_cast<const FSParser::ArrayNode *>(p_expression);
			for (const FSParser::ExpressionNode *element : array->elements) {
				collect_extract_method_expr_reads(element, p_location, r_state);
			}
		} break;
		case FSParser::Node::TUPLE_LITERAL: {
			const FSParser::TupleLiteralNode *tuple_literal = static_cast<const FSParser::TupleLiteralNode *>(p_expression);
			for (const FSParser::ExpressionNode *element : tuple_literal->elements) {
				collect_extract_method_expr_reads(element, p_location, r_state);
			}
		} break;
		case FSParser::Node::ASSIGNMENT: {
			const FSParser::AssignmentNode *assignment = static_cast<const FSParser::AssignmentNode *>(p_expression);
			if (assignment->operation != FSParser::AssignmentNode::OP_NONE) {
				collect_extract_method_expr_reads(assignment->assignee, p_location, r_state);
			}
			collect_extract_method_expr_reads(assignment->assigned_value, p_location, r_state);
		} break;
		case FSParser::Node::BINARY_OPERATOR: {
			const FSParser::BinaryOpNode *binary = static_cast<const FSParser::BinaryOpNode *>(p_expression);
			collect_extract_method_expr_reads(binary->left_operand, p_location, r_state);
			collect_extract_method_expr_reads(binary->right_operand, p_location, r_state);
		} break;
		case FSParser::Node::CALL: {
			const FSParser::CallNode *call = static_cast<const FSParser::CallNode *>(p_expression);
			collect_extract_method_expr_reads(call->callee, p_location, r_state);
			for (const FSParser::ExpressionNode *argument : call->arguments) {
				collect_extract_method_expr_reads(argument, p_location, r_state);
			}
		} break;
		case FSParser::Node::CAST: {
			const FSParser::CastNode *cast = static_cast<const FSParser::CastNode *>(p_expression);
			collect_extract_method_expr_reads(cast->operand, p_location, r_state);
		} break;
		case FSParser::Node::DICTIONARY: {
			const FSParser::DictionaryNode *dictionary = static_cast<const FSParser::DictionaryNode *>(p_expression);
			for (const FSParser::DictionaryNode::Pair &pair : dictionary->elements) {
				collect_extract_method_expr_reads(pair.key, p_location, r_state);
				collect_extract_method_expr_reads(pair.value, p_location, r_state);
			}
		} break;
		case FSParser::Node::GET_NODE:
			break;
		case FSParser::Node::LAMBDA:
			r_state.disabled_reason = "Cannot extract statements containing a lambda.";
			return;
		case FSParser::Node::LITERAL:
			break;
		case FSParser::Node::PRELOAD:
			break;
		case FSParser::Node::SELF:
			break;
		case FSParser::Node::SUBSCRIPT: {
			const FSParser::SubscriptNode *subscript = static_cast<const FSParser::SubscriptNode *>(p_expression);
			collect_extract_method_expr_reads(subscript->base, p_location, r_state);
			if (!subscript->is_attribute) {
				collect_extract_method_expr_reads(subscript->index, p_location, r_state);
			}
		} break;
		case FSParser::Node::TERNARY_OPERATOR: {
			const FSParser::TernaryOpNode *ternary = static_cast<const FSParser::TernaryOpNode *>(p_expression);
			collect_extract_method_expr_reads(ternary->condition, p_location, r_state);
			collect_extract_method_expr_reads(ternary->true_expr, p_location, r_state);
			collect_extract_method_expr_reads(ternary->false_expr, p_location, r_state);
		} break;
		case FSParser::Node::TYPE_TEST: {
			const FSParser::TypeTestNode *type_test = static_cast<const FSParser::TypeTestNode *>(p_expression);
			collect_extract_method_expr_reads(type_test->operand, p_location, r_state);
		} break;
		case FSParser::Node::UNARY_OPERATOR: {
			const FSParser::UnaryOpNode *unary = static_cast<const FSParser::UnaryOpNode *>(p_expression);
			collect_extract_method_expr_reads(unary->operand, p_location, r_state);
		} break;
		default:
			break;
	}
}

void collect_extract_method_stmt_reads(
		const FSParser::Node *p_statement,
		const RefactorLocation &p_location,
		ExtractMethodReadState &r_state) {
	if (p_statement == nullptr || !r_state.disabled_reason.is_empty()) {
		return;
	}

	switch (p_statement->type) {
		case FSParser::Node::RETURN:
			r_state.disabled_reason = "Cannot extract statements containing return.";
			return;
		case FSParser::Node::BREAK:
			r_state.disabled_reason = "Cannot extract statements containing break.";
			return;
		case FSParser::Node::CONTINUE:
			r_state.disabled_reason = "Cannot extract statements containing continue.";
			return;
		case FSParser::Node::AWAIT:
			r_state.disabled_reason = "Cannot extract statements containing await.";
			return;
		case FSParser::Node::VARIABLE: {
			const FSParser::VariableNode *variable = static_cast<const FSParser::VariableNode *>(p_statement);
			if (r_state.reads_after == nullptr && variable->identifier != nullptr) {
				extract_method_add_local(r_state.declared_inside, variable->identifier->name, variable->get_datatype());
			}
			collect_extract_method_expr_reads(variable->initializer, p_location, r_state);
		} break;
		case FSParser::Node::CONSTANT: {
			const FSParser::ConstantNode *constant = static_cast<const FSParser::ConstantNode *>(p_statement);
			if (r_state.reads_after == nullptr && constant->identifier != nullptr) {
				extract_method_add_local(r_state.declared_inside, constant->identifier->name, constant->get_datatype());
			}
			collect_extract_method_expr_reads(constant->initializer, p_location, r_state);
		} break;
		case FSParser::Node::ASSIGNMENT:
			collect_extract_method_expr_reads(
					static_cast<const FSParser::AssignmentNode *>(p_statement),
					p_location,
					r_state);
			break;
		case FSParser::Node::ASSERT: {
			const FSParser::AssertNode *assert_node = static_cast<const FSParser::AssertNode *>(p_statement);
			collect_extract_method_expr_reads(assert_node->condition, p_location, r_state);
			collect_extract_method_expr_reads(assert_node->message, p_location, r_state);
		} break;
		case FSParser::Node::IF: {
			const FSParser::IfNode *if_node = static_cast<const FSParser::IfNode *>(p_statement);
			collect_extract_method_expr_reads(if_node->condition, p_location, r_state);
			collect_extract_method_suite_reads(if_node->true_block, p_location, r_state);
			collect_extract_method_suite_reads(if_node->false_block, p_location, r_state);
		} break;
		case FSParser::Node::FOR: {
			const FSParser::ForNode *for_node = static_cast<const FSParser::ForNode *>(p_statement);
			collect_extract_method_expr_reads(for_node->list, p_location, r_state);
			collect_extract_method_suite_reads(for_node->loop, p_location, r_state);
		} break;
		case FSParser::Node::WHILE: {
			const FSParser::WhileNode *while_node = static_cast<const FSParser::WhileNode *>(p_statement);
			collect_extract_method_expr_reads(while_node->condition, p_location, r_state);
			collect_extract_method_suite_reads(while_node->loop, p_location, r_state);
		} break;
		case FSParser::Node::MATCH: {
			const FSParser::MatchNode *match_node = static_cast<const FSParser::MatchNode *>(p_statement);
			collect_extract_method_expr_reads(match_node->test, p_location, r_state);
			for (const FSParser::MatchBranchNode *branch : match_node->branches) {
				if (branch != nullptr) {
					collect_extract_method_suite_reads(branch->block, p_location, r_state);
				}
			}
		} break;
		default:
			if (p_statement->is_expression()) {
				collect_extract_method_expr_reads(
						static_cast<const FSParser::ExpressionNode *>(p_statement),
						p_location,
						r_state);
			}
			break;
	}
}

void collect_extract_method_reads_after(
		const FSParser::SuiteNode *p_suite,
		const RefactorLocation &p_location,
		Vector<StringName> &r_reads_after,
		String &r_disabled_reason) {
	if (p_suite == nullptr || !r_disabled_reason.is_empty()) {
		return;
	}

	ExtractMethodReadState state;
	state.reads_after = &r_reads_after;
	for (const FSParser::Node *statement : p_suite->statements) {
		if (statement == nullptr) {
			continue;
		}
		if (get_node_start_line_0(statement) >= p_location.end_line) {
			collect_extract_method_stmt_reads(statement, p_location, state);
			if (!state.disabled_reason.is_empty()) {
				r_disabled_reason = state.disabled_reason;
				return;
			}
		}
	}
}

void collect_extract_method_external_assigns_in_suite(
		const FSParser::SuiteNode *p_suite,
		const RefactorLocation &p_location,
		Vector<StringName> &r_assigned_existing);

void collect_extract_method_external_assign(
		const FSParser::ExpressionNode *p_expression,
		const RefactorLocation &p_location,
		Vector<StringName> &r_assigned_existing) {
	if (p_expression == nullptr) {
		return;
	}
	if (p_expression->type != FSParser::Node::IDENTIFIER) {
		return;
	}

	const FSParser::IdentifierNode *identifier = static_cast<const FSParser::IdentifierNode *>(p_expression);
	if (!extract_method_source_inside(identifier, p_location)) {
		extract_method_add_name(r_assigned_existing, identifier->name);
	}
}

void collect_extract_method_external_assigns_in_statement(
		const FSParser::Node *p_statement,
		const RefactorLocation &p_location,
		Vector<StringName> &r_assigned_existing) {
	if (p_statement == nullptr) {
		return;
	}

	switch (p_statement->type) {
		case FSParser::Node::ASSIGNMENT: {
			const FSParser::AssignmentNode *assignment = static_cast<const FSParser::AssignmentNode *>(p_statement);
			collect_extract_method_external_assign(assignment->assignee, p_location, r_assigned_existing);
		} break;
		case FSParser::Node::IF: {
			const FSParser::IfNode *if_node = static_cast<const FSParser::IfNode *>(p_statement);
			collect_extract_method_external_assigns_in_suite(if_node->true_block, p_location, r_assigned_existing);
			collect_extract_method_external_assigns_in_suite(if_node->false_block, p_location, r_assigned_existing);
		} break;
		case FSParser::Node::FOR:
			collect_extract_method_external_assigns_in_suite(
					static_cast<const FSParser::ForNode *>(p_statement)->loop,
					p_location,
					r_assigned_existing);
			break;
		case FSParser::Node::WHILE:
			collect_extract_method_external_assigns_in_suite(
					static_cast<const FSParser::WhileNode *>(p_statement)->loop,
					p_location,
					r_assigned_existing);
			break;
		case FSParser::Node::MATCH: {
			const FSParser::MatchNode *match_node = static_cast<const FSParser::MatchNode *>(p_statement);
			for (const FSParser::MatchBranchNode *branch : match_node->branches) {
				if (branch != nullptr) {
					collect_extract_method_external_assigns_in_suite(branch->block, p_location, r_assigned_existing);
				}
			}
		} break;
		default:
			break;
	}
}

void collect_extract_method_external_assigns_in_suite(
		const FSParser::SuiteNode *p_suite,
		const RefactorLocation &p_location,
		Vector<StringName> &r_assigned_existing) {
	if (p_suite == nullptr) {
		return;
	}

	for (const FSParser::Node *statement : p_suite->statements) {
		collect_extract_method_external_assigns_in_statement(statement, p_location, r_assigned_existing);
	}
}

bool find_extract_method_range(
		const RefactorLocation &p_location,
		const Vector<String> &p_lines,
		const FSParser::SuiteNode *p_suite,
		RefactorLocation &r_normalized_location,
		int &r_first_statement,
		int &r_last_statement) {
	if (p_suite == nullptr || !p_location.has_selection() || p_location.start_line < 0 ||
			p_location.start_line >= p_lines.size() || p_location.end_line < 0 ||
			p_location.end_line > p_lines.size() || p_location.start_column < 0 ||
			p_location.end_column < 0) {
		return false;
	}

	const int start_line_first_text_column = get_leading_whitespace(p_lines[p_location.start_line]).length();
	if (p_location.start_column > start_line_first_text_column) {
		return false;
	}

	int end_line_exclusive = p_location.end_line;
	if (p_location.end_column != 0) {
		// The selection must cover the final statement text; trailing whitespace is allowed.
		if (p_location.end_line >= p_lines.size() ||
				p_location.end_column > p_lines[p_location.end_line].length() ||
				p_location.end_column < get_trailing_whitespace_start_column(p_lines[p_location.end_line])) {
			return false;
		}
		end_line_exclusive = p_location.end_line + 1;
	}

	if (end_line_exclusive <= p_location.start_line) {
		return false;
	}

	r_first_statement = -1;
	r_last_statement = -1;
	for (int i = 0; i < p_suite->statements.size(); i++) {
		const FSParser::Node *statement = p_suite->statements[i];
		if (statement == nullptr) {
			continue;
		}
		if (get_node_start_line_0(statement) == p_location.start_line) {
			r_first_statement = i;
		}
		if (get_node_end_line_exclusive_0(statement) == end_line_exclusive) {
			r_last_statement = i;
		}
	}

	if (r_first_statement < 0 || r_last_statement < r_first_statement) {
		return false;
	}

	r_normalized_location = p_location;
	r_normalized_location.start_column = 0;
	r_normalized_location.end_line = end_line_exclusive;
	r_normalized_location.end_column = 0;
	return true;
}

String make_unique_method_name(const FSParser::ClassNode *p_class) {
	const String base = "_extracted_method";
	String name = base;
	int suffix = 2;
	String reason;
	while (!FSRefactorNames::validate_identifier(name, reason) ||
			(p_class != nullptr && p_class->has_member(StringName(name)))) {
		name = vformat("%s_%d", base, suffix);
		suffix++;
		if (suffix > 1000) {
			return String();
		}
	}
	return name;
}

Vector<String> collect_extract_method_member_names(const FSParser::ClassNode *p_class) {
	Vector<String> names;
	if (p_class == nullptr) {
		return names;
	}

	for (const FSParser::ClassNode::Member &member : p_class->members) {
		// Export group/category labels are editor metadata, not script member identifiers.
		if (member.type == FSParser::ClassNode::Member::GROUP) {
			continue;
		}
		names.push_back(member.get_name());
	}
	return names;
}

String resolve_extract_method_name(
		const FSParser::ClassNode *p_class,
		const Vector<String> &p_existing_member_names,
		const String &p_requested_name,
		String &r_disabled_reason) {
	if (p_requested_name.is_empty()) {
		const String generated_name = make_unique_method_name(p_class);
		if (generated_name.is_empty()) {
			r_disabled_reason = "Cannot suggest a safe method name.";
		}
		return generated_name;
	}

	if (!FSRefactoring::validate_extract_method_name(p_existing_member_names, p_requested_name, r_disabled_reason)) {
		return String();
	}

	r_disabled_reason = String();
	return p_requested_name;
}

String build_extract_method_body(
		const Vector<String> &p_lines,
		int p_start_line,
		int p_end_line,
		const String &p_remove_indent,
		const String &p_add_indent) {
	String body;
	for (int line_index = p_start_line; line_index < p_end_line && line_index < p_lines.size(); line_index++) {
		const String line = p_lines[line_index];
		if (line.strip_edges().is_empty()) {
			body += "\n";
			continue;
		}
		if (!p_remove_indent.is_empty() && line.begins_with(p_remove_indent)) {
			body += p_add_indent + line.substr(p_remove_indent.length()) + "\n";
		} else {
			body += p_add_indent + line + "\n";
		}
	}
	return body;
}

bool extract_method_position_is_eof(
		const Vector<String> &p_lines,
		bool p_source_has_final_newline,
		int p_line) {
	return !p_source_has_final_newline && p_line == p_lines.size() && p_line > 0;
}

void set_extract_method_edit_line_end(
		RefactorTextEdit &r_edit,
		const Vector<String> &p_lines,
		bool p_source_has_final_newline,
		int p_line) {
	if (extract_method_position_is_eof(p_lines, p_source_has_final_newline, p_line)) {
		r_edit.end_line = p_line - 1;
		r_edit.end_column = p_lines[p_line - 1].length();
		return;
	}

	r_edit.end_line = p_line;
	r_edit.end_column = 0;
}

void set_extract_method_insertion(
		RefactorTextEdit &r_edit,
		const Vector<String> &p_lines,
		bool p_source_has_final_newline,
		int p_line,
		const String &p_method_text) {
	if (extract_method_position_is_eof(p_lines, p_source_has_final_newline, p_line)) {
		r_edit.start_line = p_line - 1;
		r_edit.start_column = p_lines[p_line - 1].length();
		r_edit.end_line = r_edit.start_line;
		r_edit.end_column = r_edit.start_column;
		r_edit.new_text = "\n\n" + p_method_text;
		return;
	}

	r_edit.start_line = p_line;
	r_edit.start_column = 0;
	r_edit.end_line = p_line;
	r_edit.end_column = 0;
	r_edit.new_text = "\n" + p_method_text;
}

ExtractMethodCandidate build_extract_method_candidate(
		const RefactorLocation &p_location,
		const Vector<String> &p_lines,
		bool p_source_has_final_newline,
		const FSParser::ClassNode *p_class,
		const FSParser::FunctionNode *p_function,
		const FSParser::SuiteNode *p_suite,
		int p_first_statement,
		int p_last_statement,
		const String &p_requested_name) {
	ExtractMethodCandidate candidate;
	candidate.matched = true;
	candidate.member_names = collect_extract_method_member_names(p_class);

	if (p_function == nullptr || p_suite == nullptr || p_function->body == nullptr) {
		candidate.disabled_reason = "Select whole statements inside one function body.";
		return candidate;
	}
	if (p_location.start_line < 0 || p_location.start_line >= p_lines.size() ||
			p_location.end_line <= p_location.start_line || p_location.end_line > p_lines.size()) {
		candidate.disabled_reason = "Select whole statements inside one function body.";
		return candidate;
	}

	ExtractMethodReadState read_state;
	for (int i = p_first_statement; i <= p_last_statement; i++) {
		collect_extract_method_stmt_reads(p_suite->statements[i], p_location, read_state);
		if (!read_state.disabled_reason.is_empty()) {
			candidate.disabled_reason = read_state.disabled_reason;
			return candidate;
		}
	}

	Vector<StringName> reads_after;
	String disabled_reason;
	collect_extract_method_reads_after(p_suite, p_location, reads_after, disabled_reason);
	if (!disabled_reason.is_empty()) {
		candidate.disabled_reason = disabled_reason;
		return candidate;
	}

	Vector<StringName> assigned_existing;
	for (int i = p_first_statement; i <= p_last_statement; i++) {
		collect_extract_method_external_assigns_in_statement(p_suite->statements[i], p_location, assigned_existing);
	}
	if (!assigned_existing.is_empty()) {
		candidate.disabled_reason = "Cannot extract an assignment to an existing local.";
		return candidate;
	}

	Vector<ExtractMethodLocal> outputs;
	for (const ExtractMethodLocal &local : read_state.declared_inside) {
		if (string_name_vector_has(reads_after, local.name)) {
			outputs.push_back(local);
		}
	}
	if (outputs.size() > 1) {
		candidate.disabled_reason = "Extract method does not support multiple output values yet.";
		return candidate;
	}

	Vector<String> rendered_parameter_types;
	for (const ExtractMethodParameter &parameter : read_state.parameters) {
		String rendered_type;
		if (!FSRefactorTypes::render_annotatable_type(parameter.datatype, rendered_type)) {
			candidate.disabled_reason = vformat("Cannot render a type for parameter '%s'.", String(parameter.name));
			return candidate;
		}
		rendered_parameter_types.push_back(rendered_type);
	}

	String rendered_return_type = "void";
	if (outputs.size() == 1 &&
			!FSRefactorTypes::render_annotatable_type(outputs[0].datatype, rendered_return_type)) {
		candidate.disabled_reason = vformat("Cannot render a return type for '%s'.", String(outputs[0].name));
		return candidate;
	}

	String name_error;
	const String method_name = resolve_extract_method_name(p_class, candidate.member_names, p_requested_name, name_error);
	if (method_name.is_empty()) {
		candidate.disabled_reason = name_error;
		return candidate;
	}

	const String function_indent = get_leading_whitespace(p_lines[p_function->start_line - 1]);
	const String statement_indent = get_leading_whitespace(p_lines[p_location.start_line]);
	const String method_body_indent = function_indent + "\t";

	String parameter_list;
	String argument_list;
	for (int i = 0; i < read_state.parameters.size(); i++) {
		if (i > 0) {
			parameter_list += ", ";
			argument_list += ", ";
		}
		parameter_list += String(read_state.parameters[i].name) + ": " + rendered_parameter_types[i];
		argument_list += String(read_state.parameters[i].name);
	}

	const String call = method_name + "(" + argument_list + ")";
	candidate.replacement_edit.start_line = p_location.start_line;
	candidate.replacement_edit.start_column = 0;
	set_extract_method_edit_line_end(
			candidate.replacement_edit,
			p_lines,
			p_source_has_final_newline,
			p_location.end_line);
	if (outputs.size() == 1) {
		candidate.replacement_edit.new_text = statement_indent + "var " + String(outputs[0].name) + ": " +
				rendered_return_type + " = " + call + "\n";
	} else {
		candidate.replacement_edit.new_text = statement_indent + call + "\n";
	}

	String method_text = function_indent + "func " + method_name + "(" + parameter_list + ") -> " +
			rendered_return_type + ":\n";
	method_text += build_extract_method_body(
			p_lines,
			p_location.start_line,
			p_location.end_line,
			statement_indent,
			method_body_indent);
	if (outputs.size() == 1) {
		method_text += method_body_indent + "return " + String(outputs[0].name) + "\n";
	}

	const int insertion_line = p_function->end_line;
	set_extract_method_insertion(
			candidate.method_edit,
			p_lines,
			p_source_has_final_newline,
			insertion_line,
			method_text);
	candidate.suggested_name = method_name;
	candidate.name_line = insertion_line + 1;
	candidate.name_column = function_indent.length() + String("func ").length();
	candidate.enabled = true;
	return candidate;
}

bool find_extract_method_in_suite(
		const RefactorLocation &p_location,
		const Vector<String> &p_lines,
		bool p_source_has_final_newline,
		const FSParser::ClassNode *p_class,
		const FSParser::FunctionNode *p_function,
		const FSParser::SuiteNode *p_suite,
		const String &p_requested_name,
		ExtractMethodCandidate &r_candidate);

bool find_extract_method_in_children(
		const RefactorLocation &p_location,
		const Vector<String> &p_lines,
		bool p_source_has_final_newline,
		const FSParser::ClassNode *p_class,
		const FSParser::FunctionNode *p_function,
		const FSParser::Node *p_statement,
		const String &p_requested_name,
		ExtractMethodCandidate &r_candidate) {
	if (p_statement == nullptr) {
		return false;
	}

	switch (p_statement->type) {
		case FSParser::Node::IF: {
			const FSParser::IfNode *if_node = static_cast<const FSParser::IfNode *>(p_statement);
			return find_extract_method_in_suite(
						   p_location,
						   p_lines,
						   p_source_has_final_newline,
						   p_class,
						   p_function,
						   if_node->true_block,
						   p_requested_name,
						   r_candidate) ||
					find_extract_method_in_suite(
							p_location,
							p_lines,
							p_source_has_final_newline,
							p_class,
							p_function,
							if_node->false_block,
							p_requested_name,
							r_candidate);
		}
		case FSParser::Node::FOR:
			return find_extract_method_in_suite(
					p_location,
					p_lines,
					p_source_has_final_newline,
					p_class,
					p_function,
					static_cast<const FSParser::ForNode *>(p_statement)->loop,
					p_requested_name,
					r_candidate);
		case FSParser::Node::WHILE:
			return find_extract_method_in_suite(
					p_location,
					p_lines,
					p_source_has_final_newline,
					p_class,
					p_function,
					static_cast<const FSParser::WhileNode *>(p_statement)->loop,
					p_requested_name,
					r_candidate);
		case FSParser::Node::MATCH: {
			const FSParser::MatchNode *match_node = static_cast<const FSParser::MatchNode *>(p_statement);
			for (const FSParser::MatchBranchNode *branch : match_node->branches) {
				if (branch != nullptr &&
						find_extract_method_in_suite(
								p_location,
								p_lines,
								p_source_has_final_newline,
								p_class,
								p_function,
								branch->block,
								p_requested_name,
								r_candidate)) {
					return true;
				}
			}
		} break;
		default:
			break;
	}
	return false;
}

bool find_extract_method_in_suite(
		const RefactorLocation &p_location,
		const Vector<String> &p_lines,
		bool p_source_has_final_newline,
		const FSParser::ClassNode *p_class,
		const FSParser::FunctionNode *p_function,
		const FSParser::SuiteNode *p_suite,
		const String &p_requested_name,
		ExtractMethodCandidate &r_candidate) {
	if (p_suite == nullptr) {
		return false;
	}

	int first_statement = -1;
	int last_statement = -1;
	RefactorLocation normalized_location;
	if (find_extract_method_range(p_location, p_lines, p_suite, normalized_location, first_statement, last_statement)) {
		r_candidate = build_extract_method_candidate(
				normalized_location,
				p_lines,
				p_source_has_final_newline,
				p_class,
				p_function,
				p_suite,
				first_statement,
				last_statement,
				p_requested_name);
		return true;
	}

	for (const FSParser::Node *statement : p_suite->statements) {
		if (find_extract_method_in_children(
					p_location,
					p_lines,
					p_source_has_final_newline,
					p_class,
					p_function,
					statement,
					p_requested_name,
					r_candidate)) {
			return true;
		}
	}
	return false;
}

bool find_extract_method_in_function(
		const RefactorLocation &p_location,
		const Vector<String> &p_lines,
		bool p_source_has_final_newline,
		const FSParser::ClassNode *p_class,
		const FSParser::FunctionNode *p_function,
		const String &p_requested_name,
		ExtractMethodCandidate &r_candidate) {
	if (p_function == nullptr || p_function->body == nullptr) {
		return false;
	}
	return find_extract_method_in_suite(
			p_location,
			p_lines,
			p_source_has_final_newline,
			p_class,
			p_function,
			p_function->body,
			p_requested_name,
			r_candidate);
}

bool find_extract_method_in_class(
		const RefactorLocation &p_location,
		const Vector<String> &p_lines,
		bool p_source_has_final_newline,
		const FSParser::ClassNode *p_class,
		const String &p_requested_name,
		ExtractMethodCandidate &r_candidate) {
	if (p_class == nullptr) {
		return false;
	}

	for (const FSParser::ClassNode::Member &member : p_class->members) {
		switch (member.type) {
			case FSParser::ClassNode::Member::FUNCTION:
				if (find_extract_method_in_function(
							p_location,
							p_lines,
							p_source_has_final_newline,
							p_class,
							member.function,
							p_requested_name,
							r_candidate)) {
					return true;
				}
				break;
			case FSParser::ClassNode::Member::CLASS:
				if (find_extract_method_in_class(
							p_location,
							p_lines,
							p_source_has_final_newline,
							member.m_class,
							p_requested_name,
							r_candidate)) {
					return true;
				}
				break;
			default:
				break;
		}
	}
	return false;
}

bool find_extract_variable_in_function(const RefactorLocation &p_location, const Vector<String> &p_lines, const FSParser::FunctionNode *p_function, ExtractVariableCandidate &r_candidate) {
	if (p_function == nullptr || p_function->body == nullptr) {
		return false;
	}
	return find_extract_variable_in_suite(p_location, p_lines, p_function->body, r_candidate);
}

bool find_extract_variable_in_class(const RefactorLocation &p_location, const Vector<String> &p_lines, const FSParser::ClassNode *p_class, ExtractVariableCandidate &r_candidate) {
	if (p_class == nullptr) {
		return false;
	}

	for (const FSParser::ClassNode::Member &member : p_class->members) {
		switch (member.type) {
			case FSParser::ClassNode::Member::FUNCTION:
				if (find_extract_variable_in_function(p_location, p_lines, member.function, r_candidate)) {
					return true;
				}
				break;
			case FSParser::ClassNode::Member::CLASS:
				if (find_extract_variable_in_class(p_location, p_lines, member.m_class, r_candidate)) {
					return true;
				}
				break;
			default:
				break;
		}
	}
	return false;
}

bool find_extract_variable_in_suite(const RefactorLocation &p_location, const Vector<String> &p_lines, const FSParser::SuiteNode *p_suite, ExtractVariableCandidate &r_candidate) {
	if (p_suite == nullptr) {
		return false;
	}

	for (const FSParser::Node *statement : p_suite->statements) {
		if (statement == nullptr) {
			continue;
		}

		switch (statement->type) {
			case FSParser::Node::VARIABLE: {
				const FSParser::VariableNode *variable = static_cast<const FSParser::VariableNode *>(statement);
				if (try_extract_direct_expression(p_location, p_lines, variable->initializer, statement, p_suite, r_candidate)) {
					return true;
				}
			} break;
			case FSParser::Node::CONSTANT: {
				const FSParser::ConstantNode *constant = static_cast<const FSParser::ConstantNode *>(statement);
				if (try_extract_direct_expression(p_location, p_lines, constant->initializer, statement, p_suite, r_candidate)) {
					return true;
				}
			} break;
			case FSParser::Node::RETURN: {
				const FSParser::ReturnNode *return_node = static_cast<const FSParser::ReturnNode *>(statement);
				if (try_extract_direct_expression(p_location, p_lines, return_node->return_value, statement, p_suite, r_candidate)) {
					return true;
				}
			} break;
			case FSParser::Node::ASSIGNMENT: {
				const FSParser::AssignmentNode *assignment = static_cast<const FSParser::AssignmentNode *>(statement);
				if (expression_matches_selection(p_location, p_lines, assignment->assigned_value) && !is_safe_extract_assignment(assignment)) {
					r_candidate.matched = true;
					r_candidate.disabled_reason = "Cannot safely extract this assignment expression.";
					return true;
				}
				if (try_extract_direct_expression(p_location, p_lines, assignment->assigned_value, statement, p_suite, r_candidate)) {
					return true;
				}
			} break;
			case FSParser::Node::ASSERT: {
				const FSParser::AssertNode *assert_node = static_cast<const FSParser::AssertNode *>(statement);
				if (expression_matches_selection(p_location, p_lines, assert_node->condition) ||
						expression_matches_selection(p_location, p_lines, assert_node->message)) {
					r_candidate.matched = true;
					r_candidate.disabled_reason = "Cannot safely extract assert expressions.";
					return true;
				}
			} break;
			case FSParser::Node::IF: {
				const FSParser::IfNode *if_node = static_cast<const FSParser::IfNode *>(statement);
				if (try_extract_direct_expression(p_location, p_lines, if_node->condition, statement, p_suite, r_candidate) ||
						find_extract_variable_in_suite(p_location, p_lines, if_node->true_block, r_candidate) ||
						find_extract_variable_in_suite(p_location, p_lines, if_node->false_block, r_candidate)) {
					return true;
				}
			} break;
			case FSParser::Node::FOR: {
				const FSParser::ForNode *for_node = static_cast<const FSParser::ForNode *>(statement);
				if (try_extract_direct_expression(p_location, p_lines, for_node->list, statement, p_suite, r_candidate) ||
						find_extract_variable_in_suite(p_location, p_lines, for_node->loop, r_candidate)) {
					return true;
				}
			} break;
			case FSParser::Node::WHILE: {
				const FSParser::WhileNode *while_node = static_cast<const FSParser::WhileNode *>(statement);
				if (expression_matches_selection(p_location, p_lines, while_node->condition)) {
					r_candidate.matched = true;
					r_candidate.disabled_reason = "Cannot safely extract a while loop condition.";
					return true;
				}
				if (find_extract_variable_in_suite(p_location, p_lines, while_node->loop, r_candidate)) {
					return true;
				}
			} break;
			case FSParser::Node::MATCH: {
				const FSParser::MatchNode *match_node = static_cast<const FSParser::MatchNode *>(statement);
				if (try_extract_direct_expression(p_location, p_lines, match_node->test, statement, p_suite, r_candidate)) {
					return true;
				}
				for (const FSParser::MatchBranchNode *branch : match_node->branches) {
					if (branch != nullptr && find_extract_variable_in_suite(p_location, p_lines, branch->block, r_candidate)) {
						return true;
					}
				}
			} break;
			default:
				break;
		}
	}
	return false;
}

bool find_inline_variable_target_in_suite(const RefactorLocation &p_location, const Vector<String> &p_lines, const FSParser::SuiteNode *p_suite, const FSParser::VariableNode *&r_variable, const FSParser::FunctionNode *&r_function);
bool find_inline_variable_target_in_expression(const RefactorLocation &p_location, const Vector<String> &p_lines, const FSParser::ExpressionNode *p_expression, const FSParser::VariableNode *&r_variable, const FSParser::FunctionNode *&r_function);

bool find_inline_variable_declaration(const RefactorLocation &p_location, const Vector<String> &p_lines, const FSParser::SuiteNode *p_suite, const FSParser::VariableNode *p_variable, const FSParser::VariableNode *&r_variable, const FSParser::FunctionNode *&r_function) {
	if (p_suite == nullptr || p_suite->parent_function == nullptr || p_variable == nullptr || p_variable->identifier == nullptr) {
		return false;
	}
	const int line_index = p_variable->start_line - 1;
	if (line_index < 0 || line_index >= p_lines.size()) {
		return false;
	}
	const String line = p_lines[line_index];
	int name_start = 0;
	int name_end = 0;
	if (!get_identifier_text_span(p_lines, line_index, p_variable->identifier, nullptr, name_start, name_end)) {
		return false;
	}
	int declaration_start = name_start;
	int keyword_start = 0;
	if (get_node_text_start(p_lines, line_index, p_variable, "var", keyword_start)) {
		declaration_start = keyword_start;
	} else {
		keyword_start = line.find("var");
		if (keyword_start >= 0 && keyword_start <= name_start) {
			declaration_start = keyword_start;
		}
	}
	const int equal_index = line.find("=", name_end);
	const int declaration_end = equal_index >= 0 ? equal_index : name_end;
	if (!caret_on_segment(p_location, line_index, declaration_start, declaration_end)) {
		return false;
	}
	r_variable = p_variable;
	r_function = p_suite->parent_function;
	return true;
}

bool find_inline_variable_target_in_identifier(const RefactorLocation &p_location, const Vector<String> &p_lines, const FSParser::IdentifierNode *p_identifier, const FSParser::VariableNode *&r_variable, const FSParser::FunctionNode *&r_function) {
	if (p_identifier == nullptr || p_identifier->source != FSParser::IdentifierNode::LOCAL_VARIABLE || p_identifier->variable_source == nullptr) {
		return false;
	}
	if (p_identifier->source_function != nullptr &&
			(p_location.start_line + 1 < p_identifier->source_function->start_line ||
					p_location.start_line + 1 > p_identifier->source_function->end_line)) {
		return false;
	}
	const int line_index = p_identifier->start_line - 1;
	int name_start = 0;
	int name_end = 0;
	if (!get_identifier_text_span(p_lines, line_index, p_identifier, &p_location, name_start, name_end) ||
			!caret_on_segment(p_location, line_index, name_start, name_end)) {
		return false;
	}
	r_variable = p_identifier->variable_source;
	r_function = p_identifier->source_function;
	return true;
}

bool find_inline_variable_target_in_lambda(const RefactorLocation &p_location, const Vector<String> &p_lines, const FSParser::LambdaNode *p_lambda, const FSParser::VariableNode *&r_variable, const FSParser::FunctionNode *&r_function) {
	if (p_lambda == nullptr || p_lambda->function == nullptr) {
		return false;
	}
	return find_inline_variable_target_in_suite(p_location, p_lines, p_lambda->function->body, r_variable, r_function);
}

bool find_inline_variable_target_in_expression(const RefactorLocation &p_location, const Vector<String> &p_lines, const FSParser::ExpressionNode *p_expression, const FSParser::VariableNode *&r_variable, const FSParser::FunctionNode *&r_function) {
	if (p_expression == nullptr) {
		return false;
	}

	switch (p_expression->type) {
		case FSParser::Node::ARRAY: {
			const FSParser::ArrayNode *array = static_cast<const FSParser::ArrayNode *>(p_expression);
			for (const FSParser::ExpressionNode *element : array->elements) {
				if (find_inline_variable_target_in_expression(p_location, p_lines, element, r_variable, r_function)) {
					return true;
				}
			}
		} break;
		case FSParser::Node::TUPLE_LITERAL: {
			const FSParser::TupleLiteralNode *tuple_literal = static_cast<const FSParser::TupleLiteralNode *>(p_expression);
			for (const FSParser::ExpressionNode *element : tuple_literal->elements) {
				if (find_inline_variable_target_in_expression(p_location, p_lines, element, r_variable, r_function)) {
					return true;
				}
			}
		} break;
		case FSParser::Node::ASSIGNMENT: {
			const FSParser::AssignmentNode *assignment = static_cast<const FSParser::AssignmentNode *>(p_expression);
			return find_inline_variable_target_in_expression(p_location, p_lines, assignment->assignee, r_variable, r_function) ||
					find_inline_variable_target_in_expression(p_location, p_lines, assignment->assigned_value, r_variable, r_function);
		}
		case FSParser::Node::AWAIT:
			return find_inline_variable_target_in_expression(p_location, p_lines, static_cast<const FSParser::AwaitNode *>(p_expression)->to_await, r_variable, r_function);
		case FSParser::Node::BINARY_OPERATOR: {
			const FSParser::BinaryOpNode *binary = static_cast<const FSParser::BinaryOpNode *>(p_expression);
			return find_inline_variable_target_in_expression(p_location, p_lines, binary->left_operand, r_variable, r_function) ||
					find_inline_variable_target_in_expression(p_location, p_lines, binary->right_operand, r_variable, r_function);
		}
		case FSParser::Node::CALL: {
			const FSParser::CallNode *call = static_cast<const FSParser::CallNode *>(p_expression);
			if (find_inline_variable_target_in_expression(p_location, p_lines, call->callee, r_variable, r_function)) {
				return true;
			}
			for (const FSParser::ExpressionNode *argument : call->arguments) {
				if (find_inline_variable_target_in_expression(p_location, p_lines, argument, r_variable, r_function)) {
					return true;
				}
			}
		} break;
		case FSParser::Node::CAST:
			return find_inline_variable_target_in_expression(p_location, p_lines, static_cast<const FSParser::CastNode *>(p_expression)->operand, r_variable, r_function);
		case FSParser::Node::DICTIONARY: {
			const FSParser::DictionaryNode *dictionary = static_cast<const FSParser::DictionaryNode *>(p_expression);
			for (const FSParser::DictionaryNode::Pair &pair : dictionary->elements) {
				if (find_inline_variable_target_in_expression(p_location, p_lines, pair.key, r_variable, r_function) ||
						find_inline_variable_target_in_expression(p_location, p_lines, pair.value, r_variable, r_function)) {
					return true;
				}
			}
		} break;
		case FSParser::Node::IDENTIFIER:
			return find_inline_variable_target_in_identifier(p_location, p_lines, static_cast<const FSParser::IdentifierNode *>(p_expression), r_variable, r_function);
		case FSParser::Node::LAMBDA:
			return find_inline_variable_target_in_lambda(p_location, p_lines, static_cast<const FSParser::LambdaNode *>(p_expression), r_variable, r_function);
		case FSParser::Node::PRELOAD:
			return find_inline_variable_target_in_expression(p_location, p_lines, static_cast<const FSParser::PreloadNode *>(p_expression)->path, r_variable, r_function);
		case FSParser::Node::SUBSCRIPT: {
			const FSParser::SubscriptNode *subscript = static_cast<const FSParser::SubscriptNode *>(p_expression);
			if (find_inline_variable_target_in_expression(p_location, p_lines, subscript->base, r_variable, r_function)) {
				return true;
			}
			if (!subscript->is_attribute && find_inline_variable_target_in_expression(p_location, p_lines, subscript->index, r_variable, r_function)) {
				return true;
			}
		} break;
		case FSParser::Node::TERNARY_OPERATOR: {
			const FSParser::TernaryOpNode *ternary = static_cast<const FSParser::TernaryOpNode *>(p_expression);
			return find_inline_variable_target_in_expression(p_location, p_lines, ternary->condition, r_variable, r_function) ||
					find_inline_variable_target_in_expression(p_location, p_lines, ternary->true_expr, r_variable, r_function) ||
					find_inline_variable_target_in_expression(p_location, p_lines, ternary->false_expr, r_variable, r_function);
		}
		case FSParser::Node::TYPE_TEST:
			return find_inline_variable_target_in_expression(p_location, p_lines, static_cast<const FSParser::TypeTestNode *>(p_expression)->operand, r_variable, r_function);
		case FSParser::Node::UNARY_OPERATOR:
			return find_inline_variable_target_in_expression(p_location, p_lines, static_cast<const FSParser::UnaryOpNode *>(p_expression)->operand, r_variable, r_function);
		default:
			break;
	}
	return false;
}

bool find_inline_variable_target_in_suite(const RefactorLocation &p_location, const Vector<String> &p_lines, const FSParser::SuiteNode *p_suite, const FSParser::VariableNode *&r_variable, const FSParser::FunctionNode *&r_function) {
	if (p_suite == nullptr) {
		return false;
	}

	for (const FSParser::Node *statement : p_suite->statements) {
		if (statement == nullptr) {
			continue;
		}
		switch (statement->type) {
			case FSParser::Node::VARIABLE: {
				const FSParser::VariableNode *variable = static_cast<const FSParser::VariableNode *>(statement);
				if (find_inline_variable_declaration(p_location, p_lines, p_suite, variable, r_variable, r_function) ||
						find_inline_variable_target_in_expression(p_location, p_lines, variable->initializer, r_variable, r_function)) {
					return true;
				}
			} break;
			case FSParser::Node::CONSTANT: {
				const FSParser::ConstantNode *constant = static_cast<const FSParser::ConstantNode *>(statement);
				if (find_inline_variable_target_in_expression(p_location, p_lines, constant->initializer, r_variable, r_function)) {
					return true;
				}
			} break;
			case FSParser::Node::RETURN: {
				const FSParser::ReturnNode *return_node = static_cast<const FSParser::ReturnNode *>(statement);
				if (find_inline_variable_target_in_expression(p_location, p_lines, return_node->return_value, r_variable, r_function)) {
					return true;
				}
			} break;
			case FSParser::Node::ASSIGNMENT:
				if (find_inline_variable_target_in_expression(p_location, p_lines, static_cast<const FSParser::AssignmentNode *>(statement), r_variable, r_function)) {
					return true;
				}
				break;
			case FSParser::Node::ASSERT: {
				const FSParser::AssertNode *assert_node = static_cast<const FSParser::AssertNode *>(statement);
				if (find_inline_variable_target_in_expression(p_location, p_lines, assert_node->condition, r_variable, r_function) ||
						find_inline_variable_target_in_expression(p_location, p_lines, assert_node->message, r_variable, r_function)) {
					return true;
				}
			} break;
			case FSParser::Node::IF: {
				const FSParser::IfNode *if_node = static_cast<const FSParser::IfNode *>(statement);
				if (find_inline_variable_target_in_expression(p_location, p_lines, if_node->condition, r_variable, r_function) ||
						find_inline_variable_target_in_suite(p_location, p_lines, if_node->true_block, r_variable, r_function) ||
						find_inline_variable_target_in_suite(p_location, p_lines, if_node->false_block, r_variable, r_function)) {
					return true;
				}
			} break;
			case FSParser::Node::FOR: {
				const FSParser::ForNode *for_node = static_cast<const FSParser::ForNode *>(statement);
				if (find_inline_variable_target_in_expression(p_location, p_lines, for_node->list, r_variable, r_function) ||
						find_inline_variable_target_in_suite(p_location, p_lines, for_node->loop, r_variable, r_function)) {
					return true;
				}
			} break;
			case FSParser::Node::WHILE: {
				const FSParser::WhileNode *while_node = static_cast<const FSParser::WhileNode *>(statement);
				if (find_inline_variable_target_in_expression(p_location, p_lines, while_node->condition, r_variable, r_function) ||
						find_inline_variable_target_in_suite(p_location, p_lines, while_node->loop, r_variable, r_function)) {
					return true;
				}
			} break;
			case FSParser::Node::MATCH: {
				const FSParser::MatchNode *match_node = static_cast<const FSParser::MatchNode *>(statement);
				if (find_inline_variable_target_in_expression(p_location, p_lines, match_node->test, r_variable, r_function)) {
					return true;
				}
				for (const FSParser::MatchBranchNode *branch : match_node->branches) {
					if (branch != nullptr &&
							(find_inline_variable_target_in_suite(p_location, p_lines, branch->guard_body, r_variable, r_function) ||
									find_inline_variable_target_in_suite(p_location, p_lines, branch->block, r_variable, r_function))) {
						return true;
					}
				}
			} break;
			default:
				if (statement->is_expression() && find_inline_variable_target_in_expression(p_location, p_lines, static_cast<const FSParser::ExpressionNode *>(statement), r_variable, r_function)) {
					return true;
				}
				break;
		}
	}
	return false;
}

bool find_inline_variable_target_in_function(const RefactorLocation &p_location, const Vector<String> &p_lines, const FSParser::FunctionNode *p_function, const FSParser::VariableNode *&r_variable, const FSParser::FunctionNode *&r_function) {
	if (p_function == nullptr) {
		return false;
	}
	return find_inline_variable_target_in_suite(p_location, p_lines, p_function->body, r_variable, r_function);
}

bool find_inline_variable_target_in_class(const RefactorLocation &p_location, const Vector<String> &p_lines, const FSParser::ClassNode *p_class, const FSParser::VariableNode *&r_variable, const FSParser::FunctionNode *&r_function) {
	if (p_class == nullptr) {
		return false;
	}

	for (const FSParser::ClassNode::Member &member : p_class->members) {
		switch (member.type) {
			case FSParser::ClassNode::Member::FUNCTION:
				if (find_inline_variable_target_in_function(p_location, p_lines, member.function, r_variable, r_function)) {
					return true;
				}
				break;
			case FSParser::ClassNode::Member::CLASS:
				if (find_inline_variable_target_in_class(p_location, p_lines, member.m_class, r_variable, r_function)) {
					return true;
				}
				break;
			default:
				break;
		}
	}
	return false;
}

struct InlineVariableUsageCollection {
	Vector<InlineVariableUse> reads;
	bool has_assignment_target = false;
	bool has_lambda_capture = false;
};

void collect_inline_variable_uses_in_suite(const FSParser::SuiteNode *p_suite, const FSParser::VariableNode *p_target, const FSParser::FunctionNode *p_target_function, const FSParser::FunctionNode *p_current_function, InlineVariableUsageCollection &r_collection);
void collect_inline_variable_uses_in_expression(const FSParser::ExpressionNode *p_expression, const FSParser::Node *p_parent, bool p_assignment_target, const FSParser::VariableNode *p_target, const FSParser::FunctionNode *p_target_function, const FSParser::FunctionNode *p_current_function, InlineVariableUsageCollection &r_collection);

void collect_inline_variable_identifier_use(const FSParser::IdentifierNode *p_identifier, const FSParser::Node *p_parent, bool p_assignment_target, const FSParser::VariableNode *p_target, const FSParser::FunctionNode *p_target_function, const FSParser::FunctionNode *p_current_function, InlineVariableUsageCollection &r_collection) {
	if (p_identifier == nullptr || p_identifier->source != FSParser::IdentifierNode::LOCAL_VARIABLE || p_identifier->variable_source != p_target) {
		return;
	}
	if (p_current_function != p_target_function) {
		r_collection.has_lambda_capture = true;
		return;
	}
	if (p_assignment_target) {
		r_collection.has_assignment_target = true;
		return;
	}
	InlineVariableUse use;
	use.identifier = p_identifier;
	use.parent = p_parent;
	r_collection.reads.push_back(use);
}

void collect_inline_variable_uses_in_expression(const FSParser::ExpressionNode *p_expression, const FSParser::Node *p_parent, bool p_assignment_target, const FSParser::VariableNode *p_target, const FSParser::FunctionNode *p_target_function, const FSParser::FunctionNode *p_current_function, InlineVariableUsageCollection &r_collection) {
	if (p_expression == nullptr) {
		return;
	}

	switch (p_expression->type) {
		case FSParser::Node::ARRAY: {
			const FSParser::ArrayNode *array = static_cast<const FSParser::ArrayNode *>(p_expression);
			for (const FSParser::ExpressionNode *element : array->elements) {
				collect_inline_variable_uses_in_expression(element, p_expression, false, p_target, p_target_function, p_current_function, r_collection);
			}
		} break;
		case FSParser::Node::TUPLE_LITERAL: {
			const FSParser::TupleLiteralNode *tuple_literal = static_cast<const FSParser::TupleLiteralNode *>(p_expression);
			for (const FSParser::ExpressionNode *element : tuple_literal->elements) {
				collect_inline_variable_uses_in_expression(element, p_expression, false, p_target, p_target_function, p_current_function, r_collection);
			}
		} break;
		case FSParser::Node::ASSIGNMENT: {
			const FSParser::AssignmentNode *assignment = static_cast<const FSParser::AssignmentNode *>(p_expression);
			collect_inline_variable_uses_in_expression(assignment->assignee, p_expression, true, p_target, p_target_function, p_current_function, r_collection);
			collect_inline_variable_uses_in_expression(assignment->assigned_value, p_expression, false, p_target, p_target_function, p_current_function, r_collection);
		} break;
		case FSParser::Node::AWAIT:
			collect_inline_variable_uses_in_expression(static_cast<const FSParser::AwaitNode *>(p_expression)->to_await, p_expression, false, p_target, p_target_function, p_current_function, r_collection);
			break;
		case FSParser::Node::BINARY_OPERATOR: {
			const FSParser::BinaryOpNode *binary = static_cast<const FSParser::BinaryOpNode *>(p_expression);
			collect_inline_variable_uses_in_expression(binary->left_operand, p_expression, false, p_target, p_target_function, p_current_function, r_collection);
			collect_inline_variable_uses_in_expression(binary->right_operand, p_expression, false, p_target, p_target_function, p_current_function, r_collection);
		} break;
		case FSParser::Node::CALL: {
			const FSParser::CallNode *call = static_cast<const FSParser::CallNode *>(p_expression);
			collect_inline_variable_uses_in_expression(call->callee, p_expression, false, p_target, p_target_function, p_current_function, r_collection);
			for (const FSParser::ExpressionNode *argument : call->arguments) {
				collect_inline_variable_uses_in_expression(argument, p_expression, false, p_target, p_target_function, p_current_function, r_collection);
			}
		} break;
		case FSParser::Node::CAST:
			collect_inline_variable_uses_in_expression(static_cast<const FSParser::CastNode *>(p_expression)->operand, p_expression, false, p_target, p_target_function, p_current_function, r_collection);
			break;
		case FSParser::Node::DICTIONARY: {
			const FSParser::DictionaryNode *dictionary = static_cast<const FSParser::DictionaryNode *>(p_expression);
			for (const FSParser::DictionaryNode::Pair &pair : dictionary->elements) {
				collect_inline_variable_uses_in_expression(pair.key, p_expression, false, p_target, p_target_function, p_current_function, r_collection);
				collect_inline_variable_uses_in_expression(pair.value, p_expression, false, p_target, p_target_function, p_current_function, r_collection);
			}
		} break;
		case FSParser::Node::IDENTIFIER:
			collect_inline_variable_identifier_use(static_cast<const FSParser::IdentifierNode *>(p_expression), p_parent, p_assignment_target, p_target, p_target_function, p_current_function, r_collection);
			break;
		case FSParser::Node::LAMBDA: {
			const FSParser::LambdaNode *lambda = static_cast<const FSParser::LambdaNode *>(p_expression);
			if (lambda->function != nullptr) {
				collect_inline_variable_uses_in_suite(lambda->function->body, p_target, p_target_function, lambda->function, r_collection);
			}
		} break;
		case FSParser::Node::PRELOAD:
			collect_inline_variable_uses_in_expression(static_cast<const FSParser::PreloadNode *>(p_expression)->path, p_expression, false, p_target, p_target_function, p_current_function, r_collection);
			break;
		case FSParser::Node::SUBSCRIPT: {
			const FSParser::SubscriptNode *subscript = static_cast<const FSParser::SubscriptNode *>(p_expression);
			collect_inline_variable_uses_in_expression(subscript->base, p_expression, p_assignment_target, p_target, p_target_function, p_current_function, r_collection);
			if (!subscript->is_attribute) {
				collect_inline_variable_uses_in_expression(subscript->index, p_expression, false, p_target, p_target_function, p_current_function, r_collection);
			}
		} break;
		case FSParser::Node::TERNARY_OPERATOR: {
			const FSParser::TernaryOpNode *ternary = static_cast<const FSParser::TernaryOpNode *>(p_expression);
			collect_inline_variable_uses_in_expression(ternary->condition, p_expression, false, p_target, p_target_function, p_current_function, r_collection);
			collect_inline_variable_uses_in_expression(ternary->true_expr, p_expression, false, p_target, p_target_function, p_current_function, r_collection);
			collect_inline_variable_uses_in_expression(ternary->false_expr, p_expression, false, p_target, p_target_function, p_current_function, r_collection);
		} break;
		case FSParser::Node::TYPE_TEST:
			collect_inline_variable_uses_in_expression(static_cast<const FSParser::TypeTestNode *>(p_expression)->operand, p_expression, false, p_target, p_target_function, p_current_function, r_collection);
			break;
		case FSParser::Node::UNARY_OPERATOR:
			collect_inline_variable_uses_in_expression(static_cast<const FSParser::UnaryOpNode *>(p_expression)->operand, p_expression, false, p_target, p_target_function, p_current_function, r_collection);
			break;
		default:
			break;
	}
}

void collect_inline_variable_uses_in_suite(const FSParser::SuiteNode *p_suite, const FSParser::VariableNode *p_target, const FSParser::FunctionNode *p_target_function, const FSParser::FunctionNode *p_current_function, InlineVariableUsageCollection &r_collection) {
	if (p_suite == nullptr) {
		return;
	}

	for (const FSParser::Node *statement : p_suite->statements) {
		if (statement == nullptr) {
			continue;
		}
		switch (statement->type) {
			case FSParser::Node::VARIABLE: {
				const FSParser::VariableNode *variable = static_cast<const FSParser::VariableNode *>(statement);
				collect_inline_variable_uses_in_expression(variable->initializer, statement, false, p_target, p_target_function, p_current_function, r_collection);
			} break;
			case FSParser::Node::CONSTANT: {
				const FSParser::ConstantNode *constant = static_cast<const FSParser::ConstantNode *>(statement);
				collect_inline_variable_uses_in_expression(constant->initializer, statement, false, p_target, p_target_function, p_current_function, r_collection);
			} break;
			case FSParser::Node::RETURN: {
				const FSParser::ReturnNode *return_node = static_cast<const FSParser::ReturnNode *>(statement);
				collect_inline_variable_uses_in_expression(return_node->return_value, statement, false, p_target, p_target_function, p_current_function, r_collection);
			} break;
			case FSParser::Node::ASSIGNMENT:
				collect_inline_variable_uses_in_expression(static_cast<const FSParser::AssignmentNode *>(statement), nullptr, false, p_target, p_target_function, p_current_function, r_collection);
				break;
			case FSParser::Node::ASSERT: {
				const FSParser::AssertNode *assert_node = static_cast<const FSParser::AssertNode *>(statement);
				collect_inline_variable_uses_in_expression(assert_node->condition, statement, false, p_target, p_target_function, p_current_function, r_collection);
				collect_inline_variable_uses_in_expression(assert_node->message, statement, false, p_target, p_target_function, p_current_function, r_collection);
			} break;
			case FSParser::Node::IF: {
				const FSParser::IfNode *if_node = static_cast<const FSParser::IfNode *>(statement);
				collect_inline_variable_uses_in_expression(if_node->condition, statement, false, p_target, p_target_function, p_current_function, r_collection);
				collect_inline_variable_uses_in_suite(if_node->true_block, p_target, p_target_function, p_current_function, r_collection);
				collect_inline_variable_uses_in_suite(if_node->false_block, p_target, p_target_function, p_current_function, r_collection);
			} break;
			case FSParser::Node::FOR: {
				const FSParser::ForNode *for_node = static_cast<const FSParser::ForNode *>(statement);
				collect_inline_variable_uses_in_expression(for_node->list, statement, false, p_target, p_target_function, p_current_function, r_collection);
				collect_inline_variable_uses_in_suite(for_node->loop, p_target, p_target_function, p_current_function, r_collection);
			} break;
			case FSParser::Node::WHILE: {
				const FSParser::WhileNode *while_node = static_cast<const FSParser::WhileNode *>(statement);
				collect_inline_variable_uses_in_expression(while_node->condition, statement, false, p_target, p_target_function, p_current_function, r_collection);
				collect_inline_variable_uses_in_suite(while_node->loop, p_target, p_target_function, p_current_function, r_collection);
			} break;
			case FSParser::Node::MATCH: {
				const FSParser::MatchNode *match_node = static_cast<const FSParser::MatchNode *>(statement);
				collect_inline_variable_uses_in_expression(match_node->test, statement, false, p_target, p_target_function, p_current_function, r_collection);
				for (const FSParser::MatchBranchNode *branch : match_node->branches) {
					if (branch != nullptr) {
						collect_inline_variable_uses_in_suite(branch->guard_body, p_target, p_target_function, p_current_function, r_collection);
						collect_inline_variable_uses_in_suite(branch->block, p_target, p_target_function, p_current_function, r_collection);
					}
				}
			} break;
			default:
				if (statement->is_expression()) {
					collect_inline_variable_uses_in_expression(static_cast<const FSParser::ExpressionNode *>(statement), nullptr, false, p_target, p_target_function, p_current_function, r_collection);
				}
				break;
		}
	}
}

void collect_inline_variable_uses_in_class(const FSParser::ClassNode *p_class, const FSParser::VariableNode *p_target, const FSParser::FunctionNode *p_target_function, InlineVariableUsageCollection &r_collection) {
	if (p_class == nullptr) {
		return;
	}
	for (const FSParser::ClassNode::Member &member : p_class->members) {
		switch (member.type) {
			case FSParser::ClassNode::Member::FUNCTION:
				if (member.function != nullptr) {
					collect_inline_variable_uses_in_suite(member.function->body, p_target, p_target_function, member.function, r_collection);
				}
				break;
			case FSParser::ClassNode::Member::CLASS:
				collect_inline_variable_uses_in_class(member.m_class, p_target, p_target_function, r_collection);
				break;
			default:
				break;
		}
	}
}

bool expression_has_side_effects(const FSParser::ExpressionNode *p_expression) {
	if (p_expression == nullptr) {
		return false;
	}

	switch (p_expression->type) {
		case FSParser::Node::ASSIGNMENT:
		case FSParser::Node::AWAIT:
		case FSParser::Node::CALL:
		case FSParser::Node::GET_NODE:
		case FSParser::Node::LAMBDA:
		case FSParser::Node::PRELOAD:
			return true;
		case FSParser::Node::ARRAY: {
			const FSParser::ArrayNode *array = static_cast<const FSParser::ArrayNode *>(p_expression);
			for (const FSParser::ExpressionNode *element : array->elements) {
				if (expression_has_side_effects(element)) {
					return true;
				}
			}
			return false;
		}
		case FSParser::Node::TUPLE_LITERAL: {
			const FSParser::TupleLiteralNode *tuple_literal = static_cast<const FSParser::TupleLiteralNode *>(p_expression);
			for (const FSParser::ExpressionNode *element : tuple_literal->elements) {
				if (expression_has_side_effects(element)) {
					return true;
				}
			}
			return false;
		}
		case FSParser::Node::BINARY_OPERATOR: {
			const FSParser::BinaryOpNode *binary = static_cast<const FSParser::BinaryOpNode *>(p_expression);
			return expression_has_side_effects(binary->left_operand) || expression_has_side_effects(binary->right_operand);
		}
		case FSParser::Node::CAST:
			return expression_has_side_effects(static_cast<const FSParser::CastNode *>(p_expression)->operand);
		case FSParser::Node::DICTIONARY: {
			const FSParser::DictionaryNode *dictionary = static_cast<const FSParser::DictionaryNode *>(p_expression);
			for (const FSParser::DictionaryNode::Pair &pair : dictionary->elements) {
				if (expression_has_side_effects(pair.key) || expression_has_side_effects(pair.value)) {
					return true;
				}
			}
			return false;
		}
		case FSParser::Node::SUBSCRIPT: {
			const FSParser::SubscriptNode *subscript = static_cast<const FSParser::SubscriptNode *>(p_expression);
			return expression_has_side_effects(subscript->base) ||
					(!subscript->is_attribute && expression_has_side_effects(subscript->index));
		}
		case FSParser::Node::TERNARY_OPERATOR: {
			const FSParser::TernaryOpNode *ternary = static_cast<const FSParser::TernaryOpNode *>(p_expression);
			return expression_has_side_effects(ternary->condition) ||
					expression_has_side_effects(ternary->true_expr) ||
					expression_has_side_effects(ternary->false_expr);
		}
		case FSParser::Node::TYPE_TEST:
			return expression_has_side_effects(static_cast<const FSParser::TypeTestNode *>(p_expression)->operand);
		case FSParser::Node::UNARY_OPERATOR:
			return expression_has_side_effects(static_cast<const FSParser::UnaryOpNode *>(p_expression)->operand);
		default:
			return false;
	}
}

int binary_precedence(const FSParser::BinaryOpNode *p_binary) {
	if (p_binary == nullptr) {
		return 0;
	}
	switch (p_binary->operation) {
		case FSParser::BinaryOpNode::OP_LOGIC_OR:
			return 4;
		case FSParser::BinaryOpNode::OP_LOGIC_AND:
			return 5;
		case FSParser::BinaryOpNode::OP_CONTENT_TEST:
			return 7;
		case FSParser::BinaryOpNode::OP_COMP_EQUAL:
		case FSParser::BinaryOpNode::OP_COMP_NOT_EQUAL:
		case FSParser::BinaryOpNode::OP_COMP_LESS:
		case FSParser::BinaryOpNode::OP_COMP_LESS_EQUAL:
		case FSParser::BinaryOpNode::OP_COMP_GREATER:
		case FSParser::BinaryOpNode::OP_COMP_GREATER_EQUAL:
			return 8;
		case FSParser::BinaryOpNode::OP_BIT_OR:
			return 9;
		case FSParser::BinaryOpNode::OP_BIT_XOR:
			return 10;
		case FSParser::BinaryOpNode::OP_BIT_AND:
			return 11;
		case FSParser::BinaryOpNode::OP_BIT_LEFT_SHIFT:
		case FSParser::BinaryOpNode::OP_BIT_RIGHT_SHIFT:
			return 12;
		case FSParser::BinaryOpNode::OP_ADDITION:
		case FSParser::BinaryOpNode::OP_SUBTRACTION:
			return 13;
		case FSParser::BinaryOpNode::OP_MULTIPLICATION:
		case FSParser::BinaryOpNode::OP_DIVISION:
		case FSParser::BinaryOpNode::OP_MODULO:
			return 14;
		case FSParser::BinaryOpNode::OP_POWER:
			return 17;
	}
	return 0;
}

int unary_precedence(const FSParser::UnaryOpNode *p_unary) {
	if (p_unary == nullptr) {
		return 0;
	}
	switch (p_unary->operation) {
		case FSParser::UnaryOpNode::OP_LOGIC_NOT:
			return 6;
		case FSParser::UnaryOpNode::OP_POSITIVE:
		case FSParser::UnaryOpNode::OP_NEGATIVE:
			return 15;
		case FSParser::UnaryOpNode::OP_COMPLEMENT:
			return 16;
	}
	return 0;
}

int expression_precedence(const FSParser::ExpressionNode *p_expression) {
	if (p_expression == nullptr) {
		return 0;
	}
	switch (p_expression->type) {
		case FSParser::Node::ASSIGNMENT:
			return 1;
		case FSParser::Node::CAST:
			return 2;
		case FSParser::Node::TERNARY_OPERATOR:
			return 3;
		case FSParser::Node::BINARY_OPERATOR:
			return binary_precedence(static_cast<const FSParser::BinaryOpNode *>(p_expression));
		case FSParser::Node::UNARY_OPERATOR:
			return unary_precedence(static_cast<const FSParser::UnaryOpNode *>(p_expression));
		case FSParser::Node::TYPE_TEST:
			return 18;
		case FSParser::Node::AWAIT:
			return 19;
		case FSParser::Node::CALL:
		case FSParser::Node::SUBSCRIPT:
			return 20;
		default:
			return 21;
	}
}

bool inline_replacement_needs_parentheses(const FSParser::ExpressionNode *p_initializer, const InlineVariableUse &p_use) {
	if (p_initializer == nullptr || p_use.parent == nullptr || p_use.identifier == nullptr) {
		return false;
	}
	const int initializer_precedence = expression_precedence(p_initializer);

	switch (p_use.parent->type) {
		case FSParser::Node::BINARY_OPERATOR: {
			const FSParser::BinaryOpNode *parent_binary = static_cast<const FSParser::BinaryOpNode *>(p_use.parent);
			const int parent_precedence = binary_precedence(parent_binary);
			if (initializer_precedence < parent_precedence) {
				return true;
			}
			if (initializer_precedence == parent_precedence) {
				return p_use.identifier == parent_binary->right_operand ||
						(parent_binary->operation == FSParser::BinaryOpNode::OP_POWER &&
								p_use.identifier == parent_binary->left_operand);
			}
			return false;
		}
		case FSParser::Node::AWAIT: {
			const FSParser::AwaitNode *await = static_cast<const FSParser::AwaitNode *>(p_use.parent);
			return await->to_await == p_use.identifier && initializer_precedence < expression_precedence(await);
		}
		case FSParser::Node::CALL: {
			const FSParser::CallNode *call = static_cast<const FSParser::CallNode *>(p_use.parent);
			return call->callee == p_use.identifier && initializer_precedence < expression_precedence(call);
		}
		case FSParser::Node::CAST: {
			const FSParser::CastNode *cast = static_cast<const FSParser::CastNode *>(p_use.parent);
			return cast->operand == p_use.identifier && initializer_precedence < expression_precedence(cast);
		}
		case FSParser::Node::SUBSCRIPT: {
			const FSParser::SubscriptNode *subscript = static_cast<const FSParser::SubscriptNode *>(p_use.parent);
			return subscript->base == p_use.identifier && initializer_precedence < expression_precedence(subscript);
		}
		case FSParser::Node::TYPE_TEST: {
			const FSParser::TypeTestNode *type_test = static_cast<const FSParser::TypeTestNode *>(p_use.parent);
			return type_test->operand == p_use.identifier && initializer_precedence < expression_precedence(type_test);
		}
		case FSParser::Node::UNARY_OPERATOR:
			return initializer_precedence <= expression_precedence(static_cast<const FSParser::ExpressionNode *>(p_use.parent));
		default:
			break;
	}
	return false;
}

bool inline_declaration_has_trailing_content(const Vector<String> &p_lines, const RefactorLocation &p_initializer_range) {
	if (p_initializer_range.start_line != p_initializer_range.end_line ||
			p_initializer_range.end_line < 0 || p_initializer_range.end_line >= p_lines.size()) {
		return true;
	}
	const String line = p_lines[p_initializer_range.end_line];
	for (int i = p_initializer_range.end_column; i < line.length(); i++) {
		if (!is_whitespace(line[i])) {
			return true;
		}
	}
	return false;
}

bool make_inline_variable_declaration_edit(const Vector<String> &p_lines, const FSParser::VariableNode *p_variable, RefactorTextEdit &r_edit) {
	if (p_variable == nullptr || p_variable->start_line <= 0) {
		return false;
	}
	const int line_index = p_variable->start_line - 1;
	if (line_index < 0 || line_index >= p_lines.size()) {
		return false;
	}
	r_edit.start_line = line_index;
	r_edit.start_column = 0;
	if (line_index + 1 < p_lines.size()) {
		r_edit.end_line = line_index + 1;
		r_edit.end_column = 0;
	} else {
		r_edit.end_line = line_index;
		r_edit.end_column = p_lines[line_index].length();
	}
	r_edit.new_text = "";
	return true;
}

InlineVariableCandidate find_inline_variable_candidate_in_tree(const RefactorLocation &p_location, const Vector<String> &p_lines, const FSParser::ClassNode *p_tree) {
	InlineVariableCandidate candidate;
	const FSParser::VariableNode *target = nullptr;
	const FSParser::FunctionNode *target_function = nullptr;

	if (!find_inline_variable_target_in_class(p_location, p_lines, p_tree, target, target_function)) {
		candidate.disabled_reason = "Place the caret on a local variable declaration or use.";
		return candidate;
	}

	candidate.matched = true;
	if (target_function == nullptr) {
		candidate.disabled_reason = "Inline variable is only available for local variables.";
		return candidate;
	}
	if (target->initializer == nullptr) {
		candidate.disabled_reason = "This local variable has no initializer.";
		return candidate;
	}
	if (target->assignments > 1) {
		candidate.disabled_reason = "This local variable is reassigned.";
		return candidate;
	}

	String initializer_text;
	RefactorLocation initializer_range;
	if (!get_single_line_node_text(p_lines, target->initializer, initializer_text, &initializer_range)) {
		candidate.disabled_reason = "Inline variable currently supports single-line initializers.";
		return candidate;
	}
	if (inline_declaration_has_trailing_content(p_lines, initializer_range)) {
		candidate.disabled_reason = "Cannot inline declarations with trailing content after the initializer.";
		return candidate;
	}

	InlineVariableUsageCollection usages;
	collect_inline_variable_uses_in_class(p_tree, target, target_function, usages);
	if (usages.has_assignment_target) {
		candidate.disabled_reason = "This local variable is assigned after declaration.";
		return candidate;
	}
	if (usages.has_lambda_capture) {
		candidate.disabled_reason = "Cannot inline a local variable captured by a lambda.";
		return candidate;
	}
	if (usages.reads.is_empty()) {
		candidate.disabled_reason = "No read usages found for this local variable.";
		return candidate;
	}
	if (usages.reads.size() > 1 && expression_has_side_effects(target->initializer)) {
		candidate.disabled_reason = "Cannot inline a side-effecting initializer into multiple uses.";
		return candidate;
	}
	if (!make_inline_variable_declaration_edit(p_lines, target, candidate.declaration_edit)) {
		candidate.disabled_reason = "Cannot remove this local variable declaration safely.";
		return candidate;
	}

	for (const InlineVariableUse &use : usages.reads) {
		RefactorLocation range;
		if (!get_node_text_range(p_lines, use.identifier, range)) {
			candidate.disabled_reason = "Cannot locate a local variable use for replacement.";
			candidate.replacement_edits.clear();
			return candidate;
		}

		RefactorTextEdit edit;
		edit.start_line = range.start_line;
		edit.start_column = range.start_column;
		edit.end_line = range.end_line;
		edit.end_column = range.end_column;
		edit.new_text = inline_replacement_needs_parentheses(target->initializer, use) ? "(" + initializer_text + ")" : initializer_text;
		candidate.replacement_edits.push_back(edit);
	}

	candidate.enabled = true;
	return candidate;
}

bool source_lines_match(const Vector<String> &p_left, const Vector<String> &p_right) {
	if (p_left.size() != p_right.size()) {
		return false;
	}
	for (int i = 0; i < p_left.size(); i++) {
		if (p_left[i] != p_right[i]) {
			return false;
		}
	}
	return true;
}

int get_effective_line_span_end_line(const Vector<String> &p_lines, int p_end_line) {
	if (p_end_line <= p_lines.size()) {
		return p_end_line;
	}
	if (p_end_line == p_lines.size() + 1 && !p_lines.is_empty() && !p_lines[p_lines.size() - 1].is_empty()) {
		return p_lines.size();
	}
	return -1;
}

bool get_line_span_text(const Vector<String> &p_lines, int p_start_line, int p_end_line, String &r_text) {
	const int effective_end_line = get_effective_line_span_end_line(p_lines, p_end_line);
	if (p_start_line < 0 || effective_end_line < p_start_line || p_start_line >= p_lines.size()) {
		return false;
	}

	String text;
	for (int i = p_start_line; i < effective_end_line; i++) {
		text += p_lines[i];
		if (i + 1 < p_lines.size()) {
			text += "\n";
		}
	}
	r_text = text;
	return true;
}

bool make_line_span_edit(
		const Vector<String> &p_lines,
		int p_start_line,
		int p_end_line,
		const String &p_expected_text,
		const String &p_new_text,
		RefactorTextEdit &r_edit) {
	const int effective_end_line = get_effective_line_span_end_line(p_lines, p_end_line);
	if (p_start_line < 0 || effective_end_line < p_start_line || p_start_line >= p_lines.size()) {
		return false;
	}

	r_edit.start_line = p_start_line;
	r_edit.start_column = 0;
	if (effective_end_line < p_lines.size()) {
		r_edit.end_line = effective_end_line;
		r_edit.end_column = 0;
	} else if (!p_lines.is_empty()) {
		r_edit.end_line = p_lines.size() - 1;
		r_edit.end_column = p_lines[p_lines.size() - 1].length();
	} else {
		return false;
	}
	r_edit.has_expected_text = true;
	r_edit.expected_text = p_expected_text;
	r_edit.new_text = p_new_text;
	return true;
}

bool line_span_has_standalone_warning_annotation(const Vector<String> &p_lines, int p_start_line, int p_end_line) {
	const int effective_end_line = get_effective_line_span_end_line(p_lines, p_end_line);
	if (p_start_line < 0 || effective_end_line < p_start_line || p_start_line >= p_lines.size()) {
		return false;
	}

	for (int i = p_start_line; i < effective_end_line; i++) {
		const String stripped_line = p_lines[i].strip_edges();
		if (stripped_line.begins_with("@warning_ignore_start") || stripped_line.begins_with("@warning_ignore_restore")) {
			return true;
		}
	}
	return false;
}

bool line_span_has_blank_line(const Vector<String> &p_lines, int p_start_line, int p_end_line) {
	if (p_start_line < 0 || p_end_line < p_start_line) {
		return false;
	}

	const int end_line = MIN(p_end_line, p_lines.size());
	for (int i = p_start_line; i < end_line; i++) {
		if (p_lines[i].strip_edges().is_empty()) {
			return true;
		}
	}
	return false;
}

bool is_doc_comment_line(const String &p_line) {
	const String stripped_line = p_line.strip_edges();
	return stripped_line.begins_with("##");
}

bool is_ordinary_comment_line(const String &p_line) {
	const String stripped_line = p_line.strip_edges();
	return stripped_line.begins_with("#") && !stripped_line.begins_with("##");
}

int find_attached_style_order_block_start(
		const Vector<String> &p_lines,
		int p_member_start_line,
		int p_floor_line,
		int p_member_annotation_start_line,
		int p_export_group_start_line,
		bool p_has_previous_member_block) {
	if (p_member_start_line < 0 || p_member_start_line >= p_lines.size()) {
		return p_member_start_line;
	}

	const int floor_line = p_floor_line < 0 ? 0 : p_floor_line;
	int start_line = p_member_start_line;
	if (p_member_annotation_start_line >= floor_line && p_member_annotation_start_line < start_line) {
		start_line = p_member_annotation_start_line;
	}
	if (p_export_group_start_line >= floor_line && p_export_group_start_line < start_line) {
		start_line = p_export_group_start_line;
	}
	while (start_line > floor_line && is_doc_comment_line(p_lines[start_line - 1])) {
		start_line--;
	}

	int ordinary_comment_start_line = start_line;
	while (ordinary_comment_start_line > floor_line &&
			is_ordinary_comment_line(p_lines[ordinary_comment_start_line - 1])) {
		ordinary_comment_start_line--;
	}
	const bool comment_has_blank_before = ordinary_comment_start_line > 0 &&
			p_lines[ordinary_comment_start_line - 1].strip_edges().is_empty();
	if (ordinary_comment_start_line < start_line &&
			p_has_previous_member_block &&
			comment_has_blank_before) {
		start_line = ordinary_comment_start_line;
	}

	return start_line;
}

int find_attached_style_order_block_end(
		const Vector<String> &p_lines,
		int p_member_end_line,
		const String &p_trailing_comment_indent) {
	if (p_member_end_line < 0) {
		return p_member_end_line;
	}

	int end_line = p_member_end_line;
	while (end_line < p_lines.size() && is_ordinary_comment_line(p_lines[end_line])) {
		if (!p_trailing_comment_indent.is_empty() &&
				!get_leading_whitespace(p_lines[end_line]).begins_with(p_trailing_comment_indent)) {
			break;
		}
		end_line++;
	}
	return end_line;
}

String normalize_block_text(const String &p_text) {
	if (p_text.is_empty()) {
		return p_text;
	}

	const bool had_trailing_newline = p_text.ends_with("\n");
	const Vector<String> lines = p_text.split("\n");
	int line_count = lines.size();
	if (had_trailing_newline && line_count > 0) {
		line_count--;
	}
	while (line_count > 0 && lines[line_count - 1].strip_edges().is_empty()) {
		line_count--;
	}

	String normalized;
	for (int i = 0; i < line_count; i++) {
		normalized += lines[i];
		if (i + 1 < line_count || had_trailing_newline) {
			normalized += "\n";
		}
	}
	return normalized;
}

bool style_order_bucket_is_callable(StyleOrderBucket p_bucket) {
	switch (p_bucket) {
		case StyleOrderBucket::STATIC_INIT:
		case StyleOrderBucket::STATIC_METHOD:
		case StyleOrderBucket::BUILTIN_VIRTUAL_METHOD:
		case StyleOrderBucket::CUSTOM_OVERRIDE_METHOD:
		case StyleOrderBucket::PUBLIC_METHOD:
		case StyleOrderBucket::PRIVATE_METHOD:
			return true;
		case StyleOrderBucket::SIGNAL:
		case StyleOrderBucket::ENUM:
		case StyleOrderBucket::CONSTANT:
		case StyleOrderBucket::STATIC_VARIABLE:
		case StyleOrderBucket::EXPORTED_VARIABLE:
		case StyleOrderBucket::PUBLIC_VARIABLE:
		case StyleOrderBucket::PRIVATE_VARIABLE:
		case StyleOrderBucket::ONREADY_PUBLIC_VARIABLE:
		case StyleOrderBucket::ONREADY_PRIVATE_VARIABLE:
		case StyleOrderBucket::INNER_TYPE:
			return false;
	}
	return false;
}

bool style_order_buckets_need_blank_line_between(StyleOrderBucket p_previous, StyleOrderBucket p_current) {
	if (p_previous == StyleOrderBucket::INNER_TYPE || p_current == StyleOrderBucket::INNER_TYPE) {
		return true;
	}
	if (p_previous == p_current) {
		return false;
	}
	return !style_order_bucket_is_callable(p_previous) ||
			!style_order_bucket_is_callable(p_current);
}

String join_style_order_blocks(const Vector<StyleOrderBlock> &p_blocks) {
	// Blocks are expected to be newline-normalized before joining.
	String text;
	for (int i = 0; i < p_blocks.size(); i++) {
		if (i > 0) {
			if (!text.ends_with("\n")) {
				text += "\n";
			}
			if (style_order_buckets_need_blank_line_between(p_blocks[i - 1].bucket, p_blocks[i].bucket)) {
				text += "\n";
			}
		}
		text += p_blocks[i].text;
	}
	return text;
}

String match_style_order_span_final_newline(const String &p_text, bool p_should_end_with_newline) {
	if (p_should_end_with_newline) {
		return p_text.ends_with("\n") ? p_text : p_text + "\n";
	}

	String text = p_text;
	while (text.ends_with("\n")) {
		text = text.substr(0, text.length() - 1);
	}
	return text;
}

String style_order_member_name(const FSParser::ClassNode::Member &p_member) {
	switch (p_member.type) {
		case FSParser::ClassNode::Member::CLASS:
			if (p_member.m_class == nullptr || p_member.m_class->identifier == nullptr) {
				return String();
			}
			return String(p_member.m_class->identifier->name);
		case FSParser::ClassNode::Member::CONSTANT:
			if (p_member.constant == nullptr || p_member.constant->identifier == nullptr) {
				return String();
			}
			return String(p_member.constant->identifier->name);
		case FSParser::ClassNode::Member::FUNCTION:
			if (p_member.function == nullptr || p_member.function->identifier == nullptr) {
				return String();
			}
			return String(p_member.function->identifier->name);
		case FSParser::ClassNode::Member::SIGNAL:
			if (p_member.signal == nullptr || p_member.signal->identifier == nullptr) {
				return String();
			}
			return String(p_member.signal->identifier->name);
		case FSParser::ClassNode::Member::VARIABLE:
			if (p_member.variable == nullptr || p_member.variable->identifier == nullptr) {
				return String();
			}
			return String(p_member.variable->identifier->name);
		case FSParser::ClassNode::Member::ENUM:
			if (p_member.m_enum == nullptr || p_member.m_enum->identifier == nullptr) {
				return String();
			}
			return String(p_member.m_enum->identifier->name);
		case FSParser::ClassNode::Member::ENUM_VALUE:
			return p_member.enum_value.identifier != nullptr ? String(p_member.enum_value.identifier->name) : String();
		case FSParser::ClassNode::Member::GROUP:
			return p_member.annotation != nullptr ? String(p_member.annotation->export_info.name) : String();
		case FSParser::ClassNode::Member::TUPLE:
			if (p_member.m_tuple == nullptr || p_member.m_tuple->identifier == nullptr) {
				return String();
			}
			return String(p_member.m_tuple->identifier->name);
		case FSParser::ClassNode::Member::UNDEFINED:
			return String();
	}
	return String();
}

bool is_private_style_order_name(const String &p_name) {
	return p_name.begins_with("_");
}

bool native_class_has_function(const StringName &p_class_name, const StringName &p_function_name) {
	return p_class_name != StringName() &&
			ClassDB::class_exists(p_class_name) &&
			ClassDB::has_method(p_class_name, p_function_name);
}

bool native_class_has_virtual_method(const StringName &p_class_name, const StringName &p_function_name) {
	if (p_class_name == StringName() || p_function_name == StringName() || !ClassDB::class_exists(p_class_name)) {
		return false;
	}

	List<MethodInfo> virtual_methods;
	ClassDB::get_virtual_methods(p_class_name, &virtual_methods);
	for (const MethodInfo &method : virtual_methods) {
		if (method.name == String(p_function_name)) {
			return true;
		}
	}
	return false;
}

const FSParser::ClassNode *find_style_order_local_class(
		const FSParser::ClassNode *p_class,
		const StringName &p_class_name) {
	if (p_class == nullptr || p_class_name == StringName()) {
		return nullptr;
	}

	for (const FSParser::ClassNode *scope = p_class->outer; scope != nullptr; scope = scope->outer) {
		for (const FSParser::ClassNode::Member &member : scope->members) {
			if (member.type == FSParser::ClassNode::Member::CLASS &&
					member.m_class != nullptr &&
					member.m_class->identifier != nullptr &&
					member.m_class->identifier->name == p_class_name) {
				return member.m_class;
			}
		}
	}
	return nullptr;
}

bool style_order_class_has_single_identifier_extends(const FSParser::ClassNode *p_class) {
	return p_class != nullptr &&
			p_class->extends_path.is_empty() &&
			p_class->extends.size() == 1 &&
			p_class->extends[0] != nullptr;
}

const FSParser::ClassNode *find_style_order_syntax_base_class(const FSParser::ClassNode *p_class) {
	if (!style_order_class_has_single_identifier_extends(p_class)) {
		return nullptr;
	}
	return find_style_order_local_class(p_class, p_class->extends[0]->name);
}

constexpr int STYLE_ORDER_MAX_BASE_CHAIN_DEPTH = 64;

bool class_has_syntax_base_function(const FSParser::ClassNode *p_class, const StringName &p_function_name) {
	if (p_class == nullptr || p_function_name == StringName()) {
		return false;
	}

	const FSParser::ClassNode *base_class = find_style_order_syntax_base_class(p_class);
	for (int depth = 0; base_class != nullptr && depth < STYLE_ORDER_MAX_BASE_CHAIN_DEPTH; depth++) {
		if (base_class->has_function(p_function_name)) {
			return true;
		}
		base_class = find_style_order_syntax_base_class(base_class);
	}
	return false;
}

bool class_has_syntax_native_virtual_method(
		const FSParser::ClassNode *p_class,
		const StringName &p_function_name) {
	if (p_class == nullptr || p_function_name == StringName()) {
		return false;
	}

	const FSParser::ClassNode *current_class = p_class;
	for (int depth = 0; current_class != nullptr && depth < STYLE_ORDER_MAX_BASE_CHAIN_DEPTH; depth++) {
		if (!current_class->extends_used && native_class_has_virtual_method(SNAME("RefCounted"), p_function_name)) {
			return true;
		}
		if (style_order_class_has_single_identifier_extends(current_class)) {
			const StringName base_name = current_class->extends[0]->name;
			if (native_class_has_virtual_method(base_name, p_function_name)) {
				return true;
			}
		}
		current_class = find_style_order_syntax_base_class(current_class);
	}
	return false;
}

bool class_has_native_base_virtual_method(const FSParser::ClassNode *p_class, const StringName &p_function_name) {
	if (p_class == nullptr || p_function_name == StringName()) {
		return false;
	}

	FSParser::DataType base_type = p_class->base_type;
	while (true) {
		switch (base_type.kind) {
			case FSParser::DataType::CLASS: {
				const FSParser::ClassNode *base_class = base_type.class_type;
				int depth = 0;
				while (base_class != nullptr && depth < STYLE_ORDER_MAX_BASE_CHAIN_DEPTH) {
					depth++;
					base_type = base_class->base_type;
					if (base_type.kind != FSParser::DataType::CLASS) {
						break;
					}
					base_class = base_type.class_type;
				}
				if (base_type.kind == FSParser::DataType::CLASS) {
					return false;
				}
				continue;
			}
			case FSParser::DataType::SCRIPT: {
				Ref<Script> base_script = base_type.script_type;
				StringName native_type = base_type.native_type;
				while (base_script.is_valid()) {
					if (native_type == StringName()) {
						native_type = base_script->get_instance_base_type();
					}
					base_script = base_script->get_base_script();
				}
				return native_class_has_virtual_method(native_type, p_function_name);
			}
			case FSParser::DataType::NATIVE:
				return native_class_has_virtual_method(base_type.native_type, p_function_name);
			case FSParser::DataType::BUILTIN:
			case FSParser::DataType::ENUM:
			case FSParser::DataType::TUPLE:
			case FSParser::DataType::TYPE_PARAMETER:
			case FSParser::DataType::VARIANT:
			case FSParser::DataType::RESOLVING:
			case FSParser::DataType::UNRESOLVED:
				return false;
		}
	}
	return false;
}

bool class_has_base_function(const FSParser::ClassNode *p_class, const StringName &p_function_name) {
	if (p_class == nullptr || p_function_name == StringName()) {
		return false;
	}

	FSParser::DataType base_type = p_class->base_type;
	while (true) {
		switch (base_type.kind) {
			case FSParser::DataType::CLASS: {
				const FSParser::ClassNode *base_class = base_type.class_type;
				int depth = 0;
				while (base_class != nullptr && depth < STYLE_ORDER_MAX_BASE_CHAIN_DEPTH) {
					depth++;
					if (base_class->has_member(p_function_name) &&
							base_class->get_member(p_function_name).type == FSParser::ClassNode::Member::FUNCTION) {
						return true;
					}

					base_type = base_class->base_type;
					if (base_type.kind != FSParser::DataType::CLASS) {
						break;
					}
					base_class = base_type.class_type;
				}
				if (base_type.kind == FSParser::DataType::CLASS) {
					return false;
				}
				continue;
			}
			case FSParser::DataType::SCRIPT: {
				Ref<Script> base_script = base_type.script_type;
				StringName native_type = base_type.native_type;
				while (base_script.is_valid()) {
					if (base_script->has_method(p_function_name)) {
						return true;
					}
					if (native_type == StringName()) {
						native_type = base_script->get_instance_base_type();
					}
					base_script = base_script->get_base_script();
				}
				return native_class_has_function(native_type, p_function_name);
			}
			case FSParser::DataType::NATIVE:
				return native_class_has_function(base_type.native_type, p_function_name);
			case FSParser::DataType::BUILTIN:
			case FSParser::DataType::ENUM:
			case FSParser::DataType::TUPLE:
			case FSParser::DataType::TYPE_PARAMETER:
			case FSParser::DataType::VARIANT:
			case FSParser::DataType::RESOLVING:
			case FSParser::DataType::UNRESOLVED:
				return false;
		}
	}
	return false;
}

bool style_order_variable_has_export_annotation(const FSParser::VariableNode *p_variable) {
	if (p_variable == nullptr) {
		return false;
	}
	if (p_variable->exported) {
		return true;
	}

	for (const FSParser::AnnotationNode *annotation : p_variable->annotations) {
		if (annotation != nullptr && String(annotation->name).begins_with("@export")) {
			return true;
		}
	}
	return false;
}

bool style_order_export_group_has_variable_target(const FSParser::ClassNode *p_class, int p_group_index) {
	if (p_class == nullptr || p_group_index < 0 || p_group_index >= p_class->members.size()) {
		return false;
	}

	for (int i = p_group_index + 1; i < p_class->members.size(); i++) {
		const FSParser::ClassNode::Member &member = p_class->members[i];
		if (member.type == FSParser::ClassNode::Member::GROUP) {
			continue;
		}
		return member.type == FSParser::ClassNode::Member::VARIABLE &&
				style_order_variable_has_export_annotation(member.variable);
	}
	return false;
}

bool style_order_variable_has_onready_annotation(const FSParser::VariableNode *p_variable) {
	if (p_variable == nullptr) {
		return false;
	}
	if (p_variable->onready) {
		return true;
	}

	for (const FSParser::AnnotationNode *annotation : p_variable->annotations) {
		if (annotation != nullptr && annotation->name == "@onready") {
			return true;
		}
	}
	return false;
}

StyleOrderBucket get_style_order_bucket(
		const FSParser::ClassNode *p_class,
		const FSParser::ClassNode::Member &p_member,
		bool p_analysis_ok) {
	const String member_name = style_order_member_name(p_member);

	switch (p_member.type) {
		case FSParser::ClassNode::Member::SIGNAL:
			return StyleOrderBucket::SIGNAL;
		case FSParser::ClassNode::Member::ENUM:
		case FSParser::ClassNode::Member::ENUM_VALUE:
			return StyleOrderBucket::ENUM;
		case FSParser::ClassNode::Member::CONSTANT:
			return StyleOrderBucket::CONSTANT;
		case FSParser::ClassNode::Member::VARIABLE:
			if (p_member.variable != nullptr && p_member.variable->is_static) {
				return StyleOrderBucket::STATIC_VARIABLE;
			}
			if (style_order_variable_has_export_annotation(p_member.variable)) {
				return StyleOrderBucket::EXPORTED_VARIABLE;
			}
			if (style_order_variable_has_onready_annotation(p_member.variable)) {
				if (is_private_style_order_name(member_name)) {
					return StyleOrderBucket::ONREADY_PRIVATE_VARIABLE;
				}
				return StyleOrderBucket::ONREADY_PUBLIC_VARIABLE;
			}
			if (is_private_style_order_name(member_name)) {
				return StyleOrderBucket::PRIVATE_VARIABLE;
			}
			return StyleOrderBucket::PUBLIC_VARIABLE;
		case FSParser::ClassNode::Member::FUNCTION: {
			if (p_member.function != nullptr && p_member.function->is_static) {
				return member_name == "_static_init" ? StyleOrderBucket::STATIC_INIT : StyleOrderBucket::STATIC_METHOD;
			}
			const StringName function_name = p_member.function != nullptr && p_member.function->identifier != nullptr ? p_member.function->identifier->name : StringName();
			const bool is_builtin_virtual = function_name != StringName() &&
					(p_analysis_ok ? class_has_native_base_virtual_method(p_class, function_name) : class_has_syntax_native_virtual_method(p_class, function_name));
			if (function_name == SNAME("_init") || is_builtin_virtual) {
				return StyleOrderBucket::BUILTIN_VIRTUAL_METHOD;
			}
			const bool is_custom_override = function_name != StringName() &&
					(p_analysis_ok ? class_has_base_function(p_class, function_name) : class_has_syntax_base_function(p_class, function_name));
			if (function_name != StringName() &&
					is_custom_override) {
				return StyleOrderBucket::CUSTOM_OVERRIDE_METHOD;
			}
			return is_private_style_order_name(member_name) ? StyleOrderBucket::PRIVATE_METHOD : StyleOrderBucket::PUBLIC_METHOD;
		}
		case FSParser::ClassNode::Member::CLASS:
		case FSParser::ClassNode::Member::TUPLE:
			return StyleOrderBucket::INNER_TYPE;
		case FSParser::ClassNode::Member::GROUP:
			return StyleOrderBucket::EXPORTED_VARIABLE;
		case FSParser::ClassNode::Member::UNDEFINED:
			return StyleOrderBucket::PUBLIC_METHOD;
	}
	return StyleOrderBucket::PUBLIC_METHOD;
}

const FSParser::Node *get_style_order_member_source_node(const FSParser::ClassNode::Member &p_member) {
	if (p_member.type == FSParser::ClassNode::Member::ENUM_VALUE) {
		return p_member.enum_value.parent_enum;
	}
	return p_member.get_source_node();
}

int get_style_order_member_annotation_start_line(const FSParser::ClassNode::Member &p_member) {
	const FSParser::Node *node = get_style_order_member_source_node(p_member);
	if (node == nullptr || node->start_line <= 0) {
		return -1;
	}

	int annotation_start_line = -1;
	for (const FSParser::AnnotationNode *annotation : node->annotations) {
		if (annotation == nullptr || annotation->start_line <= 0 || annotation->start_line >= node->start_line) {
			continue;
		}

		const int line = annotation->start_line - 1;
		if (annotation_start_line < 0 || line < annotation_start_line) {
			annotation_start_line = line;
		}
	}
	return annotation_start_line;
}

int get_style_order_member_start_line(const FSParser::ClassNode::Member &p_member) {
	if (p_member.type == FSParser::ClassNode::Member::UNDEFINED) {
		return -1;
	}
	const FSParser::Node *node = get_style_order_member_source_node(p_member);
	return node != nullptr && node->start_line > 0 ? node->start_line - 1 : -1;
}

int get_style_order_member_end_line(const FSParser::ClassNode::Member &p_member) {
	const FSParser::Node *node = get_style_order_member_source_node(p_member);
	if (node != nullptr && node->end_line > 0) {
		return node->end_line;
	}
	return -1;
}

bool is_style_order_unnamed_enum_continuation(const FSParser::ClassNode *p_class, int p_member_index) {
	if (p_class == nullptr || p_member_index <= 0 || p_member_index >= p_class->members.size()) {
		return false;
	}

	const FSParser::ClassNode::Member &member = p_class->members[p_member_index];
	const FSParser::ClassNode::Member &previous_member = p_class->members[p_member_index - 1];
	return member.type == FSParser::ClassNode::Member::ENUM_VALUE &&
			previous_member.type == FSParser::ClassNode::Member::ENUM_VALUE &&
			member.enum_value.parent_enum != nullptr &&
			member.enum_value.parent_enum == previous_member.enum_value.parent_enum;
}

StyleOrderCandidate find_style_order_candidate_in_class(
		const Vector<String> &p_lines,
		const FSParser::ClassNode *p_class,
		bool p_is_root_class,
		bool p_analysis_ok) {
	StyleOrderCandidate candidate;
	if (p_class == nullptr || p_class->members.size() < 2) {
		candidate.disabled_reason = "Members are already sorted by the FoundryScript style guide.";
		return candidate;
	}

	Vector<StyleOrderBlock> blocks;
	int block_floor_line = 0;
	int pending_export_group_start_line = -1;
	for (int i = 0; i < p_class->members.size(); i++) {
		if (is_style_order_unnamed_enum_continuation(p_class, i)) {
			continue;
		}

		const FSParser::ClassNode::Member &member = p_class->members[i];
		if (member.type == FSParser::ClassNode::Member::GROUP) {
			if (!style_order_export_group_has_variable_target(p_class, i)) {
				candidate.disabled_reason = "Cannot sort members because an export group is not followed by an exported variable.";
				return candidate;
			}
			const int group_start_line = get_style_order_member_start_line(member);
			if (group_start_line < 0) {
				candidate.disabled_reason = "Cannot map a member declaration back to source text.";
				return candidate;
			}
			if (pending_export_group_start_line < 0 || group_start_line < pending_export_group_start_line) {
				pending_export_group_start_line = group_start_line;
			}
			continue;
		}

		const int member_start_line = get_style_order_member_start_line(member);
		const int member_annotation_start_line = get_style_order_member_annotation_start_line(member);
		const int block_start_line = find_attached_style_order_block_start(
				p_lines,
				member_start_line,
				block_floor_line,
				member_annotation_start_line,
				pending_export_group_start_line,
				!blocks.is_empty());
		const int member_end_line = get_style_order_member_end_line(member);
		const String trailing_comment_indent = !p_is_root_class && member_start_line >= 0 && member_start_line < p_lines.size() ? get_leading_whitespace(p_lines[member_start_line]) : String();
		const int block_end_line = find_attached_style_order_block_end(p_lines, member_end_line, trailing_comment_indent);
		if (block_start_line < 0 || member_end_line <= block_start_line || block_end_line <= block_start_line) {
			candidate.disabled_reason = "Cannot map a member declaration back to source text.";
			return candidate;
		}

		if (pending_export_group_start_line >= 0) {
			if (block_start_line > pending_export_group_start_line) {
				candidate.disabled_reason = "Cannot sort members because an export group is not followed by an exported variable.";
				return candidate;
			}
			if (line_span_has_blank_line(p_lines, pending_export_group_start_line + 1, member_start_line)) {
				candidate.disabled_reason = "Cannot sort members because an export group is separated from its exported variable.";
				return candidate;
			}
			pending_export_group_start_line = -1;
		}

		if (!blocks.is_empty()) {
			blocks.write[blocks.size() - 1].end_line = block_start_line;
		}

		StyleOrderBlock block;
		block.original_index = i;
		block.bucket = get_style_order_bucket(p_class, member, p_analysis_ok);
		block.start_line = block_start_line;
		block.end_line = block_end_line;
		blocks.push_back(block);
		block_floor_line = member_end_line;
	}

	for (int i = 0; i < blocks.size(); i++) {
		if (blocks[i].start_line < 0 || blocks[i].end_line <= blocks[i].start_line) {
			candidate.disabled_reason = "Cannot map a member declaration back to source text.";
			return candidate;
		}

		String text;
		if (!get_line_span_text(p_lines, blocks[i].start_line, blocks[i].end_line, text)) {
			candidate.disabled_reason = "Cannot map a member declaration back to source text.";
			return candidate;
		}
		blocks.write[i].text = normalize_block_text(text);
	}

	if (blocks.size() < 2) {
		candidate.disabled_reason = "Members are already sorted by the FoundryScript style guide.";
		return candidate;
	}

	Vector<StyleOrderBlock> sorted_blocks = blocks;
	sorted_blocks.sort_custom<StyleOrderBlockComparator>();

	int edit_end_line = blocks[blocks.size() - 1].end_line;
	if (!p_is_root_class) {
		int trailing_blank_end_line = edit_end_line;
		int effective_end_line = get_effective_line_span_end_line(p_lines, trailing_blank_end_line);
		while (effective_end_line >= 0 && effective_end_line < p_lines.size() &&
				p_lines[effective_end_line].strip_edges().is_empty()) {
			trailing_blank_end_line = effective_end_line + 1;
			effective_end_line = get_effective_line_span_end_line(p_lines, trailing_blank_end_line);
		}
		if (effective_end_line == p_lines.size()) {
			edit_end_line = trailing_blank_end_line;
		}
	}

	String expected_text;
	if (!get_line_span_text(p_lines, blocks[0].start_line, edit_end_line, expected_text)) {
		candidate.disabled_reason = "Cannot map a member declaration back to source text.";
		return candidate;
	}

	const String replacement_text = match_style_order_span_final_newline(
			join_style_order_blocks(sorted_blocks),
			expected_text.ends_with("\n"));
	if (replacement_text == expected_text) {
		candidate.disabled_reason = "Members are already sorted by the FoundryScript style guide.";
		return candidate;
	}
	if (line_span_has_standalone_warning_annotation(p_lines, blocks[0].start_line, edit_end_line)) {
		candidate.disabled_reason = "Cannot sort members while standalone warning annotations are present.";
		return candidate;
	}

	RefactorTextEdit edit;
	if (!make_line_span_edit(
				p_lines,
				blocks[0].start_line,
				edit_end_line,
				expected_text,
				replacement_text,
				edit)) {
		candidate.disabled_reason = "Cannot map a member declaration back to source text.";
		return candidate;
	}
	candidate.enabled = true;
	candidate.edits.push_back(edit);
	return candidate;
}

bool is_style_order_already_sorted_reason(const String &p_reason) {
	return p_reason == "Members are already sorted by the FoundryScript style guide.";
}

int compare_style_order_position(int p_a_line, int p_a_column, int p_b_line, int p_b_column) {
	if (p_a_line != p_b_line) {
		return p_a_line < p_b_line ? -1 : 1;
	}
	if (p_a_column == p_b_column) {
		return 0;
	}
	return p_a_column < p_b_column ? -1 : 1;
}

bool style_order_edits_have_same_span(const RefactorTextEdit &p_a, const RefactorTextEdit &p_b) {
	return compare_style_order_position(p_a.start_line, p_a.start_column, p_b.start_line, p_b.start_column) == 0 &&
			compare_style_order_position(p_a.end_line, p_a.end_column, p_b.end_line, p_b.end_column) == 0;
}

bool style_order_edit_contains_span(const RefactorTextEdit &p_outer, const RefactorTextEdit &p_inner) {
	const bool starts_before_or_at_inner =
			compare_style_order_position(
					p_outer.start_line,
					p_outer.start_column,
					p_inner.start_line,
					p_inner.start_column) <= 0;
	const bool ends_after_or_at_inner =
			compare_style_order_position(
					p_outer.end_line,
					p_outer.end_column,
					p_inner.end_line,
					p_inner.end_column) >= 0;
	return starts_before_or_at_inner && ends_after_or_at_inner && !style_order_edits_have_same_span(p_outer, p_inner);
}

void remove_nested_style_order_edits(Vector<RefactorTextEdit> &r_edits) {
	Vector<RefactorTextEdit> filtered_edits;
	for (int i = 0; i < r_edits.size(); i++) {
		bool is_nested = false;
		for (int j = 0; j < r_edits.size(); j++) {
			if (i == j) {
				continue;
			}
			if (style_order_edit_contains_span(r_edits[j], r_edits[i]) ||
					(style_order_edits_have_same_span(r_edits[j], r_edits[i]) && j < i)) {
				is_nested = true;
				break;
			}
		}
		if (!is_nested) {
			filtered_edits.push_back(r_edits[i]);
		}
	}
	r_edits = filtered_edits;
}

void collect_style_order_class_edits(
		const Vector<String> &p_lines,
		const FSParser::ClassNode *p_class,
		bool p_is_root_class,
		bool p_analysis_ok,
		Vector<RefactorTextEdit> &r_edits,
		String &r_disabled_reason) {
	if (p_class == nullptr || !r_disabled_reason.is_empty()) {
		return;
	}

	const StyleOrderCandidate candidate = find_style_order_candidate_in_class(
			p_lines,
			p_class,
			p_is_root_class,
			p_analysis_ok);
	if (candidate.enabled) {
		for (const RefactorTextEdit &edit : candidate.edits) {
			r_edits.push_back(edit);
		}
		return;
	} else if (!is_style_order_already_sorted_reason(candidate.disabled_reason)) {
		r_disabled_reason = candidate.disabled_reason;
		return;
	}

	for (const FSParser::ClassNode::Member &member : p_class->members) {
		if (member.type != FSParser::ClassNode::Member::CLASS) {
			continue;
		}
		collect_style_order_class_edits(p_lines, member.m_class, false, p_analysis_ok, r_edits, r_disabled_reason);
		if (!r_disabled_reason.is_empty()) {
			return;
		}
	}
}

void make_whole_source_style_order_edit(
		const String &p_source,
		const String &p_transformed,
		RefactorTextEdit &r_edit) {
	// Sorting can touch multiple nested class spans, so expose it as one whole-source edit.
	r_edit.start_line = 0;
	r_edit.start_column = 0;
	get_source_end_position(p_source, r_edit.end_line, r_edit.end_column);
	r_edit.has_expected_text = true;
	r_edit.expected_text = p_source;
	r_edit.new_text = p_transformed;
}

bool apply_style_order_pass(
		const String &p_source,
		const String &p_path,
		String &r_transformed,
		String &r_disabled_reason) {
	r_transformed = p_source;
	r_disabled_reason = String();

	FSParser parser;
	const Error err = parser.parse(p_source, p_path, false);
	if (err != OK || parser.get_tree() == nullptr) {
		r_disabled_reason = "Cannot parse this script.";
		return false;
	}

	FSAnalyzer analyzer(&parser);
	const bool analysis_ok = analyzer.analyze() == OK;

	const Vector<String> lines = p_source.split("\n");
	Vector<RefactorTextEdit> edits;
	collect_style_order_class_edits(lines, parser.get_tree(), true, analysis_ok, edits, r_disabled_reason);
	if (!r_disabled_reason.is_empty()) {
		return false;
	}
	if (edits.is_empty()) {
		return true;
	}

	remove_nested_style_order_edits(edits);
	if (edits.is_empty()) {
		return true;
	}

	if (!FSRefactorEdits::apply(p_source, edits, r_transformed)) {
		r_transformed = p_source;
		r_disabled_reason = "Cannot apply style-order edits to this script.";
		return false;
	}
	return true;
}

// Cache locks only protect shared state. Concurrent misses may compute the same pure candidate twice.
bool get_cached_type_annotation_candidate(const RefactorContext &p_context, const RefactorLocation &p_location, TypeAnnotationCandidate &r_candidate) {
	MutexLock lock(refactor_candidate_cache_mutex);

	if (!type_annotation_cache.valid ||
			type_annotation_cache.path != p_context.path ||
			type_annotation_cache.source_hash != p_context.source.hash64() ||
			type_annotation_cache.source_length != p_context.source.length() ||
			!same_location(type_annotation_cache.location, p_location)) {
		return false;
	}
	r_candidate = type_annotation_cache.candidate;
	return true;
}

void cache_type_annotation_candidate(const RefactorContext &p_context, const RefactorLocation &p_location, const TypeAnnotationCandidate &p_candidate) {
	MutexLock lock(refactor_candidate_cache_mutex);

	type_annotation_cache.valid = true;
	type_annotation_cache.path = p_context.path;
	type_annotation_cache.source_hash = p_context.source.hash64();
	type_annotation_cache.source_length = p_context.source.length();
	type_annotation_cache.location = p_location;
	type_annotation_cache.candidate = p_candidate;
}

bool get_cached_extract_variable_candidate(const RefactorContext &p_context, const RefactorLocation &p_location, ExtractVariableCandidate &r_candidate) {
	MutexLock lock(refactor_candidate_cache_mutex);

	if (!extract_variable_cache.valid ||
			extract_variable_cache.path != p_context.path ||
			extract_variable_cache.source_hash != p_context.source.hash64() ||
			extract_variable_cache.source_length != p_context.source.length() ||
			!same_location(extract_variable_cache.location, p_location)) {
		return false;
	}
	r_candidate = extract_variable_cache.candidate;
	return true;
}

void cache_extract_variable_candidate(const RefactorContext &p_context, const RefactorLocation &p_location, const ExtractVariableCandidate &p_candidate) {
	MutexLock lock(refactor_candidate_cache_mutex);

	extract_variable_cache.valid = true;
	extract_variable_cache.path = p_context.path;
	extract_variable_cache.source_hash = p_context.source.hash64();
	extract_variable_cache.source_length = p_context.source.length();
	extract_variable_cache.location = p_location;
	extract_variable_cache.candidate = p_candidate;
}

bool get_cached_extract_method_candidate(
		const RefactorContext &p_context,
		const RefactorLocation &p_location,
		ExtractMethodCandidate &r_candidate) {
	MutexLock lock(refactor_candidate_cache_mutex);

	if (!extract_method_cache.valid ||
			extract_method_cache.path != p_context.path ||
			extract_method_cache.source_hash != p_context.source.hash64() ||
			extract_method_cache.source_length != p_context.source.length() ||
			!same_location(extract_method_cache.location, p_location)) {
		return false;
	}
	r_candidate = extract_method_cache.candidate;
	return true;
}

void cache_extract_method_candidate(
		const RefactorContext &p_context,
		const RefactorLocation &p_location,
		const ExtractMethodCandidate &p_candidate) {
	MutexLock lock(refactor_candidate_cache_mutex);

	extract_method_cache.valid = true;
	extract_method_cache.path = p_context.path;
	extract_method_cache.source_hash = p_context.source.hash64();
	extract_method_cache.source_length = p_context.source.length();
	extract_method_cache.location = p_location;
	extract_method_cache.candidate = p_candidate;
}

bool get_cached_inline_variable_candidate(const RefactorContext &p_context, const RefactorLocation &p_location, InlineVariableCandidate &r_candidate) {
	MutexLock lock(refactor_candidate_cache_mutex);

	if (!inline_variable_cache.valid ||
			inline_variable_cache.path != p_context.path ||
			inline_variable_cache.source_hash != p_context.source.hash64() ||
			inline_variable_cache.source_length != p_context.source.length() ||
			!same_location(inline_variable_cache.location, p_location)) {
		return false;
	}
	r_candidate = inline_variable_cache.candidate;
	return true;
}

void cache_inline_variable_candidate(const RefactorContext &p_context, const RefactorLocation &p_location, const InlineVariableCandidate &p_candidate) {
	MutexLock lock(refactor_candidate_cache_mutex);

	inline_variable_cache.valid = true;
	inline_variable_cache.path = p_context.path;
	inline_variable_cache.source_hash = p_context.source.hash64();
	inline_variable_cache.source_length = p_context.source.length();
	inline_variable_cache.location = p_location;
	inline_variable_cache.candidate = p_candidate;
}

void collect_type_annotation_in_suite(const Vector<String> &p_lines, const FSParser::SuiteNode *p_suite, Vector<TypeAnnotationCandidate> &r_candidates, const FSParser::SuiteNode *p_function_body = nullptr, const TypeAnnotationRenderContext *p_render_context = nullptr);

bool get_cached_implement_abstract_candidate(const RefactorContext &p_context, const RefactorLocation &p_location, ImplementAbstractCandidate &r_candidate) {
	MutexLock lock(refactor_candidate_cache_mutex);

	if (!implement_abstract_cache.valid ||
			implement_abstract_cache.path != p_context.path ||
			implement_abstract_cache.source_hash != p_context.source.hash64() ||
			implement_abstract_cache.source_length != p_context.source.length() ||
			!same_location(implement_abstract_cache.location, p_location)) {
		return false;
	}
	r_candidate = implement_abstract_cache.candidate;
	return true;
}

void cache_implement_abstract_candidate(const RefactorContext &p_context, const RefactorLocation &p_location, const ImplementAbstractCandidate &p_candidate) {
	MutexLock lock(refactor_candidate_cache_mutex);

	implement_abstract_cache.valid = true;
	implement_abstract_cache.path = p_context.path;
	implement_abstract_cache.source_hash = p_context.source.hash64();
	implement_abstract_cache.source_length = p_context.source.length();
	implement_abstract_cache.location = p_location;
	implement_abstract_cache.candidate = p_candidate;
}

bool get_cached_style_order_candidate(const RefactorContext &p_context, StyleOrderCandidate &r_candidate) {
	MutexLock lock(refactor_candidate_cache_mutex);

	if (!style_order_cache.valid ||
			style_order_cache.path != p_context.path ||
			style_order_cache.source_hash != p_context.source.hash64() ||
			style_order_cache.source_length != p_context.source.length()) {
		return false;
	}
	r_candidate = style_order_cache.candidate;
	return true;
}

void cache_style_order_candidate(const RefactorContext &p_context, const StyleOrderCandidate &p_candidate) {
	MutexLock lock(refactor_candidate_cache_mutex);

	style_order_cache.valid = true;
	style_order_cache.path = p_context.path;
	style_order_cache.source_hash = p_context.source.hash64();
	style_order_cache.source_length = p_context.source.length();
	style_order_cache.candidate = p_candidate;
}

void collect_assignable_candidate(const Vector<String> &p_lines, const FSParser::AssignableNode *p_assignable, const String &p_kind, bool p_has_keyword, Vector<TypeAnnotationCandidate> &r_candidates, const FSParser::SuiteNode *p_function_body = nullptr, const FSParser::ClassNode *p_member_class = nullptr, const TypeAnnotationRenderContext *p_render_context = nullptr, const MemberSubclassClosure *p_member_subclasses = nullptr) {
	TypeAnnotationCandidate candidate;
	if (find_assignable_type_annotation(p_lines, p_assignable, p_kind, p_has_keyword, candidate, p_function_body, p_member_class, p_render_context, p_member_subclasses) && candidate.matched) {
		r_candidates.push_back(candidate);
	}
}

// Collects the names declared directly in p_suite (its own locals and loop/
// pattern binds), without descending into nested blocks, into r_names. This
// mirrors the analyzer's `SuiteNode::has_local()`, which resolves a name against
// the current suite and its parents only: a local declared in a nested block
// does not shadow names at an enclosing site. The collector adds each suite's
// own names as it descends, so a site sees exactly the locals in scope there,
// instead of a whole-function union that could disable an otherwise valid
// qualified annotation rooted at a name only used in some unrelated branch.
void collect_own_suite_local_names(const FSParser::SuiteNode *p_suite, Vector<String> &r_names) {
	if (p_suite == nullptr) {
		return;
	}
	for (const FSParser::SuiteNode::Local &local : p_suite->locals) {
		if (local.name != StringName()) {
			r_names.push_back(local.name);
		}
	}
}

void collect_type_annotation_in_function(
		const Vector<String> &p_lines,
		const FSParser::ClassNode *p_class,
		const FSParser::FunctionNode *p_function,
		const RefactorLocation *p_location,
		Vector<TypeAnnotationCandidate> &r_candidates,
		const TypeAnnotationRenderContext *p_render_context
#ifndef FOUNDRY_SCRIPT_NO_LSP
		,
		const Ref<FSWorkspace> &p_workspace,
		const ExtendFSParser *p_parser,
		const FSParseResultProvider *p_parse_results
#endif // FOUNDRY_SCRIPT_NO_LSP
) {
	if (p_function == nullptr) {
		return;
	}
	// Augment the file-level scope with this function's parameter and local names
	// so a qualified annotation never roots at a symbol shadowed inside the body.
	TypeAnnotationRenderContext function_context;
	const TypeAnnotationRenderContext *render_context = p_render_context;
	if (p_render_context != nullptr) {
		function_context = *p_render_context;
		for (const FSParser::ParameterNode *parameter : p_function->parameters) {
			if (parameter != nullptr && parameter->identifier != nullptr) {
				function_context.scope.shadowing_local_names.push_back(parameter->identifier->name);
			}
		}
		if (p_function->rest_parameter != nullptr && p_function->rest_parameter->identifier != nullptr) {
			function_context.scope.shadowing_local_names.push_back(p_function->rest_parameter->identifier->name);
		}
		for (const FSParser::TypeParameterNode *type_parameter : p_function->type_parameters) {
			if (type_parameter != nullptr && type_parameter->identifier != nullptr) {
				function_context.scope.shadowing_local_names.push_back(type_parameter->identifier->name);
			}
		}
		// Signature type identifiers (parameters and return type) are parsed with
		// `current_suite` set to the function body, so the analyzer resolves them
		// against the body suite: a top-level body local shadows a signature
		// annotation just like an enclosing-scope name. Add the body's own
		// top-level locals here. Locals declared only in nested blocks are added
		// suite by suite as the collector descends, so they shadow sites inside
		// those blocks but not the signature or outer-block sites.
		collect_own_suite_local_names(p_function->body, function_context.scope.shadowing_local_names);
		render_context = &function_context;
	}
	for (int i = 0; i < p_function->parameters.size(); i++) {
		const FSParser::ParameterNode *parameter = p_function->parameters[i];
		TypeAnnotationCandidate candidate;
		if (find_assignable_type_annotation(p_lines, parameter, "parameter", false, candidate, nullptr, nullptr, render_context) && candidate.matched) {
#ifndef FOUNDRY_SCRIPT_NO_LSP
			if (p_location == nullptr || caret_on_segment(*p_location, candidate.line, candidate.caret_span_start, candidate.caret_span_end)) {
				apply_callsite_parameter_type_annotation(p_class, p_function, parameter, i, p_workspace, p_parser, p_parse_results, candidate, render_context);
			}
#endif // FOUNDRY_SCRIPT_NO_LSP
			r_candidates.push_back(candidate);
		}
	}
	if (p_function->rest_parameter != nullptr) {
		// A vararg tail does not map cleanly to one call-site argument index, so
		// keep rest parameters on the existing declaration-local inference path.
		collect_assignable_candidate(p_lines, p_function->rest_parameter, "parameter", false, r_candidates, nullptr, nullptr, render_context);
	}
	TypeAnnotationCandidate return_candidate;
	if (find_function_return_type_annotation(p_lines, p_function, return_candidate, render_context) && return_candidate.matched) {
		r_candidates.push_back(return_candidate);
	}
	collect_type_annotation_in_suite(p_lines, p_function->body, r_candidates, p_function->body, render_context);
}

#ifndef FOUNDRY_SCRIPT_NO_LSP
// True when `p_descendant` derives (transitively) from the class identified by
// `p_base_fqcn`, by walking its resolved base-class chain. `p_descendant` itself is
// not considered its own subclass.
bool class_derives_from_fqcn(const FSParser::ClassNode *p_descendant, const String &p_base_fqcn) {
	if (p_descendant == nullptr || p_base_fqcn.is_empty()) {
		return false;
	}
	for (const FSParser::ClassNode *base = p_descendant->base_type.class_type; base != nullptr; base = base->base_type.class_type) {
		if (base->fqcn == p_base_fqcn) {
			return true;
		}
	}
	return false;
}

// Collects every class in `p_tree` (including nested classes) that derives from
// `p_base_fqcn` into `r_closure`.
void collect_subclasses_in_tree(const FSParser::ClassNode *p_tree, const String &p_base_fqcn, MemberSubclassClosure &r_closure) {
	if (p_tree == nullptr) {
		return;
	}
	if (class_derives_from_fqcn(p_tree, p_base_fqcn)) {
		r_closure.subclasses.push_back(p_tree);
	}
	for (const FSParser::ClassNode::Member &member : p_tree->members) {
		if (member.type == FSParser::ClassNode::Member::CLASS) {
			collect_subclasses_in_tree(member.m_class, p_base_fqcn, r_closure);
		}
	}
}

// True when `p_lines` textually mentions `extends` together with a token that could
// name the base class (its global `class_name` or its source path). A parse-failed
// script can only hide a subclass of the base if it could spell such an `extends`,
// so this filters out the project's many unrelated broken fixtures while still
// flagging a plausible-but-unanalyzable subclass.
bool source_could_extend_base(const Vector<String> &p_lines, const Vector<String> &p_base_tokens) {
	bool mentions_extends = false;
	for (const String &line : p_lines) {
		if (line.contains("extends")) {
			mentions_extends = true;
			break;
		}
	}
	if (!mentions_extends) {
		return false;
	}
	for (const String &line : p_lines) {
		for (const String &token : p_base_tokens) {
			if (!token.is_empty() && line.contains(token)) {
				return true;
			}
		}
	}
	return false;
}

// Discovers, across the whole project, the set of classes that `extends` `p_class`
// so member container element inference can fold their writers into its union (see
// MemberSubclassClosure). Soundness requires the enumeration to be exhaustive: if
// the class has no stable identity to derive from, `complete` is left false and the
// inference skips. A project script that fails to parse only breaks completeness
// when its text could plausibly extend the base (most broken fixtures are unrelated
// and must not poison every member). `p_class` and its nested classes (already
// scanned in-file) are excluded.
MemberSubclassClosure discover_member_subclasses(
		const FSParser::ClassNode *p_class,
		const Ref<FSWorkspace> &p_workspace,
		const ExtendFSParser *p_parser,
		const FSParseResultProvider *p_parse_results) {
	MemberSubclassClosure closure;
	if (p_class == nullptr || p_workspace.is_null() || p_parse_results == nullptr) {
		closure.complete = false;
		return closure;
	}
	const String base_fqcn = p_class->fqcn;
	if (base_fqcn.is_empty()) {
		// Without a resolved identity the base chain of other classes cannot be
		// matched against this one, so subclasses cannot be proven absent.
		closure.complete = false;
		return closure;
	}

	// Tokens by which another script could name this class in an `extends` clause.
	// The set is intentionally broad so the parse-failure heuristic over-approximates
	// (it may flag an unrelated broken script, only forcing a conservative skip):
	//   - the fully-qualified `class_name` (`extends My.Namespaced.Base`),
	//   - the bare class identifier, which also appears in a same-namespace or
	//     imported short spelling (`extends Base`) and in a nested-class chain
	//     (`extends Outer.Inner`),
	//   - the source file name (`extends "res://.../base.fs"`).
	Vector<String> base_tokens;
	const StringName global_name = p_class->get_global_name();
	if (global_name != StringName()) {
		base_tokens.push_back(String(global_name));
	}
	if (p_class->identifier != nullptr && p_class->identifier->name != StringName()) {
		base_tokens.push_back(String(p_class->identifier->name));
	}
	if (p_parser != nullptr && !p_parser->get_path().is_empty()) {
		base_tokens.push_back(p_parser->get_path().get_file());
	}
	// When the class has no `class_name`, its fqcn is the source path.
	if (base_fqcn.contains("/") || base_fqcn.ends_with(".fs")) {
		base_tokens.push_back(base_fqcn.get_file());
	}

	List<String> paths;
	p_workspace->list_project_script_files(paths);
	for (const String &path : paths) {
		const ExtendFSParser *parser = p_parse_results->get_parse_result(path);
		if (parser == nullptr) {
			// The script is listed in the project but could not be read or parsed at
			// all. Its contents are unknown, so fall back to reading the raw text; a
			// hidden subclass is possible only if even that fails or it could spell an
			// `extends` of this base.
			Error read_error = OK;
			const String contents = FileAccess::get_file_as_string(path, &read_error);
			if (read_error != OK) {
				closure.complete = false;
			} else if (source_could_extend_base(contents.split("\n"), base_tokens)) {
				closure.complete = false;
			}
			continue;
		}
		if (parser->parse_result != OK) {
			// A script that does not parse could hide a subclass only if its text
			// could spell an `extends` of this base.
			if (source_could_extend_base(parser->get_lines(), base_tokens)) {
				closure.complete = false;
			}
			continue;
		}
		const FSParser::ClassNode *tree = parser->get_tree();
		if (tree == nullptr) {
			continue;
		}
		if (tree->fqcn == base_fqcn) {
			continue; // The declaring file itself; its classes are scanned in-file.
		}
		collect_subclasses_in_tree(tree, base_fqcn, closure);
	}
	return closure;
}
#endif // FOUNDRY_SCRIPT_NO_LSP

void collect_type_annotation_in_class(
		const Vector<String> &p_lines,
		const FSParser::ClassNode *p_class,
		const RefactorLocation *p_location,
		Vector<TypeAnnotationCandidate> &r_candidates,
		bool p_allow_member_inference,
		const TypeAnnotationRenderContext *p_render_context
#ifndef FOUNDRY_SCRIPT_NO_LSP
		,
		const Ref<FSWorkspace> &p_workspace,
		const ExtendFSParser *p_parser,
		const FSParseResultProvider *p_parse_results
#endif // FOUNDRY_SCRIPT_NO_LSP
) {
	if (p_class == nullptr) {
		return;
	}
	// Augment the scope with this class's own names (its members, type parameters,
	// and the class name) so a qualified annotation in this class or a nested one
	// is never rooted at a name the analyzer resolves as a member/type parameter.
	TypeAnnotationRenderContext class_context;
	const TypeAnnotationRenderContext *render_context = p_render_context;
	if (p_render_context != nullptr) {
		class_context = *p_render_context;
		add_class_scope_shadow_names(class_context.scope, p_class);
		render_context = &class_context;
	}

	// When member inference is enabled, discover the project-wide subclasses of this
	// class once so each member candidate is proven sound against the open world
	// rather than relying solely on the post-edit verifier (computed lazily, only
	// for the verified migration path).
	const MemberSubclassClosure *member_subclasses = nullptr;
#ifndef FOUNDRY_SCRIPT_NO_LSP
	MemberSubclassClosure subclass_closure;
	if (p_allow_member_inference) {
		subclass_closure = discover_member_subclasses(p_class, p_workspace, p_parser, p_parse_results);
		member_subclasses = &subclass_closure;
	}
#endif // FOUNDRY_SCRIPT_NO_LSP

	for (const FSParser::ClassNode::Member &member : p_class->members) {
		switch (member.type) {
			case FSParser::ClassNode::Member::CONSTANT:
				collect_assignable_candidate(p_lines, member.constant, "constant", true, r_candidates, nullptr, nullptr, render_context);
				break;
			case FSParser::ClassNode::Member::VARIABLE:
				// Member container element inference folds project-wide subclass writers
				// into its union (subclass_closure); it is enabled only for the verified
				// migration path, where the dependency closure is parsed. Otherwise
				// members keep the analyzer's bare container type.
				collect_assignable_candidate(p_lines, member.variable, "variable", true, r_candidates, nullptr,
						p_allow_member_inference ? p_class : nullptr, render_context, member_subclasses);
				break;
			case FSParser::ClassNode::Member::FUNCTION:
				collect_type_annotation_in_function(p_lines, p_class, member.function, p_location, r_candidates, render_context
#ifndef FOUNDRY_SCRIPT_NO_LSP
						,
						p_workspace, p_parser, p_parse_results
#endif // FOUNDRY_SCRIPT_NO_LSP
				);
				break;
			case FSParser::ClassNode::Member::CLASS:
				collect_type_annotation_in_class(p_lines, member.m_class, p_location, r_candidates, p_allow_member_inference, render_context
#ifndef FOUNDRY_SCRIPT_NO_LSP
						,
						p_workspace, p_parser, p_parse_results
#endif // FOUNDRY_SCRIPT_NO_LSP
				);
				break;
			default:
				break;
		}
	}
}

void collect_type_annotation_in_suite(const Vector<String> &p_lines, const FSParser::SuiteNode *p_suite, Vector<TypeAnnotationCandidate> &r_candidates, const FSParser::SuiteNode *p_function_body, const TypeAnnotationRenderContext *p_render_context) {
	if (p_suite == nullptr) {
		return;
	}
	// Augment the inherited scope with this suite's own locals (not nested ones),
	// mirroring `SuiteNode::has_local()` precedence: a site here is shadowed by a
	// name from the current suite or an enclosing one, but not by a local declared
	// in a sibling or deeper block. Nested suites receive this context and add
	// their own locals in turn.
	TypeAnnotationRenderContext suite_context;
	const TypeAnnotationRenderContext *render_context = p_render_context;
	if (p_render_context != nullptr) {
		suite_context = *p_render_context;
		collect_own_suite_local_names(p_suite, suite_context.scope.shadowing_local_names);
		render_context = &suite_context;
	}
	for (const FSParser::Node *statement : p_suite->statements) {
		if (statement == nullptr) {
			continue;
		}
		switch (statement->type) {
			case FSParser::Node::VARIABLE:
				collect_assignable_candidate(p_lines, static_cast<const FSParser::VariableNode *>(statement), "variable", true, r_candidates, p_function_body, nullptr, render_context);
				break;
			case FSParser::Node::CONSTANT:
				collect_assignable_candidate(p_lines, static_cast<const FSParser::ConstantNode *>(statement), "constant", true, r_candidates, nullptr, nullptr, render_context);
				break;
			case FSParser::Node::IF: {
				const FSParser::IfNode *if_node = static_cast<const FSParser::IfNode *>(statement);
				collect_type_annotation_in_suite(p_lines, if_node->true_block, r_candidates, p_function_body, render_context);
				collect_type_annotation_in_suite(p_lines, if_node->false_block, r_candidates, p_function_body, render_context);
			} break;
			case FSParser::Node::FOR: {
				const FSParser::ForNode *for_node = static_cast<const FSParser::ForNode *>(statement);
				collect_type_annotation_in_suite(p_lines, for_node->loop, r_candidates, p_function_body, render_context);
			} break;
			case FSParser::Node::WHILE: {
				const FSParser::WhileNode *while_node = static_cast<const FSParser::WhileNode *>(statement);
				collect_type_annotation_in_suite(p_lines, while_node->loop, r_candidates, p_function_body, render_context);
			} break;
			case FSParser::Node::MATCH: {
				const FSParser::MatchNode *match_node = static_cast<const FSParser::MatchNode *>(statement);
				for (const FSParser::MatchBranchNode *branch : match_node->branches) {
					if (branch != nullptr) {
						collect_type_annotation_in_suite(p_lines, branch->block, r_candidates, p_function_body, render_context);
					}
				}
			} break;
			default:
				break;
		}
	}
}

Vector<TypeAnnotationCandidate> collect_type_annotation_candidates_in_tree(
		const Vector<String> &p_lines,
		const FSParser::ClassNode *p_tree,
		const RefactorLocation *p_location = nullptr,
		bool p_allow_member_inference = false,
		const TypeAnnotationRenderContext *p_render_context = nullptr
#ifndef FOUNDRY_SCRIPT_NO_LSP
		,
		const Ref<FSWorkspace> &p_workspace = Ref<FSWorkspace>(),
		const ExtendFSParser *p_parser = nullptr,
		const FSParseResultProvider *p_parse_results = nullptr
#endif // FOUNDRY_SCRIPT_NO_LSP
) {
	Vector<TypeAnnotationCandidate> candidates;
	collect_type_annotation_in_class(p_lines, p_tree, p_location, candidates, p_allow_member_inference, p_render_context
#ifndef FOUNDRY_SCRIPT_NO_LSP
			,
			p_workspace, p_parser, p_parse_results
#endif // FOUNDRY_SCRIPT_NO_LSP
	);
	return candidates;
}

TypeAnnotationCandidate find_type_annotation_candidate_in_tree(
		const RefactorLocation &p_location,
		const Vector<String> &p_lines,
		const FSParser::ClassNode *p_tree
#ifndef FOUNDRY_SCRIPT_NO_LSP
		,
		const Ref<FSWorkspace> &p_workspace = Ref<FSWorkspace>(),
		const ExtendFSParser *p_parser = nullptr,
		const FSParseResultProvider *p_parse_results = nullptr
#endif // FOUNDRY_SCRIPT_NO_LSP
) {
	// The interactive caret-located refactor applies edits directly without the
	// verification harness, so member container inference is left disabled here.
	const TypeAnnotationRenderContext render_context = make_type_annotation_render_context(p_tree);
	const Vector<TypeAnnotationCandidate> candidates = collect_type_annotation_candidates_in_tree(p_lines, p_tree, &p_location, /* allow_member_inference */ false, &render_context
#ifndef FOUNDRY_SCRIPT_NO_LSP
			,
			p_workspace, p_parser, p_parse_results
#endif // FOUNDRY_SCRIPT_NO_LSP
	);
	for (const TypeAnnotationCandidate &candidate : candidates) {
		const int caret_span_end_line = candidate.caret_span_end_line < 0 ? candidate.line : candidate.caret_span_end_line;
		const bool matched = caret_span_end_line == candidate.line
				? caret_on_segment(p_location, candidate.line, candidate.caret_span_start, candidate.caret_span_end)
				: caret_in_multiline_span(p_location, candidate.line, candidate.caret_span_start, caret_span_end_line, candidate.caret_span_end);
		if (matched) {
			return candidate;
		}
	}

	TypeAnnotationCandidate candidate;
	candidate.disabled_reason = "Place the caret on an untyped declaration with an inferred concrete type.";
	return candidate;
}

ExtractVariableCandidate find_extract_variable_candidate_in_tree(const RefactorLocation &p_location, const Vector<String> &p_lines, const FSParser::ClassNode *p_tree) {
	ExtractVariableCandidate candidate;

	_ALLOW_DISCARD_ find_extract_variable_in_class(p_location, p_lines, p_tree, candidate);
	if (!candidate.matched && candidate.disabled_reason.is_empty()) {
		candidate.disabled_reason = "Select one complete expression that can be safely extracted.";
	}
	return candidate;
}

ExtractMethodCandidate find_extract_method_candidate_in_tree(
		const RefactorLocation &p_location,
		const Vector<String> &p_lines,
		bool p_source_has_final_newline,
		const FSParser::ClassNode *p_tree,
		const String &p_requested_name) {
	ExtractMethodCandidate candidate;

	_ALLOW_DISCARD_ find_extract_method_in_class(
			p_location,
			p_lines,
			p_source_has_final_newline,
			p_tree,
			p_requested_name,
			candidate);
	if (!candidate.matched && candidate.disabled_reason.is_empty()) {
		candidate.disabled_reason = "Select complete statements inside one function body.";
	}
	return candidate;
}

ExtractVariableCandidate find_extract_variable_candidate_uncached(
		const RefactorContext &p_context,
		const RefactorLocation &p_location,
		const Vector<String> &p_lines,
		const FSParseResultProvider *p_parse_results) {
#ifndef FOUNDRY_SCRIPT_NO_LSP
	if (p_parse_results != nullptr) {
		const ExtendFSParser *lsp_parser = p_parse_results->get_parse_result(p_context.path);
		if (lsp_parser != nullptr && source_lines_match(lsp_parser->get_lines(), p_lines)) {
			if (lsp_parser->parse_result != OK) {
				ExtractVariableCandidate candidate;
				candidate.disabled_reason = "Cannot analyze this script.";
				return candidate;
			}
			return find_extract_variable_candidate_in_tree(p_location, p_lines, lsp_parser->get_tree());
		}
	}
#endif // FOUNDRY_SCRIPT_NO_LSP

	FSParser parser;
	Error err = parser.parse(p_context.source, p_context.path, false);
	if (err == OK) {
		FSAnalyzer analyzer(&parser);
		err = analyzer.analyze();
	}
	if (err != OK) {
		ExtractVariableCandidate candidate;
		candidate.disabled_reason = "Cannot analyze this script.";
		return candidate;
	}

	return find_extract_variable_candidate_in_tree(p_location, p_lines, parser.get_tree());
}

ExtractMethodCandidate find_extract_method_candidate_uncached(
		const RefactorContext &p_context,
		const RefactorLocation &p_location,
		const Vector<String> &p_lines,
		const FSParseResultProvider *p_parse_results,
		const String &p_requested_name) {
	const bool source_has_final_newline = p_context.source.ends_with("\n");
#ifndef FOUNDRY_SCRIPT_NO_LSP
	if (p_parse_results != nullptr) {
		const ExtendFSParser *lsp_parser = p_parse_results->get_parse_result(p_context.path);
		if (lsp_parser != nullptr && source_lines_match(lsp_parser->get_lines(), p_lines)) {
			if (lsp_parser->parse_result != OK) {
				ExtractMethodCandidate candidate;
				candidate.disabled_reason = "Cannot analyze this script.";
				return candidate;
			}
			return find_extract_method_candidate_in_tree(
					p_location,
					p_lines,
					source_has_final_newline,
					lsp_parser->get_tree(),
					p_requested_name);
		}
	}
#endif // FOUNDRY_SCRIPT_NO_LSP

	FSParser parser;
	Error err = parser.parse(p_context.source, p_context.path, false);
	if (err == OK) {
		FSAnalyzer analyzer(&parser);
		err = analyzer.analyze();
	}
	if (err != OK) {
		ExtractMethodCandidate candidate;
		candidate.disabled_reason = "Cannot analyze this script.";
		return candidate;
	}

	return find_extract_method_candidate_in_tree(
			p_location,
			p_lines,
			source_has_final_newline,
			parser.get_tree(),
			p_requested_name);
}

InlineVariableCandidate find_inline_variable_candidate_uncached(
		const RefactorContext &p_context,
		const RefactorLocation &p_location,
		const Vector<String> &p_lines,
		const FSParseResultProvider *p_parse_results) {
#ifndef FOUNDRY_SCRIPT_NO_LSP
	if (p_parse_results != nullptr) {
		const ExtendFSParser *lsp_parser = p_parse_results->get_parse_result(p_context.path);
		if (lsp_parser != nullptr && source_lines_match(lsp_parser->get_lines(), p_lines)) {
			if (lsp_parser->parse_result != OK) {
				InlineVariableCandidate candidate;
				candidate.disabled_reason = "Cannot analyze this script.";
				return candidate;
			}
			return find_inline_variable_candidate_in_tree(p_location, p_lines, lsp_parser->get_tree());
		}
	}
#endif // FOUNDRY_SCRIPT_NO_LSP

	FSParser parser;
	Error err = parser.parse(p_context.source, p_context.path, false);
	if (err == OK) {
		FSAnalyzer analyzer(&parser);
		err = analyzer.analyze();
	}
	if (err != OK) {
		InlineVariableCandidate candidate;
		candidate.disabled_reason = "Cannot analyze this script.";
		return candidate;
	}

	return find_inline_variable_candidate_in_tree(p_location, p_lines, parser.get_tree());
}

ExtractVariableCandidate find_extract_variable_candidate(
		const RefactorContext &p_context,
		const RefactorLocation &p_location,
		const FSParseResultProvider *p_parse_results) {
	ExtractVariableCandidate candidate;
	if (get_cached_extract_variable_candidate(p_context, p_location, candidate)) {
		return candidate;
	}

	if (!p_location.has_selection()) {
		candidate.disabled_reason = "Select one expression to extract.";
		cache_extract_variable_candidate(p_context, p_location, candidate);
		return candidate;
	}
	if (!is_location_ordered(p_location)) {
		candidate.disabled_reason = "Select one expression to extract.";
		cache_extract_variable_candidate(p_context, p_location, candidate);
		return candidate;
	}
	if (p_location.start_line != p_location.end_line) {
		candidate.disabled_reason = "Extract variable currently supports single-line expressions.";
		cache_extract_variable_candidate(p_context, p_location, candidate);
		return candidate;
	}

	const Vector<String> lines = p_context.source.split("\n");
	candidate = find_extract_variable_candidate_uncached(p_context, p_location, lines, p_parse_results);
	cache_extract_variable_candidate(p_context, p_location, candidate);
	return candidate;
}

ExtractMethodCandidate find_extract_method_candidate(
		const RefactorContext &p_context,
		const RefactorLocation &p_location,
		const FSParseResultProvider *p_parse_results) {
	ExtractMethodCandidate candidate;
	if (get_cached_extract_method_candidate(p_context, p_location, candidate)) {
		return candidate;
	}

	if (!p_location.has_selection()) {
		candidate.disabled_reason = "Select complete statements to extract.";
		cache_extract_method_candidate(p_context, p_location, candidate);
		return candidate;
	}
	if (!is_location_ordered(p_location)) {
		candidate.disabled_reason = "Select complete statements to extract.";
		cache_extract_method_candidate(p_context, p_location, candidate);
		return candidate;
	}

	const Vector<String> lines = p_context.source.split("\n");
	candidate = find_extract_method_candidate_uncached(p_context, p_location, lines, p_parse_results, String());
	cache_extract_method_candidate(p_context, p_location, candidate);
	return candidate;
}

RefactorResult prepare_extract_variable(
		const RefactorContext &p_context,
		const RefactorLocation &p_location,
		const FSParseResultProvider *p_parse_results) {
	RefactorResult result;
	const ExtractVariableCandidate candidate = find_extract_variable_candidate(p_context, p_location, p_parse_results);
	if (!candidate.enabled) {
		result.ok = false;
		result.error_message = candidate.disabled_reason;
		return result;
	}

	result.ok = true;
	result.suggested_name = candidate.suggested_name;
	result.rename_anchor_line = candidate.name_line;
	result.rename_anchor_column = candidate.name_column;
	result.edits.push_back(candidate.declaration_edit);
	result.edits.push_back(candidate.replacement_edit);
	return result;
}

RefactorResult prepare_extract_method(
		const RefactorContext &p_context,
		const RefactorLocation &p_location,
		const RefactorParams &p_params,
		const FSParseResultProvider *p_parse_results) {
	RefactorResult result;

	ExtractMethodCandidate candidate;
	if (p_params.new_name.is_empty()) {
		candidate = find_extract_method_candidate(p_context, p_location, p_parse_results);
	} else if (!p_location.has_selection() || !is_location_ordered(p_location)) {
		candidate.disabled_reason = "Select complete statements to extract.";
	} else {
		// Requested names affect the generated edits, so this path intentionally
		// bypasses the default-name candidate cache.
		const Vector<String> lines = p_context.source.split("\n");
		candidate = find_extract_method_candidate_uncached(p_context, p_location, lines, p_parse_results, p_params.new_name);
	}

	if (!candidate.enabled) {
		result.ok = false;
		result.error_message = candidate.disabled_reason;
		return result;
	}

	result.ok = true;
	result.suggested_name = candidate.suggested_name;
	result.rename_anchor_line = candidate.name_line;
	result.rename_anchor_column = candidate.name_column;
	result.extract_method_member_names = candidate.member_names;
	result.edits.push_back(candidate.replacement_edit);
	result.edits.push_back(candidate.method_edit);
	return result;
}

InlineVariableCandidate find_inline_variable_candidate(
		const RefactorContext &p_context,
		const RefactorLocation &p_location,
		const FSParseResultProvider *p_parse_results) {
	InlineVariableCandidate candidate;
	if (get_cached_inline_variable_candidate(p_context, p_location, candidate)) {
		return candidate;
	}

	if (p_location.has_selection()) {
		candidate.disabled_reason = "Place the caret on a local variable declaration or use.";
		cache_inline_variable_candidate(p_context, p_location, candidate);
		return candidate;
	}

	const Vector<String> lines = p_context.source.split("\n");
	candidate = find_inline_variable_candidate_uncached(p_context, p_location, lines, p_parse_results);
	cache_inline_variable_candidate(p_context, p_location, candidate);
	return candidate;
}

RefactorResult prepare_inline_variable(
		const RefactorContext &p_context,
		const RefactorLocation &p_location,
		const FSParseResultProvider *p_parse_results) {
	RefactorResult result;
	const InlineVariableCandidate candidate = find_inline_variable_candidate(p_context, p_location, p_parse_results);
	if (!candidate.enabled) {
		result.ok = false;
		result.error_message = candidate.disabled_reason;
		return result;
	}

	result.ok = true;
	result.edits.push_back(candidate.declaration_edit);
	for (const RefactorTextEdit &edit : candidate.replacement_edits) {
		result.edits.push_back(edit);
	}
	return result;
}

TypeAnnotationCandidate find_type_annotation_candidate_uncached(
		const RefactorContext &p_context,
		const RefactorLocation &p_location,
		const Vector<String> &p_lines,
		const FSParseResultProvider *p_parse_results) {
#ifndef FOUNDRY_SCRIPT_NO_LSP
	if (p_parse_results != nullptr) {
		const ExtendFSParser *lsp_parser = p_parse_results->get_parse_result(p_context.path);
		if (lsp_parser != nullptr && source_lines_match(lsp_parser->get_lines(), p_lines)) {
			if (lsp_parser->parse_result != OK) {
				TypeAnnotationCandidate candidate;
				candidate.disabled_reason = "Cannot analyze this script.";
				return candidate;
			}
			FSLanguageProtocol *protocol = FSLanguageProtocol::get_singleton();
			Ref<FSWorkspace> workspace = protocol ? protocol->get_workspace() : Ref<FSWorkspace>();
			return find_type_annotation_candidate_in_tree(p_location, p_lines, lsp_parser->get_tree(), workspace, lsp_parser, p_parse_results);
		}
	}
#endif // FOUNDRY_SCRIPT_NO_LSP

	FSParser parser;
	Error err = parser.parse(p_context.source, p_context.path, false);
	if (err == OK) {
		FSAnalyzer analyzer(&parser);
		err = analyzer.analyze();
	}
	if (err != OK) {
		TypeAnnotationCandidate candidate;
		candidate.disabled_reason = "Cannot analyze this script.";
		return candidate;
	}

	return find_type_annotation_candidate_in_tree(p_location, p_lines, parser.get_tree());
}

TypeAnnotationCandidate find_type_annotation_candidate(
		const RefactorContext &p_context,
		const RefactorLocation &p_location,
		const FSParseResultProvider *p_parse_results) {
	TypeAnnotationCandidate candidate;
	if (get_cached_type_annotation_candidate(p_context, p_location, candidate)) {
		return candidate;
	}

	if (p_location.has_selection()) {
		candidate.disabled_reason = "Place the caret on an untyped declaration.";
		cache_type_annotation_candidate(p_context, p_location, candidate);
		return candidate;
	}

	const Vector<String> lines = p_context.source.split("\n");
	candidate = find_type_annotation_candidate_uncached(p_context, p_location, lines, p_parse_results);
	cache_type_annotation_candidate(p_context, p_location, candidate);
	return candidate;
}

// Deepest class whose [start_line, end_line] span contains the caret line.
// Node line numbers are 1-based; RefactorLocation lines are 0-based.
const FSParser::ClassNode *find_enclosing_class(const FSParser::ClassNode *p_class, int p_caret_line_0based) {
	if (p_class == nullptr) {
		return nullptr;
	}
	const int line_1based = p_caret_line_0based + 1;
	const FSParser::ClassNode *best = p_class;
	for (const FSParser::ClassNode::Member &member : p_class->members) {
		if (member.type != FSParser::ClassNode::Member::CLASS) {
			continue;
		}
		const FSParser::ClassNode *inner = member.m_class;
		if (inner == nullptr) {
			continue;
		}
		if (line_1based >= inner->start_line && line_1based <= inner->end_line) {
			const FSParser::ClassNode *deeper = find_enclosing_class(inner, p_caret_line_0based);
			if (deeper != nullptr) {
				best = deeper;
			}
		}
	}
	return best;
}

#ifndef FOUNDRY_SCRIPT_NO_LSP
// Depth-first search for the class node with the given fully-qualified name within a
// parse tree, used to re-resolve a cross-file base against a freshly parsed file so the
// returned node and its source lines come from the same parse.
const FSParser::ClassNode *find_class_node_by_fqcn(const FSParser::ClassNode *p_root, const String &p_fqcn) {
	if (p_root == nullptr) {
		return nullptr;
	}
	if (p_root->fqcn == p_fqcn) {
		return p_root;
	}
	for (const FSParser::ClassNode::Member &member : p_root->members) {
		if (member.type == FSParser::ClassNode::Member::CLASS && member.m_class != nullptr) {
			const FSParser::ClassNode *found = find_class_node_by_fqcn(member.m_class, p_fqcn);
			if (found != nullptr) {
				return found;
			}
		}
	}
	return nullptr;
}
#endif // FOUNDRY_SCRIPT_NO_LSP

// Next FoundryScript class up the inheritance chain, or nullptr when the base is not a
// resolved FoundryScript class reachable in-tree (native bases / unresolved). A base resolved
// as a separate script (SCRIPT kind, or a CLASS in another file) is followed through the
// parse-result provider when one is available.
//
// Also reports the path and source lines of the file that declares the base, which the
// caller needs to recover verbatim parameter-default text whose source spans index that
// file. The returned node and the reported lines are always taken from the same parse:
// an inner base class shares the current file's node and lines, while a cross-file base
// is re-resolved from a single fresh parse (node matched by fully-qualified name) so its
// default spans align with the lines they index. When a cross-file base cannot be
// re-parsed (no provider, e.g. no-LSP builds), the node is still followed but the lines
// are reported as null so the renderer omits any default rather than slicing the wrong
// file.
const FSParser::ClassNode *resolve_base_class(
		const FSParser::ClassNode *p_class,
		const FSParseResultProvider *p_parse_results,
		const String &p_current_path,
		const Vector<String> *p_current_lines,
		String &r_base_path,
		const Vector<String> **r_base_lines) {
	if (p_class == nullptr) {
		return nullptr;
	}
	const FSParser::DataType &base = p_class->base_type;
	const bool is_class = base.kind == FSParser::DataType::CLASS && base.class_type != nullptr;
	const bool is_script = base.kind == FSParser::DataType::SCRIPT;
	if (!is_class && !is_script) {
		return nullptr;
	}

	// A same-file inner base class lives in the current parse tree, whose coordinates
	// already match the current lines; reuse both directly.
	if (is_class && (base.script_path.is_empty() || base.script_path == p_current_path)) {
		r_base_path = p_current_path;
		*r_base_lines = p_current_lines;
		return base.class_type;
	}

#ifndef FOUNDRY_SCRIPT_NO_LSP
	if (p_parse_results != nullptr && !base.script_path.is_empty()) {
		const ExtendFSParser *base_parser = p_parse_results->get_parse_result(base.script_path);
		if (base_parser != nullptr) {
			const FSParser::ClassNode *resolved = base_parser->get_tree();
			if (is_class && resolved != nullptr && base.class_type->fqcn != resolved->fqcn) {
				const FSParser::ClassNode *matched = find_class_node_by_fqcn(resolved, base.class_type->fqcn);
				if (matched != nullptr) {
					resolved = matched;
				}
			}
			if (resolved != nullptr) {
				r_base_path = base.script_path;
				*r_base_lines = &base_parser->get_lines();
				return resolved;
			}
		}
	}
#else
	(void)p_parse_results;
#endif // FOUNDRY_SCRIPT_NO_LSP

	// Cross-file base without a usable provider parse: follow the resolved node so
	// detection still finds the owed methods, but report no lines so any default value
	// is omitted instead of recovered from the wrong file.
	if (is_class) {
		r_base_path = base.script_path;
		*r_base_lines = nullptr;
		return base.class_type;
	}
	return nullptr;
}

// Depth-first pointer search for a class node within a parse tree. Unlike
// find_class_node_by_fqcn this compares node identity and is available in every build,
// so it can detect a same-file trait without depending on the LSP-only helper.
bool tree_contains_class(const FSParser::ClassNode *p_root, const FSParser::ClassNode *p_class) {
	if (p_root == nullptr) {
		return false;
	}
	if (p_root == p_class) {
		return true;
	}
	for (const FSParser::ClassNode::Member &member : p_root->members) {
		if (member.type == FSParser::ClassNode::Member::CLASS && member.m_class != nullptr &&
				tree_contains_class(member.m_class, p_class)) {
			return true;
		}
	}
	return false;
}

// Resolve the source lines that declare p_trait so a rendered stub can recover parameter
// default text. A same-file trait shares the target file's lines. A cross-file trait's
// lines come from its own parse, recovered through the parse-result provider; when that
// is unavailable (no-LSP builds, or a path that cannot be re-resolved) the lines are
// unknown, so defaults are omitted rather than sliced out of the wrong file — the same
// graceful degradation used for a cross-file abstract base.
const Vector<String> *resolve_trait_declaring_lines(
		const FSParser::ClassNode *p_trait,
		const FSParser::ClassNode *p_root,
		const Vector<String> &p_root_lines,
		const FSParseResultProvider *p_parse_results) {
	if (tree_contains_class(p_root, p_trait)) {
		return &p_root_lines;
	}
#ifndef FOUNDRY_SCRIPT_NO_LSP
	if (p_parse_results != nullptr) {
		// An inline trait declared inside another file has the fqcn "<path>::Trait", so its
		// path is the leading segment. A root trait declared with `trait_name` has the bare
		// global name as its fqcn, so resolve that to a path through the script server.
		String trait_path = p_trait->fqcn.get_slice("::", 0);
		if (!trait_path.begins_with("res://")) {
			const StringName global_name = p_trait->get_global_name();
			trait_path = global_name != StringName() ? ScriptServer::get_global_class_path(global_name) : String();
		}
		if (trait_path.begins_with("res://")) {
			const ExtendFSParser *trait_parser = p_parse_results->get_parse_result(trait_path);
			if (trait_parser != nullptr && find_class_node_by_fqcn(trait_parser->get_tree(), p_trait->fqcn) != nullptr) {
				return &trait_parser->get_lines();
			}
		}
	}
#else
	(void)p_parse_results;
#endif // FOUNDRY_SCRIPT_NO_LSP
	return nullptr;
}

HashMap<StringName, FSParser::DataType> trait_type_argument_substitution(
		const FSParser::ClassNode *p_class,
		FSParser::ClassNode *p_trait) {
	HashMap<StringName, FSParser::DataType> bindings;
	if (p_class == nullptr || p_trait == nullptr || p_trait->type_parameters.is_empty()) {
		return bindings;
	}

	for (const FSParser::ClassNode::TraitUse &trait_use : p_class->used_traits) {
		FSParser::ClassNode *used_trait = trait_use.resolved_trait;
		if (used_trait == nullptr) {
			continue;
		}
		if (used_trait == p_trait) {
			const int count = MIN(p_trait->type_parameters.size(), trait_use.resolved_type_arguments.size());
			for (int i = 0; i < count; i++) {
				const FSParser::TypeParameterNode *type_parameter = p_trait->type_parameters[i];
				if (type_parameter != nullptr && type_parameter->identifier != nullptr) {
					bindings.insert(type_parameter->identifier->name, trait_use.resolved_type_arguments[i]);
				}
			}
			return bindings;
		}
		if (used_trait->resolved_traits.has(p_trait)) {
			const HashMap<StringName, FSParser::DataType> inner = trait_type_argument_substitution(used_trait, p_trait);
			if (inner.is_empty()) {
				continue;
			}
			const HashMap<StringName, FSParser::DataType> outer = trait_type_argument_substitution(p_class, used_trait);
			for (const KeyValue<StringName, FSParser::DataType> &binding : inner) {
				bindings.insert(binding.key, FSParser::DataType::substitute(binding.value, outer));
			}
			return bindings;
		}
	}
	return bindings;
}

FSParser::DataType specialize_trait_requirement_type(
		const FSParser::ClassNode *p_target,
		FSParser::ClassNode *p_trait) {
	FSParser::DataType specialized_trait;
	if (p_trait == nullptr) {
		return specialized_trait;
	}
	specialized_trait = p_trait->get_datatype();
	const HashMap<StringName, FSParser::DataType> bindings = trait_type_argument_substitution(p_target, p_trait);
	if (bindings.is_empty()) {
		return specialized_trait;
	}
	for (const FSParser::TypeParameterNode *type_parameter : p_trait->type_parameters) {
		FSParser::DataType type_argument;
		if (type_parameter != nullptr && type_parameter->identifier != nullptr) {
			const FSParser::DataType *binding = bindings.getptr(type_parameter->identifier->name);
			if (binding != nullptr) {
				type_argument = *binding;
			}
		}
		specialized_trait.type_arguments.push_back(type_argument);
	}
	return specialized_trait;
}

// Collect the abstract methods required by the traits the target class uses but does
// not implement, mirroring the analyzer's `validate_trait_requirements`. `resolved_traits`
// is the flattened set of directly and transitively used traits, so the same `decided`
// set the base-chain walk built lets a concrete implementation anywhere in the FoundryScript
// class hierarchy (or an already-owed base abstract) satisfy a trait requirement; a
// method provided by the native base (e.g. RefCounted) satisfies it too.
void collect_owed_trait_abstract_methods(
		const FSParser::ClassNode *p_target,
		const FSParser::ClassNode *p_root,
		const Vector<String> &p_target_lines,
		const StringName &p_native_base,
		HashSet<StringName> &r_decided,
		Vector<OwedAbstractMethod> &r_owed,
		bool *r_depends_on_external_declarations,
		const FSParseResultProvider *p_parse_results) {
	for (FSParser::ClassNode *trait : p_target->resolved_traits) {
		if (trait == nullptr) {
			continue;
		}
		if (!tree_contains_class(p_root, trait) && r_depends_on_external_declarations != nullptr) {
			*r_depends_on_external_declarations = true;
		}
		const Vector<String> *trait_lines = resolve_trait_declaring_lines(trait, p_root, p_target_lines, p_parse_results);
		for (const FSParser::ClassNode::Member &member : trait->members) {
			if (member.type != FSParser::ClassNode::Member::FUNCTION) {
				continue;
			}
			const FSParser::FunctionNode *function = member.function;
			if (function == nullptr || function->identifier == nullptr || !function->is_abstract) {
				continue;
			}
			const StringName name = function->identifier->name;
			// A name already decided by the base chain is implemented (skip) or already
			// owed there (dedup); a name decided by an earlier trait dedups too, matching
			// the analyzer reporting one missing-implementation diagnostic per name.
			if (r_decided.has(name)) {
				continue;
			}
			// The native base can satisfy a trait requirement (e.g. a trait requiring
			// get_class() applied to a RefCounted), matching find_trait_implementation's
			// final ClassDB lookup. Mark it decided so it is not offered as owed.
			if (p_native_base != StringName() && ClassDB::get_method_info(p_native_base, name, nullptr)) {
				r_decided.insert(name);
				continue;
			}
			r_decided.insert(name);
			OwedAbstractMethod owed;
			owed.function = function;
			owed.declaring_lines = trait_lines;
			owed.declaring_class = trait;
			owed.specialized_base = specialize_trait_requirement_type(p_target, trait);
			r_owed.push_back(owed);
		}
	}
}

FSParser::DataType specialize_override_parent_type(
		const FSParser::ClassNode *p_current_class,
		const FSParser::DataType &p_current_specialized_type);

FSParser::DataType align_specialized_override_type_to_class(
		const FSParser::DataType &p_type,
		const FSParser::ClassNode *p_class);

// Collect abstract methods the target class still owes, most-derived-first.
// Walks {target, base, base-of-base, ...}; for each method name the FIRST
// declaration encountered (the most-derived) decides its fate: if that
// declaration is abstract and lives in an ancestor (not the target), it is owed.
// After the base chain, the abstract methods required by used traits are added.
void collect_owed_abstract_methods(
		const FSParser::ClassNode *p_target,
		const FSParser::ClassNode *p_root,
		const String &p_target_path,
		const Vector<String> &p_target_lines,
		Vector<OwedAbstractMethod> &r_owed,
		bool *r_depends_on_external_declarations,
		const FSParseResultProvider *p_parse_results) {
	HashSet<StringName> decided;
	// Detection runs even when the analyzer reported errors, so the inheritance chain
	// may be malformed or self-referential; track visited classes to avoid looping.
	HashSet<const FSParser::ClassNode *> visited;
	const FSParser::ClassNode *current = p_target;
	FSParser::DataType current_specialized_type;
	String current_path = p_target_path;
	const Vector<String> *current_lines = &p_target_lines;
	bool is_target = true;
	// The native class the FoundryScript chain ultimately extends (RefCounted, Node, ...). A
	// trait requirement can be satisfied by a method on this native base, so record it for
	// the trait pass below.
	StringName native_base;
	while (current != nullptr) {
		if (visited.has(current)) {
			break;
		}
		visited.insert(current);
		if (current->base_type.kind == FSParser::DataType::NATIVE) {
			native_base = current->base_type.native_type;
		}
		for (const FSParser::ClassNode::Member &member : current->members) {
			if (member.type != FSParser::ClassNode::Member::FUNCTION) {
				continue;
			}
			const FSParser::FunctionNode *function = member.function;
			if (function == nullptr || function->identifier == nullptr) {
				continue;
			}
			const StringName name = function->identifier->name;
			if (decided.has(name)) {
				// A more-derived declaration already decided this name.
				continue;
			}
			decided.insert(name);
			if (function->is_abstract && !is_target) {
				OwedAbstractMethod owed;
				owed.function = function;
				owed.declaring_lines = current_lines;
				owed.declaring_class = current;
				owed.specialized_base = current_specialized_type;
				r_owed.push_back(owed);
			}
		}
		String base_path;
		const Vector<String> *base_lines = nullptr;
		FSParser::DataType specialized_base = specialize_override_parent_type(current, current_specialized_type);
		const FSParser::ClassNode *base = resolve_base_class(current, p_parse_results, current_path, current_lines, base_path, &base_lines);
		if (!base_path.is_empty() && base_path != current_path && r_depends_on_external_declarations != nullptr) {
			*r_depends_on_external_declarations = true;
		}
		specialized_base = align_specialized_override_type_to_class(specialized_base, base);
		current = base;
		current_specialized_type = specialized_base;
		current_path = base_path;
		current_lines = base_lines;
		is_target = false;
	}

	collect_owed_trait_abstract_methods(p_target, p_root, p_target_lines, native_base, decided, r_owed, r_depends_on_external_declarations, p_parse_results);
}

// Returns the FoundryScript literal default for a return type, or false when the type
// has no clean literal (objects, custom classes, other builtins) so the caller emits
// `pass` instead of a `return`.
bool default_return_literal(const FSParser::DataType &p_type, String &r_literal) {
	if (p_type.kind != FSParser::DataType::BUILTIN) {
		// NATIVE / SCRIPT / CLASS / ENUM have no unambiguous literal default.
		return false;
	}
	switch (p_type.builtin_type) {
		case Variant::BOOL:
			r_literal = "false";
			return true;
		case Variant::INT:
			r_literal = "0";
			return true;
		case Variant::FLOAT:
			r_literal = "0.0";
			return true;
		case Variant::STRING:
			r_literal = "\"\"";
			return true;
		case Variant::STRING_NAME:
			r_literal = "&\"\"";
			return true;
		case Variant::ARRAY:
			r_literal = "[]";
			return true;
		case Variant::DICTIONARY:
			r_literal = "{}";
			return true;
		default:
			// Other builtins (Vector2, Color, ...) have no bare literal -> emit pass.
			return false;
	}
}

String render_function_signature(
		const FSParser::FunctionNode *p_function,
		const Vector<String> &p_lines,
		const String &p_class_indent,
		const Vector<FSParser::DataType> *p_parameter_types = nullptr,
		const FSParser::DataType *p_return_type_override = nullptr,
		const Vector<FSParser::DataType> *p_type_parameter_bounds = nullptr,
		const FSParser::DataType *p_rest_parameter_type = nullptr) {
	const String name = String(p_function->identifier->name);

	// An abstract method on a class cannot be static, but a trait may declare an
	// `abstract static func`, and the implementation's static flag must match it. The
	// modifiers precede `func` in declaration order: `static`, then `async`.
	String signature = p_class_indent;
	if (p_function->is_static) {
		signature += "static ";
	}
	if (p_function->is_declared_async) {
		signature += "async ";
	}
	signature += "func " + name;

	// Render the generic type-parameter list (`[T, U: Bound]`) so a generic abstract method's
	// declaration is reproduced. Each parameter's optional upper bound is rendered from its eagerly
	// resolved datatype.
	if (!p_function->type_parameters.is_empty()) {
		signature += "[";
		bool first_type_parameter = true;
		for (int i = 0; i < p_function->type_parameters.size(); i++) {
			const FSParser::TypeParameterNode *type_parameter = p_function->type_parameters[i];
			if (type_parameter == nullptr || type_parameter->identifier == nullptr) {
				continue;
			}
			if (!first_type_parameter) {
				signature += ", ";
			}
			first_type_parameter = false;
			signature += String(type_parameter->identifier->name);

			if (type_parameter->bound != nullptr) {
				String rendered_bound;
				const bool has_bound_override = p_type_parameter_bounds != nullptr && i < p_type_parameter_bounds->size();
				if (has_bound_override) {
					FSRefactorTypes::render_annotatable_type((*p_type_parameter_bounds)[i], rendered_bound);
				} else if (!FSRefactorTypes::render_annotatable_type(type_parameter->resolved_bound, rendered_bound)) {
					// Prefer the eagerly-resolved (non-meta) bound. Otherwise fall back to the bound
					// TypeNode's own datatype, which is populated even when the eager pass has not run in
					// this analysis context — but in type position it is a metatype handle (especially for
					// a user-class bound like `T: MyClass`), so strip the meta flag to render the instance
					// type rather than have render_annotatable_type reject it and silently drop the bound.
					FSParser::DataType bound_type = type_parameter->bound->get_datatype();
					bound_type.is_meta_type = false;
					FSRefactorTypes::render_annotatable_type(bound_type, rendered_bound);
				}
				if (!rendered_bound.is_empty()) {
					signature += ": " + rendered_bound;
				}
			}
		}
		signature += "]";
	}

	signature += "(";

	bool first_parameter = true;
	for (int i = 0; i < p_function->parameters.size(); i++) {
		if (!first_parameter) {
			signature += ", ";
		}
		first_parameter = false;
		const FSParser::ParameterNode *parameter = p_function->parameters[i];
		signature += String(parameter->identifier->name);

		FSParser::DataType parameter_type = parameter->get_datatype();
		if (p_parameter_types != nullptr && i < p_parameter_types->size()) {
			parameter_type = (*p_parameter_types)[i];
		}
		String rendered_type;
		if (FSRefactorTypes::render_annotatable_type(parameter_type, rendered_type)) {
			signature += ": " + rendered_type;
		}

		if (parameter->initializer != nullptr) {
			// Reproduce the base default verbatim from the source span when it is
			// recoverable; otherwise omit it so the stub still compiles.
			String default_text;
			if (get_multi_line_node_text(p_lines, parameter->initializer, default_text)) {
				signature += " = " + default_text;
			}
		}
	}

	// A rest (vararg) parameter is rendered last as `...name` with its optional type.
	// The parser forbids a default value on the rest parameter, so none is emitted.
	if (p_function->rest_parameter != nullptr && p_function->rest_parameter->identifier != nullptr) {
		if (!first_parameter) {
			signature += ", ";
		}
		signature += "..." + String(p_function->rest_parameter->identifier->name);
		FSParser::DataType rest_parameter_type = p_function->rest_parameter->get_datatype();
		if (p_rest_parameter_type != nullptr) {
			rest_parameter_type = *p_rest_parameter_type;
		}
		String rendered_rest_type;
		if (FSRefactorTypes::render_annotatable_type(rest_parameter_type, rendered_rest_type)) {
			signature += ": " + rendered_rest_type;
		}
	}
	signature += ")";

	const FSParser::DataType return_type = p_return_type_override != nullptr ? *p_return_type_override : p_function->get_datatype();
	// A void return type is set but stringifies as a NIL builtin, which
	// render_annotatable_type rejects; treat it as an explicit `-> void` with no
	// return statement.
	const bool is_void = return_type.is_set() && !return_type.is_variant() &&
			return_type.kind == FSParser::DataType::BUILTIN &&
			return_type.builtin_type == Variant::NIL;
	String rendered_return;
	const bool has_typed_return = !is_void && FSRefactorTypes::render_annotatable_type(return_type, rendered_return);

	String result = signature;
	if (is_void) {
		result += " -> void";
	} else if (has_typed_return) {
		result += " -> " + rendered_return;
	}
	return result;
}

// Renders a concrete stub for an inherited abstract method: a faithful signature
// (preserving `static`, parameter names, annotated parameter/return types and base
// default values where recoverable) plus a body that reports the missing
// implementation and either returns a literal default or falls through to `pass`.
String render_abstract_stub(
		const FSParser::FunctionNode *p_function,
		const Vector<String> &p_lines,
		const String &p_class_indent,
		const Vector<FSParser::DataType> *p_parameter_types = nullptr,
		const FSParser::DataType *p_return_type_override = nullptr,
		const Vector<FSParser::DataType> *p_type_parameter_bounds = nullptr,
		const FSParser::DataType *p_rest_parameter_type = nullptr) {
	const String body_indent = p_class_indent + "\t";
	const String name = String(p_function->identifier->name);
	const FSParser::DataType return_type = p_return_type_override != nullptr ? *p_return_type_override : p_function->get_datatype();
	// A void return type is set but stringifies as a NIL builtin, which
	// render_annotatable_type rejects; treat it as an explicit `-> void` with no
	// return statement.
	const bool is_void = return_type.is_set() && !return_type.is_variant() &&
			return_type.kind == FSParser::DataType::BUILTIN &&
			return_type.builtin_type == Variant::NIL;
	String rendered_return;
	const bool has_typed_return = !is_void && FSRefactorTypes::render_annotatable_type(return_type, rendered_return);

	String result = render_function_signature(
			p_function,
			p_lines,
			p_class_indent,
			p_parameter_types,
			&return_type,
			p_type_parameter_bounds,
			p_rest_parameter_type);
	result += ":\n";
	result += body_indent + "push_error(\"Not implemented: " + name + "\")\n";

	if (has_typed_return) {
		String literal;
		if (default_return_literal(return_type, literal)) {
			result += body_indent + "return " + literal + "\n";
		} else {
			// No clean literal: leave a `pass` so the stub parses, intentionally
			// surfacing a strict "not all paths return a value" error for the author.
			result += body_indent + "pass\n";
		}
	}
	return result;
}

String render_super_call_body(const FSParser::FunctionNode *p_function, const String &p_class_indent) {
	const String body_indent = p_class_indent + "\t";
	const String name = String(p_function->identifier->name);
	String call = "super." + name + "(";
	bool first_argument = true;
	for (int i = 0; i < p_function->parameters.size(); i++) {
		const FSParser::ParameterNode *parameter = p_function->parameters[i];
		if (parameter == nullptr || parameter->identifier == nullptr) {
			continue;
		}
		if (!first_argument) {
			call += ", ";
		}
		first_argument = false;
		call += String(parameter->identifier->name);
	}
	call += ")";
	const String expression = p_function->is_coroutine ? "await " + call : call;

	const FSParser::DataType return_type = p_function->get_datatype();
	const bool is_void = return_type.is_set() && !return_type.is_variant() &&
			return_type.kind == FSParser::DataType::BUILTIN &&
			return_type.builtin_type == Variant::NIL;
	return body_indent + (is_void ? expression : "return " + expression) + "\n";
}

String render_concrete_script_override_stub(
		const FSParser::FunctionNode *p_function,
		const Vector<String> &p_lines,
		const String &p_class_indent,
		const Vector<FSParser::DataType> *p_parameter_types = nullptr,
		const FSParser::DataType *p_return_type_override = nullptr,
		const Vector<FSParser::DataType> *p_type_parameter_bounds = nullptr) {
	return render_function_signature(p_function, p_lines, p_class_indent, p_parameter_types, p_return_type_override, p_type_parameter_bounds) + ":\n" +
			render_super_call_body(p_function, p_class_indent);
}

// Per-member indentation of the target class. When the class has members, mirror the
// real indentation of its first member line. When it has none, the top-level (root)
// class has members at column zero, while an inner class is one tab deeper than its
// header.
String class_member_indent(const FSParser::ClassNode *p_class, const Vector<String> &p_lines, bool p_is_top_level) {
	for (const FSParser::ClassNode::Member &member : p_class->members) {
		const FSParser::Node *node = member.get_source_node();
		if (node == nullptr) {
			continue;
		}
		const int line_index = node->start_line - 1;
		if (line_index >= 0 && line_index < p_lines.size()) {
			return get_leading_whitespace(p_lines[line_index]);
		}
	}
	for (const FSParser::ClassNode::TraitUse &trait_use : p_class->used_traits) {
		for (const FSParser::IdentifierNode *identifier : trait_use.name) {
			if (identifier == nullptr || identifier->start_line <= p_class->start_line) {
				continue;
			}
			const int line_index = identifier->start_line - 1;
			if (line_index >= 0 && line_index < p_lines.size()) {
				return get_leading_whitespace(p_lines[line_index]);
			}
		}
	}
	if (p_is_top_level) {
		return "";
	}
	if (p_class->start_line >= 1 && p_class->start_line - 1 < p_lines.size()) {
		return get_leading_whitespace(p_lines[p_class->start_line - 1]) + "\t";
	}
	return "\t";
}

int trait_use_end_line(const FSParser::ClassNode::TraitUse &p_trait_use) {
	int end_line = 0;
	for (const FSParser::IdentifierNode *identifier : p_trait_use.name) {
		if (identifier != nullptr && identifier->end_line > end_line) {
			end_line = identifier->end_line;
		}
	}
	for (const FSParser::TypeNode *type_argument : p_trait_use.type_arguments) {
		if (type_argument != nullptr && type_argument->end_line > end_line) {
			end_line = type_argument->end_line;
		}
	}
	return end_line;
}

String make_override_method_id(const char *p_kind, const String &p_origin, const StringName &p_name, const String &p_fingerprint) {
	return String(p_kind) + "|" + p_origin + "|" + String(p_name) + "|" + p_fingerprint.md5_text();
}

StringName get_identifier_name_or_empty(const FSParser::IdentifierNode *p_identifier) {
	return p_identifier != nullptr ? p_identifier->name : StringName();
}

StringName get_override_member_name(const FSParser::ClassNode::Member &p_member) {
	switch (p_member.type) {
		case FSParser::ClassNode::Member::CLASS:
			return p_member.m_class != nullptr ? get_identifier_name_or_empty(p_member.m_class->identifier) : StringName();
		case FSParser::ClassNode::Member::CONSTANT:
			return p_member.constant != nullptr ? get_identifier_name_or_empty(p_member.constant->identifier) : StringName();
		case FSParser::ClassNode::Member::FUNCTION:
			return p_member.function != nullptr ? get_identifier_name_or_empty(p_member.function->identifier) : StringName();
		case FSParser::ClassNode::Member::SIGNAL:
			return p_member.signal != nullptr ? get_identifier_name_or_empty(p_member.signal->identifier) : StringName();
		case FSParser::ClassNode::Member::VARIABLE:
			return p_member.variable != nullptr ? get_identifier_name_or_empty(p_member.variable->identifier) : StringName();
		case FSParser::ClassNode::Member::ENUM:
			return p_member.m_enum != nullptr ? get_identifier_name_or_empty(p_member.m_enum->identifier) : StringName();
		case FSParser::ClassNode::Member::ENUM_VALUE:
			return get_identifier_name_or_empty(p_member.enum_value.identifier);
		case FSParser::ClassNode::Member::TUPLE:
			return p_member.m_tuple != nullptr ? get_identifier_name_or_empty(p_member.m_tuple->identifier) : StringName();
		case FSParser::ClassNode::Member::GROUP:
			return StringName();
		case FSParser::ClassNode::Member::UNDEFINED:
			return StringName();
	}
	return StringName();
}

void collect_declared_override_member_names(const FSParser::ClassNode *p_target, HashSet<StringName> &r_names) {
	if (p_target == nullptr) {
		return;
	}
	for (const FSParser::ClassNode::Member &member : p_target->members) {
		const StringName name = get_override_member_name(member);
		if (name != StringName()) {
			r_names.insert(name);
		}
	}
}

bool is_constructor_like_override_method(const StringName &p_name) {
	return p_name == SNAME("_init") || p_name == SNAME("_static_init");
}

bool is_void_datatype(const FSParser::DataType &p_type) {
	return p_type.is_set() && !p_type.is_variant() &&
			p_type.kind == FSParser::DataType::BUILTIN &&
			p_type.builtin_type == Variant::NIL;
}

void collect_method_type_parameter_names(
		const FSParser::FunctionNode *p_function,
		HashSet<StringName> &r_names) {
	if (p_function == nullptr) {
		return;
	}
	for (const FSParser::TypeParameterNode *type_parameter : p_function->type_parameters) {
		if (type_parameter != nullptr && type_parameter->identifier != nullptr) {
			r_names.insert(type_parameter->identifier->name);
		}
	}
}

void collect_method_type_parameters_referenced_by_type(
		const FSParser::DataType &p_type,
		const HashSet<StringName> &p_method_type_parameters,
		HashSet<StringName> &r_referenced) {
	// Keep this in sync with FSAnalyzer::collect_type_parameter_bindings(), which
	// only infers method type parameters from these structural positions.
	if (p_type.kind == FSParser::DataType::TYPE_PARAMETER &&
			p_type.type_parameter_scope == FSParser::DataType::TYPE_PARAMETER_METHOD &&
			p_method_type_parameters.has(p_type.type_parameter_name)) {
		r_referenced.insert(p_type.type_parameter_name);
	}
	for (const FSParser::DataType &element_type : p_type.container_element_types) {
		collect_method_type_parameters_referenced_by_type(element_type, p_method_type_parameters, r_referenced);
	}
	for (const FSParser::DataType &type_argument : p_type.type_arguments) {
		collect_method_type_parameters_referenced_by_type(type_argument, p_method_type_parameters, r_referenced);
	}
}

bool generic_method_type_parameters_are_inferable_from_parameter_types(
		const FSParser::FunctionNode *p_function,
		const Vector<FSParser::DataType> &p_parameter_types) {
	HashSet<StringName> method_type_parameters;
	collect_method_type_parameter_names(p_function, method_type_parameters);
	if (method_type_parameters.is_empty()) {
		return true;
	}

	HashSet<StringName> referenced_type_parameters;
	for (const FSParser::DataType &parameter_type : p_parameter_types) {
		collect_method_type_parameters_referenced_by_type(
				parameter_type,
				method_type_parameters,
				referenced_type_parameters);
	}

	for (const StringName &type_parameter : method_type_parameters) {
		if (!referenced_type_parameters.has(type_parameter)) {
			return false;
		}
	}
	return true;
}

bool override_class_type_matches(const FSParser::DataType &p_type, const FSParser::ClassNode *p_class) {
	if (p_type.class_type == nullptr || p_class == nullptr) {
		return false;
	}
	return p_type.class_type == p_class || p_type.class_type->fqcn == p_class->fqcn;
}

void collect_override_class_type_parameter_names(
		const FSParser::ClassNode *p_class,
		HashSet<StringName> &r_names) {
	if (p_class == nullptr) {
		return;
	}
	for (const FSParser::TypeParameterNode *type_parameter : p_class->type_parameters) {
		if (type_parameter != nullptr && type_parameter->identifier != nullptr) {
			r_names.insert(type_parameter->identifier->name);
		}
	}
}

bool erase_raw_override_class_type_parameters(
		FSParser::DataType &r_type,
		const HashSet<StringName> &p_raw_class_type_parameters);

FSParser::DataType specialize_override_parent_type(
		const FSParser::ClassNode *p_current_class,
		const FSParser::DataType &p_current_specialized_type) {
	if (p_current_class == nullptr) {
		return FSParser::DataType();
	}

	FSParser::DataType parent_type = p_current_class->base_type;
	if (!override_class_type_matches(p_current_specialized_type, p_current_class)) {
		return parent_type;
	}
	if (!p_current_specialized_type.has_type_arguments()) {
		HashSet<StringName> raw_class_type_parameters;
		collect_override_class_type_parameter_names(p_current_class, raw_class_type_parameters);
		erase_raw_override_class_type_parameters(parent_type, raw_class_type_parameters);
		return parent_type;
	}

	HashMap<StringName, FSParser::DataType> bindings;
	const Vector<FSParser::TypeParameterNode *> &type_parameters = p_current_class->type_parameters;
	const int binding_count = MIN(type_parameters.size(), p_current_specialized_type.type_arguments.size());
	for (int i = 0; i < binding_count; i++) {
		const FSParser::TypeParameterNode *type_parameter = type_parameters[i];
		if (type_parameter != nullptr && type_parameter->identifier != nullptr) {
			bindings.insert(type_parameter->identifier->name, p_current_specialized_type.type_arguments[i]);
		}
	}
	if (!bindings.is_empty()) {
		parent_type = FSParser::DataType::substitute(parent_type, bindings);
	}
	return parent_type;
}

FSParser::DataType align_specialized_override_type_to_class(
		const FSParser::DataType &p_type,
		const FSParser::ClassNode *p_class) {
	FSParser::DataType result = p_type;
	if (p_class != nullptr && (result.class_type == nullptr || override_class_type_matches(result, p_class))) {
		result.class_type = const_cast<FSParser::ClassNode *>(p_class);
	}
	return result;
}

bool erase_raw_override_class_type_parameters(
		FSParser::DataType &r_type,
		const HashSet<StringName> &p_raw_class_type_parameters) {
	if (r_type.kind == FSParser::DataType::TYPE_PARAMETER &&
			r_type.type_parameter_scope == FSParser::DataType::TYPE_PARAMETER_CLASS &&
			p_raw_class_type_parameters.has(r_type.type_parameter_name)) {
		r_type = FSParser::DataType::get_variant_type();
		return true;
	}

	bool clear_container_elements = false;
	for (int i = 0; i < r_type.container_element_types.size(); i++) {
		if (erase_raw_override_class_type_parameters(r_type.container_element_types.write[i], p_raw_class_type_parameters)) {
			clear_container_elements = true;
		}
	}
	if (clear_container_elements) {
		r_type.container_element_types.clear();
	}

	bool clear_type_arguments = false;
	for (int i = 0; i < r_type.type_arguments.size(); i++) {
		if (erase_raw_override_class_type_parameters(r_type.type_arguments.write[i], p_raw_class_type_parameters)) {
			clear_type_arguments = true;
		}
	}
	if (clear_type_arguments) {
		r_type.type_arguments.clear();
	}

	bool clear_method_signature = false;
	for (int i = 0; i < r_type.method_parameter_types.size(); i++) {
		if (erase_raw_override_class_type_parameters(r_type.method_parameter_types.write[i], p_raw_class_type_parameters)) {
			clear_method_signature = true;
		}
	}
	for (int i = 0; i < r_type.method_return_type.size(); i++) {
		if (erase_raw_override_class_type_parameters(r_type.method_return_type.write[i], p_raw_class_type_parameters)) {
			clear_method_signature = true;
		}
	}
	if (clear_method_signature) {
		r_type.has_method_signature = false;
		r_type.has_explicit_method_signature = false;
		r_type.method_parameter_types.clear();
		r_type.method_return_type.clear();
	}

	bool clear_bound = false;
	for (int i = 0; i < r_type.type_parameter_bound.size(); i++) {
		if (erase_raw_override_class_type_parameters(r_type.type_parameter_bound.write[i], p_raw_class_type_parameters)) {
			clear_bound = true;
		}
	}
	if (clear_bound) {
		r_type.type_parameter_bound.clear();
	}

	return false;
}

FSParser::DataType substitute_override_member_type(
		const FSParser::DataType &p_member_type,
		const FSParser::DataType &p_specialized_base,
		const FSParser::ClassNode *p_declaring_class,
		const FSParser::FunctionNode *p_shadowing_method) {
	if (p_declaring_class == nullptr) {
		return p_member_type;
	}

	FSParser::DataType member_type = p_member_type;
	HashMap<StringName, FSParser::DataType> bindings;
	if (!p_specialized_base.type_arguments.is_empty()) {
		const Vector<FSParser::TypeParameterNode *> &type_parameters = p_declaring_class->type_parameters;
		const int binding_count = MIN(type_parameters.size(), p_specialized_base.type_arguments.size());
		for (int i = 0; i < binding_count; i++) {
			const FSParser::TypeParameterNode *type_parameter = type_parameters[i];
			if (type_parameter != nullptr && type_parameter->identifier != nullptr) {
				bindings.insert(type_parameter->identifier->name, p_specialized_base.type_arguments[i]);
			}
		}
		if (p_shadowing_method != nullptr) {
			for (const FSParser::TypeParameterNode *type_parameter : p_shadowing_method->type_parameters) {
				if (type_parameter != nullptr && type_parameter->identifier != nullptr) {
					bindings.erase(type_parameter->identifier->name);
				}
			}
		}
		if (!bindings.is_empty()) {
			member_type = FSParser::DataType::substitute(member_type, bindings);
		}
	}

	if (p_specialized_base.type_arguments.is_empty()) {
		HashSet<StringName> raw_class_type_parameters;
		collect_override_class_type_parameter_names(p_declaring_class, raw_class_type_parameters);
		erase_raw_override_class_type_parameters(member_type, raw_class_type_parameters);
	}
	return member_type;
}

int find_class_method_insertion_line(
		const FSParser::ClassNode *p_target,
		const FSParser::ClassNode *p_tree,
		const Vector<String> &p_lines) {
	int insertion_line = p_target != nullptr ? p_target->start_line : 0;
	bool has_member_line = false;
	if (p_target == nullptr) {
		return insertion_line;
	}
	for (const FSParser::ClassNode::Member &member : p_target->members) {
		const FSParser::Node *node = member.get_source_node();
		if (node != nullptr && node->end_line > 0) {
			has_member_line = true;
			if (node->end_line > insertion_line) {
				insertion_line = node->end_line;
			}
		}
	}
	for (const FSParser::ClassNode::TraitUse &trait_use : p_target->used_traits) {
		const int trait_end = trait_use_end_line(trait_use);
		if (trait_end > p_target->start_line && trait_end > insertion_line) {
			insertion_line = trait_end;
		}
	}
	if (!has_member_line && p_target == p_tree) {
		insertion_line = p_lines.size();
		if (insertion_line > 0 && p_lines[insertion_line - 1].is_empty()) {
			insertion_line -= 1;
		}
	}
	if (insertion_line > p_lines.size()) {
		insertion_line = p_lines.size();
	}
	return insertion_line;
}

ImplementAbstractCandidate find_implement_abstract_in_tree(
		const RefactorLocation &p_location,
		const String &p_path,
		const Vector<String> &p_lines,
		const FSParser::ClassNode *p_tree,
		const FSParseResultProvider *p_parse_results) {
	ImplementAbstractCandidate candidate;

	const FSParser::ClassNode *target = find_enclosing_class(p_tree, p_location.start_line);
	if (target == nullptr) {
		candidate.disabled_reason = "No unimplemented abstract methods.";
		return candidate;
	}

	if (target->is_abstract) {
		candidate.disabled_reason = "Abstract classes don't need to implement abstract methods.";
		return candidate;
	}

	// A trait may defer the abstract methods it inherits from a `uses`d trait to the
	// concrete class that applies it, so the analyzer skips trait-requirement validation
	// for traits; mirror that here rather than offering the action inside a trait.
	if (target->is_trait) {
		candidate.disabled_reason = "Traits don't need to implement abstract methods.";
		return candidate;
	}

	collect_owed_abstract_methods(target, p_tree, p_path, p_lines, candidate.abstract_methods, &candidate.depends_on_external_declarations, p_parse_results);
	if (candidate.abstract_methods.is_empty()) {
		candidate.disabled_reason = "No unimplemented abstract methods.";
		return candidate;
	}

	// Finalize all rendered text now, while the parse tree is alive, so the cached
	// candidate carries only value types and no live FunctionNode pointer.
	const String class_indent = class_member_indent(target, p_lines, target == p_tree);
	// When a cross-file base could not be re-parsed, its declaring lines are unknown;
	// rendering against empty lines makes default recovery fail cleanly, omitting the
	// default rather than slicing it out of the wrong file.
	const Vector<String> unknown_lines;
	String block;
	for (int i = 0; i < candidate.abstract_methods.size(); i++) {
		if (i > 0) {
			block += "\n";
		}
		const OwedAbstractMethod &owed = candidate.abstract_methods[i];
		// Recover default-value text from the file that declares the method (the base
		// file for a cross-file abstract base), not unconditionally from the target.
		const Vector<String> &method_lines = owed.declaring_lines != nullptr ? *owed.declaring_lines : unknown_lines;
		block += render_abstract_stub(owed.function, method_lines, class_indent);
	}
	candidate.rendered_block = block;
	candidate.insertion_line = find_class_method_insertion_line(target, p_tree, p_lines);
	candidate.abstract_methods.clear(); // Do not cache live pointers.
	candidate.matched = true;
	candidate.enabled = true;
	return candidate;
}

ImplementAbstractCandidate find_implement_abstract_candidate_uncached(
		const RefactorContext &p_context,
		const RefactorLocation &p_location,
		const Vector<String> &p_lines,
		const FSParseResultProvider *p_parse_results) {
#ifndef FOUNDRY_SCRIPT_NO_LSP
	if (p_parse_results != nullptr) {
		const ExtendFSParser *lsp_parser = p_parse_results->get_parse_result(p_context.path);
		if (lsp_parser != nullptr && source_lines_match(lsp_parser->get_lines(), p_lines)) {
			// A class that still owes abstract methods makes the analyzer report an
			// error, so `parse_result` is not OK even though the tree is fully built
			// and base types are resolved. Detect from the tree whenever one exists;
			// only a hard parse failure leaves no usable tree.
			const FSParser::ClassNode *tree = lsp_parser->get_tree();
			if (tree == nullptr) {
				ImplementAbstractCandidate candidate;
				candidate.disabled_reason = "Cannot analyze this script.";
				return candidate;
			}
			return find_implement_abstract_in_tree(p_location, p_context.path, p_lines, tree, p_parse_results);
		}
	}
#endif // FOUNDRY_SCRIPT_NO_LSP

	FSParser parser;
	Error err = parser.parse(p_context.source, p_context.path, false);
	if (err != OK) {
		ImplementAbstractCandidate candidate;
		candidate.disabled_reason = "Cannot analyze this script.";
		return candidate;
	}

	// A class that still owes abstract methods makes the analyzer report an error
	// ("must implement ... abstract methods") — that is precisely the situation this
	// refactor resolves. The analyzer still resolves base types and builds the tree
	// before flagging that error, so detection must run regardless of the analyze
	// result; only a hard parse failure (handled above) leaves no usable tree.
	FSAnalyzer analyzer(&parser);
	analyzer.analyze();

	return find_implement_abstract_in_tree(p_location, p_context.path, p_lines, parser.get_tree(), p_parse_results);
}

ImplementAbstractCandidate find_implement_abstract_candidate(
		const RefactorContext &p_context,
		const RefactorLocation &p_location,
		const FSParseResultProvider *p_parse_results) {
	ImplementAbstractCandidate candidate;
	if (get_cached_implement_abstract_candidate(p_context, p_location, candidate)) {
		return candidate;
	}

	const Vector<String> lines = p_context.source.split("\n");
	candidate = find_implement_abstract_candidate_uncached(p_context, p_location, lines, p_parse_results);
	if (!candidate.depends_on_external_declarations) {
		cache_implement_abstract_candidate(p_context, p_location, candidate);
	}
	return candidate;
}

RefactorResult prepare_implement_abstract(
		const RefactorContext &p_context,
		const RefactorLocation &p_location,
		const FSParseResultProvider *p_parse_results) {
	RefactorResult result;
	const ImplementAbstractCandidate candidate = find_implement_abstract_candidate(p_context, p_location, p_parse_results);
	if (!candidate.enabled) {
		result.ok = false;
		result.error_message = candidate.disabled_reason;
		return result;
	}

	const Vector<String> lines = p_context.source.split("\n");
	const bool has_final_newline = p_context.source.ends_with("\n");

	RefactorTextEdit edit;
	set_extract_method_insertion(edit, lines, has_final_newline, candidate.insertion_line, candidate.rendered_block);

	// Point the post-edit caret at the first inserted stub so the editor scrolls to it.
	// The inserted text is the leading newlines of the edit followed by the rendered
	// block, so the first stub line sits just past those newlines. The column targets the
	// stub's leading indentation, which is where the `func`/`async func` keyword begins.
	int leading_newlines = 0;
	while (leading_newlines < edit.new_text.length() && edit.new_text[leading_newlines] == '\n') {
		leading_newlines++;
	}
	result.rename_anchor_line = edit.start_line + leading_newlines;
	int caret_column = 0;
	while (caret_column < candidate.rendered_block.length() && candidate.rendered_block[caret_column] == '\t') {
		caret_column++;
	}
	result.rename_anchor_column = caret_column;

	result.ok = true;
	result.edits.push_back(edit);
	return result;
}

void add_script_override_candidate(
		const FSParser::FunctionNode *p_function,
		const FSParser::ClassNode *p_declaring_class,
		const Vector<String> *p_declaring_lines,
		const FSParser::DataType &p_specialized_base,
		const String &p_class_indent,
		int p_insertion_line,
		Vector<OverrideMethodCandidate> &r_candidates) {
	if (p_function == nullptr || p_function->identifier == nullptr || p_function->is_final || p_function->is_abstract ||
			p_function->rest_parameter != nullptr ||
			is_constructor_like_override_method(p_function->identifier->name)) {
		return;
	}
	const Vector<String> empty_lines;
	const Vector<String> &lines = p_declaring_lines != nullptr ? *p_declaring_lines : empty_lines;
	Vector<FSParser::DataType> parameter_types;
	for (const FSParser::ParameterNode *parameter : p_function->parameters) {
		FSParser::DataType parameter_type;
		if (parameter != nullptr) {
			parameter_type = substitute_override_member_type(parameter->get_datatype(), p_specialized_base, p_declaring_class, p_function);
		}
		parameter_types.push_back(parameter_type);
	}
	if (!generic_method_type_parameters_are_inferable_from_parameter_types(p_function, parameter_types)) {
		return;
	}
	const FSParser::DataType return_type =
			substitute_override_member_type(p_function->get_datatype(), p_specialized_base, p_declaring_class, p_function);
	Vector<FSParser::DataType> type_parameter_bounds;
	for (const FSParser::TypeParameterNode *type_parameter : p_function->type_parameters) {
		FSParser::DataType bound_type;
		if (type_parameter != nullptr && type_parameter->bound != nullptr) {
			if (type_parameter->resolved_bound.is_set()) {
				bound_type = type_parameter->resolved_bound;
			} else {
				bound_type = type_parameter->bound->get_datatype();
				bound_type.is_meta_type = false;
			}
			bound_type = substitute_override_member_type(bound_type, p_specialized_base, p_declaring_class, p_function);
		}
		type_parameter_bounds.push_back(bound_type);
	}
	OverrideMethodCandidate candidate;
	candidate.enabled = true;
	candidate.rendered_block = render_concrete_script_override_stub(
			p_function,
			lines,
			p_class_indent,
			&parameter_types,
			&return_type,
			&type_parameter_bounds);
	candidate.insertion_line = p_insertion_line;
	const String origin = p_declaring_class != nullptr && p_declaring_class->identifier != nullptr
			? String(p_declaring_class->identifier->name)
			: String("base class");
	candidate.public_candidate.name = String(p_function->identifier->name);
	candidate.public_candidate.signature = render_function_signature(p_function, lines, "", &parameter_types, &return_type, &type_parameter_bounds);
	candidate.public_candidate.origin = origin;
	candidate.public_candidate.detail = candidate.public_candidate.signature + " - " + origin;
	candidate.public_candidate.id = make_override_method_id("script", origin, p_function->identifier->name, candidate.rendered_block);
	r_candidates.push_back(candidate);
}

void add_abstract_override_candidate(
		const OwedAbstractMethod &p_owed,
		const String &p_class_indent,
		int p_insertion_line,
		Vector<OverrideMethodCandidate> &r_candidates) {
	if (p_owed.function == nullptr || p_owed.function->identifier == nullptr ||
			is_constructor_like_override_method(p_owed.function->identifier->name)) {
		return;
	}
	Vector<FSParser::DataType> parameter_types;
	for (const FSParser::ParameterNode *parameter : p_owed.function->parameters) {
		FSParser::DataType parameter_type;
		if (parameter != nullptr) {
			parameter_type = substitute_override_member_type(parameter->get_datatype(), p_owed.specialized_base, p_owed.declaring_class, p_owed.function);
		}
		parameter_types.push_back(parameter_type);
	}
	if (!generic_method_type_parameters_are_inferable_from_parameter_types(p_owed.function, parameter_types)) {
		return;
	}
	FSParser::DataType rest_parameter_type;
	const FSParser::DataType *rest_parameter_type_ptr = nullptr;
	if (p_owed.function->rest_parameter != nullptr) {
		rest_parameter_type = substitute_override_member_type(
				p_owed.function->rest_parameter->get_datatype(),
				p_owed.specialized_base,
				p_owed.declaring_class,
				p_owed.function);
		rest_parameter_type_ptr = &rest_parameter_type;
	}
	const FSParser::DataType return_type =
			substitute_override_member_type(p_owed.function->get_datatype(), p_owed.specialized_base, p_owed.declaring_class, p_owed.function);
	Vector<FSParser::DataType> type_parameter_bounds;
	for (const FSParser::TypeParameterNode *type_parameter : p_owed.function->type_parameters) {
		FSParser::DataType bound_type;
		if (type_parameter != nullptr && type_parameter->bound != nullptr) {
			if (type_parameter->resolved_bound.is_set()) {
				bound_type = type_parameter->resolved_bound;
			} else {
				bound_type = type_parameter->bound->get_datatype();
				bound_type.is_meta_type = false;
			}
			bound_type = substitute_override_member_type(bound_type, p_owed.specialized_base, p_owed.declaring_class, p_owed.function);
		}
		type_parameter_bounds.push_back(bound_type);
	}

	const Vector<String> empty_lines;
	const Vector<String> &lines = p_owed.declaring_lines != nullptr ? *p_owed.declaring_lines : empty_lines;
	OverrideMethodCandidate candidate;
	candidate.enabled = true;
	candidate.rendered_block = render_abstract_stub(
			p_owed.function,
			lines,
			p_class_indent,
			&parameter_types,
			&return_type,
			&type_parameter_bounds,
			rest_parameter_type_ptr);
	candidate.insertion_line = p_insertion_line;
	const String name = String(p_owed.function->identifier->name);
	candidate.public_candidate.name = name;
	candidate.public_candidate.signature = render_function_signature(
			p_owed.function,
			lines,
			"",
			&parameter_types,
			&return_type,
			&type_parameter_bounds,
			rest_parameter_type_ptr);
	candidate.public_candidate.origin = "abstract requirement";
	candidate.public_candidate.detail = candidate.public_candidate.signature + " - abstract requirement";
	candidate.public_candidate.id = make_override_method_id(
			"abstract",
			candidate.public_candidate.origin,
			p_owed.function->identifier->name,
			candidate.rendered_block);
	r_candidates.push_back(candidate);
}

void collect_script_base_override_candidates(
		const FSParser::ClassNode *p_target,
		const String &p_target_path,
		const Vector<String> &p_target_lines,
		const FSParseResultProvider *p_parse_results,
		HashSet<StringName> &r_decided_names,
		const String &p_class_indent,
		int p_insertion_line,
		Vector<OverrideMethodCandidate> &r_candidates) {
	if (p_target == nullptr) {
		return;
	}

	HashSet<const FSParser::ClassNode *> visited;
	visited.insert(p_target);
	const FSParser::ClassNode *current = p_target;
	FSParser::DataType current_specialized_type;
	String current_path = p_target_path;
	const Vector<String> *current_lines = &p_target_lines;
	HashSet<StringName> local_decided_names = r_decided_names;

	while (current != nullptr) {
		String base_path;
		const Vector<String> *base_lines = nullptr;
		FSParser::DataType specialized_base = specialize_override_parent_type(current, current_specialized_type);
		const FSParser::ClassNode *base = resolve_base_class(current, p_parse_results, current_path, current_lines, base_path, &base_lines);
		if (base == nullptr || visited.has(base)) {
			break;
		}
		specialized_base = align_specialized_override_type_to_class(specialized_base, base);
		visited.insert(base);
		current = base;
		current_specialized_type = specialized_base;
		current_path = base_path;
		current_lines = base_lines;

		for (const FSParser::ClassNode::Member &member : current->members) {
			const StringName name = get_override_member_name(member);
			if (name == StringName()) {
				continue;
			}
			const bool already_decided = local_decided_names.has(name);
			bool propagate_decided_name = true;
			if (!already_decided && member.type == FSParser::ClassNode::Member::FUNCTION) {
				const FSParser::FunctionNode *function = member.function;
				if (function != nullptr) {
					propagate_decided_name = !function->is_abstract;
					add_script_override_candidate(
							function,
							current,
							current_lines,
							current_specialized_type,
							p_class_indent,
							p_insertion_line,
							r_candidates);
				}
			}
			local_decided_names.insert(name);
			if (!already_decided && propagate_decided_name) {
				r_decided_names.insert(name);
			}
		}
	}
}

StringName native_base_name_for_class(
		const FSParser::ClassNode *p_target,
		const String &p_target_path,
		const Vector<String> &p_target_lines,
		const FSParseResultProvider *p_parse_results) {
	if (p_target == nullptr) {
		return StringName();
	}

	HashSet<const FSParser::ClassNode *> visited;
	const FSParser::ClassNode *current = p_target;
	String current_path = p_target_path;
	const Vector<String> *current_lines = &p_target_lines;

	while (current != nullptr) {
		if (visited.has(current)) {
			return StringName();
		}
		visited.insert(current);

		const FSParser::DataType &base_type = current->base_type;
		switch (base_type.kind) {
			case FSParser::DataType::NATIVE:
				return base_type.native_type;
			case FSParser::DataType::SCRIPT: {
				Ref<Script> base_script = base_type.script_type;
				StringName native_type = base_type.native_type;
				while (base_script.is_valid()) {
					if (native_type == StringName()) {
						native_type = base_script->get_instance_base_type();
					}
					base_script = base_script->get_base_script();
				}
				return native_type;
			}
			case FSParser::DataType::CLASS: {
				String base_path;
				const Vector<String> *base_lines = nullptr;
				const FSParser::ClassNode *base = resolve_base_class(
						current,
						p_parse_results,
						current_path,
						current_lines,
						base_path,
						&base_lines);
				if (base == nullptr) {
					return base_type.native_type;
				}
				current = base;
				current_path = base_path;
				current_lines = base_lines;
				continue;
			}
			case FSParser::DataType::BUILTIN:
			case FSParser::DataType::ENUM:
			case FSParser::DataType::TUPLE:
			case FSParser::DataType::TYPE_PARAMETER:
			case FSParser::DataType::VARIANT:
			case FSParser::DataType::RESOLVING:
			case FSParser::DataType::UNRESOLVED:
				return StringName();
		}
	}

	return StringName();
}

FSParser::DataType native_datatype_from_property(const PropertyInfo &p_property, bool p_is_argument) {
	FSParser::DataType result;
	result.type_source = FSParser::DataType::ANNOTATED_EXPLICIT;
	result.is_read_only = false;
	if (p_property.type == Variant::NIL &&
			(p_is_argument || (p_property.usage & PROPERTY_USAGE_NIL_IS_VARIANT))) {
		result.kind = FSParser::DataType::VARIANT;
		return result;
	}
	if (p_property.type == Variant::OBJECT) {
		StringName class_name = p_property.class_name;
		if (String(class_name).ends_with("?")) {
			String nullable_class_name = class_name;
			nullable_class_name = nullable_class_name.substr(0, nullable_class_name.length() - 1);
			class_name = nullable_class_name;
			result.is_nullable = true;
		}
		result.kind = FSParser::DataType::NATIVE;
		result.native_type = class_name == StringName() ? SNAME("Object") : class_name;
		return result;
	}

	result.kind = FSParser::DataType::BUILTIN;
	result.builtin_type = p_property.type;
	return result;
}

bool is_available_native_argument_name(const String &p_name, const HashSet<StringName> &p_used_names) {
	String reason;
	return FSRefactorNames::validate_identifier(p_name, reason) && !p_used_names.has(StringName(p_name));
}

String native_argument_name(const PropertyInfo &p_argument, int p_index, HashSet<StringName> &r_used_names) {
	const String original_name = p_argument.name;
	if (is_available_native_argument_name(original_name, r_used_names)) {
		r_used_names.insert(StringName(original_name));
		return original_name;
	}

	if (!original_name.is_empty()) {
		const String suffixed_name = original_name + "_arg";
		if (is_available_native_argument_name(suffixed_name, r_used_names)) {
			r_used_names.insert(StringName(suffixed_name));
			return suffixed_name;
		}
	}

	const String base_name = "arg" + itos(p_index + 1);
	if (is_available_native_argument_name(base_name, r_used_names)) {
		r_used_names.insert(StringName(base_name));
		return base_name;
	}

	int suffix = 2;
	while (true) {
		const String candidate_name = base_name + "_" + itos(suffix);
		if (is_available_native_argument_name(candidate_name, r_used_names)) {
			r_used_names.insert(StringName(candidate_name));
			return candidate_name;
		}
		suffix++;
	}
}

bool render_native_method_signature(
		const MethodInfo &p_method,
		const String &p_class_indent,
		Vector<String> &r_argument_names,
		FSParser::DataType &r_return_type,
		String &r_signature) {
	String reason;
	if (p_method.name.is_empty() || !FSRefactorNames::validate_identifier(p_method.name, reason)) {
		return false;
	}

	String signature = p_class_indent + "func " + p_method.name + "(";
	HashSet<StringName> used_argument_names;
	for (int i = 0; i < p_method.arguments.size(); i++) {
		if (i > 0) {
			signature += ", ";
		}
		const String argument_name = native_argument_name(p_method.arguments[i], i, used_argument_names);
		r_argument_names.push_back(argument_name);
		signature += argument_name;

		const FSParser::DataType argument_type = native_datatype_from_property(p_method.arguments[i], true);
		String rendered_type;
		if (FSRefactorTypes::render_annotatable_type(argument_type, rendered_type)) {
			signature += ": " + rendered_type;
		}
	}
	signature += ")";

	r_return_type = native_datatype_from_property(p_method.return_val, false);
	if (is_void_datatype(r_return_type)) {
		signature += " -> void";
	} else {
		String rendered_return;
		if (FSRefactorTypes::render_annotatable_type(r_return_type, rendered_return)) {
			signature += " -> " + rendered_return;
		}
	}

	r_signature = signature;
	return true;
}

String render_native_default_body(const FSParser::DataType &p_return_type, const String &p_class_indent) {
	const String body_indent = p_class_indent + "\t";
	if (is_void_datatype(p_return_type)) {
		return body_indent + "pass\n";
	}

	String literal;
	if (default_return_literal(p_return_type, literal)) {
		return body_indent + "return " + literal + "\n";
	}

	return body_indent + "pass\n";
}

void add_native_override_candidate(
		const StringName &p_native_class,
		const MethodInfo &p_method,
		const String &p_class_indent,
		int p_insertion_line,
		bool p_is_script_extensible_hook,
		Vector<OverrideMethodCandidate> &r_candidates) {
	// Script-extensible native hooks are bound as regular methods (dispatched through
	// Object::call()), so they carry no METHOD_FLAG_VIRTUAL despite being intentional
	// override points.
	const bool is_overridable = (p_method.flags & METHOD_FLAG_VIRTUAL) != 0 || p_is_script_extensible_hook;
	if (!is_overridable ||
			(p_method.flags & METHOD_FLAG_VARARG) != 0 || (p_method.flags & METHOD_FLAG_STATIC) != 0 ||
			is_constructor_like_override_method(StringName(p_method.name))) {
		return;
	}

	Vector<String> argument_names;
	FSParser::DataType return_type;
	String signature;
	if (!render_native_method_signature(p_method, "", argument_names, return_type, signature)) {
		return;
	}

	OverrideMethodCandidate candidate;
	candidate.enabled = true;
	candidate.rendered_block = p_class_indent + signature + ":\n" +
			render_native_default_body(return_type, p_class_indent);
	candidate.insertion_line = p_insertion_line;
	candidate.public_candidate.name = p_method.name;
	candidate.public_candidate.signature = signature;
	candidate.public_candidate.origin = String(p_native_class);
	candidate.public_candidate.detail =
			candidate.public_candidate.signature + " - " + candidate.public_candidate.origin;
	candidate.public_candidate.id =
			make_override_method_id("native", String(p_native_class), StringName(p_method.name), candidate.rendered_block);
	r_candidates.push_back(candidate);
}

void collect_native_base_override_candidates(
		const FSParser::ClassNode *p_target,
		const String &p_target_path,
		const Vector<String> &p_target_lines,
		const FSParseResultProvider *p_parse_results,
		HashSet<StringName> p_decided_names,
		const String &p_class_indent,
		int p_insertion_line,
		Vector<OverrideMethodCandidate> &r_candidates) {
	const StringName native_base =
			native_base_name_for_class(p_target, p_target_path, p_target_lines, p_parse_results);
	if (native_base == StringName() || !ClassDB::class_exists(native_base)) {
		return;
	}

	List<MethodInfo> methods;
	ClassDB::get_virtual_methods(native_base, &methods);
	HashSet<StringName> native_seen_names;
	for (const MethodInfo &method : methods) {
		const StringName name = StringName(method.name);
		if (name == StringName() || p_decided_names.has(name) || native_seen_names.has(name)) {
			continue;
		}
		native_seen_names.insert(name);
		add_native_override_candidate(
				native_base,
				method,
				p_class_indent,
				p_insertion_line,
				false,
				r_candidates);
		p_decided_names.insert(name);
	}

	List<StringName> hook_method_names;
	FSScriptExtensibleNativeHooks::collect_allowed_overrides(native_base, hook_method_names);
	for (const StringName &hook_method_name : hook_method_names) {
		if (p_decided_names.has(hook_method_name) || native_seen_names.has(hook_method_name)) {
			continue;
		}
		MethodInfo hook_method;
		if (!ClassDB::get_method_info(native_base, hook_method_name, &hook_method)) {
			continue;
		}
		native_seen_names.insert(hook_method_name);
		add_native_override_candidate(
				native_base,
				hook_method,
				p_class_indent,
				p_insertion_line,
				true,
				r_candidates);
		p_decided_names.insert(hook_method_name);
	}
}

void collect_override_candidate_names(
		const Vector<OverrideMethodCandidate> &p_candidates,
		HashSet<StringName> &r_names) {
	for (const OverrideMethodCandidate &candidate : p_candidates) {
		if (!candidate.public_candidate.name.is_empty()) {
			r_names.insert(StringName(candidate.public_candidate.name));
		}
	}
}

OverrideMethodCollection collect_override_methods_in_tree(
		const RefactorLocation &p_location,
		const String &p_path,
		const Vector<String> &p_lines,
		const FSParser::ClassNode *p_tree,
		const FSParseResultProvider *p_parse_results) {
	OverrideMethodCollection collection;
	const FSParser::ClassNode *target = find_enclosing_class(p_tree, p_location.start_line);
	const bool is_implicit_root_without_script_base = target == p_tree &&
			target->identifier == nullptr &&
			(!target->extends_used || p_location.start_line <= 0) &&
			target->base_type.kind != FSParser::DataType::CLASS &&
			target->base_type.kind != FSParser::DataType::SCRIPT;
	if (target == nullptr || is_implicit_root_without_script_base) {
		collection.error_message = "Place the caret inside a class.";
		return collection;
	}
	if (target->is_trait) {
		collection.error_message = "Traits cannot override methods.";
		return collection;
	}

	HashSet<StringName> declared_names;
	collect_declared_override_member_names(target, declared_names);
	const String class_indent = class_member_indent(target, p_lines, target == p_tree);
	const int insertion_line = find_class_method_insertion_line(target, p_tree, p_lines);
	HashSet<StringName> inherited_decided_names = declared_names;
	collect_script_base_override_candidates(
			target,
			p_path,
			p_lines,
			p_parse_results,
			inherited_decided_names,
			class_indent,
			insertion_line,
			collection.candidates);

	HashSet<StringName> abstract_decided_names = inherited_decided_names;
	collect_override_candidate_names(collection.candidates, abstract_decided_names);
	Vector<OwedAbstractMethod> owed_abstract_methods;
	bool depends_on_external_declarations = false;
	collect_owed_abstract_methods(target, p_tree, p_path, p_lines, owed_abstract_methods, &depends_on_external_declarations, p_parse_results);
	for (const OwedAbstractMethod &owed : owed_abstract_methods) {
		if (owed.function == nullptr || owed.function->identifier == nullptr) {
			continue;
		}
		const StringName name = owed.function->identifier->name;
		if (abstract_decided_names.has(name)) {
			continue;
		}
		add_abstract_override_candidate(owed, class_indent, insertion_line, collection.candidates);
		abstract_decided_names.insert(name);
	}

	HashSet<StringName> native_decided_names = abstract_decided_names;
	collect_override_candidate_names(collection.candidates, native_decided_names);
	collect_native_base_override_candidates(
			target,
			p_path,
			p_lines,
			p_parse_results,
			native_decided_names,
			class_indent,
			insertion_line,
			collection.candidates);

	collection.ok = !collection.candidates.is_empty();
	if (!collection.ok) {
		collection.error_message = "No overridable methods found.";
	}
	return collection;
}

OverrideMethodCollection find_override_method_collection_uncached(
		const RefactorContext &p_context,
		const RefactorLocation &p_location,
		const Vector<String> &p_lines,
		const FSParseResultProvider *p_parse_results) {
#ifndef FOUNDRY_SCRIPT_NO_LSP
	if (p_parse_results != nullptr) {
		const ExtendFSParser *lsp_parser = p_parse_results->get_parse_result(p_context.path);
		if (lsp_parser != nullptr && source_lines_match(lsp_parser->get_lines(), p_lines)) {
			const FSParser::ClassNode *tree = lsp_parser->get_tree();
			if (tree == nullptr) {
				OverrideMethodCollection collection;
				collection.error_message = "Cannot analyze this script.";
				return collection;
			}
			return collect_override_methods_in_tree(p_location, p_context.path, p_lines, tree, p_parse_results);
		}
	}
#endif // FOUNDRY_SCRIPT_NO_LSP

	FSParser parser;
	Error err = parser.parse(p_context.source, p_context.path, false);
	if (err != OK) {
		OverrideMethodCollection collection;
		collection.error_message = "Cannot analyze this script.";
		return collection;
	}

	FSAnalyzer analyzer(&parser);
	analyzer.analyze();

	return collect_override_methods_in_tree(p_location, p_context.path, p_lines, parser.get_tree(), p_parse_results);
}

RefactorOverrideMethodsResult find_override_method_candidates(
		const RefactorContext &p_context,
		const RefactorLocation &p_location,
		const FSParseResultProvider *p_parse_results) {
	const Vector<String> lines = p_context.source.split("\n");
	const OverrideMethodCollection collection = find_override_method_collection_uncached(p_context, p_location, lines, p_parse_results);
	RefactorOverrideMethodsResult result;
	result.ok = collection.ok;
	result.error_message = collection.error_message;
	for (const OverrideMethodCandidate &candidate : collection.candidates) {
		if (candidate.enabled) {
			result.candidates.push_back(candidate.public_candidate);
		}
	}
	return result;
}

RefactorResult prepare_override_method(
		const RefactorContext &p_context,
		const RefactorLocation &p_location,
		const RefactorParams &p_params,
		const FSParseResultProvider *p_parse_results) {
	RefactorResult result;
	const Vector<String> lines = p_context.source.split("\n");
	const OverrideMethodCollection collection = find_override_method_collection_uncached(p_context, p_location, lines, p_parse_results);
	if (!collection.ok) {
		result.ok = false;
		result.error_message = collection.error_message;
		return result;
	}

	const bool has_final_newline = p_context.source.ends_with("\n");
	for (const OverrideMethodCandidate &candidate : collection.candidates) {
		if (!candidate.enabled || candidate.public_candidate.id != p_params.override_method_id) {
			continue;
		}

		RefactorTextEdit edit;
		set_extract_method_insertion(edit, lines, has_final_newline, candidate.insertion_line, candidate.rendered_block);

		int leading_newlines = 0;
		while (leading_newlines < edit.new_text.length() && edit.new_text[leading_newlines] == '\n') {
			leading_newlines++;
		}
		result.rename_anchor_line = edit.start_line + leading_newlines;
		int caret_column = 0;
		while (caret_column < candidate.rendered_block.length() && candidate.rendered_block[caret_column] == '\t') {
			caret_column++;
		}
		result.rename_anchor_column = caret_column;
		result.ok = true;
		result.edits.push_back(edit);
		return result;
	}

	result.ok = false;
	result.error_message = "Selected override method is no longer available.";
	return result;
}

RefactorResult prepare_type_annotation(
		const RefactorContext &p_context,
		const RefactorLocation &p_location,
		const FSParseResultProvider *p_parse_results) {
	RefactorResult result;
	const TypeAnnotationCandidate candidate = find_type_annotation_candidate(p_context, p_location, p_parse_results);
	if (!candidate.enabled) {
		result.ok = false;
		result.error_message = candidate.disabled_reason;
		return result;
	}

	result.ok = true;
	result.edits.push_back(candidate.edit);
	return result;
}

RefactorCandidatesResult collect_type_annotation_candidates(
		const RefactorContext &p_context,
		const FSParseResultProvider *p_parse_results) {
	RefactorCandidatesResult result;
	const Vector<String> lines = p_context.source.split("\n");

	const FSParser::ClassNode *tree = nullptr;
#ifndef FOUNDRY_SCRIPT_NO_LSP
	if (p_parse_results != nullptr) {
		const ExtendFSParser *lsp_parser = p_parse_results->get_parse_result(p_context.path);
		if (lsp_parser != nullptr && source_lines_match(lsp_parser->get_lines(), lines)) {
			if (lsp_parser->parse_result != OK) {
				result.error_message = "Cannot analyze this script.";
				return result;
			}
			tree = lsp_parser->get_tree();
		}
	}
#endif // FOUNDRY_SCRIPT_NO_LSP

	FSParser parser;
	if (tree == nullptr) {
		Error err = parser.parse(p_context.source, p_context.path, false);
		if (err == OK) {
			FSAnalyzer analyzer(&parser);
			err = analyzer.analyze();
		}
		if (err != OK) {
			result.error_message = "Cannot analyze this script.";
			return result;
		}
		tree = parser.get_tree();
	}

#ifndef FOUNDRY_SCRIPT_NO_LSP
	Ref<FSWorkspace> workspace;
	const ExtendFSParser *lsp_parser = nullptr;
	if (p_parse_results != nullptr) {
		FSLanguageProtocol *protocol = FSLanguageProtocol::get_singleton();
		workspace = protocol ? protocol->get_workspace() : Ref<FSWorkspace>();
		lsp_parser = p_parse_results->get_parse_result(p_context.path);
	}
#endif // FOUNDRY_SCRIPT_NO_LSP

	const TypeAnnotationRenderContext render_context = make_type_annotation_render_context(tree);
	const Vector<TypeAnnotationCandidate> candidates = collect_type_annotation_candidates_in_tree(lines, tree, nullptr, p_context.allow_member_container_inference, &render_context
#ifndef FOUNDRY_SCRIPT_NO_LSP
			,
			workspace, lsp_parser, p_parse_results
#endif // FOUNDRY_SCRIPT_NO_LSP
	);
	for (const TypeAnnotationCandidate &candidate : candidates) {
		RefactorCandidate public_candidate;
		public_candidate.kind = RefactorKind::ADD_TYPE_ANNOTATION;
		public_candidate.enabled = candidate.enabled;
		public_candidate.disabled_reason = candidate.disabled_reason;
		public_candidate.declaration_kind = candidate.kind;
		public_candidate.line = candidate.line;
		public_candidate.column = candidate.caret_span_start; // Anchor at the start of the declaration span.
		if (candidate.enabled) {
			public_candidate.edits.push_back(candidate.edit);
		}
		result.candidates.push_back(public_candidate);
	}
	result.ok = true;
	return result;
}

// A located insert-explicit-cast opportunity. `caret_span` is the source range a caret
// must fall within for the candidate to apply (the whole declaration or return statement),
// while `edit` rewrites just the value expression into `value as TargetType`.
struct ExplicitCastCandidate {
	bool matched = false;
	bool enabled = false;
	String disabled_reason;
	int line = -1;
	RefactorLocation caret_span;
	RefactorTextEdit edit;
};

// Whether appending ` as T` to the expression text would re-associate against an operator
// of lower precedence than the cast, requiring the value to be wrapped in parentheses
// first. Primary expressions bind tighter than `as` and need none; anything else (binary,
// unary, ternary, await, type-test, lambda) is parenthesized to preserve meaning.
bool cast_expression_needs_parentheses(const FSParser::ExpressionNode *p_expression) {
	switch (p_expression->type) {
		case FSParser::Node::ARRAY:
		case FSParser::Node::CALL:
		case FSParser::Node::DICTIONARY:
		case FSParser::Node::GET_NODE:
		case FSParser::Node::IDENTIFIER:
		case FSParser::Node::LITERAL:
		case FSParser::Node::PRELOAD:
		case FSParser::Node::SELF:
		case FSParser::Node::SUBSCRIPT:
		case FSParser::Node::TUPLE_LITERAL:
			return false;
		default:
			return true;
	}
}

// Whether p_text has balanced (), [], and {} delimiters, ignoring any inside string
// literals. A complete single-line expression is always balanced; an unbalanced
// reconstruction means the node's source range dropped a surrounding grouping delimiter
// (parser grouping nodes do not include their own parentheses), so wrapping that text in a
// cast would emit syntactically invalid code.
// Whether `p_text` (a single source line's expression) opens and closes every grouping
// delimiter it contains. Used to refuse a cast whose recovered source dropped a surrounding
// delimiter (e.g. `(value)[0]` recovers as `value)[0]`), since wrapping it would emit invalid
// code. Tokenizing rather than hand-scanning means every FoundryScript string form -- single-line
// triple-quoted (`"""..."""`) and prefixed (`r"..."`, `&"..."`, `^"..."`) -- collapses to one
// LITERAL token, so quotes or brackets embedded in a string can never be mistaken for grouping.
bool expression_text_is_balanced(const String &p_text) {
	FSTokenizerText tokenizer;
	tokenizer.set_source_code(p_text);
	int depth = 0;
	FSTokenizer::Token token = tokenizer.scan();
	while (token.type != FSTokenizer::Token::TK_EOF) {
		switch (token.type) {
			case FSTokenizer::Token::ERROR:
				// An unterminated string or otherwise unscannable text can't be safely wrapped.
				return false;
			case FSTokenizer::Token::PARENTHESIS_OPEN:
			case FSTokenizer::Token::BRACKET_OPEN:
			case FSTokenizer::Token::BRACE_OPEN:
				depth++;
				break;
			case FSTokenizer::Token::PARENTHESIS_CLOSE:
			case FSTokenizer::Token::BRACKET_CLOSE:
			case FSTokenizer::Token::BRACE_CLOSE:
				depth--;
				if (depth < 0) {
					return false;
				}
				break;
			default:
				break;
		}
		token = tokenizer.scan();
	}
	return depth == 0;
}

// Build a cast candidate for `p_value`, the value expression of `p_statement`, targeting
// `p_target_type`. Reports the site as matched-but-disabled (with a reason) when the cast
// cannot be produced, so the editor can explain why the action is unavailable here. The
// cast is enabled only at a genuine dynamic boundary: a Variant value flowing into a known,
// renderable concrete type. Same-typed values are left alone so the edit never adds a
// redundant cast.
void build_explicit_cast_candidate(
		const Vector<String> &p_lines,
		const FSParser::Node *p_statement,
		const FSParser::ExpressionNode *p_value,
		const FSParser::DataType &p_target_type,
		ExplicitCastCandidate &r_candidate) {
	RefactorLocation statement_range;
	if (p_statement == nullptr || !get_node_text_range(p_lines, p_statement, statement_range)) {
		return;
	}
	r_candidate.matched = true;
	r_candidate.line = statement_range.start_line;
	r_candidate.caret_span = statement_range;

	if (p_value == nullptr) {
		r_candidate.disabled_reason = "There is no value here to cast.";
		return;
	}
	if (p_value->type == FSParser::Node::CAST) {
		r_candidate.disabled_reason = "This value is already cast.";
		return;
	}
	String rendered_type;
	if (!FSRefactorTypes::render_annotatable_type(p_target_type, rendered_type)) {
		r_candidate.disabled_reason = "Cannot insert a cast without a known target type.";
		return;
	}
	const FSParser::DataType value_type = p_value->get_datatype();
	if (!value_type.is_set() || value_type.kind != FSParser::DataType::VARIANT) {
		// Only a resolved Variant is a genuine dynamic boundary. Unresolved/resolving
		// types (also reported by is_variant()) are skipped so a parse gap never enables a
		// speculative cast.
		r_candidate.disabled_reason = "This value is already statically typed; no cast is needed.";
		return;
	}
	String expression_text;
	RefactorLocation expression_range;
	if (!get_single_line_node_text(p_lines, p_value, expression_text, &expression_range)) {
		r_candidate.disabled_reason = "Cannot cast a value that spans multiple lines.";
		return;
	}
	if (!expression_text_is_balanced(expression_text)) {
		// The recovered source dropped a surrounding grouping delimiter (e.g. `(value)[0]`),
		// so wrapping it would produce invalid code. Skip rather than corrupt the source.
		r_candidate.disabled_reason = "Cannot cast this value safely.";
		return;
	}
	const String wrapped = cast_expression_needs_parentheses(p_value)
			? "(" + expression_text + ") as " + rendered_type
			: expression_text + " as " + rendered_type;
	r_candidate.edit.start_line = expression_range.start_line;
	r_candidate.edit.start_column = expression_range.start_column;
	r_candidate.edit.end_line = expression_range.end_line;
	r_candidate.edit.end_column = expression_range.end_column;
	r_candidate.edit.new_text = wrapped;
	r_candidate.enabled = true;
}

// Records a cast candidate for `p_value` targeting `p_target_type`, anchored for caret
// matching at `p_caret_anchor`'s source range. The declaration and return boundaries anchor
// on the whole statement so a caret anywhere on the line applies; a call/emit argument anchors
// on the argument expression itself so the right argument is targeted when several share a line.
// Matched-but-disabled candidates are still recorded so the editor can explain why the action
// is unavailable at that site.
void record_explicit_cast_candidate(
		const Vector<String> &p_lines,
		const FSParser::Node *p_caret_anchor,
		const FSParser::ExpressionNode *p_value,
		const FSParser::DataType &p_target_type,
		Vector<ExplicitCastCandidate> &r_candidates) {
	ExplicitCastCandidate candidate;
	build_explicit_cast_candidate(p_lines, p_caret_anchor, p_value, p_target_type, candidate);
	if (candidate.matched) {
		r_candidates.push_back(candidate);
	}
}

void collect_cast_candidates_in_expression(const Vector<String> &p_lines, const FSParser::ExpressionNode *p_expression, Vector<ExplicitCastCandidate> &r_candidates);
void collect_cast_candidates_in_suite(const Vector<String> &p_lines, const FSParser::SuiteNode *p_suite, const FSParser::FunctionNode *p_function, Vector<ExplicitCastCandidate> &r_candidates);

// Walks a call's arguments, recording a cast candidate for each Variant argument flowing into
// a known typed parameter. The analyzer records the resolved parameter types on the call right
// before argument validation, so the same resolution that flags the boundary feeds the cast
// target here. Nested expressions inside each argument are walked too, so a call argument that
// is itself a call contributes its own boundaries.
void collect_cast_candidates_in_call(const Vector<String> &p_lines, const FSParser::CallNode *p_call, Vector<ExplicitCastCandidate> &r_candidates) {
	if (p_call == nullptr) {
		return;
	}
	collect_cast_candidates_in_expression(p_lines, p_call->callee, r_candidates);
#ifdef TOOLS_ENABLED
	const int resolved_count = p_call->resolved_parameter_types.size();
#else
	const int resolved_count = 0;
#endif // TOOLS_ENABLED
	for (int i = 0; i < p_call->arguments.size(); i++) {
		FSParser::ExpressionNode *argument = p_call->arguments[i];
		if (argument == nullptr) {
			continue;
		}
		collect_cast_candidates_in_expression(p_lines, argument, r_candidates);
#ifdef TOOLS_ENABLED
		if (i < resolved_count) {
			// Only a genuine boundary is surfaced: a Variant argument flowing into a known,
			// renderable parameter type. Variant-into-Variant and already-typed arguments are
			// not boundaries, so they are skipped rather than recorded as disabled noise (unlike
			// declaration/return sites, which the editor surfaces with an explanation).
			ExplicitCastCandidate candidate;
			build_explicit_cast_candidate(p_lines, argument, argument, p_call->resolved_parameter_types[i], candidate);
			if (candidate.matched && candidate.enabled) {
				r_candidates.push_back(candidate);
			}
		}
#endif // TOOLS_ENABLED
	}
}

// Recursively walks an expression, recording cast candidates at every call/emit argument
// boundary nested within it (e.g. a call inside a ternary, an array element, or another call's
// argument). Declaration initializers and return values are handled by their statement walkers;
// this only adds the argument boundaries reachable from an expression.
void collect_cast_candidates_in_expression(const Vector<String> &p_lines, const FSParser::ExpressionNode *p_expression, Vector<ExplicitCastCandidate> &r_candidates) {
	if (p_expression == nullptr) {
		return;
	}
	switch (p_expression->type) {
		case FSParser::Node::CALL:
			collect_cast_candidates_in_call(p_lines, static_cast<const FSParser::CallNode *>(p_expression), r_candidates);
			break;
		case FSParser::Node::ASSIGNMENT: {
			const FSParser::AssignmentNode *assignment = static_cast<const FSParser::AssignmentNode *>(p_expression);
			collect_cast_candidates_in_expression(p_lines, assignment->assignee, r_candidates);
			collect_cast_candidates_in_expression(p_lines, assignment->assigned_value, r_candidates);
		} break;
		case FSParser::Node::ARRAY: {
			const FSParser::ArrayNode *array = static_cast<const FSParser::ArrayNode *>(p_expression);
			for (const FSParser::ExpressionNode *element : array->elements) {
				collect_cast_candidates_in_expression(p_lines, element, r_candidates);
			}
		} break;
		case FSParser::Node::TUPLE_LITERAL: {
			const FSParser::TupleLiteralNode *tuple_literal = static_cast<const FSParser::TupleLiteralNode *>(p_expression);
			for (const FSParser::ExpressionNode *element : tuple_literal->elements) {
				collect_cast_candidates_in_expression(p_lines, element, r_candidates);
			}
		} break;
		case FSParser::Node::DICTIONARY: {
			const FSParser::DictionaryNode *dictionary = static_cast<const FSParser::DictionaryNode *>(p_expression);
			for (const FSParser::DictionaryNode::Pair &pair : dictionary->elements) {
				collect_cast_candidates_in_expression(p_lines, pair.key, r_candidates);
				collect_cast_candidates_in_expression(p_lines, pair.value, r_candidates);
			}
		} break;
		case FSParser::Node::BINARY_OPERATOR: {
			const FSParser::BinaryOpNode *binary = static_cast<const FSParser::BinaryOpNode *>(p_expression);
			collect_cast_candidates_in_expression(p_lines, binary->left_operand, r_candidates);
			collect_cast_candidates_in_expression(p_lines, binary->right_operand, r_candidates);
		} break;
		case FSParser::Node::UNARY_OPERATOR:
			collect_cast_candidates_in_expression(p_lines, static_cast<const FSParser::UnaryOpNode *>(p_expression)->operand, r_candidates);
			break;
		case FSParser::Node::TERNARY_OPERATOR: {
			const FSParser::TernaryOpNode *ternary = static_cast<const FSParser::TernaryOpNode *>(p_expression);
			collect_cast_candidates_in_expression(p_lines, ternary->condition, r_candidates);
			collect_cast_candidates_in_expression(p_lines, ternary->true_expr, r_candidates);
			collect_cast_candidates_in_expression(p_lines, ternary->false_expr, r_candidates);
		} break;
		case FSParser::Node::SUBSCRIPT: {
			const FSParser::SubscriptNode *subscript = static_cast<const FSParser::SubscriptNode *>(p_expression);
			collect_cast_candidates_in_expression(p_lines, subscript->base, r_candidates);
			if (!subscript->is_attribute) {
				collect_cast_candidates_in_expression(p_lines, subscript->index, r_candidates);
			}
		} break;
		case FSParser::Node::CAST:
			collect_cast_candidates_in_expression(p_lines, static_cast<const FSParser::CastNode *>(p_expression)->operand, r_candidates);
			break;
		case FSParser::Node::AWAIT:
			collect_cast_candidates_in_expression(p_lines, static_cast<const FSParser::AwaitNode *>(p_expression)->to_await, r_candidates);
			break;
		case FSParser::Node::TYPE_TEST:
			collect_cast_candidates_in_expression(p_lines, static_cast<const FSParser::TypeTestNode *>(p_expression)->operand, r_candidates);
			break;
		case FSParser::Node::LAMBDA: {
			// A lambda body is not a separate class member, so its boundaries are only reachable
			// by descending here. The lambda's own function supplies the return-type cast target.
			const FSParser::LambdaNode *lambda = static_cast<const FSParser::LambdaNode *>(p_expression);
			if (lambda->function != nullptr) {
				collect_cast_candidates_in_suite(p_lines, lambda->function->body, lambda->function, r_candidates);
			}
		} break;
		default:
			break;
	}
}

// Cast site for a typed `var x: T = value` declaration. Only explicitly annotated
// declarations carry a known cast target, so untyped ones are not surfaced here. The
// initializer expression is also walked for nested call-argument boundaries.
void collect_declaration_cast_candidates(const Vector<String> &p_lines, const FSParser::VariableNode *p_variable, Vector<ExplicitCastCandidate> &r_candidates) {
	if (p_variable == nullptr || p_variable->initializer == nullptr) {
		return;
	}
	if (p_variable->datatype_specifier != nullptr) {
		record_explicit_cast_candidate(p_lines, p_variable, p_variable->initializer, p_variable->get_datatype(), r_candidates);
	}
	collect_cast_candidates_in_expression(p_lines, p_variable->initializer, r_candidates);
}

// Cast site for a `return value` in a function with an explicit return type, which supplies
// the cast target. The returned expression is also walked for nested call-argument boundaries.
void collect_return_cast_candidates(const Vector<String> &p_lines, const FSParser::ReturnNode *p_return, const FSParser::FunctionNode *p_function, Vector<ExplicitCastCandidate> &r_candidates) {
	if (p_return == nullptr || p_return->return_value == nullptr) {
		return;
	}
	if (p_function != nullptr && p_function->return_type != nullptr) {
		record_explicit_cast_candidate(p_lines, p_return, p_return->return_value, p_function->get_datatype(), r_candidates);
	}
	collect_cast_candidates_in_expression(p_lines, p_return->return_value, r_candidates);
}

void collect_cast_candidates_in_suite(const Vector<String> &p_lines, const FSParser::SuiteNode *p_suite, const FSParser::FunctionNode *p_function, Vector<ExplicitCastCandidate> &r_candidates) {
	if (p_suite == nullptr) {
		return;
	}
	for (const FSParser::Node *statement : p_suite->statements) {
		if (statement == nullptr) {
			continue;
		}
		switch (statement->type) {
			case FSParser::Node::VARIABLE:
				collect_declaration_cast_candidates(p_lines, static_cast<const FSParser::VariableNode *>(statement), r_candidates);
				break;
			case FSParser::Node::RETURN:
				collect_return_cast_candidates(p_lines, static_cast<const FSParser::ReturnNode *>(statement), p_function, r_candidates);
				break;
			case FSParser::Node::IF: {
				const FSParser::IfNode *if_node = static_cast<const FSParser::IfNode *>(statement);
				collect_cast_candidates_in_expression(p_lines, if_node->condition, r_candidates);
				collect_cast_candidates_in_suite(p_lines, if_node->true_block, p_function, r_candidates);
				collect_cast_candidates_in_suite(p_lines, if_node->false_block, p_function, r_candidates);
			} break;
			case FSParser::Node::FOR: {
				const FSParser::ForNode *for_node = static_cast<const FSParser::ForNode *>(statement);
				collect_cast_candidates_in_expression(p_lines, for_node->list, r_candidates);
				collect_cast_candidates_in_suite(p_lines, for_node->loop, p_function, r_candidates);
			} break;
			case FSParser::Node::WHILE: {
				const FSParser::WhileNode *while_node = static_cast<const FSParser::WhileNode *>(statement);
				collect_cast_candidates_in_expression(p_lines, while_node->condition, r_candidates);
				collect_cast_candidates_in_suite(p_lines, while_node->loop, p_function, r_candidates);
			} break;
			case FSParser::Node::MATCH: {
				const FSParser::MatchNode *match_node = static_cast<const FSParser::MatchNode *>(statement);
				collect_cast_candidates_in_expression(p_lines, match_node->test, r_candidates);
				for (const FSParser::MatchBranchNode *branch : match_node->branches) {
					if (branch != nullptr) {
						collect_cast_candidates_in_suite(p_lines, branch->block, p_function, r_candidates);
					}
				}
			} break;
			default:
				if (statement->is_expression()) {
					collect_cast_candidates_in_expression(p_lines, static_cast<const FSParser::ExpressionNode *>(statement), r_candidates);
				}
				break;
		}
	}
}

void collect_cast_candidates_in_class(const Vector<String> &p_lines, const FSParser::ClassNode *p_class, Vector<ExplicitCastCandidate> &r_candidates) {
	if (p_class == nullptr) {
		return;
	}
	for (const FSParser::ClassNode::Member &member : p_class->members) {
		switch (member.type) {
			case FSParser::ClassNode::Member::VARIABLE:
				collect_declaration_cast_candidates(p_lines, member.variable, r_candidates);
				break;
			case FSParser::ClassNode::Member::FUNCTION:
				if (member.function != nullptr) {
					collect_cast_candidates_in_suite(p_lines, member.function->body, member.function, r_candidates);
				}
				break;
			case FSParser::ClassNode::Member::CLASS:
				collect_cast_candidates_in_class(p_lines, member.m_class, r_candidates);
				break;
			default:
				break;
		}
	}
}

ExplicitCastCandidate find_explicit_cast_candidate(const RefactorContext &p_context, const RefactorLocation &p_location) {
	ExplicitCastCandidate candidate;
	if (p_location.has_selection()) {
		candidate.disabled_reason = "Place the caret on a typed declaration's value, a return value, or a call argument.";
		return candidate;
	}

	FSParser parser;
	parser.parse(p_context.source, p_context.path, false);
	FSAnalyzer analyzer(&parser);
	analyzer.analyze();

	const Vector<String> lines = p_context.source.split("\n");
	const FSParser::ClassNode *tree = parser.get_tree();
	Vector<ExplicitCastCandidate> candidates;
	collect_cast_candidates_in_class(lines, tree, candidates);
	// A typed declaration/return statement spans its whole line, while a call argument inside it
	// spans only the argument; both can contain the caret. Pick the narrowest matching span so a
	// caret on a nested argument targets that argument rather than the enclosing statement.
	const ExplicitCastCandidate *best = nullptr;
	int best_span_width = 0;
	for (const ExplicitCastCandidate &found : candidates) {
		if (!caret_within_range(p_location, found.caret_span)) {
			continue;
		}
		const RefactorLocation &span = found.caret_span;
		const int width = span.start_line == span.end_line
				? span.end_column - span.start_column
				: (span.end_line - span.start_line) * INT16_MAX + span.end_column;
		if (best == nullptr || width < best_span_width) {
			best = &found;
			best_span_width = width;
		}
	}
	if (best != nullptr) {
		return *best;
	}

	candidate.matched = false;
	candidate.enabled = false;
	candidate.disabled_reason = "Place the caret on a typed declaration's value, a return value, or a call argument.";
	return candidate;
}

RefactorResult prepare_explicit_cast(const RefactorContext &p_context, const RefactorLocation &p_location) {
	RefactorResult result;
	const ExplicitCastCandidate candidate = find_explicit_cast_candidate(p_context, p_location);
	if (!candidate.enabled) {
		result.ok = false;
		result.error_message = candidate.disabled_reason.is_empty()
				? "Insert explicit cast is not available here."
				: candidate.disabled_reason;
		return result;
	}
	result.ok = true;
	result.edits.push_back(candidate.edit);
	return result;
}

// A located widen-to-nullable opportunity, the phase-2 satisfier for strict-null
// boundaries. `caret_span` is the source range a caret must fall within for the
// candidate to apply (the whole declaration or return statement), while `edit` is a
// zero-width insertion of `?` immediately after the boundary's type specifier, turning
// `T` into `T?`.
struct WidenToNullableCandidate {
	bool matched = false;
	bool enabled = false;
	String disabled_reason;
	int line = -1;
	RefactorLocation caret_span;
	RefactorTextEdit edit;
};

// Whether p_target is the `void` / no-value return type, which has no nullable form.
bool is_void_type(const FSParser::DataType &p_target) {
	return p_target.kind == FSParser::DataType::BUILTIN && p_target.builtin_type == Variant::NIL;
}

// Build a widen-to-nullable candidate for a non-nullable type specifier `p_type_node`
// that is fed a nullable `p_value`. Reports the site as matched-but-disabled (with a
// reason) when the widening cannot be produced, so the editor can explain why the action
// is unavailable here. The widening is the dual of the explicit cast: it leaves the value
// expression untouched and only makes the declared type admit the null the strict-null
// analyzer proved can already reach it.
//
// It is enabled when the value's underlying (non-nullable) type is assignment-compatible
// with the target, i.e. the only thing the boundary rejects is the value's nullability.
// This covers both the boundary that differs purely in nullability (`int?` -> `int`) and
// the boundaries that additionally rely on a covariant or implicit-numeric conversion the
// analyzer already accepts for the underlying types (`Button?` -> `Node`, `int?` -> `float`).
// A genuine underlying-type mismatch (e.g. `String?` -> `int`) is a separate, unrelated
// error that widening would not fix, and `void` returns have no nullable form, so both stay
// disabled to keep the action from emitting code that is still wrong or syntactically invalid.
void build_widen_to_nullable_candidate(
		const Vector<String> &p_lines,
		const FSParser::Node *p_statement,
		const FSParser::ExpressionNode *p_value,
		const FSParser::TypeNode *p_type_node,
		const FSParser::DataType &p_target_type,
		WidenToNullableCandidate &r_candidate) {
	RefactorLocation statement_range;
	if (p_statement == nullptr || !get_node_text_range(p_lines, p_statement, statement_range)) {
		return;
	}
	r_candidate.matched = true;
	r_candidate.line = statement_range.start_line;
	r_candidate.caret_span = statement_range;

	if (p_value == nullptr) {
		r_candidate.disabled_reason = "There is no value here to widen for.";
		return;
	}
	if (p_target_type.is_nullable) {
		r_candidate.disabled_reason = "This type is already nullable.";
		return;
	}
	if (!p_target_type.is_set() || p_target_type.is_variant()) {
		// A Variant boundary already admits null; widening to `Variant?` is meaningless.
		r_candidate.disabled_reason = "This type already accepts null.";
		return;
	}
	if (is_void_type(p_target_type)) {
		// `void` has no nullable form; `void?` is not valid syntax.
		r_candidate.disabled_reason = "A void return type cannot be made nullable.";
		return;
	}
	const FSParser::DataType value_type = p_value->get_datatype();
	if (!value_type.is_set() || !value_type.is_nullable) {
		// Only a resolved nullable value is a genuine strict-null boundary. Unresolved
		// types are skipped so a parse gap never enables a speculative widening.
		r_candidate.disabled_reason = "This value is not nullable; no widening is needed.";
		return;
	}
	// Require the value's underlying type to be assignment-compatible with the target so the
	// only thing the boundary rejects is the value's *top-level* nullability. Allowing the
	// implicit numeric conversion mirrors the analyzer's own assignment/return checks, so a
	// boundary the analyzer flagged solely for nullability (`Button?` -> `Node`, `int?` ->
	// `float`) becomes fixable, while a genuine underlying-type mismatch (e.g. a `String?` value
	// at an `int` boundary) is a separate type error that widening would leave unfixed and stays
	// disabled.
	//
	// Only the source's outer nullability is cleared, and the check keeps strict-null enabled so
	// a *nested* nullability mismatch still rejects the candidate: the edit only inserts a single
	// `?` after the outer type, so e.g. `Array[int?]?` reaching `Array[int]` would widen to
	// `Array[int]?` and leave the `int?` vs `int` element mismatch unfixed. That is not a boundary
	// rejected solely by top-level nullability, so it must remain disabled.
	FSParser::DataType value_underlying = value_type;
	value_underlying.is_nullable = false;
	FSTypeCompatibility::Options compatibility_options;
	compatibility_options.allow_implicit_conversion = true;
	compatibility_options.strict_null = true;
	if (!FSTypeCompatibility::check(p_target_type, value_underlying, compatibility_options).compatible) {
		r_candidate.disabled_reason = "This value's type does not match the target; widening would not fix the error.";
		return;
	}
	// Insert `?` at the end of the type specifier, a zero-width edit. The type node's end
	// must be single-line and within bounds for the insertion point to be well defined.
	RefactorLocation type_range;
	if (!get_node_text_range(p_lines, p_type_node, type_range) || type_range.start_line != type_range.end_line) {
		r_candidate.disabled_reason = "Cannot widen a type that spans multiple lines.";
		return;
	}
	r_candidate.edit.start_line = type_range.end_line;
	r_candidate.edit.start_column = type_range.end_column;
	r_candidate.edit.end_line = type_range.end_line;
	r_candidate.edit.end_column = type_range.end_column;
	r_candidate.edit.new_text = "?";
	r_candidate.enabled = true;
}

// Widen site for a typed `var x: T = value` declaration whose initializer is nullable.
bool find_declaration_widen_candidate(const Vector<String> &p_lines, const FSParser::VariableNode *p_variable, const RefactorLocation &p_location, WidenToNullableCandidate &r_candidate) {
	if (p_variable == nullptr || p_variable->datatype_specifier == nullptr || p_variable->initializer == nullptr) {
		return false;
	}
	WidenToNullableCandidate candidate;
	build_widen_to_nullable_candidate(p_lines, p_variable, p_variable->initializer, p_variable->datatype_specifier, p_variable->get_datatype(), candidate);
	if (!candidate.matched || !caret_within_range(p_location, candidate.caret_span)) {
		return false;
	}
	r_candidate = candidate;
	return true;
}

// Widen site for a `return value` whose value is nullable in a function with an explicit
// non-nullable return type.
bool find_return_widen_candidate(const Vector<String> &p_lines, const FSParser::ReturnNode *p_return, const FSParser::FunctionNode *p_function, const RefactorLocation &p_location, WidenToNullableCandidate &r_candidate) {
	if (p_return == nullptr || p_return->return_value == nullptr || p_function == nullptr || p_function->return_type == nullptr) {
		return false;
	}
	WidenToNullableCandidate candidate;
	build_widen_to_nullable_candidate(p_lines, p_return, p_return->return_value, p_function->return_type, p_function->get_datatype(), candidate);
	if (!candidate.matched || !caret_within_range(p_location, candidate.caret_span)) {
		return false;
	}
	r_candidate = candidate;
	return true;
}

bool find_widen_candidate_in_suite(const Vector<String> &p_lines, const FSParser::SuiteNode *p_suite, const FSParser::FunctionNode *p_function, const RefactorLocation &p_location, WidenToNullableCandidate &r_candidate) {
	if (p_suite == nullptr) {
		return false;
	}
	for (const FSParser::Node *statement : p_suite->statements) {
		if (statement == nullptr) {
			continue;
		}
		switch (statement->type) {
			case FSParser::Node::VARIABLE:
				if (find_declaration_widen_candidate(p_lines, static_cast<const FSParser::VariableNode *>(statement), p_location, r_candidate)) {
					return true;
				}
				break;
			case FSParser::Node::RETURN:
				if (find_return_widen_candidate(p_lines, static_cast<const FSParser::ReturnNode *>(statement), p_function, p_location, r_candidate)) {
					return true;
				}
				break;
			case FSParser::Node::IF: {
				const FSParser::IfNode *if_node = static_cast<const FSParser::IfNode *>(statement);
				if (find_widen_candidate_in_suite(p_lines, if_node->true_block, p_function, p_location, r_candidate) ||
						find_widen_candidate_in_suite(p_lines, if_node->false_block, p_function, p_location, r_candidate)) {
					return true;
				}
			} break;
			case FSParser::Node::FOR: {
				const FSParser::ForNode *for_node = static_cast<const FSParser::ForNode *>(statement);
				if (find_widen_candidate_in_suite(p_lines, for_node->loop, p_function, p_location, r_candidate)) {
					return true;
				}
			} break;
			case FSParser::Node::WHILE: {
				const FSParser::WhileNode *while_node = static_cast<const FSParser::WhileNode *>(statement);
				if (find_widen_candidate_in_suite(p_lines, while_node->loop, p_function, p_location, r_candidate)) {
					return true;
				}
			} break;
			case FSParser::Node::MATCH: {
				const FSParser::MatchNode *match_node = static_cast<const FSParser::MatchNode *>(statement);
				for (const FSParser::MatchBranchNode *branch : match_node->branches) {
					if (branch != nullptr && find_widen_candidate_in_suite(p_lines, branch->block, p_function, p_location, r_candidate)) {
						return true;
					}
				}
			} break;
			default:
				break;
		}
	}
	return false;
}

bool find_widen_candidate_in_class(const Vector<String> &p_lines, const FSParser::ClassNode *p_class, const RefactorLocation &p_location, WidenToNullableCandidate &r_candidate) {
	if (p_class == nullptr) {
		return false;
	}
	for (const FSParser::ClassNode::Member &member : p_class->members) {
		switch (member.type) {
			case FSParser::ClassNode::Member::VARIABLE:
				if (find_declaration_widen_candidate(p_lines, member.variable, p_location, r_candidate)) {
					return true;
				}
				break;
			case FSParser::ClassNode::Member::FUNCTION:
				if (member.function != nullptr && find_widen_candidate_in_suite(p_lines, member.function->body, member.function, p_location, r_candidate)) {
					return true;
				}
				break;
			case FSParser::ClassNode::Member::CLASS:
				if (find_widen_candidate_in_class(p_lines, member.m_class, p_location, r_candidate)) {
					return true;
				}
				break;
			default:
				break;
		}
	}
	return false;
}

WidenToNullableCandidate find_widen_to_nullable_candidate(const RefactorContext &p_context, const RefactorLocation &p_location) {
	WidenToNullableCandidate candidate;
	if (p_location.has_selection()) {
		candidate.disabled_reason = "Place the caret on a typed declaration or return that receives a nullable value.";
		return candidate;
	}

	FSParser parser;
	parser.parse(p_context.source, p_context.path, false);
	FSAnalyzer analyzer(&parser);
	analyzer.analyze();

	const Vector<String> lines = p_context.source.split("\n");
	const FSParser::ClassNode *tree = parser.get_tree();
	if (tree == nullptr || !find_widen_candidate_in_class(lines, tree, p_location, candidate)) {
		candidate.matched = false;
		candidate.enabled = false;
		candidate.disabled_reason = "Place the caret on a typed declaration or return that receives a nullable value.";
	}
	return candidate;
}

RefactorResult prepare_widen_to_nullable(const RefactorContext &p_context, const RefactorLocation &p_location) {
	RefactorResult result;
	const WidenToNullableCandidate candidate = find_widen_to_nullable_candidate(p_context, p_location);
	if (!candidate.enabled) {
		result.ok = false;
		result.error_message = candidate.disabled_reason.is_empty()
				? "Widen to nullable is not available here."
				: candidate.disabled_reason;
		return result;
	}
	result.ok = true;
	result.edits.push_back(candidate.edit);
	return result;
}

} // namespace

bool FSRefactoring::validate_extract_method_name(
		const Vector<String> &p_existing_member_names,
		const String &p_name,
		String &r_error_message) {
	if (!FSRefactorNames::validate_identifier(p_name, r_error_message)) {
		return false;
	}

	for (const String &member_name : p_existing_member_names) {
		if (member_name == p_name) {
			r_error_message = vformat("A member named '%s' already exists in this class.", p_name);
			return false;
		}
	}

	r_error_message = String();
	return true;
}

#ifndef FOUNDRY_SCRIPT_NO_LSP
static LSP::TextDocumentPositionParams make_document_position(const Ref<FSWorkspace> &p_workspace, const RefactorContext &p_context, const RefactorLocation &p_location) {
	LSP::TextDocumentPositionParams doc_position;
	doc_position.textDocument.uri = p_workspace->get_file_uri(p_context.path);
	doc_position.position.line = p_location.start_line;
	doc_position.position.character = p_location.start_column;
	return doc_position;
}

static bool is_string_literal_usage(
		const String &p_path,
		const LSP::Location &p_usage,
		const FSParseResultProvider &p_parse_results) {
	const ExtendFSParser *parser = p_parse_results.get_parse_result(p_path);
	if (!parser) {
		return false;
	}

	const PackedStringArray &lines = parser->get_lines();
	if (p_usage.range.start.line < 0 || p_usage.range.start.line >= lines.size()) {
		return false;
	}

	return is_column_inside_string_literal(lines[p_usage.range.start.line], p_usage.range.start.character);
}

static void collect_dynamic_string_references(
		const Ref<FSWorkspace> &p_workspace,
		const LSP::DocumentSymbol &p_symbol,
		const FSParseResultProvider &p_parse_results,
		RefactorResult &r_result) {
	List<String> paths;
	if (p_symbol.local) {
		// Object string-call APIs cannot reference local variables or parameters.
		return;
	}

	p_workspace->list_project_script_files(paths);

	for (const String &path : paths) {
		// A file whose raw text never mentions the symbol cannot reference it; skip the parse.
		if (!p_workspace->source_may_reference_symbol(path, p_symbol.name, &p_parse_results)) {
			continue;
		}
		const ExtendFSParser *parser = p_parse_results.get_parse_result(path);
		if (!parser) {
			continue;
		}

		const PackedStringArray &lines = parser->get_lines();
		for (int i = 0; i < lines.size(); i++) {
			const String &line = lines[i];
			const int column = find_dynamic_string_reference_column(line, p_symbol.name);
			if (column < 0) {
				continue;
			}

			RefactorUnresolvedReference unresolved;
			unresolved.path = path;
			unresolved.line = i;
			unresolved.column = column;
			unresolved.message = "String-based dynamic reference cannot be renamed automatically.";
			r_result.unresolved_references.push_back(unresolved);
		}
	}
}

static void collect_parse_error_textual_references(
		const Ref<FSWorkspace> &p_workspace,
		const LSP::DocumentSymbol &p_symbol,
		const FSParseResultProvider &p_parse_results,
		RefactorResult &r_result) {
	if (p_symbol.local) {
		return;
	}

	List<String> paths;
	p_workspace->list_project_script_files(paths);

	for (const String &path : paths) {
		// A file whose raw text never mentions the symbol cannot reference it; skip the parse.
		if (!p_workspace->source_may_reference_symbol(path, p_symbol.name, &p_parse_results)) {
			continue;
		}
		const ExtendFSParser *parser = p_parse_results.get_parse_result(path);
		if (parser == nullptr || parser->parse_result == OK) {
			continue;
		}

		const PackedStringArray &lines = parser->get_lines();
		for (int i = 0; i < lines.size(); i++) {
			const int column = lines[i].find(p_symbol.name);
			if (column < 0) {
				continue;
			}

			RefactorUnresolvedReference unresolved;
			unresolved.path = path;
			unresolved.line = i;
			unresolved.column = column;
			unresolved.message = "Script could not be parsed; verify references to this symbol after rename.";
			r_result.unresolved_references.push_back(unresolved);
			break;
		}
	}
}
#endif // FOUNDRY_SCRIPT_NO_LSP

static RefactorResult prepare_rename(
		const RefactorContext &p_context,
		const RefactorLocation &p_location,
		const RefactorParams &p_params,
		const FSParseResultProvider *p_parse_results) {
	RefactorResult result;

#ifdef FOUNDRY_SCRIPT_NO_LSP
	result.ok = false;
	result.error_message = "Rename requires the language server, which is not available in this build.";
	return result;
#else
	FSLanguageProtocol *protocol = FSLanguageProtocol::get_singleton();
	Ref<FSWorkspace> workspace = protocol ? protocol->get_workspace() : Ref<FSWorkspace>();
	if (workspace.is_null()) {
		result.ok = false;
		result.error_message = "Rename requires the language server, which is not available in this build.";
		return result;
	}

	if (p_parse_results == nullptr) {
		result.ok = false;
		result.error_message = "Rename requires the language server, which is not available in this build.";
		return result;
	}

	LSP::TextDocumentPositionParams doc_position = make_document_position(workspace, p_context, p_location);

	LSP::DocumentSymbol symbol;
	LSP::Range identifier_range;
	if (!workspace->can_rename(doc_position, symbol, identifier_range, p_parse_results)) {
		result.ok = false;
		result.error_message = "Cannot rename this symbol.";
		return result;
	}

	String reason;
	if (!FSRefactorNames::validate_identifier(p_params.new_name, reason)) {
		result.ok = false;
		result.error_message = reason;
		return result;
	}

	// `find_all_usages` matches usages by the address of the resolved symbol, so it must
	// receive the provider-owned symbol rather than the copy returned by `can_rename`.
	const LSP::DocumentSymbol *resolved_symbol = workspace->resolve_symbol(doc_position, "", false, p_parse_results);
	if (!resolved_symbol) {
		result.ok = false;
		result.error_message = "Cannot rename this symbol.";
		return result;
	}

	const ExtendFSParser *parser = p_parse_results->get_parse_result(p_context.path);
	if (parser) {
		String collision_reason;
		if (FSRefactorNames::has_scope_collision(parser->get_symbols(), resolved_symbol, p_params.new_name, collision_reason)) {
			result.ok = false;
			result.error_message = collision_reason;
			return result;
		}
	}

	// An @export variable's references can live outside scripts (the inspector,
	// scene/resource files), which FoundryScript symbol resolution cannot enumerate.
	// The LSP builds the symbol's `detail` with an "@export " prefix for exported
	// vars (see fs_extend_parser.cpp), so the prefix is a reliable signal here.
	const bool is_exported = resolved_symbol->detail.contains("@export ");

	const Vector<LSP::Location> usages = workspace->find_all_usages(*resolved_symbol, p_parse_results);
	for (const LSP::Location &usage : usages) {
		const String path = workspace->get_file_path(usage.uri);
		if (path.is_empty()) {
			result.ok = false;
			result.error_message = "Cannot resolve a rename target path.";
			return result;
		}
		if (is_string_literal_usage(path, usage, *p_parse_results)) {
			continue;
		}

		RefactorTextEdit edit;
		edit.start_line = usage.range.start.line;
		edit.start_column = usage.range.start.character;
		edit.end_line = usage.range.end.line;
		edit.end_column = usage.range.end.character;
		edit.has_expected_text = true;
		edit.expected_text = resolved_symbol->name;
		edit.new_text = p_params.new_name;

		RefactorFileEdit *file_edit = find_or_add_file_edit(result.file_edits, path);
		file_edit->edits.push_back(edit);
		if (path == p_context.path) {
			// The script editor applies rename through file_edits, but tests and
			// direct engine consumers still use edits for the active file.
			result.edits.push_back(edit);

			RefactorTextEdit occurrence = edit;
			occurrence.new_text = String();
			result.rename_occurrences.push_back(occurrence);
		}
	}

	if (result.file_edits.is_empty()) {
		result.ok = false;
		result.error_message = "No references found to rename.";
		return result;
	}

	const int parse_error_reference_count = result.unresolved_references.size();
	collect_parse_error_textual_references(workspace, *resolved_symbol, *p_parse_results, result);
	const bool has_parse_error_references = result.unresolved_references.size() > parse_error_reference_count;

	const int dynamic_reference_count = result.unresolved_references.size();
	collect_dynamic_string_references(workspace, *resolved_symbol, *p_parse_results, result);
	const bool has_dynamic_references = result.unresolved_references.size() > dynamic_reference_count;

	if (has_parse_error_references) {
		append_warning(result, "Some scripts could not be parsed; references in those files may need manual verification.");
	}
	if (has_dynamic_references) {
		append_warning(result, "Some string-based dynamic references to this symbol could not be resolved statically "
							   "and were not renamed.");
	}
	if (is_exported) {
		append_warning(result, "This is an exported variable; references outside scripts (such as in the inspector "
							   "or scene files) will not be updated.");
	}

	result.ok = true;
	result.suggested_name = symbol.name;
	result.rename_anchor_line = identifier_range.start.line;
	result.rename_anchor_column = identifier_range.start.character;
	return result;
#endif // FOUNDRY_SCRIPT_NO_LSP
}

constexpr int STYLE_ORDER_MAX_CONVERGENCE_PASSES = 16;

StyleOrderCandidate find_style_order_candidate_uncached(const RefactorContext &p_context) {
	StyleOrderCandidate candidate;
	const String original_source = p_context.source;
	String transformed_source = original_source;
	bool converged = false;

	for (int pass = 0; pass < STYLE_ORDER_MAX_CONVERGENCE_PASSES; pass++) {
		String pass_result;
		String disabled_reason;
		if (!apply_style_order_pass(transformed_source, p_context.path, pass_result, disabled_reason)) {
			candidate.disabled_reason = disabled_reason;
			return candidate;
		}
		if (pass_result == transformed_source) {
			converged = true;
			break;
		}
		transformed_source = pass_result;
	}

	if (!converged) {
		candidate.disabled_reason = "Cannot finish style-order sorting for this script.";
		return candidate;
	}

	if (transformed_source == original_source) {
		candidate.disabled_reason = "Members are already sorted by the FoundryScript style guide.";
		return candidate;
	}

	RefactorTextEdit edit;
	make_whole_source_style_order_edit(original_source, transformed_source, edit);
	candidate.enabled = true;
	candidate.edits.push_back(edit);
	return candidate;
}

StyleOrderCandidate find_style_order_candidate(const RefactorContext &p_context) {
	StyleOrderCandidate candidate;
	if (get_cached_style_order_candidate(p_context, candidate)) {
		return candidate;
	}

	candidate = find_style_order_candidate_uncached(p_context);
	cache_style_order_candidate(p_context, candidate);
	return candidate;
}

RefactorResult prepare_sort_members_by_style_guide(const RefactorContext &p_context) {
	RefactorResult result;
	const StyleOrderCandidate candidate = find_style_order_candidate(p_context);
	if (!candidate.enabled) {
		result.ok = false;
		result.error_message = candidate.disabled_reason;
		return result;
	}

	result.ok = true;
	for (const RefactorTextEdit &edit : candidate.edits) {
		result.edits.push_back(edit);
	}
	return result;
}

RefactorOverrideMethodsResult FSRefactoring::get_override_method_candidates(
		const RefactorContext &p_context,
		const RefactorLocation &p_location) {
#ifndef FOUNDRY_SCRIPT_NO_LSP
	RefactorParseResultProviderScope parse_results(p_context);
	const FSParseResultProvider *parse_result_provider = parse_results.get();
#else
	const FSParseResultProvider *parse_result_provider = nullptr;
#endif // FOUNDRY_SCRIPT_NO_LSP

	return find_override_method_candidates(p_context, p_location, parse_result_provider);
}

Vector<RefactorAvailability> FSRefactoring::get_available_refactors(const RefactorContext &p_context, const RefactorLocation &p_location) {
	Vector<RefactorAvailability> result;

#ifndef FOUNDRY_SCRIPT_NO_LSP
	RefactorParseResultProviderScope parse_results(p_context);
	const FSParseResultProvider *parse_result_provider = parse_results.get();
#else
	const FSParseResultProvider *parse_result_provider = nullptr;
#endif // FOUNDRY_SCRIPT_NO_LSP

	RefactorAvailability rename;
	rename.kind = RefactorKind::RENAME;
	rename.title = "Rename Symbol";

#ifdef FOUNDRY_SCRIPT_NO_LSP
	rename.enabled = false;
	rename.disabled_reason = "Rename requires the language server.";
#else
	FSLanguageProtocol *protocol = FSLanguageProtocol::get_singleton();
	Ref<FSWorkspace> workspace = protocol ? protocol->get_workspace() : Ref<FSWorkspace>();
	if (workspace.is_null()) {
		rename.enabled = false;
		rename.disabled_reason = "Rename requires the language server.";
	} else if (parse_result_provider == nullptr) {
		rename.enabled = false;
		rename.disabled_reason = "Rename requires the language server.";
	} else {
		LSP::TextDocumentPositionParams doc_position = make_document_position(workspace, p_context, p_location);
		LSP::DocumentSymbol symbol;
		LSP::Range identifier_range;
		rename.enabled = workspace->can_rename(doc_position, symbol, identifier_range, parse_result_provider);
		if (!rename.enabled) {
			rename.disabled_reason = "Place the caret on a renameable symbol.";
		}
	}
#endif // FOUNDRY_SCRIPT_NO_LSP

	result.push_back(rename);

	RefactorAvailability extract_variable;
	extract_variable.kind = RefactorKind::EXTRACT_VARIABLE;
	extract_variable.title = "Extract Variable";
	const ExtractVariableCandidate extract_candidate = find_extract_variable_candidate(p_context, p_location, parse_result_provider);
	extract_variable.enabled = extract_candidate.enabled;
	if (!extract_variable.enabled) {
		extract_variable.disabled_reason = extract_candidate.disabled_reason;
	}
	result.push_back(extract_variable);

	RefactorAvailability extract_method;
	extract_method.kind = RefactorKind::EXTRACT_METHOD;
	extract_method.title = "Extract Method";
	const ExtractMethodCandidate extract_method_candidate = find_extract_method_candidate(p_context, p_location, parse_result_provider);
	extract_method.enabled = extract_method_candidate.enabled;
	if (!extract_method.enabled) {
		extract_method.disabled_reason = extract_method_candidate.disabled_reason;
	}
	result.push_back(extract_method);

	RefactorAvailability add_type;
	add_type.kind = RefactorKind::ADD_TYPE_ANNOTATION;
	add_type.title = "Add Type Annotation";
	const TypeAnnotationCandidate type_candidate = find_type_annotation_candidate(p_context, p_location, parse_result_provider);
	add_type.enabled = type_candidate.enabled;
	if (!add_type.enabled) {
		add_type.disabled_reason = type_candidate.disabled_reason;
	}
	result.push_back(add_type);

	RefactorAvailability inline_variable;
	inline_variable.kind = RefactorKind::INLINE_VARIABLE;
	inline_variable.title = "Inline Variable";
	const InlineVariableCandidate inline_candidate = find_inline_variable_candidate(p_context, p_location, parse_result_provider);
	inline_variable.enabled = inline_candidate.enabled;
	if (!inline_variable.enabled) {
		inline_variable.disabled_reason = inline_candidate.disabled_reason;
	}
	result.push_back(inline_variable);

	RefactorAvailability implement_abstract;
	implement_abstract.kind = RefactorKind::IMPLEMENT_ABSTRACT_METHODS;
	implement_abstract.title = "Implement Abstract Methods";
	const ImplementAbstractCandidate implement_abstract_candidate = find_implement_abstract_candidate(p_context, p_location, parse_result_provider);
	implement_abstract.enabled = implement_abstract_candidate.enabled;
	if (!implement_abstract.enabled) {
		implement_abstract.disabled_reason = implement_abstract_candidate.disabled_reason;
	}
	result.push_back(implement_abstract);

	RefactorAvailability override_method;
	override_method.kind = RefactorKind::OVERRIDE_METHOD;
	override_method.title = "Override Method...";
	const RefactorOverrideMethodsResult override_candidates = find_override_method_candidates(p_context, p_location, parse_result_provider);
	override_method.enabled = override_candidates.ok && !override_candidates.candidates.is_empty();
	if (!override_method.enabled) {
		override_method.disabled_reason = !override_candidates.error_message.is_empty()
				? override_candidates.error_message
				: String("No overridable methods found.");
	}
	result.push_back(override_method);

	RefactorAvailability insert_cast;
	insert_cast.kind = RefactorKind::INSERT_EXPLICIT_CAST;
	insert_cast.title = "Insert Explicit Cast";
	const ExplicitCastCandidate cast_candidate = find_explicit_cast_candidate(p_context, p_location);
	insert_cast.enabled = cast_candidate.enabled;
	if (!insert_cast.enabled) {
		insert_cast.disabled_reason = cast_candidate.disabled_reason;
	}
	result.push_back(insert_cast);

	RefactorAvailability widen_nullable;
	widen_nullable.kind = RefactorKind::WIDEN_TO_NULLABLE;
	widen_nullable.title = "Widen to Nullable";
	const WidenToNullableCandidate widen_candidate = find_widen_to_nullable_candidate(p_context, p_location);
	widen_nullable.enabled = widen_candidate.enabled;
	if (!widen_nullable.enabled) {
		widen_nullable.disabled_reason = widen_candidate.disabled_reason;
	}
	result.push_back(widen_nullable);

	RefactorAvailability sort_members;
	sort_members.kind = RefactorKind::SORT_MEMBERS_BY_STYLE_GUIDE;
	sort_members.title = "Sort Members by Style Guide";
	const StyleOrderCandidate sort_candidate = find_style_order_candidate(p_context);
	sort_members.enabled = sort_candidate.enabled;
	if (!sort_members.enabled) {
		sort_members.disabled_reason = sort_candidate.disabled_reason;
	}
	result.push_back(sort_members);

	return result;
}

RefactorResult FSRefactoring::prepare(const RefactorContext &p_context, const RefactorLocation &p_location, RefactorKind p_kind, const RefactorParams &p_params) {
#ifndef FOUNDRY_SCRIPT_NO_LSP
	RefactorParseResultProviderScope parse_results(p_context);
	const FSParseResultProvider *parse_result_provider = parse_results.get();
#else
	const FSParseResultProvider *parse_result_provider = nullptr;
#endif // FOUNDRY_SCRIPT_NO_LSP

	RefactorResult result;
	switch (p_kind) {
		case RefactorKind::RENAME:
			result = prepare_rename(p_context, p_location, p_params, parse_result_provider);
			break;
		case RefactorKind::EXTRACT_VARIABLE:
			result = prepare_extract_variable(p_context, p_location, parse_result_provider);
			break;
		case RefactorKind::EXTRACT_METHOD:
			result = prepare_extract_method(p_context, p_location, p_params, parse_result_provider);
			break;
		case RefactorKind::ADD_TYPE_ANNOTATION:
			result = prepare_type_annotation(p_context, p_location, parse_result_provider);
			break;
		case RefactorKind::INLINE_VARIABLE:
			result = prepare_inline_variable(p_context, p_location, parse_result_provider);
			break;
		case RefactorKind::IMPLEMENT_ABSTRACT_METHODS:
			result = prepare_implement_abstract(p_context, p_location, parse_result_provider);
			break;
		case RefactorKind::OVERRIDE_METHOD:
			result = prepare_override_method(p_context, p_location, p_params, parse_result_provider);
			break;
		case RefactorKind::INSERT_EXPLICIT_CAST:
			result = prepare_explicit_cast(p_context, p_location);
			break;
		case RefactorKind::WIDEN_TO_NULLABLE:
			result = prepare_widen_to_nullable(p_context, p_location);
			break;
		case RefactorKind::SORT_MEMBERS_BY_STYLE_GUIDE:
			result = prepare_sort_members_by_style_guide(p_context);
			break;
		default:
			result.ok = false;
			result.error_message = "Refactor not implemented.";
			return result;
	}

	finalize_result(result);
	return result;
}

// Headless, caret-independent collection of every insert-explicit-cast opportunity in the
// file: one candidate per provable Variant boundary (typed declaration initializer, typed
// return value, and call/emit argument flowing into a known parameter type). Mirrors the Add
// Type Annotation collection path so the migration wizard can enumerate and batch-apply casts.
RefactorCandidatesResult collect_explicit_cast_candidates(const RefactorContext &p_context) {
	RefactorCandidatesResult result;

	FSParser parser;
	Error err = parser.parse(p_context.source, p_context.path, false);
	if (err == OK) {
		FSAnalyzer analyzer(&parser);
		err = analyzer.analyze();
	}
	if (err != OK) {
		result.error_message = "Cannot analyze this script.";
		return result;
	}

	const Vector<String> lines = p_context.source.split("\n");
	Vector<ExplicitCastCandidate> candidates;
	collect_cast_candidates_in_class(lines, parser.get_tree(), candidates);
	for (const ExplicitCastCandidate &candidate : candidates) {
		RefactorCandidate public_candidate;
		public_candidate.kind = RefactorKind::INSERT_EXPLICIT_CAST;
		public_candidate.enabled = candidate.enabled;
		public_candidate.disabled_reason = candidate.disabled_reason;
		public_candidate.line = candidate.line;
		public_candidate.column = candidate.caret_span.start_column;
		if (candidate.enabled) {
			public_candidate.edits.push_back(candidate.edit);
		}
		result.candidates.push_back(public_candidate);
	}
	result.ok = true;
	return result;
}

RefactorCandidatesResult FSRefactoring::find_candidates(const RefactorContext &p_context, RefactorKind p_kind) {
	if (p_kind == RefactorKind::INSERT_EXPLICIT_CAST) {
		RefactorCandidatesResult result = collect_explicit_cast_candidates(p_context);
		finalize_candidates(result);
		return result;
	}

	if (p_kind != RefactorKind::ADD_TYPE_ANNOTATION) {
		RefactorCandidatesResult result;
		result.error_message = "Headless candidate collection is not implemented for this refactor.";
		return result;
	}

#ifndef FOUNDRY_SCRIPT_NO_LSP
	RefactorParseResultProviderScope parse_results(p_context);
	const FSParseResultProvider *parse_result_provider = parse_results.get();
#else
	const FSParseResultProvider *parse_result_provider = nullptr;
#endif // FOUNDRY_SCRIPT_NO_LSP

	RefactorCandidatesResult result = collect_type_annotation_candidates(p_context, parse_result_provider);
	finalize_candidates(result);
	return result;
}

#endif // TOOLS_ENABLED
