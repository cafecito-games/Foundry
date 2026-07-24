/**************************************************************************/
/*  fs_analyzer_flow_finality.cpp                                         */
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

#include "fs_analyzer.h"

#include "foundry_script.h"

FSAnalyzer::FlowFinalityContext::FlowFinalityContext(FSAnalyzer *p_analyzer) :
		analyzer(p_analyzer) {
}

FSAnalyzer::FlowFinalityContext::FlowNarrowingScope::FlowNarrowingScope(FlowFinalityContext &p_context, bool p_track_captured_sources) {
	context = &p_context;
	if (context == nullptr) {
		return;
	}
	previous_flow_narrowed_types = context->flow_narrowed_types;
	restore_captured_sources = p_track_captured_sources;
	if (restore_captured_sources) {
		previous_flow_narrowing_captured_sources = context->flow_narrowing_captured_sources;
		context->flow_narrowing_captured_sources.clear();
	}
	context->flow_narrowed_types.clear();
}

FSAnalyzer::FlowFinalityContext::FlowNarrowingScope::~FlowNarrowingScope() {
	if (context == nullptr) {
		return;
	}
	context->flow_narrowed_types = previous_flow_narrowed_types;
	if (restore_captured_sources) {
		context->flow_narrowing_captured_sources = previous_flow_narrowing_captured_sources;
	}
}

FSAnalyzer::FlowFinalityContext::FlattenedTraitFinalNodesScope::FlattenedTraitFinalNodesScope(FlowFinalityContext &p_context) {
	context = &p_context;
	if (context != nullptr) {
		context->flattened_trait_final_nodes.clear();
	}
}

void FSAnalyzer::FlowFinalityContext::FlattenedTraitFinalNodesScope::insert(const FSParser::VariableNode *p_variable) {
	if (context != nullptr && p_variable != nullptr) {
		context->flattened_trait_final_nodes.insert(p_variable);
	}
}

FSAnalyzer::FlowFinalityContext::FlattenedTraitFinalNodesScope::~FlattenedTraitFinalNodesScope() {
	if (context != nullptr) {
		context->flattened_trait_final_nodes.clear();
	}
}

void FSAnalyzer::FlowFinalityContext::merge_final_assignment_branches(const FinalAssignmentState &p_first, const FinalAssignmentState &p_second, FinalAssignmentState &r_out) {
	if (!p_first.reachable && !p_second.reachable) {
		// Nothing reaches the join; both sets are empty there. Downstream is unreachable anyway.
		r_out.assigned.clear();
		r_out.maybe_assigned.clear();
		r_out.reachable = false;
		return;
	}
	if (!p_first.reachable) {
		// Only the second branch reaches the join, so it alone determines both sets.
		r_out = p_second;
		return;
	}
	if (!p_second.reachable) {
		r_out = p_first;
		return;
	}
	r_out.reachable = true;
	// Definitely assigned only if assigned on both branches (intersection).
	r_out.assigned.clear();
	for (const FSParser::VariableNode *variable : p_first.assigned) {
		if (p_second.assigned.has(variable)) {
			r_out.assigned.insert(variable);
		}
	}
	// Maybe assigned if assigned on either reachable branch (union).
	r_out.maybe_assigned = p_first.maybe_assigned;
	for (const FSParser::VariableNode *variable : p_second.maybe_assigned) {
		r_out.maybe_assigned.insert(variable);
	}
}

// Mirrors `FSCompiler::_is_flattenable_trait_member`: the member kinds a trait contributes
// to each implementing class. Name-claiming for the first-trait-wins shadowing below must use the
// exact same predicate the compiler uses so the analyzer tracks the same flattened slots.
static bool _is_flattenable_trait_member(const FSParser::ClassNode::Member &p_member) {
	switch (p_member.type) {
		case FSParser::ClassNode::Member::VARIABLE:
		case FSParser::ClassNode::Member::CONSTANT:
		case FSParser::ClassNode::Member::ENUM:
		case FSParser::ClassNode::Member::ENUM_VALUE:
		case FSParser::ClassNode::Member::SIGNAL:
			return true;
		case FSParser::ClassNode::Member::FUNCTION:
			return p_member.function != nullptr && !p_member.function->is_abstract;
		default:
			return false;
	}
}

// Collects the members an implementing class receives from its applied traits, in the order the
// compiler flattens them. Trait members are merged into the class at compile time
// (`FSCompiler::_collect_flattened_trait_members`), so they never appear in `p_class->members`
// when the final-enforcement passes run; without them a trait-supplied final is neither write-once
// enforced nor recognized as having a legal `_init`/`_static_init` slot on the implementer, and a
// concrete trait method or initializer that mutates such a final is never scanned. The shadowing
// here matches the compiler exactly: a trait member is dropped when the implementing class or any
// base already declares its name, and the first trait to provide a name wins.
static void _collect_flattened_trait_members(const FSParser::ClassNode *p_class,
		LocalVector<const FSParser::ClassNode::Member *> &r_members) {
	if (p_class->resolved_traits.is_empty()) {
		return;
	}

	HashSet<StringName> defined;
	for (const FSParser::ClassNode *owner = p_class; owner != nullptr; owner = owner->base_type.class_type) {
		for (const FSParser::ClassNode::Member &member : owner->members) {
			const StringName name = member.get_name();
			if (name != StringName()) {
				defined.insert(name);
			}
		}
	}

	for (FSParser::ClassNode *trait : p_class->resolved_traits) {
		if (trait == nullptr) {
			continue;
		}
		for (const FSParser::ClassNode::Member &member : trait->members) {
			if (!_is_flattenable_trait_member(member)) {
				continue;
			}
			const StringName name = member.get_name();
			if (name == StringName() || defined.has(name)) {
				continue;
			}
			defined.insert(name);
			r_members.push_back(&member);
		}
	}
}

