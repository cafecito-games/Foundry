/**************************************************************************/
/*  gdscript_container_inference.cpp                                      */
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

#include "gdscript_container_inference.h"

#ifdef TOOLS_ENABLED

#include "gdscript_refactoring_types.h"

namespace {

using DataType = GDScriptParser::DataType;
using Node = GDScriptParser::Node;

// Array methods that are read-only or only remove/reorder existing elements, so
// they never introduce a new element type and are safe to ignore. Methods that
// take a `Callable` (e.g. `sort_custom`, `map`) are listed here because their
// callable argument is still scanned for escapes of the variable.
bool is_safe_readonly_array_method(const StringName &p_name) {
	static const char *safe_methods[] = {
		"size", "is_empty", "clear", "sort", "sort_custom", "reverse", "shuffle",
		"pop_back", "pop_front", "pop_at", "remove_at", "erase",
		"has", "find", "rfind", "count", "back", "front", "max", "min", "hash",
		"is_read_only", "make_read_only", "get", "slice", "duplicate", "duplicate_deep",
		"map", "filter", "reduce", "any", "all", "bsearch", "bsearch_custom", "find_custom",
		"is_typed", "get_typed_builtin", "get_typed_class_name", "get_typed_script", "is_same_typed",
		nullptr
	};
	for (int i = 0; safe_methods[i] != nullptr; i++) {
		if (p_name == StringName(safe_methods[i])) {
			return true;
		}
	}
	return false;
}

bool is_expression_node(Node::Type p_type) {
	switch (p_type) {
		case Node::ARRAY:
		case Node::ASSIGNMENT:
		case Node::AWAIT:
		case Node::BINARY_OPERATOR:
		case Node::CALL:
		case Node::CAST:
		case Node::DICTIONARY:
		case Node::GET_NODE:
		case Node::IDENTIFIER:
		case Node::LAMBDA:
		case Node::LITERAL:
		case Node::PRELOAD:
		case Node::SELF:
		case Node::SUBSCRIPT:
		case Node::TERNARY_OPERATOR:
		case Node::TYPE_TEST:
		case Node::UNARY_OPERATOR:
			return true;
		default:
			return false;
	}
}

// Walks a function body and accumulates the set of element types ever stored
// into one specific local array variable, bailing out conservatively the moment
// it sees the variable escape or be mutated in an unmodelled way.
//
// Soundness rests on accumulating the *union* of element types across the whole
// body (flow-insensitive): if that union is a single concrete type, every
// element the variable can ever hold is that type, so `Array[T]` is correct
// regardless of statement order. The only way to be wrong is to *miss* a
// mutation, so every construct that could add an element we cannot account for
// forces a skip.
class ElementInferenceWalker {
public:
	explicit ElementInferenceWalker(const GDScriptParser::VariableNode *p_decl) :
			decl(p_decl) {}

	bool bailed = false;
	GDScriptContainerInference::Outcome bail_outcome = GDScriptContainerInference::NOT_APPLICABLE;
	String bail_detail;

	bool has_element = false;
	DataType element_type;

	void contribute_from_array_value(const GDScriptParser::ExpressionNode *p_value) {
		if (bailed || p_value == nullptr) {
			return;
		}
		if (p_value->type == Node::ARRAY) {
			const GDScriptParser::ArrayNode *array = static_cast<const GDScriptParser::ArrayNode *>(p_value);
			for (const GDScriptParser::ExpressionNode *element : array->elements) {
				if (element != nullptr) {
					contribute_element(element->get_datatype());
				}
				if (bailed) {
					return;
				}
			}
			return;
		}
		const DataType value_type = p_value->get_datatype();
		if (value_type.kind == DataType::BUILTIN && value_type.builtin_type == Variant::ARRAY && value_type.has_container_element_type(0)) {
			contribute_element(value_type.get_container_element_type(0));
			return;
		}
		bail(GDScriptContainerInference::UNPROVABLE, "an array value with an unknown element type is merged in");
	}

