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
// they never introduce a new element type and leave the program's observable
// behavior unchanged once the array is typed. Methods that take a `Callable`
// (e.g. `sort_custom`, `map`) are listed here because their callable argument is
// still scanned for escapes of the variable.
//
// Several families are deliberately excluded so the walker bails instead, because
// typing the array would change runtime behavior even though contents are not
// directly mutated to a new element type:
//   - `resize` grows the array with the element type's default (null on an
//     untyped array, but e.g. 0 on `Array[int]`), so typing would change values.
//   - typedness observers (`is_typed`, `get_typed_builtin`, `get_typed_class_name`,
//     `get_typed_script`, `is_same_typed`) report whether the array carries a
//     type; typing it would flip their results.
//   - value-validating reads (`has`, `find`, `rfind`, `count`, `erase`,
//     `bsearch`, `bsearch_custom`) coerce their value argument to the element
//     type on a typed array, so e.g. `[1].has(1.2)` flips from false to true.
//   - copy-returning methods (`duplicate`, `duplicate_deep`, `slice`, `filter`)
//     carry the typed flag onto the returned array; an alias of that copy would
//     reject a later mismatched element after typing. `map`/`reduce` return an
//     untyped result, so they stay safe.
bool is_safe_readonly_array_method(const StringName &p_name) {
	static const char *safe_methods[] = {
		"size", "is_empty", "clear", "sort", "sort_custom", "reverse", "shuffle",
		"pop_back", "pop_front", "pop_at", "remove_at",
		"rfind_custom", "find_custom", "back", "front", "max", "min", "hash",
		"pick_random", "is_read_only", "make_read_only", "get",
		"map", "reduce", "any", "all",
		nullptr
	};
	for (int i = 0; safe_methods[i] != nullptr; i++) {
		if (p_name == StringName(safe_methods[i])) {
			return true;
		}
	}
	return false;
}

// True when `p_expr` is a plain identifier that resolves to `p_decl`, whether as
// a local variable or, in member mode, as a member variable accessed through
// implicit `self` (a bare `member` reference).
bool identifier_refers_to(const GDScriptParser::ExpressionNode *p_expr, const GDScriptParser::VariableNode *p_decl, bool p_member_mode) {
	if (p_expr == nullptr || p_expr->type != Node::IDENTIFIER) {
		return false;
	}
	const GDScriptParser::IdentifierNode *identifier = static_cast<const GDScriptParser::IdentifierNode *>(p_expr);
	if (p_member_mode) {
		return identifier->source == GDScriptParser::IdentifierNode::MEMBER_VARIABLE && identifier->variable_source == p_decl;
	}
	return identifier->source == GDScriptParser::IdentifierNode::LOCAL_VARIABLE && identifier->variable_source == p_decl;
}