// Enforces write-once semantics for `final` member variables of `p_class`: each must be
// assigned exactly once, in its declaration initializer or definitely on every `_init()` path,
// and never reassigned or read before assignment. Static and local finals are handled elsewhere.
void FSAnalyzer::FlowFinalityContext::check_final_member_assignments(FSParser::ClassNode *p_class) {
	analyzer->require_completed_analyzer_phase(AnalyzerPhase::BODY_EXPRESSION_CALLABLE_SIGNAL, AnalyzerPhase::FLOW_FINALITY_INVARIANTS);

	if (p_class->is_trait) {
		// A trait's members are flattened into and checked on each implementing class.
		return;
	}

	HashSet<const FSParser::VariableNode *> finals;
	HashMap<StringName, const FSParser::VariableNode *> finals_by_name;
	FSParser::FunctionNode *init_function = nullptr;
	// True when the legal assignment slot is a flattened trait `_init`; its body must then be analyzed
	// with name-based (per-implementer) resolution, like the other flattened trait bodies.
	bool init_function_from_trait = false;

	for (int i = 0; i < p_class->members.size(); i++) {
		const FSParser::ClassNode::Member &member = p_class->members[i];
		if (member.type == FSParser::ClassNode::Member::VARIABLE) {
			FSParser::VariableNode *variable = member.variable;
			if (variable->is_final && !variable->is_static) {
				if (variable->property != FSParser::VariableNode::PROP_NONE) {
					// A getter/setter property has no single stored slot to write once, and its
					// setter would make the value mutable, contradicting `final`.
					analyzer->push_error(vformat(R"(Final variable "%s" cannot declare a getter or setter.)", variable->identifier->name), variable);
				} else if (variable->onready) {
					// An `@onready` initializer runs in `_ready()`, after `_init()`, so it falls
					// outside the declaration/`_init` assignment slot this analysis reasons about.
					analyzer->push_error(vformat(R"(Final variable "%s" cannot be annotated with "@onready".)", variable->identifier->name), variable);
				} else {
					finals.insert(variable);
					finals_by_name[variable->identifier->name] = variable;
				}
			}
		} else if (member.type == FSParser::ClassNode::Member::FUNCTION) {
			if (member.function->identifier != nullptr && member.function->identifier->name == SNAME("_init")) {
				init_function = member.function;
			}
		}
	}

	// Trait members flatten into this class at compile time, so they never appear in `p_class->members`
	// here. Mirror that flattening and apply the same member checks, so a trait-supplied final is an
	// owned write-once slot whose legal write site is this class's own `_init`, exactly as for a
	// directly declared final. The same property/`@onready` misuse is rejected here too, since the
	// trait's own pass returns before those checks run.
	LocalVector<const FSParser::ClassNode::Member *> trait_members;
	_collect_flattened_trait_members(p_class, trait_members);
	for (const FSParser::ClassNode::Member *member_ptr : trait_members) {
		const FSParser::ClassNode::Member &member = *member_ptr;
		if (member.type == FSParser::ClassNode::Member::VARIABLE) {
			FSParser::VariableNode *variable = member.variable;
			if (variable->is_final && !variable->is_static) {
				if (variable->property != FSParser::VariableNode::PROP_NONE) {
					analyzer->push_error(vformat(R"(Final variable "%s" cannot declare a getter or setter.)", variable->identifier->name), variable);
				} else if (variable->onready) {
					analyzer->push_error(vformat(R"(Final variable "%s" cannot be annotated with "@onready".)", variable->identifier->name), variable);
				} else {
					finals.insert(variable);
					finals_by_name[variable->identifier->name] = variable;
				}
			}
		} else if (member.type == FSParser::ClassNode::Member::FUNCTION) {
			// A trait can supply `_init` when the implementer declares none; it then becomes the legal
			// assignment slot. The implementer's own `_init` (found above) takes precedence.
			if (init_function == nullptr && member.function->identifier != nullptr && member.function->identifier->name == SNAME("_init")) {
				init_function = member.function;
				init_function_from_trait = true;
			}
		}
	}

	// A bare or `self` reference in a flattened trait body resolves against the trait's own member, so
	// its finality is stale when the implementer shadows that slot; record every final node any applied
	// trait declares so such a reference can be told apart from a reliable inherited final reached
	// through the trait's base constraint.
	FlattenedTraitFinalNodesScope trait_final_nodes(*this);
	for (const FSParser::ClassNode *trait : p_class->resolved_traits) {
		if (trait == nullptr) {
			continue;
		}
		for (const FSParser::ClassNode::Member &member : trait->members) {
			if (member.type == FSParser::ClassNode::Member::VARIABLE && member.variable->is_final && !member.variable->is_static) {
				trait_final_nodes.insert(member.variable);
			}
		}
	}

	// Reject every assignment to a final outside its legal slot. This runs for every class (even
	// one with no finals of its own) because detection is global: a write to another class's or an
	// inherited final from any method/initializer here must still be caught. The legal slot is this
	// class's own final, on `self`, lexically in its `_init`.
	for (int i = 0; i < p_class->members.size(); i++) {
		const FSParser::ClassNode::Member &member = p_class->members[i];
		if (member.type == FSParser::ClassNode::Member::FUNCTION) {
			FSParser::FunctionNode *function = member.function;
			scan_illegal_final_writes(function->body, finals, finals_by_name, FinalAssignmentScope::INSTANCE_MEMBER, function == init_function);
		} else if (member.type == FSParser::ClassNode::Member::ENUM && member.m_enum != nullptr) {
			for (FSParser::FunctionNode *function : member.m_enum->functions) {
				if (function != nullptr) {
					scan_illegal_final_writes(function->body, finals, finals_by_name, FinalAssignmentScope::INSTANCE_MEMBER, false);
				}
			}
		} else if (member.type == FSParser::ClassNode::Member::VARIABLE) {
			// A member initializer runs in the constructor prologue, not in the `_init` body, so a
			// final written from a lambda nested in it is outside the legal slot.
			scan_illegal_final_writes(member.variable->initializer, finals, finals_by_name, FinalAssignmentScope::INSTANCE_MEMBER, false);
			// Only an inline property owns its accessor bodies; a `get = func` / `set = func` property
			// points at separately declared methods (already scanned above) and the `getter`/`setter`
			// union members instead hold identifier pointers.
			if (member.variable->property == FSParser::VariableNode::PROP_INLINE) {
				if (member.variable->getter != nullptr) {
					scan_illegal_final_writes(member.variable->getter->body, finals, finals_by_name, FinalAssignmentScope::INSTANCE_MEMBER, false);
				}
				if (member.variable->setter != nullptr) {
					scan_illegal_final_writes(member.variable->setter->body, finals, finals_by_name, FinalAssignmentScope::INSTANCE_MEMBER, false);
				}
			}
		}
	}

	// The same scan over the flattened trait bodies: a concrete trait method, initializer, or accessor
	// is compiled into this class, so a write it makes to a trait-supplied final must be caught here.
	for (const FSParser::ClassNode::Member *member_ptr : trait_members) {
		const FSParser::ClassNode::Member &member = *member_ptr;
		if (member.type == FSParser::ClassNode::Member::FUNCTION) {
			FSParser::FunctionNode *function = member.function;
			scan_illegal_final_writes(function->body, finals, finals_by_name, FinalAssignmentScope::INSTANCE_MEMBER, function == init_function, true);
		} else if (member.type == FSParser::ClassNode::Member::VARIABLE) {
			scan_illegal_final_writes(member.variable->initializer, finals, finals_by_name, FinalAssignmentScope::INSTANCE_MEMBER, false, true);
			if (member.variable->property == FSParser::VariableNode::PROP_INLINE) {
				if (member.variable->getter != nullptr) {
					scan_illegal_final_writes(member.variable->getter->body, finals, finals_by_name, FinalAssignmentScope::INSTANCE_MEMBER, false, true);
				}
				if (member.variable->setter != nullptr) {
					scan_illegal_final_writes(member.variable->setter->body, finals, finals_by_name, FinalAssignmentScope::INSTANCE_MEMBER, false, true);
				}
			}
		}
	}

	if (finals.is_empty()) {
		return;
	}

	// Member initializers evaluate in declaration order before the `_init` body, so thread the
	// assignment state through them: an initialized final fills its slot when its declaration
	// runs, and any initializer (final or not) that reads a still-blank final is a use-before
	// assignment. The resulting state seeds the `_init` body walk.
	FinalAssignmentState init_state;
	LocalVector<const FSParser::VariableNode *> blank_finals;
	for (int i = 0; i < p_class->members.size(); i++) {
		const FSParser::ClassNode::Member &member = p_class->members[i];
		if (member.type != FSParser::ClassNode::Member::VARIABLE) {
			continue;
		}
		FSParser::VariableNode *variable = member.variable;
		// Any member initializer (including a property variable's) runs before the `_init` body, so
		// reading a still-blank final from one is a use-before-assignment.
		if (variable->initializer != nullptr) {
			check_final_reads_in_expression(variable->initializer, finals, finals_by_name, FinalAssignmentScope::INSTANCE_MEMBER, init_state);
		}
		// A property variable is never itself a tracked final (those are rejected earlier).
		if (!finals.has(variable)) {
			continue;
		}
		if (variable->initializer != nullptr) {
			init_state.assigned.insert(variable);
			init_state.maybe_assigned.insert(variable);
		} else {
			blank_finals.push_back(variable);
		}
	}

	// Trait member initializers run after this class's own member initializers (the compiler appends
	// flattened trait members), so thread their state in after the loop above — including non-final
	// trait variables, whose initializers can read a still-blank trait final before `_init` fills it.
	for (const FSParser::ClassNode::Member *member_ptr : trait_members) {
		const FSParser::ClassNode::Member &member = *member_ptr;
		if (member.type != FSParser::ClassNode::Member::VARIABLE) {
			continue;
		}
		FSParser::VariableNode *variable = member.variable;
		if (variable->initializer != nullptr) {
			check_final_reads_in_expression(variable->initializer, finals, finals_by_name, FinalAssignmentScope::INSTANCE_MEMBER, init_state, true);
		}
		if (!finals.has(variable)) {
			continue;
		}
		if (variable->initializer != nullptr) {
			init_state.assigned.insert(variable);
			init_state.maybe_assigned.insert(variable);
		} else {
			blank_finals.push_back(variable);
		}
	}

	// Run the definite-assignment pass over `_init`: it reports double-assignment of any tracked
	// final (initialized or blank) and reads before assignment, so it runs whenever a final exists.
	// The pass also rejects any `return` that leaves a blank final unassigned (an escape point), so
	// the only check left here is the final fall-off-the-end exit.
	if (init_function != nullptr) {
		// A `_init` parameter's default value is evaluated before the body runs, when no blank
		// final is assigned yet, so reading one through an omitted default is use-before-assignment.
		for (int i = 0; i < init_function->parameters.size(); i++) {
			check_final_reads_in_expression(init_function->parameters[i]->initializer, finals, finals_by_name, FinalAssignmentScope::INSTANCE_MEMBER, init_state, init_function_from_trait);
		}

		HashSet<const FSParser::VariableNode *> assigned_anywhere;
		analyze_final_definite_assignment_suite(init_function->body, finals, finals_by_name, FinalAssignmentScope::INSTANCE_MEMBER, init_state, assigned_anywhere, init_function_from_trait);
		if (init_state.reachable) {
			// `_init` can fall off its end: the final must be assigned on every path that reaches it.
			// (Paths that exit early via `return` are checked at the return itself; a path that aborts
			// via `@noreturn`/`push_fatal` never lets the object escape, so it carries no obligation.)
			for (const FSParser::VariableNode *variable : blank_finals) {
				if (!init_state.assigned.has(variable)) {
					analyzer->push_error(vformat(R"*(Final variable "%s" must be definitely assigned in its declaration or in "_init()".)*", variable->identifier->name), variable);
				}
			}
		}
	} else {
		for (const FSParser::VariableNode *variable : blank_finals) {
			analyzer->push_error(vformat(R"*(Final variable "%s" must be definitely assigned in its declaration or in "_init()".)*", variable->identifier->name), variable);
		}
	}
}

// Enforces write-once semantics for `final static var` of `p_class`. The single slot is filled by
// the declaration initializer or, for a blank static final, definitely on every path of
// `static func _static_init()`; otherwise an initializer is required. This mirrors
// `check_final_member_assignments` but scopes the flow analysis to static initialization and uses
// the static-variable assignment form rather than `self`-relative member access.
void FSAnalyzer::FlowFinalityContext::check_final_static_assignments(FSParser::ClassNode *p_class) {
	analyzer->require_completed_analyzer_phase(AnalyzerPhase::BODY_EXPRESSION_CALLABLE_SIGNAL, AnalyzerPhase::FLOW_FINALITY_INVARIANTS);

	if (p_class->is_trait) {
		// A trait's members are flattened into and checked on each implementing class.
		return;
	}

	HashSet<const FSParser::VariableNode *> finals;
	HashMap<StringName, const FSParser::VariableNode *> finals_by_name;
	FSParser::FunctionNode *static_init_function = nullptr;
	// True when the legal static slot is a flattened trait `_static_init`; its body is then analyzed
	// with name-based (per-implementer) resolution, like the other flattened trait bodies.
	bool static_init_function_from_trait = false;

	for (int i = 0; i < p_class->members.size(); i++) {
		const FSParser::ClassNode::Member &member = p_class->members[i];
		if (member.type == FSParser::ClassNode::Member::VARIABLE) {
			FSParser::VariableNode *variable = member.variable;
			if (variable->is_final && variable->is_static) {
				if (variable->property != FSParser::VariableNode::PROP_NONE) {
					// A getter/setter property has no single stored slot to write once, and its
					// setter would make the value mutable, contradicting `final`.
					analyzer->push_error(vformat(R"(Final variable "%s" cannot declare a getter or setter.)", variable->identifier->name), variable);
				} else {
					finals.insert(variable);
					finals_by_name[variable->identifier->name] = variable;
				}
			}
		} else if (member.type == FSParser::ClassNode::Member::FUNCTION) {
			if (member.function->identifier != nullptr && member.function->identifier->name == SNAME("_static_init")) {
				static_init_function = member.function;
			}
		}
	}

	// Trait members flatten into this class's static storage at compile time, so mirror that flattening
	// and apply the same checks: a trait-supplied `final static var` becomes an owned slot whose legal
	// write site is this class's own `_static_init`, and the same property misuse is rejected here
	// since the trait's own pass returns first.
	LocalVector<const FSParser::ClassNode::Member *> trait_members;
	_collect_flattened_trait_members(p_class, trait_members);
	for (const FSParser::ClassNode::Member *member_ptr : trait_members) {
		const FSParser::ClassNode::Member &member = *member_ptr;
		if (member.type == FSParser::ClassNode::Member::VARIABLE) {
			FSParser::VariableNode *variable = member.variable;
			if (variable->is_final && variable->is_static) {
				if (variable->property != FSParser::VariableNode::PROP_NONE) {
					analyzer->push_error(vformat(R"(Final variable "%s" cannot declare a getter or setter.)", variable->identifier->name), variable);
				} else {
					finals.insert(variable);
					finals_by_name[variable->identifier->name] = variable;
				}
			}
		} else if (member.type == FSParser::ClassNode::Member::FUNCTION) {
			// A trait can supply `_static_init` when the implementer declares none; it then becomes the
			// legal static-assignment slot. The implementer's own `_static_init` takes precedence.
			if (static_init_function == nullptr && member.function->identifier != nullptr && member.function->identifier->name == SNAME("_static_init")) {
				static_init_function = member.function;
				static_init_function_from_trait = true;
			}
		}
	}

	// Record every static final node any applied trait declares (including ones the implementer
	// shadows) so a bare/`self` reference to a trait-supplied static final's stale slot is resolved by
	// name while a reliable inherited static final is handled by the normal resolution (see the member
	// pass).
	FlattenedTraitFinalNodesScope trait_final_nodes(*this);
	for (const FSParser::ClassNode *trait : p_class->resolved_traits) {
		if (trait == nullptr) {
			continue;
		}
		for (const FSParser::ClassNode::Member &member : trait->members) {
			if (member.type == FSParser::ClassNode::Member::VARIABLE && member.variable->is_final && member.variable->is_static) {
				trait_final_nodes.insert(member.variable);
			}
		}
	}

	// Reject every assignment to a static final outside its legal slot (this class's own static
	// final, lexically in `_static_init`). This runs for every class so a write to another class's
	// or an inherited static final from any method here is still caught.
	for (int i = 0; i < p_class->members.size(); i++) {
		const FSParser::ClassNode::Member &member = p_class->members[i];
		if (member.type == FSParser::ClassNode::Member::FUNCTION) {
			FSParser::FunctionNode *function = member.function;
			scan_illegal_final_writes(function->body, finals, finals_by_name, FinalAssignmentScope::STATIC_MEMBER, function == static_init_function);
		} else if (member.type == FSParser::ClassNode::Member::ENUM && member.m_enum != nullptr) {
			for (FSParser::FunctionNode *function : member.m_enum->functions) {
				if (function != nullptr) {
					scan_illegal_final_writes(function->body, finals, finals_by_name, FinalAssignmentScope::STATIC_MEMBER, false);
				}
			}
		} else if (member.type == FSParser::ClassNode::Member::VARIABLE) {
			scan_illegal_final_writes(member.variable->initializer, finals, finals_by_name, FinalAssignmentScope::STATIC_MEMBER, false);
			if (member.variable->property == FSParser::VariableNode::PROP_INLINE) {
				if (member.variable->getter != nullptr) {
					scan_illegal_final_writes(member.variable->getter->body, finals, finals_by_name, FinalAssignmentScope::STATIC_MEMBER, false);
				}
				if (member.variable->setter != nullptr) {
					scan_illegal_final_writes(member.variable->setter->body, finals, finals_by_name, FinalAssignmentScope::STATIC_MEMBER, false);
				}
			}
		}
	}

	// The same scan over the flattened trait bodies: a concrete trait method or static initializer
	// compiled into this class may write a trait-supplied static final outside its slot.
	for (const FSParser::ClassNode::Member *member_ptr : trait_members) {
		const FSParser::ClassNode::Member &member = *member_ptr;
		if (member.type == FSParser::ClassNode::Member::FUNCTION) {
			FSParser::FunctionNode *function = member.function;
			scan_illegal_final_writes(function->body, finals, finals_by_name, FinalAssignmentScope::STATIC_MEMBER, function == static_init_function, true);
		} else if (member.type == FSParser::ClassNode::Member::VARIABLE) {
			scan_illegal_final_writes(member.variable->initializer, finals, finals_by_name, FinalAssignmentScope::STATIC_MEMBER, false, true);
			if (member.variable->property == FSParser::VariableNode::PROP_INLINE) {
				if (member.variable->getter != nullptr) {
					scan_illegal_final_writes(member.variable->getter->body, finals, finals_by_name, FinalAssignmentScope::STATIC_MEMBER, false, true);
				}
				if (member.variable->setter != nullptr) {
					scan_illegal_final_writes(member.variable->setter->body, finals, finals_by_name, FinalAssignmentScope::STATIC_MEMBER, false, true);
				}
			}
		}
	}

	if (finals.is_empty()) {
		return;
	}

	// Only static var initializers participate in static initialization, evaluating in declaration
	// order before `_static_init`. Instance var initializers run later, at instance construction,
	// once every static final is already assigned, so they are not part of this flow and may freely
	// read static finals.
	FinalAssignmentState init_state;
	LocalVector<const FSParser::VariableNode *> blank_finals;
	for (int i = 0; i < p_class->members.size(); i++) {
		const FSParser::ClassNode::Member &member = p_class->members[i];
		if (member.type != FSParser::ClassNode::Member::VARIABLE) {
			continue;
		}
		FSParser::VariableNode *variable = member.variable;
		if (!variable->is_static) {
			continue;
		}
		if (variable->initializer != nullptr) {
			check_final_reads_in_expression(variable->initializer, finals, finals_by_name, FinalAssignmentScope::STATIC_MEMBER, init_state);
		}
		if (!finals.has(variable)) {
			continue;
		}
		if (variable->initializer != nullptr) {
			init_state.assigned.insert(variable);
			init_state.maybe_assigned.insert(variable);
		} else {
			blank_finals.push_back(variable);
		}
	}

	// Trait static finals flatten into this class's static initialization, after its own static var
	// initializers; thread their state in here (including non-final trait static vars, whose
	// initializers can read a still-blank static final) so an initialized trait static fills its slot
	// and a blank one must be definitely assigned in `_static_init`.
	for (const FSParser::ClassNode::Member *member_ptr : trait_members) {
		const FSParser::ClassNode::Member &member = *member_ptr;
		if (member.type != FSParser::ClassNode::Member::VARIABLE) {
			continue;
		}
		FSParser::VariableNode *variable = member.variable;
		if (!variable->is_static) {
			continue;
		}
		if (variable->initializer != nullptr) {
			check_final_reads_in_expression(variable->initializer, finals, finals_by_name, FinalAssignmentScope::STATIC_MEMBER, init_state, true);
		}
		if (!finals.has(variable)) {
			continue;
		}
		if (variable->initializer != nullptr) {
			init_state.assigned.insert(variable);
			init_state.maybe_assigned.insert(variable);
		} else {
			blank_finals.push_back(variable);
		}
	}

	if (static_init_function != nullptr) {
		for (int i = 0; i < static_init_function->parameters.size(); i++) {
			check_final_reads_in_expression(static_init_function->parameters[i]->initializer, finals, finals_by_name, FinalAssignmentScope::STATIC_MEMBER, init_state, static_init_function_from_trait);
		}

		HashSet<const FSParser::VariableNode *> assigned_anywhere;
		analyze_final_definite_assignment_suite(static_init_function->body, finals, finals_by_name, FinalAssignmentScope::STATIC_MEMBER, init_state, assigned_anywhere, static_init_function_from_trait);
		if (init_state.reachable) {
			// A `return` that leaves a blank static final unassigned is reported at the return itself;
			// the only check left here is the fall-off-the-end exit of `_static_init()`.
			for (const FSParser::VariableNode *variable : blank_finals) {
				if (!init_state.assigned.has(variable)) {
					analyzer->push_error(vformat(R"*(Final variable "%s" must be definitely assigned in its declaration or in "_static_init()".)*", variable->identifier->name), variable);
				}
			}
		}
	} else {
		for (const FSParser::VariableNode *variable : blank_finals) {
			analyzer->push_error(vformat(R"*(Final variable "%s" must be definitely assigned in its declaration or in "_static_init()".)*", variable->identifier->name), variable);
		}
	}
}