	void scan_suite(const GDScriptParser::SuiteNode *p_suite) {
		if (bailed || p_suite == nullptr) {
			return;
		}
		for (const Node *statement : p_suite->statements) {
			scan_statement(statement);
			if (bailed) {
				return;
			}
		}
	}

private:
	const GDScriptParser::VariableNode *decl = nullptr;
	String element_rendered;

	bool is_our_var(const GDScriptParser::ExpressionNode *p_expr) const {
		if (p_expr == nullptr || p_expr->type != Node::IDENTIFIER) {
			return false;
		}
		const GDScriptParser::IdentifierNode *identifier = static_cast<const GDScriptParser::IdentifierNode *>(p_expr);
		return identifier->source == GDScriptParser::IdentifierNode::LOCAL_VARIABLE && identifier->variable_source == decl;
	}

	void bail(GDScriptContainerInference::Outcome p_outcome, const String &p_detail) {
		if (bailed) {
			return;
		}
		bailed = true;
		bail_outcome = p_outcome;
		bail_detail = p_detail;
	}

	void contribute_element(const DataType &p_element) {
		if (bailed) {
			return;
		}
		DataType element = p_element;
		// An element annotation does not carry the literal's const-ness.
		element.is_constant = false;
		String rendered;
		if (!GDScriptRefactorTypes::render_annotatable_type(element, rendered)) {
			bail(GDScriptContainerInference::UNPROVABLE,
					vformat("an element type (%s) cannot be written as an explicit annotation",
							p_element.is_set() ? p_element.to_string() : String("Variant")));
			return;
		}
		if (!has_element) {
			has_element = true;
			element_type = element;
			element_rendered = rendered;
		} else if (rendered != element_rendered) {
			bail(GDScriptContainerInference::MIXED,
					vformat("the array holds mixed element types (%s and %s)", element_rendered, rendered));
		}
	}

	void scan_statement(const Node *p_statement) {
		if (bailed || p_statement == nullptr) {
			return;
		}
		switch (p_statement->type) {
			case Node::VARIABLE: {
				const GDScriptParser::VariableNode *variable = static_cast<const GDScriptParser::VariableNode *>(p_statement);
				if (variable == decl) {
					return; // Our own declaration; its initializer is accounted for separately.
				}
				scan_value(variable->initializer); // Catches `var alias = our_var`.
			} break;
			case Node::CONSTANT: {
				const GDScriptParser::ConstantNode *constant = static_cast<const GDScriptParser::ConstantNode *>(p_statement);
				scan_value(constant->initializer);
			} break;
			case Node::IF: {
				const GDScriptParser::IfNode *if_node = static_cast<const GDScriptParser::IfNode *>(p_statement);
				scan_value(if_node->condition);
				scan_suite(if_node->true_block);
				scan_suite(if_node->false_block);
			} break;
			case Node::FOR: {
				const GDScriptParser::ForNode *for_node = static_cast<const GDScriptParser::ForNode *>(p_statement);
				// `for element in our_var:` is a safe read of the variable.
				if (!is_our_var(for_node->list)) {
					scan_value(for_node->list);
				}
				scan_suite(for_node->loop);
			} break;
			case Node::WHILE: {
				const GDScriptParser::WhileNode *while_node = static_cast<const GDScriptParser::WhileNode *>(p_statement);
				scan_value(while_node->condition);
				scan_suite(while_node->loop);
			} break;
			case Node::MATCH: {
				const GDScriptParser::MatchNode *match_node = static_cast<const GDScriptParser::MatchNode *>(p_statement);
				scan_value(match_node->test);
				for (const GDScriptParser::MatchBranchNode *branch : match_node->branches) {
					scan_match_branch(branch);
					if (bailed) {
						return;
					}
				}
			} break;
			case Node::RETURN: {
				const GDScriptParser::ReturnNode *return_node = static_cast<const GDScriptParser::ReturnNode *>(p_statement);
				scan_value(return_node->return_value); // `return our_var` escapes.
			} break;
			case Node::ASSERT: {
				const GDScriptParser::AssertNode *assert_node = static_cast<const GDScriptParser::AssertNode *>(p_statement);
				scan_value(assert_node->condition);
				scan_value(assert_node->message);
			} break;
			case Node::PASS:
			case Node::BREAK:
			case Node::CONTINUE:
			case Node::BREAKPOINT:
			case Node::ANNOTATION:
				break;
			default:
				if (is_expression_node(p_statement->type)) {
					scan_value(static_cast<const GDScriptParser::ExpressionNode *>(p_statement));
				}
				break;
		}
	}

