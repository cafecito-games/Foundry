/**************************************************************************/
/*  fs_container_inference.cpp                                            */
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

#include "fs_container_inference.h"

#ifdef TOOLS_ENABLED

#include "fs_refactoring_types.h"

#include "../fs_utility_functions.h"

#include "core/string/node_path.h"
#include "core/templates/hash_map.h"
#include "core/variant/variant.h"

namespace {

// A bare call name that is a built-in language/Variant utility (e.g. `print`,
// `len`, `char`) dispatches to engine code, not an overridable script method,
// so it cannot mutate a script-defined member.
bool is_global_utility_call(const StringName &p_name) {
	return FSUtilityFunctions::function_exists(p_name) || Variant::has_utility_function(p_name);
}

using DataType = FSParser::DataType;
using Node = FSParser::Node;

// Array methods that validate a value argument against the element type: on a
// typed array the argument is coerced to the element type before the comparison
// (`Array[int].has(1.2)` coerces `1.2` to `1`), so the call is only
// behavior-preserving when the argument already has the element type. The value
// being validated is always the first argument.
//
// `bsearch_custom` is excluded: it takes a `Callable` comparator and a value, and
// its semantics under typing are not modeled here, so it keeps bailing.
bool is_value_validating_array_method(const StringName &p_name) {
	static const char *methods[] = {
		"has", "find", "rfind", "count", "erase", "bsearch",
		nullptr
	};
	for (int i = 0; methods[i] != nullptr; i++) {
		if (p_name == StringName(methods[i])) {
			return true;
		}
	}
	return false;
}

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
//   - `bsearch_custom` takes a `Callable` comparator alongside the value; its
//     behavior under typing is not modeled, so it bails. The plain
//     value-validating reads (`has`, `find`, `rfind`, `count`, `erase`,
//     `bsearch`) are instead handled precisely via `is_value_validating_array_method`,
//     which validates the coerced argument against the element type.
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

// Computes the builtin type a compound indexed write `container[key] op= value`
// stores back, modeling the runtime: the stored type is the result of the
// `Variant` operator applied to the current element type and the value type, not
// merely the value's type (e.g. `int *= float` stores a float). Both operands
// must be concrete builtin types and the operator must have a defined result for
// that pair; otherwise the result is unknowable and inference must bail.
//
// `r_resolved` is set false when the result cannot be determined (a non-builtin
// operand or an operator with no defined return type for the pair), in which case
// the returned `DataType` is meaningless and the caller must skip conservatively.
DataType compound_write_result_type(Variant::Operator p_op, const DataType &p_element, const DataType &p_value, bool &r_resolved) {
	r_resolved = false;
	DataType result;
	if (p_op == Variant::OP_MAX) {
		return result;
	}
	if (p_element.kind != DataType::BUILTIN || p_value.kind != DataType::BUILTIN) {
		return result;
	}
	const Variant::Type element_builtin = p_element.builtin_type;
	const Variant::Type value_builtin = p_value.builtin_type;
	if (element_builtin == Variant::NIL || value_builtin == Variant::NIL) {
		return result;
	}
	// A valid operator evaluator must exist for the pair; otherwise the operation
	// would error at runtime and its result type is undefined.
	if (Variant::get_validated_operator_evaluator(p_op, element_builtin, value_builtin) == nullptr) {
		return result;
	}
	const Variant::Type return_builtin = Variant::get_operator_return_type(p_op, element_builtin, value_builtin);
	if (return_builtin == Variant::NIL) {
		return result;
	}
	result.type_source = DataType::ANNOTATED_INFERRED;
	result.kind = DataType::BUILTIN;
	result.builtin_type = return_builtin;
	r_resolved = true;
	return result;
}

// True when `p_expr` is a plain identifier that resolves to `p_decl`, whether as
// a local variable or, in member mode, as a member variable accessed through
// implicit `self` (a bare `member` reference).
//
// `p_match_by_name` switches member matching from pointer identity to the member's
// name. It is used when scanning a subclass parsed in a separate tree: there a
// reference to the inherited member resolves to that tree's own member node (or the
// base node from a dependency parse), never the original `p_decl` pointer, so the
// name is the only stable key. Pointer identity is preferred whenever available to
// avoid confusing a same-named member shadowed in a nested class.
bool identifier_refers_to(const FSParser::ExpressionNode *p_expr, const FSParser::VariableNode *p_decl, bool p_member_mode, bool p_match_by_name = false) {
	if (p_expr == nullptr || p_expr->type != Node::IDENTIFIER) {
		return false;
	}
	const FSParser::IdentifierNode *identifier = static_cast<const FSParser::IdentifierNode *>(p_expr);
	if (p_member_mode) {
		if (p_match_by_name) {
			// In a subclass the inherited member resolves to INHERITED_VARIABLE; in
			// the declaring class it is MEMBER_VARIABLE. Either names the member.
			const bool is_member_source = identifier->source == FSParser::IdentifierNode::MEMBER_VARIABLE ||
					identifier->source == FSParser::IdentifierNode::INHERITED_VARIABLE;
			return is_member_source && p_decl->identifier != nullptr && identifier->name == p_decl->identifier->name;
		}
		return identifier->source == FSParser::IdentifierNode::MEMBER_VARIABLE && identifier->variable_source == p_decl;
	}
	return identifier->source == FSParser::IdentifierNode::LOCAL_VARIABLE && identifier->variable_source == p_decl;
}

// In member mode, true when `p_subscript` is `self.<member>` for the tracked
// member, i.e. an explicit access through `self` that is equivalent to the bare
// member reference. Access through any other base (`other.<member>`) is *not*
// matched here so the walker can treat it as an unbounded foreign mutation.
//
// The attribute is matched by its resolved declaration, not merely its name, so a
// nested class with its own member of the same name (where `self` is a different
// instance) is not mistaken for the tracked member.
bool self_attribute_refers_to(const FSParser::SubscriptNode *p_subscript, const FSParser::VariableNode *p_decl, bool p_member_mode, bool p_match_by_name = false) {
	if (!p_member_mode || p_subscript == nullptr || !p_subscript->is_attribute) {
		return false;
	}
	if (p_subscript->base == nullptr || p_subscript->base->type != Node::SELF) {
		return false;
	}
	if (p_subscript->attribute == nullptr || p_decl->identifier == nullptr) {
		return false;
	}
	// When scanning a subclass tree the tracked member resolves to a different node
	// (or the base node from a dependency parse), so match on the inherited name.
	if (p_match_by_name) {
		return p_subscript->attribute->name == p_decl->identifier->name;
	}
	// When the analyzer resolved the attribute to a member variable, require it to
	// be exactly the tracked declaration. Fall back to a name comparison only when
	// the source was not resolved to a member variable.
	if (p_subscript->attribute->source == FSParser::IdentifierNode::MEMBER_VARIABLE) {
		return p_subscript->attribute->variable_source == p_decl;
	}
	return p_subscript->attribute->name == p_decl->identifier->name;
}

// Object's dynamic reflection APIs can read or write a member by name without any
// AST reference to it, e.g. `set("_items", [..])`, `get("_items").append(..)`, or
// `call("mutate")`. Signal/notification dispatchers (`emit_signal`, `notification`)
// likewise synchronously run connected callbacks or `_notification` handlers that
// the scan cannot see and that may mutate the member. In member mode any of these
// on the instance forces a conservative skip.
bool is_dynamic_reflection_method(const StringName &p_name) {
	static const char *methods[] = {
		"set", "get", "set_deferred", "set_indexed", "get_indexed",
		"call", "callv", "call_deferred", "set_block_signals",
		"get_property_list", "property_get_revert",
		// Node-derived scripts expose thread-safe/deferred property and call
		// variants that can set a property or invoke a method by name.
		"set_thread_safe", "call_thread_safe",
		"set_deferred_thread_group", "call_deferred_thread_group",
		"set_thread_group",
		// Synchronously dispatch to callbacks/handlers outside this AST.
		"emit_signal", "notification",
		nullptr
	};
	for (int i = 0; methods[i] != nullptr; i++) {
		if (p_name == StringName(methods[i])) {
			return true;
		}
	}
	return false;
}

// True when a `$path` node reference resolves to the current instance, i.e. the
// path is `.` or empty. Any other path targets a different node.
bool get_node_is_self(const FSParser::GetNodeNode *p_get_node) {
	if (p_get_node == nullptr) {
		return false;
	}
	const String path = p_get_node->full_path;
	return path.is_empty() || path == ".";
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
		case Node::TUPLE_LITERAL:
		case Node::TYPE_TEST:
		case Node::UNARY_OPERATOR:
			return true;
		default:
			return false;
	}
}

// Tracks bindings to a read of the tracked container whose analyzer type is
// `Variant` while the container is bare but narrows to a concrete slot type (array
// element / dictionary key / dictionary value) once the container is typed, and
// detects when that narrowing would break otherwise-valid code.
//
// Two binding forms are tracked:
//   - `for v in c:` loop iterators, which become hard-typed once the container is
//     typed. These break both on an incompatible reassignment (`v = <other type>`,
//     valid only because a bare container yields `Variant`) and on a typed use.
//   - plain `var v = c[i]` soft locals (and element-returning accessor reads bound to
//     one). A soft local downgrades to `Variant` on reassignment rather than erroring,
//     so reassignment never narrows it -- but a typed *use* type-checks it directly, so
//     it is tracked for the typed-use check only (`reassignment_narrows == false`).
// An inferred `var v := c[i]` cannot occur over a bare container (`:=` from a
// `Variant` element read is a parse error), and an explicitly annotated binding pins
// its type, so neither needs tracking. See the design doc.
//
// A typed *use* is the binding flowing into a position valid only while it is
// `Variant`: a typed call argument, a typed return, or a typed assignment target.
// Each such use records the type that position requires; once the slot type is known
// the use breaks unless that slot type matches it exactly (a conservative but sound
// rendered-type comparison). Reassignments and uses can appear before the binding, so
// all are recorded here and reconciled after the slot types are known (see
// `narrows_a_read`). Each binding remembers which container slot it reads from so the
// reassigned/required value is compared against the right type.
//
// The narrow remaining boundary -- genuinely flow- or value-dependent cases a single
// conservative annotation cannot capture -- still falls to the post-edit verification
// harness (#34); see the design doc.
class ReadNarrowingTracker {
public:
	enum Slot {
		ARRAY_ELEMENT,
		DICTIONARY_KEY,
		DICTIONARY_VALUE,
	};

	// Records that `p_sink` is a `for` loop iterator bound to a read of the container's
	// `p_slot`. The iterator is hard-typed once the container is typed, so both a later
	// reassignment to an incompatible value and a typed use narrow it. Ignored when the
	// iterator carries an explicit type annotation, since the analyzer keeps that type.
	void note_loop_read_binding(const void *p_sink, Slot p_slot) {
		if (p_sink != nullptr) {
			Binding binding;
			binding.slot = p_slot;
			binding.reassignment_narrows = true;
			read_bindings[p_sink] = binding;
		}
	}

	// Records that `p_sink` is a plain soft local bound to a read of the container's
	// `p_slot` (`var v = c[i]`, an element-returning accessor). Unlike a loop iterator,
	// a soft local downgrades to `Variant` on an incompatible reassignment rather than
	// erroring, so reassignment never narrows it; only a typed *use* does. Recorded so
	// the typed-use check in `narrows_a_read` can see it.
	void note_soft_read_binding(const void *p_sink, Slot p_slot) {
		if (p_sink != nullptr && !read_bindings.has(p_sink)) {
			Binding binding;
			binding.slot = p_slot;
			binding.reassignment_narrows = false;
			read_bindings[p_sink] = binding;
		}
	}

	// Records that `p_sink` flows into a type-sensitive position (a typed call
	// argument, a typed return, or a typed assignment target) that requires
	// `p_required`. While the container is bare the binding is `Variant`, which
	// every such position accepts; once the container is typed the binding narrows
	// to its slot type, which the position rejects unless that slot type matches
	// `p_required`. Uses can be seen before the binding, so all are kept and
	// reconciled in `narrows_a_read`. A non-renderable (`Variant`/soft) requirement
	// never narrows, so it is skipped there.
	void note_typed_use(const void *p_sink, const DataType &p_required) {
		if (p_sink != nullptr) {
			TypedUse use;
			use.sink = p_sink;
			use.required = p_required;
			typed_uses.push_back(use);
		}
	}

	// Records a *direct* element/value read of the container (`our_var[i]`, an
	// element-returning accessor call) that flows straight into a type-sensitive
	// position without being bound to a local first, e.g. `take_string(nums[0])` or
	// `return d[k]`. The read is `Variant` while the container is bare but narrows to
	// `p_slot`'s type once typed; it then breaks the position unless that slot type
	// matches `p_required`. Reconciled in `narrows_a_read` like a bound use.
	void note_direct_read_use(Slot p_slot, const DataType &p_required) {
		TypedUse use;
		use.is_direct_read = true;
		use.direct_slot = p_slot;
		use.required = p_required;
		typed_uses.push_back(use);
	}

	// Records a whole-variable reassignment (`p_sink = value`) of a local or loop
	// iterator, with the assigned value's type. Only reassignments to a recorded
	// read binding matter, but they can be seen before the binding, so all are kept.
	void note_reassignment(const void *p_sink, const DataType &p_value) {
		if (p_sink != nullptr) {
			Reassignment reassignment;
			reassignment.sink = p_sink;
			reassignment.value = p_value;
			reassignments.push_back(reassignment);
		}
	}

	// Records a compound reassignment (`p_sink op= ...`) of a local or loop
	// iterator. A compound operation's result type and validity depend on the
	// operand type, which changes once the binding narrows from `Variant` to the
	// element type, so any compound reassignment of a read binding is treated as
	// narrowing-unsafe regardless of the operand.
	void note_compound_reassignment(const void *p_sink) {
		if (p_sink != nullptr) {
			Reassignment reassignment;
			reassignment.sink = p_sink;
			reassignment.is_compound = true;
			reassignments.push_back(reassignment);
		}
	}