// Enforces write-once semantics for `final var` locals across every function (and nested lambda) of
// `p_class`. Each local final must be definitely assigned exactly once before use; a declaration
// initializer fills the slot immediately, a blank `final var x` stays open until a single later
// assignment. Use-before-assignment and reassignment are reported by the shared engine.
void FSAnalyzer::FlowFinalityContext::check_final_local_assignments(FSParser::ClassNode *p_class) {
	analyzer->require_completed_analyzer_phase(AnalyzerPhase::BODY_EXPRESSION_CALLABLE_SIGNAL, AnalyzerPhase::FLOW_FINALITY_INVARIANTS);

	for (int i = 0; i < p_class->members.size(); i++) {
		const FSParser::ClassNode::Member &member = p_class->members[i];
		if (member.type == FSParser::ClassNode::Member::FUNCTION) {
			analyze_function_local_finals(member.function);
		} else if (member.type == FSParser::ClassNode::Member::ENUM && member.m_enum != nullptr) {
			for (FSParser::FunctionNode *function : member.m_enum->functions) {
				analyze_function_local_finals(function);
			}
		} else if (member.type == FSParser::ClassNode::Member::VARIABLE) {
			// A member or static var initializer can embed a lambda whose body declares `final var`
			// locals. The initializer itself is an expression (no top-level local declaration), so the
			// collected throwaway sets stay empty; the walk's purpose is to reach those lambda bodies,
			// which `collect_local_finals` analyzes as their own scopes.
			if (member.variable->initializer != nullptr) {
				HashSet<const FSParser::VariableNode *> nested_finals;
				HashMap<StringName, const FSParser::VariableNode *> nested_finals_by_name;
				collect_local_finals(member.variable->initializer, nested_finals, nested_finals_by_name);
			}
			if (member.variable->property == FSParser::VariableNode::PROP_INLINE) {
				// Inline property accessor bodies hold their own statement trees (and `final var`
				// locals), so they are analyzed like any other function.
				analyze_function_local_finals(member.variable->getter);
				analyze_function_local_finals(member.variable->setter);
			}
		}
	}

	if (p_class->is_enum_file && p_class->enum_file_decl != nullptr) {
		for (FSParser::FunctionNode *function : p_class->enum_file_decl->functions) {
			analyze_function_local_finals(function);
		}
	}
}

// Runs the definite-assignment engine over a single function body for its `final var` locals, then
// recurses into nested lambda bodies (each a separate scope with its own locals, per the
// single-slot model that does not track assignment through closures).
void FSAnalyzer::FlowFinalityContext::analyze_function_local_finals(const FSParser::FunctionNode *p_function) {
	if (p_function == nullptr || p_function->body == nullptr) {
		return;
	}
	HashSet<const FSParser::VariableNode *> finals;
	HashMap<StringName, const FSParser::VariableNode *> finals_by_name;
	collect_local_finals(p_function->body, finals, finals_by_name);
	if (!finals.is_empty()) {
		// Reject reassignment of a captured local final from inside a nested lambda before running the
		// definite-assignment walk (which intentionally does not descend into lambda scopes).
		scan_illegal_final_writes(p_function->body, finals, finals_by_name, FinalAssignmentScope::LOCAL, true);

		FinalAssignmentState state;
		HashSet<const FSParser::VariableNode *> assigned_anywhere;
		analyze_final_definite_assignment_suite(p_function->body, finals, finals_by_name, FinalAssignmentScope::LOCAL, state, assigned_anywhere);
	}
}