	void scan_match_branch(const GDScriptParser::MatchBranchNode *p_branch) {
		if (bailed || p_branch == nullptr) {
			return;
		}
		for (const GDScriptParser::PatternNode *pattern : p_branch->patterns) {
			scan_pattern(pattern);
			if (bailed) {
				return;
			}
		}
		scan_suite(p_branch->guard_body);
		scan_suite(p_branch->block);
	}

	void scan_pattern(const GDScriptParser::PatternNode *p_pattern) {
		if (bailed || p_pattern == nullptr) {
			return;
		}
		switch (p_pattern->pattern_type) {
			case GDScriptParser::PatternNode::PT_EXPRESSION:
				scan_value(p_pattern->expression);
				break;
			case GDScriptParser::PatternNode::PT_ARRAY:
				for (const GDScriptParser::PatternNode *sub : p_pattern->array) {
					scan_pattern(sub);
					if (bailed) {
						return;
					}
				}
				break;
			case GDScriptParser::PatternNode::PT_DICTIONARY:
				for (const GDScriptParser::PatternNode::Pair &pair : p_pattern->dictionary) {
					scan_value(pair.key);
					scan_pattern(pair.value_pattern);
					if (bailed) {
						return;
					}
				}
				break;
			default:
				break;
		}
	}