	// True when some read binding is reassigned a value whose rendered type differs
	// from the element type its slot would narrow to (or a compound reassignment,
	// which is always unsafe). `p_slot_rendered` returns the rendered element type
	// for a slot, or false when that slot has no concrete type (then nothing
	// narrows there, so it is skipped).
	template <typename SlotRenderer>
	bool narrows_a_read(const SlotRenderer &p_slot_rendered, String &r_detail) const {
		for (const Reassignment &reassignment : reassignments) {
			HashMap<const void *, Binding>::ConstIterator binding = read_bindings.find(reassignment.sink);
			if (binding == read_bindings.end() || !binding->value.reassignment_narrows) {
				// A soft local downgrades on reassignment rather than erroring, so its
				// reassignment never narrows; only a loop iterator's does.
				continue;
			}
			String slot_rendered;
			if (!p_slot_rendered(binding->value.slot, slot_rendered)) {
				continue;
			}
			if (!reassignment.is_compound) {
				DataType value = reassignment.value;
				value.is_constant = false;
				String value_rendered;
				if (FSRefactorTypes::render_annotatable_type(value, value_rendered) && value_rendered == slot_rendered) {
					continue;
				}
			}
			r_detail = vformat(
					"a read bound to a local is later reassigned a value incompatible with the %s element type",
					slot_rendered);
			return true;
		}
		// A narrowed read used in a type-sensitive position breaks when the slot type it
		// narrows to is not exactly the type that position requires. The `Variant` the
		// read carries while the container is bare accepts every such position, so any
		// mismatch is currently valid only because of that softness. This covers a read
		// bound to a local/iterator (resolved via `read_bindings`) and a *direct*
		// element/value read that flows straight into the position.
		for (const TypedUse &use : typed_uses) {
			Slot slot;
			if (use.is_direct_read) {
				slot = use.direct_slot;
			} else {
				HashMap<const void *, Binding>::ConstIterator binding = read_bindings.find(use.sink);
				if (binding == read_bindings.end()) {
					continue;
				}
				slot = binding->value.slot;
			}
			String slot_rendered;
			if (!p_slot_rendered(slot, slot_rendered)) {
				continue;
			}
			// A position that accepts `Variant` never breaks when the read narrows, so it
			// is skipped: an untyped parameter/return is `Variant`; an *explicit* `Variant`
			// annotation is hard but still accepts every type; and a weak (unannotated)
			// assignment target has only an inferred type that downgrades on an
			// incompatible store rather than rejecting it. Only a hard, non-`Variant`
			// requirement actually rejects the narrowed type.
			String required_rendered;
			if (use.required.is_variant() || !use.required.is_hard_type() ||
					!FSRefactorTypes::render_annotatable_type(use.required, required_rendered)) {
				continue;
			}
			if (required_rendered == slot_rendered) {
				continue;
			}
			r_detail = vformat(
					"a narrowed read is used in a position requiring %s, incompatible with the %s element type",
					required_rendered, slot_rendered);
			return true;
		}
		return false;
	}

private:
	struct Binding {
		Slot slot = ARRAY_ELEMENT;
		// True for a `for` loop iterator (hard-typed once the container is typed, so a
		// reassignment narrows it); false for a soft local (downgrades on reassignment).
		bool reassignment_narrows = false;
	};
	struct Reassignment {
		const void *sink = nullptr;
		DataType value;
		bool is_compound = false;
	};
	struct TypedUse {
		const void *sink = nullptr; // The bound local/iterator, when `is_direct_read` is false.
		bool is_direct_read = false; // True for an unbound `our_var[i]`/accessor read used directly.
		Slot direct_slot = ARRAY_ELEMENT; // The narrowed slot, used only for a direct read.
		DataType required; // The type the use position requires the read to satisfy.
	};
	HashMap<const void *, Binding> read_bindings;
	Vector<Reassignment> reassignments;
	Vector<TypedUse> typed_uses;
};