// Collects the `final var` locals declared directly in a function body (descending through nested
// control-flow blocks and expressions) without crossing lambda boundaries; a nested lambda is a
// separate scope, so its body is handed to `analyze_function_local_finals` rather than collected
// here.
void FSAnalyzer::FlowFinalityContext::collect_local_finals(const FSParser::Node *p_node,
		HashSet<const FSParser::VariableNode *> &r_finals,
		HashMap<StringName, const FSParser::VariableNode *> &r_finals_by_name) {
	if (p_node == nullptr) {
		return;
	}
	switch (p_node->type) {
		case FSParser::Node::SUITE: {
			const FSParser::SuiteNode *suite = static_cast<const FSParser::SuiteNode *>(p_node);
			for (int i = 0; i < suite->statements.size(); i++) {
				collect_local_finals(suite->statements[i], r_finals, r_finals_by_name);
			}
		} break;
		case FSParser::Node::IF: {
			const FSParser::IfNode *if_node = static_cast<const FSParser::IfNode *>(p_node);
			collect_local_finals(if_node->condition, r_finals, r_finals_by_name);
			collect_local_finals(if_node->true_block, r_finals, r_finals_by_name);
			collect_local_finals(if_node->false_block, r_finals, r_finals_by_name);
		} break;
		case FSParser::Node::FOR: {
			const FSParser::ForNode *for_node = static_cast<const FSParser::ForNode *>(p_node);
			collect_local_finals(for_node->list, r_finals, r_finals_by_name);
			collect_local_finals(for_node->loop, r_finals, r_finals_by_name);
		} break;
		case FSParser::Node::WHILE: {
			const FSParser::WhileNode *while_node = static_cast<const FSParser::WhileNode *>(p_node);
			collect_local_finals(while_node->condition, r_finals, r_finals_by_name);
			collect_local_finals(while_node->loop, r_finals, r_finals_by_name);
		} break;
		case FSParser::Node::MATCH: {
			const FSParser::MatchNode *match_node = static_cast<const FSParser::MatchNode *>(p_node);
			collect_local_finals(match_node->test, r_finals, r_finals_by_name);
			for (int i = 0; i < match_node->branches.size(); i++) {
				if (match_node->branches[i] != nullptr) {
					collect_local_finals(match_node->branches[i]->guard_body, r_finals, r_finals_by_name);
					collect_local_finals(match_node->branches[i]->block, r_finals, r_finals_by_name);
				}
			}
		} break;
		case FSParser::Node::VARIABLE: {
			const FSParser::VariableNode *variable = static_cast<const FSParser::VariableNode *>(p_node);
			if (variable->is_final) {
				r_finals.insert(variable);
				r_finals_by_name[variable->identifier->name] = variable;
			}
			collect_local_finals(variable->initializer, r_finals, r_finals_by_name);
		} break;
		case FSParser::Node::LAMBDA: {
			const FSParser::LambdaNode *lambda = static_cast<const FSParser::LambdaNode *>(p_node);
			// A lambda body is its own scope; analyze it independently rather than folding its locals
			// into the enclosing function's set.
			analyze_function_local_finals(lambda->function);
		} break;
		case FSParser::Node::ASSIGNMENT: {
			const FSParser::AssignmentNode *assignment = static_cast<const FSParser::AssignmentNode *>(p_node);
			collect_local_finals(assignment->assignee, r_finals, r_finals_by_name);
			collect_local_finals(assignment->assigned_value, r_finals, r_finals_by_name);
		} break;
		case FSParser::Node::RETURN: {
			const FSParser::ReturnNode *return_node = static_cast<const FSParser::ReturnNode *>(p_node);
			collect_local_finals(return_node->return_value, r_finals, r_finals_by_name);
		} break;
		case FSParser::Node::ASSERT: {
			const FSParser::AssertNode *assert_node = static_cast<const FSParser::AssertNode *>(p_node);
			collect_local_finals(assert_node->condition, r_finals, r_finals_by_name);
			collect_local_finals(assert_node->message, r_finals, r_finals_by_name);
		} break;
		case FSParser::Node::BINARY_OPERATOR: {
			const FSParser::BinaryOpNode *binary = static_cast<const FSParser::BinaryOpNode *>(p_node);
			collect_local_finals(binary->left_operand, r_finals, r_finals_by_name);
			collect_local_finals(binary->right_operand, r_finals, r_finals_by_name);
		} break;
		case FSParser::Node::UNARY_OPERATOR: {
			const FSParser::UnaryOpNode *unary = static_cast<const FSParser::UnaryOpNode *>(p_node);
			collect_local_finals(unary->operand, r_finals, r_finals_by_name);
		} break;
		case FSParser::Node::TERNARY_OPERATOR: {
			const FSParser::TernaryOpNode *ternary = static_cast<const FSParser::TernaryOpNode *>(p_node);
			collect_local_finals(ternary->condition, r_finals, r_finals_by_name);
			collect_local_finals(ternary->true_expr, r_finals, r_finals_by_name);
			collect_local_finals(ternary->false_expr, r_finals, r_finals_by_name);
		} break;
		case FSParser::Node::TYPE_TEST: {
			const FSParser::TypeTestNode *type_test = static_cast<const FSParser::TypeTestNode *>(p_node);
			collect_local_finals(type_test->operand, r_finals, r_finals_by_name);
		} break;
		case FSParser::Node::CAST: {
			const FSParser::CastNode *cast = static_cast<const FSParser::CastNode *>(p_node);
			collect_local_finals(cast->operand, r_finals, r_finals_by_name);
		} break;
		case FSParser::Node::AWAIT: {
			const FSParser::AwaitNode *await = static_cast<const FSParser::AwaitNode *>(p_node);
			collect_local_finals(await->to_await, r_finals, r_finals_by_name);
		} break;
		case FSParser::Node::ARRAY: {
			const FSParser::ArrayNode *array = static_cast<const FSParser::ArrayNode *>(p_node);
			for (int i = 0; i < array->elements.size(); i++) {
				collect_local_finals(array->elements[i], r_finals, r_finals_by_name);
			}
		} break;
		case FSParser::Node::DICTIONARY: {
			const FSParser::DictionaryNode *dictionary = static_cast<const FSParser::DictionaryNode *>(p_node);
			for (int i = 0; i < dictionary->elements.size(); i++) {
				collect_local_finals(dictionary->elements[i].key, r_finals, r_finals_by_name);
				collect_local_finals(dictionary->elements[i].value, r_finals, r_finals_by_name);
			}
		} break;
		case FSParser::Node::CALL: {
			const FSParser::CallNode *call = static_cast<const FSParser::CallNode *>(p_node);
			collect_local_finals(call->callee, r_finals, r_finals_by_name);
			for (int i = 0; i < call->arguments.size(); i++) {
				collect_local_finals(call->arguments[i], r_finals, r_finals_by_name);
			}
		} break;
		case FSParser::Node::SUBSCRIPT: {
			const FSParser::SubscriptNode *subscript = static_cast<const FSParser::SubscriptNode *>(p_node);
			collect_local_finals(subscript->base, r_finals, r_finals_by_name);
			if (!subscript->is_attribute) {
				collect_local_finals(subscript->index, r_finals, r_finals_by_name);
			}
		} break;
		default:
			break;
	}
}

// Returns the `final` member variable designated by `p_expression` as an assignment target, or
// `nullptr` if it does not designate one. Detection is global (any `final` member, including a
// base class's or another class's), so cross-class and inherited writes are recognized; the caller
// decides legality by checking ownership (`p_finals`). Recognizes a bare member identifier (`id`,
// possibly inherited), explicit self access (`self.id`), and attribute access through any other
// receiver of the declaring type (`other.id`). `r_is_self_receiver` reports whether the reference
// is to *this* instance (bare or `self.`); a write through any other receiver is never the slot,
// and a read through one targets a different instance and is not flow-tracked.
const FSParser::VariableNode *FSAnalyzer::FlowFinalityContext::final_member_assignment_target(const FSParser::ExpressionNode *p_expression,
		const HashSet<const FSParser::VariableNode *> &p_finals,
		const HashMap<StringName, const FSParser::VariableNode *> &p_finals_by_name, FinalAssignmentScope p_scope, bool *r_is_self_receiver, bool p_flattened_trait_body) const {
	if (r_is_self_receiver != nullptr) {
		*r_is_self_receiver = false;
	}
	if (p_expression == nullptr) {
		return nullptr;
	}
	if (p_flattened_trait_body) {
		// In a flattened trait body the member references resolve against the trait's own AST, but the
		// compiler emits the flattened assignments by name against the implementing class's same-named
		// slot. A trait `variable_source` therefore carries a stale finality (an external trait body is
		// fully resolved in its own class context, so it points at the trait's member even when the
		// implementer shadows it). Resolve a reference to the implementer's own slot purely by name so
		// shadowing is honored; references that are not the implementer's slot are handled below.
		if (p_expression->type == FSParser::Node::IDENTIFIER) {
			const FSParser::IdentifierNode *identifier = static_cast<const FSParser::IdentifierNode *>(p_expression);
			switch (identifier->source) {
				case FSParser::IdentifierNode::FUNCTION_PARAMETER:
				case FSParser::IdentifierNode::LOCAL_VARIABLE:
				case FSParser::IdentifierNode::LOCAL_CONSTANT:
				case FSParser::IdentifierNode::LOCAL_ITERATOR:
				case FSParser::IdentifierNode::LOCAL_BIND:
					// A genuine local/parameter of the trait method that merely shares a final's name.
					return nullptr;
				default:
					break;
			}
			// A bare member reference is implicitly `self.<name>`; resolve the implementer's own tracked
			// slot by name so a name it shadows to a mutable (or absent) member is not treated as a final
			// write.
			HashMap<StringName, const FSParser::VariableNode *>::ConstIterator found = p_finals_by_name.find(identifier->name);
			if (found) {
				if (r_is_self_receiver != nullptr) {
					*r_is_self_receiver = true;
				}
				return found->value;
			}
			// Not a tracked slot. If it resolves to a final an applied trait declares, the implementer
			// shadowed that slot (its trait finality is stale), so it is not a final write here. Otherwise
			// it is a reliable inherited final reached through the trait's base constraint; fall through to
			// the normal resolution so that write is reported as it would be outside a trait body.
			const bool is_member_kind = identifier->source == FSParser::IdentifierNode::MEMBER_VARIABLE || identifier->source == FSParser::IdentifierNode::INHERITED_VARIABLE || identifier->source == FSParser::IdentifierNode::STATIC_VARIABLE;
			if (is_member_kind && identifier->variable_source != nullptr && flattened_trait_final_nodes.has(identifier->variable_source)) {
				return nullptr;
			}
			// Otherwise fall through to the normal resolution (an inherited final).
		} else if (p_expression->type == FSParser::Node::SUBSCRIPT) {
			const FSParser::SubscriptNode *subscript = static_cast<const FSParser::SubscriptNode *>(p_expression);
			if (!subscript->is_attribute || subscript->attribute == nullptr) {
				return nullptr;
			}
			if (subscript->base != nullptr && subscript->base->type == FSParser::Node::SELF) {
				// `self.<name>` designates this instance's slot; resolve the implementer's own tracked slot
				// by name so a name it shadows to a mutable member is not treated as a final write.
				HashMap<StringName, const FSParser::VariableNode *>::ConstIterator found = p_finals_by_name.find(subscript->attribute->name);
				if (found) {
					if (r_is_self_receiver != nullptr) {
						*r_is_self_receiver = true;
					}
					return found->value;
				}
				// Not a tracked slot: a trait-declared final the implementer shadowed is stale here, but a
				// reliable inherited final (reached through the trait's base constraint) must fall through.
				const bool attribute_is_member_kind = subscript->attribute->source == FSParser::IdentifierNode::MEMBER_VARIABLE || subscript->attribute->source == FSParser::IdentifierNode::INHERITED_VARIABLE || subscript->attribute->source == FSParser::IdentifierNode::STATIC_VARIABLE;
				if (attribute_is_member_kind && subscript->attribute->variable_source != nullptr && flattened_trait_final_nodes.has(subscript->attribute->variable_source)) {
					return nullptr;
				}
				// Otherwise fall through to the normal `self.<inherited final>` resolution.
			}
			// A non-`self` receiver carries an explicit static type, so its attribute's `variable_source`
			// is reliable — unlike a bare or `self` reference, which a flattened trait body resolves
			// against the trait's own member even when the implementer shadows it. Fall through to the
			// normal resolution so a write to any final reached through such a receiver (this class's own
			// slot, another instance of the declaring type, an inherited final, or another class's final)
			// is reported exactly as it would be outside a trait body.
		} else {
			return nullptr;
		}
	}
	if (p_scope != FinalAssignmentScope::INSTANCE_MEMBER) {
		// Static and local finals are referenced by a bare identifier (this class's static variable,
		// or a block local); the caller gates the rest by membership in `p_finals`.
		if (p_expression->type == FSParser::Node::IDENTIFIER) {
			const FSParser::IdentifierNode *identifier = static_cast<const FSParser::IdentifierNode *>(p_expression);
			const FSParser::IdentifierNode::Source expected_source = p_scope == FinalAssignmentScope::STATIC_MEMBER ? FSParser::IdentifierNode::STATIC_VARIABLE : FSParser::IdentifierNode::LOCAL_VARIABLE;
			if (identifier->source == expected_source && identifier->variable_source != nullptr && identifier->variable_source->is_final) {
				if (r_is_self_receiver != nullptr) {
					*r_is_self_receiver = true;
				}
				return identifier->variable_source;
			}
		}
		// A static final has a single shared slot regardless of receiver, so qualified forms
		// (`ClassName.VALUE`, `self.VALUE`, `instance.VALUE`) designate the same slot as the bare
		// name. Recognize them so reassignment and use-before-assignment are still enforced; reaching
		// this class's own static (matched by name) counts as the tracked slot.
		if (p_scope == FinalAssignmentScope::STATIC_MEMBER && p_expression->type == FSParser::Node::SUBSCRIPT) {
			const FSParser::SubscriptNode *subscript = static_cast<const FSParser::SubscriptNode *>(p_expression);
			if (subscript->is_attribute && subscript->attribute != nullptr && subscript->attribute->source == FSParser::IdentifierNode::STATIC_VARIABLE && subscript->attribute->variable_source != nullptr && subscript->attribute->variable_source->is_final) {
				const FSParser::VariableNode *static_final = subscript->attribute->variable_source;
				HashMap<StringName, const FSParser::VariableNode *>::ConstIterator found = p_finals_by_name.find(subscript->attribute->name);
				if (found && found->value == static_final && r_is_self_receiver != nullptr) {
					*r_is_self_receiver = true;
				}
				return static_final;
			}
		}
		return nullptr;
	}
	if (p_expression->type == FSParser::Node::IDENTIFIER) {
		const FSParser::IdentifierNode *identifier = static_cast<const FSParser::IdentifierNode *>(p_expression);
		const bool is_member = identifier->source == FSParser::IdentifierNode::MEMBER_VARIABLE || identifier->source == FSParser::IdentifierNode::INHERITED_VARIABLE;
		if (is_member && identifier->variable_source != nullptr && identifier->variable_source->is_final) {
			// A bare member reference is implicitly `self.<name>`.
			if (r_is_self_receiver != nullptr) {
				*r_is_self_receiver = true;
			}
			return identifier->variable_source;
		}
		return nullptr;
	}
	if (p_expression->type == FSParser::Node::SUBSCRIPT) {
		const FSParser::SubscriptNode *subscript = static_cast<const FSParser::SubscriptNode *>(p_expression);
		if (subscript->is_attribute && subscript->attribute != nullptr && subscript->base != nullptr) {
			// `variable_source` is part of a union keyed by `source`, so it is only valid to read
			// when the attribute resolved to a variable (not a method/signal/etc.).
			const bool attribute_is_variable = subscript->attribute->source == FSParser::IdentifierNode::MEMBER_VARIABLE || subscript->attribute->source == FSParser::IdentifierNode::INHERITED_VARIABLE || subscript->attribute->source == FSParser::IdentifierNode::STATIC_VARIABLE;
			// A `final static var` is owned by the static-final pass, not the instance-member slot, so
			// exclude it here to avoid double-diagnosing a qualified static write.
			const FSParser::VariableNode *attribute_final = (attribute_is_variable && subscript->attribute->variable_source != nullptr && subscript->attribute->variable_source->is_final && !subscript->attribute->variable_source->is_static) ? subscript->attribute->variable_source : nullptr;
			if (subscript->base->type == FSParser::Node::SELF) {
				HashMap<StringName, const FSParser::VariableNode *>::ConstIterator found = p_finals_by_name.find(subscript->attribute->name);
				if (found) {
					if (r_is_self_receiver != nullptr) {
						*r_is_self_receiver = true;
					}
					return found->value;
				}
				// A `self.<name>` reference to an inherited final still targets this instance.
				if (attribute_final != nullptr) {
					if (r_is_self_receiver != nullptr) {
						*r_is_self_receiver = true;
					}
					return attribute_final;
				}
			} else if (attribute_final != nullptr) {
				// `other.id` where `other` is statically of the declaring type: the attribute
				// resolves to a final, but the receiver is not `self`.
				return attribute_final;
			}
		}
	}
	return nullptr;
}