	// Treats `p_value` as a value that is consumed by the surrounding context. If
	// the variable itself appears here (other than in one of the recognized safe
	// read positions handled below) it has escaped and inference must stop.
	void scan_value(const GDScriptParser::ExpressionNode *p_value) {
		if (bailed || p_value == nullptr) {
			return;
		}
		if (is_our_var(p_value)) {
			bail(GDScriptContainerInference::ESCAPES, "the variable is used in a position the inference cannot bound");
			return;
		}
		switch (p_value->type) {
			case Node::SUBSCRIPT: {
				const GDScriptParser::SubscriptNode *subscript = static_cast<const GDScriptParser::SubscriptNode *>(p_value);
				if (subscript->is_attribute) {
					if (is_our_var(subscript->base)) {
						bail(GDScriptContainerInference::ESCAPES, "the variable is accessed through an unsupported member reference");
						return;
					}
					scan_value(subscript->base);
				} else if (is_our_var(subscript->base)) {
					// `our_var[index]` read: safe; the index may still reference the variable.
					scan_value(subscript->index);
				} else {
					scan_value(subscript->base);
					scan_value(subscript->index);
				}
			} break;
			case Node::CALL: {
				const GDScriptParser::CallNode *call = static_cast<const GDScriptParser::CallNode *>(p_value);
				if (call->callee != nullptr && call->callee->type == Node::SUBSCRIPT) {
					const GDScriptParser::SubscriptNode *callee = static_cast<const GDScriptParser::SubscriptNode *>(call->callee);
					if (callee->is_attribute && is_our_var(callee->base)) {
						scan_method_on_var(call, callee->attribute != nullptr ? callee->attribute->name : StringName());
						return;
					}
				}
				scan_value(call->callee);
				for (const GDScriptParser::ExpressionNode *argument : call->arguments) {
					scan_value(argument);
				}
			} break;
			case Node::ASSIGNMENT:
				scan_assignment(static_cast<const GDScriptParser::AssignmentNode *>(p_value));
				break;
			case Node::ARRAY: {
				const GDScriptParser::ArrayNode *array = static_cast<const GDScriptParser::ArrayNode *>(p_value);
				for (const GDScriptParser::ExpressionNode *element : array->elements) {
					scan_value(element);
				}
			} break;
			case Node::DICTIONARY: {
				const GDScriptParser::DictionaryNode *dictionary = static_cast<const GDScriptParser::DictionaryNode *>(p_value);
				for (const GDScriptParser::DictionaryNode::Pair &pair : dictionary->elements) {
					scan_value(pair.key);
					scan_value(pair.value);
				}
			} break;
			case Node::LAMBDA: {
				const GDScriptParser::LambdaNode *lambda = static_cast<const GDScriptParser::LambdaNode *>(p_value);
				for (const GDScriptParser::IdentifierNode *capture : lambda->captures) {
					if (capture != nullptr && (is_our_var(capture) || (decl->identifier != nullptr && capture->name == decl->identifier->name))) {
						bail(GDScriptContainerInference::ESCAPES, "the variable is captured by a lambda");
						return;
					}
				}
			} break;
			case Node::BINARY_OPERATOR: {
				const GDScriptParser::BinaryOpNode *binary = static_cast<const GDScriptParser::BinaryOpNode *>(p_value);
				scan_value(binary->left_operand);
				scan_value(binary->right_operand);
			} break;
			case Node::UNARY_OPERATOR: {
				const GDScriptParser::UnaryOpNode *unary = static_cast<const GDScriptParser::UnaryOpNode *>(p_value);
				scan_value(unary->operand);
			} break;
			case Node::TERNARY_OPERATOR: {
				const GDScriptParser::TernaryOpNode *ternary = static_cast<const GDScriptParser::TernaryOpNode *>(p_value);
				scan_value(ternary->condition);
				scan_value(ternary->true_expr);
				scan_value(ternary->false_expr);
			} break;
			case Node::CAST: {
				const GDScriptParser::CastNode *cast = static_cast<const GDScriptParser::CastNode *>(p_value);
				scan_value(cast->operand);
			} break;
			case Node::TYPE_TEST: {
				const GDScriptParser::TypeTestNode *type_test = static_cast<const GDScriptParser::TypeTestNode *>(p_value);
				scan_value(type_test->operand);
			} break;
			case Node::AWAIT: {
				const GDScriptParser::AwaitNode *await = static_cast<const GDScriptParser::AwaitNode *>(p_value);
				scan_value(await->to_await);
			} break;
			case Node::PRELOAD: {
				const GDScriptParser::PreloadNode *preload = static_cast<const GDScriptParser::PreloadNode *>(p_value);
				scan_value(preload->path);
			} break;
			default:
				// Remaining expression kinds (literals, `self`, `$node`, plain
				// identifiers that are not our variable) cannot reference it.
				break;
		}
	}

	void scan_assignment(const GDScriptParser::AssignmentNode *p_assignment) {
		if (bailed || p_assignment == nullptr) {
			return;
		}
		const GDScriptParser::ExpressionNode *assignee = p_assignment->assignee;
		if (is_our_var(assignee)) {
			// Whole-variable (re)assignment.
			if (p_assignment->operation == GDScriptParser::AssignmentNode::OP_NONE ||
					p_assignment->operation == GDScriptParser::AssignmentNode::OP_ADDITION) {
				// `our_var = [..]` or `our_var += [..]`.
				contribute_from_array_value(p_assignment->assigned_value);
			} else {
				bail(GDScriptContainerInference::UNPROVABLE, "the array is mutated by an unsupported compound assignment");
			}
			scan_value(p_assignment->assigned_value);
			return;
		}
		if (assignee != nullptr && assignee->type == Node::SUBSCRIPT) {
			const GDScriptParser::SubscriptNode *subscript = static_cast<const GDScriptParser::SubscriptNode *>(assignee);
			if (!subscript->is_attribute && is_our_var(subscript->base)) {
				// `our_var[index] = value` (plain or compound): the value becomes an element.
				if (p_assignment->assigned_value != nullptr) {
					contribute_element(p_assignment->assigned_value->get_datatype());
				}
				scan_value(subscript->index);
				scan_value(p_assignment->assigned_value);
				return;
			}
		}
		// Assignment to some other target. Both sides are values: the variable
		// appearing on either side is an escape (e.g. `alias = our_var`).
		scan_value(assignee);
		scan_value(p_assignment->assigned_value);
	}