// In member mode, true when `p_subscript` is `self.<member>` for the tracked
// member, i.e. an explicit access through `self` that is equivalent to the bare
// member reference. Access through any other base (`other.<member>`) is *not*
// matched here so the walker can treat it as an unbounded foreign mutation.
bool self_attribute_refers_to(const GDScriptParser::SubscriptNode *p_subscript, const GDScriptParser::VariableNode *p_decl, bool p_member_mode) {
	if (!p_member_mode || p_subscript == nullptr || !p_subscript->is_attribute) {
		return false;
	}
	if (p_subscript->base == nullptr || p_subscript->base->type != Node::SELF) {
		return false;
	}
	if (p_subscript->attribute == nullptr || p_decl->identifier == nullptr) {
		return false;
	}
	return p_subscript->attribute->name == p_decl->identifier->name;
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
	explicit ElementInferenceWalker(const GDScriptParser::VariableNode *p_decl, bool p_member_mode = false) :
			decl(p_decl), member_mode(p_member_mode) {}

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
	bool member_mode = false;
	String element_rendered;

	bool is_our_var(const GDScriptParser::ExpressionNode *p_expr) const {
		return identifier_refers_to(p_expr, decl, member_mode);
	}

	bool is_self_member(const GDScriptParser::SubscriptNode *p_subscript) const {
		return self_attribute_refers_to(p_subscript, decl, member_mode);
	}

	// `is_self_member` for an expression that is expected to be a `self.<member>`
	// subscript, e.g. the base of `self.member[index]` or `self.member.append(x)`.
	bool is_self_member_subscript(const GDScriptParser::ExpressionNode *p_expr) const {
		return p_expr != nullptr && p_expr->type == Node::SUBSCRIPT &&
				is_self_member(static_cast<const GDScriptParser::SubscriptNode *>(p_expr));
	}

	// In member mode, detects an attribute access that names the tracked member
	// through a base that is not `self` (e.g. `other.member`). The analysis cannot
	// bound how that other reference mutates the member, so it bails. Returns true
	// (and bails) when the access is a foreign reference to the member.
	bool is_foreign_member_access(const GDScriptParser::SubscriptNode *p_subscript) {
		if (!member_mode || p_subscript == nullptr || !p_subscript->is_attribute) {
			return false;
		}
		if (p_subscript->attribute == nullptr || decl->identifier == nullptr) {
			return false;
		}
		if (p_subscript->attribute->name != decl->identifier->name) {
			return false;
		}
		if (p_subscript->base != nullptr && p_subscript->base->type == Node::SELF) {
			return false; // `self.member`, handled elsewhere.
		}
		bail(GDScriptContainerInference::ESCAPES, "the member is accessed through another reference the inference cannot bound");
		return true;
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
				if (!is_our_var_read_list(for_node->list)) {
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

	// True when `p_list` is a safe iteration over the tracked variable, covering
	// both a bare reference and (in member mode) `self.<member>`.
	bool is_our_var_read_list(const GDScriptParser::ExpressionNode *p_list) const {
		if (is_our_var(p_list)) {
			return true;
		}
		return p_list != nullptr && p_list->type == Node::SUBSCRIPT &&
				is_self_member(static_cast<const GDScriptParser::SubscriptNode *>(p_list));
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

	// Scans the base of an attribute access (`base.attr`). A `self` base is a
	// harmless read of another field/method on this instance, not an escape, so it
	// is not forwarded to `scan_value` (which would treat a bare `self` as escape).
	void scan_attribute_base(const GDScriptParser::ExpressionNode *p_base) {
		if (p_base != nullptr && p_base->type == Node::SELF) {
			return;
		}
		scan_value(p_base);
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
				if (is_self_member(subscript)) {
					// `self.member` used directly in a value position leaks a reference,
					// exactly like a bare reference to the variable.
					bail(GDScriptContainerInference::ESCAPES, "the member is used in a position the inference cannot bound");
					return;
				}
				if (is_foreign_member_access(subscript)) {
					return;
				}
				if (subscript->is_attribute) {
					if (is_our_var(subscript->base)) {
						bail(GDScriptContainerInference::ESCAPES, "the variable is accessed through an unsupported member reference");
						return;
					}
					scan_attribute_base(subscript->base);
				} else if (is_our_var(subscript->base) || is_self_member_subscript(subscript->base)) {
					// `our_var[index]` / `self.member[index]` read: safe; the index may
					// still reference the variable.
					scan_value(subscript->index);
				} else {
					scan_value(subscript->base);
					scan_value(subscript->index);
				}
			} break;
			case Node::SELF:
				// A bare `self` value (not the base of a `self.member` access, which is
				// handled above) hands out a reference to the instance, through which
				// external code could mutate the member; the analysis cannot bound that.
				if (member_mode) {
					bail(GDScriptContainerInference::ESCAPES, "the instance escapes through `self`");
				}
				break;
			case Node::CALL: {
				const GDScriptParser::CallNode *call = static_cast<const GDScriptParser::CallNode *>(p_value);
				if (call->callee != nullptr && call->callee->type == Node::SUBSCRIPT) {
					const GDScriptParser::SubscriptNode *callee = static_cast<const GDScriptParser::SubscriptNode *>(call->callee);
					if (callee->is_attribute && (is_our_var(callee->base) || is_self_member_subscript(callee->base))) {
						scan_method_on_var(call, callee->attribute != nullptr ? callee->attribute->name : StringName());
						return;
					}
					if (callee->is_attribute && callee->base != nullptr && callee->base->type == Node::SUBSCRIPT &&
							is_foreign_member_access(static_cast<const GDScriptParser::SubscriptNode *>(callee->base))) {
						return; // `other.member.append(x)`.
					}
					// `self.method(...)` is a call on this instance, not an escape of it;
					// scan only the arguments for leaks (handled below), not the `self` base.
					if (callee->is_attribute && callee->base != nullptr && callee->base->type == Node::SELF) {
						for (const GDScriptParser::ExpressionNode *argument : call->arguments) {
							scan_value(argument);
						}
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
		const bool whole_var_assignee = is_our_var(assignee) ||
				(assignee != nullptr && assignee->type == Node::SUBSCRIPT &&
						is_self_member(static_cast<const GDScriptParser::SubscriptNode *>(assignee)));
		if (whole_var_assignee) {
			// Whole-variable (re)assignment.
			if (p_assignment->operation == GDScriptParser::AssignmentNode::OP_NONE ||
					p_assignment->operation == GDScriptParser::AssignmentNode::OP_ADDITION) {
				// `our_var = [..]` or `our_var += [..]` (also `self.member = [..]`).
				contribute_from_array_value(p_assignment->assigned_value);
			} else {
				bail(GDScriptContainerInference::UNPROVABLE, "the array is mutated by an unsupported compound assignment");
			}
			scan_value(p_assignment->assigned_value);
			return;
		}
		if (assignee != nullptr && assignee->type == Node::SUBSCRIPT) {
			const GDScriptParser::SubscriptNode *subscript = static_cast<const GDScriptParser::SubscriptNode *>(assignee);
			// `other.member = value` foreign whole-member assignment.
			if (is_foreign_member_access(subscript)) {
				return;
			}
			// `other.member[index] = value` foreign element write.
			if (!subscript->is_attribute && subscript->base != nullptr && subscript->base->type == Node::SUBSCRIPT &&
					is_foreign_member_access(static_cast<const GDScriptParser::SubscriptNode *>(subscript->base))) {
				return;
			}
			if (!subscript->is_attribute && (is_our_var(subscript->base) || is_self_member_subscript(subscript->base))) {
				if (p_assignment->operation == GDScriptParser::AssignmentNode::OP_NONE) {
					// `our_var[index] = value`: the value's type is stored verbatim.
					if (p_assignment->assigned_value != nullptr) {
						contribute_element(p_assignment->assigned_value->get_datatype());
					}
				} else {
					// `our_var[index] op= value` stores `typeof(old_element op value)`,
					// which can differ from `typeof(value)` even when the value matches
					// the element type (e.g. `**=` can widen int to float), so the
					// result type is not safely knowable here. Skip conservatively.
					bail(GDScriptContainerInference::UNPROVABLE, "an array element is mutated by a compound assignment");
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

// Dictionary methods that are read-only or only remove existing entries, so they
// never introduce a new key or value type and leave observable behavior unchanged
// once the dictionary is typed.
//
// Families deliberately excluded so the walker bails instead, because typing the
// dictionary would change runtime behavior:
//   - typedness observers (`is_typed`, `is_typed_key`, `is_typed_value`,
//     `get_typed_*`, `is_same_typed*`) report whether the dictionary carries a
//     type; typing it would flip their results.
//   - value-validating reads (`has`, `has_all`, `erase`, `get`, `find_key`,
//     `recursive_equal`) coerce their key/value argument to the dictionary's
//     type on a typed dictionary, so e.g. `{1: 0}.has(1.2)` flips false to true.
//   - copy-returning methods (`duplicate`, `duplicate_deep`, `merged`) carry the
//     typed flag onto the returned dictionary; an alias of that copy would reject
//     a later mismatched entry after typing. `keys`/`values` likewise return a
//     typed `Array` whose alias would reject mismatches.
bool is_safe_readonly_dictionary_method(const StringName &p_name) {
	static const char *safe_methods[] = {
		"size", "is_empty", "clear", "sort", "hash",
		"is_read_only", "make_read_only",
		nullptr
	};
	for (int i = 0; safe_methods[i] != nullptr; i++) {
		if (p_name == StringName(safe_methods[i])) {
			return true;
		}
	}
	return false;
}

// Walks a function body accumulating the key and value types ever stored into one
// specific local dictionary variable, bailing the moment the variable escapes or
// is mutated in an unmodelled way. The soundness model mirrors the array walker:
// the union of key types and the union of value types are each accumulated
// flow-insensitively; if both unions are single concrete types every entry the
// dictionary can hold is `K -> V`, so `Dictionary[K, V]` is correct.
class DictionaryInferenceWalker {
public:
	explicit DictionaryInferenceWalker(const GDScriptParser::VariableNode *p_decl, bool p_member_mode = false) :
			decl(p_decl), member_mode(p_member_mode) {}

	bool bailed = false;
	GDScriptContainerInference::Outcome bail_outcome = GDScriptContainerInference::NOT_APPLICABLE;
	String bail_detail;

	bool has_key = false;
	bool has_value = false;
	DataType key_type;
	DataType value_type;

	void contribute_from_dictionary_value(const GDScriptParser::ExpressionNode *p_value) {
		if (bailed || p_value == nullptr) {
			return;
		}
		if (p_value->type == Node::DICTIONARY) {
			const GDScriptParser::DictionaryNode *dictionary = static_cast<const GDScriptParser::DictionaryNode *>(p_value);
			for (const GDScriptParser::DictionaryNode::Pair &pair : dictionary->elements) {
				if (pair.key != nullptr) {
					contribute_key(pair.key->get_datatype());
				}
				if (pair.value != nullptr) {
					contribute_value(pair.value->get_datatype());
				}
				if (bailed) {
					return;
				}
			}
			return;
		}
		const DataType value_type_local = p_value->get_datatype();
		if (value_type_local.kind == DataType::BUILTIN && value_type_local.builtin_type == Variant::DICTIONARY &&
				value_type_local.has_container_element_type(0) && value_type_local.has_container_element_type(1)) {
			contribute_key(value_type_local.get_container_element_type(0));
			contribute_value(value_type_local.get_container_element_type(1));
			return;
		}
		bail(GDScriptContainerInference::UNPROVABLE, "a dictionary value with an unknown key or value type is merged in");
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
	bool member_mode = false;
	String key_rendered;
	String value_rendered;

	bool is_our_var(const GDScriptParser::ExpressionNode *p_expr) const {
		return identifier_refers_to(p_expr, decl, member_mode);
	}

	bool is_self_member(const GDScriptParser::SubscriptNode *p_subscript) const {
		return self_attribute_refers_to(p_subscript, decl, member_mode);
	}

	bool is_self_member_subscript(const GDScriptParser::ExpressionNode *p_expr) const {
		return p_expr != nullptr && p_expr->type == Node::SUBSCRIPT &&
				is_self_member(static_cast<const GDScriptParser::SubscriptNode *>(p_expr));
	}

	bool is_our_var_read_list(const GDScriptParser::ExpressionNode *p_list) const {
		if (is_our_var(p_list)) {
			return true;
		}
		return p_list != nullptr && p_list->type == Node::SUBSCRIPT &&
				is_self_member(static_cast<const GDScriptParser::SubscriptNode *>(p_list));
	}

	// In member mode, detects and bails on an attribute access that names the
	// tracked member through a base other than `self` (e.g. `other.member`).
	bool is_foreign_member_access(const GDScriptParser::SubscriptNode *p_subscript) {
		if (!member_mode || p_subscript == nullptr || !p_subscript->is_attribute) {
			return false;
		}
		if (p_subscript->attribute == nullptr || decl->identifier == nullptr) {
			return false;
		}
		if (p_subscript->attribute->name != decl->identifier->name) {
			return false;
		}
		if (p_subscript->base != nullptr && p_subscript->base->type == Node::SELF) {
			return false; // `self.member`, handled elsewhere.
		}
		bail(GDScriptContainerInference::ESCAPES, "the member is accessed through another reference the inference cannot bound");
		return true;
	}

	void bail(GDScriptContainerInference::Outcome p_outcome, const String &p_detail) {
		if (bailed) {
			return;
		}
		bailed = true;
		bail_outcome = p_outcome;
		bail_detail = p_detail;
	}

	// Accumulates one observed type into the given slot (key or value), bailing on
	// a non-renderable type (UNPROVABLE) or a second, different concrete type (MIXED).
	void contribute_slot(const DataType &p_observed, bool &r_has, DataType &r_type, String &r_rendered, const char *p_slot) {
		if (bailed) {
			return;
		}
		DataType element = p_observed;
		element.is_constant = false;
		String rendered;
		if (!GDScriptRefactorTypes::render_annotatable_type(element, rendered)) {
			bail(GDScriptContainerInference::UNPROVABLE,
					vformat("a %s type (%s) cannot be written as an explicit annotation",
							p_slot, p_observed.is_set() ? p_observed.to_string() : String("Variant")));
			return;
		}
		if (!r_has) {
			r_has = true;
			r_type = element;
			r_rendered = rendered;
		} else if (rendered != r_rendered) {
			bail(GDScriptContainerInference::MIXED,
					vformat("the dictionary holds mixed %s types (%s and %s)", p_slot, r_rendered, rendered));
		}
	}

	void contribute_key(const DataType &p_observed) {
		contribute_slot(p_observed, has_key, key_type, key_rendered, "key");
	}

	void contribute_value(const DataType &p_observed) {
		contribute_slot(p_observed, has_value, value_type, value_rendered, "value");
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
				// `for key in our_var:` is a safe read of the variable.
				if (!is_our_var_read_list(for_node->list)) {
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

	// Scans the base of an attribute access; a `self` base is a harmless read on
	// this instance, not an escape, so it is not forwarded to `scan_value`.
	void scan_attribute_base(const GDScriptParser::ExpressionNode *p_base) {
		if (p_base != nullptr && p_base->type == Node::SELF) {
			return;
		}
		scan_value(p_base);
	}

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
				if (is_self_member(subscript)) {
					// `self.member` used directly in a value position leaks a reference.
					bail(GDScriptContainerInference::ESCAPES, "the member is used in a position the inference cannot bound");
					return;
				}
				if (is_foreign_member_access(subscript)) {
					return;
				}
				if (subscript->is_attribute) {
					if (is_our_var(subscript->base)) {
						bail(GDScriptContainerInference::ESCAPES, "the variable is accessed through an unsupported member reference");
						return;
					}
					scan_attribute_base(subscript->base);
				} else if (is_our_var(subscript->base) || is_self_member_subscript(subscript->base)) {
					// `our_var[key]` / `self.member[key]` read. A typed dictionary coerces
					// the key during lookup (`{1: "x"}[1.2]` misses on the untyped
					// dictionary but hits after typing as `Dictionary[int, String]`), so
					// the key type is a constraint: it must match the inferred key type for
					// the read to behave identically. Feeding it through the key accumulator
					// yields a conservative MIXED skip whenever it would diverge.
					if (subscript->index != nullptr) {
						contribute_key(subscript->index->get_datatype());
					}
					scan_value(subscript->index);
				} else {
					scan_value(subscript->base);
					scan_value(subscript->index);
				}
			} break;
			case Node::SELF:
				// A bare `self` value hands out a reference to the instance, through
				// which external code could mutate the member; cannot be bounded.
				if (member_mode) {
					bail(GDScriptContainerInference::ESCAPES, "the instance escapes through `self`");
				}
				break;
			case Node::CALL: {
				const GDScriptParser::CallNode *call = static_cast<const GDScriptParser::CallNode *>(p_value);
				if (call->callee != nullptr && call->callee->type == Node::SUBSCRIPT) {
					const GDScriptParser::SubscriptNode *callee = static_cast<const GDScriptParser::SubscriptNode *>(call->callee);
					if (callee->is_attribute && (is_our_var(callee->base) || is_self_member_subscript(callee->base))) {
						scan_method_on_var(call, callee->attribute != nullptr ? callee->attribute->name : StringName());
						return;
					}
					if (callee->is_attribute && callee->base != nullptr && callee->base->type == Node::SUBSCRIPT &&
							is_foreign_member_access(static_cast<const GDScriptParser::SubscriptNode *>(callee->base))) {
						return; // `other.member.set(k, v)`.
					}
					// `self.method(...)` is a call on this instance, not an escape of it;
					// scan only the arguments for leaks, not the `self` base.
					if (callee->is_attribute && callee->base != nullptr && callee->base->type == Node::SELF) {
						for (const GDScriptParser::ExpressionNode *argument : call->arguments) {
							scan_value(argument);
						}
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
				break;
		}
	}

	void scan_assignment(const GDScriptParser::AssignmentNode *p_assignment) {
		if (bailed || p_assignment == nullptr) {
			return;
		}
		const GDScriptParser::ExpressionNode *assignee = p_assignment->assignee;
		const bool whole_var_assignee = is_our_var(assignee) ||
				(assignee != nullptr && assignee->type == Node::SUBSCRIPT &&
						is_self_member(static_cast<const GDScriptParser::SubscriptNode *>(assignee)));
		if (whole_var_assignee) {
			// Whole-variable (re)assignment.
			if (p_assignment->operation == GDScriptParser::AssignmentNode::OP_NONE) {
				// `our_var = {..}` (also `self.member = {..}`).
				contribute_from_dictionary_value(p_assignment->assigned_value);
			} else {
				bail(GDScriptContainerInference::UNPROVABLE, "the dictionary is mutated by an unsupported compound assignment");
			}
			scan_value(p_assignment->assigned_value);
			return;
		}
		if (assignee != nullptr && assignee->type == Node::SUBSCRIPT) {
			const GDScriptParser::SubscriptNode *subscript = static_cast<const GDScriptParser::SubscriptNode *>(assignee);
			// `other.member = value` foreign whole-member assignment.
			if (is_foreign_member_access(subscript)) {
				return;
			}
			// `other.member[key] = value` foreign element write.
			if (!subscript->is_attribute && subscript->base != nullptr && subscript->base->type == Node::SUBSCRIPT &&
					is_foreign_member_access(static_cast<const GDScriptParser::SubscriptNode *>(subscript->base))) {
				return;
			}
			if (!subscript->is_attribute && (is_our_var(subscript->base) || is_self_member_subscript(subscript->base))) {
				if (p_assignment->operation == GDScriptParser::AssignmentNode::OP_NONE) {
					// `our_var[key] = value`: both the key and the value types are stored verbatim.
					if (subscript->index != nullptr) {
						contribute_key(subscript->index->get_datatype());
					}
					if (p_assignment->assigned_value != nullptr) {
						contribute_value(p_assignment->assigned_value->get_datatype());
					}
				} else {
					// `our_var[key] op= value` stores `typeof(old_value op value)`, which
					// can differ from `typeof(value)` even when types match (e.g. `**=`
					// can widen int to float), so the value type is not safely knowable.
					bail(GDScriptContainerInference::UNPROVABLE, "a dictionary value is mutated by a compound assignment");
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
		if (p_method == StringName("set") || p_method == StringName("get_or_add")) {
			// `set(key, value)` / `get_or_add(key, default)`: the key and the stored
			// value types are drawn directly from the observed arguments.
			contribute_arg_key(p_call, 0);
			contribute_arg_value(p_call, 1);
		} else if (p_method == StringName("merge") || p_method == StringName("assign")) {
			if (p_call->arguments.size() >= 1) {
				contribute_from_dictionary_value(p_call->arguments[0]);
			} else {
				bail(GDScriptContainerInference::UNPROVABLE, vformat("'%s' is called with an unexpected number of arguments", String(p_method)));
			}
		} else if (!is_safe_readonly_dictionary_method(p_method)) {
			bail(GDScriptContainerInference::UNPROVABLE, vformat("the dictionary is used through an unmodelled method '%s'", String(p_method)));
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

	void contribute_arg_key(const GDScriptParser::CallNode *p_call, int p_index) {
		if (p_call->arguments.size() <= p_index) {
			bail(GDScriptContainerInference::UNPROVABLE, "a dictionary mutation is called with an unexpected number of arguments");
			return;
		}
		const GDScriptParser::ExpressionNode *argument = p_call->arguments[p_index];
		if (argument != nullptr) {
			contribute_key(argument->get_datatype());
		}
	}

	void contribute_arg_value(const GDScriptParser::CallNode *p_call, int p_index) {
		if (p_call->arguments.size() <= p_index) {
			bail(GDScriptContainerInference::UNPROVABLE, "a dictionary mutation is called with an unexpected number of arguments");
			return;
		}
		const GDScriptParser::ExpressionNode *argument = p_call->arguments[p_index];
		if (argument != nullptr) {
			contribute_value(argument->get_datatype());
		}
	}
};

// Reports why a member variable is not a safe candidate for class-wide element
// inference, or an empty string when it is eligible. A member is only considered
// when its mutation surface is bounded by the class: it must not be `@export`ed
// (the editor and external code can assign it), must not be `static` (shared and
// assignable through the class), and must not have a custom setter/getter (writes
// flow through user code the inference does not model).
String member_disqualifier(const GDScriptParser::VariableNode *p_member) {
	if (p_member->exported) {
		return "the member is `@export`ed, so external code can assign it";
	}
	if (p_member->is_static) {
		return "the member is `static`, so it can be assigned through the class";
	}
	if (p_member->property != GDScriptParser::VariableNode::PROP_NONE) {
		return "the member has a custom setter or getter";
	}
	return String();
}

// Drives `p_walker` over every function body in `p_class` and, recursively, its
// nested classes. A member declared on `p_class` is reachable from any of these
// methods (and a nested subclass may mutate an inherited member), so the union
// must span all of them for the inference to be sound.
template <typename Walker>
void walk_class_methods(Walker &p_walker, const GDScriptParser::ClassNode *p_class) {
	if (p_class == nullptr) {
		return;
	}
	for (const GDScriptParser::ClassNode::Member &member : p_class->members) {
		if (p_walker.bailed) {
			return;
		}
		switch (member.type) {
			case GDScriptParser::ClassNode::Member::FUNCTION:
				if (member.function != nullptr) {
					p_walker.scan_suite(member.function->body);
				}
				break;
			case GDScriptParser::ClassNode::Member::CLASS:
				walk_class_methods(p_walker, member.m_class);
				break;
			default:
				break;
		}
	}
}

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

GDScriptContainerInference::Result GDScriptContainerInference::infer_local_dictionary_element_type(
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
	// The initializer must be a dictionary literal so the full initial contents are known.
	if (p_decl->initializer == nullptr || p_decl->initializer->type != GDScriptParser::Node::DICTIONARY) {
		return result;
	}
	const GDScriptParser::DataType declared = p_decl->get_datatype();
	if (declared.has_no_type() || declared.kind != GDScriptParser::DataType::BUILTIN || declared.builtin_type != Variant::DICTIONARY) {
		return result;
	}
	// Already a typed `Dictionary[K, V]`: the analyzer resolved both element types,
	// so the existing rendering path is sufficient.
	if (declared.has_container_element_type(0) && declared.has_container_element_type(1)) {
		return result;
	}

	DictionaryInferenceWalker walker(p_decl);
	walker.contribute_from_dictionary_value(p_decl->initializer);
	walker.scan_suite(p_function_body);

	if (walker.bailed) {
		result.outcome = walker.bail_outcome;
		result.detail = walker.bail_detail;
		return result;
	}
	// Both the key and the value type must be pinned to render `Dictionary[K, V]`;
	// partial evidence is left as a bare `Dictionary`.
	if (!walker.has_key || !walker.has_value) {
		result.outcome = NO_EVIDENCE;
		return result;
	}

	GDScriptParser::DataType inferred = declared;
	inferred.set_container_element_type(0, walker.key_type);
	inferred.set_container_element_type(1, walker.value_type);
	result.outcome = INFERRED;
	result.element_type = inferred;
	return result;
}

GDScriptContainerInference::Result GDScriptContainerInference::infer_member_array_element_type(
		const GDScriptParser::VariableNode *p_member,
		const GDScriptParser::ClassNode *p_class) {
	Result result;
	if (p_member == nullptr || p_class == nullptr) {
		return result; // NOT_APPLICABLE
	}
	// Only untyped declarations are candidates; an explicit annotation is left alone.
	if (p_member->datatype_specifier != nullptr) {
		return result;
	}
	// The initializer must be an array literal so the full initial contents are known.
	if (p_member->initializer == nullptr || p_member->initializer->type != GDScriptParser::Node::ARRAY) {
		return result;
	}
	const GDScriptParser::DataType declared = p_member->get_datatype();
	if (declared.has_no_type() || declared.kind != GDScriptParser::DataType::BUILTIN || declared.builtin_type != Variant::ARRAY) {
		return result;
	}
	// Already a typed `Array[T]`: the existing rendering path is sufficient.
	if (declared.has_container_element_type(0)) {
		return result;
	}
	// A member whose mutation surface is not bounded by the class is reported as
	// skipped rather than guessed.
	const String disqualifier = member_disqualifier(p_member);
	if (!disqualifier.is_empty()) {
		result.outcome = ESCAPES;
		result.detail = disqualifier;
		return result;
	}

	ElementInferenceWalker walker(p_member, /* member_mode */ true);
	walker.contribute_from_array_value(p_member->initializer);
	walk_class_methods(walker, p_class);

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

GDScriptContainerInference::Result GDScriptContainerInference::infer_member_dictionary_element_type(
		const GDScriptParser::VariableNode *p_member,
		const GDScriptParser::ClassNode *p_class) {
	Result result;
	if (p_member == nullptr || p_class == nullptr) {
		return result; // NOT_APPLICABLE
	}
	if (p_member->datatype_specifier != nullptr) {
		return result;
	}
	if (p_member->initializer == nullptr || p_member->initializer->type != GDScriptParser::Node::DICTIONARY) {
		return result;
	}
	const GDScriptParser::DataType declared = p_member->get_datatype();
	if (declared.has_no_type() || declared.kind != GDScriptParser::DataType::BUILTIN || declared.builtin_type != Variant::DICTIONARY) {
		return result;
	}
	if (declared.has_container_element_type(0) && declared.has_container_element_type(1)) {
		return result;
	}
	const String disqualifier = member_disqualifier(p_member);
	if (!disqualifier.is_empty()) {
		result.outcome = ESCAPES;
		result.detail = disqualifier;
		return result;
	}

	DictionaryInferenceWalker walker(p_member, /* member_mode */ true);
	walker.contribute_from_dictionary_value(p_member->initializer);
	walk_class_methods(walker, p_class);

	if (walker.bailed) {
		result.outcome = walker.bail_outcome;
		result.detail = walker.bail_detail;
		return result;
	}
	if (!walker.has_key || !walker.has_value) {
		result.outcome = NO_EVIDENCE;
		return result;
	}

	GDScriptParser::DataType inferred = declared;
	inferred.set_container_element_type(0, walker.key_type);
	inferred.set_container_element_type(1, walker.value_type);
	result.outcome = INFERRED;
	result.element_type = inferred;
	return result;
}

#endif // TOOLS_ENABLED