// Reports any read of a blank `final` member that has not yet been definitely assigned along
// the current path ("may be used before assignment"). Walks the expression tree but stops at
// lambda boundaries, which are out of scope for the single-assignment-slot model.
void FSAnalyzer::FlowFinalityContext::check_final_reads_in_expression(const FSParser::ExpressionNode *p_expression,
		const HashSet<const FSParser::VariableNode *> &p_finals,
		const HashMap<StringName, const FSParser::VariableNode *> &p_finals_by_name, FinalAssignmentScope p_scope, const FinalAssignmentState &p_state, bool p_flattened_trait_body) {
	if (p_expression == nullptr) {
		return;
	}

	bool is_self_receiver = false;
	const FSParser::VariableNode *referenced = final_member_assignment_target(p_expression, p_finals, p_finals_by_name, p_scope, &is_self_receiver, p_flattened_trait_body);
	// Use-before-assignment only applies to this instance's own (this-class) slot: an inherited
	// final is assigned by the base constructor, and another instance's final (`other.id`) is a
	// separate, possibly fully constructed object.
	if (referenced != nullptr && is_self_receiver && p_finals.has(referenced) && !p_state.assigned.has(referenced)) {
		analyzer->push_error(vformat(R"(Final variable "%s" may be used before assignment.)", referenced->identifier->name), p_expression);
	}

	switch (p_expression->type) {
		case FSParser::Node::BINARY_OPERATOR: {
			const FSParser::BinaryOpNode *binary = static_cast<const FSParser::BinaryOpNode *>(p_expression);
			check_final_reads_in_expression(binary->left_operand, p_finals, p_finals_by_name, p_scope, p_state, p_flattened_trait_body);
			check_final_reads_in_expression(binary->right_operand, p_finals, p_finals_by_name, p_scope, p_state, p_flattened_trait_body);
		} break;
		case FSParser::Node::UNARY_OPERATOR: {
			const FSParser::UnaryOpNode *unary = static_cast<const FSParser::UnaryOpNode *>(p_expression);
			check_final_reads_in_expression(unary->operand, p_finals, p_finals_by_name, p_scope, p_state, p_flattened_trait_body);
		} break;
		case FSParser::Node::TERNARY_OPERATOR: {
			const FSParser::TernaryOpNode *ternary = static_cast<const FSParser::TernaryOpNode *>(p_expression);
			check_final_reads_in_expression(ternary->condition, p_finals, p_finals_by_name, p_scope, p_state, p_flattened_trait_body);
			check_final_reads_in_expression(ternary->true_expr, p_finals, p_finals_by_name, p_scope, p_state, p_flattened_trait_body);
			check_final_reads_in_expression(ternary->false_expr, p_finals, p_finals_by_name, p_scope, p_state, p_flattened_trait_body);
		} break;
		case FSParser::Node::TYPE_TEST: {
			const FSParser::TypeTestNode *type_test = static_cast<const FSParser::TypeTestNode *>(p_expression);
			check_final_reads_in_expression(type_test->operand, p_finals, p_finals_by_name, p_scope, p_state, p_flattened_trait_body);
		} break;
		case FSParser::Node::CAST: {
			const FSParser::CastNode *cast = static_cast<const FSParser::CastNode *>(p_expression);
			check_final_reads_in_expression(cast->operand, p_finals, p_finals_by_name, p_scope, p_state, p_flattened_trait_body);
		} break;
		case FSParser::Node::AWAIT: {
			const FSParser::AwaitNode *await = static_cast<const FSParser::AwaitNode *>(p_expression);
			check_final_reads_in_expression(await->to_await, p_finals, p_finals_by_name, p_scope, p_state, p_flattened_trait_body);
		} break;
		case FSParser::Node::ARRAY: {
			const FSParser::ArrayNode *array = static_cast<const FSParser::ArrayNode *>(p_expression);
			for (int i = 0; i < array->elements.size(); i++) {
				check_final_reads_in_expression(array->elements[i], p_finals, p_finals_by_name, p_scope, p_state, p_flattened_trait_body);
			}
		} break;
		case FSParser::Node::DICTIONARY: {
			const FSParser::DictionaryNode *dictionary = static_cast<const FSParser::DictionaryNode *>(p_expression);
			for (int i = 0; i < dictionary->elements.size(); i++) {
				check_final_reads_in_expression(dictionary->elements[i].key, p_finals, p_finals_by_name, p_scope, p_state, p_flattened_trait_body);
				check_final_reads_in_expression(dictionary->elements[i].value, p_finals, p_finals_by_name, p_scope, p_state, p_flattened_trait_body);
			}
		} break;
		case FSParser::Node::CALL: {
			const FSParser::CallNode *call = static_cast<const FSParser::CallNode *>(p_expression);
			// The callee can be `base.method`; checking it covers reading a final as the call base.
			check_final_reads_in_expression(call->callee, p_finals, p_finals_by_name, p_scope, p_state, p_flattened_trait_body);
			for (int i = 0; i < call->arguments.size(); i++) {
				check_final_reads_in_expression(call->arguments[i], p_finals, p_finals_by_name, p_scope, p_state, p_flattened_trait_body);
			}
		} break;
		case FSParser::Node::SUBSCRIPT: {
			const FSParser::SubscriptNode *subscript = static_cast<const FSParser::SubscriptNode *>(p_expression);
			// A self-receiver final (`self.id`) was already handled above; for anything else recurse
			// into the base/index for nested reads (e.g. `array[id]`, or the `other` in `other.id`).
			if (referenced == nullptr || !is_self_receiver) {
				check_final_reads_in_expression(subscript->base, p_finals, p_finals_by_name, p_scope, p_state, p_flattened_trait_body);
				if (!subscript->is_attribute) {
					check_final_reads_in_expression(subscript->index, p_finals, p_finals_by_name, p_scope, p_state, p_flattened_trait_body);
				}
			}
		} break;
		case FSParser::Node::ASSIGNMENT: {
			const FSParser::AssignmentNode *assignment = static_cast<const FSParser::AssignmentNode *>(p_expression);
			check_final_reads_in_expression(assignment->assigned_value, p_finals, p_finals_by_name, p_scope, p_state, p_flattened_trait_body);
		} break;
		default:
			break;
	}
}

// Reports reads of still-blank finals inside a `match` pattern (expression and dictionary-key
// patterns can reference a final). Bind/literal/wildcard patterns reference nothing.
void FSAnalyzer::FlowFinalityContext::check_final_reads_in_pattern(const FSParser::PatternNode *p_pattern,
		const HashSet<const FSParser::VariableNode *> &p_finals,
		const HashMap<StringName, const FSParser::VariableNode *> &p_finals_by_name, FinalAssignmentScope p_scope, const FinalAssignmentState &p_state, bool p_flattened_trait_body) {
	if (p_pattern == nullptr) {
		return;
	}
	switch (p_pattern->pattern_type) {
		case FSParser::PatternNode::PT_EXPRESSION: {
			check_final_reads_in_expression(p_pattern->expression, p_finals, p_finals_by_name, p_scope, p_state, p_flattened_trait_body);
		} break;
		case FSParser::PatternNode::PT_ARRAY: {
			for (int i = 0; i < p_pattern->array.size(); i++) {
				check_final_reads_in_pattern(p_pattern->array[i], p_finals, p_finals_by_name, p_scope, p_state, p_flattened_trait_body);
			}
		} break;
		case FSParser::PatternNode::PT_DICTIONARY: {
			for (int i = 0; i < p_pattern->dictionary.size(); i++) {
				check_final_reads_in_expression(p_pattern->dictionary[i].key, p_finals, p_finals_by_name, p_scope, p_state, p_flattened_trait_body);
				check_final_reads_in_pattern(p_pattern->dictionary[i].value_pattern, p_finals, p_finals_by_name, p_scope, p_state, p_flattened_trait_body);
			}
		} break;
		default:
			break;
	}
}