// Resolves the local declaration or loop iterator a plain identifier refers to,
// so a reassignment target can be matched to a recorded read binding by identity.
// Returns nullptr for anything that is not a writable local/iterator reference.
const void *local_sink_of(const FSParser::ExpressionNode *p_expr) {
	if (p_expr == nullptr || p_expr->type != Node::IDENTIFIER) {
		return nullptr;
	}
	const FSParser::IdentifierNode *identifier = static_cast<const FSParser::IdentifierNode *>(p_expr);
	switch (identifier->source) {
		case FSParser::IdentifierNode::LOCAL_VARIABLE:
			return identifier->variable_source;
		case FSParser::IdentifierNode::LOCAL_ITERATOR:
		case FSParser::IdentifierNode::LOCAL_BIND:
			return identifier->bind_source;
		default:
			return nullptr;
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
	explicit ElementInferenceWalker(const FSParser::VariableNode *p_decl, bool p_member_mode = false) :
			decl(p_decl), member_mode(p_member_mode) {}

	// Switches member matching from pointer identity to the member's name, for
	// scanning a subclass parsed in a separate tree (see `identifier_refers_to`).
	void set_match_member_by_name(bool p_enabled) { match_member_by_name = p_enabled; }

	// Sets the return-type contract of the body currently being scanned, so a
	// `return <binding>` of a narrowed read can be checked against it. The local
	// entry point sets the declaring function's return type once; member mode sets it
	// per method, and lambda scanning swaps in (and restores) the lambda's own.
	void set_current_return_type(const DataType &p_return_type) { current_return_type = p_return_type; }

	// Records whether the caller enumerated every project-wide subclass of the
	// declaring class, together with the declaring class itself. An overridable `self`
	// call need not be bailed on only when the closure is complete *and* the called
	// method is declared directly in the declaring class: then, for any instance in the
	// scanned set (the declaring class or one of its subclasses), the call dispatches
	// to the declaring class's body or a subclass override -- all folded into this same
	// union. A call to a method inherited from a base above the declaring class is not
	// bounded by the closure (a plain declaring-class instance would dispatch to the
	// unscanned base body, which could mutate the member dynamically), so it must bail.
	void set_override_resolution(bool p_complete, const FSParser::ClassNode *p_declaring_class) {
		subclasses_complete = p_complete;
		declaring_class = p_declaring_class;
	}

	bool bailed = false;
	FSContainerInference::Outcome bail_outcome = FSContainerInference::NOT_APPLICABLE;
	String bail_detail;

	bool has_element = false;
	DataType element_type;

	// Reads that coerce a value argument to the element type, and compound element
	// writes whose stored type depends on the element type, can only be validated
	// once the element type is known. Because the scan is flow-insensitive they are
	// recorded here and checked against the final accumulated element type in
	// `finalize()`, after the whole body has been walked.
	struct PendingValidation {
		DataType argument; // The value coerced to the element type by the read.
	};
	struct PendingCompoundWrite {
		Variant::Operator op = Variant::OP_MAX;
		DataType value; // The right-hand operand of `element op= value`.
	};
	Vector<PendingValidation> pending_validations;
	Vector<PendingCompoundWrite> pending_compound_writes;

	// Locals/iterators bound to a read of the array whose narrowing to the element
	// type could break a later reassignment. Reconciled in `finalize()`.
	ReadNarrowingTracker read_narrowing;

	// Resolves the deferred validations and compound writes against the final
	// element type. Must be called once after the whole body has been scanned and
	// before the result is read. When no element type was accumulated the container
	// is never typed, so the deferred operations run on a Variant element exactly as
	// before and need no validation.
	void finalize() {
		if (bailed || !has_element) {
			return;
		}
		for (const PendingValidation &validation : pending_validations) {
			DataType argument = validation.argument;
			argument.is_constant = false;
			String rendered;
			if (!FSRefactorTypes::render_annotatable_type(argument, rendered)) {
				bail(FSContainerInference::UNPROVABLE,
						"a value-validating read coerces an argument whose type cannot be proven to match the element type");
				return;
			}
			if (rendered != element_rendered) {
				bail(FSContainerInference::UNPROVABLE,
						vformat("a value-validating read coerces a %s argument to the %s element type", rendered, element_rendered));
				return;
			}
		}
		for (const PendingCompoundWrite &write : pending_compound_writes) {
			bool resolved = false;
			const DataType stored = compound_write_result_type(write.op, element_type, write.value, resolved);
			if (!resolved) {
				bail(FSContainerInference::UNPROVABLE, "a compound element write stores a value whose type cannot be proven");
				return;
			}
			// The stored type is fed through the element accumulator: it either matches
			// (behavior-preserving) or diverges into a MIXED skip.
			contribute_element(stored);
			if (bailed) {
				return;
			}
		}
		String narrow_detail;
		const bool narrows = read_narrowing.narrows_a_read(
				[this](ReadNarrowingTracker::Slot, String &r_rendered) {
					r_rendered = element_rendered;
					return has_element;
				},
				narrow_detail);
		if (narrows) {
			bail(FSContainerInference::READ_NARROWS, narrow_detail);
		}
	}

	void contribute_from_array_value(const FSParser::ExpressionNode *p_value) {
		if (bailed || p_value == nullptr) {
			return;
		}
		if (p_value->type == Node::ARRAY) {
			const FSParser::ArrayNode *array = static_cast<const FSParser::ArrayNode *>(p_value);
			for (const FSParser::ExpressionNode *element : array->elements) {
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
		bail(FSContainerInference::UNPROVABLE, "an array value with an unknown element type is merged in");
	}

	void scan_suite(const FSParser::SuiteNode *p_suite) {
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

	// Scans a class-member initializer expression for references that would alias
	// or mutate the tracked variable (used in member mode).
	void scan_initializer(const FSParser::ExpressionNode *p_initializer) {
		scan_value(p_initializer);
	}

private:
	const FSParser::VariableNode *decl = nullptr;
	bool member_mode = false;
	bool match_member_by_name = false;
	bool subclasses_complete = false;
	const FSParser::ClassNode *declaring_class = nullptr;
	String element_rendered;
	DataType current_return_type;

	bool is_our_var(const FSParser::ExpressionNode *p_expr) const {
		return identifier_refers_to(p_expr, decl, member_mode, match_member_by_name);
	}

	// Records a typed use of `p_value` against `p_required` for a narrowed read flowing
	// into a type-sensitive position: a plain reference to a local/iterator (a possible
	// narrowed read binding) or a *direct* element read of the tracked array
	// (`our_var[i]`, an accessor call). Reconciled in `finalize()`; anything else is ignored.
	void note_typed_use(const FSParser::ExpressionNode *p_value, const DataType &p_required) {
		if (const void *sink = local_sink_of(p_value)) {
			read_narrowing.note_typed_use(sink, p_required);
		} else if (is_element_read_of_var(p_value)) {
			read_narrowing.note_direct_read_use(ReadNarrowingTracker::ARRAY_ELEMENT, p_required);
		}
	}

	// Records each call argument that is a narrowed read binding against the resolved
	// parameter type it flows into. The analyzer records `resolved_parameter_types` in
	// declaration order right before argument validation; varargs beyond the fixed
	// count are not recorded, so those arguments are left to the reassignment check.
	void note_call_argument_uses(const FSParser::CallNode *p_call) {
		if (p_call == nullptr) {
			return;
		}
		const int count = MIN(p_call->arguments.size(), p_call->resolved_parameter_types.size());
		for (int i = 0; i < count; i++) {
			note_typed_use(p_call->arguments[i], p_call->resolved_parameter_types[i]);
		}
	}

	// True when `p_value` reads a single element of the tracked array whose static
	// type is `Variant` while the array is bare but narrows to the element type once
	// typed: an indexed read `our_var[i]` or an element-returning accessor call
	// (`get`/`front`/`back`/`pop_*`/`pick_random`/`max`/`min`). Binding such a read to
	// a soft local makes that local a narrowing read binding.
	bool is_element_read_of_var(const FSParser::ExpressionNode *p_value) const {
		if (p_value == nullptr) {
			return false;
		}
		if (p_value->type == Node::SUBSCRIPT) {
			const FSParser::SubscriptNode *subscript = static_cast<const FSParser::SubscriptNode *>(p_value);
			return !subscript->is_attribute && (is_our_var(subscript->base) || is_self_member_subscript(subscript->base));
		}
		if (p_value->type == Node::CALL) {
			const FSParser::CallNode *call = static_cast<const FSParser::CallNode *>(p_value);
			if (call->callee == nullptr || call->callee->type != Node::SUBSCRIPT) {
				return false;
			}
			const FSParser::SubscriptNode *callee = static_cast<const FSParser::SubscriptNode *>(call->callee);
			if (!callee->is_attribute || callee->attribute == nullptr || !(is_our_var(callee->base) || is_self_member_subscript(callee->base))) {
				return false;
			}
			return is_array_element_accessor(callee->attribute->name);
		}
		return false;
	}

	// Array methods whose return type the analyzer specializes to the element type on a
	// typed array, so binding their result narrows just like `our_var[i]`. This list
	// must match the analyzer's own `is_array_element_accessor` specialization exactly;
	// `max`/`min` are deliberately excluded because they keep a `Variant` return even on
	// a typed array, so binding them never narrows.
	static bool is_array_element_accessor(const StringName &p_name) {
		static const char *methods[] = {
			"get", "front", "back", "pick_random",
			"pop_back", "pop_front", "pop_at",
			nullptr
		};
		for (int i = 0; methods[i] != nullptr; i++) {
			if (p_name == StringName(methods[i])) {
				return true;
			}
		}
		return false;
	}

	// Handles `var v = <init>` / `var v := <init>` / `var v: T = <init>`.
	//   - When the initializer reads an element of the tracked array, records `v` as a
	//     narrowing read binding. A plain `=` local is soft (downgrades on reassignment,
	//     so only typed uses narrow it); a `:=` local is hard, so a reassignment narrows
	//     it too. A `:=` over a *subscript* read (`var v := our_var[i]`) is a parse error
	//     over a bare array, so only an accessor-call `:=` reaches here.
	//   - When `v` is explicitly typed and the initializer is itself a narrowed read
	//     binding, records that as a typed use against `v`'s hard declared type.
	void note_variable_initializer(const FSParser::VariableNode *p_variable) {
		if (p_variable == nullptr || p_variable->initializer == nullptr) {
			return;
		}
		if (p_variable->datatype_specifier == nullptr) {
			if (is_element_read_of_var(p_variable->initializer)) {
				if (p_variable->infer_datatype) {
					read_narrowing.note_loop_read_binding(p_variable, ReadNarrowingTracker::ARRAY_ELEMENT);
				} else {
					read_narrowing.note_soft_read_binding(p_variable, ReadNarrowingTracker::ARRAY_ELEMENT);
				}
			}
		} else {
			// `var v: T = <binding>`: the binding flows into a hard declared type.
			note_typed_use(p_variable->initializer, p_variable->get_datatype());
		}
	}

	bool is_self_member(const FSParser::SubscriptNode *p_subscript) const {
		return self_attribute_refers_to(p_subscript, decl, member_mode, match_member_by_name);
	}

	// `is_self_member` for an expression that is expected to be a `self.<member>`
	// subscript, e.g. the base of `self.member[index]` or `self.member.append(x)`.
	bool is_self_member_subscript(const FSParser::ExpressionNode *p_expr) const {
		return p_expr != nullptr && p_expr->type == Node::SUBSCRIPT &&
				is_self_member(static_cast<const FSParser::SubscriptNode *>(p_expr));
	}

	// In member mode, detects an attribute access that names the tracked member
	// through a base that is not `self` (e.g. `other.member`). The analysis cannot
	// bound how that other reference mutates the member, so it bails. Returns true
	// (and bails) when the access is a foreign reference to the member.
	bool is_foreign_member_access(const FSParser::SubscriptNode *p_subscript) {
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
		bail(FSContainerInference::ESCAPES, "the member is accessed through another reference the inference cannot bound");
		return true;
	}

	void bail(FSContainerInference::Outcome p_outcome, const String &p_detail) {
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
		if (!FSRefactorTypes::render_annotatable_type(element, rendered)) {
			bail(FSContainerInference::UNPROVABLE,
					vformat("an element type (%s) cannot be written as an explicit annotation",
							p_element.is_set() ? p_element.to_string() : String("Variant")));
			return;
		}
		if (!has_element) {
			has_element = true;
			element_type = element;
			element_rendered = rendered;
		} else if (rendered != element_rendered) {
			bail(FSContainerInference::MIXED,
					vformat("the array holds mixed element types (%s and %s)", element_rendered, rendered));
		}
	}

	void scan_statement(const Node *p_statement) {
		if (bailed || p_statement == nullptr) {
			return;
		}
		switch (p_statement->type) {
			case Node::VARIABLE: {
				const FSParser::VariableNode *variable = static_cast<const FSParser::VariableNode *>(p_statement);
				if (variable == decl) {
					return; // Our own declaration; its initializer is accounted for separately.
				}
				note_variable_initializer(variable);
				scan_value(variable->initializer); // Catches `var alias = our_var`.
			} break;
			case Node::CONSTANT: {
				const FSParser::ConstantNode *constant = static_cast<const FSParser::ConstantNode *>(p_statement);
				scan_value(constant->initializer);
			} break;
			case Node::VARIABLE_DESTRUCTURE: {
				const FSParser::VariableDestructureNode *destructure = static_cast<const FSParser::VariableDestructureNode *>(p_statement);
				scan_value(destructure->initializer); // Catches `var (a, b) = our_var`.
			} break;
			case Node::IF: {
				const FSParser::IfNode *if_node = static_cast<const FSParser::IfNode *>(p_statement);
				scan_value(if_node->condition);
				scan_suite(if_node->true_block);
				scan_suite(if_node->false_block);
			} break;
			case Node::FOR: {
				const FSParser::ForNode *for_node = static_cast<const FSParser::ForNode *>(p_statement);
				// `for element in our_var:` is a safe read of the variable.
				if (!is_our_var_read_list(for_node->list)) {
					scan_value(for_node->list);
				} else if (for_node->datatype_specifier == nullptr) {
					// The loop variable's type narrows from `Variant` to the element type
					// once the array is typed; record it unless it is explicitly typed.
					read_narrowing.note_loop_read_binding(for_node->variable, ReadNarrowingTracker::ARRAY_ELEMENT);
				}
				scan_suite(for_node->loop);
			} break;
			case Node::WHILE: {
				const FSParser::WhileNode *while_node = static_cast<const FSParser::WhileNode *>(p_statement);
				scan_value(while_node->condition);
				scan_suite(while_node->loop);
			} break;
			case Node::MATCH: {
				const FSParser::MatchNode *match_node = static_cast<const FSParser::MatchNode *>(p_statement);
				scan_value(match_node->test);
				for (const FSParser::MatchBranchNode *branch : match_node->branches) {
					scan_match_branch(branch);
					if (bailed) {
						return;
					}
				}
			} break;
			case Node::RETURN: {
				const FSParser::ReturnNode *return_node = static_cast<const FSParser::ReturnNode *>(p_statement);
				// `return <binding>` flows the binding into the enclosing function's return
				// contract; once the container is typed the binding narrows and a hard
				// return type may reject it.
				note_typed_use(return_node->return_value, current_return_type);
				scan_value(return_node->return_value); // `return our_var` escapes.
			} break;
			case Node::ASSERT: {
				const FSParser::AssertNode *assert_node = static_cast<const FSParser::AssertNode *>(p_statement);
				scan_value(assert_node->condition);
				scan_value(assert_node->message);
			} break;
			case Node::PASS:
			case Node::BREAK:
			case Node::CONTINUE:
			case Node::BREAKPOINT:
			case Node::ANNOTATION:
				break;
			case Node::CALL:
				// A statement-level call discards its result, so a returned `self` does
				// not escape; only its arguments and dangerous dispatch are scanned.
				scan_call(static_cast<const FSParser::CallNode *>(p_statement), /* result_consumed */ false);
				break;
			default:
				if (is_expression_node(p_statement->type)) {
					scan_value(static_cast<const FSParser::ExpressionNode *>(p_statement));
				}
				break;
		}
	}

	// True when `p_list` is a safe iteration over the tracked variable, covering
	// both a bare reference and (in member mode) `self.<member>`.
	bool is_our_var_read_list(const FSParser::ExpressionNode *p_list) const {
		if (is_our_var(p_list)) {
			return true;
		}
		return p_list != nullptr && p_list->type == Node::SUBSCRIPT &&
				is_self_member(static_cast<const FSParser::SubscriptNode *>(p_list));
	}

	void scan_match_branch(const FSParser::MatchBranchNode *p_branch) {
		if (bailed || p_branch == nullptr) {
			return;
		}
		for (const FSParser::PatternNode *pattern : p_branch->patterns) {
			scan_pattern(pattern);
			if (bailed) {
				return;
			}
		}
		scan_suite(p_branch->guard_body);
		scan_suite(p_branch->block);
	}

	// A match-pattern bind (`match <test>: var v:`) is a constant: FoundryScript rejects
	// reassigning it, so it can never trigger the read-narrowing reassignment check
	// and needs no read-binding tracking here. (Other downstream uses of a narrowed
	// bind fall under the documented verification-harness boundary; see the design
	// doc.)
	void scan_pattern(const FSParser::PatternNode *p_pattern) {
		if (bailed || p_pattern == nullptr) {
			return;
		}
		switch (p_pattern->pattern_type) {
			case FSParser::PatternNode::PT_EXPRESSION:
				scan_value(p_pattern->expression);
				break;
			case FSParser::PatternNode::PT_ARRAY:
				for (const FSParser::PatternNode *sub : p_pattern->array) {
					scan_pattern(sub);
					if (bailed) {
						return;
					}
				}
				break;
			case FSParser::PatternNode::PT_DICTIONARY:
				for (const FSParser::PatternNode::Pair &pair : p_pattern->dictionary) {
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
	void scan_attribute_base(const FSParser::ExpressionNode *p_base) {
		if (p_base != nullptr && p_base->type == Node::SELF) {
			return;
		}
		scan_value(p_base);
	}

	// A receiver expression "may be the instance" unless it is provably a value
	// that cannot alias `self`. `self`/implicit-self obviously may; any other base
	// may too unless its resolved type is a hard, non-Object builtin (e.g. a known
	// `Array`/`Dictionary`/`int`), on which a `get`/`set`/`call` is the container's
	// own method rather than Object reflection.
	static bool receiver_may_be_self(const FSParser::ExpressionNode *p_base) {
		if (p_base == nullptr || p_base->type == Node::SELF) {
			return true; // Implicit self (null base) or explicit `self`.
		}
		const DataType type = p_base->get_datatype();
		if (type.is_hard_type() && type.kind == DataType::BUILTIN && type.builtin_type != Variant::OBJECT && type.builtin_type != Variant::NIL) {
			return false; // A concrete non-Object builtin cannot be `self`.
		}
		return true; // Object/Variant/unknown: conservatively could be `self`.
	}

	// True when `p_call` invokes one of Object's dynamic reflection APIs on a
	// receiver that may be this instance (`self.set(..)`, implicit-self `set(..)`,
	// or `other.set(..)` where `other` may alias `self`). Such a call can read or
	// write any property by name, including the tracked member, with no AST
	// reference the scan can see, so the dynamic name need not be a constant.
	static bool is_self_reflection_call(const FSParser::CallNode *p_call) {
		if (p_call == nullptr || p_call->callee == nullptr) {
			return false;
		}
		if (p_call->callee->type == Node::IDENTIFIER) {
			// Implicit-self bare call, e.g. `set("_items", value)`.
			const FSParser::IdentifierNode *identifier = static_cast<const FSParser::IdentifierNode *>(p_call->callee);
			return is_dynamic_reflection_method(identifier->name);
		}
		if (p_call->callee->type == Node::SUBSCRIPT) {
			const FSParser::SubscriptNode *callee = static_cast<const FSParser::SubscriptNode *>(p_call->callee);
			if (callee->is_attribute && callee->attribute != nullptr && is_dynamic_reflection_method(callee->attribute->name)) {
				return receiver_may_be_self(callee->base);
			}
		}
		return false;
	}

	// True when `p_call` is a first-class signal emission on this instance, e.g.
	// `changed.emit()` or `self.changed.emit()`. Emitting synchronously runs
	// connected callbacks outside this AST, which may mutate the member, so it is an
	// escape just like `emit_signal`. The signal is identified by its `.emit` method
	// on a base resolved to a member signal (bare or `self`-qualified).
	static bool is_signal_emit_call(const FSParser::CallNode *p_call) {
		if (p_call == nullptr || p_call->callee == nullptr || p_call->callee->type != Node::SUBSCRIPT) {
			return false;
		}
		const FSParser::SubscriptNode *callee = static_cast<const FSParser::SubscriptNode *>(p_call->callee);
		if (!callee->is_attribute || callee->attribute == nullptr || callee->attribute->name != StringName("emit")) {
			return false;
		}
		const FSParser::ExpressionNode *base = callee->base;
		if (base == nullptr) {
			return false;
		}
		// Bare `changed.emit()`: the base identifier resolves to a member signal.
		if (base->type == Node::IDENTIFIER) {
			return static_cast<const FSParser::IdentifierNode *>(base)->source == FSParser::IdentifierNode::MEMBER_SIGNAL;
		}
		// `self.changed.emit()`: the base is a `self.<signal>` attribute.
		if (base->type == Node::SUBSCRIPT) {
			const FSParser::SubscriptNode *signal_access = static_cast<const FSParser::SubscriptNode *>(base);
			if (signal_access->is_attribute && signal_access->base != nullptr && signal_access->base->type == Node::SELF && signal_access->attribute != nullptr) {
				return signal_access->attribute->source == FSParser::IdentifierNode::MEMBER_SIGNAL;
			}
		}
		return false;
	}

	// True when `p_value` is a reference to a reflection method bound to this
	// instance, either bare (`set`) or `self`-qualified (`self.set`). The resulting
	// callable could mutate the member by name later (`s.call("_m", ..)`). A bare
	// reference shadowed by a local/parameter of the same name is not the bound
	// method, so it is excluded.
	static bool is_self_reflection_reference(const FSParser::ExpressionNode *p_value) {
		if (p_value == nullptr) {
			return false;
		}
		if (p_value->type == Node::IDENTIFIER) {
			const FSParser::IdentifierNode *identifier = static_cast<const FSParser::IdentifierNode *>(p_value);
			switch (identifier->source) {
				case FSParser::IdentifierNode::FUNCTION_PARAMETER:
				case FSParser::IdentifierNode::LOCAL_VARIABLE:
				case FSParser::IdentifierNode::LOCAL_CONSTANT:
				case FSParser::IdentifierNode::LOCAL_ITERATOR:
				case FSParser::IdentifierNode::LOCAL_BIND:
					return false;
				default:
					return is_dynamic_reflection_method(identifier->name);
			}
		}
		if (p_value->type == Node::SUBSCRIPT) {
			const FSParser::SubscriptNode *subscript = static_cast<const FSParser::SubscriptNode *>(p_value);
			if (subscript->is_attribute && subscript->base != nullptr && subscript->base->type == Node::SELF && subscript->attribute != nullptr) {
				return is_dynamic_reflection_method(subscript->attribute->name);
			}
		}
		return false;
	}

	// True when `p_call` invokes a non-static *script* method on this instance,
	// either `self.hook()` or implicit-self `hook()`. Such a method can be
	// overridden by a subclass outside the analyzed file, and the override may
	// mutate the inherited member with a different element type. Built-in language
	// and Variant utilities (`print`, `len`, ...) are excluded since they dispatch
	// to engine code that cannot touch a script member.
	static bool is_overrideable_self_method_call(const FSParser::CallNode *p_call) {
		if (p_call == nullptr || p_call->callee == nullptr) {
			return false;
		}
		if (p_call->callee->type == Node::IDENTIFIER) {
			// Implicit-self bare call, e.g. `hook()`. A name that is not a global
			// utility resolves to a method on this instance (own, inherited, or a
			// constructor), which a subclass can override.
			const FSParser::IdentifierNode *identifier = static_cast<const FSParser::IdentifierNode *>(p_call->callee);
			return !is_global_utility_call(identifier->name);
		}
		if (p_call->callee->type == Node::SUBSCRIPT) {
			const FSParser::SubscriptNode *callee = static_cast<const FSParser::SubscriptNode *>(p_call->callee);
			if (callee->is_attribute && callee->base != nullptr && callee->base->type == Node::SELF && callee->attribute != nullptr) {
				return callee->attribute->source == FSParser::IdentifierNode::MEMBER_FUNCTION &&
						!callee->attribute->function_source_is_static;
			}
		}
		return false;
	}

	// A `super(...)` / `super.method(...)` call dispatches to a base-class method in
	// another file, which can mutate the inherited member; it cannot be bounded.
	static bool is_super_call(const FSParser::CallNode *p_call) {
		return p_call != nullptr && p_call->is_super;
	}

	// True when an overridable `self` method call targets a method declared directly in
	// the declaring class. Such a method's dispatch is bounded by the scanned union: for
	// any instance in the closure it resolves to the declaring class's body or a
	// subclass override, all folded in here. A method only inherited from a base above
	// the declaring class is excluded, since a plain declaring-class instance would
	// dispatch to that unscanned base body. The name match is exact: a non-static
	// (instance) function member of the declaring class with the call's name.
	bool resolves_to_scanned_method(const FSParser::CallNode *p_call) const {
		if (declaring_class == nullptr || p_call == nullptr || p_call->function_name == StringName()) {
			return false;
		}
		for (const FSParser::ClassNode::Member &member : declaring_class->members) {
			if (member.type == FSParser::ClassNode::Member::FUNCTION && member.function != nullptr &&
					member.function->identifier != nullptr && member.function->identifier->name == p_call->function_name &&
					!member.function->is_static) {
				return true;
			}
		}
		return false;
	}

	// True when `p_value` takes a bound reference to a non-static script method on
	// this instance (`var cb = hook` or `var cb = self.hook`). The callable can
	// later dispatch to a subclass override that mutates the inherited member.
	static bool references_overrideable_method(const FSParser::ExpressionNode *p_value) {
		const FSParser::IdentifierNode *identifier = nullptr;
		if (p_value != nullptr && p_value->type == Node::IDENTIFIER) {
			identifier = static_cast<const FSParser::IdentifierNode *>(p_value);
		} else if (p_value != nullptr && p_value->type == Node::SUBSCRIPT) {
			const FSParser::SubscriptNode *subscript = static_cast<const FSParser::SubscriptNode *>(p_value);
			if (subscript->is_attribute && subscript->base != nullptr && subscript->base->type == Node::SELF) {
				identifier = subscript->attribute;
			}
		}
		return identifier != nullptr &&
				identifier->source == FSParser::IdentifierNode::MEMBER_FUNCTION &&
				!identifier->function_source_is_static;
	}

	// True when a constant expression names the tracked member: a String/StringName
	// equal to its name, or a NodePath any of whose components is its name (covering
	// `set_indexed(^"_m", v)`). The analyzer folds a constant reference
	// (`const P = "_m"`) into `reduced_value`, so a constant identifier is matched
	// as well as a raw literal.
	bool is_constant_member_name(const FSParser::ExpressionNode *p_expr) const {
		if (p_expr == nullptr || decl->identifier == nullptr || !p_expr->is_constant) {
			return false;
		}
		const Variant &value = p_expr->reduced_value;
		if (value.get_type() == Variant::STRING || value.get_type() == Variant::STRING_NAME) {
			return StringName(value) == decl->identifier->name;
		}
		if (value.get_type() == Variant::NODE_PATH) {
			const NodePath path = value;
			for (int i = 0; i < path.get_name_count(); i++) {
				if (path.get_name(i) == decl->identifier->name) {
					return true;
				}
			}
			for (int i = 0; i < path.get_subname_count(); i++) {
				if (path.get_subname(i) == decl->identifier->name) {
					return true;
				}
			}
		}
		return false;
	}

	// True when a constant string expression equals the tracked member's name,
	// recursing into array literals so `callv("set", ["_m", v])` is also matched.
	bool literal_mentions_member(const FSParser::ExpressionNode *p_expr) const {
		if (p_expr == nullptr || decl->identifier == nullptr) {
			return false;
		}
		if (is_constant_member_name(p_expr)) {
			return true;
		}
		if (p_expr->type == Node::ARRAY) {
			const FSParser::ArrayNode *array = static_cast<const FSParser::ArrayNode *>(p_expr);
			for (const FSParser::ExpressionNode *element : array->elements) {
				if (literal_mentions_member(element)) {
					return true;
				}
			}
		}
		return false;
	}

	// True when `p_call` is a dynamic reflection call (on any receiver) whose
	// arguments mention the tracked member by name, e.g. `other.set("_m", v)` or
	// `other.callv("set", ["_m", v])` where `other` may alias `self`. The
	// receiver-agnostic check covers reflection writes routed through a reference
	// the scan cannot prove distinct from `self`.
	bool reflection_call_names_member(const FSParser::CallNode *p_call) const {
		if (p_call == nullptr || p_call->callee == nullptr || decl->identifier == nullptr) {
			return false;
		}
		StringName method;
		if (p_call->callee->type == Node::IDENTIFIER) {
			method = static_cast<const FSParser::IdentifierNode *>(p_call->callee)->name;
		} else if (p_call->callee->type == Node::SUBSCRIPT) {
			const FSParser::SubscriptNode *callee = static_cast<const FSParser::SubscriptNode *>(p_call->callee);
			if (callee->is_attribute && callee->attribute != nullptr) {
				method = callee->attribute->name;
			}
		}
		if (method == StringName() || !is_dynamic_reflection_method(method)) {
			return false;
		}
		for (const FSParser::ExpressionNode *argument : p_call->arguments) {
			if (literal_mentions_member(argument)) {
				return true;
			}
		}
		return false;
	}

	// True when `p_subscript` is dynamic *property* indexing on a base that may be
	// this instance, e.g. `self["_member"]`, `other["_member"]`, or `other[prop]`
	// with a runtime key. Object subscripting indexes properties by name, so if the
	// base may alias `self` the member can be read or written here regardless of
	// whether the key is a compile-time constant. Indexing into the tracked
	// container itself, or into a concrete non-Object builtin, is a normal element
	// access and is not flagged.
	bool indexes_member_by_name(const FSParser::SubscriptNode *p_subscript) const {
		if (p_subscript == nullptr || p_subscript->is_attribute || decl->identifier == nullptr) {
			return false;
		}
		if (is_our_var(p_subscript->base) || is_self_member_subscript(p_subscript->base)) {
			return false; // Indexing into the member container itself, not the instance.
		}
		// A constant key naming the member is unsafe on any base; otherwise the base
		// must plausibly be the instance for property indexing to reach the member.
		return is_constant_member_name(p_subscript->index) || receiver_may_be_self(p_subscript->base);
	}

	// Treats `p_value` as a value that is consumed by the surrounding context. If
	// the variable itself appears here (other than in one of the recognized safe
	// read positions handled below) it has escaped and inference must stop.
	void scan_value(const FSParser::ExpressionNode *p_value) {
		if (bailed || p_value == nullptr) {
			return;
		}
		if (is_our_var(p_value)) {
			bail(FSContainerInference::ESCAPES, "the variable is used in a position the inference cannot bound");
			return;
		}
		if (member_mode && is_self_reflection_reference(p_value)) {
			// Taking a bound reference to a reflection method (`var s := set`) yields a
			// callable that can later mutate the member by name (`s.call("_m", ..)`),
			// which the scan cannot follow.
			bail(FSContainerInference::ESCAPES, "a reflection method is referenced as a callable");
			return;
		}
		if (member_mode && references_overrideable_method(p_value)) {
			// Taking a bound reference to a script method (`var cb = hook`) yields a
			// callable that can later dispatch to a subclass override mutating the
			// inherited member with another type.
			bail(FSContainerInference::ESCAPES, "an overridable method is referenced as a callable");
			return;
		}
		switch (p_value->type) {
			case Node::SUBSCRIPT: {
				const FSParser::SubscriptNode *subscript = static_cast<const FSParser::SubscriptNode *>(p_value);
				if (is_self_member(subscript)) {
					// `self.member` used directly in a value position leaks a reference,
					// exactly like a bare reference to the variable.
					bail(FSContainerInference::ESCAPES, "the member is used in a position the inference cannot bound");
					return;
				}
				if (is_foreign_member_access(subscript)) {
					return;
				}
				if (indexes_member_by_name(subscript)) {
					// `self["_member"]` / `other["_member"]`: dynamic property access that
					// can read or write the member through a reference that may alias self.
					bail(FSContainerInference::ESCAPES, "the member may be reached through dynamic property indexing");
					return;
				}
				if (subscript->is_attribute) {
					if (is_our_var(subscript->base)) {
						bail(FSContainerInference::ESCAPES, "the variable is accessed through an unsupported member reference");
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
					bail(FSContainerInference::ESCAPES, "the instance escapes through `self`");
				}
				break;
			case Node::CALL:
				scan_call(static_cast<const FSParser::CallNode *>(p_value), /* result_consumed */ true);
				break;
			case Node::ASSIGNMENT:
				scan_assignment(static_cast<const FSParser::AssignmentNode *>(p_value));
				break;
			case Node::ARRAY: {
				const FSParser::ArrayNode *array = static_cast<const FSParser::ArrayNode *>(p_value);
				for (const FSParser::ExpressionNode *element : array->elements) {
					scan_value(element);
				}
			} break;
			case Node::TUPLE_LITERAL: {
				const FSParser::TupleLiteralNode *tuple_literal = static_cast<const FSParser::TupleLiteralNode *>(p_value);
				for (const FSParser::ExpressionNode *element : tuple_literal->elements) {
					scan_value(element);
				}
			} break;
			case Node::DICTIONARY: {
				const FSParser::DictionaryNode *dictionary = static_cast<const FSParser::DictionaryNode *>(p_value);
				for (const FSParser::DictionaryNode::Pair &pair : dictionary->elements) {
					scan_value(pair.key);
					scan_value(pair.value);
				}
			} break;
			case Node::LAMBDA:
				scan_lambda(static_cast<const FSParser::LambdaNode *>(p_value));
				break;
			case Node::BINARY_OPERATOR: {
				const FSParser::BinaryOpNode *binary = static_cast<const FSParser::BinaryOpNode *>(p_value);
				scan_value(binary->left_operand);
				scan_value(binary->right_operand);
			} break;
			case Node::UNARY_OPERATOR: {
				const FSParser::UnaryOpNode *unary = static_cast<const FSParser::UnaryOpNode *>(p_value);
				scan_value(unary->operand);
			} break;
			case Node::TERNARY_OPERATOR: {
				const FSParser::TernaryOpNode *ternary = static_cast<const FSParser::TernaryOpNode *>(p_value);
				scan_value(ternary->condition);
				scan_value(ternary->true_expr);
				scan_value(ternary->false_expr);
			} break;
			case Node::CAST: {
				const FSParser::CastNode *cast = static_cast<const FSParser::CastNode *>(p_value);
				scan_value(cast->operand);
			} break;
			case Node::TYPE_TEST: {
				const FSParser::TypeTestNode *type_test = static_cast<const FSParser::TypeTestNode *>(p_value);
				scan_value(type_test->operand);
			} break;
			case Node::AWAIT: {
				const FSParser::AwaitNode *await = static_cast<const FSParser::AwaitNode *>(p_value);
				scan_value(await->to_await);
			} break;
			case Node::PRELOAD: {
				const FSParser::PreloadNode *preload = static_cast<const FSParser::PreloadNode *>(p_value);
				scan_value(preload->path);
			} break;
			case Node::GET_NODE:
				// `$"."` / `$""` resolves to the instance, so consuming it leaks `self`
				// (e.g. `return $"."`). Other paths reach a different node.
				if (member_mode && get_node_is_self(static_cast<const FSParser::GetNodeNode *>(p_value))) {
					bail(FSContainerInference::ESCAPES, "the instance escapes through a `$\".\"` node reference");
				}
				break;
			default:
				// Remaining expression kinds (literals, `self`, plain identifiers that
				// are not our variable) cannot reference it.
				break;
		}
	}

	// Scans a call expression. `p_result_consumed` is true when the call's return
	// value is used as a value (so a result that may be `self` would escape) and
	// false when it is a statement whose result is discarded.
	void scan_call(const FSParser::CallNode *p_call, bool p_result_consumed) {
		if (bailed || p_call == nullptr) {
			return;
		}
		// Record any narrowed read binding passed as a typed argument before dispatch
		// branches consume the arguments.
		note_call_argument_uses(p_call);
		// A modeled method on the tracked container (`_member.append(x)` /
		// `self._member.set(k, v)`) is handled first: its receiver is the member
		// itself, so the dynamic-reflection/override guards below (which assume the
		// receiver may be `self`) do not apply to it.
		if (p_call->callee != nullptr && p_call->callee->type == Node::SUBSCRIPT) {
			const FSParser::SubscriptNode *callee = static_cast<const FSParser::SubscriptNode *>(p_call->callee);
			if (callee->is_attribute && (is_our_var(callee->base) || is_self_member_subscript(callee->base))) {
				scan_method_on_var(p_call, callee->attribute != nullptr ? callee->attribute->name : StringName());
				return;
			}
		}
		if (member_mode && (is_self_reflection_call(p_call) || is_signal_emit_call(p_call) || reflection_call_names_member(p_call))) {
			bail(FSContainerInference::ESCAPES, "the member may be reached through a dynamic property call");
			return;
		}
		if (member_mode && is_super_call(p_call)) {
			// A `super(...)` call dispatches upward to a base-class method in another
			// file. The base classes lie outside the subclass closure the caller
			// enumerated, so their bodies are not folded into this union and a base
			// method could mutate the inherited member with another type unseen.
			bail(FSContainerInference::ESCAPES, "the member may be mutated by a base method via `super`");
			return;
		}
		if (member_mode && is_overrideable_self_method_call(p_call)) {
			// A script method on this instance can be overridden by a subclass that
			// mutates the inherited member with another type. The call is sound to
			// resolve -- rather than bail -- only when the closure is complete *and* the
			// method resolves to a scanned body (the declaring class or a scanned
			// subclass): then every override of it also lives in a scanned subclass and
			// is folded into this same union. A call to an inherited base method (defined
			// above the declaring class, outside the closure) is not scanned and could
			// mutate the member dynamically, so it must still bail.
			if (!subclasses_complete || !resolves_to_scanned_method(p_call)) {
				bail(FSContainerInference::ESCAPES, "the member may be mutated by an overridden or inherited method");
				return;
			}
		}
		if (p_call->callee != nullptr && p_call->callee->type == Node::SUBSCRIPT) {
			const FSParser::SubscriptNode *callee = static_cast<const FSParser::SubscriptNode *>(p_call->callee);
			if (callee->is_attribute && callee->base != nullptr && callee->base->type == Node::SUBSCRIPT &&
					is_foreign_member_access(static_cast<const FSParser::SubscriptNode *>(callee->base))) {
				return; // `other.member.append(x)`.
			}
			// `self.method(...)` is a call on this instance (a native method, since a
			// script method already bailed above). Its arguments are scanned for leaks,
			// but if its result is consumed it may itself be `self` (e.g.
			// `self.get_node(".")`), which would escape; bail in that case.
			if (callee->is_attribute && callee->base != nullptr && callee->base->type == Node::SELF) {
				if (member_mode && p_result_consumed) {
					bail(FSContainerInference::ESCAPES, "a call on `self` may return the instance");
					return;
				}
				for (const FSParser::ExpressionNode *argument : p_call->arguments) {
					scan_value(argument);
				}
				return;
			}
		}
		scan_value(p_call->callee);
		for (const FSParser::ExpressionNode *argument : p_call->arguments) {
			scan_value(argument);
		}
	}

	void scan_lambda(const FSParser::LambdaNode *p_lambda) {
		if (bailed || p_lambda == nullptr) {
			return;
		}
		for (const FSParser::IdentifierNode *capture : p_lambda->captures) {
			if (capture != nullptr && (is_our_var(capture) || (decl->identifier != nullptr && capture->name == decl->identifier->name))) {
				bail(FSContainerInference::ESCAPES, "the variable is captured by a lambda");
				return;
			}
		}
		if (member_mode && p_lambda->use_self) {
			// In member mode a member is reached through `self`, not a capture, so a
			// lambda that uses `self` could mutate the member through a callable that
			// may be stored or invoked later in ways the analysis cannot bound.
			bail(FSContainerInference::ESCAPES, "the member may be mutated by a lambda capturing `self`");
			return;
		}
		// Scan the lambda body. In member mode it can reach the member through another
		// reference (`func(): other._member.append(x)`); in either mode it can
		// reassign a captured local bound to a narrowing read of the container
		// (`var v = c[0]; var cb = func(): v = "x"`), which the read-narrowing tracker
		// must see. The tracked variable itself never appears here -- capturing it
		// already bailed above -- so scanning is safe.
		if (p_lambda->function != nullptr) {
			// A `return <binding>` inside the lambda targets the lambda's own return
			// contract, not the enclosing function's; swap it in for the lambda body.
			const DataType saved_return_type = current_return_type;
			current_return_type = p_lambda->function->get_datatype();
			scan_suite(p_lambda->function->body);
			current_return_type = saved_return_type;
		}
	}

	void scan_assignment(const FSParser::AssignmentNode *p_assignment) {
		if (bailed || p_assignment == nullptr) {
			return;
		}
		const FSParser::ExpressionNode *assignee = p_assignment->assignee;
		const bool whole_var_assignee = is_our_var(assignee) ||
				(assignee != nullptr && assignee->type == Node::SUBSCRIPT &&
						is_self_member(static_cast<const FSParser::SubscriptNode *>(assignee)));
		if (whole_var_assignee) {
			// Whole-variable (re)assignment.
			if (p_assignment->operation == FSParser::AssignmentNode::OP_NONE ||
					p_assignment->operation == FSParser::AssignmentNode::OP_ADDITION) {
				// `our_var = [..]` or `our_var += [..]` (also `self.member = [..]`).
				contribute_from_array_value(p_assignment->assigned_value);
			} else {
				bail(FSContainerInference::UNPROVABLE, "the array is mutated by an unsupported compound assignment");
			}
			scan_value(p_assignment->assigned_value);
			return;
		}
		if (assignee != nullptr && assignee->type == Node::SUBSCRIPT) {
			const FSParser::SubscriptNode *subscript = static_cast<const FSParser::SubscriptNode *>(assignee);
			// `other.member = value` foreign whole-member assignment.
			if (is_foreign_member_access(subscript)) {
				return;
			}
			// `self["_member"] = value` / `other["_member"] = value` dynamic write.
			if (indexes_member_by_name(subscript)) {
				bail(FSContainerInference::ESCAPES, "the member may be reached through dynamic property indexing");
				return;
			}
			// `other.member[index] = value` foreign element write.
			if (!subscript->is_attribute && subscript->base != nullptr && subscript->base->type == Node::SUBSCRIPT &&
					is_foreign_member_access(static_cast<const FSParser::SubscriptNode *>(subscript->base))) {
				return;
			}
			if (!subscript->is_attribute && (is_our_var(subscript->base) || is_self_member_subscript(subscript->base))) {
				if (p_assignment->operation == FSParser::AssignmentNode::OP_NONE) {
					// `our_var[index] = value`: the value's type is stored verbatim.
					if (p_assignment->assigned_value != nullptr) {
						contribute_element(p_assignment->assigned_value->get_datatype());
					}
				} else {
					// `our_var[index] op= value` stores `typeof(old_element op value)`,
					// which can differ from `typeof(value)` (e.g. `int *= float` stores a
					// float). The stored type depends on the element type, so the check is
					// deferred to `finalize()` once the element type is known.
					PendingCompoundWrite write;
					write.op = p_assignment->variant_op;
					write.value = p_assignment->assigned_value != nullptr ? p_assignment->assigned_value->get_datatype() : DataType();
					pending_compound_writes.push_back(write);
				}
				scan_value(subscript->index);
				scan_value(p_assignment->assigned_value);
				return;
			}
		}
		// Assignment to some other target. Both sides are values: the variable
		// appearing on either side is an escape (e.g. `alias = our_var`).
		// A whole-variable reassignment of a local/iterator (`v = value` or
		// `v op= value`) is also noted, in case `v` was bound to a narrowing read of
		// the array.
		if (const void *sink = local_sink_of(assignee)) {
			if (p_assignment->operation == FSParser::AssignmentNode::OP_NONE) {
				read_narrowing.note_reassignment(sink, p_assignment->assigned_value != nullptr ? p_assignment->assigned_value->get_datatype() : DataType());
			} else {
				read_narrowing.note_compound_reassignment(sink);
			}
		}
		// `typed_target = <binding>`: the RHS binding flows into the target's hard type.
		// A plain `=` to a hard-typed lvalue type-checks the RHS; a compound `op=`
		// depends on the operand and is conservatively treated as narrowing-unsafe via
		// the reassignment path above when the target is itself a binding.
		if (p_assignment->operation == FSParser::AssignmentNode::OP_NONE && assignee != nullptr) {
			note_typed_use(p_assignment->assigned_value, assignee->get_datatype());
		}
		scan_value(assignee);
		scan_value(p_assignment->assigned_value);
	}

	void scan_method_on_var(const FSParser::CallNode *p_call, const StringName &p_method) {
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
				bail(FSContainerInference::UNPROVABLE, vformat("'%s' is called with an unexpected number of arguments", String(p_method)));
			}
		} else if (is_value_validating_array_method(p_method)) {
			// `has`/`find`/`erase`/... coerce their value argument to the element type
			// on a typed array, so the call is behavior-preserving only when that
			// argument already has the element type. Defer the check to `finalize()`,
			// where the accumulated element type is known.
			if (p_call->arguments.size() >= 1 && p_call->arguments[0] != nullptr) {
				PendingValidation validation;
				validation.argument = p_call->arguments[0]->get_datatype();
				pending_validations.push_back(validation);
			}
		} else if (!is_safe_readonly_array_method(p_method)) {
			bail(FSContainerInference::UNPROVABLE, vformat("the array is used through an unmodelled method '%s'", String(p_method)));
		}
		if (bailed) {
			return;
		}
		// Even for modeled methods, arguments may themselves leak the variable.
		for (const FSParser::ExpressionNode *argument : p_call->arguments) {
			scan_value(argument);
			if (bailed) {
				return;
			}
		}
	}

	void contribute_arg_element(const FSParser::CallNode *p_call, int p_index) {
		if (p_call->arguments.size() <= p_index) {
			bail(FSContainerInference::UNPROVABLE, "an array mutation is called with an unexpected number of arguments");
			return;
		}
		const FSParser::ExpressionNode *argument = p_call->arguments[p_index];
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
//   - value-validating reads on the value side (`has_all`, `get`, `find_key`,
//     `recursive_equal`) coerce their key/value argument to the dictionary's
//     type on a typed dictionary, so e.g. `{1: 0}.has(1.2)` flips false to true.
//     The key-validating reads `has`/`erase` are instead handled precisely in
//     `scan_method_on_var`, which validates the coerced key against the key type.
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
	explicit DictionaryInferenceWalker(const FSParser::VariableNode *p_decl, bool p_member_mode = false) :
			decl(p_decl), member_mode(p_member_mode) {}

	// Switches member matching from pointer identity to the member's name, for
	// scanning a subclass parsed in a separate tree (see `identifier_refers_to`).
	void set_match_member_by_name(bool p_enabled) { match_member_by_name = p_enabled; }

	// Sets the return-type contract of the body currently being scanned, so a
	// `return <binding>` of a narrowed read can be checked against it (see the array
	// walker for the rationale).
	void set_current_return_type(const DataType &p_return_type) { current_return_type = p_return_type; }

	// Records whether the caller enumerated every project-wide subclass of the
	// declaring class, together with the declaring class itself (see the array walker
	// for the full rationale). An overridable `self` call need not be bailed on only
	// when the closure is complete *and* the called method is declared directly in the
	// declaring class.
	void set_override_resolution(bool p_complete, const FSParser::ClassNode *p_declaring_class) {
		subclasses_complete = p_complete;
		declaring_class = p_declaring_class;
	}

	bool bailed = false;
	FSContainerInference::Outcome bail_outcome = FSContainerInference::NOT_APPLICABLE;
	String bail_detail;

	bool has_key = false;
	bool has_value = false;
	DataType key_type;
	DataType value_type;

	// Deferred key validations (from `has`/`erase`) and compound value writes, each
	// checked against the final key/value type in `finalize()`. See the array
	// walker for the flow-insensitive rationale.
	struct PendingKeyValidation {
		DataType argument; // The key coerced to the key type by the read.
	};
	struct PendingCompoundWrite {
		Variant::Operator op = Variant::OP_MAX;
		DataType value; // The right-hand operand of `value op= operand`.
	};
	Vector<PendingKeyValidation> pending_key_validations;
	Vector<PendingCompoundWrite> pending_compound_writes;

	// Locals/iterators bound to a read of the dictionary whose narrowing to the key
	// or value type could break a later reassignment. Reconciled in `finalize()`.
	ReadNarrowingTracker read_narrowing;

	// Resolves the deferred key validations and compound writes once both element
	// types are known. When either type was never accumulated the dictionary is left
	// bare, so the deferred operations run on a Variant entry exactly as before.
	void finalize() {
		if (bailed) {
			return;
		}
		if (has_key) {
			for (const PendingKeyValidation &validation : pending_key_validations) {
				DataType argument = validation.argument;
				argument.is_constant = false;
				String rendered;
				if (!FSRefactorTypes::render_annotatable_type(argument, rendered)) {
					bail(FSContainerInference::UNPROVABLE,
							"a value-validating read coerces a key whose type cannot be proven to match the key type");
					return;
				}
				if (rendered != key_rendered) {
					bail(FSContainerInference::UNPROVABLE,
							vformat("a value-validating read coerces a %s key to the %s key type", rendered, key_rendered));
					return;
				}
			}
		}
		if (has_value) {
			for (const PendingCompoundWrite &write : pending_compound_writes) {
				bool resolved = false;
				const DataType stored = compound_write_result_type(write.op, value_type, write.value, resolved);
				if (!resolved) {
					bail(FSContainerInference::UNPROVABLE, "a compound value write stores a value whose type cannot be proven");
					return;
				}
				contribute_value(stored);
				if (bailed) {
					return;
				}
			}
		}
		// A loop key variable narrows to the key type; a `var v = d[k]` value read narrows
		// to the value type. Each binding records which slot it reads from.
		String narrow_detail;
		const bool narrows = read_narrowing.narrows_a_read(
				[this](ReadNarrowingTracker::Slot p_slot, String &r_rendered) {
					if (p_slot == ReadNarrowingTracker::DICTIONARY_VALUE) {
						r_rendered = value_rendered;
						return has_value;
					}
					r_rendered = key_rendered;
					return has_key;
				},
				narrow_detail);
		if (narrows) {
			bail(FSContainerInference::READ_NARROWS, narrow_detail);
		}
	}

	void contribute_from_dictionary_value(const FSParser::ExpressionNode *p_value) {
		if (bailed || p_value == nullptr) {
			return;
		}
		if (p_value->type == Node::DICTIONARY) {
			const FSParser::DictionaryNode *dictionary = static_cast<const FSParser::DictionaryNode *>(p_value);
			for (const FSParser::DictionaryNode::Pair &pair : dictionary->elements) {
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
		bail(FSContainerInference::UNPROVABLE, "a dictionary value with an unknown key or value type is merged in");
	}

	void scan_suite(const FSParser::SuiteNode *p_suite) {
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

	// Scans a class-member initializer expression for references that would alias
	// or mutate the tracked variable (used in member mode).
	void scan_initializer(const FSParser::ExpressionNode *p_initializer) {
		scan_value(p_initializer);
	}

private:
	const FSParser::VariableNode *decl = nullptr;
	bool member_mode = false;
	bool match_member_by_name = false;
	bool subclasses_complete = false;
	const FSParser::ClassNode *declaring_class = nullptr;
	String key_rendered;
	String value_rendered;
	DataType current_return_type;

	bool is_our_var(const FSParser::ExpressionNode *p_expr) const {
		return identifier_refers_to(p_expr, decl, member_mode, match_member_by_name);
	}

	bool is_self_member(const FSParser::SubscriptNode *p_subscript) const {
		return self_attribute_refers_to(p_subscript, decl, member_mode, match_member_by_name);
	}

	bool is_self_member_subscript(const FSParser::ExpressionNode *p_expr) const {
		return p_expr != nullptr && p_expr->type == Node::SUBSCRIPT &&
				is_self_member(static_cast<const FSParser::SubscriptNode *>(p_expr));
	}

	// Records a typed use of `p_value` against `p_required` for a narrowed read flowing
	// into a type-sensitive position: a plain reference to a local/iterator (a possible
	// narrowed read binding) or a *direct* value read of the tracked dictionary
	// (`our_var[k]`, a value accessor). Reconciled in `finalize()`; anything else is ignored.
	void note_typed_use(const FSParser::ExpressionNode *p_value, const DataType &p_required) {
		if (const void *sink = local_sink_of(p_value)) {
			read_narrowing.note_typed_use(sink, p_required);
		} else if (is_value_read_of_var(p_value)) {
			read_narrowing.note_direct_read_use(ReadNarrowingTracker::DICTIONARY_VALUE, p_required);
		}
	}

	// Records each call argument that is a narrowed read binding against the resolved
	// parameter type it flows into (see the array walker for the varargs caveat).
	void note_call_argument_uses(const FSParser::CallNode *p_call) {
		if (p_call == nullptr) {
			return;
		}
		const int count = MIN(p_call->arguments.size(), p_call->resolved_parameter_types.size());
		for (int i = 0; i < count; i++) {
			note_typed_use(p_call->arguments[i], p_call->resolved_parameter_types[i]);
		}
	}

	// True when `p_value` reads a single dictionary value whose static type is
	// `Variant` while the dictionary is bare but narrows to the value type once typed:
	// an indexed read `our_var[k]` or a value-returning accessor (`get`/`get_or_add`).
	// Binding such a read to a soft local makes it a value-narrowing read binding.
	bool is_value_read_of_var(const FSParser::ExpressionNode *p_value) const {
		if (p_value == nullptr) {
			return false;
		}
		if (p_value->type == Node::SUBSCRIPT) {
			const FSParser::SubscriptNode *subscript = static_cast<const FSParser::SubscriptNode *>(p_value);
			return !subscript->is_attribute && (is_our_var(subscript->base) || is_self_member_subscript(subscript->base));
		}
		if (p_value->type == Node::CALL) {
			const FSParser::CallNode *call = static_cast<const FSParser::CallNode *>(p_value);
			if (call->callee == nullptr || call->callee->type != Node::SUBSCRIPT) {
				return false;
			}
			const FSParser::SubscriptNode *callee = static_cast<const FSParser::SubscriptNode *>(call->callee);
			if (!callee->is_attribute || callee->attribute == nullptr || !(is_our_var(callee->base) || is_self_member_subscript(callee->base))) {
				return false;
			}
			return callee->attribute->name == StringName("get") || callee->attribute->name == StringName("get_or_add");
		}
		return false;
	}

	// Handles `var v = <init>` / `var v := <init>` / `var v: T = <init>`, mirroring the
	// array walker: a value read of the tracked dictionary makes `v` a value-narrowing
	// read binding (soft for plain `=`, hard for `:=`, which reaches here only as an
	// accessor-call read since `var v := d[k]` is a parse error over a bare dictionary);
	// a hard-typed `v` records a typed use of its narrowed-binding initializer.
	void note_variable_initializer(const FSParser::VariableNode *p_variable) {
		if (p_variable == nullptr || p_variable->initializer == nullptr) {
			return;
		}
		if (p_variable->datatype_specifier == nullptr) {
			if (is_value_read_of_var(p_variable->initializer)) {
				if (p_variable->infer_datatype) {
					read_narrowing.note_loop_read_binding(p_variable, ReadNarrowingTracker::DICTIONARY_VALUE);
				} else {
					read_narrowing.note_soft_read_binding(p_variable, ReadNarrowingTracker::DICTIONARY_VALUE);
				}
			}
		} else {
			note_typed_use(p_variable->initializer, p_variable->get_datatype());
		}
	}

	bool is_our_var_read_list(const FSParser::ExpressionNode *p_list) const {
		if (is_our_var(p_list)) {
			return true;
		}
		return p_list != nullptr && p_list->type == Node::SUBSCRIPT &&
				is_self_member(static_cast<const FSParser::SubscriptNode *>(p_list));
	}

	// In member mode, detects and bails on an attribute access that names the
	// tracked member through a base other than `self` (e.g. `other.member`).
	bool is_foreign_member_access(const FSParser::SubscriptNode *p_subscript) {
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
		bail(FSContainerInference::ESCAPES, "the member is accessed through another reference the inference cannot bound");
		return true;
	}

	void bail(FSContainerInference::Outcome p_outcome, const String &p_detail) {
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
		if (!FSRefactorTypes::render_annotatable_type(element, rendered)) {
			bail(FSContainerInference::UNPROVABLE,
					vformat("a %s type (%s) cannot be written as an explicit annotation",
							p_slot, p_observed.is_set() ? p_observed.to_string() : String("Variant")));
			return;
		}
		if (!r_has) {
			r_has = true;
			r_type = element;
			r_rendered = rendered;
		} else if (rendered != r_rendered) {
			bail(FSContainerInference::MIXED,
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
				const FSParser::VariableNode *variable = static_cast<const FSParser::VariableNode *>(p_statement);
				if (variable == decl) {
					return; // Our own declaration; its initializer is accounted for separately.
				}
				note_variable_initializer(variable);
				scan_value(variable->initializer); // Catches `var alias = our_var`.
			} break;
			case Node::CONSTANT: {
				const FSParser::ConstantNode *constant = static_cast<const FSParser::ConstantNode *>(p_statement);
				scan_value(constant->initializer);
			} break;
			case Node::VARIABLE_DESTRUCTURE: {
				const FSParser::VariableDestructureNode *destructure = static_cast<const FSParser::VariableDestructureNode *>(p_statement);
				scan_value(destructure->initializer); // Catches `var (a, b) = our_var`.
			} break;
			case Node::IF: {
				const FSParser::IfNode *if_node = static_cast<const FSParser::IfNode *>(p_statement);
				scan_value(if_node->condition);
				scan_suite(if_node->true_block);
				scan_suite(if_node->false_block);
			} break;
			case Node::FOR: {
				const FSParser::ForNode *for_node = static_cast<const FSParser::ForNode *>(p_statement);
				// `for key in our_var:` is a safe read of the variable.
				if (!is_our_var_read_list(for_node->list)) {
					scan_value(for_node->list);
				} else if (for_node->datatype_specifier == nullptr) {
					// Iterating a dictionary binds its keys, so the loop variable narrows
					// from `Variant` to the key type once the dictionary is typed.
					read_narrowing.note_loop_read_binding(for_node->variable, ReadNarrowingTracker::DICTIONARY_KEY);
				}
				scan_suite(for_node->loop);
			} break;
			case Node::WHILE: {
				const FSParser::WhileNode *while_node = static_cast<const FSParser::WhileNode *>(p_statement);
				scan_value(while_node->condition);
				scan_suite(while_node->loop);
			} break;
			case Node::MATCH: {
				const FSParser::MatchNode *match_node = static_cast<const FSParser::MatchNode *>(p_statement);
				scan_value(match_node->test);
				for (const FSParser::MatchBranchNode *branch : match_node->branches) {
					scan_match_branch(branch);
					if (bailed) {
						return;
					}
				}
			} break;
			case Node::RETURN: {
				const FSParser::ReturnNode *return_node = static_cast<const FSParser::ReturnNode *>(p_statement);
				// `return <binding>` flows the binding into the enclosing function's return
				// contract; once the container is typed the binding narrows and a hard
				// return type may reject it.
				note_typed_use(return_node->return_value, current_return_type);
				scan_value(return_node->return_value); // `return our_var` escapes.
			} break;
			case Node::ASSERT: {
				const FSParser::AssertNode *assert_node = static_cast<const FSParser::AssertNode *>(p_statement);
				scan_value(assert_node->condition);
				scan_value(assert_node->message);
			} break;
			case Node::PASS:
			case Node::BREAK:
			case Node::CONTINUE:
			case Node::BREAKPOINT:
			case Node::ANNOTATION:
				break;
			case Node::CALL:
				// A statement-level call discards its result, so a returned `self` does
				// not escape; only its arguments and dangerous dispatch are scanned.
				scan_call(static_cast<const FSParser::CallNode *>(p_statement), /* result_consumed */ false);
				break;
			default:
				if (is_expression_node(p_statement->type)) {
					scan_value(static_cast<const FSParser::ExpressionNode *>(p_statement));
				}
				break;
		}
	}

	void scan_match_branch(const FSParser::MatchBranchNode *p_branch) {
		if (bailed || p_branch == nullptr) {
			return;
		}
		for (const FSParser::PatternNode *pattern : p_branch->patterns) {
			scan_pattern(pattern);
			if (bailed) {
				return;
			}
		}
		scan_suite(p_branch->guard_body);
		scan_suite(p_branch->block);
	}

	// A match-pattern bind (`match <test>: var v:`) is a constant: FoundryScript rejects
	// reassigning it, so it can never trigger the read-narrowing reassignment check
	// and needs no read-binding tracking here. (Other downstream uses of a narrowed
	// bind fall under the documented verification-harness boundary; see the design
	// doc.)
	void scan_pattern(const FSParser::PatternNode *p_pattern) {
		if (bailed || p_pattern == nullptr) {
			return;
		}
		switch (p_pattern->pattern_type) {
			case FSParser::PatternNode::PT_EXPRESSION:
				scan_value(p_pattern->expression);
				break;
			case FSParser::PatternNode::PT_ARRAY:
				for (const FSParser::PatternNode *sub : p_pattern->array) {
					scan_pattern(sub);
					if (bailed) {
						return;
					}
				}
				break;
			case FSParser::PatternNode::PT_DICTIONARY:
				for (const FSParser::PatternNode::Pair &pair : p_pattern->dictionary) {
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
	void scan_attribute_base(const FSParser::ExpressionNode *p_base) {
		if (p_base != nullptr && p_base->type == Node::SELF) {
			return;
		}
		scan_value(p_base);
	}

	// A receiver expression "may be the instance" unless it is provably a value that
	// cannot alias `self` (a hard, non-Object builtin like a known `Dictionary`).
	static bool receiver_may_be_self(const FSParser::ExpressionNode *p_base) {
		if (p_base == nullptr || p_base->type == Node::SELF) {
			return true;
		}
		const DataType type = p_base->get_datatype();
		if (type.is_hard_type() && type.kind == DataType::BUILTIN && type.builtin_type != Variant::OBJECT && type.builtin_type != Variant::NIL) {
			return false;
		}
		return true;
	}

	// True when `p_call` invokes one of Object's dynamic reflection APIs on a
	// receiver that may be this instance, which can read or write any property by
	// name (including the tracked member) with no AST reference the scan can see;
	// the dynamic name need not be a constant.
	static bool is_self_reflection_call(const FSParser::CallNode *p_call) {
		if (p_call == nullptr || p_call->callee == nullptr) {
			return false;
		}
		if (p_call->callee->type == Node::IDENTIFIER) {
			const FSParser::IdentifierNode *identifier = static_cast<const FSParser::IdentifierNode *>(p_call->callee);
			return is_dynamic_reflection_method(identifier->name);
		}
		if (p_call->callee->type == Node::SUBSCRIPT) {
			const FSParser::SubscriptNode *callee = static_cast<const FSParser::SubscriptNode *>(p_call->callee);
			if (callee->is_attribute && callee->attribute != nullptr && is_dynamic_reflection_method(callee->attribute->name)) {
				return receiver_may_be_self(callee->base);
			}
		}
		return false;
	}

	// True when `p_call` is a first-class signal emission on this instance, e.g.
	// `changed.emit()` or `self.changed.emit()`. Emitting synchronously runs
	// connected callbacks outside this AST, which may mutate the member, so it is an
	// escape just like `emit_signal`. The signal is identified by its `.emit` method
	// on a base resolved to a member signal (bare or `self`-qualified).
	static bool is_signal_emit_call(const FSParser::CallNode *p_call) {
		if (p_call == nullptr || p_call->callee == nullptr || p_call->callee->type != Node::SUBSCRIPT) {
			return false;
		}
		const FSParser::SubscriptNode *callee = static_cast<const FSParser::SubscriptNode *>(p_call->callee);
		if (!callee->is_attribute || callee->attribute == nullptr || callee->attribute->name != StringName("emit")) {
			return false;
		}
		const FSParser::ExpressionNode *base = callee->base;
		if (base == nullptr) {
			return false;
		}
		// Bare `changed.emit()`: the base identifier resolves to a member signal.
		if (base->type == Node::IDENTIFIER) {
			return static_cast<const FSParser::IdentifierNode *>(base)->source == FSParser::IdentifierNode::MEMBER_SIGNAL;
		}
		// `self.changed.emit()`: the base is a `self.<signal>` attribute.
		if (base->type == Node::SUBSCRIPT) {
			const FSParser::SubscriptNode *signal_access = static_cast<const FSParser::SubscriptNode *>(base);
			if (signal_access->is_attribute && signal_access->base != nullptr && signal_access->base->type == Node::SELF && signal_access->attribute != nullptr) {
				return signal_access->attribute->source == FSParser::IdentifierNode::MEMBER_SIGNAL;
			}
		}
		return false;
	}

	// True when `p_value` is a reference to a reflection method bound to this
	// instance, either bare (`set`) or `self`-qualified (`self.set`). The resulting
	// callable could mutate the member by name later (`s.call("_m", ..)`). A bare
	// reference shadowed by a local/parameter of the same name is not the bound
	// method, so it is excluded.
	static bool is_self_reflection_reference(const FSParser::ExpressionNode *p_value) {
		if (p_value == nullptr) {
			return false;
		}
		if (p_value->type == Node::IDENTIFIER) {
			const FSParser::IdentifierNode *identifier = static_cast<const FSParser::IdentifierNode *>(p_value);
			switch (identifier->source) {
				case FSParser::IdentifierNode::FUNCTION_PARAMETER:
				case FSParser::IdentifierNode::LOCAL_VARIABLE:
				case FSParser::IdentifierNode::LOCAL_CONSTANT:
				case FSParser::IdentifierNode::LOCAL_ITERATOR:
				case FSParser::IdentifierNode::LOCAL_BIND:
					return false;
				default:
					return is_dynamic_reflection_method(identifier->name);
			}
		}
		if (p_value->type == Node::SUBSCRIPT) {
			const FSParser::SubscriptNode *subscript = static_cast<const FSParser::SubscriptNode *>(p_value);
			if (subscript->is_attribute && subscript->base != nullptr && subscript->base->type == Node::SELF && subscript->attribute != nullptr) {
				return is_dynamic_reflection_method(subscript->attribute->name);
			}
		}
		return false;
	}

	// True when `p_call` invokes a non-static *script* method on this instance,
	// either `self.hook()` or implicit-self `hook()`. Such a method can be
	// overridden by a subclass outside the analyzed file, and the override may
	// mutate the inherited member with a different element type. Built-in language
	// and Variant utilities (`print`, `len`, ...) are excluded since they dispatch
	// to engine code that cannot touch a script member.
	static bool is_overrideable_self_method_call(const FSParser::CallNode *p_call) {
		if (p_call == nullptr || p_call->callee == nullptr) {
			return false;
		}
		if (p_call->callee->type == Node::IDENTIFIER) {
			// Implicit-self bare call, e.g. `hook()`. A name that is not a global
			// utility resolves to a method on this instance (own, inherited, or a
			// constructor), which a subclass can override.
			const FSParser::IdentifierNode *identifier = static_cast<const FSParser::IdentifierNode *>(p_call->callee);
			return !is_global_utility_call(identifier->name);
		}
		if (p_call->callee->type == Node::SUBSCRIPT) {
			const FSParser::SubscriptNode *callee = static_cast<const FSParser::SubscriptNode *>(p_call->callee);
			if (callee->is_attribute && callee->base != nullptr && callee->base->type == Node::SELF && callee->attribute != nullptr) {
				return callee->attribute->source == FSParser::IdentifierNode::MEMBER_FUNCTION &&
						!callee->attribute->function_source_is_static;
			}
		}
		return false;
	}

	// A `super(...)` / `super.method(...)` call dispatches to a base-class method in
	// another file, which can mutate the inherited member; it cannot be bounded.
	static bool is_super_call(const FSParser::CallNode *p_call) {
		return p_call != nullptr && p_call->is_super;
	}

	// True when an overridable `self` method call targets a method declared directly in
	// the declaring class. Such a method's dispatch is bounded by the scanned union: for
	// any instance in the closure it resolves to the declaring class's body or a
	// subclass override, all folded in here. A method only inherited from a base above
	// the declaring class is excluded, since a plain declaring-class instance would
	// dispatch to that unscanned base body. The name match is exact: a non-static
	// (instance) function member of the declaring class with the call's name.
	bool resolves_to_scanned_method(const FSParser::CallNode *p_call) const {
		if (declaring_class == nullptr || p_call == nullptr || p_call->function_name == StringName()) {
			return false;
		}
		for (const FSParser::ClassNode::Member &member : declaring_class->members) {
			if (member.type == FSParser::ClassNode::Member::FUNCTION && member.function != nullptr &&
					member.function->identifier != nullptr && member.function->identifier->name == p_call->function_name &&
					!member.function->is_static) {
				return true;
			}
		}
		return false;
	}

	// True when `p_value` takes a bound reference to a non-static script method on
	// this instance (`var cb = hook` or `var cb = self.hook`). The callable can
	// later dispatch to a subclass override that mutates the inherited member.
	static bool references_overrideable_method(const FSParser::ExpressionNode *p_value) {
		const FSParser::IdentifierNode *identifier = nullptr;
		if (p_value != nullptr && p_value->type == Node::IDENTIFIER) {
			identifier = static_cast<const FSParser::IdentifierNode *>(p_value);
		} else if (p_value != nullptr && p_value->type == Node::SUBSCRIPT) {
			const FSParser::SubscriptNode *subscript = static_cast<const FSParser::SubscriptNode *>(p_value);
			if (subscript->is_attribute && subscript->base != nullptr && subscript->base->type == Node::SELF) {
				identifier = subscript->attribute;
			}
		}
		return identifier != nullptr &&
				identifier->source == FSParser::IdentifierNode::MEMBER_FUNCTION &&
				!identifier->function_source_is_static;
	}

	// True when a constant expression names the tracked member: a String/StringName
	// equal to its name, or a NodePath any of whose components is its name (covering
	// `set_indexed(^"_m", v)`). The analyzer folds a constant reference
	// (`const P = "_m"`) into `reduced_value`, so a constant identifier is matched
	// as well as a raw literal.
	bool is_constant_member_name(const FSParser::ExpressionNode *p_expr) const {
		if (p_expr == nullptr || decl->identifier == nullptr || !p_expr->is_constant) {
			return false;
		}
		const Variant &value = p_expr->reduced_value;
		if (value.get_type() == Variant::STRING || value.get_type() == Variant::STRING_NAME) {
			return StringName(value) == decl->identifier->name;
		}
		if (value.get_type() == Variant::NODE_PATH) {
			const NodePath path = value;
			for (int i = 0; i < path.get_name_count(); i++) {
				if (path.get_name(i) == decl->identifier->name) {
					return true;
				}
			}
			for (int i = 0; i < path.get_subname_count(); i++) {
				if (path.get_subname(i) == decl->identifier->name) {
					return true;
				}
			}
		}
		return false;
	}

	// True when a constant string expression equals the tracked member's name,
	// recursing into array literals so `callv("set", ["_m", v])` is also matched.
	bool literal_mentions_member(const FSParser::ExpressionNode *p_expr) const {
		if (p_expr == nullptr || decl->identifier == nullptr) {
			return false;
		}
		if (is_constant_member_name(p_expr)) {
			return true;
		}
		if (p_expr->type == Node::ARRAY) {
			const FSParser::ArrayNode *array = static_cast<const FSParser::ArrayNode *>(p_expr);
			for (const FSParser::ExpressionNode *element : array->elements) {
				if (literal_mentions_member(element)) {
					return true;
				}
			}
		}
		return false;
	}

	// True when `p_call` is a dynamic reflection call (on any receiver) whose
	// arguments mention the tracked member by name, e.g. `other.set("_m", v)` or
	// `other.callv("set", ["_m", v])` where `other` may alias `self`. The
	// receiver-agnostic check covers reflection writes routed through a reference
	// the scan cannot prove distinct from `self`.
	bool reflection_call_names_member(const FSParser::CallNode *p_call) const {
		if (p_call == nullptr || p_call->callee == nullptr || decl->identifier == nullptr) {
			return false;
		}
		StringName method;
		if (p_call->callee->type == Node::IDENTIFIER) {
			method = static_cast<const FSParser::IdentifierNode *>(p_call->callee)->name;
		} else if (p_call->callee->type == Node::SUBSCRIPT) {
			const FSParser::SubscriptNode *callee = static_cast<const FSParser::SubscriptNode *>(p_call->callee);
			if (callee->is_attribute && callee->attribute != nullptr) {
				method = callee->attribute->name;
			}
		}
		if (method == StringName() || !is_dynamic_reflection_method(method)) {
			return false;
		}
		for (const FSParser::ExpressionNode *argument : p_call->arguments) {
			if (literal_mentions_member(argument)) {
				return true;
			}
		}
		return false;
	}

	// True when `p_subscript` is dynamic *property* indexing on a base that may be
	// this instance, e.g. `self["_member"]`, `other["_member"]`, or `other[prop]`
	// with a runtime key. Object subscripting indexes properties by name, so if the
	// base may alias `self` the member can be read or written here regardless of
	// whether the key is a compile-time constant. Indexing into the tracked
	// container itself, or into a concrete non-Object builtin, is a normal element
	// access and is not flagged.
	bool indexes_member_by_name(const FSParser::SubscriptNode *p_subscript) const {
		if (p_subscript == nullptr || p_subscript->is_attribute || decl->identifier == nullptr) {
			return false;
		}
		if (is_our_var(p_subscript->base) || is_self_member_subscript(p_subscript->base)) {
			return false; // Indexing into the member container itself, not the instance.
		}
		// A constant key naming the member is unsafe on any base; otherwise the base
		// must plausibly be the instance for property indexing to reach the member.
		return is_constant_member_name(p_subscript->index) || receiver_may_be_self(p_subscript->base);
	}

	void scan_value(const FSParser::ExpressionNode *p_value) {
		if (bailed || p_value == nullptr) {
			return;
		}
		if (is_our_var(p_value)) {
			bail(FSContainerInference::ESCAPES, "the variable is used in a position the inference cannot bound");
			return;
		}
		if (member_mode && is_self_reflection_reference(p_value)) {
			// Taking a bound reference to a reflection method (`var s := set`) yields a
			// callable that can later mutate the member by name (`s.call("_m", ..)`),
			// which the scan cannot follow.
			bail(FSContainerInference::ESCAPES, "a reflection method is referenced as a callable");
			return;
		}
		if (member_mode && references_overrideable_method(p_value)) {
			// Taking a bound reference to a script method (`var cb = hook`) yields a
			// callable that can later dispatch to a subclass override mutating the
			// inherited member with another type.
			bail(FSContainerInference::ESCAPES, "an overridable method is referenced as a callable");
			return;
		}
		switch (p_value->type) {
			case Node::SUBSCRIPT: {
				const FSParser::SubscriptNode *subscript = static_cast<const FSParser::SubscriptNode *>(p_value);
				if (is_self_member(subscript)) {
					// `self.member` used directly in a value position leaks a reference.
					bail(FSContainerInference::ESCAPES, "the member is used in a position the inference cannot bound");
					return;
				}
				if (is_foreign_member_access(subscript)) {
					return;
				}
				if (indexes_member_by_name(subscript)) {
					// `self["_member"]` / `other["_member"]`: dynamic property access that
					// can read or write the member through a reference that may alias self.
					bail(FSContainerInference::ESCAPES, "the member may be reached through dynamic property indexing");
					return;
				}
				if (subscript->is_attribute) {
					if (is_our_var(subscript->base)) {
						bail(FSContainerInference::ESCAPES, "the variable is accessed through an unsupported member reference");
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
					bail(FSContainerInference::ESCAPES, "the instance escapes through `self`");
				}
				break;
			case Node::CALL:
				scan_call(static_cast<const FSParser::CallNode *>(p_value), /* result_consumed */ true);
				break;
			case Node::ASSIGNMENT:
				scan_assignment(static_cast<const FSParser::AssignmentNode *>(p_value));
				break;
			case Node::ARRAY: {
				const FSParser::ArrayNode *array = static_cast<const FSParser::ArrayNode *>(p_value);
				for (const FSParser::ExpressionNode *element : array->elements) {
					scan_value(element);
				}
			} break;
			case Node::TUPLE_LITERAL: {
				const FSParser::TupleLiteralNode *tuple_literal = static_cast<const FSParser::TupleLiteralNode *>(p_value);
				for (const FSParser::ExpressionNode *element : tuple_literal->elements) {
					scan_value(element);
				}
			} break;
			case Node::DICTIONARY: {
				const FSParser::DictionaryNode *dictionary = static_cast<const FSParser::DictionaryNode *>(p_value);
				for (const FSParser::DictionaryNode::Pair &pair : dictionary->elements) {
					scan_value(pair.key);
					scan_value(pair.value);
				}
			} break;
			case Node::LAMBDA:
				scan_lambda(static_cast<const FSParser::LambdaNode *>(p_value));
				break;
			case Node::BINARY_OPERATOR: {
				const FSParser::BinaryOpNode *binary = static_cast<const FSParser::BinaryOpNode *>(p_value);
				scan_value(binary->left_operand);
				scan_value(binary->right_operand);
			} break;
			case Node::UNARY_OPERATOR: {
				const FSParser::UnaryOpNode *unary = static_cast<const FSParser::UnaryOpNode *>(p_value);
				scan_value(unary->operand);
			} break;
			case Node::TERNARY_OPERATOR: {
				const FSParser::TernaryOpNode *ternary = static_cast<const FSParser::TernaryOpNode *>(p_value);
				scan_value(ternary->condition);
				scan_value(ternary->true_expr);
				scan_value(ternary->false_expr);
			} break;
			case Node::CAST: {
				const FSParser::CastNode *cast = static_cast<const FSParser::CastNode *>(p_value);
				scan_value(cast->operand);
			} break;
			case Node::TYPE_TEST: {
				const FSParser::TypeTestNode *type_test = static_cast<const FSParser::TypeTestNode *>(p_value);
				scan_value(type_test->operand);
			} break;
			case Node::AWAIT: {
				const FSParser::AwaitNode *await = static_cast<const FSParser::AwaitNode *>(p_value);
				scan_value(await->to_await);
			} break;
			case Node::PRELOAD: {
				const FSParser::PreloadNode *preload = static_cast<const FSParser::PreloadNode *>(p_value);
				scan_value(preload->path);
			} break;
			case Node::GET_NODE:
				// `$"."` / `$""` resolves to the instance, so consuming it leaks `self`.
				if (member_mode && get_node_is_self(static_cast<const FSParser::GetNodeNode *>(p_value))) {
					bail(FSContainerInference::ESCAPES, "the instance escapes through a `$\".\"` node reference");
				}
				break;
			default:
				break;
		}
	}

	// Scans a call expression. `p_result_consumed` is true when the call's return
	// value is used as a value (so a result that may be `self` would escape) and
	// false when it is a statement whose result is discarded.
	void scan_call(const FSParser::CallNode *p_call, bool p_result_consumed) {
		if (bailed || p_call == nullptr) {
			return;
		}
		// Record any narrowed read binding passed as a typed argument before dispatch
		// branches consume the arguments.
		note_call_argument_uses(p_call);
		// A modeled method on the tracked container (`self._member.set(k, v)`) is
		// handled first: its receiver is the member itself, so the
		// dynamic-reflection/override guards below (which assume the receiver may be
		// `self`) do not apply to it.
		if (p_call->callee != nullptr && p_call->callee->type == Node::SUBSCRIPT) {
			const FSParser::SubscriptNode *callee = static_cast<const FSParser::SubscriptNode *>(p_call->callee);
			if (callee->is_attribute && (is_our_var(callee->base) || is_self_member_subscript(callee->base))) {
				scan_method_on_var(p_call, callee->attribute != nullptr ? callee->attribute->name : StringName());
				return;
			}
		}
		if (member_mode && (is_self_reflection_call(p_call) || is_signal_emit_call(p_call) || reflection_call_names_member(p_call))) {
			bail(FSContainerInference::ESCAPES, "the member may be reached through a dynamic property call");
			return;
		}
		if (member_mode && is_super_call(p_call)) {
			// A `super(...)` call dispatches upward to a base-class method in another
			// file. The base classes lie outside the subclass closure the caller
			// enumerated, so their bodies are not folded into this union and a base
			// method could mutate the inherited member with another key/value type unseen.
			bail(FSContainerInference::ESCAPES, "the member may be mutated by a base method via `super`");
			return;
		}
		if (member_mode && is_overrideable_self_method_call(p_call)) {
			// A script method on this instance can be overridden by a subclass that
			// mutates the inherited member with another key/value type. The call is sound
			// to resolve only when the closure is complete *and* the method resolves to a
			// scanned body (see the array walker for the full rationale). A call to an
			// inherited base method is not scanned and could mutate the member
			// dynamically, so it must still bail.
			if (!subclasses_complete || !resolves_to_scanned_method(p_call)) {
				bail(FSContainerInference::ESCAPES, "the member may be mutated by an overridden or inherited method");
				return;
			}
		}
		if (p_call->callee != nullptr && p_call->callee->type == Node::SUBSCRIPT) {
			const FSParser::SubscriptNode *callee = static_cast<const FSParser::SubscriptNode *>(p_call->callee);
			if (callee->is_attribute && callee->base != nullptr && callee->base->type == Node::SUBSCRIPT &&
					is_foreign_member_access(static_cast<const FSParser::SubscriptNode *>(callee->base))) {
				return; // `other.member.set(k, v)`.
			}
			// `self.method(...)` is a call on this instance (a native method, since a
			// script method already bailed above). Its arguments are scanned for leaks,
			// but if its result is consumed it may itself be `self` (e.g.
			// `self.get_node(".")`), which would escape; bail in that case.
			if (callee->is_attribute && callee->base != nullptr && callee->base->type == Node::SELF) {
				if (member_mode && p_result_consumed) {
					bail(FSContainerInference::ESCAPES, "a call on `self` may return the instance");
					return;
				}
				for (const FSParser::ExpressionNode *argument : p_call->arguments) {
					scan_value(argument);
				}
				return;
			}
		}
		scan_value(p_call->callee);
		for (const FSParser::ExpressionNode *argument : p_call->arguments) {
			scan_value(argument);
		}
	}

	void scan_lambda(const FSParser::LambdaNode *p_lambda) {
		if (bailed || p_lambda == nullptr) {
			return;
		}
		for (const FSParser::IdentifierNode *capture : p_lambda->captures) {
			if (capture != nullptr && (is_our_var(capture) || (decl->identifier != nullptr && capture->name == decl->identifier->name))) {
				bail(FSContainerInference::ESCAPES, "the variable is captured by a lambda");
				return;
			}
		}
		if (member_mode && p_lambda->use_self) {
			// In member mode a member is reached through `self`, not a capture, so a
			// lambda that uses `self` could mutate the member through a callable that
			// may be stored or invoked later in ways the analysis cannot bound.
			bail(FSContainerInference::ESCAPES, "the member may be mutated by a lambda capturing `self`");
			return;
		}
		// Scan the lambda body. In member mode it can reach the member through another
		// reference (`func(): other._member[k] = v`); in either mode it can reassign a
		// captured local bound to a narrowing read of the dictionary
		// (`var v = d["a"]; var cb = func(): v = "x"`), which the read-narrowing
		// tracker must see. The tracked variable itself never appears here --
		// capturing it already bailed above -- so scanning is safe.
		if (p_lambda->function != nullptr) {
			// A `return <binding>` inside the lambda targets the lambda's own return
			// contract, not the enclosing function's; swap it in for the lambda body.
			const DataType saved_return_type = current_return_type;
			current_return_type = p_lambda->function->get_datatype();
			scan_suite(p_lambda->function->body);
			current_return_type = saved_return_type;
		}
	}

	void scan_assignment(const FSParser::AssignmentNode *p_assignment) {
		if (bailed || p_assignment == nullptr) {
			return;
		}
		const FSParser::ExpressionNode *assignee = p_assignment->assignee;
		const bool whole_var_assignee = is_our_var(assignee) ||
				(assignee != nullptr && assignee->type == Node::SUBSCRIPT &&
						is_self_member(static_cast<const FSParser::SubscriptNode *>(assignee)));
		if (whole_var_assignee) {
			// Whole-variable (re)assignment.
			if (p_assignment->operation == FSParser::AssignmentNode::OP_NONE) {
				// `our_var = {..}` (also `self.member = {..}`).
				contribute_from_dictionary_value(p_assignment->assigned_value);
			} else {
				bail(FSContainerInference::UNPROVABLE, "the dictionary is mutated by an unsupported compound assignment");
			}
			scan_value(p_assignment->assigned_value);
			return;
		}
		if (assignee != nullptr && assignee->type == Node::SUBSCRIPT) {
			const FSParser::SubscriptNode *subscript = static_cast<const FSParser::SubscriptNode *>(assignee);
			// `other.member = value` foreign whole-member assignment.
			if (is_foreign_member_access(subscript)) {
				return;
			}
			// `self["_member"] = value` / `other["_member"] = value` dynamic write.
			if (indexes_member_by_name(subscript)) {
				bail(FSContainerInference::ESCAPES, "the member may be reached through dynamic property indexing");
				return;
			}
			// `other.member[key] = value` foreign element write.
			if (!subscript->is_attribute && subscript->base != nullptr && subscript->base->type == Node::SUBSCRIPT &&
					is_foreign_member_access(static_cast<const FSParser::SubscriptNode *>(subscript->base))) {
				return;
			}
			if (!subscript->is_attribute && (is_our_var(subscript->base) || is_self_member_subscript(subscript->base))) {
				if (p_assignment->operation == FSParser::AssignmentNode::OP_NONE) {
					// `our_var[key] = value`: both the key and the value types are stored verbatim.
					if (subscript->index != nullptr) {
						contribute_key(subscript->index->get_datatype());
					}
					if (p_assignment->assigned_value != nullptr) {
						contribute_value(p_assignment->assigned_value->get_datatype());
					}
				} else {
					// `our_var[key] op= value` writes key `typeof(key)` and stores value
					// `typeof(old_value op value)`. The key is contributed verbatim; the
					// stored value type depends on the existing value type, so it is
					// deferred to `finalize()` once that type is known.
					if (subscript->index != nullptr) {
						contribute_key(subscript->index->get_datatype());
					}
					PendingCompoundWrite write;
					write.op = p_assignment->variant_op;
					write.value = p_assignment->assigned_value != nullptr ? p_assignment->assigned_value->get_datatype() : DataType();
					pending_compound_writes.push_back(write);
				}
				scan_value(subscript->index);
				scan_value(p_assignment->assigned_value);
				return;
			}
		}
		// Assignment to some other target. Both sides are values: the variable
		// appearing on either side is an escape (e.g. `alias = our_var`).
		// A whole-variable reassignment of a local/iterator (`v = value` or
		// `v op= value`) is also noted, in case `v` was bound to a narrowing read of
		// the dictionary.
		if (const void *sink = local_sink_of(assignee)) {
			if (p_assignment->operation == FSParser::AssignmentNode::OP_NONE) {
				read_narrowing.note_reassignment(sink, p_assignment->assigned_value != nullptr ? p_assignment->assigned_value->get_datatype() : DataType());
			} else {
				read_narrowing.note_compound_reassignment(sink);
			}
		}
		// `typed_target = <binding>`: the RHS binding flows into the target's hard type.
		if (p_assignment->operation == FSParser::AssignmentNode::OP_NONE && assignee != nullptr) {
			note_typed_use(p_assignment->assigned_value, assignee->get_datatype());
		}
		scan_value(assignee);
		scan_value(p_assignment->assigned_value);
	}

	void scan_method_on_var(const FSParser::CallNode *p_call, const StringName &p_method) {
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
				bail(FSContainerInference::UNPROVABLE, vformat("'%s' is called with an unexpected number of arguments", String(p_method)));
			}
		} else if (p_method == StringName("has") || p_method == StringName("erase")) {
			// `has(key)`/`erase(key)` coerce their key argument to the key type on a
			// typed dictionary, so the call is behavior-preserving only when that key
			// already has the key type. Defer the check to `finalize()`.
			if (p_call->arguments.size() >= 1 && p_call->arguments[0] != nullptr) {
				PendingKeyValidation validation;
				validation.argument = p_call->arguments[0]->get_datatype();
				pending_key_validations.push_back(validation);
			}
		} else if (!is_safe_readonly_dictionary_method(p_method)) {
			bail(FSContainerInference::UNPROVABLE, vformat("the dictionary is used through an unmodelled method '%s'", String(p_method)));
		}
		if (bailed) {
			return;
		}
		// Even for modeled methods, arguments may themselves leak the variable.
		for (const FSParser::ExpressionNode *argument : p_call->arguments) {
			scan_value(argument);
			if (bailed) {
				return;
			}
		}
	}

	void contribute_arg_key(const FSParser::CallNode *p_call, int p_index) {
		if (p_call->arguments.size() <= p_index) {
			bail(FSContainerInference::UNPROVABLE, "a dictionary mutation is called with an unexpected number of arguments");
			return;
		}
		const FSParser::ExpressionNode *argument = p_call->arguments[p_index];
		if (argument != nullptr) {
			contribute_key(argument->get_datatype());
		}
	}

	void contribute_arg_value(const FSParser::CallNode *p_call, int p_index) {
		if (p_call->arguments.size() <= p_index) {
			bail(FSContainerInference::UNPROVABLE, "a dictionary mutation is called with an unexpected number of arguments");
			return;
		}
		const FSParser::ExpressionNode *argument = p_call->arguments[p_index];
		if (argument != nullptr) {
			contribute_value(argument->get_datatype());
		}
	}
};

// Reports why a member variable is not a safe candidate for class-wide element
// inference, or an empty string when it is eligible. A member is only considered
// when its mutation surface is bounded by the class.
//
// FoundryScript has no enforced access modifiers, so an ordinary member is part of the
// public API: another script holding a reference can do `obj.items.append(x)`,
// which the single-file analysis cannot see. The strongest "private" signal the
// language offers is the leading-underscore naming convention, so inference is
// limited to underscore-prefixed members. On top of that the member must not be
// `@export`ed (the editor and external code can assign it), must not be `static`
// (shared and assignable through the class), and must not have a custom
// setter/getter (writes flow through user code the inference does not model).
//
// Soundness boundary: FoundryScript also has no `final`, so an external subclass can
// `extends` this class and mutate the inherited member from its own methods with
// a different element type. The inference accounts for this directly: the member
// entry points accept the project-wide set of subclasses (discovered by the caller
// from the dependency closure) and fold their writers into the same union/escape
// model, matching the inherited member by name. When the caller cannot prove it
// enumerated every subclass, it passes `p_subclasses_complete == false` and the
// inference skips conservatively. The `FSVerificationHarness` re-analysis of
// the dependency closure remains a backstop, but the proposed annotation is already
// sound across the open world rather than optimistic-then-verified.
String member_disqualifier(const FSParser::VariableNode *p_member) {
	if (p_member->exported) {
		return "the member is `@export`ed, so external code can assign it";
	}
	if (p_member->is_static) {
		return "the member is `static`, so it can be assigned through the class";
	}
	if (p_member->property != FSParser::VariableNode::PROP_NONE) {
		return "the member has a custom setter or getter";
	}
	if (p_member->identifier == nullptr || !String(p_member->identifier->name).begins_with("_")) {
		return "the member is part of the public API (no leading underscore), so external code can mutate it";
	}
	return String();
}

// Drives `p_walker` over every function body in `p_class` and, recursively, its
// nested classes. A member declared on `p_class` is reachable from any of these
// methods (and a nested subclass may mutate an inherited member), so the union
// must span all of them for the inference to be sound. Other members' initializer
// expressions and inline property accessor bodies are scanned too, since one can
// alias or mutate the tracked member (e.g. `var _alias = _items` or a setter that
// appends to it); the tracked member's own initializer is skipped because it is
// contributed separately as the literal contents.
template <typename Walker>
void walk_class_methods(Walker &p_walker, const FSParser::ClassNode *p_class, const FSParser::VariableNode *p_tracked) {
	if (p_class == nullptr) {
		return;
	}
	for (const FSParser::ClassNode::Member &member : p_class->members) {
		if (p_walker.bailed) {
			return;
		}
		switch (member.type) {
			case FSParser::ClassNode::Member::FUNCTION:
				if (member.function != nullptr) {
					// A parameter default can alias the member (`func f(a = _items):`).
					for (const FSParser::ParameterNode *parameter : member.function->parameters) {
						if (parameter != nullptr) {
							p_walker.scan_initializer(parameter->initializer);
						}
					}
					// A `return <binding>` in this method targets this method's return type.
					p_walker.set_current_return_type(member.function->get_datatype());
					p_walker.scan_suite(member.function->body);
				}
				break;
			case FSParser::ClassNode::Member::VARIABLE:
				if (member.variable != nullptr) {
					if (member.variable != p_tracked) {
						p_walker.scan_initializer(member.variable->initializer);
					}
					// Inline `set`/`get` accessor bodies can mutate the tracked member, and a
					// getter can `return <binding>`; each accessor body is checked against its
					// own return contract, not whichever method was scanned last.
					if (member.variable->property == FSParser::VariableNode::PROP_INLINE) {
						if (member.variable->setter != nullptr) {
							p_walker.set_current_return_type(member.variable->setter->get_datatype());
							p_walker.scan_suite(member.variable->setter->body);
						}
						if (member.variable->getter != nullptr) {
							p_walker.set_current_return_type(member.variable->getter->get_datatype());
							p_walker.scan_suite(member.variable->getter->body);
						}
					}
				}
				break;
			case FSParser::ClassNode::Member::CONSTANT:
				if (member.constant != nullptr) {
					p_walker.scan_initializer(member.constant->initializer);
				}
				break;
			case FSParser::ClassNode::Member::CLASS:
				walk_class_methods(p_walker, member.m_class, p_tracked);
				break;
			default:
				break;
		}
	}
}

} // namespace

FSContainerInference::Result FSContainerInference::infer_local_array_element_type(
		const FSParser::VariableNode *p_decl,
		const FSParser::SuiteNode *p_function_body) {
	Result result;
	if (p_decl == nullptr || p_function_body == nullptr) {
		return result; // NOT_APPLICABLE
	}
	// Only untyped declarations are candidates; an explicit annotation is left alone.
	if (p_decl->datatype_specifier != nullptr) {
		return result;
	}
	// The initializer must be an array literal so the full initial contents are known.
	if (p_decl->initializer == nullptr || p_decl->initializer->type != FSParser::Node::ARRAY) {
		return result;
	}
	const FSParser::DataType declared = p_decl->get_datatype();
	if (declared.has_no_type() || declared.kind != FSParser::DataType::BUILTIN || declared.builtin_type != Variant::ARRAY) {
		return result;
	}
	// Already a typed `Array[T]`: the analyzer resolved the element type, so the
	// existing rendering path is sufficient.
	if (declared.has_container_element_type(0)) {
		return result;
	}

	ElementInferenceWalker walker(p_decl);
	if (p_function_body->parent_function != nullptr) {
		walker.set_current_return_type(p_function_body->parent_function->get_datatype());
	}
	walker.contribute_from_array_value(p_decl->initializer);
	walker.scan_suite(p_function_body);
	walker.finalize();

	if (walker.bailed) {
		result.outcome = walker.bail_outcome;
		result.detail = walker.bail_detail;
		return result;
	}
	if (!walker.has_element) {
		result.outcome = NO_EVIDENCE;
		return result;
	}

	FSParser::DataType inferred = declared;
	inferred.set_container_element_type(0, walker.element_type);
	result.outcome = INFERRED;
	result.element_type = inferred;
	return result;
}

FSContainerInference::Result FSContainerInference::infer_local_dictionary_element_type(
		const FSParser::VariableNode *p_decl,
		const FSParser::SuiteNode *p_function_body) {
	Result result;
	if (p_decl == nullptr || p_function_body == nullptr) {
		return result; // NOT_APPLICABLE
	}
	// Only untyped declarations are candidates; an explicit annotation is left alone.
	if (p_decl->datatype_specifier != nullptr) {
		return result;
	}
	// The initializer must be a dictionary literal so the full initial contents are known.
	if (p_decl->initializer == nullptr || p_decl->initializer->type != FSParser::Node::DICTIONARY) {
		return result;
	}
	const FSParser::DataType declared = p_decl->get_datatype();
	if (declared.has_no_type() || declared.kind != FSParser::DataType::BUILTIN || declared.builtin_type != Variant::DICTIONARY) {
		return result;
	}
	// Already a typed `Dictionary[K, V]`: the analyzer resolved both element types,
	// so the existing rendering path is sufficient.
	if (declared.has_container_element_type(0) && declared.has_container_element_type(1)) {
		return result;
	}

	DictionaryInferenceWalker walker(p_decl);
	if (p_function_body->parent_function != nullptr) {
		walker.set_current_return_type(p_function_body->parent_function->get_datatype());
	}
	walker.contribute_from_dictionary_value(p_decl->initializer);
	walker.scan_suite(p_function_body);
	walker.finalize();

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

	FSParser::DataType inferred = declared;
	inferred.set_container_element_type(0, walker.key_type);
	inferred.set_container_element_type(1, walker.value_type);
	result.outcome = INFERRED;
	result.element_type = inferred;
	return result;
}

FSContainerInference::Result FSContainerInference::infer_member_array_element_type(
		const FSParser::VariableNode *p_member,
		const FSParser::ClassNode *p_class,
		const Vector<const FSParser::ClassNode *> &p_subclasses,
		bool p_subclasses_complete) {
	Result result;
	if (p_member == nullptr || p_class == nullptr) {
		return result; // NOT_APPLICABLE
	}
	// Only untyped declarations are candidates; an explicit annotation is left alone.
	if (p_member->datatype_specifier != nullptr) {
		return result;
	}
	// The initializer must be an array literal so the full initial contents are known.
	if (p_member->initializer == nullptr || p_member->initializer->type != FSParser::Node::ARRAY) {
		return result;
	}
	const FSParser::DataType declared = p_member->get_datatype();
	if (declared.has_no_type() || declared.kind != FSParser::DataType::BUILTIN || declared.builtin_type != Variant::ARRAY) {
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
	// FoundryScript has no `final`: an external subclass can mutate the inherited member.
	// When the caller could not prove it enumerated every subclass in the project,
	// the open world is unbounded and inference must skip conservatively.
	if (!p_subclasses_complete) {
		result.outcome = ESCAPES;
		result.detail = "the set of subclasses that could mutate the member is not bounded";
		return result;
	}

	ElementInferenceWalker walker(p_member, /* member_mode */ true);
	// The early return above guarantees the closure is complete, so an overridable
	// `self` call that targets a method declared in the declaring class resolves to a
	// scanned body rather than forcing a conservative bail.
	walker.set_override_resolution(/* subclasses_complete */ true, p_class);
	walker.contribute_from_array_value(p_member->initializer);
	// The literal's contents are contributed above, but its sub-expressions can
	// still leak the instance (e.g. `[register(self)]`); scan them for escapes.
	walker.scan_initializer(p_member->initializer);
	walk_class_methods(walker, p_class, p_member);
	// Fold every project-wide subclass writer into the same union/escape model so
	// the proposed annotation is sound across the open world, not optimistic. The
	// subclass is parsed in a separate tree, so its references to the inherited
	// member match by name rather than by the base member node's pointer.
	for (const FSParser::ClassNode *subclass : p_subclasses) {
		if (walker.bailed) {
			break;
		}
		walker.set_match_member_by_name(true);
		walk_class_methods(walker, subclass, p_member);
	}
	walker.set_match_member_by_name(false);
	walker.finalize();

	if (walker.bailed) {
		result.outcome = walker.bail_outcome;
		result.detail = walker.bail_detail;
		return result;
	}
	if (!walker.has_element) {
		result.outcome = NO_EVIDENCE;
		return result;
	}

	FSParser::DataType inferred = declared;
	inferred.set_container_element_type(0, walker.element_type);
	result.outcome = INFERRED;
	result.element_type = inferred;
	return result;
}

FSContainerInference::Result FSContainerInference::infer_member_dictionary_element_type(
		const FSParser::VariableNode *p_member,
		const FSParser::ClassNode *p_class,
		const Vector<const FSParser::ClassNode *> &p_subclasses,
		bool p_subclasses_complete) {
	Result result;
	if (p_member == nullptr || p_class == nullptr) {
		return result; // NOT_APPLICABLE
	}
	if (p_member->datatype_specifier != nullptr) {
		return result;
	}
	if (p_member->initializer == nullptr || p_member->initializer->type != FSParser::Node::DICTIONARY) {
		return result;
	}
	const FSParser::DataType declared = p_member->get_datatype();
	if (declared.has_no_type() || declared.kind != FSParser::DataType::BUILTIN || declared.builtin_type != Variant::DICTIONARY) {
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
	// FoundryScript has no `final`: an external subclass can mutate the inherited member.
	// When the caller could not prove it enumerated every subclass in the project,
	// the open world is unbounded and inference must skip conservatively.
	if (!p_subclasses_complete) {
		result.outcome = ESCAPES;
		result.detail = "the set of subclasses that could mutate the member is not bounded";
		return result;
	}

	DictionaryInferenceWalker walker(p_member, /* member_mode */ true);
	// The early return above guarantees the closure is complete, so an overridable
	// `self` call that targets a method declared in the declaring class resolves to a
	// scanned body rather than forcing a conservative bail.
	walker.set_override_resolution(/* subclasses_complete */ true, p_class);
	walker.contribute_from_dictionary_value(p_member->initializer);
	// The literal's contents are contributed above, but its sub-expressions can
	// still leak the instance (e.g. `{0: register(self)}`); scan them for escapes.
	walker.scan_initializer(p_member->initializer);
	walk_class_methods(walker, p_class, p_member);
	// Fold every project-wide subclass writer into the same union/escape model so
	// the proposed annotation is sound across the open world (see the array path).
	for (const FSParser::ClassNode *subclass : p_subclasses) {
		if (walker.bailed) {
			break;
		}
		walker.set_match_member_by_name(true);
		walk_class_methods(walker, subclass, p_member);
	}
	walker.set_match_member_by_name(false);
	walker.finalize();

	if (walker.bailed) {
		result.outcome = walker.bail_outcome;
		result.detail = walker.bail_detail;
		return result;
	}
	if (!walker.has_key || !walker.has_value) {
		result.outcome = NO_EVIDENCE;
		return result;
	}

	FSParser::DataType inferred = declared;
	inferred.set_container_element_type(0, walker.key_type);
	inferred.set_container_element_type(1, walker.value_type);
	result.outcome = INFERRED;
	result.element_type = inferred;
	return result;
}

#endif // TOOLS_ENABLED