	void scan_method_on_var(const GDScriptParser::CallNode *p_call, const StringName &p_method) {
		if (bailed) {
			return;
		}
		if (p_method == StringName("append") || p_method == StringName("push_back") || p_method == StringName("push_front") || p_method == StringName("fill")) {
			contribute_arg_element(p_call, 0);
		} else if (p_method == StringName("insert") || p_method == StringName("set")) {
			contribute_arg_element(p_call, 1);
		} else if (p_method == StringName("append_array") || p_method == StringName("assign")) {
			if (p_call->arguments.size() >= 1) {
				contribute_from_array_value(p_call->arguments[0]);
			} else {
				bail(GDScriptContainerInference::UNPROVABLE, vformat("'%s' is called with an unexpected number of arguments", String(p_method)));
			}
		} else if (!is_safe_readonly_array_method(p_method)) {
			bail(GDScriptContainerInference::UNPROVABLE, vformat("the array is used through an unmodelled method '%s'", String(p_method)));
		}
		if (bailed) {
			return;
		}
		// Even for modeled methods, arguments may themselves leak the variable.
		for (const GDScriptParser::ExpressionNode *argument : p_call->arguments) {
			scan_value(argument);
			if (bailed) {
				return;
			}
		}
	}

	void contribute_arg_element(const GDScriptParser::CallNode *p_call, int p_index) {
		if (p_call->arguments.size() <= p_index) {
			bail(GDScriptContainerInference::UNPROVABLE, "an array mutation is called with an unexpected number of arguments");
			return;
		}
		const GDScriptParser::ExpressionNode *argument = p_call->arguments[p_index];
		if (argument != nullptr) {
			contribute_element(argument->get_datatype());
		}
	}
};

} // namespace

GDScriptContainerInference::Result GDScriptContainerInference::infer_local_array_element_type(
		const GDScriptParser::VariableNode *p_decl,
		const GDScriptParser::SuiteNode *p_function_body) {
	Result result;
	if (p_decl == nullptr || p_function_body == nullptr) {
		return result; // NOT_APPLICABLE
	}
	// Only untyped declarations are candidates; an explicit annotation is left alone.
	if (p_decl->datatype_specifier != nullptr) {
		return result;
	}
	// The initializer must be an array literal so the full initial contents are known.
	if (p_decl->initializer == nullptr || p_decl->initializer->type != GDScriptParser::Node::ARRAY) {
		return result;
	}
	const GDScriptParser::DataType declared = p_decl->get_datatype();
	if (declared.has_no_type() || declared.kind != GDScriptParser::DataType::BUILTIN || declared.builtin_type != Variant::ARRAY) {
		return result;
	}
	// Already a typed `Array[T]`: the analyzer resolved the element type, so the
	// existing rendering path is sufficient.
	if (declared.has_container_element_type(0)) {
		return result;
	}

	ElementInferenceWalker walker(p_decl);
	walker.contribute_from_array_value(p_decl->initializer);
	walker.scan_suite(p_function_body);

	if (walker.bailed) {
		result.outcome = walker.bail_outcome;
		result.detail = walker.bail_detail;
		return result;
	}
	if (!walker.has_element) {
		result.outcome = NO_EVIDENCE;
		return result;
	}

	GDScriptParser::DataType inferred = declared;
	inferred.set_container_element_type(0, walker.element_type);
	result.outcome = INFERRED;
	result.element_type = inferred;
	return result;
}

#endif // TOOLS_ENABLED