// Reports every assignment to a `final` member that occurs outside its single legal slot —
// any function other than `_init`, or inside a lambda even within `_init`. This bounds the
// definite-assignment flow analysis to `_init`'s own statement tree.
void FSAnalyzer::FlowFinalityContext::scan_illegal_final_writes(const FSParser::Node *p_node,
		const HashSet<const FSParser::VariableNode *> &p_finals,
		const HashMap<StringName, const FSParser::VariableNode *> &p_finals_by_name, FinalAssignmentScope p_scope, bool p_in_init, bool p_flattened_trait_body) {
	if (p_node == nullptr) {
		return;
	}

	switch (p_node->type) {
		case FSParser::Node::SUITE: {
			const FSParser::SuiteNode *suite = static_cast<const FSParser::SuiteNode *>(p_node);
			for (int i = 0; i < suite->statements.size(); i++) {
				scan_illegal_final_writes(suite->statements[i], p_finals, p_finals_by_name, p_scope, p_in_init, p_flattened_trait_body);
			}
		} break;
		case FSParser::Node::IF: {
			const FSParser::IfNode *if_node = static_cast<const FSParser::IfNode *>(p_node);
			scan_illegal_final_writes(if_node->condition, p_finals, p_finals_by_name, p_scope, p_in_init, p_flattened_trait_body);
			scan_illegal_final_writes(if_node->true_block, p_finals, p_finals_by_name, p_scope, p_in_init, p_flattened_trait_body);
			scan_illegal_final_writes(if_node->false_block, p_finals, p_finals_by_name, p_scope, p_in_init, p_flattened_trait_body);
		} break;
		case FSParser::Node::FOR: {
			const FSParser::ForNode *for_node = static_cast<const FSParser::ForNode *>(p_node);
			scan_illegal_final_writes(for_node->list, p_finals, p_finals_by_name, p_scope, p_in_init, p_flattened_trait_body);
			scan_illegal_final_writes(for_node->loop, p_finals, p_finals_by_name, p_scope, p_in_init, p_flattened_trait_body);
		} break;
		case FSParser::Node::WHILE: {
			const FSParser::WhileNode *while_node = static_cast<const FSParser::WhileNode *>(p_node);
			scan_illegal_final_writes(while_node->condition, p_finals, p_finals_by_name, p_scope, p_in_init, p_flattened_trait_body);
			scan_illegal_final_writes(while_node->loop, p_finals, p_finals_by_name, p_scope, p_in_init, p_flattened_trait_body);
		} break;
		case FSParser::Node::MATCH: {
			const FSParser::MatchNode *match_node = static_cast<const FSParser::MatchNode *>(p_node);
			scan_illegal_final_writes(match_node->test, p_finals, p_finals_by_name, p_scope, p_in_init, p_flattened_trait_body);
			for (int i = 0; i < match_node->branches.size(); i++) {
				if (match_node->branches[i] != nullptr) {
					scan_illegal_final_writes(match_node->branches[i]->guard_body, p_finals, p_finals_by_name, p_scope, p_in_init, p_flattened_trait_body);
					scan_illegal_final_writes(match_node->branches[i]->block, p_finals, p_finals_by_name, p_scope, p_in_init, p_flattened_trait_body);
				}
			}
		} break;
		case FSParser::Node::ASSIGNMENT: {
			const FSParser::AssignmentNode *assignment = static_cast<const FSParser::AssignmentNode *>(p_node);
			bool is_self_receiver = false;
			const FSParser::VariableNode *target = final_member_assignment_target(assignment->assignee, p_finals, p_finals_by_name, p_scope, &is_self_receiver, p_flattened_trait_body);
			if (p_scope == FinalAssignmentScope::LOCAL) {
				// A local final is assigned within its own function body (tracked by the definite-
				// assignment pass). The single illegal form is a write from inside a nested lambda,
				// which captures the final but is a separate scope outside the single-slot model.
				// `p_in_init` is true in the declaring body and false once a lambda boundary is crossed.
				if (target != nullptr && p_finals.has(target) && !p_in_init) {
					analyzer->push_error(vformat(R"(Final variable "%s" cannot be assigned inside a lambda.)", target->identifier->name), assignment->assignee);
				}
			} else if (target != nullptr && !(p_finals.has(target) && is_self_receiver && p_in_init)) {
				// The only legal write fills *this class's own* final (instance or static) on *this*
				// receiver, lexically in the matching initializer slot. Everything else is rejected: a
				// write outside the slot function, through another receiver (`other.id`/alias of
				// `self`), to an inherited final (the base owns its slot), or to another class's final.
				const char *slot_function = p_scope == FinalAssignmentScope::STATIC_MEMBER ? "_static_init()" : "_init()";
				analyzer->push_error(vformat(R"*(Final variable "%s" can only be assigned in its declaration or in "%s".)*", target->identifier->name, slot_function), assignment->assignee);
			}
			scan_illegal_final_writes(assignment->assignee, p_finals, p_finals_by_name, p_scope, p_in_init, p_flattened_trait_body);
			scan_illegal_final_writes(assignment->assigned_value, p_finals, p_finals_by_name, p_scope, p_in_init, p_flattened_trait_body);
		} break;
		case FSParser::Node::LAMBDA: {
			const FSParser::LambdaNode *lambda = static_cast<const FSParser::LambdaNode *>(p_node);
			if (lambda->function != nullptr) {
				// Writes inside a lambda never fill the single slot, even within the slot function.
				scan_illegal_final_writes(lambda->function->body, p_finals, p_finals_by_name, p_scope, false, p_flattened_trait_body);
			}
		} break;
		case FSParser::Node::VARIABLE: {
			const FSParser::VariableNode *variable = static_cast<const FSParser::VariableNode *>(p_node);
			scan_illegal_final_writes(variable->initializer, p_finals, p_finals_by_name, p_scope, p_in_init, p_flattened_trait_body);
		} break;
		case FSParser::Node::RETURN: {
			const FSParser::ReturnNode *return_node = static_cast<const FSParser::ReturnNode *>(p_node);
			scan_illegal_final_writes(return_node->return_value, p_finals, p_finals_by_name, p_scope, p_in_init, p_flattened_trait_body);
		} break;
		case FSParser::Node::ASSERT: {
			const FSParser::AssertNode *assert_node = static_cast<const FSParser::AssertNode *>(p_node);
			scan_illegal_final_writes(assert_node->condition, p_finals, p_finals_by_name, p_scope, p_in_init, p_flattened_trait_body);
			scan_illegal_final_writes(assert_node->message, p_finals, p_finals_by_name, p_scope, p_in_init, p_flattened_trait_body);
		} break;
		case FSParser::Node::BINARY_OPERATOR: {
			const FSParser::BinaryOpNode *binary = static_cast<const FSParser::BinaryOpNode *>(p_node);
			scan_illegal_final_writes(binary->left_operand, p_finals, p_finals_by_name, p_scope, p_in_init, p_flattened_trait_body);
			scan_illegal_final_writes(binary->right_operand, p_finals, p_finals_by_name, p_scope, p_in_init, p_flattened_trait_body);
		} break;
		case FSParser::Node::UNARY_OPERATOR: {
			const FSParser::UnaryOpNode *unary = static_cast<const FSParser::UnaryOpNode *>(p_node);
			scan_illegal_final_writes(unary->operand, p_finals, p_finals_by_name, p_scope, p_in_init, p_flattened_trait_body);
		} break;
		case FSParser::Node::TERNARY_OPERATOR: {
			const FSParser::TernaryOpNode *ternary = static_cast<const FSParser::TernaryOpNode *>(p_node);
			scan_illegal_final_writes(ternary->condition, p_finals, p_finals_by_name, p_scope, p_in_init, p_flattened_trait_body);
			scan_illegal_final_writes(ternary->true_expr, p_finals, p_finals_by_name, p_scope, p_in_init, p_flattened_trait_body);
			scan_illegal_final_writes(ternary->false_expr, p_finals, p_finals_by_name, p_scope, p_in_init, p_flattened_trait_body);
		} break;
		case FSParser::Node::TYPE_TEST: {
			const FSParser::TypeTestNode *type_test = static_cast<const FSParser::TypeTestNode *>(p_node);
			scan_illegal_final_writes(type_test->operand, p_finals, p_finals_by_name, p_scope, p_in_init, p_flattened_trait_body);
		} break;
		case FSParser::Node::CAST: {
			const FSParser::CastNode *cast = static_cast<const FSParser::CastNode *>(p_node);
			scan_illegal_final_writes(cast->operand, p_finals, p_finals_by_name, p_scope, p_in_init, p_flattened_trait_body);
		} break;
		case FSParser::Node::AWAIT: {
			const FSParser::AwaitNode *await = static_cast<const FSParser::AwaitNode *>(p_node);
			scan_illegal_final_writes(await->to_await, p_finals, p_finals_by_name, p_scope, p_in_init, p_flattened_trait_body);
		} break;
		case FSParser::Node::ARRAY: {
			const FSParser::ArrayNode *array = static_cast<const FSParser::ArrayNode *>(p_node);
			for (int i = 0; i < array->elements.size(); i++) {
				scan_illegal_final_writes(array->elements[i], p_finals, p_finals_by_name, p_scope, p_in_init, p_flattened_trait_body);
			}
		} break;
		case FSParser::Node::DICTIONARY: {
			const FSParser::DictionaryNode *dictionary = static_cast<const FSParser::DictionaryNode *>(p_node);
			for (int i = 0; i < dictionary->elements.size(); i++) {
				scan_illegal_final_writes(dictionary->elements[i].key, p_finals, p_finals_by_name, p_scope, p_in_init, p_flattened_trait_body);
				scan_illegal_final_writes(dictionary->elements[i].value, p_finals, p_finals_by_name, p_scope, p_in_init, p_flattened_trait_body);
			}
		} break;
		case FSParser::Node::CALL: {
			const FSParser::CallNode *call = static_cast<const FSParser::CallNode *>(p_node);
			scan_illegal_final_writes(call->callee, p_finals, p_finals_by_name, p_scope, p_in_init, p_flattened_trait_body);
			for (int i = 0; i < call->arguments.size(); i++) {
				scan_illegal_final_writes(call->arguments[i], p_finals, p_finals_by_name, p_scope, p_in_init, p_flattened_trait_body);
			}
		} break;
		case FSParser::Node::SUBSCRIPT: {
			const FSParser::SubscriptNode *subscript = static_cast<const FSParser::SubscriptNode *>(p_node);
			scan_illegal_final_writes(subscript->base, p_finals, p_finals_by_name, p_scope, p_in_init, p_flattened_trait_body);
			if (!subscript->is_attribute) {
				scan_illegal_final_writes(subscript->index, p_finals, p_finals_by_name, p_scope, p_in_init, p_flattened_trait_body);
			}
		} break;
		default:
			break;
	}
}

// Threads the definite-assignment state through a structured statement, mutating `r_state`.
// Implements the JLS merge rules for if/match (intersection), loops (zero-iteration), and
// terminators (unreachable path acts as the universal set under intersection).
void FSAnalyzer::FlowFinalityContext::analyze_final_definite_assignment_statement(const FSParser::Node *p_statement,
		const HashSet<const FSParser::VariableNode *> &p_finals,
		const HashMap<StringName, const FSParser::VariableNode *> &p_finals_by_name, FinalAssignmentScope p_scope, FinalAssignmentState &r_state,
		HashSet<const FSParser::VariableNode *> &r_assigned_anywhere, bool p_flattened_trait_body) {
	if (p_statement == nullptr || !r_state.reachable) {
		return;
	}

	switch (p_statement->type) {
		case FSParser::Node::ASSIGNMENT: {
			const FSParser::AssignmentNode *assignment = static_cast<const FSParser::AssignmentNode *>(p_statement);
			check_final_reads_in_expression(assignment->assigned_value, p_finals, p_finals_by_name, p_scope, r_state, p_flattened_trait_body);

			bool is_self_receiver = false;
			const FSParser::VariableNode *target = final_member_assignment_target(assignment->assignee, p_finals, p_finals_by_name, p_scope, &is_self_receiver, p_flattened_trait_body);
			if (target == nullptr || !is_self_receiver || !p_finals.has(target)) {
				// Not this class's own slot on this instance. A write through another receiver
				// (`other.id`) or to an inherited final is rejected by the illegal-write scan and
				// never fills the slot here; the assignee may also read finals (e.g. `array[id] = x`
				// or the `other` in `other.id = x`).
				check_final_reads_in_expression(assignment->assignee, p_finals, p_finals_by_name, p_scope, r_state, p_flattened_trait_body);
				break;
			}

			if (r_state.maybe_assigned.has(target)) {
				// Assigned on some path that reaches here (e.g. one arm of an earlier `if`), so a
				// second write is a double-write.
				analyzer->push_error(vformat(R"(Cannot assign to final variable "%s"; it is already assigned.)", target->identifier->name), assignment->assignee);
			} else if (assignment->operation != FSParser::AssignmentNode::OP_NONE) {
				// A compound assignment reads the target before writing it.
				analyzer->push_error(vformat(R"(Final variable "%s" may be used before assignment.)", target->identifier->name), assignment->assignee);
			}
			r_state.assigned.insert(target);
			r_state.maybe_assigned.insert(target);
			r_assigned_anywhere.insert(target);
		} break;
		case FSParser::Node::IF: {
			const FSParser::IfNode *if_node = static_cast<const FSParser::IfNode *>(p_statement);
			check_final_reads_in_expression(if_node->condition, p_finals, p_finals_by_name, p_scope, r_state, p_flattened_trait_body);

			FinalAssignmentState true_state = r_state;
			analyze_final_definite_assignment_suite(if_node->true_block, p_finals, p_finals_by_name, p_scope, true_state, r_assigned_anywhere, p_flattened_trait_body);

			FinalAssignmentState false_state = r_state;
			if (if_node->false_block != nullptr) {
				analyze_final_definite_assignment_suite(if_node->false_block, p_finals, p_finals_by_name, p_scope, false_state, r_assigned_anywhere, p_flattened_trait_body);
			}

			merge_final_assignment_branches(true_state, false_state, r_state);
		} break;
		case FSParser::Node::FOR: {
			const FSParser::ForNode *for_node = static_cast<const FSParser::ForNode *>(p_statement);
			check_final_reads_in_expression(for_node->list, p_finals, p_finals_by_name, p_scope, r_state, p_flattened_trait_body);
			// The body may run zero times, so it adds nothing to the definitely-assigned set; analyze
			// a copy so reads/double-assigns inside are still reported, then carry the body's writes
			// into `maybe_assigned` so a write after the loop is recognized as a possible double-write.
			FinalAssignmentState body_state = r_state;
			analyze_final_definite_assignment_suite(for_node->loop, p_finals, p_finals_by_name, p_scope, body_state, r_assigned_anywhere, p_flattened_trait_body);
			for (const FSParser::VariableNode *variable : body_state.maybe_assigned) {
				r_state.maybe_assigned.insert(variable);
			}
		} break;
		case FSParser::Node::WHILE: {
			const FSParser::WhileNode *while_node = static_cast<const FSParser::WhileNode *>(p_statement);
			check_final_reads_in_expression(while_node->condition, p_finals, p_finals_by_name, p_scope, r_state, p_flattened_trait_body);
			FinalAssignmentState body_state = r_state;
			analyze_final_definite_assignment_suite(while_node->loop, p_finals, p_finals_by_name, p_scope, body_state, r_assigned_anywhere, p_flattened_trait_body);
			for (const FSParser::VariableNode *variable : body_state.maybe_assigned) {
				r_state.maybe_assigned.insert(variable);
			}
		} break;
		case FSParser::Node::MATCH: {
			const FSParser::MatchNode *match_node = static_cast<const FSParser::MatchNode *>(p_statement);
			check_final_reads_in_expression(match_node->test, p_finals, p_finals_by_name, p_scope, r_state, p_flattened_trait_body);

			bool has_unguarded_catchall = false;
			bool has_branch = false;
			FinalAssignmentState merged;
			merged.reachable = false; // Identity for intersection; replaced by the first branch.
			for (int i = 0; i < match_node->branches.size(); i++) {
				const FSParser::MatchBranchNode *branch = match_node->branches[i];
				if (branch == nullptr) {
					continue;
				}
				FinalAssignmentState branch_state = r_state;
				// Patterns and the `when` guard are evaluated with the branch's incoming state
				// before its block.
				for (int p = 0; p < branch->patterns.size(); p++) {
					check_final_reads_in_pattern(branch->patterns[p], p_finals, p_finals_by_name, p_scope, branch_state, p_flattened_trait_body);
				}
				analyze_final_definite_assignment_suite(branch->guard_body, p_finals, p_finals_by_name, p_scope, branch_state, r_assigned_anywhere, p_flattened_trait_body);
				analyze_final_definite_assignment_suite(branch->block, p_finals, p_finals_by_name, p_scope, branch_state, r_assigned_anywhere, p_flattened_trait_body);
				if (!has_branch) {
					merged = branch_state;
					has_branch = true;
				} else {
					FinalAssignmentState intersection;
					merge_final_assignment_branches(merged, branch_state, intersection);
					merged = intersection;
				}
				// A guard-less wildcard always matches, so it closes the no-match path and makes any
				// later branch unreachable; stop merging here.
				if (branch->has_wildcard && branch->guard_body == nullptr) {
					has_unguarded_catchall = true;
					break;
				}
			}

			if (!has_branch) {
				break;
			}
			if (!has_unguarded_catchall) {
				// Without a guard-less wildcard the no-match path falls through with the incoming
				// state, so nothing the branches assign can be guaranteed; intersect with it.
				FinalAssignmentState fallthrough = r_state;
				FinalAssignmentState intersection;
				merge_final_assignment_branches(merged, fallthrough, intersection);
				merged = intersection;
			}
			r_state = merged;
		} break;
		case FSParser::Node::RETURN: {
			const FSParser::ReturnNode *return_node = static_cast<const FSParser::ReturnNode *>(p_statement);
			check_final_reads_in_expression(return_node->return_value, p_finals, p_finals_by_name, p_scope, r_state, p_flattened_trait_body);
			// A `return` exits `_init()`/`_static_init()` while still producing a constructed object
			// (or a fully loaded class), so any blank final left unassigned on this path would escape
			// at its default value. Mirroring Java's blank-final-in-constructor rule, every final must
			// be definitely assigned before the return. A genuine abort such as `@noreturn`/`push_fatal`
			// never lets the object escape and is exempt (handled in the default case below), and locals
			// carry no such obligation, so this check is limited to member scopes.
			if (p_scope != FinalAssignmentScope::LOCAL) {
				const char *init_name = p_scope == FinalAssignmentScope::STATIC_MEMBER ? "_static_init()" : "_init()";
				for (const FSParser::VariableNode *variable : p_finals) {
					if (!r_state.assigned.has(variable)) {
						analyzer->push_error(vformat(R"*(Final variable "%s" must be definitely assigned before returning from "%s".)*", variable->identifier->name, init_name), return_node);
					}
				}
			}
			// The path is now unreachable; its assigned set acts as the universal set, which is neutral
			// at branch joins (so `if cond: id = a else: <abort>` keeps `id` definitely assigned after).
			r_state.reachable = false;
		} break;
		case FSParser::Node::BREAK:
		case FSParser::Node::CONTINUE: {
			r_state.reachable = false;
		} break;
		case FSParser::Node::VARIABLE: {
			const FSParser::VariableNode *variable = static_cast<const FSParser::VariableNode *>(p_statement);
			// The initializer is evaluated before the slot is filled, so a final reading itself (or any
			// still-unassigned final) there is use-before-assignment.
			check_final_reads_in_expression(variable->initializer, p_finals, p_finals_by_name, p_scope, r_state, p_flattened_trait_body);
			// A local `final var` declaration is itself the assignment slot: a declaration initializer
			// fills it on the spot, while a blank `final var x` stays open until a later assignment.
			// (Member and static finals are pre-collected and seeded before their walk, so their
			// declarations are not re-encountered as statements here.)
			if (p_scope == FinalAssignmentScope::LOCAL && variable->is_final && p_finals.has(variable) && variable->initializer != nullptr) {
				r_state.assigned.insert(variable);
				r_state.maybe_assigned.insert(variable);
				r_assigned_anywhere.insert(variable);
			}
		} break;
		case FSParser::Node::ASSERT: {
			const FSParser::AssertNode *assert_node = static_cast<const FSParser::AssertNode *>(p_statement);
			check_final_reads_in_expression(assert_node->condition, p_finals, p_finals_by_name, p_scope, r_state, p_flattened_trait_body);
			check_final_reads_in_expression(assert_node->message, p_finals, p_finals_by_name, p_scope, r_state, p_flattened_trait_body);
		} break;
		case FSParser::Node::SUITE: {
			analyze_final_definite_assignment_suite(static_cast<const FSParser::SuiteNode *>(p_statement), p_finals, p_finals_by_name, p_scope, r_state, r_assigned_anywhere, p_flattened_trait_body);
		} break;
		default: {
			if (p_statement->is_expression()) {
				check_final_reads_in_expression(static_cast<const FSParser::ExpressionNode *>(p_statement), p_finals, p_finals_by_name, p_scope, r_state, p_flattened_trait_body);
				// A call that never returns (e.g. `push_fatal`) terminates the path, mirroring how
				// the analyzer treats `@noreturn` functions elsewhere.
				if (p_statement->type == FSParser::Node::CALL && static_cast<const FSParser::CallNode *>(p_statement)->is_noreturn) {
					r_state.reachable = false;
				}
			}
		} break;
	}
}

void FSAnalyzer::FlowFinalityContext::analyze_final_definite_assignment_suite(const FSParser::SuiteNode *p_suite,
		const HashSet<const FSParser::VariableNode *> &p_finals,
		const HashMap<StringName, const FSParser::VariableNode *> &p_finals_by_name, FinalAssignmentScope p_scope, FinalAssignmentState &r_state,
		HashSet<const FSParser::VariableNode *> &r_assigned_anywhere, bool p_flattened_trait_body) {
	if (p_suite == nullptr) {
		return;
	}
	for (int i = 0; i < p_suite->statements.size(); i++) {
		analyze_final_definite_assignment_statement(p_suite->statements[i], p_finals, p_finals_by_name, p_scope, r_state, r_assigned_anywhere, p_flattened_trait_body);
	}
}

static bool _is_null_literal(const FSParser::ExpressionNode *p_expression) {
	if (p_expression == nullptr || p_expression->type != FSParser::Node::LITERAL) {
		return false;
	}

	const FSParser::LiteralNode *literal = static_cast<const FSParser::LiteralNode *>(p_expression);
	return literal->value.get_type() == Variant::NIL;
}

static bool _match_pattern_is_null_literal(const FSParser::PatternNode *p_pattern) {
	return p_pattern != nullptr &&
			p_pattern->pattern_type == FSParser::PatternNode::PT_LITERAL &&
			p_pattern->literal != nullptr &&
			_is_null_literal(p_pattern->literal);
}

static bool _match_branch_accepts_null(const FSParser::MatchBranchNode *p_branch) {
	if (p_branch == nullptr) {
		return false;
	}
	if (p_branch->has_wildcard) {
		return true;
	}
	for (const FSParser::PatternNode *pattern : p_branch->patterns) {
		if (_match_pattern_is_null_literal(pattern)) {
			return true;
		}
	}
	return false;
}

static bool _match_pattern_type_narrowing(const FSParser::PatternNode *p_pattern, FSParser::ExpressionNode *p_match_test, FSParser::DataType &r_type) {
	if (p_pattern == nullptr || p_pattern->pattern_type != FSParser::PatternNode::PT_EXPRESSION) {
		return false;
	}

	const FSParser::ExpressionNode *expression = p_pattern->expression;
	if (expression == nullptr) {
		return false;
	}

	if (expression->type == FSParser::Node::TYPE_TEST) {
		const FSParser::TypeTestNode *type_test = static_cast<const FSParser::TypeTestNode *>(expression);
		if (p_match_test == nullptr || p_match_test->type != FSParser::Node::IDENTIFIER ||
				type_test->operand == nullptr || type_test->operand->type != FSParser::Node::IDENTIFIER ||
				!type_test->test_datatype.is_set()) {
			return false;
		}

		const FSParser::IdentifierNode *match_identifier = static_cast<const FSParser::IdentifierNode *>(p_match_test);
		const FSParser::IdentifierNode *pattern_operand = static_cast<const FSParser::IdentifierNode *>(type_test->operand);
		if (pattern_operand->name != match_identifier->name) {
			return false;
		}

		r_type = type_test->test_datatype;
		r_type.is_meta_type = false;
		return true;
	}

	if (!expression->is_constant && !expression->get_datatype().is_meta_type) {
		return false;
	}

	const FSParser::DataType &pattern_type = expression->get_datatype();
	if (!pattern_type.is_set()) {
		return false;
	}

	switch (pattern_type.kind) {
		case FSParser::DataType::CLASS:
		case FSParser::DataType::NATIVE:
		case FSParser::DataType::SCRIPT:
			r_type = pattern_type;
			r_type.is_meta_type = false;
			return true;
		case FSParser::DataType::BUILTIN:
			if (pattern_type.builtin_type == Variant::OBJECT) {
				r_type = pattern_type;
				return true;
			}
			break;
		default:
			break;
	}
	return false;
}

static bool _match_branch_type_narrowing(FSParser::ExpressionNode *p_match_test, const FSParser::MatchBranchNode *p_branch, FSParser::DataType &r_type) {
	if (p_branch == nullptr || p_branch->has_wildcard || p_branch->patterns.size() != 1) {
		return false;
	}
	return _match_pattern_type_narrowing(p_branch->patterns[0], p_match_test, r_type);
}
const FSParser::Node *FSAnalyzer::FlowFinalityContext::flow_narrowing_key_from_identifier(const FSParser::IdentifierNode *p_identifier) const {
	switch (p_identifier->source) {
		case FSParser::IdentifierNode::FUNCTION_PARAMETER:
			return p_identifier->parameter_source;
		case FSParser::IdentifierNode::LOCAL_VARIABLE:
			return p_identifier->variable_source;
		case FSParser::IdentifierNode::LOCAL_ITERATOR:
		case FSParser::IdentifierNode::LOCAL_BIND:
			return p_identifier->bind_source;
		case FSParser::IdentifierNode::UNDEFINED_SOURCE:
		case FSParser::IdentifierNode::LOCAL_CONSTANT:
		case FSParser::IdentifierNode::MEMBER_VARIABLE:
		case FSParser::IdentifierNode::MEMBER_CONSTANT:
		case FSParser::IdentifierNode::MEMBER_FUNCTION:
		case FSParser::IdentifierNode::MEMBER_SIGNAL:
		case FSParser::IdentifierNode::MEMBER_CLASS:
		case FSParser::IdentifierNode::INHERITED_VARIABLE:
		case FSParser::IdentifierNode::STATIC_VARIABLE:
		case FSParser::IdentifierNode::NATIVE_CLASS:
			return nullptr;
	}

	return nullptr;
}

const FSParser::DataType *FSAnalyzer::FlowFinalityContext::lookup_flow_narrowed_type(const FSParser::Node *p_key) const {
	if (p_key == nullptr) {
		return nullptr;
	}
	HashMap<const FSParser::Node *, FSParser::DataType>::ConstIterator found = flow_narrowed_types.find(p_key);
	if (!found) {
		return nullptr;
	}
	return &found->value;
}

void FSAnalyzer::FlowFinalityContext::apply_flow_narrowing(const FSParser::IdentifierNode *p_identifier) {
	const FSParser::Node *key = flow_narrowing_key_from_identifier(p_identifier);
	if (key == nullptr) {
		return;
	}

	FSParser::DataType narrowed_type = p_identifier->get_datatype();
	if (!narrowed_type.is_nullable) {
		return;
	}

	narrowed_type.is_nullable = false;
	flow_narrowed_types[key] = narrowed_type;
}

void FSAnalyzer::FlowFinalityContext::apply_flow_narrowing(const FSParser::IdentifierNode *p_identifier, const FSParser::DataType &p_type) {
	const FSParser::Node *key = flow_narrowing_key_from_identifier(p_identifier);
	if (key == nullptr || !p_type.is_set()) {
		return;
	}

	FSParser::DataType narrowed_type = p_type;
	narrowed_type.type_source = FSParser::DataType::ANNOTATED_INFERRED;
	flow_narrowed_types[key] = narrowed_type;
}

void FSAnalyzer::FlowFinalityContext::clear_flow_narrowing(const FSParser::ExpressionNode *p_expression) {
	if (p_expression == nullptr || p_expression->type != FSParser::Node::IDENTIFIER) {
		return;
	}

	const FSParser::IdentifierNode *identifier = static_cast<const FSParser::IdentifierNode *>(p_expression);
	const FSParser::Node *key = flow_narrowing_key_from_identifier(identifier);
	if (key != nullptr) {
		flow_narrowed_types.erase(key);
	}
}

void FSAnalyzer::FlowFinalityContext::mark_flow_narrowing_capture(const FSParser::IdentifierNode *p_identifier) {
	const FSParser::Node *key = flow_narrowing_key_from_identifier(p_identifier);
	if (key != nullptr) {
		flow_narrowing_captured_sources[key] = true;
	}
}

void FSAnalyzer::FlowFinalityContext::clear_captured_flow_narrowing() {
	for (const KeyValue<const FSParser::Node *, bool> &E : flow_narrowing_captured_sources) {
		flow_narrowed_types.erase(E.key);
	}
}

void FSAnalyzer::FlowFinalityContext::apply_match_branch_flow_narrowing(FSParser::ExpressionNode *p_match_test, FSParser::MatchBranchNode *p_match_branch) {
	if (p_match_test == nullptr || p_match_test->type != FSParser::Node::IDENTIFIER || p_match_branch == nullptr) {
		return;
	}

	FSParser::IdentifierNode *identifier = static_cast<FSParser::IdentifierNode *>(p_match_test);
	if (flow_narrowing_key_from_identifier(identifier) == nullptr) {
		return;
	}

	FSParser::DataType narrowed_type;
	if (_match_branch_type_narrowing(p_match_test, p_match_branch, narrowed_type)) {
		apply_flow_narrowing(identifier, narrowed_type);
		return;
	}

	if (identifier->get_datatype().is_nullable && !_match_branch_accepts_null(p_match_branch)) {
		apply_flow_narrowing(identifier);
	}
}

bool FSAnalyzer::FlowFinalityContext::null_check_narrowing_identifier(FSParser::ExpressionNode *p_condition, bool p_condition_value, FSParser::IdentifierNode *&r_identifier) const {
	r_identifier = nullptr;
	if (p_condition == nullptr || p_condition->type != FSParser::Node::BINARY_OPERATOR) {
		return false;
	}

	FSParser::BinaryOpNode *binary_op = static_cast<FSParser::BinaryOpNode *>(p_condition);
	if (binary_op->variant_op != Variant::OP_EQUAL && binary_op->variant_op != Variant::OP_NOT_EQUAL) {
		return false;
	}

	const bool condition_true_means_not_null = binary_op->variant_op == Variant::OP_NOT_EQUAL;
	if (p_condition_value != condition_true_means_not_null) {
		return false;
	}

	FSParser::ExpressionNode *candidate = nullptr;
	if (_is_null_literal(binary_op->left_operand)) {
		candidate = binary_op->right_operand;
	} else if (_is_null_literal(binary_op->right_operand)) {
		candidate = binary_op->left_operand;
	}
	if (candidate == nullptr || candidate->type != FSParser::Node::IDENTIFIER) {
		return false;
	}

	FSParser::IdentifierNode *identifier = static_cast<FSParser::IdentifierNode *>(candidate);
	if (flow_narrowing_key_from_identifier(identifier) == nullptr || !identifier->get_datatype().is_nullable) {
		return false;
	}

	r_identifier = identifier;
	return true;
}

bool FSAnalyzer::FlowFinalityContext::type_test_narrowing_identifier(FSParser::ExpressionNode *p_condition, bool p_condition_value, FSParser::IdentifierNode *&r_identifier, FSParser::DataType &r_type) const {
	r_identifier = nullptr;
	r_type = FSParser::DataType();

	bool condition_true_means_type_match = true;
	FSParser::ExpressionNode *condition = p_condition;
	if (condition != nullptr && condition->type == FSParser::Node::UNARY_OPERATOR) {
		FSParser::UnaryOpNode *unary_op = static_cast<FSParser::UnaryOpNode *>(condition);
		if (unary_op->variant_op != Variant::OP_NOT) {
			return false;
		}
		condition_true_means_type_match = false;
		condition = unary_op->operand;
	}

	if (p_condition_value != condition_true_means_type_match || condition == nullptr || condition->type != FSParser::Node::TYPE_TEST) {
		return false;
	}

	FSParser::TypeTestNode *type_test = static_cast<FSParser::TypeTestNode *>(condition);
	if (type_test->operand == nullptr || type_test->operand->type != FSParser::Node::IDENTIFIER || !type_test->test_datatype.is_set()) {
		return false;
	}

	FSParser::IdentifierNode *identifier = static_cast<FSParser::IdentifierNode *>(type_test->operand);
	if (flow_narrowing_key_from_identifier(identifier) == nullptr) {
		return false;
	}

	r_identifier = identifier;
	r_type = type_test->test_datatype;
	return true;
}

void FSAnalyzer::FlowFinalityContext::reduce_condition_expression(FSParser::ExpressionNode *p_condition) {
	if (p_condition == nullptr) {
		return;
	}

	if (p_condition->type == FSParser::Node::BINARY_OPERATOR) {
		FSParser::BinaryOpNode *binary_op = static_cast<FSParser::BinaryOpNode *>(p_condition);
		if (binary_op->variant_op == Variant::OP_AND) {
			reduce_condition_expression(binary_op->left_operand);

			HashMap<const FSParser::Node *, FSParser::DataType> previous_flow_narrowed_types = flow_narrowed_types;
			apply_flow_narrowing_from_condition(binary_op->left_operand, true);
			reduce_condition_expression(binary_op->right_operand);
			flow_narrowed_types = previous_flow_narrowed_types;

			FSParser::DataType left_type;
			if (binary_op->left_operand) {
				left_type = binary_op->left_operand->get_datatype();
			}
			FSParser::DataType right_type;
			if (binary_op->right_operand) {
				right_type = binary_op->right_operand->get_datatype();
			}

			if (!left_type.is_set() || !right_type.is_set()) {
				return;
			}

			bool valid = false;
			FSParser::DataType result = analyzer->get_operation_type(Variant::OP_AND, left_type, right_type, valid, binary_op);
			if (valid) {
				binary_op->set_datatype(result);
			}
			return;
		}
	}

	analyzer->reduce_expression(p_condition);
}

void FSAnalyzer::FlowFinalityContext::apply_flow_narrowing_from_condition(FSParser::ExpressionNode *p_condition, bool p_condition_value) {
	if (p_condition == nullptr) {
		return;
	}

	if (p_condition->type == FSParser::Node::BINARY_OPERATOR) {
		FSParser::BinaryOpNode *binary_op = static_cast<FSParser::BinaryOpNode *>(p_condition);
		if (binary_op->variant_op == Variant::OP_AND) {
			if (p_condition_value) {
				apply_flow_narrowing_from_condition(binary_op->left_operand, true);
				apply_flow_narrowing_from_condition(binary_op->right_operand, true);
			}
			return;
		}
	}

	FSParser::IdentifierNode *narrowed_identifier = nullptr;
	if (null_check_narrowing_identifier(p_condition, p_condition_value, narrowed_identifier)) {
		apply_flow_narrowing(narrowed_identifier);
		return;
	}

	FSParser::DataType narrowed_type;
	if (type_test_narrowing_identifier(p_condition, p_condition_value, narrowed_identifier, narrowed_type)) {
		apply_flow_narrowing(narrowed_identifier, narrowed_type);
	}
}
